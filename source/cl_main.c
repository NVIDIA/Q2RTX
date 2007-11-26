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
// cl_main.c  -- client main loop

#include "cl_local.h"

cvar_t	*adr0;
cvar_t	*adr1;
cvar_t	*adr2;
cvar_t	*adr3;
cvar_t	*adr4;
cvar_t	*adr5;
cvar_t	*adr6;
cvar_t	*adr7;
cvar_t	*adr8;

extern cvar_t	*rcon_password;
cvar_t	*rcon_address;

cvar_t	*cl_noskins;
cvar_t	*cl_autoskins;
cvar_t	*cl_footsteps;
cvar_t	*cl_timeout;
cvar_t	*cl_predict;
cvar_t	*cl_gun;
cvar_t	*cl_maxfps;
cvar_t	*cl_async;
cvar_t	*r_maxfps;

cvar_t	*cl_add_particles;
cvar_t	*cl_add_lights;
cvar_t	*cl_add_entities;
cvar_t	*cl_add_blend;
cvar_t	*cl_kickangles;

cvar_t	*cl_shownet;
cvar_t	*cl_showmiss;
cvar_t	*cl_showclamp;

cvar_t	*cl_thirdperson;
cvar_t	*cl_thirdperson_angle;
cvar_t	*cl_thirdperson_range;

cvar_t *cl_railtrail_type;
cvar_t *cl_railtrail_time;
cvar_t *cl_railtrail_alpha;
cvar_t *cl_railcore_color;
cvar_t *cl_railcore_width;
cvar_t *cl_railspiral_color;
cvar_t *cl_railspiral_radius;

cvar_t	*cl_disable_particles;
cvar_t	*cl_disable_explosions;
cvar_t	*cl_chat_notify;
cvar_t	*cl_chat_sound;
cvar_t	*cl_chat_filter;

cvar_t	*cl_disconnectcmd;
cvar_t	*cl_changemapcmd;
cvar_t	*cl_beginmapcmd;

cvar_t *cl_gibs;

cvar_t *cl_protocol;

cvar_t	*gender_auto;

cvar_t	*cl_vwep;

//
// userinfo
//
cvar_t	*info_password;
cvar_t	*info_spectator;
cvar_t	*info_name;
cvar_t	*info_skin;
cvar_t	*info_rate;
cvar_t	*info_fov;
cvar_t	*info_msg;
cvar_t	*info_hand;
cvar_t	*info_gender;
cvar_t	*info_uf;

client_static_t	cls;
client_state_t	cl;

clientAPI_t		client;

centity_t	cl_entities[ MAX_EDICTS ];

qboolean CL_SendStatusRequest( char *buffer, int bufferSize );

//======================================================================

typedef enum {
	REQ_FREE,
	REQ_STATUS,
	REQ_INFO,
	REQ_PING,
	REQ_RCON
} requestType_t;

typedef struct {
	requestType_t type;
	netadr_t adr;
	int time;
} request_t;

#define MAX_REQUESTS	32
#define REQUEST_MASK	( MAX_REQUESTS - 1 )

static request_t	clientRequests[MAX_REQUESTS];
static int			currentRequest;

static request_t *CL_AddRequest( netadr_t *adr, requestType_t type ) {
	request_t *r;

	r = &clientRequests[currentRequest & REQUEST_MASK];
	currentRequest++;

	r->adr = *adr;
	r->type = type;
	if( adr->type == NA_BROADCAST ) {
		r->time = cls.realtime + 3000;
	} else {
		r->time = cls.realtime + 6000;
	}

	return r;
}

/*
===================
CL_UpdateGunSetting
===================
*/
static void CL_UpdateGunSetting( void ) {
    int nogun;

    if ( cls.state < ca_connected || cls.state > ca_active ) {
        return;
    }

    if ( cls.serverProtocol < PROTOCOL_VERSION_R1Q2 ) {
        return;
    }

	if( cl_gun->integer == -1 ) {
		nogun = 2;
	} else if( cl_gun->integer == 0 || info_hand->integer == 2 ) {
		nogun = 1;
	} else {
		nogun = 0;
	}

    MSG_WriteByte( clc_setting );
    MSG_WriteShort( CLS_NOGUN );
    MSG_WriteShort( nogun );
    MSG_FlushTo( &cls.netchan->message );
}

/*
===================
CL_UpdateGibSetting
===================
*/
static void CL_UpdateGibSetting( void ) {
    if ( cls.state < ca_connected || cls.state > ca_active ) {
        return;
    }

    if ( cls.serverProtocol != PROTOCOL_VERSION_Q2PRO ) {
        return;
    }

    MSG_WriteByte( clc_setting );
    MSG_WriteShort( CLS_NOGIBS );
    MSG_WriteShort( !cl_gibs->integer );
    MSG_FlushTo( &cls.netchan->message );
}

/*
===================
CL_UpdateFootstepsSetting
===================
*/
static void CL_UpdateFootstepsSetting( void ) {
    if ( cls.state < ca_connected || cls.state > ca_active ) {
        return;
    }

    if ( cls.serverProtocol != PROTOCOL_VERSION_Q2PRO ) {
        return;
    }

    MSG_WriteByte( clc_setting );
    MSG_WriteShort( CLS_NOFOOTSTEPS );
    MSG_WriteShort( !cl_footsteps->integer );
    MSG_FlushTo( &cls.netchan->message );
}

/*
===================
CL_UpdatePredictSetting
===================
*/
static void CL_UpdatePredictSetting( void ) {
    if ( cls.state < ca_connected || cls.state > ca_active ) {
        return;
    }

    if ( cls.serverProtocol != PROTOCOL_VERSION_Q2PRO ) {
        return;
    }

    MSG_WriteByte( clc_setting );
    MSG_WriteShort( CLS_NOPREDICT );
    MSG_WriteShort( !cl_predict->integer );
    MSG_FlushTo( &cls.netchan->message );
}

/*
===================
CL_ClientCommand
===================
*/
void CL_ClientCommand( const char *string ) {
    if ( !cls.netchan ) {
        return;
    }
    MSG_WriteByte( clc_stringcmd );
    MSG_WriteString( string );
    MSG_FlushTo( &cls.netchan->message );
}


/*
===================
Cmd_ForwardToServer
 
adds the current command line as a clc_stringcmd to the client message.
things like godmode, noclip, etc, are commands directed to the server,
so when they are typed in at the console, they will need to be forwarded.
===================
*/
void Cmd_ForwardToServer( void ) {
    char	* cmd;

    cmd = Cmd_Argv( 0 );
    if ( cls.state < ca_active || *cmd == '-' || *cmd == '+' ) {
        Com_Printf( "Unknown command \"%s\"\n", cmd );
        return;
    }

    if ( cls.demoplayback ) {
        return;
    }

    CL_ClientCommand( Cmd_RawArgsFrom( 0 ) );
}

/*
==================
CL_ForwardToServer_f
==================
*/
void CL_ForwardToServer_f( void ) {
    if ( cls.state < ca_connected ) {
        Com_Printf( "Can't \"%s\", not connected\n", Cmd_Argv( 0 ) );
        return;
    }

    if ( cls.demoplayback ) {
        return;
    }

    // don't forward the first argument
    if ( Cmd_Argc() > 1 ) {
        CL_ClientCommand( Cmd_RawArgs() );
    }
}

void CL_Setenv_f( void ) {
    int argc = Cmd_Argc();

    if ( argc > 2 ) {
        char buffer[ MAX_STRING_CHARS ];

        Q_concat( buffer, sizeof( buffer ), Cmd_Argv( 1 ), "=",
            Cmd_ArgsFrom( 2 ), NULL );

        putenv( buffer );
    } else if ( argc == 2 ) {
        char * env = getenv( Cmd_Argv( 1 ) );

        if ( env ) {
            Com_Printf( "%s=%s\n", Cmd_Argv( 1 ), env );
        } else {
            Com_Printf( "%s undefined\n", Cmd_Argv( 1 ) );
        }
    }
}

/*
==================
CL_Pause_f
==================
*/
void CL_Pause_f( void ) {
    if( cl_paused->integer == 2 ) {
        if( cls.key_dest & (KEY_CONSOLE|KEY_MENU) ) {
            // activate automatic pause
            Cvar_Set( "cl_paused", "1" );
        } else {
            Cvar_Set( "cl_paused", "0" );
        }
    } else {
        // activate manual pause
        Cvar_Set( "cl_paused", "2" );
    }
}

