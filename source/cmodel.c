/*
Copyright (C) 1997-2001 Id Software, Inc.

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
// cmodel.c -- model loading

#include "com_local.h"

#define CM_Malloc( size )   Z_TagMalloc( size, TAG_CMODEL )
#define CM_Mallocz( size )  Z_TagMallocz( size, TAG_CMODEL )
#define CM_Free( ptr )      Z_Free( ptr )

static mapsurface_t nullsurface;
static cleaf_t      nullleaf;

static int          floodvalid;
static int          checkcount;

static cvar_t       *map_noareas;
static cvar_t       *map_override;

void    CM_FloodAreaConnections( cm_t *cm );

typedef struct {
    void *base;
    size_t count;
} cmlump_t;

typedef struct {
    size_t size;
    size_t mincount;
    size_t maxcount;
    const char *name;
} lump_info_t;

static const lump_info_t lump_info[HEADER_LUMPS] = {
    { 1, 0, MAX_MAP_ENTSTRING, "entities" },
    { sizeof( dplane_t ), 1, MAX_MAP_PLANES, "planes" },
    { sizeof( dvertex_t ), 0, MAX_MAP_VERTS, "verts" },
    { 1, 0, MAX_MAP_VISIBILITY, "visibility" },
    { sizeof( dnode_t ), 1, MAX_MAP_NODES, "nodes" },
    { sizeof( texinfo_t ), 1, MAX_MAP_TEXINFO, "texinfo" },
    { sizeof( dface_t ), 0, MAX_MAP_FACES, "faces" },
    { 1, 0, MAX_MAP_LIGHTING, "lighting" },
    { sizeof( dleaf_t ), 1, MAX_MAP_LEAFS, "leafs" },
    { sizeof( uint16_t ), 0, MAX_MAP_LEAFFACES, "leaf faces" },
    { sizeof( uint16_t ), 1, MAX_MAP_LEAFBRUSHES, "leaf brushes" },
    { sizeof( dedge_t ), 0, MAX_MAP_EDGES, "edges" },
    { sizeof( uint32_t ), 0, MAX_MAP_SURFEDGES, "surf edges" },
    { sizeof( dmodel_t ), 1, MAX_MAP_MODELS, "models" },
    { sizeof( dbrush_t ), 1, MAX_MAP_BRUSHES, "brushes" },
    { sizeof( dbrushside_t ), 1, MAX_MAP_BRUSHSIDES, "brush sides" },
    { 0, 0, 0, NULL },
    { sizeof( darea_t ), 0, MAX_MAP_AREAS, "areas" },
    { sizeof( dareaportal_t ), 0, MAX_MAP_AREAPORTALS, "area portals" }
};

/*
===============================================================================

                    MAP LOADING

===============================================================================
*/

#define CM_FUNC( Func ) \
    static qboolean CM_Load##Func( cmcache_t *cache, cmlump_t *l )

/*
=================
CM_LoadSubmodels
=================
*/
CM_FUNC( Submodels ) {
    dmodel_t    *in;
    cmodel_t    *out;
    int         i, j;
    unsigned    headnode;

    cache->cmodels = CM_Malloc( sizeof( *out ) * l->count );
    cache->numcmodels = l->count;

    in = l->base;
    out = cache->cmodels;
    for( i = 0; i < l->count; i++, in++, out++ ) {
        for( j = 0; j < 3; j++ ) {
            // spread the mins / maxs by a pixel
            out->mins[j] = LittleFloat (in->mins[j]) - 1;
            out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
            out->origin[j] = LittleFloat (in->origin[j]);
        }
        headnode = LittleLong (in->headnode);
        if( headnode >= cache->numnodes ) {
            // FIXME: headnode may be garbage for some models
            Com_DPrintf( "%s: bad headnode for model %d\n", __func__, i );
            out->headnode = NULL;
        } else {
            out->headnode = cache->nodes + headnode;
        }
    }
    return qtrue;
}


/*
=================
CM_LoadSurfaces
=================
*/
CM_FUNC( Surfaces ) {
    texinfo_t   *in;
    mapsurface_t    *out;
    int         i;

    cache->numtexinfo = l->count;
    cache->surfaces = CM_Malloc( sizeof( *out ) * l->count );

    in = l->base;
    out = cache->surfaces;
    for( i = 0; i < l->count; i++, in++, out++ ) {
        memcpy( out->c.name, in->texture, sizeof( out->c.name ) );
        out->c.name[ sizeof( out->c.name ) - 1 ] = 0;
        memcpy( out->rname, in->texture, sizeof( out->rname ) );
        out->rname[ sizeof( out->rname ) - 1 ] = 0;
        out->c.flags = LittleLong (in->flags);
        out->c.value = LittleLong (in->value);
    }
    return qtrue;
}


/*
=================
CM_LoadNodes
=================
*/
CM_FUNC( Nodes ) {
    dnode_t     *in;
    uint32_t    child;
    cnode_t     *out;
    int         i, j;
    unsigned    planeNum;
    
    cache->numnodes = l->count;
    cache->nodes = CM_Malloc( sizeof( *out ) * l->count );

    in = l->base;
    out = cache->nodes;
    for( i = 0; i < l->count; i++, out++, in++ ) {
        planeNum = LittleLong(in->planenum);
        if( planeNum >= cache->numplanes ) {
            Com_DPrintf( "%s: bad planenum\n", __func__ );
            return qfalse;
        }
        out->plane = cache->planes + planeNum;

        for( j = 0; j < 2; j++ ) {
            child = LittleLong( in->children[j] );
            if( child & 0x80000000 ) {
                child = ~child;
                if( child >= cache->numleafs ) {
                    Com_DPrintf( "%s: bad leafnum\n", __func__ );
                    return qfalse;
                }
                out->children[j] = ( cnode_t * )( cache->leafs + child );
            } else {
                if( child >= l->count ) {
                    Com_DPrintf( "%s: bad nodenum\n", __func__ );
                    return qfalse;
                }
                out->children[j] = cache->nodes + child;
            }
        }
    }
    return qtrue;
}

/*
=================
CM_LoadBrushes
=================
*/
CM_FUNC( Brushes ) {
    dbrush_t    *in;
    cbrush_t    *out;
    int         i;
    unsigned    firstSide, numSides, lastSide;
    
    cache->numbrushes = l->count;
    cache->brushes = CM_Malloc( sizeof( *out ) * l->count );

    in = l->base;
    out = cache->brushes;
    for( i = 0; i < l->count; i++, out++, in++ ) {
        firstSide = LittleLong(in->firstside);
        numSides = LittleLong(in->numsides);
        lastSide = firstSide + numSides;
        if( lastSide < firstSide || lastSide > cache->numbrushsides ) {
            Com_DPrintf( "%s: bad brushsides\n", __func__ );
            return qfalse;
        }
        out->firstbrushside = cache->brushsides + firstSide;
        out->numsides = numSides;
        out->contents = LittleLong(in->contents);
        out->checkcount = 0;
    }
    return qtrue;
}

