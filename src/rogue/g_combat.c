/*
 * =======================================================================
 *
 * Combat code like damage, death and so on.
 *
 * =======================================================================
 */

#include "header/local.h"

void M_SetEffects(edict_t *self);

/*
 * clean up heal targets for medic
 */
void
cleanupHealTarget(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	ent->monsterinfo.healer = NULL;
	ent->takedamage = DAMAGE_YES;
	ent->monsterinfo.aiflags &= ~AI_RESURRECTING;
	M_SetEffects(ent);
}

/*
 * Returns true if the inflictor can directly damage the
 * target. Used for explosions and melee attacks.
 */
qboolean
CanDamage(edict_t *targ, edict_t *inflictor)
{
	vec3_t dest;
	trace_t trace;

	if (!targ || !inflictor)
	{
		return false;
	}

	/* bmodels need special checking because their origin is 0,0,0 */
	if (targ->movetype == MOVETYPE_PUSH)
	{
		VectorAdd(targ->absmin, targ->absmax, dest);
		VectorScale(dest, 0.5, dest);
		trace = gi.trace(inflictor->s.origin, vec3_origin,
				vec3_origin, dest, inflictor, MASK_SOLID);

		if (trace.fraction == 1.0)
		{
			return true;
		}

		if (trace.ent == targ)
		{
			return true;
		}

		return false;
	}

	trace = gi.trace(inflictor->s.origin, vec3_origin,
			vec3_origin, targ->s.origin, inflictor,
			MASK_SOLID);

	if (trace.fraction == 1.0)
	{
		return true;
	}

	VectorCopy(targ->s.origin, dest);
	dest[0] += 15.0;
	dest[1] += 15.0;
	trace = gi.trace(inflictor->s.origin, vec3_origin,
			vec3_origin, dest, inflictor, MASK_SOLID);

	if (trace.fraction == 1.0)
	{
		return true;
	}

	VectorCopy(targ->s.origin, dest);
	dest[0] += 15.0;
	dest[1] -= 15.0;
	trace = gi.trace(inflictor->s.origin, vec3_origin, vec3_origin,
			dest, inflictor, MASK_SOLID);

	if (trace.fraction == 1.0)
	{
		return true;
	}

	VectorCopy(targ->s.origin, dest);
	dest[0] -= 15.0;
	dest[1] += 15.0;
	trace = gi.trace(inflictor->s.origin, vec3_origin, vec3_origin,
			dest, inflictor, MASK_SOLID);

	if (trace.fraction == 1.0)
	{
		return true;
	}

	VectorCopy(targ->s.origin, dest);
	dest[0] -= 15.0;
	dest[1] -= 15.0;
	trace = gi.trace(inflictor->s.origin, vec3_origin, vec3_origin,
			dest, inflictor, MASK_SOLID);

	if (trace.fraction == 1.0)
	{
		return true;
	}

	return false;
}

