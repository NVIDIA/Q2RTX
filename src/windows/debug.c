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
// win_dbg.c -- crash dump generation
//

#include "client.h"
#include <dbghelp.h>

typedef DWORD (WINAPI *SETSYMOPTIONS)(DWORD);
typedef BOOL (WINAPI *SYMGETMODULEINFO64)(HANDLE, DWORD64,
        PIMAGEHLP_MODULE64);
typedef BOOL (WINAPI *SYMINITIALIZE)(HANDLE, PSTR, BOOL);
typedef BOOL (WINAPI *SYMCLEANUP)(HANDLE);
typedef BOOL (WINAPI *ENUMERATELOADEDMODULES64)(HANDLE,
        PENUMLOADED_MODULES_CALLBACK64, PVOID);
typedef BOOL (WINAPI *STACKWALK64)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64,
                                   PVOID, PREAD_PROCESS_MEMORY_ROUTINE64, PFUNCTION_TABLE_ACCESS_ROUTINE64,
                                   PGET_MODULE_BASE_ROUTINE64, PTRANSLATE_ADDRESS_ROUTINE64);
typedef BOOL (WINAPI *SYMFROMADDR)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
typedef PVOID(WINAPI *SYMFUNCTIONTABLEACCESS64)(HANDLE, DWORD64);
typedef DWORD64(WINAPI *SYMGETMODULEBASE64)(HANDLE, DWORD64);
typedef BOOL (WINAPI *GETFILEVERSIONINFOA)(LPCSTR, DWORD, DWORD, PVOID);
typedef BOOL (WINAPI *VERQUERYVALUEA)(const LPVOID, LPSTR, LPVOID *, PUINT);
typedef HINSTANCE(WINAPI *SHELLEXECUTEA)(HWND, LPCSTR, LPCSTR,
        LPCSTR, LPCSTR, INT);

STATIC SETSYMOPTIONS pSymSetOptions;
STATIC SYMGETMODULEINFO64 pSymGetModuleInfo64;
STATIC SYMINITIALIZE pSymInitialize;
STATIC SYMCLEANUP pSymCleanup;
STATIC ENUMERATELOADEDMODULES64 pEnumerateLoadedModules64;
STATIC STACKWALK64 pStackWalk64;
STATIC SYMFROMADDR pSymFromAddr;
STATIC SYMFUNCTIONTABLEACCESS64 pSymFunctionTableAccess64;
STATIC SYMGETMODULEBASE64 pSymGetModuleBase64;
STATIC GETFILEVERSIONINFOA pGetFileVersionInfoA;
STATIC VERQUERYVALUEA pVerQueryValueA;
STATIC SHELLEXECUTEA pShellExecuteA;

STATIC HANDLE processHandle, threadHandle;
STATIC HANDLE crashReport;
STATIC CHAR faultyModuleName[MAX_PATH];
STATIC DWORD moduleInfoSize;

#define MI_SIZE_V1   584
#define MI_SIZE_V2  1664
#define MI_SIZE_V3  1672

// google://dbghelp+not+backwards+compatible
STATIC CONST DWORD tryModuleSizes[4] = {
    sizeof(IMAGEHLP_MODULE64), MI_SIZE_V3, MI_SIZE_V2, MI_SIZE_V1
};

static const char monthNames[12][4] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

#define PRIxx64 "#.16I64x"
#define PRIxx32 "#.8I32x"

// holy crap! fighting the `nice' 64-bit only APIs
#ifdef _WIN64
#define PRIxx PRIxx64
#define WORDxx(x) ((DWORD64)(x))
#else
#define PRIxx PRIxx32
#define WORDxx(x) ((DWORD32)(x))
#endif

// does not check for overflow!
STATIC VOID write_report(LPCTSTR fmt, ...)
{
    TCHAR buf[1024];
    va_list argptr;
    int len;
    DWORD written;

    va_start(argptr, fmt);
    len = wvsprintf(buf, fmt, argptr);
    va_end(argptr);

    if (len > 0 && len < 1024) {
        WriteFile(crashReport, buf, len, &written, NULL);
    }
}

