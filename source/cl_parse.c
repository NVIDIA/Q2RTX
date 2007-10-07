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
// cl_parse.c  -- parse a message received from the server

#include "cl_local.h"

//=============================================================================


static const char *const validExts[] = {
    ".pcx", ".wal", ".tga", ".jpg", ".png",
    ".md2", ".md3", ".sp2", ".wav", ".dm2",
    ".bsp", ".txt", ".loc", ".ent", NULL
};

/*
===============
CL_CheckOrDownloadFile

Returns qtrue if the file exists, otherwise it attempts
to start a download from the server.
===============
*/
qboolean CL_CheckOrDownloadFile( const char *path ) {
	fileHandle_t	f;
	int		i, length;
	char filename[MAX_QPATH];
	char *ext;

	Q_strncpyz( filename, path, sizeof( filename ) );
	Q_strlwr( filename );

	length = strlen( filename );
	if( !length
		|| !Q_ispath( filename[0] )
		|| !Q_ispath( filename[ length - 1 ] )
		|| strchr( filename, '\\' )
		|| strchr( filename, ':' )
		|| !strchr( filename, '/' )
		|| strstr( filename, ".." ) )
	{
		Com_WPrintf( "Refusing to download file with invalid path.\n" );
		return qtrue;
	}

	// a trivial attempt to prevent malicious server from
	// uploading trojan executables to the win32 client
	ext = COM_FileExtension( filename );
    for( i = 0; validExts[i]; i++ ) {
	    if( !strcmp( ext, validExts[i] ) ) {
            break;
        }
	}
    if( !validExts[i] ) {
		Com_WPrintf( "Refusing to download file with invalid extension.\n" );
		return qtrue;
    }

	if( FS_LoadFile( filename, NULL ) != -1 ) {	
		// it exists, no need to download
		return qtrue;
	}

	strcpy( cls.downloadname, filename );

	// download to a temp name, and only rename
	// to the real name when done, so if interrupted
	// a runt file wont be left
	COM_StripExtension( cls.downloadname, cls.downloadtempname, MAX_QPATH );
	
	if( strlen( cls.downloadtempname ) >= MAX_QPATH - 5 ) {
		strcpy( cls.downloadtempname + MAX_QPATH - 5, ".tmp" );
	} else {
		strcat( cls.downloadtempname, ".tmp" );
	}

//ZOID
	// check to see if we already have a tmp for this file, if so, try to resume
	// open the file if not opened yet
	length = FS_FOpenFile( cls.downloadtempname, &f, FS_MODE_RDWR|FS_FLAG_RAW );
	if( length < 0 && f ) {
		Com_WPrintf( "Couldn't determine size of %s\n", cls.downloadtempname );
		FS_FCloseFile( f );
		f = 0;
	}
	if( f ) { // it exists
		cls.download = f;
		// give the server an offset to start the download
		Com_Printf( "Resuming %s\n", cls.downloadname );
		CL_ClientCommand( va( "download \"%s\" %i", cls.downloadname, length ) );
	} else {
		Com_Printf( "Downloading %s\n", cls.downloadname );
		CL_ClientCommand( va( "download \"%s\"", cls.downloadname ) );
	}

	cls.downloadnumber++;

	return qfalse;
}

/*
===============
CL_Download_f

Request a download from the server
===============
*/
void CL_Download_f( void ) {
	char *path;

	if( cls.state < ca_connected ) {
		Com_Printf( "Must be connected to a server.\n" );
		return;
	}

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: download <filename>\n" );
		return;
	}

	path = Cmd_Argv( 1 );

	if( !allow_download->integer ) {
		Com_Printf( "Couldn't download '%s', "
            "downloading is locally disabled.\n", path );
		return;
	}

	if( cls.downloadtempname[0] ) {
		Com_Printf( "Already downloading.\n" );
		if( cls.serverProtocol == PROTOCOL_VERSION_Q2PRO ) {
			Com_Printf( "Try using 'stopdl' command to abort the download.\n" );
		}
		return;
	}

	if( FS_LoadFile( path, NULL ) != -1 ) {	
		Com_Printf( "File '%s' already exists.\n", path );
		return;
	}

	CL_CheckOrDownloadFile( path );
}


/*
=====================
CL_ParseDownload

A download message has been received from the server
=====================
*/
static void CL_ParseDownload( void ) {
	int		size, percent;

	if( !cls.downloadtempname[0] ) {
		Com_Error( ERR_DROP, "Server sending download, but "
            "no download was requested" );
	}

	// read the data
	size = MSG_ReadShort();
	percent = MSG_ReadByte();
	if( size == -1 ) {
		if( !percent ) {
			Com_Printf( "Server was unable to send this file.\n" );
		} else {
			Com_Printf( "Server stopped the download.\n" );
		}
		if( cls.download ) {
			// if here, we tried to resume a file but the server said no
			FS_FCloseFile( cls.download );
			cls.download = 0;
		}
		cls.downloadtempname[0] = 0;
		cls.downloadname[0] = 0;
		CL_RequestNextDownload();
		return;
	}

	if( size < 0 ) {
		Com_Error( ERR_DROP, "CL_ParseDownload: bad size: %d", size );
	}

	if( msg_read.readcount + size > msg_read.cursize ) {
		Com_Error( ERR_DROP, "CL_ParseDownload: read past end of message" );
	}

	// open the file if not opened yet
	if( !cls.download ) {
		FS_FOpenFile( cls.downloadtempname, &cls.download,
            FS_MODE_WRITE|FS_FLAG_RAW );

		if( !cls.download ) {
			msg_read.readcount += size;
			Com_WPrintf( "Failed to open '%s' for writing\n",
                cls.downloadtempname );
			cls.downloadtempname[0] = 0;
			cls.downloadname[0] = 0;
			CL_RequestNextDownload();
			return;
		}
	}

	FS_Write( msg_read.data + msg_read.readcount, size, cls.download );
	msg_read.readcount += size;
	
	if( percent != 100 ) {
		// request next block
		// change display routines by zoid
		cls.downloadpercent = percent;

		CL_ClientCommand( "nextdl" );
	} else {
		FS_FCloseFile( cls.download );

		// rename the temp file to it's final name
		if( !FS_RenameFile( cls.downloadtempname, cls.downloadname ) ) {
			Com_WPrintf( "Failed to rename %s to %s\n",
				cls.downloadtempname, cls.downloadname );
		}

		Com_Printf( "Downloaded successfully.\n" );

		cls.downloadtempname[0] = 0;
		cls.downloadname[0] = 0;

		cls.download = 0;
		cls.downloadpercent = 0;

		// get another file if needed
		CL_RequestNextDownload();
	}
}

