/*
 * =======================================================================
 *
 * Rogue specific weapons.
 *
 * =======================================================================
 */

#include "header/local.h"

#define NUKE_DELAY 4
#define NUKE_TIME_TO_LIVE 6
#define NUKE_RADIUS 512
#define NUKE_DAMAGE 400
#define NUKE_QUAKE_TIME 3
#define NUKE_QUAKE_STRENGTH 100

#define PROX_TIME_TO_LIVE 45
#define PROX_TIME_DELAY 0.5
#define PROX_BOUND_SIZE 96
#define PROX_DAMAGE_RADIUS 192
#define PROX_HEALTH 20
#define PROX_DAMAGE 90

#define TESLA_TIME_TO_LIVE 30
#define TESLA_DAMAGE_RADIUS 128
#define TESLA_DAMAGE 3
#define TESLA_KNOCKBACK 8
#define TESLA_ACTIVATE_TIME 3
#define TESLA_EXPLOSION_DAMAGE_MULT 50
#define TESLA_EXPLOSION_RADIUS 200

#define TRACKER_DAMAGE_FLAGS (DAMAGE_NO_POWER_ARMOR | DAMAGE_ENERGY | DAMAGE_NO_KNOCKBACK)
#define TRACKER_IMPACT_FLAGS (DAMAGE_NO_POWER_ARMOR | DAMAGE_ENERGY)
#define TRACKER_DAMAGE_TIME 0.5

extern byte P_DamageModifier(edict_t *ent);
extern void check_dodge(edict_t *self, vec3_t start, vec3_t dir, int speed);
extern void hurt_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);
extern void droptofloor(edict_t *ent);
extern void Grenade_Explode(edict_t *ent);
extern void drawbbox(edict_t *ent);

void
flechette_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t dir;

	if (!self || !other || !plane || !surf)
	{
		return;
	}

	if (other == self->owner)
	{
		return;
	}

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->client)
	{
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);
	}

	if (other->takedamage)
	{
		T_Damage(other, self, self->owner, self->velocity, self->s.origin,
				plane->normal, self->dmg, self->dmg_radius, DAMAGE_NO_REG_ARMOR,
				MOD_ETF_RIFLE);
	}
	else
	{
		if (!plane)
		{
			VectorClear(dir);
		}
		else
		{
			VectorScale(plane->normal, 256, dir);
		}

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_FLECHETTE);
		gi.WritePosition(self->s.origin);
		gi.WriteDir(dir);
		gi.multicast(self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict(self);
}

void
fire_flechette(edict_t *self, vec3_t start, vec3_t dir, int damage,
		int speed, int kick)
{
	edict_t *flechette;

	if (!self)
	{
		return;
	}

	VectorNormalize(dir);

	flechette = G_Spawn();
	VectorCopy(start, flechette->s.origin);
	VectorCopy(start, flechette->s.old_origin);
	vectoangles2(dir, flechette->s.angles);

	VectorScale(dir, speed, flechette->velocity);
	flechette->movetype = MOVETYPE_FLYMISSILE;
	flechette->clipmask = MASK_SHOT;
	flechette->solid = SOLID_BBOX;
	flechette->s.renderfx = RF_FULLBRIGHT;
	VectorClear(flechette->mins);
	VectorClear(flechette->maxs);

	flechette->s.modelindex = gi.modelindex("models/proj/flechette/tris.md2");

	flechette->owner = self;
	flechette->touch = flechette_touch;
	flechette->nextthink = level.time + 8000 / speed;
	flechette->think = G_FreeEdict;
	flechette->dmg = damage;
	flechette->dmg_radius = kick;

	gi.linkentity(flechette);

	if (self->client)
	{
		check_dodge(self, flechette->s.origin, dir, speed);
	}
}

void
Prox_Explode(edict_t *ent)
{
	vec3_t origin;
	edict_t *owner;

	if (!ent)
	{
		return;
	}

	/* free the trigger field */
	if (ent->teamchain && (ent->teamchain->owner == ent))
	{
		G_FreeEdict(ent->teamchain);
	}

	owner = ent;

	if (ent->teammaster)
	{
		owner = ent->teammaster;
		PlayerNoise(owner, ent->s.origin, PNOISE_IMPACT);
	}

	/* play quad sound if appopriate */
	if (ent->dmg > PROX_DAMAGE)
	{
		gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);
	}

	ent->takedamage = DAMAGE_NO;
	T_RadiusDamage(ent, owner, ent->dmg, ent, PROX_DAMAGE_RADIUS, MOD_PROX);

	VectorMA(ent->s.origin, -0.02, ent->velocity, origin);
	gi.WriteByte(svc_temp_entity);

	if (ent->groundentity)
	{
		gi.WriteByte(TE_GRENADE_EXPLOSION);
	}
	else
	{
		gi.WriteByte(TE_ROCKET_EXPLOSION);
	}

	gi.WritePosition(origin);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	G_FreeEdict(ent);
}

void
prox_die(edict_t *self, edict_t *inflictor, edict_t *attacker /* unused */,
		int damage, vec3_t point)
{
	if (!self || !inflictor)
	{
		return;
	}

	if (strcmp(inflictor->classname, "prox"))
	{
		self->takedamage = DAMAGE_NO;
		Prox_Explode(self);
	}
	else
	{
		self->takedamage = DAMAGE_NO;
		self->think = Prox_Explode;
		self->nextthink = level.time + FRAMETIME;
	}
}

void
Prox_Field_Touch(edict_t *ent, edict_t *other, cplane_t *plane /* unused */,
		csurface_t *surf /* unused */)
{
	edict_t *prox;

	if (!ent || !other)
	{
		return;
	}

	if (!(other->svflags & SVF_MONSTER) && !other->client)
	{
		return;
	}

	/* trigger the prox mine if it's still there, and still mine. */
	prox = ent->owner;

	if (other == prox) /* don't set self off */
	{
		return;
	}

	if (prox->think == Prox_Explode) /* we're set to blow! */
	{
		return;
	}

	if (prox->teamchain == ent)
	{
		gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/proxwarn.wav"), 1, ATTN_NORM, 0);
		prox->think = Prox_Explode;
		prox->nextthink = level.time + PROX_TIME_DELAY;
		return;
	}

	ent->solid = SOLID_NOT;
	G_FreeEdict(ent);
}

