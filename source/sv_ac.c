/*
Copyright (C) 2007 Andrey Nazarov
Copyright (C) 2006 r1ch.net

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
// r1ch.net anticheat server interface for Quake II
//

#include "sv_local.h"

typedef enum {
	ACS_BAD,
	ACS_CLIENTACK,
	ACS_VIOLATION,
	ACS_NOACCESS,
	ACS_FILE_VIOLATION,
	ACS_READY,
	ACS_QUERYREPLY,
	ACS_PONG,
	ACS_UPDATE_REQUIRED,
	ACS_DISCONNECT,
	ACS_ERROR,
} ac_serverbyte_t;

typedef enum {
	ACC_BAD,
	ACC_VERSION,
	ACC_PREF,
	ACC_REQUESTCHALLENGE,
	ACC_CLIENTDISCONNECT,
	ACC_QUERYCLIENT,
	ACC_PING,
	ACC_UPDATECHECKS,
	ACC_SETPREFERENCES,
} ac_clientbyte_t;

typedef enum {
	OP_INVALID,
	OP_EQUAL,
	OP_NEQUAL,
	OP_GTEQUAL,
	OP_LTEQUAL,
	OP_LT,
	OP_GT,
	OP_STREQUAL,
	OP_STRNEQUAL,
	OP_STRSTR,
} ac_opcode_t;

typedef enum {
    AC_CLIENT_R1Q2  = 0x01,
    AC_CLIENT_EGL   = 0x02,
    AC_CLIENT_APRGL = 0x04,
    AC_CLIENT_APRSW = 0x08,
    AC_CLIENT_Q2PRO = 0x10
} ac_client_t;

typedef struct ac_file_s {
    struct ac_file_s *next;
    byte hash[20];
    int flags;
    char path[1];
} ac_file_t;

typedef struct ac_cvar_s {
    struct ac_cvar_s *next;
    int num_values;
    char **values;
    ac_opcode_t op;
    char *def;
    char name[1];
} ac_cvar_t;

typedef struct {
    qboolean connected;
    qboolean ready;
    qboolean ping_pending;
    unsigned last_ping;
    netstream_t  stream;
    int msglen;

    ac_file_t *files;
    int num_files;

    ac_cvar_t *cvars;
    int num_cvars;
} ac_locals_t;


#define ACP_BLOCKPLAY	( 1 << 0 )

#define	ACH_REQUIRED	( 1 << 0 )
#define	ACH_NEGATIVE	( 1 << 1 )

#define AC_PROTOCOL_VERSION	0xAC03

#define AC_DEFAULT_BACKOFF  30

#define AC_PING_INTERVAL    60000
#define AC_PING_TIMEOUT     15000

#define AC_MESSAGE  "\220\xe1\xee\xf4\xe9\xe3\xe8\xe5\xe1\xf4\221 "

#define AC_SEND_SIZE    131072
#define AC_RECV_SIZE    1024

static ac_locals_t  ac;

static list_t       ac_required_list;
static list_t       ac_exempt_list;

static time_t       ac_retry_time;
static int          ac_retry_backoff;

static byte         ac_send_buffer[AC_SEND_SIZE];
static byte         ac_recv_buffer[AC_RECV_SIZE];

static cvar_t   *ac_required;
static cvar_t   *ac_server_address;
static cvar_t   *ac_error_action;
static cvar_t   *ac_message;
static cvar_t   *ac_badfile_action;
static cvar_t   *ac_badfile_message;
static cvar_t   *ac_badfile_max;
static cvar_t   *ac_show_violation_reason;
static cvar_t   *ac_client_disconnect_action;
static cvar_t   *ac_disable_play;

static const char ac_clients[][8] = {
    "???",
    "R1Q2",
    "EGL",
    "Apr GL",
    "Apr SW",
    "Q2PRO",
};

static const int ac_num_clients = sizeof( ac_clients ) / sizeof( ac_clients[0] );

/*
==============================================================================

REPLY PARSING

==============================================================================
*/

