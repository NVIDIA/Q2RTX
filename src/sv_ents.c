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

#include "sv_local.h"

/*
=============================================================================

Encode a client frame onto the network channel

=============================================================================
*/

/*
=============
SV_EmitPacketEntities

Writes a delta update of an entity_state_t list to the message.
=============
*/
static void SV_EmitPacketEntities( client_t         *client,
                                   client_frame_t   *from,
                                   client_frame_t   *to,
                                   int              clientEntityNum )
{
    entity_state_t *newent;
    const entity_state_t *oldent, *base;
    unsigned i, oldindex, newindex, from_num_entities;
    int oldnum, newnum;
    msgEsFlags_t flags;

    if( !from )
        from_num_entities = 0;
    else
        from_num_entities = from->num_entities;

    newindex = 0;
    oldindex = 0;
    oldent = newent = NULL;
    while( newindex < to->num_entities || oldindex < from_num_entities ) {
        if( newindex >= to->num_entities ) {
            newnum = 9999;
        } else {
            i = ( to->first_entity + newindex ) % svs.num_entities;
            newent = &svs.entities[i];
            newnum = newent->number;
        }

        if( oldindex >= from_num_entities ) {
            oldnum = 9999;
        } else {
            i = ( from->first_entity + oldindex ) % svs.num_entities;
            oldent = &svs.entities[i];
            oldnum = oldent->number;
        }

        if( newnum == oldnum ) {
            // delta update from old position
            // because the force parm is false, this will not result
            // in any bytes being emited if the entity has not changed at all
            // note that players are always 'newentities', this updates their
            // oldorigin always and prevents warping
            flags = client->esFlags;
            if( client->protocol < PROTOCOL_VERSION_Q2PRO ) {
                // don't waste bandwidth for Q2PRO clients
                if( newent->number <= client->maxclients ) {
                    flags |= MSG_ES_NEWENTITY;
                }
            }
            if( newent->number == clientEntityNum ) {
                flags |= MSG_ES_FIRSTPERSON;
                VectorCopy( oldent->origin, newent->origin );
                VectorCopy( oldent->angles, newent->angles );
            }
            MSG_WriteDeltaEntity( oldent, newent, flags );
            oldindex++;
            newindex++;
            continue;
        }

        if( newnum < oldnum ) {    
            // this is a new entity, send it from the baseline
            flags = client->esFlags|MSG_ES_FORCE|MSG_ES_NEWENTITY;
            base = client->baselines[newnum >> SV_BASELINES_SHIFT];
            if( base ) {
                base += ( newnum & SV_BASELINES_MASK );
            } else {
                base = &nullEntityState;
            }
            if( newent->number == clientEntityNum ) {
                flags |= MSG_ES_FIRSTPERSON;
                VectorCopy( base->origin, newent->origin );
                VectorCopy( base->angles, newent->angles );
                VectorCopy( base->old_origin, newent->old_origin );
            }
            MSG_WriteDeltaEntity( base, newent, flags );
            newindex++;
            continue;
        }

        if( newnum > oldnum ) {
            // the old entity isn't present in the new message
            MSG_WriteDeltaEntity( oldent, NULL, MSG_ES_FORCE );
            oldindex++;
            continue;
        }
    }

    MSG_WriteShort( 0 );    // end of packetentities
}

static client_frame_t *get_last_frame( client_t *client ) {
    client_frame_t *frame;

    if( client->lastframe <= 0 ) {
        // client is asking for a retransmit
        client->frames_nodelta++;
        return NULL;
    }

    client->frames_nodelta = 0;

    if( sv.framenum - client->lastframe > UPDATE_BACKUP - 1 ) {
        // client hasn't gotten a good message through in a long time
        Com_DPrintf( "%s: delta request from out-of-date packet.\n", client->name );
        return NULL;
    }

    // we have a valid message to delta from
    frame = &client->frames[client->lastframe & UPDATE_MASK];
    if( svs.next_entity - frame->first_entity > svs.num_entities ) {
        // but entities are too old
        Com_DPrintf( "%s: delta request from out-of-date entities.\n", client->name );
        return NULL;
    }

    return frame;
}

/*
==================
SV_WriteFrameToClient_Default
==================
*/
void SV_WriteFrameToClient_Default( client_t *client ) {
    client_frame_t      *frame, *oldframe;
    player_state_t      *oldstate;
    int                 lastframe;

    // this is the frame we are creating
    frame = &client->frames[sv.framenum & UPDATE_MASK];

    // this is the frame we are delta'ing from
    oldframe = get_last_frame( client );
    if( oldframe ) {
        oldstate = &oldframe->ps;
        lastframe = client->lastframe;
    } else {
        oldstate = NULL;
        lastframe = -1;
    }

    MSG_WriteByte( svc_frame );
    MSG_WriteLong( sv.framenum );
    MSG_WriteLong( lastframe );    // what we are delta'ing from
    MSG_WriteByte( client->surpressCount );    // rate dropped packets
    client->surpressCount = 0;
    client->frameflags = 0;

    // send over the areabits
    MSG_WriteByte( frame->areabytes );
    MSG_WriteData( frame->areabits, frame->areabytes );

    // delta encode the playerstate
    MSG_WriteByte( svc_playerinfo );
    MSG_WriteDeltaPlayerstate_Default( oldstate, &frame->ps );
    
    // delta encode the entities
    MSG_WriteByte( svc_packetentities );
    SV_EmitPacketEntities( client, oldframe, frame, 0 );
}

