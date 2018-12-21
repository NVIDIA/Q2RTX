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

#ifndef COMMON_H
#define COMMON_H

#include "common/cmd.h"
#include "common/utils.h"

//
// common.h -- definitions common between client and server, but not game.dll
//

#define PRODUCT         "Q2VKPT"

#if USE_CLIENT
#define APPLICATION     "q2vkpt"
#else
#define APPLICATION     "q2proded"
#endif

#define COM_DEFAULT_CFG     "default.cfg"
#define COM_AUTOEXEC_CFG    "autoexec.cfg"
#define COM_POSTEXEC_CFG    "postexec.cfg"
#define COM_POSTINIT_CFG    "postinit.cfg"

#ifdef _WIN32
#define COM_CONFIG_CFG      "q2config.cfg"
#else
#define COM_CONFIG_CFG      "config.cfg"
#endif

// FIXME: rename these
#define COM_HISTORYFILE_NAME    ".conhistory"
#define COM_DEMOCACHE_NAME      ".democache"

#define MAXPRINTMSG     4096
#define MAXERRORMSG     1024

#define CONST_STR_LEN(x) x, x ? sizeof(x) - 1 : 0

#define __STRINGIFY(x)  #x
#define STRINGIFY(x)    __STRINGIFY(x)

typedef struct {
    const char *name;
    void (* const func)(void);
} ucmd_t;

static inline const ucmd_t *Com_Find(const ucmd_t *u, const char *c)
{
    for (; u->name; u++) {
        if (!strcmp(c, u->name)) {
            return u;
        }
    }
    return NULL;
}

typedef struct string_entry_s {
    struct string_entry_s *next;
    char string[1];
} string_entry_t;

typedef void (*rdflush_t)(int target, char *buffer, size_t len);

void        Com_BeginRedirect(int target, char *buffer, size_t buffersize, rdflush_t flush);
void        Com_EndRedirect(void);

void        Com_AbortFunc(void (*func)(void *), void *arg);

#ifdef _WIN32
void        Com_AbortFrame(void);
#endif

char        *Com_GetLastError(void);
void        Com_SetLastError(const char *msg);

void        Com_Quit(const char *reason, error_type_t type) q_noreturn;

void        Com_SetColor(color_index_t color);

void        Com_Address_g(genctx_t *ctx);
void        Com_Generic_c(genctx_t *ctx, int argnum);
#if USE_CLIENT
void        Com_Color_g(genctx_t *ctx);
#endif

size_t      Com_Time_m(char *buffer, size_t size);
size_t      Com_Uptime_m(char *buffer, size_t size);
size_t      Com_UptimeLong_m(char *buffer, size_t size);

#ifndef _WIN32
void        Com_FlushLogs(void);
#endif

void        Com_AddConfigFile(const char *name, unsigned flags);

#if USE_CLIENT
#define COM_DEDICATED   (dedicated->integer != 0)
#else
#define COM_DEDICATED   1
#endif

#ifdef _DEBUG
#define Com_DPrintf(...) \
    if (developer && developer->integer > 0) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__)
#define Com_DDPrintf(...) \
    if (developer && developer->integer > 1) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__)
#define Com_DDDPrintf(...) \
    if (developer && developer->integer > 2) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__)
#define Com_DDDDPrintf(...) \
    if (developer && developer->integer > 3) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__)
#else
#define Com_DPrintf(...)
#define Com_DDPrintf(...)
#define Com_DDDPrintf(...)
#define Com_DDDDPrintf(...)
#endif

extern cvar_t  *z_perturb;

#ifdef _DEBUG
extern cvar_t   *developer;
#endif
extern cvar_t   *dedicated;
#if USE_CLIENT
extern cvar_t   *host_speeds;
#endif
extern cvar_t   *com_version;

#if USE_CLIENT
extern cvar_t   *cl_running;
extern cvar_t   *cl_paused;
#endif
extern cvar_t   *sv_running;
extern cvar_t   *sv_paused;
extern cvar_t   *com_timedemo;
extern cvar_t   *com_sleep;

extern cvar_t   *allow_download;
extern cvar_t   *allow_download_players;
extern cvar_t   *allow_download_models;
extern cvar_t   *allow_download_sounds;
extern cvar_t   *allow_download_maps;
extern cvar_t   *allow_download_textures;
extern cvar_t   *allow_download_pics;
extern cvar_t   *allow_download_others;

extern cvar_t   *rcon_password;

#if USE_CLIENT
// host_speeds times
extern unsigned     time_before_game;
extern unsigned     time_after_game;
extern unsigned     time_before_ref;
extern unsigned     time_after_ref;
#endif

extern const char   com_version_string[];

extern unsigned     com_framenum;
extern unsigned     com_eventTime; // system time of the last event
extern unsigned     com_localTime; // milliseconds since Q2 startup
extern qboolean     com_initialized;
extern time_t       com_startTime;

void Qcommon_Init(int argc, char **argv);
void Qcommon_Frame(void);

#endif // COMMON_H
