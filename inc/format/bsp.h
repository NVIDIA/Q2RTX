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

#ifndef FORMAT_BSP_H
#define FORMAT_BSP_H

/*
==============================================================================

.BSP file format

==============================================================================
*/

#define IDBSPHEADER     MakeLittleLong('I','B','S','P')
#define BSPVERSION      38


// upper design bounds
// leaffaces, leafbrushes, planes, and verts are still bounded by
// 16 bit short limits
#define     MAX_MAP_MODELS      1024
#define     MAX_MAP_BRUSHES     8192
#define     MAX_MAP_ENTITIES    2048
#define     MAX_MAP_ENTSTRING   0x40000
#define     MAX_MAP_TEXINFO     8192

#define     MAX_MAP_AREAS       256
#define     MAX_MAP_AREAPORTALS 1024
#define     MAX_MAP_PLANES      65536
#define     MAX_MAP_NODES       65536
#define     MAX_MAP_BRUSHSIDES  65536
#define     MAX_MAP_LEAFS       65536
#define     MAX_MAP_VERTS       65536
#define     MAX_MAP_VERTEXES    MAX_MAP_VERTS
#define     MAX_MAP_FACES       65536
#define     MAX_MAP_LEAFFACES   65536
#define     MAX_MAP_LEAFBRUSHES 65536
#define     MAX_MAP_PORTALS     65536
#define     MAX_MAP_EDGES       128000
#define     MAX_MAP_SURFEDGES   256000
#define     MAX_MAP_LIGHTING    0x800000
#define     MAX_MAP_VISIBILITY  0x100000

// QBSP stuff
#define QBSPHEADER    (('P'<<24)+('S'<<16)+('B'<<8)+'Q')

// upper design bounds
// leaffaces, leafbrushes, planes, and verts are still bounded by
// 16 bit short limits
#define     MAX_QBSP_MAP_MODELS      INT_MAX
#define     MAX_QBSP_MAP_BRUSHES     INT_MAX
#define     MAX_QBSP_MAP_ENTITIES    INT_MAX
#define     MAX_QBSP_MAP_ENTSTRING   INT_MAX
#define     MAX_QBSP_MAP_TEXINFO     INT_MAX

#define     MAX_QBSP_MAP_AREAS       INT_MAX
#define     MAX_QBSP_MAP_AREAPORTALS INT_MAX
#define     MAX_QBSP_MAP_PLANES      INT_MAX
#define     MAX_QBSP_MAP_NODES       INT_MAX
#define     MAX_QBSP_MAP_BRUSHSIDES  INT_MAX
#define     MAX_QBSP_MAP_LEAFS       INT_MAX
#define     MAX_QBSP_MAP_VERTS       INT_MAX
#define     MAX_QBSP_MAP_VERTEXES    INT_MAX
#define     MAX_QBSP_MAP_FACES       INT_MAX
#define     MAX_QBSP_MAP_LEAFFACES   INT_MAX
#define     MAX_QBSP_MAP_LEAFBRUSHES INT_MAX
#define     MAX_QBSP_MAP_PORTALS     INT_MAX
#define     MAX_QBSP_MAP_EDGES       INT_MAX
#define     MAX_QBSP_MAP_SURFEDGES   INT_MAX
#define     MAX_QBSP_MAP_LIGHTING    INT_MAX
#define     MAX_QBSP_MAP_VISIBILITY  INT_MAX

// key / value pair sizes

#define     MAX_KEY         32
#define     MAX_VALUE       1024

#define     MAX_TEXNAME     32

//=============================================================================

typedef struct {
    uint32_t        fileofs, filelen;
} lump_t;

#define    LUMP_ENTITIES        0
#define    LUMP_ENTSTRING       LUMP_ENTITIES
#define    LUMP_PLANES          1
#define    LUMP_VERTEXES        2
#define    LUMP_VISIBILITY      3
#define    LUMP_NODES           4
#define    LUMP_TEXINFO         5
#define    LUMP_FACES           6
#define    LUMP_LIGHTING        7
#define    LUMP_LEAFS           8
#define    LUMP_LEAFFACES       9
#define    LUMP_LEAFBRUSHES     10
#define    LUMP_EDGES           11
#define    LUMP_SURFEDGES       12
#define    LUMP_MODELS          13
#define    LUMP_BRUSHES         14
#define    LUMP_BRUSHSIDES      15
#define    LUMP_POP             16
#define    LUMP_AREAS           17
#define    LUMP_AREAPORTALS     18
#define    HEADER_LUMPS         19

typedef struct {
    uint32_t    ident;
    uint32_t    version;
    lump_t      lumps[HEADER_LUMPS];
} dheader_t;

typedef struct {
    float       mins[3], maxs[3];
    float       origin[3];              // for sounds or lights
    uint32_t    headnode;
    uint32_t    firstface, numfaces;    // submodels just draw faces
                                        // without walking the bsp tree
} dmodel_t;

typedef struct {
    float    point[3];
} dvertex_t;

