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
// r_light.c

#include "sw_local.h"

int	r_dlightframecount;


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights
=============
*/
void R_MarkLights (dlight_t *light, int bit, mnode_t *node)
{
	cplane_t	*splitplane;
	float		dist;
	mface_t	*surf;
	int			i;
	
	if (!node->plane)
		return;

	splitplane = node->plane;
	dist = DotProduct (light->origin, splitplane->normal) - splitplane->dist;
	
//=====
//PGM
	i=light->intensity;
	if(i<0)
		i=-i;
//PGM
//=====

	if (dist > i)	// PGM (dist > light->intensity)
	{
		R_MarkLights (light, bit, node->children[0]);
		return;
	}
	if (dist < -i)	// PGM (dist < -light->intensity)
	{
		R_MarkLights (light, bit, node->children[1]);
		return;
	}
		
// mark the polygons
	surf = node->firstface;
	for (i=0 ; i<node->numfaces ; i++, surf++)
	{
		if (surf->dlightframe != r_dlightframecount)
		{
			surf->dlightbits = 0;
			surf->dlightframe = r_dlightframecount;
		}
		surf->dlightbits |= bit;
	}

	R_MarkLights (light, bit, node->children[0]);
	R_MarkLights (light, bit, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void R_PushDlights (mnode_t *headnode)
{
	int		i;
	dlight_t	*l;

	r_dlightframecount = r_framecount;
	for (i=0, l = r_newrefdef.dlights ; i<r_newrefdef.num_dlights ; i++, l++)
	{
		R_MarkLights ( l, 1 << i, headnode);
	}
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

static qboolean RecursiveLightPoint (vec3_t p, vec3_t color) {
	mface_t	    *surf;
	int			ds, dt;
	byte		*lightmap;
	float		*scales;
	int			maps;
	float		samp;
    vec3_t      end;

	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - 2048;

    surf = BSP_LightPoint( r_worldmodel->nodes, p, end, &ds, &dt );
    if( !surf ) {
        return qfalse;
    }

    ds >>= 4;
    dt >>= 4;

    lightmap = surf->lightmap;
    lightmap += dt * ((surf->extents[0]>>4)+1) + ds;

    for (maps = 0 ; maps < MAX_LIGHTMAPS && surf->styles[maps] != 255 ; maps++)
    {
        samp = *lightmap * (1.0/255);	// adjust for gl scale
        scales = r_newrefdef.lightstyles[surf->styles[maps]].rgb;
        VectorMA (color, samp, scales, color);
        lightmap += ((surf->extents[0]>>4)+1) * ((surf->extents[1]>>4)+1);
    }
    return qtrue;
}

/*
===============
R_LightPoint
===============
*/
void R_LightPoint (vec3_t p, vec3_t color)
{
	int			lnum;
	dlight_t	*dl;
	float		light;
	vec3_t		dist;
	float		add;
	
	if (!r_worldmodel || !r_worldmodel->lightmap || !r_newrefdef.lightstyles)
	{
		color[0] = color[1] = color[2] = 1.0;
		return;
	}
	
    VectorClear( color );

	RecursiveLightPoint (p, color);

	//
	// add dynamic lights
	//
	light = 0;
	for (lnum=0 ; lnum<r_newrefdef.num_dlights ; lnum++)
	{
		dl = &r_newrefdef.dlights[lnum];
		VectorSubtract (p,
						dl->origin,
						dist);
		add = dl->intensity - VectorLength(dist);
		add *= (1.0/256);
		if (add > 0)
		{
			VectorMA (color, add, dl->color, color);
		}
	}
}

//===================================================================

blocklight_t		blocklights[MAX_BLOCKLIGHTS];

/*
===============
R_AddDynamicLights
===============
*/
static void R_AddDynamicLights( void ) {
	mface_t *surf;
	int			lnum;
	int			sd, td;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	int			s, t;
	int			i;
	int			smax, tmax;
	mtexinfo_t	*tex;
	dlight_t	*dl;
	int			negativeLight;	//PGM

	surf = r_drawsurf.surf;
	smax = (surf->extents[0]>>4)+1;
	tmax = (surf->extents[1]>>4)+1;
	tex = surf->texinfo;

	for ( lnum = 0; lnum < r_newrefdef.num_dlights; lnum++ ) {
		if ( !(surf->dlightbits & (1<<lnum) ) )
			continue;		// not lit by this light

		dl = &r_newrefdef.dlights[lnum];
		rad = dl->intensity;

		negativeLight = 0;
		if(rad < 0) {
			negativeLight = 1;
			rad = -rad;
		}

		dist = PlaneDiffFast (dl->origin, surf->plane);
		rad -= fabs(dist);
		minlight = 32;		// dl->minlight;
		if (rad < minlight)
			continue;
		minlight = rad - minlight;

		for (i=0 ; i<3 ; i++) {
			impact[i] = dl->origin[i] - surf->plane->normal[i]*dist;
		}

		local[0] = DotProduct (impact, tex->axis[0]) + tex->offset[0];
		local[1] = DotProduct (impact, tex->axis[1]) + tex->offset[1];

		local[0] -= surf->texturemins[0];
		local[1] -= surf->texturemins[1];
		
		for (t = 0 ; t<tmax ; t++) {
			td = local[1] - t*16;
			if (td < 0)
				td = -td;
			for (s=0 ; s<smax ; s++) {
				sd = local[0] - s*16;
				if (sd < 0)
					sd = -sd;
				/*if (sd > td)
					dist = sd + (td>>1);
				else
					dist = td + (sd>>1);*/
				dist = sqrt( sd * sd + td * td );
				if(!negativeLight) {
					if (dist < minlight)
						blocklights[t*smax + s] += (rad - dist)*256;
				} else {
					if (dist < minlight)
						blocklights[t*smax + s] -= (rad - dist)*256;
					if(blocklights[t*smax + s] < minlight)
						blocklights[t*smax + s] = minlight;
				}
			}
		}
	}
}


/*
===============
R_BuildLightMap

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/

void R_BuildLightMap( void ) {
	int			smax, tmax;
	blocklight_t			t;
	int			i, size;
	byte		*lightmap;
	int			maps;
	mface_t	*surf;
	blocklight_t *dst;

	surf = r_drawsurf.surf;

	smax = ( surf->extents[0] >> 4 ) + 1;
	tmax = ( surf->extents[1] >> 4 ) + 1;
	size = smax * tmax;
	if( size > MAX_BLOCKLIGHTS ) {
		Com_Error( ERR_DROP, "R_BuildLightMap: surface blocklights size %i > %i", size, MAX_BLOCKLIGHTS );
	}

// clear to no light
	dst = blocklights;
	for( i = 0; i < size; i++ ) {
		*dst++ = 0;
	}
	
	if( r_fullbright->integer || !r_worldmodel->lightmap ) {
		return;
	}

// add all the lightmaps
	lightmap = surf->lightmap;
	if( lightmap ) {
		for( maps = 0; maps < MAX_LIGHTMAPS && surf->styles[maps] != 255; maps++ ) {
			fixed8_t		scale;
			
			dst = blocklights;
			scale = r_drawsurf.lightadj[maps];	// 8.8 fraction		
			for( i = 0; i < size; i++ ) {
				blocklights[i] += lightmap[0] * scale;

				lightmap++; dst++;
			}
		}
	}

// add all the dynamic lights
	if( surf->dlightframe == r_framecount )
		R_AddDynamicLights();

// bound, invert, and shift
	for( i = 0; i < size; i++ ) {
		t = blocklights[i];
		if( t < 0 )
			t = 0;
		t = ( 255 * 256 - t ) >> ( 8 - VID_CBITS );

		if( t < ( 1 << 6 ) )
			t = ( 1 << 6 );

		blocklights[i] = t;
	}

}

