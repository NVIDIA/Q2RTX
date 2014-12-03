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

#define SAVE_MAGIC1     (('2'<<24)|('V'<<16)|('S'<<8)|'S')  // "SSV2"
#define SAVE_MAGIC2     (('2'<<24)|('V'<<16)|('A'<<8)|'S')  // "SAV2"
#define SAVE_VERSION    1

#define SAVE_CURRENT    ".current"
#define SAVE_AUTO       "save0"

static int write_server_file(qboolean autosave)
{
    char        name[MAX_OSPATH];
    cvar_t      *var;
    size_t      len;
    qerror_t    ret;
    uint64_t    timestamp;

    // write magic
    MSG_WriteLong(SAVE_MAGIC1);
    MSG_WriteLong(SAVE_VERSION);

    timestamp = (uint64_t)time(NULL);

    // write the comment field
    MSG_WriteLong(timestamp & 0xffffffff);
    MSG_WriteLong(timestamp >> 32);
    MSG_WriteByte(autosave);
    MSG_WriteString(sv.configstrings[CS_NAME]);

    // write the mapcmd
    MSG_WriteString(sv.mapcmd);

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
    ret = FS_WriteFile("save/" SAVE_CURRENT "/server.ssv",
                       msg_write.data, msg_write.cursize);

    SZ_Clear(&msg_write);

    if (ret < 0)
        return -1;

    // write game state
    len = Q_snprintf(name, MAX_OSPATH,
                     "%s/save/" SAVE_CURRENT "/game.ssv", fs_gamedir);
    if (len >= MAX_OSPATH)
        return -1;

    ge->WriteGame(name, autosave);
    return 0;
}

static int write_level_file(void)
{
    char        name[MAX_OSPATH];
    int         i;
    char        *s;
    size_t      len;
    byte        portalbits[MAX_MAP_PORTAL_BYTES];
    qerror_t    ret;

    // write magic
    MSG_WriteLong(SAVE_MAGIC2);
    MSG_WriteLong(SAVE_VERSION);

    // write configstrings
    for (i = 0; i < MAX_CONFIGSTRINGS; i++) {
        s = sv.configstrings[i];
        if (!s[0])
            continue;

        len = strlen(s);
        if (len > MAX_QPATH)
            len = MAX_QPATH;

        MSG_WriteShort(i);
        MSG_WriteData(s, len);
        MSG_WriteByte(0);
    }
    MSG_WriteShort(MAX_CONFIGSTRINGS);

    len = CM_WritePortalBits(&sv.cm, portalbits);
    MSG_WriteByte(len);
    MSG_WriteData(portalbits, len);

    len = Q_snprintf(name, MAX_QPATH, "save/" SAVE_CURRENT "/%s.sv2", sv.name);
    if (len >= MAX_QPATH)
        ret = -1;
    else
        ret = FS_WriteFile(name, msg_write.data, msg_write.cursize);

    SZ_Clear(&msg_write);

    if (ret < 0)
        return -1;

    // write game level
    len = Q_snprintf(name, MAX_OSPATH,
                     "%s/save/" SAVE_CURRENT "/%s.sav", fs_gamedir, sv.name);
    if (len >= MAX_OSPATH)
        return -1;

    ge->WriteLevel(name);
    return 0;
}

static int copy_file(const char *src, const char *dst, const char *name)
{
    char    path[MAX_OSPATH];
    byte    buf[0x10000];
    FILE    *ifp, *ofp;
    size_t  len, res;
    int     ret = -1;

    len = Q_snprintf(path, MAX_OSPATH, "%s/save/%s/%s", fs_gamedir, src, name);
    if (len >= MAX_OSPATH)
        goto fail0;

    ifp = fopen(path, "rb");
    if (!ifp)
        goto fail0;

    len = Q_snprintf(path, MAX_OSPATH, "%s/save/%s/%s", fs_gamedir, dst, name);
    if (len >= MAX_OSPATH)
        goto fail1;

    if (FS_CreatePath(path))
        goto fail1;

    ofp = fopen(path, "wb");
    if (!ofp)
        goto fail1;

    do {
        len = fread(buf, 1, sizeof(buf), ifp);
        res = fwrite(buf, 1, len, ofp);
    } while (len == sizeof(buf) && res == len);

    if (ferror(ifp))
        goto fail2;

    if (ferror(ofp))
        goto fail2;

    ret = 0;
fail2:
    fclose(ofp);
fail1:
    fclose(ifp);
fail0:
    return ret;
}

