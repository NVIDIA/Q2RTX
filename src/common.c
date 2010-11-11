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
// common.c -- misc functions used in client and server
#include "com_local.h"
#include "files.h"
#include "protocol.h"
#include "q_msg.h"
#include "q_fifo.h"
#include "net_sock.h"
#include "net_chan.h"
#include "sys_public.h"
#include "cl_public.h"
#include "sv_public.h"
#include "q_list.h"
#include "bsp.h"
#include "cmodel.h"
#include "q_field.h"
#include "prompt.h"
#include "io_sleep.h"
#include <setjmp.h>
#if USE_ZLIB
#include <zlib.h>
#endif

static jmp_buf  abortframe;     // an ERR_DROP occured, exit the entire frame

static char     com_errorMsg[MAXPRINTMSG];

static char     **com_argv;
static int      com_argc;

#ifdef _DEBUG
cvar_t  *developer;
#endif
cvar_t  *timescale;
cvar_t  *fixedtime;
cvar_t  *dedicated;
cvar_t  *com_version;

cvar_t  *logfile_enable;    // 1 = create new, 2 = append to existing
cvar_t  *logfile_flush;     // 1 = flush after each print
cvar_t  *logfile_name;
cvar_t  *logfile_prefix;

#if USE_CLIENT
cvar_t  *cl_running;
cvar_t  *cl_paused;
#endif
cvar_t  *sv_running;
cvar_t  *sv_paused;
cvar_t  *com_timedemo;
cvar_t  *com_date_format;
cvar_t  *com_time_format;
#ifdef _DEBUG
cvar_t  *com_debug_break;
#endif
cvar_t  *com_fatal_error;

cvar_t  *allow_download;
cvar_t  *allow_download_players;
cvar_t  *allow_download_models;
cvar_t  *allow_download_sounds;
cvar_t  *allow_download_maps;
cvar_t  *allow_download_textures;
cvar_t  *allow_download_pics;
cvar_t  *allow_download_others;

cvar_t  *rcon_password;

qhandle_t   com_logFile;
qboolean    com_logNewline;
unsigned    com_framenum;
unsigned    com_eventTime;
unsigned    com_localTime;
qboolean    com_initialized;
time_t      com_startTime;

#if USE_CLIENT
cvar_t  *host_speeds;

// host_speeds times
unsigned    time_before_game;
unsigned    time_after_game;
unsigned    time_before_ref;
unsigned    time_after_ref;
#endif

/*
============================================================================

CLIENT / SERVER interactions

============================================================================
*/

static int          rd_target;
static char         *rd_buffer;
static size_t       rd_buffersize;
static size_t       rd_length;
static rdflush_t    rd_flush;

void Com_BeginRedirect( int target, char *buffer, size_t buffersize, rdflush_t flush ) {
    if( rd_target || !target || !buffer || buffersize < 1 || !flush ) {
        return;
    }
    rd_target = target;
    rd_buffer = buffer;
    rd_buffersize = buffersize;
    rd_flush = flush;
    rd_length = 0;
}

static void Com_AbortRedirect( void ) {
    rd_target = 0;
    rd_buffer = NULL;
    rd_buffersize = 0;
    rd_flush = NULL;
    rd_length = 0;
}

void Com_EndRedirect( void ) {
    if( !rd_target ) {
        return;
    }
    rd_flush( rd_target, rd_buffer, rd_length );
    rd_target = 0;
    rd_buffer = NULL;
    rd_buffersize = 0;
    rd_flush = NULL;
    rd_length = 0;
}

static void Com_Redirect( const char *msg, size_t total ) {
    size_t length;

    while( total ) {
        length = total;
        if( length > rd_buffersize ) {
            length = rd_buffersize;
        }
        if( rd_length + length > rd_buffersize ) {
            rd_flush( rd_target, rd_buffer, rd_length );
            rd_length = 0;
        }
        memcpy( rd_buffer + rd_length, msg, length );
        rd_length += length;
        total -= length;
    }
}

static void logfile_close( void ) {
    if( !com_logFile ) {
        return;
    }

    Com_Printf( "Closing console log.\n" );

    FS_FCloseFile( com_logFile );
    com_logFile = 0;
}

static void logfile_open( void ) {
    char buffer[MAX_OSPATH];
    unsigned mode;
    qhandle_t f;

    mode = logfile_enable->integer > 1 ? FS_MODE_APPEND : FS_MODE_WRITE;
    if( logfile_flush->integer ) {
        mode |= FS_FLUSH_SYNC;
    }

    f = FS_EasyOpenFile( buffer, sizeof( buffer ), mode,
        "logs/", logfile_name->string, ".log" );
    if( !f ) {
        Cvar_Set( "logfile", "0" );
        return;
    }

    com_logFile = f;
    com_logNewline = qtrue;
    Com_Printf( "Logging console to %s\n", buffer );
}

static void logfile_enable_changed( cvar_t *self ) {
    logfile_close();
    if( self->integer ) {
        logfile_open();
    }    
}

static void logfile_param_changed( cvar_t *self ) {
    if( logfile_enable->integer ) {
        logfile_close();
        logfile_open();
    }    
}

static size_t format_local_time( char *buffer, size_t size, const char *fmt ) {
    static struct tm cached_tm;
    static time_t cached_time;
    time_t now;
    struct tm *tm;

    if( !size ) {
        return 0;
    }

    buffer[0] = 0;

    now = time( NULL );
    if( now == cached_time ) {
        // avoid calling localtime() too often since it is not that cheap
        tm = &cached_tm;
    } else {
        tm = localtime( &now );
        if( !tm ) {
            return 0;
        }
        cached_time = now;
        cached_tm = *tm;
    }

    return strftime( buffer, size, fmt, tm );
}

static void logfile_write( print_type_t type, const char *string ) {
    char text[MAXPRINTMSG];
    char buf[MAX_QPATH];
    char *p, *maxp;
    size_t len;
    int c;

    if( logfile_prefix->string[0] ) {
        p = strchr( logfile_prefix->string, '@' );
        if( p ) {
            // expand it in place, hacky
            switch( type ) {
                case PRINT_TALK:      *p = 'T'; break;
                case PRINT_DEVELOPER: *p = 'D'; break;
                case PRINT_WARNING:   *p = 'W'; break;
                case PRINT_ERROR:     *p = 'E'; break;
                case PRINT_NOTICE:    *p = 'N'; break;
                default:              *p = 'A'; break;
            }
        }
        len = format_local_time( buf, sizeof( buf ), logfile_prefix->string );
        if( p ) {
            *p = '@';
        }
    } else {
        len = 0;
    }

    p = text;
    maxp = text + sizeof( text ) - 1;
    while( *string ) {
        if( com_logNewline ) {
            if( len > 0 && p + len < maxp ) {
                memcpy( p, buf, len );
                p += len;
            }
            com_logNewline = qfalse;
        }

        if( p == maxp ) {
            break;
        }

        c = *string++;
        if( c == '\n' ) {
            com_logNewline = qtrue;
        } else {
            c = Q_charascii( c );
        }
        
        *p++ = c;
    }
    *p = 0;

    len = p - text;
    FS_Write( text, len, com_logFile );
}

