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

pmoveParams_t   sv_pmp;

LIST_DECL( sv_masterlist ); // address of group servers
LIST_DECL( sv_banlist );
LIST_DECL( sv_blacklist );
LIST_DECL( sv_cmdlist_connect );
LIST_DECL( sv_cmdlist_begin );
LIST_DECL( sv_filterlist );

client_t    *sv_client;         // current client

cvar_t  *sv_enforcetime;
cvar_t  *sv_allow_nodelta;
#if USE_FPS
cvar_t  *sv_fps;
#endif

cvar_t  *sv_timeout;            // seconds without any message
cvar_t  *sv_zombietime;         // seconds to sink messages after disconnect
cvar_t  *sv_ghostime;

cvar_t  *sv_password;
cvar_t  *sv_reserved_password;

cvar_t  *sv_force_reconnect;
cvar_t  *sv_show_name_changes;

cvar_t  *sv_airaccelerate;
cvar_t  *sv_qwmod;              // atu QW Physics modificator
cvar_t  *sv_novis;

cvar_t  *sv_maxclients;
cvar_t  *sv_reserved_slots;
cvar_t  *sv_showclamp;
cvar_t  *sv_locked;
cvar_t  *sv_downloadserver;
cvar_t  *sv_redirect_address;

cvar_t  *sv_hostname;
cvar_t  *sv_public;            // should heartbeats be sent

#ifdef _DEBUG
cvar_t  *sv_debug;
cvar_t  *sv_pad_packets;
#endif
cvar_t  *sv_lan_force_rate;
cvar_t  *sv_calcpings_method;
cvar_t  *sv_changemapcmd;

cvar_t  *sv_strafejump_hack;
cvar_t  *sv_waterjump_hack;
#ifndef _WIN32
cvar_t  *sv_oldgame_hack;
#endif
#if USE_PACKETDUP
cvar_t  *sv_packetdup_hack;
#endif
cvar_t  *sv_allow_map;
#if !USE_CLIENT
cvar_t  *sv_recycle;
#endif
cvar_t  *sv_enhanced_setplayer;

cvar_t  *sv_iplimit;
cvar_t  *sv_status_limit;
cvar_t  *sv_status_show;
cvar_t  *sv_uptime;
cvar_t  *sv_auth_limit;
cvar_t  *sv_rcon_limit;

cvar_t  *g_features;

//============================================================================

void SV_RemoveClient( client_t *client ) {
    if( client->msg_pool ) {
        SV_ShutdownClientSend( client );
    }

    if( client->netchan ) {
        Netchan_Close( client->netchan );
        client->netchan = NULL;
    }

    // unlink them from active client list
    List_Remove( &client->entry );

#if USE_MVD_CLIENT
    // unlink them from MVD client list
    if( sv.state == ss_broadcast ) {
        MVD_RemoveClient( client );
    }
#endif

    Com_DPrintf( "Going from cs_zombie to cs_free for %s\n", client->name );

    client->state = cs_free;    // can now be reused
    client->name[0] = 0;
}

void SV_CleanClient( client_t *client ) {
    int i;
#if USE_AC_SERVER
    string_entry_t *bad, *next;

    for( bad = client->ac_bad_files; bad; bad = next ) {
        next = bad->next;
        Z_Free( bad );
    }
    client->ac_bad_files = NULL;
#endif

    // close any existing donwload
    SV_CloseDownload( client );

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

    client->state = cs_zombie;        // become free in a few seconds
    client->lastmessage = svs.realtime;

    if( reason ) {
        if( oldstate == cs_spawned ) {
            // announce to others
#if USE_MVD_CLIENT
            if( sv.state == ss_broadcast ) {
                MVD_GameClientDrop( client->edict, reason );
            } else
#endif
            {
                SV_BroadcastPrintf( PRINT_HIGH, "%s was dropped: %s\n",
                    client->name, reason );
            }
        }

        // print this to client as they will not receive broadcast
        SV_ClientPrintf( client, PRINT_HIGH, "%s was dropped: %s\n",
            client->name, reason );

        // print to server console
        if( Com_IsDedicated() ) {
            Com_Printf( "%s[%s] was dropped: %s\n", client->name,
                NET_AdrToString( &client->netchan->remote_address ), reason );
        }
    }

    // add the disconnect
    MSG_WriteByte( svc_disconnect );
    SV_ClientAddMessage( client, MSG_RELIABLE|MSG_CLEAR );

    if( oldstate == cs_spawned || ( g_features->integer & GMF_WANT_ALL_DISCONNECTS ) ) {
        // call the prog function for removing a client
        // this will remove the body, among other things
        ge->ClientDisconnect( client->edict );
    }

#if USE_AC_SERVER
    AC_ClientDisconnect( client );
#endif

    SV_CleanClient( client );

    Com_DPrintf( "Going to cs_zombie for %s\n", client->name );

#if USE_MVD_SERVER
    // give MVD server a chance to detect if it's dummy client was dropped
    SV_MvdClientDropped( client, reason );
#endif
}


/*
==============================================================================

CONNECTIONLESS COMMANDS

==============================================================================
*/

static qboolean SV_RateLimited( ratelimit_t *r ) {
    if( !r->limit ) {
        return qfalse;
    }
    if( svs.realtime - r->time > r->period ) {
        r->count = 0;
        r->time = svs.realtime;
        return qfalse;
    }

    if( r->count < r->limit ) {
        return qfalse;
    }

    return qtrue;
}

// <limit>[/<period>[sec|min|hour]]
static void SV_RateInit( ratelimit_t *r, const char *s ) {
    unsigned limit;
    unsigned period, scale;
    char *p;

    limit = strtoul( s, &p, 10 );
    if( *p == '/' ) {
        period = strtoul( p + 1, &p, 10 );
        if( *p == 0 || *p == 's' || *p == 'S' ) {
            scale = 1000;
        } else if( *p == 'm' || *p == 'M' ) {
            scale = 60*1000;
        } else if( *p == 'h' || *p == 'H' ) {
            scale = 60*60*1000;
        } else {
            // everything else is milliseconds
            scale = 1;
        }
        if( period > UINT_MAX / scale ) {
            period = UINT_MAX;
        } else {
            if( !period ) {
                period = 1;
            }
            period *= scale;
        }
    } else {
        // default is one second
        period = 1000;
    }

    r->count = 0;
    r->time = svs.realtime;
    r->limit = limit;
    r->period = period;
}

