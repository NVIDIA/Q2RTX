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
// d_scan.c
//
// Portable C scan-level rasterization code, all pixel depths.

#include "sw.h"

/*
=============
D_WarpScreen

this performs a slight compression of the screen at the same time as
the sine warp, to keep the edges from wrapping
=============
*/
void D_WarpScreen(void)
{
    int     w, h;
    int     u, v, u2, v2;
    byte    *dest;
    int     *turb;
    int     *col;
    byte    **row;

    static int  cached_width, cached_height;
    static byte *rowptr[MAXHEIGHT * 2 + AMP2 * 2];
    static int  column[MAXWIDTH * 2 + AMP2 * 2];

    //
    // these are constant over resolutions, and can be saved
    //
    w = r_newrefdef.width;
    h = r_newrefdef.height;
    if (w != cached_width || h != cached_height) {
        cached_width = w;
        cached_height = h;
        for (v = 0; v < h + AMP2 * 2; v++) {
            v2 = (int)((float)v / (h + AMP2 * 2) * r_refdef.vrect.height);
            rowptr[v] = r_warpbuffer + (WARP_WIDTH * v2) * VID_BYTES;
        }

        for (u = 0; u < w + AMP2 * 2; u++) {
            u2 = (int)((float)u / (w + AMP2 * 2) * r_refdef.vrect.width);
            column[u] = u2 * VID_BYTES;
        }
    }

    turb = intsintable + ((int)(r_newrefdef.time * SPEED) & (CYCLE - 1));
    dest = vid.buffer + r_newrefdef.y * vid.rowbytes + r_newrefdef.x * VID_BYTES;

    for (v = 0; v < h; v++, dest += vid.rowbytes) {
        col = &column[turb[v & (CYCLE - 1)]];
        row = &rowptr[v];
        for (u = 0; u < w; u++) {
            dest[u * VID_BYTES + 0] = row[turb[u & (CYCLE - 1)]][col[u] + 0];
            dest[u * VID_BYTES + 1] = row[turb[u & (CYCLE - 1)]][col[u] + 1];
            dest[u * VID_BYTES + 2] = row[turb[u & (CYCLE - 1)]][col[u] + 2];
        }
    }
}

