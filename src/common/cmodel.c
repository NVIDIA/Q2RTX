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
// cmodel.c -- model loading

#include "shared/shared.h"
#include "common/bsp.h"
#include "common/cmd.h"
#include "common/cmodel.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "common/math.h"
#include "common/sizebuf.h"
#include "common/zone.h"
#include "system/hunk.h"

mtexinfo_t nulltexinfo;

static mleaf_t      nullleaf;

static unsigned     floodvalid;
static unsigned     checkcount;

static cvar_t       *map_noareas;
static cvar_t       *map_allsolid_bug;
static cvar_t       *map_override_path;

static void    FloodAreaConnections(cm_t *cm);

//=======================================================================

enum {
    OVERRIDE_NAME   = 1,
    OVERRIDE_CSUM   = 2,
    OVERRIDE_ENTS   = 4,
    OVERRIDE_ALL    = 7
};

static void load_entstring_override(cm_t *cm, const char *server)
{
    char buffer[MAX_QPATH], *data = NULL;
    int ret;

    if (Q_snprintf(buffer, sizeof(buffer), "%s/%s.ent", map_override_path->string, server) >= sizeof(buffer)) {
        ret = Q_ERR(ENAMETOOLONG);
        goto fail;
    }

    ret = FS_LoadFileEx(buffer, (void **)&data, 0, TAG_CMODEL);
    if (!data) {
        if (ret == Q_ERR(ENOENT))
            return;
        goto fail;
    }

    Com_Printf("Loaded entity string from %s\n", buffer);
    cm->entitystring = data;
    cm->override_bits |= OVERRIDE_ENTS;
    return;

fail:
    FS_FreeFile(data);
    Com_EPrintf("Couldn't load entity string from %s: %s\n", buffer, Q_ErrorString(ret));
}

static void load_binary_override(cm_t *cm, char *server, size_t server_size)
{
    sizebuf_t sz;
    char buffer[MAX_QPATH];
    byte *data = NULL;
    int ret, bits, len;
    char *buf, name_buf[MAX_QPATH];

    if (Q_snprintf(buffer, sizeof(buffer), "%s/%s.bsp.override", map_override_path->string, server) >= sizeof(buffer)) {
        ret = Q_ERR(ENAMETOOLONG);
        goto fail;
    }

    ret = FS_LoadFile(buffer, (void **)&data);
    if (!data) {
        if (ret == Q_ERR(ENOENT))
            return;
        goto fail;
    }

    SZ_Init(&sz, data, ret);
    sz.cursize = ret;

    ret = Q_ERR_INVALID_FORMAT;

    bits = SZ_ReadLong(&sz);
    if (bits & ~OVERRIDE_ALL)
        goto fail;

    if (bits & OVERRIDE_NAME) {
        if (!(buf = SZ_ReadData(&sz, MAX_QPATH)))
            goto fail;
        if (!memchr(buf, 0, MAX_QPATH))
            goto fail;
        if (!Com_ParseMapName(name_buf, buf, sizeof(name_buf)))
            goto fail;
    }

    if (bits & OVERRIDE_CSUM)
        cm->checksum = SZ_ReadLong(&sz);

    if (bits & OVERRIDE_ENTS) {
        len = SZ_ReadLong(&sz);
        if (len <= 0)
            goto fail;
        if (!(buf = SZ_ReadData(&sz, len)))
            goto fail;
        cm->entitystring = Z_TagMalloc(len + 1, TAG_CMODEL);
        memcpy(cm->entitystring, buf, len);
        cm->entitystring[len] = 0;
    }

    if (bits & OVERRIDE_NAME)
        Q_strlcpy(server, name_buf, server_size);

    Com_Printf("Loaded %s\n", buffer);
    FS_FreeFile(data);
    cm->override_bits = bits;
    return;

fail:
    Com_EPrintf("Couldn't load %s: %s\n", buffer, Q_ErrorString(ret));
    FS_FreeFile(data);
}

