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
#ifdef DEDICATED_ONLY
#include <winsvc.h>
#endif
#ifdef USE_DBGHELP
#include <dbghelp.h>
#endif

sysAPI_t	sys;

#define MAX_CONSOLE_INPUT_EVENTS	16

static HANDLE		hinput = INVALID_HANDLE_VALUE;
static HANDLE		houtput = INVALID_HANDLE_VALUE;

static cvar_t		    *sys_viewlog;
static commandPrompt_t	sys_con;
static int				sys_hidden;
static CONSOLE_SCREEN_BUFFER_INFO	sbinfo;
static qboolean			gotConsole;
static volatile qboolean	errorEntered;
static volatile qboolean	shouldExit;

#ifdef DEDICATED_ONLY
static SERVICE_STATUS_HANDLE	statusHandle;
#endif

HINSTANCE	hGlobalInstance;

qboolean iswinnt;

cvar_t	*sys_basedir;
cvar_t  *sys_libdir;
cvar_t  *sys_refdir;
cvar_t  *sys_homedir;

static char		currentDirectory[MAX_OSPATH];

/*
===============================================================================

SYSTEM IO

===============================================================================
*/

void Sys_DebugBreak( void ) {
	DebugBreak();
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
================
Sys_Error
================
*/
void Sys_Error( const char *error, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	va_start( argptr, error );
	Q_vsnprintf( text, sizeof( text ), error, argptr );
	va_end( argptr );

	errorEntered = qtrue;

	Sys_Printf( S_COLOR_RED "********************\n"
                            "FATAL: %s\n"
                            "********************\n", text );
#ifdef DEDICATED_ONLY
	if( !statusHandle )
#endif
	{
		if( gotConsole ) {
			Sleep( INFINITE );
		}
		MessageBoxA( NULL, text, APPLICATION " Fatal Error", MB_ICONERROR | MB_OK );
	}

	exit( 1 );
}

/*
================
Sys_Quit
================
*/
void Sys_Quit( void ) {
	timeEndPeriod( 1 );

#ifndef DEDICATED_ONLY
	if( dedicated && dedicated->integer ) {
		FreeConsole();
    }
#else
	if( !statusHandle )
#endif
		exit( 0 );
}

static void Sys_HideInput( void ) {
	DWORD		dummy;
	int i;

	if( !sys_hidden ) {
		for( i = 0; i <= sys_con.inputLine.cursorPos; i++ ) {
			WriteFile( houtput, "\b \b", 3, &dummy, NULL );	
		}
	}
	sys_hidden++;
}

static void Sys_ShowInput( void ) {
	DWORD		dummy;
	int i;

	if( !sys_hidden ) {
		Com_EPrintf( "Sys_ShowInput: not hidden\n" );
		return;
	}

	sys_hidden--;
	if( !sys_hidden ) {
		WriteFile( houtput, "]", 1, &dummy, NULL );	
		for( i = 0; i < sys_con.inputLine.cursorPos; i++ ) {
			WriteFile( houtput, &sys_con.inputLine.text[i], 1, &dummy, NULL );	
		}
	}
}

/*
================
Sys_ConsoleInput
================
*/
void Sys_RunConsole( void ) {
	INPUT_RECORD	recs[MAX_CONSOLE_INPUT_EVENTS];
	DWORD		dummy;
	int		ch;
	DWORD numread, numevents;
	int i;
	inputField_t *f;
	char *s;

	if( hinput == INVALID_HANDLE_VALUE ) {
		return;
	}

	if( !gotConsole ) {
		return;
	}

	f = &sys_con.inputLine;
	while( 1 ) {
		if( !GetNumberOfConsoleInputEvents( hinput, &numevents ) ) {
			Com_EPrintf( "Error %lu getting number of console events.\n"
				"Console IO disabled.\n", GetLastError() );
			gotConsole = qfalse;
			return;
		}

		if( numevents <= 0 )
			break;
		if( numevents > MAX_CONSOLE_INPUT_EVENTS ) {
		    numevents = MAX_CONSOLE_INPUT_EVENTS;
		}

		if( !ReadConsoleInput( hinput, recs, numevents, &numread ) ) {
			Com_EPrintf( "Error %lu reading console input.\n"
				"Console IO disabled.\n", GetLastError() );
			gotConsole = qfalse;
			return;
		}
			
		for( i = 0; i < numread; i++ ) {
			if( recs[i].EventType == WINDOW_BUFFER_SIZE_EVENT ) {
				sys_con.widthInChars = recs[i].Event.WindowBufferSizeEvent.dwSize.X;
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
				Sys_HideInput();
				Prompt_HistoryUp( &sys_con );
				Sys_ShowInput();
				break;
			case VK_DOWN:
				Sys_HideInput();
				Prompt_HistoryDown( &sys_con );
				Sys_ShowInput();
				break;
			case VK_RETURN:
				Sys_HideInput();
				s = Prompt_Action( &sys_con );
				if( s ) {
					if( *s == '\\' || *s == '/' ) {
						s++;
					}
					Sys_Printf( "]%s\n", s );
                    Cbuf_AddText( s );
                    Cbuf_AddText( "\n" );
				} else {
					WriteFile( houtput, "\n", 2, &dummy, NULL );	
				}
				Sys_ShowInput();
				break;
			case VK_BACK:
				if( f->cursorPos ) {
					f->cursorPos--;
					f->text[f->cursorPos] = 0;
					WriteFile( houtput, "\b \b", 3, &dummy, NULL );	
				}
				break;
			case VK_TAB:
				Sys_HideInput();
				Prompt_CompleteCommand( &sys_con, qfalse );
				f->cursorPos = strlen( f->text );
				Sys_ShowInput();
				break;
			default:
				ch = recs[i].Event.KeyEvent.uChar.AsciiChar;
				if( ch < 32 ) {
					break;
				}
				if( f->cursorPos < sizeof( f->text ) - 1 ) {
					WriteFile( houtput, &ch, 1, &dummy, NULL );
					f->text[f->cursorPos] = ch;
					f->text[f->cursorPos+1] = 0;
					f->cursorPos++;
				}
				break;
			}
		}
	}	
}

#define FOREGROUND_BLACK	0
#define FOREGROUND_WHITE	(FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED)

static WORD textColors[8] = {
	FOREGROUND_BLACK,
	FOREGROUND_RED,
	FOREGROUND_GREEN,
	FOREGROUND_RED|FOREGROUND_GREEN,
	FOREGROUND_BLUE,
	FOREGROUND_BLUE|FOREGROUND_GREEN,
	FOREGROUND_RED|FOREGROUND_BLUE,
	FOREGROUND_WHITE
};

/*
================
Sys_ConsoleOutput

Print text to the dedicated console
================
*/
void Sys_ConsoleOutput( const char *string ) {
	DWORD		dummy;
	char	text[MAXPRINTMSG];
	char *maxp, *p;
	int length;	
	WORD attr, w;
	int c;

	if( houtput == INVALID_HANDLE_VALUE ) {
		return;
	}

	if( !gotConsole ) {
		p = text;
		maxp = text + sizeof( text ) - 1;
		while( *string ) {
			if( Q_IsColorString( string ) ) {
				string += 2;
				continue;
			}
			*p++ = *string++ & 127;
			if( p == maxp ) {
				break;
			}
		}

		*p = 0;

		length = p - text;
		WriteFile( houtput, text, length, &dummy, NULL );
		return;
	}

	Sys_HideInput();

	attr = sbinfo.wAttributes & ~FOREGROUND_WHITE;
	
	while( *string ) {
		if( Q_IsColorString( string ) ) {
			c = string[1];
			string += 2;
			if( c == COLOR_ALT ) {
				w = attr | FOREGROUND_GREEN;
			} else if( c == COLOR_RESET ) {
				w = sbinfo.wAttributes;
			} else {
				w = attr | textColors[ ColorIndex( c ) ];
			}
			SetConsoleTextAttribute( houtput, w );
			continue;
		}

		p = text;
		maxp = text + sizeof( text ) - 1;
		do {
			*p++ = *string++ & 127;
			if( p == maxp ) {
				break;
			}
		} while( *string && !Q_IsColorString( string ) );

		*p = 0;

		length = p - text;
		WriteFile( houtput, text, length, &dummy, NULL );
	}

	SetConsoleTextAttribute( houtput, sbinfo.wAttributes );

	Sys_ShowInput();
}

static BOOL WINAPI Sys_ConsoleCtrlHandler( DWORD dwCtrlType ) {
	if( errorEntered ) {
		exit( 1 );
	}
	/* 32 bit writes are guranteed to be atomic */
	shouldExit = qtrue;
	return TRUE;
}

static void Sys_ConsoleInit( void ) {
	DWORD mode;

#ifdef DEDICATED_ONLY
	if( statusHandle ) {
		return;
	}
#else
	if( !AllocConsole() ) {
		Com_EPrintf( "Couldn't create system console.\n"
			"Console IO disabled.\n" );
		return;
	}
#endif

	hinput = GetStdHandle( STD_INPUT_HANDLE );
	houtput = GetStdHandle( STD_OUTPUT_HANDLE );
	if( !GetConsoleScreenBufferInfo( houtput, &sbinfo ) ) {
		Com_EPrintf( "Couldn't get console buffer info.\n"
			"Console IO disabled.\n" );
		return;
	}

	SetConsoleTitle( "q2pro console" );
	SetConsoleCtrlHandler( Sys_ConsoleCtrlHandler, TRUE );
	GetConsoleMode( hinput, &mode );
	mode |= ENABLE_WINDOW_INPUT;
	SetConsoleMode( hinput, mode );
	sys_con.widthInChars = sbinfo.dwSize.X;
	sys_con.Printf = Sys_Printf;
	gotConsole = qtrue;

	Com_DPrintf( "System console initialized (%d cols, %d rows).\n",
		sbinfo.dwSize.X, sbinfo.dwSize.Y );
}

/*
===============================================================================

SERVICE CONTROL

===============================================================================
*/

#ifdef DEDICATED_ONLY

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

	Com_sprintf( serviceName, sizeof( serviceName ), "Q2PRO - %s", Cmd_Argv( 1 ) );

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

	Com_sprintf( serviceName, sizeof( serviceName ), "Q2PRO - %s", Cmd_Argv( 1 ) );

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

#endif

/*
===============================================================================

HUNK

===============================================================================
*/

void Hunk_Begin( mempool_t *pool, int maxsize ) {
	// reserve a huge chunk of memory, but don't commit any yet
	pool->cursize = 0;
	pool->maxsize = ( maxsize + 4095 ) & ~4095;
	pool->base = VirtualAlloc( NULL, pool->maxsize, MEM_RESERVE, PAGE_NOACCESS );
	if( !pool->base ) {
		Com_Error( ERR_FATAL,
            "VirtualAlloc reserve %d bytes failed. GetLastError() = %lu",
			pool->maxsize, GetLastError() );
	}
}

void *Hunk_Alloc( mempool_t *pool, int size ) {
	void	*buf;

	// round to cacheline
	size = ( size + 31 ) & ~ 31;

	pool->cursize += size;
	if( pool->cursize > pool->maxsize )
		Com_Error( ERR_FATAL, "Hunk_Alloc: couldn't allocate %d bytes", size );

	// commit pages as needed
	buf = VirtualAlloc( pool->base, pool->cursize, MEM_COMMIT, PAGE_READWRITE );
	if( !buf ) {
		Com_Error( ERR_FATAL,
            "VirtualAlloc commit %d bytes failed. GetLastError() = %lu",
			pool->cursize, GetLastError() );
	}

	return ( void * )( pool->base + pool->cursize - size );
}

void Hunk_End( mempool_t *pool ) {
}

void Hunk_Free( mempool_t *pool ) {
	if( pool->base ) {
		if( !VirtualFree( pool->base, 0, MEM_RELEASE ) ) {
			Com_Error( ERR_FATAL, "VirtualFree failed. GetLastError() = %lu",
				GetLastError() );
		}
	}

	memset( pool, 0, sizeof( pool ) );
}

/*
===============================================================================

MISC

===============================================================================
*/

static inline void stime2qtime( qtime_t *q, SYSTEMTIME *s ) {
	q->tm_sec   = s->wSecond;
	q->tm_min   = s->wMinute;
	q->tm_hour  = s->wHour;
	q->tm_mday  = s->wDay;
	q->tm_mon   = s->wMonth;
	q->tm_year  = s->wYear;
	q->tm_wday  = s->wDayOfWeek;
	q->tm_yday  = -1;
	q->tm_isdst = -1;
}


/*
================
Sys_Milliseconds
================
*/
int Sys_Milliseconds( void ) {
	static uint32		base;
	static qboolean	initialized = qfalse;
	int	curtime;

	if( !initialized ) {	
		// let base retain 16 bits of effectively random data
		base = timeGetTime() & 0xffff0000;
		initialized = qtrue;
	}
	curtime = timeGetTime() - base;

	return curtime;
}

uint32 Sys_Realtime( void ) {
	return timeGetTime();
}

/*
================
Sys_Mkdir
================
*/
void Sys_Mkdir( const char *path ) {
	CreateDirectoryA( path, NULL );
}

qboolean Sys_RemoveFile( const char *path ) {
	if( !DeleteFileA( path ) ) {
		return qfalse;
	}
	return qtrue;
}

qboolean Sys_RenameFile( const char *from, const char *to ) {
	if( !MoveFileA( from, to ) ) {
		return qfalse;
	}
	return qtrue;
}

void Sys_AddDefaultConfig( void ) {
}

/*
================
Sys_GetFileInfo
================
*/
qboolean Sys_GetFileInfo( const char *path, fsFileInfo_t *info ) {
	WIN32_FILE_ATTRIBUTE_DATA	data;
	SYSTEMTIME	systemTime;
	BOOL bSuccess;

	bSuccess = GetFileAttributesExA( path, GetFileExInfoStandard, &data );
	if( !bSuccess ) {
		return qfalse;
	}

	if( !info ) {
		return qtrue;
	}

	info->fileSize = data.nFileSizeLow;

	bSuccess = FileTimeToSystemTime( &data.ftCreationTime, &systemTime );
	if( !bSuccess ) {
		return qfalse;
	}
	stime2qtime( &info->timeCreate, &systemTime );

	bSuccess = FileTimeToSystemTime( &data.ftLastWriteTime, &systemTime );
	if( !bSuccess ) {
		return qfalse;
	}
	stime2qtime( &info->timeModify, &systemTime );

	return qtrue;
}

/*
================
Sys_GetClipboardData

================
*/
char *Sys_GetClipboardData( void ) {
	char *data = NULL;
	char *cliptext;

	if( OpenClipboard( NULL ) != FALSE ) {
		HANDLE hClipboardData;

		if( ( hClipboardData = GetClipboardData( CF_TEXT ) ) != NULL ) {
			if( ( cliptext = GlobalLock( hClipboardData ) ) != NULL ) {
				data = Z_CopyString( cliptext );
				GlobalUnlock( hClipboardData );
			}
		}
		CloseClipboard();
	}
	return data;
}

/*
================
Sys_SetClipboardData

================
*/
void Sys_SetClipboardData( const char *data ) {
	char *cliptext;
	int	length;

	if( OpenClipboard( NULL ) != FALSE ) {
		HANDLE hClipboardData;

		length = strlen( data );
		hClipboardData = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, length + 1 );

		if( SetClipboardData( CF_TEXT, hClipboardData ) != NULL ) {
			if( ( cliptext = GlobalLock( hClipboardData ) ) != NULL ) {
				memcpy( cliptext, data, length + 1 );
				cliptext[length] = 0;
				GlobalUnlock( hClipboardData );
			}
		}
		CloseClipboard();
	}
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

/*
================
Sys_Init
================
*/
void Sys_Init( void ) {
	OSVERSIONINFO	vinfo;

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
    
	// homedir <path>
	// specifies per-user writable directory for demos, screenshots, etc
	sys_homedir = Cvar_Get( "homedir", "", CVAR_NOSET );

	sys_libdir = Cvar_Get( "libdir", currentDirectory, CVAR_NOSET );
	sys_refdir = Cvar_Get( "refdir", va( "%s\\baseq2pro", currentDirectory ), CVAR_NOSET );

	sys_viewlog = Cvar_Get( "sys_viewlog", "0", CVAR_NOSET );

#ifdef DEDICATED_ONLY
	Cmd_AddCommand( "installservice", Sys_InstallService_f );
	Cmd_AddCommand( "deleteservice", Sys_DeleteService_f );
#endif

	houtput = GetStdHandle( STD_OUTPUT_HANDLE );
	if( dedicated->integer || sys_viewlog->integer ) {
		Sys_ConsoleInit();
	}

	Sys_FillAPI( &sys );
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
//=======================================================================

static qboolean Sys_FindInfoToExtraInfo( WIN32_FIND_DATAA *findInfo, fsFileInfo_t *info ) {
	SYSTEMTIME	systemTime;
	BOOL	bSuccess;

	info->fileSize = findInfo->nFileSizeLow;

	bSuccess = FileTimeToSystemTime( &findInfo->ftCreationTime, &systemTime );
	if( !bSuccess ) {
		return qfalse;
	}
	stime2qtime( &info->timeCreate, &systemTime );

	bSuccess = FileTimeToSystemTime( &findInfo->ftLastWriteTime, &systemTime );
	if( !bSuccess ) {
		return qfalse;
	}
	stime2qtime( &info->timeModify, &systemTime );

	return qtrue;
}

/*
=================
Sys_ListFilteredFiles
=================
*/
static void Sys_ListFilteredFiles( char **listedFiles, int *count, const char *path,
								  const char *filter, uint32 flags, int length )
{
	WIN32_FIND_DATAA	findInfo;
	HANDLE		findHandle;
	char	findPath[MAX_OSPATH];
	char	dirPath[MAX_OSPATH];
	fsFileInfo_t	info;
	char	*name;

	if( *count >= MAX_LISTED_FILES ) {
		return;
	}

	Com_sprintf( findPath, sizeof( findPath ), "%s\\*", path );

	findHandle = FindFirstFileA( findPath, &findInfo );
	if( findHandle == INVALID_HANDLE_VALUE ) {
		return;
	}

	do {
		if( !strcmp( findInfo.cFileName, "." ) || !strcmp( findInfo.cFileName, ".." ) ) {
			continue;
		}

		if( findInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {
			Com_sprintf( dirPath, sizeof( dirPath ), "%s\\%s", path, findInfo.cFileName );
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

		Com_sprintf( dirPath, sizeof( dirPath ), "%s\\%s", path, findInfo.cFileName );
		if( !FS_WildCmp( filter, dirPath + length ) ) {
			continue;
		}

		name = ( flags & FS_SEARCH_SAVEPATH ) ? dirPath + length : findInfo.cFileName;

		if( flags & FS_SEARCH_EXTRAINFO ) {
			Sys_FindInfoToExtraInfo( &findInfo, &info );
			listedFiles[( *count )++] = FS_CopyExtraInfo( name, &info );
		} else {
			listedFiles[( *count )++] = Z_CopyString( name );
		}

	} while( *count < MAX_LISTED_FILES && FindNextFileA( findHandle, &findInfo ) != FALSE );

	FindClose( findHandle );

}

/*
=================
Sys_ListFiles
=================
*/
char **Sys_ListFiles( const char *rawPath, const char *extension, uint32 flags, int *numFiles ) {
	WIN32_FIND_DATAA	findInfo;
	HANDLE		findHandle;
	char	path[MAX_OSPATH];
	char	findPath[MAX_OSPATH];
	char	*listedFiles[MAX_LISTED_FILES];
	int		count;
	char	**list;
	int		i, length;
	fsFileInfo_t	info;
	char	*name;

	count = 0;

	if( numFiles ) {
		*numFiles = 0;
	}

	Q_strncpyz( path, rawPath, sizeof( path ) );
	Com_ReplaceSeparators( path, '\\' );

	if( flags & FS_SEARCH_BYFILTER ) {
		length = strlen( path );
        if( !length ) {
            return NULL;
        }
		Q_strncpyz( findPath, extension, sizeof( findPath ) );
		Com_ReplaceSeparators( findPath, '\\' );
		Sys_ListFilteredFiles( listedFiles, &count, path, findPath, flags, length + 1 );
	} else {
		if( !extension || strchr( extension, ';' ) ) {
			Com_sprintf( findPath, sizeof( findPath ), "%s\\*", path );
		} else {
			if( *extension == '.' ) {
				extension++;
			}
			Com_sprintf( findPath, sizeof( findPath ), "%s\\*.%s", path, extension );
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

			if( flags & FS_SEARCH_EXTRAINFO ) {
				Sys_FindInfoToExtraInfo( &findInfo, &info );
				listedFiles[count++] = FS_CopyExtraInfo( name, &info );
			} else {
				listedFiles[count++] = Z_CopyString( name );
			}
		} while( count < MAX_LISTED_FILES && FindNextFileA( findHandle, &findInfo ) != FALSE );

		FindClose( findHandle );
	}

	if( !count ) {
		return NULL;
	}

	if( !( flags & FS_SEARCH_NOSORT ) ) {
		qsort( listedFiles, count, sizeof( listedFiles[0] ), SortStrcmp );
	}

	// reformat filenames back to quake filesystem style
	list = Z_Malloc( sizeof( char * ) * ( count + 1 ) );
	for( i = 0; i < count; i++ ) {
		name = listedFiles[i];
		//Q_strlwr( name );
		Com_ReplaceSeparators( name, '/' );
		list[i] = name;
	}
	list[count] = NULL;

	if( numFiles ) {
		*numFiles = count;
	}

	return list;
}

/*
=================
Sys_FreeFileList
=================
*/
void Sys_FreeFileList( char **list ) {
	char **p;

	if( !list ) {
		Com_Error( ERR_FATAL, "Sys_FreeFileList: NULL" );
	}

	p = list;
	while( *p ) {
		Z_Free( *p++ );
	}

	Z_Free( list );
}

/*
=================
Sys_GetCurrentDirectory
=================
*/
char *Sys_GetCurrentDirectory( void ) {
	return currentDirectory;
}

//=======================================================================

#ifdef USE_ANTICHEAT

typedef BOOL (WINAPI *ISWOW64PROCESS)( HANDLE, PBOOL );
typedef PVOID (*FNINIT)( VOID );

PRIVATE PVOID anticheatApi;
PRIVATE FNINIT anticheatInit;
PRIVATE HMODULE anticheatHandle;
PRIVATE ISWOW64PROCESS pIsWow64Process;

//
// r1ch.net anticheat support
//
qboolean Sys_GetAntiCheatAPI( void ) {
	qboolean updated = qfalse;
    BOOL bIsWow64 = FALSE;

	//already loaded, just reinit
	if( anticheatInit ) {
		anticheatApi = anticheatInit();
		if( !anticheatApi ) {
	        Com_Printf( S_COLOR_RED "Anticheat failed to reinitialize!\n" );
            FreeLibrary( anticheatHandle );
            anticheatHandle = NULL;
            anticheatInit = NULL;
			return qfalse;
        }
		return qtrue;
	}

	//windows version check
	if( !winnt ) {
		Com_Printf( S_COLOR_YELLOW
			"Anticheat requires Windows 2000/XP/2003.\n" );
		return qfalse;
	}

    if( !pIsWow64Process ) {
        pIsWow64Process = ( ISWOW64PROCESS )GetProcAddress(
            GetModuleHandle( "kernel32" ), "IsWow64Process" );
    }
    if( pIsWow64Process ) {
        pIsWow64Process( GetCurrentProcess(), &bIsWow64 );
        if( bIsWow64 ) {
            Com_Printf( S_COLOR_YELLOW
				"Anticheat is incompatible with 64 bit Windows.\n" );
            return qfalse;
        }
    }

reInit:
	anticheatHandle = LoadLibrary( "anticheat" );
	if( !anticheatHandle ) {
		Com_Printf( S_COLOR_RED "Anticheat failed to load.\n" );
		return qfalse;
    }

	//this should never fail unless the anticheat.dll is bad
    anticheatInit = ( FNINIT )GetProcAddress(
            anticheatHandle, "Initialize" );
    if( !anticheatInit ) {
        Com_Printf( S_COLOR_RED "Couldn't get API of anticheat.dll!\n"
                    "Please check you are using a valid "
                    "anticheat.dll from http://antiche.at/" );
        FreeLibrary( anticheatHandle );
        anticheatHandle = NULL;
        return qfalse;
    }

	anticheatApi = anticheatInit();
	if( anticheatApi ) {
        return qtrue; // succeeded
    }

    FreeLibrary( anticheatHandle );
    anticheatHandle = NULL;
    anticheatInit = NULL;
    if( !updated ) {
        updated = qtrue;
        goto reInit;
    }

	Com_Printf( S_COLOR_RED "Anticheat failed to initialize.\n" );

    return qfalse;
}

#endif /* USE_ANTICHEAT */

#ifdef USE_DBGHELP

typedef DWORD (WINAPI *SETSYMOPTIONS)( DWORD );
typedef BOOL (WINAPI *SYMGETMODULEINFO)( HANDLE, DWORD, PIMAGEHLP_MODULE );
typedef BOOL (WINAPI *SYMINITIALIZE)( HANDLE, PSTR, BOOL );
typedef BOOL (WINAPI *SYMCLEANUP)( HANDLE );
typedef BOOL (WINAPI *ENUMERATELOADEDMODULES)( HANDLE, PENUMLOADED_MODULES_CALLBACK, PVOID );
typedef BOOL (WINAPI *STACKWALK)( DWORD, HANDLE, HANDLE, LPSTACKFRAME, PVOID,
	PREAD_PROCESS_MEMORY_ROUTINE, PFUNCTION_TABLE_ACCESS_ROUTINE, PGET_MODULE_BASE_ROUTINE,
	PTRANSLATE_ADDRESS_ROUTINE );
typedef BOOL (WINAPI *SYMFROMADDR)( HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO );
typedef PVOID (WINAPI *SYMFUNCTIONTABLEACCESS)( HANDLE, DWORD );
typedef DWORD (WINAPI *SYMGETMODULEBASE)( HANDLE, DWORD );

typedef HINSTANCE (WINAPI *SHELLEXECUTE)( HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT );

PRIVATE SETSYMOPTIONS pSymSetOptions;
PRIVATE SYMGETMODULEINFO pSymGetModuleInfo;
PRIVATE SYMINITIALIZE pSymInitialize;
PRIVATE SYMCLEANUP pSymCleanup;
PRIVATE ENUMERATELOADEDMODULES pEnumerateLoadedModules;
PRIVATE STACKWALK pStackWalk;
PRIVATE SYMFROMADDR pSymFromAddr;
PRIVATE SYMFUNCTIONTABLEACCESS pSymFunctionTableAccess;
PRIVATE SYMGETMODULEBASE pSymGetModuleBase;
PRIVATE SHELLEXECUTE pShellExecute;

PRIVATE HANDLE processHandle, threadHandle;

PRIVATE FILE *crashReport;

PRIVATE CHAR moduleName[MAX_PATH];

PRIVATE BOOL CALLBACK EnumModulesCallback(
	PSTR  ModuleName,
	ULONG ModuleBase,
	ULONG ModuleSize,
	PVOID UserContext )
{
	IMAGEHLP_MODULE moduleInfo;
	DWORD pc = ( DWORD )UserContext;
	BYTE buffer[4096];
	PBYTE data;
	UINT numBytes;
	VS_FIXEDFILEINFO *info;
	char version[64];
	char *symbols;

	strcpy( version, "unknown" );
	if( GetFileVersionInfo( ModuleName, 0, sizeof( buffer ), buffer ) ) {
		if( VerQueryValue( buffer, "\\", &data, &numBytes ) ) {
			info = ( VS_FIXEDFILEINFO * )data;
			Com_sprintf( version, sizeof( version ), "%u.%u.%u.%u",
				HIWORD( info->dwFileVersionMS ),
				LOWORD( info->dwFileVersionMS ),
				HIWORD( info->dwFileVersionLS ),
				LOWORD( info->dwFileVersionLS ) );
		}
	}
	
	symbols = "failed";
	moduleInfo.SizeOfStruct = sizeof( moduleInfo );
	if( pSymGetModuleInfo( processHandle, ModuleBase, &moduleInfo ) ) {
		ModuleName = moduleInfo.ModuleName;
		switch( moduleInfo.SymType ) {
			case SymCoff: symbols = "COFF"; break;
			case SymExport: symbols = "export"; break;
			case SymNone: symbols = "none"; break;
			case SymPdb: symbols = "PDB"; break;
			default: symbols = "unknown"; break;
		}
	}
	
	fprintf( crashReport, "%08x %08x %s (version %s, symbols %s) ",
		ModuleBase, ModuleBase + ModuleSize, ModuleName, version, symbols );
	if( pc >= ModuleBase && pc < ModuleBase + ModuleSize ) {
		Q_strncpyz( moduleName, ModuleName, sizeof( moduleName ) );
		fprintf( crashReport, "*\n" );
	} else {
		fprintf( crashReport, "\n" );
	}

	return TRUE;
}

PRIVATE DWORD Sys_ExceptionHandler( DWORD exceptionCode, LPEXCEPTION_POINTERS exceptionInfo ) {
	STACKFRAME stackFrame;
	PCONTEXT context;
	SYMBOL_INFO *symbol;
	int count, ret, i;
	DWORD64 offset;
	BYTE buffer[sizeof( SYMBOL_INFO ) + 256 - 1];
	IMAGEHLP_MODULE moduleInfo;
	char path[MAX_PATH];
	char execdir[MAX_PATH];
	char *p;
	HMODULE helpModule, shellModule;
	SYSTEMTIME systemTime;
	static char *monthNames[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	OSVERSIONINFO	vinfo;

#ifndef DEDICATED_ONLY
	Win_Shutdown();
#endif

	ret = MessageBox( NULL, APPLICATION " has encountered an unhandled "
		"exception and needs to be terminated.\n"
		"Would you like to generate a crash report?",
		"Unhandled Exception",
		MB_ICONERROR | MB_YESNO
#ifdef DEDICATED_ONLY
		| MB_SERVICE_NOTIFICATION
#endif
		);
	if( ret == IDNO ) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	helpModule = LoadLibrary( "dbghelp.dll" );
	if( !helpModule ) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

#define GPA( x, y )															\
	do {																	\
		p ## y = ( x )GetProcAddress( helpModule, #y );						\
		if( !p ## y ) {														\
			return EXCEPTION_CONTINUE_SEARCH;								\
		}																	\
	} while( 0 )

	GPA( SETSYMOPTIONS, SymSetOptions );
	GPA( SYMGETMODULEINFO, SymGetModuleInfo );
	GPA( SYMCLEANUP, SymCleanup );
	GPA( SYMINITIALIZE, SymInitialize );
	GPA( ENUMERATELOADEDMODULES, EnumerateLoadedModules );
	GPA( STACKWALK, StackWalk );
	GPA( SYMFROMADDR, SymFromAddr );
	GPA( SYMFUNCTIONTABLEACCESS, SymFunctionTableAccess );
	GPA( SYMGETMODULEBASE, SymGetModuleBase );

	pSymSetOptions( SYMOPT_LOAD_ANYTHING|SYMOPT_DEBUG|SYMOPT_FAIL_CRITICAL_ERRORS );
	processHandle = GetCurrentProcess();
	threadHandle = GetCurrentThread();

	GetModuleFileName( NULL, execdir, sizeof( execdir ) - 1 );
	execdir[sizeof( execdir ) - 1] = 0;
	p = strrchr( execdir, '\\' );
	if( p ) {
		*p = 0;
	}
	
	GetSystemTime( &systemTime );

	for( i = 0; i < 100; i++ ) {
		Com_sprintf( path, sizeof( path ), "%s\\Q2PRO_CrashReport%02d.txt", execdir, i );
		if( !Sys_GetFileInfo( path, NULL ) ) {
			break;
		}
	}
	crashReport = fopen( path, "w" );
	if( !crashReport ) {
		return EXCEPTION_CONTINUE_SEARCH;
	}

	pSymInitialize( processHandle, execdir, TRUE );

	fprintf( crashReport, "Crash report generated %s %u %u, %02u:%02u:%02u UTC\n",
		monthNames[(systemTime.wMonth - 1) % 12], systemTime.wDay, systemTime.wYear,
		systemTime.wHour, systemTime.wMinute, systemTime.wSecond );
	fprintf( crashReport, "by " APPLICATION " " VERSION ", built " __DATE__", " __TIME__ "\n" );

	vinfo.dwOSVersionInfoSize = sizeof( vinfo );
	if( GetVersionEx( &vinfo ) ) {
		fprintf( crashReport, "\nWindows version: %u.%u (build %u) %s\n",
			vinfo.dwMajorVersion, vinfo.dwMinorVersion, vinfo.dwBuildNumber, vinfo.szCSDVersion );
	}

	strcpy( moduleName, "unknown" );

	context = exceptionInfo->ContextRecord;

	fprintf( crashReport, "\nLoaded modules:\n" );
	pEnumerateLoadedModules( processHandle, EnumModulesCallback, ( PVOID )context->Eip );

	fprintf( crashReport, "\nException information:\n" );
	fprintf( crashReport, "Code: %08x\n", exceptionCode );
	fprintf( crashReport, "Address: %08x (%s)\n",
		context->Eip, moduleName );

	fprintf( crashReport, "\nThread context:\n" );
	fprintf( crashReport, "EIP: %08x EBP: %08x ESP: %08x\n",
		context->Eip, context->Ebp, context->Esp );
	fprintf( crashReport, "EAX: %08x EBX: %08x ECX: %08x\n",
		context->Eax, context->Ebx, context->Ecx );
	fprintf( crashReport, "EDX: %08x ESI: %08x EDI: %08x\n",
		context->Edx, context->Esi, context->Edi );

	memset( &stackFrame, 0, sizeof( stackFrame ) );
	stackFrame.AddrPC.Offset = context->Eip;
	stackFrame.AddrPC.Mode = AddrModeFlat;
	stackFrame.AddrFrame.Offset = context->Ebp;
	stackFrame.AddrFrame.Mode = AddrModeFlat;
	stackFrame.AddrStack.Offset = context->Esp;
	stackFrame.AddrStack.Mode = AddrModeFlat;	

	fprintf( crashReport, "\nStack trace:\n" );
	count = 0;
	symbol = ( SYMBOL_INFO * )buffer;
	symbol->SizeOfStruct = sizeof( *symbol );
	symbol->MaxNameLen = 256;
	while( pStackWalk( IMAGE_FILE_MACHINE_I386,
		processHandle,
		threadHandle,
		&stackFrame,
		context,
		NULL,
		pSymFunctionTableAccess,
		pSymGetModuleBase,
		NULL ) )
	{
		fprintf( crashReport, "%d: %08x %08x %08x %08x ",
			count,
			stackFrame.Params[0],
			stackFrame.Params[1],
			stackFrame.Params[2],
			stackFrame.Params[3] );

		moduleInfo.SizeOfStruct = sizeof( moduleInfo );
		if( pSymGetModuleInfo( processHandle, stackFrame.AddrPC.Offset, &moduleInfo ) ) {
			if( moduleInfo.SymType != SymNone && moduleInfo.SymType != SymExport &&
				pSymFromAddr( processHandle, stackFrame.AddrPC.Offset, &offset, symbol ) )
			{
				fprintf( crashReport, "%s!%s+%#x\n", 
					moduleInfo.ModuleName,
					symbol->Name, offset );
			} else {
				fprintf( crashReport, "%s!%#x\n",
					moduleInfo.ModuleName,
					stackFrame.AddrPC.Offset );
			}
		} else {
			fprintf( crashReport, "%#x\n",
				stackFrame.AddrPC.Offset );
		}
		count++;
	}

	fclose( crashReport );

	shellModule = LoadLibrary( "shell32.dll" );
	if( shellModule ) {
		pShellExecute = ( SHELLEXECUTE )GetProcAddress( shellModule, "ShellExecuteA" );
		if( pShellExecute ) {
			pShellExecute( NULL, "open", path, NULL, execdir, SW_SHOW );
		}
	}

	pSymCleanup( processHandle );

	ExitProcess( 1 );
	return EXCEPTION_CONTINUE_SEARCH;
}

#if 0
EXCEPTION_DISPOSITION _ExceptionHandler(
	EXCEPTION_RECORD *ExceptionRecord,
	void *EstablisherFrame,
	CONTEXT *ContextRecord,
	void *DispatcherContext )
{
	return ExceptionContinueSearch;
}

#ifndef __GNUC__
#define __try1( handler )	__asm { \
	__asm push handler \
	__asm push fs:[0] \
	__asm mov fs:0, esp \
}
#define __except1	__asm { \
	__asm mov eax, [esp] \
	__asm mov fs:[0], eax \
	__asm add esp, 8 \
}
#endif
#endif

#endif /* USE_DBGHELP */

#if ( _MSC_VER >= 1400 )
static void msvcrt_sucks( const wchar_t *expr, const wchar_t *func, const wchar_t *file, unsigned int line, uintptr_t unused ) {
}
#endif

/*
==================
WinMain

==================
*/
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow ) {
	/* previous instances do not exist in Win32 */
	if( hPrevInstance ) {
		return 1;
	}

#ifdef DEDICATED_ONLY
	if( !GetModuleFileName( NULL, currentDirectory, sizeof( currentDirectory ) - 1 ) ) {
		return 1;
	}
	currentDirectory[sizeof( currentDirectory ) - 1] = 0;
	{
		char *p = strrchr( currentDirectory, '\\' );
		if( p ) {
			*p = 0;
		}
		if( !SetCurrentDirectory( currentDirectory ) ) {
			return 1;
		}
	}
#else
	if( !GetCurrentDirectory( sizeof( currentDirectory ) - 1, currentDirectory ) ) {
		return 1;
	}
	currentDirectory[sizeof( currentDirectory ) - 1] = 0;
#endif

	hGlobalInstance = hInstance;

#ifdef USE_DBGHELP
#ifdef _MSC_VER
	__try {
#else
	__try1( Sys_ExceptionHandler );
#endif
#endif /* USE_DBGHELP */

#if ( _MSC_VER >= 1400 )
	// no, please, don't let strftime kill the whole fucking
	// process just because it does not conform to C99 :((
	_set_invalid_parameter_handler( msvcrt_sucks );
#endif

	Qcommon_Init( lpCmdLine );

	/* main program loop */
	while( !shouldExit ) {
		Qcommon_Frame();
	}

	Com_Quit();

#ifdef USE_DBGHELP
#ifdef _MSC_VER
	} __except( Sys_ExceptionHandler( GetExceptionCode(), GetExceptionInformation() ) ) {
		return 1;
	}
#else
	__except1;
#endif
#endif /* USE_DBGHELP */

	// may get here when our service stops
    return 0;
}

#ifdef DEDICATED_ONLY

static VOID WINAPI ServiceHandler( DWORD fdwControl ) {
	if( fdwControl == SERVICE_CONTROL_STOP ) {
		shouldExit = qtrue;
	}
}

static VOID WINAPI ServiceMain( DWORD argc, LPTSTR *argv ) {
	SERVICE_STATUS	status;

	statusHandle = RegisterServiceCtrlHandler( APPLICATION, ServiceHandler );
	if( !statusHandle ) {
		return;
	}

	memset( &status, 0, sizeof( status ) );
	status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	status.dwCurrentState = SERVICE_RUNNING;
	status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
	SetServiceStatus( statusHandle, &status );

	WinMain( GetModuleHandle( NULL ), NULL, GetCommandLineA(), 0 );

	status.dwCurrentState = SERVICE_STOPPED;
	status.dwControlsAccepted = 0;
	SetServiceStatus( statusHandle, &status );
}

static SERVICE_TABLE_ENTRY serviceTable[] = {
	{ APPLICATION, ServiceMain },
	{ NULL, NULL }
};

/*
==================
main

==================
*/
int QDECL main( int argc, char **argv ) {
	int i;

	for( i = 1; i < argc; i++ ) {
		if( !strcmp( argv[i], "-service" ) ) {
			goto service;
		}
	}

	return WinMain( GetModuleHandle( NULL ), NULL, GetCommandLineA(), 0 );

service:
	if( !StartServiceCtrlDispatcher( serviceTable ) ) {
		if( GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT ) {
			return WinMain( GetModuleHandle( NULL ), NULL, GetCommandLineA(), 0 );
		}
		return 1;
	}

	return 0;
}

#endif

