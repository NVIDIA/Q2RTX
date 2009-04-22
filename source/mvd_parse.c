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
// mvd_parse.c
//

#include "mvd_local.h"

#if USE_CLIENT

#define MVD_ShowSVC( cmd ) \
    Com_Printf( "%3"PRIz":%s\n", msg_read.readcount - 1, \
        MVD_ServerCommandString( cmd ) )

static const char *MVD_ServerCommandString( int cmd ) {
    switch( cmd ) {
        case -1: return "END OF MESSAGE";
        default: return "UNKNOWN COMMAND";
#define M(x) \
        case mvd_##x: return "mvd_" #x;
        M(bad)
        M(nop)
        M(serverdata)
        M(configstring)
        M(frame)
        M(unicast)
        M(unicast_r)
        M(multicast_all)
        M(multicast_pvs)
        M(multicast_phs)
        M(multicast_all_r)
        M(multicast_pvs_r)
        M(multicast_phs_r)
        M(sound)
        M(print)
    }
}

#endif // USE_CLIENT

static void MVD_LinkEdict( mvd_t *mvd, edict_t *ent ) {
    int         index;
    mmodel_t    *cm;
    int         x, zd, zu;
    bsp_t       *cache = mvd->cm.cache;
    
    if( !cache ) {
        return;
    }
    
    if( ent->s.solid == 31 ) {
        index = ent->s.modelindex;
        if( index < 1 || index > cache->nummodels ) {
            Com_WPrintf( "%s: entity %d: bad inline model index: %d\n",
                __func__, ent->s.number, index );
            return;
        }
        cm = &cache->models[ index - 1 ];
        VectorCopy( cm->mins, ent->mins );
        VectorCopy( cm->maxs, ent->maxs );
        ent->solid = SOLID_BSP;
    } else if( ent->s.solid ) {
        x = 8 * ( ent->s.solid & 31 );
        zd = 8 * ( ( ent->s.solid >> 5 ) & 31 );
        zu = 8 * ( ( ent->s.solid >> 10 ) & 63 ) - 32;

        ent->mins[0] = ent->mins[1] = -x;
        ent->maxs[0] = ent->maxs[1] = x;
        ent->mins[2] = -zd;
        ent->maxs[2] = zu;
        ent->solid = SOLID_BBOX;
    } else {
        VectorClear( ent->mins );
        VectorClear( ent->maxs );
        ent->solid = SOLID_NOT;
    }

    SV_LinkEdict( &mvd->cm, ent );
}

void MVD_ParseEntityString( mvd_t *mvd, const char *data ) {
    const char *p;
    char key[MAX_STRING_CHARS];
    char value[MAX_STRING_CHARS];
    char classname[MAX_QPATH];
    vec3_t origin;
    vec3_t angles;
    
    while( data ) {
        p = COM_Parse( &data );
        if( !p[0] ) {
            break;
        }
        if( p[0] != '{' ) {
            Com_Error( ERR_DROP, "expected '{', found '%s'", p );
        }
        
        classname[0] = 0;
        VectorClear( origin );
        VectorClear( angles );
        while( 1 ) {
            p = COM_Parse( &data );
            if( p[0] == '}' ) {
                break;
            }
            if( p[0] == '{' ) {
                Com_Error( ERR_DROP, "expected key, found '{'" );
            }

            Q_strlcpy( key, p, sizeof( key ) );
            
            p = COM_Parse( &data );
            if( !data ) {
                Com_Error( ERR_DROP, "expected key/value pair, found EOF" );
            }
            if( p[0] == '}' || p[0] == '{' ) {
                Com_Error( ERR_DROP, "expected value, found '%s'", p );
            }

            if( !strcmp( key, "classname" ) ) {
                Q_strlcpy( classname, p, sizeof( classname ) );
                continue;
            }

            Q_strlcpy( value, p, sizeof( value ) );

            p = value;
            if( !strcmp( key, "origin" ) ) {
                origin[0] = atof( COM_Parse( &p ) );
                origin[1] = atof( COM_Parse( &p ) );
                origin[2] = atof( COM_Parse( &p ) );
            } else if( !strncmp( key, "angle", 5 ) ) {
                if( key[5] == 0 ) {
                    angles[0] = 0;
                    angles[1] = atof( COM_Parse( &p ) );
                    angles[2] = 0;
                } else if( key[5] == 's' && key[6] == 0 ) {
                    angles[0] = atof( COM_Parse( &p ) );
                    angles[1] = atof( COM_Parse( &p ) );
                    angles[2] = atof( COM_Parse( &p ) );
                }
            }
        }

        if( !classname[0] ) {
            Com_Error( ERR_DROP, "entity with no classname" );
        }

        if( strncmp( classname, "info_player_", 12 ) ) {
            continue;
        }

        if( !strcmp( classname + 12, "intermission" ) ) {
            VectorCopy( origin, mvd->spawnOrigin );
            VectorCopy( angles, mvd->spawnAngles );
            break;
        }
        
        if( !strcmp( classname + 12, "start" ) ||
            !strcmp( classname + 12, "deathmatch" ) )
        {
            VectorCopy( origin, mvd->spawnOrigin );
            VectorCopy( angles, mvd->spawnAngles );
        }

    }
}