/*
==================
CM_LoadOverrides

Ugly hack to override entstring and other parameters.

Must be called before CM_LoadMap.
May modify server buffer if name override is in effect.
May allocate enstring, must be freed with CM_FreeMap().
==================
*/
void CM_LoadOverrides(cm_t *cm, char *server, size_t server_size)
{
    if (!*map_override_path->string)
        return;

    load_binary_override(cm, server, server_size);

    if (!(cm->override_bits & OVERRIDE_ENTS))
        load_entstring_override(cm, server);
}

/*
==================
CM_FreeMap
==================
*/
void CM_FreeMap(cm_t *cm)
{
    Z_Free(cm->portalopen);
    Z_Free(cm->floodnums);

    if (cm->override_bits & OVERRIDE_ENTS)
        Z_Free(cm->entitystring);

    BSP_Free(cm->cache);

    memset(cm, 0, sizeof(*cm));
}

/*
==================
CM_LoadMap

Loads in the map and all submodels
==================
*/
int CM_LoadMap(cm_t *cm, const char *name)
{
    int ret;

    ret = BSP_Load(name, &cm->cache);
    if (!cm->cache)
        return ret;

    if (!(cm->override_bits & OVERRIDE_CSUM))
        cm->checksum = cm->cache->checksum;

    if (!(cm->override_bits & OVERRIDE_ENTS))
        cm->entitystring = cm->cache->entitystring;

    cm->floodnums = Z_TagMallocz(sizeof(cm->floodnums[0]) * cm->cache->numareas, TAG_CMODEL);
    cm->portalopen = Z_TagMallocz(sizeof(cm->portalopen[0]) * cm->cache->numportals, TAG_CMODEL);
    FloodAreaConnections(cm);

    return Q_ERR_SUCCESS;
}

mnode_t *CM_NodeNum(cm_t *cm, int number)
{
    if (!cm->cache) {
        return (mnode_t *)&nullleaf;
    }
    if (number == -1) {
        return (mnode_t *)cm->cache->leafs;   // special case for solid leaf
    }
    if (number < 0 || number >= cm->cache->numnodes) {
        Com_EPrintf("%s: bad number: %d\n", __func__, number);
        return (mnode_t *)&nullleaf;
    }
    return cm->cache->nodes + number;
}

mleaf_t *CM_LeafNum(cm_t *cm, int number)
{
    if (!cm->cache) {
        return &nullleaf;
    }
    if (number < 0 || number >= cm->cache->numleafs) {
        Com_EPrintf("%s: bad number: %d\n", __func__, number);
        return &nullleaf;
    }
    return cm->cache->leafs + number;
}

//=======================================================================

static cplane_t box_planes[12];
static mnode_t  box_nodes[6];
static mnode_t  *box_headnode;
static mbrush_t box_brush;
static mbrush_t *box_leafbrush;
static mbrushside_t box_brushsides[6];
static mleaf_t  box_leaf;
static mleaf_t  box_emptyleaf;

/*
===================
CM_InitBoxHull

Set up the planes and nodes so that the six floats of a bounding box
can just be stored out and get a proper clipping hull structure.
===================
*/
static void CM_InitBoxHull(void)
{
    int         i;
    int         side;
    mnode_t     *c;
    cplane_t    *p;
    mbrushside_t    *s;

    box_headnode = &box_nodes[0];

    box_brush.numsides = 6;
    box_brush.firstbrushside = &box_brushsides[0];
    box_brush.contents = CONTENTS_MONSTER;

    box_leaf.contents = CONTENTS_MONSTER;
    box_leaf.firstleafbrush = &box_leafbrush;
    box_leaf.numleafbrushes = 1;

    box_leafbrush = &box_brush;

    for (i = 0; i < 6; i++) {
        side = i & 1;

        // brush sides
        s = &box_brushsides[i];
        s->plane = &box_planes[i * 2 + side];
        s->texinfo = &nulltexinfo;

        // nodes
        c = &box_nodes[i];
        c->plane = &box_planes[i * 2];
        c->children[side] = (mnode_t *)&box_emptyleaf;
        if (i != 5)
            c->children[side ^ 1] = &box_nodes[i + 1];
        else
            c->children[side ^ 1] = (mnode_t *)&box_leaf;

        // planes
        p = &box_planes[i * 2];
        p->type = i >> 1;
        p->normal[i >> 1] = 1;

        p = &box_planes[i * 2 + 1];
        p->type = 3 + (i >> 1);
        p->signbits = 1 << (i >> 1);
        p->normal[i >> 1] = -1;
    }
}