void
Killed(edict_t *targ, edict_t *inflictor, edict_t *attacker,
		int damage, vec3_t point)
{
    if (!targ || !inflictor || !attacker)
	{
		return;
	}

	if (targ->health < -999)
	{
		targ->health = -999;
	}

	/* Reset AI flag for being ducked. This fixes a corner case
	   were the monster is ressurected by a medic and get's stuck
	   in the next frame for mmove_t not matching the AI state. */
	if (targ->monsterinfo.aiflags & AI_DUCKED)
	{
		targ->monsterinfo.aiflags &= ~AI_DUCKED;
	}

	if (targ->monsterinfo.aiflags & AI_MEDIC)
	{
		if (targ->enemy)
		{
			cleanupHealTarget(targ->enemy);
		}

		/* clean up self */
		targ->monsterinfo.aiflags &= ~AI_MEDIC;
		targ->enemy = attacker;
	}
	else
	{
		targ->enemy = attacker;
	}

	if ((targ->svflags & SVF_MONSTER) && (targ->deadflag != DEAD_DEAD))
	{
		/* free up slot for spawned monster if it's spawned */
		if (targ->monsterinfo.aiflags & AI_SPAWNED_CARRIER)
		{
			if (targ->monsterinfo.commander &&
				targ->monsterinfo.commander->inuse &&
				!strcmp(targ->monsterinfo.commander->classname, "monster_carrier"))
			{
				targ->monsterinfo.commander->monsterinfo.monster_slots++;
			}
		}

		if (targ->monsterinfo.aiflags & AI_SPAWNED_MEDIC_C)
		{
			if (targ->monsterinfo.commander)
			{
				if (targ->monsterinfo.commander->inuse &&
					!strcmp(targ->monsterinfo.commander->classname, "monster_medic_commander"))
				{
					targ->monsterinfo.commander->monsterinfo.monster_slots++;
				}
			}
		}

		if (targ->monsterinfo.aiflags & AI_SPAWNED_WIDOW)
		{
			/* need to check this because we can
			   have variable numbers of coop players */
			if (targ->monsterinfo.commander &&
				targ->monsterinfo.commander->inuse &&
				!strncmp(targ->monsterinfo.commander->classname, "monster_widow", 13))
			{
				if (targ->monsterinfo.commander->monsterinfo.monster_used > 0)
				{
					targ->monsterinfo.commander->monsterinfo.monster_used--;
				}
			}
		}

		if ((!(targ->monsterinfo.aiflags & AI_GOOD_GUY)) &&
			(!(targ->monsterinfo.aiflags & AI_DO_NOT_COUNT)))
		{
			level.killed_monsters++;

			if (coop->value && attacker->client)
			{
				attacker->client->resp.score++;
			}
		}
	}

	if ((targ->movetype == MOVETYPE_PUSH) ||
		(targ->movetype == MOVETYPE_STOP) || (targ->movetype == MOVETYPE_NONE))
	{
		targ->die(targ, inflictor, attacker, damage, point);
		return;
	}

	if ((targ->svflags & SVF_MONSTER) && (targ->deadflag != DEAD_DEAD))
	{
		targ->touch = NULL;
		monster_death_use(targ);
	}

	targ->die(targ, inflictor, attacker, damage, point);
}

void
SpawnDamage(int type, vec3_t origin, vec3_t normal, int damage)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(type);
	gi.WritePosition(origin);
	gi.WriteDir(normal);
	gi.multicast(origin, MULTICAST_PVS);
}

/*
 * ============
 * T_Damage
 *
 * targ		entity that is being damaged
 * inflictor	entity that is causing the damage
 * attacker	entity that caused the inflictor to damage targ
 *  example: targ=monster, inflictor=rocket, attacker=player
 *
 * dir			direction of the attack
 * point		point at which the damage is being inflicted
 * normal		normal vector from that point
 * damage		amount of damage being inflicted
 * knockback	force to be applied against targ as a result of the damage
 *
 * dflags		these flags are used to control how T_Damage works
 *  DAMAGE_RADIUS			damage was indirect (from a nearby explosion)
 *  DAMAGE_NO_ARMOR			armor does not protect from this damage
 *  DAMAGE_ENERGY			damage is from an energy based weapon
 *  DAMAGE_NO_KNOCKBACK		do not affect velocity, just view angles
 *  DAMAGE_BULLET			damage is from a bullet (used for ricochets)
 *  DAMAGE_NO_PROTECTION	kills godmode, armor, everything
 * ============
 */
