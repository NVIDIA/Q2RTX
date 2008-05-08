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
// vid_win.c
//

#include "win_local.h"

#define    WINDOW_CLASS_NAME "Quake2"

win_state_t     win;

static cvar_t    *vid_flip_on_switch;
static cvar_t    *vid_hwgamma;
static cvar_t    *win_noalttab;
static cvar_t    *win_disablewinkey;

/*
===============================================================================

COMMON WIN32 VIDEO RELATED ROUTINES

===============================================================================
*/

static void Win_Show( const vrect_t *rc, qboolean fullscreen ) {
    RECT			r;
    int				stylebits;
    int				x, y, w, h;
    HWND            after;

    r.left = 0;
    r.top = 0;
    r.right = rc->width;
    r.bottom = rc->height;

    if( fullscreen ) {
        after = HWND_TOPMOST;
        stylebits = WS_POPUP;
    } else {
        after = HWND_NOTOPMOST;
        stylebits = WS_OVERLAPPEDWINDOW;
    }

    AdjustWindowRect( &r, stylebits, FALSE );

    w = r.right - r.left;
    h = r.bottom - r.top;

    if( fullscreen ) {
    	// postion on primary monitor
    	x = 0;
    	y = 0;
    } else {
#if 0
        RECT screen;

    	// get virtual screen dimensions
    	if( GetSystemMetrics( SM_CMONITORS ) > 1 ) {
    		screen.left = GetSystemMetrics( SM_XVIRTUALSCREEN );
    		screen.top = GetSystemMetrics( SM_YVIRTUALSCREEN );
    		screen.right = screen.left + GetSystemMetrics( SM_CXVIRTUALSCREEN );
    		screen.bottom = screen.top + GetSystemMetrics( SM_CYVIRTUALSCREEN );
    	} else {
    		screen.left = 0;
    		screen.top = 0;
    		screen.right = GetSystemMetrics( SM_CXSCREEN );
    		screen.bottom = GetSystemMetrics( SM_CYSCREEN );
    	}
        Com_Printf("screen:%dx%dx%dx%d\n",screen.left,screen.top,screen.right,screen.bottom);

    	x = rc->x;
    	y = rc->y;

    	// clip to virtual screen
    	if( x + w > screen.right ) {
    		x = screen.right - w;
    	}
    	if( y + h > screen.bottom ) {
    		y = screen.bottom - h;
    	}
    	if( x < screen.left ) {
    		x = screen.left;
    	}
    	if( y < screen.top ) {
    		y = screen.top;
    	}
        Com_Printf("clip:%dx%d+%d+%d\n",w,h,x,y);
#else
    	x = rc->x;
    	y = rc->y;
#endif
    }

    win.rc.x = x;
    win.rc.y = y;
    win.rc.width = rc->width;
    win.rc.height = rc->height;

    SetWindowLong( win.wnd, GWL_STYLE, stylebits );
    SetWindowPos( win.wnd, after, x, y, w, h, SWP_FRAMECHANGED );
    ShowWindow( win.wnd, SW_SHOWNORMAL );    
    SetForegroundWindow( win.wnd );
    SetFocus( win.wnd );

    win.mode_changed = 0;
}

void Win_ModeChanged( void ) {
#ifndef REF_HARD_LINKED
    SWimp_ModeChanged();
#endif
    ref.ModeChanged( win.rc.width, win.rc.height, win.flags,
        win.pitch, win.buffer );
    SCR_ModeChanged();
}

