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

#include "com_local.h"
#include "key_public.h"
#include "in_public.h"
#include "vid_public.h"
#include "vid_local.h"
#include "ref_public.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define IDI_APP 100

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL ( WM_MOUSELAST + 1 )  // message that will be supported by the OS 
#endif

#ifndef SPI_GETWHEELSCROLLLINES
#define SPI_GETWHEELSCROLLLINES	104
#endif

#ifndef WM_XBUTTONDOWN
#define WM_XBUTTONDOWN                  0x020B
#define WM_XBUTTONUP                    0x020C
#endif

#ifndef MK_XBUTTON1
#define MK_XBUTTON1         0x0020
#define MK_XBUTTON2         0x0040
#endif

#define MOUSE_BUTTONS	5

#ifndef __LPCGUID_DEFINED__
#define __LPCGUID_DEFINED__
typedef const GUID *LPCGUID;
#endif

#define PRIVATE	static

#define MAX_VIDEO_MODES   128

typedef struct {
    HWND    wnd;
    HDC     dc;

    DEVMODE  dm;

    DWORD	lastMsgTime;
    HHOOK   kbdHook;

    vidFlags_t flags;
    SHORT   gamma_cust[3][256];
    SHORT   gamma_orig[3][256];
    vrect_t rc;
	byte	*buffer;
	int		pitch;

    int     center_x, center_y;

    qboolean	alttab_disabled;
    int         mode_changed;

    struct {
        qboolean	initialized;
        qboolean	active;	        // qfalse when not focus app
        int			state;
        qboolean    parmsvalid;
        qboolean	restoreparms;
        int		    originalparms[3];
    } mouse;
} win_state_t;

extern HINSTANCE		hGlobalInstance;
extern win_state_t      win;
extern qboolean         iswinnt;

void Win_Init( void );
void Win_Shutdown( void );
void Win_SetMode( void ); 
void Win_ModeChanged( void );
void Win_UpdateGamma( const byte *table ); 

