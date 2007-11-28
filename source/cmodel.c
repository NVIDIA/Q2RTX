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

#define CM_Malloc( size )	Z_TagMalloc( size, TAG_CMODEL )
#define CM_Mallocz( size )	Z_TagMallocz( size, TAG_CMODEL )
#define CM_Free( ptr )		Z_Free( ptr )

static mapsurface_t	nullsurface;
static cleaf_t		nullleaf;

static int			floodvalid;
static int			checkcount;

static cvar_t		*map_noareas;
static cvar_t		*map_load_entities;

void	CM_FloodAreaConnections( cm_t *cm );

/*
===============================================================================

					MAP LOADING

===============================================================================
*/

static byte	*cmod_base;
static cmcache_t	*cmod;

/*
=================
CMod_LoadSubmodels
=================
*/
static void CMod_LoadSubmodels (lump_t *l)
{
	dmodel_t	*in;
	cmodel_t	*out;
	int			i, j, count;
	uint32		headnode;

	in = (void *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadSubmodels: funny lump size");
	count = l->filelen / sizeof(*in);

	if (count < 1)
		Com_Error (ERR_DROP, "Map with no models");
	if (count > MAX_MAP_MODELS)
		Com_Error (ERR_DROP, "Map has too many models");

	cmod->cmodels = CM_Malloc( sizeof( *out ) * count );
	cmod->numcmodels = count;

	out = cmod->cmodels;
	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat (in->mins[j]) - 1;
			out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
			out->origin[j] = LittleFloat (in->origin[j]);
		}
		headnode = LittleLong (in->headnode);
		if( headnode >= cmod->numnodes ) {
			// FIXME: headnode may be garbage for some models
			Com_DPrintf( "CMod_LoadSubmodels: bad headnode for model %d\n", i );
			out->headnode = NULL;
		} else {
			out->headnode = cmod->nodes + headnode;
		}
	}
}


