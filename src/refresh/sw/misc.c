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

int d_vrectx, d_vrecty, d_vrectright_particle, d_vrectbottom_particle;

int d_pix_min, d_pix_max, d_pix_shift;

int     d_scantable[MAXHEIGHT];
short   *zspantable[MAXHEIGHT];

/*
================
D_Patch
================
*/
void D_Patch(void)
{
#if USE_ASM
    extern void D_Aff8Patch(void);
    static qboolean protectset8 = qfalse;
    extern void D_PolysetAff8Start(void);

    if (!protectset8) {
        Sys_MakeCodeWriteable((uintptr_t)D_PolysetAff8Start,
                              (uintptr_t)D_Aff8Patch - (uintptr_t)D_PolysetAff8Start);
        Sys_MakeCodeWriteable((uintptr_t)R_Surf8Start,
                              (uintptr_t)R_Surf8End - (uintptr_t)R_Surf8Start);
        protectset8 = qtrue;
    }

    R_Surf8Patch();
    D_Aff8Patch();
#endif
}
/*
================
D_ViewChanged
================
*/
void D_ViewChanged(void)
{
    int     i;

    scale_for_mip = xscale;
    if (yscale > xscale)
        scale_for_mip = yscale;

    d_zrowbytes = vid.width * 2;
    d_zwidth = vid.width;

    d_pix_min = r_refdef.vrect.width / 640;
    if (d_pix_min < 1)
        d_pix_min = 1;

    d_pix_max = (int)((float)r_refdef.vrect.width / (640.0 / 4.0) + 0.5);
    d_pix_shift = 8 - (int)((float)r_refdef.vrect.width / 640.0 + 0.5);
    if (d_pix_max < 1)
        d_pix_max = 1;

    d_vrectx = r_refdef.vrect.x;
    d_vrecty = r_refdef.vrect.y;
    d_vrectright_particle = r_refdef.vrectright - d_pix_max;
    d_vrectbottom_particle =
        r_refdef.vrectbottom - d_pix_max;

    for (i = 0; i < vid.height; i++) {
        d_scantable[i] = i * r_screenrowbytes;
        zspantable[i] = d_pzbuffer + i * d_zwidth;
    }

    /*
    ** clear Z-buffer and color-buffers if we're doing the gallery
    */
    if (r_newrefdef.rdflags & RDF_NOWORLDMODEL) {
        memset(d_pzbuffer, 0xff, vid.width * vid.height * sizeof(d_pzbuffer[0]));
        R_DrawFill8(r_newrefdef.x, r_newrefdef.y, r_newrefdef.width, r_newrefdef.height, /*(int)sw_clearcolor->value & 0xff*/0);
    }

    D_Patch();
}



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
R_PrintDSpeeds
=============
*/
void R_PrintDSpeeds(void)
{
    int ms, dp_time, r_time2, rw_time, db_time, se_time, de_time, da_time;

    r_time2 = Sys_Milliseconds();

    da_time = (da_time2 - da_time1);
    dp_time = (dp_time2 - dp_time1);
    rw_time = (rw_time2 - rw_time1);
    db_time = (db_time2 - db_time1);
    se_time = (se_time2 - se_time1);
    de_time = (de_time2 - de_time1);
    ms = (r_time2 - r_time1);

    Com_Printf("%3i %2ip %2iw %2ib %2is %2ie %2ia\n",
               ms, dp_time, rw_time, db_time, se_time, de_time, da_time);
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


/*
===============
R_SetUpFrustumIndexes
===============
*/
void R_SetUpFrustumIndexes(void)
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
void R_ViewChanged(vrectSoft_t *vr)
{
    int     i;

    r_refdef.vrect = *vr;

    r_refdef.horizontalFieldOfView = 2 * tan((float)r_newrefdef.fov_x / 360 * M_PI);
    verticalFieldOfView = 2 * tan((float)r_newrefdef.fov_y / 360 * M_PI);

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

    r_refdef.aliasvrect.x = (int)(r_refdef.vrect.x * r_aliasuvscale);
    r_refdef.aliasvrect.y = (int)(r_refdef.vrect.y * r_aliasuvscale);
    r_refdef.aliasvrect.width = (int)(r_refdef.vrect.width * r_aliasuvscale);
    r_refdef.aliasvrect.height = (int)(r_refdef.vrect.height * r_aliasuvscale);
    r_refdef.aliasvrectright = r_refdef.aliasvrect.x +
                               r_refdef.aliasvrect.width;
    r_refdef.aliasvrectbottom = r_refdef.aliasvrect.y +
                                r_refdef.aliasvrect.height;

    xOrigin = r_refdef.xOrigin;
    yOrigin = r_refdef.yOrigin;

// values for perspective projection
// if math were exact, the values would range from 0.5 to to range+0.5
// hopefully they wll be in the 0.000001 to range+.999999 and truncate
// the polygon rasterization will never render in the first row or column
// but will definately render in the [range] row and column, so adjust the
// buffer origin to get an exact edge to edge fill
    xcenter = ((float)r_refdef.vrect.width * XCENTERING) +
              r_refdef.vrect.x - 0.5;
    aliasxcenter = xcenter * r_aliasuvscale;
    ycenter = ((float)r_refdef.vrect.height * YCENTERING) +
              r_refdef.vrect.y - 0.5;
    aliasycenter = ycenter * r_aliasuvscale;

    xscale = r_refdef.vrect.width / r_refdef.horizontalFieldOfView;
    aliasxscale = xscale * r_aliasuvscale;
    xscaleinv = 1.0 / xscale;

    yscale = xscale;
    aliasyscale = yscale * r_aliasuvscale;
    yscaleinv = 1.0 / yscale;
    xscaleshrink = (r_refdef.vrect.width - 6) / r_refdef.horizontalFieldOfView;
    yscaleshrink = xscaleshrink;

// left side clip
    screenedge[0].normal[0] = -1.0 / (xOrigin * r_refdef.horizontalFieldOfView);
    screenedge[0].normal[1] = 0;
    screenedge[0].normal[2] = 1;
    screenedge[0].type = PLANE_ANYZ;

// right side clip
    screenedge[1].normal[0] =
        1.0 / ((1.0 - xOrigin) * r_refdef.horizontalFieldOfView);
    screenedge[1].normal[1] = 0;
    screenedge[1].normal[2] = 1;
    screenedge[1].type = PLANE_ANYZ;

// top side clip
    screenedge[2].normal[0] = 0;
    screenedge[2].normal[1] = -1.0 / (yOrigin * verticalFieldOfView);
    screenedge[2].normal[2] = 1;
    screenedge[2].type = PLANE_ANYZ;

// bottom side clip
    screenedge[3].normal[0] = 0;
    screenedge[3].normal[1] = 1.0 / ((1.0 - yOrigin) * verticalFieldOfView);
    screenedge[3].normal[2] = 1;
    screenedge[3].type = PLANE_ANYZ;

    for (i = 0; i < 4; i++)
        VectorNormalize(screenedge[i].normal);

    D_ViewChanged();
}


/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame(void)
{
    int         i;
    vrectSoft_t     vrect;

    if (r_fullbright->modified) {
        r_fullbright->modified = qfalse;
        D_FlushCaches();    // so all lighting changes
    }

    r_framecount++;


// build the transformation matrix for the given view angles
    VectorCopy(r_refdef.vieworg, modelorg);
    VectorCopy(r_refdef.vieworg, r_origin);

    AngleVectors(r_refdef.viewangles, vpn, vright, vup);

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
        r_screenrowbytes = WARP_WIDTH * VID_BYTES;
    } else {
        vrect.x = r_newrefdef.x;
        vrect.y = r_newrefdef.y;
        vrect.width = r_newrefdef.width;
        vrect.height = r_newrefdef.height;

        d_viewbuffer = (void *)vid.buffer;
        r_screenrowbytes = vid.rowbytes;
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
    d_spanpixcount = 0;
    r_polycount = 0;
    r_drawnpolycount = 0;
    r_wholepolycount = 0;
    r_amodels_drawn = 0;
    r_outofsurfaces = 0;
    r_outofedges = 0;

// d_setup
    d_minmip = Cvar_ClampInteger(sw_mipcap, 0, NUM_MIPS - 1);

    for (i = 0; i < (NUM_MIPS - 1); i++)
        d_scalemip[i] = basemip[i] * sw_mipscale->value;
}

/*
==============================================================================

                        MATH

==============================================================================
*/

/*
================
R_ConcatRotations
================
*/
void R_ConcatRotations(float in1[3][3], float in2[3][3], float out[3][3])
{
    out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] +
                in1[0][2] * in2[2][0];
    out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] +
                in1[0][2] * in2[2][1];
    out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] +
                in1[0][2] * in2[2][2];
    out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] +
                in1[1][2] * in2[2][0];
    out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] +
                in1[1][2] * in2[2][1];
    out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] +
                in1[1][2] * in2[2][2];
    out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] +
                in1[2][2] * in2[2][0];
    out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] +
                in1[2][2] * in2[2][1];
    out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] +
                in1[2][2] * in2[2][2];
}