void
prox_seek(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (level.time > ent->wait)
	{
		Prox_Explode(ent);
	}
	else
	{
		ent->s.frame++;

		if (ent->s.frame > 13)
		{
			ent->s.frame = 9;
		}

		ent->think = prox_seek;
		ent->nextthink = level.time + 0.1;
	}
}

void
prox_open(edict_t *ent)
{
	edict_t *search;

	if (!ent)
	{
		return;
	}

	search = NULL;

	if (ent->s.frame == 9) /* end of opening animation */
	{
		/* set the owner to NULL so the owner can shoot it, etc.
		   needs to be done here so the owner doesn't get stuck on
		   it while it's opening if fired at point blank wall */
		ent->s.sound = 0;
		ent->owner = NULL;

		if (ent->teamchain)
		{
			ent->teamchain->touch = Prox_Field_Touch;
		}

		while ((search = findradius(search, ent->s.origin, PROX_DAMAGE_RADIUS + 10)) != NULL)
		{
			if (!search->classname) /* tag token and other weird shit */
			{
				continue;
			}

			/* if it's a monster or player with health > 0
			   or it's a player start point and we can see it
			   blow up */
			if (((((search->svflags & SVF_MONSTER) ||
				   (search->client)) && (search->health > 0)) ||
				   ((deathmatch->value) &&((!strcmp(search->classname, "info_player_deathmatch")) ||
				   (!strcmp(search->classname, "info_player_start")) ||
				   (!strcmp(search->classname, "info_player_coop")) ||
				   (!strcmp(search->classname, "misc_teleporter_dest"))))) &&
				   (visible(search, ent)))
			{
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/proxwarn.wav"), 1, ATTN_NORM, 0);
				Prox_Explode(ent);
				return;
			}
		}

		if (strong_mines && (strong_mines->value))
		{
			ent->wait = level.time + PROX_TIME_TO_LIVE;
		}
		else
		{
			switch (ent->dmg / PROX_DAMAGE)
			{
				case 1:
					ent->wait = level.time + PROX_TIME_TO_LIVE;
					break;
				case 2:
					ent->wait = level.time + 30;
					break;
				case 4:
					ent->wait = level.time + 15;
					break;
				case 8:
					ent->wait = level.time + 10;
					break;
				default:
					ent->wait = level.time + PROX_TIME_TO_LIVE;
					break;
			}
		}

		ent->think = prox_seek;
		ent->nextthink = level.time + 0.2;
	}
	else
	{
		if (ent->s.frame == 0)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/proxopen.wav"), 1, ATTN_NORM, 0);
		}

		ent->s.frame++;
		ent->think = prox_open;
		ent->nextthink = level.time + 0.05;
	}
}

void
prox_land(edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	edict_t *field;
	vec3_t dir;
	vec3_t forward, right, up;
	int movetype = MOVETYPE_NONE;
	int stick_ok = 0;
	vec3_t land_point;

	if (!ent || !other || !plane || !surf)
	{
		return;
	}

	/* must turn off owner so owner can shoot it and set it off
	   moved to prox_open so owner can get away from it if fired
	   at pointblank range into wall */
	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(ent);
		return;
	}

	VectorMA(ent->s.origin, -10.0, plane->normal, land_point);

	if (gi.pointcontents(land_point) & (CONTENTS_SLIME | CONTENTS_LAVA))
	{
		Prox_Explode(ent);
		return;
	}

	if ((other->svflags & SVF_MONSTER) || other->client ||
		(other->svflags & SVF_DAMAGEABLE))
	{
		if (other != ent->teammaster)
		{
			Prox_Explode(ent);
		}

		return;
	}

#define STOP_EPSILON 0.1

	else if (other != world)
	{
		/* Here we need to check to see if we can stop on this entity. */
		vec3_t out;
		float backoff, change;
		int i;

		if ((other->movetype == MOVETYPE_PUSH) && (plane->normal[2] > 0.7))
		{
			stick_ok = 1;
		}
		else
		{
			stick_ok = 0;
		}

		backoff = DotProduct(ent->velocity, plane->normal) * 1.5;

		for (i = 0; i < 3; i++)
		{
			change = plane->normal[i] * backoff;
			out[i] = ent->velocity[i] - change;

			if ((out[i] > -STOP_EPSILON) && (out[i] < STOP_EPSILON))
			{
				out[i] = 0;
			}
		}

		if (out[2] > 60)
		{
			return;
		}

		movetype = MOVETYPE_BOUNCE;

		/* if we're here, we're going to stop on an entity */
		if (stick_ok)
		{
			/* it's a happy entity */
			VectorCopy(vec3_origin, ent->velocity);
			VectorCopy(vec3_origin, ent->avelocity);
		}
		else /* no-stick.  teflon time */
		{
			if (plane->normal[2] > 0.7)
			{
				Prox_Explode(ent);
				return;
			}

			return;
		}
	}
	else if (other->s.modelindex != 1)
	{
		return;
	}

	vectoangles2(plane->normal, dir);
	AngleVectors(dir, forward, right, up);

	if (gi.pointcontents(ent->s.origin) & (CONTENTS_LAVA | CONTENTS_SLIME))
	{
		Prox_Explode(ent);
		return;
	}

	field = G_Spawn();

	VectorCopy(ent->s.origin, field->s.origin);
	VectorClear(field->velocity);
	VectorClear(field->avelocity);
	VectorSet(field->mins, -PROX_BOUND_SIZE, -PROX_BOUND_SIZE, -PROX_BOUND_SIZE);
	VectorSet(field->maxs, PROX_BOUND_SIZE, PROX_BOUND_SIZE, PROX_BOUND_SIZE);
	field->movetype = MOVETYPE_NONE;
	field->solid = SOLID_TRIGGER;
	field->owner = ent;
	field->classname = "prox_field";
	field->teammaster = ent;
	gi.linkentity(field);

	VectorClear(ent->velocity);
	VectorClear(ent->avelocity);

	/* rotate to vertical */
	dir[PITCH] = dir[PITCH] + 90;
	VectorCopy(dir, ent->s.angles);
	ent->takedamage = DAMAGE_AIM;
	ent->movetype = movetype; /* either bounce or none, depending on whether we stuck to something */
	ent->die = prox_die;
	ent->teamchain = field;
	ent->health = PROX_HEALTH;
	ent->nextthink = level.time + 0.05;
	ent->think = prox_open;
	ent->touch = NULL;
	ent->solid = SOLID_BBOX;

	/* record who we're attached to */
	gi.linkentity(ent);
}

