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

static void MVD_LinkEntity( mvd_t *mvd, entityStateEx_t *ent ) {
	vec3_t		mins, maxs;
	cleaf_t		*leafs[MAX_TOTAL_ENT_LEAFS];
	int			clusters[MAX_TOTAL_ENT_LEAFS];
	int			num_leafs;
	int			i, j;
	int			area;
	cnode_t		*topnode;
	int			modelindex;
	cmodel_t	*cm;
	int			x, zd, zu;
	cmcache_t	*cache;

	cache = mvd->cm.cache;
	if( !cache ) {
		return;

	}
	if( ent->s.solid == 31 ) {
		modelindex = ent->s.modelindex;
		if( modelindex < 1 || modelindex > cache->numcmodels ) {
			Com_WPrintf( "MVD_LinkEntity: entity %d: "
				"bad inline model index: %d\n",
				ent->s.number, modelindex );
			ent->linked = qfalse;
			return;
		}
		cm = &cache->cmodels[ modelindex - 1 ];
		VectorCopy( cm->mins, mins );
		VectorCopy( cm->maxs, maxs );		
	} else if( ent->s.solid ) {
		x = 8 * ( ent->s.solid & 31 );
		zd = 8 * ( ( ent->s.solid >> 5 ) & 31 );
		zu = 8 * ( ( ent->s.solid >> 10 ) & 63 ) - 32;

		mins[0] = mins[1] = -x;
		maxs[0] = maxs[1] = x;
		mins[2] = -zd;
		maxs[2] = zu;
	} else {
		VectorClear( mins );
		VectorClear( maxs );
	}

// set the abs box
	if( ent->s.solid == 31 &&
		( ent->s.angles[0] || ent->s.angles[1] || ent->s.angles[2] ) )
	{	// expand for rotation
		float		max, v;
		int			i;

		max = 0;
		for( i = 0; i < 3; i++ ) {
			v = fabs( mins[i] );
			if( v > max )
				max = v;
			v = fabs( maxs[i] );
			if( v > max )
				max = v;
		}
		for( i = 0; i < 3; i++ ) {
			mins[i] = ent->s.origin[i] - max;
			maxs[i] = ent->s.origin[i] + max;
		}
	} else {
		// normal
		VectorAdd( mins, ent->s.origin, mins );	
		VectorAdd( maxs, ent->s.origin, maxs );
	}

	// because movement is clipped an epsilon away from an actual edge,
	// we must fully check even when bounding boxes don't quite touch
	mins[0] -= 1;
	mins[1] -= 1;
	mins[2] -= 1;
	maxs[0] += 1;
	maxs[1] += 1;
	maxs[2] += 1;

	// link to PVS leafs
	ent->num_clusters = 0;
	ent->areanum = 0;
	ent->areanum2 = 0;

	// get all leafs, including solids
	num_leafs = CM_BoxLeafs( &mvd->cm, mins, maxs,
		leafs, MAX_TOTAL_ENT_LEAFS, &topnode );

	// set areas
	for( i = 0; i < num_leafs; i++ ) {
		clusters[i] = CM_LeafCluster( leafs[i] );
		area = CM_LeafArea( leafs[i] );
		if( area ) {
			// doors may legally straggle two areas,
			// but nothing should evern need more than that
			if( ent->areanum && ent->areanum != area ) {
				ent->areanum2 = area;
			} else {
				ent->areanum = area;
			}
		}
	}

	if( num_leafs >= MAX_TOTAL_ENT_LEAFS ) {
		// assume we missed some leafs, and mark by headnode
		ent->num_clusters = -1;
		ent->headnode = topnode;
	} else {
		ent->num_clusters = 0;
		for( i = 0; i < num_leafs; i++ ) {
			if( clusters[i] == -1 )
				continue;		// not a visible leaf
			for( j = 0; j < i; j++ )
				if( clusters[j] == clusters[i] )
					break;
			if( j == i ) {
				if ( ent->num_clusters == MAX_ENT_CLUSTERS ) {
					// assume we missed some leafs, and mark by headnode
					ent->num_clusters = -1;
					ent->headnode = topnode;
					break;
				}

				ent->clusternums[ent->num_clusters++] = clusters[i];
			}
		}
	}

	ent->linked = qtrue;
}

