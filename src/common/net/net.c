/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

//
// net.c
//

#include "shared/shared.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/fifo.h"
#ifdef _DEBUG
#include "common/files.h"
#endif
#include "common/msg.h"
#include "common/net/net.h"
#include "common/protocol.h"
#include "common/zone.h"
#include "client/client.h"
#include "server/server.h"
#include "system/system.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else // _WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <errno.h>
#ifdef __linux__
#include <linux/types.h>
#if USE_ICMP
#include <linux/errqueue.h>
#else
#undef IP_RECVERR
#undef IPV6_RECVERR
#endif
#endif // __linux__
#endif // !_WIN32

// prevents infinite retry loops caused by broken TCP/IP stacks
#define MAX_ERROR_RETRIES   64

#if USE_CLIENT

#define MAX_LOOPBACK    4

typedef struct {
    byte    data[MAX_PACKETLEN];
    size_t  datalen;
} loopmsg_t;

typedef struct {
    loopmsg_t       msgs[MAX_LOOPBACK];
    unsigned long   get;
    unsigned long   send;
} loopback_t;

static loopback_t   loopbacks[NS_COUNT];

#endif // USE_CLIENT

cvar_t          *net_ip;
cvar_t          *net_ip6;
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

static cvar_t   *net_enable_ipv6;

#if USE_ICMP
static cvar_t   *net_ignore_icmp;
#endif

static netflag_t    net_active;
static int          net_error;

static qsocket_t    udp_sockets[NS_COUNT] = { -1, -1 };
static qsocket_t    tcp_socket = -1;

static qsocket_t    udp6_sockets[NS_COUNT] = { -1, -1 };
static qsocket_t    tcp6_socket = -1;

#ifdef _DEBUG
static qhandle_t    net_logFile;
#endif

static ioentry_t    io_entries[FD_SETSIZE];
static int          io_numfds;

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

static size_t NET_NetadrToSockadr(const netadr_t *a, struct sockaddr_storage *s)
{
    struct sockaddr_in  *s4 = (struct sockaddr_in  *)s;
    struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)s;

    memset(s, 0, sizeof(*s));

    switch (a->type) {
    case NA_BROADCAST:
        s4->sin_family = AF_INET;
        s4->sin_addr.s_addr = INADDR_BROADCAST;
        s4->sin_port = a->port;
        return sizeof(*s4);
    case NA_IP:
        s4->sin_family = AF_INET;
        memcpy(&s4->sin_addr, &a->ip, 4);
        s4->sin_port = a->port;
        return sizeof(*s4);
    case NA_IP6:
        s6->sin6_family = AF_INET6;
        memcpy(&s6->sin6_addr, &a->ip, 16);
        s6->sin6_port = a->port;
        s6->sin6_scope_id = a->scope_id;
        return sizeof(*s6);
    default:
        break;
    }

    return 0;
}

static void NET_SockadrToNetadr(const struct sockaddr_storage *s, netadr_t *a)
{
    const struct sockaddr_in  *s4 = (const struct sockaddr_in  *)s;
    const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)s;

    memset(a, 0, sizeof(*a));

    switch (s->ss_family) {
    case AF_INET:
        a->type = NA_IP;
        memcpy(&a->ip, &s4->sin_addr, 4);
        a->port = s4->sin_port;
        break;
    case AF_INET6:
        if (IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr)) {
            a->type = NA_IP;
            memcpy(&a->ip, &s6->sin6_addr.s6_addr[12], 4);
        } else {
            a->type = NA_IP6;
            memcpy(&a->ip, &s6->sin6_addr, 16);
            a->scope_id = s6->sin6_scope_id;
        }
        a->port = s6->sin6_port;
        break;
    default:
        break;
    }
}

#ifdef _WIN32
#define NS_INT16SZ      2
#define NS_INADDRSZ     4
#define NS_IN6ADDRSZ    16
#include "inet_ntop.h"
#include "inet_pton.h"
#else
#define os_inet_ntop    inet_ntop
#define os_inet_pton    inet_pton
#endif

char *NET_BaseAdrToString(const netadr_t *a)
{
    static char s[MAX_QPATH];

    switch (a->type) {
    case NA_UNSPECIFIED:
        return strcpy(s, "<unspecified>");
    case NA_LOOPBACK:
        return strcpy(s, "loopback");
    case NA_IP:
    case NA_BROADCAST:
        if (os_inet_ntop(AF_INET, &a->ip, s, sizeof(s)))
            return s;
        else
            return strcpy(s, "<invalid>");
    case NA_IP6:
        if (a->scope_id) {
            struct sockaddr_storage addr;
            size_t addrlen;

            addrlen = NET_NetadrToSockadr(a, &addr);
            if (getnameinfo((struct sockaddr *)&addr, addrlen,
                            s, sizeof(s), NULL, 0, NI_NUMERICHOST) == 0)
                return s;
        }
        if (os_inet_ntop(AF_INET6, &a->ip, s, sizeof(s)))
            return s;
        else
            return strcpy(s, "<invalid>");
    default:
        Com_Error(ERR_FATAL, "%s: bad address type", __func__);
    }

    return NULL;
}

/*
===================
NET_AdrToString
===================
*/
char *NET_AdrToString(const netadr_t *a)
{
    static char s[MAX_QPATH];

    switch (a->type) {
    case NA_UNSPECIFIED:
        return strcpy(s, "<unspecified>");
    case NA_LOOPBACK:
        return strcpy(s, "loopback");
    default:
        Q_snprintf(s, sizeof(s), (a->type == NA_IP6) ? "[%s]:%u" : "%s:%u",
                   NET_BaseAdrToString(a), BigShort(a->port));
    }
    return s;
}

static struct addrinfo *NET_SearchAdrrInfo(struct addrinfo *rp, int family)
{
    while (rp) {
        if (rp->ai_family == family)
            return rp;
        rp = rp->ai_next;
    }

    return NULL;
}

