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
// r_alias.c: routines for setting up to draw alias models

/*
** use a real variable to control lerping
*/
#include "sw.h"

int             r_amodels_drawn;

affinetridesc_t r_affinetridesc;

vec3_t          r_plightvec;
vec3_t          r_lerped[1024];
vec3_t          r_lerp_frontv, r_lerp_backv, r_lerp_move;

int             r_ambientlight;
fixed8_t        r_aliasblendcolor[3];
float           r_shadelight;

int             r_alias_alpha;
int             r_alias_one_minus_alpha;

maliasframe_t   *r_thisframe, *r_lastframe;

float   aliastransform[3][4];
float   aliasworldtransform[3][4];
float   aliasoldworldtransform[3][4];

static float    s_ziscale;
static vec3_t   s_alias_forward, s_alias_right, s_alias_up;

#define BBOX_TRIVIAL_ACCEPT 0
#define BBOX_MUST_CLIP_XY   1
#define BBOX_MUST_CLIP_Z    2
#define BBOX_TRIVIAL_REJECT 8

/*
================
R_AliasTransformVector
================
*/
void R_AliasTransformVector(vec3_t in, vec3_t out, float xf[3][4])
{
    out[0] = DotProduct(in, xf[0]) + xf[0][3];
    out[1] = DotProduct(in, xf[1]) + xf[1][3];
    out[2] = DotProduct(in, xf[2]) + xf[2][3];
}


/*
** R_AliasCheckFrameBBox
**
** Checks a specific alias frame bounding box
*/
unsigned long R_AliasCheckFrameBBox(maliasframe_t *frame, float worldxf[3][4])
{
    unsigned long aggregate_and_clipcode = ~0U,
                  aggregate_or_clipcode = 0;
    int           i;
    vec3_t        mins, maxs;
    vec3_t        transformed_min, transformed_max;
    qboolean      zclipped = qfalse, zfullyclipped = qtrue;

    /*
    ** get the exact frame bounding box
    */
    for (i = 0; i < 3; i++) {
        mins[i] = frame->translate[i];
        maxs[i] = mins[i] + frame->scale[i] * 255;
    }

    /*
    ** transform the min and max values into view space
    */
    R_AliasTransformVector(mins, transformed_min, aliastransform);
    R_AliasTransformVector(maxs, transformed_max, aliastransform);

    if (transformed_min[2] >= ALIAS_Z_CLIP_PLANE)
        zfullyclipped = qfalse;
    if (transformed_max[2] >= ALIAS_Z_CLIP_PLANE)
        zfullyclipped = qfalse;

    if (zfullyclipped) {
        return BBOX_TRIVIAL_REJECT;
    }
    if (zclipped) {
        return (BBOX_MUST_CLIP_XY | BBOX_MUST_CLIP_Z);
    }

    /*
    ** build a transformed bounding box from the given min and max
    */
    for (i = 0; i < 8; i++) {
        int      j;
        vec3_t   tmp, transformed;
        unsigned long clipcode = 0;

        if (i & 1)
            tmp[0] = mins[0];
        else
            tmp[0] = maxs[0];

        if (i & 2)
            tmp[1] = mins[1];
        else
            tmp[1] = maxs[1];

        if (i & 4)
            tmp[2] = mins[2];
        else
            tmp[2] = maxs[2];

        R_AliasTransformVector(tmp, transformed, worldxf);

        for (j = 0; j < 4; j++) {
            float dp = DotProduct(transformed, view_clipplanes[j].normal);

            if ((dp - view_clipplanes[j].dist) < 0.0F)
                clipcode |= 1 << j;
        }

        aggregate_and_clipcode &= clipcode;
        aggregate_or_clipcode  |= clipcode;
    }

    if (aggregate_and_clipcode) {
        return BBOX_TRIVIAL_REJECT;
    }
    if (!aggregate_or_clipcode) {
        return BBOX_TRIVIAL_ACCEPT;
    }

    return BBOX_MUST_CLIP_XY;
}