/*
=================
CM_LoadLeafs
=================
*/
CM_FUNC( Leafs ) {
    cleaf_t     *out;
    dleaf_t     *in;
    int         i;
    unsigned    areaNum, firstBrush, numBrushes;
    
    cache->numleafs = l->count;
    cache->numclusters = 0;
    cache->leafs = CM_Malloc( sizeof( *out ) * l->count );

    in = l->base;
    out = cache->leafs;
    for( i = 0; i < l->count; i++, in++, out++ ) {
        out->plane = NULL;
        out->contents = LittleLong (in->contents);
        out->cluster = ( signed short )LittleShort (in->cluster);
        areaNum = LittleShort (in->area);
        if( areaNum >= cache->numareas ) {
            Com_DPrintf( "%s: bad areanum\n", __func__ );
            return qfalse;
        }
        out->area = areaNum;

        firstBrush = LittleShort (in->firstleafbrush);
        numBrushes = LittleShort (in->numleafbrushes);
        if( firstBrush + numBrushes > cache->numleafbrushes ) {
            Com_DPrintf( "%s: bad brushnum\n", __func__ );
            return qfalse;
        }

        out->firstleafbrush = cache->leafbrushes + firstBrush;
        out->numleafbrushes = numBrushes;

        if (out->cluster >= cache->numclusters)
            cache->numclusters = out->cluster + 1;
    }

    if (cache->leafs[0].contents != CONTENTS_SOLID) {
        Com_DPrintf( "%s: map leaf 0 is not CONTENTS_SOLID\n", __func__ );
        return qfalse;
    }
    return qtrue;
}

/*
=================
CM_LoadPlanes
=================
*/
CM_FUNC( Planes ) {
    cplane_t    *out;
    dplane_t    *in;
    int         i;
    
    cache->numplanes = l->count;
    cache->planes = CM_Malloc( sizeof( *out ) * l->count );

    in = l->base;
    out = cache->planes;
    for( i = 0; i < l->count; i++, in++, out++ ) {
        out->normal[0] = LittleFloat( in->normal[0] );
        out->normal[1] = LittleFloat( in->normal[1] );
        out->normal[2] = LittleFloat( in->normal[2] );
        out->dist = LittleFloat (in->dist);
        SetPlaneType( out );
        SetPlaneSignbits( out );
    }
    return qtrue;
}

/*
=================
CMLoadLeafBrushes
=================
*/
CM_FUNC( LeafBrushes ) {
    cbrush_t    **out;
    uint16_t    *in;
    int         i;
    unsigned    brushNum;
    
    cache->numleafbrushes = l->count;
    cache->leafbrushes = CM_Malloc( sizeof( *out ) * l->count );

    in = l->base;
    out = cache->leafbrushes;
    for( i = 0; i < l->count; i++, in++, out++ ) {
        brushNum = LittleShort (*in);
        if( brushNum >= cache->numbrushes ) {
            Com_DPrintf( "%s: bad brushnum\n", __func__ );
            return qfalse;
        }
        *out = cache->brushes + brushNum;
    }
    return qtrue;
}

/*
=================
CM_LoadBrushSides
=================
*/
CM_FUNC( BrushSides ) {
    cbrushside_t    *out;
    dbrushside_t    *in;
    int         i;
    unsigned    planeNum, texinfoNum;

    cache->numbrushsides = l->count;
    cache->brushsides = CM_Malloc( sizeof( *out ) * l->count );

    in = l->base;
    out = cache->brushsides;
    for( i = 0; i < l->count; i++, in++, out++ ) {
        planeNum = LittleShort (in->planenum);
        if( planeNum >= cache->numplanes ) {
            Com_DPrintf( "%s: bad planenum\n", __func__ );
            return qfalse;
        }
        out->plane = cache->planes + planeNum;
        texinfoNum = LittleShort (in->texinfo);
        if( texinfoNum == ( uint16_t )-1 ) {
            out->surface = &nullsurface;
        } else {
            if (texinfoNum >= cache->numtexinfo) {
                Com_DPrintf( "%s: bad texinfo\n", __func__ );
                return qfalse;
            }
            out->surface = cache->surfaces + texinfoNum;
        }
    }
    return qtrue;
}

/*
=================
CM_LoadAreas
=================
*/
CM_FUNC( Areas ) {
    carea_t     *out;
    darea_t     *in;
    int         i;
    unsigned    numPortals, firstPortal, lastPortal;

    cache->numareas = l->count;
    cache->areas = CM_Malloc( sizeof( *out ) * l->count );

    in = l->base;
    out = cache->areas;
    for( i = 0; i < l->count; i++, in++, out++ ) {
        numPortals = LittleLong (in->numareaportals);
        firstPortal = LittleLong (in->firstareaportal);
        lastPortal = firstPortal + numPortals;
        if( lastPortal < firstPortal || lastPortal > cache->numareaportals ) {
            Com_DPrintf( "%s: bad areaportals\n", __func__ );
            return qfalse;
        }
        out->numareaportals = numPortals;
        out->firstareaportal = firstPortal;
        out->floodvalid = 0;
    }
    return qtrue;
}

/*
=================
CM_LoadAreaPortals
=================
*/
CM_FUNC( AreaPortals ) {
    careaportal_t   *out;
    dareaportal_t   *in;
    int         i;
    unsigned    portalNum, otherArea;

    cache->numareaportals = l->count;
    cache->areaportals = CM_Malloc( sizeof( *out ) * l->count );

    in = l->base;
    out = cache->areaportals;
    for( i = 0; i < l->count; i++, in++, out++ ) {
        portalNum = LittleLong (in->portalnum);
        if( portalNum >= MAX_MAP_AREAPORTALS ) {
            Com_DPrintf( "%s: bad portalnum\n", __func__ );
        }
        out->portalnum = portalNum;
        otherArea = LittleLong (in->otherarea);
        if( otherArea >= MAX_MAP_AREAS ) {
            Com_DPrintf( "%s: bad otherarea\n", __func__ );
        }
        out->otherarea = otherArea;
    }
    return qtrue;
}

/*
=================
CM_LoadVisibility
=================
*/
CM_FUNC( Visibility ) {
    unsigned numClusters;
    int i;

    if( !l->count ) {
        cache->numvisibility = cache->visrowsize = 0;
        return qtrue;
    }

    cache->numvisibility = l->count;
    cache->vis = CM_Malloc( l->count );
    memcpy( cache->vis, l->base, l->count );

    numClusters = LittleLong( cache->vis->numclusters );
    cache->vis->numclusters = numClusters;
    for( i = 0; i < numClusters; i++ ) {
        cache->vis->bitofs[i][0] = LittleLong( cache->vis->bitofs[i][0] );
        cache->vis->bitofs[i][1] = LittleLong( cache->vis->bitofs[i][1] );
    }

    cache->visrowsize = ( numClusters + 7 ) >> 3;
    return qtrue;
}


