/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2008 Andrey Nazarov

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

// bsp.c -- model loading

#include "com_local.h"
#include "q_list.h"
#include "files.h"
#include "sys_public.h"
#include "bsp.h"

void BSP_SetError( const char *fmt, ... ) q_printf( 1, 2 );

extern mtexinfo_t nulltexinfo;

static cvar_t *map_override_path;

/*
===============================================================================

                    LUMP LOADING

===============================================================================
*/

#define BSP_Malloc( size )   Hunk_Alloc( &bsp->pool, size )

#define LOAD( Func ) \
    static qboolean BSP_Load##Func( bsp_t *bsp, void *base, size_t count )

/*
=================
Visibility
=================
*/
LOAD( Visibility ) {
    unsigned numclusters, bitofs;
    int i, j;

    if( !count ) {
        return qtrue;
    }

    bsp->numvisibility = count;
    bsp->vis = BSP_Malloc( count );
    memcpy( bsp->vis, base, count );

    numclusters = LittleLong( bsp->vis->numclusters );
    if( numclusters > ( count - 4 ) / 8 ) {
		BSP_SetError( "%s: bad numclusters", __func__ );
    }
    bsp->vis->numclusters = numclusters;
    bsp->visrowsize = ( numclusters + 7 ) >> 3;
    for( i = 0; i < numclusters; i++ ) {
        for( j = 0; j < 2; j++ ) {
            bitofs = LittleLong( bsp->vis->bitofs[i][j] );
            if( bitofs >= count ) {
				BSP_SetError( "%s: bad bitofs", __func__ );
                return qfalse;
            }
            bsp->vis->bitofs[i][j] = bitofs;
        }
    }

    return qtrue;
}

/*
=================
Texinfo
=================
*/
LOAD( Texinfo ) {
    dtexinfo_t  *in;
    mtexinfo_t  *out;
    int         i;
#if USE_REF
    int         j, k;
    int         next;
    mtexinfo_t  *step;
#endif

    bsp->numtexinfo = count;
    bsp->texinfo = BSP_Malloc( sizeof( *out ) * count );

    in = base;
    out = bsp->texinfo;
    for( i = 0; i < count; i++, in++, out++ ) {
        memcpy( out->c.name, in->texture, sizeof( out->c.name ) );
        out->c.name[ sizeof( out->c.name ) - 1 ] = 0;
        memcpy( out->name, in->texture, sizeof( out->name ) );
        out->name[ sizeof( out->name ) - 1 ] = 0;
        out->c.flags = LittleLong (in->flags);
        out->c.value = LittleLong (in->value);

#if USE_REF
        for( j = 0; j < 2; j++ ) {
            for( k = 0; k < 3; k++ ) {
        		out->axis[j][k] = LittleFloat( in->vecs[j][k] );
            }
		    out->offset[j] = LittleFloat( in->vecs[j][k] );
        }

		next = LittleLong( in->nexttexinfo );
		if( next > 0 ) {
			if( next >= count ) {
				BSP_SetError( "%s: bad anim chain", __func__ );
                return qfalse;
			}
			out->next = bsp->texinfo + next;
		} else {
			out->next = NULL;
		}
#endif
    }

#if USE_REF
	// count animation frames
    out = bsp->texinfo;
	for( i = 0; i < count; i++, out++ ) {
		out->numframes = 1;
		for( step = out->next; step && step != out; step = step->next ) {
			out->numframes++;
        }
    }
#endif
    return qtrue;
}

/*
=================
Planes
=================
*/
LOAD( Planes ) {
    dplane_t    *in;
    cplane_t    *out;
    int         i, j;
    
    bsp->numplanes = count;
    bsp->planes = BSP_Malloc( sizeof( *out ) * count );

    in = base;
    out = bsp->planes;
    for( i = 0; i < count; i++, in++, out++ ) {
        for( j = 0; j < 3; j++ ) {
            out->normal[j] = LittleFloat( in->normal[j] );
        }
        out->dist = LittleFloat( in->dist );
        SetPlaneType( out );
        SetPlaneSignbits( out );
    }
    return qtrue;
}

