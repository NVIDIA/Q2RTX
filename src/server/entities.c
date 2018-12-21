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

#include "server.h"

/*
=============================================================================

Encode a client frame onto the network channel

=============================================================================
*/

// some protocol optimizations are disabled when recording a demo
#define Q2PRO_OPTIMIZE(c) \
    ((c)->protocol == PROTOCOL_VERSION_Q2PRO && !(c)->settings[CLS_RECORDING])

/*
=============
SV_EmitPacketEntities

Writes a delta update of an entity_packed_t list to the message.
=============
*/
static void SV_EmitPacketEntities(client_t         *client,
                                  client_frame_t   *from,
                                  client_frame_t   *to,
                                  int              clientEntityNum)
{
    entity_packed_t *newent;
    const entity_packed_t *oldent;
    unsigned i, oldindex, newindex, from_num_entities;
    int oldnum, newnum;
    msgEsFlags_t flags;

    if (!from)
        from_num_entities = 0;
    else
        from_num_entities = from->num_entities;

    newindex = 0;
    oldindex = 0;
    oldent = newent = NULL;
    while (newindex < to->num_entities || oldindex < from_num_entities) {
        if (newindex >= to->num_entities) {
            newnum = 9999;
        } else {
            i = (to->first_entity + newindex) % svs.num_entities;
            newent = &svs.entities[i];
            newnum = newent->number;
        }

        if (oldindex >= from_num_entities) {
            oldnum = 9999;
        } else {
            i = (from->first_entity + oldindex) % svs.num_entities;
            oldent = &svs.entities[i];
            oldnum = oldent->number;
        }

        if (newnum == oldnum) {
            // Delta update from old position. Because the force parm is false,
            // this will not result in any bytes being emitted if the entity has
            // not changed at all. Note that players are always 'newentities',
            // this updates their old_origin always and prevents warping in case
            // of packet loss.
            flags = client->esFlags;
            if (newnum <= client->maxclients) {
                flags |= MSG_ES_NEWENTITY;
            }
            if (newnum == clientEntityNum) {
                flags |= MSG_ES_FIRSTPERSON;
                VectorCopy(oldent->origin, newent->origin);
                VectorCopy(oldent->angles, newent->angles);
            }
            if (Q2PRO_SHORTANGLES(client, newnum)) {
                flags |= MSG_ES_SHORTANGLES;
            }
            MSG_WriteDeltaEntity(oldent, newent, flags);
            oldindex++;
            newindex++;
            continue;
        }

        if (newnum < oldnum) {
            // this is a new entity, send it from the baseline
            flags = client->esFlags | MSG_ES_FORCE | MSG_ES_NEWENTITY;
            oldent = client->baselines[newnum >> SV_BASELINES_SHIFT];
            if (oldent) {
                oldent += (newnum & SV_BASELINES_MASK);
            } else {
                oldent = &nullEntityState;
            }
            if (newnum == clientEntityNum) {
                flags |= MSG_ES_FIRSTPERSON;
                VectorCopy(oldent->origin, newent->origin);
                VectorCopy(oldent->angles, newent->angles);
            }
            if (Q2PRO_SHORTANGLES(client, newnum)) {
                flags |= MSG_ES_SHORTANGLES;
            }
            MSG_WriteDeltaEntity(oldent, newent, flags);
            newindex++;
            continue;
        }

        if (newnum > oldnum) {
            // the old entity isn't present in the new message
            MSG_WriteDeltaEntity(oldent, NULL, MSG_ES_FORCE);
            oldindex++;
            continue;
        }
    }

    MSG_WriteShort(0);      // end of packetentities
}

static client_frame_t *get_last_frame(client_t *client)
{
    client_frame_t *frame;

    if (client->lastframe <= 0) {
        // client is asking for a retransmit
        client->frames_nodelta++;
        return NULL;
    }

    client->frames_nodelta = 0;

    if (client->framenum - client->lastframe >= UPDATE_BACKUP) {
        // client hasn't gotten a good message through in a long time
        Com_DPrintf("%s: delta request from out-of-date packet.\n", client->name);
        return NULL;
    }

    // we have a valid message to delta from
    frame = &client->frames[client->lastframe & UPDATE_MASK];
    if (frame->number != client->lastframe) {
        // but it got never sent
        Com_DPrintf("%s: delta request from dropped frame.\n", client->name);
        return NULL;
    }

    if (svs.next_entity - frame->first_entity > svs.num_entities) {
        // but entities are too old
        Com_DPrintf("%s: delta request from out-of-date entities.\n", client->name);
        return NULL;
    }

    return frame;
}

