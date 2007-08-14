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

//
// r_bsp.c -- map loading functions common for both renderers
//

#include "config.h"
#include "q_shared.h"
#include "com_public.h"
#include "q_files.h"
#include "q_list.h"
#include "r_shared.h"

bspModel_t  r_world;

bspLeaf_t *Bsp_FindLeaf( vec3_t origin ) {
	bspNode_t *node;
	cplane_t *plane;
	vec_t dot;

	node = r_world.nodes;
	do {
		plane = node->plane;
		dot = DotProduct( plane->normal, origin ) - plane->dist;
		if( dot >= 0 ) {
			node = node->children[0];
		} else {
			node = node->children[1];
		}
	} while( node->plane );
	
	return ( bspLeaf_t * )node;
}

byte *Bsp_ClusterPVS( int clusterNum ) {
	if( !r_world.vis || clusterNum == -1 ) {
		return NULL;
	}

	return r_world.vis + clusterNum * r_world.rowsize;
}


/* ========================================================================= */

#define Bsp_Malloc( size )	sys.HunkAlloc( &r_world.pool, size )

static byte *loadData;

static void Bsp_DecompressVis( byte *src, byte *dst, uint32 rowsize ) {
	uint32 count;
	
	do {
		if( *src ) {
			*dst++ = *src++;
			rowsize--;
		} else {
			src++; count = *src++;
			if( count > rowsize ) {
				Com_WPrintf( "Bsp_DecompressVis: overrun\n" );
				count = rowsize;
			}
			rowsize -= count;
			while( count-- ) {
				*dst++ = 0;
			}
		}
	} while( rowsize );
}

static int Bsp_LoadVis( lump_t *lump ) {
	dvis_t *src_vis;
	byte *dst, *src;
	uint32 numClusters, rowsize;
	uint32 offset;
	int i;
	
	if( !lump->filelen ) {
		r_world.vis = NULL;
		r_world.numClusters = 0;
		return 0;
	}
	
	if( lump->filelen < sizeof( *src ) ) {
		Com_EPrintf( "LoadVis: bad lump size\n" );
		return -1;
	}

	src_vis = ( dvis_t * )( loadData + lump->fileofs );
	numClusters = LittleLong( src_vis->numclusters );
	if( !numClusters ) {
		r_world.vis = 0;
		r_world.numClusters = 0;
		return 0; /* it is OK to have a map without vis */
	}
	if( numClusters > MAX_MAP_LEAFS/8 ) {
		Com_EPrintf( "LoadVis: bad number of clusters\n" );
		return -1;
	}
	
	rowsize = ( numClusters + 7 ) >> 3;
	r_world.numClusters = numClusters;
	r_world.rowsize = rowsize;
	r_world.vis = Bsp_Malloc( numClusters * rowsize );

	dst = r_world.vis;
	for( i = 0; i < numClusters; i++ ) {
		offset = LittleLong( src_vis->bitofs[i][DVIS_PVS] );
		if( offset >= lump->filelen ) {
			Com_EPrintf( "LoadVis: bad offset\n" );
            return -1;
		}
		
		src = ( byte * )src_vis + offset;
		Bsp_DecompressVis( src, dst, rowsize );
		dst += rowsize;
	}

	return 0;
}

static int Bsp_LoadVertices( lump_t *lump ) {
	int count;
	dvertex_t *src;
	dvertex_t *dst;
	int i;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_EPrintf( "LoadVertices: bad lump size\n" );
		return -1;
	}

	r_world.vertices = Bsp_Malloc( ( count + EXTRA_VERTICES ) * sizeof( *dst ) );
	r_world.numVertices = count;

	src = ( dvertex_t * )( loadData + lump->fileofs );
	dst = r_world.vertices;
	for( i = 0; i < count; i++ ) {
		dst->point[0] = LittleFloat( src->point[0] );
		dst->point[1] = LittleFloat( src->point[1] );
		dst->point[2] = LittleFloat( src->point[2] );
		
		src++; dst++;	
	}

	return 0;
}

