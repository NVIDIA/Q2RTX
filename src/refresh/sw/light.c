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
// r_light.c

#include "sw.h"

int r_dlightframecount;


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
void R_MarkLights(dlight_t *light, int bit, mnode_t *node)
{
    cplane_t    *splitplane;
    float       dist;
    mface_t *surf;
    int         i;

    if (!node->plane)
        return;

    splitplane = node->plane;
    dist = DotProduct(light->origin, splitplane->normal) - splitplane->dist;

//=====
//PGM
    i = light->intensity;
    if (i < 0)
        i = -i;
//PGM
//=====

    if (dist > i) { // PGM (dist > light->intensity)
        R_MarkLights(light, bit, node->children[0]);
        return;
    }
    if (dist < -i) { // PGM (dist < -light->intensity)
        R_MarkLights(light, bit, node->children[1]);
        return;
    }

// mark the polygons
    surf = node->firstface;
    for (i = 0; i < node->numfaces; i++, surf++) {
        if (surf->dlightframe != r_dlightframecount) {
            surf->dlightbits = 0;
            surf->dlightframe = r_dlightframecount;
        }
        surf->dlightbits |= bit;
    }

    R_MarkLights(light, bit, node->children[0]);
    R_MarkLights(light, bit, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights(mnode_t *headnode)
{
    int     i;
    dlight_t    *l;

    r_dlightframecount = r_framecount;
    for (i = 0, l = r_newrefdef.dlights; i < r_newrefdef.num_dlights; i++, l++) {
        R_MarkLights(l, 1 << i, headnode);
    }
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

static qboolean RecursiveLightPoint(vec3_t start, vec3_t color)
{
    mface_t         *surf;
    int             smax, tmax, size;
    int             ds, dt;
    byte            *lightmap;
    float           *scales;
    int             maps;
    vec3_t          end;
    lightpoint_t    lightpoint;

    end[0] = start[0];
    end[1] = start[1];
    end[2] = start[2] - 2048;

    BSP_LightPoint(&lightpoint, start, end, r_worldmodel->nodes);

    surf = lightpoint.surf;
    if (!surf)
        return qfalse;

    ds = lightpoint.s >> 4;
    dt = lightpoint.t >> 4;

    smax = S_MAX(surf);
    tmax = T_MAX(surf);
    size = smax * tmax * LIGHTMAP_BYTES;

    lightmap = surf->lightmap;
    lightmap += dt * smax * LIGHTMAP_BYTES + ds * LIGHTMAP_BYTES;

    for (maps = 0; maps < surf->numstyles; maps++) {
        scales = r_newrefdef.lightstyles[surf->styles[maps]].rgb;
        color[0] += lightmap[0] * scales[0] * (1.0f / 255);
        color[1] += lightmap[1] * scales[1] * (1.0f / 255);
        color[2] += lightmap[2] * scales[2] * (1.0f / 255);
        lightmap += size;
    }

    return qtrue;
}

/*
===============
R_LightPoint
===============
*/
void R_LightPoint(vec3_t p, vec3_t color)
{
    int         lnum;
    dlight_t    *dl;
    vec3_t      dist;
    float       add;

    if (!r_worldmodel || !r_worldmodel->lightmap || !r_newrefdef.lightstyles) {
        VectorSet(color, 1, 1, 1);
        return;
    }

    VectorClear(color);

    if (!RecursiveLightPoint(p, color))
        VectorSet(color, 1, 1, 1);

    //
    // add dynamic lights
    //
    for (lnum = 0; lnum < r_newrefdef.num_dlights; lnum++) {
        dl = &r_newrefdef.dlights[lnum];
        VectorSubtract(p,
                       dl->origin,
                       dist);
        add = dl->intensity - VectorLength(dist);
        add *= (1.0 / 256);
        if (add > 0) {
            VectorMA(color, add, dl->color, color);
        }
    }

    VectorScale(color, sw_modulate->value, color);
}

//===================================================================

blocklight_t        blocklights[MAX_BLOCKLIGHTS * LIGHTMAP_BYTES];

/*
===============
R_AddDynamicLights
===============
*/
static void R_AddDynamicLights(void)
{
    mface_t *surf;
    int         lnum;
    int         sd, td;
    float       dist, rad, minlight;
    vec3_t      impact, local;
    int         s, t;
    int         i;
    int         smax, tmax;
    mtexinfo_t  *tex;
    dlight_t    *dl;
    int         negativeLight;  //PGM
    blocklight_t *block;

    surf = r_drawsurf.surf;
    smax = S_MAX(surf);
    tmax = T_MAX(surf);
    tex = surf->texinfo;

    for (lnum = 0; lnum < r_newrefdef.num_dlights; lnum++) {
        if (!(surf->dlightbits & (1 << lnum)))
            continue;       // not lit by this light

        dl = &r_newrefdef.dlights[lnum];
        rad = dl->intensity;

        negativeLight = 0;
        if (rad < 0) {
            negativeLight = 1;
            rad = -rad;
        }

        dist = PlaneDiffFast(dl->origin, surf->plane);
        rad -= fabs(dist);
        minlight = 32;      // dl->minlight;
        if (rad < minlight)
            continue;
        minlight = rad - minlight;

        for (i = 0; i < 3; i++) {
            impact[i] = dl->origin[i] - surf->plane->normal[i] * dist;
        }

        local[0] = DotProduct(impact, tex->axis[0]) + tex->offset[0];
        local[1] = DotProduct(impact, tex->axis[1]) + tex->offset[1];

        local[0] -= surf->texturemins[0];
        local[1] -= surf->texturemins[1];

        for (t = 0; t < tmax; t++) {
            td = local[1] - t * 16;
            if (td < 0)
                td = -td;
            for (s = 0; s < smax; s++) {
                sd = local[0] - s * 16;
                if (sd < 0)
                    sd = -sd;
                /*if (sd > td)
                    dist = sd + (td>>1);
                else
                    dist = td + (sd>>1);*/
                dist = sqrt(sd * sd + td * td);
                block = blocklights + t * smax * LIGHTMAP_BYTES + s * LIGHTMAP_BYTES;
                if (!negativeLight) {
                    if (dist < minlight) {
                        block[0] += (rad - dist) * dl->color[0] * 256;
                        block[1] += (rad - dist) * dl->color[1] * 256;
                        block[2] += (rad - dist) * dl->color[2] * 256;
                    }
                } else {
                    if (dist < minlight) {
                        block[0] -= (rad - dist) * dl->color[0] * 256;
                        block[1] -= (rad - dist) * dl->color[1] * 256;
                        block[2] -= (rad - dist) * dl->color[2] * 256;
                    }
                    if (block[0] < minlight) {
                        block[0] = minlight;
                    }
                    if (block[1] < minlight) {
                        block[1] = minlight;
                    }
                    if (block[2] < minlight) {
                        block[2] = minlight;
                    }
                }
            }
        }
    }
}


/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/

void R_BuildLightMap(void)
{
    int         smax, tmax;
    blocklight_t            t;
    int         i, size;
    byte        *lightmap;
    int         maps;
    mface_t *surf;
    blocklight_t *dst;

    surf = r_drawsurf.surf;

    smax = S_MAX(surf);
    tmax = T_MAX(surf);
    size = smax * tmax;
    if (size > MAX_BLOCKLIGHTS) {
        Com_Error(ERR_DROP, "R_BuildLightMap: surface blocklights size %i > %i", size, MAX_BLOCKLIGHTS);
    }

// clear to no light
    memset(blocklights, 0, sizeof(blocklights));

    if (r_fullbright->integer || !r_worldmodel->lightmap) {
        return;
    }

// add all the lightmaps
    lightmap = surf->lightmap;
    if (lightmap) {
        for (maps = 0; maps < surf->numstyles; maps++) {
            fixed8_t        scale;

            dst = blocklights;
            scale = r_drawsurf.lightadj[maps];  // 8.8 fraction
            for (i = 0; i < size; i++) {
                blocklights[i * LIGHTMAP_BYTES + 0] += lightmap[0] * scale;
                blocklights[i * LIGHTMAP_BYTES + 1] += lightmap[1] * scale;
                blocklights[i * LIGHTMAP_BYTES + 2] += lightmap[2] * scale;

                lightmap += LIGHTMAP_BYTES;
                dst += LIGHTMAP_BYTES;
            }
        }
    }

// add all the dynamic lights
    if (surf->dlightframe == r_framecount)
        R_AddDynamicLights();

// bound, invert, and shift
    for (i = 0; i < size * LIGHTMAP_BYTES; i++) {
        t = blocklights[i];
#if 0
        if (t < 0)
            t = 0;
        t = (255 * 256 - t) >> (8 - VID_CBITS);

        if (t < (1 << 6))
            t = (1 << 6);
#else
        clamp(t, 255, 65535);
#endif

        blocklights[i] = t;
    }

}

