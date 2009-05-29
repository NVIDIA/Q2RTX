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
#include "d_md2.h"

cvar_t  *adr0;
cvar_t  *adr1;
cvar_t  *adr2;
cvar_t  *adr3;
cvar_t  *adr4;
cvar_t  *adr5;
cvar_t  *adr6;
cvar_t  *adr7;
cvar_t  *adr8;

cvar_t  *rcon_address;

cvar_t  *cl_noskins;
cvar_t  *cl_footsteps;
cvar_t  *cl_timeout;
cvar_t  *cl_predict;
cvar_t  *cl_gun;
cvar_t  *cl_maxfps;
cvar_t  *cl_async;
cvar_t  *r_maxfps;

cvar_t  *cl_add_particles;
cvar_t  *cl_add_lights;
cvar_t  *cl_add_entities;
cvar_t  *cl_add_blend;
cvar_t  *cl_kickangles;
cvar_t  *cl_rollhack;

cvar_t  *cl_shownet;
cvar_t  *cl_showmiss;
cvar_t  *cl_showclamp;

cvar_t  *cl_thirdperson;
cvar_t  *cl_thirdperson_angle;
cvar_t  *cl_thirdperson_range;

cvar_t  *cl_disable_particles;
cvar_t  *cl_disable_explosions;
cvar_t  *cl_chat_notify;
cvar_t  *cl_chat_sound;
cvar_t  *cl_chat_filter;

cvar_t  *cl_disconnectcmd;
cvar_t  *cl_changemapcmd;
cvar_t  *cl_beginmapcmd;

cvar_t  *cl_gibs;
#if USE_FPS
cvar_t  *cl_updaterate;
#endif

cvar_t  *cl_protocol;

cvar_t  *gender_auto;

cvar_t  *cl_vwep;

//
// userinfo
//
cvar_t  *info_password;
cvar_t  *info_spectator;
cvar_t  *info_name;
cvar_t  *info_skin;
cvar_t  *info_rate;
cvar_t  *info_fov;
cvar_t  *info_msg;
cvar_t  *info_hand;
cvar_t  *info_gender;
cvar_t  *info_uf;

client_static_t cls;
client_state_t  cl;

centity_t   cl_entities[ MAX_EDICTS ];

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
    unsigned time;
} request_t;

#define MAX_REQUESTS    64
#define REQUEST_MASK    ( MAX_REQUESTS - 1 )

static request_t    clientRequests[MAX_REQUESTS];
static int          currentRequest;

static request_t *CL_AddRequest( const netadr_t *adr, requestType_t type ) {
    request_t *r;

    r = &clientRequests[currentRequest & REQUEST_MASK];
    currentRequest++;

    r->adr = *adr;
    r->type = type;
    r->time = cls.realtime;

    return r;
}