/*
=================
CL_CheckForResend
 
Resend a connect message if the last one has timed out
=================
*/
static void CL_CheckForResend( void ) {
	neterr_t ret;
    char tail[MAX_QPATH];
    char userinfo[MAX_INFO_STRING];

    if ( cls.demoplayback ) {
        return;
    }

    // if the local server is running and we aren't
    // then connect
    if ( cls.state < ca_connecting && sv_running->integer > ss_loading ) {
        strcpy( cls.servername, "localhost" );
        cls.serverAddress.type = NA_LOOPBACK;
        cls.serverProtocol = cl_protocol->integer;
        if( cls.serverProtocol < PROTOCOL_VERSION_DEFAULT ||
            cls.serverProtocol > PROTOCOL_VERSION_Q2PRO )
        {
            cls.serverProtocol = PROTOCOL_VERSION_Q2PRO;
        }
    
        // we don't need a challenge on the localhost
        cls.state = ca_connecting;
        cls.connect_time = -9999;

        cls.passive = qfalse;
    }

    // resend if we haven't gotten a reply yet
    if ( cls.state != ca_connecting && cls.state != ca_challenging ) {
        return;
    }

    if ( cls.realtime - cls.connect_time < 3000 )
        return;

    cls.connect_time = cls.realtime;	// for retransmit requests

    cls.connectCount++;

    if ( cls.state == ca_challenging ) {
        Com_Printf( "Requesting challenge... %i\n", cls.connectCount );
        ret = OOB_PRINT( NS_CLIENT, &cls.serverAddress, "getchallenge\n" );
		if( ret == NET_ERROR ) {
			Com_Error( ERR_DISCONNECT, "%s to %s\n", NET_ErrorString(),
                    NET_AdrToString( &cls.serverAddress ) );
		}
        return;
    }

    //
    // We have gotten a challenge from the server, so try and connect.
    //
    Com_Printf( "Requesting connection... %i\n", cls.connectCount );

    cls.userinfo_modified = 0;

    // add protocol dependent stuff
	switch( cls.serverProtocol ) {
    case PROTOCOL_VERSION_R1Q2:
        Com_sprintf( tail, sizeof( tail ), " %d %d",
            net_maxmsglen->integer, PROTOCOL_VERSION_R1Q2_CURRENT );
        cls.quakePort = net_qport->integer & 0xff;
        break;
    case PROTOCOL_VERSION_Q2PRO:
        Com_sprintf( tail, sizeof( tail ), " %d %d %d %d",
            net_maxmsglen->integer, net_chantype->integer, USE_ZLIB,
            PROTOCOL_VERSION_Q2PRO_CURRENT );
        cls.quakePort = net_qport->integer & 0xff;
        break;
    default:
        tail[0] = 0;
        cls.quakePort = net_qport->integer;
        break;
	}

    Cvar_BitInfo( userinfo, CVAR_USERINFO );
    ret = Netchan_OutOfBandPrint( NS_CLIENT, &cls.serverAddress,
        "connect %i %i %i \"%s\"%s\n", cls.serverProtocol, cls.quakePort,
        cls.challenge, userinfo, tail );
	if( ret == NET_ERROR ) {
		Com_Error( ERR_DISCONNECT, "%s to %s\n", NET_ErrorString(),
            NET_AdrToString( &cls.serverAddress ) );
	}
}


/*
================
CL_Connect_f
 
================
*/
void CL_Connect_f( void ) {
    char	*server;
    netadr_t	address;
	int	protocol;

    if ( Cmd_Argc() < 2 ) {
usage:
        Com_Printf( "Usage: connect <server> [protocol]\n"
					"Protocol argument overrides cl_protocol setting\n"
					"Supported protocols: %d, %d and %d\n",
			PROTOCOL_VERSION_DEFAULT,
			PROTOCOL_VERSION_R1Q2,
			PROTOCOL_VERSION_Q2PRO );
        return;
    }

    server = Cmd_Argv( 1 );
	protocol = cl_protocol->integer;
	if( Cmd_Argc() > 2 ) {
		protocol = atoi( Cmd_Argv( 2 ) );
		if( protocol < PROTOCOL_VERSION_DEFAULT ||
			protocol > PROTOCOL_VERSION_Q2PRO )
		{
			goto usage;
		}
	}

    if ( !NET_StringToAdr( server, &address ) ) {
        Com_Printf( "Bad server address\n" );
        return;
    }
    if ( address.port == 0 ) {
        address.port = BigShort( PORT_SERVER );
    }

    if ( sv_running->integer ) {
        // if running a local server, kill it and reissue
        SV_Shutdown( "Server was killed\n", KILL_DROP );
    }

	NET_Config( NET_CLIENT );

    CL_Disconnect( ERR_DISCONNECT, NULL );

    cls.serverAddress = address;
    cls.serverProtocol = protocol ? protocol : PROTOCOL_VERSION_Q2PRO;
    cls.protocolVersion = 0;
    Q_strncpyz( cls.servername, server, sizeof( cls.servername ) );
    cls.passive = qfalse;

    cls.state = ca_challenging;
    cls.connectCount = 0;
    cls.connect_time = -99999;	// CL_CheckForResend() will fire immediately

	CL_CheckForResend();

    Cvar_Set( "cl_paused", "0" );
	Cvar_Set( "sv_paused", "0" );
	Cvar_Set( "timedemo", "0" );

    Con_Close();
    UI_OpenMenu( UIMENU_NONE );
}

void CL_PassiveConnect_f( void ) {
    netadr_t address;

    if( cls.passive ) {
        cls.passive = qfalse;
        Com_Printf( "No longer listening for passive connections.\n" );
        return;
    }
    if ( sv_running->integer ) {
        // if running a local server, kill it and reissue
        SV_Shutdown( "Server was killed\n", KILL_DROP );
    }

    NET_Config( NET_CLIENT );

    CL_Disconnect( ERR_DISCONNECT, NULL );

    if( !NET_GetAddress( NS_CLIENT, &address ) ) {
        return;
    }

    cls.passive = qtrue;
    Com_Printf( "Listening for passive connections at %s.\n",
        NET_AdrToString( &address ) );
}

static const char *CL_Connect_g( const char *partial, int state ) {
    static int length;
    static int index;
    const char *adrstring;
	char buffer[MAX_QPATH];
    
    if( !state ) {
        length = strlen( partial );
        index = 0;
    }

	while( index < MAX_LOCAL_SERVERS ) {
		Com_sprintf( buffer, sizeof( buffer ), "adr%i", index );
		index++;
		adrstring = Cvar_VariableString( buffer );
		if( !adrstring[ 0 ] ) {
			continue;
		}
		if( !strncmp( partial, adrstring, length ) ) {
			return adrstring;
		}
	}

    return NULL;
}


/*
=====================
CL_Rcon_f
 
  Send the rest of the command line over as
  an unconnected command.
=====================
*/
void CL_Rcon_f( void ) {
    netadr_t	address;
	neterr_t	ret;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <command>\n", Cmd_Argv( 0 ) );
		return;
	}

    if( !rcon_password->string[0] ) {
        Com_Printf( "You must set 'rcon_password' before "
                    "issuing an rcon command.\n" );
        return;
    }

	if( !cls.netchan ) {
        if( !rcon_address->string[0] ) {
            Com_Printf( "You must either be connected, "
                        "or set the 'rcon_address' cvar "
                        "to issue rcon commands.\n" );
            return;
        }
		if( !NET_StringToAdr( rcon_address->string, &address ) ) {
			Com_Printf( "Bad address: %s\n", rcon_address->string );
			return;
		}
        if( !address.port )
            address.port = BigShort( PORT_SERVER );
	} else {
		address = cls.netchan->remote_address;
	}

	NET_Config( NET_CLIENT );

	CL_AddRequest( &address, REQ_RCON );

    ret = Netchan_OutOfBandPrint( NS_CLIENT, &address,
		"rcon \"%s\" %s", rcon_password->string, Cmd_RawArgs() );
	if( ret == NET_ERROR ) {
		Com_Printf( "%s to %s\n", NET_ErrorString(),
                NET_AdrToString( &address ) );
	}
}


/*
=====================
CL_ClearState
 
=====================
*/
void CL_ClearState( void ) {
    S_StopAllSounds();
    CL_ClearEffects();
    CL_ClearTEnts();
    LOC_FreeLocations();

    // wipe the entire cl structure
	CM_FreeMap( &cl.cm );
    memset( &cl, 0, sizeof( cl ) );
    memset( &cl_entities, 0, sizeof( cl_entities ) );

    if( cls.state > ca_connected ) {
        cls.state = ca_connected;
    }
}

/*
=====================
CL_Disconnect
 
Goes from a connected state to full screen console state
Sends a disconnect message to the server
This is also called on Com_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect( comErrorType_t type, const char *text ) {
    if ( cls.state > ca_disconnected ) {
        EXEC_TRIGGER( cl_disconnectcmd );
    }

    if ( cls.ref_initialized )
        ref.CinematicSetPalette( NULL );

    cls.connect_time = 0;
	cls.connectCount = 0;
    cls.passive = qfalse;

    if ( cls.demoplayback ) {
        FS_FCloseFile( cls.demoplayback );
        cls.demoplayback = 0;

        if ( com_timedemo->integer ) {
            float seconds, fps;

            seconds = ( Sys_Milliseconds() - cls.timeDemoStart ) * 0.001f;
            fps = cls.timeDemoFrames / seconds;

            Com_Printf( "%i frames, %3.1f seconds: %3.1f fps\n",
                    cls.timeDemoFrames, seconds, fps );
        }
    }
    
    if ( cls.demorecording )
        CL_Stop_f();

    if( cls.netchan ) {
        // send a disconnect message to the server
        MSG_WriteByte( clc_stringcmd );
        MSG_WriteData( "disconnect", 11 );

        cls.netchan->Transmit( cls.netchan, msg_write.cursize, msg_write.data );
        cls.netchan->Transmit( cls.netchan, msg_write.cursize, msg_write.data );
        cls.netchan->Transmit( cls.netchan, msg_write.cursize, msg_write.data );

        SZ_Clear( &msg_write );
            
        Netchan_Close( cls.netchan );
        cls.netchan = NULL;
    }

    // stop download
    if ( cls.download ) {
        FS_FCloseFile( cls.download );
        cls.download = 0;
    }

    cls.downloadtempname[ 0 ] = 0;
    cls.downloadname[ 0 ] = 0;

    CL_ClearState ();

    Cvar_Set( "cl_paused", "0" );

    cls.state = ca_disconnected;
    cls.messageString[ 0 ] = 0;
	cls.userinfo_modified = 0;

	if( cls.ui_initialized ) {
		UI_ErrorMenu( type, text );
	}

}

/*
================
CL_Disconnect_f
================
*/
static void CL_Disconnect_f( void ) {
	if( cls.state > ca_disconnected ) {
		Com_Error( ERR_SILENT, "Disconnected from server" );
	}
}


/*
================
CL_ServerStatus_f
================
*/
static void CL_ServerStatus_f( void ) {
    char		*s;
    netadr_t	adr;
	neterr_t	ret;

    if ( Cmd_Argc() < 2 ) {
        Com_Printf( "Usage: %s <server>\n", Cmd_Argv( 0 ) );
        return;
    }

    s = Cmd_Argv( 1 );
    if ( !NET_StringToAdr( s, &adr ) ) {
        Com_Printf( "Bad address: %s\n", s );
        return;
    }

    if ( !adr.port ) {
        adr.port = BigShort( PORT_SERVER );
    }

	CL_AddRequest( &adr, REQ_STATUS );

	NET_Config( NET_CLIENT );

    ret = OOB_PRINT( NS_CLIENT, &adr, "status" );
	if( ret == NET_ERROR ) {
		Com_Printf( "%s to %s\n", NET_ErrorString(), NET_AdrToString( &adr ) );
	}
		
}