/*
===================
CM_HeadnodeForBox

To keep everything totally uniform, bounding boxes are turned into small
BSP trees instead of being compared directly.
===================
*/
mnode_t *CM_HeadnodeForBox(const vec3_t mins, const vec3_t maxs)
{
    box_planes[0].dist = maxs[0];
    box_planes[1].dist = -maxs[0];
    box_planes[2].dist = mins[0];
    box_planes[3].dist = -mins[0];
    box_planes[4].dist = maxs[1];
    box_planes[5].dist = -maxs[1];
    box_planes[6].dist = mins[1];
    box_planes[7].dist = -mins[1];
    box_planes[8].dist = maxs[2];
    box_planes[9].dist = -maxs[2];
    box_planes[10].dist = mins[2];
    box_planes[11].dist = -mins[2];

    return box_headnode;
}

mleaf_t *CM_PointLeaf(cm_t *cm, const vec3_t p)
{
    if (!cm->cache) {
        return &nullleaf;       // server may call this without map loaded
    }
    return BSP_PointLeaf(cm->cache->nodes, p);
}

/*
=============
CM_BoxLeafnums

Fills in a list of all the leafs touched
=============
*/
static int          leaf_count, leaf_maxcount;
static mleaf_t      **leaf_list;
static const vec_t  *leaf_mins, *leaf_maxs;
static mnode_t      *leaf_topnode;

static void CM_BoxLeafs_r(mnode_t *node)
{
    int     s;

    while (node->plane) {
        s = BoxOnPlaneSideFast(leaf_mins, leaf_maxs, node->plane);
        if (s == BOX_INFRONT) {
            node = node->children[0];
        } else if (s == BOX_BEHIND) {
            node = node->children[1];
        } else {
            // go down both
            if (!leaf_topnode) {
                leaf_topnode = node;
            }
            CM_BoxLeafs_r(node->children[0]);
            node = node->children[1];
        }
    }

    if (leaf_count < leaf_maxcount) {
        leaf_list[leaf_count++] = (mleaf_t *)node;
    }
}

static int CM_BoxLeafs_headnode(const vec3_t mins, const vec3_t maxs,
                                mleaf_t **list, int listsize,
                                mnode_t *headnode, mnode_t **topnode)
{
    leaf_list = list;
    leaf_count = 0;
    leaf_maxcount = listsize;
    leaf_mins = mins;
    leaf_maxs = maxs;

    leaf_topnode = NULL;

    CM_BoxLeafs_r(headnode);

    if (topnode)
        *topnode = leaf_topnode;

    return leaf_count;
}

int CM_BoxLeafs(cm_t *cm, const vec3_t mins, const vec3_t maxs,
                mleaf_t **list, int listsize, mnode_t **topnode)
{
    if (!cm->cache)     // map not loaded
        return 0;
    return CM_BoxLeafs_headnode(mins, maxs, list, listsize, cm->cache->nodes, topnode);
}

/*
==================
CM_PointContents
==================
*/
int CM_PointContents(const vec3_t p, mnode_t *headnode)
{
    if (!headnode)
        return 0;
    return BSP_PointLeaf(headnode, p)->contents;
}