static void MVD_ParseMulticast( mvd_t *mvd, mvd_ops_t op, int extrabits ) {
    mvd_client_t    *client;
    client_t    *cl;
    byte        mask[MAX_MAP_VIS];
    mleaf_t     *leaf1, *leaf2;
    vec3_t      org;
    qboolean    reliable = qfalse;
    player_state_t    *ps;
    byte        *data;
    int         length, leafnum;

    length = MSG_ReadByte();
    length |= extrabits << 8;

    switch( op ) {
    case mvd_multicast_all_r:
        reliable = qtrue;
        // intentional fallthrough
    case mvd_multicast_all:
        leaf1 = NULL;
        break;
    case mvd_multicast_phs_r:
        reliable = qtrue;
        // intentional fallthrough
    case mvd_multicast_phs:
        leafnum = MSG_ReadShort();
        leaf1 = CM_LeafNum( &mvd->cm, leafnum );
        BSP_ClusterVis( mvd->cm.cache, mask, leaf1->cluster, DVIS_PHS );
        break;
    case mvd_multicast_pvs_r:
        reliable = qtrue;
        // intentional fallthrough
    case mvd_multicast_pvs:
        leafnum = MSG_ReadShort();
        leaf1 = CM_LeafNum( &mvd->cm, leafnum );
        BSP_ClusterVis( mvd->cm.cache, mask, leaf1->cluster, DVIS_PVS );
        break;
    default:
        MVD_Destroyf( mvd, "bad op" );
    }

    // skip data payload
    data = msg_read.data + msg_read.readcount;
    msg_read.readcount += length;
    if( msg_read.readcount > msg_read.cursize ) {
        MVD_Destroyf( mvd, "read past end of message" );
    }

    // send the data to all relevent clients
    LIST_FOR_EACH( mvd_client_t, client, &mvd->clients, entry ) {
        cl = client->cl;
        if( cl->state < cs_primed ) {
            continue;
        }

        // do not send unreliables to connecting clients
        if( !reliable && ( cl->state != cs_spawned ||
            cl->download || ( cl->flags & CF_NODATA ) ) )
        {
            continue;
        }

        if( leaf1 ) {
            // find the client's PVS
            ps = &client->ps;
#if 0
            VectorMA( ps->viewoffset, 0.125f, ps->pmove.origin, org );
#else
            // FIXME: for some strange reason, game code assumes the server
            // uses entity origin for PVS/PHS culling, not the view origin
            VectorScale( ps->pmove.origin, 0.125f, org );
#endif
            leaf2 = CM_PointLeaf( &mvd->cm, org );
            if( !CM_AreasConnected( &mvd->cm, leaf1->area, leaf2->area ) )
                continue;
            if( !Q_IsBitSet( mask, leaf2->cluster ) ) {
                continue;
            }
        }

        cl->AddMessage( cl, data, length, reliable );
    }
}

static void MVD_UnicastSend( mvd_t *mvd, qboolean reliable, byte *data, size_t length, mvd_player_t *player ) {
    mvd_player_t *target;
    mvd_client_t *client;
    client_t *cl;
    
    // send to all relevant clients
    LIST_FOR_EACH( mvd_client_t, client, &mvd->clients, entry ) {
        cl = client->cl;
        if( cl->state < cs_spawned ) {
            continue;
        }
        target = client->target ? client->target : mvd->dummy;
        if( target == player ) {
            cl->AddMessage( cl, data, length, reliable );
        }
    }
}

static void MVD_UnicastLayout( mvd_t *mvd, qboolean reliable, mvd_player_t *player ) {
    mvd_client_t *client;

    if( player != mvd->dummy ) {
        MSG_ReadString( NULL, 0 );
        return; // we don't care about others
    }

    MSG_ReadString( mvd->layout, sizeof( mvd->layout ) );

    // force an update to all relevant clients
    LIST_FOR_EACH( mvd_client_t, client, &mvd->clients, entry ) {
        if( client->cl->state < cs_spawned ) {
            continue;
        }
        if( client->layout_type == LAYOUT_SCORES ) {
            client->layout_time = 0;
        }
    }
}

