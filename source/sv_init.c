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
#include "mvd_local.h"

server_static_t	svs;				// persistant server info
server_t		sv;					// local server

void SV_ClientReset( client_t *client ) {
    if( client->state < cs_connected ) {
        return;
    }

    // any partially connected client will be restarted
    client->state = cs_connected;
    client->lastframe = -1;
    client->sendTime = 0;
    client->surpressCount = 0;
    memset( &client->lastcmd, 0, sizeof( client->lastcmd ) );
}

/*
================
SV_SpawnServer

Change the server to a new map, taking all connected
clients along with it.
================
*/
void SV_SpawnServer( const char *server, const char *spawnpoint ) {
	int			i;
	uint32		checksum;
	char		string[MAX_QPATH];
	client_t	*client;
    tcpClient_t *t;

	Com_Printf( "------- Server Initialization -------\n" );
	Com_Printf( "SpawnServer: %s\n", server );

	// force MVD recording to stop
	SV_MvdRecStop();

	CM_FreeMap( &sv.cm );
	
	// wipe the entire per-level structure
	memset( &sv, 0, sizeof( sv ) );
	sv.spawncount = ( rand() | ( rand() << 16 ) ) ^ Sys_Realtime();
	sv.spawncount &= 0x7FFFFFFF;

    // reset counters
	svs.realtime = 0;
	svs.nextEntityStates = 0;

    if( sv_mvd_enable->integer ) {
        // setup buffers for accumulating datagrams
        SZ_Init( &sv.mvd.message, svs.mvd.message_data, MAX_MSGLEN );
        SZ_Init( &sv.mvd.datagram, svs.mvd.datagram_data, MAX_MSGLEN );
    }

	// init rate limits
	SV_RateInit( &svs.ratelimit_status, sv_status_limit->integer, 1000 );
	SV_RateInit( &svs.ratelimit_badpass, 1, sv_badauth_time->value * 1000 );
	SV_RateInit( &svs.ratelimit_badrcon, 1, sv_badauth_time->value * 1000 );

	// save name for levels that don't set message
	Q_strncpyz( sv.configstrings[CS_NAME], server, MAX_QPATH );
	Q_strncpyz( sv.name, server, sizeof( sv.name ) );
	
	if( Cvar_VariableInteger( "deathmatch" ) ) {
		sprintf( sv.configstrings[CS_AIRACCEL],
            "%d", sv_airaccelerate->integer );
	} else {
		strcpy( sv.configstrings[CS_AIRACCEL], "0" );
	}

    FOR_EACH_CLIENT( client ) {
		// needs to reconnect
        SV_ClientReset( client );
	}

    Q_concat( string, sizeof( string ), "maps/", server, ".bsp", NULL );
    strcpy( sv.configstrings[CS_MODELS + 1], string );
    CM_LoadMap( &sv.cm, string, 0, &checksum );

    sprintf( sv.configstrings[CS_MAPCHECKSUM], "%d", ( int )checksum );
    
    for( i = 1; i < sv.cm.cache->numcmodels; i++ ) {
        sprintf( sv.configstrings[ CS_MODELS + 1 + i ], "*%d", i );
    }

	//
	// clear physics interaction links
	//
	SV_ClearWorld();

	//
	// spawn the rest of the entities on the map
	//	

	// precache and static commands can be issued during
	// map initialization
	sv.state = ss_loading;

	// load and spawn all other entities
	ge->SpawnEntities ( sv.name, CM_EntityString( &sv.cm ), spawnpoint );

	// run two frames to allow everything to settle
	ge->RunFrame ();
	ge->RunFrame ();

	// make sure maxclients string is correct
    sprintf( sv.configstrings[CS_MAXCLIENTS], "%d", sv_maxclients->integer );

	// all precaches are complete
	sv.state = ss_game;

    // respawn dummy MVD client and set base states
    SV_MvdSpawnDummy();

    LIST_FOR_EACH( tcpClient_t, t, &svs.mvd.clients, mvdEntry ) {
		// needs to reconnect
        if( t->state >= cs_connected ) {
            t->state = cs_connected;
            SV_MvdClientNew( t );
        }
    }

	// set serverinfo variable
	Cvar_FullSet( "mapname", sv.name,
        CVAR_SERVERINFO|CVAR_NOSET, CVAR_SET_DIRECT );

	Cvar_SetInteger( "sv_running", ss_game );
	Cvar_Set( "sv_paused", "0" );
	Cvar_Set( "timedemo", "0" );

	Com_Printf ("-------------------------------------\n");
}