qboolean NET_StringPairToAdr(const char *host, const char *port, netadr_t *a)
{
    byte buf[128];
    struct addrinfo hints, *res, *rp;
    int err;

    memset(&hints, 0, sizeof(hints));

    if (net_enable_ipv6->integer < 1)
        hints.ai_family = AF_INET;

    if (os_inet_pton(AF_INET, host, buf) == 1 ||
        os_inet_pton(AF_INET6, host, buf) == 1)
        hints.ai_flags |= AI_NUMERICHOST;

#ifdef AI_NUMERICSERV
    if (port && COM_IsUint(port))
        hints.ai_flags |= AI_NUMERICSERV;
#endif

    err = getaddrinfo(host, port, &hints, &res);
    if (err)
        return qfalse;

    rp = res;
    if (net_enable_ipv6->integer < 2) {
        rp = NET_SearchAdrrInfo(res, AF_INET);
        if (!rp)
            rp = res;
    }

    NET_SockadrToNetadr((struct sockaddr_storage *)rp->ai_addr, a);

    freeaddrinfo(res);
    return qtrue;
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
qboolean NET_StringToAdr(const char *s, netadr_t *a, int default_port)
{
    char copy[MAX_STRING_CHARS], *h, *p;
    size_t len;

    len = Q_strlcpy(copy, s, sizeof(copy));
    if (len >= sizeof(copy))
        return qfalse;

    // parse IPv6 address in square brackets
    h = p = copy;
    if (*h == '[') {
        h++;
        p = strchr(h, ']');
        if (!p)
            return qfalse;
        *p++ = 0;
    }

    // strip off a trailing :port if present
    p = strchr(p, ':');
    if (p)
        *p++ = 0;

    if (!NET_StringPairToAdr(h, p, a))
        return qfalse;

    if (!a->port)
        a->port = BigShort(default_port);

    return qtrue;
}

//=============================================================================

#ifdef _DEBUG

static void logfile_close(void)
{
    if (!net_logFile) {
        return;
    }

    Com_Printf("Closing network log.\n");

    FS_FCloseFile(net_logFile);
    net_logFile = 0;
}

static void logfile_open(void)
{
    char buffer[MAX_OSPATH];
    unsigned mode;
    qhandle_t f;

    mode = net_log_enable->integer > 1 ? FS_MODE_APPEND : FS_MODE_WRITE;
    if (net_log_flush->integer > 0) {
        if (net_log_flush->integer > 1) {
            mode |= FS_BUF_NONE;
        } else {
            mode |= FS_BUF_LINE;
        }
    }

    f = FS_EasyOpenFile(buffer, sizeof(buffer), mode | FS_FLAG_TEXT,
                        "logs/", net_log_name->string, ".log");
    if (!f) {
        Cvar_Set("net_log_enable", "0");
        return;
    }

    net_logFile = f;
    Com_Printf("Logging network packets to %s\n", buffer);
}

static void net_log_enable_changed(cvar_t *self)
{
    logfile_close();
    if (self->integer) {
        logfile_open();
    }
}

static void net_log_param_changed(cvar_t *self)
{
    if (net_log_enable->integer) {
        logfile_close();
        logfile_open();
    }
}

/*
=============
NET_LogPacket
=============
*/
static void NET_LogPacket(const netadr_t *address, const char *prefix,
                          const byte *data, size_t length)
{
    int numRows;
    int i, j, c;

    if (!net_logFile) {
        return;
    }

    FS_FPrintf(net_logFile, "%u : %s : %s : %"PRIz" bytes\n",
               com_localTime, prefix, NET_AdrToString(address), length);

    numRows = (length + 15) / 16;
    for (i = 0; i < numRows; i++) {
        FS_FPrintf(net_logFile, "%04x : ", i * 16);
        for (j = 0; j < 16; j++) {
            if (i * 16 + j < length) {
                FS_FPrintf(net_logFile, "%02x ", data[i * 16 + j]);
            } else {
                FS_FPrintf(net_logFile, "   ");
            }
        }
        FS_FPrintf(net_logFile, ": ");
        for (j = 0; j < 16; j++) {
            if (i * 16 + j < length) {
                c = data[i * 16 + j];
                FS_FPrintf(net_logFile, "%c", Q_isprint(c) ? c : '.');
            } else {
                FS_FPrintf(net_logFile, " ");
            }
        }
        FS_FPrintf(net_logFile, "\n");
    }

    FS_FPrintf(net_logFile, "\n");
}

#endif

//=============================================================================

#define RATE_SECS    3

void NET_UpdateStats(void)
{
    unsigned diff;

    if (net_rate_time > com_eventTime) {
        net_rate_time = com_eventTime;
    }
    diff = com_eventTime - net_rate_time;
    if (diff < RATE_SECS * 1000) {
        return;
    }
    net_rate_time = com_eventTime;

    net_rate_dn = net_rate_rcvd / RATE_SECS;
    net_rate_up = net_rate_sent / RATE_SECS;
    net_rate_sent = 0;
    net_rate_rcvd = 0;
}

/*
====================
NET_Stats_f
====================
*/
static void NET_Stats_f(void)
{
    time_t diff, now = time(NULL);
    char buffer[MAX_QPATH];

    if (com_startTime > now) {
        com_startTime = now;
    }
    diff = now - com_startTime;
    if (diff < 1) {
        diff = 1;
    }

    Com_FormatTime(buffer, sizeof(buffer), diff);
    Com_Printf("Network uptime: %s\n", buffer);
    Com_Printf("Bytes sent: %"PRIu64" (%"PRIu64" bytes/sec)\n",
               net_bytes_sent, net_bytes_sent / diff);
    Com_Printf("Bytes rcvd: %"PRIu64" (%"PRIu64" bytes/sec)\n",
               net_bytes_rcvd, net_bytes_rcvd / diff);
    Com_Printf("Packets sent: %"PRIu64" (%"PRIu64" packets/sec)\n",
               net_packets_sent, net_packets_sent / diff);
    Com_Printf("Packets rcvd: %"PRIu64" (%"PRIu64" packets/sec)\n",
               net_packets_rcvd, net_packets_rcvd / diff);
#if USE_ICMP
    Com_Printf("Total errors: %"PRIu64"/%"PRIu64"/%"PRIu64" (send/recv/icmp)\n",
               net_send_errors, net_recv_errors, net_icmp_errors);
#else
    Com_Printf("Total errors: %"PRIu64"/%"PRIu64" (send/recv)\n",
               net_send_errors, net_recv_errors);
#endif
    Com_Printf("Current upload rate: %"PRIz" bytes/sec\n", net_rate_up);
    Com_Printf("Current download rate: %"PRIz" bytes/sec\n", net_rate_dn);
}

static size_t NET_UpRate_m(char *buffer, size_t size)
{
    return Q_scnprintf(buffer, size, "%"PRIz, net_rate_up);
}

static size_t NET_DnRate_m(char *buffer, size_t size)
{
    return Q_scnprintf(buffer, size, "%"PRIz, net_rate_dn);
}

//=============================================================================

#if USE_CLIENT

static void NET_GetLoopPackets(netsrc_t sock, void (*packet_cb)(void))
{
    loopback_t *loop;
    loopmsg_t *loopmsg;

    loop = &loopbacks[sock];

    if (loop->send - loop->get > MAX_LOOPBACK - 1) {
        loop->get = loop->send - MAX_LOOPBACK + 1;
    }

    while (loop->get < loop->send) {
        loopmsg = &loop->msgs[loop->get & (MAX_LOOPBACK - 1)];
        loop->get++;

        memcpy(msg_read_buffer, loopmsg->data, loopmsg->datalen);

#ifdef _DEBUG
        if (net_log_enable->integer > 1) {
            NET_LogPacket(&net_from, "LP recv", loopmsg->data, loopmsg->datalen);
        }
#endif
        if (sock == NS_CLIENT) {
            net_rate_rcvd += loopmsg->datalen;
        }

        SZ_Init(&msg_read, msg_read_buffer, sizeof(msg_read_buffer));
        msg_read.cursize = loopmsg->datalen;

        (*packet_cb)();
    }
}

static qboolean NET_SendLoopPacket(netsrc_t sock, const void *data,
                                   size_t len, const netadr_t *to)
{
    loopback_t *loop;
    loopmsg_t *msg;

    if (net_dropsim->integer > 0 && (rand() % 100) < net_dropsim->integer) {
        return qfalse;
    }

    loop = &loopbacks[sock ^ 1];

    msg = &loop->msgs[loop->send & (MAX_LOOPBACK - 1)];
    loop->send++;

    memcpy(msg->data, data, len);
    msg->datalen = len;

#ifdef _DEBUG
    if (net_log_enable->integer > 1) {
        NET_LogPacket(to, "LP send", data, len);
    }
#endif
    if (sock == NS_CLIENT) {
        net_rate_sent += len;
    }

    return qtrue;
}

#endif // USE_CLIENT

//=============================================================================

#if USE_ICMP

static const char *os_error_string(int err);

static void NET_ErrorEvent(qsocket_t sock, netadr_t *from,
                           int ee_errno, int ee_info)
{
    if (net_ignore_icmp->integer > 0) {
        return;
    }

    if (from->type == NA_UNSPECIFIED) {
        return;
    }

    Com_DPrintf("%s: %s from %s\n", __func__,
                os_error_string(ee_errno), NET_AdrToString(from));
    net_icmp_errors++;

    if (sock == udp_sockets[NS_SERVER] ||
        sock == udp6_sockets[NS_SERVER]) {
        SV_ErrorEvent(from, ee_errno, ee_info);
        return;
    }

    if (sock == udp_sockets[NS_CLIENT] ||
        sock == udp6_sockets[NS_CLIENT]) {
        CL_ErrorEvent(from);
        return;
    }
}

#endif // USE_ICMP

//=============================================================================

// include our wrappers to hide platfrom-specific details
#ifdef _WIN32
#include "win.h"
#else
#include "unix.h"
#endif

/*
=============
NET_ErrorString
=============
*/
const char *NET_ErrorString(void)
{
    return os_error_string(net_error);
}

/*
=============
NET_AddFd

Adds file descriptor to the list of monitored descriptors
=============
*/
ioentry_t *NET_AddFd(qsocket_t fd)
{
    ioentry_t *e = os_add_io(fd);

    e->inuse = qtrue;
    return e;
}

/*
=============
NET_RemoveFd

Removes file descriptor from the list of monitored descriptors
=============
*/
void NET_RemoveFd(qsocket_t fd)
{
    ioentry_t *e = os_get_io(fd);
    int i;

    memset(e, 0, sizeof(*e));

    for (i = io_numfds - 1; i >= 0; i--) {
        e = &io_entries[i];
        if (e->inuse) {
            break;
        }
    }

    io_numfds = i + 1;
}

/*
=============
NET_Sleep

Sleeps msec or until some file descriptor is ready. Implementation is not
terribly efficient, but that's fine for a small number of descriptors we
typically have.
=============
*/
int NET_Sleep(int msec)
{
    struct timeval tv;
    fd_set rfds, wfds, efds;
    ioentry_t *e;
    qsocket_t fd;
    int i, ret;

    if (!io_numfds) {
        // don't bother with select()
        Sys_Sleep(msec);
        return 0;
    }

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);

    for (i = 0, e = io_entries; i < io_numfds; i++, e++) {
        if (!e->inuse) {
            continue;
        }
        fd = os_get_fd(e);
        e->canread = qfalse;
        e->canwrite = qfalse;
        e->canexcept = qfalse;
        if (e->wantread) FD_SET(fd, &rfds);
        if (e->wantwrite) FD_SET(fd, &wfds);
        if (e->wantexcept) FD_SET(fd, &efds);
    }

    tv.tv_sec = msec / 1000;
    tv.tv_usec = (msec % 1000) * 1000;

    ret = os_select(io_numfds, &rfds, &wfds, &efds, &tv);
    if (ret == -1) {
        Com_EPrintf("%s: %s\n", __func__, NET_ErrorString());
        return ret;
    }

    if (ret == 0)
        return ret;

    for (i = 0; i < io_numfds; i++) {
        e = &io_entries[i];
        if (!e->inuse) {
            continue;
        }
        fd = os_get_fd(e);
        if (FD_ISSET(fd, &rfds)) e->canread = qtrue;
        if (FD_ISSET(fd, &wfds)) e->canwrite = qtrue;
        if (FD_ISSET(fd, &efds)) e->canexcept = qtrue;
    }

    return ret;
}

