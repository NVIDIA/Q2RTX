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
// r_bsp.c

#include "sw.h"

//
// current entity info
//
qboolean        insubmodel;
entity_t        *currententity;
vec3_t          modelorg;       // modelorg is the viewpoint reletive to
                                // the currently rendering entity
vec3_t          r_entorigin;    // the currently rendering entity in world
                                // coordinates

vec3_t          entity_rotation[3];

int             r_currentbkey;

#define MAX_BMODEL_VERTS    500         // 6K
#define MAX_BMODEL_EDGES    1000        // 12K

static mvertex_t    *pbverts;
static bedge_t      *pbedges;
static int          numbverts, numbedges;

static mvertex_t    *pfrontenter, *pfrontexit;

static qboolean     makeclippededge;


//===========================================================================

/*
================
R_EntityRotate
================
*/
static void R_EntityRotate(vec3_t vec)
{
    vec3_t  tvec;

    VectorCopy(vec, tvec);
    vec[0] = DotProduct(entity_rotation[0], tvec);
    vec[1] = DotProduct(entity_rotation[1], tvec);
    vec[2] = DotProduct(entity_rotation[2], tvec);
}


/*
================
R_RotateBmodel
================
*/
void R_RotateBmodel(void)
{
    AnglesToAxis(currententity->angles, entity_rotation);

//
// rotate modelorg and the transformation matrix
//
    R_EntityRotate(modelorg);
    R_EntityRotate(vpn);
    R_EntityRotate(vright);
    R_EntityRotate(vup);

    R_TransformFrustum();
}


/*
================
R_RecursiveClipBPoly

Clip a bmodel poly down the world bsp tree
================
*/
static void R_RecursiveClipBPoly(bedge_t *pedges, mnode_t *pnode, mface_t *psurf)
{
    bedge_t     *psideedges[2], *pnextedge, *ptedge;
    int         i, side, lastside;
    float       dist, frac, lastdist;
    cplane_t    *splitplane, tplane;
    mvertex_t   *pvert, *plastvert, *ptvert;
    mnode_t     *pn;
    int         area;

    psideedges[0] = psideedges[1] = NULL;

    makeclippededge = qfalse;

// transform the BSP plane into model space
// FIXME: cache these?
    splitplane = pnode->plane;
    tplane.dist = -PlaneDiff(r_entorigin, splitplane);
    tplane.normal[0] = DotProduct(entity_rotation[0], splitplane->normal);
    tplane.normal[1] = DotProduct(entity_rotation[1], splitplane->normal);
    tplane.normal[2] = DotProduct(entity_rotation[2], splitplane->normal);

// clip edges to BSP plane
    for (; pedges; pedges = pnextedge) {
        pnextedge = pedges->pnext;

        // set the status for the last point as the previous point
        // FIXME: cache this stuff somehow?
        plastvert = pedges->v[0];
        lastdist = PlaneDiff(plastvert->point, &tplane);

        if (lastdist > 0)
            lastside = 0;
        else
            lastside = 1;

        pvert = pedges->v[1];
        dist = PlaneDiff(pvert->point, &tplane);

        if (dist > 0)
            side = 0;
        else
            side = 1;

        if (side != lastside) {
            // clipped
            if (numbverts >= MAX_BMODEL_VERTS)
                return;

            // generate the clipped vertex
            frac = lastdist / (lastdist - dist);
            ptvert = &pbverts[numbverts++];
            LerpVector(plastvert->point, pvert->point, frac, ptvert->point);

            // split into two edges, one on each side, and remember entering
            // and exiting points
            // FIXME: share the clip edge by having a winding direction flag?
            if (numbedges >= (MAX_BMODEL_EDGES - 1)) {
                Com_Printf("Out of edges for bmodel\n");
                return;
            }

            ptedge = &pbedges[numbedges];
            ptedge->pnext = psideedges[lastside];
            psideedges[lastside] = ptedge;
            ptedge->v[0] = plastvert;
            ptedge->v[1] = ptvert;

            ptedge = &pbedges[numbedges + 1];
            ptedge->pnext = psideedges[side];
            psideedges[side] = ptedge;
            ptedge->v[0] = ptvert;
            ptedge->v[1] = pvert;

            numbedges += 2;

            if (side == 0) {
                // entering for front, exiting for back
                pfrontenter = ptvert;
                makeclippededge = qtrue;
            } else {
                pfrontexit = ptvert;
                makeclippededge = qtrue;
            }
        } else {
            // add the edge to the appropriate side
            pedges->pnext = psideedges[side];
            psideedges[side] = pedges;
        }
    }

// if anything was clipped, reconstitute and add the edges along the clip
// plane to both sides (but in opposite directions)
    if (makeclippededge) {
        if (numbedges >= (MAX_BMODEL_EDGES - 2)) {
            Com_Error(ERR_DROP, "Out of edges for bmodel");
        }

        ptedge = &pbedges[numbedges];
        ptedge->pnext = psideedges[0];
        psideedges[0] = ptedge;
        ptedge->v[0] = pfrontexit;
        ptedge->v[1] = pfrontenter;

        ptedge = &pbedges[numbedges + 1];
        ptedge->pnext = psideedges[1];
        psideedges[1] = ptedge;
        ptedge->v[0] = pfrontenter;
        ptedge->v[1] = pfrontexit;

        numbedges += 2;
    }

// draw or recurse further
    for (i = 0; i < 2; i++) {
        if (psideedges[i]) {
            // draw if we've reached a non-solid leaf, done if all that's left is a
            // solid leaf, and continue down the tree if it's not a leaf
            pn = pnode->children[i];

            // we're done with this branch if the node or leaf isn't in the PVS
            if (pn->visframe == r_visframecount) {
                if (!pn->plane) {
                    mleaf_t *pl = (mleaf_t *)pn;
                    if (pl->contents != CONTENTS_SOLID) {
                        if (r_newrefdef.areabits) {
                            area = pl->area;
                            if (!Q_IsBitSet(r_newrefdef.areabits, area))
                                continue;       // not visible
                        }

                        r_currentbkey = pl->key;
                        R_RenderBmodelFace(psideedges[i], psurf);
                    }
                } else {
                    R_RecursiveClipBPoly(psideedges[i], pnode->children[i],
                                         psurf);
                }
            }
        }
    }
}


