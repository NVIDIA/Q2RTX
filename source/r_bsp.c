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

static void Bsp_DecompressVis( byte *src, byte *dst, unsigned rowsize ) {
	unsigned count;
	
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

static void Bsp_LoadVis( lump_t *lump ) {
	dvis_t *src_vis;
	byte *dst, *src;
	uint32_t numClusters, rowsize;
	uint32_t offset;
	int i;
	
	if( !lump->filelen ) {
		r_world.vis = NULL;
		r_world.numClusters = 0;
		return;
	}
	
	src_vis = ( dvis_t * )( loadData + lump->fileofs );
	numClusters = LittleLong( src_vis->numclusters );
	if( !numClusters ) {
		r_world.vis = 0;
		r_world.numClusters = 0;
		return; // it is OK to have a map without vis
	}
	if( numClusters > MAX_MAP_LEAFS/8 ) {
		Com_Error( ERR_DROP, "%s: too many clusters", __func__ );
	}
	
	rowsize = ( numClusters + 7 ) >> 3;
	r_world.numClusters = numClusters;
	r_world.rowsize = rowsize;
	r_world.vis = Bsp_Malloc( numClusters * rowsize );

	dst = r_world.vis;
	for( i = 0; i < numClusters; i++ ) {
		offset = LittleLong( src_vis->bitofs[i][DVIS_PVS] );
		if( offset >= lump->filelen ) {
			Com_Error( ERR_DROP, "%s: bad offset", __func__ );
		}
		
		src = ( byte * )src_vis + offset;
		Bsp_DecompressVis( src, dst, rowsize );
		dst += rowsize;
	}
}

static void Bsp_LoadVertices( lump_t *lump ) {
	int i, count;
	dvertex_t *src, *dst;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_Error( ERR_DROP, "%s: bad lump size", __func__ );
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
}

static void Bsp_LoadEdges( lump_t *lump ) {
	int i, count;
	dedge_t *src, *dst;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_Error( ERR_DROP, "%s: bad lump size", __func__ );
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
}

static void Bsp_LoadSurfEdges( lump_t *lump ) {
	int i, count;
	int *src, *dst;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_Error( ERR_DROP, "%s: bad lump size", __func__ );
	}

	r_world.surfEdges = Bsp_Malloc( ( count + EXTRA_SURFEDGES ) * sizeof( *dst ) );
	r_world.numSurfEdges = count;

	src = ( int * )( loadData + lump->fileofs );
	dst = r_world.surfEdges;
	for( i = 0; i < count; i++ ) {
		*dst++ = LittleLong( *src++ );	
	}
}

static void Bsp_LoadTexinfo( lump_t *lump ) {
	int i, count;
	texinfo_t *src;
	bspTexinfo_t *dst, *tex;
    image_t *texture;
    char path[MAX_QPATH];
	int animNext;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_Error( ERR_DROP, "%s: bad lump size", __func__ );
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
#endif

		animNext = LittleLong( src->nexttexinfo );
		if( animNext > 0 ) {
			if( animNext >= count ) {
				Com_Error( ERR_DROP, "%s: bad anim chain", __func__ );
			}
			dst->animNext = r_world.texinfos + animNext;
		} else {
			dst->animNext = NULL;
		}

		Q_concat( path, sizeof( path ), "textures/", dst->name, ".wal", NULL );
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
}

