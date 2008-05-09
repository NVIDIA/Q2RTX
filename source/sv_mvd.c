/*
Copyright (C) 2003-2006 Andrey Nazarov

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
// sv_mvd.c - MVD server and local recorder
//

#include "sv_local.h"
#include "mvd_local.h"

cvar_t	*sv_mvd_enable;
cvar_t  *sv_mvd_auth;
cvar_t	*sv_mvd_wait;
cvar_t	*sv_mvd_noblend;
cvar_t	*sv_mvd_nogun;
cvar_t	*sv_mvd_max_size;
cvar_t	*sv_mvd_max_duration;
cvar_t	*sv_mvd_begincmd;
cvar_t	*sv_mvd_scorecmd;
#if USE_ZLIB
cvar_t	*sv_mvd_encoding;
#endif
cvar_t	*sv_mvd_capture_flags;

static cmdbuf_t	dummy_buffer;
static char		dummy_buffer_text[MAX_STRING_CHARS];

/*
==================
SV_MvdPlayerIsActive

Attempts to determine if the given player entity is active,
e.g. the given player state is to be captured into MVD stream.

Ideally a compatible game DLL should provide us with information
whether given player is to be captured, instead of relying on
this stupid and complex hack.
==================
*/
qboolean SV_MvdPlayerIsActive( edict_t *ent ) {
    int num;

	if( !ent->inuse ) {
		return qfalse;
	}

    // not a client at all?
	if( !ent->client ) {
		return qfalse;
	}

    num = NUM_FOR_EDICT( ent ) - 1;
    if( num < 0 || num >= sv_maxclients->integer ) {
        return qfalse;
    }

    // check if client is actually connected (default)
    if( sv_mvd_capture_flags->integer & 1 ) {
        if( svs.udp_client_pool[num].state != cs_spawned ) {
            return qfalse;
        }
    }

	// first of all, make sure player_state_t is valid
	if( !ent->client->ps.fov ) {
		return qfalse;
	}

    // always capture dummy MVD client
    if( svs.mvd.dummy && ent == svs.mvd.dummy->edict ) {
        return qtrue;
    }

    // never capture spectators
    if( ent->client->ps.pmove.pm_type == PM_SPECTATOR ) {
        return qfalse;
    }

    // check entity visibility
    if( ( ent->svflags & SVF_NOCLIENT ) || !ES_INUSE( &ent->s ) ) {
        // never capture invisible entities
        if( sv_mvd_capture_flags->integer & 2 ) {
            return qfalse;
        }
    } else {
        // always capture visible entities (default)
        if( sv_mvd_capture_flags->integer & 4 ) {
            return qtrue;
        }
    }

	// they are likely following somene in case of PM_FREEZE
	if( ent->client->ps.pmove.pm_type == PM_FREEZE ) {
        return qfalse;
	}

	// they are likely following someone if PMF_NO_PREDICTION is set 
    if( ent->client->ps.pmove.pm_flags & PMF_NO_PREDICTION ) {
        return qfalse;
    }

	return qtrue;
}

static void SV_MvdCopyEntity( entity_state_t *dst, entity_state_t *src, int flags ) {
    if( !( flags & MSG_ES_FIRSTPERSON ) ) {
		VectorCopy( src->origin, dst->origin );
		VectorCopy( src->angles, dst->angles );
		VectorCopy( src->old_origin, dst->old_origin );
    }
    dst->modelindex = src->modelindex;
    dst->modelindex2 = src->modelindex2;
    dst->modelindex3 = src->modelindex3;
    dst->modelindex4 = src->modelindex4;
    dst->frame = src->frame;
    dst->skinnum = src->skinnum;
    dst->effects = src->effects;
    dst->renderfx = src->renderfx;
    dst->solid = src->solid;
    dst->sound = src->sound;
    dst->event = 0;
}