/*
==================
CM_TransformedPointContents

Handles offseting and rotation of the end points for moving and
rotating entities
==================
*/
int CM_TransformedPointContents(const vec3_t p, mnode_t *headnode, const vec3_t origin, const vec3_t angles)
{
    vec3_t      p_l;
    vec3_t      axis[3];

    if (!headnode) {
        return 0;
    }

    // subtract origin offset
    VectorSubtract(p, origin, p_l);

    // rotate start and end into the models frame of reference
    if (headnode != box_headnode && !VectorEmpty(angles)) {
        AnglesToAxis(angles, axis);
        RotatePoint(p_l, axis);
    }

    return BSP_PointLeaf(headnode, p_l)->contents;
}

/*
===============================================================================

BOX TRACING

===============================================================================
*/

// 1/32 epsilon to keep floating point happy
#define DIST_EPSILON    0.03125f

static vec3_t   trace_start, trace_end;
static vec3_t   trace_offsets[8];
static vec3_t   trace_extents;

static trace_t  *trace_trace;
static int      trace_contents;
static bool     trace_ispoint;      // optimized case

/*
================
CM_ClipBoxToBrush
================
*/
static void CM_ClipBoxToBrush(const vec3_t p1, const vec3_t p2, trace_t *trace, mbrush_t *brush)
{
    int         i;
    cplane_t    *plane, *clipplane;
    float       dist;
    float       enterfrac, leavefrac;
    float       d1, d2;
    bool        getout, startout;
    float       f;
    mbrushside_t    *side, *leadside;

    if (!brush->numsides)
        return;

    enterfrac = -1;
    leavefrac = 1;
    clipplane = NULL;

    getout = false;
    startout = false;
    leadside = NULL;

    side = brush->firstbrushside;
    for (i = 0; i < brush->numsides; i++, side++) {
        plane = side->plane;

        // FIXME: special case for axial
        if (!trace_ispoint) {
            // general box case
            // push the plane out apropriately for mins/maxs
            dist = DotProduct(trace_offsets[plane->signbits], plane->normal);
            dist = plane->dist - dist;
        } else {
            // special point case
            dist = plane->dist;
        }

        d1 = DotProduct(p1, plane->normal) - dist;
        d2 = DotProduct(p2, plane->normal) - dist;

        if (d2 > 0)
            getout = true; // endpoint is not in solid
        if (d1 > 0)
            startout = true;

        // if completely in front of face, no intersection
        if (d1 > 0 && d2 >= d1)
            return;

        if (d1 <= 0 && d2 <= 0)
            continue;

        // crosses face
        if (d1 > d2) {
            // enter
            f = (d1 - DIST_EPSILON) / (d1 - d2);
            if (f > enterfrac) {
                enterfrac = f;
                clipplane = plane;
                leadside = side;
            }
        } else {
            // leave
            f = (d1 + DIST_EPSILON) / (d1 - d2);
            if (f < leavefrac)
                leavefrac = f;
        }
    }

    if (!startout) {
        // original point was inside brush
        trace->startsolid = true;
        if (!getout) {
            trace->allsolid = true;
            if (!map_allsolid_bug->integer) {
                // original Q2 didn't set these
                trace->fraction = 0;
                trace->contents = brush->contents;
            }
        }
        return;
    }
    if (enterfrac < leavefrac) {
        if (enterfrac > -1 && enterfrac < trace->fraction) {
            if (enterfrac < 0)
                enterfrac = 0;
            trace->fraction = enterfrac;
            trace->plane = *clipplane;
            trace->surface = &(leadside->texinfo->c);
            trace->contents = brush->contents;
        }
    }
}

