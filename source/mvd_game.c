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
// mvd_game.c
//

#include "sv_local.h"
#include "mvd_local.h"
#include <setjmp.h>

#define FOR_EACH_PLAYER( mvd, ps, frame ) \
    for( i = 0, ps = &(mvd)->playerStates[(frame)->firstPlayer & MVD_PLAYERS_MASK]; \
        i < (frame)->numPlayers; \
        i++, ps = &(mvd)->playerStates[((frame)->firstPlayer + i ) & MVD_PLAYERS_MASK] )

static cvar_t	*mvd_motd;
static cvar_t	*mvd_admin_password;
static cvar_t	*mvd_flood_msgs;
static cvar_t	*mvd_flood_persecond;
static cvar_t	*mvd_flood_waitdelay;
static cvar_t	*mvd_flood_mute;
static cvar_t	*mvd_custom_fov;

udpClient_t     *mvd_clients;

extern jmp_buf     mvd_jmpbuf;

/* called initially at ClientBegin */
static void MVD_SetValidPos( udpClient_t *client ) {
	player_state_t *ps;
	entityStateEx_t *ent;
    mvd_t *mvd = client->mvd;
    mvdFrame_t *frame = &mvd->frames[mvd->framenum & MVD_UPDATE_MASK];
	int i, j;

	ps = &client->ps;
	memset( ps, 0, sizeof( *ps ) );

	if( mvd->spawnSet ) {
		VectorScale( mvd->spawnOrigin, 8, ps->pmove.origin );
		VectorCopy( mvd->spawnAngles, ps->viewangles );
	} else {
		for( i = 0; i < frame->numEntities; i++ ) {
			j = ( frame->firstEntity + i ) & MVD_ENTITIES_MASK;
			ent = &mvd->entityStates[j];
			if( ent->s.solid != 31 ) {
				VectorScale( ent->s.origin, 8, ps->pmove.origin );
				break;
			}
		}
		if( i == frame->numEntities ) {
			Com_WPrintf( "Don't know where to spawn %s\n", client->cl->name );
		}
	}

	ps->viewangles[ROLL] = 0;
	for( i = 0; i < 3; i++ ) {
		ps->pmove.delta_angles[i] = ANGLE2SHORT( ps->viewangles[i] ) -
			client->lastcmd.angles[i];
	}

	ps->fov = client->fov; 
	ps->pmove.pm_flags = client->pmflags;
	ps->pmove.pm_type = mvd == &mvd_waitingRoom ? PM_FREEZE : PM_SPECTATOR;

    client->clientNum = CLIENTNUM_NONE;
	client->savedClientNum = CLIENTNUM_NONE;
	client->followClientNum = CLIENTNUM_NONE;
	client->following = qfalse;
}

static void MVD_StartObserving( udpClient_t *client ) {
	player_state_t *ps;
    player_state_t *state;
	mvdPlayer_t *player;
	int i;
	int savedLayouts;
    mvdConfigstring_t *cs;
    mvd_t *mvd = client->mvd;
    mvdFrame_t *frame = &mvd->frames[mvd->framenum & MVD_UPDATE_MASK];

	ps = &client->ps;
	ps->viewangles[ROLL] = 0;
	for( i = 0; i < 3; i++ ) {
		ps->pmove.delta_angles[i] = ANGLE2SHORT( ps->viewangles[i] ) -
			client->lastcmd.angles[i];
	}

	VectorClear( ps->kick_angles );

	ps->fov = client->fov; 
	ps->blend[0] = 0;
	ps->blend[1] = 0;
	ps->blend[2] = 0;
	ps->blend[3] = 0;
	ps->pmove.pm_flags = client->pmflags;
	ps->pmove.pm_type = mvd == &mvd_waitingRoom ? PM_FREEZE : PM_SPECTATOR;
	ps->rdflags = 0;
	ps->gunindex = 0;

	if( client->scoreboard <= SBOARD_FOLLOW ) {
		savedLayouts = 0;
	} else {
		savedLayouts = ps->stats[STAT_LAYOUTS];
	}

    for( i = 0; i < MAX_STATS; i++ ) {
        ps->stats[i] = 0;
    }
    ps->stats[STAT_HEALTH] = 100;

    FOR_EACH_PLAYER( mvd, state, frame ) {
        if( PPS_NUM( state ) == mvd->clientNum ) {
            for( i = 0; i < MAX_STATS; i++ ) {
                ps->stats[i] = state->stats[i];
            }
            break;
        }
    }

    player = &mvd->players[mvd->clientNum];
    for( cs = player->configstrings; cs; cs = cs->next ) {
        MSG_WriteByte( svc_configstring );
        MSG_WriteShort( cs->index );
        MSG_WriteString( cs->string );
        SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );
    }

	ps->stats[STAT_LAYOUTS] = savedLayouts;

    client->clientNum = CLIENTNUM_NONE;
	client->savedClientNum = client->followClientNum;
	client->followClientNum = CLIENTNUM_NONE;
	client->following = qfalse;
	client->savedFollowing = qfalse;

	SV_ClientPrintf( client->cl, PRINT_MEDIUM,
        "[MVD] Switched to freefloat mode.\n" );
}