#ifdef __unix__
/*
=============
Com_FlushLogs

When called from SIGHUP handler on UNIX-like systems,
will close and reopen logfile handle for rotation.
=============
*/
void Com_FlushLogs( void ) {
    if( logfile_enable ) {
        logfile_enable_changed( logfile_enable );
    }
}
#endif

void Com_SetColor( color_index_t color ) {
    if( rd_target ) {
        return;
    }
#if USE_CLIENT
    // graphical console
    Con_SetColor( color );
#endif
#if USE_SYSCON
    // debugging console
    Sys_SetConsoleColor( color );
#endif
}

/*
=============
Com_Printf

Both client and server can use this, and it will output
to the apropriate place.
=============
*/
void Com_LPrintf( print_type_t type, const char *fmt, ... ) {
    va_list     argptr;
    char        msg[MAXPRINTMSG];
    static int  recursive;
    size_t      len;

    if( recursive == 2 ) {
        return;
    }

    recursive++;

    va_start( argptr, fmt );
    len = Q_vscnprintf( msg, sizeof( msg ), fmt, argptr );
    va_end( argptr );

    if( rd_target ) {
        Com_Redirect( msg, len );
    } else {
        switch( type ) {
        case PRINT_TALK:
            Com_SetColor( COLOR_ALT );
            break;
        case PRINT_DEVELOPER:
            Com_SetColor( COLOR_BLUE );
            break;
        case PRINT_WARNING:
            Com_SetColor( COLOR_YELLOW );
            break;
        case PRINT_ERROR:
            Com_SetColor( COLOR_RED );
            break;
        case PRINT_NOTICE:
            Com_SetColor( COLOR_CYAN );
            break;
        default:
            break;
        }

#if USE_CLIENT
        // graphical console
        Con_Print( msg );
#endif

#if USE_SYSCON
        // debugging console
        Sys_ConsoleOutput( msg );
#endif

        // remote console
        //SV_ConsoleOutput( msg );

        // logfile
        if( com_logFile ) {
            logfile_write( type, msg );
        }

        if( type ) {
            Com_SetColor( COLOR_NONE );
        }
    }

    recursive--;
}


/*
=============
Com_Error

Both client and server can use this, and it will
do the apropriate things.
=============
*/
void Com_Error( error_type_t code, const char *fmt, ... ) {
    va_list argptr;
    static qboolean recursive;

    if( recursive ) {
#ifdef _DEBUG
        Sys_DebugBreak();
#endif
        Sys_Error( "recursive error after: %s", com_errorMsg );
    }
    recursive = qtrue;

    va_start( argptr, fmt );
    Q_vsnprintf( com_errorMsg, sizeof( com_errorMsg ), fmt, argptr );
    va_end( argptr );

    // fix up drity message buffers
    MSG_Init();

    // abort any console redirects
    Com_AbortRedirect();
    
    if( code == ERR_DISCONNECT || code == ERR_SILENT ) {
        Com_WPrintf( "%s\n", com_errorMsg );
        SV_Shutdown( va( "Server was killed: %s", com_errorMsg ),
            KILL_DISCONNECT );
#if USE_CLIENT
        CL_Disconnect( code, com_errorMsg );
#endif
        goto abort;
    }

#ifdef _DEBUG
    if( com_debug_break && com_debug_break->integer ) {
        Sys_DebugBreak();
    }
#endif

    // make otherwise non-fatal errors fatal
    if( com_fatal_error && com_fatal_error->integer ) {
        code = ERR_FATAL;
    }

    if( code == ERR_DROP ) {
        Com_EPrintf( "********************\n"
                     "ERROR: %s\n"
                     "********************\n", com_errorMsg );
        SV_Shutdown( va( "Server crashed: %s\n", com_errorMsg ), KILL_DROP );
#if USE_CLIENT
        CL_Disconnect( ERR_DROP, com_errorMsg );
#endif
        goto abort;
    }

    if( com_logFile ) {
        FS_FPrintf( com_logFile, "FATAL: %s\n", com_errorMsg );
    }

    SV_Shutdown( va( "Server fatal crashed: %s\n", com_errorMsg ), KILL_DROP );
#if USE_CLIENT
    CL_Shutdown();
#endif
    Qcommon_Shutdown();

    Sys_Error( "%s", com_errorMsg );
    // doesn't get there

abort:
    if( com_logFile ) {
        FS_Flush( com_logFile );
    }
    recursive = qfalse;
    longjmp( abortframe, -1 );
}

#ifdef _WIN32
void Com_AbortFrame( void ) {
    longjmp( abortframe, -1 );
}
#endif

/*
=============
Com_Quit

Both client and server can use this, and it will
do the apropriate things. This function never returns.
=============
*/
void Com_Quit( const char *reason, killtype_t type ) {
    char buffer[MAX_STRING_CHARS];
    char *what = type == KILL_RESTART ? "restarted" : "quit";

    if( reason && *reason ) {
        Q_snprintf( buffer, sizeof( buffer ),
            "Server %s: %s\n", what, reason );
    } else {
        Q_snprintf( buffer, sizeof( buffer ),
            "Server %s\n", what );
    }
    SV_Shutdown( buffer, type );

#if USE_CLIENT
    CL_Shutdown();
#endif
    Qcommon_Shutdown();

    Sys_Quit();
}

static void Com_Quit_f( void ) {
    Com_Quit( Cmd_Args(), KILL_DROP );
}

#if !USE_CLIENT
static void Com_Recycle_f( void ) {
    Com_Quit( Cmd_Args(), KILL_RESTART );
}
#endif


/*
==============================================================================

                        ZONE MEMORY ALLOCATION

just cleared malloc with counters now...

==============================================================================
*/

#define Z_MAGIC     0x1d0d
#define Z_TAIL      0x5b7b

#define Z_TAIL_F( z ) \
    *( uint16_t * )( ( byte * )(z) + (z)->size - sizeof( uint16_t ) )

#define Z_FOR_EACH( z ) \
    for( (z) = z_chain.next; (z) != &z_chain; (z) = (z)->next )

#define Z_FOR_EACH_SAFE( z, n ) \
    for( (z) = z_chain.next; (z) != &z_chain; (z) = (n) )

typedef struct zhead_s {
    uint16_t    magic;
    uint16_t    tag;            // for group free
    size_t      size;
#ifdef _DEBUG
    void        *addr;
#endif
    struct zhead_s  *prev, *next;
} zhead_t;

