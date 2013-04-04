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
 * gl_surf.c -- surface post-processing code
 *
 */
#include "gl.h"

lightmap_builder_t lm;

/*
=============================================================================

LIGHTMAP COLOR ADJUSTING

=============================================================================
*/

static inline void
adjust_color_f(vec_t *out, const vec_t *in, float modulate)
{
    float r, g, b, y, max;

    // add & modulate
    r = (in[0] + lm.add) * modulate;
    g = (in[1] + lm.add) * modulate;
    b = (in[2] + lm.add) * modulate;

    // catch negative lights
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;

    // determine the brightest of the three color components
    max = g;
    if (r > max) {
        max = r;
    }
    if (b > max) {
        max = b;
    }

    // rescale all the color components if the intensity of the greatest
    // channel exceeds 1.0
    if (max > 255) {
        y = 255.0f / max;
        r *= y;
        g *= y;
        b *= y;
    }

    // transform to grayscale by replacing color components with
    // overall pixel luminance computed from weighted color sum
    if (lm.scale != 1) {
        y = LUMINANCE(r, g, b);
        r = y + (r - y) * lm.scale;
        g = y + (g - y) * lm.scale;
        b = y + (b - y) * lm.scale;
    }

    out[0] = r;
    out[1] = g;
    out[2] = b;
}

static inline void
adjust_color_ub(byte *out, const vec_t *in)
{
    vec3_t tmp;

    adjust_color_f(tmp, in, lm.modulate);
    out[0] = (byte)tmp[0];
    out[1] = (byte)tmp[1];
    out[2] = (byte)tmp[2];
    out[3] = 255;
}

void GL_AdjustColor(vec3_t color)
{
    adjust_color_f(color, color, gl_static.entity_modulate);
    VectorScale(color, (1.0f / 255), color);
}

/*
=============================================================================

DYNAMIC BLOCKLIGHTS

=============================================================================
*/

#define MAX_SURFACE_EXTENTS     2048
#define MAX_LIGHTMAP_EXTENTS    ((MAX_SURFACE_EXTENTS >> 4) + 1)
#define MAX_BLOCKLIGHTS         (MAX_LIGHTMAP_EXTENTS * MAX_LIGHTMAP_EXTENTS)

static float blocklights[MAX_BLOCKLIGHTS * 3];

#if USE_DLIGHTS
static void add_dynamic_lights(mface_t *surf)
{
    dlight_t    *light;
    mtexinfo_t  *tex;
    vec3_t      point;
    int         local[2];
    vec_t       dist, rad, minlight, scale, frac;
    float       *bl;
    int         i, smax, tmax, s, t, sd, td;

    smax = S_MAX(surf);
    tmax = T_MAX(surf);
    tex = surf->texinfo;

    for (i = 0; i < glr.fd.num_dlights; i++) {
        if (!(surf->dlightbits & (1 << i)))
            continue;

        light = &glr.fd.dlights[i];
        dist = PlaneDiffFast(light->transformed, surf->plane);
        rad = light->intensity - fabs(dist);
        if (rad < DLIGHT_CUTOFF)
            continue;

        if (gl_dlight_falloff->integer) {
            minlight = rad - DLIGHT_CUTOFF * 0.8f;
            scale = rad / minlight; // fall off from rad to 0
        } else {
            minlight = rad - DLIGHT_CUTOFF;
            scale = 1;              // fall off from rad to minlight
        }

        VectorMA(light->transformed, -dist, surf->plane->normal, point);

        local[0] = DotProduct(point, tex->axis[0]) + tex->offset[0];
        local[1] = DotProduct(point, tex->axis[1]) + tex->offset[1];

        local[0] -= surf->texturemins[0];
        local[1] -= surf->texturemins[1];

        bl = blocklights;
        for (t = 0; t < tmax; t++) {
            td = abs(local[1] - (t << 4));
            for (s = 0; s < smax; s++) {
                sd = abs(local[0] - (s << 4));
                if (sd > td)
                    dist = sd + (td >> 1);
                else
                    dist = td + (sd >> 1);
                if (dist < minlight) {
                    frac = rad - dist * scale;
                    bl[0] += light->color[0] * frac;
                    bl[1] += light->color[1] * frac;
                    bl[2] += light->color[2] * frac;
                }
                bl += 3;
            }
        }
    }
}
#endif