static void MVD_ParseEntityString( mvd_t *mvd ) {
	const char *data, *p;
	char key[MAX_STRING_CHARS];
	char value[MAX_STRING_CHARS];
	char classname[MAX_QPATH];
	vec3_t origin;
	vec3_t angles;
	
	mvd->spawnSet = qfalse;

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
			mvd->spawnSet = qtrue;
			continue;
		}
		
		if( mvd->spawnSet ) {
			continue;
		}
			
		if( !strcmp( classname + 12, "start" ) ||
            !strcmp( classname + 12, "deathmatch" ) )
        {
			VectorCopy( origin, mvd->spawnOrigin );
			VectorCopy( angles, mvd->spawnAngles );
			mvd->spawnSet = qtrue;
		}

	}

	if( !mvd->spawnSet ) {
		Com_WPrintf( "Couldn't find spawn point for MVD spectators\n" );
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
		MVD_Destroy( mvd, "bad op" );
	}

    // skip data payload
	data = msg_read.data + msg_read.readcount;
	msg_read.readcount += length;
	if( msg_read.readcount > msg_read.cursize ) {
		MVD_Destroy( mvd, "read past end of message" );
	}

	// send the data to all relevent clients
    LIST_FOR_EACH( udpClient_t, client, &mvd->udpClients, entry ) {
        cl = client->cl;
        if( cl->state < cs_primed ) {
            continue;
        }

		// do not send unreliables to connecting clients
		if( !reliable && ( cl->state != cs_spawned || cl->download || cl->nodata ) ) {
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

static void MVD_ParseUnicast( mvd_t *mvd, mvd_ops_t op, int extrabits ) {
	int clientNum, length, last;
	int flags;
	udpClient_t *client;
	mvdPlayer_t *player;
    clstate_t minstate;
	int i, c;
	char *s;
	qboolean gotLayout, wantLayout;
    mvdConfigstring_t *cs;

	length = MSG_ReadByte();
    length |= extrabits << 8;
	clientNum = MSG_ReadByte();

	if( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		MVD_Destroy( mvd, "Bad unicast clientNum" );
	}

	last = msg_read.readcount + length;
	if( last > msg_read.cursize ) {
		MVD_Destroy( mvd, "Read past end of message" );
	}

	player = &mvd->players[clientNum];

	gotLayout = qfalse;

	// Attempt to parse the datagram and find custom configstrings,
    // layouts, etc. Give up as soon as unknown command byte is encountered.
	while( 1 ) {
		if( msg_read.readcount > last ) {
		    MVD_Destroy( mvd, "Read past end of unicast" );
		}

		if( msg_read.readcount == last ) {
			break;
		}

        c = MSG_ReadByte();

        if( mvd_debug->integer > 1 ) {
            Com_Printf( "%s\n", MVD_ServerCommandString( c ) );
        }

		switch( c ) {
		case svc_layout:
			s = MSG_ReadString();
            if( !player->layout ) {
                player->layout = MVD_Malloc( MAX_STRING_CHARS );
            }
			Q_strncpyz( player->layout, s, MAX_STRING_CHARS );
			gotLayout = qtrue;
			break;
		case svc_configstring:
			i = MSG_ReadShort();
			s = MSG_ReadString();
			if( i < 0 || i >= MAX_CONFIGSTRINGS ) {
		        MVD_Destroy( mvd, "bad configstring index" );
			}
            length = strlen( s );
            if( length > MAX_QPATH - 1 ) {
                Com_WPrintf( "Private configstring %d for player %d "
                        "is %d chars long, ignored.\n", i, clientNum, length );
            } else {
                for( cs = player->configstrings; cs; cs = cs->next ) {
                    if( cs->index == i ) {
                        break;
                    }
                }
                if( !cs ) {
                    cs = MVD_Malloc( sizeof( *cs ) + MAX_QPATH - 1 );
                    cs->index = i;
                    cs->next = player->configstrings;
                    player->configstrings = cs;
                }
                strcpy( cs->string, s );
            }
            if( mvd_debug->integer > 1 ) {
                Com_Printf( "  index:%d string: %s\n", i, s );
            }
			MSG_WriteByte( svc_configstring );
			MSG_WriteShort( i );
			MSG_WriteString( s );
			break;
		case svc_print:
			i = MSG_ReadByte();
			s = MSG_ReadString();
			MSG_WriteByte( svc_print );
			MSG_WriteByte( i );
			MSG_WriteString( s );
			break;
		case svc_stufftext:
			s = MSG_ReadString();
			MSG_WriteByte( svc_stufftext );
			MSG_WriteString( s );
			break;
		default:
			// copy remaining data and stop
			MSG_WriteByte( c );
            MSG_WriteData( msg_read.data + msg_read.readcount,
                last - msg_read.readcount );
			msg_read.readcount = last;
			goto breakOut;
		}
	}

breakOut:
	flags = 0;
    minstate = cs_spawned;
	if( op == mvd_unicast_r ) {
		flags |= MSG_RELIABLE;
        minstate = cs_primed;
	}

	// send to all relevant clients
	wantLayout = qfalse;
    LIST_FOR_EACH( udpClient_t, client, &mvd->udpClients, entry ) {
        if( client->cl->state < minstate ) {
            continue;
        }
		if( client->scoreboard == SBOARD_SCORES &&
            clientNum == mvd->clientNum )
        {
			wantLayout = qtrue;
		}
		if( !client->following ) {
			if( clientNum == mvd->clientNum ) {
				SV_ClientAddMessage( client->cl, flags );
			}
			continue;
		}
		if( client->followClientNum == clientNum ) {
			SV_ClientAddMessage( client->cl, flags );
			if( client->scoreboard == SBOARD_FOLLOW ) {
				wantLayout = qtrue;
			}
		}
	}

	SZ_Clear( &msg_write );

	if( !gotLayout || !wantLayout ) {
		return;
	}

	MSG_WriteByte( svc_layout );
	MSG_WriteString( player->layout );

    LIST_FOR_EACH( udpClient_t, client, &mvd->udpClients, entry ) {
        if( client->cl->state < minstate ) {
            continue;
        }
		if( client->scoreboard == SBOARD_SCORES &&
            clientNum == mvd->clientNum )
        {
			SV_ClientAddMessage( client->cl, flags );
			continue;
		}
		if( client->followClientNum == clientNum &&
			client->scoreboard == SBOARD_FOLLOW )
        {
			SV_ClientAddMessage( client->cl, flags );
		}
	}

	SZ_Clear( &msg_write );
}

static qboolean MVD_EdictPV( mvd_t *mvd, entityStateEx_t *ent, byte *mask ) {
    int i, l;

    if( ent->num_clusters == -1 ) {	
        // too many leafs for individual check, go by headnode
        return CM_HeadnodeVisible( ent->headnode, mask );
    }

    // check individual leafs
    for( i = 0; i < ent->num_clusters; i++ ) {
        l = ent->clusternums[i];
        if( Q_IsBitSet( mask, l ) ) {
            return qtrue;
        }
    }
    return qfalse;		// not visible
}

/*
MVD_ParseSound

Entity positioned sounds need special handling since origins need to be
explicitly specified on entities out of client PVS, and not all clients
are able to postition sounds on BSP models properly.
*/
static void MVD_ParseSound( mvd_t *mvd, int extrabits ) {
    vec3_t  origin, clientorg;
    int 	channel, entnum, sendchan;
    int 	flags, index;
    int 	volume = 0, attenuation = 0, offset = 0;
    entityStateEx_t *entity = NULL;
    udpClient_t *client;
    int i, j;
    cleaf_t *leaf;
    int area, cluster;
    byte *mask;
    mvdFrame_t *frame;
    int modelindex;
    cmodel_t *cm;

	flags = MSG_ReadByte();
	index = MSG_ReadByte();

    if( flags & SND_VOLUME )
		volume = MSG_ReadByte();
    if( flags & SND_ATTENUATION )
		attenuation = MSG_ReadByte();
    if( flags & SND_OFFSET )
		offset = MSG_ReadByte();

	// entity relative
	sendchan = MSG_ReadShort(); 
	entnum = sendchan >> 3;
	if( entnum < 0 || entnum >= MAX_EDICTS )
		MVD_Destroy( mvd, "MVD_ParseSound: bad entnum %d",  entnum );
	channel = sendchan & 7;

    // find this entity in frame
	frame = &mvd->frames[mvd->framenum & MVD_UPDATE_MASK];
	for( i = 0; i < frame->numEntities; i++ ) {
		j = ( frame->firstEntity + i ) & MVD_ENTITIES_MASK;
		entity = &mvd->entityStates[j];
        if( entity->s.number == entnum ) {
            break;
        }
    }
    if( i == frame->numEntities ) {
        Com_WPrintf( "MVD_ParseSound: entity %d not found in frame\n", entnum );
        return;
    }

    // use the entity origin unless it is a bmodel
    if( entity->s.solid == 31 ) {
		modelindex = entity->s.modelindex;
		if( modelindex < 1 || modelindex > mvd->cm.cache->numcmodels ) {
			Com_WPrintf( "MVD_PaseSound: entity %d has bad inline model index %d\n",
				entity->s.number, modelindex );
			return;
		}
		cm = &mvd->cm.cache->cmodels[ modelindex - 1 ];
        VectorAvg( cm->mins, cm->maxs, origin );
        VectorAdd( entity->s.origin, origin, origin );
    } else {
        VectorCopy( entity->s.origin, origin );
    }

    LIST_FOR_EACH( udpClient_t, client, &mvd->udpClients, entry ) {
		// do not send sounds to connecting clients
		if( client->cl->state != cs_spawned || client->cl->download || client->cl->nodata ) {
			continue; 
		}

        // default client doesn't know that bmodels have weird origins
        if( entity->s.solid == 31 && client->cl->protocol == PROTOCOL_VERSION_DEFAULT ) {
            flags |= SND_POS;
        }

        //if( entity != client->edict ) {
            // get client viewpos
            VectorMA( client->ps.viewoffset, 0.125f,
                client->ps.pmove.origin, clientorg );

            // PHS cull this sound
            if( ( extrabits & 1 ) == 0 ) {
                leaf = CM_PointLeaf( &mvd->cm, clientorg );
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
                if( !MVD_EdictPV( mvd, entity, mask ) ) {
                    continue; // not in PHS
                }
            }

            // check if position needs to be explicitly sent
            if( ( flags & SND_POS ) == 0 ) {
                mask = CM_FatPVS( &mvd->cm, clientorg );
                if( !MVD_EdictPV( mvd, entity, mask ) ) {
                    flags |= SND_POS;   // not in PVS
                }
            }
//        }

        MSG_WriteByte( svc_sound );
        MSG_WriteByte( flags );
        MSG_WriteByte( index );

        if( flags & SND_VOLUME )
            MSG_WriteByte( volume );
        if( flags & SND_ATTENUATION )
            MSG_WriteByte( attenuation );
        if( flags & SND_OFFSET )
            MSG_WriteByte( offset );

        MSG_WriteShort( sendchan );

        if( flags & SND_POS )
            MSG_WritePos( origin );

        flags &= ~SND_POS;

	    if( extrabits & 2 ) {
		    SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );
        } else {
		    SV_ClientAddMessage( client->cl, MSG_CLEAR );
        }
    }
}

static void MVD_ParseConfigstring( mvd_t *mvd ) {
	int index, length;
	char *string;
	udpClient_t *client;

	index = MSG_ReadShort();

	if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		MVD_Destroy( mvd, "Bad configstring index: %d", index );
	}

	string = MSG_ReadStringLength( &length );

	if( MAX_QPATH * index + length >= sizeof( mvd->configstrings ) ) {
		MVD_Destroy( mvd, "Oversize configstring" );
	}

	if( !strcmp( mvd->configstrings[index], string ) ) {
		return;
	}

	strcpy( mvd->configstrings[index], string );

	MSG_WriteByte( svc_configstring );
	MSG_WriteShort( index );
	MSG_WriteString( string );
	
    // broadcast configstring change
    LIST_FOR_EACH( udpClient_t, client, &mvd->udpClients, entry ) {
        if( client->cl->state < cs_primed ) {
            continue;
        }
		SV_ClientAddMessage( client->cl, MSG_RELIABLE );
	}

	SZ_Clear( &msg_write );
}

