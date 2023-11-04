/*
Copyright (C) 2018 Andrey Nazarov

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
#else
#ifdef _WIN32
 #define WIN32_LEAN_AND_MEAN 1
 #include <windows.h>
#endif
 #include <GL/gl.h>
 #include <GL/glext.h>
#endif

#ifndef QGLAPI
#define QGLAPI extern
#endif

// GL 1.1
QGLAPI void (APIENTRYP qglBindTexture)(GLenum target, GLuint texture);
QGLAPI void (APIENTRYP qglBlendFunc)(GLenum sfactor, GLenum dfactor);
QGLAPI void (APIENTRYP qglClearColor)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
QGLAPI void (APIENTRYP qglClear)(GLbitfield mask);
QGLAPI void (APIENTRYP qglClearStencil)(GLint s);
QGLAPI void (APIENTRYP qglCullFace)(GLenum mode);
QGLAPI void (APIENTRYP qglDeleteTextures)(GLsizei n, const GLuint *textures);
QGLAPI void (APIENTRYP qglDepthFunc)(GLenum func);
QGLAPI void (APIENTRYP qglDepthMask)(GLboolean flag);
QGLAPI void (APIENTRYP qglDisable)(GLenum cap);
QGLAPI void (APIENTRYP qglDrawArrays)(GLenum mode, GLint first, GLsizei count);
QGLAPI void (APIENTRYP qglDrawElements)(GLenum mode, GLsizei count, GLenum type, const void *indices);
QGLAPI void (APIENTRYP qglEnable)(GLenum cap);
QGLAPI void (APIENTRYP qglFinish)(void);
QGLAPI void (APIENTRYP qglFrontFace)(GLenum mode);
QGLAPI void (APIENTRYP qglGenTextures)(GLsizei n, GLuint *textures);
QGLAPI GLenum (APIENTRYP qglGetError)(void);
QGLAPI void (APIENTRYP qglGetFloatv)(GLenum pname, GLfloat *data);
QGLAPI void (APIENTRYP qglGetIntegerv)(GLenum pname, GLint *data);
QGLAPI const GLubyte *(APIENTRYP qglGetString)(GLenum name);
QGLAPI GLboolean (APIENTRYP qglIsEnabled)(GLenum cap);
QGLAPI void (APIENTRYP qglLineWidth)(GLfloat width);
QGLAPI void (APIENTRYP qglPolygonOffset)(GLfloat factor, GLfloat units);
QGLAPI void (APIENTRYP qglReadPixels)(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels);
QGLAPI void (APIENTRYP qglScissor)(GLint x, GLint y, GLsizei width, GLsizei height);
QGLAPI void (APIENTRYP qglStencilFunc)(GLenum func, GLint ref, GLuint mask);
QGLAPI void (APIENTRYP qglStencilOp)(GLenum fail, GLenum zfail, GLenum zpass);
QGLAPI void (APIENTRYP qglTexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
QGLAPI void (APIENTRYP qglTexParameterf)(GLenum target, GLenum pname, GLfloat param);
QGLAPI void (APIENTRYP qglTexParameteri)(GLenum target, GLenum pname, GLint param);
QGLAPI void (APIENTRYP qglTexSubImage2D)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels);
QGLAPI void (APIENTRYP qglViewport)(GLint x, GLint y, GLsizei width, GLsizei height);

// GL 1.1, compat
QGLAPI void (APIENTRYP qglAlphaFunc)(GLenum func, GLclampf ref);
QGLAPI void (APIENTRYP qglColor4f)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
QGLAPI void (APIENTRYP qglColorPointer)(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
QGLAPI void (APIENTRYP qglDisableClientState)(GLenum cap);
QGLAPI void (APIENTRYP qglEnableClientState)(GLenum cap);
QGLAPI void (APIENTRYP qglLoadIdentity)(void);
QGLAPI void (APIENTRYP qglLoadMatrixf)(const GLfloat *m);
QGLAPI void (APIENTRYP qglMatrixMode)(GLenum mode);
QGLAPI void (APIENTRYP qglScalef)(GLfloat x, GLfloat y, GLfloat z);
QGLAPI void (APIENTRYP qglShadeModel)(GLenum mode);
QGLAPI void (APIENTRYP qglTexCoordPointer)(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
QGLAPI void (APIENTRYP qglTexEnvf)(GLenum target, GLenum pname, GLfloat param);
QGLAPI void (APIENTRYP qglTranslatef)(GLfloat x, GLfloat y, GLfloat z);
QGLAPI void (APIENTRYP qglVertexPointer)(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);

// GL 1.1, not ES
QGLAPI void (APIENTRYP qglClearDepth)(GLdouble depth);
QGLAPI void (APIENTRYP qglDepthRange)(GLdouble near, GLdouble far);

// GL 1.1, not ES, compat
QGLAPI void (APIENTRYP qglPolygonMode)(GLenum face, GLenum mode);

// GL 1.3
QGLAPI void (APIENTRYP qglActiveTexture)(GLenum texture);

// GL 1.3, compat
QGLAPI void (APIENTRYP qglClientActiveTexture)(GLenum texture);

// GL 1.5
QGLAPI void (APIENTRYP qglBindBuffer)(GLenum target, GLuint buffer);
QGLAPI void (APIENTRYP qglBufferData)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
QGLAPI void (APIENTRYP qglBufferSubData)(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
QGLAPI void (APIENTRYP qglDeleteBuffers)(GLsizei n, const GLuint *buffers);
QGLAPI void (APIENTRYP qglGenBuffers)(GLsizei n, GLuint *buffers);

// GL 2.0
QGLAPI void (APIENTRYP qglAttachShader)(GLuint program, GLuint shader);
QGLAPI void (APIENTRYP qglBindAttribLocation)(GLuint program, GLuint index, const GLchar *name);
QGLAPI void (APIENTRYP qglCompileShader)(GLuint shader);
QGLAPI GLuint (APIENTRYP qglCreateProgram)(void);
QGLAPI GLuint (APIENTRYP qglCreateShader)(GLenum type);
QGLAPI void (APIENTRYP qglDeleteProgram)(GLuint program);
QGLAPI void (APIENTRYP qglDeleteShader)(GLuint shader);
QGLAPI void (APIENTRYP qglDisableVertexAttribArray)(GLuint index);
QGLAPI void (APIENTRYP qglEnableVertexAttribArray)(GLuint index);
QGLAPI void (APIENTRYP qglGetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
QGLAPI void (APIENTRYP qglGetProgramiv)(GLuint program, GLenum pname, GLint *params);
QGLAPI void (APIENTRYP qglGetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
QGLAPI void (APIENTRYP qglGetShaderiv)(GLuint shader, GLenum pname, GLint *params);
QGLAPI GLint (APIENTRYP qglGetUniformLocation)(GLuint program, const GLchar *name);
QGLAPI void (APIENTRYP qglLinkProgram)(GLuint program);
QGLAPI void (APIENTRYP qglShaderSource)(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
QGLAPI void (APIENTRYP qglUniform1i)(GLint location, GLint v0);
QGLAPI void (APIENTRYP qglUseProgram)(GLuint program);
QGLAPI void (APIENTRYP qglVertexAttrib4f)(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
QGLAPI void (APIENTRYP qglVertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);

// GL 3.0
QGLAPI void (APIENTRYP qglGenerateMipmap)(GLenum target);
QGLAPI const GLubyte *(APIENTRYP qglGetStringi)(GLenum name, GLuint index);

// GL 3.1
QGLAPI void (APIENTRYP qglBindBufferBase)(GLenum target, GLuint index, GLuint buffer);
QGLAPI GLuint (APIENTRYP qglGetUniformBlockIndex)(GLuint program, const GLchar *uniformBlockName);
QGLAPI void (APIENTRYP qglGetActiveUniformBlockiv)(GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint *params);
QGLAPI void (APIENTRYP qglUniformBlockBinding)(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding);

// GL 4.1
QGLAPI void (APIENTRYP qglClearDepthf)(GLfloat d);
QGLAPI void (APIENTRYP qglDepthRangef)(GLfloat n, GLfloat f);

// GL 4.3
QGLAPI void (APIENTRYP qglDebugMessageCallback)(GLDEBUGPROC callback, const void *userParam);

// GL_ARB_fragment_program
QGLAPI void (APIENTRYP qglBindProgramARB)(GLenum target, GLuint program);
QGLAPI void (APIENTRYP qglDeleteProgramsARB)(GLsizei n, const GLuint *programs);
QGLAPI void (APIENTRYP qglGenProgramsARB)(GLsizei n, GLuint *programs);
QGLAPI void (APIENTRYP qglProgramLocalParameter4fvARB)(GLenum target, GLuint index, const GLfloat *params);
QGLAPI void (APIENTRYP qglProgramStringARB)(GLenum target, GLenum format, GLsizei len, const void *string);

// GL_EXT_compiled_vertex_array
QGLAPI void (APIENTRYP qglLockArraysEXT)(GLint first, GLsizei count);
QGLAPI void (APIENTRYP qglUnlockArraysEXT)(void);

bool QGL_Init(void);
void QGL_Shutdown(void);

#endif  // QGL_H
