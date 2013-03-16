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

#include "sw.h"

//static float        sky_rotate;
//static vec3_t       sky_axis;

static int          r_skyframe;
static mface_t      r_skyfaces[6];
static cplane_t     r_skyplanes[6];
static mtexinfo_t   r_skytexinfo[6];
static mvertex_t    r_skyverts[8];
static medge_t      r_skyedges[12];
static msurfedge_t  r_skysurfedges[24];

// 3dstudio environment map names
static const char       r_skysidenames[6][3] = { "rt", "bk", "lf", "ft", "up", "dn" };
static const int        r_skysideimage[6] = { 5, 2, 4, 1, 0, 3 };

// I just copied this data from a box map...
static const int box_planes[12] = {
    2, -128, 0, -128, 2, 128, 1, 128, 0, 128, 1, -128
};
static const int box_surfedges[24] = {
    1, 2, 3, 4,  1, 5, 6, 7,  8, 9, 6, 10,  2, 7, 9, 11, 12, 3, 11, 8,  12, 10, 5, 4
};
static const int box_surfverts[24] = {
    0, 0, 0, 0,  1, 0, 0, 0,  0, 0, 1, 0,  1, 1, 1, 0,  0, 1, 1, 1,  1, 1, 1, 1
};
static const int box_edges[24] = {
    1, 2, 2, 3, 3, 4, 4, 1, 1, 5, 5, 6, 6, 2, 7, 8, 8, 6, 5, 7, 8, 3, 7, 4
};
static const int box_flags[6] = {
    0, 0, 1, 1, 1, 0
};
static const vec3_t box_axis[6][2] = {
    { {0, -1, 0}, { -1, 0, 0} },
    { {0, 1, 0}, {0, 0, -1} },
    { {0, -1, 0}, {1, 0, 0} },
    { {1, 0, 0}, {0, 0, -1} },
    { {0, -1, 0}, {0, 0, -1} },
    { { -1, 0, 0}, {0, 0, -1} }
};
static const vec3_t box_verts[8] = {
    { -1, -1, -1},
    { -1, 1, -1},
    {1, 1, -1},
    {1, -1, -1},
    { -1, -1, 1},
    { -1, 1, 1},
    {1, -1, 1},
    {1, 1, 1}
};

/*
================
R_InitSkyBox
================
*/
void R_InitSkyBox(void)
{
    int i;

    for (i = 0; i < 6; i++) {
        r_skyplanes[i].normal[box_planes[i * 2]] = 1;
        r_skyplanes[i].dist = box_planes[i * 2 + 1];

        VectorCopy(box_axis[i][0], r_skytexinfo[i].axis[0]);
        VectorCopy(box_axis[i][1], r_skytexinfo[i].axis[1]);

        r_skyfaces[i].plane = &r_skyplanes[i];
        r_skyfaces[i].drawflags = box_flags[i] | DSURF_SKY;
        r_skyfaces[i].numsurfedges = 4;
        r_skyfaces[i].firstsurfedge = &r_skysurfedges[i * 4];
        r_skyfaces[i].texinfo = &r_skytexinfo[i];
        r_skyfaces[i].texturemins[0] = -128;
        r_skyfaces[i].texturemins[1] = -128;
        r_skyfaces[i].extents[0] = 256;
        r_skyfaces[i].extents[1] = 256;
    }

    for (i = 0; i < 24; i++) {
        r_skysurfedges[i].edge = &r_skyedges[box_surfedges[i] - 1];
        r_skysurfedges[i].vert = box_surfverts[i];
    }

    for (i = 0; i < 12; i++) {
        r_skyedges[i].v[0] = &r_skyverts[box_edges[i * 2 + 0] - 1];
        r_skyedges[i].v[1] = &r_skyverts[box_edges[i * 2 + 1] - 1];
        r_skyedges[i].cachededgeoffset = 0;
    }
}

/*
================
R_EmitSkyBox
================
*/
void R_EmitSkyBox(void)
{
    int i, j;
    int oldkey;

    if (insubmodel)
        return;        // submodels should never have skies
    if (r_skyframe == r_framecount)
        return;        // already set this frame

    r_skyframe = r_framecount;

    // set the eight fake vertexes
    for (i = 0; i < 8; i++)
        for (j = 0; j < 3; j++)
            r_skyverts[i].point[j] = r_origin[j] + box_verts[i][j] * 128;

    // set the six fake planes
    for (i = 0; i < 6; i++)
        if (box_planes[i * 2 + 1] > 0)
            r_skyplanes[i].dist = r_origin[box_planes[i * 2]] + 128;
        else
            r_skyplanes[i].dist = r_origin[box_planes[i * 2]] - 128;

    // fix texture offsets
    for (i = 0; i < 6; i++) {
        r_skytexinfo[i].offset[0] = -DotProduct(r_origin, r_skytexinfo[i].axis[0]);
        r_skytexinfo[i].offset[1] = -DotProduct(r_origin, r_skytexinfo[i].axis[1]);
    }

    // emit the six faces
    oldkey = r_currentkey;
    r_currentkey = 0x7ffffff0;
    for (i = 0; i < 6; i++) {
        R_RenderFace(&r_skyfaces[i], 15);
    }
    r_currentkey = oldkey;  // bsp sorting order
}

/*
============
R_SetSky
============
*/
void R_SetSky(const char *name, float rotate, vec3_t axis)
{
    int     i;
    char    path[MAX_QPATH];

//    sky_rotate = rotate;
//    VectorCopy(axis, sky_axis);

    for (i = 0; i < 6; i++) {
        Q_concat(path, sizeof(path), "env/", name,
                 r_skysidenames[r_skysideimage[i]], ".tga", NULL);
        FS_NormalizePath(path, path);
        r_skytexinfo[i].image = IMG_Find(path, IT_SKY, IF_NONE);
    }
}

