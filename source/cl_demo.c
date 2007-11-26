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
// cl_demo.c - demo recording and playback
//

#include "cl_local.h"

// =========================================================================

/*
====================
CL_WriteDemoMessage

Dumps the current demo message, prefixed by the length.
====================
*/
void CL_WriteDemoMessage( sizebuf_t *buf ) {
	int		length;

    if( buf->overflowed ) {
        SZ_Clear( buf );
        Com_WPrintf( "Demo message overflowed.\n" );
        return;
    }

	length = LittleLong( buf->cursize );
	FS_Write( &length, 4, cls.demorecording );
	FS_Write( buf->data, buf->cursize, cls.demorecording );

    SZ_Clear( buf );
}

/*
=============
CL_EmitPacketEntities

Writes a delta update of an entity_state_t list to the message.
=============
*/
static void CL_EmitPacketEntities( server_frame_t *from, server_frame_t *to ) {
	entity_state_t	*oldent, *newent;
	int	    oldindex, newindex;
	int     oldnum, newnum;
	int     i, from_num_entities;

	if( !from )
		from_num_entities = 0;
	else
		from_num_entities = from->numEntities;

	newindex = 0;
	oldindex = 0;
	oldent = newent = 0;
	while( newindex < to->numEntities || oldindex < from_num_entities ) {
		if( newindex >= to->numEntities ) {
			newnum = 9999;
		} else {
            i = ( to->firstEntity + newindex ) & PARSE_ENTITIES_MASK;
			newent = &cl.entityStates[i];
			newnum = newent->number;
		}

		if( oldindex >= from_num_entities ) {
			oldnum = 9999;
		} else {
            i = ( from->firstEntity + oldindex ) & PARSE_ENTITIES_MASK;
			oldent = &cl.entityStates[i];
			oldnum = oldent->number;
		}

		if( newnum == oldnum ) {
			// delta update from old position
			// because the force parm is false, this will not result
			// in any bytes being emited if the entity has not changed at all
			MSG_WriteDeltaEntity( oldent, newent, 0 );
			oldindex++;
			newindex++;
			continue;
		}

		if( newnum < oldnum ) {	
			// this is a new entity, send it from the baseline
			MSG_WriteDeltaEntity( &cl.baselines[newnum], newent,
                MSG_ES_FORCE|MSG_ES_NEWENTITY );
			newindex++;
			continue;
		}

		if( newnum > oldnum ) {
			// the old entity isn't present in the new message
			MSG_WriteDeltaEntity( oldent, NULL, MSG_ES_FORCE );
			oldindex++;
			continue;
		}
	}

	MSG_WriteShort( 0 );	// end of packetentities
}

/*
====================
CL_EmitDemoFrame

Writes delta from the last frame we got to the current frame.
====================
*/
void CL_EmitDemoFrame( void ) {
	server_frame_t		*oldframe;
	player_state_t		*oldstate;
	int					lastframe;

    if( cl.demoframe < 0 ) {
        oldframe = NULL;
        oldstate = NULL;
        lastframe = -1;
    } else {
        oldframe = &cl.frames[cl.demoframe & UPDATE_MASK];
        oldstate = &oldframe->ps;
        lastframe = cl.demoframe + cl.demodelta;
		if( oldframe->number != cl.demoframe || !oldframe->valid ||
		    cl.numEntityStates - oldframe->firstEntity > MAX_PARSE_ENTITIES )
        {
            oldframe = NULL;
            oldstate = NULL;
            lastframe = -1;
        }
    }

	MSG_WriteByte( svc_frame );
	MSG_WriteLong( cl.frame.number + cl.demodelta );
	MSG_WriteLong( lastframe );	// what we are delta'ing from
	MSG_WriteByte( 0 );	// rate dropped packets

	// send over the areabits
	MSG_WriteByte( cl.frame.areabytes );
	MSG_WriteData( cl.frame.areabits, cl.frame.areabytes );

	// delta encode the playerstate
	MSG_WriteByte( svc_playerinfo );
	MSG_WriteDeltaPlayerstate_Default( oldstate, &cl.frame.ps );
	
	// delta encode the entities
	MSG_WriteByte( svc_packetentities );
	CL_EmitPacketEntities( oldframe, &cl.frame );

    if( cls.demobuff.cursize + msg_write.cursize > cls.demobuff.maxsize ) {
        Com_WPrintf( "Oversize demo frame: %d bytes\n",
            cls.demobuff.cursize + msg_write.cursize );
    } else {
        SZ_Write( &cls.demobuff, msg_write.data, msg_write.cursize );
        cl.demoframe = cl.frame.number;
    }

    SZ_Clear( &msg_write );
}

