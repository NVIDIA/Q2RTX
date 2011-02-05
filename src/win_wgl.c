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
#include "win_local.h"
#include "win_glimp.h"
#include "win_wgl.h"

void ( APIENTRY * qwglDrawBuffer )(GLenum mode);
const GLubyte * ( APIENTRY * qwglGetString )(GLenum name);

int   ( WINAPI * qwglChoosePixelFormat )(HDC, CONST PIXELFORMATDESCRIPTOR *);
int   ( WINAPI * qwglDescribePixelFormat )(HDC, int, UINT, LPPIXELFORMATDESCRIPTOR);
BOOL  ( WINAPI * qwglSetPixelFormat )(HDC, int, CONST PIXELFORMATDESCRIPTOR *);
BOOL  ( WINAPI * qwglSwapBuffers )(HDC);

HGLRC ( WINAPI * qwglCreateContext )(HDC);
BOOL  ( WINAPI * qwglDeleteContext )(HGLRC);
PROC  ( WINAPI * qwglGetProcAddress )(LPCSTR);
BOOL  ( WINAPI * qwglMakeCurrent )(HDC, HGLRC);

BOOL  ( WINAPI * qwglSwapIntervalEXT )(int interval);

void WGL_Shutdown( void ) {
    if( glw.hinstOpenGL ) {
        FreeLibrary( glw.hinstOpenGL );
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

    WGL_ShutdownExtensions( ~0 );
}

#define GPA( x ) ( void * )GetProcAddress( glw.hinstOpenGL, x );

qboolean WGL_Init( const char *dllname ) {
    if( ( glw.hinstOpenGL = LoadLibrary( dllname ) ) == NULL ) {
        return qfalse;
    }

    qwglDrawBuffer              = GPA( "glDrawBuffer" );
    qwglGetString               = GPA( "glGetString" );

    qwglChoosePixelFormat       = GPA( "wglChoosePixelFormat" );
    qwglDescribePixelFormat     = GPA( "wglDescribePixelFormat" );
    qwglSetPixelFormat          = GPA( "wglSetPixelFormat" );
    qwglSwapBuffers             = GPA( "wglSwapBuffers" );

    qwglCreateContext           = GPA( "wglCreateContext" );
    qwglDeleteContext           = GPA( "wglDeleteContext" );
    qwglGetProcAddress          = GPA( "wglGetProcAddress" );
    qwglMakeCurrent             = GPA( "wglMakeCurrent" );

    return qtrue;
}

#undef GPA

void WGL_ShutdownExtensions( unsigned mask ) {
    if( mask & QWGL_EXT_swap_control ) {
        qwglSwapIntervalEXT     = NULL;
    }
}

#define GPA( x )    ( void * )qwglGetProcAddress( x )

void WGL_InitExtensions( unsigned mask ) {
    if( mask & QWGL_EXT_swap_control ) {
        qwglSwapIntervalEXT     = GPA( "wglSwapIntervalEXT" );
    }
}

#undef GPA

unsigned WGL_ParseExtensionString( const char *s ) {
    // must match defines in win_wgl.h!
    static const char *const extnames[] = {
        "WGL_EXT_swap_control",
        NULL
    };

    return Com_ParseExtensionString( s, extnames );
}

