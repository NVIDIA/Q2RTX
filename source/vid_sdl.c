/*
Copyright (C) 2003-2005 Andrey Nazarov

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
// vid_sdl.c
//

#include "com_local.h"
#include "in_public.h"
#include "vid_public.h"
#include "vid_local.h"
#include "ref_public.h"
#include "key_public.h"
#include "q2pro.xbm"
#include <SDL.h>

typedef struct {
    SDL_Surface     *surface;
    SDL_Surface     *icon;
    Uint16		    gamma[3][256];
    vidFlags_t	    flags;
    qboolean		mouseactive;	// false when not focus app
    qboolean		mouseinitialized;
} sdl_state_t;

static sdl_state_t    sdl;

void QSDL_AcquireMouse( void );

/*
===============================================================================

COMMON SDL VIDEO RELATED ROUTINES

===============================================================================
*/

static qboolean QSDL_SetMode( int flags, int forcedepth ) {
    SDL_Surface *surf;
    vrect_t rc;
    int depth;

    flags &= ~(SDL_FULLSCREEN|SDL_RESIZABLE);
	sdl.flags &= ~QVF_FULLSCREEN;

    if( vid_fullscreen->integer > 0 ) {
        Video_GetModeFS( &rc, NULL, &depth );
        if( forcedepth ) {
            depth = forcedepth;
        }
        Com_DPrintf( "...setting fullscreen mode: %dx%d:%d\n",
            rc.width, rc.height, depth );
        surf = SDL_SetVideoMode( rc.width, rc.height, depth,
            flags | SDL_FULLSCREEN );
		if( surf ) {
			sdl.flags |= QVF_FULLSCREEN;
            goto success;
        }
		Com_EPrintf( "FS video mode failed: %s\n", SDL_GetError() );
        Cvar_Set( "vid_fullscreen", "0" );
    }

    flags |= SDL_RESIZABLE;
    Video_GetPlacement( &rc );
    Com_DPrintf( "...setting windowed mode: %dx%d\n", rc.width, rc.height );
    surf = SDL_SetVideoMode( rc.width, rc.height, forcedepth, flags );
    if( !surf ) {
        return qfalse;
    }
	
success:
    sdl.surface = surf;
    ref.ModeChanged( rc.width, rc.height, sdl.flags, surf->pitch, surf->pixels );
    SCR_ModeChanged();
    return qtrue;
}

void Video_ModeChanged( void ) {
	SDL_Event	event;

    if( !QSDL_SetMode( sdl.surface->flags, sdl.surface->format->BitsPerPixel ) ) {
        Com_Error( ERR_FATAL, "Couldn't change video mode: %s", SDL_GetError() );
    }

	while( SDL_PollEvent( &event ) )
        ;
}

static qboolean QSDL_InitVideo( void ) {
	SDL_Color	color;
	byte *dst;
	char buffer[MAX_QPATH];
	int i, ret;

	if( SDL_WasInit( SDL_INIT_EVERYTHING ) == 0 ) {
		ret = SDL_Init( SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE );
	} else {
		ret = SDL_InitSubSystem( SDL_INIT_VIDEO );
	}

	if( ret == -1 ) {
		Com_EPrintf( "Couldn't initialize SDL video: %s\n", SDL_GetError() );
		return qfalse;
	}

	if( SDL_VideoDriverName( buffer, sizeof( buffer ) ) != NULL ) {
		Com_Printf( "Using SDL video driver: %s\n", buffer );
	}

    sdl.icon = SDL_CreateRGBSurface( SDL_SWSURFACE, q2icon_width,
        q2icon_height, 8, 0, 0, 0, 0 );
    if( sdl.icon ) {
        SDL_SetColorKey( sdl.icon, SDL_SRCCOLORKEY, 0 );

        // transparent pixel
        color.r = 255; color.g = 255; color.b = 255;
        SDL_SetColors( sdl.icon, &color, 0, 1 );

        // colored pixel
        color.r =   0; color.g = 128; color.b = 128;
        SDL_SetColors( sdl.icon, &color, 1, 1 );

        // expand the bitmap
        dst = sdl.icon->pixels;
        for( i = 0; i < q2icon_width * q2icon_height; i++ ) {
            *dst++ = Q_IsBitSet( q2icon_bits, i );
        }

        SDL_WM_SetIcon( sdl.icon, NULL );
    }

	if( SDL_GetGammaRamp( sdl.gamma[0], sdl.gamma[1], sdl.gamma[2] ) != -1 ) {
	    Com_DPrintf( "...enabling gamma control\n" );
		sdl.flags |= QVF_GAMMARAMP;
	}

	SDL_WM_SetCaption( APPLICATION, APPLICATION );

	SDL_EnableKeyRepeat( SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL );

	return qtrue;
}

