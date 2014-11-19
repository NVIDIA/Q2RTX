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
// sv_user.c -- server code for moving users

#include "server.h"

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
static void create_baselines(void)
{
    int        i;
    edict_t    *ent;
    entity_packed_t *base, **chunk;

    // clear baselines from previous level
    for (i = 0; i < SV_BASELINES_CHUNKS; i++) {
        base = sv_client->baselines[i];
        if (!base) {
            continue;
        }
        memset(base, 0, sizeof(*base) * SV_BASELINES_PER_CHUNK);
    }

    for (i = 1; i < sv_client->pool->num_edicts; i++) {
        ent = EDICT_POOL(sv_client, i);

        if ((g_features->integer & GMF_PROPERINUSE) && !ent->inuse) {
            continue;
        }

        if (!ES_INUSE(&ent->s)) {
            continue;
        }

        ent->s.number = i;

        chunk = &sv_client->baselines[i >> SV_BASELINES_SHIFT];
        if (*chunk == NULL) {
            *chunk = SV_Mallocz(sizeof(*base) * SV_BASELINES_PER_CHUNK);
        }

        base = *chunk + (i & SV_BASELINES_MASK);
        MSG_PackEntity(base, &ent->s, Q2PRO_SHORTANGLES(sv_client, i));

#if USE_MVD_CLIENT
        if (sv.state == ss_broadcast) {
            // spectators only need to know about inline BSP models
            if (base->solid != PACKED_BSP)
                base->solid = 0;
        } else
#endif
        if (sv_client->esFlags & MSG_ES_LONGSOLID) {
            base->solid = sv.entities[i].solid32;
        }
    }
}

static void write_plain_configstrings(void)
{
    int     i;
    char    *string;
    size_t  length;

    // write a packet full of data
    string = sv_client->configstrings;
    for (i = 0; i < MAX_CONFIGSTRINGS; i++, string += MAX_QPATH) {
        if (!string[0]) {
            continue;
        }
        length = strlen(string);
        if (length > MAX_QPATH) {
            length = MAX_QPATH;
        }
        // check if this configstring will overflow
        if (msg_write.cursize + length + 64 > sv_client->netchan->maxpacketlen) {
            SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
        }

        MSG_WriteByte(svc_configstring);
        MSG_WriteShort(i);
        MSG_WriteData(string, length);
        MSG_WriteByte(0);
    }

    SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
}

static void write_baseline(entity_packed_t *base)
{
    msgEsFlags_t flags = sv_client->esFlags | MSG_ES_FORCE;

    if (Q2PRO_SHORTANGLES(sv_client, base->number)) {
        flags |= MSG_ES_SHORTANGLES;
    }

    MSG_WriteDeltaEntity(NULL, base, flags);
}

static void write_plain_baselines(void)
{
    int i, j;
    entity_packed_t *base;

    // write a packet full of data
    for (i = 0; i < SV_BASELINES_CHUNKS; i++) {
        base = sv_client->baselines[i];
        if (!base) {
            continue;
        }
        for (j = 0; j < SV_BASELINES_PER_CHUNK; j++) {
            if (base->number) {
                // check if this baseline will overflow
                if (msg_write.cursize + 64 > sv_client->netchan->maxpacketlen) {
                    SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
                }

                MSG_WriteByte(svc_spawnbaseline);
                write_baseline(base);
            }
            base++;
        }
    }

    SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
}

#if USE_ZLIB

static void write_compressed_gamestate(void)
{
    sizebuf_t   *buf = &sv_client->netchan->message;
    entity_packed_t  *base;
    int         i, j;
    size_t      length;
    uint8_t     *patch;
    char        *string;

    MSG_WriteByte(svc_gamestate);

    // write configstrings
    string = sv_client->configstrings;
    for (i = 0; i < MAX_CONFIGSTRINGS; i++, string += MAX_QPATH) {
        if (!string[0]) {
            continue;
        }
        length = strlen(string);
        if (length > MAX_QPATH) {
            length = MAX_QPATH;
        }

        MSG_WriteShort(i);
        MSG_WriteData(string, length);
        MSG_WriteByte(0);
    }
    MSG_WriteShort(MAX_CONFIGSTRINGS);   // end of configstrings

    // write baselines
    for (i = 0; i < SV_BASELINES_CHUNKS; i++) {
        base = sv_client->baselines[i];
        if (!base) {
            continue;
        }
        for (j = 0; j < SV_BASELINES_PER_CHUNK; j++) {
            if (base->number) {
                write_baseline(base);
            }
            base++;
        }
    }
    MSG_WriteShort(0);   // end of baselines

    SZ_WriteByte(buf, svc_zpacket);
    patch = SZ_GetSpace(buf, 2);
    SZ_WriteShort(buf, msg_write.cursize);

    deflateReset(&svs.z);
    svs.z.next_in = msg_write.data;
    svs.z.avail_in = (uInt)msg_write.cursize;
    svs.z.next_out = buf->data + buf->cursize;
    svs.z.avail_out = (uInt)(buf->maxsize - buf->cursize);
    SZ_Clear(&msg_write);

    if (deflate(&svs.z, Z_FINISH) != Z_STREAM_END) {
        SV_DropClient(sv_client, "deflate() failed on gamestate");
        return;
    }

    SV_DPrintf(0, "%s: comp: %lu into %lu\n",
               sv_client->name, svs.z.total_in, svs.z.total_out);

    patch[0] = svs.z.total_out & 255;
    patch[1] = (svs.z.total_out >> 8) & 255;
    buf->cursize += svs.z.total_out;
}