void
fire_prox(edict_t *self, vec3_t start, vec3_t aimdir, int damage_multiplier, int speed)
{
	edict_t *prox;
	vec3_t dir;
	vec3_t forward, right, up;

	if (!self)
	{
		return;
	}

	vectoangles2(aimdir, dir);
	AngleVectors(dir, forward, right, up);

	prox = G_Spawn();
	VectorCopy(start, prox->s.origin);
	VectorScale(aimdir, speed, prox->velocity);
	VectorMA(prox->velocity, 200 + crandom() * 10.0, up, prox->velocity);
	VectorMA(prox->velocity, crandom() * 10.0, right, prox->velocity);
	VectorCopy(dir, prox->s.angles);
	prox->s.angles[PITCH] -= 90;
	prox->movetype = MOVETYPE_BOUNCE;
	prox->solid = SOLID_BBOX;
	prox->s.effects |= EF_GRENADE;
	prox->clipmask = MASK_SHOT | CONTENTS_LAVA | CONTENTS_SLIME;
	prox->s.renderfx |= RF_IR_VISIBLE;
	VectorSet(prox->mins, -6, -6, -6);
	VectorSet(prox->maxs, 6, 6, 6);
	prox->s.modelindex = gi.modelindex("models/weapons/g_prox/tris.md2");
	prox->owner = self;
	prox->teammaster = self;
	prox->touch = prox_land;
	prox->think = Prox_Explode;
	prox->dmg = PROX_DAMAGE * damage_multiplier;
	prox->classname = "prox";
	prox->svflags |= SVF_DAMAGEABLE;
	prox->flags |= FL_MECHANICAL;

	switch (damage_multiplier)
	{
		case 1:
			prox->nextthink = level.time + PROX_TIME_TO_LIVE;
			break;
		case 2:
			prox->nextthink = level.time + 30;
			break;
		case 4:
			prox->nextthink = level.time + 15;
			break;
		case 8:
			prox->nextthink = level.time + 10;
			break;
		default:
			prox->nextthink = level.time + PROX_TIME_TO_LIVE;
			break;
	}

	gi.linkentity(prox);
}

void
fire_player_melee(edict_t *self, vec3_t start, vec3_t aim, int reach,
		int damage, int kick, int quiet, int mod)
{
	vec3_t forward, right, up;
	vec3_t v;
	vec3_t point;
	trace_t tr;

	if (!self)
	{
		return;
	}

	vectoangles2(aim, v);
	AngleVectors(v, forward, right, up);
	VectorNormalize(forward);
	VectorMA(start, reach, forward, point);

	/* see if the hit connects */
	tr = gi.trace(start, NULL, NULL, point, self, MASK_SHOT);

	if (tr.fraction == 1.0)
	{
		if (!quiet)
		{
			gi.sound(self, CHAN_WEAPON, gi.soundindex("weapons/swish.wav"), 1, ATTN_NORM, 0);
		}

		return;
	}

	if ((tr.ent->takedamage == DAMAGE_YES) ||
		(tr.ent->takedamage == DAMAGE_AIM))
	{
		/* pull the player forward if you do damage */
		VectorMA(self->velocity, 75, forward, self->velocity);
		VectorMA(self->velocity, 75, up, self->velocity);

		/* do the damage */
		if (mod == MOD_CHAINFIST)
		{
			T_Damage(tr.ent, self, self, vec3_origin, tr.ent->s.origin, vec3_origin,
					damage, kick / 2, DAMAGE_DESTROY_ARMOR | DAMAGE_NO_KNOCKBACK, mod);
		}
		else
		{
			T_Damage(tr.ent, self, self, vec3_origin, tr.ent->s.origin, vec3_origin,
					damage, kick / 2, DAMAGE_NO_KNOCKBACK, mod);
		}

		if (!quiet)
		{
			gi.sound(self, CHAN_WEAPON, gi.soundindex("weapons/meatht.wav"), 1, ATTN_NORM, 0);
		}
	}
	else
	{
		if (!quiet)
		{
			gi.sound(self, CHAN_WEAPON, gi.soundindex("weapons/tink1.wav"), 1, ATTN_NORM, 0);
		}

		VectorScale(tr.plane.normal, 256, point);
		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_GUNSHOT);
		gi.WritePosition(tr.endpos);
		gi.WriteDir(point);
		gi.multicast(tr.endpos, MULTICAST_PVS);
	}
}

void
Nuke_Quake(edict_t *self)
{
	int i;
	edict_t *e;

	if (!self)
	{
		return;
	}

	if (self->last_move_time < level.time)
	{
		gi.positioned_sound(self->s.origin, self, CHAN_AUTO, self->noise_index,
				0.75, ATTN_NONE, 0);
		self->last_move_time = level.time + 0.5;
	}

	for (i = 1, e = g_edicts + i; i < globals.num_edicts; i++, e++)
	{
		if (!e->inuse)
		{
			continue;
		}

		if (!e->client)
		{
			continue;
		}

		if (!e->groundentity)
		{
			continue;
		}

		e->groundentity = NULL;
		e->velocity[0] += crandom() * 150;
		e->velocity[1] += crandom() * 150;
		e->velocity[2] = self->speed * (100.0 / e->mass);
	}

	if (level.time < self->timestamp)
	{
		self->nextthink = level.time + FRAMETIME;
	}
	else
	{
		G_FreeEdict(self);
	}
}

