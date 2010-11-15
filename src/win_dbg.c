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
#include <dbghelp.h>

typedef DWORD (WINAPI *SETSYMOPTIONS)( DWORD );
typedef BOOL (WINAPI *SYMGETMODULEINFO64)( HANDLE, DWORD64,
    PIMAGEHLP_MODULE64 );
typedef BOOL (WINAPI *SYMINITIALIZE)( HANDLE, PSTR, BOOL );
typedef BOOL (WINAPI *SYMCLEANUP)( HANDLE );
typedef BOOL (WINAPI *ENUMERATELOADEDMODULES64)( HANDLE,
    PENUMLOADED_MODULES_CALLBACK64, PVOID );
typedef BOOL (WINAPI *STACKWALK64)( DWORD, HANDLE, HANDLE, LPSTACKFRAME64,
    PVOID, PREAD_PROCESS_MEMORY_ROUTINE64, PFUNCTION_TABLE_ACCESS_ROUTINE64,
    PGET_MODULE_BASE_ROUTINE64, PTRANSLATE_ADDRESS_ROUTINE64 );
typedef BOOL (WINAPI *SYMFROMADDR)( HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO );
typedef PVOID (WINAPI *SYMFUNCTIONTABLEACCESS64)( HANDLE, DWORD64 );
typedef DWORD64 (WINAPI *SYMGETMODULEBASE64)( HANDLE, DWORD64 );

typedef HINSTANCE (WINAPI *SHELLEXECUTE)( HWND, LPCSTR, LPCSTR,
    LPCSTR, LPCSTR, INT );

PRIVATE SETSYMOPTIONS pSymSetOptions;
PRIVATE SYMGETMODULEINFO64 pSymGetModuleInfo64;
PRIVATE SYMINITIALIZE pSymInitialize;
PRIVATE SYMCLEANUP pSymCleanup;
PRIVATE ENUMERATELOADEDMODULES64 pEnumerateLoadedModules64;
PRIVATE STACKWALK64 pStackWalk64;
PRIVATE SYMFROMADDR pSymFromAddr;
PRIVATE SYMFUNCTIONTABLEACCESS64 pSymFunctionTableAccess64;
PRIVATE SYMGETMODULEBASE64 pSymGetModuleBase64;
PRIVATE SHELLEXECUTE pShellExecute;

PRIVATE HANDLE processHandle, threadHandle;

PRIVATE FILE *crashReport;

PRIVATE CHAR moduleName[MAX_PATH];

PRIVATE BOOL CALLBACK EnumModulesCallback(
    PCTSTR ModuleName,
    DWORD64 ModuleBase,
    ULONG ModuleSize,
    PVOID UserContext )
{
    IMAGEHLP_MODULE64 moduleInfo;
    DWORD64 pc = ( DWORD64 )UserContext;
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
            sprintf( version, "%u.%u.%u.%u",
                HIWORD( info->dwFileVersionMS ),
                LOWORD( info->dwFileVersionMS ),
                HIWORD( info->dwFileVersionLS ),
                LOWORD( info->dwFileVersionLS ) );
        }
    }
    
    symbols = "failed";
    moduleInfo.SizeOfStruct = sizeof( moduleInfo );
    if( pSymGetModuleInfo64( processHandle, ModuleBase, &moduleInfo ) ) {
        ModuleName = moduleInfo.ModuleName;
        switch( moduleInfo.SymType ) {
            case SymCoff: symbols = "COFF"; break;
            case SymExport: symbols = "export"; break;
            case SymNone: symbols = "none"; break;
            case SymPdb: symbols = "PDB"; break;
            default: symbols = "unknown"; break;
        }
    }
    
    fprintf( crashReport, "%p %p %s (version %s, symbols %s) ",
        ModuleBase, ModuleBase + ModuleSize, ModuleName, version, symbols );
    if( pc >= ModuleBase && pc < ModuleBase + ModuleSize ) {
        strncpy( moduleName, ModuleName, sizeof( moduleName ) - 1 );
        moduleName[ sizeof( moduleName ) - 1 ] = 0;
        fprintf( crashReport, "*\n" );
    } else {
        fprintf( crashReport, "\n" );
    }

    return TRUE;
}

