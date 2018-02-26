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
#ifndef APIENTRY
#define APIENTRY
#endif
#endif	// !USE_SDL

// subset of OpenGL 1.1 core functions
#define QGL_core_IMP \
    QGL(AlphaFunc); \
    QGL(BindTexture); \
    QGL(BlendFunc); \
    QGL(Clear); \
    QGL(ClearColor); \
    QGL(ClearDepth); \
    QGL(ClearStencil); \
    QGL(Color4f); \
    QGL(Color4fv); \
    QGL(ColorMask); \
    QGL(ColorPointer); \
    QGL(CopyTexImage2D); \
    QGL(CopyTexSubImage2D); \
    QGL(CullFace); \
    QGL(DeleteTextures); \
    QGL(DepthFunc); \
    QGL(DepthMask); \
    QGL(DepthRange); \
    QGL(Disable); \
    QGL(DisableClientState); \
    QGL(DrawArrays); \
    QGL(DrawElements); \
    QGL(Enable); \
    QGL(EnableClientState); \
    QGL(Finish); \
    QGL(Flush); \
    QGL(Fogf); \
    QGL(Fogfv); \
    QGL(FrontFace); \
    QGL(Frustum); \
    QGL(GenTextures); \
    QGL(GetError); \
    QGL(GetFloatv); \
    QGL(GetIntegerv); \
    QGL(GetString); \
    QGL(Hint); \
    QGL(IsEnabled); \
    QGL(IsTexture); \
    QGL(LightModelf); \
    QGL(LightModelfv); \
    QGL(Lightf); \
    QGL(Lightfv); \
    QGL(LineWidth); \
    QGL(LoadIdentity); \
    QGL(LoadMatrixf); \
    QGL(LogicOp); \
    QGL(Materialf); \
    QGL(Materialfv); \
    QGL(MatrixMode); \
    QGL(MultMatrixf); \
    QGL(Normal3f); \
    QGL(Normal3fv); \
    QGL(NormalPointer); \
    QGL(Ortho); \
    QGL(PixelStorei); \
    QGL(PointSize); \
    QGL(PolygonMode); \
    QGL(PolygonOffset); \
    QGL(PopMatrix); \
    QGL(PushMatrix); \
    QGL(ReadPixels); \
    QGL(Rotatef); \
    QGL(Scalef); \
    QGL(Scissor); \
    QGL(ShadeModel); \
    QGL(StencilFunc); \
    QGL(StencilMask); \
    QGL(StencilOp); \
    QGL(TexCoordPointer); \
    QGL(TexEnvf); \
    QGL(TexEnvfv); \
    QGL(TexImage2D); \
    QGL(TexParameterf); \
    QGL(TexParameterfv); \
    QGL(TexSubImage2D); \
    QGL(Translatef); \
    QGL(VertexPointer); \
    QGL(Viewport);

// GL_ARB_fragment_program
#define QGL_ARB_fragment_program_IMP \
    QGL(ProgramStringARB); \
    QGL(BindProgramARB); \
    QGL(DeleteProgramsARB); \
    QGL(GenProgramsARB); \
    QGL(ProgramEnvParameter4fvARB); \
    QGL(ProgramLocalParameter4fvARB); \
    QGL(GetProgramEnvParameterfvARB); \
    QGL(GetProgramLocalParameterfvARB); \
    QGL(GetProgramivARB); \
    QGL(GetProgramStringARB); \
    QGL(IsProgramARB);

// GL_ARB_multitexture
#define QGL_ARB_multitexture_IMP \
    QGL(ActiveTextureARB); \
    QGL(ClientActiveTextureARB);

// GL_ARB_vertex_buffer_object
#define QGL_ARB_vertex_buffer_object_IMP \
    QGL(BindBufferARB); \
    QGL(DeleteBuffersARB); \
    QGL(GenBuffersARB); \
    QGL(IsBufferARB); \
    QGL(BufferDataARB); \
    QGL(BufferSubDataARB); \
    QGL(GetBufferSubDataARB); \
    QGL(MapBufferARB); \
    QGL(UnmapBufferARB); \
    QGL(GetBufferParameterivARB); \
    QGL(GetBufferPointervARB);

// GL_EXT_compiled_vertex_array
#define QGL_EXT_compiled_vertex_array_IMP \
    QGL(LockArraysEXT); \
    QGL(UnlockArraysEXT);

#define QGL_ARB_fragment_program            (1 << 0)
#define QGL_ARB_multitexture                (1 << 1)
#define QGL_ARB_vertex_buffer_object        (1 << 2)
#define QGL_EXT_compiled_vertex_array       (1 << 3)
#define QGL_EXT_texture_filter_anisotropic  (1 << 4)

#define QGL_3_0_core_functions              (1U << 31)

// ==========================================================