void Win_SetMode( void ) {
    DEVMODE dm;
    vrect_t rc;
    int freq, depth;

//    ShowWindow( win.wnd, SW_HIDE );

    if( vid_fullscreen->integer > 0 ) {
        VID_GetModeFS( &rc, &freq, &depth );

        Com_DPrintf( "...setting fullscreen mode: %dx%d\n",
            rc.width, rc.height );

        memset( &dm, 0, sizeof( dm ) );
        dm.dmSize       = sizeof( dm );
        dm.dmPelsWidth  = rc.width;
        dm.dmPelsHeight = rc.height;
        dm.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT;

        if( freq ) {
            dm.dmDisplayFrequency = freq;
            dm.dmFields |= DM_DISPLAYFREQUENCY;
            Com_DPrintf( "...using display frequency of %d\n", freq );
        }

        if( depth ) {
            dm.dmBitsPerPel = depth;
            dm.dmFields |= DM_BITSPERPEL;
            Com_DPrintf( "...using bitdepth of %d\n", depth );
        } else {
            HDC hdc;
            int bitspixel;
            
            hdc = GetDC( NULL );
            bitspixel = GetDeviceCaps( hdc, BITSPIXEL );
            ReleaseDC( NULL, hdc );

            Com_DPrintf( "...using desktop bitdepth of %d\n", bitspixel );    	
        }

        Com_DPrintf( "...calling CDS: " );
        if( ChangeDisplaySettings( &dm, CDS_FULLSCREEN ) ==
            DISP_CHANGE_SUCCESSFUL )
        {
            Com_DPrintf( "ok\n" );
            Win_Show( &rc, qtrue );
            win.flags |= QVF_FULLSCREEN;
            win.dm = dm;
            return;
        }
        Com_DPrintf( "failed\n" );
    }

    VID_GetGeometry( &rc );

    Com_DPrintf( "...setting windowed mode: %dx%d+%d+%d\n",
        rc.width, rc.height, rc.x, rc.y );

    Win_Show( &rc, qfalse );
    ChangeDisplaySettings( NULL, 0 );
    win.flags &= ~QVF_FULLSCREEN;
}

void Win_UpdateGamma( const byte *table ) {
    WORD v;
    int i;

    if( win.flags & QVF_GAMMARAMP ) {
        for( i = 0; i < 256; i++ ) {
            v = table[i] << 8;
            win.gamma_cust[0][i] = v;
            win.gamma_cust[1][i] = v;
            win.gamma_cust[2][i] = v;
        }

        SetDeviceGammaRamp( win.dc, win.gamma_cust );
    }
}

/*
=================
Win_DisableAltTab
=================
*/
static void Win_DisableAltTab( void ) {
    if( !win.alttab_disabled ) {
        if( !iswinnt ) {
            BOOL old;

            SystemParametersInfo( SPI_SETSCREENSAVERRUNNING, 1, &old, 0 );
        } else {
            RegisterHotKey( 0, 0, MOD_ALT, VK_TAB );
            RegisterHotKey( 0, 1, MOD_ALT, VK_RETURN );
        }
        win.alttab_disabled = qtrue;
    }
}

/*
=================
Win_EnableAltTab
=================
*/
static void Win_EnableAltTab( void ) {
    if( win.alttab_disabled ) {
    	if( !iswinnt ) {
    		BOOL old;

    		SystemParametersInfo( SPI_SETSCREENSAVERRUNNING, 0, &old, 0 );
    	} else {
    		UnregisterHotKey( 0, 0 );
    		UnregisterHotKey( 0, 1 );
    	}
    	win.alttab_disabled = qfalse;
    }
}

static void win_noalttab_changed( cvar_t *self ) {
    if( self->integer ) {
    	Win_DisableAltTab();
    } else {
    	Win_EnableAltTab();
    }
}

/*
=================
Win_Activate
=================
*/
static void Win_Activate( WPARAM wParam ) {
    active_t active;

    if( HIWORD( wParam ) ) {
    	// we don't want to act like we're active if we're minimized
        active = ACT_MINIMIZED;
    } else {
        if( LOWORD( wParam ) ) {
            active = ACT_ACTIVATED;
        } else {
            active = ACT_RESTORED;
        }
    }

    CL_Activate( active );

    if( win_noalttab->integer ) {
    	if( active == ACT_ACTIVATED ) {
    		Win_EnableAltTab();
    	} else {
    		Win_DisableAltTab();
    	}
    }

    if( win.flags & QVF_GAMMARAMP ) {
    	if( active == ACT_ACTIVATED ) {
    		SetDeviceGammaRamp( win.dc, win.gamma_cust );
    	} else {
    		SetDeviceGammaRamp( win.dc, win.gamma_orig );
    	}
    }

    if( win.flags & QVF_FULLSCREEN ) {
    	if( active == ACT_ACTIVATED ) {
    		ShowWindow( win.wnd, SW_RESTORE );
    	} else {
    		ShowWindow( win.wnd, SW_MINIMIZE );
    	}

        if( vid_flip_on_switch->integer ) {
            if( active == ACT_ACTIVATED ) {
                ChangeDisplaySettings( &win.dm, CDS_FULLSCREEN );
            } else {
                ChangeDisplaySettings( NULL, 0 );
            }
        }
    }

    if( active == ACT_ACTIVATED ) {
    	SetForegroundWindow( win.wnd );
    }
}