void
Nuke_Explode(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (ent->teammaster->client)
	{
		PlayerNoise(ent->teammaster, ent->s.origin, PNOISE_IMPACT);
	}

	T_RadiusNukeDamage(ent, ent->teammaster, ent->dmg,
			ent, ent->dmg_radius, MOD_NUKE);

	if (ent->dmg > NUKE_DAMAGE)
	{
		gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);
	}

	gi.sound(ent, CHAN_NO_PHS_ADD + CHAN_VOICE, gi.soundindex("weapons/grenlx1a.wav"), 1, ATTN_NONE, 0);

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_EXPLOSION1_BIG);
	gi.WritePosition(ent->s.origin);
	gi.multicast(ent->s.origin, MULTICAST_PVS);

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_NUKEBLAST);
	gi.WritePosition(ent->s.origin);
	gi.multicast(ent->s.origin, MULTICAST_ALL);

	/* become a quake */
	ent->svflags |= SVF_NOCLIENT;
	ent->noise_index = gi.soundindex("world/rumble.wav");
	ent->think = Nuke_Quake;
	ent->speed = NUKE_QUAKE_STRENGTH;
	ent->timestamp = level.time + NUKE_QUAKE_TIME;
	ent->nextthink = level.time + FRAMETIME;
	ent->last_move_time = 0;
}

void
nuke_die(edict_t *self, edict_t *inflictor /* unused */,
		edict_t *attacker, int damage, vec3_t point)
{
	if (!self || !attacker)
	{
		return;
	}

	self->takedamage = DAMAGE_NO;

	if ((attacker) && !(strcmp(attacker->classname, "nuke")))
	{
		G_FreeEdict(self);
		return;
	}

	Nuke_Explode(self);
}

void
Nuke_Think(edict_t *ent)
{
	float attenuation, default_atten = 1.8;
	int damage_multiplier, muzzleflash;

	if (!ent)
	{
		return;
	}

	damage_multiplier = ent->dmg / NUKE_DAMAGE;

	switch (damage_multiplier)
	{
		case 1:
			attenuation = default_atten / 1.4;
			muzzleflash = MZ_NUKE1;
			break;
		case 2:
			attenuation = default_atten / 2.0;
			muzzleflash = MZ_NUKE2;
			break;
		case 4:
			attenuation = default_atten / 3.0;
			muzzleflash = MZ_NUKE4;
			break;
		case 8:
			attenuation = default_atten / 5.0;
			muzzleflash = MZ_NUKE8;
			break;
		default:
			attenuation = default_atten;
			muzzleflash = MZ_NUKE1;
			break;
	}

	if (ent->wait < level.time)
	{
		Nuke_Explode(ent);
	}
	else if (level.time >= (ent->wait - NUKE_TIME_TO_LIVE))
	{
		ent->s.frame++;

		if (ent->s.frame > 11)
		{
			ent->s.frame = 6;
		}

		if (gi.pointcontents(ent->s.origin) & (CONTENTS_SLIME | CONTENTS_LAVA))
		{
			Nuke_Explode(ent);
			return;
		}

		ent->think = Nuke_Think;
		ent->nextthink = level.time + 0.1;
		ent->health = 1;
		ent->owner = NULL;

		gi.WriteByte(svc_muzzleflash);
		gi.WriteShort(ent - g_edicts);
		gi.WriteByte(muzzleflash);
		gi.multicast(ent->s.origin, MULTICAST_PVS);

		if (ent->timestamp <= level.time)
		{
			if ((ent->wait - level.time) <= (NUKE_TIME_TO_LIVE / 2.0))
			{
				gi.sound(ent, CHAN_NO_PHS_ADD + CHAN_VOICE, gi.soundindex("weapons/nukewarn2.wav"), 1, attenuation, 0);
				ent->timestamp = level.time + 0.3;
			}
			else
			{
				gi.sound(ent, CHAN_NO_PHS_ADD + CHAN_VOICE, gi.soundindex("weapons/nukewarn2.wav"), 1, attenuation, 0);
				ent->timestamp = level.time + 0.5;
			}
		}
	}
	else
	{
		if (ent->timestamp <= level.time)
		{
			gi.sound(ent, CHAN_NO_PHS_ADD + CHAN_VOICE, gi.soundindex("weapons/nukewarn2.wav"), 1, attenuation, 0);
			ent->timestamp = level.time + 1.0;
		}

		ent->nextthink = level.time + FRAMETIME;
	}
}

void
nuke_bounce(edict_t *ent, edict_t *other /* unused */, cplane_t *plane /* unused */,
	   	csurface_t *surf /* unused */)
{
	if (!ent)
	{
		return;
	}

	if (random() > 0.5)
	{
		gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/hgrenb1a.wav"), 1, ATTN_NORM, 0);
	}
	else
	{
		gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/hgrenb2a.wav"), 1, ATTN_NORM, 0);
	}
}

void
fire_nuke(edict_t *self, vec3_t start, vec3_t aimdir, int speed)
{
	edict_t *nuke;
	vec3_t dir;
	vec3_t forward, right, up;
	int damage_modifier;

	if (!self)
	{
		return;
	}

	damage_modifier = (int)P_DamageModifier(self);

	vectoangles2(aimdir, dir);
	AngleVectors(dir, forward, right, up);

	nuke = G_Spawn();
	VectorCopy(start, nuke->s.origin);
	VectorScale(aimdir, speed, nuke->velocity);

	VectorMA(nuke->velocity, 200 + crandom() * 10.0, up, nuke->velocity);
	VectorMA(nuke->velocity, crandom() * 10.0, right, nuke->velocity);
	VectorClear(nuke->avelocity);
	VectorClear(nuke->s.angles);
	nuke->movetype = MOVETYPE_BOUNCE;
	nuke->clipmask = MASK_SHOT;
	nuke->solid = SOLID_BBOX;
	nuke->s.effects |= EF_GRENADE;
	nuke->s.renderfx |= RF_IR_VISIBLE;
	VectorSet(nuke->mins, -8, -8, 0);
	VectorSet(nuke->maxs, 8, 8, 16);
	nuke->s.modelindex = gi.modelindex("models/weapons/g_nuke/tris.md2");
	nuke->owner = self;
	nuke->teammaster = self;
	nuke->nextthink = level.time + FRAMETIME;
	nuke->wait = level.time + NUKE_DELAY + NUKE_TIME_TO_LIVE;
	nuke->think = Nuke_Think;
	nuke->touch = nuke_bounce;

	nuke->health = 10000;
	nuke->takedamage = DAMAGE_YES;
	nuke->svflags |= SVF_DAMAGEABLE;
	nuke->dmg = NUKE_DAMAGE * damage_modifier;

	if (damage_modifier == 1)
	{
		nuke->dmg_radius = NUKE_RADIUS;
	}
	else
	{
		nuke->dmg_radius = NUKE_RADIUS + NUKE_RADIUS * (0.25 * (float)damage_modifier);
	}

	nuke->classname = "nuke";
	nuke->die = nuke_die;

	gi.linkentity(nuke);
}
void
tesla_remove(edict_t *self)
{
	edict_t *cur, *next;

	if (!self)
	{
		return;
	}

	self->takedamage = DAMAGE_NO;

	if (self->teamchain)
	{
		cur = self->teamchain;

		while (cur)
		{
			next = cur->teamchain;
			G_FreeEdict(cur);
			cur = next;
		}
	}
	else if (self->air_finished)
	{
		gi.dprintf("tesla without a field!\n");
	}

	self->owner = self->teammaster; /* Going away, set the owner correctly. */
	self->enemy = NULL;

	/* play quad sound if quadded and an underwater explosion */
	if ((self->dmg_radius) && (self->dmg > (TESLA_DAMAGE * TESLA_EXPLOSION_DAMAGE_MULT)))
	{
		gi.sound(self, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);
	}

	Grenade_Explode(self);
}