/*
=================
BrushSides
=================
*/
LOAD( BrushSides ) {
    dbrushside_t    *in;
    mbrushside_t    *out;
    int         i;
    unsigned    planenum, texinfo;

    bsp->numbrushsides = count;
    bsp->brushsides = BSP_Malloc( sizeof( *out ) * count );

    in = base;
    out = bsp->brushsides;
    for( i = 0; i < count; i++, in++, out++ ) {
        planenum = LittleShort (in->planenum);
        if( planenum >= bsp->numplanes ) {
            BSP_SetError( "%s: bad planenum", __func__ );
            return qfalse;
        }
        out->plane = bsp->planes + planenum;
        texinfo = LittleShort (in->texinfo);
        if( texinfo == ( uint16_t )-1 ) {
            out->texinfo = &nulltexinfo;
        } else {
            if (texinfo >= bsp->numtexinfo) {
                BSP_SetError( "%s: bad texinfo", __func__ );
                return qfalse;
            }
            out->texinfo = bsp->texinfo + texinfo;
        }
    }
    return qtrue;
}

/*
=================
Brushes
=================
*/
LOAD( Brushes ) {
    dbrush_t    *in;
    mbrush_t    *out;
    int         i;
    unsigned    firstside, numsides, lastside;
    
    bsp->numbrushes = count;
    bsp->brushes = BSP_Malloc( sizeof( *out ) * count );

    in = base;
    out = bsp->brushes;
    for( i = 0; i < count; i++, out++, in++ ) {
        firstside = LittleLong(in->firstside);
        numsides = LittleLong(in->numsides);
        lastside = firstside + numsides;
        if( lastside < firstside || lastside > bsp->numbrushsides ) {
            BSP_SetError( "%s: bad brushsides", __func__ );
            return qfalse;
        }
        out->firstbrushside = bsp->brushsides + firstside;
        out->numsides = numsides;
        out->contents = LittleLong(in->contents);
        out->checkcount = 0;
    }
    return qtrue;
}

/*
=================
LeafBrushes
=================
*/
LOAD( LeafBrushes ) {
    uint16_t    *in;
    mbrush_t    **out;
    int         i;
    unsigned    brushnum;
    
    bsp->numleafbrushes = count;
    bsp->leafbrushes = BSP_Malloc( sizeof( *out ) * count );

    in = base;
    out = bsp->leafbrushes;
    for( i = 0; i < count; i++, in++, out++ ) {
        brushnum = LittleShort( *in );
        if( brushnum >= bsp->numbrushes ) {
            BSP_SetError( "%s: bad brushnum", __func__ );
            return qfalse;
        }
        *out = bsp->brushes + brushnum;
    }
    return qtrue;
}


#if USE_REF
/*
=================
Lightmap
=================
*/
LOAD( Lightmap ) {
#if USE_REF == REF_SOFT
    byte *in;
    byte *out;
    int i;

    count /= 3;
#endif

    if( !count ) {
        return qtrue;
    }

    bsp->numlightmapbytes = count;
    bsp->lightmap = BSP_Malloc( count );

#if USE_REF == REF_SOFT
    // convert the 24 bit lighting down to 8 bit
    // by taking the brightest component
    in = base;
    out = bsp->lightmap;
	for( i = 0; i < count; i++, in += 3, out++ ) {
		if( in[0] > in[1] && in[0] > in[2] )
			*out = in[0];
		else if( in[1] > in[0] && in[1] > in[2] )
			*out = in[1];
		else
			*out = in[2];
	}
#else
    memcpy( bsp->lightmap, base, count );
#endif
    return qtrue;
}

/*
=================
Vertices
=================
*/
LOAD( Vertices ) {
    dvertex_t   *in;
    mvertex_t   *out;
    int         i, j;
    
    bsp->numvertices = count;
    bsp->vertices = BSP_Malloc( sizeof( *out ) * count );

    in = base;
    out = bsp->vertices;
    for( i = 0; i < count; i++, out++, in++ ) {
        for( j = 0; j < 3; j++ ) {
    		out->point[j] = LittleFloat( in->point[j] );
        }
    }
    return qtrue;
}

