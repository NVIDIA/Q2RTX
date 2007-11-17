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

vec3_t modelViewOrigin; /* viewer origin in model space */

#define FACE_HASH_SIZE  32
#define FACE_HASH_MASK  ( FACE_HASH_SIZE - 1 )

static vec3_t lightcolor;
static bspSurface_t *alphaFaces;
//static bspSurface_t *warpFaces;
static bspSurface_t *faces_hash[FACE_HASH_SIZE];

static qboolean GL_LightPoint_r( bspNode_t *node, vec3_t start, vec3_t end ) {
	vec_t startFrac, endFrac, midFrac;
	vec3_t mid;
	int side;
	qboolean ret;
	bspSurface_t *surf;
	bspTexinfo_t *texinfo;
	int i, pitch;
	int s, t;
	byte *b1, *b2, *b3, *b4;
	int fracu, fracv;
	int w1, w2, w3, w4;
	int color[3];

	if( !node->plane ) {
		return qfalse;
	}
	
	/* calculate distancies */
	startFrac = PlaneDiffFast( start, node->plane );
	endFrac = PlaneDiffFast( end, node->plane );
	side = ( startFrac < 0 );

	if( ( endFrac < 0 ) == side ) {
		/* both points are one the same side */
		return GL_LightPoint_r( node->children[side], start, end );
	}

	/* find crossing point */
	midFrac = startFrac / ( startFrac - endFrac );
    LerpVector( start, end, midFrac, mid );

	/* check near side */
	ret = GL_LightPoint_r( node->children[side], start, mid );
	if( ret ) {
		return ret;
	}

	surf = node->firstFace;
	for( i = 0; i < node->numFaces; i++, surf++ ) {
		texinfo = surf->texinfo;
		if( texinfo->flags & (SURF_WARP|SURF_SKY) ) {
			continue;
		}
		if( !surf->lightmap ) {
			continue;
		}
		s = DotProduct( texinfo->axis[0], mid ) + texinfo->offset[0];
		t = DotProduct( texinfo->axis[1], mid ) + texinfo->offset[1];

		s -= surf->texturemins[0];
		t -= surf->texturemins[1];
		if( s < 0 || t < 0 ) {
			continue;
		}
		if( s > surf->extents[0] || t > surf->extents[1] ) {
			continue;
		}

		fracu = s & 15;
		fracv = t & 15;

		s >>= 4;
		t >>= 4;

		pitch = ( surf->extents[0] >> 4 ) + 1;
		b1 = &surf->lightmap[3 * ( ( t + 0 ) * pitch + ( s + 0 ) )];
		b2 = &surf->lightmap[3 * ( ( t + 0 ) * pitch + ( s + 1 ) )];
		b3 = &surf->lightmap[3 * ( ( t + 1 ) * pitch + ( s + 1 ) )];
		b4 = &surf->lightmap[3 * ( ( t + 1 ) * pitch + ( s + 0 ) )];
		
		w1 = ( 16 - fracu ) * ( 16 - fracv );
		w2 = fracu * ( 16 - fracv );
		w3 = fracu * fracv;
		w4 = ( 16 - fracu ) * fracv;

		color[0] = ( w1 * b1[0] + w2 * b2[0] + w3 * b3[0] + w4 * b4[0] ) >> 8;
		color[1] = ( w1 * b1[1] + w2 * b2[1] + w3 * b3[1] + w4 * b4[1] ) >> 8;
		color[2] = ( w1 * b1[2] + w2 * b2[2] + w3 * b3[2] + w4 * b4[2] ) >> 8;

		VectorMA( lightcolor, 1.0f / 255, color, lightcolor );

		return qtrue;
	}

	/* check far side */
	return GL_LightPoint_r( node->children[side^1], mid, end );
}

