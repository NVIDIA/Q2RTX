/* =======================================================================
 *
 * The basic AI functions like enemy detection, attacking and so on.
 *
 * =======================================================================
 */

#include "header/local.h"

extern cvar_t *maxclients;
int enemy_range;
float enemy_yaw;
qboolean ai_checkattack(edict_t *self, float dist);
qboolean enemy_infront;
qboolean enemy_vis;
qboolean FindTarget(edict_t *self);

/* ========================================================================== */

/*
 * Called once each frame to set level.sight_client
 * to the player to be checked for in findtarget.
 * If all clients are either dead or in notarget,
 * sight_client will be null.
 * In coop games, sight_client will cycle
 * between the clients.
 */
void
AI_SetSightClient(void)
{
	edict_t *ent;
	int start, check;

	if (level.sight_client == NULL)
	{
		start = 1;
	}
	else
	{
		start = level.sight_client - g_edicts;
	}

	check = start;

	while (1)
	{
		check++;

		if (check > game.maxclients)
		{
			check = 1;
		}

		ent = &g_edicts[check];

		if (ent->inuse && (ent->health > 0) &&
			!(ent->flags & (FL_NOTARGET | FL_DISGUISED)))
		{
			level.sight_client = ent;
			return; /* got one */
		}

		if (check == start)
		{
			level.sight_client = NULL;
			return; /* nobody to see */
		}
	}
}

/*
 * Move the specified distance at current facing.
 */
void
ai_move(edict_t *self, float dist)
{
	if (!self)
	{
		return;
	}

	M_walkmove(self, self->s.angles[YAW], dist);
}

/*
 *
 * Used for standing around and looking
 * for players Distance is for slight
 * position adjustments needed by the
 * animations
 */
