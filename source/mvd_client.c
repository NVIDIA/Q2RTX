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

jmp_buf     mvd_jmpbuf;

cvar_t  *mvd_pause;
cvar_t	*mvd_running;
cvar_t	*mvd_shownet;
cvar_t	*mvd_debug;
cvar_t	*mvd_nextserver;
cvar_t	*mvd_timeout;
cvar_t	*mvd_autoscores;
cvar_t	*mvd_safecmd;
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
    int i;

    for( i = 0; i < SV_BASELINES_CHUNKS; i++ ) {
        if( mvd->baselines[i] ) {
            Z_Free( mvd->baselines[i] );
        }
    }

#if USE_ZLIB
    if( mvd->z.state ) {
        inflateEnd( &mvd->z );
    }
    if( mvd->zbuf.data ) {
        Z_Free( mvd->zbuf.data );
    }
#endif

    List_Remove( &mvd->ready );
    List_Remove( &mvd->entry );
    Z_Free( mvd );
}


void MVD_Destroy( mvd_t *mvd, const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];
	udpClient_t *u, *unext;
    tcpClient_t *t;
    uint16 length;

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	Com_Printf( S_COLOR_YELLOW "%s\n", text );

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

    longjmp( mvd_jmpbuf, -1 );
}

void MVD_Drop( mvd_t *mvd, const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	Com_Printf( S_COLOR_YELLOW "%s\n", text );

    if( mvd->state < MVD_WAITING ) {
        MVD_Disconnect( mvd );
        MVD_ClearState( mvd );
        MVD_Free( mvd );
    } else {
        MVD_Disconnect( mvd );
    }

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

void MVD_HttpPrintf( mvd_t *mvd, const char *fmt, ... ) {
    char buffer[MAX_STRING_CHARS];
	va_list		argptr;
    int     length;

	va_start( argptr, fmt );
	length = Q_vsnprintf( buffer, sizeof( buffer ), fmt, argptr );
	va_end( argptr );

    if( !FIFO_Write( &mvd->stream.send, buffer, length ) ) {
        MVD_Drop( mvd, "overflowed" );
    }
}

void MVD_ClearState( mvd_t *mvd ) {
	mvdConfigstring_t *cs, *nextcs;
    mvdPlayer_t *player;
	int i;

    for( i = 0; i < MAX_CLIENTS; i++ ) {
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
    }

    for( i = 0; i < SV_BASELINES_CHUNKS; i++ ) {
        if( mvd->baselines[i] ) {
            memset( mvd->baselines[i], 0, sizeof( entityStateEx_t ) *
                SV_BASELINES_PER_CHUNK );
        }
    }

    CM_FreeMap( &mvd->cm );

    memset( mvd->configstrings, 0, sizeof( mvd->configstrings ) );

    mvd->framenum = 0;
    memset( mvd->frames, 0, sizeof( mvd->frames ) );
}

static void MVD_EmitGamestate( mvd_t *mvd ) {
	char		*string;
	int			i, j;
	entityStateEx_t	*base, *es;
    player_state_t *ps;
    int         length;
    uint16      *patch;
    mvdFrame_t  *frame;
	int         flags;

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

    // send baselines
    for( i = 0; i < SV_BASELINES_CHUNKS; i++ ) {
		base = mvd->baselines[i];
		if( !base ) {
			continue;
		}
    	for( j = 0; j < SV_BASELINES_PER_CHUNK; j++ ) {
            if( base->s.number ) {
                MSG_WriteDeltaEntity( NULL, &base->s, MSG_ES_FORCE );
            }
            base++;
        }
	}
    MSG_WriteShort( 0 );

    // send uncompressed frame
    frame = &mvd->frames[mvd->framenum & MVD_UPDATE_MASK];
    MSG_WriteByte( mvd_frame_nodelta );
    MSG_WriteLong( frame->serverFrame );
    MSG_WriteByte( 0 );

	flags = MSG_PS_FORCE;
	if( sv_mvd_noblend->integer ) {
		flags |= MSG_PS_IGNORE_BLEND;
	}
	if( sv_mvd_nogun->integer ) {
		flags |= MSG_PS_IGNORE_GUNINDEX|MSG_PS_IGNORE_GUNFRAMES;
	}
	for( i = 0; i < frame->numPlayers; i++ ) {
		j = ( frame->firstPlayer + i ) & MVD_PLAYERS_MASK;
		ps = &mvd->playerStates[j];

		MSG_WriteDeltaPlayerstate_Packet( NULL, ps, flags );
    }
	MSG_WriteByte( CLIENTNUM_NONE );

	for( i = 0; i < frame->numEntities; i++ ) {
		j = ( frame->firstEntity + i ) & MVD_ENTITIES_MASK;
		es = &mvd->entityStates[j];

        flags = MSG_ES_FORCE|MSG_ES_NEWENTITY;
        if( es->s.number <= mvd->maxclients ) {
            flags |= MSG_ES_FIRSTPERSON;
        }
        base = mvd->baselines[es->s.number >> SV_BASELINES_SHIFT];
        if( base ) {
            base += es->s.number & SV_BASELINES_MASK;
        }
        MSG_WriteDeltaEntity( &base->s, &es->s, flags );
    }
    MSG_WriteShort( 0 );

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

void MVD_RejectStream( const char *error, const char *reason ) {
    char buffer[MAX_STRING_CHARS];
    mvd_t *mvd;
    int number, count;

    SV_HttpPrintf(
        "HTTP/1.0 %s\r\n"
        "Content-Type: text/html\r\n"
        "Cache-Control: no-cache\r\n"
        "\r\n", error );

    SV_HttpHeader( error );
    SV_HttpPrintf(
        "<h1>%s</h1><p>%s</p><table border=\"1\"><tr>"
        "<th>Index</th><th>Name</th><th>Map</th><th>Clients</th></tr>",
        error, reason );

    number = 0;
    LIST_FOR_EACH( mvd_t, mvd, &mvd_ready, entry ) {
        SV_HttpPrintf(
            "<tr><td><a href=\"http://%s/mvdstream/%d\">%d</a></td>",
            http_host, number, number );

        Q_EscapeMarkup( buffer, mvd->name, sizeof( buffer ) );
        SV_HttpPrintf( "<td>%s</td>", buffer );

        Q_EscapeMarkup( buffer, mvd->mapname, sizeof( buffer ) );
        count = List_Count( &mvd->udpClients );
        SV_HttpPrintf( "<td>%s</td><td>%d</td></tr>", buffer, count );

        number++;
    }
    SV_HttpPrintf( "</table>" );

    SV_HttpFooter();

    SV_HttpDrop( http_client, error );
}

void MVD_GetStream( const char *uri ) {
    mvd_t *mvd;
    int index;
    uint32 magic;

    if( *uri == '/' ) {
        uri++;
    }

    if( !*uri ) {
        MVD_RejectStream( "300 Multiple Choices",
            "There are several MVD channels available. "
            "Please select an appropriate stream." );
        return;
    }

    index = atoi( uri );

    mvd = LIST_INDEX( mvd_t, index, &mvd_ready, ready );
    if( !mvd ) {
        MVD_RejectStream( "404 Not Found",
            "Requested MVD stream was not found on this server." );
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
        MVD_Spawn_f();
        return;
    }

    // cause all UDP clients to reconnect
	MSG_WriteByte( svc_stufftext );
	MSG_WriteString( "changing; reconnect\n" );

    LIST_FOR_EACH( udpClient_t, u, &mvd->udpClients, entry ) {
        SV_ClientReset( u->cl );
		SV_ClientAddMessage( u->cl, MSG_RELIABLE );
	}

    SV_SendAsyncPackets();

	SZ_Clear( &msg_write );
}

#ifndef DEDICATED_ONLY
/* called by the client code */
qboolean MVD_GetDemoPercent( int *percent, int *bufferPercent ) {
#if 0
	int delta;

	if( !mvd.demoplayback ) {
		return qfalse;
	}
	
	delta = mvd.serverPacketNum - mvd.timelines[0].framenum;
	*bufferPercent = 100 - delta * 100 / ( mvd.frameBackup - 1 );
	*percent = mvd.demofilePercent;

	return qtrue;
#else
	return qfalse;
#endif
}
#endif

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
        MVD_Drop( mvd, "End of MVD file reached" );
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
            MVD_Drop( mvd, "Response line exceeded maximum length" );
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
                MVD_Drop( mvd, "Malformed HTTP version" );
            }

            // parse status code
            token = COM_SimpleParse( &line );
            mvd->statusCode = atoi( token );
            if( !mvd->statusCode ) {
                MVD_Drop( mvd, "Malformed HTTP status code" );
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
                MVD_Drop( mvd, "Malformed HTTP header field" );
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
                        MVD_Drop( mvd, "inflateInit2() failed: %s", mvd->z.msg );
                    }
                    mvd->zbuf.data = MVD_Malloc( MAX_MSGLEN * 2 );
                    mvd->zbuf.size = MAX_MSGLEN * 2;
                } else
