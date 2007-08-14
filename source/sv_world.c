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
// world.c -- world query functions

#include "sv_local.h"

/*
===============================================================================

ENTITY AREA CHECKING

FIXME: this use of "area" is different from the bsp file use
===============================================================================
*/

// (type *)STRUCT_FROM_LINK(link_t *link, type, member)
// ent = STRUCT_FROM_LINK(link,entity_t,order)
// FIXME: remove this mess!
#define	STRUCT_FROM_LINK(l,t,m) ((t *)((byte *)l - (int)&(((t *)0)->m)))

#define	EDICT_FROM_AREA(l) STRUCT_FROM_LINK(l,edict_t,area)

typedef struct areanode_s
{
	int		axis;		// -1 = leaf node
	float	dist;
	struct areanode_s	*children[2];
	link_t	trigger_edicts;
	link_t	solid_edicts;
} areanode_t;

#define	AREA_DEPTH	4
#define	AREA_NODES	32

areanode_t	sv_areanodes[AREA_NODES];
int			sv_numareanodes;

float	*area_mins, *area_maxs;
edict_t	**area_list;
int		area_count, area_maxcount;
int		area_type;


// ClearLink is used for new headnodes
void ClearLink (link_t *l)
{
	l->prev = l->next = l;
}

void RemoveLink (link_t *l)
{
	l->next->prev = l->prev;
	l->prev->next = l->next;
}

void InsertLinkBefore (link_t *l, link_t *before)
{
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
}

/*
===============
SV_CreateAreaNode

Builds a uniformly subdivided tree for the given world size
===============
*/
areanode_t *SV_CreateAreaNode (int depth, vec3_t mins, vec3_t maxs)
{
	areanode_t	*anode;
	vec3_t		size;
	vec3_t		mins1, maxs1, mins2, maxs2;

	anode = &sv_areanodes[sv_numareanodes];
	sv_numareanodes++;

	ClearLink (&anode->trigger_edicts);
	ClearLink (&anode->solid_edicts);
	
	if (depth == AREA_DEPTH)
	{
		anode->axis = -1;
		anode->children[0] = anode->children[1] = NULL;
		return anode;
	}
	
	VectorSubtract (maxs, mins, size);
	if (size[0] > size[1])
		anode->axis = 0;
	else
		anode->axis = 1;
	
	anode->dist = 0.5 * (maxs[anode->axis] + mins[anode->axis]);
	VectorCopy (mins, mins1);	
	VectorCopy (mins, mins2);	
	VectorCopy (maxs, maxs1);	
	VectorCopy (maxs, maxs2);	
	
	maxs1[anode->axis] = mins2[anode->axis] = anode->dist;
	
	anode->children[0] = SV_CreateAreaNode (depth+1, mins2, maxs2);
	anode->children[1] = SV_CreateAreaNode (depth+1, mins1, maxs1);

	return anode;
}

/*
===============
SV_ClearWorld

===============
*/
void SV_ClearWorld( void ) {
	cmodel_t *cm;
	edict_t *ent;
	int i;

	memset( sv_areanodes, 0, sizeof( sv_areanodes ) );
	sv_numareanodes = 0;

	if( sv.cm.cache ) {
		cm = &sv.cm.cache->cmodels[0];
		SV_CreateAreaNode( 0, cm->mins, cm->maxs );
	}

	/* make sure all entities are unlinked */
	for( i = 0; i < MAX_EDICTS; i++ ) {
		ent = EDICT_NUM( i );
		ent->area.prev = ent->area.next = NULL;
	}
}


/*
===============
SV_UnlinkEdict

===============
*/
void SV_UnlinkEdict (edict_t *ent)
{
	if (!ent->area.prev)
		return;		// not linked in anywhere
	RemoveLink (&ent->area);
	ent->area.prev = ent->area.next = NULL;
}


