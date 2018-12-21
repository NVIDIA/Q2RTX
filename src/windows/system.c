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

#include "client.h"
#include "common/cvar.h"
#include "common/field.h"
#include "common/prompt.h"
#include <mmsystem.h>
#if USE_WINSVC
#include <winsvc.h>
#endif

HINSTANCE                       hGlobalInstance;

#if USE_DBGHELP
HANDLE                          mainProcessThread;
LPTOP_LEVEL_EXCEPTION_FILTER    prevExceptionFilter;
#endif

static char                     currentDirectory[MAX_OSPATH];

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

cvar_t  *sys_basedir;
cvar_t  *sys_libdir;
cvar_t  *sys_homedir;
cvar_t  *sys_forcegamelib;

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

static void write_console_data(void *data, size_t len)
{
    DWORD dummy;

    WriteFile(houtput, data, len, &dummy, NULL);
}

static void hide_console_input(void)
{
    int i;

    if (!sys_hidden) {
        for (i = 0; i <= sys_con.inputLine.cursorPos; i++) {
            write_console_data("\b \b", 3);
        }
    }
    sys_hidden++;
}

static void show_console_input(void)
{
    if (!sys_hidden) {
        return;
    }

    sys_hidden--;
    if (!sys_hidden) {
        write_console_data("]", 1);
        write_console_data(sys_con.inputLine.text, sys_con.inputLine.cursorPos);
    }
}

