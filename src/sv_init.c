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

server_static_t svs;                // persistant server info
server_t        sv;                 // local server

void SV_ClientReset( client_t *client ) {
    if( client->state < cs_connected ) {
        return;
    }

    // any partially connected client will be restarted
    client->state = cs_connected;
    client->lastframe = -1;
    client->frames_nodelta = 0;
    client->send_delta = 0;
    client->surpressCount = 0;
    memset( &client->lastcmd, 0, sizeof( client->lastcmd ) );
}

#if USE_FPS
static void set_frame_time( void ) {
    int i = sv_fps->integer / 10;

    clamp( i, 1, 6 );

    sv.frametime = 100 / i;
    sv.framemult = i;

    Cvar_SetInteger( sv_fps, i * 10, CVAR_SET_DIRECT );
}
#endif

#if !USE_CLIENT
static void resolve_masters( void ) {
    master_t *m;
    time_t now, delta;

    now = time( NULL );
    FOR_EACH_MASTER( m ) {
        // re-resolve valid address after one day,
        // resolve invalid address after three hours
        delta = m->adr.port ? 24*60*60 : 3*60*60;
        if( now < m->last_resolved ) {
            m->last_resolved = now;
            continue;
        }
        if( now - m->last_resolved < delta ) {
            continue;
        }
        if( NET_StringToAdr( m->name, &m->adr, PORT_MASTER ) ) {
            Com_DPrintf( "Master server at %s.\n", NET_AdrToString( &m->adr ) );
        } else {
            Com_WPrintf( "Couldn't resolve master: %s\n", m->name );
            m->adr.port = 0;
        }
        m->last_resolved = now = time( NULL );
    }
}
#endif

// optionally load the entity string from external source
static void override_entity_string( const char *server ) {
    char *path = map_override_path->string;
    char buffer[MAX_QPATH], *str;
    ssize_t len;

    if( !*path ) {
        return;
    }

    len = Q_concat( buffer, sizeof( buffer ), path, server, ".ent", NULL );
    if( len >= sizeof( buffer ) ) {
        len = Q_ERR_NAMETOOLONG;
        goto fail1;
    }

    len = SV_LoadFile( buffer, ( void ** )&str );
    if( !str ) {
        if( len == Q_ERR_NOENT ) {
            return;
        }
        goto fail1;
    }

    if( len > MAX_MAP_ENTSTRING ) {
        len = Q_ERR_FBIG;
        goto fail2;
    }

    Com_Printf( "Loaded entity string from %s\n", buffer );
    sv.entitystring = str;
    return;

fail2:
    SV_FreeFile( str );
fail1:
    Com_EPrintf( "Couldn't load entity string from %s: %s\n",
        buffer, Q_ErrorString( len ) );
}


