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
// sv_user.c -- server code for moving users

#include "sv_local.h"
#include "mvd_local.h"

edict_t	*sv_player;

/*
============================================================

USER STRINGCMD EXECUTION

sv_client and sv_player will be valid.
============================================================
*/

/*
================
SV_CreateBaselines

Entity baselines are used to compress the update messages
to the clients -- only the fields that differ from the
baseline will be transmitted
================
*/
static void create_baselines( void ) {
	int		i;
	edict_t	*ent;
	entity_state_t *base, **chunk;

	// clear baselines from previous level
	for( i = 0; i < SV_BASELINES_CHUNKS; i++ ) {
		base = sv_client->baselines[i];
		if( !base ) {
			continue;
		} 
		memset( base, 0, sizeof( *base ) * SV_BASELINES_PER_CHUNK );
	}

	for( i = 1; i < sv_client->pool->num_edicts; i++ ) {
		ent = EDICT_POOL( sv_client, i );

        if( ( gameFeatures & GAME_FEATURE_PROPERINUSE ) && !ent->inuse ) {
            continue;
        }

		if( !ES_INUSE( &ent->s ) ) {
			continue;
		}

		ent->s.number = i;

		chunk = &sv_client->baselines[i >> SV_BASELINES_SHIFT];
		if( *chunk == NULL ) {
			*chunk = SV_Mallocz( sizeof( *base ) * SV_BASELINES_PER_CHUNK );
		}

		base = *chunk + ( i & SV_BASELINES_MASK );

		*base = ent->s;
	}

}

static void write_plain_configstrings( void ) {
	int			i;
	char		*string;
	int			length;

	// write a packet full of data
	string = sv_client->configstrings;
	for( i = 0; i < MAX_CONFIGSTRINGS; i++, string += MAX_QPATH ) {
		if( !string[0] ) {
			continue;
		}
		length = strlen( string );
		if( length > MAX_QPATH ) {
			length = MAX_QPATH;
		}
		// check if this configstring will overflow
		if( msg_write.cursize + length + 64 > sv_client->netchan->maxpacketlen )
        {
		    SV_ClientAddMessage( sv_client, MSG_RELIABLE|MSG_CLEAR );
        }

		MSG_WriteByte( svc_configstring );
		MSG_WriteShort( i );
		MSG_WriteData( string, length );
		MSG_WriteByte( 0 );
	}

	SV_ClientAddMessage( sv_client, MSG_RELIABLE|MSG_CLEAR );
}

static void write_plain_baselines( void ) {
	int		i, j;
	entity_state_t	*base;

	// write a packet full of data
    for( i = 0; i < SV_BASELINES_CHUNKS; i++ ) {
		base = sv_client->baselines[i];
		if( !base ) {
			continue;
		}
    	for( j = 0; j < SV_BASELINES_PER_CHUNK; j++ ) {
            if( base->number ) {
                // check if this baseline will overflow
                if( msg_write.cursize + 64 > sv_client->netchan->maxpacketlen ) 
                {
                    SV_ClientAddMessage( sv_client, MSG_RELIABLE|MSG_CLEAR );
                }

                MSG_WriteByte( svc_spawnbaseline );
                MSG_WriteDeltaEntity( NULL, base, MSG_ES_FORCE );
            }
            base++;
        }
	}

	SV_ClientAddMessage( sv_client, MSG_RELIABLE|MSG_CLEAR );	
}

#if USE_ZLIB

