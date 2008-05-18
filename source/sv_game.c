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
// sv_game.c -- interface to the game dll

#include "sv_local.h"

game_export_t	*ge;

/*
================
PF_FindIndex

================
*/
static int SV_FindIndex( const char *name, int start, int max ) {
	char *string;
	int		i;
	
	if( !name || !name[0] )
		return 0;

	for( i = 1; i < max; i++ ) {
		string = sv.configstrings[start + i];
		if( !string[0] ) {
			break;
		}
		if( !strcmp( string, name ) ) {
			return i;
		}
	}

	if( i == max )
		Com_Error( ERR_DROP, "PF_FindIndex: overflow" );

	PF_Configstring( i + start, name );

	return i;
}

static int PF_ModelIndex (const char *name) {
	return SV_FindIndex (name, CS_MODELS, MAX_MODELS);
}

static int PF_SoundIndex (const char *name) {
	return SV_FindIndex (name, CS_SOUNDS, MAX_SOUNDS);
}

static int PF_ImageIndex (const char *name) {
	return SV_FindIndex (name, CS_IMAGES, MAX_IMAGES);
}

/*
===============
PF_Unicast

Sends the contents of the mutlicast buffer to a single client.
Archived in MVD stream.
===============
*/
static void PF_Unicast( edict_t *ent, qboolean reliable ) {
	client_t	*client;
	int flags, clientNum;
    svc_ops_t   op;
    sizebuf_t *buf;

	if( !ent ) {
        goto clear;
    }

	clientNum = NUM_FOR_EDICT( ent ) - 1;
	if( clientNum < 0 || clientNum >= sv_maxclients->integer ) {
        Com_WPrintf( "PF_Unicast to a non-client %d\n", clientNum );
        goto clear;
    }

	client = svs.udp_client_pool + clientNum;
    if( client->state == cs_free ) {
        Com_WPrintf( "PF_Unicast to a free client %d\n", clientNum );
        goto clear;
    }

	if( reliable ) {
		flags = MSG_RELIABLE;
        op = mvd_unicast_r;
        buf = &sv.mvd.message;
	} else {
        flags = 0;
        op = mvd_unicast;
        buf = &sv.mvd.datagram;
    }

    // HACK: fix anti-kicking exploit
    if( msg_write.data[0] == svc_disconnect ) {
        SV_ClientAddMessage( client, flags );
        client->flags |= CF_DROP;
        goto clear;
    }

	if( client == svs.mvd.dummy ) {
		if( msg_write.data[0] == svc_stufftext &&
            memcmp( msg_write.data + 1, "play ", 5 ) )
        {
            // let MVD client process this internally
			SV_ClientAddMessage( client, flags );
		} else if( sv.mvd.paused < PAUSED_FRAMES ) {
            // send this to all observers
            if( msg_write.data[0] == svc_layout ) {
                sv.mvd.layout_time = svs.realtime;
            }
            SV_MvdUnicast( buf, clientNum, op );
        }
	} else {
        SV_ClientAddMessage( client, flags );
        if( svs.mvd.dummy && sv.mvd.paused < PAUSED_FRAMES &&
            SV_MvdPlayerIsActive( ent ) )
        {
            if( msg_write.data[0] != svc_layout &&
                ( msg_write.data[0] != svc_stufftext ||
                  !memcmp( msg_write.data + 1, "play ", 5 ) ) )
            {
                SV_MvdUnicast( buf, clientNum, op );
            }
        }
    }

clear:
    SZ_Clear( &msg_write );
}

