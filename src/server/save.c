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

#define SAVE_MAGIC1     MakeLittleLong('S','S','V','2')
#define SAVE_MAGIC2     MakeLittleLong('S','A','V','2')
#define SAVE_VERSION    1

#define SAVE_CURRENT    ".current"
#define SAVE_AUTO       "save0"

cvar_t *sv_savedir = NULL;
/* Don't require GMF_ENHANCED_SAVEGAMES feature for savegame support.
 * Savegame logic may be less "safe", but in practice, this usually works fine.
 * Still, allow it as an option for cautious people. */
cvar_t *sv_force_enhanced_savegames = NULL;
static cvar_t   *sv_noreload;

static int write_binary_file(char const* name, void const* data, size_t size)
{
    char namecopy[MAX_OSPATH];
    if (Q_strlcpy(namecopy, name, MAX_OSPATH) >= MAX_OSPATH)
        return -1;

    if (FS_CreatePath(namecopy) < 0)
        return -1;

    FILE* fp = fopen(name, "wb");
    if (!fp)
        return -1;

    if (fwrite(data, 1, size, fp) < size)
    {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

static int write_server_file(bool autosave)
{
    char        name[MAX_OSPATH];
    cvar_t      *var;
    int         ret;

    // write magic
    MSG_WriteLong(SAVE_MAGIC1);
    MSG_WriteLong(SAVE_VERSION);

    // write the comment field
    MSG_WriteLong64(time(NULL));
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
	if (Q_snprintf(name, MAX_OSPATH, "%s/%s/%s/server.ssv", fs_gamedir, sv_savedir->string, SAVE_CURRENT) >= MAX_OSPATH)
        return -1;

    ret = write_binary_file(name, msg_write.data, msg_write.cursize);

    SZ_Clear(&msg_write);

    if (ret < 0)
        return -1;

    // write game state
    if (Q_snprintf(name, MAX_OSPATH, "%s/%s/%s/game.ssv", fs_gamedir, sv_savedir->string, SAVE_CURRENT) >= MAX_OSPATH)
        return -1;

    ge->WriteGame(name, autosave);
    return 0;
}

static int write_level_file(void)
{
    char        name[MAX_OSPATH];
    int         i, ret;
    char        *s;
    size_t      len;
    byte        portalbits[MAX_MAP_PORTAL_BYTES];

    // write magic
    MSG_WriteLong(SAVE_MAGIC2);
    MSG_WriteLong(SAVE_VERSION);

    // write configstrings
    for (i = 0; i < svs.csr.end; i++) {
        s = sv.configstrings[i];
        if (!s[0])
            continue;

        len = Q_strnlen(s, MAX_QPATH);
        MSG_WriteShort(i);
        MSG_WriteData(s, len);
        MSG_WriteByte(0);
    }
    MSG_WriteShort(i);

    len = CM_WritePortalBits(&sv.cm, portalbits);
    MSG_WriteByte(len);
    MSG_WriteData(portalbits, len);

    if (Q_snprintf(name, MAX_OSPATH, "%s/%s/%s/%s.sv2", fs_gamedir, sv_savedir->string, SAVE_CURRENT, sv.name) >= MAX_OSPATH)
        ret = -1;
    else
        ret = write_binary_file(name, msg_write.data, msg_write.cursize);

    SZ_Clear(&msg_write);

    if (ret < 0)
        return -1;

    // write game level
    if (Q_snprintf(name, MAX_OSPATH, "%s/%s/%s/%s.sav", fs_gamedir, sv_savedir->string, SAVE_CURRENT, sv.name) >= MAX_OSPATH)
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

    if (Q_snprintf(path, MAX_OSPATH, "%s/%s/%s/%s", fs_gamedir, sv_savedir->string, src, name) >= MAX_OSPATH)
        goto fail0;

    ifp = fopen(path, "rb");
    if (!ifp)
        goto fail0;

    if (Q_snprintf(path, MAX_OSPATH, "%s/%s/%s/%s", fs_gamedir, sv_savedir->string, dst, name) >= MAX_OSPATH)
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
    ret |= fclose(ofp);
fail1:
    ret |= fclose(ifp);
fail0:
    return ret;
}

static int remove_file(const char *dir, const char *name)
{
    char path[MAX_OSPATH];

    if (Q_snprintf(path, MAX_OSPATH, "%s/%s/%s/%s", fs_gamedir, sv_savedir->string, dir, name) >= MAX_OSPATH)
        return -1;

    return remove(path);
}

static void **list_save_dir(const char *dir, int *count)
{
    char path[MAX_OSPATH];
    listfiles_t list;

    *count = 0;
    
    if (Q_snprintf(path, MAX_OSPATH, "%s/%s/%s", fs_gamedir, sv_savedir->string, dir) >= MAX_OSPATH)
        return NULL;

    memset(&list, 0, sizeof(list));
    list.filter = ".ssv;.sav;.sv2";
    list.flags = FS_SEARCH_RECURSIVE;
    Sys_ListFiles_r(&list, path, 0);

    list.files = FS_ReallocList(list.files, list.count + 1);
    list.files[list.count] = NULL;

    *count = list.count;
    return list.files;
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

static bool file_exists(const char* name)
{
    FILE* fp = fopen(name, "rb");
    if (!fp)
        return false;

    fclose(fp);
    return true;
}

static int read_binary_file(const char *name)
{
    FILE* fp = fopen(name, "rb");
    if (!fp)
        return -1;

    int fd = os_fileno(fp);
    if (fd == -1)
        return -1;

    Q_STATBUF st;
    os_fstat(fd, &st);
    
    int64_t len = st.st_size;

    if (len > MAX_MSGLEN)
        goto fail;

    if (fread(msg_read_buffer, 1, len, fp) != len)
        goto fail;

    SZ_Init(&msg_read, msg_read_buffer, sizeof(msg_read_buffer));
    msg_read.cursize = len;

    fclose(fp);
    return 0;

fail:
    fclose(fp);
    return -1;
}

char *SV_GetSaveInfo(const char *dir)
{
    char        name[MAX_OSPATH], date[MAX_QPATH];
    size_t      len;
    int64_t     timestamp;
    int         autosave, year;
    time_t      t;
    struct tm   *tm;

    if (Q_snprintf(name, MAX_OSPATH, "%s/%s/%s/server.ssv", fs_gamedir, sv_savedir->string, dir) >= MAX_OSPATH)
        return NULL;

    if (read_binary_file(name))
        return NULL;

    if (MSG_ReadLong() != SAVE_MAGIC1)
        return NULL;

    if (MSG_ReadLong() != SAVE_VERSION)
        return NULL;

    // read the comment field
    timestamp = MSG_ReadLong64();
    autosave = MSG_ReadByte();
    MSG_ReadString(name, sizeof(name));

    if (autosave)
        return Z_CopyString(va("ENTERING %s", name));

    // get current year
    t = time(NULL);
    tm = localtime(&t);
    year = tm ? tm->tm_year : -1;

    // format savegame date
    t = timestamp;
    len = 0;
    if ((tm = localtime(&t)) != NULL) {
        if (tm->tm_year == year)
            len = strftime(date, sizeof(date), "%b %d %H:%M", tm);
        else
            len = strftime(date, sizeof(date), "%b %d  %Y", tm);
    } else {
        len = strftime(date, sizeof(date), "%b %d  %Y", tm);
    }
    if (!len)
        strcpy(date, "???");

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

    // errors like missing file, bad version, etc are
    // non-fatal and just return to the command handler

	if (Q_snprintf(name, MAX_OSPATH, "%s/%s/%s/server.ssv", fs_gamedir, sv_savedir->string, SAVE_CURRENT) >= MAX_OSPATH)
        return -1;
        
    if (read_binary_file(name))
        return -1;

    if (MSG_ReadLong() != SAVE_MAGIC1)
        return -1;

    if (MSG_ReadLong() != SAVE_VERSION)
        return -1;

    memset(&cmd, 0, sizeof(cmd));

    // read the comment field
    MSG_ReadLong64();
    if (MSG_ReadByte())
        cmd.loadgame = 2;   // autosave
    else
        cmd.loadgame = 1;   // regular savegame
    MSG_ReadString(NULL, 0);

    // read the mapcmd
    if (MSG_ReadString(cmd.buffer, sizeof(cmd.buffer)) >= sizeof(cmd.buffer))
        return -1;

    // now try to load the map
    if (!SV_ParseMapCmd(&cmd))
        return -1;

    // save pending CM to be freed later if ERR_DROP is thrown
    Com_AbortFunc(abort_func, &cmd.cm);

    // any error will drop from this point
    SV_Shutdown("Server restarted\n", ERR_RECONNECT);

    // the rest can't underflow
    msg_read.allowunderflow = false;

    // read all CVAR_LATCH cvars
    // these will be things like coop, skill, deathmatch, etc
    while (1) {
        if (MSG_ReadString(name, MAX_QPATH) >= MAX_QPATH)
            Com_Error(ERR_DROP, "Savegame cvar name too long");
        if (!name[0])
            break;

        if (MSG_ReadString(string, sizeof(string)) >= sizeof(string))
            Com_Error(ERR_DROP, "Savegame cvar value too long");

        Cvar_UserSet(name, string);
    }

    // start a new game fresh with new cvars
    SV_InitGame(MVD_SPAWN_DISABLED);

    // error out immediately if game doesn't support safe savegames
    if (sv_force_enhanced_savegames->integer && !(g_features->integer & GMF_ENHANCED_SAVEGAMES))
        Com_Error(ERR_DROP, "Game does not support enhanced savegames");

    // read game state
    if (Q_snprintf(name, MAX_OSPATH,
                   "%s/%s/%s/game.ssv", fs_gamedir, sv_savedir->string, SAVE_CURRENT) >= MAX_OSPATH)
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

    if (Q_snprintf(name, MAX_OSPATH, "%s/%s/%s/%s.sv2",
                   fs_gamedir, sv_savedir->string, SAVE_CURRENT, sv.name) >= MAX_OSPATH)
        return -1;

    if (read_binary_file(name))
        return -1;

    if (MSG_ReadLong() != SAVE_MAGIC2)
        return -1;

    if (MSG_ReadLong() != SAVE_VERSION)
        return -1;

    // any error will drop from this point

    // the rest can't underflow
    msg_read.allowunderflow = false;

    // read all configstrings
    while (1) {
        index = MSG_ReadWord();
        if (index == svs.csr.end)
            break;

        if (index < 0 || index >= svs.csr.end)
            Com_Error(ERR_DROP, "Bad savegame configstring index");

        maxlen = CS_SIZE(&svs.csr, index);
        if (MSG_ReadString(sv.configstrings[index], maxlen) >= maxlen)
            Com_Error(ERR_DROP, "Savegame configstring too long");
    }

    SV_ClearWorld();

    len = MSG_ReadByte();
    CM_SetPortalStates(&sv.cm, MSG_ReadData(len), len);

    // read game level
    if (Q_snprintf(name, MAX_OSPATH, "%s/%s/%s/%s.sav",
                   fs_gamedir, sv_savedir->string, SAVE_CURRENT, sv.name) >= MAX_OSPATH)
        Com_Error(ERR_DROP, "Savegame path too long");

    ge->ReadLevel(name);
    return 0;
}

bool SV_NoSaveGames(void)
{
    if (sv_force_enhanced_savegames->integer && !(g_features->integer & GMF_ENHANCED_SAVEGAMES))
        return true;

    if (Cvar_VariableInteger("deathmatch"))
        return true;

    return false;
}

void SV_AutoSaveBegin(const mapcmd_t *cmd)
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

    if (SV_NoSaveGames())
        return;

    memset(bitmap, 0, sizeof(bitmap));

    // clear all the client inuse flags before saving so that
    // when the level is re-entered, the clients will spawn
    // at spawn points instead of occupying body shells
    for (i = 0; i < sv_maxclients->integer; i++) {
        ent = EDICT_NUM(i + 1);
        if (ent->inuse) {
            Q_SetBit(bitmap, i);
            ent->inuse = false;
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

	if (SV_NoSaveGames())
		return;

	// save the map just entered to include the player position (client edict shell)
	if (write_level_file())
	{
		Com_EPrintf("Couldn't write level file.\n");
		return;
	}

    // save server state
    if (write_server_file(true)) {
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

void SV_CheckForSavegame(const mapcmd_t *cmd)
{
    if (SV_NoSaveGames())
        return;
    if (sv_noreload->integer)
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

void SV_CheckForEnhancedSavegames(void)
{
    if (Cvar_VariableInteger("deathmatch"))
        return;

    if (g_features->integer & GMF_ENHANCED_SAVEGAMES) {
        Com_Printf("Game supports Q2PRO enhanced savegames.\n");
        return;
    }

    if (sv.gamedetecthack == 4) {
        Com_Printf("Game supports YQ2 enhanced savegames.\n");
        Cvar_SetInteger(g_features, g_features->integer | GMF_ENHANCED_SAVEGAMES, FROM_CODE);
        return;
    }

    Com_WPrintf("Game does not support enhanced savegames. Savegames will not work.\n");
}

static void SV_Savegame_c(genctx_t *ctx, int argnum)
{
    if (argnum != 1)
        return;

    char path[MAX_OSPATH];
    if (Q_snprintf(path, MAX_OSPATH, "%s/%s", fs_gamedir, sv_savedir->string) >= MAX_OSPATH)
        return;

    // Search for all directories in the savedir
    listfiles_t list;
    memset(&list, 0, sizeof(list));
    list.flags = FS_SEARCH_DIRSONLY;
    Sys_ListFiles_r(&list, path, 0);

    // Same logic as in FS_File_g, copying the file names into the match context
    for (int i = 0; i < list.count; i++) {
        char* s = list.files[i];
        if (ctx->count < ctx->size && !strncmp(s, ctx->partial, ctx->length)) {
            ctx->matches = Z_Realloc(ctx->matches, ALIGN(ctx->count + 1, MIN_MATCHES) * sizeof(char *));
            ctx->matches[ctx->count++] = s;
        } else {
            Z_Free(s);
        }
    }

    Z_Free(list.files);
}

static void SV_Loadgame_f(void)
{
    char *dir;

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s <directory>\n", Cmd_Argv(0));
        return;
    }

    dir = Cmd_Argv(1);
    if (!COM_IsPath(dir)) {
        Com_Printf("Bad savedir.\n");
        return;
    }

    // make sure the server files exist
    if (!file_exists(va("%s/%s/%s/server.ssv", fs_gamedir, sv_savedir->string, dir)) ||
        !file_exists(va("%s/%s/%s/game.ssv", fs_gamedir, sv_savedir->string, dir))) {
        Com_Printf("No such savegame: %s\n", dir);
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

    // don't bother saving if we can't read them back!
    if (sv_force_enhanced_savegames->integer && !(g_features->integer & GMF_ENHANCED_SAVEGAMES)) {
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
    if (write_server_file(false)) {
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
    sv_noreload = Cvar_Get("sv_noreload", "0", 0);

    Cmd_Register(c_savegames);
	sv_savedir = Cvar_Get("sv_savedir", "save", 0);
	sv_force_enhanced_savegames = Cvar_Get("sv_force_enhanced_savegames", "0", 0);
}
