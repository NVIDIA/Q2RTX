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

#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>
#include <dirent.h>
#include <dlfcn.h>
#include <termios.h>
#include <sys/ioctl.h>
#ifndef __linux__
#include <machine/param.h>
#endif

#include "com_local.h"
#include "q_list.h"
#include "q_field.h"
#include "prompt.h"
#include "files.h"
#if USE_REF
#include "vid_public.h"
#endif
#include "sys_public.h"
#include "io_sleep.h"

cvar_t  *sys_basedir;
cvar_t  *sys_libdir;
cvar_t  *sys_homedir;
cvar_t  *sys_parachute;


/*
===============================================================================

TERMINAL SUPPORT

===============================================================================
*/

#if USE_SYSCON

cvar_t  *sys_console;

static qboolean         tty_enabled;
static struct termios   tty_orig;
static commandPrompt_t  tty_prompt;
static int              tty_hidden;
static ioentry_t        *tty_io;


static void tty_hide_input( void ) {
    int i;

    if( !tty_hidden ) {
        for( i = 0; i <= tty_prompt.inputLine.cursorPos; i++ ) {
            write( 1, "\b \b", 3 );
        }
    }
    tty_hidden++;
}

static void tty_show_input( void ) {
    if( !tty_hidden ) {
        return;
    }

    tty_hidden--;
    if( !tty_hidden ) {
        write( 1, "]", 1 );    
        write( 1, tty_prompt.inputLine.text,
            tty_prompt.inputLine.cursorPos );
    }
}

static void tty_init_input( void ) {
    struct termios tty;
#ifdef TIOCGWINSZ
    struct winsize ws;
#endif
    int width;

    if( !isatty( 0 ) || !isatty( 1 ) ) {
        Com_Printf( "stdin/stdout don't both refer to a TTY\n" );
        Cvar_Set( "sys_console", "1" );
        return;
    }
    
    tcgetattr( 0, &tty_orig );
    tty = tty_orig;
    tty.c_lflag &= ~( ECHO | ICANON | INPCK | ISTRIP );
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    tcsetattr( 0, TCSADRAIN, &tty );

    // determine terminal width
    width = 80;
#ifdef TIOCGWINSZ
    if( ioctl( 0, TIOCGWINSZ, &ws ) == 0 ) {
        if( ws.ws_col ) {
            width = ws.ws_col;
        }
    }
#endif
    tty_prompt.widthInChars = width;
    tty_prompt.printf = Sys_Printf;
    tty_enabled = qtrue;

    // figure out input line width
    width--;
    if( width > MAX_FIELD_TEXT - 1 ) {
        width = MAX_FIELD_TEXT - 1;
    }
    IF_Init( &tty_prompt.inputLine, width, width );

    // display command prompt
    write( 1, "]", 1 );    
}

static void tty_shutdown_input( void ) {
    if( tty_io ) {
        IO_Remove( 0 );
        tty_io = NULL;
    }
    if( tty_enabled ) {
        tty_hide_input();
        tcsetattr( 0, TCSADRAIN, &tty_orig );
        tty_enabled = qfalse;
    }
}

void Sys_SetConsoleColor( color_index_t color ) {
    static const char color_to_ansi[8] =
        { '0', '1', '2', '3', '4', '6', '5', '7' };
    char buf[5];
    size_t len;

    if( !sys_console || !sys_console->integer ) {
        return;
    }

    if( !tty_enabled ) {
        return;
    }

    buf[0] = '\033';
    buf[1] = '[';
    switch( color ) {
    case COLOR_NONE:
        buf[2] = '0';
        buf[3] = 'm';
        len = 4;
        break;
    case COLOR_ALT:
        buf[2] = '3';
        buf[3] = '2';
        buf[4] = 'm';
        len = 5;
        break;
    default:
        buf[2] = '3';
        buf[3] = color_to_ansi[color];
        buf[4] = 'm';
        len = 5;
        break;
    }

    write( 1, buf, len );
}

static void tty_write_output( const char *text ) {
    char    buf[MAXPRINTMSG];
    size_t  len;

    for( len = 0; len < MAXPRINTMSG; len++ ) {
        int c = *text++;
        if( !c ) {
            break;
        }
        buf[len] = Q_charascii( c );
    }

    write( 1, buf, len );
}