/*
==================
SV_MvdBuildFrame

Builds new MVD frame by capturing all entity and player states
and calculating portalbits. The same frame is used for all MVD
clients, as well as local recorder.
==================
*/
static void SV_MvdEmitFrame( void ) {
	player_state_t *oldps, *newps;
	entity_state_t *oldes, *newes;
	edict_t *ent;
	int flags, portalbytes;
    byte portalbits[MAX_MAP_AREAS/8];
	int i;

	MSG_WriteByte( mvd_frame );

	portalbytes = CM_WritePortalBits( &sv.cm, portalbits );
	MSG_WriteByte( portalbytes );
	MSG_WriteData( portalbits, portalbytes );
	
	flags = MSG_PS_IGNORE_PREDICTION|MSG_PS_IGNORE_DELTAANGLES;
	if( sv_mvd_noblend->integer ) {
		flags |= MSG_PS_IGNORE_BLEND;
	}
	if( sv_mvd_nogun->integer ) {
		flags |= MSG_PS_IGNORE_GUNINDEX|MSG_PS_IGNORE_GUNFRAMES;
	}

	for( i = 0; i < sv_maxclients->integer; i++ ) {
	    ent = EDICT_NUM( i + 1 );

        oldps = &svs.mvd.players[i];
        newps = &ent->client->ps;

    	if( !SV_MvdPlayerIsActive( ent ) ) {
            if( PPS_INUSE( oldps ) ) {
			    // the old player isn't present in the new message
			    MSG_WriteDeltaPlayerstate_Packet( NULL, NULL, i, flags );
                PPS_INUSE( oldps ) = qfalse;
            }
            continue;
        }

		if( PPS_INUSE( oldps ) ) {
			// delta update from old position
			// because the force parm is false, this will not result
			// in any bytes being emited if the player has not changed at all
			MSG_WriteDeltaPlayerstate_Packet( oldps, newps, i, flags );
        } else {
			// this is a new player, send it from the last state
			MSG_WriteDeltaPlayerstate_Packet( oldps, newps, i,
                flags | MSG_PS_FORCE );
		}

        *oldps = *newps;
        PPS_INUSE( oldps ) = qtrue;
	}

	MSG_WriteByte( CLIENTNUM_NONE );	// end of packetplayers

	for( i = 1; i < ge->num_edicts; i++ ) {
		ent = EDICT_NUM( i );

        oldes = &svs.mvd.entities[i];
        newes = &ent->s;

		if( ( ent->svflags & SVF_NOCLIENT ) || !ES_INUSE( newes ) ) {
            if( oldes->number ) {
			    // the old entity isn't present in the new message
			    MSG_WriteDeltaEntity( oldes, NULL, MSG_ES_FORCE );
                oldes->number = 0;
            }
            continue;
        }

        flags = 0;
        if( i <= sv_maxclients->integer ) {
            oldps = &svs.mvd.players[ i - 1 ];
            if( PPS_INUSE( oldps ) && oldps->pmove.pm_type == PM_NORMAL ) {
                flags |= MSG_ES_FIRSTPERSON;
            }
        }
        
        if( !oldes->number ) {
			// this is a new entity, send it from the last state
			flags |= MSG_ES_FORCE|MSG_ES_NEWENTITY;
        }
		
        MSG_WriteDeltaEntity( oldes, newes, flags );

        SV_MvdCopyEntity( oldes, newes, flags );
        oldes->number = i;
	}

	MSG_WriteShort( 0 );	// end of packetentities
}

static void SV_DummyWait_f( void ) {
    int count = atoi( Cmd_Argv( 1 ) );

    if( count < 1 ) {
        count = 1;
    }
	dummy_buffer.waitCount = count;
}

static void SV_DummyForward_f( void ) {
    Cmd_Shift();
    Com_DPrintf( "dummy cmd: %s %s\n", Cmd_Argv( 0 ), Cmd_Args() );
    ge->ClientCommand( svs.mvd.dummy->edict );
}

static const ucmd_t dummy_cmds[] = {
	{ "cmd", SV_DummyForward_f },
	//{ "connect", MVD_Connect_f },
	{ "set", Cvar_Set_f },
	{ "alias", Cmd_Alias_f },
	{ "play", NULL },
	{ "exec", NULL },
	{ "wait", SV_DummyWait_f },
	{ NULL, NULL }
};

