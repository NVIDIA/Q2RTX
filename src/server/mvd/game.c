/*
Copyright (C) 2003-2006 Andrey Nazarov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

//
// mvd_game.c
//

#include "client.h"

static cvar_t   *mvd_admin_password;
static cvar_t   *mvd_part_filter;
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

static int      mvd_numplayers;

static void MVD_UpdateClient(mvd_client_t *client);

/*
==============================================================================

LAYOUTS

==============================================================================
*/

// clients per screen page
#define PAGE_CLIENTS    16

#define VER_OFS (272 - (int)(sizeof(VERSION_STRING) - 1) * CHAR_WIDTH)

static void MVD_LayoutClients(mvd_client_t *client)
{
    static const char header[] =
        "xv 16 yv 0 string2 \"    Name            RTT Status\"";
    char layout[MAX_STRING_CHARS];
    char buffer[MAX_QPATH];
    const char *status1, *status2;
    size_t len, total;
    mvd_client_t *cl;
    mvd_t *mvd = client->mvd;
    int y, i, prestep, flags;

    // calculate prestep
    if (client->layout_cursor < 0) {
        client->layout_cursor = 0;
    } else if (client->layout_cursor) {
        total = List_Count(&mvd->clients);
        if (client->layout_cursor > total / PAGE_CLIENTS) {
            client->layout_cursor = total / PAGE_CLIENTS;
        }
    }

    prestep = client->layout_cursor * PAGE_CLIENTS;

    memcpy(layout, header, sizeof(header) - 1);
    total = sizeof(header) - 1;

    y = 8;
    i = 0;
    FOR_EACH_MVDCL(cl, mvd) {
        if (++i < prestep) {
            continue;
        }
        if (cl->cl->state < cs_spawned) {
            continue;
        }
        if (cl->target && cl->target != mvd->dummy) {
            status1 = "-> ";
            status2 = cl->target->name;
        } else {
            status1 = "observing";
            status2 = "";
        }
        len = Q_snprintf(buffer, sizeof(buffer),
                         "yv %d string \"%3d %-15.15s %3d %s%s\"",
                         y, i, cl->cl->name, cl->ping, status1, status2);
        if (len >= sizeof(buffer)) {
            continue;
        }
        if (total + len >= sizeof(layout)) {
            break;
        }
        memcpy(layout + total, buffer, len);
        total += len;

        if (y > 8 * PAGE_CLIENTS) {
            break;
        }
        y += 8;
    }

    layout[total] = 0;

    // the very first layout update is reliably delivered
    flags = MSG_CLEAR | MSG_COMPRESS_AUTO;
    if (!client->layout_time) {
        flags |= MSG_RELIABLE;
    }

    // send the layout
    MSG_WriteByte(svc_layout);
    MSG_WriteData(layout, total + 1);
    SV_ClientAddMessage(client->cl, flags);

    client->layout_time = svs.realtime;
}

static int MVD_CountClients(mvd_t *mvd)
{
    mvd_client_t *c;
    int count = 0;

    FOR_EACH_MVDCL(c, mvd) {
        if (c->cl->state == cs_spawned) {
            count++;
        }
    }
    return count;
}

static void MVD_LayoutChannels(mvd_client_t *client)
{
    static const char header[] =
        "xv 32 yv 8 picn inventory "
        "xv %d yv 172 string2 " VERSION_STRING " "
        "xv 0 yv 32 cstring \"\020Channel Chooser\021\""
        "xv 64 yv 48 string2 \"Name         Map     S/P\""
        "yv 56 string \"------------ ------- ---\" xv 56 ";
    static const char nochans[] =
        "yv 72 string \" No active channels.\""
        "yv 80 string \" Please wait until players\""
        "yv 88 string \" connect.\""
        ;
    static const char inactive[] =
        "yv 72 string \" Traffic saving mode.\""
        "yv 80 string \" Press any key to wake\""
        "yv 88 string \" this server up.\""
        ;
    char layout[MAX_STRING_CHARS];
    char buffer[MAX_QPATH];
    mvd_t *mvd;
    size_t len, total;
    int cursor, y;

    total = Q_scnprintf(layout, sizeof(layout),
                        header, VER_OFS);

    // FIXME: improve this
    cursor = List_Count(&mvd_channel_list);
    if (cursor) {
        if (client->layout_cursor < 0) {
            client->layout_cursor = cursor - 1;
        } else if (client->layout_cursor > cursor - 1) {
            client->layout_cursor = 0;
        }

        y = 64;
        cursor = 0;
        FOR_EACH_MVD(mvd) {
            len = Q_snprintf(buffer, sizeof(buffer),
                             "yv %d string%s \"%c%-12.12s %-7.7s %d/%d\" ", y,
                             mvd == client->mvd ? "2" : "",
                             cursor == client->layout_cursor ? 0x8d : 0x20,
                             mvd->name, mvd->mapname,
                             MVD_CountClients(mvd), mvd->numplayers);
            if (len >= sizeof(buffer)) {
                continue;
            }
            if (total + len >= sizeof(layout)) {
                break;
            }
            memcpy(layout + total, buffer, len);
            total += len;
            y += 8;
            if (y > 164) {
                break;
            }

            cursor++;
        }
    } else {
        client->layout_cursor = 0;
        if (mvd_active) {
            memcpy(layout + total, nochans, sizeof(nochans) - 1);
            total += sizeof(nochans) - 1;
        } else {
            memcpy(layout + total, inactive, sizeof(inactive) - 1);
            total += sizeof(inactive) - 1;
        }
    }

    layout[total] = 0;

    // send the layout
    MSG_WriteByte(svc_layout);
    MSG_WriteData(layout, total + 1);
    SV_ClientAddMessage(client->cl, MSG_RELIABLE | MSG_CLEAR | MSG_COMPRESS_AUTO);

    client->layout_time = svs.realtime;
}

#define MENU_ITEMS  10
#define YES "\xD9\xE5\xF3"
#define NO "\xCE\xEF"
#define DEFAULT "\xC4\xE5\xE6\xE1\xF5\xEC\xF4"

static int clamp_menu_cursor(mvd_client_t *client)
{
    if (client->layout_cursor < 0) {
        client->layout_cursor = MENU_ITEMS - 1;
    } else if (client->layout_cursor > MENU_ITEMS - 1) {
        client->layout_cursor = 0;
    }
    return client->layout_cursor;
}

static void MVD_LayoutMenu(mvd_client_t *client)
{
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
        "yv 120 string \"%cIgnore player FOV: %7s\""
        "yv 128 string \" (use 'set uf %d u' in cfg)\""
        "yv 144 string2 \"%cExit menu\""
        "%s xv %d yv 172 string2 " VERSION_STRING;
    char layout[MAX_STRING_CHARS];
    char cur[MENU_ITEMS];
    size_t total;

    memset(cur, 0x20, sizeof(cur));
    cur[clamp_menu_cursor(client)] = 0x8d;

    total = Q_scnprintf(layout, sizeof(layout), format,
                        cur[0], client->target ? "Leave" : "Enter", cur[1],
                        cur[2], MVD_CountClients(client->mvd),
                        cur[3], List_Count(&mvd_channel_list), cur[4],
                        cur[5], (client->uf & UF_MUTE_OBSERVERS) ? YES : NO,
                        cur[6], (client->uf & UF_MUTE_MISC) ? YES : NO,
                        cur[7], (client->uf & UF_MUTE_PLAYERS) ? YES : NO,
                        cur[8], (client->uf & UF_LOCALFOV) ? YES :
                        (client->uf & UF_PLAYERFOV) ? NO " " : DEFAULT,
                        client->uf,
                        cur[9], client->mvd->state == MVD_WAITING ?
                        "xv 0 yv 160 cstring [BUFFERING]" : "",
                        VER_OFS);

    // send the layout
    MSG_WriteByte(svc_layout);
    MSG_WriteData(layout, total + 1);
    SV_ClientAddMessage(client->cl, MSG_RELIABLE | MSG_CLEAR | MSG_COMPRESS_AUTO);

    client->layout_time = svs.realtime;
}