void
ai_stand(edict_t *self, float dist)
{
	vec3_t v;
	qboolean retval;

	if (!self)
	{
		return;
	}

	if (dist)
	{
		M_walkmove(self, self->s.angles[YAW], dist);
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		if (self->enemy)
		{
			VectorSubtract(self->enemy->s.origin, self->s.origin, v);
			self->ideal_yaw = vectoyaw(v);

			if ((self->s.angles[YAW] != self->ideal_yaw) &&
				self->monsterinfo.aiflags & AI_TEMP_STAND_GROUND)
			{
				self->monsterinfo.aiflags &=
					~(AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
				self->monsterinfo.run(self);
			}

			if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
			{
				M_ChangeYaw(self);
			}

			/* find out if we're going to be shooting */
			retval = ai_checkattack(self, 0);

			/* record sightings of player */
			if ((self->enemy) && (self->enemy->inuse) &&
				(visible(self, self->enemy)))
			{
				self->monsterinfo.aiflags &= ~AI_LOST_SIGHT;
				VectorCopy(self->enemy->s.origin, self->monsterinfo.last_sighting);
				VectorCopy(self->enemy->s.origin, self->monsterinfo.blind_fire_target);
				self->monsterinfo.trail_time = level.time;
				self->monsterinfo.blind_fire_delay = 0;
			}
			/* check retval to make sure we're not blindfiring */
			else if (!retval)
			{
				FindTarget(self);
				return;
			}
		}
		else
		{
			FindTarget(self);
		}

		return;
	}

	if (FindTarget(self))
	{
		return;
	}

	if (level.time > self->monsterinfo.pausetime)
	{
		self->monsterinfo.walk(self);
		return;
	}

	if (!(self->spawnflags & 1) && (self->monsterinfo.idle) &&
		(level.time > self->monsterinfo.idle_time))
	{
		if (self->monsterinfo.idle_time)
		{
			self->monsterinfo.idle(self);
			self->monsterinfo.idle_time = level.time + 15 + random() * 15;
		}
		else
		{
			self->monsterinfo.idle_time = level.time + random() * 15;
		}
	}
}

/*
 * The monster is walking it's beat
 */
void
ai_walk(edict_t *self, float dist)
{
	M_MoveToGoal(self, dist);

	if (!self)
	{
		return;
	}

	/* check for noticing a player */
	if (FindTarget(self))
	{
		return;
	}

	if ((self->monsterinfo.search) &&
		(level.time > self->monsterinfo.idle_time))
	{
		if (self->monsterinfo.idle_time)
		{
			self->monsterinfo.search(self);
			self->monsterinfo.idle_time = level.time + 15 + random() * 15;
		}
		else
		{
			self->monsterinfo.idle_time = level.time + random() * 15;
		}
	}
}

/*
 * Turns towards target and advances
 * Use this call with a distnace of 0
 * to replace ai_face
 */
void
ai_charge(edict_t *self, float dist)
{
	vec3_t v;
	float ofs;

	if (!self)
	{
		return;
	}

	if (!self->enemy || !self->enemy->inuse)
	{
		return;
	}

	if (visible(self, self->enemy))
	{
		VectorCopy(self->enemy->s.origin, self->monsterinfo.blind_fire_target);
	}

	if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
	{
		VectorSubtract(self->enemy->s.origin, self->s.origin, v);
		self->ideal_yaw = vectoyaw(v);
	}

	M_ChangeYaw(self);

	if (dist)
	{
		if (self->monsterinfo.aiflags & AI_CHARGING)
		{
			M_MoveToGoal(self, dist);
			return;
		}

		/* circle strafe support */
		if (self->monsterinfo.attack_state == AS_SLIDING)
		{
			/* if we're fighting a tesla, NEVER circle strafe */
			if ((self->enemy) && (self->enemy->classname) &&
				(!strcmp(self->enemy->classname, "tesla")))
			{
				ofs = 0;
			}
			else if (self->monsterinfo.lefty)
			{
				ofs = 90;
			}
			else
			{
				ofs = -90;
			}

			if (M_walkmove(self, self->ideal_yaw + ofs, dist))
			{
				return;
			}

			self->monsterinfo.lefty = 1 - self->monsterinfo.lefty;
			M_walkmove(self, self->ideal_yaw - ofs, dist);
		}
		else
		{
			M_walkmove(self, self->s.angles[YAW], dist);
		}
	}
}

/*
 * Don't move, but turn towards
 * ideal_yaw. Distance is for
 * slight position adjustments
 * needed by the animations
 */
void
ai_turn(edict_t *self, float dist)
{
	if (!self)
	{
		return;
	}

	if (dist)
	{
		M_walkmove(self, self->s.angles[YAW], dist);
	}

	if (FindTarget(self))
	{
		return;
	}

	if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
	{
		M_ChangeYaw(self);
	}
}

/* ========================================================================== */

/*
 * .enemy
 * Will be world if not currently angry at anyone.
 *
 * .movetarget
 * The next path spot to walk toward.  If .enemy, ignore .movetarget.
 * When an enemy is killed, the monster will try to return to it's path.
 *
 * .hunt_time
 * Set to time + something when the player is in sight, but movement straight for
 * him is blocked.  This causes the monster to use wall following code for
 * movement direction instead of sighting on the player.
 *
 * .ideal_yaw
 * A yaw angle of the intended direction, which will be turned towards at up
 * to 45 deg / state.  If the enemy is in view and hunt_time is not active,
 * this will be the exact line towards the enemy.
 *
 * .pausetime
 * A monster will leave it's stand state and head towards it's .movetarget when
 * time > .pausetime.
 */

/* ========================================================================== */

/*
 * returns the range catagorization of an entity reletive to self
 * 0	melee range, will become hostile even if back is turned
 * 1	visibility and infront, or visibility and show hostile
 * 2	infront and show hostile
 * 3	only triggered by damage
 */
int
range(edict_t *self, edict_t *other)
{
	vec3_t v;
	float len;

 	if (!self || !other)
	{
		return 0;
	}

	VectorSubtract(self->s.origin, other->s.origin, v);
	len = VectorLength(v);

	if (len < MELEE_DISTANCE)
	{
		return RANGE_MELEE;
	}

	if (len < 500)
	{
		return RANGE_NEAR;
	}

	if (len < 1000)
	{
		return RANGE_MID;
	}

	return RANGE_FAR;
}

/*
 * returns 1 if the entity is visible
 * to self, even if not infront
 */
qboolean
visible(edict_t *self, edict_t *other)
{
	vec3_t spot1;
	vec3_t spot2;
	trace_t trace;

	if (!self || !other)
	{
		return false;
	}

	VectorCopy(self->s.origin, spot1);
	spot1[2] += self->viewheight;
	VectorCopy(other->s.origin, spot2);
	spot2[2] += other->viewheight;
	trace = gi.trace(spot1, vec3_origin, vec3_origin, spot2, self, MASK_OPAQUE);

	if ((trace.fraction == 1.0) || (trace.ent == other))
	{
		return true;
	}

	return false;
}

/*
 * returns 1 if the entity is in
 * front (in sight) of self
 */
qboolean
infront(edict_t *self, edict_t *other)
{
	vec3_t vec;
	float dot;
	vec3_t forward;

	if ((self == NULL) || (other == NULL))
	{
		return false;
	}

	AngleVectors(self->s.angles, forward, NULL, NULL);

	if ((self == NULL) || (other == NULL))
	{
		return false;
	}

	VectorSubtract(other->s.origin, self->s.origin, vec);
	VectorNormalize(vec);
	dot = DotProduct(vec, forward);

	if (dot > 0.3)
	{
		return true;
	}

	return false;
}

/* ============================================================================ */

void
HuntTarget(edict_t *self)
{
	vec3_t vec;

	if (!self)
	{
		return;
	}

	self->goalentity = self->enemy;

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		self->monsterinfo.stand(self);
	}
	else
	if (self->monsterinfo.run)
	{
		self->monsterinfo.run(self);
	}

	if(visible(self, self->enemy))
	{
		VectorSubtract(self->enemy->s.origin, self->s.origin, vec);
	}

	self->ideal_yaw = vectoyaw(vec);

	/* wait a while before first attack */
	if (!(self->monsterinfo.aiflags & AI_STAND_GROUND))
	{
		AttackFinished(self, 1);
	}
}

