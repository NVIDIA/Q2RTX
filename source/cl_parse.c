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

/*
===============
CL_CheckOrDownloadFile

Returns qtrue if the file exists, otherwise it attempts
to start a download from the server.
===============
*/
qboolean CL_CheckOrDownloadFile( const char *path ) {
    fileHandle_t f;
    size_t len;

    len = strlen( path );
    if( len < 1 || len >= MAX_QPATH
        || !Q_ispath( path[0] )
        || !Q_ispath( path[ len - 1 ] )
        || strchr( path, '\\' )
        || strchr( path, ':' )
        || !strchr( path, '/' )
        || strstr( path, ".." ) )
    {
        Com_Printf( "Refusing to download file with invalid path.\n" );
        return qtrue;
    }

    if( FS_LoadFile( path, NULL ) != INVALID_LENGTH ) {    
        // it exists, no need to download
        return qtrue;
    }

    memcpy( cls.download.name, path, len + 1 );

    // download to a temp name, and only rename
    // to the real name when done, so if interrupted
    // a runt file wont be left
    memcpy( cls.download.temp, path, len );
    memcpy( cls.download.temp + len, ".tmp", 5 );

//ZOID
    // check to see if we already have a tmp for this file, if so, try to resume
    // open the file if not opened yet
    len = FS_FOpenFile( cls.download.temp, &f, FS_MODE_RDWR );
    if( len == INVALID_LENGTH && f ) {
        Com_WPrintf( "Couldn't determine size of %s\n", cls.download.temp );
        FS_FCloseFile( f );
        f = 0;
    }
    if( f ) { // it exists
        cls.download.file = f;
        // give the server an offset to start the download
        Com_Printf( "Resuming %s\n", cls.download.name );
        CL_ClientCommand( va( "download \"%s\" %"PRIz, cls.download.name, len ) );
    } else {
        Com_Printf( "Downloading %s\n", cls.download.name );
        CL_ClientCommand( va( "download \"%s\"", cls.download.name ) );
    }

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
    if( !allow_download->integer ) {
        Com_Printf( "Downloading is disabled.\n" );
        return;
    }

    if( Cmd_Argc() != 2 ) {
        Com_Printf( "Usage: download <filename>\n" );
        return;
    }

    if( cls.download.temp[0] ) {
        Com_Printf( "Already downloading.\n" );
        if( cls.serverProtocol == PROTOCOL_VERSION_Q2PRO ) {
            Com_Printf( "Try using 'stopdl' command to abort the download.\n" );
        }
        return;
    }

    path = Cmd_Argv( 1 );

    if( FS_LoadFile( path, NULL ) != INVALID_LENGTH ) {    
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
    int        size, percent;

    if( !cls.download.temp[0] ) {
        Com_Error( ERR_DROP, "%s: no download requested", __func__ );
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
        if( cls.download.file ) {
            // if here, we tried to resume a file but the server said no
            FS_FCloseFile( cls.download.file );
        }
        goto another;
    }

    if( size < 0 ) {
        Com_Error( ERR_DROP, "%s: bad size: %d", __func__, size );
    }

    if( msg_read.readcount + size > msg_read.cursize ) {
        Com_Error( ERR_DROP, "%s: read past end of message", __func__ );
    }

    // open the file if not opened yet
    if( !cls.download.file ) {
        FS_FOpenFile( cls.download.temp, &cls.download.file, FS_MODE_WRITE );
        if( !cls.download.file ) {
            msg_read.readcount += size;
            Com_WPrintf( "Failed to open '%s' for writing\n",
                cls.download.temp );
            goto another;
        }
    }

    FS_Write( msg_read.data + msg_read.readcount, size, cls.download.file );
    msg_read.readcount += size;
    
    if( percent != 100 ) {
        // request next block
        // change display routines by zoid
        cls.download.percent = percent;

        CL_ClientCommand( "nextdl" );
    } else {
        FS_FCloseFile( cls.download.file );

        // rename the temp file to it's final name
        if( !FS_RenameFile( cls.download.temp, cls.download.name ) ) {
            Com_WPrintf( "Failed to rename %s to %s\n",
                cls.download.temp, cls.download.name );
        }

        Com_Printf( "Downloaded successfully.\n" );

another:
        // get another file if needed
        memset( &cls.download, 0, sizeof( cls.download ) );
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
    entity_state_t    *state;

    if( frame->numEntities >= MAX_PACKET_ENTITIES ) {
        Com_Error( ERR_DROP, "%s: MAX_PACKET_ENTITIES exceeded", __func__ );
    }

    state = &cl.entityStates[cl.numEntityStates & PARSE_ENTITIES_MASK];
    cl.numEntityStates++;
    frame->numEntities++;

#ifdef _DEBUG
    if( cl_shownet->integer > 2 && bits ) {
        MSG_ShowDeltaEntityBits( bits );
        Com_Printf( "\n" );
    }
#endif

    MSG_ParseDeltaEntity( old, state, newnum, bits, cl.esFlags );
}

/*
==================
CL_ParsePacketEntities
==================
*/
static void CL_ParsePacketEntities( server_frame_t *oldframe,
                                    server_frame_t *frame )
{
    int            newnum;
    int            bits;
    entity_state_t    *oldstate;
    int            oldindex, oldnum;
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
            Com_Error( ERR_DROP, "%s: bad number: %d", __func__, newnum );
        }

        if( msg_read.readcount > msg_read.cursize ) {
            Com_Error( ERR_DROP, "%s: read past end of message", __func__ );
        }

        if( !newnum ) {
            break;
        }

        while( oldnum < newnum ) {
            // one or more entities from the old packet are unchanged
            SHOWNET( 2, "   unchanged: %i\n", oldnum );
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
            SHOWNET( 2, "   remove: %i\n", newnum );
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
            SHOWNET( 2, "   delta: %i ", newnum );
            CL_ParseDeltaEntity( frame, newnum, oldstate, bits );

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
            SHOWNET( 2, "   baseline: %i ", newnum );
            CL_ParseDeltaEntity( frame, newnum, &cl.baselines[newnum], bits );
            continue;
        }

    }

    // any remaining entities in the old frame are copied over
    while( oldnum != 99999 ) {    
        // one or more entities from the old packet are unchanged
        SHOWNET( 3, "   unchanged: %i\n", oldnum );
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
    cl.serverdelta = cl.frame.number;
    cl.time = cl.servertime = 0; // set time, needed for demos
    cls.state = ca_active;
    cl.oldframe.valid = qfalse;
    cl.frameflags = 0;
    cl.putaway = qfalse;
    if( cls.netchan ) {
        cl.initialSeq = cls.netchan->outgoing_sequence;
    }

    if( !cls.demo.playback ) {
        VectorScale( cl.frame.ps.pmove.origin, 0.125f, cl.predicted_origin );
        VectorCopy( cl.frame.ps.viewangles, cl.predicted_angles );
    }
    
    SCR_EndLoadingPlaque ();    // get rid of loading plaque
    SCR_LagClear();

    if( !cls.demo.playback ) {
        EXEC_TRIGGER( cl_beginmapcmd );
    }

    Cvar_Set( "cl_paused", "0" );
}

/*
================
CL_ParseFrame
================
*/
static void CL_ParseFrame( int extrabits ) {
    uint32_t bits, extraflags;
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
            Com_DPrintf( "%s: delta from current frame\n", __func__ );
            cl.frameflags |= FF_BADFRAME;
        } else if( oldframe->number != deltaframe ) {
            // The frame that the server did the delta from
            // is too old, so we can't reconstruct it properly.
            Com_DPrintf( "%s: delta frame was never received or too old\n", __func__ );
            cl.frameflags |= FF_OLDFRAME;
        } else if( !oldframe->valid ) {    
            // should never happen
            Com_DPrintf( "%s: delta from invalid frame\n", __func__ );
            cl.frameflags |= FF_BADFRAME;
        } else if( cl.numEntityStates - oldframe->firstEntity >
            MAX_PARSE_ENTITIES - MAX_PACKET_ENTITIES )
        {
            Com_DPrintf( "%s: delta entities too old\n", __func__ );
            cl.frameflags |= FF_OLDENT;
        } else {
            frame.valid = qtrue;    // valid delta parse
        }
        if( !frame.valid && cl.frame.valid && cls.demo.playback ) {
            Com_DPrintf( "%s: recovering broken demo\n", __func__ );
            oldframe = &cl.frame;
            from = &oldframe->ps;
            frame.valid = qtrue;
        }
    } else {
        oldframe = NULL;
        from = NULL;
        frame.valid = qtrue;        // uncompressed frame
        //if( !cls.demowaiting ) {
            cl.frameflags |= FF_NODELTA;
        //}
        //cls.demowaiting = qfalse;   // we can start recording now
    }

    // read areabits
    length = MSG_ReadByte();
    if( length ) {
        if( length < 0 || msg_read.readcount + length > msg_read.cursize ) {
            Com_Error( ERR_DROP, "%s: read past end of message", __func__ );
        }
        if( length > sizeof( frame.areabits ) ) {
            Com_Error( ERR_DROP, "%s: invalid areabits length", __func__ );
        }
        memcpy( frame.areabits, msg_read.data + msg_read.readcount, length );
        msg_read.readcount += length;
        frame.areabytes = length;
    } else {
        frame.areabytes = 0;
    }

    if( cls.serverProtocol <= PROTOCOL_VERSION_DEFAULT ) {
        if( MSG_ReadByte() != svc_playerinfo ) {
            Com_Error( ERR_DROP, "%s: not playerinfo", __func__ );
        }
    }

    SHOWNET( 2, "%3"PRIz":playerinfo\n", msg_read.readcount - 1 );

    // parse playerstate
    bits = MSG_ReadShort();
    if( cls.serverProtocol > PROTOCOL_VERSION_DEFAULT ) {
        MSG_ParseDeltaPlayerstate_Enhanced( from, &frame.ps, bits, extraflags );
#ifdef _DEBUG
        if( cl_shownet->integer > 2 ) {
            MSG_ShowDeltaPlayerstateBits_Enhanced( bits );
            Com_Printf( "\n" );
        }
#endif
        if( cls.serverProtocol == PROTOCOL_VERSION_Q2PRO ) {
            // parse clientNum
            if( extraflags & EPS_CLIENTNUM ) {
                frame.clientNum = MSG_ReadByte();
            } else if( oldframe ) {
                frame.clientNum = oldframe->clientNum;
            }
        } else {
            frame.clientNum = cl.clientNum;
        }
    } else {
        MSG_ParseDeltaPlayerstate_Default( from, &frame.ps, bits );
#ifdef _DEBUG
        if( cl_shownet->integer > 2 ) {
            MSG_ShowDeltaPlayerstateBits_Default( bits );
            Com_Printf( "\n" );
        }
#endif
        frame.clientNum = cl.clientNum;
    }
    if( !frame.ps.fov ) {
        // fail out early to prevent spurious errors later
        Com_Error( ERR_DROP, "%s: bad fov", __func__ );
    }

    // parse packetentities
    if( cls.serverProtocol <= PROTOCOL_VERSION_DEFAULT ) {
        if( MSG_ReadByte() != svc_packetentities ) {
            Com_Error( ERR_DROP, "%s: not packetentities", __func__ );
        }
    }

    SHOWNET( 2, "%3"PRIz":packetentities\n", msg_read.readcount - 1 );

    CL_ParsePacketEntities( oldframe, &frame );

    // save the frame off in the backup array for later delta comparisons
    cl.frames[currentframe & UPDATE_MASK] = frame;