static void MVD_LayoutScores(mvd_client_t *client)
{
    mvd_t *mvd = client->mvd;
    int flags = MSG_CLEAR | MSG_COMPRESS_AUTO;
    const char *layout;

    if (client->layout_type == LAYOUT_OLDSCORES) {
        layout = mvd->oldscores;
    } else {
        layout = mvd->layout;
    }
    if (!layout || !layout[0]) {
        layout = "xv 100 yv 60 string \"<no scoreboard>\"";
    }

    // end-of-match scoreboard is reliably delivered
    if (!client->layout_time || mvd->intermission) {
        flags |= MSG_RELIABLE;
    }

    // send the layout
    MSG_WriteByte(svc_layout);
    MSG_WriteString(layout);
    SV_ClientAddMessage(client->cl, flags);

    client->layout_time = svs.realtime;
}

static void MVD_LayoutFollow(mvd_client_t *client)
{
    mvd_t *mvd = client->mvd;
    const char *name = client->target ? client->target->name : "<no target>";
    char layout[MAX_STRING_CHARS];
    size_t total;

    total = Q_scnprintf(layout, sizeof(layout),
                        "%s string \"[%s] Chasing %s\"",
                        mvd_chase_prefix->string, mvd->name, name);

    // send the layout
    MSG_WriteByte(svc_layout);
    MSG_WriteData(layout, total + 1);
    SV_ClientAddMessage(client->cl, MSG_RELIABLE | MSG_CLEAR);

    client->layout_time = svs.realtime;
}

static void MVD_SetNewLayout(mvd_client_t *client, mvd_layout_t type)
{
    // force an update
    client->layout_type = type;
    client->layout_time = 0;
    client->layout_cursor = 0;
}

static void MVD_SetDefaultLayout(mvd_client_t *client)
{
    mvd_t *mvd = client->mvd;
    mvd_layout_t type;

    if (mvd == &mvd_waitingRoom) {
        type = LAYOUT_CHANNELS;
    } else if (mvd->intermission) {
        type = LAYOUT_SCORES;
    } else if (client->target && !(mvd->flags & MVF_SINGLEPOV)) {
        type = LAYOUT_FOLLOW;
    } else {
        type = LAYOUT_NONE;
    }

    MVD_SetNewLayout(client, type);
}

static void MVD_ToggleLayout(mvd_client_t *client, mvd_layout_t type)
{
    if (client->layout_type == type) {
        MVD_SetDefaultLayout(client);
    } else {
        MVD_SetNewLayout(client, type);
    }
}

static void MVD_SetFollowLayout(mvd_client_t *client)
{
    if (!client->layout_type) {
        MVD_SetDefaultLayout(client);
    } else if (client->layout_type == LAYOUT_FOLLOW) {
        client->layout_time = 0; // force an update
    }
}

// this is the only function that actually writes layouts
static void MVD_UpdateLayouts(mvd_t *mvd)
{
    mvd_client_t *client;

    FOR_EACH_MVDCL(client, mvd) {
        if (client->cl->state != cs_spawned) {
            continue;
        }
        client->ps.stats[STAT_LAYOUTS] = client->layout_type ? 1 : 0;
        switch (client->layout_type) {
        case LAYOUT_FOLLOW:
            if (!client->layout_time) {
                MVD_LayoutFollow(client);
            }
            break;
        case LAYOUT_OLDSCORES:
        case LAYOUT_SCORES:
            if (!client->layout_time || (!mvd->dummy && svs.realtime - client->layout_time > LAYOUT_MSEC)) {
                MVD_LayoutScores(client);
            }
            break;
        case LAYOUT_MENU:
            if (mvd->dirty || !client->layout_time) {
                MVD_LayoutMenu(client);
            }
            break;
        case LAYOUT_CLIENTS:
            if (svs.realtime - client->layout_time > LAYOUT_MSEC) {
                MVD_LayoutClients(client);
            }
            break;
        case LAYOUT_CHANNELS:
            if (mvd_dirty || !client->layout_time) {
                MVD_LayoutChannels(client);
            }
            break;
        default:
            break;
        }
    }

    mvd->dirty = false;
}


/*
==============================================================================

CHASE CAMERA

==============================================================================
*/

void MVD_WriteStringList(mvd_client_t *client, mvd_cs_t *cs)
{
    for (; cs; cs = cs->next) {
        MSG_WriteByte(svc_configstring);
        MSG_WriteShort(cs->index);
        MSG_WriteString(cs->string);
        SV_ClientAddMessage(client->cl, MSG_RELIABLE | MSG_CLEAR);
    }
}

static void MVD_FollowStop(mvd_client_t *client)
{
    mvd_t *mvd = client->mvd;
    int i;

    client->ps.viewangles[ROLL] = 0;

    for (i = 0; i < 3; i++) {
        client->ps.pmove.delta_angles[i] = ANGLE2SHORT(
                                               client->ps.viewangles[i]) - client->lastcmd.angles[i];
    }

    VectorClear(client->ps.kick_angles);
    Vector4Clear(client->ps.blend);
    client->ps.pmove.pm_flags = 0;
    client->ps.pmove.pm_type = mvd->pm_type;
    client->ps.rdflags = 0;
    client->ps.gunindex = 0;
    client->ps.fov = client->fov;

    // send delta configstrings
    if (mvd->dummy)
        MVD_WriteStringList(client, mvd->dummy->configstrings);

    client->clientNum = mvd->clientNum;
    client->oldtarget = client->target;
    client->target = NULL;

    if (client->layout_type == LAYOUT_FOLLOW) {
        MVD_SetDefaultLayout(client);
    }

    MVD_UpdateClient(client);
}

static void MVD_FollowStart(mvd_client_t *client, mvd_player_t *target)
{
    if (client->target == target) {
        return;
    }

    client->oldtarget = client->target;
    client->target = target;

    // send delta configstrings
    MVD_WriteStringList(client, target->configstrings);

    SV_ClientPrintf(client->cl, PRINT_LOW, "[MVD] Chasing %s.\n", target->name);

    MVD_SetFollowLayout(client);
    MVD_UpdateClient(client);
}

static bool MVD_TestTarget(mvd_client_t *client, mvd_player_t *target)
{
    mvd_t *mvd = client->mvd;

    if (!target)
        return false;

    if (!target->inuse)
        return false;

    if (target == mvd->dummy)
        return false;

    if (!client->chase_auto)
        return true;

    return Q_IsBitSet(client->chase_bitmap, target - mvd->players);
}

static mvd_player_t *MVD_FollowNext(mvd_client_t *client, mvd_player_t *from)
{
    mvd_t *mvd = client->mvd;
    mvd_player_t *target;

    if (!mvd->players) {
        return NULL;
    }

    if (!from) {
        from = mvd->players + mvd->maxclients - 1;
    }

    target = from;
    do {
        if (target == mvd->players + mvd->maxclients - 1) {
            target = mvd->players;
        } else {
            target++;
        }
        if (target == from) {
            return NULL;
        }
    } while (!MVD_TestTarget(client, target));

    MVD_FollowStart(client, target);
    return target;
}

static mvd_player_t *MVD_FollowPrev(mvd_client_t *client, mvd_player_t *from)
{
    mvd_t *mvd = client->mvd;
    mvd_player_t *target;

    if (!mvd->players) {
        return NULL;
    }

    if (!from) {
        from = mvd->players;
    }

    target = from;
    do {
        if (target == mvd->players) {
            target = mvd->players + mvd->maxclients - 1;
        } else {
            target--;
        }
        if (target == from) {
            return NULL;
        }
    } while (!MVD_TestTarget(client, target));

    MVD_FollowStart(client, target);
    return target;
}

static mvd_player_t *MVD_MostFollowed(mvd_t *mvd)
{
    int count[MAX_CLIENTS];
    mvd_client_t *other;
    mvd_player_t *player, *target = NULL;
    int i, maxcount = -1;

    memset(count, 0, sizeof(count));

    FOR_EACH_MVDCL(other, mvd) {
        if (other->cl->state == cs_spawned && other->target) {
            count[other->target - mvd->players]++;
        }
    }
    for (i = 0, player = mvd->players; i < mvd->maxclients; i++, player++) {
        if (player->inuse && player != mvd->dummy && maxcount < count[i]) {
            maxcount = count[i];
            target = player;
        }
    }
    return target;
}

