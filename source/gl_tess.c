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

/*
==============================================================================

BSP surfaces sorting

==============================================================================
*/

typedef struct {
    uint32  key;
    bspSurface_t *surf;
} drawSurf_t;

#define MAX_DRAW_SURFS  ( 1 << 16 )
#define DRAW_SURFS_MASK  ( MAX_DRAW_SURFS - 1 )

static drawSurf_t  drawSurfs[MAX_DRAW_SURFS];
static drawSurf_t  drawSurfsBuffer[MAX_DRAW_SURFS];
static int         numDrawSurfs;

image_t *GL_TextureAnimation( bspTexinfo_t *texinfo ) {
    int	count;

	if( !texinfo->animNext ) {
        return texinfo->image;
	}

	count = ( int )( glr.fd.time * 2 ) % texinfo->numFrames;
    while( count ) {
        texinfo = texinfo->animNext;
        count--;
    }

    return texinfo->image;
}


void GL_AddBspSurface( bspSurface_t *surf ) {
    drawSurf_t  *drawsurf;
    bspTexinfo_t *texinfo = surf->texinfo;
    int idx, istrans, texnum;
	image_t *image;

    if( surf->type >= DSURF_NUM_TYPES ) {
        Com_Error( ERR_FATAL, "GL_AddBspSurface: bad surf->type" );
    }

    if( surf->dlightframe != glr.drawframe ) {
        surf->dlightbits = 0;
    }
    
    istrans = 0;
	if( texinfo->flags & SURF_SKY ) {
		if( !gl_fastsky->integer ) {
			R_AddSkySurface( surf );
			return;
		}
		texnum = r_whiteimage->texnum;
	} else {
		if( texinfo->flags & (SURF_TRANS33|SURF_TRANS66) ) {
			if( texinfo->flags & SURF_TRANS33 ) {
				istrans = 1;
			} else {
				istrans = 2;
			}
		}
		image = GL_TextureAnimation( texinfo );
		texnum = image->texnum;
	}

    idx = numDrawSurfs & DRAW_SURFS_MASK;
    drawsurf = &drawSurfs[idx];
    drawsurf->key = ( istrans << 30 ) | ( texnum << 16 ) | surf->lightmapnum;
    drawsurf->surf = surf;
    
    numDrawSurfs++;
}

#define CopyDrawSurf( dst, src ) \
    ( (( uint32 * )dst)[0] = (( uint32 * )src)[0],\
      (( uint32 * )dst)[1] = (( uint32 * )src)[1] )

static void mergeArray( int left, int median, int right ) {
	int count;
	int i, j;
    drawSurf_t *src, *dst;

	dst = drawSurfsBuffer;
	i = left;
	j = median + 1;
	while( i <= median && j <= right ) {
		if( drawSurfs[i].key < drawSurfs[j].key ) {
			CopyDrawSurf( dst, &drawSurfs[i] ); i++;
		} else {
			CopyDrawSurf( dst, &drawSurfs[j] ); j++;
		}
        dst++;
	}
	
	while( i <= median ) {
		CopyDrawSurf( dst, &drawSurfs[i] ); i++; dst++;
	}
	
	while( j <= right ) {
		CopyDrawSurf( dst, &drawSurfs[j] ); j++; dst++;
	}

    src = drawSurfsBuffer;
	count = dst - src;
    dst = drawSurfs + left;
    while( count-- ) {
        CopyDrawSurf( dst, src ); src++; dst++;
    }
}

static void mergeSort_r( int left, int right ) {
	int median;

	if( left < right ) {
		median = ( left + right ) >> 1;
		
		mergeSort_r( left, median );
		mergeSort_r( median + 1, right );

		mergeArray( left, median, right );
	}
}


/*
==============================================================================

Surface tesselation

==============================================================================
*/

#define TURB_SCALE  ( 256.0f / ( 2 * M_PI ) )

static float r_turbsin[256] = {
    #include "warpsin.h"
};

static inline qboolean WouldOverflow( drawSurf_t *surf ) {
	bspPoly_t *poly;
    int numVerts, numIndices;
    
    if( tess.numFaces + 1 > TESS_MAX_FACES ) {
        return qtrue;
    }

    numVerts = tess.numVertices;
    numIndices = tess.numIndices;
    for( poly = surf->surf->polys; poly; poly = poly->next ) {
        numVerts += poly->numVerts;
        numIndices += poly->numIndices;
    }
    
    if( numVerts > TESS_MAX_VERTICES ) {
        return qtrue;
    }
    if( numIndices > TESS_MAX_INDICES ) {
        return qtrue;
    }
    
    return qfalse;
}