/*
Fix origin and angles on each player entity by
extracting data from player state.
*/
static void MVD_FixEntityStates( mvd_t *mvd, mvdFrame_t *frame ) {
	entityStateEx_t *currStates[MAX_CLIENTS];
	entityStateEx_t *state;
	player_state_t *ps;
	int i, j;
    int playerNum;

	for( i = 0; i < MAX_CLIENTS; i++ ) {
		currStates[i] = NULL;
	}

	for( i = 0; i < frame->numEntities; i++ ) {
		j = ( frame->firstEntity + i ) & MVD_ENTITIES_MASK;
		state = &mvd->entityStates[j];
		
		if( state->s.number < 1 || state->s.number >= MAX_EDICTS ) {
			MVD_Destroy( mvd, "Bad entity number" );
		}
		
		if( state->s.number <= MAX_CLIENTS ) {
			currStates[ state->s.number - 1 ] = state;
		}
	}

    for( i = 0; i < frame->numPlayers; i++ ) {
        j = ( frame->firstPlayer + i ) & MVD_PLAYERS_MASK;
        ps = &mvd->playerStates[j];
        playerNum = ps->pmove.pm_flags;
   
        if( ps->pmove.pm_type >= PM_DEAD ) {
            continue;
        }
        state = currStates[playerNum];
        if( !state ) {
            continue; // not present in this frame
        }

		VectorCopy( state->s.origin, state->s.old_origin );

        VectorScale( ps->pmove.origin, 0.125f, state->s.origin );
        VectorCopy( ps->viewangles, state->s.angles );

        if( state->s.angles[PITCH] > 180 ) {
            state->s.angles[PITCH] -= 360;
        }

        state->s.angles[PITCH] = state->s.angles[PITCH] / 3;

        MVD_LinkEntity( mvd, state );
    }
}