/*
=====================================================================

  DELTA FRAME PARSING

=====================================================================
*/


/*
==================
CL_ParseDeltaEntity
==================
*/
static inline void CL_ParseDeltaEntity( server_frame_t  *frame,
                                        int             newnum,
                                        entity_state_t  *old,
                                        int             bits )
{
	entity_state_t	*state;

	if( frame->numEntities == MAX_PACKET_ENTITIES ) {
		Com_Error( ERR_DROP, "CL_ParseDeltaEntity: MAX_PACKET_ENTITIES exceeded" );
	}

	state = &cl.entityStates[cl.numEntityStates & PARSE_ENTITIES_MASK];
	cl.numEntityStates++;
	frame->numEntities++;

	if( cl_shownet->integer > 2 ) {
		MSG_ShowDeltaEntityBits( bits );
	}

	MSG_ParseDeltaEntity( old, state, newnum, bits );
}

/*
==================
CL_ParsePacketEntities
==================
*/
static void CL_ParsePacketEntities( server_frame_t *oldframe,
                                    server_frame_t *frame )
{
	int			newnum;
	int			bits;
	entity_state_t	*oldstate;
	int			oldindex, oldnum;
	int i;

	frame->firstEntity = cl.numEntityStates;
	frame->numEntities = 0;

	// delta from the entities present in oldframe
	oldindex = 0;
	oldstate = NULL;
	if( !oldframe ) {
		oldnum = 99999;
	} else {
		if( oldindex >= oldframe->numEntities ) {
			oldnum = 99999;
		} else {
			i = oldframe->firstEntity + oldindex;
			oldstate = &cl.entityStates[i & PARSE_ENTITIES_MASK];
			oldnum = oldstate->number;
		}
	}

	while( 1 ) {
		newnum = MSG_ParseEntityBits( &bits );
		if( newnum < 0 || newnum >= MAX_EDICTS ) {
			Com_Error( ERR_DROP, "ParsePacketEntities: bad number %i", newnum );
		}

		if( msg_read.readcount > msg_read.cursize ) {
			Com_Error( ERR_DROP, "ParsePacketEntities: end of message" );
		}

		if( !newnum ) {
			break;
		}

		while( oldnum < newnum ) {
			// one or more entities from the old packet are unchanged
			if( cl_shownet->integer > 2 ) {
				Com_Printf( "   unchanged: %i\n", oldnum );
			}
			CL_ParseDeltaEntity( frame, oldnum, oldstate, 0 );
			
			oldindex++;

			if( oldindex >= oldframe->numEntities ) {
				oldnum = 99999;
			} else {
				i = oldframe->firstEntity + oldindex;
				oldstate = &cl.entityStates[i & PARSE_ENTITIES_MASK];
				oldnum = oldstate->number;
			}
		}

		if( bits & U_REMOVE ) {	
			// the entity present in oldframe is not in the current frame
			if( cl_shownet->integer > 2 ) {
				Com_Printf( "   remove: %i\n", newnum );
			}
			if( oldnum != newnum ) {
				Com_DPrintf( "U_REMOVE: oldnum != newnum\n" );
			}
			if( !oldframe ) {
				Com_Error( ERR_DROP, "U_REMOVE: NULL oldframe" );
			}

			oldindex++;

			if( oldindex >= oldframe->numEntities ) {
				oldnum = 99999;
			} else {
				i = oldframe->firstEntity + oldindex;
				oldstate = &cl.entityStates[i & PARSE_ENTITIES_MASK];
				oldnum = oldstate->number;
			}
			continue;
		}

		if( oldnum == newnum ) {	
			// delta from previous state
			if( cl_shownet->integer > 2 ) {
				Com_Printf( "   delta: %i ", newnum );
			}
			CL_ParseDeltaEntity( frame, newnum, oldstate, bits );
			if( cl_shownet->integer > 2 ) {
				Com_Printf( "\n" );
			}

			oldindex++;

			if( oldindex >= oldframe->numEntities ) {
				oldnum = 99999;
			} else {
				i = oldframe->firstEntity + oldindex;
				oldstate = &cl.entityStates[i & PARSE_ENTITIES_MASK];
				oldnum = oldstate->number;
			}
			continue;
		}

		if( oldnum > newnum ) {	
			// delta from baseline
			if( cl_shownet->integer > 2 ) {
				Com_Printf( "   baseline: %i ", newnum );
			}
			CL_ParseDeltaEntity( frame, newnum, &cl.baselines[newnum], bits );
			if( cl_shownet->integer > 2 ) {
				Com_Printf( "\n" );
			}
			continue;
		}

	}

	// any remaining entities in the old frame are copied over
	while( oldnum != 99999 ) {	
		// one or more entities from the old packet are unchanged
		if( cl_shownet->integer > 2 ) {
			Com_Printf( "   unchanged: %i\n", oldnum );
		}
		CL_ParseDeltaEntity( frame, oldnum, oldstate, 0 );
		
		oldindex++;

		if( oldindex >= oldframe->numEntities ) {
			oldnum = 99999;
		} else {
			i = oldframe->firstEntity + oldindex;
			oldstate = &cl.entityStates[i & PARSE_ENTITIES_MASK];
			oldnum = oldstate->number;
		}
	}
}

