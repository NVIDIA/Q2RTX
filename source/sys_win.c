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

#include "win_local.h"
#include "q_field.h"
#include "q_list.h"
#include "prompt.h"
#include <mmsystem.h>
#if USE_WINSVC
#include <winsvc.h>
#endif
#include <float.h>

HINSTANCE   hGlobalInstance;

qboolean    iswinnt;

cvar_t  *sys_basedir;
cvar_t  *sys_libdir;
cvar_t  *sys_homedir;

static char currentDirectory[MAX_OSPATH];

#if USE_WINSVC
static SERVICE_STATUS_HANDLE    statusHandle;
#endif

typedef enum {
    SE_NOT,
    SE_YES,
    SE_FULL
} should_exit_t;

static volatile should_exit_t   shouldExit;
static volatile qboolean        errorEntered;

/*
===============================================================================

CONSOLE I/O

===============================================================================
*/

#if USE_SYSCON

#define MAX_CONSOLE_INPUT_EVENTS    16

static HANDLE   hinput = INVALID_HANDLE_VALUE;
static HANDLE   houtput = INVALID_HANDLE_VALUE;

#if USE_CLIENT
static cvar_t           *sys_viewlog;
#endif

static commandPrompt_t  sys_con;
static int              sys_hidden;
static CONSOLE_SCREEN_BUFFER_INFO   sbinfo;
static qboolean             gotConsole;

static void write_console_data( void *data, size_t len ) {
    DWORD dummy;

    WriteFile( houtput, data, len, &dummy, NULL );    
}

static void hide_console_input( void ) {
    int i;

    if( !sys_hidden ) {
        for( i = 0; i <= sys_con.inputLine.cursorPos; i++ ) {
            write_console_data( "\b \b", 3 );    
        }
    }
    sys_hidden++;
}

static void show_console_input( void ) {
    if( !sys_hidden ) {
        return;
    }

    sys_hidden--;
    if( !sys_hidden ) {
        write_console_data( "]", 1 );
        write_console_data( sys_con.inputLine.text, sys_con.inputLine.cursorPos );
    }
}

/*
================
Sys_ConsoleInput
================
*/
void Sys_RunConsole( void ) {
    INPUT_RECORD    recs[MAX_CONSOLE_INPUT_EVENTS];
    int     ch;
    DWORD   numread, numevents;
    int     i;
    inputField_t    *f;
    char    *s;

    if( hinput == INVALID_HANDLE_VALUE ) {
        return;
    }

    if( !gotConsole ) {
        return;
    }

    f = &sys_con.inputLine;
    while( 1 ) {
        if( !GetNumberOfConsoleInputEvents( hinput, &numevents ) ) {
            Com_EPrintf( "Error %lu getting number of console events.\n", GetLastError() );
            gotConsole = qfalse;
            return;
        }

        if( numevents <= 0 )
            break;
        if( numevents > MAX_CONSOLE_INPUT_EVENTS ) {
            numevents = MAX_CONSOLE_INPUT_EVENTS;
        }

        if( !ReadConsoleInput( hinput, recs, numevents, &numread ) ) {
            Com_EPrintf( "Error %lu reading console input.\n", GetLastError() );
            gotConsole = qfalse;
            return;
        }
            
        for( i = 0; i < numread; i++ ) {
            if( recs[i].EventType == WINDOW_BUFFER_SIZE_EVENT ) {
                // determine terminal width
                size_t width = recs[i].Event.WindowBufferSizeEvent.dwSize.X;

                if( !width ) {
                    Com_EPrintf( "Invalid console buffer width.\n" );
                    gotConsole = qfalse;
                    return;
                }
                
                sys_con.widthInChars = width;
                 
                // figure out input line width
                width--;
                if( width > MAX_FIELD_TEXT - 1 ) {
                    width = MAX_FIELD_TEXT - 1;
                }

                hide_console_input();
                IF_Init( &sys_con.inputLine, width, width );
                show_console_input();
                continue;
            }
            if( recs[i].EventType != KEY_EVENT ) {
                continue;
            }
    
            if( !recs[i].Event.KeyEvent.bKeyDown ) {
                continue;
            }

            switch( recs[i].Event.KeyEvent.wVirtualKeyCode ) {
            case VK_UP:
                hide_console_input();
                Prompt_HistoryUp( &sys_con );
                show_console_input();
                break;
            case VK_DOWN:
                hide_console_input();
                Prompt_HistoryDown( &sys_con );
                show_console_input();
                break;
            case VK_RETURN:
                hide_console_input();
                s = Prompt_Action( &sys_con );
                if( s ) {
                    if( *s == '\\' || *s == '/' ) {
                        s++;
                    }
                    Sys_Printf( "]%s\n", s );
                    Cbuf_AddText( &cmd_buffer, s );
                    Cbuf_AddText( &cmd_buffer, "\n" );
                } else {
                    write_console_data( "\n", 1 );
                }
                show_console_input();
                break;
            case VK_BACK:
                if( f->cursorPos ) {
                    f->text[--f->cursorPos] = 0;
                    write_console_data( "\b \b", 3 );
                }
                break;
            case VK_TAB:
                hide_console_input();
                Prompt_CompleteCommand( &sys_con, qfalse );
                f->cursorPos = strlen( f->text );
                show_console_input();
                break;
            default:
                ch = recs[i].Event.KeyEvent.uChar.AsciiChar;
                if( ch < 32 ) {
                    break;
                }
                if( f->cursorPos < f->maxChars - 1 ) {
                    write_console_data( &ch, 1 );
                    f->text[f->cursorPos] = ch;
                    f->text[++f->cursorPos] = 0;
                }
                break;
            }
        }
    }    
}

