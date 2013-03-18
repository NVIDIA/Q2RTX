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
// d_polyset.c: routines for drawing sets of polygons sharing the same
// texture (used for Alias models)

#include "sw.h"

// TODO: put in span spilling to shrink list size
#define DPS_MAXSPANS            MAXHEIGHT+1
// 1 extra for spanpackage that marks end

typedef struct {
    void            *pdest;
    short           *pz;
    int             count;
    byte            *ptex;
    int             sfrac, tfrac, light, zi;
} spanpackage_t;

typedef struct {
    int     isflattop;
    int     numleftedges;
    int     *pleftedgevert0;
    int     *pleftedgevert1;
    int     *pleftedgevert2;
    int     numrightedges;
    int     *prightedgevert0;
    int     *prightedgevert1;
    int     *prightedgevert2;
} edgetable_t;

aliastriangleparms_t aliastriangleparms;

static int  r_p0[6], r_p1[6], r_p2[6];

static int  d_xdenom;

static const edgetable_t    *pedgetable;

static const edgetable_t    edgetables[12] = {
    {0, 1, r_p0, r_p2, NULL, 2, r_p0, r_p1, r_p2},
    {0, 2, r_p1, r_p0, r_p2, 1, r_p1, r_p2, NULL},
    {1, 1, r_p0, r_p2, NULL, 1, r_p1, r_p2, NULL},
    {0, 1, r_p1, r_p0, NULL, 2, r_p1, r_p2, r_p0},
    {0, 2, r_p0, r_p2, r_p1, 1, r_p0, r_p1, NULL},
    {0, 1, r_p2, r_p1, NULL, 1, r_p2, r_p0, NULL},
    {0, 1, r_p2, r_p1, NULL, 2, r_p2, r_p0, r_p1},
    {0, 2, r_p2, r_p1, r_p0, 1, r_p2, r_p0, NULL},
    {0, 1, r_p1, r_p0, NULL, 1, r_p1, r_p2, NULL},
    {1, 1, r_p2, r_p1, NULL, 1, r_p0, r_p1, NULL},
    {1, 1, r_p1, r_p0, NULL, 1, r_p2, r_p0, NULL},
    {0, 1, r_p0, r_p2, NULL, 1, r_p0, r_p1, NULL},
};

static int  a_sstepxfrac, a_tstepxfrac, r_lstepx, a_ststepxwhole;
static int  r_sstepx, r_tstepx, r_lstepy, r_sstepy, r_tstepy;
static int  r_zistepx, r_zistepy;
static int  d_aspancount, d_countextrastep;

static int  ubasestep, errorterm, erroradjustup, erroradjustdown;

static spanpackage_t    *a_spans;
static spanpackage_t    *d_pedgespanpackage;
static byte             *d_pdest, *d_ptex;
static short            *d_pz;
static int              d_sfrac, d_tfrac, d_light, d_zi;
static int              d_ptexextrastep, d_sfracextrastep;
static int              d_tfracextrastep, d_lightextrastep, d_pdestextrastep;
static int              d_lightbasestep, d_pdestbasestep, d_ptexbasestep;
static int              d_sfracbasestep, d_tfracbasestep;
static int              d_ziextrastep, d_zibasestep;
static int              d_pzextrastep, d_pzbasestep;

typedef struct {
    int     quotient;
    int     remainder;
} adivtab_t;

static const adivtab_t  adivtab[32 * 32] = {
#include "adivtab.h"
};

void (*d_pdrawspans)(spanpackage_t *pspanpackage);

void R_PolysetDrawSpansConstant8_Blended(spanpackage_t *pspanpackage);
void R_PolysetDrawSpans8_Blended(spanpackage_t *pspanpackage);
void R_PolysetDrawSpans8_Opaque(spanpackage_t *pspanpackage);

static void R_PolysetSetEdgeTable(void);
static void R_RasterizeAliasPolySmooth(void);