/*
================
CL_SetActiveState
================
*/
static void CL_SetActiveState( void ) {
	cl.time = cl.serverTime; // set time, needed for demos
	cls.state = ca_active;
	cl.oldframe.valid = qfalse;
    cl.frameflags = 0;
    cl.putaway = qfalse;

	if( !cls.demoplayback ) {
        VectorScale( cl.frame.ps.pmove.origin, 0.125f, cl.predicted_origin );
		VectorCopy( cl.frame.ps.viewangles, cl.predicted_angles );
	}
	
	SCR_ClearLagometer();
	SCR_ClearChatHUD_f();
	SCR_EndLoadingPlaque ();	// get rid of loading plaque
	Con_Close();				// close console

    EXEC_TRIGGER( cl_beginmapcmd );

	Cvar_Set( "cl_paused", "0" );
}

/*
================
CL_ParseFrame
================
*/
static void CL_ParseFrame( int extrabits ) {
	uint32  bits, extraflags;
	int     currentframe, deltaframe,
            delta, surpressed;
	server_frame_t  frame, *oldframe;
	player_state_t  *from;
	int     length;
	
	memset( &frame, 0, sizeof( frame ) );

    cl.frameflags = 0;

    surpressed = 0;
	extraflags = 0;
	if( cls.serverProtocol > PROTOCOL_VERSION_DEFAULT ) {
		bits = MSG_ReadLong();

		currentframe = bits & FRAMENUM_MASK;
		delta = bits >> FRAMENUM_BITS;

		if( delta == 31 ) {
			deltaframe = -1;
		} else {
			deltaframe = currentframe - delta;
		}

		bits = MSG_ReadByte();

		surpressed = bits & SURPRESSCOUNT_MASK;
	    if( cls.serverProtocol == PROTOCOL_VERSION_Q2PRO ) {
            cl.frameflags |= surpressed;
        } else if( surpressed ) {
            cl.frameflags |= FF_SURPRESSED;
        }
		extraflags = ( extrabits << 4 ) | ( bits >> SURPRESSCOUNT_BITS );
	} else {
		currentframe = MSG_ReadLong();
		deltaframe = MSG_ReadLong();

		// BIG HACK to let old demos continue to work
		if( cls.serverProtocol != PROTOCOL_VERSION_OLD ) {
			surpressed = MSG_ReadByte();
            if( surpressed ) {
                cl.frameflags |= FF_SURPRESSED;
            }
		}
	}

	frame.number = currentframe;
	frame.delta = deltaframe;

    if( cls.netchan && cls.netchan->dropped ) {
        cl.frameflags |= FF_SERVERDROP;
    }

	/* If the frame is delta compressed from data that we
	 * no longer have available, we must suck up the rest of
	 * the frame, but not use it, then ask for a non-compressed
	 * message */
	if( deltaframe > 0 ) {
		oldframe = &cl.frames[deltaframe & UPDATE_MASK];
		from = &oldframe->ps;
		if( deltaframe == currentframe ) {
            // old buggy q2 servers still cause this on map change
			Com_DPrintf( "Delta from current frame.\n" );
            cl.frameflags |= FF_BADFRAME;
		} else if( oldframe->number != deltaframe ) {
			// The frame that the server did the delta from
			// is too old, so we can't reconstruct it properly.
			Com_DPrintf( "Delta frame was never received or too old.\n" );
            cl.frameflags |= FF_OLDFRAME;
		} else if( !oldframe->valid ) {	
			// should never happen
			Com_DPrintf( "Delta from invalid frame.\n" );
            cl.frameflags |= FF_BADFRAME;
		} else if( cl.numEntityStates - oldframe->firstEntity >
			MAX_PARSE_ENTITIES - MAX_PACKET_ENTITIES )
		{
			Com_DPrintf( "Delta entities too old.\n" );
            cl.frameflags |= FF_OLDENT;
		} else {
			frame.valid = qtrue;	// valid delta parse
		}
        if( !frame.valid && cl.frame.valid && cls.demoplayback ) {
            Com_DPrintf( "Trying to recover from the broken demo recording.\n" );
            oldframe = &cl.frame;
            from = &oldframe->ps;
            frame.valid = qtrue;
        }
	} else {
		oldframe = NULL;
		from = NULL;
		frame.valid = qtrue;		// uncompressed frame
        //if( !cls.demowaiting ) {
            cl.frameflags |= FF_NODELTA;
        //}
    	//cls.demowaiting = qfalse;   // we can start recording now
	}

	// read areabits
	length = MSG_ReadByte();
	if( length ) {
		if( length < 0 || msg_read.readcount + length > msg_read.cursize ) {
			Com_Error( ERR_DROP, "CL_ParseFrame: read past end of message" );
		}
		if( length > sizeof( frame.areabits ) ) {
			Com_Error( ERR_DROP, "CL_ParseFrame: invalid areabits length" );
		}
		MSG_ReadData( frame.areabits, length );
		frame.areabytes = length;
	} else {
		frame.areabytes = 0;
	}

	if( cls.serverProtocol <= PROTOCOL_VERSION_DEFAULT ) {
		if( MSG_ReadByte() != svc_playerinfo ) {
			Com_Error( ERR_DROP, "CL_ParseFrame: not playerinfo" );
		}
	}

	if( cl_shownet->integer > 2 ) {
		Com_Printf( "%3i:playerinfo\n", msg_read.readcount - 1 );
	}

	frame.clientNum = cl.clientNum;

	// parse playerstate
	bits = MSG_ReadShort();
	if( cls.serverProtocol > PROTOCOL_VERSION_DEFAULT ) {
		MSG_ParseDeltaPlayerstate_Enhanced( from, &frame.ps, bits, extraflags );
		if( cl_shownet->integer > 2 ) {
			MSG_ShowDeltaPlayerstateBits_Enhanced( bits );
			Com_Printf( "\n" );
		}
		if( cls.serverProtocol == PROTOCOL_VERSION_Q2PRO ) {
            // parse clientNum
	        if( extraflags & EPS_CLIENTNUM ) {
		        frame.clientNum = MSG_ReadByte();
	        } else if( oldframe ) {
                frame.clientNum = oldframe->clientNum;
            }
        }
	} else {
		MSG_ParseDeltaPlayerstate_Default( from, &frame.ps, bits );
		if( cl_shownet->integer > 2 ) {
			MSG_ShowDeltaPlayerstateBits_Default( bits );
			Com_Printf( "\n" );
		}
	}
	if( !frame.ps.fov ) {
        // fail out early to prevent spurious errors later
        Com_Error( ERR_DROP, "CL_ParseFrame: bad fov" );
    }

	// parse packetentities
	if( cls.serverProtocol <= PROTOCOL_VERSION_DEFAULT ) {
		if( MSG_ReadByte() != svc_packetentities ) {
			Com_Error( ERR_DROP, "CL_ParseFrame: not packetentities" );
		}
	}

	if( cl_shownet->integer > 2 ) {
		Com_Printf( "%3i:packetentities\n", msg_read.readcount - 1 );
	}

	CL_ParsePacketEntities( oldframe, &frame );

	// save the frame off in the backup array for later delta comparisons
	cl.frames[currentframe & UPDATE_MASK] = frame;

	if( cl_shownet->integer > 2 ) {
        int rtt = 0;
		if( cls.netchan ) {
            int seq = cls.netchan->incoming_acknowledged & CMD_MASK;
			rtt = cls.realtime - cl.history[seq].realtime;
		}
		Com_Printf( "%3i: frame:%i  delta:%i  rtt:%i\n",
			msg_read.readcount - 1, frame.number, frame.delta, rtt );
	}

	if( !frame.valid ) {
		cl.frame.valid = qfalse;
		return; // do not change anything
	}

	cl.oldframe = cl.frame;
	cl.frame = frame;
	cl.serverTime = frame.number * 100;

	// getting a valid frame message ends the connection process
	if( cls.state == ca_precached ) {
		CL_SetActiveState();
	}

	CL_DeltaFrame();

	CL_CheckPredictionError();
}


