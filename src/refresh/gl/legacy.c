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

#include "gl.h"
#include "arbfp.h"

static void legacy_state_bits(GLbitfield bits)
{
    GLbitfield diff = bits ^ gls.state_bits;

    if (diff & GLS_COMMON_MASK) {
        GL_CommonStateBits(bits);
    }

    if (diff & GLS_ALPHATEST_ENABLE) {
        if (bits & GLS_ALPHATEST_ENABLE) {
            qglEnable(GL_ALPHA_TEST);
        } else {
            qglDisable(GL_ALPHA_TEST);
        }
    }

    if (diff & GLS_TEXTURE_REPLACE) {
        GL_ActiveTexture(0);
        if (bits & GLS_TEXTURE_REPLACE) {
            qglTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        } else {
            qglTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        }
    }

    if (diff & GLS_FLOW_ENABLE) {
        GL_ActiveTexture(0);
        qglMatrixMode(GL_TEXTURE);

        if (bits & GLS_FLOW_ENABLE) {
            float scaled, scroll;

            if (bits & GLS_WARP_ENABLE) {
                scaled = glr.fd.time * 0.5f;
                scroll = -scaled;
            } else {
                scaled = glr.fd.time / 40;
                scroll = -64 * (scaled - (int)scaled);
            }

            qglTranslatef(scroll, 0, 0);
        } else {
            qglLoadIdentity();
        }
    }

    if (diff & GLS_LIGHTMAP_ENABLE) {
        GL_ActiveTexture(1);
        if (bits & GLS_LIGHTMAP_ENABLE) {
            qglEnable(GL_TEXTURE_2D);
        } else {
            qglDisable(GL_TEXTURE_2D);
        }
    }

    if ((diff & GLS_WARP_ENABLE) && gl_static.programs[0]) {
        if (bits & GLS_WARP_ENABLE) {
            vec4_t param;

            qglEnable(GL_FRAGMENT_PROGRAM_ARB);
            qglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, gl_static.programs[0]);
            param[0] = glr.fd.time;
            param[1] = glr.fd.time;
            param[2] = param[3] = 0;
            qglProgramLocalParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB, 0, param);
        } else {
            qglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 0);
            qglDisable(GL_FRAGMENT_PROGRAM_ARB);
        }
    }

    if (diff & GLS_SHADE_SMOOTH) {
        if (bits & GLS_SHADE_SMOOTH) {
            qglShadeModel(GL_SMOOTH);
        } else {
            qglShadeModel(GL_FLAT);
        }
    }
}

