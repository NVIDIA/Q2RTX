/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_sprite.c
#include "sw_local.h"

extern polydesc_t r_polydesc;

void R_BuildPolygonFromSurface(mface_t *fa);
void R_PolygonCalculateGradients (void);

extern void R_PolyChooseSpanletRoutine( float alpha, qboolean isturbulent );

extern vec5_t r_clip_verts[2][MAXWORKINGVERTS+2];

extern void	R_ClipAndDrawPoly( float alpha, qboolean isturbulent, qboolean textured );

/*
** R_DrawSprite
**
** Draw currententity / currentmodel as a single texture
** mapped polygon
*/
void R_DrawSprite (void)
{
	vec5_t		*pverts;
	vec3_t		left, up, right, down;
	mspriteframe_t	*frame;

	frame = &currentmodel->spriteframes[
        currententity->frame % currentmodel->numframes];

	r_polydesc.pixels       = frame->image->pixels[0];
	r_polydesc.pixel_width  = frame->width;
	r_polydesc.pixel_height = frame->height;
	r_polydesc.dist         = 0;

	// generate the sprite's axes, completely parallel to the viewplane.
	VectorCopy (vup, r_polydesc.vup);
	VectorCopy (vright, r_polydesc.vright);
	VectorCopy (vpn, r_polydesc.vpn);

// build the sprite poster in worldspace
	VectorScale (r_polydesc.vright, 
		frame->width - frame->origin_x, right);
	VectorScale (r_polydesc.vup, 
		frame->height - frame->origin_y, up);
	VectorScale (r_polydesc.vright,
		-frame->origin_x, left);
	VectorScale (r_polydesc.vup,
		-frame->origin_y, down);

	// invert UP vector for sprites
	VectorNegate( r_polydesc.vup, r_polydesc.vup );

	pverts = r_clip_verts[0];

	pverts[0][0] = r_entorigin[0] + up[0] + left[0];
	pverts[0][1] = r_entorigin[1] + up[1] + left[1];
	pverts[0][2] = r_entorigin[2] + up[2] + left[2];
	pverts[0][3] = 0;
	pverts[0][4] = 0;

	pverts[1][0] = r_entorigin[0] + up[0] + right[0];
	pverts[1][1] = r_entorigin[1] + up[1] + right[1];
	pverts[1][2] = r_entorigin[2] + up[2] + right[2];
	pverts[1][3] = frame->width;
	pverts[1][4] = 0;

	pverts[2][0] = r_entorigin[0] + down[0] + right[0];
	pverts[2][1] = r_entorigin[1] + down[1] + right[1];
	pverts[2][2] = r_entorigin[2] + down[2] + right[2];
	pverts[2][3] = frame->width;
	pverts[2][4] = frame->height;

	pverts[3][0] = r_entorigin[0] + down[0] + left[0];
	pverts[3][1] = r_entorigin[1] + down[1] + left[1];
	pverts[3][2] = r_entorigin[2] + down[2] + left[2];
	pverts[3][3] = 0;
	pverts[3][4] = frame->height;

	r_polydesc.nump = 4;
	r_polydesc.s_offset = ( r_polydesc.pixel_width  >> 1);
	r_polydesc.t_offset = ( r_polydesc.pixel_height >> 1);
	VectorCopy( modelorg, r_polydesc.viewer_position );

	r_polydesc.stipple_parity = 1;
	if ( currententity->flags & RF_TRANSLUCENT )
		R_ClipAndDrawPoly ( currententity->alpha, qfalse, qtrue );
	else
		R_ClipAndDrawPoly ( 1.0F, qfalse, qtrue );
	r_polydesc.stipple_parity = 0;
}

