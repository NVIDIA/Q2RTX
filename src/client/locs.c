/*
Copyright (C) 2003-2006 Andrey Nazarov

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

//
// cl_locs.c
//

#include "client.h"

typedef struct {
    list_t entry;
    vec3_t origin;
    char name[1];
} location_t;

static LIST_DECL(cl_locations);

static cvar_t   *loc_draw;
static cvar_t   *loc_trace;
static cvar_t   *loc_dist;

/*
==============
LOC_Alloc
==============
*/
static location_t *LOC_Alloc(const char *name)
{
    location_t *loc;
    size_t len;

    len = strlen(name);
    loc = Z_Malloc(sizeof(*loc) + len);
    memcpy(loc->name, name, len + 1);

    return loc;
}

/*
==============
LOC_LoadLocations
==============
*/
void LOC_LoadLocations(void)
{
    char path[MAX_QPATH];
    char *buffer, *s, *p;
    int line, count;
    location_t *loc;
    int argc;
    int ret;

    // load from main directory
    Q_concat(path, sizeof(path), "locs/", cl.mapname, ".loc");

    ret = FS_LoadFile(path, (void **)&buffer);
    if (!buffer) {
        if (ret != Q_ERR(ENOENT)) {
            Com_EPrintf("Couldn't load %s: %s\n", path, Q_ErrorString(ret));
        }
        return;
    }

    s = buffer;
    line = count = 0;
    while (*s) {
        p = strchr(s, '\n');
        if (p) {
            *p = 0;
        }

        Cmd_TokenizeString(s, false);
        line++;

        argc = Cmd_Argc();
        if (argc) {
            if (argc < 4) {
                Com_WPrintf("Line %d is incomplete in %s\n", line, path);
            } else {
                loc = LOC_Alloc(Cmd_RawArgsFrom(3));
                loc->origin[0] = Q_atof(Cmd_Argv(0)) * 0.125f;
                loc->origin[1] = Q_atof(Cmd_Argv(1)) * 0.125f;
                loc->origin[2] = Q_atof(Cmd_Argv(2)) * 0.125f;
                List_Append(&cl_locations, &loc->entry);
                count++;
            }
        }

        if (!p) {
            break;
        }

        s = p + 1;
    }

    Com_DPrintf("Loaded %d location%s from %s\n",
                count, count == 1 ? "" : "s", path);

    FS_FreeFile(buffer);
}

/*
==============
LOC_FreeLocations
==============
*/
void LOC_FreeLocations(void)
{
    location_t *loc, *next;

    LIST_FOR_EACH_SAFE(location_t, loc, next, &cl_locations, entry) {
        Z_Free(loc);
    }

    List_Init(&cl_locations);
}

/*
==============
LOC_FindClosest
==============
*/
static location_t *LOC_FindClosest(const vec3_t pos)
{
    location_t *loc, *nearest;
    float dist, minDist;
    trace_t trace;

    minDist = 99999;
    nearest = NULL;
    LIST_FOR_EACH(location_t, loc, &cl_locations, entry) {
        dist = Distance(pos, loc->origin);
        if (dist > loc_dist->value) {
            continue;
        }

        if (loc_trace->integer) {
            CM_BoxTrace(&trace, pos, loc->origin, vec3_origin, vec3_origin,
                        cl.bsp->nodes, MASK_SOLID);
            if (trace.fraction != 1.0f) {
                continue;
            }
        }

        if (dist < minDist) {
            minDist = dist;
            nearest = loc;
        }
    }

    return nearest;
}

/*
==============
LOC_AddLocationsToScene
==============
*/
void LOC_AddLocationsToScene(void)
{
    location_t *loc, *nearest;
    float dist;
    entity_t ent;

    if (!loc_draw->integer) {
        return;
    }

    memset(&ent, 0, sizeof(ent));
    ent.model = R_RegisterModel("models/items/c_head/tris.md2");
    ent.skin = R_RegisterSkin("models/items/c_head/skin.pcx");
    ent.flags = RF_NOSHADOW;

    nearest = LOC_FindClosest(cl.playerEntityOrigin);
    if (!nearest) {
        return;
    }

    LIST_FOR_EACH(location_t, loc, &cl_locations, entry) {
        dist = Distance(cl.playerEntityOrigin, loc->origin);
        if (dist > loc_dist->integer) {
            continue;
        }

        VectorCopy(loc->origin, ent.origin);

        if (loc == nearest) {
            ent.origin[2] += 10.0f * sin(cl.time * 0.01f);
            V_AddLight(loc->origin, 200, 1, 1, 1);
        }

        V_AddEntity(&ent);
    }
}

/*
==============
LOC_Here_m
==============
*/
static size_t LOC_Here_m(char *buffer, size_t size)
{
    location_t *loc = NULL;

    if (cls.state == ca_active) {
        loc = LOC_FindClosest(cl.playerEntityOrigin);
    }

    return Q_strlcpy(buffer, loc ? loc->name : "unknown", size);
}

/*
==============
LOC_There_m
==============
*/
static size_t LOC_There_m(char *buffer, size_t size)
{
    location_t *loc = NULL;

    if (cls.state == ca_active) {
        vec3_t pos;
        trace_t trace;

        VectorMA(cl.playerEntityOrigin, 8192, cl.v_forward, pos);
        CM_BoxTrace(&trace, cl.playerEntityOrigin, pos, vec3_origin,
                    vec3_origin, cl.bsp->nodes, MASK_SOLID);

        loc = LOC_FindClosest(trace.endpos);
    }

    return Q_strlcpy(buffer, loc ? loc->name : "unknown", size);
}