static void write_compressed_gamestate( void ) {
	sizebuf_t	*buf = &sv_client->netchan->message;
	entity_state_t	*base;
	int			i, j, length;
    uint16      *patch;
    char        *string;

    MSG_WriteByte( svc_gamestate );

	// write configstrings
    string = sv_client->configstrings;
	for( i = 0; i < MAX_CONFIGSTRINGS; i++, string += MAX_QPATH ) {
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
    MSG_WriteShort( MAX_CONFIGSTRINGS ); // end of configstrings

    // write baselines
    for( i = 0; i < SV_BASELINES_CHUNKS; i++ ) {
		base = sv_client->baselines[i];
		if( !base ) {
			continue;
		}
    	for( j = 0; j < SV_BASELINES_PER_CHUNK; j++ ) {
            if( base->number ) {
                MSG_WriteDeltaEntity( NULL, base, MSG_ES_FORCE );
            }
            base++;
        }
	}
    MSG_WriteShort( 0 ); // end of baselines

	SZ_WriteByte( buf, svc_zpacket );
    patch = SZ_GetSpace( buf, 2 );
	SZ_WriteShort( buf, msg_write.cursize );

    deflateReset( &svs.z );
    svs.z.next_in = msg_write.data;
    svs.z.avail_in = msg_write.cursize;
    svs.z.next_out = buf->data + buf->cursize;
    svs.z.avail_out = buf->maxsize - buf->cursize;
	SZ_Clear( &msg_write );

	if( deflate( &svs.z, Z_FINISH ) != Z_STREAM_END ) {
        SV_DropClient( sv_client, "deflate() failed on gamestate" );
        return;
    }

    if( sv_debug_send->integer ) {
        Com_Printf( S_COLOR_BLUE"%s: comp: %lu into %lu\n",
            sv_client->name, svs.z.total_in, svs.z.total_out );
    }

    *patch = LittleShort( svs.z.total_out );
    buf->cursize += svs.z.total_out;
}

static inline int z_flush( byte *buffer ) {
    int ret;

    ret = deflate( &svs.z, Z_FINISH ); 
	if( ret != Z_STREAM_END ) {
		return ret;
	}

    if( sv_debug_send->integer ) {
        Com_Printf( S_COLOR_BLUE"%s: comp: %lu into %lu\n",
            sv_client->name, svs.z.total_in, svs.z.total_out );
    }

	MSG_WriteByte( svc_zpacket );
	MSG_WriteShort( svs.z.total_out );
	MSG_WriteShort( svs.z.total_in );
	MSG_WriteData( buffer, svs.z.total_out );
	
	SV_ClientAddMessage( sv_client, MSG_RELIABLE|MSG_CLEAR );

	return ret;
}

static inline void z_reset( byte *buffer ) {
    deflateReset( &svs.z );
    svs.z.next_out = buffer;
    svs.z.avail_out = sv_client->netchan->maxpacketlen - 5;
}

static void write_compressed_configstrings( void ) {
	int			i, length;
	byte		buffer[MAX_PACKETLEN_WRITABLE];
    char        *string;

    z_reset( buffer );

	// write a packet full of data
    string = sv_client->configstrings;
	for( i = 0; i < MAX_CONFIGSTRINGS; i++, string += MAX_QPATH ) {
		if( !string[0] ) {
			continue;
		}
		length = strlen( string );
		if( length > MAX_QPATH ) {
			length = MAX_QPATH;
		}

		// check if this configstring will overflow
		if( svs.z.avail_out < length + 32 ) {
			// then flush compressed data
			if( z_flush( buffer ) != Z_STREAM_END ) {
                goto fail;
			}
            z_reset( buffer );
		}

		MSG_WriteByte( svc_configstring );
		MSG_WriteShort( i );
		MSG_WriteData( string, length );
		MSG_WriteByte( 0 );

        svs.z.next_in = msg_write.data;
        svs.z.avail_in = msg_write.cursize;
        SZ_Clear( &msg_write );

    	if( deflate( &svs.z, Z_SYNC_FLUSH ) != Z_OK ) {
            goto fail;
        }
	}

	// finally flush all remaining compressed data
	if( z_flush( buffer ) != Z_STREAM_END ) {
fail:
        SV_DropClient( sv_client, "deflate() failed on configstrings" );
	}
}

#endif // USE_ZLIB

static const char junkchars[] =
    "!~#``&'()*`+,-./~01~2`3`4~5`67`89:~<=`>?@~ab~cd`ef~j~k~lm`no~pq`rst`uv`w``x`yz[`\\]^_`|~";

/*
================
SV_New_f

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each server load.
================
*/
void SV_New_f( void ) {
    char junk[8][16];
    int i, j, c;

	Com_DPrintf( "New() from %s\n", sv_client->name );

    if( sv_client->state < cs_connected ) {
	    Com_DPrintf( "Going from cs_assigned to cs_connected for %s\n",
            sv_client->name );
        sv_client->state = cs_connected;
        sv_client->lastmessage = svs.realtime; // don't timeout
    } else if( sv_client->state > cs_connected ) {
		Com_DPrintf( "New not valid -- already primed\n" );
		return;
	}

    if( sv_force_reconnect->string[0] && !sv_client->reconnect_var[0] &&
        !NET_IsLocalAddress( &sv_client->netchan->remote_address ) )
    {
        for( i = 0; i < 8; i++ ) {
            for( j = 0; j < 15; j++ ) {
                c = rand() | ( rand() >> 8 );
                c %= sizeof( junkchars ) - 1;
                junk[i][j] = junkchars[c];
            }
            junk[i][15] = 0;
        }

        strcpy( sv_client->reconnect_var, junk[2] );
        strcpy( sv_client->reconnect_val, junk[3] );

        SV_ClientCommand( sv_client, "set %s set\n", junk[0] );
        SV_ClientCommand( sv_client, "$%s %s connect\n", junk[0], junk[1] );
        if( rand() & 1 ) {
            SV_ClientCommand( sv_client, "$%s %s %s\n", junk[0], junk[2], junk[3] );
            SV_ClientCommand( sv_client, "$%s %s %s\n", junk[0], junk[4],
                sv_force_reconnect->string );
            SV_ClientCommand( sv_client, "$%s %s %s\n", junk[0], junk[5], junk[6] );
        } else {
            SV_ClientCommand( sv_client, "$%s %s %s\n", junk[0], junk[4],
                sv_force_reconnect->string );
            SV_ClientCommand( sv_client, "$%s %s %s\n", junk[0], junk[5], junk[6] );
            SV_ClientCommand( sv_client, "$%s %s %s\n", junk[0], junk[2], junk[3] );
        }
        SV_ClientCommand( sv_client, "$%s %s \"\"\n", junk[0], junk[0] );
        SV_ClientCommand( sv_client, "$%s $%s\n", junk[1], junk[4] );
        SV_DropClient( sv_client, NULL );
        return;
    }

	SV_ClientCommand( sv_client, "\n" );

	//
	// serverdata needs to go over for all types of servers
	// to make sure the protocol is right, and to set the gamedir
	//
    
    // create baselines for this client
    create_baselines();

	// send the serverdata
	MSG_WriteByte( svc_serverdata );
	MSG_WriteLong( sv_client->protocol );
	MSG_WriteLong( sv.spawncount );
	MSG_WriteByte( 0 ); // no attract loop
	MSG_WriteString( sv_client->gamedir );
	MSG_WriteShort( sv_client->number );
	MSG_WriteString( sv_client->mapname );

	// send protocol specific stuff
	switch( sv_client->protocol ) {
	case PROTOCOL_VERSION_R1Q2:
		MSG_WriteByte( 0 ); // not enhanced
		MSG_WriteShort( sv_client->version );
		MSG_WriteByte( 0 ); // no advanced deltas
		MSG_WriteByte( sv_strafejump_hack->integer ? 1 : 0 );
		break;
	case PROTOCOL_VERSION_Q2PRO:
		MSG_WriteShort( sv_client->version );
		MSG_WriteByte( svs.gametype );
		MSG_WriteByte( sv_strafejump_hack->integer ? 1 : 0 );
		MSG_WriteByte( sv_qwmod->integer );
		break;
	default:
		break;
	}

	SV_ClientAddMessage( sv_client, MSG_RELIABLE|MSG_CLEAR );

	SV_ClientCommand( sv_client, "\n" );

	// send version string request
	if( !sv_client->versionString ) {
		SV_ClientCommand( sv_client, "cmd \177c version $version\n" );
	}

    // send reconnect var request
    if( sv_force_reconnect->string[0] && !sv_client->reconnect_done ) {
        if( NET_IsLocalAddress( &sv_client->netchan->remote_address ) ) {
            sv_client->reconnect_done = qtrue;
        } else {
    		SV_ClientCommand( sv_client, "cmd \177c connect $%s\n",
                sv_client->reconnect_var );
        }
    }

	Com_DPrintf( "Going from cs_connected to cs_primed for %s\n",
        sv_client->name );
	sv_client->state = cs_primed;

	memset( &sv_client->lastcmd, 0, sizeof( sv_client->lastcmd ) );

#if USE_ZLIB
    if( !sv_client->zlib ) {
		write_plain_configstrings();
		write_plain_baselines();
    } else {
        if( sv_client->netchan->type == NETCHAN_NEW ) {
            write_compressed_gamestate();
        } else {
            // FIXME: Z_SYNC_FLUSH is not efficient for baselines
            write_compressed_configstrings();
		    write_plain_baselines();
        }
	}
#else // USE_ZLIB
	write_plain_configstrings();
	write_plain_baselines();
#endif // !USE_ZLIB

	// send next command
	SV_ClientCommand( sv_client, "precache %i\n", sv.spawncount );
}

/*
==================
SV_Begin_f
==================
*/
void SV_Begin_f( void ) {
	Com_DPrintf( "Begin() from %s\n", sv_client->name );

	// handle the case of a level changing while a client was connecting
	if( sv_client->state < cs_primed ) {
		Com_DPrintf( "Begin not valid -- not yet primed\n" );
		SV_New_f();
		return;
	}
	if( sv_client->state > cs_primed ) {
		Com_DPrintf( "Begin not valid -- already spawned\n" );
		return;
	}

    if( sv_force_reconnect->string[0] && !sv_client->reconnect_done ) {
        if( dedicated->integer ) {
            Com_Printf( "%s[%s]: failed to reconnect\n", sv_client->name,
                NET_AdrToString( &sv_client->netchan->remote_address ) );
        }
        SV_DropClient( sv_client, NULL );
        return;
    }

#if USE_ANTICHEAT & 2
    if( !AC_ClientBegin( sv_client ) ) {
        return;
    }
#endif

	Com_DPrintf( "Going from cs_primed to cs_spawned for %s\n",
        sv_client->name );
	sv_client->state = cs_spawned;
	sv_client->sendTime = 0;
	sv_client->commandMsec = 1800;
    sv_client->surpressCount = 0;
	
	// call the game begin function
	ge->ClientBegin( sv_player );

#if USE_ANTICHEAT & 2
    AC_ClientAnnounce( sv_client );
#endif
}

//=============================================================================

#define MAX_DOWNLOAD_CHUNK	1024

/*
==================
SV_NextDownload_f
==================
*/
static void SV_NextDownload_f( void ) {
	int		r;
	int		percent;
	int		size;

	if ( !sv_client->download )
		return;

	r = sv_client->downloadsize - sv_client->downloadcount;
	if ( r > MAX_DOWNLOAD_CHUNK )
		r = MAX_DOWNLOAD_CHUNK;

	MSG_WriteByte( svc_download );
	MSG_WriteShort( r );

	sv_client->downloadcount += r;
	size = sv_client->downloadsize;
	if( !size )
		size = 1;
	percent = sv_client->downloadcount*100/size;
	MSG_WriteByte( percent );
	MSG_WriteData( sv_client->download + sv_client->downloadcount - r, r );

	if( sv_client->downloadcount == sv_client->downloadsize ) {
		FS_FreeFile( sv_client->download );
		sv_client->download = NULL;
	}
		
	SV_ClientAddMessage( sv_client, MSG_RELIABLE|MSG_CLEAR );

}

static void SV_DownloadFailed( void ) {
	MSG_WriteByte( svc_download );
	MSG_WriteShort( -1 );
	MSG_WriteByte( 0 );
	SV_ClientAddMessage( sv_client, MSG_RELIABLE|MSG_CLEAR );
}

/*
==================
SV_BeginDownload_f
==================
*/
static void SV_BeginDownload_f( void ) {
	char	name[MAX_QPATH];
	int		downloadsize;
	int		offset = 0;
	cvar_t	*allow;
	int		length;
	char	*filename;

	length = Q_ClearStr( name, Cmd_Argv( 1 ),  sizeof( name ) );
	Q_strlwr( name );

	if( Cmd_Argc() > 2 )
		offset = atoi( Cmd_Argv( 2 ) ); // downloaded offset

	// hacked by zoid to allow more conrol over download
	// first off, no .. or global allow check
	if( !allow_download->integer
		// check for empty paths
		|| !length
		// check for illegal negative offsets
		|| offset < 0
		// don't allow anything with .. path
		|| strstr( name, ".." )
		// leading dots, slashes, etc are no good
		|| !Q_ispath( name[0] )
		// trailing dots, slashes, etc are no good
		|| !Q_ispath( name[ length - 1 ] )
		// back slashes should be never sent
		|| strchr( name, '\\' )
		// colons are bad also
		|| strchr( name, ':' )
		// MUST be in a subdirectory	
		|| !strchr( name, '/' ) )	
	{	
		SV_DownloadFailed();
		return;
	}

	if( strncmp( name, "players/", 8 ) == 0 ) {
		allow = allow_download_players;
	} else if( strncmp( name, "models/", 7 ) == 0 ) {
		allow = allow_download_models;
	} else if( strncmp( name, "sound/", 6 ) == 0 ) {
		allow = allow_download_sounds;
	} else if( strncmp( name, "maps/", 5 ) == 0 ) {
		allow = allow_download_maps;
	} else {
		allow = allow_download_other;
	}

	if( !allow->integer ) {
		Com_DPrintf( "Refusing download of %s to %s\n", name, sv_client->name );
		SV_DownloadFailed();
		return;
	}

	if( sv_client->download ) {
		Com_DPrintf( "Closing existing download for %s (should not happen)\n", sv_client->name );
		FS_FreeFile( sv_client->download );
		sv_client->download = NULL;
	}

	filename = name;

	downloadsize = FS_LoadFileEx( filename, NULL, FS_FLAG_RAW );
	
	if( downloadsize == -1 || downloadsize == 0
		// special check for maps, if it came from a pak file, don't allow
		// download  ZOID
		|| ( allow == allow_download_maps
			&& allow_download_maps->integer < 2
			&& FS_LastFileFromPak() ) )
	{
		Com_DPrintf( "Couldn't download %s to %s\n", name, sv_client->name );
		SV_DownloadFailed();
		return;
	}

	if( offset > downloadsize ) {
		Com_DPrintf( "Refusing download, %s has wrong version of %s (%d > %d)\n",
			sv_client->name, name, offset, downloadsize );
		SV_ClientPrintf( sv_client, PRINT_HIGH, "File size differs from server.\n"
			"Please delete the corresponding .tmp file from your system.\n" );
		SV_DownloadFailed();
		return;
	}

	if( offset == downloadsize ) {
		Com_DPrintf( "Refusing download, %s already has %s (%d bytes)\n",
			sv_client->name, name, offset );
		MSG_WriteByte( svc_download );
		MSG_WriteShort( 0 );
		MSG_WriteByte( 100 );
		SV_ClientAddMessage( sv_client, MSG_RELIABLE|MSG_CLEAR );
		return;
	}

	sv_client->downloadsize = FS_LoadFileEx( filename, ( void ** )&sv_client->download, FS_FLAG_RAW );
	sv_client->downloadcount = offset;

	Com_DPrintf( "Downloading %s to %s\n", name, sv_client->name );

	SV_NextDownload_f();
	
}

static void SV_StopDownload_f( void ) {
	int size, percent;

	if( !sv_client->download ) {
		return;
	}

	size = sv_client->downloadsize;
	if( !size ) {
		percent = 0;
	} else {
		percent = sv_client->downloadcount*100/size;
	}

	MSG_WriteByte( svc_download );
	MSG_WriteShort( -1 );
	MSG_WriteByte( percent );
	SV_ClientAddMessage( sv_client, MSG_RELIABLE|MSG_CLEAR );

	Com_DPrintf( "Download for %s stopped by user request\n", sv_client->name );
	FS_FreeFile( sv_client->download );
	sv_client->download = NULL;
}

//============================================================================


/*
=================
SV_Disconnect_f

The client is going to disconnect, so remove the connection immediately
=================
*/
static void SV_Disconnect_f( void ) {
	SV_DropClient( sv_client, NULL );
}


/*
==================
SV_ShowServerinfo_f

Dumps the serverinfo info string
==================
*/
static void SV_ShowServerinfo_f( void ) {
    char serverinfo[MAX_INFO_STRING];

    Cvar_BitInfo( serverinfo, CVAR_SERVERINFO );

	Com_BeginRedirect( RD_CLIENT, sv_outputbuf, SV_OUTPUTBUF_LENGTH, SV_FlushRedirect );
	Info_Print( serverinfo );
	Com_EndRedirect();
}


void SV_Nextserver( void ) {
    client_t *client;
	char	*v;

	//ZOID, ss_pic can be nextserver'd in coop mode
	if (sv.state == ss_game)
		return;		// can't nextserver while playing a normal game

    FOR_EACH_CLIENT( client ) {
        SV_ClientReset( client );
    }

	v = sv_nextserver->string;
	if (!v[0])
		Cbuf_AddText ("killserver\n");
	else
	{
		Cbuf_AddText (v);
		Cbuf_AddText ("\n");
	}
	Cvar_Set ("nextserver","");
}

/*
==================
SV_Nextserver_f

A cinematic has completed or been aborted by a client, so move
to the next server.
==================
*/
static void SV_Nextserver_f( void ) {
	if( !NET_IsLocalAddress( &sv_client->netchan->remote_address ) ) {
		Com_DPrintf( "Nextserver() from remote client, from %s\n",
            sv_client->name );
		return;
	}
	if ( atoi( Cmd_Argv( 1 ) ) != sv.spawncount ) {
		Com_DPrintf( "Nextserver() from wrong level, from %s\n",
            sv_client->name );
		return;		// leftover from last server
	}

	Com_DPrintf( "Nextserver() from %s\n", sv_client->name );

	SV_Nextserver ();
}

static void SV_NoGameData_f( void ) {
	sv_client->nodata ^= 1;
}

static void SV_CvarResult_f( void ) {
	char *c, *v;

	c = Cmd_Argv( 1 );
	if( !strcmp( c, "version" ) ) {
		if( !sv_client->versionString ) {
            v = Cmd_RawArgsFrom( 2 );
            if( dedicated->integer ) {
                Com_Printf( "%s[%s]: %s\n", sv_client->name,
                    NET_AdrToString( &sv_client->netchan->remote_address ), v );
            }
            sv_client->versionString = SV_CopyString( v );
        }
	} else if( !strcmp( c, "connect" ) ) {
        if( sv_client->reconnect_var[0] ) {
            v = Cmd_Argv( 2 );
            if( !strcmp( v, sv_client->reconnect_val ) ) {
                sv_client->reconnect_done = qtrue;
            }
        }
    }
}

#if USE_ANTICHEAT & 2

static void SV_AC_List_f( void ) {
	Com_BeginRedirect( RD_CLIENT, sv_outputbuf, SV_OUTPUTBUF_LENGTH, SV_FlushRedirect );
	AC_List_f();
	Com_EndRedirect();
}

static void SV_AC_Info_f( void ) {
	Com_BeginRedirect( RD_CLIENT, sv_outputbuf, SV_OUTPUTBUF_LENGTH, SV_FlushRedirect );
	AC_Info_f();
	Com_EndRedirect();
}

#endif

static ucmd_t ucmds[] = {
	// auto issued
	{ "new", SV_New_f },
	{ "begin", SV_Begin_f },
	{ "baselines", NULL },
	{ "configstrings", NULL },

	{ "nextserver", SV_Nextserver_f },

	{ "disconnect", SV_Disconnect_f },

	// issued by hand at client consoles	
	{ "info", SV_ShowServerinfo_f },

	{ "download", SV_BeginDownload_f },
	{ "nextdl", SV_NextDownload_f },
	{ "stopdl", SV_StopDownload_f },

	{ "\177c", SV_CvarResult_f },
	{ "nogamedata", SV_NoGameData_f },
#if USE_ANTICHEAT & 2
	{ "aclist", SV_AC_List_f },
	{ "acinfo", SV_AC_Info_f },
#endif

	{ NULL, NULL }
};

/*
==================
SV_ExecuteUserCommand
==================
*/
static void SV_ExecuteUserCommand( const char *s ) {
	const ucmd_t	*u;
	char *c;
	
	Cmd_TokenizeString( s, qfalse );
	sv_player = sv_client->edict;

	c = Cmd_Argv( 0 );
	if( !c[0] ) {
		return;
	}

    if( ( u = Com_Find( ucmds, c ) ) != NULL ) {
        if( u->func ) {
            u->func();
        }
        return;
    }
    if( sv.state == ss_game || sv.state == ss_broadcast ) {
        ge->ClientCommand( sv_player );
    }
}

/*
===========================================================================

USER CMD EXECUTION

===========================================================================
*/

static int  net_drop;


/*
==================
SV_ClientThink
==================
*/
void SV_ClientThink( client_t *cl, usercmd_t *cmd ) {
	cl->commandMsec -= cmd->msec;

	if( cl->commandMsec < 0 && sv_enforcetime->integer ) {
		Com_DPrintf( "commandMsec underflow from %s: %d\n",
			cl->name, cl->commandMsec );
		return;
	}

	ge->ClientThink( cl->edict, cmd );
}

/*
==================
SV_OldClientExecuteMove
==================
*/
static void SV_OldClientExecuteMove( void ) {
	usercmd_t	oldest, oldcmd, newcmd;
	int		lastframe;

	if( sv_client->protocol == PROTOCOL_VERSION_DEFAULT ) {
		MSG_ReadByte();	// skip over checksum
	}
	
    lastframe = MSG_ReadLong();
	if( lastframe != sv_client->lastframe ) {
		sv_client->lastframe = lastframe;
		if( lastframe > 0 ) {
			sv_client->frame_latency[lastframe & LATENCY_MASK] = 
				svs.realtime - sv_client->frames[lastframe & UPDATE_MASK].senttime;
		}
	}

	if( sv_client->protocol == PROTOCOL_VERSION_R1Q2 &&
        sv_client->version >= PROTOCOL_VERSION_R1Q2_UCMD ) 
    {
        MSG_ReadDeltaUsercmd_Hacked( NULL, &oldest );
        MSG_ReadDeltaUsercmd_Hacked( &oldest, &oldcmd );
        MSG_ReadDeltaUsercmd_Hacked( &oldcmd, &newcmd );
    } else {
        MSG_ReadDeltaUsercmd( NULL, &oldest );
        MSG_ReadDeltaUsercmd( &oldest, &oldcmd );
        MSG_ReadDeltaUsercmd( &oldcmd, &newcmd );
    }

	if( sv_client->state != cs_spawned ) {
		sv_client->lastframe = -1;
		return;
	}

	if( net_drop > 2 ) {
        sv_client->frameflags |= FF_CLIENTPRED;
//		Com_DPrintf( "%s: net_drop %i\n", sv_client->name, net_drop );
	} 

	if( net_drop < 20 ) {
		while( net_drop > 2 ) {
			SV_ClientThink( sv_client, &sv_client->lastcmd );
			net_drop--;
		}
		if( net_drop > 1 )
			SV_ClientThink( sv_client, &oldest );

		if( net_drop > 0 )
			SV_ClientThink( sv_client, &oldcmd );

	}
	SV_ClientThink( sv_client, &newcmd );
	
	sv_client->lastcmd = newcmd;
}



/*
==================
SV_NewClientExecuteMove
==================
*/
static void SV_NewClientExecuteMove( int c ) {
	usercmd_t cmds[MAX_PACKET_FRAMES][MAX_PACKET_USERCMDS];
	usercmd_t *lastcmd, *cmd;
	int		lastframe;
	int		numCmds[MAX_PACKET_FRAMES], numDups;
	int		i, j, lightlevel;

	numDups = c >> SVCMD_BITS;
	c &= SVCMD_MASK;
	if( c == clc_move_nodelta ) {
		lastframe = -1;
	} else {
		lastframe = MSG_ReadLong();
	}

	if( lastframe != sv_client->lastframe ) {
		sv_client->lastframe = lastframe;
		if( lastframe != -1 ) {
			sv_client->frame_latency[lastframe & LATENCY_MASK] = 
				svs.realtime - sv_client->frames[lastframe & UPDATE_MASK].senttime;
		}
	}

	if( numDups > MAX_PACKET_FRAMES - 1 ) {
		SV_DropClient( sv_client, "too many frames in packet" );
		return;
	}

	lightlevel = MSG_ReadByte();

	/* read them all */
	lastcmd = NULL;
	for( i = 0; i <= numDups; i++ ) {
		numCmds[i] = MSG_ReadBits( 5 );
		if( numCmds[i] == -1 ) {
			SV_DropClient( sv_client, "read past end of message" );
			return;
		}
		if( numCmds[i] >= MAX_PACKET_USERCMDS ) {
			SV_DropClient( sv_client, "too many usercmds in frame" );
			return;
		}
		for( j = 0; j < numCmds[i]; j++ ) {
			if( msg_read.readcount > msg_read.cursize ) {
				SV_DropClient( sv_client, "read past end of message" );
				return;
			}
			cmd = &cmds[i][j];
			MSG_ReadDeltaUsercmd_Enhanced( lastcmd, cmd );
			cmd->lightlevel = lightlevel;
			lastcmd = cmd;
		}
	}
	if( sv_client->state != cs_spawned ) {
		sv_client->lastframe = -1;
		return;
	}

	if( q_unlikely( !lastcmd ) ) {
		return; // should never happen
	}

	if( net_drop > numDups ) {
        sv_client->frameflags |= FF_CLIENTPRED;
//		Com_DPrintf( "%s: net_drop %i\n", sv_client->name, net_drop );
	} 

	if( net_drop < 20 ) {
		/* run lastcmd multiple times if no backups available */
		while( net_drop > numDups ) {
			SV_ClientThink( sv_client, &sv_client->lastcmd );
			net_drop--;
		}

		/* run backup cmds, if any */
		while( net_drop > 0 ) {
			i = numDups - net_drop;
			for( j = 0; j < numCmds[i]; j++ ) {
				SV_ClientThink( sv_client, &cmds[i][j] );
			}
			net_drop--;
		}

	}

	/* run new cmds */
	for( j = 0; j < numCmds[numDups]; j++ ) {
		SV_ClientThink( sv_client, &cmds[numDups][j] );
	}
	
	sv_client->lastcmd = *lastcmd;
}

/*
===================
SV_ExecuteClientMessage

The current net_message is parsed for the given client
===================
*/
void SV_ExecuteClientMessage( client_t *client ) {
	int		c;
	char	*s;
	qboolean	move_issued;
	int		stringCmdCount;
	int		userinfoUpdateCount;

	sv_client = client;
	sv_player = sv_client->edict;

	// only allow one move command
	move_issued = qfalse;
	stringCmdCount = 0;
	userinfoUpdateCount = 0;

	net_drop = client->netchan->dropped;
    if( net_drop ) {
        client->frameflags |= FF_CLIENTDROP;
    }

	while( 1 ) {
		if( msg_read.readcount > msg_read.cursize ) {
			SV_DropClient( client, "read past end of message" );
			break;
		}	

		c = MSG_ReadByte();
		if( c == -1 )
			break;
		
		switch( c & SVCMD_MASK ) {
		default:
		badbyte:
			SV_DropClient( client, "unknown command byte" );
			break;
						
		case clc_nop:
			break;

		case clc_userinfo:
			s = MSG_ReadString();

			// malicious users may try sending too many userinfo updates
			if( userinfoUpdateCount == MAX_PACKET_USERINFOS ) {
				Com_DPrintf( "Too many userinfos from %s\n", client->name );
				break;
			}

			SV_UpdateUserinfo( s );
			userinfoUpdateCount++;
			break;

		case clc_move:
			if( move_issued ) {
				SV_DropClient( client, "multiple clc_move commands in packet" );
				break;		// someone is trying to cheat...
			}

			move_issued = qtrue;

			SV_OldClientExecuteMove();
			break;

		case clc_stringcmd:	
			s = MSG_ReadString();

			Com_DPrintf( "ClientCommand( %s ): %s\n", client->name, s );

			// malicious users may try using too many string commands
			if( stringCmdCount == MAX_PACKET_STRINGCMDS ) {
				Com_DPrintf( "Too many stringcmds from %s\n", client->name );
				break;
			}
			SV_ExecuteUserCommand( s );
			stringCmdCount++;
			break;

		// r1q2 specific operations
		case clc_setting: {
				uint16		idx, value;

				if( client->protocol < PROTOCOL_VERSION_R1Q2 ) {
					goto badbyte;
				}

				idx = MSG_ReadShort();
				value = MSG_ReadShort();
				if( idx < CLS_MAX ) {
					client->settings[idx] = value;
				}
			}
			break;


		// q2pro specific operations
		case clc_move_nodelta:
		case clc_move_batched:
			if( client->protocol != PROTOCOL_VERSION_Q2PRO ) {
				goto badbyte;
			}

			if( move_issued ) {
				SV_DropClient( client, "multiple clc_move commands in packet" );
				break; // someone is trying to cheat...
			}

			move_issued = qtrue;
			SV_NewClientExecuteMove( c );
			break;

		case clc_userinfo_delta: {
				char *key, *value;
				char buffer[MAX_INFO_STRING];
				
				if( client->protocol != PROTOCOL_VERSION_Q2PRO ) {
					goto badbyte;
				}

				key = MSG_ReadString();
				value = MSG_ReadString();

				// malicious users may try sending too many userinfo updates
				if( userinfoUpdateCount == MAX_PACKET_USERINFOS ) {
				    Com_DPrintf( "Too many userinfos from %s\n", client->name );
					break;
				}
				userinfoUpdateCount++;

				strcpy( buffer, client->userinfo );
				if( !Info_SetValueForKey( buffer, key, value ) ) {
					SV_ClientPrintf( client, PRINT_HIGH,
						"Malformed userinfo update supplied. Ignored.\n" );
					break;
				}

				SV_UpdateUserinfo( buffer );
			}
			break;
		}

		if( client->state < cs_assigned ) {
			break;	// disconnect command
		}
	}

	sv_client = NULL;
	sv_player = NULL;
}



