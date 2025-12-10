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
#include "common/bsp.h"
#include "common/cmd.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "common/intreadwrite.h"
#include "common/math.h"
#include "common/mdfour.h"
#include "common/utils.h"
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
    static int BSP_Load##func(bsp_t *bsp, const byte *in, size_t count)

#define DEBUG(msg) \
    Com_SetLastError(va("%s: %s", __func__, msg))

#define BSP_Short()     (in += 2, RL16(in - 2))
#define BSP_Long()      (in += 4, RL32(in - 4))
#define BSP_Float()     LongToFloat(BSP_Long())

#define BSP_ExtFloat()  (bsp->extended ? BSP_Float()  : (int16_t)BSP_Short())
#define BSP_ExtLong()   (bsp->extended ? BSP_Long()   : BSP_Short())
#define BSP_ExtNull     (bsp->extended ? (uint32_t)-1 : (uint16_t)-1)

LOAD(Visibility)
{
    uint32_t numclusters, bitofs, hdrsize;
    int i, j;

    if (!count) {
        return Q_ERR_SUCCESS;
    }

    if (count < 4) {
        DEBUG("too small header");
        return Q_ERR_INVALID_FORMAT;
    }

    numclusters = BSP_Long();
    if (numclusters > MAX_MAP_CLUSTERS) {
        DEBUG("too many clusters");
        return Q_ERR_INVALID_FORMAT;
    }

    hdrsize = 4 + numclusters * 8;
    if (count < hdrsize) {
        DEBUG("too small header");
        return Q_ERR_INVALID_FORMAT;
    }

    bsp->numvisibility = count;
    bsp->vis = ALLOC(count);
    bsp->vis->numclusters = numclusters;
    bsp->visrowsize = (numclusters + 7) >> 3;
    Q_assert(bsp->visrowsize <= VIS_MAX_BYTES);

    for (i = 0; i < numclusters; i++) {
        for (j = 0; j < 2; j++) {
            bitofs = BSP_Long();
            if (bitofs < hdrsize || bitofs >= count) {
                DEBUG("bad bitofs");
                return Q_ERR_INVALID_FORMAT;
            }
            bsp->vis->bitofs[i][j] = bitofs;
        }
    }

    memcpy(bsp->vis->bitofs + numclusters, in, count - hdrsize);

    return Q_ERR_SUCCESS;
}