// number of overhead bytes
#define Z_EXTRA ( sizeof( zhead_t ) + sizeof( uint16_t ) )

static zhead_t      z_chain;

static cvar_t       *z_perturb;

typedef struct {
    zhead_t     z;
    char        data[2];
    uint16_t    tail;
} zstatic_t;

static const zstatic_t z_static[] = {
#define Z_STATIC( x ) \
    { { Z_MAGIC, TAG_STATIC, q_offsetof( zstatic_t, tail ) + \
        sizeof( uint16_t ) }, x, Z_TAIL }

    Z_STATIC( "0" ),
    Z_STATIC( "1" ),
    Z_STATIC( "2" ),
    Z_STATIC( "3" ),
    Z_STATIC( "4" ),
    Z_STATIC( "5" ),
    Z_STATIC( "6" ),
    Z_STATIC( "7" ),
    Z_STATIC( "8" ),
    Z_STATIC( "9" ),
    Z_STATIC( "" )

#undef Z_STATIC
};

typedef struct {
    size_t count;
    size_t bytes;
} zstats_t;

static zstats_t z_stats[TAG_MAX];

static const char z_tagnames[TAG_MAX][8] = {
    "game",
    "static",
    "generic",
    "cmd",
    "cvar",
    "fs",
    "refresh",
    "ui",
    "server",
    "mvd",
    "sound",
    "cmodel"
};

static inline void Z_Validate( zhead_t *z, const char *func ) {
    if( z->magic != Z_MAGIC ) {
        Com_Error( ERR_FATAL, "%s: bad magic", func );
    }
    if( Z_TAIL_F( z ) != Z_TAIL ) {
        Com_Error( ERR_FATAL, "%s: bad tail", func );
    }
    if( z->tag == TAG_FREE ) {
        Com_Error( ERR_FATAL, "%s: bad tag", func );
    }
}

void Z_Check( void ) {
    zhead_t *z;

    Z_FOR_EACH( z ) {
        Z_Validate( z, __func__ );
    }
}

void Z_LeakTest( memtag_t tag ) {
    zhead_t *z;
    size_t numLeaks = 0, numBytes = 0;
    
    Z_FOR_EACH( z ) {
        Z_Validate( z, __func__ );
        if( z->tag == tag ) {
            numLeaks++;
            numBytes += z->size;
        }
    }

    if( numLeaks ) {
        Com_WPrintf( "************* Z_LeakTest *************\n"
                     "%s leaked %"PRIz" bytes of memory (%"PRIz" object%s)\n"
                     "**************************************\n",
                     z_tagnames[tag < TAG_MAX ? tag : TAG_FREE],
                     numBytes, numLeaks, numLeaks == 1 ? "" : "s" );
    }
}

/*
========================
Z_Free
========================
*/
void Z_Free( void *ptr ) {
    zhead_t *z;
    zstats_t *s;

    if( !ptr ) {
        return;
    }

    z = ( zhead_t * )ptr - 1;

    Z_Validate( z, __func__ );

    s = &z_stats[z->tag < TAG_MAX ? z->tag : TAG_FREE];
    s->count--;
    s->bytes -= z->size;
    
    if( z->tag != TAG_STATIC ) {
        z->prev->next = z->next;
        z->next->prev = z->prev;
        free( z );
    }
}

#if 0
/*
========================
Z_Realloc
========================
*/
void *Z_Realloc( void *ptr, size_t size ) {
    zhead_t *z;
    zstats_t *s;

    if( !ptr ) {
        return Z_Malloc( size );
    }

    if( !size ) {
        Z_Free( ptr );
        return NULL;
    }

    z = ( zhead_t * )ptr - 1;

    Z_Validate( z, __func__ );

    if( z->tag == TAG_STATIC ) {
        Com_Error( ERR_FATAL, "%s: couldn't realloc static memory", __func__ );
    }

    s = &z_stats[z->tag < TAG_MAX ? z->tag : TAG_FREE];
    s->bytes -= z->size;

    if( size > SIZE_MAX - Z_EXTRA - 3 ) {
        Com_Error( ERR_FATAL, "%s: bad size", __func__ );
    }

    size = ( size + Z_EXTRA + 3 ) & ~3;
    z = realloc( z, size );
    if( !z ) {
        Com_Error( ERR_FATAL, "%s: couldn't realloc %"PRIz" bytes", __func__, size );
    }

    z->size = size;
    z->prev->next = z;
    z->next->prev = z;

    s->bytes += size;

    Z_TAIL_F( z ) = Z_TAIL;

    return z + 1;
}
#endif

/*
========================
Z_Stats_f
========================
*/
static void Z_Stats_f( void ) {
    size_t bytes = 0, count = 0;
    zstats_t *s;
    int i;

    Com_Printf( "    bytes blocks name\n"
                "--------- ------ -------\n" );

    for( i = 0, s = z_stats; i < TAG_MAX; i++, s++ ) {
        if( !s->count ) {
            continue;
        }
        Com_Printf( "%9"PRIz" %6"PRIz" %s\n", s->bytes, s->count, z_tagnames[i] );
        bytes += s->bytes;
        count += s->count;
    }

    Com_Printf( "--------- ------ -------\n"
                "%9"PRIz" %6"PRIz" total\n",
                bytes, count );
}

/*
========================
Z_FreeTags
========================
*/
void Z_FreeTags( memtag_t tag ) {
    zhead_t *z, *n;

    Z_FOR_EACH_SAFE( z, n ) {
        Z_Validate( z, __func__ );
        n = z->next;
        if( z->tag == tag ) {
            Z_Free( z + 1 );
        }
    }
}

/*
========================
Z_TagMalloc
========================
*/
void *Z_TagMalloc( size_t size, memtag_t tag ) {
    zhead_t *z;
    zstats_t *s;

    if( !size ) {
        return NULL;
    }

    if( tag == TAG_FREE ) {
        Com_Error( ERR_FATAL, "%s: bad tag", __func__ );
    }
    
    if( size > SIZE_MAX - Z_EXTRA - 3 ) {
        Com_Error( ERR_FATAL, "%s: bad size", __func__ );
    }

    size = ( size + Z_EXTRA + 3 ) & ~3;
    z = malloc( size );
    if( !z ) {
        Com_Error( ERR_FATAL, "%s: couldn't allocate %"PRIz" bytes", __func__, size );
    }
    z->magic = Z_MAGIC;
    z->tag = tag;
    z->size = size;

#ifdef _DEBUG
#if( defined __GNUC__ )
    z->addr = __builtin_return_address( 0 );
#elif( defined _MSC_VER )
    z->addr = _ReturnAddress();
#else
    z->addr = NULL;
#endif
#endif

    z->next = z_chain.next;
    z->prev = &z_chain;
    z_chain.next->prev = z;
    z_chain.next = z;

    if( z_perturb && z_perturb->integer ) {
        memset( z + 1, z_perturb->integer, size -
            sizeof( zhead_t ) - sizeof( uint16_t ) );
    }

    Z_TAIL_F( z ) = Z_TAIL;

    s = &z_stats[tag < TAG_MAX ? tag : TAG_FREE];
    s->count++;
    s->bytes += size;

    return z + 1;
}

