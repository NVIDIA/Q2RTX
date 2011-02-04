/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

//
// net.c
//

#include "common.h"
#include "protocol.h"
#include "q_msg.h"
#include "q_fifo.h"
#include "net_sock.h"
#include "net_stream.h"
#ifdef _DEBUG
#include "files.h"
#endif
#include "sys_public.h"
#include "sv_public.h"
#include "cl_public.h"
#include "io_sleep.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#define socklen_t int
#define IOCTLSOCKET_PARAM u_long
#define SETSOCKOPT_PARAM BOOL
#ifdef _WIN32_WCE
#define NET_GET_ERROR()     ( net_error = GetLastError() )
#else
#define NET_GET_ERROR()     ( net_error = WSAGetLastError() )
#define NET_WOULD_BLOCK()   ( NET_GET_ERROR() == WSAEWOULDBLOCK )
#endif
#else // _WIN32
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#ifdef __linux__
#include <linux/types.h>
#if USE_ICMP
#include <linux/errqueue.h>
#endif
#endif // __linux__
#define SOCKET int
#define INVALID_SOCKET -1
#define closesocket close
#define ioctlsocket ioctl
#define IOCTLSOCKET_PARAM int
#define SETSOCKOPT_PARAM int
#define NET_GET_ERROR()     ( net_error = errno )
#define NET_WOULD_BLOCK()   ( NET_GET_ERROR() == EWOULDBLOCK )
#endif // !_WIN32

#if USE_CLIENT

#define    MAX_LOOPBACK    4

typedef struct {
    byte    data[MAX_PACKETLEN];
    size_t  datalen;
} loopmsg_t;

typedef struct {
    loopmsg_t   msgs[MAX_LOOPBACK];
    unsigned    get;
    unsigned    send;
} loopback_t;

static loopback_t   loopbacks[NS_COUNT];

#endif

cvar_t          *net_ip;
cvar_t          *net_port;

netadr_t        net_from;

#if USE_CLIENT
static cvar_t   *net_clientport;
static cvar_t   *net_dropsim;
#endif

#ifdef _DEBUG
static cvar_t   *net_log_enable;
static cvar_t   *net_log_name;
static cvar_t   *net_log_flush;
#endif

static cvar_t   *net_tcp_ip;
static cvar_t   *net_tcp_port;
static cvar_t   *net_tcp_backlog;

#if USE_ICMP
static cvar_t   *net_ignore_icmp;
#endif

static netflag_t    net_active;
static int          net_error;

static const char   socketNames[NS_COUNT][8] = { "Client", "Server" };
static SOCKET       udp_sockets[NS_COUNT] = { INVALID_SOCKET, INVALID_SOCKET };
static SOCKET       tcp_socket = INVALID_SOCKET;
#ifdef _DEBUG
static qhandle_t    net_logFile;
#endif

// current rate measurement
static unsigned     net_rate_time;
static size_t       net_rate_rcvd;
static size_t       net_rate_sent;
static size_t       net_rate_dn;
static size_t       net_rate_up;

// lifetime statistics
static uint64_t     net_recv_errors;
static uint64_t     net_send_errors;
#if USE_ICMP
static uint64_t     net_icmp_errors;
#endif
static uint64_t     net_bytes_rcvd;
static uint64_t     net_bytes_sent;
static uint64_t     net_packets_rcvd;
static uint64_t     net_packets_sent;

//=============================================================================

/*
===================
NET_NetadrToSockadr
===================
*/
static void NET_NetadrToSockadr( const netadr_t *a, struct sockaddr_in *s ) {
    memset( s, 0, sizeof( *s ) );

    switch( a->type ) {
    case NA_BROADCAST:
        s->sin_family = AF_INET;
        s->sin_addr.s_addr = INADDR_BROADCAST;
        s->sin_port = a->port;
        break;
    case NA_IP:
        s->sin_family = AF_INET;
        s->sin_addr.s_addr = a->ip.u32;
        s->sin_port = a->port;
        break;
    default:
        Com_Error( ERR_FATAL, "%s: bad address type", __func__ );
        break;
    }
}

/*
===================
NET_SockadrToNetadr
===================
*/
static void NET_SockadrToNetadr( const struct sockaddr_in *s, netadr_t *a ) {
    memset( a, 0, sizeof( *a ) );

    a->type = NA_IP;
    a->ip.u32 = s->sin_addr.s_addr;
    a->port = s->sin_port;
}

/*
=============
NET_StringToSockaddr

localhost
idnewt
idnewt:28000
192.246.40.70
192.246.40.70:28000
=============
*/
static qboolean NET_StringToSockaddr( const char *s, struct sockaddr_in *sadr ) {
    struct hostent *h;
    char copy[MAX_QPATH], *p;
    int dots;

    memset( sadr, 0, sizeof( *sadr ) );

    sadr->sin_family = AF_INET;
    sadr->sin_port = 0;

    Q_strlcpy( copy, s, sizeof( copy ) );
    // strip off a trailing :port if present
    p = strchr( copy, ':' );
    if( p ) {
        *p = 0;
        sadr->sin_port = htons( ( u_short )atoi( p + 1 ) );
    }
    for( p = copy, dots = 0; *p; p++ ) {
        if( *p == '.' ) {
            dots++;
        } else if( !Q_isdigit( *p ) ) {
            break;
        }
    }
    if( *p == 0 && dots <= 3 ) {
        uint32_t addr = inet_addr( copy );

        if( addr == INADDR_NONE ) {
            return qfalse;
        }
        sadr->sin_addr.s_addr = addr;
    } else {
        if( !( h = gethostbyname( copy ) ) )
            return qfalse;
        sadr->sin_addr.s_addr = *( uint32_t * )h->h_addr_list[0];
    }

    return qtrue;
}


/*
===================
NET_AdrToString
===================
*/
char *NET_AdrToString( const netadr_t *a ) {
    static char s[MAX_QPATH];
    const uint8_t *ip;

    switch( a->type ) {
    case NA_LOOPBACK:
        strcpy( s, "loopback" );
        return s;
    case NA_IP:
    case NA_BROADCAST:
        ip = a->ip.u8;
        Q_snprintf( s, sizeof( s ), "%u.%u.%u.%u:%u",
            ip[0], ip[1], ip[2], ip[3], ntohs( a->port ) );
        return s;
    default:
        Com_Error( ERR_FATAL, "%s: bad address type", __func__ );
        break;
    }

    return NULL;
}