static LRESULT CALLBACK LowLevelKeyboardProc( int nCode, WPARAM wParam, LPARAM lParam ) {
    PKBDLLHOOKSTRUCT kb = ( PKBDLLHOOKSTRUCT )lParam;
    unsigned key;

    if( nCode != HC_ACTION ) {
    	goto ignore;
    }

    switch( kb->vkCode ) {
    case VK_LWIN:
    	key = K_LWINKEY;
    	break;
    case VK_RWIN:
    	key = K_RWINKEY;
    	break;
    default:
    	goto ignore;
    }

    switch( wParam ) {
    case WM_KEYDOWN:
    	Key_Event( key, qtrue, kb->time );
    	return TRUE;
    case WM_KEYUP:
    	Key_Event( key, qfalse, kb->time );
    	return TRUE;
    default:
    	break;
    }

ignore:
    return CallNextHookEx( NULL, nCode, wParam, lParam );
}

static void win_disablewinkey_changed( cvar_t *self ) {
    if( self->integer ) {
    	if( !iswinnt ) {
    		Com_Printf( "Low-level keyboard hook requires Windows NT\n" );
    		Cvar_Set( "win_disablewinkey", "0" );
    		return;
    	}
    	win.kbdHook = SetWindowsHookEx( WH_KEYBOARD_LL, LowLevelKeyboardProc, hGlobalInstance, 0 );
    	if( !win.kbdHook ) {
    		Com_EPrintf( "Couldn't set low-level keyboard hook, error %#lX\n", GetLastError() );
    		Cvar_Set( "win_disablewinkey", "0" );
    	}
    } else {
    	if( win.kbdHook ) {
    		UnhookWindowsHookEx( win.kbdHook );
    		win.kbdHook = NULL;
    	}
    }
}

static const byte   scantokey[128] = { 
//  0           1           2    		3				4			5				6			7 
//  8           9           A    		B				C			D				E			F 
    0,      	K_ESCAPE,   '1',		'2',			'3',		'4',			'5',		 '6', 
    '7',    	'8',        '9',		'0',			'-',		'=',			K_BACKSPACE,  K_TAB,	// 0 
    'q',    	'w',        'e',		'r',			't',		'y',			'u',		 'i', 
    'o',    	'p',	    '[',		']',			K_ENTER,	K_CTRL,			'a',		 's',		// 1 
    'd',    	'f',	    'g',		'h',			'j',		'k',			'l',		 ';', 
    '\'' ,    	'`',	    K_LSHIFT,	'\\',			'z',		'x',			'c',		 'v',		// 2 
    'b',    	'n',	    'm',		',',			'.',		'/',			K_RSHIFT,	 '*', 
    K_ALT,    	K_SPACE,	K_CAPSLOCK,	K_F1,			K_F2,		K_F3,			K_F4,		 K_F5,		// 3 
    K_F6,    	K_F7,	    K_F8,		K_F9,			K_F10,		K_PAUSE,		K_SCROLLOCK, K_HOME, 
    K_UPARROW,  K_PGUP,	    K_KP_MINUS,	K_LEFTARROW,	K_KP_5,		K_RIGHTARROW,	K_KP_PLUS,	 K_END,		// 4 
    K_DOWNARROW,K_PGDN,     K_INS,		K_DEL,			0,			0,				0,			 K_F11, 
    K_F12,    	0,		    0,			K_LWINKEY,		K_RWINKEY,  K_MENU,			0,			 0,			// 5
    0,    		0,		    0,			0,				0,			0,				0,			 0, 
    0,    		0,		    0,			0,				0,			0,				0,			 0,			// 6 
    0,    		0,		    0,			0,				0,			0,				0,			 0, 
    0,    		0,		    0,			0,				0,			0,				0,			 0			// 7 
};

