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

#ifndef FORMAT_MD3_H
#define FORMAT_MD3_H

/*
=======================================================================

.MD3 triangle model file format

=======================================================================
*/

#define MD3_IDENT           MakeLittleLong('I','D','P','3')
#define MD3_VERSION         15

// limits
#define MD3_MAX_LODS        3
#define MD3_MAX_TRIANGLES   8192    // per mesh
#define MD3_MAX_VERTS       4096    // per mesh
#define MD3_MAX_SKINS       256     // per mesh
#define MD3_MAX_FRAMES      1024    // per model
#define MD3_MAX_MESHES      32      // per model
#define MD3_MAX_TAGS        16      // per frame
#define MD3_MAX_PATH        64

// vertex scales
#define    MD3_XYZ_SCALE        (1.0f/64.0f)

typedef struct {
    float       st[2];
} dmd3coord_t;

typedef struct {
    int16_t     point[3];
    uint8_t     norm[2];
} dmd3vertex_t;

typedef struct {
    float       mins[3];
    float       maxs[3];
    float       translate[3];
    float       radius;
    char        creator[16];
} dmd3frame_t;

typedef struct {
    char        name[MD3_MAX_PATH];     // tag name
    float       origin[3];
    float       axis[3][3];
} dmd3tag_t;

typedef struct {
    char        name[MD3_MAX_PATH];
    uint32_t    unused;                 // shader
} dmd3skin_t;

typedef struct {
    uint32_t    ident;

    char        name[MD3_MAX_PATH];
    uint32_t    flags;

    uint32_t    num_frames;
    uint32_t    num_skins;
    uint32_t    num_verts;
    uint32_t    num_tris;

    uint32_t    ofs_indexes;
    uint32_t    ofs_skins;
    uint32_t    ofs_tcs;
    uint32_t    ofs_verts;

    uint32_t    meshsize;
} dmd3mesh_t;

typedef struct {
    uint32_t    ident;
    uint32_t    version;

    char        filename[MD3_MAX_PATH];

    uint32_t    flags;

    uint32_t    num_frames;
    uint32_t    num_tags;
    uint32_t    num_meshes;
    uint32_t    num_skins;

    uint32_t    ofs_frames;
    uint32_t    ofs_tags;
    uint32_t    ofs_meshes;
    uint32_t    ofs_end;
} dmd3header_t;

#endif // FORMAT_MD3_H