static void Tess_SimplePoly( drawSurf_t *surf ) {
    bspSurface_t *bspSurf = surf->surf;
    bspPoly_t *poly = bspSurf->polys;
    vec_t *src_vert, *dst_vert;
    tcoord_t *dst_tc;
    int *dst_indices;
    int i, count, currentVert;
	float scroll;

	scroll = 0;
	if( bspSurf->texinfo->flags & SURF_FLOWING ) {
		scroll = glr.scroll;
	}

    src_vert = poly->vertices;
    dst_vert = tess.vertices + tess.numVertices * 4;
    dst_tc = tess.tcoords + tess.numVertices;
    count = poly->numVerts;
    for( i = 0; i < count; i++ ) {
        VectorCopy( src_vert, dst_vert );
        dst_tc->st[0] = src_vert[3] + scroll;
        dst_tc->st[1] = src_vert[4];
        src_vert += VERTEX_SIZE; dst_vert += 4;
        dst_tc++;
    }

    dst_indices = tess.indices + tess.numIndices;
    count = poly->numVerts - 2;
    currentVert = tess.numVertices;
    for( i = 0; i < count; i++ ) {
        dst_indices[0] = currentVert;
        dst_indices[1] = currentVert + ( i + 1 );
        dst_indices[2] = currentVert + ( i + 2 );
        dst_indices += 3;
    }

    tess.faces[ tess.numFaces++ ] = bspSurf;
    tess.numVertices += poly->numVerts;
    tess.numIndices += poly->numIndices;
    tess.dlightbits |= bspSurf->dlightbits;

	c.trisDrawn += count;

}

static void Tess_LightmappedPoly( drawSurf_t *surf ) {
    bspSurface_t *bspSurf = surf->surf;
    bspPoly_t *poly = bspSurf->polys;
    vec_t *src_vert, *dst_vert;
    tcoord_t *dst_tc;
    tcoord_t *dst_lmtc;
    int *dst_indices;
    int i, count, currentVert;

    src_vert = poly->vertices;
    dst_vert = tess.vertices + tess.numVertices * 4;
    dst_tc = tess.tcoords + tess.numVertices;
    dst_lmtc = tess.lmtcoords + tess.numVertices;
    count = poly->numVerts;
    for( i = 0; i < count; i++ ) {
        VectorCopy( src_vert, dst_vert );
        *( uint32 * )&dst_tc->st[0] = *( uint32 * )&src_vert[3];
        *( uint32 * )&dst_tc->st[1] = *( uint32 * )&src_vert[4];
        *( uint32 * )&dst_lmtc->st[0] = *( uint32 * )&src_vert[5];
        *( uint32 * )&dst_lmtc->st[1] = *( uint32 * )&src_vert[6];
        src_vert += VERTEX_SIZE; dst_vert += 4;
        dst_tc++; dst_lmtc++;
    }

    dst_indices = tess.indices + tess.numIndices;
    count = poly->numVerts - 2;
    currentVert = tess.numVertices;
    for( i = 0; i < count; i++ ) {
        dst_indices[0] = currentVert;
        dst_indices[1] = currentVert + ( i + 1 );
        dst_indices[2] = currentVert + ( i + 2 );
        dst_indices += 3;
    }

    tess.faces[ tess.numFaces++ ] = bspSurf;
    tess.numVertices += poly->numVerts;
    tess.numIndices += poly->numIndices;
    tess.dlightbits |= bspSurf->dlightbits;

	c.trisDrawn += count;

}

#define DIV64	( 1.0f / 64 )

static void Tess_WarpPolys( drawSurf_t *surf ) {
    bspSurface_t *bspSurf = surf->surf;
    bspPoly_t *poly = bspSurf->polys;
    vec_t *src_vert, *dst_vert;
    tcoord_t *dst_tc;
    int *dst_indices;
    int i, j, k, count, currentVert;
    vec_t s, t;
	float scroll;

	scroll = 0;
	if( bspSurf->texinfo->flags & SURF_FLOWING ) {
		scroll = glr.scroll;
	}

    for( poly = bspSurf->polys; poly; poly = poly->next ) {
        src_vert = poly->vertices;
        dst_vert = tess.vertices + tess.numVertices * 4;
        dst_tc = tess.tcoords + tess.numVertices;
        count = poly->numVerts;
        for( i = 0; i < count; i++ ) {
            VectorCopy( src_vert, dst_vert );
            dst_vert += 4;
            
            s = src_vert[3];
            t = src_vert[4];
			src_vert += VERTEX_SIZE;
            
            j = Q_ftol( ( t * 0.125f + glr.fd.time ) * TURB_SCALE );
            k = Q_ftol( ( s * 0.125f + glr.fd.time ) * TURB_SCALE );
            s += r_turbsin[ j & 255 ];
            t += r_turbsin[ k & 255 ];
			
			dst_tc->st[0] = s * DIV64 + scroll;
			dst_tc->st[1] = t * DIV64;
            dst_tc++;
        }

        dst_indices = tess.indices + tess.numIndices;
        count = poly->numVerts - 2;
        currentVert = tess.numVertices;
        for( i = 0; i < count; i++ ) {
            dst_indices[0] = currentVert;
            dst_indices[1] = currentVert + ( i + 1 );
            dst_indices[2] = currentVert + ( i + 2 );
            dst_indices += 3;
        }
        dst_indices[0] = currentVert;
        dst_indices[1] = currentVert + ( i + 1 );
        dst_indices[2] = currentVert + 1;
        dst_indices += 3;
        
        tess.numVertices += poly->numVerts;
        tess.numIndices += poly->numIndices;

		c.trisDrawn += count + 1;
    }

    tess.faces[ tess.numFaces++ ] = bspSurf;
    tess.dlightbits |= bspSurf->dlightbits;

}

