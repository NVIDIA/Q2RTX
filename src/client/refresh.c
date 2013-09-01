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

// Main windowed and fullscreen graphics interface module. This module
// is used for both the software and OpenGL rendering versions of the
// Quake refresh engine.


#include "client.h"

// Console variables that we need to access from this module
cvar_t      *vid_ref;           // Name of Refresh DLL loaded
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
qboolean VID_GetFullscreen(vrect_t *rc, int *freq_p, int *depth_p)
{
    unsigned long w, h, freq, depth;
    char *s;
    int mode;

    // fill in default parameters
    rc->x = 0;
    rc->y = 0;
    rc->width = 640;
    rc->height = 480;

    if (freq_p)
        *freq_p = 0;
    if (depth_p)
        *depth_p = 0;

    if (!vid_modelist || !vid_fullscreen)
        return qfalse;

    s = vid_modelist->string;
    while (Q_isspace(*s))
        s++;
    if (!*s)
        return qfalse;

    mode = 1;
    while (1) {
        if (!strncmp(s, "desktop", 7)) {
            s += 7;
            if (*s && !Q_isspace(*s)) {
                Com_DPrintf("Mode %d is malformed\n", mode);
                return qfalse;
            }
            w = h = freq = depth = 0;
        } else {
            w = strtoul(s, &s, 10);
            if (*s != 'x' && *s != 'X') {
                Com_DPrintf("Mode %d is malformed\n", mode);
                return qfalse;
            }
            h = strtoul(s + 1, &s, 10);
            freq = depth = 0;
            if (*s == '@') {
                freq = strtoul(s + 1, &s, 10);
                if (*s == ':') {
                    depth = strtoul(s + 1, &s, 10);
                }
            } else if (*s == ':') {
                depth = strtoul(s + 1, &s, 10);
                if (*s == '@') {
                    freq = strtoul(s + 1, &s, 10);
                }
            }
        }
        if (mode == vid_fullscreen->integer) {
            break;
        }
        while (Q_isspace(*s))
            s++;
        if (!*s) {
            Com_DPrintf("Mode %d not found\n", vid_fullscreen->integer);
            return qfalse;
        }
        mode++;
    }

    // sanity check
    if (w < 64 || w > 8192 || h < 64 || h > 8192 || freq > 1000 || depth > 32) {
        Com_DPrintf("Mode %lux%lu@%lu:%lu doesn't look sane\n", w, h, freq, depth);
        return qfalse;
    }

    rc->width = w;
    rc->height = h;

    if (freq_p)
        *freq_p = freq;
    if (depth_p)
        *depth_p = depth;

    return qtrue;
}

// 640x480
// 640x480+0
// 640x480+0+0
// 640x480-100-100
qboolean VID_GetGeometry(vrect_t *rc)
{
    unsigned long w, h;
    long x, y;
    char *s;

    // fill in default parameters
    rc->x = 0;
    rc->y = 0;
    rc->width = 640;
    rc->height = 480;

    if (!vid_geometry)
        return qfalse;

    s = vid_geometry->string;
    if (!*s)
        return qfalse;

    w = strtoul(s, &s, 10);
    if (*s != 'x' && *s != 'X') {
        Com_DPrintf("Geometry string is malformed\n");
        return qfalse;
    }
    h = strtoul(s + 1, &s, 10);
    x = y = 0;
    if (*s == '+' || *s == '-') {
        x = strtol(s, &s, 10);
        if (*s == '+' || *s == '-') {
            y = strtol(s, &s, 10);
        }
    }

    // sanity check
    if (w < 64 || w > 8192 || h < 64 || h > 8192) {
        Com_DPrintf("Geometry %lux%lu doesn't look sane\n", w, h);
        return qfalse;
    }

    rc->x = x;
    rc->y = y;
    rc->width = w;
    rc->height = h;

    return qtrue;
}

void VID_SetGeometry(vrect_t *rc)
{
    char buffer[MAX_QPATH];

    if (!vid_geometry)
        return;

    Q_snprintf(buffer, sizeof(buffer), "%dx%d%+d%+d",
               rc->width, rc->height, rc->x, rc->y);
    Cvar_SetByVar(vid_geometry, buffer, FROM_CODE);
}