/*
=============
NET_StringToAdr

localhost
idnewt
idnewt:28000
192.246.40.70
192.246.40.70:28000
=============
*/
qboolean NET_StringToAdr( const char *s, netadr_t *a, int port ) {
    struct sockaddr_in sadr;
    
    if( !NET_StringToSockaddr( s, &sadr ) ) {
        return qfalse;
    }
    
    NET_SockadrToNetadr( &sadr, a );

    if( !a->port ) {
        a->port = BigShort( port );
    }

    return qtrue;
}

//=============================================================================

#ifdef _DEBUG

static void logfile_close( void ) {
    if( !net_logFile ) {
        return;
    }

    Com_Printf( "Closing network log.\n" );

    FS_FCloseFile( net_logFile );
    net_logFile = 0;
}

static void logfile_open( void ) {
    char buffer[MAX_OSPATH];
    unsigned mode;
    qhandle_t f;

    mode = net_log_enable->integer > 1 ? FS_MODE_APPEND : FS_MODE_WRITE;
    if( net_log_flush->integer ) {
        mode |= FS_FLUSH_SYNC;
    }

    f = FS_EasyOpenFile( buffer, sizeof( buffer ), mode,
        "logs/", net_log_name->string, ".log" );
    if( !f ) {
        Cvar_Set( "net_log_enable", "0" );
        return;
    }

    net_logFile = f;
    Com_Printf( "Logging network packets to %s\n", buffer );
}

static void net_log_enable_changed( cvar_t *self ) {
    logfile_close();
    if( self->integer ) {
        logfile_open();
    }    
}

static void net_log_param_changed( cvar_t *self ) {
    if( net_log_enable->integer ) {
        logfile_close();
        logfile_open();
    }    
}

/*
=============
NET_LogPacket
=============
*/
static void NET_LogPacket( const netadr_t *address, const char *prefix,
                           const byte *data, size_t length )
{
    int numRows;
    int i, j, c;

    if( !net_logFile ) {
        return;
    }

    FS_FPrintf( net_logFile, "%s : %s\n", prefix, NET_AdrToString( address ) );

    numRows = ( length + 15 ) / 16;
    for( i = 0; i < numRows; i++ ) {
        FS_FPrintf( net_logFile, "%04x : ", i * 16 );
        for( j = 0; j < 16; j++ ) {
            if( i * 16 + j < length ) {
                FS_FPrintf( net_logFile, "%02x ", data[i * 16 + j] );
            } else {
                FS_FPrintf( net_logFile, "   " );
            }
        }
        FS_FPrintf( net_logFile, ": " );
        for( j = 0; j < 16; j++ ) {
            if( i * 16 + j < length ) {
                c = data[i * 16 + j];
                FS_FPrintf( net_logFile, "%c", ( c < 32 || c > 127 ) ? '.' : c );
            } else {
                FS_FPrintf( net_logFile, " " );
            }
        }
        FS_FPrintf( net_logFile, "\n" );
    }

    FS_FPrintf( net_logFile, "\n" );
}

#endif

#define RATE_SECS    3

static void NET_UpdateStats( void ) {
    unsigned diff;

    if( net_rate_time > com_eventTime ) {
        net_rate_time = com_eventTime;
    }
    diff = com_eventTime - net_rate_time;
    if( diff < RATE_SECS * 1000 ) {
        return;
    }
    net_rate_time = com_eventTime;

    net_rate_dn = net_rate_rcvd / RATE_SECS;
    net_rate_up = net_rate_sent / RATE_SECS;
    net_rate_sent = 0;
    net_rate_rcvd = 0;
}


#if USE_CLIENT

/*
=============
NET_GetLoopPacket
=============
*/
qboolean NET_GetLoopPacket( netsrc_t sock ) {
    loopback_t *loop;
    loopmsg_t *loopmsg;

    NET_UpdateStats();

    loop = &loopbacks[sock];

    if( loop->send - loop->get > MAX_LOOPBACK - 1 ) {
        loop->get = loop->send - MAX_LOOPBACK + 1;
    }

    if( loop->get >= loop->send ) {
        return qfalse;
    }

    loopmsg = &loop->msgs[loop->get & (MAX_LOOPBACK-1)];
    loop->get++;

    memcpy( msg_read_buffer, loopmsg->data, loopmsg->datalen );

#ifdef _DEBUG
    if( net_log_enable->integer ) {
        NET_LogPacket( &net_from, "LP recv", loopmsg->data, loopmsg->datalen );
    }
#endif
    if( sock == NS_CLIENT ) {
        net_rate_rcvd += loopmsg->datalen;
    }

    SZ_Init( &msg_read, msg_read_buffer, sizeof( msg_read_buffer ) );
    msg_read.cursize = loopmsg->datalen;

    return qtrue;
}

#endif

#if USE_ICMP

// prevents infinite retry loops caused by broken TCP/IP stacks
#define MAX_ERROR_RETRIES   64

static void icmp_error_event( netsrc_t sock, int info ) {
    if( net_ignore_icmp->integer > 0 ) {
        return;
    }

    Com_DPrintf( "%s: %s from %s\n", __func__,
        NET_ErrorString(), NET_AdrToString( &net_from ) );
    net_icmp_errors++;

    switch( sock ) {
    case NS_SERVER:
        SV_ErrorEvent( info );
        break;
#if USE_CLIENT
    case NS_CLIENT:
        CL_ErrorEvent();
        break;
#endif
    default:
        break;
    }
}

#ifdef __linux__

