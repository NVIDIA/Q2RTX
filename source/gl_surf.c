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

static cvar_t *gl_coloredlightmaps;
static cvar_t *gl_brightness;

cvar_t *gl_modulate_hack;

/*
=============================================================================

LIGHTMAP TEXTURES BUILDING

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
    /* lightmap images would be automatically freed
	 * by R_FreeUnusedImages on next level load */
    GL_SelectTMU( 1 );
    lm.lightmaps[lm.numMaps] = R_CreateImage( va( "*lightmap%d", lm.numMaps ),
            lm.buffer, LM_BLOCK_WIDTH, LM_BLOCK_HEIGHT,
                it_lightmap, if_auto );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    lm.numMaps++;
    GL_SelectTMU( 0 );
}

static int LM_BuildSurfaceLightmap( bspSurface_t *surf ) {
    byte *ptr, *dst, *src;
    int i, j;
    bspPoly_t *poly;
    vec_t *vert;
    int smax, tmax, s, t;
    
    smax = ( surf->extents[0] >> 4 ) + 1;
    tmax = ( surf->extents[1] >> 4 ) + 1;

    if( !LM_AllocBlock( smax, tmax, &s, &t ) ) {
        LM_UploadBlock();
        if( lm.numMaps == LM_MAX_LIGHTMAPS ) {
            Com_EPrintf( "LM_MAX_LIGHTMAPS exceeded\n" );
            return -1;
        }
        LM_InitBlock();
        if( !LM_AllocBlock( smax, tmax, &s, &t ) ) {
            Com_EPrintf( "LM_AllocBlock( %d, %d ) failed\n", smax, tmax );
            return -1;
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
    
    surf->lightmapnum = lm.numMaps + 1;

    s = ( s << 4 ) + 8;
    t = ( t << 4 ) + 8;

    poly = surf->polys;
    vert = poly->vertices;
    for( i = 0; i < poly->numVerts; i++ ) {
        vert[5] = vert[3] - surf->texturemins[0] + s;
        vert[6] = vert[4] - surf->texturemins[1] + t;
        vert[5] /= LM_BLOCK_WIDTH * 16;
        vert[6] /= LM_BLOCK_HEIGHT * 16;
        vert += VERTEX_SIZE;
    }

    return 0;
}

/*
=============================================================================

POLYGONS BUILDING

=============================================================================
*/

static void GL_CalcSurfaceExtents( bspSurface_t *surf ) {
	vec2_t mins, maxs;
    int bmins[2], bmaxs[2];
	int i;
    bspPoly_t *poly = surf->polys;
    vec_t *vert;

	mins[0] = mins[1] = 99999;
	maxs[0] = maxs[1] = -99999;

    vert = poly->vertices;
	for( i = 0; i < poly->numVerts; i++ ) {
		if( mins[0] > vert[3] ) mins[0] = vert[3];
		if( maxs[0] < vert[3] ) maxs[0] = vert[3];
		
		if( mins[1] > vert[4] ) mins[1] = vert[4];
		if( maxs[1] < vert[4] ) maxs[1] = vert[4];

		vert += VERTEX_SIZE;
	}

    bmins[0] = floor( mins[0] / 16 );
    bmins[1] = floor( mins[1] / 16 );
    bmaxs[0] = ceil( maxs[0] / 16 );
    bmaxs[1] = ceil( maxs[1] / 16 );

	surf->texturemins[0] = bmins[0] << 4;
	surf->texturemins[1] = bmins[1] << 4;

	surf->extents[0] = ( bmaxs[0] - bmins[0] ) << 4;
	surf->extents[1] = ( bmaxs[1] - bmins[1] ) << 4;
    
}

static int GL_BuildSurfacePoly( bspSurface_t *surf ) {
	int *src_surfedge;
	dvertex_t *src_vert;
	dedge_t *src_edge;
	vec_t *dst_vert;
	bspTexinfo_t *texinfo;
	bspPoly_t *poly;
	int i;
	int index, vertIndex;
    int numEdges = surf->numSurfEdges;
	
	poly = sys.HunkAlloc( &r_world.pool, sizeof( *poly ) +
            sizeof( vec_t ) * VERTEX_SIZE * ( numEdges - 1 ) );
	poly->next = NULL;
	poly->numVerts = numEdges;
	poly->numIndices = ( numEdges - 2 ) * 3;
    surf->polys = poly;

	texinfo = surf->texinfo;
	if( !surf->lightmap || ( texinfo->flags & NOLIGHT_MASK ) ||
            gl_fullbright->integer )
    {
		surf->type = DSURF_NOLM;
	} else {
		surf->type = DSURF_POLY;
	}
    
	src_surfedge = surf->firstSurfEdge;
	dst_vert = poly->vertices;
	for( i = 0; i < numEdges; i++ ) {
		index = *src_surfedge++;
		
		vertIndex = 0;
		if( index < 0 ) {
			index = -index;
			vertIndex = 1;
		}

		if( index >= r_world.numEdges ) {
			printf( "LoadFace: bad edge index" );
			return -1;
		}

		src_edge = r_world.edges + index;
		src_vert = r_world.vertices + src_edge->v[vertIndex];

		VectorCopy( src_vert->point, dst_vert );
		
		/* texture coordinates */
		dst_vert[3] = DotProduct( dst_vert, texinfo->axis[0] ) +
            texinfo->offset[0];
		dst_vert[4] = DotProduct( dst_vert, texinfo->axis[1] ) +
            texinfo->offset[1];

        /* lightmap coordinates */
        dst_vert[5] = 0;
        dst_vert[6] = 0;

		dst_vert += VERTEX_SIZE;
	}

    return 0;

}

#define SUBDIVIDE_SIZE  64
#define SUBDIVIDE_VERTS 64

static bspSurface_t *warpsurf;

static void BoundPolygon( int numVerts, const vec_t *verts,
        vec3_t mins, vec3_t maxs )
{
    ClearBounds( mins, maxs );

    while( numVerts-- ) {
        AddPointToBounds( verts, mins, maxs );
        verts += 3;
    }
}


static void SubdividePolygon_r( int numVerts, vec_t *verts ) {
    int i, j;
    vec3_t front[SUBDIVIDE_VERTS];
    vec3_t back[SUBDIVIDE_VERTS];
    vec_t dist[SUBDIVIDE_VERTS];
    vec3_t mins, maxs;
    int f, b;
    vec_t mid, frac, scale, *v;
    bspPoly_t *poly;
    vec3_t total;
    vec_t total_s, total_t;
    bspTexinfo_t *texinfo;
    
    if( numVerts > SUBDIVIDE_VERTS - 4 ) {
        Com_Error( ERR_DROP, "SubdividePolygon_r: numVerts = %d", numVerts );
    }

    BoundPolygon( numVerts, verts, mins, maxs );

    for( i = 0; i < 3; i++ ) {
        mid = ( mins[i] + maxs[i] ) * 0.5f;
        mid = SUBDIVIDE_SIZE * floor( mid / SUBDIVIDE_SIZE + 0.5f );
        if( mid - mins[i] < 8 ) {
            continue;
        }
        if( maxs[i] - mid < 8 ) {
            continue;
        }

        v = verts + i;
        for( j = 0; j < numVerts; j++, v += 3 ) {
            dist[j] = *v - mid;
        }

        dist[j] = dist[0];
        VectorCopy( verts, v - i );

        f = b = 0;
        v = verts;
        for( j = 0; j < numVerts; j++, v += 3 ) {
            if( dist[j] >= 0 ) {
                VectorCopy( v, front[f] ); f++;
            }
            if( dist[j] <= 0 ) {
                VectorCopy( v, back[b] ); b++;
            }
            if( dist[j] == 0 || dist[j+1] == 0 ) {
                continue;
            }
            if( ( dist[j] > 0 ) != ( dist[j+1] > 0 ) ) {
                frac = dist[j] / ( dist[j] - dist[j+1] );
                front[f][0] = back[b][0] = v[0] + frac * ( v[3+0] - v[0] );
                front[f][1] = back[b][1] = v[1] + frac * ( v[3+1] - v[1] );
                front[f][2] = back[b][2] = v[2] + frac * ( v[3+2] - v[2] );
                f++; b++;
            }
        }

        SubdividePolygon_r( f, front[0] );
        SubdividePolygon_r( b, back[0] );
        return;
    }

    poly = sys.HunkAlloc( &r_world.pool, sizeof( *poly ) +
            sizeof( vec_t ) * VERTEX_SIZE * numVerts );
    poly->next = warpsurf->polys;
    poly->numVerts = numVerts + 1;
    poly->numIndices = numVerts * 3;
    warpsurf->polys = poly;
    
    texinfo = warpsurf->texinfo;
    VectorClear( total );
    total_s = total_t = 0;
    v = poly->vertices + VERTEX_SIZE;
    for( i = 0; i < numVerts; i++ ) {
        VectorCopy( verts, v );
        v[3] = DotProduct( verts, texinfo->axis[0] );
        v[4] = DotProduct( verts, texinfo->axis[1] );
        total_s += v[3];
        total_t += v[4];
        VectorAdd( total, verts, total );
        verts += 3; v += VERTEX_SIZE;
    }

    /* middle point */
    v = poly->vertices;
    scale = 1.0f / numVerts;
    VectorScale( total, scale, v );
    v[3] = total_s * scale;
    v[4] = total_t * scale;

}

static int GL_BuildSurfaceWarpPolys( bspSurface_t *surf ) {
	int *src_surfedge;
	dvertex_t *src_vert;
	dedge_t *src_edge;
	vec_t *dst_vert;
	int i;
	int index, vertIndex;
    int numEdges = surf->numSurfEdges;
    vec3_t verts[SUBDIVIDE_VERTS];
	
    surf->polys = NULL;
    surf->type = DSURF_WARP;
    warpsurf = surf;
    
	src_surfedge = surf->firstSurfEdge;
    dst_vert = verts[0];
	for( i = 0; i < numEdges; i++ ) {
		index = *src_surfedge++;
		
		vertIndex = 0;
		if( index < 0 ) {
			index = -index;
			vertIndex = 1;
		}

		if( index >= r_world.numEdges ) {
			printf( "LoadFace: bad edge index" );
			return -1;
		}

		src_edge = r_world.edges + index;
		src_vert = r_world.vertices + src_edge->v[vertIndex];

		VectorCopy( src_vert->point, dst_vert );
		
		dst_vert += 3;
	}

    SubdividePolygon_r( numEdges, verts[0] );

    return 0;

}

static void GL_NormalizeSurfaceTexcoords( bspSurface_t *surf ) {
    bspTexinfo_t *texinfo = surf->texinfo;
    bspPoly_t *poly = surf->polys;
    vec_t *vert;
    tcoord_t *tc;
    int i;
    vec2_t scale;

    surf->normalizedTC = sys.HunkAlloc( &r_world.pool, sizeof( tcoord_t ) * poly->numVerts );

    scale[0] = 1.0f / texinfo->image->width;
    scale[1] = 1.0f / texinfo->image->height;

    tc = surf->normalizedTC;
    vert = poly->vertices;
    for( i = 0; i < poly->numVerts; i++ ) {
        vert[3] *= scale[0];
        vert[4] *= scale[1];
        tc->st[0] = DotProduct( vert, texinfo->normalizedAxis[0] );
        tc->st[1] = DotProduct( vert, texinfo->normalizedAxis[1] );
        
        vert += VERTEX_SIZE; tc++;
    }
}

int GL_PostProcessSurface( bspSurface_t *surf ) {
	bspTexinfo_t *texinfo = surf->texinfo;
    
    if( texinfo->flags & SURF_SKY ) {
    }
    if( ( texinfo->flags & SURF_WARP ) && gl_subdivide->integer ) {
        if( GL_BuildSurfaceWarpPolys( surf ) ) {
            return -1;
        }
    } else {
        if( GL_BuildSurfacePoly( surf ) ) {
            return -1;
        }
        GL_CalcSurfaceExtents( surf );
    }

    if( surf->type == DSURF_POLY ) {
        if( LM_BuildSurfaceLightmap( surf ) ) {
            return -1;
        }
    }
    
    if( !( texinfo->flags & (SURF_SKY|SURF_WARP) ) ) {
        GL_NormalizeSurfaceTexcoords( surf );
    }
    
    if( ( texinfo->flags & SURF_WARP ) && !gl_subdivide->integer ) {
        GL_NormalizeSurfaceTexcoords( surf );
    }

    return 0;
}

void GL_BeginPostProcessing( void ) {
    lm.numMaps = 0;
    LM_InitBlock();

	gl_coloredlightmaps = cvar.Get( "gl_coloredlightmaps", "1",
        CVAR_ARCHIVE|CVAR_LATCHED );
	gl_brightness = cvar.Get( "gl_brightness", "0",
        CVAR_ARCHIVE|CVAR_LATCHED );
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
    int i;
    
    for( i = 0; i < LM_BLOCK_WIDTH; i++ ) {
        if( lm.inuse[i] ) {
            LM_UploadBlock();
            break;
        }
    }

    Com_DPrintf( "GL_EndPostProcessing: %d lightmaps built\n", lm.numMaps );
}