static void AC_Drop( void ) {
    time_t clock;
    client_t *cl;

    NET_Close( &ac.stream );

    if( !ac.connected ) {
		Com_Printf( "ANTICHEAT: Server connection failed. "
            "Retrying in %d seconds.\n", ac_retry_backoff );
        clock = time( NULL );
        ac_retry_time = clock + ac_retry_backoff;
        ac_retry_backoff += 5;
        return;
    }

    FOR_EACH_CLIENT( cl ) {
        cl->ac_valid = qfalse;
        cl->ac_file_failures = 0;
    }

	// inform
	if( ac.ready ) {
		SV_BroadcastPrintf( PRINT_HIGH, AC_MESSAGE
            "This server has lost the connection to the anticheat server. "
            "Any anticheat clients are no longer valid.\n" );

		if( ac_required->integer == 2 ) {
			SV_BroadcastPrintf( PRINT_HIGH, AC_MESSAGE 
                "You will need to reconnect once the server has "
                "re-established the anticheat connection.\n" );
        }
        ac_retry_backoff = AC_DEFAULT_BACKOFF;
	} else {
        ac_retry_backoff += 30; // this generally indicates a server problem
    }

	Com_WPrintf(
        "ANTICHEAT: Lost connection to anticheat server! "
        "Will attempt to reconnect in %d seconds.\n", ac_retry_backoff );

    clock = time( NULL );
    ac_retry_time = clock + ac_retry_backoff;

    memset( &ac, 0, sizeof( ac ) );
}


static void AC_Announce( client_t *client, const char *fmt, ... ) {
	va_list		argptr;
	char		string[MAX_STRING_CHARS];
    int         length;
	
	va_start( argptr, fmt );
	length = Q_vsnprintf( string, sizeof( string ), fmt, argptr );
	va_end( argptr );

	MSG_WriteByte( svc_print );
	MSG_WriteByte( PRINT_HIGH );
	MSG_WriteData( string, length + 1 );

	Com_Printf( "%s", string );

    if( client->state == cs_spawned ) {
        FOR_EACH_CLIENT( client ) {
            if( client->state == cs_spawned ) {
                SV_ClientAddMessage( client, MSG_RELIABLE );
            }
        }
    } else {
        SV_ClientAddMessage( client, MSG_RELIABLE );
    }

	SZ_Clear( &msg_write );
}

static client_t *AC_ParseClient( void ) {
    client_t *cl;
	uint16 clientID = MSG_ReadShort();

	// we check challenge to ensure we don't get
    // a race condition if a client reconnects.
	uint32 challenge = MSG_ReadLong();

	if( clientID >= sv_maxclients->integer ) {
		Com_WPrintf( "ANTICHEAT: Illegal client ID: %u\n", clientID );
		return NULL;
	}

	cl = &svs.clientpool[clientID];

	if( cl->challenge != challenge ) {
		return NULL;
    }

	if( cl->state < cs_connected ) {
        return NULL;
    }

    return cl;
}

static void AC_ParseViolation( void ) {
	client_t		*cl;
    char		*reason, *clientreason;

    cl = AC_ParseClient();
    if( !cl ) {
        return;
    }

	reason = MSG_ReadString();

	if( msg_read.readcount < msg_read.cursize ) {
		clientreason = MSG_ReadString();
    } else {
		clientreason = NULL;
    }

    // FIXME: should we notify other players about anticheat violations 
    // found before clientbegin? one side says yes to expose cheaters, 
    // other side says no since client will have no previous message to 
    // show that they're trying to join. currently showing messages only 
    // for spawned clients.

    // fixme maybe
    if( strcmp( reason, "disconnected" ) ) {
        char	showreason[32];

        if( ac_show_violation_reason->integer )
            Com_sprintf( showreason, sizeof( showreason ), " (%s)", reason );
        else
            showreason[0] = 0;

        AC_Announce( cl, "%s was kicked for anticheat violation%s\n",
            cl->name, showreason );

        Com_Printf( "ANTICHEAT VIOLATION: %s[%s] was kicked: %s\n",
            cl->name, NET_AdrToString( &cl->netchan->remote_address ), reason );

        if( clientreason )
            SV_ClientPrintf( cl, PRINT_HIGH, "%s\n", clientreason );

        // hack to fix late zombies race condition
        cl->lastmessage = svs.realtime;
        SV_DropClient( cl, NULL );
        return;
    }

    if( !cl->ac_valid ) {
        return;
    }

    Com_Printf( "ANTICHEAT DISCONNECT: %s[%s] disconnected from "
        "anticheat server\n", cl->name,
        NET_AdrToString( &cl->netchan->remote_address ) );

    if( ac_client_disconnect_action->integer == 1 ) {
        AC_Announce( cl, "%s lost connection to anticheat server.\n", cl->name );
        SV_DropClient( cl, NULL );
        return;
    }

    AC_Announce( cl, "%s lost connection to anticheat server, "
        "client is no longer valid.\n", cl->name );
    cl->ac_valid = qfalse;
}

