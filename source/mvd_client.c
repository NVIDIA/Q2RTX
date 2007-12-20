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
// mvd_client.c
//

#include "sv_local.h"
#include "mvd_local.h"
#include <setjmp.h>

list_t		mvd_channels;
list_t		mvd_ready;
mvd_t       mvd_waitingRoom;
qboolean    mvd_dirty;
int         mvd_chanid;

jmp_buf     mvd_jmpbuf;

cvar_t	*mvd_running;
cvar_t	*mvd_shownet;
cvar_t	*mvd_debug;
cvar_t	*mvd_timeout;
cvar_t	*mvd_wait_enter;
cvar_t	*mvd_wait_leave;

// ====================================================================


void MVD_Disconnect( mvd_t *mvd ) {
	if( mvd->demorecording ) {
		MVD_StreamedStop_f();
	}
    if( mvd->stream.state ) {
        NET_Close( &mvd->stream );
    }
	if( mvd->demofile ) {
		FS_FCloseFile( mvd->demofile );
        mvd->demofile = 0;
	}

    mvd->state = MVD_DISCONNECTED;
}

void MVD_Free( mvd_t *mvd ) {
#if USE_ZLIB
    if( mvd->z.state ) {
        inflateEnd( &mvd->z );
    }
    if( mvd->zbuf.data ) {
        Z_Free( mvd->zbuf.data );
    }
#endif
    Z_Free( mvd->players );

    List_Remove( &mvd->ready );
    List_Remove( &mvd->entry );
    Z_Free( mvd );
}

void MVD_Destroy( mvd_t *mvd ) {
	udpClient_t *u, *unext;
    tcpClient_t *t;
    uint16 length;

    // cause UDP clients to reconnect
    LIST_FOR_EACH_SAFE( udpClient_t, u, unext, &mvd->udpClients, entry ) {
        MVD_SwitchChannel( u, &mvd_waitingRoom );
    }

    // send EOF to TCP clients and kick them
    length = 0;
    LIST_FOR_EACH( tcpClient_t, t, &mvd->tcpClients, mvdEntry ) {
        SV_HttpWrite( t, &length, 2 );
        SV_HttpFinish( t );
        SV_HttpDrop( t, "channel destroyed" );
        NET_Run( &t->stream );
	}

    MVD_Disconnect( mvd );
    MVD_ClearState( mvd );
    MVD_Free( mvd );

    mvd_dirty = qtrue;
}

void MVD_Drop( mvd_t *mvd ) {
    if( mvd->state < MVD_WAITING ) {
        MVD_Destroy( mvd );
    } else {
        MVD_Disconnect( mvd );
    }
}

void MVD_ResetStream( mvd_t *mvd ) {
    mvd->state = MVD_PREPARING;
    List_Delete( &mvd_ready );

    longjmp( mvd_jmpbuf, -1 );
}

void MVD_Destroyf( mvd_t *mvd, const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	Com_Printf( "[%s] %s\n", mvd->name, text );

    MVD_Destroy( mvd );

    longjmp( mvd_jmpbuf, -1 );
}

void MVD_Dropf( mvd_t *mvd, const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	Com_Printf( "[%s] %s\n", mvd->name, text );

    MVD_Drop( mvd );

    longjmp( mvd_jmpbuf, -1 );
}

void MVD_DPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	if( !mvd_debug->integer ) {
        return;
	}

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	Com_Printf( S_COLOR_BLUE "%s", text );
}

static void MVD_HttpPrintf( mvd_t *mvd, const char *fmt, ... ) {
    char buffer[MAX_STRING_CHARS];
	va_list		argptr;
    int     length;

	va_start( argptr, fmt );
	length = Q_vsnprintf( buffer, sizeof( buffer ), fmt, argptr );
	va_end( argptr );

    if( FIFO_Write( &mvd->stream.send, buffer, length ) != length ) {
        MVD_Dropf( mvd, "%s: overflow", __func__ );
    }
}

void MVD_ClearState( mvd_t *mvd ) {
	mvd_cs_t *cs, *nextcs;
    mvd_player_t *player;
    edict_t *ent;
	int i;

    for( i = 0; i < mvd->pool.num_edicts; i++ ) {
        ent = &mvd->edicts[i];
        memset( &ent->s, 0, sizeof( ent->s ) );
        ent->inuse = qfalse;
    }
    mvd->pool.num_edicts = 0;

    for( i = 0; i < mvd->maxclients; i++ ) {
        player = &mvd->players[i];
        if( player->layout ) {
            Z_Free( player->layout );
            player->layout = NULL;
        }
        for( cs = player->configstrings; cs; cs = nextcs ) {
            nextcs = cs->next;
            Z_Free( cs );
        }
        player->configstrings = NULL;
        memset( &player->ps, 0, sizeof( player->ps ) );
        player->inuse = qfalse;
    }

    CM_FreeMap( &mvd->cm );

    memset( mvd->configstrings, 0, sizeof( mvd->configstrings ) );

    mvd->framenum = 0;
}