/*
==================
MVD_ParseDeltaEntity
==================
*/
#define RELINK_MASK	    (U_MODEL|U_ORIGIN1|U_ORIGIN2|U_ORIGIN3|U_SOLID)

static inline void MVD_ParseDeltaEntity( mvd_t             *mvd,
                                         mvdFrame_t        *frame,
                                         int               newnum,
                                         entityStateEx_t   *old,
                                         int               bits )
{
	entityStateEx_t	*state;
	int i;

	i = mvd->nextEntityStates & MVD_ENTITIES_MASK;
	state = &mvd->entityStates[i];
	mvd->nextEntityStates++;
	frame->numEntities++;

	if( mvd_shownet->integer > 2 ) {
		MSG_ShowDeltaEntityBits( bits );
	}

	MSG_ParseDeltaEntity( &old->s, &state->s, newnum, bits );

	if( newnum > mvd->maxclients ) {
		if( !old || !old->linked || ( bits & RELINK_MASK ) ) {
			MVD_LinkEntity( mvd, state );
		} else {
			state->linked = qtrue;
			state->num_clusters = old->num_clusters;
			state->headnode = old->headnode;
			state->areanum = old->areanum;
			state->areanum2 = old->areanum2;
			for( i = 0; i < state->num_clusters; i++ ) {
				state->clusternums[i] = old->clusternums[i];
			}
		}
	}
}

