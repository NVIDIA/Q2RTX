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

/*
===============================================================================

OPERATOR CONSOLE ONLY COMMANDS

These commands can only be entered from stdin or by a remote operator datagram
===============================================================================
*/

/*
====================
SV_SetMaster_f

Specify a list of master servers
====================
*/
void SV_SetMaster_f (void)
{
	int		i, slot;

	// only dedicated servers send heartbeats
	if (!dedicated->integer)
	{
		Com_Printf ("Only dedicated servers use masters.\n");
		return;
	}

	// make sure the server is listed public
	Cvar_Set ("public", "1");

	for (i=0 ; i<MAX_MASTERS ; i++)
		memset (&master_adr[i], 0, sizeof(master_adr[0]));

	slot = 0;
	for (i=1 ; i<Cmd_Argc() ; i++)
	{
		if (slot == MAX_MASTERS) {
		    Com_Printf ("Too many masters.\n");
			break;
        }

		if (!NET_StringToAdr (Cmd_Argv(i), &master_adr[slot]))
		{
			Com_Printf ("Bad address: %s\n", Cmd_Argv(i));
			continue;
		}
		if (master_adr[slot].port == 0)
			master_adr[slot].port = BigShort (PORT_MASTER);

		Com_Printf ("Master server at %s\n", NET_AdrToString (&master_adr[slot]));

		//Com_Printf ("Sending a ping.\n");

		//Netchan_OutOfBandPrint (NS_SERVER, &master_adr[slot], "ping");

		slot++;
	}

	svs.last_heartbeat = -9999999;
}

static const char *SV_SetPlayer_g( const char *partial, int state ) {
    static int length;
    static int index;
    client_t *client;

	if( !svs.initialized ) {
		return NULL;
	}
    
    if( !state ) {
        length = strlen( partial );
        index = 0;
    }

	while( index < sv_maxclients->integer ) {
		client = &svs.clientpool[index++];
		if( !client->state || client->protocol == -1 ) {
			continue;
		}
		if( !strncmp( partial, client->name, length ) ) {
			return client->name;
		}
	}

    return NULL;
}


/*
==================
SV_SetPlayer

Sets sv_client and sv_player to the player with idnum Cmd_Argv(1)
==================
*/
qboolean SV_SetPlayer (void)
{
	client_t	*cl;
	int			i;
	int			idnum;
	char		*s;

	if (Cmd_Argc() < 2)
		return qfalse;

	s = Cmd_Argv(1);

	// numeric values are just slot numbers
	for( i = 0; s[i]; i++ ) {
		if( !Q_isdigit( s[i] ) ) {
			break;
		}
	} 
	if( !s[i] ) {
		idnum = atoi(s);
		if (idnum < 0 || idnum >= sv_maxclients->integer)
		{
			Com_Printf ("Bad client slot: %i\n", idnum);
			return qfalse;
		}

		sv_client = &svs.clientpool[idnum];
		sv_player = sv_client->edict;
		if (!sv_client->state)
		{
			Com_Printf ("Client %i is not active\n", idnum);
			return qfalse;
		}
		return qtrue;
	}

	// check for a name match
    for( i = 0; i < sv_maxclients->integer; i++ ) {
        cl = &svs.clientpool[i];
        if( !cl->state ) {
            continue;
        }
		if (!strcmp(cl->name, s))
		{
			sv_client = cl;
			sv_player = sv_client->edict;
			return qtrue;
		}
	}

	Com_Printf ("Userid %s is not on the server\n", s);
	return qfalse;
}


/*
===============================================================================

SAVEGAME FILES

===============================================================================
*/

#define GAME_SEARCH_FLAGS	( FS_SEARCH_BYFILTER | FS_SEARCH_SAVEPATH | FS_PATH_GAME | FS_TYPE_REAL )

