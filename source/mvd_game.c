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

static cvar_t	*mvd_admin_password;
static cvar_t	*mvd_flood_msgs;
static cvar_t	*mvd_flood_persecond;
static cvar_t	*mvd_flood_waitdelay;
static cvar_t	*mvd_flood_mute;

udpClient_t     *mvd_clients;

mvd_player_t    mvd_dummy;

extern jmp_buf     mvd_jmpbuf;


/*
==============================================================================

LAYOUTS

==============================================================================
*/

static void MVD_LayoutClients( udpClient_t *client ) {
	char layout[MAX_STRING_CHARS];
	char buffer[MAX_STRING_CHARS];
	char status[MAX_QPATH];
	int length, total;
	udpClient_t *cl;
    mvd_t *mvd = client->mvd;
	int y;

	total = sprintf( layout,
		"xl 32 yb -40 string2 \""APPLICATION" "VERSION"\" "
		"yb -32 string http://q2pro.sf.net/ "
		"xv 0 yv 0 string2 Name "
		"xv 152 string2 Ping "
		"xv 208 string2 Status " );

	y = 8;
    LIST_FOR_EACH( udpClient_t, cl, &mvd->udpClients, entry ) {
		if( cl->cl->state < cs_spawned ) {
			strcpy( status, "connecting" );
        } else if( cl->target ) {
			strcpy( status, "-> " );
            strcpy( status + 3, cl->target->name );
        } else {
            strcpy( status, "observing" );
        }
		length = sprintf( buffer,
			"xv 0 yv %d string \"%.15s\" "
			"xv 152 string %d "
			"xv 208 string \"%s\" ",
			y, cl->cl->name, cl->ping, status );
		if( total + length >= sizeof( layout ) ) {
			break;
		}
		memcpy( layout + total, buffer, length );
		total += length;
		y += 8;
	}

    layout[total] = 0;

    // send the layout
	MSG_WriteByte( svc_layout );
	MSG_WriteData( layout, total + 1 );
	SV_ClientAddMessage( client->cl, MSG_CLEAR );

	client->layout_time = sv.time + LAYOUT_MSEC;
}

static void MVD_LayoutChannels( udpClient_t *client ) {
	char layout[MAX_STRING_CHARS];
	char buffer[MAX_STRING_CHARS];
    mvd_t *mvd;
	int length, total, cursor, y;

    total = sprintf( layout, "xv 32 yv 8 picn inventory "
        "xv 64 yv 32 string2 \"Channel       Map     CL\" "
        "yv 40 string \"------------- ------- --\" "
		"xl 32 yb -40 string2 \""APPLICATION" "VERSION"\" "
		"yb -32 string http://q2pro.sf.net/ " );

    cursor = List_Count( &mvd_ready );
    if( cursor ) {
        clamp( client->cursor, 0, cursor - 1 );

        y = 48;
        cursor = 0;
        LIST_FOR_EACH( mvd_t, mvd, &mvd_ready, ready ) {
            length = sprintf( buffer,
                "xv 56 yv %d string \"%c%-13.13s %-7.7s %d%s\" ", y,
                cursor == client->cursor ? 0x8d : 0x20,
                mvd->name, mvd->mapname,
                List_Count( &mvd->udpClients ),
                mvd == client->mvd ? "+" : "" );
            if( total + length >= sizeof( layout ) ) {
                break;
            }
            memcpy( layout + total, buffer, length );
            total += length;
            y += 8;

            cursor++;
        }
    } else {
        client->cursor = 0;
        total += sprintf( layout + total,
            "xv 56 yv 48 string \" <no channels>\" " );
    }

    layout[total] = 0;

    // send the layout
	MSG_WriteByte( svc_layout );
	MSG_WriteData( layout, total + 1 );				
	SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );
}