#ifdef _DEBUG
    if( cl_shownet->integer > 2 ) {
        int rtt = 0;
        if( cls.netchan ) {
            int seq = cls.netchan->incoming_acknowledged & CMD_MASK;
            rtt = cls.realtime - cl.history[seq].sent;
        }
        Com_Printf( "%3"PRIz":frame:%d  delta:%d  rtt:%d\n",
            msg_read.readcount - 1, frame.number, frame.delta, rtt );
    }
#endif

    if( !frame.valid ) {
        cl.frame.valid = qfalse;
        return; // do not change anything
    }

    cl.oldframe = cl.frame;
    cl.frame = frame;

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

static void CL_ParseConfigstring( int index ) {
    size_t  len, maxlen;
    char    *string;

    if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
        Com_Error( ERR_DROP, "%s: bad index: %d", __func__, index );
    }

    string = cl.configstrings[index];
    maxlen = CS_SIZE( index );
    len = MSG_ReadString( string, maxlen );

    SHOWNET( 2, "    %d \"%s\"\n", index, string );

    if( len >= maxlen ) {
        Com_WPrintf(
            "%s: index %d overflowed: %"PRIz" > %"PRIz"\n",
            __func__, index, len, maxlen - 1 );
        len = maxlen - 1; 
    }

    if( cls.demo.recording && cls.demo.paused ) {
        Q_SetBit( cl.dcs, index );
    }

    // do something apropriate 
    if( index == CS_MAXCLIENTS ) {
        cl.maxclients = atoi( string );
        return;
    }
    if( index == CS_MODELS + 1 ) {
        if( len <= 9 ) {
            Com_Error( ERR_DROP, "%s: bad world model: %s", __func__, string );
        }
        memcpy( cl.mapname, string + 5, len - 9 ); // skip "maps/"
        cl.mapname[len - 9] = 0; // cut off ".bsp"
        return;
    }