static void AC_ParseClientAck( void ) {
	client_t		*cl;

    cl = AC_ParseClient();
    if( !cl ) {
        return;
    }

	if( cl->state != cs_connected && cl->state != cs_primed ) {
		Com_DPrintf( "ANTICHEAT: %s with client in state %d\n",
            __func__, cl->state );
		return;
	}

	cl->ac_client_type = MSG_ReadByte();
	cl->ac_valid = qtrue;
}

static void AC_ParseFileViolation( void ) {
	//linkednamelist_t	*bad;
	client_t	*cl;
	char		*path, *hash;
    int			action;
    ac_file_t	*f;

    cl = AC_ParseClient();
    if( !cl ) {
        return;
    }

	path = MSG_ReadString();
	if( msg_read.readcount < msg_read.cursize )
		hash = MSG_ReadString();
	else
		hash = "no hash?";

    cl->ac_file_failures++;

    action = ac_badfile_action->integer;
    for( f = ac.files; f; f = f->next ) {
        if( !strcmp( f->path, path ) ) {
            if( f->flags & ACH_REQUIRED ) {
                action = 1;
                break;
            }
        }
    }

    Com_Printf( "ANTICHEAT FILE VIOLATION: %s[%s] has a modified %s [%s]\n",
        cl->name, NET_AdrToString( &cl->netchan->remote_address ), path, hash );
    switch( action ) {
    case 0:
        AC_Announce( cl, "%s was kicked for modified %s\n", cl->name, path );
        break;
    case 1:
        SV_ClientPrintf( cl, PRINT_HIGH,
            "WARNING: Your file %s has been modified. "
            "Please replace it with a known valid copy.\n", path );
        break;
    case 2:
        // spamalicious :)
        AC_Announce( cl, "%s has a modified %s\n", cl->name, path );
        break;
    }

    // show custom msg
    if( ac_badfile_message->string[0] ) {
        SV_ClientPrintf( cl, PRINT_HIGH, "%s\n", ac_badfile_message->string );
    }

    if( !action ) {
        SV_DropClient( cl, NULL );
        return;
    }

    if( ac_badfile_max->integer > 0 && cl->ac_file_failures > ac_badfile_max->integer ) {
        AC_Announce( cl, "%s was kicked for too many modified files\n", cl->name );
        SV_DropClient( cl, NULL );
        return;
    }

    /*
    bad = &cl->anticheat_bad_files;
    while (bad->next)
        bad = bad->next;

    bad->next = Z_TagMalloc (sizeof(*bad), TAGMALLOC_ANTICHEAT);
    bad = bad->next;
    bad->name = CopyString (quakePath, TAGMALLOC_ANTICHEAT);
    bad->next = NULL;
    */
}

static void AC_ParseReady( void ) {
	ac.ready = qtrue;
    ac.last_ping = svs.realtime;
	ac_retry_backoff = AC_DEFAULT_BACKOFF;
	Com_Printf( "ANTICHEAT: Ready to serve anticheat clients.\n" );
	Cvar_FullSet( "anticheat", ac_required->string,
        CVAR_SERVERINFO | CVAR_NOSET, CVAR_SET_DIRECT );
}

static void AC_ParseQueryReply( void ) {
	client_t		*cl;
	int				type, valid;

    cl = AC_ParseClient();
    if( !cl ) {
        return;
    }

	type = MSG_ReadByte();
    MSG_ReadByte();
    MSG_ReadByte();
    valid = MSG_ReadByte();

	cl->ac_query_sent = AC_QUERY_DONE;
	if( valid == 1 ) {
		cl->ac_client_type = type;
		cl->ac_valid = qtrue;
	}

	if( cl->state < cs_connected || cl->state > cs_primed ) {
		Com_DPrintf( "ANTICHEAT: %s with client in state %d\n",
            __func__, cl->state );
		SV_DropClient( cl, NULL );
		return;
	}

    sv_client = cl;
    sv_player = cl->edict;
	SV_Begin_f();
    sv_client = NULL;
    sv_player = NULL;
}

// this is different from the violation "disconnected" as this message is 
// only sent if the client manually disconnected and exists to prevent the
// race condition of the server seeing the disconnect violation before the
// udp message and thus showing "%s lost connection" right before the
// player leaves the server
static void AC_ParseDisconnect ( void ) {
	client_t		*cl;

    cl = AC_ParseClient();
    if( cl ) {
		Com_Printf( "ANTICHEAT: Dropping %s, disconnect message.\n", cl->name );
		SV_DropClient( cl, NULL );
	}
}