static void MVD_LayoutScores( udpClient_t *client ) {
    mvd_t *mvd = client->mvd;
    char *layout = mvd->layout[0] ? mvd->layout :
        "xv 100 yv 60 string \"<no scoreboard>\"";

    // send the layout
	MSG_WriteByte( svc_layout );
    MSG_WriteString( layout );
	SV_ClientAddMessage( client->cl, MSG_CLEAR );
}

static void MVD_LayoutFollow( udpClient_t *client ) {
    char *name = client->target ? client->target->name : "<no target>";

    // send the layout
	MSG_WriteByte( svc_layout );
    MSG_WriteString( va( "xv 0 yt 48 cstring \"%s\"", name ) );
	SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );
}

static void MVD_SetDefaultLayout( udpClient_t *client ) {
    mvd_t *mvd = client->mvd;

	if( mvd == &mvd_waitingRoom ) {
        if( client->layout_type != LAYOUT_CHANNELS ) {
    		client->layout_type = LAYOUT_CHANNELS;
            client->cursor = 0;
		    MVD_LayoutChannels( client );
        }
	} else if( mvd->intermission ) {
        client->layout_type = LAYOUT_SCORES;
        MVD_LayoutScores( client );
	} else if( client->target && ( client->cl->protocol !=
        PROTOCOL_VERSION_Q2PRO || client->cl->settings[CLS_RECORDING] ) )
    {
        client->layout_type = LAYOUT_FOLLOW;
        MVD_LayoutFollow( client );
    } else {
		client->layout_type = LAYOUT_NONE;
	}
}

/*
==============================================================================

CHASE CAMERA

==============================================================================
*/

static void MVD_FollowStop( udpClient_t *client ) {
    mvd_t *mvd = client->mvd;
    mvd_cs_t *cs;
	int i;

	client->ps.viewangles[ROLL] = 0;

	for( i = 0; i < 3; i++ ) {
		client->ps.pmove.delta_angles[i] = ANGLE2SHORT(
            client->ps.viewangles[i] ) - client->lastcmd.angles[i];
	}

	VectorClear( client->ps.kick_angles );
    Vector4Clear( client->ps.blend );
	client->ps.pmove.pm_flags = 0;
	client->ps.pmove.pm_type = mvd->pm_type;
	client->ps.rdflags = 0;
	client->ps.gunindex = 0;
    client->ps.fov = client->fov;

    for( cs = mvd->dummy->configstrings; cs; cs = cs->next ) {
        MSG_WriteByte( svc_configstring );
        MSG_WriteShort( cs->index );
        MSG_WriteString( cs->string );
        SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );
    }

    client->clientNum = mvd->clientNum;
    client->oldtarget = client->target;
    client->target = NULL;

    if( client->layout_type == LAYOUT_FOLLOW ) {
        MVD_SetDefaultLayout( client );
    }

    MVD_UpdateClient( client );
}

static void MVD_FollowStart( udpClient_t *client, mvd_player_t *target ) {
    mvd_cs_t *cs;

	if( client->target == target ) {
		return;
	}

	client->target = target;

	// send delta configstrings
    for( cs = target->configstrings; cs; cs = cs->next ) {
        MSG_WriteByte( svc_configstring );
        MSG_WriteShort( cs->index );
        MSG_WriteString( cs->string );
        SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );
	}

    SV_ClientPrintf( client->cl, PRINT_LOW, "[MVD] Following %s.\n", target->name );

    if( !client->layout_type && ( client->cl->protocol !=
        PROTOCOL_VERSION_Q2PRO || client->cl->settings[CLS_RECORDING] ) )
    {
        client->layout_type = LAYOUT_FOLLOW;
    }
    if( client->layout_type == LAYOUT_FOLLOW ) {
        MVD_LayoutFollow( client );
    }

    MVD_UpdateClient( client );
}

static void MVD_FollowFirst( udpClient_t *client ) {
    mvd_t *mvd = client->mvd;
	mvd_player_t *target;
	int i;

    // pick up the first active player
    for( i = 0; i < mvd->maxclients; i++ ) {
        target = &mvd->players[i];
        if( target->inuse && target != mvd->dummy ) {
            MVD_FollowStart( client, target );
            return;
        }
    }

    SV_ClientPrintf( client->cl, PRINT_MEDIUM, "[MVD] No players to follow.\n" );
}

