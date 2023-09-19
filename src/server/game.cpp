/*
Copyright (C) 1997-2001 Id Software, Inc.

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
// sv_game.c -- interface to the game dll

#include "server.h"
extern "C" {

game_export_t    *ge;

static void PF_configstring(int index, const char *val);

/*
================
PF_FindIndex

================
*/
static int PF_FindIndex(const char *name, int start, int max, const char *func)
{
    char *string;
    int i;

    if (!name || !name[0])
        return 0;

    for (i = 1; i < max; i++) {
        string = sv.configstrings[start + i];
        if (!string[0]) {
            break;
        }
        if (!strcmp(string, name)) {
            return i;
        }
    }

    if (i == max) {
        Com_Error(ERR_DROP, "%s(%s): overflow", func, name);
    }

    PF_configstring(i + start, name);

    return i;
}

static int PF_ModelIndex(const char *name)
{
    return PF_FindIndex(name, CS_MODELS, MAX_MODELS, __func__);
}

static int PF_SoundIndex(const char *name)
{
    return PF_FindIndex(name, CS_SOUNDS, MAX_SOUNDS, __func__);
}

static int PF_ImageIndex(const char *name)
{
    return PF_FindIndex(name, CS_IMAGES, MAX_IMAGES, __func__);
}

/*
===============
PF_Unicast

Sends the contents of the mutlicast buffer to a single client.
Archived in MVD stream.
===============
*/
static void PF_Unicast(edict_t *ent, qboolean reliable)
{
    client_t    *client;
    int         cmd, flags, clientNum;

    if (!ent) {
        goto clear;
    }

    clientNum = NUM_FOR_EDICT(ent) - 1;
    if (clientNum < 0 || clientNum >= sv_maxclients->integer) {
        Com_WPrintf("%s to a non-client %d\n", __func__, clientNum);
        goto clear;
    }

    client = svs.client_pool + clientNum;
    if (client->state <= cs_zombie) {
        Com_WPrintf("%s to a free/zombie client %d\n", __func__, clientNum);
        goto clear;
    }

    if (!msg_write.cursize) {
        Com_DPrintf("%s with empty data\n", __func__);
        goto clear;
    }

    cmd = msg_write.data[0];

    flags = 0;
    if (reliable) {
        flags |= MSG_RELIABLE;
    }

    if (cmd == svc_layout || (cmd == svc_configstring && msg_write.data[1] == CS_STATUSBAR)) {
        flags |= MSG_COMPRESS_AUTO;
    }

    SV_ClientAddMessage(client, flags);

    // fix anti-kicking exploit for broken mods
    if (cmd == svc_disconnect) {
        client->drop_hack = true;
        goto clear;
    }

    SV_MvdUnicast(ent, clientNum, reliable);

clear:
    SZ_Clear(&msg_write);
}

/*
=================
PF_bprintf

Sends text to all active clients.
Archived in MVD stream.
=================
*/
static void PF_bprintf(int level, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    client_t    *client;
    size_t      len;
    int         i;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    SV_MvdBroadcastPrint(level, string);

    MSG_WriteByte(svc_print);
    MSG_WriteByte(level);
    MSG_WriteData(string, len + 1);

    // echo to console
    if (COM_DEDICATED) {
        // mask off high bits
        for (i = 0; i < len; i++)
            string[i] &= 127;
        Com_Printf("%s", string);
    }

    FOR_EACH_CLIENT(client) {
        if (client->state != cs_spawned)
            continue;
        if (level >= client->messagelevel) {
            SV_ClientAddMessage(client, MSG_RELIABLE);
        }
    }

    SZ_Clear(&msg_write);
}

/*
===============
PF_dprintf

Debug print to server console.
===============
*/
static void PF_dprintf(const char *fmt, ...)
{
    char        msg[MAXPRINTMSG];
    va_list     argptr;

    va_start(argptr, fmt);
    Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    Com_Printf("%s", msg);
}