static void MVD_UnicastString( mvd_t *mvd, qboolean reliable, mvd_player_t *player ) {
    int index;
    char string[MAX_QPATH];
    mvd_cs_t *cs;
    byte *data;
    size_t readcount, length;

    data = msg_read.data + msg_read.readcount - 1;
    readcount = msg_read.readcount - 1;

    index = MSG_ReadShort();
    length = MSG_ReadString( string, sizeof( string ) );

    if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
        MVD_Destroyf( mvd, "%s: bad index: %d", __func__, index );
    }
    if( index < CS_GENERAL ) {
        Com_DPrintf( "%s: common configstring: %d\n", __func__, index );
        return;
    }
    if( length >= sizeof( string ) ) {
        Com_DPrintf( "%s: oversize configstring: %d\n", __func__, index );
        return;
    }

    for( cs = player->configstrings; cs; cs = cs->next ) {
        if( cs->index == index ) {
            break;
        }
    }
    if( !cs ) {
        cs = MVD_Malloc( sizeof( *cs ) + MAX_QPATH - 1 );
        cs->index = index;
        cs->next = player->configstrings;
        player->configstrings = cs;
    }

    memcpy( cs->string, string, length + 1 );

    length = msg_read.readcount - readcount;
    MVD_UnicastSend( mvd, reliable, data, length, player );
}

static void MVD_UnicastPrint( mvd_t *mvd, qboolean reliable, mvd_player_t *player ) {
    int level;
    byte *data;
    size_t readcount, length;
    mvd_client_t *client;
    client_t *cl;
    mvd_player_t *target;

    data = msg_read.data + msg_read.readcount - 1;
    readcount = msg_read.readcount - 1;

    level = MSG_ReadByte();
    MSG_ReadString( NULL, 0 );

    length = msg_read.readcount - readcount;
    
    // send to all relevant clients
    LIST_FOR_EACH( mvd_client_t, client, &mvd->clients, entry ) {
        cl = client->cl;
        if( cl->state < cs_spawned ) {
            continue;
        }
        if( level < cl->messagelevel ) {
            continue;
        }
        if( level == PRINT_CHAT && ( client->uf & UF_MUTE_PLAYERS ) ) {
            continue;
        }
        // decide if message should be routed or not
        target = ( mvd->flags & MVF_NOMSGS ) ? mvd->dummy :
            client->target ? client->target : mvd->dummy;
        if( target == player ) {
            cl->AddMessage( cl, data, length, reliable );
        }
    }
}

static void MVD_UnicastStuff( mvd_t *mvd, qboolean reliable, mvd_player_t *player ) {
    char string[8];
    byte *data;
    size_t readcount, length;

    data = msg_read.data + msg_read.readcount - 1;
    readcount = msg_read.readcount - 1;

    MSG_ReadString( string, sizeof( string ) );
    if( strncmp( string, "play ", 5 ) ) {
        return;
    }

    length = msg_read.readcount - readcount;
    MVD_UnicastSend( mvd, reliable, data, length, player );
}

/*
MVD_ParseUnicast

Attempt to parse the datagram and find custom configstrings,
layouts, etc. Give up as soon as unknown command byte is encountered.
*/
static void MVD_ParseUnicast( mvd_t *mvd, mvd_ops_t op, int extrabits ) {
    int clientNum;
    size_t length, last;
    mvd_player_t *player;
    byte *data;
    qboolean reliable;
    int cmd;

    length = MSG_ReadByte();
    length |= extrabits << 8;
    clientNum = MSG_ReadByte();

    if( clientNum < 0 || clientNum >= mvd->maxclients ) {
        MVD_Destroyf( mvd, "%s: bad number: %d", __func__, clientNum );
    }

    last = msg_read.readcount + length;
    if( last > msg_read.cursize ) {
        MVD_Destroyf( mvd, "%s: read past end of message", __func__ );
    }

    player = &mvd->players[clientNum];

    reliable = op == mvd_unicast_r ? qtrue : qfalse;

    while( msg_read.readcount < last ) {
        cmd = MSG_ReadByte();
#if USE_CLIENT
        if( mvd_shownet->integer > 1 ) {
            MSG_ShowSVC( cmd );
        }
#endif
        switch( cmd ) {
        case svc_layout:
            MVD_UnicastLayout( mvd, reliable, player );
            break;
        case svc_configstring:
            MVD_UnicastString( mvd, reliable, player );
            break;
        case svc_print:
            MVD_UnicastPrint( mvd, reliable, player );
            break;
        case svc_stufftext:
            MVD_UnicastStuff( mvd, reliable, player );
            break;
        default:
#if USE_CLIENT
            if( mvd_shownet->integer > 1 ) {
                Com_Printf( "%"PRIz":SKIPPING UNICAST\n", msg_read.readcount - 1 );
            }
#endif
            // send remaining data and return
            data = msg_read.data + msg_read.readcount - 1;
            length = last - msg_read.readcount + 1;
            MVD_UnicastSend( mvd, reliable, data, length, player );
            msg_read.readcount = last;
            return;
        }
    }

#if USE_CLIENT
    if( mvd_shownet->integer > 1 ) {
        Com_Printf( "%"PRIz":END OF UNICAST\n", msg_read.readcount - 1 );
    }
#endif

    if( msg_read.readcount > last ) {
        MVD_Destroyf( mvd, "%s: read past end of unicast", __func__ );
    }
}