/*
===============
SV_LinkEdict

===============
*/
void SV_LinkEdict (edict_t *ent)
{
	areanode_t	*node;
	cleaf_t		*leafs[MAX_TOTAL_ENT_LEAFS];
	int			clusters[MAX_TOTAL_ENT_LEAFS];
	int			num_leafs;
	int			i, j, k;
	int			area;
	cnode_t		*topnode;

	if (ent->area.prev)
		SV_UnlinkEdict (ent);	// unlink from old position
		
	if (ent == ge->edicts)
		return;		// don't add the world

	if (!ent->inuse)
		return;

	if( !sv.cm.cache ) {
		return;
	}

	// set the size
	VectorSubtract (ent->maxs, ent->mins, ent->size);
	
	// encode the size into the entity_state for client prediction
	if (ent->solid == SOLID_BBOX && !(ent->svflags & SVF_DEADMONSTER))
	{	// assume that x/y are equal and symetric
		i = ent->maxs[0]/8;
		if (i<1)
			i = 1;
		if (i>31)
			i = 31;

		// z is not symetric
		j = (-ent->mins[2])/8;
		if (j<1)
			j = 1;
		if (j>31)
			j = 31;

		// and z maxs can be negative...
		k = (ent->maxs[2]+32)/8;
		if (k<1)
			k = 1;
		if (k>63)
			k = 63;

		ent->s.solid = (k<<10) | (j<<5) | i;
	}
	else if (ent->solid == SOLID_BSP)
	{
		ent->s.solid = 31;		// a solid_bbox will never create this value
	}
	else
		ent->s.solid = 0;

	// set the abs box
	if (ent->solid == SOLID_BSP && 
	(ent->s.angles[0] || ent->s.angles[1] || ent->s.angles[2]) )
	{	// expand for rotation
		float		max, v;
		int			i;

		max = 0;
		for (i=0 ; i<3 ; i++)
		{
			v =fabs( ent->mins[i]);
			if (v > max)
				max = v;
			v =fabs( ent->maxs[i]);
			if (v > max)
				max = v;
		}
		for (i=0 ; i<3 ; i++)
		{
			ent->absmin[i] = ent->s.origin[i] - max;
			ent->absmax[i] = ent->s.origin[i] + max;
		}
	}
	else
	{	// normal
		VectorAdd (ent->s.origin, ent->mins, ent->absmin);	
		VectorAdd (ent->s.origin, ent->maxs, ent->absmax);
	}

	// because movement is clipped an epsilon away from an actual edge,
	// we must fully check even when bounding boxes don't quite touch
	ent->absmin[0] -= 1;
	ent->absmin[1] -= 1;
	ent->absmin[2] -= 1;
	ent->absmax[0] += 1;
	ent->absmax[1] += 1;
	ent->absmax[2] += 1;

// link to PVS leafs
	ent->num_clusters = 0;
	ent->areanum = 0;
	ent->areanum2 = 0;

	//get all leafs, including solids
	num_leafs = CM_BoxLeafs( &sv.cm, ent->absmin, ent->absmax,
		leafs, MAX_TOTAL_ENT_LEAFS, &topnode);

	// set areas
	for (i=0 ; i<num_leafs ; i++)
	{
		clusters[i] = CM_LeafCluster (leafs[i]);
		area = CM_LeafArea (leafs[i]);
		if (area)
		{	// doors may legally straggle two areas,
			// but nothing should evern need more than that
			if (ent->areanum && ent->areanum != area)
			{
				if (ent->areanum2 && ent->areanum2 != area && sv.state == ss_loading)
					Com_DPrintf ("Object touching 3 areas at %f %f %f\n",
					ent->absmin[0], ent->absmin[1], ent->absmin[2]);
				ent->areanum2 = area;
			}
			else
				ent->areanum = area;
		}
	}

	if (num_leafs >= MAX_TOTAL_ENT_LEAFS)
	{	// assume we missed some leafs, and mark by headnode
		ent->num_clusters = -1;
		ent->headnode = topnode - sv.cm.cache->nodes;
	}
	else
	{
		ent->num_clusters = 0;
		for (i=0 ; i<num_leafs ; i++)
		{
			if (clusters[i] == -1)
				continue;		// not a visible leaf
			for (j=0 ; j<i ; j++)
				if (clusters[j] == clusters[i])
					break;
			if (j == i)
			{
				if (ent->num_clusters == MAX_ENT_CLUSTERS)
				{	// assume we missed some leafs, and mark by headnode
					ent->num_clusters = -1;
					ent->headnode = topnode - sv.cm.cache->nodes;
					break;
				}

				ent->clusternums[ent->num_clusters++] = clusters[i];
			}
		}
	}

	// if first time, make sure old_origin is valid
	if (!ent->linkcount && sv.state != ss_broadcast)
	{
		VectorCopy (ent->s.origin, ent->s.old_origin);
	}
	ent->linkcount++;

	if (ent->solid == SOLID_NOT)
		return;

// find the first node that the ent's box crosses
	node = sv_areanodes;
	while (1)
	{
		if (node->axis == -1)
			break;
		if (ent->absmin[node->axis] > node->dist)
			node = node->children[0];
		else if (ent->absmax[node->axis] < node->dist)
			node = node->children[1];
		else
			break;		// crosses the node
	}
	
	// link it in	
	if (ent->solid == SOLID_TRIGGER)
		InsertLinkBefore (&ent->area, &node->trigger_edicts);
	else
		InsertLinkBefore (&ent->area, &node->solid_edicts);

}