/*
================
Sys_ConsoleInput
================
*/
void Sys_RunConsole(void)
{
    INPUT_RECORD    recs[MAX_CONSOLE_INPUT_EVENTS];
    int     ch;
    DWORD   numread, numevents;
    int     i;
    inputField_t    *f;
    char    *s;

    if (hinput == INVALID_HANDLE_VALUE) {
        return;
    }

    if (!gotConsole) {
        return;
    }

    f = &sys_con.inputLine;
    while (1) {
        if (!GetNumberOfConsoleInputEvents(hinput, &numevents)) {
            Com_EPrintf("Error %lu getting number of console events.\n", GetLastError());
            gotConsole = qfalse;
            return;
        }

        if (numevents <= 0)
            break;
        if (numevents > MAX_CONSOLE_INPUT_EVENTS) {
            numevents = MAX_CONSOLE_INPUT_EVENTS;
        }

        if (!ReadConsoleInput(hinput, recs, numevents, &numread)) {
            Com_EPrintf("Error %lu reading console input.\n", GetLastError());
            gotConsole = qfalse;
            return;
        }

        for (i = 0; i < numread; i++) {
            if (recs[i].EventType == WINDOW_BUFFER_SIZE_EVENT) {
                // determine terminal width
                size_t width = recs[i].Event.WindowBufferSizeEvent.dwSize.X;

                if (!width) {
                    Com_EPrintf("Invalid console buffer width.\n");
                    gotConsole = qfalse;
                    return;
                }

                sys_con.widthInChars = width;

                // figure out input line width
                width--;
                if (width > MAX_FIELD_TEXT - 1) {
                    width = MAX_FIELD_TEXT - 1;
                }

                hide_console_input();
                IF_Init(&sys_con.inputLine, width, width);
                show_console_input();
                continue;
            }
            if (recs[i].EventType != KEY_EVENT) {
                continue;
            }

            if (!recs[i].Event.KeyEvent.bKeyDown) {
                continue;
            }

            switch (recs[i].Event.KeyEvent.wVirtualKeyCode) {
            case VK_UP:
                hide_console_input();
                Prompt_HistoryUp(&sys_con);
                show_console_input();
                break;
            case VK_DOWN:
                hide_console_input();
                Prompt_HistoryDown(&sys_con);
                show_console_input();
                break;
            case VK_RETURN:
                hide_console_input();
                s = Prompt_Action(&sys_con);
                if (s) {
                    if (*s == '\\' || *s == '/') {
                        s++;
                    }
                    Sys_Printf("]%s\n", s);
                    Cbuf_AddText(&cmd_buffer, s);
                    Cbuf_AddText(&cmd_buffer, "\n");
                } else {
                    write_console_data("\n", 1);
                }
                show_console_input();
                break;
            case VK_BACK:
                if (f->cursorPos) {
                    f->text[--f->cursorPos] = 0;
                    write_console_data("\b \b", 3);
                }
                break;
            case VK_TAB:
                hide_console_input();
                Prompt_CompleteCommand(&sys_con, qfalse);
                f->cursorPos = strlen(f->text);
                show_console_input();
                break;
            default:
                ch = recs[i].Event.KeyEvent.uChar.AsciiChar;
                if (ch < 32) {
                    break;
                }
                if (f->cursorPos < f->maxChars - 1) {
                    write_console_data(&ch, 1);
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
    FOREGROUND_RED | FOREGROUND_GREEN,
    FOREGROUND_BLUE,
    FOREGROUND_BLUE | FOREGROUND_GREEN,
    FOREGROUND_RED | FOREGROUND_BLUE,
    FOREGROUND_WHITE
};

void Sys_SetConsoleColor(color_index_t color)
{
    WORD    attr, w;

    if (houtput == INVALID_HANDLE_VALUE) {
        return;
    }

    if (!gotConsole) {
        return;
    }

    attr = sbinfo.wAttributes & ~FOREGROUND_WHITE;

    switch (color) {
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

    if (color != COLOR_NONE) {
        hide_console_input();
    }
    SetConsoleTextAttribute(houtput, w);
    if (color == COLOR_NONE) {
        show_console_input();
    }
}

static void write_console_output(const char *text)
{
    char    buf[MAXPRINTMSG];
    size_t  len;

    for (len = 0; len < MAXPRINTMSG; len++) {
        int c = *text++;
        if (!c) {
            break;
        }
        buf[len] = Q_charascii(c);
    }

    write_console_data(buf, len);
}

/*
================
Sys_ConsoleOutput

Print text to the dedicated console
================
*/
void Sys_ConsoleOutput(const char *text)
{
    if (houtput == INVALID_HANDLE_VALUE) {
        return;
    }

    if (!gotConsole) {
        write_console_output(text);
    } else {
        hide_console_input();
        write_console_output(text);
        show_console_input();
    }
}

void Sys_SetConsoleTitle(const char *title)
{
    if (gotConsole) {
        SetConsoleTitle(title);
    }
}

static BOOL WINAPI Sys_ConsoleCtrlHandler(DWORD dwCtrlType)
{
    if (errorEntered) {
        exit(1);
    }
    shouldExit = SE_FULL;
    return TRUE;
}

static void Sys_ConsoleInit(void)
{
    DWORD mode;
    size_t width;

#if USE_CLIENT
    if (!AllocConsole()) {
        Com_EPrintf("Couldn't create system console.\n");
        return;
    }
#elif USE_WINSVC
    if (statusHandle) {
        return;
    }
#endif

    hinput = GetStdHandle(STD_INPUT_HANDLE);
    houtput = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleScreenBufferInfo(houtput, &sbinfo)) {
        Com_EPrintf("Couldn't get console buffer info.\n");
        return;
    }

    // determine terminal width
    width = sbinfo.dwSize.X;
    if (!width) {
        Com_EPrintf("Invalid console buffer width.\n");
        return;
    }
    sys_con.widthInChars = width;
    sys_con.printf = Sys_Printf;
    gotConsole = qtrue;

    SetConsoleTitle(PRODUCT " console");
    SetConsoleCtrlHandler(Sys_ConsoleCtrlHandler, TRUE);
    GetConsoleMode(hinput, &mode);
    mode |= ENABLE_WINDOW_INPUT;
    SetConsoleMode(hinput, mode);

    // figure out input line width
    width--;
    if (width > MAX_FIELD_TEXT - 1) {
        width = MAX_FIELD_TEXT - 1;
    }
    IF_Init(&sys_con.inputLine, width, width);

    Com_DPrintf("System console initialized (%d cols, %d rows).\n",
                sbinfo.dwSize.X, sbinfo.dwSize.Y);
}

#endif // USE_SYSCON

/*
===============================================================================

SERVICE CONTROL

===============================================================================
*/

#if USE_WINSVC

static void Sys_InstallService_f(void)
{
    char servicePath[256];
    char serviceName[1024];
    SC_HANDLE scm, service;
    DWORD error, length;
    char *commandline;

    if (Cmd_Argc() < 3) {
        Com_Printf("Usage: %s <servicename> <+command> [...]\n"
                   "Example: %s test +set net_port 27910 +map q2dm1\n",
                   Cmd_Argv(0), Cmd_Argv(0));
        return;
    }

    scm = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) {
            Com_Printf("Insufficient privileges for opening Service Control Manager.\n");
        } else {
            Com_EPrintf("%#lx opening Service Control Manager.\n", error);
        }
        return;
    }

    Q_concat(serviceName, sizeof(serviceName), "Q2PRO - ", Cmd_Argv(1), NULL);

    length = GetModuleFileName(NULL, servicePath, MAX_PATH);
    if (!length) {
        error = GetLastError();
        Com_EPrintf("%#lx getting module file name.\n", error);
        goto fail;
    }
    commandline = Cmd_RawArgsFrom(2);
    if (length + strlen(commandline) + 10 > sizeof(servicePath) - 1) {
        Com_Printf("Oversize service command line.\n");
        goto fail;
    }
    strcpy(servicePath + length, " -service ");
    strcpy(servicePath + length + 10, commandline);

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
                  NULL);

    if (!service) {
        error = GetLastError();
        if (error == ERROR_SERVICE_EXISTS || error == ERROR_DUPLICATE_SERVICE_NAME) {
            Com_Printf("Service already exists.\n");
        } else {
            Com_EPrintf("%#lx creating service.\n", error);
        }
        goto fail;
    }

    Com_Printf("Service created successfully.\n");

    CloseServiceHandle(service);

fail:
    CloseServiceHandle(scm);
}

static void Sys_DeleteService_f(void)
{
    char serviceName[256];
    SC_HANDLE scm, service;
    DWORD error;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <servicename>\n", Cmd_Argv(0));
        return;
    }

    scm = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
    if (!scm) {
        error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) {
            Com_Printf("Insufficient privileges for opening Service Control Manager.\n");
        } else {
            Com_EPrintf("%#lx opening Service Control Manager.\n", error);
        }
        return;
    }

    Q_concat(serviceName, sizeof(serviceName), "Q2PRO - ", Cmd_Argv(1), NULL);

    service = OpenService(
                  scm,
                  serviceName,
                  DELETE);

    if (!service) {
        error = GetLastError();
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
            Com_Printf("Service doesn't exist.\n");
        } else {
            Com_EPrintf("%#lx opening service.\n", error);
        }
        goto fail;
    }

    if (!DeleteService(service)) {
        error = GetLastError();
        if (error == ERROR_SERVICE_MARKED_FOR_DELETE) {
            Com_Printf("Service has already been marked for deletion.\n");
        } else {
            Com_EPrintf("%#lx deleting service.\n", error);
        }
    } else {
        Com_Printf("Service deleted successfully.\n");
    }

    CloseServiceHandle(service);

