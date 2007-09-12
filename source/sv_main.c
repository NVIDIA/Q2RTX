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

netadr_t	master_adr[MAX_MASTERS];	// address of group servers

LIST_DECL( sv_banlist );
LIST_DECL( sv_blacklist );

client_t	*sv_client;			// current client

cvar_t	*sv_enforcetime;

cvar_t	*sv_timeout;			// seconds without any message
cvar_t	*sv_zombietime;			// seconds to sink messages after disconnect
cvar_t	*sv_ghostime;

cvar_t	*rcon_password;			// password for remote server commands
cvar_t  *sv_password;
cvar_t  *sv_reserved_password;

cvar_t *allow_download;
cvar_t *allow_download_players;
cvar_t *allow_download_models;
cvar_t *allow_download_sounds;
cvar_t *allow_download_maps;
cvar_t *allow_download_demos;
cvar_t *allow_download_other;

cvar_t	*sv_airaccelerate;
cvar_t	*sv_qwmod;				// atu QW Physics modificator
cvar_t	*sv_noreload;			// don't reload level state when reentering

cvar_t	*sv_http_enable;
cvar_t	*sv_http_maxclients;
cvar_t	*sv_http_minclients;

cvar_t	*sv_maxclients;
cvar_t	*sv_reserved_slots;
cvar_t	*sv_showclamp;

cvar_t	*sv_hostname;
cvar_t	*sv_public;			// should heartbeats be sent

cvar_t	*sv_debug_send;
cvar_t	*sv_pad_packets;
cvar_t	*sv_lan_force_rate;

cvar_t	*sv_strafejump_hack;
cvar_t	*sv_bodyque_hack;
#ifndef _WIN32
cvar_t	*sv_oldgame_hack;
#endif

cvar_t	*sv_iplimit;
cvar_t	*sv_status_limit;
cvar_t	*sv_status_show;
cvar_t	*sv_badauth_time;

cvar_t	*sv_nextserver;

void Master_Shutdown (void);


//============================================================================

void SV_RemoveClient( client_t *client ) {
    if( client->msgpool ) {
        SV_PacketizedClear( client );
        Z_Free( client->msgpool );
        client->msgpool = NULL;
    }

    if( client->datagram.data ) {
    	Z_Free( client->datagram.data );
	    client->datagram.data = NULL;
        client->datagram.maxsize = 0;
    }

    if( client->netchan ) {
    	Netchan_Close( client->netchan );
	    client->netchan = NULL;
    }

    // unlink them from active clients list
    List_Delete( &client->entry );

	Com_DPrintf( "Going from cs_zombie to cs_free for %s\n", client->name );

	client->state = cs_free;	// can now be reused
	client->name[0] = 0;
}

void SV_CleanClient( client_t *client ) {
	int i;

	if( client->download ) {
		FS_FreeFile( client->download );
		client->download = NULL;
	}

	if( client->versionString ) {
		Z_Free( client->versionString );
		client->versionString = NULL;
	}

    // free baselines allocated for this client
    for( i = 0; i < SV_BASELINES_CHUNKS; i++ ) {
        if( client->baselines[i] ) {
            Z_Free( client->baselines[i] );
            client->baselines[i] = NULL;
        }
    }
}

/*
=====================
SV_DropClient

Called when the player is totally leaving the server, either willingly
or unwillingly.  This is NOT called if the entire server is quiting
or crashing.
=====================
*/
void SV_DropClient( client_t *client, const char *reason ) {
    int oldstate = client->state;

    if( client->state <= cs_zombie ) {
        return; // called recursively?
    }

    if( client == svs.mvdummy ) {
        Com_Printf( "Attempted to drop dummy MVD client, ignored.\n" );
        return;
    }

	client->state = cs_zombie;		// become free in a few seconds
    client->lastmessage = svs.realtime;

	if( reason ) {
        if( oldstate == cs_spawned ) {
            SV_BroadcastPrintf( PRINT_HIGH, "%s was dropped: %s\n",
                client->name, reason );
        }
        SV_ClientPrintf( client, PRINT_HIGH, "*** Connection dropped: %s ***\n",
            reason );
	}

	// add the disconnect
	MSG_WriteByte( svc_disconnect );
	SV_ClientAddMessage( client, MSG_RELIABLE|MSG_CLEAR );

	if( oldstate == cs_spawned ) {
		// call the prog function for removing a client
		// this will remove the body, among other things
		ge->ClientDisconnect( client->edict );
	}

	SV_CleanClient( client );

	Com_DPrintf( "Going to cs_zombie for %s\n", client->name );
}


/*
==============================================================================

CONNECTIONLESS COMMANDS

==============================================================================
*/

qboolean SV_RateLimited( ratelimit_t *r ) {
	if( !r->limit ) {
		return qfalse;
	}
	if( r->time < svs.realtime ) {
		r->count = 0;
		r->time = svs.realtime + r->period;
		return qfalse;
	}

	if( r->count < r->limit ) {
		return qfalse;
	}

	return qtrue;
}

void SV_RateInit( ratelimit_t *r, int limit, int period ) {
	if( limit < 0 ) {
		limit = 0;
	}
	if( period < 100 ) {
		period = 100;
	}
	r->count = 0;
	r->time = svs.realtime + period;
	r->limit = limit;
	r->period = period;
}

addrmatch_t *SV_MatchAddress( list_t *list, netadr_t *address ) {
    uint32 addr = *( uint32 * )address->ip;
    addrmatch_t *match;

    LIST_FOR_EACH( addrmatch_t, match, list, entry ) {
        if( ( addr & match->mask ) == ( match->addr & match->mask ) ) {
            return match;
        }
    }
    return NULL;
}