void MVD_UpdateFollower( udpClient_t *client, player_state_t *src ) {
	player_state_t *ps = &client->ps;

	memcpy( ps, src, sizeof( *ps ) );
	if( client->cl->protocol == PROTOCOL_VERSION_Q2PRO ) {
		if( client->cl->settings[CLS_LOCALFOV] ) {
			ps->fov = client->fov;
		}
	} else if( mvd_custom_fov->integer ) {
		ps->fov = client->fov;
	}
	ps->pmove.pm_flags &= ~PMF_TELEPORT_BIT;
	ps->pmove.pm_flags |= client->pmflags | PMF_NO_PREDICTION;
	ps->pmove.pm_type = PM_FREEZE;
    if( client->scoreboard > SBOARD_FOLLOW ) {
        ps->stats[STAT_LAYOUTS] = 1;
    } else if( client->scoreboard == SBOARD_NONE ) {
        ps->stats[STAT_LAYOUTS] = 0;
    }
    client->clientNum = PPS_NUM( src );
}

static void MVD_UpdateLayoutClients( udpClient_t *client ) {
	char layout[MAX_STRING_CHARS];
	char buffer[MAX_STRING_CHARS];
	char status[MAX_QPATH];
	int length, totalLength;
	udpClient_t *cl;
    mvd_t *mvd = client->mvd;
    char name[16];
    char *s;
	int y;

	totalLength = Com_sprintf( layout, sizeof( layout ),
		//"xl 32 yb -40 string2 \""APPLICATION" "VERSION"\" "
		//"yb -32 string http://q2pro.sf.net/ "
		"xv 0 yv 0 string2 Name "
		"xv 152 string2 Ping "
		"xv 208 string2 Status " );

	y = 8;
    LIST_FOR_EACH( udpClient_t, cl, &mvd->udpClients, entry ) {
		if( cl->cl->state < cs_spawned ) {
			strcpy( status, "connecting" );
        } else if( cl->following ) {
            s = mvd->configstrings[CS_PLAYERSKINS + cl->followClientNum];
            Q_strncpyz( name, s, sizeof( name ) );
            s = strchr( name, '\\' );
            if( s ) {
                *s = 0;
            }
            Com_sprintf( status, sizeof( status ), "-> %s", name );
        } else {
            strcpy( status, "observing" );
        }
		length = Com_sprintf( buffer, sizeof( buffer ),
			"xv 0 yv %d string \"%.16s\" "
			"xv 152 string %d "
			"xv 208 string \"%s\" ",
			y, cl->cl->name, cl->ping, status );
		if( totalLength + length > sizeof( layout ) - 1 ) {
            length = Com_sprintf( buffer, sizeof( buffer ),
                "xv 0 yv %d string <...>", y );
		    if( totalLength + length > sizeof( layout ) - 1 ) {
			    break;
            }
		}
		strcpy( layout + totalLength, buffer );
		totalLength += length;
		y += 8;
	}

	MSG_WriteByte( svc_layout );
	MSG_WriteString( layout );
				
	SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );

	client->ps.stats[STAT_LAYOUTS] = 1;
	client->layoutTime = sv.time + LAYOUT_MSEC;
}