void *Z_TagMallocz( size_t size, memtag_t tag ) {
    if( !size ) {
        return NULL;
    }
    return memset( Z_TagMalloc( size, tag ), 0, size );
}

static byte     *z_reserved_data;
static size_t   z_reserved_inuse;
static size_t   z_reserved_total;

void Z_TagReserve( size_t size, memtag_t tag ) {
    z_reserved_data = Z_TagMalloc( size, tag );
    z_reserved_total = size;
    z_reserved_inuse = 0;
}

void *Z_ReservedAlloc( size_t size ) {
    void *ptr;

    if( !size ) {
        return NULL;
    }

    if( size > z_reserved_total - z_reserved_inuse ) {
        Com_Error( ERR_FATAL, "%s: couldn't allocate %"PRIz" bytes", __func__, size );
    }

    ptr = z_reserved_data + z_reserved_inuse;
    z_reserved_inuse += size;

    return ptr;
}

void *Z_ReservedAllocz( size_t size ) {
    if( !size ) {
        return NULL;
    }
    return memset( Z_ReservedAlloc( size ), 0, size );
}

char *Z_ReservedCopyString( const char *in ) {
    size_t len;

    if( !in ) {
        return NULL;
    }

    len = strlen( in ) + 1;
    return memcpy( Z_ReservedAlloc( len ), in, len );
}

/*
========================
Z_Init
========================
*/
static void Z_Init( void ) {
    z_chain.next = z_chain.prev = &z_chain;
}

/*
================
Z_TagCopyString
================
*/
char *Z_TagCopyString( const char *in, memtag_t tag ) {
    size_t len;

    if( !in ) {
        return NULL;
    }

    len = strlen( in ) + 1;
    return memcpy( Z_TagMalloc( len, tag ), in, len );
}

/*
================
Cvar_CopyString
================
*/
char *Cvar_CopyString( const char *in ) {
    size_t len;
    zstatic_t *z;
    zstats_t *s;
    int i;

    if( !in ) {
        return NULL;
    }

    if( !in[0] ) {
        i = 10;
    } else if( !in[1] && Q_isdigit( in[0] ) ) {
        i = in[0] - '0';
    } else {
        len = strlen( in ) + 1;
        return memcpy( Z_TagMalloc( len, TAG_CVAR ), in, len );
    }

    // return static storage
    z = ( zstatic_t * )&z_static[i];
    s = &z_stats[TAG_STATIC];
    s->count++;
    s->bytes += z->z.size;
    return z->data;
}

/*
==============================================================================

                        FIFO

==============================================================================
*/

size_t FIFO_Read( fifo_t *fifo, void *buffer, size_t len ) {
    size_t wrapped, head = fifo->ay - fifo->ax;

    if( head > len ) {
        if( buffer ) {
            memcpy( buffer, fifo->data + fifo->ax, len );
            fifo->ax += len;
        }
        return len;
    }

    wrapped = len - head;
    if( wrapped > fifo->bs ) {
        wrapped = fifo->bs;
    }
    if( buffer ) {
        memcpy( buffer, fifo->data + fifo->ax, head );
        memcpy( ( byte * )buffer + head, fifo->data, wrapped );
        fifo->ax = wrapped;
        fifo->ay = fifo->bs;
        fifo->bs = 0;
    }

    return head + wrapped;
}

size_t FIFO_Write( fifo_t *fifo, const void *buffer, size_t len ) {
    size_t tail, wrapped, remaining;

    if( fifo->bs ) {
        remaining = fifo->ax - fifo->bs;
        if( len > remaining ) {
            len = remaining;
        }
        if( buffer ) {
            memcpy( fifo->data + fifo->bs, buffer, len );
            fifo->bs += len;
        }
        return len;
    }

    tail = fifo->size - fifo->ay;
    if( tail > len ) {
        if( buffer ) {
            memcpy( fifo->data + fifo->ay, buffer, len );
            fifo->ay += len;
        }
        return len;
    }

    wrapped = len - tail;
    if( wrapped > fifo->ax ) {
        wrapped = fifo->ax;
    }
    if( buffer ) {
        memcpy( fifo->data + fifo->ay, buffer, tail );
        memcpy( fifo->data, ( byte * )buffer + tail, wrapped );
        fifo->ay = fifo->size;
        fifo->bs = wrapped;
    }

    return tail + wrapped;
}

qboolean FIFO_ReadMessage( fifo_t *fifo, size_t msglen ) {
    size_t len;
    byte *data;

    data = FIFO_Peek( fifo, &len );
    if( len < msglen ) {
        // read in two chunks into message buffer
        if( !FIFO_TryRead( fifo, msg_read_buffer, msglen ) ) {
            return qfalse; // not yet available
        }
        SZ_Init( &msg_read, msg_read_buffer, sizeof( msg_read_buffer ) );
    } else {
        // read in a single block without copying any memory
        SZ_Init( &msg_read, data, msglen );
        FIFO_Decommit( fifo, msglen );
    }

    msg_read.cursize = msglen;
    return qtrue;
}

/*
==============================================================================

                        MATH

==============================================================================
*/

const vec3_t bytedirs[NUMVERTEXNORMALS] = {
#include "anorms.h"
};

int DirToByte( const vec3_t dir ) {
    int     i, best;
    float   d, bestd;
    
    if( !dir ) {
        return 0;
    }

    bestd = 0;
    best = 0;
    for( i = 0; i < NUMVERTEXNORMALS; i++ ) {
        d = DotProduct( dir, bytedirs[i] );
        if( d > bestd ) {
            bestd = d;
            best = i;
        }
    }
    
    return best;
}

void ByteToDir( int index, vec3_t dir ) {
    if( index < 0 || index >= NUMVERTEXNORMALS ) {
        Com_Error( ERR_FATAL, "ByteToDir: illegal index" );
    }

    VectorCopy( bytedirs[index], dir );
}

void SetPlaneType( cplane_t *plane ) {
    vec_t *normal = plane->normal;
    
    if( normal[0] == 1 ) {
        plane->type = PLANE_X;
        return;
    }
    if( normal[1] == 1 ) {
        plane->type = PLANE_Y;
        return;
    }
    if( normal[2] == 1 ) {
        plane->type = PLANE_Z;
        return;
    }

    plane->type = PLANE_NON_AXIAL;
}