LOAD(Texinfo)
{
    mtexinfo_t  *out;
    int         i;
#if USE_REF
    int         j;
    int32_t     next;
    mtexinfo_t  *step;
#endif

    bsp->numtexinfo = count;
    bsp->texinfo = ALLOC(sizeof(*out) * count);

    out = bsp->texinfo;
    for (i = 0; i < count; i++, out++) {
#if USE_REF
        for (j = 0; j < 2; j++) {
            out->axis[j][0] = BSP_Float();
            out->axis[j][1] = BSP_Float();
            out->axis[j][2] = BSP_Float();
            out->offset[j] = BSP_Float();
        }
#else
        in += 32;
#endif
        out->c.flags = BSP_Long();
        out->c.value = BSP_Long();

        memcpy(out->c.name, in, sizeof(out->c.name) - 1);
        memcpy(out->name, in, sizeof(out->name) - 1);
        in += MAX_TEXNAME;

#if USE_REF
        out->radiance = out->c.value;
        next = (int32_t)BSP_Long();
        if (next > 0) {
            if (next >= count) {
                DEBUG("bad anim chain");
                return Q_ERR_INVALID_FORMAT;
            }
            out->next = bsp->texinfo + next;
        } else {
            out->next = NULL;
        }
#else
        in += 4;
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
    cplane_t    *out;
    int         i;

    bsp->numplanes = count;
    bsp->planes = ALLOC(sizeof(*out) * count);

    out = bsp->planes;
    for (i = 0; i < count; i++, in += 4, out++) {
        out->normal[0] = BSP_Float();
        out->normal[1] = BSP_Float();
        out->normal[2] = BSP_Float();
        out->dist = BSP_Float();
        SetPlaneType(out);
        SetPlaneSignbits(out);
    }

    return Q_ERR_SUCCESS;
}

LOAD(BrushSides)
{
    mbrushside_t    *out;
    int         i;
    uint32_t    planenum, texinfo;

    bsp->numbrushsides = count;
    bsp->brushsides = ALLOC(sizeof(*out) * count);

    out = bsp->brushsides;
    for (i = 0; i < count; i++, out++) {
        planenum = BSP_ExtLong();
        if (planenum >= bsp->numplanes) {
            DEBUG("bad planenum");
            return Q_ERR_INVALID_FORMAT;
        }
        out->plane = bsp->planes + planenum;
        texinfo = BSP_ExtLong();
        if (texinfo == BSP_ExtNull) {
            out->texinfo = &nulltexinfo;
        } else {
            if (texinfo >= bsp->numtexinfo) {
                DEBUG("bad texinfo");
                return Q_ERR_INVALID_FORMAT;
            }
            out->texinfo = bsp->texinfo + texinfo;
        }
    }

    return Q_ERR_SUCCESS;
}

LOAD(Brushes)
{
    mbrush_t    *out;
    int         i;
    uint32_t    firstside, numsides, lastside;

    bsp->numbrushes = count;
    bsp->brushes = ALLOC(sizeof(*out) * count);

    out = bsp->brushes;
    for (i = 0; i < count; i++, out++) {
        firstside = BSP_Long();
        numsides = BSP_Long();
        lastside = firstside + numsides;
        if (lastside < firstside || lastside > bsp->numbrushsides) {
            DEBUG("bad brushsides");
            return Q_ERR_INVALID_FORMAT;
        }
        out->firstbrushside = bsp->brushsides + firstside;
        out->numsides = numsides;
        out->contents = BSP_Long();
        out->checkcount = 0;
    }

    return Q_ERR_SUCCESS;
}

LOAD(LeafBrushes)
{
    mbrush_t    **out;
    int         i;
    uint32_t    brushnum;

    bsp->numleafbrushes = count;
    bsp->leafbrushes = ALLOC(sizeof(*out) * count);

    out = bsp->leafbrushes;
    for (i = 0; i < count; i++, out++) {
        brushnum = BSP_ExtLong();
        if (brushnum >= bsp->numbrushes) {
            DEBUG("bad brushnum");
            return Q_ERR_INVALID_FORMAT;
        }
        *out = bsp->brushes + brushnum;
    }

    return Q_ERR_SUCCESS;
}


#if USE_REF
LOAD(Lightmap)
{
    if (count) {
        bsp->numlightmapbytes = count;
        bsp->lightmap = ALLOC(count);
        memcpy(bsp->lightmap, in, count);
    }

    return Q_ERR_SUCCESS;
}

LOAD(Vertices)
{
    mvertex_t   *out;
    int         i;

    bsp->numvertices = count;
    bsp->vertices = ALLOC(sizeof(*out) * count);

    out = bsp->vertices;
    for (i = 0; i < count; i++, out++) {
        out->point[0] = BSP_Float();
        out->point[1] = BSP_Float();
        out->point[2] = BSP_Float();
    }

    return Q_ERR_SUCCESS;
}

LOAD(Edges)
{
    medge_t     *out;
    int         i, j;
    uint32_t    vertnum;

    bsp->numedges = count;
    bsp->edges = ALLOC(sizeof(*out) * count);

    out = bsp->edges;
    for (i = 0; i < count; i++, out++) {
        for (j = 0; j < 2; j++) {
            vertnum = BSP_ExtLong();
            if (vertnum >= bsp->numvertices) {
                DEBUG("bad vertnum");
                return Q_ERR_INVALID_FORMAT;
            }
            out->v[j] = bsp->vertices + vertnum;
        }
    }

    return Q_ERR_SUCCESS;
}

LOAD(SurfEdges)
{
    msurfedge_t *out;
    int         i;
    uint32_t    index, vert;

    bsp->numsurfedges = count;
    bsp->surfedges = ALLOC(sizeof(*out) * count);

    out = bsp->surfedges;
    for (i = 0; i < count; i++, out++) {
        index = BSP_Long();

        vert = index >> 31;
        if (vert)
            index = -index;

        if (index >= bsp->numedges) {
            DEBUG("bad edgenum");
            return Q_ERR_INVALID_FORMAT;
        }

        out->edge = bsp->edges + index;
        out->vert = vert;
    }

    return Q_ERR_SUCCESS;
}

LOAD(Faces)
{
    mface_t     *out;
    int         i, j;
    uint32_t    firstedge, numedges, lastedge;
    uint32_t    planenum, texinfo, side;
    uint32_t    lightofs;

    bsp->numfaces = count;
    bsp->faces = ALLOC(sizeof(*out) * count);

    out = bsp->faces;
    for (i = 0; i < count; i++, out++) {
        planenum = BSP_ExtLong();
        if (planenum >= bsp->numplanes) {
            DEBUG("bad planenum");
            return Q_ERR_INVALID_FORMAT;
        }
        out->plane = bsp->planes + planenum;

        side = BSP_ExtLong();
        out->drawflags = side & DSURF_PLANEBACK;

        firstedge = BSP_Long();
        numedges = BSP_ExtLong();
        lastedge = firstedge + numedges;
        if (numedges < 3) {
            DEBUG("too few surfedges");
            return Q_ERR_INVALID_FORMAT;
        }
        if (numedges > 4096) {
            DEBUG("too many surfedges");
            return Q_ERR_INVALID_FORMAT;
        }
        if (lastedge < firstedge || lastedge > bsp->numsurfedges) {
            DEBUG("bad surfedges");
            return Q_ERR_INVALID_FORMAT;
        }
        out->firstsurfedge = bsp->surfedges + firstedge;
        out->numsurfedges = numedges;

        texinfo = BSP_ExtLong();
        if (texinfo >= bsp->numtexinfo) {
            DEBUG("bad texinfo");
            return Q_ERR_INVALID_FORMAT;
        }
        out->texinfo = bsp->texinfo + texinfo;

        for (j = 0; j < MAX_LIGHTMAPS && in[j] != 255; j++) {
            out->styles[j] = in[j];
        }
        for (out->numstyles = j; j < MAX_LIGHTMAPS; j++) {
            out->styles[j] = 255;
        }
        in += MAX_LIGHTMAPS;

        lightofs = BSP_Long();
        if (lightofs == (uint32_t)-1 || bsp->numlightmapbytes == 0) {
            out->lightmap = NULL;
        } else {
            if (lightofs >= bsp->numlightmapbytes) {
                DEBUG("bad lightofs");
                return Q_ERR_INVALID_FORMAT;
            }
            out->lightmap = bsp->lightmap + lightofs;
        }
    }

    return Q_ERR_SUCCESS;
}

LOAD(LeafFaces)
{
    mface_t     **out;
    int         i;
    uint32_t    facenum;

    bsp->numleaffaces = count;
    bsp->leaffaces = ALLOC(sizeof(*out) * count);

    out = bsp->leaffaces;
    for (i = 0; i < count; i++, out++) {
        facenum = BSP_ExtLong();
        if (facenum >= bsp->numfaces) {
            DEBUG("bad facenum");
            return Q_ERR_INVALID_FORMAT;
        }
        *out = bsp->faces + facenum;
    }

    return Q_ERR_SUCCESS;
}
#endif

LOAD(Leafs)
{
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
        return Q_ERR_INVALID_FORMAT;
    }

    bsp->numleafs = count;
    bsp->leafs = ALLOC(sizeof(*out) * count);

    out = bsp->leafs;
    for (i = 0; i < count; i++, out++) {
        out->plane = NULL;
        out->contents = BSP_Long();
        cluster = BSP_ExtLong();
        if (cluster == BSP_ExtNull) {
            // solid leafs use special -1 cluster
            out->cluster = -1;
        } else if (bsp->vis == NULL) {
            // map has no vis, use 0 as a default cluster
            out->cluster = 0;
        } else {
            // validate cluster
            if (cluster >= bsp->vis->numclusters) {
                DEBUG("bad cluster");
                return Q_ERR_INVALID_FORMAT;
            }
            out->cluster = cluster;
        }

        area = BSP_ExtLong();
        if (area >= bsp->numareas) {
            DEBUG("bad area");
            return Q_ERR_INVALID_FORMAT;
        }
        out->area = area;

#if USE_REF
        for (j = 0; j < 3; j++)
            out->mins[j] = BSP_ExtFloat();
        for (j = 0; j < 3; j++)
            out->maxs[j] = BSP_ExtFloat();

        firstleafface = BSP_ExtLong();
        numleaffaces = BSP_ExtLong();
        lastleafface = firstleafface + numleaffaces;
        if (lastleafface < firstleafface || lastleafface > bsp->numleaffaces) {
            DEBUG("bad leaffaces");
            return Q_ERR_INVALID_FORMAT;
        }
        out->firstleafface = bsp->leaffaces + firstleafface;
        out->numleaffaces = numleaffaces;

        out->parent = NULL;
        out->visframe = -1;
#else
        in += 16 * (bsp->extended + 1);
#endif

        firstleafbrush = BSP_ExtLong();
        numleafbrushes = BSP_ExtLong();
        lastleafbrush = firstleafbrush + numleafbrushes;
        if (lastleafbrush < firstleafbrush || lastleafbrush > bsp->numleafbrushes) {
            DEBUG("bad leafbrushes");
            return Q_ERR_INVALID_FORMAT;
        }
        out->firstleafbrush = bsp->leafbrushes + firstleafbrush;
        out->numleafbrushes = numleafbrushes;
    }

    if (bsp->leafs[0].contents != CONTENTS_SOLID) {
        DEBUG("map leaf 0 is not CONTENTS_SOLID");
        return Q_ERR_INVALID_FORMAT;
    }

    return Q_ERR_SUCCESS;
}