static int remove_file(const char *dir, const char *name)
{
    char path[MAX_OSPATH];
    size_t len;

    len = Q_snprintf(path, MAX_OSPATH, "%s/save/%s/%s", fs_gamedir, dir, name);
    if (len >= MAX_OSPATH)
        return -1;

    return remove(path);
}

static void **list_save_dir(const char *dir, int *count)
{
    return FS_ListFiles(va("save/%s", dir), ".ssv;.sav;.sv2",
        FS_TYPE_REAL | FS_PATH_GAME, count);
}

static int wipe_save_dir(const char *dir)
{
    void **list;
    int i, count, ret = 0;

    if ((list = list_save_dir(dir, &count)) == NULL)
        return 0;

    for (i = 0; i < count; i++)
        ret |= remove_file(dir, list[i]);

    FS_FreeList(list);
    return ret;
}

static int copy_save_dir(const char *src, const char *dst)
{
    void **list;
    int i, count, ret = 0;

    if ((list = list_save_dir(src, &count)) == NULL)
        return -1;

    for (i = 0; i < count; i++)
        ret |= copy_file(src, dst, list[i]);

    FS_FreeList(list);
    return ret;
}

static int read_binary_file(const char *name)
{
    qhandle_t f;
    size_t len;

    len = FS_FOpenFile(name, &f, FS_MODE_READ | FS_TYPE_REAL | FS_PATH_GAME);
    if (!f)
        return -1;

    if (len > MAX_MSGLEN)
        goto fail;

    if (FS_Read(msg_read_buffer, len, f) != len)
        goto fail;

    SZ_Init(&msg_read, msg_read_buffer, len);
    msg_read.cursize = len;

    FS_FCloseFile(f);
    return 0;

fail:
    FS_FCloseFile(f);
    return -1;
}

char *SV_GetSaveInfo(const char *dir)
{
    char        name[MAX_QPATH], date[MAX_QPATH];
    size_t      len;
    uint64_t    timestamp;
    int         autosave, year;
    time_t      t;
    struct tm   *tm;

    len = Q_snprintf(name, MAX_QPATH, "save/%s/server.ssv", dir);
    if (len >= MAX_QPATH)
        return NULL;

    if (read_binary_file(name))
        return NULL;

    if (MSG_ReadLong() != SAVE_MAGIC1)
        return NULL;

    if (MSG_ReadLong() != SAVE_VERSION)
        return NULL;

    // read the comment field
    timestamp = (uint64_t)MSG_ReadLong();
    timestamp |= (uint64_t)MSG_ReadLong() << 32;
    autosave = MSG_ReadByte();
    MSG_ReadString(name, sizeof(name));

    if (autosave)
        return Z_CopyString(va("ENTERING %s", name));

    // get current year
    t = time(NULL);
    tm = localtime(&t);
    year = tm ? tm->tm_year : -1;

    // format savegame date
    t = (time_t)timestamp;
    if ((tm = localtime(&t)) != NULL) {
        if (tm->tm_year == year)
            strftime(date, sizeof(date), "%b %d %H:%M", tm);
        else
            strftime(date, sizeof(date), "%b %d  %Y", tm);
    } else {
        strcpy(date, "???");
    }

    return Z_CopyString(va("%s %s", date, name));
}

static void abort_func(void *arg)
{
    CM_FreeMap(arg);
}

