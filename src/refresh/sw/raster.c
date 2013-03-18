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
// r_rast.c

#include "sw.h"

// !!! if these are changed, they must be changed in asm_draw.h too !!!
#if (defined __amd64__) || (defined _M_AMD64)
#define FULLY_CLIPPED_CACHED    0x8000000000000000ULL
#define CLIPPED_NOT_CACHED      0x7FFFFFFFFFFFFFFFULL
#define FRAMECOUNT_MASK         0x7FFFFFFFFFFFFFFFULL
#else
#define FULLY_CLIPPED_CACHED    0x80000000UL
#define CLIPPED_NOT_CACHED      0x7FFFFFFFUL
#define FRAMECOUNT_MASK         0x7FFFFFFFUL
#endif

static uintptr_t    cacheoffset;

int         c_faceclip;                 // number of faces clipped

clipplane_t view_clipplanes[4];

static medge_t      *r_pedge;

static qboolean     r_leftclipped, r_rightclipped;
static qboolean     r_nearzionly;

static mvertex_t    r_leftenter, r_leftexit;
static mvertex_t    r_rightenter, r_rightexit;

static int          r_emitted;
static float        r_nearzi;
static float        r_u1, r_v1, r_lzi1;
static int          r_ceilv1;

static qboolean     r_lastvertvalid;


/*
================
R_EmitEdge
================
*/
static void R_EmitEdge(mvertex_t *pv0, mvertex_t *pv1)
{
    edge_t  *edge, *pcheck;
    int     u_check;
    float   u, u_step;
    vec3_t  local, transformed;
    float   *world;
    int     v, v2, ceilv0;
    float   scale, lzi0, u0, v0;
    int     side;

    if (r_lastvertvalid) {
        u0 = r_u1;
        v0 = r_v1;
        lzi0 = r_lzi1;
        ceilv0 = r_ceilv1;
    } else {
        world = &pv0->point[0];

        // transform and project
        VectorSubtract(world, modelorg, local);
        R_TransformVector(local, transformed);

        if (transformed[2] < NEAR_CLIP)
            transformed[2] = NEAR_CLIP;

        lzi0 = 1.0 / transformed[2];

        // FIXME: build x/yscale into transform?
        scale = r_refdef.xscale * lzi0;
        u0 = (r_refdef.xcenter + scale * transformed[0]);
        if (u0 < r_refdef.fvrectx_adj)
            u0 = r_refdef.fvrectx_adj;
        if (u0 > r_refdef.fvrectright_adj)
            u0 = r_refdef.fvrectright_adj;

        scale = r_refdef.yscale * lzi0;
        v0 = (r_refdef.ycenter - scale * transformed[1]);
        if (v0 < r_refdef.fvrecty_adj)
            v0 = r_refdef.fvrecty_adj;
        if (v0 > r_refdef.fvrectbottom_adj)
            v0 = r_refdef.fvrectbottom_adj;

        ceilv0 = (int) ceil(v0);
    }

    world = &pv1->point[0];

// transform and project
    VectorSubtract(world, modelorg, local);
    R_TransformVector(local, transformed);

    if (transformed[2] < NEAR_CLIP)
        transformed[2] = NEAR_CLIP;

    r_lzi1 = 1.0 / transformed[2];

    scale = r_refdef.xscale * r_lzi1;
    r_u1 = (r_refdef.xcenter + scale * transformed[0]);
    if (r_u1 < r_refdef.fvrectx_adj)
        r_u1 = r_refdef.fvrectx_adj;
    if (r_u1 > r_refdef.fvrectright_adj)
        r_u1 = r_refdef.fvrectright_adj;

    scale = r_refdef.yscale * r_lzi1;
    r_v1 = (r_refdef.ycenter - scale * transformed[1]);
    if (r_v1 < r_refdef.fvrecty_adj)
        r_v1 = r_refdef.fvrecty_adj;
    if (r_v1 > r_refdef.fvrectbottom_adj)
        r_v1 = r_refdef.fvrectbottom_adj;

    if (r_lzi1 > lzi0)
        lzi0 = r_lzi1;

    if (lzi0 > r_nearzi)    // for mipmap finding
        r_nearzi = lzi0;

// for right edges, all we want is the effect on 1/z
    if (r_nearzionly)
        return;

    r_emitted = 1;

    r_ceilv1 = (int) ceil(r_v1);


// create the edge
    if (ceilv0 == r_ceilv1) {
        // we cache unclipped horizontal edges as fully clipped
        if (cacheoffset != CLIPPED_NOT_CACHED) {
            cacheoffset = FULLY_CLIPPED_CACHED |
                          (r_framecount & FRAMECOUNT_MASK);
        }

        return;     // horizontal edge
    }

    side = ceilv0 > r_ceilv1;

    edge = edge_p++;

    edge->owner = r_pedge;

    edge->nearzi = lzi0;

    if (side == 0) {
        // trailing edge (go from p1 to p2)
        v = ceilv0;
        v2 = r_ceilv1 - 1;

        edge->surfs[0] = surface_p - surfaces;
        edge->surfs[1] = 0;

        u_step = ((r_u1 - u0) / (r_v1 - v0));
        u = u0 + ((float)v - v0) * u_step;
    } else {
        // leading edge (go from p2 to p1)
        v2 = ceilv0 - 1;
        v = r_ceilv1;

        edge->surfs[0] = 0;
        edge->surfs[1] = surface_p - surfaces;

        u_step = ((u0 - r_u1) / (v0 - r_v1));
        u = r_u1 + ((float)v - r_v1) * u_step;
    }

    edge->u_step = u_step * 0x100000;
    edge->u = u * 0x100000 + 0xFFFFF;

// we need to do this to avoid stepping off the edges if a very nearly
// horizontal edge is less than epsilon above a scan, and numeric error causes
// it to incorrectly extend to the scan, and the extension of the line goes off
// the edge of the screen
// FIXME: is this actually needed?
    if (edge->u < r_refdef.vrect_x_adj_shift20)
        edge->u = r_refdef.vrect_x_adj_shift20;
    if (edge->u > r_refdef.vrectright_adj_shift20)
        edge->u = r_refdef.vrectright_adj_shift20;

//
// sort the edge in normally
//
    u_check = edge->u;
    if (edge->surfs[0])
        u_check++;  // sort trailers after leaders

    if (!newedges[v] || newedges[v]->u >= u_check) {
        edge->next = newedges[v];
        newedges[v] = edge;
    } else {
        pcheck = newedges[v];
        while (pcheck->next && pcheck->next->u < u_check)
            pcheck = pcheck->next;
        edge->next = pcheck->next;
        pcheck->next = edge;
    }

    edge->nextremove = removeedges[v2];
    removeedges[v2] = edge;
}