#if USE_AC_SERVER

/*
=============
NET_Sleepv

Sleeps msec or until some file descriptor from a given subset is ready
=============
*/
int NET_Sleepv(int msec, ...)
{
    va_list argptr;
    struct timeval tv;
    fd_set rfds, wfds, efds;
    ioentry_t *e;
    qsocket_t fd;
    int ret;

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);

    va_start(argptr, msec);
    while (1) {
        fd = va_arg(argptr, qsocket_t);
        if (fd == -1) {
            break;
        }
        e = os_get_io(fd);
        if (!e->inuse) {
            continue;
        }
        e->canread = qfalse;
        e->canwrite = qfalse;
        e->canexcept = qfalse;
        if (e->wantread) FD_SET(fd, &rfds);
        if (e->wantwrite) FD_SET(fd, &wfds);
        if (e->wantexcept) FD_SET(fd, &efds);
    }
    va_end(argptr);

    tv.tv_sec = msec / 1000;
    tv.tv_usec = (msec % 1000) * 1000;

    ret = os_select(io_numfds, &rfds, &wfds, &efds, &tv);
    if (ret == -1) {
        Com_EPrintf("%s: %s\n", __func__, NET_ErrorString());
        return ret;
    }

    if (ret == 0)
        return ret;

    va_start(argptr, msec);
    while (1) {
        fd = va_arg(argptr, qsocket_t);
        if (fd == -1) {
            break;
        }
        e = os_get_io(fd);
        if (!e->inuse) {
            continue;
        }
        if (FD_ISSET(fd, &rfds)) e->canread = qtrue;
        if (FD_ISSET(fd, &wfds)) e->canwrite = qtrue;
        if (FD_ISSET(fd, &efds)) e->canexcept = qtrue;
    }
    va_end(argptr);

    return ret;
}