static inline int z_flush(byte *buffer)
{
    int ret;

    ret = deflate(&svs.z, Z_FINISH);
    if (ret != Z_STREAM_END) {
        return ret;
    }

    SV_DPrintf(0, "%s: comp: %lu into %lu\n",
               sv_client->name, svs.z.total_in, svs.z.total_out);

    MSG_WriteByte(svc_zpacket);
    MSG_WriteShort(svs.z.total_out);
    MSG_WriteShort(svs.z.total_in);
    MSG_WriteData(buffer, svs.z.total_out);

    SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);

    return ret;
}

static inline void z_reset(byte *buffer)
{
    deflateReset(&svs.z);
    svs.z.next_out = buffer;
    svs.z.avail_out = (uInt)(sv_client->netchan->maxpacketlen - 5);
}

static void write_compressed_configstrings(void)
{
    int     i;
    size_t  length;
    byte    buffer[MAX_PACKETLEN_WRITABLE];
    char    *string;

    z_reset(buffer);

    // write a packet full of data
    string = sv_client->configstrings;
    for (i = 0; i < MAX_CONFIGSTRINGS; i++, string += MAX_QPATH) {
        if (!string[0]) {
            continue;
        }
        length = strlen(string);
        if (length > MAX_QPATH) {
            length = MAX_QPATH;
        }

        // check if this configstring will overflow
        if (svs.z.avail_out < length + 32) {
            // then flush compressed data
            if (z_flush(buffer) != Z_STREAM_END) {
                goto fail;
            }
            z_reset(buffer);
        }

        MSG_WriteByte(svc_configstring);
        MSG_WriteShort(i);
        MSG_WriteData(string, length);
        MSG_WriteByte(0);

        svs.z.next_in = msg_write.data;
        svs.z.avail_in = (uInt)msg_write.cursize;
        SZ_Clear(&msg_write);

        if (deflate(&svs.z, Z_SYNC_FLUSH) != Z_OK) {
            goto fail;
        }
    }

    // finally flush all remaining compressed data
    if (z_flush(buffer) != Z_STREAM_END) {
fail:
        SV_DropClient(sv_client, "deflate() failed on configstrings");
    }
}

#endif // USE_ZLIB

static void stuff_cmds(list_t *list)
{
    stuffcmd_t *stuff;

    LIST_FOR_EACH(stuffcmd_t, stuff, list, entry) {
        MSG_WriteByte(svc_stufftext);
        MSG_WriteData(stuff->string, stuff->len);
        MSG_WriteByte('\n');
        MSG_WriteByte(0);
        SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
    }
}

static void stuff_junk(void)
{
    static const char junkchars[] =
        "!~#``&'()*`+,-./~01~2`3`4~5`67`89:~<=`>?@~ab~c"
        "d`ef~j~k~lm`no~pq`rst`uv`w``x`yz[`\\]^_`|~";
    char junk[8][16];
    int i, j, k;

    for (i = 0; i < 8; i++) {
        for (j = 0; j < 15; j++) {
            k = rand_byte() % (sizeof(junkchars) - 1);
            junk[i][j] = junkchars[k];
        }
        junk[i][15] = 0;
    }

    strcpy(sv_client->reconnect_var, junk[2]);
    strcpy(sv_client->reconnect_val, junk[3]);

    SV_ClientCommand(sv_client, "set %s set\n", junk[0]);
    SV_ClientCommand(sv_client, "$%s %s connect\n", junk[0], junk[1]);
    if (rand_byte() & 1) {
        SV_ClientCommand(sv_client, "$%s %s %s\n", junk[0], junk[2], junk[3]);
        SV_ClientCommand(sv_client, "$%s %s %s\n", junk[0], junk[4],
                         sv_force_reconnect->string);
        SV_ClientCommand(sv_client, "$%s %s %s\n", junk[0], junk[5], junk[6]);
    } else {
        SV_ClientCommand(sv_client, "$%s %s %s\n", junk[0], junk[4],
                         sv_force_reconnect->string);
        SV_ClientCommand(sv_client, "$%s %s %s\n", junk[0], junk[5], junk[6]);
        SV_ClientCommand(sv_client, "$%s %s %s\n", junk[0], junk[2], junk[3]);
    }
    SV_ClientCommand(sv_client, "$%s %s \"\"\n", junk[0], junk[0]);
    SV_ClientCommand(sv_client, "$%s $%s\n", junk[1], junk[4]);
}