static void MVD_FollowLast( udpClient_t *client ) {
    mvd_t *mvd = client->mvd;
	mvd_player_t *target;
	int i;

    // pick up the last active player
    for( i = 0; i < mvd->maxclients; i++ ) {
        target = &mvd->players[ mvd->maxclients - i - 1 ];
        if( target->inuse && target != mvd->dummy ) {
            MVD_FollowStart( client, target );
            return;
        }
    }

    SV_ClientPrintf( client->cl, PRINT_MEDIUM, "[MVD] No players to follow.\n" );
}

static void MVD_FollowNext( udpClient_t *client ) {
    mvd_t *mvd = client->mvd;
	mvd_player_t *target = client->target;

    if( !target ) {
        MVD_FollowFirst( client );
        return;
    }

    do {
        if( target == mvd->players + mvd->maxclients - 1 ) {
            target = mvd->players;
        } else {
            target++;
        }
        if( target == client->target ) {
            return;
        }
    } while( !target->inuse || target == mvd->dummy );

	MVD_FollowStart( client, target );
}

static void MVD_FollowPrev( udpClient_t *client ) {
    mvd_t *mvd = client->mvd;
	mvd_player_t *target = client->target;

    if( !target ) {
        MVD_FollowLast( client );
        return;
    }

    do {
        if( target == mvd->players ) {
            target = mvd->players + mvd->maxclients - 1;
        } else {
            target--;
        }
        if( target == client->target ) {
            return;
        }
    } while( !target->inuse || target == mvd->dummy );

	MVD_FollowStart( client, target );
}

void MVD_UpdateClient( udpClient_t *client ) {
    mvd_t *mvd = client->mvd;
    mvd_player_t *target = client->target;
    int i;

    if( !target ) {
        // copy stats of the dummy MVD observer
        target = mvd->dummy;
        for( i = 0; i < MAX_STATS; i++ ) {
            client->ps.stats[i] = target->ps.stats[i];
        }
        return;
    }

    if( !target->inuse ) {
        // player is no longer active
        MVD_FollowStop( client );
        return;
    }

    // copy entire player state
	client->ps = target->ps;
    if( client->uf & UF_LOCALFOV ) {
    	client->ps.fov = client->fov;
    }
	client->ps.pmove.pm_flags |= PMF_NO_PREDICTION;
	client->ps.pmove.pm_type = PM_FREEZE;
    client->clientNum = target - mvd->players;
}

/*
==============================================================================

SPECTATOR COMMANDS

==============================================================================
*/

void MVD_BroadcastPrintf( mvd_t *mvd, int level, int mask, const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];
    int         len;
    udpClient_t *other;
    client_t    *cl;

	va_start( argptr, fmt );
	len = Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

    MSG_WriteByte( svc_print );
    MSG_WriteByte( level );
    MSG_WriteData( text, len + 1 );

    LIST_FOR_EACH( udpClient_t, other, &mvd->udpClients, entry ) {
        cl = other->cl;
        if( cl->state < cs_spawned ) {
            continue;
        }
		if( level < cl->messagelevel ) {
			continue;
        }
        if( level == PRINT_CHAT && ( other->uf & mask ) ) {
            continue;
        }
		SV_ClientAddMessage( cl, MSG_RELIABLE );
	}

    SZ_Clear( &msg_write );
}

void MVD_SwitchChannel( udpClient_t *client, mvd_t *mvd ) {
    client_t *cl = client->cl;

	List_Remove( &client->entry );
    List_Append( &mvd->udpClients, &client->entry );
    client->mvd = mvd;
    client->begin_time = 0;
    client->target = client->oldtarget = NULL;

    cl->gamedir = mvd->gamedir;
    cl->mapname = mvd->mapname;
    cl->configstrings = ( char * )mvd->configstrings;
    cl->slot = mvd->clientNum;
    cl->cm = &mvd->cm;
    cl->pool = &mvd->pool;

    // needs to reconnect
    MSG_WriteByte( svc_stufftext );
    MSG_WriteString( va( "changing map=%s; reconnect\n", mvd->mapname ) );
    SV_ClientReset( client->cl );
    SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );
}