/*
===================
CL_UpdateGunSetting
===================
*/
static void CL_UpdateGunSetting( void ) {
    int nogun;

    if( cls.state < ca_connected ) {
        return;
    }
    if( cls.serverProtocol < PROTOCOL_VERSION_R1Q2 ) {
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
    if( cls.state < ca_connected ) {
        return;
    }

    if( cls.serverProtocol != PROTOCOL_VERSION_Q2PRO ) {
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
    if( cls.state < ca_connected ) {
        return;
    }
    if( cls.serverProtocol != PROTOCOL_VERSION_Q2PRO ) {
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
    if( cls.state < ca_connected ) {
        return;
    }
    if( cls.serverProtocol != PROTOCOL_VERSION_Q2PRO ) {
        return;
    }

    MSG_WriteByte( clc_setting );
    MSG_WriteShort( CLS_NOPREDICT );
    MSG_WriteShort( !cl_predict->integer );
    MSG_FlushTo( &cls.netchan->message );
}

#if USE_FPS
static void CL_UpdateRateSetting( void ) {
    if( cls.state < ca_connected ) {
        return;
    }
    if( cls.serverProtocol != PROTOCOL_VERSION_R1Q2 ) {
        return;
    }

    MSG_WriteByte( clc_setting );
    MSG_WriteShort( CLS_FPS );
    MSG_WriteShort( cl_updaterate->integer );
    MSG_FlushTo( &cls.netchan->message );
}
#endif

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
CL_ForwardToServer
 
adds the current command line as a clc_stringcmd to the client message.
things like godmode, noclip, etc, are commands directed to the server,
so when they are typed in at the console, they will need to be forwarded.
===================
*/
qboolean CL_ForwardToServer( void ) {
    char    *cmd;

    cmd = Cmd_Argv( 0 );
    if( cls.state < ca_active || *cmd == '-' || *cmd == '+' ) {
        return qfalse;
    }

    CL_ClientCommand( Cmd_RawArgsFrom( 0 ) );
    return qtrue;
}

/*
==================
CL_ForwardToServer_f
==================
*/
static void CL_ForwardToServer_f( void ) {
    if ( cls.state < ca_connected ) {
        Com_Printf( "Can't \"%s\", not connected\n", Cmd_Argv( 0 ) );
        return;
    }

    if ( cls.demo.playback ) {
        return;
    }

    // don't forward the first argument
    if ( Cmd_Argc() > 1 ) {
        CL_ClientCommand( Cmd_RawArgs() );
    }
}

/*
==================
CL_Pause_f
==================
*/
static void CL_Pause_f( void ) {
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

    if ( cls.demo.playback ) {
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
        cls.connect_time = cls.realtime - CONNECT_DELAY;

        cls.passive = qfalse;

        Con_Close();
#if USE_UI
        UI_OpenMenu( UIMENU_NONE );
#endif
    }

    // resend if we haven't gotten a reply yet
    if( cls.state != ca_connecting && cls.state != ca_challenging ) {
        return;
    }

    if( cls.realtime - cls.connect_time < CONNECT_DELAY ) {
        return;
    }

    cls.connect_time = cls.realtime;    // for retransmit requests
    cls.connect_count++;

    if ( cls.state == ca_challenging ) {
        Com_Printf( "Requesting challenge... %i\n", cls.connect_count );
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
    Com_Printf( "Requesting connection... %i\n", cls.connect_count );

    cls.userinfo_modified = 0;

    // add protocol dependent stuff
    switch( cls.serverProtocol ) {
    case PROTOCOL_VERSION_R1Q2:
        Q_snprintf( tail, sizeof( tail ), " %d %d",
            net_maxmsglen->integer, PROTOCOL_VERSION_R1Q2_CURRENT );
        cls.quakePort = net_qport->integer & 0xff;
        break;
    case PROTOCOL_VERSION_Q2PRO:
        Q_snprintf( tail, sizeof( tail ), " %d %d %d %d",
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
    Netchan_OutOfBand( NS_CLIENT, &cls.serverAddress,
        "connect %i %i %i \"%s\"%s\n", cls.serverProtocol, cls.quakePort,
        cls.challenge, userinfo, tail );
}

static void CL_Connect_c( genctx_t *ctx, int argnum ) {
    if( argnum == 1 ) {
        Com_Address_g( ctx );
    } else if( argnum == 2 ) {
        if( !ctx->partial[0] || ( ctx->partial[0] == '3' && !ctx->partial[1] ) ) {
            Prompt_AddMatch( ctx, "34" );
            Prompt_AddMatch( ctx, "35" );
            Prompt_AddMatch( ctx, "36" );
        }
    }
}

/*
================
CL_Connect_f
 
================
*/
static void CL_Connect_f( void ) {
    char    *server;
    netadr_t    address;
    int protocol;
    int argc = Cmd_Argc();

    if( argc < 2 ) {
usage:
        Com_Printf( "Usage: %s <server> [34|35|36]\n", Cmd_Argv( 0 ) );
        return;
    }

    if( argc > 2 ) {
        protocol = atoi( Cmd_Argv( 2 ) );
        if( protocol < PROTOCOL_VERSION_DEFAULT ||
            protocol > PROTOCOL_VERSION_Q2PRO )
        {
            goto usage;
        }
    } else {
        protocol = cl_protocol->integer;
        if( !protocol ) {
            protocol = PROTOCOL_VERSION_Q2PRO;
        }
    }

    server = Cmd_Argv( 1 );
    if ( !NET_StringToAdr( server, &address, PORT_SERVER ) ) {
        Com_Printf( "Bad server address\n" );
        return;
    }

    // copy early to avoid potential cmd_argv[1] clobbering
    Q_strlcpy( cls.servername, server, sizeof( cls.servername ) );

    if ( sv_running->integer ) {
        // if running a local server, kill it and reissue
        SV_Shutdown( "Server was killed\n", KILL_DROP );
    }

    NET_Config( NET_CLIENT );

    CL_Disconnect( ERR_DISCONNECT, NULL );

    cls.serverAddress = address;
    cls.serverProtocol = protocol;
    cls.protocolVersion = 0;
    cls.passive = qfalse;
    cls.state = ca_challenging;
    cls.connect_count = 0;
    cls.connect_time = cls.realtime - CONNECT_DELAY;    // CL_CheckForResend() will fire immediately

    CL_CheckForResend();

    Cvar_Set( "cl_paused", "0" );
    Cvar_Set( "sv_paused", "0" );
    Cvar_Set( "timedemo", "0" );

    Con_Close();
#if USE_UI
    UI_OpenMenu( UIMENU_NONE );
#endif
}

static void CL_PassiveConnect_f( void ) {
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

void CL_SendRcon( const netadr_t *adr, const char *pass, const char *cmd ) {
    NET_Config( NET_CLIENT );

    CL_AddRequest( adr, REQ_RCON );

    Netchan_OutOfBand( NS_CLIENT, adr, "rcon \"%s\" %s", pass, cmd );
}


/*
=====================
CL_Rcon_f
 
  Send the rest of the command line over as
  an unconnected command.
=====================
*/
static void CL_Rcon_f( void ) {
    netadr_t    address;

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
        if( !NET_StringToAdr( rcon_address->string, &address, PORT_SERVER ) ) {
            Com_Printf( "Bad address: %s\n", rcon_address->string );
            return;
        }
    } else {
        address = cls.netchan->remote_address;
    }

    CL_SendRcon( &address, rcon_password->string, Cmd_RawArgs() );
}

static void CL_Rcon_c( genctx_t *ctx, int argnum ) {
    Com_Generic_c( ctx, argnum - 1 );
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
    BSP_Free( cl.bsp );
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
    if( cls.state > ca_disconnected && !cls.demo.playback ) {
        EXEC_TRIGGER( cl_disconnectcmd );
    }

#if USE_REF == REF_SOFT
    if( cls.ref_initialized ) {
        R_CinematicSetPalette( NULL );
    }
#endif

    cls.connect_time = 0;
    cls.connect_count = 0;
    cls.passive = qfalse;
#if USE_ICMP
    cls.errorReceived = qfalse;
#endif

    // stop demo
    if( cls.demo.recording ) {
        CL_Stop_f();
    }
    if( cls.demo.playback ) {
        FS_FCloseFile( cls.demo.playback );

        if( com_timedemo->integer ) {
            unsigned msec = Sys_Milliseconds();
            float sec = ( msec - cls.demo.time_start ) * 0.001f;
            float fps = cls.demo.time_frames / sec;

            Com_Printf( "%u frames, %3.1f seconds: %3.1f fps\n",
                cls.demo.time_frames, sec, fps );
        }
    }
    memset( &cls.demo, 0, sizeof( cls.demo ) );
    
    if( cls.netchan ) {
        // send a disconnect message to the server
        MSG_WriteByte( clc_stringcmd );
        MSG_WriteData( "disconnect", 11 );

        cls.netchan->Transmit( cls.netchan, msg_write.cursize, msg_write.data, 3 );

        SZ_Clear( &msg_write );
            
        Netchan_Close( cls.netchan );
        cls.netchan = NULL;
    }

    // stop download
    if( cls.download.file ) {
        FS_FCloseFile( cls.download.file );
    }
    memset( &cls.download, 0, sizeof( cls.download ) );

    CL_ClearState ();

    Cvar_Set( "cl_paused", "0" );

    cls.state = ca_disconnected;
    cls.messageString[ 0 ] = 0;
    cls.userinfo_modified = 0;

#if USE_UI
    UI_ErrorMenu( type, text );
#endif
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

static void CL_ServerStatus_c( genctx_t *ctx, int argnum ) {
    if( argnum == 1 ) {
        Com_Address_g( ctx );
    }
}

/*
================
CL_ServerStatus_f
================
*/
static void CL_ServerStatus_f( void ) {
    char        *s;
    netadr_t    adr;
    neterr_t    ret;

    if( Cmd_Argc() < 2 ) {
        if( !cls.netchan ) {
            Com_Printf( "Usage: %s [address]\n", Cmd_Argv( 0 ) );
            return;
        }
        adr = cls.netchan->remote_address;
    } else {
        s = Cmd_Argv( 1 );
        if( !NET_StringToAdr( s, &adr, PORT_SERVER ) ) {
            Com_Printf( "Bad address: %s\n", s );
            return;
        }
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
static qboolean CL_ServerStatusResponse( const char *status, size_t len, serverStatus_t *dest ) {
    const char *s;
    playerStatus_t *player;
    size_t infolen;

    memset( dest, 0, sizeof( *dest ) );

    s = memchr( status, '\n', len );
    if( !s ) {
        return qfalse;
    }
    infolen = s - status;
    if( !infolen || infolen >= MAX_STRING_CHARS ) {
        return qfalse;
    }
    s++;

    strcpy( dest->address, NET_AdrToString( &net_from ) );
    memcpy( dest->infostring, status, infolen );
    
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
        Q_strlcpy( player->name, COM_Parse( &s ), sizeof( player->name ) );

        if ( ++dest->numPlayers == MAX_STATUS_PLAYERS ) {
            break;
        }
    } while( s );

    qsort( dest->players, dest->numPlayers, sizeof( dest->players[ 0 ] ),
        SortPlayers );

    return qtrue;
}

void CL_DumpServerInfo( const serverStatus_t *status ) {
    char    key[MAX_STRING_CHARS];
    char    value[MAX_STRING_CHARS];
    const   playerStatus_t *player;
    const char    *infostring;
    int i;

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

    Com_Printf( "\nNum Score Ping Name\n" );
    for( i = 0, player = status->players; i < status->numPlayers; i++, player++ ) {
        Com_Printf( "%3i %5i %4i %s\n", i + 1, player->score, player->ping,
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
    char string[MAX_NET_STRING];
    int i, oldest;
    unsigned delta;
    size_t len;

    len = MSG_ReadString( string, sizeof( string ) );

    if ( ( cls.state == ca_challenging || cls.state == ca_connecting ) &&
            NET_IsEqualBaseAdr( &net_from, &cls.serverAddress ) )
    {
        // server rejected our connect request
        if( NET_IsLocalAddress( &cls.serverAddress ) ) {
            Com_Error( ERR_DROP, "Server rejected loopback connection" );
        }
        Com_Printf( "%s", string );
        Q_strlcpy( cls.messageString, string, sizeof( cls.messageString ) );
        cls.state = ca_challenging;
        //cls.connectCount = 0;
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
        delta = cls.realtime - r->time;
        if( r->adr.type == NA_BROADCAST ) {
            if( delta > 3000 ) {
                continue;
            }
        } else {
            if( delta > 6000 ) {
                break;
            }
            if( !NET_IsEqualBaseAdr( &net_from, &r->adr ) ) {
                continue;
            }
        }
        switch( r->type ) {
        case REQ_STATUS:
            if( CL_ServerStatusResponse( string, len, &serverStatus ) ) {
                CL_DumpServerInfo( &serverStatus );
            }
            break;
        case REQ_INFO:
            break;
#if USE_UI
        case REQ_PING:
            if( CL_ServerStatusResponse( string, len, &serverStatus ) ) {
                UI_AddToServerList( &serverStatus );
            }
            break;
#endif
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

    Com_DPrintf( "%s: dropped unrequested packet\n", __func__ );
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
    char    send[2048];
    int     i, l;
    char    *in, *out;
    netadr_t    adr;
 
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
    int i, j;
    char *s;

    if ( cls.state < ca_connected ) {
        return;
    }

    if ( cls.demo.recording )
        CL_Stop_f();

    S_StopAllSounds();

    Com_Printf( "Changing map...\n" );

    if( !cls.demo.playback ) {
        EXEC_TRIGGER( cl_changemapcmd );
    }

    SCR_BeginLoadingPlaque();

    cls.state = ca_connected;   // not active anymore, but not disconnected
    cl.mapname[0] = 0;
    cl.configstrings[CS_NAME][0] = 0;

    // parse additional parameters
    j = Cmd_Argc();
    for( i = 1; i < j; i++ ) {
        s = Cmd_Argv( i );
        if( !strncmp( s, "map=", 4 ) ) {
            Q_strlcpy( cl.mapname, s + 4, sizeof( cl.mapname ) );
        }
    }

    SCR_UpdateScreen();
}


/*
=================
CL_Reconnect_f
 
The server is changing levels
=================
*/
static void CL_Reconnect_f( void ) {
    if( cls.state >= ca_precached ) {
        CL_Disconnect( ERR_SILENT, NULL );
    }
    if( cls.state >= ca_connected ) {
        cls.state = ca_connected;

        if( cls.demo.playback ) {
            return;
        }
        if( cls.download.file ) {
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

    cls.connect_time = cls.realtime - CONNECT_DELAY;
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
qboolean CL_SendStatusRequest( char *buffer, size_t size ) {
    netadr_t    address;

    memset( &address, 0, sizeof( address ) );

    NET_Config( NET_CLIENT );

    if( !buffer ) {
        // send a broadcast packet
        address.type = NA_BROADCAST;
        address.port = BigShort( PORT_SERVER );
    } else {
        if ( !NET_StringToAdr( buffer, &address, PORT_SERVER ) ) {
            return qfalse;
        }

        // return resolved address
        if( size > 0 ) {
            Q_strlcpy( buffer, NET_AdrToString( &address ), size );
        }
    }

    CL_AddRequest( &address, REQ_PING );

    OOB_PRINT( NS_CLIENT, &address, "status" );

    // Com_ProcessEvents();

    return qtrue;
}


/*
=================
CL_PingServers_f
=================
*/
static void CL_PingServers_f( void ) {
    int i;
    char    buffer[32];
    cvar_t  *var;
    netadr_t    address;

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
    for( i = 0; i < 64; i++ ) {
        Q_snprintf( buffer, sizeof( buffer ), "adr%i", i );
        var = Cvar_FindVar( buffer );
        if( !var ) {
            break;
        }
        if( !var->string[0] )
            continue;

        if( !NET_StringToAdr( var->string, &address, PORT_SERVER ) ) {
            Com_Printf( "bad address: %s\n", var->string );
            continue;
        }

        Com_Printf( "pinging %s...\n", var->string );
        CL_AddRequest( &address, REQ_STATUS );

        OOB_PRINT( NS_CLIENT, &address, "status" );

        // Com_ProcessEvents();
        SCR_UpdateScreen();
    }
}

/*
=================
CL_Skins_f
 
Load or download any custom player skins and models
=================
*/
static void CL_Skins_f( void ) {
    int i;
    char *s;
    clientinfo_t *ci;

    for( i = 0 ; i < MAX_CLIENTS; i++ ) {
        s = cl.configstrings[ CS_PLAYERSKINS + i ];
        if( !s[0] )
            continue;
        ci = &cl.clientinfo[i];
        CL_LoadClientinfo( ci, s );
        Com_Printf( "client %d: %s --> %s/%s\n", i, s,
            ci->model_name, ci->skin_name );
        SCR_UpdateScreen();
    }
}

static void cl_noskins_changed( cvar_t *self ) {
    int i;
    char *s;
    clientinfo_t *ci;

    for( i = 0 ; i < MAX_CLIENTS; i++ ) {
        s = cl.configstrings[ CS_PLAYERSKINS + i ];
        if( !s[0] )
            continue;
        ci = &cl.clientinfo[i];
        CL_LoadClientinfo( ci, s );
    }
}

void CL_Name_g( genctx_t *ctx ) {
    int i;
    clientinfo_t *ci;

    if( cls.state < ca_loading ) {
        return;
    }

    for( i = 0; i < MAX_CLIENTS; i++ ) {
        ci = &cl.clientinfo[i];
        if( ci->name[0] && !Prompt_AddMatch( ctx, ci->name ) ) {
            break;
        }
    }
}


/*
=================
CL_ConnectionlessPacket
 
Responses to broadcasts, etc
=================
*/
static void CL_ConnectionlessPacket( void ) {
    char    string[MAX_STRING_CHARS];
    char    *s, *c;
    int     i, j, k;
    size_t  len;

    MSG_BeginReading();
    MSG_ReadLong(); // skip the -1

    len = MSG_ReadStringLine( string, sizeof( string ) );
    if( len >= sizeof( string ) ) {
        Com_DPrintf( "Oversize message received.  Ignored.\n" );
        return;
    }

    Cmd_TokenizeString( string, qfalse );

    c = Cmd_Argv( 0 );

    Com_DPrintf( "%s: %s\n", NET_AdrToString ( &net_from ), string );

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
        cls.connect_time = cls.realtime - CONNECT_DELAY;
        //cls.connectCount = 0;

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
        char mapname[MAX_QPATH];

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

        mapname[0] = 0;

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
            } else if( !strncmp( s, "map=", 4 ) ) {
                Q_strlcpy( mapname, s + 4, sizeof( mapname ) );
            }
        }

        Com_Printf( "Connected to %s (protocol %d).\n",
            NET_AdrToString( &cls.serverAddress ), cls.serverProtocol );
        if( cls.netchan ) {
            // this may happen after svc_reconnect
            Netchan_Close( cls.netchan );
        }
        cls.netchan = Netchan_Setup( NS_CLIENT, type, &cls.serverAddress,
                cls.quakePort, 1024, cls.serverProtocol );

#if USE_AC_CLIENT
        if( anticheat ) {
            MSG_WriteByte( clc_nop );
            MSG_FlushTo( &cls.netchan->message );
            cls.netchan->Transmit( cls.netchan, 0, NULL, 3 );
            S_StopAllSounds();
            cls.connect_count = -1;
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
        cls.connect_count = 0;
        strcpy( cl.mapname, mapname ); // for levelshot screen
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
        Q_strlcpy( cls.servername, s, sizeof( cls.servername ) );
        cls.passive = qfalse;

        cls.state = ca_challenging;
        cls.connect_time = cls.realtime - CONNECT_DELAY;
        cls.connect_count = 0;

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
static void CL_PacketEvent( void ) {
    //
    // remote command packet
    //
    if( *( int * )msg_read.data == -1 ) {
        CL_ConnectionlessPacket();
        return;
    }

    if( cls.state < ca_connected ) {
        return;
    }

    if( !cls.netchan ) {
        return;     // dump it if not connected
    }

    if( msg_read.cursize < 8 ) {
        Com_DPrintf( "%s: runt packet\n", NET_AdrToString( &net_from ) );
        return;
    }

    //
    // packet from server
    //
    if( !NET_IsEqualAdr( &net_from, &cls.netchan->remote_address ) ) {
        Com_DPrintf( "%s: sequenced packet without connection\n",
            NET_AdrToString( &net_from ) );
        return;
    }

    if( !cls.netchan->Process( cls.netchan ) )
        return;     // wasn't accepted for some reason

#if USE_ICMP
    cls.errorReceived = qfalse; // don't drop
#endif

    CL_ParseServerMessage();

    CL_AddNetgraph();

    SCR_LagSample();
}

#if USE_ICMP
void CL_ErrorEvent( void ) {
    //
    // error packet from server
    //
    if( cls.state < ca_connected ) {
        return;
    }
    if( !cls.netchan ) {
        return;     // dump it if not connected
    }
    if( !NET_IsEqualBaseAdr( &net_from, &cls.netchan->remote_address ) ) {
        return;
    }
    if( net_from.port && net_from.port != cls.netchan->remote_address.port ) {
        return;
    }

    cls.errorReceived = qtrue; // drop connection soon
}
#endif


//=============================================================================

/*
==============
CL_FixUpGender_f
==============
*/
static void CL_FixUpGender( void ) {
    char *p;
    char sk[MAX_QPATH];

    Q_strlcpy( sk, info_skin->string, sizeof( sk ) );
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
    int i;
    char    *s;

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
CL_RestartSound_f
 
Restart the sound subsystem so it can pick up
new parameters and flush all sounds
=================
*/
static void CL_RestartSound_f( void ) {
    S_Shutdown();
    S_Init();
    CL_RegisterSounds();
}

/*
=================
CL_PlaySound_f

Moved here from sound code so that command is always registered.
=================
*/
static void CL_PlaySound_c( genctx_t *ctx, int state ) {
    FS_File_g( "sound", "*.wav", FS_SEARCH_SAVEPATH | FS_SEARCH_BYFILTER | 0x80000000, ctx );
}

static void CL_PlaySound_f( void ) {
    int     i;
    char name[MAX_QPATH];

    if( Cmd_Argc() < 2 ) {
        Com_Printf( "Usage: %s <sound> [...]\n", Cmd_Argv( 0 ) );
        return;
    }

    for( i = 1; i < Cmd_Argc(); i++ ) {
        Cmd_ArgvBuffer( i, name, sizeof( name ) );
        COM_DefaultExtension( name, ".wav", sizeof( name ) );
        S_StartLocalSound( name );
    }
}


static void CL_RegisterModels( void ) {
    int i;
    char *name;

    for ( i = 1; i < MAX_MODELS; i++ ) {
        name = cl.configstrings[CS_MODELS+i];
        if( !name[0] ) {
            break;
        }
        if( name[0] == '*' )
            cl.model_clip[i] = BSP_InlineModel( cl.bsp, name );
        else
            cl.model_clip[i] = NULL;
    }
}

void CL_LoadState( load_state_t state ) {
    cl.load_state = state;
    cl.load_time[state] = Sys_Milliseconds();

    // Com_ProcessEvents(); 
    SCR_UpdateScreen();
}

static int precache_check; // for autodownload of precache items
static int precache_spawncount;
static int precache_tex;
static int precache_model_skin;

static void *precache_model; // used for skin checking in alias models

#define PLAYER_MULT 5

// ENV_CNT is map load, ENV_CNT+1 is first env map
#define ENV_CNT (CS_PLAYERSKINS + MAX_CLIENTS * PLAYER_MULT)
#define TEXTURE_CNT (ENV_CNT+13)

static const char env_suf[6][3] = { "rt", "bk", "lf", "ft", "up", "dn" };

void CL_RequestNextDownload ( void ) {
    char fn[ MAX_QPATH ];
    dmd2header_t *pheader;
    size_t length;

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
                size_t num_skins, ofs_skins, end_skins;

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
                    pheader = ( dmd2header_t * ) precache_model;
                    if( length < sizeof( *pheader ) ||
                        LittleLong( pheader->ident ) != MD2_IDENT ||
                        LittleLong( pheader->version ) != MD2_VERSION )
                    {
                        // not an alias model
                        FS_FreeFile( precache_model );
                        precache_model = NULL;
                        precache_model_skin = 0;
                        precache_check++;
                        continue;
                    }
                    num_skins = LittleLong( pheader->num_skins );
                    ofs_skins = LittleLong( pheader->ofs_skins );
                    end_skins = ofs_skins + num_skins * MD2_MAX_SKINNAME;
                    if( num_skins > MD2_MAX_SKINS || end_skins < ofs_skins || end_skins > length ) {
                        // bad alias model
                        FS_FreeFile( precache_model );
                        precache_model = NULL;
                        precache_model_skin = 0;
                        precache_check++;
                        continue;
                    }
                }

                pheader = ( dmd2header_t * ) precache_model;
                num_skins = LittleLong( pheader->num_skins );
                ofs_skins = LittleLong( pheader->ofs_skins );

                while ( precache_model_skin - 1 < num_skins ) {
                    Q_strlcpy( fn, ( char * )precache_model + ofs_skins +
                        ( precache_model_skin - 1 ) * MD2_MAX_SKINNAME, sizeof( fn ) );
                    if ( !CL_CheckOrDownloadFile( fn ) ) {
                        precache_model_skin++;
                        return; // started a download
                    }
                    precache_model_skin++;
                }
                FS_FreeFile( precache_model );
                precache_model = NULL;
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
        if ( allow_download_pics->integer ) {
            if ( precache_check == CS_IMAGES )
                precache_check++; // zero is blank
            while ( precache_check < CS_IMAGES + MAX_IMAGES &&
                    cl.configstrings[ precache_check ][ 0 ] ) {
                Q_concat( fn, sizeof( fn ), "pics/", cl.configstrings[ precache_check++ ], ".pcx", NULL );
                if ( !CL_CheckOrDownloadFile( fn ) )
                    return; // started a download
            }
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
                Q_strlcpy( model, p, sizeof( model ) );
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

        if( ( cl.bsp = BSP_Load( cl.configstrings[ CS_MODELS + 1 ] ) ) == NULL ) {
            Com_Error( ERR_DROP, "Couldn't load %s: %s",
                cl.configstrings[ CS_MODELS + 1 ], BSP_GetError() );
        }

#if USE_MAPCHECKSUM
        if ( cl.bsp->checksum != atoi( cl.configstrings[ CS_MAPCHECKSUM ] ) ) {
            Com_Error ( ERR_DROP, "Local map version differs from server: %i != '%s'\n",
                        cl.bsp->checksum, cl.configstrings[ CS_MAPCHECKSUM ] );
            return;
        }
#endif
        CL_RegisterModels();
    }

    if ( precache_check > ENV_CNT && precache_check < TEXTURE_CNT ) {
        if ( allow_download->integer && allow_download_textures->integer ) {
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
        if ( allow_download->integer && allow_download_textures->integer ) {
            while ( precache_tex < cl.bsp->numtexinfo ) {
                char *texname = cl.bsp->texinfo[ precache_tex++ ].name;

                // Also check if 32bit images are present
                Q_concat( fn, sizeof( fn ), "textures/", texname, ".jpg", NULL );
                if ( FS_LoadFile( fn, NULL ) == INVALID_LENGTH ) {
                    Q_concat( fn, sizeof( fn ), "textures/", texname, ".tga", NULL );
                    if ( FS_LoadFile( fn, NULL ) == INVALID_LENGTH ) {
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

    CL_PrepRefresh ();

    CL_LoadState( LOAD_SOUNDS );
    CL_RegisterSounds ();

    LOC_LoadLocations();
    
    CL_LoadState( LOAD_FINISH );

    CL_ClientCommand( va( "begin %i\n", precache_spawncount ) );

    Cvar_FixCheats();

    CL_UpdateGunSetting();
    CL_UpdateGibSetting();
    CL_UpdateFootstepsSetting();
    CL_UpdatePredictSetting();
#if USE_FPS
    CL_UpdateRateSetting();
#endif

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
    if( cls.state < ca_connected ) {
        return;
    }

    cls.state = ca_loading;
    CL_LoadState( LOAD_MAP );

    S_StopAllSounds();

    //Yet another hack to let old demos work
    //the old precache sequence
    if( cls.demo.playback ) {
        if( ( cl.bsp = BSP_Load( cl.configstrings[ CS_MODELS + 1 ] ) ) == NULL ) {
            Com_Error( ERR_DROP, "Couldn't load %s: %s",
                cl.configstrings[ CS_MODELS + 1 ], BSP_GetError() );
        }
        CL_RegisterModels();
        CL_PrepRefresh();
        CL_LoadState( LOAD_SOUNDS );
        CL_RegisterSounds();
        CL_LoadState( LOAD_FINISH );
        cls.state = ca_precached;
        return;
    }

    precache_check = CS_MODELS;
    precache_spawncount = atoi( Cmd_Argv( 1 ) );
    if( precache_model ) {
        FS_FreeFile( precache_model );
        precache_model = NULL;
    }
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

static void dump_program( const char *text, const char *name ) {
    char buffer[MAX_OSPATH];
    fileHandle_t f;
    size_t len;

    if( cls.state != ca_active ) {
        Com_Printf( "Must be in a level to dump.\n" );
        return;
    }

    if( Cmd_Argc() != 2 ) {
        Com_Printf( "Usage: %s <filename>\n", Cmd_Argv( 0 ) );
        return;
    }

    if( !*text ) {
        Com_Printf( "No %s to dump.\n", name );
        return;
    }

    len = Q_concat( buffer, sizeof( buffer ), "layouts/", Cmd_Argv( 1 ), ".txt", NULL );
    if( len >= sizeof( buffer ) ) {
        Com_EPrintf( "Oversize filename specified.\n" );
        return;
    }

    FS_FOpenFile( buffer, &f, FS_MODE_WRITE );
    if( !f ) {
        Com_EPrintf( "Couldn't open %s for writing.\n", buffer );
        return;
    }

    FS_FPrintf( f, "%s\n", text );

    FS_FCloseFile( f );

    Com_Printf( "Dumped %s program to %s.\n", name, buffer );
}

static void CL_DumpStatusbar_f( void ) {
    dump_program( cl.configstrings[CS_STATUSBAR], "status bar" );
}

static void CL_DumpLayout_f( void ) {
    dump_program( cl.layout, "layout" );
}

static const cmd_option_t o_writeconfig[] = {
    { "a", "aliases", "write aliases" },
    { "b", "bindings", "write bindings" },
    { "c", "cvars", "write archived cvars" },
    { "h", "help", "display this help message" },
    { "m", "modified", "write modified cvars" },
    { NULL }
};

static void CL_WriteConfig_c( genctx_t *ctx, int argnum ) {
    Cmd_Option_c( o_writeconfig, Cmd_Config_g, ctx, argnum );
}

/*
===============
CL_WriteConfig_f
===============
*/
static void CL_WriteConfig_f( void ) {
    char buffer[MAX_OSPATH];
    qboolean aliases = qfalse, bindings = qfalse, modified = qfalse;
    int c, mask = 0;
    fileHandle_t f;
    size_t len;

    while( ( c = Cmd_ParseOptions( o_writeconfig ) ) != -1 ) {
        switch( c ) {
        case 'a':
            aliases = qtrue;
            break;
        case 'b':
            bindings = qtrue;
            break;
        case 'c':
            mask |= CVAR_ARCHIVE;
            break;
        case 'h':
            Cmd_PrintUsage( o_writeconfig, "<filename>" );
            Com_Printf( "Save current configuration into file.\n" );
            Cmd_PrintHelp( o_writeconfig );
            return;
        case 'm':
            modified = qtrue;
            mask = ~0;
            break;
        default:
            return;
        }
    }

    if( !cmd_optarg[0] ) {
        Com_Printf( "Missing filename argument.\n" );
        Cmd_PrintHint();
        return;
    }

    if( !aliases && !bindings && !mask ) {
        bindings = qtrue;
        mask = CVAR_ARCHIVE;
    }

    len = Q_concat( buffer, sizeof( buffer ), "configs/", cmd_optarg, ".cfg", NULL );
    if( len >= sizeof( buffer ) ) {
        Com_EPrintf( "Oversize filename specified.\n" );
        return;
    }

    FS_FOpenFile( buffer, &f, FS_MODE_WRITE );
    if( !f ) {
        Com_Printf( "Couldn't open %s for writing\n", buffer );
        return;
    }

    FS_FPrintf( f, "// generated by q2pro\n" );

    if( bindings ) {
        FS_FPrintf( f, "\n// key bindings\n" );
        Key_WriteBindings( f );
    }
    if( aliases ) {
        FS_FPrintf( f, "\n// command aliases\n" );
        Cmd_WriteAliases( f );
    }
    if( mask ) {
        FS_FPrintf( f, "\n//%s cvars\n", modified ? "modified" : "archived" );
        Cvar_WriteVariables( f, mask, modified );
    }
    
    FS_FCloseFile( f );

    Com_Printf( "Wrote %s.\n", buffer );
}

static void CL_Say_c( genctx_t *ctx, int argnum ) {
    CL_Name_g( ctx );
}

static size_t CL_Mapname_m( char *buffer, size_t size ) {
    return Q_strlcpy( buffer, cl.mapname, size );
}

static size_t CL_Server_m( char *buffer, size_t size ) {
    return Q_strlcpy( buffer, cls.servername, size );
}

static size_t CL_Ups_m( char *buffer, size_t size ) {
    vec3_t vel;
    int ups;
    player_state_t *ps;

    if( cl.frame.clientNum == CLIENTNUM_NONE ) {
        if( size ) {
            *buffer = 0;
        }
        return 0;
    }

    if( !cls.demo.playback && cl.frame.clientNum == cl.clientNum &&
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
    return Q_scnprintf( buffer, size, "%d", ups );
}

static size_t CL_Timer_m( char *buffer, size_t size ) {
    int hour, min, sec;

    sec = cl.time / 1000;
    min = sec / 60; sec %= 60;
    hour = min / 60; min %= 60;

    if( hour ) {
        return Q_scnprintf( buffer, size, "%i:%i:%02i", hour, min, sec );
    }
    return Q_scnprintf( buffer, size, "%i:%02i", min, sec );
}

static size_t CL_Fps_m( char *buffer, size_t size ) {
    return Q_scnprintf( buffer, size, "%i", cls.fps );
}
static size_t CL_Ping_m( char *buffer, size_t size ) {
    return Q_scnprintf( buffer, size, "%i", cls.ping );
}
static size_t CL_Lag_m( char *buffer, size_t size ) {
    return Q_scnprintf( buffer, size, "%.2f%%", cls.netchan ?
        ( (float)cls.netchan->total_dropped /
          cls.netchan->total_received ) * 100.0f : 0 );
}
static size_t CL_Health_m( char *buffer, size_t size ) {
    return Q_scnprintf( buffer, size, "%i", cl.frame.ps.stats[STAT_HEALTH] );
}
static size_t CL_Ammo_m( char *buffer, size_t size ) {
    return Q_scnprintf( buffer, size, "%i", cl.frame.ps.stats[STAT_AMMO] );
}
static size_t CL_Armor_m( char *buffer, size_t size ) {
    return Q_scnprintf( buffer, size, "%i", cl.frame.ps.stats[STAT_ARMOR] );
}

/*
====================
CL_RestartFilesystem
 
Flush caches and restart the VFS.
====================
*/
void CL_RestartFilesystem( void ) {
    int cls_state;

    if( !cl_running->integer ) {
        FS_Restart();
        return;
    }

    // temporary switch to loading state
    cls_state = cls.state;
    if( cls.state >= ca_precached ) {
        cls.state = ca_loading;
    }

#if USE_UI
    UI_Shutdown();
#endif

    S_StopAllSounds();
    S_FreeAllSounds();

    if( cls.ref_initialized ) {
        R_Shutdown( qfalse );

        FS_Restart();

        R_Init( qfalse );

        SCR_RegisterMedia();
        Con_RegisterMedia();
#if USE_UI
        UI_Init();
#endif
    } else {
        FS_Restart();
    }

#if USE_UI
    if( cls_state == ca_disconnected ) {
        UI_OpenMenu( UIMENU_MAIN );
    } else
#endif
    if( cls_state >= ca_loading ) {
        CL_LoadState( LOAD_MAP );
        CL_RegisterModels();
        CL_PrepRefresh();
        CL_LoadState( LOAD_SOUNDS );
        CL_RegisterSounds();
        CL_LoadState( LOAD_FINISH );
    }

    // switch back to original state
    cls.state = cls_state;
}

/*
====================
CL_RestartRefresh
====================
*/
static void CL_RestartRefresh_f( void ) {
    int cls_state;

    if( !cls.ref_initialized ) {
        return;
    }

    // temporary switch to loading state
    cls_state = cls.state;
    if ( cls.state >= ca_precached ) {
        cls.state = ca_loading;
    }

    S_StopAllSounds();
   
    IN_Shutdown();
    CL_ShutdownRefresh();
    CL_InitRefresh();
    IN_Init();

#if USE_UI
    if( cls_state == ca_disconnected ) {
        UI_OpenMenu( UIMENU_MAIN );
    } else
#endif
    if( cls_state >= ca_loading ) {
        CL_LoadState( LOAD_MAP );
        CL_PrepRefresh();
        CL_LoadState( LOAD_FINISH );
    }

    // switch back to original state
    cls.state = cls_state;
}

/*
============
CL_LocalConnect
============
*/
void CL_LocalConnect( void ) {
    if ( FS_NeedRestart() ) {
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

#if USE_FPS
static void cl_updaterate_changed( cvar_t *self ) {
    CL_UpdateRateSetting();
}
#endif

static const cmdreg_t c_client[] = {
    { "cmd", CL_ForwardToServer_f },
    { "pause", CL_Pause_f },
    { "pingservers", CL_PingServers_f },
    { "skins", CL_Skins_f },
    { "userinfo", CL_Userinfo_f },
    { "snd_restart", CL_RestartSound_f },
    { "play", CL_PlaySound_f, CL_PlaySound_c },
    { "changing", CL_Changing_f },
    { "disconnect", CL_Disconnect_f },
    { "connect", CL_Connect_f, CL_Connect_c },
    { "passive", CL_PassiveConnect_f },
    { "reconnect", CL_Reconnect_f },
    { "rcon", CL_Rcon_f, CL_Rcon_c },
    { "precache", CL_Precache_f },
    { "download", CL_Download_f },
    { "serverstatus", CL_ServerStatus_f, CL_ServerStatus_c },
    { "dumpclients", CL_DumpClients_f },
    { "dumpstatusbar", CL_DumpStatusbar_f },
    { "dumplayout", CL_DumpLayout_f },
    { "writeconfig", CL_WriteConfig_f, CL_WriteConfig_c },
//    { "msgtab", CL_Msgtab_f, CL_Msgtab_g },
    { "vid_restart", CL_RestartRefresh_f },

    //
    // forward to server commands
    //
    // the only thing this does is allow command completion
    // to work -- all unknown commands are automatically
    // forwarded to the server
    { "say", NULL, CL_Say_c },
    { "say_team", NULL, CL_Say_c },

    { "wave" }, { "inven" }, { "kill" }, { "use" },
    { "drop" }, { "info" }, { "prog" },
    { "give" }, { "god" }, { "notarget" }, { "noclip" },
    { "invuse" }, { "invprev" }, { "invnext" }, { "invdrop" },
    { "weapnext" }, { "weapprev" },

    { NULL }
};

/*
=================
CL_InitLocal
=================
*/
static void CL_InitLocal ( void ) {
    cvar_t *var;
    int i;

    cls.state = ca_disconnected;

    CL_RegisterInput();
    CL_InitDemos();
    LOC_Init();
    CL_InitAscii();
    CL_InitEffects();

    Cmd_Register( c_client );

    for ( i = 0 ; i < MAX_LOCAL_SERVERS ; i++ ) {
        var = Cvar_Get( va( "adr%i", i ), "", CVAR_ARCHIVE );
        var->generator = Com_Address_g;
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
    cl_noskins->changed = cl_noskins_changed;
    cl_predict = Cvar_Get ( "cl_predict", "1", 0 );
    cl_predict->changed = cl_predict_changed;
    cl_kickangles = Cvar_Get( "cl_kickangles", "1", CVAR_CHEAT );
    cl_maxfps = Cvar_Get( "cl_maxfps", "60", CVAR_ARCHIVE );
    cl_async = Cvar_Get( "cl_async", "1", CVAR_ARCHIVE );
    r_maxfps = Cvar_Get( "r_maxfps", "0", CVAR_ARCHIVE );
    cl_rollhack = Cvar_Get( "cl_rollhack", "1", 0 );

    cl_shownet = Cvar_Get( "cl_shownet", "0", 0 );
    cl_showmiss = Cvar_Get ( "cl_showmiss", "0", 0 );
    cl_showclamp = Cvar_Get ( "showclamp", "0", 0 );
    cl_timeout = Cvar_Get ( "cl_timeout", "120", 0 );

    rcon_address = Cvar_Get ( "rcon_address", "", CVAR_PRIVATE );
    rcon_address->generator = Com_Address_g;

    cl_thirdperson = Cvar_Get( "cl_thirdperson", "0", CVAR_CHEAT );
    cl_thirdperson_angle = Cvar_Get( "cl_thirdperson_angle", "0", 0 );
    cl_thirdperson_range = Cvar_Get( "cl_thirdperson_range", "60", 0 );

    cl_disable_particles = Cvar_Get( "cl_disable_particles", "0", 0 );
    cl_disable_explosions = Cvar_Get( "cl_disable_explosions", "0", 0 );
    cl_gibs = Cvar_Get( "cl_gibs", "1", 0 );
    cl_gibs->changed = cl_gibs_changed;

#if USE_FPS
    cl_updaterate = Cvar_Get( "cl_updaterate", "0", 0 );
    cl_updaterate->changed = cl_updaterate_changed;
#endif

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
    info_uf = Cvar_Get( "uf", va( "%d", UF_LOCALFOV ), CVAR_USERINFO );


    //
    // macros
    //
    Cmd_AddMacro( "cl_mapname", CL_Mapname_m );
    Cmd_AddMacro( "cl_server", CL_Server_m );
    Cmd_AddMacro( "cl_timer", CL_Timer_m );
    Cmd_AddMacro( "cl_ups", CL_Ups_m );
    Cmd_AddMacro( "cl_fps", CL_Fps_m );
    Cmd_AddMacro( "cl_ping", CL_Ping_m );
    Cmd_AddMacro( "cl_lag", CL_Lag_m );
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
    if( cls.state < ca_connected || cls.demo.playback ) {
        return qtrue;
    }

    if( !sv_running->integer ) {
        return qfalse;
    }

    // developer option
    if( Cvar_VariableInteger( "cheats" ) ) {
        return qtrue;
    }

    // single player can cheat
    if( cls.state > ca_connected && cl.maxclients < 2 ) {
        return qtrue;
    }

    return qfalse;
}

//============================================================================

/*
==================
CL_Activate
==================
*/
void CL_Activate( active_t active ) {
    if( cls.active != active ) {
        Com_DPrintf( "%s: %u\n", __func__, active );
        cls.active = active;
        Key_ClearStates();
        IN_Activate();
        S_Activate();
    }
}

/*
==================
CL_SetClientTime
==================
*/
static void CL_SetClientTime( void ) {
    int prevtime;

    if( com_timedemo->integer ) {
        cl.time = cl.servertime;
        cl.lerpfrac = 1.0f;
        return;
    }

    prevtime = cl.servertime - cl.frametime;
    if( cl.time > cl.servertime ) {
        if( cl_showclamp->integer )
            Com_Printf( "high clamp %i\n", cl.time - cl.servertime );
        cl.time = cl.servertime;
        cl.lerpfrac = 1.0f;
    } else if( cl.time < prevtime ) {
        if( cl_showclamp->integer )
            Com_Printf( "low clamp %i\n", prevtime - cl.time );
        cl.time = prevtime;
        cl.lerpfrac = 0;
    } else {
        cl.lerpfrac = ( cl.time - prevtime ) * cl.framefrac;
    }

    if( cl_showclamp->integer == 2 ) {
        Com_Printf( "time %i, lerpfrac %.3f\n",
            cl.time, cl.lerpfrac );
    }
}

static void CL_MeasureStats( void ) {
    cls.measureFramecount++;

    if( com_localTime - cls.measureTime < 1000 ) {
        return;
    }

    if( cls.netchan ) {
        int ack = cls.netchan->incoming_acknowledged;
        int ping = 0;
        int i, j, k = 0;

        i = ack - 16 + 1;
        if( i < cl.initialSeq ) {
            i = cl.initialSeq;
        }
        for( j = i; j <= ack; j++ ) {
            client_history_t *h = &cl.history[j & CMD_MASK];
            if( h->rcvd > h->sent ) {
                ping += h->rcvd - h->sent;
                k++;
            }
        }

        cls.ping = k ? ping / k : 0;
    }

    cls.measureTime = com_localTime;
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
    if( !cl.reply_delta ) {
        return;
    }

    if( cls.realtime - cl.reply_time < cl.reply_delta ) {
        return;
    }

    Cbuf_AddText( "cmd say \"" );
    Cbuf_AddText( com_version->string );
    Cbuf_AddText( "\"\n" );

    cl.reply_delta = 0;
}
#endif

static void CL_CheckTimeout( void ) {
    unsigned delta;

    if( NET_IsLocalAddress( &cls.netchan->remote_address ) ) {
        return;
    }

#if USE_ICMP
    if( cls.errorReceived ) {
        delta = 5000;
        if( com_localTime - cls.netchan->last_received > delta )  {
            Com_Error( ERR_DISCONNECT, "Server connection was reset." );
        }
    }
#endif
 
    delta = cl_timeout->value * 1000;
    if( delta && com_localTime - cls.netchan->last_received > delta )  {
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
void CL_Frame( unsigned msec ) {
    static unsigned ref_extra, phys_extra, main_extra;
    unsigned ref_msec, phys_msec;
    qboolean phys_frame, ref_frame;

    time_after_ref = time_before_ref = 0;

    if( !cl_running->integer ) {
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
            if( cls.active == ACT_MINIMIZED ) {
                // run at 10 fps if minimized
                if( main_extra < 100 ) {
                    NET_Sleep( 100 - main_extra );
                    return;
                }
                ref_frame = qfalse;
            } else if( cls.active == ACT_RESTORED || cls.state < ca_active ) {
                // run at 60 fps if not active
                if( main_extra < 16 ) {
                    NET_Sleep( 16 - main_extra );
                    return;
                }
            }
        }
        if( ref_extra < ref_msec ) {
            if( !cl_async->integer && !cl.sendPacketNow ) {
#if 0
                if( cls.demo.playback || cl.frame.ps.pmove.pm_type == PM_FREEZE ) {
                    NET_Sleep( ref_msec - ref_extra );
                }
#endif
                return; // everything ticks in sync with refresh
            }
            ref_frame = qfalse;
        }
    }

    if ( cls.demo.playback ) { // FIXME: HACK
        if( cl_paused->integer ) {
            if( !sv_paused->integer ) {
                Cvar_Set( "sv_paused", "1" );
                IN_Activate();
            }
        } else {
            if( sv_paused->integer ) {
                Cvar_Set( "sv_paused", "0" );
                IN_Activate();
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
    if( cls.demo.playback ) {
        if( cls.demo.recording && cl_paused->integer == 2 && !cls.demo.paused ) {
            static int demo_extra;
            
            // XXX: record zero frames when manually paused
            // for syncing with audio comments, etc
            demo_extra += main_extra;
            if( demo_extra > 100 ) {
                CL_EmitZeroFrame();
                demo_extra = 0;
            }
        }

        CL_DemoFrame();
    }

    // calculate local time
    if( cls.state == ca_active && !sv_paused->integer ) {
        CL_SetClientTime();
    }

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

    Con_RunConsole();

    if( ref_frame ) {
        // update the screen
        if ( host_speeds->integer )
            time_before_ref = Sys_Milliseconds();

        SCR_UpdateScreen();

        if ( host_speeds->integer )
            time_after_ref = Sys_Milliseconds();

        ref_extra = 0;
        //
    // update audio after the 3D view was drawn
    S_Update();

    }

    // advance local effects for next frame
    CL_RunDLights();
    CL_RunLightStyles();

    // check connection timeout
    if( cls.netchan ) {
        CL_CheckTimeout();
    }

    CL_MeasureStats();

    cls.framecount++;

    main_extra = 0;
}

/*
============
CL_ProcessEvents
============
*/
void CL_ProcessEvents( void ) {
    if( !cl_running->integer ) {
        return;
    }

    CL_RunRefresh();

    IN_Frame();

    memset( &net_from, 0, sizeof( net_from ) );
    net_from.type = NA_LOOPBACK;

    // process loopback packets
    while( NET_GetLoopPacket( NS_CLIENT ) ) {
        CL_PacketEvent();
    }

    // process network packets
    while( NET_GetPacket( NS_CLIENT ) ) {
        CL_PacketEvent();
    }
}

//============================================================================

/*
====================
CL_Init
====================
*/
void CL_Init( void ) {
    if( dedicated->integer ) {
        return; // nothing running on the client
    }

    if( cl_running->integer ) {
        return;
    }

    // all archived variables will now be loaded

    // start with full screen console
    cls.key_dest = KEY_CONSOLE;

#ifdef _WIN32
    CL_InitRefresh();
    S_Init();   // sound must be initialized after window is created
#else
    S_Init();
    CL_InitRefresh();
#endif

    CL_InitLocal();
    IN_Init();

#if USE_ZLIB
    if( inflateInit2( &cls.z, -15 ) != Z_OK ) {
        Com_Error( ERR_FATAL, "inflateInit2() failed" );
    }
#endif

#if USE_UI
    UI_OpenMenu( UIMENU_MAIN );
#endif

    Con_PostInit();
    Con_RunConsole();

    Cvar_Set( "cl_running", "1" );
}

/*
===============
CL_WriteConfig

Writes key bindings and archived cvars to config.cfg
===============
*/
static void CL_WriteConfig( void ) {
    fileHandle_t f;

    FS_FOpenFile( COM_CONFIG_NAME, &f, FS_MODE_WRITE );
    if( !f ) {
        return;
    }

    FS_FPrintf( f, "// generated by q2pro, do not modify\n" );

    Key_WriteBindings( f );
    Cvar_WriteVariables( f, CVAR_ARCHIVE, qfalse );
    
    FS_FCloseFile( f );
}


/*
===============
CL_Shutdown
 
FIXME: this is a callback from Com_Quit and Com_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void CL_Shutdown( void ) {
    static qboolean isdown = qfalse;
    
    if( isdown ) {
        Com_Printf( "CL_Shutdown: recursive shutdown\n" );
        return;
    }
    isdown = qtrue;

    if( !cl_running || !cl_running->integer ) {
        return;
    }

    CL_Disconnect( ERR_SILENT, NULL );

#if USE_ZLIB
    inflateEnd( &cls.z );
#endif

    S_Shutdown();
    IN_Shutdown();
    Con_Shutdown();
    CL_ShutdownRefresh();
    CL_WriteConfig();

    Cvar_Set( "cl_running", "0" );

    isdown = qfalse;
}