#endif // USE_AC_SERVER

//=============================================================================

static void NET_GetUdpPackets(qsocket_t sock, void (*packet_cb)(void))
{
    ioentry_t *e;
    ssize_t ret;

    if (sock == -1)
        return;

    e = os_get_io(sock);
    if (!e->canread)
        return;

    while (1) {
        ret = os_udp_recv(sock, msg_read_buffer, MAX_PACKETLEN, &net_from);
        if (ret == NET_AGAIN) {
            e->canread = qfalse;
            break;
        }

        if (ret == NET_ERROR) {
            Com_DPrintf("%s: %s from %s\n", __func__,
                        NET_ErrorString(), NET_AdrToString(&net_from));
            net_recv_errors++;
            break;
        }

#ifdef _DEBUG
        if (net_log_enable->integer)
            NET_LogPacket(&net_from, "UDP recv", msg_read_buffer, ret);
#endif

        net_rate_rcvd += ret;
        net_bytes_rcvd += ret;
        net_packets_rcvd++;

        SZ_Init(&msg_read, msg_read_buffer, sizeof(msg_read_buffer));
        msg_read.cursize = ret;

        (*packet_cb)();
    }
}

/*
=============
NET_GetPackets

Fills msg_read_buffer with packet contents,
net_from variable receives source address.
=============
*/
void NET_GetPackets(netsrc_t sock, void (*packet_cb)(void))
{
#if USE_CLIENT
    memset(&net_from, 0, sizeof(net_from));
    net_from.type = NA_LOOPBACK;

    // process loopback packets
    NET_GetLoopPackets(sock, packet_cb);
#endif

    // process UDP packets
    NET_GetUdpPackets(udp_sockets[sock], packet_cb);

    // process UDP6 packets
    NET_GetUdpPackets(udp6_sockets[sock], packet_cb);
}

/*
=============
NET_SendPacket

=============
*/
qboolean NET_SendPacket(netsrc_t sock, const void *data,
                        size_t len, const netadr_t *to)
{
    ssize_t ret;
    qsocket_t s;

    if (len == 0)
        return qfalse;

    if (len > MAX_PACKETLEN) {
        Com_EPrintf("%s: oversize packet to %s\n", __func__,
                    NET_AdrToString(to));
        return qfalse;
    }

    switch (to->type) {
    case NA_UNSPECIFIED:
        return qfalse;
#if USE_CLIENT
    case NA_LOOPBACK:
        return NET_SendLoopPacket(sock, data, len, to);
#endif
    case NA_IP:
    case NA_BROADCAST:
        s = udp_sockets[sock];
        break;
    case NA_IP6:
        s = udp6_sockets[sock];
        break;
    default:
        Com_Error(ERR_FATAL, "%s: bad address type", __func__);
    }

    if (s == -1)
        return qfalse;

    ret = os_udp_send(s, data, len, to);
    if (ret == NET_AGAIN)
        return qfalse;

    if (ret == NET_ERROR) {
        Com_DPrintf("%s: %s to %s\n", __func__,
                    NET_ErrorString(), NET_AdrToString(to));
        net_send_errors++;
        return qfalse;
    }

    if (ret < len)
        Com_WPrintf("%s: short send to %s\n", __func__,
                    NET_AdrToString(to));

#ifdef _DEBUG
    if (net_log_enable->integer)
        NET_LogPacket(to, "UDP send", data, ret);
#endif

    net_rate_sent += ret;
    net_bytes_sent += ret;
    net_packets_sent++;

    return qtrue;
}

//=============================================================================