static void MVD_UpdateLayoutChannels( udpClient_t *client ) {
	char layout[MAX_STRING_CHARS];
	char buffer[MAX_STRING_CHARS];
    mvd_t *mvd;
	int length, totalLength, cursor, y;

    strcpy( layout, "xv 32 yv 8 picn inventory "
        "xv 64 yv 32 string2 \"Channel       Map     CL\" "
        "yv 40 string \"------------- ------- --\" "
		"xl 32 yb -40 string2 \""APPLICATION" "VERSION"\" "
		"yb -32 string http://q2pro.sf.net/ " );
    totalLength = strlen( layout );

    cursor = List_Count( &mvd_ready );
    if( cursor ) {
        clamp( client->cursor, 0, cursor - 1 );

        y = 48;
        cursor = 0;
        LIST_FOR_EACH( mvd_t, mvd, &mvd_ready, ready ) {
            length = Com_sprintf( buffer, sizeof( buffer ),
                "xv 56 yv %d string \"%c%-13.13s %-7.7s %d%s\" ", y,
                cursor == client->cursor ? 0x8d : 0x20,
                mvd->name, mvd->mapname,
                List_Count( &mvd->udpClients ),
                mvd == client->mvd ? "+" : "" );
            if( totalLength + length > sizeof( layout ) - 1 ) {
                break;
            }
            strcpy( layout + totalLength, buffer );
            totalLength += length;
            y += 8;

            cursor++;
        }
    } else {
        client->cursor = 0;
        strcat( layout, "xv 56 yv 48 string \" <no channels>\" " );
    }

	MSG_WriteByte( svc_layout );
	MSG_WriteString( layout );
				
	SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );

	client->ps.stats[STAT_LAYOUTS] = 1;
	client->layoutTime = sv.time + LAYOUT_MSEC;
}

static void MVD_UpdateLayoutScores( udpClient_t *client ) {
    mvd_t *mvd = client->mvd;
	mvdPlayer_t *player= &mvd->players[mvd->clientNum];

	MSG_WriteByte( svc_layout );
	MSG_WriteString( player->layout );
	SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );

	client->ps.stats[STAT_LAYOUTS] = 1;
}

static void MVD_SetDefaultLayout( udpClient_t *client ) {
	if( client->mvd == &mvd_waitingRoom ) {
		client->scoreboard = SBOARD_CHANNELS;
        client->cursor = 0;
		MVD_UpdateLayoutChannels( client );
	} else {
		client->ps.stats[STAT_LAYOUTS] = 0;
		client->scoreboard = SBOARD_NONE;
	}
}