fail:
    CloseServiceHandle(scm);
}

#endif // USE_WINSVC

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
void Sys_Printf(const char *fmt, ...)
{
    va_list     argptr;
    char        msg[MAXPRINTMSG];

    va_start(argptr, fmt);
    Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    Sys_ConsoleOutput(msg);
}
#endif

/*
================
Sys_Error
================
*/
void Sys_Error(const char *error, ...)
{
    va_list     argptr;
    char        text[MAXERRORMSG];

    va_start(argptr, error);
    Q_vsnprintf(text, sizeof(text), error, argptr);
    va_end(argptr);

    errorEntered = qtrue;

#if USE_CLIENT
    VID_Shutdown();
#endif

#if USE_SYSCON
    Sys_SetConsoleColor(COLOR_RED);
    Sys_Printf("********************\n"
               "FATAL: %s\n"
               "********************\n", text);
    Sys_SetConsoleColor(COLOR_NONE);
#endif

#if USE_WINSVC
    if (!statusHandle)
#endif
    {
#if USE_SYSCON
        if (gotConsole) {
            Sleep(INFINITE);
        }
#endif
        MessageBoxA(NULL, text, PRODUCT " Fatal Error", MB_ICONERROR | MB_OK);
    }

    exit(1);
}

/*
================
Sys_Quit

This function never returns.
================
*/
void Sys_Quit(void)
{
    timeEndPeriod(1);

#if USE_CLIENT
#if USE_SYSCON
    if (dedicated && dedicated->integer) {
        FreeConsole();
    }
#endif
#elif USE_WINSVC
    if (statusHandle && !shouldExit) {
        shouldExit = SE_YES;
        Com_AbortFrame();
    }
#endif

    exit(0);
}