static qsocket_t UDP_OpenSocket(const char *iface, int port, int family)
{
    qsocket_t s, newsocket;
    struct addrinfo hints, *res, *rp;
    char buf[MAX_QPATH];
    const char *node, *service;
    int err;

    Com_DPrintf("Opening UDP%s socket: %s:%d\n",
                (family == AF_INET6) ? "6" : "", iface, port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = family;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    // empty string binds to all interfaces
    if (!*iface) {
        node = NULL;
    } else {
        node = iface;
    }

    if (port == PORT_ANY) {
        service = "0";
    } else {
        Q_snprintf(buf, sizeof(buf), "%d", port);
        service = buf;
    }

#ifdef AI_NUMERICSERV
    hints.ai_flags |= AI_NUMERICSERV;
#endif

    // resolve iface addr
    err = getaddrinfo(node, service, &hints, &res);
    if (err) {
        Com_EPrintf("%s: %s:%d: bad interface address: %s\n",
                    __func__, iface, port, gai_strerror(err));
        return -1;
    }

    newsocket = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        s = os_socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s == -1) {
            Com_EPrintf("%s: %s:%d: can't create socket: %s\n",
                        __func__, iface, port, NET_ErrorString());
            continue;
        }

        // make it non-blocking
        if (os_make_nonblock(s, 1)) {
            Com_EPrintf("%s: %s:%d: can't make socket non-blocking: %s\n",
                        __func__, iface, port, NET_ErrorString());
            os_closesocket(s);
            continue;
        }

        if (rp->ai_family == AF_INET) {
            // make it broadcast capable
            if (os_setsockopt(s, SOL_SOCKET, SO_BROADCAST, 1)) {
                Com_WPrintf("%s: %s:%d: can't make socket broadcast capable: %s\n",
                            __func__, iface, port, NET_ErrorString());
            }

#ifdef IP_RECVERR
            // enable ICMP error queue
            if (os_setsockopt(s, IPPROTO_IP, IP_RECVERR, 1)) {
                Com_WPrintf("%s: %s:%d: can't enable ICMP error queue: %s\n",
                            __func__, iface, port, NET_ErrorString());
            }
#endif

#ifdef IP_MTU_DISCOVER
            // allow IP fragmentation by disabling path MTU discovery
            if (os_setsockopt(s, IPPROTO_IP, IP_MTU_DISCOVER, IP_PMTUDISC_DONT)) {
                Com_WPrintf("%s: %s:%d: can't disable path MTU discovery: %s\n",
                            __func__, iface, port, NET_ErrorString());
            }
#endif
        }

        if (rp->ai_family == AF_INET6) {
#ifdef IPV6_RECVERR
            // enable ICMP6 error queue
            if (os_setsockopt(s, IPPROTO_IPV6, IPV6_RECVERR, 1)) {
                Com_WPrintf("%s: %s:%d: can't enable ICMP6 error queue: %s\n",
                            __func__, iface, port, NET_ErrorString());
            }
#endif

#ifdef IPV6_V6ONLY
            // disable IPv4-mapped addresses
            if (os_setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, 1)) {
                Com_WPrintf("%s: %s:%d: can't make socket IPv6-only: %s\n",
                            __func__, iface, port, NET_ErrorString());
            }
#endif
        }

        if (os_bind(s, rp->ai_addr, rp->ai_addrlen)) {
            Com_EPrintf("%s: %s:%d: can't bind socket: %s\n",
                        __func__, iface, port, NET_ErrorString());
            os_closesocket(s);
            continue;
        }

        newsocket = s;
        break;
    }

    freeaddrinfo(res);

    return newsocket;
}

static qsocket_t TCP_OpenSocket(const char *iface, int port, int family, netsrc_t who)
{
    qsocket_t s, newsocket;
    struct addrinfo hints, *res, *rp;
    char buf[MAX_QPATH];
    const char *node, *service;
    int err;

    Com_DPrintf("Opening TCP%s socket: %s:%d\n",
                (family == AF_INET6) ? "6" : "", iface, port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // empty string binds to all interfaces
    if (!*iface) {
        node = NULL;
    } else {
        node = iface;
    }

    if (port == PORT_ANY) {
        service = "0";
    } else {
        Q_snprintf(buf, sizeof(buf), "%d", port);
        service = buf;
    }

#ifdef AI_NUMERICSERV
    hints.ai_flags |= AI_NUMERICSERV;
#endif

    // resolve iface addr
    err = getaddrinfo(node, service, &hints, &res);
    if (err) {
        Com_EPrintf("%s: %s:%d: bad interface address: %s\n",
                    __func__, iface, port, gai_strerror(err));
        return -1;
    }

    newsocket = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        s = os_socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s == -1) {
            Com_EPrintf("%s: %s:%d: can't create socket: %s\n",
                        __func__, iface, port, NET_ErrorString());
            continue;
        }

        // make it non-blocking
        if (os_make_nonblock(s, 1)) {
            Com_EPrintf("%s: %s:%d: can't make socket non-blocking: %s\n",
                        __func__, iface, port, NET_ErrorString());
            os_closesocket(s);
            continue;
        }

        if (who == NS_SERVER) {
            // give it a chance to reuse previous port
            if (os_setsockopt(s, SOL_SOCKET, SO_REUSEADDR, 1)) {
                Com_WPrintf("%s: %s:%d: can't force socket to reuse address: %s\n",
                            __func__, iface, port, NET_ErrorString());
            }
        }

        if (rp->ai_family == AF_INET6) {
#ifdef IPV6_V6ONLY
            // disable IPv4-mapped addresses
            if (os_setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, 1)) {
                Com_WPrintf("%s: %s:%d: can't make socket IPv6-only: %s\n",
                            __func__, iface, port, NET_ErrorString());
            }
#endif
        }

        if (os_bind(s, rp->ai_addr, rp->ai_addrlen)) {
            Com_EPrintf("%s: %s:%d: can't bind socket: %s\n",
                        __func__, iface, port, NET_ErrorString());
            os_closesocket(s);
            continue;
        }

        newsocket = s;
        break;
    }

    freeaddrinfo(res);

    return newsocket;
}

static void NET_OpenServer(void)
{
    static int saved_port;
    ioentry_t *e;
    qsocket_t s;

    if (udp_sockets[NS_SERVER] != -1)
        return;

    s = UDP_OpenSocket(net_ip->string, net_port->integer, AF_INET);
    if (s != -1) {
        saved_port = net_port->integer;
        udp_sockets[NS_SERVER] = s;
        e = NET_AddFd(s);
        e->wantread = qtrue;
        return;
    }

    if (saved_port && saved_port != net_port->integer) {
        // revert to the last valid port
        Com_Printf("Reverting to the last valid port %d...\n", saved_port);
        Cbuf_AddText(&cmd_buffer, va("set net_port %d\n", saved_port));
        return;
    }

#if USE_CLIENT
    if (!dedicated->integer) {
        Com_WPrintf("Couldn't open server UDP port.\n");
        return;
    }
#endif

    Com_Error(ERR_FATAL, "Couldn't open dedicated server UDP port");
}

static void NET_OpenServer6(void)
{
    ioentry_t *e;
    qsocket_t s;

    if (net_enable_ipv6->integer < 2)
        return;

    if (udp6_sockets[NS_SERVER] != -1)
        return;

    s = UDP_OpenSocket(net_ip6->string, net_port->integer, AF_INET6);
    if (s == -1)
        return;

    udp6_sockets[NS_SERVER] = s;
    e = NET_AddFd(s);
    e->wantread = qtrue;
}