/*
================
CM_TestBoxInBrush
================
*/
static void CM_TestBoxInBrush(const vec3_t p1, trace_t *trace, mbrush_t *brush)
{
    int         i;
    cplane_t    *plane;
    float       dist;
    float       d1;
    mbrushside_t    *side;

    if (!brush->numsides)
        return;

    side = brush->firstbrushside;
    for (i = 0; i < brush->numsides; i++, side++) {
        plane = side->plane;

        // FIXME: special case for axial
        // general box case
        // push the plane out apropriately for mins/maxs
        dist = DotProduct(trace_offsets[plane->signbits], plane->normal);
        dist = plane->dist - dist;

        d1 = DotProduct(p1, plane->normal) - dist;

        // if completely in front of face, no intersection
        if (d1 > 0)
            return;
    }

    // inside this brush
    trace->startsolid = trace->allsolid = true;
    trace->fraction = 0;
    trace->contents = brush->contents;
}

/*
================
CM_TraceToLeaf
================
*/
static void CM_TraceToLeaf(mleaf_t *leaf)
{
    int         k;
    mbrush_t    *b, **leafbrush;

    if (!(leaf->contents & trace_contents))
        return;
    // trace line against all brushes in the leaf
    leafbrush = leaf->firstleafbrush;
    for (k = 0; k < leaf->numleafbrushes; k++, leafbrush++) {
        b = *leafbrush;
        if (b->checkcount == checkcount)
            continue;   // already checked this brush in another leaf
        b->checkcount = checkcount;

        if (!(b->contents & trace_contents))
            continue;
        CM_ClipBoxToBrush(trace_start, trace_end, trace_trace, b);
        if (!trace_trace->fraction)
            return;
    }
}

/*
================
CM_TestInLeaf
================
*/
static void CM_TestInLeaf(mleaf_t *leaf)
{
    int         k;
    mbrush_t    *b, **leafbrush;

    if (!(leaf->contents & trace_contents))
        return;
    // trace line against all brushes in the leaf
    leafbrush = leaf->firstleafbrush;
    for (k = 0; k < leaf->numleafbrushes; k++, leafbrush++) {
        b = *leafbrush;
        if (b->checkcount == checkcount)
            continue;   // already checked this brush in another leaf
        b->checkcount = checkcount;

        if (!(b->contents & trace_contents))
            continue;
        CM_TestBoxInBrush(trace_start, trace_trace, b);
        if (!trace_trace->fraction)
            return;
    }
}

/*
==================
CM_RecursiveHullCheck

==================
*/
static void CM_RecursiveHullCheck(mnode_t *node, float p1f, float p2f, const vec3_t p1, const vec3_t p2)
{
    cplane_t    *plane;
    float       t1, t2, offset;
    float       frac, frac2;
    float       idist;
    vec3_t      mid;
    int         side;
    float       midf;

    if (trace_trace->fraction <= p1f)
        return;     // already hit something nearer

recheck:
    // if plane is NULL, we are in a leaf node
    plane = node->plane;
    if (!plane) {
        CM_TraceToLeaf((mleaf_t *)node);
        return;
    }

    //
    // find the point distances to the seperating plane
    // and the offset for the size of the box
    //
    if (plane->type < 3) {
        t1 = p1[plane->type] - plane->dist;
        t2 = p2[plane->type] - plane->dist;
        offset = trace_extents[plane->type];
    } else {
        t1 = PlaneDiff(p1, plane);
        t2 = PlaneDiff(p2, plane);
        if (trace_ispoint)
            offset = 0;
        else
            offset = fabsf(trace_extents[0] * plane->normal[0]) +
                     fabsf(trace_extents[1] * plane->normal[1]) +
                     fabsf(trace_extents[2] * plane->normal[2]);
    }

    // see which sides we need to consider
    if (t1 >= offset && t2 >= offset) {
        node = node->children[0];
        goto recheck;
    }
    if (t1 < -offset && t2 < -offset) {
        node = node->children[1];
        goto recheck;
    }

    // put the crosspoint DIST_EPSILON pixels on the near side
    if (t1 < t2) {
        idist = 1.0f / (t1 - t2);
        side = 1;
        frac2 = (t1 + offset + DIST_EPSILON) * idist;
        frac = (t1 - offset + DIST_EPSILON) * idist;
    } else if (t1 > t2) {
        idist = 1.0f / (t1 - t2);
        side = 0;
        frac2 = (t1 - offset - DIST_EPSILON) * idist;
        frac = (t1 + offset + DIST_EPSILON) * idist;
    } else {
        side = 0;
        frac = 1;
        frac2 = 0;
    }

    frac = Q_clipf(frac, 0, 1);
    frac2 = Q_clipf(frac2, 0, 1);

    // move up to the node
    midf = p1f + (p2f - p1f) * frac;
    LerpVector(p1, p2, frac, mid);

    CM_RecursiveHullCheck(node->children[side], p1f, midf, p1, mid);

    // go past the node
    midf = p1f + (p2f - p1f) * frac2;
    LerpVector(p1, p2, frac2, mid);

    CM_RecursiveHullCheck(node->children[side ^ 1], midf, p2f, mid, p2);
}