typedef void (*tesselatorFunc_t)( drawSurf_t * );

tesselatorFunc_t tessTable[DSURF_NUM_TYPES] = {
    Tess_LightmappedPoly,   /* DSURF_POLY */
    Tess_WarpPolys,         /* DSURF_WARP */
    Tess_SimplePoly		    /* DSURF_NOLM */
};

/*
==============================================================================

Surface drawing

==============================================================================
*/

void Tess_DrawSurfaceTriangles( int *indices, int numIndices ) {
    qglDisable( GL_TEXTURE_2D );
    qglPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
    qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
    GL_TexEnv( GL_REPLACE );
    qglDisable( GL_DEPTH_TEST );
    qglColor4f( 1, 1, 1, 1 );
    
	qglDrawElements( GL_TRIANGLES, numIndices, GL_UNSIGNED_INT,
            indices );

    qglEnable( GL_DEPTH_TEST );
    qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
    qglEnable( GL_TEXTURE_2D );
    qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
}

#if 0
static void ProjectDlightTexture( void ) {
    bspSurface_t *bspSurf;
    bspTexinfo_t *texinfo;
    cplane_t *plane;
    vec3_t point;
    vec2_t local;
    vec_t dist, scale, f;
    int i, j, faceNum, numIndices;
    dlight_t *light;
    tcoord_t *src_tc;
    vec_t *dst_tc;
static    vec_t tcArray[TESS_MAX_VERTICES*2];
static    byte colorsArray[TESS_MAX_VERTICES*4];
    int cfArray[TESS_MAX_VERTICES];
    int idxArray[TESS_MAX_INDICES];
    byte *dst_col;
    int v1, v2, v3;
    int *src_idx, *dst_idx;
    int clipflags;
    color_t color;
    int currentVert, maxVert;

    GL_BindTexture( r_dlightTex->texnum );
//	qglVertexPointer( 3, GL_FLOAT, 16, tess.vertices );
    qglTexCoordPointer( 2, GL_FLOAT, 0, tcArray );
    qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, colorsArray );

    for( i = 0, light = glr.fd.dlights; i < glr.fd.num_dlights; i++, light++ ) {
        currentVert = 0;
        for( faceNum = 0; faceNum < tess.numFaces; faceNum++, currentVert = maxVert ) {
            bspSurf = tess.faces[faceNum];
            maxVert = currentVert + bspSurf->polys->numVerts;
            if( !( bspSurf->dlightbits & ( 1 << i ) ) ) {
                //continue;
            }
            plane = bspSurf->plane;
            switch( plane->type ) {
            case PLANE_X:
				dist = light->transformed[0] - plane->dist;
                break;
            case PLANE_Y:
                dist = light->transformed[1] - plane->dist;
                break;
            case PLANE_Z:
                dist = light->transformed[2] - plane->dist;
                break;
            default:
                dist = DotProduct( light->transformed, plane->normal ) - plane->dist;
                break;
            }

            if( dist > light->intensity || dist < -light->intensity ) {
                //continue;
            }

            VectorMA( light->transformed, -dist, plane->normal, point );
            
            texinfo = bspSurf->texinfo;
            local[0] = DotProduct( point, texinfo->normalizedAxis[0] );
            local[1] = DotProduct( point, texinfo->normalizedAxis[1] );
            
            scale = 1.0f / light->intensity;
            
            dist = fabs( dist );
			f = 1.0f - dist * scale;
            if( f < 0 ) f = 0;

            color[0] = 255 * light->color[0] * f;
            color[1] = 255 * light->color[1] * f;
            color[2] = 255 * light->color[2] * f;
            color[3] = 255;

            src_tc = bspSurf->normalizedTC;
            dst_tc = tcArray + currentVert * 2;
            dst_col = colorsArray + currentVert * 4;
            for( j = currentVert; j < maxVert; j++ ) {
                dst_tc[0] = ( src_tc->st[0] - local[0] ) * scale + 0.5f;
                dst_tc[1] = ( src_tc->st[1] - local[1] ) * scale + 0.5f;
                
                /*clipflags = 0;
                if( dst_tc[0] > 1 ) {
                    clipflags |= 1;
                } else if( dst_tc[0] < 0 ) {
                    clipflags |= 2;
                }
                if( dst_tc[1] > 1 ) {
                    clipflags |= 4;
                } else if( dst_tc[1] < 0 ) {
                    clipflags |= 8;
                }*/

                *( uint32 * )dst_col = *( uint32 * )color;
                //cfArray[j] = clipflags;
                
				dst_col += 4; src_tc++; dst_tc += 2;
            }

        }
            
#if 0
        numIndices = 0;
        src_idx = tess.indices;
        dst_idx = idxArray;
        for( faceNum = 0; faceNum < tess.numFaces; faceNum++ ) {
            bspSurf = tess.faces[faceNum];
            if( !( bspSurf->dlightbits & ( 1 << i ) ) ) {
                src_idx += bspSurf->polys->numIndices;
                continue;
            }
            for( j = 0; j < bspSurf->polys->numVerts - 2; j++ ) {
                v1 = src_idx[0];
                v2 = src_idx[1];
                v3 = src_idx[2];
                src_idx += 3;
                if( cfArray[v1] & cfArray[v2] & cfArray[v3] ) {
                    continue;
                }
                dst_idx[0] = v1;
                dst_idx[1] = v2;
                dst_idx[2] = v3;

                dst_idx += 3;
                numIndices += 3;
            }

        }

        if( numIndices ) {
            qglDrawElements( GL_TRIANGLES, numIndices,
                GL_UNSIGNED_INT, idxArray );    
        }
#endif
    }