static int read_server_file(void)
{
    char        name[MAX_OSPATH], string[MAX_STRING_CHARS];
    mapcmd_t    cmd;
    size_t      len;

    // errors like missing file, bad version, etc are
    // non-fatal and just return to the command handler
    if (read_binary_file("save/" SAVE_CURRENT "/server.ssv"))
        return -1;

    if (MSG_ReadLong() != SAVE_MAGIC1)
        return -1;

    if (MSG_ReadLong() != SAVE_VERSION)
        return -1;

    memset(&cmd, 0, sizeof(cmd));

    // read the comment field
    MSG_ReadLong();
    MSG_ReadLong();
    if (MSG_ReadByte())
        cmd.loadgame = 2;   // autosave
    else
        cmd.loadgame = 1;   // regular savegame
    MSG_ReadString(NULL, 0);

    // read the mapcmd
    len = MSG_ReadString(cmd.buffer, sizeof(cmd.buffer));
    if (len >= sizeof(cmd.buffer))
        return -1;

    // now try to load the map
    if (!SV_ParseMapCmd(&cmd))
        return -1;

    // save pending CM to be freed later if ERR_DROP is thrown
    Com_AbortFunc(abort_func, &cmd.cm);

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
        if (len >= MAX_QPATH)
            Com_Error(ERR_DROP, "Savegame cvar name too long");

        len = MSG_ReadString(string, sizeof(string));
        if (len >= sizeof(string))
            Com_Error(ERR_DROP, "Savegame cvar value too long");

        Cvar_UserSet(name, string);
    }

    // start a new game fresh with new cvars
    SV_InitGame(MVD_SPAWN_DISABLED);

    // error out immediately if game doesn't support safe savegames
    if (!(g_features->integer & GMF_ENHANCED_SAVEGAMES))
        Com_Error(ERR_DROP, "Game does not support enhanced savegames");

    // read game state
    len = Q_snprintf(name, MAX_OSPATH,
                     "%s/save/" SAVE_CURRENT "/game.ssv", fs_gamedir);
    if (len >= MAX_OSPATH)
        Com_Error(ERR_DROP, "Savegame path too long");

    ge->ReadGame(name);

    // clear pending CM
    Com_AbortFunc(NULL, NULL);

    // go to the map
    SV_SpawnServer(&cmd);
    return 0;
}

static int read_level_file(void)
{
    char    name[MAX_OSPATH];
    size_t  len, maxlen;
    int     index;

    len = Q_snprintf(name, MAX_QPATH, "save/" SAVE_CURRENT "/%s.sv2", sv.name);
    if (len >= MAX_QPATH)
        return -1;

    if (read_binary_file(name))
        return -1;

    if (MSG_ReadLong() != SAVE_MAGIC2)
        return -1;

    if (MSG_ReadLong() != SAVE_VERSION)
        return -1;

    // any error will drop from this point

    // the rest can't underflow
    msg_read.allowunderflow = qfalse;

    // read all configstrings
    while (1) {
        index = MSG_ReadShort();
        if (index == MAX_CONFIGSTRINGS)
            break;

        if (index < 0 || index > MAX_CONFIGSTRINGS)
            Com_Error(ERR_DROP, "Bad savegame configstring index");

        maxlen = CS_SIZE(index);
        len = MSG_ReadString(sv.configstrings[index], maxlen);
        if (len >= maxlen)
            Com_Error(ERR_DROP, "Savegame configstring too long");
    }

    len = MSG_ReadByte();
    if (len > MAX_MAP_PORTAL_BYTES)
        Com_Error(ERR_DROP, "Savegame portalbits too long");

    SV_ClearWorld();

    CM_SetPortalStates(&sv.cm, MSG_ReadData(len), len);

    // read game level
    len = Q_snprintf(name, MAX_OSPATH, "%s/save/" SAVE_CURRENT "/%s.sav",
                     fs_gamedir, sv.name);
    if (len >= MAX_OSPATH)
        Com_Error(ERR_DROP, "Savegame path too long");

    ge->ReadLevel(name);
    return 0;
}

static int no_save_games(void)
{
    if (dedicated->integer)
        return 1;

    if (!(g_features->integer & GMF_ENHANCED_SAVEGAMES))
        return 1;

    if (Cvar_VariableInteger("deathmatch"))
        return 1;

    return 0;
}

void SV_AutoSaveBegin(mapcmd_t *cmd)
{
    byte        bitmap[MAX_CLIENTS / CHAR_BIT];
    edict_t     *ent;
    int         i;

    // check for clearing the current savegame
    if (cmd->endofunit) {
        wipe_save_dir(SAVE_CURRENT);
        return;
    }

    if (sv.state != ss_game)
        return;

    if (no_save_games())
        return;

    memset(bitmap, 0, sizeof(bitmap));

    // clear all the client inuse flags before saving so that
    // when the level is re-entered, the clients will spawn
    // at spawn points instead of occupying body shells
    for (i = 0; i < sv_maxclients->integer; i++) {
        ent = EDICT_NUM(i + 1);
        if (ent->inuse) {
            Q_SetBit(bitmap, i);
            ent->inuse = qfalse;
        }
    }

    // save the map just exited
    if (write_level_file())
        Com_EPrintf("Couldn't write level file.\n");

    // we must restore these for clients to transfer over correctly
    for (i = 0; i < sv_maxclients->integer; i++) {
        ent = EDICT_NUM(i + 1);
        ent->inuse = Q_IsBitSet(bitmap, i);
    }
}