/*
==================
SV_WriteFrameToClient_Enhanced
==================
*/
void SV_WriteFrameToClient_Enhanced( client_t *client ) {
    client_frame_t  *frame, *oldframe;
    player_state_t  *oldstate;
    uint32_t        extraflags;
    int             delta, surpressed;
    byte            *b1, *b2;
    msgPsFlags_t    psFlags;
    int             clientEntityNum;

    // this is the frame we are creating
    frame = &client->frames[sv.framenum & UPDATE_MASK];

    // this is the frame we are delta'ing from
    oldframe = get_last_frame( client );
    if( oldframe ) {
        oldstate = &oldframe->ps;
        delta = sv.framenum - client->lastframe;
    } else {
        oldstate = NULL;
        delta = 31;
    }

    // first byte to be patched
    b1 = SZ_GetSpace( &msg_write, 1 );

    MSG_WriteLong( ( sv.framenum & FRAMENUM_MASK ) | ( delta << FRAMENUM_BITS ) );

    // second byte to be patched
    b2 = SZ_GetSpace( &msg_write, 1 );

    // send over the areabits
    MSG_WriteByte( frame->areabytes );
    MSG_WriteData( frame->areabits, frame->areabytes );

    // ignore some parts of playerstate if not recording demo
    psFlags = 0;
    if( !client->settings[CLS_RECORDING] ) {
        if( client->settings[CLS_NOGUN] ) {
            psFlags |= MSG_PS_IGNORE_GUNFRAMES;
            if( client->settings[CLS_NOGUN] != 2 ) {
                psFlags |= MSG_PS_IGNORE_GUNINDEX;
            }
        }
        if( client->settings[CLS_NOBLEND] ) {
            psFlags |= MSG_PS_IGNORE_BLEND;
        }
        if( frame->ps.pmove.pm_type < PM_DEAD ) {
            if( !( frame->ps.pmove.pm_flags & PMF_NO_PREDICTION ) ) {
                psFlags |= MSG_PS_IGNORE_VIEWANGLES;
            }
        } else {
            // lying dead on a rotating platform?
            psFlags |= MSG_PS_IGNORE_DELTAANGLES;
        }
    }

    clientEntityNum = 0;
    if( client->protocol == PROTOCOL_VERSION_Q2PRO ) {
        if( frame->ps.pmove.pm_type < PM_DEAD ) {
            clientEntityNum = frame->clientNum + 1;
        }
        if( client->settings[CLS_NOPREDICT] ) {
            psFlags |= MSG_PS_IGNORE_PREDICTION;
        }
        surpressed = client->frameflags;
    } else {
        surpressed = client->surpressCount;
    }

    // delta encode the playerstate
    extraflags = MSG_WriteDeltaPlayerstate_Enhanced( oldstate, &frame->ps, psFlags );

    if( client->protocol == PROTOCOL_VERSION_Q2PRO ) {
        // delta encode the clientNum
        if( client->version < PROTOCOL_VERSION_Q2PRO_CLIENTNUM_FIX ) {
            if( !oldframe || frame->clientNum != oldframe->clientNum ) {
                extraflags |= EPS_CLIENTNUM;
                MSG_WriteByte( frame->clientNum );
            }
        } else {
            int clientNum = oldframe ? oldframe->clientNum : 0;
            if( clientNum != frame->clientNum ) {
                extraflags |= EPS_CLIENTNUM;
                MSG_WriteByte( frame->clientNum );
            }
        }
    }

    // save 3 high bits of extraflags
    *b1 = svc_frame | ( ( ( extraflags & 0x70 ) << 1 ) );

    // save 4 low bits of extraflags
    *b2 = ( surpressed & SURPRESSCOUNT_MASK ) |
        ( ( extraflags & 0x0F ) << SURPRESSCOUNT_BITS );

    client->surpressCount = 0;
    client->frameflags = 0;

    // delta encode the entities
    SV_EmitPacketEntities( client, oldframe, frame, clientEntityNum );
}

/*
=============================================================================

Build a client frame structure

=============================================================================
*/

qboolean SV_EdictPV( cm_t *cm, edict_t *ent, byte *mask ) {
    mnode_t *node;
    int i, l;

    if( ent->num_clusters == -1 ) {    
        // too many leafs for individual check, go by headnode
        node = CM_NodeNum( cm, ent->headnode );
        return CM_HeadnodeVisible( node, mask );
    }

    // check individual leafs
    for( i = 0; i < ent->num_clusters; i++ ) {
        l = ent->clusternums[i];
        if( Q_IsBitSet( mask, l ) ) {
            return qtrue;
        }
    }

    return qfalse;        // not visible
}