//    qglDisableClientState( GL_COLOR_ARRAY );
}

// f = ( l + d ) * t

void EndSurface_Multitextured( void ) {
    GL_TexEnv( GL_REPLACE );
	GL_BindTexture( lm.lightmaps[ tess.lightmapnum - 1 ]->texnum );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.lmtcoords );

    GL_SelectTMU( 1 );
    if( tess.dlightbits ) {
    
    qglEnable( GL_TEXTURE_2D );
    GL_TexEnv( GL_ADD );

	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
     /*   if( gl_dynamic->integer == 2 ) {
			GL_Bits( GLS_BLEND_MODULATE );
        } else {
            GL_Bits( GLS_BLEND_ADD );
        }*/
        
        ProjectDlightTexture();
    }

    /* enable texturing on TMU1 */
    GL_SelectTMU( 2 );
    qglEnable( GL_TEXTURE_2D );
	GL_BindTexture( tess.texnum );
    GL_TexEnv( GL_MODULATE );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.tcoords );

	qglVertexPointer( 3, GL_FLOAT, 16, tess.vertices );
    if( qglLockArraysEXT ) {
    	qglLockArraysEXT( 0, tess.numVertices );
    }
	qglDrawElements( GL_TRIANGLES, tess.numIndices, GL_UNSIGNED_INT,
            tess.indices );
    
    /* disable texturing on TMU1 */
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
    qglDisable( GL_TEXTURE_2D );
    GL_SelectTMU( 1 );

	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
    qglDisableClientState( GL_COLOR_ARRAY );
    qglDisable( GL_TEXTURE_2D );
    GL_SelectTMU( 0 );


    if( gl_showtris->integer ) {
		Tess_DrawSurfaceTriangles( tess.indices, tess.numIndices );
    }

    if( qglUnlockArraysEXT ) {
    	qglUnlockArraysEXT();
    }


}

#else