int
CheckPowerArmor(edict_t *ent, vec3_t point, vec3_t normal,
		int damage, int dflags)
{
	gclient_t *client;
	int save;
	int power_armor_type;
	int index = 0;
	int damagePerCell;
	int pa_te_type;
	int power = 0;
	int power_used;

	if (!ent)
	{
		return 0;
	}

	if (!damage)
	{
		return 0;
	}

	client = ent->client;

	if (dflags & (DAMAGE_NO_ARMOR | DAMAGE_NO_POWER_ARMOR))
	{
		return 0;
	}

	if (client)
	{
		power_armor_type = PowerArmorType(ent);

		if (power_armor_type != POWER_ARMOR_NONE)
		{
			index = ITEM_INDEX(FindItem("Cells"));
			power = client->pers.inventory[index];
		}
	}
	else if (ent->svflags & SVF_MONSTER)
	{
		power_armor_type = ent->monsterinfo.power_armor_type;
		power = ent->monsterinfo.power_armor_power;
	}
	else
	{
		return 0;
	}

	if (power_armor_type == POWER_ARMOR_NONE)
	{
		return 0;
	}

	if (!power)
	{
		return 0;
	}

	if (power_armor_type == POWER_ARMOR_SCREEN)
	{
		vec3_t vec;
		float dot;
		vec3_t forward;

		/* only works if damage point is in front */
		AngleVectors(ent->s.angles, forward, NULL, NULL);
		VectorSubtract(point, ent->s.origin, vec);
		VectorNormalize(vec);
		dot = DotProduct(vec, forward);

		if (dot <= 0.3)
		{
			return 0;
		}

		damagePerCell = 1;
		pa_te_type = TE_SCREEN_SPARKS;
		damage = damage / 3;
	}
	else
	{
		damagePerCell = 2;
		pa_te_type = TE_SHIELD_SPARKS;
		damage = (2 * damage) / 3;
	}

	/* etf rifle */
	if (dflags & DAMAGE_NO_REG_ARMOR)
	{
		save = (power * damagePerCell) / 2;
	}
	else
	{
		save = power * damagePerCell;
	}

	if (!save)
	{
		return 0;
	}

	if (save > damage)
	{
		save = damage;
	}

	SpawnDamage(pa_te_type, point, normal, save);
	ent->powerarmor_time = level.time + 0.2;

	if (dflags & DAMAGE_NO_REG_ARMOR)
	{
		power_used = (save / damagePerCell) * 2;
	}
	else
	{
		power_used = save / damagePerCell;
	}

	if (client)
	{
		client->pers.inventory[index] -= power_used;
	}
	else
	{
		ent->monsterinfo.power_armor_power -= power_used;
	}

	return save;
}

int
CheckArmor(edict_t *ent, vec3_t point, vec3_t normal,
		int damage, int te_sparks, int dflags)
{
	gclient_t *client;
	int save;
	int index;
	gitem_t *armor;

	if (!ent)
	{
		return 0;
	}

	if (!damage)
	{
		return 0;
	}

	client = ent->client;

	if (!client)
	{
		return 0;
	}

	if (dflags & (DAMAGE_NO_ARMOR | DAMAGE_NO_REG_ARMOR))
	{
		return 0;
	}

	index = ArmorIndex(ent);

	if (!index)
	{
		return 0;
	}

	armor = GetItemByIndex(index);

	if (dflags & DAMAGE_ENERGY)
	{
		save = ceil(((gitem_armor_t *)armor->info)->energy_protection * damage);
	}
	else
	{
		save = ceil(((gitem_armor_t *)armor->info)->normal_protection * damage);
	}

	if (save >= client->pers.inventory[index])
	{
		save = client->pers.inventory[index];
	}

	if (!save)
	{
		return 0;
	}

	client->pers.inventory[index] -= save;
	SpawnDamage(te_sparks, point, normal, save);

	return save;
}