/*
=============
SV_BuildClientFrame

Decides which entities are going to be visible to the client, and
copies off the playerstat and areabits.
=============
*/
void SV_BuildClientFrame( client_t *client ) {
    int         e;
    vec3_t      org;
    edict_t     *ent;
    edict_t     *clent;
    client_frame_t  *frame;
    entity_state_t  *state;
    player_state_t  *ps;
    int         l;
    int         clientarea, clientcluster;
    mleaf_t     *leaf;
    byte        clientphs[VIS_MAX_BYTES];
    byte        clientpvs[VIS_MAX_BYTES];

    clent = client->edict;
    if( !clent->client )
        return;        // not in game yet

    // this is the frame we are creating
    frame = &client->frames[sv.framenum & UPDATE_MASK];
    client->frame_latency[sv.framenum & LATENCY_MASK] = -1;
    client->frames_sent++;

    frame->sentTime = com_eventTime; // save it for ping calc later

    // find the client's PVS
    ps = &clent->client->ps;
    VectorMA( ps->viewoffset, 0.125f, ps->pmove.origin, org );

    leaf = CM_PointLeaf( client->cm, org );
    clientarea = CM_LeafArea( leaf );
    clientcluster = CM_LeafCluster( leaf );

    // calculate the visible areas
    frame->areabytes = CM_WriteAreaBits( client->cm, frame->areabits, clientarea );
    if( !frame->areabytes && client->protocol != PROTOCOL_VERSION_Q2PRO ) {
        frame->areabits[0] = 255;
        frame->areabytes = 1;
    }

    // grab the current player_state_t
    frame->ps = *ps;

    // grab the current clientNum
    if( g_features->integer & GMF_CLIENTNUM ) {
        frame->clientNum = clent->client->clientNum;
    } else {
        frame->clientNum = client->number;
    }

    CM_FatPVS( client->cm, clientpvs, org );
    BSP_ClusterVis( client->cm->cache, clientphs, clientcluster, DVIS_PHS );

    // build up the list of visible entities
    frame->num_entities = 0;
    frame->first_entity = svs.next_entity;

    for( e = 1; e < client->pool->num_edicts; e++ ) {
        ent = EDICT_POOL( client, e );

        // ignore entities not in use
        if( ( g_features->integer & GMF_PROPERINUSE ) && !ent->inuse ) {
            continue;
        }

        // ignore ents without visible models
        if( ent->svflags & SVF_NOCLIENT )
            continue;

        // ignore ents without visible models unless they have an effect
        if( !ent->s.modelindex && !ent->s.effects && !ent->s.sound ) {
            if( !ent->s.event ) {
                continue;
            }
            if( ent->s.event == EV_FOOTSTEP && client->settings[CLS_NOFOOTSTEPS] ) {
                continue;
            }
        }

        if( ( ent->s.effects & EF_GIB ) && client->settings[CLS_NOGIBS] ) {
            continue;
        }

        // ignore if not touching a PV leaf
        if( ent != clent && !sv_novis->integer ) {
            // check area
            if( !CM_AreasConnected( client->cm, clientarea, ent->areanum ) ) {    
                // doors can legally straddle two areas, so
                // we may need to check another one
                if( !CM_AreasConnected( client->cm, clientarea, ent->areanum2 ) ) {
                    continue;        // blocked by a door
                }
            }

            // beams just check one point for PHS
            if( ent->s.renderfx & RF_BEAM ) {
                l = ent->clusternums[0];
                if( !Q_IsBitSet( clientphs, l ) )
                    continue;
            } else {
                if( !SV_EdictPV( client->cm, ent, clientpvs ) ) {
                    continue;
                }

                if( !ent->s.modelindex ) {    
                    // don't send sounds if they will be attenuated away
                    vec3_t    delta;
                    float    len;

                    VectorSubtract( org, ent->s.origin, delta );
                    len = VectorLength( delta );
                    if( len > 400 )
                        continue;
                }
            }
        }

        if( ent->s.number != e ) {
            Com_WPrintf( "%s: fixing ent->s.number: %d to %d\n",
                __func__, ent->s.number, e );
            ent->s.number = e;
        }

        // add it to the circular client_entities array
        state = &svs.entities[svs.next_entity % svs.num_entities];
        *state = ent->s;

        // clear footsteps
        if( ent->s.event == EV_FOOTSTEP && client->settings[CLS_NOFOOTSTEPS] ) {
            state->event = 0;
        }

        // XXX: hide this enitity from renderer
        if( ( client->protocol != PROTOCOL_VERSION_Q2PRO ||
              client->settings[CLS_RECORDING] ) &&
            ( g_features->integer & GMF_CLIENTNUM ) &&
            e == frame->clientNum + 1 && ent != clent )
        {
            state->modelindex = 0;
        }

        // don't mark players missiles as solid
        if( ent->owner == client->edict ) {
            state->solid = 0;
        } else if( client->esFlags & MSG_ES_LONGSOLID ) {
            state->solid = sv.entities[e].solid32;
        }

        svs.next_entity++;

        if( ++frame->num_entities == MAX_PACKET_ENTITIES ) {
            break;
        }
    }
}