STATIC BOOL CALLBACK enum_modules_callback(
    PCSTR ModuleName,
    DWORD64 ModuleBase,
    ULONG ModuleSize,
    PVOID UserContext)
{
    IMAGEHLP_MODULE64 moduleInfo;
    DWORD64 pc = *(DWORD64 *)UserContext;
    BYTE buffer[4096];
    PBYTE data;
    UINT numBytes;
    VS_FIXEDFILEINFO *info;
    char version[64];
    char *symbols, *star;
    int len;
    BOOL ret;

    len = lstrlen(ModuleName);
    if (len >= MAX_PATH) {
        return TRUE;
    }

    if (pGetFileVersionInfoA(ModuleName, 0, sizeof(buffer), buffer) &&
        pVerQueryValueA(buffer, "\\", (LPVOID *)&data, &numBytes) &&
        numBytes >= sizeof(*info)) {
        info = (VS_FIXEDFILEINFO *)data;
        wsprintf(version, "%u.%u.%u.%u",
                 HIWORD(info->dwFileVersionMS),
                 LOWORD(info->dwFileVersionMS),
                 HIWORD(info->dwFileVersionLS),
                 LOWORD(info->dwFileVersionLS));
    } else {
        CopyMemory(version, "unknown", 8);
    }

    moduleInfo.SizeOfStruct = moduleInfoSize;
    ret = pSymGetModuleInfo64(
              processHandle,
              ModuleBase,
              &moduleInfo);
    if (ret) {
        ModuleName = moduleInfo.ModuleName;
        switch (moduleInfo.SymType) {
        case SymNone:       symbols = "none";       break;
        case SymCoff:       symbols = "COFF";       break;
        case SymPdb:        symbols = "PDB";        break;
        case SymExport:     symbols = "export";     break;
        case SymVirtual:    symbols = "virtual";    break;
        default:            symbols = "unknown";    break;
        }
    } else {
        write_report("SymGetModuleInfo64 failed with error %#x\r\n",
                     GetLastError());
        symbols = "failed";
    }

    if (pc >= ModuleBase && pc < ModuleBase + ModuleSize) {
        CopyMemory(faultyModuleName, ModuleName, len + 1);
        star = " *";
    } else {
        star = "";
    }

    write_report(
        "%"PRIxx" %"PRIxx" %s (version %s, symbols %s)%s\r\n",
        WORDxx(ModuleBase), WORDxx(ModuleBase + ModuleSize),
        ModuleName, version, symbols, star);

    return TRUE;
}

#define CRASH_TITLE PRODUCT " Unhandled Exception"

