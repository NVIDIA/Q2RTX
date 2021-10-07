/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2008 Andrey Nazarov
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

// bsp.c -- model loading

#include "shared/shared.h"
#include "shared/list.h"
#include "common/cvar.h"
#include "common/cmd.h"
#include "common/common.h"
#include "common/files.h"
#include "common/bsp.h"
#include "common/math.h"
#include "common/utils.h"
#include "common/mdfour.h"
#include "system/hunk.h"

extern mtexinfo_t nulltexinfo;

static cvar_t *map_visibility_patch;

/*
===============================================================================

                    LUMP LOADING

===============================================================================
*/

#define ALLOC(size) \
    Hunk_Alloc(&bsp->hunk, size)

#define LOAD(func) \
    static qerror_t BSP_Load##func(bsp_t *bsp, void *base, size_t count)

// QBSP
#define LOAD_EXT(func) \
    static qerror_t BSP_QBSP_Load##func(bsp_t *bsp, void *base, size_t count)

#define DEBUG(msg) \
    Com_DPrintf("%s: %s\n", __func__, msg)

LOAD(Visibility)
{
    uint32_t numclusters, bitofs;
    int i, j;

    if (!count) {
        return Q_ERR_SUCCESS;
    }

    if (count < 4) {
        DEBUG("too small header");
        return Q_ERR_TOO_FEW;
    }

    bsp->numvisibility = count;
    bsp->vis = ALLOC(count);
    memcpy(bsp->vis, base, count);

    numclusters = LittleLong(bsp->vis->numclusters);
    if (numclusters > (bsp->extended ? MAX_QBSP_MAP_LEAFS : MAX_MAP_LEAFS)) {
        DEBUG("bad numclusters");
        return Q_ERR_TOO_MANY;
    }

    if (numclusters > (count - 4) / 8) {
        DEBUG("too small header");
        return Q_ERR_TOO_FEW;
    }

    bsp->vis->numclusters = numclusters;
    bsp->visrowsize = (numclusters + 7) >> 3;

    for (i = 0; i < numclusters; i++) {
        for (j = 0; j < 2; j++) {
            bitofs = LittleLong(bsp->vis->bitofs[i][j]);
            if (bitofs >= count) {
                DEBUG("bad bitofs");
                return Q_ERR_BAD_INDEX;
            }
            bsp->vis->bitofs[i][j] = bitofs;
        }
    }

    return Q_ERR_SUCCESS;
}

LOAD(Texinfo)
{
    dtexinfo_t  *in;
    mtexinfo_t  *out;
    int         i;
#if USE_REF
    int         j, k;
    int32_t     next;
    mtexinfo_t  *step;
#endif

    bsp->numtexinfo = count;
    bsp->texinfo = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->texinfo;
    for (i = 0; i < count; i++, in++, out++) {
        memcpy(out->c.name, in->texture, sizeof(out->c.name));
        out->c.name[sizeof(out->c.name) - 1] = 0;
        memcpy(out->name, in->texture, sizeof(out->name));
        out->name[sizeof(out->name) - 1] = 0;
        out->c.flags = LittleLong(in->flags);
        out->c.value = LittleLong(in->value);

#if USE_REF
        out->radiance = in->value;
        for (j = 0; j < 2; j++) {
            for (k = 0; k < 3; k++) {
                out->axis[j][k] = LittleFloat(in->vecs[j][k]);
            }
            out->offset[j] = LittleFloat(in->vecs[j][k]);
        }

        next = (int32_t)LittleLong(in->nexttexinfo);
        if (next > 0) {
            if (next >= count) {
                DEBUG("bad anim chain");
                return Q_ERR_BAD_INDEX;
            }
            out->next = bsp->texinfo + next;
        } else {
            out->next = NULL;
        }
#endif
    }

#if USE_REF
    // count animation frames
    out = bsp->texinfo;
    for (i = 0; i < count; i++, out++) {
        out->numframes = 1;
        for (step = out->next; step && step != out; step = step->next) {
            if (out->numframes == count) {
                DEBUG("infinite anim chain");
                return Q_ERR_INFINITE_LOOP;
            }
            out->numframes++;
        }
    }
#endif

    return Q_ERR_SUCCESS;
}

LOAD(Planes)
{
    dplane_t    *in;
    cplane_t    *out;
    int         i, j;

    bsp->numplanes = count;
    bsp->planes = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->planes;
    for (i = 0; i < count; i++, in++, out++) {
        for (j = 0; j < 3; j++) {
            out->normal[j] = LittleFloat(in->normal[j]);
        }
        out->dist = LittleFloat(in->dist);
        SetPlaneType(out);
        SetPlaneSignbits(out);
    }

    return Q_ERR_SUCCESS;
}

LOAD(BrushSides)
{
    dbrushside_t    *in;
    mbrushside_t    *out;
    int         i;
    uint16_t    planenum, texinfo;

    bsp->numbrushsides = count;
    bsp->brushsides = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->brushsides;
    for (i = 0; i < count; i++, in++, out++) {
        planenum = LittleShort(in->planenum);
        if (planenum >= bsp->numplanes) {
            DEBUG("bad planenum");
            return Q_ERR_BAD_INDEX;
        }
        out->plane = bsp->planes + planenum;
        texinfo = LittleShort(in->texinfo);
        if (texinfo == (uint16_t)-1) {
            out->texinfo = &nulltexinfo;
        } else {
            if (texinfo >= bsp->numtexinfo) {
                DEBUG("bad texinfo");
                return Q_ERR_BAD_INDEX;
            }
            out->texinfo = bsp->texinfo + texinfo;
        }
    }

    return Q_ERR_SUCCESS;
}

LOAD_EXT(BrushSides)
{
    dbrushside_qbsp_t    *in;
    mbrushside_t    *out;
    int         i;
    uint32_t    planenum, texinfo;

    bsp->numbrushsides = count;
    bsp->brushsides = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->brushsides;
    for (i = 0; i < count; i++, in++, out++) {
        planenum = LittleLong(in->planenum);
        if (planenum >= bsp->numplanes) {
            DEBUG("bad planenum");
            return Q_ERR_BAD_INDEX;
        }
        out->plane = bsp->planes + planenum;
        texinfo = LittleLong(in->texinfo);
        if (texinfo == (uint32_t)-1) {
            out->texinfo = &nulltexinfo;
        } else {
            if (texinfo >= bsp->numtexinfo) {
                DEBUG("bad texinfo");
                return Q_ERR_BAD_INDEX;
            }
            out->texinfo = bsp->texinfo + texinfo;
        }
    }

    return Q_ERR_SUCCESS;
}