static void ProjectDlightTexture( void ) {
    bspSurface_t *bspSurf;
    bspTexinfo_t *texinfo;
    cplane_t *plane;
    vec3_t point;
    vec2_t local;
    vec_t dist, scale, f;
    int i, j, faceNum, numIndices;
    dlight_t *light;
    tcoord_t *src_tc;
    vec_t *dst_tc;
    vec_t tcArray[TESS_MAX_VERTICES*2];
    int cfArray[TESS_MAX_VERTICES];
    int idxArray[TESS_MAX_INDICES];
    byte colorsArray[TESS_MAX_VERTICES*4];
    byte *dst_col;
    int v1, v2, v3;
    int *src_idx, *dst_idx;
    int clipflags;
    color_t color;
    int currentVert, maxVert;

    GL_BindTexture( r_dlightTex->texnum );
    qglTexCoordPointer( 2, GL_FLOAT, 0, tcArray );
    qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, colorsArray );

    for( i = 0, light = glr.fd.dlights; i < glr.fd.num_dlights; i++, light++ ) {
        currentVert = 0;
        for( faceNum = 0; faceNum < tess.numFaces; faceNum++, currentVert = maxVert ) {
            bspSurf = tess.faces[faceNum];
            maxVert = currentVert + bspSurf->polys->numVerts;
            if( !( bspSurf->dlightbits & ( 1 << i ) ) ) {
                continue;
            }
            plane = bspSurf->plane;
            switch( plane->type ) {
            case PLANE_X:
				dist = light->transformed[0] - plane->dist;
                break;
            case PLANE_Y:
                dist = light->transformed[1] - plane->dist;
                break;
            case PLANE_Z:
                dist = light->transformed[2] - plane->dist;
                break;
            default:
                dist = DotProduct( light->transformed, plane->normal ) - plane->dist;
                break;
            }

            if( dist > light->intensity || dist < -light->intensity ) {
                continue;
            }

            VectorMA( light->transformed, -dist, plane->normal, point );
            
            texinfo = bspSurf->texinfo;
            local[0] = DotProduct( point, texinfo->normalizedAxis[0] );
            local[1] = DotProduct( point, texinfo->normalizedAxis[1] );
            
            scale = 1.0f / light->intensity;
            
            dist = fabs( dist );
			f = 1.0f - dist * scale;

            color[0] = 255 * light->color[0] * f;
            color[1] = 255 * light->color[1] * f;
            color[2] = 255 * light->color[2] * f;
            color[3] = 255;

            src_tc = bspSurf->normalizedTC;
            dst_tc = tcArray + currentVert * 2;
            dst_col = colorsArray + currentVert * 4;
            for( j = currentVert; j < maxVert; j++ ) {
                dst_tc[0] = ( src_tc->st[0] - local[0] ) * scale + 0.5f;
                dst_tc[1] = ( src_tc->st[1] - local[1] ) * scale + 0.5f;
                
                clipflags = 0;
                if( dst_tc[0] > 1 ) {
                    clipflags |= 1;
                } else if( dst_tc[0] < 0 ) {
                    clipflags |= 2;
                }
                if( dst_tc[1] > 1 ) {
                    clipflags |= 4;
                } else if( dst_tc[1] < 0 ) {
                    clipflags |= 8;
                }

                *( uint32 * )dst_col = *( uint32 * )color;
                cfArray[j] = clipflags;
                
				dst_col += 4; src_tc++; dst_tc += 2;
            }

        }
            
        numIndices = 0;
        src_idx = tess.indices;
        dst_idx = idxArray;
        for( faceNum = 0; faceNum < tess.numFaces; faceNum++ ) {
            bspSurf = tess.faces[faceNum];
            if( !( bspSurf->dlightbits & ( 1 << i ) ) ) {
                src_idx += bspSurf->polys->numIndices;
                continue;
            }
            for( j = 0; j < bspSurf->polys->numVerts - 2; j++ ) {
                v1 = src_idx[0];
                v2 = src_idx[1];
                v3 = src_idx[2];
                src_idx += 3;
                if( cfArray[v1] & cfArray[v2] & cfArray[v3] ) {
                    continue;
                }
                dst_idx[0] = v1;
                dst_idx[1] = v2;
                dst_idx[2] = v3;

                dst_idx += 3;
                numIndices += 3;
            }

        }

        if( numIndices ) {
            qglDrawElements( GL_TRIANGLES, numIndices,
                GL_UNSIGNED_INT, idxArray );    
        }
    }

    qglDisableClientState( GL_COLOR_ARRAY );

}

void EndSurface_Multitextured( void ) {
	GL_BindTexture( tess.texnum );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.tcoords );

    /* enable texturing on TMU1 */
    GL_SelectTMU( 1 );
    qglEnable( GL_TEXTURE_2D );
	GL_BindTexture( lm.lightmaps[ tess.lightmapnum - 1 ]->texnum );
    GL_TexEnv( GL_MODULATE );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.lmtcoords );

	qglVertexPointer( 3, GL_FLOAT, 16, tess.vertices );
    if( qglLockArraysEXT ) {
    	qglLockArraysEXT( 0, tess.numVertices );
    }
	qglDrawElements( GL_TRIANGLES, tess.numIndices, GL_UNSIGNED_INT,
            tess.indices );
    
    /* disable texturing on TMU1 */
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
    qglDisable( GL_TEXTURE_2D );
    GL_SelectTMU( 0 );

    if( gl_showtris->integer ) {
		Tess_DrawSurfaceTriangles( tess.indices, tess.numIndices );
    }

    if( qglUnlockArraysEXT ) {
    	qglUnlockArraysEXT();
    }

    if( tess.dlightbits ) {
        GL_TexEnv( GL_MODULATE );
        if( gl_dynamic->integer == 2 ) {
			GL_Bits( GLS_BLEND_MODULATE );
        } else {
            GL_Bits( GLS_BLEND_ADD );
        }
        
        ProjectDlightTexture();
    }

}