/*
=================
Sys_ConsoleOutput
=================
*/
void Sys_ConsoleOutput( const char *text ) {
    if( !sys_console || !sys_console->integer ) {
        return;
    }

    if( !tty_enabled ) {
        tty_write_output( text );
    } else {
        tty_hide_input();
        tty_write_output( text );
        tty_show_input();
    }
}

void Sys_SetConsoleTitle( const char *title ) {
    char buffer[MAX_STRING_CHARS];
    size_t len;

    if( !sys_console || !sys_console->integer ) {
        return;
    }

    if( !tty_enabled ) {
        return;
    }

    len = Q_snprintf( buffer, sizeof( buffer ), "\033]0;%s\007", title );
    if( len < sizeof( buffer ) ) {
        write( 1, buffer, len );    
    }
}

static void tty_parse_input( const char *text ) {
    inputField_t *f;
    char *s;
    int i, key;

    f = &tty_prompt.inputLine;
    while( *text ) {
        key = *text++;

        if( key == tty_orig.c_cc[VERASE] || key == 127 || key == 8 ) {
            if( f->cursorPos ) {
                f->text[--f->cursorPos] = 0;
                write( 1, "\b \b", 3 );    
            }
            continue;
        }

        if( key == tty_orig.c_cc[VKILL] ) {
            for( i = 0; i < f->cursorPos; i++ ) {
                write( 1, "\b \b", 3 );    
            }
            f->cursorPos = 0;
            continue;
        }

        if( key >= 32 ) {
            if( f->cursorPos == f->maxChars - 1 ) {
                write( 1, va( "\b \b%c", key ), 4 );    
                f->text[f->cursorPos+0] = key;
                f->text[f->cursorPos+1] = 0;
            } else {
                write( 1, va( "%c", key ), 1 );    
                f->text[f->cursorPos+0] = key;
                f->text[f->cursorPos+1] = 0;
                f->cursorPos++;
            }
            continue;
        }
    
        if( key == '\n' ) {
            tty_hide_input();
            s = Prompt_Action( &tty_prompt );
            if( s ) {
                if( *s == '\\' || *s == '/' ) {
                    s++;
                }
                Sys_Printf( "]%s\n", s );
                Cbuf_AddText( &cmd_buffer, s );
            } else {
                write( 1, "]\n", 2 );    
            }
            tty_show_input();
            continue;
        }

        if( key == '\t' ) {
            tty_hide_input();
            Prompt_CompleteCommand( &tty_prompt, qfalse );
            f->cursorPos = strlen( f->text ); // FIXME
            tty_show_input();
            continue;
        }

        if( *text ) {
            key = *text++;
            if( key == '[' || key == 'O' ) {
                if( *text ) {
                    key = *text++;
                    switch( key ) {
                    case 'A':
                        tty_hide_input();
                        Prompt_HistoryUp( &tty_prompt );
                        tty_show_input();
                        break;
                    case 'B':
                        tty_hide_input();
                        Prompt_HistoryDown( &tty_prompt );
                        tty_show_input();
                        break;
#if 0
                    case 'C':
                        if( f->text[f->cursorPos] ) {
                            Sys_ConsoleWrite( "\033[C", 3 );
                            f->cursorPos++;
                        }
                        break;
                    case 'D':
                        if( f->cursorPos ) {
                            Sys_ConsoleWrite( "\033[D", 3 );
                            f->cursorPos--;
                        }
                        break;
#endif
                    }
                }
            }
        }
    }
}

void Sys_RunConsole( void ) {
    char text[MAX_STRING_CHARS];
    int ret;

    if( !sys_console || !sys_console->integer ) {
        return;
    }

    if( !tty_io || !tty_io->canread ) {
        return;
    }

    ret = read( 0, text, sizeof( text ) - 1 );
    if( !ret ) {
        Com_DPrintf( "Read EOF from stdin.\n" );
        tty_shutdown_input();
        Cvar_Set( "sys_console", "0" );
        return;
    }
    if( ret < 0 ) {
        if( errno == EINTR ) {
            return;
        }
        Com_Error( ERR_FATAL, "%s: read() failed: %s",
            __func__, strerror( errno ) );
    }
    text[ret] = 0;

    if( !tty_enabled ) {    
        Cbuf_AddText( &cmd_buffer, text );
        return;
    }

    tty_parse_input( text );
}