LOAD(Brushes)
{
    dbrush_t    *in;
    mbrush_t    *out;
    int         i;
    uint32_t    firstside, numsides, lastside;

    bsp->numbrushes = count;
    bsp->brushes = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->brushes;
    for (i = 0; i < count; i++, out++, in++) {
        firstside = LittleLong(in->firstside);
        numsides = LittleLong(in->numsides);
        lastside = firstside + numsides;
        if (lastside < firstside || lastside > bsp->numbrushsides) {
            DEBUG("bad brushsides");
            return Q_ERR_BAD_INDEX;
        }
        out->firstbrushside = bsp->brushsides + firstside;
        out->numsides = numsides;
        out->contents = LittleLong(in->contents);
        out->checkcount = 0;
    }

    return Q_ERR_SUCCESS;
}

LOAD(LeafBrushes)
{
    uint16_t    *in;
    mbrush_t    **out;
    int         i;
    uint16_t    brushnum;

    bsp->numleafbrushes = count;
    bsp->leafbrushes = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->leafbrushes;
    for (i = 0; i < count; i++, in++, out++) {
        brushnum = LittleShort(*in);
        if (brushnum >= bsp->numbrushes) {
            DEBUG("bad brushnum");
            return Q_ERR_BAD_INDEX;
        }
        *out = bsp->brushes + brushnum;
    }

    return Q_ERR_SUCCESS;
}

LOAD_EXT(LeafBrushes)
{
    uint32_t    *in;
    mbrush_t    **out;
    int         i;
    uint32_t    brushnum;

    bsp->numleafbrushes = count;
    bsp->leafbrushes = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->leafbrushes;
    for (i = 0; i < count; i++, in++, out++) {
        brushnum = LittleLong(*in);
        if (brushnum >= bsp->numbrushes) {
            DEBUG("bad brushnum");
            return Q_ERR_BAD_INDEX;
        }
        *out = bsp->brushes + brushnum;
    }

    return Q_ERR_SUCCESS;
}


#if USE_REF
LOAD(Lightmap)
{
    if (!count) {
        return Q_ERR_SUCCESS;
    }

    bsp->numlightmapbytes = count;
    bsp->lightmap = ALLOC(count);

    memcpy(bsp->lightmap, base, count);

    return Q_ERR_SUCCESS;
}

LOAD(Vertices)
{
    dvertex_t   *in;
    mvertex_t   *out;
    int         i, j;

    bsp->numvertices = count;
    bsp->vertices = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->vertices;
    for (i = 0; i < count; i++, out++, in++) {
        for (j = 0; j < 3; j++) {
            out->point[j] = LittleFloat(in->point[j]);
        }
    }

    return Q_ERR_SUCCESS;
}

LOAD(Edges)
{
    dedge_t     *in;
    medge_t     *out;
    int         i, j;
    uint16_t    vertnum;

    bsp->numedges = count;
    bsp->edges = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->edges;
    for (i = 0; i < count; i++, out++, in++) {
        for (j = 0; j < 2; j++) {
            vertnum = LittleShort(in->v[j]);
            if (vertnum >= bsp->numvertices) {
                DEBUG("bad vertnum");
                return Q_ERR_BAD_INDEX;
            }
            out->v[j] = bsp->vertices + vertnum;
        }
    }

    return Q_ERR_SUCCESS;
}

LOAD_EXT(Edges)
{
    dedge_qbsp_t     *in;
    medge_t     *out;
    int         i, j;
    uint32_t    vertnum;

    bsp->numedges = count;
    bsp->edges = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->edges;
    for (i = 0; i < count; i++, out++, in++) {
        for (j = 0; j < 2; j++) {
            vertnum = LittleLong(in->v[j]);
            if (vertnum >= bsp->numvertices) {
                DEBUG("bad vertnum");
                return Q_ERR_BAD_INDEX;
            }
            out->v[j] = bsp->vertices + vertnum;
        }
    }

    return Q_ERR_SUCCESS;
}

LOAD(SurfEdges)
{
    int         *in;
    msurfedge_t *out;
    int         i, vert;
    int32_t     index;

    bsp->numsurfedges = count;
    bsp->surfedges = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->surfedges;
    for (i = 0; i < count; i++, out++, in++) {
        index = (int32_t)LittleLong(*in);

        vert = 0;
        if (index < 0) {
            index = -index;
            vert = 1;
        }

        if (index >= bsp->numedges) {
            DEBUG("bad edgenum");
            return Q_ERR_BAD_INDEX;
        }

        out->edge = bsp->edges + index;
        out->vert = vert;
    }

    return Q_ERR_SUCCESS;
}

LOAD(Faces)
{
    dface_t     *in;
    mface_t     *out;
    int         i, j;
    uint32_t    firstedge, numedges, lastedge;
    uint16_t    planenum, texinfo, side;
    uint32_t    lightofs;

    bsp->numfaces = count;
    bsp->faces = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->faces;
    for (i = 0; i < count; i++, in++, out++) {
        firstedge = LittleLong(in->firstedge);
        numedges = LittleShort(in->numedges);
        lastedge = firstedge + numedges;
        if (numedges < 3) {
            DEBUG("bad surfedges");
            return Q_ERR_TOO_FEW;
        }
        if (numedges > 4096) {
            DEBUG("bad surfedges");
            return Q_ERR_TOO_MANY;
        }
        if (lastedge < firstedge || lastedge > bsp->numsurfedges) {
            DEBUG("bad surfedges");
            return Q_ERR_BAD_INDEX;
        }
        out->firstsurfedge = bsp->surfedges + firstedge;
        out->numsurfedges = numedges;

        planenum = LittleShort(in->planenum);
        if (planenum >= bsp->numplanes) {
            DEBUG("bad planenum");
            return Q_ERR_BAD_INDEX;
        }
        out->plane = bsp->planes + planenum;

        texinfo = LittleShort(in->texinfo);
        if (texinfo >= bsp->numtexinfo) {
            DEBUG("bad texinfo");
            return Q_ERR_BAD_INDEX;
        }
        out->texinfo = bsp->texinfo + texinfo;

        for (j = 0; j < MAX_LIGHTMAPS && in->styles[j] != 255; j++) {
            out->styles[j] = in->styles[j];
        }
        out->numstyles = j;
        for (; j < MAX_LIGHTMAPS; j++) {
            out->styles[j] = 255;
        }

        lightofs = LittleLong(in->lightofs);
        if (lightofs == (uint32_t)-1 || bsp->numlightmapbytes == 0) {
            out->lightmap = NULL;
        } else {
            if (lightofs >= bsp->numlightmapbytes) {
                DEBUG("bad lightofs");
                return Q_ERR_BAD_INDEX;
            }
            out->lightmap = bsp->lightmap + lightofs;
        }

        side = LittleShort(in->side);
        out->drawflags = side & DSURF_PLANEBACK;
    }

    return Q_ERR_SUCCESS;
}