static void MVD_UpdateTarget(mvd_client_t *client)
{
    mvd_t *mvd = client->mvd;
    mvd_player_t *target;
    edict_t *ent;
    int i;

    // find new target for effects auto chasecam
    if (client->chase_mask && !mvd->intermission) {
        for (i = 0; i < mvd->maxclients; i++) {
            target = &mvd->players[i];
            if (!target->inuse || target == mvd->dummy) {
                continue;
            }
            ent = &mvd->edicts[i + 1];
            if (ent->s.effects & client->chase_mask) {
                MVD_FollowStart(client, target);
                return;
            }
        }
    }

    // check if current target is still active
    target = client->target;
    if (target) {
        if (target->inuse) {
            return;
        }
        if (!MVD_FollowNext(client, target)) {
            MVD_FollowStop(client);
            return;
        }
    }

    // find the next target for auto chasecam
    target = client->oldtarget;
    if (client->chase_auto) {
        if (MVD_TestTarget(client, target)) {
            MVD_FollowStart(client, target);
        } else {
            MVD_FollowNext(client, target);
        }
        return;
    }

    // if the map has just started, try to return to the previous target
    if (target && target->inuse && client->chase_wait) {
        MVD_FollowStart(client, target);
    }
}

static void MVD_UpdateClient(mvd_client_t *client)
{
    mvd_t *mvd = client->mvd;
    mvd_player_t *target = client->target;
    int i;

    if (!target) {
        int contents = 0;

        // copy stats of the dummy MVD observer
        if (mvd->dummy) {
            for (i = 0; i < MAX_STATS; i++) {
                client->ps.stats[i] = mvd->dummy->ps.stats[i];
            }
        }

        // get contents from world
        if (mvd->cm.cache) {
            vec3_t vieworg;
            VectorMA(client->ps.viewoffset, 0.125f, client->ps.pmove.origin, vieworg);
            contents = CM_PointContents(vieworg, mvd->cm.cache->nodes);
        }

        if (contents & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER))
            client->ps.rdflags = RDF_UNDERWATER;
        else
            client->ps.rdflags = 0;

        if (contents & (CONTENTS_SOLID | CONTENTS_LAVA))
            Vector4Set(client->ps.blend, 1.0f, 0.3f, 0.0f, 0.6f);
        else if (contents & CONTENTS_SLIME)
            Vector4Set(client->ps.blend, 0.0f, 0.1f, 0.05f, 0.6f);
        else if (contents & CONTENTS_WATER)
            Vector4Set(client->ps.blend, 0.5f, 0.3f, 0.2f, 0.4f);
        else
            Vector4Clear(client->ps.blend);
    } else {
        // copy entire player state
        client->ps = target->ps;
        if ((client->uf & UF_LOCALFOV) || (!(client->uf & UF_PLAYERFOV)
                                           && client->ps.fov >= 90)) {
            client->ps.fov = client->fov;
        }
        client->ps.pmove.pm_flags |= PMF_NO_PREDICTION;
        client->ps.pmove.pm_type = PM_FREEZE;
        client->clientNum = target - mvd->players;

        if (target != mvd->dummy) {
            if (mvd_stats_hack->integer && mvd->dummy) {
                // copy stats of the dummy MVD observer
                for (i = 0; i < MAX_STATS; i++) {
                    if (mvd_stats_hack->integer & BIT(i)) {
                        client->ps.stats[i] = mvd->dummy->ps.stats[i];
                    }
                }
            }

            // chasing someone counts as activity
            mvd_last_activity = svs.realtime;
        }
    }

    // override score
    switch (mvd_stats_score->integer) {
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

void MVD_BroadcastPrintf(mvd_t *mvd, int level, int mask, const char *fmt, ...)
{
    va_list     argptr;
    char        text[MAX_STRING_CHARS];
    size_t      len;
    mvd_client_t *other;
    client_t    *cl;

    va_start(argptr, fmt);
    len = Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(text)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    if (level == PRINT_CHAT && mvd_filter_version->integer) {
        char *s;

        while ((s = strstr(text, "!version")) != NULL) {
            s[6] = '0';
        }
    }

    MSG_WriteByte(svc_print);
    MSG_WriteByte(level);
    MSG_WriteData(text, len + 1);

    FOR_EACH_MVDCL(other, mvd) {
        cl = other->cl;
        if (cl->state < cs_spawned) {
            continue;
        }
        if (level < cl->messagelevel) {
            continue;
        }
        if (other->uf & mask) {
            continue;
        }
        SV_ClientAddMessage(cl, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}

static void MVD_SetServerState(client_t *cl, mvd_t *mvd)
{
    cl->gamedir = mvd->gamedir;
    cl->mapname = mvd->mapname;
    cl->configstrings = mvd->configstrings;
    cl->csr = mvd->csr;
    cl->slot = mvd->clientNum;
    cl->cm = &mvd->cm;
    cl->ge = &mvd->ge;
    cl->spawncount = mvd->servercount;
    cl->maxclients = mvd->maxclients;
    if (cl->csr->extended)
        cl->esFlags |= MSG_ES_EXTENSIONS;
    else
        cl->esFlags &= ~MSG_ES_EXTENSIONS;
}

void MVD_SwitchChannel(mvd_client_t *client, mvd_t *mvd)
{
    client_t *cl = client->cl;

    List_Remove(&client->entry);
    List_SeqAdd(&mvd->clients, &client->entry);
    client->mvd = mvd;
    client->begin_time = 0;
    client->target = NULL;
    client->oldtarget = NULL;
    client->chase_mask = 0;
    client->chase_auto = 0;
    client->chase_wait = 0;
    memset(client->chase_bitmap, 0, sizeof(client->chase_bitmap));
    MVD_SetServerState(cl, mvd);

    // needs to reconnect
    MSG_WriteByte(svc_stufftext);
    MSG_WriteString(va("changing map=%s; reconnect\n", mvd->mapname));
    SV_ClientReset(cl);
    SV_ClientAddMessage(cl, MSG_RELIABLE | MSG_CLEAR);
}

static bool MVD_PartFilter(mvd_client_t *client)
{
    unsigned i, delta, treshold;
    float f = mvd_part_filter->value;

    if (!f) {
        return true; // show everyone
    }
    if (f < 0) {
        return false; // hide everyone
    }
    if (client->admin) {
        return true; // show admins
    }
    if (!client->floodHead) {
        return false; // not talked yet
    }
    if (f > 24 * 24 * 60 * 60) {
        return true; // treshold too big
    }

    // take the most recent sample
    i = (client->floodHead - 1) & FLOOD_MASK;
    delta = svs.realtime - client->floodSamples[i];
    treshold = f * 1000;

    return delta < treshold;
}

static void MVD_TrySwitchChannel(mvd_client_t *client, mvd_t *mvd)
{
    if (mvd == client->mvd) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] You are already %s.\n", mvd == &mvd_waitingRoom ?
                        "in the Waiting Room" : "on this channel");
        return; // nothing to do
    }
    if (!CLIENT_COMPATIBLE(mvd->csr, client->cl)) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] This channel is not compatible with your client version.\n");
        return;
    }

    if (client->begin_time) {
        if (svs.realtime - client->begin_time < 2000) {
            SV_ClientPrintf(client->cl, PRINT_HIGH,
                            "[MVD] You may not switch channels too soon.\n");
            return;
        }
        if (MVD_PartFilter(client)) {
            MVD_BroadcastPrintf(client->mvd, PRINT_MEDIUM, UF_MUTE_MISC,
                                "[MVD] %s left the channel\n", client->cl->name);
        }
    }

    MVD_SwitchChannel(client, mvd);
}

static void MVD_Admin_f(mvd_client_t *client)
{
    char *s = mvd_admin_password->string;

    if (client->admin) {
        client->admin = false;
        SV_ClientPrintf(client->cl, PRINT_HIGH, "[MVD] Lost admin status.\n");
        return;
    }

    if (!NET_IsLocalAddress(&client->cl->netchan.remote_address)) {
        if (Cmd_Argc() < 2) {
            SV_ClientPrintf(client->cl, PRINT_HIGH, "Usage: %s <password>\n", Cmd_Argv(0));
            return;
        }
        if (!s[0] || strcmp(s, Cmd_Argv(1))) {
            SV_ClientPrintf(client->cl, PRINT_HIGH, "[MVD] Invalid password.\n");
            return;
        }
    }

    client->admin = true;
    SV_ClientPrintf(client->cl, PRINT_HIGH, "[MVD] Granted admin status.\n");
}

