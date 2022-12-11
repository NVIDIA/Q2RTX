/* =======================================================================
 *
 * Deathmatch tag.
 *
 * =======================================================================
 */

#include "../header/local.h"

extern edict_t *SelectFarthestDeathmatchSpawnPoint(void);
extern void SelectSpawnPoint(edict_t *ent, vec3_t origin, vec3_t angles);
void SP_dm_tag_token(edict_t *self);

edict_t *tag_token;
edict_t *tag_owner;
int tag_count;

void
Tag_PlayerDeath(edict_t *targ, edict_t *inflictor /* unused */, edict_t *attacker /* unused */)
{
	if (!targ)
	{
		return;
	}

	if (tag_token && targ && (targ == tag_owner))
	{
		Tag_DropToken(targ, FindItem("Tag Token"));
		tag_owner = NULL;
		tag_count = 0;
	}
}

void
Tag_KillItBonus(edict_t *self)
{
	edict_t *armor;

	if (!self)
	{
		return;
	}

	/* if the player is hurt, boost them up to max. */
	if (self->health < self->max_health)
	{
		self->health += 200;

		if (self->health > self->max_health)
		{
			self->health = self->max_health;
		}
	}

	/* give the player a body armor */
	armor = G_Spawn();
	armor->spawnflags |= DROPPED_ITEM;
	armor->item = FindItem("Body Armor");
	Touch_Item(armor, self, NULL, NULL);

	if (armor->inuse)
	{
		G_FreeEdict(armor);
	}
}

void
Tag_PlayerDisconnect(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (tag_token && self && (self == tag_owner))
	{
		Tag_DropToken(self, FindItem("Tag Token"));
		tag_owner = NULL;
		tag_count = 0;
	}
}

void
Tag_Score(edict_t *attacker, edict_t *victim, int scoreChange)
{
	gitem_t *quad;
	int mod;

	if (!attacker || !victim)
	{
		return;
	}

	mod = meansOfDeath & ~MOD_FRIENDLY_FIRE;

	if (tag_token && tag_owner)
	{
		/* owner killed somone else */
		if ((scoreChange > 0) && (tag_owner == attacker))
		{
			scoreChange = 3;
			tag_count++;

			if (tag_count == 5)
			{
				quad = FindItem("Quad Damage");
				attacker->client->pers.inventory[ITEM_INDEX(quad)]++;
				quad->use(attacker, quad);
				tag_count = 0;
			}
		}
		/* owner got killed. 5 points and switch owners */
		else if ((tag_owner == victim) && (tag_owner != attacker))
		{
			scoreChange = 5;

			if ((mod == MOD_HUNTER_SPHERE) || (mod == MOD_DOPPLE_EXPLODE) ||
				(mod == MOD_DOPPLE_VENGEANCE) || (mod == MOD_DOPPLE_HUNTER) ||
				(attacker->health <= 0))
			{
				Tag_DropToken(tag_owner, FindItem("Tag Token"));
				tag_owner = NULL;
				tag_count = 0;
			}
			else
			{
				Tag_KillItBonus(attacker);
				tag_owner = attacker;
				tag_count = 0;
			}
		}
	}

	attacker->client->resp.score += scoreChange;
}

qboolean
Tag_PickupToken(edict_t *ent, edict_t *other)
{
	if (gamerules && (gamerules->value != 2))
	{
		return false;
	}

	if (!ent || !other)
	{
		return false;
	}

	/* sanity checking is good. */
	if (tag_token != ent)
	{
		tag_token = ent;
	}

	other->client->pers.inventory[ITEM_INDEX(ent->item)]++;

	tag_owner = other;
	tag_count = 0;

	Tag_KillItBonus(other);

	return true;
}

void
Tag_Respawn(edict_t *ent)
{
	edict_t *spot;

	if (!ent)
	{
		return;
	}

	spot = SelectFarthestDeathmatchSpawnPoint();

	if (spot == NULL)
	{
		ent->nextthink = level.time + 1;
		return;
	}

	VectorCopy(spot->s.origin, ent->s.origin);
	gi.linkentity(ent);
}