/*
MVD_ParseSound

Entity positioned sounds need special handling since origins need to be
explicitly specified for entities out of client PVS, and not all clients
are able to postition sounds on BSP models properly.

FIXME: this duplicates code in sv_game.c
*/
static void MVD_ParseSound( mvd_t *mvd, int extrabits ) {
    int         flags, index;
    int         volume, attenuation, offset, sendchan;
    int         entnum;
    vec3_t      origin;
    mvd_client_t        *client;
    client_t    *cl;
    byte        mask[MAX_MAP_VIS];
    mleaf_t     *leaf;
    int         area;
    player_state_t      *ps;
    message_packet_t    *msg;
    edict_t     *entity;
    int         i;

    flags = MSG_ReadByte();
    index = MSG_ReadByte();

    volume = attenuation = offset = 0;
    if( flags & SND_VOLUME )
        volume = MSG_ReadByte();
    if( flags & SND_ATTENUATION )
        attenuation = MSG_ReadByte();
    if( flags & SND_OFFSET )
        offset = MSG_ReadByte();

    // entity relative
    sendchan = MSG_ReadShort();
    entnum = sendchan >> 3;
    if( entnum < 0 || entnum >= MAX_EDICTS ) {
        MVD_Destroyf( mvd, "%s: bad entnum: %d", __func__, entnum );
    }

    entity = &mvd->edicts[entnum];
    if( !entity->inuse ) {
        Com_DPrintf( "%s: entnum not in use: %d\n", __func__, entnum );
        return;
    }

    LIST_FOR_EACH( mvd_client_t, client, &mvd->clients, entry ) {
        cl = client->cl;

        // do not send unreliables to connecting clients
        if( cl->state != cs_spawned || cl->download || ( cl->flags & CF_NODATA ) ) {
            continue;
        }

        // PHS cull this sound
        if( !( extrabits & 1 ) ) {
            // get client viewpos
            ps = &client->ps;
            VectorMA( ps->viewoffset, 0.125f, ps->pmove.origin, origin );
            leaf = CM_PointLeaf( &mvd->cm, origin );
            area = CM_LeafArea( leaf );
            if( !CM_AreasConnected( &mvd->cm, area, entity->areanum ) ) {
                // doors can legally straddle two areas, so
                // we may need to check another one
                if( !entity->areanum2 || !CM_AreasConnected( &mvd->cm, area, entity->areanum2 ) ) {
                    continue;        // blocked by a door
                }
            }
            BSP_ClusterVis( mvd->cm.cache, mask, leaf->cluster, DVIS_PHS );
            if( !SV_EdictPV( &mvd->cm, entity, mask ) ) {
                continue; // not in PHS
            }
        }

        // use the entity origin unless it is a bmodel
        if( entity->solid == SOLID_BSP ) {
            VectorAvg( entity->mins, entity->maxs, origin );
            VectorAdd( entity->s.origin, origin, origin );
        } else {
            VectorCopy( entity->s.origin, origin );
        }

        // reliable sounds will always have position explicitly set,
        // as no one gurantees reliables to be delivered in time
        if( extrabits & 2 ) {
            MSG_WriteByte( svc_sound );
            MSG_WriteByte( flags | SND_POS );
            MSG_WriteByte( index );

            if( flags & SND_VOLUME )
                MSG_WriteByte( volume );
            if( flags & SND_ATTENUATION )
                MSG_WriteByte( attenuation );
            if( flags & SND_OFFSET )
                MSG_WriteByte( offset );

            MSG_WriteShort( sendchan );
            MSG_WritePos( origin );

            SV_ClientAddMessage( cl, MSG_RELIABLE|MSG_CLEAR );
            continue;
        }

        if( LIST_EMPTY( &cl->msg_free_list ) ) {
            Com_WPrintf( "%s: %s: out of message slots\n",
                __func__, cl->name );
            continue;
        }

        // default client doesn't know that bmodels have weird origins
        if( entity->solid == SOLID_BSP && cl->protocol == PROTOCOL_VERSION_DEFAULT ) {
            flags |= SND_POS;
        }

        msg = LIST_FIRST( message_packet_t, &cl->msg_free_list, entry );

        msg->cursize = 0;
        msg->flags = flags;
        msg->index = index;
        msg->volume = volume;
        msg->attenuation = attenuation;
        msg->timeofs = offset;
        msg->sendchan = sendchan;
        for( i = 0; i < 3; i++ ) {
            msg->pos[i] = origin[i] * 8;
        }

        List_Remove( &msg->entry );
        List_Append( &cl->msg_unreliable_list, &msg->entry );
        cl->msg_unreliable_bytes += MAX_SOUND_PACKET;
    }
}

