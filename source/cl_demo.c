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
    if( !buf->cursize ) {
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

    if( cls.demopaused ) {
        return;
    }

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
    cl.demodelta++; // insert new zero frame

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
    cls.demopaused = qfalse;

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
    cls.demopaused = qfalse;

    // the first frame will be delta uncompressed
    cl.demoframe = -1;
    cl.demodelta = 0;

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

static void CL_Suspend_f( void ) {
    int i, j, index;
    int length, total = 0;

	if( !cls.demorecording ) {
		Com_Printf( "Not recording a demo.\n" );
		return;
	}
    if( !cls.demopaused ) {
		Com_Printf( "Suspended demo.\n" );
        cls.demopaused = qtrue;
        return;
    }

    // XXX: embed these in frame instead?
    for( i = 0; i < CS_BITMAP_LONGS; i++ ) {
        if( (( uint32_t * )cl.dcs)[i] == 0 ) {
            continue;
        }
        index = i << 5;
        for( j = 0; j < 32; j++, index++ ) {
            if( !Q_IsBitSet( cl.dcs, index ) ) {
                continue;
            }
            length = strlen( cl.configstrings[index] );
            if( length > MAX_QPATH ) {
                length = MAX_QPATH;
            }
            if( msg_write.cursize + length + 4 > MAX_PACKETLEN_WRITABLE_DEFAULT ) {
                CL_WriteDemoMessage( &msg_write );
            }
            MSG_WriteByte( svc_configstring );
            MSG_WriteShort( index );
		    MSG_WriteData( cl.configstrings[index], length );
    		MSG_WriteByte( 0 );
            total += length + 4;
        }
    }

	// write it to the demo file
    CL_WriteDemoMessage( &msg_write );

    Com_Printf( "Resumed demo (%d bytes flushed).\n", total );

    cl.demodelta += cl.demoframe - cl.frame.number; // do not create holes
    cls.demopaused = qfalse;

    // clear dirty configstrings
    memset( cl.dcs, 0, sizeof( cl.dcs ) );
}

static int CL_ReadFirstDemoMessage( fileHandle_t f ) {
	uint32_t	ul;
    uint16_t    us;
    unsigned    msglen;
    int         type;

    // read magic/msglen
    if( FS_Read( &ul, 4, f ) != 4 ) {
        Com_DPrintf( "%s: short read of msglen\n", __func__ );
        return -1;
    }

    if( ul == MVD_MAGIC ) {
        if( FS_Read( &us, 2, f ) != 2 ) {
            Com_DPrintf( "%s: short read of msglen\n", __func__ );
            return -1;
        }
        if( us == ( uint16_t )-1 ) {
            Com_DPrintf( "%s: end of demo\n", __func__ );
            return -1;
        }
        msglen = LittleShort( us );
        type = 1;
    } else {
        if( ul == ( uint32_t )-1 ) {
            Com_DPrintf( "%s: end of demo\n", __func__ );
            return -1;
        }
	    msglen = LittleLong( ul );
        type = 0;
    }

	if( msglen >= sizeof( msg_read_buffer ) ) {
        Com_DPrintf( "%s: bad msglen\n", __func__ );
		return -1;
	}

    SZ_Init( &msg_read, msg_read_buffer, sizeof( msg_read_buffer ) );
	msg_read.cursize = msglen;

	// read packet data
	if( FS_Read( msg_read.data, msglen, f ) != msglen ) {
        Com_DPrintf( "%s: short read of data\n", __func__ );
		return -1;
	}

	return type;
}

/*
====================
CL_ReadNextDemoMessage
====================
*/
static qboolean CL_ReadNextDemoMessage( fileHandle_t f ) {
	uint32_t		msglen;

	// read msglen
	if( FS_Read( &msglen, 4, f ) != 4 ) {
        Com_DPrintf( "%s: short read of msglen\n", __func__ );
		return qfalse;
	}

	if( msglen == ( uint32_t )-1 ) {
        Com_DPrintf( "%s: end of demo\n", __func__ );
		return qfalse;
	}

	msglen = LittleLong( msglen );
	if( msglen >= sizeof( msg_read_buffer ) ) {
        Com_DPrintf( "%s: bad msglen\n", __func__ );
		return qfalse;
	}

    SZ_Init( &msg_read, msg_read_buffer, sizeof( msg_read_buffer ) );
	msg_read.cursize = msglen;

	// read packet data
	if( FS_Read( msg_read.data, msglen, f ) != msglen ) {
        Com_DPrintf( "%s: short read of data\n", __func__ );
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
	int length, type;
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

    type = CL_ReadFirstDemoMessage( demofile );
    if( type == -1 ) {
		Com_Printf( "%s is not a demo file\n", name );
        FS_FCloseFile( demofile );
        return;
    }

    if( type == 1 ) {
		Com_DPrintf( "%s is a MVD file\n", name );
        Cbuf_InsertText( va( "mvdplay /%s\n", name ) );
        FS_FCloseFile( demofile );
        return;
    }

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

	CL_ParseServerMessage();
	while( cls.state == ca_connected ) {
	    Cbuf_Execute();
		CL_ParseNextDemoMessage();
    }

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

static void CL_ParseInfoString( demoInfo_t *info, int clientNum, int index, const char *string ) {
    int len;
    char *p;

    if( index >= CS_PLAYERSKINS && index < CS_PLAYERSKINS + MAX_CLIENTS ) {
        if( index - CS_PLAYERSKINS == clientNum ) {
            p = strchr( string, '\\' );
            if( p ) {
                *p = 0;
            }
            Q_strncpyz( info->pov, string, sizeof( info->pov ) );
        }
    } else if( index == CS_MODELS + 1 ) {
        if( strlen( string ) > 9 ) {
            len = Q_strncpyz( info->map, string + 5, sizeof( info->map ) ); // skip "maps/"
            info->map[ len - 4 ] = 0; // cut off ".bsp"
        }
    }
}

/*
====================
CL_GetDemoInfo
====================
*/
demoInfo_t *CL_GetDemoInfo( const char *path, demoInfo_t *info ) {
	fileHandle_t f;
	int c, index;
	char *string;
    int clientNum, type;

	FS_FOpenFile( path, &f, FS_MODE_READ );
	if( !f ) {
		return NULL;
	}

    type = CL_ReadFirstDemoMessage( f );
    if( type == -1 ) {
        goto fail;
    }

    if( type == 0 ) {
        if( MSG_ReadByte() != svc_serverdata ) {
            goto fail;
        }
        if( MSG_ReadLong() != PROTOCOL_VERSION_DEFAULT ) {
            goto fail;
        }
        MSG_ReadLong();
        MSG_ReadByte();
        MSG_ReadString();
        clientNum = MSG_ReadShort();
        MSG_ReadString();

        while( 1 ) {
            c = MSG_ReadByte();
            if( c == -1 ) {
                if( !CL_ReadNextDemoMessage( f ) ) {
                    break;
                }
                continue; // parse new message
            }
            if( c != svc_configstring ) {
                break;
            }
            index = MSG_ReadShort();
            if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
                goto fail;
            }
            string = MSG_ReadString();
            CL_ParseInfoString( info, clientNum, index, string );
        }
    } else {
        if( MSG_ReadByte() != mvd_serverdata ) {
            goto fail;
        }
        if( MSG_ReadLong() != PROTOCOL_VERSION_MVD ) {
            goto fail;
        }
        MSG_ReadShort();
        MSG_ReadLong();
        MSG_ReadString();
        clientNum = MSG_ReadShort();

        while( 1 ) {
            index = MSG_ReadShort();
            if( index == MAX_CONFIGSTRINGS ) {
                break;
            }
            if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
                goto fail;
            }
            string = MSG_ReadString();
            CL_ParseInfoString( info, clientNum, index, string );
        }
    }

	FS_FCloseFile( f );
	return info;

fail:
	FS_FCloseFile( f );
	return NULL;

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
		cl.time = cl.servertime;
		cls.timeDemoFrames++;
		return;
	}

	while( cl.servertime < cl.time ) {
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
    { "suspend", CL_Suspend_f },

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