void GL_LightPoint( vec3_t origin, vec3_t dest ) {
    extern cvar_t *gl_modulate_hack;
	vec3_t point;
#if USE_DYNAMIC
	dlight_t *light;
	vec3_t dir;
	vec_t dist, f;
#endif
    int i;

	if( !r_world.name[0] || gl_fullbright->integer ) {
		VectorSet( dest, 1, 1, 1 );
		return;
	}

	point[0] = origin[0];
	point[1] = origin[1];
	point[2] = origin[2] - 8192;

   	VectorClear( lightcolor );
    if( !r_world.lightmap || !GL_LightPoint_r( r_world.nodes, origin, point ) ) { 
        VectorSet( lightcolor, 1, 1, 1 );
    }

	if( gl_modulate_hack && gl_modulate_hack->integer ) {
		VectorScale( lightcolor, gl_modulate->value, lightcolor );
	}

#if USE_DYNAMIC
    for( i = 0, light = glr.fd.dlights; i < glr.fd.num_dlights; i++, light++ ) {
		VectorSubtract( light->origin, origin, dir );
		dist = VectorLength( dir );
		if( dist > light->intensity ) {
			continue;
		}
        f = 1.0f - dist / light->intensity;
		VectorMA( lightcolor, f, light->color, lightcolor );
	}
#endif

	/* apply modulate twice to mimic original ref_gl behavior */
	VectorScale( lightcolor, gl_modulate->value, lightcolor );

	for( i = 0; i < 3; i++ ) {
		if( lightcolor[i] > 1 ) {
			lightcolor[i] = 1;
		} else if( lightcolor[i] < 0 ) {
			lightcolor[i] = 0;
		}
	}

	VectorCopy( lightcolor, dest );

}

#if USE_DYNAMIC
static void GL_MarkLights_r( bspNode_t *node, dlight_t *light ) {
    vec_t dot;
    int count;
    bspSurface_t *face;
    int lightbit = 1 << light->index;
    
    while( node->plane ) {
        dot = PlaneDiffFast( light->transformed, node->plane );
        if( dot > light->intensity ) {
            node = node->children[0];
            continue;
        }
        if( dot < -light->intensity ) {
            node = node->children[1];
            continue;
        }

        face = node->firstFace;
        count = node->numFaces;
        while( count-- ) {
            if( !( face->texinfo->flags & NOLIGHT_MASK ) ) {
                if( face->dlightframe != glr.drawframe ) {
                    face->dlightframe = glr.drawframe;
                    face->dlightbits = 0;
                }
            
                face->dlightbits |= lightbit;
            }
            face++;
        }
        
        GL_MarkLights_r( node->children[0], light );

        node = node->children[1];
    }
}

void GL_MarkLights( void ) {
    int i;
    dlight_t *light;

    for( i = 0, light = glr.fd.dlights; i < glr.fd.num_dlights; i++, light++ ) {
		light->index = i;
		VectorCopy( light->origin, light->transformed );
        GL_MarkLights_r( r_world.nodes, light );
    }
}

static void GL_TransformLights( bspSubmodel_t *model ) {
    int i;
    dlight_t *light;
	vec3_t temp;

	if( !model->headnode ) {
        // this happens on some maps
        // could lead to a crash of the original ref_gl
		//Com_WPrintf( "GL_TransformLights: NULL headnode\n" );
		return;
	}
	
    for( i = 0, light = glr.fd.dlights; i < glr.fd.num_dlights; i++, light++ ) {
		light->index = i;
		VectorSubtract( light->origin, glr.ent->origin, temp );
		light->transformed[0] = DotProduct( temp, glr.entaxis[0] );
		light->transformed[1] = DotProduct( temp, glr.entaxis[1] );
		light->transformed[2] = DotProduct( temp, glr.entaxis[2] );

		GL_MarkLights_r( model->headnode, light );

    }
}
#endif