static int Bsp_LoadEdges( lump_t *lump ) {
	int count;
	dedge_t *src;
	dedge_t *dst;
	int i;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_EPrintf( "LoadEdges: bad lump size\n" );
		return -1;
	}

	r_world.edges = Bsp_Malloc( ( count + EXTRA_EDGES ) * sizeof( *dst ) );
	r_world.numEdges = count;

	src = ( dedge_t * )( loadData + lump->fileofs );
	dst = r_world.edges;
	for( i = 0; i < count; i++ ) {
		dst->v[0] = LittleShort( src->v[0] );
		dst->v[1] = LittleShort( src->v[1] );
		
		src++; dst++;	
	}

	return 0;
}

static int Bsp_LoadSurfEdges( lump_t *lump ) {
	int count;
	int *src;
	int *dst;
	int i;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_EPrintf( "LoadSurfEdges: bad lump size\n" );
		return -1;
	}

	r_world.surfEdges = Bsp_Malloc( ( count + EXTRA_SURFEDGES ) * sizeof( *dst ) );
	r_world.numSurfEdges = count;

	src = ( int * )( loadData + lump->fileofs );
	dst = r_world.surfEdges;
	for( i = 0; i < count; i++ ) {
		*dst++ = LittleLong( *src++ );	
	}

	return 0;
}

static int Bsp_LoadTexinfo( lump_t *lump ) {
	int count;
	int i;
	texinfo_t *src;
	bspTexinfo_t *dst, *tex;
    image_t *texture;
    char path[MAX_QPATH];
	int animNext;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_EPrintf( "LoadTexinfo: bad lump size\n" );
		return -1;
	}

	r_world.numTexinfos = count;
	r_world.texinfos = Bsp_Malloc( sizeof( *dst ) * count );
	
	src = ( texinfo_t * )( loadData + lump->fileofs );
	dst = r_world.texinfos;
	for( i = 0; i < count; i++ ) {
		Q_strncpyz( dst->name, src->texture, sizeof( dst->name ) );
		dst->flags = LittleLong( src->flags );

		dst->axis[0][0] = LittleFloat( src->vecs[0][0] );
		dst->axis[0][1] = LittleFloat( src->vecs[0][1] );
		dst->axis[0][2] = LittleFloat( src->vecs[0][2] );

		dst->axis[1][0] = LittleFloat( src->vecs[1][0] );
		dst->axis[1][1] = LittleFloat( src->vecs[1][1] );
		dst->axis[1][2] = LittleFloat( src->vecs[1][2] );

		dst->offset[0] = LittleFloat( src->vecs[0][3] );
		dst->offset[1] = LittleFloat( src->vecs[1][3] );

#ifdef OPENGL_RENDERER
        upload_texinfo = dst;

        VectorNormalize2( dst->axis[0], dst->normalizedAxis[0] );
        VectorNormalize2( dst->axis[1], dst->normalizedAxis[1] );
#endif

		animNext = LittleLong( src->nexttexinfo );
		if( animNext > 0 ) {
			if( animNext >= count ) {
				Com_EPrintf( "Bsp_LoadTexinfo: bad anim chain\n" );
				return -1;
			}
			dst->animNext = r_world.texinfos + animNext;
		} else {
			dst->animNext = NULL;
		}

		Com_sprintf( path, sizeof( path ), "textures/%s.wal", dst->name );
        texture = R_FindImage( path, it_wall );
        if( texture ) {
            dst->image = texture;
        } else {
            dst->image = r_notexture;
        }

#ifdef OPENGL_RENDERER
        upload_texinfo = NULL;
#endif
		
		src++; dst++;
	}

	dst = r_world.texinfos;
	for( i = 0; i < count; i++ ) {
		dst->numFrames = 1;
		for( tex = dst->animNext; tex && tex != dst; tex = tex->animNext ) {
			dst->numFrames++;
		}
		dst++;
	}

	return 0;
}

