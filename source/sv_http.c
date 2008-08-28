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

#include "sv_local.h"
#include "mvd_local.h"

char            http_host[MAX_STRING_CHARS];
char            http_header[MAX_STRING_CHARS];
tcpClient_t     *http_client;

void SV_HttpHeader( const char *title ) {
    SV_HttpPrintf(
        "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 3.2//EN\">"
        "<html><head><title>%s</title></head><body>", title );
}

void SV_HttpFooter( void ) {
    SV_HttpPrintf( "<hr><address>" APPLICATION " "
        VERSION " at <a href=\"http://%s/\">%s</a></address></body></html>",
        http_host, http_host );
}

void SV_HttpReject( const char *error, const char *reason ) {
    if( http_client->state <= cs_zombie ) {
        return;
    }

    // construct HTTP response header
    SV_HttpPrintf( "HTTP/1.0 %s\r\n", error );
    if( http_header[0] ) {
        SV_HttpPrintf( "%s", http_header );
    }
    if( reason && http_client->method != HTTP_METHOD_HEAD ) {
        SV_HttpPrintf( "Content-Type: text/html; charset=us-ascii\r\n" );
    }
    SV_HttpPrintf( "\r\n" );

    // add optional response body
    if( reason && http_client->method != HTTP_METHOD_HEAD ) {
        SV_HttpHeader( error );
        SV_HttpPrintf( "<h1>%s</h1><p>%s</p>", error, reason );
        SV_HttpFooter();
    }

    SV_HttpDrop( http_client, error );
}

static void SV_HttpPrintTime( int sec ) {
    int min, hour, day;
    
    min = sec / 60; sec %= 60;
    hour = min / 60; min %= 60;
    day = hour / 24; hour %= 24;

    if( day ) {
        SV_HttpPrintf(
            "%d day%s, %d hour%s, %d min%s",
            day, day == 1 ? "" : "s",
            hour, hour == 1 ? "" : "s",
            min, min == 1 ? "" : "s" );
    } else if( hour ) {
        SV_HttpPrintf(
            "%d hour%s, %d min%s",
            hour, hour == 1 ? "" : "s",
            min, min == 1 ? "" : "s" );
    } else if( min ) {
        SV_HttpPrintf(
            "%d min%s",
            min, min == 1 ? "" : "s" );
    } else {
        SV_HttpPrintf( "&lt; 1 min" );
    }
}