static void AC_ParseError( void ) {
    //Com_EPrintf( "ANTICHEAT: %s\n", AC_ReadString() );
    AC_Disconnect();
}

static void AC_ParseMessage( void ) {
    uint16 msglen;
    byte *data;
    int length;
    int cmd;

    // parse msglen
    if( !ac.msglen ) {
        if( !FIFO_TryRead( &ac.stream.recv, &msglen, 2 ) ) {
            return;
        }
        if( !msglen ) {
            return;
        }
        msglen = LittleShort( msglen );
        if( msglen > AC_RECV_SIZE ) {
            Com_EPrintf( "ANTICHEAT: Oversize message: %d bytes\n", msglen );
            AC_Drop();
            return;
        }
        ac.msglen = msglen;
    }

    // first, try to read in a single block
    data = FIFO_Peek( &ac.stream.recv, &length );
    if( length < ac.msglen ) {
        if( !FIFO_TryRead( &ac.stream.recv, msg_read_buffer, ac.msglen ) ) {
            return; // not yet available
        }
        SZ_Init( &msg_read, msg_read_buffer, sizeof( msg_read_buffer ) );
    } else {
        SZ_Init( &msg_read, data, ac.msglen );
        FIFO_Decommit( &ac.stream.recv, ac.msglen );
    }

    msg_read.cursize = ac.msglen;
    ac.msglen = 0;

    cmd = MSG_ReadByte();
	switch( cmd ) {
    case ACS_VIOLATION:
        AC_ParseViolation();
        break;
    case ACS_CLIENTACK:
        AC_ParseClientAck();
        break;
    case ACS_FILE_VIOLATION:
        AC_ParseFileViolation();
        break;
    case ACS_READY:
        AC_ParseReady();
        break;
    case ACS_QUERYREPLY:
        AC_ParseQueryReply();
        break;
    case ACS_ERROR:
        AC_ParseError();
        break;
    case ACS_NOACCESS:
        Com_WPrintf( "ANTICHEAT: You do not have permission to "
            "use the anticheat server. Anticheat disabled.\n" );
        AC_Disconnect();
        break;
    case ACS_UPDATE_REQUIRED:
        Com_WPrintf( "ANTICHEAT: The anticheat server is no longer "
            "compatible with this version of " APPLICATION ". "
            "Please make sure you are using the latest " APPLICATION " version. "
            "Anticheat disabled.\n" );
        AC_Disconnect();
        break;
    case ACS_DISCONNECT:
        AC_ParseDisconnect();
        break;
    case ACS_PONG:
        ac.ping_pending = qfalse;
        ac.last_ping = svs.realtime;
        break;
    default:
        Com_EPrintf( "ANTICHEAT: Unknown command byte %d, please make "
            "sure you are using the latest " APPLICATION " version. "
            "Anticheat disabled.\n", cmd );
        AC_Disconnect();
        break;
	}

}

/*
==============================================================================

IN-GAME QUERIES

==============================================================================
*/

static void AC_Write( const char *func ) {
    byte *src = msg_write.data;
    int len = msg_write.cursize;

    SZ_Clear( &msg_write );

    if( !FIFO_TryWrite( &ac.stream.send, src, len ) ) {
        Com_WPrintf( "ANTICHEAT: Send buffer exceeded in %s\n", func );
    }
}

static void AC_ClientQuery( client_t *cl ) {
	int num = cl - svs.clientpool;

	cl->ac_query_sent = AC_QUERY_SENT;
	cl->ac_query_time = svs.realtime;

	if( !ac.ready )
		return;

	//if( ac_nag_time->integer )
	//	cl->anticheat_nag_time = svs.realtime;

    MSG_WriteShort( 9 );
	MSG_WriteByte( ACC_QUERYCLIENT );
	MSG_WriteLong( num );
    MSG_WriteLong( cl->challenge );
    AC_Write( __func__ );
}