static void QSDL_ShutdownVideo( void ) {
	if( sdl.flags & QVF_GAMMARAMP ) {
		SDL_SetGammaRamp( sdl.gamma[0], sdl.gamma[1], sdl.gamma[2] );
	}

	if( sdl.icon ) {
		SDL_FreeSurface( sdl.icon );
	}

    memset( &sdl, 0, sizeof( sdl ) );

	if( SDL_WasInit( SDL_INIT_EVERYTHING ) == SDL_INIT_VIDEO ) {
		SDL_Quit();
	} else {
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
	}
}

static void QSDL_UpdateGamma( const byte *table ) {
	Uint16 ramp[256];
	int i;

	if( sdl.flags & QVF_GAMMARAMP ) {
        for( i = 0; i < 256; i++ ) {
            ramp[i] = table[i] << 8;
        }
        SDL_SetGammaRamp( ramp, ramp, ramp );
    }
}

static void QSDL_KeyEvent( SDL_keysym *keysym, qboolean down ) {
	uint32 key;

    if( keysym->sym <= 127 ) {
        // ASCII chars are mapped directly
	    Key_Event( keysym->sym, down, com_eventTime );
        return;
    }

#define K( s, d )	case SDLK_ ## s: key = K_ ## d; break;

	switch( keysym->sym ) {
	K( KP0,			KP_INS		    )
	K( KP1,			KP_END		    )
	K( KP2,			KP_DOWNARROW	)
	K( KP3,			KP_PGDN		    )
	K( KP4,			KP_LEFTARROW	)
	K( KP5,			KP_5			)
	K( KP6,			KP_RIGHTARROW	)
	K( KP7,			KP_HOME		    )
	K( KP8,			KP_UPARROW	    )
	K( KP9,			KP_PGUP		    )
	K( KP_PERIOD,	KP_DEL		    )
	K( KP_DIVIDE,	KP_SLASH		)
	K( KP_MULTIPLY, KP_MULTIPLY     )
	K( KP_MINUS,	KP_MINUS		)
	K( KP_PLUS,		KP_PLUS		    )
	K( KP_ENTER,	KP_ENTER		)

	K( UP,			UPARROW		)
	K( DOWN,		DOWNARROW	)
	K( RIGHT,		RIGHTARROW	)
	K( LEFT,		LEFTARROW	)
	K( INSERT,		INS			)
	K( HOME,		HOME		)
	K( END,			END			)
	K( PAGEUP,		PGUP		)
	K( PAGEDOWN,	PGDN		)

	K( F1,      F1  )
	K( F2,	    F2	)
	K( F3,	    F3	)
	K( F4,	    F4	)
	K( F5,	    F5	)
	K( F6,	    F6	)
	K( F7,	    F7	)
	K( F8,	    F8	)
	K( F9,	    F9	)
	K( F10,     F10	)
	K( F11,	    F11	)
	K( F12,	    F12	)

	K( NUMLOCK,		NUMLOCK		)
	K( CAPSLOCK,	CAPSLOCK	)
	K( SCROLLOCK,	SCROLLOCK   )
	K( LSUPER,		LWINKEY		)
	K( RSUPER,		RWINKEY		)
	K( MENU,		MENU		)

	K( RSHIFT,		SHIFT   )
	K( LSHIFT,		SHIFT	)
	K( RCTRL,		CTRL	)
	K( LCTRL,		CTRL	)
	K( RALT,		ALT		)
	K( LALT,		ALT		)

#undef K

	default:
		Com_DPrintf( "%s: unknown keysym %d\n", __func__, keysym->sym );
        return;
	}

	Key_Event( key, down, com_eventTime );
}