// Linux at least supports receiving ICMP errors on unconnected UDP sockets
// via IP_RECVERR cruft below... What about BSD?
//
// Returns true if failed socket operation should be retried, extremely hacky :/
static qboolean process_error_queue( netsrc_t sock, struct sockaddr_in *to ) {
    byte buffer[1024];
    struct sockaddr_in from;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    struct sock_extended_err *ee;
    int info;
    int tries;
    qboolean found;

    tries = 0;
    found = qfalse;

retry:
    memset( &from, 0, sizeof( from ) );

    memset( &msg, 0, sizeof( msg ) );
    msg.msg_name = &from;
    msg.msg_namelen = sizeof( from );
    msg.msg_control = buffer;
    msg.msg_controllen = sizeof( buffer );

    if( recvmsg( udp_sockets[sock], &msg, MSG_ERRQUEUE ) == -1 ) {
        if( NET_WOULD_BLOCK() ) {
            // wouldblock is silent
            goto finish;
        }
        Com_EPrintf( "%s: %s\n", __func__, NET_ErrorString() );
        goto finish;
    }

    if( !( msg.msg_flags & MSG_ERRQUEUE ) ) {
        Com_DPrintf( "%s: no extended error received\n", __func__ );
        goto finish;
    }

    // find an ICMP error message
    for( cmsg = CMSG_FIRSTHDR( &msg );
        cmsg != NULL;
        cmsg = CMSG_NXTHDR( &msg, cmsg ) ) 
    {
        if( cmsg->cmsg_level != IPPROTO_IP ) {
            continue;
        }
        if( cmsg->cmsg_type != IP_RECVERR ) {
            continue;
        }
        ee = ( struct sock_extended_err * )CMSG_DATA( cmsg );
        if( ee->ee_origin == SO_EE_ORIGIN_ICMP ) {
            break;
        }
    }

    if( !cmsg ) {
        Com_DPrintf( "%s: no ICMP error found\n", __func__ );
        goto finish;
    }

    NET_SockadrToNetadr( &from, &net_from );

    // check if this error was caused by a packet sent to the given address
    // if so, do not retry send operation to prevent infinite loop
    if( to != NULL && from.sin_addr.s_addr == to->sin_addr.s_addr &&
        ( !from.sin_port || from.sin_port == to->sin_port ) )
    {
        Com_DPrintf( "%s: found offending address %s:%d\n",
            __func__, inet_ntoa( from.sin_addr ), BigShort( from.sin_port ) );
        found = qtrue;
    }

    // handle ICMP errors
    net_error = ee->ee_errno;
    info = 0;
#if USE_PMTUDISC
    // for EMSGSIZE ee_info should hold discovered MTU
    if( net_error == EMSGSIZE && ee->ee_info >= 576 && ee->ee_info < 4096 ) {
        info = ee->ee_info;
    }
#endif
    icmp_error_event( sock, info );

    if( ++tries < MAX_ERROR_RETRIES ) {
        goto retry;
    }

finish:
    return !!tries && !found;
}

#endif // __linux__

#endif // USE_ICMP

/*
=============
NET_GetPacket

Fills msg_read_buffer with packet contents,
net_from variable receives source address.
=============
*/
qboolean NET_GetPacket( netsrc_t sock ) {
    struct sockaddr_in from;
    socklen_t fromlen;
    int ret;
#if USE_ICMP
    int tries;
#ifndef _WIN32
    int saved_error;
#endif
#endif
    ioentry_t *e;

    if( udp_sockets[sock] == INVALID_SOCKET ) {
        return qfalse;
    }

    NET_UpdateStats();

    e = IO_Get( udp_sockets[sock] );
    if( !e->canread ) {
        return qfalse;
    }

#if USE_ICMP
    tries = 0;

retry:
#endif
    memset( &from, 0, sizeof( from ) );

    fromlen = sizeof( from );
    ret = recvfrom( udp_sockets[sock], ( void * )msg_read_buffer,
        MAX_PACKETLEN, 0, ( struct sockaddr * )&from, &fromlen );

    if( !ret ) {
        return qfalse;
    }

    NET_SockadrToNetadr( &from, &net_from );

    if( ret == -1 ) {
        NET_GET_ERROR();

#ifdef _WIN32
        switch( net_error ) {
        case WSAEWOULDBLOCK:
            // wouldblock is silent
            e->canread = qfalse;
            break;
#if USE_ICMP
        case WSAECONNRESET:
        case WSAENETRESET:
            // winsock has already provided us with
            // a valid address from ICMP error packet
            icmp_error_event( sock, 0 );
            if( ++tries < MAX_ERROR_RETRIES ) {
                goto retry;
            }
            // intentional fallthrough
#endif // USE_ICMP
        default:
            Com_DPrintf( "%s: %s from %s\n", __func__,
                NET_ErrorString(), NET_AdrToString( &net_from ) );
            net_recv_errors++;
            break;
        }
#else // _WIN32
        switch( net_error ) {
        case EWOULDBLOCK:
            // wouldblock is silent
            e->canread = qfalse;
            break;
        default:
#if USE_ICMP
            saved_error = net_error;
            // recvfrom() fails on Linux if there's an ICMP originated
            // pending error on socket. suck up error queue and retry...
            if( process_error_queue( sock, NULL ) ) {
                if( ++tries < MAX_ERROR_RETRIES ) {
                    goto retry;
                }
            }
            net_error = saved_error;
#endif
            Com_DPrintf( "%s: %s from %s\n", __func__,
                NET_ErrorString(), NET_AdrToString( &net_from ) );
            net_recv_errors++;
            break;
        }
#endif // !_WIN32
        return qfalse;
    }

    if( ret > MAX_PACKETLEN ) {
        Com_EPrintf( "%s: oversize packet from %s\n", __func__,
            NET_AdrToString( &net_from ) );
        return qfalse;
    }

#ifdef _DEBUG
    if( net_log_enable->integer ) {
        NET_LogPacket( &net_from, "UDP recv", msg_read_buffer, ret );
    }
#endif

    SZ_Init( &msg_read, msg_read_buffer, sizeof( msg_read_buffer ) );
    msg_read.cursize = ret;
    net_rate_rcvd += ret;
    net_bytes_rcvd += ret;
    net_packets_rcvd++;

    return qtrue;
}

//=============================================================================

