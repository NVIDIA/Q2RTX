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

//
// glx.c -- support for GLX extensions
//

#include "video.h"
#include <SDL_syswm.h>
#include <GL/glx.h>
#include <GL/glxext.h>

#define QGLX_EXT_swap_control       (1<<0)
#define QGLX_EXT_swap_control_tear  (1<<1)
#define QGLX_SGI_video_sync         (1<<2)

// for debugging
#define SHOW_SYNC() \
    Com_DDDDPrintf("%s: %u\n", __func__, glx_sync_count)

static const char *(*qglXQueryExtensionsString)(Display *, int);
static GLXDrawable (*qglXGetCurrentDrawable)(void); 
static int (*qglXSwapIntervalEXT)(Display *, GLXDrawable, int);
static int (*qglXGetVideoSyncSGI)(unsigned int *);
static int (*qglXWaitVideoSyncSGI)(int, int, unsigned int *);

static Display  *glx_dpy;
static unsigned glx_extensions;
static unsigned glx_sync_count;

static cvar_t *gl_swapinterval;
static cvar_t *gl_video_sync;

static unsigned GLX_ParseExtensionString(const char *s)
{
    static const char *const extnames[] = {
        "GLX_EXT_swap_control",
        "GLX_EXT_swap_control_tear",
        "GLX_SGI_video_sync",
        NULL
    };

    return Com_ParseExtensionString(s, extnames);
}

static void gl_swapinterval_changed(cvar_t *self)
{
    int drawable;

    if (!qglXSwapIntervalEXT)
        return;
    if (!qglXGetCurrentDrawable)
        return;

    drawable = qglXGetCurrentDrawable();
    if (!drawable)
        return;

    if (self->integer < 0 && !(glx_extensions & QGLX_EXT_swap_control_tear)) {
        Com_Printf("Negative swap interval is not supported on this system.\n");
        Cvar_Reset(self);
    }

    qglXSwapIntervalEXT(glx_dpy, drawable, self->integer);
}

void VID_GLX_SurfaceChanged(void)
{
    SDL_SysWMinfo info;
    const char *extensions;

    SDL_VERSION(&info.version);
    if (!SDL_GetWMInfo(&info))
        return;

    if (info.subsystem != SDL_SYSWM_X11)
        return;

    glx_dpy = info.info.x11.display;
    if (!glx_dpy)
        return;

    gl_swapinterval = Cvar_Get("gl_swapinterval", "1", CVAR_ARCHIVE);
    gl_video_sync = Cvar_Get("gl_video_sync", "1", CVAR_REFRESH);

    qglXQueryExtensionsString = SDL_GL_GetProcAddress("glXQueryExtensionsString");
    qglXGetCurrentDrawable = SDL_GL_GetProcAddress("glXGetCurrentDrawable");

    if (qglXQueryExtensionsString)
        extensions = qglXQueryExtensionsString(glx_dpy, DefaultScreen(glx_dpy));
    else
        extensions = NULL;

    glx_extensions = GLX_ParseExtensionString(extensions);

    if (glx_extensions & QGLX_EXT_swap_control) {
        if (glx_extensions & QGLX_EXT_swap_control_tear)
            Com_Printf("...enabling GLX_EXT_swap_control(_tear)\n");
        else
            Com_Printf("...enabling GLX_EXT_swap_control\n");
        qglXSwapIntervalEXT = SDL_GL_GetProcAddress("glXSwapIntervalEXT");
        gl_swapinterval->changed = gl_swapinterval_changed;
        gl_swapinterval_changed(gl_swapinterval);
    } else {
        Com_Printf("GLX_EXT_swap_control not found\n");
        Cvar_Set("gl_swapinterval", "0");
    }

    if (glx_extensions & QGLX_SGI_video_sync) {
        if (gl_video_sync->integer) {
            Com_Printf("...enabling GLX_SGI_video_sync\n");
            qglXGetVideoSyncSGI = SDL_GL_GetProcAddress("glXGetVideoSyncSGI");
            qglXWaitVideoSyncSGI = SDL_GL_GetProcAddress("glXWaitVideoSyncSGI");
            if (qglXGetVideoSyncSGI) {
                qglXGetVideoSyncSGI(&glx_sync_count);
                SHOW_SYNC();
                sdl.flags |= QVF_VIDEOSYNC;
            }
        } else {
            Com_Printf("...ignoring GLX_SGI_video_sync\n");
        }
    } else if (gl_video_sync->integer) {
        Com_Printf("GLX_SGI_video_sync not found\n");
        Cvar_Set("gl_video_sync", "0");
    }
}

void VID_Shutdown(void)
{
    glx_dpy         = NULL;
    glx_extensions  = 0;
    glx_sync_count  = 0;

    if (gl_swapinterval)
        gl_swapinterval->changed = NULL;

    qglXQueryExtensionsString   = NULL;
    qglXGetCurrentDrawable      = NULL;
    qglXSwapIntervalEXT         = NULL;
    qglXGetVideoSyncSGI         = NULL;
    qglXWaitVideoSyncSGI        = NULL;

    VID_SDL_Shutdown();
}

void VID_VideoWait(void)
{
    if (!qglXGetVideoSyncSGI)
        return;

    if (!qglXWaitVideoSyncSGI)
        return;

    // work around glXWaitVideoSyncSGI blocking indefinitely if vsync is enabled
    if (gl_swapinterval->integer)
        qglXGetVideoSyncSGI(&glx_sync_count);
    else
        qglXWaitVideoSyncSGI(1, 0, &glx_sync_count);

    SHOW_SYNC();
}

qboolean VID_VideoSync(void)
{
    unsigned count;

    if (!qglXGetVideoSyncSGI)
        return qtrue;

    if (qglXGetVideoSyncSGI(&count))
        return qtrue;

    if (count != glx_sync_count) {
        glx_sync_count = count;
        SHOW_SYNC();
        return qtrue;
    }

    return qfalse;
}

void VID_BeginFrame(void)
{
}

void VID_EndFrame(void)
{
    SDL_GL_SwapBuffers();

    if (qglXGetVideoSyncSGI) {
        qglXGetVideoSyncSGI(&glx_sync_count);
        SHOW_SYNC();
    }
}