static void MVD_Forward_f(mvd_client_t *client)
{
    mvd_t *mvd = client->mvd;

    if (!client->admin) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] You don't have admin status.\n");
        return;
    }

    if (!mvd->forward_cmd) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] This channel does not support command forwarding.\n");
        return;
    }

    mvd->forward_cmd(client);
}

static void MVD_Say_f(mvd_client_t *client, int argnum)
{
    mvd_t *mvd = client->mvd;
    char text[150], hightext[150];
    mvd_client_t *other;
    client_t *cl;
    unsigned i, j, delta;

    if (mvd_flood_mute->integer && !client->admin) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] Spectators may not talk on this server.\n");
        return;
    }
    if (client->uf & UF_MUTE_OBSERVERS) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] Please turn off ignore mode first.\n");
        return;
    }

    if (client->floodTime) {
        delta = svs.realtime - client->floodTime;
        if (delta < mvd_flood_waitdelay->integer) {
            SV_ClientPrintf(client->cl, PRINT_HIGH,
                            "[MVD] You can't talk for %u more seconds.\n",
                            (mvd_flood_waitdelay->integer - delta) / 1000);
            return;
        }
    }

    j = Cvar_ClampInteger(mvd_flood_msgs, 0, FLOOD_SAMPLES - 1) + 1;
    if (client->floodHead >= j) {
        i = (client->floodHead - j) & FLOOD_MASK;
        delta = svs.realtime - client->floodSamples[i];
        if (delta < mvd_flood_persecond->integer) {
            SV_ClientPrintf(client->cl, PRINT_HIGH,
                            "[MVD] You can't talk for %u seconds.\n",
                            mvd_flood_waitdelay->integer / 1000);
            client->floodTime = svs.realtime;
            return;
        }
    }

    client->floodSamples[client->floodHead & FLOOD_MASK] = svs.realtime;
    client->floodHead++;

    Q_snprintf(text, sizeof(text), "[MVD] %s: %s",
               client->cl->name, Cmd_ArgsFrom(argnum));

    // color text for legacy clients
    for (i = 0; text[i]; i++)
        hightext[i] = text[i] | 128;
    hightext[i] = 0;

    FOR_EACH_MVDCL(other, mvd) {
        cl = other->cl;
        if (cl->state < cs_spawned) {
            continue;
        }
        if (!client->admin && (other->uf & UF_MUTE_OBSERVERS)) {
            continue;
        }

        if (cl->protocol == PROTOCOL_VERSION_Q2PRO &&
            cl->version >= PROTOCOL_VERSION_Q2PRO_SERVER_STATE) {
            SV_ClientPrintf(cl, PRINT_CHAT, "%s\n", text);
        } else {
            SV_ClientPrintf(cl, PRINT_HIGH, "%s\n", hightext);
        }
    }
}

static void MVD_Observe_f(mvd_client_t *client)
{
    mvd_t *mvd = client->mvd;

    if (!mvd->players) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] Please enter a channel first.\n");
        return;
    }

    client->chase_mask = 0;
    client->chase_auto = false;
    client->chase_wait = false;

    if (mvd->intermission) {
        return;
    }

    if (client->target) {
        MVD_FollowStop(client);
        return;
    }

    if (client->oldtarget && client->oldtarget->inuse) {
        MVD_FollowStart(client, client->oldtarget);
        return;
    }

    if (!MVD_FollowNext(client, NULL)) {
        SV_ClientPrintf(client->cl, PRINT_MEDIUM, "[MVD] No players to chase.\n");
    }
}

static mvd_player_t *MVD_SetPlayer(mvd_client_t *client, const char *s)
{
    mvd_t *mvd = client->mvd;
    mvd_player_t *player, *match;
    int i, count;

    // numeric values are just slot numbers
    if (COM_IsUint(s)) {
        i = Q_atoi(s);
        if (i < 0 || i >= mvd->maxclients) {
            SV_ClientPrintf(client->cl, PRINT_HIGH,
                            "[MVD] Player slot number %d is invalid.\n", i);
            return NULL;
        }

        player = &mvd->players[i];
        if (!player->inuse || player == mvd->dummy) {
            SV_ClientPrintf(client->cl, PRINT_HIGH,
                            "[MVD] Player slot number %d is not active.\n", i);
            return NULL;
        }

        return player;
    }

    // check for a name match
    match = NULL;
    count = 0;
    for (i = 0, player = mvd->players; i < mvd->maxclients; i++, player++) {
        if (!player->inuse || !player->name[0] || player == mvd->dummy) {
            continue;
        }
        if (!Q_stricmp(player->name, s)) {
            return player; // exact match
        }
        if (Q_stristr(player->name, s)) {
            match = player; // partial match
            count++;
        }
    }

    if (!match) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] No players matching '%s' found.\n", s);
        return NULL;
    }

    if (count > 1) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] '%s' matches multiple players.\n", s);
        return NULL;
    }

    return match;
}

static void MVD_Follow_f(mvd_client_t *client)
{
    mvd_t *mvd = client->mvd;
    mvd_player_t *player;
    char *s;
    int mask;

    if (!mvd->players) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] Please enter a channel first.\n");
        return;
    }

    if (mvd->intermission) {
        return;
    }

    if (Cmd_Argc() < 2) {
        MVD_Observe_f(client);
        return;
    }

    s = Cmd_Argv(1);
    if (*s == '!') {
        s++;
        switch (*s) {
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
        case '!':
            goto match;
        case 'p':
            if (client->oldtarget) {
                if (client->oldtarget->inuse) {
                    MVD_FollowStart(client, client->oldtarget);
                } else {
                    SV_ClientPrintf(client->cl, PRINT_HIGH,
                                    "[MVD] Previous chase target is not active.\n");
                }
            } else {
                SV_ClientPrintf(client->cl, PRINT_HIGH,
                                "[MVD] You have no previous chase target.\n");
            }
            return;
        default:
            SV_ClientPrintf(client->cl, PRINT_HIGH,
                            "[MVD] Unknown chase target '%s'. Valid targets are: "
                            "q[uad]/i[nvulner]/r[ed_flag]/b[lue_flag]/p[revious_target].\n", s);
            return;
        }
        SV_ClientPrintf(client->cl, PRINT_MEDIUM,
                        "[MVD] Chasing players with '%s' powerup.\n", s);
        client->chase_mask = mask;
        client->chase_auto = false;
        client->chase_wait = false;
        return;
    }

match:
    player = MVD_SetPlayer(client, s);
    if (player) {
        MVD_FollowStart(client, player);
        client->chase_mask = 0;
        client->chase_wait = false;
    }
}

static bool count_chase_bits(mvd_client_t *client)
{
    mvd_t *mvd = client->mvd;
    int i, j, count = 0;

    for (i = 0; i < (mvd->maxclients + CHAR_BIT - 1) / CHAR_BIT; i++)
        if (client->chase_bitmap[i])
            for (j = 0; j < 8; j++)
                if (client->chase_bitmap[i] & BIT(j))
                    count++;

    return count;
}

