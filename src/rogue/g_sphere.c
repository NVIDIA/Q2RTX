/*
 * =======================================================================
 *
 * Defender sphere.
 *
 * =======================================================================
 */

#include "header/local.h"

#define DEFENDER_LIFESPAN 30
#define HUNTER_LIFESPAN 30
#define VENGEANCE_LIFESPAN 30
#define MINIMUM_FLY_TIME 15

extern char *ED_NewString(char *string);
void LookAtKiller(edict_t *self, edict_t *inflictor, edict_t *attacker);

void defender_think(edict_t *self);
void hunter_think(edict_t *self);
void vengeance_think(edict_t *self);
void vengeance_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);
void hunter_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);

void
sphere_think_explode(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->owner && self->owner->client &&
		!(self->spawnflags & SPHERE_DOPPLEGANGER))
	{
		self->owner->client->owned_sphere = NULL;
	}

	BecomeExplosion1(self);
}

void
sphere_explode(edict_t *self, edict_t *inflictor /* unused */, edict_t *attacker /* unused */,
		int damage /* unused */, vec3_t point /* unused */)
{
	if (!self)
	{
		return;
	}

	sphere_think_explode(self);
}

void
sphere_if_idle_die(edict_t *self, edict_t *inflictor /* unused */, edict_t *attacker /* unused */,
		int damage /* unused */, vec3_t point /* unused */)
{
	if (!self)
	{
		return;
	}

	if (!self->enemy)
	{
		sphere_think_explode(self);
	}
}

void
sphere_fly(edict_t *self)
{
	vec3_t dest;
	vec3_t dir;

	if (!self)
	{
		return;
	}

	if (level.time >= self->wait)
	{
		sphere_think_explode(self);
		return;
	}

	VectorCopy(self->owner->s.origin, dest);
	dest[2] = self->owner->absmax[2] + 4;

	if (level.time == (float)(int)level.time)
	{
		if (!visible(self, self->owner))
		{
			VectorCopy(dest, self->s.origin);
			gi.linkentity(self);
			return;
		}
	}

	VectorSubtract(dest, self->s.origin, dir);
	VectorScale(dir, 5, self->velocity);
}

void
sphere_chase(edict_t *self, int stupidChase)
{
	vec3_t dest;
	vec3_t dir;
	float dist;

	if (!self)
	{
		return;
	}

	if ((level.time >= self->wait) ||
		(self->enemy && (self->enemy->health < 1)))
	{
		sphere_think_explode(self);
		return;
	}

	VectorCopy(self->enemy->s.origin, dest);

	if (self->enemy->client)
	{
		dest[2] += self->enemy->viewheight;
	}

	if (visible(self, self->enemy) || stupidChase)
	{
		/* if moving, hunter sphere uses active sound */
		if (!stupidChase)
		{
			self->s.sound = gi.soundindex("spheres/h_active.wav");
		}

		VectorSubtract(dest, self->s.origin, dir);
		VectorNormalize(dir);
		vectoangles2(dir, self->s.angles);
		VectorScale(dir, 500, self->velocity);
		VectorCopy(dest, self->monsterinfo.saved_goal);
	}
	else if (VectorCompare(self->monsterinfo.saved_goal, vec3_origin))
	{
		VectorSubtract(self->enemy->s.origin, self->s.origin, dir);
		vectoangles2(dir, self->s.angles);

		/* if lurking, hunter sphere uses lurking sound */
		self->s.sound = gi.soundindex("spheres/h_lurk.wav");
		VectorClear(self->velocity);
	}
	else
	{
		VectorSubtract(self->monsterinfo.saved_goal, self->s.origin, dir);
		dist = VectorNormalize(dir);

		if (dist > 1)
		{
			vectoangles2(dir, self->s.angles);

			if (dist > 500)
			{
				VectorScale(dir, 500, self->velocity);
			}
			else if (dist < 20)
			{
				VectorScale(dir, (dist / FRAMETIME), self->velocity);
			}
			else
			{
				VectorScale(dir, dist, self->velocity);
			}

			/* if moving, hunter sphere uses active sound */
			if (!stupidChase)
			{
				self->s.sound = gi.soundindex("spheres/h_active.wav");
			}
		}
		else
		{
			VectorSubtract(self->enemy->s.origin, self->s.origin, dir);
			vectoangles2(dir, self->s.angles);

			/* if not moving, hunter sphere uses lurk sound */
			if (!stupidChase)
			{
				self->s.sound = gi.soundindex("spheres/h_lurk.wav");
			}

			VectorClear(self->velocity);
		}
	}
}