/*
=================
PF_bprintf

Sends text to all active clients.
Archived in MVD stream.
=================
*/
static void PF_bprintf( int level, const char *fmt, ... ) {
	va_list		argptr;
	char		string[MAX_STRING_CHARS];
	client_t	*client;
    size_t      len;
	int			i;

	va_start( argptr, fmt );
	len = Q_vsnprintf( string, sizeof( string ), fmt, argptr );
	va_end( argptr );

    if( svs.mvd.dummy && sv.mvd.paused < PAUSED_FRAMES ) {
    	SV_MvdBroadcastPrint( level, string );
    }

	MSG_WriteByte( svc_print );
	MSG_WriteByte( level );
	MSG_WriteData( string, len + 1 );

	// echo to console
	if( dedicated->integer ) {
		// mask off high bits
		for( i = 0; i < len; i++ )
			string[i] &= 127;
		Com_Printf( "%s", string );
	}

    FOR_EACH_CLIENT( client ) {
		if( client->state != cs_spawned )
			continue;
		if( level >= client->messagelevel ) {
		    SV_ClientAddMessage( client, MSG_RELIABLE );
        }
	}

	SZ_Clear( &msg_write );
}


/*
===============
PF_dprintf

Debug print to server console.
===============
*/
static void PF_dprintf( const char *fmt, ... ) {
	char		msg[MAXPRINTMSG];
	va_list		argptr;
	
	va_start( argptr, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	Com_Printf( "%s", msg );
}


/*
===============
PF_cprintf

Print to a single client if the level passes.
Archived in MVD stream.
===============
*/
static void PF_cprintf( edict_t *ent, int level, const char *fmt, ... ) {
	char		msg[MAX_STRING_CHARS];
	va_list		argptr;
	int			clientNum;
	size_t		len;
	client_t	*client;

	va_start( argptr, fmt );
	len = Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	if( !ent ) {
		Com_Printf( "%s", msg );
		return;
	}

	clientNum = NUM_FOR_EDICT( ent ) - 1;
	if( clientNum < 0 || clientNum >= sv_maxclients->integer ) {
		Com_Error( ERR_DROP, "PF_cprintf to a non-client %d", clientNum );
    }

	client = svs.udp_client_pool + clientNum;
    if( client->state == cs_free ) {
        Com_Error( ERR_DROP, "PF_cprintf to a free client %d", clientNum );
    }

	MSG_WriteByte( svc_print );
	MSG_WriteByte( level );
	MSG_WriteData( msg, len + 1 );

    if( level >= client->messagelevel ) {
        SV_ClientAddMessage( client, MSG_RELIABLE );
    }

    if( svs.mvd.dummy && sv.mvd.paused < PAUSED_FRAMES &&
        ( client == svs.mvd.dummy || SV_MvdPlayerIsActive( ent ) ) )
    {
        SV_MvdUnicast( &sv.mvd.message, clientNum, mvd_unicast_r );
    }

    SZ_Clear( &msg_write );
}


/*
===============
PF_centerprintf

Centerprint to a single client.
Archived in MVD stream.
===============
*/
static void PF_centerprintf( edict_t *ent, const char *fmt, ... ) {
	char		msg[MAX_STRING_CHARS];
	va_list		argptr;
	int			n;
	size_t		len;

    if( !ent ) {
        return;
    }
	
	n = NUM_FOR_EDICT( ent );
	if( n < 1 || n > sv_maxclients->integer ) {
        Com_WPrintf( "PF_centerprintf to a non-client\n" );
		return;
    }

	va_start( argptr, fmt );
	len = Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	MSG_WriteByte( svc_centerprint );
	MSG_WriteData( msg, len + 1 );

	PF_Unicast( ent, qtrue );
}


/*
===============
PF_error

Abort the server with a game error
===============
*/
static q_noreturn void PF_error (const char *fmt, ...) {
	char		msg[MAXPRINTMSG];
	va_list		argptr;
	
	va_start (argptr,fmt);
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end (argptr);

	Com_Error (ERR_DROP, "Game Error: %s", msg);
}


/*
=================
PF_setmodel

Also sets mins and maxs for inline bmodels
=================
*/
static void PF_setmodel (edict_t *ent, const char *name) {
	int		i;
	cmodel_t	*mod;

	if (!name)
		Com_Error (ERR_DROP, "PF_setmodel: NULL");

	i = PF_ModelIndex (name);
		
	ent->s.modelindex = i;

// if it is an inline model, get the size information for it
	if( name[0] == '*' ) {
		mod = CM_InlineModel (&sv.cm, name);
		VectorCopy (mod->mins, ent->mins);
		VectorCopy (mod->maxs, ent->maxs);
		PF_LinkEdict (ent);
	}

}

/*
===============
PF_Configstring

If game is actively running, broadcasts configstring change.
Archived in MVD stream.
===============
*/
void PF_Configstring( int index, const char *val ) {
	size_t length, maxlength;
	client_t *client;

	if( index < 0 || index >= MAX_CONFIGSTRINGS )
		Com_Error( ERR_DROP, "%s: bad index: %d\n", __func__, index );

	if( !val )
		val = "";

	length = strlen( val );
	maxlength = sizeof( sv.configstrings ) - MAX_QPATH * index;
	if( length >= maxlength ) {
		Com_Error( ERR_DROP, "%s: index %d overflowed: %"PRIz" > %"PRIz"\n", __func__,
            index, length, maxlength - 1 );
	}

	if( !strcmp( sv.configstrings[index], val ) ) {
		return;
	}

	// change the string in sv
	strcpy( sv.configstrings[index], val );
	
	if( sv.state == ss_loading ) {
		return;
	}

    if( svs.mvd.dummy ) {
        SV_MvdConfigstring( index, val );
    }

	// send the update to everyone
	MSG_WriteByte( svc_configstring );
	MSG_WriteShort( index );
	MSG_WriteData( val, length + 1 );

    FOR_EACH_CLIENT( client ) {
		if( client->state < cs_primed ) {
			continue;
		}
		SV_ClientAddMessage( client, MSG_RELIABLE );
	}

	SZ_Clear( &msg_write );
}

static void PF_WriteFloat( float f ) {
	Com_Error( ERR_DROP, "PF_WriteFloat not implemented" );
}

/*
=================
PF_inPVS

Also checks portalareas so that doors block sight
=================
*/
static qboolean PF_inPVS (vec3_t p1, vec3_t p2) {
	cleaf_t	*leaf;
	int		cluster;
	int		area1, area2;
	byte	*mask;

	if( !sv.cm.cache ) {
		Com_Error( ERR_DROP, "PF_inPVS: no map loaded" );
	}

	leaf = CM_PointLeaf (&sv.cm, p1);
	cluster = CM_LeafCluster (leaf);
	area1 = CM_LeafArea (leaf);
	mask = CM_ClusterPVS (&sv.cm, cluster);

	leaf = CM_PointLeaf (&sv.cm, p2);
	cluster = CM_LeafCluster (leaf);
	area2 = CM_LeafArea (leaf);
	if ( !Q_IsBitSet( mask, cluster ) )
		return qfalse;
	if (!CM_AreasConnected (&sv.cm, area1, area2))
		return qfalse;		// a door blocks sight
	return qtrue;
}


/*
=================
PF_inPHS

Also checks portalareas so that doors block sound
=================
*/
static qboolean PF_inPHS (vec3_t p1, vec3_t p2) {
	cleaf_t	*leaf;
	int		cluster;
	int		area1, area2;
	byte	*mask;

	if( !sv.cm.cache ) {
		Com_Error( ERR_DROP, "PF_inPHS: no map loaded" );
	}

	leaf = CM_PointLeaf (&sv.cm, p1);
	cluster = CM_LeafCluster (leaf);
	area1 = CM_LeafArea (leaf);
	mask = CM_ClusterPHS (&sv.cm, cluster);

	leaf = CM_PointLeaf (&sv.cm, p2);
	cluster = CM_LeafCluster (leaf);
	area2 = CM_LeafArea (leaf);
	if( !Q_IsBitSet( mask, cluster ) )
		return qfalse;		// more than one bounce away
	if (!CM_AreasConnected (&sv.cm, area1, area2))
		return qfalse;		// a door blocks hearing

	return qtrue;
}

/*  
==================
PF_StartSound

Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.

If channel & 8, the sound will be sent to everyone, not just
things in the PHS.

FIXME: if entity isn't in PHS, they must be forced to be sent or
have the origin explicitly sent.

Channel 0 is an auto-allocate channel, the others override anything
already running on that entity/channel pair.

An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.  (max 4 attenuation)

Timeofs can range from 0.0 to 0.1 to cause sounds to be started
later in the frame than they normally would.

If origin is NULL, the origin is determined from the entity origin
or the midpoint of the entity box for bmodels.
==================
*/  
static void PF_StartSound( edict_t *edict, int channel,
					    int soundindex, float volume,
					    float attenuation, float timeofs )
{       
	int			sendchan;
    int			flags;
	int			ent;
	vec3_t		origin;
	client_t	*client;
	byte		*mask;
	cleaf_t		*leaf;
	int			area, cluster;
    player_state_t  *ps;
    sound_packet_t  *msg;

	if( !edict )
		return;

	if( volume < 0 || volume > 1.0 )
		Com_Error( ERR_DROP, "%s: volume = %f", __func__, volume );
	if( attenuation < 0 || attenuation > 4 )
		Com_Error( ERR_DROP, "%s: attenuation = %f", __func__, attenuation );
	if( timeofs < 0 || timeofs > 0.255 )
		Com_Error( ERR_DROP, "%s: timeofs = %f", __func__, timeofs );
    if( soundindex < 0 || soundindex >= MAX_SOUNDS )
		Com_Error( ERR_DROP, "%s: soundindex = %d", __func__, soundindex );

	ent = NUM_FOR_EDICT( edict );

	sendchan = ( ent << 3 ) | ( channel & 7 );

	// always send the entity number for channel overrides
	flags = SND_ENT;
	if( volume != DEFAULT_SOUND_PACKET_VOLUME )
		flags |= SND_VOLUME;
	if( attenuation != DEFAULT_SOUND_PACKET_ATTENUATION )
		flags |= SND_ATTENUATION;
	if( timeofs )
		flags |= SND_OFFSET;

    // if the sound doesn't attenuate,send it to everyone
    // (global radio chatter, voiceovers, etc)
    if( attenuation == ATTN_NONE ) {
        channel |= CHAN_NO_PHS_ADD;
    }

    FOR_EACH_CLIENT( client ) {
		// do not send sounds to connecting clients
		if( client->state != cs_spawned || client->download || ( client->flags & CF_NODATA ) ) {
			continue; 
		}

        // PHS cull this sound
        if( ( channel & CHAN_NO_PHS_ADD ) == 0 ) {
            // get client viewpos
            ps = &client->edict->client->ps;
            VectorMA( ps->viewoffset, 0.125f, ps->pmove.origin, origin );
            leaf = CM_PointLeaf( &sv.cm, origin );
            area = CM_LeafArea( leaf );
            if( !CM_AreasConnected( &sv.cm, area, edict->areanum ) ) {
                // doors can legally straddle two areas, so
                // we may need to check another one
                if( !edict->areanum2 || !CM_AreasConnected( &sv.cm, area, edict->areanum2 ) ) {
                    continue;		// blocked by a door
                }
            }
            cluster = CM_LeafCluster( leaf );
            mask = CM_ClusterPHS( &sv.cm, cluster );
            if( !SV_EdictPV( &sv.cm, edict, mask ) ) {
                continue; // not in PHS
            }
        }

        // reliable sounds will always have position explicitly set,
        // as no one gurantees reliables to be delivered in time
        // why should this happen anyway?
        if( channel & CHAN_RELIABLE ) {
            // use the entity origin unless it is a bmodel
            if( edict->solid == SOLID_BSP ) {
                VectorAvg( edict->mins, edict->maxs, origin );
                VectorAdd( edict->s.origin, origin, origin );
            } else {
                VectorCopy( edict->s.origin, origin );
            }

            MSG_WriteByte( svc_sound );
            MSG_WriteByte( flags | SND_POS );
            MSG_WriteByte( soundindex );

            if( flags & SND_VOLUME )
                MSG_WriteByte( volume * 255 );
            if( flags & SND_ATTENUATION )
                MSG_WriteByte( attenuation * 64 );
            if( flags & SND_OFFSET )
                MSG_WriteByte( timeofs * 1000 );

            MSG_WriteShort( sendchan );
            MSG_WritePos( origin );

            SV_ClientAddMessage( client, MSG_RELIABLE|MSG_CLEAR );
            continue;
        }

        if( LIST_EMPTY( &client->msg_free ) ) {
            Com_WPrintf( "%s: %s: out of message slots\n",
                __func__, client->name );
            continue;
        }

        msg = LIST_FIRST( sound_packet_t, &client->msg_free, entry );

        msg->cursize = 0; // !!! make sure this does not get Z_Free'ed
        msg->flags = flags;
        msg->index = soundindex;
        msg->volume = volume * 255;
        msg->attenuation = attenuation * 64;
        msg->timeofs = timeofs * 1000;
        msg->sendchan = sendchan;

        List_Remove( &msg->entry );
        List_Append( &client->msg_sound, &msg->entry );
    }

    if( svs.mvd.dummy && sv.mvd.paused < PAUSED_FRAMES ) {
        int extrabits = 0;

        if( channel & CHAN_NO_PHS_ADD ) {
            extrabits |= 1 << SVCMD_BITS;
        }
	    if( channel & CHAN_RELIABLE ) {
            extrabits |= 2 << SVCMD_BITS;
        }

        SZ_WriteByte( &sv.mvd.datagram, mvd_sound | extrabits );
        SZ_WriteByte( &sv.mvd.datagram, flags );
        SZ_WriteByte( &sv.mvd.datagram, soundindex );

        if( flags & SND_VOLUME )
            SZ_WriteByte( &sv.mvd.datagram, volume * 255 );
        if( flags & SND_ATTENUATION )
            SZ_WriteByte( &sv.mvd.datagram, attenuation * 64 );
        if( flags & SND_OFFSET )
            SZ_WriteByte( &sv.mvd.datagram, timeofs * 1000 );

        SZ_WriteShort( &sv.mvd.datagram, sendchan );
    }
}

static void PF_PositionedSound( vec3_t origin, edict_t *entity, int channel,
					            int soundindex, float volume,
					            float attenuation, float timeofs )
{       
	int			sendchan;
    int			flags;
	int			ent;

	if( !origin )
		Com_Error( ERR_DROP, "%s: NULL origin", __func__ );
	if( volume < 0 || volume > 1.0 )
		Com_Error( ERR_DROP, "%s: volume = %f", __func__, volume );
	if( attenuation < 0 || attenuation > 4 )
		Com_Error( ERR_DROP, "%s: attenuation = %f", __func__, attenuation );
	if( timeofs < 0 || timeofs > 0.255 )
		Com_Error( ERR_DROP, "%s: timeofs = %f", __func__, timeofs );
    if( soundindex < 0 || soundindex >= MAX_SOUNDS )
		Com_Error( ERR_DROP, "%s: soundindex = %d", __func__, soundindex );

	ent = NUM_FOR_EDICT( entity );

	sendchan = ( ent << 3 ) | ( channel & 7 );

	// always send the entity number for channel overrides
	flags = SND_ENT|SND_POS;
	if( volume != DEFAULT_SOUND_PACKET_VOLUME )
		flags |= SND_VOLUME;
	if( attenuation != DEFAULT_SOUND_PACKET_ATTENUATION )
		flags |= SND_ATTENUATION;
	if( timeofs )
		flags |= SND_OFFSET;

    MSG_WriteByte( svc_sound );
    MSG_WriteByte( flags );
    MSG_WriteByte( soundindex );

    if( flags & SND_VOLUME )
        MSG_WriteByte( volume * 255 );
    if( flags & SND_ATTENUATION )
        MSG_WriteByte( attenuation * 64 );
    if( flags & SND_OFFSET )
        MSG_WriteByte( timeofs * 1000 );

    MSG_WriteShort( sendchan );
    MSG_WritePos( origin );

	// if the sound doesn't attenuate,send it to everyone
	// (global radio chatter, voiceovers, etc)
	if( attenuation == ATTN_NONE || ( channel & CHAN_NO_PHS_ADD ) ) {
	    if( channel & CHAN_RELIABLE ) {
            SV_Multicast( NULL, MULTICAST_ALL_R );
        } else {
            SV_Multicast( NULL, MULTICAST_ALL );
        }
    } else {
	    if( channel & CHAN_RELIABLE ) {
            SV_Multicast( origin, MULTICAST_PHS_R );
        } else {
            SV_Multicast( origin, MULTICAST_PHS );
        }
    }
}


void PF_Pmove( pmove_t *pm ) {
	if( sv_client ) {
		Pmove( pm, &sv_client->pmp );
	} else {
		Pmove( pm, &sv_pmp );
	}
}

static cvar_t *PF_cvar( const char *name, const char *value, int flags ) {
	if( flags & CVAR_EXTENDED_MASK ) {
		Com_WPrintf( "Game DLL attemped to set extended flags on variable '%s', cleared.\n", name );
		flags &= ~CVAR_EXTENDED_MASK;
	}

	return Cvar_Get( name, value, flags | CVAR_GAME );
}

static void PF_AddCommandString( const char *string ) {
	Cbuf_AddTextEx( &cmd_buffer, string );
}

static void PF_SetAreaPortalState( int portalnum, qboolean open ) {
	if( !sv.cm.cache ) {
		Com_Error( ERR_DROP, "%s: no map loaded", __func__ );
	}
	CM_SetAreaPortalState( &sv.cm, portalnum, open );
}

static qboolean PF_AreasConnected( int area1, int area2 ) {
	if( !sv.cm.cache ) {
		Com_Error( ERR_DROP, "%s: no map loaded", __func__ );
	}
	return CM_AreasConnected( &sv.cm, area1, area2 );
}

//==============================================

static void *game_library;

/*
===============
SV_ShutdownGameProgs

Called when either the entire server is being killed, or
it is changing to a different game directory.
===============
*/
void SV_ShutdownGameProgs (void) {
	if( ge ) {
	    ge->Shutdown();
	    ge = NULL;
    }
    if( game_library ) {
    	Sys_FreeLibrary( game_library );
	    game_library = NULL;
    }
    if( g_features ) {
        Cvar_SetByVar( g_features, NULL, CVAR_SET_DIRECT );
        g_features->flags = CVAR_VOLATILE;
    }
}

/*
===============
SV_InitGameProgs

Init the game subsystem for a new map
===============
*/
void SV_InitGameProgs ( void ) {
	game_import_t	import;
	char	path[MAX_OSPATH];
    game_export_t	*(*entry)( game_import_t * ) = NULL;

	// unload anything we have now
	SV_ShutdownGameProgs ();

#ifdef _WIN32
	// FIXME: check current debug directory first for
    // e.g. running legacy stuff like Q2Admin
	Q_concat( path, sizeof( path ), Sys_GetCurrentDirectory(),
        PATH_SEP_STRING "release" PATH_SEP_STRING GAMELIB, NULL );
    entry = Sys_LoadLibrary( path, "GetGameAPI", &game_library );
	if( !entry )
#endif
    {
        // try gamedir first
        if( fs_game->string[0] ) {
            Q_concat( path, sizeof( path ), sys_libdir->string,
                PATH_SEP_STRING, fs_game->string, PATH_SEP_STRING GAMELIB, NULL );
            entry = Sys_LoadLibrary( path, "GetGameAPI", &game_library );
        }
        if( !entry ) {
            // then try baseq2
            Q_concat( path, sizeof( path ), sys_libdir->string,
                PATH_SEP_STRING BASEGAME PATH_SEP_STRING GAMELIB, NULL );
            entry = Sys_LoadLibrary( path, "GetGameAPI", &game_library );
            if( !entry ) {
                // then try to fall back to refdir
                Q_concat( path, sizeof( path ), sys_refdir->string,
                    PATH_SEP_STRING GAMELIB, NULL );
                entry = Sys_LoadLibrary( path, "GetGameAPI", &game_library );
                if( !entry ) {
                    Com_Error( ERR_DROP, "Failed to load game DLL" );
                }
            }
        }
    }

	// load a new game dll
	import.multicast = SV_Multicast;
	import.unicast = PF_Unicast;
	import.bprintf = PF_bprintf;
	import.dprintf = PF_dprintf;
	import.cprintf = PF_cprintf;
	import.centerprintf = PF_centerprintf;
	import.error = PF_error;

	import.linkentity = PF_LinkEdict;
	import.unlinkentity = PF_UnlinkEdict;
	import.BoxEdicts = SV_AreaEdicts;
#ifdef _WIN32
#ifdef __GNUC__
	import.trace = ( sv_trace_t )SV_Trace_Old;
#else
	import.trace = SV_Trace;
#endif
#else /* _WIN32 */
	if( sv_oldgame_hack->integer ) {
		import.trace = ( sv_trace_t )SV_Trace_Old;
	} else {
		import.trace = SV_Trace;
	}
#endif /* !_WIN32 */
	import.pointcontents = SV_PointContents;
	import.setmodel = PF_setmodel;
	import.inPVS = PF_inPVS;
	import.inPHS = PF_inPHS;
	import.Pmove = PF_Pmove;

	import.modelindex = PF_ModelIndex;
	import.soundindex = PF_SoundIndex;
	import.imageindex = PF_ImageIndex;

	import.configstring = PF_Configstring;
	import.sound = PF_StartSound;
	import.positioned_sound = PF_PositionedSound;

	import.WriteChar = MSG_WriteChar;
	import.WriteByte = MSG_WriteByte;
	import.WriteShort = MSG_WriteShort;
	import.WriteLong = MSG_WriteLong;
	import.WriteFloat = PF_WriteFloat;
	import.WriteString = MSG_WriteString;
	import.WritePosition = MSG_WritePos;
	import.WriteDir = MSG_WriteDir;
	import.WriteAngle = MSG_WriteAngle;

	import.TagMalloc = Z_TagMallocz;
	import.TagFree = Z_Free;
	import.FreeTags = Z_FreeTags;

	import.cvar = PF_cvar;
	import.cvar_set = Cvar_UserSet;
	import.cvar_forceset = Cvar_Set;

	import.argc = Cmd_Argc;
	import.argv = Cmd_Argv;
	import.args = Cmd_Args;
	import.AddCommandString = PF_AddCommandString;

	import.DebugGraph = SCR_DebugGraph;
	import.SetAreaPortalState = PF_SetAreaPortalState;
	import.AreasConnected = PF_AreasConnected;

	ge = entry( &import );
	if (!ge) {
		Com_Error (ERR_DROP, "Game DLL returned NULL exports");
    }
	if (ge->apiversion != GAME_API_VERSION) {
		Com_Error (ERR_DROP, "Game DLL is version %d, expected %d",
            ge->apiversion, GAME_API_VERSION);
    }

    // initialize
	ge->Init ();

    Sys_FixFPCW();
}

