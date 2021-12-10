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
#if USE_WINSVC
#include <winsvc.h>
#endif
#include <versionhelpers.h>

HINSTANCE                       hGlobalInstance;

#if USE_DBGHELP
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
static volatile bool            errorEntered;

static LARGE_INTEGER            timer_freq;

cvar_t  *sys_basedir;
cvar_t  *sys_libdir;
cvar_t  *sys_homedir;
cvar_t  *sys_forcegamelib;

// Enable Windows visual styles for message boxes
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

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
static bool             gotConsole;

static void write_console_data(void *data, size_t len)
{
    DWORD res;
    WriteFile(houtput, data, len, &res, NULL);
}

static void hide_console_input(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;

    if (!sys_hidden && GetConsoleScreenBufferInfo(houtput, &info)) {
        size_t len = strlen(sys_con.inputLine.text);
        COORD pos = { 0, info.dwCursorPosition.Y };
        DWORD res = min(len + 1, info.dwSize.X);
        FillConsoleOutputCharacter(houtput, ' ', res, pos, &res);
        SetConsoleCursorPosition(houtput, pos);
    }
    sys_hidden++;
}

static void show_console_input(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;

    if (sys_hidden && !--sys_hidden && GetConsoleScreenBufferInfo(houtput, &info)) {
        inputField_t *f = &sys_con.inputLine;
        size_t pos = f->cursorPos;
        char *text = f->text;

        // update line width after resize
        f->visibleChars = info.dwSize.X - 1;

        // scroll horizontally
        if (pos >= f->visibleChars) {
            pos = f->visibleChars - 1;
            text += f->cursorPos - pos;
        }

        size_t len = strlen(text);
        DWORD res = min(len, f->visibleChars) + 1;
        WriteConsoleOutputCharacter(houtput, va("]%s", text), res, (COORD){ 0, info.dwCursorPosition.Y }, &res);
        SetConsoleCursorPosition(houtput, (COORD){ pos + 1, info.dwCursorPosition.Y });
    }
}

static void console_delete(inputField_t *f)
{
    if (f->text[f->cursorPos]) {
        hide_console_input();
        memmove(f->text + f->cursorPos, f->text + f->cursorPos + 1, sizeof(f->text) - f->cursorPos - 1);
        show_console_input();
    }
}

static void console_move_cursor(inputField_t *f, size_t pos)
{
    size_t oldpos = f->cursorPos;
    f->cursorPos = pos = min(pos, f->maxChars - 1);

    if (oldpos < f->visibleChars && pos < f->visibleChars) {
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(houtput, &info)) {
            SetConsoleCursorPosition(houtput, (COORD){ pos + 1, info.dwCursorPosition.Y });
        }
    } else {
        hide_console_input();
        show_console_input();
    }
}

static void console_move_right(inputField_t *f)
{
    if (f->text[f->cursorPos] && f->cursorPos < f->maxChars - 1) {
        console_move_cursor(f, f->cursorPos + 1);
    }
}

static void console_move_left(inputField_t *f)
{
    if (f->cursorPos > 0) {
        console_move_cursor(f, f->cursorPos - 1);
    }
}

static void console_replace_char(inputField_t *f, int ch)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(houtput, &info)) {
        DWORD res;
        FillConsoleOutputCharacter(houtput, ch, 1, info.dwCursorPosition, &res);
    }
}

static void scroll_console_window(int key)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(houtput, &info)) {
        int lo = -info.srWindow.Top;
        int hi = max(info.dwCursorPosition.Y - info.srWindow.Bottom, 0);
        int page = info.srWindow.Bottom - info.srWindow.Top + 1;
        int rows = 0;
        switch (key) {
            case VK_HOME: rows = lo; break;
            case VK_END:  rows = hi; break;
            case VK_PRIOR: rows = max(-page, lo); break;
            case VK_NEXT:  rows = min( page, hi); break;
        }
        if (rows)
            SetConsoleWindowInfo(houtput, FALSE, &(SMALL_RECT){ .Top = rows, .Bottom = rows });
    }
}