//======================================================================

/*
==================
CM_BoxTrace
==================
*/
void CM_BoxTrace(trace_t *trace,
                 const vec3_t start, const vec3_t end,
                 const vec3_t mins, const vec3_t maxs,
                 mnode_t *headnode, int brushmask)
{
    const vec_t *bounds[2] = { mins, maxs };
    int i, j;

    checkcount++;       // for multi-check avoidance

    // fill in a default trace
    trace_trace = trace;
    memset(trace_trace, 0, sizeof(*trace_trace));
    trace_trace->fraction = 1;
    trace_trace->surface = &(nulltexinfo.c);

    if (!headnode) {
        return;
    }

    trace_contents = brushmask;
    VectorCopy(start, trace_start);
    VectorCopy(end, trace_end);
    for (i = 0; i < 8; i++)
        for (j = 0; j < 3; j++)
            trace_offsets[i][j] = bounds[(i >> j) & 1][j];

    //
    // check for position test special case
    //
    if (VectorCompare(start, end)) {
        mleaf_t     *leafs[1024];
        int         numleafs;
        vec3_t      c1, c2;

        VectorAdd(start, mins, c1);
        VectorAdd(start, maxs, c2);
        for (i = 0; i < 3; i++) {
            c1[i] -= 1;
            c2[i] += 1;
        }

        numleafs = CM_BoxLeafs_headnode(c1, c2, leafs, q_countof(leafs), headnode, NULL);
        for (i = 0; i < numleafs; i++) {
            CM_TestInLeaf(leafs[i]);
            if (trace_trace->allsolid)
                break;
        }
        VectorCopy(start, trace_trace->endpos);
        return;
    }

    //
    // check for point special case
    //
    if (VectorEmpty(mins) && VectorEmpty(maxs)) {
        trace_ispoint = true;
        VectorClear(trace_extents);
    } else {
        trace_ispoint = false;
        trace_extents[0] = max(-mins[0], maxs[0]);
        trace_extents[1] = max(-mins[1], maxs[1]);
        trace_extents[2] = max(-mins[2], maxs[2]);
    }

    //
    // general sweeping through world
    //
    CM_RecursiveHullCheck(headnode, 0, 1, start, end);

    if (trace_trace->fraction == 1)
        VectorCopy(end, trace_trace->endpos);
    else
        LerpVector(start, end, trace_trace->fraction, trace_trace->endpos);
}

/*
==================
CM_TransformedBoxTrace

Handles offseting and rotation of the end points for moving and
rotating entities
==================
*/
void CM_TransformedBoxTrace(trace_t *trace,
                            const vec3_t start, const vec3_t end,
                            const vec3_t mins, const vec3_t maxs,
                            mnode_t *headnode, int brushmask,
                            const vec3_t origin, const vec3_t angles)
{
    vec3_t      start_l, end_l;
    vec3_t      axis[3];
    bool        rotated;

    // subtract origin offset
    VectorSubtract(start, origin, start_l);
    VectorSubtract(end, origin, end_l);

    // rotate start and end into the models frame of reference
    rotated = headnode != box_headnode && !VectorEmpty(angles);
    if (rotated) {
        AnglesToAxis(angles, axis);
        RotatePoint(start_l, axis);
        RotatePoint(end_l, axis);
    }

    // sweep the box through the model
    CM_BoxTrace(trace, start_l, end_l, mins, maxs, headnode, brushmask);

    // rotate plane normal into the worlds frame of reference
    if (rotated && trace->fraction != 1.0f) {
        TransposeAxis(axis);
        RotatePoint(trace->plane.normal, axis);
    }

    // FIXME: offset plane distance?

    LerpVector(start, end, trace->fraction, trace->endpos);
}