/*
=============
D_DrawTurbulent16
=============
*/
void D_DrawTurbulent16(espan_t *pspan, int *warptable)
{
    int             count, spancount;
    byte            *pbase, *pdest, *ptex;
    fixed16_t       s, t, snext, tnext, sstep, tstep;
    float           sdivz, tdivz, zi, z, du, dv, spancountminus1;
    float           sdivz16stepu, tdivz16stepu, zi16stepu;
    int             *turb;
    int             turb_s, turb_t;

    turb = warptable + ((int)(r_newrefdef.time * SPEED) & (CYCLE - 1));

    sstep = 0;   // keep compiler happy
    tstep = 0;   // ditto

    pbase = (byte *)cacheblock;

    sdivz16stepu = d_sdivzstepu * 16;
    tdivz16stepu = d_tdivzstepu * 16;
    zi16stepu = d_zistepu * 16;

    do {
        pdest = d_spantable[pspan->v] + pspan->u * VID_BYTES;

        count = pspan->count;

        // calculate the initial s/z, t/z, 1/z, s, and t and clamp
        du = (float)pspan->u;
        dv = (float)pspan->v;

        sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
        tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
        zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
        z = (float)0x10000 / zi;    // prescale to 16.16 fixed-point

        s = (int)(sdivz * z) + sadjust;
        if (s > bbextents)
            s = bbextents;
        else if (s < 0)
            s = 0;

        t = (int)(tdivz * z) + tadjust;
        if (t > bbextentt)
            t = bbextentt;
        else if (t < 0)
            t = 0;

        do {
            // calculate s and t at the far end of the span
            if (count >= 16)
                spancount = 16;
            else
                spancount = count;

            count -= spancount;

            if (q_likely(count)) {
                // calculate s/z, t/z, zi->fixed s and t at far end of span,
                // calculate s and t steps across span by shifting
                sdivz += sdivz16stepu;
                tdivz += tdivz16stepu;
                zi += zi16stepu;
                z = (float)0x10000 / zi;    // prescale to 16.16 fixed-point

                snext = (int)(sdivz * z) + sadjust;
                if (snext > bbextents)
                    snext = bbextents;
                else if (snext < 16)
                    snext = 16; // prevent round-off error on <0 steps from
                                // from causing overstepping & running off the
                                // edge of the texture

                tnext = (int)(tdivz * z) + tadjust;
                if (tnext > bbextentt)
                    tnext = bbextentt;
                else if (tnext < 16)
                    tnext = 16; // guard against round-off error on <0 steps

                sstep = (snext - s) >> 4;
                tstep = (tnext - t) >> 4;
            } else {
                // calculate s/z, t/z, zi->fixed s and t at last pixel in span (so
                // can't step off polygon), clamp, calculate s and t steps across
                // span by division, biasing steps low so we don't run off the
                // texture
                spancountminus1 = (float)(spancount - 1);
                sdivz += d_sdivzstepu * spancountminus1;
                tdivz += d_tdivzstepu * spancountminus1;
                zi += d_zistepu * spancountminus1;
                z = (float)0x10000 / zi;    // prescale to 16.16 fixed-point

                snext = (int)(sdivz * z) + sadjust;
                if (snext > bbextents)
                    snext = bbextents;
                else if (snext < 16)
                    snext = 16; // prevent round-off error on <0 steps from
                                // from causing overstepping & running off the
                                // edge of the texture

                tnext = (int)(tdivz * z) + tadjust;
                if (tnext > bbextentt)
                    tnext = bbextentt;
                else if (tnext < 16)
                    tnext = 16; // guard against round-off error on <0 steps

                if (spancount > 1) {
                    sstep = (snext - s) / (spancount - 1);
                    tstep = (tnext - t) / (spancount - 1);
                }
            }

            s = s & ((CYCLE << 16) - 1);
            t = t & ((CYCLE << 16) - 1);

            do {
                turb_s = ((s + turb[(t >> 16) & (CYCLE - 1)]) >> 16) & TURB_MASK;
                turb_t = ((t + turb[(s >> 16) & (CYCLE - 1)]) >> 16) & TURB_MASK;
                ptex = pbase + (turb_t * TURB_SIZE * TEX_BYTES) + turb_s * TEX_BYTES;
                pdest[0] = ptex[2];
                pdest[1] = ptex[1];
                pdest[2] = ptex[0];
                pdest += VID_BYTES;
                s += sstep;
                t += tstep;
            } while (--spancount > 0);

            s = snext;
            t = tnext;

        } while (count > 0);

    } while ((pspan = pspan->pnext) != NULL);
}