static void MVD_AutoFollow_f(mvd_client_t *client)
{
    mvd_t *mvd = client->mvd;
    mvd_player_t *player;
    char *s, *p;
    int i, j, argc;

    if (!mvd->players) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] Please enter a channel first.\n");
        return;
    }

    if (mvd->intermission)
        return;

    argc = Cmd_Argc();
    if (argc < 2) {
        i = count_chase_bits(client);
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] Auto chasing is %s (%d target%s).\n",
                        client->chase_auto ? "ON" : "OFF", i, i == 1 ? "" : "s");
        return;
    }

    s = Cmd_Argv(1);
    if (!strcmp(s, "add") || !strcmp(s, "rm") || !strcmp(s, "del")) {
        if (argc < 3) {
            SV_ClientPrintf(client->cl, PRINT_HIGH,
                            "Usage: %s %s <wildcard> [...]\n",
                            Cmd_Argv(0), Cmd_Argv(1));
            return;
        }

        for (i = 2; i < argc; i++) {
            p = Cmd_Argv(i);
            for (j = 0; j < mvd->maxclients; j++) {
                player = &mvd->players[j];
                if (!player->name[0] || player == mvd->dummy)
                    continue;

                if (!Com_WildCmpEx(p, player->name, 0, true))
                    continue;

                if (*s == 'a')
                    Q_SetBit(client->chase_bitmap, j);
                else
                    Q_ClearBit(client->chase_bitmap, j);
            }
        }

        if (*s == 'a') {
            MVD_UpdateTarget(client);
            return;
        }

        if (!count_chase_bits(client))
            client->chase_auto = false;
        return;
    }

    if (!strcmp(s, "set")) {
        memset(client->chase_bitmap, 0, sizeof(client->chase_bitmap));

        for (i = 2; i < argc; i++) {
            j = Q_atoi(Cmd_Argv(i));
            if (j >= 0 && j < mvd->maxclients)
                Q_SetBit(client->chase_bitmap, j);
        }

        if (!count_chase_bits(client))
            client->chase_auto = false;
        return;
    }

    if (!strcmp(s, "list")) {
        char    buffer[1000];
        size_t  total, len;

        total = 0;
        for (i = 0; i < mvd->maxclients; i++) {
            if (!Q_IsBitSet(client->chase_bitmap, i))
                continue;

            player = &mvd->players[i];
            if (player == mvd->dummy)
                continue;

            len = strlen(player->name);
            if (len == 0)
                continue;

            if (total + len + 2 >= sizeof(buffer))
                break;

            if (total) {
                buffer[total + 0] = ',';
                buffer[total + 1] = ' ';
                total += 2;
            }

            memcpy(buffer + total, player->name, len);
            total += len;
        }
        buffer[total] = 0;

        if (total)
            SV_ClientPrintf(client->cl, PRINT_HIGH,
                            "[MVD] Auto chasing %s\n", buffer);
        else
            SV_ClientPrintf(client->cl, PRINT_HIGH,
                            "[MVD] Auto chase list is empty.\n");
        return;
    }

    if (!strcmp(s, "on")) {
        if (!count_chase_bits(client)) {
            SV_ClientPrintf(client->cl, PRINT_HIGH,
                            "[MVD] Please add auto chase targets first.\n");
            return;
        }

        client->chase_mask = 0;
        client->chase_auto = true;
        client->chase_wait = false;
        if (!MVD_TestTarget(client, client->target))
            MVD_FollowNext(client, client->target);
        return;
    }

    if (!strcmp(s, "off")) {
        client->chase_auto = false;
        return;
    }

    SV_ClientPrintf(client->cl, PRINT_HIGH,
                    "[MVD] Usage: %s <add|rm|set|list|on|off> [...]\n",
                    Cmd_Argv(0));
}

static void MVD_Invuse_f(mvd_client_t *client)
{
    mvd_t *mvd;
    int uf = client->uf;

    if (client->layout_type == LAYOUT_MENU) {
        switch (clamp_menu_cursor(client)) {
        case 0:
            MVD_SetDefaultLayout(client);
            MVD_Observe_f(client);
            return;
        case 1:
            MVD_SetNewLayout(client, LAYOUT_SCORES);
            break;
        case 2:
            MVD_SetNewLayout(client, LAYOUT_CLIENTS);
            break;
        case 3:
            MVD_SetNewLayout(client, LAYOUT_CHANNELS);
            break;
        case 4:
            MVD_TrySwitchChannel(client, &mvd_waitingRoom);
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
            client->uf &= ~(UF_LOCALFOV | UF_PLAYERFOV);
            if (uf & UF_LOCALFOV)
                client->uf |= UF_PLAYERFOV;
            else if (!(uf & UF_PLAYERFOV))
                client->uf |= UF_LOCALFOV;
            break;
        case 9:
            MVD_SetDefaultLayout(client);
            break;
        }
        if (uf != client->uf) {
            SV_ClientCommand(client->cl, "set uf %d u\n", client->uf);
            client->layout_time = 0; // force an update
        }
        return;
    }

    if (client->layout_type == LAYOUT_CHANNELS) {
        mvd = LIST_INDEX(mvd_t, client->layout_cursor, &mvd_channel_list, entry);
        if (mvd) {
            MVD_TrySwitchChannel(client, mvd);
        }
        return;
    }
}

static void MVD_Join_f(mvd_client_t *client)
{
    mvd_t *mvd;

    SV_ClientRedirect();
    mvd = MVD_SetChannel(1);
    Com_EndRedirect();

    if (!mvd) {
        return;
    }
    if (mvd->state < MVD_WAITING) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] This channel is not ready yet.\n");
        return;
    }

    MVD_TrySwitchChannel(client, mvd);
}

static void print_channel(client_t *cl, mvd_t *mvd)
{
    mvd_player_t *player;
    char buffer[MAX_QPATH];
    size_t len, total;
    int i;

    total = 0;
    for (i = 0; i < mvd->maxclients; i++) {
        player = &mvd->players[i];
        if (!player->inuse || player == mvd->dummy) {
            continue;
        }
        len = strlen(player->name);
        if (len == 0) {
            continue;
        }
        if (total + len + 2 >= sizeof(buffer)) {
            break;
        }
        if (total) {
            buffer[total + 0] = ',';
            buffer[total + 1] = ' ';
            total += 2;
        }
        memcpy(buffer + total, player->name, len);
        total += len;
    }
    buffer[total] = 0;

    SV_ClientPrintf(cl, PRINT_HIGH,
                    "%2d %-12.12s %-8.8s %3d %3d %s\n", mvd->id,
                    mvd->name, mvd->mapname,
                    List_Count(&mvd->clients),
                    mvd->numplayers, buffer);
}

static void mvd_channel_list_f(mvd_client_t *client)
{
    mvd_t *mvd;

    SV_ClientPrintf(client->cl, PRINT_HIGH,
                    "id name         map      spc plr who is playing\n"
                    "-- ------------ -------- --- --- --------------\n");

    FOR_EACH_MVD(mvd) {
        print_channel(client->cl, mvd);
    }
}

static void MVD_Clients_f(mvd_client_t *client)
{
    // TODO: dump them in console
    client->layout_type = LAYOUT_CLIENTS;
    client->layout_time = 0;
    client->layout_cursor = 0;
}

static void MVD_Commands_f(mvd_client_t *client)
{
    SV_ClientPrintf(client->cl, PRINT_HIGH,
                    "chase [player_id]      toggle chasecam mode\n"
                    "autochase <...>        control automatic chasecam\n"
                    "observe                toggle observer mode\n"
                    "menu                   show main menu\n"
                    "score                  show scoreboard\n"
                    "oldscore               show previous scoreboard\n"
                    "channels               list active channels\n"
                    "join [channel_id]      join specified channel\n"
                    "leave                  go to the Waiting Room\n"
                   );
}

