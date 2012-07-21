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
#include <dirent.h>
#include <dlfcn.h>
#include <termios.h>
#include <sys/ioctl.h>
#ifndef __linux__
#include <machine/param.h>
#endif

#include "common.h"
#include "q_list.h"
#include "q_field.h"
#include "q_fifo.h"
#include "prompt.h"
#include "files.h"
#if USE_REF
#include "vid_public.h"
#endif
#include "sys_public.h"
#include "net_common.h"

cvar_t  *sys_basedir;
cvar_t  *sys_libdir;
cvar_t  *sys_homedir;
cvar_t  *sys_forcegamelib;

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

static int stdout_sleep(void)
{
    struct timeval tv;
    fd_set fd;

    tv.tv_sec = 0;
    tv.tv_usec = 10000;

    FD_ZERO(&fd);
    FD_SET(STDOUT_FILENO, &fd);

    return select(STDOUT_FILENO + 1, NULL, &fd, NULL, &tv);
}

// handles partial writes correctly, but never spins too much
// blocks for 100 ms before giving up and losing data
static void stdout_write(const void *buf, size_t len)
{
    const char *what;
    int ret, spins;

    for (spins = 0; spins < 10; spins++) {
        if (len == 0)
            break;

        ret = write(STDOUT_FILENO, buf, len);
        if (q_unlikely(ret < 0)) {
            if (errno == EINTR)
                continue;

            if (errno == EAGAIN) {
                ret = stdout_sleep();
                if (ret >= 0)
                    continue;
                what = "select";
            } else {
                what = "write";
            }

            // avoid recursive calls
            sys_console = NULL;
            tty_enabled = qfalse;

            Com_Error(ERR_FATAL, "%s: %s() failed: %s",
                      __func__, what, strerror(errno));
        }

        buf += ret;
        len -= ret;
    }
}

static void tty_hide_input(void)
{
    int i;

    if (!tty_hidden) {
        for (i = 0; i <= tty_prompt.inputLine.cursorPos; i++) {
            stdout_write("\b \b", 3);
        }
    }
    tty_hidden++;
}

static void tty_show_input(void)
{
    if (!tty_hidden) {
        return;
    }

    tty_hidden--;
    if (!tty_hidden) {
        stdout_write("]", 1);
        stdout_write(tty_prompt.inputLine.text,
                     tty_prompt.inputLine.cursorPos);
    }
}

static void tty_init_input(void)
{
    struct termios tty;
#ifdef TIOCGWINSZ
    struct winsize ws;
#endif
    int width;

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        Com_Printf("stdin/stdout don't both refer to a TTY\n");
        Cvar_Set("sys_console", "1");
        return;
    }

    tcgetattr(STDIN_FILENO, &tty_orig);
    tty = tty_orig;
    tty.c_lflag &= ~(ECHO | ICANON | INPCK | ISTRIP);
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSADRAIN, &tty);

    // determine terminal width
    width = 80;
#ifdef TIOCGWINSZ
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col) {
            width = ws.ws_col;
        }
    }
#endif
    tty_prompt.widthInChars = width;
    tty_prompt.printf = Sys_Printf;
    tty_enabled = qtrue;

    // figure out input line width
    width--;
    if (width > MAX_FIELD_TEXT - 1) {
        width = MAX_FIELD_TEXT - 1;
    }
    IF_Init(&tty_prompt.inputLine, width, width);

    // display command prompt
    stdout_write("]", 1);
}

static void tty_shutdown_input(void)
{
    if (tty_io) {
        NET_RemoveFd(STDIN_FILENO);
        tty_io = NULL;
    }
    if (tty_enabled) {
        tty_hide_input();
        tcsetattr(STDIN_FILENO, TCSADRAIN, &tty_orig);
        tty_enabled = qfalse;
    }
}

void Sys_SetConsoleColor(color_index_t color)
{
    static const char color_to_ansi[8] =
    { '0', '1', '2', '3', '4', '6', '5', '7' };
    char buf[5];
    size_t len;

    if (!sys_console || !sys_console->integer) {
        return;
    }

    if (!tty_enabled) {
        return;
    }

    buf[0] = '\033';
    buf[1] = '[';
    switch (color) {
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

    if (color != COLOR_NONE) {
        tty_hide_input();
    }
    stdout_write(buf, len);
    if (color == COLOR_NONE) {
        tty_show_input();
    }
}

static void tty_write_output(const char *text)
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

    stdout_write(buf, len);
}