#define FOREGROUND_BLACK    0
#define FOREGROUND_WHITE    (FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED)

static const WORD textColors[8] = {
    FOREGROUND_BLACK,
    FOREGROUND_RED,
    FOREGROUND_GREEN,
    FOREGROUND_RED|FOREGROUND_GREEN,
    FOREGROUND_BLUE,
    FOREGROUND_BLUE|FOREGROUND_GREEN,
    FOREGROUND_RED|FOREGROUND_BLUE,
    FOREGROUND_WHITE
};

void Sys_SetConsoleColor( color_index_t color ) {
    WORD    attr, w;

    if( houtput == INVALID_HANDLE_VALUE ) {
        return;
    }

    if( !gotConsole ) {
        return;
    }

    attr = sbinfo.wAttributes & ~FOREGROUND_WHITE;

    switch( color ) {
    case COLOR_NONE:
        w = sbinfo.wAttributes;
        break;
    case COLOR_ALT:
        w = attr | FOREGROUND_GREEN;
        break;
    default:
        w = attr | textColors[color];
        break;
    }

    SetConsoleTextAttribute( houtput, w );
}

static void write_console_output( const char *text ) {
    char    buf[MAXPRINTMSG];
    size_t  len;

    for( len = 0; len < MAXPRINTMSG; len++ ) {
        int c = *text++;
        if( !c ) {
            break;
        }
        buf[len] = Q_charascii( c );
    }

    write_console_data( buf, len );
}

/*
================
Sys_ConsoleOutput

Print text to the dedicated console
================
*/
void Sys_ConsoleOutput( const char *text ) {
    if( houtput == INVALID_HANDLE_VALUE ) {
        return;
    }

    if( !gotConsole ) {
        write_console_output( text );
    } else {
        hide_console_input();
        write_console_output( text );
        show_console_input();
    }
}

void Sys_SetConsoleTitle( const char *title ) {
    if( gotConsole ) {
        SetConsoleTitle( title );
    }
}

static BOOL WINAPI Sys_ConsoleCtrlHandler( DWORD dwCtrlType ) {
    if( errorEntered ) {
        exit( 1 );
    }
    shouldExit = SE_FULL;
    return TRUE;
}