/*
=============
NET_SendPacket

=============
*/
qboolean NET_SendPacket( netsrc_t sock, const netadr_t *to, size_t length, const void *data ) {
    struct sockaddr_in addr;
    int ret;
#if USE_ICMP && ( defined __linux__ )
    int tries;
    int saved_error;
#endif

    if( !length ) {
        return qfalse;
    }

    if( length > MAX_PACKETLEN ) {
        Com_EPrintf( "%s: oversize packet to %s\n", __func__,
            NET_AdrToString( to ) );
        return qfalse;
    }

    switch( to->type ) {
#if USE_CLIENT
    case NA_LOOPBACK: {
            loopback_t *loop;
            loopmsg_t *msg;

            if( net_dropsim->integer > 0 &&
                ( rand() % 100 ) < net_dropsim->integer )
            {
                return NET_AGAIN;
            }
    
            loop = &loopbacks[sock ^ 1];

            msg = &loop->msgs[loop->send & ( MAX_LOOPBACK - 1 )];
            loop->send++;
            
            memcpy( msg->data, data, length );
            msg->datalen = length;

#ifdef _DEBUG
            if( net_log_enable->integer ) {
                NET_LogPacket( to, "LB send", data, length );
            }
#endif
            if( sock == NS_CLIENT ) {
                net_rate_sent += length;
            }
        }
        return qtrue;
#endif
    case NA_IP:
    case NA_BROADCAST:
        break;
    default:
        Com_Error( ERR_FATAL, "%s: bad address type", __func__ );
        break;
    }

    if( udp_sockets[sock] == INVALID_SOCKET ) {
        return qfalse;
    }

    NET_NetadrToSockadr( to, &addr );

#if USE_ICMP && ( defined __linux__ )
    tries = 0;

retry:
#endif
    ret = sendto( udp_sockets[sock], data, length, 0,
        ( struct sockaddr * )&addr, sizeof( addr ) );
    if( ret == -1 ) {
        NET_GET_ERROR();

#ifdef _WIN32
        switch( net_error ) {
        case WSAEWOULDBLOCK:
        case WSAEINTR:
            // wouldblock is silent
            break;
        case WSAEADDRNOTAVAIL:
            // some PPP links do not allow broadcasts
            if( to->type == NA_BROADCAST ) {
                break;
            }
            // intentional fallthrough
        default:
            Com_DPrintf( "%s: %s to %s\n", __func__,
                NET_ErrorString(), NET_AdrToString( to ) );
            net_send_errors++;
            break;
        }
#else // _WIN32
        switch( net_error ) {
        case EWOULDBLOCK:
            // wouldblock is silent
            break;
        default:
#if USE_ICMP
            saved_error = net_error;
            // sendto() fails on Linux if there's an ICMP originated
            // pending error on socket. suck up error queue and retry...
            //
            // this one is especially lame - how do I distingiush between
            // a failure caused by completely unrelated ICMP error sitting
            // in the queue and an error explicit to this sendto() call?
            // 
            // on one hand, I don't want to drop packets to legitimate
            // clients because of this, and have to retry sendto() after
            // processing error queue, on another hand, infinite loop should be
            // avoided if this sendto() regenerates a message in error queue
            //
            // this mess is worked around by passing destination address
            // to process_error_queue() and checking if this address/port
            // pair is found in the queue. if it is found, or the queue
            // is empty, do not retry
            if( process_error_queue( sock, &addr ) ) {
                if( ++tries < MAX_ERROR_RETRIES ) {
                    goto retry;
                }
            }
            net_error = saved_error;
#endif
            Com_DPrintf( "%s: %s to %s\n", __func__,
                NET_ErrorString(), NET_AdrToString( to ) );
            net_send_errors++;
            break;
        }
#endif // !_WIN32
        return qfalse;
    }

    if( ret != length ) {
        Com_WPrintf( "%s: short send to %s\n", __func__,
            NET_AdrToString( to ) );
    }

#ifdef _DEBUG
    if( net_log_enable->integer ) {
        NET_LogPacket( to, "UDP send", data, ret );
    }
#endif
    net_rate_sent += ret;
    net_bytes_sent += ret;
    net_packets_sent++;

    return qtrue;
}


//=============================================================================

const char *NET_ErrorString( void ) {
#ifdef _WIN32
    switch( net_error ) {
    case S_OK:
        return "NO ERROR";
    default:
        return "UNKNOWN ERROR";
#include "wsaerr.h"
    }
#else
    return strerror( net_error );
#endif
}

static qboolean get_bind_addr( const char *iface, int port, struct sockaddr_in *sadr ) {
    if( *iface ) {
        if( !NET_StringToSockaddr( iface, sadr ) ) {
            return qfalse;
        }
    } else {
        // empty string binds to all interfaces 
        memset( sadr, 0, sizeof( *sadr ) );
        sadr->sin_family = AF_INET;
        sadr->sin_addr.s_addr = INADDR_ANY;
    }
    if( port != PORT_ANY ) {
        sadr->sin_port = htons( ( u_short )port );
    }

    return qtrue;
}

static SOCKET create_socket( int type, int proto ) {
    SOCKET ret = socket( PF_INET, type, proto );

    NET_GET_ERROR();
    return ret;
}

static int set_option( SOCKET s, int level, int optname, int value ) {
    SETSOCKOPT_PARAM _value = value;
    int ret = setsockopt( s, level, optname, ( char * )&_value, sizeof( _value ) );

    NET_GET_ERROR();
    return ret;
}

#define enable_option(s,level,optname)  set_option(s,level,optname,1)

static int make_nonblock( SOCKET s ) {
    IOCTLSOCKET_PARAM _true = 1;
    int ret = ioctlsocket( s, FIONBIO, &_true );

    NET_GET_ERROR();
    return ret;
}

static int bind_socket( SOCKET s, struct sockaddr_in *sadr ) {
    int ret = bind( s, ( struct sockaddr * )sadr, sizeof( *sadr ) );

    NET_GET_ERROR();
    return ret;
}