LOAD(Nodes)
{
    mnode_t     *out;
    int         i, j;
    uint32_t    planenum, child;
#if USE_REF
    uint32_t    firstface, numfaces, lastface;
#endif

    if (!count) {
        DEBUG("map with no nodes");
        return Q_ERR_INVALID_FORMAT;
    }

    bsp->numnodes = count;
    bsp->nodes = ALLOC(sizeof(*out) * count);

    out = bsp->nodes;
    for (i = 0; i < count; i++, out++) {
        planenum = BSP_Long();
        if (planenum >= bsp->numplanes) {
            DEBUG("bad planenum");
            return Q_ERR_INVALID_FORMAT;
        }
        out->plane = bsp->planes + planenum;

        for (j = 0; j < 2; j++) {
            child = BSP_Long();
            if (child & BIT(31)) {
                child = ~child;
                if (child >= bsp->numleafs) {
                    DEBUG("bad leafnum");
                    return Q_ERR_INVALID_FORMAT;
                }
                out->children[j] = (mnode_t *)(bsp->leafs + child);
            } else {
                if (child >= count) {
                    DEBUG("bad nodenum");
                    return Q_ERR_INVALID_FORMAT;
                }
                out->children[j] = bsp->nodes + child;
            }
        }

#if USE_REF
        for (j = 0; j < 3; j++)
            out->mins[j] = BSP_ExtFloat();
        for (j = 0; j < 3; j++)
            out->maxs[j] = BSP_ExtFloat();

        firstface = BSP_ExtLong();
        numfaces = BSP_ExtLong();
        lastface = firstface + numfaces;
        if (lastface < firstface || lastface > bsp->numfaces) {
            DEBUG("bad faces");
            return Q_ERR_INVALID_FORMAT;
        }
        out->firstface = bsp->faces + firstface;
        out->numfaces = numfaces;

        out->parent = NULL;
        out->visframe = -1;
#else
        in += 16 * (bsp->extended + 1);
#endif
    }

    return Q_ERR_SUCCESS;
}