/*
====================
SortPlayers
====================
*/
static int QDECL SortPlayers( const void *v1, const void *v2 ) {
    const playerStatus_t *p1 = ( const playerStatus_t * )v1;
    const playerStatus_t *p2 = ( const playerStatus_t * )v2;

    return p2->score - p1->score;
}

/*
====================
CL_ServerStatusResponse
====================
*/
static qboolean CL_ServerStatusResponse( const char *status,
    const netadr_t *from, serverStatus_t *dest )
{
	const char *s;
    playerStatus_t *player;
    int length;

    memset( dest, 0, sizeof( *dest ) );

    s = strchr( status, '\n' );
    if ( !s ) {
		return qfalse;
    }
    length = s - status;
	if( length > MAX_STRING_CHARS - 1 ) {
		return qfalse;
	}
    s++;

    strcpy( dest->address, NET_AdrToString( from ) );
    strncpy( dest->infostring, status, length );
    
    // HACK: check if this is a status response
	if( !strstr( dest->infostring, "\\hostname\\" ) ) {
		return qfalse;
    }

    // parse player list
    if( *s < 32 ) {
        return qtrue;
    }
    do {
        player = &dest->players[dest->numPlayers];
        player->score = atoi( COM_Parse( &s ) );
        player->ping = atoi( COM_Parse( &s ) );
        if( !s ) {
            break;
        } 
        Q_strncpyz( player->name, COM_Parse( &s ), sizeof( player->name ) );

        if ( ++dest->numPlayers == MAX_STATUS_PLAYERS ) {
            break;
        }
    } while( s );

    qsort( dest->players, dest->numPlayers, sizeof( dest->players[ 0 ] ),
        SortPlayers );

    return qtrue;
}

void CL_DumpServerInfo( const serverStatus_t *status ) {
	char	key[MAX_STRING_CHARS];
	char	value[MAX_STRING_CHARS];
    const   playerStatus_t *player, *last;
    const char    *infostring;

    Com_Printf( "Info response from %s:\n",
        NET_AdrToString( &net_from ) );

    infostring = status->infostring;
	do {
		Info_NextPair( &infostring, key, value );
		
		if( !key[0] ) {
			break;
		}

		if( value[0] ) {
			Com_Printf( "%-20s %s\n", key, value );
		} else {
			Com_Printf( "%-20s <MISSING VALUE>\n", key );
		}
	} while( infostring );

    Com_Printf( "\nScore Ping Name\n" );
    last = status->players + status->numPlayers;
    for( player = status->players; player != last; player++ ) {
        Com_Printf( "%5i %4i %s\n", player->score, player->ping,
            player->name );
    }
}

/*
====================
CL_ParsePrintMessage
====================
*/
static void CL_ParsePrintMessage( void ) {
	request_t *r;
    serverStatus_t serverStatus;
    char *string;
    int i, oldest;

    string = MSG_ReadString();

    if ( ( cls.state == ca_challenging || cls.state == ca_connecting ) &&
            NET_IsEqualBaseAdr( &net_from, &cls.serverAddress ) )
	{
		// server rejected our connect request
		if( NET_IsLocalAddress( &cls.serverAddress ) ) {
			Com_Error( ERR_DROP, "Server rejected loopback connection" );
		}
		Com_Printf( "%s", string );
        Q_strncpyz( cls.messageString, string, sizeof( cls.messageString ) );
		cls.state = ca_challenging;
		cls.connectCount = 0;
		return;
    }

	oldest = currentRequest - MAX_REQUESTS;
	if( oldest < 0 ) {
		oldest = 0;
	}
	for( i = currentRequest - 1; i >= oldest; i-- ) {
		r = &clientRequests[i & REQUEST_MASK];
		if( !r->type ) {
			continue;
		}
		if( r->adr.type == NA_BROADCAST ) {
			if( r->time < cls.realtime ) {
				continue;
			}
		} else {
			if( r->time < cls.realtime ) {
				break;
			}
			if( !NET_IsEqualBaseAdr( &net_from, &r->adr ) ) {
				continue;
			}
		}
		switch( r->type ) {
		case REQ_STATUS:
			if( CL_ServerStatusResponse( string, &net_from, &serverStatus ) ) {
			    CL_DumpServerInfo( &serverStatus );
            }
			break;
		case REQ_INFO:
			break;
		case REQ_PING:
			if( CL_ServerStatusResponse( string, &net_from, &serverStatus ) ) {
				UI_AddToServerList( &serverStatus );
			}
			break;
		case REQ_RCON:
			Com_Printf( "%s", string );
			CL_AddRequest( &net_from, REQ_RCON );
			break;
		default:
			break;
		}

		r->type = REQ_FREE;
		return;
	}

	Com_DPrintf( "Dropped unrequested packet\n" );
}


/*
====================
CL_Packet_f
 
packet <destination> <contents>
 
Contents allows \n escape character
====================
*/ 
/*
void CL_Packet_f (void)
{
	char	send[2048];
	int		i, l;
	char	*in, *out;
	netadr_t	adr;
 
	if (Cmd_Argc() != 3)
	{
		Com_Printf ("packet <destination> <contents>\n");
		return;
	}
 
	if (!NET_StringToAdr (Cmd_Argv(1), &adr))
	{
		Com_Printf ("Bad address\n");
		return;
	}
	if (!adr.port)
		adr.port = BigShort (PORT_SERVER);
 
	in = Cmd_Argv(2);
	out = send+4;
	send[0] = send[1] = send[2] = send[3] = (char)0xff;
 
	l = strlen (in);
	for (i=0 ; i<l ; i++)
	{
		if (in[i] == '\\' && in[i+1] == 'n')
		{
			*out++ = '\n';
			i++;
		}
		else
			*out++ = in[i];
	}
	*out = 0;
 
	NET_SendPacket (NS_CLIENT, out-send, send, &adr);
}
*/

/*
=================
CL_Changing_f
 
Just sent as a hint to the client that they should
drop to full console
=================
*/
static void CL_Changing_f( void ) {
    if ( cls.state < ca_connected ) {
        return;
    }

    S_StopAllSounds();

    Com_Printf( "Changing map...\n" );

    EXEC_TRIGGER( cl_changemapcmd );

    SCR_BeginLoadingPlaque();

    cls.state = ca_connected;	// not active anymore, but not disconnected

    SCR_UpdateScreen();
}


/*
=================
CL_Reconnect_f
 
The server is changing levels
=================
*/
void CL_Reconnect_f( void ) {
    if( cls.state >= ca_precached ) {
        CL_Disconnect( ERR_SILENT, NULL );
    }
    if( cls.state >= ca_connected ) {
        cls.state = ca_connected;

        if ( cls.demoplayback ) {
            return;
        }
        if ( cls.download ) {
            return; // if we are downloading, we don't change!
        }

        Com_Printf( "Reconnecting...\n" );

        CL_ClientCommand( "new" );
        return;
    }

    // issued manually at console
    if( cls.serverAddress.type == NA_BAD ) {
        Com_Printf( "No server to reconnect to.\n" );
        return;
    }
    if( cls.serverAddress.type == NA_LOOPBACK ) {
        Com_Printf( "Can not reconnect to loopback.\n" );
        return;
    }

    Com_Printf( "Reconnecting...\n" );

    cls.connect_time = -9999;
    cls.state = ca_challenging;

    SCR_UpdateScreen();
}

#if 0
/*
=================
CL_ParseStatusMessage
 
Handle a reply from a ping
=================
*/
void CL_ParseStatusMessage ( void ) {}
#endif

/*
=================
CL_SendStatusRequest
=================
*/
qboolean CL_SendStatusRequest( char *buffer, int bufferSize ) {
    netadr_t	address;

    memset( &address, 0, sizeof( address ) );

	NET_Config( NET_CLIENT );

    // send a broadcast packet
    if ( !strcmp( buffer, "broadcast" ) ) {
        address.type = NA_BROADCAST;
        address.port = BigShort( PORT_SERVER );
    } else {
        if ( !NET_StringToAdr( buffer, &address ) ) {
            return qfalse;
        }

        if ( !address.port ) {
            address.port = BigShort( PORT_SERVER );
        }

        Q_strncpyz( buffer, NET_AdrToString( &address ), bufferSize );
    }

	CL_AddRequest( &address, REQ_PING );

    OOB_PRINT( NS_CLIENT, &address, "status" );

    Com_ProcessEvents();

    return qtrue;
}


/*
=================
CL_PingServers_f
=================
*/
static void CL_PingServers_f( void ) {
    int	i;
    char	buffer[ 32 ];
    char	*adrstring;
    netadr_t	address;

    memset( &address, 0, sizeof( address ) );

	NET_Config( NET_CLIENT );

    // send a broadcast packet
    Com_Printf( "pinging broadcast...\n" );
    address.type = NA_BROADCAST;
    address.port = BigShort( PORT_SERVER );

	CL_AddRequest( &address, REQ_STATUS );

    OOB_PRINT( NS_CLIENT, &address, "status" );

    SCR_UpdateScreen();

    // send a packet to each address book entry
    for ( i = 0; i < MAX_LOCAL_SERVERS; i++ ) {
        Com_sprintf( buffer, sizeof( buffer ), "adr%i", i );
        adrstring = Cvar_VariableString( buffer );
        if ( !adrstring[ 0 ] )
            continue;

        if ( !NET_StringToAdr( adrstring, &address ) ) {
            Com_Printf( "bad address: %s\n", adrstring );
            continue;
        }

        if ( !address.port ) {
            address.port = BigShort( PORT_SERVER );
        }

        Com_Printf( "pinging %s...\n", adrstring );
	    CL_AddRequest( &address, REQ_STATUS );

        OOB_PRINT( NS_CLIENT, &address, "status" );

        Com_ProcessEvents();
        SCR_UpdateScreen();

    }
}