/*
================
SV_SpawnServer

Change the server to a new map, taking all connected
clients along with it.
================
*/
static void SV_SpawnServer( cm_t *cm, const char *server, const char *spawnpoint ) {
    int         i;
    client_t    *client;

    Com_Printf( "------- Server Initialization -------\n" );
    Com_Printf( "SpawnServer: %s\n", server );

    // free current level
    CM_FreeMap( &sv.cm );
    SV_FreeFile( sv.entitystring );
    
    // wipe the entire per-level structure
    memset( &sv, 0, sizeof( sv ) );
    sv.spawncount = ( rand() | ( rand() << 16 ) ) ^ Sys_Milliseconds();
    sv.spawncount &= 0x7FFFFFFF;

    // reset entity counter
    svs.next_entity = 0;

    // save name for levels that don't set message
    Q_strlcpy( sv.configstrings[CS_NAME], server, MAX_QPATH );
    Q_strlcpy( sv.name, server, sizeof( sv.name ) );
    
    if( Cvar_VariableInteger( "deathmatch" ) ) {
        sprintf( sv.configstrings[CS_AIRACCEL],
            "%d", sv_airaccelerate->integer );
    } else {
        strcpy( sv.configstrings[CS_AIRACCEL], "0" );
    }

    FOR_EACH_CLIENT( client ) {
        // needs to reconnect
        SV_ClientReset( client );
        client->spawncount = sv.spawncount;
    }

#if !USE_CLIENT
    resolve_masters();
#endif

    override_entity_string( server );

    sv.cm = *cm;
    sprintf( sv.configstrings[CS_MAPCHECKSUM], "%d", ( int )cm->cache->checksum );

    // set inline model names
    Q_concat( sv.configstrings[CS_MODELS + 1], MAX_QPATH, "maps/", server, ".bsp", NULL );
    for( i = 1; i < cm->cache->nummodels; i++ ) {
        sprintf( sv.configstrings[ CS_MODELS + 1 + i ], "*%d", i );
    }

    //
    // clear physics interaction links
    //
    SV_ClearWorld();

    //
    // spawn the rest of the entities on the map
    //  
#if USE_FPS
    set_frame_time();
#endif

    // precache and static commands can be issued during
    // map initialization
    sv.state = ss_loading;

    X86_PUSH_FPCW;
    X86_SINGLE_FPCW;

    // load and spawn all other entities
    ge->SpawnEntities ( sv.name, sv.entitystring ?
        sv.entitystring : cm->cache->entitystring, spawnpoint );

    // run two frames to allow everything to settle
    ge->RunFrame ();
    ge->RunFrame ();

    X86_POP_FPCW;

    // make sure maxclients string is correct
    sprintf( sv.configstrings[CS_MAXCLIENTS], "%d", sv_maxclients->integer );

    // all precaches are complete
    sv.state = ss_game;

#if USE_MVD_SERVER
    // respawn dummy MVD client, set base states, etc
    SV_MvdMapChanged();
#endif

    // set serverinfo variable
    SV_InfoSet( "mapname", sv.name );
    SV_InfoSet( "port", net_port->string );

    Cvar_SetInteger( sv_running, ss_game, FROM_CODE );
    Cvar_Set( "sv_paused", "0" );
    Cvar_Set( "timedemo", "0" );

    EXEC_TRIGGER( sv_changemapcmd );

#if USE_SYSCON
    SV_SetConsoleTitle();
#endif

    Com_Printf ("-------------------------------------\n");
}

/*
==============
SV_InitGame

A brand new game has been started.
If ismvd is true, load the built-in MVD game module.
==============
*/
void SV_InitGame( qboolean ismvd ) {
    int     i, entnum;
    edict_t *ent;
    client_t *client;

    if( svs.initialized ) {
        // cause any connected clients to reconnect
        SV_Shutdown( "Server restarted\n", ERR_RECONNECT );
    } else {
#if USE_CLIENT
        // make sure the client is down
        CL_Disconnect( ERR_RECONNECT );
        SCR_BeginLoadingPlaque();
#endif

        CM_FreeMap( &sv.cm );
        memset( &sv, 0, sizeof( sv ) );
    }

    // get any latched variable changes (maxclients, etc)
    Cvar_GetLatchedVars ();

#if !USE_CLIENT
    Cvar_Reset( sv_recycle );
#endif

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
        if( Com_IsDedicated() ) {
            if( !Cvar_VariableInteger( "coop" ) )
                Cvar_Set( "deathmatch", "1" );
        }
    }

    // init clients
    if( Cvar_VariableInteger( "deathmatch" ) ) {
        if( sv_maxclients->integer <= 1 ) {
            Cvar_SetInteger( sv_maxclients, 8, FROM_CODE );
        } else if( sv_maxclients->integer > CLIENTNUM_RESERVED ) {
            Cvar_SetInteger( sv_maxclients, CLIENTNUM_RESERVED, FROM_CODE );
        }
    } else if( Cvar_VariableInteger( "coop" ) ) {
        if( sv_maxclients->integer <= 1 || sv_maxclients->integer > 4 )
            Cvar_Set( "maxclients", "4" );
    } else {    // non-deathmatch, non-coop is one player
        Cvar_FullSet( "maxclients", "1", CVAR_SERVERINFO|CVAR_LATCH, FROM_CODE );
    }

    // enable networking
    if( sv_maxclients->integer > 1 ) {
        NET_Config( NET_SERVER );
    }

    svs.client_pool = SV_Mallocz( sizeof( client_t ) * sv_maxclients->integer );

    svs.num_entities = sv_maxclients->integer * UPDATE_BACKUP * MAX_PACKET_ENTITIES;
    svs.entities = SV_Mallocz( sizeof( entity_state_t ) * svs.num_entities );