void
M_ReactToDamage(edict_t *targ, edict_t *attacker, edict_t *inflictor)
{
	qboolean new_tesla;

    if (!targ || !attacker || !inflictor)
	{
		return;
	}

	if (!(attacker->client) && !(attacker->svflags & SVF_MONSTER))
	{
		return;
	}

	/* logic for tesla - if you are hit by a tesla,
	   and can't see who you should be mad at (attacker)
	   attack the tesla also, target the tesla if it's
	   a "new" tesla */
	if ((inflictor) && (!strcmp(inflictor->classname, "tesla")))
	{
		new_tesla = MarkTeslaArea(targ, inflictor);

		if (new_tesla)
		{
			TargetTesla(targ, inflictor);
		}

		return;
	}

	if ((attacker == targ) || (attacker == targ->enemy))
	{
		return;
	}

	/* if we are a good guy monster and
	   our attacker is a player or another
	   good guy, do not get mad at them */
	if (targ->monsterinfo.aiflags & AI_GOOD_GUY)
	{
		if (attacker->client || (attacker->monsterinfo.aiflags & AI_GOOD_GUY))
		{
			return;
		}
	}

	/* if we're currently mad at something
	   a target_anger made us mad at, ignore
	   damage */
	if (targ->enemy && targ->monsterinfo.aiflags & AI_TARGET_ANGER)
	{
		float percentHealth;

		/* make sure whatever we were pissed at is still around. */
		if (targ->enemy->inuse)
		{
			percentHealth = (float)(targ->health) / (float)(targ->max_health);

			if (targ->enemy->inuse && (percentHealth > 0.33))
			{
				return;
			}
		}

		/* remove the target anger flag */
		targ->monsterinfo.aiflags &= ~AI_TARGET_ANGER;
	}

	/* if we're healing someone, do like above and try to stay with them */
	if ((targ->enemy) && (targ->monsterinfo.aiflags & AI_MEDIC))
	{
		float percentHealth;

		percentHealth = (float)(targ->health) / (float)(targ->max_health);

		/* ignore it some of the time */
		if (targ->enemy->inuse && (percentHealth > 0.25))
		{
			return;
		}

		/* remove the medic flag */
		targ->monsterinfo.aiflags &= ~AI_MEDIC;
		cleanupHealTarget(targ->enemy);
	}

	/* if attacker is a client, get mad at them
	   because he's good and we're not */
	if (attacker->client)
	{
		targ->monsterinfo.aiflags &= ~AI_SOUND_TARGET;

		/* this can only happen in coop (both new and
		   old enemies are clients) only switch if can't
		   see the current enemy */
		if (targ->enemy && targ->enemy->client)
		{
			if (visible(targ, targ->enemy))
			{
				targ->oldenemy = attacker;
				return;
			}

			targ->oldenemy = targ->enemy;
		}

		targ->enemy = attacker;

		if (!(targ->monsterinfo.aiflags & AI_DUCKED))
		{
			FoundTarget(targ);
		}

		return;
	}

	if (((targ->flags & (FL_FLY | FL_SWIM)) ==
		 (attacker->flags & (FL_FLY | FL_SWIM))) &&
		(strcmp(targ->classname, attacker->classname) != 0) &&
		!(attacker->monsterinfo.aiflags & AI_IGNORE_SHOTS) &&
		!(targ->monsterinfo.aiflags & AI_IGNORE_SHOTS))
	{
		if (targ->enemy && targ->enemy->client)
		{
			targ->oldenemy = targ->enemy;
		}

		targ->enemy = attacker;

		if (!(targ->monsterinfo.aiflags & AI_DUCKED))
		{
			FoundTarget(targ);
		}
	}
	/* if they *meant* to shoot us, then shoot back */
	else if (attacker->enemy == targ)
	{
		if (targ->enemy && targ->enemy->client)
		{
			targ->oldenemy = targ->enemy;
		}

		targ->enemy = attacker;

		if (!(targ->monsterinfo.aiflags & AI_DUCKED))
		{
			FoundTarget(targ);
		}
	}
	/* otherwise get mad at whoever they are mad at (help our buddy) unless it is us! */
	else if (attacker->enemy && (attacker->enemy != targ))
	{
		if (targ->enemy && targ->enemy->client)
		{
			targ->oldenemy = targ->enemy;
		}

		targ->enemy = attacker->enemy;

		if (!(targ->monsterinfo.aiflags & AI_DUCKED))
		{
			FoundTarget(targ);
		}
	}
}

qboolean
CheckTeamDamage(edict_t *targ, edict_t *attacker)
{
	return false;
}