static void QSDL_MouseButtonEvent( int button, qboolean down ) {
    uint32 key;

    if( !sdl.mouseinitialized ) {
        return;
    }

    if( !sdl.mouseactive ) {
        QSDL_AcquireMouse();
        sdl.mouseactive = qtrue;
        return;
    }

#define K( s, d )	case SDL_BUTTON_ ## s: key = K_ ## d; break;

    switch( button ) {
    K( LEFT,        MOUSE1      )
    K( RIGHT,       MOUSE2      )
    K( MIDDLE,      MOUSE3      )
    K( WHEELUP,     MWHEELUP    )
    K( WHEELDOWN,   MWHEELDOWN  )

#undef K

    default:
		Com_DPrintf( "%s: unknown button %d\n", __func__, button );
        return;
    }

    Key_Event( key, down, com_eventTime );
}

/*
============
Video_PumpEvents
============
*/
void Video_PumpEvents( void ) {
	SDL_Event	event;

	while( SDL_PollEvent( &event ) ) {
		switch( event.type ) {
		case SDL_ACTIVEEVENT:
			// state is actually a bitmask!
			if( event.active.state & SDL_APPACTIVE ) {
				if( event.active.gain ) {
					CL_AppActivate( qtrue );
				} else {
					CL_AppActivate( qfalse );
				}
			}
			break;

		case SDL_QUIT:
			Com_Quit();
			break;

        case SDL_VIDEORESIZE:
            if( sdl.surface->flags & SDL_RESIZABLE ) {
                event.resize.w &= ~7;
                event.resize.h &= ~1;
                Cvar_Set( "vid_placement", va( "%dx%d",
                    event.resize.w, event.resize.h ) );
                Video_ModeChanged();
                return;
            }
            break;

		case SDL_KEYDOWN:
			QSDL_KeyEvent( &event.key.keysym, qtrue );
			break;

		case SDL_KEYUP:
			QSDL_KeyEvent( &event.key.keysym, qfalse );
			break;

		case SDL_MOUSEBUTTONDOWN:
            QSDL_MouseButtonEvent( event.button.button, qtrue );
            break;

		case SDL_MOUSEBUTTONUP:
            QSDL_MouseButtonEvent( event.button.button, qfalse );
            break;
		}
	}
}


/*
===============================================================================

OPENGL SPECIFIC

===============================================================================
*/

static qboolean QSDL_InitGL( void ) {
	cvar_t *gl_driver;

	if( !QSDL_InitVideo() ) {
		return qfalse;
	}

	gl_driver = Cvar_Get( "gl_driver", DEFAULT_OPENGL_DRIVER, CVAR_LATCH );

	if( SDL_GL_LoadLibrary( gl_driver->string ) == -1 ) {
		Com_EPrintf( "Couldn't load OpenGL library: %s\n", SDL_GetError() );
        goto fail;
	}

	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 5 );
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 5 );
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 5 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 16 );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

	sdl.flags |= QVF_ACCELERATED;

    if( !QSDL_SetMode( SDL_OPENGL|SDL_RESIZABLE, 0 ) ) {
        Com_EPrintf( "Couldn't set video mode: %s\n", SDL_GetError() );
        goto fail;
    }

	CL_AppActivate( qtrue );
	return qtrue;

fail:
    QSDL_ShutdownVideo();
	return qfalse;
}

static void QSDL_BeginFrameGL( void ) {
}

static void QSDL_EndFrameGL( void ) {
	SDL_GL_SwapBuffers();
}

void Video_FillGLAPI( videoAPI_t *api ) {
	api->Init = QSDL_InitGL;
	api->Shutdown = QSDL_ShutdownVideo;
	api->UpdateGamma = QSDL_UpdateGamma;
    api->UpdatePalette = NULL;
	api->GetProcAddr = SDL_GL_GetProcAddress;
	api->BeginFrame = QSDL_BeginFrameGL;
	api->EndFrame = QSDL_EndFrameGL;
}

/*
===============================================================================

SOFTWARE SPECIFIC

===============================================================================
*/

#ifndef REF_HARD_LINKED

static qboolean QSDL_InitSoft( void ) {
	if( !QSDL_InitVideo() ) {
		return qfalse;
	}

    if( !QSDL_SetMode( SDL_SWSURFACE|SDL_HWPALETTE|SDL_RESIZABLE, 8 ) ) {
        Com_EPrintf( "Couldn't set video mode: %s\n", SDL_GetError() );
        QSDL_ShutdownVideo();
        return qfalse;
    }

	CL_AppActivate( qtrue );
	return qtrue;
}