/*
================
SV_New_f

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each server load.
================
*/
void SV_New_f(void)
{
    clstate_t oldstate;

    Com_DPrintf("New() from %s\n", sv_client->name);

    oldstate = sv_client->state;
    if (sv_client->state < cs_connected) {
        Com_DPrintf("Going from cs_assigned to cs_connected for %s\n",
                    sv_client->name);
        sv_client->state = cs_connected;
        sv_client->lastmessage = svs.realtime; // don't timeout
        time(&sv_client->connect_time);
    } else if (sv_client->state > cs_connected) {
        Com_DPrintf("New not valid -- already primed\n");
        return;
    }

    // stuff some junk, drop them and expect them to be back soon
    if (sv_force_reconnect->string[0] && !sv_client->reconnect_var[0] &&
        !NET_IsLocalAddress(&sv_client->netchan->remote_address)) {
        stuff_junk();
        SV_DropClient(sv_client, NULL);
        return;
    }

    SV_ClientCommand(sv_client, "\n");

    //
    // serverdata needs to go over for all types of servers
    // to make sure the protocol is right, and to set the gamedir
    //

    // create baselines for this client
    create_baselines();

    // send the serverdata
    MSG_WriteByte(svc_serverdata);
    MSG_WriteLong(sv_client->protocol);
    MSG_WriteLong(sv_client->spawncount);
    MSG_WriteByte(0);   // no attract loop
    MSG_WriteString(sv_client->gamedir);
    if (sv.state == ss_pic)
        MSG_WriteShort(-1);
    else
        MSG_WriteShort(sv_client->slot);
    MSG_WriteString(&sv_client->configstrings[CS_NAME * MAX_QPATH]);

    // send protocol specific stuff
    switch (sv_client->protocol) {
    case PROTOCOL_VERSION_R1Q2:
        MSG_WriteByte(0);   // not enhanced
        MSG_WriteShort(sv_client->version);
        MSG_WriteByte(0);   // no advanced deltas
        MSG_WriteByte(sv_client->pmp.strafehack);
        break;
    case PROTOCOL_VERSION_Q2PRO:
        MSG_WriteShort(sv_client->version);
        MSG_WriteByte(sv.state);
        MSG_WriteByte(sv_client->pmp.strafehack);
        MSG_WriteByte(sv_client->pmp.qwmode);
        if (sv_client->version >= PROTOCOL_VERSION_Q2PRO_WATERJUMP_HACK) {
            MSG_WriteByte(sv_client->pmp.waterhack);
        }
        break;
    default:
        break;
    }

    SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);

    SV_ClientCommand(sv_client, "\n");

    // send version string request
    if (oldstate == cs_assigned) {
        SV_ClientCommand(sv_client, "cmd \177c version $version\n"
#if USE_AC_SERVER
                         "cmd \177c actoken $actoken\n"
#endif
                        );
        stuff_cmds(&sv_cmdlist_connect);
    }

    // send reconnect var request
    if (sv_force_reconnect->string[0] && !sv_client->reconnected) {
        SV_ClientCommand(sv_client, "cmd \177c connect $%s\n",
                         sv_client->reconnect_var);
    }

    Com_DPrintf("Going from cs_connected to cs_primed for %s\n",
                sv_client->name);
    sv_client->state = cs_primed;

    memset(&sv_client->lastcmd, 0, sizeof(sv_client->lastcmd));

    if (sv.state == ss_pic)
        return;

#if USE_ZLIB
    if (sv_client->has_zlib) {
        if (sv_client->netchan->type == NETCHAN_NEW) {
            write_compressed_gamestate();
        } else {
            // FIXME: Z_SYNC_FLUSH is not efficient for baselines
            write_compressed_configstrings();
            write_plain_baselines();
        }
    } else
#endif // USE_ZLIB
    {
        write_plain_configstrings();
        write_plain_baselines();
    }

    // send next command
    SV_ClientCommand(sv_client, "precache %i\n", sv_client->spawncount);
}

/*
==================
SV_Begin_f
==================
*/
void SV_Begin_f(void)
{
    Com_DPrintf("Begin() from %s\n", sv_client->name);

    // handle the case of a level changing while a client was connecting
    if (sv_client->state < cs_primed) {
        Com_DPrintf("Begin not valid -- not yet primed\n");
        SV_New_f();
        return;
    }
    if (sv_client->state > cs_primed) {
        Com_DPrintf("Begin not valid -- already spawned\n");
        return;
    }

    if (!sv_client->version_string) {
        SV_DropClient(sv_client, "!failed version probe");
        return;
    }

    if (sv_force_reconnect->string[0] && !sv_client->reconnected) {
        SV_DropClient(sv_client, "!failed to reconnect");
        return;
    }

    if (!AC_ClientBegin(sv_client)) {
        return;
    }

    Com_DPrintf("Going from cs_primed to cs_spawned for %s\n",
                sv_client->name);
    sv_client->state = cs_spawned;
    sv_client->send_delta = 0;
    sv_client->command_msec = 1800;
    sv_client->suppress_count = 0;
    sv_client->http_download = qfalse;

    SV_AlignKeyFrames(sv_client);

    stuff_cmds(&sv_cmdlist_begin);

    // call the game begin function
    ge->ClientBegin(sv_player);

    AC_ClientAnnounce(sv_client);
}

//=============================================================================

void SV_CloseDownload(client_t *client)
{
    if (client->download) {
        Z_Free(client->download);
        client->download = NULL;
    }
    if (client->downloadname) {
        Z_Free(client->downloadname);
        client->downloadname = NULL;
    }
    client->downloadsize = 0;
    client->downloadcount = 0;
    client->downloadcmd = 0;
    client->downloadpending = qfalse;
}

/*
==================
SV_NextDownload_f
==================
*/
static void SV_NextDownload_f(void)
{
    if (!sv_client->download)
        return;

    sv_client->downloadpending = qtrue;
}

