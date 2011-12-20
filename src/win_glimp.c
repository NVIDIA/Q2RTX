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

/*
GLW_IMP.C

This file contains ALL Win32 specific stuff having to do with the
OpenGL refresh.  When a port is being made the following functions
must be implemented by the port:

GLimp_EndFrame
GLimp_Init
GLimp_Shutdown
GLimp_SwitchFullscreen
*/

#include "win_local.h"
#include "win_glimp.h"
#include "win_wgl.h"

glwstate_t glw;

static cvar_t   *gl_driver;
static cvar_t   *gl_drawbuffer;
static cvar_t   *gl_swapinterval;
static cvar_t   *gl_allow_software;

/*
GLimp_Shutdown

This routine does all OS specific shutdown procedures for the OpenGL
subsystem.  Under OpenGL this means NULLing out the current DC and
HGLRC, deleting the rendering context, and releasing the DC acquired
for the window.  The state structure is also nulled out.
*/
void VID_Shutdown(void)
{
    if (qwglMakeCurrent) {
        qwglMakeCurrent(NULL, NULL);
    }

    if (glw.hGLRC && qwglDeleteContext) {
        qwglDeleteContext(glw.hGLRC);
        glw.hGLRC = NULL;
    }

    WGL_Shutdown();
    Win_Shutdown();

    if (gl_swapinterval) {
        gl_swapinterval->changed = NULL;
    }
    if (gl_drawbuffer) {
        gl_drawbuffer->changed = NULL;
    }

    memset(&glw, 0, sizeof(glw));
}

static qboolean InitGL(void)
{
    PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR),      // size of this pfd
        1,                              // version number
        PFD_DRAW_TO_WINDOW |            // support window
        PFD_SUPPORT_OPENGL |            // support OpenGL
        PFD_DOUBLEBUFFER,               // double buffered
        PFD_TYPE_RGBA,                  // RGBA type
        24,                             // 24-bit color depth
        0, 0, 0, 0, 0, 0,               // color bits ignored
        0,                              // no alpha buffer
        0,                              // shift bit ignored
        0,                              // no accumulation buffer
        0, 0, 0, 0,                     // accum bits ignored
        32,                             // 32-bit z-buffer
        0,                              // no stencil buffer
        0,                              // no auxiliary buffer
        PFD_MAIN_PLANE,                 // main layer
        0,                              // reserved
        0, 0, 0                         // layer masks ignored
    };
    int pixelformat;
    const char *what;

    // figure out if we're running on a minidriver or not
    if (!Q_stristr(gl_driver->string, "opengl32")) {
        Com_Printf("...running a minidriver: %s\n", gl_driver->string);
        glw.minidriver = qtrue;
    } else {
        glw.minidriver = qfalse;
    }

    // load OpenGL library
    if (!WGL_Init(gl_driver->string)) {
        what = "WGL_Init";
        goto fail1;
    }

    // set pixel format
    if (glw.minidriver) {
        // check if certain entry points are present if using a minidriver
        if (!qwglChoosePixelFormat || !qwglSetPixelFormat ||
            !qwglDescribePixelFormat || !qwglSwapBuffers) {
            Com_EPrintf("Required MCD entry points are missing\n");
            goto fail2;
        }

        if ((pixelformat = qwglChoosePixelFormat(win.dc, &pfd)) == 0) {
            what = "wglChoosePixelFormat";
            goto fail1;
        }

        if (qwglSetPixelFormat(win.dc, pixelformat, &pfd) == FALSE) {
            what = "wglSetPixelFormat";
            goto fail1;
        }

        qwglDescribePixelFormat(win.dc, pixelformat, sizeof(pfd), &pfd);
    } else {
        if ((pixelformat = ChoosePixelFormat(win.dc, &pfd)) == 0) {
            what = "ChoosePixelFormat";
            goto fail1;
        }

        if (SetPixelFormat(win.dc, pixelformat, &pfd) == FALSE) {
            what = "SetPixelFormat";
            goto fail1;
        }

        DescribePixelFormat(win.dc, pixelformat, sizeof(pfd), &pfd);
    }

    // check for software emulation
    if (pfd.dwFlags & PFD_GENERIC_FORMAT) {
        if (!gl_allow_software->integer) {
            Com_EPrintf("No hardware OpenGL acceleration detected\n");
            goto fail2;
        }
        Com_WPrintf("...using software emulation\n");
    } else if (pfd.dwFlags & PFD_GENERIC_ACCELERATED) {
        Com_DPrintf("...MCD acceleration found\n");
        win.flags |= QVF_ACCELERATED;
    } else {
        Com_DPrintf("...ICD acceleration found\n");
        win.flags |= QVF_ACCELERATED;
    }

    // startup the OpenGL subsystem by creating a context and making it current
    if ((glw.hGLRC = qwglCreateContext(win.dc)) == NULL) {
        what = "wglCreateContext";
        goto fail1;
    }

    if (!qwglMakeCurrent(win.dc, glw.hGLRC)) {
        what = "wglMakeCurrent";
        goto fail1;
    }

    // print out PFD specifics
    Com_DPrintf("GL_VENDOR: %s\n", qwglGetString(GL_VENDOR));
    Com_DPrintf("GL_RENDERER: %s\n", qwglGetString(GL_RENDERER));
    Com_DPrintf("GL_PFD: color(%d-bits: %d,%d,%d,%d) Z(%d-bit) stencil(%d-bit)\n",
                pfd.cColorBits, pfd.cRedBits, pfd.cGreenBits, pfd.cBlueBits,
                pfd.cAlphaBits, pfd.cDepthBits, pfd.cStencilBits);

    return qtrue;

