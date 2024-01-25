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

#include "g_local.h"

game_locals_t   game;
level_locals_t  level;
game_import_t   gi;
game_export_t   globals;
spawn_temp_t    st;

int sm_meat_index;
int snd_fry;
int meansOfDeath;

edict_t     *g_edicts;

cvar_t  *deathmatch;
cvar_t  *coop;
cvar_t  *dmflags;
cvar_t  *skill;
cvar_t  *fraglimit;
cvar_t  *timelimit;
cvar_t  *password;
cvar_t  *spectator_password;
cvar_t  *needpass;
cvar_t  *maxclients;
cvar_t  *maxspectators;
cvar_t  *maxentities;
cvar_t  *g_select_empty;
cvar_t  *g_protocol_extensions;
cvar_t  *dedicated;
cvar_t  *nomonsters;
cvar_t  *aimfix;

cvar_t  *filterban;

cvar_t  *sv_maxvelocity;
cvar_t  *sv_gravity;

cvar_t  *sv_rollspeed;
cvar_t  *sv_rollangle;
cvar_t  *gun_x;
cvar_t  *gun_y;
cvar_t  *gun_z;

cvar_t  *run_pitch;
cvar_t  *run_roll;
cvar_t  *bob_up;
cvar_t  *bob_pitch;
cvar_t  *bob_roll;

cvar_t  *sv_cheats;

cvar_t  *flood_msgs;
cvar_t  *flood_persecond;
cvar_t  *flood_waitdelay;

cvar_t  *sv_maplist;

cvar_t  *sv_features;

cvar_t  *sv_flaregun;

void SpawnEntities(const char *mapname, const char *entities, const char *spawnpoint);
void ClientThink(edict_t *ent, usercmd_t *cmd);
qboolean ClientConnect(edict_t *ent, char *userinfo);
void ClientUserinfoChanged(edict_t *ent, char *userinfo);
void ClientDisconnect(edict_t *ent);
void ClientBegin(edict_t *ent);
void ClientCommand(edict_t *ent);
void RunEntity(edict_t *ent);
void WriteGame(const char *filename, qboolean autosave);
void ReadGame(const char *filename);
void WriteLevel(const char *filename);
void ReadLevel(const char *filename);
void InitGame(void);
void G_RunFrame(void);

//===================================================================

void ShutdownGame(void)
{
    gi.dprintf("==== ShutdownGame ====\n");

    gi.FreeTags(TAG_LEVEL);
    gi.FreeTags(TAG_GAME);
}

/*
============
InitGame

This will be called when the dll is first loaded, which
only happens when a new game is started or a save game
is loaded.
============
*/
void InitGame(void)
{
    int features = G_FEATURES;

    gi.dprintf("==== InitGame ====\n");

    Q_srand(time(NULL));

    gun_x = gi.cvar("gun_x", "0", 0);
    gun_y = gi.cvar("gun_y", "0", 0);
    gun_z = gi.cvar("gun_z", "0", 0);

    //FIXME: sv_ prefix is wrong for these
    sv_rollspeed = gi.cvar("sv_rollspeed", "200", 0);
    sv_rollangle = gi.cvar("sv_rollangle", "2", 0);
    sv_maxvelocity = gi.cvar("sv_maxvelocity", "2000", 0);
    sv_gravity = gi.cvar("sv_gravity", "800", 0);

    // noset vars
    dedicated = gi.cvar("dedicated", "0", CVAR_NOSET);

    nomonsters = gi.cvar("nomonsters", "0", 0);
	
    aimfix = gi.cvar("aimfix", "0", CVAR_ARCHIVE);

    // latched vars
    sv_cheats = gi.cvar("cheats", "0", CVAR_SERVERINFO | CVAR_LATCH);
    gi.cvar("gamename", GAMEVERSION , CVAR_SERVERINFO | CVAR_LATCH);
    gi.cvar("gamedate", __DATE__ , CVAR_SERVERINFO | CVAR_LATCH);

    maxclients = gi.cvar("maxclients", "4", CVAR_SERVERINFO | CVAR_LATCH);
    maxspectators = gi.cvar("maxspectators", "4", CVAR_SERVERINFO);
    deathmatch = gi.cvar("deathmatch", "0", CVAR_LATCH);
    coop = gi.cvar("coop", "0", CVAR_LATCH);
    skill = gi.cvar("skill", "1", CVAR_LATCH);
    maxentities = gi.cvar("maxentities", "1024", CVAR_LATCH);

    // change anytime vars
    dmflags = gi.cvar("dmflags", "0", CVAR_SERVERINFO);
    fraglimit = gi.cvar("fraglimit", "0", CVAR_SERVERINFO);
    timelimit = gi.cvar("timelimit", "0", CVAR_SERVERINFO);
    password = gi.cvar("password", "", CVAR_USERINFO);
    spectator_password = gi.cvar("spectator_password", "", CVAR_USERINFO);
    needpass = gi.cvar("needpass", "0", CVAR_SERVERINFO);
    filterban = gi.cvar("filterban", "1", 0);

    g_select_empty = gi.cvar("g_select_empty", "0", CVAR_ARCHIVE);
    g_protocol_extensions = gi.cvar("g_protocol_extensions", "0", CVAR_LATCH);

    run_pitch = gi.cvar("run_pitch", "0.002", 0);
    run_roll = gi.cvar("run_roll", "0.005", 0);
    bob_up = gi.cvar("bob_up", "0.005", 0);
    bob_pitch = gi.cvar("bob_pitch", "0.002", 0);
    bob_roll = gi.cvar("bob_roll", "0.002", 0);

    // flood control
    flood_msgs = gi.cvar("flood_msgs", "4", 0);
    flood_persecond = gi.cvar("flood_persecond", "4", 0);
    flood_waitdelay = gi.cvar("flood_waitdelay", "10", 0);

    // dm map list
    sv_maplist = gi.cvar("sv_maplist", "", 0);

    // obtain server features
    sv_features = gi.cvar("sv_features", NULL, 0);

	// flare gun switch: 
	//   0 = no flare gun
	//   1 = spawn with the flare gun
	//   2 = spawn with the flare gun and some grenades
	sv_flaregun = gi.cvar("sv_flaregun", "2", 0);

    // enable protocol extensions if supported
    if (sv_features && (int)sv_features->value & GMF_PROTOCOL_EXTENSIONS && (int)g_protocol_extensions->value) {
        features |= GMF_PROTOCOL_EXTENSIONS;
        game.csr = cs_remap_new;
    } else {
        game.csr = cs_remap_old;
    }

    // export our own features
    gi.cvar_forceset("g_features", va("%d", features));

    // items
    InitItems();

    game.helpmessage1[0] = 0;
    game.helpmessage2[0] = 0;

    // initialize all entities for this game
    game.maxentities = Q_clip(maxentities->value, (int)maxclients->value + 1, game.csr.max_edicts);
    g_edicts = gi.TagMalloc(game.maxentities * sizeof(g_edicts[0]), TAG_GAME);
    globals.edicts = g_edicts;
    globals.max_edicts = game.maxentities;

    // initialize all clients for this game
    game.maxclients = maxclients->value;
    game.clients = gi.TagMalloc(game.maxclients * sizeof(game.clients[0]), TAG_GAME);
    globals.num_edicts = game.maxclients + 1;
}