void MVD_TrySwitchChannel( udpClient_t *client, mvd_t *mvd ) {
    if( mvd == client->mvd ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] You are already on this channel.\n" );
        return; // nothing to do
    }
    if( client->begin_time ) {
        if( client->begin_time > svs.realtime ) {
            client->begin_time = svs.realtime;
        }
        if( svs.realtime - client->begin_time < 3000 ) {
            SV_ClientPrintf( client->cl, PRINT_HIGH,
                "[MVD] You may not switch channels too soon.\n" );
            return;
        }
        MVD_BroadcastPrintf( client->mvd, PRINT_MEDIUM, 0,
            "[MVD] %s left the channel.\n", client->cl->name );
    }

    MVD_SwitchChannel( client, mvd );
}

static void MVD_Admin_f( udpClient_t *client ) {
    char *s = mvd_admin_password->string;

    if( client->admin ) {
        client->admin = qfalse;
    	SV_ClientPrintf( client->cl, PRINT_HIGH, "[MVD] Lost admin status.\n" );
        return;
    }

    if( !NET_IsLocalAddress( &client->cl->netchan->remote_address ) ) {
        if( Cmd_Argc() < 2 ) {
            SV_ClientPrintf( client->cl, PRINT_HIGH, "Usage: %s <password>\n", Cmd_Argv( 0 ) );
            return;
        }
        if( !s[0] || strcmp( s, Cmd_Argv( 1 ) ) ) {
            SV_ClientPrintf( client->cl, PRINT_HIGH, "[MVD] Invalid password.\n" );
            return;
        }
    }

    client->admin = qtrue;
	SV_ClientPrintf( client->cl, PRINT_HIGH, "[MVD] Granted admin status.\n" );
}

static void MVD_Say_f( udpClient_t *client ) {
    mvd_t *mvd = client->mvd;
    char *text;
    int i;

    if( mvd_flood_mute->integer && !client->admin ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] Spectators can't talk on this server.\n" );
        return;
    }

    if( client->floodTime > sv.time ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
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
            SV_ClientPrintf( client->cl, PRINT_HIGH,
                "[MVD] You can't talk for %d seconds.\n",
                mvd_flood_waitdelay->integer );
            client->floodTime = sv.time + mvd_flood_waitdelay->value * 1000;
            return;
        }
    }

    client->floodSamples[client->floodHead & FLOOD_MASK] = sv.time;
    client->floodHead++;

    text = Cmd_Args();
    //text[128] = 0; // don't let it be too long

    MVD_BroadcastPrintf( mvd, PRINT_CHAT, client->admin ? 0 : UF_NOMVDCHAT,
        "{%s}: %s\n", client->cl->name, text );
}

static void MVD_Observe_f( udpClient_t *client ) {
    if( client->target ) {
        MVD_FollowStop( client );
    } else if( client->oldtarget && client->oldtarget->inuse ) {
        MVD_FollowStart( client, client->oldtarget );
    } else {
        MVD_FollowFirst( client );
    }
}

static void MVD_Follow_f( udpClient_t *client ) {
    mvd_t *mvd = client->mvd;
    mvd_player_t *player;
    int number;

    if( Cmd_Argc() < 2 ) {
        MVD_Observe_f( client );
        return;
    }

    number = atoi( Cmd_Argv( 1 ) );
    if( number < 0 || number >= mvd->maxclients ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] Player number %d is invalid.\n", number );
        return;
    }

    player = &mvd->players[number];
    if( !player->inuse || player == mvd->dummy ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] Player %d is not active.\n", number );
        return;
    }

    MVD_FollowStart( client, player );
}