qboolean AC_ClientBegin( client_t *cl ) {
	if( cl->ac_required == AC_EXEMPT ) {
        return qtrue; // client is EXEMPT
    }

    if( cl->ac_valid ) {
        return qtrue; // client is VALID
    }

    if( cl->ac_query_sent == AC_QUERY_UNSENT && ac.ready ) {
        AC_ClientQuery( cl );
        return qfalse; // not yet QUERIED
    }

	if( cl->ac_required != AC_REQUIRED ) {
        return qtrue; // anticheat is NOT REQUIRED
    }

    if( ac.ready ) {
        // anticheat connection is UP, client is STILL INVALID
        // AFTER QUERY, anticheat is REQUIRED
        Com_Printf( "ANTICHEAT: Rejected connecting client %s[%s], "
            "no anticheat response.\n", cl->name,
            NET_AdrToString( &cl->netchan->remote_address ) );
        SV_ClientPrintf( cl, PRINT_HIGH, "%s\n", ac_message->string );
        SV_DropClient( cl, NULL );
        return qfalse;
    }

    if( ac_error_action->integer == 0 ) {
        return qtrue; // error action is ALLOW
    }

    // anticheat server connection is DOWN, client is INVALID,
    // anticheat is REQUIRED, error action is DENY
    Com_Printf( "ANTICHEAT: Rejected connecting client %s[%s], "
        "no connection to anticheat server.\n", cl->name,
        NET_AdrToString( &cl->netchan->remote_address ) );
    SV_ClientPrintf( cl, PRINT_HIGH,
        "This server is unable to take new connections right now. "
        "Please try again later.\n" );
    SV_DropClient( cl, NULL );
    return qfalse;
}

void AC_ClientAnnounce( client_t *cl ) {
	if( !ac_required->integer ) {
        return;
    }
	if( cl->state <= cs_zombie ) {
        return;
    }
    if( cl->ac_required == AC_EXEMPT ) {
        SV_BroadcastPrintf( PRINT_MEDIUM, AC_MESSAGE
            "%s is exempt from using anticheat.\n", cl->name );
    } else if( cl->ac_valid ) {
        if( cl->ac_file_failures ) {
            SV_BroadcastPrintf( PRINT_MEDIUM, AC_MESSAGE
                "%s failed %d file check%s.\n", 
                cl->name, cl->ac_file_failures,
                cl->ac_file_failures == 1 ? "" : "s" );
        }
    } else {
        SV_BroadcastPrintf( PRINT_MEDIUM, AC_MESSAGE
            "%s is not using anticheat.\n", cl->name );
    }
}

char *AC_ClientConnect( client_t *cl ) {
	int num = cl - svs.clientpool;

    if( !ac_required->integer ) {
        return "";
    }

    if( SV_MatchAddress( &ac_exempt_list, &net_from ) ) {
        cl->ac_required = AC_EXEMPT;
        return "";
    }

    if( ac_required->integer == 2 ) {
        // anticheat is required for everyone
        cl->ac_required = AC_REQUIRED;
    } else {
        cl->ac_required = AC_NORMAL;
        if( SV_MatchAddress( &ac_required_list, &net_from ) ) {
            cl->ac_required = AC_REQUIRED;
        }
    }

    if( ac.ready ) {
        MSG_WriteShort( 15 );
        MSG_WriteByte( ACC_REQUESTCHALLENGE );
        MSG_WriteData( net_from.ip, 4 );
        MSG_WriteData( &net_from.port, 2 );
        MSG_WriteLong( num );
        MSG_WriteLong( cl->challenge );
        AC_Write( __func__ );
    }

    return " ac=1";
}

void AC_ClientDisconnect( client_t *cl ) {
	int num = cl - svs.clientpool;

	cl->ac_query_sent = AC_QUERY_UNSENT;
	cl->ac_valid = qfalse;

	if( !ac.ready )
		return;

    MSG_WriteShort( 9 );
	MSG_WriteByte( ACC_CLIENTDISCONNECT );
	MSG_WriteLong( num );
    MSG_WriteLong( cl->challenge );
    AC_Write( __func__ );
}

/*
==============================================================================

STARTUP STUFF

==============================================================================
*/

static qboolean AC_Flush( void ) {
    byte *src = msg_write.data;
    int ret, len = msg_write.cursize;

    SZ_Clear( &msg_write );

    if( !ac.connected ) {
        return qfalse;
    }

    while( 1 ) {
        ret = FIFO_Write( &ac.stream.send, src, len );
        if( ret == len ) {
            break;
        }

        len -= ret;
        src += ret;

        Com_WPrintf( "ANTICHEAT: Send buffer length exceeded, "
            "server may be frozen for a short while!\n" );
        do {
            Sys_RunConsole();
            Sys_Sleep( 1 );
            AC_Run();
            if( !ac.connected ) {
                return qfalse;
            }
        } while( FIFO_Usage( &ac.stream.send ) > AC_SEND_SIZE / 2 );
    }

    return qtrue;
}