/*
================
R_DrawTriangle
================
*/
void R_DrawTriangle(void)
{
    spanpackage_t spans[DPS_MAXSPANS];
    int dv1_ab, dv0_ac;
    int dv0_ab, dv1_ac;

    dv0_ab = aliastriangleparms.a->u - aliastriangleparms.b->u;
    dv1_ab = aliastriangleparms.a->v - aliastriangleparms.b->v;

    if (!(dv0_ab | dv1_ab))
        return;

    dv0_ac = aliastriangleparms.a->u - aliastriangleparms.c->u;
    dv1_ac = aliastriangleparms.a->v - aliastriangleparms.c->v;

    if (!(dv0_ac | dv1_ac))
        return;

    d_xdenom = (dv0_ac * dv1_ab) - (dv0_ab * dv1_ac);

    if (d_xdenom < 0) {
        a_spans = spans;

        r_p0[0] = aliastriangleparms.a->u;      // u
        r_p0[1] = aliastriangleparms.a->v;      // v
        r_p0[2] = aliastriangleparms.a->s;      // s
        r_p0[3] = aliastriangleparms.a->t;      // t
        r_p0[4] = aliastriangleparms.a->l;      // light
        r_p0[5] = aliastriangleparms.a->zi;     // iz

        r_p1[0] = aliastriangleparms.b->u;
        r_p1[1] = aliastriangleparms.b->v;
        r_p1[2] = aliastriangleparms.b->s;
        r_p1[3] = aliastriangleparms.b->t;
        r_p1[4] = aliastriangleparms.b->l;
        r_p1[5] = aliastriangleparms.b->zi;

        r_p2[0] = aliastriangleparms.c->u;
        r_p2[1] = aliastriangleparms.c->v;
        r_p2[2] = aliastriangleparms.c->s;
        r_p2[3] = aliastriangleparms.c->t;
        r_p2[4] = aliastriangleparms.c->l;
        r_p2[5] = aliastriangleparms.c->zi;

        R_PolysetSetEdgeTable();
        R_RasterizeAliasPolySmooth();
    }
}


/*
===================
R_PolysetScanLeftEdge
====================
*/
static void R_PolysetScanLeftEdge(int height)
{
    do {
        d_pedgespanpackage->pdest = d_pdest;
        d_pedgespanpackage->pz = d_pz;
        d_pedgespanpackage->count = d_aspancount;
        d_pedgespanpackage->ptex = d_ptex;

        d_pedgespanpackage->sfrac = d_sfrac;
        d_pedgespanpackage->tfrac = d_tfrac;

        // FIXME: need to clamp l, s, t, at both ends?
        d_pedgespanpackage->light = d_light;
        d_pedgespanpackage->zi = d_zi;

        d_pedgespanpackage++;

        errorterm += erroradjustup;
        if (errorterm >= 0) {
            d_pdest += d_pdestextrastep;
            d_pz += d_pzextrastep;
            d_aspancount += d_countextrastep;
            d_ptex += d_ptexextrastep;
            d_sfrac += d_sfracextrastep;
            d_ptex += (d_sfrac >> 16) * TEX_BYTES;

            d_sfrac &= 0xFFFF;
            d_tfrac += d_tfracextrastep;
            if (d_tfrac & 0x10000) {
                d_ptex += r_affinetridesc.skinwidth;
                d_tfrac &= 0xFFFF;
            }
            d_light += d_lightextrastep;
            d_zi += d_ziextrastep;
            errorterm -= erroradjustdown;
        } else {
            d_pdest += d_pdestbasestep;
            d_pz += d_pzbasestep;
            d_aspancount += ubasestep;
            d_ptex += d_ptexbasestep;
            d_sfrac += d_sfracbasestep;
            d_ptex += (d_sfrac >> 16) * TEX_BYTES;
            d_sfrac &= 0xFFFF;
            d_tfrac += d_tfracbasestep;
            if (d_tfrac & 0x10000) {
                d_ptex += r_affinetridesc.skinwidth;
                d_tfrac &= 0xFFFF;
            }
            d_light += d_lightbasestep;
            d_zi += d_zibasestep;
        }
    } while (--height);
}

/*
===================
FloorDivMod

Returns mathematically correct (floor-based) quotient and remainder for
numer and denom, both of which should contain no fractional part. The
quotient must fit in 32 bits.
FIXME: GET RID OF THIS! (FloorDivMod)
====================
*/
static void FloorDivMod(float numer, float denom, int *quotient, int *rem)
{
    int     q, r;
    float   x;

    if (numer >= 0.0) {
        x = floor(numer / denom);
        q = (int)x;
        r = (int)floor(numer - (x * denom));
    } else {
        //
        // perform operations with positive values, and fix mod to make floor-based
        //
        x = floor(-numer / denom);
        q = -(int)x;
        r = (int)floor(-numer - (x * denom));
        if (r != 0) {
            q--;
            r = (int)denom - r;
        }
    }

    *quotient = q;
    *rem = r;
}