/*
=======
Win_KeyEvent

Map from windows to quake keynums
=======
*/
static void Win_KeyEvent( WPARAM wParam, LPARAM lParam, qboolean down ) {
    unsigned result;
    unsigned scancode = ( lParam >> 16 ) & 255;
    unsigned is_extended = ( lParam >> 24 ) & 1;

    if( scancode > 127 ) {
    	return;
    }

    result = scantokey[scancode];
    if( !result ) {
    	Com_DPrintf( "%s: unknown scancode: %u\n", __func__, scancode );
        return;
    }

    if( !is_extended ) {
    	switch( result ) {
    	case K_HOME:
    		result = K_KP_HOME;
            break;
    	case K_UPARROW:
    		result = K_KP_UPARROW;
            break;
    	case K_PGUP:
    		result = K_KP_PGUP;
            break;
    	case K_LEFTARROW:
    		result = K_KP_LEFTARROW;
            break;
    	case K_RIGHTARROW:
    		result = K_KP_RIGHTARROW;
            break;
    	case K_END:
    		result = K_KP_END;
            break;
    	case K_DOWNARROW:
    		result = K_KP_DOWNARROW;
            break;
    	case K_PGDN:
    		result = K_KP_PGDN;
            break;
    	case K_INS:
    		result = K_KP_INS;
            break;
    	case K_DEL:
    		result = K_KP_DEL;
            break;
    	case K_LSHIFT:
    		Key_Event( K_SHIFT, down, win.lastMsgTime );
    		Key_Event( K_LSHIFT, down, win.lastMsgTime );
    		return;
    	case K_RSHIFT:
    		Key_Event( K_SHIFT, down, win.lastMsgTime );
    		Key_Event( K_RSHIFT, down, win.lastMsgTime );
    		return;
    	case K_ALT:
    		Key_Event( K_ALT, down, win.lastMsgTime );
    		Key_Event( K_LALT, down, win.lastMsgTime );
    		return;
    	case K_CTRL:
    		Key_Event( K_CTRL, down, win.lastMsgTime );
    		Key_Event( K_LCTRL, down, win.lastMsgTime );
    		return;	
    	}
    } else {
    	switch( result ) {
    	case 0x0D:
    		result = K_KP_ENTER;
            break;
    	case 0x2F:
    		result = K_KP_SLASH;
            break;
    	case 0xAF:
    		result = K_KP_PLUS;
            break;
    	case K_ALT:
    		Key_Event( K_ALT, down, win.lastMsgTime );
    		Key_Event( K_RALT, down, win.lastMsgTime );
    		return;
    	case K_CTRL:
    		Key_Event( K_CTRL, down, win.lastMsgTime );
    		Key_Event( K_RCTRL, down, win.lastMsgTime );
    		return;	
    	}
    }

    Key_Event( result, down, win.lastMsgTime );
}

