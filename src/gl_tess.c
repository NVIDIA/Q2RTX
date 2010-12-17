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

#define FACE_HASH_BITS  5
#define FACE_HASH_SIZE  (1<<FACE_HASH_BITS)
#define FACE_HASH_MASK  (FACE_HASH_SIZE-1)

// assumes a < 1024 (texture), b > 1024 (lightmap)
#define FACE_HASH(a,b) \
    (((a)^((a)>>FACE_HASH_BITS)^((b)>>FACE_HASH_BITS*2))&FACE_HASH_MASK)

static mface_t *faces_alpha, *faces_warp, *faces_alpha_warp;
static mface_t *faces_hash[FACE_HASH_SIZE];

void GL_Flush2D( void ) {
    glStateBits_t bits;

    if( !tess.numverts ) {
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
    qglTexCoordPointer( 2, GL_FLOAT, 16, tess.vertices + 2 );
    qglVertexPointer( 2, GL_FLOAT, 16, tess.vertices );

    if( qglLockArraysEXT ) {
        qglLockArraysEXT( 0, tess.numverts );
    }
   
    qglDrawArrays( GL_QUADS, 0, tess.numverts );

    if( gl_showtris->integer ) {
        GL_EnableOutlines();
        qglDrawArrays( GL_QUADS, 0, tess.numverts );
        GL_DisableOutlines();
    }

    if( qglUnlockArraysEXT ) {
        qglUnlockArraysEXT();
    }

    qglDisableClientState( GL_COLOR_ARRAY );

    c.batchesDrawn2D++;

    tess.numverts = 0;
    tess.texnum[0] = 0;
    tess.flags = 0;
}

void GL_DrawParticles( void ) {
    particle_t *p;
    int i;
    vec3_t transformed;
    vec_t scale, dist;
    color_t color;
    int numverts;
    vec_t *dst_vert;
    uint32_t *dst_color;

    if( !glr.fd.num_particles ) {
        return;
    }

    GL_BindTexture( TEXNUM_PARTICLE );
    GL_TexEnv( GL_MODULATE );
    GL_Bits( GLS_BLEND_BLEND | GLS_DEPTHMASK_FALSE );

    qglEnableClientState( GL_COLOR_ARRAY );
    qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.colors );
    qglTexCoordPointer( 2, GL_FLOAT, 20, tess.vertices + 3 );
    qglVertexPointer( 3, GL_FLOAT, 20, tess.vertices );

    numverts = 0;
    for( i = 0, p = glr.fd.particles; i < glr.fd.num_particles; i++, p++ ) {
        VectorSubtract( p->origin, glr.fd.vieworg, transformed );
        dist = DotProduct( transformed, glr.viewaxis[0] );

        scale = gl_partscale->value;
        if( dist > 20 ) {
            scale += dist * 0.01f;
        }

        if( p->color == 255 ) {
            FastColorCopy( p->rgb, color );
        } else {
            *( uint32_t * )color = d_8to24table[p->color & 255];
        }
        color[3] = p->alpha * 255;

        if( numverts + 3 > TESS_MAX_VERTICES ) {
            qglDrawArrays( GL_TRIANGLES, 0, numverts );
            numverts = 0;
        }
        
        dst_vert = tess.vertices + numverts * 5;
        VectorMA( p->origin, scale*0.25f, glr.viewaxis[1], dst_vert );
        VectorMA( dst_vert, -scale*0.25f, glr.viewaxis[2], dst_vert );
        VectorMA( dst_vert, scale, glr.viewaxis[2], dst_vert + 5 );
        VectorMA( dst_vert, -scale, glr.viewaxis[1], dst_vert + 10 );

        dst_vert[ 3] = 0;     dst_vert[ 4] = 0;
        dst_vert[ 8] = 1.75f; dst_vert[ 9] = 0;
        dst_vert[13] = 0;     dst_vert[14] = 1.75f;

        dst_color = ( uint32_t * )tess.colors + numverts;
        dst_color[0] = *( uint32_t * )color;
        dst_color[1] = *( uint32_t * )color;
        dst_color[2] = *( uint32_t * )color;

        numverts += 3;
    }

    qglDrawArrays( GL_TRIANGLES, 0, numverts );
    qglDisableClientState( GL_COLOR_ARRAY );

    if( gl_showtris->integer ) {
        GL_EnableOutlines();
        qglDrawArrays( GL_TRIANGLES, 0, numverts );
        GL_DisableOutlines();
    }
}