/*
=================
CM_LoadEntityString
=================
*/
CM_FUNC( EntityString ) {
    cache->numEntityChars = l->count;
    cache->entitystring = CM_Malloc( l->count + 1 );
    memcpy( cache->entitystring, l->base, l->count );
    cache->entitystring[l->count] = 0;
    return qtrue;
}

#define CM_CACHESIZE        16

static cmcache_t    cm_cache[CM_CACHESIZE];

static void CM_FreeCache( cmcache_t *cache ) {
#define CM_FREE( f )    if( cache->f ) CM_Free( cache->f )

    CM_FREE( vis );
    CM_FREE( surfaces );
    CM_FREE( planes );
    CM_FREE( brushsides );
    CM_FREE( brushes );
    CM_FREE( leafbrushes );
    CM_FREE( areaportals );
    CM_FREE( areas );
    CM_FREE( leafs );
    CM_FREE( nodes );
    CM_FREE( cmodels );
    CM_FREE( entitystring );

    memset( cache, 0, sizeof( *cache ) );
}

void CM_FreeMap( cm_t *cm ) {
    cmcache_t *cache;

    if( cm->floodnums ) {
        Z_Free( cm->floodnums );
    }

    cache = cm->cache;
    if( cache ) {
        if( cache->refcount <= 0 ) {
            Com_Error( ERR_FATAL, "CM_FreeMap: negative refcount" );
        }
        if( --cache->refcount == 0 ) {
            CM_FreeCache( cache );
        }
    }

    memset( cm, 0, sizeof( *cm ) );
}

static void CM_AllocPortals( cm_t *cm, int flags ) {
    if( flags & CM_LOAD_CLIENT ) {
        cm->floodnums = NULL;
        cm->portalopen = NULL;
        return;
    }
    cm->floodnums = CM_Mallocz( sizeof( int ) * cm->cache->numareas +
        sizeof( qboolean ) * cm->cache->numareaportals );
    cm->portalopen = ( qboolean * )( cm->floodnums + cm->cache->numareas );
    CM_FloodAreaConnections( cm );
}

typedef struct {
    qboolean (*func)( cmcache_t *, cmlump_t * );
    int lump;
} lump_load_t;

#define CM_LOAD( Func, Lump )   { CM_Load##Func, LUMP_##Lump }

static const lump_load_t lump_load[] = {
    CM_LOAD( Visibility, VISIBILITY ),
    CM_LOAD( Surfaces, TEXINFO ),
    CM_LOAD( Planes, PLANES ),
    CM_LOAD( BrushSides, BRUSHSIDES ),
    CM_LOAD( Brushes, BRUSHES ),
    CM_LOAD( LeafBrushes, LEAFBRUSHES ),
    CM_LOAD( AreaPortals, AREAPORTALS ),
    CM_LOAD( Areas, AREAS ),
    CM_LOAD( Leafs, LEAFS ),
    CM_LOAD( Nodes, NODES ),
    CM_LOAD( Submodels, MODELS ),
    { NULL }
};

/*
==================
CM_LoadMap

Loads in the map and all submodels
==================
*/
const char *CM_LoadMapEx( cm_t *cm, const char *name, int flags, uint32_t *checksum ) {
    cmcache_t       *cache;
    byte            *buf;
    int             i;
    dheader_t       *header;
    lump_t          *in;
    cmlump_t         lumps[HEADER_LUMPS];
    cmlump_t         *out;
    const lump_info_t *info;
    const lump_load_t *load;
    size_t          length, endpos, fileofs, filelen;
    char            *error;

    if( !name || !name[0] ) {
        Com_Error( ERR_FATAL, "CM_LoadMap: NULL name" );
    }

    memset( cm, 0, sizeof( *cm ) );

    for( i = 0, cache = cm_cache; i < CM_CACHESIZE; i++, cache++ ) {
        if( !cache->name[0] ) {
            continue;
        }
        if( strcmp( cache->name, name ) ) {
            continue;
        }
        //if( !( cache->flags & CM_LOAD_VISONLY ) || ( flags & CM_LOAD_VISONLY ) )
        {
            cm->cache = cache;
            if( checksum ) {
                *checksum = cache->checksum;
            }
            CM_AllocPortals( cm, flags );
            cache->refcount++;
            return NULL; // still have the right version
        }
        break;
    }

    // find a free slot
    for( i = 0, cache = cm_cache; i < CM_CACHESIZE; i++, cache++ ) {
        if( !cache->name[0] ) {
            break;
        }
    }
    if( i == CM_CACHESIZE ) {
        return "out of cache slots";
    }

    //
    // load the file
    //
    length = FS_LoadFile( name, (void **)&buf );
    if( !buf ) {
        return "file not found";
    }

    cache->checksum = LittleLong( Com_BlockChecksum( buf, length ) );
    if( checksum ) {
        *checksum = cache->checksum;
    }

    // byte swap and validate the header
    header = ( dheader_t * )buf;
    if( LittleLong( header->ident ) != IDBSPHEADER ) {
        error = "not an IBSP file";
        goto fail;
    }
    if( LittleLong( header->version ) != BSPVERSION ) {
        error = "wrong IBSP version";
        goto fail;
    }
    
    // byte swap and validate lumps
    in = header->lumps;
    out = lumps;
    info = lump_info;
    for( i = 0; i < HEADER_LUMPS; i++, in++, out++, info++ ) {
        if( !info->size ) {
            continue;
        }
        fileofs = LittleLong( in->fileofs );
        filelen = LittleLong( in->filelen );
        endpos = fileofs + filelen;
        if( endpos < fileofs || endpos > length ) {
            error = va( "%s lump is out of bounds", info->name );
            goto fail;
        }
        out->base = buf + fileofs;
        if( filelen % info->size ) {
            error = va( "%s lump has funny size", info->name );
            goto fail;
        }
        out->count = filelen / info->size;
        if( out->count < info->mincount || out->count > info->maxcount ) {
            error = va( "%s lump has bad number of elements", info->name );
            goto fail;
        }
    }


    // load into heap
    if( !( flags & CM_LOAD_ENTONLY ) ) {
        for( load = lump_load; load->func; load++ ) {
            if( !load->func( cache, &lumps[load->lump] ) ) {
                error = va( "%s lump has invalid structure", lump_info[load->lump].name );
                goto fail;
            }
        }
    }

    // optionally load the entity string from external source
    if( map_override->integer && !( flags & CM_LOAD_CLIENT ) ) {
        char *entstring;
        char buffer[MAX_QPATH];

        COM_StripExtension( name, buffer, sizeof( buffer ) );
        Q_strcat( buffer, sizeof( buffer ), ".ent" );
        length = FS_LoadFileEx( buffer, ( void ** )&entstring, 0, TAG_CMODEL );
        if( entstring ) {
            Com_DPrintf( "Loaded entity string from %s\n", buffer );
            cache->entitystring = entstring;
            cache->numEntityChars = length;
        } else {
            CM_LoadEntityString( cache, &lumps[LUMP_ENTITIES] );
        }
    } else {
        CM_LoadEntityString( cache, &lumps[LUMP_ENTITIES] );
    }

    FS_FreeFile( buf );

    Q_strncpyz( cache->name, name, sizeof( cache->name ) );

    cache->refcount = 1;
    cm->cache = cache;

    CM_AllocPortals( cm, flags );

    return NULL;

fail:
    FS_FreeFile( buf );
    CM_FreeCache( cache );
    return error;
}

