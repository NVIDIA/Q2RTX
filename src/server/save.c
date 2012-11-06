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

#define SAVE_MAGIC1 (('1'<<24)|('V'<<16)|('A'<<8)|'S')
#define SAVE_MAGIC2 (('2'<<24)|('V'<<16)|('A'<<8)|'S')
#define SAVE_VERSION 1

// save to temporary dir and rename only when done
#define SAVE_CURRENT ".current"

/*
===============================================================================

SAVEGAME FILES

===============================================================================
*/

static qerror_t write_server_file(qboolean autosave)
{
    char name[MAX_OSPATH];
    cvar_t *var;
    size_t len;
    qerror_t ret;

    // write magic
    MSG_WriteLong(SAVE_MAGIC1);
    MSG_WriteLong(SAVE_VERSION);

    // write the comment field
    MSG_WriteByte(autosave);
    MSG_WriteString(sv.configstrings[CS_NAME]);

    // write the mapcmd
    MSG_WriteString(sv.name);

    // write all CVAR_LATCH cvars
    // these will be things like coop, skill, deathmatch, etc
    for (var = cvar_vars; var; var = var->next) {
        if (!(var->flags & CVAR_LATCH))
            continue;
        if (var->flags & CVAR_PRIVATE)
            continue;
        MSG_WriteString(var->name);
        MSG_WriteString(var->string);
    }
    MSG_WriteString(NULL);

    // write server state
    ret = FS_WriteFile("save/" SAVE_CURRENT "/server.state",
                       msg_write.data, msg_write.cursize);

    SZ_Clear(&msg_write);

    if (ret < 0) {
        return ret;
    }

    // write game state
    len = Q_snprintf(name, sizeof(name),
                     "%s/save/" SAVE_CURRENT "/game.state", fs_gamedir);
    if (len >= sizeof(name)) {
        return Q_ERR_NAMETOOLONG;
    }

    ge->WriteGame(name, autosave);
    return Q_ERR_SUCCESS;
}

static qerror_t write_level_file(void)
{
    char    name[MAX_OSPATH];
    int     i;
    char    *s;
    size_t  len;
    byte    portalbits[MAX_MAP_PORTAL_BYTES];
    qerror_t ret;

    // write magic
    MSG_WriteLong(SAVE_MAGIC2);
    MSG_WriteLong(SAVE_VERSION);

    // write configstrings
    for (i = 0; i < MAX_CONFIGSTRINGS; i++) {
        s = sv.configstrings[i];
        if (!s[0]) {
            continue;
        }
        len = strlen(s);
        if (len > MAX_QPATH) {
            len = MAX_QPATH;
        }

        MSG_WriteShort(i);
        MSG_WriteData(s, len);
        MSG_WriteByte(0);
    }
    MSG_WriteShort(MAX_CONFIGSTRINGS);

    len = CM_WritePortalBits(&sv.cm, portalbits);
    MSG_WriteByte(len);
    MSG_WriteData(portalbits, len);

    ret = FS_WriteFile("save/" SAVE_CURRENT "/server.level",
                       msg_write.data, msg_write.cursize);

    SZ_Clear(&msg_write);

    if (ret < 0) {
        return ret;
    }

    // write game level
    len = Q_snprintf(name, sizeof(name),
                     "%s/save/" SAVE_CURRENT "/game.level", fs_gamedir);
    if (len >= sizeof(name)) {
        return Q_ERR_NAMETOOLONG;
    }

    ge->WriteLevel(name);
    return Q_ERR_SUCCESS;
}

static qerror_t rename_file(const char *dir, const char *base, const char *suf)
{
    char from[MAX_QPATH];
    char to[MAX_QPATH];
    size_t len;

    len = Q_snprintf(from, sizeof(from), "save/%s/%s%s", SAVE_CURRENT, base, suf);
    if (len >= sizeof(from))
        return Q_ERR_NAMETOOLONG;

    len = Q_snprintf(to, sizeof(to), "save/%s/%s%s", dir, base, suf);
    if (len >= sizeof(to))
        return Q_ERR_NAMETOOLONG;

    return FS_RenameFile(from, to);
}

static qerror_t move_files(const char *dir)
{
    char name[MAX_OSPATH];
    size_t len;
    qerror_t ret;

    len = Q_snprintf(name, sizeof(name), "%s/save/%s/", fs_gamedir, dir);
    if (len >= sizeof(name))
        return Q_ERR_NAMETOOLONG;

    ret = FS_CreatePath(name);
    if (ret)
        return ret;

    ret = rename_file(dir, "game", ".level");
    if (ret)
        return ret;

    ret = rename_file(dir, "server", ".level");
    if (ret)
        return ret;

    ret = rename_file(dir, "game", ".state");
    if (ret)
        return ret;

    ret = rename_file(dir, "server", ".state");
    if (ret)
        return ret;

    return Q_ERR_SUCCESS;
}