static void SV_DummyExecuteString( const char *line ) {
	char *cmd, *alias;
	const ucmd_t *u;
    cvar_t *v;

	if( !line[0] ) {
		return;
	}

	Cmd_TokenizeString( line, qtrue );

	cmd = Cmd_Argv( 0 );
	if( !cmd[0] ) {
		return;
	}
	for( u = dummy_cmds; u->name; u++ ) {
		if( !strcmp( cmd, u->name ) ) {
			if( u->func ) {
				u->func();
			}
			return;
		}
	}

	alias = Cmd_AliasCommand( cmd );
	if( alias ) {
		if( ++dummy_buffer.aliasCount == ALIAS_LOOP_COUNT ) {
			Com_WPrintf( "SV_DummyExecuteString: runaway alias loop\n" );
			return;
		}
		Cbuf_InsertTextEx( &dummy_buffer, alias );
		return;
	}

    v = Cvar_FindVar( cmd );
	if( v ) {
        Cvar_Command( v );
		return;
	}

    Com_DPrintf( "dummy stufftext: %s\n", line );
	sv_client = svs.mvd.dummy;
	sv_player = svs.mvd.dummy->edict;
	ge->ClientCommand( sv_player );
	sv_client = NULL;
	sv_player = NULL;
}

static void SV_DummyAddMessage( client_t *client, byte *data,
							  size_t length, qboolean reliable )
{
    tcpClient_t *t;

    if( !length || !reliable ) {
        return;
    }

    if( data[0] == svc_stufftext ) {
        data[length] = 0;
		Cbuf_AddTextEx( &dummy_buffer, ( char * )( data + 1 ) );
        return;
    }

    LIST_FOR_EACH( tcpClient_t, t, &svs.mvd.clients, mvdEntry ) {
        if( t->state >= cs_primed ) {
        }
    }

}
void SV_MvdSpawnDummy( void ) {
    client_t *c = svs.mvd.dummy;
	player_state_t *ps;
	entity_state_t *es;
	edict_t *ent;
    int i;

    if( !c ) {
        return;
    }

	sv_client = c;
	sv_player = c->edict;

    ge->ClientBegin( sv_player );

	sv_client = NULL;
	sv_player = NULL;

    if( sv_mvd_begincmd->string[0] ) {
    	Cbuf_AddTextEx( &dummy_buffer, sv_mvd_begincmd->string );
    }

    c->state = cs_spawned;

    memset( svs.mvd.players, 0, sizeof( player_state_t ) * sv_maxclients->integer );
    memset( svs.mvd.entities, 0, sizeof( entity_state_t ) * MAX_EDICTS );

    sv.mvd.layout_time = svs.realtime;

    // set base player states
	for( i = 0; i < sv_maxclients->integer; i++ ) {
	    ent = EDICT_NUM( i + 1 );

    	if( !SV_MvdPlayerIsActive( ent ) ) {
            continue;
        }

        ps = &svs.mvd.players[i];
        *ps = ent->client->ps;
        PPS_INUSE( ps ) = qtrue;
	}

    // set base entity states
	for( i = 1; i < ge->num_edicts; i++ ) {
		ent = EDICT_NUM( i );

		if( ( ent->svflags & SVF_NOCLIENT ) || !ES_INUSE( &ent->s ) ) {
            continue;
        }

        es = &svs.mvd.entities[i];
        *es = ent->s;
        es->number = i;
	}
}

