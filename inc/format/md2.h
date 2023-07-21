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

#ifndef FORMAT_MD2_H
#define FORMAT_MD2_H

/*
========================================================================

.MD2 triangle model file format

========================================================================
*/

#define MD2_IDENT       MakeLittleLong('I','D','P','2')
#define MD2_VERSION     8

#define MD2_MAX_TRIANGLES   4096
#define MD2_MAX_VERTS       2048
#define MD2_MAX_FRAMES      512
#define MD2_MAX_SKINS       32
#define MD2_MAX_SKINNAME    64
#define MD2_MAX_SKINWIDTH   640
#define MD2_MAX_SKINHEIGHT  480

typedef struct {
    int16_t    s;
    int16_t    t;
} dmd2stvert_t;

typedef struct {
    uint16_t    index_xyz[3];
    uint16_t    index_st[3];
} dmd2triangle_t;

typedef struct {
    uint8_t    v[3];            // scaled byte to fit in frame mins/maxs
    uint8_t    lightnormalindex;
} dmd2trivertx_t;

typedef struct {
    float           scale[3];       // multiply byte verts by this
    float           translate[3];   // then add this
    char            name[16];       // frame name from grabbing
    dmd2trivertx_t  verts[1];       // variable sized
} dmd2frame_t;

#define MD2_MAX_FRAMESIZE \
    (sizeof(dmd2frame_t) + sizeof(dmd2trivertx_t) * (MD2_MAX_VERTS - 1))

// the glcmd format:
// a positive integer starts a tristrip command, followed by that many
// vertex structures.
// a negative integer starts a trifan command, followed by -x vertexes
// a zero indicates the end of the command list.
// a vertex consists of a floating point s, a floating point t,
// and an integer vertex index.


typedef struct dmd2header_s {
    uint32_t        ident;
    uint32_t        version;

    uint32_t        skinwidth;
    uint32_t        skinheight;
    uint32_t        framesize;      // byte size of each frame

    uint32_t        num_skins;
    uint32_t        num_xyz;
    uint32_t        num_st;         // greater than num_xyz for seams
    uint32_t        num_tris;
    uint32_t        num_glcmds;     // dwords in strip/fan command list
    uint32_t        num_frames;

    uint32_t        ofs_skins;      // each skin is a MAX_SKINNAME string
    uint32_t        ofs_st;         // byte offset from start for stverts
    uint32_t        ofs_tris;       // offset for dtriangles
    uint32_t        ofs_frames;     // offset for first frame
    uint32_t        ofs_glcmds;
    uint32_t        ofs_end;        // end of file
} dmd2header_t;

#endif // FORMAT_MD2_H
