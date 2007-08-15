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

static vec3_t lightcolor;

static qboolean GL_LightPoint_r( bspNode_t *node, vec3_t start, vec3_t end ) {
	cplane_t *plane;
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

	if( !( plane = node->plane ) ) {
		return qfalse;
	}
	
	/* calculate distancies */
	startFrac = DotProduct( plane->normal, start ) - plane->dist;
	endFrac = DotProduct( plane->normal, end ) - plane->dist;
	side = ( startFrac < 0 );

	if( ( endFrac < 0 ) == side ) {
		/* both points are one the same side */
		return GL_LightPoint_r( node->children[side], start, end );
	}

	/* find crossing point */
	midFrac = startFrac / ( startFrac - endFrac );
	mid[0] = start[0] + ( end[0] - start[0] ) * midFrac;
	mid[1] = start[1] + ( end[1] - start[1] ) * midFrac;
	mid[2] = start[2] + ( end[2] - start[2] ) * midFrac;

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
	dlight_t *light;
	vec3_t dir;
	vec_t dist, f;
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

    for( i = 0, light = glr.fd.dlights; i < glr.fd.num_dlights; i++, light++ ) {
		VectorSubtract( light->origin, origin, dir );
		dist = VectorLength( dir );
		if( dist > light->intensity ) {
			continue;
		}
        f = 1.0f - dist / light->intensity;
		VectorMA( lightcolor, f, light->color, lightcolor );
	}

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

static void GL_MarkLights_r( bspNode_t *node, dlight_t *light ) {
    cplane_t *plane;
    vec_t dot;
    int lightbit, count;
    bspSurface_t *face;
    
    while( ( plane = node->plane ) != NULL ) {
        switch( plane->type ) {
        case PLANE_X:
            dot = light->transformed[0] - plane->dist;
            break;
        case PLANE_Y:
            dot = light->transformed[1] - plane->dist;
            break;
        case PLANE_Z:
            dot = light->transformed[2] - plane->dist;
            break;
        default:
            dot = DotProduct( light->transformed, plane->normal ) - plane->dist;
            break;
        }

        if( dot > light->intensity ) {
            node = node->children[0];
            continue;
        }
        if( dot < -light->intensity ) {
            node = node->children[1];
            continue;
        }

        lightbit = 1 << light->index;
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

typedef void (*drawFaceFunc_t)( bspSurface_t *surf );

static drawFaceFunc_t   drawFaceFunc;
static drawFaceFunc_t   drawFaceFuncTable[] = {
    GL_AddBspSurface,
    GL_DrawSurfPoly
};

static bspSurface_t	*alphaFaces;

#define BACKFACE_EPSILON	0.001f

#define SIDE_FRONT	0
#define SIDE_BACK	1

void GL_DrawBspModel( bspSubmodel_t *model ) {
	bspSurface_t *face;
	int count;
	vec3_t bounds[2];
	vec_t dot;
	cplane_t *plane;
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

	if( gl_dynamic->integer ) {
		GL_TransformLights( model );
	}

	/* draw visible faces */
	/* FIXME: go by headnode instead? */
	face = model->firstFace;
    count = model->numFaces;
	while( count-- ) {
		plane = face->plane;
		switch( plane->type ) {
		case PLANE_X:
			dot = modelViewOrigin[0] - plane->dist;
			break;
		case PLANE_Y:
			dot = modelViewOrigin[1] - plane->dist;
			break;
		case PLANE_Z:
			dot = modelViewOrigin[2] - plane->dist;
			break;
		default:
			dot = DotProduct( modelViewOrigin, plane->normal ) - plane->dist;
			break;
		}
		if( ( dot < -BACKFACE_EPSILON && face->side == SIDE_FRONT ) ||
			( dot > BACKFACE_EPSILON && face->side == SIDE_BACK ) )
		{
			c.facesCulled++;
		} else {
			if( face->texinfo->flags & (SURF_TRANS33|SURF_TRANS66) ) {
				face->next = alphaFaces;
				alphaFaces = face;
			} else {
				GL_AddBspSurface( face );
			}
			c.facesDrawn++;
		}
		face++;
	}

	qglPushMatrix();
	qglTranslatef( ent->origin[0], ent->origin[1], ent->origin[2] );
	if( glr.entrotated ) {
		qglRotatef( ent->angles[YAW],   0, 0, 1 );
		qglRotatef( ent->angles[PITCH], 0, 1, 0 );
		qglRotatef( ent->angles[ROLL],  1, 0, 0 );
	}
	GL_SortAndDrawSurfs( qtrue );
	qglPopMatrix();

}

static void GL_WorldNode_r( bspNode_t *node, int clipflags ) {
	bspLeaf_t *leaf;
	bspSurface_t **leafFace, *face;
	int count, side, area;
    cplane_t *plane;
    vec_t dot;
	uint32 type;
	
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

        plane = node->plane;
        type = plane->type;
        if( type < 3 ) {
            dot = modelViewOrigin[type] - plane->dist;
        } else {
            dot = DotProduct( modelViewOrigin, plane->normal ) - plane->dist;
        }

        side = dot < 0;
        
        GL_WorldNode_r( node->children[side], clipflags );

        face = node->firstFace;
        count = node->numFaces;
        while( count-- ) {
            if( face->drawframe == glr.drawframe ) {
                if( face->side == side ) {
                    if( face->texinfo->flags & (SURF_TRANS33|SURF_TRANS66) ) {
                        face->next = alphaFaces;
                        alphaFaces = face;
                    } else {
                        drawFaceFunc( face );
                    }
                    c.facesDrawn++;
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
    GL_MarkLeaves();

    if( gl_dynamic->integer ) {
        GL_MarkLights();
    }
    
    R_ClearSkyBox();

    drawFaceFunc = drawFaceFuncTable[gl_primitives->integer & 1];

	VectorCopy( glr.fd.vieworg, modelViewOrigin );

    GL_WorldNode_r( r_world.nodes, NODE_CLIPPED );
    GL_SortAndDrawSurfs( qtrue );

    if( !gl_fastsky->integer ) {
        R_DrawSkyBox();
    }
	
}

void GL_DrawAlphaFaces( void ) {
	bspSurface_t *face, *next;

	face = alphaFaces;
	if( !face ) {
		return;
	}
    
	drawFaceFunc = drawFaceFuncTable[gl_primitives->integer & 1];

	do {
		drawFaceFunc( face ); 
		/* Prevent loop condition in case the same face is included twice.
		 * This should never happen normally. */
		next = face->next;
		face->next = NULL;
		face = next;
	} while( face );

	GL_SortAndDrawSurfs( qfalse );

	alphaFaces = NULL;
}