/*
==================
SV_BeginDownload_f
==================
*/
static void SV_BeginDownload_f(void)
{
    char    name[MAX_QPATH];
    byte    *download;
    int     downloadcmd;
    ssize_t downloadsize, maxdownloadsize, result;
    int     offset = 0;
    cvar_t  *allow;
    size_t  len;
    qhandle_t f;

    len = Cmd_ArgvBuffer(1, name, sizeof(name));
    if (len >= MAX_QPATH) {
        goto fail1;
    }

    // hack for 'status' command
    if (!strcmp(name, "http")) {
        sv_client->http_download = qtrue;
        return;
    }

    len = FS_NormalizePath(name, name);

    if (Cmd_Argc() > 2)
        offset = atoi(Cmd_Argv(2));     // downloaded offset

    // hacked by zoid to allow more conrol over download
    // first off, no .. or global allow check
    if (!allow_download->integer
        // check for empty paths
        || !len
        // check for illegal negative offsets
        || offset < 0
        // don't allow anything with .. path
        || strstr(name, "..")
        // leading dots, slashes, etc are no good
        || !Q_ispath(name[0])
        // trailing dots, slashes, etc are no good
        || !Q_ispath(name[len - 1])
        // MUST be in a subdirectory
        || !strchr(name, '/')) {
        Com_DPrintf("Refusing download of %s to %s\n", name, sv_client->name);
        goto fail1;
    }

    if (FS_pathcmpn(name, CONST_STR_LEN("players/")) == 0) {
        allow = allow_download_players;
    } else if (FS_pathcmpn(name, CONST_STR_LEN("models/")) == 0 ||
               FS_pathcmpn(name, CONST_STR_LEN("sprites/")) == 0) {
        allow = allow_download_models;
    } else if (FS_pathcmpn(name, CONST_STR_LEN("sound/")) == 0) {
        allow = allow_download_sounds;
    } else if (FS_pathcmpn(name, CONST_STR_LEN("maps/")) == 0) {
        allow = allow_download_maps;
    } else if (FS_pathcmpn(name, CONST_STR_LEN("textures/")) == 0 ||
               FS_pathcmpn(name, CONST_STR_LEN("env/")) == 0) {
        allow = allow_download_textures;
    } else if (FS_pathcmpn(name, CONST_STR_LEN("pics/")) == 0) {
        allow = allow_download_pics;
    } else {
        allow = allow_download_others;
    }

    if (!allow->integer) {
        Com_DPrintf("Refusing download of %s to %s\n", name, sv_client->name);
        goto fail1;
    }

    if (sv_client->download) {
        Com_DPrintf("Closing existing download for %s (should not happen)\n", sv_client->name);
        SV_CloseDownload(sv_client);
    }

    f = 0;
    downloadcmd = svc_download;

#if USE_ZLIB
    // prefer raw deflate stream from .pkz if supported
    if (sv_client->protocol == PROTOCOL_VERSION_Q2PRO &&
        sv_client->version >= PROTOCOL_VERSION_Q2PRO_ZLIB_DOWNLOADS &&
        sv_client->has_zlib && offset == 0) {
        downloadsize = FS_FOpenFile(name, &f, FS_MODE_READ | FS_FLAG_DEFLATE);
        if (f) {
            Com_DPrintf("Serving compressed download to %s\n", sv_client->name);
            downloadcmd = svc_zdownload;
        }
    }
#endif

    if (!f) {
        downloadsize = FS_FOpenFile(name, &f, FS_MODE_READ);
        if (!f) {
            Com_DPrintf("Couldn't download %s to %s\n", name, sv_client->name);
            goto fail1;
        }
    }

    maxdownloadsize = MAX_LOADFILE;
#if 0
    if (sv_max_download_size->integer) {
        maxdownloadsize = Cvar_ClampInteger(sv_max_download_size, 1, MAX_LOADFILE);
    }
#endif

    if (downloadsize == 0) {
        Com_DPrintf("Refusing empty download of %s to %s\n", name, sv_client->name);
        goto fail2;
    }

    if (downloadsize > maxdownloadsize) {
        Com_DPrintf("Refusing oversize download of %s to %s\n", name, sv_client->name);
        goto fail2;
    }

    if (offset > downloadsize) {
        Com_DPrintf("Refusing download, %s has wrong version of %s (%d > %d)\n",
                    sv_client->name, name, offset, (int)downloadsize);
        SV_ClientPrintf(sv_client, PRINT_HIGH, "File size differs from server.\n"
                        "Please delete the corresponding .tmp file from your system.\n");
        goto fail2;
    }

    if (offset == downloadsize) {
        Com_DPrintf("Refusing download, %s already has %s (%d bytes)\n",
                    sv_client->name, name, offset);
        FS_FCloseFile(f);
        MSG_WriteByte(svc_download);
        MSG_WriteShort(0);
        MSG_WriteByte(100);
        SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
        return;
    }

    download = SV_Malloc(downloadsize);
    result = FS_Read(download, downloadsize, f);
    if (result != downloadsize) {
        Com_DPrintf("Couldn't download %s to %s\n", name, sv_client->name);
        goto fail3;
    }

    FS_FCloseFile(f);

    sv_client->download = download;
    sv_client->downloadsize = downloadsize;
    sv_client->downloadcount = offset;
    sv_client->downloadname = SV_CopyString(name);
    sv_client->downloadcmd = downloadcmd;
    sv_client->downloadpending = qtrue;

    Com_DPrintf("Downloading %s to %s\n", name, sv_client->name);
    return;

fail3:
    Z_Free(download);
fail2:
    FS_FCloseFile(f);
fail1:
    MSG_WriteByte(svc_download);
    MSG_WriteShort(-1);
    MSG_WriteByte(0);
    SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
}

static void SV_StopDownload_f(void)
{
    int percent;

    if (!sv_client->download)
        return;

    percent = sv_client->downloadcount * 100 / sv_client->downloadsize;

    MSG_WriteByte(svc_download);
    MSG_WriteShort(-1);
    MSG_WriteByte(percent);
    SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);

    Com_DPrintf("Download of %s to %s stopped by user request\n",
                sv_client->downloadname, sv_client->name);
    SV_CloseDownload(sv_client);
    SV_AlignKeyFrames(sv_client);
}

//============================================================================