/*
=====================================================================

  SERVER CONNECTING MESSAGES

=====================================================================
*/

/*
================
CL_ParseClientinfo

Load the skin, icon, and model for a client
================
*/
void CL_ParseClientinfo( int player ) {
	char			*s;
	clientinfo_t	*ci;

	s = cl.configstrings[player+CS_PLAYERSKINS];

	ci = &cl.clientinfo[player];

    if( strcmp( ci->cinfo, s ) ) {
    	CL_LoadClientinfo( ci, s );
    }
}


static void CL_ConfigString( int index, const char *string ) {
	int		length, maxlength;

    if( index >= CS_STATUSBAR && index < CS_AIRACCEL ) {
        maxlength = MAX_QPATH * ( CS_AIRACCEL - index );
    } else {
        maxlength = MAX_QPATH;
    }
    length = strlen( string );
    if( length >= maxlength ) {
		Com_Error( ERR_DROP, "%s: index %d overflowed: %d chars",
            __func__, index, length );
    }

	memcpy( cl.configstrings[index], string, length + 1 );

	// do something apropriate 

	if( index == CS_MAXCLIENTS ) {
		cl.maxclients = atoi( string );
        return;
    }
    if( index == CS_MODELS + 1 ) {
        if( length <= 9 ) {
            Com_Error( ERR_DROP, "%s: bad world model: %s", __func__, string );
        }
        strcpy( cl.mapname, string + 5 ); // skip "maps/"
        cl.mapname[length - 9] = 0; // cut off ".bsp"
        return;
    }
	if (index >= CS_LIGHTS && index < CS_LIGHTS+MAX_LIGHTSTYLES) {
		CL_SetLightstyle (index - CS_LIGHTS);
        return;
    }

	if( cls.state < ca_precached ) {
        return;
    }

	if (index >= CS_MODELS && index < CS_MODELS+MAX_MODELS) {
        cl.model_draw[index-CS_MODELS] = ref.RegisterModel (string);
        if (*string == '*')
            cl.model_clip[index-CS_MODELS] = CM_InlineModel (&cl.cm, string);
        else
            cl.model_clip[index-CS_MODELS] = 0;
	} else if (index >= CS_SOUNDS && index < CS_SOUNDS+MAX_MODELS) {
		cl.sound_precache[index-CS_SOUNDS] = S_RegisterSound (string);
	} else if (index >= CS_IMAGES && index < CS_IMAGES+MAX_MODELS) {
		cl.image_precache[index-CS_IMAGES] = ref.RegisterPic (string);
	} else if (index >= CS_PLAYERSKINS && index < CS_PLAYERSKINS+MAX_CLIENTS) {
		CL_ParseClientinfo (index-CS_PLAYERSKINS);
	} else if( index == CS_AIRACCEL && !cl.pmp.qwmod ) {
		cl.pmp.airaccelerate = atoi( string ) ? qtrue : qfalse;
	}
}