static void MVD_GameClientCommand(edict_t *ent)
{
    mvd_client_t *client = EDICT_MVDCL(ent);
    char *cmd;

    if (client->cl->state < cs_spawned) {
        return;
    }

    cmd = Cmd_Argv(0);
    if (!*cmd) {
        return;
    }

    // don't timeout
    mvd_last_activity = svs.realtime;

    if (!strcmp(cmd, "!mvdadmin")) {
        MVD_Admin_f(client);
        return;
    }
    if (!strcmp(cmd, "fwd")) {
        MVD_Forward_f(client);
        return;
    }
    if (!strcmp(cmd, "say") || !strcmp(cmd, "say_team")) {
        MVD_Say_f(client, 1);
        return;
    }
    if (!strcmp(cmd, "follow") || !strcmp(cmd, "chase")) {
        MVD_Follow_f(client);
        return;
    }
    if (!strcmp(cmd, "autofollow") || !strcmp(cmd, "autochase")) {
        MVD_AutoFollow_f(client);
        return;
    }
    if (!strcmp(cmd, "observe") || !strcmp(cmd, "spectate") ||
        !strcmp(cmd, "observer") || !strcmp(cmd, "spectator") ||
        !strcmp(cmd, "obs") || !strcmp(cmd, "spec")) {
        MVD_Observe_f(client);
        return;
    }
    if (!strcmp(cmd, "inven") || !strcmp(cmd, "menu")) {
        MVD_ToggleLayout(client, LAYOUT_MENU);
        return;
    }
    if (!strcmp(cmd, "invnext")) {
        if (client->layout_type >= LAYOUT_MENU) {
            client->layout_cursor++;
            client->layout_time = 0;
        } else if (!client->mvd->intermission) {
            MVD_FollowNext(client, client->target);
            client->chase_mask = 0;
        }
        return;
    }
    if (!strcmp(cmd, "invprev")) {
        if (client->layout_type >= LAYOUT_MENU) {
            client->layout_cursor--;
            client->layout_time = 0;
        } else if (!client->mvd->intermission) {
            MVD_FollowPrev(client, client->target);
            client->chase_mask = 0;
        }
        return;
    }
    if (!strcmp(cmd, "invuse")) {
        MVD_Invuse_f(client);
        return;
    }
    if (!strcmp(cmd, "help") || !strcmp(cmd, "score")) {
        MVD_ToggleLayout(client, LAYOUT_SCORES);
        return;
    }
    if (!strcmp(cmd, "oldscore") || !strcmp(cmd, "oldscores") ||
        !strcmp(cmd, "lastscore") || !strcmp(cmd, "lastscores")) {
        MVD_ToggleLayout(client, LAYOUT_OLDSCORES);
        return;
    }
    if (!strcmp(cmd, "putaway")) {
        MVD_SetDefaultLayout(client);
        return;
    }
    if (!strcmp(cmd, "channels")) {
        mvd_channel_list_f(client);
        return;
    }
    if (!strcmp(cmd, "clients") || !strcmp(cmd, "players")) {
        MVD_Clients_f(client);
        return;
    }
    if (!strcmp(cmd, "join")) {
        MVD_Join_f(client);
        return;
    }
    if (!strcmp(cmd, "leave")) {
        MVD_TrySwitchChannel(client, &mvd_waitingRoom);
        return;
    }
    if (!strcmp(cmd, "commands")) {
        MVD_Commands_f(client);
        return;
    }

    MVD_Say_f(client, 0);
}

/*
==============================================================================

CONFIGSTRING MANAGEMENT

==============================================================================
*/

void MVD_FreePlayer(mvd_player_t *player)
{
    mvd_cs_t *cs, *next;

    for (cs = player->configstrings; cs; cs = next) {
        next = cs->next;
        Z_Free(cs);
    }
    player->configstrings = NULL;
}

static void reset_unicast_strings(mvd_t *mvd, int index)
{
    mvd_cs_t *cs, **next_p;
    mvd_player_t *player;
    int i;

    for (i = 0; i < mvd->maxclients; i++) {
        player = &mvd->players[i];
        next_p = &player->configstrings;
        for (cs = player->configstrings; cs; cs = cs->next) {
            if (cs->index == index) {
                Com_DPrintf("%s: reset %d on %d\n", __func__, index, i);
                *next_p = cs->next;
                Z_Free(cs);
                break;
            }
            next_p = &cs->next;
        }
    }
}

static void set_player_name(mvd_t *mvd, int index)
{
    mvd_player_t *player;
    char *string, *p;

    string = mvd->configstrings[mvd->csr->playerskins + index];
    player = &mvd->players[index];
    Q_strlcpy(player->name, string, sizeof(player->name));
    p = strchr(player->name, '\\');
    if (p) {
        *p = 0;
    }
}

static void update_player_name(mvd_t *mvd, int index)
{
    mvd_client_t *client;

    // parse player name
    set_player_name(mvd, index);

    // update layouts
    FOR_EACH_MVDCL(client, mvd) {
        if (client->cl->state < cs_spawned) {
            continue;
        }
        if (client->layout_type != LAYOUT_FOLLOW) {
            continue;
        }
        if (client->target == mvd->players + index) {
            client->layout_time = 0;
        }
    }
}

void MVD_SetPlayerNames(mvd_t *mvd)
{
    int i;

    for (i = 0; i < mvd->maxclients; i++) {
        set_player_name(mvd, i);
    }
}