// special hack for end game screen in coop mode
static void SV_NextServer_f(void)
{
    if (sv.state != ss_pic)
        return;     // can't nextserver while playing a normal game

    if (Q_stricmp(sv.name, "victory.pcx"))
        return;

    if (Cvar_VariableInteger("deathmatch"))
        return;

    sv.name[0] = 0; // make sure another doesn't sneak in

    if (Cvar_VariableInteger("coop"))
        Cbuf_AddText(&cmd_buffer, "gamemap \"*base1\"\n");
    else
        Cbuf_AddText(&cmd_buffer, "killserver\n");
}

// the client is going to disconnect, so remove the connection immediately
static void SV_Disconnect_f(void)
{
    SV_DropClient(sv_client, "!?disconnected");
    SV_RemoveClient(sv_client);   // don't bother with zombie state
}

// dumps the serverinfo info string
static void SV_ShowServerInfo_f(void)
{
    char serverinfo[MAX_INFO_STRING];

    Cvar_BitInfo(serverinfo, CVAR_SERVERINFO);

    SV_ClientRedirect();
    Info_Print(serverinfo);
    Com_EndRedirect();
}

// dumps misc protocol info
static void SV_ShowMiscInfo_f(void)
{
    SV_ClientRedirect();
    SV_PrintMiscInfo();
    Com_EndRedirect();
}

static void SV_NoGameData_f(void)
{
    sv_client->nodata ^= 1;
    SV_AlignKeyFrames(sv_client);
}

static void SV_Lag_f(void)
{
    client_t *cl;

    if (Cmd_Argc() > 1) {
        SV_ClientRedirect();
        cl = SV_GetPlayer(Cmd_Argv(1), qtrue);
        Com_EndRedirect();
        if (!cl) {
            return;
        }
    } else {
        cl = sv_client;
    }

    SV_ClientPrintf(sv_client, PRINT_HIGH,
                    "Lag stats for:       %s\n"
                    "RTT (min/avg/max):   %d/%d/%d ms\n"
                    "Server to client PL: %.2f%% (approx)\n"
                    "Client to server PL: %.2f%%\n",
                    cl->name, cl->min_ping, AVG_PING(cl), cl->max_ping,
                    PL_S2C(cl), PL_C2S(cl));
}

#if USE_PACKETDUP
static void SV_PacketdupHack_f(void)
{
    int numdups = sv_client->numpackets - 1;

    if (Cmd_Argc() > 1) {
        numdups = atoi(Cmd_Argv(1));
        if (numdups < 0 || numdups > sv_packetdup_hack->integer) {
            SV_ClientPrintf(sv_client, PRINT_HIGH,
                            "Packetdup of %d is not allowed on this server.\n", numdups);
            return;
        }

        sv_client->numpackets = numdups + 1;
    }

    SV_ClientPrintf(sv_client, PRINT_HIGH,
                    "Server is sending %d duplicate packet%s to you.\n",
                    numdups, numdups == 1 ? "" : "s");
}
#endif

static void SV_CvarResult_f(void)
{
    char *c, *v;

    c = Cmd_Argv(1);
    if (!strcmp(c, "version")) {
        if (!sv_client->version_string) {
            v = Cmd_RawArgsFrom(2);
            if (COM_DEDICATED) {
                Com_Printf("%s[%s]: %s\n", sv_client->name,
                           NET_AdrToString(&sv_client->netchan->remote_address), v);
            }
            sv_client->version_string = SV_CopyString(v);
        }
    } else if (!strcmp(c, "connect")) {
        if (sv_client->reconnect_var[0]) {
            if (!strcmp(Cmd_Argv(2), sv_client->reconnect_val)) {
                sv_client->reconnected = qtrue;
            }
        }
    } else if (!strcmp(c, "actoken")) {
        AC_ClientToken(sv_client, Cmd_Argv(2));
    } else if (!strcmp(c, "console")) {
        if (sv_client->console_queries > 0) {
            Com_Printf("%s[%s]: \"%s\" is \"%s\"\n", sv_client->name,
                       NET_AdrToString(&sv_client->netchan->remote_address),
                       Cmd_Argv(2), Cmd_RawArgsFrom(3));
            sv_client->console_queries--;
        }
    }
}

static void SV_AC_List_f(void)
{
    SV_ClientRedirect();
    AC_List_f();
    Com_EndRedirect();
}

static void SV_AC_Info_f(void)
{
    SV_ClientRedirect();
    AC_Info_f();
    Com_EndRedirect();
}

static const ucmd_t ucmds[] = {
    // auto issued
    { "new", SV_New_f },
    { "begin", SV_Begin_f },
    { "baselines", NULL },
    { "configstrings", NULL },
    { "nextserver", SV_NextServer_f },
    { "disconnect", SV_Disconnect_f },

    // issued by hand at client consoles
    { "info", SV_ShowServerInfo_f },
    { "sinfo", SV_ShowMiscInfo_f },

    { "download", SV_BeginDownload_f },
    { "nextdl", SV_NextDownload_f },
    { "stopdl", SV_StopDownload_f },

    { "\177c", SV_CvarResult_f },
    { "nogamedata", SV_NoGameData_f },
    { "lag", SV_Lag_f },
#if USE_PACKETDUP
    { "packetdup", SV_PacketdupHack_f },
#endif
    { "aclist", SV_AC_List_f },
    { "acinfo", SV_AC_Info_f },

    { NULL, NULL }
};