#endif

void EndSurface_Single( void ) {
    GL_BindTexture( tess.texnum );
   
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.tcoords );
	qglVertexPointer( 3, GL_FLOAT, 16, tess.vertices );
    if( qglLockArraysEXT ) {
    	qglLockArraysEXT( 0, tess.numVertices );
    }
	qglDrawElements( GL_TRIANGLES, tess.numIndices, GL_UNSIGNED_INT,
            tess.indices );
    if( gl_showtris->integer ) {
        Tess_DrawSurfaceTriangles( tess.indices, tess.numIndices );
    }
    if( qglUnlockArraysEXT ) {
	    qglUnlockArraysEXT();
    }
}

typedef void (*drawSurfFunc_t)( void );

static drawSurfFunc_t endSurfTable[DSURF_NUM_TYPES] = {
    EndSurface_Multitextured,   /* DSURF_POLY */
    EndSurface_Single,          /* DSURF_WARP */
    EndSurface_Single           /* DSURF_NOLM */
};

static void EndSurface( drawSurf_t *surf ) {
    int istrans = ( surf->key >> 30 ) & 3;

    if( istrans ) {
        GL_Bits( GLS_BLEND_BLEND | GLS_DEPTHMASK_FALSE );
		if( istrans == 1 ) {
			qglColor4f( 1, 1, 1, 0.33f );
		} else {
			qglColor4f( 1, 1, 1, 0.66f );
		}
    } else {
        GL_Bits( GLS_DEFAULT );
        qglColor4f( 1, 1, 1, 1 );
    }

	GL_TexEnv( GL_MODULATE );

    (*endSurfTable[surf->surf->type])();

    tess.numFaces = 0;
	tess.numVertices = 0;
    tess.numIndices = 0;
    tess.dlightbits = 0;
    tess.texnum = 0;
    
    c.batchesDrawn++;
}

static void BeginSurface( drawSurf_t *surf ) {
    if( WouldOverflow( surf ) ) {
        return;
    }

    tess.texnum = ( surf->key >> 16 ) & 1023;
    tess.lightmapnum = surf->key & 0xFFFF;

	(*tessTable[surf->surf->type])( surf );
}

void GL_SortAndDrawSurfs( qboolean doSort ) {
    drawSurf_t *surf, *last;
    int oldkey;

    if( !numDrawSurfs ) {
		return;
	}

    if( numDrawSurfs > MAX_DRAW_SURFS ) {
        Com_DPrintf( "MAX_DRAW_SURFS exceeded\n" );
        numDrawSurfs = MAX_DRAW_SURFS;
    }

	if( doSort && gl_sort->integer ) {
		mergeSort_r( 0, numDrawSurfs - 1 );
	}

    surf = drawSurfs;
	last = drawSurfs + numDrawSurfs;
    oldkey = surf->key;
    BeginSurface( surf );
	surf++;

    for( ; surf != last; surf++ ) {
		if( oldkey == surf->key && !WouldOverflow( surf ) ) {
	        (*tessTable[surf->surf->type])( surf );
			continue;
		}

        EndSurface( surf - 1 );

        oldkey = surf->key;
        BeginSurface( surf );
    }

	EndSurface( surf - 1 );

	numDrawSurfs = 0;
}

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

	GL_TexEnv( GL_MODULATE );
	GL_Bits( bits );
	qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.colors );

    EndSurface_Single();

	qglDisableClientState( GL_COLOR_ARRAY );
    
	tess.numVertices = 0;
    tess.numIndices = 0;
    tess.texnum = 0;
	tess.istrans = 0;
  
}