static void SV_GetStatus( void ) {
    char buffer[MAX_STRING_CHARS];
    cvar_t *var;
    client_t *cl;
    int count, len, sec;
    time_t clock;

	if( sv_status_show->integer < 1 ) {
        SV_HttpReject( "403 Forbidden",
            "You do not have permission to view "
            "the status page of this server." );
		return;
	}

    SV_HttpPrintf( "HTTP/1.0 200 OK\r\n" );

    if( http_client->method == HTTP_METHOD_HEAD ) {
        SV_HttpPrintf( "\r\n" );
        SV_HttpDrop( http_client, "200 OK " );
        return;
    }

    SV_HttpPrintf(
        "Content-Type: text/html; charset=us-ascii\r\n"
        "\r\n" );

    count = SV_CountClients();
    len = Q_EscapeMarkup( buffer, sv_hostname->string, sizeof( buffer ) );
    Com_sprintf( buffer + len, sizeof( buffer ) - len, " - %d/%d",
        count, sv_maxclients->integer - sv_reserved_slots->integer );

    SV_HttpHeader( buffer );

    buffer[len] = 0;
    SV_HttpPrintf( "<h1>%s</h1>", buffer );

    time( &clock );

	if( sv_status_show->integer > 1 ) {
        SV_HttpPrintf( "<h2>Player Info</h2>" );
        if( count ) {
            SV_HttpPrintf( "<table border=\"1\">"
                "<tr><th>Score</th><th>Ping</th><th>Time</th><th>Name</th></tr>" );
            FOR_EACH_CLIENT( cl ) {
                if( cl->state < cs_connected ) {
                    continue;
                }

                SV_HttpPrintf( "<tr><td>%d</td><td>%d</td><td>",
                    cl->edict->client->ps.stats[STAT_FRAGS], cl->ping );

                if( cl->connect_time > clock ) {
                    cl->connect_time = clock;
                }
                sec = clock - cl->connect_time;

                SV_HttpPrintTime( sec );

                Q_EscapeMarkup( buffer, cl->name, sizeof( buffer ) );
                SV_HttpPrintf( "</td><td>%s</td></tr>", buffer );
            }
            SV_HttpPrintf( "</table>" );
        } else {
            SV_HttpPrintf( "<p>No players.</p>" );
        }
    }

    SV_HttpPrintf(
        "<h2>Server Info</h2><table border=\"1\"><tr>"
        "<th>Key</th><th>Value</th></tr>" );

	for( var = cvar_vars; var; var = var->next ) {
		if( !( var->flags & CVAR_SERVERINFO ) ) {
            continue;
        }
        if( !var->string[0] ) {
            continue;
        }

        Q_EscapeMarkup( buffer, var->name, sizeof( buffer ) );
        SV_HttpPrintf( "<tr><td>%s</td>", buffer );

        // XXX: ugly hack to hide reserved slots
        if( var == sv_maxclients && sv_reserved_slots->integer ) {
            SV_HttpPrintf( "<td>%d</td></tr>",
                sv_maxclients->integer - sv_reserved_slots->integer );
        } else {
            Q_EscapeMarkup( buffer, var->string, sizeof( buffer ) );
            SV_HttpPrintf( "<td>%s</td></tr>", buffer );
        }
	}

    // add uptime
    if( sv_uptime->integer ) {
        if( com_startTime > clock ) {
            com_startTime = clock;
        }
        sec = clock - com_startTime;

        SV_HttpPrintf( "<tr><td>uptime</td><td>" );
        SV_HttpPrintTime( sec );
        SV_HttpPrintf( "</td></tr>" );
    }

    SV_HttpPrintf( "</table>"
        "<p><a href=\"quake2://%s\">Join this server</a></p>", http_host );
    if( sv_mvd_enable->integer ) {
        SV_HttpPrintf(
            "<p><a href=\"http://%s/mvdstream\">Download MVD stream</a></p>",
            http_host );
    }
    SV_HttpFooter();

    SV_HttpDrop( http_client, "200 OK" );
}

static void uri_status( const char *uri ) {
    if( sv.state == ss_game ) {
        SV_GetStatus();
    } else {
        MVD_GetStatus();
    }
}

static void uri_mvdstream( const char *uri ) {
	if( !sv_mvd_enable->integer ) {
        SV_HttpReject( "403 Forbidden",
            "You do not have permission to access "
            "live MVD stream on this server." );
		return;
	}

    if( sv_mvd_auth->string[0] && ( http_client->credentials == NULL ||
          strcmp( http_client->credentials, sv_mvd_auth->string ) ) )
    {
        strcpy( http_header,
            "WWW-Authenticate: Basic realm=\"mvdstream\"\r\n" );
        SV_HttpReject( "401 Not Authorized",
            "You are not authorized to access "
            "live MVD stream on this server." );
        return;
    }

    if( sv.state == ss_game ) {
        SV_MvdGetStream( uri );
    } else {
        MVD_GetStream( uri );
    }
}