addrmatch_t *SV_MatchAddress( list_t *list, netadr_t *address ) {
    uint32_t addr = BigLong( *( uint32_t * )address->ip );
    addrmatch_t *match;

    LIST_FOR_EACH( addrmatch_t, match, list, entry ) {
        if( ( addr & match->mask ) == ( match->addr & match->mask ) ) {
            match->hits++;
            match->time = time( NULL );
            return match;
        }
    }

    return NULL;
}

/*
===============
SV_StatusString

Builds the string that is sent as heartbeats and status replies.
It is assumed that size of status buffer is at least SV_OUTPUTBUF_LENGTH!
===============
*/
static size_t SV_StatusString( char *status ) {
    char entry[MAX_STRING_CHARS];
    client_t *cl;
    size_t total, len;
    char *tmp = sv_maxclients->string;

    // XXX: ugly hack to hide reserved slots
    if( sv_reserved_slots->integer ) {
        Q_snprintf( entry, sizeof( entry ), "%d",
            sv_maxclients->integer - sv_reserved_slots->integer );
        sv_maxclients->string = entry;
    }

    // add server info
    total = Cvar_BitInfo( status, CVAR_SERVERINFO );

    sv_maxclients->string = tmp;

    // add uptime
    if( sv_uptime->integer > 0 ) {
        if( sv_uptime->integer > 1 ) {
            len = Com_UptimeLong_m( entry, MAX_INFO_VALUE );
        } else {
            len = Com_Uptime_m( entry, MAX_INFO_VALUE );
        }
        if( total + 8 + len < MAX_INFO_STRING ) {
            memcpy( status + total, "\\uptime\\", 8 );
            memcpy( status + total + 8, entry, len );
            total += 8 + len;
        }
    }

    status[total++] = '\n';

    // add player list
    if( sv_status_show->integer > 1 ) {
        FOR_EACH_CLIENT( cl ) {
            if( cl->state == cs_zombie ) {
                continue;
            }
            len = Q_snprintf( entry, sizeof( entry ),
                "%i %i \"%s\"\n",
                cl->edict->client->ps.stats[STAT_FRAGS],
                cl->ping, cl->name );
            if( len >= sizeof( entry ) ) {
                continue;
            }
            if( total + len >= SV_OUTPUTBUF_LENGTH ) {
                break;        // can't hold any more
            }
            memcpy( status + total, entry, len );
            total += len;
        }
    }

    status[total] = 0;

    return total;
}

/*
================
SVC_Status

Responds with all the info that qplug or qspy can see
================
*/
static void SVC_Status( void ) {
    char    buffer[MAX_PACKETLEN_DEFAULT];
    size_t  len;

    if( !sv_status_show->integer ) {
        return;
    }

    if( SV_RateLimited( &svs.ratelimit_status ) ) {
        Com_DPrintf( "Dropping status request from %s\n",
            NET_AdrToString( &net_from ) );
        return;
    }

    svs.ratelimit_status.count++;

    // write the packet header
    memcpy( buffer, "\xff\xff\xff\xffprint\n", 10 );
    len = 10;

    len += SV_StatusString( buffer + len );

    // send the datagram
    NET_SendPacket( NS_SERVER, &net_from, len, buffer );
}