static void MVD_StartFollowing( udpClient_t *client, int playerNum ) {
	mvdPlayer_t *player;
	int i;
    char name[MAX_CLIENT_NAME];
    char *s;
	player_state_t *ps = NULL;
    mvd_t *mvd = client->mvd;
    mvdFrame_t *frame = &mvd->frames[mvd->framenum & MVD_UPDATE_MASK];
    mvdConfigstring_t *cs;

	// find the desired player in current frame
    FOR_EACH_PLAYER( mvd, ps, frame ) {
		if( PPS_NUM( ps ) == playerNum ) {
			break;
		}
	}

	if( i == frame->numPlayers ) {
		// pick up the first player
        FOR_EACH_PLAYER( mvd, ps, frame ) {
            if( PPS_NUM( ps ) != mvd->clientNum ) {
                break;
            }
        }
        if( i == frame->numPlayers ) {
            SV_ClientPrintf( client->cl, PRINT_MEDIUM,
                "[MVD] No players to follow.\n" );
            if( client->following ) {
                MVD_StartObserving( client );
            }
            return;
        }

		if( playerNum != CLIENTNUM_NONE ) {
			SV_ClientPrintf( client->cl, PRINT_MEDIUM,
				"[MVD] Player %d is not active.\n", playerNum );
		}
		//j = frame->firstPlayer % mvd.maxPlayerStates;
		//ps = &mvd.playerStates[j];
		playerNum = PPS_NUM( ps );
	}

	if( client->following && playerNum == client->followClientNum ) {
		return;
	}

	player = &mvd->players[playerNum];

	client->savedClientNum = client->followClientNum;
	client->followClientNum = playerNum;
	client->following = qtrue;

	client->pmflags ^= PMF_TELEPORT_BIT;
	MVD_UpdateFollower( client, ps );
    
    s = mvd->configstrings[CS_PLAYERSKINS + playerNum];
    Q_strncpyz( name, s, sizeof( name ) );
    s = strchr( name, '\\' );
    if( s ) {
        *s = 0;
    }

	SV_ClientPrintf( client->cl, PRINT_MEDIUM, "[MVD] Following %s.\n", name );

	// send delta configstrings
    for( cs = player->configstrings; cs; cs = cs->next ) {
        MSG_WriteByte( svc_configstring );
        MSG_WriteShort( cs->index );
        MSG_WriteString( cs->string );
        SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );
	}

	// send layout
	if( client->scoreboard == SBOARD_FOLLOW ) {
		MSG_WriteByte( svc_layout );
		MSG_WriteString( player->layout );
		SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );

		client->ps.stats[STAT_LAYOUTS] = ps->stats[STAT_LAYOUTS] & 1;
	}
}

static void MVD_FollowCycle( udpClient_t *client, int dir ) {
    mvd_t *mvd = client->mvd;
    mvdFrame_t *frame = &mvd->frames[mvd->framenum & MVD_UPDATE_MASK];
	player_state_t *ps;
	int i, j;
    int playerNum;

	if( !frame->numPlayers ) {
		return;
	}

	for( i = 0; i < frame->numPlayers; i++ ) {
		j = ( frame->firstPlayer + i ) & MVD_PLAYERS_MASK;
		ps = &mvd->playerStates[j];
        playerNum = PPS_NUM( ps );
		if( playerNum == client->followClientNum ) {
			i += dir;
			break;
		}
	}

	i %= frame->numPlayers;
	for( ; i < frame->numPlayers; i++ ) {
	    j = ( frame->firstPlayer + i ) & MVD_PLAYERS_MASK;
    	ps = &mvd->playerStates[j];
        playerNum = PPS_NUM( ps );
        if( playerNum != mvd->clientNum ) {
	        MVD_StartFollowing( client, playerNum );
        }
    }
}

static void MVD_UpdateClient( udpClient_t *client ) {
    mvd_t *mvd = client->mvd;
    mvdFrame_t *frame = &mvd->frames[mvd->framenum & MVD_UPDATE_MASK];
    int i;
    player_state_t *ps;

    if( !client->following ) {
        /* find the dummy MVD client */
        FOR_EACH_PLAYER( mvd, ps, frame ) {
            if( PPS_NUM( ps ) == mvd->clientNum ) {
                for( i = 0; i < STAT_LAYOUTS; i++ ) {
                    client->ps.stats[i] = ps->stats[i];
                }
                for( i = STAT_LAYOUTS + 1; i < MAX_STATS; i++ ) {
                    client->ps.stats[i] = ps->stats[i];
                }
                break;
            }
        }
        return;
    }

    /* find the player */
    FOR_EACH_PLAYER( mvd, ps, frame ) {
        if( PPS_NUM( ps ) == client->followClientNum ) {
            MVD_UpdateFollower( client, ps );
            return;
        }
    }

    /* player is no longer present */
    MVD_StartObserving( client );
    client->savedFollowing = qtrue;
}

void MVD_Update( mvd_t *mvd ) {
    udpClient_t *client;

	/* update clients */
    LIST_FOR_EACH( udpClient_t, client, &mvd->udpClients, entry ) {
		if( client->cl->state != cs_spawned ) {
            continue;
        }
        if( client->scoreboard == SBOARD_CLIENTS &&
            client->layoutTime < sv.time )
        {
            MVD_UpdateLayoutClients( client );
        }
    }
}


