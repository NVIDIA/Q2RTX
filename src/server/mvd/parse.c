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
// mvd_parse.c
//

#include "client.h"
#include "server/mvd/protocol.h"

static bool match_ended_hack;

#if USE_DEBUG
#define SHOWNET(level, ...) \
    if (mvd_shownet->integer > level) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__)

static const char *MVD_ServerCommandString(int cmd)
{
    switch (cmd) {
    case -1: return "END OF MESSAGE";
    default: return "UNKNOWN COMMAND";
#define M(x) \
        case mvd_##x: return "mvd_" #x;
        M(bad)
        M(nop)
        M(serverdata)
        M(configstring)
        M(frame)
        M(unicast)
        M(unicast_r)
        M(multicast_all)
        M(multicast_pvs)
        M(multicast_phs)
        M(multicast_all_r)
        M(multicast_pvs_r)
        M(multicast_phs_r)
        M(sound)
        M(print)
    }
}
#else
#define SHOWNET(...)
#endif

void MVD_ParseEntityString(mvd_t *mvd, const char *data)
{
    const char *key, *value;
    char classname[MAX_QPATH];
    vec3_t origin;
    vec3_t angles;

    while (1) {
        key = COM_Parse(&data);
        if (!data) {
            break;
        }
        if (key[0] != '{') {
            Com_WPrintf("%s: found %s when expecting {\n", __func__, key);
            return;
        }

        classname[0] = 0;
        VectorClear(origin);
        VectorClear(angles);
        while (1) {
            key = COM_Parse(&data);
            if (key[0] == '}') {
                break;
            }
            value = COM_Parse(&data);
            if (!data) {
                Com_WPrintf("%s: EOF without closing brace\n", __func__);
                return;
            }
            if (value[0] == '}') {
                Com_WPrintf("%s: closing brace without data\n", __func__);
                return;
            }

            if (!strcmp(key, "classname")) {
                Q_strlcpy(classname, value, sizeof(classname));
            } else if (!strcmp(key, "origin")) {
                sscanf(value, "%f %f %f", &origin[0], &origin[1], &origin[2]);
            } else if (!strcmp(key, "angles")) {
                sscanf(value, "%f %f", &angles[0], &angles[1]);
            } else if (!strcmp(key, "angle")) {
                angles[1] = Q_atof(value);
            }
        }

        if (strncmp(classname, "info_player_", 12)) {
            continue;
        }

        if (!strcmp(classname + 12, "intermission")) {
            VectorCopy(origin, mvd->spawnOrigin);
            VectorCopy(angles, mvd->spawnAngles);
            break;
        }

        if (!strcmp(classname + 12, "start") ||
            !strcmp(classname + 12, "deathmatch")) {
            VectorCopy(origin, mvd->spawnOrigin);
            VectorCopy(angles, mvd->spawnAngles);
        }
    }
}

static void MVD_ParseMulticast(mvd_t *mvd, mvd_ops_t op, int extrabits)
{
    mvd_client_t    *client;
    client_t    *cl;
    byte        mask[VIS_MAX_BYTES];
    mleaf_t     *leaf1 = NULL, *leaf2;
    vec3_t      org;
    bool        reliable = false;
    byte        *data;
    int         length, leafnum;

    length = MSG_ReadByte();
    length |= extrabits << 8;

    switch (op) {
    case mvd_multicast_all_r:
        reliable = true;
        // intentional fallthrough
    case mvd_multicast_all:
        break;
    case mvd_multicast_phs_r:
        reliable = true;
        // intentional fallthrough
    case mvd_multicast_phs:
        leafnum = MSG_ReadWord();
        if (!mvd->demoseeking) {
            leaf1 = CM_LeafNum(&mvd->cm, leafnum);
            BSP_ClusterVis(mvd->cm.cache, mask, leaf1->cluster, DVIS_PHS);
        }
        break;
    case mvd_multicast_pvs_r:
        reliable = true;
        // intentional fallthrough
    case mvd_multicast_pvs:
        leafnum = MSG_ReadWord();
        if (!mvd->demoseeking) {
            leaf1 = CM_LeafNum(&mvd->cm, leafnum);
            BSP_ClusterVis(mvd->cm.cache, mask, leaf1->cluster, DVIS_PVS);
        }
        break;
    default:
        MVD_Destroyf(mvd, "bad op");
    }

    // skip data payload
    data = MSG_ReadData(length);
    if (!data) {
        MVD_Destroyf(mvd, "read past end of message");
    }

    if (mvd->demoseeking)
        return;

    // send the data to all relevent clients
    FOR_EACH_MVDCL(client, mvd) {
        cl = client->cl;
        if (cl->state < cs_primed) {
            continue;
        }

        // do not send unreliables to connecting clients
        if (!reliable && !CLIENT_ACTIVE(cl)) {
            continue;
        }

        if (leaf1) {
            VectorScale(client->ps.pmove.origin, 0.125f, org);
            leaf2 = CM_PointLeaf(&mvd->cm, org);
            if (!CM_AreasConnected(&mvd->cm, leaf1->area, leaf2->area))
                continue;
            if (leaf2->cluster == -1)
                continue;
            if (!Q_IsBitSet(mask, leaf2->cluster))
                continue;
        }

        cl->AddMessage(cl, data, length, reliable);
    }
}

