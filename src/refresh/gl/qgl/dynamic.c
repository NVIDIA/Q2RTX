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
#include "dynamic.h"

// ==========================================================

#define QGL(x)  qgl##x##_t qgl##x
QGL_core_IMP
QGL_ARB_fragment_program_IMP
QGL_ARB_multitexture_IMP
QGL_ARB_vertex_buffer_object_IMP
QGL_EXT_compiled_vertex_array_IMP
#undef QGL

qglGenerateMipmap_t qglGenerateMipmap;

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

static const char *enumToString(GLenum num)
{
#define E(x) case x: return #x;
    switch (num) {
/* Data types */
        E(GL_UNSIGNED_BYTE)
        E(GL_UNSIGNED_SHORT)
        E(GL_UNSIGNED_INT)
        E(GL_FLOAT)

/* Primitives */
        E(GL_LINES)
        E(GL_LINE_STRIP)
        E(GL_TRIANGLES)
        E(GL_TRIANGLE_STRIP)

/* Vertex Arrays */
        E(GL_VERTEX_ARRAY)
        E(GL_COLOR_ARRAY)
        E(GL_TEXTURE_COORD_ARRAY)

/* Matrix Mode */
        E(GL_MODELVIEW)
        E(GL_PROJECTION)
        E(GL_TEXTURE)

/* Polygons */
        E(GL_LINE)
        E(GL_FILL)
        E(GL_CW)
        E(GL_CCW)
        E(GL_FRONT)
        E(GL_BACK)
        E(GL_CULL_FACE)
        E(GL_POLYGON_OFFSET_FILL)

/* Depth buffer */
        E(GL_LEQUAL)
        E(GL_GREATER)
        E(GL_DEPTH_TEST)

/* Lighting */
        E(GL_FRONT_AND_BACK)
        E(GL_FLAT)
        E(GL_SMOOTH)

/* Alpha testing */
        E(GL_ALPHA_TEST)

/* Blending */
        E(GL_BLEND)
        E(GL_SRC_ALPHA)
        E(GL_ONE_MINUS_SRC_ALPHA)
        E(GL_DST_COLOR)

/* Stencil */
        E(GL_STENCIL_TEST)
        E(GL_REPLACE)

/* Scissor box */
        E(GL_SCISSOR_TEST)

/* Texture mapping */
        E(GL_TEXTURE_ENV)
        E(GL_TEXTURE_ENV_MODE)
        E(GL_TEXTURE_2D)
        E(GL_TEXTURE_WRAP_S)
        E(GL_TEXTURE_WRAP_T)
        E(GL_TEXTURE_MAG_FILTER)
        E(GL_TEXTURE_MIN_FILTER)
        E(GL_MODULATE)

/* GL_ARB_fragment_program */
        E(GL_FRAGMENT_PROGRAM_ARB)

/* GL_EXT_texture_filter_anisotropic */
        E(GL_TEXTURE_MAX_ANISOTROPY_EXT)
    }
#undef E
    return va("%#x", num);
}

static void APIENTRY logAlphaFunc(GLenum func, GLclampf ref)
{
    SIGf("%s( %s, %f )\n", "glAlphaFunc", enumToString(func), ref);
    dllAlphaFunc(func, ref);
}

static void APIENTRY logBindTexture(GLenum target, GLuint texture)
{
    SIGf("%s( %s, %u )\n", "glBindTexture", enumToString(target), texture);
    dllBindTexture(target, texture);
}

static void APIENTRY logBlendFunc(GLenum sfactor, GLenum dfactor)
{
    SIGf("%s( %s, %s )\n", "glBlendFunc", enumToString(sfactor), enumToString(dfactor));
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
    SIGf("%s( %d, %s, %d, %p )\n", "glColorPointer", size, enumToString(type), stride, pointer);
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
    SIGf("%s( %s )\n", "glCullFace", enumToString(mode));
    dllCullFace(mode);
}

static void APIENTRY logDeleteTextures(GLsizei n, const GLuint *textures)
{
    SIGf("%s( %d, %p )\n", "glDeleteTextures", n, textures);
    dllDeleteTextures(n, textures);
}

static void APIENTRY logDepthFunc(GLenum func)
{
    SIGf("%s( %s )\n", "glDepthFunc", enumToString(func));
    dllDepthFunc(func);
}