// be careful to avoid using any non-trivial C runtime functions here!
// C runtime structures may be already corrupted and unusable at this point.
LONG WINAPI Sys_ExceptionFilter(LPEXCEPTION_POINTERS exceptionInfo)
{
    STACKFRAME64 stackFrame;
    PEXCEPTION_RECORD exception;
    PCONTEXT context;
    DWORD64 pc;
    SYMBOL_INFO *symbol;
    int count, ret, i;
    DWORD64 offset;
    BYTE buffer[sizeof(SYMBOL_INFO) + 256 - 1];
    IMAGEHLP_MODULE64 moduleInfo;
    char path[MAX_PATH];
    char execdir[MAX_PATH];
    HMODULE moduleHandle;
    SYSTEMTIME systemTime;
    OSVERSIONINFO vinfo;
    DWORD len;
    LONG action;

    // give previous filter a chance to handle this exception
    if (prevExceptionFilter) {
        action = prevExceptionFilter(exceptionInfo);
        if (action != EXCEPTION_CONTINUE_SEARCH) {
            return action;
        }
    }

    // debugger present? not our business
    if (IsDebuggerPresent()) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // called from different thread? not our business
    if (GetCurrentThread() != mainProcessThread) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

#if USE_CLIENT
    //Win_Shutdown();
    VID_Shutdown();
#endif

    ret = MessageBox(NULL,
                     PRODUCT " has encountered an unhandled "
                     "exception and needs to be terminated.\n"
                     "Would you like to generate a crash report?",
                     CRASH_TITLE,
                     MB_ICONERROR | MB_YESNO
#if !USE_CLIENT
                     | MB_SERVICE_NOTIFICATION
#endif
                    );
    if (ret == IDNO) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

#define LL(x)                                   \
    do {                                        \
        moduleHandle = LoadLibrary(x);          \
        if (!moduleHandle) {                    \
            return EXCEPTION_CONTINUE_SEARCH;   \
        }                                       \
    } while(0)

#define GPA(x, y)                                   \
    do {                                            \
        p##y = (x)GetProcAddress(moduleHandle, #y); \
        if (!p##y) {                                \
            return EXCEPTION_CONTINUE_SEARCH;       \
        }                                           \
    } while(0)

    LL("dbghelp.dll");
    GPA(SETSYMOPTIONS, SymSetOptions);
    GPA(SYMGETMODULEINFO64, SymGetModuleInfo64);
    GPA(SYMCLEANUP, SymCleanup);
    GPA(SYMINITIALIZE, SymInitialize);
    GPA(ENUMERATELOADEDMODULES64, EnumerateLoadedModules64);
    GPA(STACKWALK64, StackWalk64);
    GPA(SYMFROMADDR, SymFromAddr);
    GPA(SYMFUNCTIONTABLEACCESS64, SymFunctionTableAccess64);
    GPA(SYMGETMODULEBASE64, SymGetModuleBase64);

    LL("version.dll");
    GPA(GETFILEVERSIONINFOA, GetFileVersionInfoA);
    GPA(VERQUERYVALUEA, VerQueryValueA);

    LL("shell32.dll");
    GPA(SHELLEXECUTEA, ShellExecuteA);

    // get base directory to save crash dump to
    len = GetModuleFileName(NULL, execdir, sizeof(execdir));
    if (!len || len >= sizeof(execdir)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    while (--len) {
        if (execdir[len] == '\\') {
            break;
        }
    }

    if (!len || len + 24 >= MAX_PATH) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    execdir[len] = 0;

    CopyMemory(path, execdir, len);
    CopyMemory(path + len, "\\Q2PRO_CrashReportXX.txt", 25);
    for (i = 0; i < 100; i++) {
        path[len + 18] = '0' + i / 10;
        path[len + 19] = '0' + i % 10;
        crashReport = CreateFile(
                          path,
                          GENERIC_WRITE,
                          FILE_SHARE_READ,
                          NULL,
                          CREATE_NEW,
                          FILE_ATTRIBUTE_NORMAL,
                          NULL);

        if (crashReport != INVALID_HANDLE_VALUE) {
            break;
        }

        if (GetLastError() != ERROR_FILE_EXISTS) {
            MessageBox(NULL,
                       "Couldn't create crash report. "
                       "Base directory is not writable.",
                       CRASH_TITLE,
                       MB_ICONERROR);
            return EXCEPTION_EXECUTE_HANDLER;
        }
    }

    if (i == 100) {
        MessageBox(NULL,
                   "Couldn't create crash report. "
                   "All report slots are full.\n"
                   "Please remove existing reports from base directory.",
                   CRASH_TITLE,
                   MB_ICONERROR);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    pSymSetOptions(
        SYMOPT_LOAD_ANYTHING |
        SYMOPT_DEBUG |
        SYMOPT_FAIL_CRITICAL_ERRORS);

    processHandle = GetCurrentProcess();
    threadHandle = GetCurrentThread();

    pSymInitialize(processHandle, execdir, TRUE);

    GetSystemTime(&systemTime);
    write_report(
        "Crash report generated %s %u %u, %02u:%02u:%02u UTC\r\n",
        monthNames[(systemTime.wMonth - 1) % 12],
        systemTime.wDay,
        systemTime.wYear,
        systemTime.wHour,
        systemTime.wMinute,
        systemTime.wSecond);
    write_report(
        "by " APPLICATION " " VERSION
        ", built " __DATE__", " __TIME__ "\r\n");

    vinfo.dwOSVersionInfoSize = sizeof(vinfo);
    if (GetVersionEx(&vinfo)) {
        write_report(
            "\r\nWindows version: %u.%u (build %u) %s\r\n",
            vinfo.dwMajorVersion,
            vinfo.dwMinorVersion,
            vinfo.dwBuildNumber,
            vinfo.szCSDVersion);
    } else {
        write_report("GetVersionEx failed with error %#x\r\n",
                     GetLastError());
    }

    // oh no, dbghelp is not backwards and forwards compatible
    // why in hell is it different from other windows DLLs?
    for (i = 0; i < 4; i++) {
        len = tryModuleSizes[i];
        if (i && len >= sizeof(moduleInfo)) {
            continue;
        }
        moduleInfo.SizeOfStruct = len;
        if (pSymGetModuleInfo64(
                processHandle,
                (DWORD64)((INT_PTR)hGlobalInstance),
                &moduleInfo)) {
            moduleInfoSize = len;
            if (i) {
                write_report(
                    "Module info size is %u (not %u)\r\n",
                    len, tryModuleSizes[0]);
            }
            break;
        }
    }

    if (i == 4) {
        // bad luck
        write_report("SymGetModuleInfo64 is fucked up :(");
        moduleInfoSize = sizeof(moduleInfo);
    }

    CopyMemory(faultyModuleName, "unknown", 8);

    exception = exceptionInfo->ExceptionRecord;
    context = exceptionInfo->ContextRecord;

#ifdef _WIN64
    pc = context->Rip;
#else
    pc = (DWORD64)context->Eip;
#endif

    write_report("\r\nLoaded modules:\r\n");
    ret = pEnumerateLoadedModules64(
              processHandle,
              enum_modules_callback,
              &pc
          );
    if (!ret) {
        write_report("EnumerateLoadedModules64 failed with error %#x\r\n",
                     GetLastError());
    }

    write_report("\r\nException information:\r\n");
    write_report("Code: %#08x\r\n", exception->ExceptionCode);
    write_report("Address: %"PRIxx" (%s)\r\n", WORDxx(pc), faultyModuleName);

    write_report("\r\nThread context:\r\n");
#ifdef _WIN64
    write_report("RIP: %"PRIxx64" RBP: %"PRIxx64" RSP: %"PRIxx64"\r\n",
                 context->Rip, context->Rbp, context->Rsp);
    write_report("RAX: %"PRIxx64" RBX: %"PRIxx64" RCX: %"PRIxx64"\r\n",
                 context->Rax, context->Rbx, context->Rcx);
    write_report("RDX: %"PRIxx64" RSI: %"PRIxx64" RDI: %"PRIxx64"\r\n",
                 context->Rdx, context->Rsi, context->Rdi);
    write_report("R8 : %"PRIxx64" R9 : %"PRIxx64" R10: %"PRIxx64"\r\n",
                 context->R8, context->R9, context->R10);
    write_report("R11: %"PRIxx64" R12: %"PRIxx64" R13: %"PRIxx64"\r\n",
                 context->R11, context->R12, context->R13);
    write_report("R14: %"PRIxx64" R15: %"PRIxx64"\r\n",
                 context->R14, context->R15);
#else
    write_report("EIP: %"PRIxx32" EBP: %"PRIxx32" ESP: %"PRIxx32"\r\n",
                 context->Eip, context->Ebp, context->Esp);
    write_report("EAX: %"PRIxx32" EBX: %"PRIxx32" ECX: %"PRIxx32"\r\n",
                 context->Eax, context->Ebx, context->Ecx);
    write_report("EDX: %"PRIxx32" ESI: %"PRIxx32" EDI: %"PRIxx32"\r\n",
                 context->Edx, context->Esi, context->Edi);
#endif

    ZeroMemory(&stackFrame, sizeof(stackFrame));
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

    write_report("\r\nStack trace:\r\n");
    count = 0;
    symbol = (SYMBOL_INFO *)buffer;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 256;
    while (pStackWalk64(
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
               NULL)) {
        write_report(
            "%d: %"PRIxx" %"PRIxx" %"PRIxx" %"PRIxx" ",
            count,
            WORDxx(stackFrame.Params[0]),
            WORDxx(stackFrame.Params[1]),
            WORDxx(stackFrame.Params[2]),
            WORDxx(stackFrame.Params[3]));

        moduleInfo.SizeOfStruct = moduleInfoSize;
        if (pSymGetModuleInfo64(
                processHandle,
                stackFrame.AddrPC.Offset,
                &moduleInfo)) {
            if (moduleInfo.SymType != SymNone &&
                moduleInfo.SymType != SymExport &&
                pSymFromAddr(
                    processHandle,
                    stackFrame.AddrPC.Offset,
                    &offset,
                    symbol)) {
                write_report("%s!%s+%"PRIxx32"\r\n",
                             moduleInfo.ModuleName,
                             symbol->Name, (DWORD32)offset);
            } else {
                write_report("%s!%"PRIxx"\r\n",
                             moduleInfo.ModuleName,
                             WORDxx(stackFrame.AddrPC.Offset));
            }
        } else {
            write_report("%"PRIxx"\r\n",
                         WORDxx(stackFrame.AddrPC.Offset));
        }

        count++;
    }

    CloseHandle(crashReport);

    pSymCleanup(processHandle);

    pShellExecuteA(NULL, "open", path, NULL, execdir, SW_SHOW);

    return EXCEPTION_EXECUTE_HANDLER;
}