#if USE_LIGHTSTYLES
    if (index >= CS_LIGHTS && index < CS_LIGHTS+MAX_LIGHTSTYLES) {
        CL_SetLightStyle( index - CS_LIGHTS, string, len );
        return;
    }
#endif

    if( cls.state < ca_precached ) {
        return;
    }
    if (index >= CS_MODELS+2 && index < CS_MODELS+MAX_MODELS) {
        cl.model_draw[index-CS_MODELS] = R_RegisterModel (string);
        if (*string == '*')
            cl.model_clip[index-CS_MODELS] = BSP_InlineModel (cl.bsp, string);
        else
            cl.model_clip[index-CS_MODELS] = NULL;
    } else if (index >= CS_SOUNDS && index < CS_SOUNDS+MAX_MODELS) {
        cl.sound_precache[index-CS_SOUNDS] = S_RegisterSound (string);
    } else if (index >= CS_IMAGES && index < CS_IMAGES+MAX_MODELS) {
        cl.image_precache[index-CS_IMAGES] = R_RegisterPic (string);
    } else if (index >= CS_PLAYERSKINS && index < CS_PLAYERSKINS+MAX_CLIENTS) {
        CL_LoadClientinfo( &cl.clientinfo[index - CS_PLAYERSKINS], string );
    } else if( index == CS_AIRACCEL && !cl.pmp.qwmode ) {
        cl.pmp.airaccelerate = atoi( string ) ? qtrue : qfalse;
    }
}