/*
===============
PF_cprintf

Print to a single client if the level passes.
Archived in MVD stream.
===============
*/
static void PF_cprintf(edict_t *ent, int level, const char *fmt, ...)
{
    char        msg[MAX_STRING_CHARS];
    va_list     argptr;
    int         clientNum;
    size_t      len;
    client_t    *client;

    va_start(argptr, fmt);
    len = Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(msg)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    if (!ent) {
        Com_LPrintf(level == PRINT_CHAT ? PRINT_TALK : PRINT_ALL, "%s", msg);
        return;
    }

    clientNum = NUM_FOR_EDICT(ent) - 1;
    if (clientNum < 0 || clientNum >= sv_maxclients->integer) {
        Com_Error(ERR_DROP, "%s to a non-client %d", __func__, clientNum);
    }

    client = svs.client_pool + clientNum;
    if (client->state <= cs_zombie) {
        Com_WPrintf("%s to a free/zombie client %d\n", __func__, clientNum);
        return;
    }

    MSG_WriteByte(svc_print);
    MSG_WriteByte(level);
    MSG_WriteData(msg, len + 1);

    if (level >= client->messagelevel) {
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SV_MvdUnicast(ent, clientNum, true);

    SZ_Clear(&msg_write);
}

/*
===============
PF_centerprintf

Centerprint to a single client.
Archived in MVD stream.
===============
*/
static void PF_centerprintf(edict_t *ent, const char *fmt, ...)
{
    char        msg[MAX_STRING_CHARS];
    va_list     argptr;
    int         n;
    size_t      len;

    if (!ent) {
        return;
    }

    n = NUM_FOR_EDICT(ent);
    if (n < 1 || n > sv_maxclients->integer) {
        Com_WPrintf("%s to a non-client %d\n", __func__, n - 1);
        return;
    }

    va_start(argptr, fmt);
    len = Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(msg)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_centerprint);
    MSG_WriteData(msg, len + 1);

    PF_Unicast(ent, true);
}

/*
===============
PF_error

Abort the server with a game error
===============
*/
static q_noreturn void PF_error(const char *fmt, ...)
{
    char        msg[MAXERRORMSG];
    va_list     argptr;

    va_start(argptr, fmt);
    Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    Com_Error(ERR_DROP, "Game Error: %s", msg);
}

/*
=================
PF_setmodel

Also sets mins and maxs for inline bmodels
=================
*/
static void PF_setmodel(edict_t *ent, const char *name)
{
    mmodel_t    *mod;

    if (!ent || !name)
        Com_Error(ERR_DROP, "PF_setmodel: NULL");

    ent->s.modelindex = PF_ModelIndex(name);

// if it is an inline model, get the size information for it
    if (name[0] == '*') {
        mod = CM_InlineModel(&sv.cm, name);
        VectorCopy(mod->mins, ent->mins);
        VectorCopy(mod->maxs, ent->maxs);
        PF_LinkEdict(ent);
    }
}