static void Sys_ConsoleInit( void ) {
    DWORD mode;
    size_t width;

#if USE_CLIENT
    if( !AllocConsole() ) {
        Com_EPrintf( "Couldn't create system console.\n" );
        return;
    }
#else
    if( statusHandle ) {
        return;
    }
#endif

    hinput = GetStdHandle( STD_INPUT_HANDLE );
    houtput = GetStdHandle( STD_OUTPUT_HANDLE );
    if( !GetConsoleScreenBufferInfo( houtput, &sbinfo ) ) {
        Com_EPrintf( "Couldn't get console buffer info.\n" );
        return;
    }

    // determine terminal width
    width = sbinfo.dwSize.X;
    if( !width ) {
        Com_EPrintf( "Invalid console buffer width.\n" );
        return;
    }
    sys_con.widthInChars = width;
    sys_con.printf = Sys_Printf;
    gotConsole = qtrue;

    SetConsoleTitle( APPLICATION " console" );
    SetConsoleCtrlHandler( Sys_ConsoleCtrlHandler, TRUE );
    GetConsoleMode( hinput, &mode );
    mode |= ENABLE_WINDOW_INPUT;
    SetConsoleMode( hinput, mode );

    // figure out input line width
    width--;
    if( width > MAX_FIELD_TEXT - 1 ) {
        width = MAX_FIELD_TEXT - 1;
    }
    IF_Init( &sys_con.inputLine, width, width );

    Com_DPrintf( "System console initialized (%d cols, %d rows).\n",
        sbinfo.dwSize.X, sbinfo.dwSize.Y );
}

#endif // USE_SYSCON

/*
===============================================================================

SERVICE CONTROL

===============================================================================
*/

#if USE_WINSVC

static void Sys_InstallService_f( void ) {
    char servicePath[256];
    char serviceName[1024];
    SC_HANDLE scm, service;
    DWORD error, length;
    char *commandline;

    if( Cmd_Argc() < 3 ) {
        Com_Printf( "Usage: %s <servicename> <+command> [...]\n"
            "Example: %s test +set net_port 27910 +map q2dm1\n",
                Cmd_Argv( 0 ), Cmd_Argv( 0 ) );
        return;
    }

    scm = OpenSCManager( NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS );
    if( !scm ) {
        error = GetLastError();
        if( error == ERROR_ACCESS_DENIED ) {
            Com_Printf( "Insufficient privileges for opening Service Control Manager.\n" );
        } else {
            Com_EPrintf( "%#lx opening Service Control Manager.\n", error );
        }
        return;
    }

    Q_concat( serviceName, sizeof( serviceName ), "Q2PRO - ", Cmd_Argv( 1 ), NULL );

    length = GetModuleFileName( NULL, servicePath, MAX_PATH );
    if( !length ) {
        error = GetLastError();
        Com_EPrintf( "%#lx getting module file name.\n", error );
        goto fail;
    }
    commandline = Cmd_RawArgsFrom( 2 );
    if( length + strlen( commandline ) + 10 > sizeof( servicePath ) - 1 ) {
        Com_Printf( "Oversize service command line.\n" );
        goto fail;
    }
    strcpy( servicePath + length, " -service " );
    strcpy( servicePath + length + 10, commandline );

    service = CreateService(
            scm,
            serviceName,
            serviceName,
            SERVICE_START,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START,
            SERVICE_ERROR_IGNORE,
            servicePath,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL );

    if( !service ) {
        error = GetLastError();
        if( error == ERROR_SERVICE_EXISTS || error == ERROR_DUPLICATE_SERVICE_NAME ) {
            Com_Printf( "Service already exists.\n" );
        } else {
            Com_EPrintf( "%#lx creating service.\n", error );
        }
        goto fail;
    }

    Com_Printf( "Service created successfully.\n" );

    CloseServiceHandle( service );

fail:
    CloseServiceHandle( scm );
}