/*
=============
D_DrawSpans16
=============
*/
void D_DrawSpans16(espan_t *pspan)
{
    int             count, spancount;
    byte            *pbase, *pdest, *ptex;
    fixed16_t       s, t, snext, tnext, sstep, tstep;
    float           sdivz, tdivz, zi, z, du, dv, spancountminus1;
    float           sdivz16stepu, tdivz16stepu, zi16stepu;

    sstep = 0;  // keep compiler happy
    tstep = 0;  // ditto

    pbase = (byte *)cacheblock;

    sdivz16stepu = d_sdivzstepu * 16;
    tdivz16stepu = d_tdivzstepu * 16;
    zi16stepu = d_zistepu * 16;

    do {
        pdest = d_spantable[pspan->v] + pspan->u * VID_BYTES;

        count = pspan->count;

        // calculate the initial s/z, t/z, 1/z, s, and t and clamp
        du = (float)pspan->u;
        dv = (float)pspan->v;

        sdivz = d_sdivzorigin + dv * d_sdivzstepv + du * d_sdivzstepu;
        tdivz = d_tdivzorigin + dv * d_tdivzstepv + du * d_tdivzstepu;
        zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
        z = (float)0x10000 / zi;    // prescale to 16.16 fixed-point

        s = (int)(sdivz * z) + sadjust;
        if (s > bbextents)
            s = bbextents;
        else if (s < 0)
            s = 0;

        t = (int)(tdivz * z) + tadjust;
        if (t > bbextentt)
            t = bbextentt;
        else if (t < 0)
            t = 0;

        do {
            // calculate s and t at the far end of the span
            if (count >= 16)
                spancount = 16;
            else
                spancount = count;

            count -= spancount;

            if (q_likely(count)) {
                // calculate s/z, t/z, zi->fixed s and t at far end of span,
                // calculate s and t steps across span by shifting
                sdivz += sdivz16stepu;
                tdivz += tdivz16stepu;
                zi += zi16stepu;
                z = (float)0x10000 / zi;    // prescale to 16.16 fixed-point

                snext = (int)(sdivz * z) + sadjust;
                if (snext > bbextents)
                    snext = bbextents;
                else if (snext < 16)
                    snext = 16; // prevent round-off error on <0 steps from
                                // from causing overstepping & running off the
                                // edge of the texture

                tnext = (int)(tdivz * z) + tadjust;
                if (tnext > bbextentt)
                    tnext = bbextentt;
                else if (tnext < 16)
                    tnext = 16;  // guard against round-off error on <0 steps

                sstep = (snext - s) >> 4;
                tstep = (tnext - t) >> 4;
            } else {
                // calculate s/z, t/z, zi->fixed s and t at last pixel in span (so
                // can't step off polygon), clamp, calculate s and t steps across
                // span by division, biasing steps low so we don't run off the
                // texture
                spancountminus1 = (float)(spancount - 1);
                sdivz += d_sdivzstepu * spancountminus1;
                tdivz += d_tdivzstepu * spancountminus1;
                zi += d_zistepu * spancountminus1;
                z = (float)0x10000 / zi;    // prescale to 16.16 fixed-point

                snext = (int)(sdivz * z) + sadjust;
                if (snext > bbextents)
                    snext = bbextents;
                else if (snext < 16)
                    snext = 16; // prevent round-off error on <0 steps from
                                // from causing overstepping & running off the
                                // edge of the texture

                tnext = (int)(tdivz * z) + tadjust;
                if (tnext > bbextentt)
                    tnext = bbextentt;
                else if (tnext < 16)
                    tnext = 16;  // guard against round-off error on <0 steps

                if (spancount > 1) {
                    sstep = (snext - s) / (spancount - 1);
                    tstep = (tnext - t) / (spancount - 1);
                }
            }

            do {
                ptex = pbase + (s >> 16) * TEX_BYTES + (t >> 16) * cachewidth;
                pdest[0] = ptex[2];
                pdest[1] = ptex[1];
                pdest[2] = ptex[0];
                pdest += VID_BYTES;
                s += sstep;
                t += tstep;
            } while (--spancount > 0);

            s = snext;
            t = tnext;

        } while (count > 0);

    } while ((pspan = pspan->pnext) != NULL);
}

/*
=============
D_DrawZSpans
=============
*/
void D_DrawZSpans(espan_t *pspan)
{
    int             count, doublecount, izistep;
    int             izi;
    short           *pdest;
    uint32_t        ltemp;
    float           zi;
    float           du, dv;

// FIXME: check for clamping/range problems
// we count on FP exceptions being turned off to avoid range problems
    izistep = (int)(d_zistepu * 0x8000 * 0x10000);

    do {
        pdest = d_zspantable[pspan->v] + pspan->u;

        count = pspan->count;

        // calculate the initial 1/z
        du = (float)pspan->u;
        dv = (float)pspan->v;

        zi = d_ziorigin + dv * d_zistepv + du * d_zistepu;
        // we count on FP exceptions being turned off to avoid range problems
        izi = (int)(zi * 0x8000 * 0x10000);

        if ((uintptr_t)pdest & 0x02) {
            *pdest++ = (short)(izi >> 16);
            izi += izistep;
            count--;
        }

        if ((doublecount = count >> 1) > 0) {
            do {
                ltemp = izi >> 16;
                izi += izistep;
                ltemp |= izi & 0xFFFF0000;
                izi += izistep;
                *(uint32_t *)pdest = ltemp;
                pdest += 2;
            } while (--doublecount > 0);
        }

        if (count & 1)
            *pdest = (short)(izi >> 16);

    } while ((pspan = pspan->pnext) != NULL);
}