static void handle_filtercmd(filtercmd_t *filter)
{
    size_t len;

    switch (filter->action) {
    case FA_PRINT:
        MSG_WriteByte(svc_print);
        MSG_WriteByte(PRINT_HIGH);
        break;
    case FA_STUFF:
        MSG_WriteByte(svc_stufftext);
        break;
    case FA_KICK:
        SV_DropClient(sv_client, filter->comment[0] ?
                      filter->comment : "issued banned command");
        // fall through
    default:
        return;
    }

    len = strlen(filter->comment);
    MSG_WriteData(filter->comment, len);
    MSG_WriteByte('\n');
    MSG_WriteByte(0);

    SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
}

/*
==================
SV_ExecuteUserCommand
==================
*/
static void SV_ExecuteUserCommand(const char *s)
{
    const ucmd_t *u;
    filtercmd_t *filter;
    char *c;

    Cmd_TokenizeString(s, qfalse);
    sv_player = sv_client->edict;

    c = Cmd_Argv(0);
    if (!c[0]) {
        return;
    }

    if ((u = Com_Find(ucmds, c)) != NULL) {
        if (u->func) {
            u->func();
        }
        return;
    }

    if (sv.state == ss_pic) {
        return;
    }

    if (sv_client->state != cs_spawned && !sv_allow_unconnected_cmds->integer) {
        return;
    }

    LIST_FOR_EACH(filtercmd_t, filter, &sv_filterlist, entry) {
        if (!Q_stricmp(filter->string, c)) {
            handle_filtercmd(filter);
            return;
        }
    }

    if (!strcmp(c, "say") || !strcmp(c, "say_team")) {
        // don't timeout. only chat commands count as activity.
        sv_client->lastactivity = svs.realtime;
    }

    ge->ClientCommand(sv_player);
}

/*
===========================================================================

USER CMD EXECUTION

===========================================================================
*/

static qboolean    moveIssued;
static int         stringCmdCount;
static int         userinfoUpdateCount;

/*
==================
SV_ClientThink
==================
*/
static inline void SV_ClientThink(usercmd_t *cmd)
{
    usercmd_t *old = &sv_client->lastcmd;

    sv_client->command_msec -= cmd->msec;
    sv_client->num_moves++;

    if (sv_client->command_msec < 0 && sv_enforcetime->integer) {
        Com_DPrintf("commandMsec underflow from %s: %d\n",
                    sv_client->name, sv_client->command_msec);
        return;
    }

    if (cmd->buttons != old->buttons
        || cmd->forwardmove != old->forwardmove
        || cmd->sidemove != old->sidemove
        || cmd->upmove != old->upmove) {
        // don't timeout
        sv_client->lastactivity = svs.realtime;
    }

    ge->ClientThink(sv_player, cmd);
}

static void SV_SetLastFrame(int lastframe)
{
    client_frame_t *frame;

    if (lastframe > 0) {
        if (lastframe >= sv_client->framenum)
            return; // ignore invalid acks

        if (lastframe <= sv_client->lastframe)
            return; // ignore duplicate acks

        if (sv_client->framenum - lastframe <= UPDATE_BACKUP) {
            frame = &sv_client->frames[lastframe & UPDATE_MASK];

            if (frame->number == lastframe) {
                // save time for ping calc
                if (frame->sentTime <= com_eventTime)
                    frame->latency = com_eventTime - frame->sentTime;
            }
        }

        // count valid ack
        sv_client->frames_acked++;
    }

    sv_client->lastframe = lastframe;
}

/*
==================
SV_OldClientExecuteMove
==================
*/
static void SV_OldClientExecuteMove(void)
{
    usercmd_t   oldest, oldcmd, newcmd;
    int         lastframe;
    int         net_drop;

    if (moveIssued) {
        SV_DropClient(sv_client, "multiple clc_move commands in packet");
        return;     // someone is trying to cheat...
    }

    moveIssued = qtrue;

    if (sv_client->protocol == PROTOCOL_VERSION_DEFAULT) {
        MSG_ReadByte();    // skip over checksum
    }

    lastframe = MSG_ReadLong();

    // read all cmds
    if (sv_client->protocol == PROTOCOL_VERSION_R1Q2 &&
        sv_client->version >= PROTOCOL_VERSION_R1Q2_UCMD) {
        MSG_ReadDeltaUsercmd_Hacked(NULL, &oldest);
        MSG_ReadDeltaUsercmd_Hacked(&oldest, &oldcmd);
        MSG_ReadDeltaUsercmd_Hacked(&oldcmd, &newcmd);
    } else {
        MSG_ReadDeltaUsercmd(NULL, &oldest);
        MSG_ReadDeltaUsercmd(&oldest, &oldcmd);
        MSG_ReadDeltaUsercmd(&oldcmd, &newcmd);
    }

    if (sv_client->state != cs_spawned) {
        SV_SetLastFrame(-1);
        return;
    }

    SV_SetLastFrame(lastframe);

    net_drop = sv_client->netchan->dropped;
    if (net_drop > 2) {
        sv_client->frameflags |= FF_CLIENTPRED;
    }

    if (net_drop < 20) {
        // run lastcmd multiple times if no backups available
        while (net_drop > 2) {
            SV_ClientThink(&sv_client->lastcmd);
            net_drop--;
        }

        // run backup cmds
        if (net_drop > 1)
            SV_ClientThink(&oldest);
        if (net_drop > 0)
            SV_ClientThink(&oldcmd);
    }

    // run new cmd
    SV_ClientThink(&newcmd);

    sv_client->lastcmd = newcmd;
}

