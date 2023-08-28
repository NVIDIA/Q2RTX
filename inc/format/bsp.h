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

// arbitrary limit
#define     MAX_MAP_CLUSTERS    65536

// QBSP stuff
#define QBSPHEADER    (('P'<<24)+('S'<<16)+('B'<<8)+'Q')

// key / value pair sizes

#define     MAX_KEY         32
#define     MAX_VALUE       1024

#define     MAX_TEXNAME     32

//=============================================================================

typedef struct {
    uint32_t        fileofs, filelen;
} lump_t;

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
