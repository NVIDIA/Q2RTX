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

static void set_frame_time(void)
{
#if USE_FPS
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
#endif
}

static void resolve_masters(void)
{
#if !USE_CLIENT
    time_t now = time(NULL);

    for (int i = 0; i < MAX_MASTERS; i++) {
        master_t *m = &sv_masters[i];
        if (!m->name) {
            break;
        }
        if (now < m->last_resolved) {
            m->last_resolved = now;
            continue;
        }
        // re-resolve valid address after one day,
        // resolve invalid address after three hours
        int hours = m->adr.type ? 24 : 3;
        if (now - m->last_resolved < hours * 3600) {
            continue;
        }
        if (NET_StringToAdr(m->name, &m->adr, PORT_MASTER)) {
            Com_DPrintf("Master server at %s.\n", NET_AdrToString(&m->adr));
        } else {
            Com_WPrintf("Couldn't resolve master: %s\n", m->name);
            memset(&m->adr, 0, sizeof(m->adr));
        }
        m->last_resolved = now = time(NULL);
    }
#endif
}

// optionally load the entity string from external source
static void override_entity_string(const char *server)
{
    char *path = map_override_path->string;
    char buffer[MAX_QPATH], *str;
    int ret;

    if (!*path) {
        return;
    }

    if (Q_concat(buffer, sizeof(buffer), path, server, ".ent") >= sizeof(buffer)) {
        ret = Q_ERR(ENAMETOOLONG);
        goto fail1;
    }

    ret = SV_LoadFile(buffer, (void **)&str);
    if (!str) {
        if (ret == Q_ERR(ENOENT)) {
            return;
        }
        goto fail1;
    }

    if (ret > MAX_MAP_ENTSTRING) {
        ret = Q_ERR(EFBIG);
        goto fail2;
    }

    Com_Printf("Loaded entity string from %s\n", buffer);
    sv.entitystring = str;
    return;

fail2:
    SV_FreeFile(str);
fail1:
    Com_EPrintf("Couldn't load entity string from %s: %s\n",
                buffer, Q_ErrorString(ret));
}