void Sys_DebugBreak(void)
{
    DebugBreak();
}

unsigned Sys_Milliseconds(void)
{
    return timeGetTime();
}

void Sys_AddDefaultConfig(void)
{
}

void Sys_Sleep(int msec)
{
    Sleep(msec);
}

/*
================
Sys_Init
================
*/
void Sys_Init(void)
{
    OSVERSIONINFO vinfo;
#ifndef _WIN64
    HMODULE module;
    BOOL (WINAPI * pSetProcessDEPPolicy)(DWORD);
#endif
    cvar_t *var = NULL;

    timeBeginPeriod(1);

    // check windows version
    vinfo.dwOSVersionInfoSize = sizeof(vinfo);
    if (!GetVersionEx(&vinfo)) {
        Sys_Error("Couldn't get OS info");
    }
    if (vinfo.dwPlatformId != VER_PLATFORM_WIN32_NT) {
        Sys_Error(PRODUCT " requires Windows NT");
    }
    if (vinfo.dwMajorVersion < 5) {
        Sys_Error(PRODUCT " requires Windows 2000 or greater");
    }

    // basedir <path>
    // allows the game to run from outside the data tree
    sys_basedir = Cvar_Get("basedir", currentDirectory, CVAR_NOSET);
    sys_libdir = Cvar_Get("libdir", currentDirectory, CVAR_NOSET);

    // homedir <path>
    // specifies per-user writable directory for demos, screenshots, etc
    sys_homedir = Cvar_Get("homedir", "", CVAR_NOSET);

    sys_forcegamelib = Cvar_Get("sys_forcegamelib", "", CVAR_NOSET);

#if USE_WINSVC
    Cmd_AddCommand("installservice", Sys_InstallService_f);
    Cmd_AddCommand("deleteservice", Sys_DeleteService_f);
#endif

#if USE_SYSCON
    houtput = GetStdHandle(STD_OUTPUT_HANDLE);
#if USE_CLIENT
    sys_viewlog = Cvar_Get("sys_viewlog", "0", CVAR_NOSET);

    if (dedicated->integer || sys_viewlog->integer)
#endif
        Sys_ConsoleInit();
#endif // USE_SYSCON

#if USE_DBGHELP
    var = Cvar_Get("sys_disablecrashdump", "0", CVAR_NOSET);

    // install our exception filter
    if (!var->integer) {
        mainProcessThread = GetCurrentThread();
        prevExceptionFilter = SetUnhandledExceptionFilter(
                                  Sys_ExceptionFilter);
    }
#endif

#ifndef _WIN64
    module = GetModuleHandle("kernel32.dll");
    if (module) {
        pSetProcessDEPPolicy = (PVOID)GetProcAddress(module,
                                                     "SetProcessDEPPolicy");
        if (pSetProcessDEPPolicy) {
            var = Cvar_Get("sys_disabledep", "0", CVAR_NOSET);

            // opt-in or opt-out for DEP
            if (!var->integer) {
                pSetProcessDEPPolicy(
                    PROCESS_DEP_ENABLE |
                    PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION);
            } else if (var->integer == 2) {
                pSetProcessDEPPolicy(0);
            }
        }
    }
#endif
}

/*
========================================================================

DLL LOADING

========================================================================
*/

void Sys_FreeLibrary(void *handle)
{
    if (handle && !FreeLibrary(handle)) {
        Com_Error(ERR_FATAL, "FreeLibrary failed on %p", handle);
    }
}