static void CL_ParseBaseline( int index, int bits ) {
    if( index < 1 || index >= MAX_EDICTS ) {
        Com_Error( ERR_DROP, "%s: bad index: %d", __func__, index );
    }
#ifdef _DEBUG
    if( cl_shownet->integer > 2 ) {
        MSG_ShowDeltaEntityBits( bits );
        Com_Printf( "\n" );
    }
#endif
    MSG_ParseDeltaEntity( NULL, &cl.baselines[index], index, bits, cl.esFlags );
}

/*
==================
CL_ParseGamestate

Instead of wasting space for svc_configstring and svc_spawnbaseline
bytes, entire game state is compressed into a single stream.
==================
*/
static void CL_ParseGamestate( void ) {
    int        index, bits;

    while( msg_read.readcount < msg_read.cursize ) {
        index = MSG_ReadShort();
        if( index == MAX_CONFIGSTRINGS ) {
            break;
        }
        CL_ParseConfigstring( index );
    }

    while( msg_read.readcount < msg_read.cursize ) {
        index = MSG_ParseEntityBits( &bits );
        if( !index ) {
            break;
        }
        CL_ParseBaseline( index, bits );
    }
}

/*
==================
CL_ParseServerData
==================
*/
static void CL_ParseServerData( void ) {
    char    levelname[MAX_QPATH];
    int     i, protocol, attractloop;
    size_t  len;

    Cbuf_Execute( &cl_cmdbuf );        // make sure any stuffed commands are done
    
    // wipe the client_state_t struct
    CL_ClearState();

    // parse protocol version number
    protocol = MSG_ReadLong();
    cl.servercount = MSG_ReadLong();
    attractloop = MSG_ReadByte();

    Com_DPrintf( "Serverdata packet received "
        "(protocol=%d, servercount=%d, attractloop=%d)\n",
        protocol, cl.servercount, attractloop );

    // check protocol
    if( cls.serverProtocol != protocol ) {
        if( !cls.demo.playback ) { 
            Com_Error( ERR_DROP, "Requested protocol version %d, but server returned %d.",
                cls.serverProtocol, protocol );
        }
        // BIG HACK to let demos from release work with the 3.0x patch!!!
        if( protocol < PROTOCOL_VERSION_OLD || protocol > PROTOCOL_VERSION_Q2PRO ) {
            Com_Error( ERR_DROP, "Demo uses unsupported protocol version %d.", protocol );
        }
        cls.serverProtocol = protocol;
    }

    // game directory
    len = MSG_ReadString( cl.gamedir, sizeof( cl.gamedir ) );
    if( len >= sizeof( cl.gamedir ) ) {
        Com_Error( ERR_DROP, "Oversize gamedir string" );
    }

    // never allow demos to change gamedir
    // do not set gamedir if connected to local sever,
    // since it was already done by SV_InitGame
    if( !cls.demo.playback && !sv_running->integer ) {
        Cvar_UserSet( "game", cl.gamedir );
        if( FS_NeedRestart() ) {
            CL_RestartFilesystem( qfalse );
        }
    }

    // parse player entity number
    cl.clientNum = MSG_ReadShort();

    // get the full level name
    MSG_ReadString( levelname, sizeof( levelname ) );

    // setup default pmove parameters
    PmoveInit( &cl.pmp );

    // setup default frame times
    cl.frametime = 100;
    cl.framefrac = 0.01f;

    if( cls.serverProtocol == PROTOCOL_VERSION_R1Q2 ) {
        i = MSG_ReadByte();
        if( i ) {
            Com_Error( ERR_DROP, "'Enhanced' R1Q2 servers are not supported" );
        }
        i = MSG_ReadShort();
        // for some reason, R1Q2 servers always report the highest protocol
        // version they support, while still using the lower version
        // client specified in the 'connect' packet. oh well...
        if( !R1Q2_SUPPORTED( i ) ) {
            Com_WPrintf(
                "R1Q2 server reports unsupported protocol version %d.\n"
                "Assuming it really uses our current client version %d.\n"
                "Things will break if it does not!\n", i, PROTOCOL_VERSION_R1Q2_CURRENT );
            clamp( i, PROTOCOL_VERSION_R1Q2_MINIMUM, PROTOCOL_VERSION_R1Q2_CURRENT );
        }
        Com_DPrintf( "Using minor R1Q2 protocol version %d\n", i );
        cls.protocolVersion = i;
        MSG_ReadByte(); // used to be advanced deltas
        i = MSG_ReadByte();
        if( i ) {
            Com_DPrintf( "R1Q2 strafejump hack enabled\n" );
            cl.pmp.strafehack = qtrue;
        }
        if( cls.protocolVersion >= PROTOCOL_VERSION_R1Q2_LONG_SOLID ) {
            cl.esFlags |= MSG_ES_LONGSOLID;
        }
        cl.pmp.speedmult = 2;
    } else if( cls.serverProtocol == PROTOCOL_VERSION_Q2PRO ) {
        i = MSG_ReadShort();
        if( !Q2PRO_SUPPORTED( i ) ) {
            Com_Error( ERR_DROP,
                "Q2PRO server reports unsupported protocol version %d.\n"
                "Current client version is %d.", i, PROTOCOL_VERSION_Q2PRO_CURRENT );
        }
        Com_DPrintf( "Using minor Q2PRO protocol version %d\n", i );
        cls.protocolVersion = i;
        MSG_ReadByte(); // used to be gametype
        i = MSG_ReadByte();
        if( i ) {
            Com_DPrintf( "Q2PRO strafejump hack enabled\n" );
            cl.pmp.strafehack = qtrue;
        }
        i = MSG_ReadByte(); //atu QWMod
        if( i ) {
            Com_DPrintf( "Q2PRO QW mode enabled\n" );
            PmoveEnableQW( &cl.pmp );
        }
        cl.esFlags |= MSG_ES_UMASK;
        if( cls.protocolVersion >= PROTOCOL_VERSION_Q2PRO_LONG_SOLID ) {
            cl.esFlags |= MSG_ES_LONGSOLID;
        }
        if( cls.protocolVersion >= PROTOCOL_VERSION_Q2PRO_WATERJUMP_HACK ) {
            i = MSG_ReadByte();
            if( i ) {
                Com_DPrintf( "Q2PRO waterjump hack enabled\n" );
                cl.pmp.waterhack = qtrue;
            }
        }
        cl.pmp.speedmult = 2;        
        cl.pmp.flyhack = qtrue; // fly hack is unconditionally enabled
        cl.pmp.flyfriction = 4;
    }

    if( cl.clientNum == -1 ) {
        // tell the server to advance to the next map / cinematic
        CL_ClientCommand( va( "nextserver %i\n", cl.servercount ) );
    } else {
        // seperate the printfs so the server message can have a color
        Con_Printf(
            "\n\n"
            "\35\36\36\36\36\36\36\36\36\36\36\36"
            "\36\36\36\36\36\36\36\36\36\36\36\36"
            "\36\36\36\36\36\36\36\36\36\36\36\37"
            "\n\n" );

        Com_SetColor( COLOR_ALT );
        Com_Printf( "%s\n", levelname );
        Com_SetColor( COLOR_NONE );

        // make sure clientNum is in range
        if( cl.clientNum < 0 || cl.clientNum >= MAX_CLIENTS ) {
            cl.clientNum = CLIENTNUM_NONE;
        }
    }

}

