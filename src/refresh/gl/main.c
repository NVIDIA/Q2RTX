/*
Copyright (C) 2003-2006 Andrey Nazarov
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.

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

/*
 * gl_main.c
 *
 */

#include "gl.h"

glRefdef_t glr;
glStatic_t gl_static;
glConfig_t gl_config;
statCounters_t  c;

entity_t gl_world;

// regular variables
cvar_t *gl_partscale;
cvar_t *gl_partstyle;
cvar_t *gl_celshading;
cvar_t *gl_dotshading;
cvar_t *gl_shadows;
cvar_t *gl_modulate;
cvar_t *gl_modulate_world;
cvar_t *gl_coloredlightmaps;
cvar_t *gl_brightness;
cvar_t *gl_dynamic;
cvar_t *gl_dlight_falloff;
cvar_t *gl_modulate_entities;
cvar_t *gl_doublelight_entities;
cvar_t *gl_fontshadow;
cvar_t *gl_shaders;
cvar_t *gl_use_hd_assets;
cvar_t *gl_waterwarp;
cvar_t *vid_vsync;

// development variables
cvar_t *gl_znear;
cvar_t *gl_drawworld;
cvar_t *gl_drawentities;
cvar_t *gl_drawsky;
cvar_t *gl_showtris;
cvar_t *gl_showorigins;
cvar_t *gl_showtearing;
#if USE_DEBUG
cvar_t *gl_showstats;
cvar_t *gl_showscrap;
cvar_t *gl_nobind;
cvar_t *gl_test;
#endif
cvar_t *gl_cull_nodes;
cvar_t *gl_cull_models;
cvar_t *gl_clear;
cvar_t *gl_finish;
cvar_t *gl_hash_faces;
cvar_t *gl_novis;
cvar_t *gl_lockpvs;
cvar_t *gl_lightmap;
cvar_t *gl_fullbright;
cvar_t *gl_vertexlight;
cvar_t *gl_polyblend;
cvar_t *gl_showerrors;

// ==============================================================================

static const vec_t quad_tc[8] = { 0, 1, 0, 0, 1, 1, 1, 0 };

static void GL_SetupFrustum(void)
{
    vec_t angle, sf, cf;
    vec3_t forward, left, up;
    cplane_t *p;
    int i;

    // right/left
    angle = DEG2RAD(glr.fd.fov_x / 2);
    sf = sin(angle);
    cf = cos(angle);

    VectorScale(glr.viewaxis[0], sf, forward);
    VectorScale(glr.viewaxis[1], cf, left);

    VectorAdd(forward, left, glr.frustumPlanes[0].normal);
    VectorSubtract(forward, left, glr.frustumPlanes[1].normal);

    // top/bottom
    angle = DEG2RAD(glr.fd.fov_y / 2);
    sf = sin(angle);
    cf = cos(angle);

    VectorScale(glr.viewaxis[0], sf, forward);
    VectorScale(glr.viewaxis[2], cf, up);

    VectorAdd(forward, up, glr.frustumPlanes[2].normal);
    VectorSubtract(forward, up, glr.frustumPlanes[3].normal);

    for (i = 0, p = glr.frustumPlanes; i < 4; i++, p++) {
        p->dist = DotProduct(glr.fd.vieworg, p->normal);
        p->type = PLANE_NON_AXIAL;
        SetPlaneSignbits(p);
    }
}

glCullResult_t GL_CullBox(const vec3_t bounds[2])
{
    int i, bits;
    glCullResult_t cull;

    if (!gl_cull_models->integer) {
        return CULL_IN;
    }

    cull = CULL_IN;
    for (i = 0; i < 4; i++) {
        bits = BoxOnPlaneSide(bounds[0], bounds[1], &glr.frustumPlanes[i]);
        if (bits == BOX_BEHIND) {
            return CULL_OUT;
        }
        if (bits != BOX_INFRONT) {
            cull = CULL_CLIP;
        }
    }

    return cull;
}

glCullResult_t GL_CullSphere(const vec3_t origin, float radius)
{
    float dist;
    cplane_t *p;
    int i;
    glCullResult_t cull;

    if (!gl_cull_models->integer) {
        return CULL_IN;
    }

    radius *= glr.entscale;
    cull = CULL_IN;
    for (i = 0, p = glr.frustumPlanes; i < 4; i++, p++) {
        dist = PlaneDiff(origin, p);
        if (dist < -radius) {
            return CULL_OUT;
        }
        if (dist <= radius) {
            cull = CULL_CLIP;
        }
    }

    return cull;
}