LOAD(SubModels)
{
    mmodel_t    *out;
    int         i, j;
    uint32_t    headnode;
#if USE_REF
    uint32_t    firstface, numfaces, lastface;
#endif

    if (!count) {
        DEBUG("map with no models");
        return Q_ERR_INVALID_FORMAT;
    }
    if (count > MAX_MODELS - 2) {
        DEBUG("too many models");
        return Q_ERR_INVALID_FORMAT;
    }

    bsp->nummodels = count;
    bsp->models = ALLOC(sizeof(*out) * count);

    out = bsp->models;
    for (i = 0; i < count; i++, out++) {
        // spread the mins / maxs by a pixel
        for (j = 0; j < 3; j++)
            out->mins[j] = BSP_Float() - 1;
        for (j = 0; j < 3; j++)
            out->maxs[j] = BSP_Float() + 1;
        for (j = 0; j < 3; j++)
            out->origin[j] = BSP_Float();

        headnode = BSP_Long();
        if (headnode & BIT(31)) {
            // be careful, some models have no nodes, just a leaf
            headnode = ~headnode;
            if (headnode >= bsp->numleafs) {
                DEBUG("bad headleaf");
                return Q_ERR_INVALID_FORMAT;
            }
            out->headnode = (mnode_t *)(bsp->leafs + headnode);
        } else {
            if (headnode >= bsp->numnodes) {
                DEBUG("bad headnode");
                return Q_ERR_INVALID_FORMAT;
            }
            out->headnode = bsp->nodes + headnode;
        }
#if USE_REF
        if (i == 0) {
            in += 8;
            continue;
        }
        firstface = BSP_Long();
        numfaces = BSP_Long();
        lastface = firstface + numfaces;
        if (lastface < firstface || lastface > bsp->numfaces) {
            DEBUG("bad faces");
            return Q_ERR_INVALID_FORMAT;
        }
        out->firstface = bsp->faces + firstface;
        out->numfaces = numfaces;

        out->radius = RadiusFromBounds(out->mins, out->maxs);
#else
        in += 8;
#endif
    }

    return Q_ERR_SUCCESS;
}

// These are validated after all the areas are loaded
LOAD(AreaPortals)
{
    mareaportal_t   *out;
    int         i;

    bsp->numareaportals = count;
    bsp->areaportals = ALLOC(sizeof(*out) * count);

    out = bsp->areaportals;
    for (i = 0; i < count; i++, out++) {
        out->portalnum = BSP_Long();
        out->otherarea = BSP_Long();
    }

    return Q_ERR_SUCCESS;
}