/*
====================
Win_MainWndProc

main window procedure
====================
*/
LONG WINAPI Win_MainWndProc ( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam ) {
    switch( uMsg ) {
    case WM_MOUSEWHEEL: {
            extern keydest_t Key_GetDest( void );
    		UINT lines;
    		int key;

    		if( ( short )HIWORD( wParam ) > 0 ) {
    			key = K_MWHEELUP;
    		} else {
    			key = K_MWHEELDOWN;
    		}

    		if( Key_GetDest() & KEY_CONSOLE ) {
    			SystemParametersInfo( SPI_GETWHEELSCROLLLINES, 0, &lines, 0 );
    			if( !lines ) {
    				lines = 1;
    			} else if( lines > 6 ) {
    				lines = 6;
    			}
    		} else {
    			lines = 1;
    		}

    		do {
    			Key_Event( key, qtrue, win.lastMsgTime );
    			Key_Event( key, qfalse, win.lastMsgTime );
    		} while( --lines );
    	}
    	break;

    case WM_MOUSEMOVE: {
            int x = ( short )LOWORD( lParam );
            int y = ( short )HIWORD( lParam );

            IN_MouseEvent( x, y );
        }
        // fall through

// this is complicated because Win32 seems to pack multiple mouse events into
// one update sometimes, so we always check all states and look for events
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        if( win.mouse.initialized ) {
    		int	i, mask, temp = 0;

    		if( wParam & MK_LBUTTON )
    			temp |= 1;

    		if( wParam & MK_RBUTTON )
    			temp |= 2;

    		if( wParam & MK_MBUTTON )
    			temp |= 4;

    		if( wParam & MK_XBUTTON1 )
    			temp |= 8;

    		if( wParam & MK_XBUTTON2 )
    			temp |= 16;

            if( temp == win.mouse.state ) {
                break;
            }

            // perform button actions
            for( i = 0, mask = 1; i < MOUSE_BUTTONS; i++, mask <<= 1 ) {
                if( ( temp & mask ) && !( win.mouse.state & mask ) ) {
                    Key_Event( K_MOUSE1 + i, qtrue, win.lastMsgTime );
                }
                if( !( temp & mask ) && ( win.mouse.state & mask ) ) {
                    Key_Event( K_MOUSE1 + i, qfalse, win.lastMsgTime );
                }
            }    
                
            win.mouse.state = temp;
    	}
    	break;

    case WM_HOTKEY:
    	return FALSE;

    case WM_PAINT:
    	SCR_UpdateScreen();
    	break;

    case WM_CLOSE:
    	PostQuitMessage( 0 );
    	return FALSE;

    case WM_ACTIVATE:
    	Win_Activate( wParam );
    	break;

    case WM_SIZING:
    	if( !vid_fullscreen->integer ) {
    	    RECT *rc = ( RECT * )lParam;
    		int w = rc->right - rc->left;
    		int h = rc->bottom - rc->top;
    		if( w < 64 ) w = 64; else w &= ~7;
    		if( h < 64 ) h = 64; else h &= ~1;
    		switch( wParam ) {
    		case WMSZ_BOTTOM:
    			rc->bottom = rc->top + h;
    			break;
    		case WMSZ_BOTTOMLEFT:
    			rc->bottom = rc->top + h;
    			rc->left = rc->right - w;
    			break;
    		case WMSZ_BOTTOMRIGHT:
    			rc->right = rc->left + w;
        		rc->bottom = rc->top + h;
    			break;
    		case WMSZ_LEFT:
    			rc->left = rc->right - w;
    			break;
    		case WMSZ_RIGHT:
    			rc->right = rc->left + w;
    			break;
    		case WMSZ_TOP:
    			rc->top = rc->bottom - h;
    			break;
    		case WMSZ_TOPLEFT:
    			rc->top = rc->bottom - h;
    			rc->left = rc->right - w;
    			break;
    		case WMSZ_TOPRIGHT:
    			rc->top = rc->bottom - h;
    			rc->right = rc->left + w;
    			break;
    		}
    		return TRUE;
        }
    	break;

    case WM_SIZE:
    	if( wParam == SIZE_RESTORED && !vid_fullscreen->integer ) {
    	    int w = ( short )LOWORD( lParam );
        	int h = ( short )HIWORD( lParam );
            win.rc.width = w;
            win.rc.height = h;
            win.mode_changed |= 1;
        }
    	break;

    case WM_MOVE: 
    	if( !vid_fullscreen->integer ) {
            int x = ( short )LOWORD( lParam );
            int y = ( short )HIWORD( lParam );
            RECT    r;
            int    	style;

            r.left   = 0;
            r.top    = 0;
            r.right  = 1;
            r.bottom = 1;

            style = GetWindowLong( hWnd, GWL_STYLE );
            AdjustWindowRect( &r, style, FALSE );

            win.rc.x = x + r.left;
            win.rc.y = y + r.top;

            win.mode_changed |= 2;
        }
        break;

    case WM_SYSCOMMAND:
        switch( wParam & 0xFFF0 ) {
        case SC_SCREENSAVE:
    		return FALSE;
        case SC_MAXIMIZE:
    		if( !vid_fullscreen->integer ) {
                VID_ToggleFullscreen();
    		}
            return FALSE;
    	}
        break;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    	Win_KeyEvent( wParam, lParam, qtrue );
    	return FALSE;

    case WM_SYSCHAR:
    case WM_CHAR:
#if USE_CHAR_EVENTS
    	Key_CharEvent( wParam );
#endif
    	return FALSE;

    case WM_SYSKEYUP:
    case WM_KEYUP:
    	Win_KeyEvent( wParam, lParam, qfalse );
    	return FALSE;

    default:	
        break;
    }

    // pass all unhandled messages to DefWindowProc
    return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

void VID_ModeChanged( void ) {
    Win_SetMode();
    Win_ModeChanged();
}

/*
============
VID_PumpEvents
============
*/
void VID_PumpEvents( void ) {
    MSG        msg;

    while( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) ) {
    	if( msg.message == WM_QUIT ) {
    		Com_Quit();
    		break;
    	}
        win.lastMsgTime = msg.time;
        TranslateMessage( &msg );
        DispatchMessage( &msg );
    }

    if( win.mode_changed ) {
        VID_SetGeometry( &win.rc );
        if( win.mode_changed & 1 ) {
            Win_ModeChanged();
        }
        win.mode_changed = 0;
    }
}