static void MVD_EmitGamestate( mvd_t *mvd ) {
	char		*string;
	int			i, j;
	entity_state_t	*es;
    player_state_t *ps;
    int         length;
    uint16      *patch;
	int flags, extra, portalbytes;
    byte portalbits[MAX_MAP_AREAS/8];

    patch = SZ_GetSpace( &msg_write, 2 );

	// send the serverdata
	MSG_WriteByte( mvd_serverdata );
	MSG_WriteLong( PROTOCOL_VERSION_MVD );
	MSG_WriteShort( PROTOCOL_VERSION_MVD_MINOR );
	MSG_WriteLong( mvd->servercount );
	MSG_WriteString( mvd->gamedir );
	MSG_WriteShort( mvd->clientNum );

    // send configstrings
	for( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
        string = mvd->configstrings[i];
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
	
    // send base player states
	flags = 0;
	if( sv_mvd_noblend->integer ) {
		flags |= MSG_PS_IGNORE_BLEND;
	}
	if( sv_mvd_nogun->integer ) {
		flags |= MSG_PS_IGNORE_GUNINDEX|MSG_PS_IGNORE_GUNFRAMES;
	}
	for( i = 0; i < mvd->maxclients; i++ ) {
        ps = &mvd->players[i].ps;
        extra = 0;
        if( !PPS_INUSE( ps ) ) {
            extra |= MSG_PS_REMOVE;
        }
		MSG_WriteDeltaPlayerstate_Packet( NULL, ps, i, flags | extra );
	}
	MSG_WriteByte( CLIENTNUM_NONE );

    // send base entity states
	for( i = 1; i < mvd->pool.num_edicts; i++ ) {
        es = &mvd->edicts[i].s;
        flags = 0;
        if( i <= mvd->maxclients ) {
            flags |= MSG_ES_FIRSTPERSON;
        }
        if( ( j = es->number ) == 0 ) {
            flags |= MSG_ES_REMOVE;
        }
        es->number = i;
        MSG_WriteDeltaEntity( NULL, es, flags );
        es->number = j;
	}
	MSG_WriteShort( 0 );

    // TODO: write private layouts/configstrings

    *patch = LittleShort( msg_write.cursize - 2 );
}

void MVD_SendGamestate( tcpClient_t *client ) {
    MVD_EmitGamestate( client->mvd );

	Com_DPrintf( "Sent gamestate to MVD client %s\n",
        NET_AdrToString( &client->stream.address ) );
	client->state = cs_spawned;

    SV_HttpWrite( client, msg_write.data, msg_write.cursize );
    SZ_Clear( &msg_write );
}

void MVD_GetStatus( void ) {
    char buffer[MAX_STRING_CHARS];
    mvd_t *mvd;
    int count;

    SV_HttpPrintf( "HTTP/1.0 200 OK\r\n" );

    if( http_client->method == HTTP_METHOD_HEAD ) {
        SV_HttpPrintf( "\r\n" );
        SV_HttpDrop( http_client, "200 OK" );
        return;
    }

    SV_HttpPrintf(
        "Content-Type: text/html; charset=us-ascii\r\n"
        "\r\n" );

    count = SV_CountClients();
    Q_EscapeMarkup( buffer, sv_hostname->string, sizeof( buffer ) );
    SV_HttpHeader( va( "%s - %d/%d", buffer, count, sv_maxclients->integer ) );
    SV_HttpPrintf( "<h1>Status page of %s</h1>"
        "<p>This server has ", buffer );

    count = List_Count( &mvd_ready );
    if( count ) {
        SV_HttpPrintf( "%d MVD stream%s available. ",
            count, count == 1 ? "" : "s" );
    } else {
        SV_HttpPrintf( "no MVD streams available. " );
    }

    count = List_Count( &mvd_waitingRoom.udpClients ); 
    if( count ) {
        SV_HttpPrintf( "Waiting room has %d client%s.</p>",
            count, count == 1 ? "" : "s" );
    } else {
        SV_HttpPrintf( "Waiting room is empty.</p>" );
    }
    
    if( !LIST_EMPTY( &mvd_ready ) ) {
        SV_HttpPrintf(
            "<table border=\"1\"><tr>"
            "<th>ID</th><th>Name</th><th>Map</th><th>Clients</th></tr>" );

        LIST_FOR_EACH( mvd_t, mvd, &mvd_ready, ready ) {
            SV_HttpPrintf(
                "<tr><td><a href=\"http://%s/mvdstream/%d\">%d</a></td>",
                http_host, mvd->id, mvd->id );

            Q_EscapeMarkup( buffer, mvd->name, sizeof( buffer ) );
            SV_HttpPrintf(
                "<td><a href=\"http://%s/mvdstream/%d\">%s</a></td>",
                http_host, mvd->id, buffer );

            Q_EscapeMarkup( buffer, mvd->mapname, sizeof( buffer ) );
            count = List_Count( &mvd->udpClients );
            SV_HttpPrintf( "<td>%s</td><td>%d</td></tr>", buffer, count );
        }
        SV_HttpPrintf( "</table><br>" );
    }

    SV_HttpPrintf( "<a href=\"quake2://%s\">Join this server</a>", http_host );

    SV_HttpFooter();

    SV_HttpDrop( http_client, "200 OK" );
}

static mvd_t *MVD_SetStream( const char *uri ) {
    mvd_t *mvd;
    int id;

    if( LIST_EMPTY( &mvd_ready ) ) {
        SV_HttpReject( "503 Service Unavailable",
            "No MVD streams are available on this server." );
        return NULL;
    }

    if( *uri == '/' ) {
        uri++;
    }

    if( *uri == 0 ) {
        if( List_Count( &mvd_ready ) == 1 ) {
            return LIST_FIRST( mvd_t, &mvd_ready, ready );
        }
        strcpy( http_header, "Cache-Control: no-cache\r\n" );
        SV_HttpReject( "300 Multiple Choices",
            "Please specify an exact stream ID." );
        return NULL;
    }

    id = atoi( uri );
    LIST_FOR_EACH( mvd_t, mvd, &mvd_ready, ready ) {
        if( mvd->id == id ) {
            return mvd;
        }
    }

    SV_HttpReject( "404 Not Found",
        "Requested MVD stream was not found on this server." );
    return NULL;
}

void MVD_GetStream( const char *uri ) {
    mvd_t *mvd;
    uint32 magic;

    mvd = MVD_SetStream( uri );
    if( !mvd ) {
        return;
    }

    SV_HttpPrintf( "HTTP/1.0 200 OK\r\n" );

    if( http_client->method == HTTP_METHOD_HEAD ) {
        SV_HttpPrintf( "\r\n" );
        SV_HttpDrop( http_client, "200 OK " );
        return;
    }

    List_Append( &mvd->tcpClients, &http_client->mvdEntry );
    http_client->mvd = mvd;

    SV_HttpPrintf(
#if USE_ZLIB
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

    MVD_SendGamestate( http_client );
}


void MVD_ChangeLevel( mvd_t *mvd ) {
	udpClient_t *u;

    if( sv.state != ss_broadcast ) {
        MVD_Spawn_f(); // the game is just starting
        return;
    }

    // cause all UDP clients to reconnect
	MSG_WriteByte( svc_stufftext );
	MSG_WriteString( "changing; reconnect\n" );

    LIST_FOR_EACH( udpClient_t, u, &mvd->udpClients, entry ) {
        SV_ClientReset( u->cl );
		SV_ClientAddMessage( u->cl, MSG_RELIABLE );
	}

	SZ_Clear( &msg_write );

    SV_SendAsyncPackets();
}

static void MVD_PlayNext( mvd_t *mvd, demoentry_t *entry ) {
    uint32 magic = 0;

    if( !entry ) {
        if( mvd->demoloop ) {
            if( --mvd->demoloop == 0 ) {
                MVD_Dropf( mvd, "End of play list reached" );
                return;
            }
        }
        entry = mvd->demohead;
    }

    if( mvd->demofile ) {
        FS_FCloseFile( mvd->demofile );
        mvd->demofile = 0;
    }

    FS_FOpenFile( entry->path, &mvd->demofile, FS_MODE_READ );
    if( !mvd->demofile ) {
        MVD_Dropf( mvd, "Couldn't reopen %s", entry->path );
    }

    FS_Read( &magic, 4, mvd->demofile );
    if( magic != MVD_MAGIC ) {
        MVD_Dropf( mvd, "%s is not a MVD2 file", entry->path );
    }

    Com_Printf( "[%s] Reading from %s\n", mvd->name, entry->path );

    // reset state
    mvd->demoentry = entry;

    // set channel address
    Q_strncpyz( mvd->address, COM_SkipPath( entry->path ), sizeof( mvd->address ) );
}


static void MVD_ReadDemo( mvd_t *mvd ) {
    byte *data;
    int length, read, total = 0;

    do {
        data = FIFO_Reserve( &mvd->stream.recv, &length );
        if( !length ) {
            return;
        }
        read = FS_Read( data, length, mvd->demofile );
        FIFO_Commit( &mvd->stream.recv, read );
        total += read;
    } while( read );

    if( !total ) {
//        MVD_Dropf( mvd, "End of MVD file reached" );
        MVD_PlayNext( mvd, mvd->demoentry->next );
    }
}

static qboolean MVD_ParseResponse( mvd_t *mvd ) {
    char key[MAX_TOKEN_CHARS];
    char *p, *token;
    const char *line;
    byte *b, *data;
    int length;

    while( 1 ) {
        data = FIFO_Peek( &mvd->stream.recv, &length );
        if( !length ) {
            break;
        }
        if( ( b = memchr( data, '\n', length ) ) != NULL ) {
            length = b - data + 1;
        }
        if( mvd->responseLength + length > MAX_NET_STRING - 1 ) {
            MVD_Dropf( mvd, "Response line exceeded maximum length" );
        }

        memcpy( mvd->response + mvd->responseLength, data, length );
        mvd->responseLength += length;
        mvd->response[mvd->responseLength] = 0;

        FIFO_Decommit( &mvd->stream.recv, length );

        if( !b ) {
            continue;
        }

        line = mvd->response;
        mvd->responseLength = 0;
        
        if( !mvd->statusCode ) {
            // parse version
            token = COM_SimpleParse( &line );
            if( !token[0] ) {
                continue; // empty line?
            }
            if( strncmp( token, "HTTP/", 5 ) ) {
                MVD_Dropf( mvd, "Malformed HTTP version" );
            }

            // parse status code
            token = COM_SimpleParse( &line );
            mvd->statusCode = atoi( token );
            if( !mvd->statusCode ) {
                MVD_Dropf( mvd, "Malformed HTTP status code" );
            }

            // parse reason phrase
            if( line ) {
                while( *line && *line <= ' ' ) {
                    line++;
                }
                Q_ClearStr( mvd->statusText, line, MAX_QPATH );
            }
        } else {
            // parse header fields
            token = COM_SimpleParse( &line );
            if( !token[0] ) {
                return qtrue; // end of header
            }
            strcpy( key, token );
            p = strchr( key, ':' );
            if( !p ) {
                MVD_Dropf( mvd, "Malformed HTTP header field" );
            }
            *p = 0;
            Q_strlwr( key );

            token = COM_SimpleParse( &line );
            if( !strcmp( key, "content-type" ) ) {
            } else if( !strcmp( key, "content-encoding" ) ) {
#if USE_ZLIB
                if( !Q_stricmp( token, "deflate" ) ||
                    !Q_stricmp( token, "gzip" ) ||
                    !Q_stricmp( token, "x-gzip" ) )
                {
                    if( inflateInit2( &mvd->z, 47 ) != Z_OK ) {
                        MVD_Dropf( mvd, "inflateInit2() failed: %s", mvd->z.msg );
                    }
                    mvd->zbuf.data = MVD_Malloc( MAX_MSGLEN * 2 );
                    mvd->zbuf.size = MAX_MSGLEN * 2;
                } else
#endif
                {
                    MVD_Dropf( mvd, "Unsupported content encoding: %s", token );
                }
            } else if( !strcmp( key, "content-length" ) ) {
                mvd->contentLength = atoi( token );
            } else if( !strcmp( key, "transfer-encoding" ) ) {
            } else if( !strcmp( key, "location" ) ) {
            }
        }
    }

    return qfalse;
}

static inline float MVD_BufferPercent( mvd_t *mvd ) {
    int usage = FIFO_Usage( &mvd->stream.recv );
    int size = mvd->stream.recv.size;

#ifdef USE_ZLIB
    usage += FIFO_Usage( &mvd->zbuf );
    size += mvd->zbuf.size;
#endif

    if( !size ) {
        return 0;
    }
    return usage * 100.0f / size;
}

void MVD_Frame( void ) {
    mvd_t *mvd, *next;
    neterr_t ret;
    float usage;

    LIST_FOR_EACH_SAFE( mvd_t, mvd, next, &mvd_channels, entry ) {
        if( mvd->state <= MVD_DEAD || mvd->state >= MVD_DISCONNECTED ) {
            continue;
        }

        if( setjmp( mvd_jmpbuf ) ) {
            continue;
        }

        if( mvd->demoplayback ) {
            if( mvd->demofile ) {
                MVD_ReadDemo( mvd );
            }
            if( mvd->state == MVD_PREPARING ) {
                MVD_Parse( mvd );
            }
            continue;
        }

        // process network stream
        ret = NET_Run( &mvd->stream );
        switch( ret ) {
        case NET_AGAIN:
            // check timeout
            if( mvd->lastReceived > svs.realtime ) {
                mvd->lastReceived = svs.realtime;
            }
            if( svs.realtime - mvd->lastReceived > mvd_timeout->value * 1000 ) {
                MVD_Dropf( mvd, "Connection timed out" );
            }
            continue;
        case NET_ERROR:
            MVD_Dropf( mvd, "%s to %s", NET_ErrorString(),
                NET_AdrToString( &mvd->stream.address ) );
        case NET_CLOSED:
            MVD_Dropf( mvd, "Connection closed" );
        case NET_OK:
            break;
        }

        // don't timeout
        mvd->lastReceived = svs.realtime;

        // run MVD state machine
        switch( mvd->state ) {
        case MVD_CONNECTING:
            Com_Printf( "[%s] Connected, awaiting response...\n", mvd->name );
            mvd->state = MVD_CONNECTED;
            // fall through
        case MVD_CONNECTED:
            if( !MVD_ParseResponse( mvd ) ) {
                continue;
            }
            if( mvd->statusCode != 200 ) {
                MVD_Dropf( mvd, "HTTP request failed: %d %s",
                    mvd->statusCode, mvd->statusText );
            }
            Com_Printf( "[%s] Got response, awaiting gamestate...\n", mvd->name );
            mvd->state = MVD_CHECKING;
            // fall through
        case MVD_CHECKING:
        case MVD_PREPARING:
            MVD_Parse( mvd );
            if( mvd->state > MVD_PREPARING ) {
            // fall through
        default:
                usage = MVD_BufferPercent( mvd );
                if( mvd->state == MVD_WAITING ) {
                    if( usage >= mvd_wait_leave->value ) {
                        Com_Printf( "[%s] Reading data...\n", mvd->name );
                        mvd->state = MVD_READING;
                    }
                } else {
                    if( mvd_wait_leave->value > mvd_wait_enter->value &&
                        usage < mvd_wait_enter->value )
                    {
                        Com_Printf( "[%s] Buffering data...\n", mvd->name );
                        mvd->state = MVD_WAITING;
                    }
                }
            }
        }
    }
}


/*
====================================================================

OPERATOR COMMANDS

====================================================================
*/

mvd_t *MVD_SetChannel( int arg ) {
    char *s = Cmd_Argv( arg );
    mvd_t *mvd;
    int id;

    if( LIST_EMPTY( &mvd_channels ) ) {
        Com_Printf( "No active channels.\n" );
        return NULL;
    }

    if( !*s ) {
        if( List_Count( &mvd_channels ) == 1 ) {
            return LIST_FIRST( mvd_t, &mvd_channels, entry );
        }
        Com_Printf( "Please specify an exact channel ID.\n" );
        return NULL;
    }

    id = atoi( s );
    LIST_FOR_EACH( mvd_t, mvd, &mvd_channels, entry ) {
        if( mvd->id == id ) {
            return mvd;
        }
    }

    Com_Printf( "No such channel ID: %s\n", s );
    return NULL;
}

void MVD_Spawn_f( void ) {
    SV_InitGame( qtrue );

    // set serverinfo variables
    Cvar_Set( "mapname", "nomap" );
    Cvar_SetInteger( "sv_running", ss_broadcast );
    Cvar_SetInteger( "sv_paused", 0 );
    Cvar_SetInteger( "timedemo", 0 );

	sv.spawncount = ( rand() | ( rand() << 16 ) ) ^ Sys_Realtime();
	sv.spawncount &= 0x7FFFFFFF;

    sv.state = ss_broadcast;
}

void MVD_ListChannels_f( void ) {
    mvd_t *mvd;
    int usage;

    if( LIST_EMPTY( &mvd_channels ) ) {
        Com_Printf( "No active channels.\n" );
        return;
    }

	Com_Printf( "id name             map      stat buf address       \n"
	            "-- ---------------- -------- ---- --- --------------\n" );

    LIST_FOR_EACH( mvd_t, mvd, &mvd_channels, entry ) {
        Com_Printf( "%2d %-16.16s %-8.8s ", mvd->id,
            mvd->name, mvd->mapname );
        switch( mvd->state ) {
        case MVD_DEAD:
            Com_Printf( "DEAD" );
            break;
	    case MVD_CONNECTING:
	    case MVD_CONNECTED:
            Com_Printf( "CNCT" );
            break;
	    case MVD_CHECKING:
	    case MVD_PREPARING:
            Com_Printf( "PREP" );
            break;
        case MVD_WAITING:
            Com_Printf( "WAIT" );
            break;
        case MVD_READING:
            Com_Printf( "READ" );
            break;
        case MVD_DISCONNECTED:
            Com_Printf( "DISC" );
            break;
        }
        usage = MVD_BufferPercent( mvd );
        Com_Printf( " %3d %s\n", usage, mvd->address );
    }
}

void MVD_StreamedStop_f( void ) {
    mvd_t *mvd;
	uint16 msglen;

    mvd = MVD_SetChannel( 1 );
    if( !mvd ) {
        Com_Printf( "Usage: %s [chanid]\n", Cmd_Argv( 0 ) );
        return;
    }

	if( !mvd->demorecording ) {
		Com_Printf( "[%s] Not recording a demo.\n", mvd->name );
		return;
	}

	msglen = 0;
	FS_Write( &msglen, 2, mvd->demofile );
	FS_FCloseFile( mvd->demofile );

	mvd->demofile = 0;
	mvd->demorecording = qfalse;

	Com_Printf( "[%s] Stopped recording.\n", mvd->name );
}

void MVD_StreamedRecord_f( void ) {
	char buffer[MAX_OSPATH];
	char *name;
	fileHandle_t f;
    mvd_t *mvd;
    uint32 magic;
    
	if( Cmd_Argc() < 2 || ( mvd = MVD_SetChannel( 2 ) ) == NULL ) {
		Com_Printf( "Usage: %s [/]<filename> [chanid]\n", Cmd_Argv( 0 ) );
		return;
	}

	if( mvd->demorecording ) {
		Com_Printf( "[%s] Already recording.\n", mvd->name );
		return;
	}

	if( mvd->state < MVD_WAITING ) {
		Com_Printf( "[%s] Channel not ready.\n", mvd->name );
		return;
	}

	//
	// open the demo file
	//
	name = Cmd_Argv( 1 );
	if( name[0] == '/' ) {
		Q_strncpyz( buffer, name + 1, sizeof( buffer ) );
	} else {
		Q_concat( buffer, sizeof( buffer ), "demos/", name, NULL );
    	COM_AppendExtension( buffer, ".mvd2", sizeof( buffer ) );
	}

	FS_FOpenFile( buffer, &f, FS_MODE_WRITE );
	if( !f ) {
		Com_EPrintf( "Couldn't open %s for writing\n", buffer );
		return;
	}
	
	Com_Printf( "[%s] Recording into %s.\n", mvd->name, buffer );

	mvd->demofile = f;
	mvd->demorecording = qtrue;

    MVD_EmitGamestate( mvd );

    magic = MVD_MAGIC;
    FS_Write( &magic, 4, f );
	FS_Write( msg_write.data, msg_write.cursize, f );

    SZ_Clear( &msg_write );
}


/*
==============
MVD_Connect_f

[http://]host[:port][/resource]
==============
*/
void MVD_Connect_f( void ) {
    static const cmd_option_t options[] = {
        { "h", "help", "display this message" },
        { "i:number", "id", "specify remote stream ID as <number>" },
        { "n:string", "name", "specify channel name as <string>" },
        { NULL }
    };
	netadr_t adr;
    netstream_t stream;
    char buffer[MAX_STRING_CHARS];
    char resource[MAX_STRING_CHARS];
    char credentials[MAX_STRING_CHARS];
	char *id = "", *name = NULL, *host, *p;
    mvd_t *mvd;
    uint16 port;
    int c;

    while( ( c = Cmd_ParseOptions( options ) ) != -1 ) {
        switch( c ) {
        case 'h':
            Cmd_PrintUsage( options, "<uri>" );
            Com_Printf( "Create new MVD channel and connect to URI.\n" );
            Cmd_PrintHelp( options );
            Com_Printf(
    "Full URI syntax: [http://][user:pass@]<host>[:port][/resource]\n"
    "If resource is given, default port is 80 and stream ID is ignored.\n"
    "Otherwise, default port is %d and stream ID is undefined.\n", PORT_SERVER );
            return;
        case 'i':
            id = cmd_optarg;
            break;
        case 'n':
            name = cmd_optarg;
            break;
        default:
            return;
        }
    }

    if( !cmd_optarg[0] ) {
        Com_Printf( "Missing URI argument.\n" );
        Cmd_PrintHint();
        return;
    }

	Cmd_ArgvBuffer( cmd_optind, buffer, sizeof( buffer ) );

    // skip optional http:// prefix
    host = buffer;
    if( !strncmp( host, "http://", 7 ) ) {
        host += 7;
    }

    // parse credentials
    p = strchr( host, '@' );
    if( p ) {
        *p = 0;
        strcpy( credentials, host );
        host = p + 1;
    } else {
        credentials[0] = 0;
    }

    // parse resource
    p = strchr( host, '/' );
    if( p ) {
        *p = 0;
        strcpy( resource, p + 1 );
        port = BigShort( 80 );
    } else {
        Q_concat( resource, sizeof( resource ), "mvdstream/", id, NULL );
        port = BigShort( PORT_SERVER );
    }

    // resolve hostname
	if( !NET_StringToAdr( host, &adr ) ) {
		Com_Printf( "Bad server address: %s\n", host );
		return;
	}
    if( !adr.port ) {
        adr.port = port;
    }

    if( NET_Connect( &adr, &stream ) == NET_ERROR ) {
        Com_Printf( "%s to %s\n", NET_ErrorString(),
            NET_AdrToString( &adr ) );
        return;
    }

    Z_TagReserve( sizeof( *mvd ) + MAX_MSGLEN * 2 + 256, TAG_MVD );

    mvd = Z_ReservedAllocz( sizeof( *mvd ) );
    mvd->id = mvd_chanid++;
    mvd->state = MVD_CONNECTING;
    mvd->stream = stream;
    mvd->stream.recv.data = Z_ReservedAlloc( MAX_MSGLEN * 2 );
    mvd->stream.recv.size = MAX_MSGLEN * 2;
    mvd->stream.send.data = Z_ReservedAlloc( 256 );
    mvd->stream.send.size = 256;
    mvd->pool.edicts = mvd->edicts;
    mvd->pool.edict_size = sizeof( edict_t );
    mvd->pool.max_edicts = MAX_EDICTS;
    mvd->pm_type = PM_SPECTATOR;
    mvd->lastReceived = svs.realtime;
    List_Init( &mvd->udpClients );
    List_Init( &mvd->tcpClients );
    List_Init( &mvd->ready );
    List_Append( &mvd_channels, &mvd->entry );

    // set channel name
    if( name ) {
        Q_strncpyz( mvd->name, name, sizeof( mvd->name ) );
    } else {
        Com_sprintf( mvd->name, sizeof( mvd->name ), "net%d", mvd->id );
    }

    Q_strncpyz( mvd->address, host, sizeof( mvd->address ) );

    Com_Printf( "[%s] Connecting to %s...\n", mvd->name, NET_AdrToString( &adr ) );

    MVD_HttpPrintf( mvd,
        "GET /%s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: " APPLICATION "/" VERSION "\r\n"
#ifdef USE_ZLIB
        "Accept-Encoding: gzip, deflate\r\n"
#endif
        "Accept: application/*\r\n",
        resource, host );
    if( credentials[0] ) {
        Q_Encode64( buffer, credentials, sizeof( buffer ) );
        MVD_HttpPrintf( mvd, "Authorization: Basic %s\r\n", buffer );
    }
    MVD_HttpPrintf( mvd, "\r\n" );
}

static void MVD_Disconnect_f( void ) {
    mvd_t *mvd;

    mvd = MVD_SetChannel( 1 );
    if( !mvd ) {
        return;
    }

    if( mvd->state == MVD_DISCONNECTED ) {
        Com_Printf( "[%s] Already disconnected.\n", mvd->name );
        return;
    }

    Com_Printf( "[%s] Channel was disconnected.\n", mvd->name );
    MVD_Drop( mvd );
}

static void MVD_Kill_f( void ) {
    mvd_t *mvd;

    mvd = MVD_SetChannel( 1 );
    if( !mvd ) {
        return;
    }

    Com_Printf( "[%s] Channel was killed.\n", mvd->name );
    MVD_Destroy( mvd );
}

static void MVD_Control_f( void ) {
    static const cmd_option_t options[] = {
        { "h", "help", "display this message" },
        { "l:number", "loop", "replay <number> of times (0 means forever)" },
        { "n:string", "name", "specify channel name as <string>" },
        { NULL }
    };
    mvd_t *mvd;
    char *name = NULL;
    int loop = -1;
    int todo = 0;
    int c;

    while( ( c = Cmd_ParseOptions( options ) ) != -1 ) {
        switch( c ) {
        case 'h':
            Cmd_PrintUsage( options, "[chanid]" );
            Com_Printf( "Change attributes of existing MVD channel.\n" );
            Cmd_PrintHelp( options );
            return;
        case 'l':
            loop = atoi( cmd_optarg );
            if( loop < 0 ) {
                Com_Printf( "Invalid value for %s option.\n", cmd_optopt );
                Cmd_PrintHint();
                return;
            }
            todo |= 1;
            break;
        case 'n':
            name = cmd_optarg;
            todo |= 2;
            break;
        default:
            return;
        }
    }

    if( !todo ) {
        Com_Printf( "At least one option needed.\n" );
        Cmd_PrintHint();
        return;
    }

    mvd = MVD_SetChannel( cmd_optind );
    if( !mvd ) {
        Cmd_PrintHint();
        return;
    }

    if( name ) {
        Com_Printf( "[%s] Channel renamed to %s.\n", mvd->name, name );
        Q_strncpyz( mvd->name, name, sizeof( mvd->name ) );
    }
    if( loop != -1 ) {
        Com_Printf( "[%s] Loop count changed to %d.\n", mvd->name, loop );
        mvd->demoloop = loop;
    }
}

const char *MVD_Play_g( const char *partial, int state ) {
	return Com_FileNameGeneratorByFilter( "demos", "*.mvd2;*.mvd2.gz",
        partial, qfalse, state );
}

void MVD_Play_f( void ) {
    static const cmd_option_t options[] = {
        { "h", "help", "display this message" },
        { "l:number", "loop", "replay <number> of times (0 means forever)" },
        { "n:string", "name", "specify channel name as <string>" },
        { NULL }
    };
	char *name = NULL, *s;
	char buffer[MAX_OSPATH];
    int loop = 1, len;
    mvd_t *mvd;
    int c, argc;
    demoentry_t *entry, *head;
    int i;

    while( ( c = Cmd_ParseOptions( options ) ) != -1 ) {
        switch( c ) {
        case 'h':
            Cmd_PrintUsage( options, "[/]<filename>" );
            Com_Printf( "Create new MVD channel and begin demo playback.\n" );
            Cmd_PrintHelp( options );
            Com_Printf( "Final path is formatted as demos/<filename>.mvd2.\n"
                "Prepend slash to specify raw path.\n" );
            return;
        case 'l':
            loop = atoi( cmd_optarg );
            if( loop < 0 ) {
                Com_Printf( "Invalid value for %s option.\n", cmd_optopt );
                Cmd_PrintHint();
                return;
            }
            break;
        case 'n':
            name = cmd_optarg;
            break;
        default:
            return;
        }
    }

    argc = Cmd_Argc();
    if( cmd_optind == argc ) {
        Com_Printf( "Missing filename argument.\n" );
        Cmd_PrintHint();
        return;
    }

    head = NULL;
    for( i = argc - 1; i >= cmd_optind; i-- ) {
	    s = Cmd_Argv( i );
        if( *s == '/' ) {
            Q_strncpyz( buffer, s + 1, sizeof( buffer ) );
        } else {
            Q_concat( buffer, sizeof( buffer ), "demos/", s, NULL );
            COM_AppendExtension( buffer, ".mvd2", sizeof( buffer ) );
        }
        if( FS_LoadFile( buffer, NULL ) == -1 ) {
            Com_Printf( "Ignoring non-existent entry: %s\n", buffer );
            continue;
        }

        len = strlen( buffer ) + 1;
        entry = Z_Malloc( sizeof( *entry ) + len );
        memcpy( entry->path, buffer, len );
        entry->next = head;
        head = entry;
    }

    if( !head ) {
        return;
    }

    Z_TagReserve( sizeof( *mvd ) + MAX_MSGLEN * 2, TAG_MVD );

    mvd = Z_ReservedAllocz( sizeof( *mvd ) );
    mvd->id = mvd_chanid++;
    mvd->state = MVD_PREPARING;
    mvd->demoplayback = qtrue;
    mvd->demohead = head;
    mvd->demoloop = loop;
    mvd->stream.recv.data = Z_ReservedAlloc( MAX_MSGLEN * 2 );
    mvd->stream.recv.size = MAX_MSGLEN * 2;
    mvd->pool.edicts = mvd->edicts;
    mvd->pool.edict_size = sizeof( edict_t );
    mvd->pool.max_edicts = MAX_EDICTS;
    mvd->pm_type = PM_SPECTATOR;
    List_Init( &mvd->udpClients );
    List_Init( &mvd->tcpClients );
    List_Init( &mvd->ready );
    List_Append( &mvd_channels, &mvd->entry );

    // set channel name
    if( name ) {
        Q_strncpyz( mvd->name, name, sizeof( mvd->name ) );
    } else {
        Com_sprintf( mvd->name, sizeof( mvd->name ), "dem%d", mvd->id );
    }

    MVD_PlayNext( mvd, mvd->demohead );
}


void MVD_Shutdown( void ) {
    mvd_t *mvd, *next;

    LIST_FOR_EACH_SAFE( mvd_t, mvd, next, &mvd_channels, entry ) {
        MVD_Disconnect( mvd );
        MVD_ClearState( mvd );
        MVD_Free( mvd );
    }

    List_Init( &mvd_channels );
    List_Init( &mvd_ready );

	if( mvd_clients ) {
		Z_Free( mvd_clients );
        mvd_clients = NULL;
	}

    mvd_chanid = 0;

    Z_LeakTest( TAG_MVD );
}

static const cmdreg_t c_mvd[] = {
	{ "mvdplay", MVD_Play_f, MVD_Play_g },
	{ "mvdconnect", MVD_Connect_f },
	{ "mvdisconnect", MVD_Disconnect_f },
	{ "mvdkill", MVD_Kill_f },
	{ "mvdspawn", MVD_Spawn_f },
	{ "mvdchannels", MVD_ListChannels_f },
	{ "mvdcontrol", MVD_Control_f },

    { NULL }
};


/*
==============
MVD_Register
==============
*/
void MVD_Register( void ) {
	mvd_shownet = Cvar_Get( "mvd_shownet", "0", 0 );
	mvd_debug = Cvar_Get( "mvd_debug", "0", 0 );
	mvd_timeout = Cvar_Get( "mvd_timeout", "120", 0 );
	mvd_wait_enter = Cvar_Get( "mvd_wait_enter", "0.5", 0 );
	mvd_wait_leave = Cvar_Get( "mvd_wait_leave", "2", 0 );

    Cmd_Register( c_mvd );

    List_Init( &mvd_channels );
    List_Init( &mvd_ready );
}