LOAD_EXT(Faces)
{
    dface_qbsp_t     *in;
    mface_t     *out;
    int         i, j;
    uint32_t    firstedge, numedges, lastedge;
    uint32_t    planenum, texinfo, side;
    uint32_t    lightofs;

    bsp->numfaces = count;
    bsp->faces = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->faces;
    for (i = 0; i < count; i++, in++, out++) {
        firstedge = LittleLong(in->firstedge);
        numedges = LittleLong(in->numedges);
        lastedge = firstedge + numedges;
        if (numedges < 3) {
            DEBUG("bad surfedges");
            return Q_ERR_TOO_FEW;
        }
        if (numedges > 4096) {
            DEBUG("bad surfedges");
            return Q_ERR_TOO_MANY;
        }
        if (lastedge < firstedge || lastedge > bsp->numsurfedges) {
            DEBUG("bad surfedges");
            return Q_ERR_BAD_INDEX;
        }
        out->firstsurfedge = bsp->surfedges + firstedge;
        out->numsurfedges = numedges;

        planenum = LittleLong(in->planenum);
        if (planenum >= bsp->numplanes) {
            DEBUG("bad planenum");
            return Q_ERR_BAD_INDEX;
        }
        out->plane = bsp->planes + planenum;

        texinfo = LittleLong(in->texinfo);
        if (texinfo >= bsp->numtexinfo) {
            DEBUG("bad texinfo");
            return Q_ERR_BAD_INDEX;
        }
        out->texinfo = bsp->texinfo + texinfo;

        for (j = 0; j < MAX_LIGHTMAPS && in->styles[j] != 255; j++) {
            out->styles[j] = in->styles[j];
        }
        out->numstyles = j;
        for (; j < MAX_LIGHTMAPS; j++) {
            out->styles[j] = 255;
        }

        lightofs = LittleLong(in->lightofs);
        if (lightofs == (uint32_t)-1 || bsp->numlightmapbytes == 0) {
            out->lightmap = NULL;
        } else {
            if (lightofs >= bsp->numlightmapbytes) {
                DEBUG("bad lightofs");
                return Q_ERR_BAD_INDEX;
            }
            out->lightmap = bsp->lightmap + lightofs;
        }

        side = LittleLong(in->side);
        out->drawflags = side & DSURF_PLANEBACK;
    }

    return Q_ERR_SUCCESS;
}

LOAD(LeafFaces)
{
    uint16_t    *in;
    mface_t     **out;
    int         i;
    uint16_t    facenum;

    bsp->numleaffaces = count;
    bsp->leaffaces = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->leaffaces;
    for (i = 0; i < count; i++, in++, out++) {
        facenum = LittleShort(*in);
        if (facenum >= bsp->numfaces) {
            DEBUG("bad facenum");
            return Q_ERR_BAD_INDEX;
        }
        *out = bsp->faces + facenum;
    }

    return Q_ERR_SUCCESS;
}

LOAD_EXT(LeafFaces)
{
    uint32_t    *in;
    mface_t     **out;
    int         i;
    uint32_t    facenum;

    bsp->numleaffaces = count;
    bsp->leaffaces = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->leaffaces;
    for (i = 0; i < count; i++, in++, out++) {
        facenum = LittleLong(*in);
        if (facenum >= bsp->numfaces) {
            DEBUG("bad facenum");
            return Q_ERR_BAD_INDEX;
        }
        *out = bsp->faces + facenum;
    }

    return Q_ERR_SUCCESS;
}
#endif

LOAD(Leafs)
{
    dleaf_t     *in;
    mleaf_t     *out;
    int         i;
    uint16_t    cluster, area;
    uint16_t    firstleafbrush, numleafbrushes, lastleafbrush;
#if USE_REF
    int         j;
    uint16_t    firstleafface, numleaffaces, lastleafface;
#endif

    if (!count) {
        DEBUG("map with no leafs");
        return Q_ERR_TOO_FEW;
    }

    bsp->numleafs = count;
    bsp->leafs = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->leafs;
    for (i = 0; i < count; i++, in++, out++) {
        out->plane = NULL;
        out->contents = LittleLong(in->contents);
        cluster = LittleShort(in->cluster);
        if (cluster == (uint16_t)-1) {
            // solid leafs use special -1 cluster
            out->cluster = -1;
        } else if (bsp->vis == NULL) {
            // map has no vis, use 0 as a default cluster
            out->cluster = 0;
        } else {
            // validate cluster
            if (cluster >= bsp->vis->numclusters) {
                DEBUG("bad cluster");
                return Q_ERR_BAD_INDEX;
            }
            out->cluster = cluster;
        }

        area = LittleShort(in->area);
        if (area >= bsp->numareas) {
            DEBUG("bad area");
            return Q_ERR_BAD_INDEX;
        }
        out->area = area;

        firstleafbrush = LittleShort(in->firstleafbrush);
        numleafbrushes = LittleShort(in->numleafbrushes);
        lastleafbrush = firstleafbrush + numleafbrushes;
        if (lastleafbrush < firstleafbrush || lastleafbrush > bsp->numleafbrushes) {
            DEBUG("bad leafbrushes");
            return Q_ERR_BAD_INDEX;
        }
        out->firstleafbrush = bsp->leafbrushes + firstleafbrush;
        out->numleafbrushes = numleafbrushes;

#if USE_REF
        firstleafface = LittleShort(in->firstleafface);
        numleaffaces = LittleShort(in->numleaffaces);
        lastleafface = firstleafface + numleaffaces;
        if (lastleafface < firstleafface || lastleafface > bsp->numleaffaces) {
            DEBUG("bad leaffaces");
            return Q_ERR_BAD_INDEX;
        }
        out->firstleafface = bsp->leaffaces + firstleafface;
        out->numleaffaces = numleaffaces;

        for (j = 0; j < 3; j++) {
            out->mins[j] = (int16_t)LittleShort(in->mins[j]);
            out->maxs[j] = (int16_t)LittleShort(in->maxs[j]);
        }

        out->parent = NULL;
        out->visframe = -1;
#endif
    }

    if (bsp->leafs[0].contents != CONTENTS_SOLID) {
        DEBUG("map leaf 0 is not CONTENTS_SOLID");
        return Q_ERR_INVALID_FORMAT;
    }

    return Q_ERR_SUCCESS;
}

