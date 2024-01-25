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

void GL_SampleLightPoint(vec3_t color)
{
    mface_t         *surf = glr.lightpoint.surf;
    int             s, t, i;
    byte            *lightmap;
    byte            *b1, *b2, *b3, *b4;
    float           fracu, fracv;
    float           w1, w2, w3, w4;
    vec3_t          temp;
    int             smax, tmax, size;
    lightstyle_t    *style;

    s = glr.lightpoint.s;
    t = glr.lightpoint.t;

    fracu = glr.lightpoint.s - s;
    fracv = glr.lightpoint.t - t;

    // compute weights of lightmap blocks
    w1 = (1.0f - fracu) * (1.0f - fracv);
    w2 = fracu * (1.0f - fracv);
    w3 = fracu * fracv;
    w4 = (1.0f - fracu) * fracv;

    smax = surf->lm_width;
    tmax = surf->lm_height;
    size = smax * tmax * 3;

    VectorClear(color);

    // add all the lightmaps with bilinear filtering
    lightmap = surf->lightmap;
    for (i = 0; i < surf->numstyles; i++) {
        b1 = &lightmap[3 * ((t + 0) * smax + (s + 0))];
        b2 = &lightmap[3 * ((t + 0) * smax + (s + 1))];
        b3 = &lightmap[3 * ((t + 1) * smax + (s + 1))];
        b4 = &lightmap[3 * ((t + 1) * smax + (s + 0))];

        temp[0] = w1 * b1[0] + w2 * b2[0] + w3 * b3[0] + w4 * b4[0];
        temp[1] = w1 * b1[1] + w2 * b2[1] + w3 * b3[1] + w4 * b4[1];
        temp[2] = w1 * b1[2] + w2 * b2[2] + w3 * b3[2] + w4 * b4[2];

        style = LIGHT_STYLE(surf, i);

        color[0] += temp[0] * style->white;
        color[1] += temp[1] * style->white;
        color[2] += temp[2] * style->white;

        lightmap += size;
    }
}

static bool _GL_LightPoint(const vec3_t start, vec3_t color)
{
    bsp_t           *bsp;
    int             i, index;
    lightpoint_t    pt;
    vec3_t          end, mins, maxs;
    entity_t        *ent;
    mmodel_t        *model;
    vec_t           *angles;

    bsp = gl_static.world.cache;
    if (!bsp || !bsp->lightmap)
        return false;

    end[0] = start[0];
    end[1] = start[1];
    end[2] = start[2] - 8192;

    // get base lightpoint from world
    BSP_LightPoint(&glr.lightpoint, start, end, bsp->nodes);

    // trace to other BSP models
    for (i = 0; i < glr.fd.num_entities; i++) {
        ent = &glr.fd.entities[i];
        index = ent->model;
        if (!(index & BIT(31)))
            break;  // BSP models are at the start of entity array

        index = ~index;
        if (index < 1 || index >= bsp->nummodels)
            continue;

        model = &bsp->models[index];
        if (!model->numfaces)
            continue;

        // cull in X/Y plane
        if (!VectorEmpty(ent->angles)) {
            if (fabsf(start[0] - ent->origin[0]) > model->radius)
                continue;
            if (fabsf(start[1] - ent->origin[1]) > model->radius)
                continue;
            angles = ent->angles;
        } else {
            VectorAdd(model->mins, ent->origin, mins);
            VectorAdd(model->maxs, ent->origin, maxs);
            if (start[0] < mins[0] || start[0] > maxs[0])
                continue;
            if (start[1] < mins[1] || start[1] > maxs[1])
                continue;
            angles = NULL;
        }

        BSP_TransformedLightPoint(&pt, start, end, model->headnode,
                                  ent->origin, angles);

        if (pt.fraction < glr.lightpoint.fraction)
            glr.lightpoint = pt;
    }

    if (!glr.lightpoint.surf)
        return false;

    GL_SampleLightPoint(color);

    GL_AdjustColor(color);

    return true;
}