static void MVD_UnicastSend(mvd_t *mvd, bool reliable, byte *data, size_t length, mvd_player_t *player)
{
    mvd_player_t *target;
    mvd_client_t *client;
    client_t *cl;

    // send to all relevant clients
    FOR_EACH_MVDCL(client, mvd) {
        cl = client->cl;
        if (cl->state < cs_spawned) {
            continue;
        }
        target = client->target ? client->target : mvd->dummy;
        if (target == player) {
            cl->AddMessage(cl, data, length, reliable);
        }
    }
}

static void MVD_UnicastLayout(mvd_t *mvd, mvd_player_t *player)
{
    mvd_client_t *client;

    if (mvd->dummy && player != mvd->dummy) {
        MSG_ReadString(NULL, 0);
        return; // we don't care about others
    }

    MSG_ReadString(mvd->layout, sizeof(mvd->layout));

    // HACK: if we got "match ended" string this frame, save oldscores
    if (match_ended_hack) {
        Q_strlcpy(mvd->oldscores, mvd->layout, sizeof(mvd->oldscores));
    }

    if (mvd->demoseeking || !mvd->dummy)
        return;

    // force an update to all relevant clients
    FOR_EACH_MVDCL(client, mvd) {
        if (client->cl->state < cs_spawned) {
            continue;
        }
        if (client->layout_type == LAYOUT_SCORES) {
            client->layout_time = 0;
        }
    }
}

static void MVD_UnicastString(mvd_t *mvd, bool reliable, mvd_player_t *player)
{
    int index;
    char string[MAX_QPATH];
    mvd_cs_t *cs;
    byte *data;
    size_t readcount, length;

    data = msg_read.data + msg_read.readcount - 1;
    readcount = msg_read.readcount - 1;

    index = MSG_ReadWord();
    length = MSG_ReadString(string, sizeof(string));

    if (index < 0 || index >= mvd->csr->end) {
        MVD_Destroyf(mvd, "%s: bad index: %d", __func__, index);
    }
    if (index < mvd->csr->general) {
        Com_DPrintf("%s: common configstring: %d\n", __func__, index);
        return;
    }
    if (length >= sizeof(string)) {
        Com_DPrintf("%s: oversize configstring: %d\n", __func__, index);
        return;
    }

    for (cs = player->configstrings; cs; cs = cs->next) {
        if (cs->index == index) {
            break;
        }
    }
    if (!cs) {
        cs = MVD_Malloc(sizeof(*cs) + MAX_QPATH - 1);
        cs->index = index;
        cs->next = player->configstrings;
        player->configstrings = cs;
    }

    memcpy(cs->string, string, length + 1);

    if (mvd->demoseeking)
        return;

    length = msg_read.readcount - readcount;
    MVD_UnicastSend(mvd, reliable, data, length, player);
}

