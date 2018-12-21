/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2008 Andrey Nazarov

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

#ifndef BSP_H
#define BSP_H

#include "shared/list.h"
#include "common/error.h"
#include "system/hunk.h"
#include "format/bsp.h"

#ifndef MIPLEVELS
#define MIPLEVELS 4
#endif

// maximum size of a PVS row, in bytes
#define VIS_MAX_BYTES   (MAX_MAP_LEAFS >> 3)

// take advantage of 64-bit systems
#define VIS_FAST_LONGS(bsp) \
    (((bsp)->visrowsize + sizeof(uint_fast32_t) - 1) / sizeof(uint_fast32_t))

typedef struct mtexinfo_s {  // used internally due to name len probs //ZOID
    csurface_t          c;
    char                name[MAX_TEXNAME];

#if USE_REF
    int                 radiance;
    vec3_t              axis[2];
    vec2_t              offset;
    struct image_s      *image; // used for texturing
    int                 numframes;
    struct mtexinfo_s   *next; // used for animation
#if USE_REF == REF_SOFT
    vec_t               mipadjust;
#endif
#endif
} mtexinfo_t;

#if USE_REF
typedef struct {
    vec3_t      point;
} mvertex_t;

typedef struct {
    mvertex_t   *v[2];
#if USE_REF == REF_SOFT
    uintptr_t   cachededgeoffset;
#endif
} medge_t;

typedef struct {
    medge_t     *edge;
    int         vert;
} msurfedge_t;

#define SURF_TRANS_MASK (SURF_TRANS33 | SURF_TRANS66)
#define SURF_COLOR_MASK (SURF_TRANS_MASK | SURF_WARP)
#define SURF_NOLM_MASK  (SURF_COLOR_MASK | SURF_FLOWING | SURF_SKY)

#define DSURF_PLANEBACK     1

// for lightmap block calculation
#define S_MAX(surf) (((surf)->extents[0] >> 4) + 1)
#define T_MAX(surf) (((surf)->extents[1] >> 4) + 1)

typedef struct mface_s {
    msurfedge_t     *firstsurfedge;
    int             numsurfedges;

    cplane_t        *plane;
    int             drawflags; // DSURF_PLANEBACK, etc

    byte            *lightmap;
    byte            styles[MAX_LIGHTMAPS];
    int             numstyles;

    mtexinfo_t      *texinfo;
    int             texturemins[2];
    int             extents[2];

#if USE_REF == REF_GL || USE_REF == REF_GLPT
    int             texnum[2];
    int             statebits;
    int             firstvert;
    int             light_s, light_t;
    float           stylecache[MAX_LIGHTMAPS];
#else
    struct surfcache_s    *cachespots[MIPLEVELS]; // surface generation data
#endif

    int             drawframe;

#if USE_DLIGHTS
    int             dlightframe;
    int             dlightbits;
#endif
    struct mface_s  *next;
} mface_t;
#endif

typedef struct mnode_s {
    /* ======> */
    cplane_t            *plane;     // never NULL to differentiate from leafs
#if USE_REF
    union {
        vec_t           minmaxs[6];
        struct {
            vec3_t      mins;
            vec3_t      maxs;
        };
    };

    int                 visframe;
#endif
    struct mnode_s      *parent;
    /* <====== */

    struct mnode_s      *children[2];

#if USE_REF
    int                 numfaces;
    mface_t             *firstface;
#endif
} mnode_t;

typedef struct {
    cplane_t            *plane;
    mtexinfo_t          *texinfo;
} mbrushside_t;

typedef struct {
    int                 contents;
    int                 numsides;
    mbrushside_t        *firstbrushside;
    int                 checkcount;        // to avoid repeated testings
} mbrush_t;

typedef struct {
    /* ======> */
    cplane_t            *plane;     // always NULL to differentiate from nodes
#if USE_REF
    vec3_t              mins;
    vec3_t              maxs;

    int                 visframe;
#endif
    struct mnode_s      *parent;
    /* <====== */

    int             contents;
    int             cluster;
    int             area;
    mbrush_t        **firstleafbrush;
    int             numleafbrushes;
#if USE_REF
    mface_t         **firstleafface;
    int             numleaffaces;
#if USE_REF == REF_SOFT
    unsigned        key;
#endif
#endif
} mleaf_t;

typedef struct {
    unsigned    portalnum;
    unsigned    otherarea;
} mareaportal_t;

typedef struct {
    int             numareaportals;
    mareaportal_t   *firstareaportal;
    int             floodvalid;
} marea_t;

typedef struct mmodel_s {
#if USE_REF
    /* ======> */
    int             type;
    /* <====== */
#endif
    vec3_t          mins, maxs;
    vec3_t          origin;        // for sounds or lights
    mnode_t         *headnode;

#if USE_REF
    float           radius;

    int             numfaces;
    mface_t         *firstface;

#if USE_REF == REF_GL
    int             drawframe;
#endif
#endif
} mmodel_t;

typedef struct bsp_s {
    list_t      entry;
    int         refcount;

    unsigned    checksum;

    memhunk_t   hunk;

    int             numbrushsides;
    mbrushside_t    *brushsides;

    int             numtexinfo;
    mtexinfo_t      *texinfo;

    int             numplanes;
    cplane_t        *planes;

    int             numnodes;
    mnode_t         *nodes;

    int             numleafs;
    mleaf_t         *leafs;

    int             numleafbrushes;
    mbrush_t        **leafbrushes;

    int             nummodels;
    mmodel_t        *models;

    int             numbrushes;
    mbrush_t        *brushes;

    int             numvisibility;
    int             visrowsize;
    dvis_t          *vis;

    int             numentitychars;
    char            *entitystring;

    int             numareas;
    marea_t         *areas;

    int             lastareaportal; // largest portal number used
    int             numareaportals; // size of the array below
    mareaportal_t   *areaportals;

#if USE_REF
    int             numfaces;
    mface_t         *faces;

    int             numleaffaces;
    mface_t         **leaffaces;

    int             numlightmapbytes;
    byte            *lightmap;

    int             numvertices;
    mvertex_t       *vertices;

    int             numedges;
    medge_t         *edges;

    int             numsurfedges;
    msurfedge_t     *surfedges;
#endif

    char            name[1];
} bsp_t;

qerror_t BSP_Load(const char *name, bsp_t **bsp_p);
void BSP_Free(bsp_t *bsp);
const char *BSP_GetError(void);

#if USE_REF
typedef struct {
    mface_t     *surf;
    cplane_t    plane;
    int         s, t;
    float       fraction;
} lightpoint_t;

void BSP_LightPoint(lightpoint_t *point, vec3_t start, vec3_t end, mnode_t *headnode);
void BSP_TransformedLightPoint(lightpoint_t *point, vec3_t start, vec3_t end,
                               mnode_t *headnode, vec3_t origin, vec3_t angles);
#endif

byte *BSP_ClusterVis(bsp_t *bsp, byte *mask, int cluster, int vis);
mleaf_t *BSP_PointLeaf(mnode_t *node, vec3_t p);
mmodel_t *BSP_InlineModel(bsp_t *bsp, const char *name);

void BSP_Init(void);

#endif // BSP_H