qboolean SV_MvdCreateDummy( void ) {
    client_t *newcl, *lastcl;
    char userinfo[MAX_INFO_STRING];
    char *s;
    qboolean allow;
    int number;

    if( svs.mvd.dummy ) {
        return qtrue; // already created
    }

    // find a free client slot
    lastcl = svs.udp_client_pool + sv_maxclients->integer;
    for( newcl = svs.udp_client_pool; newcl < lastcl; newcl++ ) {
        if( !newcl->state ) {
            break;
        }
    }
    if( newcl == lastcl ) {
        Com_WPrintf( "No slot for dummy MVD client\n" );
        return qfalse;
    }

	memset( newcl, 0, sizeof( *newcl ) );
    number = newcl - svs.udp_client_pool;
	newcl->number = newcl->slot = number;
	newcl->protocol = -1;
    newcl->state = cs_connected;
    newcl->AddMessage = SV_DummyAddMessage;
	newcl->edict = EDICT_NUM( number + 1 );
    newcl->netchan = SV_Mallocz( sizeof( netchan_t ) );
    newcl->netchan->remote_address.type = NA_LOOPBACK;

    List_Init( &newcl->entry );

    Com_sprintf( userinfo, sizeof( userinfo ),
        "\\name\\[MVDSPEC]\\skin\\male/grunt\\mvdspec\\%d\\ip\\loopback",
        PROTOCOL_VERSION_MVD_CURRENT );

    svs.mvd.dummy = newcl;

	// get the game a chance to reject this connection or modify the userinfo
	sv_client = newcl;
	sv_player = newcl->edict;
	allow = ge->ClientConnect( newcl->edict, userinfo );
    sv_client = NULL;
    sv_player = NULL;
	if ( !allow ) {
	    s = Info_ValueForKey( userinfo, "rejmsg" );
        if( *s ) {
            Com_WPrintf( "Dummy MVD client rejected by game DLL: %s\n", s );
        }
        svs.mvd.dummy = NULL;
        return qfalse;
	}

	// parse some info from the info strings
	strcpy( newcl->userinfo, userinfo );
	SV_UserinfoChanged( newcl );

    SV_MvdSpawnDummy();

    return qtrue;
}

void SV_MvdDropDummy( const char *reason ) {
    tcpClient_t *client;

    if( !svs.mvd.dummy ) {
        return;
    }
    LIST_FOR_EACH( tcpClient_t, client, &svs.mvd.clients, mvdEntry ) {
        SV_HttpDrop( client, reason );
    }
    SV_MvdRecStop();

	SV_RemoveClient( svs.mvd.dummy );
    svs.mvd.dummy = NULL;

	SZ_Clear( &sv.mvd.datagram );
	SZ_Clear( &sv.mvd.message );
}

/*
==================
SV_MvdBeginFrame

Checks whether there are active clients on server
and pauses/resumes MVD frame builing proccess accordingly.

On resume, dumps all configstrings clobbered by game DLL
into the multicast buffer.
==================
*/
void SV_MvdBeginFrame( void ) {
	int i, j;
	int index;
	size_t length;
    client_t *client;
    qboolean found;

    if( sv_mvd_wait->integer > 0 ) {
        found = qfalse;
        if( sv_mvd_wait->integer == 1 ) {
            for( i = 1; i <= sv_maxclients->integer; i++ ) {
                edict_t *ent = EDICT_NUM( i );
                if( SV_MvdPlayerIsActive( ent ) ) {
                    found = qtrue;
                    break;
                }
            }
        } else {
            FOR_EACH_CLIENT( client ) {
                if( client->state == cs_spawned ) {
                    found = qtrue;
                    break;
                }
            }
        }

        if( !found ) {
            if( sv.mvd.paused == PAUSED_FRAMES ) {
                Com_Printf( "MVD stream paused, no active clients.\n" );
                for( i = 0; i < CS_BITMAP_LONGS; i++ ) {
                    (( uint32_t * )sv.mvd.dcs)[i] = 0;
                }
            }
            sv.mvd.paused++;
            return;
        }
    }

	if( sv.mvd.paused >= PAUSED_FRAMES ) {
        for( i = 0; i < CS_BITMAP_LONGS; i++ ) {
            if( (( uint32_t * )sv.mvd.dcs)[i] == 0 ) {
                continue;
            }
            index = i << 5;
            for( j = 0; j < 32; j++, index++ ) {
                if( !Q_IsBitSet( sv.mvd.dcs, index ) ) {
                    continue;
                }
                SZ_WriteByte( &sv.mvd.message, svc_configstring );
                SZ_WriteShort( &sv.mvd.message, index );
                length = strlen( sv.configstrings[index] );
                if( length > MAX_QPATH ) {
                    length = MAX_QPATH;
                }
                SZ_Write( &sv.mvd.message, sv.configstrings[index], length );
                SZ_WriteByte( &sv.mvd.message, 0 );
            }
        }

        Com_Printf( "MVD stream resumed, flushed %"PRIz" bytes.\n",
            sv.mvd.message.cursize );
        // will be subsequently written to disk by SV_MvdEndFrame
    }

	sv.mvd.paused = 0;
}