static void MVD_UnicastPrint(mvd_t *mvd, bool reliable, mvd_player_t *player)
{
    int level;
    byte *data;
    size_t readcount, length;
    mvd_client_t *client;
    client_t *cl;
    mvd_player_t *target;

    data = msg_read.data + msg_read.readcount - 1;
    readcount = msg_read.readcount - 1;

    level = MSG_ReadByte();
    MSG_ReadString(NULL, 0);

    if (mvd->demoseeking)
        return;

    length = msg_read.readcount - readcount;

    // send to all relevant clients
    FOR_EACH_MVDCL(client, mvd) {
        cl = client->cl;
        if (cl->state < cs_spawned) {
            continue;
        }
        if (level < cl->messagelevel) {
            continue;
        }
        if (level == PRINT_CHAT && (client->uf & UF_MUTE_PLAYERS)) {
            continue;
        }
        // decide if message should be routed or not
        target = (client->target && !(mvd->flags & MVF_NOMSGS)) ? client->target : mvd->dummy;
        if (target == player) {
            cl->AddMessage(cl, data, length, reliable);
        }
    }
}

static void MVD_UnicastStuff(mvd_t *mvd, bool reliable, mvd_player_t *player)
{
    char string[8];
    byte *data;
    size_t readcount, length;

    if (mvd->demoseeking) {
        MSG_ReadString(NULL, 0);
        return;
    }

    data = msg_read.data + msg_read.readcount - 1;
    readcount = msg_read.readcount - 1;

    MSG_ReadString(string, sizeof(string));
    if (strncmp(string, "play ", 5)) {
        return;
    }

    length = msg_read.readcount - readcount;
    MVD_UnicastSend(mvd, reliable, data, length, player);
}

/*
MVD_ParseUnicast

Attempt to parse the datagram and find custom configstrings,
layouts, etc. Give up as soon as unknown command byte is encountered.
*/
static void MVD_ParseUnicast(mvd_t *mvd, mvd_ops_t op, int extrabits)
{
    int clientNum;
    size_t length, last;
    mvd_player_t *player;
    byte *data;
    bool reliable;
    int cmd;

    length = MSG_ReadByte();
    length |= extrabits << 8;
    clientNum = MSG_ReadByte();

    if (clientNum < 0 || clientNum >= mvd->maxclients) {
        MVD_Destroyf(mvd, "%s: bad number: %d", __func__, clientNum);
    }

    last = msg_read.readcount + length;
    if (last > msg_read.cursize) {
        MVD_Destroyf(mvd, "%s: read past end of message", __func__);
    }

    player = &mvd->players[clientNum];

    reliable = op == mvd_unicast_r;

    while (msg_read.readcount < last) {
        cmd = MSG_ReadByte();

        SHOWNET(1, "%3zu:%s\n", msg_read.readcount - 1, MSG_ServerCommandString(cmd));

        switch (cmd) {
        case svc_layout:
            MVD_UnicastLayout(mvd, player);
            break;
        case svc_configstring:
            MVD_UnicastString(mvd, reliable, player);
            break;
        case svc_print:
            MVD_UnicastPrint(mvd, reliable, player);
            break;
        case svc_stufftext:
            MVD_UnicastStuff(mvd, reliable, player);
            break;
        default:
            SHOWNET(1, "%3zu:SKIPPING UNICAST\n", msg_read.readcount - 1);
            // send remaining data and return
            data = msg_read.data + msg_read.readcount - 1;
            length = last - msg_read.readcount + 1;
            if (!mvd->demoseeking)
                MVD_UnicastSend(mvd, reliable, data, length, player);
            msg_read.readcount = last;
            return;
        }
    }

    SHOWNET(1, "%3zu:END OF UNICAST\n", msg_read.readcount);

    if (msg_read.readcount > last) {
        MVD_Destroyf(mvd, "%s: read past end of unicast", __func__);
    }
}