void CL_EmitZeroFrame( void ) {
    cl.demodelta++;

	MSG_WriteByte( svc_frame );
	MSG_WriteLong( cl.frame.number + cl.demodelta );
	MSG_WriteLong( cl.frame.number + cl.demodelta - 1 );	// what we are delta'ing from
	MSG_WriteByte( 0 );	// rate dropped packets

	// send over the areabits
	MSG_WriteByte( cl.frame.areabytes );
	MSG_WriteData( cl.frame.areabits, cl.frame.areabytes );

	MSG_WriteByte( svc_playerinfo );
    MSG_WriteShort( 0 );
    MSG_WriteLong( 0 );

	MSG_WriteByte( svc_packetentities );
    MSG_WriteShort( 0 );

    CL_WriteDemoMessage( &msg_write );

    SZ_Clear( &msg_write );
}


/*
====================
CL_Stop_f

stop recording a demo
====================
*/
void CL_Stop_f( void ) {
	int length;

	if( !cls.demorecording ) {
		Com_Printf( "Not recording a demo.\n" );
		return;
	}

	if( cls.netchan && cls.serverProtocol >= PROTOCOL_VERSION_R1Q2 ) {
		// tell the server we finished recording
		MSG_WriteByte( clc_setting );
		MSG_WriteShort( CLS_RECORDING );
		MSG_WriteShort( 0 );
		MSG_FlushTo( &cls.netchan->message );
	}

// finish up
	length = -1;
	FS_Write( &length, 4, cls.demorecording );

    length = FS_RawTell( cls.demorecording );

// close demofile
	FS_FCloseFile( cls.demorecording );
	cls.demorecording = 0;

	Com_Printf( "Stopped demo (%d bytes written).\n", length );
}

/*
====================
CL_Record_f

record <demoname>

Begins recording a demo from the current position
====================
*/
void CL_Record_f( void ) {
	char	name[MAX_OSPATH];
	int		i, length;
	entity_state_t	*ent;
	char *string;
	fileHandle_t demofile;
	qboolean compressed = qfalse;

	i = 1;
	if( !strcmp( Cmd_Argv( i ), "-c" ) || !strcmp( Cmd_Argv( i ), "--compressed" ) ) {
		compressed = qtrue;
		i++;
	}

	if( i >= Cmd_Argc() ) {
		Com_Printf( "Usage: %s [-c|--compressed] [/]<filename>\n", Cmd_Argv( 0 ) );
		return;
	}

	if( cls.demorecording ) {
		Com_Printf( "Already recording.\n" );
		return;
	}

	if( cls.state != ca_active ) {
		Com_Printf( "You must be in a level to record.\n" );
		return;
	}

	//
	// open the demo file
	//
	string = Cmd_Argv( i );
	if( *string == '/' ) {
		Q_strncpyz( name, string + 1, sizeof( name ) );
	} else {
		Q_concat( name, sizeof( name ), "demos/", string, NULL );
    	COM_AppendExtension( name, ".dm2", sizeof( name ) );
	}
	if( compressed ) {
		Q_strcat( name, sizeof( name ), ".gz" );
	}

	FS_FOpenFile( name, &demofile, FS_MODE_WRITE );
	if( !demofile ) {
		Com_EPrintf( "Couldn't open %s for writing.\n", name );
		return;
	}

	Com_Printf( "Recording client demo to %s.\n", name );

	cls.demorecording = demofile;

    // the first frame will be delta uncompressed
    cl.demoframe = -1;

	if( cls.netchan && cls.serverProtocol >= PROTOCOL_VERSION_R1Q2 ) {
		// tell the server we are recording
		MSG_WriteByte( clc_setting );
		MSG_WriteShort( CLS_RECORDING );
		MSG_WriteShort( 1 );
		MSG_FlushTo( &cls.netchan->message );
	}

	//
	// write out messages to hold the startup information
	//

	// send the serverdata
	MSG_WriteByte( svc_serverdata );
	MSG_WriteLong( PROTOCOL_VERSION_DEFAULT );
	MSG_WriteLong( 0x10000 + cl.servercount );
	MSG_WriteByte( 1 );	// demos are always attract loops
	MSG_WriteString( cl.gamedir );
    MSG_WriteShort( cl.clientNum );
	MSG_WriteString( cl.configstrings[CS_NAME] );

	// configstrings
	for( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		string = cl.configstrings[i];
		if( !string[0] ) {
			continue;
		}

		length = strlen( string );
		if( length > MAX_QPATH ) {
			length = MAX_QPATH;
		}
		
        if( msg_write.cursize + length + 4 > MAX_PACKETLEN_WRITABLE_DEFAULT ) {
            CL_WriteDemoMessage( &msg_write );
        }

		MSG_WriteByte( svc_configstring );
		MSG_WriteShort( i );
		MSG_WriteData( string, length );
		MSG_WriteByte( 0 );
	}

	// baselines
	for( i = 1; i < MAX_EDICTS; i++ ) {
		ent = &cl.baselines[i];
		if( !ent->number ) {
			continue;
		}

        if( msg_write.cursize + 64 > MAX_PACKETLEN_WRITABLE_DEFAULT ) {
            CL_WriteDemoMessage( &msg_write );
        }

		MSG_WriteByte( svc_spawnbaseline );		
		MSG_WriteDeltaEntity( NULL, ent, MSG_ES_FORCE );
	}

	MSG_WriteByte( svc_stufftext );
	MSG_WriteString( "precache\n" );

	// write it to the demo file
    CL_WriteDemoMessage( &msg_write );

	// the rest of the demo file will be individual frames
}

