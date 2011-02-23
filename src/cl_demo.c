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

static byte     demo_buffer[MAX_PACKETLEN_WRITABLE];
static int      demo_extra;

// =========================================================================

/*
====================
CL_WriteDemoMessage

Dumps the current demo message, prefixed by the length.
====================
*/
void CL_WriteDemoMessage( sizebuf_t *buf ) {
    uint32_t msglen;

    if( buf->overflowed ) {
        SZ_Clear( buf );
        Com_WPrintf( "Demo message overflowed (should never happen).\n" );
        return;
    }
    if( !buf->cursize ) {
        return;
    }

    msglen = LittleLong( buf->cursize );
    FS_Write( &msglen, 4, cls.demo.recording );
    FS_Write( buf->data, buf->cursize, cls.demo.recording );

    SZ_Clear( buf );
}

/*
=============
CL_EmitPacketEntities

Writes a delta update of an entity_state_t list to the message.
=============
*/
static void CL_EmitPacketEntities( server_frame_t *from, server_frame_t *to ) {
    entity_state_t    *oldent, *newent;
    int     oldindex, newindex;
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

    MSG_WriteShort( 0 );    // end of packetentities
}

/*
====================
CL_EmitDemoFrame

Writes delta from the last frame we got to the current frame.
====================
*/
void CL_EmitDemoFrame( void ) {
    server_frame_t  *oldframe;
    player_state_t  *oldstate;
    int             lastframe;

    if( cls.demo.paused ) {
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
    MSG_WriteLong( lastframe );    // what we are delta'ing from
    MSG_WriteByte( 0 );    // rate dropped packets

    // send over the areabits
    MSG_WriteByte( cl.frame.areabytes );
    MSG_WriteData( cl.frame.areabits, cl.frame.areabytes );

    // delta encode the playerstate
    MSG_WriteByte( svc_playerinfo );
    MSG_WriteDeltaPlayerstate_Default( oldstate, &cl.frame.ps );
    
    // delta encode the entities
    MSG_WriteByte( svc_packetentities );
    CL_EmitPacketEntities( oldframe, &cl.frame );

    if( cls.demo.buffer.cursize + msg_write.cursize > cls.demo.buffer.maxsize ) {
        Com_DPrintf( "Demo frame overflowed\n" );
        cls.demo.frames_dropped++;
    } else {
        SZ_Write( &cls.demo.buffer, msg_write.data, msg_write.cursize );
        cl.demoframe = cl.frame.number;
        cls.demo.frames_written++;
    }

    SZ_Clear( &msg_write );
}

static void CL_EmitZeroFrame( void ) {
    cl.demodelta++; // insert new zero frame

    MSG_WriteByte( svc_frame );
    MSG_WriteLong( cl.frame.number + cl.demodelta );
    MSG_WriteLong( cl.frame.number + cl.demodelta - 1 );    // what we are delta'ing from
    MSG_WriteByte( 0 );    // rate dropped packets

    // send over the areabits
    MSG_WriteByte( cl.frame.areabytes );
    MSG_WriteData( cl.frame.areabits, cl.frame.areabytes );

    MSG_WriteByte( svc_playerinfo );
    MSG_WriteShort( 0 );
    MSG_WriteLong( 0 );

    MSG_WriteByte( svc_packetentities );
    MSG_WriteShort( 0 );

    CL_WriteDemoMessage( &msg_write );

    cls.demo.frames_written++;

    SZ_Clear( &msg_write );
}

static size_t format_demo_status( char *buffer, size_t size ) {
    off_t pos = FS_Tell( cls.demo.recording );
    size_t len = Com_FormatSizeLong( buffer, size, pos );

    len += Q_scnprintf( buffer + len, size - len, ", %u frames",
        cls.demo.frames_written );

    if( cls.demo.frames_dropped || cls.demo.messages_dropped ) {
        len += Q_scnprintf( buffer + len, size - len, ", %u/%u dropped",
            cls.demo.frames_dropped, cls.demo.messages_dropped );
    }

    return len;
}

/*
====================
CL_Stop_f

stop recording a demo
====================
*/
void CL_Stop_f( void ) {
    uint32_t msglen;
    char buffer[MAX_QPATH];

    if( !cls.demo.recording ) {
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
    msglen = ( uint32_t )-1;
    FS_Write( &msglen, 4, cls.demo.recording );

    FS_Flush( cls.demo.recording );

    format_demo_status( buffer, sizeof( buffer ) );

// close demofile
    FS_FCloseFile( cls.demo.recording );
    cls.demo.recording = 0;
    cls.demo.paused = qfalse;
    cls.demo.frames_written = 0;
    cls.demo.frames_dropped = 0;
    cls.demo.messages_dropped = 0;

// print some statistics
    Com_Printf( "Stopped demo (%s).\n", buffer );
}

static const cmd_option_t o_record[] = {
    { "h", "help", "display this message" },
    { "z", "compress", "compress demo with gzip" },
    { "e", "extended", "use extended packet size" },
    { "s", "standard", "use standard packet size" },
    { NULL }
};

/*
====================
CL_Record_f

record <demoname>

Begins recording a demo from the current position
====================
*/
static void CL_Record_f( void ) {
    char    buffer[MAX_OSPATH];
    int     i, c;
    size_t  len;
    entity_state_t  *ent;
    char            *s;
    qhandle_t       f;
    unsigned        mode = FS_MODE_WRITE;
    size_t          size = MAX_PACKETLEN_WRITABLE_DEFAULT;

    while( ( c = Cmd_ParseOptions( o_record ) ) != -1 ) {
        switch( c ) {
        case 'h':
            Cmd_PrintUsage( o_record, "<filename>" );
            Com_Printf( "Begin client demo recording.\n" );
            Cmd_PrintHelp( o_record );
            return;
        case 'z':
            mode |= FS_FLAG_GZIP;
        case 'e':
            size = MAX_PACKETLEN_WRITABLE;
            break;
        case 's':
            size = MAX_PACKETLEN_WRITABLE_DEFAULT;
            break;
        default:
            return;
        }
    }

    if( cls.demo.recording ) {
        format_demo_status( buffer, sizeof( buffer ) );
        Com_Printf( "Already recording (%s).\n", buffer );
        return;
    }

    if( !cmd_optarg[0] ) {
        Com_Printf( "Missing filename argument.\n" );
        Cmd_PrintHint();
        return;
    }

    if( cls.state != ca_active ) {
        Com_Printf( "You must be in a level to record.\n" );
        return;
    }

    //
    // open the demo file
    //
    f = FS_EasyOpenFile( buffer, sizeof( buffer ), mode,
        "demos/", cmd_optarg, ".dm2" );
    if( !f ) {
        return;
    }

    Com_Printf( "Recording client demo to %s.\n", buffer );

    cls.demo.recording = f;
    cls.demo.paused = qfalse;

    SZ_Init( &cls.demo.buffer, demo_buffer, size );

    demo_extra = 0;

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
    MSG_WriteByte( 1 );    // demos are always attract loops
    MSG_WriteString( cl.gamedir );
    MSG_WriteShort( cl.clientNum );
    MSG_WriteString( cl.configstrings[CS_NAME] );

    // configstrings
    for( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
        s = cl.configstrings[i];
        if( !s[0] ) {
            continue;
        }

        len = strlen( s );
        if( len > MAX_QPATH ) {
            len = MAX_QPATH;
        }
        
        if( msg_write.cursize + len + 4 > MAX_PACKETLEN_WRITABLE_DEFAULT ) {
            CL_WriteDemoMessage( &msg_write );
        }

        MSG_WriteByte( svc_configstring );
        MSG_WriteShort( i );
        MSG_WriteData( s, len );
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
    size_t length, total = 0;

    if( !cls.demo.recording ) {
        Com_Printf( "Not recording a demo.\n" );
        return;
    }
    if( !cls.demo.paused ) {
        Com_Printf( "Suspended demo.\n" );
        cls.demo.paused = qtrue;
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

    Com_Printf( "Resumed demo (%"PRIz" bytes flushed).\n", total );

    cl.demodelta += cl.demoframe - cl.frame.number; // do not create holes
    cls.demo.paused = qfalse;

    // clear dirty configstrings
    memset( cl.dcs, 0, sizeof( cl.dcs ) );
}

static int read_first_message( qhandle_t f ) {
    uint32_t    ul;
    uint16_t    us;
    size_t      msglen;
    ssize_t     read;
    qerror_t    ret;
    int         type;

    // read magic/msglen
    read = FS_Read( &ul, 4, f );
    if( read != 4 ) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    // check for gzip header
    if( ( ( LittleLong( ul ) & 0xe0ffffff ) == 0x00088b1f ) ) {
        ret = FS_FilterFile( f );
        if( ret ) {
            return ret;
        }
        read = FS_Read( &ul, 4, f );
        if( read != 4 ) {
            return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
        }
    }

    // determine demo type
    if( ul == MVD_MAGIC ) {
        read = FS_Read( &us, 2, f );
        if( read != 2 ) {
            return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
        }
        if( !us ) {
            return Q_ERR_UNEXPECTED_EOF;
        }
        msglen = LittleShort( us );
        type = 1;
    } else {
        if( ul == ( uint32_t )-1 ) {
            return Q_ERR_UNEXPECTED_EOF;
        }
        msglen = LittleLong( ul );
        type = 0;
    }

    if( msglen < 64 || msglen > sizeof( msg_read_buffer ) ) {
        return Q_ERR_INVALID_FORMAT;
    }

    SZ_Init( &msg_read, msg_read_buffer, sizeof( msg_read_buffer ) );
    msg_read.cursize = msglen;

    // read packet data
    read = FS_Read( msg_read.data, msglen, f );
    if( read != msglen ) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    return type;
}

static int read_next_message( qhandle_t f ) {
    uint32_t msglen;
    ssize_t read;

    // read msglen
    read = FS_Read( &msglen, 4, f );
    if( read != 4 ) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    // check for EOF packet
    if( msglen == ( uint32_t )-1 ) {
        return 0;
    }

    msglen = LittleLong( msglen );
    if( msglen > sizeof( msg_read_buffer ) ) {
        return Q_ERR_INVALID_FORMAT;
    }

    SZ_Init( &msg_read, msg_read_buffer, sizeof( msg_read_buffer ) );
    msg_read.cursize = msglen;

    // read packet data
    read = FS_Read( msg_read.data, msglen, f );
    if( read != msglen ) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    return 1;
}

static void parse_next_message( void ) {
    int ret;

    ret = read_next_message( cls.demo.playback );
    if( ret <= 0 ) {
        char *s = Cvar_VariableString( "nextserver" );

        if( !s[0] ) {
            if( ret == 0 ) {
                Com_Error( ERR_DISCONNECT, "Demo finished" );
            } else {
                Com_Error( ERR_DROP, "Couldn't read demo: %s", Q_ErrorString( ret ) );
            }
        }

        FS_FCloseFile( cls.demo.playback );
        memset( &cls.demo, 0, sizeof( cls.demo ) );

        Cbuf_AddText( &cmd_buffer, s );
        Cbuf_AddText( &cmd_buffer, "\n" );
        Cvar_Set( "nextserver", "" );
        cls.state = ca_connected;
        return;
    }

    CL_ParseServerMessage();

    if( cls.demo.file_size ) {
        off_t pos = FS_Tell( cls.demo.playback );

        if( pos > cls.demo.file_offset ) {
            cls.demo.file_percent = ( pos - cls.demo.file_offset ) * 100 / cls.demo.file_size;
        }
    }
}

/*
====================
CL_PlayDemo_f
====================
*/
static void CL_PlayDemo_f( void ) {
    char name[MAX_OSPATH];
    qhandle_t demofile;
    char *arg;
    ssize_t len, ofs;
    int type, argc = Cmd_Argc();

    if( argc < 2 ) {
        Com_Printf( "Usage: %s <filename> [...]\n", Cmd_Argv( 0 ) );
        return;
    }

    arg = Cmd_Argv( 1 );
    if( arg[0] == '/' ) {
        // Assume full path is given
        len = Q_strlcpy( name, arg + 1, sizeof( name ) );
        if( len >= sizeof( name ) ) {
            len = Q_ERR_NAMETOOLONG;
            goto fail;
        }
        len = FS_FOpenFile( name, &demofile, FS_MODE_READ );
    } else {
        // Search for matching extensions
        len = Q_concat( name, sizeof( name ), "demos/", arg, NULL );
        if( len >= sizeof( name ) ) {
            len = Q_ERR_NAMETOOLONG;
            goto fail;
        }
        len = FS_FOpenFile( name, &demofile, FS_MODE_READ );    
        if( !demofile ) {
            len = COM_DefaultExtension( name, ".dm2", sizeof( name ) );
            if( len >= sizeof( name ) ) {
                len = Q_ERR_NAMETOOLONG;
                goto fail;
            }
            len = FS_FOpenFile( name, &demofile, FS_MODE_READ );
        }
    }

    if( !demofile ) {
fail:
        Com_Printf( "Couldn't open %s: %s\n", name, Q_ErrorString( len ) );
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

    type = read_first_message( demofile );
    if( type < 0 ) {
        Com_Printf( "Couldn't read %s: %s\n", name, Q_ErrorString( type ) );
        FS_FCloseFile( demofile );
        return;
    }

    if( type == 1 ) {
        Cbuf_InsertText( &cmd_buffer, va( "mvdplay --replace @@ /%s\n", name ) );
        FS_FCloseFile( demofile );
        return;
    }

    // if running a local server, kill it and reissue
    SV_Shutdown( "Server was killed.\n", ERR_DISCONNECT );

    CL_Disconnect( ERR_RECONNECT );

    cls.demo.playback = demofile;
    cls.state = ca_connected;
    Q_strlcpy( cls.servername, COM_SkipPath( name ), sizeof( cls.servername ) );
    cls.serverAddress.type = NA_LOOPBACK;

    Con_Popup();
    SCR_UpdateScreen();

    CL_ParseServerMessage();
    while( cls.state == ca_connected ) {
        Cbuf_Execute( &cl_cmdbuf );
        parse_next_message();
    }

    ofs = FS_Tell( demofile );
    if( ofs > 0 ) {
        cls.demo.file_offset = ofs;
        cls.demo.file_size = len - ofs;
    }

    if( com_timedemo->integer ) {
        cls.demo.time_frames = 0;
        cls.demo.time_start = Sys_Milliseconds();
    }
}

static void CL_Demo_c( genctx_t *ctx, int argnum ) {
    if( argnum == 1 ) {
        FS_File_g( "demos", "*.dm2;*.dm2.gz;*.mvd2;*.mvd2.gz", FS_SEARCH_SAVEPATH | FS_SEARCH_BYFILTER, ctx );
    }
}

static void parse_info_string( demoInfo_t *info, int clientNum, int index, const char *string ) {
    size_t len;
    char *p;

    if( index >= CS_PLAYERSKINS && index < CS_PLAYERSKINS + MAX_CLIENTS ) {
        if( index - CS_PLAYERSKINS == clientNum ) {
            Q_strlcpy( info->pov, string, sizeof( info->pov ) );
            p = strchr( info->pov, '\\' );
            if( p ) {
                *p = 0;
            }
        }
    } else if( index == CS_MODELS + 1 ) {
        len = strlen( string );
        if( len > 9 ) {
            memcpy( info->map, string + 5, len - 9 ); // skip "maps/"
            info->map[ len - 9 ] = 0; // cut off ".bsp"
        }
    }
}

/*
====================
CL_GetDemoInfo
====================
*/
demoInfo_t *CL_GetDemoInfo( const char *path, demoInfo_t *info ) {
    qhandle_t f;
    int c, index;
    char string[MAX_QPATH];
    int clientNum, type;

    FS_FOpenFile( path, &f, FS_MODE_READ );
    if( !f ) {
        return NULL;
    }

    type = read_first_message( f );
    if( type < 0 ) {
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
        MSG_ReadString( NULL, 0);
        clientNum = MSG_ReadShort();
        MSG_ReadString( NULL, 0 );

        while( 1 ) {
            c = MSG_ReadByte();
            if( c == -1 ) {
                if( read_next_message( f ) <= 0 ) {
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
            MSG_ReadString( string, sizeof( string ) );
            parse_info_string( info, clientNum, index, string );
        }

        info->mvd = qfalse;
    } else {
        if( ( MSG_ReadByte() & SVCMD_MASK ) != mvd_serverdata ) {
            goto fail;
        }
        if( MSG_ReadLong() != PROTOCOL_VERSION_MVD ) {
            goto fail;
        }
        MSG_ReadShort();
        MSG_ReadLong();
        MSG_ReadString( NULL, 0 );
        clientNum = MSG_ReadShort();

        while( 1 ) {
            index = MSG_ReadShort();
            if( index == MAX_CONFIGSTRINGS ) {
                break;
            }
            if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
                goto fail;
            }
            MSG_ReadString( string, sizeof( string ) );
            parse_info_string( info, clientNum, index, string );
        }

        info->mvd = qtrue;
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
void CL_DemoFrame( int msec ) {
    if( cls.state < ca_connected ) {
        return;
    }

    if( cls.state != ca_active ) {
        parse_next_message();
        return;
    }

    if( cls.demo.recording && cl_paused->integer == 2 && !cls.demo.paused ) {
        // XXX: record zero frames when manually paused
        // for syncing with audio comments, etc
        demo_extra += msec;
        if( demo_extra > 100 ) {
            CL_EmitZeroFrame();
            demo_extra = 0;
        }
    }

    if( com_timedemo->integer ) {
        parse_next_message();
        cl.time = cl.servertime;
        cls.demo.time_frames++;
        return;
    }

    // cl.time has already been advanced for this client frame
    // read the next frame to start lerp cycle again
    while( cl.servertime < cl.time ) {
        parse_next_message();
        if( cls.state != ca_active ) {
            break;
        }
    }
}

static const cmdreg_t c_demo[] = {
    { "demo", CL_PlayDemo_f, CL_Demo_c },
    { "record", CL_Record_f, CL_Demo_c },
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