#if USE_CLIENT
static void NET_OpenClient(void)
{
    ioentry_t *e;
    qsocket_t s;
    netadr_t adr;

    if (udp_sockets[NS_CLIENT] != -1)
        return;

    s = UDP_OpenSocket(net_ip->string, net_clientport->integer, AF_INET);
    if (s == -1) {
        // now try with random port
        if (net_clientport->integer != PORT_ANY)
            s = UDP_OpenSocket(net_ip->string, PORT_ANY, AF_INET);

        if (s == -1) {
            Com_WPrintf("Couldn't open client UDP port.\n");
            return;
        }

        if (os_getsockname(s, &adr)) {
            Com_EPrintf("Couldn't get client UDP socket name: %s\n", NET_ErrorString());
            os_closesocket(s);
            return;
        }

        Com_WPrintf("Client UDP socket bound to %s.\n", NET_AdrToString(&adr));
        Cvar_SetByVar(net_clientport, va("%d", PORT_ANY), FROM_CODE);
    }

    udp_sockets[NS_CLIENT] = s;
    e = NET_AddFd(s);
    e->wantread = qtrue;
}

static void NET_OpenClient6(void)
{
    ioentry_t *e;
    qsocket_t s;

    if (net_enable_ipv6->integer < 1)
        return;

    if (udp6_sockets[NS_CLIENT] != -1)
        return;

    s = UDP_OpenSocket(net_ip6->string, net_clientport->integer, AF_INET6);
    if (s == -1)
        return;

    udp6_sockets[NS_CLIENT] = s;
    e = NET_AddFd(s);
    e->wantread = qtrue;
}
#endif

/*
====================
NET_Config
====================
*/
void NET_Config(netflag_t flag)
{
    netsrc_t sock;

    if (flag == net_active) {
        return;
    }

    if (flag == NET_NONE) {
        // shut down any existing sockets
        for (sock = 0; sock < NS_COUNT; sock++) {
            if (udp_sockets[sock] != -1) {
                NET_RemoveFd(udp_sockets[sock]);
                os_closesocket(udp_sockets[sock]);
                udp_sockets[sock] = -1;
            }
            if (udp6_sockets[sock] != -1) {
                NET_RemoveFd(udp6_sockets[sock]);
                os_closesocket(udp6_sockets[sock]);
                udp6_sockets[sock] = -1;
            }
        }
        net_active = NET_NONE;
        return;
    }

#if USE_CLIENT
    if (flag & NET_CLIENT) {
        NET_OpenClient();
        NET_OpenClient6();
    }
#endif

    if (flag & NET_SERVER) {
        NET_OpenServer();
        NET_OpenServer6();
    }

    net_active |= flag;
}

/*
====================
NET_GetAddress
====================
*/
qboolean NET_GetAddress(netsrc_t sock, netadr_t *adr)
{
    if (udp_sockets[sock] == -1)
        return qfalse;

    if (os_getsockname(udp_sockets[sock], adr))
        return qfalse;

    return qtrue;
}

//=============================================================================

void NET_CloseStream(netstream_t *s)
{
    if (!s->state) {
        return;
    }

    NET_RemoveFd(s->socket);
    os_closesocket(s->socket);
    s->socket = -1;
    s->state = NS_DISCONNECTED;
}

static neterr_t NET_Listen4(qboolean arg)
{
    qsocket_t s;
    ioentry_t *e;
    neterr_t ret;

    if (!arg) {
        if (tcp_socket != -1) {
            NET_RemoveFd(tcp_socket);
            os_closesocket(tcp_socket);
            tcp_socket = -1;
        }
        return NET_OK;
    }

    if (tcp_socket != -1) {
        return NET_AGAIN;
    }

    s = TCP_OpenSocket(net_ip->string, net_port->integer, AF_INET, NS_SERVER);
    if (s == -1) {
        return NET_ERROR;
    }

    ret = os_listen(s, 16);
    if (ret) {
        os_closesocket(s);
        return ret;
    }

    tcp_socket = s;

    // initialize io entry
    e = NET_AddFd(s);
    e->wantread = qtrue;

    return NET_OK;
}

static neterr_t NET_Listen6(qboolean arg)
{
    qsocket_t s;
    ioentry_t *e;
    neterr_t ret;

    if (!arg) {
        if (tcp6_socket != -1) {
            NET_RemoveFd(tcp6_socket);
            os_closesocket(tcp6_socket);
            tcp6_socket = -1;
        }
        return NET_OK;
    }


    if (tcp6_socket != -1) {
        return NET_AGAIN;
    }

    if (net_enable_ipv6->integer < 2) {
        return NET_AGAIN;
    }

    s = TCP_OpenSocket(net_ip6->string, net_port->integer, AF_INET6, NS_SERVER);
    if (s == -1) {
        return NET_ERROR;
    }

    ret = os_listen(s, 16);
    if (ret) {
        os_closesocket(s);
        return ret;
    }

    tcp6_socket = s;

    // initialize io entry
    e = NET_AddFd(s);
    e->wantread = qtrue;

    return NET_OK;
}

neterr_t NET_Listen(qboolean arg)
{
    neterr_t ret4, ret6;

    ret4 = NET_Listen4(arg);
    ret6 = NET_Listen6(arg);

    if (ret4 == NET_OK || ret6 == NET_OK)
        return NET_OK;

    if (ret4 == NET_ERROR || ret6 == NET_ERROR)
        return NET_ERROR;

    return NET_AGAIN;
}

static neterr_t NET_AcceptSocket(netstream_t *s, qsocket_t sock)
{
    ioentry_t *e;
    qsocket_t newsocket;
    neterr_t ret;

    if (sock == -1) {
        return NET_AGAIN;
    }

    e = os_get_io(sock);
    if (!e->canread) {
        return NET_AGAIN;
    }

    ret = os_accept(sock, &newsocket, &net_from);
    if (ret) {
        e->canread = qfalse;
        return ret;
    }

    // make it non-blocking
    ret = os_make_nonblock(newsocket, 1);
    if (ret) {
        os_closesocket(newsocket);
        return ret;
    }

    // initialize stream
    memset(s, 0, sizeof(*s));
    s->socket = newsocket;
    s->address = net_from;
    s->state = NS_CONNECTED;

    // initialize io entry
    e = NET_AddFd(newsocket);
    //e->wantwrite = qtrue;
    e->wantread = qtrue;

    return NET_OK;
}