static void MVD_Invuse_f( udpClient_t *client ) {
    mvd_t *mvd;
    int cursor = 0;

    if( client->layout_type != LAYOUT_CHANNELS ) {
        return;
    }

    LIST_FOR_EACH( mvd_t, mvd, &mvd_channels, entry ) {
        if( !mvd->framenum ) {
            continue;
        }
        if( cursor == client->cursor ) {
            MVD_TrySwitchChannel( client, mvd );
            return;
        }
        cursor++;
    }
}

static void MVD_Join_f( udpClient_t *client ) {
    mvd_t *mvd;
        
	SV_BeginRedirect( RD_CLIENT );
    mvd = MVD_SetChannel( 1 );
	Com_EndRedirect();

    if( !mvd ) {
        return;
    }

    MVD_TrySwitchChannel( client, mvd );
}

static void MVD_GameClientCommand( edict_t *ent ) {
	udpClient_t *client = EDICT_MVDCL( ent );
	char *cmd;

	cmd = Cmd_Argv( 0 );

	if( !strcmp( cmd, "!mvdadmin" ) ) {
        MVD_Admin_f( client );
		return;
	}
	if( !strcmp( cmd, "say" ) || !strcmp( cmd, "say_team" ) ) {
        MVD_Say_f( client );
		return;
	}
	if( !strcmp( cmd, "follow" ) || !strcmp( cmd, "chase" ) ) {
        MVD_Follow_f( client );
		return;
	}
	if( !strcmp( cmd, "observe" ) ) {
        MVD_Observe_f( client );
		return;
	}
	if( !strcmp( cmd, "inven" ) || !strcmp( cmd, "menu" ) ) {
		if( client->layout_type == LAYOUT_CLIENTS ) {
			MVD_SetDefaultLayout( client );
		} else {
			client->layout_type = LAYOUT_CLIENTS;
			MVD_LayoutClients( client );
		}
		return;
	}
	if( !strcmp( cmd, "invnext" ) ) {
		if( client->layout_type == LAYOUT_CHANNELS ) {
            client->cursor++;
			MVD_LayoutChannels( client );
        } else {
            MVD_FollowNext( client );
        }
        return;
    }
	if( !strcmp( cmd, "invprev" ) ) {
		if( client->layout_type == LAYOUT_CHANNELS ) {
            client->cursor--;
			MVD_LayoutChannels( client );
        } else {
            MVD_FollowPrev( client );
        }
        return;
    }
	if( !strcmp( cmd, "invuse" ) ) {
        MVD_Invuse_f( client );
        return;
    }
	if( !strcmp( cmd, "help" ) || !strncmp( cmd, "score", 5 ) ) {
		if( client->layout_type == LAYOUT_SCORES ) {
			MVD_SetDefaultLayout( client );
		} else {
			client->layout_type = LAYOUT_SCORES;
			MVD_LayoutScores( client );
		}
		return;
	}
	if( !strcmp( cmd, "putaway" ) ) {
		MVD_SetDefaultLayout( client );
		return;
	}
	if( !strcmp( cmd, "channels" ) ) {
		client->layout_type = LAYOUT_CHANNELS;
		MVD_LayoutChannels( client );
		return;
	}
	if( !strcmp( cmd, "join" ) ) {
        MVD_Join_f( client );
        return;
    }
	if( !strcmp( cmd, "leave" ) ) {
        MVD_TrySwitchChannel( client, &mvd_waitingRoom );
        return;
    }
	
	SV_ClientPrintf( client->cl, PRINT_HIGH, "[MVD] Unknown command: %s\n", cmd );
}

/*
==============================================================================

MISC GAME FUNCTIONS

==============================================================================
*/