/*
MVD_ParseSound

Entity positioned sounds need special handling since origins need to be
explicitly specified for entities out of client PVS, and not all clients
are able to postition sounds on BSP models properly.

FIXME: this duplicates code in sv_game.c
*/
static void MVD_ParseSound(mvd_t *mvd, int extrabits)
{
    int         flags, index;
    int         volume, attenuation, offset, sendchan;
    int         entnum;
    vec3_t      origin, org;
    mvd_client_t        *client;
    client_t    *cl;
    byte        mask[VIS_MAX_BYTES];
    mleaf_t     *leaf1, *leaf2;
    message_packet_t    *msg;
    edict_t     *entity;
    int         i;

    flags = MSG_ReadByte();
    if (mvd->csr->extended && flags & SND_INDEX16)
        index = MSG_ReadWord();
    else
        index = MSG_ReadByte();

    volume = attenuation = offset = 0;
    if (flags & SND_VOLUME)
        volume = MSG_ReadByte();
    if (flags & SND_ATTENUATION)
        attenuation = MSG_ReadByte();
    if (flags & SND_OFFSET)
        offset = MSG_ReadByte();

    // entity relative
    sendchan = MSG_ReadWord();
    entnum = sendchan >> 3;
    if (entnum < 0 || entnum >= mvd->csr->max_edicts) {
        MVD_Destroyf(mvd, "%s: bad entnum: %d", __func__, entnum);
    }

    entity = &mvd->edicts[entnum];
    if (!entity->inuse) {
        Com_DPrintf("%s: entnum not in use: %d\n", __func__, entnum);
        return;
    }

    if (mvd->demoseeking)
        return;

    // use the entity origin unless it is a bmodel
    if (entity->solid == SOLID_BSP) {
        VectorAvg(entity->mins, entity->maxs, origin);
        VectorAdd(entity->s.origin, origin, origin);
    } else {
        VectorCopy(entity->s.origin, origin);
    }

    // prepare multicast message
    MSG_WriteByte(svc_sound);
    MSG_WriteByte(flags | SND_POS);
    if (mvd->csr->extended && flags & SND_INDEX16)
        MSG_WriteShort(index);
    else
        MSG_WriteByte(index);

    if (flags & SND_VOLUME)
        MSG_WriteByte(volume);
    if (flags & SND_ATTENUATION)
        MSG_WriteByte(attenuation);
    if (flags & SND_OFFSET)
        MSG_WriteByte(offset);

    MSG_WriteShort(sendchan);
    MSG_WritePos(origin);

    leaf1 = NULL;
    if (!(extrabits & 1)) {
        leaf1 = CM_PointLeaf(&mvd->cm, origin);
        BSP_ClusterVis(mvd->cm.cache, mask, leaf1->cluster, DVIS_PHS);
    }

    FOR_EACH_MVDCL(client, mvd) {
        cl = client->cl;

        // do not send sounds to connecting clients
        if (!CLIENT_ACTIVE(cl)) {
            continue;
        }

        // PHS cull this sound
        if (!(extrabits & 1)) {
            VectorScale(client->ps.pmove.origin, 0.125f, org);
            leaf2 = CM_PointLeaf(&mvd->cm, org);
            if (!CM_AreasConnected(&mvd->cm, leaf1->area, leaf2->area))
                continue;
            if (leaf2->cluster == -1)
                continue;
            if (!Q_IsBitSet(mask, leaf2->cluster))
                continue;
        }

        // reliable sounds will always have position explicitly set,
        // as no one guarantees reliables to be delivered in time
        if (extrabits & 2) {
            SV_ClientAddMessage(cl, MSG_RELIABLE);
            continue;
        }

        // default client doesn't know that bmodels have weird origins
        if (entity->solid == SOLID_BSP && cl->protocol == PROTOCOL_VERSION_DEFAULT) {
            SV_ClientAddMessage(cl, 0);
            continue;
        }

        if (LIST_EMPTY(&cl->msg_free_list)) {
            Com_WPrintf("%s: %s: out of message slots\n",
                        __func__, cl->name);
            continue;
        }

        msg = LIST_FIRST(message_packet_t, &cl->msg_free_list, entry);

        msg->cursize = 0;
        msg->flags = flags;
        msg->index = index;
        msg->volume = volume;
        msg->attenuation = attenuation;
        msg->timeofs = offset;
        msg->sendchan = sendchan;
        for (i = 0; i < 3; i++) {
            msg->pos[i] = COORD2SHORT(origin[i]);
        }

        List_Remove(&msg->entry);
        List_Append(&cl->msg_unreliable_list, &msg->entry);
        cl->msg_unreliable_bytes += MAX_SOUND_PACKET;
    }

    // clear multicast buffer
    SZ_Clear(&msg_write);
}