/*
==================
SV_WriteFrameToClient_Default
==================
*/
void SV_WriteFrameToClient_Default(client_t *client)
{
    client_frame_t  *frame, *oldframe;
    player_packed_t *oldstate;
    int             lastframe;

    // this is the frame we are creating
    frame = &client->frames[client->framenum & UPDATE_MASK];

    // this is the frame we are delta'ing from
    oldframe = get_last_frame(client);
    if (oldframe) {
        oldstate = &oldframe->ps;
        lastframe = client->lastframe;
    } else {
        oldstate = NULL;
        lastframe = -1;
    }

    MSG_WriteByte(svc_frame);
    MSG_WriteLong(client->framenum);
    MSG_WriteLong(lastframe);   // what we are delta'ing from
    MSG_WriteByte(client->suppress_count);  // rate dropped packets
    client->suppress_count = 0;
    client->frameflags = 0;

    // send over the areabits
    MSG_WriteByte(frame->areabytes);
    MSG_WriteData(frame->areabits, frame->areabytes);

    // delta encode the playerstate
    MSG_WriteByte(svc_playerinfo);
    MSG_WriteDeltaPlayerstate_Default(oldstate, &frame->ps);

    // delta encode the entities
    MSG_WriteByte(svc_packetentities);
    SV_EmitPacketEntities(client, oldframe, frame, 0);
}

/*
==================
SV_WriteFrameToClient_Enhanced
==================
*/
void SV_WriteFrameToClient_Enhanced(client_t *client)
{
    client_frame_t  *frame, *oldframe;
    player_packed_t *oldstate;
    uint32_t        extraflags;
    int             delta, suppressed;
    byte            *b1, *b2;
    msgPsFlags_t    psFlags;
    int             clientEntityNum;

    // this is the frame we are creating
    frame = &client->frames[client->framenum & UPDATE_MASK];

    // this is the frame we are delta'ing from
    oldframe = get_last_frame(client);
    if (oldframe) {
        oldstate = &oldframe->ps;
        delta = client->framenum - client->lastframe;
    } else {
        oldstate = NULL;
        delta = 31;
    }

    // first byte to be patched
    b1 = SZ_GetSpace(&msg_write, 1);

    MSG_WriteLong((client->framenum & FRAMENUM_MASK) | (delta << FRAMENUM_BITS));

    // second byte to be patched
    b2 = SZ_GetSpace(&msg_write, 1);

    // send over the areabits
    MSG_WriteByte(frame->areabytes);
    MSG_WriteData(frame->areabits, frame->areabytes);

    // ignore some parts of playerstate if not recording demo
    psFlags = 0;
    if (!client->settings[CLS_RECORDING]) {
        if (client->settings[CLS_NOGUN]) {
            psFlags |= MSG_PS_IGNORE_GUNFRAMES;
            if (client->settings[CLS_NOGUN] != 2) {
                psFlags |= MSG_PS_IGNORE_GUNINDEX;
            }
        }
        if (client->settings[CLS_NOBLEND]) {
            psFlags |= MSG_PS_IGNORE_BLEND;
        }
        if (frame->ps.pmove.pm_type < PM_DEAD) {
            if (!(frame->ps.pmove.pm_flags & PMF_NO_PREDICTION)) {
                psFlags |= MSG_PS_IGNORE_VIEWANGLES;
            }
        } else {
            // lying dead on a rotating platform?
            psFlags |= MSG_PS_IGNORE_DELTAANGLES;
        }
    }

    clientEntityNum = 0;
    if (client->protocol == PROTOCOL_VERSION_Q2PRO) {
        if (frame->ps.pmove.pm_type < PM_DEAD && !client->settings[CLS_RECORDING]) {
            clientEntityNum = frame->clientNum + 1;
        }
        if (client->settings[CLS_NOPREDICT]) {
            psFlags |= MSG_PS_IGNORE_PREDICTION;
        }
        suppressed = client->frameflags;
    } else {
        suppressed = client->suppress_count;
    }

    // delta encode the playerstate
    extraflags = MSG_WriteDeltaPlayerstate_Enhanced(oldstate, &frame->ps, psFlags);

    if (client->protocol == PROTOCOL_VERSION_Q2PRO) {
        // delta encode the clientNum
        if (client->version < PROTOCOL_VERSION_Q2PRO_CLIENTNUM_FIX) {
            if (!oldframe || frame->clientNum != oldframe->clientNum) {
                extraflags |= EPS_CLIENTNUM;
                MSG_WriteByte(frame->clientNum);
            }
        } else {
            int clientNum = oldframe ? oldframe->clientNum : 0;
            if (clientNum != frame->clientNum) {
                extraflags |= EPS_CLIENTNUM;
                MSG_WriteByte(frame->clientNum);
            }
        }
    }

    // save 3 high bits of extraflags
    *b1 = svc_frame | (((extraflags & 0x70) << 1));

    // save 4 low bits of extraflags
    *b2 = (suppressed & SUPPRESSCOUNT_MASK) |
          ((extraflags & 0x0F) << SUPPRESSCOUNT_BITS);

    client->suppress_count = 0;
    client->frameflags = 0;

    // delta encode the entities
    SV_EmitPacketEntities(client, oldframe, frame, clientEntityNum);
}