#endif // USE_SYSCON

/*
===============================================================================

HUNK

===============================================================================
*/

void Hunk_Begin( mempool_t *pool, size_t maxsize ) {
    void *buf;

    // reserve a huge chunk of memory, but don't commit any yet
    pool->maxsize = ( maxsize + 4095 ) & ~4095;
    pool->cursize = 0;
    buf = mmap( NULL, pool->maxsize, PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANON, -1, 0 );
    if( buf == NULL || buf == ( void * )-1 ) {
        Com_Error( ERR_FATAL, "%s: unable to reserve %"PRIz" bytes: %s",
            __func__, pool->maxsize, strerror( errno ) );
    }
    pool->base = buf;
    pool->mapped = pool->maxsize;
}

void *Hunk_Alloc( mempool_t *pool, size_t size ) {
    void *buf;

    // round to cacheline
    size = ( size + 63 ) & ~63;
    if( pool->cursize + size > pool->maxsize ) {
        Com_Error( ERR_FATAL, "%s: unable to allocate %"PRIz" bytes out of %"PRIz,
            __func__, size, pool->maxsize );
    }
    buf = ( byte * )pool->base + pool->cursize;
    pool->cursize += size;
    return buf;
}

void Hunk_End( mempool_t *pool ) {
    size_t newsize = ( pool->cursize + 4095 ) & ~4095;

    if( newsize > pool->maxsize ) {
        Com_Error( ERR_FATAL, "%s: newsize > maxsize", __func__ );
    }

    if( newsize < pool->maxsize ) {
#ifdef _GNU_SOURCE
        void *buf = mremap( pool->base, pool->maxsize, newsize, 0 );
#else
        void *unmap_base = ( byte * )pool->base + newsize;
        size_t unmap_len = pool->maxsize - newsize;
        void *buf = munmap( unmap_base, unmap_len ) + pool->base;
#endif
        if( buf != pool->base ) {
            Com_Error( ERR_FATAL, "%s: could not remap virtual block: %s",
                __func__, strerror( errno ) );
        }
    }
    pool->mapped = newsize;
}

void Hunk_Free( mempool_t *pool ) {
    if( pool->base ) {
        if( munmap( pool->base, pool->mapped ) ) {
            Com_Error( ERR_FATAL, "%s: munmap failed: %s",
                __func__, strerror( errno ) );
        }
    }
    memset( pool, 0, sizeof( *pool ) );
}

/*
===============================================================================

GENERAL ROUTINES

===============================================================================
*/

void Sys_DebugBreak( void ) {
    raise( SIGTRAP );
}

unsigned Sys_Milliseconds( void ) {
    struct timeval tp;
    unsigned time;
    
    gettimeofday( &tp, NULL );
    time = tp.tv_sec * 1000 + tp.tv_usec / 1000;
    return time;
}

/*
================
Sys_GetPathInfo
================
*/
qboolean Sys_GetPathInfo( const char *path, file_info_t *info ) {
    struct stat st;

    if( stat( path, &st ) == -1 ) {
        return qfalse;
    }

    if( !S_ISREG( st.st_mode ) ) {
        return qfalse;
    }

    if( info ) {
        info->size = st.st_size;
        info->ctime = st.st_ctime;
        info->mtime = st.st_mtime;
    }

    return qtrue;
}

qboolean Sys_GetFileInfo( FILE *fp, file_info_t *info ) {
    struct stat st;

    if( fstat( fileno( fp ), &st ) == -1 ) {
        return qfalse;
    }

    if( !S_ISREG( st.st_mode ) ) {
        return qfalse;
    }

    if( info ) {
        info->size = st.st_size;
        info->ctime = st.st_ctime;
        info->mtime = st.st_mtime;
    }

    return qtrue;
}

/*
=================
Sys_Quit

This function never returns.
=================
*/
void Sys_Quit( void ) {
#if USE_SYSCON
    tty_shutdown_input();
#endif
    exit( 0 );
}

/*
=================
Sys_GetCurrentDirectory
=================
*/
char *Sys_GetCurrentDirectory( void ) {
    static char curpath[MAX_OSPATH];

    getcwd( curpath, sizeof( curpath ) );

    return curpath;
}

