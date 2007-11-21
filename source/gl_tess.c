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

#define FACE_HASH_SIZE  32
#define FACE_HASH_MASK  ( FACE_HASH_SIZE - 1 )

static bspSurface_t *faces_alpha, *faces_warp, *faces_alpha_warp;
static bspSurface_t *faces_hash[FACE_HASH_SIZE];

void GL_Flush2D( void ) {
	glStateBits_t bits;

    if( !tess.numVertices ) {
        return;
    }

	bits = GLS_DEPTHTEST_DISABLE;
    if( tess.flags & 2 ) {
        bits |= GLS_BLEND_BLEND;
    } else if( tess.flags & 1 ) {
        bits |= GLS_ALPHATEST_ENABLE;
	}

    GL_BindTexture( tess.texnum[0] );
	GL_TexEnv( GL_MODULATE );
	GL_Bits( bits );

	qglEnableClientState( GL_COLOR_ARRAY );

	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.colors );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.tcoords );
	qglVertexPointer( 3, GL_FLOAT, 16, tess.vertices );
   
	qglDrawArrays( GL_QUADS, 0, tess.numVertices );

	qglDisableClientState( GL_COLOR_ARRAY );
    
	tess.numVertices = 0;
    tess.texnum[0] = 0;
	tess.flags = 0;
}

void GL_StretchPic( float x, float y, float w, float h,
        float s1, float t1, float s2, float t2,
        const byte *color, image_t *image )
{
    vec_t *dst_vert;
    tcoord_t *dst_tc;
    uint32 *dst_color;
    
    if( tess.numVertices + 4 > TESS_MAX_VERTICES ||
        ( tess.numVertices && tess.texnum[0] != image->texnum ) )
    {
        GL_Flush2D();
    }

    tess.texnum[0] = image->texnum;

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
    		tess.flags |= 1;
        } else {
    		tess.flags |= 2;
        }
	}
	if( color[3] != 255 ) {
		tess.flags |= 2;
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

static void GL_BindArrays( void ) {
    vec_t *vbo = gl_static.vbo;

    if( !vbo && qglBindBufferARB ) {
        qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 1 );
    }

	qglVertexPointer( 3, GL_FLOAT, 4*VERTEX_SIZE, vbo + 0 );
	qglTexCoordPointer( 2, GL_FLOAT, 4*VERTEX_SIZE, vbo + 3 );

    qglClientActiveTextureARB( GL_TEXTURE1_ARB );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 4*VERTEX_SIZE, vbo + 5 );
    qglClientActiveTextureARB( GL_TEXTURE0_ARB );
}

static void GL_UnbindArrays( void ) {
    if( qglBindBufferARB ) {
        qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
    }
    qglClientActiveTextureARB( GL_TEXTURE1_ARB );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
    qglClientActiveTextureARB( GL_TEXTURE0_ARB );
}

static void GL_Flush3D( void ) {
    if( !tess.numIndices ) {
        return;
    }

    if( tess.flags & SURF_TRANS33 ) {
        qglColor4f( 1, 1, 1, 0.33f );
    } else if( tess.flags & SURF_TRANS66 ) {
        qglColor4f( 1, 1, 1, 0.66f );
    }

	GL_BindTexture( tess.texnum[0] );

    if( tess.texnum[1] ) {
        GL_SelectTMU( 1 );
        qglEnable( GL_TEXTURE_2D );
        GL_TexEnv( GL_MODULATE );
        GL_BindTexture( tess.texnum[1] );
    }

    qglDrawElements( GL_TRIANGLES, tess.numIndices, GL_UNSIGNED_INT, tess.indices );

    if( tess.texnum[1] ) {
        qglDisable( GL_TEXTURE_2D );
        GL_SelectTMU( 0 );
    }

    c.batchesDrawn++;

    tess.texnum[0] = tess.texnum[1] = 0;
    tess.numIndices = 0;
    tess.flags = 0;
}

static void GL_DrawFace( bspSurface_t *surf ) {
    int *dst_indices;
    int i, j = surf->firstVert;

    if( tess.texnum[0] != surf->texnum[0] ||
        tess.texnum[1] != surf->texnum[1] ||
        ( ( surf->texflags ^ tess.flags ) & ( SURF_TRANS33 | SURF_TRANS66 ) ) ||
        tess.numIndices + surf->numIndices > TESS_MAX_INDICES )
    {
        GL_Flush3D();
    }

    tess.texnum[0] = surf->texnum[0];
    tess.texnum[1] = surf->texnum[1];
    tess.flags = surf->texflags;
    dst_indices = tess.indices + tess.numIndices;
    for( i = 0; i < surf->numVerts - 2; i++ ) {
        dst_indices[0] = j;
        dst_indices[1] = j + ( i + 1 );
        dst_indices[2] = j + ( i + 2 );
        dst_indices += 3;
    }
    tess.numIndices += surf->numIndices;
    c.trisDrawn += surf->numVerts - 2;
}

static inline void GL_DrawChain( bspSurface_t **head ) {
    bspSurface_t *face;

    for( face = *head; face; face = face->next ) {
        GL_DrawFace( face ); 
    }
    *head = NULL;
}

void GL_DrawSolidFaces( void ) {
    int i;

    GL_BindArrays();

    GL_Bits( GLS_DEFAULT );
    GL_TexEnv( GL_REPLACE );
    qglColor4f( 1, 1, 1, 1 );

    if( faces_warp ) {
        GL_EnableWarp();
        GL_DrawChain( &faces_warp );
        GL_Flush3D();
        GL_DisableWarp();
    }

    for( i = 0; i < FACE_HASH_SIZE; i++ ) {
        GL_DrawChain( &faces_hash[i] );
    }

    GL_Flush3D();
    GL_UnbindArrays();
}


void GL_DrawAlphaFaces( void ) {
    GL_BindArrays();

    GL_Bits( GLS_BLEND_BLEND | GLS_DEPTHMASK_FALSE );
    GL_TexEnv( GL_MODULATE );

    if( faces_alpha_warp ) {
        GL_EnableWarp();
        GL_DrawChain( &faces_alpha_warp );
        GL_Flush3D();
        GL_DisableWarp();
    }

    GL_DrawChain( &faces_alpha );

    GL_Flush3D();
    GL_UnbindArrays();
}

void GL_AddSolidFace( bspSurface_t *face ) {
    if( face->type == DSURF_WARP ) {
        face->next = faces_warp;
        faces_warp = face;
    } else {
        int i = ( face->texnum[0] ^ face->texnum[1] ) & FACE_HASH_MASK;
        face->next = faces_hash[i];
        faces_hash[i] = face;
    }

    c.facesDrawn++;
}

void GL_AddFace( bspSurface_t *face ) {
    if( face->type == DSURF_SKY ) {
        R_AddSkySurface( face );
        return;
    }

    if( face->texflags & (SURF_TRANS33|SURF_TRANS66) ) {
        if( face->type == DSURF_WARP ) {
            face->next = faces_alpha_warp;
            faces_alpha_warp = face;
        } else {
            face->next = faces_alpha;
            faces_alpha = face;
        }
        c.facesDrawn++;
        return;
    }

    GL_AddSolidFace( face );
}

