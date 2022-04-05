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
#include "client.h"
#include "glimp.h"
#include "wgl.h"

void (APIENTRY * qwglDrawBuffer)(GLenum mode);
const GLubyte * (APIENTRY * qwglGetString)(GLenum name);

int (WINAPI * qwglChoosePixelFormat)(HDC, CONST PIXELFORMATDESCRIPTOR *);
int (WINAPI * qwglDescribePixelFormat)(HDC, int, UINT, LPPIXELFORMATDESCRIPTOR);
BOOL (WINAPI * qwglSetPixelFormat)(HDC, int, CONST PIXELFORMATDESCRIPTOR *);
BOOL (WINAPI * qwglSwapBuffers)(HDC);

HGLRC (WINAPI * qwglCreateContext)(HDC);
BOOL (WINAPI * qwglDeleteContext)(HGLRC);
PROC (WINAPI * qwglGetProcAddress)(LPCSTR);
BOOL (WINAPI * qwglMakeCurrent)(HDC, HGLRC);

const char * (WINAPI * qwglGetExtensionsStringARB)(HDC hdc);

BOOL (WINAPI * qwglChoosePixelFormatARB)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);

BOOL (WINAPI * qwglSwapIntervalEXT)(int interval);

void WGL_Shutdown(void)
{
    if (glw.hinstOpenGL) {
        FreeLibrary(glw.hinstOpenGL);
        glw.hinstOpenGL = NULL;
    }

    qwglDrawBuffer              = NULL;
    qwglGetString               = NULL;

    qwglChoosePixelFormat       = NULL;
    qwglDescribePixelFormat     = NULL;
    qwglSetPixelFormat          = NULL;
    qwglSwapBuffers             = NULL;

    qwglCreateContext           = NULL;
    qwglDeleteContext           = NULL;
    qwglGetProcAddress          = NULL;
    qwglMakeCurrent             = NULL;

    WGL_ShutdownExtensions(~0);
}

#define GPA(x)  (void *)GetProcAddress(glw.hinstOpenGL, x);

bool WGL_Init(const char *dllname)
{
    if ((glw.hinstOpenGL = LoadLibrary(dllname)) == NULL) {
        return false;
    }

    qwglDrawBuffer              = GPA("glDrawBuffer");
    qwglGetString               = GPA("glGetString");

    qwglChoosePixelFormat       = GPA("wglChoosePixelFormat");
    qwglDescribePixelFormat     = GPA("wglDescribePixelFormat");
    qwglSetPixelFormat          = GPA("wglSetPixelFormat");
    qwglSwapBuffers             = GPA("wglSwapBuffers");

    qwglCreateContext           = GPA("wglCreateContext");
    qwglDeleteContext           = GPA("wglDeleteContext");
    qwglGetProcAddress          = GPA("wglGetProcAddress");
    qwglMakeCurrent             = GPA("wglMakeCurrent");

    return true;
}

#undef GPA

void WGL_ShutdownExtensions(unsigned mask)
{
    if (mask & QWGL_ARB_extensions_string) {
        qwglGetExtensionsStringARB  = NULL;
    }
    if (mask & QWGL_ARB_pixel_format) {
        qwglChoosePixelFormatARB    = NULL;
    }
    if (mask & QWGL_EXT_swap_control) {
        qwglSwapIntervalEXT         = NULL;
    }
}

#define GPA(x)  (void *)qwglGetProcAddress(x)

void WGL_InitExtensions(unsigned mask)
{
    if (mask & QWGL_ARB_extensions_string) {
        qwglGetExtensionsStringARB  = GPA("wglGetExtensionsStringARB");
    }
    if (mask & QWGL_ARB_pixel_format) {
        qwglChoosePixelFormatARB    = GPA("wglChoosePixelFormatARB");
    }
    if (mask & QWGL_EXT_swap_control) {
        qwglSwapIntervalEXT         = GPA("wglSwapIntervalEXT");
    }
}

#undef GPA

unsigned WGL_ParseExtensionString(const char *s)
{
    // must match defines in win_wgl.h!
    static const char *const extnames[] = {
        "WGL_ARB_extensions_string",
        "WGL_ARB_multisample",
        "WGL_ARB_pixel_format",
        "WGL_EXT_swap_control",
        "WGL_EXT_swap_control_tear",
        NULL
    };

    return Com_ParseExtensionString(s, extnames);
}

