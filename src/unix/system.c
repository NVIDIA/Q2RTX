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

#include "shared/shared.h"
#include "common/cmd.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#if USE_REF
#include "client/video.h"
#endif
#include "system/system.h"
#include "tty.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>

#if USE_SDL
#include <SDL_main.h>
#endif

cvar_t  *sys_basedir;
cvar_t  *sys_libdir;
cvar_t  *sys_homedir;
cvar_t  *sys_forcegamelib;

static qboolean terminate;

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
    tty_shutdown_input();
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

    terminate = qtrue;
}

static void kill_handler(int signum)
{
    tty_shutdown_input();

#if USE_CLIENT && USE_REF && !USE_X11
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
    cvar_t  *sys_parachute;

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

    if (tty_init_input()) {
        signal(SIGHUP, term_handler);
    } else if (COM_DEDICATED) {
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

/*
=================
Sys_Error
=================
*/
void Sys_Error(const char *error, ...)
{
    va_list     argptr;
    char        text[MAXERRORMSG];

    tty_shutdown_input();

#if USE_CLIENT && USE_REF
    VID_FatalShutdown();
#endif

    va_start(argptr, error);
    Q_vsnprintf(text, sizeof(text), error, argptr);
    va_end(argptr);

    fprintf(stderr,
            "********************\n"
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

    dlerror();
    module = dlopen(path, RTLD_LAZY);
    if (!module) {
        Com_SetLastError(dlerror());
        return NULL;
    }

    if (sym) {
        dlerror();
        entry = dlsym(module, sym);
        if (!entry) {
            Com_SetLastError(dlerror());
            dlclose(module);
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

    dlerror();
    entry = dlsym(handle, sym);
    if (!entry)
        Com_SetLastError(dlerror());

    return entry;
}

/*
===============================================================================

MISC

===============================================================================
*/

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

        st.st_mode = 0;

#ifdef _DIRENT_HAVE_D_TYPE
        // try to avoid stat() if possible
        if (!(flags & FS_SEARCH_EXTRAINFO)
            && ent->d_type != DT_UNKNOWN
            && ent->d_type != DT_LNK) {
            st.st_mode = DTTOIF(ent->d_type);
        }
#endif

        if (st.st_mode == 0 && stat(fullpath, &st) == -1) {
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
            info = FS_CopyInfo(name,
                               st.st_size,
                               st.st_ctime,
                               st.st_mtime);
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
            return EXIT_SUCCESS;
        }
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            fprintf(stderr, "Usage: %s [+command arguments] [...]\n", argv[0]);
            return EXIT_SUCCESS;
        }
    }

    if (!getuid() || !geteuid()) {
        fprintf(stderr, "You can not run " PRODUCT " as superuser "
                "for security reasons!\n");
        return EXIT_FAILURE;
    }

    Qcommon_Init(argc, argv);
    while (!terminate) {
        Qcommon_Frame();
    }

    Com_Quit(NULL, ERR_DISCONNECT);
    return EXIT_FAILURE; // never gets here
}