/*
================
R_ClipEdge
================
*/
static void R_ClipEdge(mvertex_t *pv0, mvertex_t *pv1, clipplane_t *clip)
{
    float       d0, d1, f;
    mvertex_t   clipvert;

    for (; clip; clip = clip->next) {
        d0 = DotProduct(pv0->point, clip->normal) - clip->dist;
        d1 = DotProduct(pv1->point, clip->normal) - clip->dist;

        if (d0 >= 0) {
            // point 0 is unclipped
            if (d1 >= 0) {
                // both points are unclipped
                continue;
            }

            // only point 1 is clipped

            // we don't cache clipped edges
            cacheoffset = CLIPPED_NOT_CACHED;

            f = d0 / (d0 - d1);
            LerpVector(pv0->point, pv1->point, f, clipvert.point);

            if (clip->leftedge) {
                r_leftclipped = qtrue;
                r_leftexit = clipvert;
            } else if (clip->rightedge) {
                r_rightclipped = qtrue;
                r_rightexit = clipvert;
            }

            R_ClipEdge(pv0, &clipvert, clip->next);
            return;
        } else {
            // point 0 is clipped
            if (d1 < 0) {
                // both points are clipped
                // we do cache fully clipped edges
                if (!r_leftclipped)
                    cacheoffset = FULLY_CLIPPED_CACHED |
                                  (r_framecount & FRAMECOUNT_MASK);
                return;
            }

            // only point 0 is clipped
            r_lastvertvalid = qfalse;

            // we don't cache partially clipped edges
            cacheoffset = CLIPPED_NOT_CACHED;

            f = d0 / (d0 - d1);
            LerpVector(pv0->point, pv1->point, f, clipvert.point);

            if (clip->leftedge) {
                r_leftclipped = qtrue;
                r_leftenter = clipvert;
            } else if (clip->rightedge) {
                r_rightclipped = qtrue;
                r_rightenter = clipvert;
            }

            R_ClipEdge(&clipvert, pv1, clip->next);
            return;
        }
    }

// add the edge
    R_EmitEdge(pv0, pv1);
}


