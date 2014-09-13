/*
Copyright (C) 2003-2006 Andrey Nazarov

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

glState_t gls;

// for uploading
void GL_ForceTexture(GLuint tmu, GLuint texnum)
{
    GL_ActiveTexture(tmu);

    if (gls.texnums[tmu] == texnum) {
        return;
    }

    qglBindTexture(GL_TEXTURE_2D, texnum);
    gls.texnums[tmu] = texnum;

    c.texSwitches++;
}

// for drawing
void GL_BindTexture(GLuint tmu, GLuint texnum)
{
#ifdef _DEBUG
    if (gl_nobind->integer && !tmu) {
        texnum = TEXNUM_DEFAULT;
    }
#endif

    if (gls.texnums[tmu] == texnum) {
        return;
    }

    GL_ActiveTexture(tmu);

    qglBindTexture(GL_TEXTURE_2D, texnum);
    gls.texnums[tmu] = texnum;

    c.texSwitches++;
}

void GL_StateBits(glStateBits_t bits)
{
    glStateBits_t diff = bits ^ gls.state_bits;

    if (!diff) {
        return;
    }

    if (diff & GLS_BLEND_MASK) {
        if (bits & GLS_BLEND_MASK) {
            qglEnable(GL_BLEND);
            if (bits & GLS_BLEND_BLEND) {
                qglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            } else if (bits & GLS_BLEND_ADD) {
                qglBlendFunc(GL_SRC_ALPHA, GL_ONE);
            } else if (bits & GLS_BLEND_MODULATE) {
                qglBlendFunc(GL_DST_COLOR, GL_ONE);
            }
        } else {
            qglDisable(GL_BLEND);
        }
    }

    if (diff & GLS_DEPTHMASK_FALSE) {
        if (bits & GLS_DEPTHMASK_FALSE) {
            qglDepthMask(GL_FALSE);
        } else {
            qglDepthMask(GL_TRUE);
        }
    }

    if (diff & GLS_DEPTHTEST_DISABLE) {
        if (bits & GLS_DEPTHTEST_DISABLE) {
            qglDisable(GL_DEPTH_TEST);
        } else {
            qglEnable(GL_DEPTH_TEST);
        }
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

        qglMatrixMode(GL_MODELVIEW);
    }

    if (diff & GLS_LIGHTMAP_ENABLE) {
        GL_ActiveTexture(1);
        if (bits & GLS_LIGHTMAP_ENABLE) {
            qglEnable(GL_TEXTURE_2D);
        } else {
            qglDisable(GL_TEXTURE_2D);
        }
    }

#ifdef GL_ARB_fragment_program
    if ((diff & GLS_WARP_ENABLE) && gl_static.prognum_warp) {
        if (bits & GLS_WARP_ENABLE) {
            vec4_t param;

            qglEnable(GL_FRAGMENT_PROGRAM_ARB);
            qglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, gl_static.prognum_warp);
            param[0] = glr.fd.time;
            param[1] = glr.fd.time;
            param[2] = param[3] = 0;
            qglProgramLocalParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB, 0, param);
        } else {
            qglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 0);
            qglDisable(GL_FRAGMENT_PROGRAM_ARB);
        }
    }
#endif

    if (diff & GLS_CULL_DISABLE) {
        if (bits & GLS_CULL_DISABLE) {
            qglDisable(GL_CULL_FACE);
        } else {
            qglEnable(GL_CULL_FACE);
        }
    }

    if (diff & GLS_SHADE_SMOOTH) {
        if (bits & GLS_SHADE_SMOOTH) {
            qglShadeModel(GL_SMOOTH);
        } else {
            qglShadeModel(GL_FLAT);
        }
    }

    gls.state_bits = bits;
}

void GL_ArrayBits(glArrayBits_t bits)
{
    glArrayBits_t diff = bits ^ gls.array_bits;

    if (!diff) {
        return;
    }

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

    gls.array_bits = bits;
}

void GL_Ortho(GLfloat xmin, GLfloat xmax, GLfloat ymin, GLfloat ymax, GLfloat znear, GLfloat zfar)
{
    GLfloat width, height, depth;
    GLfloat matrix[16];

    width = xmax - xmin;
    height = ymax - ymin;
    depth = zfar - znear;

    matrix[0] = 2 / width;
    matrix[4] = 0;
    matrix[8] = 0;
    matrix[12] = -(xmax + xmin) / width;

    matrix[1] = 0;
    matrix[5] = 2 / height;
    matrix[9] = 0;
    matrix[13] = -(ymax + ymin) / height;

    matrix[2] = 0;
    matrix[6] = 0;
    matrix[10] = -2 / depth;
    matrix[14] = -(zfar + znear) / depth;

    matrix[3] = 0;
    matrix[7] = 0;
    matrix[11] = 0;
    matrix[15] = 1;

    qglMatrixMode(GL_PROJECTION);
    qglLoadMatrixf(matrix);
}

void GL_Setup2D(void)
{
    qglViewport(0, 0, r_config.width, r_config.height);

    GL_Ortho(0, r_config.width, r_config.height, 0, -1, 1);
    draw.scale = 1;

    draw.colors[0].u32 = U32_WHITE;
    draw.colors[1].u32 = U32_WHITE;

    if (draw.scissor) {
        qglDisable(GL_SCISSOR_TEST);
        draw.scissor = qfalse;
    }

    qglMatrixMode(GL_MODELVIEW);
    qglLoadIdentity();
}

static void GL_Frustum(void)
{
    GLfloat xmin, xmax, ymin, ymax, zfar, znear;
    GLfloat width, height, depth;
    GLfloat matrix[16];

    znear = gl_znear->value;

    if (glr.fd.rdflags & RDF_NOWORLDMODEL)
        zfar = 2048;
    else
        zfar = gl_static.world.size * 2;

    ymax = znear * tan(glr.fd.fov_y * M_PI / 360.0);
    ymin = -ymax;

    xmax = znear * tan(glr.fd.fov_x * M_PI / 360.0);
    xmin = -xmax;

    width = xmax - xmin;
    height = ymax - ymin;
    depth = zfar - znear;

    matrix[0] = 2 * znear / width;
    matrix[4] = 0;
    matrix[8] = (xmax + xmin) / width;
    matrix[12] = 0;

    matrix[1] = 0;
    matrix[5] = 2 * znear / height;
    matrix[9] = (ymax + ymin) / height;
    matrix[13] = 0;

    matrix[2] = 0;
    matrix[6] = 0;
    matrix[10] = -(zfar + znear) / depth;
    matrix[14] = -2 * zfar * znear / depth;

    matrix[3] = 0;
    matrix[7] = 0;
    matrix[11] = -1;
    matrix[15] = 0;

    qglMatrixMode(GL_PROJECTION);
    qglLoadMatrixf(matrix);
}

static void GL_RotateForViewer(void)
{
    GLfloat *matrix = glr.viewmatrix;

    AnglesToAxis(glr.fd.viewangles, glr.viewaxis);

    matrix[0] = -glr.viewaxis[1][0];
    matrix[4] = -glr.viewaxis[1][1];
    matrix[8] = -glr.viewaxis[1][2];
    matrix[12] = DotProduct(glr.viewaxis[1], glr.fd.vieworg);

    matrix[1] = glr.viewaxis[2][0];
    matrix[5] = glr.viewaxis[2][1];
    matrix[9] = glr.viewaxis[2][2];
    matrix[13] = -DotProduct(glr.viewaxis[2], glr.fd.vieworg);

    matrix[2] = -glr.viewaxis[0][0];
    matrix[6] = -glr.viewaxis[0][1];
    matrix[10] = -glr.viewaxis[0][2];
    matrix[14] = DotProduct(glr.viewaxis[0], glr.fd.vieworg);

    matrix[3] = 0;
    matrix[7] = 0;
    matrix[11] = 0;
    matrix[15] = 1;

    qglMatrixMode(GL_MODELVIEW);
    qglLoadMatrixf(matrix);

    // forced matrix upload
    gls.currentmatrix = matrix;
}

void GL_Setup3D(void)
{
    qglViewport(glr.fd.x, r_config.height - (glr.fd.y + glr.fd.height),
                glr.fd.width, glr.fd.height);

    GL_Frustum();

    GL_RotateForViewer();

    // enable depth writes before clearing
    GL_StateBits(GLS_DEFAULT);

    qglClear(GL_DEPTH_BUFFER_BIT | gl_static.stencil_buffer_bit);
}

void GL_SetDefaultState(void)
{
    qglClearColor(0, 0, 0, 1);
    qglClearDepth(1);
    qglClearStencil(0);

    qglEnable(GL_DEPTH_TEST);
    qglDepthFunc(GL_LEQUAL);
    qglDepthRange(0, 1);
    qglDepthMask(GL_TRUE);
    qglDisable(GL_BLEND);
    qglDisable(GL_ALPHA_TEST);
    qglAlphaFunc(GL_GREATER, 0.666f);
    qglPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    qglFrontFace(GL_CW);
    qglCullFace(GL_BACK);
    qglEnable(GL_CULL_FACE);
    qglShadeModel(GL_FLAT);

    if (qglActiveTextureARB && qglClientActiveTextureARB) {
        qglActiveTextureARB(GL_TEXTURE1_ARB);
        qglBindTexture(GL_TEXTURE_2D, 0);
        qglDisable(GL_TEXTURE_2D);
        qglClientActiveTextureARB(GL_TEXTURE1_ARB);
        qglDisableClientState(GL_TEXTURE_COORD_ARRAY);

        qglActiveTextureARB(GL_TEXTURE0_ARB);
        qglBindTexture(GL_TEXTURE_2D, 0);
        qglEnable(GL_TEXTURE_2D);
        qglClientActiveTextureARB(GL_TEXTURE0_ARB);
        qglDisableClientState(GL_TEXTURE_COORD_ARRAY);
    } else {
        qglBindTexture(GL_TEXTURE_2D, 0);
        qglEnable(GL_TEXTURE_2D);
        qglDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    qglMatrixMode(GL_TEXTURE);
    qglLoadIdentity();
    qglMatrixMode(GL_MODELVIEW);

    qglDisableClientState(GL_VERTEX_ARRAY);
    qglDisableClientState(GL_COLOR_ARRAY);

    qglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | gl_static.stencil_buffer_bit);

    memset(&gls, 0, sizeof(gls));
}

// for screenshots
byte *IMG_ReadPixels(int *width, int *height, int *rowbytes)
{
    int align = 4;
    int pitch;
    byte *pixels;

    qglGetIntegerv(GL_PACK_ALIGNMENT, &align);
    pitch = (r_config.width * 3 + align - 1) & ~(align - 1);
    pixels = FS_AllocTempMem(pitch * r_config.height);

    qglReadPixels(0, 0, r_config.width, r_config.height,
                  GL_RGB, GL_UNSIGNED_BYTE, pixels);

    *width = r_config.width;
    *height = r_config.height;
    *rowbytes = pitch;

    return pixels;
}

void GL_EnableOutlines(void)
{
    GL_BindTexture(0, TEXNUM_WHITE);
    GL_StateBits(GLS_DEFAULT);
    GL_ArrayBits(GLA_VERTEX);
    qglColor4f(1, 1, 1, 1);

    qglPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    qglDepthRange(0, 0);
}

void GL_DisableOutlines(void)
{
    qglDepthRange(0, 1);
    qglPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void GL_InitPrograms(void)
{
#ifdef GL_ARB_fragment_program
    GLuint prog = 0;

    if (gl_config.ext_supported & QGL_ARB_fragment_program) {
        if (gl_fragment_program->integer) {
            Com_Printf("...enabling GL_ARB_fragment_program\n");
            QGL_InitExtensions(QGL_ARB_fragment_program);
            gl_config.ext_enabled |= QGL_ARB_fragment_program;
        } else {
            Com_Printf("...ignoring GL_ARB_fragment_program\n");
        }
    } else if (gl_fragment_program->integer) {
        Com_Printf("GL_ARB_fragment_program not found\n");
        Cvar_Set("gl_fragment_program", "0");
    }

    if (!qglGenProgramsARB || !qglBindProgramARB ||
        !qglProgramStringARB || !qglDeleteProgramsARB) {
        return;
    }

    GL_ClearErrors();

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
    gl_static.prognum_warp = prog;
#endif
}

void GL_ShutdownPrograms(void)
{
#ifdef GL_ARB_fragment_program
    if (!qglDeleteProgramsARB) {
        return;
    }

    if (gl_static.prognum_warp) {
        qglDeleteProgramsARB(1, &gl_static.prognum_warp);
        gl_static.prognum_warp = 0;
    }

    QGL_ShutdownExtensions(QGL_ARB_fragment_program);
    gl_config.ext_enabled &= ~QGL_ARB_fragment_program;
#endif
}