void SV_MvdEndFrame( void ) {
    tcpClient_t *client;
    usercmd_t cmd;
    int length;

	Cbuf_ExecuteEx( &dummy_buffer );
    if( dummy_buffer.waitCount > 0 ) {
        dummy_buffer.waitCount--;
    }

    memset( &cmd, 0, sizeof( cmd ) );
    cmd.msec = 100;
	ge->ClientThink( svs.mvd.dummy->edict, &cmd );

    if( sv.mvd.paused >= PAUSED_FRAMES ) {
        goto clear;
    }

    if( sv.mvd.message.overflowed ) {
        // if reliable message overflowed, kick all clients
        Com_EPrintf( "MVD message overflowed\n" );
        SV_MvdDropDummy( "overflowed" );
        goto clear;
    }

    if( sv.mvd.datagram.overflowed ) {
        Com_WPrintf( "MVD datagram overflowed\n" );
	    SZ_Clear( &sv.mvd.datagram );
    }

    if( sv_mvd_scorecmd->string[0] ) {
        if( svs.realtime - sv.mvd.layout_time > 9000 ) {
            Cbuf_AddTextEx( &dummy_buffer, sv_mvd_scorecmd->string );
            sv.mvd.layout_time = svs.realtime;
        }
    }

    // build delta updates
    SV_MvdEmitFrame();

    // check if frame fits
    if( sv.mvd.message.cursize + msg_write.cursize > MAX_MSGLEN ) {
        Com_WPrintf( "Dumping MVD frame: %"PRIz" bytes\n", msg_write.cursize );
        SZ_Clear( &msg_write );
    }

    // check if unreliable datagram fits
    if( sv.mvd.message.cursize + msg_write.cursize + sv.mvd.datagram.cursize > MAX_MSGLEN ) {
        Com_WPrintf( "Dumping MVD datagram: %"PRIz" bytes\n", sv.mvd.datagram.cursize );
	    SZ_Clear( &sv.mvd.datagram );
    }

    length = LittleShort( sv.mvd.message.cursize + msg_write.cursize + sv.mvd.datagram.cursize );

    // send frame to clients
    LIST_FOR_EACH( tcpClient_t, client, &svs.mvd.clients, mvdEntry ) {
        if( client->state == cs_spawned ) {
            SV_HttpWrite( client, &length, 2 );
            SV_HttpWrite( client, sv.mvd.message.data, sv.mvd.message.cursize );
            SV_HttpWrite( client, msg_write.data, msg_write.cursize );
            SV_HttpWrite( client, sv.mvd.datagram.data, sv.mvd.datagram.cursize );
#if USE_ZLIB
            client->noflush++;
#endif
        }
    }

    // write frame to demofile
    if( sv.mvd.file ) {
        FS_Write( &length, 2, sv.mvd.file );
        FS_Write( sv.mvd.message.data, sv.mvd.message.cursize, sv.mvd.file );
        FS_Write( msg_write.data, msg_write.cursize, sv.mvd.file );
        FS_Write( sv.mvd.datagram.data, sv.mvd.datagram.cursize, sv.mvd.file );

        if( sv_mvd_max_size->value > 0 ) {
            int numbytes = FS_RawTell( sv.mvd.file );

            if( numbytes > sv_mvd_max_size->value * 1000 ) {
                Com_Printf( "Stopping MVD recording, maximum size reached.\n" );
                SV_MvdRecStop();
            }
        } else if( sv_mvd_max_duration->value > 0 &&
            sv.mvd.framenum > sv_mvd_max_duration->value * 600 )
        {
            Com_Printf( "Stopping MVD recording, maximum duration reached.\n" );
            SV_MvdRecStop();
        }
    }

    SZ_Clear( &msg_write );

clear:
	SZ_Clear( &sv.mvd.datagram );
	SZ_Clear( &sv.mvd.message );
}