void CM_ClipEntity(trace_t *dst, const trace_t *src, struct edict_s *ent)
{
    dst->allsolid |= src->allsolid;
    dst->startsolid |= src->startsolid;
    if (src->fraction < dst->fraction) {
        dst->fraction = src->fraction;
        VectorCopy(src->endpos, dst->endpos);
        dst->plane = src->plane;
        dst->surface = src->surface;
        dst->contents |= src->contents;
        dst->ent = ent;
    }
}

/*
===============================================================================

AREAPORTALS

===============================================================================
*/

static void FloodArea_r(cm_t *cm, int number, int floodnum)
{
    int i;
    mareaportal_t *p;
    marea_t *area;

    area = &cm->cache->areas[number];
    if (area->floodvalid == floodvalid) {
        if (cm->floodnums[number] == floodnum)
            return;
        Com_Error(ERR_DROP, "FloodArea_r: reflooded");
    }

    cm->floodnums[number] = floodnum;
    area->floodvalid = floodvalid;
    p = area->firstareaportal;
    for (i = 0; i < area->numareaportals; i++, p++) {
        if (cm->portalopen[p->portalnum])
            FloodArea_r(cm, p->otherarea, floodnum);
    }
}

static void FloodAreaConnections(cm_t *cm)
{
    int     i;
    marea_t *area;
    int     floodnum;

    // all current floods are now invalid
    floodvalid++;
    floodnum = 0;

    // area 0 is not used
    for (i = 1; i < cm->cache->numareas; i++) {
        area = &cm->cache->areas[i];
        if (area->floodvalid == floodvalid)
            continue;       // already flooded into
        floodnum++;
        FloodArea_r(cm, i, floodnum);
    }
}

void CM_SetAreaPortalState(cm_t *cm, int portalnum, bool open)
{
    if (!cm->cache) {
        return;
    }

    if (portalnum < 0 || portalnum >= cm->cache->numportals) {
        Com_DPrintf("%s: portalnum %d is out of range\n", __func__, portalnum);
        return;
    }

    cm->portalopen[portalnum] = open;
    FloodAreaConnections(cm);
}

bool CM_AreasConnected(cm_t *cm, int area1, int area2)
{
    bsp_t *cache = cm->cache;

    if (!cache) {
        return false;
    }
    if (map_noareas->integer) {
        return true;
    }
    if (area1 < 1 || area2 < 1) {
        return false;
    }
    if (area1 >= cache->numareas || area2 >= cache->numareas) {
        Com_EPrintf("%s: area > numareas\n", __func__);
        return false;
    }
    if (cm->floodnums[area1] == cm->floodnums[area2]) {
        return true;
    }

    return false;
}

/*
=================
CM_WriteAreaBits

Writes a length byte followed by a bit vector of all the areas
that area in the same flood as the area parameter

This is used by the client refreshes to cull visibility
=================
*/
int CM_WriteAreaBits(cm_t *cm, byte *buffer, int area)
{
    bsp_t   *cache = cm->cache;
    int     i;
    int     floodnum;
    int     bytes;

    if (!cache) {
        return 0;
    }

    bytes = (cache->numareas + 7) >> 3;
    Q_assert(bytes <= MAX_MAP_AREA_BYTES);

    if (map_noareas->integer || !area) {
        // for debugging, send everything
        memset(buffer, 255, bytes);
    } else {
        memset(buffer, 0, bytes);

        floodnum = cm->floodnums[area];
        for (i = 0; i < cache->numareas; i++) {
            if (cm->floodnums[i] == floodnum) {
                Q_SetBit(buffer, i);
            }
        }
    }

    return bytes;
}