static void AC_WriteString( const char *s ) {
    int len = strlen( s );

    if( len > 255 ) {
        len = 255;
    }

    MSG_WriteByte( len );
    MSG_WriteData( s, len );
}

static void AC_SendChecks( void ) {
    ac_file_t *f, *p;
    ac_cvar_t *c;
    int i;

    MSG_WriteShort( 9 );
    MSG_WriteByte( ACC_UPDATECHECKS );
    MSG_WriteLong( ac.num_files );
    MSG_WriteLong( ac.num_cvars );
    AC_Flush();

    for( f = ac.files, p = NULL; f; p = f, f = f->next ) {
        MSG_WriteData( f->hash, sizeof( f->hash ) );
        MSG_WriteByte( f->flags );
        if( p && !strcmp( f->path, p->path ) ) {
            MSG_WriteByte( 0 );
        } else {
            AC_WriteString( f->path );
        }
        AC_Flush();
    }

    for( c = ac.cvars; c; c = c->next ) {
        AC_WriteString( c->name );
        MSG_WriteByte( c->op );
        MSG_WriteByte( c->num_values );
        for( i = 0; i < c->num_values; i++ ) {
            AC_WriteString( c->values[i] );
        }
        AC_WriteString( c->def );
        AC_Flush();
    }
}

static void AC_SendPrefs( void ) {
    int prefs = 0;

    if( ac_disable_play->integer ) {
        prefs |= ACP_BLOCKPLAY;
    }

    MSG_WriteShort( 5 );
    MSG_WriteByte( ACC_SETPREFERENCES );
    MSG_WriteLong( prefs );
    AC_Flush();
}

static void AC_SendPing( void ) {
    ac.last_ping = svs.realtime;
    ac.ping_pending = qtrue;

    MSG_WriteShort( 1 );
    MSG_WriteByte( ACC_PING );
    AC_Flush();
}

static void AC_SendHello( void ) {
    int hostlen = strlen( sv_hostname->string );
    int verlen = strlen( com_version->string );

    MSG_WriteByte( 0x02 );
    MSG_WriteShort( 22 + hostlen + verlen ); // why 22 instead of 9?
    MSG_WriteByte( ACC_VERSION );
    MSG_WriteShort( AC_PROTOCOL_VERSION );
    MSG_WriteShort( hostlen );
    MSG_WriteData( sv_hostname->string, hostlen );
    MSG_WriteShort( verlen );
    MSG_WriteData( com_version->string, verlen );
    MSG_WriteLong( net_port->integer );
    AC_Flush();

    AC_SendChecks();
    AC_SendPrefs();
    AC_SendPing();
}

static void AC_CheckTimeouts( void ) {
    client_t *cl;

    if( ac.ping_pending ) {
        if( svs.realtime - ac.last_ping > AC_PING_TIMEOUT ) {
			Com_Printf( "ANTICHEAT: Server ping timeout, disconnecting.\n" );
            AC_Drop();
            return;
        }
    }
    if( ac.ready ) {
        if( svs.realtime - ac.last_ping > AC_PING_INTERVAL ) {
            AC_SendPing();
        }
    }

    FOR_EACH_CLIENT( cl ) {
		if( cl->state != cs_primed ) {
            continue;
        }
        if( cl->ac_query_sent != AC_QUERY_SENT ) {
            continue;
        }
        if( cl->ac_query_time > svs.realtime ) {
            cl->ac_query_time = svs.realtime;
        }
        if( svs.realtime - cl->ac_query_time > 5000 ) {
            Com_WPrintf( "ANTICHEAT: Query timed out for %s, possible network problem.\n", cl->name );
            cl->ac_valid = qfalse;
            sv_client = cl;
            sv_player = cl->edict;
            SV_Begin_f();
            sv_client = NULL;
            sv_player = NULL;
		}
    }
}