void
FoundTarget(edict_t *self)
{
	if (!self|| !self->enemy || !self->enemy->inuse)
	{
		return;
	}

	/* let other monsters see this monster for a while */
	if (self->enemy->client)
	{
		if (self->enemy->flags & FL_DISGUISED)
		{
			self->enemy->flags &= ~FL_DISGUISED;
		}

		level.sight_entity = self;
		level.sight_entity_framenum = level.framenum;
		level.sight_entity->light_level = 128;
	}

	self->show_hostile = (int)level.time + 1; /* wake up other monsters */

	VectorCopy(self->enemy->s.origin, self->monsterinfo.last_sighting);
	self->monsterinfo.trail_time = level.time;
	VectorCopy(self->enemy->s.origin, self->monsterinfo.blind_fire_target);
	self->monsterinfo.blind_fire_delay = 0;

	if (!self->combattarget)
	{
		HuntTarget(self);
		return;
	}

	self->goalentity = self->movetarget = G_PickTarget(self->combattarget);

	if (!self->movetarget)
	{
		self->goalentity = self->movetarget = self->enemy;
		HuntTarget(self);
		gi.dprintf("%s at %s, combattarget %s not found\n",
				self->classname, vtos(self->s.origin),
				self->combattarget);
		return;
	}

	/* clear out our combattarget, these are a one shot deal */
	self->combattarget = NULL;
	self->monsterinfo.aiflags |= AI_COMBAT_POINT;

	/* clear the targetname, that point is ours! */
	self->movetarget->targetname = NULL;
	self->monsterinfo.pausetime = 0;

	/* run for it */
	self->monsterinfo.run(self);
}

/*
 * Self is currently not attacking anything,
 * so try to find a target
 *
 * Returns TRUE if an enemy was sighted
 *
 * When a player fires a missile, the point
 * of impact becomes a fakeplayer so that
 * monsters that see the impact will respond
 * as if they had seen the player.
 *
 * To avoid spending too much time, only
 * a single client (or fakeclient) is
 * checked each frame. This means multi
 * player games will have slightly
 * slower noticing monsters.
 */