static void Sys_DeleteService_f( void ) {
    char serviceName[256];
    SC_HANDLE scm, service;
    DWORD error;

    if( Cmd_Argc() < 2 ) {
        Com_Printf( "Usage: %s <servicename>\n", Cmd_Argv( 0 ) );
        return;
    }

    scm = OpenSCManager( NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS );
    if( !scm ) {
        error = GetLastError();
        if( error == ERROR_ACCESS_DENIED ) {
            Com_Printf( "Insufficient privileges for opening Service Control Manager.\n" );
        } else {
            Com_EPrintf( "%#lx opening Service Control Manager.\n", error );
        }
        return;
    }

    Q_concat( serviceName, sizeof( serviceName ), "Q2PRO - ", Cmd_Argv( 1 ), NULL );

    service = OpenService(
            scm,
            serviceName,
            DELETE );

    if( !service ) {
        error = GetLastError();
        if( error == ERROR_SERVICE_DOES_NOT_EXIST ) {
            Com_Printf( "Service doesn't exist.\n" );
        } else {
            Com_EPrintf( "%#lx opening service.\n", error );
        }
        goto fail;
    }

    if( !DeleteService( service ) ) {
        error = GetLastError();
        if( error == ERROR_SERVICE_MARKED_FOR_DELETE ) {
            Com_Printf( "Service has already been marked for deletion.\n" );
        } else {
            Com_EPrintf( "%#lx deleting service.\n", error );
        }
    } else {
        Com_Printf( "Service deleted successfully.\n" );
    }

    CloseServiceHandle( service );

fail:
    CloseServiceHandle( scm );
}

#endif // USE_WINSVC

/*
===============================================================================

HUNK

===============================================================================
*/

void Hunk_Begin( mempool_t *pool, size_t maxsize ) {
    // reserve a huge chunk of memory, but don't commit any yet
    pool->cursize = 0;
    pool->maxsize = ( maxsize + 4095 ) & ~4095;
    pool->base = VirtualAlloc( NULL, pool->maxsize, MEM_RESERVE, PAGE_NOACCESS );
    if( !pool->base ) {
        Com_Error( ERR_FATAL,
            "VirtualAlloc reserve %"PRIz" bytes failed. GetLastError() = %lu",
            pool->maxsize, GetLastError() );
    }
}

void *Hunk_Alloc( mempool_t *pool, size_t size ) {
    void    *buf;

    // round to cacheline
    size = ( size + 63 ) & ~63;

    pool->cursize += size;
    if( pool->cursize > pool->maxsize )
        Com_Error( ERR_FATAL, "%s: couldn't allocate %"PRIz" bytes", __func__, size );

    // commit pages as needed
    buf = VirtualAlloc( pool->base, pool->cursize, MEM_COMMIT, PAGE_READWRITE );
    if( !buf ) {
        Com_Error( ERR_FATAL,
            "VirtualAlloc commit %"PRIz" bytes failed. GetLastError() = %lu",
            pool->cursize, GetLastError() );
    }

    return ( byte * )pool->base + pool->cursize - size;
}

void Hunk_End( mempool_t *pool ) {
    // for statistics
    pool->mapped = ( pool->cursize + 4095 ) & ~4095;
}

void Hunk_Free( mempool_t *pool ) {
    if( pool->base ) {
        if( !VirtualFree( pool->base, 0, MEM_RELEASE ) ) {
            Com_Error( ERR_FATAL, "VirtualFree failed. GetLastError() = %lu",
                GetLastError() );
        }
    }

    memset( pool, 0, sizeof( *pool ) );
}

