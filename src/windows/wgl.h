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

//
// win_wgl.h
//

#define QWGL_ARB_extensions_string  (1 << 0)
#define QWGL_ARB_multisample        (1 << 1)
#define QWGL_ARB_pixel_format       (1 << 2)
#define QWGL_EXT_swap_control       (1 << 3)
#define QWGL_EXT_swap_control_tear  (1 << 4)

bool        WGL_Init(const char *dllname);
void        WGL_Shutdown(void);
void        WGL_InitExtensions(unsigned mask);
void        WGL_ShutdownExtensions(unsigned mask);
unsigned    WGL_ParseExtensionString(const char *s);

extern void (APIENTRY * qwglDrawBuffer)(GLenum mode);
extern const GLubyte * (APIENTRY * qwglGetString)(GLenum name);

extern int (WINAPI * qwglChoosePixelFormat)(HDC, CONST PIXELFORMATDESCRIPTOR *);
extern int (WINAPI * qwglDescribePixelFormat)(HDC, int, UINT, LPPIXELFORMATDESCRIPTOR);
extern BOOL (WINAPI * qwglSetPixelFormat)(HDC, int, CONST PIXELFORMATDESCRIPTOR *);
extern BOOL (WINAPI * qwglSwapBuffers)(HDC);

extern HGLRC (WINAPI * qwglCreateContext)(HDC);
extern BOOL (WINAPI * qwglDeleteContext)(HGLRC);
extern PROC (WINAPI * qwglGetProcAddress)(LPCSTR);
extern BOOL (WINAPI * qwglMakeCurrent)(HDC, HGLRC);

extern const char * (WINAPI * qwglGetExtensionsStringARB)(HDC hdc);

extern BOOL (WINAPI * qwglChoosePixelFormatARB)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);

extern BOOL (WINAPI * qwglSwapIntervalEXT)(int interval);