void Sys_AddDefaultConfig( void ) {
    FILE *fp;
    struct stat st;
    size_t len, r;

    fp = fopen( SYS_SITECFG_NAME, "r" );
    if( !fp ) {
        return;
    }

    if( fstat( fileno( fp ), &st ) == 0 ) {
        Com_Printf( "Execing " SYS_SITECFG_NAME "\n" );

        len = st.st_size;
        if( len >= cmd_buffer.maxsize ) {
            len = cmd_buffer.maxsize - 1;
        }

        r = fread( cmd_buffer.text, 1, len, fp );
        cmd_buffer.text[r] = 0;

        cmd_buffer.cursize = COM_Compress( cmd_buffer.text );
    }

    fclose( fp );
}

void Sys_Sleep( int msec ) {
    struct timespec req;

    req.tv_sec = msec / 1000;
    req.tv_nsec = ( msec % 1000 ) * 1000000;
    nanosleep( &req, NULL );
}

#if USE_AC_CLIENT
qboolean Sys_GetAntiCheatAPI( void ) {
    Sys_Sleep( 1500 );
    return qfalse;
}
#endif

void Sys_FixFPCW( void ) {
#ifdef __i386__
    uint16_t cw;

    __asm__ __volatile__( "fnstcw %0" : "=m" (cw) );

    Com_DPrintf( "FPU control word: %x\n", cw );

    if( cw & 0x300 ) {
        Com_DPrintf( "Setting FPU to single precision mode\n" );
        cw &= ~0x300;
    }
    if( cw & 0xC00 ) {
        Com_DPrintf( "Setting FPU to round to nearest mode\n" );
        cw &= ~0xC00;
    }

    __asm__ __volatile__( "fldcw %0" : : "m" (cw) );
#endif
}

static void hup_handler( int signum ) {
    Com_FlushLogs();
}

static void term_handler( int signum ) {
#ifdef _GNU_SOURCE
    Com_Printf( "%s\n", strsignal( signum ) );
#else
    Com_Printf( "Received signal %d, exiting\n", signum );
#endif
    Com_Quit( NULL, KILL_DROP );
}

static void kill_handler( int signum ) {
#if USE_SYSCON
    tty_shutdown_input();
#endif

#if USE_CLIENT && USE_REF
    VID_FatalShutdown();
#endif

#ifdef _GNU_SOURCE
    fprintf( stderr, "%s\n", strsignal( signum ) );
#else
    fprintf( stderr, "Received signal %d, aborting\n", signum );
#endif

    exit( 1 );
}

/*
=================
Sys_Init
=================
*/
void Sys_Init( void ) {
    char    *homedir;

    signal( SIGTERM, term_handler );
    signal( SIGINT, term_handler );
    signal( SIGTTIN, SIG_IGN );
    signal( SIGTTOU, SIG_IGN );
    signal( SIGUSR1, hup_handler );

    // basedir <path>
    // allows the game to run from outside the data tree
    sys_basedir = Cvar_Get( "basedir", DATADIR, CVAR_NOSET );
    
    // homedir <path>
    // specifies per-user writable directory for demos, screenshots, etc
    if( HOMEDIR[0] == '~' ) {
        char *s = getenv( "HOME" );
        if( s && *s ) {
            homedir = va( "%s%s", s, HOMEDIR + 1 );
        } else {
            homedir = "";
        }
    } else {
        homedir = HOMEDIR;
    }
    sys_homedir = Cvar_Get( "homedir", homedir, CVAR_NOSET );
    sys_libdir = Cvar_Get( "libdir", LIBDIR, CVAR_NOSET );

#if USE_SYSCON
    // we want TTY support enabled if started from terminal,
    // but don't want any output by default if launched without one
    // (from X session for example)
    sys_console = Cvar_Get( "sys_console", isatty( 0 ) &&
        isatty( 1 ) ? "2" : "0", CVAR_NOSET );

    if( sys_console->integer > 0 ) {
        // change stdin to non-blocking and stdout to blocking
        fcntl( 0, F_SETFL, fcntl( 0, F_GETFL, 0 ) | FNDELAY );
        fcntl( 1, F_SETFL, fcntl( 1, F_GETFL, 0 ) & ~FNDELAY );

        // add stdin to the list of descriptors to wait on
        tty_io = IO_Add( 0 );
        tty_io->wantread = qtrue;

        // init optional TTY support
        if( sys_console->integer > 1 ) {
            tty_init_input(); 
        }
        signal( SIGHUP, term_handler );
    } else
#endif
    if( Com_IsDedicated() ) {
        signal( SIGHUP, hup_handler );
    }

    sys_parachute = Cvar_Get( "sys_parachute", "1", CVAR_NOSET );

    if( sys_parachute->integer ) {
        // perform some cleanup when crashing
        signal( SIGSEGV, kill_handler );
        signal( SIGILL, kill_handler );
        signal( SIGFPE, kill_handler );
        signal( SIGTRAP, kill_handler );
    }

    Sys_FixFPCW();
}