/*
=================
Edges
=================
*/
LOAD( Edges ) {
    dedge_t     *in;
    medge_t     *out;
    int         i, j;
    unsigned    vertnum;
    
    bsp->numedges = count;
    bsp->edges = BSP_Malloc( sizeof( *out ) * count );

    in = base;
    out = bsp->edges;
    for( i = 0; i < count; i++, out++, in++ ) {
        for( j = 0; j < 2; j++ ) {
    		vertnum = LittleShort( in->v[j] );
            if( vertnum >= bsp->numvertices ) {
                BSP_SetError( "%s: bad vertnum", __func__ );
                return qfalse;
            }
            out->v[j] = bsp->vertices + vertnum;
        }
    }
    return qtrue;
}

/*
=================
SurfEdges
=================
*/
LOAD( SurfEdges ) {
    int         *in;
    msurfedge_t *out;
    int         i;
    int         index, vert;
    
    bsp->numsurfedges = count;
    bsp->surfedges = BSP_Malloc( sizeof( *out ) * count );

    in = base;
    out = bsp->surfedges;
    for( i = 0; i < count; i++, out++, in++ ) {
        index = ( signed int )LittleLong( *in );

		vert = 0;
		if( index < 0 ) {
			index = -index;
			vert = 1;
		}

		if( index >= bsp->numedges ) {
            BSP_SetError( "%s: bad edgenum", __func__ );
		}

        out->edge = bsp->edges + index;
        out->vert = vert;
    }
    return qtrue;
}

/*
=================
Faces
=================
*/
LOAD( Faces ) {
	dface_t *in;
	mface_t *out;
	int i;
#if USE_REF == REF_SOFT
	int j;
#endif
	unsigned texinfo, lightofs;
	unsigned firstedge, numedges, lastedge;
    unsigned planenum, side;

    bsp->numfaces = count;
    bsp->faces = BSP_Malloc( sizeof( *out ) * count );

    in = base;
    out = bsp->faces;
    for( i = 0; i < count; i++, in++, out++ ) {
		firstedge = LittleLong( in->firstedge );
		numedges = LittleShort( in->numedges );
        lastedge = firstedge + numedges;
		if( numedges < 3 || lastedge < firstedge || lastedge > bsp->numsurfedges ) {
			BSP_SetError( "%s: bad surfedges", __func__ );
            return qfalse;
		}
        out->firstsurfedge = bsp->surfedges + firstedge;
        out->numsurfedges = numedges;

        planenum = LittleShort( in->planenum );
        if( planenum >= bsp->numplanes ) {
            BSP_SetError( "%s: bad planenum", __func__ );
            return qfalse;
        }
        out->plane = bsp->planes + planenum;

		texinfo = LittleShort( in->texinfo );
		if( texinfo >= bsp->numtexinfo ) {
			BSP_SetError( "%s: bad texinfo", __func__ );
            return qfalse;
		}
        out->texinfo = bsp->texinfo + texinfo;

#if USE_REF == REF_SOFT
		for( j = 0; j < MAX_LIGHTMAPS; j++ ) {
			out->styles[j] = in->styles[j];
        }
#endif
        
        lightofs = LittleLong( in->lightofs );
        if( lightofs == ( uint32_t )-1 || bsp->numlightmapbytes == 0 ) {
            out->lightmap = NULL;
        } else {
#if USE_REF == REF_SOFT
	        // lighting info is converted from 24 bit on disk to 8 bit
            lightofs /= 3;
#endif
            if( lightofs >= bsp->numlightmapbytes ) {
                BSP_SetError( "%s: bad lightofs", __func__ );
                return qfalse;
            }
            out->lightmap = bsp->lightmap + lightofs;
        }

        side = LittleShort( in->side );
        out->drawflags = side & DSURF_PLANEBACK;
	}
    return qtrue;
}

