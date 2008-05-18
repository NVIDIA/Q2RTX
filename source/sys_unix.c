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

#ifdef DEDICATED_ONLY
#undef USE_SDL
#endif

#if USE_SDL
#include <SDL.h>
#if USE_X11
#include <SDL_syswm.h>
#endif
#endif

cvar_t	*sys_basedir;
cvar_t  *sys_libdir;
cvar_t  *sys_refdir;
cvar_t  *sys_homedir;
cvar_t  *sys_stdio;

sysAPI_t sys;

static qboolean         tty_enabled;
static struct termios   tty_orig;
static commandPrompt_t	tty_prompt;
static int				tty_hidden;

/*
===============================================================================

TERMINAL SUPPORT

===============================================================================
*/

static void Sys_ConsoleWrite( char *data, int count ) {
    int ret;

    while( count ) {
        ret = write( 1, data, count );
        if( ret <= 0 ) {
            //Com_Error( ERR_FATAL, "%s: %d bytes written: %s",
              //  __func__, ret, strerror( errno ) );
            break;
        }
        count -= ret;
        data += ret;
    }
}

static void Sys_HideInput( void ) {
	int i;

    if( !tty_enabled ) {
        return;
    }

	if( !tty_hidden ) {
		for( i = 0; i <= tty_prompt.inputLine.cursorPos; i++ ) {
            Sys_ConsoleWrite( "\b \b", 3 );
		}
	}
	tty_hidden++;
}

static void Sys_ShowInput( void ) {
    if( !tty_enabled ) {
        return;
    }

	if( !tty_hidden ) {
		Com_EPrintf( "Sys_ShowInput: not hidden\n" );
		return;
	}

	tty_hidden--;
	if( !tty_hidden ) {
        Sys_ConsoleWrite( "]", 1 );	
        Sys_ConsoleWrite( tty_prompt.inputLine.text,
            tty_prompt.inputLine.cursorPos );
	}
}

static void Sys_InitTTY( void ) {
    struct termios tty;
#ifdef TIOCGWINSZ
    struct winsize ws;
#endif
    
    tcgetattr( 0, &tty_orig );
    tty = tty_orig;
    tty.c_lflag &= ~( ECHO | ICANON | INPCK | ISTRIP );
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    tcsetattr( 0, TCSADRAIN, &tty );
    tty_prompt.widthInChars = 80;
#ifdef TIOCGWINSZ
    if( ioctl( 0, TIOCGWINSZ, &ws ) == 0 ) {
        tty_prompt.widthInChars = ws.ws_col;
    }
#endif
    tty_prompt.printf = Sys_Printf;
    tty_enabled = qtrue;

    Sys_ConsoleWrite( " ", 1 );	
}

static void Sys_ShutdownTTY( void ) {
    if( tty_enabled ) {
        Sys_HideInput();
        Sys_RunConsole();
        tcsetattr( 0, TCSADRAIN, &tty_orig );
    }
}


static const char color_to_ansi[8] = { '0', '1', '2', '3', '4', '6', '5', '7' };

/*
=================
Sys_ConsoleOutput
=================
*/
void Sys_ConsoleOutput( const char *string ) {
    char    buffer[MAXPRINTMSG*2];
	char    *m, *p;
    int     color = 0;

    p = buffer;
    m = buffer + sizeof( buffer );

	if( !tty_enabled ) {
        while( *string ) {
            if( Q_IsColorString( string ) ) {
                string += 2;
                continue;
            }
            if( p + 1 > m ) {
                break;
            }
            *p++ = *string++ & 127;
        }

        Sys_ConsoleWrite( buffer, p - buffer );
		return;
	}

    Sys_HideInput();
    
    while( *string ) {
        if( Q_IsColorString( string ) ) {
            color = string[1];
            string += 2;
            if( p + 5 > m ) {
                break;
            }
            p[0] = '\033';
            p[1] = '[';
            if( color == COLOR_RESET ) {
                p[2] = '0';
                p[3] = 'm';
                p += 4;
            } else if( color == COLOR_ALT ) {
                p[2] = '3';
                p[3] = '2';
                p[4] = 'm';
                p += 5;
            } else {
                p[2] = '3';
                p[3] = color_to_ansi[ ColorIndex( color ) ];
                p[4] = 'm';
                p += 5;
            }
            continue;
        }
        if( p + 1 > m ) {
            break;
        }
        *p++ = *string++ & 127;
    }

    if( color ) {
        if( p + 4 > m ) {
            p = m - 4;
        }
        p[0] = '\033';
        p[1] = '[';
        p[2] = '0';
        p[3] = 'm';
        p += 4;
    }

    Sys_ConsoleWrite( buffer, p - buffer );
	
    Sys_ShowInput();
}