/*
====================
CL_ReadNextDemoMessage
====================
*/
static qboolean CL_ReadNextDemoMessage( fileHandle_t f ) {
	int		msglen;

	// read msglen
	if( FS_Read( &msglen, 4, f ) != 4 ) {
		return qfalse;
	}

	if( msglen == -1 ) {
		return qfalse;
	}

	msglen = LittleLong( msglen );
	if( msglen < 1 || msglen >= msg_read.maxsize ) {
		return qfalse;
	}

	msg_read.cursize = msglen;
	msg_read.readcount = 0;

	// read packet data
	if( FS_Read( msg_read.data, msglen, f ) != msglen ) {
		return qfalse;
	}

	return qtrue;

}

/*
====================
CL_ParseNextDemoMessage
====================
*/
static void CL_ParseNextDemoMessage( void ) {
	int pos;
	char *s;

	if( !CL_ReadNextDemoMessage( cls.demoplayback ) ) {
		s = Cvar_VariableString( "nextserver" );
		if( !s[0] ) {
			Com_Error( ERR_SILENT, "Demo finished" );
		}
		Cbuf_AddText( s );
		Cbuf_AddText( "\n" );
		Cvar_Set( "nextserver", "" );
		cls.state = ca_connected;
		return;
	}

	CL_ParseServerMessage();

	if( cls.demofileSize ) {
		pos = FS_RawTell( cls.demoplayback ) - cls.demofileFrameOffset;
		if( pos < 0 ) {
			pos = 0;
		}
		cls.demofilePercent = pos * 100 / cls.demofileSize;
	}
}

/*
====================
CL_PlayDemo_f
====================
*/
static void CL_PlayDemo_f( void ) {
	char name[MAX_OSPATH];
	fileHandle_t demofile;
	char *arg;
	int length;
    int argc = Cmd_Argc();

	if( argc < 2 ) {
		Com_Printf( "Usage: %s <filename> [...]\n", Cmd_Argv( 0 ) );
		return;
	}

	demofile = 0;
	length = 0;

	arg = Cmd_Argv( 1 );
	if( arg[0] == '/' ) {
		// Assume full path is given
		Q_strncpyz( name, arg + 1, sizeof( name ) );
		FS_FOpenFile( name, &demofile, FS_MODE_READ );
	} else {
		// Search for matching extensions
		Q_concat( name, sizeof( name ), "demos/", arg, NULL );
		FS_FOpenFile( name, &demofile, FS_MODE_READ );	
        if( !demofile ) {
			COM_AppendExtension( name, ".dm2", sizeof( name ) );
			FS_FOpenFile( name, &demofile, FS_MODE_READ );
        }
    }

	if( !demofile ) {
		Com_Printf( "Couldn't open %s\n", name );
		return;
	}

#if 0
    // add trailing filenames to play list
    for( i = 2; i < argc; i++ ) {
        arg = Cmd_Argv( i );
        length = strlen( arg );
        entry = Z_Malloc( sizeof( *entry ) + length );
        memcpy( entry->filename, arg, length + 1 );
    }
#endif

	if( sv_running->integer ) {
		// if running a local server, kill it and reissue
		SV_Shutdown( "Server was killed\n", KILL_DROP );
	}

	CL_Disconnect( ERR_DISCONNECT, NULL );
	
	Con_Close();

	cls.demoplayback = demofile;
	cls.state = ca_connected;
	Q_strncpyz( cls.servername, COM_SkipPath( name ), sizeof( cls.servername ) );
	cls.serverAddress.type = NA_LOOPBACK;

	SCR_UpdateScreen();

	do {
		CL_ParseNextDemoMessage();
	    Cbuf_Execute();
	} while( cls.state == ca_connected );

	length = FS_GetFileLengthNoCache( demofile );
    if( length > 0 ) {
    	cls.demofileFrameOffset = FS_Tell( demofile );
	    cls.demofileSize = length - cls.demofileFrameOffset;
    } else {
    	cls.demofileFrameOffset = 0;
	    cls.demofileSize = 0;
    }

	if( com_timedemo->integer ) {
		cls.timeDemoFrames = 0;
		cls.timeDemoStart = Sys_Milliseconds();
	}
}