/*
==================
MVD_ParsePacketEntities
==================
*/
static void MVD_ParsePacketEntities( mvd_t      *mvd,
                                     mvdFrame_t *oldframe,
								     mvdFrame_t *frame )
{
	int			newnum;
	int			bits;
	entityStateEx_t	*oldstate, *base;
	int			oldindex, oldnum;
	int i;

	frame->firstEntity = mvd->nextEntityStates;
	frame->numEntities = 0;

	// delta from the entities present in oldframe
	oldindex = 0;
	oldstate = NULL;
	if( !oldframe ) {
		oldnum = 99999;
	} else {
		if( oldindex >= oldframe->numEntities ) {
			oldnum = 99999;
		} else {
			i = oldframe->firstEntity + oldindex;
			oldstate = &mvd->entityStates[i & MVD_ENTITIES_MASK];
			oldnum = oldstate->s.number;
		}
	}

	while( 1 ) {
		newnum = MSG_ParseEntityBits( &bits );
		if( msg_read.readcount > msg_read.cursize ) {
			MVD_Destroy( mvd, "Read past end of message" );
		}

		if( newnum < 0 || newnum >= MAX_EDICTS ) {
			MVD_Destroy( mvd, "Bad packetentity number: %d", newnum );
		}

		if( !newnum ) {
			break;
		}

		while( oldnum < newnum ) {
			// one or more entities from the old packet are unchanged
			if( mvd_shownet->integer > 2 ) {
				Com_Printf( "   unchanged: %i\n", oldnum );
			}
			MVD_ParseDeltaEntity( mvd, frame, oldnum, oldstate, 0 );
			
			oldindex++;

			if( oldindex >= oldframe->numEntities ) {
				oldnum = 99999;
			} else {
				i = oldframe->firstEntity + oldindex;
				oldstate = &mvd->entityStates[i & MVD_ENTITIES_MASK];
				oldnum = oldstate->s.number;
			}
		}

		if( bits & U_REMOVE ) {	
			// the entity present in oldframe is not in the current frame
			if( mvd_shownet->integer > 2 ) {
				Com_Printf( "   remove: %i\n", newnum );
			}
			if( oldnum != newnum ) {
				Com_DPrintf( "U_REMOVE: oldnum != newnum\n" );
			}
			if( !oldframe ) {
			    MVD_Destroy( mvd, "U_REMOVE: NULL oldframe" );
			}

			oldindex++;

			if( oldindex >= oldframe->numEntities ) {
				oldnum = 99999;
			} else {
				i = oldframe->firstEntity + oldindex;
				oldstate = &mvd->entityStates[i & MVD_ENTITIES_MASK];
				oldnum = oldstate->s.number;
			}
			continue;
		}

		if( oldnum == newnum ) {	
			// delta from previous state
			if( mvd_shownet->integer > 2 ) {
				Com_Printf( "   delta: %i ", newnum );
			}
			MVD_ParseDeltaEntity( mvd, frame, newnum, oldstate, bits );
			if( mvd_shownet->integer > 2 ) {
				Com_Printf( "\n" );
			}

			oldindex++;

			if( oldindex >= oldframe->numEntities ) {
				oldnum = 99999;
			} else {
				i = oldframe->firstEntity + oldindex;
				oldstate = &mvd->entityStates[i & MVD_ENTITIES_MASK];
				oldnum = oldstate->s.number;
			}
			continue;
		}

		if( oldnum > newnum ) {	
			// delta from baseline
			if( mvd_shownet->integer > 2 ) {
				Com_Printf( "   baseline: %i ", newnum );
			}
			base = mvd->baselines[newnum >> SV_BASELINES_SHIFT];
			if( base ) {
				base += newnum & SV_BASELINES_MASK;
			}
			MVD_ParseDeltaEntity( mvd, frame, newnum, base, bits );
			if( mvd_shownet->integer > 2 ) {
				Com_Printf( "\n" );
			}
			continue;
		}

	}

	// any remaining entities in the old frame are copied over
	while( oldnum != 99999 ) {	
		// one or more entities from the old packet are unchanged
		if( mvd_shownet->integer > 2 ) {
			Com_Printf( "   unchanged: %i\n", oldnum );
		}
		MVD_ParseDeltaEntity( mvd, frame, oldnum, oldstate, 0 );
		
		oldindex++;

		if( oldindex >= oldframe->numEntities ) {
			oldnum = 99999;
		} else {
			i = oldframe->firstEntity + oldindex;
			oldstate = &mvd->entityStates[i & MVD_ENTITIES_MASK];
			oldnum = oldstate->s.number;
		}
	}
}

