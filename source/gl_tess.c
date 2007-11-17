/*
Copyright (C) 2003-2006 Andrey Nazarov

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

#include "gl_local.h"

tesselator_t tess;

void GL_Flush2D( void ) {
	glStateBits_t bits;

    if( !tess.numVertices ) {
        return;
    }

	bits = GLS_DEPTHTEST_DISABLE;
    if( tess.istrans & 2 ) {
        bits |= GLS_BLEND_BLEND;
    } else if( tess.istrans & 1 ) {
        bits |= GLS_ALPHATEST_ENABLE;
	}

    GL_BindTexture( tess.texnum );
	GL_TexEnv( GL_MODULATE );
	GL_Bits( bits );

	qglEnableClientState( GL_COLOR_ARRAY );

	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.colors );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.tcoords );
	qglVertexPointer( 3, GL_FLOAT, 16, tess.vertices );
   
	qglDrawArrays( GL_QUADS, 0, tess.numVertices );

	qglDisableClientState( GL_COLOR_ARRAY );
    
	tess.numVertices = 0;
    tess.texnum = 0;
	tess.istrans = 0;
}

void GL_StretchPic( float x, float y, float w, float h,
        float s1, float t1, float s2, float t2, const byte *color, image_t *image )
{
    vec_t *dst_vert;
    tcoord_t *dst_tc;
    uint32 *dst_color;
    
    if( tess.numVertices + 4 > TESS_MAX_VERTICES ||
        ( tess.numVertices && tess.texnum != image->texnum ) )
    {
        GL_Flush2D();
    }

    tess.texnum = image->texnum;

    dst_vert = tess.vertices + tess.numVertices * 4;
    VectorSet( dst_vert,      x,     y,     0 );
    VectorSet( dst_vert +  4, x + w, y,     0 );
    VectorSet( dst_vert +  8, x + w, y + h, 0 );
    VectorSet( dst_vert + 12, x,     y + h, 0 );

    dst_color = ( uint32 * )tess.colors + tess.numVertices;
    dst_color[0] = *( const uint32 * )color;
    dst_color[1] = *( const uint32 * )color;
    dst_color[2] = *( const uint32 * )color;
    dst_color[3] = *( const uint32 * )color;
	
	if( image->flags & if_transparent ) {
        if( ( image->flags & if_paletted ) && draw.scale == 1 ) {
    		tess.istrans |= 1;
        } else {
    		tess.istrans |= 2;
        }
	}
	if( color[3] != 255 ) {
		tess.istrans |= 2;
	}

    dst_tc = tess.tcoords + tess.numVertices;
    dst_tc[0].st[0] = s1; dst_tc[0].st[1] = t1;
    dst_tc[1].st[0] = s2; dst_tc[1].st[1] = t1;
    dst_tc[2].st[0] = s2; dst_tc[2].st[1] = t2;
    dst_tc[3].st[0] = s1; dst_tc[3].st[1] = t2;

    tess.numVertices += 4;
}

void GL_DrawParticles( void ) {
    particle_t *p;
    int i;
    vec3_t transformed;
    vec_t scale, dist;
    color_t color;
    int numVertices;
    vec_t *dst_vert;
    uint32 *dst_color;
    tcoord_t *dst_tc;

    if( !glr.fd.num_particles ) {
        return;
    }

    GL_BindTexture( r_particletexture->texnum );
    GL_TexEnv( GL_MODULATE );
	GL_Bits( GLS_BLEND_BLEND | GLS_DEPTHMASK_FALSE );

    qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.colors );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.tcoords );
	qglVertexPointer( 3, GL_FLOAT, 16, tess.vertices );

    numVertices = 0;
    for( i = 0, p = glr.fd.particles; i < glr.fd.num_particles; i++, p++ ) {
        VectorSubtract( p->origin, glr.fd.vieworg, transformed );
        dist = DotProduct( transformed, glr.viewaxis[0] );

        scale = gl_partscale->value;
        if( dist > 20 ) {
            scale += dist * 0.006f;
        }

        if( p->color == 255 ) {
            *( uint32 * )color = *( uint32 * )p->rgb;
        } else {
            *( uint32 * )color = d_8to24table[p->color & 255];
        }
        color[3] = p->alpha * 255;

        if( numVertices + 3 > TESS_MAX_VERTICES ) {
	        qglDrawArrays( GL_TRIANGLES, 0, numVertices );
            numVertices = 0;
        }
        
        dst_vert = tess.vertices + numVertices * 4;
//        VectorMA( p->origin, -scale*0.5f, glr.viewaxis[2], dst_vert );
        VectorMA( p->origin, scale*0.5f, glr.viewaxis[1], dst_vert );
        VectorMA( dst_vert, scale, glr.viewaxis[2], dst_vert + 4 );
        VectorMA( dst_vert, -scale, glr.viewaxis[1], dst_vert + 8 );

        dst_color = ( uint32 * )tess.colors + numVertices;
        dst_color[0] = *( uint32 * )color;
        dst_color[1] = *( uint32 * )color;
        dst_color[2] = *( uint32 * )color;

        dst_tc = tess.tcoords + numVertices;
        dst_tc[0].st[0] = 0.0625f; dst_tc[0].st[1] = 0.0625f;
        dst_tc[1].st[0] = 1.0625f; dst_tc[1].st[1] = 0.0625f;
        dst_tc[2].st[0] = 0.0625f; dst_tc[2].st[1] = 1.0625f;

        numVertices += 3;
    }

	qglDrawArrays( GL_TRIANGLES, 0, numVertices );
	qglDisableClientState( GL_COLOR_ARRAY );
}

/* all things serve the Beam */
void GL_DrawBeams( void ) {
	vec3_t d1, d2, d3;
	vec_t *start, *end;
	color_t color;
    vec_t *dst_vert;
    uint32 *dst_color;
    tcoord_t *dst_tc;
	vec_t length;
	int numVertices;
	entity_t *ent;
	int i;

    if( !glr.num_beams ) {
        return;
    }

    GL_BindTexture( r_beamtexture->texnum );
    GL_TexEnv( GL_MODULATE );
	GL_Bits( GLS_BLEND_ADD | GLS_DEPTHMASK_FALSE );
    qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.colors );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.tcoords );
	qglVertexPointer( 3, GL_FLOAT, 16, tess.vertices );

	numVertices = 0;
	for( i = 0, ent = glr.fd.entities; i < glr.fd.num_entities; i++, ent++ ) {
		if( !( ent->flags & RF_BEAM ) ) {
			continue;
		}

		start = ent->origin;
		end = ent->oldorigin;
		VectorSubtract( end, start, d1 );
		VectorSubtract( glr.fd.vieworg, start, d2 );
		CrossProduct( d1, d2, d3 );
		VectorNormalize( d3 );
		VectorScale( d3, ent->frame*1.2f, d3 );

		length = VectorLength( d1 );

		if( ent->lightstyle ) {
			*( uint32 * )color = *( uint32 * )&ent->skinnum;
		} else {
			*( uint32 * )color = d_8to24table[ent->skinnum & 0xFF];
		}
		color[3] = 255 * ent->alpha;

        if( numVertices + 4 > TESS_MAX_VERTICES ) {
	        qglDrawArrays( GL_QUADS, 0, numVertices );
            numVertices = 0;
        }

		dst_vert = tess.vertices + numVertices * 4;
		VectorAdd( start, d3, dst_vert );
		VectorSubtract( start, d3, dst_vert + 4 );
		VectorSubtract( end, d3, dst_vert + 8 );
		VectorAdd( end, d3, dst_vert + 12 );
	    
		dst_color = ( uint32 * )tess.colors + numVertices;
		dst_color[0] = *( uint32 * )color;
		dst_color[1] = *( uint32 * )color;
		dst_color[2] = *( uint32 * )color;
		dst_color[3] = *( uint32 * )color;

		dst_tc = tess.tcoords + numVertices;
		dst_tc[0].st[0] = 0; dst_tc[0].st[1] = 0;
		dst_tc[1].st[0] = 1; dst_tc[1].st[1] = 0;
		dst_tc[2].st[0] = 1; dst_tc[2].st[1] = length;
		dst_tc[3].st[0] = 0; dst_tc[3].st[1] = length;

		numVertices += 4;
	}
   
	qglDrawArrays( GL_QUADS, 0, numVertices );
	qglDisableClientState( GL_COLOR_ARRAY );
}