void VID_ToggleFullscreen(void)
{
    if (!vid_fullscreen || !_vid_fullscreen)
        return;

    if (!vid_fullscreen->integer) {
        if (!_vid_fullscreen->integer) {
            Cvar_Set("_vid_fullscreen", "1");
        }
        Cbuf_AddText(&cmd_buffer, "set vid_fullscreen $_vid_fullscreen\n");
    } else {
        Cbuf_AddText(&cmd_buffer, "set vid_fullscreen 0\n");
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
void CL_RunRefresh(void)
{
    if (!cls.ref_initialized) {
        return;
    }

    VID_PumpEvents();

    if (mode_changed) {
        if (mode_changed & MODE_FULLSCREEN) {
            VID_SetMode();
            if (vid_fullscreen->integer) {
                Cvar_Set("_vid_fullscreen", vid_fullscreen->string);
            }
        } else {
            if (vid_fullscreen->integer) {
                if (mode_changed & MODE_MODELIST) {
                    VID_SetMode();
                }
            } else {
                if (mode_changed & MODE_GEOMETRY) {
                    VID_SetMode();
                }
            }
        }
        mode_changed = 0;
    }

    if (cvar_modified & CVAR_REFRESH) {
        CL_RestartRefresh(qtrue);
        cvar_modified &= ~CVAR_REFRESH;
    } else if (cvar_modified & CVAR_FILES) {
        CL_RestartRefresh(qfalse);
        cvar_modified &= ~CVAR_FILES;
    }
}

static void vid_geometry_changed(cvar_t *self)
{
    mode_changed |= MODE_GEOMETRY;
}

static void vid_fullscreen_changed(cvar_t *self)
{
    mode_changed |= MODE_FULLSCREEN;
}

static void vid_modelist_changed(cvar_t *self)
{
    mode_changed |= MODE_MODELIST;
}

/*
============
CL_InitRefresh
============
*/
void CL_InitRefresh(void)
{
    char *modelist;

    if (cls.ref_initialized) {
        return;
    }

    Com_SetLastError(NULL);

    modelist = VID_GetDefaultModeList();
    if (!modelist) {
        Com_Error(ERR_FATAL, "Couldn't initialize refresh: %s", Com_GetLastError());
    }

    // Create the video variables so we know how to start the graphics drivers
    vid_ref = Cvar_Get("vid_ref", VID_REF, CVAR_ROM);
    vid_fullscreen = Cvar_Get("vid_fullscreen", "0", CVAR_ARCHIVE);
    _vid_fullscreen = Cvar_Get("_vid_fullscreen", "1", CVAR_ARCHIVE);
    vid_modelist = Cvar_Get("vid_modelist", modelist, 0);
    vid_geometry = Cvar_Get("vid_geometry", VID_GEOMETRY, CVAR_ARCHIVE);

    Z_Free(modelist);

    if (vid_fullscreen->integer) {
        Cvar_Set("_vid_fullscreen", vid_fullscreen->string);
    } else if (!_vid_fullscreen->integer) {
        Cvar_Set("_vid_fullscreen", "1");
    }

    Com_SetLastError(NULL);

    if (!R_Init(qtrue)) {
        Com_Error(ERR_FATAL, "Couldn't initialize refresh: %s", Com_GetLastError());
    }

    cls.ref_initialized = qtrue;

    vid_geometry->changed = vid_geometry_changed;
    vid_fullscreen->changed = vid_fullscreen_changed;
    vid_modelist->changed = vid_modelist_changed;

    mode_changed = 0;

    // Initialize the rest of graphics subsystems
    V_Init();
    SCR_Init();
    UI_Init();

    SCR_RegisterMedia();
    Con_RegisterMedia();

    cvar_modified &= ~(CVAR_FILES | CVAR_REFRESH);
}

/*
============
CL_ShutdownRefresh
============
*/
void CL_ShutdownRefresh(void)
{
    if (!cls.ref_initialized) {
        return;
    }

    // Shutdown the rest of graphics subsystems
    V_Shutdown();
    SCR_Shutdown();
    UI_Shutdown();

    vid_geometry->changed = NULL;
    vid_fullscreen->changed = NULL;
    vid_modelist->changed = NULL;

    R_Shutdown(qtrue);

    cls.ref_initialized = qfalse;

    // no longer active
    cls.active = ACT_MINIMIZED;

    Z_LeakTest(TAG_RENDERER);
}

