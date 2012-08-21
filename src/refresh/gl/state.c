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

void GL_BindTexture(int texnum)
{
#ifdef _DEBUG
    if (gl_nobind->integer && !gls.tmu) {
        texnum = TEXNUM_DEFAULT;
    }
#endif

    if (gls.texnum[gls.tmu] == texnum) {
        return;
    }

    qglBindTexture(GL_TEXTURE_2D, texnum);
    gls.texnum[gls.tmu] = texnum;

    c.texSwitches++;
}

void GL_SelectTMU(int tmu)
{
    if (gls.tmu == tmu) {
        return;
    }

    if (tmu < 0 || tmu >= gl_config.numTextureUnits) {
        Com_Error(ERR_FATAL, "GL_SelectTMU: bad tmu %d", tmu);
    }

    qglActiveTextureARB(GL_TEXTURE0_ARB + tmu);
    qglClientActiveTextureARB(GL_TEXTURE0_ARB + tmu);

    gls.tmu = tmu;
}

void GL_TexEnv(GLenum texenv)
{
    if (gls.texenv[gls.tmu] == texenv) {
        return;
    }

    switch (texenv) {
    case GL_REPLACE:
    case GL_MODULATE:
    case GL_BLEND:
    case GL_ADD:
        qglTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, texenv);
        break;
    default:
        Com_Error(ERR_FATAL, "GL_TexEnv: bad texenv");
        break;
    }

    gls.texenv[gls.tmu] = texenv;
}

void GL_CullFace(glCullFace_t cull)
{
    if (gls.cull == cull) {
        return;
    }
    switch (cull) {
    case GLS_CULL_DISABLE:
        qglDisable(GL_CULL_FACE);
        break;
    case GLS_CULL_FRONT:
        qglEnable(GL_CULL_FACE);
        qglCullFace(GL_FRONT);
        break;
    case GLS_CULL_BACK:
        qglEnable(GL_CULL_FACE);
        qglCullFace(GL_BACK);
        break;
    default:
        Com_Error(ERR_FATAL, "GL_CullFace: bad cull");
        break;
    }

    gls.cull = cull;
}

void GL_Bits(glStateBits_t bits)
{
    glStateBits_t diff = bits ^ gls.bits;

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

    gls.bits = bits;
}

void GL_Setup2D(void)
{
    qglViewport(0, 0, r_config.width, r_config.height);

    qglMatrixMode(GL_PROJECTION);
    qglLoadIdentity();

    qglOrtho(0, r_config.width, r_config.height, 0, -1, 1);
    draw.scale = 1;

    draw.colors[0].u32 = U32_WHITE;
    draw.colors[1].u32 = U32_WHITE;

    if (draw.flags & DRAW_CLIP_MASK) {
        qglDisable(GL_SCISSOR_TEST);
    }

    draw.flags = 0;

    qglMatrixMode(GL_MODELVIEW);
    qglLoadIdentity();

    GL_Bits(GLS_DEPTHTEST_DISABLE);
    GL_CullFace(GLS_CULL_DISABLE);
}