/*
=================
Sys_ConsoleOutput
=================
*/
void Sys_ConsoleOutput(const char *text)
{
    if (!sys_console || !sys_console->integer) {
        return;
    }

    if (!tty_enabled) {
        tty_write_output(text);
    } else {
        tty_hide_input();
        tty_write_output(text);
        tty_show_input();
    }
}

void Sys_SetConsoleTitle(const char *title)
{
    char buf[MAX_STRING_CHARS];
    size_t len;

    if (!sys_console || !sys_console->integer) {
        return;
    }

    if (!tty_enabled) {
        return;
    }

    buf[0] = '\033';
    buf[1] = ']';
    buf[2] = '0';
    buf[3] = ';';

    for (len = 4; len < MAX_STRING_CHARS - 1; len++) {
        int c = *title++;
        if (!c) {
            break;
        }
        buf[len] = Q_charascii(c);
    }

    buf[len++] = '\007';

    stdout_write(buf, len);
}

static void tty_parse_input(const char *text)
{
    inputField_t *f;
    char *s;
    int i, key;

    f = &tty_prompt.inputLine;
    while (*text) {
        key = *text++;

        if (key == tty_orig.c_cc[VERASE] || key == 127 || key == 8) {
            if (f->cursorPos) {
                f->text[--f->cursorPos] = 0;
                stdout_write("\b \b", 3);
            }
            continue;
        }

        if (key == tty_orig.c_cc[VKILL]) {
            for (i = 0; i < f->cursorPos; i++) {
                stdout_write("\b \b", 3);
            }
            f->cursorPos = 0;
            continue;
        }

        if (key >= 32) {
            if (f->cursorPos == f->maxChars - 1) {
                stdout_write(va("\b \b%c", key), 4);
                f->text[f->cursorPos + 0] = key;
                f->text[f->cursorPos + 1] = 0;
            } else {
                stdout_write(va("%c", key), 1);
                f->text[f->cursorPos + 0] = key;
                f->text[f->cursorPos + 1] = 0;
                f->cursorPos++;
            }
            continue;
        }

        if (key == '\n') {
            tty_hide_input();
            s = Prompt_Action(&tty_prompt);
            if (s) {
                if (*s == '\\' || *s == '/') {
                    s++;
                }
                Sys_Printf("]%s\n", s);
                Cbuf_AddText(&cmd_buffer, s);
            } else {
                stdout_write("]\n", 2);
            }
            tty_show_input();
            continue;
        }

        if (key == '\t') {
            tty_hide_input();
            Prompt_CompleteCommand(&tty_prompt, qfalse);
            f->cursorPos = strlen(f->text);   // FIXME
            tty_show_input();
            continue;
        }

        if (*text) {
            key = *text++;
            if (key == '[' || key == 'O') {
                if (*text) {
                    key = *text++;
                    switch (key) {
                    case 'A':
                        tty_hide_input();
                        Prompt_HistoryUp(&tty_prompt);
                        tty_show_input();
                        break;
                    case 'B':
                        tty_hide_input();
                        Prompt_HistoryDown(&tty_prompt);
                        tty_show_input();
                        break;
#if 0
                    case 'C':
                        if (f->text[f->cursorPos]) {
                            Sys_ConsoleWrite("\033[C", 3);
                            f->cursorPos++;
                        }
                        break;
                    case 'D':
                        if (f->cursorPos) {
                            Sys_ConsoleWrite("\033[D", 3);
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

void Sys_RunConsole(void)
{
    char text[MAX_STRING_CHARS];
    ssize_t ret;

    if (!sys_console || !sys_console->integer) {
        return;
    }

    if (!tty_io || !tty_io->canread) {
        return;
    }

    ret = read(STDIN_FILENO, text, sizeof(text) - 1);
    if (!ret) {
        Com_DPrintf("Read EOF from stdin.\n");
        tty_shutdown_input();
        Cvar_Set("sys_console", "0");
        return;
    }

    // make sure the next call will not block
    tty_io->canread = qfalse;

    if (ret < 0) {
        if (errno == EAGAIN || errno == EINTR) {
            return;
        }
        Com_Error(ERR_FATAL, "%s: read() failed: %s",
                  __func__, strerror(errno));
    }

    text[ret] = 0;

    if (!tty_enabled) {
        Cbuf_AddText(&cmd_buffer, text);
        return;
    }

    tty_parse_input(text);
}

#endif // USE_SYSCON

/*
===============================================================================

HUNK

===============================================================================
*/

void Hunk_Begin(mempool_t *pool, size_t maxsize)
{
    void *buf;

    // reserve a huge chunk of memory, but don't commit any yet
    pool->maxsize = (maxsize + 4095) & ~4095;
    pool->cursize = 0;
    buf = mmap(NULL, pool->maxsize, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANON, -1, 0);
    if (buf == NULL || buf == (void *) - 1) {
        Com_Error(ERR_FATAL, "%s: unable to reserve %"PRIz" bytes: %s",
                  __func__, pool->maxsize, strerror(errno));
    }
    pool->base = buf;
    pool->mapped = pool->maxsize;
}

void *Hunk_Alloc(mempool_t *pool, size_t size)
{
    void *buf;

    // round to cacheline
    size = (size + 63) & ~63;
    if (pool->cursize + size > pool->maxsize) {
        Com_Error(ERR_FATAL, "%s: unable to allocate %"PRIz" bytes out of %"PRIz,
                  __func__, size, pool->maxsize);
    }
    buf = (byte *)pool->base + pool->cursize;
    pool->cursize += size;
    return buf;
}

void Hunk_End(mempool_t *pool)
{
    size_t newsize = (pool->cursize + 4095) & ~4095;

    if (newsize > pool->maxsize) {
        Com_Error(ERR_FATAL, "%s: newsize > maxsize", __func__);
    }

    if (newsize < pool->maxsize) {
#ifdef _GNU_SOURCE
        void *buf = mremap(pool->base, pool->maxsize, newsize, 0);
#else
        void *unmap_base = (byte *)pool->base + newsize;
        size_t unmap_len = pool->maxsize - newsize;
        void *buf = munmap(unmap_base, unmap_len) + pool->base;
#endif
        if (buf != pool->base) {
            Com_Error(ERR_FATAL, "%s: could not remap virtual block: %s",
                      __func__, strerror(errno));
        }
    }
    pool->mapped = newsize;
}

void Hunk_Free(mempool_t *pool)
{
    if (pool->base) {
        if (munmap(pool->base, pool->mapped)) {
            Com_Error(ERR_FATAL, "%s: munmap failed: %s",
                      __func__, strerror(errno));
        }
    }
    memset(pool, 0, sizeof(*pool));
}

/*
===============================================================================

GENERAL ROUTINES

===============================================================================
*/

void Sys_DebugBreak(void)
{
    raise(SIGTRAP);
}

unsigned Sys_Milliseconds(void)
{
    struct timeval tp;
    unsigned time;

    gettimeofday(&tp, NULL);
    time = tp.tv_sec * 1000 + tp.tv_usec / 1000;
    return time;
}

/*
=================
Sys_Quit

This function never returns.
=================
*/
void Sys_Quit(void)
{
#if USE_SYSCON
    tty_shutdown_input();
#endif
    exit(EXIT_SUCCESS);
}

#define SYS_SITE_CFG    "/etc/default/q2pro"

void Sys_AddDefaultConfig(void)
{
    FILE *fp;
    struct stat st;
    size_t len, r;

    fp = fopen(SYS_SITE_CFG, "r");
    if (!fp) {
        return;
    }

    if (fstat(fileno(fp), &st) == 0) {
        len = st.st_size;
        if (len >= cmd_buffer.maxsize) {
            len = cmd_buffer.maxsize - 1;
        }

        r = fread(cmd_buffer.text, 1, len, fp);
        cmd_buffer.text[r] = 0;

        cmd_buffer.cursize = COM_Compress(cmd_buffer.text);
    }

    fclose(fp);

    if (cmd_buffer.cursize) {
        Com_Printf("Execing %s\n", SYS_SITE_CFG);
        Cbuf_Execute(&cmd_buffer);
    }
}

void Sys_Sleep(int msec)
{
    struct timespec req;

    req.tv_sec = msec / 1000;
    req.tv_nsec = (msec % 1000) * 1000000;
    nanosleep(&req, NULL);
}

#if USE_AC_CLIENT
qboolean Sys_GetAntiCheatAPI(void)
{
    Sys_Sleep(1500);
    return qfalse;
}
#endif

static void hup_handler(int signum)
{
    Com_FlushLogs();
}

static void term_handler(int signum)
{
#ifdef _GNU_SOURCE
    Com_Printf("%s\n", strsignal(signum));
#else
    Com_Printf("Received signal %d, exiting\n", signum);
#endif
    Com_Quit(NULL, ERR_DISCONNECT);
}

static void kill_handler(int signum)
{
#if USE_SYSCON
    tty_shutdown_input();
#endif

#if USE_CLIENT && USE_REF
    VID_FatalShutdown();
#endif

#ifdef _GNU_SOURCE
    fprintf(stderr, "%s\n", strsignal(signum));
#else
    fprintf(stderr, "Received signal %d, aborting\n", signum);
#endif

    exit(EXIT_FAILURE);
}

/*
=================
Sys_Init
=================
*/
void Sys_Init(void)
{
    char    *homedir;

    signal(SIGTERM, term_handler);
    signal(SIGINT, term_handler);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGUSR1, hup_handler);

    // basedir <path>
    // allows the game to run from outside the data tree
    sys_basedir = Cvar_Get("basedir", DATADIR, CVAR_NOSET);

    // homedir <path>
    // specifies per-user writable directory for demos, screenshots, etc
    if (HOMEDIR[0] == '~') {
        char *s = getenv("HOME");
        if (s && *s) {
            homedir = va("%s%s", s, HOMEDIR + 1);
        } else {
            homedir = "";
        }
    } else {
        homedir = HOMEDIR;
    }
    sys_homedir = Cvar_Get("homedir", homedir, CVAR_NOSET);
    sys_libdir = Cvar_Get("libdir", LIBDIR, CVAR_NOSET);
    sys_forcegamelib = Cvar_Get("sys_forcegamelib", "", CVAR_NOSET);

#if USE_SYSCON
    // we want TTY support enabled if started from terminal,
    // but don't want any output by default if launched without one
    // (from X session for example)
    sys_console = Cvar_Get("sys_console", isatty(STDIN_FILENO) &&
                           isatty(STDOUT_FILENO) ? "2" : "0", CVAR_NOSET);

    if (sys_console->integer > 0) {
        int ret;

        // change stdin to non-blocking
        ret = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (!(ret & O_NONBLOCK))
            fcntl(STDIN_FILENO, F_SETFL, ret | O_NONBLOCK);

        // change stdout to non-blocking
        ret = fcntl(STDOUT_FILENO, F_GETFL, 0);
        if (!(ret & O_NONBLOCK))
            fcntl(STDOUT_FILENO, F_SETFL, ret | O_NONBLOCK);

        // add stdin to the list of descriptors to wait on
        tty_io = NET_AddFd(STDIN_FILENO);
        tty_io->wantread = qtrue;

        // init optional TTY support
        if (sys_console->integer > 1)
            tty_init_input();

        signal(SIGHUP, term_handler);
    } else
#endif
        if (Com_IsDedicated()) {
            signal(SIGHUP, hup_handler);
        }

    sys_parachute = Cvar_Get("sys_parachute", "1", CVAR_NOSET);

    if (sys_parachute->integer) {
        // perform some cleanup when crashing
        signal(SIGSEGV, kill_handler);
        signal(SIGILL, kill_handler);
        signal(SIGFPE, kill_handler);
        signal(SIGTRAP, kill_handler);
    }
}

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
=================
Sys_Error
=================
*/
void Sys_Error(const char *error, ...)
{
    va_list     argptr;
    char        text[MAXERRORMSG];

#if USE_SYSCON
    tty_shutdown_input();
#endif

#if USE_CLIENT && USE_REF
    VID_FatalShutdown();
#endif

    va_start(argptr, error);
    Q_vsnprintf(text, sizeof(text), error, argptr);
    va_end(argptr);

    fprintf(stderr, "********************\n"
            "FATAL: %s\n"
            "********************\n", text);
    exit(EXIT_FAILURE);
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
void Sys_FreeLibrary(void *handle)
{
    if (handle && dlclose(handle)) {
        Com_Error(ERR_FATAL, "dlclose failed on %p: %s", handle, dlerror());
    }
}

/*
=================
Sys_LoadLibrary
=================
*/
void *Sys_LoadLibrary(const char *path, const char *sym, void **handle)
{
    void    *module, *entry;

    *handle = NULL;

    module = dlopen(path, RTLD_LAZY);
    if (!module) {
        Com_DPrintf("%s failed: %s\n", __func__, dlerror());
        return NULL;
    }

    if (sym) {
        entry = dlsym(module, sym);
        if (!entry) {
            Com_DPrintf("%s failed: %s\n", __func__, dlerror());
            dlclose(module);
            return NULL;
        }
    } else {
        entry = NULL;
    }

    Com_DPrintf("%s succeeded: %s\n", __func__, path);

    *handle = module;

    return entry;
}

void *Sys_GetProcAddress(void *handle, const char *sym)
{
    return dlsym(handle, sym);
}

/*
===============================================================================

MISC

===============================================================================
*/

static void *copy_info(const char *name, const struct stat *st)
{
    return FS_CopyInfo(name, st->st_size, st->st_ctime, st->st_mtime);
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
    struct dirent *ent;
    DIR *dir;
    struct stat st;
    char fullpath[MAX_OSPATH];
    char *name;
    size_t len;
    void *info;

    if ((dir = opendir(path)) == NULL) {
        return;
    }

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue; // ignore dotfiles
        }

        len = Q_concat(fullpath, sizeof(fullpath),
                       path, "/", ent->d_name, NULL);
        if (len >= sizeof(fullpath)) {
            continue;
        }

        if (stat(fullpath, &st) == -1) {
            continue;
        }

        // pattern search implies recursive search
        if ((flags & FS_SEARCH_BYFILTER) &&
            S_ISDIR(st.st_mode) && depth < MAX_LISTED_DEPTH) {
            Sys_ListFiles_r(fullpath, filter, flags, baselen,
                            count_p, files, depth + 1);

            // re-check count
            if (*count_p >= MAX_LISTED_FILES) {
                break;
            }
        }

        // check type
        if (flags & FS_SEARCH_DIRSONLY) {
            if (!S_ISDIR(st.st_mode)) {
                continue;
            }
        } else {
            if (!S_ISREG(st.st_mode)) {
                continue;
            }
        }

        // check filter
        if (filter) {
            if (flags & FS_SEARCH_BYFILTER) {
                if (!FS_WildCmp(filter, fullpath + baselen)) {
                    continue;
                }
            } else {
                if (!FS_ExtCmp(filter, ent->d_name)) {
                    continue;
                }
            }
        }

        // strip path
        if (flags & FS_SEARCH_SAVEPATH) {
            name = fullpath + baselen;
        } else {
            name = ent->d_name;
        }

        // strip extension
        if (flags & FS_SEARCH_STRIPEXT) {
            *COM_FileExtension(name) = 0;

            if (!*name) {
                continue;
            }
        }

        // copy info off
        if (flags & FS_SEARCH_EXTRAINFO) {
            info = copy_info(name, &st);
        } else {
            info = FS_CopyString(name);
        }

        files[(*count_p)++] = info;

        if (*count_p >= MAX_LISTED_FILES) {
            break;
        }
    }

    closedir(dir);
}

/*
=================
main
=================
*/
int main(int argc, char **argv)
{
    if (argc > 1) {
        if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version")) {
            fprintf(stderr, "%s\n", com_version_string);
            return 0;
        }
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            fprintf(stderr, "Usage: %s [+command arguments] [...]\n", argv[0]);
            return 0;
        }
    }

    if (!getuid() || !geteuid()) {
        fprintf(stderr,    "You can not run " PRODUCT " as superuser "
                "for security reasons!\n");
        return 1;
    }

    Qcommon_Init(argc, argv);
    while (1) {
        Qcommon_Frame();
    }

    return EXIT_FAILURE; // never gets here
}