/*
================
SV_SpawnServer

Change the server to a new map, taking all connected
clients along with it.
================
*/
void SV_SpawnServer(mapcmd_t *cmd)
{
    int         i;
    client_t    *client;
    char        *entitystring;

    SCR_BeginLoadingPlaque();           // for local system

    Com_Printf("------- Server Initialization -------\n");
    Com_Printf("SpawnServer: %s\n", cmd->server);

	static bool warning_printed = false;
	if (dedicated->integer && !SV_NoSaveGames() && !warning_printed)
	{
		Com_Printf("\nWARNING: Dedicated coop servers save game state into the same place as single player game by default (currently '%s/%s'). "
			"To override that, set the 'sv_savedir' console variable. To host multiple dedicated coop servers on one machine, set that cvar "
			"to different values on different instances of the server.\n\n", fs_gamedir, Cvar_WeakGet("sv_savedir")->string);

		warning_printed = true;
	}

    // everyone needs to reconnect
    FOR_EACH_CLIENT(client) {
        SV_ClientReset(client);
    }

    SV_BroadcastCommand("changing map=%s\n", cmd->server);
    SV_SendClientMessages();
    SV_SendAsyncPackets();

    // free current level
    CM_FreeMap(&sv.cm);
    SV_FreeFile(sv.entitystring);

    // wipe the entire per-level structure
    memset(&sv, 0, sizeof(sv));
    sv.spawncount = Q_rand() & 0x7fffffff;

    // set legacy spawncounts
    FOR_EACH_CLIENT(client) {
        client->spawncount = sv.spawncount;
    }

    // reset entity counter
    svs.next_entity = 0;

    // set framerate parameters
    set_frame_time();

    // save name for levels that don't set message
    Q_strlcpy(sv.configstrings[CS_NAME], cmd->server, MAX_QPATH);
    Q_strlcpy(sv.name, cmd->server, sizeof(sv.name));
    Q_strlcpy(sv.mapcmd, cmd->buffer, sizeof(sv.mapcmd));

    if (Cvar_VariableInteger("deathmatch")) {
        sprintf(sv.configstrings[CS_AIRACCEL], "%d", sv_airaccelerate->integer);
    } else {
        strcpy(sv.configstrings[CS_AIRACCEL], "0");
    }

    resolve_masters();

    if (cmd->state == ss_game) {
        override_entity_string(cmd->server);

        sv.cm = cmd->cm;
        sprintf(sv.configstrings[CS_MAPCHECKSUM], "%d", (int)sv.cm.cache->checksum);

        // set inline model names
        Q_concat(sv.configstrings[CS_MODELS + 1], MAX_QPATH, "maps/", cmd->server, ".bsp");
        for (i = 1; i < sv.cm.cache->nummodels; i++) {
            sprintf(sv.configstrings[CS_MODELS + 1 + i], "*%d", i);
        }

        entitystring = sv.entitystring ? sv.entitystring : sv.cm.cache->entitystring;
    } else {
        // no real map
        strcpy(sv.configstrings[CS_MAPCHECKSUM], "0");
        entitystring = "";
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

    // load and spawn all other entities
    ge->SpawnEntities(sv.name, entitystring, cmd->spawnpoint);

    // run two frames to allow everything to settle
    ge->RunFrame(); sv.framenum++;
    ge->RunFrame(); sv.framenum++;

    // make sure maxclients string is correct
    sprintf(sv.configstrings[CS_MAXCLIENTS], "%d", sv_maxclients->integer);

    // check for a savegame
    SV_CheckForSavegame(cmd);

    // all precaches are complete
    sv.state = cmd->state;

    // respawn dummy MVD client, set base states, etc
    SV_MvdMapChanged();

    // set serverinfo variable
    SV_InfoSet("mapname", sv.name);
    SV_InfoSet("port", net_port->string);

    Cvar_SetInteger(sv_running, sv.state, FROM_CODE);
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
SV_ParseMapCmd

Parses mapcmd into more C friendly form.
Loads and fully validates the map to make sure server doesn't get killed.
==============
*/
bool SV_ParseMapCmd(mapcmd_t *cmd)
{
    char        expanded[MAX_QPATH];
    char        *s, *ch;
    int         ret = Q_ERR(ENAMETOOLONG);

    s = cmd->buffer;

    // skip the end-of-unit flag if necessary
    if (*s == '*') {
        s++;
        cmd->endofunit = true;
    }

    // if there is a + in the map, set nextserver to the remainder.
    ch = strchr(s, '+');
    if (ch)
    {
        *ch = 0;
        Cvar_Set("nextserver", va("gamemap \"%s\"", ch + 1));
    }
    else
        Cvar_Set("nextserver", "");

    cmd->server = s;

    // if there is a $, use the remainder as a spawnpoint
    ch = strchr(s, '$');
    if (ch) {
        *ch = 0;
        cmd->spawnpoint = ch + 1;
    } else {
        cmd->spawnpoint = cmd->buffer + strlen(cmd->buffer);
    }

    // now expand and try to load the map
    if (!COM_CompareExtension(s, ".pcx")) {
        if (Q_concat(expanded, sizeof(expanded), "pics/", s) < sizeof(expanded)) {
            ret = FS_LoadFile(expanded, NULL);
        }
        cmd->state = ss_pic;
    } 
    else if (!COM_CompareExtension(s, ".cin")) {
        ret = Q_ERR_SUCCESS;
        cmd->state = ss_cinematic;
    }
    else {
        if (Q_concat(expanded, sizeof(expanded), "maps/", s, ".bsp") < sizeof(expanded)) {
            ret = CM_LoadMap(&cmd->cm, expanded);
        }
        cmd->state = ss_game;
    }

    if (ret < 0) {
        Com_Printf("Couldn't load %s: %s\n", expanded, Q_ErrorString(ret));
        return false;
    }

    return true;
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
    Q_assert(deflateInit2(&svs.z, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
             -MAX_WBITS, 9, Z_DEFAULT_STRATEGY) == Z_OK);
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
    svs.heartbeat_index = 0;

    for (i = 0; i < sv_maxclients->integer; i++) {
        client = svs.client_pool + i;
        entnum = i + 1;
        ent = EDICT_NUM(entnum);
        ent->s.number = entnum;
        client->edict = ent;
        client->number = i;
    }

    AC_Connect(mvd_spawn);

    svs.initialized = true;
}