void *Sys_LoadLibrary(const char *path, const char *sym, void **handle)
{
    HMODULE module;
    void    *entry;

    *handle = NULL;

    module = LoadLibraryA(path);
    if (!module) {
        Com_SetLastError(va("%s: LoadLibrary failed with error %lu",
                            path, GetLastError()));
        return NULL;
    }

    if (sym) {
        entry = GetProcAddress(module, sym);
        if (!entry) {
            Com_SetLastError(va("%s: GetProcAddress(%s) failed with error %lu",
                                path, sym, GetLastError()));
            FreeLibrary(module);
            return NULL;
        }
    } else {
        entry = NULL;
    }

    *handle = module;
    return entry;
}

void *Sys_GetProcAddress(void *handle, const char *sym)
{
    void    *entry;

    entry = GetProcAddress(handle, sym);
    if (!entry)
        Com_SetLastError(va("GetProcAddress(%s) failed with error %lu",
                            sym, GetLastError()));

    return entry;
}

/*
========================================================================

FILESYSTEM

========================================================================
*/

static inline time_t file_time_to_unix(FILETIME *f)
{
    ULARGE_INTEGER u = *(ULARGE_INTEGER *)f;
    return (time_t)((u.QuadPart - 116444736000000000ULL) / 10000000);
}

static void *copy_info(const char *name, const LPWIN32_FIND_DATAA data)
{
    time_t ctime = file_time_to_unix(&data->ftCreationTime);
    time_t mtime = file_time_to_unix(&data->ftLastWriteTime);

    return FS_CopyInfo(name, data->nFileSizeLow, ctime, mtime);
}

/*
=================
Sys_ListFiles_r

Internal function to filesystem. Conventions apply:
    - files should hold at least MAX_LISTED_FILES
    - *count_p must be initialized in range [0, MAX_LISTED_FILES - 1]
    - depth must be 0 on the first call
=================
*/
void Sys_ListFiles_r(const char  *path,
                     const char  *filter,
                     unsigned    flags,
                     size_t      baselen,
                     int         *count_p,
                     void        **files,
                     int         depth)
{
    WIN32_FIND_DATAA    data;
    HANDLE      handle;
    char        fullpath[MAX_OSPATH], *name;
    size_t      pathlen, len;
    unsigned    mask;
    void        *info;

    // optimize single extension search
    if (!(flags & FS_SEARCH_BYFILTER) &&
        filter && !strchr(filter, ';')) {
        if (*filter == '.') {
            filter++;
        }
        len = Q_concat(fullpath, sizeof(fullpath),
                       path, "\\*.", filter, NULL);
        filter = NULL; // do not check it later
    } else {
        len = Q_concat(fullpath, sizeof(fullpath),
                       path, "\\*", NULL);
    }

    if (len >= sizeof(fullpath)) {
        return;
    }

    // format path to windows style
    // done on the first run only
    if (!depth) {
        FS_ReplaceSeparators(fullpath, '\\');
    }

    handle = FindFirstFileA(fullpath, &data);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }

    // make it point right after the slash
    pathlen = strlen(path) + 1;

    do {
        if (!strcmp(data.cFileName, ".") ||
            !strcmp(data.cFileName, "..")) {
            continue; // ignore special entries
        }

        // construct full path
        len = strlen(data.cFileName);
        if (pathlen + len >= sizeof(fullpath)) {
            continue;
        }

        memcpy(fullpath + pathlen, data.cFileName, len + 1);

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            mask = FS_SEARCH_DIRSONLY;
        } else {
            mask = 0;
        }

        // pattern search implies recursive search
        if ((flags & FS_SEARCH_BYFILTER) && mask &&
            depth < MAX_LISTED_DEPTH) {
            Sys_ListFiles_r(fullpath, filter, flags, baselen,
                            count_p, files, depth + 1);

            // re-check count
            if (*count_p >= MAX_LISTED_FILES) {
                break;
            }
        }

        // check type
        if ((flags & FS_SEARCH_DIRSONLY) != mask) {
            continue;
        }

        // check filter
        if (filter) {
            if (flags & FS_SEARCH_BYFILTER) {
                if (!FS_WildCmp(filter, fullpath + baselen)) {
                    continue;
                }
            } else {
                if (!FS_ExtCmp(filter, data.cFileName)) {
                    continue;
                }
            }
        }

        // strip path
        if (flags & FS_SEARCH_SAVEPATH) {
            name = fullpath + baselen;
        } else {
            name = data.cFileName;
        }

        // reformat it back to quake filesystem style
        FS_ReplaceSeparators(name, '/');

        // strip extension
        if (flags & FS_SEARCH_STRIPEXT) {
            *COM_FileExtension(name) = 0;

            if (!*name) {
                continue;
            }
        }

        // copy info off
        if (flags & FS_SEARCH_EXTRAINFO) {
            info = copy_info(name, &data);
        } else {
            info = FS_CopyString(name);
        }

        files[(*count_p)++] = info;
    } while (*count_p < MAX_LISTED_FILES &&
             FindNextFileA(handle, &data) != FALSE);

    FindClose(handle);
}