/*
=================
CL_Skins_f
 
Load or download any custom player skins and models
=================
*/
void CL_Skins_f ( void ) {
    int	i;
    char *s;

    for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
        s = cl.configstrings[ CS_PLAYERSKINS + i ];
        if( !s[0] )
            continue;
        Com_Printf ( "client %i: %s\n", i, s );
        SCR_UpdateScreen ();
        CL_ParseClientinfo ( i );
    }
}

/*
=================
CL_ConnectionlessPacket
 
Responses to broadcasts, etc
=================
*/
static void CL_ConnectionlessPacket( void ) {
    char	*s;
    char	*c;
    int	i, j, k;

    MSG_BeginReading();
    MSG_ReadLong();	// skip the -1

    s = MSG_ReadStringLine();

    Cmd_TokenizeString( s, qfalse );

    c = Cmd_Argv( 0 );

    Com_DPrintf( "%s: %s\n", NET_AdrToString ( &net_from ), s );

    // challenge from the server we are connecting to
    if ( !strcmp( c, "challenge" ) ) {
		int mask = 0;

        if ( cls.state < ca_challenging ) {
            Com_DPrintf( "Challenge received while not connecting.  Ignored.\n" );
            return;
        }
        if ( !NET_IsEqualBaseAdr( &net_from, &cls.serverAddress ) ) {
            Com_DPrintf( "Challenge from different address.  Ignored.\n" );
            return;
        }
        if ( cls.state > ca_challenging ) {
            Com_DPrintf( "Dup challenge received.  Ignored.\n" );
            return;
        }

        cls.challenge = atoi( Cmd_Argv( 1 ) );
        cls.state = ca_connecting;
        cls.connect_time = -9999;
        cls.connectCount = 0;

		// parse additional parameters
        j = Cmd_Argc();
		for( i = 2; i < j; i++ ) {
			s = Cmd_Argv( i );
			if( !strncmp( s, "p=", 2 ) ) {
				s += 2;
				while( *s ) {
					k = strtoul( s, &s, 10 );
					if( k == PROTOCOL_VERSION_R1Q2 ) {
						mask |= 1;
					} else if( k == PROTOCOL_VERSION_Q2PRO ) {
                        mask |= 2;
					}
					s = strchr( s, ',' );
					if( s == NULL ) {
						break;
					}
					s++;
				}
			}
        }

		// choose supported protocol
		switch( cls.serverProtocol ) {
        case PROTOCOL_VERSION_Q2PRO:
            if( mask & 2 ) {
                break;
            }
            cls.serverProtocol = PROTOCOL_VERSION_R1Q2;
        case PROTOCOL_VERSION_R1Q2:
            if( mask & 1 ) {
                break;
            }
        default:
            cls.serverProtocol = PROTOCOL_VERSION_DEFAULT;
            break;
        }
		Com_DPrintf( "Selected protocol %d\n", cls.serverProtocol );

		cls.messageString[0] = 0;

        CL_CheckForResend();
        return;
    }

    // server connection
    if ( !strcmp( c, "client_connect" ) ) {
		netchan_type_t type;
        int anticheat = 0;

        if ( cls.state < ca_connecting ) {
            Com_DPrintf( "Connect received while not connecting.  Ignored.\n" );
            return;
        }
        if ( !NET_IsEqualBaseAdr( &net_from, &cls.serverAddress ) ) {
            Com_DPrintf( "Connect from different address.  Ignored.\n" );
            return;
        }
        if ( cls.state > ca_connecting ) {
            Com_DPrintf( "Dup connect received.  Ignored.\n" );
            return;
        }

        if ( cls.serverProtocol == PROTOCOL_VERSION_Q2PRO ) {
			type = NETCHAN_NEW;
        } else {
            type = NETCHAN_OLD;
        }

		// parse additional parameters
        j = Cmd_Argc();
		for( i = 1; i < j; i++ ) {
			s = Cmd_Argv( i );
			if( !strncmp( s, "ac=", 3 ) ) {
                s += 3;
                if( *s ) {
                    anticheat = atoi( s );
                }
            } else if( !strncmp( s, "nc=", 3 ) ) {
                s += 3;
                if( *s ) {
                    type = atoi( s );
                    if( type != NETCHAN_OLD && type != NETCHAN_NEW ) {
			            Com_Error( ERR_DISCONNECT,
                            "Server returned invalid netchan type" );
                    }
                }
            }
        }

		Com_Printf( "Connection to %s established (protocol %d).\n",
			NET_AdrToString( &cls.serverAddress ), cls.serverProtocol );
		if( cls.netchan ) {
			// this may happen after svc_reconnect
			Netchan_Close( cls.netchan );
		}
		cls.netchan = Netchan_Setup( NS_CLIENT, type, &cls.serverAddress,
                cls.quakePort, 1024, cls.serverProtocol );

#if USE_ANTICHEAT
        if( anticheat ) {
			MSG_WriteByte( clc_nop );
			MSG_FlushTo( &cls.netchan->message );
			cls.netchan->Transmit( cls.netchan, 0, NULL );
			S_StopAllSounds();
			Com_Printf( "Loading anticheat, this may take a few moments...\n" );
			SCR_UpdateScreen();
			if( !Sys_GetAntiCheatAPI() ) {
				Com_Printf( "Trying to connect without anticheat.\n" );
			} else {
				Com_Printf( S_COLOR_CYAN "Anticheat loaded successfully.\n" );
			}
        }
#else
        if( anticheat >= 2 ) {
            Com_Printf( "Anticheat required by server, "
                    "but no anticheat support linked in.\n" );
        }
#endif

        CL_ClientCommand( "new" );
        cls.state = ca_connected;
		cls.messageString[0] = 0;
        return;
    }

#if 0
    // server responding to a status broadcast
    if ( !strcmp( c, "info" ) ) {
        CL_ParseStatusMessage();
        return;
    }
#endif

    if ( !strcmp( c, "passive_connect" ) ) {
        if( !cls.passive ) {
            Com_DPrintf( "Passive connect received while not connecting.  Ignored.\n" );
            return;
        }
        s = NET_AdrToString( &net_from );
		Com_Printf( "Received passive connect from %s.\n", s );

        cls.serverAddress = net_from;
        cls.serverProtocol = cl_protocol->integer;
        Q_strncpyz( cls.servername, s, sizeof( cls.servername ) );
        cls.passive = qfalse;

        cls.state = ca_challenging;
        cls.connect_time = -9999;
        cls.connectCount = 0;

        CL_CheckForResend();
        return;
    }

    // print command from somewhere
    if ( !strcmp( c, "print" ) ) {
        CL_ParsePrintMessage();
        return;
    }

    Com_DPrintf( "Unknown connectionless packet command.\n" );
}


/*
=================
CL_PacketEvent
=================
*/
void CL_PacketEvent( neterr_t ret ) {
    //
    // remote command packet
    //
    if ( ret == NET_OK && *( int * )msg_read.data == -1 ) {
        CL_ConnectionlessPacket();
        return;
    }

	if( cls.state < ca_connected ) {
		return;
	}

	if ( !cls.netchan ) {
        return;		// dump it if not connected
	}

    if ( ret == NET_OK && msg_read.cursize < 8 ) {
        Com_DPrintf( "%s: runt packet\n", NET_AdrToString( &net_from ) );
        return;
    }

    //
    // packet from server
    //
    if ( !NET_IsEqualAdr( &net_from, &cls.netchan->remote_address ) ) {
        Com_DPrintf( "%s: sequenced packet without connection\n",
            NET_AdrToString( &net_from ) );
        return;
    }

	if( ret == NET_ERROR ) {
		Com_Error( ERR_DISCONNECT, "Connection reset by peer" );
	}

    if ( !cls.netchan->Process( cls.netchan ) )
        return;		// wasn't accepted for some reason


    CL_ParseServerMessage();

    CL_AddNetgraph();

    SCR_LagSample();
}


//=============================================================================

/*
==============
CL_FixUpGender_f
==============
*/
static void CL_FixUpGender( void ) {
    char *p;
    char sk[MAX_QPATH];

    Q_strncpyz( sk, info_skin->string, sizeof( sk ) );
    if ( ( p = strchr( sk, '/' ) ) != NULL )
        *p = 0;
    if ( Q_stricmp( sk, "male" ) == 0 || Q_stricmp( sk, "cyborg" ) == 0 )
        Cvar_Set ( "gender", "male" );
    else if ( Q_stricmp( sk, "female" ) == 0 || Q_stricmp( sk, "crackhor" ) == 0 )
        Cvar_Set ( "gender", "female" );
    else
        Cvar_Set ( "gender", "none" );
    info_gender->modified = qfalse;
}

void CL_UpdateUserinfo( cvar_t *var, cvarSetSource_t source ) {
	int i;

	if( var == info_skin && source != CVAR_SET_CONSOLE &&
        gender_auto->integer )
    {
		 CL_FixUpGender();
	}
	if( !cls.netchan ) {
		return;
	}
	if( cls.serverProtocol != PROTOCOL_VERSION_Q2PRO ) {
        // transmit at next oportunity
		cls.userinfo_modified = MAX_PACKET_USERINFOS;	
		return;
	}

	if( cls.userinfo_modified == MAX_PACKET_USERINFOS ) {
		return; // can't hold any more
	}

	// check for the same variable being modified twice
	for( i = 0; i < cls.userinfo_modified; i++ ) {
		if( cls.userinfo_updates[i] == var ) {
			Com_DPrintf( "Dup modified %s at frame %u\n", var->name, com_framenum );
			return;
		}
	}

	Com_DPrintf( "Modified %s at frame %u\n", var->name, com_framenum );

	cls.userinfo_updates[cls.userinfo_modified++] = var;	
}

/*
==============
CL_Userinfo_f
==============
*/
static void CL_Userinfo_f ( void ) {
    char userinfo[MAX_INFO_STRING];

    Cvar_BitInfo( userinfo, CVAR_USERINFO );

    Com_Printf( "User info settings:\n" );
    Info_Print( userinfo );
}

