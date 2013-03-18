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
// r_misc.c

#include "sw.h"

#define NUM_MIPS    4

cvar_t  *sw_mipcap;
cvar_t  *sw_mipscale;

int             d_minmip;
float           d_scalemip[NUM_MIPS - 1];

static const float  basemip[NUM_MIPS - 1] = {1.0, 0.5 * 0.8, 0.25 * 0.8};


/*
=============
R_PrintTimes
=============
*/
void R_PrintTimes(void)
{
    int     r_time2;
    int     ms;

    r_time2 = Sys_Milliseconds();

    ms = r_time2 - r_time1;

    Com_Printf("%5i ms %3i/%3i/%3i poly %3i surf\n",
               ms, c_faceclip, r_polycount, r_drawnpolycount, c_surf);
    c_surf = 0;
}


/*
=============
R_PrintAliasStats
=============
*/
void R_PrintAliasStats(void)
{
    Com_Printf("%3i polygon model drawn\n", r_amodels_drawn);
}



/*
===================
R_TransformFrustum
===================
*/
void R_TransformFrustum(void)
{
    int     i;
    vec3_t  v, v2;

    for (i = 0; i < 4; i++) {
        v[0] = screenedge[i].normal[2];
        v[1] = -screenedge[i].normal[0];
        v[2] = screenedge[i].normal[1];

        v2[0] = v[1] * vright[0] + v[2] * vup[0] + v[0] * vpn[0];
        v2[1] = v[1] * vright[1] + v[2] * vup[1] + v[0] * vpn[1];
        v2[2] = v[1] * vright[2] + v[2] * vup[2] + v[0] * vpn[2];

        VectorCopy(v2, view_clipplanes[i].normal);

        view_clipplanes[i].dist = DotProduct(modelorg, v2);
    }
}


/*
================
R_TransformVector
================
*/
void R_TransformVector(vec3_t in, vec3_t out)
{
    out[0] = DotProduct(in, vright);
    out[1] = DotProduct(in, vup);
    out[2] = DotProduct(in, vpn);
}

#if 0
/*
================
R_TransformPlane
================
*/
void R_TransformPlane(cplane_t *p, float *normal, float *dist)
{
    float   d;

    d = DotProduct(r_origin, p->normal);
    *dist = p->dist - d;
// TODO: when we have rotating entities, this will need to use the view matrix
    R_TransformVector(p->normal, normal);
}
#endif

/*
===============
R_SetUpFrustumIndexes
===============
*/
static void R_SetUpFrustumIndexes(void)
{
    int     i, j, *pindex;

    pindex = r_frustum_indexes;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 3; j++) {
            if (view_clipplanes[i].normal[j] < 0) {
                pindex[j] = j;
                pindex[j + 3] = j + 3;
            } else {
                pindex[j] = j + 3;
                pindex[j + 3] = j;
            }
        }

        // FIXME: do just once at start
        pfrustum_indexes[i] = pindex;
        pindex += 6;
    }
}

/*
===============
R_ViewChanged

Called every time the vid structure or r_refdef changes.
Guaranteed to be called before the first refresh
===============
*/
static void R_ViewChanged(vrect_t *vr)
{
    float   fov_h, fov_v;
    int     i;

    r_refdef.vrect = *vr;

    fov_h = 2 * tan((float)r_newrefdef.fov_x / 360 * M_PI);
    fov_v = 2 * tan((float)r_newrefdef.fov_y / 360 * M_PI);

    r_refdef.fvrectx = (float)r_refdef.vrect.x;
    r_refdef.fvrectx_adj = (float)r_refdef.vrect.x - 0.5;
    r_refdef.vrect_x_adj_shift20 = (r_refdef.vrect.x << 20) + (1 << 19) - 1;
    r_refdef.fvrecty = (float)r_refdef.vrect.y;
    r_refdef.fvrecty_adj = (float)r_refdef.vrect.y - 0.5;
    r_refdef.vrectright = r_refdef.vrect.x + r_refdef.vrect.width;
    r_refdef.vrectright_adj_shift20 = (r_refdef.vrectright << 20) + (1 << 19) - 1;
    r_refdef.fvrectright = (float)r_refdef.vrectright;
    r_refdef.fvrectright_adj = (float)r_refdef.vrectright - 0.5;
    r_refdef.vrectrightedge = (float)r_refdef.vrectright - 0.99;
    r_refdef.vrectbottom = r_refdef.vrect.y + r_refdef.vrect.height;
    r_refdef.fvrectbottom = (float)r_refdef.vrectbottom;
    r_refdef.fvrectbottom_adj = (float)r_refdef.vrectbottom - 0.5;

// values for perspective projection
// if math were exact, the values would range from 0.5 to to range+0.5
// hopefully they wll be in the 0.000001 to range+.999999 and truncate
// the polygon rasterization will never render in the first row or column
// but will definately render in the [range] row and column, so adjust the
// buffer origin to get an exact edge to edge fill
    r_refdef.xcenter = ((float)r_refdef.vrect.width * 0.5) + r_refdef.vrect.x - 0.5;
    r_refdef.ycenter = ((float)r_refdef.vrect.height * 0.5) + r_refdef.vrect.y - 0.5;
    r_refdef.xscale = r_refdef.vrect.width / fov_h;
    r_refdef.xscaleinv = 1.0 / r_refdef.xscale;
    r_refdef.yscale = r_refdef.vrect.height / fov_v;
    r_refdef.yscaleinv = 1.0 / r_refdef.yscale;
    r_refdef.xscaleshrink = (r_refdef.vrect.width - 6) / fov_h;
    r_refdef.yscaleshrink = (r_refdef.vrect.height - 6) / fov_v;

    r_refdef.scale_for_mip = max(r_refdef.xscale, r_refdef.yscale);

    r_refdef.pix_min = r_refdef.vrect.width / 640;
    if (r_refdef.pix_min < 1)
        r_refdef.pix_min = 1;

    r_refdef.pix_max = (int)((float)r_refdef.vrect.width / (640.0 / 4.0) + 0.5);
    r_refdef.pix_shift = 8 - (int)((float)r_refdef.vrect.width / 640.0 + 0.5);
    if (r_refdef.pix_max < 1)
        r_refdef.pix_max = 1;

    r_refdef.vrectright_particle = r_refdef.vrectright - r_refdef.pix_max;
    r_refdef.vrectbottom_particle = r_refdef.vrectbottom - r_refdef.pix_max;

// left side clip
    screenedge[0].normal[0] = -1.0 / (0.5 * fov_h);
    screenedge[0].normal[1] = 0;
    screenedge[0].normal[2] = 1;
    screenedge[0].type = PLANE_ANYZ;

// right side clip
    screenedge[1].normal[0] = 1.0 / (0.5 * fov_h);
    screenedge[1].normal[1] = 0;
    screenedge[1].normal[2] = 1;
    screenedge[1].type = PLANE_ANYZ;

// top side clip
    screenedge[2].normal[0] = 0;
    screenedge[2].normal[1] = -1.0 / (0.5 * fov_v);
    screenedge[2].normal[2] = 1;
    screenedge[2].type = PLANE_ANYZ;

// bottom side clip
    screenedge[3].normal[0] = 0;
    screenedge[3].normal[1] = 1.0 / (0.5 * fov_v);
    screenedge[3].normal[2] = 1;
    screenedge[3].type = PLANE_ANYZ;

    for (i = 0; i < 4; i++)
        VectorNormalize(screenedge[i].normal);
}