static void QSDL_UpdatePalette( const byte *palette ) {
	SDL_Color	colors[256];
	SDL_Color	*c;

	for( c = colors; c < colors + 256; c++ ) {
		c->r = palette[0];
		c->g = palette[1];
		c->b = palette[2];
		palette += 4;
	}

	SDL_SetPalette( sdl.surface, SDL_LOGPAL, colors, 0, 256 );
}

static void QSDL_BeginFrameSoft( void ) {
	 SDL_LockSurface( sdl.surface );
}

static void QSDL_EndFrameSoft( void ) {
	SDL_UnlockSurface( sdl.surface );
	SDL_Flip( sdl.surface );
}

void Video_FillSWAPI( videoAPI_t *api ) {
	api->Init = QSDL_InitSoft;
	api->Shutdown = QSDL_ShutdownVideo;
	api->UpdateGamma = QSDL_UpdateGamma;
	api->UpdatePalette = QSDL_UpdatePalette;
	api->GetProcAddr = NULL;
	api->BeginFrame = QSDL_BeginFrameSoft;
	api->EndFrame = QSDL_EndFrameSoft;
}

#endif // !REF_HARD_LINKED

/*
===============================================================================

SDL MOUSE DRIVER

===============================================================================
*/

#define SDL_FULLFOCUS  (SDL_APPACTIVE|SDL_APPINPUTFOCUS|SDL_APPMOUSEFOCUS)

void QSDL_AcquireMouse( void ) {
    uint32 state;

	// move cursor to center of the main window before we grab the mouse
	if( sdl.surface ) {
		SDL_WarpMouse( sdl.surface->w / 2, sdl.surface->h / 2 );
	}

	// pump mouse motion events generated by SDL_WarpMouse
	SDL_PollEvent( NULL );

	// grab the mouse, so SDL enters relative mouse mode
	SDL_WM_GrabInput( SDL_GRAB_ON );
    state = SDL_GetAppState();
    if( ( state & SDL_FULLFOCUS ) != SDL_FULLFOCUS ) {
        Com_DPrintf( "QSDL_AcquireMouse: don't have full focus\n" );
    }
	SDL_ShowCursor( SDL_DISABLE );
	
	// pump mouse motion events still pending
	SDL_PollEvent( NULL );

	// clear any deltas generated
	SDL_GetRelativeMouseState( NULL, NULL );
}

static void QSDL_DeAcquireMouse( void ) {
	// release the mouse
	SDL_ShowCursor( SDL_ENABLE );
	SDL_WM_GrabInput( SDL_GRAB_OFF );
}

static void QSDL_SendMouseMoveEvents( void ) {
	int		dx, dy;

	if( !sdl.mouseinitialized ) {
		return;
	}

	if( !sdl.mouseactive ) {
		return;
	}

	SDL_GetRelativeMouseState( &dx, &dy );

	if( dx || dy ) {
	    CL_MouseEvent( dx, dy );
    }
}

static void QSDL_ShutdownMouse( void ) {
	QSDL_DeAcquireMouse();
	sdl.mouseactive = qfalse;
	sdl.mouseinitialized = qfalse;
}

static qboolean QSDL_InitMouse( void ) {
	if( SDL_WasInit( SDL_INIT_VIDEO ) != SDL_INIT_VIDEO ) {
		return qfalse;
	}

	sdl.mouseinitialized = qtrue;

	return qtrue;
}

static void QSDL_ActivateMouse( qboolean active ) {
	if( !sdl.mouseinitialized ) {
		return;
	}

	if( sdl.mouseactive == active ) {
	//	return;
	}

	if( active ) {
		QSDL_AcquireMouse();
	} else {
		QSDL_DeAcquireMouse();
	}

	sdl.mouseactive = active;
}

static void QSDL_ClearMouseStates( void ) {
	// no work here
}

void Video_FillInputAPI( inputAPI_t *api ) {
	api->Init = QSDL_InitMouse;
	api->Shutdown = QSDL_ShutdownMouse;
	api->Activate = QSDL_ActivateMouse;
	api->Frame = QSDL_SendMouseMoveEvents;
	api->ClearStates = QSDL_ClearMouseStates;
}

