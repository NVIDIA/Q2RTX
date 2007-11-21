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

/*
 * gl_surf.c -- surface post-processing code
 * 
 */
#include "gl_local.h"

lightmapBuilder_t lm;

static cvar_t   *gl_coloredlightmaps;
static cvar_t   *gl_brightness;

cvar_t *gl_modulate_hack;

/*
=============================================================================

LIGHTMAPS BUILDING

=============================================================================
*/

static float colorScale, brightness;

static qboolean LM_AllocBlock( int w, int h, int *s, int *t ) {
    int i, j;
    int x, y, maxInuse, minInuse;

	x = 0; y = LM_BLOCK_HEIGHT;
    minInuse = LM_BLOCK_HEIGHT;
    for( i = 0; i < LM_BLOCK_WIDTH - w; i++ ) {
        maxInuse = 0;
        for( j = 0; j < w; j++ ) {
            if( lm.inuse[ i + j ] >= minInuse ) {
                break;
            }
            if( maxInuse < lm.inuse[ i + j ] ) {
                maxInuse = lm.inuse[ i + j ];
            }
        }
        if( j == w ) {
            x = i;
            y = minInuse = maxInuse;
        }
    }

    if( y + h > LM_BLOCK_HEIGHT ) {
        return qfalse;
    }
    
    for( i = 0; i < w; i++ ) {
        lm.inuse[ x + i ] = y + h;
    }

    *s = x;
    *t = y;
    return qtrue;
}

static void LM_InitBlock( void ) {
    int i;
    
    for( i = 0; i < LM_BLOCK_WIDTH; i++ ) {
        lm.inuse[i] = 0;
    }
}

static void LM_UploadBlock( void ) {
    int comp = colorScale ? GL_RGB : GL_LUMINANCE;

    qglBindTexture( GL_TEXTURE_2D, LM_TEXNUM + lm.numMaps );
	qglTexImage2D( GL_TEXTURE_2D, 0, comp, LM_BLOCK_WIDTH, LM_BLOCK_HEIGHT, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, lm.buffer );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

    if( lm.highWater < ++lm.numMaps ) {
        lm.highWater = lm.numMaps;
    }
}

static void LM_BuildSurfaceLightmap( bspSurface_t *surf, vec_t *vbo ) {
    byte *ptr, *dst, *src;
    int i, j;
    int smax, tmax, s, t;
    
    smax = ( surf->extents[0] >> 4 ) + 1;
    tmax = ( surf->extents[1] >> 4 ) + 1;

    if( !LM_AllocBlock( smax, tmax, &s, &t ) ) {
        LM_UploadBlock();
        if( lm.numMaps == LM_MAX_LIGHTMAPS ) {
            Com_Error( ERR_DROP, "%s: LM_MAX_LIGHTMAPS exceeded\n", __func__ );
        }
        LM_InitBlock();
        if( !LM_AllocBlock( smax, tmax, &s, &t ) ) {
            Com_Error( ERR_DROP, "%s: LM_AllocBlock( %d, %d ) failed\n",
                __func__, smax, tmax );
        }
    }
    
    src = surf->lightmap;
    dst = &lm.buffer[ ( t * LM_BLOCK_WIDTH + s ) << 2 ];

    for( i = 0; i < tmax; i++ ) {
        ptr = dst;
        for( j = 0; j < smax; j++ ) {
			float r, g, b, min, max, mid;

			r = src[0];
			g = src[1];
			b = src[2];

			if( colorScale != 1.0f ) {
			min = max = r;
				if ( g < min ) min = g;
				if ( b < min ) min = b;
				if ( g > max ) max = g;
				if ( b > max ) max = b;
				mid = 0.5 * ( min + max );
				r = mid + ( r - mid ) * colorScale;
				g = mid + ( g - mid ) * colorScale;
				b = mid + ( b - mid ) * colorScale;
			}

			if( !gl_modulate_hack->integer ) {
				r *= gl_modulate->value;
				g *= gl_modulate->value;
				b *= gl_modulate->value;
			} 

			max = g;
			if( r > max ) {
				max = r;
			}
			if( b > max ) {
				max = b;
			}

			if( max > 255 ) {
				r *= 255.0f / max;
				g *= 255.0f / max;
				b *= 255.0f / max;
			}

			//atu brightness adjustments
			brightness = 255.0f * gl_brightness->value;
			r += brightness;
			g += brightness;
			b += brightness;
			if ( r > 255.0f ) r = 255.0f;
			else if ( r < 0.0f ) r = 0.0f;
			if ( g > 255.0f ) g = 255.0f;
			else if ( g < 0.0f ) g = 0.0f;
			if ( b > 255.0f ) b = 255.0f;
			else if ( b < 0.0f ) b = 0.0f;

			src[0] = r;
			src[1] = g;
			src[2] = b;

			ptr[0] = src[0];
            ptr[1] = src[1];
            ptr[2] = src[2];
            ptr[3] = 255;

            src += 3; ptr += 4;
        }
        dst += LM_BLOCK_WIDTH * 4;
    }
    
    surf->texnum[1] = LM_TEXNUM + lm.numMaps;

    s = ( s << 4 ) + 8;
    t = ( t << 4 ) + 8;

    for( i = 0; i < surf->numVerts; i++ ) {
        vbo[5] += s - surf->texturemins[0]; 
        vbo[6] += t - surf->texturemins[1];
        vbo[5] /= LM_BLOCK_WIDTH * 16;
        vbo[6] /= LM_BLOCK_HEIGHT * 16;
        vbo += VERTEX_SIZE;
    }
}