DWORD Sys_ExceptionHandler( DWORD exceptionCode, LPEXCEPTION_POINTERS exceptionInfo ) {
    STACKFRAME64 stackFrame;
    PCONTEXT context;
    SYMBOL_INFO *symbol;
    int count, ret, i, len;
    DWORD64 offset;
    BYTE buffer[sizeof( SYMBOL_INFO ) + 256 - 1];
    IMAGEHLP_MODULE64 moduleInfo;
    char path[MAX_PATH];
    char execdir[MAX_PATH];
    char *p;
    HMODULE helpModule, shellModule;
    SYSTEMTIME systemTime;
    static const char monthNames[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    OSVERSIONINFO    vinfo;

#if USE_CLIENT
    Win_Shutdown();
#endif

    ret = MessageBox( NULL, APPLICATION " has encountered an unhandled "
        "exception and needs to be terminated.\n"
        "Would you like to generate a crash report?",
        "Unhandled Exception",
        MB_ICONERROR | MB_YESNO
#if !USE_CLIENT
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

#define GPA( x, y )                                                            \
    do {                                                                    \
        p ## y = ( x )GetProcAddress( helpModule, #y );                        \
        if( !p ## y ) {                                                        \
            return EXCEPTION_CONTINUE_SEARCH;                                \
        }                                                                    \
    } while( 0 )

    GPA( SETSYMOPTIONS, SymSetOptions );
    GPA( SYMGETMODULEINFO64, SymGetModuleInfo64 );
    GPA( SYMCLEANUP, SymCleanup );
    GPA( SYMINITIALIZE, SymInitialize );
    GPA( ENUMERATELOADEDMODULES64, EnumerateLoadedModules64 );
    GPA( STACKWALK64, StackWalk64 );
    GPA( SYMFROMADDR, SymFromAddr );
    GPA( SYMFUNCTIONTABLEACCESS64, SymFunctionTableAccess64 );
    GPA( SYMGETMODULEBASE64, SymGetModuleBase64 );

    pSymSetOptions( SYMOPT_LOAD_ANYTHING|SYMOPT_DEBUG|SYMOPT_FAIL_CRITICAL_ERRORS );
    processHandle = GetCurrentProcess();
    threadHandle = GetCurrentThread();

    GetModuleFileName( NULL, execdir, sizeof( execdir ) - 1 );
    execdir[sizeof( execdir ) - 1] = 0;
    p = strrchr( execdir, '\\' );
    if( !p ) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    *p = 0;
    len = p - execdir;
    if( len + 24 >= MAX_PATH ) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    
    memcpy( path, execdir, len );
    memcpy( path + len, "\\Q2PRO_CrashReportXX.txt", 25 );
    for( i = 0; i < 100; i++ ) {
        path[len+18] = '0' + i / 10;
        path[len+19] = '0' + i % 10;
        if( !Sys_GetPathInfo( path, NULL ) ) {
            break;
        }
    }
    crashReport = fopen( path, "w" );
    if( !crashReport ) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    pSymInitialize( processHandle, execdir, TRUE );

    GetSystemTime( &systemTime );
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
    pEnumerateLoadedModules64( processHandle, EnumModulesCallback,
#ifdef _WIN64
        ( PVOID )context->Rip
#else
        ( PVOID )context->Eip
#endif
    );

    fprintf( crashReport, "\nException information:\n" );
    fprintf( crashReport, "Code: %08x\n", exceptionCode );
    fprintf( crashReport, "Address: %p (%s)\n",
#ifdef _WIN64
        context->Rip,
#else
        context->Eip,
#endif
        moduleName );

    fprintf( crashReport, "\nThread context:\n" );
#ifdef _WIN64
    fprintf( crashReport, "RIP: %p RBP: %p RSP: %p\n",
        context->Rip, context->Rbp, context->Rsp );
    fprintf( crashReport, "RAX: %p RBX: %p RCX: %p\n",
        context->Rax, context->Rbx, context->Rcx );
    fprintf( crashReport, "RDX: %p RSI: %p RDI: %p\n",
        context->Rdx, context->Rsi, context->Rdi );
#else
    fprintf( crashReport, "EIP: %p EBP: %p ESP: %p\n",
        context->Eip, context->Ebp, context->Esp );
    fprintf( crashReport, "EAX: %p EBX: %p ECX: %p\n",
        context->Eax, context->Ebx, context->Ecx );
    fprintf( crashReport, "EDX: %p ESI: %p EDI: %p\n",
        context->Edx, context->Esi, context->Edi );
#endif

    memset( &stackFrame, 0, sizeof( stackFrame ) );
#ifdef _WIN64
    stackFrame.AddrPC.Offset = context->Rip;
    stackFrame.AddrFrame.Offset = context->Rbp;
    stackFrame.AddrStack.Offset = context->Rsp;
#else
    stackFrame.AddrPC.Offset = context->Eip;
    stackFrame.AddrFrame.Offset = context->Ebp;
    stackFrame.AddrStack.Offset = context->Esp;
#endif
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Mode = AddrModeFlat;    

    fprintf( crashReport, "\nStack trace:\n" );
    count = 0;
    symbol = ( SYMBOL_INFO * )buffer;
    symbol->SizeOfStruct = sizeof( SYMBOL_INFO );
    symbol->MaxNameLen = 256;
    while( pStackWalk64(
#ifdef _WIN64
        IMAGE_FILE_MACHINE_AMD64,
#else
        IMAGE_FILE_MACHINE_I386,
#endif
        processHandle,
        threadHandle,
        &stackFrame,
        context,
        NULL,
        pSymFunctionTableAccess64,
        pSymGetModuleBase64,
        NULL ) )
    {
        fprintf( crashReport, "%d: %p %p %p %p ",
            count,
            stackFrame.Params[0],
            stackFrame.Params[1],
            stackFrame.Params[2],
            stackFrame.Params[3] );

        moduleInfo.SizeOfStruct = sizeof( moduleInfo );
        if( pSymGetModuleInfo64( processHandle, stackFrame.AddrPC.Offset, &moduleInfo ) ) {
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