/*
================
R_ConcatTransforms
================
*/
void R_ConcatTransforms(float in1[3][4], float in2[3][4], float out[3][4])
{
    out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] +
                in1[0][2] * in2[2][0];
    out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] +
                in1[0][2] * in2[2][1];
    out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] +
                in1[0][2] * in2[2][2];
    out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] +
                in1[0][2] * in2[2][3] + in1[0][3];
    out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] +
                in1[1][2] * in2[2][0];
    out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] +
                in1[1][2] * in2[2][1];
    out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] +
                in1[1][2] * in2[2][2];
    out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] +
                in1[1][2] * in2[2][3] + in1[1][3];
    out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] +
                in1[2][2] * in2[2][0];
    out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] +
                in1[2][2] * in2[2][1];
    out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] +
                in1[2][2] * in2[2][2];
    out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] +
                in1[2][2] * in2[2][3] + in1[2][3];
}

void R_RotatePointAroundVector(vec3_t dst, const vec3_t dir, const vec3_t point, float degrees)
{
    float   m[3][3];
    float   im[3][3];
    float   zrot[3][3];
    float   tmpmat[3][3];
    float   rot[3][3];
    int     i;
    vec3_t  vr, vup, vf;

    vf[0] = dir[0];
    vf[1] = dir[1];
    vf[2] = dir[2];

    PerpendicularVector(vr, dir);
    CrossProduct(vr, vf, vup);

    m[0][0] = vr[0];
    m[1][0] = vr[1];
    m[2][0] = vr[2];

    m[0][1] = vup[0];
    m[1][1] = vup[1];
    m[2][1] = vup[2];

    m[0][2] = vf[0];
    m[1][2] = vf[1];
    m[2][2] = vf[2];

    memcpy(im, m, sizeof(im));

    im[0][1] = m[1][0];
    im[0][2] = m[2][0];
    im[1][0] = m[0][1];
    im[1][2] = m[2][1];
    im[2][0] = m[0][2];
    im[2][1] = m[1][2];

    memset(zrot, 0, sizeof(zrot));
    zrot[0][0] = zrot[1][1] = zrot[2][2] = 1.0F;

    zrot[0][0] = cos(DEG2RAD(degrees));
    zrot[0][1] = sin(DEG2RAD(degrees));
    zrot[1][0] = -sin(DEG2RAD(degrees));
    zrot[1][1] = cos(DEG2RAD(degrees));

    R_ConcatRotations(m, zrot, tmpmat);
    R_ConcatRotations(tmpmat, im, rot);

    for (i = 0; i < 3; i++) {
        dst[i] = rot[i][0] * point[0] + rot[i][1] * point[1] + rot[i][2] * point[2];
    }
}