/*
================
R_AliasCheckBBox
================
*/
qboolean R_AliasCheckBBox(void)
{
    unsigned long ccodes[2] = { 0, 0 };

    ccodes[0] = R_AliasCheckFrameBBox(r_thisframe, aliasworldtransform);

    /*
    ** non-lerping model
    */
    if (currententity->backlerp == 0) {
        if (ccodes[0] == BBOX_TRIVIAL_ACCEPT)
            return BBOX_TRIVIAL_ACCEPT;
        else if (ccodes[0] & BBOX_TRIVIAL_REJECT)
            return BBOX_TRIVIAL_REJECT;
        else
            return (ccodes[0] & ~BBOX_TRIVIAL_REJECT);
    }

    ccodes[1] = R_AliasCheckFrameBBox(r_lastframe, aliasoldworldtransform);

    if ((ccodes[0] | ccodes[1]) == BBOX_TRIVIAL_ACCEPT)
        return BBOX_TRIVIAL_ACCEPT;
    else if ((ccodes[0] & ccodes[1]) & BBOX_TRIVIAL_REJECT)
        return BBOX_TRIVIAL_REJECT;
    else
        return (ccodes[0] | ccodes[1]) & ~BBOX_TRIVIAL_REJECT;
}


/*
================
R_AliasProjectAndClipTestFinalVert
================
*/
void R_AliasProjectAndClipTestFinalVert(finalvert_t *fv)
{
    float   zi;
    float   x, y, z;

    // project points
    x = fv->xyz[0];
    y = fv->xyz[1];
    z = fv->xyz[2];
    zi = 1.0 / z;

    fv->zi = zi * s_ziscale;

    fv->u = (x * aliasxscale * zi) + aliasxcenter;
    fv->v = (y * aliasyscale * zi) + aliasycenter;

    if (fv->u < r_refdef.aliasvrect.x)
        fv->flags |= ALIAS_LEFT_CLIP;
    if (fv->v < r_refdef.aliasvrect.y)
        fv->flags |= ALIAS_TOP_CLIP;
    if (fv->u > r_refdef.aliasvrectright)
        fv->flags |= ALIAS_RIGHT_CLIP;
    if (fv->v > r_refdef.aliasvrectbottom)
        fv->flags |= ALIAS_BOTTOM_CLIP;
}

/*
================
R_AliasTransformFinalVerts
================
*/
static void R_AliasTransformFinalVerts(int numpoints, finalvert_t *fv, maliasvert_t *oldv, maliasvert_t *newv)
{
    int i;

    for (i = 0; i < numpoints; i++, fv++, oldv++, newv++) {
        int         temp;
        float       lightcos;
        const vec_t *plightnormal;
        vec3_t      lerped_vert;

        lerped_vert[0] = r_lerp_move[0] + oldv->v[0] * r_lerp_backv[0] + newv->v[0] * r_lerp_frontv[0];
        lerped_vert[1] = r_lerp_move[1] + oldv->v[1] * r_lerp_backv[1] + newv->v[1] * r_lerp_frontv[1];
        lerped_vert[2] = r_lerp_move[2] + oldv->v[2] * r_lerp_backv[2] + newv->v[2] * r_lerp_frontv[2];

        plightnormal = bytedirs[newv->lightnormalindex];

        // PMM - added double damage shell
        if (currententity->flags & (RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM)) {
            lerped_vert[0] += plightnormal[0] * POWERSUIT_SCALE;
            lerped_vert[1] += plightnormal[1] * POWERSUIT_SCALE;
            lerped_vert[2] += plightnormal[2] * POWERSUIT_SCALE;
        }

        fv->xyz[0] = DotProduct(lerped_vert, aliastransform[0]) + aliastransform[0][3];
        fv->xyz[1] = DotProduct(lerped_vert, aliastransform[1]) + aliastransform[1][3];
        fv->xyz[2] = DotProduct(lerped_vert, aliastransform[2]) + aliastransform[2][3];

        fv->flags = 0;

        // lighting
        lightcos = DotProduct(plightnormal, r_plightvec);
        if (lightcos < 0)
            lightcos *= 0.3f;

        temp = r_ambientlight;
        temp += (int)(r_shadelight * (lightcos + 1));

        // clamp; because we limited the minimum ambient and shading light, we
        // don't have to clamp low light, just bright
        if (temp > 0xffff)
            temp = 0xffff;

        fv->l = temp;

        if (fv->xyz[2] < ALIAS_Z_CLIP_PLANE) {
            fv->flags |= ALIAS_Z_CLIP;
        } else {
            R_AliasProjectAndClipTestFinalVert(fv);
        }
    }
}