#if USE_SYSCON
/*
================
Sys_Printf
================
*/
void Sys_Printf( const char *fmt, ... ) {
    va_list     argptr;
    char        msg[MAXPRINTMSG];

    va_start( argptr, fmt );
    Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
    va_end( argptr );

    Sys_ConsoleOutput( msg );
}
#endif

/*
=================
Sys_Error
=================
*/
void Sys_Error( const char *error, ... ) {
    va_list     argptr;
    char        text[MAXPRINTMSG];

#if USE_SYSCON
    tty_shutdown_input();
#endif

#if USE_CLIENT && USE_REF
    VID_FatalShutdown();
#endif

    va_start( argptr, error );
    Q_vsnprintf( text, sizeof( text ), error, argptr );
    va_end( argptr );
    
    fprintf( stderr, "********************\n"
                     "FATAL: %s\n"
                     "********************\n", text );
    exit( 1 );
}

/*
========================================================================

DLL LOADING

========================================================================
*/

/*
=================
Sys_FreeLibrary
=================
*/
void Sys_FreeLibrary( void *handle ) {
    if( handle && dlclose( handle ) ) {
        Com_Error( ERR_FATAL, "dlclose failed on %p: %s", handle, dlerror() );
    }
}

/*
=================
Sys_LoadLibrary
=================
*/
void *Sys_LoadLibrary( const char *path, const char *sym, void **handle ) {
    void    *module, *entry;

    *handle = NULL;

    module = dlopen( path, RTLD_LAZY );
    if( !module ) {
        Com_DPrintf( "%s failed: %s\n", __func__, dlerror() );
        return NULL;
    }

    if( sym ) {
        entry = dlsym( module, sym );
        if( !entry ) {
            Com_DPrintf( "%s failed: %s\n", __func__, dlerror() );
            dlclose( module );
            return NULL;
        }
    } else {
        entry = NULL;
    }

    Com_DPrintf( "%s succeeded: %s\n", __func__, path );

    *handle = module;

    return entry;
}

void *Sys_GetProcAddress( void *handle, const char *sym ) {
    return dlsym( handle, sym );
}

/*
===============================================================================

MISC

===============================================================================
*/

/*
=================
Sys_ListFilteredFiles
=================
*/
static void Sys_ListFilteredFiles(  void        **listedFiles,
                                    int         *count,
                                    const char  *path,
                                    const char  *filter,
                                    int         flags,
                                    size_t      baselen,
                                    int         depth )
{
    struct dirent *findInfo;
    DIR        *findHandle;
    struct  stat st;
    char    findPath[MAX_OSPATH];
    char    *name;
    size_t  len;

    if( depth >= 32 ) {
        return;
    }

    if( *count >= MAX_LISTED_FILES ) {
        return;
    }

    if( ( findHandle = opendir( path ) ) == NULL ) {
        return;
    }

    while( ( findInfo = readdir( findHandle ) ) != NULL ) {
        if( !strcmp( findInfo->d_name, "." ) ) {
            continue;
        }
        if( !strcmp( findInfo->d_name, ".." ) ) {
            continue;
        }
        len = Q_concat( findPath, sizeof( findPath ),
            path, "/", findInfo->d_name, NULL );
        if( len >= sizeof( findPath ) ) {
            continue;
        }

        if( stat( findPath, &st ) == -1 ) {
            continue;
        }

        if( st.st_mode & S_IFDIR ) {
            Sys_ListFilteredFiles( listedFiles, count, findPath,
                filter, flags, baselen, depth + 1 );
        }

        if( ( flags & FS_SEARCHDIRS_MASK ) == FS_SEARCHDIRS_ONLY ) {
            if( !( st.st_mode & S_IFDIR ) ) {
                continue;
            }
        } else if( ( flags & FS_SEARCHDIRS_MASK ) == FS_SEARCHDIRS_NO ) {
            if( st.st_mode & S_IFDIR ) {
                continue;
            }
        }

        if( !FS_WildCmp( filter, findPath + baselen ) ) {
            continue;
        }
        if( flags & FS_SEARCH_SAVEPATH ) {
            name = findPath + baselen;
        } else {
            name = findInfo->d_name;
        }

        if( flags & FS_SEARCH_EXTRAINFO ) {
            listedFiles[( *count )++] = FS_CopyInfo( name,
                st.st_size, st.st_ctime, st.st_mtime );
        } else {
            listedFiles[( *count )++] = FS_CopyString( name );
        }
        if( *count >= MAX_LISTED_FILES ) {
            break;
        }

    }

    closedir( findHandle );
}


