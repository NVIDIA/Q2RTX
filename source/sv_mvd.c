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
static void SV_MvdBuildFrame( void ) {
    client_frame_t *frame;
	edict_t *ent;
	entity_state_t *es, *s;
	player_state_t *ps;
	int i;

	frame = &sv.mvdframes[++sv.mvdframenum & 1];
	frame->senttime = svs.realtime;
	frame->areabytes = CM_WritePortalBits( &sv.cm, frame->areabits );
	frame->numEntities = 0;
	frame->firstEntity = svs.nextEntityStates;
	frame->numPlayers = 0;
	frame->firstPlayer = svs.nextPlayerStates;

	for( i = 1; i < ge->num_edicts; i++ ) {
		ent = EDICT_NUM( i );

		if( ent->svflags & SVF_NOCLIENT ) {
            continue;
        }

        s = &ent->s;
		if( !s->modelindex && !s->effects && !s->sound && !s->event ) {
            continue;
        }

        es = &svs.entityStates[svs.nextEntityStates % svs.numEntityStates];
        *es = *s;
        es->number = i;

        svs.nextEntityStates++;
        frame->numEntities++;
	}

	for( i = 0; i < sv_maxclients->integer; i++ ) {
	    ent = EDICT_NUM( i + 1 );
        if( !svs.mvdummy || i != svs.mvdummy->number ) {
    		if( !SV_MvdPlayerIsActive( ent ) ) {
                continue;
            }
        }

	    ps = &svs.playerStates[svs.nextPlayerStates % svs.numPlayerStates];
        *ps = ent->client->ps;
        PPS_NUM( ps ) = i;

    	svs.nextPlayerStates++;
	    frame->numPlayers++;
	}
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

    Com_Printf( "dummy stufftext: %s\n", line );
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

    if( !c ) {
        return;
    }

	sv_client = c;
	sv_player = c->edict;

    SV_CreateBaselines( c->param.baselines );
    c->param.basesize = sizeof( entity_state_t );
    c->param.maxplayers = sv_maxclients->integer;

    ge->ClientBegin( sv_player );

	sv_client = NULL;
	sv_player = NULL;

	Cbuf_AddTextEx( &dummy_buffer, "wait 50; putaway; wait 10; help\n" );

    SV_MvdBuildFrame();

    c->state = cs_spawned;
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
            Com_Printf( "Dummy MVD client rejected by game DLL: %s\n", s );
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
            if( sv.mvdpaused == PAUSED_FRAMES ) {
                Com_Printf( "MVD stream paused, no active clients.\n" );
                for( i = 0; i < DCS_DWORDS; i++ ) {
                    (( uint32 * )sv.dirty_configstrings)[i] = 0;
                }
                //SV_MvdBroadcastCommand( "mvdpause\n" );
            }
            sv.mvdpaused++;
            return;
        }
    }

	if( sv.mvdpaused >= PAUSED_FRAMES ) {
        for( i = 0; i < DCS_DWORDS; i++ ) {
            if( (( uint32 * )sv.dirty_configstrings)[i] == 0 ) {
                continue;
            }
            index = i << 5;
            for( j = 0; j < 32; j++, index++ ) {
                if( !Q_IsBitSet( sv.dirty_configstrings, index ) ) {
                    continue;
                }
                SZ_WriteByte( &sv.multicast, svc_configstring );
                SZ_WriteShort( &sv.multicast, index );
                length = strlen( sv.configstrings[index] );
                if( length > MAX_QPATH ) {
                    length = MAX_QPATH;
                }
                SZ_Write( &sv.multicast, sv.configstrings[index], length );
                SZ_WriteByte( &sv.multicast, 0 );
            }
        }

        Com_Printf( "MVD stream resumed, flushed %d bytes.\n",
                sv.multicast.cursize );
        // will be subsequently written to disk by SV_MvdEndFrame
    }
		
	sv.mvdpaused = 0;
}


/*
==================
SV_MvdEmitFrame

Writes new MVD frame, delta compressed from the previous one.
==================
*/
static void SV_MvdEmitFrame( qboolean delta ) {
	client_frame_t *frame, *oldframe;
	msgPsFlags_t flags;

	frame = &sv.mvdframes[sv.mvdframenum & 1];

    if( delta ) {
	    oldframe = &sv.mvdframes[( sv.mvdframenum - 1 ) & 1];
	    MSG_WriteByte( mvd_frame );
    } else {
        oldframe = NULL;
	    MSG_WriteByte( mvd_frame_nodelta );
    }

	MSG_WriteLong( sv.framenum );

	MSG_WriteByte( frame->areabytes );
	MSG_WriteData( frame->areabits, frame->areabytes );
	
	flags = MSG_PS_IGNORE_PREDICTION|MSG_PS_IGNORE_DELTAANGLES;
	if( sv_mvd_noblend->integer ) {
		flags |= MSG_PS_IGNORE_BLEND;
	}
	if( sv_mvd_nogun->integer ) {
		flags |= MSG_PS_IGNORE_GUNINDEX|MSG_PS_IGNORE_GUNFRAMES;
	}
	SV_EmitPacketPlayers( oldframe, frame, flags );
	SV_EmitPacketEntities( oldframe, frame, MAX_EDICTS,
        &svs.mvdummy->param );
}