void
T_Damage(edict_t *targ, edict_t *inflictor, edict_t *attacker, vec3_t dir,
		vec3_t point, vec3_t normal, int damage, int knockback, int dflags,
		int mod)
{
	gclient_t *client;
	int take;
	int save;
	int asave;
	int psave;
	int te_sparks;
	int sphere_notified;

	if (!targ || !inflictor || !attacker)
	{
		return;
	}

	if (!targ->takedamage)
	{
		return;
	}

	sphere_notified = false;

	/* friendly fire avoidance. If enabled you can't
	   hurt teammates (but you can hurt yourself)
	   knockback still occurs */
	if ((targ != attacker) && ((deathmatch->value &&
		 ((int)(dmflags->value) & (DF_MODELTEAMS | DF_SKINTEAMS))) ||
		 coop->value))
	{
		if (OnSameTeam(targ, attacker))
		{
			/* nukes kill everyone */
			if (((int)(dmflags->value) & DF_NO_FRIENDLY_FIRE) &&
				(mod != MOD_NUKE))
			{
				damage = 0;
			}
			else
			{
				mod |= MOD_FRIENDLY_FIRE;
			}
		}
	}

	meansOfDeath = mod;

	/* allow the deathmatch game to change values */
	if (deathmatch->value && gamerules && gamerules->value)
	{
		if (DMGame.ChangeDamage)
		{
			damage = DMGame.ChangeDamage(targ, attacker, damage, mod);
		}

		if (DMGame.ChangeKnockback)
		{
			knockback = DMGame.ChangeKnockback(targ, attacker, knockback, mod);
		}

		if (!damage)
		{
			return;
		}
	}

	/* easy mode takes half damage */
	if ((skill->value == 0) && (deathmatch->value == 0) && targ->client)
	{
		damage *= 0.5;

		if (!damage)
		{
			damage = 1;
		}
	}

	client = targ->client;

	/* defender sphere takes half damage */
	if ((client) && (client->owned_sphere) &&
		(client->owned_sphere->spawnflags == 1))
	{
		damage *= 0.5;

		if (!damage)
		{
			damage = 1;
		}
	}

	if (dflags & DAMAGE_BULLET)
	{
		te_sparks = TE_BULLET_SPARKS;
	}
	else
	{
		te_sparks = TE_SPARKS;
	}

	VectorNormalize(dir);

	/* bonus damage for suprising a monster */
	if (!(dflags & DAMAGE_RADIUS) && (targ->svflags & SVF_MONSTER) &&
		(attacker->client) && (!targ->enemy) && (targ->health > 0))
	{
		damage *= 2;
	}

	if (targ->flags & FL_NO_KNOCKBACK)
	{
		knockback = 0;
	}

	/* figure momentum add */
	if (!(dflags & DAMAGE_NO_KNOCKBACK))
	{
		if ((knockback) && (targ->movetype != MOVETYPE_NONE) &&
			(targ->movetype != MOVETYPE_BOUNCE) &&
			(targ->movetype != MOVETYPE_PUSH) &&
			(targ->movetype != MOVETYPE_STOP))
		{
			vec3_t kvel;
			float mass;

			if (targ->mass < 50)
			{
				mass = 50;
			}
			else
			{
				mass = targ->mass;
			}

			if (targ->client && (attacker == targ))
			{
				/* the rocket jump hack... */
				VectorScale(dir, 1600.0 * (float)knockback / mass, kvel);
			}
			else
			{
				VectorScale(dir, 500.0 * (float)knockback / mass, kvel);
			}

			VectorAdd(targ->velocity, kvel, targ->velocity);
		}
	}

	take = damage;
	save = 0;

	/* check for godmode */
	if ((targ->flags & FL_GODMODE) && !(dflags & DAMAGE_NO_PROTECTION))
	{
		take = 0;
		save = damage;
		SpawnDamage(te_sparks, point, normal, save);
	}

	/* check for invincibility */
	if ((client &&
		 (client->invincible_framenum > level.framenum)) &&
		!(dflags & DAMAGE_NO_PROTECTION))
	{
		if (targ->pain_debounce_time < level.time)
		{
			gi.sound(targ, CHAN_ITEM, gi.soundindex( "items/protect4.wav"), 1, ATTN_NORM, 0);
			targ->pain_debounce_time = level.time + 2;
		}

		take = 0;
		save = damage;
	}

	/* check for monster invincibility */
	if (((targ->svflags & SVF_MONSTER) &&
		 (targ->monsterinfo.invincible_framenum > level.framenum)) &&
		!(dflags & DAMAGE_NO_PROTECTION))
	{
		if (targ->pain_debounce_time < level.time)
		{
			gi.sound(targ, CHAN_ITEM, gi.soundindex( "items/protect4.wav"), 1, ATTN_NORM, 0);
			targ->pain_debounce_time = level.time + 2;
		}

		take = 0;
		save = damage;
	}

	psave = CheckPowerArmor(targ, point, normal, take, dflags);
	take -= psave;

	asave = CheckArmor(targ, point, normal, take, te_sparks, dflags);
	take -= asave;

	/* treat cheat/powerup savings the same as armor */
	asave += save;

	/* team damage avoidance */
	if (!(dflags & DAMAGE_NO_PROTECTION) && CheckTeamDamage(targ, attacker))
	{
		return;
	}

	/* this option will do damage both to the armor
	   and person. originally for DPU rounds */
	if (dflags & DAMAGE_DESTROY_ARMOR)
	{
		if (!(targ->flags & FL_GODMODE) && !(dflags & DAMAGE_NO_PROTECTION) &&
			!(client && (client->invincible_framenum > level.framenum)))
		{
			take = damage;
		}
	}

	/* do the damage */
	if (take)
	{
		/* need more blood for chainfist. */
		if (targ->flags & FL_MECHANICAL)
		{
			SpawnDamage(TE_ELECTRIC_SPARKS, point, normal, take);
		}
		else if ((targ->svflags & SVF_MONSTER) || (client))
		{
			if (mod == MOD_CHAINFIST)
			{
				SpawnDamage(TE_MOREBLOOD, point, normal, 255);
			}
			else
			{
				SpawnDamage(TE_BLOOD, point, normal, take);
			}
		}
		else
		{
			SpawnDamage(te_sparks, point, normal, take);
		}

		targ->health = targ->health - take;

		/* spheres need to know who to shoot at */
		if (client && client->owned_sphere)
		{
			sphere_notified = true;

			if (client->owned_sphere->pain)
			{
				client->owned_sphere->pain(client->owned_sphere, attacker, 0, 0);
			}
		}

		if (targ->health <= 0)
		{
			if ((targ->svflags & SVF_MONSTER) || (client))
			{
				targ->flags |= FL_NO_KNOCKBACK;
			}

			Killed(targ, inflictor, attacker, take, point);
			return;
		}
	}

	/* spheres need to know who to shoot at */
	if (!sphere_notified)
	{
		if (client && client->owned_sphere)
		{
			if (client->owned_sphere->pain)
			{
				client->owned_sphere->pain(client->owned_sphere, attacker, 0,
						0);
			}
		}
	}

	if (targ->svflags & SVF_MONSTER)
	{
		M_ReactToDamage(targ, attacker, inflictor);

		if (!(targ->monsterinfo.aiflags & AI_DUCKED) && (take))
		{
			targ->pain(targ, attacker, knockback, take);

			/* nightmare mode monsters don't go into pain frames often */
			if (skill->value == 3)
			{
				targ->pain_debounce_time = level.time + 5;
			}
		}
	}
	else if (client)
	{
		if (!(targ->flags & FL_GODMODE) && (take))
		{
			targ->pain(targ, attacker, knockback, take);
		}
	}
	else if (take)
	{
		if (targ->pain)
		{
			targ->pain(targ, attacker, knockback, take);
		}
	}

	/* add to the damage inflicted on a player this frame
	   the total will be turned into screen blends and view angle kicks
	   at the end of the frame */
	if (client)
	{
		client->damage_parmor += psave;
		client->damage_armor += asave;
		client->damage_blood += take;
		client->damage_knockback += knockback;
		VectorCopy(point, client->damage_from);
	}
}

