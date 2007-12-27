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

#ifdef USE_SDL
#include <SDL.h>
#include <SDL_syswm.h>
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

static byte             sys_output_buffer[MAX_MSGLEN];
static fifo_t           sys_output = {
    .data = sys_output_buffer,
    .size = sizeof( sys_output_buffer )
};

void Sys_Printf( const char *fmt, ... );

/*
===============================================================================

TERMINAL SUPPORT

===============================================================================
*/

static void Sys_HideInput( void ) {
	int i;

    if( !tty_enabled ) {
        return;
    }

	if( !tty_hidden ) {
		for( i = 0; i <= tty_prompt.inputLine.cursorPos; i++ ) {
            FIFO_Write( &sys_output, "\b \b", 3 );	
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
        FIFO_Write( &sys_output, "]", 1 );	
        FIFO_Write( &sys_output, tty_prompt.inputLine.text,
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
	char    *data, *maxp, *p;
    int     length;
    int     color = 0;

	if( !tty_enabled ) {
        data = p = FIFO_Reserve( &sys_output, &length );
        maxp = p + length;
        while( *string ) {
            if( Q_IsColorString( string ) ) {
                string += 2;
                continue;
            }
            if( p + 1 > maxp ) {
                break;
            }
            *p++ = *string++ & 127;
        }

        FIFO_Commit( &sys_output, p - data );
		return;
	}

    Sys_HideInput();
    
    data = p = FIFO_Reserve( &sys_output, &length );
    maxp = p + length;
    while( *string ) {
        if( Q_IsColorString( string ) ) {
            color = string[1];
            string += 2;
            if( p + 5 > maxp ) {
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
        if( p + 1 > maxp ) {
            break;
        }
        *p++ = *string++ & 127;
    }

    if( color ) {
        if( p + 4 > maxp ) {
            p = maxp - 4;
        }
        p[0] = '\033';
        p[1] = '[';
        p[2] = '0';
        p[3] = 'm';
        p += 4;
    }

    FIFO_Commit( &sys_output, p - data );
	
    Sys_ShowInput();
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
                FIFO_Write( &sys_output, "\b \b", 3 );	
            }
            continue;
        }

        if( key == tty_orig.c_cc[VKILL] ) {
            for( i = 0; i < f->cursorPos; i++ ) {
                FIFO_Write( &sys_output, "\b \b", 3 );	
            }
            f->cursorPos = 0;
            continue;
        }

        if( key >= 32 ) {
            if( f->cursorPos < sizeof( f->text ) - 1 ) {
                FIFO_Write( &sys_output, &key, 1 );	
                f->text[f->cursorPos] = key;
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
                FIFO_Write( &sys_output, "]\n", 2 );	
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
                            FIFO_Write( &sys_output, "\033[C", 3 );
                            f->cursorPos++;
                        }
                        break;
                    case 'D':
                        if( f->cursorPos ) {
                            FIFO_Write( &sys_output, "\033[D", 3 );
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
	fd_set	rfd, wfd;
    struct timeval tv;
    byte *data;
    int length;
    char text[MAX_STRING_CHARS];
    int ret;

    if( !sys_stdio->integer ) {
        return;
    }

    FD_ZERO( &rfd );
    FD_ZERO( &wfd );
    FD_SET( 0, &rfd ); // stdin
    FD_SET( 1, &wfd ); // stdout
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if( select( 2, &rfd, &wfd, NULL, &tv ) == -1 ) {
        Com_Error( ERR_FATAL, "%s: select() failed", __func__ );
    }

    if( FD_ISSET( 0, &rfd ) ) {
        ret = read( 0, text, sizeof( text ) );
        if( !ret ) {
            Com_DPrintf( "Read EOF from stdin.\n" );
            Sys_ShutdownTTY();
            Cvar_Set( "sys_stdio", "0" );
            return;
        }
        if( ret < 0 ) {
            Com_Error( ERR_FATAL, "%s: %d bytes read", __func__, ret );
        }
        text[ret] = 0;

        Sys_ParseInput( text );
    }

    if( FD_ISSET( 1, &wfd ) ) {
        data = FIFO_Peek( &sys_output, &length );
        if( length ) {
            ret = write( 1, data, length );
            if( ret <= 0 ) {
                Com_Error( ERR_FATAL, "%s: %d bytes written", __func__, ret );
            }
            FIFO_Decommit( &sys_output, ret );
        }
    }
}

/*
===============================================================================

HUNK

===============================================================================
*/

void Hunk_Begin( mempool_t *pool, int maxsize ) {
    byte *buf;

	// reserve a huge chunk of memory, but don't commit any yet
	pool->maxsize = ( maxsize + 4095 ) & ~4095;
	pool->cursize = 0;
    buf = mmap( NULL, pool->maxsize, PROT_READ|PROT_WRITE,
		MAP_PRIVATE|MAP_ANON, -1, 0 );
	if( buf == NULL || buf == ( byte * )-1 ) {
		Com_Error( ERR_FATAL, "%s: unable to virtual allocate %d bytes",
            __func__, pool->maxsize );
    }
    pool->base = buf;
    pool->mapped = pool->maxsize;
}

void *Hunk_Alloc( mempool_t *pool, int size ) {
	byte *buf;

	// round to cacheline
	size = ( size + 31 ) & ~31;
	if( pool->cursize + size > pool->maxsize ) {
		Com_Error( ERR_FATAL, "%s: unable to allocate %d bytes out of %d",
            __func__, size, pool->maxsize );
    }
	buf = pool->base + pool->cursize;
	pool->cursize += size;
	return buf;
}

void Hunk_End( mempool_t *pool ) {
	byte *n;

#ifndef __linux__
	size_t old_size = pool->maxsize;
	size_t new_size = pool->cursize;
	void * unmap_base;
	size_t unmap_len;

	new_size = round_page(new_size);
	old_size = round_page(old_size);
	if (new_size > old_size) {
		Com_Error( ERR_FATAL, "Hunk_End: new_size > old_size" );
    }
	if (new_size < old_size) {
		unmap_base = (caddr_t)(pool->base + new_size);
		unmap_len = old_size - new_size;
		n = munmap(unmap_base, unmap_len) + pool->base;
	}
#else
	n = mremap( pool->base, pool->maxsize, pool->cursize, 0 );
#endif
	if( n != pool->base ) {
		Com_Error( ERR_FATAL, "%s: could not remap virtual block: %s",
            __func__, strerror( errno ) );
    }
    pool->mapped = pool->cursize;
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
    raise( SIGTERM );
}

/*
================
Sys_Milliseconds
================
*/
int curtime;

int Sys_Milliseconds( void ) {
	struct timeval tp;
	static int		secbase;

	gettimeofday( &tp, NULL );

	if( !secbase ) {
		secbase = tp.tv_sec;
		return tp.tv_usec / 1000;
	}

	curtime = ( tp.tv_sec - secbase ) * 1000 + tp.tv_usec / 1000;

	return curtime;
}

uint32 Sys_Realtime( void ) {
	struct timeval tp;
    uint32 time;
    
	gettimeofday( &tp, NULL );
	time = tp.tv_sec * 1000 + tp.tv_usec / 1000;
	return time;
}

/*
================
Sys_Mkdir
================
*/
void Sys_Mkdir( const char *path ) {
    mkdir( path, 0777 );
}

qboolean Sys_RemoveFile( const char *path ) {
	if( remove( path ) ) {
		return qfalse;
	}
	return qtrue;
}

qboolean Sys_RenameFile( const char *from, const char *to ) {
	if( rename( from, to ) ) {
		return qfalse;
	}
	return qtrue;
}

/*
================
Sys_GetFileInfo
================
*/
qboolean Sys_GetFileInfo( const char *path, fsFileInfo_t *info ) {
	struct stat		st;

	if( stat( path, &st ) == -1 ) {
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
#if USE_SDL
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

    if( stat( DEFCFG, &st ) == -1 ) {
        return;
    }

    fp = fopen( DEFCFG, "r" );
    if( !fp ) {
        return;
    }

    Com_Printf( "Execing " DEFCFG "\n" );
    text = Cbuf_Alloc( &cmd_buffer, st.st_size );
    if( text ) {
        fread( text, st.st_size, 1, fp );
    }

    fclose( fp );
}

void Sys_Sleep( int msec ) {
    struct timespec req = { 0, msec * 1000000 };
    nanosleep( &req, NULL );
}

void Sys_Setenv( const char *name, const char *value ) {
    setenv( name, value, 1 );
}

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
    uint16 cw;

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
Sys_Kill
=================
*/
static void Sys_Kill( int signum ) {
	signal( SIGTERM, SIG_DFL );
	signal( SIGINT, SIG_DFL );
	signal( SIGSEGV, SIG_DFL );
	
	Com_Printf( "Received signal %d, exiting\n", signum );
	Com_Quit();
}

/*
=================
Sys_Segv
=================
*/
static void Sys_Segv( int signum ) {
	signal( SIGTERM, SIG_DFL );
	signal( SIGINT, SIG_DFL );
	signal( SIGSEGV, SIG_DFL );

    Sys_ShutdownTTY();

#if USE_SDL
    SDL_ShowCursor( SDL_ENABLE );
    SDL_WM_GrabInput( SDL_GRAB_OFF );
	SDL_Quit();
#endif

	fprintf( stderr, "Received signal SIGSEGV, segmentation fault\n" );

    exit( 1 );
}

/*
=================
Sys_Init
=================
*/
void Sys_Init( void ) {
    char    *homedir;

	signal( SIGTERM, Sys_Kill );
	signal( SIGINT, Sys_Kill );
	signal( SIGSEGV, Sys_Segv );
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
        // change stdin and stdout to non-blocking
        fcntl( 0, F_SETFL, fcntl( 0, F_GETFL, 0 ) | FNDELAY );
        fcntl( 1, F_SETFL, fcntl( 1, F_GETFL, 0 ) | FNDELAY );

        // init TTY support
        if( sys_stdio->integer > 1 ) {
            if( isatty( 0 ) == 1 ) {
                Sys_InitTTY(); 
            } else {
                Com_DPrintf( "stdin in not a tty, tty input disabled.\n" );
                Cvar_SetInteger( "sys_stdio", 1 );
            }
        }
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
    SDL_ShowCursor( SDL_ENABLE );
    SDL_WM_GrabInput( SDL_GRAB_OFF );
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
    if( !handle ) {
        return;
    }
    if( dlclose( handle ) ) {
        Com_Error( ERR_FATAL, "dlclose failed on %p", handle );
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
                                    int         length,
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
                        int         length,
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