void ProjectPointOnPlane(vec3_t dst, const vec3_t p, const vec3_t normal)
{
    float d;
    vec3_t n;
    float inv_denom;

    inv_denom = 1.0F / DotProduct(normal, normal);

    d = DotProduct(normal, p) * inv_denom;

    n[0] = normal[0] * inv_denom;
    n[1] = normal[1] * inv_denom;
    n[2] = normal[2] * inv_denom;

    dst[0] = p[0] - d * n[0];
    dst[1] = p[1] - d * n[1];
    dst[2] = p[2] - d * n[2];
}

/*
** assumes "src" is normalized
*/
void PerpendicularVector(vec3_t dst, const vec3_t src)
{
    int pos;
    int i;
    float minelem = 1.0F;
    vec3_t tempvec;

    /*
    ** find the smallest magnitude axially aligned vector
    */
    for (pos = 0, i = 0; i < 3; i++) {
        if (fabs(src[i]) < minelem) {
            pos = i;
            minelem = fabs(src[i]);
        }
    }
    tempvec[0] = tempvec[1] = tempvec[2] = 0.0F;
    tempvec[pos] = 1.0F;

    /*
    ** project the point onto the plane defined by src
    */
    ProjectPointOnPlane(dst, tempvec, src);

    /*
    ** normalize the result
    */
    VectorNormalize(dst);
}