void SetPlaneSignbits( cplane_t *plane ) {
    int bits = 0;
    
    if( plane->normal[0] < 0 ) {
        bits |= 1;
    }
    if( plane->normal[1] < 0 ) {
        bits |= 2;
    }
    if( plane->normal[2] < 0 ) {
        bits |= 4;
    }
    plane->signbits = bits;
}

/*
==================
BoxOnPlaneSide

Returns 1, 2, or 1 + 2
==================
*/
#if !USE_ASM
int BoxOnPlaneSide( vec3_t emins, vec3_t emaxs, cplane_t *p ) {
    vec_t   *bounds[2] = { emins, emaxs };
    int     i = p->signbits & 1;
    int     j = ( p->signbits >> 1 ) & 1;
    int     k = ( p->signbits >> 2 ) & 1;

#define P(i,j,k) \
    p->normal[0]*bounds[i][0]+ \
    p->normal[1]*bounds[j][1]+ \
    p->normal[2]*bounds[k][2]

    vec_t   dist1 = P( i ^ 1, j ^ 1, k ^ 1 );
    vec_t   dist2 = P( i, j, k );
    int     sides = 0;

#undef P

    if (dist1 >= p->dist)
        sides = 1;
    if (dist2 < p->dist)
        sides |= 2;

    return sides;
}
#endif // USE_ASM

/*
==============================================================================

                        WILDCARD COMPARE

==============================================================================
*/

static qboolean match_char( int c1, int c2, qboolean ignorecase ) {
    if( c1 == '?' ) {
        return !!c2; // match any char except NUL
    }

    if( c1 != c2 ) {
        if( !ignorecase ) {
            return qfalse;
        }
#ifdef _WIN32
        // ugly hack for file listing
        c1 = c1 == '\\' ? '/' : Q_tolower( c1 );
        c2 = c2 == '\\' ? '/' : Q_tolower( c2 );
#else
        c1 = Q_tolower( c1 );
        c2 = Q_tolower( c2 );
#endif
        if( c1 != c2 ) {
            return qfalse;
        }
    }

    return qtrue;
}

static qboolean match_part( const char *filter, const char *string, size_t len, qboolean ignorecase ) {
    do {
        int c1 = *filter++;
        int c2 = *string++;

        if( !match_char( c1, c2, ignorecase ) ) {
            return qfalse;
        }
    } while( --len );

    return qtrue;
}

// match the longest possible part
static const char *match_filter( const char *filter, const char *string, size_t len, qboolean ignorecase ) {
    const char *ret = NULL;
    size_t remaining = strlen( string );

    while( remaining >= len ) {
        if( match_part( filter, string, len, ignorecase ) ) {
            string += len;
            remaining -= len;
            ret = string;
            continue;
        }
        string++;
        remaining--;
    }

    return ret;
}

/*
=================
Com_WildCmpEx

Wildcard compare.
Returns non-zero if matches, zero otherwise.
=================
*/
qboolean Com_WildCmpEx( const char *filter, const char *string, int term, qboolean ignorecase ) {
    const char *sub;
    size_t len;

    while( *filter && *filter != term ) {
        if( *filter == '*' ) {
            // skip consecutive wildcards
            do {
                filter++;
            } while( *filter == '*' );

            // wildcard at the end matches everything
            if( !*filter || *filter == term ) {
                return qtrue;
            }

            // scan out filter part to match
            sub = filter; len = 0;
            do {
                filter++; len++;
            } while( *filter && *filter != term && *filter != '*' );

            string = match_filter( sub, string, len, ignorecase );
            if( !string ) {
                return qfalse;
            }
        } else {
            int c1 = *filter++;
            int c2 = *string++;

            // match single character
            if( !match_char( c1, c2, ignorecase ) ) {
                return qfalse;
            }
        }
    }

    // match NUL at the end
    return !*string;
}

/*
==============================================================================

                        MISC

==============================================================================
*/

const char colorNames[10][8] = {
    "black", "red", "green", "yellow",
    "blue", "cyan", "magenta", "white",
    "alt", "none"
};

/*
================
Com_ParseColor

Parses color name or index up to the maximum allowed index.
Returns COLOR_NONE in case of error.
================
*/
color_index_t Com_ParseColor( const char *s, color_index_t last ) {
    color_index_t i;

    if( COM_IsUint( s ) ) {
        i = strtoul( s, NULL, 10 );
        return i > last ? COLOR_NONE : i;
    }

    for( i = 0; i <= last; i++ ) {
        if( !strcmp( colorNames[i], s ) ) {
            return i;
        }
    }
    return COLOR_NONE;
}

/*
================
Com_PlayerToEntityState

Restores entity origin and angles from player state
================
*/
void Com_PlayerToEntityState( const player_state_t *ps, entity_state_t *es ) {
    vec_t pitch;

    VectorScale( ps->pmove.origin, 0.125f, es->origin );

    pitch = ps->viewangles[PITCH];
    if( pitch > 180 ) {
        pitch -= 360;
    }
    es->angles[PITCH] = pitch / 3;
    es->angles[YAW] = ps->viewangles[YAW];
    es->angles[ROLL] = 0;
}

/*
================
Com_HashString
================
*/
unsigned Com_HashString( const char *s, unsigned size ) {
    unsigned hash, c;

    hash = 0;
    while( *s ) {
        c = *s++;
        hash = 127 * hash + c;
    }

    hash = ( hash >> 20 ) ^ ( hash >> 10 ) ^ hash;
    return hash & ( size - 1 );
}


/*
===============
Com_PageInMemory

===============
*/
int    paged_total;

void Com_PageInMemory( void *buffer, size_t size ) {
    int        i;

    for( i = size - 1; i > 0; i -= 4096 )
        paged_total += (( byte * )buffer)[i];
}

size_t Com_FormatTime( char *buffer, size_t size, time_t t ) {
    int     sec, min, hour, day;

    min = t / 60; sec = t % 60;
    hour = min / 60; min %= 60;
    day = hour / 24; hour %= 24;

    if( day ) {
        return Q_scnprintf( buffer, size, "%d+%d:%02d.%02d", day, hour, min, sec );
    }
    if( hour ) {
        return Q_scnprintf( buffer, size, "%d:%02d.%02d", hour, min, sec );
    }
    return Q_scnprintf( buffer, size, "%02d.%02d", min, sec );
}