void MVD_UpdateConfigstring(mvd_t *mvd, int index)
{
    char *s = mvd->configstrings[index];
    mvd_client_t *client;

    if (index >= mvd->csr->playerskins && index < mvd->csr->playerskins + mvd->maxclients) {
        // update player name
        update_player_name(mvd, index - mvd->csr->playerskins);
    } else if (index >= mvd->csr->general) {
        // reset unicast versions of this string
        reset_unicast_strings(mvd, index);
    }

    MSG_WriteByte(svc_configstring);
    MSG_WriteShort(index);
    MSG_WriteString(s);

    // broadcast configstring change
    FOR_EACH_MVDCL(client, mvd) {
        if (client->cl->state < cs_primed) {
            continue;
        }
        SV_ClientAddMessage(client->cl, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}

/*
==============================================================================

MISC GAME FUNCTIONS

==============================================================================
*/

void MVD_LinkEdict(mvd_t *mvd, edict_t *ent)
{
    int         index;
    mmodel_t    *cm;
    bsp_t       *cache = mvd->cm.cache;

    if (!cache) {
        return;
    }

    if (ent->s.solid == PACKED_BSP) {
        index = ent->s.modelindex;
        if (index < 1 || index > cache->nummodels) {
            Com_WPrintf("%s: entity %d: bad inline model index: %d\n",
                        __func__, ent->s.number, index);
            return;
        }
        cm = &cache->models[index - 1];
        VectorCopy(cm->mins, ent->mins);
        VectorCopy(cm->maxs, ent->maxs);
        ent->solid = SOLID_BSP;
    } else if (ent->s.solid) {
        MSG_UnpackSolid16(ent->s.solid, ent->mins, ent->maxs);
        ent->solid = SOLID_BBOX;
    } else {
        VectorClear(ent->mins);
        VectorClear(ent->maxs);
        ent->solid = SOLID_NOT;
    }

    SV_LinkEdict(&mvd->cm, ent);
}

void MVD_RemoveClient(client_t *client)
{
    int index = client - svs.client_pool;
    mvd_client_t *cl = &mvd_clients[index];

    List_Remove(&cl->entry);

    memset(cl, 0, sizeof(*cl));
    cl->cl = client;
}

static void MVD_GameInit(void)
{
    mvd_t *mvd = &mvd_waitingRoom;
    edict_t *edicts;
    cvar_t *mvd_default_map;
    char buffer[MAX_QPATH];
    unsigned checksum;
    bsp_t *bsp;
    int i;
    int ret;

    Com_Printf("----- MVD_GameInit -----\n");

    mvd_admin_password = Cvar_Get("mvd_admin_password", "", CVAR_PRIVATE);
    mvd_part_filter = Cvar_Get("mvd_part_filter", "0", 0);
    mvd_flood_msgs = Cvar_Get("flood_msgs", "4", 0);
    mvd_flood_persecond = Cvar_Get("flood_persecond", "4", 0);   // FIXME: rename this
    mvd_flood_persecond->changed = sv_sec_timeout_changed;
    mvd_flood_persecond->changed(mvd_flood_persecond);
    mvd_flood_waitdelay = Cvar_Get("flood_waitdelay", "10", 0);
    mvd_flood_waitdelay->changed = sv_sec_timeout_changed;
    mvd_flood_waitdelay->changed(mvd_flood_waitdelay);
    mvd_flood_mute = Cvar_Get("flood_mute", "0", 0);
    mvd_filter_version = Cvar_Get("mvd_filter_version", "0", 0);
    mvd_default_map = Cvar_Get("mvd_default_map", "q2dm1", CVAR_LATCH);
    mvd_stats_score = Cvar_Get("mvd_stats_score", "0", 0);
    mvd_stats_hack = Cvar_Get("mvd_stats_hack", "0", 0);
    mvd_freeze_hack = Cvar_Get("mvd_freeze_hack", "1", 0);
    mvd_chase_prefix = Cvar_Get("mvd_chase_prefix", "xv 0 yb -64", 0);
    Cvar_Set("g_features", va("%d", MVD_FEATURES));

    mvd_clients = MVD_Mallocz(sizeof(mvd_clients[0]) * sv_maxclients->integer);
    edicts = MVD_Mallocz(sizeof(edicts[0]) * (sv_maxclients->integer + 1));

    for (i = 0; i < sv_maxclients->integer; i++) {
        mvd_clients[i].cl = &svs.client_pool[i];
        edicts[i + 1].client = (gclient_t *)&mvd_clients[i];
    }

    mvd_ge.edicts = edicts;
    mvd_ge.edict_size = sizeof(edicts[0]);
    mvd_ge.num_edicts = sv_maxclients->integer + 1;
    mvd_ge.max_edicts = sv_maxclients->integer + 1;

    Q_snprintf(buffer, sizeof(buffer),
               "maps/%s.bsp", mvd_default_map->string);

    ret = BSP_Load(buffer, &bsp);
    if (!bsp) {
        Com_EPrintf("Couldn't load %s for the Waiting Room: %s\n",
                    buffer, BSP_ErrorString(ret));
        Cvar_Reset(mvd_default_map);
        strcpy(buffer, "maps/q2dm1.bsp");
        checksum = 80717714;
        VectorSet(mvd->spawnOrigin, 984, 192, 784);
        VectorSet(mvd->spawnAngles, 25, 72, 0);
    } else {
        // get the spectator spawn point
        MVD_ParseEntityString(mvd, bsp->entitystring);
        checksum = bsp->checksum;
        BSP_Free(bsp);
    }

    strcpy(mvd->name, "Waiting Room");
    Cvar_VariableStringBuffer("game", mvd->gamedir, sizeof(mvd->gamedir));
    Q_strlcpy(mvd->mapname, mvd_default_map->string, sizeof(mvd->mapname));
    List_Init(&mvd->clients);

    strcpy(mvd->configstrings[CS_NAME], "Waiting Room");
    strcpy(mvd->configstrings[CS_SKY], "unit1_");
    strcpy(mvd->configstrings[CS_MAXCLIENTS_OLD], "8");
    sprintf(mvd->configstrings[CS_MAPCHECKSUM_OLD], "%d", checksum);
    strcpy(mvd->configstrings[CS_MODELS_OLD + 1], buffer);
    strcpy(mvd->configstrings[CS_LIGHTS_OLD], "m");

    mvd->dummy = &mvd_dummy;
    mvd->pm_type = PM_FREEZE;
    mvd->servercount = sv.spawncount;
    mvd->csr = &cs_remap_old;

    // set serverinfo variables
    SV_InfoSet("mapname", mvd->mapname);
    //SV_InfoSet("gamedir", "gtv");
    SV_InfoSet("gamename", "gtv");
    SV_InfoSet("gamedate", __DATE__);
    MVD_InfoSet("mvd_channels", "0");
    MVD_InfoSet("mvd_players", "0");
    mvd_numplayers = 0;
}

static void MVD_GameShutdown(void)
{
    Com_Printf("----- MVD_GameShutdown -----\n");

    MVD_Shutdown();

    mvd_ge.edicts = NULL;
    mvd_ge.edict_size = 0;
    mvd_ge.num_edicts = 0;
    mvd_ge.max_edicts = 0;

    Cvar_Set("g_features", "0");
}

static void MVD_GameSpawnEntities(const char *mapname, const char *entstring, const char *spawnpoint)
{
}
static void MVD_GameWriteGame(const char *filename, qboolean autosave)
{
}
static void MVD_GameReadGame(const char *filename)
{
}
static void MVD_GameWriteLevel(const char *filename)
{
}
static void MVD_GameReadLevel(const char *filename)
{
}

static qboolean MVD_GameClientConnect(edict_t *ent, char *userinfo)
{
    mvd_client_t *client = EDICT_MVDCL(ent);
    mvd_t *mvd = NULL;

    // if there is exactly one active channel, assign them to it,
    // otherwise, assign to Waiting Room
    if (LIST_SINGLE(&mvd_channel_list)) {
        mvd = LIST_FIRST(mvd_t, &mvd_channel_list, entry);
    }
    if (!mvd || !CLIENT_COMPATIBLE(mvd->csr, client->cl)) {
        mvd = &mvd_waitingRoom;
    }
    List_SeqAdd(&mvd->clients, &client->entry);
    client->mvd = mvd;

    // override server state
    MVD_SetServerState(client->cl, mvd);

    // don't timeout
    mvd_last_activity = svs.realtime;

    return true;
}

static void MVD_GameClientBegin(edict_t *ent)
{
    mvd_client_t *client = EDICT_MVDCL(ent);
    mvd_t *mvd = client->mvd;
    mvd_player_t *target;

    client->floodTime = 0;
    client->floodHead = 0;
    memset(&client->lastcmd, 0, sizeof(client->lastcmd));
    memset(&client->ps, 0, sizeof(client->ps));
    client->jump_held = 0;
    client->layout_type = LAYOUT_NONE;
    client->layout_time = 0;
    client->layout_cursor = 0;
    client->notified = false;

    // skip notifications for local clients
    if (NET_IsLocalAddress(&client->cl->netchan.remote_address))
        client->notified = true;

    // skip notifications for Waiting Room channel
    if (mvd == &mvd_waitingRoom)
        client->notified = true;

    if (!client->begin_time) {
        if (MVD_PartFilter(client)) {
            MVD_BroadcastPrintf(mvd, PRINT_MEDIUM, UF_MUTE_MISC,
                                "[MVD] %s entered the channel\n", client->cl->name);
        }
        target = MVD_MostFollowed(mvd);
    } else if (mvd->flags & MVF_SINGLEPOV) {
        target = MVD_MostFollowed(mvd);
    } else {
        target = client->oldtarget;
    }

    client->target = NULL;
    client->begin_time = svs.realtime;

    MVD_SetDefaultLayout(client);

    if (mvd->intermission) {
        // force them to chase dummy MVD client
        client->target = mvd->dummy;
        MVD_SetFollowLayout(client);
        MVD_UpdateClient(client);
    } else if (target && target->inuse) {
        // start normal chase cam mode
        MVD_FollowStart(client, target);
    } else {
        // spawn the spectator
        VectorScale(mvd->spawnOrigin, 8, client->ps.pmove.origin);
        VectorCopy(mvd->spawnAngles, client->ps.viewangles);
        MVD_FollowStop(client);

        // if the map has just changed, player might not have spawned yet.
        // save the old target and try to return to it later.
        client->oldtarget = target;
        client->chase_wait = true;
    }

    mvd_dirty = true;
}

static void MVD_GameClientUserinfoChanged(edict_t *ent, char *userinfo)
{
    mvd_client_t *client = EDICT_MVDCL(ent);
    int fov;

    client->uf = Q_atoi(Info_ValueForKey(userinfo, "uf"));

    fov = Q_atoi(Info_ValueForKey(userinfo, "fov"));
    if (fov < 1) {
        fov = 90;
    } else if (fov > 160) {
        fov = 160;
    }
    client->fov = fov;
    if (!client->target)
        client->ps.fov = fov;
}

void MVD_GameClientNameChanged(edict_t *ent, const char *name)
{
    mvd_client_t *client = EDICT_MVDCL(ent);
    client_t *cl = client->cl;

    if (client->begin_time && MVD_PartFilter(client)) {
        MVD_BroadcastPrintf(client->mvd, PRINT_MEDIUM, UF_MUTE_MISC,
                            "[MVD] %s changed name to %s\n", cl->name, name);
    }
}

// called early from SV_DropClient to prevent multiple disconnect messages
void MVD_GameClientDrop(edict_t *ent, const char *prefix, const char *reason)
{
    mvd_client_t *client = EDICT_MVDCL(ent);
    client_t *cl = client->cl;

    if (client->begin_time && MVD_PartFilter(client)) {
        MVD_BroadcastPrintf(client->mvd, PRINT_MEDIUM, UF_MUTE_MISC,
                            "[MVD] %s%s%s\n", cl->name, prefix, reason);
    }

    client->begin_time = 0;
}

static void MVD_GameClientDisconnect(edict_t *ent)
{
    mvd_client_t *client = EDICT_MVDCL(ent);
    client_t *cl = client->cl;

    if (client->begin_time && MVD_PartFilter(client)) {
        MVD_BroadcastPrintf(client->mvd, PRINT_MEDIUM, UF_MUTE_MISC,
                            "[MVD] %s disconnected\n", cl->name);
    }

    client->begin_time = 0;
}

static mvd_player_t *MVD_HitPlayer(mvd_client_t *client)
{
    mvd_t *mvd = client->mvd;
    trace_t trace;
    vec3_t start, end, forward;
    mvd_player_t *player, *target;
    edict_t *ent;
    float fraction;
    int i;

    if (!mvd->players)
        return NULL;

    if (mvd->intermission)
        return NULL;

    VectorMA(client->ps.viewoffset, 0.125f, client->ps.pmove.origin, start);
    AngleVectors(client->ps.viewangles, forward, NULL, NULL);
    VectorMA(start, 8192, forward, end);

    if (mvd->cm.cache) {
        CM_BoxTrace(&trace, start, end, vec3_origin, vec3_origin,
                    mvd->cm.cache->nodes, CONTENTS_SOLID);
        fraction = trace.fraction;
    } else {
        fraction = 1;
    }

    target = NULL;
    for (i = 0; i < mvd->maxclients; i++) {
        player = &mvd->players[i];
        if (!player->inuse || player == mvd->dummy)
            continue;

        ent = &mvd->edicts[i + 1];
        if (!ent->inuse)
            continue;

        if (ent->solid != SOLID_BBOX)
            continue;

        CM_TransformedBoxTrace(&trace, start, end, vec3_origin, vec3_origin,
                               CM_HeadnodeForBox(ent->mins, ent->maxs),
                               CONTENTS_MONSTER, ent->s.origin, vec3_origin);

        if (trace.fraction < fraction) {
            fraction = trace.fraction;
            target = player;
        }
    }

    return target;
}

static trace_t q_gameabi MVD_Trace(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end)
{
    trace_t trace;

    memset(&trace, 0, sizeof(trace));
    VectorCopy(end, trace.endpos);
    trace.fraction = 1;

    return trace;
}

static int MVD_PointContents(const vec3_t p)
{
    return 0;
}

static void MVD_GameClientThink(edict_t *ent, usercmd_t *cmd)
{
    mvd_client_t *client = EDICT_MVDCL(ent);
    usercmd_t *old = &client->lastcmd;
    pmove_t pm;

    if (cmd->buttons != old->buttons
        || cmd->forwardmove != old->forwardmove
        || cmd->sidemove != old->sidemove
        || cmd->upmove != old->upmove) {
        // don't timeout
        mvd_last_activity = svs.realtime;
    }

    if ((cmd->buttons & ~old->buttons) & BUTTON_ATTACK) {
        mvd_player_t *hit;

        // small hack to allow picking up targets by aim
        if (client->target == NULL && (hit = MVD_HitPlayer(client)) != NULL) {
            client->oldtarget = hit;
        }

        MVD_Observe_f(client);
    }

    if (client->target) {
        if (cmd->upmove >= 10) {
            if (client->jump_held < 1) {
                if (!client->mvd->intermission) {
                    MVD_FollowNext(client, client->target);
                    client->chase_mask = 0;
                }
                client->jump_held = 1;
            }
        } else if (cmd->upmove <= -10) {
            if (client->jump_held > -1) {
                if (!client->mvd->intermission) {
                    MVD_FollowPrev(client, client->target);
                    client->chase_mask = 0;
                }
                client->jump_held = -1;
            }
        } else {
            client->jump_held = 0;
        }
    } else {
        memset(&pm, 0, sizeof(pm));
        pm.trace = MVD_Trace;
        pm.pointcontents = MVD_PointContents;
        pm.s = client->ps.pmove;
        pm.cmd = *cmd;

        PF_Pmove(&pm);

        client->ps.pmove = pm.s;
        if (pm.s.pm_type != PM_FREEZE) {
            VectorCopy(pm.viewangles, client->ps.viewangles);
        }
    }

    *old = *cmd;
}

static void MVD_IntermissionStart(mvd_t *mvd)
{
    mvd_client_t *client;

    // set this early so MVD_SetDefaultLayout works
    mvd->intermission = true;

#if 0
    // save oldscores
    // FIXME: unfortunately this will also reset oldscores during
    // match timeout with certain mods (OpenTDM should work fine though)
    Q_strlcpy(mvd->oldscores, mvd->layout, sizeof(mvd->oldscores));
#endif

    // force all clients to switch to the MVD dummy
    // and open the scoreboard, unless they had some special layout up
    FOR_EACH_MVDCL(client, mvd) {
        if (client->cl->state != cs_spawned) {
            continue;
        }
        client->oldtarget = client->target;
        client->target = mvd->dummy;
        if (client->layout_type < LAYOUT_SCORES) {
            MVD_SetDefaultLayout(client);
        }
    }
}

static void MVD_IntermissionStop(mvd_t *mvd)
{
    mvd_client_t *client;
    mvd_player_t *target;

    // set this early so MVD_SetDefaultLayout works
    mvd->intermission = false;

    // force all clients to switch to previous mode
    // and close the scoreboard
    FOR_EACH_MVDCL(client, mvd) {
        if (client->cl->state != cs_spawned) {
            continue;
        }
        if (client->layout_type == LAYOUT_SCORES) {
            client->layout_type = 0;
        }
        target = client->oldtarget;
        if (target && target->inuse) {
            // start normal chase cam mode
            MVD_FollowStart(client, target);
        } else {
            MVD_FollowStop(client);
        }
        client->oldtarget = NULL;
    }
}

static void MVD_NotifyClient(mvd_client_t *client)
{
    mvd_t *mvd = client->mvd;

    if (client->notified)
        return;

    if (svs.realtime - client->begin_time < 2000)
        return;

    // notify them if visibility data is missing
    if (!mvd->cm.cache) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] GTV server doesn't have this map!\n");
    }

    // notify them if channel is in waiting state
    if (mvd->state == MVD_WAITING) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] Buffering data, please wait...\n");
    }

    client->notified = true;
}

