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

refcfg_t r_config;

int registration_sequence;

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
#if USE_DLIGHTS
cvar_t *gl_dlight_falloff;
#endif
cvar_t *gl_modulate_entities;
cvar_t *gl_doublelight_entities;
cvar_t *gl_fragment_program;
cvar_t *gl_vertex_buffer_object;
cvar_t *gl_fontshadow;

// development variables
cvar_t *gl_znear;
cvar_t *gl_drawworld;
cvar_t *gl_drawentities;
cvar_t *gl_drawsky;
cvar_t *gl_showtris;
cvar_t *gl_showorigins;
cvar_t *gl_showtearing;
#ifdef _DEBUG
cvar_t *gl_log;
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

glCullResult_t GL_CullBox(vec3_t bounds[2])
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

static inline void make_box_points(const vec3_t    origin,
                                   vec3_t    bounds[2],
                                   vec3_t    points[8])
{
    int i;

    for (i = 0; i < 8; i++) {
        VectorCopy(origin, points[i]);
        VectorMA(points[i], bounds[(i >> 0) & 1][0], glr.entaxis[0], points[i]);
        VectorMA(points[i], bounds[(i >> 1) & 1][1], glr.entaxis[1], points[i]);
        VectorMA(points[i], bounds[(i >> 2) & 1][2], glr.entaxis[2], points[i]);
    }

}