/*
============
Win_Init
============
*/
void Win_Init( void ) {
    WNDCLASSEX		wc;

    // register variables
    vid_flip_on_switch = Cvar_Get( "vid_flip_on_switch", "0", 0 );
    vid_hwgamma = Cvar_Get( "vid_hwgamma", "0", CVAR_ARCHIVE|CVAR_LATCHED );
    win_noalttab = Cvar_Get( "win_noalttab", "0", CVAR_ARCHIVE );
    win_noalttab->changed = win_noalttab_changed;
    win_disablewinkey = Cvar_Get( "win_disablewinkey", "0", CVAR_ARCHIVE );
    win_disablewinkey->changed = win_disablewinkey_changed;

    win_disablewinkey_changed( win_disablewinkey );

    // register the frame class
    memset( &wc, 0, sizeof( wc ) );
    wc.cbSize = sizeof( wc );
    wc.lpfnWndProc = ( WNDPROC )Win_MainWndProc;
    wc.hInstance = hGlobalInstance;
    wc.hIcon = LoadImage( hGlobalInstance, MAKEINTRESOURCE( IDI_APP ),
        IMAGE_ICON, 32, 32, LR_CREATEDIBSECTION );
    wc.hIconSm = LoadImage( hGlobalInstance, MAKEINTRESOURCE( IDI_APP ),
        IMAGE_ICON, 16, 16, LR_CREATEDIBSECTION );
    wc.hCursor = LoadCursor ( NULL, IDC_ARROW );
    wc.hbrBackground = GetStockObject( BLACK_BRUSH );
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if( !RegisterClassEx( &wc ) ) {
        Com_Error( ERR_FATAL, "Couldn't register main window class" );
    }

    // create the window
    win.wnd = CreateWindow(
        WINDOW_CLASS_NAME,
        APPLICATION,
        0, //style
        0, 0, 0, 0,
        NULL,
        NULL,
        hGlobalInstance,
        NULL );

    if( !win.wnd ) {
        Com_Error( ERR_FATAL, "Couldn't create main window" );
    }

    win.dc = GetDC( win.wnd );
    if( !win.dc ) {
        Com_Error( ERR_FATAL, "Couldn't get DC of the main window" );
    }

    // init gamma ramp
    if( vid_hwgamma->integer ) {
        if( GetDeviceGammaRamp( win.dc, win.gamma_orig ) ) {
            Com_DPrintf( "...enabling hardware gamma\n" );
    	    win.flags |= QVF_GAMMARAMP;
    	    memcpy( win.gamma_cust, win.gamma_orig, sizeof( win.gamma_cust ) );
        } else {
            Com_DPrintf( "...hardware gamma not supported\n" );
            Cvar_Set( "vid_hwgamma", "0" );
        }
    }
}

/*
============
Win_Shutdown
============
*/
void Win_Shutdown( void ) {
    if( win.flags & QVF_GAMMARAMP ) {
        SetDeviceGammaRamp( win.dc, win.gamma_orig );
    }

    // prevents leaving empty slots in the taskbar
    ShowWindow( win.wnd, SW_SHOWNORMAL );    
    ReleaseDC( win.wnd, win.dc );
    DestroyWindow( win.wnd );
    UnregisterClass( WINDOW_CLASS_NAME, hGlobalInstance );

    if( win.kbdHook ) {
    	UnhookWindowsHookEx( win.kbdHook );
    }

    if( win.flags & QVF_FULLSCREEN ) {
    	ChangeDisplaySettings( 0, 0 );
    }

    memset( &win, 0, sizeof( win ) );
}

/*
===============================================================================

MOUSE

===============================================================================
*/

// mouse variables
static cvar_t    *win_xpfix;