/*
=============================================================================

Build a client frame structure

=============================================================================
*/

#if USE_FPS
static void
fix_old_origin(client_t *client, entity_packed_t *state, edict_t *ent, int e)
{
    server_entity_t *sent = &sv.entities[e];
    int i, j, k;

    if (ent->s.renderfx & RF_BEAM)
        return;

    if (!ent->linkcount)
        return; // not linked in anywhere

    if (sent->create_framenum >= sv.framenum) {
        // created this frame. unfortunate for projectiles: they will move only
        // with 1/client->framediv fraction of their normal speed on the client
        return;
    }

    if (state->event == EV_PLAYER_TELEPORT && !Q2PRO_OPTIMIZE(client)) {
        // other clients will lerp from old_origin on EV_PLAYER_TELEPORT...
        VectorCopy(state->origin, state->old_origin);
        return;
    }

    if (sent->create_framenum > sv.framenum - client->framediv) {
        // created between client frames
        VectorScale(sent->create_origin, 8.0f, state->old_origin);
        return;
    }

    // find the oldest valid origin
    for (i = 0; i < client->framediv - 1; i++) {
        j = sv.framenum - (client->framediv - i);
        k = j & ENT_HISTORY_MASK;
        if (sent->history[k].framenum == j) {
            VectorScale(sent->history[k].origin, 8.0f, state->old_origin);
            return;
        }
    }

    // no valid old_origin, just use what game provided
}
#endif

