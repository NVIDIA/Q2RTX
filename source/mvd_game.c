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
// mvd_game.c
//

#include "mvd_local.h"
#include <setjmp.h>

static cvar_t   *mvd_admin_password;
static cvar_t   *mvd_flood_msgs;
static cvar_t   *mvd_flood_persecond;
static cvar_t   *mvd_flood_waitdelay;
static cvar_t   *mvd_flood_mute;
static cvar_t   *mvd_filter_version;
static cvar_t   *mvd_stats_score;
static cvar_t   *mvd_stats_hack;
static cvar_t   *mvd_freeze_hack;
static cvar_t   *mvd_chase_prefix;

mvd_client_t    *mvd_clients;

mvd_player_t    mvd_dummy;

extern jmp_buf  mvd_jmpbuf;

static void MVD_UpdateClient( mvd_client_t *client );

/*
==============================================================================

LAYOUTS

==============================================================================
*/

static void MVD_LayoutClients( mvd_client_t *client ) {
    static const char header[] = 
        "xv 16 yv 0 string2 \"    Name            RTT Status\"";
    char layout[MAX_STRING_CHARS];
    char buffer[MAX_QPATH];
    char status[MAX_QPATH];
    size_t len, total;
    mvd_client_t *cl;
    mvd_t *mvd = client->mvd;
    int y, i, prestep, flags;

    // calculate prestep
    if( client->layout_cursor < 0 ) {
        client->layout_cursor = 0;
    } else if( client->layout_cursor ) {
        total = List_Count( &mvd->clients );
        if( client->layout_cursor > total / 10 ) {
            client->layout_cursor = total / 10;
        }
    }

    prestep = client->layout_cursor * 10;

    memcpy( layout, header, sizeof( header ) - 1 );
    total = sizeof( header ) - 1;

    y = 8;
    i = 0;
    LIST_FOR_EACH( mvd_client_t, cl, &mvd->clients, entry ) {
        if( ++i < prestep ) {
            continue;
        }
        if( cl->cl->state < cs_spawned ) {
            continue;
        }
        if( cl->target ) {
            strcpy( status, "-> " );
            strcpy( status + 3, cl->target->name );
        } else {
            strcpy( status, "observing" );
        }
        len = Q_snprintf( buffer, sizeof( buffer ),
            "yv %d string \"%3d %-15.15s %3d %s\"",
            y, i, cl->cl->name, cl->ping, status );
        if( len >= sizeof( buffer ) ) {
            continue;
        }
        if( total + len >= sizeof( layout ) ) {
            break;
        }
        memcpy( layout + total, buffer, len );
        total += len;
        y += 8;
    }

    layout[total] = 0;

    // the very first layout update is reliably delivered
    flags = MSG_CLEAR;
    if( !client->layout_time ) {
        flags |= MSG_RELIABLE;
    }

    // send the layout
    MSG_WriteByte( svc_layout );
    MSG_WriteData( layout, total + 1 );
    SV_ClientAddMessage( client->cl, flags );

    client->layout_time = svs.realtime;
}

static void MVD_LayoutChannels( mvd_client_t *client ) {
    static const char header[] =
        "xv 32 yv 8 picn inventory "
        "xv 240 yv 172 string2 " VERSION " "
        "xv 0 yv 32 cstring \"\020Channel Chooser\021\""
        "xv 64 yv 48 string2 \"Name         Map     S/P\""
        "yv 56 string \"------------ ------- ---\" xv 56 ";
    static const char nochans[] =
        "yv 72 string \" No active channels.\""
        "yv 80 string \" Please wait until players\""
        "yv 88 string \" connect.\""
        ;
    char layout[MAX_STRING_CHARS];
    char buffer[MAX_QPATH];
    mvd_t *mvd;
    size_t len, total;
    int cursor, y;

    memcpy( layout, header, sizeof( header ) - 1 );
    total = sizeof( header ) - 1;

    // FIXME: improve this
    cursor = List_Count( &mvd_channel_list );
    if( cursor ) {
        if( client->layout_cursor < 0 ) {
            client->layout_cursor = cursor - 1;
        } else if( client->layout_cursor > cursor - 1 ) {
            client->layout_cursor = 0;
        }

        y = 64;
        cursor = 0;
        LIST_FOR_EACH( mvd_t, mvd, &mvd_channel_list, entry ) {
            len = Q_snprintf( buffer, sizeof( buffer ),
                "yv %d string%s \"%c%-12.12s %-7.7s %d/%d\" ", y,
                mvd == client->mvd ? "2" : "",
                cursor == client->layout_cursor ? 0x8d : 0x20,
                mvd->name, mvd->mapname,
                List_Count( &mvd->clients ),
                mvd->numplayers );
            if( len >= sizeof( buffer ) ) {
                continue;
            }
            if( total + len >= sizeof( layout ) ) {
                break;
            }
            memcpy( layout + total, buffer, len );
            total += len;
            y += 8;
            if( y > 164 ) {
                break;
            }

            cursor++;
        }
    } else {
        client->layout_cursor = 0;
        memcpy( layout + total, nochans, sizeof( nochans ) - 1 );
        total += sizeof( nochans ) - 1;
    }

    layout[total] = 0;

    // send the layout
    MSG_WriteByte( svc_layout );
    MSG_WriteData( layout, total + 1 );                
    SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );

    client->layout_time = svs.realtime;
}

#define MENU_ITEMS  10
#define YES "\xD9\xE5\xF3"
#define NO "\xCE\xEF"