static void GL_MarkLights_r(mnode_t *node, dlight_t *light, unsigned lightbit)
{
    vec_t dot;
    int count;
    mface_t *face;

    while (node->plane) {
        dot = PlaneDiffFast(light->transformed, node->plane);
        if (dot > light->intensity - DLIGHT_CUTOFF) {
            node = node->children[0];
            continue;
        }
        if (dot < -light->intensity + DLIGHT_CUTOFF) {
            node = node->children[1];
            continue;
        }

        face = node->firstface;
        count = node->numfaces;
        while (count--) {
            if (!(face->drawflags & SURF_NOLM_MASK)) {
                if (face->dlightframe != glr.dlightframe) {
                    face->dlightframe = glr.dlightframe;
                    face->dlightbits = 0;
                }

                face->dlightbits |= lightbit;
            }

            face++;
        }

        GL_MarkLights_r(node->children[0], light, lightbit);
        node = node->children[1];
    }
}

static void GL_MarkLights(void)
{
    int i;
    dlight_t *light;

    glr.dlightframe++;

    for (i = 0, light = glr.fd.dlights; i < glr.fd.num_dlights; i++, light++) {
        if(light->light_type != DLIGHT_SPHERE)
            continue;
        VectorCopy(light->origin, light->transformed);
        GL_MarkLights_r(gl_static.world.cache->nodes, light, BIT(i));
    }
}

static void GL_TransformLights(mmodel_t *model)
{
    int i;
    dlight_t *light;
    vec3_t temp;

    glr.dlightframe++;

    for (i = 0, light = glr.fd.dlights; i < glr.fd.num_dlights; i++, light++) {
        if(light->light_type != DLIGHT_SPHERE)
            continue;
        VectorSubtract(light->origin, glr.ent->origin, temp);
        light->transformed[0] = DotProduct(temp, glr.entaxis[0]);
        light->transformed[1] = DotProduct(temp, glr.entaxis[1]);
        light->transformed[2] = DotProduct(temp, glr.entaxis[2]);
        GL_MarkLights_r(model->headnode, light, BIT(i));
    }
}

static void GL_AddLights(const vec3_t origin, vec3_t color)
{
    dlight_t *light;
    vec_t f;
    int i;

    for (i = 0, light = glr.fd.dlights; i < glr.fd.num_dlights; i++, light++) {
        if(light->light_type != DLIGHT_SPHERE)
            continue;
        f = light->intensity - DLIGHT_CUTOFF - Distance(light->origin, origin);
        if (f > 0) {
            f *= (1.0f / 255);
            VectorMA(color, f, light->color, color);
        }
    }
}

void GL_LightPoint(const vec3_t origin, vec3_t color)
{
    if (gl_fullbright->integer) {
        VectorSet(color, 1, 1, 1);
        return;
    }

    // get lighting from world
    if (!_GL_LightPoint(origin, color)) {
        VectorSet(color, 1, 1, 1);
    }

    // add dynamic lights
    GL_AddLights(origin, color);

    if (gl_doublelight_entities->integer) {
        // apply modulate twice to mimic original ref_gl behavior
        VectorScale(color, gl_static.entity_modulate, color);
    }
}

void R_LightPoint_GL(const vec3_t origin, vec3_t color)
{
    GL_LightPoint(origin, color);

    color[0] = Q_clipf(color[0], 0, 1);
    color[1] = Q_clipf(color[1], 0, 1);
    color[2] = Q_clipf(color[2], 0, 1);
}