static qboolean AC_Reconnect( void ) {
    netadr_t address;
    time_t clock;

    if( !NET_StringToAdr( ac_server_address->string, &address ) ) {
        Com_WPrintf( "ANTICHEAT: Unable to lookup %s.\n",
            ac_server_address->string );
        goto fail;
    }

    if( !address.port ) {
        address.port = BigShort( PORT_SERVER );
    }

    if( NET_Connect( &address, &ac.stream ) == NET_ERROR ) {
        Com_EPrintf( "ANTICHEAT: %s to %s.\n",
            NET_ErrorString(), ac_server_address->string );
        goto fail;
    }

    ac.stream.send.data = ac_send_buffer;
    ac.stream.send.size = AC_SEND_SIZE;
    ac.stream.recv.data = ac_recv_buffer;
    ac.stream.recv.size = AC_RECV_SIZE;
    ac_retry_time = 0;
    return qtrue;

fail:
    ac_retry_backoff += 60;
    Com_Printf( "ANTICHEAT: Retrying in %d seconds.\n", ac_retry_backoff );
    clock = time( NULL );
    ac_retry_time = clock + ac_retry_backoff;
    return qfalse;
}


void AC_Run( void ) {
    neterr_t ret;
    time_t clock;

    if( ac_retry_time ) {
        clock = time( NULL );
        if( ac_retry_time < clock ) {
		    Com_Printf( "ANTICHEAT: Attempting to reconnect to anticheat server...\n" );
            AC_Reconnect();
        }
        return;
    }

    if( !ac.stream.state ) {
        return;
    }

    ret = NET_Run( &ac.stream );
    switch( ret ) {
    case NET_AGAIN:
        if( ac.connected ) {
            AC_CheckTimeouts();
        }
        break;
    case NET_ERROR:
        Com_EPrintf( "ANTICHEAT: %s to %s.\n", NET_ErrorString(),
            NET_AdrToString( &ac.stream.address ) );
    case NET_CLOSED:
        AC_Drop();
        break;
    case NET_OK:
        if( !ac.connected ) {
            Com_Printf( "ANTICHEAT: Connected to anticheat server!\n" );
            ac.connected = qtrue;
            AC_SendHello();
        }
        AC_ParseMessage();
        AC_CheckTimeouts();
        break;
    }
}

void AC_Connect( void ) {
    int attempts;

    if( !ac_required->integer ) {
        return;
    }

#ifndef DEDICATED_ONLY
    if( !dedicated->integer ) {
		Com_Printf( "ANTICHEAT: Only supported on dedicated servers, disabling.\n" );
        Cvar_SetByVar( ac_required, "0", CVAR_SET_DIRECT );
        return;
    }
#endif

	Com_Printf( "ANTICHEAT: Attempting to connect to %s...\n", ac_server_address->string );
    Sys_RunConsole();

    ac_retry_backoff = AC_DEFAULT_BACKOFF;
    if( !AC_Reconnect() ) {
        return;
    }

    // syncronize startup
    for( attempts = 0; attempts < 5000; attempts++ ) {
        Sys_RunConsole();
        Sys_Sleep( 1 );
        AC_Run();
        if( ac.ready || !ac.stream.state ) {
            break;
        }
    }
}

void AC_Disconnect( void ) {
    NET_Close( &ac.stream );

    ac_retry_time = 0;
    ac_retry_backoff = AC_DEFAULT_BACKOFF;

    memset( &ac, 0, sizeof( ac ) );
    Cvar_SetByVar( ac_required, "0", CVAR_SET_DIRECT );
    Cvar_FullSet( "anticheat", "0", CVAR_NOSET, CVAR_SET_DIRECT );
}

void AC_List_f( void ) {
	client_t	*cl;
	char	*sub;
    int i;

	if( !ac_required->integer ) {
		Com_Printf( "The anticheat module is not in use on this server.\n"
            "For information on anticheat, please visit http://antiche.at/\n" );
		return;
	}

	sub = Cmd_Argv( 1 );

	Com_Printf(
		"+----------------+--------+-----+------+\n"
		"|  Player Name   |AC Valid|Files|Client|\n"
		"+----------------+--------+-----+------+\n" );

    FOR_EACH_CLIENT( cl ) {
		if( cl->state < cs_spawned ) {
			continue;
        }

		if( *sub && !strstr( cl->name, sub ) ) {
            continue;
        }

        if( cl->ac_required == AC_EXEMPT ) {
            Com_Printf( "|%-16s| exempt | N/A | N/A  |\n", cl->name );
        } else if( cl->ac_valid ) {
            i = cl->ac_client_type;
            if( i < 0 || i >= ac_num_clients ) {
                i = 0;
            }
            Com_Printf( "|%-16s|   yes  | %3d |%-6s|\n", cl->name,
                cl->ac_file_failures, ac_clients[i] );
        } else {
            Com_Printf( "|%-16s|   NO   | N/A | N/A  |\n", cl->name );
        }
	}

	Com_Printf( "+----------------+--------+-----+------+\n" );

    Com_Printf(
        "This Quake II server is %sconnected to the anticheat server.\n"
        "For information on anticheat, please visit http://antiche.at/\n",
        ac.ready ? "" : "NOT " );
}