void
sphere_fire(edict_t *self, edict_t *enemy)
{
	vec3_t dest;
	vec3_t dir;

	if (!self || !enemy)
	{
		return;
	}

	if ((level.time >= self->wait) || !enemy)
	{
		sphere_think_explode(self);
		return;
	}

	VectorCopy(enemy->s.origin, dest);
	self->s.effects |= EF_ROCKET;

	VectorSubtract(dest, self->s.origin, dir);
	VectorNormalize(dir);
	vectoangles2(dir, self->s.angles);
	VectorScale(dir, 1000, self->velocity);

	self->touch = vengeance_touch;
	self->think = sphere_think_explode;
	self->nextthink = self->wait;
}

void
sphere_touch(edict_t *self, edict_t *other, cplane_t *plane,
		csurface_t *surf, int mod)
{
	if (!self || !other || !plane || !surf)
	{
		return;
	}

	if (self->spawnflags & SPHERE_DOPPLEGANGER)
	{
		if (other == self->teammaster)
		{
			return;
		}

		self->takedamage = DAMAGE_NO;
		self->owner = self->teammaster;
		self->teammaster = NULL;
	}
	else
	{
		if (other == self->owner)
		{
			return;
		}

		/* Don't blow up on bodies */
		if (!strcmp(other->classname, "bodyque"))
		{
			return;
		}
	}

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (other->takedamage)
	{
		T_Damage(other, self, self->owner, self->velocity, self->s.origin,
				plane->normal, 10000, 1, DAMAGE_DESTROY_ARMOR, mod);
	}
	else
	{
		T_RadiusDamage(self, self->owner, 512, self->owner, 256, mod);
	}

	sphere_think_explode(self);
}

void
vengeance_touch(edict_t *self, edict_t *other, cplane_t *plane,
		csurface_t *surf)
{
	if (!self || !other || !plane)
	{
		return;
	}

	if (self->spawnflags & SPHERE_DOPPLEGANGER)
	{
		sphere_touch(self, other, plane, surf, MOD_DOPPLE_VENGEANCE);
	}
	else
	{
		sphere_touch(self, other, plane, surf, MOD_VENGEANCE_SPHERE);
	}
}

void
hunter_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	edict_t *owner;

	if (!self || !other || !plane || !surf)
	{
		return;
	}

	/* don't blow up if you hit the world.... sheesh. */
	if (other == world)
	{
		return;
	}

	if (self->owner)
	{
		/* if owner is flying with us, make sure they stop too. */
		owner = self->owner;

		if (owner->flags & FL_SAM_RAIMI)
		{
			VectorClear(owner->velocity);
			owner->movetype = MOVETYPE_NONE;
			gi.linkentity(owner);
		}
	}

	if (self->spawnflags & SPHERE_DOPPLEGANGER)
	{
		sphere_touch(self, other, plane, surf, MOD_DOPPLE_HUNTER);
	}
	else
	{
		sphere_touch(self, other, plane, surf, MOD_HUNTER_SPHERE);
	}
}

