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
#include "fixed.h"

// ==========================================================

// OpenGL 3.0 core function
PFNGLGENERATEMIPMAPPROC     qglGenerateMipmap;

// GL_ARB_fragment_program
PFNGLPROGRAMSTRINGARBPROC               qglProgramStringARB;
PFNGLBINDPROGRAMARBPROC                 qglBindProgramARB;
PFNGLDELETEPROGRAMSARBPROC              qglDeleteProgramsARB;
PFNGLGENPROGRAMSARBPROC                 qglGenProgramsARB;
PFNGLPROGRAMENVPARAMETER4FVARBPROC      qglProgramEnvParameter4fvARB;
PFNGLPROGRAMLOCALPARAMETER4FVARBPROC    qglProgramLocalParameter4fvARB; 

// GL_ARB_multitexture
PFNGLACTIVETEXTUREARBPROC       qglActiveTextureARB;
PFNGLCLIENTACTIVETEXTUREARBPROC qglClientActiveTextureARB;

// GL_ARB_vertex_buffer_object
PFNGLBINDBUFFERARBPROC      qglBindBufferARB;
PFNGLDELETEBUFFERSARBPROC   qglDeleteBuffersARB;
PFNGLGENBUFFERSARBPROC      qglGenBuffersARB;
PFNGLBUFFERDATAARBPROC      qglBufferDataARB;
PFNGLBUFFERSUBDATAARBPROC   qglBufferSubDataARB;

// GL_EXT_compiled_vertex_array
PFNGLLOCKARRAYSEXTPROC      qglLockArraysEXT;
PFNGLUNLOCKARRAYSEXTPROC    qglUnlockArraysEXT;

// ==========================================================

void QGL_ShutdownExtensions(unsigned mask)
{
    if (mask & QGL_ARB_fragment_program) {
        qglProgramStringARB             = NULL;
        qglBindProgramARB               = NULL;
        qglDeleteProgramsARB            = NULL;
        qglGenProgramsARB               = NULL;
        qglProgramEnvParameter4fvARB    = NULL;
        qglProgramLocalParameter4fvARB  = NULL;
    }

    if (mask & QGL_ARB_multitexture) {
        qglActiveTextureARB         = NULL;
        qglClientActiveTextureARB   = NULL;
    }

    if (mask & QGL_ARB_vertex_buffer_object) {
        qglBindBufferARB    = NULL;
        qglDeleteBuffersARB = NULL;
        qglGenBuffersARB    = NULL;
        qglBufferDataARB    = NULL;
        qglBufferSubDataARB = NULL;
    }

    if (mask & QGL_EXT_compiled_vertex_array) {
        qglLockArraysEXT    = NULL;
        qglUnlockArraysEXT  = NULL;
    }

    if (mask & QGL_3_0_core_functions) {
        qglGenerateMipmap   = NULL;
    }
}

#define GPA(x)  VID_GetProcAddr(x)

void QGL_InitExtensions(unsigned mask)
{
    if (mask & QGL_ARB_fragment_program) {
        qglProgramStringARB             = GPA("glProgramStringARB");
        qglBindProgramARB               = GPA("glBindProgramARB");
        qglDeleteProgramsARB            = GPA("glDeleteProgramsARB");
        qglGenProgramsARB               = GPA("glGenProgramsARB");
        qglProgramEnvParameter4fvARB    = GPA("glProgramEnvParameter4fvARB");
        qglProgramLocalParameter4fvARB  = GPA("glProgramLocalParameter4fvARB");
    }

    if (mask & QGL_ARB_multitexture) {
        qglActiveTextureARB         = GPA("glActiveTextureARB");
        qglClientActiveTextureARB   = GPA("glClientActiveTextureARB");
    }

    if (mask & QGL_ARB_vertex_buffer_object) {
        qglBindBufferARB    = GPA("glBindBufferARB");
        qglDeleteBuffersARB = GPA("glDeleteBuffersARB");
        qglGenBuffersARB    = GPA("glGenBuffersARB");
        qglBufferDataARB    = GPA("glBufferDataARB");
        qglBufferSubDataARB = GPA("glBufferSubDataARB");
    }

    if (mask & QGL_EXT_compiled_vertex_array) {
        qglLockArraysEXT    = GPA("glLockArraysEXT");
        qglUnlockArraysEXT  = GPA("glUnlockArraysEXT");
    }

    if (mask & QGL_3_0_core_functions) {
        qglGenerateMipmap   = GPA("glGenerateMipmap");
    }
}

#undef GPA

unsigned QGL_ParseExtensionString(const char *s)
{
    // must match defines in fixed.h!
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