static void CL_ParseGamestate( void ) {
	int		index, bits;
    char    *string;

    while( 1 ) {
        index = MSG_ReadShort();
        if( index == MAX_CONFIGSTRINGS ) {
            break;
        }
        if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
            Com_Error( ERR_DROP, "%s: bad configstring index: %d",
                __func__, index );
        }

        string = MSG_ReadString();

        CL_ConfigString( index, string );
    }

    while( 1 ) {
        index = MSG_ParseEntityBits( &bits );
        if( !index ) {
            break;
        }
        if( index < 1 || index >= MAX_EDICTS ) {
            Com_Error( ERR_DROP, "%s: bad baseline index: %d",
                __func__, index );
        }
        MSG_ParseDeltaEntity( NULL, &cl.baselines[index], index, bits );
    }
}

/*
==================
CL_ParseServerData
==================
*/
static void CL_ParseServerData( void ) {
	char	*str;
	int		i, protocol, attractloop;

	Cbuf_Execute();		// make sure any stuffed commands are done
	
    // wipe the client_state_t struct
	CL_ClearState();

    // parse protocol version number
	protocol = MSG_ReadLong();
	cl.servercount = MSG_ReadLong();
	attractloop = MSG_ReadByte();

	Com_DPrintf( "Serverdata packet received (protocol=%d, servercount=%d, attractloop=%d)\n",
		protocol, cl.servercount, attractloop );

    // check protocol
	if( cls.serverProtocol != protocol ) {
		if( !cls.demoplayback ) { 
			Com_Error( ERR_DROP, "Requested protocol version %d, but server returned %d.",
                cls.serverProtocol, protocol );
        }

        // BIG HACK to let demos from release work with the 3.0x patch!!!
        if( protocol == PROTOCOL_VERSION_OLD ) {
            Com_DPrintf( "Using protocol %d for compatibility with old demos.\n", PROTOCOL_VERSION_OLD );
        } else if( protocol < PROTOCOL_VERSION_DEFAULT || protocol > PROTOCOL_VERSION_Q2PRO ) {
            Com_Error( ERR_DROP, "Demo uses unsupported protocol version %d.", protocol );
        }
		cls.serverProtocol = protocol;
	}

	// game directory
	str = MSG_ReadString();
	Q_strncpyz( cl.gamedir, str, sizeof( cl.gamedir ) );

	// never allow demos to change gamedir
	// do not set gamedir if connected to local sever,
	// since it was already done by SV_InitGame
	if( !cls.demoplayback && !sv_running->integer ) {
		Cvar_UserSet( "game", cl.gamedir );
		if( FS_NeedRestart() ) {
			CL_RestartFilesystem();
		}
	}

	// parse player entity number
	cl.clientNum = MSG_ReadShort();

	// get the full level name
	str = MSG_ReadString();

	cl.pmp.speedMultiplier = 1;
	cl.pmp.maxspeed = 300;
	cl.pmp.upspeed = 350;
	cl.pmp.friction = 6;
	cl.pmp.waterfriction = 1;
    cl.pmp.flyfriction = 9;
	cl.pmp.airaccelerate = 0;	
	cl.gametype = GT_DEATHMATCH;
#ifdef PMOVE_HACK
    cl.pmp.highprec = qtrue;
#endif
	if( cls.serverProtocol == PROTOCOL_VERSION_R1Q2 ) {
		i = MSG_ReadByte();
		if( i ) {
			Com_Error( ERR_DROP, "'Enhanced' R1Q2 servers are not supported" );
		}
		i = MSG_ReadShort();
		if( !R1Q2_SUPPORTED( i ) ) {
			Com_Error( ERR_DROP, "Unsupported R1Q2 protocol version %d.\n"
                "Current client version is %d.", i, PROTOCOL_VERSION_R1Q2_CURRENT );
		}
        cls.protocolVersion = i;
		i = MSG_ReadByte();
		if( i ) { // seems to be no longer used
			Com_DPrintf( "R1Q2 advancedDeltas enabled\n" );
		}
		cl.pmp.strafeHack = MSG_ReadByte();
		if( cl.pmp.strafeHack ) {
			Com_DPrintf( "R1Q2 strafeHack enabled\n" );
		}
		cl.pmp.speedMultiplier = 2;
	} else if( cls.serverProtocol == PROTOCOL_VERSION_Q2PRO ) {
		i = MSG_ReadShort();
		if( !Q2PRO_SUPPORTED( i ) ) {
			Com_Error( ERR_DROP, "Unsupported Q2PRO protocol version %d.\n"
                "Current client version is %d.", i, PROTOCOL_VERSION_Q2PRO_CURRENT );
		}
        cls.protocolVersion = i;
		cl.gametype = MSG_ReadByte();
		cl.pmp.strafeHack = MSG_ReadByte();
		cl.pmp.qwmod = MSG_ReadByte(); //atu QWMod
		cl.pmp.speedMultiplier = 2;		
        cl.pmp.flyfix = qtrue;
        cl.pmp.flyfriction = 4;

		if( cl.pmp.strafeHack ) {
			Com_DPrintf( "Q2PRO strafeHack enabled\n" );
		}
		if( cl.pmp.qwmod ) {
			Com_DPrintf( "Q2PRO QWMod enabled\n" );

			cl.pmp.maxspeed = 320;
			cl.pmp.upspeed = ((cl.pmp.qwmod == 2) ? 310 : 350);
			cl.pmp.friction = 4;
			cl.pmp.waterfriction = 4;
			cl.pmp.airaccelerate = qtrue;
		}
	}

	if( cl.clientNum == -1 ) {
	    // tell the server to advance to the next map / cinematic
	    CL_ClientCommand( va( "nextserver %i\n", cl.servercount ) );
	} else {
		// seperate the printfs so the server message can have a color
		Con_Printf( "\n\n\35\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\36\37\n\n" );
		Con_Printf( S_COLOR_ALT "%s\n\n", str );

        // make sure clientNum is in range
        if( cl.clientNum < 0 || cl.clientNum >= MAX_CLIENTS ) {
            cl.clientNum = CLIENTNUM_NONE;
        }
	}

}

