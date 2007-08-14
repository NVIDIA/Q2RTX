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
        "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">"
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
        SV_HttpPrintf( "Content-Type: text/html\r\n" );
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

static void uri_status( const char *uri ) {
    char buffer[MAX_STRING_CHARS];
    cvar_t *var;
    client_t *cl;
    int count;

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
        "Content-Type: text/html\r\n"
//      "Content-Encoding: deflate\r\n"
        "\r\n" );

    count = 0;
    FOR_EACH_CLIENT( cl ) {
		if( cl->state != cs_zombie ) {
            count++;
        }
    }

    Q_EscapeMarkup( buffer, sv_hostname->string, sizeof( buffer ) );
    SV_HttpHeader( va( "%s - %d/%d", buffer, count, sv_maxclients->integer ) );
    SV_HttpPrintf( "<h1>Status page of %s</h1>", buffer );

	if( sv_status_show->integer > 1 ) {
        SV_HttpPrintf( "<h2>Player Info</h2>" );
        if( count ) {
            SV_HttpPrintf(
                "<table border=\"1\"><tr>"
                "<th>Score</th><th>Ping</th><th>Name</th></tr>" );
            FOR_EACH_CLIENT( cl ) {
                if( cl->state >= cs_connected ) {
                    Q_EscapeMarkup( buffer, cl->name, sizeof( buffer ) );
                    SV_HttpPrintf( "<tr><td>%d</td><td>%d</td><td>%s</td></tr>",
                        cl->edict->client->ps.stats[STAT_FRAGS],
                            cl->ping, buffer );
                }
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
        Q_EscapeMarkup( buffer, var->string, sizeof( buffer ) );
        SV_HttpPrintf( "<td>%s</td></tr>", buffer );
	}

    SV_HttpPrintf( "</table><br><a href=\"quake2://%s\">Join this server</a>",
        http_host );
    SV_HttpFooter();

//    deflateInit( &client->z, Z_DEFAULT_COMPRESSION );

    //SV_HttpFinish( client );

  //  deflateEnd( &client->z );

    SV_HttpDrop( http_client, "200 OK" );
}

static void uri_mvdstream( const char *uri ) {
    if( sv.state != ss_game && sv.state != ss_broadcast ) {
        SV_HttpReject( "403 Forbidden",
            "Live MVD stream is unavailable on this server." );
        return;
    }
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
    { NULL }
};

void SV_HttpHandle( const uriEntry_t *e, const char *uri ) {
    const char *p;
    int length;

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

void SV_HttpWrite( tcpClient_t *client, void *data, int length ) {
    fifo_t *fifo = &client->stream.send;
#if USE_ZLIB
    z_streamp z = &client->z;
    int param;
#endif

    if( client->state <= cs_zombie ) {
        return;
    }

#if USE_ZLIB
    if( !z->state ) {
        if( !FIFO_Write( fifo, data, length ) ) {
            SV_HttpDrop( client, "overflowed" );
        }
        return;
    }

    z->next_in = data;
    z->avail_in = length;

    param = Z_NO_FLUSH;
    if( client->noflush > 120 ) {
        param = Z_SYNC_FLUSH;
    }

    while( z->avail_in ) {
        data = FIFO_Reserve( fifo, &length );
        if( !length ) {
            SV_HttpDrop( client, "overflowed" );
            return;
        }

        z->next_out = data;
        z->avail_out = length;

        if( deflate( z, param ) != Z_OK ) {
            SV_HttpDrop( client, "deflate failed" );
            return;
        }

        length -= z->avail_out;
        if( length ) {
            FIFO_Commit( fifo, length );
            client->noflush = 0;
        }
    }
#else
    if( !FIFO_Write( fifo, data, length ) ) {
        SV_HttpDrop( client, "overflowed" );
    }
#endif
}

void SV_HttpFinish( tcpClient_t *client ) {
#if USE_ZLIB
    fifo_t *fifo = &client->stream.send;
    z_streamp z = &client->z;
    byte *data;
    int length, ret;

    if( client->state <= cs_zombie ) {
        return;
    }

    if( !z->state ) {
        return;
    }

    z->next_in = NULL;
    z->avail_in = 0;

    do {
        data = FIFO_Reserve( fifo, &length );
        if( !length ) {
            SV_HttpDrop( client, "overflowed" );
            return;
        }

        z->next_out = data;
        z->avail_out = length;

        ret = deflate( z, Z_FINISH );

        FIFO_Commit( fifo, length - z->avail_out );
    } while( ret == Z_OK );

    if( ret != Z_STREAM_END ) {
        SV_HttpDrop( client, "deflate failed" );
    }
#endif
}

void SV_HttpPrintf( const char *fmt, ... ) {
    char buffer[MAX_STRING_CHARS];
	va_list		argptr;
    int         length;

    if( http_client->state <= cs_zombie ) {
        return;
    }

	va_start( argptr, fmt );
	length = Q_vsnprintf( buffer, sizeof( buffer ), fmt, argptr );
	va_end( argptr );

    if( !FIFO_Write( &http_client->stream.send, buffer, length ) ) {
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
    Com_DPrintf( "HTTP client %s removed\n",
        NET_AdrToString( &client->stream.address ) );

    NET_Close( &client->stream );
    List_Delete( &client->entry );
    Z_Free( client );
}

static void SV_HttpAccept( netstream_t *stream ) {
    tcpClient_t *client;
    netstream_t *s;
    int count;

    count = List_Count( &svs.tcpClients );
    if( count >= sv_http_maxclients->integer ) {
        Com_DPrintf( "HTTP client %s rejected: too many clients\n",
            NET_AdrToString( &stream->address ) );
    //    NET_TrySend( stream, "HTTP/1.0 503 Service Unavailable\r\n\r\n" );
        NET_Close( stream );
        return;
    }

    if( sv_iplimit->integer > 0 ) {
        count = 0;
        LIST_FOR_EACH( tcpClient_t, client, &svs.tcpClients, entry ) {
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

    Z_TagReserve( sizeof( *client ) + 256 + MAX_MSGLEN * 2, TAG_SERVER );
    client = Z_ReservedAllocz( sizeof( *client ) );
	client->lastmessage = svs.realtime;
    client->state = cs_assigned;
    List_Init( &client->mvdEntry );

    s = &client->stream;
    s->recv.data = Z_ReservedAlloc( 256 );
    s->recv.size = 256;
    s->send.data = Z_ReservedAlloc( MAX_MSGLEN * 2 );
    s->send.size = MAX_MSGLEN * 2;
    s->socket = stream->socket;
    s->address = stream->address;
    s->state = stream->state;

    List_Append( &svs.tcpClients, &client->entry );

    Com_DPrintf( "HTTP client %s accepted\n",
        NET_AdrToString( &stream->address ) );
}

static qboolean SV_HttpParseRequest( tcpClient_t *client ) {
    char key[MAX_TOKEN_CHARS];
    char *p, *token;
    const char *line;
    byte *b, *data;
    int length;
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
            SV_HttpReject( "400 Bad Request",
                "Maximum request line length exceeded." );
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
            token = COM_SimpleParse( &line );
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
                SV_HttpReject( "501 Not Implemented",
                    "Specified method is not implemented." );
                break;
            }

            // parse URI
            token = COM_SimpleParse( &line );
            if( !Q_stricmpn( token, "http://", 7 ) ) {
                p = strchr( token + 7, '/' );
                if( !p ) {
                    SV_HttpReject( "400 Bad Request", "Invalid absolute URI." );
                    break;
                }
                client->resource = SV_CopyString( p );
                *p = 0;
                client->host = SV_CopyString( token + 7 );
            } else {
                if( *token != '/' ) {
                    SV_HttpReject( "400 Bad Request", "Invalid relative URI." );
                    break;
                }
                client->resource = SV_CopyString( token );
            }
            Com_DPrintf( "GET %s\n", client->resource );

            // parse version
            token = COM_SimpleParse( &line );
            if( strncmp( token, "HTTP/", 5 ) ) {
                SV_HttpReject( "400 Bad Request", "HTTP version is malfromed." );
                break;
            }
            major = strtoul( token + 5, &token, 10 );
            if( *token != '.' ) {
                SV_HttpReject( "400 Bad Request", "HTTP version is malformed." );
                break;
            }
            minor = strtoul( token + 1, &token, 10 );
            if( major != 1 || ( minor != 0 && minor != 1 ) ) {
                SV_HttpReject( "505 HTTP Version not supported",
                    "HTTP version is not supported." );
                break;
            }
        } else {
            token = COM_SimpleParse( &line );
            if( !token[0] ) {
                if( !client->host ) {
                    SV_HttpReject( "400 Bad Request", "Missing host field." );
                }
                return qtrue; // end of header
            }
            // parse header fields
            strcpy( key, token );
            p = strchr( key, ':' );
            if( !p ) {
                SV_HttpReject( "400 Bad Request", "Malformed request field." );
                break;
            }
            *p = 0;
            Q_strlwr( key );

            token = COM_SimpleParse( &line );
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
                token = COM_SimpleParse( &line );
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
    uint32      point;

    // accept new connections
    ret = NET_Accept( &net_from, &stream );
    if( ret == NET_ERROR ) {
        Com_DPrintf( "%s from %s, ignored\n", NET_ErrorString(),
            NET_AdrToString( &net_from ) );
    } else if( ret == NET_OK ) {
        SV_HttpAccept( &stream );
    }

    // run existing connections
    LIST_FOR_EACH_SAFE( tcpClient_t, client, next, &svs.tcpClients, entry ) {
        http_client = client;
        http_header[0] = 0;

        // check timeouts
		if( client->lastmessage > svs.realtime ) {
			client->lastmessage = svs.realtime;
		}
        switch( client->state ) {
        case cs_zombie:
            if( client->lastmessage < svs.zombiepoint ||
                FIFO_Usage( &client->stream.send ) == 0 )
            {
                SV_HttpRemove( client );
			    continue;
		    }
            break;
        case cs_assigned:
            point = svs.droppoint;
            if( point < svs.ghostpoint ) {
                point = svs.ghostpoint;
            }
            if( client->lastmessage < point ) {
                SV_HttpReject( "408 Request Timeout", NULL );
                continue;
            }
            break;
        default:
            if( client->lastmessage < svs.droppoint ) {
                SV_HttpDrop( client, "connection timed out" );
                SV_HttpRemove( client );
                continue;
            }
            break;
        }

        // run network stream
        ret = NET_Run( &client->stream );
        if( ret == NET_AGAIN ) {
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
                client->state = cs_connected;
                Q_EscapeMarkup( http_host, client->host, sizeof( http_host ) );
                SV_HttpHandle( rootURIs, client->resource );
            }

            http_client = NULL;
        }

    }
}