static int Bsp_LoadFaces( lump_t *lump ) {
	int count;
	dface_t *src_face;
	bspSurface_t *dst_face;
	int i, j;
	uint32 texinfoNum;
    uint32 lightmapOffset;
	uint32 firstEdge, numEdges;
    uint32 planeNum;

	count = lump->filelen / sizeof( *src_face );
	if( lump->filelen % sizeof( *src_face ) ) {
		Com_EPrintf( "LoadFace: bad faces lump size\n" );
		return -1;
	}

	r_world.surfaces = Bsp_Malloc( ( count + EXTRA_SURFACES ) * sizeof( *dst_face ) );
	r_world.numSurfaces = count;

	src_face = ( dface_t * )( loadData + lump->fileofs );
	dst_face = r_world.surfaces;
	for( i = 0; i < count; i++ ) {
		firstEdge = LittleLong( src_face->firstedge );
		numEdges = LittleShort( src_face->numedges );
        if( numEdges < 3 || numEdges > MAX_MAP_SURFEDGES ) {
			Com_EPrintf( "LoadFace: bad surfedges\n" );
			return -1;
        }
		if( firstEdge + numEdges > r_world.numSurfEdges ) {
			Com_EPrintf( "LoadFace: surfedges out of bounds\n" );
			return -1;
		}

        planeNum = LittleShort( src_face->planenum );
        if( planeNum >= r_world.numPlanes ) {
            Com_EPrintf( "LoadFace: bad planenum\n" );
            return -1;
        }

		texinfoNum = LittleShort( src_face->texinfo );
		if( texinfoNum >= r_world.numTexinfos ) {
			Com_EPrintf( "LoadFace: bad texinfo\n" );
			return -1;
		}
        
        lightmapOffset = LittleLong( src_face->lightofs );
        if( lightmapOffset == ( uint32 )-1 || r_world.lightmapSize == 0 ) {
            dst_face->lightmap = NULL;
        } else {
            if( lightmapOffset >= r_world.lightmapSize ) {
                Com_EPrintf( "LoadFace: bad lightofs\n" );
                return -1;
            }
            dst_face->lightmap = r_world.lightmap + lightmapOffset;
        }

		dst_face->texinfo = r_world.texinfos + texinfoNum;
        j = LittleShort( src_face->side );
        dst_face->side = j & 1;
		dst_face->index = i;
        dst_face->type = DSURF_POLY;
        dst_face->plane = r_world.planes + planeNum;
        dst_face->firstSurfEdge = r_world.surfEdges + firstEdge;
        dst_face->numSurfEdges = numEdges;
		
#ifdef OPENGL_RENDERER
		if( GL_PostProcessSurface( dst_face ) ) {
            return -1;
        }
#endif

		src_face++; dst_face++;
	}
	

	return 0;
}

static int Bsp_LoadLeafFaces( lump_t *lump ) {
	int count;
	int i;
	uint32 faceNum;
	uint16 *src;
	bspSurface_t **dst;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_EPrintf( "LoadLeafFaces: bad lump size\n" );
		return -1;
	}

	r_world.numLeafFaces = count;
	r_world.leafFaces = Bsp_Malloc( sizeof( *dst ) * count );
	
	src = ( uint16 * )( loadData + lump->fileofs );
	dst = r_world.leafFaces;
	for( i = 0; i < count; i++ ) {
		faceNum = LittleShort( *src );
		if( faceNum >= r_world.numSurfaces ) {
			Com_EPrintf( "LoadLeafFaces: bad face index\n" );
			return -1;
		}
		*dst = r_world.surfaces + faceNum;
		
		src++; dst++;
	}

	return 0;
}

static int Bsp_LoadLeafs( lump_t *lump ) {
	int count;
	int i;
	uint32 cluster, area;
	uint32 firstLeafFace;
	uint32 numLeafFaces;
	dleaf_t *src;
	bspLeaf_t *dst;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_EPrintf( "LoadLeafs: bad lump size\n" );
		return -1;
	}

	r_world.numLeafs = count;
	r_world.leafs = Bsp_Malloc( sizeof( *dst ) * count );
	
	src = ( dleaf_t * )( loadData + lump->fileofs );
	dst = r_world.leafs;
	for( i = 0; i < count; i++ ) {
		dst->index = i;
		dst->plane = NULL;
		dst->parent = NULL;
		dst->visframe = -1;
	
		area = LittleShort( src->area );
		if( area >= MAX_MAP_AREAS ) {
			Com_EPrintf( "LoadLeafs: bad area num\n" );
			return -1;
		}
		dst->area = area;
		dst->contents = LittleLong( src->contents );

		cluster = LittleShort( src->cluster );
		if( cluster == ( uint16 )-1 || r_world.numClusters == 0 ) {
			dst->cluster = -1;
		} else {
            if( cluster >= r_world.numClusters ) {
			    Com_EPrintf( "LoadLeafs: bad cluster num\n" );
			    return -1;
		    }
    		dst->cluster = cluster;
        }
		
		firstLeafFace = LittleShort( src->firstleafface );
		numLeafFaces = LittleShort( src->numleaffaces );
		
		if( firstLeafFace + numLeafFaces > r_world.numLeafFaces ) {
			Com_EPrintf( "LoadLeafs: bad leafface\n" );
			return -1;
		}

		dst->firstLeafFace = r_world.leafFaces + firstLeafFace;
		dst->numLeafFaces = numLeafFaces;
		
		LSV( mins );
		LSV( maxs );
		
		src++; dst++;
	}

	return 0;
}

