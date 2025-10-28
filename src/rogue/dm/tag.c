/* =======================================================================
 *
 * Deathmatch tag.
 *
 * =======================================================================
 */

#include "../header/local.h"

extern edict_t *SelectFarthestDeathmatchSpawnPoint(void);
extern void SelectSpawnPoint(edict_t *ent, vec3_t origin, vec3_t angles);
void droptofloor(edict_t *self);
void SP_dm_tag_token(edict_t *self);

static edict_t *tag_owner;
static int tag_count;

static gitem_t *it_token;
static gitem_t *it_quad;

void
Tag_PlayerDeath(edict_t *targ, edict_t *inflictor /* unused */, edict_t *attacker /* unused */)
{
	if (targ && (tag_owner == targ))
	{
		Tag_DropToken(targ, it_token);
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
	if (self && (tag_owner == self))
	{
		Tag_DropToken(self, it_token);
		tag_owner = NULL;
		tag_count = 0;
	}
}

void
Tag_Score(edict_t *attacker, edict_t *victim, int scoreChange)
{
	int mod;

	if (!attacker || !victim)
	{
		return;
	}

	mod = meansOfDeath & ~MOD_FRIENDLY_FIRE;

	if (tag_owner)
	{
		/* owner killed somone else */
		if ((scoreChange > 0) && (tag_owner == attacker))
		{
			scoreChange = 3;
			tag_count++;

			if (tag_count == 5)
			{
				tag_count = 0;

				if (it_quad)
				{
					attacker->client->pers.inventory[ITEM_INDEX(it_quad)]++;
					it_quad->use(attacker, it_quad);
				}
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
				Tag_DropToken(tag_owner, it_token);
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
	if (!ent)
	{
		return false;
	}

	tag_owner = other;
	tag_count = 0;

	if (other)
	{
		other->client->pers.inventory[ITEM_INDEX(ent->item)]++;
		Tag_KillItBonus(other);
	}

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

	if (!spot)
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

	ent->think = Tag_Respawn;

	/* check here to see if it's in lava or slime. if so, do a respawn sooner */
	ent->nextthink = level.time + ((gi.pointcontents(ent->s.origin) & (CONTENTS_LAVA | CONTENTS_SLIME)) ?
		3 : 30);
}

void
Tag_DropToken(edict_t *ent, gitem_t *item)
{
	edict_t *tag_token;
	trace_t trace;
	vec3_t forward, right;
	static vec3_t offset = {24, 0, -16};

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

	AngleVectors(ent->client ? ent->client->v_angle : ent->s.angles,
		forward, right, NULL);

	G_ProjectSource(ent->s.origin, offset, forward, right, tag_token->s.origin);

	trace = gi.trace(ent->s.origin, tag_token->mins, tag_token->maxs, tag_token->s.origin, ent, CONTENTS_SOLID);
	VectorCopy(trace.endpos, tag_token->s.origin);

	VectorScale(forward, 100, tag_token->velocity);
	tag_token->velocity[2] = 300;

	tag_token->think = Tag_MakeTouchable;
	tag_token->nextthink = level.time + 1;

	gi.linkentity(tag_token);

	if (ent->client)
	{
		ent->client->pers.inventory[ITEM_INDEX(item)]--;
		ValidateSelectedItem(ent);
	}
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
	it_token = FindItem("Tag Token");
	it_quad = FindItem("Quad Damage");
}

static void
Tag_SpawnToken(void)
{
	edict_t *e;
	vec3_t angles, origin;

	e = G_Spawn();
	e->classname = "dm_tag_token";

	SelectSpawnPoint(e, origin, angles);
	VectorCopy(origin, e->s.origin);
	VectorCopy(origin, e->s.old_origin);
	VectorCopy(angles, e->s.angles);

	SP_dm_tag_token(e);
}

void
Tag_PostInitSetup(void)
{
	tag_owner = NULL;
	tag_count = 0;

	/* automatic spawning of tag token if one is not present on map. */
	if (!G_Find(NULL, FOFS(classname), "dm_tag_token"))
	{
		Tag_SpawnToken();
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

	if (!deathmatch->value || DMGame.Type != RDM_TAG)
	{
		G_FreeEdict(self);
		return;
	}

	SpawnItem(self, it_token);
}

const dm_game_rt dm_game_tag =
{
	.Type = RDM_TAG,

	.GameInit = Tag_GameInit,
	.PostInitSetup = Tag_PostInitSetup,
	.ClientBegin = NULL,
	.SelectSpawnPoint = NULL,
	.PlayerDeath = Tag_PlayerDeath,
	.Score = Tag_Score,
	.PlayerEffects = Tag_PlayerEffects,
	.DogTag = Tag_DogTag,
	.PlayerDisconnect = Tag_PlayerDisconnect,
	.ChangeDamage = Tag_ChangeDamage,
	.ChangeKnockback = NULL,
	.CheckDMRules = NULL
};