static void Win_HideCursor( void ) {
    while( ShowCursor( FALSE ) >= 0 )
    	;
}

static void Win_ShowCursor( void ) {
    while( ShowCursor( TRUE ) < 0 )
    	;
}

/*
===========
Win_AcquireMouse

Called when the window gains focus or changes in some way
===========
*/
static void Win_AcquireMouse( void ) {
    RECT rc;
    int parms[3];

    if( win.mouse.parmsvalid ) {
        if( win_xpfix->integer ) {
            parms[0] = parms[1] = parms[2] = 0;
        } else {
            parms[0] = parms[1] = 0;
            parms[2] = 1;
        }
    	win.mouse.restoreparms = SystemParametersInfo(
            SPI_SETMOUSE, 0, parms, 0 );
    }

    GetWindowRect( win.wnd, &rc );
    
    win.center_x = ( rc.right + rc.left ) / 2;
    win.center_y = ( rc.top + rc.bottom ) / 2;

    SetCursorPos( win.center_x, win.center_y );

    SetCapture( win.wnd );
    ClipCursor( &rc );

    SetWindowText( win.wnd, "[" APPLICATION "]" );
}


/*
===========
Win_DeAcquireMouse

Called when the window loses focus
===========
*/
static void Win_DeAcquireMouse( void ) {
    if( win.mouse.restoreparms )
        SystemParametersInfo( SPI_SETMOUSE, 0, win.mouse.originalparms, 0 );

    ClipCursor( NULL );
    ReleaseCapture();

    SetWindowText( win.wnd, APPLICATION );
}

static void win_xpfix_changed( cvar_t *self ) {
    if( win.mouse.grabbed == IN_GRAB ) {
        Win_AcquireMouse();
    }
}

/*
===========
Win_GetMouseMotion
===========
*/
static qboolean Win_GetMouseMotion( int *dx, int *dy ) {
    POINT    pt;

    if( win.mouse.grabbed != IN_GRAB ) {
    	return qfalse;
    }

    // find mouse movement
    if( !GetCursorPos( &pt ) ) {
    	return qfalse;
    }

    *dx = pt.x - win.center_x;
    *dy = pt.y - win.center_y;

    // force the mouse to the center, so there's room to move
    SetCursorPos( win.center_x, win.center_y );
    return qtrue;
}

/*
===========
Win_ShutdownMouse
===========
*/
static void Win_ShutdownMouse( void ) {
    Win_DeAcquireMouse();
    Win_ShowCursor();
    memset( &win.mouse, 0, sizeof( win.mouse ) );
}

/*
===========
Win_StartupMouse
===========
*/
static qboolean Win_InitMouse( void ) {
    if( !win.wnd ) {
    	return qfalse;
    }

    win_xpfix = Cvar_Get( "win_xpfix", "0", 0 );
    win_xpfix->changed = win_xpfix_changed;

    win.mouse.initialized = qtrue;
    win.mouse.parmsvalid = SystemParametersInfo( SPI_GETMOUSE, 0,
        win.mouse.originalparms, 0 );

    return qtrue;
}

/*
===========
Win_GrabMouse

Called when the main window gains or loses focus.
The window may have been destroyed and recreated
between a deactivate and an activate.
===========
*/
static void Win_GrabMouse( grab_t grab ) {
    if( !win.mouse.initialized ) {
    	return;
    }
    if( win.mouse.grabbed == grab ) {
    	return;
    }

    if( grab == IN_GRAB ) {
    	Win_AcquireMouse();
        Win_HideCursor();
    } else {
        if( win.mouse.grabbed == IN_GRAB ) {
            Win_DeAcquireMouse();
        }
        if( grab == IN_HIDE ) {
            Win_HideCursor();
        } else {
            Win_ShowCursor();
        }
    }

    win.mouse.state = 0;
    win.mouse.grabbed = grab;
}

/*
@@@@@@@@@@@@@@@@@@@
VID_FillInputAPI
@@@@@@@@@@@@@@@@@@@
*/
void VID_FillInputAPI( inputAPI_t *api ) {
    api->Init = Win_InitMouse;
    api->Shutdown = Win_ShutdownMouse;
    api->Grab = Win_GrabMouse;
    api->GetEvents = NULL;
    api->GetMotion = Win_GetMouseMotion;
}