// called just after new frame is parsed
void MVD_UpdateClients(mvd_t *mvd)
{
    mvd_client_t *client;
    bool intermission = mvd_freeze_hack->integer
        && mvd->dummy && mvd->dummy->ps.pmove.pm_type == PM_FREEZE;

    // check for intermission
    if (!mvd->intermission && intermission)
        MVD_IntermissionStart(mvd);
    else if (mvd->intermission && !intermission)
        MVD_IntermissionStop(mvd);

    // update UDP clients
    FOR_EACH_MVDCL(client, mvd) {
        if (client->cl->state == cs_spawned) {
            MVD_UpdateTarget(client);
            MVD_UpdateClient(client);
            MVD_NotifyClient(client);
        }
    }
}

static void MVD_WriteDemoMessage(mvd_t *mvd)
{
    uint16_t msglen;
    int ret;

    msglen = LittleShort(msg_read.cursize);
    ret = FS_Write(&msglen, 2, mvd->demorecording);
    if (ret != 2)
        goto fail;
    ret = FS_Write(msg_read.data, msg_read.cursize, mvd->demorecording);
    if (ret == msg_read.cursize)
        return;

fail:
    Com_EPrintf("[%s] Couldn't write demo: %s\n", mvd->name, Q_ErrorString(ret));
    MVD_StopRecord(mvd);
}

static void MVD_GameRunFrame(void)
{
    mvd_t *mvd, *next;
    int numplayers = 0;

    LIST_FOR_EACH_SAFE(mvd_t, mvd, next, &mvd_channel_list, entry) {
        if (setjmp(mvd_jmpbuf)) {
            continue;
        }

        // parse stream
        if (!mvd->read_frame(mvd)) {
            goto update;
        }

        // write this message to demofile
        if (mvd->demorecording) {
            MVD_WriteDemoMessage(mvd);
        }

update:
        MVD_UpdateLayouts(mvd);
        numplayers += mvd->numplayers;
    }

    MVD_UpdateLayouts(&mvd_waitingRoom);

    if (mvd_dirty) {
        MVD_InfoSet("mvd_channels", va("%d", List_Count(&mvd_channel_list)));
        mvd_dirty = false;
    }

    if (numplayers != mvd_numplayers) {
        MVD_InfoSet("mvd_players", va("%d", numplayers));
        mvd_numplayers = numplayers;
        mvd_dirty = true; // update layouts next frame
    }
}

static void MVD_GameServerCommand(void)
{
}

void MVD_PrepWorldFrame(void)
{
    mvd_t *mvd;
    edict_t *ent;
    int i;

    // reset events and old origins
    FOR_EACH_MVD(mvd) {
        for (i = 1; i < mvd->ge.num_edicts; i++) {
            ent = &mvd->edicts[i];
            if (!ent->inuse) {
                continue;
            }
            if (!(ent->s.renderfx & RF_BEAM)) {
                VectorCopy(ent->s.origin, ent->s.old_origin);
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