static void GL_MarkLeaves(void)
{
    static int lastNodesVisible;
    byte vis1[VIS_MAX_BYTES];
    byte vis2[VIS_MAX_BYTES];
    mleaf_t *leaf;
    mnode_t *node;
    uint_fast32_t *src1, *src2;
    int cluster1, cluster2, longs;
    vec3_t tmp;
    int i;
    bsp_t *bsp = gl_static.world.cache;

    leaf = BSP_PointLeaf(bsp->nodes, glr.fd.vieworg);
    cluster1 = cluster2 = leaf->cluster;
    VectorCopy(glr.fd.vieworg, tmp);
    if (!leaf->contents) {
        tmp[2] -= 16;
    } else {
        tmp[2] += 16;
    }
    leaf = BSP_PointLeaf(bsp->nodes, tmp);
    if (!(leaf->contents & CONTENTS_SOLID)) {
        cluster2 = leaf->cluster;
    }

    if (cluster1 == glr.viewcluster1 && cluster2 == glr.viewcluster2) {
        goto finish;
    }

    if (gl_lockpvs->integer) {
        goto finish;
    }

    glr.visframe++;
    glr.viewcluster1 = cluster1;
    glr.viewcluster2 = cluster2;

    if (!bsp->vis || gl_novis->integer || cluster1 == -1) {
        // mark everything visible
        for (i = 0; i < bsp->numnodes; i++) {
            bsp->nodes[i].visframe = glr.visframe;
        }
        for (i = 0; i < bsp->numleafs; i++) {
            bsp->leafs[i].visframe = glr.visframe;
        }
        lastNodesVisible = bsp->numnodes;
        goto finish;
    }

    BSP_ClusterVis(bsp, vis1, cluster1, DVIS_PVS);
    if (cluster1 != cluster2) {
        BSP_ClusterVis(bsp, vis2, cluster2, DVIS_PVS);
        longs = VIS_FAST_LONGS(bsp);
        src1 = (uint_fast32_t *)vis1;
        src2 = (uint_fast32_t *)vis2;
        while (longs--) {
            *src1++ |= *src2++;
        }
    }

    lastNodesVisible = 0;
    for (i = 0, leaf = bsp->leafs; i < bsp->numleafs; i++, leaf++) {
        cluster1 = leaf->cluster;
        if (cluster1 == -1) {
            continue;
        }
        if (Q_IsBitSet(vis1, cluster1)) {
            node = (mnode_t *)leaf;

            // mark parent nodes visible
            do {
                if (node->visframe == glr.visframe) {
                    break;
                }
                node->visframe = glr.visframe;
                node = node->parent;
                lastNodesVisible++;
            } while (node);
        }
    }

finish:
    c.nodesVisible = lastNodesVisible;

}

#define BACKFACE_EPSILON    0.01f

#define BSP_CullFace(face, dot) \
    (((dot) < -BACKFACE_EPSILON && !((face)->drawflags & DSURF_PLANEBACK)) || \
     ((dot) >  BACKFACE_EPSILON &&  ((face)->drawflags & DSURF_PLANEBACK)))

void GL_DrawBspModel(mmodel_t *model)
{
    mface_t *face, *last;
    vec3_t bounds[2];
    vec_t dot;
    vec3_t transformed, temp;
    entity_t *ent = glr.ent;
    glCullResult_t cull;

    if (!model->numfaces)
        return;

    if (glr.entrotated) {
        cull = GL_CullSphere(ent->origin, model->radius);
        if (cull == CULL_OUT) {
            c.spheresCulled++;
            return;
        }
        if (cull == CULL_CLIP) {
            VectorCopy(model->mins, bounds[0]);
            VectorCopy(model->maxs, bounds[1]);
            cull = GL_CullLocalBox(ent->origin, bounds);
            if (cull == CULL_OUT) {
                c.rotatedBoxesCulled++;
                return;
            }
        }
        VectorSubtract(glr.fd.vieworg, ent->origin, temp);
        transformed[0] = DotProduct(temp, glr.entaxis[0]);
        transformed[1] = DotProduct(temp, glr.entaxis[1]);
        transformed[2] = DotProduct(temp, glr.entaxis[2]);
    } else {
        VectorAdd(model->mins, ent->origin, bounds[0]);
        VectorAdd(model->maxs, ent->origin, bounds[1]);
        cull = GL_CullBox(bounds);
        if (cull == CULL_OUT) {
            c.boxesCulled++;
            return;
        }
        VectorSubtract(glr.fd.vieworg, ent->origin, transformed);
    }

    GL_TransformLights(model);

    GL_RotateForEntity();

    GL_BindArrays();

    // draw visible faces
    last = model->firstface + model->numfaces;
    for (face = model->firstface; face < last; face++) {
        dot = PlaneDiffFast(transformed, face->plane);
        if (BSP_CullFace(face, dot)) {
            c.facesCulled++;
            continue;
        }

        // sky faces don't have their polygon built
        if (face->drawflags & (SURF_SKY | SURF_NODRAW)) {
            continue;
        }

        if (face->drawflags & SURF_TRANS_MASK) {
            if (model->drawframe != glr.drawframe)
                GL_AddAlphaFace(face, ent);
            continue;
        }

        if (gl_dynamic->integer) {
            GL_PushLights(face);
        }

        GL_DrawFace(face);
    }

    GL_Flush3D();

    // protect against infinite loop if the same inline model
    // with alpha faces is referenced by multiple entities
    model->drawframe = glr.drawframe;
}