static void MVD_LayoutMenu( mvd_client_t *client ) {
    static const char format[] =
        "xv 32 yv 8 picn inventory "
        "xv 0 yv 32 cstring \"\020Main Menu\021\" xv 56 "
        "yv 48 string2 \"%c%s in-eyes mode\""
        "yv 56 string2 \"%cShow scoreboard\""
        "yv 64 string2 \"%cShow spectators (%d)\""
        "yv 72 string2 \"%cShow channels (%d)\""
        "yv 80 string2 \"%cLeave this channel\""
        "yv 96 string \"%cIgnore spectator chat: %s\""
        "yv 104 string \"%cIgnore connect msgs:   %s\""
        "yv 112 string \"%cIgnore player chat:    %s\""
        "yv 120 string \"%cIgnore player FOV:     %s\""
        "yv 128 string \" (use 'set uf %d u' in cfg)\""
        "yv 144 string2 \"%cExit menu\""
        "%s xv 240 yv 172 string2 " VERSION;
    char layout[MAX_STRING_CHARS];
    char cur[MENU_ITEMS];
    size_t total;

    if( client->layout_cursor < 0 ) {
        client->layout_cursor = MENU_ITEMS - 1;
    } else if( client->layout_cursor > MENU_ITEMS - 1 ) {
        client->layout_cursor = 0;
    }

    memset( cur, 0x20, sizeof( cur ) );
    cur[client->layout_cursor] = 0x8d;

    total = Q_scnprintf( layout, sizeof( layout ), format,
        cur[0], client->target ? "Leave" : "Enter", cur[1],
        cur[2], List_Count( &client->mvd->clients ),
        cur[3], List_Count( &mvd_channel_list ), cur[4],
        cur[5], ( client->uf & UF_MUTE_OBSERVERS ) ? YES : NO,
        cur[6], ( client->uf & UF_MUTE_MISC ) ? YES : NO,
        cur[7], ( client->uf & UF_MUTE_PLAYERS ) ? YES: NO,
        cur[8], ( client->uf & UF_LOCALFOV ) ? YES : NO,
        client->uf,
        cur[9], client->mvd->state == MVD_WAITING ?
        "xv 0 yv 160 cstring [BUFFERING]" : "" );

    // send the layout
    MSG_WriteByte( svc_layout );
    MSG_WriteData( layout, total + 1 );                
    SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );

    client->layout_time = svs.realtime;
}

static void MVD_LayoutScores( mvd_client_t *client, const char *layout ) {
    mvd_t *mvd = client->mvd;
    int flags = MSG_CLEAR;

    if( !layout || !layout[0] ) {
        layout = "xv 100 yv 60 string \"<no scoreboard>\"";
    }

    // end-of-match scoreboard is reliably delivered
    if( !client->layout_time || mvd->intermission ) {
        flags |= MSG_RELIABLE;
    }

    // send the layout
    MSG_WriteByte( svc_layout );
    MSG_WriteString( layout );
    SV_ClientAddMessage( client->cl, flags );

    client->layout_time = svs.realtime;
}

static void MVD_LayoutFollow( mvd_client_t *client ) {
    mvd_t *mvd = client->mvd;
    char *name = client->target ? client->target->name : "<no target>";
    char layout[MAX_STRING_CHARS];
    size_t total;

    total = Q_scnprintf( layout, sizeof( layout ),
        "%s string \"[%s] Chasing %s\"",
        mvd_chase_prefix->string, mvd->name, name );

    // send the layout
    MSG_WriteByte( svc_layout );
    MSG_WriteData( layout, total + 1 );
    SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );

    client->layout_time = svs.realtime;
}

static void MVD_SetDefaultLayout( mvd_client_t *client ) {
    mvd_t *mvd = client->mvd;

    if( mvd == &mvd_waitingRoom ) {
        client->layout_type = LAYOUT_CHANNELS;
    } else if( mvd->intermission ) {
        client->layout_type = LAYOUT_SCORES;
    } else if( client->target ) {
        client->layout_type = LAYOUT_FOLLOW;
    } else {
        client->layout_type = LAYOUT_NONE;
    }

    // force an update
    client->layout_time = 0;
    client->layout_cursor = 0;
}

static void MVD_SetFollowLayout( mvd_client_t *client ) {
    if( !client->layout_type ) {
        MVD_SetDefaultLayout( client );
    } else if( client->layout_type == LAYOUT_FOLLOW ) {
        client->layout_time = 0; // force an update
    }
}

// this is the only function that actually writes layouts
static void MVD_UpdateLayouts( mvd_t *mvd ) {
    mvd_client_t *client;

    LIST_FOR_EACH( mvd_client_t, client, &mvd->clients, entry ) {
        if( client->cl->state != cs_spawned ) {
            continue;
        }
        client->ps.stats[STAT_LAYOUTS] = client->layout_type ? 1 : 0;
        switch( client->layout_type ) {
        case LAYOUT_FOLLOW:
            if( !client->layout_time ) {
                MVD_LayoutFollow( client );
            }
            break;
        case LAYOUT_OLDSCORES:
            if( !client->layout_time ) {
                MVD_LayoutScores( client, mvd->oldscores );
            }
            break;
        case LAYOUT_SCORES:
            if( !client->layout_time ) {
                MVD_LayoutScores( client, mvd->layout );
            }
            break;
        case LAYOUT_MENU:
            if( !client->layout_time ) {
                MVD_LayoutMenu( client );
            }
            break;
        case LAYOUT_CLIENTS:
            if( svs.realtime - client->layout_time > LAYOUT_MSEC ) {
                MVD_LayoutClients( client );
            }
            break;
        case LAYOUT_CHANNELS:
            if( mvd_dirty || !client->layout_time ) {
                MVD_LayoutChannels( client );
            }
            break;
        default:
            break;
        }
    }
}


/*
==============================================================================

CHASE CAMERA

==============================================================================
*/

static void MVD_FollowStop( mvd_client_t *client ) {
    mvd_t *mvd = client->mvd;
    mvd_cs_t *cs;
    int i;

    client->ps.viewangles[ROLL] = 0;

    for( i = 0; i < 3; i++ ) {
        client->ps.pmove.delta_angles[i] = ANGLE2SHORT(
            client->ps.viewangles[i] ) - client->lastcmd.angles[i];
    }

    VectorClear( client->ps.kick_angles );
    Vector4Clear( client->ps.blend );
    client->ps.pmove.pm_flags = 0;
    client->ps.pmove.pm_type = mvd->pm_type;
    client->ps.rdflags = 0;
    client->ps.gunindex = 0;
    client->ps.fov = client->fov;

    for( cs = mvd->dummy->configstrings; cs; cs = cs->next ) {
        MSG_WriteByte( svc_configstring );
        MSG_WriteShort( cs->index );
        MSG_WriteString( cs->string );
        SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );
    }

    client->clientNum = mvd->clientNum;
    client->oldtarget = client->target;
    client->target = NULL;

    if( client->layout_type == LAYOUT_FOLLOW ) {
        MVD_SetDefaultLayout( client );
    }

    MVD_UpdateClient( client );
}