/*
==============
SV_InitGame

A brand new game has been started.
If ismvd is true, load the built-in MVD game module.
==============
*/
void SV_InitGame( qboolean ismvd ){
	int		i, entnum;
	edict_t	*ent;
	client_t *client;

	if( svs.initialized ) {
		// cause any connected clients to reconnect
		SV_Shutdown( "Server restarted\n", KILL_RESTART );
	} else {
		// make sure the client is down
		CL_Disconnect( ERR_SILENT, NULL );
		SCR_BeginLoadingPlaque();

		CM_FreeMap( &sv.cm );
		memset( &sv, 0, sizeof( sv ) );
	}

	// get any latched variable changes (maxclients, etc)
	Cvar_GetLatchedVars ();

	CL_LocalConnect();

	if( ismvd ) {
		Cvar_Set( "deathmatch", "1" );
		Cvar_Set( "coop", "0" );
	} else {
		if( Cvar_VariableInteger( "coop" ) &&
            Cvar_VariableInteger( "deathmatch" ) )
        {
			Com_Printf( "Deathmatch and Coop both set, disabling Coop\n" );
			Cvar_Set( "coop", "0" );
		}

		// dedicated servers can't be single player and are usually DM
		// so unless they explicity set coop, force it to deathmatch
		if( dedicated->integer ) {
			if( !Cvar_VariableInteger( "coop" ) )
				Cvar_Set( "deathmatch", "1" );
		}
	}

	// init clients
	if( Cvar_VariableInteger( "deathmatch" ) ) {
		if( sv_maxclients->integer <= 1 ) {
			Cvar_SetInteger( "maxclients", 8 );
		} else if( sv_maxclients->integer > CLIENTNUM_RESERVED ) {
			Cvar_SetInteger( "maxclients", CLIENTNUM_RESERVED );
		}
		svs.gametype = GT_DEATHMATCH;
	} else if( Cvar_VariableInteger( "coop" ) ) {
		if( sv_maxclients->integer <= 1 || sv_maxclients->integer > 4 )
			Cvar_SetInteger( "maxclients", 4 );
		svs.gametype = GT_COOP;
	} else {	// non-deathmatch, non-coop is one player
		Cvar_FullSet( "maxclients", "1", CVAR_SERVERINFO|CVAR_LATCH,
            CVAR_SET_DIRECT );
		svs.gametype = GT_SINGLEPLAYER;
	}

    Cvar_ClampInteger( sv_reserved_slots, 0, sv_maxclients->integer - 1 );

    // enable networking
	if( sv_maxclients->integer > 1 ) {
		NET_Config( NET_SERVER );

        if( sv_http_enable->integer ) {
            if( NET_Listen( qtrue ) == NET_OK ) {
                Cvar_ClampInteger( sv_http_minclients, 0,
                    sv_http_maxclients->integer );
//                svs.httppool = 
            } else {
                Com_EPrintf( "%s while opening server TCP port.\n",
                    NET_ErrorString() );
                Cvar_Set( "sv_http_enable", "0" );
            }
        }
    }

	svs.numEntityStates = sv_maxclients->integer * UPDATE_BACKUP * MAX_PACKET_ENTITIES;

	svs.clientpool = SV_Mallocz( sizeof( client_t ) * sv_maxclients->integer );
	svs.entityStates = SV_Mallocz( sizeof( entity_state_t ) * svs.numEntityStates );

    if( sv_mvd_enable->integer ) {
        Z_TagReserve( sizeof( player_state_t ) * sv_maxclients->integer +
            sizeof( entity_state_t ) * MAX_EDICTS + MAX_MSGLEN * 2, TAG_SERVER );
        svs.mvd.message_data = Z_ReservedAlloc( MAX_MSGLEN );
        svs.mvd.datagram_data = Z_ReservedAlloc( MAX_MSGLEN );
        svs.mvd.players = Z_ReservedAlloc( sizeof( player_state_t ) * sv_maxclients->integer );
        svs.mvd.entities = Z_ReservedAlloc( sizeof( entity_state_t ) * MAX_EDICTS );
    }

#if USE_ZLIB
	if( deflateInit2( &svs.z, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
        -15, 9, Z_DEFAULT_STRATEGY ) != Z_OK )
    {
        Com_Error( ERR_FATAL, "%s: deflateInit2() failed", __func__ );
    }
#endif

	// heartbeats will always be sent to the id master
	svs.last_heartbeat = -99999;		// send immediately

	// init game
	if( ismvd ) {
		if( ge ) {
			SV_ShutdownGameProgs();
		}
		ge = &mvd_ge;
		ge->Init();
	} else {
		SV_InitGameProgs();
	}

    List_Init( &svs.clients );
    List_Init( &svs.mvd.clients );
    List_Init( &svs.tcpClients );

	for( i = 0; i < sv_maxclients->integer; i++ ) {
        client = svs.clientpool + i;
		entnum = i + 1;
		ent = EDICT_NUM( entnum );
		ent->s.number = entnum;
		client->edict = ent;
        client->number = i;
	}

#if USE_ANTICHEAT & 2
    if( !ismvd ) {
        AC_Connect();
    }
#endif

	svs.initialized = qtrue;
}


