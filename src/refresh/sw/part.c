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

#include "sw.h"

typedef struct {
    particle_t  *particle;
    fixed8_t    color[3];
    int         one_minus_alpha;
    vec3_t      right, up, pn;
} partparms_t;

static partparms_t partparms;

/*
** R_DrawParticle
**
** Yes, this is amazingly slow, but it's the C reference
** implementation and should be both robust and vaguely
** understandable.  The only time this path should be
** executed is if we're debugging on x86 or if we're
** recompiling and deploying on a non-x86 platform.
**
** To minimize error and improve readability I went the
** function pointer route.  This exacts some overhead, but
** it pays off in clean and easy to understand code.
*/
static void R_DrawParticle(void)
{
    vec3_t  local, transformed;
    float   zi;
    byte    *pdest;
    short   *pz;
    int     i, izi, pix, count, u, v;

    /*
    ** transform the particle
    */
    VectorSubtract(partparms.particle->origin, r_origin, local);

    transformed[0] = DotProduct(local, partparms.right);
    transformed[1] = DotProduct(local, partparms.up);
    transformed[2] = DotProduct(local, partparms.pn);

    if (transformed[2] < PARTICLE_Z_CLIP)
        return;

    /*
    ** project the point
    */
    // FIXME: preadjust xcenter and ycenter
    zi = 1.0 / transformed[2];
    u = (int)(r_refdef.xcenter + zi * transformed[0] + 0.5);
    v = (int)(r_refdef.ycenter - zi * transformed[1] + 0.5);

    if (v > r_refdef.vrectbottom_particle ||
        u > r_refdef.vrectright_particle ||
        v < r_refdef.vrect.y ||
        u < r_refdef.vrect.x) {
        return;
    }

    /*
    ** compute addresses of zbuffer, framebuffer, and
    ** compute the Z-buffer reference value.
    */
    pz = d_zspantable[v] + u;
    pdest = d_spantable[v] + u * VID_BYTES;
    izi = (int)(zi * 0x8000);

    /*
    ** determine the screen area covered by the particle,
    ** which also means clamping to a min and max
    */
    pix = izi >> r_refdef.pix_shift;
    if (pix < r_refdef.pix_min)
        pix = r_refdef.pix_min;
    else if (pix > r_refdef.pix_max)
        pix = r_refdef.pix_max;

    /*
    ** render the appropriate pixels
    */
    for (count = pix; count; count--, pz += d_zwidth, pdest += d_screenrowbytes) {
        for (i = 0; i < pix; i++) {
            if (pz[i] <= izi) {
                pz[i] = izi;
                pdest[i * VID_BYTES + 0] = (pdest[i * VID_BYTES + 0] * partparms.one_minus_alpha + partparms.color[2]) >> 8;
                pdest[i * VID_BYTES + 1] = (pdest[i * VID_BYTES + 1] * partparms.one_minus_alpha + partparms.color[1]) >> 8;
                pdest[i * VID_BYTES + 2] = (pdest[i * VID_BYTES + 2] * partparms.one_minus_alpha + partparms.color[0]) >> 8;
            }
        }
    }
}

/*
** R_DrawParticles
**
** Responsible for drawing all of the particles in the particle list
** throughout the world.  Doesn't care if we're using the C path or
** if we're using the asm path, it simply assigns a function pointer
** and goes.
*/
void R_DrawParticles(void)
{
    particle_t *p;
    int         i;
    int         alpha;

    VectorScale(vright, r_refdef.xscaleshrink, partparms.right);
    VectorScale(vup, r_refdef.yscaleshrink, partparms.up);
    VectorCopy(vpn, partparms.pn);

    for (p = r_newrefdef.particles, i = 0; i < r_newrefdef.num_particles; i++, p++) {
        partparms.particle = p;

        alpha = 255 * p->alpha;
        partparms.one_minus_alpha = 255 - alpha;

        if (p->color == -1) {
            partparms.color[0] = p->rgba.u8[0] * alpha;
            partparms.color[1] = p->rgba.u8[1] * alpha;
            partparms.color[2] = p->rgba.u8[2] * alpha;
        } else {
            color_t color;

            color.u32 = d_8to24table[p->color & 0xff];
            partparms.color[0] = color.u8[0] * alpha;
            partparms.color[1] = color.u8[1] * alpha;
            partparms.color[2] = color.u8[2] * alpha;
        }

        R_DrawParticle();
    }
}