/*
================
R_AliasPreparePoints

General clipped case
================
*/
static void R_AliasPreparePoints(void)
{
    int         i;
    maliasst_t  *pstverts;
    maliastri_t *ptri;
    finalvert_t *pfv[3];
    finalvert_t finalverts[MAXALIASVERTS +
                           ((CACHE_SIZE - 1) / sizeof(finalvert_t)) + 3];
    finalvert_t *pfinalverts;

    // put work vertexes on stack, cache aligned
    pfinalverts = (finalvert_t *)
                  (((uintptr_t)&finalverts[0] + CACHE_SIZE - 1) & ~(CACHE_SIZE - 1));

    R_AliasTransformFinalVerts(currentmodel->numverts, pfinalverts,
                               r_lastframe->verts, r_thisframe->verts);

// clip and draw all triangles
//
    pstverts = currentmodel->sts;
    ptri = currentmodel->tris;

    if ((currententity->flags & (RF_WEAPONMODEL | RF_LEFTHAND)) == (RF_WEAPONMODEL | RF_LEFTHAND)) {
        for (i = 0; i < currentmodel->numtris; i++, ptri++) {
            pfv[0] = &pfinalverts[ptri->index_xyz[0]];
            pfv[1] = &pfinalverts[ptri->index_xyz[1]];
            pfv[2] = &pfinalverts[ptri->index_xyz[2]];

            if (pfv[0]->flags & pfv[1]->flags & pfv[2]->flags)
                continue;       // completely clipped

            // insert s/t coordinates
            pfv[0]->s = pstverts[ptri->index_st[0]].s << 16;
            pfv[0]->t = pstverts[ptri->index_st[0]].t << 16;

            pfv[1]->s = pstverts[ptri->index_st[1]].s << 16;
            pfv[1]->t = pstverts[ptri->index_st[1]].t << 16;

            pfv[2]->s = pstverts[ptri->index_st[2]].s << 16;
            pfv[2]->t = pstverts[ptri->index_st[2]].t << 16;

            if (!(pfv[0]->flags | pfv[1]->flags | pfv[2]->flags)) {
                // totally unclipped
                aliastriangleparms.a = pfv[2];
                aliastriangleparms.b = pfv[1];
                aliastriangleparms.c = pfv[0];

                R_DrawTriangle();
            } else {
                R_AliasClipTriangle(pfv[2], pfv[1], pfv[0]);
            }
        }
    } else {
        for (i = 0; i < currentmodel->numtris; i++, ptri++) {
            pfv[0] = &pfinalverts[ptri->index_xyz[0]];
            pfv[1] = &pfinalverts[ptri->index_xyz[1]];
            pfv[2] = &pfinalverts[ptri->index_xyz[2]];

            if (pfv[0]->flags & pfv[1]->flags & pfv[2]->flags)
                continue;       // completely clipped

            // insert s/t coordinates
            pfv[0]->s = pstverts[ptri->index_st[0]].s << 16;
            pfv[0]->t = pstverts[ptri->index_st[0]].t << 16;

            pfv[1]->s = pstverts[ptri->index_st[1]].s << 16;
            pfv[1]->t = pstverts[ptri->index_st[1]].t << 16;

            pfv[2]->s = pstverts[ptri->index_st[2]].s << 16;
            pfv[2]->t = pstverts[ptri->index_st[2]].t << 16;

            if (!(pfv[0]->flags | pfv[1]->flags | pfv[2]->flags)) {
                // totally unclipped
                aliastriangleparms.a = pfv[0];
                aliastriangleparms.b = pfv[1];
                aliastriangleparms.c = pfv[2];

                R_DrawTriangle();
            } else {
                // partially clipped
                R_AliasClipTriangle(pfv[0], pfv[1], pfv[2]);
            }
        }
    }
}