static SOCKET UDP_OpenSocket( const char *iface, int port ) {
    SOCKET              s;
    struct sockaddr_in  sadr;
#ifdef __linux__
    int                 pmtudisc;
#endif

    Com_DPrintf( "Opening UDP socket: %s:%i\n", iface, port );

    s = create_socket( SOCK_DGRAM, IPPROTO_UDP );
    if( s == INVALID_SOCKET ) {
        Com_EPrintf( "%s: %s:%d: can't create socket: %s\n",
            __func__, iface, port, NET_ErrorString() );
        return INVALID_SOCKET;
    }

    // make it non-blocking
    if( make_nonblock( s ) == -1 ) {
        Com_EPrintf( "%s: %s:%d: can't make socket non-blocking: %s\n",
            __func__, iface, port, NET_ErrorString() );
        goto fail;
    }

    // make it broadcast capable
    if( enable_option( s, SOL_SOCKET, SO_BROADCAST ) == -1 ) {
        Com_WPrintf( "%s: %s:%d: can't make socket broadcast capable: %s\n",
            __func__, iface, port, NET_ErrorString() );
    }

#ifdef __linux__
    pmtudisc = IP_PMTUDISC_DONT;

#if USE_ICMP
    // enable ICMP error queue
    if( net_ignore_icmp->integer <= 0 ) {
        if( enable_option( s, IPPROTO_IP, IP_RECVERR ) == -1 ) {
            Com_WPrintf( "%s: %s:%d: can't enable ICMP error queue: %s\n",
                __func__, iface, port, NET_ErrorString() );
            Cvar_Set( "net_ignore_icmp", "1" );
        }

#if USE_PMTUDISC
        // overload negative values to enable path MTU discovery
        switch( net_ignore_icmp->integer ) {
            case -1: pmtudisc = IP_PMTUDISC_WANT; break;
            case -2: pmtudisc = IP_PMTUDISC_DO; break;
#ifdef IP_PMTUDISC_PROBE
            case -3: pmtudisc = IP_PMTUDISC_PROBE; break;
#endif
        }
#endif // USE_PMTUDISC
    }
#endif // USE_ICMP

    // disable or enable path MTU discovery
    if( set_option( s, IPPROTO_IP, IP_MTU_DISCOVER, pmtudisc ) == -1 ) {
        Com_WPrintf( "%s: %s:%d: can't %sable path MTU discovery: %s\n",
            __func__, iface, port, pmtudisc == IP_PMTUDISC_DONT ? "dis" : "en",
            NET_ErrorString() );
    }

#endif // __linux__

    // resolve iface sadr
    if( !get_bind_addr( iface, port, &sadr ) ) {
        Com_EPrintf( "%s: %s:%d: bad interface address\n",
            __func__, iface, port );
        goto fail;
    }

    if( bind_socket( s, &sadr ) == -1 ) {
        Com_EPrintf( "%s: %s:%d: can't bind socket: %s\n",
            __func__, iface, port, NET_ErrorString() );
        goto fail;
    }

    return s;

fail:
    closesocket( s );
    return INVALID_SOCKET;
}

static SOCKET TCP_OpenSocket( const char *iface, int port, netsrc_t who ) {
    SOCKET              s;
    struct sockaddr_in  sadr;

    Com_DPrintf( "Opening TCP socket: %s:%i\n", iface, port );

    s = create_socket( SOCK_STREAM, IPPROTO_TCP );
    if( s == INVALID_SOCKET ) {
        Com_EPrintf( "%s: %s:%d: can't create socket: %s\n",
            __func__, iface, port, NET_ErrorString() );
        return INVALID_SOCKET;
    }

    // make it non-blocking
    if( make_nonblock( s ) == -1 ) {
        Com_EPrintf( "%s: %s:%d: can't make socket non-blocking: %s\n",
            __func__, iface, port, NET_ErrorString() );
        goto fail;
    }

    // give it a chance to reuse previous port
    if( who == NS_SERVER ) {
        if( enable_option( s, SOL_SOCKET, SO_REUSEADDR ) == -1 ) {
            Com_WPrintf( "%s: %s:%d: can't force socket to reuse address: %s\n",
                __func__, iface, port, NET_ErrorString() );
        }
    }

    if( !get_bind_addr( iface, port, &sadr ) ) {
        Com_EPrintf( "%s: %s:%d: bad interface address\n",
            __func__, iface, port );
        goto fail;
    }

    if( bind_socket( s, &sadr ) == -1 ) {
        Com_EPrintf( "%s: %s:%d: can't bind socket: %s\n",
            __func__, iface, port, NET_ErrorString() );
        goto fail;
    }

    return s;

fail:
    closesocket( s );
    return INVALID_SOCKET;
}

static void NET_OpenServer( void ) {
    static int saved_port;
    ioentry_t *e;
    SOCKET s;

    s = UDP_OpenSocket( net_ip->string, net_port->integer );
    if( s != INVALID_SOCKET ) {
        saved_port = net_port->integer;
        udp_sockets[NS_SERVER] = s;
        e = IO_Add( s );
        e->wantread = qtrue;
        return;
    }

    if( saved_port && saved_port != net_port->integer ) {
        // revert to the last valid port
        Com_Printf( "Reverting to the last valid port %d...\n", saved_port );
        Cbuf_AddText( &cmd_buffer, va( "set net_port %d\n", saved_port ) );
        return;
    }

#if USE_CLIENT
    if( !dedicated->integer ) {
        Com_WPrintf( "Couldn't open server UDP port.\n" );
        return;
    }
#endif

    Com_Error( ERR_FATAL, "Couldn't open dedicated server UDP port" );
}

#if USE_CLIENT
static void NET_OpenClient( void ) {
    ioentry_t *e;
    SOCKET s;
    struct sockaddr_in sadr;
    socklen_t len;

    s = UDP_OpenSocket( net_ip->string, net_clientport->integer );
    if( s == INVALID_SOCKET ) {
        // now try with random port
        if( net_clientport->integer != PORT_ANY ) {
            s = UDP_OpenSocket( net_ip->string, PORT_ANY );
        }
        if( s == INVALID_SOCKET ) {
            Com_WPrintf( "Couldn't open client UDP port.\n" );
            return;
        }
        len = sizeof( sadr );
        getsockname( s, ( struct sockaddr * )&sadr, &len );
        Com_WPrintf( "Client bound to UDP port %d.\n", ntohs( sadr.sin_port ) );
        Cvar_SetByVar( net_clientport, va( "%d", PORT_ANY ), FROM_CODE );
    }

    udp_sockets[NS_CLIENT] = s;
    e = IO_Add( s );
    e->wantread = qtrue;
}
#endif

//=============================================================================

void NET_Close( netstream_t *s ) {
    if( !s->state ) {
        return;
    }

    IO_Remove( s->socket );
    closesocket( s->socket );
    s->socket = INVALID_SOCKET;
    s->state = NS_DISCONNECTED;
}