/*
================
R_EmitCachedEdge
================
*/
static void R_EmitCachedEdge(void)
{
    edge_t      *pedge_t;

    pedge_t = (edge_t *)((byte *)r_edges + r_pedge->cachededgeoffset);

    if (!pedge_t->surfs[0])
        pedge_t->surfs[0] = surface_p - surfaces;
    else
        pedge_t->surfs[1] = surface_p - surfaces;

    if (pedge_t->nearzi > r_nearzi) // for mipmap finding
        r_nearzi = pedge_t->nearzi;

    r_emitted = 1;
}


/*
================
R_RenderFace
================
*/
void R_RenderFace(mface_t *fa, int clipflags)
{
    int         i;
    unsigned    mask;
    cplane_t    *pplane;
    float       distinv;
    vec3_t      p_normal;
    medge_t     tedge;
    msurfedge_t *surfedge;
    clipplane_t *pclip;
    qboolean    makeleftedge, makerightedge;

    // translucent surfaces are not drawn by the edge renderer
    if (fa->texinfo->c.flags & SURF_TRANS_MASK) {
        fa->next = r_alpha_surfaces;
        r_alpha_surfaces = fa;
        return;
    }

    // sky surfaces encountered in the world will cause the
    // environment box surfaces to be emited
    if (fa->texinfo->c.flags & SURF_SKY) {
        R_EmitSkyBox();
        return;
    }

// skip out if no more surfs
    if ((surface_p) >= surf_max) {
        r_outofsurfaces++;
        return;
    }

// ditto if not enough edges left, or switch to auxedges if possible
    if ((edge_p + fa->numsurfedges + 4) >= edge_max) {
        r_outofedges += fa->numsurfedges;
        return;
    }

    c_faceclip++;

// set up clip planes
    pclip = NULL;

    for (i = 3, mask = 0x08; i >= 0; i--, mask >>= 1) {
        if (clipflags & mask) {
            view_clipplanes[i].next = pclip;
            pclip = &view_clipplanes[i];
        }
    }

// push the edges through
    r_emitted = 0;
    r_nearzi = 0;
    r_nearzionly = qfalse;
    makeleftedge = makerightedge = qfalse;
    r_lastvertvalid = qfalse;

    surfedge = fa->firstsurfedge;
    for (i = 0; i < fa->numsurfedges; i++, surfedge++) {
        r_pedge = surfedge->edge;

        // if the edge is cached, we can just reuse the edge
        if (!insubmodel) {
            if (r_pedge->cachededgeoffset & FULLY_CLIPPED_CACHED) {
                if ((r_pedge->cachededgeoffset & FRAMECOUNT_MASK) ==
                    r_framecount) {
                    r_lastvertvalid = qfalse;
                    continue;
                }
            } else {
                if ((((byte *)edge_p - (byte *)r_edges) >
                     r_pedge->cachededgeoffset) &&
                    (((edge_t *)((byte *)r_edges +
                                 r_pedge->cachededgeoffset))->owner == r_pedge)) {
                    R_EmitCachedEdge();
                    r_lastvertvalid = qfalse;
                    continue;
                }
            }
        }

        // assume it's cacheable
        cacheoffset = (byte *)edge_p - (byte *)r_edges;
        r_leftclipped = r_rightclipped = qfalse;
        R_ClipEdge(r_pedge->v[surfedge->vert    ],
                   r_pedge->v[surfedge->vert ^ 1],
                   pclip);
        r_pedge->cachededgeoffset = cacheoffset;

        if (r_leftclipped)
            makeleftedge = qtrue;
        if (r_rightclipped)
            makerightedge = qtrue;
        r_lastvertvalid = qtrue;
    }

// if there was a clip off the left edge, add that edge too
// FIXME: faster to do in screen space?
// FIXME: share clipped edges?
    if (makeleftedge) {
        r_pedge = &tedge;
        r_lastvertvalid = qfalse;
        R_ClipEdge(&r_leftexit, &r_leftenter, pclip->next);
    }

// if there was a clip off the right edge, get the right r_nearzi
    if (makerightedge) {
        r_pedge = &tedge;
        r_lastvertvalid = qfalse;
        r_nearzionly = qtrue;
        R_ClipEdge(&r_rightexit, &r_rightenter, view_clipplanes[1].next);
    }

// if no edges made it out, return without posting the surface
    if (!r_emitted)
        return;

    r_polycount++;

    surface_p->msurf = fa;
    surface_p->nearzi = r_nearzi;
    surface_p->flags = fa->drawflags;
    surface_p->insubmodel = insubmodel;
    surface_p->spanstate = 0;
    surface_p->entity = currententity;
    surface_p->key = r_currentkey++;
    surface_p->spans = NULL;

    pplane = fa->plane;
// FIXME: cache this?
    R_TransformVector(pplane->normal, p_normal);
// FIXME: cache this?
    distinv = 1.0 / (pplane->dist - DotProduct(modelorg, pplane->normal));

    surface_p->d_zistepu = p_normal[0] * r_refdef.xscaleinv * distinv;
    surface_p->d_zistepv = -p_normal[1] * r_refdef.yscaleinv * distinv;
    surface_p->d_ziorigin = p_normal[2] * distinv -
                            r_refdef.xcenter * surface_p->d_zistepu -
                            r_refdef.ycenter * surface_p->d_zistepv;

    surface_p++;
}


