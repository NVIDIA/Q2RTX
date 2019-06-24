/*
 * =======================================================================
 *
 * Rogue specific deathmatch stuff.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "monster/misc/player.h"

#define IT_TYPE_MASK (IT_WEAPON | IT_AMMO | IT_POWERUP | IT_ARMOR | IT_KEY)

dm_game_rt DMGame;

extern qboolean Pickup_Health(edict_t *ent, edict_t *other);
extern qboolean Pickup_Adrenaline(edict_t *ent, edict_t *other);
extern qboolean Pickup_Armor(edict_t *ent, edict_t *other);
extern qboolean Pickup_PowerArmor(edict_t *ent, edict_t *other);
extern edict_t *Sphere_Spawn(edict_t *owner, int spawnflags);
extern void ED_CallSpawn(edict_t *ent);
void fire_doppleganger(edict_t *ent, vec3_t start, vec3_t aimdir);

void
InitGameRules(void)
{
	int gameNum;

	/* clear out the game rule structure before we start */
	memset(&DMGame, 0, sizeof(dm_game_rt));

	if (gamerules && gamerules->value)
	{
		gameNum = gamerules->value;

		switch (gameNum)
		{
			case RDM_TAG:
				DMGame.GameInit = Tag_GameInit;
				DMGame.PostInitSetup = Tag_PostInitSetup;
				DMGame.PlayerDeath = Tag_PlayerDeath;
				DMGame.Score = Tag_Score;
				DMGame.PlayerEffects = Tag_PlayerEffects;
				DMGame.DogTag = Tag_DogTag;
				DMGame.PlayerDisconnect = Tag_PlayerDisconnect;
				DMGame.ChangeDamage = Tag_ChangeDamage;
				break;

			/* reset gamerules if it's not a valid number */
			default:
				gamerules->value = 0;
				break;
		}
	}

	/* if we're set up to play, initialize the game as needed. */
	if (DMGame.GameInit)
	{
		DMGame.GameInit();
	}
}

char *
FindSubstituteItem(edict_t *ent)
{
	int i;
	int itflags, myflags;
	float rnd;
	int count;
	int pick;
	gitem_t *it;

	/* there are only two classes of power armor, and we don't want
	   to give out power screens. therefore, power shields should
	   remain power shields. (powerscreens shouldn't be there at all...) */
	if (ent->item->pickup == Pickup_PowerArmor)
	{
		return NULL;
	}

	/* health is special case */
	if ((ent->item->pickup == Pickup_Health) ||
		(ent->item->pickup == Pickup_Adrenaline))
	{
		/* health pellets stay health pellets */
		if (!strcmp(ent->classname, "item_health_small"))
		{
			return NULL;
		}

		rnd = random();

		if (rnd < 0.6)
		{
			return "item_health";
		}
		else if (rnd < 0.9)
		{
			return "item_health_large";
		}
		else if (rnd < 0.99)
		{
			return "item_adrenaline";
		}
		else
		{
			return "item_health_mega";
		}
	}
	/* armor is also special case */
	else if (ent->item->pickup == Pickup_Armor)
	{
		/* armor shards stay armor shards */
		if (ent->item->tag == ARMOR_SHARD)
		{
			return NULL;
		}

		rnd = random();

		if (rnd < 0.6)
		{
			return "item_armor_jacket";
		}
		else if (rnd < 0.9)
		{
			return "item_armor_combat";
		}
		else
		{
			return "item_armor_body";
		}
	}

	/* we want to stay within the item class */
	myflags = ent->item->flags & IT_TYPE_MASK;

	if ((myflags & IT_AMMO) && (myflags & IT_WEAPON))
	{
		myflags = IT_AMMO;
	}

	count = 0;

	/* first pass, count the matching items */
	it = itemlist;

	for (i = 0; i < game.num_items; i++, it++)
	{
		itflags = it->flags;

		if (!itflags || (itflags & IT_NOT_GIVEABLE))
		{
			continue;
		}

		/* prox,grenades,etc should count as ammo. */
		if ((itflags & IT_AMMO) && (itflags & IT_WEAPON))
		{
			itflags = IT_AMMO;
		}

		/* don't respawn spheres if they're dmflag disabled. */
		if ((int)dmflags->value & DF_NO_SPHERES)
		{
			if (!strcmp(ent->classname, "item_sphere_vengeance") ||
				!strcmp(ent->classname, "item_sphere_hunter") ||
				!strcmp(ent->classname, "item_spehre_defender"))
			{
				continue;
			}
		}

		if (((int)dmflags->value & DF_NO_NUKES) &&
			!strcmp(ent->classname, "ammo_nuke"))
		{
			continue;
		}

		if (((int)dmflags->value & DF_NO_MINES) &&
			(!strcmp(ent->classname, "ammo_prox") || !strcmp(ent->classname, "ammo_tesla")))
		{
			continue;
		}

		if ((itflags & IT_TYPE_MASK) == (myflags & IT_TYPE_MASK))
		{
			count++;
		}
	}

	if (!count)
	{
		return NULL;
	}

	pick = ceil(random() * count);
	count = 0;

	/* second pass, pick one. */
	it = itemlist;

	for (i = 0; i < game.num_items; i++, it++)
	{
		itflags = it->flags;

		if (!itflags || (itflags & IT_NOT_GIVEABLE))
		{
			continue;
		}

		/* prox,grenades,etc should count as ammo. */
		if ((itflags & IT_AMMO) && (itflags & IT_WEAPON))
		{
			itflags = IT_AMMO;
		}

		if (((int)dmflags->value & DF_NO_NUKES) &&
			!strcmp(ent->classname, "ammo_nuke"))
		{
			continue;
		}

		if (((int)dmflags->value & DF_NO_MINES) &&
			(!strcmp(ent->classname, "ammo_prox") || !strcmp(ent->classname, "ammo_tesla")))
		{
			continue;
		}

		if ((itflags & IT_TYPE_MASK) == (myflags & IT_TYPE_MASK))
		{
			count++;

			if (pick == count)
			{
				return it->classname;
			}
		}
	}

	return NULL;
}