/*
==================
SV_MvdRecFrame
==================
*/
static void SV_MvdRecFrame( void ) {
    FS_Write( msg_write.data, msg_write.cursize, sv.demofile );
    FS_Write( sv.multicast.data, sv.multicast.cursize, sv.demofile );

	if( sv_mvd_max_size->value > 0 ) {
	    int numbytes = FS_RawTell( sv.demofile );

        if( numbytes > sv_mvd_max_size->value * 1000 ) {
    		Com_Printf( "Stopping MVD recording, maximum size reached.\n" );
	    	SV_MvdRecStop();
        }
	} else if( sv_mvd_max_duration->value > 0 &&
        sv.mvdframenum > sv_mvd_max_duration->value * 600 )
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

    if( sv.mvdpaused >= PAUSED_FRAMES ) {
	    SZ_Clear( &sv.multicast );
        return;
    }

    SV_MvdBuildFrame();

    patch = SZ_GetSpace( &msg_write, 2 );
    SV_MvdEmitFrame( qtrue );
    *patch = LittleShort( msg_write.cursize + sv.multicast.cursize - 2 );

    LIST_FOR_EACH( tcpClient_t, client, &svs.mvdClients, mvdEntry ) {
        if( client->state == cs_spawned ) {
            SV_HttpWrite( client, msg_write.data, msg_write.cursize );
            SV_HttpWrite( client, sv.multicast.data, sv.multicast.cursize );
#if USE_ZLIB
            client->noflush++;
#endif
        }
    }

    if( sv.mvdrecording ) {
        SV_MvdRecFrame();
    }

    SZ_Clear( &msg_write );
	SZ_Clear( &sv.multicast );
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
	entity_state_t	*base;
    int         length;
    uint16      *patch;

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

    // send baselines
    for( i = 0; i < SV_BASELINES_CHUNKS; i++ ) {
		base = svs.mvdummy->param.baselines[i];
		if( !base ) {
			continue;
		}
    	for( j = 0; j < SV_BASELINES_PER_CHUNK; j++ ) {
            if( base->number ) {
                MSG_WriteDeltaEntity( NULL, base, MSG_ES_FORCE );
            }
            base++;
        }
	}
    MSG_WriteShort( 0 );

    // send uncompressed frame
    SV_MvdEmitFrame( qfalse );

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

	if( sv.mvdpaused >= PAUSED_FRAMES ) {
        return;
    }

	bits = ( msg_write.cursize >> 8 ) & 7;
    SZ_WriteByte( &sv.multicast, op | ( bits << SVCMD_BITS ) );
    SZ_WriteByte( &sv.multicast, msg_write.cursize & 255 );

	if( op != mvd_multicast_all && op != mvd_multicast_all_r ) {
		SZ_WriteShort( &sv.multicast, leafnum );
	}
	
	SZ_Write( &sv.multicast, msg_write.data, msg_write.cursize );
}

/*
==============
SV_MvdUnicast
==============
*/
void SV_MvdUnicast( int clientNum, mvd_ops_t op ) {
    int bits;

	if( sv.mvdpaused >= PAUSED_FRAMES ) {
        return;
    }

	bits = ( msg_write.cursize >> 8 ) & 7;
    SZ_WriteByte( &sv.multicast, op | ( bits << SVCMD_BITS ) );
    SZ_WriteByte( &sv.multicast, msg_write.cursize & 255 );
    SZ_WriteByte( &sv.multicast, clientNum );
    SZ_Write( &sv.multicast, msg_write.data, msg_write.cursize );
}

/*
==============
SV_MvdConfigstring
==============
*/
void SV_MvdConfigstring( int index, const char *string ) {
	if( sv.mvdpaused >= PAUSED_FRAMES ) {
		Q_SetBit( sv.dirty_configstrings, index );
        return;
	}
	SZ_WriteByte( &sv.multicast, mvd_configstring );
	SZ_WriteShort( &sv.multicast, index );
	SZ_WriteString( &sv.multicast, string );
}

/*
==============
SV_MvdRecStop

Stops server local MVD recording.
==============
*/
void SV_MvdRecStop( void ) {
	int length;

	if( !sv.mvdrecording ) {
		return;
	}
    
	// write demo EOF marker
	length = 0;
	FS_Write( &length, 2, sv.demofile );

	FS_FCloseFile( sv.demofile );
    sv.demofile = 0;

    sv.mvdrecording = qfalse;
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
		Com_Printf( "MVD recording disabled on this server.\n" );
		return;
	}

	if( sv.mvdrecording ) {
		Com_Printf( "Already recording a local MVD.\n" );
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
		Com_EPrintf( "Unable to create dummy MVD client\n" );
        return;
    }

	sv.demofile = demofile;
	sv.mvdrecording = qtrue;
	
    magic = MVD_MAGIC;
    FS_Write( &magic, 4, sv.demofile );

    SV_MvdEmitGamestate();
    FS_Write( msg_write.data, msg_write.cursize, sv.demofile );
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
	if( !sv.mvdrecording ) {
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