void
defender_shoot(edict_t *self, edict_t *enemy)
{
	vec3_t dir;
	vec3_t start;

	if (!self || !enemy)
	{
		return;
	}

	if (!(enemy->inuse) || (enemy->health <= 0))
	{
		return;
	}

	if (enemy == self->owner)
	{
		return;
	}

	VectorSubtract(enemy->s.origin, self->s.origin, dir);
	VectorNormalize(dir);

	if (self->monsterinfo.attack_finished > level.time)
	{
		return;
	}

	if (!visible(self, self->enemy))
	{
		return;
	}

	VectorCopy(self->s.origin, start);
	start[2] += 2;
	fire_blaster2(self->owner, start, dir, 10, 1000, EF_BLASTER, 0);

	self->monsterinfo.attack_finished = level.time + 0.4;
}

void
body_gib(edict_t *self)
{
	int n;

	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

	for (n = 0; n < 4; n++)
	{
		ThrowGib(self, "models/objects/gibs/sm_meat/tris.md2", 50, GIB_ORGANIC);
	}

	ThrowGib(self, "models/objects/gibs/skull/tris.md2", 50, GIB_ORGANIC);
}

void
hunter_pain(edict_t *self, edict_t *other, float kick, int damage)
{
	edict_t *owner;
	float dist;
	vec3_t dir;

	if (!self || !other)
	{
		return;
	}

	if (self->enemy)
	{
		return;
	}

	owner = self->owner;

	if (!(self->spawnflags & SPHERE_DOPPLEGANGER))
	{
		if (owner && (owner->health > 0))
		{
			return;
		}

		if (other == owner)
		{
			return;
		}
	}
	else
	{
		/* if fired by a doppleganger, set it to 10 second timeout */
		self->wait = level.time + MINIMUM_FLY_TIME;
	}

	if ((self->wait - level.time) < MINIMUM_FLY_TIME)
	{
		self->wait = level.time + MINIMUM_FLY_TIME;
	}

	self->s.effects |= EF_BLASTER | EF_TRACKER;
	self->touch = hunter_touch;
	self->enemy = other;

	if ((self->spawnflags & SPHERE_DOPPLEGANGER) || !(owner && owner->client))
	{
		return;
	}

	if (!((int)dmflags->value & DF_FORCE_RESPAWN) &&
		(huntercam && (huntercam->value)))
	{
		VectorSubtract(other->s.origin, self->s.origin, dir);
		dist = VectorLength(dir);

		if (owner && (dist >= 192))
		{
			/* detach owner from body and send him flying */
			owner->movetype = MOVETYPE_FLYMISSILE;

			/* gib like we just died, even though we didn't, really. */
			body_gib(owner);

			/* move the sphere to the owner's current viewpoint./
			   we know it's a valid spot (or will be momentarily) */
			VectorCopy(owner->s.origin, self->s.origin);
			self->s.origin[2] += owner->viewheight;

			/* move the player's origin to the sphere's new origin */
			VectorCopy(self->s.origin, owner->s.origin);
			VectorCopy(self->s.angles, owner->s.angles);
			VectorCopy(self->s.angles, owner->client->v_angle);
			VectorClear(owner->mins);
			VectorClear(owner->maxs);
			VectorSet(owner->mins, -5, -5, -5);
			VectorSet(owner->maxs, 5, 5, 5);
			owner->client->ps.fov = 140;
			owner->s.modelindex = 0;
			owner->s.modelindex2 = 0;
			owner->viewheight = 8;
			owner->solid = SOLID_NOT;
			owner->flags |= FL_SAM_RAIMI;
			gi.linkentity(owner);

			self->solid = SOLID_BBOX;
			gi.linkentity(self);
		}
	}
}

void
defender_pain(edict_t *self, edict_t *other, float kick, int damage)
{
	if (!self || !other)
	{
		return;
	}

	if (other == self->owner)
	{
		return;
	}

	self->enemy = other;
}

