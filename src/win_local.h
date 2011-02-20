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

//
// win_local.h
//

#include "common.h"
#if USE_CLIENT
#include "key_public.h"
#include "in_public.h"
#include "vid_public.h"
#include "vid_local.h"
#include "ref_public.h"
#endif
#include "files.h"
#include "sys_public.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef STATIC
#define STATIC static
#endif

#ifdef __COREDLL__
#ifdef GetProcAddress
#undef GetProcAddress
#endif
#define GetProcAddress GetProcAddressA
#endif

#if USE_CLIENT

#include <tchar.h>

#define IDI_APP 100

#define MOUSE_BUTTONS   5

#ifndef __LPCGUID_DEFINED__
#define __LPCGUID_DEFINED__
typedef const GUID *LPCGUID;
#endif

#ifdef __COREDLL__
#define ChangeDisplaySettings( mode, flags ) \
    ChangeDisplaySettingsEx( NULL, mode, NULL, flags, NULL )
#define AdjustWindowRect( rect, style, menu ) \
    AdjustWindowRectEx( rect, style, menu, 0 )
#endif

typedef struct {
    HWND    wnd;
    HDC     dc;

    DEVMODE  dm;

    DWORD   lastMsgTime;
    HHOOK   kbdHook;

    vidFlags_t flags;
#ifndef __COREDLL__
    SHORT   gamma_cust[3][256];
    SHORT   gamma_orig[3][256];
#endif

    // x and y specify position of non-client area on the screen
    // width and height specify size of client area
    vrect_t rc;

    byte    *buffer;
    int     pitch;

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
        grab_t      grabbed;
        int         state;
        qboolean    parmsvalid;
        qboolean    restoreparms;
        int         originalparms[3];
        int         mx, my;
    } mouse;
} win_state_t;

extern win_state_t      win;

void Win_Init( void );
void Win_Shutdown( void );
void Win_SetMode( void ); 
void Win_ModeChanged( void );

#endif // USE_CLIENT

extern HINSTANCE        hGlobalInstance;

#if USE_DBGHELP
DWORD Sys_ExceptionHandler( DWORD                   exceptionCode,
                            LPEXCEPTION_POINTERS    exceptionInfo );
#endif