void CM_LoadMap( cm_t *cm, const char *name, int flags, uint32_t *checksum ) {
    const char *error = CM_LoadMapEx( cm, name, flags, checksum );

    if( error ) {
        Com_Error( ERR_DROP, "Couldn't load %s: %s", name, error );
    }
}

/*
==================
CM_InlineModel
==================
*/
cmodel_t *CM_InlineModel( cm_t *cm, const char *name ) {
    int     num;
    cmodel_t *cmodel;

    if( !name || name[0] != '*' ) {
        Com_Error( ERR_DROP, "%s: bad name: %s", __func__, name );
    }
    if( !cm->cache ) {
        Com_Error( ERR_DROP, "%s: NULL cache", __func__ );
    }
    num = atoi( name + 1 );
    if( num < 1 || num >= cm->cache->numcmodels ) {
        Com_Error ( ERR_DROP, "%s: bad number: %d", __func__, num );
    }

    cmodel = &cm->cache->cmodels[num];

    return cmodel;
}

int CM_NumClusters( cm_t *cm ) {
    if( !cm->cache ) {
        return 0;
    }
    return cm->cache->numclusters;
}

int CM_NumInlineModels( cm_t *cm ) {
    if( !cm->cache ) {
        return 0;
    }
    return cm->cache->numcmodels;
}

char *CM_EntityString( cm_t *cm ) {
    if( !cm->cache ) {
        return "";
    }
    if( !cm->cache->entitystring ) {
        return "";
    }
    return cm->cache->entitystring;
}

cnode_t *CM_NodeNum( cm_t *cm, int number ) {
    if( !cm->cache ) {
        Com_Error( ERR_DROP, "%s: NULL cache", __func__ );
    }
    if( number == -1 ) {
        return ( cnode_t * )cm->cache->leafs; // special case for solid leaf
    }
    if( number < 0 || number >= cm->cache->numnodes ) {
        Com_Error( ERR_DROP, "%s: bad number: %d", __func__, number );
    }
    return cm->cache->nodes + number;
}

cleaf_t *CM_LeafNum( cm_t *cm, int number ) {
    if( !cm->cache ) {
        Com_Error( ERR_DROP, "%s: NULL cache", __func__ );
    }
    if( number < 0 || number >= cm->cache->numleafs ) {
        Com_Error( ERR_DROP, "%s: bad number: %d", __func__, number );
    }
    return cm->cache->leafs + number;
}

//=======================================================================

static cplane_t box_planes[12];
static cnode_t  box_nodes[6];
static cnode_t  *box_headnode;
static cbrush_t box_brush;
static cbrush_t *box_leafbrush;
static cbrushside_t box_brushsides[6];
static cleaf_t  box_leaf;
static cleaf_t  box_emptyleaf;

/*
===================
CM_InitBoxHull

Set up the planes and nodes so that the six floats of a bounding box
can just be stored out and get a proper clipping hull structure.
===================
*/
static void CM_InitBoxHull( void ) {
    int         i;
    int         side;
    cnode_t     *c;
    cplane_t    *p;
    cbrushside_t    *s;

    box_headnode = &box_nodes[0];

    box_brush.numsides = 6;
    box_brush.firstbrushside = &box_brushsides[0];
    box_brush.contents = CONTENTS_MONSTER;

    box_leaf.contents = CONTENTS_MONSTER;
    box_leaf.firstleafbrush = &box_leafbrush;
    box_leaf.numleafbrushes = 1;

    box_leafbrush = &box_brush;

    for( i = 0; i < 6; i++ ) {
        side = i & 1;

        // brush sides
        s = &box_brushsides[i];
        s->plane = &box_planes[i*2+side];
        s->surface = &nullsurface;

        // nodes
        c = &box_nodes[i];
        c->plane = &box_planes[i*2];
        c->children[side] = ( cnode_t * )&box_emptyleaf;
        if( i != 5 )
            c->children[side^1] = &box_nodes[i + 1];
        else
            c->children[side^1] = ( cnode_t * )&box_leaf;

        // planes
        p = &box_planes[i*2];
        p->type = i >> 1;
        p->signbits = 0;
        VectorClear( p->normal );
        p->normal[i>>1] = 1;

        p = &box_planes[i*2+1];
        p->type = 3 + ( i >> 1 );
        p->signbits = 0;
        VectorClear( p->normal );
        p->normal[i>>1] = -1;
    }   
}


/*
===================
CM_HeadnodeForBox

To keep everything totally uniform, bounding boxes are turned into small
BSP trees instead of being compared directly.
===================
*/
cnode_t *CM_HeadnodeForBox( vec3_t mins, vec3_t maxs ) {
    box_planes[0].dist = maxs[0];
    box_planes[1].dist = -maxs[0];
    box_planes[2].dist = mins[0];
    box_planes[3].dist = -mins[0];
    box_planes[4].dist = maxs[1];
    box_planes[5].dist = -maxs[1];
    box_planes[6].dist = mins[1];
    box_planes[7].dist = -mins[1];
    box_planes[8].dist = maxs[2];
    box_planes[9].dist = -maxs[2];
    box_planes[10].dist = mins[2];
    box_planes[11].dist = -mins[2];

    return box_headnode;
}


/*
==================
CM_PointLeaf_r

==================
*/
static cleaf_t *CM_PointLeaf_r( vec3_t p, cnode_t *node ) {
    float       d;

    while( node->plane ) {
        d = PlaneDiffFast( p, node->plane );
        if( d < 0 )
            node = node->children[1];
        else
            node = node->children[0];
    }

    return ( cleaf_t * )node;
}

cleaf_t *CM_PointLeaf( cm_t *cm, vec3_t p ) {
    if( !cm->cache ) {
        return &nullleaf;       // server may call this without map loaded
    }
    return CM_PointLeaf_r( p, cm->cache->nodes );
}