static void MVD_GameInit( void ) {
    mvd_t *mvd = &mvd_waitingRoom;
    edict_t *edicts;
	int i;

	Com_Printf( "----- MVD_GameInit -----\n" );

	mvd_motd = Cvar_Get( "mvd_motd", "Hello ${sv_client}!\\n"
        "This is an experimental MVD server!", 0 );
	mvd_admin_password = Cvar_Get( "mvd_admin_password", "", CVAR_PRIVATE );
	mvd_flood_msgs = Cvar_Get( "flood_msgs", "4", 0 );
	mvd_flood_persecond = Cvar_Get( "flood_persecond", "4", 0 ); // FIXME: rename this
	mvd_flood_waitdelay = Cvar_Get( "flood_waitdelay", "10", 0 );
	mvd_flood_mute = Cvar_Get( "flood_mute", "0", 0 );
	mvd_custom_fov = Cvar_Get( "mvd_custom_fov", "1", 0 );

    Z_TagReserve( ( sizeof( edict_t ) +
        sizeof( udpClient_t ) ) * sv_maxclients->integer +
        sizeof( edict_t ), TAG_GAME );
	mvd_clients = Z_ReservedAllocz( sizeof( udpClient_t ) *
        sv_maxclients->integer );
    edicts = Z_ReservedAllocz( sizeof( edict_t ) *
        ( sv_maxclients->integer + 1 ) );

	for( i = 0; i < sv_maxclients->integer; i++ ) {
		mvd_clients[i].cl = &svs.clientpool[i];
        edicts[i + 1].client = ( gclient_t * )&mvd_clients[i];
	}

    mvd_ge.edicts = edicts;
    mvd_ge.edict_size = sizeof( edict_t );
    mvd_ge.num_edicts = sv_maxclients->integer + 1;
    mvd_ge.max_edicts = sv_maxclients->integer + 1;

    strcpy( mvd->name, "Waiting Room" );
    List_Init( &mvd->udpClients );

    strcpy( mvd->configstrings[CS_NAME], "Waiting Room" );
    strcpy( mvd->configstrings[CS_MAXCLIENTS], "8" );
    strcpy( mvd->configstrings[CS_MAPCHECKSUM], "80717714" );
    strcpy( mvd->configstrings[CS_MODELS + 1], "maps/q2dm1.bsp" );

    mvd->spawnSet = qtrue;
    VectorSet( mvd->spawnOrigin, 984, 192, 784 );
    VectorSet( mvd->spawnAngles, 25, 72, 0 );
}

static void MVD_GameShutdown( void ) {
    mvd_t *mvd, *next;

	Com_Printf( "----- MVD_GameShutdown -----\n" );

    LIST_FOR_EACH_SAFE( mvd_t, mvd, next, &mvd_channels, entry ) {
        MVD_Disconnect( mvd );
        MVD_ClearState( mvd );
        MVD_Free( mvd );
    }

    List_Init( &mvd_channels );
    List_Init( &mvd_ready );

	if( mvd_clients ) {
		Z_Free( mvd_clients );
        mvd_clients = NULL;
	}

    mvd_ge.edicts = NULL;
    mvd_ge.edict_size = 0;
    mvd_ge.num_edicts = 0;
    mvd_ge.max_edicts = 0;

    Z_LeakTest( TAG_MVD );
}

static void MVD_GameSpawnEntities( const char *mapname, const char *entstring, const char *spawnpoint ) {
}
static void MVD_GameWriteGame( const char *filename, qboolean autosave ) {
}
static void MVD_GameReadGame( const char *filename ) {
}
static void MVD_GameWriteLevel( const char *filename ) {
}
static void MVD_GameReadLevel( const char *filename ) {
}

static qboolean MVD_GameClientConnect( edict_t *ent, char *userinfo ) {
	udpClient_t *client = EDICT_MVDCL( ent );
    mvd_t *mvd = client->mvd;
    int count;

    if( !mvd ) {
        count = List_Count( &mvd_ready );
        if( count == 1 ) {
            mvd = LIST_FIRST( mvd_t, &mvd_ready, ready );
        } else {
            mvd = &mvd_waitingRoom;
        }
        List_Append( &mvd->udpClients, &client->entry );
        client->mvd = mvd;
    }

	return qtrue;
}