/*
==================
SV_NewClientExecuteMove
==================
*/
static void SV_NewClientExecuteMove(int c)
{
    usercmd_t   cmds[MAX_PACKET_FRAMES][MAX_PACKET_USERCMDS];
    usercmd_t   *lastcmd, *cmd;
    int         lastframe;
    int         numCmds[MAX_PACKET_FRAMES], numDups;
    int         i, j, lightlevel;
    int         net_drop;

    if (moveIssued) {
        SV_DropClient(sv_client, "multiple clc_move commands in packet");
        return;     // someone is trying to cheat...
    }

    moveIssued = qtrue;

    numDups = c >> SVCMD_BITS;
    c &= SVCMD_MASK;

    if (numDups >= MAX_PACKET_FRAMES) {
        SV_DropClient(sv_client, "too many frames in packet");
        return;
    }

    if (c == clc_move_nodelta) {
        lastframe = -1;
    } else {
        lastframe = MSG_ReadLong();
    }

    lightlevel = MSG_ReadByte();

    // read all cmds
    lastcmd = NULL;
    for (i = 0; i <= numDups; i++) {
        numCmds[i] = MSG_ReadBits(5);
        if (numCmds[i] == -1) {
            SV_DropClient(sv_client, "read past end of message");
            return;
        }
        if (numCmds[i] >= MAX_PACKET_USERCMDS) {
            SV_DropClient(sv_client, "too many usercmds in frame");
            return;
        }
        for (j = 0; j < numCmds[i]; j++) {
            if (msg_read.readcount > msg_read.cursize) {
                SV_DropClient(sv_client, "read past end of message");
                return;
            }
            cmd = &cmds[i][j];
            MSG_ReadDeltaUsercmd_Enhanced(lastcmd, cmd, sv_client->version);
            cmd->lightlevel = lightlevel;
            lastcmd = cmd;
        }
    }

    if (sv_client->state != cs_spawned) {
        SV_SetLastFrame(-1);
        return;
    }

    SV_SetLastFrame(lastframe);

    if (q_unlikely(!lastcmd)) {
        return; // should never happen
    }

    net_drop = sv_client->netchan->dropped;
    if (net_drop > numDups) {
        sv_client->frameflags |= FF_CLIENTPRED;
    }

    if (net_drop < 20) {
        // run lastcmd multiple times if no backups available
        while (net_drop > numDups) {
            SV_ClientThink(&sv_client->lastcmd);
            net_drop--;
        }

        // run backup cmds, if any
        while (net_drop > 0) {
            i = numDups - net_drop;
            for (j = 0; j < numCmds[i]; j++) {
                SV_ClientThink(&cmds[i][j]);
            }
            net_drop--;
        }

    }

    // run new cmds
    for (j = 0; j < numCmds[numDups]; j++) {
        SV_ClientThink(&cmds[numDups][j]);
    }

    sv_client->lastcmd = *lastcmd;
}

/*
=================
SV_UpdateUserinfo

Ensures that userinfo is valid and name is properly set.
=================
*/
static void SV_UpdateUserinfo(void)
{
    char *s;

    if (!sv_client->userinfo[0]) {
        SV_DropClient(sv_client, "empty userinfo");
        return;
    }

    if (!Info_Validate(sv_client->userinfo)) {
        SV_DropClient(sv_client, "malformed userinfo");
        return;
    }

    // validate name
    s = Info_ValueForKey(sv_client->userinfo, "name");
    s[MAX_CLIENT_NAME - 1] = 0;
    if (COM_IsWhite(s) || (sv_client->name[0] && strcmp(sv_client->name, s) &&
                           SV_RateLimited(&sv_client->ratelimit_namechange))) {
        if (!sv_client->name[0]) {
            SV_DropClient(sv_client, "malformed name");
            return;
        }
        if (!Info_SetValueForKey(sv_client->userinfo, "name", sv_client->name)) {
            SV_DropClient(sv_client, "oversize userinfo");
            return;
        }
        if (COM_IsWhite(s))
            SV_ClientPrintf(sv_client, PRINT_HIGH, "You can't have an empty name.\n");
        else
            SV_ClientPrintf(sv_client, PRINT_HIGH, "You can't change your name too often.\n");
        SV_ClientCommand(sv_client, "set name \"%s\"\n", sv_client->name);
    }

    SV_UserinfoChanged(sv_client);
}

static void SV_ParseFullUserinfo(void)
{
    size_t len;

    // malicious users may try sending too many userinfo updates
    if (userinfoUpdateCount >= MAX_PACKET_USERINFOS) {
        Com_DPrintf("Too many userinfos from %s\n", sv_client->name);
        MSG_ReadString(NULL, 0);
        return;
    }

    len = MSG_ReadString(sv_client->userinfo, sizeof(sv_client->userinfo));
    if (len >= sizeof(sv_client->userinfo)) {
        SV_DropClient(sv_client, "oversize userinfo");
        return;
    }

    Com_DDPrintf("%s(%s): %s [%d]\n", __func__,
                 sv_client->name, sv_client->userinfo, userinfoUpdateCount);

    SV_UpdateUserinfo();
    userinfoUpdateCount++;
}

