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

static cmdbuf_t	dummy_buffer;
static char		dummy_buffer_text[MAX_STRING_CHARS];

/*
==================
SV_MvdPlayerIsActive

Determines if the given player entity is active,
e.g. fully in game and visible to other players.

Should work both for human players, as well as bots,
and should never attempt to capture spectators.
FIXME: If intermission is running, capture everyone.

Tested to work with CTF, OSP Tourney, AQ2 TNG, Gladiator bots.
Ideally a compatible game DLL should provide us with information
whether given player is to be captured, instead of relying on this hack.
==================
*/
qboolean SV_MvdPlayerIsActive( edict_t *ent ) {
	if( !ent->inuse ) {
		return qfalse;
	}

    // not a client at all?
	if( !ent->client ) {
		return qfalse;
	}

	// HACK: make sure player_state_t is valid
	if( !ent->client->ps.fov ) {
		return qfalse;
	}

    if( svs.mvdummy && ent == svs.mvdummy->edict ) {
        return qtrue;
    }

	// HACK: if pm_type == PM_FREEZE, assume intermission is running
	// if PMF_NO_PREDICTION is set, they are following someone!
	if( ent->client->ps.pmove.pm_type == PM_FREEZE &&
		!( ent->client->ps.pmove.pm_flags & PMF_NO_PREDICTION ) )
	{
		return qtrue;
	}

	// if set to invisible, skip
	if( ent->svflags & SVF_NOCLIENT ) {
		return qfalse;
	}

	if( ent->s.modelindex || ent->s.effects || ent->s.sound || ent->s.event ) {
		return qtrue;
	}

    // if it has no effects, skip
	return qfalse;
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

        oldps = &svs.players[i];
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

        oldes = &sv.mvd.entities[i];
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
            flags |= MSG_ES_FIRSTPERSON;
        }
        
        if( !oldes->number ) {
			// this is a new entity, send it from the last state
			flags |= MSG_ES_FORCE|MSG_ES_NEWENTITY;
        }
		
        MSG_WriteDeltaEntity( oldes, newes, flags );

        *oldes = *newes;
        oldes->number = i;
	}

	MSG_WriteShort( 0 );	// end of packetentities
}

static void SV_DummyWait_f( void ) {
	dummy_buffer.waitCount = 1;
}

static const ucmd_t dummy_cmds[] = {
	//{ "cmd", SV_DummyForward_f },
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
	sv_client = svs.mvdummy;
	sv_player = svs.mvdummy->edict;
	ge->ClientCommand( sv_player );
	sv_client = NULL;
	sv_player = NULL;
}