static void legacy_array_bits(GLbitfield bits)
{
    GLbitfield diff = bits ^ gls.array_bits;

    if (diff & GLA_VERTEX) {
        if (bits & GLA_VERTEX) {
            qglEnableClientState(GL_VERTEX_ARRAY);
        } else {
            qglDisableClientState(GL_VERTEX_ARRAY);
        }
    }

    if (diff & GLA_TC) {
        GL_ClientActiveTexture(0);
        if (bits & GLA_TC) {
            qglEnableClientState(GL_TEXTURE_COORD_ARRAY);
        } else {
            qglDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
    }

    if (diff & GLA_LMTC) {
        GL_ClientActiveTexture(1);
        if (bits & GLA_LMTC) {
            qglEnableClientState(GL_TEXTURE_COORD_ARRAY);
        } else {
            qglDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
    }

    if (diff & GLA_COLOR) {
        if (bits & GLA_COLOR) {
            qglEnableClientState(GL_COLOR_ARRAY);
        } else {
            qglDisableClientState(GL_COLOR_ARRAY);
        }
    }
}

static void legacy_vertex_pointer(GLint size, GLsizei stride, const GLfloat *pointer)
{
    qglVertexPointer(size, GL_FLOAT, sizeof(GLfloat) * stride, pointer);
}

static void legacy_tex_coord_pointer(GLint size, GLsizei stride, const GLfloat *pointer)
{
    GL_ClientActiveTexture(0);
    qglTexCoordPointer(size, GL_FLOAT, sizeof(GLfloat) * stride, pointer);
}

static void legacy_light_coord_pointer(GLint size, GLsizei stride, const GLfloat *pointer)
{
    GL_ClientActiveTexture(1);
    qglTexCoordPointer(size, GL_FLOAT, sizeof(GLfloat) * stride, pointer);
}

static void legacy_color_byte_pointer(GLint size, GLsizei stride, const GLubyte *pointer)
{
    qglColorPointer(size, GL_UNSIGNED_BYTE, sizeof(GLfloat) * stride, pointer);
}

static void legacy_color_float_pointer(GLint size, GLsizei stride, const GLfloat *pointer)
{
    qglColorPointer(size, GL_FLOAT, sizeof(GLfloat) * stride, pointer);
}

static void legacy_color(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    qglColor4f(r, g, b, a);
}

static void legacy_view_matrix(const GLfloat *matrix)
{
    qglMatrixMode(GL_MODELVIEW);

    if (matrix)
        qglLoadMatrixf(matrix);
    else
        qglLoadIdentity();
}

static void legacy_proj_matrix(const GLfloat *matrix)
{
    qglMatrixMode(GL_PROJECTION);
    qglLoadMatrixf(matrix);
}

static void legacy_clear(void)
{
    qglDisable(GL_ALPHA_TEST);
    qglAlphaFunc(GL_GREATER, 0.666f);
    qglShadeModel(GL_FLAT);

    if (qglActiveTexture && qglClientActiveTexture) {
        qglActiveTexture(GL_TEXTURE1);
        qglBindTexture(GL_TEXTURE_2D, 0);
        qglDisable(GL_TEXTURE_2D);
        qglClientActiveTexture(GL_TEXTURE1);
        qglDisableClientState(GL_TEXTURE_COORD_ARRAY);

        qglActiveTexture(GL_TEXTURE0);
        qglBindTexture(GL_TEXTURE_2D, 0);
        qglEnable(GL_TEXTURE_2D);
        qglClientActiveTexture(GL_TEXTURE0);
        qglDisableClientState(GL_TEXTURE_COORD_ARRAY);
    } else {
        qglBindTexture(GL_TEXTURE_2D, 0);
        qglEnable(GL_TEXTURE_2D);
        qglDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    qglMatrixMode(GL_TEXTURE);
    qglLoadIdentity();

    qglDisableClientState(GL_VERTEX_ARRAY);
    qglDisableClientState(GL_COLOR_ARRAY);

    if (gl_static.programs[0]) {
        qglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 0);
        qglDisable(GL_FRAGMENT_PROGRAM_ARB);
    }
}

static void legacy_init(void)
{
    GLuint prog = 0;

    if (!qglGenProgramsARB) {
        return;
    }

    QGL_ClearErrors();

    qglGenProgramsARB(1, &prog);
    qglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, prog);
    qglProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
                        sizeof(gl_prog_warp) - 1, gl_prog_warp);

    if (GL_ShowErrors("Failed to initialize fragment program")) {
        qglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 0);
        qglDeleteProgramsARB(1, &prog);
        return;
    }

    qglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 0);
    gl_static.programs[0] = prog;
}

static void legacy_shutdown(void)
{
    if (gl_static.programs[0]) {
        qglDeleteProgramsARB(1, gl_static.programs);
        gl_static.programs[0] = 0;
    }
}

const glbackend_t backend_legacy = {
    .name = "legacy",

    .init = legacy_init,
    .shutdown = legacy_shutdown,
    .clear = legacy_clear,

    .proj_matrix = legacy_proj_matrix,
    .view_matrix = legacy_view_matrix,

    .state_bits = legacy_state_bits,
    .array_bits = legacy_array_bits,

    .vertex_pointer = legacy_vertex_pointer,
    .tex_coord_pointer = legacy_tex_coord_pointer,
    .light_coord_pointer = legacy_light_coord_pointer,
    .color_byte_pointer = legacy_color_byte_pointer,
    .color_float_pointer = legacy_color_float_pointer,
    .color = legacy_color,
};
