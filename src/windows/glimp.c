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
#if USE_REF == REF_GLPT
#include "glad/glad.h"
#endif
#include "client.h"
#include "glimp.h"
#include "wgl.h"

glwstate_t glw;

static cvar_t   *gl_driver;
static cvar_t   *gl_drawbuffer;
static cvar_t   *gl_swapinterval;
static cvar_t   *gl_allow_software;
static cvar_t   *gl_colorbits;
static cvar_t   *gl_depthbits;
static cvar_t   *gl_stencilbits;
static cvar_t   *gl_multisamples;

/*
VID_Shutdown

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

static void ReportLastError(const char *what)
{
    Com_EPrintf("%s failed with error %lu\n", what, GetLastError());
}

static void ReportPixelFormat(int pixelformat, PIXELFORMATDESCRIPTOR *pfd)
{
    Com_DPrintf("GL_FPD(%d): flags(%#lx) color(%d) Z(%d) stencil(%d)\n",
                pixelformat, pfd->dwFlags, pfd->cColorBits, pfd->cDepthBits,
                pfd->cStencilBits);
}

#define FAIL_OK     0
#define FAIL_SOFT   -1
#define FAIL_HARD   -2

static int SetupGL(int colorbits, int depthbits, int stencilbits, int multisamples)
{
    PIXELFORMATDESCRIPTOR pfd;
    int pixelformat;

    // create the main window
    Win_Init();

    if (colorbits == 0)
        colorbits = 24;

    if (depthbits == 0)
        depthbits = colorbits > 16 ? 24 : 16;

    if (depthbits < 24)
        stencilbits = 0;

    // choose pixel format
    if (qwglChoosePixelFormatARB && multisamples > 1) {
        int iAttributes[20];
        UINT numFormats;

        iAttributes[0] = WGL_DRAW_TO_WINDOW_ARB;
        iAttributes[1] = TRUE;
        iAttributes[2] = WGL_SUPPORT_OPENGL_ARB;
        iAttributes[3] = TRUE;
        iAttributes[4] = WGL_DOUBLE_BUFFER_ARB;
        iAttributes[5] = TRUE;
        iAttributes[6] = WGL_PIXEL_TYPE_ARB;
        iAttributes[7] = WGL_TYPE_RGBA_ARB;
        iAttributes[8] = WGL_COLOR_BITS_ARB;
        iAttributes[9] = colorbits;
        iAttributes[10] = WGL_DEPTH_BITS_ARB;
        iAttributes[11] = depthbits;
        iAttributes[12] = WGL_STENCIL_BITS_ARB;
        iAttributes[13] = stencilbits;
        iAttributes[14] = WGL_SAMPLE_BUFFERS_ARB;
        iAttributes[15] = 1;
        iAttributes[16] = WGL_SAMPLES_ARB;
        iAttributes[17] = multisamples;
        iAttributes[18] = 0;
        iAttributes[19] = 0;

        if (qwglChoosePixelFormatARB(win.dc, iAttributes, NULL, 1, &pixelformat, &numFormats) == FALSE) {
            ReportLastError("wglChoosePixelFormatARB");
            goto soft;
        }
        if (numFormats == 0) {
            Com_EPrintf("No suitable OpenGL pixelformat found for %d multisamples\n", multisamples);
            goto soft;
        }
    } else {
        memset(&pfd, 0, sizeof(pfd));
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = colorbits;
        pfd.cDepthBits = depthbits;
        pfd.cStencilBits = stencilbits;
        pfd.iLayerType = PFD_MAIN_PLANE;

        if (glw.minidriver) {
            if ((pixelformat = qwglChoosePixelFormat(win.dc, &pfd)) == 0) {
                ReportLastError("wglChoosePixelFormat");
                goto soft;
            }
        } else {
            if ((pixelformat = ChoosePixelFormat(win.dc, &pfd)) == 0) {
                ReportLastError("ChoosePixelFormat");
                goto soft;
            }
        }
    }

    // set pixel format
    if (glw.minidriver) {
        qwglDescribePixelFormat(win.dc, pixelformat, sizeof(pfd), &pfd);
        ReportPixelFormat(pixelformat, &pfd);

        if (qwglSetPixelFormat(win.dc, pixelformat, &pfd) == FALSE) {
            ReportLastError("wglSetPixelFormat");
            goto soft;
        }
    } else {
        DescribePixelFormat(win.dc, pixelformat, sizeof(pfd), &pfd);
        ReportPixelFormat(pixelformat, &pfd);

        if (SetPixelFormat(win.dc, pixelformat, &pfd) == FALSE) {
            ReportLastError("SetPixelFormat");
            goto soft;
        }
    }

    // check for software emulation
    if (pfd.dwFlags & PFD_GENERIC_FORMAT) {
        if (!gl_allow_software->integer) {
            Com_EPrintf("No hardware OpenGL acceleration detected\n");
            goto soft;
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
        ReportLastError("wglCreateContext");
        goto hard;
    }

    if (qwglMakeCurrent(win.dc, glw.hGLRC) == FALSE) {
        ReportLastError("wglMakeCurrent");
        qwglDeleteContext(glw.hGLRC);
        glw.hGLRC = NULL;
        goto hard;
    }

    return FAIL_OK;

soft:
    // it failed, clean up
    Win_Shutdown();
    return FAIL_SOFT;

hard:
    Win_Shutdown();
    return FAIL_HARD;
}

#define FAKE_WINDOW_CLASS   "Q2PRO FAKE WINDOW CLASS"
#define FAKE_WINDOW_NAME    "Q2PRO FAKE WINDOW NAME"

static unsigned GetFakeWindowExtensions(void)
{
    WNDCLASSEX wc;
    PIXELFORMATDESCRIPTOR pfd;
    int pixelformat;
    HWND wnd;
    HDC dc;
    HGLRC rc;
    unsigned extensions = 0;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = hGlobalInstance;
    wc.lpszClassName = FAKE_WINDOW_CLASS;

    if (!RegisterClassEx(&wc))
        goto fail0;

    wnd = CreateWindow(
        FAKE_WINDOW_CLASS,
        FAKE_WINDOW_NAME,
        0,
        0, 0, 0, 0,
        NULL,
        NULL,
        hGlobalInstance,
        NULL);
    if (!wnd)
        goto fail1;

    if ((dc = GetDC(wnd)) == NULL)
        goto fail2;

    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    if (glw.minidriver) {
        if ((pixelformat = qwglChoosePixelFormat(dc, &pfd)) == 0)
            goto fail3;

        if (qwglSetPixelFormat(dc, pixelformat, &pfd) == FALSE)
            goto fail3;
    } else {
        if ((pixelformat = ChoosePixelFormat(dc, &pfd)) == 0)
            goto fail3;

        if (SetPixelFormat(dc, pixelformat, &pfd) == FALSE)
            goto fail3;
    }

    if ((rc = qwglCreateContext(dc)) == NULL)
        goto fail3;

    if (qwglMakeCurrent(dc, rc) == FALSE)
        goto fail4;

    WGL_InitExtensions(QWGL_ARB_extensions_string);

    if (!qwglGetExtensionsStringARB)
        goto fail5;

    extensions = WGL_ParseExtensionString(qwglGetExtensionsStringARB(dc));

    if (extensions & QWGL_ARB_pixel_format) {
        Com_Printf("...enabling WGL_ARB_pixel_format\n");
        WGL_InitExtensions(QWGL_ARB_pixel_format);
    }

fail5:
    qwglMakeCurrent(NULL, NULL);
fail4:
    qwglDeleteContext(rc);
fail3:
    ReleaseDC(wnd, dc);
fail2:
    DestroyWindow(wnd);
fail1:
    UnregisterClass(FAKE_WINDOW_CLASS, hGlobalInstance);
fail0:
    return extensions;
}

static int LoadGL(const char *driver)
{
    int colorbits = Cvar_ClampInteger(gl_colorbits, 0, 32);
    int depthbits = Cvar_ClampInteger(gl_depthbits, 0, 32);
    int stencilbits = Cvar_ClampInteger(gl_stencilbits, 0, 8);
    int multisamples = Cvar_ClampInteger(gl_multisamples, 0, 32);
    int ret;

    // figure out if we're running on a minidriver or not
    if (!Q_stricmp(driver, "opengl32") ||
        !Q_stricmp(driver, "opengl32.dll")) {
        glw.minidriver = qfalse;
    } else {
        Com_Printf("...running a minidriver: %s\n", driver);
        glw.minidriver = qtrue;
    }

    // load the OpenGL library and bind to it
    if (!WGL_Init(driver)) {
        ReportLastError("WGL_Init");
        return FAIL_SOFT;
    }

    // check if basic WGL entry points are present
    if (!qwglCreateContext || !qwglMakeCurrent || !qwglDeleteContext) {
        Com_EPrintf("Required WGL entry points are missing\n");
        goto fail;
    }

    if (glw.minidriver) {
        // check if MCD entry points are present if using a minidriver
        if (!qwglChoosePixelFormat || !qwglSetPixelFormat ||
            !qwglDescribePixelFormat || !qwglSwapBuffers) {
            Com_EPrintf("Required MCD entry points are missing\n");
            goto fail;
        }
    }

    // check for WGL_ARB_multisample by creating a fake window
    if (multisamples > 1) {
        unsigned extensions = GetFakeWindowExtensions();

        if (extensions & QWGL_ARB_multisample) {
            if (qwglChoosePixelFormatARB) {
                Com_Printf("...enabling WGL_ARB_multisample\n");
            } else {
                Com_Printf("...ignoring WGL_ARB_multisample, WGL_ARB_pixel_format not found\n");
                Cvar_Set("gl_multisamples", "0");
                multisamples = 0;
            }
        } else {
            Com_Printf("WGL_ARB_multisample not found\n");
            Cvar_Set("gl_multisamples", "0");
            multisamples = 0;
        }
    }

    // create window, choose PFD, setup OpenGL context
    ret = SetupGL(colorbits, depthbits, stencilbits, multisamples);

    // attempt to recover
    if (ret == FAIL_SOFT && (colorbits || depthbits || stencilbits || multisamples > 1)) {
        Cvar_Set("gl_multisamples", "0");
        ret = SetupGL(0, 0, 0, 0);
    }

    if (ret)
        goto fail;

    return FAIL_OK;

fail:
    // it failed, clean up
    WGL_Shutdown();
    return FAIL_SOFT;
}

static void gl_swapinterval_changed(cvar_t *self)
{
    if (self->integer < 0 && !(glw.extensions & QWGL_EXT_swap_control_tear)) {
        Com_Printf("Negative swap interval is not supported on this system.\n");
        Cvar_Reset(self);
    }

    if (qwglSwapIntervalEXT && !qwglSwapIntervalEXT(self->integer))
        ReportLastError("wglSwapIntervalEXT");
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
VID_Init

This routine is responsible for initializing the OS specific portions
of OpenGL.  Under Win32 this means dealing with the pixelformats and
doing the wgl interface stuff.
*/
qboolean VID_Init(void)
{
    const char *extensions;
    int ret;

    gl_driver = Cvar_Get("gl_driver", "opengl32", CVAR_ARCHIVE | CVAR_REFRESH);
    gl_drawbuffer = Cvar_Get("gl_drawbuffer", "GL_BACK", 0);
    gl_swapinterval = Cvar_Get("gl_swapinterval", "1", CVAR_ARCHIVE);
    gl_allow_software = Cvar_Get("gl_allow_software", "0", 0);
    gl_colorbits = Cvar_Get("gl_colorbits", "0", CVAR_REFRESH);
    gl_depthbits = Cvar_Get("gl_depthbits", "0", CVAR_REFRESH);
    gl_stencilbits = Cvar_Get("gl_stencilbits", "8", CVAR_REFRESH);
    gl_multisamples = Cvar_Get("gl_multisamples", "0", CVAR_REFRESH);

    // don't allow absolute or relative paths
    FS_SanitizeFilenameVariable(gl_driver);

    // load and initialize the OpenGL driver
    ret = LoadGL(gl_driver->string);

    // attempt to recover if this was a minidriver
    if (ret == FAIL_SOFT && glw.minidriver) {
        Com_Printf("...falling back to opengl32\n");
        Cvar_Reset(gl_driver);
        ret = LoadGL(gl_driver->string);
    }

    // it failed, abort
    if (ret)
        return qfalse;

#if USE_REF == REF_GLPT
	gladLoadGL();
#endif

    // initialize WGL extensions
    WGL_InitExtensions(QWGL_ARB_extensions_string);

    if (qwglGetExtensionsStringARB)
        extensions = qwglGetExtensionsStringARB(win.dc);
    else
        extensions = NULL;

    // fall back to GL_EXTENSIONS for legacy drivers
    if (!extensions || !*extensions)
        extensions = (const char *)qwglGetString(GL_EXTENSIONS);

    glw.extensions = WGL_ParseExtensionString(extensions);

    if (glw.extensions & QWGL_EXT_swap_control) {
        if (glw.extensions & QWGL_EXT_swap_control_tear)
            Com_Printf("...enabling WGL_EXT_swap_control(_tear)\n");
        else
            Com_Printf("...enabling WGL_EXT_swap_control\n");
        WGL_InitExtensions(QWGL_EXT_swap_control);
        gl_swapinterval->changed = gl_swapinterval_changed;
        gl_swapinterval_changed(gl_swapinterval);
    } else {
        Com_Printf("WGL_EXT_swap_control not found\n");
        Cvar_Set("gl_swapinterval", "0");
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
VID_EndFrame

Responsible for doing a swapbuffers and possibly for other stuff
as yet to be determined.  Probably better not to make this a VID_
function and instead do a call to VID_SwapBuffers.
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
            Com_Error(ERR_FATAL, "%s failed with error %lu",
                      glw.minidriver ? "wglSwapBuffers" : "SwapBuffers", error);
        }
    }
}

void *VID_GetCoreAddr(const char *sym)
{
    void    *entry;

    if (glw.hinstOpenGL)
        entry = (void *)GetProcAddress(glw.hinstOpenGL, sym);
    else
        entry = NULL;

    if (!entry)
        Com_EPrintf("Couldn't get OpenGL entry point: %s\n", sym);

    return entry;
}

void *VID_GetProcAddr(const char *sym)
{
    void    *entry;

    if (qwglGetProcAddress)
        entry = (void *)qwglGetProcAddress(sym);
    else
        entry = NULL;

    if (!entry)
        Com_EPrintf("Couldn't get OpenGL entry point: %s\n", sym);

    return entry;
}