/*
==================
CL_ParseBaseline
==================
*/
static void CL_ParseBaseline( void ) {
	int				bits;
	int				newnum;

	newnum = MSG_ParseEntityBits( &bits );
	if( newnum < 1 || newnum >= MAX_EDICTS ) {
		Com_Error( ERR_DROP, "CL_ParseBaseline: bad entity number %i", newnum );
	}
	MSG_ParseDeltaEntity( NULL, &cl.baselines[newnum], newnum, bits );
}

/*
================
CL_LoadClientinfo

================
*/
void CL_LoadClientinfo( clientinfo_t *ci, char *s ) {
	int         i;
	char		*t;
	char		model_name[MAX_QPATH];
	char		skin_name[MAX_QPATH];
	char		model_filename[MAX_QPATH];
	char		skin_filename[MAX_QPATH];
	char		weapon_filename[MAX_QPATH];

	strcpy( ci->cinfo, s );

	// isolate the player's name
	strcpy( ci->name, s );
	t = strchr( s, '\\' );
	if( t ) {
		ci->name[ t - s ] = 0;
		s = t + 1;
	}

	if( cl_noskins->integer || *s == 0 ) {
noskin:
	    strcpy( model_filename, "players/male/tris.md2" );
		strcpy( weapon_filename, "players/male/weapon.md2" );
	    strcpy( skin_filename, "players/male/grunt.pcx" );
		strcpy( ci->iconname, "/players/male/grunt_i.pcx" );
		ci->model = ref.RegisterModel( model_filename );
		memset( ci->weaponmodel, 0, sizeof( ci->weaponmodel ) );
		ci->weaponmodel[0] = ref.RegisterModel( weapon_filename );
		ci->skin = ref.RegisterSkin( skin_filename );
		ci->icon = ref.RegisterPic( ci->iconname );
	} else {
		strcpy( model_name, s );

		// isolate the model name
		t = strchr( model_name, '/' );
		if( !t )
			t = strchr( model_name, '\\' );
		if( !t )
			t = model_name;
        if( t == model_name ) {
            goto noskin;
        }
		*t = 0;

		// isolate the skin name
		strcpy( skin_name, t + 1 );

		// model file
		Com_sprintf( model_filename, sizeof( model_filename ),
            "players/%s/tris.md2", model_name );
		ci->model = ref.RegisterModel( model_filename );
		if( !ci->model && Q_stricmp( model_name, "male" ) ) {
			strcpy( model_name, "male" );
			strcpy( model_filename, "players/male/tris.md2" );
			ci->model = ref.RegisterModel( model_filename );
		}

		// skin file
		Com_sprintf( skin_filename, sizeof( skin_filename ),
            "players/%s/%s.pcx", model_name, skin_name );
		ci->skin = ref.RegisterSkin( skin_filename );

		// if we don't have the skin and the model was female,
		// see if athena skin exists
        if( !ci->skin && !Q_stricmp( model_name, "female" ) ) {
            strcpy( skin_name, "athena" );
            strcpy( skin_filename, "players/female/athena.pcx" );
            ci->skin = ref.RegisterSkin( skin_filename );
        }

		// if we don't have the skin and the model wasn't male,
		// see if the male has it (this is for CTF's skins)
 		if( !ci->skin && Q_stricmp( model_name, "male" ) ) {
			// change model to male
			strcpy( model_name, "male" );
			strcpy( model_filename, "players/male/tris.md2" );
			ci->model = ref.RegisterModel( model_filename );

			// see if the skin exists for the male model
			Com_sprintf( skin_filename, sizeof( skin_filename ),
                "players/%s/%s.pcx", model_name, skin_name );
			ci->skin = ref.RegisterSkin( skin_filename );
		}

		// if we still don't have a skin, it means that the male model
        // didn't have it, so default to grunt
		if( !ci->skin ) {
			// see if the skin exists for the male model
			Com_sprintf( skin_filename, sizeof( skin_filename ),
                "players/%s/grunt.pcx", model_name );
			ci->skin = ref.RegisterSkin( skin_filename );
		}

		// weapon file
		for( i = 0; i < cl.numWeaponModels; i++ ) {
			Com_sprintf( weapon_filename, sizeof( weapon_filename ),
                "players/%s/%s", model_name, cl.weaponModels[i] );
			ci->weaponmodel[i] = ref.RegisterModel( weapon_filename );
			if( !ci->weaponmodel[i] && strcmp( model_name, "cyborg" ) == 0 ) {
				// try male
				Com_sprintf( weapon_filename, sizeof( weapon_filename ),
                    "players/male/%s", cl.weaponModels[i] );
				ci->weaponmodel[i] = ref.RegisterModel( weapon_filename );
			}
			if( !cl_vwep->integer )
				break; // only one when vwep is off
		}

		// icon file
		Com_sprintf( ci->iconname, sizeof( ci->iconname ),
            "/players/%s/%s_i.pcx", model_name, skin_name );
		ci->icon = ref.RegisterPic( ci->iconname );
	}

	// must have loaded all data types to be valid
	if( !ci->skin || !ci->icon || !ci->model || !ci->weaponmodel[0] ) {
		ci->skin = 0;
		ci->icon = 0;
		ci->model = 0;
		ci->weaponmodel[0] = 0;
	}
}