/*
===================
R_PolysetSetUpForLineScan
====================
*/
static void R_PolysetSetUpForLineScan(fixed8_t startvertu, fixed8_t startvertv,
                                      fixed8_t endvertu, fixed8_t endvertv)
{
    float       dm, dn;
    int         tm, tn;
    const adivtab_t *ptemp;

    errorterm = -1;

    tm = endvertu - startvertu;
    tn = endvertv - startvertv;

    if (((tm <= 16) && (tm >= -15)) &&
        ((tn <= 16) && (tn >= -15))) {
        ptemp = &adivtab[((tm + 15) << 5) + (tn + 15)];
        ubasestep = ptemp->quotient;
        erroradjustup = ptemp->remainder;
        erroradjustdown = tn;
    } else {
        dm = tm;
        dn = tn;

        FloorDivMod(dm, dn, &ubasestep, &erroradjustup);

        erroradjustdown = dn;
    }
}

/*
================
R_PolysetCalcGradients
================
*/
static void R_PolysetCalcGradients(int skinwidth)
{
    float   xstepdenominv, ystepdenominv, t0, t1;
    float   p01_minus_p21, p11_minus_p21, p00_minus_p20, p10_minus_p20;

    p00_minus_p20 = r_p0[0] - r_p2[0];
    p01_minus_p21 = r_p0[1] - r_p2[1];
    p10_minus_p20 = r_p1[0] - r_p2[0];
    p11_minus_p21 = r_p1[1] - r_p2[1];

    xstepdenominv = 1.0 / (float)d_xdenom;

    ystepdenominv = -xstepdenominv;

// ceil () for light so positive steps are exaggerated, negative steps
// diminished,  pushing us away from underflow toward overflow. Underflow is
// very visible, overflow is very unlikely, because of ambient lighting
    t0 = r_p0[4] - r_p2[4];
    t1 = r_p1[4] - r_p2[4];
    r_lstepx = (int)
               ceil((t1 * p01_minus_p21 - t0 * p11_minus_p21) * xstepdenominv);
    r_lstepy = (int)
               ceil((t1 * p00_minus_p20 - t0 * p10_minus_p20) * ystepdenominv);

    t0 = r_p0[2] - r_p2[2];
    t1 = r_p1[2] - r_p2[2];
    r_sstepx = (int)((t1 * p01_minus_p21 - t0 * p11_minus_p21) *
                     xstepdenominv);
    r_sstepy = (int)((t1 * p00_minus_p20 - t0 * p10_minus_p20) *
                     ystepdenominv);

    t0 = r_p0[3] - r_p2[3];
    t1 = r_p1[3] - r_p2[3];
    r_tstepx = (int)((t1 * p01_minus_p21 - t0 * p11_minus_p21) *
                     xstepdenominv);
    r_tstepy = (int)((t1 * p00_minus_p20 - t0 * p10_minus_p20) *
                     ystepdenominv);

    t0 = r_p0[5] - r_p2[5];
    t1 = r_p1[5] - r_p2[5];
    r_zistepx = (int)((t1 * p01_minus_p21 - t0 * p11_minus_p21) *
                      xstepdenominv);
    r_zistepy = (int)((t1 * p00_minus_p20 - t0 * p10_minus_p20) *
                      ystepdenominv);

    a_sstepxfrac = r_sstepx & 0xFFFF;
    a_tstepxfrac = r_tstepx & 0xFFFF;

    a_ststepxwhole = skinwidth * (r_tstepx >> 16) + (r_sstepx >> 16) * TEX_BYTES;
}