/*
================
R_DrawSolidClippedSubmodelPolygons

Bmodel crosses multiple leafs
================
*/
void R_DrawSolidClippedSubmodelPolygons(mmodel_t *pmodel, mnode_t *topnode)
{
    int         i, j;
    vec_t       dot;
    mface_t     *psurf;
    int         numsurfaces;
    cplane_t    *pplane;
    mvertex_t   bverts[MAX_BMODEL_VERTS];
    bedge_t     bedges[MAX_BMODEL_EDGES], *pbedge;
    msurfedge_t *surfedge;

// FIXME: use bounding-box-based frustum clipping info?

    psurf = pmodel->firstface;
    numsurfaces = pmodel->numfaces;

    for (i = 0; i < numsurfaces; i++, psurf++) {
        // find which side of the node we are on
        pplane = psurf->plane;

        dot = PlaneDiff(modelorg, pplane);

        // draw the polygon
        if ((!(psurf->drawflags & DSURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
            ((psurf->drawflags & DSURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
            continue;

        // FIXME: use bounding-box-based frustum clipping info?

        // copy the edges to bedges, flipping if necessary so always
        // clockwise winding
        // FIXME: if edges and vertices get caches, these assignments must move
        // outside the loop, and overflow checking must be done here
        pbverts = bverts;
        pbedges = bedges;
        numbverts = numbedges = 0;
        pbedge = &bedges[numbedges];
        numbedges += psurf->numsurfedges;

        if (numbedges >= MAX_BMODEL_EDGES) {
            Com_Printf("Out of edges for bmodel\n");
            return;
        }

        surfedge = psurf->firstsurfedge;
        for (j = 0; j < psurf->numsurfedges; j++, surfedge++) {
            pbedge[j].v[0] = surfedge->edge->v[surfedge->vert    ];
            pbedge[j].v[1] = surfedge->edge->v[surfedge->vert ^ 1];
            pbedge[j].pnext = &pbedge[j + 1];
        }

        pbedge[j - 1].pnext = NULL; // mark end of edges

        if (!(psurf->texinfo->c.flags & SURF_TRANS_MASK))
            R_RecursiveClipBPoly(pbedge, topnode, psurf);
        else
            R_RenderBmodelFace(pbedge, psurf);
    }
}


/*
================
R_DrawSubmodelPolygons

All in one leaf
================
*/
void R_DrawSubmodelPolygons(mmodel_t *pmodel, int clipflags, mnode_t *topnode)
{
    int         i;
    vec_t       dot;
    mface_t *psurf;
    int         numsurfaces;
    cplane_t    *pplane;

// FIXME: use bounding-box-based frustum clipping info?

    psurf = pmodel->firstface;
    numsurfaces = pmodel->numfaces;

    for (i = 0; i < numsurfaces; i++, psurf++) {
        // find which side of the node we are on
        pplane = psurf->plane;

        dot = PlaneDiff(modelorg, pplane);

        // draw the polygon
        if (((psurf->drawflags & DSURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
            (!(psurf->drawflags & DSURF_PLANEBACK) && (dot > BACKFACE_EPSILON))) {
            r_currentkey = ((mleaf_t *)topnode)->key;

            // FIXME: use bounding-box-based frustum clipping info?
            R_RenderFace(psurf, clipflags);
        }
    }
}


int c_drawnode;

/*
================
R_RecursiveWorldNode
================
*/
static void R_RecursiveWorldNode(mnode_t *node, int clipflags)
{
    int         i, c, side, *pindex;
    vec3_t      acceptpt, rejectpt;
    cplane_t    *plane;
    mface_t     *surf, **mark;
    float       d, dot;
    mleaf_t     *pleaf;

    while (node->visframe == r_visframecount) {
        // cull the clipping planes if not trivial accept
        // FIXME: the compiler is doing a lousy job of optimizing here; it could be
        //  twice as fast in ASM
        if (clipflags) {
            for (i = 0; i < 4; i++) {
                if (!(clipflags & (1 << i)))
                    continue;   // don't need to clip against it

                // generate accept and reject points
                // FIXME: do with fast look-ups or integer tests based on the sign bit
                // of the floating point values

                pindex = pfrustum_indexes[i];

                rejectpt[0] = (float)node->minmaxs[pindex[0]];
                rejectpt[1] = (float)node->minmaxs[pindex[1]];
                rejectpt[2] = (float)node->minmaxs[pindex[2]];

                d = PlaneDiff(rejectpt, &view_clipplanes[i]);
                if (d <= 0)
                    return;

                acceptpt[0] = (float)node->minmaxs[pindex[3 + 0]];
                acceptpt[1] = (float)node->minmaxs[pindex[3 + 1]];
                acceptpt[2] = (float)node->minmaxs[pindex[3 + 2]];

                d = PlaneDiff(acceptpt, &view_clipplanes[i]);
                if (d >= 0)
                    clipflags &= ~(1 << i); // node is entirely on screen
            }
        }

        c_drawnode++;

        // if a leaf node, draw stuff
        if (!node->plane) {
            pleaf = (mleaf_t *)node;

            if (pleaf->contents == CONTENTS_SOLID)
                return;     // solid

            // check for door connected areas
            if (r_newrefdef.areabits) {
                if (! Q_IsBitSet(r_newrefdef.areabits, pleaf->area))
                    return;     // not visible
            }

            mark = pleaf->firstleafface;
            c = pleaf->numleaffaces;
            if (c) {
                do {
                    (*mark)->drawframe = r_framecount;
                    mark++;
                } while (--c);
            }

            pleaf->key = r_currentkey;
            r_currentkey++;     // all bmodels in a leaf share the same key
            return;
        }
        // node is just a decision point, so go down the apropriate sides

        // find which side of the node we are on
        plane = node->plane;

        dot = PlaneDiffFast(modelorg, plane);

        if (dot >= 0)
            side = 0;
        else
            side = 1;

        // recurse down the children, front side first
        R_RecursiveWorldNode(node->children[side], clipflags);

        // draw stuff
        c = node->numfaces;
        if (c) {
            surf = node->firstface;
            if (dot < -BACKFACE_EPSILON) {
                do {
                    if ((surf->drawflags & DSURF_PLANEBACK) &&
                        (surf->drawframe == r_framecount)) {
                        R_RenderFace(surf, clipflags);
                    }
                    surf++;
                } while (--c);
            } else if (dot > BACKFACE_EPSILON) {
                do {
                    if (!(surf->drawflags & DSURF_PLANEBACK) &&
                        (surf->drawframe == r_framecount)) {
                        R_RenderFace(surf, clipflags);
                    }
                    surf++;
                } while (--c);
            }

            // all surfaces on the same node share the same sequence number
            r_currentkey++;
        }

        // recurse down the back side
        node = node->children[side ^ 1];
    }
}



/*
================
R_RenderWorld
================
*/
void R_RenderWorld(void)
{

    if (!r_drawworld->integer)
        return;
    if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
        return;

    c_drawnode = 0;

    // auto cycle the world frame for texture animation
    r_worldentity.frame = (int)(r_newrefdef.time * 2);
    currententity = &r_worldentity;

    VectorCopy(r_origin, modelorg);

    R_RecursiveWorldNode(r_worldmodel->nodes, 15);
}