static void MVD_GameClientBegin( edict_t *ent ) {
	udpClient_t *client = EDICT_MVDCL( ent );
	char *s;

	client->savedClientNum = CLIENTNUM_NONE;

	client->floodTime = 0;
	client->floodHead = 0;
	memset( &client->lastcmd, 0, sizeof( client->lastcmd ) );
	
	if( !client->connected ) {
		SV_BroadcastPrintf( PRINT_MEDIUM, "[MVD] %s entered the server\n",
            client->cl->name );
		client->connected = qtrue;
	}

	if( mvd_motd->string[0] ) {
		s = Cmd_MacroExpandString( mvd_motd->string, qfalse );
		if( !s ) {
			Com_WPrintf( "Macro expansion of mvd_motd failed\n" );
			Cvar_Set( "mvd_motd", "" );
		} else {
			s = Q_UnescapeString( s );
			MSG_WriteByte( svc_centerprint );
			MSG_WriteString( s );
			SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );
		}
	}

	MVD_SetValidPos( client );
    MVD_StartObserving( client );
	MVD_SetDefaultLayout( client );
}

static void MVD_GameClientUserinfoChanged( edict_t *ent, char *userinfo ) {
	udpClient_t *client = EDICT_MVDCL( ent );
	float fov;

	fov = atof( Info_ValueForKey( userinfo, "fov" ) );
	if( fov < 1 ) {
		fov = 90;
	} else if( fov > 160 ) {
		fov = 160;
	}
	client->fov = fov;
	if( !client->following ) {
		client->ps.fov = fov;
	}
}

static void MVD_GameClientDisconnect( edict_t *ent ) {
	udpClient_t *client = EDICT_MVDCL( ent );
    client_t *cl = client->cl;

    if( client->connected ) {
    	SV_BroadcastPrintf( PRINT_MEDIUM,
            "[MVD] %s left the server\n", cl->name );
    }

    List_Delete( &client->entry );

	memset( client, 0, sizeof( *client ) );
	client->cl = cl;
}

void MVD_SwitchChannel( udpClient_t *client, mvd_t *mvd ) {
    if( mvd == client->mvd ) {
        return; /* nothing to do */
    }

    List_Delete( &client->entry );
    List_Append( &mvd->udpClients, &client->entry );
    client->mvd = mvd;

    /* needs to reconnect */
    MSG_WriteByte( svc_stufftext );
    MSG_WriteString( "changing; reconnect\n" );
    SV_ClientReset( client->cl );
    SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );
}

static void MVD_StartAdmin( udpClient_t *client ) {
    client->admin = qtrue;
	SV_ClientPrintf( client->cl, PRINT_HIGH, "[MVD] Granted admin status.\n" );
}

static void MVD_StopAdmin( udpClient_t *client ) {
    client->admin = qfalse;
	SV_ClientPrintf( client->cl, PRINT_HIGH, "[MVD] Lost admin status.\n" );
}

