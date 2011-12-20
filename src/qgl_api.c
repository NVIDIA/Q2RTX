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
** QGL.C
**
** This file implements the operating system binding of GL to QGL function
** pointers.  When doing a port of Quake2 you must implement the following
** two functions:
**
** QGL_Init() - loads libraries, assigns function pointers, etc.
** QGL_Shutdown() - unloads libraries, NULLs function pointers
*/

#include "common.h"
#include "vid_public.h"
#include "qgl_local.h"
#include "qgl_api.h"

static FILE *log_fp;

void (APIENTRY * qglAlphaFunc)(GLenum func, GLclampf ref);
void (APIENTRY * qglBindTexture)(GLenum target, GLuint texture);
void (APIENTRY * qglBlendFunc)(GLenum sfactor, GLenum dfactor);
void (APIENTRY * qglClear)(GLbitfield mask);
void (APIENTRY * qglClearColor)(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void (APIENTRY * qglClearDepth)(GLclampd depth);
void (APIENTRY * qglClearIndex)(GLfloat c);
void (APIENTRY * qglClearStencil)(GLint s);
void (APIENTRY * qglColor4f)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void (APIENTRY * qglColor4fv)(const GLfloat *v);
void (APIENTRY * qglColor4ub)(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
void (APIENTRY * qglColor4ubv)(const GLubyte *v);
void (APIENTRY * qglColorMask)(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void (APIENTRY * qglColorPointer)(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void (APIENTRY * qglCopyTexImage2D)(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
void (APIENTRY * qglCopyTexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void (APIENTRY * qglCullFace)(GLenum mode);
void (APIENTRY * qglDeleteTextures)(GLsizei n, const GLuint *textures);
void (APIENTRY * qglDepthFunc)(GLenum func);
void (APIENTRY * qglDepthMask)(GLboolean flag);
void (APIENTRY * qglDepthRange)(GLclampd zNear, GLclampd zFar);
void (APIENTRY * qglDisable)(GLenum cap);
void (APIENTRY * qglDisableClientState)(GLenum array);
void (APIENTRY * qglDrawArrays)(GLenum mode, GLint first, GLsizei count);
void (APIENTRY * qglDrawElements)(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void (APIENTRY * qglEnable)(GLenum cap);
void (APIENTRY * qglEnableClientState)(GLenum array);
void (APIENTRY * qglFinish)(void);
void (APIENTRY * qglFlush)(void);
void (APIENTRY * qglFogf)(GLenum pname, GLfloat param);
void (APIENTRY * qglFogfv)(GLenum pname, const GLfloat *params);
void (APIENTRY * qglFrontFace)(GLenum mode);
void (APIENTRY * qglFrustum)(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void (APIENTRY * qglGenTextures)(GLsizei n, GLuint *textures);
void (APIENTRY * qglGetBooleanv)(GLenum pname, GLboolean *params);
GLenum (APIENTRY * qglGetError)(void);
void (APIENTRY * qglGetFloatv)(GLenum pname, GLfloat *params);
void (APIENTRY * qglGetIntegerv)(GLenum pname, GLint *params);
void (APIENTRY * qglGetLightfv)(GLenum light, GLenum pname, GLfloat *params);
void (APIENTRY * qglGetMaterialfv)(GLenum face, GLenum pname, GLfloat *params);
void (APIENTRY * qglGetPointerv)(GLenum pname, GLvoid* *params);
const GLubyte * (APIENTRY * qglGetString)(GLenum name);
void (APIENTRY * qglGetTexEnvfv)(GLenum target, GLenum pname, GLfloat *params);
void (APIENTRY * qglGetTexEnviv)(GLenum target, GLenum pname, GLint *params);
void (APIENTRY * qglGetTexParameterfv)(GLenum target, GLenum pname, GLfloat *params);
void (APIENTRY * qglGetTexParameteriv)(GLenum target, GLenum pname, GLint *params);
void (APIENTRY * qglHint)(GLenum target, GLenum mode);
GLboolean (APIENTRY * qglIsEnabled)(GLenum cap);
GLboolean (APIENTRY * qglIsTexture)(GLuint texture);
void (APIENTRY * qglLightModelf)(GLenum pname, GLfloat param);
void (APIENTRY * qglLightModelfv)(GLenum pname, const GLfloat *params);
void (APIENTRY * qglLightf)(GLenum light, GLenum pname, GLfloat param);
void (APIENTRY * qglLightfv)(GLenum light, GLenum pname, const GLfloat *params);
void (APIENTRY * qglLineWidth)(GLfloat width);
void (APIENTRY * qglLoadIdentity)(void);
void (APIENTRY * qglLoadMatrixf)(const GLfloat *m);
void (APIENTRY * qglLogicOp)(GLenum opcode);
void (APIENTRY * qglMaterialf)(GLenum face, GLenum pname, GLfloat param);
void (APIENTRY * qglMaterialfv)(GLenum face, GLenum pname, const GLfloat *params);
void (APIENTRY * qglMatrixMode)(GLenum mode);
void (APIENTRY * qglMultMatrixf)(const GLfloat *m);
void (APIENTRY * qglNormal3f)(GLfloat nx, GLfloat ny, GLfloat nz);
void (APIENTRY * qglNormal3fv)(const GLfloat *v);
void (APIENTRY * qglNormalPointer)(GLenum type, GLsizei stride, const GLvoid *pointer);
void (APIENTRY * qglOrtho)(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void (APIENTRY * qglPixelStorei)(GLenum pname, GLint param);
void (APIENTRY * qglPointSize)(GLfloat size);
void (APIENTRY * qglPolygonMode)(GLenum face, GLenum mode);
void (APIENTRY * qglPolygonOffset)(GLfloat factor, GLfloat units);
void (APIENTRY * qglPopMatrix)(void);
void (APIENTRY * qglPushMatrix)(void);
void (APIENTRY * qglReadPixels)(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
void (APIENTRY * qglRotatef)(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void (APIENTRY * qglScalef)(GLfloat x, GLfloat y, GLfloat z);
void (APIENTRY * qglScissor)(GLint x, GLint y, GLsizei width, GLsizei height);
void (APIENTRY * qglShadeModel)(GLenum mode);
void (APIENTRY * qglStencilFunc)(GLenum func, GLint ref, GLuint mask);
void (APIENTRY * qglStencilMask)(GLuint mask);
void (APIENTRY * qglStencilOp)(GLenum fail, GLenum zfail, GLenum zpass);
void (APIENTRY * qglTexCoordPointer)(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void (APIENTRY * qglTexEnvf)(GLenum target, GLenum pname, GLfloat param);
void (APIENTRY * qglTexEnvfv)(GLenum target, GLenum pname, const GLfloat *params);
void (APIENTRY * qglTexEnvi)(GLenum target, GLenum pname, GLint param);
void (APIENTRY * qglTexEnviv)(GLenum target, GLenum pname, const GLint *params);
void (APIENTRY * qglTexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void (APIENTRY * qglTexParameterf)(GLenum target, GLenum pname, GLfloat param);
void (APIENTRY * qglTexParameterfv)(GLenum target, GLenum pname, const GLfloat *params);
void (APIENTRY * qglTexParameteri)(GLenum target, GLenum pname, GLint param);
void (APIENTRY * qglTexParameteriv)(GLenum target, GLenum pname, const GLint *params);
void (APIENTRY * qglTexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void (APIENTRY * qglTranslatef)(GLfloat x, GLfloat y, GLfloat z);
void (APIENTRY * qglVertexPointer)(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void (APIENTRY * qglViewport)(GLint x, GLint y, GLsizei width, GLsizei height);

//
// OS-specific
//

#ifdef _WIN32
// this one is defined in win_wgl.c
extern PROC(WINAPI * qwglGetProcAddress)(LPCSTR);
#endif

//
// extensions
//

// GL_EXT_compiled_vertex_array
void (APIENTRY * qglLockArraysEXT)(GLint first, GLsizei count);
void (APIENTRY * qglUnlockArraysEXT)(void);

// GL_ARB_multitexture
void (APIENTRY * qglActiveTextureARB)(GLenum texture);
void (APIENTRY * qglClientActiveTextureARB)(GLenum texture);

// GL_ARB_fragment_program
void (APIENTRY * qglProgramStringARB)(GLenum target, GLenum format, GLsizei len, const GLvoid *string);
void (APIENTRY * qglBindProgramARB)(GLenum target, GLuint program);
void (APIENTRY * qglDeleteProgramsARB)(GLsizei n, const GLuint *programs);
void (APIENTRY * qglGenProgramsARB)(GLsizei n, GLuint *programs);
void (APIENTRY * qglProgramEnvParameter4fvARB)(GLenum target, GLuint index, const GLfloat *params);
void (APIENTRY * qglProgramLocalParameter4fvARB)(GLenum target, GLuint index, const GLfloat *params);
void (APIENTRY * qglGetProgramEnvParameterfvARB)(GLenum, GLuint, GLfloat *);
void (APIENTRY * qglGetProgramLocalParameterfvARB)(GLenum, GLuint, GLfloat *);
void (APIENTRY * qglGetProgramivARB)(GLenum, GLenum, GLint *);
void (APIENTRY * qglGetProgramStringARB)(GLenum, GLenum, GLvoid *);
GLboolean (APIENTRY * qglIsProgramARB)(GLuint);

// GL_ARB_vertex_buffer_object
void (APIENTRY * qglBindBufferARB)(GLenum target, GLuint buffer);
void (APIENTRY * qglDeleteBuffersARB)(GLsizei n, const GLuint *buffers);
void (APIENTRY * qglGenBuffersARB)(GLsizei n, GLuint *buffers);
GLboolean (APIENTRY * qglIsBufferARB)(GLuint);
void (APIENTRY * qglBufferDataARB)(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
void (APIENTRY * qglBufferSubDataARB)(GLenum, GLintptrARB, GLsizeiptrARB, const GLvoid *);
void (APIENTRY * qglGetBufferSubDataARB)(GLenum, GLintptrARB, GLsizeiptrARB, GLvoid *);
GLvoid * (APIENTRY * qglMapBufferARB)(GLenum target, GLenum access);
GLboolean (APIENTRY * qglUnmapBufferARB)(GLenum target);
void (APIENTRY * qglGetBufferParameterivARB)(GLenum, GLenum, GLint *);
void (APIENTRY * qglGetBufferPointervARB)(GLenum, GLenum, GLvoid* *);

// ==========================================================

static void (APIENTRY * dllAlphaFunc)(GLenum func, GLclampf ref);
static void (APIENTRY * dllBindTexture)(GLenum target, GLuint texture);
static void (APIENTRY * dllBlendFunc)(GLenum sfactor, GLenum dfactor);
static void (APIENTRY * dllClear)(GLbitfield mask);
static void (APIENTRY * dllClearColor)(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
static void (APIENTRY * dllClearDepth)(GLclampd depth);
static void (APIENTRY * dllClearIndex)(GLfloat c);
static void (APIENTRY * dllClearStencil)(GLint s);
static void (APIENTRY * dllColor4f)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
static void (APIENTRY * dllColor4fv)(const GLfloat *v);
static void (APIENTRY * dllColor4ub)(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
static void (APIENTRY * dllColor4ubv)(const GLubyte *v);
static void (APIENTRY * dllColorMask)(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
static void (APIENTRY * dllColorPointer)(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
static void (APIENTRY * dllCopyTexImage2D)(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
static void (APIENTRY * dllCopyTexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
static void (APIENTRY * dllCullFace)(GLenum mode);
static void (APIENTRY * dllDeleteTextures)(GLsizei n, const GLuint *textures);
static void (APIENTRY * dllDepthFunc)(GLenum func);
static void (APIENTRY * dllDepthMask)(GLboolean flag);
static void (APIENTRY * dllDepthRange)(GLclampd zNear, GLclampd zFar);
static void (APIENTRY * dllDisable)(GLenum cap);
static void (APIENTRY * dllDisableClientState)(GLenum array);
static void (APIENTRY * dllDrawArrays)(GLenum mode, GLint first, GLsizei count);
static void (APIENTRY * dllDrawElements)(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
static void (APIENTRY * dllEnable)(GLenum cap);
static void (APIENTRY * dllEnableClientState)(GLenum array);
static void (APIENTRY * dllFinish)(void);
static void (APIENTRY * dllFlush)(void);
static void (APIENTRY * dllFogf)(GLenum pname, GLfloat param);
static void (APIENTRY * dllFogfv)(GLenum pname, const GLfloat *params);
static void (APIENTRY * dllFrontFace)(GLenum mode);
static void (APIENTRY * dllFrustum)(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
static void (APIENTRY * dllGenTextures)(GLsizei n, GLuint *textures);
static void (APIENTRY * dllGetBooleanv)(GLenum pname, GLboolean *params);
static GLenum (APIENTRY * dllGetError)(void);
static void (APIENTRY * dllGetFloatv)(GLenum pname, GLfloat *params);
static void (APIENTRY * dllGetIntegerv)(GLenum pname, GLint *params);
static void (APIENTRY * dllGetLightfv)(GLenum light, GLenum pname, GLfloat *params);
static void (APIENTRY * dllGetMaterialfv)(GLenum face, GLenum pname, GLfloat *params);
static void (APIENTRY * dllGetPointerv)(GLenum pname, GLvoid* *params);
static const GLubyte * (APIENTRY * dllGetString)(GLenum name);
static void (APIENTRY * dllGetTexEnvfv)(GLenum target, GLenum pname, GLfloat *params);
static void (APIENTRY * dllGetTexEnviv)(GLenum target, GLenum pname, GLint *params);
static void (APIENTRY * dllGetTexParameterfv)(GLenum target, GLenum pname, GLfloat *params);
static void (APIENTRY * dllGetTexParameteriv)(GLenum target, GLenum pname, GLint *params);
static void (APIENTRY * dllHint)(GLenum target, GLenum mode);
static GLboolean (APIENTRY * dllIsEnabled)(GLenum cap);
static GLboolean (APIENTRY * dllIsTexture)(GLuint texture);
static void (APIENTRY * dllLightModelf)(GLenum pname, GLfloat param);
static void (APIENTRY * dllLightModelfv)(GLenum pname, const GLfloat *params);
static void (APIENTRY * dllLightf)(GLenum light, GLenum pname, GLfloat param);
static void (APIENTRY * dllLightfv)(GLenum light, GLenum pname, const GLfloat *params);
static void (APIENTRY * dllLineWidth)(GLfloat width);
static void (APIENTRY * dllLoadIdentity)(void);
static void (APIENTRY * dllLoadMatrixf)(const GLfloat *m);
static void (APIENTRY * dllLogicOp)(GLenum opcode);
static void (APIENTRY * dllMaterialf)(GLenum face, GLenum pname, GLfloat param);
static void (APIENTRY * dllMaterialfv)(GLenum face, GLenum pname, const GLfloat *params);
static void (APIENTRY * dllMatrixMode)(GLenum mode);
static void (APIENTRY * dllMultMatrixf)(const GLfloat *m);
static void (APIENTRY * dllNormal3f)(GLfloat nx, GLfloat ny, GLfloat nz);
static void (APIENTRY * dllNormal3fv)(const GLfloat *v);
static void (APIENTRY * dllNormalPointer)(GLenum type, GLsizei stride, const GLvoid *pointer);
static void (APIENTRY * dllOrtho)(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
static void (APIENTRY * dllPixelStorei)(GLenum pname, GLint param);
static void (APIENTRY * dllPointSize)(GLfloat size);
static void (APIENTRY * dllPolygonMode)(GLenum face, GLenum mode);
static void (APIENTRY * dllPolygonOffset)(GLfloat factor, GLfloat units);
static void (APIENTRY * dllPopMatrix)(void);
static void (APIENTRY * dllPushMatrix)(void);
static void (APIENTRY * dllReadPixels)(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
static void (APIENTRY * dllRotatef)(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
static void (APIENTRY * dllScalef)(GLfloat x, GLfloat y, GLfloat z);
static void (APIENTRY * dllScissor)(GLint x, GLint y, GLsizei width, GLsizei height);
static void (APIENTRY * dllShadeModel)(GLenum mode);
static void (APIENTRY * dllStencilFunc)(GLenum func, GLint ref, GLuint mask);
static void (APIENTRY * dllStencilMask)(GLuint mask);
static void (APIENTRY * dllStencilOp)(GLenum fail, GLenum zfail, GLenum zpass);
static void (APIENTRY * dllTexCoordPointer)(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
static void (APIENTRY * dllTexEnvf)(GLenum target, GLenum pname, GLfloat param);
static void (APIENTRY * dllTexEnvfv)(GLenum target, GLenum pname, const GLfloat *params);
static void (APIENTRY * dllTexEnvi)(GLenum target, GLenum pname, GLint param);
static void (APIENTRY * dllTexEnviv)(GLenum target, GLenum pname, const GLint *params);
static void (APIENTRY * dllTexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
static void (APIENTRY * dllTexParameterf)(GLenum target, GLenum pname, GLfloat param);
static void (APIENTRY * dllTexParameterfv)(GLenum target, GLenum pname, const GLfloat *params);
static void (APIENTRY * dllTexParameteri)(GLenum target, GLenum pname, GLint param);
static void (APIENTRY * dllTexParameteriv)(GLenum target, GLenum pname, const GLint *params);
static void (APIENTRY * dllTexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
static void (APIENTRY * dllTranslatef)(GLfloat x, GLfloat y, GLfloat z);
static void (APIENTRY * dllVertexPointer)(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
static void (APIENTRY * dllViewport)(GLint x, GLint y, GLsizei width, GLsizei height);

#define SIG(x) fprintf(log_fp, x "\n")

static void APIENTRY logAlphaFunc(GLenum func, GLclampf ref)
{
    fprintf(log_fp, "glAlphaFunc( 0x%x, %f )\n", func, ref);
    dllAlphaFunc(func, ref);
}

static void APIENTRY logBindTexture(GLenum target, GLuint texture)
{
    fprintf(log_fp, "glBindTexture( 0x%x, %u )\n", target, texture);
    dllBindTexture(target, texture);
}

static void APIENTRY logBlendFunc(GLenum sfactor, GLenum dfactor)
{
    fprintf(log_fp, "glBlendFunc( 0x%x, 0x%x )\n", sfactor, dfactor);
    dllBlendFunc(sfactor, dfactor);
}

static void APIENTRY logClear(GLbitfield mask)
{
    fprintf(log_fp, "glClear\n");
    dllClear(mask);
}

static void APIENTRY logClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    fprintf(log_fp, "glClearColor\n");
    dllClearColor(red, green, blue, alpha);
}

static void APIENTRY logClearDepth(GLclampd depth)
{
    fprintf(log_fp, "glClearDepth\n");
    dllClearDepth(depth);
}

static void APIENTRY logClearIndex(GLfloat c)
{
    fprintf(log_fp, "glClearIndex\n");
    dllClearIndex(c);
}

static void APIENTRY logClearStencil(GLint s)
{
    fprintf(log_fp, "glClearStencil\n");
    dllClearStencil(s);
}

static void APIENTRY logColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    fprintf(log_fp, "glColor4f( %f,%f,%f,%f )\n", red, green, blue, alpha);
    dllColor4f(red, green, blue, alpha);
}

static void APIENTRY logColor4fv(const GLfloat *v)
{
    fprintf(log_fp, "glColor4fv( %f,%f,%f,%f )\n", v[0], v[1], v[2], v[3]);
    dllColor4fv(v);
}

static void APIENTRY logColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    SIG("glColor4ub");
    dllColor4ub(red, green, blue, alpha);
}

static void APIENTRY logColor4ubv(const GLubyte *v)
{
    SIG("glColor4ubv");
    dllColor4ubv(v);
}

static void APIENTRY logColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    SIG("glColorMask");
    dllColorMask(red, green, blue, alpha);
}

static void APIENTRY logColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    SIG("glColorPointer");
    dllColorPointer(size, type, stride, pointer);
}

static void APIENTRY logCopyTexImage2D(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border)
{
    SIG("glCopyTexImage2D");
    dllCopyTexImage2D(target, level, internalFormat, x, y, width, height, border);
}

static void APIENTRY logCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height)
{
    SIG("glCopyTexSubImage2D");
    dllCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
}

static void APIENTRY logCullFace(GLenum mode)
{
    SIG("glCullFace");
    dllCullFace(mode);
}

static void APIENTRY logDeleteTextures(GLsizei n, const GLuint *textures)
{
    SIG("glDeleteTextures");
    dllDeleteTextures(n, textures);
}

static void APIENTRY logDepthFunc(GLenum func)
{
    SIG("glDepthFunc");
    dllDepthFunc(func);
}

static void APIENTRY logDepthMask(GLboolean flag)
{
    SIG("glDepthMask");
    dllDepthMask(flag);
}

static void APIENTRY logDepthRange(GLclampd zNear, GLclampd zFar)
{
    SIG("glDepthRange");
    dllDepthRange(zNear, zFar);
}

static void APIENTRY logDisable(GLenum cap)
{
    fprintf(log_fp, "glDisable( 0x%x )\n", cap);
    dllDisable(cap);
}

static void APIENTRY logDisableClientState(GLenum array)
{
    SIG("glDisableClientState");
    dllDisableClientState(array);
}

static void APIENTRY logDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    SIG("glDrawArrays");
    dllDrawArrays(mode, first, count);
}

static void APIENTRY logDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    SIG("glDrawElements");
    dllDrawElements(mode, count, type, indices);
}

static void APIENTRY logEnable(GLenum cap)
{
    fprintf(log_fp, "glEnable( 0x%x )\n", cap);
    dllEnable(cap);
}

static void APIENTRY logEnableClientState(GLenum array)
{
    SIG("glEnableClientState");
    dllEnableClientState(array);
}

static void APIENTRY logFinish(void)
{
    SIG("glFinish");
    dllFinish();
}

static void APIENTRY logFlush(void)
{
    SIG("glFlush");
    dllFlush();
}

static void APIENTRY logFogf(GLenum pname, GLfloat param)
{
    SIG("glFogf");
    dllFogf(pname, param);
}

static void APIENTRY logFogfv(GLenum pname, const GLfloat *params)
{
    SIG("glFogfv");
    dllFogfv(pname, params);
}

static void APIENTRY logFrontFace(GLenum mode)
{
    SIG("glFrontFace");
    dllFrontFace(mode);
}

static void APIENTRY logFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    SIG("glFrustum");
    dllFrustum(left, right, bottom, top, zNear, zFar);
}

static void APIENTRY logGenTextures(GLsizei n, GLuint *textures)
{
    SIG("glGenTextures");
    dllGenTextures(n, textures);
}

static void APIENTRY logGetBooleanv(GLenum pname, GLboolean *params)
{
    SIG("glGetBooleanv");
    dllGetBooleanv(pname, params);
}

static GLenum APIENTRY logGetError(void)
{
    SIG("glGetError");
    return dllGetError();
}

static void APIENTRY logGetFloatv(GLenum pname, GLfloat *params)
{
    SIG("glGetFloatv");
    dllGetFloatv(pname, params);
}

static void APIENTRY logGetIntegerv(GLenum pname, GLint *params)
{
    SIG("glGetIntegerv");
    dllGetIntegerv(pname, params);
}

static void APIENTRY logGetLightfv(GLenum light, GLenum pname, GLfloat *params)
{
    SIG("glGetLightfv");
    dllGetLightfv(light, pname, params);
}

static void APIENTRY logGetMaterialfv(GLenum face, GLenum pname, GLfloat *params)
{
    SIG("glGetMaterialfv");
    dllGetMaterialfv(face, pname, params);
}

static void APIENTRY logGetPointerv(GLenum pname, GLvoid* *params)
{
    SIG("glGetPointerv");
    dllGetPointerv(pname, params);
}

static const GLubyte * APIENTRY logGetString(GLenum name)
{
    SIG("glGetString");
    return dllGetString(name);
}

static void APIENTRY logGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params)
{
    SIG("glGetTexEnvfv");
    dllGetTexEnvfv(target, pname, params);
}

static void APIENTRY logGetTexEnviv(GLenum target, GLenum pname, GLint *params)
{
    SIG("glGetTexEnviv");
    dllGetTexEnviv(target, pname, params);
}

static void APIENTRY logGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
    SIG("glGetTexParameterfv");
    dllGetTexParameterfv(target, pname, params);
}

static void APIENTRY logGetTexParameteriv(GLenum target, GLenum pname, GLint *params)
{
    SIG("glGetTexParameteriv");
    dllGetTexParameteriv(target, pname, params);
}

static void APIENTRY logHint(GLenum target, GLenum mode)
{
    fprintf(log_fp, "glHint( 0x%x, 0x%x )\n", target, mode);
    dllHint(target, mode);
}

static GLboolean APIENTRY logIsEnabled(GLenum cap)
{
    SIG("glIsEnabled");
    return dllIsEnabled(cap);
}

static GLboolean APIENTRY logIsTexture(GLuint texture)
{
    SIG("glIsTexture");
    return dllIsTexture(texture);
}

static void APIENTRY logLightModelf(GLenum pname, GLfloat param)
{
    SIG("glLightModelf");
    dllLightModelf(pname, param);
}

static void APIENTRY logLightModelfv(GLenum pname, const GLfloat *params)
{
    SIG("glLightModelfv");
    dllLightModelfv(pname, params);
}

static void APIENTRY logLightf(GLenum light, GLenum pname, GLfloat param)
{
    SIG("glLightf");
    dllLightf(light, pname, param);
}

static void APIENTRY logLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    SIG("glLightfv");
    dllLightfv(light, pname, params);
}

static void APIENTRY logLineWidth(GLfloat width)
{
    SIG("glLineWidth");
    dllLineWidth(width);
}

static void APIENTRY logLoadIdentity(void)
{
    SIG("glLoadIdentity");
    dllLoadIdentity();
}

static void APIENTRY logLoadMatrixf(const GLfloat *m)
{
    SIG("glLoadMatrixf");
    dllLoadMatrixf(m);
}

static void APIENTRY logLogicOp(GLenum opcode)
{
    SIG("glLogicOp");
    dllLogicOp(opcode);
}

static void APIENTRY logMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    SIG("glMaterialf");
    dllMaterialf(face, pname, param);
}

static void APIENTRY logMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    SIG("glMaterialfv");
    dllMaterialfv(face, pname, params);
}

static void APIENTRY logMatrixMode(GLenum mode)
{
    SIG("glMatrixMode");
    dllMatrixMode(mode);
}

static void APIENTRY logMultMatrixf(const GLfloat *m)
{
    SIG("glMultMatrixf");
    dllMultMatrixf(m);
}

static void APIENTRY logNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
    SIG("glNormal3f");
    dllNormal3f(nx, ny, nz);
}

static void APIENTRY logNormal3fv(const GLfloat *v)
{
    SIG("glNormal3fv");
    dllNormal3fv(v);
}

static void APIENTRY logNormalPointer(GLenum type, GLsizei stride, const void *pointer)
{
    SIG("glNormalPointer");
    dllNormalPointer(type, stride, pointer);
}

static void APIENTRY logOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    SIG("glOrtho");
    dllOrtho(left, right, bottom, top, zNear, zFar);
}

static void APIENTRY logPixelStorei(GLenum pname, GLint param)
{
    SIG("glPixelStorei");
    dllPixelStorei(pname, param);
}

static void APIENTRY logPointSize(GLfloat size)
{
    SIG("glPointSize");
    dllPointSize(size);
}

static void APIENTRY logPolygonMode(GLenum face, GLenum mode)
{
    fprintf(log_fp, "glPolygonMode( 0x%x, 0x%x )\n", face, mode);
    dllPolygonMode(face, mode);
}

static void APIENTRY logPolygonOffset(GLfloat factor, GLfloat units)
{
    SIG("glPolygonOffset");
    dllPolygonOffset(factor, units);
}

static void APIENTRY logPopMatrix(void)
{
    SIG("glPopMatrix");
    dllPopMatrix();
}

static void APIENTRY logPushMatrix(void)
{
    SIG("glPushMatrix");
    dllPushMatrix();
}

static void APIENTRY logReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels)
{
    SIG("glReadPixels");
    dllReadPixels(x, y, width, height, format, type, pixels);
}

static void APIENTRY logRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    SIG("glRotatef");
    dllRotatef(angle, x, y, z);
}

static void APIENTRY logScalef(GLfloat x, GLfloat y, GLfloat z)
{
    SIG("glScalef");
    dllScalef(x, y, z);
}

static void APIENTRY logScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    SIG("glScissor");
    dllScissor(x, y, width, height);
}

static void APIENTRY logShadeModel(GLenum mode)
{
    SIG("glShadeModel");
    dllShadeModel(mode);
}

static void APIENTRY logStencilFunc(GLenum func, GLint ref, GLuint mask)
{
    SIG("glStencilFunc");
    dllStencilFunc(func, ref, mask);
}

static void APIENTRY logStencilMask(GLuint mask)
{
    SIG("glStencilMask");
    dllStencilMask(mask);
}

static void APIENTRY logStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
    SIG("glStencilOp");
    dllStencilOp(fail, zfail, zpass);
}

static void APIENTRY logTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    SIG("glTexCoordPointer");
    dllTexCoordPointer(size, type, stride, pointer);
}

static void APIENTRY logTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    fprintf(log_fp, "glTexEnvf( 0x%x, 0x%x, %f )\n", target, pname, param);
    dllTexEnvf(target, pname, param);
}

static void APIENTRY logTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    SIG("glTexEnvfv");
    dllTexEnvfv(target, pname, params);
}

static void APIENTRY logTexEnvi(GLenum target, GLenum pname, GLint param)
{
    fprintf(log_fp, "glTexEnvi( 0x%x, 0x%x, 0x%x )\n", target, pname, param);
    dllTexEnvi(target, pname, param);
}

static void APIENTRY logTexEnviv(GLenum target, GLenum pname, const GLint *params)
{
    SIG("glTexEnviv");
    dllTexEnviv(target, pname, params);
}

static void APIENTRY logTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels)
{
    SIG("glTexImage2D");
    dllTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
}

static void APIENTRY logTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    fprintf(log_fp, "glTexParameterf( 0x%x, 0x%x, %f )\n", target, pname, param);
    dllTexParameterf(target, pname, param);
}

static void APIENTRY logTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
    SIG("glTexParameterfv");
    dllTexParameterfv(target, pname, params);
}

static void APIENTRY logTexParameteri(GLenum target, GLenum pname, GLint param)
{
    fprintf(log_fp, "glTexParameteri( 0x%x, 0x%x, 0x%x )\n", target, pname, param);
    dllTexParameteri(target, pname, param);
}

static void APIENTRY logTexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
    SIG("glTexParameteriv");
    dllTexParameteriv(target, pname, params);
}

static void APIENTRY logTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)
{
    SIG("glTexSubImage2D");
    dllTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
}

static void APIENTRY logTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    SIG("glTranslatef");
    dllTranslatef(x, y, z);
}

static void APIENTRY logVertexPointer(GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    SIG("glVertexPointer");
    dllVertexPointer(size, type, stride, pointer);
}

static void APIENTRY logViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    SIG("glViewport");
    dllViewport(x, y, width, height);
}

/*
** QGL_Shutdown
**
** Unloads the specified DLL then nulls out all the proc pointers.
*/
void QGL_Shutdown(void)
{
    qglAlphaFunc                = NULL;
    qglBindTexture              = NULL;
    qglBlendFunc                = NULL;
    qglClear                    = NULL;
    qglClearColor               = NULL;
    qglClearDepth               = NULL;
    qglClearIndex               = NULL;
    qglClearStencil             = NULL;
    qglColor4f                  = NULL;
    qglColor4fv                 = NULL;
    qglColor4ub                 = NULL;
    qglColor4ubv                = NULL;
    qglColorMask                = NULL;
    qglColorPointer             = NULL;
    qglCopyTexImage2D           = NULL;
    qglCopyTexSubImage2D        = NULL;
    qglCullFace                 = NULL;
    qglDeleteTextures           = NULL;
    qglDepthFunc                = NULL;
    qglDepthMask                = NULL;
    qglDepthRange               = NULL;
    qglDisable                  = NULL;
    qglDisableClientState       = NULL;
    qglDrawArrays               = NULL;
    qglDrawElements             = NULL;
    qglEnable                   = NULL;
    qglEnableClientState        = NULL;
    qglFinish                   = NULL;
    qglFlush                    = NULL;
    qglFogf                     = NULL;
    qglFogfv                    = NULL;
    qglFrontFace                = NULL;
    qglFrustum                  = NULL;
    qglGenTextures              = NULL;
    qglGetBooleanv              = NULL;
    qglGetError                 = NULL;
    qglGetFloatv                = NULL;
    qglGetIntegerv              = NULL;
    qglGetLightfv               = NULL;
    qglGetMaterialfv            = NULL;
    qglGetPointerv              = NULL;
    qglGetString                = NULL;
    qglGetTexEnvfv              = NULL;
    qglGetTexEnviv              = NULL;
    qglGetTexParameterfv        = NULL;
    qglGetTexParameteriv        = NULL;
    qglHint                     = NULL;
    qglIsEnabled                = NULL;
    qglIsTexture                = NULL;
    qglLightModelf              = NULL;
    qglLightModelfv             = NULL;
    qglLightf                   = NULL;
    qglLightfv                  = NULL;
    qglLineWidth                = NULL;
    qglLoadIdentity             = NULL;
    qglLoadMatrixf              = NULL;
    qglLogicOp                  = NULL;
    qglMaterialf                = NULL;
    qglMaterialfv               = NULL;
    qglMatrixMode               = NULL;
    qglMultMatrixf              = NULL;
    qglNormal3f                 = NULL;
    qglNormal3fv                = NULL;
    qglNormalPointer            = NULL;
    qglOrtho                    = NULL;
    qglPixelStorei              = NULL;
    qglPointSize                = NULL;
    qglPolygonMode              = NULL;
    qglPolygonOffset            = NULL;
    qglPopMatrix                = NULL;
    qglPushMatrix               = NULL;
    qglReadPixels               = NULL;
    qglRotatef                  = NULL;
    qglScalef                   = NULL;
    qglScissor                  = NULL;
    qglShadeModel               = NULL;
    qglStencilFunc              = NULL;
    qglStencilMask              = NULL;
    qglStencilOp                = NULL;
    qglTexCoordPointer          = NULL;
    qglTexEnvf                  = NULL;
    qglTexEnvfv                 = NULL;
    qglTexEnvi                  = NULL;
    qglTexEnviv                 = NULL;
    qglTexImage2D               = NULL;
    qglTexParameterf            = NULL;
    qglTexParameterfv           = NULL;
    qglTexParameteri            = NULL;
    qglTexParameteriv           = NULL;
    qglTexSubImage2D            = NULL;
    qglTranslatef               = NULL;
    qglVertexPointer            = NULL;
    qglViewport                 = NULL;

    QGL_ShutdownExtensions(~0);
}

void QGL_ShutdownExtensions(unsigned mask)
{
    if (mask & QGL_EXT_compiled_vertex_array) {
        qglLockArraysEXT            = NULL;
        qglUnlockArraysEXT          = NULL;
    }

    if (mask & QGL_ARB_multitexture) {
        qglActiveTextureARB         = NULL;
        qglClientActiveTextureARB   = NULL;
    }

    if (mask & QGL_ARB_fragment_program) {
        qglProgramStringARB                 = NULL;
        qglBindProgramARB                   = NULL;
        qglDeleteProgramsARB                = NULL;
        qglGenProgramsARB                   = NULL;
        qglProgramEnvParameter4fvARB        = NULL;
        qglProgramLocalParameter4fvARB      = NULL;
        qglGetProgramEnvParameterfvARB      = NULL;
        qglGetProgramLocalParameterfvARB    = NULL;
        qglGetProgramivARB                  = NULL;
        qglGetProgramStringARB              = NULL;
        qglIsProgramARB                     = NULL;
    }

    if (mask & QGL_ARB_vertex_buffer_object) {
        qglBindBufferARB            = NULL;
        qglDeleteBuffersARB         = NULL;
        qglGenBuffersARB            = NULL;
        qglIsBufferARB              = NULL;
        qglBufferDataARB            = NULL;
        qglBufferSubDataARB         = NULL;
        qglGetBufferSubDataARB      = NULL;
        qglMapBufferARB             = NULL;
        qglUnmapBufferARB           = NULL;
        qglGetBufferParameterivARB  = NULL;
        qglGetBufferPointervARB     = NULL;
    }
}

#define GPA(a)  VID_GetProcAddr(a)

/*
** QGL_Init
**
** This is responsible for binding our qgl function pointers to
** the appropriate GL stuff.  In Windows this means doing a
** LoadLibrary and a bunch of calls to GetProcAddress.  On other
** operating systems we need to do the right thing, whatever that
** might be.
**
*/
void QGL_Init(void)
{
    qglAlphaFunc                = dllAlphaFunc              = GPA("glAlphaFunc");
    qglBindTexture              = dllBindTexture            = GPA("glBindTexture");
    qglBlendFunc                = dllBlendFunc              = GPA("glBlendFunc");
    qglClear                    = dllClear                  = GPA("glClear");
    qglClearColor               = dllClearColor             = GPA("glClearColor");
    qglClearDepth               = dllClearDepth             = GPA("glClearDepth");
    qglClearIndex               = dllClearIndex             = GPA("glClearIndex");
    qglClearStencil             = dllClearStencil           = GPA("glClearStencil");
    qglColor4f                  = dllColor4f                = GPA("glColor4f");
    qglColor4fv                 = dllColor4fv               = GPA("glColor4fv");
    qglColor4ub                 = dllColor4ub               = GPA("glColor4ub");
    qglColor4ubv                = dllColor4ubv              = GPA("glColor4ubv");
    qglColorMask                = dllColorMask              = GPA("glColorMask");
    qglColorPointer             = dllColorPointer           = GPA("glColorPointer");
    qglCopyTexImage2D           = dllCopyTexImage2D         = GPA("glCopyTexImage2D");
    qglCopyTexSubImage2D        = dllCopyTexSubImage2D      = GPA("glCopyTexSubImage2D");
    qglCullFace                 = dllCullFace               = GPA("glCullFace");
    qglDeleteTextures           = dllDeleteTextures         = GPA("glDeleteTextures");
    qglDepthFunc                = dllDepthFunc              = GPA("glDepthFunc");
    qglDepthMask                = dllDepthMask              = GPA("glDepthMask");
    qglDepthRange               = dllDepthRange             = GPA("glDepthRange");
    qglDisable                  = dllDisable                = GPA("glDisable");
    qglDisableClientState       = dllDisableClientState     = GPA("glDisableClientState");
    qglDrawArrays               = dllDrawArrays             = GPA("glDrawArrays");
    qglDrawElements             = dllDrawElements           = GPA("glDrawElements");
    qglEnable                   = dllEnable                 = GPA("glEnable");
    qglEnableClientState        = dllEnableClientState      = GPA("glEnableClientState");
    qglFinish                   = dllFinish                 = GPA("glFinish");
    qglFlush                    = dllFlush                  = GPA("glFlush");
    qglFogf                     = dllFogf                   = GPA("glFogf");
    qglFogfv                    = dllFogfv                  = GPA("glFogfv");
    qglFrontFace                = dllFrontFace              = GPA("glFrontFace");
    qglFrustum                  = dllFrustum                = GPA("glFrustum");
    qglGenTextures              = dllGenTextures            = GPA("glGenTextures");
    qglGetBooleanv              = dllGetBooleanv            = GPA("glGetBooleanv");
    qglGetError                 = dllGetError               = GPA("glGetError");
    qglGetFloatv                = dllGetFloatv              = GPA("glGetFloatv");
    qglGetIntegerv              = dllGetIntegerv            = GPA("glGetIntegerv");
    qglGetLightfv               = dllGetLightfv             = GPA("glGetLightfv");
    qglGetMaterialfv            = dllGetMaterialfv          = GPA("glGetMaterialfv");
    qglGetPointerv              = dllGetPointerv            = GPA("glGetPointerv");
    qglGetString                = dllGetString              = GPA("glGetString");
    qglGetTexEnvfv              = dllGetTexEnvfv            = GPA("glGetTexEnvfv");
    qglGetTexEnviv              = dllGetTexEnviv            = GPA("glGetTexEnviv");
    qglGetTexParameterfv        = dllGetTexParameterfv      = GPA("glGetTexParameterfv");
    qglGetTexParameteriv        = dllGetTexParameteriv      = GPA("glGetTexParameteriv");
    qglHint                     = dllHint                   = GPA("glHint");
    qglIsEnabled                = dllIsEnabled              = GPA("glIsEnabled");
    qglIsTexture                = dllIsTexture              = GPA("glIsTexture");
    qglLightModelf              = dllLightModelf            = GPA("glLightModelf");
    qglLightModelfv             = dllLightModelfv           = GPA("glLightModelfv");
    qglLightf                   = dllLightf                 = GPA("glLightf");
    qglLightfv                  = dllLightfv                = GPA("glLightfv");
    qglLineWidth                = dllLineWidth              = GPA("glLineWidth");
    qglLoadIdentity             = dllLoadIdentity           = GPA("glLoadIdentity");
    qglLoadMatrixf              = dllLoadMatrixf            = GPA("glLoadMatrixf");
    qglLogicOp                  = dllLogicOp                = GPA("glLogicOp");
    qglMaterialf                = dllMaterialf              = GPA("glMaterialf");
    qglMaterialfv               = dllMaterialfv             = GPA("glMaterialfv");
    qglMatrixMode               = dllMatrixMode             = GPA("glMatrixMode");
    qglMultMatrixf              = dllMultMatrixf            = GPA("glMultMatrixf");
    qglNormal3f                 = dllNormal3f               = GPA("glNormal3f");
    qglNormal3fv                = dllNormal3fv              = GPA("glNormal3fv");
    qglNormalPointer            = dllNormalPointer          = GPA("glNormalPointer");
    qglOrtho                    = dllOrtho                  = GPA("glOrtho");
    qglPixelStorei              = dllPixelStorei            = GPA("glPixelStorei");
    qglPointSize                = dllPointSize              = GPA("glPointSize");
    qglPolygonMode              = dllPolygonMode            = GPA("glPolygonMode");
    qglPolygonOffset            = dllPolygonOffset          = GPA("glPolygonOffset");
    qglPopMatrix                = dllPopMatrix              = GPA("glPopMatrix");
    qglPushMatrix               = dllPushMatrix             = GPA("glPushMatrix");
    qglReadPixels               = dllReadPixels             = GPA("glReadPixels");
    qglRotatef                  = dllRotatef                = GPA("glRotatef");
    qglScalef                   = dllScalef                 = GPA("glScalef");
    qglScissor                  = dllScissor                = GPA("glScissor");
    qglShadeModel               = dllShadeModel             = GPA("glShadeModel");
    qglStencilFunc              = dllStencilFunc            = GPA("glStencilFunc");
    qglStencilMask              = dllStencilMask            = GPA("glStencilMask");
    qglStencilOp                = dllStencilOp              = GPA("glStencilOp");
    qglTexCoordPointer          = dllTexCoordPointer        = GPA("glTexCoordPointer");
    qglTexEnvf                  = dllTexEnvf                = GPA("glTexEnvf");
    qglTexEnvfv                 = dllTexEnvfv               = GPA("glTexEnvfv");
    qglTexEnvi                  = dllTexEnvi                = GPA("glTexEnvi");
    qglTexEnviv                 = dllTexEnviv               = GPA("glTexEnviv");
    qglTexImage2D               = dllTexImage2D             = GPA("glTexImage2D");
    qglTexParameterf            = dllTexParameterf          = GPA("glTexParameterf");
    qglTexParameterfv           = dllTexParameterfv         = GPA("glTexParameterfv");
    qglTexParameteri            = dllTexParameteri          = GPA("glTexParameteri");
    qglTexParameteriv           = dllTexParameteriv         = GPA("glTexParameteriv");
    qglTexSubImage2D            = dllTexSubImage2D          = GPA("glTexSubImage2D");
    qglTranslatef               = dllTranslatef             = GPA("glTranslatef");
    qglVertexPointer            = dllVertexPointer          = GPA("glVertexPointer");
    qglViewport                 = dllViewport               = GPA("glViewport");
}

#ifdef _WIN32
// hack, use ICD function for obtaining extensions
#undef GPA
#define GPA(a)  (void *)qwglGetProcAddress(a)
#endif

void QGL_InitExtensions(unsigned mask)
{
    if (mask & QGL_EXT_compiled_vertex_array) {
        qglLockArraysEXT                        = GPA("glLockArraysEXT");
        qglUnlockArraysEXT                      = GPA("glUnlockArraysEXT");
    }

    if (mask & QGL_ARB_multitexture) {
        qglActiveTextureARB                     = GPA("glActiveTextureARB");
        qglClientActiveTextureARB               = GPA("glClientActiveTextureARB");
    }

    if (mask & QGL_ARB_fragment_program) {
        qglProgramStringARB                     = GPA("glProgramStringARB");
        qglBindProgramARB                       = GPA("glBindProgramARB");
        qglDeleteProgramsARB                    = GPA("glDeleteProgramsARB");
        qglGenProgramsARB                       = GPA("glGenProgramsARB");
        qglProgramEnvParameter4fvARB            = GPA("glProgramEnvParameter4fvARB");
        qglProgramLocalParameter4fvARB          = GPA("glProgramLocalParameter4fvARB");
        qglGetProgramEnvParameterfvARB          = GPA("glGetProgramEnvParameterfvARB");
        qglGetProgramLocalParameterfvARB        = GPA("glGetProgramLocalParameterfvARB");
        qglGetProgramivARB                      = GPA("glGetProgramivARB");
        qglGetProgramStringARB                  = GPA("glGetProgramStringARB");
        qglIsProgramARB                         = GPA("glIsProgramARB");
    }

    if (mask & QGL_ARB_vertex_buffer_object) {
        qglBindBufferARB                        = GPA("glBindBufferARB");
        qglDeleteBuffersARB                     = GPA("glDeleteBuffersARB");
        qglGenBuffersARB                        = GPA("glGenBuffersARB");
        qglIsBufferARB                          = GPA("glIsBufferARB");
        qglBufferDataARB                        = GPA("glBufferDataARB");
        qglBufferSubDataARB                     = GPA("glBufferSubDataARB");
        qglGetBufferSubDataARB                  = GPA("glGetBufferSubDataARB");
        qglMapBufferARB                         = GPA("glMapBufferARB");
        qglUnmapBufferARB                       = GPA("glUnmapBufferARB");
        qglGetBufferParameterivARB              = GPA("glGetBufferParameterivARB");
        qglGetBufferPointervARB                 = GPA("glGetBufferPointervARB");
    }
}

#undef GPA

unsigned QGL_ParseExtensionString(const char *s)
{
    // must match defines in qgl_api.h!
    static const char *const extnames[] = {
        "GL_EXT_compiled_vertex_array",
        "GL_ARB_multitexture",
        "GL_EXT_texture_filter_anisotropic",
        "GL_ARB_fragment_program",
        "GL_ARB_vertex_buffer_object",
        NULL
    };

    return Com_ParseExtensionString(s, extnames);
}

void QGL_EnableLogging(qboolean enable)
{
    if (enable) {
        if (!log_fp) {
            extern char fs_gamedir[];
            struct tm *newtime;
            time_t aclock;
            char buffer[MAX_OSPATH];
            size_t len;

            time(&aclock);

            len = Q_snprintf(buffer, sizeof(buffer), "%s/qgl.log", fs_gamedir);
            if (len >= sizeof(buffer)) {
                return;
            }

            log_fp = fopen(buffer, "w");
            if (!log_fp) {
                return;
            }

            newtime = localtime(&aclock);
            fprintf(log_fp, "%s\n", asctime(newtime));
        }

        qglAlphaFunc                = logAlphaFunc;
        qglBindTexture              = logBindTexture;
        qglBlendFunc                = logBlendFunc;
        qglClear                    = logClear;
        qglClearColor               = logClearColor;
        qglClearDepth               = logClearDepth;
        qglClearIndex               = logClearIndex;
        qglClearStencil             = logClearStencil;
        qglColor4f                  = logColor4f;
        qglColor4fv                 = logColor4fv;
        qglColor4ub                 = logColor4ub;
        qglColor4ubv                = logColor4ubv;
        qglColorMask                = logColorMask;
        qglColorPointer             = logColorPointer;
        qglCopyTexImage2D           = logCopyTexImage2D;
        qglCopyTexSubImage2D        = logCopyTexSubImage2D;
        qglCullFace                 = logCullFace;
        qglDeleteTextures           = logDeleteTextures;
        qglDepthFunc                = logDepthFunc;
        qglDepthMask                = logDepthMask;
        qglDepthRange               = logDepthRange;
        qglDisable                  = logDisable;
        qglDisableClientState       = logDisableClientState;
        qglDrawArrays               = logDrawArrays;
        qglDrawElements             = logDrawElements;
        qglEnable                   = logEnable;
        qglEnableClientState        = logEnableClientState;
        qglFinish                   = logFinish;
        qglFlush                    = logFlush;
        qglFogf                     = logFogf;
        qglFogfv                    = logFogfv;
        qglFrontFace                = logFrontFace;
        qglFrustum                  = logFrustum;
        qglGenTextures              = logGenTextures;
        qglGetBooleanv              = logGetBooleanv;
        qglGetError                 = logGetError;
        qglGetFloatv                = logGetFloatv;
        qglGetIntegerv              = logGetIntegerv;
        qglGetLightfv               = logGetLightfv;
        qglGetMaterialfv            = logGetMaterialfv;
        qglGetPointerv              = logGetPointerv;
        qglGetString                = logGetString;
        qglGetTexEnvfv              = logGetTexEnvfv;
        qglGetTexEnviv              = logGetTexEnviv;
        qglGetTexParameterfv        = logGetTexParameterfv;
        qglGetTexParameteriv        = logGetTexParameteriv;
        qglHint                     = logHint;
        qglIsEnabled                = logIsEnabled;
        qglIsTexture                = logIsTexture;
        qglLightModelf              = logLightModelf;
        qglLightModelfv             = logLightModelfv;
        qglLightf                   = logLightf;
        qglLightfv                  = logLightfv;
        qglLineWidth                = logLineWidth;
        qglLoadIdentity             = logLoadIdentity;
        qglLoadMatrixf              = logLoadMatrixf;
        qglLogicOp                  = logLogicOp;
        qglMaterialf                = logMaterialf;
        qglMaterialfv               = logMaterialfv;
        qglMatrixMode               = logMatrixMode;
        qglMultMatrixf              = logMultMatrixf;
        qglNormal3f                 = logNormal3f;
        qglNormal3fv                = logNormal3fv;
        qglNormalPointer            = logNormalPointer;
        qglOrtho                    = logOrtho;
        qglPixelStorei              = logPixelStorei;
        qglPointSize                = logPointSize;
        qglPolygonMode              = logPolygonMode;
        qglPolygonOffset            = logPolygonOffset;
        qglPopMatrix                = logPopMatrix;
        qglPushMatrix               = logPushMatrix;
        qglReadPixels               = logReadPixels;
        qglRotatef                  = logRotatef;
        qglScalef                   = logScalef;
        qglScissor                  = logScissor;
        qglShadeModel               = logShadeModel;
        qglStencilFunc              = logStencilFunc;
        qglStencilMask              = logStencilMask;
        qglStencilOp                = logStencilOp;
        qglTexCoordPointer          = logTexCoordPointer;
        qglTexEnvf                  = logTexEnvf;
        qglTexEnvfv                 = logTexEnvfv;
        qglTexEnvi                  = logTexEnvi;
        qglTexEnviv                 = logTexEnviv;
        qglTexImage2D               = logTexImage2D;
        qglTexParameterf            = logTexParameterf;
        qglTexParameterfv           = logTexParameterfv;
        qglTexParameteri            = logTexParameteri;
        qglTexParameteriv           = logTexParameteriv;
        qglTexSubImage2D            = logTexSubImage2D;
        qglTranslatef               = logTranslatef;
        qglVertexPointer            = logVertexPointer;
        qglViewport                 = logViewport;
    } else {
        if (log_fp) {
            fclose(log_fp);
            log_fp = NULL;
        }

        qglAlphaFunc                = dllAlphaFunc;
        qglBindTexture              = dllBindTexture;
        qglBlendFunc                = dllBlendFunc;
        qglClear                    = dllClear;
        qglClearColor               = dllClearColor;
        qglClearDepth               = dllClearDepth;
        qglClearIndex               = dllClearIndex;
        qglClearStencil             = dllClearStencil;
        qglColor4f                  = dllColor4f;
        qglColor4fv                 = dllColor4fv;
        qglColor4ub                 = dllColor4ub;
        qglColor4ubv                = dllColor4ubv;
        qglColorMask                = dllColorMask;
        qglColorPointer             = dllColorPointer;
        qglCopyTexImage2D           = dllCopyTexImage2D;
        qglCopyTexSubImage2D        = dllCopyTexSubImage2D;
        qglCullFace                 = dllCullFace;
        qglDeleteTextures           = dllDeleteTextures;
        qglDepthFunc                = dllDepthFunc;
        qglDepthMask                = dllDepthMask;
        qglDepthRange               = dllDepthRange;
        qglDisable                  = dllDisable;
        qglDisableClientState       = dllDisableClientState;
        qglDrawArrays               = dllDrawArrays;
        qglDrawElements             = dllDrawElements;
        qglEnable                   = dllEnable;
        qglEnableClientState        = dllEnableClientState;
        qglFinish                   = dllFinish;
        qglFlush                    = dllFlush;
        qglFogf                     = dllFogf;
        qglFogfv                    = dllFogfv;
        qglFrontFace                = dllFrontFace;
        qglFrustum                  = dllFrustum;
        qglGenTextures              = dllGenTextures;
        qglGetBooleanv              = dllGetBooleanv;
        qglGetError                 = dllGetError;
        qglGetFloatv                = dllGetFloatv;
        qglGetIntegerv              = dllGetIntegerv;
        qglGetLightfv               = dllGetLightfv;
        qglGetMaterialfv            = dllGetMaterialfv;
        qglGetPointerv              = dllGetPointerv;
        qglGetString                = dllGetString;
        qglGetTexEnvfv              = dllGetTexEnvfv;
        qglGetTexEnviv              = dllGetTexEnviv;
        qglGetTexParameterfv        = dllGetTexParameterfv;
        qglGetTexParameteriv        = dllGetTexParameteriv;
        qglHint                     = dllHint;
        qglIsEnabled                = dllIsEnabled;
        qglIsTexture                = dllIsTexture;
        qglLightModelf              = dllLightModelf;
        qglLightModelfv             = dllLightModelfv;
        qglLightf                   = dllLightf;
        qglLightfv                  = dllLightfv;
        qglLineWidth                = dllLineWidth;
        qglLoadIdentity             = dllLoadIdentity;
        qglLoadMatrixf              = dllLoadMatrixf;
        qglLogicOp                  = dllLogicOp;
        qglMaterialf                = dllMaterialf;
        qglMaterialfv               = dllMaterialfv;
        qglMatrixMode               = dllMatrixMode;
        qglMultMatrixf              = dllMultMatrixf;
        qglNormal3f                 = dllNormal3f;
        qglNormal3fv                = dllNormal3fv;
        qglNormalPointer            = dllNormalPointer;
        qglOrtho                    = dllOrtho;
        qglPixelStorei              = dllPixelStorei;
        qglPointSize                = dllPointSize;
        qglPolygonMode              = dllPolygonMode;
        qglPolygonOffset            = dllPolygonOffset;
        qglPopMatrix                = dllPopMatrix;
        qglPushMatrix               = dllPushMatrix;
        qglReadPixels               = dllReadPixels;
        qglRotatef                  = dllRotatef;
        qglScalef                   = dllScalef;
        qglScissor                  = dllScissor;
        qglShadeModel               = dllShadeModel;
        qglStencilFunc              = dllStencilFunc;
        qglStencilMask              = dllStencilMask;
        qglStencilOp                = dllStencilOp;
        qglTexCoordPointer          = dllTexCoordPointer;
        qglTexEnvf                  = dllTexEnvf;
        qglTexEnvfv                 = dllTexEnvfv;
        qglTexEnvi                  = dllTexEnvi;
        qglTexEnviv                 = dllTexEnviv;
        qglTexImage2D               = dllTexImage2D;
        qglTexParameterf            = dllTexParameterf;
        qglTexParameterfv           = dllTexParameterfv;
        qglTexParameteri            = dllTexParameteri;
        qglTexParameteriv           = dllTexParameteriv;
        qglTexSubImage2D            = dllTexSubImage2D;
        qglTranslatef               = dllTranslatef;
        qglVertexPointer            = dllVertexPointer;
        qglViewport                 = dllViewport;
    }
}

void QGL_LogNewFrame(void)
{
    if (log_fp) {
        fprintf(log_fp, "\n*** NewFrame ***\n");
    }
}