static void add_light_styles(mface_t *surf, int size)
{
    lightstyle_t *style;
    byte *src;
    float *bl;
    int i, j;

    if (!surf->numstyles) {
        // should this ever happen?
        memset(blocklights, 0, sizeof(blocklights[0]) * size * 3);
        return;
    }

    // init primary lightmap
    style = LIGHT_STYLE(surf, 0);

    src = surf->lightmap;
    bl = blocklights;
    if (style->white == 1) {
        for (j = 0; j < size; j++) {
            bl[0] = src[0];
            bl[1] = src[1];
            bl[2] = src[2];

            bl += 3; src += 3;
        }
    } else {
        for (j = 0; j < size; j++) {
            bl[0] = src[0] * style->rgb[0];
            bl[1] = src[1] * style->rgb[1];
            bl[2] = src[2] * style->rgb[2];

            bl += 3; src += 3;
        }
    }

    surf->stylecache[0] = style->white;

    // add remaining lightmaps
    for (i = 1; i < surf->numstyles; i++) {
        style = LIGHT_STYLE(surf, i);

        bl = blocklights;
        for (j = 0; j < size; j++) {
            bl[0] += src[0] * style->rgb[0];
            bl[1] += src[1] * style->rgb[1];
            bl[2] += src[2] * style->rgb[2];

            bl += 3; src += 3;
        }

        surf->stylecache[i] = style->white;
    }
}

static void update_dynamic_lightmap(mface_t *surf)
{
    byte temp[MAX_BLOCKLIGHTS * 4], *dst;
    int smax, tmax, size, i;
    float *bl;

    smax = S_MAX(surf);
    tmax = T_MAX(surf);
    size = smax * tmax;

    // add all the lightmaps
    add_light_styles(surf, size);

#if USE_DLIGHTS
    // add all the dynamic lights
    if (surf->dlightframe == glr.dlightframe) {
        add_dynamic_lights(surf);
    } else {
        surf->dlightframe = 0;
    }
#endif

    // put into texture format
    bl = blocklights;
    dst = temp;
    for (i = 0; i < size; i++) {
        adjust_color_ub(dst, bl);
        bl += 3; dst += 4;
    }

    // upload lightmap subimage
    GL_ForceTexture(1, surf->texnum[1]);
    qglTexSubImage2D(GL_TEXTURE_2D, 0,
                     surf->light_s, surf->light_t, smax, tmax,
                     GL_RGBA, GL_UNSIGNED_BYTE, temp);

    c.texUploads++;
}

void GL_PushLights(mface_t *surf)
{
    lightstyle_t *style;
    int i;

    if (!surf->lightmap) {
        return;
    }
    if (surf->drawflags & SURF_NOLM_MASK) {
        return;
    }
    if (!surf->texnum[1]) {
        return;
    }

#if USE_DLIGHTS
    // dynamic this frame or dynamic previously
    if (surf->dlightframe) {
        update_dynamic_lightmap(surf);
        return;
    }
#endif

    // check for light style updates
    for (i = 0; i < surf->numstyles; i++) {
        style = LIGHT_STYLE(surf, i);
        if (style->white != surf->stylecache[i]) {
            update_dynamic_lightmap(surf);
            return;
        }
    }
}

/*
=============================================================================

LIGHTMAPS BUILDING

=============================================================================
*/

#define LM_AllocBlock(w, h, s, t) \
    GL_AllocBlock(LM_BLOCK_WIDTH, LM_BLOCK_HEIGHT, lm.inuse, w, h, s, t)

static void LM_InitBlock(void)
{
    int i;

    for (i = 0; i < LM_BLOCK_WIDTH; i++) {
        lm.inuse[i] = 0;
    }

    lm.dirty = qfalse;
}