size_t Com_FormatTimeLong( char *buffer, size_t size, time_t t ) {
    int     sec, min, hour, day;
    size_t  len;

    if( !t ) {
        return Q_scnprintf( buffer, size, "0 secs" );
    }

    min = t / 60; sec = t % 60;
    hour = min / 60; min %= 60;
    day = hour / 24; hour %= 24;

    len = 0;

    if( day ) {
        len += Q_scnprintf( buffer + len, size - len,
            "%d day%s%s", day, day == 1 ? "" : "s", ( hour || min || sec ) ? ", " : "" );
    }
    if( hour ) {
        len += Q_scnprintf( buffer + len, size - len,
            "%d hour%s%s", hour, hour == 1 ? "" : "s", ( min || sec ) ? ", " : "" );
    }
    if( min ) {
        len += Q_scnprintf( buffer + len, size - len,
            "%d min%s%s", min, min == 1 ? "" : "s", sec ? ", " : "" );
    }
    if( sec ) {
        len += Q_scnprintf( buffer + len, size - len,
            "%d sec%s", sec, sec == 1 ? "" : "s" );
    }

    return len;
}

size_t Com_TimeDiff( char *buffer, size_t size, time_t *p, time_t now ) {
    time_t diff;

    if( *p > now ) {
        *p = now;
    }
    diff = now - *p;
    return Com_FormatTime( buffer, size, diff );
}

size_t Com_TimeDiffLong( char *buffer, size_t size, time_t *p, time_t now ) {
    time_t diff;

    if( *p > now ) {
        *p = now;
    }
    diff = now - *p;
    return Com_FormatTimeLong( buffer, size, diff );
}

size_t Com_FormatSize( char *dest, size_t destsize, off_t bytes ) {
    if( bytes >= 10000000 ) {
        return Q_scnprintf( dest, destsize, "%dM", (int)(bytes / 1000000) );
    }
    if( bytes >= 1000000 ) {
        return Q_scnprintf( dest, destsize, "%.1fM", (float)bytes / 1000000 );
    }
    if( bytes >= 1000 ) {
        return Q_scnprintf( dest, destsize, "%dK", (int)(bytes / 1000) );
    }
    if( bytes >= 0 ) {
        return Q_scnprintf( dest, destsize, "%d", (int)bytes );
    }
    return Q_scnprintf( dest, destsize, "???" );
}

size_t Com_FormatSizeLong( char *dest, size_t destsize, off_t bytes ) {
    if( bytes >= 10000000 ) {
        return Q_scnprintf( dest, destsize, "%d MB", (int)(bytes / 1000000) );
    }
    if( bytes >= 1000000 ) {
        return Q_scnprintf( dest, destsize, "%.1f MB", (float)bytes / 1000000 );
    }
    if( bytes >= 1000 ) {
        return Q_scnprintf( dest, destsize, "%d kB", (int)(bytes / 1000) );
    }
    if( bytes >= 0 ) {
        return Q_scnprintf( dest, destsize, "%d byte%s",
            (int)bytes, bytes == 1 ? "" : "s"  );
    }
    return Q_scnprintf( dest, destsize, "unknown size" );
}

/*
==============================================================================

                        INIT / SHUTDOWN

==============================================================================
*/

size_t Com_Time_m( char *buffer, size_t size ) {
    return format_local_time( buffer, size, com_time_format->string );
}

static size_t Com_Date_m( char *buffer, size_t size ) {
    return format_local_time( buffer, size, com_date_format->string );
}

size_t Com_Uptime_m( char *buffer, size_t size ) {
    return Com_TimeDiff( buffer, size, &com_startTime, time( NULL ) );
}

size_t Com_UptimeLong_m( char *buffer, size_t size ) {
    return Com_TimeDiffLong( buffer, size, &com_startTime, time( NULL ) );
}

static size_t Com_Random_m( char *buffer, size_t size ) {
    return Q_scnprintf( buffer, size, "%d", rand_byte() % 10 );
}

static size_t Com_MapList_m( char *buffer, size_t size ) {
    int i, numFiles;
    void **list;
    char *s, *p;
    size_t len, total = 0;

    list = FS_ListFiles( "maps", ".bsp", 0, &numFiles );
    for( i = 0; i < numFiles; i++ ) {
        s = list[i];
        p = COM_FileExtension( list[i] );
        *p = 0;
        len = strlen( s );
        if( total + len + 1 < size ) {
            memcpy( buffer + total, s, len );
            buffer[total + len] = ' ';
            total += len + 1;
        }
        Z_Free( s );
    }
    buffer[total] = 0;

    Z_Free( list );
    return total;
}

static void Com_LastError_f( void ) {
    if( com_errorMsg[0] ) {
        Com_Printf( "%s\n", com_errorMsg );
    } else {
        Com_Printf( "No error.\n" );
    }
}

#ifndef __COREDLL__
static void Com_Setenv_f( void ) {
    int argc = Cmd_Argc();

    if( argc > 2 ) {
        Q_setenv( Cmd_Argv( 1 ), Cmd_ArgsFrom( 2 ) );
    } else if( argc == 2 ) {
        char *env = getenv( Cmd_Argv( 1 ) );

        if( env ) {
            Com_Printf( "%s=%s\n", Cmd_Argv( 1 ), env );
        } else {
            Com_Printf( "%s undefined\n", Cmd_Argv( 1 ) );
        }
    } else {
        Com_Printf( "Usage: %s <name> [value]\n", Cmd_Argv( 0 ) );
    }
}
#endif

#ifdef _DEBUG

/*
=============
Com_Error_f

Just throw a fatal error to
test error shutdown procedures
=============
*/
static void Com_Error_f( void ) {
    Com_Error( ERR_FATAL, "%s", Cmd_Argv( 1 ) );
}

static void Com_ErrorDrop_f( void ) {
    Com_Error( ERR_DROP, "%s", Cmd_Argv( 1 ) );
}

static void Com_Freeze_f( void ) {
    unsigned time, msec;
    float seconds;

    if( Cmd_Argc() < 2 ) {
        Com_Printf( "Usage: %s <seconds>\n", Cmd_Argv( 0 ) );
        return;
    }

    seconds = atof( Cmd_Argv( 1 ) );
    if( seconds < 0 ) {
        return;
    }

    time = Sys_Milliseconds();
    msec = seconds * 1000;
    while( Sys_Milliseconds() - time < msec )
        ;
}

static void Com_Crash_f( void ) {
    *( uint32_t * )0 = 0x123456;
}

#endif

void Com_Address_g( genctx_t *ctx ) {
    int i;
    cvar_t *var;
    
    for( i = 0; i < 1024; i++ ) {
        var = Cvar_FindVar( va( "adr%d", i ) );
        if( !var ) {
            break;
        }
        if( !var->string[0] ) {
            continue;
        }
        if( !Prompt_AddMatch( ctx, var->string ) ) {
            break;
        }
    }
}

