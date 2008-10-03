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

cvar_t  *sv_mvd_enable;
cvar_t  *sv_mvd_auth;
cvar_t  *sv_mvd_noblend;
cvar_t  *sv_mvd_nogun;
cvar_t  *sv_mvd_nomsgs;

typedef struct {
    client_t        *dummy;
    unsigned        layout_time;

    // reliable data, may not be discarded
    sizebuf_t       message;

    // unreliable data, may be discarded
    sizebuf_t       datagram;

    // delta compressor buffers
    player_state_t  *players;  // [maxclients]
    entity_state_t  *entities; // [MAX_EDICTS]

    // local recorder
    fileHandle_t    recording; 
    int             numlevels; // stop after that many levels
    int             numframes; // stop after that many frames
} mvd_server_t;

static mvd_server_t     mvd;

// TCP client list
static LIST_DECL( mvd_client_list );

static cvar_t  *sv_mvd_max_size;
static cvar_t  *sv_mvd_max_duration;
static cvar_t  *sv_mvd_max_levels;
static cvar_t  *sv_mvd_begincmd;
static cvar_t  *sv_mvd_scorecmd;
static cvar_t  *sv_mvd_autorecord;
#if USE_ZLIB
static cvar_t  *sv_mvd_encoding;
#endif
static cvar_t  *sv_mvd_capture_flags;

static qboolean init_mvd( void );

static void     rec_stop( void ); 
static qboolean rec_allowed( void );
static void     rec_start( fileHandle_t demofile );


/*
==============================================================================

DUMMY MVD CLIENT

MVD dummy is a fake client maintained entirely server side.
Representing MVD observers, this client is used to obtain base playerstate
for freefloat observers, receive scoreboard updates and text messages, etc.

==============================================================================
*/

static cmdbuf_t    dummy_buffer;
static char        dummy_buffer_text[MAX_STRING_CHARS];

static void dummy_wait_f( void ) {
    int count = atoi( Cmd_Argv( 1 ) );

    if( count < 1 ) {
        count = 1;
    }
    dummy_buffer.waitCount = count;
}

static void dummy_forward_f( void ) {
    Cmd_Shift();
    Com_DPrintf( "dummy cmd: %s %s\n", Cmd_Argv( 0 ), Cmd_Args() );

    sv_client = mvd.dummy;
    sv_player = sv_client->edict;
    ge->ClientCommand( sv_player );
    sv_client = NULL;
    sv_player = NULL;
}

static void dummy_record_f( void ) {
    char buffer[MAX_OSPATH];
    fileHandle_t demofile;
    size_t len;

    if( !sv_mvd_autorecord->integer ) {
        return;
    }

    if( Cmd_Argc() < 2 ) {
        Com_Printf( "Usage: %s <filename>\n", Cmd_Argv( 0 ) );
        return;
    }

    if( !rec_allowed() ) {
        return;
    }

    len = Q_concat( buffer, sizeof( buffer ), "demos/", Cmd_Argv( 1 ), ".mvd2", NULL );
    if( len >= sizeof( buffer ) ) {
        Com_EPrintf( "Oversize filename specified.\n" );
        return;
    }

    FS_FOpenFile( buffer, &demofile, FS_MODE_WRITE );
    if( !demofile ) {
        Com_EPrintf( "Couldn't open %s for writing\n", buffer );
        return;
    }

    if( !init_mvd() ) {
        FS_FCloseFile( demofile );
        return;
    }

    rec_start( demofile );

    Com_Printf( "Auto-recording local MVD to %s\n", buffer );
}

static void dummy_stop_f( void ) {
    if( !sv_mvd_autorecord->integer ) {
        return;
    }

    if( !mvd.recording ) {
        Com_Printf( "Not recording a local MVD.\n" );
        return;
    }

    Com_Printf( "Stopped local MVD auto-recording.\n" );
    rec_stop();
}

static const ucmd_t dummy_cmds[] = {
    { "cmd", dummy_forward_f },
    { "set", Cvar_Set_f },
    { "alias", Cmd_Alias_f },
    { "play", NULL },
    { "exec", NULL },
    { "wait", dummy_wait_f },
    { "record", dummy_record_f },
    { "stop", dummy_stop_f },
    { NULL, NULL }
};