/*
================
CL_LoadClientinfo

================
*/
void CL_LoadClientinfo( clientinfo_t *ci, const char *s ) {
    int         i;
    char        *t;
    char        model_name[MAX_QPATH];
    char        skin_name[MAX_QPATH];
    char        model_filename[MAX_QPATH];
    char        skin_filename[MAX_QPATH];
    char        weapon_filename[MAX_QPATH];
    char        icon_filename[MAX_QPATH];

    // isolate the player's name
    strcpy( ci->name, s );
    t = strchr( s, '\\' );
    if( t ) {
        ci->name[ t - s ] = 0;
        s = t + 1;
    }

    strcpy( model_name, s );

    // isolate the model name
    t = strchr( model_name, '/' );
    if( !t )
        t = strchr( model_name, '\\' );
    if( !t )
        t = model_name;
    if( t == model_name ) {
        strcpy( model_name, "male" );
        strcpy( skin_name, "grunt" );
    } else {
        *t = 0;

        // apply restictions on skins
        if( cl_noskins->integer == 2 && !Q_stricmp( model_name, "female" ) ) {
            strcpy( model_name, "female" );
            strcpy( skin_name, "athena" );
        } else if( cl_noskins->integer ) {
            strcpy( model_name, "male" );
            strcpy( skin_name, "grunt" );
        } else {
            // isolate the skin name
            strcpy( skin_name, t + 1 );
        }
    }

    // model file
    Q_concat( model_filename, sizeof( model_filename ),
        "players/", model_name, "/tris.md2", NULL );
    ci->model = R_RegisterModel( model_filename );
    if( !ci->model && Q_stricmp( model_name, "male" ) ) {
        strcpy( model_name, "male" );
        strcpy( model_filename, "players/male/tris.md2" );
        ci->model = R_RegisterModel( model_filename );
    }

    // skin file
    Q_concat( skin_filename, sizeof( skin_filename ),
        "players/", model_name, "/", skin_name, ".pcx", NULL );
    ci->skin = R_RegisterSkin( skin_filename );

    // if we don't have the skin and the model was female,
    // see if athena skin exists
    if( !ci->skin && !Q_stricmp( model_name, "female" ) ) {
        strcpy( skin_name, "athena" );
        strcpy( skin_filename, "players/female/athena.pcx" );
        ci->skin = R_RegisterSkin( skin_filename );
    }

    // if we don't have the skin and the model wasn't male,
    // see if the male has it (this is for CTF's skins)
    if( !ci->skin && Q_stricmp( model_name, "male" ) ) {
        // change model to male
        strcpy( model_name, "male" );
        strcpy( model_filename, "players/male/tris.md2" );
        ci->model = R_RegisterModel( model_filename );

        // see if the skin exists for the male model
        Q_concat( skin_filename, sizeof( skin_filename ),
            "players/male/", skin_name, ".pcx", NULL );
        ci->skin = R_RegisterSkin( skin_filename );
    }

    // if we still don't have a skin, it means that the male model
    // didn't have it, so default to grunt
    if( !ci->skin ) {
        // see if the skin exists for the male model
        strcpy( skin_name, "grunt" );
        strcpy( skin_filename, "players/male/grunt.pcx" );
        ci->skin = R_RegisterSkin( skin_filename );
    }

    // weapon file
    for( i = 0; i < cl.numWeaponModels; i++ ) {
        Q_concat( weapon_filename, sizeof( weapon_filename ),
            "players/", model_name, "/", cl.weaponModels[i], NULL );
        ci->weaponmodel[i] = R_RegisterModel( weapon_filename );
        if( !ci->weaponmodel[i] && Q_stricmp( model_name, "male" ) ) {
            // try male
            Q_concat( weapon_filename, sizeof( weapon_filename ),
                "players/male/", cl.weaponModels[i], NULL );
            ci->weaponmodel[i] = R_RegisterModel( weapon_filename );
        }
    }

    // icon file
    Q_concat( icon_filename, sizeof( icon_filename ),
        "/players/", model_name, "/", skin_name, "_i.pcx", NULL );
    ci->icon = R_RegisterPic( icon_filename );

    strcpy( ci->model_name, model_name );
    strcpy( ci->skin_name, skin_name );

    // must have loaded all data types to be valid
    if( !ci->skin || !ci->icon || !ci->model || !ci->weaponmodel[0] ) {
        ci->skin = 0;
        ci->icon = 0;
        ci->model = 0;
        ci->weaponmodel[0] = 0;
        ci->model_name[0] = 0;
        ci->skin_name[0] = 0;
    }
}