/*
======================
SV_Map

  the full syntax is:

  map [*]<map>$<startspot>+<nextserver>

command from the console or progs.
Map can also be a.cin, .pcx, or .dm2 file
Nextserver is used to allow a cinematic to play, then proceed to
another level:

	map tram.cin+jail_e3
======================
*/
void SV_Map (const char *levelstring, qboolean restart) {
	char	level[MAX_QPATH];
	char	spawnpoint[MAX_QPATH];
	char	expanded[MAX_QPATH];
	char	*ch;

	// skip the end-of-unit flag if necessary
	if( *levelstring == '*' ) {
		levelstring++;
	}

	// save levelstring as it typically points to cmd_argv
	Q_strncpyz( level, levelstring, sizeof( level ) );

	// if there is a + in the map, set nextserver to the remainder
	ch = strstr(level, "+");
	if (ch) {
		*ch = 0;
		Cvar_Set ("nextserver", va("gamemap \"%s\"", ch+1));
	} else {
		Cvar_Set ("nextserver", "");
    }

	// if there is a $, use the remainder as a spawnpoint
	ch = strstr( level, "$" );
	if( ch ) {
		*ch = 0;
		strcpy( spawnpoint, ch + 1 );
	} else {
		spawnpoint[0] = 0;
    }

    Q_concat( expanded, sizeof( expanded ), "maps/", level, ".bsp", NULL );
    if( FS_LoadFile( expanded, NULL ) == -1 ) {
        Com_Printf( "Can't find %s\n", expanded );
        return;
    }

	if( sv.state == ss_dead || sv.state == ss_broadcast || restart ) {
		SV_InitGame( qfalse );	// the game is just starting
	}

    // change state to loading
    if( sv.state > ss_loading ) {
        sv.state = ss_loading;
    }
    
    SCR_BeginLoadingPlaque();			// for local system
    SV_BroadcastCommand( "changing\n" );
    SV_SendClientMessages();
    SV_SendAsyncPackets();
    SV_SpawnServer( level, spawnpoint );

	SV_BroadcastCommand( "reconnect\n" );
}