/*
================
R_AliasSetUpTransform
================
*/
static void R_AliasSetUpTransform(void)
{
    int             i;
    float           viewmatrix[3][4];
    vec3_t          angles;

// TODO: should really be stored with the entity instead of being reconstructed
// TODO: should use a look-up table
// TODO: could cache lazily, stored in the entity
//
    angles[ROLL] = currententity->angles[ROLL];
    angles[PITCH] = currententity->angles[PITCH];
    angles[YAW] = currententity->angles[YAW];
    AngleVectors(angles, s_alias_forward, s_alias_right, s_alias_up);

// TODO: can do this with simple matrix rearrangement

    memset(aliasworldtransform, 0, sizeof(aliasworldtransform));
    memset(aliasoldworldtransform, 0, sizeof(aliasoldworldtransform));

    for (i = 0; i < 3; i++) {
        aliasoldworldtransform[i][0] = aliasworldtransform[i][0] =  s_alias_forward[i];
        aliasoldworldtransform[i][1] = aliasworldtransform[i][1] = -s_alias_right[i];
        aliasoldworldtransform[i][2] = aliasworldtransform[i][2] =  s_alias_up[i];
    }

    aliasworldtransform[0][3] = currententity->origin[0] - r_origin[0];
    aliasworldtransform[1][3] = currententity->origin[1] - r_origin[1];
    aliasworldtransform[2][3] = currententity->origin[2] - r_origin[2];

    aliasoldworldtransform[0][3] = currententity->oldorigin[0] - r_origin[0];
    aliasoldworldtransform[1][3] = currententity->oldorigin[1] - r_origin[1];
    aliasoldworldtransform[2][3] = currententity->oldorigin[2] - r_origin[2];

// FIXME: can do more efficiently than full concatenation
//  memcpy(rotationmatrix, t2matrix, sizeof(rotationmatrix));

//  R_ConcatTransforms(t2matrix, tmatrix, rotationmatrix);

// TODO: should be global, set when vright, etc., set
    VectorCopy(vright, viewmatrix[0]);
    VectorCopy(vup, viewmatrix[1]);
    VectorNegate(viewmatrix[1], viewmatrix[1]);
    VectorCopy(vpn, viewmatrix[2]);

    viewmatrix[0][3] = 0;
    viewmatrix[1][3] = 0;
    viewmatrix[2][3] = 0;

//  memcpy(aliasworldtransform, rotationmatrix, sizeof(aliastransform));

    R_ConcatTransforms(viewmatrix, aliasworldtransform, aliastransform);

    aliasworldtransform[0][3] = currententity->origin[0];
    aliasworldtransform[1][3] = currententity->origin[1];
    aliasworldtransform[2][3] = currententity->origin[2];

    aliasoldworldtransform[0][3] = currententity->oldorigin[0];
    aliasoldworldtransform[1][3] = currententity->oldorigin[1];
    aliasoldworldtransform[2][3] = currententity->oldorigin[2];
}

/*
===============
R_AliasSetupSkin
===============
*/
static qboolean R_AliasSetupSkin(void)
{
    int             skinnum;
    image_t         *pskindesc;

    if (currententity->skin)
        pskindesc = IMG_ForHandle(currententity->skin);
    else {
        skinnum = currententity->skinnum;
        if ((skinnum >= currentmodel->numskins) || (skinnum < 0)) {
            Com_DPrintf("R_AliasSetupSkin %s: no such skin # %d\n",
                        currentmodel->name, skinnum);
            skinnum = 0;
        }

        pskindesc = currentmodel->skins[skinnum];
    }

    if (!pskindesc)
        return qfalse;

    r_affinetridesc.pskin = pskindesc->pixels[0];
    r_affinetridesc.skinwidth = pskindesc->width * TEX_BYTES;
    r_affinetridesc.skinheight = pskindesc->height;

    R_PolysetUpdateTables();        // FIXME: precalc edge lookups

    return qtrue;
}


