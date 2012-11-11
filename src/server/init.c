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

#include "server.h"

server_static_t svs;                // persistant server info
server_t        sv;                 // local server

void SV_ClientReset(client_t *client)
{
    if (client->state < cs_connected) {
        return;
    }

    // any partially connected client will be restarted
    client->state = cs_connected;
    client->framenum = 1; // frame 0 can't be used
    client->lastframe = -1;
    client->frames_nodelta = 0;
    client->send_delta = 0;
    client->suppress_count = 0;
    memset(&client->lastcmd, 0, sizeof(client->lastcmd));
}

#if USE_FPS
static void set_frame_time(void)
{
    int framediv;

    if (g_features->integer & GMF_VARIABLE_FPS)
        framediv = sv_fps->integer / BASE_FRAMERATE;
    else
        framediv = 1;

    clamp(framediv, 1, MAX_FRAMEDIV);

    sv.framerate = framediv * BASE_FRAMERATE;
    sv.frametime = BASE_FRAMETIME / framediv;
    sv.framediv = framediv;

    Cvar_SetInteger(sv_fps, sv.framerate, FROM_CODE);
}
#endif

#if !USE_CLIENT
static void resolve_masters(void)
{
    master_t *m;
    time_t now, delta;

    now = time(NULL);
    FOR_EACH_MASTER(m) {
        // re-resolve valid address after one day,
        // resolve invalid address after three hours
        delta = m->adr.port ? 24 * 60 * 60 : 3 * 60 * 60;
        if (now < m->last_resolved) {
            m->last_resolved = now;
            continue;
        }
        if (now - m->last_resolved < delta) {
            continue;
        }
        if (NET_StringToAdr(m->name, &m->adr, PORT_MASTER)) {
            Com_DPrintf("Master server at %s.\n", NET_AdrToString(&m->adr));
        } else {
            Com_WPrintf("Couldn't resolve master: %s\n", m->name);
            m->adr.port = 0;
        }
        m->last_resolved = now = time(NULL);
    }
}
#endif

// optionally load the entity string from external source
static void override_entity_string(const char *server)
{
    char *path = map_override_path->string;
    char buffer[MAX_QPATH], *str;
    ssize_t len;

    if (!*path) {
        return;
    }

    len = Q_concat(buffer, sizeof(buffer), path, server, ".ent", NULL);
    if (len >= sizeof(buffer)) {
        len = Q_ERR_NAMETOOLONG;
        goto fail1;
    }

    len = SV_LoadFile(buffer, (void **)&str);
    if (!str) {
        if (len == Q_ERR_NOENT) {
            return;
        }
        goto fail1;
    }

    if (len > MAX_MAP_ENTSTRING) {
        len = Q_ERR_FBIG;
        goto fail2;
    }

    Com_Printf("Loaded entity string from %s\n", buffer);
    sv.entitystring = str;
    return;

fail2:
    SV_FreeFile(str);
fail1:
    Com_EPrintf("Couldn't load entity string from %s: %s\n",
                buffer, Q_ErrorString(len));
}


/*
================
SV_SpawnServer

Change the server to a new map, taking all connected
clients along with it.
================
*/
void SV_SpawnServer(cm_t *cm, const char *server, const char *spawnpoint)
{
    int         i;
    client_t    *client;

    SCR_BeginLoadingPlaque();           // for local system

    Com_Printf("------- Server Initialization -------\n");
    Com_Printf("SpawnServer: %s\n", server);

    // everyone needs to reconnect
    FOR_EACH_CLIENT(client) {
        SV_ClientReset(client);
    }

    SV_BroadcastCommand("changing map=%s\n", server);
    SV_SendClientMessages();
    SV_SendAsyncPackets();

    // free current level
    CM_FreeMap(&sv.cm);
    SV_FreeFile(sv.entitystring);

    // wipe the entire per-level structure
    memset(&sv, 0, sizeof(sv));
    sv.spawncount = (rand() | (rand() << 16)) ^ Sys_Milliseconds();
    sv.spawncount &= 0x7FFFFFFF;

    // set legacy spawncounts
    FOR_EACH_CLIENT(client) {
        client->spawncount = sv.spawncount;
    }

    // reset entity counter
    svs.next_entity = 0;

#if USE_FPS
    // set framerate parameters
    set_frame_time();
#endif

    // save name for levels that don't set message
    Q_strlcpy(sv.configstrings[CS_NAME], server, MAX_QPATH);
    Q_strlcpy(sv.name, server, sizeof(sv.name));

    if (Cvar_VariableInteger("deathmatch")) {
        sprintf(sv.configstrings[CS_AIRACCEL],
                "%d", sv_airaccelerate->integer);
    } else {
        strcpy(sv.configstrings[CS_AIRACCEL], "0");
    }

#if !USE_CLIENT
    resolve_masters();
#endif

    override_entity_string(server);

    sv.cm = *cm;
    sprintf(sv.configstrings[CS_MAPCHECKSUM], "%d", (int)cm->cache->checksum);

    // set inline model names
    Q_concat(sv.configstrings[CS_MODELS + 1], MAX_QPATH, "maps/", server, ".bsp", NULL);
    for (i = 1; i < cm->cache->nummodels; i++) {
        sprintf(sv.configstrings[CS_MODELS + 1 + i], "*%d", i);
    }

    //
    // clear physics interaction links
    //
    SV_ClearWorld();

    //
    // spawn the rest of the entities on the map
    //

    // precache and static commands can be issued during
    // map initialization
    sv.state = ss_loading;

    X86_PUSH_FPCW;
    X86_SINGLE_FPCW;

    // load and spawn all other entities
    ge->SpawnEntities(sv.name, sv.entitystring ?
                      sv.entitystring : cm->cache->entitystring, spawnpoint);

    // run two frames to allow everything to settle
    ge->RunFrame(); sv.framenum++;
    ge->RunFrame(); sv.framenum++;

    X86_POP_FPCW;

    // make sure maxclients string is correct
    sprintf(sv.configstrings[CS_MAXCLIENTS], "%d", sv_maxclients->integer);

    // all precaches are complete
    sv.state = ss_game;

    // respawn dummy MVD client, set base states, etc
    SV_MvdMapChanged();

    // set serverinfo variable
    SV_InfoSet("mapname", sv.name);
    SV_InfoSet("port", net_port->string);

    Cvar_SetInteger(sv_running, ss_game, FROM_CODE);
    Cvar_Set("sv_paused", "0");
    Cvar_Set("timedemo", "0");

    EXEC_TRIGGER(sv_changemapcmd);

#if USE_SYSCON
    SV_SetConsoleTitle();
#endif

    SV_BroadcastCommand("reconnect\n");

    Com_Printf("-------------------------------------\n");
}