#define NODE_CLIPPED    0
#define NODE_UNCLIPPED  (BIT(4) - 1)

static inline bool GL_ClipNode(mnode_t *node, int *clipflags)
{
    int flags = *clipflags;
    int i, bits, mask;

    if (flags == NODE_UNCLIPPED) {
        return true;
    }
    for (i = 0, mask = 1; i < 4; i++, mask <<= 1) {
        if (flags & mask) {
            continue;
        }
        bits = BoxOnPlaneSide(node->mins, node->maxs,
                              &glr.frustumPlanes[i]);
        if (bits == BOX_BEHIND) {
            return false;
        }
        if (bits == BOX_INFRONT) {
            flags |= mask;
        }
    }

    *clipflags = flags;

    return true;
}

static inline void GL_DrawLeaf(mleaf_t *leaf)
{
    mface_t **face, **last;

    if (leaf->contents == CONTENTS_SOLID) {
        return; // solid leaf
    }
    if (glr.fd.areabits && !Q_IsBitSet(glr.fd.areabits, leaf->area)) {
        return; // door blocks sight
    }

    last = leaf->firstleafface + leaf->numleaffaces;
    for (face = leaf->firstleafface; face < last; face++) {
        (*face)->drawframe = glr.drawframe;
    }

    c.leavesDrawn++;
}

static inline void GL_DrawNode(mnode_t *node)
{
    mface_t *face, *last = node->firstface + node->numfaces;

    for (face = node->firstface; face < last; face++) {
        if (face->drawframe != glr.drawframe) {
            continue;
        }

        if (face->drawflags & SURF_SKY) {
            R_AddSkySurface(face);
            continue;
        }

        if (face->drawflags & SURF_NODRAW) {
            continue;
        }

        if (face->drawflags & SURF_TRANS_MASK) {
            GL_AddAlphaFace(face, &gl_world);
            continue;
        }

        if (gl_dynamic->integer) {
            GL_PushLights(face);
        }

        if (gl_hash_faces->integer) {
            GL_AddSolidFace(face);
        } else {
            GL_DrawFace(face);
        }
    }

    c.nodesDrawn++;
}

static void GL_WorldNode_r(mnode_t *node, int clipflags)
{
    int side;
    vec_t dot;

    while (node->visframe == glr.visframe) {
        if (!GL_ClipNode(node, &clipflags)) {
            c.nodesCulled++;
            break;
        }

        if (!node->plane) {
            GL_DrawLeaf((mleaf_t *)node);
            break;
        }

        dot = PlaneDiffFast(glr.fd.vieworg, node->plane);
        side = dot < 0;

        GL_WorldNode_r(node->children[side], clipflags);

        GL_DrawNode(node);

        node = node->children[side ^ 1];
    }
}

void GL_DrawWorld(void)
{
    // auto cycle the world frame for texture animation
    gl_world.frame = (int)(glr.fd.time * 2);

    glr.ent = &gl_world;

    GL_MarkLeaves();

    GL_MarkLights();

    R_ClearSkyBox();

    GL_LoadMatrix(glr.viewmatrix);

    GL_BindArrays();

    if (gl_hash_faces->integer)
        GL_ClearSolidFaces();

    GL_WorldNode_r(gl_static.world.cache->nodes,
                   gl_cull_nodes->integer ? NODE_CLIPPED : NODE_UNCLIPPED);

    if (gl_hash_faces->integer)
        GL_DrawSolidFaces();

    GL_Flush3D();

    R_DrawSkyBox();
}