void GL_MarkLeaves( void ) {
	byte fatvis[MAX_MAP_LEAFS/8];
	bspLeaf_t *leaf, *lastleaf;
	bspNode_t *node, *lastnode;
	byte *vis1, *vis2;
	uint32 *dst, *src1, *src2;
	int cluster1, cluster2, longs;
	vec3_t tmp;
    static int lastNodesVisible;

	leaf = Bsp_FindLeaf( glr.fd.vieworg );
	cluster1 = cluster2 = leaf->cluster;
	VectorCopy( glr.fd.vieworg, tmp );
	if( !leaf->contents ) {
		tmp[2] -= 16;	
	} else {
		tmp[2] += 16;	
	}
	leaf = Bsp_FindLeaf( tmp );
	if( !( leaf->contents & CONTENTS_SOLID ) ) {
		cluster2 = leaf->cluster;
	}
    
	if( cluster1 == glr.viewcluster1 && cluster2 == glr.viewcluster2 ) {
		goto finish;
	}
		
	if( gl_lockpvs->integer ) {
        goto finish;
    }

	vis1 = vis2 = Bsp_ClusterPVS( cluster1 );
	if( cluster1 != cluster2 ) {
		vis2 = Bsp_ClusterPVS( cluster2 );
		if( !vis1 ) {
			vis1 = vis2;
		} else if( !vis2 ) {
			vis2 = vis1;
		}
	}
	glr.visframe++;
    lastNodesVisible = 0;
    glr.viewcluster1 = cluster1;
	glr.viewcluster2 = cluster2;

    if( !vis1 || gl_novis->integer ) {
        /* mark everything visible */
		lastleaf = r_world.leafs + r_world.numLeafs;
	    for( leaf = r_world.leafs; leaf != lastleaf; leaf++ ) {
            leaf->visframe = glr.visframe;
        }
		lastnode = r_world.nodes + r_world.numNodes;
	    for( node = r_world.nodes; node != lastnode; node++ ) {
            node->visframe = glr.visframe;
        }
		lastNodesVisible = r_world.numNodes;
        goto finish;
    }

	if( vis1 != vis2 ) {
		longs = ( r_world.numClusters + 31 ) >> 5;
		src1 = ( uint32 * )vis1;
		src2 = ( uint32 * )vis2;
		dst = ( uint32 * )fatvis;
		while( longs-- ) {
			*dst++ = *src1++ | *src2++;
		}
		vis1 = fatvis;
	}
	
	lastleaf = r_world.leafs + r_world.numLeafs;
	for( leaf = r_world.leafs; leaf != lastleaf; leaf++ ) {
		cluster1 = leaf->cluster;
		if( cluster1 == -1 ) {
			continue;
		}
		if( Q_IsBitSet( vis1, cluster1 ) ) {
			node = ( bspNode_t * )leaf;

            /* mark parent nodes visible */
			do {
				if( node->visframe == glr.visframe ) {
					break;
				}
				node->visframe = glr.visframe;
				node = node->parent;
				lastNodesVisible++;
			} while( node );
		}
	}
    
finish:
    c.nodesVisible = lastNodesVisible;

}

static inline void GL_AddSurf( bspSurface_t *face ) {
    if( face->texinfo->flags & (SURF_TRANS33|SURF_TRANS66) ) {
        face->next = alphaFaces;
        alphaFaces = face;
    } /*else if( face->type == SURF_WARP ) {
        face->next = warpFaces;
        warpFaces = face;
    } */else {
#if 0
        GL_DrawSurf( face );
#else
        int i = ( face->texinfo->image->texnum ^ face->lightmapnum ) & FACE_HASH_MASK;
        face->next = faces_hash[i];
        faces_hash[i] = face;
#endif
    }
    c.facesDrawn++;
}

#define NODE_CLIPPED    0
#define NODE_UNCLIPPED  15

static inline qboolean GL_ClipNodeToFrustum( bspNode_t *node, int *clipflags ) {
    int flags = *clipflags;
    int i, bits, mask;
    
    for( i = 0, mask = 1; i < 4; i++, mask <<= 1 ) {
        if( flags & mask ) {
            continue;
        }
        bits = BoxOnPlaneSide( node->mins, node->maxs,
            &glr.frustumPlanes[i] );
        if( bits == BOX_BEHIND ) {
            return qfalse;
        }
        if( bits == BOX_INFRONT ) {
            flags |= mask;
        }
    }

    *clipflags = flags;

    return qtrue;
}


#define BACKFACE_EPSILON	0.001f

#define SIDE_FRONT	0
#define SIDE_BACK	1