/*
=====================
SV_WipeSavegame

Delete save/<XXX>/
=====================
*/
void SV_WipeSavegame( const char *savename ) {
	char	name[MAX_QPATH];
	char	**list, **s;

	Com_DPrintf( "SV_WipeSaveGame( %s )\n", savename );

	Com_sprintf( name, sizeof( name ), "save/%s/server.ssv", savename );
	FS_RemoveFile( name );
	
	Com_sprintf( name, sizeof( name ), "save/%s/game.ssv", savename );
	FS_RemoveFile( name );

	Com_sprintf( name, sizeof( name ), "save/%s/*.sav", savename );
	if( ( list = FS_ListFiles( NULL, name, GAME_SEARCH_FLAGS, NULL ) ) != NULL ) {
		s = list;
		while( *s ) {
			FS_RemoveFile( *s );
			s++;
		}
		FS_FreeFileList( list );
	}

	Com_sprintf( name, sizeof( name ), "save/%s/*.sv2", savename );
	if( ( list = FS_ListFiles( NULL, name, GAME_SEARCH_FLAGS, NULL ) ) != NULL ) {
		s = list;
		while( *s ) {
			FS_RemoveFile( *s );
			s++;
		}
		FS_FreeFileList( list );
	}

}

/*
================
SV_CopySaveGame
================
*/
void SV_CopySaveGame( const char *src, const char *dst ) {
	char	name[MAX_QPATH], name2[MAX_QPATH];
	int		len;
	char	**list, **s;

	Com_DPrintf( "SV_CopySaveGame(%s, %s)\n", src, dst );

	SV_WipeSavegame( dst );

	// copy the savegame over
	Com_sprintf( name, sizeof( name ), "save/%s/server.ssv", src );
	Com_sprintf( name2, sizeof( name2 ), "save/%s/server.ssv", dst );
	FS_CopyFile( name, name2 );

	Com_sprintf( name, sizeof( name ), "save/%s/game.ssv", src );
	Com_sprintf( name2, sizeof( name2 ), "save/%s/game.ssv", dst );
	FS_CopyFile( name, name2 );

	Com_sprintf( name, sizeof( name ), "save/%s/", src );
	len = strlen( name );

	Com_sprintf( name, sizeof( name ), "save/%s/*.sav", src );
	if( ( list = FS_ListFiles( NULL, name, GAME_SEARCH_FLAGS, NULL ) ) != NULL ) {
		s = list;
		while( *s ) {
			Com_sprintf( name2, sizeof( name2 ), "save/%s/%s", dst, *s + len );
			FS_CopyFile( *s, name2 );
			s++;
		}
		FS_FreeFileList( list );
	}

	Com_sprintf( name, sizeof( name ), "save/%s/*.sv2", src );
	if( ( list = FS_ListFiles( NULL, name, GAME_SEARCH_FLAGS, NULL ) ) != NULL ) {
		s = list;
		while( *s ) {
			Com_sprintf( name2, sizeof( name2 ), "save/%s/%s", dst, *s + len );
			FS_CopyFile( *s, name2 );
			s++;
		}
		FS_FreeFileList( list );
	}
}


/*
==============
SV_WriteLevelFile

==============
*/
void SV_WriteLevelFile (void)
{
	char	name[MAX_OSPATH];
	fileHandle_t f;

	Com_DPrintf("SV_WriteLevelFile()\n");

	Com_sprintf (name, sizeof(name), "save/current/%s.sv2", sv.name);
	FS_FOpenFile( name, &f, FS_MODE_WRITE );
	if (!f)
	{
		Com_Printf ("Failed to open %s\n", name);
		return;
	}

	FS_Write (sv.configstrings, sizeof(sv.configstrings), f);
	CM_WritePortalState (&sv.cm, f);
	FS_FCloseFile (f);

	Com_sprintf (name, sizeof(name), "%s/save/current/%s.sav", FS_Gamedir(), sv.name);
	ge->WriteLevel (name);
}

/*
==============
SV_ReadLevelFile

==============
*/
void SV_ReadLevelFile (void)
{
	char	name[MAX_OSPATH];
	fileHandle_t f;

	Com_DPrintf("SV_ReadLevelFile()\n");

	Com_sprintf (name, sizeof(name), "save/current/%s.sv2", sv.name);
	
	FS_FOpenFile( name, &f, FS_MODE_READ|FS_TYPE_REAL );
	if( !f ) {
		Com_Printf ("Failed to open %s\n", name);
		return;
	}
	FS_Read( sv.configstrings, sizeof( sv.configstrings ), f );
	*( ( byte * )sv.configstrings + sizeof( sv.configstrings ) - 1 ) = 0;
	
	CM_ReadPortalState (&sv.cm, f);
	FS_FCloseFile (f);

	Com_sprintf (name, sizeof(name), "%s/save/current/%s.sav", FS_Gamedir(), sv.name);
	ge->ReadLevel (name);
}

