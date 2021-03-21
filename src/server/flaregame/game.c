/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2021 Frank Richter

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

#include "shared/shared.h"
#include "game.h"
#include "local.h"

struct flaregame_local_s flaregame;

static void FlareGame_Init(void)
{
    flaregame.real_ge->Init();

    flaregame.exported_ge.edicts = flaregame.real_ge->edicts;
    flaregame.exported_ge.max_edicts = flaregame.real_ge->max_edicts;
    flaregame.exported_ge.num_edicts = flaregame.real_ge->num_edicts;
}

static void FlareGame_SpawnEntities(const char *mapname, const char *entities, const char *spawnpoint)
{
    memset(&flaregame.level, 0, sizeof(flaregame.level));
    flaregame.real_ge->SpawnEntities(mapname, entities, spawnpoint);
}

static void FlareGame_ReadGame(const char *filename)
{
    flaregame.real_ge->ReadGame(filename);
    flaregame.exported_ge.edicts = flaregame.real_ge->edicts;
}

static void FlareGame_Shutdown(void)
{
    flaregame.real_ge->Shutdown();

    memset(&flaregame, 0, sizeof(flaregame));
}

static void FlareGame_LinkEntity(edict_t* ent)
{
    flaregame.exported_ge.num_edicts = flaregame.real_ge->num_edicts;
    flaregame.real_gi.linkentity(ent);
}

static void FlareGame_UnLinkEntity(edict_t* ent)
{
    flaregame.real_gi.unlinkentity(ent);
}

static void FlareGame_ClientCommand(edict_t *ent)
{
    if (ent->client && (ent->client->ps.pmove.pm_type != PM_FREEZE))
    {
        char *cmd = flaregame.real_gi.argv(0);

        if (Q_stricmp(cmd, "throwflare") == 0)
        {
            flaregame.real_gi.centerprintf(ent, "Flare throw");
            return;
        }
    }

    flaregame.real_ge->ClientCommand(ent);
}

static void FlareGame_RunFrame(void)
{
    /* Idea: Here, "unlink" all flare entities, so the real game won't see them
       (and tries to do stuff with them)
     */

    flaregame.level.framenum++;

    flaregame.real_ge->RunFrame();

    // ...and after the game did it's thing, re-link all flare entities
}

game_export_t *FlareGame_Entry(game_export_t *(*entry)(game_import_t *), game_import_t *import)
{
    flaregame.real_gi = *import;

    flaregame.exported_gi = *import;
    flaregame.exported_gi.linkentity = &FlareGame_LinkEntity;
    flaregame.exported_gi.unlinkentity = &FlareGame_UnLinkEntity;

    game_export_t *ge = entry(&flaregame.exported_gi);
    if(!ge)
        return NULL;
    flaregame.real_ge = ge;

    flaregame.exported_ge = *ge;
    flaregame.exported_ge.Init = &FlareGame_Init;
    flaregame.exported_ge.Shutdown = &FlareGame_Shutdown;
    flaregame.exported_ge.SpawnEntities = &FlareGame_SpawnEntities;
    flaregame.exported_ge.ReadGame = &FlareGame_ReadGame;
    flaregame.exported_ge.ClientCommand = &FlareGame_ClientCommand;
    flaregame.exported_ge.RunFrame = &FlareGame_RunFrame;

    return &flaregame.exported_ge;
}