/*
======================
CL_RegisterSounds
======================
*/
static void CL_RegisterSounds( void ) {
    int	i;
    char	*s;

    S_BeginRegistration ();
    CL_RegisterTEntSounds ();
    for ( i = 1; i < MAX_SOUNDS; i++ ) {
        s = cl.configstrings[ CS_SOUNDS + i ];
        if ( !s[ 0 ] )
            break;
        cl.sound_precache[ i ] = S_RegisterSound( s );
    }
    S_EndRegistration ();
}

/*
=================
CL_Snd_Restart_f
 
Restart the sound subsystem so it can pick up
new parameters and flush all sounds
=================
*/
static void CL_Snd_Restart_f ( void ) {
    S_Shutdown ();
    S_Init ();
    CL_RegisterSounds ();
}

static int precache_check; // for autodownload of precache items
static int precache_spawncount;
static int precache_tex;
static int precache_model_skin;

static byte *precache_model; // used for skin checking in alias models

#define PLAYER_MULT 5

// ENV_CNT is map load, ENV_CNT+1 is first env map
#define ENV_CNT (CS_PLAYERSKINS + MAX_CLIENTS * PLAYER_MULT)
#define TEXTURE_CNT (ENV_CNT+13)

static const char env_suf[6][3] = { "rt", "bk", "lf", "ft", "up", "dn" };

void CL_RequestNextDownload ( void ) {
    unsigned	map_checksum;		// for detecting cheater maps
    char fn[ MAX_QPATH ];
    dmdl_t *pheader;
    int length;

    if ( cls.state != ca_connected && cls.state != ca_loading )
        return;

	if ( ( !allow_download->integer || NET_IsLocalAddress( &cls.serverAddress )) && precache_check < ENV_CNT )
        precache_check = ENV_CNT;

    //ZOID
    if ( precache_check == CS_MODELS ) { // confirm map
        precache_check = CS_MODELS + 2; // 0 isn't used
        if ( allow_download_maps->integer )
            if ( !CL_CheckOrDownloadFile( cl.configstrings[ CS_MODELS + 1 ] ) )
                return; // started a download
    }
    if ( precache_check >= CS_MODELS && precache_check < CS_MODELS + MAX_MODELS ) {
        if ( allow_download_models->integer ) {
            while ( precache_check < CS_MODELS + MAX_MODELS &&
                    cl.configstrings[ precache_check ][ 0 ] ) {
                int num_skins, ofs_skins;

                if ( cl.configstrings[ precache_check ][ 0 ] == '*' ||
                        cl.configstrings[ precache_check ][ 0 ] == '#' ) {
                    precache_check++;
                    continue;
                }
                if ( precache_model_skin == 0 ) {
                    if ( !CL_CheckOrDownloadFile( cl.configstrings[ precache_check ] ) ) {
                        precache_model_skin = 1;
                        return; // started a download
                    }
                    precache_model_skin = 1;
                }

                // checking for skins in the model
                if ( !precache_model ) {
                    length = FS_LoadFile ( cl.configstrings[ precache_check ], ( void ** ) & precache_model );
                    if ( !precache_model ) {
                        precache_model_skin = 0;
                        precache_check++;
                        continue; // couldn't load it
                    }
                    pheader = ( dmdl_t * ) precache_model;
                    if( length < sizeof( *pheader ) ||
                        LittleLong( pheader->ident ) != IDALIASHEADER ||
                        LittleLong ( pheader->version ) != ALIAS_VERSION )
                    {
                        // not an alias model
                        FS_FreeFile( precache_model );
                        precache_model = 0;
                        precache_model_skin = 0;
                        precache_check++;
                        continue;
                    }
                    num_skins = LittleLong( pheader->num_skins );
                    ofs_skins = LittleLong( pheader->ofs_skins );
                    if( ofs_skins + num_skins * MAX_SKINNAME > length ) {
                        // bad alias model
                        FS_FreeFile( precache_model );
                        precache_model = 0;
                        precache_model_skin = 0;
                        precache_check++;
                        continue;
                    }
                }

                pheader = ( dmdl_t * ) precache_model;
                num_skins = LittleLong( pheader->num_skins );
                ofs_skins = LittleLong( pheader->ofs_skins );

                while ( precache_model_skin - 1 < num_skins ) {
                    Q_strncpyz( fn, ( char * )precache_model + ofs_skins +
                        ( precache_model_skin - 1 ) * MAX_SKINNAME, sizeof( fn ) );
                    if ( !CL_CheckOrDownloadFile( fn ) ) {
                        precache_model_skin++;
                        return; // started a download
                    }
                    precache_model_skin++;
                }
                if ( precache_model ) {
                    FS_FreeFile( precache_model );
                    precache_model = 0;
                }
                precache_model_skin = 0;
                precache_check++;
            }
        }
        precache_check = CS_SOUNDS;
    }
    if ( precache_check >= CS_SOUNDS && precache_check < CS_SOUNDS + MAX_SOUNDS ) {
        if ( allow_download_sounds->integer ) {
            if ( precache_check == CS_SOUNDS )
                precache_check++; // zero is blank
            while ( precache_check < CS_SOUNDS + MAX_SOUNDS &&
                    cl.configstrings[ precache_check ][ 0 ] ) {
                if ( cl.configstrings[ precache_check ][ 0 ] == '*' ) {
                    precache_check++;
                    continue;
                }
                Q_concat( fn, sizeof( fn ), "sound/", cl.configstrings[ precache_check++ ], NULL );
                if ( !CL_CheckOrDownloadFile( fn ) )
                    return; // started a download
            }
        }
        precache_check = CS_IMAGES;
    }
    if ( precache_check >= CS_IMAGES && precache_check < CS_IMAGES + MAX_IMAGES ) {
        if ( precache_check == CS_IMAGES )
            precache_check++; // zero is blank
        while ( precache_check < CS_IMAGES + MAX_IMAGES &&
                cl.configstrings[ precache_check ][ 0 ] ) {
            Q_concat( fn, sizeof( fn ), "pics/", cl.configstrings[ precache_check++ ], ".pcx", NULL );
            if ( !CL_CheckOrDownloadFile( fn ) )
                return; // started a download
        }
        precache_check = CS_PLAYERSKINS;
    }
    // skins are special, since a player has three things to download:
    // model, weapon model and skin
    // so precache_check is now *3
    if ( precache_check >= CS_PLAYERSKINS && precache_check < CS_PLAYERSKINS + MAX_CLIENTS * PLAYER_MULT ) {
        if ( allow_download_players->integer ) {
            while ( precache_check < CS_PLAYERSKINS + MAX_CLIENTS * PLAYER_MULT ) {
                int i, n;
                char model[ MAX_QPATH ], skin[ MAX_QPATH ], *p;

                i = ( precache_check - CS_PLAYERSKINS ) / PLAYER_MULT;
                n = ( precache_check - CS_PLAYERSKINS ) % PLAYER_MULT;

                if ( !cl.configstrings[ CS_PLAYERSKINS + i ][ 0 ] ) {
                    precache_check = CS_PLAYERSKINS + ( i + 1 ) * PLAYER_MULT;
                    continue;
                }

                if ( ( p = strchr( cl.configstrings[ CS_PLAYERSKINS + i ], '\\' ) ) != NULL )
                    p++;
                else
                    p = cl.configstrings[ CS_PLAYERSKINS + i ];
                Q_strncpyz( model, p, sizeof( model ) );
                p = strchr( model, '/' );
                if ( !p )
                    p = strchr( model, '\\' );
                if ( p ) {
                    *p++ = 0;
                    strcpy( skin, p );
                } else
                    *skin = 0;

                switch ( n ) {
                case 0:   // model
                    Q_concat( fn, sizeof( fn ), "players/", model, "/tris.md2", NULL );
                    if ( !CL_CheckOrDownloadFile( fn ) ) {
                        precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 1;
                        return; // started a download
                    }
                    n++;
                    /*FALL THROUGH*/

                case 1:   // weapon model
                    Q_concat( fn, sizeof( fn ), "players/", model, "/weapon.md2", NULL );
                    if ( !CL_CheckOrDownloadFile( fn ) ) {
                        precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 2;
                        return; // started a download
                    }
                    n++;
                    /*FALL THROUGH*/

                case 2:   // weapon skin
                    Q_concat( fn, sizeof( fn ), "players/", model, "/weapon.pcx", NULL );
                    if ( !CL_CheckOrDownloadFile( fn ) ) {
                        precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 3;
                        return; // started a download
                    }
                    n++;
                    /*FALL THROUGH*/

                case 3:   // skin
                    Q_concat( fn, sizeof( fn ), "players/", model, "/", skin, ".pcx", NULL );
                    if ( !CL_CheckOrDownloadFile( fn ) ) {
                        precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 4;
                        return; // started a download
                    }
                    n++;
                    /*FALL THROUGH*/

                case 4:   // skin_i
                    Q_concat( fn, sizeof( fn ), "players/", model, "/", skin, "_i.pcx", NULL );
                    if ( !CL_CheckOrDownloadFile( fn ) ) {
                        precache_check = CS_PLAYERSKINS + i * PLAYER_MULT + 5;
                        return; // started a download
                    }
                    // move on to next model
                    precache_check = CS_PLAYERSKINS + ( i + 1 ) * PLAYER_MULT;
                }
            }
        }
        // precache phase completed
        precache_check = ENV_CNT;
    }

    if ( precache_check == ENV_CNT ) {
        precache_check = ENV_CNT + 1;
        SCR_LoadingString( "collision map" );

        CM_LoadMap ( &cl.cm, cl.configstrings[ CS_MODELS + 1 ], CM_LOAD_CLIENT, &map_checksum );

#if USE_MAPCHECKSUM
        if ( map_checksum != atoi( cl.configstrings[ CS_MAPCHECKSUM ] ) ) {
            Com_Error ( ERR_DROP, "Local map version differs from server: %i != '%s'\n",
                        map_checksum, cl.configstrings[ CS_MAPCHECKSUM ] );
            return;
        }
#endif
    }

    if ( precache_check > ENV_CNT && precache_check < TEXTURE_CNT ) {
        if ( allow_download->integer && allow_download_maps->integer ) {
            while ( precache_check < TEXTURE_CNT ) {
                int n = precache_check++ - ENV_CNT - 1;

                if ( n & 1 )
                    Q_concat( fn, sizeof( fn ),
                        "env/", cl.configstrings[ CS_SKY ], env_suf[ n / 2 ], ".pcx", NULL );
                else
                    Q_concat( fn, sizeof( fn ),
                        "env/", cl.configstrings[ CS_SKY ], env_suf[ n / 2 ], ".tga", NULL );
                if ( !CL_CheckOrDownloadFile( fn ) )
                    return; // started a download
            }
        }
        precache_check = TEXTURE_CNT;
    }

    if ( precache_check == TEXTURE_CNT ) {
        precache_check = TEXTURE_CNT + 1;
        precache_tex = 0;
    }

    // confirm existance of textures, download any that don't exist
    if ( precache_check == TEXTURE_CNT + 1 ) {
        if ( allow_download->integer && allow_download_maps->integer ) {
			while ( precache_tex < cl.cm.cache->numtexinfo ) {
				char *texname = cl.cm.cache->surfaces[ precache_tex++ ].rname;

                // Also check if 32bit images are present
                Q_concat( fn, sizeof( fn ), "textures/", texname, ".jpg", NULL );
                if ( FS_LoadFile( fn, NULL ) == -1 ) {
                    Q_concat( fn, sizeof( fn ), "textures/", texname, ".tga", NULL );
                    if ( FS_LoadFile( fn, NULL ) == -1 ) {
                        Q_concat( fn, sizeof( fn ), "textures/", texname, ".wal", NULL );
                        if ( !CL_CheckOrDownloadFile( fn ) ) {
                            return; // started a download
                        }
                    }
                }
            }
        }
        precache_check = TEXTURE_CNT + 999;
    }


    //ZOID
    SCR_LoadingString( "sounds" );
    CL_RegisterSounds ();

    CL_PrepRefresh ();

    LOC_LoadLocations();

    CL_ClientCommand( va( "begin %i\n", precache_spawncount ) );

    Cvar_FixCheats();

    CL_UpdateGunSetting();
    CL_UpdateGibSetting();
    CL_UpdateFootstepsSetting();
	CL_UpdatePredictSetting();

    cls.state = ca_precached;
}