/*
==============
SV_WriteServerFile

==============
*/
void SV_WriteServerFile (qboolean autosave)
{
	fileHandle_t f;
	cvar_t	*var;
	char	name[MAX_OSPATH], string[128];
	char	comment[MAX_QPATH]; // increased from 32 to avoid Com_sprintf warnings
	time_t	aclock;
	struct tm	*newtime;

	Com_DPrintf( "SV_WriteServerFile( %i )\n", autosave );

	FS_FOpenFile( "save/current/server.ssv", &f, FS_MODE_WRITE );
	if( !f ) {
		Com_WPrintf( "Couldn't write save/current/server.ssv\n" );
		return;
	}
	// write the comment field
	memset (comment, 0, sizeof(comment));

	if (!autosave)
	{
		time (&aclock);
		newtime = localtime (&aclock);
		Com_sprintf (comment,sizeof(comment), "%2i:%i%i %2i/%2i  ", newtime->tm_hour
			, newtime->tm_min/10, newtime->tm_min%10,
			newtime->tm_mon+1, newtime->tm_mday);
		strncat (comment, sv.configstrings[CS_NAME], sizeof(comment)-1-strlen(comment) );
	}
	else
	{	// autosaved
		Com_sprintf( comment, sizeof( comment ), "ENTERING %s", sv.configstrings[CS_NAME] );
	}

	// write 32 bytes instead of 64 for compatibility
	FS_Write (comment, 32, f);

	// write the mapcmd
	FS_Write (svs.mapcmd, sizeof(svs.mapcmd), f);

	// write all CVAR_LATCH cvars
	// these will be things like coop, skill, deathmatch, etc
	for (var = cvar_vars ; var ; var=var->next)
	{
		if (!(var->flags & CVAR_LATCH))
			continue;
		if (strlen(var->name) >= sizeof(name)-1
			|| strlen(var->string) >= sizeof(string)-1)
		{
			Com_Printf ("Cvar too long: %s = %s\n", var->name, var->string);
			continue;
		}
		memset (name, 0, sizeof(name));
		memset (string, 0, sizeof(string));
		strcpy (name, var->name);
		strcpy (string, var->string);
		FS_Write (name, sizeof(name), f);
		FS_Write (string, sizeof(string), f);
	}

	FS_FCloseFile (f);

	// write game state
	Com_sprintf (name, sizeof(name), "%s/save/current/game.ssv", FS_Gamedir());
	ge->WriteGame (name, autosave);
}

/*
==============
SV_ReadServerFile

==============
*/
void SV_ReadServerFile (void)
{
	fileHandle_t f;
	char	name[MAX_OSPATH], string[128];
	char	comment[32];
	char	mapcmd[MAX_TOKEN_CHARS];

	Com_DPrintf("SV_ReadServerFile()\n");

	Com_sprintf (name, sizeof(name), "save/current/server.ssv");
	FS_FOpenFile( name, &f, FS_MODE_READ|FS_TYPE_REAL|FS_PATH_GAME );
	if (!f)
	{
		Com_Printf ("Couldn't read %s\n", name);
		return;
	}
	// read the comment field
	FS_Read (comment, sizeof(comment), f);

	// read the mapcmd
	FS_Read (mapcmd, sizeof(mapcmd), f);

	// read all CVAR_LATCH cvars
	// these will be things like coop, skill, deathmatch, etc
	while (1)
	{
		if (!FS_Read (name, sizeof(name), f))
			break;
		FS_Read (string, sizeof(string), f);
		Com_DPrintf ("Set %s = %s\n", name, string);
		Cvar_Set (name, string);
	}

	FS_FCloseFile (f);

	// start a new game fresh with new cvars
	SV_InitGame ( 0 );

	strcpy (svs.mapcmd, mapcmd);

	// read game state
	Com_sprintf (name, sizeof(name), "%s/save/current/game.ssv", FS_Gamedir());
	ge->ReadGame (name);

}


//=========================================================


/*
==================
SV_DemoMap_f

Puts the server in demo mode on a specific map/cinematic
==================
*/
void SV_DemoMap_f( void ) {
	if( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: %s <mapname>\n", Cmd_Argv( 0 ) );
		return;
	}
	SV_Map( ATR_DEMO, Cmd_Argv( 1 ), qfalse );
}