/*
=================
CMod_LoadSurfaces
=================
*/
static void CMod_LoadSurfaces (lump_t *l)
{
	texinfo_t	*in;
	mapsurface_t	*out;
	int			i, count;

	in = (void *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadSurfaces: funny lump size");
	count = l->filelen / sizeof(*in);
	if (count < 1)
		Com_Error (ERR_DROP, "Map with no surfaces");
	if (count > MAX_MAP_TEXINFO)
		Com_Error (ERR_DROP, "Map has too many surfaces");

	cmod->numtexinfo = count;
	cmod->surfaces = CM_Malloc( sizeof( *out ) * count );

	out = cmod->surfaces;
	for ( i=0 ; i<count ; i++, in++, out++)
	{
		Q_strncpyz (out->c.name, in->texture, sizeof(out->c.name));
		Q_strncpyz (out->rname, in->texture, sizeof(out->rname));
		out->c.flags = LittleLong (in->flags);
		out->c.value = LittleLong (in->value);
	}
}


/*
=================
CMod_LoadNodes

=================
*/
static void CMod_LoadNodes (lump_t *l)
{
	dnode_t		*in;
	uint32		child;
	cnode_t		*out;
	int			i, j, count;
	uint32		planeNum;
	
	in = (void *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadNodes: funny lump size");
	count = l->filelen / sizeof(*in);

	if (count < 1)
		Com_Error (ERR_DROP, "Map has no nodes");
	if (count > MAX_MAP_NODES)
		Com_Error (ERR_DROP, "Map has too many nodes");

	cmod->numnodes = count;
	cmod->nodes = CM_Malloc( sizeof( *out ) * count );

	out = cmod->nodes;
	for (i=0 ; i<count ; i++, out++, in++)
	{
		planeNum = LittleLong(in->planenum);
		if( planeNum >= cmod->numplanes ) {
			Com_Error (ERR_DROP, "CMod_LoadNodes: bad planenum");
		}
		out->plane = cmod->planes + planeNum;

		for( j = 0; j < 2; j++ ) {
			child = LittleLong( in->children[j] );
			if( child & 0x80000000 ) {
				child = ~child;
				if( child >= cmod->numleafs ) {
					Com_Error( ERR_DROP, "CMod_LoadNodes: bad leafnum" );
				}
				out->children[j] = ( cnode_t * )( cmod->leafs + child );
			} else {
				if( child >= count ) {
					Com_Error ( ERR_DROP, "CMod_LoadNodes: bad nodenum" );
				}
				out->children[j] = cmod->nodes + child;
			}
		}
	}

}

/*
=================
CMod_LoadBrushes

=================
*/
static void CMod_LoadBrushes (lump_t *l)
{
	dbrush_t	*in;
	cbrush_t	*out;
	int			i, count;
	uint32		firstSide, numSides;
	
	in = (void *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadBrushes: funny lump size");
	count = l->filelen / sizeof(*in);

	if (count > MAX_MAP_BRUSHES)
		Com_Error (ERR_DROP, "Map has too many brushes");

	cmod->numbrushes = count;
	cmod->brushes = CM_Malloc( sizeof( *out ) * count );

	out = cmod->brushes;
	for (i=0 ; i<count ; i++, out++, in++)
	{
		firstSide = LittleLong(in->firstside);
		numSides = LittleLong(in->numsides);
		if( firstSide + numSides > cmod->numbrushsides ) {
			Com_Error (ERR_DROP, "CMod_LoadBrushes: bad brushsides");
		}
		out->firstbrushside = cmod->brushsides + firstSide;
		out->numsides = numSides;
		out->contents = LittleLong(in->contents);
        out->checkcount = 0;
	}

}

/*
=================
CMod_LoadLeafs
=================
*/
static void CMod_LoadLeafs (lump_t *l)
{
	int			i;
	cleaf_t		*out;
	dleaf_t 	*in;
	int			count;
	uint16		areaNum, firstBrush, numBrushes;
	
	in = (void *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadLeafs: funny lump size");
	count = l->filelen / sizeof(*in);

	if (count < 1)
		Com_Error (ERR_DROP, "Map with no leafs");
	// need to save space for box planes
	if (count > MAX_MAP_LEAFS)
		Com_Error (ERR_DROP, "Map has too many leafs");
	
	cmod->numleafs = count;
	cmod->numclusters = 0;
	cmod->leafs = CM_Malloc( sizeof( *out ) * count );

	out = cmod->leafs;
	for ( i=0 ; i<count ; i++, in++, out++)
	{
        out->plane = NULL;
		out->contents = LittleLong (in->contents);
		out->cluster = ( signed short )LittleShort (in->cluster);
		areaNum = LittleShort (in->area);
		if( areaNum >= cmod->numareas ) {
			Com_Error (ERR_DROP, "CMod_LoadLeafs: bad areanum");
		}
		out->area = areaNum;

		firstBrush = LittleShort (in->firstleafbrush);
		numBrushes = LittleShort (in->numleafbrushes);
		if( firstBrush + numBrushes > cmod->numleafbrushes ) {
			Com_Error (ERR_DROP, "CMod_LoadLeafs: bad brushnum");
		}

		out->firstleafbrush = cmod->leafbrushes + firstBrush;
		out->numleafbrushes = numBrushes;

		if (out->cluster >= cmod->numclusters)
			cmod->numclusters = out->cluster + 1;
	}

	if (cmod->leafs[0].contents != CONTENTS_SOLID)
		Com_Error (ERR_DROP, "Map leaf 0 is not CONTENTS_SOLID");
}

/*
=================
CMod_LoadPlanes
=================
*/
static void CMod_LoadPlanes (lump_t *l)
{
	int			i;
	cplane_t	*out;
	dplane_t 	*in;
	int			count;
	
	in = (void *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadPlanes: funny lump size");
	count = l->filelen / sizeof(*in);

	if (count < 1)
		Com_Error (ERR_DROP, "Map with no planes");
	if (count > MAX_MAP_PLANES)
		Com_Error (ERR_DROP, "Map has too many planes");
		
	cmod->numplanes = count;
	cmod->planes = CM_Malloc( sizeof( *out ) * count );

	out = cmod->planes;
	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->normal[0] = LittleFloat( in->normal[0] );
		out->normal[1] = LittleFloat( in->normal[1] );
		out->normal[2] = LittleFloat( in->normal[2] );
		out->dist = LittleFloat (in->dist);
		//out->type = LittleLong (in->type);

		SetPlaneType( out );
		SetPlaneSignbits( out );
	}
}

/*
=================
CMod_LoadLeafBrushes
=================
*/
static void CMod_LoadLeafBrushes (lump_t *l)
{
	int			i;
	cbrush_t	**out;
	uint16	 	*in;
	int			count;
	uint16		brushNum;
	
	in = (void *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadLeafBrushes: funny lump size");
	count = l->filelen / sizeof(*in);

	if (count < 1)
		Com_Error (ERR_DROP, "Map with no leafbrushes");
	if (count > MAX_MAP_LEAFBRUSHES)
		Com_Error (ERR_DROP, "Map has too many leafbrushes");
	
	cmod->numleafbrushes = count;
	cmod->leafbrushes = CM_Malloc( sizeof( *out ) * count );

	out = cmod->leafbrushes;
	for ( i=0 ; i<count ; i++, in++, out++) {
		brushNum = LittleShort (*in);
		if( brushNum >= cmod->numbrushes ) {
			Com_Error (ERR_DROP, "CMod_LoadLeafBrushes: bad brushnum");
		}
		*out = cmod->brushes + brushNum;
	}
}

/*
=================
CMod_LoadBrushSides
=================
*/
static void CMod_LoadBrushSides (lump_t *l)
{
	int			i;
	cbrushside_t	*out;
	dbrushside_t 	*in;
	int			count;
	uint32			planeNum;
	uint16			texinfoNum;

	in = (void *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadBrushSides: funny lump size");
	count = l->filelen / sizeof(*in);

	if (count > MAX_MAP_BRUSHSIDES)
		Com_Error (ERR_DROP, "Map has too many brush sides");

	cmod->numbrushsides = count;
	cmod->brushsides = CM_Malloc( sizeof( *out ) * count );

	out = cmod->brushsides;
	for ( i=0 ; i<count ; i++, in++, out++)
	{
		planeNum = LittleShort (in->planenum);
		if( planeNum >= cmod->numplanes ) {
			Com_Error (ERR_DROP, "CMod_LoadBrushSides: bad planenum");
		}
		out->plane = cmod->planes + planeNum;
		texinfoNum = LittleShort (in->texinfo);
		if( texinfoNum == (uint16)-1 ) {
			out->surface = &nullsurface;
		} else {
			if (texinfoNum >= cmod->numtexinfo)
				Com_Error (ERR_DROP, "CMod_LoadBrushSides: bad texinfo");
			out->surface = cmod->surfaces + texinfoNum;
		}
	}
}

/*
=================
CMod_LoadAreas
=================
*/
static void CMod_LoadAreas (lump_t *l)
{
	int			i;
	carea_t		*out;
	darea_t 	*in;
	int			count;
	uint32		numPortals, firstPortal;

	in = (void *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadAreas: funny lump size");
	count = l->filelen / sizeof(*in);

	if (count > MAX_MAP_AREAS)
		Com_Error (ERR_DROP, "Map has too many areas");

	cmod->numareas = count;
	cmod->areas = CM_Malloc( sizeof( *out ) * count );

	out = cmod->areas;
	for ( i=0 ; i<count ; i++, in++, out++)
	{
		numPortals = LittleLong (in->numareaportals);
		firstPortal = LittleLong (in->firstareaportal);
		if( firstPortal + numPortals > cmod->numareaportals ) {
			Com_Error (ERR_DROP, "CMod_LoadAreas: bad areaportals");
		}
		out->numareaportals = numPortals;
		out->firstareaportal = firstPortal;
		out->floodvalid = 0;
	}
}

/*
=================
CMod_LoadAreaPortals
=================
*/
static void CMod_LoadAreaPortals (lump_t *l)
{
	int			i;
	careaportal_t		*out;
	dareaportal_t 	*in;
	int			count;
	uint32		portalNum, otherArea;

	in = (void *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadAreaPortals: funny lump size");
	count = l->filelen / sizeof(*in);

	if (count > MAX_MAP_AREAS)
		Com_Error (ERR_DROP, "Map has too many areas");

	cmod->numareaportals = count;
	cmod->areaportals = CM_Malloc( sizeof( *out ) * count );

	out = cmod->areaportals;
	for ( i=0 ; i<count ; i++, in++, out++)
	{
		portalNum = LittleLong (in->portalnum);
		if( portalNum >= MAX_MAP_AREAPORTALS ) {
			Com_Error (ERR_DROP, "CMod_LoadAreaPortals: bad portalnum");
		}
		out->portalnum = portalNum;
		otherArea = LittleLong (in->otherarea);
		if( otherArea >= MAX_MAP_AREAS ) {
			Com_Error (ERR_DROP, "CMod_LoadAreaPortals: bad otherarea");
		}
		out->otherarea = otherArea;
	}
}

/*
=================
CMod_LoadVisibility
=================
*/
static void CMod_LoadVisibility( lump_t *l ) {
	int		i;
	uint32 numClusters;

	if( !l->filelen ) {
		return;
	}

	if( l->filelen > MAX_MAP_VISIBILITY )
		Com_Error (ERR_DROP, "Map has too large visibility lump");

	cmod->numvisibility = l->filelen;
	cmod->vis = CM_Malloc( l->filelen );
	memcpy( cmod->vis, cmod_base + l->fileofs, l->filelen );

	numClusters = LittleLong( cmod->vis->numclusters );
	cmod->vis->numclusters = numClusters;
	for( i = 0; i < numClusters; i++ ) {
		cmod->vis->bitofs[i][0] = LittleLong( cmod->vis->bitofs[i][0] );
		cmod->vis->bitofs[i][1] = LittleLong( cmod->vis->bitofs[i][1] );
	}

	cmod->visrowsize = ( numClusters + 7 ) >> 3;
}


/*
=================
CMod_LoadEntityString
=================
*/
static void CMod_LoadEntityString( lump_t *l ) {
	if( l->filelen > MAX_MAP_ENTSTRING ) {
		Com_Error( ERR_DROP, "Map has too large entity lump" );
	}

	cmod->numEntityChars = l->filelen;
	cmod->entitystring = CM_Malloc( l->filelen + 1 );
	memcpy( cmod->entitystring, cmod_base + l->fileofs, l->filelen );
	cmod->entitystring[l->filelen] = 0;
}

#if 0
/*
==================
CM_ConcatToEntityString
==================
*/
static void CM_ConcatToEntityString( const char *text ) {
	int len;

	len = strlen( text );
	if( numEntityChars + len > sizeof( cmod->entitystring ) - 1 ) {
		Com_Error( ERR_DROP, "CM_ConcatToEntityString: oversize entity lump" );
	}

	strcpy( cmod->entitystring + numEntityChars, text );
	numEntityChars += len;
}

/*
==================
CM_ParseMapFile

Parses complete *.map file
==================
*/
static qboolean CM_ParseMapFile( const char *data ) {
	char *token;
	int numInlineModels;
	qboolean inlineModel;
	char buffer[MAX_STRING_CHARS];
	char key[MAX_TOKEN_CHARS];

	numInlineModels = 0;

	while( 1 ) {
		token = COM_Parse( ( const char ** )&data );
		if( !data ) {
			break;
		}

		if( *token != '{' ) {
			Com_WPrintf( "CM_ParseMapFile: expected '{', found '%s'\n", token );
			return qfalse;
		}

		CM_ConcatToEntityString( "{ " );

		inlineModel = qfalse;

		// Parse entity
		while( 1 ) {
			token = COM_Parse( ( const char ** )&data );
			if( !data ) {
				Com_WPrintf( "CM_ParseMapFile: expected key, found EOF\n" );
				return qfalse;
			}

			if( *token == '}' ) {
				// FIXME HACK: restore inline model number
				// This may not work properly if the entity order is different!!!
				if( inlineModel && numInlineModels ) {
					Com_sprintf( buffer, sizeof( buffer ), "\"model\" \"*%i\" } ", numInlineModels );
					CM_ConcatToEntityString( buffer );
				} else {
					CM_ConcatToEntityString( "} " );
				}

				if( inlineModel ) {
					numInlineModels++;
				}

				break;
			}

			if( *token == '{' ) {
				inlineModel = qtrue;

				// Parse brush
				while( 1 ) {
					token = COM_Parse( ( const char ** )&data );
					if( !data ) {
						Com_WPrintf( "CM_ParseMapFile: expected brush data, found EOF\n" );
						return qfalse;
					}

					if( *token == '}' ) {
						break;
					}

					if( *token == '{' ) {
						Com_WPrintf( "CM_ParseMapFile: expected brush data, found '{'\n" );
						return qfalse;
					}

				}

				continue;
			}

			Q_strncpyz( key, token, sizeof( key ) );

			token = COM_Parse( ( const char ** )&data );
			if( !data ) {
				Com_WPrintf( "CM_ParseMapFile: expected value, found EOF\n" );
				return qfalse;
			}

			if( *token == '}' || *token == '{' ) {
				Com_WPrintf( "CM_ParseMapFile: expected value, found '%s'\n", token );
				return qfalse;
			}

			Com_sprintf( buffer, sizeof( buffer ), "\"%s\" \"%s\" ", key, token );

			CM_ConcatToEntityString( buffer );

		}
	}

	cmod->entitystring[numEntityChars] = 0;

	if( numInlineModels != numcmodels + 1 ) {
		Com_WPrintf( "CM_ParseMapFile: mismatched number of inline models\n" );
		return qfalse;
	}

	return qtrue;
}
#endif

#define CM_CACHESIZE		16

static cmcache_t	cm_cache[CM_CACHESIZE];

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

#define CM_FREE( f ) \
	do { \
		if( cache->f ) { \
			CM_Free( cache->f ); \
		} \
	} while( 0 )

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
	}

	memset( cm, 0, sizeof( *cm ) );
}


/*
==================
CM_LoadMap

Loads in the map and all submodels
==================
*/
qboolean CM_LoadMap( cm_t *cm, const char *name, uint32 flags, uint32 *checksum ) {
	cmcache_t		*cache;
	byte			*buf;
	int				i;
	dheader_t		header;
	int				length;
//	char *entstring;
//	char buffer[MAX_QPATH];

	if( !name ) {
		Com_Error( ERR_FATAL, "CM_LoadMap: NULL name" );
	}
	if( !name[0] ) {
		Com_Error( ERR_DROP, "CM_LoadMap: empty name" );
	}

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
			cm->cache = cache;
			if( flags & CM_LOAD_CLIENT ) {
				cm->floodnums = NULL;
				cm->portalopen = NULL;
			} else {
				cm->floodnums = CM_Mallocz( sizeof( int ) * cache->numareas +
					sizeof( qboolean ) * cache->numareaportals );
				cm->portalopen = ( qboolean * )( cm->floodnums + cache->numareas );
				CM_FloodAreaConnections( cm );
			}
			cache->refcount++;
			return qtrue; // still have the right version
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
		Com_Error( ERR_DROP, "Out of cache slots to load %s", name );
	}

	//
	// load the file
	//
	length = FS_LoadFileEx( name, (void **)&buf, FS_FLAG_CACHE );
	if( !buf )
		Com_Error( ERR_DROP, "Couldn't load %s", name );

	cache->checksum = LittleLong( Com_BlockChecksum( buf, length ) );
	if( checksum ) {
		*checksum = cache->checksum;
	}

	header = *( dheader_t * )buf;
	for( i = 0; i < sizeof( dheader_t )/4; i++ )
		(( uint32 * )&header)[i] = LittleLong( (( uint32 * )&header)[i] );

	if( header.version != BSPVERSION ) {
		Com_Error( ERR_DROP, "CM_LoadMap: %s has wrong version number (%i should be %i)",
			name, header.version, BSPVERSION );
	}

	cmod_base = buf;
	cmod = cache;

#define CM_LOAD( Func, Lump ) \
	do { \
		CMod_Load##Func( &header.lumps[LUMP_##Lump] ); \
	} while( 0 )

	// load into heap
	CM_LOAD( Visibility, VISIBILITY );
	CM_LOAD( Surfaces, TEXINFO );
	CM_LOAD( Planes, PLANES );
	CM_LOAD( BrushSides, BRUSHSIDES );
	CM_LOAD( Brushes, BRUSHES );
	CM_LOAD( LeafBrushes, LEAFBRUSHES );
	CM_LOAD( AreaPortals, AREAPORTALS );
	CM_LOAD( Areas, AREAS );
	CM_LOAD( Leafs, LEAFS );
	CM_LOAD( Nodes, NODES );
	CM_LOAD( Submodels, MODELS );

#if 0
	// Load the entity string from file, if specified
	entstring = NULL;
	if( cmod->load_entities->integer && !clientload ) {
		COM_StripExtension( name, buffer, sizeof( buffer ) );
		Q_strcat( buffer, sizeof( buffer ), ".map" );
		FS_LoadFile( buffer, ( void ** )&entstring );
	}

	if( entstring ) {
		Com_Printf( "Loading %s...\n", buffer );
		if( !CM_ParseMapFile( entstring ) ) {
			CMod_LoadEntityString( &header.lumps[LUMP_ENTITIES] );
		}
		FS_FreeFile( entstring );
	} else {
		CMod_LoadEntityString( &header.lumps[LUMP_ENTITIES] );
	}
#else
	CMod_LoadEntityString( &header.lumps[LUMP_ENTITIES] );
#endif

	FS_FreeFile( buf );

	cache->refcount = 1;
	cm->cache = cache;

	if( flags & CM_LOAD_CLIENT ) {
		cm->floodnums = NULL;
		cm->portalopen = NULL;
	} else {
		cm->floodnums = CM_Mallocz( sizeof( int ) * cache->numareas +
			sizeof( qboolean ) * cache->numareaportals );
		cm->portalopen = ( qboolean * )( cm->floodnums + cache->numareas );
		CM_FloodAreaConnections( cm );
	}

	Q_strncpyz( cache->name, name, sizeof( cache->name ) );

	//map_load_entities->modified = qfalse;

	return qtrue;
}

/*
==================
CM_InlineModel
==================
*/
cmodel_t *CM_InlineModel( cm_t *cm, const char *name ) {
	int		num;
	cmodel_t *cmodel;

	if( !name || name[0] != '*' ) {
		Com_Error( ERR_DROP, "CM_InlineModel: bad name: %s", name );
	}
	if( !cm->cache ) {
		Com_Error( ERR_DROP, "CM_InlineModel: NULL cache" );
	}
	num = atoi( name + 1 );
	if( num < 1 || num >= cm->cache->numcmodels ) {
		Com_Error ( ERR_DROP, "CM_InlineModel: bad number: %d", num );
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
		Com_Error( ERR_DROP, "CM_NodeNum: NULL cache" );
	}
	if( number == -1 ) {
		return ( cnode_t * )cm->cache->leafs; // special case for solid leaf
	}
	if( number < 0 || number >= cm->cache->numnodes ) {
		Com_Error( ERR_DROP, "CM_NodeNum: bad number: %d", number );
	}
	return cm->cache->nodes + number;
}

cleaf_t *CM_LeafNum( cm_t *cm, int number ) {
	if( !cm->cache ) {
		Com_Error( ERR_DROP, "CM_LeafNum: NULL cache" );
	}
	if( number < 0 || number >= cm->cache->numleafs ) {
		Com_Error( ERR_DROP, "CM_LeafNum: bad number: %d", number );
	}
	return cm->cache->leafs + number;
}

//=======================================================================

static cplane_t	box_planes[12];
static cnode_t	box_nodes[6];
static cnode_t	*box_headnode;
static cbrush_t	box_brush;
static cbrush_t *box_leafbrush;
static cbrushside_t box_brushsides[6];
static cleaf_t	box_leaf;
static cleaf_t	box_emptyleaf;

/*
===================
CM_InitBoxHull

Set up the planes and nodes so that the six floats of a bounding box
can just be stored out and get a proper clipping hull structure.
===================
*/
static void CM_InitBoxHull( void ) {
	int			i;
	int			side;
	cnode_t		*c;
	cplane_t	*p;
	cbrushside_t	*s;

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
	float		d;

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
		return &nullleaf;		// server may call this without map loaded
	}
	return CM_PointLeaf_r( p, cm->cache->nodes );
}


/*
=============
CM_BoxLeafnums

Fills in a list of all the leafs touched
=============
*/
static int		leaf_count, leaf_maxcount;
static cleaf_t	**leaf_list;
static float	*leaf_mins, *leaf_maxs;
static cnode_t	*leaf_topnode;

static void CM_BoxLeafs_r( cnode_t *node ) {
	cplane_t	*plane;
	int		s;

	while( 1 ) {
		plane = node->plane;
		if( !plane ) {
			if( leaf_count >= leaf_maxcount ) {
				return;
			}
			leaf_list[leaf_count++] = ( cleaf_t * )node;
			return;
		}
	
		s = BoxOnPlaneSideFast( leaf_mins, leaf_maxs, plane );
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

int	CM_BoxLeafs( cm_t *cm, vec3_t mins, vec3_t maxs, cleaf_t **list, int listsize, cnode_t **topnode ) {
	if( !cm->cache )	// map not loaded
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
	cleaf_t		*leaf;

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
int	CM_TransformedPointContents( vec3_t p, cnode_t *headnode, vec3_t origin, vec3_t angles ) {
	vec3_t		p_l;
	vec3_t		temp;
	vec3_t		forward, right, up;
	cleaf_t		*leaf;

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
#define	DIST_EPSILON	(0.03125)

static vec3_t	trace_start, trace_end;
static vec3_t	trace_mins, trace_maxs;
static vec3_t	trace_extents;

static trace_t	*trace_trace;
static int		trace_contents;
static qboolean	trace_ispoint;		// optimized case

/*
================
CM_ClipBoxToBrush
================
*/
static void CM_ClipBoxToBrush (vec3_t mins, vec3_t maxs, vec3_t p1, vec3_t p2,
					  trace_t *trace, cbrush_t *brush)
{
	int			i, j;
	cplane_t	*plane, *clipplane;
	float		dist;
	float		enterfrac, leavefrac;
	vec3_t		ofs;
	float		d1, d2;
	qboolean	getout, startout;
	float		f;
	cbrushside_t	*side, *leadside;

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
		{	// general box case

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
		{	// special point case
			dist = plane->dist;
		}

		d1 = DotProduct (p1, plane->normal) - dist;
		d2 = DotProduct (p2, plane->normal) - dist;

		if (d2 > 0)
			getout = qtrue;	// endpoint is not in solid
		if (d1 > 0)
			startout = qtrue;

		// if completely in front of face, no intersection
		if (d1 > 0 && d2 >= d1)
			return;

		if (d1 <= 0 && d2 <= 0)
			continue;

		// crosses face
		if (d1 > d2)
		{	// enter
			f = (d1-DIST_EPSILON) / (d1-d2);
			if (f > enterfrac)
			{
				enterfrac = f;
				clipplane = plane;
				leadside = side;
			}
		}
		else
		{	// leave
			f = (d1+DIST_EPSILON) / (d1-d2);
			if (f < leavefrac)
				leavefrac = f;
		}
	}

	if (!startout)
	{	// original point was inside brush
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
	int			i, j;
	cplane_t	*plane;
	float		dist;
	vec3_t		ofs;
	float		d1;
	cbrushside_t	*side;

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
	int			k;
	cbrush_t	*b, **leafbrush;

	if( !( leaf->contents & trace_contents ) )
		return;
	// trace line against all brushes in the leaf
	leafbrush = leaf->firstleafbrush;
	for (k=0 ; k<leaf->numleafbrushes ; k++, leafbrush++)
	{
		b = *leafbrush;
		if (b->checkcount == checkcount)
			continue;	// already checked this brush in another leaf
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
	int			k;
	cbrush_t	*b, **leafbrush;

	if( !( leaf->contents & trace_contents ) )
		return;
	// trace line against all brushes in the leaf
	leafbrush = leaf->firstleafbrush;
	for (k=0 ; k<leaf->numleafbrushes ; k++, leafbrush++)
	{
		b = *leafbrush;
		if (b->checkcount == checkcount)
			continue;	// already checked this brush in another leaf
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
	cplane_t	*plane;
	float		t1, t2, offset;
	float		frac, frac2;
	float		idist;
	int			i;
	vec3_t		mid;
	int			side;
	float		midf;

	if (trace_trace->fraction <= p1f)
		return;		// already hit something nearer

	// if plane is NULL, we are in a leaf node
	plane = node->plane;
	if (!plane)
	{
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
		t1 = DotProduct (plane->normal, p1) - plane->dist;
		t2 = DotProduct (plane->normal, p2) - plane->dist;
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
		CM_RecursiveHullCheck (node->children[0], p1f, p2f, p1, p2);
		return;
	}
	if (t1 < -offset && t2 < -offset)
	{
		CM_RecursiveHullCheck (node->children[1], p1f, p2f, p1, p2);
		return;
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
	int		i;

	checkcount++;		// for multi-check avoidance

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
		cleaf_t		*leafs[1024];
		int		i, numleafs;
		vec3_t	c1, c2;

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
	vec3_t		start_l, end_l;
	vec3_t		a;
	vec3_t		forward, right, up;
	vec3_t		temp;
	qboolean	rotated;

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

/*
===============================================================================

PVS / PHS

===============================================================================
*/

static byte		pvsrow[MAX_MAP_LEAFS/8];
static byte		phsrow[MAX_MAP_LEAFS/8];

/*
===================
CM_DecompressVis
===================
*/
static void CM_DecompressVis( byte *in, byte *out, int rowsize ) {
	int		c;
	byte	*out_p;

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
	int		i;
	careaportal_t	*p;
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
	int		i;
	carea_t	*area;
	int		floodnum;

	// all current floods are now invalid
	floodvalid++;
	floodnum = 0;

	// area 0 is not used
	for( i = 1; i < cm->cache->numareas; i++ ) {
		area = &cm->cache->areas[i];
		if( area->floodvalid == floodvalid )
			continue;		// already flooded into
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
	if( map_noareas->integer )
		return qtrue;

	if( area1 > cache->numareas || area2 > cache->numareas )
		Com_Error( ERR_DROP, "CM_AreasConnected: area > numareas" );

	if( cm->floodnums[area1] == cm->floodnums[area2] )
		return qtrue;

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
	int		i;
	int		floodnum;
	int		bytes;

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
	int		i, bytes, numportals;

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
	int		i, numportals;

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
===================
CM_WritePortalState

Writes the portal state to a savegame file
===================
*/
void CM_WritePortalState( cm_t *cm, fileHandle_t f ) {
	// FIXME: incompatible savegames
	FS_Write( cm->portalopen, sizeof( qboolean ) * cm->cache->numareaportals, f );
}

/*
===================
CM_ReadPortalState

Reads the portal state from a savegame file
and recalculates the area connections
===================
*/
void CM_ReadPortalState( cm_t *cm, fileHandle_t f ) {
	FS_Read( cm->portalopen, sizeof( qboolean ) * cm->cache->numareaportals, f );
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
	int		cluster;

	if( !node->plane ) {
		leaf = ( cleaf_t * )node;
		cluster = leaf->cluster;
		if( cluster == -1 )
			return qfalse;
		if( Q_IsBitSet( visbits, cluster ) )
			return qtrue;
		return qfalse;
	}

	if( CM_HeadnodeVisible( node->children[0], visbits ) )
		return qtrue;
	return CM_HeadnodeVisible( node->children[1], visbits );
}


/*
============
CM_FatPVS

The client will interpolate the view position,
so we can't use a single PVS point
===========
*/
byte *CM_FatPVS( cm_t *cm, const vec3_t org ) {
	static byte	fatpvs[MAX_MAP_LEAFS/8];
	cleaf_t	*leafs[64];
	int		clusters[64];
	int		i, j, count;
	int		longs;
	uint32	*src, *dst;
	vec3_t	mins, maxs;
		
	if( !cm->cache ) {	// map not loaded
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

	src = ( uint32 * )CM_ClusterPVS( cm, clusters[0] );
	dst = ( uint32 * )fatpvs;
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
		src = ( uint32 * )CM_ClusterPVS( cm, clusters[i] );
		dst = ( uint32 * )fatpvs;
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
	map_load_entities = Cvar_Get( "map_load_entities", "0", 0 );
}