/* all things serve the Beam */
void GL_DrawBeams( void ) {
    vec3_t d1, d2, d3;
    vec_t *start, *end;
    color_t color;
    vec_t *dst_vert;
    uint32_t *dst_color;
    vec_t length;
    int numverts;
    entity_t *ent;
    int i;

    if( !glr.num_beams ) {
        return;
    }

    GL_BindTexture( TEXNUM_BEAM );
    GL_TexEnv( GL_MODULATE );
    GL_Bits( GLS_BLEND_BLEND | GLS_DEPTHMASK_FALSE );
    qglEnableClientState( GL_COLOR_ARRAY );
    qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.colors );
    qglTexCoordPointer( 2, GL_FLOAT, 20, tess.vertices + 3 );
    qglVertexPointer( 3, GL_FLOAT, 20, tess.vertices );

    numverts = 0;
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
            *( uint32_t * )color = *( uint32_t * )&ent->skinnum;
        } else {
            *( uint32_t * )color = d_8to24table[ent->skinnum & 0xFF];
        }
        color[3] = 255 * ent->alpha;

        if( numverts + 4 > TESS_MAX_VERTICES ) {
            qglDrawArrays( GL_QUADS, 0, numverts );
            numverts = 0;
        }

        dst_vert = tess.vertices + numverts * 5;
        VectorAdd( start, d3, dst_vert );
        VectorSubtract( start, d3, dst_vert + 5 );
        VectorSubtract( end, d3, dst_vert + 10 );
        VectorAdd( end, d3, dst_vert + 15 );

        dst_vert[3] = 0; dst_vert[4] = 0;
        dst_vert[8] = 1; dst_vert[9] = 0;
        dst_vert[13] = 1; dst_vert[14] = length;
        dst_vert[18] = 0; dst_vert[19] = length;
        
        dst_color = ( uint32_t * )tess.colors + numverts;
        dst_color[0] = *( uint32_t * )color;
        dst_color[1] = *( uint32_t * )color;
        dst_color[2] = *( uint32_t * )color;
        dst_color[3] = *( uint32_t * )color;

        numverts += 4;
    }
   
    qglDrawArrays( GL_QUADS, 0, numverts );
    qglDisableClientState( GL_COLOR_ARRAY );
}

static void GL_BindArrays( void ) {
    vec_t *ptr;

    if( gl_static.world.vertices ) {
        ptr = tess.vertices;
    } else {
        ptr = NULL;
        qglBindBufferARB( GL_ARRAY_BUFFER_ARB, gl_static.world.bufnum );
    }

    qglVertexPointer( 3, GL_FLOAT, 4*VERTEX_SIZE, ptr + 0 );
    qglTexCoordPointer( 2, GL_FLOAT, 4*VERTEX_SIZE,
        gl_lightmap->integer ? ptr + 5 : ptr + 3 );

    qglClientActiveTextureARB( GL_TEXTURE1_ARB );
    qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
    qglTexCoordPointer( 2, GL_FLOAT, 4*VERTEX_SIZE, ptr + 5 );
    qglClientActiveTextureARB( GL_TEXTURE0_ARB );
}

static void GL_UnbindArrays( void ) {
    if( !gl_static.world.vertices ) {
        qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
    }
    qglClientActiveTextureARB( GL_TEXTURE1_ARB );
    qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
    qglClientActiveTextureARB( GL_TEXTURE0_ARB );
}

static void GL_Flush3D( void ) {
    if( !tess.numindices ) {
        return;
    }

    if( tess.flags & (SURF_TRANS33|SURF_TRANS66|SURF_WARP) ) {
        float f = gl_static.inverse_intensity;

        if( tess.flags & SURF_TRANS33 ) {
            qglColor4f( f, f, f, 0.33f );
        } else if( tess.flags & SURF_TRANS66 ) {
            qglColor4f( f, f, f, 0.66f );
        } else {
            qglColor4f( f, f, f, 1.0f );
        }

        GL_TexEnv( GL_MODULATE );
    } else {
        GL_TexEnv( GL_REPLACE );
    }

    GL_BindTexture( tess.texnum[0] );

    if( tess.flags & SURF_FLOWING ) {
        float scaled, scroll;

        if( tess.flags & SURF_WARP ) {
            scaled = glr.fd.time * 0.5f;
            scroll = -scaled;
        } else {
            scaled = glr.fd.time / 40;
            scroll = -64 * ( scaled - (int)scaled );
        }

        qglMatrixMode( GL_TEXTURE );
        qglPushMatrix();
        qglTranslatef( scroll, 0, 0 );
    }

    if( tess.texnum[1] ) {
        GL_SelectTMU( 1 );
        qglEnable( GL_TEXTURE_2D );
        GL_TexEnv( GL_MODULATE );
        GL_BindTexture( tess.texnum[1] );
    }

    if( gl_static.world.vertices && qglLockArraysEXT ) {
        qglLockArraysEXT( 0, tess.numverts );
    }

    qglDrawElements( GL_TRIANGLES, tess.numindices, GL_UNSIGNED_INT, tess.indices );

    if( tess.texnum[1] ) {
        qglDisable( GL_TEXTURE_2D );
        GL_SelectTMU( 0 );
    }

    if( gl_showtris->integer ) {
        GL_EnableOutlines();
        qglDrawElements( GL_TRIANGLES, tess.numindices, GL_UNSIGNED_INT, tess.indices );
        GL_DisableOutlines();
    }

    if( gl_static.world.vertices && qglUnlockArraysEXT ) {
        qglUnlockArraysEXT();
    }

    if( tess.flags & SURF_FLOWING ) {
        qglPopMatrix();
        qglMatrixMode( GL_MODELVIEW );
    }

    c.batchesDrawn++;

    tess.texnum[0] = tess.texnum[1] = 0;
    tess.numindices = 0;
    tess.numverts = 0;
    tess.flags = 0;
}