void AC_Info_f( void ) {
	//client_t			*cl;
	//linkednamelist_t	*bad;

	if( !ac_required->integer ) {
		Com_Printf( "The anticheat module is not in use on this server.\n"
            "For information on anticheat, please visit http://antiche.at/\n" );
		return;
	}

	if( Cmd_Argc() == 1 ) {
		Com_Printf( "Usage: %s [substring|id]\n", Cmd_Argv( 0 ) );
		return;
	}

    //SV_SetPlayer();

    /*
	bad = &cl->anticheat_bad_files;

	Com_Printf ("File check failures for %s:\n", LOG_GENERAL, cl->name);
	while (bad->next)
	{
		bad = bad->next;
		if (!filesubstring[0] || strstr (bad->name, filesubstring))
			Com_Printf ("%s\n", LOG_GENERAL, bad->name);
	}*/
}

static void AC_Invalidate_f( void ) {
	client_t	*cl;

	if( !ac.ready ) {
		Com_Printf( "Anticheat is not ready.\n" );
		return;
	}
    
    FOR_EACH_CLIENT( cl ) {
		if( cl->state > cs_connected ) {
			AC_ClientDisconnect( cl );
        }
	}

	Com_Printf( "All clients marked as invalid.\n" );
}

static void AC_Update_f( void ) {
	if( !ac.ready ) {
		Com_Printf( "Anticheat is not ready.\n" );
		return;
	}

	AC_SendChecks();
	Com_Printf( "Anticheat configuration updated.\n" );
}

static void AC_AddException_f( void ) {
    SV_AddMatch_f( &ac_exempt_list );
}
static void AC_DelException_f( void ) {
    SV_DelMatch_f( &ac_exempt_list );
}
static void AC_ListExceptions_f( void ) {
    SV_ListMatches_f( &ac_exempt_list );
}

static void AC_AddRequirement_f( void ) {
    SV_AddMatch_f( &ac_required_list );
}
static void AC_DelRequirement_f( void ) {
    SV_DelMatch_f( &ac_required_list );
}
static void AC_ListRequirements_f( void ) {
    SV_ListMatches_f( &ac_required_list );
}

static const cmdreg_t c_ac[] = {
    { "svaclist", AC_List_f },
    { "svacinfo", AC_Info_f },
    { "svacupdate", AC_Update_f },
    { "svacinvalidate", AC_Invalidate_f },

    { "addacexception", AC_AddException_f },
    { "delacexception", AC_DelException_f },
    { "listacexceptions", AC_ListExceptions_f },

    { "addacrequirement", AC_AddRequirement_f },
    { "delacrequirement", AC_DelRequirement_f },
    { "listacrequirements", AC_ListRequirements_f },

    { NULL }
};

static void ac_disable_play_changed( cvar_t *self ) {
    if( ac.connected ) {
        AC_SendPrefs();
    }
}

void AC_Register( void ) {
    ac_required = Cvar_Get( "sv_anticheat_required", "0", CVAR_LATCH );
    ac_server_address = Cvar_Get( "sv_anticheat_server_address", "anticheat.r1ch.net", CVAR_LATCH );
    ac_error_action = Cvar_Get( "sv_anticheat_error_action", "0", 0 );
    ac_message = Cvar_Get( "sv_anticheat_message",
        "This server requires the r1ch.net anticheat module. "
        "Please see http://antiche.at/ for more details.", 0 );
    ac_badfile_action = Cvar_Get( "sv_anticheat_badfile_action", "0", 0 );
    ac_badfile_message = Cvar_Get( "sv_anticheat_badfile_message", "", 0 );
    ac_badfile_max = Cvar_Get( "sv_anticheat_badfile_max", "0", 0 );
    ac_show_violation_reason = Cvar_Get( "sv_anticheat_show_violation_reason", "0", 0 );
    ac_client_disconnect_action = Cvar_Get( "sv_anticheat_client_disconnect_action", "0", 0 );
    ac_disable_play = Cvar_Get( "sv_anticheat_disable_play", "0", 0 );
    ac_disable_play->changed = ac_disable_play_changed;

    Cmd_Register( c_ac );

    List_Init( &ac_required_list );
    List_Init( &ac_exempt_list );
}