/*
=================
CL_Precache_f
 
The server will send this command right
before allowing the client into the server
=================
*/
static void CL_Precache_f( void ) {
    if ( cls.state < ca_connected ) {
        return;
    }

    cls.state = ca_loading;

    S_StopAllSounds();

    //Yet another hack to let old demos work
    //the old precache sequence
    if ( cls.demoplayback || Cmd_Argc() < 2 ) {
        uint32	map_checksum;		// for detecting cheater maps

        SCR_LoadingString( "collision map" );
        CM_LoadMap( &cl.cm, cl.configstrings[ CS_MODELS + 1 ],
                CM_LOAD_CLIENT, &map_checksum );
        SCR_LoadingString( "sounds" );
        CL_RegisterSounds();
        CL_PrepRefresh();
        cls.state = ca_precached;
        return;
    }

    precache_check = CS_MODELS;
    precache_spawncount = atoi( Cmd_Argv( 1 ) );
    precache_model = 0;
    precache_model_skin = 0;

    CL_RequestNextDownload();

    if( cls.state != ca_precached ) {
        cls.state = ca_connected;
    }
}


static void CL_DumpClients_f( void ) {
    int i;

    if ( cls.state != ca_active ) {
		Com_Printf( "Must be in a level to dump\n" );
        return;
    }

    for ( i = 0 ; i < MAX_CLIENTS ; i++ ) {
        if ( !cl.clientinfo[ i ].name[ 0 ] ) {
            continue;
        }

        Com_Printf( "%3i: %s\n", i, cl.clientinfo[ i ].name );
    }
}

static void CL_DumpStatusbar_f( void ) {
	char buffer[MAX_QPATH];
	fileHandle_t f;

    if ( cls.state != ca_active ) {
		Com_Printf( "Must be in a level to dump.\n" );
        return;
    }

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: %s <filename>\n", Cmd_Argv( 0 ) );
		return;
	}

	Cmd_ArgvBuffer( 1, buffer, sizeof( buffer ) );
	COM_DefaultExtension( buffer, ".txt", sizeof( buffer ) );

	FS_FOpenFile( buffer, &f, FS_MODE_WRITE );
	if( !f ) {
		Com_EPrintf( "Couldn't open %s for writing.\n", buffer );
		return;
	}

	FS_FPrintf( f, "// status bar program dump of '%s' mod\n",
            Cvar_VariableString( "gamedir" ) );
	FS_FPrintf( f, "%s\n", cl.configstrings[CS_STATUSBAR] );

	FS_FCloseFile( f );

	Com_Printf( "Dumped status bar program to %s.\n", buffer );
}

static void CL_DumpLayout_f( void ) {
	char buffer[MAX_QPATH];
	fileHandle_t f;

    if ( cls.state != ca_active ) {
		Com_Printf( "Must be in a level to dump.\n" );
        return;
    }

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: %s <filename>\n", Cmd_Argv( 0 ) );
		return;
	}

	if( !cl.layout[0] ) {
		Com_Printf( "No layout to dump.\n" );
		return;
	}

	Cmd_ArgvBuffer( 1, buffer, sizeof( buffer ) );
	COM_DefaultExtension( buffer, ".txt", sizeof( buffer ) );

	FS_FOpenFile( buffer, &f, FS_MODE_WRITE );
	if( !f ) {
		Com_EPrintf( "Couldn't open %s for writing.\n", buffer );
		return;
	}

	FS_FPrintf( f, "// layout program dump of '%s' mod\n",
            Cvar_VariableString( "gamedir" ) );
	FS_FPrintf( f, "%s\n", cl.layout );

	FS_FCloseFile( f );

	Com_Printf( "Dumped layout program to %s.\n", buffer );
}

static int CL_Mapname_m( char *buffer, int size ) {
    if( !cl.mapname[0] ) {
        return Q_strncpyz( buffer, "nomap", size );
    }
    return Q_strncpyz( buffer, cl.mapname, size );
}

static int CL_Server_m( char *buffer, int size ) {
    if( cls.state <= ca_disconnected ) {
        return Q_strncpyz( buffer, "noserver", size );
    }
    return Q_strncpyz( buffer, cls.servername, size );
}

static int CL_Ups_m( char *buffer, int size ) {
	vec3_t vel;
	int ups;
	player_state_t *ps;

	if( cl.frame.clientNum == CLIENTNUM_NONE ) {
		buffer[0] = 0;
		return 0;
	}

	if( !cls.demoplayback && cl.frame.clientNum == cl.clientNum &&
        cl_predict->integer )
    {
		VectorCopy( cl.predicted_velocity, vel );
	} else {
		ps = &cl.frame.ps;
		
		vel[0] = ps->pmove.velocity[0] * 0.125f;
		vel[1] = ps->pmove.velocity[1] * 0.125f;
		vel[2] = ps->pmove.velocity[2] * 0.125f;
	}

	ups = VectorLength( vel );
	return Com_sprintf( buffer, size, "%d", ups );
}

static int CL_Timer_m( char *buffer, int size ) {
	int hour, min, sec;

	sec = cl.time / 1000;
	min = sec / 60; sec %= 60;
	hour = min / 60; min %= 60;

	if( hour ) {
		return Com_sprintf( buffer, size, "%i:%i:%02i", hour, min, sec );
    }
	return Com_sprintf( buffer, size, "%i:%02i", min, sec );
}

static int CL_Fps_m( char *buffer, int size ) {
	return Com_sprintf( buffer, size, "%i", cls.fps );
}
static int CL_Ping_m( char *buffer, int size ) {
	return Com_sprintf( buffer, size, "%i", cls.ping );
}
static int CL_Health_m( char *buffer, int size ) {
	return Com_sprintf( buffer, size, "%i", cl.frame.ps.stats[STAT_HEALTH] );
}
static int CL_Ammo_m( char *buffer, int size ) {
	return Com_sprintf( buffer, size, "%i", cl.frame.ps.stats[STAT_AMMO] );
}
static int CL_Armor_m( char *buffer, int size ) {
	return Com_sprintf( buffer, size, "%i", cl.frame.ps.stats[STAT_ARMOR] );
}

/*
====================
CL_RestartFilesystem
 
Flush caches and restart the VFS.
====================
*/
void CL_RestartFilesystem( void ) {
    int	cls_state;

    if ( !cl_running->integer ) {
        FS_Restart();
        return;
    }

    Com_DPrintf( "CL_RestartFilesystem()\n" );

    /* temporary switch to loading state */
    cls_state = cls.state;
    if ( cls.state >= ca_precached ) {
        cls.state = ca_loading;
    }

    UI_OpenMenu( UIMENU_NONE );
    CL_ShutdownUI();

    S_StopAllSounds();
	S_FreeAllSounds();

    ref.Shutdown( qfalse );

    FS_Restart();

    ref.Init( qfalse );

    SCR_RegisterMedia();
    Con_SetupDC();
    CL_InitUI();

    if ( cls_state == ca_disconnected ) {
        UI_OpenMenu( UIMENU_MAIN );
    } else if ( cls_state >= ca_loading ) {
		CL_RegisterSounds();
        CL_PrepRefresh();
    }

    /* switch back to original state */
    cls.state = cls_state;

}