int CM_WritePortalBits(cm_t *cm, byte *buffer)
{
    int     i, bytes, numportals;

    if (!cm->cache) {
        return 0;
    }

    numportals = min(cm->cache->numportals, MAX_MAP_PORTAL_BYTES << 3);

    bytes = (numportals + 7) >> 3;
    memset(buffer, 0, bytes);
    for (i = 0; i < numportals; i++) {
        if (cm->portalopen[i]) {
            Q_SetBit(buffer, i);
        }
    }

    return bytes;
}

void CM_SetPortalStates(cm_t *cm, byte *buffer, int bytes)
{
    int     i, numportals;

    if (!cm->cache) {
        return;
    }

    numportals = min(cm->cache->numportals, bytes << 3);
    for (i = 0; i < numportals; i++) {
        cm->portalopen[i] = Q_IsBitSet(buffer, i);
    }
    for (; i < cm->cache->numportals; i++) {
        cm->portalopen[i] = true;
    }

    FloodAreaConnections(cm);
}

/*
=============
CM_HeadnodeVisible

Returns true if any leaf under headnode has a cluster that
is potentially visible
=============
*/
bool CM_HeadnodeVisible(mnode_t *node, byte *visbits)
{
    mleaf_t *leaf;
    int     cluster;

    while (node->plane) {
        if (CM_HeadnodeVisible(node->children[0], visbits))
            return true;
        node = node->children[1];
    }

    leaf = (mleaf_t *)node;
    cluster = leaf->cluster;
    if (cluster == -1)
        return false;
    if (Q_IsBitSet(visbits, cluster))
        return true;
    return false;
}

/*
============
CM_FatPVS

The client will interpolate the view position,
so we can't use a single PVS point
===========
*/
byte *CM_FatPVS(cm_t *cm, byte *mask, const vec3_t org, int vis)
{
    byte    temp[VIS_MAX_BYTES];
    mleaf_t *leafs[64];
    int     clusters[64];
    int     i, j, count, longs;
    size_t  *src, *dst;
    vec3_t  mins, maxs;

    if (!cm->cache) {   // map not loaded
        return memset(mask, 0, VIS_MAX_BYTES);
    }
    if (!cm->cache->vis) {
        return memset(mask, 0xff, VIS_MAX_BYTES);
    }

    for (i = 0; i < 3; i++) {
        mins[i] = org[i] - 8;
        maxs[i] = org[i] + 8;
    }

    count = CM_BoxLeafs(cm, mins, maxs, leafs, q_countof(leafs), NULL);
    Q_assert(count > 0);

    // convert leafs to clusters
    for (i = 0; i < count; i++) {
        clusters[i] = leafs[i]->cluster;
    }

    BSP_ClusterVis(cm->cache, mask, clusters[0], vis);
    longs = VIS_FAST_LONGS(cm->cache);

    // or in all the other leaf bits
    for (i = 1; i < count; i++) {
        for (j = 0; j < i; j++) {
            if (clusters[i] == clusters[j]) {
                goto nextleaf; // already have the cluster we want
            }
        }
        src = (size_t *)BSP_ClusterVis(cm->cache, temp, clusters[i], vis);
        dst = (size_t *)mask;
        for (j = 0; j < longs; j++) {
            *dst++ |= *src++;
        }

nextleaf:;
    }

    return mask;
}

/*
=============
CM_Init
=============
*/
void CM_Init(void)
{
    CM_InitBoxHull();

    nullleaf.cluster = -1;

    map_noareas = Cvar_Get("map_noareas", "0", 0);
    map_allsolid_bug = Cvar_Get("map_allsolid_bug", "1", 0);
    map_override_path = Cvar_Get("map_override_path", "", 0);
}