static void Bsp_LoadFaces( lump_t *lump ) {
	dface_t *src_face;
	bspSurface_t *dst_face;
	int i, j, count;
	uint32_t texinfoNum;
    uint32_t lightmapOffset;
	uint32_t firstEdge;
    uint16_t numEdges;
    uint16_t planeNum;

	count = lump->filelen / sizeof( *src_face );
	if( lump->filelen % sizeof( *src_face ) ) {
		Com_Error( ERR_DROP, "%s: bad lump size", __func__ );
	}

	r_world.surfaces = Bsp_Malloc( ( count + EXTRA_SURFACES ) * sizeof( *dst_face ) );
	r_world.numSurfaces = count;

	src_face = ( dface_t * )( loadData + lump->fileofs );
	dst_face = r_world.surfaces;
	for( i = 0; i < count; i++ ) {
		firstEdge = LittleLong( src_face->firstedge );
		numEdges = LittleShort( src_face->numedges );
        if( numEdges < 3 ) {
			Com_Error( ERR_DROP, "%s: bad surfedges", __func__ );
        }
		if( firstEdge + numEdges > r_world.numSurfEdges ) {
			Com_Error( ERR_DROP, "%s: surfedges out of bounds", __func__ );
		}

        planeNum = LittleShort( src_face->planenum );
        if( planeNum >= r_world.numPlanes ) {
            Com_Error( ERR_DROP, "%s: bad planenum", __func__ );
        }

		texinfoNum = LittleShort( src_face->texinfo );
		if( texinfoNum >= r_world.numTexinfos ) {
			Com_Error( ERR_DROP, "%s: bad texinfo", __func__ );
		}
        
        lightmapOffset = LittleLong( src_face->lightofs );
        if( lightmapOffset == ( uint32_t )-1 || r_world.lightmapSize == 0 ) {
            dst_face->lightmap = NULL;
        } else {
            if( lightmapOffset >= r_world.lightmapSize ) {
                Com_Error( ERR_DROP, "%s: bad lightofs", __func__ );
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
		
		src_face++; dst_face++;
	}
}

static void Bsp_LoadLeafFaces( lump_t *lump ) {
	int i, count;
	uint32_t faceNum;
	uint16_t *src;
	bspSurface_t **dst;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_Error( ERR_DROP, "%s: bad lump size", __func__ );
	}

	r_world.numLeafFaces = count;
	r_world.leafFaces = Bsp_Malloc( sizeof( *dst ) * count );
	
	src = ( uint16_t * )( loadData + lump->fileofs );
	dst = r_world.leafFaces;
	for( i = 0; i < count; i++ ) {
		faceNum = LittleShort( *src );
		if( faceNum >= r_world.numSurfaces ) {
			Com_Error( ERR_DROP, "%s: bad face index\n", __func__ );
		}
		*dst = r_world.surfaces + faceNum;
		
		src++; dst++;
	}
}

static void Bsp_LoadLeafs( lump_t *lump ) {
	int i, count;
	uint16_t cluster, area;
	uint16_t firstLeafFace, numLeafFaces;
	dleaf_t *src;
	bspLeaf_t *dst;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_Error( ERR_DROP, "%s: bad lump size", __func__ );
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
			Com_Error( ERR_DROP, "%s: bad area num", __func__ );
		}
		dst->area = area;
		dst->contents = LittleLong( src->contents );

		cluster = LittleShort( src->cluster );
		if( cluster == ( uint16_t )-1 || r_world.numClusters == 0 ) {
			dst->cluster = -1;
		} else {
            if( cluster >= r_world.numClusters ) {
			    Com_Error( ERR_DROP, "%s: bad cluster num", __func__ );
		    }
    		dst->cluster = cluster;
        }
		
		firstLeafFace = LittleShort( src->firstleafface );
		numLeafFaces = LittleShort( src->numleaffaces );
		
		if( firstLeafFace + numLeafFaces > r_world.numLeafFaces ) {
			Com_Error( ERR_DROP, "%s: bad leafface", __func__ );
		}

		dst->firstLeafFace = r_world.leafFaces + firstLeafFace;
		dst->numLeafFaces = numLeafFaces;
		
		LSV( mins );
		LSV( maxs );
		
		src++; dst++;
	}
}

static void Bsp_LoadPlanes( lump_t *lump ) {
	int i, count;
	dplane_t *src;
	cplane_t *dst;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_Error( ERR_DROP, "%s: bad lump size", __func__ );
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
}

static void Bsp_LoadNodes( lump_t *lump ) {
	int i, j, count;
	uint32_t planenum;
	uint32_t child;
	dnode_t *src;
	bspNode_t *dst;
	uint16_t firstFace, numFaces;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_Error( ERR_DROP, "%s: bad lump size", __func__ );
	}

	r_world.numNodes = count;
	r_world.nodes = Bsp_Malloc( sizeof( *dst ) * count );
	
	src = ( dnode_t * )( loadData + lump->fileofs );
	dst = r_world.nodes;
	for( i = 0; i < count; i++ ) {
		dst->index = i;
		dst->parent = NULL;
		dst->visframe = -1;
		
		// load splitting plane index
		planenum = LittleLong( src->planenum );
		if( planenum >= r_world.numPlanes ) {
			Com_Error( ERR_DROP, "%s: bad planenum for node %d", __func__, i );
		}
		dst->plane = r_world.planes + planenum;
		
		// load children indices
		for( j = 0; j < 2; j++ ) {
			child = LittleLong( src->children[j] );
			if( child & 0x80000000 ) {
				// leaf index
				child = ~child;
				if( child >= r_world.numLeafs ) {
					Com_Error( ERR_DROP, "%s: bad leafnum for node %d", __func__, i );
				}
				dst->children[j] = ( bspNode_t * )( r_world.leafs + child );
			} else {
				// node index
				if( child >= count ) {
					Com_Error( ERR_DROP, "%s: bad nodenum for node %d", __func__, i );
				}
				dst->children[j] = r_world.nodes + child;
			}
		}

		firstFace = LittleShort( src->firstface );
		numFaces = LittleShort( src->numfaces );

		if( firstFace + numFaces > r_world.numSurfaces ) {
			Com_Error( ERR_DROP, "%s: bad faces for node %d", __func__, i );
		}

		dst->firstFace = r_world.surfaces + firstFace;
		dst->numFaces = numFaces;
		
		LSV( mins );
		LSV( maxs );

		src++; dst++;
	}
}