static void dummy_exec_string( const char *line ) {
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
            Com_WPrintf( "dummy_exec_string: runaway alias loop\n" );
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
    sv_client = mvd.dummy;
    sv_player = mvd.dummy->edict;
    ge->ClientCommand( sv_player );
    sv_client = NULL;
    sv_player = NULL;
}

static void dummy_add_message( client_t *client, byte *data,
                              size_t length, qboolean reliable )
{
    if( !length || !reliable ) {
        return;
    }

    if( data[0] == svc_stufftext ) {
        data[length] = 0;
        Cbuf_AddTextEx( &dummy_buffer, ( char * )( data + 1 ) );
        return;
    }
}

static void dummy_spawn( void ) {
    sv_client = mvd.dummy;
    sv_player = sv_client->edict;
    ge->ClientBegin( sv_player );
    sv_client = NULL;
    sv_player = NULL;

    if( sv_mvd_begincmd->string[0] ) {
        Cbuf_AddTextEx( &dummy_buffer, sv_mvd_begincmd->string );
    }

    mvd.layout_time = svs.realtime;

    mvd.dummy->state = cs_spawned;
}

static client_t *find_slot( void ) {
    client_t *c;
    int i, j;

    // first check if there is a free reserved slot
    j = sv_maxclients->integer - sv_reserved_slots->integer;
    for( i = j; i < sv_maxclients->integer; i++ ) {
        c = &svs.udp_client_pool[i];
        if( !c->state ) {
            return c;
        }
    }

    // then check regular slots
    for( i = 0; i < j; i++ ) {
        c = &svs.udp_client_pool[i];
        if( !c->state ) {
            return c;
        }
    }

    return NULL;
}

static qboolean dummy_create( void ) {
    client_t *newcl;
    char userinfo[MAX_INFO_STRING];
    char *s;
    qboolean allow;
    int number;

    // find a free client slot
    newcl = find_slot();
    if( !newcl ) {
        Com_EPrintf( "No slot for dummy MVD client\n" );
        return qfalse;
    }

    memset( newcl, 0, sizeof( *newcl ) );
    number = newcl - svs.udp_client_pool;
    newcl->number = newcl->slot = number;
    newcl->protocol = -1;
    newcl->state = cs_connected;
    newcl->AddMessage = dummy_add_message;
    newcl->edict = EDICT_NUM( number + 1 );
    newcl->netchan = SV_Mallocz( sizeof( netchan_t ) );
    newcl->netchan->remote_address.type = NA_LOOPBACK;

    List_Init( &newcl->entry );

    Q_snprintf( userinfo, sizeof( userinfo ),
        "\\name\\[MVDSPEC]\\skin\\male/grunt\\mvdspec\\%d\\ip\\loopback",
        PROTOCOL_VERSION_MVD_CURRENT );

    mvd.dummy = newcl;

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
        mvd.dummy = NULL;
        return qfalse;
    }

    // parse some info from the info strings
    strcpy( newcl->userinfo, userinfo );
    SV_UserinfoChanged( newcl );

    return qtrue;
}

static void dummy_run( void ) {
    usercmd_t cmd;

    Cbuf_ExecuteEx( &dummy_buffer );
    if( dummy_buffer.waitCount > 0 ) {
        dummy_buffer.waitCount--;
    }

    // run ClientThink to prevent timeouts, etc
    memset( &cmd, 0, sizeof( cmd ) );
    cmd.msec = 100;
    sv_client = mvd.dummy;
    sv_player = sv_client->edict;
    ge->ClientThink( sv_player, &cmd );
    sv_client = NULL;
    sv_player = NULL;

    // check if the layout is constantly updated. if not,
    // game mod has probably closed the scoreboard, open it again
    if( sv_mvd_scorecmd->string[0] ) {
        if( svs.realtime - mvd.layout_time > 9000 ) {
            Cbuf_AddTextEx( &dummy_buffer, sv_mvd_scorecmd->string );
            mvd.layout_time = svs.realtime;
        }
    }
}