static void MVD_ParseConfigstring(mvd_t *mvd)
{
    int index;
    size_t maxlen;
    char *s;

    index = MSG_ReadWord();
    if (index < 0 || index >= mvd->csr->end) {
        MVD_Destroyf(mvd, "%s: bad index: %d", __func__, index);
    }

    s = mvd->configstrings[index];
    maxlen = CS_SIZE(mvd->csr, index);
    if (MSG_ReadString(s, maxlen) >= maxlen) {
        MVD_Destroyf(mvd, "%s: index %d overflowed", __func__, index);
    }

    if (mvd->demoseeking) {
        Q_SetBit(mvd->dcs, index);
        return;
    }

    MVD_UpdateConfigstring(mvd, index);
}

static void MVD_ParsePrint(mvd_t *mvd)
{
    int level;
    char string[MAX_STRING_CHARS];

    level = MSG_ReadByte();
    MSG_ReadString(string, sizeof(string));

    if (level == PRINT_HIGH && (strstr(string, "Match ended.") ||
                                !strcmp(string, "Fraglimit hit.\n") ||
                                !strcmp(string, "Timelimit hit.\n"))) {
        match_ended_hack = true;
    }

    if (mvd->demoseeking)
        return;

    MVD_BroadcastPrintf(mvd, level, level == PRINT_CHAT ?
                        UF_MUTE_PLAYERS : 0, "%s", string);
}

/*
Fix origin and angles on each player entity by
extracting data from player state.
*/
static void MVD_PlayerToEntityStates(mvd_t *mvd)
{
    mvd_player_t *player;
    edict_t *edict;
    int i;

    mvd->numplayers = 0;
    for (i = 1, player = mvd->players; i <= mvd->maxclients; i++, player++) {
        if (!player->inuse || player == mvd->dummy) {
            continue;
        }

        mvd->numplayers++;
        if (player->ps.pmove.pm_type != PM_NORMAL) {
            continue;   // can be out of sync, in this case
            // server should provide valid data
        }

        edict = &mvd->edicts[i];
        if (!edict->inuse) {
            continue; // not present in this frame
        }

        Com_PlayerToEntityState(&player->ps, &edict->s);

        MVD_LinkEdict(mvd, edict);
    }
}

#define RELINK_MASK        (U_MODEL|U_ORIGIN1|U_ORIGIN2|U_ORIGIN3|U_SOLID)

/*
==================
MVD_ParsePacketEntities
==================
*/
static void MVD_ParsePacketEntities(mvd_t *mvd)
{
    int         number;
    uint64_t    bits;
    edict_t     *ent;

    while (1) {
        if (msg_read.readcount > msg_read.cursize) {
            MVD_Destroyf(mvd, "%s: read past end of message", __func__);
        }

        number = MSG_ParseEntityBits(&bits, mvd->esFlags);
        if (number < 0 || number >= mvd->csr->max_edicts) {
            MVD_Destroyf(mvd, "%s: bad number: %d", __func__, number);
        }

        if (!number) {
            break;
        }

        ent = &mvd->edicts[number];

#if USE_DEBUG
        if (mvd_shownet->integer > 2) {
            Com_Printf("   %s: %d ", ent->inuse ?
                       "delta" : "baseline", number);
            MSG_ShowDeltaEntityBits(bits);
            Com_Printf("\n");
        }
#endif

        MSG_ParseDeltaEntity(&ent->s, &ent->x, number, bits, mvd->esFlags);

        // lazily relink even if removed
        if ((bits & RELINK_MASK) && !mvd->demoseeking) {
            MVD_LinkEdict(mvd, ent);
        }

        // mark this entity as seen even if removed
        ent->svflags |= SVF_MONSTER;

        // shuffle current origin to old if removed
        if (bits & U_REMOVE) {
            SHOWNET(2, "   remove: %d\n", number);
            if (!(ent->s.renderfx & RF_BEAM)) {
                VectorCopy(ent->s.origin, ent->s.old_origin);
            }
            ent->inuse = false;
            continue;
        }

        ent->inuse = true;
        if (number >= mvd->ge.num_edicts) {
            mvd->ge.num_edicts = number + 1;
        }
    }
}