/*
=============
CM_BoxLeafnums

Fills in a list of all the leafs touched
=============
*/
static int      leaf_count, leaf_maxcount;
static cleaf_t  **leaf_list;
static float    *leaf_mins, *leaf_maxs;
static cnode_t  *leaf_topnode;

static void CM_BoxLeafs_r( cnode_t *node ) {
    int     s;

    while( node->plane ) {
        s = BoxOnPlaneSideFast( leaf_mins, leaf_maxs, node->plane );
        if( s == 1 ) {
            node = node->children[0];
        } else if( s == 2 ) {
            node = node->children[1];
        } else {
            // go down both
            if( !leaf_topnode ) {
                leaf_topnode = node;
            }
            CM_BoxLeafs_r( node->children[0] );
            node = node->children[1];
        }
    }

    if( leaf_count < leaf_maxcount ) {
        leaf_list[leaf_count++] = ( cleaf_t * )node;
    }
}

static int CM_BoxLeafs_headnode( vec3_t mins, vec3_t maxs, cleaf_t **list, int listsize,
                                    cnode_t *headnode, cnode_t **topnode )
{
    leaf_list = list;
    leaf_count = 0;
    leaf_maxcount = listsize;
    leaf_mins = mins;
    leaf_maxs = maxs;

    leaf_topnode = NULL;

    CM_BoxLeafs_r( headnode );

    if( topnode )
        *topnode = leaf_topnode;

    return leaf_count;
}

int CM_BoxLeafs( cm_t *cm, vec3_t mins, vec3_t maxs, cleaf_t **list, int listsize, cnode_t **topnode ) {
    if( !cm->cache )    // map not loaded
        return 0;
    return CM_BoxLeafs_headnode( mins, maxs, list,
        listsize, cm->cache->nodes, topnode );
}



/*
==================
CM_PointContents

==================
*/
int CM_PointContents( vec3_t p, cnode_t *headnode ) {
    cleaf_t     *leaf;

    if( !headnode ) {
        return 0;
    }

    leaf = CM_PointLeaf_r( p, headnode );

    return leaf->contents;
}

/*
==================
CM_TransformedPointContents

Handles offseting and rotation of the end points for moving and
rotating entities
==================
*/
int CM_TransformedPointContents( vec3_t p, cnode_t *headnode, vec3_t origin, vec3_t angles ) {
    vec3_t      p_l;
    vec3_t      temp;
    vec3_t      forward, right, up;
    cleaf_t     *leaf;

    if( !headnode ) {
        return 0;
    }

    // subtract origin offset
    VectorSubtract (p, origin, p_l);

    // rotate start and end into the models frame of reference
    if (headnode != box_headnode && 
    (angles[0] || angles[1] || angles[2]) )
    {
        AngleVectors (angles, forward, right, up);

        VectorCopy (p_l, temp);
        p_l[0] = DotProduct (temp, forward);
        p_l[1] = -DotProduct (temp, right);
        p_l[2] = DotProduct (temp, up);
    }

    leaf = CM_PointLeaf_r( p_l, headnode );

    return leaf->contents;
}


/*
===============================================================================

BOX TRACING

===============================================================================
*/

// 1/32 epsilon to keep floating point happy
#define DIST_EPSILON    (0.03125)

static vec3_t   trace_start, trace_end;
static vec3_t   trace_mins, trace_maxs;
static vec3_t   trace_extents;

static trace_t  *trace_trace;
static int      trace_contents;
static qboolean trace_ispoint;      // optimized case

/*
================
CM_ClipBoxToBrush
================
*/
static void CM_ClipBoxToBrush (vec3_t mins, vec3_t maxs, vec3_t p1, vec3_t p2,
                      trace_t *trace, cbrush_t *brush)
{
    int         i, j;
    cplane_t    *plane, *clipplane;
    float       dist;
    float       enterfrac, leavefrac;
    vec3_t      ofs;
    float       d1, d2;
    qboolean    getout, startout;
    float       f;
    cbrushside_t    *side, *leadside;

    enterfrac = -1;
    leavefrac = 1;
    clipplane = NULL;

    if (!brush->numsides)
        return;

    getout = qfalse;
    startout = qfalse;
    leadside = NULL;

    side = brush->firstbrushside;
    for (i=0 ; i<brush->numsides ; i++, side++)
    {
        plane = side->plane;

        // FIXME: special case for axial

        if (!trace_ispoint)
        {   // general box case

            // push the plane out apropriately for mins/maxs

            // FIXME: use signbits into 8 way lookup for each mins/maxs
            for (j=0 ; j<3 ; j++)
            {
                if (plane->normal[j] < 0)
                    ofs[j] = maxs[j];
                else
                    ofs[j] = mins[j];
            }
            dist = DotProduct (ofs, plane->normal);
            dist = plane->dist - dist;
        }
        else
        {   // special point case
            dist = plane->dist;
        }

        d1 = DotProduct (p1, plane->normal) - dist;
        d2 = DotProduct (p2, plane->normal) - dist;

        if (d2 > 0)
            getout = qtrue; // endpoint is not in solid
        if (d1 > 0)
            startout = qtrue;

        // if completely in front of face, no intersection
        if (d1 > 0 && d2 >= d1)
            return;

        if (d1 <= 0 && d2 <= 0)
            continue;

        // crosses face
        if (d1 > d2)
        {   // enter
            f = (d1-DIST_EPSILON) / (d1-d2);
            if (f > enterfrac)
            {
                enterfrac = f;
                clipplane = plane;
                leadside = side;
            }
        }
        else
        {   // leave
            f = (d1+DIST_EPSILON) / (d1-d2);
            if (f < leavefrac)
                leavefrac = f;
        }
    }

    if (!startout)
    {   // original point was inside brush
        trace->fraction = 0;
        trace->startsolid = qtrue;
        if (!getout)
            trace->allsolid = qtrue;
        return;
    }
    if (enterfrac < leavefrac)
    {
        if (enterfrac > -1 && enterfrac < trace->fraction)
        {
            if (enterfrac < 0)
                enterfrac = 0;
            trace->fraction = enterfrac;
            trace->plane = *clipplane;
            trace->surface = &(leadside->surface->c);
            trace->contents = brush->contents;
        }
    }
}