static int Bsp_LoadPlanes( lump_t *lump ) {
	int count;
	int i;
	dplane_t *src;
	cplane_t *dst;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_EPrintf( "LoadPlanes: bad lump size\n" );
		return -1;
	}

	r_world.numPlanes = count;
	r_world.planes = Bsp_Malloc( sizeof( *dst ) * count );
	
	src = ( dplane_t * )( loadData + lump->fileofs );
	dst = r_world.planes;
	for( i = 0; i < count; i++ ) {
		LV( normal );
		LF( dist );
        SetPlaneType( dst );
        SetPlaneSignbits( dst );
		
		src++; dst++;
	}

	return 0;
}

static int Bsp_LoadNodes( lump_t *lump ) {
	int count;
	int i, j;
	uint32 planenum;
	uint32 child;
	dnode_t *src;
	bspNode_t *dst;
	uint32 firstFace, numFaces;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_EPrintf( "LoadNodes: bad lump size\n" );
		return -1;
	}

	r_world.numNodes = count;
	r_world.nodes = Bsp_Malloc( sizeof( *dst ) * count );
	
	src = ( dnode_t * )( loadData + lump->fileofs );
	dst = r_world.nodes;
	for( i = 0; i < count; i++ ) {
		dst->index = i;
		dst->parent = NULL;
		dst->visframe = -1;
		
		/* load splitting plane index */
		planenum = LittleLong( src->planenum );
		if( planenum >= r_world.numPlanes ) {
			Com_EPrintf( "LoadNodes: bad planenum\n" );
			return -1;
		}
		dst->plane = r_world.planes + planenum;
		
		/* load children indices */
		for( j = 0; j < 2; j++ ) {
			child = LittleLong( src->children[j] );
			if( child & 0x80000000 ) {
				/* leaf index */
				child = ~child;
				if( child >= r_world.numLeafs ) {
					Com_EPrintf( "LoadNodes: bad leafnum\n" );
					return -1;
				}
				dst->children[j] = ( bspNode_t * )( r_world.leafs + child );
			} else {
				/* node index */
				if( child >= count ) {
					Com_EPrintf( "LoadNodes: bad nodenum\n" );
					return -1;
				}
				dst->children[j] = r_world.nodes + child;
			}
		}

		firstFace = LittleShort( src->firstface );
		numFaces = LittleShort( src->numfaces );

		if( firstFace + numFaces > r_world.numSurfaces ) {
			Com_EPrintf( "LoadNodes: bad faces\n" );
			return -1;
		}

		dst->firstFace = r_world.surfaces + firstFace;
		dst->numFaces = numFaces;
		
		LSV( mins );
		LSV( maxs );

		src++; dst++;
	}

	return 0;
}

#if 0
int Bsp_LoadEntityString( lump_t *lump ) {
	if( !lump->filelen ) {
        return 0;
    }

    r_world.entityString = Bsp_Malloc( lump->filelen + 1 );
    memcpy( r_world.entityString, loadData + lump->fileofs, lump->filelen );
    r_world.entityString[lump->filelen] = 0;
	
	return 0;
}
#endif

int Bsp_LoadLightMap( lump_t *lump ) {
	if( !lump->filelen ) {
        return 0;
    }

    r_world.lightmap = Bsp_Malloc( lump->filelen );
    memcpy( r_world.lightmap, loadData + lump->fileofs, lump->filelen );
    r_world.lightmapSize = lump->filelen;

    return 0;
}

