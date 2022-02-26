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

#include "ent.h"

#include "common/math.h"
#include "common/protocol.h"

#include "local.h"

const char *FlareEnt_model = "models/objects/flare/tris.md2";

// From: P_ProjectSource
static void project_source(vec3_t point, vec3_t distance, vec3_t forward, vec3_t right, vec3_t result)
{
    vec3_t  _distance;

    VectorCopy(distance, _distance);
    _distance[1] = 0; // always use "center handedness"

    result[0] = point[0] + forward[0] * _distance[0] + right[0] * distance[1];
    result[1] = point[1] + forward[1] * _distance[0] + right[1] * distance[1];
    result[2] = point[2] + forward[2] * _distance[0] + right[2] * distance[1] + distance[2];
}

void FlareEnt_Init(struct flaregame_ent_s *flare_ent, edict_t *cmd_ent)
{
    memset(flare_ent, 0, sizeof(struct flaregame_ent_s));

    vec3_t forward;
    vec3_t start;

    if(cmd_ent) {
        vec3_t offset;
        vec3_t right;

        // Setup the parameters used to initialize the flare
        int viewheight = 22; // baseq2 value
        VectorSet(offset, 8, 8, viewheight - 8);
        AngleVectors(cmd_ent->client->ps.viewangles, forward, right, NULL);
        project_source(cmd_ent->s.origin, offset, forward, right, start);
    } else {
        VectorClear(start);
        VectorClear(forward);
    }

    VectorCopy(start, flare_ent->s.origin);
    VectorScale(forward, 800, flare_ent->velocity);
    VectorSet(flare_ent->angular_velocity, 300, 300, 300);

    const float size[3] = { 4, 1, 1 };
    VectorScale(size, -0.5f, flare_ent->mins);
    VectorScale(size, 0.5f, flare_ent->maxs);

    flare_ent->s.modelindex = flaregame.real_gi.modelindex(FlareEnt_model);
    flare_ent->s.solid = SOLID_BBOX;
    flare_ent->nextthink = flaregame.level.framenum + 1;
    flare_ent->eoltime = flaregame.level.framenum + 150; // live for 15 seconds
}

static void flare_touch(struct flaregame_ent_s *flare_ent)
{
    // Flares don't weigh that much, so let's have them stop
    // the instant they whack into anything.
    VectorClear(flare_ent->velocity);
    VectorClear(flare_ent->angular_velocity);
}

#define STOP_EPSILON    0.1

// from g_phys.c
static int clip_velocity(vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
    float   backoff;
    float   change;
    int     i, blocked;

    blocked = 0;
    if (normal[2] > 0)
        blocked |= 1;       // floor
    if (!normal[2])
        blocked |= 2;       // step

    backoff = DotProduct(in, normal) * overbounce;

    for (i = 0 ; i < 3 ; i++) {
        change = normal[i] * backoff;
        out[i] = in[i] - change;
        if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
            out[i] = 0;
    }

    return blocked;
}

// Based on SV_PushEntity
static trace_t push_entity(struct flaregame_ent_s *flare_ent, vec3_t push)
{
    trace_t trace;
    vec3_t  start;
    vec3_t  end;
    int     mask;

    VectorCopy(flare_ent->s.origin, start);
    VectorAdd(start, push, end);

    mask = MASK_SHOT;

    trace = flaregame.real_gi.trace(start, flare_ent->mins, flare_ent->maxs, end, NULL, mask);

    VectorCopy(trace.endpos, flare_ent->s.origin);

    if (trace.fraction != 1.0) {
        flare_touch(flare_ent);
    }

    return trace;
}