static void GL_DrawWarp( bspSurface_t *surf ) {
	bspPoly_t *poly = surf->polys;
    vec4_t param;

    qglEnable( GL_FRAGMENT_PROGRAM_ARB );
    qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, gl_static.prog_warp );
    param[0] = glr.fd.time * 0.125f;
    param[1] = glr.fd.time * 0.125f;
    param[2] = param[3] = 0;
    qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, param );

    GL_SelectTMU( 1 );
    qglEnable( GL_TEXTURE_2D );
    //GL_TexEnv( GL_MODULATE );
    GL_BindTexture( r_warptexture->texnum );

	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 4*VERTEX_SIZE, poly->vertices + 3 );

    qglDrawArrays( GL_POLYGON, 0, poly->numVerts );

    qglDisable( GL_TEXTURE_2D );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
    GL_SelectTMU( 0 );

    qglDisable( GL_FRAGMENT_PROGRAM_ARB );
}

void GL_DrawSurf( bspSurface_t *surf ) {
	bspTexinfo_t *texinfo = surf->texinfo;
	bspPoly_t *poly = surf->polys;
    
    if( ( texinfo->flags & SURF_SKY ) && !gl_fastsky->integer ) {
        R_AddSkySurface( surf );
        return;
    }
	
	if( texinfo->flags & (SURF_TRANS33|SURF_TRANS66) ) {
        GL_Bits( GLS_BLEND_BLEND | GLS_DEPTHMASK_FALSE );
		if( texinfo->flags & SURF_TRANS33 ) {
			qglColor4f( 1, 1, 1, 0.33f );
		} else {
			qglColor4f( 1, 1, 1, 0.66f );
		}
        GL_TexEnv( GL_MODULATE );
    } else {
        GL_Bits( GLS_DEFAULT );
        GL_TexEnv( GL_REPLACE );
		qglColor4f( 1, 1, 1, 1 );
    }

	GL_BindTexture( texinfo->image->texnum );

	qglVertexPointer( 3, GL_FLOAT, 4*VERTEX_SIZE, poly->vertices );
	qglTexCoordPointer( 2, GL_FLOAT, 4*VERTEX_SIZE, poly->vertices + 3 );
	
    if( surf->type == DSURF_WARP ) {
//        qglDrawArrays( GL_POLYGON, 0, poly->numVerts );
        GL_DrawWarp( surf );
        return;
    }
	
	if( surf->type == DSURF_NOLM ) {
        qglDrawArrays( GL_POLYGON, 0, poly->numVerts );
		return;
	}

    GL_SelectTMU( 1 );
    qglEnable( GL_TEXTURE_2D );
    GL_TexEnv( GL_MODULATE );
    GL_BindTexture( lm.lightmaps[surf->lightmapnum]->texnum );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 4*VERTEX_SIZE, poly->vertices + 5 );

    qglDrawArrays( GL_POLYGON, 0, poly->numVerts );

    qglDisable( GL_TEXTURE_2D );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
    GL_SelectTMU( 0 );
}