void
tesla_die(edict_t *self, edict_t *inflictor /* unused */, edict_t *attacker /* unused */,
		int damage /* unused */, vec3_t point /* unused */)
{
	if (!self)
	{
		return;
	}

	tesla_remove(self);
}

void
tesla_blow(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->dmg = self->dmg * TESLA_EXPLOSION_DAMAGE_MULT;
	self->dmg_radius = TESLA_EXPLOSION_RADIUS;
	tesla_remove(self);
}

void
tesla_zap(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
}

void
tesla_think_active(edict_t *self)
{
	int i, num;
	edict_t *touch[MAX_EDICTS], *hit;
	vec3_t dir, start;
	trace_t tr;

	if (!self)
	{
		return;
	}

	if (level.time > self->air_finished)
	{
		tesla_remove(self);
		return;
	}

	VectorCopy(self->s.origin, start);
	start[2] += 16;

	num = gi.BoxEdicts(self->teamchain->absmin, self->teamchain->absmax,
			touch, MAX_EDICTS, AREA_SOLID);

	for (i = 0; i < num; i++)
	{
		/* if the tesla died while zapping things, stop zapping. */
		if (!(self->inuse))
		{
			break;
		}

		hit = touch[i];

		if (!hit->inuse)
		{
			continue;
		}

		if (hit == self)
		{
			continue;
		}

		if (hit->health < 1)
		{
			continue;
		}

		/* don't hit clients in single-player or coop */
		if (hit->client)
		{
			if (coop->value || !deathmatch->value)
			{
				continue;
			}
		}

		if (!(hit->svflags & (SVF_MONSTER | SVF_DAMAGEABLE)) && !hit->client)
		{
			continue;
		}

		tr = gi.trace(start, vec3_origin, vec3_origin, hit->s.origin,
				self, MASK_SHOT);

		if ((tr.fraction == 1) || (tr.ent == hit))
		{
			VectorSubtract(hit->s.origin, start, dir);

			/* play quad sound if it's above the "normal" damage */
			if (self->dmg > TESLA_DAMAGE)
			{
				gi.sound(self, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);
			}

			/*  don't do knockback to walking monsters */
			if ((hit->svflags & SVF_MONSTER) &&
				!(hit->flags & (FL_FLY | FL_SWIM)))
			{
				T_Damage(hit, self, self->teammaster, dir, tr.endpos,
						tr.plane.normal, self->dmg, 0, 0, MOD_TESLA);
			}
			else
			{
				T_Damage(hit, self, self->teammaster, dir, tr.endpos, tr.plane.normal,
						self->dmg, TESLA_KNOCKBACK, 0, MOD_TESLA);
			}

			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_LIGHTNING);
			gi.WriteShort(hit - g_edicts); /* destination entity */
			gi.WriteShort(self - g_edicts); /* source entity */
			gi.WritePosition(tr.endpos);
			gi.WritePosition(start);
			gi.multicast(start, MULTICAST_PVS);
		}
	}

	if (self->inuse)
	{
		self->think = tesla_think_active;
		self->nextthink = level.time + FRAMETIME;
	}
}

void
tesla_activate(edict_t *self)
{
	edict_t *trigger;
	edict_t *search;

	if (!self)
	{
		return;
	}

	if (gi.pointcontents(self->s.origin) & (CONTENTS_SLIME | CONTENTS_LAVA | CONTENTS_WATER))
	{
		tesla_blow(self);
		return;
	}

	/* only check for spawn points in deathmatch */
	if (deathmatch->value)
	{
		search = NULL;

		while ((search = findradius(search, self->s.origin, 1.5 * TESLA_DAMAGE_RADIUS)) != NULL)
		{
			if (search->classname)
			{
				if (((!strcmp(search->classname, "info_player_deathmatch")) ||
					 (!strcmp(search->classname, "info_player_start")) ||
					 (!strcmp(search->classname, "info_player_coop")) ||
					 (!strcmp(search->classname, "misc_teleporter_dest"))) &&
						(visible(search, self)))
				{
					tesla_remove(self);
					return;
				}
			}
		}
	}

	trigger = G_Spawn();
	VectorCopy(self->s.origin, trigger->s.origin);
	VectorSet(trigger->mins, -TESLA_DAMAGE_RADIUS, -TESLA_DAMAGE_RADIUS, self->mins[2]);
	VectorSet(trigger->maxs, TESLA_DAMAGE_RADIUS, TESLA_DAMAGE_RADIUS, TESLA_DAMAGE_RADIUS);
	trigger->movetype = MOVETYPE_NONE;
	trigger->solid = SOLID_TRIGGER;
	trigger->owner = self;
	trigger->touch = tesla_zap;
	trigger->classname = "tesla trigger";

	/* doesn't need to be marked as a teamslave since the move code for bounce looks for teamchains */
	gi.linkentity(trigger);

	VectorClear(self->s.angles);

	/* clear the owner if in deathmatch */
	if (deathmatch->value)
	{
		self->owner = NULL;
	}

	self->teamchain = trigger;
	self->think = tesla_think_active;
	self->nextthink = level.time + FRAMETIME;
	self->air_finished = level.time + TESLA_TIME_TO_LIVE;
}