/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame(void)
{
    int         i;
    vrect_t     vrect;

    if (r_fullbright->modified) {
        r_fullbright->modified = qfalse;
        D_FlushCaches();    // so all lighting changes
    }

    r_framecount++;


// build the transformation matrix for the given view angles
    VectorCopy(r_newrefdef.vieworg, modelorg);
    VectorCopy(r_newrefdef.vieworg, r_origin);

    AngleVectors(r_newrefdef.viewangles, vpn, vright, vup);

// current viewleaf
    if (!(r_newrefdef.rdflags & RDF_NOWORLDMODEL)) {
        r_viewleaf = BSP_PointLeaf(r_worldmodel->nodes, r_origin);
        r_viewcluster = r_viewleaf->cluster;
    }

    if (sw_waterwarp->integer && (r_newrefdef.rdflags & RDF_UNDERWATER))
        r_dowarp = qtrue;
    else
        r_dowarp = qfalse;

    if (r_dowarp) {
        // warp into off screen buffer
        vrect.x = 0;
        vrect.y = 0;
        vrect.width = r_newrefdef.width < WARP_WIDTH ? r_newrefdef.width : WARP_WIDTH;
        vrect.height = r_newrefdef.height < WARP_HEIGHT ? r_newrefdef.height : WARP_HEIGHT;

        d_viewbuffer = r_warpbuffer;
        d_screenrowbytes = WARP_WIDTH * VID_BYTES;
    } else {
        vrect.x = r_newrefdef.x;
        vrect.y = r_newrefdef.y;
        vrect.width = r_newrefdef.width;
        vrect.height = r_newrefdef.height;

        d_viewbuffer = (void *)vid.buffer;
        d_screenrowbytes = vid.rowbytes;
    }

    R_ViewChanged(&vrect);

// start off with just the four screen edge clip planes
    R_TransformFrustum();
    R_SetUpFrustumIndexes();

// save base values
    VectorCopy(vpn, base_vpn);
    VectorCopy(vright, base_vright);
    VectorCopy(vup, base_vup);

// clear frame counts
    c_faceclip = 0;
    r_polycount = 0;
    r_drawnpolycount = 0;
    r_wholepolycount = 0;
    r_amodels_drawn = 0;
    r_outofsurfaces = 0;
    r_outofedges = 0;

// d_setup
    for (i = 0; i < vid.height; i++) {
        d_spantable[i] = d_viewbuffer + i * d_screenrowbytes;
        d_zspantable[i] = d_pzbuffer + i * d_zwidth;
    }

// clear Z-buffer and color-buffers if we're doing the gallery
    if (r_newrefdef.rdflags & RDF_NOWORLDMODEL) {
        memset(d_pzbuffer, 0xff, vid.width * vid.height * sizeof(d_pzbuffer[0]));
#if 0
        R_DrawFill8(r_newrefdef.x, r_newrefdef.y, r_newrefdef.width, r_newrefdef.height, /*(int)sw_clearcolor->value & 0xff*/0);
#endif
    }

    d_minmip = Cvar_ClampInteger(sw_mipcap, 0, NUM_MIPS - 1);

    for (i = 0; i < (NUM_MIPS - 1); i++)
        d_scalemip[i] = basemip[i] * sw_mipscale->value;
}