// subset of OpenGL 1.1 core functions
typedef void (APIENTRY * qglAlphaFunc_t)(GLenum func, GLclampf ref);
typedef void (APIENTRY * qglBindTexture_t)(GLenum target, GLuint texture);
typedef void (APIENTRY * qglBlendFunc_t)(GLenum sfactor, GLenum dfactor);
typedef void (APIENTRY * qglClear_t)(GLbitfield mask);
typedef void (APIENTRY * qglClearColor_t)(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
typedef void (APIENTRY * qglClearDepth_t)(GLclampd depth);
typedef void (APIENTRY * qglClearStencil_t)(GLint s);
typedef void (APIENTRY * qglColor4f_t)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void (APIENTRY * qglColor4fv_t)(const GLfloat *v);
typedef void (APIENTRY * qglColorMask_t)(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
typedef void (APIENTRY * qglColorPointer_t)(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
typedef void (APIENTRY * qglCopyTexImage2D_t)(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
typedef void (APIENTRY * qglCopyTexSubImage2D_t)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (APIENTRY * qglCullFace_t)(GLenum mode);
typedef void (APIENTRY * qglDeleteTextures_t)(GLsizei n, const GLuint *textures);
typedef void (APIENTRY * qglDepthFunc_t)(GLenum func);
typedef void (APIENTRY * qglDepthMask_t)(GLboolean flag);
typedef void (APIENTRY * qglDepthRange_t)(GLclampd zNear, GLclampd zFar);
typedef void (APIENTRY * qglDisable_t)(GLenum cap);
typedef void (APIENTRY * qglDisableClientState_t)(GLenum array);
typedef void (APIENTRY * qglDrawArrays_t)(GLenum mode, GLint first, GLsizei count);
typedef void (APIENTRY * qglDrawElements_t)(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
typedef void (APIENTRY * qglEnable_t)(GLenum cap);
typedef void (APIENTRY * qglEnableClientState_t)(GLenum array);
typedef void (APIENTRY * qglFinish_t)(void);
typedef void (APIENTRY * qglFlush_t)(void);
typedef void (APIENTRY * qglFogf_t)(GLenum pname, GLfloat param);
typedef void (APIENTRY * qglFogfv_t)(GLenum pname, const GLfloat *params);
typedef void (APIENTRY * qglFrontFace_t)(GLenum mode);
typedef void (APIENTRY * qglFrustum_t)(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
typedef void (APIENTRY * qglGenTextures_t)(GLsizei n, GLuint *textures);
typedef GLenum (APIENTRY * qglGetError_t)(void);
typedef void (APIENTRY * qglGetFloatv_t)(GLenum pname, GLfloat *params);
typedef void (APIENTRY * qglGetIntegerv_t)(GLenum pname, GLint *params);
typedef const GLubyte * (APIENTRY * qglGetString_t)(GLenum name);
typedef void (APIENTRY * qglHint_t)(GLenum target, GLenum mode);
typedef GLboolean (APIENTRY * qglIsEnabled_t)(GLenum cap);
typedef GLboolean (APIENTRY * qglIsTexture_t)(GLuint texture);
typedef void (APIENTRY * qglLightModelf_t)(GLenum pname, GLfloat param);
typedef void (APIENTRY * qglLightModelfv_t)(GLenum pname, const GLfloat *params);
typedef void (APIENTRY * qglLightf_t)(GLenum light, GLenum pname, GLfloat param);
typedef void (APIENTRY * qglLightfv_t)(GLenum light, GLenum pname, const GLfloat *params);
typedef void (APIENTRY * qglLineWidth_t)(GLfloat width);
typedef void (APIENTRY * qglLoadIdentity_t)(void);
typedef void (APIENTRY * qglLoadMatrixf_t)(const GLfloat *m);
typedef void (APIENTRY * qglLogicOp_t)(GLenum opcode);
typedef void (APIENTRY * qglMaterialf_t)(GLenum face, GLenum pname, GLfloat param);
typedef void (APIENTRY * qglMaterialfv_t)(GLenum face, GLenum pname, const GLfloat *params);
typedef void (APIENTRY * qglMatrixMode_t)(GLenum mode);
typedef void (APIENTRY * qglMultMatrixf_t)(const GLfloat *m);
typedef void (APIENTRY * qglNormal3f_t)(GLfloat nx, GLfloat ny, GLfloat nz);
typedef void (APIENTRY * qglNormal3fv_t)(const GLfloat *v);
typedef void (APIENTRY * qglNormalPointer_t)(GLenum type, GLsizei stride, const GLvoid *pointer);
typedef void (APIENTRY * qglOrtho_t)(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
typedef void (APIENTRY * qglPixelStorei_t)(GLenum pname, GLint param);
typedef void (APIENTRY * qglPointSize_t)(GLfloat size);
typedef void (APIENTRY * qglPolygonMode_t)(GLenum face, GLenum mode);
typedef void (APIENTRY * qglPolygonOffset_t)(GLfloat factor, GLfloat units);
typedef void (APIENTRY * qglPopMatrix_t)(void);
typedef void (APIENTRY * qglPushMatrix_t)(void);
typedef void (APIENTRY * qglReadPixels_t)(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
typedef void (APIENTRY * qglRotatef_t)(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRY * qglScalef_t)(GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRY * qglScissor_t)(GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (APIENTRY * qglShadeModel_t)(GLenum mode);
typedef void (APIENTRY * qglStencilFunc_t)(GLenum func, GLint ref, GLuint mask);
typedef void (APIENTRY * qglStencilMask_t)(GLuint mask);
typedef void (APIENTRY * qglStencilOp_t)(GLenum fail, GLenum zfail, GLenum zpass);
typedef void (APIENTRY * qglTexCoordPointer_t)(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
typedef void (APIENTRY * qglTexEnvf_t)(GLenum target, GLenum pname, GLfloat param);
typedef void (APIENTRY * qglTexEnvfv_t)(GLenum target, GLenum pname, const GLfloat *params);
typedef void (APIENTRY * qglTexImage2D_t)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
typedef void (APIENTRY * qglTexParameterf_t)(GLenum target, GLenum pname, GLfloat param);
typedef void (APIENTRY * qglTexParameterfv_t)(GLenum target, GLenum pname, const GLfloat *params);
typedef void (APIENTRY * qglTexSubImage2D_t)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
typedef void (APIENTRY * qglTranslatef_t)(GLfloat x, GLfloat y, GLfloat z);
typedef void (APIENTRY * qglVertexPointer_t)(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
typedef void (APIENTRY * qglViewport_t)(GLint x, GLint y, GLsizei width, GLsizei height);

// OpenGL 3.0 core function
typedef void (APIENTRY * qglGenerateMipmap_t)(GLenum target);

// GL_ARB_fragment_program
typedef void (APIENTRY * qglProgramStringARB_t)(GLenum target, GLenum format, GLsizei len, const GLvoid *string);
typedef void (APIENTRY * qglBindProgramARB_t)(GLenum target, GLuint program);
typedef void (APIENTRY * qglDeleteProgramsARB_t)(GLsizei n, const GLuint *programs);
typedef void (APIENTRY * qglGenProgramsARB_t)(GLsizei n, GLuint *programs);
typedef void (APIENTRY * qglProgramEnvParameter4fvARB_t)(GLenum target, GLuint index, const GLfloat *params);
typedef void (APIENTRY * qglProgramLocalParameter4fvARB_t)(GLenum target, GLuint index, const GLfloat *params);
typedef void (APIENTRY * qglGetProgramEnvParameterfvARB_t)(GLenum target, GLuint index, GLfloat *params);
typedef void (APIENTRY * qglGetProgramLocalParameterfvARB_t)(GLenum target, GLuint index, GLfloat *params);
typedef void (APIENTRY * qglGetProgramivARB_t)(GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRY * qglGetProgramStringARB_t)(GLenum target, GLenum pname, GLvoid *string);
typedef GLboolean (APIENTRY * qglIsProgramARB_t)(GLuint program);

// GL_ARB_multitexture
typedef void (APIENTRY * qglActiveTextureARB_t)(GLenum texture);
typedef void (APIENTRY * qglClientActiveTextureARB_t)(GLenum texture);

// GL_ARB_vertex_buffer_object
typedef void (APIENTRY * qglBindBufferARB_t)(GLenum target, GLuint buffer);
typedef void (APIENTRY * qglDeleteBuffersARB_t)(GLsizei n, const GLuint *buffers);
typedef void (APIENTRY * qglGenBuffersARB_t)(GLsizei n, GLuint *buffers);
typedef GLboolean (APIENTRY * qglIsBufferARB_t)(GLuint buffer);
typedef void (APIENTRY * qglBufferDataARB_t)(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
typedef void (APIENTRY * qglBufferSubDataARB_t)(GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid *data);
typedef void (APIENTRY * qglGetBufferSubDataARB_t)(GLenum target, GLintptrARB offset, GLsizeiptrARB size, GLvoid *data);
typedef GLvoid * (APIENTRY * qglMapBufferARB_t)(GLenum target, GLenum access);
typedef GLboolean (APIENTRY * qglUnmapBufferARB_t)(GLenum target);
typedef void (APIENTRY * qglGetBufferParameterivARB_t)(GLenum target, GLenum pname, GLint *params);
typedef void (APIENTRY * qglGetBufferPointervARB_t)(GLenum target, GLenum pname, GLvoid* *params);

// GL_EXT_compiled_vertex_array
typedef void (APIENTRY * qglLockArraysEXT_t)(GLint first, GLsizei count);
typedef void (APIENTRY * qglUnlockArraysEXT_t)(void);

// ==========================================================

qboolean QGL_Init(void);
void QGL_Shutdown(void);
void QGL_InitExtensions(unsigned mask);
void QGL_ShutdownExtensions(unsigned mask);

unsigned QGL_ParseExtensionString(const char *s);

#ifdef _DEBUG
void QGL_EnableLogging(unsigned mask);
void QGL_DisableLogging(unsigned mask);
void QGL_LogComment(const char *fmt, ...);
#endif

#define QGL(x)  extern qgl##x##_t qgl##x
QGL_core_IMP
QGL_ARB_fragment_program_IMP
QGL_ARB_multitexture_IMP
QGL_ARB_vertex_buffer_object_IMP
QGL_EXT_compiled_vertex_array_IMP
#undef QGL

extern qglGenerateMipmap_t qglGenerateMipmap;

#endif  // QGL_H