fail1:
    Com_EPrintf("%s failed with error %#lx\n", what, GetLastError());
    if (glw.hGLRC && qwglDeleteContext) {
        qwglDeleteContext(glw.hGLRC);
        glw.hGLRC = NULL;
    }

fail2:
    WGL_Shutdown();
    return qfalse;
}

static void gl_swapinterval_changed(cvar_t *self)
{
    if (qwglSwapIntervalEXT) {
        qwglSwapIntervalEXT(self->integer);
    }
}

static void gl_drawbuffer_changed(cvar_t *self)
{
    if (!Q_stricmp(self->string, "GL_FRONT")) {
        glw.drawbuffer = GL_FRONT;
    } else if (!Q_stricmp(self->string, "GL_BACK")) {
        glw.drawbuffer = GL_BACK;
    } else {
        Cvar_Reset(self);
        glw.drawbuffer = GL_BACK;
    }

    qwglDrawBuffer(glw.drawbuffer);
}

/*
GLimp_Init

This routine is responsible for initializing the OS specific portions
of OpenGL.  Under Win32 this means dealing with the pixelformats and
doing the wgl interface stuff.
*/
qboolean VID_Init(void)
{
    const char *extensions;
    unsigned mask;

    gl_driver = Cvar_Get("gl_driver", DEFAULT_OPENGL_DRIVER, CVAR_ARCHIVE | CVAR_REFRESH);
    gl_drawbuffer = Cvar_Get("gl_drawbuffer", "GL_BACK", 0);
    gl_swapinterval = Cvar_Get("gl_swapinterval", "1", CVAR_ARCHIVE);
    gl_allow_software = Cvar_Get("gl_allow_software", "0", 0);

    while (1) {
        // create the window
        Win_Init();

        // initialize OpenGL context
        if (InitGL()) {
            break;
        }

        // it failed, clean up
        Win_Shutdown();

        // see if this was a minidriver
        if (!glw.minidriver) {
            return qfalse;
        }

        // attempt to recover
        Com_Printf("...attempting to load opengl32\n");
        Cvar_Set("gl_driver", "opengl32");
    }

    // initialize WGL extensions
    extensions = (const char *)qwglGetString(GL_EXTENSIONS);
    mask = WGL_ParseExtensionString(extensions);

    if (mask & QWGL_EXT_swap_control) {
        Com_Printf("...enabling WGL_EXT_swap_control\n");
        WGL_InitExtensions(QWGL_EXT_swap_control);
        gl_swapinterval->changed = gl_swapinterval_changed;
        gl_swapinterval_changed(gl_swapinterval);
    } else {
        Com_Printf("WGL_EXT_swap_control not found\n");
    }

    gl_drawbuffer->changed = gl_drawbuffer_changed;
    gl_drawbuffer_changed(gl_drawbuffer);

    VID_SetMode();

    return qtrue;
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

/*
GLimp_EndFrame

Responsible for doing a swapbuffers and possibly for other stuff
as yet to be determined.  Probably better not to make this a GLimp
function and instead do a call to GLimp_SwapBuffers.
*/
void VID_EndFrame(void)
{
    BOOL ret;

    // don't flip if drawing to front buffer
    if (glw.drawbuffer == GL_FRONT) {
        return;
    }

    if (glw.minidriver) {
        ret = qwglSwapBuffers(win.dc);
    } else {
        ret = SwapBuffers(win.dc);
    }

    if (!ret) {
        DWORD error = GetLastError();

        // this happens sometimes when the window is iconified
        if (!IsIconic(win.wnd)) {
            Com_Error(ERR_FATAL, "%s failed with error %#lx",
                      glw.minidriver ? "wglSwapBuffers" : "SwapBuffers", error);
        }
    }
}

void *VID_GetProcAddr(const char *symbol)
{
    return (void *)GetProcAddress(glw.hinstOpenGL, symbol);
}