neterr_t NET_Listen( qboolean arg ) {
    SOCKET s;
    ioentry_t *e;

    if( !arg ) {
        if( tcp_socket != INVALID_SOCKET ) {
            IO_Remove( tcp_socket );
            closesocket( tcp_socket );
            tcp_socket = INVALID_SOCKET;
        }
        return NET_OK;
    }

    if( tcp_socket != INVALID_SOCKET ) {
        return NET_OK;
    }

    s = TCP_OpenSocket( net_tcp_ip->string,
        net_tcp_port->integer, NS_SERVER );
    if( s == INVALID_SOCKET ) {
        return NET_ERROR;
    }
    if( listen( s, net_tcp_backlog->integer ) == -1 ) {
        NET_GET_ERROR();
        closesocket( s );
        return NET_ERROR;
    }

    tcp_socket = s;
    e = IO_Add( s );
    e->wantread = qtrue;

    return NET_OK;
}

// net_from variable receives source address
neterr_t NET_Accept( netstream_t *s ) {
    struct sockaddr_in from;
    socklen_t fromlen;
    SOCKET newsocket;
    ioentry_t *e;

    if( tcp_socket == INVALID_SOCKET ) {
        return NET_AGAIN;
    }

    e = IO_Get( tcp_socket );
    if( !e->canread ) {
        return NET_AGAIN;
    }

    memset( &from, 0, sizeof( from ) );
    fromlen = sizeof( from );
    newsocket = accept( tcp_socket, ( struct sockaddr * )&from, &fromlen );

    NET_SockadrToNetadr( &from, &net_from );

    if( newsocket == -1 ) {
        if( NET_WOULD_BLOCK() ) {
            // wouldblock is silent
            e->canread = qfalse;
            return NET_AGAIN;
        }
        return NET_ERROR;
    }

    // make it non-blocking
    if( make_nonblock( newsocket ) == -1 ) {
        closesocket( newsocket );
        return NET_ERROR;
    }

    // initialize stream
    memset( s, 0, sizeof( *s ) );
    s->socket = newsocket;
    s->address = net_from;
    s->state = NS_CONNECTED;

    // initialize io entry
    e = IO_Add( newsocket );
    //e->wantwrite = qtrue;
    e->wantread = qtrue;

    return NET_OK;
}