static void clear_console_window(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;

    if (sys_hidden)
        scroll_console_window(VK_END);

    hide_console_input();
    if (GetConsoleScreenBufferInfo(houtput, &info)) {
        COORD pos = { 0, info.srWindow.Top };
        DWORD res = (info.srWindow.Bottom - info.srWindow.Top) * info.dwSize.X;
        FillConsoleOutputCharacter(houtput, ' ', res, pos, &res);
        SetConsoleCursorPosition(houtput, pos);
    }
    show_console_input();
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
    inputField_t    *f = &sys_con.inputLine;
    char    *s;

    if (hinput == INVALID_HANDLE_VALUE) {
        return;
    }

    if (!gotConsole) {
        return;
    }

    while (1) {
        if (!GetNumberOfConsoleInputEvents(hinput, &numevents)) {
            Com_EPrintf("Error %lu getting number of console events.\n", GetLastError());
            gotConsole = false;
            return;
        }

        if (numevents < 1)
            break;
        if (numevents > MAX_CONSOLE_INPUT_EVENTS) {
            numevents = MAX_CONSOLE_INPUT_EVENTS;
        }

        if (!ReadConsoleInput(hinput, recs, numevents, &numread)) {
            Com_EPrintf("Error %lu reading console input.\n", GetLastError());
            gotConsole = false;
            return;
        }

        for (i = 0; i < numread; i++) {
            if (recs[i].EventType == WINDOW_BUFFER_SIZE_EVENT) {
                // determine terminal width
                COORD size = recs[i].Event.WindowBufferSizeEvent.dwSize;
                WORD width = size.X;

                if (width < 2) {
                    Com_EPrintf("Invalid console buffer width.\n");
                    continue;
                }

                // figure out input line width
                if (width != sys_con.widthInChars) {
                    sys_con.widthInChars = width;
                    sys_con.inputLine.visibleChars = 0; // force refresh
                }

                Com_DPrintf("System console resized (%d cols, %d rows).\n", size.X, size.Y);
                continue;
            }
            if (recs[i].EventType != KEY_EVENT) {
                continue;
            }

            if (!recs[i].Event.KeyEvent.bKeyDown) {
                continue;
            }

            WORD key = recs[i].Event.KeyEvent.wVirtualKeyCode;
            DWORD mod = recs[i].Event.KeyEvent.dwControlKeyState;
            size_t pos;

            if (mod & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) {
                switch (key) {
                case 'A':
                    console_move_cursor(f, 0);
                    break;
                case 'E':
                    console_move_cursor(f, strlen(f->text));
                    break;

                case 'B':
                    console_move_left(f);
                    break;
                case 'F':
                    console_move_right(f);
                    break;

                case 'C':
                    GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
                    break;

                case 'D':
                    console_delete(f);
                    break;

                case 'W':
                    pos = f->cursorPos;
                    while (pos > 0 && f->text[pos - 1] <= ' ') {
                        pos--;
                    }
                    while (pos > 0 && f->text[pos - 1] > ' ') {
                        pos--;
                    }
                    if (pos < f->cursorPos) {
                        hide_console_input();
                        memmove(f->text + pos, f->text + f->cursorPos, sizeof(f->text) - f->cursorPos);
                        f->cursorPos = pos;
                        show_console_input();
                    }
                    break;

                case 'U':
                    if (f->cursorPos > 0) {
                        hide_console_input();
                        memmove(f->text, f->text + f->cursorPos, sizeof(f->text) - f->cursorPos);
                        f->cursorPos = 0;
                        show_console_input();
                    }
                    break;

                case 'K':
                    if (f->text[f->cursorPos]) {
                        hide_console_input();
                        f->text[f->cursorPos] = 0;
                        show_console_input();
                    }
                    break;

                case 'L':
                    clear_console_window();
                    break;

                case 'N':
                    hide_console_input();
                    Prompt_HistoryDown(&sys_con);
                    show_console_input();
                    break;

                case 'P':
                    hide_console_input();
                    Prompt_HistoryUp(&sys_con);
                    show_console_input();
                    break;

                case 'R':
                    hide_console_input();
                    Prompt_CompleteHistory(&sys_con, false);
                    show_console_input();
                    break;

                case 'S':
                    hide_console_input();
                    Prompt_CompleteHistory(&sys_con, true);
                    show_console_input();
                    break;

                case VK_HOME:
                case VK_END:
                    scroll_console_window(key);
                    break;
                }
                continue;
            }

            if (mod & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) {
                switch (key) {
                case 'B':
                    pos = f->cursorPos;
                    while (pos > 0 && f->text[pos - 1] <= ' ') {
                        pos--;
                    }
                    while (pos > 0 && f->text[pos - 1] > ' ') {
                        pos--;
                    }
                    console_move_cursor(f, pos);
                    break;

                case 'F':
                    pos = f->cursorPos;
                    while (f->text[pos] && f->text[pos] <= ' ') {
                        pos++;
                    }
                    while (f->text[pos] > ' ') {
                        pos++;
                    }
                    console_move_cursor(f, pos);
                    break;
                }
                continue;
            }

            switch (key) {
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

            case VK_PRIOR:
            case VK_NEXT:
                scroll_console_window(key);
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
                    write_console_data("]\n", 2);
                }
                show_console_input();
                break;

            case VK_BACK:
                if (f->cursorPos > 0) {
                    if (f->text[f->cursorPos] == 0 && f->cursorPos < f->visibleChars) {
                        f->text[--f->cursorPos] = 0;
                        write_console_data("\b \b", 3);
                    } else {
                        hide_console_input();
                        memmove(f->text + f->cursorPos - 1, f->text + f->cursorPos, sizeof(f->text) - f->cursorPos);
                        f->cursorPos--;
                        show_console_input();
                    }
                }
                break;

            case VK_DELETE:
                console_delete(f);
                break;

            case VK_END:
                console_move_cursor(f, strlen(f->text));
                break;
            case VK_HOME:
                console_move_cursor(f, 0);
                break;

            case VK_RIGHT:
                console_move_right(f);
                break;
            case VK_LEFT:
                console_move_left(f);
                break;

            case VK_TAB:
                hide_console_input();
                Prompt_CompleteCommand(&sys_con, false);
                show_console_input();
                break;

            default:
                ch = recs[i].Event.KeyEvent.uChar.AsciiChar;
                if (ch < 32) {
                    break;
                }
                if (f->cursorPos == f->maxChars - 1) {
                    // buffer limit reached, replace the character under
                    // cursor. replace without moving cursor to prevent
                    // newline when cursor is at the rightmost column.
                    console_replace_char(f, ch);
                    f->text[f->cursorPos + 0] = ch;
                    f->text[f->cursorPos + 1] = 0;
                } else if (f->text[f->cursorPos] == 0 && f->cursorPos + 1 < f->visibleChars) {
                    write_console_data(va("%c", ch), 1);
                    f->text[f->cursorPos + 0] = ch;
                    f->text[f->cursorPos + 1] = 0;
                    f->cursorPos++;
                } else {
                    hide_console_input();
                    memmove(f->text + f->cursorPos + 1, f->text + f->cursorPos, sizeof(f->text) - f->cursorPos - 1);
                    f->text[f->cursorPos++] = ch;
                    f->text[f->maxChars] = 0;
                    show_console_input();
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
    CONSOLE_SCREEN_BUFFER_INFO info;
    WORD    attr, w;

    if (houtput == INVALID_HANDLE_VALUE) {
        return;
    }

    if (!gotConsole) {
        return;
    }

    if (!GetConsoleScreenBufferInfo(houtput, &info)) {
        return;
    }

    attr = info.wAttributes & ~FOREGROUND_WHITE;

    switch (color) {
    case COLOR_NONE:
        w = attr | FOREGROUND_WHITE;
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

    if (!*text) {
        return;
    }

    if (!gotConsole) {
        write_console_output(text);
    } else {
        static bool hack = false;

        if (!hack) {
            hide_console_input();
            hack = true;
        }

        write_console_output(text);

        if (text[strlen(text) - 1] == '\n') {
            show_console_input();
            hack = false;
        }
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
    CONSOLE_SCREEN_BUFFER_INFO info;
    DWORD mode;
    WORD width;

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
    if (!GetConsoleScreenBufferInfo(houtput, &info)) {
        Com_EPrintf("Couldn't get console buffer info.\n");
        return;
    }

    // determine terminal width
    width = info.dwSize.X;
    if (width < 2) {
        Com_EPrintf("Invalid console buffer width.\n");
        return;
    }

    if (!GetConsoleMode(hinput, &mode)) {
        Com_EPrintf("Couldn't get console input mode.\n");
        return;
    }

    mode &= ~(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    mode |= ENABLE_WINDOW_INPUT;
    if (!SetConsoleMode(hinput, mode)) {
        Com_EPrintf("Couldn't set console input mode.\n");
        return;
    }

    SetConsoleTitle(PRODUCT " console");
    SetConsoleCtrlHandler(Sys_ConsoleCtrlHandler, TRUE);

    sys_con.widthInChars = width;
    sys_con.printf = Sys_Printf;
    gotConsole = true;

    // figure out input line width
    IF_Init(&sys_con.inputLine, width - 1, MAX_FIELD_TEXT - 1);

    Com_DPrintf("System console initialized (%d cols, %d rows).\n",
                info.dwSize.X, info.dwSize.Y);
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

    Q_concat(serviceName, sizeof(serviceName), "Q2PRO - ", Cmd_Argv(1));

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

    Q_concat(serviceName, sizeof(serviceName), "Q2PRO - ", Cmd_Argv(1));

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

ASYNC WORK QUEUE

===============================================================================
*/

#if USE_CLIENT

static bool work_initialized;
static bool work_terminate;
static CRITICAL_SECTION work_crit;
static HANDLE work_event;
static HANDLE work_thread;
static asyncwork_t *pend_head;
static asyncwork_t *done_head;

static void append_work(asyncwork_t **head, asyncwork_t *work)
{
    asyncwork_t *c, **p;
    for (p = head, c = *head; c; p = &c->next, c = c->next);
    work->next = NULL;
    *p = work;
}

static void complete_work(void)
{
    asyncwork_t *work, *next;

    if (!work_initialized)
        return;
    if (!TryEnterCriticalSection(&work_crit))
        return;
    if (q_unlikely(done_head)) {
        for (work = done_head; work; work = next) {
            next = work->next;
            if (work->done_cb)
                work->done_cb(work->cb_arg);
            Z_Free(work);
        }
        done_head = NULL;
    }
    LeaveCriticalSection(&work_crit);
}

static DWORD WINAPI thread_func(LPVOID arg)
{
    EnterCriticalSection(&work_crit);
    while (1) {
        while (!pend_head && !work_terminate) {
            LeaveCriticalSection(&work_crit);
            if (WaitForSingleObject(work_event, INFINITE))
                return 1;
            EnterCriticalSection(&work_crit);
        }

        asyncwork_t *work = pend_head;
        if (!work)
            break;
        pend_head = work->next;

        LeaveCriticalSection(&work_crit);
        work->work_cb(work->cb_arg);
        EnterCriticalSection(&work_crit);

        append_work(&done_head, work);
    }
    LeaveCriticalSection(&work_crit);

    return 0;
}

static void shutdown_work(void)
{
    if (!work_initialized)
        return;

    EnterCriticalSection(&work_crit);
    work_terminate = true;
    LeaveCriticalSection(&work_crit);

    SetEvent(work_event);

    WaitForSingleObject(work_thread, INFINITE);
    complete_work();

    DeleteCriticalSection(&work_crit);
    CloseHandle(work_event);
    CloseHandle(work_thread);
    work_initialized = false;
}

void Sys_QueueAsyncWork(asyncwork_t *work)
{
    if (!work_initialized) {
        InitializeCriticalSection(&work_crit);
        work_event = CreateEvent(NULL, FALSE, FALSE, NULL);
        if (!work_event)
            Sys_Error("Couldn't create async work event");
        work_thread = CreateThread(NULL, 0, thread_func, NULL, 0, NULL);
        if (!work_thread)
            Sys_Error("Couldn't create async work thread");
        work_initialized = true;
    }

    EnterCriticalSection(&work_crit);
    append_work(&pend_head, Z_CopyStruct(work));
    LeaveCriticalSection(&work_crit);

    SetEvent(work_event);
}

#else
#define shutdown_work() (void)0
#define complete_work() (void)0
#endif

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

    errorEntered = true;

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
            hide_console_input();
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
    shutdown_work();

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
    LARGE_INTEGER tm;
    QueryPerformanceCounter(&tm);
    return tm.QuadPart * 1000ULL / timer_freq.QuadPart;
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
#ifndef _WIN64
    HMODULE module;
    BOOL (WINAPI * pSetProcessDEPPolicy)(DWORD);
#endif
    cvar_t *var q_unused;

    // check windows version
    if (!IsWindowsXPOrGreater())
        Sys_Error(PRODUCT " requires Windows XP or greater");

    if (!QueryPerformanceFrequency(&timer_freq))
        Sys_Error("QueryPerformanceFrequency failed");

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
    return (u.QuadPart - 116444736000000000ULL) / 10000000;
}

static void *copy_info(const char *name, const LPWIN32_FIND_DATAA data)
{
    int64_t size = data->nFileSizeLow | (uint64_t)data->nFileSizeHigh << 32;
    time_t ctime = file_time_to_unix(&data->ftCreationTime);
    time_t mtime = file_time_to_unix(&data->ftLastWriteTime);

    return FS_CopyInfo(name, size, ctime, mtime);
}

/*
=================
Sys_ListFiles_r
=================
*/
void Sys_ListFiles_r(listfiles_t *list, const char *path, int depth)
{
    WIN32_FIND_DATAA    data;
    HANDLE      handle;
    char        fullpath[MAX_OSPATH], *name;
    size_t      pathlen, len;
    unsigned    mask;
    void        *info;
    const char  *filter = list->filter;

    // optimize single extension search
    if (!(list->flags & FS_SEARCH_BYFILTER) &&
        filter && !strchr(filter, ';')) {
        if (*filter == '.') {
            filter++;
        }
        len = Q_concat(fullpath, sizeof(fullpath), path, "\\*.", filter);
        filter = NULL; // do not check it later
    } else {
        len = Q_concat(fullpath, sizeof(fullpath), path, "\\*");
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
        if ((list->flags & FS_SEARCH_BYFILTER) && mask &&
            depth < MAX_LISTED_DEPTH) {
            Sys_ListFiles_r(list, fullpath, depth + 1);

            // re-check count
            if (list->count >= MAX_LISTED_FILES) {
                break;
            }
        }

        // check type
        if ((list->flags & FS_SEARCH_DIRSONLY) != mask) {
            continue;
        }

        // check filter
        if (filter) {
            if (list->flags & FS_SEARCH_BYFILTER) {
                if (!FS_WildCmp(filter, fullpath + list->baselen)) {
                    continue;
                }
            } else {
                if (!FS_ExtCmp(filter, data.cFileName)) {
                    continue;
                }
            }
        }

        // strip path
        if (list->flags & FS_SEARCH_SAVEPATH) {
            name = fullpath + list->baselen;
        } else {
            name = data.cFileName;
        }

        // reformat it back to quake filesystem style
        FS_ReplaceSeparators(name, '/');

        // strip extension
        if (list->flags & FS_SEARCH_STRIPEXT) {
            *COM_FileExtension(name) = 0;

            if (!*name) {
                continue;
            }
        }

        // copy info off
        if (list->flags & FS_SEARCH_EXTRAINFO) {
            info = copy_info(name, &data);
        } else {
            info = FS_CopyString(name);
        }

        list->files = FS_ReallocList(list->files, list->count + 1);
        list->files[list->count++] = info;
    } while (list->count < MAX_LISTED_FILES &&
             FindNextFileA(handle, &data) != FALSE);

    FindClose(handle);
}

bool Sys_IsDir(const char *path)
{
	WCHAR wpath[MAX_OSPATH] = { 0 };
	MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_OSPATH);

	DWORD fileAttributes = GetFileAttributesW(wpath);
	if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
		return false;
	}

	return (fileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool Sys_IsFile(const char *path)
{
	WCHAR wpath[MAX_OSPATH] = { 0 };
	MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_OSPATH);

	DWORD fileAttributes = GetFileAttributesW(wpath);
	if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
		return false;
	}

	// I guess the assumption that if it's not a file or device
	// then it's a directory is good enough for us?
	return (fileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) == 0;
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
        complete_work();
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

