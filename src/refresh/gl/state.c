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
#if USE_DEBUG
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

void GL_CommonStateBits(GLbitfield bits)
{
    GLbitfield diff = bits ^ gls.state_bits;

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

    if (diff & GLS_CULL_DISABLE) {
        if (bits & GLS_CULL_DISABLE) {
            qglDisable(GL_CULL_FACE);
        } else {
            qglEnable(GL_CULL_FACE);
        }
    }
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

    gl_static.backend.proj_matrix(matrix);
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
        draw.scissor = false;
    }

    gl_static.backend.view_matrix(NULL);
}

void GL_Frustum(GLfloat fov_x, GLfloat fov_y, GLfloat reflect_x)
{
    GLfloat xmin, xmax, ymin, ymax, zfar, znear;
    GLfloat width, height, depth;
    GLfloat matrix[16];

    znear = gl_znear->value;

    if (glr.fd.rdflags & RDF_NOWORLDMODEL)
        zfar = 2048;
    else
        zfar = gl_static.world.size * 2;

    xmax = znear * tan(fov_x * (M_PI / 360));
    xmin = -xmax;

    ymax = znear * tan(fov_y * (M_PI / 360));
    ymin = -ymax;

    width = xmax - xmin;
    height = ymax - ymin;
    depth = zfar - znear;

    matrix[0] = reflect_x * 2 * znear / width;
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

    gl_static.backend.proj_matrix(matrix);
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

    GL_ForceMatrix(matrix);
}

void GL_Setup3D(void)
{
    qglViewport(glr.fd.x, r_config.height - (glr.fd.y + glr.fd.height),
                glr.fd.width, glr.fd.height);

    if (gl_static.backend.update)
        gl_static.backend.update();

    GL_Frustum(glr.fd.fov_x, glr.fd.fov_y, 1.0f);

    GL_RotateForViewer();

    // enable depth writes before clearing
    GL_StateBits(GLS_DEFAULT);

    qglClear(GL_DEPTH_BUFFER_BIT | gl_static.stencil_buffer_bit);
}

void GL_DrawOutlines(GLsizei count, QGL_INDEX_TYPE *indices)
{
    GL_BindTexture(0, TEXNUM_WHITE);
    GL_StateBits(GLS_DEFAULT);
    GL_ArrayBits(GLA_VERTEX);
    GL_Color(1, 1, 1, 1);
    GL_DepthRange(0, 0);

    if (qglPolygonMode) {
        qglPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        if (indices)
            qglDrawElements(GL_TRIANGLES, count, QGL_INDEX_ENUM, indices);
        else
            qglDrawArrays(GL_TRIANGLES, 0, count);

        qglPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    } else {
        GLsizei i;

        if (indices) {
            for (i = 0; i < count / 3; i++)
                qglDrawElements(GL_LINE_LOOP, 3, QGL_INDEX_ENUM, &indices[i * 3]);
        } else {
            for (i = 0; i < count / 3; i++)
                qglDrawArrays(GL_LINE_LOOP, i * 3, 3);
        }
    }

    GL_DepthRange(0, 1);
}

void GL_ClearState(void)
{
    qglClearColor(0, 0, 0, 1);
    GL_ClearDepth(1);
    qglClearStencil(0);

    qglEnable(GL_DEPTH_TEST);
    qglDepthFunc(GL_LEQUAL);
    GL_DepthRange(0, 1);
    qglDepthMask(GL_TRUE);
    qglDisable(GL_BLEND);
    qglFrontFace(GL_CW);
    qglCullFace(GL_BACK);
    qglEnable(GL_CULL_FACE);
    
    gl_static.backend.clear();

    qglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | gl_static.stencil_buffer_bit);

    memset(&gls, 0, sizeof(gls));
    GL_ShowErrors(__func__);
}

void GL_InitState(void)
{
    gl_static.use_shaders = gl_shaders->integer > 0;
 
    if (gl_static.use_shaders) {
        if (!(gl_config.caps & QGL_CAP_SHADER)) {
            Com_Printf("GLSL rendering backend not available.\n");
            gl_static.use_shaders = false;
            Cvar_Set("gl_shaders", "0");
        }
    } else {
        if (!(gl_config.caps & QGL_CAP_LEGACY)) {
            Com_Printf("Legacy rendering backend not available.\n");
            gl_static.use_shaders = true;
            Cvar_Set("gl_shaders", "1");
        }
    }

    gl_static.backend = gl_static.use_shaders ? backend_shader : backend_legacy;
    gl_static.backend.init();

    Com_Printf("Using %s rendering backend.\n", gl_static.backend.name);
}

void GL_ShutdownState(void)
{
    gl_static.backend.shutdown();
}