void SV_ConsoleOutput( const char *msg ) {
    tcpClient_t *client;
	char text[MAXPRINTMSG];
	char *p, *maxp;
    size_t len;
    int c;

    if( !svs.initialized ) {
        return;
    }
    if( LIST_EMPTY( &svs.console_list ) ) {
        return;
    }

	p = text;
    maxp = text + sizeof( text ) - 1;
	while( *msg ) {
		if( Q_IsColorString( msg ) ) {
			msg += 2;
			continue;
		}

        if( p == maxp ) {
            break;
        }

        c = *msg++;
        c &= 127;
		
		*p++ = c;
	}
	*p = 0;

	len = p - text;

    LIST_FOR_EACH( tcpClient_t, client, &svs.console_list, mvdEntry ) {
        if( FIFO_Write( &client->stream.send, text, len ) != len ) {
            SV_HttpDrop( client, "overflowed" );
        }
    }
}

static void uri_console( const char *uri ) {
    char *auth = sv_console_auth->string;
    char *cred = http_client->credentials;

    if( !auth[0] || !cred || strcmp( cred, auth ) ) {
        strcpy( http_header,
            "WWW-Authenticate: Basic realm=\"console\"\r\n" );
        SV_HttpReject( "401 Not Authorized",
            "You are not authorized to access "
            "console stream on this server." );
        return;
    }

    if( http_client->method == HTTP_METHOD_HEAD ) {
        SV_HttpPrintf( "HTTP/1.0 200 OK\r\n\r\n" );
        SV_HttpDrop( http_client, "200 OK " );
        return;
    }

    SV_HttpPrintf(
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n" );
    List_Append( &svs.console_list, &http_client->mvdEntry );
	http_client->state = cs_spawned;
}

#if 0
static void uri_root( const char *uri ) {
    SV_HttpPrintf( "HTTP/1.0 200 OK\r\n" );

    if( http_client->method == HTTP_METHOD_HEAD ) {
        SV_HttpPrintf( "\r\n" );
        SV_HttpDrop( http_client, "200 OK " );
        return;
    }

    SV_HttpPrintf(
        "Content-Type: text/html\r\n"
        "\r\n" );
    SV_HttpPrintf(
        "<html><head><title>Hello</title></head><body>"
        "<h1>Hello</h1><p>Hello world!</p>" );
    SV_HttpFooter();

    SV_HttpDrop( http_client, "200 OK" );
}
#endif

typedef struct {
    const char *uri;
    void (*handler)( const char * );
} uriEntry_t;

static const uriEntry_t rootURIs[] = {
    { "", uri_status },
    { "status", uri_status },
    { "mvdstream", uri_mvdstream },
    { "console", uri_console },
    { NULL }
};

void SV_HttpHandle( const uriEntry_t *e, const char *uri ) {
    const char *p;
    size_t length;

    if( *uri == '/' ) {
        uri++;
    }

    if( ( p = strchr( uri, '/' ) ) != NULL ) {
        length = p - uri;
    } else {
        length = strlen( uri );
        p = uri + length;
    }

    for( ; e->uri; e++ ) {
        if( !strncmp( e->uri, uri, length ) ) {
            e->handler( p );
            break;
        }
    }

    if( !e->uri ) {
        SV_HttpReject( "404 Not Found",
            "The requested URL was not found on this server." );
    }
}

void SV_HttpWrite( tcpClient_t *client, void *data, size_t len ) {
    fifo_t *fifo = &client->stream.send;

    if( client->state <= cs_zombie ) {
        return;
    }

#if USE_ZLIB
    if( client->z.state ) {
        z_streamp z = &client->z;
        int param = Z_NO_FLUSH;

        if( client->noflush > 120 ) {
            param = Z_SYNC_FLUSH;
        }

        z->next_in = data;
        z->avail_in = ( uInt )len;

        while( z->avail_in ) {
            data = FIFO_Reserve( fifo, &len );
            if( !len ) {
                SV_HttpDrop( client, "overflowed" );
                return;
            }

            z->next_out = data;
            z->avail_out = ( uInt )len;

            if( deflate( z, param ) != Z_OK ) {
                SV_HttpDrop( client, "deflate failed" );
                return;
            }

            len -= z->avail_out;
            if( len > 0 ) {
                FIFO_Commit( fifo, len );
                client->noflush = 0;
            }
        }
        return;
    }
#endif

    if( !FIFO_TryWrite( fifo, data, len ) ) {
        SV_HttpDrop( client, "overflowed" );
    }
}

void SV_HttpFinish( tcpClient_t *client ) {
#if USE_ZLIB
    fifo_t *fifo = &client->stream.send;
    z_streamp z = &client->z;
    byte *data;
    size_t len;
	int ret;

    if( client->state <= cs_zombie ) {
        return;
    }

    if( !z->state ) {
        return;
    }

    z->next_in = NULL;
    z->avail_in = 0;

    do {
        data = FIFO_Reserve( fifo, &len );
        if( !len ) {
            SV_HttpDrop( client, "overflowed" );
            return;
        }

        z->next_out = data;
        z->avail_out = ( uInt )len;

        ret = deflate( z, Z_FINISH );

        FIFO_Commit( fifo, len - z->avail_out );
    } while( ret == Z_OK );

    if( ret != Z_STREAM_END ) {
        SV_HttpDrop( client, "deflate failed" );
    }
#endif
}

void SV_HttpPrintf( const char *fmt, ... ) {
    char buffer[MAX_STRING_CHARS];
	va_list		argptr;
    size_t      len;

    if( http_client->state <= cs_zombie ) {
        return;
    }

	va_start( argptr, fmt );
	len = Q_vsnprintf( buffer, sizeof( buffer ), fmt, argptr );
	va_end( argptr );

    if( FIFO_Write( &http_client->stream.send, buffer, len ) != len ) {
        SV_HttpDrop( http_client, "overflowed" );
    }
}

void SV_HttpDrop( tcpClient_t *client, const char *error ) {
    if( client->state <= cs_zombie ) {
        return;
    }
    if( error ) {
        Com_DPrintf( "HTTP client %s dropped: %s\n",
            NET_AdrToString( &client->stream.address ), error );
    }

    if( client->resource ) {
        Z_Free( client->resource );
        client->resource = NULL;
    }
    if( client->host ) {
        Z_Free( client->host );
        client->host = NULL;
    }
    if( client->agent ) {
        Z_Free( client->agent );
        client->agent = NULL;
    }
    if( client->credentials ) {
        Z_Free( client->credentials );
        client->credentials = NULL;
    }
#if USE_ZLIB
    if( client->z.state ) {
        deflateEnd( &client->z );
    }
#endif

    List_Remove( &client->mvdEntry );
    client->mvd = NULL;
    client->state = cs_zombie;
    client->lastmessage = svs.realtime;
}

void SV_HttpRemove( tcpClient_t *client ) {
    char *addr, *dest;
    int count;

    addr = NET_AdrToString( &client->stream.address );
    NET_Close( &client->stream );
    List_Remove( &client->entry );

    count = List_Count( &svs.tcp_client_pool );
    if( count < sv_http_minclients->integer ) {
        List_Insert( &svs.tcp_client_pool, &client->entry );
        dest = "pool";
    } else {
        Z_Free( client );
        dest = "memory";
    }

    Com_DPrintf( "HTTP client %s removed into %s\n", addr, dest );
}

static void SV_HttpAccept( netstream_t *stream ) {
    tcpClient_t *client;
    netstream_t *s;
    int count;
    char *from;

    count = List_Count( &svs.tcp_client_list );
    if( count >= sv_http_maxclients->integer ) {
        Com_DPrintf( "HTTP client %s rejected: too many clients\n",
            NET_AdrToString( &stream->address ) );
    //    NET_TrySend( stream, "HTTP/1.0 503 Service Unavailable\r\n\r\n" );
        NET_Close( stream );
        return;
    }

    if( sv_iplimit->integer > 0 ) {
        count = 0;
        LIST_FOR_EACH( tcpClient_t, client, &svs.tcp_client_list, entry ) {
            if( NET_IsEqualBaseAdr( &client->stream.address, &stream->address ) ) {
                count++;
            }
        }
        if( count >= sv_iplimit->integer ) {
            Com_DPrintf( "HTTP client %s rejected: too many connections "
                "from single IP address\n",
                NET_AdrToString( &stream->address ) );
            NET_Close( stream );
            return;
        }
    }

    if( LIST_EMPTY( &svs.tcp_client_pool ) ) {
        client = SV_Malloc( sizeof( *client ) + 256 + MAX_MSGLEN * 2 );
        from = "memory";
    } else {
        client = LIST_FIRST( tcpClient_t, &svs.tcp_client_pool, entry );
        List_Remove( &client->entry );
        from = "pool";
    }

    memset( client, 0, sizeof( *client ) );
    List_Init( &client->mvdEntry );

    s = &client->stream;
    s->recv.data = ( byte * )( client + 1 );
    s->recv.size = 256;
    s->send.data = s->recv.data + 256;
    s->send.size = MAX_MSGLEN * 2;
    s->socket = stream->socket;
    s->address = stream->address;
    s->state = stream->state;

	client->lastmessage = svs.realtime;
    client->state = cs_assigned;
    List_SeqAdd( &svs.tcp_client_list, &client->entry );

    Com_DPrintf( "HTTP client %s accepted from %s\n",
        NET_AdrToString( &stream->address ), from );
}

static qboolean SV_HttpParseRequest( tcpClient_t *client ) {
    char key[MAX_TOKEN_CHARS];
    char *p, *token;
    const char *line;
    byte *b, *data;
    size_t length;
    int major, minor;

    while( 1 ) {
        data = FIFO_Peek( &client->stream.recv, &length );
        if( !length ) {
            break;
        }
        if( ( b = memchr( data, '\n', length ) ) != NULL ) {
            // TODO: support folded lines
            length = b - data + 1;
        }
        if( client->requestLength + length > MAX_NET_STRING - 1 ) {
            SV_HttpReject( "400 Bad Request", NULL );
            break;
        }

        memcpy( client->request + client->requestLength, data, length );
        client->requestLength += length;
        client->request[client->requestLength] = 0;

        FIFO_Decommit( &client->stream.recv, length );

        if( !b ) {
            continue;
        }

        line = client->request;
        client->requestLength = 0;

        if( !client->method ) {
            // parse request line
            token = COM_SimpleParse( &line, NULL );
            if( !token[0] ) {
                continue; // ignore empty lines
            }

            // parse method
            if( !strcmp( token, "GET" ) ) {
                client->method = HTTP_METHOD_GET;
            } else if( !strcmp( token, "HEAD" ) ) {
                client->method = HTTP_METHOD_HEAD;
            /*} else if( !strcmp( token, "POST" ) ) {
                client->method = HTTP_METHOD_POST;*/
            } else {
                SV_HttpReject( "501 Not Implemented", NULL );
                break;
            }

            // parse URI
            token = COM_SimpleParse( &line, NULL );
            if( !Q_stricmpn( token, "http://", 7 ) ) {
                p = strchr( token + 7, '/' );
                if( !p ) {
                    SV_HttpReject( "400 Bad Request", NULL );
                    break;
                }
                client->resource = SV_CopyString( p );
                *p = 0;
                client->host = SV_CopyString( token + 7 );
            } else {
                if( *token != '/' ) {
                    SV_HttpReject( "400 Bad Request", NULL );
                    break;
                }
                client->resource = SV_CopyString( token );
            }

            // parse version
            token = COM_SimpleParse( &line, NULL );
            if( strncmp( token, "HTTP/", 5 ) ) {
                SV_HttpReject( "400 Bad Request", NULL );
                break;
            }
            major = strtoul( token + 5, &token, 10 );
            if( *token != '.' ) {
                SV_HttpReject( "400 Bad Request", NULL );
                break;
            }
            minor = strtoul( token + 1, &token, 10 );
            if( major != 1 || ( minor != 0 && minor != 1 ) ) {
                SV_HttpReject( "505 HTTP Version not supported", NULL );
                break;
            }
        } else {
            token = COM_SimpleParse( &line, NULL );
            if( !token[0] ) {
                if( !client->host || !client->resource ) {
                    SV_HttpReject( "400 Bad Request", NULL );
                    return qfalse;
                }
                return qtrue; // end of header
            }
            // parse header fields
            strcpy( key, token );
            p = strchr( key, ':' );
            if( !p ) {
                SV_HttpReject( "400 Bad Request", NULL );
                break;
            }
            *p = 0;
            Q_strlwr( key );

            token = COM_SimpleParse( &line, NULL );
            if( !strcmp( key, "host" ) ) {
                if( !client->host ) {
                    client->host = SV_CopyString( token );
                }
                continue;
            }

            if( !strcmp( key, "authorization" ) ) {
                if( Q_stricmp( token, "Basic" ) ) {
                    continue;
                }
                token = COM_SimpleParse( &line, NULL );
                if( Q_Decode64( key, token, sizeof( key ) ) == -1 ) {
                    continue;
                }
                client->credentials = SV_CopyString( key );
                continue;
            }

            if( !strcmp( key, "user-agent" ) ) {
                if( !client->agent ) {
                    client->agent = SV_CopyString( token );
                }
                continue;
            }
        }
    }

    return qfalse;
}


void SV_HttpRun( void ) {
    tcpClient_t *client, *next;
    neterr_t ret;
    netstream_t stream;
	unsigned    zombie_time = 1000 * sv_zombietime->value;
	unsigned    drop_time = 1000 * sv_timeout->value;
    unsigned    ghost_time = 1000 * sv_ghostime->value;
    unsigned    delta;

    // accept new connections
    ret = NET_Accept( &net_from, &stream );
    if( ret == NET_ERROR ) {
        Com_DPrintf( "%s from %s, ignored\n", NET_ErrorString(),
            NET_AdrToString( &net_from ) );
    } else if( ret == NET_OK ) {
        SV_HttpAccept( &stream );
    }

    // run existing connections
    LIST_FOR_EACH_SAFE( tcpClient_t, client, next, &svs.tcp_client_list, entry ) {
        http_client = client;
        http_header[0] = 0;

        // check timeouts
		delta = svs.realtime - client->lastmessage;
        switch( client->state ) {
        case cs_zombie:
            if( delta > zombie_time || !FIFO_Usage( &client->stream.send ) ) {
                SV_HttpRemove( client );
			    continue;
		    }
            break;
        case cs_assigned:
            if( delta > ghost_time || delta > drop_time ) {
                SV_HttpReject( "408 Request Timeout", NULL );
                continue;
            }
            break;
        default:
            if( delta > drop_time ) {
                SV_HttpDrop( client, "connection timed out" );
                SV_HttpRemove( client );
                continue;
            }
            break;
        }

        // run network stream
        ret = NET_Run( &client->stream );
        if( ret == NET_AGAIN ) {
            // don't timeout
            if( client->state >= cs_connected && !FIFO_Usage( &client->stream.send ) ) {
                client->lastmessage = svs.realtime;
            }
            continue;
        }
        if( ret != NET_OK ) {
            SV_HttpDrop( client, "connection reset by peer" );
            SV_HttpRemove( client );
            continue;
        }

        // don't timeout
        if( client->state >= cs_connected ) {
            client->lastmessage = svs.realtime;
        }

        // parse the request
        if( client->state == cs_assigned ) {
            if( SV_HttpParseRequest( client ) ) {
                Com_DPrintf( "GET %s\n", client->resource );
                client->state = cs_connected;
                Q_EscapeMarkup( http_host, client->host, sizeof( http_host ) );
                SV_HttpHandle( rootURIs, client->resource );
            }

            http_client = NULL;
        }

    }
}