static void MVD_GameClientCommand( edict_t *ent ) {
	udpClient_t *client = EDICT_MVDCL( ent );
    mvd_t *mvd = client->mvd;
    client_t *cl;
	char *cmd;
	int clientNum, cursor, i;

    cl = client->cl;
	cmd = Cmd_Argv( 0 );
	if( !strcmp( cmd, "!mvdadmin" ) ) {
		if( mvd->demoplayback ) {
			return;
		}
        if( client->admin ) {
			MVD_StopAdmin( client );
            return;
        }
		if( !NET_IsLocalAddress( &cl->netchan->remote_address ) ) {
			if( Cmd_Argc() < 2 ) {
				SV_ClientPrintf( cl, PRINT_HIGH, "Usage: %s <password>\n",
                    Cmd_Argv( 0 ) );
				return;
			}
			if( !mvd_admin_password->string[0]
					|| strcmp( mvd_admin_password->string, Cmd_Argv( 1 ) ) )
			{
				SV_ClientPrintf( cl, PRINT_HIGH, "[MVD] Invalid password.\n" );
				return;
			}
		}
		MVD_StartAdmin( client );
		return;
	}

	if( !strcmp( cmd, "say" ) || !strcmp( cmd, "say_team" ) ) {
        if( mvd_flood_mute->integer ) {
			SV_ClientPrintf( cl, PRINT_HIGH,
				"[MVD] Sorry, spectator chat is muted.\n" );
            return;
        }
		if( client->floodTime > sv.time ) {
			SV_ClientPrintf( cl, PRINT_HIGH,
				"[MVD] You can't talk for %d more seconds.\n",
				( client->floodTime - sv.time ) / 1000 );
			return;
		}
		Cvar_ClampInteger( mvd_flood_msgs, 0, FLOOD_SAMPLES - 1 );
		i = client->floodHead - mvd_flood_msgs->integer - 1;
		if( i >= 0 ) {
			Cvar_ClampValue( mvd_flood_persecond, 0, 60 );
			if( sv.time - client->floodSamples[i & FLOOD_MASK] <
				mvd_flood_persecond->value * 1000 )
			{
				Cvar_ClampValue( mvd_flood_waitdelay, 0, 60 );
				SV_ClientPrintf( cl, PRINT_HIGH,
					"[MVD] You can't talk for %d seconds.\n",
					mvd_flood_waitdelay->integer );
				client->floodTime = sv.time + mvd_flood_waitdelay->value * 1000;
				return;
			}
		}
		SV_BroadcastPrintf( PRINT_CHAT, "{%s}: %s\n", cl->name, Cmd_Args() );
		client->floodSamples[client->floodHead & FLOOD_MASK] = sv.time;
		client->floodHead++;
		return;
	}
	if( !strcmp( cmd, "playernext" ) ) {
		MVD_FollowCycle( client, 1 );
		return;
	}
	if( !strcmp( cmd, "playerprev" ) ) {
		MVD_FollowCycle( client, -1 );
		return;
	}
	if( !strcmp( cmd, "playertoggle" ) ) {
		MVD_StartFollowing( client, client->savedClientNum );
		return;
	}
	if( !strcmp( cmd, "follow" ) ) {
		if( Cmd_Argc() < 2 ) {
			if( !client->following ) {
				MVD_StartFollowing( client, client->savedClientNum );
			}
			return;
		}
		clientNum = atoi( Cmd_Argv( 1 ) );
		MVD_StartFollowing( client, clientNum );
		return;
	}
	if( !strcmp( cmd, "observe" ) ) {
		if( client->following ) {
			MVD_StartObserving( client );
		}
		return;
	}
	if( !strcmp( cmd, "inven" ) ) {
		if( client->scoreboard == SBOARD_CLIENTS ) {
			MVD_SetDefaultLayout( client );
		} else {
			client->scoreboard = SBOARD_CLIENTS;
			MVD_UpdateLayoutClients( client );
		}
		return;
	}
	if( !strcmp( cmd, "invnext" ) ) {
		if( client->scoreboard == SBOARD_CHANNELS ) {
            client->cursor++;
			MVD_UpdateLayoutChannels( client );
        }
        return;
    }
	if( !strcmp( cmd, "invprev" ) ) {
		if( client->scoreboard == SBOARD_CHANNELS ) {
            client->cursor--;
			MVD_UpdateLayoutChannels( client );
        }
        return;
    }
	if( !strcmp( cmd, "invuse" ) ) {
        if( client->scoreboard == SBOARD_CHANNELS ) {
            cursor = 0;
            LIST_FOR_EACH( mvd_t, mvd, &mvd_channels, entry ) {
                if( !mvd->framenum ) {
                    continue;
                }
                if( cursor == client->cursor ) {
                    MVD_SwitchChannel( client, mvd );
                    return;
                }
                cursor++;
            }
        }
        return;
    }
	if( !strcmp( cmd, "help" ) ) {
		if( client->scoreboard == SBOARD_SCORES ) {
			MVD_SetDefaultLayout( client );
		} else {
			client->scoreboard = SBOARD_SCORES;
			MVD_UpdateLayoutScores( client );
		}
		return;
	}

	if( !strcmp( cmd, "putaway" ) ) {
		MVD_SetDefaultLayout( client );
		return;
	}
	if( !strcmp( cmd, "channels" ) ) {
		client->scoreboard = SBOARD_CHANNELS;
		MVD_UpdateLayoutChannels( client );
		return;
	}
	
	SV_ClientPrintf( cl, PRINT_LOW, "[MVD] unknown command '%s'\n", cmd );
}