void MVD_FreePlayer( mvd_player_t *player ) {
    mvd_cs_t *cs, *next;

    for( cs = player->configstrings; cs; cs = next ) {
        next = cs->next;
        Z_Free( cs );
    }
}

static void set_player_name( mvd_player_t *player, const char *string ) {
    char *p;

    Q_strlcpy( player->name, string, sizeof( player->name ) );
    p = strchr( player->name, '\\' );
    if( p ) {
        *p = 0;
    }
}

static void MVD_ParseConfigstring( mvd_t *mvd ) {
    int index;
    size_t len, maxlen;
    char *string;
    mvd_client_t *client;
    mvd_player_t *player;
    mvd_cs_t *cs, **pcs;
    int i;

    index = MSG_ReadShort();
    if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
        MVD_Destroyf( mvd, "%s: bad index: %d", __func__, index );
    }

    string = mvd->configstrings[index];
    maxlen = CS_SIZE( index );
    len = MSG_ReadString( string, maxlen );
    if( len >= maxlen ) {
        MVD_Destroyf( mvd, "%s: index %d overflowed", __func__, index );
    }

    if( index >= CS_PLAYERSKINS && index < CS_PLAYERSKINS + mvd->maxclients ) {
        // update player name
        player = &mvd->players[ index - CS_PLAYERSKINS ];
        set_player_name( player, string );
        LIST_FOR_EACH( mvd_client_t, client, &mvd->clients, entry ) {
            if( client->cl->state < cs_spawned ) {
                continue;
            }
            if( client->target == player && client->layout_type == LAYOUT_FOLLOW ) {
                client->layout_time = 0;
            }
        }
    } else if( index >= CS_GENERAL ) {
        // reset unicast versions of this string
        for( i = 0; i < mvd->maxclients; i++ ) {
            player = &mvd->players[i];
            pcs = &player->configstrings;
            for( cs = player->configstrings; cs; cs = cs->next ) {
                if( cs->index == index ) {
                    Com_DPrintf( "%s: reset %d on %d\n", __func__, index, i );
                    *pcs = cs->next;
                    Z_Free( cs );
                    break;
                }
                pcs = &cs->next;
            }
        }
    }

    MSG_WriteByte( svc_configstring );
    MSG_WriteShort( index );
    MSG_WriteData( string, len + 1 );
    
    // broadcast configstring change
    LIST_FOR_EACH( mvd_client_t, client, &mvd->clients, entry ) {
        if( client->cl->state < cs_primed ) {
            continue;
        }
        SV_ClientAddMessage( client->cl, MSG_RELIABLE );
    }

    SZ_Clear( &msg_write );
}

static void MVD_ParsePrint( mvd_t *mvd ) {
    int level;
    char string[MAX_STRING_CHARS];

    level = MSG_ReadByte();
    MSG_ReadString( string, sizeof( string ) );

    MVD_BroadcastPrintf( mvd, level, level == PRINT_CHAT ?
        UF_MUTE_PLAYERS : 0, "%s", string );
}

/*
Fix origin and angles on each player entity by
extracting data from player state.
*/
static void MVD_PlayerToEntityStates( mvd_t *mvd ) {
    mvd_player_t *player;
    edict_t *edict;
    int i;

    mvd->numplayers = 0;
    for( i = 1, player = mvd->players; i <= mvd->maxclients; i++, player++ ) {
        if( !player->inuse || player == mvd->dummy ) {
            continue;
        }

        mvd->numplayers++;
        if( player->ps.pmove.pm_type != PM_NORMAL ) {
            continue;   // can be out of sync, in this case
                        // server should provide valid data
        }

        edict = &mvd->edicts[i];
        if( !edict->inuse ) {
            continue; // not present in this frame
        }

        Com_PlayerToEntityState( &player->ps, &edict->s );

        MVD_LinkEdict( mvd, edict );
    }
}