/*
===============================================================================

MISC

===============================================================================
*/

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
================
Sys_Error
================
*/
void Sys_Error( const char *error, ... ) {
    va_list     argptr;
    char        text[MAXPRINTMSG];

    va_start( argptr, error );
    Q_vsnprintf( text, sizeof( text ), error, argptr );
    va_end( argptr );

    errorEntered = qtrue;

#if USE_SYSCON
    Sys_SetConsoleColor( COLOR_RED );
    Sys_Printf( "********************\n"
                "FATAL: %s\n"
                "********************\n", text );
    Sys_SetConsoleColor( COLOR_NONE );
#endif

#if USE_WINSVC
    if( !statusHandle )
#endif
    {
#if USE_SYSCON
        if( gotConsole ) {
            Sleep( INFINITE );
        }
#endif
        MessageBoxA( NULL, text, APPLICATION " Fatal Error", MB_ICONERROR | MB_OK );
    }

    exit( 1 );
}

/*
================
Sys_Quit

This function never returns.
================
*/
void Sys_Quit( void ) {
    timeEndPeriod( 1 );

#if USE_CLIENT
#if USE_SYSCON
    if( dedicated && dedicated->integer ) {
        FreeConsole();
    }
#endif
#elif USE_WINSVC
    if( statusHandle && !shouldExit ) {
        shouldExit = SE_YES;
        Com_AbortFrame();
    }
#endif

    exit( 0 );
}

void Sys_DebugBreak( void ) {
    DebugBreak();
}

unsigned Sys_Milliseconds( void ) {
    return timeGetTime();
}

void Sys_AddDefaultConfig( void ) {
}

void Sys_FixFPCW( void ) {
#ifdef __i386__ // FIXME: MSVC?
    _controlfp( _PC_24|_RC_NEAR, _MCW_PC|_MCW_RC );
#endif
}

void Sys_Sleep( int msec ) {
    Sleep( msec );
}

/*
================
Sys_Init
================
*/
void Sys_Init( void ) {
    OSVERSIONINFO    vinfo;

    timeBeginPeriod( 1 );

    vinfo.dwOSVersionInfoSize = sizeof( vinfo );

    if( !GetVersionEx( &vinfo ) ) {
        Sys_Error( "Couldn't get OS info" );
    }

    iswinnt = qtrue;
    if( vinfo.dwMajorVersion < 4 ) {
        Sys_Error( APPLICATION " requires windows version 4 or greater" );
    }
    if( vinfo.dwPlatformId == VER_PLATFORM_WIN32s ) {
        Sys_Error( APPLICATION " doesn't run on Win32s" );
    } else if( vinfo.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS ) {
        if( vinfo.dwMinorVersion == 0 ) {
            Sys_Error( APPLICATION " doesn't run on Win95" );
        }
        iswinnt = qfalse;
    }

    // basedir <path>
    // allows the game to run from outside the data tree
    sys_basedir = Cvar_Get( "basedir", currentDirectory, CVAR_NOSET );
    sys_libdir = Cvar_Get( "libdir", currentDirectory, CVAR_NOSET );
    
    // homedir <path>
    // specifies per-user writable directory for demos, screenshots, etc
    sys_homedir = Cvar_Get( "homedir", "", CVAR_NOSET );

#if USE_WINSVC
    Cmd_AddCommand( "installservice", Sys_InstallService_f );
    Cmd_AddCommand( "deleteservice", Sys_DeleteService_f );
#endif

#if USE_SYSCON
    houtput = GetStdHandle( STD_OUTPUT_HANDLE );
#if USE_CLIENT
    sys_viewlog = Cvar_Get( "sys_viewlog", "0", CVAR_NOSET );

    if( dedicated->integer || sys_viewlog->integer )
#endif
        Sys_ConsoleInit();
#endif
}

/*
========================================================================

DLL LOADING

========================================================================
*/

void Sys_FreeLibrary( void *handle ) {
    if( !handle ) {
        return;
    }
    if( !FreeLibrary( handle ) ) {
        Com_Error( ERR_FATAL, "FreeLibrary failed on %p", handle );
    }
}