/*
=================
LeafFaces
=================
*/
LOAD( LeafFaces ) {
    uint16_t    *in;
    mface_t     **out;
    int         i;
    unsigned    facenum;
    
    bsp->numleaffaces = count;
    bsp->leaffaces = BSP_Malloc( sizeof( *out ) * count );

    in = base;
    out = bsp->leaffaces;
    for( i = 0; i < count; i++, in++, out++ ) {
        facenum = LittleShort( *in );
        if( facenum >= bsp->numfaces ) {
            BSP_SetError( "%s: bad facenum", __func__ );
            return qfalse;
        }
        *out = bsp->faces + facenum;
    }
    return qtrue;
}
#endif

/*
=================
Leafs
=================
*/
LOAD( Leafs ) {
    dleaf_t     *in;
    mleaf_t     *out;
    int         i, cluster;
    unsigned    area, firstleafbrush, numleafbrushes;
#if USE_REF
    int         j;
    unsigned    firstleafface, numleaffaces;
#endif

    bsp->numleafs = count;
    bsp->leafs = BSP_Malloc( sizeof( *out ) * count );

    in = base;
    out = bsp->leafs;
    for( i = 0; i < count; i++, in++, out++ ) {
        out->plane = NULL;
        out->contents = LittleLong (in->contents);
        cluster = ( signed short )LittleShort (in->cluster);
        if( bsp->vis && cluster != -1 ) {
            if( cluster < 0 || cluster >= bsp->vis->numclusters ) {
                BSP_SetError( "%s: bad cluster", __func__ );
                return qfalse;
            }
        }
        out->cluster = cluster;

        area = LittleShort( in->area );
        if( area >= bsp->numareas ) {
            BSP_SetError( "%s: bad area", __func__ );
            return qfalse;
        }
        out->area = area;

        firstleafbrush = LittleShort (in->firstleafbrush);
        numleafbrushes = LittleShort (in->numleafbrushes);
        if( firstleafbrush + numleafbrushes > bsp->numleafbrushes ) {
            BSP_SetError( "%s: bad leafbrushes", __func__ );
            return qfalse;
        }
        out->firstleafbrush = bsp->leafbrushes + firstleafbrush;
        out->numleafbrushes = numleafbrushes;

#if USE_REF
        firstleafface = LittleShort (in->firstleafface);
        numleaffaces = LittleShort (in->numleaffaces);
        if( firstleafface + numleaffaces > bsp->numleaffaces ) {
            BSP_SetError( "%s: bad leaffaces", __func__ );
            return qfalse;
        }
        out->firstleafface = bsp->leaffaces + firstleafface;
        out->numleaffaces = numleaffaces;

		for( j = 0; j < 3; j++ ) {
            out->mins[j] = ( signed short )LittleShort( in->mins[j] );
            out->maxs[j] = ( signed short )LittleShort( in->maxs[j] );
        }

		out->parent = NULL;
		out->visframe = -1;
#endif
    }

    if (bsp->leafs[0].contents != CONTENTS_SOLID) {
        BSP_SetError( "%s: map leaf 0 is not CONTENTS_SOLID", __func__ );
        return qfalse;
    }
    return qtrue;
}