/*
=================
GetGameAPI

Returns a pointer to the structure with all entry points
and global variables
=================
*/
q_exported game_export_t *GetGameAPI(game_import_t *import)
{
    gi = *import;

    globals.apiversion = GAME_API_VERSION;
    globals.Init = InitGame;
    globals.Shutdown = ShutdownGame;
    globals.SpawnEntities = SpawnEntities;

    globals.WriteGame = WriteGame;
    globals.ReadGame = ReadGame;
    globals.WriteLevel = WriteLevel;
    globals.ReadLevel = ReadLevel;

    globals.ClientThink = ClientThink;
    globals.ClientConnect = ClientConnect;
    globals.ClientUserinfoChanged = ClientUserinfoChanged;
    globals.ClientDisconnect = ClientDisconnect;
    globals.ClientBegin = ClientBegin;
    globals.ClientCommand = ClientCommand;

    globals.RunFrame = G_RunFrame;

    globals.ServerCommand = ServerCommand;

    globals.edict_size = sizeof(edict_t);

    return &globals;
}

#ifndef GAME_HARD_LINKED
// this is only here so the functions in q_shared.c can link
void Com_LPrintf(print_type_t type, const char *fmt, ...)
{
    va_list     argptr;
    char        text[MAX_STRING_CHARS];

    if (type == PRINT_DEVELOPER) {
        return;
    }

    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    gi.dprintf("%s", text);
}

void Com_Error(error_type_t type, const char *fmt, ...)
{
    va_list     argptr;
    char        text[MAX_STRING_CHARS];

    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    gi.error("%s", text);
}
#endif

//======================================================================

/*
=================
ClientEndServerFrames
=================
*/
void ClientEndServerFrames(void)
{
    int     i;
    edict_t *ent;

    // calc the player views now that all pushing
    // and damage has been added
    for (i = 0; i < maxclients->value; i++) {
        ent = g_edicts + 1 + i;
        if (!ent->inuse || !ent->client)
            continue;
        ClientEndServerFrame(ent);
    }
}

/*
=================
CreateTargetChangeLevel

Returns the created target changelevel
=================
*/
edict_t *CreateTargetChangeLevel(char *map)
{
    edict_t *ent;

    ent = G_Spawn();
    ent->classname = "target_changelevel";
    if (map != level.nextmap)
        Q_strlcpy(level.nextmap, map, sizeof(level.nextmap));
    ent->map = level.nextmap;
    return ent;
}