/*
==================
SV_GameMap_f

Saves the state of the map just being exited and goes to a new map.

If the initial character of the map string is '*', the next map is
in a new unit, so the current savegame directory is cleared of
map files.

Example:

*inter.cin+jail

Clears the archived maps, plays the inter.cin cinematic, then
goes to map jail.bsp.
==================
*/
void SV_GameMap_f (void)
{
	char		*map;
	int			i;
	client_t	*cl;
	qboolean	*savedInuse;
	char		expanded[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		Com_Printf( "Usage: %s <mapname>\n", Cmd_Argv( 0 ) );
		return;
	}

	Com_DPrintf("SV_GameMap(%s)\n", Cmd_Argv(1));

	FS_CreatePath (va("%s/save/current/", FS_Gamedir()));

	// check for clearing the current savegame
	map = Cmd_Argv(1);
	if (map[0] == '*')
	{
		// wipe all the *.sav files
		SV_WipeSavegame ("current");
	}
	else
	{
		// if not a pcx, demo, or cinematic, check to make sure the level exists
		if( !strchr( map, '.' ) && !strchr( map, '$' ) ) {
			Com_sprintf( expanded, sizeof( expanded ), "maps/%s.bsp", map );
			if( FS_LoadFile( expanded, NULL ) == -1 ) {
				Com_Printf( "Can't find %s\n", expanded );
				return;
			}
		}

		// save the map just exited
		if (sv.state == ss_game)
		{
			// clear all the client inuse flags before saving so that
			// when the level is re-entered, the clients will spawn
			// at spawn points instead of occupying body shells
			savedInuse = SV_Malloc(sv_maxclients->integer * sizeof(qboolean));
			for (i=0,cl=svs.clientpool ; i<sv_maxclients->integer; i++,cl++)
			{
				savedInuse[i] = cl->edict->inuse;
				cl->edict->inuse = qfalse;
			}

			SV_WriteLevelFile ();

			// we must restore these for clients to transfer over correctly
			for (i=0,cl=svs.clientpool ; i<sv_maxclients->integer; i++,cl++)
				cl->edict->inuse = savedInuse[i];
			Z_Free (savedInuse);
		}
	}

	// start up the next map
	SV_Map (ATR_NONE, Cmd_Argv(1), qfalse );

	// archive server state
	Q_strncpyz( svs.mapcmd, Cmd_Argv( 1 ), sizeof( svs.mapcmd ) );

	// copy off the level to the autosave slot
	if (!dedicated->integer)
	{
		SV_WriteServerFile (qtrue);
		SV_CopySaveGame ("current", "save0");
	}
}

/*
==================
SV_Map_f

Goes directly to a given map without any savegame archiving.
For development work
==================
*/
void SV_Map_f( void ) {
	char	*map;
	char	expanded[MAX_QPATH];

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: %s <mapname>\n", Cmd_Argv( 0 ) );
		return;
	}

	// if not a pcx, demo, or cinematic, check to make sure the level exists
	map = Cmd_Argv( 1 );
	if( !strchr( map, '.' ) ) {
		Com_sprintf( expanded, sizeof( expanded ), "maps/%s.bsp", map );
		if( FS_LoadFile( expanded, NULL ) == -1 ) {
			Com_Printf( "Can't find %s\n", expanded );
			return;
		}
	}

	sv.state = ss_dead;		// don't save current level when changing
	SV_WipeSavegame( "current" );
	SV_GameMap_f();
}

static const char *SV_Map_g( const char *partial, int state ) {
	return Com_FileNameGenerator( "maps", ".bsp", partial, qtrue, state );
}

/*
=====================================================================

  SAVEGAMES

=====================================================================
*/