static void MVD_Update( mvd_t *mvd ) {
    udpClient_t *client;

    LIST_FOR_EACH( udpClient_t, client, &mvd->udpClients, entry ) {
		if( client->cl->state != cs_spawned ) {
            continue;
        }
        client->ps.stats[STAT_LAYOUTS] = client->layout_type ? 1 : 0;
        switch( client->layout_type ) {
        case LAYOUT_NONE:
            if( client->cl->protocol != PROTOCOL_VERSION_Q2PRO ) {
                break;
            }
            if( client->target && client->cl->settings[CLS_RECORDING] ) {
                client->layout_type = LAYOUT_FOLLOW;
                MVD_LayoutFollow( client );
            }
            break;
        case LAYOUT_CLIENTS:
            if( client->layout_time < sv.time ) {
                MVD_LayoutClients( client );
            }
            break;
        case LAYOUT_CHANNELS:
            if( mvd_dirty ) {
                MVD_LayoutChannels( client );
            }
            break;
        default:
            break;
        }
    }
}

void MVD_RemoveClient( client_t *client ) {
	int index = client - svs.udp_client_pool;
	udpClient_t *cl = &mvd_clients[index];

    List_Remove( &cl->entry );

	memset( cl, 0, sizeof( *cl ) );
	cl->cl = client;
}

static void MVD_GameInit( void ) {
    mvd_t *mvd = &mvd_waitingRoom;
    edict_t *edicts;
	int i;

	Com_Printf( "----- MVD_GameInit -----\n" );

	mvd_admin_password = Cvar_Get( "mvd_admin_password", "", CVAR_PRIVATE );
	mvd_flood_msgs = Cvar_Get( "flood_msgs", "4", 0 );
	mvd_flood_persecond = Cvar_Get( "flood_persecond", "4", 0 ); // FIXME: rename this
	mvd_flood_waitdelay = Cvar_Get( "flood_waitdelay", "10", 0 );
	mvd_flood_mute = Cvar_Get( "flood_mute", "0", 0 );

    Z_TagReserve( ( sizeof( edict_t ) +
        sizeof( udpClient_t ) ) * sv_maxclients->integer +
        sizeof( edict_t ), TAG_GAME );
	mvd_clients = Z_ReservedAllocz( sizeof( udpClient_t ) *
        sv_maxclients->integer );
    edicts = Z_ReservedAllocz( sizeof( edict_t ) *
        ( sv_maxclients->integer + 1 ) );

	for( i = 0; i < sv_maxclients->integer; i++ ) {
		mvd_clients[i].cl = &svs.udp_client_pool[i];
        edicts[i + 1].client = ( gclient_t * )&mvd_clients[i];
	}

    mvd_ge.edicts = edicts;
    mvd_ge.edict_size = sizeof( edict_t );
    mvd_ge.num_edicts = sv_maxclients->integer + 1;
    mvd_ge.max_edicts = sv_maxclients->integer + 1;

    strcpy( mvd->name, "Waiting Room" );
    strcpy( mvd->mapname, "q2dm1" );
    List_Init( &mvd->udpClients );

    strcpy( mvd->configstrings[CS_NAME], "Waiting Room" );
    strcpy( mvd->configstrings[CS_SKY], "unit1_" );
    strcpy( mvd->configstrings[CS_MAXCLIENTS], "8" );
    strcpy( mvd->configstrings[CS_MAPCHECKSUM], "80717714" );
    strcpy( mvd->configstrings[CS_MODELS + 1], "maps/q2dm1.bsp" );

    VectorSet( mvd->spawnOrigin, 984, 192, 784 );
    VectorSet( mvd->spawnAngles, 25, 72, 0 );

    mvd->dummy = &mvd_dummy;
    mvd->pm_type = PM_FREEZE;

    gameFeatures = GAME_FEATURE_CLIENTNUM|GAME_FEATURE_PROPERINUSE;
}