static void dummy_drop( const char *reason ) {
    tcpClient_t *client;

    rec_stop();

    LIST_FOR_EACH( tcpClient_t, client, &mvd_client_list, mvdEntry ) {
        SV_HttpDrop( client, reason );
    }

    SV_RemoveClient( mvd.dummy );
    mvd.dummy = NULL;

    SZ_Clear( &mvd.datagram );
    SZ_Clear( &mvd.message );
}

/*
==============================================================================

FRAME UPDATES

As MVD stream operates over reliable transport, there is no concept of
"baselines" and delta compression is always performed from the last
state seen on the map. There is also no support for "nodelta" frames
(except the very first frame sent as part of the gamestate).

This allows building only one update per frame and multicasting it to
several destinations at once.

Additional bandwidth savings are performed by filtering out origin and
angles updates on player entities, as MVD client can easily recover them
from corresponding player states, assuming those are kept in sync by the
game mod. This assumption should be generally true for moving players,
as vanilla Q2 server performs PVS/PHS culling for them using origin from
entity states, but not player states.

==============================================================================
*/

/*
==================
player_is_active

Attempts to determine if the given player entity is active,
and the given player state should be captured into MVD stream.

Entire function is a nasty hack. Ideally a compatible game DLL
should do it for us by providing some SVF_* flag or something.
==================
*/
static qboolean player_is_active( const edict_t *ent ) {
    int num;

    if( ( g_features->integer & GMF_PROPERINUSE ) && !ent->inuse ) {
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

    // by default, check if client is actually connected
    // it may not be the case for bots!
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
    if( ent == mvd.dummy->edict ) {
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

    // they are likely following someone in case of PM_FREEZE
    if( ent->client->ps.pmove.pm_type == PM_FREEZE ) {
        return qfalse;
    }

    // they are likely following someone if PMF_NO_PREDICTION is set 
    if( ent->client->ps.pmove.pm_flags & PMF_NO_PREDICTION ) {
        return qfalse;
    }

    return qtrue;
}

/*
==================
build_gamestate

Initialize MVD delta compressor for the first time on the given map.
==================
*/
static void build_gamestate( void ) {
    player_state_t *ps;
    entity_state_t *es;
    edict_t *ent;
    int i;

    memset( mvd.players, 0, sizeof( player_state_t ) * sv_maxclients->integer );
    memset( mvd.entities, 0, sizeof( entity_state_t ) * MAX_EDICTS );

    // set base player states
    for( i = 0; i < sv_maxclients->integer; i++ ) {
        ent = EDICT_NUM( i + 1 );

        if( !player_is_active( ent ) ) {
            continue;
        }

        ps = &mvd.players[i];
        *ps = ent->client->ps;
        PPS_INUSE( ps ) = qtrue;
    }

    // set base entity states
    for( i = 1; i < ge->num_edicts; i++ ) {
        ent = EDICT_NUM( i );

        if( ( ent->svflags & SVF_NOCLIENT ) || !ES_INUSE( &ent->s ) ) {
            continue;
        }

        es = &mvd.entities[i];
        *es = ent->s;
        es->number = i;
    }
}


/*
==================
emit_gamestate

Writes a single giant message with all the startup info,
followed by an uncompressed (baseline) frame.
==================
*/
static void emit_gamestate( void ) {
    char        *string;
    int         i, j;
    player_state_t  *ps;
    entity_state_t  *es;
    size_t      length;
    uint8_t     *patch;
    int         flags, extra, portalbytes;
    byte        portalbits[MAX_MAP_AREAS/8];

    patch = SZ_GetSpace( &msg_write, 2 );

    // send the serverdata
    MSG_WriteByte( mvd_serverdata );
    MSG_WriteLong( PROTOCOL_VERSION_MVD );
    MSG_WriteShort( PROTOCOL_VERSION_MVD_CURRENT );
    MSG_WriteLong( sv.spawncount );
    MSG_WriteString( fs_game->string );
    MSG_WriteShort( mvd.dummy->number );

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
    for( i = 0, ps = mvd.players; i < sv_maxclients->integer; i++, ps++ ) {
        extra = 0;
        if( !PPS_INUSE( ps ) ) {
            extra |= MSG_PS_REMOVE;
        }
        MSG_WriteDeltaPlayerstate_Packet( NULL, ps, i, flags | extra );
    }
    MSG_WriteByte( CLIENTNUM_NONE );

    // send entity states
    for( i = 1, es = mvd.entities + 1; i < ge->num_edicts; i++, es++ ) {
        flags = 0;
        if( i <= sv_maxclients->integer ) {
            ps = &mvd.players[ i - 1 ];
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


static void copy_entity_state( entity_state_t *dst, const entity_state_t *src, int flags ) {
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
emit_frame

Builds new MVD frame by capturing all entity and player states
and calculating portalbits. The same frame is used for all MVD
clients, as well as local recorder.
==================
*/
static void emit_frame( void ) {
    player_state_t *oldps, *newps;
    entity_state_t *oldes, *newes;
    edict_t *ent;
    int flags, portalbytes;
    byte portalbits[MAX_MAP_AREAS/8];
    int i;

    MSG_WriteByte( mvd_frame );

    // send portal bits
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

    // send player states
    for( i = 0; i < sv_maxclients->integer; i++ ) {
        ent = EDICT_NUM( i + 1 );

        oldps = &mvd.players[i];
        newps = &ent->client->ps;

        if( !player_is_active( ent ) ) {
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

        // shuffle current state to previous
        *oldps = *newps;
        PPS_INUSE( oldps ) = qtrue;
    }

    MSG_WriteByte( CLIENTNUM_NONE );    // end of packetplayers

    // send entity states
    for( i = 1; i < ge->num_edicts; i++ ) {
        ent = EDICT_NUM( i );

        oldes = &mvd.entities[i];
        newes = &ent->s;

        if( ( ent->svflags & SVF_NOCLIENT ) || !ES_INUSE( newes ) ) {
            if( oldes->number ) {
                // the old entity isn't present in the new message
                MSG_WriteDeltaEntity( oldes, NULL, MSG_ES_FORCE );
                oldes->number = 0;
            }
            continue;
        }

        // calculate flags
        flags = 0;
        if( i <= sv_maxclients->integer ) {
            oldps = &mvd.players[ i - 1 ];
            if( PPS_INUSE( oldps ) && oldps->pmove.pm_type == PM_NORMAL ) {
                // do not waste bandwidth on origin/angle updates,
                // client will recover them from player state
                flags |= MSG_ES_FIRSTPERSON;
            }
        }
        
        if( !oldes->number ) {
            // this is a new entity, send it from the last state
            flags |= MSG_ES_FORCE|MSG_ES_NEWENTITY;
        }
        
        MSG_WriteDeltaEntity( oldes, newes, flags );

        // shuffle current state to previous
        copy_entity_state( oldes, newes, flags );
        oldes->number = i;
    }

    MSG_WriteShort( 0 );    // end of packetentities
}

// if dummy is not yet connected, create and spawn it
static qboolean init_mvd( void ) {
    if( !mvd.dummy ) {
        if( !dummy_create() ) {
            return qfalse;
        }
        dummy_spawn();
        build_gamestate();
    }
    return qtrue;
}

/*
==================
SV_MvdRunFrame
==================
*/
void SV_MvdRunFrame( void ) {
    tcpClient_t *client;
    uint16_t length;

    if( !mvd.dummy ) {
        return;
    }

    dummy_run();

    if( mvd.message.overflowed ) {
        // if reliable message overflowed, kick all clients
        Com_EPrintf( "Reliable MVD message overflowed\n" );
        dummy_drop( "overflowed" );
        goto clear;
    }

    if( mvd.datagram.overflowed ) {
        Com_WPrintf( "Unreliable MVD datagram overflowed\n" );
        SZ_Clear( &mvd.datagram );
    }

    // emit a single delta update
    emit_frame();

    // check if frame fits
    // FIXME: dumping frame should not be allowed in current design
    if( mvd.message.cursize + msg_write.cursize > MAX_MSGLEN ) {
        Com_WPrintf( "Dumping MVD frame: %"PRIz" bytes\n", msg_write.cursize );
        SZ_Clear( &msg_write );
    }

    // check if unreliable datagram fits
    if( mvd.message.cursize + msg_write.cursize + mvd.datagram.cursize > MAX_MSGLEN ) {
        Com_WPrintf( "Dumping unreliable MVD datagram: %"PRIz" bytes\n", mvd.datagram.cursize );
        SZ_Clear( &mvd.datagram );
    }

    length = LittleShort( mvd.message.cursize + msg_write.cursize + mvd.datagram.cursize );

    // send frame to clients
    LIST_FOR_EACH( tcpClient_t, client, &mvd_client_list, mvdEntry ) {
        if( client->state == cs_spawned ) {
            SV_HttpWrite( client, &length, 2 );
            SV_HttpWrite( client, mvd.message.data, mvd.message.cursize );
            SV_HttpWrite( client, msg_write.data, msg_write.cursize );
            SV_HttpWrite( client, mvd.datagram.data, mvd.datagram.cursize );
#if USE_ZLIB
            client->noflush++;
#endif
        }
    }

    // write frame to demofile
    if( mvd.recording ) {
        FS_Write( &length, 2, mvd.recording );
        FS_Write( mvd.message.data, mvd.message.cursize, mvd.recording );
        FS_Write( msg_write.data, msg_write.cursize, mvd.recording );
        FS_Write( mvd.datagram.data, mvd.datagram.cursize, mvd.recording );

        if( sv_mvd_max_size->value > 0 ) {
            int numbytes = FS_RawTell( mvd.recording );

            if( numbytes > sv_mvd_max_size->value * 1000 ) {
                Com_Printf( "Stopping MVD recording, maximum size reached.\n" );
                rec_stop();
            }
        } else if( sv_mvd_max_duration->value > 0 &&
            ++mvd.numframes > sv_mvd_max_duration->value * 600 )
        {
            Com_Printf( "Stopping MVD recording, maximum duration reached.\n" );
            rec_stop();
        }
    }

    SZ_Clear( &msg_write );

clear:
    SZ_Clear( &mvd.datagram );
    SZ_Clear( &mvd.message );
}

/*
==============================================================================

CLIENT HANDLING

==============================================================================
*/

static void SV_MvdClientNew( tcpClient_t *client ) {
    Com_DPrintf( "Sending gamestate to MVD client %s\n",
        NET_AdrToString( &client->stream.address ) );
    client->state = cs_spawned;

    emit_gamestate();

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

    if( !init_mvd() ) {
        SV_HttpReject( "503 Service Unavailable",
            "Unable to create dummy MVD client. Server is full." );
        return;
    }

    List_Append( &mvd_client_list, &http_client->mvdEntry );

    SV_MvdInitStream();

    SV_MvdClientNew( http_client );
}

/*
==============================================================================

SERVER HOOKS

These hooks are called by server code when some event occurs.

==============================================================================
*/

/*
==================
SV_MvdMapChanged

Server has just changed the map, spawn the MVD dummy and go!
==================
*/
void SV_MvdMapChanged( void ) {
    tcpClient_t *client;

    if( !sv_mvd_enable->integer ) {
        return; // do noting if disabled
    }

    if( !mvd.dummy ) {
        if( !sv_mvd_autorecord->integer ) {
            return; // not listening for autorecord command
        }
        if( !dummy_create() ) {
            return;
        }
        Com_Printf( "Spawning MVD dummy for auto-recording\n" );
    }

    SZ_Clear( &mvd.datagram );
    SZ_Clear( &mvd.message );

    dummy_spawn();
    build_gamestate();
    emit_gamestate();

    // send gamestate to all MVD clients
    LIST_FOR_EACH( tcpClient_t, client, &mvd_client_list, mvdEntry ) {
        SV_HttpWrite( client, msg_write.data, msg_write.cursize );
    }

    if( mvd.recording ) {
        int maxlevels = sv_mvd_max_levels->integer;
    
        // check if it is time to stop recording
        if( maxlevels > 0 && ++mvd.numlevels >= maxlevels ) {
            Com_Printf( "Stopping MVD recording, "
                "maximum number of level changes reached.\n" );
            rec_stop();
        } else {
            FS_Write( msg_write.data, msg_write.cursize, mvd.recording );
        }
    }

    SZ_Clear( &msg_write );
}

/*
==================
SV_MvdClientDropped

Server has just dropped a client, check if that was our MVD dummy client.
==================
*/
void SV_MvdClientDropped( client_t *client, const char *reason ) {
    if( client == mvd.dummy ) {
        dummy_drop( reason );
    }
}

/*
==================
SV_MvdInit

Server is initializing, prepare MVD server for this game.
==================
*/
void SV_MvdInit( void ) {
    if( !sv_mvd_enable->integer ) {
        return; // do noting if disabled
    }

    // allocate buffers
    Z_TagReserve( sizeof( player_state_t ) * sv_maxclients->integer +
        sizeof( entity_state_t ) * MAX_EDICTS + MAX_MSGLEN * 2, TAG_SERVER );
    SZ_Init( &mvd.message, Z_ReservedAlloc( MAX_MSGLEN ), MAX_MSGLEN );
    SZ_Init( &mvd.datagram, Z_ReservedAlloc( MAX_MSGLEN ), MAX_MSGLEN );
    mvd.players = Z_ReservedAlloc( sizeof( player_state_t ) * sv_maxclients->integer );
    mvd.entities = Z_ReservedAlloc( sizeof( entity_state_t ) * MAX_EDICTS );

    // reserve the slot for dummy MVD client
    if( !sv_reserved_slots->integer ) {
        Cvar_Set( "sv_reserved_slots", "1" );
    }
}

/*
==================
SV_MvdShutdown

Server is shutting down, clean everything up.
==================
*/
void SV_MvdShutdown( void ) {
    tcpClient_t *t;
    uint16_t length;

    // stop recording
    rec_stop();

    // send EOF to MVD clients
    // NOTE: not clearing mvd_client_list as clients will be
    // properly dropped by SV_FinalMessage just after
    length = 0;
    LIST_FOR_EACH( tcpClient_t, t, &mvd_client_list, mvdEntry ) {
        SV_HttpWrite( t, &length, 2 );
        SV_HttpFinish( t );
    }

    // remove MVD dummy
    if( mvd.dummy ) {
        SV_RemoveClient( mvd.dummy );
    }

    // free static data
    Z_Free( mvd.message.data );

    memset( &mvd, 0, sizeof( mvd ) );
}


/*
==============================================================================

GAME API HOOKS

These hooks are called from PF_* functions to add additional
out-of-band data into the MVD stream.

==============================================================================
*/

/*
==============
SV_MvdMulticast

TODO: would be better to combine identical unicast/multicast messages
into one larger message to save space (useful for shotgun patterns
as they often occur in the same BSP leaf)
==============
*/
void SV_MvdMulticast( int leafnum, multicast_t to ) {
    mvd_ops_t   op;
    sizebuf_t   *buf;
    int         bits;
    
    // do nothing if not active
    if( !mvd.dummy ) {
        return;
    }

    op = mvd_multicast_all + to;
    buf = to < MULTICAST_ALL_R ? &mvd.datagram : &mvd.message;
    bits = ( msg_write.cursize >> 8 ) & 7;

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

Performs some basic filtering of the unicast data that would be
otherwise discarded by the MVD client.
==============
*/
void SV_MvdUnicast( edict_t *ent, int clientNum, qboolean reliable ) {
    mvd_ops_t   op;
    sizebuf_t   *buf;
    int         bits;

    // do nothing if not active
    if( !mvd.dummy ) {
        return;
    }

    // discard any data to players not in the game
    if( !player_is_active( ent ) ) {
        return;
    }

    switch( msg_write.data[0] ) {
    case svc_layout:
        if( ent == mvd.dummy->edict ) {
            // special case, send to all observers
            mvd.layout_time = svs.realtime;
        } else {
            // discard any layout updates to players
            return;
        }
        break;
    case svc_stufftext:
        if( memcmp( msg_write.data + 1, "play ", 5 ) ) {
            // discard any stufftexts, except of play sound hacks
            return;
        }
        break;
    case svc_print:
        if( ent != mvd.dummy->edict && sv_mvd_nomsgs->integer ) {
            // optionally discard text messages to players
            return;
        }
        break;
    }

    // decide where should it go
    if( reliable ) {
        op = mvd_unicast_r;
        buf = &mvd.message;
    } else {
        op = mvd_unicast;
        buf = &mvd.datagram;
    }

    // write it
    bits = ( msg_write.cursize >> 8 ) & 7;
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
    if( mvd.dummy ) {
        SZ_WriteByte( &mvd.message, mvd_configstring );
        SZ_WriteShort( &mvd.message, index );
        SZ_WriteString( &mvd.message, string );
    }
}

/*
==============
SV_MvdBroadcastPrint
==============
*/
void SV_MvdBroadcastPrint( int level, const char *string ) {
    if( mvd.dummy ) {
        SZ_WriteByte( &mvd.message, mvd_print );
        SZ_WriteByte( &mvd.message, level );
        SZ_WriteString( &mvd.message, string );
    }
}

/*
==============
SV_MvdStartSound

FIXME: origin will be incorrect on entities not captured this frame
==============
*/
void SV_MvdStartSound( int entnum, int channel, int flags,
                        int soundindex, int volume,
                        int attenuation, int timeofs )
{
    int extrabits, sendchan;

    if( !mvd.dummy ) {
        return;
    }

    extrabits = 0;
    if( channel & CHAN_NO_PHS_ADD ) {
        extrabits |= 1 << SVCMD_BITS;
    }
    if( channel & CHAN_RELIABLE ) {
        // FIXME: write to mvd.message
        extrabits |= 2 << SVCMD_BITS;
    }

    SZ_WriteByte( &mvd.datagram, mvd_sound | extrabits );
    SZ_WriteByte( &mvd.datagram, flags );
    SZ_WriteByte( &mvd.datagram, soundindex );

    if( flags & SND_VOLUME )
        SZ_WriteByte( &mvd.datagram, volume );
    if( flags & SND_ATTENUATION )
        SZ_WriteByte( &mvd.datagram, attenuation );
    if( flags & SND_OFFSET )
        SZ_WriteByte( &mvd.datagram, timeofs );

    sendchan = ( entnum << 3 ) | ( channel & 7 );
    SZ_WriteShort( &mvd.datagram, sendchan );
}


/*
==============================================================================

LOCAL MVD RECORDER

==============================================================================
*/

/*
==============
rec_stop

Stops server local MVD recording.
==============
*/
static void rec_stop( void ) {
    uint16_t length;

    if( !mvd.recording ) {
        return;
    }
    
    // write demo EOF marker
    length = 0;
    FS_Write( &length, 2, mvd.recording );

    FS_FCloseFile( mvd.recording );
    mvd.recording = 0;
}

static qboolean rec_allowed( void ) {
    if( !mvd.entities ) {
        Com_Printf( "MVD recording is disabled on this server.\n" );
        return qfalse;
    }
    if( mvd.recording ) {
        Com_Printf( "Already recording a local MVD.\n" );
        return qfalse;
    }
    return qtrue;
}

static void rec_start( fileHandle_t demofile ) {
    uint32_t magic;

    mvd.recording = demofile;
    mvd.numlevels = 0;
    mvd.numframes = 0;
    
    magic = MVD_MAGIC;
    FS_Write( &magic, 4, demofile );

    emit_gamestate();
    FS_Write( msg_write.data, msg_write.cursize, demofile );
    SZ_Clear( &msg_write );
}


const cmd_option_t o_mvdrecord[] = {
    { "h", "help", "display this message" },
    { "z", "gzip", "compress file with gzip" },
    { NULL }
};

extern void MVD_StreamedStop_f( void );
extern void MVD_StreamedRecord_f( void );
extern void MVD_File_g( genctx_t *ctx );

static void SV_MvdRecord_c( genctx_t *ctx, int argnum ) {
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
static void SV_MvdRecord_f( void ) {
    char buffer[MAX_OSPATH];
    fileHandle_t demofile;
    qboolean gzip = qfalse;
    int c;
    size_t len;

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

    if( !rec_allowed() ) {
        return;
    }

    //
    // open the demo file
    //
    len = Q_concat( buffer, sizeof( buffer ), "demos/", cmd_optarg,
        gzip ? ".mvd2.gz" : ".mvd2", NULL );
    if( len >= sizeof( buffer ) ) {
        Com_EPrintf( "Oversize filename specified.\n" );
        return;
    }

    FS_FOpenFile( buffer, &demofile, FS_MODE_WRITE );
    if( !demofile ) {
        Com_EPrintf( "Couldn't open %s for writing\n", buffer );
        return;
    }

    if( !init_mvd() ) {
        FS_FCloseFile( demofile );
        return;
    }

    if( gzip ) {
        FS_FilterFile( demofile );
    }

    rec_start( demofile );

    Com_Printf( "Recording local MVD to %s\n", buffer );
}


/*
==============
MVD_Stop_f

Ends server MVD recording
==============
*/
static void SV_MvdStop_f( void ) {
    if( sv.state == ss_broadcast ) {
        MVD_StreamedStop_f();
        return;
    }
    if( !mvd.recording ) {
        Com_Printf( "Not recording a local MVD.\n" );
        return;
    }

    Com_Printf( "Stopped local MVD recording.\n" );
    rec_stop();
}

static void SV_MvdStuff_f( void ) {
    if( mvd.dummy ) {
        Cbuf_AddTextEx( &dummy_buffer, Cmd_RawArgs() );
        Cbuf_AddTextEx( &dummy_buffer, "\n" );
    } else {
        Com_Printf( "Can't '%s', dummy MVD client is not active\n", Cmd_Argv( 0 ) );
    }
}

static const cmdreg_t c_svmvd[] = {
    { "mvdrecord", SV_MvdRecord_f, SV_MvdRecord_c },
    { "mvdstop", SV_MvdStop_f },
    { "mvdstuff", SV_MvdStuff_f },

    { NULL }
};

void SV_MvdRegister( void ) {
    sv_mvd_enable = Cvar_Get( "sv_mvd_enable", "0", CVAR_LATCH );
    sv_mvd_auth = Cvar_Get( "sv_mvd_auth", "", CVAR_PRIVATE );
    sv_mvd_max_size = Cvar_Get( "sv_mvd_max_size", "0", 0 );
    sv_mvd_max_duration = Cvar_Get( "sv_mvd_max_duration", "0", 0 );
    sv_mvd_max_levels = Cvar_Get( "sv_mvd_max_levels", "1", 0 );
    sv_mvd_noblend = Cvar_Get( "sv_mvd_noblend", "0", CVAR_LATCH );
    sv_mvd_nogun = Cvar_Get( "sv_mvd_nogun", "1", CVAR_LATCH );
    sv_mvd_nomsgs = Cvar_Get( "sv_mvd_nomsgs", "1", CVAR_LATCH );
    sv_mvd_begincmd = Cvar_Get( "sv_mvd_begincmd",
        "wait 50; putaway; wait 10; help;", 0 );
    sv_mvd_scorecmd = Cvar_Get( "sv_mvd_scorecmd",
        "putaway; wait 10; help;", 0 );
    sv_mvd_autorecord = Cvar_Get( "sv_mvd_autorecord", "0", 0 );
#if USE_ZLIB
    sv_mvd_encoding = Cvar_Get( "sv_mvd_encoding", "1", 0 );
#endif
    sv_mvd_capture_flags = Cvar_Get( "sv_mvd_capture_flags", "5", 0 );

    dummy_buffer.text = dummy_buffer_text;
    dummy_buffer.maxsize = sizeof( dummy_buffer_text );
    dummy_buffer.exec = dummy_exec_string;

    Cmd_Register( c_svmvd );
}