/*
=====================================================================

ACTION MESSAGES

=====================================================================
*/

tent_params_t   te;
mz_params_t     mz;

static void CL_ParseTEntParams( void ) {
    te.type = MSG_ReadByte();

    switch( te.type ) {
    case TE_BLOOD:
    case TE_GUNSHOT:
    case TE_SPARKS:
    case TE_BULLET_SPARKS:
    case TE_SCREEN_SPARKS:
    case TE_SHIELD_SPARKS:
    case TE_SHOTGUN:
    case TE_BLASTER:
    case TE_GREENBLOOD:
    case TE_BLASTER2:
    case TE_FLECHETTE:
    case TE_HEATBEAM_SPARKS:
    case TE_HEATBEAM_STEAM:
    case TE_MOREBLOOD:
    case TE_ELECTRIC_SPARKS:
        MSG_ReadPos( te.pos1 );
        MSG_ReadDir( te.dir );
        break;

    case TE_SPLASH:
    case TE_LASER_SPARKS:
    case TE_WELDING_SPARKS:
    case TE_TUNNEL_SPARKS:
        te.count = MSG_ReadByte();
        MSG_ReadPos( te.pos1 );
        MSG_ReadDir( te.dir );
        te.color = MSG_ReadByte();
        break;

    case TE_BLUEHYPERBLASTER:
    case TE_RAILTRAIL:
    case TE_BUBBLETRAIL:
    case TE_DEBUGTRAIL:
    case TE_BUBBLETRAIL2:
    case TE_BFG_LASER:
        MSG_ReadPos( te.pos1 );
        MSG_ReadPos( te.pos2 );
        break;

    case TE_GRENADE_EXPLOSION:
    case TE_GRENADE_EXPLOSION_WATER:
    case TE_EXPLOSION2:
    case TE_PLASMA_EXPLOSION:
    case TE_ROCKET_EXPLOSION:
    case TE_ROCKET_EXPLOSION_WATER:
    case TE_EXPLOSION1:
    case TE_EXPLOSION1_NP:
    case TE_EXPLOSION1_BIG:
    case TE_BFG_EXPLOSION:
    case TE_BFG_BIGEXPLOSION:
    case TE_BOSSTPORT:
    case TE_PLAIN_EXPLOSION:
    case TE_CHAINFIST_SMOKE:
    case TE_TRACKER_EXPLOSION:
    case TE_TELEPORT_EFFECT:
    case TE_DBALL_GOAL:
    case TE_WIDOWSPLASH:
    case TE_NUKEBLAST:
        MSG_ReadPos( te.pos1 );
        break;

    case TE_PARASITE_ATTACK:
    case TE_MEDIC_CABLE_ATTACK:
    case TE_HEATBEAM:
    case TE_MONSTER_HEATBEAM:
        te.entity1 = MSG_ReadShort();
        MSG_ReadPos( te.pos1 );
        MSG_ReadPos( te.pos2 );
        break;

    case TE_GRAPPLE_CABLE:
        te.entity1 = MSG_ReadShort();
        MSG_ReadPos( te.pos1 );
        MSG_ReadPos( te.pos2 );
        MSG_ReadPos( te.offset );
        break;

    case TE_LIGHTNING:
        te.entity1 = MSG_ReadShort();
        te.entity2 = MSG_ReadShort();
        MSG_ReadPos( te.pos1 );
        MSG_ReadPos( te.pos2 );
        break;

    case TE_FLASHLIGHT:
        MSG_ReadPos( te.pos1 );
        te.entity1 = MSG_ReadShort();
        break;

    case TE_FORCEWALL:
        MSG_ReadPos( te.pos1 );
        MSG_ReadPos( te.pos2 );
        te.color = MSG_ReadByte();
        break;

    case TE_STEAM:
        te.entity1 = MSG_ReadShort();
        te.count = MSG_ReadByte();
        MSG_ReadPos( te.pos1 );
        MSG_ReadDir( te.dir );
        te.color = MSG_ReadByte();
        te.entity2 = MSG_ReadShort();
        if( te.entity1 != -1 ) {
            te.time = MSG_ReadLong();
        }
        break;

    case TE_WIDOWBEAMOUT:
        te.entity1 = MSG_ReadShort();
        MSG_ReadPos( te.pos1 );
        break;

    default:
        Com_Error( ERR_DROP, "%s: bad type", __func__ );
    }

    CL_ParseTEnt();
}