void R_PolysetDrawSpans8_Blended(spanpackage_t *pspanpackage)
{
    int     lcount;
    byte    *lpdest;
    byte    *lptex;
    int     lsfrac, ltfrac;
    int     llight;
    int     lzi;
    short   *lpz;
    int     tmp[3];

    do {
        lcount = d_aspancount - pspanpackage->count;

        errorterm += erroradjustup;
        if (errorterm >= 0) {
            d_aspancount += d_countextrastep;
            errorterm -= erroradjustdown;
        } else {
            d_aspancount += ubasestep;
        }

        if (lcount) {
            lpdest = pspanpackage->pdest;
            lptex = pspanpackage->ptex;
            lpz = pspanpackage->pz;
            lsfrac = pspanpackage->sfrac;
            ltfrac = pspanpackage->tfrac;
            llight = pspanpackage->light;
            lzi = pspanpackage->zi;

            do {
                if ((lzi >> 16) >= *lpz) {
                    tmp[0] = (r_aliasblendcolor[0] * llight) >> 15;
                    tmp[1] = (r_aliasblendcolor[1] * llight) >> 15;
                    tmp[2] = (r_aliasblendcolor[2] * llight) >> 15;
                    if (tmp[0] > 255) tmp[0] = 255;
                    if (tmp[1] > 255) tmp[1] = 255;
                    if (tmp[2] > 255) tmp[2] = 255;
                    tmp[0] = (lptex[0] * tmp[0]) >> 8;
                    tmp[1] = (lptex[1] * tmp[1]) >> 8;
                    tmp[2] = (lptex[2] * tmp[2]) >> 8;
                    lpdest[0] = (lpdest[0] * r_alias_one_minus_alpha + tmp[2] * r_alias_alpha) >> 8;
                    lpdest[1] = (lpdest[1] * r_alias_one_minus_alpha + tmp[1] * r_alias_alpha) >> 8;
                    lpdest[2] = (lpdest[2] * r_alias_one_minus_alpha + tmp[0] * r_alias_alpha) >> 8;
                    *lpz = lzi >> 16;
                }
                lpdest += VID_BYTES;
                lzi += r_zistepx;
                lpz++;
                llight += r_lstepx;
                lptex += a_ststepxwhole;
                lsfrac += a_sstepxfrac;
                lptex += (lsfrac >> 16) * TEX_BYTES;
                lsfrac &= 0xFFFF;
                ltfrac += a_tstepxfrac;
                if (ltfrac & 0x10000) {
                    lptex += r_affinetridesc.skinwidth;
                    ltfrac &= 0xFFFF;
                }
            } while (--lcount);
        }

        pspanpackage++;
    } while (pspanpackage->count != -999999);
}

void R_PolysetDrawSpansConstant8_Blended(spanpackage_t *pspanpackage)
{
    int     lcount;
    byte    *lpdest;
    int     lzi;
    short   *lpz;

    do {
        lcount = d_aspancount - pspanpackage->count;

        errorterm += erroradjustup;
        if (errorterm >= 0) {
            d_aspancount += d_countextrastep;
            errorterm -= erroradjustdown;
        } else {
            d_aspancount += ubasestep;
        }

        if (lcount) {
            lpdest = pspanpackage->pdest;
            lpz = pspanpackage->pz;
            lzi = pspanpackage->zi;

            do {
                if ((lzi >> 16) >= *lpz) {
                    lpdest[0] = (lpdest[0] * r_alias_one_minus_alpha + r_aliasblendcolor[2]) >> 8;
                    lpdest[1] = (lpdest[1] * r_alias_one_minus_alpha + r_aliasblendcolor[1]) >> 8;
                    lpdest[2] = (lpdest[2] * r_alias_one_minus_alpha + r_aliasblendcolor[0]) >> 8;
                }
                lpdest += VID_BYTES;
                lzi += r_zistepx;
                lpz++;
            } while (--lcount);
        }

        pspanpackage++;
    } while (pspanpackage->count != -999999);
}