/*
====================
SV_AreaEdicts_r

====================
*/
void SV_AreaEdicts_r (areanode_t *node)
{
	link_t		*l, *next, *start;
	edict_t		*check;
	int			count;

	count = 0;

	// touch linked edicts
	if (area_type == AREA_SOLID)
		start = &node->solid_edicts;
	else
		start = &node->trigger_edicts;

	for (l=start->next  ; l != start ; l = next)
	{
		if( !l ) {
			Com_Error( ERR_DROP, "SV_AreaEdicts: NULL link" );
		}
		next = l->next;
		check = EDICT_FROM_AREA(l);

		if (check->solid == SOLID_NOT)
			continue;		// deactivated
		if (check->absmin[0] > area_maxs[0]
		|| check->absmin[1] > area_maxs[1]
		|| check->absmin[2] > area_maxs[2]
		|| check->absmax[0] < area_mins[0]
		|| check->absmax[1] < area_mins[1]
		|| check->absmax[2] < area_mins[2])
			continue;		// not touching

		if (area_count == area_maxcount)
		{
			Com_WPrintf ("SV_AreaEdicts: MAXCOUNT\n");
			return;
		}

		area_list[area_count] = check;
		area_count++;
	}
	
	if (node->axis == -1)
		return;		// terminal node

	// recurse down both sides
	if ( area_maxs[node->axis] > node->dist )
		SV_AreaEdicts_r ( node->children[0] );
	if ( area_mins[node->axis] < node->dist )
		SV_AreaEdicts_r ( node->children[1] );
}

/*
================
SV_AreaEdicts
================
*/
int SV_AreaEdicts (vec3_t mins, vec3_t maxs, edict_t **list,
	int maxcount, int areatype)
{
	area_mins = mins;
	area_maxs = maxs;
	area_list = list;
	area_count = 0;
	area_maxcount = maxcount;
	area_type = areatype;

	SV_AreaEdicts_r (sv_areanodes);

	return area_count;
}


//===========================================================================

/*
================
SV_HullForEntity

Returns a headnode that can be used for testing or clipping an
object of mins/maxs size.
================
*/
static cnode_t *SV_HullForEntity( edict_t *ent ) {
	cmodel_t	*model;

	if( ent->solid == SOLID_BSP ) {
		// explicit hulls in the BSP model
		if( ent->s.modelindex < 1 || ent->s.modelindex > sv.cm.cache->numcmodels ) {
			Com_Error( ERR_DROP, "SV_HullForEntity: inline model index %d out of range", ent->s.modelindex );
		}

		model = &sv.cm.cache->cmodels[ ent->s.modelindex - 1 ];

		return model->headnode;
	}

	// create a temp hull from bounding box sizes
	return CM_HeadnodeForBox( ent->mins, ent->maxs );
}