void
T_RadiusDamage(edict_t *inflictor, edict_t *attacker, float damage,
		edict_t *ignore, float radius, int mod)
{
	float points;
	edict_t *ent = NULL;
	vec3_t v;
	vec3_t dir;

	if (!inflictor || !attacker)
	{
		return;
	}

	while ((ent = findradius(ent, inflictor->s.origin, radius)) != NULL)
	{
		if (ent == ignore)
		{
			continue;
		}

		if (!ent->takedamage)
		{
			continue;
		}

		VectorAdd(ent->mins, ent->maxs, v);
		VectorMA(ent->s.origin, 0.5, v, v);
		VectorSubtract(inflictor->s.origin, v, v);
		points = damage - 0.5 * VectorLength(v);

		if (ent == attacker)
		{
			points = points * 0.5;
		}

		if (points > 0)
		{
			if (CanDamage(ent, inflictor))
			{
				VectorSubtract(ent->s.origin, inflictor->s.origin, dir);
				T_Damage(ent, inflictor, attacker, dir, inflictor->s.origin, vec3_origin,
						(int)points, (int)points, DAMAGE_RADIUS, mod);
			}
		}
	}
}

void
T_RadiusNukeDamage(edict_t *inflictor, edict_t *attacker, float damage,
		edict_t *ignore, float radius, int mod)
{
	float points;
	edict_t *ent = NULL;
	vec3_t v;
	vec3_t dir;
	float len;
	float killzone, killzone2;
	trace_t tr;
	float dist;

	killzone = radius;
	killzone2 = radius * 2.0;

	if (!inflictor || !attacker || !ignore)
	{
		return;
	}

	while ((ent = findradius(ent, inflictor->s.origin, killzone2)) != NULL)
	{
		/* ignore nobody */
		if (ent == ignore)
		{
			continue;
		}

		if (!ent->takedamage)
		{
			continue;
		}

		if (!ent->inuse)
		{
			continue;
		}

		if (!(ent->client || (ent->svflags & SVF_MONSTER) ||
			  (ent->svflags & SVF_DAMAGEABLE)))
		{
			continue;
		}

		VectorAdd(ent->mins, ent->maxs, v);
		VectorMA(ent->s.origin, 0.5, v, v);
		VectorSubtract(inflictor->s.origin, v, v);
		len = VectorLength(v);

		if (len <= killzone)
		{
			if (ent->client)
			{
				ent->flags |= FL_NOGIB;
			}

			points = 10000;
		}
		else if (len <= killzone2)
		{
			points = (damage / killzone) * (killzone2 - len);
		}
		else
		{
			points = 0;
		}

		if (points > 0)
		{
			if (ent->client)
			{
				ent->client->nuke_framenum = level.framenum + 20;
			}

			VectorSubtract(ent->s.origin, inflictor->s.origin, dir);
			T_Damage(ent, inflictor, attacker, dir, inflictor->s.origin,
					vec3_origin, (int)points, (int)points, DAMAGE_RADIUS,
					mod);
		}
	}

	/* skip the worldspawn */
	ent = g_edicts + 1;

	/* cycle through players */
	while (ent)
	{
		if ((ent->client) &&
			(ent->client->nuke_framenum != level.framenum + 20) && (ent->inuse))
		{
			tr = gi.trace(inflictor->s.origin, NULL, NULL, ent->s.origin,
					inflictor, MASK_SOLID);

			if (tr.fraction == 1.0)
			{
				ent->client->nuke_framenum = level.framenum + 20;
			}
			else
			{
				dist = realrange(ent, inflictor);

				if (dist < 2048)
				{
					ent->client->nuke_framenum = max(ent->client->nuke_framenum,
							level.framenum + 15);
				}
				else
				{
					ent->client->nuke_framenum = max(ent->client->nuke_framenum,
							level.framenum + 10);
				}
			}

			ent++;
		}
		else
		{
			ent = NULL;
		}
	}
}