/*
================
R_RenderBmodelFace
================
*/
void R_RenderBmodelFace(bedge_t *pedges, mface_t *psurf)
{
    int         i;
    unsigned    mask;
    cplane_t    *pplane;
    float       distinv;
    vec3_t      p_normal;
    medge_t     tedge;
    clipplane_t *pclip;
    qboolean    makeleftedge, makerightedge;

    if (psurf->texinfo->c.flags & SURF_TRANS_MASK) {
        psurf->next = r_alpha_surfaces;
        r_alpha_surfaces = psurf;
        return;
    }

// skip out if no more surfs
    if (surface_p >= surf_max) {
        r_outofsurfaces++;
        return;
    }

// ditto if not enough edges left, or switch to auxedges if possible
    if ((edge_p + psurf->numsurfedges + 4) >= edge_max) {
        r_outofedges += psurf->numsurfedges;
        return;
    }

    c_faceclip++;

// this is a dummy to give the caching mechanism someplace to write to
    r_pedge = &tedge;

// set up clip planes
    pclip = NULL;

    for (i = 3, mask = 0x08; i >= 0; i--, mask >>= 1) {
        if (r_clipflags & mask) {
            view_clipplanes[i].next = pclip;
            pclip = &view_clipplanes[i];
        }
    }

// push the edges through
    r_emitted = 0;
    r_nearzi = 0;
    r_nearzionly = qfalse;
    makeleftedge = makerightedge = qfalse;
// FIXME: keep clipped bmodel edges in clockwise order so last vertex caching
// can be used?
    r_lastvertvalid = qfalse;

    for (; pedges; pedges = pedges->pnext) {
        r_leftclipped = r_rightclipped = qfalse;
        R_ClipEdge(pedges->v[0], pedges->v[1], pclip);

        if (r_leftclipped)
            makeleftedge = qtrue;
        if (r_rightclipped)
            makerightedge = qtrue;
    }

// if there was a clip off the left edge, add that edge too
// FIXME: faster to do in screen space?
// FIXME: share clipped edges?
    if (makeleftedge) {
        r_pedge = &tedge;
        R_ClipEdge(&r_leftexit, &r_leftenter, pclip->next);
    }

// if there was a clip off the right edge, get the right r_nearzi
    if (makerightedge) {
        r_pedge = &tedge;
        r_nearzionly = qtrue;
        R_ClipEdge(&r_rightexit, &r_rightenter, view_clipplanes[1].next);
    }

// if no edges made it out, return without posting the surface
    if (!r_emitted)
        return;

    r_polycount++;

    surface_p->msurf = psurf;
    surface_p->nearzi = r_nearzi;
    surface_p->flags = psurf->drawflags;
    surface_p->insubmodel = qtrue;
    surface_p->spanstate = 0;
    surface_p->entity = currententity;
    surface_p->key = r_currentbkey;
    surface_p->spans = NULL;

    pplane = psurf->plane;
// FIXME: cache this?
    R_TransformVector(pplane->normal, p_normal);
// FIXME: cache this?
    distinv = 1.0 / (pplane->dist - DotProduct(modelorg, pplane->normal));

    surface_p->d_zistepu = p_normal[0] * r_refdef.xscaleinv * distinv;
    surface_p->d_zistepv = -p_normal[1] * r_refdef.yscaleinv * distinv;
    surface_p->d_ziorigin = p_normal[2] * distinv -
                            r_refdef.xcenter * surface_p->d_zistepu -
                            r_refdef.ycenter * surface_p->d_zistepv;

    surface_p++;
}