// Adapted from SV_Physics_Toss
static void flare_toss(struct flaregame_ent_s *flare_ent)
{
    trace_t     trace;
    vec3_t      move;
    float       backoff;
    qboolean    wasinwater;
    qboolean    isinwater;
    vec3_t      old_origin;

    if (flare_ent->velocity[2] > 0)
        flare_ent->groundentity = NULL;

    // check for the groundentity going away
    if (flare_ent->groundentity)
        if (!game_edict_in_use(flare_ent->groundentity))
            flare_ent->groundentity = NULL;

    // if onground, return without moving
    if (flare_ent->groundentity)
    {
        if(flare_ent->groundentity->linkcount != flare_ent->groundentity_linkcount)
        {
            // "Ground" entity might have moved (eg elevator), just move with it
            vec3_t ge_origin, ge_delta;
            VectorCopy(flare_ent->groundentity->s.origin, ge_origin);
            VectorSubtract(ge_origin, flare_ent->groundentity_origin, ge_delta);
            VectorAdd(flare_ent->s.origin, ge_delta, flare_ent->s.origin);
            VectorCopy(ge_origin, flare_ent->groundentity_origin);
        }
        return;
    }

    VectorCopy(flare_ent->s.origin, old_origin);

    // bound velocity
    for (int i = 0 ; i < 3 ; i++) {
        if (flare_ent->velocity[i] > sv_maxvelocity->value)
            flare_ent->velocity[i] = sv_maxvelocity->value;
        else if (flare_ent->velocity[i] < -sv_maxvelocity->value)
            flare_ent->velocity[i] = -sv_maxvelocity->value;
    }

    // add gravity
    flare_ent->velocity[2] -= sv_gravity->value * BASE_FRAMETIME_1000;

    // move angles
    VectorMA(flare_ent->s.angles, BASE_FRAMETIME_1000, flare_ent->angular_velocity, flare_ent->s.angles);

    // move origin
    VectorScale(flare_ent->velocity, BASE_FRAMETIME_1000, move);
    trace = push_entity(flare_ent, move);

    if (trace.fraction < 1) {
        backoff = 1.5;

        clip_velocity(flare_ent->velocity, trace.plane.normal, flare_ent->velocity, backoff);

        // stop if on ground
        if (trace.plane.normal[2] > 0.7) {
            if (flare_ent->velocity[2] < 60) {
                flare_ent->groundentity = trace.ent;
                flare_ent->groundentity_linkcount = trace.ent->linkcount;
                VectorCopy(trace.ent->s.origin, flare_ent->groundentity_origin);
                VectorCopy(vec3_origin, flare_ent->velocity);
                VectorCopy(vec3_origin, flare_ent->angular_velocity);
            }
        }
    }

    // check for water transition
    wasinwater = (flare_ent->watertype & MASK_WATER);
    flare_ent->watertype = flaregame.real_gi.pointcontents(flare_ent->s.origin);
    isinwater = flare_ent->watertype & MASK_WATER;

    if (isinwater)
        flare_ent->waterlevel = 1;
    else
        flare_ent->waterlevel = 0;

    if (!wasinwater && isinwater)
        flaregame.real_gi.positioned_sound(old_origin, flaregame.real_ge->edicts, CHAN_AUTO, flaregame.real_gi.soundindex("misc/h2ohit1.wav"), 1, 1, 0);
    else if (wasinwater && !isinwater)
        flaregame.real_gi.positioned_sound(flare_ent->s.origin, flaregame.real_ge->edicts, CHAN_AUTO, flaregame.real_gi.soundindex("misc/h2ohit1.wav"), 1, 1, 0);
}

/*
 * Drops a spark from the flare flying thru the air.
 */
static void flare_sparks(struct flaregame_ent_s *self)
{
    // Insert some randomness into "spark" effect
    if ((rand () & 0x7fff) >= (int)(0x7fff * 0.9))
        return;

    vec3_t dir;
    vec3_t forward, right, up;
    // Spawn some sparks.  This isn't net-friendly at all, but will
    // be fine for single player.
    flaregame.real_gi.WriteByte(svc_temp_entity);
    flaregame.real_gi.WriteByte(TE_FLARE);

    flaregame.real_gi.WriteShort(0); // supposed to be entity num; unused by client
    // if this is the first tick of flare, set count to 1 to start the sound
    flaregame.real_gi.WriteByte(self->eoltime - flaregame.level.framenum < 148 ? 0 : 1);

    flaregame.real_gi.WritePosition(self->s.origin);

    // If we are still moving, calculate the normal to the direction
    // we are travelling.
    if (VectorLength(self->velocity) > 0.0)
    {
        vectoangles2(self->velocity, dir);
        AngleVectors(dir, forward, right, up);

        flaregame.real_gi.WriteDir(up);
    }
    // If we're stopped, just write out the origin as our normal
    else
    {
        flaregame.real_gi.WriteDir(vec3_origin);
    }
    flaregame.real_gi.multicast(self->s.origin, MULTICAST_PVS);
}

// Puff of smoke if flare goes out
static void flare_smoke(struct flaregame_ent_s *self)
{
    flaregame.real_gi.WriteByte(svc_temp_entity);
    flaregame.real_gi.WriteByte(TE_STEAM);
    flaregame.real_gi.WriteShort(-1); // One-off effect
    flaregame.real_gi.WriteByte(40); // Count
    flaregame.real_gi.WritePosition(self->s.origin); // origin
    const vec3_t up = {0, 0, 1};
    flaregame.real_gi.WriteDir(up); // direction
    flaregame.real_gi.WriteByte(0x3); // color
    flaregame.real_gi.WriteShort(50); // magnitude

    flaregame.real_gi.multicast(self->s.origin, MULTICAST_PVS);
}

flare_disposition_t FlareEnt_Think(struct flaregame_ent_s *self)
{
    if (flaregame.level.framenum >= self->eoltime) {
        if(!self->waterlevel) {
            // A small goodbye puff of smoke (if not underwater)
            flare_smoke(self);
        }
        return FLAREENT_REMOVE;
    }

    flare_toss(self);
    if (flaregame.level.framenum >= self->nextthink)
    {
        flare_sparks(self);

        // We'll think again in .2 seconds
        self->nextthink = flaregame.level.framenum + 2;
    }

    return FLAREENT_KEEP;
}
