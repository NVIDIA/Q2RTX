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

#include "shared/shared.h"
#include "common/common.h"
#include "client/video.h"
#include "qgl.h"

// ==========================================================

#define QGL(x)  qgl##x##_t qgl##x
QGL_core_IMP
QGL_ARB_fragment_program_IMP
QGL_ARB_multitexture_IMP
QGL_ARB_vertex_buffer_object_IMP
QGL_EXT_compiled_vertex_array_IMP
#undef QGL

// ==========================================================

#ifdef _DEBUG

static FILE *log_fp;

#define QGL(x)  static qgl##x##_t dll##x
QGL_core_IMP
QGL_ARB_fragment_program_IMP
QGL_ARB_multitexture_IMP
QGL_ARB_vertex_buffer_object_IMP
QGL_EXT_compiled_vertex_array_IMP
#undef QGL

#define SIG(x) fprintf(log_fp, "%s\n", x)
#define SIGf(...) fprintf(log_fp, __VA_ARGS__)

static void APIENTRY logAlphaFunc(GLenum func, GLclampf ref)
{
    SIGf("%s( %#x, %f )\n", "glAlphaFunc", func, ref);
    dllAlphaFunc(func, ref);
}

static void APIENTRY logBindTexture(GLenum target, GLuint texture)
{
    SIGf("%s( %#x, %u )\n", "glBindTexture", target, texture);
    dllBindTexture(target, texture);
}

static void APIENTRY logBlendFunc(GLenum sfactor, GLenum dfactor)
{
    SIGf("%s( %#x, %#x )\n", "glBlendFunc", sfactor, dfactor);
    dllBlendFunc(sfactor, dfactor);
}

static void APIENTRY logClear(GLbitfield mask)
{
    SIGf("%s( %#x )\n", "glClear", mask);
    dllClear(mask);
}

static void APIENTRY logClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    SIG("glClearColor");
    dllClearColor(red, green, blue, alpha);
}

static void APIENTRY logClearDepth(GLclampd depth)
{
    SIG("glClearDepth");
    dllClearDepth(depth);
}

static void APIENTRY logClearStencil(GLint s)
{
    SIG("glClearStencil");
    dllClearStencil(s);
}

static void APIENTRY logColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    SIGf("%s( %f, %f, %f, %f )\n", "glColor4f", red, green, blue, alpha);
    dllColor4f(red, green, blue, alpha);
}

static void APIENTRY logColor4fv(const GLfloat *v)
{
    SIGf("%s( %f, %f, %f, %f )\n", "glColor4fv", v[0], v[1], v[2], v[3]);
    dllColor4fv(v);
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
    SIGf("%s( %#x )\n", "glDisable", cap);
    dllDisable(cap);
}

static void APIENTRY logDisableClientState(GLenum array)
{
    SIGf("%s( %#x )\n", "glDisableClientState", array);
    dllDisableClientState(array);
}

static void APIENTRY logDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    SIGf("%s( %#x, %u, %u )\n", "glDrawArrays", mode, first, count);
    dllDrawArrays(mode, first, count);
}

static void APIENTRY logDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    SIGf("%s( %#x, %u, %#x, %p )\n", "glDrawElements", mode, count, type, indices);
    dllDrawElements(mode, count, type, indices);
}

static void APIENTRY logEnable(GLenum cap)
{
    SIGf("%s( %#x )\n", "glEnable", cap);
    dllEnable(cap);
}

static void APIENTRY logEnableClientState(GLenum array)
{
    SIGf("%s( %#x )\n", "glEnableClientState", array);
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

static const GLubyte * APIENTRY logGetString(GLenum name)
{
    SIG("glGetString");
    return dllGetString(name);
}

static void APIENTRY logHint(GLenum target, GLenum mode)
{
    SIGf("%s( %#x, %#x )\n", "glHint", target, mode);
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
    SIGf("%s( %#x, %#x )\n", "glPolygonMode", face, mode);
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
    SIGf("%s( %#x, %#x, %f )\n", "glTexEnvf", target, pname, param);
    dllTexEnvf(target, pname, param);
}

static void APIENTRY logTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    SIG("glTexEnvfv");
    dllTexEnvfv(target, pname, params);
}

static void APIENTRY logTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels)
{
    SIG("glTexImage2D");
    dllTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
}

static void APIENTRY logTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    SIGf("%s( %#x, %#x, %f )\n", "glTexParameterf", target, pname, param);
    dllTexParameterf(target, pname, param);
}

static void APIENTRY logTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
    SIG("glTexParameterfv");
    dllTexParameterfv(target, pname, params);
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

static void APIENTRY logActiveTextureARB(GLenum texture)
{
    SIGf("%s( GL_TEXTURE%d )\n", "glActiveTextureARB", texture - GL_TEXTURE0);
    dllActiveTextureARB(texture);
}