void Com_Generic_c( genctx_t *ctx, int argnum ) {
    xcompleter_t c;
    xgenerator_t g;
    cvar_t *var;
    char *s;

    // complete command, alias or cvar name
    if( !argnum ) {
        Cmd_Command_g( ctx );
        Cvar_Variable_g( ctx );
        Cmd_Alias_g( ctx );
        return;
    }

    s = Cmd_Argv( ctx->argnum - argnum );

    // complete command argument or cvar value
    if( ( c = Cmd_FindCompleter( s ) ) != NULL ) {
        c( ctx, argnum );
    } else if( argnum == 1 && ( var = Cvar_FindVar( s ) ) != NULL ) {
        g = var->generator;
        if( g ) {
            ctx->data = var;
            g( ctx );
        }
    }
}

#if USE_CLIENT
void Com_Color_g( genctx_t *ctx ) {
    int color;

    for( color = 0; color < 8; color++ ) {
        if( !Prompt_AddMatch( ctx, colorNames[color] ) ) {
            break;
        }
    }
}
#endif

/*
===============
Com_AddEarlyCommands

Adds command line parameters as script statements.
Commands lead with a +, and continue until another +

Set commands are added early, so they are guaranteed to be set before
the client and server initialize for the first time.

Other commands are added late, after all initialization is complete.
===============
*/
static void Com_AddEarlyCommands( qboolean clear ) {
    int     i;
    char    *s;

    for( i = 1; i < com_argc; i++ ) {
        s = com_argv[i];
        if( !s ) {
            continue;
        }
        if( strcmp( s, "+set" ) ) {
            continue;
        }
        if( i + 2 >= com_argc ) {
            Com_Printf( "Usage: +set <variable> <value>\n" );
            com_argc = i;
            break;
        }
        Cvar_SetEx( com_argv[ i + 1 ], com_argv[ i + 2 ], FROM_CMDLINE );
        if( clear ) {
            com_argv[i] = com_argv[ i + 1 ] = com_argv[ i + 2 ] = NULL;
        }
        i += 2;
    }
}

/*
=================
Com_AddLateCommands

Adds command line parameters as script statements
Commands lead with a + and continue until another +

Returns qtrue if any late commands were added, which
will keep the demoloop from immediately starting

Assumes +set commands are already filtered out
=================
*/
static qboolean Com_AddLateCommands( void ) {
    int     i;
    char    *s;
    qboolean ret = qfalse;

    for( i = 1; i < com_argc; i++ ) {
        s = com_argv[i];
        if( !s ) {
            continue;
        }
        if( *s == '+' ) {
            if( ret ) {
                Cbuf_AddText( &cmd_buffer, "\n" );
            }
            s++;
        } else if( ret ) {
            Cbuf_AddText( &cmd_buffer, " " );
        }
        Cbuf_AddText( &cmd_buffer, s );
        ret = qtrue;
    }

    if( ret ) {
        Cbuf_AddText( &cmd_buffer, "\n" );
        Cbuf_Execute( &cmd_buffer );
    }

    return ret;
}


/*
=================
Qcommon_Init
=================
*/
void Qcommon_Init( int argc, char **argv ) {
    static const char version[] = APPLICATION " " VERSION " " __DATE__ " " BUILDSTRING " " CPUSTRING;

    if( setjmp( abortframe ) )
        Sys_Error( "Error during initialization: %s", com_errorMsg );

    com_argc = argc;
    com_argv = argv;

    // prepare enough of the subsystems to handle
    // cvar and command buffer management
    Z_Init();
    MSG_Init();
    Cbuf_Init();
    Cmd_Init();
    Cvar_Init();
    Key_Init();
    Prompt_Init();
#if USE_CLIENT
    Con_Init();
#endif
    
    //
    // init commands and vars
    //
    z_perturb = Cvar_Get( "z_perturb", "0", 0 );
#if USE_CLIENT
    host_speeds = Cvar_Get ("host_speeds", "0", 0);
#endif
#ifdef _DEBUG
    developer = Cvar_Get ("developer", "0", 0);
#endif
    timescale = Cvar_Get ("timescale", "1", CVAR_CHEAT );
    fixedtime = Cvar_Get ("fixedtime", "0", CVAR_CHEAT );
    logfile_enable = Cvar_Get( "logfile", "0", 0 );
    logfile_flush = Cvar_Get( "logfile_flush", "0", 0 );
    logfile_name = Cvar_Get( "logfile_name", "console", 0 );
    logfile_prefix = Cvar_Get( "logfile_prefix", "[%Y-%m-%d %H:%M] ", 0 );
#if USE_CLIENT
    dedicated = Cvar_Get ("dedicated", "0", CVAR_NOSET);
    cl_running = Cvar_Get( "cl_running", "0", CVAR_ROM );
    cl_paused = Cvar_Get( "cl_paused", "0", CVAR_ROM );
#else
    dedicated = Cvar_Get ("dedicated", "1", CVAR_ROM);
#endif
    sv_running = Cvar_Get( "sv_running", "0", CVAR_ROM );
    sv_paused = Cvar_Get( "sv_paused", "0", CVAR_ROM );
    com_timedemo = Cvar_Get( "timedemo", "0", CVAR_CHEAT );
    com_date_format = Cvar_Get( "com_date_format", "%Y-%m-%d", 0 );
#ifdef _WIN32
    com_time_format = Cvar_Get( "com_time_format", "%H.%M", 0 );
#else
    com_time_format = Cvar_Get( "com_time_format", "%H:%M", 0 );
#endif
#ifdef _DEBUG
    com_debug_break = Cvar_Get( "com_debug_break", "0", 0 );
#endif
    com_fatal_error = Cvar_Get( "com_fatal_error", "0", 0 );
    com_version = Cvar_Get( "version", version, CVAR_SERVERINFO|CVAR_ROM );

    allow_download = Cvar_Get( "allow_download", "0", CVAR_ARCHIVE );
    allow_download_players = Cvar_Get( "allow_download_players", "1", CVAR_ARCHIVE );
    allow_download_models = Cvar_Get( "allow_download_models", "1", CVAR_ARCHIVE );
    allow_download_sounds = Cvar_Get( "allow_download_sounds", "1", CVAR_ARCHIVE );
    allow_download_maps = Cvar_Get( "allow_download_maps", "1", CVAR_ARCHIVE );
    allow_download_textures = Cvar_Get( "allow_download_textures", "1", CVAR_ARCHIVE );
    allow_download_pics = Cvar_Get( "allow_download_pics", "1", CVAR_ARCHIVE );
    allow_download_others = Cvar_Get( "allow_download_others", "0", 0 );

    rcon_password = Cvar_Get( "rcon_password", "", CVAR_PRIVATE );

    Cmd_AddCommand ("z_stats", Z_Stats_f);

#ifndef __COREDLL__
    Cmd_AddCommand( "setenv", Com_Setenv_f );
#endif

    Cmd_AddMacro( "com_date", Com_Date_m );
    Cmd_AddMacro( "com_time", Com_Time_m );
    Cmd_AddMacro( "com_uptime", Com_Uptime_m );
    Cmd_AddMacro( "com_uptime_long", Com_UptimeLong_m );
    Cmd_AddMacro( "random", Com_Random_m );
    Cmd_AddMacro( "com_maplist", Com_MapList_m );

    // add any system-wide configuration files
    Sys_AddDefaultConfig();
    Cbuf_Execute( &cmd_buffer );
    
    // we need to add the early commands twice, because
    // a basedir or cddir needs to be set before execing
    // config files, but we want other parms to override
    // the settings of the config files
    Com_AddEarlyCommands( qfalse );

    Sys_Init();

#if USE_SYSCON
    Sys_RunConsole();
#endif

    // print version
    Com_LPrintf( PRINT_NOTICE, "%s\n", version );

    FS_Init();

#if USE_SYSCON
    Sys_RunConsole();
#endif

    // no longer allow CVAR_NOSET modifications
    com_initialized = qtrue;

    // after FS is initialized, open logfile
    logfile_enable->changed = logfile_enable_changed;
    logfile_flush->changed = logfile_param_changed;
    logfile_name->changed = logfile_param_changed;
    logfile_enable_changed( logfile_enable );

    Cbuf_AddText( &cmd_buffer, "exec "COM_DEFAULTCFG_NAME"\n" );
    Cbuf_Execute( &cmd_buffer );
    
    Cbuf_AddText( &cmd_buffer, "exec "COM_CONFIG_NAME"\n" );
    Cbuf_Execute( &cmd_buffer );

    Cbuf_AddText( &cmd_buffer, "exec "COM_AUTOEXECCFG_NAME"\n" );
    Cbuf_Execute( &cmd_buffer );

    Com_AddEarlyCommands( qtrue );

#ifdef _DEBUG
    Cmd_AddCommand( "error", Com_Error_f );
    Cmd_AddCommand( "errordrop", Com_ErrorDrop_f );
    Cmd_AddCommand( "freeze", Com_Freeze_f );
    Cmd_AddCommand( "crash", Com_Crash_f );
#endif

    Cmd_AddCommand( "lasterror", Com_LastError_f );

    Cmd_AddCommand( "quit", Com_Quit_f );
#if !USE_CLIENT
    Cmd_AddCommand( "recycle", Com_Recycle_f );
#endif

    srand( Sys_Milliseconds() );

    Netchan_Init();
    NET_Init();
    BSP_Init();
    CM_Init();
    SV_Init();
#if USE_CLIENT
    CL_Init();
#endif

#if USE_SYSCON
    Sys_RunConsole();
#endif

    // add + commands from command line
    if( !Com_AddLateCommands() ) {
        // if the user didn't give any commands, run default action
        if( Com_IsDedicated() ) {
            Cbuf_AddText( &cmd_buffer, "dedicated_start\n" );
        } else {
            // TODO
            //Cbuf_AddText( "d1\n" );
        }
        Cbuf_Execute( &cmd_buffer );
    }
#if USE_CLIENT
    else {
        // the user asked for something explicit
        // so drop the loading plaque
        SCR_EndLoadingPlaque();
    }
#endif

    // even not given a starting map, dedicated server starts
    // listening for rcon commands (create socket after all configs
    // are executed to make sure port number is properly set)
    if( Com_IsDedicated() ) {
        NET_Config( NET_SERVER );
    }

    Com_Printf( "====== " APPLICATION " initialized ======\n\n" );
    Com_LPrintf( PRINT_NOTICE, APPLICATION " " VERSION ", " __DATE__ "\n" );
    Com_Printf( "http://skuller.net/q2pro/\n\n" );

    time( &com_startTime );

    com_eventTime = Sys_Milliseconds();
}