static trace_t MVD_Trace( vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end ) {
    trace_t trace;

    memset( &trace, 0, sizeof( trace ) );
    VectorCopy( end, trace.endpos );
    trace.fraction = 1;

    return trace;
}

static int MVD_PointContents( vec3_t p ) {
    return 0;
}

static void MVD_GameClientThink( edict_t *ent, usercmd_t *cmd ) {
	udpClient_t *client = EDICT_MVDCL( ent );
    usercmd_t *old = &client->lastcmd;
	pmove_t pm;

	if( !( old->buttons & BUTTON_ATTACK ) && ( cmd->buttons & BUTTON_ATTACK ) ) {
		if( client->following ) {
			MVD_StartObserving( client );
		} else {
			MVD_StartFollowing( client, client->savedClientNum );
		}
	}

	if( client->following ) {
		if( !old->upmove && cmd->upmove ) {
			MVD_FollowCycle( client, 1 );
		}
	} else {
        memset( &pm, 0, sizeof( pm ) );
        pm.trace = MVD_Trace;
        pm.pointcontents = MVD_PointContents;
        pm.s = client->ps.pmove;
        pm.cmd = *cmd;

        PF_Pmove( &pm );

        client->ps.pmove = pm.s;
        if( pm.s.pm_type != PM_FREEZE ) {
            VectorCopy( pm.viewangles, client->ps.viewangles );
        }
    }

	*old = *cmd;
}

static void MVD_GameRunFrame( void ) {
    mvd_t *mvd, *next;
    udpClient_t *u;
    tcpClient_t *t;
    uint16 length;

    LIST_FOR_EACH_SAFE( mvd_t, mvd, next, &mvd_ready, ready ) {
        if( setjmp( mvd_jmpbuf ) ) {
            continue;
        }

        MVD_Update( mvd );

        // parse stream
        if( mvd->state < MVD_READING ) {
            continue;
        }

        if( !MVD_Parse( mvd ) ) {
            continue;
        }

        // update UDP clients
        LIST_FOR_EACH( udpClient_t, u, &mvd->udpClients, entry ) {
            if( u->cl->state == cs_spawned ) {
                MVD_UpdateClient( u );
            }
        }

        // send this message to TCP clients
        length = LittleShort( msg_read.cursize );
        LIST_FOR_EACH( tcpClient_t, t, &mvd->tcpClients, mvdEntry ) {
            if( t->state == cs_spawned ) {
                SV_HttpWrite( t, &length, 2 );
                SV_HttpWrite( t, msg_read.data, msg_read.cursize );
#if USE_ZLIB
                t->noflush++;
#endif
            }
        }

        // write this message to demofile
        if( mvd->demorecording ) {
        	FS_Write( &length, 2, mvd->demofile );
            FS_Write( msg_read.data, msg_read.cursize, mvd->demofile );
        }
    }
}

static void MVD_GameServerCommand( void ) {
}

game_export_t mvd_ge = {
	GAME_API_VERSION,

	MVD_GameInit,
	MVD_GameShutdown,
	MVD_GameSpawnEntities,
	MVD_GameWriteGame,
	MVD_GameReadGame,
	MVD_GameWriteLevel,
	MVD_GameReadLevel,
	MVD_GameClientConnect,
	MVD_GameClientBegin,
	MVD_GameClientUserinfoChanged,
	MVD_GameClientDisconnect,
	MVD_GameClientCommand,
	MVD_GameClientThink,
	MVD_GameRunFrame,
	MVD_GameServerCommand
};