static int Bsp_LoadSubmodels( lump_t *lump ) {
	int count;
	int i;
	dmodel_t *src;
	bspSubmodel_t *dst;
	uint32 firstFace, numFaces;
	uint32 headnode;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_EPrintf( "LoadSubmodels: bad lump size\n" );
		return -1;
	}

	r_world.numSubmodels = count;
	r_world.submodels = Bsp_Malloc( sizeof( *dst ) * count );

	src = ( dmodel_t * )( loadData + lump->fileofs );
	dst = r_world.submodels;
	for( i = 0; i < count; i++ ) {
		firstFace = LittleLong( src->firstface );
		numFaces = LittleLong( src->numfaces );
		if( firstFace + numFaces > r_world.numSurfaces ) {
			Com_EPrintf( "LoadSubmodels: bad faces\n" );
			return -1;
		}

		headnode = LittleLong( src->headnode );
		if( headnode >= r_world.numNodes ) {
			/* FIXME: headnode may be garbage for some models */
			Com_DPrintf( "LoadSubmodels: bad headnode for model %d\n", i );
			dst->headnode = NULL;
		} else {
			dst->headnode = r_world.nodes + headnode;
		}

		dst->type = MODEL_BSP;
		dst->firstFace = r_world.surfaces + firstFace;
		dst->numFaces = numFaces;

		LV( mins );
		LV( maxs );
		LV( origin );

		dst->radius = RadiusFromBounds( dst->mins, dst->maxs );

		src++; dst++;
	}

	return 0;
}


static void Bsp_SetParent( bspNode_t *node ) {
	bspNode_t *child;
	
	while( node->plane ) {
		child = node->children[0];
		child->parent = node;
		Bsp_SetParent( child );
		
		child = node->children[1];
		child->parent = node;
		node = child;
	}

}

void Bsp_FreeWorld( void ) {
    if( r_world.name[0] ) {
        sys.HunkFree( &r_world.pool );
        memset( &r_world, 0, sizeof( r_world ) );
    }
}

qboolean Bsp_LoadWorld( const char *path ) {
	dheader_t header;
	lump_t *lump;
	int i;
    byte *data;
    int length;

    length = fs.LoadFileEx( path, ( void ** )&data, FS_FLAG_CACHE );
    if( !data ) {
        return qfalse;
    }

	if( length < sizeof( header ) ) {
		Com_EPrintf( "Bsp_Load: file length < header length\n" );
        goto fail;
	}
	
	/* byte swap and validate the header */
	header = *( dheader_t * )data;
	header.ident = LittleLong( header.ident );
	header.version = LittleLong( header.version );
	if( header.ident != IDBSPHEADER ) {
		Com_EPrintf( "Bsp_Load: not an IBSP file: %x != %x\n",
			header.ident, IDBSPHEADER );
        goto fail;
	}
	if( header.version != BSPVERSION ) {
		Com_EPrintf( "Bsp_Load: wrong version number: %u != %u\n",
			header.version, BSPVERSION );
        goto fail;
	}
	
	/* byte swap and validate lumps */
	lump = header.lumps;
	for( i = 0; i < HEADER_LUMPS; i++ ) {
		lump->fileofs = LittleLong( lump->fileofs );
		lump->filelen = LittleLong( lump->filelen );
		if( lump->fileofs + lump->filelen > length ) {
			Com_EPrintf( "Bsp_Load: lump #%d out of bounds\n", i );
            goto fail;
		}
		lump++;
	}

	loadData = data;

    
    /* reserve 16 MB of virtual memory */
    sys.HunkBegin( &r_world.pool, 0x1000000 );

#define BSP_LOAD( Func, Lump ) \
			if( Bsp_Load##Func( &header.lumps[LUMP_##Lump] ) ) { \
				sys.HunkFree( &r_world.pool ); \
                goto fail; \
			}

	BSP_LOAD( Vis, VISIBILITY )
	BSP_LOAD( Vertices, VERTEXES )
	BSP_LOAD( Edges, EDGES )
	BSP_LOAD( SurfEdges, SURFEDGES )
	BSP_LOAD( Texinfo, TEXINFO )
	BSP_LOAD( LightMap, LIGHTING )
	BSP_LOAD( Planes, PLANES )
	BSP_LOAD( Faces, FACES )
	BSP_LOAD( LeafFaces, LEAFFACES )
	BSP_LOAD( Leafs, LEAFS )
	BSP_LOAD( Nodes, NODES )
	BSP_LOAD( Submodels, MODELS )
//	BSP2_LOAD( EntityString, ENTITIES )
    
    fs.FreeFile( data );

    sys.HunkEnd( &r_world.pool );

    Bsp_SetParent( r_world.nodes );

    strcpy( r_world.name, path );

    return qtrue;

fail:
	fs.FreeFile( data );
	return qfalse;
}