/*
====================
CL_RestartRefresh
====================
*/
void CL_RestartRefresh_f( void ) {
    int	cls_state;

    Com_DPrintf( "CL_RestartRefresh()\n" );

    /* temporary switch to loading state */
    cls_state = cls.state;
    if ( cls.state >= ca_precached ) {
        cls.state = ca_loading;
    }

    UI_OpenMenu( UIMENU_NONE );
    CL_ShutdownUI();

    S_StopAllSounds();
   
    CL_ShutdownInput();
    CL_ShutdownRefresh();

    CL_InitRefresh();
    CL_InitInput();

    SCR_RegisterMedia();
    Con_SetupDC();
    CL_InitUI();

    if ( cls_state == ca_disconnected ) {
        UI_OpenMenu( UIMENU_MAIN );
    } else if ( cls_state >= ca_loading ) {
        CL_PrepRefresh();
    }

    /* switch back to original state */
    cls.state = cls_state;

}

/*
============
CL_LocalConnect
============
*/
void CL_LocalConnect( void ) {
    if ( FS_NeedRestart() ) {
        cls.state = ca_challenging;
        CL_RestartFilesystem();
    }
}

static void cl_gun_changed( cvar_t *self ) {
    CL_UpdateGunSetting();
}

static void info_hand_changed( cvar_t *self ) {
    CL_UpdateGunSetting();
}

static void cl_gibs_changed( cvar_t *self ) {
    CL_UpdateGibSetting();
}

static void cl_footsteps_changed( cvar_t *self ) {
    CL_UpdateFootstepsSetting();
}

static void cl_predict_changed( cvar_t *self ) {
    CL_UpdatePredictSetting();
}

static void CL_GetClientStatus( clientStatus_t *status ) {
	if( !status ) {
		Com_Error( ERR_DROP, "CL_GetClientStatus: NULL" );
	}
	status->connState = cls.state;
	status->connectCount = cls.connectCount;
	status->demoplayback = cls.demoplayback;
	status->servername = cls.servername;
	status->mapname = cl.mapname;
	status->fullname = cl.configstrings[CS_NAME];
	status->loadingString = cls.state > ca_connected ?
        cl.loadingString : cls.messageString;
}

void CL_FillAPI( clientAPI_t *api ) {
	api->StartLocalSound = S_StartLocalSound;
	api->StopAllSounds = S_StopAllSounds;
	api->SendStatusRequest = CL_SendStatusRequest;
	api->GetClientStatus = CL_GetClientStatus;
	api->GetDemoInfo = CL_GetDemoInfo;
	api->UpdateScreen = SCR_UpdateScreen;
}

static const cmdreg_t c_client[] = {
    { "cmd", CL_ForwardToServer_f },
    { "pause", CL_Pause_f },
    { "pingservers", CL_PingServers_f },
    { "skins", CL_Skins_f },
    { "userinfo", CL_Userinfo_f },
    { "snd_restart", CL_Snd_Restart_f },
    { "changing", CL_Changing_f },
    { "disconnect", CL_Disconnect_f },
    { "connect", CL_Connect_f, CL_Connect_g },
    { "passive", CL_PassiveConnect_f },
    { "reconnect", CL_Reconnect_f },
    { "rcon", CL_Rcon_f },
    { "setenv", CL_Setenv_f },
    { "precache", CL_Precache_f },
    { "download", CL_Download_f },
    { "serverstatus", CL_ServerStatus_f, CL_Connect_g },
    { "dumpclients", CL_DumpClients_f },
	{ "dumpstatusbar", CL_DumpStatusbar_f },
	{ "dumplayout", CL_DumpLayout_f },
    { "vid_restart", CL_RestartRefresh_f },

    //
    // forward to server commands
    //
    // the only thing this does is allow command completion
    // to work -- all unknown commands are automatically
    // forwarded to the server
    { "wave" }, { "inven" }, { "kill" }, { "use" },
    { "drop" }, { "say" }, { "say_team" }, { "info" },
    { "prog" }, { "give" }, { "god" }, { "notarget" },
    { "noclip" }, { "invuse" }, { "invprev" }, { "invnext" },
    { "invdrop" }, { "weapnext" }, { "weapprev" }, { "vote" },
    { "observe" }, { "follow" }, { "time" }, { "playernext" },
	{ "playerprev" }, { "playertoggle" }, { "!mvdadmin" },

    { NULL }
};

/*
=================
CL_InitLocal
=================
*/
void CL_InitLocal ( void ) {
    int i;

    cls.state = ca_disconnected;
    cls.realtime = 0;

	CL_FillAPI( &client );

    CL_RegisterInput();

    CL_InitDemos();

    LOC_Init();

    Cmd_Register( c_client );

    for ( i = 0 ; i < MAX_LOCAL_SERVERS ; i++ ) {
        Cvar_Get( va( "adr%i", i ), "", CVAR_ARCHIVE );
    }

    //
    // register our variables
    //
    cl_add_blend = Cvar_Get ( "cl_blend", "1", CVAR_ARCHIVE );
    cl_add_lights = Cvar_Get ( "cl_lights", "1", 0 );
    cl_add_particles = Cvar_Get ( "cl_particles", "1", 0 );
    cl_add_entities = Cvar_Get ( "cl_entities", "1", 0 );
    cl_gun = Cvar_Get ( "cl_gun", "1", 0 );
	cl_gun->changed = cl_gun_changed;
    cl_footsteps = Cvar_Get( "cl_footsteps", "1", 0 );
	cl_footsteps->changed = cl_footsteps_changed;
    cl_noskins = Cvar_Get ( "cl_noskins", "0", 0 );
    cl_autoskins = Cvar_Get ( "cl_autoskins", "0", 0 );
    cl_predict = Cvar_Get ( "cl_predict", "1", 0 );
	cl_predict->changed = cl_predict_changed;
    cl_kickangles = Cvar_Get( "cl_kickangles", "1", CVAR_CHEAT );
	cl_maxfps = Cvar_Get( "cl_maxfps", "60", CVAR_ARCHIVE );
	cl_async = Cvar_Get( "cl_async", "1", CVAR_ARCHIVE );
	r_maxfps = Cvar_Get( "r_maxfps", "0", CVAR_ARCHIVE );

    cl_shownet = Cvar_Get( "cl_shownet", "0", 0 );
    cl_showmiss = Cvar_Get ( "cl_showmiss", "0", 0 );
    cl_showclamp = Cvar_Get ( "showclamp", "0", 0 );
    cl_timeout = Cvar_Get ( "cl_timeout", "120", 0 );

    rcon_password = Cvar_Get ( "rcon_password", "", CVAR_PRIVATE );
    rcon_address = Cvar_Get ( "rcon_address", "", CVAR_PRIVATE );

    cl_thirdperson = Cvar_Get( "cl_thirdperson", "0", CVAR_CHEAT );
    cl_thirdperson_angle = Cvar_Get( "cl_thirdperson_angle", "0", 0 );
    cl_thirdperson_range = Cvar_Get( "cl_thirdperson_range", "60", 0 );

    cl_railtrail_type = Cvar_Get( "cl_railtrail_type", "0", 0 );
    cl_railtrail_time = Cvar_Get( "cl_railtrail_time", "1.0", 0 );
    cl_railtrail_alpha = Cvar_Get( "cl_railtrail_alpha", "1.0", 0 );
    cl_railcore_color = Cvar_Get( "cl_railcore_color", "0xFF0000", 0 );
    cl_railcore_width = Cvar_Get( "cl_railcore_width", "3", 0 );
    cl_railspiral_color = Cvar_Get( "cl_railspiral_color", "0x0000FF", 0 );
    cl_railspiral_radius = Cvar_Get( "cl_railspiral_radius", "3", 0 );

    cl_disable_particles = Cvar_Get( "cl_disable_particles", "0", 0 );
	cl_disable_explosions = Cvar_Get( "cl_disable_explosions", "0", 0 );
    cl_gibs = Cvar_Get( "cl_gibs", "1", 0 );
	cl_gibs->changed = cl_gibs_changed;

    cl_chat_notify = Cvar_Get( "cl_chat_notify", "1", 0 );
    cl_chat_sound = Cvar_Get( "cl_chat_sound", "misc/talk.wav", 0 );
    cl_chat_filter = Cvar_Get( "cl_chat_filter", "0", 0 );

    cl_disconnectcmd = Cvar_Get( "cl_disconnectcmd", "", 0 );
    cl_changemapcmd = Cvar_Get( "cl_changemapcmd", "", 0 );
    cl_beginmapcmd = Cvar_Get( "cl_beginmapcmd", "", 0 );

    cl_protocol = Cvar_Get( "cl_protocol", "0", 0 );

    gender_auto = Cvar_Get ( "gender_auto", "1", CVAR_ARCHIVE );

    cl_vwep = Cvar_Get ( "cl_vwep", "1", CVAR_ARCHIVE );

    //
    // userinfo
    //
    info_password = Cvar_Get( "password", "", CVAR_USERINFO );
	info_spectator = Cvar_Get( "spectator", "0", CVAR_USERINFO );
    info_name = Cvar_Get( "name", "unnamed", CVAR_USERINFO | CVAR_ARCHIVE );
    info_skin = Cvar_Get( "skin", "male/grunt", CVAR_USERINFO | CVAR_ARCHIVE );
	info_rate = Cvar_Get( "rate", "5000", CVAR_USERINFO | CVAR_ARCHIVE );
    info_msg = Cvar_Get( "msg", "1", CVAR_USERINFO | CVAR_ARCHIVE );
    info_hand = Cvar_Get( "hand", "0", CVAR_USERINFO | CVAR_ARCHIVE );
	info_hand->changed = info_hand_changed;
    info_fov = Cvar_Get( "fov", "90", CVAR_USERINFO | CVAR_ARCHIVE );
    info_gender = Cvar_Get( "gender", "male", CVAR_USERINFO | CVAR_ARCHIVE );
    info_gender->modified = qfalse; // clear this so we know when user sets it manually
    info_uf = Cvar_Get( "uf", "0", CVAR_USERINFO );


    //
    // macros
    //
    Cmd_AddMacro( "cl_mapname", CL_Mapname_m );
    Cmd_AddMacro( "cl_server", CL_Server_m );
	Cmd_AddMacro( "cl_timer", CL_Timer_m );
	Cmd_AddMacro( "cl_ups", CL_Ups_m );
	Cmd_AddMacro( "cl_fps", CL_Fps_m );
	Cmd_AddMacro( "cl_ping", CL_Ping_m );
	Cmd_AddMacro( "cl_health", CL_Health_m );
	Cmd_AddMacro( "cl_ammo", CL_Ammo_m );
	Cmd_AddMacro( "cl_armor", CL_Armor_m );
}