static void MVD_FollowStart( mvd_client_t *client, mvd_player_t *target ) {
    mvd_cs_t *cs;

    if( client->target == target ) {
        return;
    }

    client->oldtarget = client->target;
    client->target = target;

    // send delta configstrings
    for( cs = target->configstrings; cs; cs = cs->next ) {
        MSG_WriteByte( svc_configstring );
        MSG_WriteShort( cs->index );
        MSG_WriteString( cs->string );
        SV_ClientAddMessage( client->cl, MSG_RELIABLE|MSG_CLEAR );
    }

    SV_ClientPrintf( client->cl, PRINT_LOW, "[MVD] Chasing %s.\n", target->name );

    MVD_SetFollowLayout( client );
    MVD_UpdateClient( client );
}

static void MVD_FollowFirst( mvd_client_t *client ) {
    mvd_t *mvd = client->mvd;
    mvd_player_t *target;
    int i;

    // pick up the first active player
    for( i = 0; i < mvd->maxclients; i++ ) {
        target = &mvd->players[i];
        if( target->inuse && target != mvd->dummy ) {
            MVD_FollowStart( client, target );
            return;
        }
    }

    SV_ClientPrintf( client->cl, PRINT_MEDIUM, "[MVD] No players to chase.\n" );
}

static void MVD_FollowLast( mvd_client_t *client ) {
    mvd_t *mvd = client->mvd;
    mvd_player_t *target;
    int i;

    // pick up the last active player
    for( i = 0; i < mvd->maxclients; i++ ) {
        target = &mvd->players[ mvd->maxclients - i - 1 ];
        if( target->inuse && target != mvd->dummy ) {
            MVD_FollowStart( client, target );
            return;
        }
    }

    SV_ClientPrintf( client->cl, PRINT_MEDIUM, "[MVD] No players to chase.\n" );
}

static void MVD_FollowNext( mvd_client_t *client ) {
    mvd_t *mvd = client->mvd;
    mvd_player_t *target = client->target;

    if( !target ) {
        MVD_FollowFirst( client );
        return;
    }

    do {
        if( target == mvd->players + mvd->maxclients - 1 ) {
            target = mvd->players;
        } else {
            target++;
        }
        if( target == client->target ) {
            return;
        }
    } while( !target->inuse || target == mvd->dummy );

    MVD_FollowStart( client, target );
}

static void MVD_FollowPrev( mvd_client_t *client ) {
    mvd_t *mvd = client->mvd;
    mvd_player_t *target = client->target;

    if( !target ) {
        MVD_FollowLast( client );
        return;
    }

    do {
        if( target == mvd->players ) {
            target = mvd->players + mvd->maxclients - 1;
        } else {
            target--;
        }
        if( target == client->target ) {
            return;
        }
    } while( !target->inuse || target == mvd->dummy );

    MVD_FollowStart( client, target );
}

static mvd_player_t *MVD_MostFollowed( mvd_t *mvd ) {
    int count[MAX_CLIENTS];
    mvd_client_t *other;
    mvd_player_t *player, *target = NULL;
    int i, maxcount = -1;

    memset( count, 0, sizeof( count ) );

    LIST_FOR_EACH( mvd_client_t, other, &mvd->clients, entry ) {
        if( other->cl->state == cs_spawned && other->target ) {
            count[ other->target - mvd->players ]++;
        }
    }
    for( i = 0, player = mvd->players; i < mvd->maxclients; i++, player++ ) {
        if( player->inuse && player != mvd->dummy && maxcount < count[i] ) {
            maxcount = count[i];
            target = player;
        }
    }
    return target;
}

static void MVD_UpdateClient( mvd_client_t *client ) {
    mvd_t *mvd = client->mvd;
    mvd_player_t *target = client->target;
    int i;

    if( !target ) {
        // copy stats of the dummy MVD observer
        target = mvd->dummy;
        for( i = 0; i < MAX_STATS; i++ ) {
            client->ps.stats[i] = target->ps.stats[i];
        }
    } else {
        if( !target->inuse ) {
            // player is no longer active
            MVD_FollowStop( client );
            return;
        }

        // copy entire player state
        client->ps = target->ps;
        if( client->uf & UF_LOCALFOV ) {
            client->ps.fov = client->fov;
        }
        client->ps.pmove.pm_flags |= PMF_NO_PREDICTION;
        client->ps.pmove.pm_type = PM_FREEZE;
        client->clientNum = target - mvd->players;

        if( mvd_stats_hack->integer && target != mvd->dummy ) {
            // copy stats of the dummy MVD observer
            target = mvd->dummy;
            for( i = 0; i < MAX_STATS; i++ ) {
                if( mvd_stats_hack->integer & ( 1 << i ) ) {
                    client->ps.stats[i] = target->ps.stats[i];
                }
            }
        }
    }

    // override score
    switch( mvd_stats_score->integer ) {
    case 0:
        client->ps.stats[STAT_FRAGS] = 0;
        break;
    case 1:
        client->ps.stats[STAT_FRAGS] = mvd->id;
        break;
    }
}

/*
==============================================================================

SPECTATOR COMMANDS

==============================================================================
*/

void MVD_BroadcastPrintf( mvd_t *mvd, int level, int mask, const char *fmt, ... ) {
    va_list     argptr;
    char        text[MAX_STRING_CHARS];
    size_t      len;
    mvd_client_t *other;
    client_t    *cl;

    va_start( argptr, fmt );
    len = Q_vsnprintf( text, sizeof( text ), fmt, argptr );
    va_end( argptr );

    if( len >= sizeof( text ) ) {
        Com_WPrintf( "%s: overflow\n", __func__ );
        return;
    }

    if( level == PRINT_CHAT && mvd_filter_version->integer ) {
        char *s;

        while( ( s = strstr( text, "!version" ) ) != NULL ) {
            s[6] = '0';
        }
    }

    MSG_WriteByte( svc_print );
    MSG_WriteByte( level );
    MSG_WriteData( text, len + 1 );

    LIST_FOR_EACH( mvd_client_t, other, &mvd->clients, entry ) {
        cl = other->cl;
        if( cl->state < cs_spawned ) {
            continue;
        }
        if( level < cl->messagelevel ) {
            continue;
        }
        if( other->uf & mask ) {
            continue;
        }
        SV_ClientAddMessage( cl, MSG_RELIABLE );
    }

    SZ_Clear( &msg_write );
}