static qerror_t read_binary_file(const char *name)
{
    qhandle_t f;
    ssize_t len, read;
    qerror_t ret;

    len = FS_FOpenFile(name, &f,
                       FS_MODE_READ | FS_TYPE_REAL | FS_PATH_GAME);
    if (!f) {
        return len;
    }

    if (len > MAX_MSGLEN) {
        ret = Q_ERR_FBIG;
        goto fail;
    }

    read = FS_Read(msg_read_buffer, len, f);
    if (read != len) {
        ret = read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
        goto fail;
    }

    SZ_Init(&msg_read, msg_read_buffer, len);
    msg_read.cursize = len;
    ret = Q_ERR_SUCCESS;

fail:
    FS_FCloseFile(f);
    return ret;
}

static qerror_t read_server_file(const char *dir)
{
    char    name[MAX_OSPATH], string[MAX_STRING_CHARS];
    char    mapcmd[MAX_QPATH];
    char    *s, *ch, *spawnpoint;
    size_t  len;
    qerror_t ret;
    cm_t    cm;

    // errors like missing file, bad version, etc are
    // non-fatal and just return to the command handler
    len = Q_snprintf(name, MAX_QPATH, "save/%s/server.state", dir);
    if (len >= MAX_QPATH) {
        return Q_ERR_NAMETOOLONG;
    }

    ret = read_binary_file(name);
    if (ret) {
        return ret;
    }

    if (MSG_ReadLong() != SAVE_MAGIC1) {
        return Q_ERR_UNKNOWN_FORMAT;
    }
    if (MSG_ReadLong() != SAVE_VERSION) {
        return Q_ERR_INVALID_FORMAT;
    }

    // read the comment field
    MSG_ReadByte();
    MSG_ReadString(NULL, 0);

    // read the mapcmd
    len = MSG_ReadString(mapcmd, sizeof(mapcmd));
    if (len >= sizeof(mapcmd)) {
        return Q_ERR_STRING_TRUNCATED;
    }

    s = mapcmd;

    // if there is a + in the map, set nextserver to the remainder
    // we go directly to nextserver as we don't support cinematics
    ch = strchr(s, '+');
    if (ch) {
        s = ch + 1;
    }

    // skip the end-of-unit flag if necessary
    if (*s == '*') {
        s++;
    }

    // if there is a $, use the remainder as a spawnpoint
    ch = strchr(s, '$');
    if (ch) {
        *ch = 0;
        spawnpoint = ch + 1;
    } else {
        spawnpoint = mapcmd + len;
    }

    // now expand and try to load the map
    len = Q_concat(name, MAX_QPATH, "maps/", s, ".bsp", NULL);
    if (len >= MAX_QPATH) {
        return Q_ERR_NAMETOOLONG;
    }

    ret = CM_LoadMap(&cm, name);
    if (ret) {
        return ret;
    }

    // any error will drop from this point
    SV_Shutdown("Server restarted\n", ERR_RECONNECT);

    // the rest can't underflow
    msg_read.allowunderflow = qfalse;

    // read all CVAR_LATCH cvars
    // these will be things like coop, skill, deathmatch, etc
    while (1) {
        len = MSG_ReadString(name, MAX_QPATH);
        if (!len)
            break;
        if (len >= MAX_QPATH) {
            ret = Q_ERR_STRING_TRUNCATED;
            goto fail;
        }

        len = MSG_ReadString(string, sizeof(string));
        if (len >= sizeof(string)) {
            ret = Q_ERR_STRING_TRUNCATED;
            goto fail;
        }

        Cvar_UserSet(name, string);
    }

    // start a new game fresh with new cvars
    SV_InitGame(MVD_SPAWN_DISABLED);

    // error out immediately if game doesn't support safe savegames
    if (!(g_features->integer & GMF_ENHANCED_SAVEGAMES)) {
        Com_Error(ERR_DROP, "Game does not support enhanced savegames");
    }

    // read game state
    len = Q_snprintf(name, sizeof(name), "%s/save/%s/game.state", fs_gamedir, dir);
    if (len >= sizeof(name)) {
        ret = Q_ERR_NAMETOOLONG;
        goto fail;
    }

    ge->ReadGame(name);

    // go to the map
    SV_SpawnServer(&cm, s, spawnpoint);
    return Q_ERR_SUCCESS;

fail:
    Com_Error(ERR_DROP, "Couldn't load %s: %s", dir, Q_ErrorString(ret));
    return Q_ERR_FAILURE;
}