void
tesla_think(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (gi.pointcontents(ent->s.origin) & (CONTENTS_SLIME | CONTENTS_LAVA))
	{
		tesla_remove(ent);
		return;
	}

	VectorClear(ent->s.angles);

	if (!(ent->s.frame))
	{
		gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/teslaopen.wav"), 1, ATTN_NORM, 0);
	}

	ent->s.frame++;

	if (ent->s.frame > 14)
	{
		ent->s.frame = 14;
		ent->think = tesla_activate;
		ent->nextthink = level.time + 0.1;
	}
	else
	{
		if (ent->s.frame > 9)
		{
			if (ent->s.frame == 10)
			{
				if (ent->owner && ent->owner->client)
				{
					PlayerNoise(ent->owner, ent->s.origin, PNOISE_WEAPON);      /* PGM */
				}

				ent->s.skinnum = 1;
			}
			else if (ent->s.frame == 12)
			{
				ent->s.skinnum = 2;
			}
			else if (ent->s.frame == 14)
			{
				ent->s.skinnum = 3;
			}
		}

		ent->think = tesla_think;
		ent->nextthink = level.time + 0.1;
	}
}

void
tesla_lava(edict_t *ent, edict_t *other /* unused */, cplane_t *plane, csurface_t *surf /* unused */)
{
	vec3_t land_point;

	if (!ent || !plane)
	{
		return;
	}

	VectorMA(ent->s.origin, -20.0, plane->normal, land_point);

	if (gi.pointcontents(land_point) & (CONTENTS_SLIME | CONTENTS_LAVA))
	{
		tesla_blow(ent);
		return;
	}

	if (random() > 0.5)
	{
		gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/hgrenb1a.wav"), 1, ATTN_NORM, 0);
	}
	else
	{
		gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/hgrenb2a.wav"), 1, ATTN_NORM, 0);
	}
}

void
fire_tesla(edict_t *self, vec3_t start, vec3_t aimdir,
		int damage_multiplier, int speed)
{
	edict_t *tesla;
	vec3_t dir;
	vec3_t forward, right, up;

	if (!self)
	{
		return;
	}

	vectoangles2(aimdir, dir);
	AngleVectors(dir, forward, right, up);

	tesla = G_Spawn();
	VectorCopy(start, tesla->s.origin);
	VectorScale(aimdir, speed, tesla->velocity);
	VectorMA(tesla->velocity, 200 + crandom() * 10.0, up, tesla->velocity);
	VectorMA(tesla->velocity, crandom() * 10.0, right, tesla->velocity);
	VectorClear(tesla->s.angles);
	tesla->movetype = MOVETYPE_BOUNCE;
	tesla->solid = SOLID_BBOX;
	tesla->s.effects |= EF_GRENADE;
	tesla->s.renderfx |= RF_IR_VISIBLE;
	VectorSet(tesla->mins, -12, -12, 0);
	VectorSet(tesla->maxs, 12, 12, 20);
	tesla->s.modelindex = gi.modelindex("models/weapons/g_tesla/tris.md2");

	tesla->owner = self;
	tesla->teammaster = self;

	tesla->wait = level.time + TESLA_TIME_TO_LIVE;
	tesla->think = tesla_think;
	tesla->nextthink = level.time + TESLA_ACTIVATE_TIME;

	/* blow up on contact with lava & slime code */
	tesla->touch = tesla_lava;

	if (deathmatch->value)
	{
		tesla->health = 20;
	}
	else
	{
		tesla->health = 30;
	}

	tesla->takedamage = DAMAGE_YES;
	tesla->die = tesla_die;
	tesla->dmg = TESLA_DAMAGE * damage_multiplier;
	tesla->classname = "tesla";
	tesla->svflags |= SVF_DAMAGEABLE;
	tesla->clipmask = MASK_SHOT | CONTENTS_SLIME | CONTENTS_LAVA;
	tesla->flags |= FL_MECHANICAL;

	gi.linkentity(tesla);
}

void
fire_beams(edict_t *self, vec3_t start, vec3_t aimdir, vec3_t offset,
		int damage, int kick, int te_beam, int te_impact, int mod)
{
	trace_t tr;
	vec3_t dir;
	vec3_t forward, right, up;
	vec3_t end;
	vec3_t water_start, endpoint;
	qboolean water = false, underwater = false;
	int content_mask = MASK_SHOT | MASK_WATER;
	vec3_t beam_endpt;

	if (!self)
	{
		return;
	}

	vectoangles2(aimdir, dir);
	AngleVectors(dir, forward, right, up);

	VectorMA(start, 8192, forward, end);

	if (gi.pointcontents(start) & MASK_WATER)
	{
		underwater = true;
		VectorCopy(start, water_start);
		content_mask &= ~MASK_WATER;
	}

	tr = gi.trace(start, NULL, NULL, end, self, content_mask);

	/* see if we hit water */
	if (tr.contents & MASK_WATER)
	{
		water = true;
		VectorCopy(tr.endpos, water_start);

		if (!VectorCompare(start, tr.endpos))
		{
			gi.WriteByte(svc_temp_entity);
			gi.WriteByte(TE_HEATBEAM_SPARKS);
			gi.WritePosition(water_start);
			gi.WriteDir(tr.plane.normal);
			gi.multicast(tr.endpos, MULTICAST_PVS);
		}

		/* re-trace ignoring water this time */
		tr = gi.trace(water_start, NULL, NULL, end, self, MASK_SHOT);
	}

	VectorCopy(tr.endpos, endpoint);

	/* halve the damage if target underwater */
	if (water)
	{
		damage = damage / 2;
	}

	/* send gun puff / flash */
	if (!((tr.surface) && (tr.surface->flags & SURF_SKY)))
	{
		if (tr.fraction < 1.0)
		{
			if (tr.ent->takedamage)
			{
				T_Damage(tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal,
						damage, kick, DAMAGE_ENERGY, mod);
			}
			else
			{
				if ((!water) && (strncmp(tr.surface->name, "sky", 3)))
				{
					/* This is the truncated steam entry - uses 1+1+2 extra bytes of data */
					gi.WriteByte(svc_temp_entity);
					gi.WriteByte(TE_HEATBEAM_STEAM);
					gi.WritePosition(tr.endpos);
					gi.WriteDir(tr.plane.normal);
					gi.multicast(tr.endpos, MULTICAST_PVS);

					if (self->client)
					{
						PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
					}
				}
			}
		}
	}

	/* if went through water, determine where the end and make a bubble trail */
	if ((water) || (underwater))
	{
		vec3_t pos;

		VectorSubtract(tr.endpos, water_start, dir);
		VectorNormalize(dir);
		VectorMA(tr.endpos, -2, dir, pos);

		if (gi.pointcontents(pos) & MASK_WATER)
		{
			VectorCopy(pos, tr.endpos);
		}
		else
		{
			tr = gi.trace(pos, NULL, NULL, water_start, tr.ent, MASK_WATER);
		}

		VectorAdd(water_start, tr.endpos, pos);
		VectorScale(pos, 0.5, pos);

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BUBBLETRAIL2);
		gi.WritePosition(water_start);
		gi.WritePosition(tr.endpos);
		gi.multicast(pos, MULTICAST_PVS);
	}

	if ((!underwater) && (!water))
	{
		VectorCopy(tr.endpos, beam_endpt);
	}
	else
	{
		VectorCopy(endpoint, beam_endpt);
	}

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(te_beam);
	gi.WriteShort(self - g_edicts);
	gi.WritePosition(start);
	gi.WritePosition(beam_endpt);
	gi.multicast(self->s.origin, MULTICAST_ALL);
}