void
vengeance_pain(edict_t *self, edict_t *other, float kick, int damage)
{
	if (!self || !other)
	{
		return;
	}

	if (self->enemy)
	{
		return;
	}

	if (!(self->spawnflags & SPHERE_DOPPLEGANGER))
	{
		if (self->owner->health >= 25)
		{
			return;
		}

		if (other == self->owner)
		{
			return;
		}
	}
	else
	{
		self->wait = level.time + MINIMUM_FLY_TIME;
	}

	if ((self->wait - level.time) < MINIMUM_FLY_TIME)
	{
		self->wait = level.time + MINIMUM_FLY_TIME;
	}

	self->s.effects |= EF_ROCKET;
	self->touch = vengeance_touch;
	self->enemy = other;
}

void
defender_think(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (!self->owner)
	{
		G_FreeEdict(self);
		return;
	}

	/* if we've exited the level, just remove ourselves. */
	if (level.intermissiontime)
	{
		sphere_think_explode(self);
		return;
	}

	if (self->owner->health <= 0)
	{
		sphere_think_explode(self);
		return;
	}

	self->s.frame++;

	if (self->s.frame > 19)
	{
		self->s.frame = 0;
	}

	if (self->enemy)
	{
		if (self->enemy->health > 0)
		{
			defender_shoot(self, self->enemy);
		}
		else
		{
			self->enemy = NULL;
		}
	}

	sphere_fly(self);

	if (self->inuse)
	{
		self->nextthink = level.time + 0.1;
	}
}

void
hunter_think(edict_t *self)
{
	edict_t *owner;
	vec3_t dir, ang;

	if (!self)
	{
		return;
	}

	/* if we've exited the level, just remove ourselves. */
	if (level.intermissiontime)
	{
		sphere_think_explode(self);
		return;
	}

	owner = self->owner;

	if (!owner && !(self->spawnflags & SPHERE_DOPPLEGANGER))
	{
		G_FreeEdict(self);
		return;
	}

	if (owner)
	{
		self->ideal_yaw = owner->s.angles[YAW];
	}
	else if (self->enemy) /* fired by doppleganger */
	{
		VectorSubtract(self->enemy->s.origin, self->s.origin, dir);
		vectoangles2(dir, ang);
		self->ideal_yaw = ang[YAW];
	}

	M_ChangeYaw(self);

	if (self->enemy)
	{
		sphere_chase(self, 0);

		/* deal with sam raimi cam */
		if (owner && (owner->flags & FL_SAM_RAIMI))
		{
			if (self->inuse)
			{
				owner->movetype = MOVETYPE_FLYMISSILE;
				LookAtKiller(owner, self, self->enemy);

				/* owner is flying with us, move him too */
				owner->movetype = MOVETYPE_FLYMISSILE;
				owner->viewheight = self->s.origin[2] - owner->s.origin[2];
				VectorCopy(self->s.origin, owner->s.origin);
				VectorCopy(self->velocity, owner->velocity);
				VectorClear(owner->mins);
				VectorClear(owner->maxs);
				gi.linkentity(owner);
			}
			else /* sphere timed out */
			{
				VectorClear(owner->velocity);
				owner->movetype = MOVETYPE_NONE;
				gi.linkentity(owner);
			}
		}
	}
	else
	{
		sphere_fly(self);
	}

	if (self->inuse)
	{
		self->nextthink = level.time + 0.1;
	}
}

void
vengeance_think(edict_t *self)
{
	if (!self)
	{
		return;
	}

	/* if we've exited the level, just remove ourselves. */
	if (level.intermissiontime)
	{
		sphere_think_explode(self);
		return;
	}

	if (!(self->owner) && !(self->spawnflags & SPHERE_DOPPLEGANGER))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->enemy)
	{
		sphere_chase(self, 1);
	}
	else
	{
		sphere_fly(self);
	}

	if (self->inuse)
	{
		self->nextthink = level.time + 0.1;
	}
}