/*
================
SVC_Ack

================
*/
static void SVC_Ack( void ) {
    master_t *m;

    FOR_EACH_MASTER( m ) {
        if( !m->adr.port ) {
            continue;
        }
        if( NET_IsEqualBaseAdr( &m->adr, &net_from ) ) {
            Com_DPrintf( "Ping acknowledge from %s\n",
                NET_AdrToString( &net_from ) );
            m->last_ack = svs.realtime;
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
    char    string[MAX_QPATH];
    size_t  len;
    int     count;
    int     version;

    if (sv_maxclients->integer == 1)
        return;        // ignore in single player

    version = atoi (Cmd_Argv(1));

    if (version != PROTOCOL_VERSION_DEFAULT) {
        return;
    }
    
    count = SV_CountClients();

    len = Q_snprintf (string, sizeof(string),
        "\xff\xff\xff\xffinfo\n%16s %8s %2i/%2i\n",
        sv_hostname->string, sv.name, count, sv_maxclients->integer -
        sv_reserved_slots->integer );
    if( len >= sizeof( string ) ) {
        return;
    }
    
    NET_SendPacket( NS_SERVER, &net_from, len, string );
}

/*
================
SVC_Ping

Just responds with an acknowledgement
================
*/
static void SVC_Ping( void ) {
    OOB_PRINT( NS_SERVER, &net_from, "ack" );
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
    int         i, oldest;
    unsigned    challenge;
    unsigned    oldestTime;

    oldest = 0;
    oldestTime = 0xffffffff;

    // see if we already have a challenge for this ip
    for( i = 0; i < MAX_CHALLENGES; i++ ) {
        if( NET_IsEqualBaseAdr( &net_from, &svs.challenges[i].adr ) )
            break;
        if( svs.challenges[i].time > com_eventTime ) {
            svs.challenges[i].time = com_eventTime;
        }
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
        svs.challenges[oldest].time = com_eventTime;
    } else {
        svs.challenges[i].challenge = challenge;
        svs.challenges[i].time = com_eventTime;
    }

    // send it back
    Netchan_OutOfBand( NS_SERVER, &net_from,
        "challenge %u p=34,35,36", challenge );
}

static void send_redirect_hack( const char *addr ) {
    Netchan_OutOfBand( NS_SERVER, &net_from, "client_connect" );

    MSG_WriteLong( 1 );
    MSG_WriteLong( 0 );
    MSG_WriteByte( svc_print );
    MSG_WriteByte( PRINT_HIGH );
    MSG_WriteString( va( "Server is full.\nRedirecting you to %s...\n", addr ) );
    MSG_WriteByte( svc_stufftext );
    MSG_WriteString( va( "connect %s\n", addr ) );

    NET_SendPacket( NS_SERVER, &net_from, msg_write.cursize, msg_write.data );
    SZ_Clear( &msg_write );
}

#define SV_OobPrintf(...) \
    Netchan_OutOfBand( NS_SERVER, &net_from, "print\n" __VA_ARGS__ )

/*
==================
SVC_DirectConnect

A connection request that did not come from the master
==================
*/
static void SVC_DirectConnect( void ) {
    char        userinfo[MAX_INFO_STRING];
    char        reconnect_var[16];
    char        reconnect_val[16];
    int         i, number, count;
    size_t      length;
    client_t    *cl, *newcl, *lastcl;
    int         protocol, version;
    int         qport;
    int         challenge;
    qboolean    allow;
    char        *info, *s;
    int         maxlength;
    netchan_type_t nctype;
    char        *ncstring, *acstring;
    char        dlstring[MAX_INFO_STRING];
    int         reserved;
    qboolean    has_zlib;

    protocol = atoi( Cmd_Argv( 1 ) );
    qport = atoi( Cmd_Argv( 2 ) ) ;
    challenge = atoi( Cmd_Argv( 3 ) );

    Com_DPrintf( "%s: protocol=%i, qport=%i, challenge=%i\n",
        __func__, protocol, qport, challenge );

    if( protocol < PROTOCOL_VERSION_DEFAULT ||
        protocol > PROTOCOL_VERSION_Q2PRO )
    {
        SV_OobPrintf( "Unsupported protocol version %d.\n", protocol );
        Com_DPrintf( "    rejected connect with protocol %i\n", protocol );
        return;
    }

    if( !NET_IsLocalAddress( &net_from ) ) {
        addrmatch_t *match;

        // see if the challenge is valid
        for( i = 0; i < MAX_CHALLENGES; i++ ) {
            if( !svs.challenges[i].challenge ) {
                continue;
            }
            if( NET_IsEqualBaseAdr( &net_from, &svs.challenges[i].adr ) ) {
                if( svs.challenges[i].challenge == challenge )
                    break;        // good
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

        // check for banned address
        if( ( match = SV_MatchAddress( &sv_banlist, &net_from ) ) != NULL ) {
            s = match->comment;
            if( !*s ) {
                s = "Your IP address is banned from this server.";
            }
            SV_OobPrintf( "%s\nConnection refused.\n", s );
            Com_DPrintf( "   rejected connect from banned IP\n" );
            return;
        }

        if( sv_locked->integer ) {
            SV_OobPrintf( "Server is locked.\n" );
            Com_DPrintf( "    rejected - server is locked.\n" );
            return;
        }

        // limit number of connections from single IP
        if( sv_iplimit->integer > 0 ) {
            count = 0;
            FOR_EACH_CLIENT( cl ) {
                if( NET_IsEqualBaseAdr( &net_from,
                    &cl->netchan->remote_address ) ) 
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
    }

    // set maximum message length
    maxlength = MAX_PACKETLEN_WRITABLE_DEFAULT;
    has_zlib = qfalse;
    if( protocol >= PROTOCOL_VERSION_R1Q2 ) {
        has_zlib = qtrue;
        s = Cmd_Argv( 5 );
        if( *s ) {
            maxlength = atoi( s );
            if( maxlength < 0 || maxlength > MAX_PACKETLEN_WRITABLE ) {
                SV_OobPrintf( "Invalid maximum message length.\n" );
                Com_DPrintf( "    rejected - bad maxmsglen.\n" );
                return;
            }
            if( !maxlength ) {
                maxlength = MAX_PACKETLEN_WRITABLE;
            } else if( maxlength < MIN_PACKETLEN ) {
                maxlength = MIN_PACKETLEN;
            }
        }
    }

    if( !NET_IsLocalAddress( &net_from ) ) {
        // cap maximum message length for real connections
        if( net_maxmsglen->integer > 0 && maxlength > net_maxmsglen->integer ) {
            maxlength = net_maxmsglen->integer;
        }
    }

    if( protocol == PROTOCOL_VERSION_R1Q2 ) {
        // set minor protocol version
        s = Cmd_Argv( 6 );
        if( *s ) {
            version = atoi( s );
            clamp( version, PROTOCOL_VERSION_R1Q2_MINIMUM,
                PROTOCOL_VERSION_R1Q2_CURRENT );
        } else {
            version = PROTOCOL_VERSION_R1Q2_MINIMUM;
        }
        nctype = NETCHAN_OLD;
        ncstring = "";
    } else if( protocol == PROTOCOL_VERSION_Q2PRO ) {
        // set netchan type
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
            has_zlib = qfalse;
        }

        // set minor protocol version
        s = Cmd_Argv( 8 );
        if( *s ) {
            version = atoi( s );
            clamp( version, PROTOCOL_VERSION_Q2PRO_MINIMUM,
                PROTOCOL_VERSION_Q2PRO_CURRENT );
            if( version == PROTOCOL_VERSION_Q2PRO_RESERVED ) {
                version--; // never use this version
            }
        } else {
            version = PROTOCOL_VERSION_Q2PRO_MINIMUM;
        }
    } else {
        nctype = NETCHAN_OLD;
        ncstring = "";
        version = 0;
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
    if( COM_iswhite( s ) ) {
        SV_OobPrintf( "Please set your name before connecting.\n" );
        Com_DPrintf( "    rejected - empty name.\n" );
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
        if( !s[0] ) {
            SV_OobPrintf( "Please set your password before connecting.\n" );
            Com_DPrintf( "    rejected - empty password.\n" );
            return;
        }
        if( SV_RateLimited( &svs.ratelimit_auth ) ) {
            SV_OobPrintf( "Invalid password.\n" );
            Com_DPrintf( "    rejected - auth attempt limit exceeded.\n" );
            return;
        }
        if( strcmp( sv_password->string, s ) ) {
            svs.ratelimit_auth.count++;
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

    Q_strlcpy( userinfo, info, sizeof( userinfo ) );

    // make sure mvdspec key is not set
    Info_RemoveKey( userinfo, "mvdspec" );

    if( sv_password->string[0] || sv_reserved_password->string[0] ) {
        // unset password key to make game mod happy
        Info_RemoveKey( userinfo, "password" );
    }

    // force the IP key/value pair so the game can filter based on ip
    s = NET_AdrToString( &net_from );
    if( !Info_SetValueForKey( userinfo, "ip", s ) ) {
        SV_OobPrintf( "Oversize userinfo string.\n" );
        Com_DPrintf( "    rejected - oversize userinfo.\n" );
        return;
    }

    newcl = NULL;
    reconnect_var[0] = 0;
    reconnect_val[0] = 0;

    // if there is already a slot for this ip, reuse it
    FOR_EACH_CLIENT( cl ) {
        if( NET_IsEqualAdr( &net_from, &cl->netchan->remote_address ) ) {
            if( cl->state == cs_zombie ) {
                strcpy( reconnect_var, cl->reconnect_var );
                strcpy( reconnect_val, cl->reconnect_val );
            } else {
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
        lastcl = svs.udp_client_pool + sv_maxclients->integer - reserved;
        for( newcl = svs.udp_client_pool; newcl < lastcl; newcl++ ) {
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

                // optionally redirect them to a different address
                if( sv_redirect_address->string[0] ) {
                    send_redirect_hack( sv_redirect_address->string );
                }
            }
            return;
        }
    }

    // build a new connection
    // accept the new client
    // this is the only place a client_t is ever initialized
    memset( newcl, 0, sizeof( *newcl ) );
    number = newcl - svs.udp_client_pool;
    newcl->number = newcl->slot = number;
    newcl->challenge = challenge; // save challenge for checksumming
    newcl->protocol = protocol;
    newcl->version = version;
    newcl->has_zlib = has_zlib;
    newcl->edict = EDICT_NUM( number + 1 );
    newcl->gamedir = fs_game->string;
    newcl->mapname = sv.name;
    newcl->configstrings = ( char * )sv.configstrings;
    newcl->pool = ( edict_pool_t * )&ge->edicts;
    newcl->cm = &sv.cm;
    newcl->spawncount = sv.spawncount;
    newcl->maxclients = sv_maxclients->integer;
    strcpy( newcl->reconnect_var, reconnect_var );
    strcpy( newcl->reconnect_val, reconnect_val );

    // copy default pmove parameters
    newcl->pmp = sv_pmp;
    newcl->pmp.airaccelerate = sv_airaccelerate->integer ? qtrue : qfalse;

    // r1q2 extensions
    i = 2;
    if( protocol == PROTOCOL_VERSION_R1Q2 ||
        protocol == PROTOCOL_VERSION_Q2PRO )
    {
        newcl->pmp.speedmult = 2;
        i = 1;
    }
    newcl->pmp.strafehack = sv_strafejump_hack->integer >= i ? qtrue : qfalse;

    if( protocol == PROTOCOL_VERSION_R1Q2 ) {
        if( version >= PROTOCOL_VERSION_R1Q2_LONG_SOLID ) {
            newcl->esFlags |= MSG_ES_LONGSOLID;
        }
    }

    // q2pro extensions
    i = 2;
    if( protocol == PROTOCOL_VERSION_Q2PRO ) {
        if( sv_qwmod->integer ) {
            PmoveEnableQW( &newcl->pmp );
        }
        newcl->pmp.flyhack = qtrue;
        newcl->pmp.flyfriction = 4;
        newcl->esFlags |= MSG_ES_UMASK;
        if( version >= PROTOCOL_VERSION_Q2PRO_LONG_SOLID ) {
            newcl->esFlags |= MSG_ES_LONGSOLID;
        }
        if( version >= PROTOCOL_VERSION_Q2PRO_WATERJUMP_HACK ) {
            i = 1;
        }
    }
    newcl->pmp.waterhack = sv_waterjump_hack->integer >= i ? qtrue : qfalse;

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
    newcl->numpackets = 1;

    // parse some info from the info strings
    Q_strlcpy( newcl->userinfo, userinfo, sizeof( newcl->userinfo ) );
    SV_UserinfoChanged( newcl );

#if USE_AC_SERVER
    if( !sv_force_reconnect->string[0] || reconnect_var[0] ) {
        acstring = AC_ClientConnect( newcl );
    } else
#endif
    {
        acstring = "";
    }

    if( sv_downloadserver->string[0] ) {
        Q_snprintf( dlstring, sizeof( dlstring ), " dlserver=%s",
            sv_downloadserver->string );
    } else {
        dlstring[0] = 0;
    }

    // send the connect packet to the client
    Netchan_OutOfBand( NS_SERVER, &net_from, "client_connect%s%s%s map=%s",
        ncstring, acstring, dlstring, newcl->mapname );

    SV_InitClientSend( newcl );

    if( protocol == PROTOCOL_VERSION_DEFAULT ) {
        newcl->WriteFrame = SV_WriteFrameToClient_Default;
    } else {
        newcl->WriteFrame = SV_WriteFrameToClient_Enhanced;
    }

    // loopback client doesn't need to reconnect
    if( NET_IsLocalAddress( &net_from ) ) {
        newcl->reconnected = qtrue;
    }

    // add them to the linked list of connected clients
    List_SeqAdd( &svs.udp_client_list, &newcl->entry );

    Com_DPrintf( "Going from cs_free to cs_assigned for %s\n", newcl->name );
    newcl->state = cs_assigned;
    newcl->lastframe = -1;
    newcl->lastmessage = svs.realtime;    // don't timeout
    newcl->min_ping = 9999;
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
    int i;
    char *string;

    if( SV_RateLimited( &svs.ratelimit_rcon ) ) {
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
        svs.ratelimit_rcon.count++;
        return;
    }

    Com_Printf( "Rcon from %s:\n%s\n",
        NET_AdrToString( &net_from ), string );

    SV_BeginRedirect( RD_PACKET );

    Cmd_ExecuteString( &cmd_buffer, string );

    Com_EndRedirect();
}

static const ucmd_t svcmds[] = {
    { "ping",           SVC_Ping          },
    { "ack",            SVC_Ack           },
    { "status",         SVC_Status        },
    { "info",           SVC_Info          },
    { "getchallenge",   SVC_GetChallenge  },
    { "connect",        SVC_DirectConnect },
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
    char    string[MAX_STRING_CHARS];
    char    *c;
    int     i;
    size_t  len;

    if( SV_MatchAddress( &sv_blacklist, &net_from ) ) {
        Com_DPrintf( "ignored blackholed connectionless packet\n" );
        return;
    }

    MSG_BeginReading();
    MSG_ReadLong();        // skip the -1 marker

    len = MSG_ReadStringLine( string, sizeof( string ) );
    if( len >= sizeof( string ) ) {
        Com_DPrintf( "ignored oversize connectionless packet\n" );
        return;
    }

    Cmd_TokenizeString( string, qfalse );

    c = Cmd_Argv( 0 );
    Com_DPrintf( "ServerPacket[%s]: %s\n", NET_AdrToString( &net_from ), c );

    if( !NET_IsLocalAddress( &net_from ) && net_from.ip[0] == 127 &&
        net_from.port == Cvar_VariableInteger( "net_port" ) )
    {
        Com_DPrintf( "dropped connectionless packet from self\n" );
        return;
    }

    if( !strcmp( c, "rcon" ) ) {
        SVC_RemoteCommand();
        return; // accept rcon commands even if not active
    }

    if( !svs.initialized ) {
        Com_DPrintf( "ignored connectionless packet\n" );
        return;
    }

    for( i = 0; svcmds[i].name; i++ ) {
        if( !strcmp( c, svcmds[i].name ) ) {
            svcmds[i].func();
            return;
        }
    }

    Com_DPrintf( "bad connectionless packet\n" );
}


//============================================================================

int SV_CountClients( void ) {
    client_t *cl;
    int count = 0;

    FOR_EACH_CLIENT( cl ) {
        if( cl->state > cs_zombie ) {
            count++;
        }
    }
    return count;
}

static int ping_nop( client_t *cl ) {
    return 0;
}

static int ping_min( client_t *cl ) {
    int j, count = 9999;

    for( j = 0; j < LATENCY_COUNTS; j++ ) {
        if( cl->frame_latency[j] > 0 ) {
            if( count > cl->frame_latency[j] ) {
                count = cl->frame_latency[j];
            }
        }
    }
    return count == 9999 ? 0 : count;
}

static int ping_avg( client_t *cl ) {
    int j, total = 0, count = 0;

    for( j = 0; j < LATENCY_COUNTS; j++ ) {
        if( cl->frame_latency[j] > 0 ) {
            count++;
            total += cl->frame_latency[j];
        }
    }
    return count ? total / count : 0;
}

/*
===================
SV_CalcPings

Updates the cl->ping variables
===================
*/
static void SV_CalcPings( void ) {
    client_t    *cl;
    int         (*calc)( client_t * );
    int         res;

    switch( sv_calcpings_method->integer ) {
        case 0:  calc = ping_nop; break;
        case 2:  calc = ping_min; break;
        default: calc = ping_avg; break;
    }

    // update avg ping every 10 seconds
    res = sv.framenum % 100;

    FOR_EACH_CLIENT( cl ) {
        if( cl->state == cs_spawned ) {
            cl->ping = calc( cl );
            if( cl->ping ) {
                if( cl->ping < cl->min_ping ) {
                    cl->min_ping = cl->ping;
                } else if( cl->ping > cl->max_ping ) {
                    cl->max_ping = cl->ping;
                }
                if( !res ) {
                    cl->avg_ping_time += cl->ping;
                    cl->avg_ping_count++;
                }
            }
        } else {
            cl->ping = 0;
        }

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
    client_t    *cl;

    if( sv.framenum & 15 )
        return;

    FOR_EACH_CLIENT( cl ) {
        cl->commandMsec = 18 * SV_FRAMETIME;        // 1600 + some slop
    }

    if( sv.framenum & 63 )
        return;

    FOR_EACH_CLIENT( cl ) {
        cl->fps = ( cl->numMoves * SV_FPS ) >> 6;
        cl->numMoves = 0;
    }
}


/*
=================
SV_PacketEvent
=================
*/
static void SV_PacketEvent( void ) {
    client_t    *client;
    netchan_t   *netchan;
    int         qport;
        
    // check for connectionless packet (0xffffffff) first
    // connectionless packets are processed even if the server is down
    if( *( int * )msg_read.data == -1 ) {
        SV_ConnectionlessPacket();
        return;
    }

    if( !svs.initialized ) {
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
        } else {
            if( netchan->remote_address.port != net_from.port ) {
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
                //if( client->state != cs_assigned ) {
                    client->lastmessage = svs.realtime;    // don't timeout
                //}
#if USE_ICMP
                client->unreachable = qfalse; // don't drop
#endif
                SV_ExecuteClientMessage( client );
            }
        }

        break;
    }
}

#if USE_ICMP
/*
=================
SV_ErrorEvent
=================
*/
void SV_ErrorEvent( int info ) {
    client_t    *client;
    netchan_t   *netchan;

    if( !svs.initialized ) {
        return;
    }

    // check for errors from connected clients
    FOR_EACH_CLIENT( client ) {
        if( client->state == cs_zombie ) {
            continue; // already a zombie
        }
        netchan = client->netchan;
        if( !NET_IsEqualBaseAdr( &net_from, &netchan->remote_address ) ) {
            continue;
        }
        if( net_from.port && netchan->remote_address.port != net_from.port ) {
            continue;
        }
#if USE_PMTUDISC
        if( info ) {
            // we are doing path MTU discovery and got ICMP fragmentation-needed
            // update MTU only for connecting clients to minimize spoofed ICMP interference
            // MTU info has already been sanity checked for us by network code
            // assume total 64 bytes of headers
            if( client->state == cs_primed &&
                netchan->reliable_length &&
                info < netchan->maxpacketlen + 64 )
            {
                if( netchan->type == NETCHAN_OLD ) {
                    // TODO: old clients require entire queue flush :(
                    continue;
                }
                Com_Printf( "Fixing up maxmsglen for %s: %d --> %d\n",
                    client->name, (int)netchan->maxpacketlen, info - 64 );
                netchan->maxpacketlen = info - 64;
            }
            continue;
        }
#endif
        client->unreachable = qtrue; // drop them soon
        break;
    }
}
#endif

static void SV_ReadPackets( void ) {
#if USE_CLIENT
    memset( &net_from, 0, sizeof( net_from ) );
    net_from.type = NA_LOOPBACK;

    // process loopback packets
    while( NET_GetLoopPacket( NS_SERVER ) ) {
        SV_PacketEvent();
    }
#endif

    // process network packets
    while( NET_GetPacket( NS_SERVER ) ) {
        SV_PacketEvent();
    }
}


/*
==================
SV_CheckTimeouts

If a packet has not been received from a client for timeout->value
seconds, drop the conneciton.

When a client is normally dropped, the client_t goes into a zombie state
for a few seconds to make sure any final reliable message gets resent
if necessary
==================
*/
static void SV_CheckTimeouts( void ) {
    client_t    *client;
    unsigned    zombie_time = 1000 * sv_zombietime->value;
    unsigned    drop_time   = 1000 * sv_timeout->value;
    unsigned    ghost_time  = 1000 * sv_ghostime->value;
    unsigned    delta;

    FOR_EACH_CLIENT( client ) {
        // never timeout local clients
        if( NET_IsLocalAddress( &client->netchan->remote_address ) ) {
            continue;
        }
        // NOTE: delta calculated this way is not sensitive to overflow
        delta = svs.realtime - client->lastmessage;
        if( client->state == cs_zombie ) {
            if( delta > zombie_time ) {
                SV_RemoveClient( client );
            }
            continue;
        }
        if( client->drop_hack ) {
            SV_DropClient( client, NULL );
            continue;
        }
#if USE_ICMP
        if( client->unreachable ) {
            if( delta > ghost_time ) {
                SV_DropClient( client, "connection reset by peer" );
                SV_RemoveClient( client );    // don't bother with zombie state
                continue;
            }
        }
#endif
        if( delta > drop_time || ( client->state == cs_assigned && delta > ghost_time ) ) {
            SV_DropClient( client, "connection timed out" );
            SV_RemoveClient( client );    // don't bother with zombie state
            continue;
        }

        if( client->frames_nodelta > 64 && !sv_allow_nodelta->integer ) {
            SV_DropClient( client, "too many nodelta frames" );
            continue;
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
    edict_t    *ent;
    int        i;

#if USE_MVD_CLIENT
    if( sv.state == ss_broadcast ) {
        MVD_PrepWorldFrame();
        return;
    }
#endif

    for( i = 1; i < ge->num_edicts; i++, ent++ ) {
        ent = EDICT_NUM( i );
        // events only last for a single message
        ent->s.event = 0;
    }    

    sv.tracecount = 0;
}

#if USE_CLIENT
static inline qboolean check_paused( void ) {
    if( !dedicated->integer && cl_paused->integer &&
        List_Count( &svs.udp_client_list ) == 1  )
    {
        if( !sv_paused->integer ) {
            Cvar_Set( "sv_paused", "1" );
            IN_Activate();
        }
        return qtrue; // don't run if paused
    }

    if( sv_paused->integer ) {
        Cvar_Set( "sv_paused", "0" );
        IN_Activate();
    }
    return qfalse;

}
#endif


/*
=================
SV_RunGameFrame
=================
*/
static void SV_RunGameFrame( void ) {
    // we always need to bump framenum, even if we
    // don't run the world, otherwise the delta
    // compression can get confused when a client
    // has the "current" frame
    sv.framenum++;

#if USE_CLIENT
    // pause if there is only local client on the server
    if( check_paused() ) {
        return;
    }
#endif

#if USE_MVD_SERVER
    // save the entire world state if recording a serverdemo
    SV_MvdBeginFrame();
#endif

#if USE_CLIENT
    if( host_speeds->integer )
        time_before_game = Sys_Milliseconds();
#endif

    ge->RunFrame();

#if USE_CLIENT
    if( host_speeds->integer )
        time_after_game = Sys_Milliseconds();
#endif

    if( msg_write.cursize ) {
        Com_WPrintf( "Game DLL left %"PRIz" bytes "
            "in multicast buffer, cleared.\n",
            msg_write.cursize );
        SZ_Clear( &msg_write );
    }

#if USE_MVD_SERVER
    // save the entire world state if recording a serverdemo
    SV_MvdEndFrame();
#endif

}

/*
================
SV_MasterHeartbeat

Send a message to the master every few minutes to
let it know we are alive, and log information
================
*/
static void SV_MasterHeartbeat( void ) {
    char    buffer[MAX_PACKETLEN_DEFAULT];
    size_t  len;
    master_t *m;

    if( !Com_IsDedicated() )
        return;        // only dedicated servers send heartbeats

    if( !sv_public->integer )
        return;        // a private dedicated game

    if( svs.realtime - svs.last_heartbeat < HEARTBEAT_SECONDS*1000 )
        return;        // not time to send yet

    svs.last_heartbeat = svs.realtime;

    // write the packet header
    memcpy( buffer, "\xff\xff\xff\xffheartbeat\n", 14 );
    len = 14;

    // send the same string that we would give for a status OOB command
    len += SV_StatusString( buffer + len );

    // send to group master
    FOR_EACH_MASTER( m ) {
        if( m->adr.port ) {
            Com_DPrintf( "Sending heartbeat to %s\n",
                NET_AdrToString( &m->adr ) );
            NET_SendPacket( NS_SERVER, &m->adr, len, buffer );
        }
    }
}

/*
=================
SV_MasterShutdown

Informs all masters that this server is going down
=================
*/
static void SV_MasterShutdown( void ) {
    master_t *m;

    if( !Com_IsDedicated() )
        return;        // only dedicated servers send heartbeats

    if( !sv_public->integer )
        return;        // a private dedicated game

    // send to group master
    FOR_EACH_MASTER( m ) {
        if( m->adr.port ) {
            Com_DPrintf( "Sending shutdown to %s\n",
                NET_AdrToString( &m->adr ) );
            OOB_PRINT( NS_SERVER, &m->adr, "shutdown" );
        }
    }
}

/*
==================
SV_Frame

Some things like MVD client connections and command buffer
processing are run even when server is not yet initalized.

Returns amount of extra frametime available for sleeping on IO.
==================
*/
unsigned SV_Frame( unsigned msec ) {
#if USE_CLIENT
    time_before_game = time_after_game = 0;
#endif

    // advance local server time
    svs.realtime += msec;

#if USE_MVD_CLIENT
    // run connections to MVD/GTV servers
    MVD_Frame();
#endif

    // read packets from UDP clients
    SV_ReadPackets();

    if( svs.initialized ) {
#if USE_AC_SERVER
        // run connection to the anticheat server
        AC_Run();
#endif

#if USE_MVD_SERVER
        // run connections from MVD/GTV clients
        SV_MvdRunClients();
#endif

        // deliver fragments and reliable messages for connecting clients
        SV_SendAsyncPackets();
    }

    // move autonomous things around if enough time has passed
    sv.frameresidual += msec;
    if( sv.frameresidual < SV_FRAMETIME ) {
        return SV_FRAMETIME - sv.frameresidual;
    }

    if( svs.initialized ) {
        // check timeouts
        SV_CheckTimeouts();

        // update ping based on the last known frame from all clients
        SV_CalcPings();

        // give the clients some timeslices
        SV_GiveMsec();

        // let everything in the world think and move
        SV_RunGameFrame();

        // send messages back to the UDP clients
        SV_SendClientMessages();

        // send a heartbeat to the master if needed
        SV_MasterHeartbeat();

        // clear teleport flags, etc for next frame
        SV_PrepWorldFrame();
    }

    if( Com_IsDedicated() ) {
        // run cmd buffer in dedicated mode
        if( cmd_buffer.waitCount > 0 ) {
            cmd_buffer.waitCount--;
        }
    }

    // decide how long to sleep next frame
    sv.frameresidual -= SV_FRAMETIME;
    if( sv.frameresidual < SV_FRAMETIME ) {
        return SV_FRAMETIME - sv.frameresidual;
    }

    // don't accumulate bogus residual
    if( sv.frameresidual > 250 ) {
        Com_DPrintf( "Reset residual %u\n", sv.frameresidual );
        sv.frameresidual = 100;
    }

    return 0;
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
    size_t len;

    if( !userinfo[0] ) {
        SV_DropClient( sv_client, "empty userinfo" );
        return;
    }
    
    if( !Info_Validate( userinfo ) ) {
        SV_DropClient( sv_client, "malformed userinfo" );
        return;
    }

    s = Info_ValueForKey( userinfo, "name" );
    if( COM_iswhite( s ) ) {
        if( sv_client->name[0] ) {
            SV_ClientCommand( sv_client, "set name \"%s\"\n", sv_client->name );
        } else {
            SV_DropClient( sv_client, "malformed name" );
        }
        return;
    }

    s = Info_ValueForKey( userinfo, "skin" );
    len = strlen( s );
    if( !Q_ispath( s[0] ) || !Q_ispath( s[ len - 1 ] ) || strchr( s, '.' ) ) {
        SV_ClientCommand( sv_client, "set skin \"male/grunt\"\n" );
        return;
    }

    // force the IP key/value pair so the game can filter based on ip
    s = NET_AdrToString( &sv_client->netchan->remote_address );
    if( !Info_SetValueForKey( userinfo, "ip", s ) ) {
        SV_DropClient( sv_client, "oversize userinfo" );
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
    char    name[MAX_CLIENT_NAME];
    char    *val;
    size_t  len;
    int     i;

    // call prog code to allow overrides
    ge->ClientUserinfoChanged( cl->edict, cl->userinfo );

    // name for C code
    val = Info_ValueForKey( cl->userinfo, "name" );
    len = Q_strlcpy( name, val, sizeof( name ) );
    if( len >= sizeof( name ) ) {
        len = sizeof( name ) - 1;
    }
    // mask off high bit
    for( i = 0; i < len; i++ )
        name[i] &= 127;
    if( cl->name[0] && strcmp( cl->name, name ) ) {
        if( Com_IsDedicated() ) {
            Com_Printf( "%s[%s] changed name to %s\n", cl->name,
                NET_AdrToString( &cl->netchan->remote_address ), name );
        }
#if USE_MVD_CLIENT
        if( sv.state == ss_broadcast ) {
            MVD_GameClientNameChanged( cl->edict, name );
        } else
#endif
        if( sv_show_name_changes->integer ) {
            SV_BroadcastPrintf( PRINT_HIGH, "%s changed name to %s\n",
                cl->name, name );
        }
    }
    memcpy( cl->name, name, len + 1 );

    // rate command
    val = Info_ValueForKey( cl->userinfo, "rate" );
    if( *val ) {
        cl->rate = atoi( val );
        clamp( cl->rate, 100, 15000 );
    } else {
        cl->rate = 5000;
    }

    // never drop over the loopback
    if( NET_IsLocalAddress( &cl->netchan->remote_address ) ) {
        cl->rate = 0;
    }

    // don't drop over LAN connections
    if( sv_lan_force_rate->integer &&
        NET_IsLanAddress( &cl->netchan->remote_address ) )
    {
        cl->rate = 0;
    }

    // msg command
    val = Info_ValueForKey( cl->userinfo, "msg" );
    if( *val ) {
        cl->messagelevel = atoi( val );
        clamp( cl->messagelevel, PRINT_LOW, PRINT_CHAT + 1 );
    }
}


//============================================================================

#if USE_SYSCON
void SV_SetConsoleTitle( void ) {
    char buffer[MAX_STRING_CHARS];

    Q_snprintf( buffer, sizeof( buffer ), "%s (port %d%s)",
        sv_hostname->string, net_port->integer,
        sv_running->integer ? "" : ", down" );

    Sys_SetConsoleTitle( buffer );
}
#endif

static void sv_status_limit_changed( cvar_t *self ) {
    SV_RateInit( &svs.ratelimit_status, self->string );
}
static void sv_auth_limit_changed( cvar_t *self ) {
    SV_RateInit( &svs.ratelimit_auth, self->string );
}
static void sv_rcon_limit_changed( cvar_t *self ) {
    SV_RateInit( &svs.ratelimit_rcon, self->string );
}
static void init_rate_limits( void ) {
    SV_RateInit( &svs.ratelimit_status, sv_status_limit->string );
    SV_RateInit( &svs.ratelimit_auth, sv_auth_limit->string );
    SV_RateInit( &svs.ratelimit_rcon, sv_rcon_limit->string );
}

#if USE_SYSCON
static void sv_hostname_changed( cvar_t *self ) {
    SV_SetConsoleTitle();
}
#endif

#if USE_ZLIB
voidpf SV_Zalloc OF(( voidpf opaque, uInt items, uInt size )) {
    return SV_Malloc( items * size );
}

void SV_Zfree OF(( voidpf opaque, voidpf address )) {
    Z_Free( address );
}
#endif

/*
===============
SV_Init

Only called at quake2.exe startup, not for each game
===============
*/
void SV_Init( void ) {
    SV_InitOperatorCommands();

#if USE_MVD_SERVER
    SV_MvdRegister();
#endif

#if USE_MVD_CLIENT
    MVD_Register();
#endif

#if USE_AC_SERVER
    AC_Register();
#endif

    Cvar_Get( "protocol", va( "%i", PROTOCOL_VERSION_DEFAULT ), CVAR_SERVERINFO|CVAR_ROM );
    
    Cvar_Get( "skill", "1", CVAR_LATCH );
    Cvar_Get( "deathmatch", "1", CVAR_SERVERINFO|CVAR_LATCH );
    Cvar_Get( "coop", "0", /*CVAR_SERVERINFO|*/CVAR_LATCH );
    Cvar_Get( "cheats", "0", CVAR_SERVERINFO|CVAR_LATCH );
    Cvar_Get( "dmflags", va( "%i", DF_INSTANT_ITEMS ), CVAR_SERVERINFO );
    Cvar_Get( "fraglimit", "0", CVAR_SERVERINFO );
    Cvar_Get( "timelimit", "0", CVAR_SERVERINFO );

    sv_maxclients = Cvar_Get( "maxclients", "8", CVAR_SERVERINFO|CVAR_LATCH );
    sv_reserved_slots = Cvar_Get( "sv_reserved_slots", "0", CVAR_LATCH );
    sv_hostname = Cvar_Get( "hostname", "noname", CVAR_SERVERINFO|CVAR_ARCHIVE );
#if USE_SYSCON
    sv_hostname->changed = sv_hostname_changed;
#endif
    sv_timeout = Cvar_Get( "timeout", "90", 0 );
    sv_zombietime = Cvar_Get( "zombietime", "2", 0 );
    sv_ghostime = Cvar_Get( "sv_ghostime", "6", 0 );
    sv_showclamp = Cvar_Get( "showclamp", "0", 0 );
    sv_enforcetime = Cvar_Get( "sv_enforcetime", "1", 0 );
    sv_allow_nodelta = Cvar_Get( "sv_allow_nodelta", "1", 0 );
#if USE_FPS
    sv_fps = Cvar_Get( "sv_fps", "10", CVAR_LATCH );
#endif
    sv_force_reconnect = Cvar_Get( "sv_force_reconnect", "", CVAR_LATCH );
    sv_show_name_changes = Cvar_Get( "sv_show_name_changes", "0", 0 );

    sv_airaccelerate = Cvar_Get("sv_airaccelerate", "0", CVAR_LATCH);
    sv_qwmod = Cvar_Get( "sv_qwmod", "0", CVAR_LATCH ); //atu QWMod
    sv_public = Cvar_Get( "public", "0", CVAR_LATCH );
    sv_password = Cvar_Get( "sv_password", "", CVAR_PRIVATE );
    sv_reserved_password = Cvar_Get( "sv_reserved_password", "", CVAR_PRIVATE );
    sv_locked = Cvar_Get( "sv_locked", "0", 0 );
    sv_novis = Cvar_Get ("sv_novis", "0", 0);
    sv_downloadserver = Cvar_Get( "sv_downloadserver", "", 0 );
    sv_redirect_address = Cvar_Get( "sv_redirect_address", "", 0 );

#ifdef _DEBUG
    sv_debug = Cvar_Get( "sv_debug", "0", 0 );
    sv_pad_packets = Cvar_Get( "sv_pad_packets", "0", 0 );
#endif
    sv_lan_force_rate = Cvar_Get( "sv_lan_force_rate", "0", CVAR_LATCH );
    sv_calcpings_method = Cvar_Get( "sv_calcpings_method", "1", 0 );
    sv_changemapcmd = Cvar_Get( "sv_changemapcmd", "", 0 );

    sv_strafejump_hack = Cvar_Get( "sv_strafejump_hack", "1", CVAR_LATCH );
    sv_waterjump_hack = Cvar_Get( "sv_waterjump_hack", "0", CVAR_LATCH );

#ifndef _WIN32
    sv_oldgame_hack = Cvar_Get( "sv_oldgame_hack", "0", CVAR_LATCH );
#endif
#if USE_PACKETDUP
    sv_packetdup_hack = Cvar_Get( "sv_packetdup_hack", "0", 0 );
#endif

    sv_allow_map = Cvar_Get( "sv_allow_map", "0", 0 );

#if !USE_CLIENT
    sv_recycle = Cvar_Get( "sv_recycle", "0", 0 );
#endif

    sv_enhanced_setplayer = Cvar_Get( "sv_enhanced_setplayer", "0", 0 );

    sv_iplimit = Cvar_Get( "sv_iplimit", "3", 0 );

    sv_status_show = Cvar_Get( "sv_status_show", "2", 0 );

    sv_status_limit = Cvar_Get( "sv_status_limit", "15", 0 );
    sv_status_limit->changed = sv_status_limit_changed;

    sv_uptime = Cvar_Get( "sv_uptime", "0", 0 );

    sv_auth_limit = Cvar_Get( "sv_auth_limit", "1", 0 );
    sv_auth_limit->changed = sv_auth_limit_changed;

    sv_rcon_limit = Cvar_Get( "sv_rcon_limit", "1", 0 );
    sv_rcon_limit->changed = sv_rcon_limit_changed;

    Cvar_Get( "sv_features", va( "%d", SV_FEATURES ), CVAR_ROM );
    g_features = Cvar_Get( "g_features", "0", CVAR_ROM );

    init_rate_limits();

    // set up default pmove parameters
    PmoveInit( &sv_pmp );

#if USE_SYSCON
    SV_SetConsoleTitle();
#endif
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
    client_t    *client;
    netchan_t   *netchan;
    int         i;

    MSG_WriteByte( svc_print );
    MSG_WriteByte( PRINT_HIGH );
    MSG_WriteString( message );
    MSG_WriteByte( cmd );

    // send it twice
    // stagger the packets to crutch operating system limited buffers
    for( i = 0; i < 2; i++ ) {
        FOR_EACH_CLIENT( client ) {
            if( client->state == cs_zombie ) {
                continue;
            }
            netchan = client->netchan;
            while( netchan->fragment_pending ) {
                netchan->TransmitNextFragment( netchan );
            }
            netchan->Transmit( netchan, msg_write.cursize, msg_write.data, 1 );
        }
    }

    SZ_Clear( &msg_write );

    // free any data dynamically allocated
    FOR_EACH_CLIENT( client ) {
        if( client->state != cs_zombie ) {
            SV_CleanClient( client );
        }
        SV_RemoveClient( client );
    }
}


/*
================
SV_Shutdown

Called when each game quits, from Com_Quit or Com_Error
================
*/
void SV_Shutdown( const char *finalmsg, killtype_t type ) {
    Cvar_Set( "sv_running", "0" );
    Cvar_Set( "sv_paused", "0" );

    if( !svs.initialized ) {
#if USE_MVD_CLIENT
        MVD_Shutdown(); // make sure MVD client is down
#endif
        return;
    }

#if USE_AC_SERVER
    AC_Disconnect();
#endif

#if USE_MVD_SERVER
    // shutdown MVD server
    SV_MvdShutdown( type );
#endif

    if( type == KILL_RESTART ) {
        SV_FinalMessage( finalmsg, svc_reconnect );
    } else {
        SV_FinalMessage( finalmsg, svc_disconnect );
    }

    SV_MasterShutdown();
    SV_ShutdownGameProgs();

    // free current level
    CM_FreeMap( &sv.cm );
    memset( &sv, 0, sizeof( sv ) );

    // free server static data
    Z_Free( svs.udp_client_pool );
    Z_Free( svs.entityStates );
#if USE_ZLIB
    deflateEnd( &svs.z );
#endif
    memset( &svs, 0, sizeof( svs ) );

    // reset rate limits
    init_rate_limits();

    sv_client = NULL;
    sv_player = NULL;

#if USE_SYSCON
    SV_SetConsoleTitle();
#endif

    Z_LeakTest( TAG_SERVER );
}