/*
================
R_AliasSetupLighting

  FIXME: put lighting into tables
================
*/
static void R_AliasSetupLighting(void)
{
    float           lightvec[3] = { -1, 0, 0};
    vec3_t          light;
    int             i, j;

    // all components of light should be identical in software
    if (currententity->flags & RF_FULLBRIGHT) {
        for (i = 0; i < 3; i++)
            light[i] = 1.0;
    } else {
        R_LightPoint(currententity->origin, light);
    }


    if (currententity->flags & RF_MINLIGHT) {
        for (i = 0; i < 3; i++)
            if (light[i] < 0.1)
                light[i] = 0.1;
    }

    if (currententity->flags & RF_GLOW) {
        // bonus items will pulse with time
        float   scale;
        float   min;

        scale = 0.1 * sin(r_newrefdef.time * 7);
        for (i = 0; i < 3; i++) {
            min = light[i] * 0.8;
            light[i] += scale;
            if (light[i] < min)
                light[i] = min;
        }
    }

    j = LUMINANCE(light[0], light[1], light[2]) * 256;

    r_ambientlight = 0;
    r_shadelight = j << 8;

// rotate the lighting vector into the model's frame of reference
    r_plightvec[0] =  DotProduct(lightvec, s_alias_forward);
    r_plightvec[1] = -DotProduct(lightvec, s_alias_right);
    r_plightvec[2] =  DotProduct(lightvec, s_alias_up);
}


/*
=================
R_AliasSetupFrames

=================
*/
static void R_AliasSetupFrames(void)
{
    int thisframe = currententity->frame;
    int lastframe = currententity->oldframe;

    if (thisframe >= currentmodel->numframes || thisframe < 0) {
        Com_DPrintf("%s: %s: no such thisframe %d\n",
                    __func__, currentmodel->name, thisframe);
        thisframe = 0;
    }
    if (lastframe >= currentmodel->numframes || lastframe < 0) {
        Com_DPrintf("%s: %s: no such lastframe %d\n",
                    __func__, currentmodel->name, lastframe);
        lastframe = 0;
    }

    r_thisframe = &currentmodel->frames[thisframe];
    r_lastframe = &currentmodel->frames[lastframe];
}

/*
** R_AliasSetUpLerpData
**
** Precomputes lerp coefficients used for the whole frame.
*/
static void R_AliasSetUpLerpData(float backlerp)
{
    float   frontlerp;
    vec3_t  translation, vectors[3];
    int     i;

    frontlerp = 1.0F - backlerp;

    /*
    ** convert entity's angles into discrete vectors for R, U, and F
    */
    AngleVectors(currententity->angles, vectors[0], vectors[1], vectors[2]);

    /*
    ** translation is the vector from last position to this position
    */
    VectorSubtract(currententity->oldorigin, currententity->origin, translation);

    /*
    ** move should be the delta back to the previous frame * backlerp
    */
    r_lerp_move[0] =  DotProduct(translation, vectors[0]);  // forward
    r_lerp_move[1] = -DotProduct(translation, vectors[1]);  // left
    r_lerp_move[2] =  DotProduct(translation, vectors[2]);  // up

    VectorAdd(r_lerp_move, r_lastframe->translate, r_lerp_move);

    for (i = 0; i < 3; i++) {
        r_lerp_move[i] = backlerp * r_lerp_move[i] + frontlerp * r_thisframe->translate[i];
    }

    for (i = 0; i < 3; i++) {
        r_lerp_frontv[i] = frontlerp * r_thisframe->scale[i];
        r_lerp_backv[i]  = backlerp  * r_lastframe->scale[i];
    }
}

