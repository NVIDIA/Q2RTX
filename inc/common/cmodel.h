/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef CMODEL_H
#define CMODEL_H

#include "common/bsp.h"

// bitmasks communicated by server
#define MAX_MAP_AREA_BYTES      (MAX_MAP_AREAS / 8)
#define MAX_MAP_PORTAL_BYTES    MAX_MAP_AREA_BYTES

typedef struct {
    bsp_t       *cache;
    int         *floodnums;     // if two areas have equal floodnums,
                                // they are connected
    bool        *portalopen;
} cm_t;

void        CM_Init(void);

void        CM_FreeMap(cm_t *cm);
int         CM_LoadMap(cm_t *cm, const char *name);

int         CM_NumClusters(cm_t *cm);
int         CM_NumInlineModels(cm_t *cm);
char        *CM_EntityString(cm_t *cm);
mnode_t     *CM_NodeNum(cm_t *cm, int number);
mleaf_t     *CM_LeafNum(cm_t *cm, int number);

#define CM_InlineModel(cm, name) BSP_InlineModel((cm)->cache, name)

#define CM_NumNode(cm, node) ((node) ? ((node) - (cm)->cache->nodes) : -1)

// creates a clipping hull for an arbitrary box
mnode_t     *CM_HeadnodeForBox(const vec3_t mins, const vec3_t maxs);

// returns an ORed contents mask
int         CM_PointContents(const vec3_t p, mnode_t *headnode);
int         CM_TransformedPointContents(const vec3_t p, mnode_t *headnode,
                                        const vec3_t origin, const vec3_t angles);

void        CM_BoxTrace(trace_t *trace,
                        const vec3_t start, const vec3_t end,
                        const vec3_t mins, const vec3_t maxs,
                        mnode_t *headnode, int brushmask);
void        CM_TransformedBoxTrace(trace_t *trace,
                                   const vec3_t start, const vec3_t end,
                                   const vec3_t mins, const vec3_t maxs,
                                   mnode_t *headnode, int brushmask,
                                   const vec3_t origin, const vec3_t angles);
void        CM_ClipEntity(trace_t *dst, const trace_t *src, struct edict_s *ent);

// call with topnode set to the headnode, returns with topnode
// set to the first node that splits the box
int         CM_BoxLeafs(cm_t *cm, const vec3_t mins, const vec3_t maxs,
                        mleaf_t **list, int listsize, mnode_t **topnode);
mleaf_t     *CM_PointLeaf(cm_t *cm, const vec3_t p);

byte        *CM_FatPVS(cm_t *cm, byte *mask, const vec3_t org, int vis);

void        CM_SetAreaPortalState(cm_t *cm, int portalnum, bool open);
bool        CM_AreasConnected(cm_t *cm, int area1, int area2);

int         CM_WriteAreaBits(cm_t *cm, byte *buffer, int area);
int         CM_WritePortalBits(cm_t *cm, byte *buffer);
void        CM_SetPortalStates(cm_t *cm, byte *buffer, int bytes);
bool        CM_HeadnodeVisible(mnode_t *headnode, byte *visbits);

void        CM_WritePortalState(cm_t *cm, qhandle_t f);
void        CM_ReadPortalState(cm_t *cm, qhandle_t f);

#endif // CMODEL_H