/*
==================
SV_MvdEmitGamestate

Writes a single giant message with all the startup info.
==================
*/
static void SV_MvdEmitGamestate( void ) {
	char		*string;
	int			i, j;
    player_state_t  *ps;
	entity_state_t	*es;
    size_t      length;
    uint8_t     *patch;
	int flags, extra, portalbytes;
    byte portalbits[MAX_MAP_AREAS/8];

    patch = SZ_GetSpace( &msg_write, 2 );

	// send the serverdata
	MSG_WriteByte( mvd_serverdata );
	MSG_WriteLong( PROTOCOL_VERSION_MVD );
	MSG_WriteShort( PROTOCOL_VERSION_MVD_CURRENT );
	MSG_WriteLong( sv.spawncount );
	MSG_WriteString( fs_game->string );
	MSG_WriteShort( svs.mvd.dummy->number );

    // send configstrings
	for( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
        string = sv.configstrings[i];
		if( !string[0] ) {
			continue;
		}
		length = strlen( string );
		if( length > MAX_QPATH ) {
			length = MAX_QPATH;
		}

		MSG_WriteShort( i );
		MSG_WriteData( string, length );
		MSG_WriteByte( 0 );
	}
	MSG_WriteShort( MAX_CONFIGSTRINGS );

    // send baseline frame
	portalbytes = CM_WritePortalBits( &sv.cm, portalbits );
	MSG_WriteByte( portalbytes );
	MSG_WriteData( portalbits, portalbytes );
	
    // send player states
	flags = 0;
	if( sv_mvd_noblend->integer ) {
		flags |= MSG_PS_IGNORE_BLEND;
	}
	if( sv_mvd_nogun->integer ) {
		flags |= MSG_PS_IGNORE_GUNINDEX|MSG_PS_IGNORE_GUNFRAMES;
	}
	for( i = 0, ps = svs.mvd.players; i < sv_maxclients->integer; i++, ps++ ) {
        extra = 0;
        if( !PPS_INUSE( ps ) ) {
            extra |= MSG_PS_REMOVE;
        }
    	MSG_WriteDeltaPlayerstate_Packet( NULL, ps, i, flags | extra );
	}
	MSG_WriteByte( CLIENTNUM_NONE );

    // send entity states
	for( i = 1, es = svs.mvd.entities + 1; i < ge->num_edicts; i++, es++ ) {
        flags = 0;
        if( i <= sv_maxclients->integer ) {
            ps = &svs.mvd.players[ i - 1 ];
            if( PPS_INUSE( ps ) && ps->pmove.pm_type == PM_NORMAL ) {
                flags |= MSG_ES_FIRSTPERSON;
            }
        }
        if( ( j = es->number ) == 0 ) {
            flags |= MSG_ES_REMOVE;
        }
        es->number = i;
        MSG_WriteDeltaEntity( NULL, es, flags );
        es->number = j;
	}
	MSG_WriteShort( 0 );

    length = msg_write.cursize - 2;
    patch[0] = length & 255;
    patch[1] = ( length >> 8 ) & 255;
}

void SV_MvdClientNew( tcpClient_t *client ) {
	Com_DPrintf( "Sending gamestate to MVD client %s\n",
        NET_AdrToString( &client->stream.address ) );
	client->state = cs_spawned;

    SV_MvdEmitGamestate();

#if USE_ZLIB
    client->noflush = 99999;
#endif

    SV_HttpWrite( client, msg_write.data, msg_write.cursize );
    SZ_Clear( &msg_write );
}

void SV_MvdInitStream( void ) {
    uint32_t magic;
#if USE_ZLIB
    int bits;
#endif

    SV_HttpPrintf( "HTTP/1.0 200 OK\r\n" );

#if USE_ZLIB
    switch( sv_mvd_encoding->integer ) {
    case 1:
        SV_HttpPrintf( "Content-Encoding: gzip\r\n" );
        bits = 31;
        break;
    case 2:
        SV_HttpPrintf( "Content-Encoding: deflate\r\n" );
        bits = 15;
        break;
    default:
        bits = 0;
        break;
    }
#endif

    SV_HttpPrintf(
        "Content-Type: application/octet-stream\r\n"
        "Content-Disposition: attachment; filename=\"stream.mvd2\"\r\n"
        "\r\n" );

#if USE_ZLIB
    if( bits ) {
        deflateInit2( &http_client->z, Z_DEFAULT_COMPRESSION,
            Z_DEFLATED, bits, 8, Z_DEFAULT_STRATEGY );
    }
#endif

    magic = MVD_MAGIC;
    SV_HttpWrite( http_client, &magic, 4 );
}