LOAD_EXT(Leafs)
{
    dleaf_qbsp_t     *in;
    mleaf_t     *out;
    int         i;
    uint32_t    cluster, area;
    uint32_t    firstleafbrush, numleafbrushes, lastleafbrush;
#if USE_REF
    int         j;
    uint32_t    firstleafface, numleaffaces, lastleafface;
#endif

    if (!count) {
        DEBUG("map with no leafs");
        return Q_ERR_TOO_FEW;
    }

    bsp->numleafs = count;
    bsp->leafs = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->leafs;
    for (i = 0; i < count; i++, in++, out++) {
        out->plane = NULL;
        out->contents = LittleLong(in->contents);
        cluster = LittleLong(in->cluster);
        if (cluster == (uint32_t)-1) {
            // solid leafs use special -1 cluster
            out->cluster = -1;
        } else if (bsp->vis == NULL) {
            // map has no vis, use 0 as a default cluster
            out->cluster = 0;
        } else {
            // validate cluster
            if (cluster >= bsp->vis->numclusters) {
                DEBUG("bad cluster");
                return Q_ERR_BAD_INDEX;
            }
            out->cluster = cluster;
        }

        area = LittleLong(in->area);
        if (area >= bsp->numareas) {
            DEBUG("bad area");
            return Q_ERR_BAD_INDEX;
        }
        out->area = area;

        firstleafbrush = LittleLong(in->firstleafbrush);
        numleafbrushes = LittleLong(in->numleafbrushes);
        lastleafbrush = firstleafbrush + numleafbrushes;
        if (lastleafbrush < firstleafbrush || lastleafbrush > bsp->numleafbrushes) {
            DEBUG("bad leafbrushes");
            return Q_ERR_BAD_INDEX;
        }
        out->firstleafbrush = bsp->leafbrushes + firstleafbrush;
        out->numleafbrushes = numleafbrushes;

#if USE_REF
        firstleafface = LittleLong(in->firstleafface);
        numleaffaces = LittleLong(in->numleaffaces);
        lastleafface = firstleafface + numleaffaces;
        if (lastleafface < firstleafface || lastleafface > bsp->numleaffaces) {
            DEBUG("bad leaffaces");
            return Q_ERR_BAD_INDEX;
        }
        out->firstleafface = bsp->leaffaces + firstleafface;
        out->numleaffaces = numleaffaces;

        for (j = 0; j < 3; j++) {
            out->mins[j] = LittleFloat(in->mins[j]);
            out->maxs[j] = LittleFloat(in->maxs[j]);
        }

        out->parent = NULL;
        out->visframe = -1;
#endif
    }

    if (bsp->leafs[0].contents != CONTENTS_SOLID) {
        DEBUG("map leaf 0 is not CONTENTS_SOLID");
        return Q_ERR_INVALID_FORMAT;
    }

    return Q_ERR_SUCCESS;
}

LOAD(Nodes)
{
    dnode_t     *in;
    mnode_t     *out;
    int         i, j;
    uint32_t    planenum, child;
#if USE_REF
    uint16_t    firstface, numfaces, lastface;
#endif

    if (!count) {
        DEBUG("map with no nodes");
        return Q_ERR_TOO_FEW;
    }

    bsp->numnodes = count;
    bsp->nodes = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->nodes;
    for (i = 0; i < count; i++, out++, in++) {
        planenum = LittleLong(in->planenum);
        if (planenum >= bsp->numplanes) {
            DEBUG("bad planenum");
            return Q_ERR_BAD_INDEX;
        }
        out->plane = bsp->planes + planenum;

        for (j = 0; j < 2; j++) {
            child = LittleLong(in->children[j]);
            if (child & 0x80000000) {
                child = ~child;
                if (child >= bsp->numleafs) {
                    DEBUG("bad leafnum");
                    return Q_ERR_BAD_INDEX;
                }
                out->children[j] = (mnode_t *)(bsp->leafs + child);
            } else {
                if (child >= count) {
                    DEBUG("bad nodenum");
                    return Q_ERR_BAD_INDEX;
                }
                out->children[j] = bsp->nodes + child;
            }
        }

#if USE_REF
        firstface = LittleShort(in->firstface);
        numfaces = LittleShort(in->numfaces);
        lastface = firstface + numfaces;
        if (lastface < firstface || lastface > bsp->numfaces) {
            DEBUG("bad faces");
            return Q_ERR_BAD_INDEX;
        }
        out->firstface = bsp->faces + firstface;
        out->numfaces = numfaces;

        for (j = 0; j < 3; j++) {
            out->mins[j] = (int16_t)LittleShort(in->mins[j]);
            out->maxs[j] = (int16_t)LittleShort(in->maxs[j]);
        }

        out->parent = NULL;
        out->visframe = -1;
#endif
    }

    return Q_ERR_SUCCESS;
}

LOAD_EXT(Nodes)
{
    dnode_qbsp_t     *in;
    mnode_t     *out;
    int         i, j;
    uint32_t    planenum, child;
#if USE_REF
    uint32_t    firstface, numfaces, lastface;
#endif

    if (!count) {
        DEBUG("map with no nodes");
        return Q_ERR_TOO_FEW;
    }

    bsp->numnodes = count;
    bsp->nodes = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->nodes;
    for (i = 0; i < count; i++, out++, in++) {
        planenum = LittleLong(in->planenum);
        if (planenum >= bsp->numplanes) {
            DEBUG("bad planenum");
            return Q_ERR_BAD_INDEX;
        }
        out->plane = bsp->planes + planenum;

        for (j = 0; j < 2; j++) {
            child = LittleLong(in->children[j]);
            if (child & 0x80000000) {
                child = ~child;
                if (child >= bsp->numleafs) {
                    DEBUG("bad leafnum");
                    return Q_ERR_BAD_INDEX;
                }
                out->children[j] = (mnode_t *)(bsp->leafs + child);
            } else {
                if (child >= count) {
                    DEBUG("bad nodenum");
                    return Q_ERR_BAD_INDEX;
                }
                out->children[j] = bsp->nodes + child;
            }
        }

#if USE_REF
        firstface = LittleLong(in->firstface);
        numfaces = LittleLong(in->numfaces);
        lastface = firstface + numfaces;
        if (lastface < firstface || lastface > bsp->numfaces) {
            DEBUG("bad faces");
            return Q_ERR_BAD_INDEX;
        }
        out->firstface = bsp->faces + firstface;
        out->numfaces = numfaces;

        for (j = 0; j < 3; j++) {
            out->mins[j] = LittleFloat(in->mins[j]);
            out->maxs[j] = LittleFloat(in->maxs[j]);
        }

        out->parent = NULL;
        out->visframe = -1;
#endif
    }

    return Q_ERR_SUCCESS;
}