static void LM_UploadBlock(void)
{
    if (!lm.dirty) {
        return;
    }

    GL_ForceTexture(1, lm.texnums[lm.nummaps++]);
    qglTexImage2D(GL_TEXTURE_2D, 0, lm.comp, LM_BLOCK_WIDTH, LM_BLOCK_HEIGHT, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, lm.buffer);
    qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

static void build_style_map(int dynamic)
{
    static lightstyle_t fake;
    int i;

    if (!dynamic) {
        // make all styles fullbright
        fake.rgb[0] = 1;
        fake.rgb[1] = 1;
        fake.rgb[2] = 1;
        fake.white = 1;
        glr.fd.lightstyles = &fake;

        memset(gl_static.lightstylemap, 0, sizeof(gl_static.lightstylemap));
        return;
    }

    for (i = 0; i < MAX_LIGHTSTYLES; i++) {
        gl_static.lightstylemap[i] = i;
    }

    if (dynamic != 1) {
        // make dynamic styles fullbright
        for (i = 1; i < 32; i++) {
            gl_static.lightstylemap[i] = 0;
        }
    }
}

static void LM_BeginBuilding(void)
{
    // lightmap textures are not deleted from memory when changing maps,
    // they are merely reused
    lm.nummaps = 0;

    LM_InitBlock();

    // start up with fullbright styles
    build_style_map(0);
}

static void LM_EndBuilding(void)
{
    // upload the last lightmap
    LM_UploadBlock();
    LM_InitBlock();

    // vertex lighting implies fullbright styles
    if (gl_fullbright->integer || gl_vertexlight->integer)
        return;

    // now build the real lightstyle map
    build_style_map(gl_dynamic->integer);

    Com_DPrintf("%s: %d lightmaps built\n", __func__, lm.nummaps);
}

static void build_primary_lightmap(mface_t *surf)
{
    byte *ptr, *dst;
    int smax, tmax, size, i, j;
    float *bl;

    smax = S_MAX(surf);
    tmax = T_MAX(surf);
    size = smax * tmax;

    // add all the lightmaps
    add_light_styles(surf, size);

#if USE_DLIGHTS
    surf->dlightframe = 0;
#endif

    // put into texture format
    bl = blocklights;
    dst = &lm.buffer[(surf->light_t * LM_BLOCK_WIDTH + surf->light_s) << 2];
    for (i = 0; i < tmax; i++) {
        ptr = dst;
        for (j = 0; j < smax; j++) {
            adjust_color_ub(ptr, bl);
            bl += 3; ptr += 4;
        }

        dst += LM_BLOCK_WIDTH * 4;
    }
}

static void LM_BuildSurface(mface_t *surf, vec_t *vbo)
{
    int smax, tmax, s, t;

    smax = S_MAX(surf);
    tmax = T_MAX(surf);

    if (!LM_AllocBlock(smax, tmax, &s, &t)) {
        LM_UploadBlock();
        if (lm.nummaps == LM_MAX_LIGHTMAPS) {
            Com_EPrintf("%s: LM_MAX_LIGHTMAPS exceeded\n", __func__);
            return;
        }
        LM_InitBlock();
        if (!LM_AllocBlock(smax, tmax, &s, &t)) {
            Com_EPrintf("%s: LM_AllocBlock(%d, %d) failed\n",
                        __func__, smax, tmax);
            return;
        }
    }

    lm.dirty = qtrue;

    // store the surface lightmap parameters
    surf->light_s = s;
    surf->light_t = t;
    surf->texnum[1] = lm.texnums[lm.nummaps];

    // build the primary lightmap
    build_primary_lightmap(surf);
}

static void LM_RebuildSurfaces(void)
{
    bsp_t *bsp = gl_static.world.cache;
    mface_t *surf;
    int i, texnum;

    build_style_map(gl_dynamic->integer);

    if (!lm.nummaps) {
        return;
    }

    GL_ForceTexture(1, lm.texnums[0]);
    texnum = lm.texnums[0];

    for (i = 0, surf = bsp->faces; i < bsp->numfaces; i++, surf++) {
        if (!surf->lightmap) {
            continue;
        }
        if (surf->drawflags & SURF_NOLM_MASK) {
            continue;
        }
        if (!surf->texnum[1]) {
            continue;
        }

        if (surf->texnum[1] != texnum) {
            // done with previous lightmap
            qglTexImage2D(GL_TEXTURE_2D, 0, lm.comp,
                          LM_BLOCK_WIDTH, LM_BLOCK_HEIGHT, 0,
                          GL_RGBA, GL_UNSIGNED_BYTE, lm.buffer);
            GL_ForceTexture(1, surf->texnum[1]);
            texnum = surf->texnum[1];

            c.texUploads++;
        }

        build_primary_lightmap(surf);
    }

    // upload the last lightmap
    qglTexImage2D(GL_TEXTURE_2D, 0, lm.comp,
                  LM_BLOCK_WIDTH, LM_BLOCK_HEIGHT, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, lm.buffer);

    c.texUploads++;
}


/*
=============================================================================

POLYGONS BUILDING

=============================================================================
*/

static uint32_t color_for_surface(mface_t *surf)
{
    if (surf->drawflags & SURF_TRANS33)
        return gl_static.inverse_intensity_33;

    if (surf->drawflags & SURF_TRANS66)
        return gl_static.inverse_intensity_66;

    if (surf->drawflags & SURF_WARP)
        return gl_static.inverse_intensity_100;

    return U32_WHITE;
}

static void build_surface_poly(mface_t *surf, vec_t *vbo)
{
    msurfedge_t *src_surfedge;
    mvertex_t *src_vert;
    medge_t *src_edge;
    mtexinfo_t *texinfo = surf->texinfo;
    vec2_t scale, tc, mins, maxs;
    int i, bmins[2], bmaxs[2];
    uint32_t color;

    surf->texnum[0] = texinfo->image->texnum;
    surf->texnum[1] = 0;

    color = color_for_surface(surf);

    // convert surface flags to state bits
    surf->statebits = GLS_DEFAULT;
    if (!(surf->drawflags & SURF_COLOR_MASK)) {
        surf->statebits |= GLS_TEXTURE_REPLACE;
    }

    if (surf->drawflags & SURF_WARP) {
        surf->statebits |= GLS_WARP_ENABLE;
    }

    if (surf->drawflags & SURF_TRANS_MASK) {
        surf->statebits |= GLS_BLEND_BLEND | GLS_DEPTHMASK_FALSE;
    } else if (surf->drawflags & SURF_ALPHATEST) {
        surf->statebits |= GLS_ALPHATEST_ENABLE;
    }

    if (surf->drawflags & SURF_FLOWING) {
        surf->statebits |= GLS_FLOW_ENABLE;
    }

    // normalize texture coordinates
    scale[0] = 1.0f / texinfo->image->width;
    scale[1] = 1.0f / texinfo->image->height;

    mins[0] = mins[1] = 99999;
    maxs[0] = maxs[1] = -99999;

    src_surfedge = surf->firstsurfedge;
    for (i = 0; i < surf->numsurfedges; i++) {
        src_edge = src_surfedge->edge;
        src_vert = src_edge->v[src_surfedge->vert];
        src_surfedge++;

        // vertex coordinates
        VectorCopy(src_vert->point, vbo);

        // vertex color
        memcpy(vbo + 3, &color, sizeof(color));

        // texture0 coordinates
        tc[0] = DotProduct(vbo, texinfo->axis[0]) + texinfo->offset[0];
        tc[1] = DotProduct(vbo, texinfo->axis[1]) + texinfo->offset[1];

        if (mins[0] > tc[0]) mins[0] = tc[0];
        if (maxs[0] < tc[0]) maxs[0] = tc[0];

        if (mins[1] > tc[1]) mins[1] = tc[1];
        if (maxs[1] < tc[1]) maxs[1] = tc[1];

        vbo[4] = tc[0] * scale[0];
        vbo[5] = tc[1] * scale[1];

        // texture1 coordinates
        vbo[6] = tc[0];
        vbo[7] = tc[1];

        vbo += VERTEX_SIZE;
    }

    // calculate surface extents
    bmins[0] = floor(mins[0] / 16);
    bmins[1] = floor(mins[1] / 16);
    bmaxs[0] = ceil(maxs[0] / 16);
    bmaxs[1] = ceil(maxs[1] / 16);

    surf->texturemins[0] = bmins[0] << 4;
    surf->texturemins[1] = bmins[1] << 4;

    surf->extents[0] = (bmaxs[0] - bmins[0]) << 4;
    surf->extents[1] = (bmaxs[1] - bmins[1]) << 4;
}

// vertex lighting approximation
static void sample_surface_verts(mface_t *surf, vec_t *vbo)
{
    int     i;
    vec3_t  color;

    glr.lightpoint.surf = surf;

    for (i = 0; i < surf->numsurfedges; i++) {
        glr.lightpoint.s = (int)vbo[6] - surf->texturemins[0];
        glr.lightpoint.t = (int)vbo[7] - surf->texturemins[1];

        GL_SampleLightPoint(color);
        adjust_color_ub((byte *)(vbo + 3), color);

        vbo += VERTEX_SIZE;
    }

    surf->statebits &= ~GLS_TEXTURE_REPLACE;
    surf->statebits |= GLS_SHADE_SMOOTH;
}

// validates and processes surface lightmap
static void build_surface_light(mface_t *surf, vec_t *vbo)
{
    int smax, tmax, size;
    byte *src, *ptr;
    bsp_t *bsp;

    if (gl_fullbright->integer)
        return;

    if (!surf->lightmap)
        return;

    if (surf->drawflags & SURF_NOLM_MASK)
        return;

    // validate extents
    if (surf->extents[0] < 0 || surf->extents[0] > MAX_SURFACE_EXTENTS ||
        surf->extents[1] < 0 || surf->extents[1] > MAX_SURFACE_EXTENTS) {
        Com_EPrintf("%s: bad surface extents\n", __func__);
        surf->lightmap = NULL;  // don't use this lightmap
        return;
    }

    // validate blocklights size
    smax = S_MAX(surf);
    tmax = T_MAX(surf);
    size = smax * tmax;
    if (size > MAX_BLOCKLIGHTS) {
        Com_EPrintf("%s: MAX_BLOCKLIGHTS exceeded\n", __func__);
        surf->lightmap = NULL;  // don't use this lightmap
        return;
    }

    // validate lightmap bounds
    bsp = gl_static.world.cache;
    src = surf->lightmap + surf->numstyles * size * 3;
    ptr = bsp->lightmap + bsp->numlightmapbytes;
    if (src > ptr) {
        Com_EPrintf("%s: bad surface lightmap\n", __func__);
        surf->lightmap = NULL;  // don't use this lightmap
        return;
    }

    if (gl_vertexlight->integer)
        sample_surface_verts(surf, vbo);
    else
        LM_BuildSurface(surf, vbo);
}

// normalizes and stores lightmap texture coordinates in vertices
static void normalize_surface_lmtc(mface_t *surf, vec_t *vbo)
{
    float s, t;
    int i;

    s = ((surf->light_s << 4) + 8) - surf->texturemins[0];
    t = ((surf->light_t << 4) + 8) - surf->texturemins[1];

    for (i = 0; i < surf->numsurfedges; i++) {
        vbo[6] += s;
        vbo[7] += t;
        vbo[6] *= 1.0f / (LM_BLOCK_WIDTH * 16);
        vbo[7] *= 1.0f / (LM_BLOCK_HEIGHT * 16);

        vbo += VERTEX_SIZE;
    }
}

// duplicates normalized texture0 coordinates for non-lit surfaces in texture1
// to make them render properly when gl_lightmap hack is used
static void duplicate_surface_lmtc(mface_t *surf, vec_t *vbo)
{
    int i;

    for (i = 0; i < surf->numsurfedges; i++) {
        vbo[6] = vbo[4];
        vbo[7] = vbo[5];

        vbo += VERTEX_SIZE;
    }
}

static qboolean create_surface_vbo(size_t size)
{
    GLuint buf = 0;

    if (!qglGenBuffersARB || !qglBindBufferARB ||
        !qglBufferDataARB || !qglBufferSubDataARB ||
        !qglDeleteBuffersARB) {
        return qfalse;
    }

    GL_ClearErrors();

    qglGenBuffersARB(1, &buf);
    qglBindBufferARB(GL_ARRAY_BUFFER_ARB, buf);
    qglBufferDataARB(GL_ARRAY_BUFFER_ARB, size, NULL, GL_STATIC_DRAW_ARB);

    if (GL_ShowErrors("Failed to create world model VBO")) {
        qglBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
        qglDeleteBuffersARB(1, &buf);
        return qfalse;
    }

    gl_static.world.vertices = NULL;
    gl_static.world.bufnum = buf;
    return qtrue;
}

static void upload_surface_vbo(int lastvert)
{
    GLintptrARB offset = lastvert * VERTEX_SIZE * sizeof(vec_t);
    GLsizeiptrARB size = tess.numverts * VERTEX_SIZE * sizeof(vec_t);

    Com_DDPrintf("%s: %"PRIz" bytes at %"PRIz"\n", __func__, size, offset);

    qglBufferSubDataARB(GL_ARRAY_BUFFER_ARB, offset, size, tess.vertices);
    tess.numverts = 0;
}

static void upload_world_surfaces(void)
{
    bsp_t *bsp = gl_static.world.cache;
    vec_t *vbo;
    mface_t *surf;
    int i, currvert, lastvert;

    // force vertex lighting if multitexture is not supported
    if (!qglActiveTextureARB || !qglClientActiveTextureARB)
        Cvar_Set("gl_vertexlight", "1");

    if (!gl_static.world.vertices)
        qglBindBufferARB(GL_ARRAY_BUFFER_ARB, gl_static.world.bufnum);

    currvert = 0;
    lastvert = 0;
    for (i = 0, surf = bsp->faces; i < bsp->numfaces; i++, surf++) {
        if (surf->drawflags & SURF_SKY)
            continue;

        if (gl_static.world.vertices) {
            vbo = gl_static.world.vertices + currvert * VERTEX_SIZE;
        } else {
            // upload VBO chunk if needed
            if (tess.numverts + surf->numsurfedges > TESS_MAX_VERTICES) {
                upload_surface_vbo(lastvert);
                lastvert = currvert;
            }

            vbo = tess.vertices + tess.numverts * VERTEX_SIZE;
            tess.numverts += surf->numsurfedges;
        }

        surf->firstvert = currvert;
        build_surface_poly(surf, vbo);
        build_surface_light(surf, vbo);

        if (surf->texnum[1])
            normalize_surface_lmtc(surf, vbo);
        else
            duplicate_surface_lmtc(surf, vbo);

        currvert += surf->numsurfedges;
    }

    // upload the last VBO chunk
    if (!gl_static.world.vertices) {
        upload_surface_vbo(lastvert);
        qglBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
    }

    gl_fullbright->modified = qfalse;
    gl_vertexlight->modified = qfalse;
}

static void set_world_size(void)
{
    mnode_t *node = gl_static.world.cache->nodes;
    vec_t size, temp;
    int i;

    for (i = 0, size = 0; i < 3; i++) {
        temp = node->maxs[i] - node->mins[i];
        if (temp > size)
            size = temp;
    }

    if (size > 4096)
        gl_static.world.size = 8192;
    else if (size > 2048)
        gl_static.world.size = 4096;
    else
        gl_static.world.size = 2048;
}

// called from the main loop whenever lighting parameters change
void GL_RebuildLighting(void)
{
    if (!gl_static.world.cache)
        return;

    // if doing vertex lighting, rebuild all surfaces
    if (gl_fullbright->integer || gl_vertexlight->integer) {
        upload_world_surfaces();
        return;
    }

    // if did vertex lighting previously, rebuild all surfaces and lightmaps
    if (gl_fullbright->modified || gl_vertexlight->modified) {
        LM_BeginBuilding();
        upload_world_surfaces();
        LM_EndBuilding();
        return;
    }

    // rebuild all lightmaps
    LM_RebuildSurfaces();
}

void GL_FreeWorld(void)
{
    if (!gl_static.world.cache) {
        return;
    }

    BSP_Free(gl_static.world.cache);

    if (gl_static.world.vertices) {
        Hunk_Free(&gl_static.world.hunk);
    } else if (qglDeleteBuffersARB) {
        qglDeleteBuffersARB(1, &gl_static.world.bufnum);
    }

    memset(&gl_static.world, 0, sizeof(gl_static.world));
}

void GL_LoadWorld(const char *name)
{
    char buffer[MAX_QPATH];
    size_t size;
    bsp_t *bsp;
    mtexinfo_t *info;
    mface_t *surf;
    qerror_t ret;
    imageflags_t flags;
    int i;

    ret = BSP_Load(name, &bsp);
    if (!bsp) {
        Com_Error(ERR_DROP, "%s: couldn't load %s: %s",
                  __func__, name, Q_ErrorString(ret));
    }

    // check if the required world model was already loaded
    if (gl_static.world.cache == bsp) {
        for (i = 0; i < bsp->numtexinfo; i++) {
            bsp->texinfo[i].image->registration_sequence = registration_sequence;
        }
        for (i = 0; i < bsp->numnodes; i++) {
            bsp->nodes[i].visframe = 0;
        }
        for (i = 0; i < bsp->numleafs; i++) {
            bsp->leafs[i].visframe = 0;
        }
        Com_DPrintf("%s: reused old world model\n", __func__);
        bsp->refcount--;
        return;
    }

    // free previous model, if any
    GL_FreeWorld();

    gl_static.world.cache = bsp;

    // calculate world size for far clip plane and sky box
    set_world_size();

    // register all texinfo
    for (i = 0, info = bsp->texinfo; i < bsp->numtexinfo; i++, info++) {
        if (info->c.flags & SURF_WARP)
            flags = IF_TURBULENT;
        else
            flags = IF_NONE;

        Q_concat(buffer, sizeof(buffer), "textures/", info->name, ".wal", NULL);
        FS_NormalizePath(buffer, buffer);
        info->image = IMG_Find(buffer, IT_WALL, flags);
    }

    // calculate vertex buffer size in bytes
    size = 0;
    for (i = 0, surf = bsp->faces; i < bsp->numfaces; i++, surf++) {
        // hack surface flags into drawflags for faster access
        surf->drawflags |= surf->texinfo->c.flags & ~DSURF_PLANEBACK;

        // don't count sky surfaces
        if (surf->drawflags & SURF_SKY)
            continue;

        size += surf->numsurfedges * VERTEX_SIZE * sizeof(vec_t);
    }

    // try VBO first, then allocate on hunk
    if (create_surface_vbo(size)) {
        Com_DPrintf("%s: %"PRIz" bytes of vertex data as VBO\n", __func__, size);
    } else {
        Hunk_Begin(&gl_static.world.hunk, size);
        gl_static.world.vertices = Hunk_Alloc(&gl_static.world.hunk, size);
        Hunk_End(&gl_static.world.hunk);

        Com_DPrintf("%s: %"PRIz" bytes of vertex data on hunk\n", __func__, size);
    }

    // begin building lightmaps
    LM_BeginBuilding();

    // post process all surfaces
    upload_world_surfaces();

    // end building lightmaps
    LM_EndBuilding();

    GL_ShowErrors(__func__);
}