void
Tag_MakeTouchable(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	ent->touch = Touch_Item;

	tag_token->think = Tag_Respawn;

	/* check here to see if it's in lava or slime. if so, do a respawn sooner */
	if (gi.pointcontents(ent->s.origin) & (CONTENTS_LAVA | CONTENTS_SLIME))
	{
		tag_token->nextthink = level.time + 3;
	}
	else
	{
		tag_token->nextthink = level.time + 30;
	}
}

void
Tag_DropToken(edict_t *ent, gitem_t *item)
{
	trace_t trace;
	vec3_t forward, right;
	vec3_t offset;

	if (!ent || !item)
	{
		return;
	}

	/* reset the score count for next player */
	tag_count = 0;
	tag_owner = NULL;

	tag_token = G_Spawn();

	tag_token->classname = item->classname;
	tag_token->item = item;
	tag_token->spawnflags = DROPPED_ITEM;
	tag_token->s.effects = EF_ROTATE | EF_TAGTRAIL;
	tag_token->s.renderfx = RF_GLOW;
	VectorSet(tag_token->mins, -15, -15, -15);
	VectorSet(tag_token->maxs, 15, 15, 15);
	gi.setmodel(tag_token, tag_token->item->world_model);
	tag_token->solid = SOLID_TRIGGER;
	tag_token->movetype = MOVETYPE_TOSS;
	tag_token->touch = NULL;
	tag_token->owner = ent;

	AngleVectors(ent->client->v_angle, forward, right, NULL);
	VectorSet(offset, 24, 0, -16);
	G_ProjectSource(ent->s.origin, offset, forward, right, tag_token->s.origin);
	trace = gi.trace(ent->s.origin, tag_token->mins, tag_token->maxs, tag_token->s.origin, ent, CONTENTS_SOLID);
	VectorCopy(trace.endpos, tag_token->s.origin);

	VectorScale(forward, 100, tag_token->velocity);
	tag_token->velocity[2] = 300;

	tag_token->think = Tag_MakeTouchable;
	tag_token->nextthink = level.time + 1;

	gi.linkentity(tag_token);

	ent->client->pers.inventory[ITEM_INDEX(item)]--;
	ValidateSelectedItem(ent);
}

void
Tag_PlayerEffects(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (ent == tag_owner)
	{
		ent->s.effects |= EF_TAGTRAIL;
	}
}

void
Tag_DogTag(edict_t *ent, edict_t *killer /* unused */, char **pic)
{
	if (!ent || !pic)
	{
		return;
	}

	if (ent == tag_owner)
	{
		(*pic) = "tag3";
	}
}

int
Tag_ChangeDamage(edict_t *targ, edict_t *attacker, int damage, int mod)
{
	if (!targ || !attacker)
	{
		return 0;
	}

	if ((targ != tag_owner) && (attacker != tag_owner))
	{
		return damage * 3 / 4;
	}

	return damage;
}

void
Tag_GameInit(void)
{
	tag_token = NULL;
	tag_owner = NULL;
	tag_count = 0;
}

void
Tag_PostInitSetup(void)
{
	edict_t *e;
	vec3_t origin, angles;

	/* automatic spawning of tag token if one is not present on map. */
	e = G_Find(NULL, FOFS(classname), "dm_tag_token");

	if (e == NULL)
	{
		e = G_Spawn();
		e->classname = "dm_tag_token";

		SelectSpawnPoint(e, origin, angles);
		VectorCopy(origin, e->s.origin);
		VectorCopy(origin, e->s.old_origin);
		VectorCopy(angles, e->s.angles);
		SP_dm_tag_token(e);
	}
}

/*
 * QUAKED dm_tag_token (.3 .3 1) (-16 -16 -16) (16 16 16)
 * The tag token for deathmatch tag games.
 */
void
SP_dm_tag_token(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (!(deathmatch->value))
	{
		G_FreeEdict(self);
		return;
	}

	if (gamerules && (gamerules->value != 2))
	{
		G_FreeEdict(self);
		return;
	}

	/* store the tag token edict pointer for later use. */
	tag_token = self;
	tag_count = 0;

	self->classname = "dm_tag_token";
	self->model = "models/items/tagtoken/tris.md2";
	self->count = 1;
	SpawnItem(self, FindItem("Tag Token"));
}
