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

#include "common/protocol.h"

#include "local.h"

void FlareEnt_Init(struct flaregame_ent_s *flare_ent, edict_t *cmd_ent)
{
    VectorScale(cmd_ent->client->ps.pmove.origin, 0.125f, flare_ent->origin);
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
    flaregame.real_gi.WriteByte( 0 /*self->timestamp - level.time < 14.75 ? 0 : 1*/);

    flaregame.real_gi.WritePosition(self->origin);

    // If we are still moving, calculate the normal to the direction
    // we are travelling.
    /*if (VectorLength(self->velocity) > 0.0)
    {
        vectoangles(self->velocity, dir);
        AngleVectors(dir, forward, right, up);

        gi.WriteDir(up);
    }
    // If we're stopped, just write out the origin as our normal
    else*/
    {
        flaregame.real_gi.WriteDir(vec3_origin);
    }
    flaregame.real_gi.multicast(self->origin, MULTICAST_PVS);
}

qboolean FlareEnt_Think(struct flaregame_ent_s *self)
{
    // TODO: Move
    // TODO: Lifetime
    flare_sparks(self);

    return FLAREENT_KEEP;
}