static void MVD_SetServerState( client_t *cl, mvd_t *mvd ) {
    cl->gamedir = mvd->gamedir;
    cl->mapname = mvd->mapname;
    cl->configstrings = ( char * )mvd->configstrings;
    cl->slot = mvd->clientNum;
    cl->cm = &mvd->cm;
    cl->pool = &mvd->pool;
    cl->spawncount = mvd->servercount;
    cl->maxclients = mvd->maxclients;
}

void MVD_SwitchChannel( mvd_client_t *client, mvd_t *mvd ) {
    client_t *cl = client->cl;

    List_Remove( &client->entry );
    List_Append( &mvd->clients, &client->entry );
    client->mvd = mvd;
    client->begin_time = 0;
    client->target = client->oldtarget = NULL;
    MVD_SetServerState( cl, mvd );

    // needs to reconnect
    MSG_WriteByte( svc_stufftext );
    MSG_WriteString( va( "changing map=%s; reconnect\n", mvd->mapname ) );
    SV_ClientReset( cl );
    SV_ClientAddMessage( cl, MSG_RELIABLE|MSG_CLEAR );
}

void MVD_TrySwitchChannel( mvd_client_t *client, mvd_t *mvd ) {
    if( mvd == client->mvd ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] You are already %s.\n", mvd == &mvd_waitingRoom ?
            "in the Waiting Room" : "on this channel" );
        return; // nothing to do
    }
    if( client->begin_time ) {
        if( client->begin_time > svs.realtime ) {
            client->begin_time = svs.realtime;
        }
        if( svs.realtime - client->begin_time < 2000 ) {
            SV_ClientPrintf( client->cl, PRINT_HIGH,
                "[MVD] You may not switch channels too soon.\n" );
            return;
        }
        MVD_BroadcastPrintf( client->mvd, PRINT_MEDIUM, UF_MUTE_MISC,
            "[MVD] %s left the channel\n", client->cl->name );
    }

    MVD_SwitchChannel( client, mvd );
}

static void MVD_Admin_f( mvd_client_t *client ) {
    char *s = mvd_admin_password->string;

    if( client->admin ) {
        client->admin = qfalse;
        SV_ClientPrintf( client->cl, PRINT_HIGH, "[MVD] Lost admin status.\n" );
        return;
    }

    if( !NET_IsLocalAddress( &client->cl->netchan->remote_address ) ) {
        if( Cmd_Argc() < 2 ) {
            SV_ClientPrintf( client->cl, PRINT_HIGH, "Usage: %s <password>\n", Cmd_Argv( 0 ) );
            return;
        }
        if( !s[0] || strcmp( s, Cmd_Argv( 1 ) ) ) {
            SV_ClientPrintf( client->cl, PRINT_HIGH, "[MVD] Invalid password.\n" );
            return;
        }
    }

    client->admin = qtrue;
    SV_ClientPrintf( client->cl, PRINT_HIGH, "[MVD] Granted admin status.\n" );
}

static void MVD_Forward_f( mvd_client_t *client ) {
    mvd_t *mvd = client->mvd;

    if( !client->admin ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] You don't have admin status.\n" );
        return;
    }

    if( !mvd->forward_cmd ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] This channel does not support command forwarding.\n" );
        return;
    }

    mvd->forward_cmd( client );
}

static void MVD_Say_f( mvd_client_t *client, int argnum ) {
    mvd_t *mvd = client->mvd;
    unsigned delta, delay = mvd_flood_waitdelay->value * 1000;
    unsigned treshold = mvd_flood_persecond->value * 1000;
    char text[150], *p;
    int i;

    if( mvd_flood_mute->integer && !client->admin ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] Spectators may not talk on this server.\n" );
        return;
    }
    if( client->uf & UF_MUTE_OBSERVERS ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] Please turn off ignore mode first.\n" );
        return;
    }

    if( client->floodTime ) {
        delta = svs.realtime - client->floodTime;
        if( delta < delay ) {
            SV_ClientPrintf( client->cl, PRINT_HIGH,
                "[MVD] You can't talk for %u more seconds.\n",
                ( delay - delta ) / 1000 );
            return;
        }
    }

    Cvar_ClampInteger( mvd_flood_msgs, 0, FLOOD_SAMPLES - 1 );
    i = client->floodHead - mvd_flood_msgs->integer - 1;
    if( i >= 0 ) {
        delta = svs.realtime - client->floodSamples[i & FLOOD_MASK];
        if( delta < treshold ) {
            SV_ClientPrintf( client->cl, PRINT_HIGH,
                "[MVD] You can't talk for %u seconds.\n", delay / 1000 );
            client->floodTime = svs.realtime;
            return;
        }
    }

    client->floodSamples[client->floodHead & FLOOD_MASK] = svs.realtime;
    client->floodHead++;

    Q_snprintf( text, sizeof( text ), "[MVD] %s: %s",
        client->cl->name, Cmd_ArgsFrom( argnum ) );
    for( p = text; *p; p++ ) {
        *p |= 128;
    }

    MVD_BroadcastPrintf( mvd, PRINT_MEDIUM, client->admin ?
        0 : UF_MUTE_OBSERVERS, "%s\n", text );
}

static void MVD_Observe_f( mvd_client_t *client ) {
    if( client->mvd == &mvd_waitingRoom ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] Please enter a channel first.\n" );
        return;
    }
    if( client->mvd->intermission ) {
        return;
    }
    if( client->target ) {
        MVD_FollowStop( client );
    } else if( client->oldtarget && client->oldtarget->inuse ) {
        MVD_FollowStart( client, client->oldtarget );
    } else {
        MVD_FollowFirst( client );
    }
}

