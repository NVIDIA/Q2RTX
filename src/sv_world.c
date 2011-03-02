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

typedef struct areanode_s {
    int     axis;       // -1 = leaf node
    float   dist;
    struct areanode_s   *children[2];
    list_t  trigger_edicts;
    list_t  solid_edicts;
} areanode_t;

#define    AREA_DEPTH    4
#define    AREA_NODES    32

static areanode_t   sv_areanodes[AREA_NODES];
static int          sv_numareanodes;

static float    *area_mins, *area_maxs;
static edict_t  **area_list;
static int      area_count, area_maxcount;
static int      area_type;

/*
===============
SV_CreateAreaNode

Builds a uniformly subdivided tree for the given world size
===============
*/
static areanode_t *SV_CreateAreaNode (int depth, vec3_t mins, vec3_t maxs) {
    areanode_t  *anode;
    vec3_t      size;
    vec3_t      mins1, maxs1, mins2, maxs2;

    anode = &sv_areanodes[sv_numareanodes];
    sv_numareanodes++;

    List_Init (&anode->trigger_edicts);
    List_Init (&anode->solid_edicts);
    
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
    mmodel_t *cm;
    edict_t *ent;
    int i;

    memset( sv_areanodes, 0, sizeof( sv_areanodes ) );
    sv_numareanodes = 0;

    if( sv.cm.cache ) {
        cm = &sv.cm.cache->models[0];
        SV_CreateAreaNode( 0, cm->mins, cm->maxs );
    }

    // make sure all entities are unlinked
    for( i = 0; i < ge->max_edicts; i++ ) {
        ent = EDICT_NUM( i );
        ent->area.prev = ent->area.next = NULL;
    }
}