edict_t *
DoRandomRespawn(edict_t *ent)
{
	edict_t *newEnt;
	char *classname;

	if (!ent)
	{
		return NULL;
	}

	classname = FindSubstituteItem(ent);

	if (classname == NULL)
	{
		return NULL;
	}

	gi.unlinkentity(ent);

	newEnt = G_Spawn();
	newEnt->classname = classname;
	VectorCopy(ent->s.origin, newEnt->s.origin);
	VectorCopy(ent->s.old_origin, newEnt->s.old_origin);
	VectorCopy(ent->mins, newEnt->mins);
	VectorCopy(ent->maxs, newEnt->maxs);

	VectorSet(newEnt->gravityVector, 0, 0, -1);

	ED_CallSpawn(newEnt);

	newEnt->s.renderfx |= RF_IR_VISIBLE;

	return newEnt;
}

void
PrecacheForRandomRespawn(void)
{
	gitem_t *it;
	int i;
	int itflags;

	it = itemlist;

	for (i = 0; i < game.num_items; i++, it++)
	{
		itflags = it->flags;

		if (!itflags || (itflags & IT_NOT_GIVEABLE))
		{
			continue;
		}

		PrecacheItem(it);
	}
}

void
doppleganger_die(edict_t *self, edict_t *inflictor /* unused */, edict_t *attacker,
		int damage, vec3_t point)
{
	edict_t *sphere;
	float dist;
	vec3_t dir;

	if (!self || !attacker)
	{
		return;
	}

	if ((self->enemy) && (self->enemy != self->teammaster))
	{
		VectorSubtract(self->enemy->s.origin, self->s.origin, dir);
		dist = VectorLength(dir);

		if (dist > 768)
		{
			sphere = Sphere_Spawn(self, SPHERE_HUNTER | SPHERE_DOPPLEGANGER);
			sphere->pain(sphere, attacker, 0, 0);
		}
		else
		{
			sphere = Sphere_Spawn(self, SPHERE_VENGEANCE | SPHERE_DOPPLEGANGER);
			sphere->pain(sphere, attacker, 0, 0);
		}
	}

	if (self->teamchain)
	{
		BecomeExplosion1(self->teamchain);
	}

	BecomeExplosion1(self);
}

void
doppleganger_pain(edict_t *self, edict_t *other, float kick, int damage)
{
	self->enemy = other;
}

void
doppleganger_timeout(edict_t *self)
{
	if (self->teamchain)
	{
		BecomeExplosion1(self->teamchain);
	}

	BecomeExplosion1(self);
}

void
body_think(edict_t *self)
{
	float r;

	if (fabsf(self->ideal_yaw - anglemod(self->s.angles[YAW])) < 2)
	{
		if (self->timestamp < level.time)
		{
			r = random();

			if (r < 0.10)
			{
				self->ideal_yaw = random() * 350.0;
				self->timestamp = level.time + 1;
			}
		}
	}
	else
	{
		M_ChangeYaw(self);
	}

	self->s.frame++;

	if (self->s.frame > FRAME_stand40)
	{
		self->s.frame = FRAME_stand01;
	}

	self->nextthink = level.time + 0.1;
}

void
fire_doppleganger(edict_t *ent, vec3_t start, vec3_t aimdir)
{
	edict_t *base;
	edict_t *body;
	vec3_t dir;
	vec3_t forward, right, up;
	int number;

	if (!ent)
	{
		return;
	}

	vectoangles2(aimdir, dir);
	AngleVectors(dir, forward, right, up);

	base = G_Spawn();
	VectorCopy(start, base->s.origin);
	VectorCopy(dir, base->s.angles);
	VectorClear(base->velocity);
	VectorClear(base->avelocity);
	base->movetype = MOVETYPE_TOSS;
	base->solid = SOLID_BBOX;
	base->s.renderfx |= RF_IR_VISIBLE;
	base->s.angles[PITCH] = 0;
	VectorSet(base->mins, -16, -16, -24);
	VectorSet(base->maxs, 16, 16, 32);
	base->s.modelindex = 0;
	base->teammaster = ent;
	base->svflags |= SVF_DAMAGEABLE;
	base->takedamage = DAMAGE_AIM;
	base->health = 30;
	base->pain = doppleganger_pain;
	base->die = doppleganger_die;

	base->nextthink = level.time + 30;
	base->think = doppleganger_timeout;
	base->classname = "doppleganger";

	gi.linkentity(base);

	body = G_Spawn();
	number = body->s.number;
	body->s = ent->s;
	body->s.sound = 0;
	body->s.event = 0;
	body->s.number = number;
	body->yaw_speed = 30;
	body->ideal_yaw = 0;
	VectorCopy(start, body->s.origin);
	body->s.origin[2] += 8;
	body->think = body_think;
	body->nextthink = level.time + FRAMETIME;
	gi.linkentity(body);

	base->teamchain = body;
	body->teammaster = base;
}