/*
================
CL_ParseConfigString
================
*/
static void CL_ParseConfigString (void) {
	int		i;
	char	*s;

	i = MSG_ReadShort ();
	if (i < 0 || i >= MAX_CONFIGSTRINGS)
		Com_Error( ERR_DROP, "%s: bad index: %d", __func__, i );

	s = MSG_ReadString();

	if( cl_shownet->integer > 2 ) {
		Com_Printf( "    %i \"%s\"\n", i, Q_FormatString( s ) );
	}

    CL_ConfigString( i, s );
}


/*
=====================================================================

ACTION MESSAGES

=====================================================================
*/

/*
==================
CL_ParseStartSoundPacket
==================
*/
static void CL_ParseStartSoundPacket( void ) {
    vec3_t  pos_v;
	float	*pos;
    int 	channel, ent;
    int 	sound_num;
    float 	volume;
    float 	attenuation; 
	int		flags;
	float	ofs;

	flags = MSG_ReadByte();
	sound_num = MSG_ReadByte();
	if( sound_num == -1 ) {
		Com_Error( ERR_DROP, "%s: read past end of message", __func__ );
	}

    if( flags & SND_VOLUME )
		volume = MSG_ReadByte() / 255.0;
	else
		volume = DEFAULT_SOUND_PACKET_VOLUME;
	
    if( flags & SND_ATTENUATION )
		attenuation = MSG_ReadByte() / 64.0;
	else
		attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;	

    if( flags & SND_OFFSET )
		ofs = MSG_ReadByte() / 1000.0;
	else
		ofs = 0;

	if( flags & SND_ENT ) {	
		// entity relative
		channel = MSG_ReadShort(); 
		ent = channel >> 3;
		if( ent < 0 || ent >= MAX_EDICTS )
			Com_Error( ERR_DROP, "%s: bad ent: %d", __func__, ent );
		channel &= 7;
	} else {
		ent = 0;
		channel = 0;
	}

	if( flags & SND_POS ) {
		// positioned in space
		MSG_ReadPos( pos_v );
		pos = pos_v;
	} else {
		if( !( flags & SND_ENT ) ) {
			Com_Error( ERR_DROP, "%s: neither SND_ENT nor SND_POS set", __func__ );
		}
        if( cl_entities[ent].serverframe != cl.frame.number ) {
            if( cl_entities[ent].serverframe ) { 
                Com_DPrintf( "BUG: sound on entity %d last seen %d frames ago\n",
                    ent, cl.frame.number - cl_entities[ent].serverframe );
            } else {
                Com_DPrintf( "BUG: sound on entity %d we have never seen\n", ent );
            }
        }
		// use entity number
		pos = NULL;
	}

	if( cl.sound_precache[sound_num] ) {
    	S_StartSound( pos, ent, channel, cl.sound_precache[sound_num],
            volume, attenuation, ofs );
    }
}

/*
=====================
CL_ParseReconnect
=====================
*/
static void CL_ParseReconnect( void ) {
	if( cls.demoplayback ) {
		return;
	}

    S_StopAllSounds();

	if ( cls.demorecording )
        CL_Stop_f();

	Com_Printf( "Server disconnected, reconnecting\n" );
	if( cls.download ) {
		FS_FCloseFile( cls.download );
		cls.download = 0;
	}

    EXEC_TRIGGER( cl_changemapcmd );

	cls.downloadtempname[0] = 0;
	cls.downloadname[0] = 0;

	CL_ClearState();
	cls.state = ca_challenging;
	cls.connect_time = -99999;	// CL_CheckForResend() will fire immediately
	cls.connectCount = 0;
}

/*
====================
CL_CheckForVersion
====================
*/
static void CL_CheckForVersion( const char *string ) {
    char * p;

    if ( cls.demoplayback ) {
        return;
    }

    p = strstr( string, ": " );
    if ( !p ) {
        return;
    }

    if ( strncmp( p + 2, "!version", 8 ) ) {
        return;
    }

    if ( cl.replyTime && cls.realtime - cl.replyTime < 120000 ) {
        return;
    }

    cl.replyTime = cls.realtime + 1024 + ( rand() & 1023 );
}


/*
=====================
CL_ParsePrint
=====================
*/
static void CL_ParsePrint( void ) {
	int level;
	char *string;

	level = MSG_ReadByte();
	string = MSG_ReadString();

	if( cl_shownet->integer > 2 ) {
		Com_Printf( "    %i \"%s\"\n", level, Q_FormatString( string ) );
	}

	if( level != PRINT_CHAT ) {
		Com_Printf( "%s", string );
		return;
	}

	CL_CheckForVersion( string );

	// disable notify
	if( !cl_chat_notify->integer ) {
		Con_SkipNotify( qtrue );
	}

    // filter text
    if( cl_chat_filter->integer ) {
        Q_ClearStr( string, string, MAX_STRING_CHARS - 1 );
        Q_strcat( string, MAX_STRING_CHARS, "\n" );
    }

	Com_Printf( S_COLOR_ALT "%s", string );

	Con_SkipNotify( qfalse );

	SCR_AddToChatHUD( string );

    // play sound
	if( cl_chat_sound->string[0] ) {
		S_StartLocalSound( cl_chat_sound->string );
	}
	
}

/*
=====================
CL_ParseCenterPrint
=====================
*/
static void CL_ParseCenterPrint( void ) {
	char *string;

	string = MSG_ReadString();

	if( cl_shownet->integer > 2 ) {
		Com_Printf( "    \"%s\"\n", Q_FormatString( string ) );
	}

    SCR_CenterPrint( string );
}

