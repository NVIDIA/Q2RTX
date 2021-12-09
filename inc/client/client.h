/*
Copyright (C) 2003-2006 Andrey Nazarov

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

#ifndef CLIENT_H
#define CLIENT_H

#include "common/cmd.h"
#include "common/net/net.h"
#include "common/utils.h"

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 8

#if USE_CLIENT

#define MAX_LOCAL_SERVERS   16
#define MAX_STATUS_PLAYERS  64

typedef struct {
    char name[MAX_CLIENT_NAME];
    int ping;
    int score;
} playerStatus_t;

typedef struct {
    char infostring[MAX_INFO_STRING];
    playerStatus_t players[MAX_STATUS_PLAYERS];
    int numPlayers;
} serverStatus_t;

typedef struct {
    char map[MAX_QPATH];
    char pov[MAX_CLIENT_NAME];
    qboolean mvd;
} demoInfo_t;

typedef enum {
    ACT_MINIMIZED,
    ACT_RESTORED,
    ACT_ACTIVATED
} active_t;

qboolean CL_ProcessEvents(void);
#if USE_ICMP
void CL_ErrorEvent(netadr_t *from);
#endif
void CL_Init(void);
void CL_Disconnect(error_type_t type);
void CL_Shutdown(void);
unsigned CL_Frame(unsigned msec);
void CL_RestartFilesystem(qboolean total);
void CL_Activate(active_t active);
void CL_UpdateUserinfo(cvar_t *var, from_t from);
void CL_SendStatusRequest(const netadr_t *address);
demoInfo_t *CL_GetDemoInfo(const char *path, demoInfo_t *info);
qboolean CL_CheatsOK(void);
void CL_SetSky(void);

#if USE_CURL
ssize_t HTTP_FetchFile(const char *url, void **data);
#endif

qboolean CL_ForwardToServer(void);
// adds the current command line as a clc_stringcmd to the client message.
// things like godmode, noclip, etc, are commands directed to the server,
// so when they are typed in at the console, they will need to be forwarded.

void Con_Init(void);
void Con_SetColor(color_index_t color);
void Con_Print(const char *text);
void Con_Printf(const char *fmt, ...) q_printf(1, 2);
void Con_Close(qboolean force);

void SCR_BeginLoadingPlaque(void);
void SCR_EndLoadingPlaque(void);
void SCR_ModeChanged(void);
void SCR_UpdateScreen(void);

#define U32_BLACK   MakeColor(  0,   0,   0, 255)
#define U32_RED     MakeColor(255,   0,   0, 255)
#define U32_GREEN   MakeColor(  0, 255,   0, 255)
#define U32_YELLOW  MakeColor(255, 255,   0, 255)
#define U32_BLUE    MakeColor(  0,   0, 255, 255)
#define U32_CYAN    MakeColor(  0, 255, 255, 255)
#define U32_MAGENTA MakeColor(255,   0, 255, 255)
#define U32_WHITE   MakeColor(255, 255, 255, 255)

#define UI_LEFT             0x00000001
#define UI_RIGHT            0x00000002
#define UI_CENTER           (UI_LEFT | UI_RIGHT)
#define UI_BOTTOM           0x00000004
#define UI_TOP              0x00000008
#define UI_MIDDLE           (UI_BOTTOM | UI_TOP)
#define UI_DROPSHADOW       0x00000010
#define UI_ALTCOLOR         0x00000020
#define UI_IGNORECOLOR      0x00000040
#define UI_XORCOLOR         0x00000080
#define UI_AUTOWRAP         0x00000100
#define UI_MULTILINE        0x00000200
#define UI_DRAWCURSOR       0x00000400

extern const uint32_t   colorTable[8];

qboolean SCR_ParseColor(const char *s, color_t *color);

float V_CalcFov(float fov_x, float width, float height);

#else // USE_CLIENT

#define CL_Init()                       (void)0
#define CL_Disconnect(type)             (void)0
#define CL_Shutdown()                   (void)0
#define CL_UpdateUserinfo(var, from)    (void)0
#define CL_ErrorEvent(from)             (void)0
#define CL_RestartFilesystem(total)     FS_Restart(total)
#define CL_ForwardToServer()            qfalse
#define CL_CheatsOK()                   (!!Cvar_VariableInteger("cheats"))

#define Con_Init()                      (void)0
#define Con_SetColor(color)             (void)0
#define Con_Print(text)                 (void)0

#define SCR_BeginLoadingPlaque()        (void)0
#define SCR_EndLoadingPlaque()          (void)0

#endif // !USE_CLIENT

#endif // CLIENT_H