/*
=============
SV_PointContents
=============
*/
int SV_PointContents (vec3_t p)
{
	edict_t		*touch[MAX_EDICTS], *hit;
	int			i, num;
	int			contents, c2;
	cnode_t		*headnode;
	float		*angles;

	if( !sv.cm.cache ) {
		Com_Error( ERR_DROP, "SV_PointContents: no map loaded" );
	}

	// get base contents from world
	contents = CM_PointContents (p, sv.cm.cache->nodes);

	// or in contents from all the other entities
	num = SV_AreaEdicts (p, p, touch, MAX_EDICTS, AREA_SOLID);

	for (i=0 ; i<num ; i++)
	{
		hit = touch[i];

		// might intersect, so do an exact clip
		headnode = SV_HullForEntity (hit);
		angles = hit->s.angles;
		if (hit->solid != SOLID_BSP)
			angles = vec3_origin;	// boxes don't rotate

		c2 = CM_TransformedPointContents (p, headnode, hit->s.origin, hit->s.angles);

		contents |= c2;
	}

	return contents;
}



typedef struct {
	vec3_t		boxmins, boxmaxs;// enclose the test object along entire move
	float		*mins, *maxs;	// size of the moving object
	vec3_t		mins2, maxs2;	// size when clipping against mosnters
	float		*start, *end;
	trace_t		*trace;
	edict_t		*passedict;
	int			contentmask;
} moveclip_t;

/*
====================
SV_ClipMoveToEntities

====================
*/
void SV_ClipMoveToEntities ( moveclip_t *clip )
{
	int			i, num;
	edict_t		*touchlist[MAX_EDICTS], *touch;
	trace_t		trace;
	cnode_t		*headnode;
	float		*angles;

	num = SV_AreaEdicts (clip->boxmins, clip->boxmaxs, touchlist
		, MAX_EDICTS, AREA_SOLID);

	// be careful, it is possible to have an entity in this
	// list removed before we get to it (killtriggered)
	for (i=0 ; i<num ; i++)
	{
		touch = touchlist[i];
		if (touch->solid == SOLID_NOT)
			continue;
		if (touch == clip->passedict)
			continue;
		if (clip->trace->allsolid)
			return;
		if (clip->passedict)
		{
		 	if (touch->owner == clip->passedict)
				continue;	// don't clip against own missiles
			if (clip->passedict->owner == touch)
				continue;	// don't clip against owner
		}

		if ( !(clip->contentmask & CONTENTS_DEADMONSTER)
		&& (touch->svflags & SVF_DEADMONSTER) )
				continue;

		// might intersect, so do an exact clip
		headnode = SV_HullForEntity (touch);
		angles = touch->s.angles;
		if (touch->solid != SOLID_BSP)
			angles = vec3_origin;	// boxes don't rotate

		if (touch->svflags & SVF_MONSTER)
			CM_TransformedBoxTrace (&trace, clip->start, clip->end,
				clip->mins2, clip->maxs2, headnode, clip->contentmask,
				touch->s.origin, angles);
		else
			CM_TransformedBoxTrace (&trace, clip->start, clip->end,
				clip->mins, clip->maxs, headnode,  clip->contentmask,
				touch->s.origin, angles);

		clip->trace->allsolid |= trace.allsolid;
		clip->trace->startsolid |= trace.startsolid;
		if( trace.fraction < clip->trace->fraction ) {
			clip->trace->fraction = trace.fraction;
            VectorCopy( trace.endpos, clip->trace->endpos );
            clip->trace->plane = trace.plane;
            clip->trace->surface = trace.surface;
            clip->trace->contents |= trace.contents;
			clip->trace->ent = touch;
        }
	}
}


/*
==================
SV_TraceBounds
==================
*/
static void SV_TraceBounds (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, vec3_t boxmins, vec3_t boxmaxs)
{
	int		i;
	
	for (i=0 ; i<3 ; i++)
	{
		if (end[i] > start[i])
		{
			boxmins[i] = start[i] + mins[i] - 1;
			boxmaxs[i] = end[i] + maxs[i] + 1;
		}
		else
		{
			boxmins[i] = end[i] + mins[i] - 1;
			boxmaxs[i] = start[i] + maxs[i] + 1;
		}
	}
}