// net_from variable receives source address
neterr_t NET_Accept(netstream_t *s)
{
    neterr_t ret;

    ret = NET_AcceptSocket(s, tcp_socket);
    if (ret == NET_AGAIN)
        ret = NET_AcceptSocket(s, tcp6_socket);

    return ret;
}

neterr_t NET_Connect(const netadr_t *peer, netstream_t *s)
{
    qsocket_t socket;
    ioentry_t *e;
    neterr_t ret;

    // always bind to `net_ip' for outgoing TCP connections
    // to avoid problems with AC or MVD/GTV auth on a multi IP system
    switch (peer->type) {
    case NA_IP:
        socket = TCP_OpenSocket(net_ip->string, PORT_ANY, AF_INET, NS_CLIENT);
        break;
    case NA_IP6:
        socket = TCP_OpenSocket(net_ip6->string, PORT_ANY, AF_INET6, NS_CLIENT);
        break;
    default:
        return NET_ERROR;
    }

    if (socket == -1) {
        return NET_ERROR;
    }

    ret = os_connect(socket, peer);
    if (ret) {
        os_closesocket(socket);
        return NET_ERROR;
    }

    // initialize stream
    memset(s, 0, sizeof(*s));
    s->state = NS_CONNECTING;
    s->address = *peer;
    s->socket = socket;

    // initialize io entry
    e = NET_AddFd(socket);
    e->wantwrite = qtrue;
#ifdef _WIN32
    e->wantexcept = qtrue;
#endif

    return NET_OK;
}

neterr_t NET_RunConnect(netstream_t *s)
{
    ioentry_t *e;
    neterr_t ret;
    int err;

    if (s->state != NS_CONNECTING) {
        return NET_AGAIN;
    }

    e = os_get_io(s->socket);
    if (!e->canwrite
#ifdef _WIN32
        && !e->canexcept
#endif
       ) {
        return NET_AGAIN;
    }

    ret = os_getsockopt(s->socket, SOL_SOCKET, SO_ERROR, &err);
    if (ret) {
        goto fail;
    }
    if (err) {
        net_error = err;
        goto fail;
    }

    s->state = NS_CONNECTED;
    e->wantwrite = qfalse;
    e->wantread = qtrue;
#ifdef _WIN32
    e->wantexcept = qfalse;
#endif
    return NET_OK;

fail:
    s->state = NS_BROKEN;
    e->wantwrite = qfalse;
    e->wantread = qfalse;
#ifdef _WIN32
    e->wantexcept = qfalse;
#endif
    return NET_ERROR;
}

// updates wantread/wantwrite
void NET_UpdateStream(netstream_t *s)
{
    size_t len;
    ioentry_t *e;

    if (s->state != NS_CONNECTED) {
        return;
    }

    e = os_get_io(s->socket);

    FIFO_Reserve(&s->recv, &len);
    e->wantread = len ? qtrue : qfalse;

    FIFO_Peek(&s->send, &len);
    e->wantwrite = len ? qtrue : qfalse;
}