void SV_AutoSaveEnd(void)
{
    if (sv.state != ss_game)
        return;

    if (no_save_games())
        return;

    // save server state
    if (write_server_file(qtrue)) {
        Com_EPrintf("Couldn't write server file.\n");
        return;
    }

    // clear whatever savegames are there
    if (wipe_save_dir(SAVE_AUTO)) {
        Com_EPrintf("Couldn't wipe '%s' directory.\n", SAVE_AUTO);
        return;
    }

    // copy off the level to the autosave slot
    if (copy_save_dir(SAVE_CURRENT, SAVE_AUTO)) {
        Com_EPrintf("Couldn't write '%s' directory.\n", SAVE_AUTO);
        return;
    }
}

void SV_CheckForSavegame(mapcmd_t *cmd)
{
    if (no_save_games())
        return;

    if (read_level_file()) {
        // only warn when loading a regular savegame. autosave without level
        // file is ok and simply starts the map from the beginning.
        if (cmd->loadgame == 1)
            Com_EPrintf("Couldn't read level file.\n");
        return;
    }

    if (cmd->loadgame) {
        // called from SV_Loadgame_f
        ge->RunFrame();
        ge->RunFrame();
    } else {
        int i;

        // coming back to a level after being in a different
        // level, so run it for ten seconds
        for (i = 0; i < 100; i++)
            ge->RunFrame();
    }
}

static void SV_Savegame_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        FS_File_g("save", NULL, FS_SEARCH_DIRSONLY | FS_TYPE_REAL | FS_PATH_GAME, ctx);
    }
}

static void SV_Loadgame_f(void)
{
    char *dir;

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s <directory>\n", Cmd_Argv(0));
        return;
    }

    if (dedicated->integer) {
        Com_Printf("Savegames are for listen servers only.\n");
        return;
    }

    dir = Cmd_Argv(1);
    if (!COM_IsPath(dir)) {
        Com_Printf("Bad savedir.\n");
        return;
    }

    // make sure the server files exist
    if (!FS_FileExistsEx(va("save/%s/server.ssv", dir), FS_TYPE_REAL | FS_PATH_GAME) ||
        !FS_FileExistsEx(va("save/%s/game.ssv", dir), FS_TYPE_REAL | FS_PATH_GAME)) {
        Com_Printf ("No such savegame: %s\n", dir);
        return;
    }

    // clear whatever savegames are there
    if (wipe_save_dir(SAVE_CURRENT)) {
        Com_Printf("Couldn't wipe '%s' directory.\n", SAVE_CURRENT);
        return;
    }

    // copy it off
    if (copy_save_dir(dir, SAVE_CURRENT)) {
        Com_Printf("Couldn't read '%s' directory.\n", dir);
        return;
    }

    // read server state
    if (read_server_file()) {
        Com_Printf("Couldn't read server file.\n");
        return;
    }
}

static void SV_Savegame_f(void)
{
    char *dir;

    if (sv.state != ss_game) {
        Com_Printf("You must be in a game to save.\n");
        return;
    }

    if (dedicated->integer) {
        Com_Printf("Savegames are for listen servers only.\n");
        return;
    }

    // don't bother saving if we can't read them back!
    if (!(g_features->integer & GMF_ENHANCED_SAVEGAMES)) {
        Com_Printf("Game does not support enhanced savegames.\n");
        return;
    }

    if (Cvar_VariableInteger("deathmatch")) {
        Com_Printf("Can't savegame in a deathmatch.\n");
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
    if (write_level_file()) {
        Com_Printf("Couldn't write level file.\n");
        return;
    }

    // save server state
    if (write_server_file(qfalse)) {
        Com_Printf("Couldn't write server file.\n");
        return;
    }

    // clear whatever savegames are there
    if (wipe_save_dir(dir)) {
        Com_Printf("Couldn't wipe '%s' directory.\n", dir);
        return;
    }

    // copy it off
    if (copy_save_dir(SAVE_CURRENT, dir)) {
        Com_Printf("Couldn't write '%s' directory.\n", dir);
        return;
    }

    Com_Printf("Game saved.\n");
}

static const cmdreg_t c_savegames[] = {
    { "save", SV_Savegame_f, SV_Savegame_c },
    { "load", SV_Loadgame_f, SV_Savegame_c },
    { NULL }
};

void SV_RegisterSavegames(void)
{
    Cmd_Register(c_savegames);
}
