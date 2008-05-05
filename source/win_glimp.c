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

static cvar_t	*gl_driver;
static cvar_t	*gl_drawbuffer;
static cvar_t   *gl_swapinterval;

/*
GLimp_Shutdown

This routine does all OS specific shutdown procedures for the OpenGL
subsystem.  Under OpenGL this means NULLing out the current DC and
HGLRC, deleting the rendering context, and releasing the DC acquired
for the window.  The state structure is also nulled out.
*/
static void GLimp_Shutdown( void ) {
	if( qwglMakeCurrent ) {
		qwglMakeCurrent( NULL, NULL );
	}

	if( glw.hGLRC && qwglDeleteContext ) {
		qwglDeleteContext( glw.hGLRC );
		glw.hGLRC = NULL;
	}

	WGL_Shutdown();
    Win_Shutdown();

    if( gl_swapinterval ) {
        gl_swapinterval->changed = NULL;
    }

    memset( &glw, 0, sizeof( glw ) );
}

static qboolean GLimp_InitGL( void ) {
    PIXELFORMATDESCRIPTOR pfd = {
		sizeof( PIXELFORMATDESCRIPTOR ),	// size of this pfd
		1,								// version number
		PFD_DRAW_TO_WINDOW |			// support window
		PFD_SUPPORT_OPENGL |			// support OpenGL
		PFD_DOUBLEBUFFER,				// double buffered
		PFD_TYPE_RGBA,					// RGBA type
		24,								// 24-bit color depth
		0, 0, 0, 0, 0, 0,				// color bits ignored
		0,								// no alpha buffer
		0,								// shift bit ignored
		0,								// no accumulation buffer
		0, 0, 0, 0, 					// accum bits ignored
		32,								// 32-bit z-buffer	
		0,								// no stencil buffer
		0,								// no auxiliary buffer
		PFD_MAIN_PLANE,					// main layer
		0,								// reserved
		0, 0, 0							// layer masks ignored
    };
    int pixelformat;
    const char *renderer;

	// figure out if we're running on a minidriver or not
	if( !strstr( gl_driver->string, "opengl32" ) ) {
		Com_Printf( "...running a minidriver: %s\n", gl_driver->string );
		glw.minidriver = qtrue;
	}

	// load OpenGL library
	Com_DPrintf( "...initializing WGL: " );
	if( !WGL_Init( gl_driver->string ) ) {
		goto fail1;
	}
	Com_DPrintf( "ok\n" );

	Com_DPrintf( "...setting pixel format: " );
	if( glw.minidriver ) {
		if ( ( pixelformat = qwglChoosePixelFormat( win.dc, &pfd ) ) == 0 ) {
		    goto fail1;
		}
		if( qwglSetPixelFormat( win.dc, pixelformat, &pfd ) == FALSE ) {
		    goto fail1;
		}
		qwglDescribePixelFormat( win.dc, pixelformat, sizeof( pfd ), &pfd );
	} else {
		if( ( pixelformat = ChoosePixelFormat( win.dc, &pfd ) ) == 0 ) {
		    goto fail1;
		}
		if( SetPixelFormat( win.dc, pixelformat, &pfd ) == FALSE ) {
		    goto fail1;
		}
		DescribePixelFormat( win.dc, pixelformat, sizeof( pfd ), &pfd );
	}
	Com_DPrintf( "ok\n" );

	// startup the OpenGL subsystem by creating a context and making it current
	Com_DPrintf( "...creating OpenGL context: " );
	if( ( glw.hGLRC = qwglCreateContext( win.dc ) ) == NULL ) {
		goto fail1;
	}
	Com_DPrintf( "ok\n" );

	Com_DPrintf( "...making context current: " );
    if( !qwglMakeCurrent( win.dc, glw.hGLRC ) ) {
		goto fail1;
	}
	Com_DPrintf( "ok\n" );

    renderer = qglGetString( GL_RENDERER );

    if( pfd.dwFlags & PFD_GENERIC_ACCELERATED ) {
        win.flags |= QVF_ACCELERATED;
    } else if( !renderer || !renderer[0] || !Q_stricmp( renderer, "gdi generic" ) ) {
		Com_Printf( "No hardware OpenGL acceleration detected.\n" );
		goto fail2;
    }

	// print out PFD specifics
	Com_Printf( "GL_VENDOR: %s\n", qglGetString( GL_VENDOR ) );
	Com_Printf( "GL_RENDERER: %s\n", renderer );
	Com_Printf( "GL_PFD: color(%d-bits: %d,%d,%d,%d) Z(%d-bit) stencil(%d-bit)\n",
		pfd.cColorBits, pfd.cRedBits, pfd.cGreenBits, pfd.cBlueBits,
		pfd.cAlphaBits, pfd.cDepthBits, pfd.cStencilBits );

	return qtrue;

fail1:
	Com_DPrintf( "failed with error %#lx\n", GetLastError() );
fail2:
	if( glw.hGLRC && qwglDeleteContext ) {
		qwglDeleteContext( glw.hGLRC );
		glw.hGLRC = NULL;
	}

	WGL_Shutdown();

	return qfalse;
}