static void APIENTRY logClientActiveTextureARB(GLenum texture)
{
    SIGf("%s( GL_TEXTURE%d )\n", "glClientActiveTextureARB", texture - GL_TEXTURE0);
    dllClientActiveTextureARB(texture);
}

static void APIENTRY logLockArraysEXT(GLint first, GLsizei count)
{
    SIGf("%s( %u, %u )\n", "glLockArraysEXT", first, count);
    dllLockArraysEXT(first, count);
}

static void APIENTRY logUnlockArraysEXT(void)
{
    SIG("glUnlockArraysEXT");
    dllUnlockArraysEXT();
}

#endif // _DEBUG

// ==========================================================

void QGL_Shutdown(void)
{
#ifdef _DEBUG
#define QGL(x)  qgl##x = dll##x = NULL
#else
#define QGL(x)  qgl##x = NULL
#endif
    QGL_core_IMP
    QGL_ARB_fragment_program_IMP
    QGL_ARB_multitexture_IMP
    QGL_ARB_vertex_buffer_object_IMP
    QGL_EXT_compiled_vertex_array_IMP
}

void QGL_ShutdownExtensions(unsigned mask)
{
    if (mask & QGL_ARB_fragment_program) {
        QGL_ARB_fragment_program_IMP
    }

    if (mask & QGL_ARB_multitexture) {
        QGL_ARB_multitexture_IMP
    }

    if (mask & QGL_ARB_vertex_buffer_object) {
        QGL_ARB_vertex_buffer_object_IMP
    }

    if (mask & QGL_EXT_compiled_vertex_array) {
        QGL_EXT_compiled_vertex_array_IMP
    }
#undef QGL
}

#define GCA(x)  VID_GetCoreAddr("gl"#x)
#define GPA(x)  VID_GetProcAddr("gl"#x)

void QGL_Init(void)
{
#ifdef _DEBUG
#define QGL(x)  qgl##x = dll##x = GCA(x)
#else
#define QGL(x)  qgl##x = GCA(x)
#endif
    QGL_core_IMP
#undef QGL
}

void QGL_InitExtensions(unsigned mask)
{
#ifdef _DEBUG
#define QGL(x)  qgl##x = dll##x = GPA(x)
#else
#define QGL(x)  qgl##x = GPA(x)
#endif
    if (mask & QGL_ARB_fragment_program) {
        QGL_ARB_fragment_program_IMP
    }

    if (mask & QGL_ARB_multitexture) {
        QGL_ARB_multitexture_IMP
    }

    if (mask & QGL_ARB_vertex_buffer_object) {
        QGL_ARB_vertex_buffer_object_IMP
    }

    if (mask & QGL_EXT_compiled_vertex_array) {
        QGL_EXT_compiled_vertex_array_IMP
    }
#undef QGL
}

unsigned QGL_ParseExtensionString(const char *s)
{
    // must match defines in qgl_api.h!
    static const char *const extnames[] = {
        "GL_ARB_fragment_program",
        "GL_ARB_multitexture",
        "GL_ARB_vertex_buffer_object",
        "GL_EXT_compiled_vertex_array",
        "GL_EXT_texture_filter_anisotropic",
        NULL
    };

    return Com_ParseExtensionString(s, extnames);
}

#ifdef _DEBUG

void QGL_EnableLogging(unsigned mask)
{
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

#define QGL(x)  qgl##x = log##x
    QGL_core_IMP

    if (mask & QGL_ARB_fragment_program) {
    }

    if (mask & QGL_ARB_multitexture) {
        QGL_ARB_multitexture_IMP
    }

    if (mask & QGL_ARB_vertex_buffer_object) {
    }

    if (mask & QGL_EXT_compiled_vertex_array) {
        QGL_EXT_compiled_vertex_array_IMP
    }
#undef QGL
}

void QGL_DisableLogging(unsigned mask)
{
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }

#define QGL(x)  qgl##x = dll##x
    QGL_core_IMP

    if (mask & QGL_ARB_fragment_program) {
        QGL_ARB_fragment_program_IMP
    }

    if (mask & QGL_ARB_multitexture) {
        QGL_ARB_multitexture_IMP
    }

    if (mask & QGL_ARB_vertex_buffer_object) {
        QGL_ARB_vertex_buffer_object_IMP
    }

    if (mask & QGL_EXT_compiled_vertex_array) {
        QGL_EXT_compiled_vertex_array_IMP
    }
#undef QGL
}

void QGL_LogComment(const char *fmt, ...)
{
    va_list ap;

    if (log_fp) {
        va_start(ap, fmt);
        vfprintf(log_fp, fmt, ap);
        va_end(ap);
    }
}

#endif // _DEBUG