/*
=================
Qcommon_Frame
=================
*/
void Qcommon_Frame( void ) {
#if USE_CLIENT
    unsigned time_before, time_event, time_between, time_after;
#endif
    unsigned oldtime, msec, clientrem;
    static unsigned remaining;
    static float frac;

    if( setjmp( abortframe ) ) {
        return;            // an ERR_DROP was thrown
    }

#if USE_CLIENT
    time_before = time_event = time_between = time_after = 0;

    if( host_speeds->integer )
        time_before = Sys_Milliseconds();
#endif

    // sleep on network sockets when running a dedicated server
    // still do a select(), but don't sleep when running a client!
    IO_Sleep( remaining );

    // calculate time spent running last frame and sleeping
    oldtime = com_eventTime;
    com_eventTime = Sys_Milliseconds();
    if( oldtime > com_eventTime ) {
        oldtime = com_eventTime;
    }
    msec = com_eventTime - oldtime;

#if USE_CLIENT
    // spin until msec is non-zero if running a client
    if( !dedicated->integer && !com_timedemo->integer ) {
        while( msec < 1 ) {
            CL_ProcessEvents();
            com_eventTime = Sys_Milliseconds();
            msec = com_eventTime - oldtime;
        }
    }
#endif

    if( msec > 250 ) {
        Com_DPrintf( "Hitch warning: %u msec frame time\n", msec );
        msec = 100; // time was unreasonable,
                    // host OS was hibernated or something
    }

    if( fixedtime->integer ) {
        Cvar_ClampInteger( fixedtime, 1, 1000 );
        msec = fixedtime->integer;
    } else if( timescale->value > 0 ) {
        frac += msec * timescale->value;
        msec = frac;
        frac -= msec;
    }

    // run local time
    com_localTime += msec;
    com_framenum++;

#if USE_CLIENT
    if( host_speeds->integer )
        time_event = Sys_Milliseconds();
#endif

#if USE_SYSCON
    // run system console
    Sys_RunConsole();
#endif

    remaining = SV_Frame( msec );

#if USE_CLIENT
    if( host_speeds->integer )
        time_between = Sys_Milliseconds();

    clientrem = CL_Frame( msec );
    if( remaining > clientrem ) {
        remaining = clientrem;
    }

    if( host_speeds->integer )
        time_after = Sys_Milliseconds();

    if( host_speeds->integer ) {
        int all, ev, sv, gm, cl, rf;

        all = time_after - time_before;
        ev = time_event - time_before;
        sv = time_between - time_event;
        cl = time_after - time_between;
        gm = time_after_game - time_before_game;
        rf = time_after_ref - time_before_ref;
        sv -= gm;
        cl -= rf;

        Com_Printf( "all:%3i ev:%3i sv:%3i gm:%3i cl:%3i rf:%3i\n",
            all, ev, sv, gm, cl, rf );
    }
#endif

    // this is the only place where console commands are processed.
    Cbuf_Execute( &cmd_buffer );
}

/*
=================
Qcommon_Shutdown
=================
*/
void Qcommon_Shutdown( void ) {
    NET_Shutdown();
    logfile_close();
    FS_Shutdown();
}