static void MVD_GameShutdown( void ) {
	Com_Printf( "----- MVD_GameShutdown -----\n" );

    MVD_Shutdown();

    mvd_ge.edicts = NULL;
    mvd_ge.edict_size = 0;
    mvd_ge.num_edicts = 0;
    mvd_ge.max_edicts = 0;
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
    client_t *cl = client->cl;
    mvd_t *mvd;
    int count;

    // assign them to some channel
    count = List_Count( &mvd_ready );
    if( count == 1 ) {
        mvd = LIST_FIRST( mvd_t, &mvd_ready, ready );
    } else {
        mvd = &mvd_waitingRoom;
    }
    List_Append( &mvd->udpClients, &client->entry );
    client->mvd = mvd;
    
    // override server state
    cl->gamedir = mvd->gamedir;
    cl->mapname = mvd->mapname;
    cl->configstrings = ( char * )mvd->configstrings;
    cl->slot = mvd->clientNum;
    cl->cm = &mvd->cm;
    cl->pool = &mvd->pool;

	return qtrue;
}

static void MVD_GameClientBegin( edict_t *ent ) {
	udpClient_t *client = EDICT_MVDCL( ent );
    mvd_t *mvd = client->mvd;

	client->floodTime = 0;
	client->floodHead = 0;
	memset( &client->lastcmd, 0, sizeof( client->lastcmd ) );
	memset( &client->ps, 0, sizeof( client->ps ) );
	
	if( !client->begin_time ) {
		MVD_BroadcastPrintf( mvd, PRINT_MEDIUM, 0,
            "[MVD] %s entered the channel\n", client->cl->name );
	}
	client->begin_time = svs.realtime;

    // spawn the spectator
	VectorScale( mvd->spawnOrigin, 8, client->ps.pmove.origin );
    VectorCopy( mvd->spawnAngles, client->ps.viewangles );

	MVD_SetDefaultLayout( client );
    MVD_FollowStop( client );
}

static void MVD_GameClientUserinfoChanged( edict_t *ent, char *userinfo ) {
	udpClient_t *client = EDICT_MVDCL( ent );
    char *s;
	float fov;

    s = Info_ValueForKey( userinfo, "uf" );
    client->uf = atoi( s );

    s = Info_ValueForKey( userinfo, "fov" );
	fov = atof( s );
	if( fov < 1 ) {
		fov = 90;
	} else if( fov > 160 ) {
		fov = 160;
	}
	client->fov = fov;
    if( client->uf & UF_LOCALFOV ) {
    	client->ps.fov = fov;
    }
}

static void MVD_GameClientDisconnect( edict_t *ent ) {
	udpClient_t *client = EDICT_MVDCL( ent );
    client_t *cl = client->cl;

    if( client->begin_time ) {
    	MVD_BroadcastPrintf( client->mvd, PRINT_MEDIUM, 0,
            "[MVD] %s disconnected\n", cl->name );
		client->begin_time = 0;
    }
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
        MVD_Observe_f( client );
	}

	if( client->target ) {
		if( !old->upmove && cmd->upmove ) {
			MVD_FollowNext( client );
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

        // parse stream
        if( mvd->state < MVD_READING ) {
            goto update;
        }

        if( !MVD_Parse( mvd ) ) {
            goto update;
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
        	FS_Write( &length, 2, mvd->demorecording );
            FS_Write( msg_read.data, msg_read.cursize, mvd->demorecording );
        }

        // check for intermission
        if( !mvd->intermission ) {
            if( mvd->dummy->ps.pmove.pm_type == PM_FREEZE ) {
                mvd->intermission = qtrue;
                LIST_FOR_EACH( udpClient_t, u, &mvd->udpClients, entry ) {
                    if( u->cl->state == cs_spawned && u->layout_type < LAYOUT_SCORES ) {
			            u->layout_type = LAYOUT_SCORES;
            			MVD_LayoutScores( u );            
                    }
                }
            }
        } else {
            if( mvd->dummy->ps.pmove.pm_type != PM_FREEZE ) {
                mvd->intermission = qfalse;
            }
        }

update:
        MVD_Update( mvd );
    }

    MVD_Update( &mvd_waitingRoom );
    
    mvd_dirty = qfalse;
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