static int GL_CopyVerts( mface_t *surf ) {
    void *src, *dst;
    int firstvert;

    if( tess.numverts + surf->numsurfedges > TESS_MAX_VERTICES ) {
        GL_Flush3D();
    }

    src = gl_static.world.vertices + surf->firstvert * VERTEX_SIZE;
    dst = tess.vertices + tess.numverts * VERTEX_SIZE;
    memcpy( dst, src, surf->numsurfedges * VERTEX_SIZE * sizeof( vec_t ) );

    firstvert = tess.numverts;
    tess.numverts += surf->numsurfedges;
    return firstvert;
}

static int GL_TextureAnimation( mtexinfo_t *tex ) {
    int     frame, c;

    if( !tex->next )
        return tex->image->texnum;

    frame = ( int )( glr.fd.time * 2 );
    c = frame % tex->numframes;
    while( c ) {
        tex = tex->next;
        c--;
    }

    return tex->image->texnum;
}

#define SURF_FLUSH_MASK \
    (SURF_TRANS33|SURF_TRANS66|SURF_WARP|SURF_FLOWING)

static void GL_DrawFace( mface_t *surf ) {
    int numtris = surf->numsurfedges - 2;
    int numindices = numtris * 3;
    int diff = surf->texinfo->c.flags ^ tess.flags;
    int texnum = GL_TextureAnimation( surf->texinfo );
    int *dst_indices;
    int i, j;

    if( tess.texnum[0] != texnum ||
        tess.texnum[1] != surf->texnum[1] ||
        ( diff & SURF_FLUSH_MASK ) ||
        tess.numindices + numindices > TESS_MAX_INDICES )
    {
        GL_Flush3D();
    }

    if( gl_static.world.vertices ) {
        j = GL_CopyVerts( surf );
    } else {
        j = surf->firstvert;
    }

    if( gl_lightmap->integer ) {
        tess.texnum[0] = surf->texnum[1] ? surf->texnum[1] : texnum;
        tess.texnum[1] = 0;
    } else {
        tess.texnum[0] = texnum;
        tess.texnum[1] = surf->texnum[1];
    }

    tess.flags = surf->texinfo->c.flags;

    dst_indices = tess.indices + tess.numindices;
    for( i = 0; i < numtris; i++ ) {
        dst_indices[0] = j;
        dst_indices[1] = j + ( i + 1 );
        dst_indices[2] = j + ( i + 2 );
        dst_indices += 3;
    }
    tess.numindices += numindices;

    c.trisDrawn += numtris;
}

static inline void GL_DrawChain( mface_t **head ) {
    mface_t *face;

    for( face = *head; face; face = face->next ) {
        GL_DrawFace( face ); 
    }
    *head = NULL;
}

void GL_DrawSolidFaces( void ) {
    int i;

    GL_BindArrays();

    GL_Bits( GLS_DEFAULT );

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

void GL_AddSolidFace( mface_t *face ) {
    if( ( face->texinfo->c.flags & SURF_WARP ) && gl_static.prognum_warp ) {
        face->next = faces_warp;
        faces_warp = face;
    } else {
        unsigned i = FACE_HASH( face->texnum[0], face->texnum[1] );
        face->next = faces_hash[i];
        faces_hash[i] = face;
    }

    c.facesDrawn++;
}

void GL_AddFace( mface_t *face ) {
    int flags = face->texinfo->c.flags;

    if( flags & SURF_SKY ) {
        R_AddSkySurface( face );
        return;
    }

    if( flags & (SURF_TRANS33|SURF_TRANS66) ) {
        if( ( flags & SURF_WARP ) && gl_static.prognum_warp ) {
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