static void CL_ParseMuzzleFlashParams( void ) {
    int entity, weapon;

    entity = MSG_ReadShort();
    if( entity < 1 || entity >= MAX_EDICTS )
        Com_Error( ERR_DROP, "%s: bad entity", __func__ );

    weapon = MSG_ReadByte();
    mz.silenced = weapon & MZ_SILENCED;
    mz.weapon = weapon & ~MZ_SILENCED;
    mz.entity = entity;

    CL_ParseMuzzleFlash();
}

static void CL_ParseMuzzleFlashParams2( void ) {
    int entity;

    entity = MSG_ReadShort();
    if( entity < 1 || entity >= MAX_EDICTS )
        Com_Error( ERR_DROP, "%s: bad entity", __func__ );

    mz.weapon = MSG_ReadByte();
    mz.entity = entity;

    CL_ParseMuzzleFlash2();
}

/*
==================
CL_ParseStartSoundPacket
==================
*/
static void CL_ParseStartSoundPacket( void ) {
    vec3_t  pos_v;
    float   *pos;
    int     channel, ent;
    int     sound_num;
    float   volume;
    float   attenuation; 
    int     flags;
    float   ofs;

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
                Com_DPrintf( "SERVER BUG: sound on entity %d last seen %d frames ago\n",
                    ent, cl.frame.number - cl_entities[ent].serverframe );
            } else {
                Com_DPrintf( "SERVER BUG: sound on entity %d never seen before\n", ent );
            }
        }
        // use entity number
        pos = NULL;
    }

    SHOWNET( 2, "    %s\n", cl.configstrings[CS_SOUNDS+sound_num] );

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
    if( cls.demo.playback ) {
        return;
    }

    S_StopAllSounds();

    if( cls.demo.recording )
        CL_Stop_f();

    Com_Printf( "Server disconnected, reconnecting\n" );

    if( cls.download.file ) {
        FS_FCloseFile( cls.download.file );
    }
    memset( &cls.download, 0, sizeof( cls.download ) );

    EXEC_TRIGGER( cl_changemapcmd );

    CL_ClearState();
    cls.state = ca_challenging;
    cls.connect_time = cls.realtime - CONNECT_DELAY;
    cls.connect_count = 0;

    CL_CheckForResend();
}

#if USE_AUTOREPLY
/*
====================
CL_CheckForVersion
====================
*/
static void CL_CheckForVersion( const char *string ) {
    char *p;

    p = strstr( string, ": " );
    if( !p ) {
        return;
    }

    if( strncmp( p + 2, "!version", 8 ) ) {
        return;
    }

    if( cl.reply_time && cls.realtime - cl.reply_time < 120000 ) {
        return;
    }

    cl.reply_time = cls.realtime;
    cl.reply_delta = 1024 + ( rand() & 1023 );
}
#endif


/*
=====================
CL_ParsePrint
=====================
*/
static void CL_ParsePrint( void ) {
    int level;
    char string[MAX_STRING_CHARS];

    level = MSG_ReadByte();
    MSG_ReadString( string, sizeof( string ) );

    SHOWNET( 2, "    %i \"%s\"\n", level, string );

    if( level != PRINT_CHAT ) {
        Com_Printf( "%s", string );
        return;
    }

#if USE_AUTOREPLY
    if( !cls.demo.playback ) {
        CL_CheckForVersion( string );
    }
#endif

    // disable notify
    if( !cl_chat_notify->integer ) {
        Con_SkipNotify( qtrue );
    }

    // filter text
    if( cl_chat_filter->integer ) {
        int len = Q_ClearStr( string, string, MAX_STRING_CHARS - 1 );
        string[len] = '\n';
    }

    Com_LPrintf( PRINT_TALK, "%s", string );

    Con_SkipNotify( qfalse );

#if USE_CHATHUD
    SCR_AddToChatHUD( string );
#endif

    // play sound
    if( cl_chat_sound->string[0] ) {
        S_StartLocalSound_( cl_chat_sound->string );
    }
    
}