void R_PolysetDrawSpans8_Opaque(spanpackage_t *pspanpackage)
{
    int     lcount;

    do {
        lcount = d_aspancount - pspanpackage->count;

        errorterm += erroradjustup;
        if (errorterm >= 0) {
            d_aspancount += d_countextrastep;
            errorterm -= erroradjustdown;
        } else {
            d_aspancount += ubasestep;
        }

        if (lcount) {
            int     lsfrac, ltfrac;
            byte    *lpdest;
            byte    *lptex;
            int     llight;
            int     lzi;
            short   *lpz;
            int     tmp[3];

            lpdest = pspanpackage->pdest;
            lptex = pspanpackage->ptex;
            lpz = pspanpackage->pz;
            lsfrac = pspanpackage->sfrac;
            ltfrac = pspanpackage->tfrac;
            llight = pspanpackage->light;
            lzi = pspanpackage->zi;

            do {
                if ((lzi >> 16) >= *lpz) {
                    tmp[0] = (r_aliasblendcolor[0] * llight) >> 15;
                    tmp[1] = (r_aliasblendcolor[1] * llight) >> 15;
                    tmp[2] = (r_aliasblendcolor[2] * llight) >> 15;
                    if (tmp[0] > 255) tmp[0] = 255;
                    if (tmp[1] > 255) tmp[1] = 255;
                    if (tmp[2] > 255) tmp[2] = 255;
                    lpdest[0] = (lptex[2] * tmp[2]) >> 8;
                    lpdest[1] = (lptex[1] * tmp[1]) >> 8;
                    lpdest[2] = (lptex[0] * tmp[0]) >> 8;
                    *lpz = lzi >> 16;
                }
                lpdest += VID_BYTES;
                lzi += r_zistepx;
                lpz++;
                llight += r_lstepx;
                lptex += a_ststepxwhole;
                lsfrac += a_sstepxfrac;
                lptex += (lsfrac >> 16) * TEX_BYTES;
                lsfrac &= 0xFFFF;
                ltfrac += a_tstepxfrac;
                if (ltfrac & 0x10000) {
                    lptex += r_affinetridesc.skinwidth;
                    ltfrac &= 0xFFFF;
                }
            } while (--lcount);
        }

        pspanpackage++;
    } while (pspanpackage->count != -999999);
}

static void R_PolysetSetUpAndScanLeftEdge(int *plefttop, int *pleftbottom)
{
    int     height, ystart;
    int     working_lstepx;

    height = pleftbottom[1] - plefttop[1];
    ystart = plefttop[1];

    d_ptex = (byte *)r_affinetridesc.pskin + (plefttop[2] >> 16) * TEX_BYTES +
             (plefttop[3] >> 16) * r_affinetridesc.skinwidth;
    d_sfrac = plefttop[2] & 0xFFFF;
    d_tfrac = plefttop[3] & 0xFFFF;
    d_light = plefttop[4];
    d_zi = plefttop[5];

    d_pdest = d_spantable[ystart] + plefttop[0] * VID_BYTES;
    d_pz = d_zspantable[ystart] + plefttop[0];

    if (height == 1) {
        d_pedgespanpackage->pdest = d_pdest;
        d_pedgespanpackage->pz = d_pz;
        d_pedgespanpackage->count = d_aspancount;
        d_pedgespanpackage->ptex = d_ptex;

        d_pedgespanpackage->sfrac = d_sfrac;
        d_pedgespanpackage->tfrac = d_tfrac;

        // FIXME: need to clamp l, s, t, at both ends?
        d_pedgespanpackage->light = d_light;
        d_pedgespanpackage->zi = d_zi;

        d_pedgespanpackage++;
    } else {
        R_PolysetSetUpForLineScan(plefttop[0], plefttop[1],
                                  pleftbottom[0], pleftbottom[1]);

        d_pzbasestep = d_zwidth + ubasestep;
        d_pzextrastep = d_pzbasestep + 1;

        d_pdestbasestep = d_screenrowbytes + ubasestep * VID_BYTES;
        d_pdestextrastep = d_pdestbasestep + 1 * VID_BYTES;

        // for negative steps in x along left edge, bias toward overflow rather than
        // underflow (sort of turning the floor () we did in the gradient calcs into
        // ceil (), but plus a little bit)
        if (ubasestep < 0)
            working_lstepx = r_lstepx - 1;
        else
            working_lstepx = r_lstepx;

        d_countextrastep = ubasestep + 1;
        d_ptexbasestep = ((r_sstepy + r_sstepx * ubasestep) >> 16) * TEX_BYTES +
                         ((r_tstepy + r_tstepx * ubasestep) >> 16) *
                         r_affinetridesc.skinwidth;
        d_sfracbasestep = (r_sstepy + r_sstepx * ubasestep) & 0xFFFF;
        d_tfracbasestep = (r_tstepy + r_tstepx * ubasestep) & 0xFFFF;
        d_lightbasestep = r_lstepy + working_lstepx * ubasestep;
        d_zibasestep = r_zistepy + r_zistepx * ubasestep;

        d_ptexextrastep = ((r_sstepy + r_sstepx * d_countextrastep) >> 16) * TEX_BYTES +
                          ((r_tstepy + r_tstepx * d_countextrastep) >> 16) *
                          r_affinetridesc.skinwidth;
        d_sfracextrastep = (r_sstepy + r_sstepx * d_countextrastep) & 0xFFFF;
        d_tfracextrastep = (r_tstepy + r_tstepx * d_countextrastep) & 0xFFFF;
        d_lightextrastep = d_lightbasestep + working_lstepx;
        d_ziextrastep = d_zibasestep + r_zistepx;

        R_PolysetScanLeftEdge(height);
    }
}