/*
=============================================================================

POLYGONS BUILDING

=============================================================================
*/

static void GL_BuildSurfacePoly( bspSurface_t *surf, vec_t *vbo ) {
	int *src_surfedge;
	dvertex_t *src_vert;
	dedge_t *src_edge;
	bspTexinfo_t *texinfo = surf->texinfo;
	int index, vertIndex;
    int i, numEdges = surf->numSurfEdges;
    vec2_t scale, tc, mins, maxs;
    int bmins[2], bmaxs[2];
	
	surf->numVerts = numEdges;
	surf->numIndices = ( numEdges - 2 ) * 3;
    surf->texnum[0] = texinfo->image->texnum;
    surf->texflags = texinfo->flags;

    if( texinfo->flags & SURF_WARP ) {
        if( qglProgramStringARB ) {
            surf->type = DSURF_WARP;
            surf->texnum[1] = r_warptexture->texnum;
        } else {
		    surf->type = DSURF_NOLM;
        }
    } else if( !surf->lightmap || gl_fullbright->integer || ( texinfo->flags & NOLIGHT_MASK ) ) {
		surf->type = DSURF_NOLM;
	} else {
		surf->type = DSURF_POLY;
	}

    // normalize texture coordinates
    scale[0] = 1.0f / texinfo->image->width;
    scale[1] = 1.0f / texinfo->image->height;
    if( surf->type == DSURF_WARP ) {
        scale[0] *= 0.5f;
        scale[1] *= 0.5f;
    }

	mins[0] = mins[1] = 99999;
	maxs[0] = maxs[1] = -99999;

	src_surfedge = surf->firstSurfEdge;
	for( i = 0; i < numEdges; i++ ) {
		index = *src_surfedge++;
		
		vertIndex = 0;
		if( index < 0 ) {
			index = -index;
			vertIndex = 1;
		}

		if( index >= r_world.numEdges ) {
            Com_Error( ERR_DROP, "%s: bad edge index", __func__ );
		}

		src_edge = r_world.edges + index;
		src_vert = r_world.vertices + src_edge->v[vertIndex];

        // vertex coordinates
		VectorCopy( src_vert->point, vbo );
		
		// texture0 coordinates
		tc[0] = DotProduct( vbo, texinfo->axis[0] ) + texinfo->offset[0];
		tc[1] = DotProduct( vbo, texinfo->axis[1] ) + texinfo->offset[1];

		if( mins[0] > tc[0] ) mins[0] = tc[0];
		if( maxs[0] < tc[0] ) maxs[0] = tc[0];
		
		if( mins[1] > tc[1] ) mins[1] = tc[1];
		if( maxs[1] < tc[1] ) maxs[1] = tc[1];

        vbo[3] = tc[0] * scale[0];
        vbo[4] = tc[1] * scale[1];

        // texture1 coordinates
        if( surf->type == DSURF_WARP ) {
            vbo[5] = vbo[3];
            vbo[6] = vbo[4];
        } else {
            vbo[5] = tc[0];
            vbo[6] = tc[1];
        }

		vbo += VERTEX_SIZE;
	}

    // calculate surface extents
    bmins[0] = floor( mins[0] / 16 );
    bmins[1] = floor( mins[1] / 16 );
    bmaxs[0] = ceil( maxs[0] / 16 );
    bmaxs[1] = ceil( maxs[1] / 16 );

	surf->texturemins[0] = bmins[0] << 4;
	surf->texturemins[1] = bmins[1] << 4;

	surf->extents[0] = ( bmaxs[0] - bmins[0] ) << 4;
	surf->extents[1] = ( bmaxs[1] - bmins[1] ) << 4;
}