/*
=============
SV_BuildClientFrame

Decides which entities are going to be visible to the client, and
copies off the playerstat and areabits.
=============
*/
void SV_BuildClientFrame(client_t *client)
{
    int         e;
    vec3_t      org;
    edict_t     *ent;
    edict_t     *clent;
    client_frame_t  *frame;
    entity_packed_t *state;
    player_state_t  *ps;
    int         l;
    int         clientarea, clientcluster;
    mleaf_t     *leaf;
    byte        clientphs[VIS_MAX_BYTES];
    byte        clientpvs[VIS_MAX_BYTES];
    int cull_nonvisible_entities = Cvar_Get("sv_cull_nonvisible_entities", "1", CVAR_CHEAT)->integer;

    clent = client->edict;
    if (!clent->client)
        return;        // not in game yet

    // this is the frame we are creating
    frame = &client->frames[client->framenum & UPDATE_MASK];
    frame->number = client->framenum;
    frame->sentTime = com_eventTime; // save it for ping calc later
    frame->latency = -1; // not yet acked

    client->frames_sent++;

    // find the client's PVS
    ps = &clent->client->ps;
    VectorMA(ps->viewoffset, 0.125f, ps->pmove.origin, org);

    leaf = CM_PointLeaf(client->cm, org);
    clientarea = CM_LeafArea(leaf);
    clientcluster = CM_LeafCluster(leaf);

    // calculate the visible areas
    frame->areabytes = CM_WriteAreaBits(client->cm, frame->areabits, clientarea);
    if (!frame->areabytes && client->protocol != PROTOCOL_VERSION_Q2PRO) {
        frame->areabits[0] = 255;
        frame->areabytes = 1;
    }

    // grab the current player_state_t
    MSG_PackPlayer(&frame->ps, ps);

    // grab the current clientNum
    if (g_features->integer & GMF_CLIENTNUM) {
        frame->clientNum = clent->client->clientNum;
    } else {
        frame->clientNum = client->number;
    }

    CM_FatPVS(client->cm, clientpvs, org);
    BSP_ClusterVis(client->cm->cache, clientphs, clientcluster, DVIS_PHS);

    // build up the list of visible entities
    frame->num_entities = 0;
    frame->first_entity = svs.next_entity;

    for (e = 1; e < client->pool->num_edicts; e++) {
        ent = EDICT_POOL(client, e);

        // ignore entities not in use
        if (!ent->inuse && (g_features->integer & GMF_PROPERINUSE)) {
            continue;
        }

        // ignore ents without visible models
        if (ent->svflags & SVF_NOCLIENT)
            continue;

        // ignore ents without visible models unless they have an effect
        if (!ent->s.modelindex && !ent->s.effects && !ent->s.sound) {
            if (!ent->s.event) {
                continue;
            }
            if (ent->s.event == EV_FOOTSTEP && client->settings[CLS_NOFOOTSTEPS]) {
                continue;
            }
        }

        if ((ent->s.effects & EF_GIB) && client->settings[CLS_NOGIBS]) {
            continue;
        }

        // ignore if not touching a PV leaf
        if (ent != clent && !sv_novis->integer) {
            // check area
            if (!CM_AreasConnected(client->cm, clientarea, ent->areanum)) {
                // doors can legally straddle two areas, so
                // we may need to check another one
                if (!CM_AreasConnected(client->cm, clientarea, ent->areanum2)) {
                    continue;        // blocked by a door
                }
            }

            // beams just check one point for PHS
            if (ent->s.renderfx & RF_BEAM) {
                l = ent->clusternums[0];
                if (!Q_IsBitSet(clientphs, l))
                    continue;
            } else {
                if (cull_nonvisible_entities && !SV_EdictIsVisible(client->cm, ent, clientpvs)) {
                    continue;
                }

                if (!ent->s.modelindex) {
                    // don't send sounds if they will be attenuated away
                    vec3_t    delta;
                    float    len;

                    VectorSubtract(org, ent->s.origin, delta);
                    len = VectorLength(delta);
                    if (len > 400)
                        continue;
                }
            }
        }

        if (ent->s.number != e) {
            Com_WPrintf("%s: fixing ent->s.number: %d to %d\n",
                        __func__, ent->s.number, e);
            ent->s.number = e;
        }

        // add it to the circular client_entities array
        state = &svs.entities[svs.next_entity % svs.num_entities];
        MSG_PackEntity(state, &ent->s, Q2PRO_SHORTANGLES(client, e));

#if USE_FPS
        // fix old entity origins for clients not running at
        // full server frame rate
        if (client->framediv != 1)
            fix_old_origin(client, state, ent, e);
#endif

        // clear footsteps
        if (state->event == EV_FOOTSTEP && client->settings[CLS_NOFOOTSTEPS]) {
            state->event = 0;
        }

        // hide POV entity from renderer, unless this is player's own entity
        if (e == frame->clientNum + 1 && ent != clent &&
            (g_features->integer & GMF_CLIENTNUM) && !Q2PRO_OPTIMIZE(client)) {
            state->modelindex = 0;
        }

#if USE_MVD_CLIENT
        if (sv.state == ss_broadcast) {
            // spectators only need to know about inline BSP models
            if (state->solid != PACKED_BSP)
                state->solid = 0;
        } else
#endif
        if (ent->owner == clent) {
            // don't mark players missiles as solid
            state->solid = 0;
        } else if (client->esFlags & MSG_ES_LONGSOLID) {
            state->solid = sv.entities[e].solid32;
        }

        svs.next_entity++;

        if (++frame->num_entities == MAX_PACKET_ENTITIES) {
            break;
        }
    }
}

