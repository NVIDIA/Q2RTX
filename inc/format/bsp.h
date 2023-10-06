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
#define IDBSPHEADER_EXT MakeLittleLong('Q','B','S','P')
#define BSPVERSION      38


// can't be increased without changing network protocol
#define     MAX_MAP_AREAS       256
#define     MAX_MAP_LEAFS       65536

// arbitrary limit
#define     MAX_MAP_AREAPORTALS 1024

// QBSP stuff
#define QBSPHEADER    (('P'<<24)+('S'<<16)+('B'<<8)+'Q')

#define     MAX_QBSP_MAP_LEAFS       INT_MAX

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

#define    MAX_LIGHTMAPS    4

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

//=============================================================================

#define BSPXHEADER      MakeLittleLong('B','S','P','X')

typedef struct {
    char        name[24];
    uint32_t    fileofs;
    uint32_t    filelen;
} xlump_t;

#endif // FORMAT_BSP_H