void GL_Setup3D(void)
{
    GLdouble xmin, xmax, ymin, ymax, zfar;
    int yb = glr.fd.y + glr.fd.height;

    qglViewport(glr.fd.x, r_config.height - yb,
                glr.fd.width, glr.fd.height);

    qglMatrixMode(GL_PROJECTION);
    qglLoadIdentity();

    ymax = gl_znear->value * tan(glr.fd.fov_y * M_PI / 360.0);
    ymin = -ymax;

    xmax = gl_znear->value * tan(glr.fd.fov_x * M_PI / 360.0);
    xmin = -xmax;

    if (glr.fd.rdflags & RDF_NOWORLDMODEL)
        zfar = 2048;
    else
        zfar = gl_static.world.size * 2;

    qglFrustum(xmin, xmax, ymin, ymax, gl_znear->value, zfar);

    qglMatrixMode(GL_MODELVIEW);

    AnglesToAxis(glr.fd.viewangles, glr.viewaxis);

    glr.viewmatrix[0] = -glr.viewaxis[1][0];
    glr.viewmatrix[4] = -glr.viewaxis[1][1];
    glr.viewmatrix[8] = -glr.viewaxis[1][2];
    glr.viewmatrix[12] = DotProduct(glr.viewaxis[1], glr.fd.vieworg);

    glr.viewmatrix[1] = glr.viewaxis[2][0];
    glr.viewmatrix[5] = glr.viewaxis[2][1];
    glr.viewmatrix[9] = glr.viewaxis[2][2];
    glr.viewmatrix[13] = -DotProduct(glr.viewaxis[2], glr.fd.vieworg);

    glr.viewmatrix[2] = -glr.viewaxis[0][0];
    glr.viewmatrix[6] = -glr.viewaxis[0][1];
    glr.viewmatrix[10] = -glr.viewaxis[0][2];
    glr.viewmatrix[14] = DotProduct(glr.viewaxis[0], glr.fd.vieworg);

    glr.viewmatrix[3] = 0;
    glr.viewmatrix[7] = 0;
    glr.viewmatrix[11] = 0;
    glr.viewmatrix[15] = 1;

    qglLoadMatrixf(glr.viewmatrix);

    GL_Bits(GLS_DEFAULT);
    GL_CullFace(GLS_CULL_BACK);

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
    qglFrontFace(GL_CW);
    qglPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    qglActiveTextureARB(GL_TEXTURE1_ARB);
    qglClientActiveTextureARB(GL_TEXTURE1_ARB);
    qglBindTexture(GL_TEXTURE_2D, 0);
    qglDisable(GL_TEXTURE_2D);
    qglDisableClientState(GL_TEXTURE_COORD_ARRAY);

    qglActiveTextureARB(GL_TEXTURE0_ARB);
    qglClientActiveTextureARB(GL_TEXTURE0_ARB);
    qglBindTexture(GL_TEXTURE_2D, 0);
    qglEnable(GL_TEXTURE_2D);
    qglEnableClientState(GL_TEXTURE_COORD_ARRAY);

    qglEnableClientState(GL_VERTEX_ARRAY);
    qglDisableClientState(GL_COLOR_ARRAY);

    qglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | gl_static.stencil_buffer_bit);

    memset(&gls, 0, sizeof(gls));
}

// for screenshots
byte *IMG_ReadPixels(qboolean reverse, int *width, int *height)
{
    byte *pixels;

    pixels = FS_AllocTempMem(r_config.width * r_config.height * 3);

    qglReadPixels(0, 0, r_config.width, r_config.height,
                  reverse ? GL_BGR : GL_RGB, GL_UNSIGNED_BYTE, pixels);

    *width = r_config.width;
    *height = r_config.height;

    return pixels;
}

void GL_EnableOutlines(void)
{
    if (gls.fp_enabled) {
        qglDisable(GL_FRAGMENT_PROGRAM_ARB);
    }
    qglDisable(GL_TEXTURE_2D);
    qglDisableClientState(GL_TEXTURE_COORD_ARRAY);
    qglPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    qglDepthRange(0, 0);
    qglColor4f(1, 1, 1, 1);
}

void GL_DisableOutlines(void)
{
    qglDepthRange(0, 1);
    qglPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    qglEnableClientState(GL_TEXTURE_COORD_ARRAY);
    qglEnable(GL_TEXTURE_2D);
    if (gls.fp_enabled) {
        qglEnable(GL_FRAGMENT_PROGRAM_ARB);
    }
}

void GL_EnableWarp(void)
{
    vec4_t param;

    qglEnable(GL_FRAGMENT_PROGRAM_ARB);
    qglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, gl_static.prognum_warp);
    param[0] = glr.fd.time;
    param[1] = glr.fd.time;
    param[2] = param[3] = 0;
    qglProgramLocalParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB, 0, param);
    gls.fp_enabled = qtrue;
}

void GL_DisableWarp(void)
{
    qglBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 0);
    qglDisable(GL_FRAGMENT_PROGRAM_ARB);
    gls.fp_enabled = qfalse;
}

void GL_InitPrograms(void)
{
    GLuint prog;

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

    if (!qglProgramStringARB) {
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
}

void GL_ShutdownPrograms(void)
{
    if (!qglDeleteProgramsARB) {
        return;
    }

    if (gl_static.prognum_warp) {
        qglDeleteProgramsARB(1, &gl_static.prognum_warp);
        gl_static.prognum_warp = 0;
    }

    QGL_ShutdownExtensions(QGL_ARB_fragment_program);
    gl_config.ext_enabled &= ~QGL_ARB_fragment_program;
}