static void Bsp_LoadLightMap( lump_t *lump ) {
	if( !lump->filelen ) {
        return;
    }

    r_world.lightmap = Bsp_Malloc( lump->filelen );
    memcpy( r_world.lightmap, loadData + lump->fileofs, lump->filelen );
    r_world.lightmapSize = lump->filelen;
}

static void Bsp_LoadSubmodels( lump_t *lump ) {
	int i, count;
	dmodel_t *src;
	bspSubmodel_t *dst;
	uint32_t firstFace, numFaces;
	uint32_t headnode;

	count = lump->filelen / sizeof( *src );
	if( lump->filelen % sizeof( *src ) ) {
		Com_Error( ERR_DROP, "%s: bad lump size", __func__ );
	}

	r_world.numSubmodels = count;
	r_world.submodels = Bsp_Malloc( sizeof( *dst ) * count );

	src = ( dmodel_t * )( loadData + lump->fileofs );
	dst = r_world.submodels;
	for( i = 0; i < count; i++ ) {
		firstFace = LittleLong( src->firstface );
		numFaces = LittleLong( src->numfaces );
		if( firstFace + numFaces > r_world.numSurfaces ) {
			Com_Error( ERR_DROP, "%s: bad faces for model %d", __func__, i );
		}

		headnode = LittleLong( src->headnode );
		if( headnode >= r_world.numNodes ) {
			// FIXME: headnode may be garbage for some models
			Com_DPrintf( "%s: bad headnode for model %d", __func__, i );
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

void Bsp_LoadWorld( const char *path ) {
	dheader_t header;
	lump_t *lump;
	int i;
    byte *data;
    int length;

    length = fs.LoadFileEx( path, ( void ** )&data, FS_FLAG_CACHE );
    if( !data ) {
        Com_Error( ERR_DROP, "%s: couldn't load %s", __func__, path );
    }

	if( length < sizeof( header ) ) {
		Com_Error( ERR_DROP, "%s: %s is too small", __func__, path );
	}
	
	// byte swap and validate the header
	header = *( dheader_t * )data;
	header.ident = LittleLong( header.ident );
	header.version = LittleLong( header.version );
	if( header.ident != IDBSPHEADER ) {
		Com_Error( ERR_DROP, "%s: %s is not an IBSP file", __func__, path );
	}
	if( header.version != BSPVERSION ) {
		Com_Error( ERR_DROP, "%s: %s has wrong IBSP version", __func__, path );
	}
	
	// byte swap and validate lumps
	lump = header.lumps;
	for( i = 0; i < HEADER_LUMPS; i++ ) {
		lump->fileofs = LittleLong( lump->fileofs );
		lump->filelen = LittleLong( lump->filelen );
		if( lump->fileofs + lump->filelen > length ) {
			Com_Error( ERR_DROP, "%s: %s has lump #%d out of bounds\n",
                __func__, path, i );
		}
		lump++;
	}

	loadData = data;
    
    // reserve 16 MB of virtual memory
    sys.HunkBegin( &r_world.pool, 0x1000000 );

#ifdef OPENGL_RENDERER
	GL_BeginPostProcessing();
#endif

#define LOAD( Func, Lump ) do { \
			                    Bsp_Load##Func( &header.lumps[LUMP_##Lump] ); \
                           } while( 0 )

	LOAD( Vis,          VISIBILITY );
	LOAD( Vertices,     VERTEXES   );
	LOAD( Edges,        EDGES      );
	LOAD( SurfEdges,    SURFEDGES  );
	LOAD( Texinfo,      TEXINFO    );
	LOAD( LightMap,     LIGHTING   );
	LOAD( Planes,       PLANES     );
	LOAD( Faces,        FACES      );
	LOAD( LeafFaces,    LEAFFACES  );
	LOAD( Leafs,        LEAFS      );
	LOAD( Nodes,        NODES      );
	LOAD( Submodels,    MODELS     );

#undef LOAD

#ifdef OPENGL_RENDERER
	GL_EndPostProcessing();
#endif
    
    fs.FreeFile( data );

    sys.HunkEnd( &r_world.pool );

    Bsp_SetParent( r_world.nodes );

    strcpy( r_world.name, path );
}