/*
===============
SV_LinkEdict

General purpose routine shared between game DLL and MVD code.
Links entity to PVS leafs.
===============
*/
void SV_LinkEdict( cm_t *cm, edict_t *ent ) {
    mleaf_t     *leafs[MAX_TOTAL_ENT_LEAFS];
    int         clusters[MAX_TOTAL_ENT_LEAFS];
    int         num_leafs;
    int         i, j;
    int         area;
    mnode_t     *topnode;

    // set the size
    VectorSubtract (ent->maxs, ent->mins, ent->size);

    // set the abs box
    if (ent->solid == SOLID_BSP &&
        (ent->s.angles[0] || ent->s.angles[1] || ent->s.angles[2]) )
    {    // expand for rotation
        float        max, v;
        int            i;

        max = 0;
        for (i=0 ; i<3 ; i++) {
            v = Q_fabs( ent->mins[i]);
            if (v > max)
                max = v;
            v = Q_fabs( ent->maxs[i]);
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
    {    // normal
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
    num_leafs = CM_BoxLeafs( cm, ent->absmin, ent->absmax,
        leafs, MAX_TOTAL_ENT_LEAFS, &topnode);

    // set areas
    for (i=0 ; i<num_leafs ; i++)
    {
        clusters[i] = CM_LeafCluster (leafs[i]);
        area = CM_LeafArea (leafs[i]);
        if (area)
        {    // doors may legally straggle two areas,
            // but nothing should evern need more than that
            if (ent->areanum && ent->areanum != area)
            {
                if (ent->areanum2 && ent->areanum2 != area && sv.state == ss_loading)
                {
                    Com_DPrintf ("Object touching 3 areas at %f %f %f\n",
                        ent->absmin[0], ent->absmin[1], ent->absmin[2]);
                }
                ent->areanum2 = area;
            }
            else
                ent->areanum = area;
        }
    }

    if (num_leafs >= MAX_TOTAL_ENT_LEAFS)
    {    // assume we missed some leafs, and mark by headnode
        ent->num_clusters = -1;
        ent->headnode = CM_NumNode( cm, topnode );
    }
    else
    {
        ent->num_clusters = 0;
        for (i=0 ; i<num_leafs ; i++)
        {
            if (clusters[i] == -1)
                continue;        // not a visible leaf
            for (j=0 ; j<i ; j++)
                if (clusters[j] == clusters[i])
                    break;
            if (j == i)
            {
                if (ent->num_clusters == MAX_ENT_CLUSTERS)
                {    // assume we missed some leafs, and mark by headnode
                    ent->num_clusters = -1;
                    ent->headnode = CM_NumNode( cm, topnode );
                    break;
                }

                ent->clusternums[ent->num_clusters++] = clusters[i];
            }
        }
    }
}

void PF_UnlinkEdict (edict_t *ent) {
    if (!ent->area.prev)
        return;        // not linked in anywhere
    List_Remove (&ent->area);
    ent->area.prev = ent->area.next = NULL;
}

void PF_LinkEdict (edict_t *ent) {
    areanode_t *node;
    server_entity_t *sent;
    int entnum;

    if (ent->area.prev)
        PF_UnlinkEdict (ent);    // unlink from old position
        
    if (ent == ge->edicts)
        return;        // don't add the world

    if (!ent->inuse) {
        Com_DPrintf( "%s: entity %d is not in use\n", __func__, NUM_FOR_EDICT( ent ) );
        return;
    }

    if( !sv.cm.cache ) {
        return;
    }

    entnum = NUM_FOR_EDICT( ent );
    sent = &sv.entities[entnum];

    // encode the size into the entity_state for client prediction
    switch( ent->solid ) {
    case SOLID_BBOX:
        if( ( ent->svflags & SVF_DEADMONSTER ) || VectorCompare( ent->mins, ent->maxs ) ) {
            ent->s.solid = 0;
            sent->solid32 = 0;
        } else {
            ent->s.solid = MSG_PackSolid16( ent->mins, ent->maxs );
            sent->solid32 = MSG_PackSolid32( ent->mins, ent->maxs );
        }
        break;
    case SOLID_BSP:
        ent->s.solid = PACKED_BSP;      // a SOLID_BBOX will never create this value
        sent->solid32 = PACKED_BSP;     // FIXME: use 255?
        break;
    default:
        ent->s.solid = 0;
        sent->solid32 = 0;
        break;
    }

    SV_LinkEdict( &sv.cm, ent );

    // if first time, make sure old_origin is valid
    if (!ent->linkcount) {
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
            break;        // crosses the node
    }
    
    // link it in    
    if (ent->solid == SOLID_TRIGGER)
        List_Append( &node->trigger_edicts, &ent->area );
    else
        List_Append( &node->solid_edicts, &ent->area );
}


/*
====================
SV_AreaEdicts_r

====================
*/
static void SV_AreaEdicts_r (areanode_t *node) {
    list_t      *start;
    edict_t     *check;

    // touch linked edicts
    if (area_type == AREA_SOLID)
        start = &node->solid_edicts;
    else
        start = &node->trigger_edicts;

    LIST_FOR_EACH( edict_t, check, start, area ) {
        if (check->solid == SOLID_NOT)
            continue;        // deactivated
        if (check->absmin[0] > area_maxs[0]
        || check->absmin[1] > area_maxs[1]
        || check->absmin[2] > area_maxs[2]
        || check->absmax[0] < area_mins[0]
        || check->absmax[1] < area_mins[1]
        || check->absmax[2] < area_mins[2])
            continue;        // not touching

        if (area_count == area_maxcount) {
            Com_WPrintf ("SV_AreaEdicts: MAXCOUNT\n");
            return;
        }

        area_list[area_count] = check;
        area_count++;
    }
    
    if (node->axis == -1)
        return;        // terminal node

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
static mnode_t *SV_HullForEntity( edict_t *ent ) {
    mmodel_t    *model;

    if( ent->solid == SOLID_BSP ) {
        int index = ent->s.modelindex - 1;

        // explicit hulls in the BSP model
        if( index <= 0 || index >= sv.cm.cache->nummodels ) {
            Com_Error( ERR_DROP, "%s: inline model %d out of range",
                __func__, index );
        }

        model = &sv.cm.cache->models[index];
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
    edict_t     *touch[MAX_EDICTS], *hit;
    int         i, num;
    int         contents, c2;
    mnode_t     *headnode;

    if( !sv.cm.cache ) {
        Com_Error( ERR_DROP, "%s: no map loaded", __func__ );
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

        c2 = CM_TransformedPointContents (p, headnode,
            hit->s.origin, hit->s.angles);

        contents |= c2;
    }

    return contents;
}



typedef struct {
    vec3_t      boxmins, boxmaxs;// enclose the test object along entire move
    vec_t       *mins, *maxs;    // size of the moving object
    vec_t       *start, *end;
    trace_t     *trace;
    edict_t     *passedict;
    int         contentmask;
} moveclip_t;

/*
====================
SV_ClipMoveToEntities

====================
*/
static void SV_ClipMoveToEntities( moveclip_t *clip ) {
    int         i, num;
    edict_t     *touchlist[MAX_EDICTS], *touch;
    trace_t     trace;
    mnode_t     *headnode;

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
                continue;    // don't clip against own missiles
            if (clip->passedict->owner == touch)
                continue;    // don't clip against owner
        }

        if ( !(clip->contentmask & CONTENTS_DEADMONSTER)
        && (touch->svflags & SVF_DEADMONSTER) )
                continue;

        // might intersect, so do an exact clip
        headnode = SV_HullForEntity (touch);

        CM_TransformedBoxTrace (&trace, clip->start, clip->end,
            clip->mins, clip->maxs, headnode,  clip->contentmask,
            touch->s.origin, touch->s.angles);

        CM_ClipEntity( clip->trace, &trace, touch );
    }
}


/*
==================
SV_TraceBounds
==================
*/
static void SV_TraceBounds (moveclip_t *clip) {
    int        i;
    
    for (i=0 ; i<3 ; i++) {
        if (clip->end[i] > clip->start[i]) {
            clip->boxmins[i] = clip->start[i] + clip->mins[i] - 1;
            clip->boxmaxs[i] = clip->end[i] + clip->maxs[i] + 1;
        } else {
            clip->boxmins[i] = clip->end[i] + clip->mins[i] - 1;
            clip->boxmaxs[i] = clip->start[i] + clip->maxs[i] + 1;
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
trace_t *SV_Trace(  trace_t     *trace,
                    vec3_t      start,
                    vec3_t      mins,
                    vec3_t      maxs,
                    vec3_t      end,
                    edict_t     *passedict,
                    int         contentmask )
{
    moveclip_t    clip;

    if( !sv.cm.cache ) {
        Com_Error( ERR_DROP, "%s: no map loaded", __func__ );
    }

    if( ++sv.tracecount > 10000 ) {
        Com_EPrintf( "%s: game DLL caught in infinite loop!\n", __func__ );
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
    CM_BoxTrace( trace, start, end, mins, maxs,
        sv.cm.cache->nodes, contentmask );
    trace->ent = ge->edicts;
    if (trace->fraction == 0) {
        return trace;        // blocked by the world
    }

    memset( &clip, 0, sizeof( clip ) );
    clip.trace = trace;
    clip.contentmask = contentmask;
    clip.start = start;
    clip.end = end;
    clip.mins = mins;
    clip.maxs = maxs;
    clip.passedict = passedict;
    
    // create the bounding box of the entire move
    SV_TraceBounds( &clip );

    // clip to other solid entities
    SV_ClipMoveToEntities( &clip );

    return trace;
}

/*
==================
SV_Trace_Native

Variant of SV_Trace for native game ABI
==================
*/
trace_t SV_Trace_Native (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, edict_t *passedict, int contentmask) {
    trace_t trace;

    SV_Trace( &trace, start, mins, maxs, end, passedict, contentmask );

    return trace;
}