static void GL_BuildSkyPoly( bspSurface_t *surf ) {
	int *src_surfedge;
	dvertex_t *src_vert;
	dedge_t *src_edge;
	bspTexinfo_t *texinfo = surf->texinfo;
	int index, vertIndex;
    int i, numEdges = surf->numSurfEdges;
    vec_t *dst_vert;
	
	surf->numVerts = numEdges;
	surf->numIndices = ( numEdges - 2 ) * 3;
    surf->texnum[0] = texinfo->image->texnum;
    surf->texflags = texinfo->flags;
	surf->type = DSURF_SKY;

    surf->vertices = sys.HunkAlloc( &r_world.pool,
        numEdges * 3 * sizeof( vec_t ) );

	src_surfedge = surf->firstSurfEdge;
    dst_vert = surf->vertices;
	for( i = 0; i < numEdges; i++ ) {
		index = *src_surfedge++;
		
		vertIndex = 0;
		if( index < 0 ) {
			index = -index;
			vertIndex = 1;
		}

		if( index >= r_world.numEdges ) {
            Com_Error( ERR_DROP, "%s: bad edge index", __func__ );
		}

		src_edge = r_world.edges + index;
		src_vert = r_world.vertices + src_edge->v[vertIndex];

		VectorCopy( src_vert->point, dst_vert );

		dst_vert += 3;
	}
}

void GL_BeginPostProcessing( void ) {
    lm.numMaps = 0;
    LM_InitBlock();

	gl_coloredlightmaps = cvar.Get( "gl_coloredlightmaps", "1", CVAR_ARCHIVE|CVAR_LATCHED );
	gl_brightness = cvar.Get( "gl_brightness", "0", CVAR_ARCHIVE|CVAR_LATCHED );
	gl_modulate_hack = cvar.Get( "gl_modulate_hack", "0", CVAR_LATCHED );

	if( gl_coloredlightmaps->value < 0 ) {
		cvar.Set( "gl_coloredlightmaps", "0" );
	} else if( gl_coloredlightmaps->value > 1 ) {
		cvar.Set( "gl_coloredlightmaps", "1" );
	}

	if( gl_brightness->value < -1 ) {
		cvar.Set( "gl_brightness", "-1" );
	} else if( gl_brightness->value > 1 ) {
		cvar.Set( "gl_brightness", "1" );
	}

	brightness = gl_brightness->value;
	colorScale = gl_coloredlightmaps->value;
}
    
void GL_EndPostProcessing( void ) {
    bspSurface_t *surf;
    int i, size, count;
    vec_t *vbo = NULL;
   
    // calculate vertex buffer size in bytes
    count = 0;
    for( i = 0, surf = r_world.surfaces; i < r_world.numSurfaces; i++, surf++ ) {
        count += surf->numSurfEdges;
    }
    size = count * VERTEX_SIZE * 4;

    if( qglBindBufferARB ) {
        qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 1 );
        
        qglBufferDataARB( GL_ARRAY_BUFFER_ARB, size, NULL, GL_STATIC_DRAW_ARB );

        GL_ShowErrors( __func__ );

        vbo = qglMapBufferARB( GL_ARRAY_BUFFER_ARB, GL_READ_WRITE_ARB );
        qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
        if( vbo ) {
            gl_static.vbo = NULL;
            Com_DPrintf( "%d bytes of vertex data as VBO\n", size );
        } else {
            Com_EPrintf( "Failed to map VBO in client memory" );
        }
    }
    
    if( !vbo ) {
        vbo = sys.HunkAlloc( &r_world.pool, size );
        gl_static.vbo = vbo;
        Com_DPrintf( "%d bytes of vertex data on hunk\n", size );
    }

    // post process all surfaces
    count = 0;
    for( i = 0, surf = r_world.surfaces; i < r_world.numSurfaces; i++, surf++ ) {
        if( ( surf->texinfo->flags & SURF_SKY ) && !gl_fastsky->integer ) {
            GL_BuildSkyPoly( surf );
            continue;
        }
        surf->firstVert = count;
        GL_BuildSurfacePoly( surf, vbo );

        if( surf->type == DSURF_POLY ) {
            LM_BuildSurfaceLightmap( surf, vbo );
        }

        count += surf->numVerts;
        vbo += surf->numVerts * VERTEX_SIZE;
    }
    
    if( qglBindBufferARB ) {
        qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 1 );
        qglUnmapBufferARB( GL_ARRAY_BUFFER_ARB );
        qglBindBufferARB( GL_ARRAY_BUFFER_ARB, 0 );
    }

    // upload the last lightmap
    for( i = 0; i < LM_BLOCK_WIDTH; i++ ) {
        if( lm.inuse[i] ) {
            LM_UploadBlock();
            break;
        }
    }

    Com_DPrintf( "%d lightmaps built\n", lm.numMaps );
}