/*
==================
MVD_ParseDeltaPlayer
==================
*/
static inline void MVD_ParseDeltaPlayer( mvd_t             *mvd,
                                         mvdFrame_t        *frame,
                                         int               newnum,
								         player_state_t    *old,
                                         int               bits )
{
	player_state_t	*state;
	int i;

	i = mvd->nextPlayerStates & MVD_PLAYERS_MASK;
	state = &mvd->playerStates[i];
	mvd->nextPlayerStates++;
	frame->numPlayers++;

	if( mvd_shownet->integer > 2 ) {
		MSG_ShowDeltaPlayerstateBits_Packet( bits );
	}
	MSG_ParseDeltaPlayerstate_Packet( old, state, bits );

    // save player number
	state->pmove.pm_flags = newnum;
}

/*
==================
MVD_ParsePacketPlayers
==================
*/
static void MVD_ParsePacketPlayers( mvd_t       *mvd,
                                    mvdFrame_t  *oldframe,
                                    mvdFrame_t  *frame )
{
	int			newnum;
	int			bits;
	player_state_t	*oldstate;
	int			oldindex, oldnum;
	int			i;

	frame->firstPlayer = mvd->nextPlayerStates;
	frame->numPlayers = 0;

	// delta from the players present in oldframe
	oldindex = 0;
	oldstate = NULL;
	if( !oldframe ) {
		oldnum = 99999;
	} else {
		if( oldindex >= oldframe->numPlayers ) {
			oldnum = 99999;
		} else {
			i = oldframe->firstPlayer + oldindex;
			oldstate = &mvd->playerStates[i & MVD_PLAYERS_MASK];
			oldnum = PPS_NUM( oldstate );
		}
	}

	while( 1 ) {
		newnum = MSG_ReadByte();
		if( msg_read.readcount > msg_read.cursize ) {
			MVD_Destroy( mvd, "Read past end of message" );
		}

		if( newnum < 0 || newnum >= MAX_CLIENTS ) {
			MVD_Destroy( mvd, "Bad packetplayer number: %d", newnum );
		}

		if( newnum == CLIENTNUM_NONE ) {
			break;
		}

		bits = MSG_ReadShort();

		while( oldnum < newnum ) {
			// one or more players from the old packet are unchanged
			if( mvd_shownet->integer > 2 ) {
				Com_Printf( "   unchanged: %i\n", oldnum );
			}
			MVD_ParseDeltaPlayer( mvd, frame, oldnum, oldstate, 0 );
			
			oldindex++;

			if( oldindex >= oldframe->numPlayers ) {
				oldnum = 99999;
			} else {
				i = oldframe->firstPlayer + oldindex;
				oldstate = &mvd->playerStates[i & MVD_PLAYERS_MASK];
				oldnum = PPS_NUM( oldstate );
			}
		}

		if( bits & PPS_REMOVE ) {	
			// the player present in oldframe is not in the current frame
			if( mvd_shownet->integer > 2 ) {
				Com_Printf( "   remove: %i\n", newnum );
			}
			if( oldnum != newnum ) {
				Com_DPrintf( "PPS_REMOVE: oldnum != newnum\n" );
			}
			if( !oldframe ) {
			    MVD_Destroy( mvd, "PPS_REMOVE: NULL oldframe" );
			}

			oldindex++;

			if( oldindex >= oldframe->numPlayers ) {
				oldnum = 99999;
			} else {
				i = oldframe->firstPlayer + oldindex;
				oldstate = &mvd->playerStates[i & MVD_PLAYERS_MASK];
				oldnum = PPS_NUM( oldstate );
			}
			continue;
		}

		if( oldnum == newnum ) {	
			// delta from previous state
			if( mvd_shownet->integer > 2 ) {
				Com_Printf( "   delta: %i ", newnum );
			}
			MVD_ParseDeltaPlayer( mvd, frame, newnum, oldstate, bits );
			if( mvd_shownet->integer > 2 ) {
				Com_Printf( "\n" );
			}

			oldindex++;

			if( oldindex >= oldframe->numPlayers ) {
				oldnum = 99999;
			} else {
				i = oldframe->firstPlayer + oldindex;
				oldstate = &mvd->playerStates[i & MVD_PLAYERS_MASK];
				oldnum = PPS_NUM( oldstate );
			}
			continue;
		}

		if( oldnum > newnum ) {	
			// delta from baseline
			if( mvd_shownet->integer > 2 ) {
				Com_Printf( "   baseline: %i ", newnum );
			}
			MVD_ParseDeltaPlayer( mvd, frame, newnum, NULL, bits );
			if( mvd_shownet->integer > 2 ) {
				Com_Printf( "\n" );
			}
			continue;
		}

	}

	while( oldnum != 99999 ) {	
		if( mvd_shownet->integer > 2 ) {
			Com_Printf( "   unchanged: %i\n", oldnum );
		}
		MVD_ParseDeltaPlayer( mvd, frame, oldnum, oldstate, 0 );
		
		oldindex++;

		if( oldindex >= oldframe->numPlayers ) {
			oldnum = 99999;
		} else {
			i = oldframe->firstPlayer + oldindex;
			oldstate = &mvd->playerStates[i & MVD_PLAYERS_MASK];
			oldnum = PPS_NUM( oldstate );
		}
	}
}