static void MVD_Follow_f( mvd_client_t *client ) {
    mvd_t *mvd = client->mvd;
    mvd_player_t *player;
    entity_state_t *ent;
    char *s;
    int i, mask;

    if( mvd == &mvd_waitingRoom ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] Please enter a channel first.\n" );
        return;
    }

    if( mvd->intermission ) {
        return;
    }

    if( Cmd_Argc() < 2 ) {
        MVD_Observe_f( client );
        return;
    }

    s = Cmd_Argv( 1 );
    if( s[0] == '!' ) {
        switch( s[1] ) {
        case 'q':
            mask = EF_QUAD;
            break;
        case 'i':
            mask = EF_PENT;
            break;
        case 'r':
            mask = EF_FLAG1;
            break;
        case 'b':
            mask = EF_FLAG2;
            break;
        case 'p':
            if( client->oldtarget && client->oldtarget->inuse ) {
                MVD_FollowStart( client, client->oldtarget );
            } else {
                SV_ClientPrintf( client->cl, PRINT_HIGH,
                    "[MVD] Previous target is not active.\n" );
            }
            return;
        default:
            SV_ClientPrintf( client->cl, PRINT_HIGH,
                "[MVD] Unknown target '%s'. Valid targets:\n"
                "q (quad), i (invulner), r (red flag), "
                "b (blue flag), p (previous target).\n", s + 1 );
            return;
        }
        for( i = 0; i < mvd->maxclients; i++ ) {
            player = &mvd->players[i];
            if( !player->inuse || player == mvd->dummy ) {
                continue;
            }
            ent = &mvd->edicts[ i + 1 ].s;
            if( ent->effects & mask ) {
                goto follow;
            }
        }
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] No players with '%s' powerup.\n", s + 1 );
        return;
    }

    i = atoi( s );
    if( i < 0 || i >= mvd->maxclients ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] Player number %d is invalid.\n", i );
        return;
    }

    player = &mvd->players[i];
    if( !player->inuse || player == mvd->dummy ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] Player %d is not active.\n", i );
        return;
    }

follow:
    MVD_FollowStart( client, player );
}

static void MVD_Invuse_f( mvd_client_t *client ) {
    mvd_t *mvd;
    int uf = client->uf;

    if( client->layout_type == LAYOUT_MENU ) {
        if( client->layout_cursor < 0 ) {
            client->layout_cursor = MENU_ITEMS - 1;
        } else if( client->layout_cursor > MENU_ITEMS - 1 ) {
            client->layout_cursor = 0;
        }
        switch( client->layout_cursor ) {
        case 0:
            MVD_SetDefaultLayout( client );
            MVD_Observe_f( client );
            return;
        case 1:
            client->layout_type = LAYOUT_SCORES;
            break;
        case 2:
            client->layout_type = LAYOUT_CLIENTS;
            client->layout_cursor = 0;
            break;
        case 3:
            client->layout_type = LAYOUT_CHANNELS;
            client->layout_cursor = 0;
            break;
        case 4:
            MVD_TrySwitchChannel( client, &mvd_waitingRoom );
            return;
        case 5:
            client->uf ^= UF_MUTE_OBSERVERS;
            break;
        case 6:
            client->uf ^= UF_MUTE_MISC;
            break;
        case 7:
            client->uf ^= UF_MUTE_PLAYERS;
            break;
        case 8:
            client->uf ^= UF_LOCALFOV;
            break;
        case 9:
            MVD_SetDefaultLayout( client );
            break;
        }
        if( uf != client->uf ) {
            SV_ClientCommand( client->cl, "set uf %d u\n", client->uf );
        }
        client->layout_time = 0;
        return;
    }

    if( client->layout_type == LAYOUT_CHANNELS ) {
        mvd = LIST_INDEX( mvd_t, client->layout_cursor, &mvd_channel_list, entry );
        if( mvd ) {
            MVD_TrySwitchChannel( client, mvd );
        } else {
            client->layout_time = 0;
        }
        return;
    }
}

static void MVD_Join_f( mvd_client_t *client ) {
    mvd_t *mvd;
        
    SV_BeginRedirect( RD_CLIENT );
    mvd = MVD_SetChannel( 1 );
    Com_EndRedirect();

    if( !mvd ) {
        return;
    }
    if( mvd->state < MVD_WAITING ) {
        SV_ClientPrintf( client->cl, PRINT_HIGH,
            "[MVD] This channel is not ready yet.\n" );
        return;
    }

    MVD_TrySwitchChannel( client, mvd );
}

static void print_channel( client_t *cl, mvd_t *mvd ) {
    mvd_player_t *player;
    char buffer[MAX_QPATH];
    size_t len, total;
    int i;

    total = 0;
    for( i = 0; i < mvd->maxclients; i++ ) {
        player = &mvd->players[i];
        if( !player->inuse || player == mvd->dummy ) {
            continue;
        }
        len = strlen( player->name );
        if( total + len + 2 >= sizeof( buffer ) ) {
            break;
        }
        if( total ) {
            buffer[total+0] = ',';
            buffer[total+1] = ' ';
            total += 2;
        }
        memcpy( buffer + total, player->name, len );
        total += len;
    }
    buffer[total] = 0;

    SV_ClientPrintf( cl, PRINT_HIGH,
        "%2d %-12.12s %-8.8s %3d %3d %s\n", mvd->id,
        mvd->name, mvd->mapname,
        List_Count( &mvd->clients ),
        mvd->numplayers, buffer );
}

static void mvd_channel_list_f( mvd_client_t *client ) {
    mvd_t *mvd;

    SV_ClientPrintf( client->cl, PRINT_HIGH,
        "id name         map      spc plr who is playing\n"
        "-- ------------ -------- --- --- --------------\n" );

    LIST_FOR_EACH( mvd_t, mvd, &mvd_channel_list, entry ) {
        print_channel( client->cl, mvd );
    }
}

static void MVD_Clients_f( mvd_client_t *client ) {
    // TODO: dump them in console
    client->layout_type = LAYOUT_CLIENTS;
    client->layout_time = 0;
    client->layout_cursor = 0;
}

static void MVD_Commands_f( mvd_client_t *client ) {
    SV_ClientPrintf( client->cl, PRINT_HIGH,
        "chase [player_id]      toggle chasecam mode\n"
        "observe                toggle observer mode\n"
        "menu                   show main menu\n"
        "score                  show scoreboard\n"
        "oldscore               show previous scoreboard\n"
        "channels [all]         list active (or all) channels\n"
        "join [channel_id]      join specified channel\n"
        "leave                  go to the Waiting Room\n"
    );
}