static void SV_DummyAddMessage( client_t *client, byte *data,
							  int length, qboolean reliable )
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

    LIST_FOR_EACH( tcpClient_t, t, &svs.mvdClients, mvdEntry ) {
        if( t->state >= cs_primed ) {
        }
    }

}
void SV_MvdSpawnDummy( void ) {
    client_t *c = svs.mvdummy;
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

	Cbuf_AddTextEx( &dummy_buffer, "wait 50; putaway; wait 10; help\n" );

    c->state = cs_spawned;

    // set base player states
	for( i = 0; i < sv_maxclients->integer; i++ ) {
	    ent = EDICT_NUM( i + 1 );

    	if( !SV_MvdPlayerIsActive( ent ) ) {
            continue;
        }

        ps = &svs.players[i];
        *ps = ent->client->ps;
        PPS_INUSE( ps ) = qtrue;
	}

    // set base entity states
	for( i = 1; i < ge->num_edicts; i++ ) {
		ent = EDICT_NUM( i );

		if( ( ent->svflags & SVF_NOCLIENT ) || !ES_INUSE( &ent->s ) ) {
            continue;
        }

        es = &sv.mvd.entities[i];
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

    if( svs.mvdummy ) {
        return qtrue; // already created
    }

    // find a free client slot
    lastcl = svs.clientpool + sv_maxclients->integer;
    for( newcl = svs.clientpool; newcl < lastcl; newcl++ ) {
        if( !newcl->state ) {
            break;
        }
    }
    if( newcl == lastcl ) {
        Com_EPrintf( "No slot for dummy MVD client\n" );
        return qfalse;
    }

	memset( newcl, 0, sizeof( *newcl ) );
    number = newcl - svs.clientpool;
	newcl->number = number;
	newcl->protocol = -1;
    newcl->state = cs_connected;
    newcl->AddMessage = SV_DummyAddMessage;
	newcl->edict = EDICT_NUM( number + 1 );

    List_Init( &newcl->entry );

    Com_sprintf( userinfo, sizeof( userinfo ),
        "\\name\\[MVDSPEC]\\skin\\male/grunt\\mvdversion\\%d\\ip\\loopback",
        PROTOCOL_VERSION_MVD_MINOR );

    svs.mvdummy = newcl;

	// get the game a chance to reject this connection or modify the userinfo
	sv_client = newcl;
	sv_player = newcl->edict;
	allow = ge->ClientConnect( newcl->edict, userinfo );
    sv_client = NULL;
    sv_player = NULL;
	if ( !allow ) {
	    s = Info_ValueForKey( userinfo, "rejmsg" );
        if( *s ) {
            Com_EPrintf( "Dummy MVD client rejected by game DLL: %s\n", s );
        }
        svs.mvdummy = NULL;
        return qfalse;
	}

	// parse some info from the info strings
	Q_strncpyz( newcl->userinfo, userinfo, sizeof( newcl->userinfo ) );
	SV_UserinfoChanged( newcl );

    SV_MvdSpawnDummy();

    return qtrue;
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
	int index, length;
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
                for( i = 0; i < DCS_DWORDS; i++ ) {
                    (( uint32 * )sv.mvd.dcs)[i] = 0;
                }
                //SV_MvdBroadcastCommand( "mvdpause\n" );
            }
            sv.mvd.paused++;
            return;
        }
    }

	if( sv.mvd.paused >= PAUSED_FRAMES ) {
        for( i = 0; i < DCS_DWORDS; i++ ) {
            if( (( uint32 * )sv.mvd.dcs)[i] == 0 ) {
                continue;
            }
            index = i << 5;
            for( j = 0; j < 32; j++, index++ ) {
                if( !Q_IsBitSet( sv.mvd.dcs, index ) ) {
                    continue;
                }
                SZ_WriteByte( &sv.mvd.multicast, svc_configstring );
                SZ_WriteShort( &sv.mvd.multicast, index );
                length = strlen( sv.configstrings[index] );
                if( length > MAX_QPATH ) {
                    length = MAX_QPATH;
                }
                SZ_Write( &sv.mvd.multicast, sv.configstrings[index], length );
                SZ_WriteByte( &sv.mvd.multicast, 0 );
            }
        }

        Com_Printf( "MVD stream resumed, flushed %d bytes.\n",
            sv.mvd.multicast.cursize );
        // will be subsequently written to disk by SV_MvdEndFrame
    }

	sv.mvd.paused = 0;
}