/*
==================
MVD_ParsePacketPlayers
==================
*/
static void MVD_ParsePacketPlayers(mvd_t *mvd)
{
    int             number;
    int             bits;
    mvd_player_t    *player;

    while (1) {
        if (msg_read.readcount > msg_read.cursize) {
            MVD_Destroyf(mvd, "%s: read past end of message", __func__);
        }

        number = MSG_ReadByte();
        if (number == CLIENTNUM_NONE) {
            break;
        }

        if (number < 0 || number >= mvd->maxclients) {
            MVD_Destroyf(mvd, "%s: bad number: %d", __func__, number);
        }

        player = &mvd->players[number];

        bits = MSG_ReadWord();

#if USE_DEBUG
        if (mvd_shownet->integer > 2) {
            Com_Printf("   %s: %d ", player->inuse ?
                       "delta" : "baseline", number);
            MSG_ShowDeltaPlayerstateBits_Packet(bits);
            Com_Printf("\n");
        }
#endif

        MSG_ParseDeltaPlayerstate_Packet(&player->ps, &player->ps, bits, mvd->psFlags);

        if (bits & PPS_REMOVE) {
            SHOWNET(2, "   remove: %d\n", number);
            player->inuse = false;
            continue;
        }

        player->inuse = true;
    }
}

/*
================
MVD_ParseFrame
================
*/
static void MVD_ParseFrame(mvd_t *mvd)
{
    byte *data;
    int length;

    // read portalbits
    length = MSG_ReadByte();
    data = MSG_ReadData(length);
    if (!data) {
        MVD_Destroyf(mvd, "%s: read past end of message", __func__);
    }
    if (!mvd->demoseeking)
        CM_SetPortalStates(&mvd->cm, data, length);

    SHOWNET(1, "%3zu:playerinfo\n", msg_read.readcount);
    MVD_ParsePacketPlayers(mvd);
    SHOWNET(1, "%3zu:packetentities\n", msg_read.readcount);
    MVD_ParsePacketEntities(mvd);
    SHOWNET(1, "%3zu:frame:%u\n", msg_read.readcount, mvd->framenum);
    MVD_PlayerToEntityStates(mvd);

    // update clients now so that effects datagram that
    // follows can reference current view positions
    if (mvd->state && mvd->framenum && !mvd->demoseeking) {
        MVD_UpdateClients(mvd);
    }

    mvd->framenum++;
}

void MVD_ClearState(mvd_t *mvd, bool full)
{
    mvd_player_t *player;
    int i;

    // clear all entities, don't trust num_edicts as it is possible
    // to miscount removed but seen entities
    memset(mvd->edicts, 0, sizeof(mvd->edicts[0]) * mvd->csr->max_edicts);
    mvd->ge.num_edicts = 0;

    // clear all players
    for (i = 0; i < mvd->maxclients; i++) {
        player = &mvd->players[i];
        MVD_FreePlayer(player);
        memset(player, 0, sizeof(*player));
    }

    mvd->numplayers = 0;

    if (!full)
        return;

    // free all snapshots
    for (i = 0; i < mvd->numsnapshots; i++) {
        Z_Free(mvd->snapshots[i]);
    }
    mvd->numsnapshots = 0;

    Z_Freep((void**)&mvd->snapshots);

    // free current map
    CM_FreeMap(&mvd->cm);

    VectorClear(mvd->spawnOrigin);
    VectorClear(mvd->spawnAngles);

    if (mvd->intermission) {
        // save oldscores
        //Q_strlcpy(mvd->oldscores, mvd->layout, sizeof(mvd->oldscores));
    }

    memset(mvd->configstrings, 0, sizeof(mvd->configstrings[0]) * mvd->csr->end);
    mvd->layout[0] = 0;

    mvd->framenum = 0;
    // intermission flag will be cleared in MVD_ChangeLevel
}