void SV_MvdGetStream( const char *uri ) {
    if( http_client->method == HTTP_METHOD_HEAD ) {
        SV_HttpPrintf( "HTTP/1.0 200 OK\r\n\r\n" );
        SV_HttpDrop( http_client, "200 OK " );
        return;
    }

    if( !SV_MvdCreateDummy() ) {
        SV_HttpReject( "503 Service Unavailable",
            "Unable to create dummy MVD client. Server is full." );
        return;
    }

    List_Append( &svs.mvd.clients, &http_client->mvdEntry );

    SV_MvdInitStream();

    SV_MvdClientNew( http_client );
}

/*
==============
SV_MvdMulticast

TODO: attempt to combine several identical unicast/multicast messages
into one message to save space (useful for shotgun patterns
as they often occur in the same BSP leaf)
==============
*/
void SV_MvdMulticast( sizebuf_t *buf, int leafnum, mvd_ops_t op ) {
	int bits = ( msg_write.cursize >> 8 ) & 7;

    SZ_WriteByte( buf, op | ( bits << SVCMD_BITS ) );
    SZ_WriteByte( buf, msg_write.cursize & 255 );

	if( op != mvd_multicast_all && op != mvd_multicast_all_r ) {
		SZ_WriteShort( buf, leafnum );
	}
	
	SZ_Write( buf, msg_write.data, msg_write.cursize );
}

/*
==============
SV_MvdUnicast
==============
*/
void SV_MvdUnicast( sizebuf_t *buf, int clientNum, mvd_ops_t op ) {
    int bits = ( msg_write.cursize >> 8 ) & 7;

    SZ_WriteByte( buf, op | ( bits << SVCMD_BITS ) );
    SZ_WriteByte( buf, msg_write.cursize & 255 );
    SZ_WriteByte( buf, clientNum );
    SZ_Write( buf, msg_write.data, msg_write.cursize );
}

/*
==============
SV_MvdConfigstring
==============
*/
void SV_MvdConfigstring( int index, const char *string ) {
	if( sv.mvd.paused >= PAUSED_FRAMES ) {
		Q_SetBit( sv.mvd.dcs, index );
        return;
	}
	SZ_WriteByte( &sv.mvd.message, mvd_configstring );
	SZ_WriteShort( &sv.mvd.message, index );
	SZ_WriteString( &sv.mvd.message, string );
}

void SV_MvdBroadcastPrint( int level, const char *string ) {
	SZ_WriteByte( &sv.mvd.message, mvd_print );
	SZ_WriteByte( &sv.mvd.message, level );
	SZ_WriteString( &sv.mvd.message, string );
}

/*
==============
SV_MvdRecStop

Stops server local MVD recording.
==============
*/
void SV_MvdRecStop( void ) {
	int length;

	if( !sv.mvd.file ) {
		return;
	}
    
	// write demo EOF marker
	length = 0;
	FS_Write( &length, 2, sv.mvd.file );

	FS_FCloseFile( sv.mvd.file );
    sv.mvd.file = 0;
}

const cmd_option_t o_mvdrecord[] = {
    { "h", "help", "display this message" },
    { "z", "gzip", "compress file with gzip" },
    { NULL }
};

static void MVD_Record_c( genctx_t *ctx, int argnum ) {
    if( argnum == 1 ) {
        MVD_File_g( ctx );
    }
}