#define RELINK_MASK        (U_MODEL|U_ORIGIN1|U_ORIGIN2|U_ORIGIN3|U_SOLID)

/*
==================
MVD_ParsePacketEntities
==================
*/
static void MVD_ParsePacketEntities( mvd_t *mvd ) {
    int     number;
    int     bits;
    edict_t *ent;

    while( 1 ) {
        if( msg_read.readcount > msg_read.cursize ) {
            MVD_Destroyf( mvd, "%s: read past end of message", __func__ );
        }

        number = MSG_ParseEntityBits( &bits );
        if( number < 0 || number >= MAX_EDICTS ) {
            MVD_Destroyf( mvd, "%s: bad number: %d", __func__, number );
        }

        if( !number ) {
            break;
        }

        ent = &mvd->edicts[number];

#if USE_CLIENT
        if( mvd_shownet->integer > 2 ) {
            Com_Printf( "   %s: %d ", ent->inuse ?
                "delta" : "baseline", number );
            MSG_ShowDeltaEntityBits( bits );
            Com_Printf( "\n" );
        }
#endif

        MSG_ParseDeltaEntity( &ent->s, &ent->s, number, bits );

        // lazily relink even if removed
        if( bits & RELINK_MASK ) {
            MVD_LinkEdict( mvd, ent );
        }

        if( bits & U_REMOVE ) {
#if USE_CLIENT
            if( mvd_shownet->integer > 2 ) {
                Com_Printf( "   remove: %d\n", number );
            }
#endif
            ent->inuse = qfalse;
            continue;
        }

        ent->inuse = qtrue;
        if( number >= mvd->pool.num_edicts ) {
            mvd->pool.num_edicts = number + 1;
        }
    }
}

/*
==================
MVD_ParsePacketPlayers
==================
*/
static void MVD_ParsePacketPlayers( mvd_t *mvd ) {
    int             number;
    int             bits;
    mvd_player_t    *player;

    while( 1 ) {
        if( msg_read.readcount > msg_read.cursize ) {
            MVD_Destroyf( mvd, "%s: read past end of message", __func__ );
        }

        number = MSG_ReadByte();
        if( number == CLIENTNUM_NONE ) {
            break;
        }

        if( number < 0 || number >= mvd->maxclients ) {
            MVD_Destroyf( mvd, "%s: bad number: %d", __func__, number );
        }

        player = &mvd->players[number];

        bits = MSG_ReadShort();

#if USE_CLIENT
        if( mvd_shownet->integer > 2 ) {
            Com_Printf( "   %s: %d ", player->inuse ?
                "delta" : "baseline", number );
            MSG_ShowDeltaPlayerstateBits_Packet( bits );
            Com_Printf( "\n" );
        }
#endif

        MSG_ParseDeltaPlayerstate_Packet( &player->ps, &player->ps, bits );

        if( bits & PPS_REMOVE ) {    
#if USE_CLIENT
            if( mvd_shownet->integer > 2 ) {
                Com_Printf( "   remove: %d\n", number );
            }
#endif
            player->inuse = qfalse;
            continue;
        }

        player->inuse = qtrue;
    }
}

/*
================
MVD_ParseFrame
================
*/
static void MVD_ParseFrame( mvd_t *mvd ) {
    int length;

    // read portalbits
    length = MSG_ReadByte();
    if( length ) {
        if( length < 0 || msg_read.readcount + length > msg_read.cursize ) {
            MVD_Destroyf( mvd, "%s: read past end of message", __func__ );
        }
        if( length > MAX_MAP_PORTAL_BYTES ) {
            MVD_Destroyf( mvd, "%s: bad portalbits length: %d", __func__, length );
        }
        CM_SetPortalStates( &mvd->cm, msg_read.data +
            msg_read.readcount, length );
        msg_read.readcount += length;
    } else {
        CM_SetPortalStates( &mvd->cm, NULL, 0 );
    }

#if USE_CLIENT
    if( mvd_shownet->integer > 1 ) {
        Com_Printf( "%3"PRIz":playerinfo\n", msg_read.readcount - 1 );
    }
#endif

    MVD_ParsePacketPlayers( mvd );

#if USE_CLIENT
    if( mvd_shownet->integer > 1 ) {
        Com_Printf( "%3"PRIz":packetentities\n", msg_read.readcount - 1 );
    }
#endif

    MVD_ParsePacketEntities( mvd );

#if USE_CLIENT
    if( mvd_shownet->integer > 1 ) {
        Com_Printf( "%3"PRIz":frame:%u\n", msg_read.readcount - 1, mvd->framenum );
    }
#endif

    MVD_PlayerToEntityStates( mvd );

    mvd->framenum++;
}