void *Sys_LoadLibrary( const char *path, const char *sym, void **handle ) {
    HMODULE module;
    void    *entry;

    *handle = NULL;

    module = LoadLibraryA( path );
    if( !module ) {
        Com_DPrintf( "%s failed: LoadLibrary returned %lu on %s\n",
            __func__, GetLastError(), path );
        return NULL;
    }

    entry = GetProcAddress( module, sym );
    if( !entry ) {
        Com_DPrintf( "%s failed: GetProcAddress returned %lu on %s\n",
            __func__, GetLastError(), path );
        FreeLibrary( module );
        return NULL;
    }

    *handle = module;

    Com_DPrintf( "%s succeeded: %s\n", __func__, path );

    return entry;
}

void *Sys_GetProcAddress( void *handle, const char *sym ) {
    return GetProcAddress( handle, sym );
}

/*
========================================================================

FILESYSTEM

========================================================================
*/

static inline time_t file_time_to_unix( FILETIME *f ) {
    ULARGE_INTEGER u = *( ULARGE_INTEGER * )f;
    return ( time_t )( ( u.QuadPart - 116444736000000000ULL ) / 10000000 );
}

/*
================
Sys_GetPathInfo
================
*/
qboolean Sys_GetPathInfo( const char *path, fsFileInfo_t *info ) {
    WIN32_FILE_ATTRIBUTE_DATA    data;

    if( !GetFileAttributesExA( path, GetFileExInfoStandard, &data ) ) {
        return qfalse;
    }

    if( info ) {
        info->size = data.nFileSizeLow;
        info->ctime = file_time_to_unix( &data.ftCreationTime );
        info->mtime = file_time_to_unix( &data.ftLastWriteTime );
    }

    return qtrue;
}