static void MVD_GameClientCommand( edict_t *ent ) {
    mvd_client_t *client = EDICT_MVDCL( ent );
    char *cmd;

    if( client->cl->state < cs_spawned ) {
        return;
    }

    cmd = Cmd_Argv( 0 );

    if( !strcmp( cmd, "!mvdadmin" ) ) {
        MVD_Admin_f( client );
        return;
    }
    if( !strcmp( cmd, "fwd" ) ) {
        MVD_Forward_f( client );
        return;
    }
    if( !strcmp( cmd, "say" ) || !strcmp( cmd, "say_team" ) ) {
        MVD_Say_f( client, 1 );
        return;
    }
    if( !strcmp( cmd, "follow" ) || !strcmp( cmd, "chase" ) ) {
        MVD_Follow_f( client );
        return;
    }
    if( !strcmp( cmd, "observe" ) ) {
        MVD_Observe_f( client );
        return;
    }
    if( !strcmp( cmd, "inven" ) || !strcmp( cmd, "menu" ) ) {
        if( client->layout_type == LAYOUT_MENU ) {
            MVD_SetDefaultLayout( client );
        } else {
            client->layout_type = LAYOUT_MENU;
            client->layout_time = 0;
        }
        return;
    }
    if( !strcmp( cmd, "invnext" ) ) {
        if( client->layout_type >= LAYOUT_MENU ) {
            client->layout_cursor++;
            client->layout_time = 0;
        } else if( !client->mvd->intermission ) {
            MVD_FollowNext( client );
        }
        return;
    }
    if( !strcmp( cmd, "invprev" ) ) {
        if( client->layout_type >= LAYOUT_MENU ) {
            client->layout_cursor--;
            client->layout_time = 0;
        } else if( !client->mvd->intermission ) {
            MVD_FollowPrev( client );
        }
        return;
    }
    if( !strcmp( cmd, "invuse" ) ) {
        MVD_Invuse_f( client );
        return;
    }
    if( !strcmp( cmd, "help" ) || !strcmp( cmd, "score" ) ) {
        if( client->layout_type == LAYOUT_SCORES ) {
            MVD_SetDefaultLayout( client );
        } else {
            client->layout_type = LAYOUT_SCORES;
            client->layout_time = 0;
        }
        return;
    }
    if( !strcmp( cmd, "oldscore" ) ) {
        if( client->layout_type == LAYOUT_OLDSCORES ) {
            MVD_SetDefaultLayout( client );
        } else {
            client->layout_type = LAYOUT_OLDSCORES;
            client->layout_time = 0;
        }
        return;
    }
    if( !strcmp( cmd, "putaway" ) ) {
        MVD_SetDefaultLayout( client );
        return;
    }
    if( !strcmp( cmd, "channels" ) ) {
        mvd_channel_list_f( client );
        return;
    }
    if( !strcmp( cmd, "clients" ) || !strcmp( cmd, "players" ) ) {
        MVD_Clients_f( client );
        return;
    }
    if( !strcmp( cmd, "join" ) ) {
        MVD_Join_f( client );
        return;
    }
    if( !strcmp( cmd, "leave" ) ) {
        MVD_TrySwitchChannel( client, &mvd_waitingRoom );
        return;
    }
    if( !strcmp( cmd, "commands" ) ) {
        MVD_Commands_f( client );
        return;
    }

    MVD_Say_f( client, 0 );
}

/*
==============================================================================

MISC GAME FUNCTIONS

==============================================================================
*/

void MVD_RemoveClient( client_t *client ) {
    int index = client - svs.udp_client_pool;
    mvd_client_t *cl = &mvd_clients[index];

    List_Remove( &cl->entry );

    memset( cl, 0, sizeof( *cl ) );
    cl->cl = client;
}

static void MVD_GameInit( void ) {
    mvd_t *mvd = &mvd_waitingRoom;
    edict_t *edicts;
    cvar_t *mvd_default_map;
    char buffer[MAX_QPATH];
    unsigned checksum;
    bsp_t *bsp;
    int i;

    Com_Printf( "----- MVD_GameInit -----\n" );

    mvd_admin_password = Cvar_Get( "mvd_admin_password", "", CVAR_PRIVATE );
    mvd_flood_msgs = Cvar_Get( "flood_msgs", "4", 0 );
    mvd_flood_persecond = Cvar_Get( "flood_persecond", "4", 0 ); // FIXME: rename this
    mvd_flood_waitdelay = Cvar_Get( "flood_waitdelay", "10", 0 );
    mvd_flood_mute = Cvar_Get( "flood_mute", "0", 0 );
    mvd_filter_version = Cvar_Get( "mvd_filter_version", "0", 0 );
    mvd_default_map = Cvar_Get( "mvd_default_map", "q2dm1", CVAR_LATCH );
    mvd_stats_score = Cvar_Get( "mvd_stats_score", "0", 0 );
    mvd_stats_hack = Cvar_Get( "mvd_stats_hack", "0", 0 );
    mvd_freeze_hack = Cvar_Get( "mvd_freeze_hack", "1", 0 );
    mvd_chase_prefix = Cvar_Get( "mvd_chase_prefix", "xv 0 yb -64", 0 );
    Cvar_Set( "g_features", va( "%d", MVD_FEATURES ) );

    Z_TagReserve( ( sizeof( edict_t ) +
        sizeof( mvd_client_t ) ) * sv_maxclients->integer +
        sizeof( edict_t ), TAG_MVD );
    mvd_clients = Z_ReservedAllocz( sizeof( mvd_client_t ) *
        sv_maxclients->integer );
    edicts = Z_ReservedAllocz( sizeof( edict_t ) *
        ( sv_maxclients->integer + 1 ) );

    for( i = 0; i < sv_maxclients->integer; i++ ) {
        mvd_clients[i].cl = &svs.udp_client_pool[i];
        edicts[i + 1].client = ( gclient_t * )&mvd_clients[i];
    }

    mvd_ge.edicts = edicts;
    mvd_ge.edict_size = sizeof( edict_t );
    mvd_ge.num_edicts = sv_maxclients->integer + 1;
    mvd_ge.max_edicts = sv_maxclients->integer + 1;

    Q_snprintf( buffer, sizeof( buffer ),
        "maps/%s.bsp", mvd_default_map->string );

    if( ( bsp = BSP_Load( buffer ) ) == NULL ) {
        Com_WPrintf( "Couldn't load %s for the Waiting Room: %s\n",
            buffer, BSP_GetError() );
        Cvar_Reset( mvd_default_map );
        strcpy( buffer, "maps/q2dm1.bsp" );
        checksum = 80717714;
        VectorSet( mvd->spawnOrigin, 984, 192, 784 );
        VectorSet( mvd->spawnAngles, 25, 72, 0 );
    } else {
        // get the spectator spawn point
        MVD_ParseEntityString( mvd, bsp->entitystring );
        checksum = bsp->checksum;
        BSP_Free( bsp );
    }

    strcpy( mvd->name, "Waiting Room" );
    Cvar_VariableStringBuffer( "game", mvd->gamedir, sizeof( mvd->gamedir ) );
    Q_strlcpy( mvd->mapname, mvd_default_map->string, sizeof( mvd->mapname ) );
    List_Init( &mvd->clients );

    strcpy( mvd->configstrings[CS_NAME], "Waiting Room" );
    strcpy( mvd->configstrings[CS_SKY], "unit1_" );
    strcpy( mvd->configstrings[CS_MAXCLIENTS], "8" );
    sprintf( mvd->configstrings[CS_MAPCHECKSUM], "%d", checksum );
    strcpy( mvd->configstrings[CS_MODELS + 1], buffer );

    mvd->dummy = &mvd_dummy;
    mvd->pm_type = PM_FREEZE;
    mvd->servercount = sv.spawncount;

    // set serverinfo variables
    SV_InfoSet( "mapname", mvd->mapname );
//    SV_InfoSet( "gamedir", "gtv" );
    SV_InfoSet( "gamename", "gtv" );
    SV_InfoSet( "gamedate", __DATE__ );
    MVD_InfoSet( "channels", "0" );
}