/*
================
R_RasterizeAliasPolySmooth
================
*/
static void R_RasterizeAliasPolySmooth(void)
{
    int     /*initialleftheight,*/ initialrightheight;
    int     *plefttop, *prighttop, *pleftbottom, *prightbottom;
    int     originalcount;

    plefttop = pedgetable->pleftedgevert0;
    prighttop = pedgetable->prightedgevert0;

    pleftbottom = pedgetable->pleftedgevert1;
    prightbottom = pedgetable->prightedgevert1;

    /*initialleftheight = pleftbottom[1] - plefttop[1];*/
    initialrightheight = prightbottom[1] - prighttop[1];

//
// set the s, t, and light gradients, which are consistent across the triangle
// because being a triangle, things are affine
//
    R_PolysetCalcGradients(r_affinetridesc.skinwidth);

//
// rasterize the polygon
//

//
// scan out the top (and possibly only) part of the left edge
//
    d_pedgespanpackage = a_spans;

    d_aspancount = plefttop[0] - prighttop[0];

    R_PolysetSetUpAndScanLeftEdge(plefttop, pleftbottom);

//
// scan out the bottom part of the left edge, if it exists
//
    if (pedgetable->numleftedges == 2) {
        plefttop = pleftbottom;
        pleftbottom = pedgetable->pleftedgevert2;

        d_aspancount = plefttop[0] - prighttop[0];

        R_PolysetSetUpAndScanLeftEdge(plefttop, pleftbottom);
    }

// scan out the top (and possibly only) part of the right edge, updating the
// count field
    d_pedgespanpackage = a_spans;

    R_PolysetSetUpForLineScan(prighttop[0], prighttop[1],
                              prightbottom[0], prightbottom[1]);
    d_aspancount = 0;
    d_countextrastep = ubasestep + 1;
    originalcount = a_spans[initialrightheight].count;
    a_spans[initialrightheight].count = -999999; // mark end of the spanpackages
    (*d_pdrawspans)(a_spans);

// scan out the bottom part of the right edge, if it exists
    if (pedgetable->numrightedges == 2) {
        int             height;
        spanpackage_t   *pstart;

        pstart = a_spans + initialrightheight;
        pstart->count = originalcount;

        d_aspancount = prightbottom[0] - prighttop[0];

        prighttop = prightbottom;
        prightbottom = pedgetable->prightedgevert2;

        height = prightbottom[1] - prighttop[1];

        R_PolysetSetUpForLineScan(prighttop[0], prighttop[1],
                                  prightbottom[0], prightbottom[1]);

        d_countextrastep = ubasestep + 1;
        a_spans[initialrightheight + height].count = -999999; // mark end of the spanpackages
        (*d_pdrawspans)(pstart);
    }
}


/*
================
R_PolysetSetEdgeTable
================
*/
static void R_PolysetSetEdgeTable(void)
{
    int         edgetableindex;

    edgetableindex = 0; // assume the vertices are already in
                        // top to bottom order

//
// determine which edges are right & left, and the order in which
// to rasterize them
//
    if (r_p0[1] >= r_p1[1]) {
        if (r_p0[1] == r_p1[1]) {
            if (r_p0[1] < r_p2[1])
                pedgetable = &edgetables[2];
            else
                pedgetable = &edgetables[5];

            return;
        } else {
            edgetableindex = 1;
        }
    }

    if (r_p0[1] == r_p2[1]) {
        if (edgetableindex)
            pedgetable = &edgetables[8];
        else
            pedgetable = &edgetables[9];

        return;
    } else if (r_p1[1] == r_p2[1]) {
        if (edgetableindex)
            pedgetable = &edgetables[10];
        else
            pedgetable = &edgetables[11];

        return;
    }

    if (r_p0[1] > r_p2[1])
        edgetableindex += 2;

    if (r_p1[1] > r_p2[1])
        edgetableindex += 4;

    pedgetable = &edgetables[edgetableindex];
}