qboolean Sys_GetFileInfo( FILE *fp, fsFileInfo_t *info ) {
    int pos;

    pos = ftell( fp );
    fseek( fp, 0, SEEK_END );
    info->size = ftell( fp );
    info->ctime = 0;
    info->mtime = 0;
    fseek( fp, pos, SEEK_SET );

    return qtrue;
}


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
                                    size_t      length )
{
    WIN32_FIND_DATAA    findInfo;
    HANDLE  findHandle;
    char    findPath[MAX_OSPATH];
    char    dirPath[MAX_OSPATH];
    char    *name;

    if( *count >= MAX_LISTED_FILES ) {
        return;
    }

    Q_concat( findPath, sizeof( findPath ), path, "\\*", NULL );

    findHandle = FindFirstFileA( findPath, &findInfo );
    if( findHandle == INVALID_HANDLE_VALUE ) {
        return;
    }

    do {
        if( !strcmp( findInfo.cFileName, "." ) || !strcmp( findInfo.cFileName, ".." ) ) {
            continue;
        }

        if( findInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {
            Q_concat( dirPath, sizeof( dirPath ), path, "\\", findInfo.cFileName, NULL );
            Sys_ListFilteredFiles( listedFiles, count, dirPath, filter, flags, length );
        }

        if( ( flags & FS_SEARCHDIRS_MASK ) == FS_SEARCHDIRS_ONLY ) {
            if( !( findInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) ) {
                continue;
            }
        } else if( ( flags & FS_SEARCHDIRS_MASK ) == FS_SEARCHDIRS_NO ) {
            if( findInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {
                continue;
            }
        }

        Q_concat( dirPath, sizeof( dirPath ), path, "\\", findInfo.cFileName, NULL );
        if( !FS_WildCmp( filter, dirPath + length ) ) {
            continue;
        }

        name = ( flags & FS_SEARCH_SAVEPATH ) ? dirPath + length : findInfo.cFileName;

        // reformat it back to quake filesystem style
        FS_ReplaceSeparators( name, '/' );

        if( flags & FS_SEARCH_EXTRAINFO ) {
            time_t ctime = file_time_to_unix( &findInfo.ftCreationTime );
            time_t mtime = file_time_to_unix( &findInfo.ftLastWriteTime );
            listedFiles[( *count )++] = FS_CopyInfo( name, findInfo.nFileSizeLow, ctime, mtime );
        } else {
            listedFiles[( *count )++] = FS_CopyString( name );
        }
    } while( *count < MAX_LISTED_FILES && FindNextFileA( findHandle, &findInfo ) != FALSE );

    FindClose( findHandle );
}

/*
=================
Sys_ListFiles
=================
*/
void **Sys_ListFiles(   const char  *rawPath,
                        const char  *extension,
                        int         flags,
                        size_t      length,
                        int         *numFiles )
{
    WIN32_FIND_DATAA    findInfo;
    HANDLE  findHandle;
    char    path[MAX_OSPATH];
    char    findPath[MAX_OSPATH];
    void    *listedFiles[MAX_LISTED_FILES];
    int     count;
    char    *name;

    count = 0;

    if( numFiles ) {
        *numFiles = 0;
    }

    Q_strlcpy( path, rawPath, sizeof( path ) );
    FS_ReplaceSeparators( path, '\\' );

    if( flags & FS_SEARCH_BYFILTER ) {
        Q_strlcpy( findPath, extension, sizeof( findPath ) );
        FS_ReplaceSeparators( findPath, '\\' );
        Sys_ListFilteredFiles( listedFiles, &count, path, findPath, flags, length );
    } else {
        if( !extension || strchr( extension, ';' ) ) {
            Q_concat( findPath, sizeof( findPath ), path, "\\*", NULL );
        } else {
            if( *extension == '.' ) {
                extension++;
            }
            Q_concat( findPath, sizeof( findPath ), path, "\\*.", extension, NULL );
            extension = NULL; // do not check later
        }
        
        findHandle = FindFirstFileA( findPath, &findInfo );
        if( findHandle == INVALID_HANDLE_VALUE ) {
            return NULL;
        }

        do {
            if( !strcmp( findInfo.cFileName, "." ) || !strcmp( findInfo.cFileName, ".." ) ) {
                continue;
            }

            if( ( flags & FS_SEARCHDIRS_MASK ) == FS_SEARCHDIRS_ONLY ) {
                if( !( findInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) ) {
                    continue;
                }
            } else if( ( flags & FS_SEARCHDIRS_MASK ) == FS_SEARCHDIRS_NO ) {
                if( findInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {
                    continue;
                }
            }

            if( extension && !FS_ExtCmp( extension, findInfo.cFileName ) ) {
                continue;
            }

            name = ( flags & FS_SEARCH_SAVEPATH ) ? va( "%s\\%s", path, findInfo.cFileName ) : findInfo.cFileName;
            
            // reformat it back to quake filesystem style
            FS_ReplaceSeparators( name, '/' );

            if( flags & FS_SEARCH_EXTRAINFO ) {
                time_t ctime = file_time_to_unix( &findInfo.ftCreationTime );
                time_t mtime = file_time_to_unix( &findInfo.ftLastWriteTime );
                listedFiles[count++] = FS_CopyInfo( name, findInfo.nFileSizeLow, ctime, mtime );
            } else {
                listedFiles[count++] = FS_CopyString( name );
            }
        } while( count < MAX_LISTED_FILES && FindNextFileA( findHandle, &findInfo ) != FALSE );

        FindClose( findHandle );
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
Sys_GetCurrentDirectory
=================
*/
char *Sys_GetCurrentDirectory( void ) {
    return currentDirectory;
}

/*
========================================================================

MAIN

========================================================================
*/

#if ( _MSC_VER >= 1400 )
static void msvcrt_sucks( const wchar_t *expr, const wchar_t *func,
    const wchar_t *file, unsigned int line, uintptr_t unused ) {
}
#endif

static int Sys_Main( int argc, char **argv ) {
    char *p;

    // fix current directory to point to the basedir
    if( !GetModuleFileNameA( NULL, currentDirectory, sizeof( currentDirectory ) - 1 ) ) {
        return 1;
    }
    if( ( p = strrchr( currentDirectory, '\\' ) ) != NULL ) {
        *p = 0;
    }
#ifndef UNDER_CE
    if( !SetCurrentDirectoryA( currentDirectory ) ) {
        return 1;
    }
#endif

#if USE_DBGHELP
    // install our exception handler
    __try {
#endif

#if ( _MSC_VER >= 1400 )
    // work around strftime given invalid format string
    // killing the whole fucking process :((
    _set_invalid_parameter_handler( msvcrt_sucks );
#endif

    Qcommon_Init( argc, argv );

    // main program loop
    while( 1 ) {
        Qcommon_Frame();
#if USE_WINSVC
        if( shouldExit ) {
            if( shouldExit == SE_FULL ) {
                Com_Quit( NULL, KILL_DROP );
            }
            break;
        }
#endif
    }

#if USE_DBGHELP
    } __except( Sys_ExceptionHandler( GetExceptionCode(), GetExceptionInformation() ) ) {
        return 1;
    }
#endif

    // may get here when our service stops
    return 0;
}

#if USE_CLIENT

#define MAX_LINE_TOKENS    128

static char     *sys_argv[MAX_LINE_TOKENS];
static int      sys_argc;

/*
===============
Sys_ParseCommandLine

===============
*/
static void Sys_ParseCommandLine( char *line ) {
    sys_argc = 1;
    sys_argv[0] = APPLICATION;
    while( *line ) {
        while( *line && *line <= 32 ) {
            line++;
        }
        if( *line == 0 ) {
            break;
        }
        sys_argv[sys_argc++] = line;
        while( *line > 32 ) {
            line++;
        }
        if( *line == 0 ) {
            break;
        }
        *line = 0;
        if( sys_argc == MAX_LINE_TOKENS ) {
            break;
        }
        line++;
    }
}

/*
==================
WinMain

==================
*/
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow ) {
    // previous instances do not exist in Win32
    if( hPrevInstance ) {
        return 1;
    }

    hGlobalInstance = hInstance;
#ifndef UNICODE
    // TODO: wince support
    Sys_ParseCommandLine( lpCmdLine );
#endif
    return Sys_Main( sys_argc, sys_argv );
}

#else // USE_CLIENT

#if USE_WINSVC

static char     **sys_argv;
static int      sys_argc;

static VOID WINAPI ServiceHandler( DWORD fdwControl ) {
    if( fdwControl == SERVICE_CONTROL_STOP ) {
        shouldExit = SE_FULL;
    }
}

static VOID WINAPI ServiceMain( DWORD argc, LPTSTR *argv ) {
    SERVICE_STATUS    status;

    statusHandle = RegisterServiceCtrlHandler( APPLICATION, ServiceHandler );
    if( !statusHandle ) {
        return;
    }

    memset( &status, 0, sizeof( status ) );
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = SERVICE_RUNNING;
    status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    SetServiceStatus( statusHandle, &status );

    Sys_Main( sys_argc, sys_argv );

    status.dwCurrentState = SERVICE_STOPPED;
    status.dwControlsAccepted = 0;
    SetServiceStatus( statusHandle, &status );
}

static SERVICE_TABLE_ENTRY serviceTable[] = {
    { APPLICATION, ServiceMain },
    { NULL, NULL }
};

#endif // USE_WINSVC

/*
==================
main

==================
*/
int QDECL main( int argc, char **argv ) {
#if USE_WINSVC
    int i;
#endif

    hGlobalInstance = GetModuleHandle( NULL );

#if USE_WINSVC
    for( i = 1; i < argc; i++ ) {
        if( !strcmp( argv[i], "-service" ) ) {
            argv[i] = NULL;
            sys_argc = argc;
            sys_argv = argv;
            if( StartServiceCtrlDispatcher( serviceTable ) ) {
                return 0;
            }
            if( GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT ) {
                break; // fall back to normal server startup
            }
            return 1;
        }
    }
#endif
    
    return Sys_Main( argc, argv );
}

#endif // !USE_CLIENT