LOAD(Areas)
{
    marea_t     *out;
    int         i;
    uint32_t    numareaportals, firstareaportal, lastareaportal;

    if (count > MAX_MAP_AREAS) {
        DEBUG("too many areas");
        return Q_ERR_INVALID_FORMAT;
    }

    bsp->numareas = count;
    bsp->areas = ALLOC(sizeof(*out) * count);

    out = bsp->areas;
    for (i = 0; i < count; i++, out++) {
        numareaportals = BSP_Long();
        firstareaportal = BSP_Long();
        lastareaportal = firstareaportal + numareaportals;
        if (lastareaportal < firstareaportal || lastareaportal > bsp->numareaportals) {
            DEBUG("bad areaportals");
            return Q_ERR_INVALID_FORMAT;
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
    memcpy(bsp->entitystring, in, count);
    bsp->entitystring[count] = 0;

    return Q_ERR_SUCCESS;
}

/*
===============================================================================

                    MAP LOADING

===============================================================================
*/

typedef struct {
    const char *name;
    void (*load)(bsp_t *, const byte *, size_t);
    size_t (*parse_header)(bsp_t *, const byte *, size_t);
} xlump_info_t;

typedef struct {
    int (*load)(bsp_t *, const byte *, size_t);
    const char *name;
    uint8_t lump;
    uint8_t disksize[2];
    uint32_t memsize;
} lump_info_t;

typedef struct {
    int ofs;
    const char *name;
} bsp_stat_t;

#define L(name, lump, mem_t, disksize1, disksize2) \
    { BSP_Load##name, #name, lump, { disksize1, disksize2 }, sizeof(mem_t) }

static const lump_info_t bsp_lumps[] = {
    L(Visibility,    3, byte,            1,  1),
    L(Texinfo,       5, mtexinfo_t,     76, 76),
    L(Planes,        1, cplane_t,       20, 20),
    L(BrushSides,   15, mbrushside_t,    4,  8),
    L(Brushes,      14, mbrush_t,       12, 12),
    L(LeafBrushes,  10, mbrush_t *,      2,  4),
    L(AreaPortals,  18, mareaportal_t,   8,  8),
    L(Areas,        17, marea_t,         8,  8),
#if USE_REF
    L(Lightmap,      7, byte,            1,  1),
    L(Vertices,      2, mvertex_t,      12, 12),
    L(Edges,        11, medge_t,         4,  8),
    L(SurfEdges,    12, msurfedge_t,     4,  4),
    L(Faces,         6, mface_t,        20, 28),
    L(LeafFaces,     9, mface_t *,       2,  4),
#endif
    L(Leafs,         8, mleaf_t,        28, 52),
    L(Nodes,         4, mnode_t,        28, 44),
    L(SubModels,    13, mmodel_t,       48, 48),
    L(EntString,     0, char,            1,  1),
};

#undef L

#define F(x) { q_offsetof(bsp_t, num##x), #x }

static const bsp_stat_t bsp_stats[] = {
    F(brushsides),
    F(texinfo),
    F(planes),
    F(nodes),
    F(leafs),
    F(leafbrushes),
    F(models),
    F(brushes),
    F(visibility),
    F(entitychars),
    F(areas),
    F(areaportals),
#if USE_REF
    F(faces),
    F(leaffaces),
    F(lightmapbytes),
    F(vertices),
    F(edges),
    F(surfedges),
#endif
};

#undef F

static list_t   bsp_cache;

static void BSP_PrintStats(bsp_t *bsp)
{
    bool extended = bsp->extended;

    for (int i = 0; i < q_countof(bsp_stats); i++) {
        const bsp_stat_t *s = &bsp_stats[i];
        Com_Printf("%8d : %s\n", *(int *)((byte *)bsp + s->ofs), s->name);
    }
    if (bsp->vis)
        Com_Printf("%8u : clusters\n", bsp->vis->numclusters);

#if USE_REF
    extended |= bsp->lm_decoupled;
#endif

    if (extended) {
        Com_Printf("Features :");
        if (bsp->extended)
            Com_Printf(" QBSP");
#if USE_REF
        if (bsp->lm_decoupled)
            Com_Printf(" DECOUPLED_LM");
#endif
        Com_Printf("\n");
    }
    Com_Printf("Checksum : %#x\n", bsp->checksum);

    Com_Printf("------------------\n");
}

static void BSP_List_f(void)
{
    bsp_t *bsp;
    size_t bytes;
    bool verbose = Cmd_Argc() > 1;

    if (LIST_EMPTY(&bsp_cache)) {
        Com_Printf("BSP cache is empty\n");
        return;
    }

    Com_Printf("------------------\n");
    bytes = 0;

    LIST_FOR_EACH(bsp_t, bsp, &bsp_cache, entry) {
        Com_Printf("%8zu : %s (%d refs)\n",
                   bsp->hunk.mapped, bsp->name, bsp->refcount);
        if (verbose)
            BSP_PrintStats(bsp);
        bytes += bsp->hunk.mapped;
    }
    Com_Printf("Total resident: %zu\n", bytes);
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

static int BSP_SetParent(mnode_t *node, unsigned key)
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

static int BSP_ValidateTree(bsp_t *bsp)
{
    mmodel_t *mod;
    int i, ret;
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
static int BSP_ValidateAreaPortals(bsp_t *bsp)
{
    mareaportal_t   *p;
    int             i;

    bsp->numportals = 0;
    for (i = 0, p = bsp->areaportals; i < bsp->numareaportals; i++, p++) {
        if (p->portalnum >= bsp->numareaportals) {
            DEBUG("bad portalnum");
            return Q_ERR_INVALID_FORMAT;
        }
        if (p->otherarea >= bsp->numareas) {
            DEBUG("bad otherarea");
            return Q_ERR_INVALID_FORMAT;
        }
        bsp->numportals = max(bsp->numportals, p->portalnum + 1);
    }

    return Q_ERR_SUCCESS;
}

void BSP_Free(bsp_t *bsp)
{
    if (!bsp) {
        return;
    }
    Q_assert(bsp->refcount > 0);
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
static bool BSP_GetPatchedPVSFileName(const char* map_path, char pvs_path[MAX_QPATH])
{
	int path_len = strlen(map_path);
	if (path_len < 5 || strcmp(map_path + path_len - 4, ".bsp") != 0)
		return false;

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

	return true;
}

// Loads the first- and second-order PVS matrices from a file called `maps/pvs/<mapname>.bin`
static bool BSP_LoadPatchedPVS(bsp_t *bsp)
{
	char pvs_path[MAX_QPATH];

	if (!BSP_GetPatchedPVSFileName(bsp->name, pvs_path))
		return false;

	unsigned char* filebuf = 0;
	int filelen = 0;
	filelen = FS_LoadFile(pvs_path, (void**)&filebuf);

	if (filebuf == 0)
		return false;

	size_t matrix_size = bsp->visrowsize * bsp->vis->numclusters;
	if (filelen != matrix_size * 2)
	{
		FS_FreeFile(filebuf);
		return false;
	}

	bsp->pvs_matrix = Z_Malloc(matrix_size);
	memcpy(bsp->pvs_matrix, filebuf, matrix_size);

	bsp->pvs2_matrix = Z_Malloc(matrix_size);
	memcpy(bsp->pvs2_matrix, filebuf + matrix_size, matrix_size);

	FS_FreeFile(filebuf);
	return true;
}

// Saves the first- and second-order PVS matrices to a file called `maps/pvs/<mapname>.bin`
bool BSP_SavePatchedPVS(bsp_t *bsp)
{
	char pvs_path[MAX_QPATH];

	if (!BSP_GetPatchedPVSFileName(bsp->name, pvs_path))
		return false;

	if (!bsp->pvs_matrix)
		return false;

	if (!bsp->pvs2_matrix)
		return false;

	size_t matrix_size = bsp->visrowsize * bsp->vis->numclusters;
	unsigned char* filebuf = Z_Malloc(matrix_size * 2);

	memcpy(filebuf, bsp->pvs_matrix, matrix_size);
	memcpy(filebuf + matrix_size, bsp->pvs2_matrix, matrix_size);

	int err = FS_WriteFile(pvs_path, filebuf, matrix_size * 2);

	Z_Free(filebuf);

	if (err >= 0)
		return true;
	else
		return false;
}

#if USE_CLIENT

int BSP_LoadMaterials(bsp_t *bsp)
{
    char path[MAX_QPATH];
    mtexinfo_t *out, *tex;
    int i, j, step_id = FOOTSTEP_RESERVED_COUNT;
    qhandle_t f;

    for (i = 0, out = bsp->texinfo; i < bsp->numtexinfo; i++, out++) {
        // see if already loaded material for this texinfo
        for (j = i - 1; j >= 0; j--) {
            tex = &bsp->texinfo[j];
            if (!Q_stricmp(tex->name, out->name)) {
                strcpy(out->step_material, tex->step_material);
                out->step_id = tex->step_id;
                break;
            }
        }
        if (j != -1)
            continue;

        // load material file
        Q_concat(path, sizeof(path), "textures/", out->name, ".mat");
        FS_OpenFile(path, &f, FS_MODE_READ | FS_FLAG_LOADFILE);
        if (f) {
            FS_Read(out->step_material, sizeof(out->step_material) - 1, f);
            FS_CloseFile(f);
        }

        if (out->step_material[0] && !COM_IsPath(out->step_material)) {
            Com_WPrintf("Bad material \"%s\" in %s\n", Com_MakePrintable(out->step_material), path);
            out->step_material[0] = 0;
        }

        if (!out->step_material[0] || !Q_stricmp(out->step_material, "default")) {
            out->step_id = FOOTSTEP_ID_DEFAULT;
            continue;
        }

        if (!Q_stricmp(out->step_material, "ladder")) {
            out->step_id = FOOTSTEP_ID_LADDER;
            continue;
        }

        // see if already allocated step_id for this material
        for (j = i - 1; j >= 0; j--) {
            tex = &bsp->texinfo[j];
            if (!Q_stricmp(tex->step_material, out->step_material)) {
                out->step_id = tex->step_id;
                break;
            }
        }

        // allocate new step_id
        if (j == -1)
            out->step_id = step_id++;
    }

    Com_DPrintf("%s: %d materials loaded\n", __func__, step_id);
    return step_id;
}

#endif

#if USE_REF
static void BSP_LoadBspxNormals(bsp_t* bsp, const byte* in, size_t data_size)
{
	if (data_size < sizeof(uint32_t))
		return;

	// Count the total number of face-vertices in the BSP
	uint32_t total_vertices = 0;
	for (int i = 0; i < bsp->numfaces; i++)
	{
		mface_t* face = bsp->faces + i;
		total_vertices += face->numsurfedges;
	}

	// Validate the header and that all data fits into the lump
	uint32_t num_vectors = BSP_Long();
	size_t expected_data_size =
		sizeof(uint32_t) +
		sizeof(vec3_t) * num_vectors +            // vectors
		sizeof(uint32_t) * 3 * total_vertices;    // indices
	if (data_size < expected_data_size)
		return;

	// Allocate the storage arrays
	bsp->basisvectors = ALLOC(sizeof(vec3_t) * num_vectors);
	bsp->numbasisvectors = num_vectors;
	bsp->bases = ALLOC(sizeof(mbasis_t) * total_vertices);
	bsp->numbases = total_vertices;

	// Copy the vectors data
	for (uint32_t i = 0; i < num_vectors; i++) {
		for (int j = 0; j < 3; j++)
			bsp->basisvectors[i][j] = BSP_Float();
	}

	// Copy the indices data
	for (uint32_t i = 0; i < total_vertices; i++) {
		bsp->bases[i].normal = BSP_Long();
		bsp->bases[i].tangent = BSP_Long();
		bsp->bases[i].bitangent = BSP_Long();
	}

	// Add basis indexing
	int basis_offset = 0;
	for (int i = 0; i < bsp->numfaces; i++)
	{
		mface_t* face = bsp->faces + i;
		face->firstbasis = basis_offset;
		basis_offset += face->numsurfedges;
	}
}

static size_t BSP_ParseNormalsHeader(bsp_t* bsp, const byte* in, size_t data_size)
{
    return data_size + HUNK_ALIGN - 1; // extra memory to account for alignment in ALLOC()
}

static void BSP_ParseDecoupledLM(bsp_t *bsp, const byte *in, size_t filelen)
{
    mface_t *out;
    uint32_t offset;

    if (filelen % 40)
        return;
    if (bsp->numfaces > filelen / 40)
        return;

    out = bsp->faces;
    for (int i = 0; i < bsp->numfaces; i++, out++) {
        out->lm_width = BSP_Short();
        out->lm_height = BSP_Short();

        offset = BSP_Long();
        if (offset < bsp->numlightmapbytes)
            out->lightmap = bsp->lightmap + offset;

        for (int j = 0; j < 2; j++) {
            out->lm_axis[j][0] = BSP_Float();
            out->lm_axis[j][1] = BSP_Float();
            out->lm_axis[j][2] = BSP_Float();
            out->lm_offset[j] = BSP_Float();
        }
    }

    bsp->lm_decoupled = true;
}

static const xlump_info_t bspx_lumps[] = {
    { "DECOUPLED_LM", BSP_ParseDecoupledLM },
    { "FACENORMALS", BSP_LoadBspxNormals, BSP_ParseNormalsHeader }
};

// returns amount of extra data to allocate
static size_t BSP_ParseExtensionHeader(bsp_t *bsp, lump_t *out, const byte *buf, uint32_t pos, uint32_t filelen)
{
    pos = ALIGN(pos, 4);
    if (pos > filelen - 8)
        return 0;
    if (RL32(buf + pos) != BSPXHEADER)
        return 0;
    pos += 8;

    uint32_t numlumps = RL32(buf + pos - 4);
    if (numlumps > (filelen - pos) / sizeof(xlump_t)) {
        Com_WPrintf("Bad BSPX header\n");
        return 0;
    }

    size_t extrasize = 0;
    xlump_t *l = (xlump_t *)(buf + pos);
    for (int i = 0; i < numlumps; i++, l++) {
        uint32_t ofs = LittleLong(l->fileofs);
        uint32_t len = LittleLong(l->filelen);
        uint32_t end = ofs + len;
        if (end <= ofs || end > filelen)
            continue;
        for (int j = 0; j < q_countof(bspx_lumps); j++) {
            const xlump_info_t *e = &bspx_lumps[j];
            if (strcmp(l->name, e->name))
                continue;
            if (out[j].filelen) {
                Com_WPrintf("Duplicate %s lump\n", e->name);
                break;
            }
            if (e->parse_header)
                extrasize += ALIGN(e->parse_header(bsp, buf + ofs, len), HUNK_ALIGN); // to mirror Hunk_TryAlloc() overallocation
            out[j].fileofs = ofs;
            out[j].filelen = len;
            break;
        }
    }

    return extrasize;
}

#endif

/*
==================
BSP_Load

Loads in the map and all submodels
==================
*/
int BSP_Load(const char *name, bsp_t **bsp_p)
{
    bsp_t           *bsp;
    byte            *buf;
    dheader_t       *header;
    const lump_info_t *info;
    uint32_t        filelen, ofs, len, end, count, maxpos;
    int             i, ret;
    uint32_t        lump_ofs[q_countof(bsp_lumps)];
    uint32_t        lump_count[q_countof(bsp_lumps)];
    size_t          memsize;
    bool            extended = false;

    Q_assert(name);
    Q_assert(bsp_p);

    *bsp_p = NULL;

    if (!*name)
        return Q_ERR(ENOENT);

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

    if (filelen < sizeof(dheader_t)) {
        ret = Q_ERR_FILE_TOO_SMALL;
        goto fail2;
    }

    // byte swap and validate the header
    header = (dheader_t *)buf;
    switch (LittleLong(header->ident)) {
    case IDBSPHEADER:
        break;
    case IDBSPHEADER_EXT:
        extended = true;
        break;
    default:
        ret = Q_ERR_UNKNOWN_FORMAT;
        goto fail2;
    }
    if (LittleLong(header->version) != BSPVERSION) {
        ret = Q_ERR_UNKNOWN_FORMAT;
        goto fail2;
    }

    // byte swap and validate all lumps
    memsize = 0;
    maxpos = 0;
    for (i = 0, info = bsp_lumps; i < q_countof(bsp_lumps); i++, info++) {
        ofs = LittleLong(header->lumps[info->lump].fileofs);
        len = LittleLong(header->lumps[info->lump].filelen);
        end = ofs + len;
        if (end < ofs || end > filelen) {
            Com_SetLastError(va("%s lump out of bounds", info->name));
            ret = Q_ERR_INVALID_FORMAT;
            goto fail2;
        }
        if (len % info->disksize[extended]) {
            Com_SetLastError(va("%s lump has odd size", info->name));
            ret = Q_ERR_INVALID_FORMAT;
            goto fail2;
        }
        count = len / info->disksize[extended];
        Q_assert(count <= INT_MAX / info->memsize);

        lump_ofs[i] = ofs;
        lump_count[i] = count;

        // round to cacheline
        memsize += ALIGN(count * info->memsize, HUNK_ALIGN);
        maxpos = max(maxpos, end);
    }

    // load into hunk
    len = strlen(name);
    bsp = Z_Mallocz(sizeof(*bsp) + len);
    memcpy(bsp->name, name, len + 1);
    bsp->refcount = 1;
    bsp->extended = extended;

#if USE_REF
    lump_t ext[q_countof(bspx_lumps)] = { 0 };
    memsize += BSP_ParseExtensionHeader(bsp, ext, buf, maxpos, filelen);
#endif

    Hunk_Begin(&bsp->hunk, memsize);

    // calculate the checksum
    bsp->checksum = Com_BlockChecksum(buf, filelen);

    // load all lumps
    for (i = 0; i < q_countof(bsp_lumps); i++) {
        ret = bsp_lumps[i].load(bsp, buf + lump_ofs[i], lump_count[i]);
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
		bsp->pvs_patched = true;
	}

#if USE_REF
    // load extension lumps
    for (i = 0; i < q_countof(bspx_lumps); i++) {
        if (ext[i].filelen) {
            bspx_lumps[i].load(bsp, buf + ext[i].fileofs, ext[i].filelen);
        }
    }
#endif

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

const char *BSP_ErrorString(int err)
{
    switch (err) {
    case Q_ERR_INVALID_FORMAT:
    case Q_ERR_INFINITE_LOOP:
        return Com_GetLastError();
    default:
        return Q_ErrorString(err);
    }
}

/*
===============================================================================

HELPER FUNCTIONS

===============================================================================
*/

#if USE_REF

static lightpoint_t *light_point;

static bool BSP_RecursiveLightPoint(mnode_t *node, float p1f, float p2f, const vec3_t p1, const vec3_t p2)
{
    vec_t d1, d2, frac, midf, s, t;
    vec3_t mid;
    int i, side;
    mface_t *surf;

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
            return true;

        for (i = 0, surf = node->firstface; i < node->numfaces; i++, surf++) {
            if (!surf->lightmap)
                continue;
            if (surf->drawflags & SURF_NOLM_MASK)
                continue;

            s = DotProduct(surf->lm_axis[0], mid) + surf->lm_offset[0];
            t = DotProduct(surf->lm_axis[1], mid) + surf->lm_offset[1];
            if (s < 0 || s > surf->lm_width - 1)
                continue;
            if (t < 0 || t > surf->lm_height - 1)
                continue;

            light_point->surf = surf;
            light_point->plane = *surf->plane;
            light_point->s = s;
            light_point->t = t;
            light_point->fraction = midf;
            return true;
        }

        // check far side
        return BSP_RecursiveLightPoint(node->children[side ^ 1], midf, p2f, mid, p2);
    }

    return false;
}

void BSP_LightPoint(lightpoint_t *point, const vec3_t start, const vec3_t end, mnode_t *headnode)
{
    light_point = point;
    light_point->surf = NULL;
    light_point->fraction = 1;

    BSP_RecursiveLightPoint(headnode, 0, 1, start, end);
}

void BSP_TransformedLightPoint(lightpoint_t *point, const vec3_t start, const vec3_t end,
                               mnode_t *headnode, const vec3_t origin, const vec3_t angles)
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
        if (c > out_end - out) {
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
        } else if (bsp->checksum == 0x1ebe8001) {
            // mgu6m2, waterfall
            Q_SetBit(mask, 213);
            Q_SetBit(mask, 214);
            Q_SetBit(mask, 217);
        }
    }

    return mask;
}

mleaf_t *BSP_PointLeaf(mnode_t *node, const vec3_t p)
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

    Q_assert(bsp);
    Q_assert(name);
    Q_assert(name[0] == '*');

    num = Q_atoi(name + 1);
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

