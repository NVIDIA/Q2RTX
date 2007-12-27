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
static void SV_SetMaster_f( void ) {
	int		i, slot;

	// only dedicated servers send heartbeats
	if( !dedicated->integer ) {
		Com_Printf( "Only dedicated servers use masters.\n" );
		return;
	}

	// make sure the server is listed public
	Cvar_Set( "public", "1" );

	for( i = 0; i < MAX_MASTERS; i++ )
		memset( &master_adr[i], 0, sizeof( master_adr[0] ) );

	slot = 0;
	for( i = 1; i < Cmd_Argc(); i++) {
		if( slot == MAX_MASTERS ) {
		    Com_Printf( "Too many masters.\n" );
			break;
        }

		if( !NET_StringToAdr( Cmd_Argv( i ), &master_adr[slot] ) ) {
			Com_Printf( "Bad address: %s\n", Cmd_Argv( i ) );
			continue;
		}
		if( master_adr[slot].port == 0 )
			master_adr[slot].port = BigShort( PORT_MASTER );

		Com_Printf( "Master server at %s\n", NET_AdrToString( &master_adr[slot] ) );

		//Com_Printf ("Sending a ping.\n");

		//Netchan_OutOfBandPrint (NS_SERVER, &master_adr[slot], "ping");

		slot++;
	}

	svs.last_heartbeat = 0;
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


//=========================================================


/*
==================
SV_DemoMap_f

Puts the server in demo mode on a specific map/cinematic
==================
*/
static void SV_DemoMap_f( void ) {
    Com_Printf( "This command is no longer supported.\n"
        "To play a demo, use 'demo' command instead.\n" );
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
static void SV_GameMap_f( void ) {
	if( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: %s <mapname>\n", Cmd_Argv( 0 ) );
		return;
	}

	SV_Map( Cmd_Argv( 1 ), qfalse );
}

/*
==================
SV_Map_f

Goes directly to a given map without any savegame archiving.
For development work
==================
*/
static void SV_Map_f( void ) {
	if( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: %s <mapname>\n", Cmd_Argv( 0 ) );
		return;
	}

	SV_Map( Cmd_Argv( 1 ), qtrue );
}

static const char *SV_Map_g( const char *partial, int state ) {
	return Com_FileNameGenerator( "maps", ".bsp", partial, qtrue, state );
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

	SV_DropClient( sv_client, "kicked" );
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
    	Com_Printf( "%2i/%d/%d ", client->protocol,
            client->version, client->netchan->type );
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
    LIST_FOR_EACH( tcpClient_t, client, &svs.tcp_client_list, entry ) {
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
    LIST_FOR_EACH( tcpClient_t, client, &svs.tcp_client_list, entry ) {
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

    if( LIST_EMPTY( &svs.tcp_client_list ) ) {
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
		Com_Printf( "Usage: %s <raw text>\n", Cmd_Argv( 0 ) );
		return;
	}

	s = Cmd_RawArgs();
    FOR_EACH_CLIENT( client ) {
		if( client->state != cs_spawned )
			continue;
		SV_ClientPrintf( client, PRINT_CHAT, "console: %s\n", s );
	}

    if( dedicated->integer ) {
    	Com_Printf( "console: %s\n", s );
    }
}


/*
==================
SV_Heartbeat_f
==================
*/
static void SV_Heartbeat_f( void ) {
	svs.last_heartbeat = 0;
}


/*
===========
SV_Serverinfo_f

  Examine or change the serverinfo string
===========
*/
static void SV_Serverinfo_f( void ) {
    char serverinfo[MAX_INFO_STRING];

    Cvar_BitInfo( serverinfo, CVAR_SERVERINFO );

	Com_Printf( "Server info settings:\n" );
	Info_Print( serverinfo );
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
		Com_Printf( "Usage: %s <userid> <raw text>\n", Cmd_Argv( 0 ) );
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
		Com_Printf( "Usage: %s <raw text>\n", Cmd_Argv( 0 ) );
		return;
	}

	MSG_WriteByte( svc_stufftext );
	MSG_WriteString( Cmd_RawArgsFrom( 1 ) );

    FOR_EACH_CLIENT( client ) {
		SV_ClientAddMessage( client, MSG_RELIABLE );
	}

	SZ_Clear( &msg_write );

}

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

	OOB_PRINT( NS_SERVER, &address, "passive_connect\n" );
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
static void SV_ServerCommand_f( void ) {
	if( !ge ) {
		Com_Printf( "No game loaded.\n" );
		return;
	}

	ge->ServerCommand();
}

// ( ip & mask ) == ( addr & mask )
// bits = 32 --> mask = 255.255.255.255
// bits = 24 --> mask = 255.255.255.0

static qboolean SV_ParseMask( const char *s, uint32 *addr, uint32 *mask ) {
    netadr_t address;
    char *p;
    int bits;

    p = strchr( s, '/' );
    if( p ) {
        *p++ = 0;
        if( *p == 0 ) {
            Com_Printf( "Please specify a mask.\n" );
            return qfalse;
        }
        bits = atoi( p );
        if( bits < 1 || bits > 32 ) {
            Com_Printf( "Bad mask: %d bits\n", bits );
            return qfalse;
        }
    } else {
        bits = 32;
    }

    if( !NET_StringToAdr( s, &address ) ) {
        Com_Printf( "Bad address: %s\n", s );
        return qfalse;
    }

    *addr = *( uint32 * )address.ip;
    *mask = 0xffffffffU >> ( 32 - bits ); // LE
    return qtrue;
}

void SV_AddMatch_f( list_t *list ) {
    char	*s;
    addrmatch_t *match;
    uint32 addr, mask;

    if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <address/mask>\n", Cmd_Argv( 0 ) );
        return;
    }

    s = Cmd_Argv( 1 );
    if( !SV_ParseMask( s, &addr, &mask ) ) {
        return;
    }

#if 0
    LIST_FOR_EACH( addrmatch_t, match, list, entry ) {
        if( ( match->addr & match->mask ) == ( addr & mask ) ) {
            Com_Printf( "Address already matches.\n" );
            return;
        }
    }
#endif

    match = Z_Malloc( sizeof( *match ) );
    match->addr = addr;
    match->mask = mask;
    List_Append( list, &match->entry );
}

void SV_DelMatch_f( list_t *list ) {
    char	*s;
    addrmatch_t *match, *next;
    uint32 addr, mask;
    int i;

    if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <address/mask|id|all>\n", Cmd_Argv( 0 ) );
        return;
    }

    if( LIST_EMPTY( list ) ) {
        Com_Printf( "Address list is empty.\n" );
        return;
    }

    s = Cmd_Argv( 1 );
    if( !strcmp( s, "all" ) ) {
        LIST_FOR_EACH_SAFE( addrmatch_t, match, next, list, entry ) {
            Z_Free( match );
        }
        List_Init( list );
        Com_Printf( "Address list cleared.\n" );
        return;
    }

	// numeric values are just slot numbers
	for( i = 0; s[i]; i++ ) {
		if( !Q_isdigit( s[i] ) ) {
			break;
		}
	} 
	if( !s[i] ) {
		i = atoi( s );
        if( i < 1 ) {
            Com_Printf( "Bad index: %d\n", i );
            return;
        }
        match = LIST_INDEX( addrmatch_t, i + 1, list, entry );
        if( match ) {
            goto remove;
        }
        Com_Printf( "No such index: %d\n", i );
        return;
    }

    if( !SV_ParseMask( s, &addr, &mask ) ) {
        return;
    }

    LIST_FOR_EACH( addrmatch_t, match, list, entry ) {
        if( match->addr == addr && match->mask == mask ) {
remove:
            Com_Printf( "Address removed from list.\n" );
            List_Remove( &match->entry );
            Z_Free( match );
            return;
        }
    }
    Com_Printf( "Address/mask pair not found in list.\n" );
}

void SV_ListMatches_f( list_t *list ) {
    addrmatch_t *match;
    byte ip[4];
    int i, count;

    if( LIST_EMPTY( list ) ) {
        Com_Printf( "Address list is empty.\n" );
        return;
    }

    count = 1;
    LIST_FOR_EACH( addrmatch_t, match, list, entry ) {
        *( uint32 * )ip = match->addr;
        for( i = 0; i < 32; i++ ) {
            if( ( match->mask & ( 1 << i ) ) == 0 ) {
                break;
            }
        }
        Com_Printf( "(%d) %d.%d.%d.%d/%d\n",
            count, ip[0], ip[1], ip[2], ip[3], i );
        count++;
    }
}

static void SV_AddBan_f( void ) {
    SV_AddMatch_f( &sv_banlist );
}
static void SV_DelBan_f( void ) {
    SV_DelMatch_f( &sv_banlist );
}
static void SV_ListBans_f( void ) {
    SV_ListMatches_f( &sv_banlist );
}

static int SV_Client_m( char *buffer, int size ) {
	if( !sv_client ) {
		return Q_strncpyz( buffer, "unknown", size );
	}
	return Q_strncpyz( buffer, sv_client->name, size );
}

static int SV_ClientNum_m( char *buffer, int size ) {
	if( !sv_client ) {
		return Q_strncpyz( buffer, "", size );
	}
    return Com_sprintf( buffer, size, "%d", sv_client - svs.clientpool );
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
	{ "killserver", SV_KillServer_f },
	{ "sv", SV_ServerCommand_f },
	{ "pick", SV_PickClient_f },
	{ "addban", SV_AddBan_f },
	{ "delban", SV_DelBan_f },
	{ "listbans", SV_ListBans_f },

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
	Cmd_AddMacro ("sv_curid", SV_ClientNum_m);

}
 

