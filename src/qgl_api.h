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
** QGL.H
*/

#ifndef __QGL_H__
#define __QGL_H__

#define QGL_EXT_compiled_vertex_array       (1<<0)
#define QGL_ARB_multitexture                (1<<1)
#define QGL_EXT_texture_filter_anisotropic  (1<<2)
#define QGL_ARB_fragment_program            (1<<3)
#define QGL_ARB_vertex_buffer_object        (1<<4)

void    QGL_Init( void );
void    QGL_Shutdown( void );
void    QGL_InitExtensions( unsigned mask );
void    QGL_ShutdownExtensions( unsigned mask );
unsigned QGL_ParseExtensionString( const char *s );
void    QGL_EnableLogging( qboolean enable );
void    QGL_LogNewFrame( void );

extern void ( APIENTRY * qglAlphaFunc )(GLenum func, GLclampf ref);
extern void ( APIENTRY * qglBindTexture )(GLenum target, GLuint texture);
extern void ( APIENTRY * qglBlendFunc )(GLenum sfactor, GLenum dfactor);
extern void ( APIENTRY * qglClear )(GLbitfield mask);
extern void ( APIENTRY * qglClearColor )(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
extern void ( APIENTRY * qglClearDepth )(GLclampd depth);
extern void ( APIENTRY * qglClearIndex )(GLfloat c);
extern void ( APIENTRY * qglClearStencil )(GLint s);
extern void ( APIENTRY * qglColor4f )(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
extern void ( APIENTRY * qglColor4fv )(const GLfloat *v);
extern void ( APIENTRY * qglColor4ub )(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
extern void ( APIENTRY * qglColor4ubv )(const GLubyte *v);
extern void ( APIENTRY * qglColorMask )(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
extern void ( APIENTRY * qglColorPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
extern void ( APIENTRY * qglCopyTexImage2D )(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
extern void ( APIENTRY * qglCopyTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
extern void ( APIENTRY * qglCullFace )(GLenum mode);
extern void ( APIENTRY * qglDeleteTextures )(GLsizei n, const GLuint *textures);
extern void ( APIENTRY * qglDepthFunc )(GLenum func);
extern void ( APIENTRY * qglDepthMask )(GLboolean flag);
extern void ( APIENTRY * qglDepthRange )(GLclampd zNear, GLclampd zFar);
extern void ( APIENTRY * qglDisable )(GLenum cap);
extern void ( APIENTRY * qglDisableClientState )(GLenum array);
extern void ( APIENTRY * qglDrawArrays )(GLenum mode, GLint first, GLsizei count);
extern void ( APIENTRY * qglDrawElements )(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
extern void ( APIENTRY * qglEnable )(GLenum cap);
extern void ( APIENTRY * qglEnableClientState )(GLenum array);
extern void ( APIENTRY * qglFinish )(void);
extern void ( APIENTRY * qglFlush )(void);
extern void ( APIENTRY * qglFogf )(GLenum pname, GLfloat param);
extern void ( APIENTRY * qglFogfv )(GLenum pname, const GLfloat *params);
extern void ( APIENTRY * qglFrontFace )(GLenum mode);
extern void ( APIENTRY * qglFrustum )(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
extern void ( APIENTRY * qglGenTextures )(GLsizei n, GLuint *textures);
extern void ( APIENTRY * qglGetBooleanv )(GLenum pname, GLboolean *params);
extern GLenum ( APIENTRY * qglGetError )(void);
extern void ( APIENTRY * qglGetFloatv )(GLenum pname, GLfloat *params);
extern void ( APIENTRY * qglGetIntegerv )(GLenum pname, GLint *params);
extern void ( APIENTRY * qglGetLightfv )(GLenum light, GLenum pname, GLfloat *params);
extern void ( APIENTRY * qglGetMaterialfv )(GLenum face, GLenum pname, GLfloat *params);
extern void ( APIENTRY * qglGetPointerv )(GLenum pname, GLvoid* *params);
extern const GLubyte * ( APIENTRY * qglGetString )(GLenum name);
extern void ( APIENTRY * qglGetTexEnvfv )(GLenum target, GLenum pname, GLfloat *params);
extern void ( APIENTRY * qglGetTexEnviv )(GLenum target, GLenum pname, GLint *params);
extern void ( APIENTRY * qglGetTexParameterfv )(GLenum target, GLenum pname, GLfloat *params);
extern void ( APIENTRY * qglGetTexParameteriv )(GLenum target, GLenum pname, GLint *params);
extern void ( APIENTRY * qglHint )(GLenum target, GLenum mode);
extern GLboolean ( APIENTRY * qglIsEnabled )(GLenum cap);
extern GLboolean ( APIENTRY * qglIsTexture )(GLuint texture);
extern void ( APIENTRY * qglLightModelf )(GLenum pname, GLfloat param);
extern void ( APIENTRY * qglLightModelfv )(GLenum pname, const GLfloat *params);
extern void ( APIENTRY * qglLightf )(GLenum light, GLenum pname, GLfloat param);
extern void ( APIENTRY * qglLightfv )(GLenum light, GLenum pname, const GLfloat *params);
extern void ( APIENTRY * qglLineWidth )(GLfloat width);
extern void ( APIENTRY * qglLoadIdentity )(void);
extern void ( APIENTRY * qglLoadMatrixf )(const GLfloat *m);
extern void ( APIENTRY * qglLogicOp )(GLenum opcode);
extern void ( APIENTRY * qglMaterialf )(GLenum face, GLenum pname, GLfloat param);
extern void ( APIENTRY * qglMaterialfv )(GLenum face, GLenum pname, const GLfloat *params);
extern void ( APIENTRY * qglMatrixMode )(GLenum mode);
extern void ( APIENTRY * qglMultMatrixf )(const GLfloat *m);
extern void ( APIENTRY * qglNormal3f )(GLfloat nx, GLfloat ny, GLfloat nz);
extern void ( APIENTRY * qglNormal3fv )(const GLfloat *v);
extern void ( APIENTRY * qglNormalPointer )(GLenum type, GLsizei stride, const GLvoid *pointer);
extern void ( APIENTRY * qglOrtho )(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
extern void ( APIENTRY * qglPixelStorei )(GLenum pname, GLint param);
extern void ( APIENTRY * qglPointSize )(GLfloat size);
extern void ( APIENTRY * qglPolygonMode )(GLenum face, GLenum mode);
extern void ( APIENTRY * qglPolygonOffset )(GLfloat factor, GLfloat units);
extern void ( APIENTRY * qglPopMatrix )(void);
extern void ( APIENTRY * qglPushMatrix )(void);
extern void ( APIENTRY * qglReadPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
extern void ( APIENTRY * qglRotatef )(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
extern void ( APIENTRY * qglScalef )(GLfloat x, GLfloat y, GLfloat z);
extern void ( APIENTRY * qglScissor )(GLint x, GLint y, GLsizei width, GLsizei height);
extern void ( APIENTRY * qglShadeModel )(GLenum mode);
extern void ( APIENTRY * qglStencilFunc )(GLenum func, GLint ref, GLuint mask);
extern void ( APIENTRY * qglStencilMask )(GLuint mask);
extern void ( APIENTRY * qglStencilOp )(GLenum fail, GLenum zfail, GLenum zpass);
extern void ( APIENTRY * qglTexCoordPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
extern void ( APIENTRY * qglTexEnvf )(GLenum target, GLenum pname, GLfloat param);
extern void ( APIENTRY * qglTexEnvfv )(GLenum target, GLenum pname, const GLfloat *params);
extern void ( APIENTRY * qglTexEnvi )(GLenum target, GLenum pname, GLint param);
extern void ( APIENTRY * qglTexEnviv )(GLenum target, GLenum pname, const GLint *params);
extern void ( APIENTRY * qglTexImage2D )(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
extern void ( APIENTRY * qglTexParameterf )(GLenum target, GLenum pname, GLfloat param);
extern void ( APIENTRY * qglTexParameterfv )(GLenum target, GLenum pname, const GLfloat *params);
extern void ( APIENTRY * qglTexParameteri )(GLenum target, GLenum pname, GLint param);
extern void ( APIENTRY * qglTexParameteriv )(GLenum target, GLenum pname, const GLint *params);
extern void ( APIENTRY * qglTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
extern void ( APIENTRY * qglTranslatef )(GLfloat x, GLfloat y, GLfloat z);
extern void ( APIENTRY * qglVertexPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
extern void ( APIENTRY * qglViewport )(GLint x, GLint y, GLsizei width, GLsizei height);

//
// extensions
//

// GL_EXT_compiled_vertex_array
extern void ( APIENTRY * qglLockArraysEXT )(GLint first, GLsizei count);
extern void ( APIENTRY * qglUnlockArraysEXT )(void);

// GL_ARB_multitexture
extern void ( APIENTRY * qglActiveTextureARB )(GLenum texture);
extern void ( APIENTRY * qglClientActiveTextureARB )(GLenum texture);

// GL_ARB_fragment_program
extern void ( APIENTRY * qglProgramStringARB )(GLenum target, GLenum format, GLsizei len, const GLvoid *string);
extern void ( APIENTRY * qglBindProgramARB )(GLenum target, GLuint program);
extern void ( APIENTRY * qglDeleteProgramsARB )(GLsizei n, const GLuint *programs);
extern void ( APIENTRY * qglGenProgramsARB )(GLsizei n, GLuint *programs);
extern void ( APIENTRY * qglProgramEnvParameter4fvARB )(GLenum target, GLuint index, const GLfloat *params);
extern void ( APIENTRY * qglProgramLocalParameter4fvARB )(GLenum target, GLuint index, const GLfloat *params);
extern void ( APIENTRY * qglGetProgramEnvParameterfvARB )(GLenum, GLuint, GLfloat *);
extern void ( APIENTRY * qglGetProgramLocalParameterfvARB )(GLenum, GLuint, GLfloat *);
extern void ( APIENTRY * qglGetProgramivARB )(GLenum, GLenum, GLint *);
extern void ( APIENTRY * qglGetProgramStringARB )(GLenum, GLenum, GLvoid *);
extern GLboolean ( APIENTRY * qglIsProgramARB )(GLuint);

// GL_ARB_vertex_buffer_object
extern void ( APIENTRY * qglBindBufferARB )(GLenum target, GLuint buffer);
extern void ( APIENTRY * qglDeleteBuffersARB )(GLsizei n, const GLuint *buffers);
extern void ( APIENTRY * qglGenBuffersARB )(GLsizei n, GLuint *buffers);
extern GLboolean ( APIENTRY * qglIsBufferARB )(GLuint);
extern void ( APIENTRY * qglBufferDataARB )(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
extern void ( APIENTRY * qglBufferSubDataARB )(GLenum, GLintptrARB, GLsizeiptrARB, const GLvoid *);
extern void ( APIENTRY * qglGetBufferSubDataARB )(GLenum, GLintptrARB, GLsizeiptrARB, GLvoid *);
extern GLvoid * ( APIENTRY * qglMapBufferARB )(GLenum target, GLenum access);
extern GLboolean ( APIENTRY * qglUnmapBufferARB )(GLenum target);
extern void ( APIENTRY * qglGetBufferParameterivARB )(GLenum, GLenum, GLint *);
extern void ( APIENTRY * qglGetBufferPointervARB )(GLenum, GLenum, GLvoid* *);

#endif