glCullResult_t GL_CullLocalBox(const vec3_t origin, const vec3_t bounds[2])
{
    vec3_t points[8];
    cplane_t *p;
    int i, j;
    vec_t dot;
    bool infront;
    glCullResult_t cull;

    if (!gl_cull_models->integer) {
        return CULL_IN;
    }

    for (i = 0; i < 8; i++) {
        VectorCopy(origin, points[i]);
        VectorMA(points[i], bounds[(i >> 0) & 1][0], glr.entaxis[0], points[i]);
        VectorMA(points[i], bounds[(i >> 1) & 1][1], glr.entaxis[1], points[i]);
        VectorMA(points[i], bounds[(i >> 2) & 1][2], glr.entaxis[2], points[i]);
    }

    cull = CULL_IN;
    for (i = 0, p = glr.frustumPlanes; i < 4; i++, p++) {
        infront = false;
        for (j = 0; j < 8; j++) {
            dot = DotProduct(points[j], p->normal);
            if (dot >= p->dist) {
                infront = true;
                if (cull == CULL_CLIP) {
                    break;
                }
            } else {
                cull = CULL_CLIP;
                if (infront) {
                    break;
                }
            }
        }
        if (!infront) {
            return CULL_OUT;
        }
    }

    return cull;
}

// shared between lightmap and scrap allocators
bool GL_AllocBlock(int width, int height, int *inuse,
                   int w, int h, int *s, int *t)
{
    int i, j, k, x, y, max_inuse, min_inuse;

    x = 0; y = height;
    min_inuse = height;
    for (i = 0; i < width - w; i++) {
        max_inuse = 0;
        for (j = 0; j < w; j++) {
            k = inuse[i + j];
            if (k >= min_inuse) {
                break;
            }
            if (max_inuse < k) {
                max_inuse = k;
            }
        }
        if (j == w) {
            x = i;
            y = min_inuse = max_inuse;
        }
    }

    if (y + h > height) {
        return false;
    }

    for (i = 0; i < w; i++) {
        inuse[x + i] = y + h;
    }

    *s = x;
    *t = y;
    return true;
}

// P = A * B
void GL_MultMatrix(GLfloat *restrict p, const GLfloat *restrict a, const GLfloat *restrict b)
{
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            p[i * 4 + j] =
                a[0 * 4 + j] * b[i * 4 + 0] +
                a[1 * 4 + j] * b[i * 4 + 1] +
                a[2 * 4 + j] * b[i * 4 + 2] +
                a[3 * 4 + j] * b[i * 4 + 3];
        }
    }
}

void GL_SetEntityAxis(void)
{
    entity_t *e = glr.ent;

    glr.entrotated = false;
    glr.entscale = 1;

    if (VectorEmpty(e->angles)) {
        VectorSet(glr.entaxis[0], 1, 0, 0);
        VectorSet(glr.entaxis[1], 0, 1, 0);
        VectorSet(glr.entaxis[2], 0, 0, 1);
    } else {
        AnglesToAxis(e->angles, glr.entaxis);
        glr.entrotated = true;
    }

    if (e->scale && e->scale != 1) {
        VectorScale(glr.entaxis[0], e->scale, glr.entaxis[0]);
        VectorScale(glr.entaxis[1], e->scale, glr.entaxis[1]);
        VectorScale(glr.entaxis[2], e->scale, glr.entaxis[2]);
        glr.entrotated = true;
        glr.entscale = e->scale;
    }
}

void GL_RotationMatrix(GLfloat *matrix)
{
    matrix[0] = glr.entaxis[0][0];
    matrix[4] = glr.entaxis[1][0];
    matrix[8] = glr.entaxis[2][0];
    matrix[12] = glr.ent->origin[0];

    matrix[1] = glr.entaxis[0][1];
    matrix[5] = glr.entaxis[1][1];
    matrix[9] = glr.entaxis[2][1];
    matrix[13] = glr.ent->origin[1];

    matrix[2] = glr.entaxis[0][2];
    matrix[6] = glr.entaxis[1][2];
    matrix[10] = glr.entaxis[2][2];
    matrix[14] = glr.ent->origin[2];

    matrix[3] = 0;
    matrix[7] = 0;
    matrix[11] = 0;
    matrix[15] = 1;
}

void GL_RotateForEntity(void)
{
    GLfloat matrix[16];

    GL_RotationMatrix(matrix);
    GL_MultMatrix(glr.entmatrix, glr.viewmatrix, matrix);
    GL_ForceMatrix(glr.entmatrix);
}