static void MVD_ClearState( mvd_t *mvd ) {
    mvd_player_t *player;
    int i;

    // clear all entities, don't trust num_edicts as it is possible
    // to miscount removed entities
    memset( mvd->edicts, 0, sizeof( mvd->edicts ) );
    mvd->pool.num_edicts = 0;

    // clear all players
    for( i = 0; i < mvd->maxclients; i++ ) {
        player = &mvd->players[i];
        MVD_FreePlayer( player );
        memset( player, 0, sizeof( *player ) );
    }
    mvd->numplayers = 0;

    // free current map
    CM_FreeMap( &mvd->cm );

    if( mvd->intermission ) {
        // save oldscores
        //strcpy( mvd->oldscores, mvd->layout );
    }

    memset( mvd->configstrings, 0, sizeof( mvd->configstrings ) );
    mvd->layout[0] = 0;

    mvd->framenum = 0;
    // intermission flag will be cleared in MVD_ChangeLevel
}

static void MVD_ChangeLevel( mvd_t *mvd ) {
    mvd_client_t *client;

    if( sv.state != ss_broadcast ) {
        MVD_Spawn_f(); // the game is just starting
        return;
    }

    // cause all UDP clients to reconnect
    MSG_WriteByte( svc_stufftext );
    MSG_WriteString( va( "changing map=%s; reconnect\n", mvd->mapname ) );

    LIST_FOR_EACH( mvd_client_t, client, &mvd->clients, entry ) {
        if( mvd->intermission && client->cl->state == cs_spawned ) {
            // make them switch to previous target instead of MVD dummy
            client->target = client->oldtarget;
            client->oldtarget = NULL;
        }
        SV_ClientReset( client->cl );
        client->cl->spawncount = mvd->servercount;
        SV_ClientAddMessage( client->cl, MSG_RELIABLE );
    }

    SZ_Clear( &msg_write );

    mvd->intermission = qfalse;

    SV_SendAsyncPackets();
}

static void MVD_ParseServerData( mvd_t *mvd, int extrabits ) {
    int protocol;
    size_t len, maxlen;
    char *string;
    int i, index;
    mvd_player_t *player;

    // clear the leftover from previous level
    MVD_ClearState( mvd );

    // parse major protocol version
    protocol = MSG_ReadLong();
    if( protocol != PROTOCOL_VERSION_MVD ) {
        MVD_Destroyf( mvd, "Unsupported protocol: %d", protocol );
    }

    // parse minor protocol version
    protocol = MSG_ReadShort();
    if( !MVD_SUPPORTED( protocol ) ) {
        MVD_Destroyf( mvd, "Unsupported MVD protocol version: %d.\n"
            "Current version is %d.\n", protocol, PROTOCOL_VERSION_MVD_CURRENT );
    }

    mvd->servercount = MSG_ReadLong();
    len = MSG_ReadString( mvd->gamedir, sizeof( mvd->gamedir ) );
    if( len >= sizeof( mvd->gamedir ) ) {
        MVD_Destroyf( mvd, "Oversize gamedir string" );
    }
    mvd->clientNum = MSG_ReadShort();
    mvd->flags = extrabits;

#if 0
    // change gamedir unless playing a demo
    Cvar_UserSet( "game", mvd->gamedir );
    if( FS_NeedRestart() ) {
        FS_Restart();
    }
#endif

    // parse configstrings
    while( 1 ) {
        index = MSG_ReadShort();
        if( index == MAX_CONFIGSTRINGS ) {
            break;
        }

        if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
            MVD_Destroyf( mvd, "Bad configstring index: %d", index );
        }

        string = mvd->configstrings[index];
        maxlen = CS_SIZE( index );
        len = MSG_ReadString( string, maxlen );
        if( len >= maxlen ) {
            MVD_Destroyf( mvd, "Configstring %d overflowed", index );
        }

        if( msg_read.readcount > msg_read.cursize ) {
            MVD_Destroyf( mvd, "Read past end of message" );
        }
    }

    // parse maxclients
    index = atoi( mvd->configstrings[CS_MAXCLIENTS] );
    if( index < 1 || index > MAX_CLIENTS ) {
        MVD_Destroyf( mvd, "Invalid maxclients" );
    }

    // check if maxclients changed
    if( index != mvd->maxclients ) {
        mvd_client_t *client;

        // free any old players
        Z_Free( mvd->players );

        // allocate new players
        mvd->players = MVD_Mallocz( sizeof( mvd_player_t ) * index );
        mvd->maxclients = index;

        // clear chase targets
        LIST_FOR_EACH( mvd_client_t, client, &mvd->clients, entry ) {
            client->target = client->oldtarget = NULL;
        }
    }

    // validate clientNum
    if( mvd->clientNum < 0 || mvd->clientNum >= mvd->maxclients ) {
        MVD_Destroyf( mvd, "Invalid client num: %d", mvd->clientNum );
    }
    mvd->dummy = mvd->players + mvd->clientNum;

    // parse world model
    string = mvd->configstrings[ CS_MODELS + 1 ]; 
    len = strlen( string );
    if( len <= 9 ) {
        MVD_Destroyf( mvd, "Bad world model: %s", string );
    }
    memcpy( mvd->mapname, string + 5, len - 9 ); // skip "maps/"
    mvd->mapname[len - 9] = 0; // cut off ".bsp"

    // load the world model (we are only interesed in visibility info)
    Com_Printf( "[%s] -=- Loading %s...\n", mvd->name, string );
    if( !CM_LoadMap( &mvd->cm, string ) ) {
        MVD_Destroyf( mvd, "Couldn't load %s: %s", string, BSP_GetError() );
    }