/*
=================
Sys_ListFiles
=================
*/
void **Sys_ListFiles(   const char  *path,
                        const char  *extension,
                        int         flags,
                        size_t      baselen,
                        int         *numFiles )
{
    struct dirent *findInfo;
    DIR     *findHandle;
    struct stat st;
    char    findPath[MAX_OSPATH];
    void    *listedFiles[MAX_LISTED_FILES];
    int     count = 0;
    char    *s;
    size_t  len;

    if( numFiles ) {
        *numFiles = 0;
    }

    if( flags & FS_SEARCH_BYFILTER ) {
        Sys_ListFilteredFiles( listedFiles, &count, path,
            extension, flags, baselen, 0 );
    } else {
        if( ( findHandle = opendir( path ) ) == NULL ) {
            return NULL;
        }

        while( ( findInfo = readdir( findHandle ) ) != NULL ) {
            if( !strcmp( findInfo->d_name, "." ) ) {
                continue;
            }
            if( !strcmp( findInfo->d_name, ".." ) ) {
                continue;
            }

            len = Q_concat( findPath, sizeof( findPath ),
                path, "/", findInfo->d_name, NULL );
            if( len >= sizeof( findPath ) ) {
                continue;
            }

            if( stat( findPath, &st ) == -1 ) {
                continue;
            }
            if( ( flags & FS_SEARCHDIRS_MASK ) == FS_SEARCHDIRS_ONLY ) {
                if( !( st.st_mode & S_IFDIR ) ) {
                    continue;
                }
            } else if( ( flags & FS_SEARCHDIRS_MASK ) == FS_SEARCHDIRS_NO ) {
                if( st.st_mode & S_IFDIR ) {
                    continue;
                }
            }

            if( extension && !FS_ExtCmp( extension, findInfo->d_name ) ) {
                continue;
            }
            
            if( flags & FS_SEARCH_SAVEPATH ) {
                s = findPath + baselen;
            } else {
                s = findInfo->d_name;
            }
            if( flags & FS_SEARCH_EXTRAINFO ) {
                listedFiles[count++] = FS_CopyInfo( s, st.st_size,
                    st.st_ctime, st.st_mtime );
            } else {
                listedFiles[count++] = FS_CopyString( s );
            }

            if( count >= MAX_LISTED_FILES ) {
                break;
            }
        }

        closedir( findHandle );
    }

    if( !count ) {
        return NULL;
    }

    if( numFiles ) {
        *numFiles = count;
    }

    return FS_CopyList( listedFiles, count );
}

/*
=================
main
=================
*/
int main( int argc, char **argv ) {
    if( argc > 1 ) {
        if( !strcmp( argv[1], "-v" ) || !strcmp( argv[1], "--version" ) ) {
            printf( APPLICATION " " VERSION " " __DATE__ " " BUILDSTRING " "
                    CPUSTRING "\n" );
            return 0;
        }
        if( !strcmp( argv[1], "-h" ) || !strcmp( argv[1], "--help" ) ) {
            printf( "Usage: %s [+command arguments] [...]\n", argv[0] );
            return 0;
        }
    }

    if( !getuid() || !geteuid() ) {
        printf(  "You can not run " APPLICATION " as superuser "
                 "for security reasons!\n" );
        return 1;
    }

    Qcommon_Init( argc, argv );
    while( 1 ) {
        Qcommon_Frame();
    }

    return 1; // never gets here
}