static void read_level_file(const char *dir)
{
    char name[MAX_OSPATH];
    size_t len, maxlen;
    qerror_t ret;
    int index;

    len = Q_snprintf(name, MAX_QPATH, "save/%s/server.level", dir);
    if (len >= MAX_QPATH) {
        ret = Q_ERR_NAMETOOLONG;
        goto fail;
    }

    ret = read_binary_file(name);
    if (ret) {
        goto fail;
    }

    if (MSG_ReadLong() != SAVE_MAGIC2) {
        ret = Q_ERR_UNKNOWN_FORMAT;
        goto fail;
    }
    if (MSG_ReadLong() != SAVE_VERSION) {
        ret = Q_ERR_INVALID_FORMAT;
        goto fail;
    }

    // the rest can't underflow
    msg_read.allowunderflow = qfalse;

    while (1) {
        index = MSG_ReadShort();
        if (index == MAX_CONFIGSTRINGS) {
            break;
        }
        if (index < 0 || index >= MAX_CONFIGSTRINGS) {
            ret = Q_ERR_BAD_INDEX;
            goto fail;
        }

        maxlen = CS_SIZE(index);
        len = MSG_ReadString(sv.configstrings[index], maxlen);
        if (len >= maxlen) {
            ret = Q_ERR_STRING_TRUNCATED;
            goto fail;
        }
    }

    len = MSG_ReadByte();
    if (len > MAX_MAP_PORTAL_BYTES) {
        ret = Q_ERR_INVALID_FORMAT;
        goto fail;
    }

    SV_ClearWorld();

    CM_SetPortalStates(&sv.cm, MSG_ReadData(len), len);

    // read game level
    len = Q_snprintf(name, sizeof(name), "%s/save/%s/game.level", fs_gamedir, dir);
    if (len >= sizeof(name)) {
        ret = Q_ERR_NAMETOOLONG;
        goto fail;
    }

    ge->ReadLevel(name);

    ge->RunFrame();
    ge->RunFrame();
    return;

fail:
    Com_Error(ERR_DROP, "Couldn't load %s: %s", dir, Q_ErrorString(ret));
}


/*
==============
SV_Loadgame_f

==============
*/
void SV_Loadgame_f(void)
{
    char *dir;
    qerror_t ret;

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s <directory>\n", Cmd_Argv(0));
        return;
    }

    if (dedicated->integer) {
        Com_Printf("Savegames are for listen servers only\n");
        return;
    }

    dir = Cmd_Argv(1);
    if (!COM_IsPath(dir)) {
        Com_Printf("Bad savedir.\n");
        return;
    }

    ret = read_server_file(dir);
    if (ret) {
        Com_Printf("Couldn't load %s: %s\n", dir, Q_ErrorString(ret));
        return;
    }

    read_level_file(dir);
}


/*
==============
SV_Savegame_f

==============
*/
void SV_Savegame_f(void)
{
    char *dir;
    qerror_t ret;

    if (sv.state != ss_game) {
        Com_Printf("You must be in a game to save.\n");
        return;
    }

    if (dedicated->integer) {
        Com_Printf("Savegames are for listen servers only\n");
        return;
    }

    // don't bother saving if we can't read them back!
    if (!(g_features->integer & GMF_ENHANCED_SAVEGAMES)) {
        Com_Printf("Game does not support enhanced savegames\n");
        return;
    }

    if (Cvar_VariableInteger("deathmatch")) {
        Com_Printf("Can't savegame in a deathmatch\n");
        return;
    }

    if (sv_maxclients->integer == 1 && svs.client_pool[0].edict->client->ps.stats[STAT_HEALTH] <= 0) {
        Com_Printf("Can't savegame while dead!\n");
        return;
    }

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s <directory>\n", Cmd_Argv(0));
        return;
    }

    dir = Cmd_Argv(1);
    if (!COM_IsPath(dir)) {
        Com_Printf("Bad savedir.\n");
        return;
    }

    // archive current level, including all client edicts.
    // when the level is reloaded, they will be shells awaiting
    // a connecting client
    ret = write_level_file();
    if (ret)
        goto fail;

    // save server state
    ret = write_server_file(qfalse);
    if (ret)
        goto fail;

    // rename all stuff
    ret = move_files(dir);
    if (ret)
        goto fail;

    Com_Printf("Game saved.\n");
    return;

fail:
    Com_EPrintf("Couldn't write %s: %s\n", dir, Q_ErrorString(ret));
}