static void SV_ParseDeltaUserinfo(void)
{
    char key[MAX_INFO_KEY], value[MAX_INFO_VALUE];
    size_t len;

    // malicious users may try sending too many userinfo updates
    if (userinfoUpdateCount >= MAX_PACKET_USERINFOS) {
        Com_DPrintf("Too many userinfos from %s\n", sv_client->name);
        MSG_ReadString(NULL, 0);
        MSG_ReadString(NULL, 0);
        return;
    }

    // optimize by combining multiple delta updates into one (hack)
    while (1) {
        len = MSG_ReadString(key, sizeof(key));
        if (len >= sizeof(key)) {
            SV_DropClient(sv_client, "oversize delta key");
            return;
        }

        len = MSG_ReadString(value, sizeof(value));
        if (len >= sizeof(value)) {
            SV_DropClient(sv_client, "oversize delta value");
            return;
        }

        if (userinfoUpdateCount < MAX_PACKET_USERINFOS) {
            if (!Info_SetValueForKey(sv_client->userinfo, key, value)) {
                SV_DropClient(sv_client, "malformed userinfo");
                return;
            }

            Com_DDPrintf("%s(%s): %s %s [%d]\n", __func__,
                         sv_client->name, key, value, userinfoUpdateCount);

            userinfoUpdateCount++;
        } else {
            Com_DPrintf("Too many userinfos from %s\n", sv_client->name);
        }

        if (msg_read.readcount >= msg_read.cursize)
            break; // end of message

        if (msg_read.data[msg_read.readcount] != clc_userinfo_delta)
            break; // not delta userinfo

        msg_read.readcount++;
    }

    SV_UpdateUserinfo();
}

#if USE_FPS
void SV_AlignKeyFrames(client_t *client)
{
    int framediv = sv.framediv / client->framediv;
    int framenum = sv.framenum / client->framediv;
    int frameofs = framenum % framediv;
    int newnum = frameofs + Q_align(client->framenum, framediv);

    Com_DPrintf("[%d] align %d --> %d (num = %d, div = %d, ofs = %d)\n",
                sv.framenum, client->framenum, newnum, framenum, framediv, frameofs);
    client->framenum = newnum;
}

static void set_client_fps(int value)
{
    int framediv, framerate;

    // 0 means highest
    if (!value)
        value = sv.framerate;

    framediv = value / BASE_FRAMERATE;

    clamp(framediv, 1, MAX_FRAMEDIV);

    framediv = sv.framediv / Q_gcd(sv.framediv, framediv);
    framerate = sv.framerate / framediv;

    Com_DPrintf("[%d] client div=%d, server div=%d, rate=%d\n",
                sv.framenum, framediv, sv.framediv, framerate);

    sv_client->framediv = framediv;

    SV_AlignKeyFrames(sv_client);

    // save for status inspection
    sv_client->settings[CLS_FPS] = framerate;

    MSG_WriteByte(svc_setting);
    MSG_WriteLong(SVS_FPS);
    MSG_WriteLong(framerate);
    SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
}
#endif

static void SV_ParseClientSetting(void)
{
    int idx, value;

    idx = MSG_ReadShort();
    value = MSG_ReadShort();

    Com_DDPrintf("%s(%s): [%d] = %d\n", __func__, sv_client->name, idx, value);

    if (idx < 0 || idx >= CLS_MAX)
        return;

    sv_client->settings[idx] = value;

#if USE_FPS
    if (idx == CLS_FPS && sv_client->protocol == PROTOCOL_VERSION_Q2PRO)
        set_client_fps(value);
#endif
}

static void SV_ParseClientCommand(void)
{
    char buffer[MAX_STRING_CHARS];
    size_t len;

    len = MSG_ReadString(buffer, sizeof(buffer));
    if (len >= sizeof(buffer)) {
        SV_DropClient(sv_client, "oversize stringcmd");
        return;
    }

    // malicious users may try using too many string commands
    if (stringCmdCount >= MAX_PACKET_STRINGCMDS) {
        Com_DPrintf("Too many stringcmds from %s\n", sv_client->name);
        return;
    }

    Com_DDPrintf("%s(%s): %s\n", __func__, sv_client->name, buffer);

    SV_ExecuteUserCommand(buffer);
    stringCmdCount++;
}

/*
===================
SV_ExecuteClientMessage

The current net_message is parsed for the given client
===================
*/
void SV_ExecuteClientMessage(client_t *client)
{
    int c;

    X86_PUSH_FPCW;
    X86_SINGLE_FPCW;

    sv_client = client;
    sv_player = sv_client->edict;

    // only allow one move command
    moveIssued = qfalse;
    stringCmdCount = 0;
    userinfoUpdateCount = 0;

    while (1) {
        if (msg_read.readcount > msg_read.cursize) {
            SV_DropClient(client, "read past end of message");
            break;
        }

        c = MSG_ReadByte();
        if (c == -1)
            break;

        switch (c & SVCMD_MASK) {
        default:
badbyte:
            SV_DropClient(client, "unknown command byte");
            break;

        case clc_nop:
            break;

        case clc_userinfo:
            SV_ParseFullUserinfo();
            break;

        case clc_move:
            SV_OldClientExecuteMove();
            break;

        case clc_stringcmd:
            SV_ParseClientCommand();
            break;

        case clc_setting:
            if (client->protocol < PROTOCOL_VERSION_R1Q2)
                goto badbyte;

            SV_ParseClientSetting();
            break;

        case clc_move_nodelta:
        case clc_move_batched:
            if (client->protocol != PROTOCOL_VERSION_Q2PRO)
                goto badbyte;

            SV_NewClientExecuteMove(c);
            break;

        case clc_userinfo_delta:
            if (client->protocol != PROTOCOL_VERSION_Q2PRO)
                goto badbyte;

            SV_ParseDeltaUserinfo();
            break;
        }

        if (client->state <= cs_zombie)
            break;    // disconnect command
    }

    sv_client = NULL;
    sv_player = NULL;

    X86_POP_FPCW;
}

