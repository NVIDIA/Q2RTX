/*
Copyright (C) 2003-2012 Andrey Nazarov

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

#include "video.h"

#if USE_X11
void VID_GLX_SurfaceChanged(void);
#endif

void VID_SDL_SurfaceChanged(void)
{
    int accel;

    SDL_GL_GetAttribute(SDL_GL_ACCELERATED_VISUAL, &accel);
    if (accel) {
        sdl.flags |= QVF_ACCELERATED;
    }

#if USE_X11
    VID_GLX_SurfaceChanged();
#endif
}

#ifdef __OpenBSD__
#define LIBGL   "libGL.so"
#else
#define LIBGL   "libGL.so.1"
#endif

qboolean VID_Init(void)
{
    cvar_t *gl_driver;
    cvar_t *gl_colorbits;
    cvar_t *gl_depthbits;
    cvar_t *gl_stencilbits;
    cvar_t *gl_multisamples;
    char *s;
    int colorbits;
    int depthbits;
    int stencilbits;
    int multisamples;

    if (!VID_SDL_Init()) {
        return qfalse;
    }

    gl_driver = Cvar_Get("gl_driver", LIBGL, CVAR_REFRESH);
    gl_colorbits = Cvar_Get("gl_colorbits", "0", CVAR_REFRESH);
    gl_depthbits = Cvar_Get("gl_depthbits", "0", CVAR_REFRESH);
    gl_stencilbits = Cvar_Get("gl_stencilbits", "8", CVAR_REFRESH);
    gl_multisamples = Cvar_Get("gl_multisamples", "0", CVAR_REFRESH);

    // don't allow absolute or relative paths
    FS_SanitizeFilenameVariable(gl_driver);

    while (1) {
        // ugly hack to work around brain-dead servers that actively
        // check and enforce `gl_driver' cvar to `opengl32', unaware
        // of other systems than Windows
        s = gl_driver->string;
        if (!Q_stricmp(s, "opengl32") || !Q_stricmp(s, "opengl32.dll")) {
            Com_Printf("...attempting to load %s instead of %s\n",
                       gl_driver->default_string, s);
            s = gl_driver->default_string;
        }

        if (SDL_GL_LoadLibrary(s) == 0) {
            break;
        }

        Com_EPrintf("Couldn't load OpenGL library: %s\n", SDL_GetError());
        if (!strcmp(s, gl_driver->default_string)) {
            goto fail;
        }

        // attempt to recover
        Com_Printf("...falling back to %s\n", gl_driver->default_string);
        Cvar_Reset(gl_driver);
    }

    colorbits = Cvar_ClampInteger(gl_colorbits, 0, 32);
    depthbits = Cvar_ClampInteger(gl_depthbits, 0, 32);
    stencilbits = Cvar_ClampInteger(gl_stencilbits, 0, 8);
    multisamples = Cvar_ClampInteger(gl_multisamples, 0, 32);

    if (colorbits == 0)
        colorbits = 24;

    if (depthbits == 0)
        depthbits = colorbits > 16 ? 24 : 16;

    if (depthbits < 24)
        stencilbits = 0;

    if (colorbits > 16) {
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    } else {
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 5);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
    }

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depthbits);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, stencilbits);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    if (multisamples > 1) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, multisamples);
    }

    if (!VID_SDL_SetMode(SDL_OPENGL | SDL_RESIZABLE, 0)) {
        Com_EPrintf("Couldn't set video mode: %s\n", SDL_GetError());
        goto fail;
    }

    return qtrue;

fail:
    VID_SDL_Shutdown();
    return qfalse;
}

#if !USE_X11

void VID_Shutdown(void)
{
    VID_SDL_Shutdown();
}

void VID_VideoWait(void)
{
}

qboolean VID_VideoSync(void)
{
    return qtrue;
}

void VID_BeginFrame(void)
{
}

void VID_EndFrame(void)
{
    SDL_GL_SwapBuffers();
}
#endif

void *VID_GetCoreAddr(const char *sym)
{
    return SDL_GL_GetProcAddress(sym);
}

void *VID_GetProcAddr(const char *sym)
{
    return SDL_GL_GetProcAddress(sym);
}

