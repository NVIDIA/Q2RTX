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

#ifndef SYSTEM_H
#define SYSTEM_H

#include "common/utils.h"

typedef struct {
    const char  *filter;
    unsigned    flags;
    unsigned    baselen;
    void        **files;
    int         count;
} listfiles_t;

// loads the dll and returns entry pointer
void    *Sys_LoadLibrary(const char *path, const char *sym, void **handle);
void    Sys_FreeLibrary(void *handle);
void    *Sys_GetProcAddress(void *handle, const char *sym);

unsigned Sys_Milliseconds(void);
void     Sys_Sleep(int msec);

void    Sys_Init(void);
void    Sys_AddDefaultConfig(void);

#if USE_SYSCON
void    Sys_RunConsole(void);
void    Sys_ConsoleOutput(const char *string);
void    Sys_SetConsoleTitle(const char *title);
void    Sys_SetConsoleColor(color_index_t color);
void    Sys_Printf(const char *fmt, ...) q_printf(1, 2);
#else
#define Sys_RunConsole()            (void)0
#define Sys_ConsoleOutput(string)   (void)0
#define Sys_SetConsoleTitle(title)  (void)0
#define Sys_SetConsoleColor(color)  (void)0
#define Sys_Printf(...)             (void)0
#endif

void    Sys_Error(const char *error, ...) q_noreturn q_printf(1, 2);
void    Sys_Quit(void) q_noreturn;

void    Sys_ListFiles_r(listfiles_t *list, const char *path, int depth);
bool    Sys_IsDir(const char *path);
bool    Sys_IsFile(const char *path);

void    Sys_DebugBreak(void);

#if USE_AC_CLIENT
bool Sys_GetAntiCheatAPI(void);
#endif

#if USE_CLIENT
typedef struct asyncwork_s {
    void (*work_cb)(void *);
    void (*done_cb)(void *);
    void *cb_arg;
    struct asyncwork_s *next;
} asyncwork_t;

void Sys_QueueAsyncWork(asyncwork_t *work);
#endif

extern cvar_t   *sys_basedir;
extern cvar_t   *sys_libdir;
extern cvar_t   *sys_homedir;
extern cvar_t   *sys_forcegamelib;

#endif // SYSTEM_H