/*
==================
CL_CheatsOK
==================
*/
qboolean CL_CheatsOK( void ) {
    if ( cls.state < ca_connected ) {
        return qtrue;
    }

    if ( cls.demoplayback ) {
        return qtrue;
    }

    // developer option
	if ( sv_running->integer ) {
		if( sv_running->integer != ss_game || Cvar_VariableInteger( "cheats" ) ) {
			return qtrue;
		}
    }

    // single player can cheat
    if ( atoi( cl.configstrings[ CS_MAXCLIENTS ] ) < 2 ) {
        return qtrue;
    }

    return qfalse;
}

//============================================================================

void CL_AppActivate( qboolean active ) {
	cls.appactive = active;
    Key_ClearStates();
    CL_InputActivate();
    S_Activate( active );
}

/*
==================
CL_SetClientTime
==================
*/
static void CL_SetClientTime( void ) {
    int	prevtime;

    prevtime = cl.oldframe.number * 100;
    if ( prevtime >= cl.serverTime ) {
        /* this may happen on a very first frame */
        cl.lerpfrac = 0;
        return;
    }
    if( sv_paused->integer ) {
        return;
    }

    if ( cl.time > cl.serverTime ) {
        if ( cl_showclamp->integer )
            Com_Printf( "high clamp %i\n", cl.time - cl.serverTime );
        cl.time = cl.serverTime;
        cl.lerpfrac = 1.0;
    } else if ( cl.time < prevtime ) {
        if ( cl_showclamp->integer )
            Com_Printf( "low clamp %i\n", cl.serverTime - prevtime );
        cl.time = prevtime;
        cl.lerpfrac = 0;
    } else {
        cl.lerpfrac = ( float ) ( cl.time - prevtime ) / ( float ) ( cl.serverTime - prevtime );
    }

    if ( com_timedemo->integer ) {
        cl.lerpfrac = 1.0;
    }

    if ( cl_showclamp->integer == 2 )
        Com_Printf( "time %i, lerpfrac %.3f\n", cl.time, cl.lerpfrac );

}

static void CL_MeasureStats( void ) {
	int time = Sys_Milliseconds();

	cls.measureFramecount++;

    if( cls.measureTime > time ) {
        cls.measureTime = time;
    }
	if( time - cls.measureTime < 1000 ) {
        return;
    }

    if( cls.netchan ) {
        int ack = cls.netchan->incoming_acknowledged;
        int ping = 0;
        int i, j;

        i = ack - 16 + 1;
        if( i < cl.initialSeq ) {
            i = cl.initialSeq;
        }
		for( j = i; j <= ack; j++ ) {
            client_history_t *h = &cl.history[j & CMD_MASK];
            if( h->rcvd > h->sent ) {
	            ping += h->rcvd - h->sent;
            }
		}

        if( j != i ) {
    		cls.ping = ping / ( j - i );
        }
    }

	cls.measureTime = time;
	cls.fps = cls.measureFramecount;
	cls.measureFramecount = 0;
}

#if USE_AUTOREPLY
/*
====================
CL_CheckForReply
====================
*/
static void CL_CheckForReply( void ) {
    if ( !cl.replyTime ) {
        return;
    }

    if ( cls.realtime < cl.replyTime ) {
        return;
    }

    Cbuf_AddText( "cmd say \"" );
	Cbuf_AddText( Cvar_VariableString( "version" ) );
    Cbuf_AddText( "\"\n" );

    cl.replyTime = 0;
}
#endif

static void CL_CheckTimeout( void ) {
    if( cls.netchan->last_received > com_localTime ) {
        cls.netchan->last_received = com_localTime;
    }
    if( com_localTime - cls.netchan->last_received > cl_timeout->value * 1000 ) 
    {
        // timeoutcount saves debugger
        if ( ++cl.timeoutcount > 5 ) {
            Com_Error( ERR_DISCONNECT, "Server connection timed out." );
        }
    } else {
        cl.timeoutcount = 0;
    }
}


/*
==================
CL_Frame
 
==================
*/
void CL_Frame( int msec ) {
	static int ref_extra, phys_extra, main_extra;
	int ref_msec, phys_msec;
    qboolean phys_frame, ref_frame;

	time_after_ref = time_before_ref = 0;

	if( !cl_running->integer ) {
        // still run cmd buffer in dedicated mode
        if( cmd_buffer.waitCount > 0 ) {
            cmd_buffer.waitCount--;
        }
        return;
	}

	ref_extra += msec;
    main_extra += msec;
	cls.realtime += msec;

	ref_msec = 1;
	if( cl_async->integer ) {
		phys_extra += msec;
        phys_frame = qtrue;

		Cvar_ClampInteger( cl_maxfps, 10, 120 );
		phys_msec = 1000 / cl_maxfps->integer;
		if( phys_extra < phys_msec ) {
            phys_frame = qfalse;
		}

		if( r_maxfps->integer ) {
			if( r_maxfps->integer < 10 ) {
				Cvar_Set( "r_maxfps", "10" );
			}
			ref_msec = 1000 / r_maxfps->integer;
		}
	} else {
        phys_frame = qtrue;
		if( cl_maxfps->integer ) {
			if( cl_maxfps->integer < 10 ) {
				Cvar_Set( "cl_maxfps", "10" );
			}
			ref_msec = 1000 / cl_maxfps->integer;
		}
	}

    ref_frame = qtrue;
	if( !com_timedemo->integer ) {
        if( !sv_running->integer ) {
            if( !cls.appactive ) {
                // run at 10 fps if background app
                if( main_extra < 100 ) {
                    NET_Sleep( 100 - main_extra );
                    return;
                }
            } else if( cls.state < ca_active ) {
                // run at 60 fps if not active
                if( main_extra < 16 ) {
                    NET_Sleep( 16 - main_extra );
                    return;
                }
            }
        }
        if( ref_extra < ref_msec ) {
            if( !cl_async->integer ) {
                return; // everything ticks in sync with refresh
            }
            ref_frame = qfalse;
        }
    }

    if ( cls.demoplayback ) { // FIXME: HACK
        if( cl_paused->integer ) {
            if( !sv_paused->integer ) {
                Cvar_Set( "sv_paused", "1" );
            }
        } else {
            if( sv_paused->integer ) {
                Cvar_Set( "sv_paused", "0" );
            }
        }
    }
    
    // decide the simulation time
    cls.frametime = main_extra * 0.001f;

    if( cls.frametime > 1.0 / 5 )
        cls.frametime = 1.0 / 5;

	if( !sv_paused->integer ) {
	    cl.time += main_extra;
    }

    // read next demo frame
    if( cls.demoplayback ) {
        CL_DemoFrame();
    }

    // calculate local time
    CL_SetClientTime();

#if USE_AUTOREPLY
    // check for version reply
    CL_CheckForReply();
#endif

    // resend a connection request if necessary
    CL_CheckForResend();

    // read user intentions
    CL_UpdateCmd( main_extra );

    // finalize pending cmd
    phys_frame |= cl.sendPacketNow;
    if( phys_frame ) {
        CL_FinalizeCmd();
        phys_extra = 0;
    }

    // send pending cmds
    CL_SendCmd();

    // predict all unacknowledged movements
    CL_PredictMovement();

    if( ref_frame ) {
        // update the screen
        if ( host_speeds->integer )
            time_before_ref = Sys_Milliseconds();

        SCR_UpdateScreen();

        if ( host_speeds->integer )
            time_after_ref = Sys_Milliseconds();

        // update audio after the 3D view was drawn
        S_Update();

	    ref_extra = 0;
    }

    // advance local effects for next frame
    CL_RunDLights();
    CL_RunLightStyles();
    Con_RunConsole();

    // check connection timeout
    if( cls.netchan ) {
        CL_CheckTimeout();
    }

	CL_MeasureStats();

    cls.framecount++;

    main_extra = 0;
}

//============================================================================

static byte     demo_buffer[MAX_PACKETLEN_WRITABLE_DEFAULT];

/*
====================
CL_Init
====================
*/
void CL_Init( void ) {
    if ( dedicated->integer ) {
        return; // nothing running on the client
    }

    if ( cl_running->integer ) {
        return;
    }

    // all archived variables will now be loaded

#ifdef _WIN32
    CL_InitRefresh();
    S_Init();	// sound must be initialized after window is created
#else
    S_Init();
    CL_InitRefresh();
#endif

    V_Init();
    SCR_Init();
    CL_InitLocal();
    CL_InitInput();
    SCR_RegisterMedia();
    Con_SetupDC();
    CL_InitUI();

#if USE_ZLIB
    if( inflateInit2( &cls.z, -15 ) != Z_OK ) {
        Com_Error( ERR_FATAL, "inflateInit2() failed" );
    }
#endif

    SZ_Init( &cls.demobuff, demo_buffer, sizeof( demo_buffer ) );

    UI_OpenMenu( UIMENU_MAIN );

    Con_PostInit();
    Con_RunConsole();

    Cvar_Set( "cl_running", "1" );
}


/*
===============
CL_Shutdown
 
FIXME: this is a callback from Sys_Quit and Com_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void CL_Shutdown( void ) {
    static qboolean isdown = qfalse;

    if ( isdown ) {
        Com_Printf( "CL_Shutdown: recursive shutdown\n" );
        return;
    }
    isdown = qtrue;

    CL_Disconnect( ERR_SILENT, NULL );

#if USE_ZLIB
    inflateEnd( &cls.z );
#endif

    CL_ShutdownUI();
    S_Shutdown();
    CL_ShutdownInput();
    Con_Shutdown();
    CL_ShutdownRefresh();

    Cvar_Set( "cl_running", "0" );

    isdown = qfalse;
}