#endif
                {
                    MVD_Drop( mvd, "Unsupported content encoding: %s", token );
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
            MVD_ReadDemo( mvd );
            if( mvd->state == MVD_PREPARING ) {
                MVD_Parse( mvd );
            }
            continue;
        }

        // process network stream
        ret = NET_Run( &mvd->stream );
        switch( ret ) {
        case NET_AGAIN:
            continue;
        case NET_ERROR:
            MVD_Drop( mvd, "%s to %s", NET_ErrorString(),
                NET_AdrToString( &mvd->stream.address ) );
        case NET_CLOSED:
            MVD_Drop( mvd, "Connection closed" );
        case NET_OK:
            break;
        }

        // run MVD state machine
        switch( mvd->state ) {
        case MVD_CONNECTING:
            Com_Printf( "Connected, awaiting response...\n" );
            mvd->state = MVD_CONNECTED;
            // fall through
        case MVD_CONNECTED:
            if( !MVD_ParseResponse( mvd ) ) {
                continue;
            }
            if( mvd->statusCode != 200 ) {
                MVD_Drop( mvd, "HTTP request failed: %d %s",
                    mvd->statusCode, mvd->statusText );
            }
            Com_Printf( "Got response, awaiting gamestate...\n" );
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
                        Com_Printf( "Reading data...\n" );
                        mvd->state = MVD_READING;
                    }
                } else {
                    if( mvd_wait_leave->value > mvd_wait_enter->value &&
                        usage < mvd_wait_enter->value )
                    {
                        Com_Printf( "Buffering data...\n" );
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

    if( !*s ) {
        if( List_Count( &mvd_channels ) == 1 ) {
            return LIST_FIRST( mvd_t, &mvd_channels, entry );
        }
        Com_Printf( "Please specify a channel\n" );
        return NULL;
    }

    mvd = LIST_INDEX( mvd_t, atoi( s ), &mvd_channels, entry );
    if( mvd ) {
        return mvd;
    }

    Com_Printf( "No such channel: %s\n", s );
    return NULL;
}

void MVD_Spawn_f( void ) {
    SV_InitGame( qtrue );

    /* set serverinfo variables */
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
    int number, usage;

    if( LIST_EMPTY( &mvd_channels ) ) {
        Com_Printf( "No active channels.\n" );
        return;
    }

	Com_Printf( "num name             map      state buf address       \n"
	            "--- ---------------- -------- ----- --- --------------\n" );

    number = 0;
    LIST_FOR_EACH( mvd_t, mvd, &mvd_channels, entry ) {
        Com_Printf( "%3d %-16.16s %-8.8s", number,
            mvd->name, mvd->mapname );
        switch( mvd->state ) {
        case MVD_DEAD:
            Com_Printf( " DEAD " );
            break;
	    case MVD_CONNECTING:
	    case MVD_CONNECTED:
            Com_Printf( " CNCT " );
            break;
	    case MVD_CHECKING:
	    case MVD_PREPARING:
            Com_Printf( " PREP " );
            break;
        case MVD_WAITING:
            Com_Printf( " WAIT " );
            break;
        case MVD_READING:
            Com_Printf( " READ " );
            break;
        case MVD_DISCONNECTED:
            Com_Printf( " DISC " );
            break;
        }
        usage = MVD_BufferPercent( mvd );
        Com_Printf( " %3d ", usage );
        if( mvd->demoplayback ) {
            Com_Printf( "%s", mvd->demopath );
        } else {
            Com_Printf( "%s", NET_AdrToString( &mvd->stream.address ) );
        }
        Com_Printf( "\n" );
        number++;
    }
}

void MVD_StreamedStop_f( void ) {
    mvd_t *mvd;
	uint16 msglen;

    mvd = MVD_SetChannel( 1 );
    if( !mvd ) {
        Com_Printf( "Usage: %s [channel]\n", Cmd_Argv( 0 ) );
        return;
    }

	if( !mvd->demorecording ) {
		Com_Printf( "Not recording on channel %s.\n", mvd->name );
		return;
	}

	msglen = 0;
	FS_Write( &msglen, 2, mvd->demofile );
	FS_FCloseFile( mvd->demofile );

	mvd->demofile = 0;
	mvd->demorecording = qfalse;

	Com_Printf( "Stopped recording on channel %s.\n", mvd->name );
}

void MVD_StreamedRecord_f( void ) {
	char buffer[MAX_QPATH];
	char *name;
	fileHandle_t f;
    mvd_t *mvd;
    uint32 magic;
    
	if( Cmd_Argc() < 2 || ( mvd = MVD_SetChannel( 2 ) ) == NULL ) {
		Com_Printf( "Usage: %s [/]<filename> [channel]\n", Cmd_Argv( 0 ) );
		return;
	}

	if( mvd->demorecording ) {
		Com_Printf( "Already recording on channel %s.\n", mvd->name );
		return;
	}

	if( mvd->state < MVD_WAITING ) {
		Com_Printf( "Channel %s is not ready for recording.\n", mvd->name );
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

	FS_FOpenFile( buffer, &f, FS_MODE_WRITE );
	if( !f ) {
		Com_EPrintf( "Couldn't open %s for writing\n", buffer );
		return;
	}
	
	Com_Printf( "Recording on channel %s into %s\n", mvd->name, buffer );

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
	netadr_t adr;
    netstream_t stream;
    char buffer[MAX_STRING_CHARS];
    char resource[MAX_STRING_CHARS];
    char credentials[MAX_STRING_CHARS];
	char *host, *p;
    mvd_t *mvd;
    uint16 port;

    if ( Cmd_Argc() < 2 ) {
        Com_Printf( "Usage: %s <[http://][user:pass@]server[:port][/resource]>", Cmd_Argv( 0 ) );
        return;
    }

	Cmd_ArgvBuffer( 1, buffer, sizeof( buffer ) );

    host = buffer;
    if( !strncmp( host, "http://", 7 ) ) {
        host += 7;
    }
    p = strchr( host, '@' );
    if( p ) {
        *p = 0;
        strcpy( credentials, host );
        host = p + 1;
    } else {
        credentials[0] = 0;
    }
    p = strchr( host, '/' );
    if( p ) {
        *p = 0;
        strcpy( resource, p + 1 );
        port = BigShort( 80 );
    } else {
        Com_sprintf( resource, sizeof( resource ),
            "mvdstream/%s", Cmd_Argv( 2 ) );
        port = BigShort( PORT_SERVER );
    }
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
    strcpy( mvd->name, "unnamed" );
    mvd->state = MVD_CONNECTING;
    mvd->stream = stream;
    mvd->stream.recv.data = Z_ReservedAlloc( MAX_MSGLEN * 2 );
    mvd->stream.recv.size = MAX_MSGLEN * 2;
    mvd->stream.send.data = Z_ReservedAlloc( 256 );
    mvd->stream.send.size = 256;
    List_Init( &mvd->udpClients );
    List_Init( &mvd->tcpClients );
    List_Init( &mvd->ready );
    List_Append( &mvd_channels, &mvd->entry );

    Com_Printf( "Connecting to %s...\n", NET_AdrToString( &adr ) );

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
        Com_Printf( "Usage: %s [channel]\n", Cmd_Argv( 0 ) );
        return;
    }

//    MVD_Drop( mvd, "Disconnected from console" );
}

const char *MVD_Play_g( const char *partial, int state ) {
	return Com_FileNameGeneratorByFilter( "demos", "*.mvd2;*.mvd2.gz",
        partial, qfalse, state );
}

void MVD_Play_f( void ) {
	char *name;
	char buffer[MAX_QPATH];
	fileHandle_t f;
	int length;
    uint32 magic = 0;
    mvd_t *mvd;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s [/]<filename>\n", Cmd_Argv( 0 ) );
		return;
	}

	name = Cmd_Argv( 1 );
	if( name[0] == '/' ) {
		Q_strncpyz( buffer, name + 1, sizeof( buffer ) );
	} else {
		Com_sprintf( buffer, sizeof( buffer ), "demos/%s", name );
		COM_DefaultExtension( buffer, ".mvd2", sizeof( buffer ) );
	}
	FS_FOpenFile( buffer, &f, FS_MODE_READ );
	if( !f ) {
		Com_Printf( "Couldn't open '%s'\n", buffer );
		return;
	}

    FS_Read( &magic, 4, f );
    if( magic != MVD_MAGIC ) {
        Com_Printf( "'%s' is not a MVD2 file\n", buffer );
		FS_FCloseFile( f );
        return;
    }

    Z_TagReserve( sizeof( *mvd ) + MAX_MSGLEN * 2, TAG_MVD );

    mvd = Z_ReservedAllocz( sizeof( *mvd ) );
    mvd->state = MVD_PREPARING;
    mvd->demoplayback = qtrue;
	mvd->demofile = f;
    mvd->stream.recv.data = Z_ReservedAlloc( MAX_MSGLEN * 2 );
    mvd->stream.recv.size = MAX_MSGLEN * 2;
    List_Init( &mvd->udpClients );
    List_Init( &mvd->tcpClients );
    List_Init( &mvd->ready );
    List_Append( &mvd_channels, &mvd->entry );

    if( dedicated->integer && !sv_nextserver->string[0] ) {
    	Cvar_Set( "nextserver", va( "mvdplay /%s", buffer ) );
    }

	length = FS_GetFileLengthNoCache( mvd->demofile );
	mvd->demofileFrameOffset = FS_Tell( mvd->demofile );
	mvd->demofileSize = length - mvd->demofileFrameOffset;
	strcpy( mvd->demopath, buffer );
}