typedef struct {
    float       normal[3];
    float       dist;
    uint32_t    type;   // PLANE_X - PLANE_ANYZ ?remove? trivial to regenerate
} dplane_t;

typedef struct {
    uint32_t    planenum;
    uint32_t    children[2];    // negative numbers are -(leafs+1), not nodes
    int16_t     mins[3];        // for frustom culling
    int16_t     maxs[3];
    uint16_t    firstface;
    uint16_t    numfaces;       // counting both sides
} dnode_t;

typedef struct {
    float       vecs[2][4];             // [s/t][xyz offset]
    uint32_t    flags;                  // miptex flags + overrides
    int32_t     value;                  // light emission, etc
    char        texture[MAX_TEXNAME];   // texture name (textures/*.wal)
    uint32_t    nexttexinfo;            // for animations, -1 = end of chain
} dtexinfo_t;

// note that edge 0 is never used, because negative edge nums are used for
// counterclockwise use of the edge in a face
typedef struct {
    uint16_t    v[2];           // vertex numbers
} dedge_t;

#define    MAX_LIGHTMAPS    4

typedef struct {
    uint16_t    planenum;
    uint16_t    side;

    uint32_t    firstedge;      // we must support > 64k edges
    uint16_t    numedges;
    uint16_t    texinfo;

    // lighting info
    uint8_t     styles[MAX_LIGHTMAPS];
    uint32_t    lightofs;       // start of [numstyles*surfsize] samples
} dface_t;

typedef struct {
    uint32_t    contents;       // OR of all brushes (not needed?)

    uint16_t    cluster;
    uint16_t    area;

    int16_t     mins[3];        // for frustum culling
    int16_t     maxs[3];

    uint16_t    firstleafface;
    uint16_t    numleaffaces;

    uint16_t    firstleafbrush;
    uint16_t    numleafbrushes;
} dleaf_t;

typedef struct {
    uint16_t    planenum;        // facing out of the leaf
    uint16_t    texinfo;
} dbrushside_t;

typedef struct {
    uint32_t    firstside;
    uint32_t    numsides;
    uint32_t    contents;
} dbrush_t;

#define ANGLE_UP    -1
#define ANGLE_DOWN  -2


// the visibility lump consists of a header with a count, then
// byte offsets for the PVS and PHS of each cluster, then the raw
// compressed bit vectors
#define DVIS_PVS    0
#define DVIS_PHS    1
#define DVIS_PVS2   16 // Q2RTX : 2nd order PVS

typedef struct {
    uint32_t    numclusters;
    uint32_t    bitofs[][2];    // bitofs[numclusters][2]
} dvis_t;

// each area has a list of portals that lead into other areas
// when portals are closed, other areas may not be visible or
// hearable even if the vis info says that it should be
typedef struct {
    uint32_t    portalnum;
    uint32_t    otherarea;
} dareaportal_t;

typedef struct {
    uint32_t    numareaportals;
    uint32_t    firstareaportal;
} darea_t;

// QBSP versions
typedef struct {
    uint32_t    planenum;
    uint32_t    children[2];    // negative numbers are -(leafs+1), not nodes
    float     mins[3];        // for frustom culling
    float     maxs[3];
    uint32_t    firstface;
    uint32_t    numfaces;       // counting both sides
} dnode_qbsp_t;

typedef struct {
    uint32_t    v[2];           // vertex numbers
} dedge_qbsp_t;

typedef struct {
    uint32_t    planenum;
    uint32_t    side;

    uint32_t    firstedge;      // we must support > 64k edges
    uint32_t    numedges;
    uint32_t    texinfo;

    // lighting info
    uint8_t     styles[MAX_LIGHTMAPS];
    uint32_t    lightofs;       // start of [numstyles*surfsize] samples
} dface_qbsp_t;

typedef struct {
    uint32_t    contents;       // OR of all brushes (not needed?)

    uint32_t    cluster;
    uint32_t    area;

    float     mins[3];        // for frustum culling
    float     maxs[3];

    uint32_t    firstleafface;
    uint32_t    numleaffaces;

    uint32_t    firstleafbrush;
    uint32_t    numleafbrushes;
} dleaf_qbsp_t;

typedef struct {
    uint32_t    planenum;        // facing out of the leaf
    uint32_t    texinfo;
} dbrushside_qbsp_t;

typedef struct {
    char id[4];  // 'BSPX'
    uint32_t numlumps;
} bspx_header_t;

typedef struct {
    char lumpname[24]; // up to 23 chars, zero-padded
    uint32_t fileofs;       // from file start
    uint32_t filelen;
} bspx_lump_t;

typedef struct {
	uint32_t num_vectors;
	/* followed by:
		vec3 vectors[num_vectors]

		for each face in bsp {
			for each vert in face {
				u32 normal_index;
				u32 tangent_index;
				u32 bitangent_index;
			}
		}
	 */
} bspx_facenormals_header_t;

#endif // FORMAT_BSP_H