qboolean
FindTarget(edict_t *self)
{
	edict_t *client;
	qboolean heardit;
	int r;

	if (!self)
	{
		return false;
	}

	if (self->monsterinfo.aiflags & AI_GOOD_GUY)
	{
		return false;
	}

	/* if we're going to a combat point, just proceed */
	if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
	{
		return false;
	}

	/* if the first spawnflag bit is set, the monster
	   will only wake up on really seeing the player,
	   not another monster getting angry or hearing
	   something */

	heardit = false;

	if ((level.sight_entity_framenum >= (level.framenum - 1)) &&
		!(self->spawnflags & 1))
	{
		client = level.sight_entity;

		if (client->enemy == self->enemy)
		{
			return false;
		}
	}
	else if (level.disguise_violation_framenum > level.framenum)
	{
		client = level.disguise_violator;
	}
	else if (level.sound_entity_framenum >= (level.framenum - 1))
	{
		client = level.sound_entity;
		heardit = true;
	}
	else if (!(self->enemy) &&
			 (level.sound2_entity_framenum >= (level.framenum - 1)) &&
			 !(self->spawnflags & 1))
	{
		client = level.sound2_entity;
		heardit = true;
	}
	else
	{
		client = level.sight_client;

		if (!client)
		{
			return false; /* no clients to get mad at */
		}
	}

	/* if the entity went away, forget it */
	if (!client->inuse)
	{
		return false;
	}

	if (client == self->enemy)
	{
		return true;
	}

	if ((self->monsterinfo.aiflags & AI_HINT_PATH) && (coop) && (coop->value))
	{
		heardit = false;
	}

	if (client->client)
	{
		if (client->flags & FL_NOTARGET)
		{
			return false;
		}
	}
	else if (client->svflags & SVF_MONSTER)
	{
		if (!client->enemy)
		{
			return false;
		}

		if (client->enemy->flags & FL_NOTARGET)
		{
			return false;
		}
	}
	else if (heardit)
	{
		if ((client->owner) && (client->owner->flags & FL_NOTARGET))
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	if (!heardit)
	{
		r = range(self, client);

		if (r == RANGE_FAR)
		{
			return false;
		}

		/* is client in an spot too dark to be seen? */
		if (client->light_level <= 5)
		{
			return false;
		}

		if (!visible(self, client))
		{
			return false;
		}

		if (r == RANGE_NEAR)
		{
			if ((client->show_hostile < (int)level.time) && !infront(self, client))
			{
				return false;
			}
		}
		else if (r == RANGE_MID)
		{
			if (!infront(self, client))
			{
				return false;
			}
		}

		self->enemy = client;

		if (strcmp(self->enemy->classname, "player_noise") != 0)
		{
			self->monsterinfo.aiflags &= ~AI_SOUND_TARGET;

			if (!self->enemy->client)
			{
				self->enemy = self->enemy->enemy;

				if (!self->enemy->client)
				{
					self->enemy = NULL;
					return false;
				}
			}
		}
	}
	else /* heardit */
	{
		vec3_t temp;

		if (self->spawnflags & 1)
		{
			if (!visible(self, client))
			{
				return false;
			}
		}
		else
		{
			if (!gi.inPHS(self->s.origin, client->s.origin))
			{
				return false;
			}
		}

		VectorSubtract(client->s.origin, self->s.origin, temp);

		if (VectorLength(temp) > 1000) /* too far to hear */
		{
			return false;
		}

		/* check area portals - if they are different
		   and not connected then we can't hear it */
		if (client->areanum != self->areanum)
		{
			if (!gi.AreasConnected(self->areanum, client->areanum))
			{
				return false;
			}
		}

		self->ideal_yaw = vectoyaw(temp);

		if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
		{
			M_ChangeYaw(self);
		}

		/* hunt the sound for a bit; hopefully find the real player */
		self->monsterinfo.aiflags |= AI_SOUND_TARGET;
		self->enemy = client;
	}

	/* if we got an enemy, we need to bail out of
	   hint paths, so take over here */
	if (self->monsterinfo.aiflags & AI_HINT_PATH)
	{
		/* this calls foundtarget for us */
		hintpath_stop(self);
	}
	else
	{
		FoundTarget(self);
	}

	if (!(self->monsterinfo.aiflags & AI_SOUND_TARGET) &&
		(self->monsterinfo.sight))
	{
		self->monsterinfo.sight(self, self->enemy);
	}

	return true;
}

/* ============================================================================= */

qboolean
FacingIdeal(edict_t *self)
{
	float delta;

	if (!self)
	{
		return false;
	}

	delta = anglemod(self->s.angles[YAW] - self->ideal_yaw);

	if ((delta > 45) && (delta < 315))
	{
		return false;
	}

	return true;
}

/* ============================================================================= */

qboolean
M_CheckAttack(edict_t *self)
{
	vec3_t spot1, spot2;
	float chance;
	trace_t tr;

	if (!self || !self->enemy || !self->enemy->inuse)
	{
		return false;
	}

	if (self->enemy->health > 0)
	{
		/* see if any entities are in the way of the shot */
		VectorCopy(self->s.origin, spot1);
		spot1[2] += self->viewheight;
		VectorCopy(self->enemy->s.origin, spot2);
		spot2[2] += self->enemy->viewheight;

		tr = gi.trace(spot1, NULL, NULL, spot2, self,
				CONTENTS_SOLID | CONTENTS_MONSTER | CONTENTS_SLIME | CONTENTS_LAVA |
				CONTENTS_WINDOW);

		/* do we have a clear shot? */
		if (tr.ent != self->enemy)
		{
			/* we want them to go ahead and shoot at info_notnulls if they can. */
			if ((self->enemy->solid != SOLID_NOT) || (tr.fraction < 1.0))
			{
				/* if we can't see our target, and we're not
				   blocked by a monster, go into blind fire
				   if available */
				if ((!(tr.ent->svflags & SVF_MONSTER)) &&
					(!visible(self, self->enemy)))
				{
					if ((self->monsterinfo.blindfire) &&
						(self->monsterinfo.blind_fire_delay <= 20.0))
					{
						if (level.time < self->monsterinfo.attack_finished)
						{
							return false;
						}

						if (level.time <
							(self->monsterinfo.trail_time +
							 self->monsterinfo.blind_fire_delay))
						{
							/* wait for our time */
							return false;
						}
						else
						{
							/* make sure we're not going to shoot a monster */
							tr = gi.trace(spot1, NULL, NULL,
									self->monsterinfo.blind_fire_target,
									self, CONTENTS_MONSTER);

							if (tr.allsolid || tr.startsolid ||
								((tr.fraction < 1.0) &&
								 (tr.ent != self->enemy)))
							{
								return false;
							}

							self->monsterinfo.attack_state = AS_BLIND;
							return true;
						}
					}
				}

				return false;
			}
		}
	}

	/* melee attack */
	if (enemy_range == RANGE_MELEE)
	{
		/* don't always melee in easy mode */
		if ((skill->value == 0) && (rand() & 3))
		{
			/* fix for melee only monsters & strafing */
			self->monsterinfo.attack_state = AS_STRAIGHT;
			return false;
		}

		if (self->monsterinfo.melee)
		{
			self->monsterinfo.attack_state = AS_MELEE;
		}
		else
		{
			self->monsterinfo.attack_state = AS_MISSILE;
		}

		return true;
	}

	/* missile attack */
	if (!self->monsterinfo.attack)
	{
		/* fix for melee only monsters & strafing */
		self->monsterinfo.attack_state = AS_STRAIGHT;
		return false;
	}

	if (level.time < self->monsterinfo.attack_finished)
	{
		return false;
	}

	if (enemy_range == RANGE_FAR)
	{
		return false;
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		chance = 0.4;
	}
	else if (enemy_range == RANGE_MELEE)
	{
		chance = 0.2;
	}
	else if (enemy_range == RANGE_NEAR)
	{
		chance = 0.1;
	}
	else if (enemy_range == RANGE_MID)
	{
		chance = 0.02;
	}
	else
	{
		return false;
	}

	if (skill->value == 0)
	{
		chance *= 0.5;
	}
	else if (skill->value >= 2)
	{
		chance *= 2;
	}

	/* go ahead and shoot every time if it's a info_notnull */
	if ((random() < chance) || (self->enemy->solid == SOLID_NOT))
	{
		self->monsterinfo.attack_state = AS_MISSILE;
		self->monsterinfo.attack_finished = level.time + 2 * random();
		return true;
	}

	/* daedalus should strafe more.. this can be done
	   here or in a customized check_attack code for
	   the hover. */
	if (self->flags & FL_FLY)
	{
		/* originally, just 0.3 */
		float strafe_chance;

		if (!(strcmp(self->classname, "monster_daedalus")))
		{
			strafe_chance = 0.8;
		}
		else
		{
			strafe_chance = 0.6;
		}

		/* if enemy is tesla, never strafe */
		if ((self->enemy) && (self->enemy->classname) &&
			(!strcmp(self->enemy->classname, "tesla")))
		{
			strafe_chance = 0;
		}

		if (random() < strafe_chance)
		{
			self->monsterinfo.attack_state = AS_SLIDING;
		}
		else
		{
			self->monsterinfo.attack_state = AS_STRAIGHT;
		}
	}
	else
	{
		/* do we want the monsters strafing? */
		if (random() < 0.4)
		{
			self->monsterinfo.attack_state = AS_SLIDING;
		}
		else
		{
			self->monsterinfo.attack_state = AS_STRAIGHT;
		}
	}

	return false;
}

/*
 * Turn and close until within an
 * angle to launch a melee attack
 */
void
ai_run_melee(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->ideal_yaw = enemy_yaw;

	if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
	{
		M_ChangeYaw(self);
	}

	if (FacingIdeal(self))
	{
		if (self->monsterinfo.melee)
		{
			self->monsterinfo.melee(self);
			self->monsterinfo.attack_state = AS_STRAIGHT;
		}
	}
}

/*
 * Turn in place until within an
 * angle to launch a missile attack
 */
void
ai_run_missile(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->ideal_yaw = enemy_yaw;

	if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
	{
		M_ChangeYaw(self);
	}

	if (FacingIdeal(self))
	{
		if (self->monsterinfo.attack)
		{
			self->monsterinfo.attack(self);

			if ((self->monsterinfo.attack_state == AS_MISSILE) ||
			    (self->monsterinfo.attack_state == AS_BLIND)) {
				self->monsterinfo.attack_state = AS_STRAIGHT;
			}
		}
	}
}

/*
 * Strafe sideways, but stay at
 * aproximately the same range
 */
void
ai_run_slide(edict_t *self, float distance)
{
	float ofs;
	float angle;

	if (!self)
	{
		return;
	}

	self->ideal_yaw = enemy_yaw;
	angle = 90;

	if (self->monsterinfo.lefty)
	{
		ofs = angle;
	}
	else
	{
		ofs = -angle;
	}

	if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
	{
		M_ChangeYaw(self);
	}

	/* clamp maximum sideways move for non flyers to make them look less jerky */
	if (!(self->flags & FL_FLY))
	{
		distance = min(distance, 0.8);
	}

	if (M_walkmove(self, self->ideal_yaw + ofs, distance))
	{
		return;
	}

	/* if we're dodging, give up on it and go straight */
	if (self->monsterinfo.aiflags & AI_DODGING)
	{
		monster_done_dodge(self);
		self->monsterinfo.attack_state = AS_STRAIGHT;
		return;
	}

	self->monsterinfo.lefty = 1 - self->monsterinfo.lefty;

	if (M_walkmove(self, self->ideal_yaw - ofs, distance))
	{
		return;
	}

	/* if we're dodging, give up on it and go straight */
	if (self->monsterinfo.aiflags & AI_DODGING)
	{
		monster_done_dodge(self);
	}

	/* the move failed, so signal the caller (ai_run) to try going straight */
	self->monsterinfo.attack_state = AS_STRAIGHT;
}

/*
 * Decides if we're going to attack
 * or do something else used by
 * ai_run and ai_stand
 */
qboolean
ai_checkattack(edict_t *self, float dist)
{
	vec3_t temp;
	qboolean hesDeadJim;
	qboolean retval;

	if (!self || !self->enemy || !self->enemy->inuse)
	{
		enemy_vis = false;

		return false;
	}

	/* this causes monsters to run blindly
	   to the combat point w/o firing */
	if (self->goalentity)
	{
		if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
		{
			return false;
		}

		if ((self->monsterinfo.aiflags & AI_SOUND_TARGET) && !visible(self, self->goalentity))
		{
			if ((level.time - self->enemy->last_sound_time) > 5.0)
			{
				if (self->goalentity == self->enemy)
				{
					if (self->movetarget)
					{
						self->goalentity = self->movetarget;
					}
					else
					{
						self->goalentity = NULL;
					}
				}

				self->monsterinfo.aiflags &= ~AI_SOUND_TARGET;

				if (self->monsterinfo.aiflags & AI_TEMP_STAND_GROUND)
				{
					self->monsterinfo.aiflags &= ~(AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
				}
			}
			else
			{
				self->show_hostile = (int)level.time + 1;
				return false;
			}
		}
	}

	enemy_vis = false;

	/* see if the enemy is dead */
	hesDeadJim = false;

	if ((!self->enemy) || (!self->enemy->inuse))
	{
		hesDeadJim = true;
	}
	else if (self->monsterinfo.aiflags & AI_MEDIC)
	{
		if (!(self->enemy->inuse) || (self->enemy->health > 0))
		{
			hesDeadJim = true;
		}
	}
	else
	{
		if (self->monsterinfo.aiflags & AI_BRUTAL)
		{
			if (self->enemy->health <= -80)
			{
				hesDeadJim = true;
			}
		}
		else
		{
			if (self->enemy->health <= 0)
			{
				hesDeadJim = true;
			}
		}
	}

	if (hesDeadJim)
	{
		self->monsterinfo.aiflags &= ~AI_MEDIC;
		self->enemy = NULL;

		if (self->oldenemy && (self->oldenemy->health > 0))
		{
			self->enemy = self->oldenemy;
			self->oldenemy = NULL;
			HuntTarget(self);
		}
		else if (self->monsterinfo.last_player_enemy &&
				 (self->monsterinfo.last_player_enemy->health > 0))
		{
			self->enemy = self->monsterinfo.last_player_enemy;
			self->oldenemy = NULL;
			self->monsterinfo.last_player_enemy = NULL;
			HuntTarget(self);
		}
		else
		{
			if (self->movetarget)
			{
				self->goalentity = self->movetarget;
				self->monsterinfo.walk(self);
			}
			else
			{
				/* we need the pausetime otherwise the stand code
				   will just revert to walking with no target and
				   the monsters will wonder around aimlessly trying
				   to hunt the world entity */
				self->monsterinfo.pausetime = level.time + 100000000;
				self->monsterinfo.stand(self);
			}

			return true;
		}
	}

	self->show_hostile = (int)level.time + 1; /* wake up other monsters */

	/* check knowledge of enemy */
	enemy_vis = visible(self, self->enemy);

	if (enemy_vis)
	{
		self->monsterinfo.search_time = level.time + 5;
		VectorCopy(self->enemy->s.origin, self->monsterinfo.last_sighting);
		self->monsterinfo.aiflags &= ~AI_LOST_SIGHT;
		self->monsterinfo.trail_time = level.time;
		VectorCopy(self->enemy->s.origin, self->monsterinfo.blind_fire_target);
		self->monsterinfo.blind_fire_delay = 0;
	}
	
	if (coop && coop->value && (self->monsterinfo.search_time < level.time))
	{
		if (FindTarget(self))
		{
			return true;
		}
	}

	if (self->enemy)
	{
		enemy_infront = infront(self, self->enemy);
		enemy_range = range(self, self->enemy);
		VectorSubtract(self->enemy->s.origin, self->s.origin, temp);
		enemy_yaw = vectoyaw(temp);
	}

	retval = self->monsterinfo.checkattack(self);

	if (retval)
	{
		if (self->monsterinfo.attack_state == AS_MISSILE)
		{
			ai_run_missile(self);
			return true;
		}

		if (self->monsterinfo.attack_state == AS_MELEE)
		{
			ai_run_melee(self);
			return true;
		}

		/* added so monsters can shoot blind */
		if (self->monsterinfo.attack_state == AS_BLIND)
		{
			ai_run_missile(self);
			return true;
		}

		/* if enemy is not currently visible,
		   we will never attack */
		if (!enemy_vis)
		{
			return false;
		}
	}

	return retval;
}

/*
 * The monster has an enemy
 * it is trying to kill
 */
void
ai_run(edict_t *self, float dist)
{
	vec3_t v;
	edict_t *tempgoal;
	edict_t *save;
	qboolean new;
	edict_t *marker;
	float d1, d2;
	trace_t tr;
	vec3_t v_forward, v_right;
	float left, center, right;
	vec3_t left_target, right_target;
	qboolean retval;
	qboolean alreadyMoved = false;
	qboolean gotcha = false;
	edict_t *realEnemy;

	if (!self || !self->enemy || !self->enemy->inuse)
	{
		return;
	}

	/* if we're going to a combat point, just proceed */
	if (self->monsterinfo.aiflags & AI_COMBAT_POINT)
	{
		M_MoveToGoal(self, dist);
		return;
	}

	if (self->monsterinfo.aiflags & AI_DUCKED)
	{
		self->monsterinfo.aiflags &= ~AI_DUCKED;
	}

	if (self->maxs[2] != self->monsterinfo.base_height)
	{
		monster_duck_up(self);
	}

	/* if we're currently looking for a hint path */
	if (self->monsterinfo.aiflags & AI_HINT_PATH)
	{
		M_MoveToGoal(self, dist);

		if (!self->inuse)
		{
			return;
		}

		/* first off, make sure we're looking for
		   the player, not a noise he made */
		if (self->enemy)
		{
			if (self->enemy->inuse)
			{
				if (strcmp(self->enemy->classname, "player_noise") != 0)
				{
					realEnemy = self->enemy;
				}
				else if (self->enemy->owner)
				{
					realEnemy = self->enemy->owner;
				}
				else /* uh oh, can't figure out enemy, bail */
				{
					self->enemy = NULL;
					hintpath_stop(self);
					return;
				}
			}
			else
			{
				self->enemy = NULL;
				hintpath_stop(self);
				return;
			}
		}
		else
		{
			hintpath_stop(self);
			return;
		}

		if (coop && coop->value)
		{
			/* if we're in coop, check my real enemy first..
			   if I SEE him, set gotcha to true */
			if (self->enemy && visible(self, realEnemy))
			{
				gotcha = true;
			}
			else /* otherwise, let FindTarget bump us out of hint paths, if appropriate */
			{
				FindTarget(self);
			}
		}
		else
		{
			if (self->enemy && visible(self, realEnemy))
			{
				gotcha = true;
			}
		}

		/* if we see the player, stop following hintpaths. */
		if (gotcha)
		{
			/* disconnect from hintpaths and start looking normally for players. */
			hintpath_stop(self);
		}

		return;
	}

	if (self->monsterinfo.aiflags & AI_SOUND_TARGET)
	{
		/* paranoia checking */
		if (self->enemy)
		{
			VectorSubtract(self->s.origin, self->enemy->s.origin, v);
		}

		if ((!self->enemy) || (VectorLength(v) < 64))
		{
			self->monsterinfo.aiflags |= (AI_STAND_GROUND | AI_TEMP_STAND_GROUND);
			self->monsterinfo.stand(self);
			return;
		}

		M_MoveToGoal(self, dist);
		/* prevent double moves for sound_targets */
		alreadyMoved = true;

		if (!self->inuse)
		{
			return;
		}

		if (!FindTarget(self))
		{
			return;
		}
	}

	retval = ai_checkattack(self, dist);

	/* don't strafe if we can't see our enemy */
	if ((!enemy_vis) && (self->monsterinfo.attack_state == AS_SLIDING))
	{
		self->monsterinfo.attack_state = AS_STRAIGHT;
	}

	/* unless we're dodging (dodging out of view looks smart) */
	if (self->monsterinfo.aiflags & AI_DODGING)
	{
		self->monsterinfo.attack_state = AS_SLIDING;
	}

	if (self->monsterinfo.attack_state == AS_SLIDING)
	{
		/* protect against double moves */
		if (!alreadyMoved)
		{
			ai_run_slide(self, dist);
		}

		/* we're using attack_state as the return value out of
		   ai_run_slide to indicate whether or not the move
		   succeeded.  If the move succeeded, and we're still
		   sliding, we're done in here (since we've  had our
		   chance to shoot in ai_checkattack, and have moved).
		   if the move failed, our state is as_straight, and
		   it will be taken care of below */
		if ((!retval) && (self->monsterinfo.attack_state == AS_SLIDING))
		{
			return;
		}
	}
	else if (self->monsterinfo.aiflags & AI_CHARGING)
	{
		self->ideal_yaw = enemy_yaw;

		if (!(self->monsterinfo.aiflags & AI_MANUAL_STEERING))
		{
			M_ChangeYaw(self);
		}
	}

	if (retval)
	{
		if ((dist != 0) && (!alreadyMoved) &&
			(self->monsterinfo.attack_state == AS_STRAIGHT) &&
			(!(self->monsterinfo.aiflags & AI_STAND_GROUND)))
		{
			M_MoveToGoal(self, dist);
		}

		if ((self->enemy) && (self->enemy->inuse) && (enemy_vis))
		{
			self->monsterinfo.aiflags &= ~AI_LOST_SIGHT;
			VectorCopy(self->enemy->s.origin, self->monsterinfo.last_sighting);
			self->monsterinfo.trail_time = level.time;
			VectorCopy(self->enemy->s.origin, self->monsterinfo.blind_fire_target);
			self->monsterinfo.blind_fire_delay = 0;
		}

		return;
	}

	if ((self->enemy) && (self->enemy->inuse) && (enemy_vis))
	{
		/* check for alreadyMoved */
		if (!alreadyMoved)
		{
			M_MoveToGoal(self, dist);
		}

		if (!self->inuse)
		{
			return;
		}

		self->monsterinfo.aiflags &= ~AI_LOST_SIGHT;
		VectorCopy(self->enemy->s.origin, self->monsterinfo.last_sighting);
		self->monsterinfo.trail_time = level.time;
		VectorCopy(self->enemy->s.origin, self->monsterinfo.blind_fire_target);
		self->monsterinfo.blind_fire_delay = 0;

		return;
	}

	if ((self->monsterinfo.trail_time + 5) <= level.time)
	{
		/* and we haven't checked for valid hint paths in the last 10 seconds */
		if ((self->monsterinfo.last_hint_time + 10) <= level.time)
		{
			/* check for hint_paths. */
			self->monsterinfo.last_hint_time = level.time;

			if (monsterlost_checkhint(self))
			{
				return;
			}
		}
	}

	if ((self->monsterinfo.search_time) &&
		(level.time > (self->monsterinfo.search_time + 20)))
	{
		/* double move protection */
		if (!alreadyMoved)
		{
			M_MoveToGoal(self, dist);
		}

		self->monsterinfo.search_time = 0;
		return;
	}

	save = self->goalentity;
	tempgoal = G_Spawn();
	self->goalentity = tempgoal;

	new = false;

	if (!(self->monsterinfo.aiflags & AI_LOST_SIGHT))
	{
		/* just lost sight of the player, decide where to go first */
		self->monsterinfo.aiflags |= (AI_LOST_SIGHT | AI_PURSUIT_LAST_SEEN);
		self->monsterinfo.aiflags &= ~(AI_PURSUE_NEXT | AI_PURSUE_TEMP);
		new = true;
	}

	if (self->monsterinfo.aiflags & AI_PURSUE_NEXT)
	{
		self->monsterinfo.aiflags &= ~AI_PURSUE_NEXT;

		/* give ourself more time since we got this far */
		self->monsterinfo.search_time = level.time + 5;

		if (self->monsterinfo.aiflags & AI_PURSUE_TEMP)
		{
			self->monsterinfo.aiflags &= ~AI_PURSUE_TEMP;
			marker = NULL;
			VectorCopy(self->monsterinfo.saved_goal, self->monsterinfo.last_sighting);
			new = true;
		}
		else if (self->monsterinfo.aiflags & AI_PURSUIT_LAST_SEEN)
		{
			self->monsterinfo.aiflags &= ~AI_PURSUIT_LAST_SEEN;
			marker = PlayerTrail_PickFirst(self);
		}
		else
		{
			marker = PlayerTrail_PickNext(self);
		}

		if (marker)
		{
			VectorCopy(marker->s.origin, self->monsterinfo.last_sighting);
			self->monsterinfo.trail_time = marker->timestamp;
			self->s.angles[YAW] = self->ideal_yaw = marker->s.angles[YAW];
			new = true;
		}
	}

	VectorSubtract(self->s.origin, self->monsterinfo.last_sighting, v);
	d1 = VectorLength(v);

	if (d1 <= dist)
	{
		self->monsterinfo.aiflags |= AI_PURSUE_NEXT;
		dist = d1;
	}

	VectorCopy(self->monsterinfo.last_sighting, self->goalentity->s.origin);

	if (new)
	{
		tr = gi.trace(self->s.origin, self->mins, self->maxs,
				self->monsterinfo.last_sighting, self,
				MASK_PLAYERSOLID);

		if (tr.fraction < 1)
		{
			VectorSubtract(self->goalentity->s.origin, self->s.origin, v);
			d1 = VectorLength(v);
			center = tr.fraction;
			d2 = d1 * ((center + 1) / 2);
			self->s.angles[YAW] = self->ideal_yaw = vectoyaw(v);
			AngleVectors(self->s.angles, v_forward, v_right, NULL);

			VectorSet(v, d2, -16, 0);
			G_ProjectSource(self->s.origin, v, v_forward, v_right, left_target);
			tr = gi.trace(self->s.origin, self->mins, self->maxs,
					left_target, self, MASK_PLAYERSOLID);
			left = tr.fraction;

			VectorSet(v, d2, 16, 0);
			G_ProjectSource(self->s.origin, v, v_forward, v_right, right_target);
			tr = gi.trace(self->s.origin, self->mins, self->maxs, right_target,
					self, MASK_PLAYERSOLID);
			right = tr.fraction;

			center = (d1 * center) / d2;

			if ((left >= center) && (left > right))
			{
				if (left < 1)
				{
					VectorSet(v, d2 * left * 0.5, -16, 0);
					G_ProjectSource(self->s.origin, v, v_forward,
							v_right, left_target);
				}

				VectorCopy(self->monsterinfo.last_sighting, self->monsterinfo.saved_goal);
				self->monsterinfo.aiflags |= AI_PURSUE_TEMP;
				VectorCopy(left_target, self->goalentity->s.origin);
				VectorCopy(left_target, self->monsterinfo.last_sighting);
				VectorSubtract(self->goalentity->s.origin, self->s.origin, v);
				self->s.angles[YAW] = self->ideal_yaw = vectoyaw(v);
			}
			else if ((right >= center) && (right > left))
			{
				if (right < 1)
				{
					VectorSet(v, d2 * right * 0.5, 16, 0);
					G_ProjectSource(self->s.origin, v, v_forward, v_right,
							right_target);
				}

				VectorCopy(self->monsterinfo.last_sighting,
						self->monsterinfo.saved_goal);
				self->monsterinfo.aiflags |= AI_PURSUE_TEMP;
				VectorCopy(right_target, self->goalentity->s.origin);
				VectorCopy(right_target, self->monsterinfo.last_sighting);
				VectorSubtract(self->goalentity->s.origin, self->s.origin, v);
				self->s.angles[YAW] = self->ideal_yaw = vectoyaw(v);
			}
		}
	}

	M_MoveToGoal(self, dist);

	if (!self->inuse)
	{
		return;
	}

	G_FreeEdict(tempgoal);

	if (self)
	{
		self->goalentity = save;
	}
}