void GL_StretchPic( float x, float y, float w, float h,
        float s1, float t1, float s2, float t2, const byte *color, image_t *image )
{
    int currentVert, currentIdx;
    vec_t *dst_vert;
    int *dst_idx;
    tcoord_t *dst_tc;
    uint32 *dst_color;
    
    if( tess.numVertices + 4 > TESS_MAX_VERTICES ||
        tess.numIndices + 6 > TESS_MAX_INDICES ||
        ( tess.numVertices && tess.texnum != image->texnum ) )
    {
        GL_Flush2D();
    }

    currentVert = tess.numVertices;
    currentIdx = tess.numIndices;
    tess.numVertices += 4;
    tess.numIndices += 6;
    tess.texnum = image->texnum;

    dst_vert = tess.vertices + currentVert * 4;
    VectorSet( dst_vert +  0, x, y, 0 );
    VectorSet( dst_vert +  4, x + w, y, 0 );
    VectorSet( dst_vert +  8, x + w, y + h, 0 );
    VectorSet( dst_vert + 12, x, y + h, 0 );

    dst_color = ( uint32 * )tess.colors + currentVert;
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

    dst_tc = tess.tcoords + currentVert;
    dst_tc[0].st[0] = s1; dst_tc[0].st[1] = t1;
    dst_tc[1].st[0] = s2; dst_tc[1].st[1] = t1;
    dst_tc[2].st[0] = s2; dst_tc[2].st[1] = t2;
    dst_tc[3].st[0] = s1; dst_tc[3].st[1] = t2;

    dst_idx = tess.indices + currentIdx;
    dst_idx[0] = currentVert;
    dst_idx[1] = currentVert + 1;
    dst_idx[2] = currentVert + 2;
    dst_idx[3] = currentVert;
    dst_idx[4] = currentVert + 2;
    dst_idx[5] = currentVert + 3;

}

void GL_DrawParticles( void ) {
    particle_t *p;
    int i;
    vec3_t transformed;
    vec_t scale;
    color_t color;
    int currentVert;
    vec_t *dst_vert;
    uint32 *dst_color;
    tcoord_t *dst_tc;
    int *dst_idx;

    if( !glr.fd.num_particles ) {
        return;
    }

    GL_TexEnv( GL_MODULATE );
	GL_Bits( GLS_BLEND_BLEND | GLS_DEPTHMASK_FALSE );
    qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.colors );

    tess.texnum = r_particletexture->texnum;
    currentVert = 0;
    for( i = 0, p = glr.fd.particles; i < glr.fd.num_particles; i++, p++ ) {
        VectorSubtract( p->origin, glr.fd.vieworg, transformed );
        scale = DotProduct( transformed, glr.viewaxis[0] );

        if( scale < 20 ) {
            scale = 1.5f;
        } else {
            scale = 1.5f + scale * 0.006f;
        }

        if( p->color == 255 ) {
            *( uint32 * )color = *( uint32 * )p->rgb;
        } else {
            *( uint32 * )color = d_8to24table[p->color & 255];
        }
        color[3] = p->alpha * 255;

        if( currentVert + 3 > TESS_MAX_VERTICES ||
                currentVert + 3 > TESS_MAX_INDICES )
        {
            tess.numVertices = tess.numIndices = currentVert;
            EndSurface_Single();
            currentVert = 0;
        }
        
        dst_vert = tess.vertices + currentVert * 4;
        VectorCopy( p->origin, dst_vert );
        VectorMA( p->origin, scale, glr.viewaxis[2], dst_vert + 4 );
        VectorMA( p->origin, -scale, glr.viewaxis[1], dst_vert + 8 );

        dst_color = ( uint32 * )tess.colors + currentVert;
        dst_color[0] = *( uint32 * )color;
        dst_color[1] = *( uint32 * )color;
        dst_color[2] = *( uint32 * )color;

        dst_tc = tess.tcoords + currentVert;
        dst_tc[0].st[0] = 0.0625f; dst_tc[0].st[1] = 0.0625f;
        dst_tc[1].st[0] = 1.0625f; dst_tc[1].st[1] = 0.0625f;
        dst_tc[2].st[0] = 0.0625f; dst_tc[2].st[1] = 1.0625f;

        dst_idx = tess.indices + currentVert;
        dst_idx[0] = currentVert;
        dst_idx[1] = currentVert + 1;
        dst_idx[2] = currentVert + 2;

        currentVert += 3;
    }

    tess.numVertices = tess.numIndices = currentVert;
    EndSurface_Single();
    tess.numVertices = tess.numIndices = 0;
    tess.texnum = 0;

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
    int *dst_idx;
	vec_t length;
	int currentVert, currentIdx;
	entity_t *ent;
	int i;

    if( !glr.num_beams ) {
        return;
    }

    GL_TexEnv( GL_MODULATE );
	GL_Bits( GLS_BLEND_ADD | GLS_DEPTHMASK_FALSE );
    qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.colors );

	tess.texnum = r_beamtexture->texnum;
	currentVert = 0;
	currentIdx = 0;
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

        if( currentVert + 4 > TESS_MAX_VERTICES ||
            currentIdx + 6 > TESS_MAX_INDICES )
        {
			tess.numVertices = currentVert;
			tess.numIndices = currentIdx;
            EndSurface_Single();
            currentVert = 0;
			currentIdx = 0;
        }

		dst_vert = tess.vertices + currentVert * 4;
		VectorAdd( start, d3, dst_vert );
		VectorSubtract( start, d3, dst_vert + 4 );
		VectorSubtract( end, d3, dst_vert + 8 );
		VectorAdd( end, d3, dst_vert + 12 );
	    
		dst_color = ( uint32 * )tess.colors + currentVert;
		dst_color[0] = *( uint32 * )color;
		dst_color[1] = *( uint32 * )color;
		dst_color[2] = *( uint32 * )color;
		dst_color[3] = *( uint32 * )color;

		dst_tc = tess.tcoords + currentVert;
		dst_tc[0].st[0] = 0; dst_tc[0].st[1] = 0;
		dst_tc[1].st[0] = 1; dst_tc[1].st[1] = 0;
		dst_tc[2].st[0] = 1; dst_tc[2].st[1] = length;
		dst_tc[3].st[0] = 0; dst_tc[3].st[1] = length;

		dst_idx = tess.indices + currentIdx;
		dst_idx[0] = currentVert + 0;
		dst_idx[1] = currentVert + 1;
		dst_idx[2] = currentVert + 2;
		dst_idx[3] = currentVert + 2;
		dst_idx[4] = currentVert + 3;
		dst_idx[5] = currentVert + 0;

		currentVert += 4;
		currentIdx += 6;
	}

	tess.numVertices = currentVert;
	tess.numIndices = currentIdx;
	EndSurface_Single();
	tess.numVertices = tess.numIndices = 0;
	tess.texnum = 0;

	qglDisableClientState( GL_COLOR_ARRAY );

    qglDisableClientState( GL_COLOR_ARRAY );
	
}