static void LOC_Add_f(void)
{
    location_t *loc;

    if (Cmd_Argc() < 3) {
        Com_Printf("Usage: %s <name>\n", Cmd_ArgsRange(0, 1));
        return;
    }

    if (cls.state != ca_active) {
        Com_Printf("Must be in a level.\n");
        return;
    }

    loc = LOC_Alloc(Cmd_ArgsFrom(2));
    VectorCopy(cl.playerEntityOrigin, loc->origin);
    List_Append(&cl_locations, &loc->entry);

    Com_Printf("Added location %s at %s\n", loc->name, vtos(loc->origin));
}

static void LOC_Delete_f(void)
{
    location_t *loc;

    if (cls.state != ca_active) {
        Com_Printf("Must be in a level.\n");
        return;
    }

    loc = LOC_FindClosest(cl.playerEntityOrigin);
    if (!loc) {
        Com_Printf("No closest location.\n");
        return;
    }

    Com_Printf("Deleted location %s at %s\n", loc->name, vtos(loc->origin));
    List_Remove(&loc->entry);
    Z_Free(loc);
}

static void LOC_Set_f(void)
{
    location_t *oldloc, *newloc;

    if (Cmd_Argc() < 3) {
        Com_Printf("Usage: %s <name>\n", Cmd_ArgsRange(0, 1));
        return;
    }

    if (cls.state != ca_active) {
        Com_Printf("Must be in a level.\n");
        return;
    }

    oldloc = LOC_FindClosest(cl.playerEntityOrigin);
    if (!oldloc) {
        Com_Printf("No closest location.\n");
        return;
    }

    newloc = LOC_Alloc(Cmd_ArgsFrom(2));
    VectorCopy(oldloc->origin, newloc->origin);
    List_Link(oldloc->entry.prev, oldloc->entry.next, &newloc->entry);
    Z_Free(oldloc);

    Com_Printf("Renamed location at %s to %s\n", vtos(newloc->origin), newloc->name);
}

static void LOC_List_f(void)
{
    location_t *loc;
    int count;

    if (cls.state != ca_active) {
        Com_Printf("Must be in a level.\n");
        return;
    }

    if (LIST_EMPTY(&cl_locations)) {
        Com_Printf("No locations to list.\n");
        return;
    }

    count = 0;
    LIST_FOR_EACH(location_t, loc, &cl_locations, entry) {
        Com_Printf("%s at %s\n", loc->name, vtos(loc->origin));
        count++;
    }

    Com_Printf("%d location%s listed\n", count, count == 1 ? "" : "s");
}

static void LOC_Save_f(void)
{
    char buffer[MAX_QPATH];
    const char *name;
    location_t *loc;
    qhandle_t f;
    int count;

    if (cls.state != ca_active) {
        Com_Printf("Must be in a level.\n");
        return;
    }

    if (LIST_EMPTY(&cl_locations)) {
        Com_Printf("No locations to write.\n");
        return;
    }

    if (Cmd_Argc() > 2)
        name = Cmd_Argv(2);
    else
        name = cl.mapname;

    f = FS_EasyOpenFile(buffer, sizeof(buffer), FS_MODE_WRITE | FS_FLAG_TEXT,
                        "locs/", name, ".loc");
    if (!f) {
        return;
    }

    count = 0;
    LIST_FOR_EACH(location_t, loc, &cl_locations, entry) {
        FS_FPrintf(f, "%.f %.f %.f %s\n",
                   loc->origin[0] * 8,
                   loc->origin[1] * 8,
                   loc->origin[2] * 8,
                   loc->name);
        count++;
    }

    if (FS_CloseFile(f))
        Com_EPrintf("Error writing %s\n", buffer);
    else
        Com_Printf("Wrote %d location%s to %s\n",
                   count, count == 1 ? "" : "s", buffer);
}

static void LOC_Cmd_g(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        Prompt_AddMatch(ctx, "add");
        Prompt_AddMatch(ctx, "del");
        Prompt_AddMatch(ctx, "set");
        Prompt_AddMatch(ctx, "list");
        Prompt_AddMatch(ctx, "save");
    }
}

static void LOC_Cmd_f(void)
{
    const char *cmd = Cmd_Argv(1);

    if (!strcmp(cmd, "add"))
        LOC_Add_f();
    else if (!strcmp(cmd, "del"))
        LOC_Delete_f();
    else if (!strcmp(cmd, "set"))
        LOC_Set_f();
    else if (!strcmp(cmd, "list"))
        LOC_List_f();
    else if (!strcmp(cmd, "save"))
        LOC_Save_f();
    else
        Com_Printf("Usage: %s <add|del|set|list|save>\n", Cmd_Argv(0));
}

static const cmdreg_t c_loc[] = {
    { "loc", LOC_Cmd_f, LOC_Cmd_g },
    { NULL }
};

/*
==============
LOC_Init
==============
*/
void LOC_Init(void)
{
    loc_trace = Cvar_Get("loc_trace", "0", 0);
    loc_draw = Cvar_Get("loc_draw", "0", 0);
    loc_dist = Cvar_Get("loc_dist", "500", 0);

    Cmd_AddMacro("loc_here", LOC_Here_m);
    Cmd_AddMacro("loc_there", LOC_There_m);

    Cmd_Register(c_loc);
}