/*
================
CM_TestBoxInBrush
================
*/
static void CM_TestBoxInBrush (vec3_t mins, vec3_t maxs, vec3_t p1,
                      trace_t *trace, cbrush_t *brush)
{
    int         i, j;
    cplane_t    *plane;
    float       dist;
    vec3_t      ofs;
    float       d1;
    cbrushside_t    *side;

    if (!brush->numsides)
        return;

    side = brush->firstbrushside;
    for (i=0 ; i<brush->numsides ; i++, side++)
    {
        plane = side->plane;

        // FIXME: special case for axial

        // general box case

        // push the plane out apropriately for mins/maxs

        // FIXME: use signbits into 8 way lookup for each mins/maxs
        for (j=0 ; j<3 ; j++)
        {
            if (plane->normal[j] < 0)
                ofs[j] = maxs[j];
            else
                ofs[j] = mins[j];
        }
        dist = DotProduct (ofs, plane->normal);
        dist = plane->dist - dist;

        d1 = DotProduct (p1, plane->normal) - dist;

        // if completely in front of face, no intersection
        if (d1 > 0)
            return;

    }

    // inside this brush
    trace->startsolid = trace->allsolid = qtrue;
    trace->fraction = 0;
    trace->contents = brush->contents;
}


/*
================
CM_TraceToLeaf
================
*/
static void CM_TraceToLeaf ( cleaf_t *leaf )
{
    int         k;
    cbrush_t    *b, **leafbrush;

    if( !( leaf->contents & trace_contents ) )
        return;
    // trace line against all brushes in the leaf
    leafbrush = leaf->firstleafbrush;
    for (k=0 ; k<leaf->numleafbrushes ; k++, leafbrush++)
    {
        b = *leafbrush;
        if (b->checkcount == checkcount)
            continue;   // already checked this brush in another leaf
        b->checkcount = checkcount;

        if ( !(b->contents & trace_contents))
            continue;
        CM_ClipBoxToBrush (trace_mins, trace_maxs, trace_start, trace_end, trace_trace, b);
        if (!trace_trace->fraction)
            return;
    }

}


/*
================
CM_TestInLeaf
================
*/
static void CM_TestInLeaf ( cleaf_t *leaf )
{
    int         k;
    cbrush_t    *b, **leafbrush;

    if( !( leaf->contents & trace_contents ) )
        return;
    // trace line against all brushes in the leaf
    leafbrush = leaf->firstleafbrush;
    for (k=0 ; k<leaf->numleafbrushes ; k++, leafbrush++)
    {
        b = *leafbrush;
        if (b->checkcount == checkcount)
            continue;   // already checked this brush in another leaf
        b->checkcount = checkcount;

        if ( !(b->contents & trace_contents))
            continue;
        CM_TestBoxInBrush (trace_mins, trace_maxs, trace_start, trace_trace, b);
        if (!trace_trace->fraction)
            return;
    }

}


/*
==================
CM_RecursiveHullCheck

==================
*/
static void CM_RecursiveHullCheck ( cnode_t *node, float p1f, float p2f, vec3_t p1, vec3_t p2)
{
    cplane_t    *plane;
    float       t1, t2, offset;
    float       frac, frac2;
    float       idist;
    int         i;
    vec3_t      mid;
    int         side;
    float       midf;

    if (trace_trace->fraction <= p1f)
        return;     // already hit something nearer

recheck:
    // if plane is NULL, we are in a leaf node
    plane = node->plane;
    if (!plane) {
        CM_TraceToLeaf ( ( cleaf_t * )node );
        return;
    }

    //
    // find the point distances to the seperating plane
    // and the offset for the size of the box
    //
    if (plane->type < 3)
    {
        t1 = p1[plane->type] - plane->dist;
        t2 = p2[plane->type] - plane->dist;
        offset = trace_extents[plane->type];
    }
    else
    {
        t1 = PlaneDiff( p1, plane );
        t2 = PlaneDiff( p2, plane );
        if (trace_ispoint)
            offset = 0;
        else
            offset = fabs(trace_extents[0]*plane->normal[0]) +
                fabs(trace_extents[1]*plane->normal[1]) +
                fabs(trace_extents[2]*plane->normal[2]);
    }

    // see which sides we need to consider
    if (t1 >= offset && t2 >= offset)
    {
        node = node->children[0];
        goto recheck;
    }
    if (t1 < -offset && t2 < -offset)
    {
        node = node->children[1];
        goto recheck;
    }

    // put the crosspoint DIST_EPSILON pixels on the near side
    if (t1 < t2)
    {
        idist = 1.0/(t1-t2);
        side = 1;
        frac2 = (t1 + offset + DIST_EPSILON)*idist;
        frac = (t1 - offset + DIST_EPSILON)*idist;
    }
    else if (t1 > t2)
    {
        idist = 1.0/(t1-t2);
        side = 0;
        frac2 = (t1 - offset - DIST_EPSILON)*idist;
        frac = (t1 + offset + DIST_EPSILON)*idist;
    }
    else
    {
        side = 0;
        frac = 1;
        frac2 = 0;
    }

    // move up to the node
    if (frac < 0)
        frac = 0;
    if (frac > 1)
        frac = 1;
        
    midf = p1f + (p2f - p1f)*frac;
    for (i=0 ; i<3 ; i++)
        mid[i] = p1[i] + frac*(p2[i] - p1[i]);

    CM_RecursiveHullCheck (node->children[side], p1f, midf, p1, mid);


    // go past the node
    if (frac2 < 0)
        frac2 = 0;
    if (frac2 > 1)
        frac2 = 1;
        
    midf = p1f + (p2f - p1f)*frac2;
    for (i=0 ; i<3 ; i++)
        mid[i] = p1[i] + frac2*(p2[i] - p1[i]);

    CM_RecursiveHullCheck (node->children[side^1], midf, p2f, mid, p2);
}



//======================================================================

/*
==================
CM_BoxTrace
==================
*/
void CM_BoxTrace( trace_t *trace, vec3_t start, vec3_t end,
                    vec3_t mins, vec3_t maxs,
                    cnode_t *headnode, int brushmask )
{
    int     i;

    checkcount++;       // for multi-check avoidance

    // fill in a default trace
    trace_trace = trace;
    memset (trace_trace, 0, sizeof( *trace_trace ));
    trace_trace->fraction = 1;
    trace_trace->surface = &(nullsurface.c);

    if( !headnode ) {
        return;
    }

    trace_contents = brushmask;
    VectorCopy (start, trace_start);
    VectorCopy (end, trace_end);
    VectorCopy (mins, trace_mins);
    VectorCopy (maxs, trace_maxs);

    //
    // check for position test special case
    //
    if (start[0] == end[0] && start[1] == end[1] && start[2] == end[2])
    {
        cleaf_t     *leafs[1024];
        int     i, numleafs;
        vec3_t  c1, c2;

        VectorAdd (start, mins, c1);
        VectorAdd (start, maxs, c2);
        for (i=0 ; i<3 ; i++)
        {
            c1[i] -= 1;
            c2[i] += 1;
        }

        numleafs = CM_BoxLeafs_headnode (c1, c2, leafs, 1024, headnode, NULL);
        for (i=0 ; i<numleafs ; i++)
        {
            CM_TestInLeaf (leafs[i]);
            if (trace_trace->allsolid)
                break;
        }
        VectorCopy (start, trace_trace->endpos);
        return;
    }

    //
    // check for point special case
    //
    if (mins[0] == 0 && mins[1] == 0 && mins[2] == 0
        && maxs[0] == 0 && maxs[1] == 0 && maxs[2] == 0)
    {
        trace_ispoint = qtrue;
        VectorClear (trace_extents);
    }
    else
    {
        trace_ispoint = qfalse;
        trace_extents[0] = -mins[0] > maxs[0] ? -mins[0] : maxs[0];
        trace_extents[1] = -mins[1] > maxs[1] ? -mins[1] : maxs[1];
        trace_extents[2] = -mins[2] > maxs[2] ? -mins[2] : maxs[2];
    }

    //
    // general sweeping through world
    //
    CM_RecursiveHullCheck (headnode, 0, 1, start, end);

    if (trace_trace->fraction == 1)
    {
        VectorCopy (end, trace_trace->endpos);
    }
    else
    {
        for (i=0 ; i<3 ; i++)
            trace_trace->endpos[i] = start[i] + trace_trace->fraction * (end[i] - start[i]);
    }
    
}