static void MVD_ChangeLevel(mvd_t *mvd)
{
    mvd_client_t *client;

    if (sv.state != ss_broadcast) {
        // the game is just starting
        SV_InitGame(MVD_SPAWN_INTERNAL);
        MVD_Spawn();
        return;
    }

    // cause all UDP clients to reconnect
    MSG_WriteByte(svc_stufftext);
    MSG_WriteString(va("changing map=%s; reconnect\n", mvd->mapname));

    FOR_EACH_MVDCL(client, mvd) {
        if (client->target != mvd->dummy) {
            // make them switch to previous target instead of MVD dummy
            client->oldtarget = client->target;
        }
        client->target = NULL;
        SV_ClientReset(client->cl);
        client->cl->spawncount = mvd->servercount;
        SV_ClientAddMessage(client->cl, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);

    mvd->intermission = false;

    mvd_dirty = true;

    SV_SendAsyncPackets();
}

static void MVD_ParseServerData(mvd_t *mvd, int extrabits)
{
    int protocol;
    size_t maxlen;
    char *string;
    int index;
    int ret;
    edict_t *ent;

    // clear the leftover from previous level
    MVD_ClearState(mvd, true);

    // parse major protocol version
    protocol = MSG_ReadLong();
    if (protocol != PROTOCOL_VERSION_MVD) {
        MVD_Destroyf(mvd, "Unsupported protocol: %d", protocol);
    }

    // parse minor protocol version
    mvd->version = MSG_ReadWord();
    if (!MVD_SUPPORTED(mvd->version)) {
        MVD_Destroyf(mvd, "Unsupported MVD protocol version: %d.\n"
                     "Current version is %d.\n", mvd->version, PROTOCOL_VERSION_MVD_CURRENT);
    }

    mvd->servercount = MSG_ReadLong();
    if (MSG_ReadString(mvd->gamedir, sizeof(mvd->gamedir)) >= sizeof(mvd->gamedir)) {
        MVD_Destroyf(mvd, "Oversize gamedir string");
    }
    mvd->clientNum = MSG_ReadShort();
    mvd->flags = extrabits;
    mvd->esFlags = MSG_ES_UMASK;
    mvd->psFlags = 0;
    mvd->csr = &cs_remap_old;

    if (mvd->version >= PROTOCOL_VERSION_MVD_EXTENDED_LIMITS && mvd->flags & MVF_EXTLIMITS) {
        mvd->esFlags |= MSG_ES_EXTENSIONS;
        mvd->psFlags |= MSG_PS_EXTENSIONS;
        mvd->csr = &cs_remap_new;
    }

#if 0
    // change gamedir unless playing a demo
    Cvar_UserSet("game", mvd->gamedir);
#endif

    // parse configstrings
    while (1) {
        index = MSG_ReadWord();
        if (index == mvd->csr->end) {
            break;
        }

        if (index < 0 || index >= mvd->csr->end) {
            MVD_Destroyf(mvd, "Bad configstring index: %d", index);
        }

        string = mvd->configstrings[index];
        maxlen = CS_SIZE(mvd->csr, index);
        if (MSG_ReadString(string, maxlen) >= maxlen) {
            MVD_Destroyf(mvd, "Configstring %d overflowed", index);
        }

        if (msg_read.readcount > msg_read.cursize) {
            MVD_Destroyf(mvd, "Read past end of message");
        }
    }

    // parse maxclients
    index = Q_atoi(mvd->configstrings[mvd->csr->maxclients]);
    if (index < 1 || index > MAX_CLIENTS) {
        MVD_Destroyf(mvd, "Invalid maxclients");
    }

    // check if maxclients changed
    if (index != mvd->maxclients) {
        mvd_client_t *client;

        // free any old players
        Z_Free(mvd->players);

        // allocate new players
        mvd->players = MVD_Mallocz(sizeof(mvd->players[0]) * index);
        mvd->maxclients = index;

        // clear chase targets
        FOR_EACH_MVDCL(client, mvd) {
            client->target = NULL;
            client->oldtarget = NULL;
            client->chase_mask = 0;
            client->chase_auto = false;
            client->chase_wait = false;
            memset(client->chase_bitmap, 0, sizeof(client->chase_bitmap));
        }
    }

    if (mvd->clientNum == -1) {
        mvd->dummy = NULL;
    } else {
        // validate clientNum
        if (mvd->clientNum < 0 || mvd->clientNum >= mvd->maxclients) {
            MVD_Destroyf(mvd, "Invalid client num: %d", mvd->clientNum);
        }
        mvd->dummy = mvd->players + mvd->clientNum;
    }

    // parse world model
    string = mvd->configstrings[mvd->csr->models + 1];
    if (!Com_ParseMapName(mvd->mapname, string, sizeof(mvd->mapname))) {
        MVD_Destroyf(mvd, "Bad world model: %s", string);
    }

    // load the world model (we are only interesed in visibility info)
    Com_Printf("[%s] -=- Loading %s...\n", mvd->name, string);
    ret = CM_LoadMap(&mvd->cm, string);
    if (ret) {
        Com_EPrintf("[%s] =!= Couldn't load %s: %s\n", mvd->name, string, BSP_ErrorString(ret));
        // continue with null visibility
    } else if (mvd->cm.cache->checksum != Q_atoi(mvd->configstrings[mvd->csr->mapchecksum])) {
        Com_EPrintf("[%s] =!= Local map version differs from server!\n", mvd->name);
        CM_FreeMap(&mvd->cm);
    }

    // set player names
    MVD_SetPlayerNames(mvd);

    // init world entity
    ent = &mvd->edicts[0];
    ent->solid = SOLID_BSP;
    ent->inuse = true;

    if (mvd->cm.cache) {
        // get the spawn point for spectators
        MVD_ParseEntityString(mvd, mvd->cm.cache->entitystring);
    }

    // parse baseline frame
    MVD_ParseFrame(mvd);

    // save base configstrings
    memcpy(mvd->baseconfigstrings, mvd->configstrings, sizeof(mvd->baseconfigstrings[0]) * mvd->csr->end);

    // force inital snapshot
    mvd->last_snapshot = INT_MIN;

    // if the channel has been just created, init some things
    if (!mvd->state) {
        mvd_t *cur;

        // sort this one into the list of active channels
        FOR_EACH_MVD(cur) {
            if (cur->id > mvd->id) {
                break;
            }
        }
        List_Append(&cur->entry, &mvd->entry);
        mvd->state = MVD_WAITING;
    }

    // case all UDP clients to reconnect
    MVD_ChangeLevel(mvd);
}

bool MVD_ParseMessage(mvd_t *mvd)
{
    int     cmd, extrabits;
    bool    ret = false;

#if USE_DEBUG
    if (mvd_shownet->integer == 1) {
        Com_LPrintf(PRINT_DEVELOPER, "%zu ", msg_read.cursize);
    } else if (mvd_shownet->integer > 1) {
        Com_LPrintf(PRINT_DEVELOPER, "------------------\n");
    }
#endif

//
// parse the message
//
    match_ended_hack = false;
    while (1) {
        if (msg_read.readcount > msg_read.cursize) {
            MVD_Destroyf(mvd, "Read past end of message");
        }
        if (msg_read.readcount == msg_read.cursize) {
            SHOWNET(1, "%3zu:END OF MESSAGE\n", msg_read.readcount);
            break;
        }

        cmd = MSG_ReadByte();
        extrabits = cmd >> SVCMD_BITS;
        cmd &= SVCMD_MASK;

        SHOWNET(1, "%3zu:%s\n", msg_read.readcount - 1, MVD_ServerCommandString(cmd));

        switch (cmd) {
        case mvd_serverdata:
            MVD_ParseServerData(mvd, extrabits);
            ret = true;
            break;
        case mvd_multicast_all:
        case mvd_multicast_pvs:
        case mvd_multicast_phs:
        case mvd_multicast_all_r:
        case mvd_multicast_pvs_r:
        case mvd_multicast_phs_r:
            MVD_ParseMulticast(mvd, cmd, extrabits);
            break;
        case mvd_unicast:
        case mvd_unicast_r:
            MVD_ParseUnicast(mvd, cmd, extrabits);
            break;
        case mvd_configstring:
            MVD_ParseConfigstring(mvd);
            break;
        case mvd_frame:
            MVD_ParseFrame(mvd);
            break;
        case mvd_sound:
            MVD_ParseSound(mvd, extrabits);
            break;
        case mvd_print:
            MVD_ParsePrint(mvd);
            break;
        case mvd_nop:
            break;
        default:
            MVD_Destroyf(mvd, "Illegible command at %zu: %d",
                         msg_read.readcount - 1, cmd);
        }
    }

    return ret;
}