/*
==============
MVD_Record_f

Begins server MVD recording.
Every entity, every playerinfo and every message will be recorded.
==============
*/
static void MVD_Record_f( void ) {
	char buffer[MAX_OSPATH];
	fileHandle_t demofile;
    uint32_t magic;
    qboolean gzip = qfalse;
    int c;

	if( sv.state != ss_game ) {
        if( sv.state == ss_broadcast ) {
            MVD_StreamedRecord_f();
        } else {
    		Com_Printf( "No server running.\n" );
        }
		return;
	}

    while( ( c = Cmd_ParseOptions( o_mvdrecord ) ) != -1 ) {
        switch( c ) {
        case 'h':
            Cmd_PrintUsage( o_mvdrecord, "[/]<filename>" );
            Com_Printf( "Begin local MVD recording.\n" );
            Cmd_PrintHelp( o_mvdrecord );
            return;
        case 'z':
            gzip = qtrue;
            break;
        }
    }

    if( !cmd_optarg[0] ) {
        Com_Printf( "Missing filename argument.\n" );
        Cmd_PrintHint();
        return;
    }

	if( !svs.mvd.entities ) {
		Com_Printf( "MVD recording is disabled on this server.\n" );
		return;
	}

	if( sv.mvd.file ) {
		Com_Printf( "Already recording a local MVD.\n" );
		return;
	}

	//
	// open the demo file
	//
	if( cmd_optarg[0] == '/' ) {
		Q_strncpyz( buffer, cmd_optarg + 1, sizeof( buffer ) );
	} else {
		Q_concat( buffer, sizeof( buffer ), "demos/", cmd_optarg, NULL );
    	COM_AppendExtension( buffer, ".mvd2", sizeof( buffer ) );
        if( gzip ) {
        	COM_AppendExtension( buffer, ".gz", sizeof( buffer ) );
        }
	}

	FS_FOpenFile( buffer, &demofile, FS_MODE_WRITE );
	if( !demofile ) {
		Com_EPrintf( "Couldn't open %s for writing\n", buffer );
		return;
	}

    if( !SV_MvdCreateDummy() ) {
        FS_FCloseFile( demofile );
        return;
    }

	if( gzip ) {
        FS_FilterFile( demofile );
    }

	sv.mvd.file = demofile;
	
    magic = MVD_MAGIC;
    FS_Write( &magic, 4, demofile );

    SV_MvdEmitGamestate();
    FS_Write( msg_write.data, msg_write.cursize, demofile );
    SZ_Clear( &msg_write );

	Com_Printf( "Recording local MVD to %s\n", buffer );
}


/*
==============
MVD_Stop_f

Ends server MVD recording
==============
*/
static void MVD_Stop_f( void ) {
    if( sv.state == ss_broadcast ) {
        MVD_StreamedStop_f();
        return;
    }
	if( !sv.mvd.file ) {
		Com_Printf( "Not recording a local MVD.\n" );
		return;
	}

	Com_Printf( "Stopped local MVD recording.\n" );
	SV_MvdRecStop();
}

static void MVD_Stuff_f( void ) {
    if( svs.mvd.dummy ) {
        Cbuf_AddTextEx( &dummy_buffer, Cmd_RawArgs() );
    } else {
        Com_Printf( "Can't '%s', dummy MVD client is not active\n", Cmd_Argv( 0 ) );
    }
}

static const cmdreg_t c_svmvd[] = {
	{ "mvdrecord", MVD_Record_f, MVD_Record_c },
	{ "mvdstop", MVD_Stop_f },
	{ "mvdstuff", MVD_Stuff_f },

    { NULL }
};

void SV_MvdRegister( void ) {
	sv_mvd_enable = Cvar_Get( "sv_mvd_enable", "0", CVAR_LATCH );
	sv_mvd_auth = Cvar_Get( "sv_mvd_auth", "", CVAR_PRIVATE );
	sv_mvd_wait = Cvar_Get( "sv_mvd_wait", "0", CVAR_ROM ); // TODO
	sv_mvd_max_size = Cvar_Get( "sv_mvd_max_size", "0", 0 );
	sv_mvd_max_duration = Cvar_Get( "sv_mvd_max_duration", "0", 0 );
	sv_mvd_noblend = Cvar_Get( "sv_mvd_noblend", "0", CVAR_LATCH );
	sv_mvd_nogun = Cvar_Get( "sv_mvd_nogun", "1", CVAR_LATCH );
    sv_mvd_begincmd = Cvar_Get( "sv_mvd_begincmd",
        "wait 50; putaway; wait 10; help;", 0 );
    sv_mvd_scorecmd = Cvar_Get( "sv_mvd_scorecmd",
        "putaway; wait 10; help;", 0 );
#if USE_ZLIB
    sv_mvd_encoding = Cvar_Get( "sv_mvd_encoding", "1", 0 );
#endif
    sv_mvd_capture_flags = Cvar_Get( "sv_mvd_capture_flags", "5", 0 );

	dummy_buffer.text = dummy_buffer_text;
    dummy_buffer.maxsize = sizeof( dummy_buffer_text );
	dummy_buffer.exec = SV_DummyExecuteString;

    Cmd_Register( c_svmvd );
}