static void MVD_GameShutdown( void ) {
    Com_Printf( "----- MVD_GameShutdown -----\n" );

    MVD_Shutdown();

    mvd_ge.edicts = NULL;
    mvd_ge.edict_size = 0;
    mvd_ge.num_edicts = 0;
    mvd_ge.max_edicts = 0;

    Cvar_Set( "g_features", "0" );
}

static void MVD_GameSpawnEntities( const char *mapname, const char *entstring, const char *spawnpoint ) {
}
static void MVD_GameWriteGame( const char *filename, qboolean autosave ) {
}
static void MVD_GameReadGame( const char *filename ) {
}
static void MVD_GameWriteLevel( const char *filename ) {
}
static void MVD_GameReadLevel( const char *filename ) {
}

static qboolean MVD_GameClientConnect( edict_t *ent, char *userinfo ) {
    mvd_client_t *client = EDICT_MVDCL( ent );
    mvd_t *mvd;
    int count;

    // if there is exactly one active channel, assign them to it,
    // otherwise, assign to Waiting Room
    count = List_Count( &mvd_channel_list );
    if( count == 1 ) {
        mvd = LIST_FIRST( mvd_t, &mvd_channel_list, entry );
    } else {
        mvd = &mvd_waitingRoom;
    }
    List_Append( &mvd->clients, &client->entry );
    client->mvd = mvd;
    
    // override server state
    MVD_SetServerState( client->cl, mvd );

    return qtrue;
}

static void MVD_GameClientBegin( edict_t *ent ) {
    mvd_client_t *client = EDICT_MVDCL( ent );
    mvd_t *mvd = client->mvd;
    mvd_player_t *target;

    client->floodTime = 0;
    client->floodHead = 0;
    memset( &client->lastcmd, 0, sizeof( client->lastcmd ) );
    memset( &client->ps, 0, sizeof( client->ps ) );
    client->jump_held = 0;
    client->layout_type = LAYOUT_NONE;
    client->layout_time = 0;
    client->layout_cursor = 0;
    
    if( !client->begin_time ) {
        MVD_BroadcastPrintf( mvd, PRINT_MEDIUM, UF_MUTE_MISC,
            "[MVD] %s entered the channel\n", client->cl->name );
        target = MVD_MostFollowed( mvd );
    } else {
        target = client->target;
    }
    client->oldtarget = NULL;
    client->begin_time = svs.realtime;

    MVD_SetDefaultLayout( client );

    if( mvd->intermission ) {
        // force them to chase dummy MVD client
        client->target = mvd->dummy;
        MVD_SetFollowLayout( client );
        MVD_UpdateClient( client );
    } else if( target && target->inuse ) {
        // start normal chase cam mode
        MVD_FollowStart( client, target );
    } else {
        // spawn the spectator
        VectorScale( mvd->spawnOrigin, 8, client->ps.pmove.origin );
        VectorCopy( mvd->spawnAngles, client->ps.viewangles );
        MVD_FollowStop( client );
    }
}

static void MVD_GameClientUserinfoChanged( edict_t *ent, char *userinfo ) {
    mvd_client_t *client = EDICT_MVDCL( ent );
    char *s;
    float fov;

    s = Info_ValueForKey( userinfo, "uf" );
    client->uf = atoi( s );

    s = Info_ValueForKey( userinfo, "fov" );
    fov = atof( s );
    if( fov < 1 ) {
        fov = 90;
    } else if( fov > 160 ) {
        fov = 160;
    }
    client->fov = fov;
    if( client->uf & UF_LOCALFOV ) {
        client->ps.fov = fov;
    }
}

void MVD_GameClientNameChanged( edict_t *ent, const char *name ) {
    mvd_client_t *client = EDICT_MVDCL( ent );
    client_t *cl = client->cl;

    if( client->begin_time ) {
        MVD_BroadcastPrintf( client->mvd, PRINT_MEDIUM, UF_MUTE_MISC,
            "[MVD] %s changed name to %s\n", cl->name, name );
    }
}

// called early from SV_Drop to prevent multiple disconnect messages
void MVD_GameClientDrop( edict_t *ent, const char *reason ) {
    mvd_client_t *client = EDICT_MVDCL( ent );
    client_t *cl = client->cl;

    if( client->begin_time ) {
        MVD_BroadcastPrintf( client->mvd, PRINT_MEDIUM, UF_MUTE_MISC,
            "[MVD] %s was dropped: %s\n", cl->name, reason );
        client->begin_time = 0;
    }
}

static void MVD_GameClientDisconnect( edict_t *ent ) {
    mvd_client_t *client = EDICT_MVDCL( ent );
    client_t *cl = client->cl;

    if( client->begin_time ) {
        MVD_BroadcastPrintf( client->mvd, PRINT_MEDIUM, UF_MUTE_MISC,
            "[MVD] %s disconnected\n", cl->name );
        client->begin_time = 0;
    }
}