/*
=================
Nodes
=================
*/
LOAD( Nodes ) {
    dnode_t     *in;
    uint32_t    child;
    mnode_t     *out;
    int         i, j;
    unsigned    planeNum;
#if USE_REF
    unsigned    firstface, numfaces, lastface;
#endif

    if( !count ) {
        BSP_SetError( "%s: map with no nodes", __func__ );
        return qfalse;
    }
    
    bsp->numnodes = count;
    bsp->nodes = BSP_Malloc( sizeof( *out ) * count );

    in = base;
    out = bsp->nodes;
    for( i = 0; i < count; i++, out++, in++ ) {
        planeNum = LittleLong(in->planenum);
        if( planeNum >= bsp->numplanes ) {
            BSP_SetError( "%s: bad planenum", __func__ );
            return qfalse;
        }
        out->plane = bsp->planes + planeNum;

        for( j = 0; j < 2; j++ ) {
            child = LittleLong( in->children[j] );
            if( child & 0x80000000 ) {
                child = ~child;
                if( child >= bsp->numleafs ) {
                    BSP_SetError( "%s: bad leafnum", __func__ );
                    return qfalse;
                }
                out->children[j] = ( mnode_t * )( bsp->leafs + child );
            } else {
                if( child >= count ) {
                    BSP_SetError( "%s: bad nodenum", __func__ );
                    return qfalse;
                }
                out->children[j] = bsp->nodes + child;
            }
        }

#if USE_REF
		firstface = LittleLong( in->firstface );
		numfaces = LittleLong( in->numfaces );
        lastface = firstface + numfaces;
		if( lastface < firstface || lastface > bsp->numfaces ) {
			BSP_SetError( "%s: bad faces", __func__ );
            return qfalse;
		}
		out->firstface = bsp->faces + firstface;
		out->numfaces = numfaces;

		for( j = 0; j < 3; j++ ) {
            out->mins[j] = ( signed short )LittleShort( in->mins[j] );
            out->maxs[j] = ( signed short )LittleShort( in->maxs[j] );
        }

		out->parent = NULL;
		out->visframe = -1;
#endif
    }
    return qtrue;
}

/*
=================
Submodels
=================
*/
LOAD( Submodels ) {
    dmodel_t    *in;
    mmodel_t    *out;
    int         i, j;
    unsigned    headnode;
#if USE_REF
    unsigned    firstface, numfaces, lastface;
#endif

    bsp->models = BSP_Malloc( sizeof( *out ) * count );
    bsp->nummodels = count;

    in = base;
    out = bsp->models;
    for( i = 0; i < count; i++, in++, out++ ) {
        for( j = 0; j < 3; j++ ) {
            // spread the mins / maxs by a pixel
            out->mins[j] = LittleFloat (in->mins[j]) - 1;
            out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
            out->origin[j] = LittleFloat (in->origin[j]);
        }
        headnode = LittleLong (in->headnode);
        if( headnode >= bsp->numnodes ) {
            // FIXME: headnode may be garbage for some models
            Com_DPrintf( "%s: bad headnode\n", __func__ );
            out->headnode = NULL;
        } else {
            out->headnode = bsp->nodes + headnode;
        }
#if USE_REF
		firstface = LittleLong( in->firstface );
		numfaces = LittleLong( in->numfaces );
        lastface = firstface + numfaces;
		if( lastface < firstface || lastface > bsp->numfaces ) {
			BSP_SetError( "%s: bad faces", __func__ );
            return qfalse;
		}
		out->firstface = bsp->faces + firstface;
		out->numfaces = numfaces;

		out->radius = RadiusFromBounds( out->mins, out->maxs );
#endif
    }
    return qtrue;
}

/*
=================
AreaPortals
=================
*/
LOAD( AreaPortals ) {
    dareaportal_t   *in;
    mareaportal_t   *out;
    int         i;
    unsigned    portalnum, otherarea;

    bsp->numareaportals = count;
    bsp->areaportals = BSP_Malloc( sizeof( *out ) * count );

    in = base;
    out = bsp->areaportals;
    for( i = 0; i < count; i++, in++, out++ ) {
        portalnum = LittleLong (in->portalnum);
        if( portalnum >= MAX_MAP_AREAPORTALS ) {
            BSP_SetError( "%s: bad portalnum", __func__ );
            return qfalse;
        }
        out->portalnum = portalnum;
        otherarea = LittleLong (in->otherarea);
        if( otherarea >= MAX_MAP_AREAS ) {
            BSP_SetError( "%s: bad otherarea", __func__ );
            return qfalse;
        }
        out->otherarea = otherarea;
    }
    return qtrue;
}

/*
=================
Areas
=================
*/
LOAD( Areas ) {
    darea_t     *in;
    marea_t     *out;
    int         i;
    unsigned    numareaportals, firstareaportal, lastareaportal;

    bsp->numareas = count;
    bsp->areas = BSP_Malloc( sizeof( *out ) * count );

    in = base;
    out = bsp->areas;
    for( i = 0; i < count; i++, in++, out++ ) {
        numareaportals = LittleLong (in->numareaportals);
        firstareaportal = LittleLong (in->firstareaportal);
        lastareaportal = firstareaportal + numareaportals;
        if( lastareaportal < firstareaportal || lastareaportal > bsp->numareaportals ) {
            BSP_SetError( "%s: bad areaportals", __func__ );
            return qfalse;
        }
        out->numareaportals = numareaportals;
        out->firstareaportal = firstareaportal;
        out->floodvalid = 0;
    }
    return qtrue;
}