/*
===============
PF_configstring

If game is actively running, broadcasts configstring change.
Archived in MVD stream.
===============
*/
static void PF_configstring(int index, const char *val)
{
    size_t len, maxlen;
    client_t *client;
    char *dst;

    if (index < 0 || index >= MAX_CONFIGSTRINGS)
        Com_Error(ERR_DROP, "%s: bad index: %d", __func__, index);

    if (sv.state == ss_dead) {
        Com_WPrintf("%s: not yet initialized\n", __func__);
        return;
    }

    if (!val)
        val = "";

    // error out entirely if it exceedes array bounds
    len = strlen(val);
    maxlen = (MAX_CONFIGSTRINGS - index) * MAX_QPATH;
    if (len >= maxlen) {
        Com_Error(ERR_DROP,
                  "%s: index %d overflowed: %zu > %zu",
                  __func__, index, len, maxlen - 1);
    }

    // print a warning and truncate everything else
    maxlen = CS_SIZE(index);
    if (len >= maxlen) {
        Com_WPrintf(
            "%s: index %d overflowed: %zu > %zu\n",
            __func__, index, len, maxlen - 1);
        len = maxlen - 1;
    }

    dst = sv.configstrings[index];
    if (!strncmp(dst, val, maxlen)) {
        return;
    }

    // change the string in sv
    memcpy(dst, val, len);
    dst[len] = 0;

    if (sv.state == ss_loading) {
        return;
    }

    SV_MvdConfigstring(index, val, len);

    // send the update to everyone
    MSG_WriteByte(svc_configstring);
    MSG_WriteShort(index);
    MSG_WriteData(val, len);
    MSG_WriteByte(0);

    FOR_EACH_CLIENT(client) {
        if (client->state < cs_primed) {
            continue;
        }
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}

static void PF_WriteFloat(float f)
{
    Com_Error(ERR_DROP, "PF_WriteFloat not implemented");
}

static qboolean PF_inVIS(const vec3_t p1, const vec3_t p2, int vis)
{
    mleaf_t *leaf1, *leaf2;
    byte mask[VIS_MAX_BYTES];
    bsp_t *bsp = sv.cm.cache;

    if (!bsp) {
        Com_Error(ERR_DROP, "%s: no map loaded", __func__);
    }

    leaf1 = BSP_PointLeaf(bsp->nodes, p1);
    BSP_ClusterVis(bsp, mask, leaf1->cluster, vis);

    leaf2 = BSP_PointLeaf(bsp->nodes, p2);
    if (leaf2->cluster == -1)
        return false;
    if (!Q_IsBitSet(mask, leaf2->cluster))
        return false;
    if (!CM_AreasConnected(&sv.cm, leaf1->area, leaf2->area))
        return false;       // a door blocks it
    return true;
}

/*
=================
PF_inPVS

Also checks portalareas so that doors block sight
=================
*/
static qboolean PF_inPVS(const vec3_t p1, const vec3_t p2)
{
    return PF_inVIS(p1, p2, DVIS_PVS);
}

/*
=================
PF_inPHS

Also checks portalareas so that doors block sound
=================
*/
static qboolean PF_inPHS(const vec3_t p1, const vec3_t p2)
{
    return PF_inVIS(p1, p2, DVIS_PHS);
}

/*
==================
SV_StartSound

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
static void SV_StartSound(const vec3_t origin, edict_t *edict,
                          int channel, int soundindex, float volume,
                          float attenuation, float timeofs)
{
    int         i, ent, flags, sendchan;
    vec3_t      origin_v;
    client_t    *client;
    byte        mask[VIS_MAX_BYTES];
    mleaf_t     *leaf1, *leaf2;
    message_packet_t    *msg;
    bool        force_pos;

    if (!edict)
        Com_Error(ERR_DROP, "%s: edict = NULL", __func__);
    if (volume < 0 || volume > 1)
        Com_Error(ERR_DROP, "%s: volume = %f", __func__, volume);
    if (attenuation < 0 || attenuation > 4)
        Com_Error(ERR_DROP, "%s: attenuation = %f", __func__, attenuation);
    if (timeofs < 0 || timeofs > 0.255f)
        Com_Error(ERR_DROP, "%s: timeofs = %f", __func__, timeofs);
    if (soundindex < 0 || soundindex >= MAX_SOUNDS)
        Com_Error(ERR_DROP, "%s: soundindex = %d", __func__, soundindex);

    attenuation = min(attenuation, 255.0f / 64);

    ent = NUM_FOR_EDICT(edict);

    sendchan = (ent << 3) | (channel & 7);

    // always send the entity number for channel overrides
    flags = SND_ENT;
    if (volume != DEFAULT_SOUND_PACKET_VOLUME)
        flags |= SND_VOLUME;
    if (attenuation != DEFAULT_SOUND_PACKET_ATTENUATION)
        flags |= SND_ATTENUATION;
    if (timeofs)
        flags |= SND_OFFSET;

    // send origin for invisible entities
    // the origin can also be explicitly set
    force_pos = (edict->svflags & SVF_NOCLIENT) || origin;

    // use the entity origin unless it is a bmodel or explicitly specified
    if (!origin) {
        if (edict->solid == SOLID_BSP) {
            VectorAvg(edict->mins, edict->maxs, origin_v);
            VectorAdd(origin_v, edict->s.origin, origin_v);
            origin = origin_v;
        } else {
            origin = edict->s.origin;
        }
    }

    // prepare multicast message
    MSG_WriteByte(svc_sound);
    MSG_WriteByte(flags | SND_POS);
    MSG_WriteByte(soundindex);

    if (flags & SND_VOLUME)
        MSG_WriteByte(volume * 255);
    if (flags & SND_ATTENUATION)
        MSG_WriteByte(attenuation * 64);
    if (flags & SND_OFFSET)
        MSG_WriteByte(timeofs * 1000);

    MSG_WriteShort(sendchan);
    MSG_WritePos(origin);

    // if the sound doesn't attenuate, send it to everyone
    // (global radio chatter, voiceovers, etc)
    if (attenuation == ATTN_NONE)
        channel |= CHAN_NO_PHS_ADD;

    // multicast if force sending origin
    if (force_pos) {
        if (channel & CHAN_NO_PHS_ADD) {
            if (channel & CHAN_RELIABLE) {
                SV_Multicast(NULL, MULTICAST_ALL_R);
            } else {
                SV_Multicast(NULL, MULTICAST_ALL);
            }
        } else {
            if (channel & CHAN_RELIABLE) {
                SV_Multicast(origin, MULTICAST_PHS_R);
            } else {
                SV_Multicast(origin, MULTICAST_PHS);
            }
        }
        return;
    }

    leaf1 = NULL;
    if (!(channel & CHAN_NO_PHS_ADD)) {
        leaf1 = CM_PointLeaf(&sv.cm, origin);
        BSP_ClusterVis(sv.cm.cache, mask, leaf1->cluster, DVIS_PHS);
    }

    // decide per client if origin needs to be sent
    FOR_EACH_CLIENT(client) {
        // do not send sounds to connecting clients
        if (!CLIENT_ACTIVE(client)) {
            continue;
        }

        // PHS cull this sound
        if (!(channel & CHAN_NO_PHS_ADD)) {
            leaf2 = CM_PointLeaf(&sv.cm, client->edict->s.origin);
            if (!CM_AreasConnected(&sv.cm, leaf1->area, leaf2->area))
                continue;
            if (leaf2->cluster == -1)
                continue;
            if (!Q_IsBitSet(mask, leaf2->cluster))
                continue;
        }

        // reliable sounds will always have position explicitly set,
        // as no one guarantees reliables to be delivered in time
        if (channel & CHAN_RELIABLE) {
            SV_ClientAddMessage(client, MSG_RELIABLE);
            continue;
        }

        // default client doesn't know that bmodels have weird origins
        if (edict->solid == SOLID_BSP && client->protocol == PROTOCOL_VERSION_DEFAULT) {
            SV_ClientAddMessage(client, 0);
            continue;
        }

        if (LIST_EMPTY(&client->msg_free_list)) {
            Com_WPrintf("%s: %s: out of message slots\n",
                        __func__, client->name);
            continue;
        }

        msg = LIST_FIRST(message_packet_t, &client->msg_free_list, entry);

        msg->cursize = 0;
        msg->flags = flags;
        msg->index = soundindex;
        msg->volume = volume * 255;
        msg->attenuation = attenuation * 64;
        msg->timeofs = timeofs * 1000;
        msg->sendchan = sendchan;
        for (i = 0; i < 3; i++) {
            msg->pos[i] = COORD2SHORT(origin[i]);
        }

        List_Remove(&msg->entry);
        List_Append(&client->msg_unreliable_list, &msg->entry);
        client->msg_unreliable_bytes += MAX_SOUND_PACKET;
    }

    // clear multicast buffer
    SZ_Clear(&msg_write);

    SV_MvdStartSound(ent, channel, flags, soundindex,
                     volume * 255, attenuation * 64, timeofs * 1000);
}

static void PF_StartSound(edict_t *entity, int channel,
                          int soundindex, float volume,
                          float attenuation, float timeofs)
{
    if (!entity)
        return;
    SV_StartSound(NULL, entity, channel, soundindex, volume, attenuation, timeofs);
}

void PF_Pmove(pmove_t *pm)
{
    if (sv_client) {
        Pmove(pm, &sv_client->pmp);
    } else {
        Pmove(pm, &sv_pmp);
    }
}

static cvar_t *PF_cvar(const char *name, const char *value, int flags)
{
    if (flags & CVAR_EXTENDED_MASK) {
        Com_WPrintf("Game attemped to set extended flags on '%s', masked out.\n", name);
        flags &= ~CVAR_EXTENDED_MASK;
    }

    return Cvar_Get(name, value, flags | CVAR_GAME);
}

static void PF_AddCommandString(const char *string)
{
    Cbuf_AddText(&cmd_buffer, string);
}

static void PF_SetAreaPortalState(int portalnum, qboolean open)
{
    if (!sv.cm.cache) {
        Com_Error(ERR_DROP, "%s: no map loaded", __func__);
    }
    CM_SetAreaPortalState(&sv.cm, portalnum, open);
}

static qboolean PF_AreasConnected(int area1, int area2)
{
    if (!sv.cm.cache) {
        Com_Error(ERR_DROP, "%s: no map loaded", __func__);
    }
    return CM_AreasConnected(&sv.cm, area1, area2);
}

static void *PF_TagMalloc(unsigned size, unsigned tag)
{
    Q_assert(tag + TAG_MAX > tag);
    if (!size) {
        return NULL;
    }
    return memset(Z_TagMalloc(size, static_cast<memtag_t>( tag + TAG_MAX ) ), 0, size); // WID: C++20: Added cast.
}

static void PF_FreeTags(unsigned tag)
{
    Q_assert(tag + TAG_MAX > tag);
    Z_FreeTags( static_cast<memtag_t>( tag + TAG_MAX ) ); // WID: C++20: Added cast.
}

static void PF_DebugGraph(float value, int color)
{
}

//==============================================

static void *game_library;

// WID: C++20: Typedef this for casting
typedef game_export_t*(GameEntryFunctionPointer(game_import_t*));
/*
===============
SV_ShutdownGameProgs

Called when either the entire server is being killed, or
it is changing to a different game directory.
===============
*/
void SV_ShutdownGameProgs(void)
{
    if (ge) {
        ge->Shutdown();
        ge = NULL;
    }
    if (game_library) {
        Sys_FreeLibrary(game_library);
        game_library = NULL;
    }
    Cvar_Set("g_features", "0");
}

static GameEntryFunctionPointer *_SV_LoadGameLibrary(const char *path)
{
    void *entry;

    entry = Sys_LoadLibrary(path, "GetGameAPI", &game_library);
    if (!entry)
        Com_EPrintf("Failed to load game library: %s\n", Com_GetLastError());
    else
        Com_Printf("Loaded game library from %s\n", path);

    return static_cast<GameEntryFunctionPointer*>( entry );
}

static GameEntryFunctionPointer *SV_LoadGameLibrary(const char *game, const char *prefix)
{
    char path[MAX_OSPATH];

    if (Q_concat(path, sizeof(path), sys_libdir->string,
                 PATH_SEP_STRING, game, PATH_SEP_STRING,
                 prefix, "game" CPUSTRING LIBSUFFIX) >= sizeof(path)) {
        Com_EPrintf("Game library path length exceeded\n");
        return NULL;
    }

    if (os_access(path, F_OK)) {
        if (!*prefix)
            Com_Printf("Can't access %s: %s\n", path, strerror(errno));
        return NULL;
    }

    return _SV_LoadGameLibrary(path);
}

/*
===============
SV_InitGameProgs

Init the game subsystem for a new map
===============
*/
void SV_InitGameProgs(void)
{
	
    game_import_t   import;
	GameEntryFunctionPointer *entry = NULL;

    // unload anything we have now
    SV_ShutdownGameProgs();

    // for debugging or `proxy' mods
    if (sys_forcegamelib->string[0])
        entry = _SV_LoadGameLibrary(sys_forcegamelib->string); // WID: C++20: Added cast.

    // try game first
    if (!entry && fs_game->string[0]) {
        entry = SV_LoadGameLibrary(fs_game->string, "q2pro_"); // WID: C++20: Added cast.
        if (!entry)
            entry = SV_LoadGameLibrary(fs_game->string, ""); // WID: C++20: Added cast.
    }

    // then try baseq2
    if (!entry) {
        entry = SV_LoadGameLibrary(BASEGAME, "q2pro_"); // WID: C++20: Added cast.
        if (!entry)
            entry = SV_LoadGameLibrary(BASEGAME, ""); // WID: C++20: Added cast.
    }

    // all paths failed
    if (!entry)
        Com_Error(ERR_DROP, "Failed to load game library");

	// Setup import frametime related values so the GameDLL knows about it.
	import.tick_rate = BASE_FRAMERATE;
	import.frame_time_s = BASE_FRAMETIME_1000;
	import.frame_time_ms = BASE_FRAMETIME;

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
    import.trace = SV_Trace;
    import.pointcontents = SV_PointContents;
    import.setmodel = PF_setmodel;
    import.inPVS = PF_inPVS;
    import.inPHS = PF_inPHS;
    import.Pmove = PF_Pmove;

    import.modelindex = PF_ModelIndex;
    import.soundindex = PF_SoundIndex;
    import.imageindex = PF_ImageIndex;

    import.configstring = PF_configstring;
    import.sound = PF_StartSound;
    import.positioned_sound = SV_StartSound;

    import.WriteChar = MSG_WriteChar;
    import.WriteByte = MSG_WriteByte;
    import.WriteShort = MSG_WriteShort;
    import.WriteLong = MSG_WriteLong;
    import.WriteFloat = PF_WriteFloat;
    import.WriteString = MSG_WriteString;
    import.WritePosition = MSG_WritePos;
    import.WriteDir = MSG_WriteDir;
    import.WriteAngle = MSG_WriteAngle;

    import.TagMalloc = PF_TagMalloc;
    import.TagFree = Z_Free;
    import.FreeTags = PF_FreeTags;

    import.cvar = PF_cvar;
    import.cvar_set = Cvar_UserSet;
    import.cvar_forceset = Cvar_Set;

    import.argc = Cmd_Argc;
    import.argv = Cmd_Argv;
    // original Cmd_Args() did actually return raw arguments
    import.args = Cmd_RawArgs;
    import.AddCommandString = PF_AddCommandString;

    import.DebugGraph = PF_DebugGraph;
    import.SetAreaPortalState = PF_SetAreaPortalState;
    import.AreasConnected = PF_AreasConnected;

    ge = entry(&import);
    if (!ge) {
        Com_Error(ERR_DROP, "Game library returned NULL exports");
    }

    if (ge->apiversion != GAME_API_VERSION) {
        Com_Error(ERR_DROP, "Game library is version %d, expected %d",
                  ge->apiversion, GAME_API_VERSION);
    }

    // initialize
    ge->Init();

    // sanitize edict_size
    if (ge->edict_size < sizeof(edict_t) || ge->edict_size > (unsigned)INT_MAX / MAX_EDICTS) {
        Com_Error(ERR_DROP, "Game library returned bad size of edict_t");
    }

    // sanitize max_edicts
    if (ge->max_edicts <= sv_maxclients->integer || ge->max_edicts > MAX_EDICTS) {
        Com_Error(ERR_DROP, "Game library returned bad number of max_edicts");
    }
}

};