static trace_t MVD_Trace( vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end ) {
    trace_t trace;

    memset( &trace, 0, sizeof( trace ) );
    VectorCopy( end, trace.endpos );
    trace.fraction = 1;

    return trace;
}

static int MVD_PointContents( vec3_t p ) {
    return 0;
}

static void MVD_GameClientThink( edict_t *ent, usercmd_t *cmd ) {
    mvd_client_t *client = EDICT_MVDCL( ent );
    usercmd_t *old = &client->lastcmd;
    pmove_t pm;

    if( ( cmd->buttons & ~old->buttons ) & BUTTON_ATTACK ) {
        MVD_Observe_f( client );
    }

    if( client->target ) {
        if( cmd->upmove >= 10 ) {
            if( client->jump_held < 1 ) {
                if( !client->mvd->intermission ) {
                    MVD_FollowNext( client );
                }
                client->jump_held = 1;
            }
        } else if( cmd->upmove <= -10 ) {
            if( client->jump_held > -1 ) {
                if( !client->mvd->intermission ) {
                    MVD_FollowPrev( client );
                }
                client->jump_held = -1;
            }
        } else {
            client->jump_held = 0;
        }
    } else {
        memset( &pm, 0, sizeof( pm ) );
        pm.trace = MVD_Trace;
        pm.pointcontents = MVD_PointContents;
        pm.s = client->ps.pmove;
        pm.cmd = *cmd;

        PF_Pmove( &pm );

        client->ps.pmove = pm.s;
        if( pm.s.pm_type != PM_FREEZE ) {
            VectorCopy( pm.viewangles, client->ps.viewangles );
        }
    }

    *old = *cmd;
}

static void MVD_IntermissionStart( mvd_t *mvd ) {
    mvd_client_t *client;

    // set this early so MVD_SetDefaultLayout works
    mvd->intermission = qtrue;

    // save oldscores
    // FIXME: unfortunately this will also reset oldscores during
    // match timeout with certain mods (OpenTDM should work fine though)
    strcpy( mvd->oldscores, mvd->layout );

    // force all clients to switch to the MVD dummy
    // and open the scoreboard, unless they had some special layout up
    LIST_FOR_EACH( mvd_client_t, client, &mvd->clients, entry ) {
        if( client->cl->state != cs_spawned ) {
            continue;
        }
        client->oldtarget = client->target;
        client->target = mvd->dummy;
        if( client->layout_type < LAYOUT_SCORES ) {
            MVD_SetDefaultLayout( client );
        }
    }
}

static void MVD_IntermissionStop( mvd_t *mvd ) {
    mvd_client_t *client;
    mvd_player_t *target;

    // set this early so MVD_SetDefaultLayout works
    mvd->intermission = qfalse;

    // force all clients to switch to previous mode
    // and close the scoreboard
    LIST_FOR_EACH( mvd_client_t, client, &mvd->clients, entry ) {
        if( client->cl->state != cs_spawned ) {
            continue;
        }
        if( client->layout_type == LAYOUT_SCORES ) {
            client->layout_type = 0;
        }
        target = client->oldtarget;
        if( target && target->inuse ) {
            // start normal chase cam mode
            MVD_FollowStart( client, target );
        } else {
            MVD_FollowStop( client );
        }
        client->oldtarget = NULL;
    }
}

static void MVD_GameRunFrame( void ) {
    mvd_t *mvd, *next;
    mvd_client_t *client;

    LIST_FOR_EACH_SAFE( mvd_t, mvd, next, &mvd_channel_list, entry ) {
        if( setjmp( mvd_jmpbuf ) ) {
            continue;
        }

        // parse stream
        if( !mvd->read_frame( mvd ) ) {
            goto update;
        }

        // check for intermission
        if( mvd_freeze_hack->integer ) {
            if( !mvd->intermission ) {
                if( mvd->dummy->ps.pmove.pm_type == PM_FREEZE ) {
                    MVD_IntermissionStart( mvd );
                }
            } else if( mvd->dummy->ps.pmove.pm_type != PM_FREEZE ) {
                MVD_IntermissionStop( mvd );
            }
        } else if( mvd->intermission ) {
            MVD_IntermissionStop( mvd );
        }

        // update UDP clients
        LIST_FOR_EACH( mvd_client_t, client, &mvd->clients, entry ) {
            if( client->cl->state == cs_spawned ) {
                MVD_UpdateClient( client );
            }
        }

        // write this message to demofile
        if( mvd->demorecording ) {
            uint16_t length = LittleShort( msg_read.cursize );
            FS_Write( &length, 2, mvd->demorecording );
            FS_Write( msg_read.data, msg_read.cursize, mvd->demorecording );
        }

update:
        MVD_UpdateLayouts( mvd );
    }

    MVD_UpdateLayouts( &mvd_waitingRoom );
   
    if( mvd_dirty ) {
        MVD_InfoSet( "channels", va( "%d", List_Count( &mvd_channel_list ) ) );
        mvd_dirty = qfalse;
    }
}

static void MVD_GameServerCommand( void ) {
}

void MVD_PrepWorldFrame( void ) {
    mvd_t *mvd;
    edict_t *ent;
    int i;

    // reset events and old origins
    LIST_FOR_EACH( mvd_t, mvd, &mvd_channel_list, entry ) {
        for( i = 1, ent = &mvd->edicts[1]; i < mvd->pool.num_edicts; i++, ent++ ) {
            if( !ent->inuse ) {
                continue;
            }
            if( !( ent->s.renderfx & RF_BEAM ) ) {
                VectorCopy( ent->s.origin, ent->s.old_origin );
            }
            ent->s.event = 0;
        }
    }
}


game_export_t mvd_ge = {
    GAME_API_VERSION,

    MVD_GameInit,
    MVD_GameShutdown,
    MVD_GameSpawnEntities,
    MVD_GameWriteGame,
    MVD_GameReadGame,
    MVD_GameWriteLevel,
    MVD_GameReadLevel,
    MVD_GameClientConnect,
    MVD_GameClientBegin,
    MVD_GameClientUserinfoChanged,
    MVD_GameClientDisconnect,
    MVD_GameClientCommand,
    MVD_GameClientThink,
    MVD_GameRunFrame,
    MVD_GameServerCommand
};