/*
=================
EndDMLevel

The timelimit or fraglimit has been exceeded
=================
*/
void EndDMLevel(void)
{
    edict_t     *ent;
    char *s, *t, *f;
    static const char *seps = " ,\n\r";

    // stay on same level flag
    if ((int)dmflags->value & DF_SAME_LEVEL) {
        BeginIntermission(CreateTargetChangeLevel(level.mapname));
        return;
    }

    // see if it's in the map list
    if (*sv_maplist->string) {
        s = strdup(sv_maplist->string);
        f = NULL;
        t = strtok(s, seps);
        while (t != NULL) {
            if (Q_stricmp(t, level.mapname) == 0) {
                // it's in the list, go to the next one
                t = strtok(NULL, seps);
                if (t == NULL) { // end of list, go to first one
                    if (f == NULL) // there isn't a first one, same level
                        BeginIntermission(CreateTargetChangeLevel(level.mapname));
                    else
                        BeginIntermission(CreateTargetChangeLevel(f));
                } else
                    BeginIntermission(CreateTargetChangeLevel(t));
                free(s);
                return;
            }
            if (!f)
                f = t;
            t = strtok(NULL, seps);
        }
        free(s);
    }

    if (level.nextmap[0]) // go to a specific map
        BeginIntermission(CreateTargetChangeLevel(level.nextmap));
    else {  // search for a changelevel
        ent = G_Find(NULL, FOFS(classname), "target_changelevel");
        if (!ent) {
            // the map designer didn't include a changelevel,
            // so create a fake ent that goes back to the same level
            BeginIntermission(CreateTargetChangeLevel(level.mapname));
            return;
        }
        BeginIntermission(ent);
    }
}

/*
=================
CheckNeedPass
=================
*/
void CheckNeedPass(void)
{
    int need;

    // if password or spectator_password has changed, update needpass
    // as needed
    if (password->modified || spectator_password->modified) {
        password->modified = spectator_password->modified = false;

        need = 0;

        if (*password->string && Q_stricmp(password->string, "none"))
            need |= 1;
        if (*spectator_password->string && Q_stricmp(spectator_password->string, "none"))
            need |= 2;

        gi.cvar_set("needpass", va("%d", need));
    }
}

/*
=================
CheckDMRules
=================
*/
void CheckDMRules(void)
{
    int         i;
    gclient_t   *cl;

    if (level.intermission_framenum)
        return;

    if (!deathmatch->value)
        return;

    if (timelimit->value) {
        if (level.time >= timelimit->value * 60) {
            gi.bprintf(PRINT_HIGH, "Timelimit hit.\n");
            EndDMLevel();
            return;
        }
    }

    if (fraglimit->value) {
        for (i = 0; i < maxclients->value; i++) {
            cl = game.clients + i;
            if (!g_edicts[i + 1].inuse)
                continue;

            if (cl->resp.score >= fraglimit->value) {
                gi.bprintf(PRINT_HIGH, "Fraglimit hit.\n");
                EndDMLevel();
                return;
            }
        }
    }
}

/*
=============
ExitLevel
=============
*/
void ExitLevel(void)
{
    int     i;
    edict_t *ent;
    char    command[256];

    Q_snprintf(command, sizeof(command), "gamemap \"%s\"\n", level.changemap);
    gi.AddCommandString(command);
    level.changemap = NULL;
    level.exitintermission = 0;
    level.intermission_framenum = 0;

    // clear some things before going to next level
    for (i = 0; i < maxclients->value; i++) {
        ent = g_edicts + 1 + i;
        if (!ent->inuse)
            continue;
        if (ent->health > ent->client->pers.max_health)
            ent->health = ent->client->pers.max_health;
    }

}

/*
================
G_RunFrame

Advances the world by 0.1 seconds
================
*/
void G_RunFrame(void)
{
    int     i;
    edict_t *ent;

    level.framenum++;
    level.time = level.framenum * FRAMETIME;

    // choose a client for monsters to target this frame
    AI_SetSightClient();

    // exit intermissions
    if (level.exitintermission) {
        ExitLevel();
        return;
    }

    //
    // treat each object in turn
    // even the world gets a chance to think
    //
    ent = &g_edicts[0];
    for (i = 0; i < globals.num_edicts; i++, ent++) {
        if (!ent->inuse)
            continue;

        level.current_entity = ent;

        if (!(ent->s.renderfx & RF_BEAM))
            VectorCopy(ent->s.origin, ent->s.old_origin);

        // if the ground entity moved, make sure we are still on it
        if ((ent->groundentity) && (ent->groundentity->linkcount != ent->groundentity_linkcount)) {
            ent->groundentity = NULL;
            if (!(ent->flags & (FL_SWIM | FL_FLY)) && (ent->svflags & SVF_MONSTER)) {
                M_CheckGround(ent);
            }
        }

        if (i > 0 && i <= maxclients->value) {
            ClientBeginServerFrame(ent);
            continue;
        }

        G_RunEntity(ent);
    }

    // exit intermission right now to avoid annoying fov change
    if (level.exitintermission) {
        ExitLevel();
        return;
    }

    // see if it is time to end a deathmatch
    CheckDMRules();

    // see if needpass needs updated
    CheckNeedPass();

    // build the playerstate_t structures for all players
    ClientEndServerFrames();
}