/*
==================
CM_TransformedBoxTrace

Handles offseting and rotation of the end points for moving and
rotating entities
==================
*/
void CM_TransformedBoxTrace ( trace_t *trace, vec3_t start, vec3_t end,
                          vec3_t mins, vec3_t maxs,
                          cnode_t *headnode, int brushmask,
                          vec3_t origin, vec3_t angles)
{
    vec3_t      start_l, end_l;
    vec3_t      a;
    vec3_t      forward, right, up;
    vec3_t      temp;
    qboolean    rotated;

    // subtract origin offset
    VectorSubtract (start, origin, start_l);
    VectorSubtract (end, origin, end_l);

    // rotate start and end into the models frame of reference
    if (headnode != box_headnode && 
    (angles[0] || angles[1] || angles[2]) )
        rotated = qtrue;
    else
        rotated = qfalse;

    if (rotated)
    {
        AngleVectors (angles, forward, right, up);

        VectorCopy (start_l, temp);
        start_l[0] = DotProduct (temp, forward);
        start_l[1] = -DotProduct (temp, right);
        start_l[2] = DotProduct (temp, up);

        VectorCopy (end_l, temp);
        end_l[0] = DotProduct (temp, forward);
        end_l[1] = -DotProduct (temp, right);
        end_l[2] = DotProduct (temp, up);
    }

    // sweep the box through the model
    CM_BoxTrace ( trace, start_l, end_l, mins, maxs, headnode, brushmask);

    if (rotated && trace->fraction != 1.0)
    {
        // FIXME: figure out how to do this with existing angles
        VectorNegate (angles, a);
        AngleVectors (a, forward, right, up);

        VectorCopy (trace->plane.normal, temp);
        trace->plane.normal[0] = DotProduct (temp, forward);
        trace->plane.normal[1] = -DotProduct (temp, right);
        trace->plane.normal[2] = DotProduct (temp, up);
    }

    trace->endpos[0] = start[0] + trace->fraction * (end[0] - start[0]);
    trace->endpos[1] = start[1] + trace->fraction * (end[1] - start[1]);
    trace->endpos[2] = start[2] + trace->fraction * (end[2] - start[2]);
}


void CM_ClipEntity( trace_t *dst, trace_t *src, struct edict_s *ent ) {
    dst->allsolid |= src->allsolid;
    dst->startsolid |= src->startsolid;
    if( src->fraction < dst->fraction ) {
        dst->fraction = src->fraction;
        VectorCopy( src->endpos, dst->endpos );
        dst->plane = src->plane;
        dst->surface = src->surface;
        dst->contents |= src->contents;
        dst->ent = ent;
    }
}


/*
===============================================================================

PVS / PHS

===============================================================================
*/

static byte     pvsrow[MAX_MAP_LEAFS/8];
static byte     phsrow[MAX_MAP_LEAFS/8];

/*
===================
CM_DecompressVis
===================
*/
static void CM_DecompressVis( byte *in, byte *out, int rowsize ) {
    int     c;
    byte    *out_p;

    out_p = out;
    do {
        if( *in ) {
            *out_p++ = *in++;
            continue;
        }
    
        c = in[1];
        in += 2;
        if( ( out_p - out ) + c > rowsize ) {
            c = rowsize - ( out_p - out );
            Com_WPrintf( "CM_DecompressVis: overrun\n" );
        }
        while( c ) {
            *out_p++ = 0;
            c--;
        }
    } while( out_p - out < rowsize );
}

byte *CM_ClusterPVS( cm_t *cm, int cluster ) {
    cmcache_t *cache = cm->cache;

    if( !cache || !cache->vis ) {
        memset( pvsrow, 0xff, sizeof( pvsrow ) );
    } else if( cluster == -1 ) {
        memset( pvsrow, 0, cache->visrowsize );
    } else {
        if( cluster < 0 || cluster >= cache->vis->numclusters ) {
            Com_Error( ERR_DROP, "CM_ClusterPVS: bad cluster" );
        }
        CM_DecompressVis( ( byte * )cache->vis + cache->vis->bitofs[cluster][DVIS_PVS],
            pvsrow, cache->visrowsize );
    }
    return pvsrow;
}

byte *CM_ClusterPHS( cm_t *cm, int cluster ) {
    cmcache_t *cache = cm->cache;

    if( !cache || !cache->vis ) {
        memset( phsrow, 0xff, sizeof( phsrow ) );
    } else if( cluster == -1 ) {
        memset( phsrow, 0, cache->visrowsize );
    } else {
        if( cluster < 0 || cluster >= cache->vis->numclusters ) {
            Com_Error( ERR_DROP, "CM_ClusterPVS: bad cluster" );
        }
        CM_DecompressVis( ( byte * )cache->vis + cache->vis->bitofs[cluster][DVIS_PHS],
            phsrow, cache->visrowsize );
    }
    return phsrow;
}

/*
===============================================================================

AREAPORTALS

===============================================================================
*/

static void FloodArea_r( cm_t *cm, int number, int floodnum ) {
    int     i;
    careaportal_t   *p;
    carea_t *area;

    area = &cm->cache->areas[number];
    if( area->floodvalid == floodvalid ) {
        if( cm->floodnums[number] == floodnum )
            return;
        Com_Error( ERR_DROP, "FloodArea_r: reflooded" );
    }

    cm->floodnums[number] = floodnum;
    area->floodvalid = floodvalid;
    p = &cm->cache->areaportals[area->firstareaportal];
    for( i = 0; i < area->numareaportals; i++, p++ ) {
        if( cm->portalopen[p->portalnum] )
            FloodArea_r( cm, p->otherarea, floodnum );
    }
}

