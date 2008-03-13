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

#include "sv_local.h"
#include "mvd_local.h"

#define MVD_ShowSVC( cmd ) do { \
	Com_Printf( "%3i:%s\n", msg_read.readcount - 1, \
        MVD_ServerCommandString( cmd ) ); \
    } while( 0 )

static const char mvd_strings[mvd_num_types][20] = {
    "mvd_bad",
    "mvd_nop",
    "mvd_disconnect",
    "mvd_reconnect",
    "mvd_serverdata",
    "mvd_configstring",
    "mvd_frame",
    "mvd_frame_nodelta",
	"mvd_unicast",
    "mvd_unicast_r",
	"mvd_multicast_all",
	"mvd_multicast_pvs",
	"mvd_multicast_phs",
	"mvd_multicast_all_r",
	"mvd_multicast_pvs_r",
	"mvd_multicast_phs_r",
    "mvd_sound",
    "mvd_print",
    "mvd_stufftext"
};

const char *MVD_ServerCommandString( int cmd ) {
    const char *s;

    if( cmd == -1 ) {
        s = "END OF MESSAGE";
    } else if( cmd >= 0 && cmd < mvd_num_types ) {
        s = mvd_strings[cmd];
    } else {
		s = "UNKNOWN COMMAND";
	}

    return s;
}