/*
==============
SV_InitGame

A brand new game has been started.
If mvd_spawn is non-zero, load the built-in MVD game module.
==============
*/
void SV_InitGame(unsigned mvd_spawn)
{
    int     i, entnum;
    edict_t *ent;
    client_t *client;

    if (svs.initialized) {
        // cause any connected clients to reconnect
        SV_Shutdown("Server restarted\n", ERR_RECONNECT | mvd_spawn);
    } else {
        // make sure the client is down
        CL_Disconnect(ERR_RECONNECT);
        SCR_BeginLoadingPlaque();

        CM_FreeMap(&sv.cm);
        SV_FreeFile(sv.entitystring);
        memset(&sv, 0, sizeof(sv));

#if USE_FPS
        // set up default frametime for main loop
        sv.frametime = BASE_FRAMETIME;
#endif
    }

    // get any latched variable changes (maxclients, etc)
    Cvar_GetLatchedVars();

#if !USE_CLIENT
    Cvar_Reset(sv_recycle);
#endif

    if (mvd_spawn) {
        Cvar_Set("deathmatch", "1");
        Cvar_Set("coop", "0");
    } else {
        if (Cvar_VariableInteger("coop") &&
            Cvar_VariableInteger("deathmatch")) {
            Com_Printf("Deathmatch and Coop both set, disabling Coop\n");
            Cvar_Set("coop", "0");
        }

        // dedicated servers can't be single player and are usually DM
        // so unless they explicity set coop, force it to deathmatch
        if (COM_DEDICATED) {
            if (!Cvar_VariableInteger("coop"))
                Cvar_Set("deathmatch", "1");
        }
    }

    // init clients
    if (Cvar_VariableInteger("deathmatch")) {
        if (sv_maxclients->integer <= 1) {
            Cvar_SetInteger(sv_maxclients, 8, FROM_CODE);
        } else if (sv_maxclients->integer > CLIENTNUM_RESERVED) {
            Cvar_SetInteger(sv_maxclients, CLIENTNUM_RESERVED, FROM_CODE);
        }
    } else if (Cvar_VariableInteger("coop")) {
        if (sv_maxclients->integer <= 1 || sv_maxclients->integer > 4)
            Cvar_Set("maxclients", "4");
    } else {    // non-deathmatch, non-coop is one player
        Cvar_FullSet("maxclients", "1", CVAR_SERVERINFO | CVAR_LATCH, FROM_CODE);
    }

    // enable networking
    if (sv_maxclients->integer > 1) {
        NET_Config(NET_SERVER);
    }

    svs.client_pool = SV_Mallocz(sizeof(client_t) * sv_maxclients->integer);

    svs.num_entities = sv_maxclients->integer * UPDATE_BACKUP * MAX_PACKET_ENTITIES;
    svs.entities = SV_Mallocz(sizeof(entity_packed_t) * svs.num_entities);

    // initialize MVD server
    if (!mvd_spawn) {
        SV_MvdInit();
    }

    Cvar_ClampInteger(sv_reserved_slots, 0, sv_maxclients->integer - 1);

#if USE_ZLIB
    svs.z.zalloc = SV_zalloc;
    svs.z.zfree = SV_zfree;
    if (deflateInit2(&svs.z, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     -MAX_WBITS, 9, Z_DEFAULT_STRATEGY) != Z_OK) {
        Com_Error(ERR_FATAL, "%s: deflateInit2() failed", __func__);
    }
#endif

    // init game
#if USE_MVD_CLIENT
    if (mvd_spawn) {
        if (ge) {
            SV_ShutdownGameProgs();
        }
        ge = &mvd_ge;
        ge->Init();
    } else
#endif
        SV_InitGameProgs();

    // send heartbeat very soon
    svs.last_heartbeat = -(HEARTBEAT_SECONDS - 5) * 1000;

    for (i = 0; i < sv_maxclients->integer; i++) {
        client = svs.client_pool + i;
        entnum = i + 1;
        ent = EDICT_NUM(entnum);
        ent->s.number = entnum;
        client->edict = ent;
        client->number = i;
    }

    AC_Connect(mvd_spawn);

    svs.initialized = qtrue;
}