static const char *CL_PlayDemo_g( const char *partial, int state ) {
	return Com_FileNameGeneratorByFilter( "demos", "*.dm2;*.dm2.gz",
        partial, qfalse, state );
}

/*
====================
CL_GetDemoInfo
====================
*/
qboolean CL_GetDemoInfo( const char *path, demoInfo_t *info ) {
	fileHandle_t hFile;
	int c, protocol;
	char *s, *p;

	memset( info, 0, sizeof( *info ) );

	FS_FOpenFile( path, &hFile, FS_MODE_READ );
	if( !hFile ) {
		return qfalse;
	}

	if( !CL_ReadNextDemoMessage( hFile ) ) {
		goto fail;
	}

	if( MSG_ReadByte() != svc_serverdata ) {
		goto fail;
	}

	protocol = MSG_ReadLong();

	msg_read.readcount += 5;

	Q_strncpyz( info->gamedir, MSG_ReadString(), sizeof( info->gamedir ) );

	info->clientNum = MSG_ReadShort();

	Q_strncpyz( info->fullLevelName, MSG_ReadString(), sizeof( info->fullLevelName ) );

	switch( protocol ) {
	case PROTOCOL_VERSION_MVD:
		info->mvd = qtrue;
		msg_read.readcount += 2;
		break;
	case PROTOCOL_VERSION_R1Q2:
		msg_read.readcount += 5;
		break;
	case PROTOCOL_VERSION_Q2PRO:
		msg_read.readcount += 5;
		break;
	default:
		break;
	}

	while( 1 ) {
		c = MSG_ReadByte();
		if( c == -1 ) {
			if( !CL_ReadNextDemoMessage( hFile ) ) {
				break;
			}
			continue; // parse new message
		}
		if( c != svc_configstring ) {
			break;
		}
		c = MSG_ReadShort();
		s = MSG_ReadString();
		if( c >= CS_PLAYERSKINS && c < CS_PLAYERSKINS + MAX_DEMOINFO_CLIENTS ) {
			c -= CS_PLAYERSKINS;
			Q_strncpyz( info->clients[c], s, sizeof( info->clients[0] ) );
			if( ( p = strchr( info->clients[c], '\\' ) ) != NULL ) {
				*p = 0;
			}
		} else if( c == CS_MODELS + 1 ) {
			if( strlen( s ) > 9 ) {
				Q_strncpyz( info->mapname, s + 5, sizeof( info->mapname ) ); // skip "maps/"
				info->mapname[ strlen( info->mapname ) - 4 ] = 0; // cut off ".bsp"
			}
		}
	}

	FS_FCloseFile( hFile );
	return qtrue;

fail:
	FS_FCloseFile( hFile );
	return qfalse;

}

// =========================================================================


/*
====================
CL_DemoFrame
====================
*/
void CL_DemoFrame( void ) {
	if( cls.state < ca_connected ) {
        return;
    }
	if( cls.state != ca_active ) {
		CL_ParseNextDemoMessage();
		return;
	}

	if( com_timedemo->integer ) {
		CL_ParseNextDemoMessage();
		cl.time = cl.serverTime;
		cls.timeDemoFrames++;
		return;
	}

	while( cl.serverTime < cl.time ) {
		CL_ParseNextDemoMessage();
		if( cls.state != ca_active ) {
			break;
		}
	}
}

static const cmdreg_t c_demo[] = {
    { "demo", CL_PlayDemo_f, CL_PlayDemo_g },
    { "record", CL_Record_f, CL_PlayDemo_g },
    { "stop", CL_Stop_f },

    { NULL }
};

/*
====================
CL_InitDemos
====================
*/
void CL_InitDemos( void ) {
	Cmd_Register( c_demo );
}