/*
=====================
CL_ParseStuffText
=====================
*/
static void CL_ParseStuffText( void ) {
	char *string;

	string = MSG_ReadString();

	if( cl_shownet->integer > 2 ) {
		Com_Printf( "    \"%s\"\n", Q_FormatString( string ) );
	}

	if( cls.demoplayback &&
            strcmp( string, "precache\n" ) &&
            strcmp( string, "changing\n" ) &&
            strncmp( string, "play ", 5 ) &&
            strcmp( string, "reconnect\n" ) )
    {
		Com_DPrintf( "ignored stufftext: %s\n", string );
		return;
	}

	Com_DPrintf( "stufftext: %s\n", Q_FormatString( string ) );

	Cbuf_AddText( string );
}

/*
=====================
CL_ParseLayout
=====================
*/
static void CL_ParseLayout( void ) {
	char *string;

	string = MSG_ReadString();

	if( cl_shownet->integer > 2 ) {
		Com_Printf( "    \"%s\"\n", Q_FormatString( string ) );
	}
	
	Q_strncpyz( cl.layout, string, sizeof( cl.layout ) );
    cl.putaway = qfalse;
}

/*
================
CL_ParseInventory
================
*/
static void CL_ParseInventory( void ) {
	int		i;

	for( i = 0; i < MAX_ITEMS; i++ ) {
		cl.inventory[i] = MSG_ReadShort();
	}
    cl.putaway = qfalse;
}

static void CL_ParseZPacket( void ) {
#if USE_ZLIB
	sizebuf_t	temp;
	byte		buffer[MAX_MSGLEN];
	unsigned	inlen, outlen;

	if( msg_read.data != msg_read_buffer ) {
		Com_Error( ERR_DROP, "%s: recursively entered", __func__ );
	}

	inlen = MSG_ReadShort();
    if( msg_read.readcount + inlen > msg_read.cursize ) {
		Com_Error( ERR_DROP, "%s: read past end of message", __func__ );
    }

	outlen = MSG_ReadShort();
    if( outlen > MAX_MSGLEN ) {
		Com_Error( ERR_DROP, "%s: invalid output length", __func__ );
    }

    inflateReset( &cls.z );

    cls.z.next_in = msg_read.data + msg_read.readcount;
    cls.z.avail_in = inlen;
    cls.z.next_out = buffer;
    cls.z.avail_out = outlen;
	if( inflate( &cls.z, Z_FINISH ) != Z_STREAM_END ) {
		Com_Error( ERR_DROP, "%s: inflate() failed: %s", __func__, cls.z.msg );
    }

	msg_read.readcount += inlen;

	temp = msg_read;
    SZ_Init( &msg_read, buffer, outlen );
    msg_read.cursize = outlen;

    CL_ParseServerMessage();

	msg_read = temp;
#else
	Com_Error( ERR_DROP, "Compressed server packet received, "
        "but no zlib support linked in." );
#endif
}

/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage( void ) {
	int			cmd, readcount;
	int			extrabits;

	if( cl_shownet->integer == 1 ) {
		Com_Printf( "%i ", msg_read.cursize );
	} else if( cl_shownet->integer > 1 ) {
		Com_Printf( "------------------\n" );
	}

//
// parse the message
//
	while( 1 ) {
		if( msg_read.readcount > msg_read.cursize ) {
			Com_Error( ERR_DROP, "CL_ParseServerMessage: read past end of server message" );
		}

        readcount = msg_read.readcount;

		if( ( cmd = MSG_ReadByte() ) == -1 ) {
			if( cl_shownet->integer > 1 ) {
				Com_Printf( "%3i:END OF MESSAGE\n", msg_read.readcount - 1 );
			}
			break;
		}

		extrabits = cmd >> SVCMD_BITS;
		cmd &= SVCMD_MASK;

		if( cl_shownet->integer > 1 ) {
			MSG_ShowSVC( cmd );
		}
	
	// other commands
		switch( cmd ) {
		default:
			Com_Error( ERR_DROP, "CL_ParseServerMessage: illegible server message: %d", cmd );
			break;
			
		case svc_nop:
			break;
			
		case svc_disconnect:
			Com_Error( ERR_DISCONNECT, "Server disconnected" );
			break;

		case svc_reconnect:
			CL_ParseReconnect();
			return;

		case svc_print:
			CL_ParsePrint();
			break;
			
		case svc_centerprint:
			CL_ParseCenterPrint();
			break;
			
		case svc_stufftext:
			CL_ParseStuffText();
			break;
			
		case svc_serverdata:
			CL_ParseServerData();
			break;
			
		case svc_configstring:
			CL_ParseConfigString();
			break;
			
		case svc_sound:
			CL_ParseStartSoundPacket();
			break;
			
		case svc_spawnbaseline:
			CL_ParseBaseline();
			break;

		case svc_temp_entity:
			CL_ParseTEnt();
			break;

		case svc_muzzleflash:
			CL_ParseMuzzleFlash();
			break;

		case svc_muzzleflash2:
			CL_ParseMuzzleFlash2();
			break;

		case svc_download:
			CL_ParseDownload();
			continue;

		case svc_frame:
			CL_ParseFrame( extrabits );
			continue;

		case svc_inventory:
			CL_ParseInventory();
			break;

		case svc_layout:
			CL_ParseLayout();
			break;

		case svc_zpacket:
			CL_ParseZPacket();
			continue;

		case svc_gamestate:
			CL_ParseGamestate();
			continue;
		}

        // copy protocol invariant stuff
        if( cls.demorecording ) {
            SZ_Write( &cls.demobuff, msg_read.data + readcount,
                msg_read.readcount - readcount );
        }
	}

//
// if recording demos, write the message out
//
    if( cls.demorecording ) {
        CL_WriteDemoMessage( &cls.demobuff );
    }
}