// returns NET_OK only when there was some data read
neterr_t NET_RunStream(netstream_t *s)
{
    ssize_t ret;
    size_t len;
    void *data;
    neterr_t result = NET_AGAIN;
    ioentry_t *e;

    if (s->state != NS_CONNECTED) {
        return result;
    }

    e = os_get_io(s->socket);
    if (e->wantread && e->canread) {
        // read as much as we can
        data = FIFO_Reserve(&s->recv, &len);
        if (len) {
            ret = os_recv(s->socket, data, len, 0);
            if (!ret) {
                goto closed;
            }
            if (ret == NET_ERROR) {
                goto error;
            }
            if (ret == NET_AGAIN) {
                // wouldblock is silent
                e->canread = qfalse;
            } else {
                FIFO_Commit(&s->recv, ret);
#if _DEBUG
                if (net_log_enable->integer) {
                    NET_LogPacket(&s->address, "TCP recv", data, ret);
                }
#endif
                net_rate_rcvd += ret;
                net_bytes_rcvd += ret;

                result = NET_OK;

                // now see if there's more space to read
                FIFO_Reserve(&s->recv, &len);
                if (!len) {
                    e->wantread = qfalse;
                }
            }
        }
    }

    if (e->wantwrite && e->canwrite) {
        // write as much as we can
        data = FIFO_Peek(&s->send, &len);
        if (len) {
            ret = os_send(s->socket, data, len, 0);
            if (!ret) {
                goto closed;
            }
            if (ret == NET_ERROR) {
                goto error;
            }
            if (ret == NET_AGAIN) {
                // wouldblock is silent
                e->canwrite = qfalse;
            } else {
                FIFO_Decommit(&s->send, ret);
#if _DEBUG
                if (net_log_enable->integer) {
                    NET_LogPacket(&s->address, "TCP send", data, ret);
                }
#endif
                net_rate_sent += ret;
                net_bytes_sent += ret;

                //result = NET_OK;

                // now see if there's more data to write
                FIFO_Peek(&s->send, &len);
                if (!len) {
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

static void dump_addrinfo(struct addrinfo *ai)
{
    char buf1[MAX_QPATH], buf2[MAX_STRING_CHARS];
    char *fa = (ai->ai_addr->sa_family == AF_INET6) ? "6" : "";

    getnameinfo(ai->ai_addr, ai->ai_addrlen,
                buf1, sizeof(buf1), NULL, 0, NI_NUMERICHOST);
    if (getnameinfo(ai->ai_addr, ai->ai_addrlen,
                    buf2, sizeof(buf2), NULL, 0, NI_NAMEREQD) == 0)
        Com_Printf("IP%1s     : %s (%s)\n", fa, buf1, buf2);
    else
        Com_Printf("IP%1s     : %s\n", fa, buf1);
}

static void dump_socket(qsocket_t s, const char *s1, const char *s2)
{
    netadr_t adr;

    if (s == -1)
        return;

    if (os_getsockname(s, &adr)) {
        Com_EPrintf("Couldn't get %s %s socket name: %s\n",
                    s1, s2, NET_ErrorString());
        return;
    }

    Com_Printf("%s %s socket bound to %s\n",
               s1, s2, NET_AdrToString(&adr));
}

/*
====================
NET_ShowIP_f
====================
*/
static void NET_ShowIP_f(void)
{
    char buffer[MAX_STRING_CHARS];
    struct addrinfo hints, *res, *rp;
    int err;

    if (gethostname(buffer, sizeof(buffer)) == -1) {
        Com_EPrintf("Couldn't get system host name\n");
        return;
    }

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_CANONNAME;
    hints.ai_socktype = SOCK_STREAM;

    if (net_enable_ipv6->integer < 1)
        hints.ai_family = AF_INET;

    err = getaddrinfo(buffer, NULL, &hints, &res);
    if (err) {
        Com_Printf("Couldn't resolve %s: %s\n", buffer, gai_strerror(err));
        return;
    }

    if (res->ai_canonname)
        Com_Printf("Hostname: %s\n", res->ai_canonname);

    for (rp = res; rp; rp = rp->ai_next)
        dump_addrinfo(rp);

    freeaddrinfo(res);

    // dump listening IP sockets
    dump_socket(udp_sockets[NS_CLIENT], "Client", "UDP");
    dump_socket(udp_sockets[NS_SERVER], "Server", "UDP");
    dump_socket(tcp_socket, "Server", "TCP");

    // dump listening IPv6 sockets
    dump_socket(udp6_sockets[NS_CLIENT], "Client", "UDP6");
    dump_socket(udp6_sockets[NS_SERVER], "Server", "UDP6");
    dump_socket(tcp6_socket, "Server", "TCP6");
}

/*
====================
NET_Dns_f
====================
*/
static void NET_Dns_f(void)
{
    char buffer[MAX_STRING_CHARS], *h, *p;
    struct addrinfo hints, *res, *rp;
    int err;

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s <address>\n", Cmd_Argv(0));
        return;
    }

    Cmd_ArgvBuffer(1, buffer, sizeof(buffer));

    // parse IPv6 address square brackets
    h = p = buffer;
    if (*h == '[') {
        h++;
        p = strchr(h, ']');
        if (!p) {
            Com_Printf("Bad IPv6 address\n");
            return;
        }
        *p++ = 0;
    }

    // strip off a trailing :port if present
    p = strchr(p, ':');
    if (p)
        *p++ = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_CANONNAME;
    hints.ai_socktype = SOCK_STREAM;

    if (net_enable_ipv6->integer < 1)
        hints.ai_family = AF_INET;

    err = getaddrinfo(h, NULL, &hints, &res);
    if (err) {
        Com_Printf("Couldn't resolve %s: %s\n", h, gai_strerror(err));
        return;
    }

    if (res->ai_canonname)
        Com_Printf("Hostname: %s\n", res->ai_canonname);

    for (rp = res; rp; rp = rp->ai_next)
        dump_addrinfo(rp);

    freeaddrinfo(res);
}

/*
====================
NET_Restart_f
====================
*/
static void NET_Restart_f(void)
{
    netflag_t flag = net_active;
    qboolean listen4 = (tcp_socket != -1);
    qboolean listen6 = (tcp6_socket != -1);

    Com_DPrintf("%s\n", __func__);

    NET_Listen4(qfalse);
    NET_Listen6(qfalse);
    NET_Config(NET_NONE);

    listen6 |= listen4;
    listen6 &= net_enable_ipv6->integer > 1;

    NET_Config(flag);
    NET_Listen4(listen4);
    NET_Listen6(listen6);

#if USE_SYSCON
    SV_SetConsoleTitle();
#endif
}

static void net_udp_param_changed(cvar_t *self)
{
    NET_Restart_f();
}

static const char *NET_EnableIP6(void)
{
    qsocket_t s = os_socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

    if (s == -1)
        return "0";

    os_closesocket(s);
    return "1";
}

/*
====================
NET_Init
====================
*/
void NET_Init(void)
{
    os_net_init();

    net_ip = Cvar_Get("net_ip", "", 0);
    net_ip->changed = net_udp_param_changed;
    net_ip6 = Cvar_Get("net_ip6", "", 0);
    net_ip6->changed = net_udp_param_changed;
    net_port = Cvar_Get("net_port", STRINGIFY(PORT_SERVER), 0);
    net_port->changed = net_udp_param_changed;

#if USE_CLIENT
    net_clientport = Cvar_Get("net_clientport", STRINGIFY(PORT_ANY), 0);
    net_clientport->changed = net_udp_param_changed;
    net_dropsim = Cvar_Get("net_dropsim", "0", 0);
#endif

#if _DEBUG
    net_log_enable = Cvar_Get("net_log_enable", "0", 0);
    net_log_enable->changed = net_log_enable_changed;
    net_log_name = Cvar_Get("net_log_name", "network", 0);
    net_log_name->changed = net_log_param_changed;
    net_log_flush = Cvar_Get("net_log_flush", "0", 0);
    net_log_flush->changed = net_log_param_changed;
#endif

    net_enable_ipv6 = Cvar_Get("net_enable_ipv6", NET_EnableIP6(), 0);
    net_enable_ipv6->changed = net_udp_param_changed;

#if USE_ICMP
    net_ignore_icmp = Cvar_Get("net_ignore_icmp", "0", 0);
#endif

#if _DEBUG
    net_log_enable_changed(net_log_enable);
#endif

    net_rate_time = com_eventTime;

    Cmd_AddCommand("net_restart", NET_Restart_f);
    Cmd_AddCommand("net_stats", NET_Stats_f);
    Cmd_AddCommand("showip", NET_ShowIP_f);
    Cmd_AddCommand("dns", NET_Dns_f);

    Cmd_AddMacro("net_uprate", NET_UpRate_m);
    Cmd_AddMacro("net_dnrate", NET_DnRate_m);
}

/*
====================
NET_Shutdown
====================
*/
void NET_Shutdown(void)
{
#if _DEBUG
    logfile_close();
#endif

    NET_Listen(qfalse);
    NET_Config(NET_NONE);
    os_net_shutdown();

    Cmd_RemoveCommand("net_restart");
    Cmd_RemoveCommand("net_stats");
    Cmd_RemoveCommand("showip");
    Cmd_RemoveCommand("dns");
}