static void MVD_LinkEdict( mvd_t *mvd, edict_t *ent ) {
	int			index;
	cmodel_t	*cm;
	int			x, zd, zu;
	cmcache_t	*cache = mvd->cm.cache;
    
	if( !cache ) {
		return;
	}
    
	if( ent->s.solid == 31 ) {
		index = ent->s.modelindex;
		if( index < 1 || index > cache->numcmodels ) {
			Com_WPrintf( "%s: entity %d: bad inline model index: %d\n",
				__func__, ent->s.number, index );
			return;
		}
		cm = &cache->cmodels[ index - 1 ];
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

static void MVD_ParseEntityString( mvd_t *mvd ) {
	const char *data, *p;
	char key[MAX_STRING_CHARS];
	char value[MAX_STRING_CHARS];
	char classname[MAX_QPATH];
	vec3_t origin;
	vec3_t angles;
	
    if( !mvd->cm.cache ) {
        return;
    }
	data = mvd->cm.cache->entitystring;
	if( !data || !data[0] ) {
		return;
	}

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

			Q_strncpyz( key, p, sizeof( key ) );
			
			p = COM_Parse( &data );
			if( !data ) {
				Com_Error( ERR_DROP, "expected key/value pair, found EOF" );
			}
			if( p[0] == '}' || p[0] == '{' ) {
				Com_Error( ERR_DROP, "expected value, found '%s'", p );
			}

			if( !strcmp( key, "classname" ) ) {
				Q_strncpyz( classname, p, sizeof( classname ) );
				continue;
			}

			Q_strncpyz( value, p, sizeof( value ) );

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
	udpClient_t	*client;
    client_t    *cl;
	byte		*mask;
	cleaf_t		*leaf;
	int			cluster;
	int			area1, area2;
	vec3_t		org;
    qboolean    reliable = qfalse;
	player_state_t	*ps;
    byte        *data;
	int         length, leafnum;

	length = MSG_ReadByte();
    length |= extrabits << 8;

	switch( op ) {
	case mvd_multicast_all_r:
		reliable = qtrue;	// intentional fallthrough
	case mvd_multicast_all:
		area1 = 0;
		cluster = 0;
		mask = NULL;
		break;

	case mvd_multicast_phs_r:
		reliable = qtrue;	// intentional fallthrough
	case mvd_multicast_phs:
        leafnum = MSG_ReadShort();
		leaf = CM_LeafNum( &mvd->cm, leafnum );
		area1 = CM_LeafArea( leaf );
		cluster = CM_LeafCluster( leaf );
		mask = CM_ClusterPHS( &mvd->cm, cluster );
		break;

	case mvd_multicast_pvs_r:
		reliable = qtrue;	// intentional fallthrough
	case mvd_multicast_pvs:
        leafnum = MSG_ReadShort();
		leaf = CM_LeafNum( &mvd->cm, leafnum );
		area1 = CM_LeafArea( leaf );
		cluster = CM_LeafCluster( leaf );
		mask = CM_ClusterPVS( &mvd->cm, cluster );
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
    LIST_FOR_EACH( udpClient_t, client, &mvd->udpClients, entry ) {
        cl = client->cl;
        if( cl->state < cs_primed ) {
            continue;
        }

		// do not send unreliables to connecting clients
		if( !reliable && ( cl->state != cs_spawned || cl->download || ( cl->flags & CF_NODATA ) ) ) {
			continue;
		}

		if( mask ) {
			// find the client's PVS
			ps = &client->ps;
			VectorMA( ps->viewoffset, 0.125f, ps->pmove.origin, org );
			leaf = CM_PointLeaf( &mvd->cm, org );
			area2 = CM_LeafArea( leaf );
			if( !CM_AreasConnected( &mvd->cm, area1, area2 ) )
				continue;
			cluster = CM_LeafCluster( leaf );
			if( !Q_IsBitSet( mask, cluster ) ) {
				continue;
			}
		}

		cl->AddMessage( cl, data, length, reliable );
	}
}

static void MVD_UnicastSend( mvd_t *mvd, qboolean reliable, byte *data, int length, mvd_player_t *player ) {
    mvd_player_t *target;
    udpClient_t *client;
    client_t *cl;
    
	// send to all relevant clients
    LIST_FOR_EACH( udpClient_t, client, &mvd->udpClients, entry ) {
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
    char *string;
    udpClient_t *client;

    string = MSG_ReadString();
    if( player != mvd->dummy ) {
        return; // we don't care about others
    }

    Q_strncpyz( mvd->layout, string, sizeof( mvd->layout ) );

	// send to all relevant clients
    LIST_FOR_EACH( udpClient_t, client, &mvd->udpClients, entry ) {
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
    char *string;
    mvd_cs_t *cs;
    byte *data;
    int readcount, length;

	data = msg_read.data + msg_read.readcount - 1;
    readcount = msg_read.readcount - 1;

    index = MSG_ReadShort();
    string = MSG_ReadStringLength( &length );

    if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
        MVD_Destroyf( mvd, "%s: bad index: %d", __func__, index );
    }
    if( index < CS_GENERAL ) {
        Com_DPrintf( "%s: common configstring: %d\n", __func__, index );
        return;
    }
    if( length >= MAX_QPATH ) {
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
    char *string;
    byte *data;
    int readcount, length;
    udpClient_t *client;
    client_t *cl;
    mvd_player_t *target;

	data = msg_read.data + msg_read.readcount - 1;
    readcount = msg_read.readcount - 1;

    level = MSG_ReadByte();
    string = MSG_ReadString();

    length = msg_read.readcount - readcount;
    
	// send to all relevant clients
    LIST_FOR_EACH( udpClient_t, client, &mvd->udpClients, entry ) {
        cl = client->cl;
        if( cl->state < cs_spawned ) {
            continue;
        }
		if( level < cl->messagelevel ) {
			continue;
        }
        if( level == PRINT_CHAT && ( client->uf & UF_NOGAMECHAT ) ) {
            continue;
        }
        target = client->target ? client->target : mvd->dummy;
		if( target == player ) {
		    cl->AddMessage( cl, data, length, reliable );
		}
	}
}

static void MVD_UnicastStuff( mvd_t *mvd, qboolean reliable, mvd_player_t *player ) {
    char *string;
    byte *data;
    int readcount, length;

	data = msg_read.data + msg_read.readcount - 1;
    readcount = msg_read.readcount - 1;

    string = MSG_ReadString();
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
	int clientNum, length, last;
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
		if( mvd_shownet->integer > 1 ) {
			MSG_ShowSVC( cmd );
		}
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
            if( mvd_shownet->integer > 1 ) {
                Com_Printf( "%d:SKIPPING UNICAST\n", msg_read.readcount - 1 );
            }
			// send remaining data and return
            data = msg_read.data + msg_read.readcount - 1;
            length = last - msg_read.readcount + 1;
            MVD_UnicastSend( mvd, reliable, data, length, player );
			msg_read.readcount = last;
			return;
		}
	}

	if( mvd_shownet->integer > 1 ) {
		Com_Printf( "%d:END OF UNICAST\n", msg_read.readcount - 1 );
	}

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
    int flags, index;
    int volume, attenuation, offset, sendchan;
	int			entnum;
	vec3_t		origin;
    udpClient_t *client;
	client_t	*cl;
	byte		*mask;
	cleaf_t		*leaf;
	int			area, cluster;
    player_state_t  *ps;
    sound_packet_t  *msg;
    edict_t     *entity;

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

    LIST_FOR_EACH( udpClient_t, client, &mvd->udpClients, entry ) {
        cl = client->cl;

		// do not send unreliables to connecting clients
		if( cl->state != cs_spawned || cl->download || ( cl->flags & CF_NODATA ) ) {
			continue;
		}

        // PHS cull this sound
        if( extrabits & 1 ) {
            // get client viewpos
            ps = &client->ps;
            VectorMA( ps->viewoffset, 0.125f, ps->pmove.origin, origin );
            leaf = CM_PointLeaf( &mvd->cm, origin );
            area = CM_LeafArea( leaf );
            if( !CM_AreasConnected( &mvd->cm, area, entity->areanum ) ) {
                // doors can legally straddle two areas, so
                // we may need to check another one
                if( !entity->areanum2 || !CM_AreasConnected( &mvd->cm, area, entity->areanum2 ) ) {
                    continue;		// blocked by a door
                }
            }
            cluster = CM_LeafCluster( leaf );
            mask = CM_ClusterPHS( &mvd->cm, cluster );
            if( !SV_EdictPV( &mvd->cm, entity, mask ) ) {
                continue; // not in PHS
            }
        }

        // reliable sounds will always have position explicitly set,
        // as no one gurantees reliables to be delivered in time
        // why should this happen anyway?
        if( extrabits & 2 ) {
            // use the entity origin unless it is a bmodel
            if( entity->solid == SOLID_BSP ) {
                VectorAvg( entity->mins, entity->maxs, origin );
                VectorAdd( entity->s.origin, origin, origin );
            } else {
                VectorCopy( entity->s.origin, origin );
            }

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

        if( LIST_EMPTY( &cl->msg_free ) ) {
            Com_WPrintf( "%s: %s: out of message slots\n",
                __func__, cl->name );
            continue;
        }

        msg = LIST_FIRST( sound_packet_t, &cl->msg_free, entry );

        msg->cursize = 0;
        msg->flags = flags;
        msg->index = index;
        msg->volume = volume;
        msg->attenuation = attenuation;
        msg->timeofs = offset;
        msg->sendchan = sendchan;

        List_Remove( &msg->entry );
        List_Append( &cl->msg_sound, &msg->entry );
	}
}

static void MVD_ParseConfigstring( mvd_t *mvd ) {
	int index, length;
	char *string, *p;
	udpClient_t *client;
    mvd_player_t *player;
    mvd_cs_t *cs, **pcs;
    int i;

	index = MSG_ReadShort();

	if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		MVD_Destroyf( mvd, "%s: bad index: %d", __func__, index );
	}

	string = MSG_ReadStringLength( &length );

	if( MAX_QPATH * index + length >= sizeof( mvd->configstrings ) ) {
		MVD_Destroyf( mvd, "%s: oversize configstring: %d", __func__, index );
	}

	if( !strcmp( mvd->configstrings[index], string ) ) {
		return;
	}
    
	if( index >= CS_PLAYERSKINS && index < CS_PLAYERSKINS + mvd->maxclients ) {
        // update player name
        player = &mvd->players[ index - CS_PLAYERSKINS ];
        Q_strncpyz( player->name, string, sizeof( player->name ) );
        p = strchr( player->name, '\\' );
        if( p ) {
            *p = 0;
        }
        LIST_FOR_EACH( udpClient_t, client, &mvd->udpClients, entry ) {
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

	memcpy( mvd->configstrings[index], string, length + 1 );

	MSG_WriteByte( svc_configstring );
	MSG_WriteShort( index );
	MSG_WriteData( string, length + 1 );
	
    // broadcast configstring change
    LIST_FOR_EACH( udpClient_t, client, &mvd->udpClients, entry ) {
        if( client->cl->state < cs_primed ) {
            continue;
        }
		SV_ClientAddMessage( client->cl, MSG_RELIABLE );
	}

	SZ_Clear( &msg_write );
}

static void MVD_ParsePrint( mvd_t *mvd ) {
    int level = MSG_ReadByte();
    char *string = MSG_ReadString();

    MVD_BroadcastPrintf( mvd, level, UF_NOGAMECHAT, "%s", string );
}

/*
Fix origin and angles on each player entity by
extracting data from player state.
*/
static void MVD_PlayerToEntityStates( mvd_t *mvd ) {
    mvd_player_t *player;
    edict_t *edict;
	int i;

    for( i = 1, player = mvd->players; i <= mvd->maxclients; i++, player++ ) {
        if( !player->inuse ) {
            continue;
        }
        if( player->ps.pmove.pm_type >= PM_DEAD ) {
            continue; // can be out of sync
        }

        edict = &mvd->edicts[i];
        if( !edict->inuse ) {
            continue; // not present in this frame
        }

		VectorCopy( edict->s.origin, edict->s.old_origin );

        VectorScale( player->ps.pmove.origin, 0.125f, edict->s.origin );
        VectorCopy( player->ps.viewangles, edict->s.angles );

        if( edict->s.angles[PITCH] > 180 ) {
            edict->s.angles[PITCH] -= 360;
        }

        edict->s.angles[PITCH] = edict->s.angles[PITCH] / 3;

        MVD_LinkEdict( mvd, edict );
    }
}

#define RELINK_MASK	    (U_MODEL|U_ORIGIN1|U_ORIGIN2|U_ORIGIN3|U_SOLID)


/*
==================
MVD_ParsePacketEntities
==================
*/
static void MVD_ParsePacketEntities( mvd_t *mvd ) {
	int			number;
	int			bits;
    edict_t     *ent;

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

        if( mvd_shownet->integer > 2 ) {
			Com_Printf( "   %s: %d ", ent->inuse ?
                "delta" : "baseline", number );
            MSG_ShowDeltaEntityBits( bits );
            Com_Printf( "\n" );
        }

        MSG_ParseDeltaEntity( &ent->s, &ent->s, number, bits );

		if( bits & U_REMOVE ) {	
			if( mvd_shownet->integer > 2 ) {
				Com_Printf( "   remove: %d\n", number );
			}
            ent->inuse = qfalse;
			continue;
		}

        ent->inuse = qtrue;
        if( number >= mvd->pool.num_edicts ) {
            mvd->pool.num_edicts = number + 1;
        }

        if( number > mvd->maxclients && ( bits & RELINK_MASK ) ) {
            MVD_LinkEdict( mvd, ent );
        }
	}
}

/*
==================
MVD_ParsePacketPlayers
==================
*/
static void MVD_ParsePacketPlayers( mvd_t *mvd ) {
	int			number;
	int			bits;
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

        if( mvd_shownet->integer > 2 ) {
			Com_Printf( "   %s: %d ", player->inuse ?
                "delta" : "baseline", number );
            MSG_ShowDeltaPlayerstateBits_Packet( bits );
            Com_Printf( "\n" );
        }

        MSG_ParseDeltaPlayerstate_Packet( &player->ps, &player->ps, bits );

		if( bits & PPS_REMOVE ) {	
			if( mvd_shownet->integer > 2 ) {
				Com_Printf( "   remove: %d\n", number );
			}
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
		if( length > MAX_MAP_AREAS/8 ) {
            MVD_Destroyf( mvd, "%s: bad portalbits length: %d", __func__, length );
		}
        CM_SetPortalStates( &mvd->cm, msg_read.data +
            msg_read.readcount, length );
        msg_read.readcount += length;
	} else {
        CM_SetPortalStates( &mvd->cm, NULL, 0 );
	}

	if( mvd_shownet->integer > 1 ) {
		Com_Printf( "%3i:playerinfo\n", msg_read.readcount - 1 );
	}

	MVD_ParsePacketPlayers( mvd );

	if( mvd_shownet->integer > 1 ) {
		Com_Printf( "%3i:packetentities\n", msg_read.readcount - 1 );
	}

	MVD_ParsePacketEntities( mvd );

	if( mvd_shownet->integer > 1 ) {
		Com_Printf( "%3i: frame:%i\n", msg_read.readcount - 1, mvd->framenum );
	}

	MVD_PlayerToEntityStates( mvd );

    mvd->framenum++;
}

static void MVD_ParseServerData( mvd_t *mvd ) {
	int protocol;
    int length, index;
	char *gamedir, *string, *p;
	uint32_t checksum;
    int i;
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
	gamedir = MSG_ReadString();
    mvd->clientNum = MSG_ReadShort();
	
	// change gamedir unless playing a demo
    Q_strncpyz( mvd->gamedir, gamedir, sizeof( mvd->gamedir ) );
	if( !mvd->demoplayback ) {
		Cvar_UserSet( "game", gamedir );
        if( FS_NeedRestart() ) {
            FS_Restart();
        }
	    Cvar_FullSet( "gamedir", "gtv", CVAR_SERVERINFO|CVAR_NOSET, CVAR_SET_DIRECT );
	}

    // parse configstrings
    while( 1 ) {
        index = MSG_ReadShort();
        if( index == MAX_CONFIGSTRINGS ) {
            break;
        }

        if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
            MVD_Destroyf( mvd, "Bad configstring index: %d", index );
        }

        string = MSG_ReadStringLength( &length );

        if( MAX_QPATH * index + length > sizeof( mvd->configstrings ) - 1 ) {
            MVD_Destroyf( mvd, "Oversize configstring" );
        }

        memcpy( mvd->configstrings[index], string, length + 1 );

        if( msg_read.readcount > msg_read.cursize ) {
            MVD_Destroyf( mvd, "Read past end of message" );
        }
    }

    // parse maxclients
    index = atoi( mvd->configstrings[CS_MAXCLIENTS] );
    if( index < 1 || index > MAX_CLIENTS ) {
        MVD_Destroyf( mvd, "Invalid maxclients" );
    }

    if( !mvd->players ) {
        mvd->players = MVD_Mallocz( sizeof( mvd_player_t ) * index );
        mvd->maxclients = index;
    } else if( index != mvd->maxclients ) {
        MVD_Destroyf( mvd, "Unexpected maxclients change" );
    }

    // validate clientNum
    if( mvd->clientNum < 0 || mvd->clientNum >= mvd->maxclients ) {
        MVD_Destroyf( mvd, "Invalid client num: %d", mvd->clientNum );
    }
    mvd->dummy = mvd->players + mvd->clientNum;

    // parse world model
    string = mvd->configstrings[ CS_MODELS + 1]; 
    length = strlen( string );
    if( length <= 9 ) {
        MVD_Destroyf( mvd, "Bad world model: %s", string );
    }
    strcpy( mvd->mapname, string + 5 ); // skip "maps/"
    mvd->mapname[length - 9] = 0; // cut off ".bsp"

    // check if map exists so CM_LoadMap does not kill
    // entire server if it does not
    if( FS_LoadFile( string, NULL ) == -1 ) {
        MVD_Destroyf( mvd, "Couldn't find map: %s", string );
    }

	// load the world model (we are only interesed in
    // visibility info, do not load brushes and such)
    Com_Printf( "[%s] Loading %s...\n", mvd->name, string );
    CM_LoadMap( &mvd->cm, string, CM_LOAD_VISONLY, &checksum );

#if USE_MAPCHECKSUM
    if( checksum != atoi( mvd->configstrings[CS_MAPCHECKSUM] ) ) {
        MVD_Destroyf( mvd, "Local map version differs from server" );
    }
#endif

    // set player names
    for( i = 0; i < mvd->maxclients; i++ ) {
        player = &mvd->players[i];
        string = mvd->configstrings[ CS_PLAYERSKINS + i ];
        if( *string ) {
            Q_strncpyz( player->name, string, sizeof( player->name ) );
            p = strchr( player->name, '\\' );
            if( p ) {
                *p = 0;
            }
        }
    }

    // get the spawn point for spectators
    MVD_ParseEntityString( mvd );

    // parse baseline frame
    MVD_ParseFrame( mvd );

    if( mvd->state < MVD_WAITING ) {
        List_Append( &mvd_ready, &mvd->ready );
        mvd->state = mvd->demoplayback ? MVD_READING : MVD_WAITING;
        mvd->waitTime = svs.realtime;
        mvd_dirty = qtrue;
    }

    MVD_ChangeLevel( mvd );
}

static qboolean MVD_ParseMessage( mvd_t *mvd, fifo_t *fifo ) {
    uint16_t    msglen;
    uint32_t    magic;
    byte        *data;
    int         length;
	int			cmd, extrabits;

    // parse magic
    if( mvd->state == MVD_CHECKING ) {
        if( !FIFO_TryRead( fifo, &magic, 4 ) ) {
            return qfalse;
        }
        if( magic != MVD_MAGIC ) {
            MVD_Destroyf( mvd, "Not a MVD stream" );
        }
        mvd->state = MVD_PREPARING;
    }

    // parse msglen
    msglen = mvd->msglen;
    if( !msglen ) {
        if( !FIFO_TryRead( fifo, &msglen, 2 ) ) {
            return qfalse;
        }
        if( !msglen ) {
            MVD_Finish( mvd, "End of MVD stream reached" );
        }
        msglen = LittleShort( msglen );
        if( msglen > MAX_MSGLEN ) {
            MVD_Destroyf( mvd, "Invalid MVD message length: %d bytes", msglen );
        }
        mvd->msglen = msglen;
    }

    // first, try to read in a single block
    data = FIFO_Peek( fifo, &length );
    if( length < msglen ) {
        if( !FIFO_TryRead( fifo, msg_read_buffer, msglen ) ) {
            return qfalse; // not yet available
        }
        SZ_Init( &msg_read, msg_read_buffer, sizeof( msg_read_buffer ) );
    } else {
        SZ_Init( &msg_read, data, msglen );
        FIFO_Decommit( fifo, msglen );
    }

    mvd->msglen = 0;

    msg_read.cursize = msglen;

	if( mvd_shownet->integer == 1 ) {
		Com_Printf( "%i ", msg_read.cursize );
	} else if( mvd_shownet->integer > 1 ) {
		Com_Printf( "------------------\n" );
	}

//
// parse the message
//
	while( 1 ) {
		if( msg_read.readcount > msg_read.cursize ) {
            MVD_Destroyf( mvd, "Read past end of message" );
		}

		if( ( cmd = MSG_ReadByte() ) == -1 ) {
			if( mvd_shownet->integer > 1 ) {
				Com_Printf( "%3i:END OF MESSAGE\n", msg_read.readcount - 1 );
			}
			break;
		}

        extrabits = cmd >> SVCMD_BITS;
        cmd &= SVCMD_MASK;

		if( mvd_shownet->integer > 1 ) {
			MVD_ShowSVC( cmd );
		}

		switch( cmd ) {
		case mvd_serverdata:
			MVD_ParseServerData( mvd );
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
		//case mvd_frame_nodelta:
            //MVD_ResetFrame( mvd );
			//MVD_ParseFrame( mvd );
			//break;
		case mvd_sound:
			MVD_ParseSound( mvd, extrabits );
			break;
        case mvd_print:
            MVD_ParsePrint( mvd );
            break;
		default:
            MVD_Destroyf( mvd, "Illegible command at %d: %d",
                msg_read.readcount - 1, cmd );
		}
	}

    return qtrue;
}

#if USE_ZLIB
static int MVD_Decompress( mvd_t *mvd ) {
    byte        *data;
    int         avail_in, avail_out;
    z_streamp   z = &mvd->z;
    int         ret = Z_BUF_ERROR;

    do {
        data = FIFO_Peek( &mvd->stream.recv, &avail_in );
        if( !avail_in ) {
            break;
        }
        z->next_in = data;
        z->avail_in = avail_in;

        data = FIFO_Reserve( &mvd->zbuf, &avail_out );
        if( !avail_out ) {
            break;
        }
        z->next_out = data;
        z->avail_out = avail_out;

        ret = inflate( z, Z_SYNC_FLUSH );

        FIFO_Decommit( &mvd->stream.recv, avail_in - z->avail_in );
        FIFO_Commit( &mvd->zbuf, avail_out - z->avail_out );
    } while( ret == Z_OK );

    return ret;
}
#endif

qboolean MVD_Parse( mvd_t *mvd ) {
    fifo_t *fifo;

#if USE_ZLIB
    if( mvd->z.state ) {
        int ret = MVD_Decompress( mvd );

        switch( ret ) {
        case Z_BUF_ERROR:
        case Z_OK:
            break;
        case Z_STREAM_END:
            Com_DPrintf( "End of zlib stream reached\n" );
            inflateEnd( &mvd->z );
            break;
        default:
            MVD_Destroyf( mvd, "inflate() failed: %s", mvd->z.msg );
        }
    }
    if( mvd->zbuf.data ) {
        fifo = &mvd->zbuf;
    } else
#endif
    {
        fifo = &mvd->stream.recv;
    }

    if( MVD_ParseMessage( mvd, fifo ) ) {
        return qtrue;
    }

    // ran out of buffers
    if( mvd->state == MVD_DISCONNECTED ) {
        MVD_Finish( mvd, "MVD stream was truncated" );
    }
    if( mvd->state == MVD_READING ) {
        Com_Printf( "[%s] Buffering data...\n", mvd->name );
        MVD_BeginWaiting( mvd );
    }
    return qfalse;
}