#if USE_MAPCHECKSUM
    if( mvd->cm.cache->checksum != atoi( mvd->configstrings[CS_MAPCHECKSUM] ) ) {
        MVD_Destroyf( mvd, "Local map version differs from server" );
    }
#endif

    // set player names
    for( i = 0; i < mvd->maxclients; i++ ) {
        player = &mvd->players[i];
        string = mvd->configstrings[ CS_PLAYERSKINS + i ];
        set_player_name( player, string );
    }

    // get the spawn point for spectators
    MVD_ParseEntityString( mvd, mvd->cm.cache->entitystring );

    // parse baseline frame
    MVD_ParseFrame( mvd );

    // if the channel has been just created, init some things
    if( !mvd->state ) {
        mvd_t *cur;

        // sort this one into the list of active channels
        LIST_FOR_EACH( mvd_t, cur, &mvd_channel_list, entry ) {
            if( cur->id > mvd->id ) {
                break;
            }
        }
        List_Append( &cur->entry, &mvd->entry );
        mvd->state = MVD_WAITING;

        mvd_dirty = qtrue;
    }

    // case all UDP clients to reconnect
    MVD_ChangeLevel( mvd );
}

void MVD_ParseMessage( mvd_t *mvd ) {
    int     cmd, extrabits;

#if USE_CLIENT
    if( mvd_shownet->integer == 1 ) {
        Com_Printf( "%"PRIz" ", msg_read.cursize );
    } else if( mvd_shownet->integer > 1 ) {
        Com_Printf( "------------------\n" );
    }
#endif

//
// parse the message
//
    while( 1 ) {
        if( msg_read.readcount > msg_read.cursize ) {
            MVD_Destroyf( mvd, "Read past end of message" );
        }
        if( msg_read.readcount == msg_read.cursize ) {
#if USE_CLIENT
            if( mvd_shownet->integer > 1 ) {
                Com_Printf( "%3"PRIz":END OF MESSAGE\n", msg_read.readcount - 1 );
            }
#endif
            break;
        }

        cmd = MSG_ReadByte();
        extrabits = cmd >> SVCMD_BITS;
        cmd &= SVCMD_MASK;

#if USE_CLIENT
        if( mvd_shownet->integer > 1 ) {
            MVD_ShowSVC( cmd );
        }
#endif
    
        switch( cmd ) {
        case mvd_serverdata:
            MVD_ParseServerData( mvd, extrabits );
            break;
        case mvd_multicast_all:
        case mvd_multicast_pvs:
        case mvd_multicast_phs:
        case mvd_multicast_all_r:
        case mvd_multicast_pvs_r:
        case mvd_multicast_phs_r:
            MVD_ParseMulticast( mvd, cmd, extrabits );
            break;
        case mvd_unicast:
        case mvd_unicast_r:
            MVD_ParseUnicast( mvd, cmd, extrabits );
            break;
        case mvd_configstring:
            MVD_ParseConfigstring( mvd );
            break;
        case mvd_frame:
            MVD_ParseFrame( mvd );
            break;
        case mvd_sound:
            MVD_ParseSound( mvd, extrabits );
            break;
        case mvd_print:
            MVD_ParsePrint( mvd );
            break;
        case mvd_nop:
            break;
        default:
            MVD_Destroyf( mvd, "Illegible command at %"PRIz": %d",
                msg_read.readcount - 1, cmd );
        }
    }
}

