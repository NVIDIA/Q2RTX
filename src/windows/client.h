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
// client.h -- Win32 client stuff
//

#include "shared/shared.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#if USE_CLIENT
#include "client/client.h"
#include "client/input.h"
#include "client/keys.h"
#include "client/ui.h"
#include "client/video.h"
#include "refresh/refresh.h"
#endif
#include "system/system.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef STATIC
#define STATIC static
#endif

// supported in XP SP3 or greater
#ifndef PROCESS_DEP_ENABLE
#define PROCESS_DEP_ENABLE 0x01
#endif
#ifndef PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION
#define PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION 0x02
#endif

#if USE_CLIENT

#include <tchar.h>

#define IDI_APP 100

#define MOUSE_BUTTONS   5

// supported in Vista or greater
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL  0x020E
#endif

#ifndef __LPCGUID_DEFINED__
#define __LPCGUID_DEFINED__
typedef const GUID *LPCGUID;
#endif

// MinGW-w64 doesn't define these...
#ifndef DM_GRAYSCALE
#define DM_GRAYSCALE    1
#endif
#ifndef DM_INTERLACED
#define DM_INTERLACED   2
#endif

typedef struct {
    HWND    wnd;
    HDC     dc;

    DEVMODE  dm;

    DWORD   lastMsgTime;
    HHOOK   kbdHook;

    vidFlags_t flags;
    byte    *buffer;
    int     pitch;

    SHORT   gamma_cust[3][256];
    SHORT   gamma_orig[3][256];

    // x and y specify position of non-client area on the screen
    // width and height specify size of client area
    vrect_t rc;

    // rectangle of client area in screen coordinates
    RECT    screen_rc;

    // center of client area in screen coordinates
    int     center_x, center_y;

    qboolean    alttab_disabled;
    int         mode_changed;

    struct {
        enum {
            WIN_MOUSE_DISABLED,
            WIN_MOUSE_LEGACY,
            WIN_MOUSE_RAW
        } initialized;
        qboolean    grabbed;
        int         state;
        qboolean    parmsvalid;
        qboolean    restoreparms;
        int         originalparms[3];
        int         mx, my;
    } mouse;
} win_state_t;

extern win_state_t      win;

void Win_Init(void);
void Win_Shutdown(void);
void Win_SetMode(void);
void Win_ModeChanged(void);

#endif // USE_CLIENT

extern HINSTANCE                    hGlobalInstance;

#if USE_DBGHELP
extern HANDLE                       mainProcessThread;
extern LPTOP_LEVEL_EXCEPTION_FILTER prevExceptionFilter;

LONG WINAPI Sys_ExceptionFilter(LPEXCEPTION_POINTERS);
#endif