glCullResult_t GL_CullLocalBox(const vec3_t origin, vec3_t bounds[2])
{
    vec3_t points[8];
    cplane_t *p;
    int i, j;
    vec_t dot;
    qboolean infront;
    glCullResult_t cull;

    if (!gl_cull_models->integer) {
        return CULL_IN;
    }

    make_box_points(origin, bounds, points);

    cull = CULL_IN;
    for (i = 0, p = glr.frustumPlanes; i < 4; i++, p++) {
        infront = qfalse;
        for (j = 0; j < 8; j++) {
            dot = DotProduct(points[j], p->normal);
            if (dot >= p->dist) {
                infront = qtrue;
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

#if 0
void GL_DrawBox(const vec3_t origin, vec3_t bounds[2])
{
    static const int indices1[4] = { 0, 1, 3, 2 };
    static const int indices2[4] = { 4, 5, 7, 6 };
    static const int indices3[8] = { 0, 4, 1, 5, 2, 6, 3, 7 };
    vec3_t points[8];

    qglDisable(GL_TEXTURE_2D);
    qglDisable(GL_DEPTH_TEST);
    qglDisableClientState(GL_TEXTURE_COORD_ARRAY);
    qglColor4f(1, 1, 1, 1);

    make_box_points(origin, bounds, points);

    qglVertexPointer(3, GL_FLOAT, 0, points);
    qglDrawElements(GL_LINE_LOOP, 4, GL_UNSIGNED_INT, indices1);
    qglDrawElements(GL_LINE_LOOP, 4, GL_UNSIGNED_INT, indices2);
    qglDrawElements(GL_LINES, 8, GL_UNSIGNED_INT, indices3);

    qglEnableClientState(GL_TEXTURE_COORD_ARRAY);
    qglEnable(GL_DEPTH_TEST);
    qglEnable(GL_TEXTURE_2D);
}
#endif

// shared between lightmap and scrap allocators
qboolean GL_AllocBlock(int width, int height, int *inuse,
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
        return qfalse;
    }

    for (i = 0; i < w; i++) {
        inuse[x + i] = y + h;
    }

    *s = x;
    *t = y;
    return qtrue;
}

// P = A * B
void GL_MultMatrix(GLfloat *p, const GLfloat *a, const GLfloat *b)
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

void GL_RotateForEntity(vec3_t origin)
{
    GLfloat matrix[16];

    matrix[0] = glr.entaxis[0][0];
    matrix[4] = glr.entaxis[1][0];
    matrix[8] = glr.entaxis[2][0];
    matrix[12] = origin[0];

    matrix[1] = glr.entaxis[0][1];
    matrix[5] = glr.entaxis[1][1];
    matrix[9] = glr.entaxis[2][1];
    matrix[13] = origin[1];

    matrix[2] = glr.entaxis[0][2];
    matrix[6] = glr.entaxis[1][2];
    matrix[10] = glr.entaxis[2][2];
    matrix[14] = origin[2];

    matrix[3] = 0;
    matrix[7] = 0;
    matrix[11] = 0;
    matrix[15] = 1;

    GL_MultMatrix(glr.entmatrix, glr.viewmatrix, matrix);
    qglLoadMatrixf(glr.entmatrix);

    // forced matrix upload
    gls.currentmatrix = glr.entmatrix;
}

static void GL_DrawSpriteModel(model_t *model)
{
    static const vec_t tcoords[8] = { 0, 1, 0, 0, 1, 1, 1, 0 };
    entity_t *e = glr.ent;
    mspriteframe_t *frame = &model->spriteframes[e->frame % model->numframes];
    image_t *image = frame->image;
    float alpha = (e->flags & RF_TRANSLUCENT) ? e->alpha : 1;
    int bits = GLS_DEPTHMASK_FALSE;
    vec3_t up, down, left, right;
    vec3_t points[4];

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
    qglColor4f(1, 1, 1, alpha);

    VectorScale(glr.viewaxis[1], frame->origin_x, left);
    VectorScale(glr.viewaxis[1], frame->origin_x - frame->width, right);
    VectorScale(glr.viewaxis[2], -frame->origin_y, down);
    VectorScale(glr.viewaxis[2], frame->height - frame->origin_y, up);

    VectorAdd3(e->origin, down, left, points[0]);
    VectorAdd3(e->origin, up, left, points[1]);
    VectorAdd3(e->origin, down, right, points[2]);
    VectorAdd3(e->origin, up, right, points[3]);

    GL_TexCoordPointer(2, 0, tcoords);
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
    entity_t *e = glr.ent;
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
        if (VectorEmpty(ent->angles)) {
            glr.entrotated = qfalse;
            VectorSet(glr.entaxis[0], 1, 0, 0);
            VectorSet(glr.entaxis[1], 0, 1, 0);
            VectorSet(glr.entaxis[2], 0, 0, 1);
        } else {
            glr.entrotated = qtrue;
            AnglesToAxis(ent->angles, glr.entaxis);
        }

        // inline BSP model
        if (ent->model & 0x80000000) {
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
            Com_Error(ERR_FATAL, "%s: bad model type", __func__);
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

void GL_ClearErrors(void)
{
    GLenum err;

    while ((err = qglGetError()) != GL_NO_ERROR)
        ;
}

qboolean GL_ShowErrors(const char *func)
{
    GLenum err = qglGetError();

    if (err == GL_NO_ERROR) {
        return qfalse;
    }

    do {
        if (gl_showerrors->integer) {
            Com_EPrintf("%s: %s\n", func, GL_ErrorString(err));
        }
    } while ((err = qglGetError()) != GL_NO_ERROR);

    return qtrue;
}

void R_RenderFrame(refdef_t *fd)
{
    GL_Flush2D();

    if (!gl_static.world.cache && !(fd->rdflags & RDF_NOWORLDMODEL)) {
        Com_Error(ERR_FATAL, "%s: NULL worldmodel", __func__);
    }

    glr.drawframe++;

    glr.fd = *fd;
    glr.num_beams = 0;

#if USE_DLIGHTS
    if (gl_dynamic->integer != 1 || gl_vertexlight->integer) {
        glr.fd.num_dlights = 0;
    }
#endif

    if (lm.dirty) {
        GL_RebuildLighting();
        lm.dirty = qfalse;
    }

    GL_Setup3D();

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

    // go back into 2D mode
    GL_Setup2D();

    if (gl_polyblend->integer && glr.fd.blend[3] != 0) {
        GL_Blend();
    }

#ifdef _DEBUG
    if (gl_lightmap->integer > 1) {
        Draw_Lightmaps();
    }
#endif

    GL_ShowErrors(__func__);
}

void R_BeginFrame(void)
{
#ifdef _DEBUG
    if (gl_log->integer) {
        QGL_LogComment("\n*** R_BeginFrame ***\n");
    }
#endif

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

void R_EndFrame(void)
{
#ifdef _DEBUG
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

    // enable/disable fragment programs on the fly
    if (gl_fragment_program->modified) {
        GL_ShutdownPrograms();
        GL_InitPrograms();
        gl_fragment_program->modified = qfalse;
    }

    GL_ShowErrors(__func__);

#ifdef _DEBUG
    if (gl_log->modified) {
        if (gl_log->integer)
            QGL_EnableLogging(gl_config.ext_enabled);
        else
            QGL_DisableLogging(gl_config.ext_enabled);
        gl_log->modified = qfalse;
    }
#endif

    VID_EndFrame();
}

// ==============================================================================

static void GL_Strings_f(void)
{
    Com_Printf("GL_VENDOR: %s\n", qglGetString(GL_VENDOR));
    Com_Printf("GL_RENDERER: %s\n", qglGetString(GL_RENDERER));
    Com_Printf("GL_VERSION: %s\n", qglGetString(GL_VERSION));
    Com_Printf("GL_EXTENSIONS: %s\n", qglGetString(GL_EXTENSIONS));
    Com_Printf("GL_MAX_TEXTURE_SIZE: %d\n", gl_config.maxTextureSize);
    Com_Printf("GL_MAX_TEXTURE_UNITS: %d\n", gl_config.numTextureUnits);
    Com_Printf("GL_MAX_TEXTURE_MAX_ANISOTROPY: %.f\n", gl_config.maxAnisotropy);
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
    if (AT_LEAST_OPENGL_ES(1, 0))
        lm.comp = GL_RGBA; // ES doesn't support internal format != external
    else
        lm.comp = lm.scale ? GL_RGB : GL_LUMINANCE;
    lm.add = 255 * Cvar_ClampValue(gl_brightness, -1, 1);
    lm.modulate = gl_modulate->value * gl_modulate_world->value;
    lm.dirty = qtrue; // rebuild all lightmaps next frame
}

static void gl_modulate_entities_changed(cvar_t *self)
{
    gl_static.entity_modulate = gl_modulate->value * gl_modulate_entities->value;
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

static void GL_Register(void)
{
    // regular variables
    gl_partscale = Cvar_Get("gl_partscale", "2", 0);
    gl_partstyle = Cvar_Get("gl_partstyle", "0", 0);
    gl_celshading = Cvar_Get("gl_celshading", "0", 0);
    gl_dotshading = Cvar_Get("gl_dotshading", "1", 0);
    gl_shadows = Cvar_Get("gl_shadows", "0", CVAR_ARCHIVE);
    gl_modulate = Cvar_Get("gl_modulate", "1", CVAR_ARCHIVE);
    gl_modulate->changed = gl_modulate_changed;
    gl_modulate_world = Cvar_Get("gl_modulate_world", "1", 0);
    gl_modulate_world->changed = gl_lightmap_changed;
    gl_coloredlightmaps = Cvar_Get("gl_coloredlightmaps", "1", 0);
    gl_coloredlightmaps->changed = gl_lightmap_changed;
    gl_brightness = Cvar_Get("gl_brightness", "0", 0);
    gl_brightness->changed = gl_lightmap_changed;
    gl_dynamic = Cvar_Get("gl_dynamic", "2", 0);
    gl_dynamic->changed = gl_lightmap_changed;
#if USE_DLIGHTS
    gl_dlight_falloff = Cvar_Get("gl_dlight_falloff", "1", 0);
#endif
    gl_modulate_entities = Cvar_Get("gl_modulate_entities", "1", 0);
    gl_modulate_entities->changed = gl_modulate_entities_changed;
    gl_doublelight_entities = Cvar_Get("gl_doublelight_entities", "1", 0);
    gl_fragment_program = Cvar_Get("gl_fragment_program", "1", 0);
    gl_vertex_buffer_object = Cvar_Get("gl_vertex_buffer_object", "1", CVAR_FILES);
    gl_vertex_buffer_object->modified = qtrue;
    gl_fontshadow = Cvar_Get("gl_fontshadow", "0", 0);

    // development variables
    gl_znear = Cvar_Get("gl_znear", "2", CVAR_CHEAT);
    gl_drawworld = Cvar_Get("gl_drawworld", "1", CVAR_CHEAT);
    gl_drawentities = Cvar_Get("gl_drawentities", "1", CVAR_CHEAT);
    gl_drawsky = Cvar_Get("gl_drawsky", "1", 0);
    gl_drawsky->changed = gl_drawsky_changed;
    gl_showtris = Cvar_Get("gl_showtris", "0", CVAR_CHEAT);
    gl_showorigins = Cvar_Get("gl_showorigins", "0", CVAR_CHEAT);
    gl_showtearing = Cvar_Get("gl_showtearing", "0", 0);
#ifdef _DEBUG
    gl_log = Cvar_Get("gl_log", "0", CVAR_CHEAT);
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

    Cmd_AddCommand("strings", GL_Strings_f);
    Cmd_AddMacro("gl_viewcluster", GL_ViewCluster_m);
}

static void GL_Unregister(void)
{
    Cmd_RemoveCommand("strings");
}

static qboolean GL_SetupConfig(void)
{
    const char *version, *extensions;
    GLint integer;
    GLfloat value;
    char *p;

    // get version string
    version = (const char *)qglGetString(GL_VERSION);
    if (!version || !*version) {
        Com_EPrintf("OpenGL returned NULL version string\n");
        return qfalse;
    }

    // parse ES profile prefix
    if (!strncmp(version, "OpenGL ES", 9)) {
        version += 9;
        if (version[0] == '-' && version[1] && version[2] && version[3] == ' ') {
            version += 4;
        } else if (version[0] == ' ') {
            version += 1;
        } else {
            Com_EPrintf("OpenGL returned invalid version string\n");
            return qfalse;
        }
        gl_config.es_profile = qtrue;
    } else {
        gl_config.es_profile = qfalse;
    }

    // parse version
    gl_config.version_major = strtoul(version, &p, 10);
    if (*p == '.') {
        gl_config.version_minor = strtoul(p + 1, NULL, 10);
    } else {
        gl_config.version_minor = 0;
    }

    if (gl_config.version_major < 1) {
        Com_EPrintf("OpenGL returned invalid version string\n");
        return qfalse;
    }

    // OpenGL 1.0 doesn't have vertex arrays
    if (!AT_LEAST_OPENGL(1, 1) && !AT_LEAST_OPENGL_ES(1, 0)) {
        Com_EPrintf("OpenGL version 1.1 or higher required\n");
        return qfalse;
    }

    // allow version override for debugging purposes
    p = Cvar_Get("gl_versionoverride", "", CVAR_REFRESH)->string;
    if (*p) {
        gl_config.version_major = strtoul(p, &p, 10);
        if (*p == '.')
            gl_config.version_minor = strtoul(p + 1, NULL, 10);
        else
            gl_config.version_minor = 0;
    }

    // get and parse extension string
    extensions = (const char *)qglGetString(GL_EXTENSIONS);
    gl_config.ext_supported = QGL_ParseExtensionString(extensions);
    gl_config.ext_enabled = 0;

    // initialize our 'always on' extensions
    if (gl_config.ext_supported & QGL_EXT_compiled_vertex_array) {
        Com_Printf("...enabling GL_EXT_compiled_vertex_array\n");
        gl_config.ext_enabled |= QGL_EXT_compiled_vertex_array;
    } else {
        Com_Printf("GL_EXT_compiled_vertex_array not found\n");
    }

    gl_config.numTextureUnits = 1;
    if (gl_config.ext_supported & QGL_ARB_multitexture) {
        qglGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &integer);
        if (integer >= 2) {
            Com_Printf("...enabling GL_ARB_multitexture (%d TMUs)\n", integer);
            gl_config.ext_enabled |= QGL_ARB_multitexture;
            if (integer > MAX_TMUS) {
                integer = MAX_TMUS;
            }
            gl_config.numTextureUnits = integer;
        } else {
            Com_Printf("...ignoring GL_ARB_multitexture,\n"
                       "%d TMU is not enough\n", integer);
        }
    } else {
        Com_Printf("GL_ARB_multitexture not found\n");
    }

    gl_config.maxAnisotropy = 1;
    if (gl_config.ext_supported & QGL_EXT_texture_filter_anisotropic) {
        qglGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &value);
        if (value >= 2) {
            Com_Printf("...enabling GL_EXT_texture_filter_anisotropic (%.f max)\n", value);
            gl_config.ext_enabled |= QGL_EXT_texture_filter_anisotropic;
            gl_config.maxAnisotropy = value;
        } else {
            Com_Printf("...ignoring GL_EXT_texture_filter_anisotropic,\n"
                       "%.f anisotropy is not enough\n", value);
        }
    } else {
        Com_Printf("GL_EXT_texture_filter_anisotropic not found\n");
    }

    if (AT_LEAST_OPENGL(3, 0)) {
        gl_config.ext_enabled |= QGL_3_0_core_functions;
    }

    QGL_InitExtensions(gl_config.ext_enabled);

    qglGetIntegerv(GL_MAX_TEXTURE_SIZE, &integer);
    if (integer < 256) {
        Com_EPrintf("OpenGL reports invalid maximum texture size\n");
        return qfalse;
    }

    if (integer & (integer - 1)) {
        integer = npot32(integer) >> 1;
    }

    if (integer > MAX_TEXTURE_SIZE) {
        integer = MAX_TEXTURE_SIZE;
    }

    gl_config.maxTextureSize = integer;

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

    GL_ShowErrors(__func__);
    return qtrue;
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
        gl_static.latlngtab[i][0] = lat * (255.0f / (2 * M_PI));
        gl_static.latlngtab[i][1] = lng * (255.0f / (2 * M_PI));
    }

    for (i = 0; i < 256; i++) {
        gl_static.sintab[i] = sin(i * (2 * M_PI / 255.0f));
    }
}

static void GL_PostInit(void)
{
    registration_sequence = 1;

    if (gl_vertex_buffer_object->modified) {
        // enable buffer objects before map is loaded
        if (gl_config.ext_supported & QGL_ARB_vertex_buffer_object) {
            if (gl_vertex_buffer_object->integer) {
                Com_Printf("...enabling GL_ARB_vertex_buffer_object\n");
                QGL_InitExtensions(QGL_ARB_vertex_buffer_object);
                gl_config.ext_enabled |= QGL_ARB_vertex_buffer_object;
            } else {
                Com_Printf("...ignoring GL_ARB_vertex_buffer_object\n");
            }
        } else if (gl_vertex_buffer_object->integer) {
            Com_Printf("GL_ARB_vertex_buffer_object not found\n");
            Cvar_Set("gl_vertex_buffer_object", "0");
        }

        // reset the modified flag
        gl_vertex_buffer_object->modified = qfalse;
    }

    GL_SetDefaultState();
    GL_InitImages();
    MOD_Init();
}

// ==============================================================================

/*
===============
R_Init
===============
*/
qboolean R_Init(qboolean total)
{
    Com_DPrintf("GL_Init( %i )\n", total);

    if (!total) {
        GL_PostInit();
        return qtrue;
    }

    Com_Printf("------- R_Init -------\n");
    Com_DPrintf("ref_gl " VERSION ", " __DATE__ "\n");

    // initialize OS-specific parts of OpenGL
    // create the window and set up the context
    if (!VID_Init()) {
        return qfalse;
    }

    // initialize our QGL dynamic bindings
    if (!QGL_Init()) {
        goto fail;
    }

    // initialize extensions and get various limits from OpenGL
    if (!GL_SetupConfig()) {
        goto fail;
    }

    // register our variables
    GL_Register();

#ifdef _DEBUG
    if (gl_log->integer) {
        QGL_EnableLogging(gl_config.ext_enabled);
    }
    gl_log->modified = qfalse;
#endif

    GL_PostInit();

    GL_InitPrograms();
    gl_fragment_program->modified = qfalse;

    GL_InitTables();

    Com_Printf("----------------------\n");

    return qtrue;

fail:
    memset(&gl_config, 0, sizeof(gl_config));
    QGL_Shutdown();
    VID_Shutdown();
    return qfalse;
}

/*
===============
R_Shutdown
===============
*/
void R_Shutdown(qboolean total)
{
    Com_DPrintf("GL_Shutdown( %i )\n", total);

    GL_FreeWorld();
    GL_ShutdownImages();
    MOD_Shutdown();

    if (gl_vertex_buffer_object->modified) {
        // disable buffer objects after map is freed
        QGL_ShutdownExtensions(QGL_ARB_vertex_buffer_object);
        gl_config.ext_enabled &= ~QGL_ARB_vertex_buffer_object;
    }

    if (!total) {
        return;
    }

    GL_ShutdownPrograms();

    // shut down OS specific OpenGL stuff like contexts, etc.
    VID_Shutdown();

    // shutdown our QGL subsystem
    QGL_Shutdown();

    GL_Unregister();

    memset(&gl_static, 0, sizeof(gl_static));
    memset(&gl_config, 0, sizeof(gl_config));
}

/*
===============
R_BeginRegistration
===============
*/
void R_BeginRegistration(const char *name)
{
    char fullname[MAX_QPATH];

    gl_static.registering = qtrue;
    registration_sequence++;

    memset(&glr, 0, sizeof(glr));
    glr.viewcluster1 = glr.viewcluster2 = -2;

    Q_concat(fullname, sizeof(fullname), "maps/", name, ".bsp", NULL);
    GL_LoadWorld(fullname);
}

/*
===============
R_EndRegistration
===============
*/
void R_EndRegistration(void)
{
    IMG_FreeUnused();
    MOD_FreeUnused();
    Scrap_Upload();
    gl_static.registering = qfalse;
}

/*
===============
R_ModeChanged
===============
*/
void R_ModeChanged(int width, int height, int flags, int rowbytes, void *pixels)
{
    r_config.width = width;
    r_config.height = height;
    r_config.flags = flags;
}

void R_AddDecal(decal_t *d) {}

