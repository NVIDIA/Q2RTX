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

// Main windowed and fullscreen graphics interface module. This module
// is used for both the software and OpenGL rendering versions of the
// Quake refresh engine.


#include "cl_local.h"
#include "vid_public.h"
#include "in_public.h"
#include "vid_local.h"

// Console variables that we need to access from this module
cvar_t		*vid_ref;			// Name of Refresh DLL loaded
cvar_t      *vid_geometry;
cvar_t      *vid_modelist;
cvar_t      *vid_fullscreen;
cvar_t      *_vid_fullscreen;

#define MODE_GEOMETRY   1
#define MODE_FULLSCREEN 2
#define MODE_MODELIST   4

static int  mode_changed;

/*
==========================================================================

HELPER FUNCTIONS

==========================================================================
*/

// 640x480 800x600 1024x768
// 640x480@75
// 640x480@75:32
// 640x480:32@75
void VID_GetModeFS( vrect_t *rc, int *freq, int *depth ) {
    char *s = vid_modelist->string;
    int mode = 1;
    int w = 640, h = 480, hz = 0, bpp = 0;

    while( *s ) {
        w = strtoul( s, &s, 10 );
        if( *s != 'x' ) {
            Com_DPrintf( "Mode %d is malformed\n", mode );
            goto malformed;
        }
        h = strtoul( s + 1, &s, 10 );
        if( *s == '@' ) {
            hz = strtoul( s + 1, &s, 10 );
            if( *s == ':' ) {
                bpp = strtoul( s + 1, &s, 10 );
            }
        } else if( *s == ':' ) {
            bpp = strtoul( s + 1, &s, 10 );
            if( *s == '@' ) {
                hz = strtoul( s + 1, &s, 10 );
            }
        }
        if( mode == vid_fullscreen->integer ) {
            break;
        }
        if( *s == 0 ) {
            Com_DPrintf( "Mode %d not found\n", vid_fullscreen->integer );
            break;
        }
        s++;
        mode++;
    }

    // sanity check
    if( w < 64 || w > 8192 || h < 64 || h > 8192 ) {
        Com_DPrintf( "Mode %dx%d doesn't look sane\n", w, h );
malformed:
        w = 640;
        h = 480;
        hz = 0;
        bpp = 0;
    }

    rc->x = 0;
    rc->y = 0;
    rc->width = w;
    rc->height = h;

    if( freq ) {
        *freq = hz;
    }
    if( depth ) {
        *depth = bpp;
    }
}

// 640x480
// 640x480+0
// 640x480+0+0
void VID_GetGeometry( vrect_t *rc ) {
    char *s = vid_geometry->string;
    int w = 640, h = 480, x = 0, y = 0;

    w = strtoul( s, &s, 10 );
    if( *s != 'x' ) {
        Com_DPrintf( "Geometry string is malformed\n" );
        goto malformed;
    }
    h = strtoul( s + 1, &s, 10 );
    if( *s == '+' ) {
        x = strtoul( s + 1, &s, 10 );
        if( *s == '+' ) {
            y = strtoul( s + 1, &s, 10 );
        }
    }

    // sanity check
    if( x < 0 || x > 8192 || y < 0 || y > 8192 ) {
        x = 0;
        y = 0;
    }
    if( w < 64 || w > 8192 || h < 64 || h > 8192 ) {
malformed:
        w = 640;
        h = 480;
    }

    rc->x = x;
    rc->y = y;
    rc->width = w;
    rc->height = h;
}


void VID_SetGeometry( vrect_t *rc ) {
    char buffer[MAX_QPATH];

    Q_snprintf( buffer, sizeof( buffer ), "%dx%d+%d+%d",
        rc->width, rc->height, rc->x, rc->y );
    Cvar_SetByVar( vid_geometry, buffer, CVAR_SET_DIRECT );
}

void VID_ToggleFullscreen( void ) {
    if( !vid_fullscreen->integer ) {
        if( !_vid_fullscreen->integer ) {
            Cvar_Set( "_vid_fullscreen", "1" );
        }
        Cbuf_AddText( "set vid_fullscreen $_vid_fullscreen\n" );
    } else {
        Cbuf_AddText( "set vid_fullscreen 0\n" );
    }
}

/*
==========================================================================

LOADING / SHUTDOWN

==========================================================================
*/

/*
============
CL_RunResfresh
============
*/
void CL_RunRefresh( void ) {
	if( !cls.ref_initialized ) {
        return;
    }
    
	VID_PumpEvents();

    if( mode_changed ) {
        if( mode_changed & MODE_FULLSCREEN ) {
			if( vid_fullscreen->integer ) {
                Cvar_Set( "_vid_fullscreen", vid_fullscreen->string );
			}
            VID_SetMode();
        } else {
            if( vid_fullscreen->integer ) {
                if( mode_changed & MODE_MODELIST ) {
                    VID_SetMode();
                }
            } else {
                if( mode_changed & MODE_GEOMETRY ) {
                    VID_SetMode();
                }
            }
        }
        mode_changed = 0;
    }
}

static void vid_geometry_changed( cvar_t *self ) {
    mode_changed |= MODE_GEOMETRY;
}

static void vid_fullscreen_changed( cvar_t *self ) {
    mode_changed |= MODE_FULLSCREEN;
}

static void vid_modelist_changed( cvar_t *self ) {
    mode_changed |= MODE_MODELIST;
}

/*
============
CL_InitRefresh
============
*/
void CL_InitRefresh( void ) {
	if( cls.ref_initialized ) {
		return;
	}

	// Create the video variables so we know how to start the graphics drivers
	vid_ref = Cvar_Get( "vid_ref", VID_REF, CVAR_ROM );
    vid_fullscreen = Cvar_Get( "vid_fullscreen", "0", CVAR_ARCHIVE );
    _vid_fullscreen = Cvar_Get( "_vid_fullscreen", "1", CVAR_ARCHIVE );
    vid_modelist = Cvar_Get( "vid_modelist", VID_MODELIST, CVAR_ARCHIVE );
    vid_geometry = Cvar_Get( "vid_geometry", VID_GEOMETRY, CVAR_ARCHIVE );

    if( vid_fullscreen->integer ) {
        Cvar_Set( "_vid_fullscreen", vid_fullscreen->string );
    } else if( !_vid_fullscreen->integer ) {
        Cvar_Set( "_vid_fullscreen", "1" );
    }

	if( !R_Init( qtrue ) ) {
		Com_Error( ERR_FATAL, "Couldn't initialize refresh" );
	}

	cls.ref_initialized = qtrue;
    
    vid_geometry->changed = vid_geometry_changed;
    vid_fullscreen->changed = vid_fullscreen_changed;
    vid_modelist->changed = vid_modelist_changed;

    mode_changed = 0;

    // Initialize the rest of graphics subsystems
    V_Init();
    SCR_Init();
#if USE_UI
    UI_Init();
#endif

    SCR_RegisterMedia();
    Con_RegisterMedia();
}

/*
============
CL_ShutdownRefresh
============
*/
void CL_ShutdownRefresh( void ) {
	if( !cls.ref_initialized ) {
		return;
	}

    // Shutdown the rest of graphics subsystems
    V_Shutdown();
    SCR_Shutdown();
#if USE_UI
    UI_Shutdown();
#endif

    vid_geometry->changed = NULL;
    vid_fullscreen->changed = NULL;
    vid_modelist->changed = NULL;

	R_Shutdown( qtrue );

	cls.ref_initialized = qfalse;

	Z_LeakTest( TAG_RENDERER );
}