static void APIENTRY logDepthMask(GLboolean flag)
{
    SIGf("%s( %s )\n", "glDepthMask", flag ? "GL_TRUE" : "GL_FALSE");
    dllDepthMask(flag);
}

static void APIENTRY logDepthRange(GLclampd zNear, GLclampd zFar)
{
    SIGf("%s( %f, %f )\n", "glDepthRange", zNear, zFar);
    dllDepthRange(zNear, zFar);
}

static void APIENTRY logDisable(GLenum cap)
{
    SIGf("%s( %s )\n", "glDisable", enumToString(cap));
    dllDisable(cap);
}

static void APIENTRY logDisableClientState(GLenum array)
{
    SIGf("%s( %s )\n", "glDisableClientState", enumToString(array));
    dllDisableClientState(array);
}

static void APIENTRY logDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    SIGf("%s( %s, %u, %u )\n", "glDrawArrays", enumToString(mode), first, count);
    dllDrawArrays(mode, first, count);
}

static void APIENTRY logDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    SIGf("%s( %s, %u, %s, %p )\n", "glDrawElements", enumToString(mode), count, enumToString(type), indices);
    dllDrawElements(mode, count, type, indices);
}

static void APIENTRY logEnable(GLenum cap)
{
    SIGf("%s( %s )\n", "glEnable", enumToString(cap));
    dllEnable(cap);
}

static void APIENTRY logEnableClientState(GLenum array)
{
    SIGf("%s( %s )\n", "glEnableClientState", enumToString(array));
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
    SIGf("%s( %s )\n", "glFrontFace", enumToString(mode));
    dllFrontFace(mode);
}

static void APIENTRY logFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    SIG("glFrustum");
    dllFrustum(left, right, bottom, top, zNear, zFar);
}

static void APIENTRY logGenTextures(GLsizei n, GLuint *textures)
{
    SIGf("%s( %d, %p )\n", "glGenTextures", n, textures);
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
    SIGf("glLoadMatrixf( %p )\n", m);
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
    SIGf("%s( %s )\n", "glMatrixMode", enumToString(mode));
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
    SIGf("%s( %s, %s )\n", "glPolygonMode", enumToString(face), enumToString(mode));
    dllPolygonMode(face, mode);
}

static void APIENTRY logPolygonOffset(GLfloat factor, GLfloat units)
{
    SIGf("%s( %f, %f )\n", "glPolygonOffset", factor, units);
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
    SIGf("%s( %d, %d, %d, %d )\n", "glScissor", x, y, width, height);
    dllScissor(x, y, width, height);
}

static void APIENTRY logShadeModel(GLenum mode)
{
    SIGf("%s( %s )\n", "glShadeModel", enumToString(mode));
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
    SIGf("%s( %d, %s, %d, %p )\n", "glTexCoordPointer", size, enumToString(type), stride, pointer);
    dllTexCoordPointer(size, type, stride, pointer);
}

static void APIENTRY logTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    SIGf("%s( %s, %s, %s )\n", "glTexEnvf", enumToString(target), enumToString(pname), enumToString(param));
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
    SIGf("%s( %s, %s, %f )\n", "glTexParameterf", enumToString(target), enumToString(pname), param);
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
    SIGf("%s( %d, %s, %d, %p )\n", "glVertexPointer", size, enumToString(type), stride, pointer);
    dllVertexPointer(size, type, stride, pointer);
}

static void APIENTRY logViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    SIGf("%s( %d, %d, %d, %d )\n", "glViewport", x, y, width, height);
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

    QGL_ShutdownExtensions(~0);
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

    if (mask & QGL_3_0_core_functions) {
        qglGenerateMipmap = NULL;
    }
#undef QGL
}

#define GCA(x)  VID_GetCoreAddr("gl"#x)
#define GPA(x)  VID_GetProcAddr("gl"#x)

qboolean QGL_Init(void)
{
#ifdef _DEBUG
#define QGL(x)  if ((qgl##x = dll##x = GCA(x)) == NULL) return qfalse;
#else
#define QGL(x)  if ((qgl##x = GCA(x)) == NULL)          return qfalse;
#endif
    QGL_core_IMP
#undef QGL

    return qtrue;
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

    if (mask & QGL_3_0_core_functions) {
        qglGenerateMipmap = GPA(GenerateMipmap);
    }
#undef QGL
}

unsigned QGL_ParseExtensionString(const char *s)
{
    // must match defines in dynamic.h!
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