void
fire_heat(edict_t *self, vec3_t start, vec3_t aimdir, vec3_t offset,
		int damage, int kick, qboolean monster)
{
	if (!self)
	{
		return;
	}

	if (monster)
	{
		fire_beams(self, start, aimdir, offset, damage, kick,
				TE_MONSTER_HEATBEAM, TE_HEATBEAM_SPARKS, MOD_HEATBEAM);
	}
	else
	{
		fire_beams(self, start, aimdir, offset, damage,
				kick, TE_HEATBEAM, TE_HEATBEAM_SPARKS, MOD_HEATBEAM);
	}
}

/*
 * Fires a single green blaster bolt.  Used by monsters, generally.
 */
void
blaster2_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	int mod;
	int damagestat;

	if (!self || !other || !plane || !surf)
	{
		return;
	}

	if (other == self->owner)
	{
		return;
	}

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->owner && self->owner->client)
	{
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);
	}

	if (other->takedamage)
	{
		/* the only time players will be firing blaster2
		   bolts will be from the defender sphere. */
		if (self->owner->client)
		{
			mod = MOD_DEFENDER_SPHERE;
		}
		else
		{
			mod = MOD_BLASTER2;
		}

		if (self->owner)
		{
			damagestat = self->owner->takedamage;
			self->owner->takedamage = DAMAGE_NO;

			if (self->dmg >= 5)
			{
				T_RadiusDamage(self, self->owner, self->dmg * 3, other,
						self->dmg_radius, 0);
			}

			T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal,
					self->dmg, 1, DAMAGE_ENERGY, mod);
			self->owner->takedamage = damagestat;
		}
		else
		{
			if (self->dmg >= 5)
			{
				T_RadiusDamage(self, self->owner, self->dmg * 3, other,
						self->dmg_radius, 0);
			}

			T_Damage(other, self, self->owner, self->velocity, self->s.origin,
					plane->normal, self->dmg, 1, DAMAGE_ENERGY, mod);
		}
	}
	else
	{
		/* yeowch this will get expensive */
		if (self->dmg >= 5)
		{
			T_RadiusDamage(self, self->owner, self->dmg * 3, self->owner,
					self->dmg_radius, 0);
		}

		gi.WriteByte(svc_temp_entity);
		gi.WriteByte(TE_BLASTER2);
		gi.WritePosition(self->s.origin);

		if (!plane)
		{
			gi.WriteDir(vec3_origin);
		}
		else
		{
			gi.WriteDir(plane->normal);
		}

		gi.multicast(self->s.origin, MULTICAST_PVS);
	}

	G_FreeEdict(self);
}

void
fire_blaster2(edict_t *self, vec3_t start, vec3_t dir, int damage,
		int speed, int effect, qboolean hyper)
{
	edict_t *bolt;
	trace_t tr;

	if (!self)
	{
		return;
	}

	VectorNormalize(dir);

	bolt = G_Spawn();
	VectorCopy(start, bolt->s.origin);
	VectorCopy(start, bolt->s.old_origin);
	vectoangles2(dir, bolt->s.angles);
	VectorScale(dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	bolt->s.effects |= effect;
	VectorClear(bolt->mins);
	VectorClear(bolt->maxs);

	if (effect)
	{
		bolt->s.effects |= EF_TRACKER;
	}

	bolt->dmg_radius = 128;
	bolt->s.modelindex = gi.modelindex("models/proj/laser2/tris.md2");
	bolt->touch = blaster2_touch;

	bolt->owner = self;
	bolt->nextthink = level.time + 2;
	bolt->think = G_FreeEdict;
	bolt->dmg = damage;
	bolt->classname = "bolt";
	gi.linkentity(bolt);

	if (self->client)
	{
		check_dodge(self, bolt->s.origin, dir, speed);
	}

	tr = gi.trace(self->s.origin, NULL, NULL, bolt->s.origin, bolt, MASK_SHOT);

	if (tr.fraction < 1.0)
	{
		VectorMA(bolt->s.origin, -10, dir, bolt->s.origin);
		bolt->touch(bolt, tr.ent, NULL, NULL);
	}
}

void
tracker_pain_daemon_think(edict_t *self)
{
	static vec3_t pain_normal = {0, 0, 1};
	int hurt;

	if (!self)
	{
		return;
	}

	if (!self->inuse)
	{
		return;
	}

	if ((level.time - self->timestamp) > TRACKER_DAMAGE_TIME)
	{
		if (!self->enemy->client)
		{
			self->enemy->s.effects &= ~EF_TRACKERTRAIL;
		}

		G_FreeEdict(self);
	}
	else
	{
		if (self->enemy->health > 0)
		{
			T_Damage(self->enemy, self, self->owner, vec3_origin, self->enemy->s.origin,
					pain_normal, self->dmg, 0, TRACKER_DAMAGE_FLAGS, MOD_TRACKER);

			/* if we kill the player, we'll be removed. */
			if (self->inuse)
			{
				/* if we killed a monster, gib them. */
				if (self->enemy->health < 1)
				{
					if (self->enemy->gib_health)
					{
						hurt = -self->enemy->gib_health;
					}
					else
					{
						hurt = 500;
					}

					T_Damage(self->enemy, self, self->owner, vec3_origin, self->enemy->s.origin,
							pain_normal, hurt, 0, TRACKER_DAMAGE_FLAGS, MOD_TRACKER);
				}

				if (self->enemy->client)
				{
					self->enemy->client->tracker_pain_framenum = level.framenum + 1;
				}
				else
				{
					self->enemy->s.effects |= EF_TRACKERTRAIL;
				}

				self->nextthink = level.time + FRAMETIME;
			}
		}
		else
		{
			if (!self->enemy->client)
			{
				self->enemy->s.effects &= ~EF_TRACKERTRAIL;
			}

			G_FreeEdict(self);
		}
	}
}

void
tracker_pain_daemon_spawn(edict_t *owner, edict_t *enemy, int damage)
{
	edict_t *daemon;

	if (!owner || !enemy)
	{
		return;
	}

	daemon = G_Spawn();
	daemon->classname = "pain daemon";
	daemon->think = tracker_pain_daemon_think;
	daemon->nextthink = level.time + FRAMETIME;
	daemon->timestamp = level.time;
	daemon->owner = owner;
	daemon->enemy = enemy;
	daemon->dmg = damage;
}

void
tracker_explode(edict_t *self, cplane_t *plane)
{
	vec3_t dir;

	if (!self)
	{
		return;
	}

	if (!plane)
	{
		VectorClear(dir);
	}
	else
	{
		VectorScale(plane->normal, 256, dir);
	}

	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_TRACKER_EXPLOSION);
	gi.WritePosition(self->s.origin);
	gi.multicast(self->s.origin, MULTICAST_PVS);

	G_FreeEdict(self);
}