/*
 * Like T_RadiusDamage, but ignores
 * anything with classname=ignoreClass
 */
void
T_RadiusClassDamage(edict_t *inflictor, edict_t *attacker, float damage,
		char *ignoreClass, float radius, int mod)
{
	float points;
	edict_t *ent = NULL;
	vec3_t v;
	vec3_t dir;

	if (!inflictor || !attacker || !ignoreClass)
	{
		return;
	}

	while ((ent = findradius(ent, inflictor->s.origin, radius)) != NULL)
	{
		if (ent->classname && !strcmp(ent->classname, ignoreClass))
		{
			continue;
		}

		if (!ent->takedamage)
		{
			continue;
		}

		VectorAdd(ent->mins, ent->maxs, v);
		VectorMA(ent->s.origin, 0.5, v, v);
		VectorSubtract(inflictor->s.origin, v, v);
		points = damage - 0.5 * VectorLength(v);

		if (ent == attacker)
		{
			points = points * 0.5;
		}

		if (points > 0)
		{
			if (CanDamage(ent, inflictor))
			{
				VectorSubtract(ent->s.origin, inflictor->s.origin, dir);
				T_Damage(ent, inflictor, attacker, dir, inflictor->s.origin,
						vec3_origin, (int)points, (int)points, DAMAGE_RADIUS,
						mod);
			}
		}
	}
}