#if USE_MVD_SERVER
    // initialize MVD server
    if( !ismvd ) {
        SV_MvdInit();
    }
#endif

    Cvar_ClampInteger( sv_reserved_slots, 0, sv_maxclients->integer - 1 );

#if USE_ZLIB
    svs.z.zalloc = SV_Zalloc;
    svs.z.zfree = SV_Zfree;
    if( deflateInit2( &svs.z, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
        -MAX_WBITS, 9, Z_DEFAULT_STRATEGY ) != Z_OK )
    {
        Com_Error( ERR_FATAL, "%s: deflateInit2() failed", __func__ );
    }
#endif

    // init game
#if USE_MVD_CLIENT
    if( ismvd ) {
        if( ge ) {
            SV_ShutdownGameProgs();
        }
        ge = &mvd_ge;
        ge->Init();
    } else
#endif
        SV_InitGameProgs();

    // send heartbeat very soon
    svs.last_heartbeat = -(HEARTBEAT_SECONDS-5)*1000;

    List_Init( &svs.client_list );

    for( i = 0; i < sv_maxclients->integer; i++ ) {
        client = svs.client_pool + i;
        entnum = i + 1;
        ent = EDICT_NUM( entnum );
        ent->s.number = entnum;
        client->edict = ent;
        client->number = i;
    }

#if USE_AC_SERVER
    AC_Connect( ismvd );
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
    char    level[MAX_QPATH];
    char    spawnpoint[MAX_QPATH];
    char    expanded[MAX_QPATH];
    char    *ch;
    cm_t    cm;
    qerror_t ret;
    size_t  len;

    // skip the end-of-unit flag if necessary
    if( *levelstring == '*' ) {
        levelstring++;
    }

    // save levelstring as it typically points to cmd_argv
    len = Q_strlcpy( level, levelstring, sizeof( level ) );
    if( len >= sizeof( level ) ) {
        Com_Printf( "Refusing to process oversize level string.\n" );
        return;
    }

    // if there is a + in the map, set nextserver to the remainder
    ch = strchr(level, '+');
    if (ch) {
        *ch = 0;
        Cvar_Set ("nextserver", va("gamemap \"%s\"", ch+1));
    } else {
        Cvar_Set ("nextserver", "");
    }

    // if there is a $, use the remainder as a spawnpoint
    ch = strchr( level, '$' );
    if( ch ) {
        *ch = 0;
        strcpy( spawnpoint, ch + 1 );
    } else {
        spawnpoint[0] = 0;
    }

    len = Q_concat( expanded, sizeof( expanded ), "maps/", level, ".bsp", NULL );
    if( len >= sizeof( expanded ) ) {
        ret = Q_ERR_NAMETOOLONG;
    } else {
        ret = CM_LoadMap( &cm, expanded );
    }
    if( ret ) {
        Com_Printf( "Couldn't load %s: %s\n", expanded, Q_ErrorString( ret ) );
        return;
    }

    if( sv.state != ss_game || restart ) {
        SV_InitGame( qfalse );  // the game is just starting
    }

    // change state to loading
    if( sv.state > ss_loading ) {
        sv.state = ss_loading;
    }
    
#if USE_CLIENT
    SCR_BeginLoadingPlaque();           // for local system
#endif
    SV_BroadcastCommand( "changing map=%s\n", level );
    SV_SendClientMessages();
    SV_SendAsyncPackets();
    SV_SpawnServer( &cm, level, spawnpoint );

    SV_BroadcastCommand( "reconnect\n" );
}