void
tracker_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	float damagetime;

	if (!self || !other || !surf || !plane)
	{
		return;
	}

	if (other == self->owner)
	{
		return;
	}

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict(self);
		return;
	}

	if (self->client)
	{
		PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);
	}

	if (other->takedamage)
	{
		if ((other->svflags & SVF_MONSTER) || other->client)
		{
			if (other->health > 0) /* knockback only for living creatures */
			{
				T_Damage(other, self, self->owner, self->velocity, self->s.origin,
						plane->normal, 0, (self->dmg * 3), TRACKER_IMPACT_FLAGS,
						MOD_TRACKER);

				if (!(other->flags & (FL_FLY | FL_SWIM)))
				{
					other->velocity[2] += 140;
				}

				damagetime = ((float)self->dmg) * FRAMETIME;
				damagetime = damagetime / TRACKER_DAMAGE_TIME;

				tracker_pain_daemon_spawn(self->owner, other, (int)damagetime);
			}
			else /* lots of damage (almost autogib) for dead bodies */
			{
				T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal,
						self->dmg * 4, (self->dmg * 3), TRACKER_IMPACT_FLAGS, MOD_TRACKER);
			}
		}
		else /* full damage in one shot for inanimate objects */
		{
			T_Damage(other, self, self->owner, self->velocity, self->s.origin, plane->normal,
					self->dmg, (self->dmg * 3), TRACKER_IMPACT_FLAGS, MOD_TRACKER);
		}
	}

	tracker_explode(self, plane);
	return;
}

void
tracker_fly(edict_t *self)
{
	vec3_t dest;
	vec3_t dir;
	vec3_t center;

	if (!self)
	{
		return;
	}

	if ((!self->enemy) || (!self->enemy->inuse) || (self->enemy->health < 1))
	{
		tracker_explode(self, NULL);
		return;
	}

	/* try to hunt for center of enemy, if possible and not client */
	if (self->enemy->client)
	{
		VectorCopy(self->enemy->s.origin, dest);
		dest[2] += self->enemy->viewheight;
	}
	else if (VectorCompare(self->enemy->absmin, vec3_origin) ||
			 VectorCompare(self->enemy->absmax, vec3_origin))
	{
		VectorCopy(self->enemy->s.origin, dest);
	}
	else
	{
		VectorMA(vec3_origin, 0.5, self->enemy->absmin, center);
		VectorMA(center, 0.5, self->enemy->absmax, center);
		VectorCopy(center, dest);
	}

	VectorSubtract(dest, self->s.origin, dir);
	VectorNormalize(dir);
	vectoangles2(dir, self->s.angles);
	VectorScale(dir, self->speed, self->velocity);
	VectorCopy(dest, self->monsterinfo.saved_goal);

	self->nextthink = level.time + 0.1;
}

void
fire_tracker(edict_t *self, vec3_t start, vec3_t dir, int damage,
		int speed, edict_t *enemy)
{
	edict_t *bolt;
	trace_t tr;

	if (!self || !enemy)
	{
		return;
	}

	VectorNormalize(dir);

	bolt = G_Spawn();
	VectorCopy(start, bolt->s.origin);
	VectorCopy(start, bolt->s.old_origin);
	vectoangles2(dir, bolt->s.angles);
	VectorScale(dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	bolt->speed = speed;
	bolt->s.effects = EF_TRACKER;
	bolt->s.sound = gi.soundindex("weapons/disrupt.wav");
	VectorClear(bolt->mins);
	VectorClear(bolt->maxs);

	bolt->s.modelindex = gi.modelindex("models/proj/disintegrator/tris.md2");
	bolt->touch = tracker_touch;
	bolt->enemy = enemy;
	bolt->owner = self;
	bolt->dmg = damage;
	bolt->classname = "tracker";
	gi.linkentity(bolt);

	if (enemy)
	{
		bolt->nextthink = level.time + 0.1;
		bolt->think = tracker_fly;
	}
	else
	{
		bolt->nextthink = level.time + 10;
		bolt->think = G_FreeEdict;
	}

	if (self->client)
	{
		check_dodge(self, bolt->s.origin, dir, speed);
	}

	tr = gi.trace(self->s.origin, NULL, NULL, bolt->s.origin, bolt, MASK_SHOT);

	if (tr.fraction < 1.0)
	{
		VectorMA(bolt->s.origin, -10, dir, bolt->s.origin);
		bolt->touch(bolt, tr.ent, NULL, NULL);
	}
}