static void GL_DrawSpriteModel(const model_t *model)
{
    const entity_t *e = glr.ent;
    const mspriteframe_t *frame = &model->spriteframes[(unsigned)e->frame % model->numframes];
    const image_t *image = frame->image;
    const float alpha = (e->flags & RF_TRANSLUCENT) ? e->alpha : 1;
    int bits = GLS_DEPTHMASK_FALSE;
    vec3_t up, down, left, right;
    vec3_t points[4];

	const vec3_t world_y = { 0.f, 0.f, 1.f };

    if (alpha == 1) {
        if (image->flags & IF_TRANSPARENT) {
            if (image->flags & IF_PALETTED) {
                bits |= GLS_ALPHATEST_ENABLE;
            } else {
                bits |= GLS_BLEND_BLEND;
            }
        }
    } else {
        bits |= GLS_BLEND_BLEND;
    }

    GL_LoadMatrix(glr.viewmatrix);
    GL_BindTexture(0, image->texnum);
    GL_StateBits(bits);
    GL_ArrayBits(GLA_VERTEX | GLA_TC);
    GL_Color(1, 1, 1, alpha);

    VectorScale(glr.viewaxis[1], frame->origin_x, left);
    VectorScale(glr.viewaxis[1], frame->origin_x - frame->width, right);

	if (model->sprite_vertical)
	{
		VectorScale(world_y, -frame->origin_y, down);
		VectorScale(world_y, frame->height - frame->origin_y, up);
	}
	else
	{
		VectorScale(glr.viewaxis[2], -frame->origin_y, down);
		VectorScale(glr.viewaxis[2], frame->height - frame->origin_y, up);
	}

    VectorAdd3(e->origin, down, left, points[0]);
    VectorAdd3(e->origin, up, left, points[1]);
    VectorAdd3(e->origin, down, right, points[2]);
    VectorAdd3(e->origin, up, right, points[3]);

    GL_TexCoordPointer(2, 0, quad_tc);
    GL_VertexPointer(3, 0, &points[0][0]);
    qglDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void GL_DrawNullModel(void)
{
    static const uint32_t colors[6] = {
        U32_RED, U32_RED,
        U32_GREEN, U32_GREEN,
        U32_BLUE, U32_BLUE
    };
    const entity_t *e = glr.ent;
    vec3_t points[6];

    VectorCopy(e->origin, points[0]);
    VectorCopy(e->origin, points[2]);
    VectorCopy(e->origin, points[4]);

    VectorMA(e->origin, 16, glr.entaxis[0], points[1]);
    VectorMA(e->origin, 16, glr.entaxis[1], points[3]);
    VectorMA(e->origin, 16, glr.entaxis[2], points[5]);

    GL_LoadMatrix(glr.viewmatrix);
    GL_BindTexture(0, TEXNUM_WHITE);
    GL_StateBits(GLS_DEFAULT);
    GL_ArrayBits(GLA_VERTEX | GLA_COLOR);
    GL_ColorBytePointer(4, 0, (GLubyte *)colors);
    GL_VertexPointer(3, 0, &points[0][0]);
    qglDrawArrays(GL_LINES, 0, 6);
}

static void GL_DrawEntities(int mask)
{
    entity_t *ent, *last;
    model_t *model;

    if (!gl_drawentities->integer) {
        return;
    }

    last = glr.fd.entities + glr.fd.num_entities;
    for (ent = glr.fd.entities; ent != last; ent++) {
        if (ent->flags & RF_BEAM) {
            // beams are drawn elsewhere in single batch
            glr.num_beams++;
            continue;
        }
        if ((ent->flags & RF_TRANSLUCENT) != mask) {
            continue;
        }

        glr.ent = ent;

        // convert angles to axis
        GL_SetEntityAxis();

        // inline BSP model
        if (ent->model & BIT(31)) {
            bsp_t *bsp = gl_static.world.cache;
            int index = ~ent->model;

            if (glr.fd.rdflags & RDF_NOWORLDMODEL) {
                Com_Error(ERR_DROP, "%s: inline model without world",
                          __func__);
            }

            if (index < 1 || index >= bsp->nummodels) {
                Com_Error(ERR_DROP, "%s: inline model %d out of range",
                          __func__, index);
            }

            GL_DrawBspModel(&bsp->models[index]);
            continue;
        }

        model = MOD_ForHandle(ent->model);
        if (!model) {
            GL_DrawNullModel();
            continue;
        }

        switch (model->type) {
        case MOD_ALIAS:
            GL_DrawAliasModel(model);
            break;
        case MOD_SPRITE:
            GL_DrawSpriteModel(model);
            break;
        case MOD_EMPTY:
            break;
        default:
            Q_assert(!"bad model type");
        }

        if (gl_showorigins->integer) {
            GL_DrawNullModel();
        }
    }
}

static void GL_DrawTearing(void)
{
    static int i;

    // alternate colors to make tearing obvious
    i++;
    if (i & 1) {
        qglClearColor(1, 1, 1, 1);
    } else {
        qglClearColor(1, 0, 0, 0);
    }

    qglClear(GL_COLOR_BUFFER_BIT);
    qglClearColor(0, 0, 0, 1);
}

static const char *GL_ErrorString(GLenum err)
{
    const char *str;

    switch (err) {
#define E(x) case GL_##x: str = "GL_"#x; break;
        E(NO_ERROR)
        E(INVALID_ENUM)
        E(INVALID_VALUE)
        E(INVALID_OPERATION)
        E(STACK_OVERFLOW)
        E(STACK_UNDERFLOW)
        E(OUT_OF_MEMORY)
    default: str = "UNKNOWN ERROR";
#undef E
    }

    return str;
}

void QGL_ClearErrors(void)
{
    GLenum err;

    while ((err = qglGetError()) != GL_NO_ERROR)
        ;
}

bool GL_ShowErrors(const char *func)
{
    GLenum err = qglGetError();

    if (err == GL_NO_ERROR) {
        return false;
    }

    do {
        if (gl_showerrors->integer) {
            Com_EPrintf("%s: %s\n", func, GL_ErrorString(err));
        }
    } while ((err = qglGetError()) != GL_NO_ERROR);

    return true;
}

static void GL_WaterWarp(void)
{
    GL_ForceTexture(0, gl_static.warp_texture);
    GL_StateBits(GLS_DEPTHTEST_DISABLE | GLS_DEPTHMASK_FALSE |
                 GLS_CULL_DISABLE | GLS_TEXTURE_REPLACE | GLS_WARP_ENABLE);
    GL_ArrayBits(GLA_VERTEX | GLA_TC);

    vec_t points[8] = {
        glr.fd.x,                glr.fd.y,
        glr.fd.x,                glr.fd.y + glr.fd.height,
        glr.fd.x + glr.fd.width, glr.fd.y,
        glr.fd.x + glr.fd.width, glr.fd.y + glr.fd.height,
    };

    GL_TexCoordPointer(2, 0, quad_tc);
    GL_VertexPointer(2, 0, points);
    qglDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void R_RenderFrame_GL(refdef_t *fd)
{
    GL_Flush2D();

    Q_assert(gl_static.world.cache || (fd->rdflags & RDF_NOWORLDMODEL));

    glr.drawframe++;

    glr.fd = *fd;
    glr.num_beams = 0;

    if (gl_dynamic->integer != 1 || gl_vertexlight->integer) {
        glr.fd.num_dlights = 0;
    }

    if (lm.dirty) {
        GL_RebuildLighting();
        lm.dirty = false;
    }

    bool waterwarp = (glr.fd.rdflags & RDF_UNDERWATER) && gl_static.use_shaders && gl_waterwarp->integer;

    if (waterwarp) {
        if (glr.fd.width != glr.framebuffer_width || glr.fd.height != glr.framebuffer_height) {
            glr.framebuffer_ok = GL_InitWarpTexture();
            glr.framebuffer_width = glr.fd.width;
            glr.framebuffer_height = glr.fd.height;
        }
        waterwarp = glr.framebuffer_ok;
    }

    if (waterwarp) {
        qglBindFramebuffer(GL_FRAMEBUFFER, gl_static.warp_framebuffer);
    }

    GL_Setup3D(waterwarp);

    if (gl_cull_nodes->integer) {
        GL_SetupFrustum();
    }

    if (!(glr.fd.rdflags & RDF_NOWORLDMODEL) && gl_drawworld->integer) {
        GL_DrawWorld();
    }

    GL_DrawEntities(0);

    GL_DrawBeams();

    GL_DrawParticles();

    GL_DrawEntities(RF_TRANSLUCENT);

    if (!(glr.fd.rdflags & RDF_NOWORLDMODEL)) {
        GL_DrawAlphaFaces();
    }

    if (waterwarp) {
        qglBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // go back into 2D mode
    GL_Setup2D();

    if (waterwarp) {
        GL_WaterWarp();
    }

    if (gl_polyblend->integer && glr.fd.blend[3] != 0) {
        GL_Blend();
    }

#if USE_DEBUG
    if (gl_lightmap->integer > 1) {
        Draw_Lightmaps();
    }
#endif

    GL_ShowErrors(__func__);
}

void R_BeginFrame_GL(void)
{
    memset(&c, 0, sizeof(c));

    if (gl_finish->integer) {
        qglFinish();
    }

    GL_Setup2D();

    if (gl_clear->integer) {
        qglClear(GL_COLOR_BUFFER_BIT);
    }

    GL_ShowErrors(__func__);
}

void R_EndFrame_GL(void)
{
#if USE_DEBUG
    if (gl_showstats->integer) {
        GL_Flush2D();
        Draw_Stats();
    }
    if (gl_showscrap->integer) {
        Draw_Scrap();
    }
#endif
    GL_Flush2D();

    if (gl_showtearing->integer) {
        GL_DrawTearing();
    }

    GL_ShowErrors(__func__);

    vid.swap_buffers();
}

// ==============================================================================

static void GL_Strings_f(void)
{
    GLint integer = 0;
    GLfloat value = 0;

    Com_Printf("GL_VENDOR: %s\n", qglGetString(GL_VENDOR));
    Com_Printf("GL_RENDERER: %s\n", qglGetString(GL_RENDERER));
    Com_Printf("GL_VERSION: %s\n", qglGetString(GL_VERSION));

    if (gl_config.ver_sl) {
        Com_Printf("GL_SHADING_LANGUAGE_VERSION: %s\n", qglGetString(GL_SHADING_LANGUAGE_VERSION));
    }

    if (Cmd_Argc() > 1) {
        Com_Printf("GL_EXTENSIONS: ");
        if (qglGetStringi) {
            qglGetIntegerv(GL_NUM_EXTENSIONS, &integer);
            for (int i = 0; i < integer; i++)
                Com_Printf("%s ", qglGetStringi(GL_EXTENSIONS, i));
        } else {
            const char *s = (const char *)qglGetString(GL_EXTENSIONS);
            if (s) {
                while (*s) {
                    Com_Printf("%s", s);
                    s += min(strlen(s), MAXPRINTMSG - 1);
                }
            }
        }
        Com_Printf("\n");
    }

    qglGetIntegerv(GL_MAX_TEXTURE_SIZE, &integer);
    Com_Printf("GL_MAX_TEXTURE_SIZE: %d\n", integer);

    if (qglClientActiveTexture) {
        qglGetIntegerv(GL_MAX_TEXTURE_UNITS, &integer);
        Com_Printf("GL_MAX_TEXTURE_UNITS: %d\n", integer);
    }

    if (gl_config.caps & QGL_CAP_TEXTURE_ANISOTROPY) {
        qglGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &value);
        Com_Printf("GL_MAX_TEXTURE_MAX_ANISOTROPY: %.f\n", value);
    }

    Com_Printf("GL_PFD: color(%d-bit) Z(%d-bit) stencil(%d-bit)\n",
               gl_config.colorbits, gl_config.depthbits, gl_config.stencilbits);
}

static size_t GL_ViewCluster_m(char *buffer, size_t size)
{
    return Q_scnprintf(buffer, size, "%d", glr.viewcluster1);
}

static void gl_lightmap_changed(cvar_t *self)
{
    lm.scale = Cvar_ClampValue(gl_coloredlightmaps, 0, 1);
    lm.comp = !(gl_config.caps & QGL_CAP_TEXTURE_BITS) ? GL_RGBA : lm.scale ? GL_RGB : GL_LUMINANCE;
    lm.add = 255 * Cvar_ClampValue(gl_brightness, -1, 1);
    lm.modulate = Cvar_ClampValue(gl_modulate, 0, 1e6);
    lm.modulate *= Cvar_ClampValue(gl_modulate_world, 0, 1e6);
    if (gl_static.use_shaders && (self == gl_brightness || self == gl_modulate || self == gl_modulate_world) && !gl_vertexlight->integer)
        return;
    lm.dirty = true; // rebuild all lightmaps next frame
}

static void gl_modulate_entities_changed(cvar_t *self)
{
    gl_static.entity_modulate = Cvar_ClampValue(gl_modulate, 0, 1e6);
    gl_static.entity_modulate *= Cvar_ClampValue(gl_modulate_entities, 0, 1e6);
}

static void gl_modulate_changed(cvar_t *self)
{
    gl_lightmap_changed(self);
    gl_modulate_entities_changed(self);
}

// ugly hack to reset sky
static void gl_drawsky_changed(cvar_t *self)
{
    if (gl_static.world.cache)
        CL_SetSky();
}

static void gl_novis_changed(cvar_t *self)
{
    glr.viewcluster1 = glr.viewcluster2 = -2;
}

static void vid_vsync_changed(cvar_t *self)
{
    if (vid.swap_interval)
        vid.swap_interval(self->integer);
}

static void GL_Register(void)
{
    // regular variables
    gl_partscale = Cvar_Get("gl_partscale", "2", 0);
    gl_partstyle = Cvar_Get("gl_partstyle", "0", 0);
    gl_celshading = Cvar_Get("gl_celshading", "0", 0);
    gl_dotshading = Cvar_Get("gl_dotshading", "1", 0);
    gl_shadows = Cvar_Get("gl_shadows", "1", CVAR_ARCHIVE);
    gl_modulate = Cvar_Get("gl_modulate", "1", CVAR_ARCHIVE);
    gl_modulate->changed = gl_modulate_changed;
    gl_modulate_world = Cvar_Get("gl_modulate_world", "1", 0);
    gl_modulate_world->changed = gl_lightmap_changed;
    gl_coloredlightmaps = Cvar_Get("gl_coloredlightmaps", "1", 0);
    gl_coloredlightmaps->changed = gl_lightmap_changed;
    gl_brightness = Cvar_Get("gl_brightness", "0", 0);
    gl_brightness->changed = gl_lightmap_changed;
    gl_dynamic = Cvar_Get("gl_dynamic", "1", 0);
    gl_dynamic->changed = gl_lightmap_changed;
    gl_dlight_falloff = Cvar_Get("gl_dlight_falloff", "1", 0);
    gl_modulate_entities = Cvar_Get("gl_modulate_entities", "1", 0);
    gl_modulate_entities->changed = gl_modulate_entities_changed;
    gl_doublelight_entities = Cvar_Get("gl_doublelight_entities", "1", 0);
    gl_fontshadow = Cvar_Get("gl_fontshadow", "0", 0);
    gl_shaders = Cvar_Get("gl_shaders", (gl_config.caps & QGL_CAP_SHADER) ? "1" : "0", CVAR_REFRESH);
    gl_use_hd_assets = Cvar_Get("gl_use_hd_assets", "0", CVAR_FILES);
    gl_waterwarp = Cvar_Get("gl_waterwarp", "0", 0);
    vid_vsync = Cvar_Get("vid_vsync", "0", CVAR_ARCHIVE);
    vid_vsync->changed = vid_vsync_changed;

    // development variables
    gl_znear = Cvar_Get("gl_znear", "2", CVAR_CHEAT);
    gl_drawworld = Cvar_Get("gl_drawworld", "1", CVAR_CHEAT);
    gl_drawentities = Cvar_Get("gl_drawentities", "1", CVAR_CHEAT);
    gl_drawsky = Cvar_Get("gl_drawsky", "1", 0);
    gl_drawsky->changed = gl_drawsky_changed;
    gl_showtris = Cvar_Get("gl_showtris", "0", CVAR_CHEAT);
    gl_showorigins = Cvar_Get("gl_showorigins", "0", CVAR_CHEAT);
    gl_showtearing = Cvar_Get("gl_showtearing", "0", CVAR_CHEAT);
#if USE_DEBUG
    gl_showstats = Cvar_Get("gl_showstats", "0", 0);
    gl_showscrap = Cvar_Get("gl_showscrap", "0", 0);
    gl_nobind = Cvar_Get("gl_nobind", "0", CVAR_CHEAT);
    gl_test = Cvar_Get("gl_test", "0", 0);
#endif
    gl_cull_nodes = Cvar_Get("gl_cull_nodes", "1", 0);
    gl_cull_models = Cvar_Get("gl_cull_models", "1", 0);
    gl_hash_faces = Cvar_Get("gl_hash_faces", "1", 0);
    gl_clear = Cvar_Get("gl_clear", "0", 0);
    gl_finish = Cvar_Get("gl_finish", "0", 0);
    gl_novis = Cvar_Get("gl_novis", "0", 0);
    gl_novis->changed = gl_novis_changed;
    gl_lockpvs = Cvar_Get("gl_lockpvs", "0", CVAR_CHEAT);
    gl_lightmap = Cvar_Get("gl_lightmap", "0", CVAR_CHEAT);
    gl_fullbright = Cvar_Get("r_fullbright", "0", CVAR_CHEAT);
    gl_fullbright->changed = gl_lightmap_changed;
    gl_vertexlight = Cvar_Get("gl_vertexlight", "0", 0);
    gl_vertexlight->changed = gl_lightmap_changed;
    gl_polyblend = Cvar_Get("gl_polyblend", "1", 0);
    gl_showerrors = Cvar_Get("gl_showerrors", "1", 0);

    gl_lightmap_changed(NULL);
    gl_modulate_entities_changed(NULL);
    vid_vsync_changed(vid_vsync);

    Cmd_AddCommand("strings", GL_Strings_f);
    Cmd_AddMacro("gl_viewcluster", GL_ViewCluster_m);
}

static void GL_Unregister(void)
{
    Cmd_RemoveCommand("strings");
}

static void APIENTRY myDebugProc(GLenum source, GLenum type, GLuint id, GLenum severity,
                                 GLsizei length, const GLchar *message, const void *userParam)
{
    int level = PRINT_DEVELOPER;

    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH:   level = PRINT_ERROR;   break;
    case GL_DEBUG_SEVERITY_MEDIUM: level = PRINT_WARNING; break;
    case GL_DEBUG_SEVERITY_LOW:    level = PRINT_ALL;     break;
    }

    Com_LPrintf(level, "%s\n", message);
}

static void GL_SetupConfig(void)
{
    GLint integer = 0;

    gl_config.colorbits = 0;
    qglGetIntegerv(GL_RED_BITS, &integer);
    gl_config.colorbits += integer;
    qglGetIntegerv(GL_GREEN_BITS, &integer);
    gl_config.colorbits += integer;
    qglGetIntegerv(GL_BLUE_BITS, &integer);
    gl_config.colorbits += integer;

    qglGetIntegerv(GL_DEPTH_BITS, &integer);
    gl_config.depthbits = integer;

    qglGetIntegerv(GL_STENCIL_BITS, &integer);
    gl_config.stencilbits = integer;

    if (qglDebugMessageCallback && qglIsEnabled(GL_DEBUG_OUTPUT)) {
        Com_Printf("Enabling GL debug output.\n");
        qglEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        qglDebugMessageCallback(myDebugProc, NULL);
    }
}

static void GL_InitTables(void)
{
    vec_t lat, lng;
    const vec_t *v;
    int i;

    for (i = 0; i < NUMVERTEXNORMALS; i++) {
        v = bytedirs[i];
        lat = acos(v[2]);
        lng = atan2(v[1], v[0]);
        gl_static.latlngtab[i][0] = (int)(lat * (float)(255 / (2 * M_PI))) & 255;
        gl_static.latlngtab[i][1] = (int)(lng * (float)(255 / (2 * M_PI))) & 255;
    }

    for (i = 0; i < 256; i++) {
        gl_static.sintab[i] = sin(i * (2 * M_PI / 255));
    }
}

static void GL_PostInit(void)
{
    registration_sequence = 1;

    GL_ClearState();
    GL_InitImages();
    MOD_Init();
}

// ==============================================================================

/*
===============
R_Init
===============
*/
ref_type_t R_Init_GL(bool total)
{
    Com_DPrintf("GL_Init( %i )\n", total);

    if (!total) {
        GL_PostInit();
        return REF_TYPE_GL;
    }

    Com_Printf("------- R_Init -------\n");

    Com_Printf("Using video driver: %s\n", vid.name);

    // initialize OS-specific parts of OpenGL
    // create the window and set up the context
    if (!vid.init(GAPI_OPENGL)) {
        return REF_TYPE_NONE;
    }

    // initialize our QGL dynamic bindings
    if (!QGL_Init()) {
        goto fail;
    }

    // get various limits from OpenGL
    GL_SetupConfig();

    // register our variables
    GL_Register();

    GL_InitState();

    GL_InitTables();

    GL_PostInit();

    GL_ShowErrors(__func__);

    Com_Printf("----------------------\n");

    return REF_TYPE_GL;

fail:
    memset(&gl_static, 0, sizeof(gl_static));
    memset(&gl_config, 0, sizeof(gl_config));
    QGL_Shutdown();
    vid.shutdown();
    return REF_TYPE_NONE;
}

/*
===============
R_Shutdown
===============
*/
void R_Shutdown_GL(bool total)
{
    Com_DPrintf("GL_Shutdown( %i )\n", total);

    GL_FreeWorld();
    GL_ShutdownImages();
    MOD_Shutdown();

    if (!total) {
        return;
    }

    GL_ShutdownState();

    // shutdown our QGL subsystem
    QGL_Shutdown();

    // shut down OS specific OpenGL stuff like contexts, etc.
    vid.shutdown();

    GL_Unregister();

    memset(&gl_static, 0, sizeof(gl_static));
    memset(&gl_config, 0, sizeof(gl_config));
}

/*
===============
R_GetGLConfig
===============
*/
r_opengl_config_t *R_GetGLConfig(void)
{
    static r_opengl_config_t cfg;

    cfg.colorbits    = Cvar_ClampInteger(Cvar_Get("gl_colorbits",    "0", CVAR_REFRESH), 0, 32);
    cfg.depthbits    = Cvar_ClampInteger(Cvar_Get("gl_depthbits",    "0", CVAR_REFRESH), 0, 32);
    cfg.stencilbits  = Cvar_ClampInteger(Cvar_Get("gl_stencilbits",  "8", CVAR_REFRESH), 0,  8);
    cfg.multisamples = Cvar_ClampInteger(Cvar_Get("gl_multisamples", "0", CVAR_REFRESH), 0, 32);

    if (cfg.colorbits == 0)
        cfg.colorbits = 24;

    if (cfg.depthbits == 0)
        cfg.depthbits = cfg.colorbits > 16 ? 24 : 16;

    if (cfg.depthbits < 24)
        cfg.stencilbits = 0;

    if (cfg.multisamples < 2)
        cfg.multisamples = 0;

    cfg.debug = Cvar_Get("gl_debug", "0", CVAR_REFRESH)->integer;
    return &cfg;
}

/*
===============
R_BeginRegistration
===============
*/
void R_BeginRegistration_GL(const char *name)
{
    char fullname[MAX_QPATH];

    gl_static.registering = true;
    registration_sequence++;

    memset(&glr, 0, sizeof(glr));
    glr.viewcluster1 = glr.viewcluster2 = -2;

    Q_concat(fullname, sizeof(fullname), "maps/", name, ".bsp");
    GL_LoadWorld(fullname);
}

/*
===============
R_EndRegistration
===============
*/
void R_EndRegistration_GL(void)
{
    IMG_FreeUnused();
    MOD_FreeUnused();
    Scrap_Upload();
    gl_static.registering = false;
}

/*
===============
R_ModeChanged
===============
*/
void R_ModeChanged_GL(int width, int height, int flags)
{
    r_config.width = width;
    r_config.height = height;
    r_config.flags = flags;
}

void R_AddDecal_GL(decal_t *d) {}
bool R_InterceptKey_GL(unsigned key, bool down) { return false; }

void R_RegisterFunctionsGL()
{
	R_Init = R_Init_GL;
	R_Shutdown = R_Shutdown_GL;
	R_BeginRegistration = R_BeginRegistration_GL;
	R_EndRegistration = R_EndRegistration_GL;
	R_SetSky = R_SetSky_GL;
	R_RenderFrame = R_RenderFrame_GL;
	R_LightPoint = R_LightPoint_GL;
	R_ClearColor = R_ClearColor_GL;
	R_SetAlpha = R_SetAlpha_GL;
	R_SetAlphaScale = R_SetAlphaScale_GL;
	R_SetColor = R_SetColor_GL;
	R_SetClipRect = R_SetClipRect_GL;
	R_SetScale = R_SetScale_GL;
	R_DrawChar = R_DrawChar_GL;
	R_DrawString = R_DrawString_GL;
	R_DrawPic = R_DrawPic_GL;
	R_DrawStretchPic = R_DrawStretchPic_GL;
	R_DrawKeepAspectPic = R_DrawKeepAspectPic_GL;
	R_DrawStretchRaw = R_DrawStretchRaw_GL;
	R_UpdateRawPic = R_UpdateRawPic_GL;
	R_DiscardRawPic = R_DiscardRawPic_GL;
	R_TileClear = R_TileClear_GL;
	R_DrawFill8 = R_DrawFill8_GL;
	R_DrawFill32 = R_DrawFill32_GL;
	R_BeginFrame = R_BeginFrame_GL;
	R_EndFrame = R_EndFrame_GL;
	R_ModeChanged = R_ModeChanged_GL;
	R_AddDecal = R_AddDecal_GL;
	R_InterceptKey = R_InterceptKey_GL;
	R_IsHDR = NULL;
	IMG_Load = IMG_Load_GL;
	IMG_Unload = IMG_Unload_GL;
	IMG_ReadPixels = IMG_ReadPixels_GL;
	IMG_ReadPixelsHDR = NULL;
	MOD_LoadMD2 = MOD_LoadMD2_GL;
	MOD_LoadMD3 = MOD_LoadMD3_GL;
    MOD_LoadIQM = NULL;
	MOD_Reference = MOD_Reference_GL;
}