/*
====================
CM_FloodAreaConnections
====================
*/
void CM_FloodAreaConnections( cm_t *cm ) {
    int     i;
    carea_t *area;
    int     floodnum;

    // all current floods are now invalid
    floodvalid++;
    floodnum = 0;

    // area 0 is not used
    for( i = 1; i < cm->cache->numareas; i++ ) {
        area = &cm->cache->areas[i];
        if( area->floodvalid == floodvalid )
            continue;       // already flooded into
        floodnum++;
        FloodArea_r( cm, i, floodnum );
    }

}

void CM_SetAreaPortalState( cm_t *cm, int portalnum, qboolean open ) {
    if( portalnum > cm->cache->numareaportals )
        Com_Error( ERR_DROP, "CM_SetAreaPortalState: areaportal > numareaportals" );

    cm->portalopen[portalnum] = open;
    CM_FloodAreaConnections( cm );
}

qboolean CM_AreasConnected( cm_t *cm, int area1, int area2 ) {
    cmcache_t *cache = cm->cache;

    if( !cache ) {
        return qfalse;
    }
    if( map_noareas->integer ) {
        return qtrue;
    }
    if( area1 < 1 || area2 < 1 ) {
        return qfalse;
    }
    if( area1 >= cache->numareas || area2 >= cache->numareas ) {
        Com_Error( ERR_DROP, "CM_AreasConnected: area > numareas" );
    }
    if( cm->floodnums[area1] == cm->floodnums[area2] ) {
        return qtrue;
    }

    return qfalse;
}


/*
=================
CM_WriteAreaBits

Writes a length byte followed by a bit vector of all the areas
that area in the same flood as the area parameter

This is used by the client refreshes to cull visibility
=================
*/
int CM_WriteAreaBits( cm_t *cm, byte *buffer, int area ) {
    cmcache_t *cache = cm->cache;
    int     i;
    int     floodnum;
    int     bytes;

    if( !cache ) {
        return 0;
    }

    bytes = ( cache->numareas + 7 ) >> 3;

    if( map_noareas->integer || !area ) {
        // for debugging, send everything
        memset( buffer, 255, bytes );
    } else {
        memset( buffer, 0, bytes );

        floodnum = cm->floodnums[area];
        for ( i = 0; i < cache->numareas; i++) {
            if( cm->floodnums[i] == floodnum ) {
                Q_SetBit( buffer, i );
            }
        }
    }

    return bytes;
}

int CM_WritePortalBits( cm_t *cm, byte *buffer ) {
    int     i, bytes, numportals;

    if( !cm->cache ) {
        return 0;
    }

    numportals = cm->cache->numareaportals;
    if( numportals > MAX_MAP_AREAS ) {
        /* HACK: use the same array size as areabytes!
         * It is nonsense for a map to have > 256 areaportals anyway. */
        Com_WPrintf( "CM_WritePortalBits: too many areaportals\n" );
        numportals = MAX_MAP_AREAS;
    }

    bytes = ( numportals + 7 ) >> 3;
    memset( buffer, 0, bytes );
    for( i = 0; i < numportals; i++ ) {
        if( cm->portalopen[i] ) {
            Q_SetBit( buffer, i );
        }
    }

    return bytes;
}

void CM_SetPortalStates( cm_t *cm, byte *buffer, int bytes ) {
    int     i, numportals;

    if( !bytes ) {
        for( i = 0; i < cm->cache->numareaportals; i++ ) {
            cm->portalopen[i] = qtrue;
        }
    } else {
        numportals = bytes << 3;
        if( numportals > cm->cache->numareaportals ) {
            numportals = cm->cache->numareaportals;
        }
        for( i = 0; i < numportals; i++ ) {
            cm->portalopen[i] = Q_IsBitSet( buffer, i ) ? qtrue : qfalse;
        }
    }

    CM_FloodAreaConnections( cm );
}


/*
=============
CM_HeadnodeVisible

Returns qtrue if any leaf under headnode has a cluster that
is potentially visible
=============
*/
qboolean CM_HeadnodeVisible( cnode_t *node, byte *visbits ) {
    cleaf_t *leaf;
    int     cluster;

    while( node->plane ) {
        if( CM_HeadnodeVisible( node->children[0], visbits ) )
            return qtrue;
        node = node->children[1];
    }

    leaf = ( cleaf_t * )node;
    cluster = leaf->cluster;
    if( cluster == -1 )
        return qfalse;
    if( Q_IsBitSet( visbits, cluster ) )
        return qtrue;
    return qfalse;
}


/*
============
CM_FatPVS

The client will interpolate the view position,
so we can't use a single PVS point
===========
*/
byte *CM_FatPVS( cm_t *cm, const vec3_t org ) {
    static byte fatpvs[MAX_MAP_LEAFS/8];
    cleaf_t *leafs[64];
    int     clusters[64];
    int     i, j, count;
    int     longs;
    uint32_t *src, *dst;
    vec3_t  mins, maxs;
        
    if( !cm->cache ) {  // map not loaded
        memset( fatpvs, 0, sizeof( fatpvs ) );
        return fatpvs;
    }

    for( i = 0; i < 3; i++ ) {
        mins[i] = org[i] - 8;
        maxs[i] = org[i] + 8;
    }

    count = CM_BoxLeafs( cm, mins, maxs, leafs, 64, NULL );
    if( count < 1 )
        Com_Error( ERR_DROP, "CM_FatPVS: leaf count < 1" );
    longs = ( cm->cache->numclusters + 31 ) >> 5;

    // convert leafs to clusters
    for( i = 0 ; i < count; i++ ) {
        clusters[i] = leafs[i]->cluster;
    }

    src = ( uint32_t * )CM_ClusterPVS( cm, clusters[0] );
    dst = ( uint32_t * )fatpvs;
    for( j = 0; j < longs; j++ ) {
        *dst++ = *src++;
    }

    // or in all the other leaf bits
    for( i = 1; i < count; i++ ) {
        for( j = 0; j < i; j++ ) {
            if( clusters[i] == clusters[j] ) {
                goto nextleaf; // already have the cluster we want
            }
        }
        src = ( uint32_t * )CM_ClusterPVS( cm, clusters[i] );
        dst = ( uint32_t * )fatpvs;
        for( j = 0; j < longs; j++ ) {
            *dst++ |= *src++;
        }

nextleaf:;
    }

    return fatpvs;
}

/*
=============
CM_Init
=============
*/
void CM_Init( void ) {
    CM_InitBoxHull();

    map_noareas = Cvar_Get( "map_noareas", "0", 0 );
    map_override = Cvar_Get( "map_override", "0", 0 );
}