/*
==============
SV_Loadgame_f

==============
*/
void SV_Loadgame_f (void)
{
	char	name[MAX_QPATH];
	char	*dir;

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("USAGE: loadgame <directory>\n");
		return;
	}

	Com_Printf ("Loading game...\n");

	dir = Cmd_Argv(1);
	if (strstr (dir, "..") || strstr (dir, "/") || strstr (dir, "\\") )
	{
		Com_Printf ("Bad savedir.\n");
	}

	// make sure the server.ssv file exists
	Com_sprintf (name, sizeof(name), "save/%s/server.ssv", Cmd_Argv(1));
	if( FS_LoadFileEx( name, NULL, FS_TYPE_REAL|FS_PATH_GAME ) == -1 ) {
		Com_Printf ("No such savegame: %s\n", name);
		return;
	}

	SV_CopySaveGame (Cmd_Argv(1), "current");

	SV_ReadServerFile ();

	// go to the map
	sv.state = ss_dead;		// don't save current level when changing
	SV_Map (ATR_NONE, svs.mapcmd, qtrue);
}



/*
==============
SV_Savegame_f

==============
*/
void SV_Savegame_f (void)
{
	char	*dir;

	if (sv.state != ss_game)
	{
		Com_Printf ("You must be in a game to save.\n");
		return;
	}

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("USAGE: savegame <directory>\n");
		return;
	}

	if (Cvar_VariableValue("deathmatch"))
	{
		Com_Printf ("Can't savegame in a deathmatch\n");
		return;
	}

	if (!strcmp (Cmd_Argv(1), "current"))
	{
		Com_Printf ("Can't save to 'current'\n");
		return;
	}

	if (sv_maxclients->integer == 1 && svs.clientpool[0].edict && svs.clientpool[0].edict->client && svs.clientpool[0].edict->client->ps.stats[STAT_HEALTH] <= 0)
	{
		Com_Printf ("\nCan't savegame while dead!\n");
		return;
	}

	dir = Cmd_Argv(1);
	if (strstr (dir, "..") || strstr (dir, "/") || strstr (dir, "\\") )
	{
		Com_Printf ("Bad savedir.\n");
	}

	Com_Printf ("Saving game...\n");

	// archive current level, including all client edicts.
	// when the level is reloaded, they will be shells awaiting
	// a connecting client
	SV_WriteLevelFile ();

	// save server state
	SV_WriteServerFile (qfalse);

	// copy it off
	SV_CopySaveGame ("current", dir);

	Com_Printf ("Done.\n");
}

//===============================================================

/*
==================
SV_Kick_f

Kick a user off of the server
==================
*/
void SV_Kick_f( void ) {
	if( !svs.initialized ) {
		Com_Printf( "No server running.\n" );
		return;
	}

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: %s <userid>\n", Cmd_Argv( 0 ) );
		return;
	}

	if( !SV_SetPlayer() )
		return;

	SV_DropClient( sv_client, "was kicked" );
	sv_client->lastmessage = svs.realtime;	// min case there is a funny zombie

	sv_client = NULL;
	sv_player = NULL;
}

static void SV_DumpUdpClients( void ) {
	client_t	*client;

	Com_Printf(
"num score ping name             lastmsg address                rate proto\n"
"--- ----- ---- ---------------- ------- --------------------- ----- -----\n" );
    FOR_EACH_CLIENT( client ) {
		Com_Printf( "%3i ", client->number );
		if( sv.state == ss_broadcast ) {
			Com_Printf( "      " );
		} else {
			Com_Printf( "%5i ", client->edict->client->ps.stats[STAT_FRAGS] );
		}

		switch( client->state ) {
		case cs_zombie:
			Com_Printf( "ZMBI " );
			break;
		case cs_assigned:
			Com_Printf( "ASGN " );
			break;
		case cs_connected:
			Com_Printf( "CNCT " );
			break;
		case cs_primed:
			Com_Printf( "PRIM " );
			break;
		default:
			Com_Printf( "%4i ", client->ping < 9999 ? client->ping : 9999 );
			break;
		}

		Com_Printf( "%-16.16s ", client->name );
		Com_Printf( "%7u ", svs.realtime - client->lastmessage );
		Com_Printf( "%-21s ", NET_AdrToString(
            &client->netchan->remote_address ) );
    	Com_Printf( "%5i ", client->rate );
    	Com_Printf( "%2i/%d ", client->protocol, client->netchan->type );
		Com_Printf( "\n" );
	}

}

static void SV_DumpUdpVersions( void ) {
	client_t	*client;

	Com_Printf(
"num name             version\n"
"--- ---------------- -----------------------------------------\n" );
		
    FOR_EACH_CLIENT( client ) {
        Com_Printf( "%3i %-16.16s %-40.40s\n",
            client->number, client->name,
            client->versionString ? client->versionString : "" );
    }
}