/*
================
MVD_ParseFrame
================
*/
static void MVD_ParseFrame( mvd_t *mvd, qboolean delta ) {
	mvdFrame_t *oldframe, *frame;
	int length;

    // allocate new frame
	frame = &mvd->frames[++mvd->framenum & MVD_UPDATE_MASK];
	frame->serverFrame = MSG_ReadLong();
    frame->number = mvd->framenum;

    if( delta ) {
		oldframe = &mvd->frames[( mvd->framenum - 1 ) & MVD_UPDATE_MASK];
		if( oldframe->number != mvd->framenum - 1 ) {
			MVD_Destroy( mvd, "Delta from invalid frame" );
        }
	} else {
        oldframe = NULL; // uncompressed frame
	}

	// read portalbits
	length = MSG_ReadByte();
	if( length ) {
		if( length < 0 || msg_read.readcount + length > msg_read.cursize ) {
            MVD_Destroy( mvd, "Read past end of message" );
		}
		if( length > MAX_MAP_AREAS/8 ) {
            MVD_Destroy( mvd, "Bad portalbits length" );
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

	MVD_ParsePacketPlayers( mvd, oldframe, frame );

	if( mvd_shownet->integer > 1 ) {
		Com_Printf( "%3i:packetentities\n", msg_read.readcount - 1 );
	}

	MVD_ParsePacketEntities( mvd, oldframe, frame );

	if( mvd_shownet->integer > 1 ) {
		Com_Printf( "%3i: frame:%i  delta:%i\n",
			msg_read.readcount - 1, mvd->framenum, delta );
	}

	MVD_FixEntityStates( mvd, frame );
}

static void MVD_ParseServerData( mvd_t *mvd ) {
	int protocol, clientNum;
    int length, index;
	char *gamedir, *string;
	entityStateEx_t *base, **chunk;
	int entnum, bits;
	uint32 checksum;

    // clear the leftover from previous level
    MVD_ClearState( mvd );

	// parse major protocol version
	protocol = MSG_ReadLong();
	if( protocol != PROTOCOL_VERSION_MVD ) {
        MVD_Destroy( mvd, "Unsupported protocol: %d", protocol );
    }

    // parse minor protocol version
    protocol = MSG_ReadShort();
    if( protocol != PROTOCOL_VERSION_MVD_MINOR ) {
        MVD_Destroy( mvd, "MVD protocol version mismatch: %d instead of %d",
            protocol, PROTOCOL_VERSION_MVD_MINOR );
    }

	mvd->servercount = MSG_ReadLong();
	gamedir = MSG_ReadString();
	clientNum = MSG_ReadShort();
    if( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
        MVD_Destroy( mvd, "Invalid client num: %d", clientNum );
    }
    mvd->clientNum = clientNum;
	
	// change gamedir unless playing a demo
    Q_strncpyz( mvd->gamedir, gamedir, sizeof( mvd->gamedir ) );
	if( !mvd->demoplayback ) {
		Cvar_UserSet( "game", gamedir );
        if( FS_NeedRestart() ) {
            FS_Restart();
        }
	}

    // parse configstrings
    while( 1 ) {
        index = MSG_ReadShort();
        if( index == MAX_CONFIGSTRINGS ) {
            break;
        }

        if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
            MVD_Destroy( mvd, "Bad configstring index: %d", index );
        }

        string = MSG_ReadStringLength( &length );

        if( MAX_QPATH * index + length > sizeof( mvd->configstrings ) - 1 ) {
            MVD_Destroy( mvd, "Oversize configstring" );
        }

        strcpy( mvd->configstrings[index], string );

        if( msg_read.readcount > msg_read.cursize ) {
            MVD_Destroy( mvd, "Read past end of message" );
        }
    }

    mvd->maxclients = atoi( mvd->configstrings[CS_MAXCLIENTS] );

    string = mvd->configstrings[ CS_MODELS + 1]; 
    length = strlen( string );
    if( length <= 9 ) {
        MVD_Destroy( mvd, "Bad world model: %s", string );
    }
    strcpy( mvd->mapname, string + 5 ); // skip "maps/"
    mvd->mapname[length - 9] = 0; // cut off ".bsp"

	// load the world model (we are only interesed in visibility info)
    Com_Printf( "Loading %s...\n", string );
    CM_LoadMap( &mvd->cm, string, CM_LOAD_VISONLY, &checksum );

    if( checksum != atoi( mvd->configstrings[CS_MAPCHECKSUM] ) ) {
        MVD_Destroy( mvd, "Local map version differs from server" );
    }

    // get the spawn point for spectators
    MVD_ParseEntityString( mvd );

    // parse baselines
    while( 1 ) {
        entnum = MSG_ParseEntityBits( &bits );
        if( !entnum ) {
            break;
        }

        if( entnum < 0 || entnum >= MAX_EDICTS ) {
            MVD_Destroy( mvd, "Bad baseline number: %d", entnum );
        }
        
        chunk = &mvd->baselines[entnum >> SV_BASELINES_SHIFT];
        if( *chunk == NULL ) {
            *chunk = MVD_Mallocz( sizeof( *base ) * SV_BASELINES_PER_CHUNK );
        }

        base = *chunk + ( entnum & SV_BASELINES_MASK );
        MSG_ParseDeltaEntity( NULL, &base->s, entnum, bits );

        MVD_LinkEntity( mvd, base );

        if( msg_read.readcount > msg_read.cursize ) {
            MVD_Destroy( mvd, "Read past end of message" );
        }
    }

    // parse uncompressed frame
    if( MSG_ReadByte() != mvd_frame_nodelta ) {
        MVD_Destroy( mvd, "Expected uncompressed frame" );
    }
    MVD_ParseFrame( mvd, qfalse );

    if( mvd->state < MVD_WAITING ) {
        List_Append( &mvd_ready, &mvd->ready );
        mvd->state = mvd->demoplayback ? MVD_READING : MVD_WAITING;
    }

    MVD_ChangeLevel( mvd );
}

static qboolean MVD_ParseMessage( mvd_t *mvd, fifo_t *fifo ) {
    uint16      msglen;
    uint32      magic;
    byte        *data;
    int         length;
	int			cmd, extrabits;

    // parse magic
    if( mvd->state == MVD_CHECKING ) {
        if( !FIFO_Read( fifo, &magic, 4 ) ) {
            return qfalse;
        }
        if( magic != MVD_MAGIC ) {
            MVD_Destroy( mvd, "Not a MVD stream" );
        }
        mvd->state = MVD_PREPARING;
    }

    // parse msglen
    msglen = mvd->msglen;
    if( !msglen ) {
        if( !FIFO_Read( fifo, &msglen, 2 ) ) {
            return qfalse;
        }
        if( !msglen ) {
            MVD_Destroy( mvd, "End of MVD stream reached" );
        }
        msglen = LittleShort( msglen );
        if( msglen > MAX_MSGLEN ) {
            MVD_Destroy( mvd, "Invalid MVD message length: %d bytes", msglen );
        }
        mvd->msglen = msglen;
    }

    // first, try to read in a single block
    data = FIFO_Peek( fifo, &length );
    if( length < msglen ) {
        if( !FIFO_Read( fifo, msg_read_buffer, msglen ) ) {
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
            MVD_Destroy( mvd, "Read past end of message" );
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
			MVD_ParseFrame( mvd, qtrue );
			break;
		case mvd_frame_nodelta:
			MVD_ParseFrame( mvd, qfalse );
			break;
		case mvd_sound:
			MVD_ParseSound( mvd, extrabits );
			break;
		default:
            MVD_Destroy( mvd, "Illegible command at %d: %d",
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
            MVD_Destroy( mvd, "inflate() failed: %s", mvd->z.msg );
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
    if( mvd->state == MVD_DISCONNECTED ) {
        MVD_Destroy( mvd, "MVD stream was truncated" );
    }
    return qfalse;
}