neterr_t NET_Connect( const netadr_t *peer, netstream_t *s ) {
    SOCKET socket;
    ioentry_t *e;
    struct sockaddr_in sadr;
    int ret;

    // always bind to `net_ip' for outgoing TCP connections
    // to avoid problems with AC or MVD/GTV auth on a multi IP system
    socket = TCP_OpenSocket( net_ip->string, PORT_ANY, NS_CLIENT );
    if( socket == INVALID_SOCKET ) {
        return NET_ERROR;
    }

    NET_NetadrToSockadr( peer, &sadr );

    ret = connect( socket, ( struct sockaddr * )&sadr, sizeof( sadr ) );
    if( ret == -1 ) {
#ifdef _WIN32
        if( NET_GET_ERROR() != WSAEWOULDBLOCK ) {
#else
        if( NET_GET_ERROR() != EINPROGRESS ) {
#endif
            // wouldblock is silent
            closesocket( socket );
            return NET_ERROR;
        }
    }

    // initialize stream
    memset( s, 0, sizeof( *s ) );
    s->state = NS_CONNECTING;
    s->address = *peer;
    s->socket = socket;

    // initialize io entry
    e = IO_Add( socket );
    e->wantwrite = qtrue;
#ifdef _WIN32
    e->wantexcept = qtrue;
#endif

    return NET_OK;
}

neterr_t NET_RunConnect( netstream_t *s ) {
    socklen_t len;
    int ret, err;
    ioentry_t *e;

    if( s->state != NS_CONNECTING ) {
        return NET_AGAIN;
    }

    e = IO_Get( s->socket );
    if( !e->canwrite
#ifdef _WIN32
        && !e->canexcept
#endif
      )
    {
        return NET_AGAIN;
    }

    len = sizeof( err );
    ret = getsockopt( s->socket, SOL_SOCKET, SO_ERROR, ( char * )&err, &len );
    if( ret == -1 ) {
        goto error1;
    }
    if( err ) {
        net_error = err;
        goto error2;
    }

    e->wantwrite = qfalse;
    e->wantread = qtrue;
#ifdef _WIN32
    e->wantexcept = qfalse;
#endif
    s->state = NS_CONNECTED;
    return NET_OK;

error1:
    NET_GET_ERROR();
error2:
    s->state = NS_BROKEN;
    e->wantwrite = qfalse;
    e->wantread = qfalse;
#ifdef _WIN32
    e->wantexcept = qfalse;
#endif
    return NET_ERROR;
}

// updates wantread/wantwrite
void NET_UpdateStream( netstream_t *s ) {
    size_t len;
    ioentry_t *e;

    if( s->state != NS_CONNECTED ) {
        return;
    }

    e = IO_Get( s->socket );

    FIFO_Reserve( &s->recv, &len );
    e->wantread = len ? qtrue : qfalse;

    FIFO_Peek( &s->send, &len );
    e->wantwrite = len ? qtrue : qfalse;
}

// returns NET_OK only when there was some data read
neterr_t NET_RunStream( netstream_t *s ) {
    int ret;
    size_t len;
    void *data;
    neterr_t result = NET_AGAIN;
    ioentry_t *e;

    if( s->state != NS_CONNECTED ) {
        return result;
    }

    e = IO_Get( s->socket );
    if( e->wantread && e->canread ) {
        // read as much as we can
        data = FIFO_Reserve( &s->recv, &len );
        if( len ) {
            ret = recv( s->socket, data, len, 0 );
            if( !ret ) {
                goto closed;
            }
            if( ret == -1 ) {
                if( NET_WOULD_BLOCK() ) {
                    // wouldblock is silent
                    e->canread = qfalse;
                } else {
                    goto error;
                }
            } else {
                FIFO_Commit( &s->recv, ret );
#if _DEBUG
                if( net_log_enable->integer ) {
                    NET_LogPacket( &s->address, "TCP recv", data, ret );
                }
#endif
                net_rate_rcvd += ret;
                net_bytes_rcvd += ret;

                result = NET_OK;

                // now see if there's more space to read
                FIFO_Reserve( &s->recv, &len );
                if( !len ) {
                    e->wantread = qfalse;
                }
            }
        }
    }

    if( e->wantwrite && e->canwrite ) {
        // write as much as we can
        data = FIFO_Peek( &s->send, &len );
        if( len ) {
            ret = send( s->socket, data, len, 0 );
            if( !ret ) {
                goto closed;
            }
            if( ret == -1 ) {
                if( NET_WOULD_BLOCK() ) {
                    // wouldblock is silent
                    e->canwrite = qfalse;
                } else {
                    goto error;
                }
            } else {
                FIFO_Decommit( &s->send, ret );
#if _DEBUG
                if( net_log_enable->integer ) {
                    NET_LogPacket( &s->address, "TCP send", data, ret );
                }
#endif
                net_rate_sent += ret;
                net_bytes_sent += ret;

                //result = NET_OK;

                // now see if there's more data to write
                FIFO_Peek( &s->send, &len );
                if( !len ) {
                    e->wantwrite = qfalse;
                }

            }
        }
    }

    return result;

closed:
    s->state = NS_CLOSED;
    e->wantread = qfalse;
    return NET_CLOSED;

error:
    s->state = NS_BROKEN;
    e->wantread = qfalse;
    e->wantwrite = qfalse;
    return NET_ERROR;
}

//===================================================================

/*
====================
NET_Stats_f
====================
*/
static void NET_Stats_f( void ) {
    time_t diff, now = time( NULL );
    char buffer[MAX_QPATH];

    if( com_startTime > now ) {
        com_startTime = now;
    }
    diff = now - com_startTime;
    if( diff < 1 ) {
        diff = 1;
    }

    Com_FormatTime( buffer, sizeof( buffer ), diff );
    Com_Printf( "Network uptime: %s\n", buffer );
    Com_Printf( "Bytes sent: %"PRIu64" (%"PRIu64" bytes/sec)\n",
        net_bytes_sent, net_bytes_sent / diff );
    Com_Printf( "Bytes rcvd: %"PRIu64" (%"PRIu64" bytes/sec)\n",
        net_bytes_rcvd, net_bytes_rcvd / diff );
    Com_Printf( "Packets sent: %"PRIu64" (%"PRIu64" packets/sec)\n",
        net_packets_sent, net_packets_sent / diff );
    Com_Printf( "Packets rcvd: %"PRIu64" (%"PRIu64" packets/sec)\n",
        net_packets_rcvd, net_packets_rcvd / diff );
#if USE_ICMP
    Com_Printf( "Total errors: %"PRIu64"/%"PRIu64"/%"PRIu64" (send/recv/icmp)\n",
        net_send_errors, net_recv_errors, net_icmp_errors );
#else
    Com_Printf( "Total errors: %"PRIu64"/%"PRIu64" (send/recv)\n",
        net_send_errors, net_recv_errors );
#endif
    Com_Printf( "Current upload rate: %"PRIz" bytes/sec\n", net_rate_up );
    Com_Printf( "Current download rate: %"PRIz" bytes/sec\n", net_rate_dn );
}

/*
====================
NET_DumpHostInfo
====================
*/
static void NET_DumpHostInfo( struct hostent *h ) {
    byte **list;
    int i;

    Com_Printf( "Hostname: %s\n", h->h_name );

    list = ( byte ** )h->h_aliases;
    for( i = 0; list[i]; i++ ) {
        Com_Printf( "Alias   : %s\n", list[i] );
    }

    list = ( byte ** )h->h_addr_list;
    for( i = 0; list[i]; i++ ) {
        Com_Printf( "IP      : %u.%u.%u.%u\n",
            list[i][0], list[i][1], list[i][2], list[i][3] );
    }
}

static void dump_socket( SOCKET s, const char *s1, const char *s2 ) {
    struct sockaddr_in sadr;
    socklen_t len;
    netadr_t adr;

    len = sizeof( sadr );
    if( getsockname( s, ( struct sockaddr * )&sadr, &len ) == -1 ) {
        NET_GET_ERROR();
        Com_EPrintf( "%s: getsockname: %s\n", __func__, NET_ErrorString() );
        return;
    }
    NET_SockadrToNetadr( &sadr, &adr );
    Com_Printf( "%s %s socket bound to %s\n", s1, s2, NET_AdrToString( &adr ) );
}

/*
====================
NET_ShowIP_f
====================
*/
static void NET_ShowIP_f( void ) {
    char buffer[256];
    struct hostent *h;
    netsrc_t sock;

    if( gethostname( buffer, sizeof( buffer ) ) == -1 ) {
        NET_GET_ERROR();
        Com_EPrintf( "%s: gethostname: %s\n", __func__, NET_ErrorString() );
        return;
    }

    if( !( h = gethostbyname( buffer ) ) ) {
        NET_GET_ERROR();
        Com_EPrintf( "%s: gethostbyname: %s\n", __func__, NET_ErrorString() );
        return;
    }

    NET_DumpHostInfo( h );

    for( sock = 0; sock < NS_COUNT; sock++ ) {
        if( udp_sockets[sock] != INVALID_SOCKET ) {
            dump_socket( udp_sockets[sock], socketNames[sock], "UDP" );
        }
    }
    if( tcp_socket != INVALID_SOCKET ) {
        dump_socket( tcp_socket, socketNames[NS_SERVER], "TCP" );
    }
}

/*
====================
NET_Dns_f
====================
*/
static void NET_Dns_f( void ) {
    char buffer[MAX_QPATH];
    char *p;
    struct hostent *h;
    u_long address;

    if( Cmd_Argc() != 2 ) {
        Com_Printf( "Usage: %s <address>\n", Cmd_Argv( 0 ) );
        return;
    }

    Cmd_ArgvBuffer( 1, buffer, sizeof( buffer ) );

    if( ( p = strchr( buffer, ':' ) ) != NULL ) {
        *p = 0;
    }

    if( ( address = inet_addr( buffer ) ) != INADDR_NONE ) {
        h = gethostbyaddr( ( const char * )&address, sizeof( address ), AF_INET );
    } else {
        h = gethostbyname( buffer );
    }

    if( !h ) {
        Com_Printf( "Couldn't resolve %s\n", buffer );
        return;
    }

    NET_DumpHostInfo( h );
    
}

/*
====================
NET_Restart_f
====================
*/
static void NET_Restart_f( void ) {
    netflag_t flag = net_active;
    qboolean listen = tcp_socket != INVALID_SOCKET;

    Com_DPrintf( "%s\n", __func__ );

    if( listen ) {
        NET_Listen( qfalse );
    }
    NET_Config( NET_NONE );
    NET_Config( flag );
    if( listen ) {
        NET_Listen( qtrue );
    }

#if USE_SYSCON
    SV_SetConsoleTitle();
#endif
}

/*
====================
NET_Config
====================
*/
void NET_Config( netflag_t flag ) {
    netsrc_t sock;

    if( flag == net_active ) {
        return;
    }

    if( flag == NET_NONE ) {
        // shut down any existing sockets
        for( sock = 0; sock < NS_COUNT; sock++ ) {
            if( udp_sockets[sock] != INVALID_SOCKET ) {
                IO_Remove( udp_sockets[sock] );
                closesocket( udp_sockets[sock] );
                udp_sockets[sock] = INVALID_SOCKET;
            }
        }
        net_active = NET_NONE;
        return;
    }

#if USE_CLIENT
    if( ( flag & NET_CLIENT ) && udp_sockets[NS_CLIENT] == INVALID_SOCKET ) {
        NET_OpenClient();
    }
#endif

    if( ( flag & NET_SERVER ) && udp_sockets[NS_SERVER] == INVALID_SOCKET ) {
        NET_OpenServer();
    }

    net_active |= flag;
}

qboolean NET_GetAddress( netsrc_t sock, netadr_t *adr ) {
    struct sockaddr_in sadr;
    socklen_t len;
    SOCKET s = udp_sockets[sock];

    if( s == INVALID_SOCKET ) {
        return qfalse;
    }

    len = sizeof( sadr );
    if( getsockname( s, ( struct sockaddr * )&sadr, &len ) == -1 ) {
        return qfalse;
    }

    NET_SockadrToNetadr( &sadr, adr );

    return qtrue;
}

static size_t NET_UpRate_m( char *buffer, size_t size ) {
    return Q_scnprintf( buffer, size, "%"PRIz, net_rate_up );
}

static size_t NET_DnRate_m( char *buffer, size_t size ) {
    return Q_scnprintf( buffer, size, "%"PRIz, net_rate_dn );
}

static void net_udp_param_changed( cvar_t *self ) {
    // keep TCP socket vars in sync unless modified by user
    if( !( net_tcp_ip->flags & CVAR_MODIFIED ) ) {
        Cvar_SetByVar( net_tcp_ip, net_ip->string, FROM_CODE );
    }
    if( !( net_tcp_port->flags & CVAR_MODIFIED ) ) {
        Cvar_SetByVar( net_tcp_port, net_port->string, FROM_CODE );
    }

    NET_Restart_f();
}

static void net_tcp_param_changed( cvar_t *self ) {
    if( tcp_socket != INVALID_SOCKET ) {
        NET_Listen( qfalse );
        NET_Listen( qtrue );
    }
}

/*
====================
NET_Init
====================
*/
void NET_Init( void ) {
#ifdef _WIN32
    WSADATA ws;
    int ret;

    ret = WSAStartup( MAKEWORD( 1, 1 ), &ws );
    if( ret ) {
        Com_Error( ERR_FATAL, "Winsock initialization failed, returned %d", ret );
    }

    Com_DPrintf( "Winsock Initialized\n" );
#endif

    net_ip = Cvar_Get( "net_ip", "", 0 );
    net_ip->changed = net_udp_param_changed;
    net_port = Cvar_Get( "net_port", PORT_SERVER_STRING, 0 );
    net_port->changed = net_udp_param_changed;
#if USE_CLIENT
    net_clientport = Cvar_Get( "net_clientport", PORT_ANY_STRING, 0 );
    net_clientport->changed = net_udp_param_changed;
    net_dropsim = Cvar_Get( "net_dropsim", "0", 0 );
#endif
#if _DEBUG
    net_log_enable = Cvar_Get( "net_log_enable", "0", 0 );
    net_log_enable->changed = net_log_enable_changed;
    net_log_name = Cvar_Get( "net_log_name", "network", 0 );
    net_log_name->changed = net_log_param_changed;
    net_log_flush = Cvar_Get( "net_log_flush", "0", 0 );
    net_log_flush->changed = net_log_param_changed;
#endif
#if USE_ICMP
    net_ignore_icmp = Cvar_Get( "net_ignore_icmp", "0", 0 );
#endif
    net_tcp_ip = Cvar_Get( "net_tcp_ip", net_ip->string, 0 );
    net_tcp_ip->changed = net_tcp_param_changed;
    net_tcp_port = Cvar_Get( "net_tcp_port", net_port->string, 0 );
    net_tcp_port->changed = net_tcp_param_changed;
    net_tcp_backlog = Cvar_Get( "net_tcp_backlog", "4", 0 );

#if _DEBUG
    net_log_enable_changed( net_log_enable );
#endif

    net_rate_time = com_eventTime;

    Cmd_AddCommand( "net_restart", NET_Restart_f );
    Cmd_AddCommand( "net_stats", NET_Stats_f );
    Cmd_AddCommand( "showip", NET_ShowIP_f );
    Cmd_AddCommand( "dns", NET_Dns_f );

    Cmd_AddMacro( "net_uprate", NET_UpRate_m );
    Cmd_AddMacro( "net_dnrate", NET_DnRate_m );
}

/*
====================
NET_Shutdown
====================
*/
void NET_Shutdown( void ) {
#if _DEBUG
    if( net_logFile ) {
        FS_FCloseFile( net_logFile );
        net_logFile = 0;
    }
#endif

    NET_Listen( qfalse );
    NET_Config( NET_NONE );

#ifdef _WIN32
    WSACleanup();
#endif

    Cmd_RemoveCommand( "net_restart" );
    Cmd_RemoveCommand( "net_stats" );
    Cmd_RemoveCommand( "showip" );
    Cmd_RemoveCommand( "dns" );
}