static void R_AliasSetupBlend(void)
{
    extern void (*d_pdrawspans)(void *);
    extern void R_PolysetDrawSpansConstant8_Blended(void *);
    extern void R_PolysetDrawSpans8_Blended(void *);
    extern void R_PolysetDrawSpans8_Opaque(void *);

    int         mask;
    color_t     color;

    r_alias_alpha = 255 * currententity->alpha;
    r_alias_one_minus_alpha = 255 - r_alias_alpha;

    /*
    ** select the proper span routine based on translucency
    */
    mask = currententity->flags & (RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM);
    if (mask) {
        if (mask == RF_SHELL_RED)
            color.u32 = d_8to24table[SHELL_RED_COLOR];
        else if (mask == RF_SHELL_GREEN)
            color.u32 = d_8to24table[SHELL_GREEN_COLOR];
        else if (mask == RF_SHELL_BLUE)
            color.u32 = d_8to24table[SHELL_BLUE_COLOR];
        else if (mask == (RF_SHELL_RED | RF_SHELL_GREEN))
            color.u32 = d_8to24table[SHELL_RG_COLOR];
        else if (mask == (RF_SHELL_RED | RF_SHELL_BLUE))
            color.u32 = d_8to24table[SHELL_RB_COLOR];
        else if (mask == (RF_SHELL_BLUE | RF_SHELL_GREEN))
            color.u32 = d_8to24table[SHELL_BG_COLOR];
        else if (mask == (RF_SHELL_DOUBLE))
            color.u32 = d_8to24table[SHELL_DOUBLE_COLOR];
        else if (mask == (RF_SHELL_HALF_DAM))
            color.u32 = d_8to24table[SHELL_HALF_DAM_COLOR];
        else
            color.u32 = d_8to24table[SHELL_WHITE_COLOR];

        r_aliasblendcolor[0] = color.u8[0] * r_alias_alpha;
        r_aliasblendcolor[1] = color.u8[1] * r_alias_alpha;
        r_aliasblendcolor[2] = color.u8[2] * r_alias_alpha;

        d_pdrawspans = R_PolysetDrawSpansConstant8_Blended;
    } else if (currententity->flags & RF_TRANSLUCENT) {
        if (currententity->alpha == 1)
            d_pdrawspans = R_PolysetDrawSpans8_Opaque;
        else
            d_pdrawspans = R_PolysetDrawSpans8_Blended;
    } else {
        d_pdrawspans = R_PolysetDrawSpans8_Opaque;
    }
}

/*
================
R_AliasDrawModel
================
*/
void R_AliasDrawModel(void)
{
    if (r_lerpmodels->integer == 0)
        currententity->backlerp = 0;

    if ((currententity->flags & (RF_WEAPONMODEL | RF_LEFTHAND)) == (RF_WEAPONMODEL | RF_LEFTHAND)) {
        aliasxscale = -aliasxscale;
    }

    /*
    ** we have to set our frame pointers and transformations before
    ** doing any real work
    */
    R_AliasSetupFrames();
    R_AliasSetUpTransform();

    // see if the bounding box lets us trivially reject, also sets
    // trivial accept status
    if (R_AliasCheckBBox() == BBOX_TRIVIAL_REJECT) {
        if ((currententity->flags & (RF_WEAPONMODEL | RF_LEFTHAND)) == (RF_WEAPONMODEL | RF_LEFTHAND)) {
            aliasxscale = -aliasxscale;
        }
        return;
    }

    // set up the skin and verify it exists
    if (!R_AliasSetupSkin()) {
        Com_DPrintf("R_AliasDrawModel %s: NULL skin found\n",
                    currentmodel->name);
        return;
    }

    r_amodels_drawn++;
    R_AliasSetupLighting();

    R_AliasSetupBlend();

    /*
    ** compute this_frame and old_frame addresses
    */
    R_AliasSetUpLerpData(currententity->backlerp);

    if (currententity->flags & RF_DEPTHHACK)
        s_ziscale = (float)0x8000 * (float)0x10000 * 3.0;
    else
        s_ziscale = (float)0x8000 * (float)0x10000;

    R_AliasPreparePoints();

    if ((currententity->flags & (RF_WEAPONMODEL | RF_LEFTHAND)) == (RF_WEAPONMODEL | RF_LEFTHAND)) {
        aliasxscale = -aliasxscale;
    }
}