/*
==================
SV_MvdRecFrame
==================
*/
static void SV_MvdRecFrame( void ) {
    FS_Write( msg_write.data, msg_write.cursize, sv.mvd.file );
    FS_Write( sv.mvd.multicast.data, sv.mvd.multicast.cursize, sv.mvd.file );

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

void SV_MvdEndFrame( void ) {
    tcpClient_t *client;
    uint16 *patch;

	Cbuf_ExecuteEx( &dummy_buffer );
    if( dummy_buffer.waitCount > 0 ) {
        dummy_buffer.waitCount--;
    }

    if( sv.mvd.paused >= PAUSED_FRAMES ) {
	    SZ_Clear( &sv.mvd.multicast );
        return;
    }

    patch = SZ_GetSpace( &msg_write, 2 );
    SV_MvdEmitFrame();
    *patch = LittleShort( msg_write.cursize + sv.mvd.multicast.cursize - 2 );

    LIST_FOR_EACH( tcpClient_t, client, &svs.mvdClients, mvdEntry ) {
        if( client->state == cs_spawned ) {
            SV_HttpWrite( client, msg_write.data, msg_write.cursize );
            SV_HttpWrite( client, sv.mvd.multicast.data, sv.mvd.multicast.cursize );
#if USE_ZLIB
            client->noflush++;
#endif
        }
    }

    if( sv.mvd.file ) {
        SV_MvdRecFrame();
    }

    SZ_Clear( &msg_write );
	SZ_Clear( &sv.mvd.multicast );
}

/*
==================
SV_MvdEmitGamestate

Writes a single giant message with all the startup info.
==================
*/
static void SV_MvdEmitGamestate( void ) {
	char		*string;
	int			i;
    player_state_t  *ps;
	entity_state_t	*es;
    int         length;
    uint16      *patch;
	int flags, portalbytes;
    byte portalbits[MAX_MAP_AREAS/8];

    patch = SZ_GetSpace( &msg_write, 2 );

	// send the serverdata
	MSG_WriteByte( mvd_serverdata );
	MSG_WriteLong( PROTOCOL_VERSION_MVD );
	MSG_WriteShort( PROTOCOL_VERSION_MVD_MINOR );
	MSG_WriteLong( sv.spawncount );
	MSG_WriteString( fs_game->string );
	MSG_WriteShort( svs.mvdummy->number );

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
	for( i = 0; i < sv_maxclients->integer; i++ ) {
        ps = &svs.players[i];
		if( PPS_INUSE( ps ) ) {
			MSG_WriteDeltaPlayerstate_Packet( NULL, ps, i,
                flags | MSG_PS_FORCE );
		}
	}
	MSG_WriteByte( CLIENTNUM_NONE );

    // send entity states
	for( i = 1; i < ge->num_edicts; i++ ) {
        es = &sv.mvd.entities[i];
        if( es->number ) {
            flags = MSG_ES_FORCE|MSG_ES_NEWENTITY;
            if( i <= sv_maxclients->integer ) {
                flags |= MSG_ES_FIRSTPERSON;
            }
            MSG_WriteDeltaEntity( NULL, es, flags );
        }
	}
	MSG_WriteShort( 0 );

    *patch = LittleShort( msg_write.cursize - 2 );
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


void SV_MvdGetStream( const char *uri ) {
    uint32 magic;

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

    List_Append( &svs.mvdClients, &http_client->mvdEntry );

    SV_HttpPrintf(
        "HTTP/1.0 200 OK\r\n"
#ifdef USE_ZLIB
        "Content-Encoding: deflate\r\n"
#endif
        "Content-Type: application/octet-stream\r\n"
        "Content-Disposition: attachment; filename=\"stream.mvd2\"\r\n"
        "\r\n" );

#if USE_ZLIB
    deflateInit( &http_client->z, Z_DEFAULT_COMPRESSION );
#endif

    magic = MVD_MAGIC;
    SV_HttpWrite( http_client, &magic, 4 );

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
void SV_MvdMulticast( int leafnum, mvd_ops_t op ) {
	int bits;

	if( sv.mvd.paused >= PAUSED_FRAMES ) {
        return;
    }

	bits = ( msg_write.cursize >> 8 ) & 7;
    SZ_WriteByte( &sv.mvd.multicast, op | ( bits << SVCMD_BITS ) );
    SZ_WriteByte( &sv.mvd.multicast, msg_write.cursize & 255 );

	if( op != mvd_multicast_all && op != mvd_multicast_all_r ) {
		SZ_WriteShort( &sv.mvd.multicast, leafnum );
	}
	
	SZ_Write( &sv.mvd.multicast, msg_write.data, msg_write.cursize );
}

/*
==============
SV_MvdUnicast
==============
*/
void SV_MvdUnicast( int clientNum, mvd_ops_t op ) {
    int bits;

	if( sv.mvd.paused >= PAUSED_FRAMES ) {
        return;
    }

	bits = ( msg_write.cursize >> 8 ) & 7;
    SZ_WriteByte( &sv.mvd.multicast, op | ( bits << SVCMD_BITS ) );
    SZ_WriteByte( &sv.mvd.multicast, msg_write.cursize & 255 );
    SZ_WriteByte( &sv.mvd.multicast, clientNum );
    SZ_Write( &sv.mvd.multicast, msg_write.data, msg_write.cursize );
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
	SZ_WriteByte( &sv.mvd.multicast, mvd_configstring );
	SZ_WriteShort( &sv.mvd.multicast, index );
	SZ_WriteString( &sv.mvd.multicast, string );
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

/*
==============
MVD_Record_f

Begins server MVD recording.
Every entity, every playerinfo and every message will be recorded.
==============
*/
static void MVD_Record_f( void ) {
	char buffer[MAX_QPATH];
	char *name;
	fileHandle_t demofile;
    uint32 magic;

	if( sv.state != ss_game ) {
        if( sv.state == ss_broadcast ) {
            MVD_StreamedRecord_f();
        } else {
    		Com_Printf( "Must be running a game server to record.\n" );
        }
		return;
	}

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: %s [/]<filename>\n", Cmd_Argv( 0 ) );
		return;
	}

	if( !sv_mvd_enable->integer ) {
		Com_Printf( "MVD recording is disabled on this server.\n" );
		return;
	}

	if( sv.mvd.file ) {
		Com_Printf( "Already recording.\n" );
		return;
	}

	//
	// open the demo file
	//
	name = Cmd_Argv( 1 );
	if( name[0] == '/' ) {
		Q_strncpyz( buffer, name + 1, sizeof( buffer ) );
	} else {
		Com_sprintf( buffer, sizeof( buffer ), "demos/%s", name );
		COM_DefaultExtension( buffer, ".mvd2", sizeof( buffer ) );
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

	sv.mvd.file = demofile;
	
    magic = MVD_MAGIC;
    FS_Write( &magic, 4, demofile );

    SV_MvdEmitGamestate();
    FS_Write( msg_write.data, msg_write.cursize, demofile );
    SZ_Clear( &msg_write );

	Com_Printf( "Recording MVD to %s\n", buffer );
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

	Com_Printf( "Local MVD recording completed.\n" );
	SV_MvdRecStop();
}

static void MVD_Stuff_f( void ) {
    if( svs.mvdummy ) {
        Cbuf_AddTextEx( &dummy_buffer, Cmd_RawArgs() );
    } else {
        Com_Printf( "Can't %s, dummy MVD client not active\n", Cmd_Argv( 0 ) );
    }
}

static const cmdreg_t c_svmvd[] = {
	{ "mvdrecord", MVD_Record_f, MVD_Play_g },
	{ "mvdstop", MVD_Stop_f },
	{ "mvdstuff", MVD_Stuff_f },

    { NULL }
};

void SV_MvdRegister( void ) {
	sv_mvd_enable = Cvar_Get( "sv_mvd_enable", "0", CVAR_LATCH );
	sv_mvd_auth = Cvar_Get( "sv_mvd_auth", "", CVAR_PRIVATE );
	sv_mvd_wait = Cvar_Get( "sv_mvd_wait", "0", 0 );
	sv_mvd_max_size = Cvar_Get( "sv_mvd_max_size", "0", 0 );
	sv_mvd_max_duration = Cvar_Get( "sv_mvd_max_duration", "0", 0 );
	sv_mvd_noblend = Cvar_Get( "sv_mvd_noblend", "0", CVAR_LATCH );
	sv_mvd_nogun = Cvar_Get( "sv_mvd_nogun", "1", CVAR_LATCH );

	dummy_buffer.text = dummy_buffer_text;
    dummy_buffer.maxsize = sizeof( dummy_buffer_text );
	dummy_buffer.exec = SV_DummyExecuteString;

    Cmd_Register( c_svmvd );
}