/*
========================================================================

MAIN

========================================================================
*/

static BOOL fix_current_directory(void)
{
    char *p;

    if (!GetModuleFileNameA(NULL, currentDirectory, sizeof(currentDirectory) - 1)) {
        return FALSE;
    }

    if ((p = strrchr(currentDirectory, '\\')) != NULL) {
        *p = 0;
    }

#ifndef UNDER_CE
    if (!SetCurrentDirectoryA(currentDirectory)) {
        return FALSE;
    }
#endif

    return TRUE;
}

#if (_MSC_VER >= 1400)
static void msvcrt_sucks(const wchar_t *expr, const wchar_t *func,
                         const wchar_t *file, unsigned int line, uintptr_t unused)
{
}
#endif

static int Sys_Main(int argc, char **argv)
{
    // fix current directory to point to the basedir
    if (!fix_current_directory()) {
        return 1;
    }

#if (_MSC_VER >= 1400)
    // work around strftime given invalid format string
    // killing the whole fucking process :((
    _set_invalid_parameter_handler(msvcrt_sucks);
#endif

    Qcommon_Init(argc, argv);

    // main program loop
    while (1) {
        Qcommon_Frame();
        if (shouldExit) {
#if USE_WINSVC
            if (shouldExit == SE_FULL)
#endif
                Com_Quit(NULL, ERR_DISCONNECT);
            break;
        }
    }

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
static void Sys_ParseCommandLine(char *line)
{
    sys_argc = 1;
    sys_argv[0] = APPLICATION;
    while (*line) {
        while (*line && *line <= 32) {
            line++;
        }
        if (*line == 0) {
            break;
        }
        sys_argv[sys_argc++] = line;
        while (*line > 32) {
            line++;
        }
        if (*line == 0) {
            break;
        }
        *line = 0;
        if (sys_argc == MAX_LINE_TOKENS) {
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
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
    // previous instances do not exist in Win32
    if (hPrevInstance) {
        return 1;
    }

    hGlobalInstance = hInstance;
#ifndef UNICODE
    // TODO: wince support
    Sys_ParseCommandLine(lpCmdLine);
#endif
    return Sys_Main(sys_argc, sys_argv);
}

#else // USE_CLIENT

#if USE_WINSVC

static char     **sys_argv;
static int      sys_argc;

static VOID WINAPI ServiceHandler(DWORD fdwControl)
{
    if (fdwControl == SERVICE_CONTROL_STOP) {
        shouldExit = SE_FULL;
    }
}

static VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv)
{
    SERVICE_STATUS    status;

    statusHandle = RegisterServiceCtrlHandler(APPLICATION, ServiceHandler);
    if (!statusHandle) {
        return;
    }

    memset(&status, 0, sizeof(status));
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = SERVICE_RUNNING;
    status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    SetServiceStatus(statusHandle, &status);

    Sys_Main(sys_argc, sys_argv);

    status.dwCurrentState = SERVICE_STOPPED;
    status.dwControlsAccepted = 0;
    SetServiceStatus(statusHandle, &status);
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
int main(int argc, char **argv)
{
#if USE_WINSVC
    int i;
#endif

    hGlobalInstance = GetModuleHandle(NULL);

#if USE_WINSVC
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-service")) {
            argv[i] = NULL;
            sys_argc = argc;
            sys_argv = argv;
            if (StartServiceCtrlDispatcher(serviceTable)) {
                return 0;
            }
            if (GetLastError() == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
                break; // fall back to normal server startup
            }
            return 1;
        }
    }
#endif

    return Sys_Main(argc, argv);
}

#endif // !USE_CLIENT