static void SV_DumpTcpClients( void ) {
	tcpClient_t	*client;
    int count;

	Com_Printf(
"num resource             buf lastmsg address               state\n"
"--- -------------------- --- ------- --------------------- -----\n" );
    count = 0;
    LIST_FOR_EACH( tcpClient_t, client, &svs.tcpClients, entry ) {
        Com_Printf( "%3d %-20.20s %3d %7u %-21s ",
            count, client->resource ? client->resource : "",
            FIFO_Usage( &client->stream.send ),
            svs.realtime - client->lastmessage,
            NET_AdrToString( &client->stream.address ) );

		switch( client->state ) {
		case cs_zombie:
			Com_Printf( "ZMBI " );
			break;
		case cs_assigned:
			Com_Printf( "ASGN " );
			break;
		case cs_connected:
			Com_Printf( "CNCT " );
			break;
		default:
			Com_Printf( "SEND " );
			break;
		}
        Com_Printf( "\n" );

        count++;
    }
}

static void SV_DumpTcpVersions( void ) {
	tcpClient_t	*client;
    int count;

	Com_Printf(
"num address               user-agent\n"
"--- --------------------- -----------------------------------------\n" );
		
    count = 0;
    LIST_FOR_EACH( tcpClient_t, client, &svs.tcpClients, entry ) {
        Com_Printf( "%3i %-21s %-40.40s\n",
            count, NET_AdrToString( &client->stream.address ),
            client->agent ? client->agent : "" );
        count++;
    }
}


/*
================
SV_Status_f
================
*/
static void SV_Status_f( void ) {
	if( !svs.initialized ) {
		Com_Printf( "No server running.\n" );
		return;
	}

	if( sv.name[0] ) {
	    Com_Printf( "Current map: %s\n\n", sv.name );
    }

    if( LIST_EMPTY( &svs.clients ) ) {
        Com_Printf( "No UDP clients.\n" );
    } else {
	    if( Cmd_Argc() > 1 ) {
            SV_DumpUdpVersions();
        } else {
            SV_DumpUdpClients();
        }
    }
    Com_Printf( "\n" );

    if( LIST_EMPTY( &svs.tcpClients ) ) {
        Com_Printf( "No TCP clients.\n" );
    } else {
	    if( Cmd_Argc() > 1 ) {
            SV_DumpTcpVersions();
        } else {
            SV_DumpTcpClients();
        }
    }
    Com_Printf( "\n" );
}

/*
==================
SV_ConSay_f
==================
*/
void SV_ConSay_f( void ) {
	client_t *client;
	char *s;

	if( !svs.initialized ) {
		Com_Printf( "No server running\n" );
		return;
	}

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <text>\n", Cmd_Argv( 0 ) );
		return;
	}

	s = Cmd_RawArgs();
    FOR_EACH_CLIENT( client ) {
		if( client->state != cs_spawned )
			continue;
		SV_ClientPrintf( client, PRINT_CHAT, "console: %s\n", s );
	}

	Com_Printf( "console: %s\n", s );
}


/*
==================
SV_Heartbeat_f
==================
*/
static void SV_Heartbeat_f( void ) {
	svs.last_heartbeat = -9999999;
}


/*
===========
SV_Serverinfo_f

  Examine or change the serverinfo string
===========
*/
static void SV_Serverinfo_f( void ) {
	Com_Printf( "Server info settings:\n" );
	Info_Print( Cvar_Serverinfo() );
}


/*
===========
SV_DumpUser_f

Examine all a users info strings
===========
*/
static void SV_DumpUser_f( void ) {
	if( !svs.initialized ) {
		Com_Printf( "No server running\n" );
		return;
	}

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: %s <userid>\n", Cmd_Argv( 0 ) );
		return;
	}

	if( !SV_SetPlayer() )
		return;

	Com_Printf( "userinfo\n" );
	Com_Printf( "--------\n" );
	Info_Print( sv_client->userinfo );
	if( sv_client->versionString ) {
		Com_Printf( "version              %s\n", sv_client->versionString );
	}

	sv_client = NULL;
	sv_player = NULL;

}