static void GL_DrawWarpPolys( bspSurface_t *surf ) {
	bspPoly_t *poly;
	vec_t *vert;
	int i, j, k;
	vec_t s, t;

	for( poly = surf->polys; poly; poly = poly->next ) {
		vert = poly->vertices;
		qglBegin( GL_TRIANGLE_FAN );
		for( i = 0; i < poly->numVerts + 1; i++ ) {
			if( i == poly->numVerts ) {
				vert = poly->vertices + VERTEX_SIZE;
			}

			j = Q_ftol( ( vert[4] * 0.125 + glr.fd.time ) * TURB_SCALE );
			k = Q_ftol( ( vert[3] * 0.125 + glr.fd.time ) * TURB_SCALE );
			
			s = vert[3] + r_turbsin[j & 255];
			s *= DIV64;

			t = vert[4] + r_turbsin[k & 255];
			t *= DIV64;

			qglTexCoord2f( s, t );
			qglVertex3fv( vert );

			vert += VERTEX_SIZE;
		}
		qglEnd();
	}
}

static void GL_DrawNolmPoly( bspSurface_t *surf ) {
	bspPoly_t *poly = surf->polys;
	vec_t *vert;
	int i;

    qglBegin( GL_POLYGON );
    vert = poly->vertices;
    for( i = 0; i < poly->numVerts; i++ ) {
        qglTexCoord2fv( vert + 3 );
        qglVertex3fv( vert );
        vert += VERTEX_SIZE;      
    }
    qglEnd();
}


void GL_DrawSurfPoly( bspSurface_t *surf ) {
	bspTexinfo_t *texinfo = surf->texinfo;
	bspPoly_t *poly;
    vec_t *vert;
    int i;
    
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
    } else {
		qglColor4f( 1, 1, 1, 1 );
        GL_Bits( GLS_DEFAULT );
        qglColor4ubv( colorWhite );
    }
    GL_TexEnv( GL_MODULATE );

	GL_BindTexture( texinfo->image->texnum );

	if( surf->type == DSURF_WARP ) {
		GL_DrawWarpPolys( surf );
		return;
	}
	
	if( surf->type == DSURF_NOLM ) {
		GL_DrawNolmPoly( surf );
		return;
	}

    GL_SelectTMU( 1 );
    qglEnable( GL_TEXTURE_2D );
    GL_BindTexture( lm.lightmaps[ surf->lightmapnum - 1 ]->texnum );
    GL_TexEnv( GL_MODULATE );
    
	poly = surf->polys;
    vert = poly->vertices;
	qglBegin( GL_POLYGON );
    for( i = 0; i < poly->numVerts; i++ ) {
		qglMultiTexCoord2fvARB( GL_TEXTURE0_ARB, vert + 3 );
        qglMultiTexCoord2fvARB( GL_TEXTURE1_ARB, vert + 5 );
        qglVertex3fv( vert );
        vert += VERTEX_SIZE;
        
    }
    qglEnd();

    qglDisable( GL_TEXTURE_2D );
    GL_SelectTMU( 0 );
}