void GL_DrawBspModel( bspSubmodel_t *model ) {
	bspSurface_t *face;
	int count;
	vec3_t bounds[2];
	vec_t dot;
	vec3_t temp;
	entity_t *ent = glr.ent;
	glCullResult_t cull;
	
	if( glr.entrotated ) {
		cull = GL_CullSphere( ent->origin, model->radius );
		if( cull == CULL_OUT ) {
			c.spheresCulled++;
			return;
		}
		if( cull == CULL_CLIP ) {
			VectorCopy( model->mins, bounds[0] );
			VectorCopy( model->maxs, bounds[1] );
			cull = GL_CullLocalBox( ent->origin, bounds );
			if( cull == CULL_OUT ) {
				c.rotatedBoxesCulled++;
				return;
			}
		}
		VectorSubtract( glr.fd.vieworg, ent->origin, temp );
		modelViewOrigin[0] = DotProduct( temp, glr.entaxis[0] );
		modelViewOrigin[1] = DotProduct( temp, glr.entaxis[1] );
		modelViewOrigin[2] = DotProduct( temp, glr.entaxis[2] );
	} else {
		VectorAdd( model->mins, ent->origin, bounds[0] );
		VectorAdd( model->maxs, ent->origin, bounds[1] );
		cull = GL_CullBox( bounds );
		if( cull == CULL_OUT ) {
			c.boxesCulled++;
			return;
		}
		VectorSubtract( glr.fd.vieworg, ent->origin, modelViewOrigin );
	}

	glr.drawframe++;

#if USE_DYNAMIC
	if( gl_dynamic->integer ) {
		GL_TransformLights( model );
	}
#endif

	qglPushMatrix();
	qglTranslatef( ent->origin[0], ent->origin[1], ent->origin[2] );
	if( glr.entrotated ) {
		qglRotatef( ent->angles[YAW],   0, 0, 1 );
		qglRotatef( ent->angles[PITCH], 0, 1, 0 );
		qglRotatef( ent->angles[ROLL],  1, 0, 0 );
	}

	/* draw visible faces */
	/* FIXME: go by headnode instead? */
	face = model->firstFace;
    count = model->numFaces;
	while( count-- ) {
		dot = PlaneDiffFast( modelViewOrigin, face->plane );
		if( ( dot < -BACKFACE_EPSILON && face->side == SIDE_FRONT ) ||
			( dot > BACKFACE_EPSILON && face->side == SIDE_BACK ) )
		{
			c.facesCulled++;
		} else {
            /* FIXME: warp/trans surfaces are not supported */
            GL_DrawSurf( face );  
		}
		face++;
	}

	qglPopMatrix();
}

static void GL_WorldNode_r( bspNode_t *node, int clipflags ) {
	bspLeaf_t *leaf;
	bspSurface_t **leafFace, *face;
	int count, side, area;
    vec_t dot;
	
    while( node->visframe == glr.visframe ) {
        if( gl_cull_nodes->integer && clipflags != NODE_UNCLIPPED &&
            GL_ClipNodeToFrustum( node, &clipflags ) == qfalse )
        {
            c.nodesCulled++;
            break;
        }

        if( !node->plane ) {
            /* found a leaf */
            leaf = ( bspLeaf_t * )node;
            if( leaf->contents == CONTENTS_SOLID ) {
                break;
            }
            area = leaf->area;
            if( !glr.fd.areabits || Q_IsBitSet( glr.fd.areabits, area ) ) {
                leafFace = leaf->firstLeafFace;
                count = leaf->numLeafFaces;
                while( count-- ) {
                    face = *leafFace++;
                    face->drawframe = glr.drawframe;
                }
            }
            break;
        }

        dot = PlaneDiffFast( modelViewOrigin, node->plane );
        side = ( dot < 0 );

        GL_WorldNode_r( node->children[side], clipflags );

        face = node->firstFace;
        count = node->numFaces;
        while( count-- ) {
            if( face->drawframe == glr.drawframe ) {
                if( face->side == side ) {
                    GL_AddSurf( face );
                } else {
                    c.facesCulled++;
                }
            }
            face++;
        }

        c.nodesDrawn++;

        node = node->children[ side ^ 1 ];
    }

}

void GL_DrawWorld( void ) {	
    int i;
    bspSurface_t *face;

    GL_MarkLeaves();

#if USE_DYNAMIC
    if( gl_dynamic->integer ) {
        GL_MarkLights();
    }
#endif
    
    R_ClearSkyBox();

	VectorCopy( glr.fd.vieworg, modelViewOrigin );

    GL_WorldNode_r( r_world.nodes, NODE_CLIPPED );

    for( i = 0; i < FACE_HASH_SIZE; i++ ) {
        for( face = faces_hash[i]; face; face = face->next ) {
            GL_DrawSurf( face ); 
        }
        faces_hash[i] = NULL;
    }

    if( !gl_fastsky->integer ) {
        R_DrawSkyBox();
    }
}

void GL_DrawAlphaFaces( void ) {
	bspSurface_t *face, *next;

    if( ( face = alphaFaces ) == NULL ) {
        return;
    }

    do {
        GL_DrawSurf( face ); 
        next = face->next;
        face->next = NULL;
        face = next;
    } while( face );

    alphaFaces = NULL;
}

