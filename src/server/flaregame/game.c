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
#include "common/zone.h"
#include "../server.h" // used for "entity in use" logic
#include "ent.h"
#include "game.h"
#include "local.h"

#include <assert.h>

#define TAG_FLAREGAME       707

struct flaregame_local_s flaregame;

static flaregame_flare_t *Flare_Alloc(void)
{
    flaregame_flare_t *flare;
    if(!LIST_EMPTY(&flaregame.avail_flares))
    {
        flare = LIST_FIRST(flaregame_flare_t, &flaregame.avail_flares, entry);
        List_Delete(&flare->entry);
    }
    else
    {
        flare = Z_TagMallocz(sizeof(flaregame_flare_t), TAG_FLAREGAME);
        List_Init(&flare->entry);
    }
    assert(!flare->game_edict);
    return flare;
}

static void Flare_Free(flaregame_flare_t *flare)
{
    flare->game_edict = NULL;
    List_Delete(&flare->entry);
    List_Append(&flaregame.avail_flares, &flare->entry);
}

static void FlareGame_Init(void)
{
    flaregame.real_ge->Init();

    flaregame.exported_ge.edicts = flaregame.real_ge->edicts;
    flaregame.exported_ge.max_edicts = flaregame.real_ge->max_edicts;
    flaregame.exported_ge.num_edicts = flaregame.real_ge->num_edicts;
}

static void FlareGame_SpawnEntities(const char *mapname, const char *entities, const char *spawnpoint)
{
    flaregame_flare_t *flare, *next_flare;
    LIST_FOR_EACH_SAFE(flaregame_flare_t, flare, next_flare, &flaregame.active_flares, entry)
    {
        Flare_Free(flare);
    }
    LIST_FOR_EACH_SAFE(flaregame_flare_t, flare, next_flare, &flaregame.avail_flares, entry)
    {
        Z_Free(flare);
    }

    memset(&flaregame.level, 0, sizeof(flaregame.level));
    List_Init(&flaregame.active_flares);
    List_Init(&flaregame.avail_flares);

    flaregame.real_ge->SpawnEntities(mapname, entities, spawnpoint);

    // precache
    flaregame.real_gi.modelindex(FlareEnt_model);
}

static void FlareGame_ReadGame(const char *filename)
{
    flaregame.real_ge->ReadGame(filename);
    flaregame.exported_ge.edicts = flaregame.real_ge->edicts;
}

static void FlareGame_Shutdown(void)
{
    flaregame.real_ge->Shutdown();

    Z_FreeTags(TAG_FLAREGAME);
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
            flaregame_flare_t *flare = Flare_Alloc();
            FlareEnt_Init(&flare->ent, ent);
            List_Insert(&flaregame.active_flares, &flare->entry);
            return;
        }
    }

    flaregame.real_ge->ClientCommand(ent);
}

static void unlink_all_flares(void)
{
    flaregame_flare_t *flare;
    LIST_FOR_EACH(flaregame_flare_t, flare, &flaregame.active_flares, entry)
    {
        if(flare->game_edict == NULL)
            continue;

        FlareGame_UnLinkEntity(flare->game_edict);
        flare->game_edict->inuse = qfalse;
        flare->game_edict->s.modelindex = 0;
        flare->game_edict->s.effects = 0;
        flare->game_edict->s.sound = 0;
        flare->game_edict->s.event = 0;
    }
}

static inline qboolean game_edict_in_use(struct edict_s* ent)
{
    if(g_features->integer & GMF_PROPERINUSE)
        return ent->inuse;

    return ES_INUSE(&ent->s);
}

#define GET_REAL_EDICT(n) ((edict_t *)((byte *)flaregame.real_ge->edicts + flaregame.real_ge->edict_size*(n)))
#define REAL_NUM_FOR_EDICT(e) ((int)(((byte *)(e) - (byte *)flaregame.real_ge->edicts) / flaregame.real_ge->edict_size))

static edict_t* find_free_game_edict(void)
{
    int maxclients = sv_maxclients->value;
    int i;

    // Based on G_Spawn()
    for (i = maxclients + 1 ; i < flaregame.real_ge->num_edicts ; i++) {
        edict_t *e = GET_REAL_EDICT(i);
        if(!game_edict_in_use(e))
            return e;
    }

    if (i == flaregame.real_ge->max_edicts)
        return NULL;

    edict_t *e = GET_REAL_EDICT(i);
    flaregame.real_ge->num_edicts++;
    return e;
}

static void link_one_flare(flaregame_flare_t *flare, struct edict_s *ent)
{
    flare->game_edict = ent;
    if(!ent)
        return;

    ent->inuse = qtrue;
    memcpy(&ent->s, &flare->ent.s, sizeof(struct entity_state_s));
    ent->s.number = REAL_NUM_FOR_EDICT(ent);
    FlareGame_LinkEntity(flare->game_edict);
}

static void link_all_flares(void)
{
    flaregame_flare_t *flare;
    // Pass one: preferably link flares to previous edict
    LIST_FOR_EACH(flaregame_flare_t, flare, &flaregame.active_flares, entry)
    {
        if(!flare->game_edict || game_edict_in_use(flare->game_edict))
        {
            flare->game_edict = NULL; // consider it on next pass
            continue;
        }

        link_one_flare(flare, flare->game_edict);
    }

    // Pass two: allocate flares to "free" edicts
    LIST_FOR_EACH(flaregame_flare_t, flare, &flaregame.active_flares, entry)
    {
        if(flare->game_edict)
            continue;

        // Need to look for new slot
        edict_t* ent = find_free_game_edict();
        link_one_flare(flare, ent);
    }

    /* FIXME: If the edict pressure is high, the algorithm above has the drawback
       that it may prefer to "keep" older flares and "skip" more recently added ones,
       which doesn't appear the most sensible approach.
       Perhaps the algo could be tweaked... if flares are considered worth the effort. */
}

static void FlareGame_RunFrame(void)
{
    flaregame.level.framenum++;

    /* Here, "unlink" all flare entities, so the real game won't see them
       (and tries to do stuff with them - this would not end well) */
    unlink_all_flares();

    flaregame.real_ge->RunFrame();

    // Run all flares
    flaregame_flare_t *flare, *next_flare;
    LIST_FOR_EACH_SAFE(flaregame_flare_t, flare, next_flare, &flaregame.active_flares, entry)
    {
        if(FlareEnt_Think(&flare->ent) == FLAREENT_KEEP)
            continue;

        Flare_Free(flare);
    }

    // ...and after the game did it's thing, link (or re-link) all flare entities
    link_all_flares();
}

game_export_t *FlareGame_Entry(game_export_t *(*entry)(game_import_t *), game_import_t *import)
{
    memset(&flaregame, 0, sizeof(flaregame));
    List_Init(&flaregame.active_flares);
    List_Init(&flaregame.avail_flares);

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