void SV_DumpMatches( list_t *list ) {
    addrmatch_t *match;
    byte ip[4];
    int i, count;

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

/*
===============
SV_StatusString

Builds the string that is sent as heartbeats and status replies
===============
*/
static char	*SV_StatusString( qboolean big ) {
	char	player[MAX_STRING_CHARS];
	static char	status[MAX_PACKETLEN_DEFAULT - 16];
	int		j;
	client_t	*cl;
	int		statusLength;
	int		playerLength;
	char	*s;

	if( big ) {
		s = Cvar_BitInfo_Big( CVAR_SERVERINFO );
	} else {
		s = Cvar_BitInfo( CVAR_SERVERINFO );
	}

    Info_SetValueForKey( s, "maxclients", va( "%d", sv_maxclients->integer -
        sv_reserved_slots->integer ) );

	statusLength = Com_sprintf( status, sizeof( status ), "%s\n", s );

	if( sv_status_show->integer < 2 ) {
		return status;
	}

    FOR_EACH_CLIENT( cl ) {
		if( cl->state == cs_zombie ) {
            continue;
        }
        if( sv.state == ss_broadcast ) {
            j = 0;
        } else {
            j = cl->edict->client->ps.stats[STAT_FRAGS];
        }
        playerLength = Com_sprintf( player, sizeof( player ),
            "%i %i \"%s\"\n", j, cl->ping, cl->name );
        if( statusLength + playerLength > sizeof( status ) - 1 )
            break;		// can't hold any more
        strcpy( status + statusLength, player );
        statusLength += playerLength;
	}

	return status;
}

static void SV_OobPrintf( const char *format, ... ) {
	va_list		argptr;
	byte        send_data[MAX_PACKETLEN_DEFAULT];
	int			length;

    /* write the packet header */
	*( uint32 * )send_data = -1;	/* -1 sequence means out of band */
	strcpy( ( char * )send_data + 4, "print\n" );
	
	va_start( argptr, format );
	length = Q_vsnprintf( ( char * )send_data + 10, sizeof( send_data ) - 10,
        format, argptr );
	va_end( argptr );

    /* send the datagram */
	NET_SendPacket( NS_SERVER, &net_from, length + 10, send_data );
}


/*
================
SVC_Status

Responds with all the info that qplug or qspy can see
================
*/
static void SVC_Status( void ) {
	char *s;
	int i, j, k;
	qboolean big = qfalse;

	if( !sv_status_show->integer ) {
		return;
	}

	if( SV_RateLimited( &svs.ratelimit_status ) ) {
		Com_DPrintf( "Dropping status request from %s\n",
            NET_AdrToString( &net_from ) );
		return;
	}

	svs.ratelimit_status.count++;

	/* parse additional parameters */
    j = Cmd_Argc();
	for( i = 1; i < j; i++ ) {
		s = Cmd_Argv( i );
		if( !strncmp( s, "p=", 2 ) ) {
			s += 2;
			while( *s ) {
				k = atoi( s );
				if( k == PROTOCOL_VERSION_Q2PRO ) {
					big = qtrue;
				}
				s = strchr( s, ',' );
				if( s == NULL ) {
					break;
				}
				s++;
			}
		}
    }

	SV_OobPrintf( "%s", SV_StatusString( big ) );
}

/*
================
SVC_Ack

================
*/
static void SVC_Ack( void ) {
	int i;

	for( i = 0; i < MAX_MASTERS; i++ ) {
		if( !master_adr[i].port ) {
			continue;
		}
		if( NET_IsEqualBaseAdr( &master_adr[i], &net_from ) ) {
			Com_Printf( "Ping acknowledge from %s\n",
                NET_AdrToString( &net_from ) );
			break;
		}
	}
}

/*
================
SVC_Info

Responds with short info for broadcast scans
The second parameter should be the current protocol version number.
================
*/
static void SVC_Info( void ) {
	char	string[MAX_QPATH];
	int		count;
	int		version;
    client_t *client;

	if (sv_maxclients->integer == 1)
		return;		// ignore in single player

	version = atoi (Cmd_Argv(1));

	if (version != PROTOCOL_VERSION_DEFAULT) {
		return;
	}
	
	count = 0;
    FOR_EACH_CLIENT( client ) {
		if (client->state != cs_zombie)
			count++;
    }

	Com_sprintf (string, sizeof(string), "%16s %8s %2i/%2i\n",
		sv_hostname->string, sv.name, count, sv_maxclients->integer -
        sv_reserved_slots->integer );
	
	Netchan_OutOfBandPrint (NS_SERVER, &net_from, "info\n%s", string);
}

/*
================
SVC_Ping

Just responds with an acknowledgement
================
*/
static void SVC_Ping( void ) {
	Netchan_OutOfBandPrint( NS_SERVER, &net_from, "ack" );
}

/*
=================
SVC_GetChallenge

Returns a challenge number that can be used
in a subsequent client_connect command.
We do this to prevent denial of service attacks that
flood the server with invalid connection IPs.  With a
challenge, they must give a valid IP address.
=================
*/
static void SVC_GetChallenge( void ) {
	int		i, challenge;
	int		oldest;
	int		oldestTime;

	oldest = 0;
	oldestTime = 0x7fffffff;

	// see if we already have a challenge for this ip
	for( i = 0; i < MAX_CHALLENGES; i++ ) {
		if( NET_IsEqualBaseAdr ( &net_from, &svs.challenges[i].adr ) )
			break;
		if( svs.challenges[i].time < oldestTime ) {
			oldestTime = svs.challenges[i].time;
			oldest = i;
		}
	}

    challenge = ( ( rand() << 16 ) | rand() ) & 0x7fffffff;
	if( i == MAX_CHALLENGES ) {
		// overwrite the oldest
		svs.challenges[oldest].challenge = challenge;
		svs.challenges[oldest].adr = net_from;
		svs.challenges[oldest].time = Sys_Milliseconds();
	} else {
		svs.challenges[i].challenge = challenge;
		svs.challenges[i].time = Sys_Milliseconds();
	}

	// send it back
	Netchan_OutOfBandPrint( NS_SERVER, &net_from,
        "challenge %d p=34,35,36:%d", challenge,
        PROTOCOL_VERSION_Q2PRO_MINOR );
}

/*
==================
SVC_DirectConnect

A connection request that did not come from the master
==================
*/
static void SVC_DirectConnect( void ) {
	char		userinfo[MAX_INFO_STRING];
	int			i, number, count, length;
	client_t	*cl, *newcl, *lastcl;
	int			protocol;
	int			qport;
	int			challenge;
	qboolean	allow;
	char		*info, *s;
    int         maxlength;
    netchan_type_t nctype;
    char        *ncstring;
    int         reserved;
    byte        *buffer;
    qboolean    zlib;

	protocol = atoi( Cmd_Argv( 1 ) );
	qport = atoi( Cmd_Argv( 2 ) ) ;
	challenge = atoi( Cmd_Argv( 3 ) );

	Com_DPrintf( "SVC_DirectConnect: protocol=%i, qport=%i, challenge=%i\n",
        protocol, qport, challenge );

    if( protocol < PROTOCOL_VERSION_DEFAULT ||
        protocol > PROTOCOL_VERSION_Q2PRO )
    {
        SV_OobPrintf( "Unsupported protocol version %d.\n", protocol );
        Com_DPrintf( "    rejected connect with protocol %i\n", protocol );
        return;
    }

	if( !NET_IsLocalAddress( &net_from ) ) {
        // check for banned address
        if( SV_MatchAddress( &sv_banlist, &net_from ) ) {
            SV_OobPrintf( "Your IP address is banned.\n" );
            Com_DPrintf( "   rejected connect from banned IP\n" );
            return;
        }

	    // see if the challenge is valid
		for( i = 0; i < MAX_CHALLENGES; i++ ) {
			if( !svs.challenges[i].challenge ) {
				continue;
			}
			if( NET_IsEqualBaseAdr( &net_from, &svs.challenges[i].adr ) ) {
				if( svs.challenges[i].challenge == challenge )
					break;		// good
				SV_OobPrintf( "Bad challenge.\n" );
				Com_DPrintf( "    rejected -  bad challenge.\n" );
				return;
			}
		}
		if( i == MAX_CHALLENGES ) {
			SV_OobPrintf( "No challenge for address.\n" );
			Com_DPrintf( "    rejected - no challenge.\n" );
			return;
		}
		svs.challenges[i].challenge = 0;
	}

	// limit number of connections from single IP
	if( sv_iplimit->integer > 0 ) {
		count = 0;
        FOR_EACH_CLIENT( cl ) {
			if( NET_IsEqualBaseAdr( &net_from, &cl->netchan->remote_address ) ) 
            {
				if( cl->state == cs_zombie ) {
					count++;
				} else {
					count += 2;
				}
			}
		}
		count >>= 1;
		if( count >= sv_iplimit->integer ) {
			SV_OobPrintf( "Too many connections from your IP address.\n" );
			Com_DPrintf( "    rejected - %d connections from this IP.\n", count );
			return;
		}
	}

    // set maximum message length
    maxlength = MAX_PACKETLEN_WRITABLE_DEFAULT;
    zlib = qfalse;
    if( protocol >= PROTOCOL_VERSION_R1Q2 ) {
        zlib = qtrue;
        s = Cmd_Argv( 5 );
        if( *s ) {
            maxlength = atoi( s );
            if( maxlength < 0 || maxlength > MAX_PACKETLEN_WRITABLE ) {
                SV_OobPrintf( "Invalid maximum packet length.\n" );
                Com_DPrintf( "    rejected - bad maxpacketlen.\n" );
                return;
            }
            if( !maxlength ) {
                maxlength = MAX_PACKETLEN_WRITABLE;
            }
        }
    }

    // cap maximum message length
    if( net_maxmsglen->integer > 0 && maxlength > net_maxmsglen->integer ) {
        maxlength = net_maxmsglen->integer;
    }

    // set netchan type
    if( protocol == PROTOCOL_VERSION_Q2PRO ) {
        s = Cmd_Argv( 6 );
        if( *s ) {
            nctype = atoi( s );
            if( nctype == NETCHAN_OLD ) {
                ncstring = " nc=0";
            } else if( nctype == NETCHAN_NEW ) {
                ncstring = " nc=1";
            } else {
                SV_OobPrintf( "Invalid netchan type.\n" );
                Com_DPrintf( "    rejected - bad nctype.\n" );
                return;
            }
        } else {
            nctype = NETCHAN_NEW;
            ncstring = " nc=1";
        }

        // set zlib
        s = Cmd_Argv( 7 );
        if( *s && !atoi( s ) ) {
            zlib = qfalse;
        }
    } else {
        nctype = NETCHAN_OLD;
        ncstring = "";
    }

    // validate userinfo
	info = Cmd_Argv( 4 );
	if( !info[0] ) {
        SV_OobPrintf( "Empty userinfo string.\n" );
		Com_DPrintf( "    rejected - empty userinfo.\n" );
		return;
	}

	if( !Info_Validate( info ) ) {
        SV_OobPrintf( "Malformed userinfo string.\n" );
		Com_DPrintf( "    rejected - malformed userinfo.\n" );
        return;
	}

	s = Info_ValueForKey( info, "name" );
	if( !s[0] || Q_IsWhiteSpace( s ) ) {
        SV_OobPrintf( "Please set your name before connecting.\n" );
		Com_DPrintf( "    rejected - empty name.\n" );
        return;
	}

	length = strlen( s );
	if( length > MAX_CLIENT_NAME - 1 ) {
        SV_OobPrintf( "Names longer than %d characters are not allowed.\n"
            "Your name is %d characters long.\n", MAX_CLIENT_NAME - 1, length );
		Com_DPrintf( "    rejected - oversize name (%d chars).\n", length );
		return;
	}
	
	s = Info_ValueForKey( info, "skin" );
	if( !s[0] ) {
        SV_OobPrintf( "Please set your skin before connecting.\n" );
		Com_DPrintf( "    rejected - empty skin.\n" );
        return;
	}
	
	length = strlen( s );
	if( !Q_ispath( s[0] ) || !Q_ispath( s[ length - 1 ] ) || strchr( s, '.' ) ) 
    {
        SV_OobPrintf( "Malformed skin.\n" );
		Com_DPrintf( "    rejected - malformed skin.\n" );
		return;
	}

    // check password
    s = Info_ValueForKey( info, "password" );
    reserved = 0;
    if( sv_password->string[0] ) {
        if( SV_RateLimited( &svs.ratelimit_badpass ) ) {
            Com_DPrintf( "    rejected - auth attempt limit exceeded.\n" );
            return;
        }
        if( !s[0] ) {
            SV_OobPrintf( "Please set your password before connecting.\n" );
            Com_DPrintf( "    rejected - empty password.\n" );
            return;
        }
        if( strcmp( sv_password->string, s ) ) {
            svs.ratelimit_badpass.count++;
            SV_OobPrintf( "Invalid password.\n" );
            Com_DPrintf( "    rejected - invalid password.\n" );
            return;
        }
        // allow them to use reserved slots
    } else if( !sv_reserved_password->string[0] ||
            strcmp( sv_reserved_password->string, s ) )
    {
        // in no reserved password is set on the server, do not allow
        // anyone to access reserved slots at all
        reserved = sv_reserved_slots->integer;
    }

	Q_strncpyz( userinfo, info, sizeof( userinfo ) );

	// force the IP key/value pair so the game can filter based on ip
	s = NET_AdrToString( &net_from );
	if( !Info_AttemptSetValueForKey( userinfo, "ip", s ) ) {
		SV_OobPrintf( "Oversize userinfo string.\n" );
		Com_DPrintf( "    rejected - oversize userinfo.\n" );
		return;
	}

	newcl = NULL;

	// if there is already a slot for this ip, reuse it
    FOR_EACH_CLIENT( cl ) {
		if( NET_IsEqualAdr( &net_from, &cl->netchan->remote_address ) ) {
			if( cl->state != cs_zombie ) {
				SV_DropClient( cl, "reconnected" );
			}

			Com_DPrintf( "%s: reconnect\n", NET_AdrToString( &net_from ) );
			newcl = cl;

			// NOTE: it's safe to call SV_Remove since we exit the loop
			SV_RemoveClient( cl );
			break;
		}
	}

	// find a client slot
	if( !newcl ) {
        lastcl = svs.clientpool + sv_maxclients->integer - reserved;
        for( newcl = svs.clientpool; newcl < lastcl; newcl++ ) {
            if( !newcl->state ) {
                break;
            }
        }
		if( newcl == lastcl ) {
            if( sv_reserved_slots->integer && !reserved ) {
    			SV_OobPrintf( "Server and reserved slots are full.\n" );
			    Com_DPrintf( "    rejected - reserved slots are full.\n" );
            } else {
    			SV_OobPrintf( "Server is full.\n" );
			    Com_DPrintf( "    rejected - server is full.\n" );
            }
			return;
		}
	}

	// build a new connection
	// accept the new client
	// this is the only place a client_t is ever initialized
	memset( newcl, 0, sizeof( *newcl ) );
	number = newcl - svs.clientpool;
    newcl->number = number;
	newcl->challenge = challenge; // save challenge for checksumming
	newcl->protocol = protocol;
    newcl->zlib = zlib;
	newcl->edict = EDICT_NUM( number + 1 );
    newcl->gamedir = fs_game->string;
    newcl->mapname = sv.configstrings[CS_NAME];
    newcl->configstrings = ( char * )sv.configstrings;
    newcl->pool = ( edict_pool_t * )&ge->edicts;
    newcl->cm = &sv.cm;

    // default pmove parameters
    newcl->pmp.maxspeed = 300;
    newcl->pmp.upspeed = 350;
    newcl->pmp.friction = 6;
    newcl->pmp.flyfriction = 9;
    newcl->pmp.waterfriction = 1;
    newcl->pmp.airaccelerate = sv_airaccelerate->integer ? qtrue : qfalse;

    // r1q2 extensions
	if( protocol == PROTOCOL_VERSION_R1Q2 ||
		protocol == PROTOCOL_VERSION_Q2PRO )
	{
		newcl->pmp.speedMultiplier = 2;
		newcl->pmp.strafeHack = sv_strafejump_hack->integer ? qtrue : qfalse;
	} else {
		newcl->pmp.speedMultiplier = 1;
		newcl->pmp.strafeHack = qfalse;
	}

    // q2pro extensions
	if( protocol == PROTOCOL_VERSION_Q2PRO ) {
        if( sv_qwmod->integer ) {
            newcl->pmp.qwmod = sv_qwmod->integer;
            newcl->pmp.maxspeed = 320;
            newcl->pmp.upspeed = ( sv_qwmod->integer > 1 ) ? 310 : 350;
            newcl->pmp.friction = 4;
            newcl->pmp.waterfriction = 4;
            newcl->pmp.airaccelerate = qtrue;
        }
        newcl->pmp.flyfix = qtrue;
        newcl->pmp.flyfriction = 4;
	}

	// get the game a chance to reject this connection or modify the userinfo
	sv_client = newcl;
	sv_player = newcl->edict;
	allow = ge->ClientConnect( newcl->edict, userinfo );
	sv_client = NULL;
	sv_player = NULL;
	if ( !allow ) {
		char *reason;

		reason = Info_ValueForKey( userinfo, "rejmsg" );
		if( *reason ) { 
			SV_OobPrintf( "%s\nConnection refused.\n", reason );
		} else {
			SV_OobPrintf( "Connection refused.\n" );
		}
		Com_DPrintf( "    game rejected a connection.\n" );
		return;
	}
    
    // setup netchan
    newcl->netchan = Netchan_Setup( NS_SERVER, nctype, &net_from,
        qport, maxlength, protocol );

	// parse some info from the info strings
	Q_strncpyz( newcl->userinfo, userinfo, sizeof( newcl->userinfo ) );
	SV_UserinfoChanged( newcl );

	// send the connect packet to the client
	Netchan_OutOfBandPrint( NS_SERVER, &net_from, "client_connect%s",
        ncstring );

    List_Init( &newcl->freemsg );
    List_Init( &newcl->inusemsg[0] );
    List_Init( &newcl->inusemsg[1] );
    List_Init( &newcl->soundmsg );

    newcl->msgpool = SV_Malloc( sizeof( message_packet_t ) * MSG_POOLSIZE );
    for( i = 0; i < MSG_POOLSIZE; i++ ) {
        List_Append( &newcl->freemsg, &newcl->msgpool[i].entry );
    }

    // setup protocol
    if( nctype == NETCHAN_NEW ) {
        buffer = SV_Malloc( MAX_MSGLEN );
        SZ_Init( &newcl->datagram, buffer, MAX_MSGLEN );
        newcl->datagram.allowoverflow = qtrue;
        newcl->AddMessage = SV_NewClientAddMessage;
        newcl->WriteDatagram = SV_NewClientWriteDatagram;
        newcl->FinishFrame = SV_NewClientFinishFrame;
    } else {
        newcl->AddMessage = SV_OldClientAddMessage;
        newcl->WriteDatagram = SV_OldClientWriteDatagram;
        newcl->FinishFrame = SV_OldClientFinishFrame;
    }
    if( protocol == PROTOCOL_VERSION_DEFAULT ) {
        newcl->WriteFrame = SV_WriteFrameToClient_Default;
    } else {
        newcl->WriteFrame = SV_WriteFrameToClient_Enhanced;
    }

    // add them to the linked list of connected clients
    List_Append( &svs.clients, &newcl->entry );

	Com_DPrintf( "Going from cs_free to cs_assigned for %s\n", newcl->name );
	newcl->state = cs_assigned;
	newcl->lastframe = -1;
	newcl->lastmessage = svs.realtime;	// don't timeout
}

static int Rcon_Validate (void)
{
	if (!rcon_password->string[0])
		return 0;

	if (strcmp (Cmd_Argv(1), rcon_password->string) )
		return 0;

	return 1;
}

/*
===============
SVC_RemoteCommand

A client issued an rcon command.
Redirect all printfs.
===============
*/
static void SVC_RemoteCommand( void ) {
	int		i;
	char *string;

	if( SV_RateLimited( &svs.ratelimit_badrcon ) ) {
		Com_DPrintf( "Dropping rcon from %s\n",
			NET_AdrToString( &net_from ) );
		return;
	}

	i = Rcon_Validate();
	string = Cmd_RawArgsFrom( 2 );
	if( i == 0 ) {
		Com_Printf( "Invalid rcon from %s:\n%s\n",
			NET_AdrToString( &net_from ), string );
		SV_OobPrintf( "Bad rcon_password.\n" );
		svs.ratelimit_badrcon.count++;
		return;
    }

	Com_Printf( "Rcon from %s:\n%s\n",
		NET_AdrToString( &net_from ), string );

	Com_BeginRedirect( RD_PACKET, sv_outputbuf, SV_OUTPUTBUF_LENGTH,
            SV_FlushRedirect );

	Cmd_ExecuteString( string );

	Com_EndRedirect();
}

static const struct {
    const char  *name;
    void        (*func)( void );
    qboolean    passive;
} svcmds[] = {
    { "ping",           SVC_Ping                    },
    { "ack",            SVC_Ack                     },
    { "status",         SVC_Status                  },
    { "info",           SVC_Info                    },
    { "getchallenge",   SVC_GetChallenge            },
    { "connect",        SVC_DirectConnect           },
    { "rcon",           SVC_RemoteCommand, qtrue    },
    { NULL }
};

/*
=================
SV_ConnectionlessPacket

A connectionless packet has four leading 0xff
characters to distinguish it from a game channel.
Clients that are in the game can still send
connectionless packets.
=================
*/
static void SV_ConnectionlessPacket( void ) {
	char	*s, *c;
    int     i;

	MSG_BeginReading();
	MSG_ReadLong();		// skip the -1 marker

	s = MSG_ReadStringLine();

	Cmd_TokenizeString( s, qfalse );

	c = Cmd_Argv( 0 );
	Com_DPrintf( "ServerPacket %s : %s\n", NET_AdrToString( &net_from ), c );

	if( !NET_IsLocalAddress( &net_from ) && net_from.ip[0] == 127 &&
		net_from.port == Cvar_VariableInteger( "net_port" ) )
	{
		Com_DPrintf( "dropped connectionless packet from self\n" );
		return;
	}

    for( i = 0; svcmds[i].name; i++ ) {
        if( !strcmp( c, svcmds[i].name ) ) {
            if( !svs.initialized && !svcmds[i].passive ) {
                Com_DPrintf( "ignored connectionless packet\n" );
            } else {
                svcmds[i].func();
            }
            return;
        }
    }

	Com_DPrintf( "bad connectionless packet\n" );
}


//============================================================================

/*
===================
SV_CalcPings

Updates the cl->ping variables
===================
*/
static void SV_CalcPings( void ) {
	int			j;
	client_t	*cl;
	int			total, count;

    FOR_EACH_CLIENT( cl ) {
		if( cl->state != cs_spawned )
			continue;

		total = 0;
		count = 0;
		for( j = 0; j < LATENCY_COUNTS; j++ ) {
			if( cl->frame_latency[j] > 0 ) {
				count++;
				total += cl->frame_latency[j];
			}
		}
		if( !count )
			cl->ping = 0;
		else
			cl->ping = total / count;

		// let the game dll know about the ping
		cl->edict->client->ping = cl->ping;
	}
}


/*
===================
SV_GiveMsec

Every few frames, gives all clients an allotment of milliseconds
for their command moves.  If they exceed it, assume cheating.
===================
*/
static void SV_GiveMsec( void ) {
	client_t	*cl;

	if( sv.framenum & 15 )
		return;

    FOR_EACH_CLIENT( cl ) {
		cl->commandMsec = 1800;		// 1600 + some slop
	}
}


/*
=================
SV_PacketEvent
=================
*/
void SV_PacketEvent( neterr_t ret ) {
	client_t	*client;
    netchan_t   *netchan;
	int			qport;

	// check for connectionless packet (0xffffffff) first
    // connectionless packets are processed even if the server is down
	if( ret == NET_OK && *( int * )msg_read.data == -1 ) {
		SV_ConnectionlessPacket();
		return;
	}

	if( !svs.initialized ) {
		return;
	}

	if( ret == NET_ERROR ) {
		// check for errors from connected clients
        FOR_EACH_CLIENT( client ) {
			if( client->state == cs_zombie ) {
				continue; // already a zombie
			}
            netchan = client->netchan;
			if( !NET_IsEqualAdr( &net_from, &netchan->remote_address ) ) {
				continue;
			}
            if( client->lastmessage > svs.realtime ) {
                client->lastmessage = svs.realtime;
            }
            if( svs.realtime - client->lastmessage > 6000 ) {
    			SV_DropClient( client, "connection reset by peer" );
            }
			break;
		}
		return;
	}

	// check for packets from connected clients
    FOR_EACH_CLIENT( client ) {
        netchan = client->netchan;
		if( !NET_IsEqualBaseAdr( &net_from, &netchan->remote_address ) ) {
			continue;
		}

		// read the qport out of the message so we can fix up
		// stupid address translating routers
		if( client->protocol == PROTOCOL_VERSION_DEFAULT ) {
			qport = msg_read.data[8] | ( msg_read.data[9] << 8 );
			if( netchan->qport != qport ) {
				continue;
			}
		} else if( netchan->qport ) {
			qport = msg_read.data[8];
			if( netchan->qport != qport ) {
				continue;
			}
		}

		if( netchan->remote_address.port != net_from.port ) {
			Com_DPrintf( "Fixing up a translated port for %s: %d --> %d\n",
                client->name, netchan->remote_address.port, net_from.port );
			netchan->remote_address.port = net_from.port;
		}

		if( netchan->Process( netchan ) ) {
			// this is a valid, sequenced packet, so process it
			if( client->state != cs_zombie ) {
                if( client->state != cs_assigned ) {
    				client->lastmessage = svs.realtime;	// don't timeout
                }
				SV_ExecuteClientMessage( client );
			}
		}

		break;
	}
}

/*
==================
SV_SendAsyncPackets

If the client is just connecting, it is pointless to wait another 100ms
before sending next command and/or reliable acknowledge, send it as soon
as client rate limit allows.

For spawned clients, this is not used, as we are forced to send svc_frame
packets synchronously with game DLL ticks.
==================
*/
void SV_SendAsyncPackets( void ) {
	qboolean retransmit;
	client_t	*client;
    netchan_t   *netchan;
	int cursize;
	
    FOR_EACH_CLIENT( client ) {
		// don't overrun bandwidth
		if( client->sendTime > svs.realtime ) {
			continue;
		}

        netchan = client->netchan;
        
        // make sure all fragments are transmitted first
        if( netchan->fragment_pending ) {
            cursize = netchan->TransmitNextFragment( netchan );
			if( sv_debug_send->integer ) {
				Com_Printf( S_COLOR_BLUE"%s: frag: %d\n",
                    client->name, cursize );
            }
            goto calctime;
        }

		// spawned clients are handled elsewhere
		if( client->state == cs_spawned && !client->download &&
            !client->nodata )
        {
			continue;
		}

        // see if it's time to resend a (possibly dropped) packet
        retransmit = qfalse;
        if( netchan->last_sent > com_localTime ) {
            netchan->last_sent = com_localTime;
        }
        if( com_localTime - netchan->last_sent > 1000 ) {
            retransmit = qtrue;
        }

        // don't write new reliables if not yet acknowledged
        if( netchan->reliable_length && !retransmit ) {
            continue;
        }

		// just update reliable	if needed
		if( netchan->type == NETCHAN_OLD ) {
			SV_OldClientWriteReliableMessages( client, netchan->maxpacketlen );
		}
		if( netchan->message.cursize || netchan->reliable_ack_pending ||
            netchan->reliable_length || retransmit )
        {
			cursize = netchan->Transmit( netchan, 0, NULL );
			if( sv_debug_send->integer ) {
				Com_Printf( S_COLOR_BLUE"%s: send: %d\n",
                    client->name, cursize );
            }
calctime:
			SV_CalcSendTime( client, cursize );
		}
	}
}

/*
==================
SV_CheckTimeouts

If a packet has not been received from a client for timeout->value
seconds, drop the conneciton.  Server frames are used instead of
realtime to avoid dropping the local client while debugging.

When a client is normally dropped, the client_t goes into a zombie state
for a few seconds to make sure any final reliable message gets resent
if necessary
==================
*/
static void SV_CheckTimeouts( void ) {
	client_t	*client, *next;
    uint32      point;

    LIST_FOR_EACH_SAFE( client_t, client, next, &svs.clients, entry ) {
		// message times may be wrong across a changelevel
		if( client->lastmessage > svs.realtime ) {
			client->lastmessage = svs.realtime;
		}
#ifndef DEDICATED_ONLY
		// never timeout local clients
		if( NET_IsLocalAddress( &client->netchan->remote_address ) ) {
			continue;
		}
#endif
		if( client->state == cs_zombie ) {
            if( client->lastmessage < svs.zombiepoint ) {
    			SV_RemoveClient( client );
	    		continue;
	    	}
        } else {
            point = svs.droppoint;
            if( client->state == cs_assigned && point < svs.ghostpoint ) {
                point = svs.ghostpoint;
            }
		    if( client->lastmessage < point ) {
			    SV_DropClient( client, "timed out" );
			    SV_RemoveClient( client );	// don't bother with zombie state	
            }
		}
	}
}

/*
================
SV_PrepWorldFrame

This has to be done before the world logic, because
player processing happens outside RunWorldFrame
================
*/
static void SV_PrepWorldFrame( void ) {
	edict_t	*ent;
	int		i;

	for( i = 1; i < ge->num_edicts; i++, ent++ ) {
		ent = EDICT_NUM( i );
		// events only last for a single message
		ent->s.event = 0;
	}	

	sv.tracecount = 0;
}

/*
=================
SV_RunGameFrame
=================
*/
static void SV_RunGameFrame( void ) {
	if( host_speeds->integer )
		time_before_game = Sys_Milliseconds();

	// we always need to bump framenum, even if we
	// don't run the world, otherwise the delta
	// compression can get confused when a client
	// has the "current" frame
	sv.framenum++;
	sv.time += 100;

    if( svs.mvdummy ) {
    	SV_MvdBeginFrame();
    }
	
	ge->RunFrame();

	if( msg_write.cursize ) {
		Com_WPrintf( "Game DLL left %d bytes in multicast buffer, cleared.\n",
            msg_write.cursize );
		SZ_Clear( &msg_write );
	}

	// save the entire world state if recording a serverdemo
    if( svs.mvdummy ) {
	    SV_MvdEndFrame();
    }

	// never get more than one tic behind
	if( sv.time < svs.realtime ) {
		if( sv_showclamp->integer )
			Com_Printf( "sv highclamp\n" );
		svs.realtime = sv.time;
	}

	if( host_speeds->integer )
		time_after_game = Sys_Milliseconds();

}

#ifndef DEDICATED_ONLY
static qboolean SV_CheckPaused( void ) {
	client_t *client;

	sv_paused->integer = 0;
	if( cl_paused->integer ) {
        FOR_EACH_CLIENT( client ) {
            if( client->state != cs_spawned ) {
                break; // never pause if loading
            }
            if( !NET_IsLocalAddress( &client->netchan->remote_address ) ) {
		        cl_paused->integer = 0;
                return qfalse; // never pause in multiplayer
            }
		}
		sv_paused->integer = 1;
		return qtrue; // don't run if paused
	}
    return qfalse;
}
#endif

/*
==================
SV_Frame

==================
*/
void SV_Frame( int msec ) {
    uint32 time;

	time_before_game = time_after_game = 0;

	svs.realtime += msec;

	MVD_Frame();

	// if server is not active, do nothing
	if( !svs.initialized ) {
		if( dedicated->integer ) {
			NET_Sleep( 1 );
		}
		return;
	}

#ifndef DEDICATED_ONLY
	if( SV_CheckPaused() ) {
		return;
	}
#endif

	time = 1000 * sv_timeout->value;
	svs.droppoint = svs.realtime > time ? svs.realtime - time : 0;

	time = 1000 * sv_zombietime->value;
	svs.zombiepoint = svs.realtime > time ? svs.realtime - time : 0;

	time = 1000 * sv_ghostime->value;
	svs.ghostpoint = svs.realtime > time ? svs.realtime - time : 0;

    if( sv_http_enable->integer ) {
        SV_HttpRun();
    }

	// check timeouts
	SV_CheckTimeouts ();

    // deliver fragments and reliable messages for connecting clients
	SV_SendAsyncPackets();

	// move autonomous things around if enough time has passed
	if( !com_timedemo->integer && svs.realtime < sv.time ) {
		// never let the time get too far off
		if( sv.time - svs.realtime > 100 ) {
			if( sv_showclamp->integer )
				Com_Printf( "sv lowclamp\n" );
			svs.realtime = sv.time - 100;
		}
		if( dedicated->integer && com_sleep->integer ) {
            if( com_sleep->integer > 1 ) {
    			NET_Sleep( sv.time - svs.realtime );
            } else {
                NET_Sleep( 1 );
            }
		}
		return;
	}

	// update ping based on the last known frame from all clients
	SV_CalcPings ();

	// give the clients some timeslices
	SV_GiveMsec ();

	// let everything in the world think and move
	SV_RunGameFrame ();

	// send messages back to the clients that had packets read this frame
	SV_SendClientMessages ();

	// send a heartbeat to the master if needed
	Master_Heartbeat ();

	// clear teleport flags, etc for next frame
	if( sv.state != ss_broadcast ) {
		SV_PrepWorldFrame ();
	}
}

//============================================================================

/*
================
Master_Heartbeat

Send a message to the master every few minutes to
let it know we are alive, and log information
================
*/
#define	HEARTBEAT_SECONDS	300
void Master_Heartbeat (void)
{
	char		*string;
	int			i;

	// pgm post3.19 change, cvar pointer not validated before dereferencing
	if (!dedicated || !dedicated->integer)
		return;		// only dedicated servers send heartbeats

	// pgm post3.19 change, cvar pointer not validated before dereferencing
	if (!sv_public || !sv_public->integer)
		return;		// a private dedicated game

	// check for time wraparound
	if (svs.last_heartbeat > svs.realtime)
		svs.last_heartbeat = svs.realtime;

	if (svs.realtime - svs.last_heartbeat < HEARTBEAT_SECONDS*1000)
		return;		// not time to send yet

	svs.last_heartbeat = svs.realtime;

	// send the same string that we would give for a status OOB command
	string = SV_StatusString( qfalse );

	// send to group master
	for( i = 0; i < MAX_MASTERS; i++ ) {
		if( master_adr[i].port ) {
			Com_Printf( "Sending heartbeat to %s\n",
                NET_AdrToString( &master_adr[i] ) );
			Netchan_OutOfBandPrint( NS_SERVER, &master_adr[i],
                "heartbeat\n%s", string );
		}
    }
}

/*
=================
Master_Shutdown

Informs all masters that this server is going down
=================
*/
void Master_Shutdown( void ) {
	int			i;

	// pgm post3.19 change, cvar pointer not validated before dereferencing
	if (!dedicated || !dedicated->integer)
		return;		// only dedicated servers send heartbeats

	// pgm post3.19 change, cvar pointer not validated before dereferencing
	if (!sv_public || !sv_public->integer)
		return;		// a private dedicated game

	// send to group master
	for( i = 0; i < MAX_MASTERS; i++ ) {
		if( master_adr[i].port ) {
			Com_Printf( "Sending shutdown to %s\n",
                NET_AdrToString( &master_adr[i] ) );
			Netchan_OutOfBandPrint( NS_SERVER, &master_adr[i], "shutdown" );
		}
    }
}

//============================================================================

/*
=================
SV_UpdateUserinfo

Ensures that name, skin and ip are properly set.
WARNING: may modify userinfo in place!
=================
*/
void SV_UpdateUserinfo( char *userinfo ) {
	char *s;
	int length;

	if( !userinfo[0] ) {
		SV_ClientPrintf( sv_client, PRINT_HIGH,
			"Empty userinfo string supplied. Ignored.\n" );
		return;
	}
	
	if( !Info_Validate( userinfo ) ) {
		SV_ClientPrintf( sv_client, PRINT_HIGH,
			"Malformed userinfo string supplied. Ignored.\n" );
		return;
	}

	s = Info_ValueForKey( userinfo, "name" );
	if( !s[0] || Q_IsWhiteSpace( s ) ) {
		SV_ClientPrintf( sv_client, PRINT_HIGH,
			"Empty name supplied in userinfo. Ignored.\n" );
		return;
	}
		
	length = strlen( s );
	if(	length > MAX_CLIENT_NAME - 1 ) {
		SV_ClientPrintf( sv_client, PRINT_HIGH,
			"Oversize name supplied in userinfo. Ignored.\n" );
		return;
	}

    s = Info_ValueForKey( userinfo, "skin" );
    if( !s[0] ) {
        SV_ClientPrintf( sv_client, PRINT_HIGH,
            "Empty skin supplied in userinfo. Ignored.\n" );
        return;
    }
    
    length = strlen( s );
    if( !Q_ispath( s[0] ) || !Q_ispath( s[ length - 1 ] ) || strchr( s, '.' ) ) 
    {
        SV_ClientPrintf( sv_client, PRINT_HIGH,
            "Malformed skin supplied in userinfo. Ignored.\n" );
        return;
    }

	// force the IP key/value pair so the game can filter based on ip
	s = NET_AdrToString( &sv_client->netchan->remote_address );
	if( !Info_AttemptSetValueForKey( userinfo, "ip", s ) ) {
		return;
	}

	strcpy( sv_client->userinfo, userinfo );

	SV_UserinfoChanged( sv_client );
}


/*
=================
SV_UserinfoChanged

Pull specific info from a newly changed userinfo string
into a more C freindly form.
=================
*/
void SV_UserinfoChanged( client_t *cl ) {
	char	*val;
	int		i;

	// call prog code to allow overrides
	ge->ClientUserinfoChanged( cl->edict, cl->userinfo );
	
	// name for C code
	val = Info_ValueForKey( cl->userinfo, "name" );
	Q_strncpyz( cl->name, val, sizeof( cl->name ) );
	// mask off high bit
	for( i = 0; cl->name[i]; i++ )
		cl->name[i] &= 127;

	// rate command
	val = Info_ValueForKey( cl->userinfo, "rate" );
	if( *val ) {
		cl->rate = atoi( val );
		clamp( cl->rate, 100, 15000 );
	} else {
		cl->rate = 5000;
	}

	// never drop over the loopback
    if( cl->netchan ) {
        netadr_t *address = &cl->netchan->remote_address;
        if( NET_IsLocalAddress( address ) ||
            ( sv_lan_force_rate->integer && NET_IsLanAddress( address ) ) )
        {
            cl->rate = 0;
        }
    }

	// msg command
	val = Info_ValueForKey( cl->userinfo, "msg" );
	if( *val ) {
		cl->messagelevel = atoi( val );
	}
}


//============================================================================

static void sv_status_limit_changed( cvar_t *self ) {
	SV_RateInit( &svs.ratelimit_status, self->integer, 1000 );
}

static void sv_badauth_time_changed( cvar_t *self ) {
	SV_RateInit( &svs.ratelimit_badpass, 1, self->value * 1000 );
	SV_RateInit( &svs.ratelimit_badrcon, 1, self->value * 1000 );
}

/*
===============
SV_Init

Only called at quake2.exe startup, not for each game
===============
*/
void SV_Init( void ) {
	SV_InitOperatorCommands	();
    SV_MvdRegister();
	MVD_Register();

	Cvar_Get( "protocol", va( "%i", PROTOCOL_VERSION_DEFAULT ), CVAR_SERVERINFO|CVAR_ROM );
	
	Cvar_Get( "skill", "1", CVAR_LATCH );
	Cvar_Get( "deathmatch", "0", CVAR_SERVERINFO|CVAR_LATCH );
	Cvar_Get( "coop", "0", CVAR_SERVERINFO|CVAR_LATCH );
	Cvar_Get( "cheats", "0", CVAR_SERVERINFO|CVAR_LATCH );
	Cvar_Get( "dmflags", va( "%i", DF_INSTANT_ITEMS ), CVAR_SERVERINFO );
	Cvar_Get( "fraglimit", "0", CVAR_SERVERINFO );
	Cvar_Get( "timelimit", "0", CVAR_SERVERINFO );

	sv_maxclients = Cvar_Get( "maxclients", "1", CVAR_SERVERINFO|CVAR_LATCH );
	sv_reserved_slots = Cvar_Get( "sv_reserved_slots", "0", CVAR_LATCH );
	sv_hostname = Cvar_Get( "hostname", "noname", CVAR_SERVERINFO|CVAR_ARCHIVE );
	sv_timeout = Cvar_Get( "timeout", "125", 0 );
	sv_zombietime = Cvar_Get( "zombietime", "2", 0 );
	sv_ghostime = Cvar_Get( "sv_ghostime", "6", 0 );
	sv_showclamp = Cvar_Get( "showclamp", "0", 0 );
	sv_enforcetime = Cvar_Get ( "sv_enforcetime", "1", 0 );

    sv_http_enable = Cvar_Get( "sv_http_enable", "0", CVAR_LATCH );
    sv_http_maxclients = Cvar_Get( "sv_http_maxclients", "32", 0 );
    sv_http_minclients = Cvar_Get( "sv_http_minclients", "8", 0 );

	allow_download = Cvar_Get( "allow_download", "1", CVAR_ARCHIVE );
	allow_download_players = Cvar_Get( "allow_download_players", "0", CVAR_ARCHIVE );
	allow_download_models = Cvar_Get( "allow_download_models", "1", CVAR_ARCHIVE );
	allow_download_sounds = Cvar_Get( "allow_download_sounds", "1", CVAR_ARCHIVE );
	allow_download_maps	= Cvar_Get( "allow_download_maps", "1", CVAR_ARCHIVE );
	allow_download_demos = Cvar_Get( "allow_download_demos", "0", 0 );
	allow_download_other = Cvar_Get( "allow_download_other", "0", 0 );

	sv_noreload = Cvar_Get ("sv_noreload", "0", 0);
	sv_airaccelerate = Cvar_Get("sv_airaccelerate", "0", CVAR_LATCH);
	sv_qwmod = Cvar_Get( "sv_qwmod", "0", CVAR_LATCH ); //atu QWMod
	sv_public = Cvar_Get ("public", "0", 0);
	rcon_password = Cvar_Get( "rcon_password", "", CVAR_PRIVATE );
	sv_password = Cvar_Get( "sv_password", "", CVAR_PRIVATE );
	sv_reserved_password = Cvar_Get( "sv_reserved_password", "", CVAR_PRIVATE );

	sv_debug_send = Cvar_Get( "sv_debug_send", "0", 0 );
	sv_pad_packets = Cvar_Get( "sv_pad_packets", "0", 0 );
	sv_lan_force_rate = Cvar_Get( "sv_lan_force_rate", "0", CVAR_LATCH );

	sv_strafejump_hack = Cvar_Get( "sv_strafejump_hack", "1", CVAR_LATCH );

	sv_bodyque_hack = Cvar_Get( "sv_bodyque_hack", "0", 0 );
#ifndef _WIN32
	sv_oldgame_hack = Cvar_Get( "sv_oldgame_hack", "0", CVAR_LATCH );
#endif

	sv_iplimit = Cvar_Get( "sv_iplimit", "3", 0 );
	sv_nextserver = Cvar_Get( "nextserver", "", 0 );

	sv_status_show = Cvar_Get( "sv_status_show", "2", 0 );

	sv_status_limit = Cvar_Get( "sv_status_limit", "15", 0 );
	sv_status_limit->changed = sv_status_limit_changed;

	sv_badauth_time = Cvar_Get( "sv_badauth_time", "1", 0 );
	sv_badauth_time->changed = sv_badauth_time_changed;

	SV_RateInit( &svs.ratelimit_status, sv_status_limit->integer, 1000 );
	SV_RateInit( &svs.ratelimit_badpass, 1, sv_badauth_time->value * 1000 );
	SV_RateInit( &svs.ratelimit_badrcon, 1, sv_badauth_time->value * 1000 );
}

/*
==================
SV_FinalMessage

Used by SV_Shutdown to send a final message to all
connected clients before the server goes down. The messages are sent
immediately, not just stuck on the outgoing message list, because the
server is going to totally exit after returning from this function.
==================
*/
static void SV_FinalMessage( const char *message, int cmd ) {
	client_t	*client, *next;
    tcpClient_t *t, *tnext;
    netchan_t   *netchan;
    uint16 length;

	MSG_WriteByte( svc_print );
	MSG_WriteByte( PRINT_HIGH );
	MSG_WriteString( message );
	MSG_WriteByte( cmd );

	// send it twice
	// stagger the packets to crutch operating system limited buffers

    FOR_EACH_CLIENT( client ) {
		if( client->state == cs_zombie ) {
			continue;
		}
        netchan = client->netchan;
		while( netchan->fragment_pending ) {
			netchan->TransmitNextFragment( netchan );
		}
		netchan->Transmit( netchan, msg_write.cursize, msg_write.data );
	}

    FOR_EACH_CLIENT( client ) {
		if( client->state == cs_zombie ) {
			continue;
		}
        netchan = client->netchan;
		while( netchan->fragment_pending ) {
			netchan->TransmitNextFragment( netchan );
		}
		netchan->Transmit( netchan, msg_write.cursize, msg_write.data );
	}
	SZ_Clear( &msg_write );

    // send EOF to MVD clients
    length = 0;
    LIST_FOR_EACH( tcpClient_t, t, &svs.mvdClients, mvdEntry ) {
        SV_HttpWrite( t, &length, 2 );
        SV_HttpFinish( t );
        NET_Run( &t->stream );
	}

	// free any data dynamically allocated
    LIST_FOR_EACH_SAFE( client_t, client, next, &svs.clients, entry ) {
		if( client->state != cs_zombie ) {
			SV_CleanClient( client );
		}
		SV_RemoveClient( client );
	}

    LIST_FOR_EACH_SAFE( tcpClient_t, t, tnext, &svs.tcpClients, entry ) {
        SV_HttpDrop( t, NULL );
        SV_HttpRemove( t );
	}

    if( svs.mvdummy ) {
		SV_CleanClient( svs.mvdummy );
		SV_RemoveClient( svs.mvdummy );
        svs.mvdummy = NULL;
    }
}


/*
================
SV_Shutdown

Called when each game quits,
before Sys_Quit or Sys_Error
================
*/
void SV_Shutdown( const char *finalmsg, killtype_t type ) {
	int cmd;

	Cvar_Set( "sv_running", "0" );
	Cvar_Set( "sv_paused", "0" );
    
//	MVD_Disconnect();
    
	if( !svs.initialized ) {
		return;
	}

	SV_MvdRecStop();

    switch( type ) {
    case KILL_RESTART:
        cmd = svc_reconnect;
        break;
    case KILL_DISCONNECT:
        if( !dedicated->integer ) {
            cmd = svc_disconnect;
            break;
        }

        if( mvd_safecmd->string[0] ) {
            Cbuf_AddText( mvd_safecmd->string );
            cmd = svc_reconnect;
        } else {
            cmd = svc_disconnect;
        }
        break;
    default:
        cmd = svc_disconnect;
        break;
    }

    SV_FinalMessage( finalmsg, cmd );

	Master_Shutdown();
	SV_ShutdownGameProgs();

	// free current level
	CM_FreeMap( &sv.cm );
	memset( &sv, 0, sizeof( sv ) );

	// free server static data
	Z_Free( svs.clientpool );
	Z_Free( svs.entityStates );
	Z_Free( svs.players );
#if USE_ZLIB
    deflateEnd( &svs.z );
#endif
	memset( &svs, 0, sizeof( svs ) );

	sv_client = NULL;
	sv_player = NULL;

	Z_LeakTest( TAG_SERVER );
}