/*
=================
EntityString
=================
*/
LOAD( EntityString ) {
    char *path = map_override_path->string;

    // optionally load the entity string from external source
    if( *path ) {
        char buffer[MAX_QPATH], *str;
        char base[MAX_QPATH];
        size_t len;

        str = COM_SkipPath( bsp->name );
        COM_StripExtension( str, base, sizeof( base ) );
        Q_concat( buffer, sizeof( buffer ), path, base, ".ent", NULL );
        len = FS_LoadFile( buffer, ( void ** )&str );
        if( str ) {
            Com_DPrintf( "Loaded entity string from %s\n", buffer );
            bsp->entitystring = BSP_Malloc( len + 1 );
            memcpy( bsp->entitystring, str, len + 1 );
            bsp->numentitychars = len;
            FS_FreeFile( str );
            return qtrue;
        }
        Com_DPrintf( "Couldn't load entity string from %s\n", buffer );
    }

    bsp->numentitychars = count;
    bsp->entitystring = BSP_Malloc( count + 1 );
    memcpy( bsp->entitystring, base, count );
    bsp->entitystring[count] = 0;
    return qtrue;
}


/*
===============================================================================

                    MAP LOADING

===============================================================================
*/

typedef struct {
    qboolean (*load)( bsp_t *, void *, size_t );
    int lump;
    size_t size;
    size_t maxcount;
} lump_info_t;

