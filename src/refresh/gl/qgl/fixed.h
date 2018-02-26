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

#ifndef QGL_H
#define QGL_H

#if USE_SDL
#include <SDL_opengl.h>
#else	// USE_SDL
#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#endif	// _MSC_VER
#include <GL/gl.h>
#include <GL/glext.h>
#endif	// !USE_SDL

// ==========================================================

// subset of OpenGL 1.1 core functions
#define qglAlphaFunc glAlphaFunc
#define qglBindTexture glBindTexture
#define qglBlendFunc glBlendFunc
#define qglClear glClear
#define qglClearColor glClearColor
#define qglClearDepth glClearDepth
#define qglClearStencil glClearStencil
#define qglColor4f glColor4f
#define qglColor4fv glColor4fv
#define qglColorMask glColorMask
#define qglColorPointer glColorPointer
#define qglCopyTexImage2D glCopyTexImage2D
#define qglCopyTexSubImage2D glCopyTexSubImage2D
#define qglCullFace glCullFace
#define qglDeleteTextures glDeleteTextures
#define qglDepthFunc glDepthFunc
#define qglDepthMask glDepthMask
#define qglDepthRange glDepthRange
#define qglDisable glDisable
#define qglDisableClientState glDisableClientState
#define qglDrawArrays glDrawArrays
#define qglDrawElements glDrawElements
#define qglEnable glEnable
#define qglEnableClientState glEnableClientState
#define qglFinish glFinish
#define qglFlush glFlush
#define qglFogf glFogf
#define qglFogfv glFogfv
#define qglFrontFace glFrontFace
#define qglFrustum glFrustum
#define qglGenTextures glGenTextures
#define qglGetError glGetError
#define qglGetFloatv glGetFloatv
#define qglGetIntegerv glGetIntegerv
#define qglGetString glGetString
#define qglHint glHint
#define qglIsEnabled glIsEnabled
#define qglIsTexture glIsTexture
#define qglLightModelf glLightModelf
#define qglLightModelfv glLightModelfv
#define qglLightf glLightf
#define qglLightfv glLightfv
#define qglLineWidth glLineWidth
#define qglLoadIdentity glLoadIdentity
#define qglLoadMatrixf glLoadMatrixf
#define qglLogicOp glLogicOp
#define qglMaterialf glMaterialf
#define qglMaterialfv glMaterialfv
#define qglMatrixMode glMatrixMode
#define qglMultMatrixf glMultMatrixf
#define qglNormal3f glNormal3f
#define qglNormal3fv glNormal3fv
#define qglNormalPointer glNormalPointer
#define qglOrtho glOrtho
#define qglPixelStorei glPixelStorei
#define qglPointSize glPointSize
#define qglPolygonMode glPolygonMode
#define qglPolygonOffset glPolygonOffset
#define qglPopMatrix glPopMatrix
#define qglPushMatrix glPushMatrix
#define qglReadPixels glReadPixels
#define qglRotatef glRotatef
#define qglScalef glScalef
#define qglScissor glScissor
#define qglShadeModel glShadeModel
#define qglStencilFunc glStencilFunc
#define qglStencilMask glStencilMask
#define qglStencilOp glStencilOp
#define qglTexCoordPointer glTexCoordPointer
#define qglTexEnvf glTexEnvf
#define qglTexEnvfv glTexEnvfv
#define qglTexImage2D glTexImage2D
#define qglTexParameterf glTexParameterf
#define qglTexParameterfv glTexParameterfv
#define qglTexSubImage2D glTexSubImage2D
#define qglTranslatef glTranslatef
#define qglVertexPointer glVertexPointer
#define qglViewport glViewport

// OpenGL 3.0 core function
extern PFNGLGENERATEMIPMAPPROC      qglGenerateMipmap;

// GL_ARB_fragment_program
extern PFNGLPROGRAMSTRINGARBPROC            qglProgramStringARB;
extern PFNGLBINDPROGRAMARBPROC              qglBindProgramARB;
extern PFNGLDELETEPROGRAMSARBPROC           qglDeleteProgramsARB;
extern PFNGLGENPROGRAMSARBPROC              qglGenProgramsARB;
extern PFNGLPROGRAMENVPARAMETER4FVARBPROC   qglProgramEnvParameter4fvARB;
extern PFNGLPROGRAMLOCALPARAMETER4FVARBPROC qglProgramLocalParameter4fvARB; 

// GL_ARB_multitexture
extern PFNGLACTIVETEXTUREARBPROC        qglActiveTextureARB;
extern PFNGLCLIENTACTIVETEXTUREARBPROC  qglClientActiveTextureARB;

// GL_ARB_vertex_buffer_object
extern PFNGLBINDBUFFERARBPROC       qglBindBufferARB;
extern PFNGLDELETEBUFFERSARBPROC    qglDeleteBuffersARB;
extern PFNGLGENBUFFERSARBPROC       qglGenBuffersARB;
extern PFNGLBUFFERDATAARBPROC       qglBufferDataARB;
extern PFNGLBUFFERSUBDATAARBPROC    qglBufferSubDataARB;

// GL_EXT_compiled_vertex_array
extern PFNGLLOCKARRAYSEXTPROC       qglLockArraysEXT;
extern PFNGLUNLOCKARRAYSEXTPROC     qglUnlockArraysEXT;

// ==========================================================

#define QGL_ARB_fragment_program            (1 << 0)
#define QGL_ARB_multitexture                (1 << 1)
#define QGL_ARB_vertex_buffer_object        (1 << 2)
#define QGL_EXT_compiled_vertex_array       (1 << 3)
#define QGL_EXT_texture_filter_anisotropic  (1 << 4)

#define QGL_3_0_core_functions              (1U << 31)

#define QGL_Init()                      qtrue
#define QGL_Shutdown()                  QGL_ShutdownExtensions(~0)

void QGL_InitExtensions(unsigned mask);
void QGL_ShutdownExtensions(unsigned mask);

unsigned QGL_ParseExtensionString(const char *s);

#ifdef _DEBUG
#define QGL_EnableLogging(mask)         (void)0
#define QGL_DisableLogging(mask)        (void)0
#define QGL_LogComment(...)             (void)0
#endif

#endif // QGL_H