/*
=====================
CL_ParseCenterPrint
=====================
*/
static void CL_ParseCenterPrint( void ) {
    char string[MAX_STRING_CHARS];

    MSG_ReadString( string, sizeof( string ) );
    SHOWNET( 2, "    \"%s\"\n", string );
    SCR_CenterPrint( string );
}

/*
=====================
CL_ParseStuffText
=====================
*/
static void CL_ParseStuffText( void ) {
    char s[MAX_STRING_CHARS];

    MSG_ReadString( s, sizeof( s ) );
    SHOWNET( 2, "    \"%s\"\n", s );
    Cbuf_AddText( &cl_cmdbuf, s );
}

/*
=====================
CL_ParseLayout
=====================
*/
static void CL_ParseLayout( void ) {
    MSG_ReadString( cl.layout, sizeof( cl.layout ) );
    SHOWNET( 2, "    \"%s\"\n", cl.layout );
    cl.putaway = qfalse;
}

/*
================
CL_ParseInventory
================
*/
static void CL_ParseInventory( void ) {
    int        i;

    for( i = 0; i < MAX_ITEMS; i++ ) {
        cl.inventory[i] = MSG_ReadShort();
    }
    cl.putaway = qfalse;
}

static void CL_ParseZPacket( void ) {
#if USE_ZLIB
    sizebuf_t   temp;
    byte        buffer[MAX_MSGLEN];
    size_t      inlen, outlen;

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
    cls.z.avail_in = ( uInt )inlen;
    cls.z.next_out = buffer;
    cls.z.avail_out = ( uInt )outlen;
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

static void CL_ParseSetting( void ) {
    uint32_t    index, value;

    index = MSG_ReadLong();
    value = MSG_ReadLong();

    switch( index ) {
#if USE_FPS
    case SVS_FPS:
        if( !value ) {
            value = 10;
        }
        cl.frametime = 1000 / value;
        cl.framefrac = value * 0.001f;
        break;
#endif
    default:
        break;
    }
}

/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage( void ) {
    int         cmd, extrabits;
    size_t      readcount;
    int         index, bits;

#ifdef _DEBUG
    if( cl_shownet->integer == 1 ) {
        Com_Printf( "%"PRIz" ", msg_read.cursize );
    } else if( cl_shownet->integer > 1 ) {
        Com_Printf( "------------------\n" );
    }
#endif

//
// parse the message
//
    while( 1 ) {
        if( msg_read.readcount > msg_read.cursize ) {
            Com_Error( ERR_DROP, "%s: read past end of server message", __func__ );
        }

        readcount = msg_read.readcount;

        if( ( cmd = MSG_ReadByte() ) == -1 ) {
            SHOWNET( 1, "%3"PRIz":END OF MESSAGE\n", msg_read.readcount - 1 );
            break;
        }

        extrabits = cmd >> SVCMD_BITS;
        cmd &= SVCMD_MASK;

#ifdef _DEBUG
        if( cl_shownet->integer > 1 ) {
            MSG_ShowSVC( cmd );
        }
#endif
    
    // other commands
        switch( cmd ) {
        default:
        badbyte:
            Com_Error( ERR_DROP, "%s: illegible server message: %d", __func__, cmd );
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
            continue;
            
        case svc_configstring:
            index = MSG_ReadShort();
            CL_ParseConfigstring( index );
            break;
 
        case svc_sound:
            CL_ParseStartSoundPacket();
            break;
            
        case svc_spawnbaseline:
            index = MSG_ParseEntityBits( &bits );
            CL_ParseBaseline( index, bits );
            break;

        case svc_temp_entity:
            CL_ParseTEntParams();
            break;

        case svc_muzzleflash:
            CL_ParseMuzzleFlashParams();
            break;

        case svc_muzzleflash2:
            CL_ParseMuzzleFlashParams2();
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
            if( cls.serverProtocol < PROTOCOL_VERSION_R1Q2 ) {
                goto badbyte;
            }
            CL_ParseZPacket();
            continue;

        case svc_gamestate:
            if( cls.serverProtocol != PROTOCOL_VERSION_Q2PRO ) {
                goto badbyte;
            }
            CL_ParseGamestate();
            continue;

        case svc_setting:
            if( cls.serverProtocol < PROTOCOL_VERSION_R1Q2 ) {
                goto badbyte;
            }
            CL_ParseSetting();
            continue;
        }

        // copy protocol invariant stuff
        if( cls.demo.recording && !cls.demo.paused ) {
            size_t len = msg_read.readcount - readcount;

            // with modern servers, it is easily possible to overflow
            // the small protocol 34 demo frame... attempt to preserve
            // reliable messages at least, which should come first
            if( cls.demo.buffer.cursize + len < cls.demo.buffer.maxsize ) {
                SZ_Write( &cls.demo.buffer, msg_read.data + readcount, len );
            }
        }
    }

//
// if recording demos, write the message out
//
    if( cls.demo.recording && !cls.demo.paused ) {
        CL_WriteDemoMessage( &cls.demo.buffer );
    }
}