/*
==================
SV_Stuff_f

Stuff raw command string to the client.
==================
*/
void SV_Stuff_f( void ) {
	if( !svs.initialized ) {
		Com_Printf( "No server running.\n" );
		return;
	}

	if( Cmd_Argc() < 3 ) {
		Com_Printf( "Usage: %s <userid> <text>\n", Cmd_Argv( 0 ) );
		return;
	}

	if( !SV_SetPlayer() )
		return;

	MSG_WriteByte( svc_stufftext );
	MSG_WriteString( Cmd_RawArgsFrom( 2 ) );
	SV_ClientAddMessage( sv_client, MSG_RELIABLE|MSG_CLEAR );

	sv_client = NULL;
	sv_player = NULL;
}

/*
==================
SV_Stuff_f

Stuff raw command string to all clients.
==================
*/
void SV_Stuffall_f( void ) {
	client_t *client;

	if( !svs.initialized ) {
		Com_Printf( "No server running.\n" );
		return;
	}

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <text>\n", Cmd_Argv( 0 ) );
		return;
	}

	MSG_WriteByte( svc_stufftext );
	MSG_WriteString( Cmd_RawArgsFrom( 1 ) );

    FOR_EACH_CLIENT( client ) {
		SV_ClientAddMessage( client, MSG_RELIABLE );
	}

	SZ_Clear( &msg_write );

}

void SVC_GetChallenge( void );

static void SV_PickClient_f( void ) {
    char	*s;
    netadr_t address;

	if( !svs.initialized ) {
		Com_Printf( "No server running.\n" );
		return;
	}
    if( sv_maxclients->integer == 1 ) {
		Com_Printf( "Single player server running.\n" );
		return;
    }

    if ( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <address>\n", Cmd_Argv( 0 ) );
        return;
    }

    s = Cmd_Argv( 1 );
    if ( !NET_StringToAdr( s, &address ) ) {
        Com_Printf( "Bad client address: %s\n", s );
        return;
    }
    if ( address.port == 0 ) {
        Com_Printf( "Please specify client port explicitly.\n" );
        return;
    }

	Netchan_OutOfBandPrint( NS_SERVER, &address, "passive_connect\n" );
}


/*
===============
SV_KillServer_f

Kick everyone off, possibly in preparation for a new game

===============
*/
void SV_KillServer_f( void ) {
	if( !svs.initialized ) {
		Com_Printf( "No server running.\n" );
		return;
	}

	SV_Shutdown( "Server was killed.\n", KILL_DROP );
}

/*
===============
SV_ServerCommand_f

Let the game dll handle a command
===============
*/
void SV_ServerCommand_f( void ) {
	if( !ge ) {
		Com_Printf( "No game loaded.\n" );
		return;
	}

	ge->ServerCommand();
}

static void SV_Client_m( char *buffer, int bufferSize ) {
	if( !sv_client ) {
		Q_strncpyz( buffer, "unknown", bufferSize );
		return;
	}

	Q_strncpyz( buffer, sv_client->name, bufferSize );
}

//===========================================================

static const cmdreg_t c_server[] = {
	{ "heartbeat", SV_Heartbeat_f },
	{ "kick", SV_Kick_f, SV_SetPlayer_g },
	{ "status", SV_Status_f },
	{ "serverinfo", SV_Serverinfo_f },
	{ "dumpuser", SV_DumpUser_f, SV_SetPlayer_g },
	{ "stuff", SV_Stuff_f, SV_SetPlayer_g },
	{ "stuffall", SV_Stuffall_f },
	{ "map", SV_Map_f, SV_Map_g },
	{ "demomap", SV_DemoMap_f },
	{ "gamemap", SV_GameMap_f, SV_Map_g },
	{ "setmaster", SV_SetMaster_f },
	{ "save", SV_Savegame_f },
	{ "load", SV_Loadgame_f },
	{ "killserver", SV_KillServer_f },
	{ "sv", SV_ServerCommand_f },
	{ "pick", SV_PickClient_f },

    { NULL }
};


/*
==================
SV_InitOperatorCommands
==================
*/
void SV_InitOperatorCommands( void ) {
    Cmd_Register( c_server );

	if ( dedicated->integer )
		Cmd_AddCommand( "say", SV_ConSay_f );


	Cmd_AddMacro ("sv_client", SV_Client_m);

}
 