void Sys_SetConsoleTitle( const char *title ) {
    char buffer[MAX_STRING_CHARS];
    int len;

	if( !tty_enabled ) {
        return;
    }
    len = Com_sprintf( buffer, sizeof( buffer ), "\033]0;%s\007", title );
    Sys_ConsoleWrite( buffer, len );	
}

/*
=================
Sys_ParseInput
=================
*/
static void Sys_ParseInput( const char *text ) {
	inputField_t *f;
	char *s;
    int i, key;

	if( !tty_enabled ) {    
        Cbuf_AddText( text );
        return;
	}

	f = &tty_prompt.inputLine;
    while( *text ) {
        key = *text++;

        if( key == tty_orig.c_cc[VERASE] || key == 127 || key == 8 ) {
            if( f->cursorPos ) {
                f->text[--f->cursorPos] = 0;
                Sys_ConsoleWrite( "\b \b", 3 );	
            }
            continue;
        }

        if( key == tty_orig.c_cc[VKILL] ) {
            for( i = 0; i < f->cursorPos; i++ ) {
                Sys_ConsoleWrite( "\b \b", 3 );	
            }
            f->cursorPos = 0;
            continue;
        }

        if( key >= 32 ) {
            if( f->cursorPos < sizeof( f->text ) - 1 ) {
                char c = key;
                Sys_ConsoleWrite( &c, 1 );	
                f->text[f->cursorPos] = c;
                f->text[++f->cursorPos] = 0;
            }
            continue;
        }
    
        if( key == '\n' ) {
            Sys_HideInput();
            s = Prompt_Action( &tty_prompt );
            if( s ) {
                if( *s == '\\' || *s == '/' ) {
                    s++;
                }
                Sys_Printf( "]%s\n", s );
                Cbuf_AddText( s );
            } else {
                Sys_ConsoleWrite( "]\n", 2 );	
            }
            Sys_ShowInput();
            continue;
        }

        if( key == '\t' ) {
            Sys_HideInput();
            Prompt_CompleteCommand( &tty_prompt, qfalse );
            f->cursorPos = strlen( f->text );
            Sys_ShowInput();
            continue;
        }

        //Com_Printf( "%s\n",Q_FormatString(text));
        if( *text ) {
            key = *text++;
            if( key == '[' || key == 'O' ) {
                if( *text ) {
                    key = *text++;
                    switch( key ) {
                    case 'A':
                        Sys_HideInput();
                        Prompt_HistoryUp( &tty_prompt );
                        Sys_ShowInput();
                        break;
                    case 'B':
                        Sys_HideInput();
                        Prompt_HistoryDown( &tty_prompt );
                        Sys_ShowInput();
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
	fd_set	fd;
    struct timeval tv;
    char text[MAX_STRING_CHARS];
    int ret;

    if( !sys_stdio->integer ) {
        return;
    }

    FD_ZERO( &fd );
    FD_SET( 0, &fd ); // stdin
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if( select( 1, &fd, NULL, NULL, &tv ) == -1 ) {
        Com_Error( ERR_FATAL, "%s: select() failed: %s",
            __func__, strerror( errno ) );
    }

    if( !FD_ISSET( 0, &fd ) ) {
        return;
    }

    ret = read( 0, text, sizeof( text ) - 1 );
    if( !ret ) {
        Com_DPrintf( "Read EOF from stdin.\n" );
        Sys_ShutdownTTY();
        Cvar_Set( "sys_stdio", "0" );
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

    Sys_ParseInput( text );
}

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
		Com_Error( ERR_FATAL, "%s: unable to virtual allocate %"PRIz" bytes",
            __func__, pool->maxsize );
    }
    pool->base = buf;
    pool->mapped = pool->maxsize;
}

void *Hunk_Alloc( mempool_t *pool, size_t size ) {
	void *buf;

	// round to cacheline
	size = ( size + 31 ) & ~31;
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
Sys_Mkdir
================
*/
qboolean Sys_Mkdir( const char *path ) {
    if( mkdir( path, 0777 ) == -1 ) {
        return qfalse;
    }
    return qtrue;
}

qboolean Sys_RemoveFile( const char *path ) {
	if( unlink( path ) == -1 ) {
		return qfalse;
	}
	return qtrue;
}

qboolean Sys_RenameFile( const char *from, const char *to ) {
	if( rename( from, to ) == -1 ) {
		return qfalse;
	}
	return qtrue;
}

/*
================
Sys_GetPathInfo
================
*/
qboolean Sys_GetPathInfo( const char *path, fsFileInfo_t *info ) {
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

qboolean Sys_GetFileInfo( FILE *fp, fsFileInfo_t *info ) {
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
=================
*/
void Sys_Quit( void ) {
    Sys_ShutdownTTY();
	exit( 0 );
}

/*
=================
Sys_GetClipboardData
=================
*/
char *Sys_GetClipboardData( void ) {
#if USE_SDL && USE_X11
	SDL_SysWMinfo	info;
    Display *dpy;
	Window sowner, win;
	Atom type, property;
	unsigned long len, bytes_left;
	unsigned char *data;
	int format, result;
	char *ret;

    if( SDL_WasInit( SDL_INIT_VIDEO ) != SDL_INIT_VIDEO ) {
        return NULL;
    }
	SDL_VERSION( &info.version );
	if( !SDL_GetWMInfo( &info ) ) {
        return NULL;
    }
    if( info.subsystem != SDL_SYSWM_X11 ) {
        return NULL;
    }

    dpy = info.info.x11.display;
    win = info.info.x11.window;
	
	sowner = XGetSelectionOwner( dpy, XA_PRIMARY );
	if( sowner == None ) {
        return NULL;
    }

    property = XInternAtom( dpy, "GETCLIPBOARDDATA_PROP", False );
		                       
    XConvertSelection( dpy, XA_PRIMARY, XA_STRING, property, win, CurrentTime );
		
    XSync( dpy, False );
		
    result = XGetWindowProperty( dpy, win, property, 0, 0, False,
        AnyPropertyType, &type, &format, &len, &bytes_left, &data );
								   
    if( result != Success ) {
        return NULL;
    }

    ret = NULL;
    if( bytes_left ) {
        result = XGetWindowProperty( dpy, win, property, 0, bytes_left, True,
            AnyPropertyType, &type, &format, &len, &bytes_left, &data );
        if( result == Success ) {
            ret = Z_CopyString( ( char * )data );
        }
    }

	XFree( data );

    return ret;
#else
    return NULL;
#endif
}

/*
=================
Sys_SetClipboardData
=================
*/
void Sys_SetClipboardData( const char *data ) {
}

/*
=================
Sys_GetCurrentDirectory
=================
*/
char *Sys_GetCurrentDirectory( void ) {
    static char	curpath[MAX_OSPATH];

	getcwd( curpath, sizeof( curpath ) );

	return curpath;
}

void Sys_AddDefaultConfig( void ) {
    FILE *fp;
    struct stat st;
    char *text;

    fp = fopen( SYS_SITECFG_NAME, "r" );
    if( !fp ) {
        return;
    }

    if( fstat( fileno( fp ), &st ) == 0 ) {
        text = Cbuf_Alloc( &cmd_buffer, st.st_size );
        if( text ) {
            Com_Printf( "Execing " SYS_SITECFG_NAME "\n" );
            fread( text, st.st_size, 1, fp );
        }
    }

    fclose( fp );
}

void Sys_Sleep( int msec ) {
    struct timespec req;

    req.tv_sec = msec / 1000;
    req.tv_nsec = ( msec % 1000 ) * 1000000;
    nanosleep( &req, NULL );
}

void Sys_Setenv( const char *name, const char *value ) {
    setenv( name, value, 1 );
}

#if USE_ANTICHEAT & 1
qboolean Sys_GetAntiCheatAPI( void ) {
    Sys_Sleep( 1500 );
    return qfalse;
}
#endif

/*
================
Sys_FillAPI
================
*/
void Sys_FillAPI( sysAPI_t *api ) {
	api->Milliseconds = Sys_Milliseconds;
	api->GetClipboardData = Sys_GetClipboardData;
	api->SetClipboardData = Sys_SetClipboardData;
    api->HunkBegin = Hunk_Begin;
    api->HunkAlloc = Hunk_Alloc;
    api->HunkEnd = Hunk_End;
    api->HunkFree = Hunk_Free;
}

void Sys_FixFPCW( void ) {
#ifdef __i386__
    uint16_t cw;

    __asm__ __volatile__( "fnstcw %0" : "=m" (cw) );

    Com_DPrintf( "FPU control word: %x\n", cw );

    if( cw & 0x300 ) {
        Com_Printf( "Setting FPU to single precision mode\n" );
        cw &= ~0x300;
    }
    if( cw & 0xC00 ) {
        Com_Printf( "Setting FPU to round to nearest mode\n" );
        cw &= ~0xC00;
    }

    __asm__ __volatile__( "fldcw %0" : : "m" (cw) );
#endif
}

/*
=================
Sys_Term
=================
*/
static void Sys_Term( int signum ) {
#ifdef _GNU_SOURCE
	Com_Printf( "%s\n", strsignal( signum ) );
#else
	Com_Printf( "Received signal %d, exiting\n", signum );
#endif
	Com_Quit( NULL );
}

/*
=================
Sys_Kill
=================
*/
static void Sys_Kill( int signum ) {
    Sys_ShutdownTTY();

#if USE_SDL
    SDL_ShowCursor( SDL_ENABLE );
    SDL_WM_GrabInput( SDL_GRAB_OFF );
	SDL_Quit();
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

	signal( SIGTERM, Sys_Term );
	signal( SIGINT, Sys_Term );
	signal( SIGSEGV, Sys_Kill );
	signal( SIGILL, Sys_Kill );
	signal( SIGFPE, Sys_Kill );
	signal( SIGTRAP, Sys_Kill );
	signal( SIGTTIN, SIG_IGN );
	signal( SIGTTOU, SIG_IGN );

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
	sys_refdir = Cvar_Get( "refdir", REFDIR, CVAR_NOSET );

    sys_stdio = Cvar_Get( "sys_stdio", "2", CVAR_NOSET );

    if( sys_stdio->integer ) {
        // change stdin to non-blocking and stdout to blocking
        fcntl( 0, F_SETFL, fcntl( 0, F_GETFL, 0 ) | FNDELAY );
        fcntl( 1, F_SETFL, fcntl( 1, F_GETFL, 0 ) & ~FNDELAY );

        // init TTY support
        if( sys_stdio->integer > 1 ) {
            if( isatty( 0 ) == 1 ) {
                Sys_InitTTY(); 
            } else {
                Com_DPrintf( "stdin in not a tty, tty input disabled.\n" );
                Cvar_Set( "sys_stdio", "1" );
            }
        }
	    signal( SIGHUP, Sys_Term );
    } else {
	    signal( SIGHUP, SIG_IGN );
    }

    Sys_FixFPCW();

	Sys_FillAPI( &sys );
}

/*
================
Sys_Printf
================
*/
void Sys_Printf( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

    Sys_ConsoleOutput( msg );
}

/*
=================
Sys_Error
=================
*/
void Sys_Error( const char *error, ... ) {
    va_list     argptr;
    char        text[MAXPRINTMSG];

    Sys_ShutdownTTY();

#if USE_SDL
    SDL_WM_GrabInput( SDL_GRAB_OFF );
    SDL_ShowCursor( SDL_ENABLE );
	SDL_Quit();
#endif

    va_start( argptr, error );
    Q_vsnprintf( text, sizeof( text ), error, argptr );
    va_end( argptr );
    
    fprintf( stderr, "********************\n"
                     "FATAL: %s\n"
                     "********************\n", text );
    exit( 1 );
}


/*void floating_point_exception_handler( int whatever ) {
	signal( SIGFPE, floating_point_exception_handler );
}*/


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
	void	*module, *entry;

	*handle = NULL;

	module = dlopen( path, RTLD_NOW );
	if( !module ) {
		Com_DPrintf( "%s failed: %s\n", __func__, dlerror() );
		return NULL;
	}

	entry = dlsym( module, sym );
	if( !entry ) {
		Com_DPrintf( "%s failed: %s\n", __func__, dlerror() );
		dlclose( module );
		return NULL;
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
                                    size_t      length,
                                    int         depth )
{
	struct dirent *findInfo;
	DIR		*findHandle;
    struct  stat st;
	char	findPath[MAX_OSPATH];
	char	*name;

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
        Q_concat( findPath, sizeof( findPath ),
            path, "/", findInfo->d_name, NULL );

        if( stat( findPath, &st ) == -1 ) {
            continue;
        }

		if( st.st_mode & S_IFDIR ) {
			Sys_ListFilteredFiles( listedFiles, count, findPath,
                filter, flags, length, depth + 1 );
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

		if( !FS_WildCmp( filter, findPath + length ) ) {
			continue;
		}
        if( flags & FS_SEARCH_SAVEPATH ) {
    		name = findPath + length;
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
                        size_t      length,
                        int         *numFiles )
{
	struct dirent *findInfo;
	DIR		*findHandle;
	struct stat st;
	char	findPath[MAX_OSPATH];
	void	*listedFiles[MAX_LISTED_FILES];
	int		count = 0;
    char    *s;

	if( numFiles ) {
		*numFiles = 0;
	}

	if( flags & FS_SEARCH_BYFILTER ) {
		Sys_ListFilteredFiles( listedFiles, &count, path,
            extension, flags, length, 0 );
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

			Q_concat( findPath, sizeof( findPath ),
                path, "/", findInfo->d_name, NULL );

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
				s = findPath + length;
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
                 "for security reasons!" );
        return 1;
	}
		
    Qcommon_Init( argc, argv );
    while( 1 ) {
        Qcommon_Frame();
    }

    return 1; // never gets here
}