#define LUMP( Func, Lump, Type ) \
    { BSP_Load##Func, LUMP_##Lump, sizeof( Type ), MAX_MAP_##Lump }

static const lump_info_t bsp_lumps[] = {
    LUMP( Visibility,   VISIBILITY,     byte            ),
    LUMP( Texinfo,      TEXINFO,        dtexinfo_t      ),
    LUMP( Planes,       PLANES,         dplane_t        ),
    LUMP( BrushSides,   BRUSHSIDES,     dbrushside_t    ),
    LUMP( Brushes,      BRUSHES,        dbrush_t        ),
    LUMP( LeafBrushes,  LEAFBRUSHES,    uint16_t        ),
    LUMP( AreaPortals,  AREAPORTALS,    dareaportal_t   ),
    LUMP( Areas,        AREAS,          darea_t         ),
#if USE_REF
	LUMP( Lightmap,     LIGHTING,       byte            ),
	LUMP( Vertices,     VERTEXES,       dvertex_t       ),
	LUMP( Edges,        EDGES,          dedge_t         ),
	LUMP( SurfEdges,    SURFEDGES,      uint32_t        ),
	LUMP( Faces,        FACES,          dface_t         ),
	LUMP( LeafFaces,    LEAFFACES,      uint16_t        ),
#endif
    LUMP( Leafs,        LEAFS,          dleaf_t         ),
    LUMP( Nodes,        NODES,          dnode_t         ),
    LUMP( Submodels,    MODELS,         dmodel_t        ),
    LUMP( EntityString, ENTSTRING,      char            ),
    { NULL }
};

static list_t   bsp_cache;
static char     bsp_error[MAX_QPATH];

static void BSP_List_f( void ) {
    bsp_t *bsp;
    size_t bytes;

    if( LIST_EMPTY( &bsp_cache ) ) {
        Com_Printf( "BSP cache is empty\n" );
        return;
    }

	Com_Printf( "------------------\n");
    bytes = 0;

    LIST_FOR_EACH( bsp_t, bsp, &bsp_cache, entry ) {
        Com_Printf( "%8"PRIz" : %s (%d refs)\n",
            bsp->pool.mapped, bsp->name, bsp->refcount );
        bytes += bsp->pool.mapped;
    }
	Com_Printf( "Total resident: %"PRIz"\n", bytes );
}

static bsp_t *BSP_Find( const char *name ) {
    bsp_t *bsp;

    LIST_FOR_EACH( bsp_t, bsp, &bsp_cache, entry ) {
        if( !FS_pathcmp( bsp->name, name ) ) {
            return bsp;
        }
    }
    return NULL;
}

static qboolean BSP_SetParent( mnode_t *node ) {
	mnode_t *child;
	
	while( node->plane ) {
		child = node->children[0];
        if( child->parent ) {
            return qfalse;
        }
		child->parent = node;
		BSP_SetParent( child );
		
		child = node->children[1];
        if( child->parent ) {
            return qfalse;
        }
		child->parent = node;
		node = child;
	}
    return qtrue;
}

void BSP_SetError( const char *fmt, ... ) {
	va_list		argptr;

	va_start( argptr, fmt );
	Q_vsnprintf( bsp_error, sizeof( bsp_error ), fmt, argptr );
	va_end( argptr );
}

const char *BSP_GetError( void ) {
    return bsp_error;
}

void BSP_Free( bsp_t *bsp ) {
    if( !bsp ) {
        return;
    }
    if( bsp->refcount <= 0 ) {
        Com_Error( ERR_FATAL, "%s: negative refcount", __func__ );
    }
    if( --bsp->refcount == 0 ) {
        Hunk_Free( &bsp->pool );
        List_Remove( &bsp->entry );
        Z_Free( bsp );
    }
}


/*
==================
BSP_Load

Loads in the map and all submodels
==================
*/
bsp_t *BSP_Load( const char *name ) {
    bsp_t           *bsp;
    byte            *buf;
    dheader_t       *header;
    const lump_info_t *info;
    size_t          filelen, ofs, len, end, count;

    if( !name || !name[0] ) {
        Com_Error( ERR_FATAL, "%s: NULL", __func__ );
    }

    BSP_SetError( "no error" );

    if( ( bsp = BSP_Find( name ) ) != NULL ) {
        bsp->refcount++;
        return bsp;
    }


    //
    // load the file
    //
    filelen = FS_LoadFile( name, ( void ** )&buf );
    if( !buf ) {
        BSP_SetError( "file not found" );
        return NULL;
    }

    len = strlen( name );
    bsp = Z_Mallocz( sizeof( *bsp ) + len );
    memcpy( bsp->name, name, len + 1 );
    bsp->refcount = 1;

    // byte swap and validate the header
    header = ( dheader_t * )buf;
    if( LittleLong( header->ident ) != IDBSPHEADER ) {
        BSP_SetError( "not an IBSP file" );
        goto fail2;
    }
    if( LittleLong( header->version ) != BSPVERSION ) {
        BSP_SetError( "unsupported IBSP version" );
        goto fail2;
    }
    
    // load into hunk
#if USE_REF
    Hunk_Begin( &bsp->pool, 0x1000000 );
#else
    Hunk_Begin( &bsp->pool, 0x556000 );
#endif

    // calculate the checksum
    bsp->checksum = LittleLong( Com_BlockChecksum( buf, filelen ) );

    // byte swap, validate and load all lumps
    for( info = bsp_lumps; info->load; info++ ) {
        ofs = LittleLong( header->lumps[info->lump].fileofs );
        len = LittleLong( header->lumps[info->lump].filelen );
        end = ofs + len;
        if( end < ofs || end > filelen ) {
            BSP_SetError( "lump %d extents are out of bounds", info->lump );
            goto fail1;
        }
        if( len % info->size ) {
            BSP_SetError( "lump %d has funny size", info->lump );
            goto fail1;
        }
        count = len / info->size;
        if( count > info->maxcount ) {
            BSP_SetError( "lump %d has too many elements", info->lump );
            goto fail1;
        }
        if( !info->load( bsp, buf + ofs, count ) ) {
            goto fail1;
        }
    }

    if( !BSP_SetParent( bsp->nodes ) ) {
        BSP_SetError( "cycle encountered in BSP graph" );
        goto fail1;
    }

    Hunk_End( &bsp->pool );

    FS_FreeFile( buf );

    List_Append( &bsp_cache, &bsp->entry );
    return bsp;

fail1:
    Hunk_Free( &bsp->pool );
fail2:
    FS_FreeFile( buf );
    Z_Free( bsp );
    return NULL;
}

/*
===============================================================================

HELPER FUNCTIONS

===============================================================================
*/

#if USE_REF

mface_t *BSP_LightPoint( mnode_t *node, vec3_t start, vec3_t end, int *ps, int *pt ) {
	vec_t startFrac, endFrac, midFrac;
	vec3_t _start, mid;
	int side;
	mface_t *surf;
	mtexinfo_t *texinfo;
	int i;
	int s, t;

    VectorCopy( start, _start );
	while( node->plane ) {
        // calculate distancies
        startFrac = PlaneDiffFast( _start, node->plane );
        endFrac = PlaneDiffFast( end, node->plane );
        side = ( startFrac < 0 );

        if( ( endFrac < 0 ) == side ) {
            // both points are one the same side
            node = node->children[side];
            continue;
        }

        // find crossing point
        midFrac = startFrac / ( startFrac - endFrac );
        LerpVector( _start, end, midFrac, mid );

        // check near side
        surf = BSP_LightPoint( node->children[side], _start, mid, ps, pt );
        if( surf ) {
            return surf;
        }

        for( i = 0, surf = node->firstface; i < node->numfaces; i++, surf++ ) {
            texinfo = surf->texinfo;
            if( texinfo->c.flags & SURF_NOLM_MASK ) {
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

            *ps = s;
            *pt = t;

            return surf;
        }

        // check far side
        VectorCopy( mid, _start );
        node = node->children[side^1];
    }

	return NULL;
}

#endif

byte *BSP_ClusterVis( bsp_t *bsp, byte *mask, int cluster, int vis ) {
    byte    *in, *out;
    int     c;

    if( !bsp || !bsp->vis ) {
        return memset( mask, 0xff, MAX_MAP_VIS );
    }
    if( cluster == -1 ) {
        return memset( mask, 0, bsp->visrowsize );
    }
    if( cluster < 0 || cluster >= bsp->vis->numclusters ) {
        Com_Error( ERR_DROP, "%s: bad cluster", __func__ );
    }

    // decompress vis
    in = ( byte * )bsp->vis + bsp->vis->bitofs[cluster][vis];
    out = mask;
    do {
        if( *in ) {
            *out++ = *in++;
            continue;
        }
    
        c = in[1];
        in += 2;
        if( ( out - mask ) + c > bsp->visrowsize ) {
            c = bsp->visrowsize - ( out - mask );
            Com_WPrintf( "%s: overrun\n", __func__ );
        }
        while( c ) {
            *out++ = 0;
            c--;
        }
    } while( out - mask < bsp->visrowsize );
    
    return mask;
}

mleaf_t *BSP_PointLeaf( mnode_t *node, vec3_t p ) {
    float d;

    while( node->plane ) {
        d = PlaneDiffFast( p, node->plane );
        if( d < 0 )
            node = node->children[1];
        else
            node = node->children[0];
    }

    return ( mleaf_t * )node;
}

/*
==================
BSP_InlineModel
==================
*/
mmodel_t *BSP_InlineModel( bsp_t *bsp, const char *name ) {
    int     num;

    if( !bsp || !name ) {
        Com_Error( ERR_DROP, "%s: NULL", __func__ );
    }
    if( name[0] != '*' ) {
        Com_Error( ERR_DROP, "%s: bad name: %s", __func__, name );
    }
    num = atoi( name + 1 );
    if( num < 1 || num >= bsp->nummodels ) {
        Com_Error ( ERR_DROP, "%s: bad number: %d", __func__, num );
    }

    return &bsp->models[num];
}

void BSP_Init( void ) {
    map_override_path = Cvar_Get( "map_override_path", "", 0 );

    Cmd_AddCommand( "bsplist", BSP_List_f );

    List_Init( &bsp_cache );
}