static void gl_swapinterval_changed( cvar_t *self ) {
	if( qwglSwapIntervalEXT ) {
		qwglSwapIntervalEXT( self->integer );
	}
}

/*
GLimp_Init

This routine is responsible for initializing the OS specific portions
of OpenGL.  Under Win32 this means dealing with the pixelformats and
doing the wgl interface stuff.
*/
static qboolean GLimp_Init( void ) {
    const char *extensions;

	gl_driver = Cvar_Get( "gl_driver", "opengl32", CVAR_ARCHIVE|CVAR_LATCHED );
	gl_drawbuffer = Cvar_Get( "gl_drawbuffer", "GL_BACK", 0 );
	gl_swapinterval = Cvar_Get( "gl_swapinterval", "1", CVAR_ARCHIVE );

    // create the window
	Win_Init();

    // initialize OpenGL context
    if( !GLimp_InitGL() ) {
        if( !glw.minidriver ) {
            goto fail;
        }
        Com_Printf( "...attempting to load opengl32\n" );
        Cvar_Set( "gl_driver","opengl32" );
        if( !GLimp_InitGL() ) {
            goto fail;
        }
    }

    // initialize WGL extensions
	extensions = qglGetString( GL_EXTENSIONS );
	if( extensions && strstr( extensions, "WGL_EXT_swap_control" ) ) {
		Com_Printf( "...enabling WGL_EXT_swap_control\n" );
		qwglSwapIntervalEXT = ( PFNWGLSWAPINTERWALEXTPROC )qwglGetProcAddress( "wglSwapIntervalEXT" );        
        gl_swapinterval->changed = gl_swapinterval_changed;
        gl_swapinterval_changed( gl_swapinterval );
	} else {
		Com_Printf( "WGL_EXT_swap_control not found\n" );
    }

    Win_SetMode();
    Win_ModeChanged();

	return qtrue;

fail:
    Win_Shutdown();
    return qfalse;
}


static void GLimp_BeginFrame( void ) {
}

/*
GLimp_EndFrame

Responsible for doing a swapbuffers and possibly for other stuff
as yet to be determined.  Probably better not to make this a GLimp
function and instead do a call to GLimp_SwapBuffers.
*/
static void GLimp_EndFrame( void ) {
    if( !qwglSwapBuffers( win.dc ) ) {
        int error = GetLastError();

        if( !IsIconic( win.wnd ) ) {
            Com_Error( ERR_FATAL, "GLimp_EndFrame: wglSwapBuffers failed with error %#x", error );
        }
    }
}

/*
@@@@@@@@@@@@
VID_FillGLAPI
@@@@@@@@@@@@
*/
void VID_FillGLAPI( videoAPI_t *api ) {
	api->Init = GLimp_Init;
	api->Shutdown = GLimp_Shutdown;
	api->UpdateGamma = Win_UpdateGamma;
	api->GetProcAddr = WGL_GetProcAddress;
	api->BeginFrame = GLimp_BeginFrame;
	api->EndFrame = GLimp_EndFrame;
}