LOAD(Submodels)
{
    dmodel_t    *in;
    mmodel_t    *out;
    int         i, j;
    uint32_t    headnode;
#if USE_REF
    uint32_t    firstface, numfaces, lastface;
#endif

    if (!count) {
        DEBUG("map with no models");
        return Q_ERR_TOO_FEW;
    }

    bsp->models = ALLOC(sizeof(*out) * count);
    bsp->nummodels = count;

    in = base;
    out = bsp->models;
    for (i = 0; i < count; i++, in++, out++) {
        for (j = 0; j < 3; j++) {
            // spread the mins / maxs by a pixel
            out->mins[j] = LittleFloat(in->mins[j]) - 1;
            out->maxs[j] = LittleFloat(in->maxs[j]) + 1;
            out->origin[j] = LittleFloat(in->origin[j]);
        }
        headnode = LittleLong(in->headnode);
        if (headnode & 0x80000000) {
            // be careful, some models have no nodes, just a leaf
            headnode = ~headnode;
            if (headnode >= bsp->numleafs) {
                DEBUG("bad headleaf");
                return Q_ERR_BAD_INDEX;
            }
            out->headnode = (mnode_t *)(bsp->leafs + headnode);
        } else {
            if (headnode >= bsp->numnodes) {
                DEBUG("bad headnode");
                return Q_ERR_BAD_INDEX;
            }
            out->headnode = bsp->nodes + headnode;
        }
#if USE_REF
        if (i == 0) {
            continue;
        }
        firstface = LittleLong(in->firstface);
        numfaces = LittleLong(in->numfaces);
        lastface = firstface + numfaces;
        if (lastface < firstface || lastface > bsp->numfaces) {
            DEBUG("bad faces");
            return Q_ERR_BAD_INDEX;
        }
        out->firstface = bsp->faces + firstface;
        out->numfaces = numfaces;

        out->radius = RadiusFromBounds(out->mins, out->maxs);
#endif
    }

    return Q_ERR_SUCCESS;
}

// These are validated after all the areas are loaded
LOAD(AreaPortals)
{
    dareaportal_t   *in;
    mareaportal_t   *out;
    int         i;

    bsp->numareaportals = count;
    bsp->areaportals = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->areaportals;
    for (i = 0; i < count; i++, in++, out++) {
        out->portalnum = LittleLong(in->portalnum);
        out->otherarea = LittleLong(in->otherarea);
    }

    return Q_ERR_SUCCESS;
}

LOAD(Areas)
{
    darea_t     *in;
    marea_t     *out;
    int         i;
    uint32_t    numareaportals, firstareaportal, lastareaportal;

    bsp->numareas = count;
    bsp->areas = ALLOC(sizeof(*out) * count);

    in = base;
    out = bsp->areas;
    for (i = 0; i < count; i++, in++, out++) {
        numareaportals = LittleLong(in->numareaportals);
        firstareaportal = LittleLong(in->firstareaportal);
        lastareaportal = firstareaportal + numareaportals;
        if (lastareaportal < firstareaportal || lastareaportal > bsp->numareaportals) {
            DEBUG("bad areaportals");
            return Q_ERR_BAD_INDEX;
        }
        out->numareaportals = numareaportals;
        out->firstareaportal = bsp->areaportals + firstareaportal;
        out->floodvalid = 0;
    }

    return Q_ERR_SUCCESS;
}

LOAD(EntString)
{
    bsp->numentitychars = count;
    bsp->entitystring = ALLOC(count + 1);
    memcpy(bsp->entitystring, base, count);
    bsp->entitystring[count] = 0;

    return Q_ERR_SUCCESS;
}

/*
===============================================================================

                    MAP LOADING

===============================================================================
*/

typedef struct {
    qerror_t (*load)(bsp_t *, void *, size_t);
    unsigned lump;
    size_t disksize;
    size_t memsize;
    size_t maxcount;
} lump_info_t;