/*
==================
SV_Trace

Moves the given mins/maxs volume through the world from start to end.

Passedict and edicts owned by passedict are explicitly not checked.

==================
*/
trace_t SV_Trace (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, edict_t *passedict, int contentmask) {
	moveclip_t	clip;
	trace_t trace;

	if( !sv.cm.cache ) {
		Com_Error( ERR_DROP, "SV_Trace: no map loaded" );
	}

	if( ++sv.tracecount > 10000 ) {
		Com_EPrintf( "SV_Trace: game DLL caught in infinite loop!\n" );
		memset( &trace, 0, sizeof( trace ) );
		trace.fraction = 1;
		trace.ent = ge->edicts;
		VectorCopy( end, trace.endpos );
		sv.tracecount = 0;
		return trace;
	}

	if (!mins)
		mins = vec3_origin;
	if (!maxs)
		maxs = vec3_origin;

	// clip to world
	CM_BoxTrace ( &trace, start, end, mins, maxs, sv.cm.cache->nodes, contentmask);
	trace.ent = ge->edicts;
	if (trace.fraction == 0) {
		return trace;		// blocked by the world
	}

	memset ( &clip, 0, sizeof ( moveclip_t ) );
	clip.trace = &trace;
	clip.contentmask = contentmask;
	clip.start = start;
	clip.end = end;
	clip.mins = mins;
	clip.maxs = maxs;
	clip.passedict = passedict;

	VectorCopy (mins, clip.mins2);
	VectorCopy (maxs, clip.maxs2);
	
	// create the bounding box of the entire move
	SV_TraceBounds ( start, clip.mins2, clip.maxs2, end, clip.boxmins, clip.boxmaxs );

	// clip to other solid entities
	SV_ClipMoveToEntities ( &clip );

	return trace;
	
}

/*
==================
SV_Trace_Old

HACK: different compilers (and even different versions
of the same compiler) handle returning of structures differently.

This function is intended to be binary-compatible with
game DLLs built by earlier versions of GCC.

Game DLLs built by MSVC seems to behave similary to old GCC
(except they additionaly require pointer to destination
structure to be retured in EAX register), so this function is linked in
when building with MinGW, under assumption that no mods built with MinGW ever exist.
==================
*/
trace_t *SV_Trace_Old (trace_t *trace, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end,
					   edict_t *passedict, int contentmask)
{
	moveclip_t	clip;

	if( !sv.cm.cache ) {
		Com_Error( ERR_DROP, "SV_Trace: no map loaded" );
	}

	if( ++sv.tracecount > 10000 ) {
		Com_EPrintf( "SV_Trace: game DLL caught in infinite loop!\n" );
		memset( trace, 0, sizeof( *trace ) );
		trace->fraction = 1;
		trace->ent = ge->edicts;
		VectorCopy( end, trace->endpos );
		sv.tracecount = 0;
		return trace;
	}

	if (!mins)
		mins = vec3_origin;
	if (!maxs)
		maxs = vec3_origin;

	// clip to world
	CM_BoxTrace( trace, start, end, mins, maxs, sv.cm.cache->nodes, contentmask );
	trace->ent = ge->edicts;
	if (trace->fraction == 0) {
		return trace;		// blocked by the world
	}

	memset ( &clip, 0, sizeof ( moveclip_t ) );
	clip.trace = trace;
	clip.contentmask = contentmask;
	clip.start = start;
	clip.end = end;
	clip.mins = mins;
	clip.maxs = maxs;
	clip.passedict = passedict;

	VectorCopy (mins, clip.mins2);
	VectorCopy (maxs, clip.maxs2);
	
	// create the bounding box of the entire move
	SV_TraceBounds ( start, clip.mins2, clip.maxs2, end, clip.boxmins, clip.boxmaxs );

	// clip to other solid entities
	SV_ClipMoveToEntities ( &clip );

	return trace;
}