static const cmdreg_t c_mvd[] = {
	{ "mvdplay", MVD_Play_f, MVD_Play_g },
	{ "mvdconnect", MVD_Connect_f },
	{ "mvdisconnect", MVD_Disconnect_f },
	{ "mvdspawn", MVD_Spawn_f },
	{ "mvdchannels", MVD_ListChannels_f },

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
	mvd_pause = Cvar_Get( "mvd_pause", "0", 0 );
	mvd_nextserver = Cvar_Get( "mvd_nextserver", "1", 0 );
	mvd_timeout = Cvar_Get( "mvd_timeout", "120", 0 );
	mvd_autoscores = Cvar_Get( "mvd_autoscores", "", 0 );
	mvd_safecmd = Cvar_Get( "mvd_safecmd", "", 0 );
	mvd_wait_enter = Cvar_Get( "mvd_wait_enter", "0.5", 0 );
	mvd_wait_leave = Cvar_Get( "mvd_wait_leave", "2", 0 );
//	mvd_drop_enter = Cvar_Get( "mvd_drop_enter", "95", 0 );
//	mvd_drop_leave = Cvar_Get( "mvd_drop_leave", "90", 0 );

    Cmd_Register( c_mvd );

    List_Init( &mvd_channels );
    List_Init( &mvd_ready );
}