edict_t *
Sphere_Spawn(edict_t *owner, int spawnflags)
{
	edict_t *sphere;

	if (!owner)
	{
		return NULL;
	}

	sphere = G_Spawn();
	VectorCopy(owner->s.origin, sphere->s.origin);
	sphere->s.origin[2] = owner->absmax[2];
	sphere->s.angles[YAW] = owner->s.angles[YAW];
	sphere->solid = SOLID_BBOX;
	sphere->clipmask = MASK_SHOT;
	sphere->s.renderfx = RF_FULLBRIGHT | RF_IR_VISIBLE;
	sphere->movetype = MOVETYPE_FLYMISSILE;

	if (spawnflags & SPHERE_DOPPLEGANGER)
	{
		sphere->teammaster = owner->teammaster;
	}
	else
	{
		sphere->owner = owner;
	}

	sphere->classname = "sphere";
	sphere->yaw_speed = 40;
	sphere->monsterinfo.attack_finished = 0;
	sphere->spawnflags = spawnflags; /* need this for the HUD to recognize sphere */
	/* PMM */
	sphere->takedamage = DAMAGE_NO;

	switch (spawnflags & SPHERE_TYPE)
	{
		case SPHERE_DEFENDER:
			sphere->s.modelindex = gi.modelindex("models/items/defender/tris.md2");
			sphere->s.modelindex2 =
				gi.modelindex("models/items/shell/tris.md2");
			sphere->s.sound = gi.soundindex("spheres/d_idle.wav");
			sphere->pain = defender_pain;
			sphere->wait = level.time + DEFENDER_LIFESPAN;
			sphere->die = sphere_explode;
			sphere->think = defender_think;
			break;
		case SPHERE_HUNTER:
			sphere->s.modelindex = gi.modelindex("models/items/hunter/tris.md2");
			sphere->s.sound = gi.soundindex("spheres/h_idle.wav");
			sphere->wait = level.time + HUNTER_LIFESPAN;
			sphere->pain = hunter_pain;
			sphere->die = sphere_if_idle_die;
			sphere->think = hunter_think;
			break;
		case SPHERE_VENGEANCE:
			sphere->s.modelindex = gi.modelindex("models/items/vengnce/tris.md2");
			sphere->s.sound = gi.soundindex("spheres/v_idle.wav");
			sphere->wait = level.time + VENGEANCE_LIFESPAN;
			sphere->pain = vengeance_pain;
			sphere->die = sphere_if_idle_die;
			sphere->think = vengeance_think;
			VectorSet(sphere->avelocity, 30, 30, 0);
			break;
		default:
			gi.dprintf("Tried to create an invalid sphere\n");
			G_FreeEdict(sphere);
			return NULL;
	}

	sphere->nextthink = level.time + 0.1;

	gi.linkentity(sphere);

	return sphere;
}

void
Own_Sphere(edict_t *self, edict_t *sphere)
{
	if (!sphere || !self)
	{
		return;
	}

	/* ownership only for players */
	if (self->client)
	{
		/* if they don't have one */
		if (!(self->client->owned_sphere))
		{
			self->client->owned_sphere = sphere;
		}
		/* they already have one, take care of the old one */
		else
		{
			if (self->client->owned_sphere->inuse)
			{
				G_FreeEdict(self->client->owned_sphere);
				self->client->owned_sphere = sphere;
			}
			else
			{
				self->client->owned_sphere = sphere;
			}
		}
	}
}

void
Defender_Launch(edict_t *self)
{
	edict_t *sphere;

	if (!self)
	{
		return;
	}

	sphere = Sphere_Spawn(self, SPHERE_DEFENDER);
	Own_Sphere(self, sphere);
}

void
Hunter_Launch(edict_t *self)
{
	edict_t *sphere;

	if (!self)
	{
		return;
	}

	sphere = Sphere_Spawn(self, SPHERE_HUNTER);
	Own_Sphere(self, sphere);
}

void
Vengeance_Launch(edict_t *self)
{
	edict_t *sphere;

	if (!self)
	{
		return;
	}

	sphere = Sphere_Spawn(self, SPHERE_VENGEANCE);
	Own_Sphere(self, sphere);
}
