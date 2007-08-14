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

/*
================
SV_FindIndex

================
*/
static int SV_FindIndex( const char *name, int start, int max, qboolean create ) {
	char *string;
	int		i;
	
	if( !name || !name[0] )
		return 0;

	for( i = 1; i < max; i++ ) {
		string = sv.configstrings[start + i];
		if( !string[0] ) {
			break;
		}
		if( !strcmp( string, name ) ) {
			return i;
		}
	}

	if( !create )
		return 0;

	if( i == max )
		Com_Error( ERR_DROP, "SV_FindIndex: overflow" );

	PF_Configstring( i + start, name );

	return i;
}


int SV_ModelIndex (const char *name) {
	return SV_FindIndex (name, CS_MODELS, MAX_MODELS, qtrue);
}

int SV_SoundIndex (const char *name) {
	return SV_FindIndex (name, CS_SOUNDS, MAX_SOUNDS, qtrue);
}

int SV_ImageIndex (const char *name) {
	return SV_FindIndex (name, CS_IMAGES, MAX_IMAGES, qtrue);
}

/*
=================
SV_CheckForSavegame
=================
*/
void SV_CheckForSavegame (void) {
	char		name[MAX_OSPATH];
	int			i;

	if ( sv_noreload->integer )
		return;

	if ( Cvar_VariableInteger( "deathmatch" ) )
		return;

	Com_sprintf (name, sizeof(name), "save/current/%s.sav", sv.name);

	if( FS_LoadFile( name, NULL ) < 1 )
		return;		// no savegame


	SV_ClearWorld ();

	// get configstrings and areaportals
	SV_ReadLevelFile ();

	if (!sv.loadgame)
	{	// coming back to a level after being in a different
		// level, so run it for ten seconds

		// rlava2 was sending too many lightstyles, and overflowing the
		// reliable data. temporarily changing the server state to loading
		// prevents these from being passed down.
		server_state_t		previousState;		// PGM

		previousState = sv.state;				// PGM
		sv.state = ss_loading;					// PGM
		for (i=0 ; i<100 ; i++)
			ge->RunFrame ();

		sv.state = previousState;				// PGM
	}
}

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
void SV_SpawnServer( char *server, char *spawnpoint, server_state_t serverstate,
					attractLoop_t attractloop, qboolean loadgame,
                    qboolean fakemap )
{
	int			i;
	uint32		checksum;
	char		string[MAX_QPATH];
	client_t	*client;
    tcpClient_t *t;

	Com_Printf( "------- Server Initialization -------\n" );
	Com_Printf( "SpawnServer: %s\n", server );

	/* force MVD recording to stop */
	SV_MvdRecStop();

	CM_FreeMap( &sv.cm );
	
	if( sv.demofile ) {
		FS_FCloseFile( sv.demofile );
	}

	// wipe the entire per-level structure
	memset( &sv, 0, sizeof( sv ) );
	svs.realtime = 0;
	svs.nextEntityStates = 0;
	svs.nextPlayerStates = 0;
	sv.loadgame = loadgame;
	sv.attractloop = attractloop;
	sv.spawncount = ( rand() | ( rand() << 16 ) ) ^ Sys_Realtime();
	sv.spawncount &= 0x7FFFFFFF;

    // setup a buffer for accumulating multicast datagrams
    if( svs.multicast_buffer ) {
    	SZ_Init( &sv.multicast, svs.multicast_buffer, MAX_MSGLEN );
    }

	// init rate limits
	SV_RateInit( &svs.ratelimit_status, sv_status_limit->integer, 1000 );
	SV_RateInit( &svs.ratelimit_badpass, 1, sv_badauth_time->value * 1000 );
	SV_RateInit( &svs.ratelimit_badrcon, 1, sv_badauth_time->value * 1000 );

	// save name for levels that don't set message
	Q_strncpyz( sv.configstrings[CS_NAME], server, MAX_QPATH );
	Q_strncpyz( sv.name, server, sizeof( sv.name ) );
	
	if( Cvar_VariableInteger( "deathmatch" ) ) {
		strcpy( sv.configstrings[CS_AIRACCEL],
            va( "%d", sv_airaccelerate->integer ) );
	} else {
		strcpy( sv.configstrings[CS_AIRACCEL], "0" );
	}

    FOR_EACH_CLIENT( client ) {
		// needs to reconnect
        SV_ClientReset( client );
	}

	if( !fakemap ) {
		Com_sprintf( string, sizeof( string ), "maps/%s.bsp", server );
		strcpy( sv.configstrings[CS_MODELS + 1], string );
		CM_LoadMap( &sv.cm, string, 0, &checksum );
	
		Com_sprintf( sv.configstrings[CS_MAPCHECKSUM],
			MAX_QPATH, "%i", ( int )checksum );
		
		for( i = 1; i < sv.cm.cache->numcmodels; i++ ) {
			Com_sprintf( sv.configstrings[ CS_MODELS + 1 + i ],
				MAX_QPATH, "*%i", i );
		}
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

	// all precaches are complete
	sv.state = serverstate;

	// check for a savegame
	SV_CheckForSavegame ();

	// check maxclients (MVD clients rely on this)
    if( serverstate < ss_cinematic ) {
        i = atoi( sv.configstrings[CS_MAXCLIENTS] );
        if( i != sv_maxclients->integer ) {
            Com_WPrintf( "Game DLL specified wrong CS_MAXCLIENTS "
                "(%d instead of %d), fixed.\n", i, sv_maxclients->integer );
            Com_sprintf( string, sizeof( string ), "%d",
                    sv_maxclients->integer );
            strcpy( sv.configstrings[CS_MAXCLIENTS], string );
        }
    }

    SV_MvdSpawnDummy();

    LIST_FOR_EACH( tcpClient_t, t, &svs.mvdClients, mvdEntry ) {
		// needs to reconnect
        if( t->state >= cs_connected ) {
            t->state = cs_connected;
            SV_MvdClientNew( t );
        }
    }

	// set serverinfo variable
	Cvar_FullSet( "mapname", sv.name, CVAR_SERVERINFO|CVAR_NOSET,
        CVAR_SET_DIRECT );

	Cvar_SetInteger( "sv_running", serverstate );
	Cvar_SetInteger( "sv_paused", 0 );
	if( serverstate != ss_demo ) {
		Cvar_Set( "timedemo", "0" );
	}

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

	if( sv_maxclients->integer > 1 ) {
		NET_Config( NET_SERVER );

        if( sv_http_enable->integer ) {
            if( NET_Listen( qtrue ) == NET_ERROR ) {
                Com_EPrintf( "%s while opening server TCP port.\n",
                    NET_ErrorString() );
                Cvar_Set( "sv_http_enable", "0" );
            }
        }
    }

	svs.numEntityStates = sv_maxclients->integer * UPDATE_BACKUP * MAX_PACKET_ENTITIES;
	svs.numPlayerStates = sv_maxclients->integer * UPDATE_BACKUP;
	if( sv_mvd_enable->integer && !ismvd ) {
		svs.numEntityStates += MAX_EDICTS * 2;
		svs.numPlayerStates += sv_maxclients->integer * 2;
        svs.multicast_buffer = SV_Malloc( MAX_MSGLEN );
	}

	svs.clientpool = SV_Mallocz( sizeof( client_t ) * sv_maxclients->integer );
	svs.entityStates = SV_Mallocz( sizeof( entity_state_t ) * svs.numEntityStates );
    svs.playerStates = SV_Mallocz( sizeof( player_state_t ) * svs.numPlayerStates );

#if USE_ZLIB
	if( deflateInit2( &svs.z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 9,
        Z_DEFAULT_STRATEGY ) != Z_OK )
    {
        Com_Error( ERR_FATAL, "deflateInit2() failed" );
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
    List_Init( &svs.mvdClients );
    List_Init( &svs.tcpClients );

	for( i = 0; i < sv_maxclients->integer; i++ ) {
        client = svs.clientpool + i;
		entnum = i + 1;
		ent = EDICT_NUM( entnum );
		ent->s.number = entnum;
		client->edict = ent;
        client->number = i;
	}

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
void SV_Map (attractLoop_t attractloop, char *levelstring, qboolean loadgame)
{
	char	level[MAX_QPATH];
	char	*ch;
	int		l;
	char	spawnpoint[MAX_QPATH];
    server_state_t state;
    qboolean fakemap;

	// skip the end-of-unit flag if necessary
	if( *levelstring == '*' ) {
		levelstring++;
	}

	// moved here, because levelstring typically points to cmd_argv
	// and may be clobbered by SV_InitGame
	Q_strncpyz( level, levelstring, sizeof( level ) );

	if( ( sv.state == ss_dead && !sv.loadgame ) ||
		sv.state == ss_broadcast )
	{
		SV_InitGame ( qfalse );	// the game is just starting
	}

	// if there is a + in the map, set nextserver to the remainder
	ch = strstr(level, "+");
	if (ch)
	{
		*ch = 0;
		Cvar_Set ("nextserver", va("gamemap \"%s\"", ch+1));
	}
	else
		Cvar_Set ("nextserver", "");

	//ZOID special hack for end game screen in coop mode
	if (Cvar_VariableValue ("coop") && !Q_stricmp(level, "victory.pcx"))
		Cvar_Set ("nextserver", "gamemap \"*base1\"");

	// if there is a $, use the remainder as a spawnpoint
	ch = strstr( level, "$" );
	if( ch ) {
		*ch = 0;
		strcpy( spawnpoint, ch + 1 );
	} else {
		spawnpoint[0] = 0;
    }

	l = strlen( level );
	if( l > 4 && !strcmp( level + l - 4, ".cin" ) ) {
        fakemap = qtrue;
        state = ss_cinematic;
	} else if( l > 4 && !strcmp( level + l - 4, ".dm2" ) ) {
        fakemap = qtrue;
        state = ss_demo;
	} else if( l > 4 && !strcmp( level + l - 4, ".pcx" ) ) {
        fakemap = qtrue;
        state = ss_pic;
	} else {
        fakemap = qfalse;
        state = ss_game;
    }
    
    SCR_BeginLoadingPlaque();			// for local system
    SV_BroadcastCommand( "changing\n" );
    /* temporary change server state to loading so
     * SV_SendClientMessages will not send dummy svc_frame */
    if( sv.state > ss_loading ) {
        sv.state = ss_loading;
    }
    SV_SendClientMessages();
    SV_SendAsyncPackets();
    SV_SpawnServer( level, spawnpoint, state, attractloop,
            loadgame, fakemap );

	SV_BroadcastCommand( "reconnect\n" );
}