#define L(func, lump, disk_t, mem_t) \
    { BSP_Load##func, LUMP_##lump, sizeof(disk_t), sizeof(mem_t), MAX_MAP_##lump }

static const lump_info_t bsp_lumps[] = {
    L(Visibility,   VISIBILITY,     byte,           byte),
    L(Texinfo,      TEXINFO,        dtexinfo_t,     mtexinfo_t),
    L(Planes,       PLANES,         dplane_t,       cplane_t),
    L(BrushSides,   BRUSHSIDES,     dbrushside_t,   mbrushside_t),
    L(Brushes,      BRUSHES,        dbrush_t,       mbrush_t),
    L(LeafBrushes,  LEAFBRUSHES,    uint16_t,       mbrush_t *),
    L(AreaPortals,  AREAPORTALS,    dareaportal_t,  mareaportal_t),
    L(Areas,        AREAS,          darea_t,        marea_t),
#if USE_REF
    L(Lightmap,     LIGHTING,       byte,           byte),
    L(Vertices,     VERTEXES,       dvertex_t,      mvertex_t),
    L(Edges,        EDGES,          dedge_t,        medge_t),
    L(SurfEdges,    SURFEDGES,      uint32_t,       msurfedge_t),
    L(Faces,        FACES,          dface_t,        mface_t),
    L(LeafFaces,    LEAFFACES,      uint16_t,       mface_t *),
#endif
    L(Leafs,        LEAFS,          dleaf_t,        mleaf_t),
    L(Nodes,        NODES,          dnode_t,        mnode_t),
    L(Submodels,    MODELS,         dmodel_t,       mmodel_t),
    L(EntString,    ENTSTRING,      char,           char),
    { NULL }
};

#undef L

// QBSP

#define LS(func, lump, disk_t, mem_t) \
    { BSP_Load##func, LUMP_##lump, sizeof(disk_t), sizeof(mem_t), MAX_QBSP_MAP_##lump }
#define L(func, lump, disk_t, mem_t) \
    { BSP_QBSP_Load##func, LUMP_##lump, sizeof(disk_t), sizeof(mem_t), MAX_QBSP_MAP_##lump }

static const lump_info_t qbsp_lumps[] = {
    LS(Visibility,   VISIBILITY,     byte,           byte),
    LS(Texinfo,      TEXINFO,        dtexinfo_t,     mtexinfo_t),
    LS(Planes,       PLANES,         dplane_t,       cplane_t),
    L(BrushSides,   BRUSHSIDES,     dbrushside_qbsp_t,   mbrushside_t),
    LS(Brushes,      BRUSHES,        dbrush_t,       mbrush_t),
    L(LeafBrushes,  LEAFBRUSHES,    uint32_t,       mbrush_t *),
    LS(AreaPortals,  AREAPORTALS,    dareaportal_t,  mareaportal_t),
    LS(Areas,        AREAS,          darea_t,        marea_t),
#if USE_REF
    LS(Lightmap,     LIGHTING,       byte,           byte),
    LS(Vertices,     VERTEXES,       dvertex_t,      mvertex_t),
    L(Edges,        EDGES,          dedge_qbsp_t,        medge_t),
    LS(SurfEdges,    SURFEDGES,      uint32_t,       msurfedge_t),
    L(Faces,        FACES,          dface_qbsp_t,        mface_t),
    L(LeafFaces,    LEAFFACES,      uint32_t,       mface_t *),
#endif
    L(Leafs,        LEAFS,          dleaf_qbsp_t,        mleaf_t),
    L(Nodes,        NODES,          dnode_qbsp_t,        mnode_t),
    LS(Submodels,    MODELS,         dmodel_t,       mmodel_t),
    LS(EntString,    ENTSTRING,      char,           char),
    { NULL }
};

#undef LS
#undef L

static list_t   bsp_cache;

static void BSP_List_f(void)
{
    bsp_t *bsp;
    size_t bytes;

    if (LIST_EMPTY(&bsp_cache)) {
        Com_Printf("BSP cache is empty\n");
        return;
    }

    Com_Printf("------------------\n");
    bytes = 0;

    LIST_FOR_EACH(bsp_t, bsp, &bsp_cache, entry) {
        Com_Printf("%8"PRIz" : %s (%d refs)\n",
                   bsp->hunk.mapped, bsp->name, bsp->refcount);
        bytes += bsp->hunk.mapped;
    }
    Com_Printf("Total resident: %"PRIz"\n", bytes);
}

static bsp_t *BSP_Find(const char *name)
{
    bsp_t *bsp;

    LIST_FOR_EACH(bsp_t, bsp, &bsp_cache, entry) {
        if (!FS_pathcmp(bsp->name, name)) {
            return bsp;
        }
    }

    return NULL;
}

static qerror_t BSP_SetParent(mnode_t *node, int key)
{
    mnode_t *child;
#if USE_REF
    mface_t *face;
    int i;
#endif

    while (node->plane) {
#if USE_REF
        // a face may never belong to more than one node
        for (i = 0, face = node->firstface; i < node->numfaces; i++, face++) {
            if (face->drawframe) {
                DEBUG("duplicate face");
                return Q_ERR_INFINITE_LOOP;
            }
            face->drawframe = key;
        }
#endif

        child = node->children[0];
        if (child->parent) {
            DEBUG("cycle encountered");
            return Q_ERR_INFINITE_LOOP;
        }
        child->parent = node;
        if (BSP_SetParent(child, key)) {
            return Q_ERR_INFINITE_LOOP;
        }

        child = node->children[1];
        if (child->parent) {
            DEBUG("cycle encountered");
            return Q_ERR_INFINITE_LOOP;
        }
        child->parent = node;
        node = child;
    }

    return Q_ERR_SUCCESS;
}

static qerror_t BSP_ValidateTree(bsp_t *bsp)
{
    mmodel_t *mod;
    qerror_t ret;
    int i;
#if USE_REF
    mface_t *face;
    int j;
#endif

    for (i = 0, mod = bsp->models; i < bsp->nummodels; i++, mod++) {
        if (i == 0 && mod->headnode != bsp->nodes) {
            DEBUG("map model 0 headnode is not the first node");
            return Q_ERR_INVALID_FORMAT;
        }

        ret = BSP_SetParent(mod->headnode, ~i);
        if (ret) {
            return ret;
        }

#if USE_REF
        // a face may never belong to more than one model
        for (j = 0, face = mod->firstface; j < mod->numfaces; j++, face++) {
            if (face->drawframe && face->drawframe != ~i) {
                DEBUG("duplicate face");
                return Q_ERR_INFINITE_LOOP;
            }
            face->drawframe = ~i;
        }
#endif
    }

    return Q_ERR_SUCCESS;
}

// also calculates the last portal number used
// by CM code to allocate portalopen[] array
static qerror_t BSP_ValidateAreaPortals(bsp_t *bsp)
{
    mareaportal_t   *p;
    int             i;

    bsp->lastareaportal = 0;
    for (i = 0, p = bsp->areaportals; i < bsp->numareaportals; i++, p++) {
        if (p->portalnum >= MAX_MAP_AREAPORTALS) {
            DEBUG("bad portalnum");
            return Q_ERR_TOO_MANY;
        }
        if (p->portalnum > bsp->lastareaportal) {
            bsp->lastareaportal = p->portalnum;
        }
        if (p->otherarea >= bsp->numareas) {
            DEBUG("bad otherarea");
            return Q_ERR_BAD_INDEX;
        }
    }

    return Q_ERR_SUCCESS;
}

void BSP_Free(bsp_t *bsp)
{
    if (!bsp) {
        return;
    }
    if (bsp->refcount <= 0) {
        Com_Error(ERR_FATAL, "%s: negative refcount", __func__);
    }
    if (--bsp->refcount == 0) {
		if (bsp->pvs2_matrix)
		{
			// free the PVS2 matrix separately - it's not part of the hunk
			Z_Free(bsp->pvs2_matrix);
			bsp->pvs2_matrix = NULL;
		}

        Hunk_Free(&bsp->hunk);
        List_Remove(&bsp->entry);
        Z_Free(bsp);
    }
}

static void BSP_BuildPvsMatrix(bsp_t *bsp)
{
	if (!bsp->vis)
		return;

	// a typical map with 2K clusters will take half a megabyte of memory for the matrix
	size_t matrix_size = bsp->visrowsize * bsp->vis->numclusters;

	// allocate the matrix but don't set it in the BSP structure yet: 
	// we want BSP_CluterVis to use the old PVS data here, and not the new empty matrix
	byte* pvs_matrix = Z_Mallocz(matrix_size);
	
	for (int cluster = 0; cluster < bsp->vis->numclusters; cluster++)
	{
		BSP_ClusterVis(bsp, pvs_matrix + bsp->visrowsize * cluster, cluster, DVIS_PVS);
	}

	bsp->pvs_matrix = pvs_matrix;
}

byte* BSP_GetPvs(bsp_t *bsp, int cluster)
{
	if (!bsp->vis || !bsp->pvs_matrix)
		return NULL;
	
	if (cluster < 0 || cluster >= bsp->vis->numclusters)
		return NULL;

	return bsp->pvs_matrix + bsp->visrowsize * cluster;
}

byte* BSP_GetPvs2(bsp_t *bsp, int cluster)
{
	if (!bsp->vis || !bsp->pvs2_matrix)
		return NULL;

	if (cluster < 0 || cluster >= bsp->vis->numclusters)
		return NULL;

	return bsp->pvs2_matrix + bsp->visrowsize * cluster;
}

// Converts `maps/<name>.bsp` into `maps/pvs/<name>.bin`
static qboolean BSP_GetPatchedPVSFileName(const char* map_path, char pvs_path[MAX_QPATH])
{
	int path_len = strlen(map_path);
	if (path_len < 5 || strcmp(map_path + path_len - 4, ".bsp") != 0)
		return qfalse;

	const char* map_file = strrchr(map_path, '/');
	if (map_file)
		map_file += 1;
	else
		map_file = map_path;

	memset(pvs_path, 0, MAX_QPATH);
	strncpy(pvs_path, map_path, map_file - map_path);
	strcat(pvs_path, "pvs/");
	strncat(pvs_path, map_file, strlen(map_file) - 4);
	strcat(pvs_path, ".bin");

	return qtrue;
}

// Loads the first- and second-order PVS matrices from a file called `maps/pvs/<mapname>.bin`
static qboolean BSP_LoadPatchedPVS(bsp_t *bsp)
{
	char pvs_path[MAX_QPATH];

	if (!BSP_GetPatchedPVSFileName(bsp->name, pvs_path))
		return qfalse;

	unsigned char* filebuf = 0;
	ssize_t filelen = 0;
	filelen = FS_LoadFile(pvs_path, (void**)&filebuf);

	if (filebuf == 0)
		return qfalse;

	size_t matrix_size = bsp->visrowsize * bsp->vis->numclusters;
	if (filelen != matrix_size * 2)
	{
		FS_FreeFile(filebuf);
		return qfalse;
	}

	bsp->pvs_matrix = Z_Malloc(matrix_size);
	memcpy(bsp->pvs_matrix, filebuf, matrix_size);

	bsp->pvs2_matrix = Z_Malloc(matrix_size);
	memcpy(bsp->pvs2_matrix, filebuf + matrix_size, matrix_size);

	FS_FreeFile(filebuf);
	return qtrue;
}

// Saves the first- and second-order PVS matrices to a file called `maps/pvs/<mapname>.bin`
qboolean BSP_SavePatchedPVS(bsp_t *bsp)
{
	char pvs_path[MAX_QPATH];

	if (!BSP_GetPatchedPVSFileName(bsp->name, pvs_path))
		return qfalse;

	if (!bsp->pvs_matrix)
		return qfalse;

	if (!bsp->pvs2_matrix)
		return qfalse;

	size_t matrix_size = bsp->visrowsize * bsp->vis->numclusters;
	unsigned char* filebuf = Z_Malloc(matrix_size * 2);

	memcpy(filebuf, bsp->pvs_matrix, matrix_size);
	memcpy(filebuf + matrix_size, bsp->pvs2_matrix, matrix_size);

	qerror_t err = FS_WriteFile(pvs_path, filebuf, matrix_size * 2);

	Z_Free(filebuf);

	if (err >= 0)
		return qtrue;
	else
		return qfalse;
}

/*
==================
BSP_Load

Loads in the map and all submodels
==================
*/
qerror_t BSP_Load(const char *name, bsp_t **bsp_p)
{
    bsp_t           *bsp;
    byte            *buf;
    dheader_t       *header;
    const lump_info_t *info;
    size_t          filelen, ofs, len, end, count;
    qerror_t        ret;
    byte            *lumpdata[HEADER_LUMPS];
    size_t          lumpcount[HEADER_LUMPS];
    size_t          memsize;

    if (!name || !bsp_p)
        Com_Error(ERR_FATAL, "%s: NULL", __func__);

    *bsp_p = NULL;

    if (!*name)
        return Q_ERR_NOENT;

    if ((bsp = BSP_Find(name)) != NULL) {
        Com_PageInMemory(bsp->hunk.base, bsp->hunk.cursize);
        bsp->refcount++;
        *bsp_p = bsp;
        return Q_ERR_SUCCESS;
    }

    //
    // load the file
    //
    filelen = FS_LoadFile(name, (void **)&buf);
    if (!buf) {
        return filelen;
    }

    // byte swap and validate the header
    header = (dheader_t *)buf;
    if (LittleLong(header->ident) != IDBSPHEADER &&
        LittleLong(header->ident) != QBSPHEADER) {
        ret = Q_ERR_UNKNOWN_FORMAT;
        goto fail2;
    }
    if (LittleLong(header->version) != BSPVERSION) {
        ret = Q_ERR_UNKNOWN_FORMAT;
        goto fail2;
    }

    const lump_info_t *lumps = LittleLong(header->ident) == IDBSPHEADER ? bsp_lumps : qbsp_lumps;

    // byte swap and validate all lumps
    memsize = 0;
    for (info = lumps; info->load; info++) {
        ofs = LittleLong(header->lumps[info->lump].fileofs);
        len = LittleLong(header->lumps[info->lump].filelen);
        end = ofs + len;
        if (end < ofs || end > filelen) {
            ret = Q_ERR_BAD_EXTENT;
            goto fail2;
        }
        if (len % info->disksize) {
            ret = Q_ERR_ODD_SIZE;
            goto fail2;
        }
        count = len / info->disksize;
        if (count > info->maxcount) {
            ret = Q_ERR_TOO_MANY;
            goto fail2;
        }

        lumpdata[info->lump] = buf + ofs;
        lumpcount[info->lump] = count;

        memsize += count * info->memsize;
    }

    // load into hunk
    len = strlen(name);
    bsp = Z_Mallocz(sizeof(*bsp) + len);
    memcpy(bsp->name, name, len + 1);
    bsp->refcount = 1;
    bsp->extended = (lumps == qbsp_lumps);

    // add an extra page for cacheline alignment overhead
    Hunk_Begin(&bsp->hunk, memsize + 4096);

    // calculate the checksum
    bsp->checksum = LittleLong(Com_BlockChecksum(buf, filelen));

    // load all lumps
    for (info = lumps; info->load; info++) {
        ret = info->load(bsp, lumpdata[info->lump], lumpcount[info->lump]);
        if (ret) {
            goto fail1;
        }
    }

    ret = BSP_ValidateAreaPortals(bsp);
    if (ret) {
        goto fail1;
    }

    ret = BSP_ValidateTree(bsp);
    if (ret) {
        goto fail1;
    }

	if (!BSP_LoadPatchedPVS(bsp))
	{
		BSP_BuildPvsMatrix(bsp);
	}
	else
	{
		bsp->pvs_patched = qtrue;
	}

    Hunk_End(&bsp->hunk);

    List_Append(&bsp_cache, &bsp->entry);

    FS_FreeFile(buf);

    *bsp_p = bsp;
    return Q_ERR_SUCCESS;

fail1:
    Hunk_Free(&bsp->hunk);
    Z_Free(bsp);
fail2:
    FS_FreeFile(buf);
    return ret;
}

/*
===============================================================================

HELPER FUNCTIONS

===============================================================================
*/

#if USE_REF

static lightpoint_t *light_point;

static qboolean BSP_RecursiveLightPoint(mnode_t *node, float p1f, float p2f, vec3_t p1, vec3_t p2)
{
    vec_t d1, d2, frac, midf;
    vec3_t mid;
    int i, side, s, t;
    mface_t *surf;
    mtexinfo_t *texinfo;

    while (node->plane) {
        // calculate distancies
        d1 = PlaneDiffFast(p1, node->plane);
        d2 = PlaneDiffFast(p2, node->plane);
        side = (d1 < 0);

        if ((d2 < 0) == side) {
            // both points are one the same side
            node = node->children[side];
            continue;
        }

        // find crossing point
        frac = d1 / (d1 - d2);
        midf = p1f + (p2f - p1f) * frac;
        LerpVector(p1, p2, frac, mid);

        // check near side
        if (BSP_RecursiveLightPoint(node->children[side], p1f, midf, p1, mid))
            return qtrue;

        for (i = 0, surf = node->firstface; i < node->numfaces; i++, surf++) {
            if (!surf->lightmap)
                continue;

            texinfo = surf->texinfo;
            if (texinfo->c.flags & SURF_NOLM_MASK)
                continue;

            s = DotProduct(texinfo->axis[0], mid) + texinfo->offset[0];
            t = DotProduct(texinfo->axis[1], mid) + texinfo->offset[1];

            s -= surf->texturemins[0];
            t -= surf->texturemins[1];
            if (s < 0 || s > surf->extents[0])
                continue;
            if (t < 0 || t > surf->extents[1])
                continue;

            light_point->surf = surf;
            light_point->plane = *surf->plane;
            light_point->s = s;
            light_point->t = t;
            light_point->fraction = midf;
            return qtrue;
        }

        // check far side
        return BSP_RecursiveLightPoint(node->children[side ^ 1], midf, p2f, mid, p2);
    }

    return qfalse;
}

void BSP_LightPoint(lightpoint_t *point, vec3_t start, vec3_t end, mnode_t *headnode)
{
    light_point = point;
    light_point->surf = NULL;
    light_point->fraction = 1;

    BSP_RecursiveLightPoint(headnode, 0, 1, start, end);
}

void BSP_TransformedLightPoint(lightpoint_t *point, vec3_t start, vec3_t end,
                               mnode_t *headnode, vec3_t origin, vec3_t angles)
{
    vec3_t start_l, end_l;
    vec3_t axis[3];

    light_point = point;
    light_point->surf = NULL;
    light_point->fraction = 1;

    // subtract origin offset
    VectorSubtract(start, origin, start_l);
    VectorSubtract(end, origin, end_l);

    // rotate start and end into the models frame of reference
    if (angles) {
        AnglesToAxis(angles, axis);
        RotatePoint(start_l, axis);
        RotatePoint(end_l, axis);
    }

    // sweep the line through the model
    if (!BSP_RecursiveLightPoint(headnode, 0, 1, start_l, end_l))
        return;

    // rotate plane normal into the worlds frame of reference
    if (angles) {
        TransposeAxis(axis);
        RotatePoint(point->plane.normal, axis);
    }

    // offset plane distance
    point->plane.dist += DotProduct(point->plane.normal, origin);
}

#endif

byte *BSP_ClusterVis(bsp_t *bsp, byte *mask, int cluster, int vis)
{
    byte    *in, *out, *in_end, *out_end;
    int     c;

    if (!bsp || !bsp->vis) {
        return memset(mask, 0xff, VIS_MAX_BYTES);
    }
    if (cluster == -1) {
        return memset(mask, 0, bsp->visrowsize);
    }
    if (cluster < 0 || cluster >= bsp->vis->numclusters) {
        Com_Error(ERR_DROP, "%s: bad cluster", __func__);
    }

	if (vis == DVIS_PVS2)
	{
		if (bsp->pvs2_matrix)
		{
			byte* row = BSP_GetPvs2(bsp, cluster);
			memcpy(mask, row, bsp->visrowsize);
			return mask;
		}

		// fallback
		vis = DVIS_PVS;
	}

	if (vis == DVIS_PVS && bsp->pvs_matrix)
	{
		byte* row = BSP_GetPvs(bsp, cluster);
		memcpy(mask, row, bsp->visrowsize);
		return mask;
	}

    // decompress vis
    in_end = (byte *)bsp->vis + bsp->numvisibility;
    in = (byte *)bsp->vis + bsp->vis->bitofs[cluster][vis];
    out_end = mask + bsp->visrowsize;
    out = mask;
    do {
        if (in >= in_end) {
            goto overrun;
        }
        if (*in) {
            *out++ = *in++;
            continue;
        }

        if (in + 1 >= in_end) {
            goto overrun;
        }
        c = in[1];
        in += 2;
        if (out + c > out_end) {
overrun:
            c = out_end - out;
        }
        while (c--) {
            *out++ = 0;
        }
    } while (out < out_end);

    // apply our ugly PVS patches
    if (map_visibility_patch->integer) {
        if (bsp->checksum == 0x1e5b50c5) {
            // q2dm3, pent bridge
            if (cluster == 345 || cluster == 384) {
                Q_SetBit(mask, 466);
                Q_SetBit(mask, 484);
                Q_SetBit(mask, 692);
            }
        } else if (bsp->checksum == 0x04cfa792) {
            // q2dm1, above lower RL
            if (cluster == 395) {
                Q_SetBit(mask, 176);
                Q_SetBit(mask, 183);
            }
        } else if (bsp->checksum == 0x2c3ab9b0) {
            // q2dm8, CG/RG area
            if (cluster == 629 || cluster == 631 ||
                cluster == 633 || cluster == 639) {
                Q_SetBit(mask, 908);
                Q_SetBit(mask, 909);
                Q_SetBit(mask, 910);
                Q_SetBit(mask, 915);
                Q_SetBit(mask, 923);
                Q_SetBit(mask, 924);
                Q_SetBit(mask, 927);
                Q_SetBit(mask, 930);
                Q_SetBit(mask, 938);
                Q_SetBit(mask, 939);
                Q_SetBit(mask, 947);
            }
        }
    }

    return mask;
}

mleaf_t *BSP_PointLeaf(mnode_t *node, vec3_t p)
{
    float d;

    while (node->plane) {
        d = PlaneDiffFast(p, node->plane);
        if (d < 0)
            node = node->children[1];
        else
            node = node->children[0];
    }

    return (mleaf_t *)node;
}

/*
==================
BSP_InlineModel
==================
*/
mmodel_t *BSP_InlineModel(bsp_t *bsp, const char *name)
{
    int     num;

    if (!bsp || !name) {
        Com_Error(ERR_DROP, "%s: NULL", __func__);
    }
    if (name[0] != '*') {
        Com_Error(ERR_DROP, "%s: bad name: %s", __func__, name);
    }
    num = atoi(name + 1);
    if (num < 1 || num >= bsp->nummodels) {
        Com_Error(ERR_DROP, "%s: bad number: %d", __func__, num);
    }

    return &bsp->models[num];
}

void BSP_Init(void)
{
    map_visibility_patch = Cvar_Get("map_visibility_patch", "1", 0);

    Cmd_AddCommand("bsplist", BSP_List_f);

    List_Init(&bsp_cache);
}

