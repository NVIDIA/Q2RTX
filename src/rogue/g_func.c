/*
 * =======================================================================
 *
 * Level functions. Platforms, buttons, dooors and so on.
 *
 * =======================================================================
 */

#include "header/local.h"

#define PLAT_LOW_TRIGGER 1
#define PLAT2_TOGGLE 2
#define PLAT2_TOP 4
#define PLAT2_TRIGGER_TOP 8
#define PLAT2_TRIGGER_BOTTOM 16
#define PLAT2_BOX_LIFT 32

#define STATE_TOP 0
#define STATE_BOTTOM 1
#define STATE_UP 2
#define STATE_DOWN 3

#define DOOR_START_OPEN 1
#define DOOR_REVERSE 2
#define DOOR_CRUSHER 4
#define DOOR_NOMONSTER 8
#define DOOR_TOGGLE 32
#define DOOR_X_AXIS 64
#define DOOR_Y_AXIS 128
#define DOOR_INACTIVE 8192

#define AccelerationDistance(target, rate) (target * ((target / rate) + 1) / 2)

#define PLAT2_CALLED 1
#define PLAT2_MOVING 2
#define PLAT2_WAITING 4

#define TRAIN_START_ON 1
#define TRAIN_TOGGLE 2
#define TRAIN_BLOCK_STOPS 4

#define SECRET_ALWAYS_SHOOT 1
#define SECRET_1ST_LEFT 2
#define SECRET_1ST_DOWN 4

void door_secret_move1(edict_t *self);
void door_secret_move2(edict_t *self);
void door_secret_move3(edict_t *self);
void door_secret_move4(edict_t *self);
void door_secret_move5(edict_t *self);
void door_secret_move6(edict_t *self);
void door_secret_done(edict_t *self);

void train_next(edict_t *self);
void door_go_down(edict_t *self);
void plat2_go_down(edict_t *ent);
void plat2_go_up(edict_t *ent);
void plat2_spawn_danger_area(edict_t *ent);
void plat2_kill_danger_area(edict_t *ent);
void Think_AccelMove(edict_t *ent);
void plat_go_down(edict_t *ent);

/*
 * =========================================================
 *
 * PLATS
 *
 * movement options:
 *
 * linear
 * smooth start, hard stop
 * smooth start, smooth stop
 *
 * start
 * end
 * acceleration
 * speed
 * deceleration
 * begin sound
 * end sound
 * target fired when reaching end
 * wait at end
 *
 * object characteristics that use move segments
 * ---------------------------------------------
 * movetype_push, or movetype_stop
 * action when touched
 * action when blocked
 * action when used
 *  disabled?
 * auto trigger spawning
 *
 *
 * =========================================================
 */

/* Support routines for movement (changes in origin using velocity) */

void
Move_Done(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	VectorClear(ent->velocity);
	ent->moveinfo.endfunc(ent);
}

void
Move_Final(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (ent->moveinfo.remaining_distance == 0)
	{
		Move_Done(ent);
		return;
	}

	VectorScale(ent->moveinfo.dir,
			ent->moveinfo.remaining_distance / FRAMETIME,
			ent->velocity);

	ent->think = Move_Done;
	ent->nextthink = level.time + FRAMETIME;
}

void
Move_Begin(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	float frames;

	if ((ent->moveinfo.speed * FRAMETIME) >= ent->moveinfo.remaining_distance)
	{
		Move_Final(ent);
		return;
	}

	VectorScale(ent->moveinfo.dir, ent->moveinfo.speed, ent->velocity);
	frames = floor( (ent->moveinfo.remaining_distance /
			 ent->moveinfo.speed) / FRAMETIME);
	ent->moveinfo.remaining_distance -= frames * ent->moveinfo.speed * FRAMETIME;
	ent->nextthink = level.time + (frames * FRAMETIME);
	ent->think = Move_Final;
}

void
Move_Calc(edict_t *ent, vec3_t dest, void (*func)(edict_t *))
{
	if (!ent || !func)
	{
		return;
	}

	VectorClear(ent->velocity);
	VectorSubtract(dest, ent->s.origin, ent->moveinfo.dir);
	ent->moveinfo.remaining_distance = VectorNormalize(ent->moveinfo.dir);
	ent->moveinfo.endfunc = func;

	if ((ent->moveinfo.speed == ent->moveinfo.accel) &&
		(ent->moveinfo.speed == ent->moveinfo.decel))
	{
		if (level.current_entity ==
			((ent->flags & FL_TEAMSLAVE) ? ent->teammaster : ent))
		{
			Move_Begin(ent);
		}
		else
		{
			ent->nextthink = level.time + FRAMETIME;
			ent->think = Move_Begin;
		}
	}
	else
	{
		/* accelerative */
		ent->moveinfo.current_speed = 0;
		ent->think = Think_AccelMove;
		ent->nextthink = level.time + FRAMETIME;
	}
}

/* Support routines for angular movement
  (changes in angle using avelocity) */
void
AngleMove_Done(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	VectorClear(ent->avelocity);
	ent->moveinfo.endfunc(ent);
}

void
AngleMove_Final(edict_t *ent)
{
	vec3_t move;

	if (!ent)
	{
		return;
	}

	if (ent->moveinfo.state == STATE_UP)
	{
		VectorSubtract(ent->moveinfo.end_angles, ent->s.angles, move);
	}
	else
	{
		VectorSubtract(ent->moveinfo.start_angles, ent->s.angles, move);
	}

	if (VectorCompare(move, vec3_origin))
	{
		AngleMove_Done(ent);
		return;
	}

	VectorScale(move, 1.0 / FRAMETIME, ent->avelocity);

	ent->think = AngleMove_Done;
	ent->nextthink = level.time + FRAMETIME;
}

void
AngleMove_Begin(edict_t *ent)
{
	vec3_t destdelta;
	float len;
	float traveltime;
	float frames;

	if (!ent)
	{
		return;
	}

	/* accelerate as needed */
	if (ent->moveinfo.speed < ent->speed)
	{
		ent->moveinfo.speed += ent->accel;

		if (ent->moveinfo.speed > ent->speed)
		{
			ent->moveinfo.speed = ent->speed;
		}
	}

	/* set destdelta to the vector needed to move */
	if (ent->moveinfo.state == STATE_UP)
	{
		VectorSubtract(ent->moveinfo.end_angles, ent->s.angles, destdelta);
	}
	else
	{
		VectorSubtract(ent->moveinfo.start_angles, ent->s.angles, destdelta);
	}

	/* calculate length of vector */
	len = VectorLength(destdelta);

	/* divide by speed to get time to reach dest */
	traveltime = len / ent->moveinfo.speed;

	if (traveltime < FRAMETIME)
	{
		AngleMove_Final(ent);
		return;
	}

	frames = floor(traveltime / FRAMETIME);

	/* scale the destdelta vector by the time spent traveling to get velocity */
	VectorScale(destdelta, 1.0 / traveltime, ent->avelocity);

	/* if we're done accelerating, act as a normal rotation */
	if (ent->moveinfo.speed >= ent->speed)
	{
		/* set nextthink to trigger a think when dest is reached */
		ent->nextthink = level.time + frames * FRAMETIME;
		ent->think = AngleMove_Final;
	}
	else
	{
		ent->nextthink = level.time + FRAMETIME;
		ent->think = AngleMove_Begin;
	}
}

void
AngleMove_Calc(edict_t *ent, void (*func)(edict_t *))
{
	if (!ent || !func)
	{
		return;
	}

	VectorClear(ent->avelocity);
	ent->moveinfo.endfunc = func;

	/* if we're supposed to accelerate, this will
	   tell anglemove_begin to do so */
	if (ent->accel != ent->speed)
	{
		ent->moveinfo.speed = 0;
	}

	if (level.current_entity ==
		((ent->flags & FL_TEAMSLAVE) ? ent->teammaster : ent))
	{
		AngleMove_Begin(ent);
	}
	else
	{
		ent->nextthink = level.time + FRAMETIME;
		ent->think = AngleMove_Begin;
	}
}

/*
 * The team has completed a frame of movement, so
 * change the speed for the next frame
 */

void
plat_CalcAcceleratedMove(moveinfo_t *moveinfo)
{
	float accel_dist;
	float decel_dist;

	if (!moveinfo)
	{
		return;
	}

	moveinfo->move_speed = moveinfo->speed;

	if (moveinfo->remaining_distance < moveinfo->accel)
	{
		moveinfo->current_speed = moveinfo->remaining_distance;
		return;
	}

	accel_dist = AccelerationDistance(moveinfo->speed, moveinfo->accel);
	decel_dist = AccelerationDistance(moveinfo->speed, moveinfo->decel);

	if ((moveinfo->remaining_distance - accel_dist - decel_dist) < 0)
	{
		float f;

		f = (moveinfo->accel + moveinfo->decel) / (moveinfo->accel * moveinfo->decel);
		moveinfo->move_speed = (-2 + sqrt(4 - 4 * f * (-2 * moveinfo->remaining_distance))) / (2 * f);
		decel_dist = AccelerationDistance(moveinfo->move_speed, moveinfo->decel);
	}

	moveinfo->decel_distance = decel_dist;
}

void
plat_Accelerate(moveinfo_t *moveinfo)
{
	if (!moveinfo)
	{
		return;
	}

	/* are we decelerating? */
	if (moveinfo->remaining_distance <= moveinfo->decel_distance)
	{
		if (moveinfo->remaining_distance < moveinfo->decel_distance)
		{
			if (moveinfo->next_speed)
			{
				moveinfo->current_speed = moveinfo->next_speed;
				moveinfo->next_speed = 0;
				return;
			}

			if (moveinfo->current_speed > moveinfo->decel)
			{
				moveinfo->current_speed -= moveinfo->decel;
			}
		}

		return;
	}

	/* are we at full speed and need to start decelerating during this move? */
	if (moveinfo->current_speed == moveinfo->move_speed)
	{
		if ((moveinfo->remaining_distance - moveinfo->current_speed) <
			moveinfo->decel_distance)
		{
			float p1_distance;
			float p2_distance;
			float distance;

			p1_distance = moveinfo->remaining_distance -
						  moveinfo->decel_distance;
			p2_distance = moveinfo->move_speed *
						  (1.0 - (p1_distance / moveinfo->move_speed));
			distance = p1_distance + p2_distance;
			moveinfo->current_speed = moveinfo->move_speed;
			moveinfo->next_speed = moveinfo->move_speed - moveinfo->decel *
								   (p2_distance / distance);
			return;
		}
	}

	/* are we accelerating? */
	if (moveinfo->current_speed < moveinfo->speed)
	{
		float old_speed;
		float p1_distance;
		float p1_speed;
		float p2_distance;
		float distance;

		old_speed = moveinfo->current_speed;

		/* figure simple acceleration up to move_speed */
		moveinfo->current_speed += moveinfo->accel;

		if (moveinfo->current_speed > moveinfo->speed)
		{
			moveinfo->current_speed = moveinfo->speed;
		}

		/* are we accelerating throughout this entire move? */
		if ((moveinfo->remaining_distance - moveinfo->current_speed) >=
			moveinfo->decel_distance)
		{
			return;
		}

		/* during this move we will accelrate from current_speed to move_speed
		   and cross over the decel_distance; figure the average speed for the
		   entire move */
		p1_distance = moveinfo->remaining_distance - moveinfo->decel_distance;
		p1_speed = (old_speed + moveinfo->move_speed) / 2.0;
		p2_distance = moveinfo->move_speed * (1.0 - (p1_distance / p1_speed));
		distance = p1_distance + p2_distance;
		moveinfo->current_speed = (p1_speed * (p1_distance /
		  distance)) + (moveinfo->move_speed * (p2_distance / distance));
		moveinfo->next_speed = moveinfo->move_speed - moveinfo->decel *
							   (p2_distance / distance);
		return;
	}

	/* we are at constant velocity (move_speed) */
	return;
}

void
Think_AccelMove(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	ent->moveinfo.remaining_distance -= ent->moveinfo.current_speed;
	plat_CalcAcceleratedMove(&ent->moveinfo);
	plat_Accelerate(&ent->moveinfo);

	/* will the entire move complete on next frame? */
	if (ent->moveinfo.remaining_distance <= ent->moveinfo.current_speed)
	{
		Move_Final(ent);
		return;
	}

	VectorScale(ent->moveinfo.dir, ent->moveinfo.current_speed * 10,
			ent->velocity);
	ent->nextthink = level.time + FRAMETIME;
	ent->think = Think_AccelMove;
}

void
plat_hit_top(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (!(ent->flags & FL_TEAMSLAVE))
	{
		if (ent->moveinfo.sound_end)
		{
			gi.sound(ent, CHAN_NO_PHS_ADD + CHAN_VOICE, ent->moveinfo.sound_end,
					1, ATTN_STATIC, 0);
		}

		ent->s.sound = 0;
	}

	ent->moveinfo.state = STATE_TOP;

	ent->think = plat_go_down;
	ent->nextthink = level.time + 3;
}

void
plat_hit_bottom(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (!(ent->flags & FL_TEAMSLAVE))
	{
		if (ent->moveinfo.sound_end)
		{
			gi.sound(ent, CHAN_NO_PHS_ADD + CHAN_VOICE,
					ent->moveinfo.sound_end, 1, ATTN_STATIC, 0);
		}

		ent->s.sound = 0;
	}

	ent->moveinfo.state = STATE_BOTTOM;

	plat2_kill_danger_area(ent);
}

void
plat_go_down(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (!(ent->flags & FL_TEAMSLAVE))
	{
		if (ent->moveinfo.sound_start)
		{
			gi.sound(ent, CHAN_NO_PHS_ADD + CHAN_VOICE,
					ent->moveinfo.sound_start, 1,
					ATTN_STATIC, 0);
		}

		ent->s.sound = ent->moveinfo.sound_middle;
	}

	ent->moveinfo.state = STATE_DOWN;
	Move_Calc(ent, ent->moveinfo.end_origin, plat_hit_bottom);
}

void
plat_go_up(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (!(ent->flags & FL_TEAMSLAVE))
	{
		if (ent->moveinfo.sound_start)
		{
			gi.sound(ent, CHAN_NO_PHS_ADD + CHAN_VOICE,
					ent->moveinfo.sound_start, 1,
					ATTN_STATIC, 0);
		}

		ent->s.sound = ent->moveinfo.sound_middle;
	}

	ent->moveinfo.state = STATE_UP;
	Move_Calc(ent, ent->moveinfo.start_origin, plat_hit_top);

	plat2_spawn_danger_area(ent);
}

void
plat_blocked(edict_t *self, edict_t *other)
{
	if (!self || !other)
	{
		return;
	}

	if (!(other->svflags & SVF_MONSTER) && (!other->client))
	{
		/* give it a chance to go away on it's own terms (like gibs) */
		T_Damage(other, self, self, vec3_origin, other->s.origin,
				vec3_origin, 100000, 1, 0, MOD_CRUSH);

		/* if it's still there, nuke it */
		if (other)
		{
			/* Hack for entity without it's origin near the model */
			VectorMA(other->absmin, 0.5, other->size, other->s.origin);
			BecomeExplosion1(other);
		}

		return;
	}

	/* gib dead things */
	if (other->health < 1)
	{
		T_Damage(other, self, self, vec3_origin, other->s.origin,
				vec3_origin, 100, 1, 0, MOD_CRUSH);
	}

	T_Damage(other, self, self, vec3_origin, other->s.origin,
			vec3_origin, self->dmg, 1, 0, MOD_CRUSH);

	if (self->moveinfo.state == STATE_UP)
	{
		plat_go_down(self);
	}
	else if (self->moveinfo.state == STATE_DOWN)
	{
		plat_go_up(self);
	}
}

void
Use_Plat(edict_t *ent, edict_t *other, edict_t *activator /* unused */)
{
	if (!ent || !other)
	{
		return;
	}

	/* if a monster is using us, then allow the activity when stopped. */
	if (other->svflags & SVF_MONSTER)
	{
		if (ent->moveinfo.state == STATE_TOP)
		{
			plat_go_down(ent);
		}
		else if (ent->moveinfo.state == STATE_BOTTOM)
		{
			plat_go_up(ent);
		}

		return;
	}

	if (ent->think)
	{
		return; /* already down */
	}

	plat_go_down(ent);
}

void
Touch_Plat_Center(edict_t *ent, edict_t *other, cplane_t *plane /* unsed */,
		csurface_t *surf /* unused */)
{
	if (!ent || !other)
	{
		return;
	}

	if (!other->client)
	{
		return;
	}

	if (other->health <= 0)
	{
		return;
	}

	ent = ent->enemy; /* now point at the plat, not the trigger */

	if (ent->moveinfo.state == STATE_BOTTOM)
	{
		plat_go_up(ent);
	}
	else if (ent->moveinfo.state == STATE_TOP)
	{
		ent->nextthink = level.time + 1; /* the player is still on the plat, so delay going down */
	}
}

edict_t *
plat_spawn_inside_trigger(edict_t *ent)
{
	edict_t *trigger;
	vec3_t tmin, tmax;

	if (!ent)
	{
		return NULL;
	}

	/* middle trigger */
	trigger = G_Spawn();
	trigger->touch = Touch_Plat_Center;
	trigger->movetype = MOVETYPE_NONE;
	trigger->solid = SOLID_TRIGGER;
	trigger->enemy = ent;

	tmin[0] = ent->mins[0] + 25;
	tmin[1] = ent->mins[1] + 25;
	tmin[2] = ent->mins[2];

	tmax[0] = ent->maxs[0] - 25;
	tmax[1] = ent->maxs[1] - 25;
	tmax[2] = ent->maxs[2] + 8;

	tmin[2] = tmax[2] - (ent->pos1[2] - ent->pos2[2] + st.lip);

	if (ent->spawnflags & PLAT_LOW_TRIGGER)
	{
		tmax[2] = tmin[2] + 8;
	}

	if (tmax[0] - tmin[0] <= 0)
	{
		tmin[0] = (ent->mins[0] + ent->maxs[0]) * 0.5;
		tmax[0] = tmin[0] + 1;
	}

	if (tmax[1] - tmin[1] <= 0)
	{
		tmin[1] = (ent->mins[1] + ent->maxs[1]) * 0.5;
		tmax[1] = tmin[1] + 1;
	}

	VectorCopy(tmin, trigger->mins);
	VectorCopy(tmax, trigger->maxs);

	gi.linkentity(trigger);

	return trigger;
}

/*
 * QUAKED func_plat (0 .5 .8) ? PLAT_LOW_TRIGGER
 *
 * speed -> default 150
 *
 * Plats are always drawn in the extended position,
 * so they will light correctly.
 *
 * If the plat is the target of another trigger or button,
 * it will start out disabled in the extended position until
 * it is trigger, when it will lower and become a normal plat.
 *
 * "speed"	overrides default 200.
 * "accel"  overrides default 500
 * "lip"	overrides default 8 pixel lip
 *
 * If the "height" key is set, that will determine the amount
 * the plat moves, instead of being implicitly determoveinfoned
 * by the model's height.
 *
 * Set "sounds" to one of the following:
 * 1) base fast
 * 2) chain slow
 */
void
SP_func_plat(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	VectorClear(ent->s.angles);
	ent->solid = SOLID_BSP;
	ent->movetype = MOVETYPE_PUSH;

	gi.setmodel(ent, ent->model);

	ent->blocked = plat_blocked;

	if (!ent->speed)
	{
		ent->speed = 20;
	}
	else
	{
		ent->speed *= 0.1;
	}

	if (!ent->accel)
	{
		ent->accel = 5;
	}
	else
	{
		ent->accel *= 0.1;
	}

	if (!ent->decel)
	{
		ent->decel = 5;
	}
	else
	{
		ent->decel *= 0.1;
	}

	if (!ent->dmg)
	{
		ent->dmg = 2;
	}

	if (!st.lip)
	{
		st.lip = 8;
	}

	/* pos1 is the top position, pos2 is the bottom */
	VectorCopy(ent->s.origin, ent->pos1);
	VectorCopy(ent->s.origin, ent->pos2);

	if (st.height)
	{
		ent->pos2[2] -= st.height;
	}
	else
	{
		ent->pos2[2] -= (ent->maxs[2] - ent->mins[2]) - st.lip;
	}

	ent->use = Use_Plat;

	plat_spawn_inside_trigger(ent); /* the "start moving" trigger */

	if (ent->targetname)
	{
		ent->moveinfo.state = STATE_UP;
	}
	else
	{
		VectorCopy(ent->pos2, ent->s.origin);
		gi.linkentity(ent);
		ent->moveinfo.state = STATE_BOTTOM;
	}

	ent->moveinfo.speed = ent->speed;
	ent->moveinfo.accel = ent->accel;
	ent->moveinfo.decel = ent->decel;
	ent->moveinfo.wait = ent->wait;
	VectorCopy(ent->pos1, ent->moveinfo.start_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.start_angles);
	VectorCopy(ent->pos2, ent->moveinfo.end_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.end_angles);

	ent->moveinfo.sound_start = gi.soundindex("plats/pt1_strt.wav");
	ent->moveinfo.sound_middle = gi.soundindex("plats/pt1_mid.wav");
	ent->moveinfo.sound_end = gi.soundindex("plats/pt1_end.wav");
}

void
plat2_spawn_danger_area(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	vec3_t mins, maxs;

	VectorCopy(ent->mins, mins);
	VectorCopy(ent->maxs, maxs);
	maxs[2] = ent->mins[2] + 64;

	SpawnBadArea(mins, maxs, 0, ent);
}

void
plat2_kill_danger_area(edict_t *ent)
{
	edict_t *t;

	if (!ent)
	{
		return;
	}

	t = NULL;

	while ((t = G_Find(t, FOFS(classname), "bad_area")))
	{
		if (t->owner == ent)
		{
			G_FreeEdict(t);
		}
	}
}

void
plat2_hit_top(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (!(ent->flags & FL_TEAMSLAVE))
	{
		if (ent->moveinfo.sound_end)
		{
			gi.sound(ent, CHAN_NO_PHS_ADD + CHAN_VOICE, ent->moveinfo.sound_end,
					1, ATTN_STATIC, 0);
		}

		ent->s.sound = 0;
	}

	ent->moveinfo.state = STATE_TOP;

	if (ent->plat2flags & PLAT2_CALLED)
	{
		ent->plat2flags = PLAT2_WAITING;

		if (!(ent->spawnflags & PLAT2_TOGGLE))
		{
			ent->think = plat2_go_down;
			ent->nextthink = level.time + 5.0;
		}

		if (deathmatch->value)
		{
			ent->last_move_time = level.time - 1.0;
		}
		else
		{
			ent->last_move_time = level.time - 2.0;
		}
	}
	else if (!(ent->spawnflags & PLAT2_TOP) &&
			 !(ent->spawnflags & PLAT2_TOGGLE))
	{
		ent->plat2flags = 0;
		ent->think = plat2_go_down;
		ent->nextthink = level.time + 2.0;
		ent->last_move_time = level.time;
	}
	else
	{
		ent->plat2flags = 0;
		ent->last_move_time = level.time;
	}

	G_UseTargets(ent, ent);
}

void
plat2_hit_bottom(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (!(ent->flags & FL_TEAMSLAVE))
	{
		if (ent->moveinfo.sound_end)
		{
			gi.sound(ent, CHAN_NO_PHS_ADD + CHAN_VOICE,
					ent->moveinfo.sound_end, 1,
					ATTN_STATIC, 0);
		}

		ent->s.sound = 0;
	}

	ent->moveinfo.state = STATE_BOTTOM;

	if (ent->plat2flags & PLAT2_CALLED)
	{
		ent->plat2flags = PLAT2_WAITING;

		if (!(ent->spawnflags & PLAT2_TOGGLE))
		{
			ent->think = plat2_go_up;
			ent->nextthink = level.time + 5.0;
		}

		if (deathmatch->value)
		{
			ent->last_move_time = level.time - 1.0;
		}
		else
		{
			ent->last_move_time = level.time - 2.0;
		}
	}
	else if ((ent->spawnflags & PLAT2_TOP) && !(ent->spawnflags & PLAT2_TOGGLE))
	{
		ent->plat2flags = 0;
		ent->think = plat2_go_up;
		ent->nextthink = level.time + 2.0;
		ent->last_move_time = level.time;
	}
	else
	{
		ent->plat2flags = 0;
		ent->last_move_time = level.time;
	}

	plat2_kill_danger_area(ent);
	G_UseTargets(ent, ent);
}

void
plat2_go_down(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (!(ent->flags & FL_TEAMSLAVE))
	{
		if (ent->moveinfo.sound_start)
		{
			gi.sound(ent, CHAN_NO_PHS_ADD + CHAN_VOICE,
					ent->moveinfo.sound_start, 1,
					ATTN_STATIC, 0);
		}

		ent->s.sound = ent->moveinfo.sound_middle;
	}

	ent->moveinfo.state = STATE_DOWN;
	ent->plat2flags |= PLAT2_MOVING;

	Move_Calc(ent, ent->moveinfo.end_origin, plat2_hit_bottom);
}

void
plat2_go_up(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (!(ent->flags & FL_TEAMSLAVE))
	{
		if (ent->moveinfo.sound_start)
		{
			gi.sound(ent, CHAN_NO_PHS_ADD + CHAN_VOICE,
					ent->moveinfo.sound_start, 1,
					ATTN_STATIC, 0);
		}

		ent->s.sound = ent->moveinfo.sound_middle;
	}

	ent->moveinfo.state = STATE_UP;
	ent->plat2flags |= PLAT2_MOVING;

	plat2_spawn_danger_area(ent);

	Move_Calc(ent, ent->moveinfo.start_origin, plat2_hit_top);
}

void
plat2_operate(edict_t *ent, edict_t *other)
{
	int otherState;
	float pauseTime;
	float platCenter;
	edict_t *trigger;

  	if (!ent || !other)
	{
		return;
	}

	trigger = ent;
	ent = ent->enemy; /* now point at the plat, not the trigger */

	if (ent->plat2flags & PLAT2_MOVING)
	{
		return;
	}

	if ((ent->last_move_time + 2) > level.time)
	{
		return;
	}

	platCenter = (trigger->absmin[2] + trigger->absmax[2]) / 2;

	if (ent->moveinfo.state == STATE_TOP)
	{
		otherState = STATE_TOP;

		if (ent->spawnflags & PLAT2_BOX_LIFT)
		{
			if (platCenter > other->s.origin[2])
			{
				otherState = STATE_BOTTOM;
			}
		}
		else
		{
			if (trigger->absmax[2] > other->s.origin[2])
			{
				otherState = STATE_BOTTOM;
			}
		}
	}
	else
	{
		otherState = STATE_BOTTOM;

		if (other->s.origin[2] > platCenter)
		{
			otherState = STATE_TOP;
		}
	}

	ent->plat2flags = PLAT2_MOVING;

	if (deathmatch->value)
	{
		pauseTime = 0.3;
	}
	else
	{
		pauseTime = 0.5;
	}

	if (ent->moveinfo.state != otherState)
	{
		ent->plat2flags |= PLAT2_CALLED;
		pauseTime = 0.1;
	}

	ent->last_move_time = level.time;

	if (ent->moveinfo.state == STATE_BOTTOM)
	{
		ent->think = plat2_go_up;
		ent->nextthink = level.time + pauseTime;
	}
	else
	{
		ent->think = plat2_go_down;
		ent->nextthink = level.time + pauseTime;
	}
}

void
Touch_Plat_Center2(edict_t *ent, edict_t *other,
		cplane_t *plane /* unused */, csurface_t *surf /* unused */)
{
	if (!ent || !other)
	{
		return;
	}

	/* this requires monsters to actively trigger plats, not just step on them. */
	if (other->health <= 0)
	{
		return;
	}

	/* don't let non-monsters activate plat2s */
	if ((!(other->svflags & SVF_MONSTER)) && (!other->client))
	{
		return;
	}

	plat2_operate(ent, other);
}

void
plat2_blocked(edict_t *self, edict_t *other)
{
	if (!self || !other)
	{
		return;
	}

	if (!(other->svflags & SVF_MONSTER) && (!other->client))
	{
		/* give it a chance to go away on it's own terms (like gibs) */
		T_Damage(other, self, self, vec3_origin, other->s.origin,
				vec3_origin, 100000, 1, 0, MOD_CRUSH);

		/* if it's still there, nuke it */
		if (other && other->inuse)
		{
			BecomeExplosion1(other);
		}

		return;
	}

	/* gib dead things */
	if (other->health < 1)
	{
		T_Damage(other, self, self, vec3_origin, other->s.origin,
				vec3_origin, 100, 1, 0, MOD_CRUSH);
	}

	T_Damage(other, self, self, vec3_origin, other->s.origin,
			vec3_origin, self->dmg, 1, 0, MOD_CRUSH);

	if (self->moveinfo.state == STATE_UP)
	{
		plat2_go_down(self);
	}
	else if (self->moveinfo.state == STATE_DOWN)
	{
		plat2_go_up(self);
	}
}

void
Use_Plat2(edict_t *ent, edict_t *other /* unused */,
	   	edict_t *activator)
{
	edict_t *trigger;
	int i;

	if (!ent || !activator)
	{
		return;
	}

	if (ent->moveinfo.state > STATE_BOTTOM)
	{
		return;
	}

	if ((ent->last_move_time + 2) > level.time)
	{
		return;
	}

	for (i = 1, trigger = g_edicts + 1; i < globals.num_edicts; i++, trigger++)
	{
		if (!trigger->inuse)
		{
			continue;
		}

		if (trigger->touch == Touch_Plat_Center2)
		{
			if (trigger->enemy == ent)
			{
				plat2_operate(trigger, activator);
				return;
			}
		}
	}
}

void
plat2_activate(edict_t *ent, edict_t *other /* unused */,
	   	edict_t *activator /* unused */)
{
	edict_t *trigger;

	if (!ent)
	{
		return;
	}

	ent->use = Use_Plat2;
	trigger = plat_spawn_inside_trigger(ent); /* the "start moving" trigger */

	trigger->maxs[0] += 10;
	trigger->maxs[1] += 10;
	trigger->mins[0] -= 10;
	trigger->mins[1] -= 10;

	gi.linkentity(trigger);

	trigger->touch = Touch_Plat_Center2; /* Override trigger touch function */

	plat2_go_down(ent);
}

/* QUAKED func_plat2 (0 .5 .8) ? PLAT_LOW_TRIGGER PLAT2_TOGGLE PLAT2_TOP PLAT2_TRIGGER_TOP PLAT2_TRIGGER_BOTTOM BOX_LIFT
 * speed default 150
 *
 * PLAT_LOW_TRIGGER - creates a short trigger field at the bottom
 * PLAT2_TOGGLE - plat will not return to default position.
 * PLAT2_TOP - plat's default position will the the top.
 * PLAT2_TRIGGER_TOP - plat will trigger it's targets each time it hits top
 * PLAT2_TRIGGER_BOTTOM - plat will trigger it's targets each time it hits bottom
 * BOX_LIFT - this indicates that the lift is a box, rather than just a platform
 *
 * Plats are always drawn in the extended position, so they will light correctly.
 *
 * If the plat is the target of another trigger or button, it will start out
 * disabled in the extended position until it is trigger, when it will lower
 * and become a normal plat.
 *
 * "speed"	overrides default 200.
 * "accel" overrides default 500
 * "lip"	no default
 *
 * If the "height" key is set, that will determine the amount the plat moves,
 *  instead of being implicitly determoveinfoned by the model's height.
 *
 */
void
SP_func_plat2(edict_t *ent)
{
	edict_t *trigger;

	if (!ent)
	{
		return;
	}

	VectorClear(ent->s.angles);
	ent->solid = SOLID_BSP;
	ent->movetype = MOVETYPE_PUSH;

	gi.setmodel(ent, ent->model);

	ent->blocked = plat2_blocked;

	if (!ent->speed)
	{
		ent->speed = 20;
	}
	else
	{
		ent->speed *= 0.1;
	}

	if (!ent->accel)
	{
		ent->accel = 5;
	}
	else
	{
		ent->accel *= 0.1;
	}

	if (!ent->decel)
	{
		ent->decel = 5;
	}
	else
	{
		ent->decel *= 0.1;
	}

	if (deathmatch->value)
	{
		ent->speed *= 2;
		ent->accel *= 2;
		ent->decel *= 2;
	}

	/* Added to kill things it's being blocked by */
	if (!ent->dmg)
	{
		ent->dmg = 2;
	}

	/* pos1 is the top position, pos2 is the bottom */
	VectorCopy(ent->s.origin, ent->pos1);
	VectorCopy(ent->s.origin, ent->pos2);

	if (st.height)
	{
		ent->pos2[2] -= (st.height - st.lip);
	}
	else
	{
		ent->pos2[2] -= (ent->maxs[2] - ent->mins[2]) - st.lip;
	}

	ent->moveinfo.state = STATE_TOP;

	if (ent->targetname)
	{
		ent->use = plat2_activate;
	}
	else
	{
		ent->use = Use_Plat2;

		trigger = plat_spawn_inside_trigger(ent); /* the "start moving" trigger */

		trigger->maxs[0] += 10;
		trigger->maxs[1] += 10;
		trigger->mins[0] -= 10;
		trigger->mins[1] -= 10;

		gi.linkentity(trigger);
		trigger->touch = Touch_Plat_Center2; /* Override trigger touch function */

		if (!(ent->spawnflags & PLAT2_TOP))
		{
			VectorCopy(ent->pos2, ent->s.origin);
			ent->moveinfo.state = STATE_BOTTOM;
		}
	}

	gi.linkentity(ent);

	ent->moveinfo.speed = ent->speed;
	ent->moveinfo.accel = ent->accel;
	ent->moveinfo.decel = ent->decel;
	ent->moveinfo.wait = ent->wait;
	VectorCopy(ent->pos1, ent->moveinfo.start_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.start_angles);
	VectorCopy(ent->pos2, ent->moveinfo.end_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.end_angles);

	ent->moveinfo.sound_start = gi.soundindex("plats/pt1_strt.wav");
	ent->moveinfo.sound_middle = gi.soundindex("plats/pt1_mid.wav");
	ent->moveinfo.sound_end = gi.soundindex("plats/pt1_end.wav");
}

/* ==================================================================== */

/* QUAKED func_rotating (0 .5 .8) ? START_ON REVERSE X_AXIS Y_AXIS TOUCH_PAIN STOP ANIMATED ANIMATED_FAST EAST MED HARD DM COOP ACCEL
 *
 * You need to have an origin brush as part of this entity. The center
 * of that brush will bethe point around which it is rotated. It will
 * rotate around the Z axis by default.  You can check either the
 * X_AXIS or Y_AXIS box to change that.
 *
 * func_rotating will use it's targets when it stops and starts.
 *
 * "speed" determines how fast it moves; default value is 100.
 * "dmg"   damage to inflict when blocked (2 default)
 * "accel" if specified, is how much the rotation speed will increase per .1sec.
 *
 * REVERSE will cause the it to rotate in the opposite direction.
 * STOP mean it will stop moving instead of pushing entities
 * ACCEL means it will accelerate to it's final speed and decelerate when shutting down.
 */
void
rotating_accel(edict_t *self)
{
	float current_speed;

	if (!self)
	{
		return;
	}

	current_speed = VectorLength(self->avelocity);

	if (current_speed >= (self->speed - self->accel)) /* done */
	{
		VectorScale(self->movedir, self->speed, self->avelocity);
		G_UseTargets(self, self);
	}
	else
	{
		current_speed += self->accel;
		VectorScale(self->movedir, current_speed, self->avelocity);
		self->think = rotating_accel;
		self->nextthink = level.time + FRAMETIME;
	}
}

void
rotating_decel(edict_t *self)
{
	float current_speed;

	if (!self)
	{
		return;
	}

	current_speed = VectorLength(self->avelocity);

	if (current_speed <= self->decel) /* done */
	{
		VectorClear(self->avelocity);
		G_UseTargets(self, self);
		self->touch = NULL;
	}
	else
	{
		current_speed -= self->decel;
		VectorScale(self->movedir, current_speed, self->avelocity);
		self->think = rotating_decel;
		self->nextthink = level.time + FRAMETIME;
	}
}

void
rotating_blocked(edict_t *self, edict_t *other)
{
	if (!self || !other)
	{
		return;
	}

	T_Damage(other, self, self, vec3_origin, other->s.origin,
			vec3_origin, self->dmg, 1, 0, MOD_CRUSH);
}

void
rotating_touch(edict_t *self, edict_t *other, cplane_t *plane /* unused */,
	   	csurface_t *surf /* unused */)
{
	if (!self || !other)
	{
		return;
	}

	if (self->avelocity[0] || self->avelocity[1] || self->avelocity[2])
	{
		T_Damage(other, self, self, vec3_origin, other->s.origin,
				vec3_origin, self->dmg, 1, 0, MOD_CRUSH);
	}
}

void
rotating_use(edict_t *self, edict_t *other /* unused */,
	   	edict_t *activator /* unused */)
{
	if (!self)
	{
		return;
	}

	if (!VectorCompare(self->avelocity, vec3_origin))
	{
		self->s.sound = 0;

		if (self->spawnflags & 8192) /* Decelerate */
		{
			rotating_decel(self);
		}
		else
		{
			VectorClear(self->avelocity);
			G_UseTargets(self, self);
			self->touch = NULL;
		}
	}
	else
	{
		self->s.sound = self->moveinfo.sound_middle;

		if (self->spawnflags & 8192) /* accelerate */
		{
			rotating_accel(self);
		}
		else
		{
			VectorScale(self->movedir, self->speed, self->avelocity);
			G_UseTargets(self, self);
		}

		if (self->spawnflags & 16)
		{
			self->touch = rotating_touch;
		}
	}
}

void
SP_func_rotating(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	ent->solid = SOLID_BSP;

	if (ent->spawnflags & 32)
	{
		ent->movetype = MOVETYPE_STOP;
	}
	else
	{
		ent->movetype = MOVETYPE_PUSH;
	}

	/* set the axis of rotation */
	VectorClear(ent->movedir);

	if (ent->spawnflags & 4)
	{
		ent->movedir[2] = 1.0;
	}
	else if (ent->spawnflags & 8)
	{
		ent->movedir[0] = 1.0;
	}
	else /* Z_AXIS */
	{
		ent->movedir[1] = 1.0;
	}

	/* check for reverse rotation */
	if (ent->spawnflags & 2)
	{
		VectorNegate(ent->movedir, ent->movedir);
	}

	if (!ent->speed)
	{
		ent->speed = 100;
	}

	if (!ent->dmg)
	{
		ent->dmg = 2;
	}

	ent->use = rotating_use;

	if (ent->dmg)
	{
		ent->blocked = rotating_blocked;
	}

	if (ent->spawnflags & 1)
	{
		ent->use(ent, NULL, NULL);
	}

	if (ent->spawnflags & 64)
	{
		ent->s.effects |= EF_ANIM_ALL;
	}

	if (ent->spawnflags & 128)
	{
		ent->s.effects |= EF_ANIM_ALLFAST;
	}

	if (ent->spawnflags & 8192) /* Accelerate / Decelerate */
	{
		if (!ent->accel)
		{
			ent->accel = 1;
		}
		else if (ent->accel > ent->speed)
		{
			ent->accel = ent->speed;
		}

		if (!ent->decel)
		{
			ent->decel = 1;
		}
		else if (ent->decel > ent->speed)
		{
			ent->decel = ent->speed;
		}
	}

	gi.setmodel(ent, ent->model);
	gi.linkentity(ent);
}

/* ==================================================================== */

/* BUTTONS */

/*
 * QUAKED func_button (0 .5 .8) ?
 *
 * When a button is touched, it moves some distance
 * in the direction of it's angle, triggers all of it's
 * targets, waits some time, then returns to it's original
 * position where it can be triggered again.
 *
 * "angle"		determines the opening direction
 * "target"	    all entities with a matching targetname will be used
 * "speed"		override the default 40 speed
 * "wait"		override the default 1 second wait (-1 = never return)
 * "lip"		override the default 4 pixel lip remaining at end of move
 * "health"	    if set, the button must be killed instead of touched
 * "sounds"
 *    1) silent
 *    2) steam metal
 *    3) wooden clunk
 *    4) metallic click
 *    5) in-out
 */
void
button_done(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->moveinfo.state = STATE_BOTTOM;
	self->s.effects &= ~EF_ANIM23;
	self->s.effects |= EF_ANIM01;
}

void
button_return(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->moveinfo.state = STATE_DOWN;

	Move_Calc(self, self->moveinfo.start_origin, button_done);

	self->s.frame = 0;

	if (self->health)
	{
		self->takedamage = DAMAGE_YES;
	}
}

void
button_wait(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->moveinfo.state = STATE_TOP;
	self->s.effects &= ~EF_ANIM01;
	self->s.effects |= EF_ANIM23;

	G_UseTargets(self, self->activator);
	self->s.frame = 1;

	if (self->moveinfo.wait >= 0)
	{
		self->nextthink = level.time + self->moveinfo.wait;
		self->think = button_return;
	}
}

void
button_fire(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if ((self->moveinfo.state == STATE_UP) ||
		(self->moveinfo.state == STATE_TOP))
	{
		return;
	}

	self->moveinfo.state = STATE_UP;

	if (self->moveinfo.sound_start && !(self->flags & FL_TEAMSLAVE))
	{
		gi.sound(self, CHAN_NO_PHS_ADD + CHAN_VOICE,
				self->moveinfo.sound_start,
				1, ATTN_STATIC, 0);
	}

	Move_Calc(self, self->moveinfo.end_origin, button_wait);
}

void
button_use(edict_t *self, edict_t *other /* unused */, edict_t *activator)
{
	if (!self || !activator)
	{
		return;
	}

	self->activator = activator;
	button_fire(self);
}

void
button_touch(edict_t *self, edict_t *other, cplane_t *plane /* unused */,
	   	csurface_t *surf /* unused */)
{
	if (!self || !other)
	{
		return;
	}

	if (!other->client)
	{
		return;
	}

	if (other->health <= 0)
	{
		return;
	}

	self->activator = other;
	button_fire(self);
}

void
button_killed(edict_t *self, edict_t *inflictor /* unused */,
		edict_t *attacker, int damage /* unused */,
		vec3_t point /* unused */)
{
	if (!self || !attacker)
	{
		return;
	}

	self->activator = attacker;
	self->health = self->max_health;
	self->takedamage = DAMAGE_NO;
	button_fire(self);
}

void
SP_func_button(edict_t *ent)
{
	vec3_t abs_movedir;
	float dist;

	if (!ent)
	{
		return;
	}

	G_SetMovedir(ent->s.angles, ent->movedir);
	ent->movetype = MOVETYPE_STOP;
	ent->solid = SOLID_BSP;
	gi.setmodel(ent, ent->model);

	if (ent->sounds != 1)
	{
		ent->moveinfo.sound_start = gi.soundindex("switches/butn2.wav");
	}

	if (!ent->speed)
	{
		ent->speed = 40;
	}

	if (!ent->accel)
	{
		ent->accel = ent->speed;
	}

	if (!ent->decel)
	{
		ent->decel = ent->speed;
	}

	if (!ent->wait)
	{
		ent->wait = 3;
	}

	if (!st.lip)
	{
		st.lip = 4;
	}

	VectorCopy(ent->s.origin, ent->pos1);
	abs_movedir[0] = fabs(ent->movedir[0]);
	abs_movedir[1] = fabs(ent->movedir[1]);
	abs_movedir[2] = fabs(ent->movedir[2]);
	dist = abs_movedir[0] * ent->size[0] + abs_movedir[1] * ent->size[1] +
		   abs_movedir[2] * ent->size[2] - st.lip;
	VectorMA(ent->pos1, dist, ent->movedir, ent->pos2);

	ent->use = button_use;
	ent->s.effects |= EF_ANIM01;

	if (ent->health)
	{
		ent->max_health = ent->health;
		ent->die = button_killed;
		ent->takedamage = DAMAGE_YES;
	}
	else if (!ent->targetname)
	{
		ent->touch = button_touch;
	}

	ent->moveinfo.state = STATE_BOTTOM;

	ent->moveinfo.speed = ent->speed;
	ent->moveinfo.accel = ent->accel;
	ent->moveinfo.decel = ent->decel;
	ent->moveinfo.wait = ent->wait;
	VectorCopy(ent->pos1, ent->moveinfo.start_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.start_angles);
	VectorCopy(ent->pos2, ent->moveinfo.end_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.end_angles);

	gi.linkentity(ent);
}


/* ==================================================================== */

/*
 * DOORS
 *
 * spawn a trigger surrounding the entire team
 * unless it is already targeted by another
 */

/*
 * QUAKED func_door (0 .5 .8) ? START_OPEN x CRUSHER NOMONSTER ANIMATED TOGGLE ANIMATED_FAST
 *
 * TOGGLE		wait in both the start and end states for a trigger event.
 * START_OPEN	the door to moves to its destination when spawned, and operate in reverse.
 *              It is used to temporarily or permanently close off an area when triggered
 *              (not useful for touch or takedamage doors).
 * NOMONSTER	monsters will not trigger this door
 *
 * "message"	is printed when the door is touched if it is a trigger door and it hasn't been fired yet
 * "angle"		determines the opening direction
 * "targetname" if set, no touch field will be spawned and a remote button or trigger field activates the door.
 * "health"	    if set, door must be shot open
 * "speed"		movement speed (100 default)
 * "wait"		wait before returning (3 default, -1 = never return)
 * "lip"		lip remaining at end of move (8 default)
 * "dmg"		damage to inflict when blocked (2 default)
 * "sounds"
 *    1)	silent
 *    2)	light
 *    3)	medium
 *    4)	heavy
 */

void
door_use_areaportals(edict_t *self, qboolean open)
{
	edict_t *t = NULL;

	if (!self)
	{
		return;
	}

	if (!self->target)
	{
		return;
	}

	while ((t = G_Find(t, FOFS(targetname), self->target)))
	{
		if (Q_stricmp(t->classname, "func_areaportal") == 0)
		{
			gi.SetAreaPortalState(t->style, open);
		}
	}
}

void
door_hit_top(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (!(self->flags & FL_TEAMSLAVE))
	{
		if (self->moveinfo.sound_end)
		{
			gi.sound(self, CHAN_NO_PHS_ADD + CHAN_VOICE,
					self->moveinfo.sound_end, 1,
					ATTN_STATIC, 0);
		}

		self->s.sound = 0;
	}

	self->moveinfo.state = STATE_TOP;

	if (self->spawnflags & DOOR_TOGGLE)
	{
		return;
	}

	if (self->moveinfo.wait >= 0)
	{
		self->think = door_go_down;
		self->nextthink = level.time + self->moveinfo.wait;
	}
}

void
door_hit_bottom(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (!(self->flags & FL_TEAMSLAVE))
	{
		if (self->moveinfo.sound_end)
		{
			gi.sound(self, CHAN_NO_PHS_ADD + CHAN_VOICE,
					self->moveinfo.sound_end, 1,
					ATTN_STATIC, 0);
		}

		self->s.sound = 0;
	}

	self->moveinfo.state = STATE_BOTTOM;
	door_use_areaportals(self, false);
}

void
door_go_down(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (!(self->flags & FL_TEAMSLAVE))
	{
		if (self->moveinfo.sound_start)
		{
			gi.sound(self, CHAN_NO_PHS_ADD + CHAN_VOICE,
					self->moveinfo.sound_start,
					1, ATTN_STATIC, 0);
		}

		self->s.sound = self->moveinfo.sound_middle;
	}

	if (self->max_health)
	{
		self->takedamage = DAMAGE_YES;
		self->health = self->max_health;
	}

	self->moveinfo.state = STATE_DOWN;

	if (strcmp(self->classname, "func_door") == 0)
	{
		Move_Calc(self, self->moveinfo.start_origin, door_hit_bottom);
	}
	else if (strcmp(self->classname, "func_door_rotating") == 0)
	{
		AngleMove_Calc(self, door_hit_bottom);
	}
}

void
door_go_up(edict_t *self, edict_t *activator)
{
	if (!self || !activator)
	{
		return;
	}

	if (self->moveinfo.state == STATE_UP)
	{
		return; /* already going up */
	}

	if (self->moveinfo.state == STATE_TOP)
	{
		/* reset top wait time */
		if (self->moveinfo.wait >= 0)
		{
			self->nextthink = level.time + self->moveinfo.wait;
		}

		return;
	}

	if (!(self->flags & FL_TEAMSLAVE))
	{
		if (self->moveinfo.sound_start)
		{
			gi.sound(self, CHAN_NO_PHS_ADD + CHAN_VOICE,
					self->moveinfo.sound_start, 1,
					ATTN_STATIC, 0);
		}

		self->s.sound = self->moveinfo.sound_middle;
	}

	self->moveinfo.state = STATE_UP;

	if (strcmp(self->classname, "func_door") == 0)
	{
		Move_Calc(self, self->moveinfo.end_origin, door_hit_top);
	}
	else if (strcmp(self->classname, "func_door_rotating") == 0)
	{
		AngleMove_Calc(self, door_hit_top);
	}

	G_UseTargets(self, activator);
	door_use_areaportals(self, true);
}

void
smart_water_go_up(edict_t *self)
{
	float distance;
	edict_t *lowestPlayer;
	edict_t *ent;
	float lowestPlayerPt;
	int i;

	if (!self)
	{
		return;
	}

	if (self->moveinfo.state == STATE_TOP)
	{
		/* reset top wait time */
		if (self->moveinfo.wait >= 0)
		{
			self->nextthink = level.time + self->moveinfo.wait;
		}

		return;
	}

	if (self->health)
	{
		if (self->absmax[2] >= self->health)
		{
			VectorClear(self->velocity);
			self->nextthink = 0;
			self->moveinfo.state = STATE_TOP;
			return;
		}
	}

	if (!(self->flags & FL_TEAMSLAVE))
	{
		if (self->moveinfo.sound_start)
		{
			gi.sound(self, CHAN_NO_PHS_ADD + CHAN_VOICE,
					self->moveinfo.sound_start, 1,
					ATTN_STATIC, 0);
		}

		self->s.sound = self->moveinfo.sound_middle;
	}

	/* find the lowest player point. */
	lowestPlayerPt = 999999;
	lowestPlayer = NULL;

	for (i = 0; i < game.maxclients; i++)
	{
		ent = &g_edicts[1 + i];

		/* don't count dead or unused player slots */
		if ((ent->inuse) && (ent->health > 0))
		{
			if (ent->absmin[2] < lowestPlayerPt)
			{
				lowestPlayerPt = ent->absmin[2];
				lowestPlayer = ent;
			}
		}
	}

	if (!lowestPlayer)
	{
		return;
	}

	distance = lowestPlayerPt - self->absmax[2];

	/* for the calculations, make sure we
	   intend to go up at least a little. */
	if (distance < self->accel)
	{
		distance = 100;
		self->moveinfo.speed = 5;
	}
	else
	{
		self->moveinfo.speed = distance / self->accel;
	}

	if (self->moveinfo.speed < 5)
	{
		self->moveinfo.speed = 5;
	}
	else if (self->moveinfo.speed > self->speed)
	{
		self->moveinfo.speed = self->speed;
	}

	/* should this allow any movement other than straight up? */
	VectorSet(self->moveinfo.dir, 0, 0, 1);
	VectorScale(self->moveinfo.dir, self->moveinfo.speed, self->velocity);
	self->moveinfo.remaining_distance = distance;

	if (self->moveinfo.state != STATE_UP)
	{
		G_UseTargets(self, lowestPlayer);
		door_use_areaportals(self, true);
		self->moveinfo.state = STATE_UP;
	}

	self->think = smart_water_go_up;
	self->nextthink = level.time + FRAMETIME;
}

void
door_use(edict_t *self, edict_t *other /* unused */, edict_t *activator)
{
	edict_t *ent;
	vec3_t center;

	if (!self || !activator)
	{
		return;
	}

	if (self->flags & FL_TEAMSLAVE)
	{
		return;
	}

	if (self->spawnflags & DOOR_TOGGLE)
	{
		if ((self->moveinfo.state == STATE_UP) ||
			(self->moveinfo.state == STATE_TOP))
		{
			/* trigger all paired doors */
			for (ent = self; ent; ent = ent->teamchain)
			{
				ent->message = NULL;
				ent->touch = NULL;
				door_go_down(ent);
			}

			return;
		}
	}

	/* smart water is different */
	VectorAdd(self->mins, self->maxs, center);
	VectorScale(center, 0.5, center);

	if ((gi.pointcontents(center) & MASK_WATER) && self->spawnflags & 2)
	{
		self->message = NULL;
		self->touch = NULL;
		self->enemy = activator;
		smart_water_go_up(self);
		return;
	}

	/* trigger all paired doors */
	for (ent = self; ent; ent = ent->teamchain)
	{
		ent->message = NULL;
		ent->touch = NULL;
		door_go_up(ent, activator);
	}
}

void
Touch_DoorTrigger(edict_t *self, edict_t *other, cplane_t *plane /* unused */,
	   	csurface_t *surf /* unused */)
{
	if (!self || !other)
	{
		return;
	}

	if (other->health <= 0)
	{
		return;
	}

	if (!(other->svflags & SVF_MONSTER) && (!other->client))
	{
		return;
	}

	if ((self->owner->spawnflags & DOOR_NOMONSTER) &&
		(other->svflags & SVF_MONSTER))
	{
		return;
	}

	if (level.time < self->touch_debounce_time)
	{
		return;
	}

	self->touch_debounce_time = level.time + 1.0;

	door_use(self->owner, other, other);
}

void
Think_CalcMoveSpeed(edict_t *self)
{
	edict_t *ent;
	float min;
	float time;
	float newspeed;
	float ratio;
	float dist;

	if (!self)
	{
		return;
	}

	if (self->flags & FL_TEAMSLAVE)
	{
		return; /* only the team master does this */
	}

	/* find the smallest distance any member of the team will be moving */
	min = fabs(self->moveinfo.distance);

	for (ent = self->teamchain; ent; ent = ent->teamchain)
	{
		dist = fabs(ent->moveinfo.distance);

		if (dist < min)
		{
			min = dist;
		}
	}

	time = min / self->moveinfo.speed;

	/* adjust speeds so they will all complete at the same time */
	for (ent = self; ent; ent = ent->teamchain)
	{
		newspeed = fabs(ent->moveinfo.distance) / time;
		ratio = newspeed / ent->moveinfo.speed;

		if (ent->moveinfo.accel == ent->moveinfo.speed)
		{
			ent->moveinfo.accel = newspeed;
		}
		else
		{
			ent->moveinfo.accel *= ratio;
		}

		if (ent->moveinfo.decel == ent->moveinfo.speed)
		{
			ent->moveinfo.decel = newspeed;
		}
		else
		{
			ent->moveinfo.decel *= ratio;
		}

		ent->moveinfo.speed = newspeed;
	}
}

void
Think_SpawnDoorTrigger(edict_t *ent)
{
	edict_t *other;
	vec3_t mins, maxs;

	if (!ent)
	{
		return;
	}

	if (ent->flags & FL_TEAMSLAVE)
	{
		return; /* only the team leader spawns a trigger */
	}

	VectorCopy(ent->absmin, mins);
	VectorCopy(ent->absmax, maxs);

	for (other = ent->teamchain; other; other = other->teamchain)
	{
		AddPointToBounds(other->absmin, mins, maxs);
		AddPointToBounds(other->absmax, mins, maxs);
	}

	/* expand */
	mins[0] -= 60;
	mins[1] -= 60;
	maxs[0] += 60;
	maxs[1] += 60;

	other = G_Spawn();
	VectorCopy(mins, other->mins);
	VectorCopy(maxs, other->maxs);
	other->owner = ent;
	other->solid = SOLID_TRIGGER;
	other->movetype = MOVETYPE_NONE;
	other->touch = Touch_DoorTrigger;
	gi.linkentity(other);

	if (ent->spawnflags & DOOR_START_OPEN)
	{
		door_use_areaportals(ent, true);
	}

	Think_CalcMoveSpeed(ent);
}

void
door_blocked(edict_t *self, edict_t *other)
{
	edict_t *ent;

	if (!self || !other)
	{
		return;
	}

	if (!(other->svflags & SVF_MONSTER) && (!other->client))
	{
		/* give it a chance to go away on it's own terms (like gibs) */
		T_Damage(other, self, self, vec3_origin, other->s.origin,
				vec3_origin, 100000, 1, 0, MOD_CRUSH);

		/* if it's still there, nuke it */
		if (other)
		{
			/* Hack for entitiy without their origin near the model */
			VectorMA(other->absmin, 0.5, other->size, other->s.origin);
			BecomeExplosion1(other);
		}

		return;
	}

	T_Damage(other, self, self, vec3_origin, other->s.origin,
			vec3_origin, self->dmg, 1, 0, MOD_CRUSH);

	if (self->spawnflags & DOOR_CRUSHER)
	{
		return;
	}

	/* if a door has a negative wait, it would never come
	   back if blocked, so let it just squash the object
	   to death real fast */
	if (self->moveinfo.wait >= 0)
	{
		if (self->moveinfo.state == STATE_DOWN)
		{
			for (ent = self->teammaster; ent; ent = ent->teamchain)
			{
				door_go_up(ent, ent->activator);
			}
		}
		else
		{
			for (ent = self->teammaster; ent; ent = ent->teamchain)
			{
				door_go_down(ent);
			}
		}
	}
}

void
door_killed(edict_t *self, edict_t *inflictor /* unused */,
		edict_t *attacker, int damage /* unused */,
		vec3_t point /* unused */)
{
	edict_t *ent;

	if (!self || !attacker)
	{
		return;
	}

	for (ent = self->teammaster; ent; ent = ent->teamchain)
	{
		ent->health = ent->max_health;
		ent->takedamage = DAMAGE_NO;
	}

	door_use(self->teammaster, attacker, attacker);
}

void
door_touch(edict_t *self, edict_t *other, cplane_t *plane /* unused */,
		csurface_t *surf /* unused */)
{
	if (!self || !other)
	{
		return;
	}

	if (!other->client)
	{
		return;
	}

	if (level.time < self->touch_debounce_time)
	{
		return;
	}

	self->touch_debounce_time = level.time + 5.0;

	gi.centerprintf(other, "%s", self->message);
	gi.sound(other, CHAN_AUTO, gi.soundindex("misc/talk1.wav"), 1, ATTN_NORM, 0);
}

void
SP_func_door(edict_t *ent)
{
	vec3_t abs_movedir;

	if (!ent)
	{
		return;
	}

	if (ent->sounds != 1)
	{
		ent->moveinfo.sound_start = gi.soundindex("doors/dr1_strt.wav");
		ent->moveinfo.sound_middle = gi.soundindex("doors/dr1_mid.wav");
		ent->moveinfo.sound_end = gi.soundindex("doors/dr1_end.wav");
	}

	G_SetMovedir(ent->s.angles, ent->movedir);
	ent->movetype = MOVETYPE_PUSH;
	ent->solid = SOLID_BSP;
	gi.setmodel(ent, ent->model);

	ent->blocked = door_blocked;
	ent->use = door_use;

	if (!ent->speed)
	{
		ent->speed = 100;
	}

	if (deathmatch->value)
	{
		ent->speed *= 2;
	}

	if (!ent->accel)
	{
		ent->accel = ent->speed;
	}

	if (!ent->decel)
	{
		ent->decel = ent->speed;
	}

	if (!ent->wait)
	{
		ent->wait = 3;
	}

	if (!st.lip)
	{
		st.lip = 8;
	}

	if (!ent->dmg)
	{
		ent->dmg = 2;
	}

	/* calculate second position */
	VectorCopy(ent->s.origin, ent->pos1);
	abs_movedir[0] = fabs(ent->movedir[0]);
	abs_movedir[1] = fabs(ent->movedir[1]);
	abs_movedir[2] = fabs(ent->movedir[2]);
	ent->moveinfo.distance = abs_movedir[0] * ent->size[0] + abs_movedir[1] *
							 ent->size[1] + abs_movedir[2] * ent->size[2] -
							 st.lip;
	VectorMA(ent->pos1, ent->moveinfo.distance, ent->movedir, ent->pos2);

	/* if it starts open, switch the positions */
	if (ent->spawnflags & DOOR_START_OPEN)
	{
		VectorCopy(ent->pos2, ent->s.origin);
		VectorCopy(ent->pos1, ent->pos2);
		VectorCopy(ent->s.origin, ent->pos1);
	}

	ent->moveinfo.state = STATE_BOTTOM;

	if (ent->health)
	{
		ent->takedamage = DAMAGE_YES;
		ent->die = door_killed;
		ent->max_health = ent->health;
	}
	else if (ent->targetname && ent->message)
	{
		gi.soundindex("misc/talk.wav");
		ent->touch = door_touch;
	}

	ent->moveinfo.speed = ent->speed;
	ent->moveinfo.accel = ent->accel;
	ent->moveinfo.decel = ent->decel;
	ent->moveinfo.wait = ent->wait;
	VectorCopy(ent->pos1, ent->moveinfo.start_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.start_angles);
	VectorCopy(ent->pos2, ent->moveinfo.end_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.end_angles);

	if (ent->spawnflags & 16)
	{
		ent->s.effects |= EF_ANIM_ALL;
	}

	if (ent->spawnflags & 64)
	{
		ent->s.effects |= EF_ANIM_ALLFAST;
	}

	/* to simplify logic elsewhere, make
	   non-teamed doors into a team of one */
	if (!ent->team)
	{
		ent->teammaster = ent;
	}

	gi.linkentity(ent);

	ent->nextthink = level.time + FRAMETIME;

	if (ent->health || ent->targetname)
	{
		ent->think = Think_CalcMoveSpeed;
	}
	else
	{
		ent->think = Think_SpawnDoorTrigger;
	}
}

void
Door_Activate(edict_t *self, edict_t *other /* unused */,
	   	edict_t *activator /* unused */)
{
	if (!self)
	{
		return;
	}

	self->use = NULL;

	if (self->health)
	{
		self->takedamage = DAMAGE_YES;
		self->die = door_killed;
		self->max_health = self->health;
	}

	if (self->health)
	{
		self->think = Think_CalcMoveSpeed;
	}
	else
	{
		self->think = Think_SpawnDoorTrigger;
	}

	self->nextthink = level.time + FRAMETIME;
}

/*
 * QUAKED func_door_rotating (0 .5 .8) ? START_OPEN REVERSE CRUSHER NOMONSTER ANIMATED TOGGLE X_AXIS Y_AXIS
 *
 * TOGGLE       causes the door to wait in both the start and end states for a trigger event.
 * START_OPEN	the door to moves to its destination when spawned, and operate in reverse.
 *              It is used to temporarily or permanently close off an area when triggered
 *              (not useful for touch or takedamage doors).
 * NOMONSTER	monsters will not trigger this door
 *
 * You need to have an origin brush as part of this entity.  The center of that brush will be
 * the point around which it is rotated. It will rotate around the Z axis by default.  You can
 * check either the X_AXIS or Y_AXIS box to change that.
 *
 * "distance" is how many degrees the door will be rotated.
 * "speed" determines how fast the door moves; default value is 100.
 *
 * REVERSE will cause the door to rotate in the opposite direction.
 *
 * "message"	is printed when the door is touched if it is a trigger door and it hasn't been fired yet
 * "angle"		determines the opening direction
 * "targetname" if set, no touch field will be spawned and a remote button or trigger field activates the door.
 * "health"	    if set, door must be shot open
 * "speed"		movement speed (100 default)
 * "wait"		wait before returning (3 default, -1 = never return)
 * "dmg"		damage to inflict when blocked (2 default)
 * "sounds"
 *    1)	silent
 *    2)	light
 *    3)	medium
 *    4)	heavy
 */
void
SP_func_door_rotating(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	VectorClear(ent->s.angles);

	/* set the axis of rotation */
	VectorClear(ent->movedir);

	if (ent->spawnflags & DOOR_X_AXIS)
	{
		ent->movedir[2] = 1.0;
	}
	else if (ent->spawnflags & DOOR_Y_AXIS)
	{
		ent->movedir[0] = 1.0;
	}
	else /* Z_AXIS */
	{
		ent->movedir[1] = 1.0;
	}

	/* check for reverse rotation */
	if (ent->spawnflags & DOOR_REVERSE)
	{
		VectorNegate(ent->movedir, ent->movedir);
	}

	if (!st.distance)
	{
		gi.dprintf("%s at %s with no distance set\n", ent->classname,
				vtos(ent->s.origin));
		st.distance = 90;
	}

	VectorCopy(ent->s.angles, ent->pos1);
	VectorMA(ent->s.angles, st.distance, ent->movedir, ent->pos2);
	ent->moveinfo.distance = st.distance;

	ent->movetype = MOVETYPE_PUSH;
	ent->solid = SOLID_BSP;
	gi.setmodel(ent, ent->model);

	ent->blocked = door_blocked;
	ent->use = door_use;

	if (!ent->speed)
	{
		ent->speed = 100;
	}

	if (!ent->accel)
	{
		ent->accel = ent->speed;
	}

	if (!ent->decel)
	{
		ent->decel = ent->speed;
	}

	if (!ent->wait)
	{
		ent->wait = 3;
	}

	if (!ent->dmg)
	{
		ent->dmg = 2;
	}

	if (ent->sounds != 1)
	{
		ent->moveinfo.sound_start = gi.soundindex("doors/dr1_strt.wav");
		ent->moveinfo.sound_middle = gi.soundindex("doors/dr1_mid.wav");
		ent->moveinfo.sound_end = gi.soundindex("doors/dr1_end.wav");
	}

	/* if it starts open, switch the positions */
	if (ent->spawnflags & DOOR_START_OPEN)
	{
		VectorCopy(ent->pos2, ent->s.angles);
		VectorCopy(ent->pos1, ent->pos2);
		VectorCopy(ent->s.angles, ent->pos1);
		VectorNegate(ent->movedir, ent->movedir);
	}

	if (ent->health)
	{
		ent->takedamage = DAMAGE_YES;
		ent->die = door_killed;
		ent->max_health = ent->health;
	}

	if (ent->targetname && ent->message)
	{
		gi.soundindex("misc/talk.wav");
		ent->touch = door_touch;
	}

	ent->moveinfo.state = STATE_BOTTOM;
	ent->moveinfo.speed = ent->speed;
	ent->moveinfo.accel = ent->accel;
	ent->moveinfo.decel = ent->decel;
	ent->moveinfo.wait = ent->wait;
	VectorCopy(ent->s.origin, ent->moveinfo.start_origin);
	VectorCopy(ent->pos1, ent->moveinfo.start_angles);
	VectorCopy(ent->s.origin, ent->moveinfo.end_origin);
	VectorCopy(ent->pos2, ent->moveinfo.end_angles);

	if (ent->spawnflags & 16)
	{
		ent->s.effects |= EF_ANIM_ALL;
	}

	/* to simplify logic elsewhere, make non-teamed doors into a team of one */
	if (!ent->team)
	{
		ent->teammaster = ent;
	}

	gi.linkentity(ent);

	ent->nextthink = level.time + FRAMETIME;

	if (ent->health || ent->targetname)
	{
		ent->think = Think_CalcMoveSpeed;
	}
	else
	{
		ent->think = Think_SpawnDoorTrigger;
	}

	if (ent->spawnflags & DOOR_INACTIVE)
	{
		ent->takedamage = DAMAGE_NO;
		ent->die = NULL;
		ent->think = NULL;
		ent->nextthink = 0;
		ent->use = Door_Activate;
	}
}

void
smart_water_blocked(edict_t *self, edict_t *other)
{
	if (!self || !other)
	{
		return;
	}

	if (!(other->svflags & SVF_MONSTER) && (!other->client))
	{
		/* give it a chance to go away on it's own terms (like gibs) */
		T_Damage(other, self, self, vec3_origin, other->s.origin,
				vec3_origin, 100000, 1, 0, MOD_LAVA);

		/* if it's still there, nuke it */
		if (other && other->inuse)
		{
			BecomeExplosion1(other);
		}

		return;
	}

	T_Damage(other, self, self, vec3_origin, other->s.origin,
			vec3_origin, 100, 1, 0, MOD_LAVA);
}

/* ==================================================================== */

/*
 * QUAKED func_water (0 .5 .8) ? START_OPEN
 *
 * func_water is a moveable water brush.  It must be targeted to operate.
 * Use a non-water texture at your own risk.
 *
 * START_OPEN causes the water to move to its destination when spawned
 *             and operate in reverse.
 *
 * "angle"		determines the opening direction (up or down only)
 * "speed"		movement speed (25 default)
 * "wait"		wait before returning (-1 default, -1 = TOGGLE)
 * "lip"		lip remaining at end of move (0 default)
 * "sounds"	    (yes, these need to be changed)
 *    0)	no sound
 *    1)	water
 *    2)	lava
 */
void
SP_func_water(edict_t *self)
{
	vec3_t abs_movedir;

	if (!self)
	{
		return;
	}

	G_SetMovedir(self->s.angles, self->movedir);
	self->movetype = MOVETYPE_PUSH;
	self->solid = SOLID_BSP;
	gi.setmodel(self, self->model);

	switch (self->sounds)
	{
		default:
			break;

		case 1: /* water */
			self->moveinfo.sound_start = gi.soundindex("world/mov_watr.wav");
			self->moveinfo.sound_end = gi.soundindex("world/stp_watr.wav");
			break;

		case 2: /* lava */
			self->moveinfo.sound_start = gi.soundindex("world/mov_watr.wav");
			self->moveinfo.sound_end = gi.soundindex("world/stp_watr.wav");
			break;
	}

	/* calculate second position */
	VectorCopy(self->s.origin, self->pos1);
	abs_movedir[0] = fabs(self->movedir[0]);
	abs_movedir[1] = fabs(self->movedir[1]);
	abs_movedir[2] = fabs(self->movedir[2]);
	self->moveinfo.distance = abs_movedir[0] * self->size[0] + abs_movedir[1] *
							  self->size[1] + abs_movedir[2] * self->size[2] -
							  st.lip;
	VectorMA(self->pos1, self->moveinfo.distance, self->movedir, self->pos2);

	/* if it starts open, switch the positions */
	if (self->spawnflags & DOOR_START_OPEN)
	{
		VectorCopy(self->pos2, self->s.origin);
		VectorCopy(self->pos1, self->pos2);
		VectorCopy(self->s.origin, self->pos1);
	}

	VectorCopy(self->pos1, self->moveinfo.start_origin);
	VectorCopy(self->s.angles, self->moveinfo.start_angles);
	VectorCopy(self->pos2, self->moveinfo.end_origin);
	VectorCopy(self->s.angles, self->moveinfo.end_angles);

	self->moveinfo.state = STATE_BOTTOM;

	if (!self->speed)
	{
		self->speed = 25;
	}

	self->moveinfo.accel = self->moveinfo.decel = self->moveinfo.speed = self->speed;

	if (self->spawnflags & 2)   /* smart water */
	{
		if (!self->accel)
		{
			self->accel = 20;
		}

		self->blocked = smart_water_blocked;
	}

	if (!self->wait)
	{
		self->wait = -1;
	}

	self->moveinfo.wait = self->wait;

	self->use = door_use;

	if (self->wait == -1)
	{
		self->spawnflags |= DOOR_TOGGLE;
	}

	self->classname = "func_door";

	gi.linkentity(self);
}

/*
 * QUAKED func_train (0 .5 .8) ? START_ON TOGGLE BLOCK_STOPS
 *
 * Trains are moving platforms that players can ride.
 * The targets origin specifies the min point of the train
 * at each corner. The train spawns at the first target it
 * is pointing at. If the train is the target of a button
 * or trigger, it will not begin moving until activated.
 *
 * speed	default 100
 * dmg		default	2
 * noise	looping sound to play when the train is in motion
 *
 */
void
train_blocked(edict_t *self, edict_t *other)
{
	if (!self || !other)
	{
		return;
	}

	if (!(other->svflags & SVF_MONSTER) && (!other->client))
	{
		/* give it a chance to go away on it's own terms (like gibs) */
		T_Damage(other, self, self, vec3_origin, other->s.origin,
				vec3_origin, 100000, 1, 0, MOD_CRUSH);

		/* if it's still there, nuke it */
		if (other)
		{
			/* Hack for entity without an origin near the model */
			VectorMA(other->absmin, 0.5, other->size, other->s.origin);
			BecomeExplosion1(other);
		}

		return;
	}

	if (level.time < self->touch_debounce_time)
	{
		return;
	}

	if (!self->dmg)
	{
		return;
	}

	self->touch_debounce_time = level.time + 0.5;
	T_Damage(other, self, self, vec3_origin, other->s.origin,
			vec3_origin, self->dmg, 1, 0, MOD_CRUSH);
}

void
train_wait(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->target_ent->pathtarget)
	{
		char *savetarget;
		edict_t *ent;

		ent = self->target_ent;
		savetarget = ent->target;
		ent->target = ent->pathtarget;
		G_UseTargets(ent, self->activator);
		ent->target = savetarget;

		/* make sure we didn't get killed by a killtarget */
		if (!self->inuse)
		{
			return;
		}
	}

	if (self->moveinfo.wait)
	{
		if (self->moveinfo.wait > 0)
		{
			self->nextthink = level.time + self->moveinfo.wait;
			self->think = train_next;
		}
		else if (self->spawnflags & TRAIN_TOGGLE)
		{
			self->target_ent = NULL;
			self->spawnflags &= ~TRAIN_START_ON;
			VectorClear(self->velocity);
			self->nextthink = 0;
		}

		if (!(self->flags & FL_TEAMSLAVE))
		{
			if (self->moveinfo.sound_end)
			{
				gi.sound(self, CHAN_NO_PHS_ADD + CHAN_VOICE,
						self->moveinfo.sound_end,
						1, ATTN_STATIC, 0);
			}

			self->s.sound = 0;
		}
	}
	else
	{
		train_next(self);
	}
}

void
train_piece_wait(edict_t *self)
{
}

void
train_next(edict_t *self)
{
	edict_t *ent;
	vec3_t dest;
	qboolean first;

	if (!self)
	{
		return;
	}

	first = true;

again:
	if (!self->target)
	{
		return;
	}

	ent = G_PickTarget(self->target);

	if (!ent)
	{
		gi.dprintf("train_next: bad target %s\n", self->target);
		return;
	}

	self->target = ent->target;

	/* check for a teleport path_corner */
	if (ent->spawnflags & 1)
	{
		if (!first)
		{
			gi.dprintf("connected teleport path_corners, see %s at %s\n",
					ent->classname, vtos(ent->s.origin));
			return;
		}

		first = false;
		VectorSubtract(ent->s.origin, self->mins, self->s.origin);
		VectorCopy(self->s.origin, self->s.old_origin);
		self->s.event = EV_OTHER_TELEPORT;
		gi.linkentity(self);
		goto again;
	}

	if (ent->speed)
	{
		self->speed = ent->speed;
		self->moveinfo.speed = ent->speed;

		if (ent->accel)
		{
			self->moveinfo.accel = ent->accel;
		}
		else
		{
			self->moveinfo.accel = ent->speed;
		}

		if (ent->decel)
		{
			self->moveinfo.decel = ent->decel;
		}
		else
		{
			self->moveinfo.decel = ent->speed;
		}

		self->moveinfo.current_speed = 0;
	}

	self->moveinfo.wait = ent->wait;
	self->target_ent = ent;

	if (!(self->flags & FL_TEAMSLAVE))
	{
		if (self->moveinfo.sound_start)
		{
			gi.sound(self, CHAN_NO_PHS_ADD + CHAN_VOICE,
					self->moveinfo.sound_start, 1,
					ATTN_STATIC, 0);
		}

		self->s.sound = self->moveinfo.sound_middle;
	}

	VectorSubtract(ent->s.origin, self->mins, dest);
	self->moveinfo.state = STATE_TOP;
	VectorCopy(self->s.origin, self->moveinfo.start_origin);
	VectorCopy(dest, self->moveinfo.end_origin);
	Move_Calc(self, dest, train_wait);
	self->spawnflags |= TRAIN_START_ON;

	if (self->team)
	{
		edict_t *e;
		vec3_t dir, dst;

		VectorSubtract(dest, self->s.origin, dir);

		for (e = self->teamchain; e; e = e->teamchain)
		{
			VectorAdd(dir, e->s.origin, dst);
			VectorCopy(e->s.origin, e->moveinfo.start_origin);
			VectorCopy(dst, e->moveinfo.end_origin);

			e->moveinfo.state = STATE_TOP;
			e->speed = self->speed;
			e->moveinfo.speed = self->moveinfo.speed;
			e->moveinfo.accel = self->moveinfo.accel;
			e->moveinfo.decel = self->moveinfo.decel;
			e->movetype = MOVETYPE_PUSH;
			Move_Calc(e, dst, train_piece_wait);
		}
	}
}

void
train_resume(edict_t *self)
{
	edict_t *ent;
	vec3_t dest;

	if (!self)
	{
		return;
	}

	ent = self->target_ent;

	VectorSubtract(ent->s.origin, self->mins, dest);
	self->moveinfo.state = STATE_TOP;
	VectorCopy(self->s.origin, self->moveinfo.start_origin);
	VectorCopy(dest, self->moveinfo.end_origin);
	Move_Calc(self, dest, train_wait);
	self->spawnflags |= TRAIN_START_ON;
}

void
func_train_find(edict_t *self)
{
	edict_t *ent;

	if (!self)
	{
		return;
	}

	if (!self->target)
	{
		gi.dprintf("train_find: no target\n");
		return;
	}

	ent = G_PickTarget(self->target);

	if (!ent)
	{
		gi.dprintf("train_find: target %s not found\n", self->target);
		return;
	}

	self->target = ent->target;

	VectorSubtract(ent->s.origin, self->mins, self->s.origin);
	gi.linkentity(self);

	/* if not triggered, start immediately */
	if (!self->targetname)
	{
		self->spawnflags |= TRAIN_START_ON;
	}

	if (self->spawnflags & TRAIN_START_ON)
	{
		self->nextthink = level.time + FRAMETIME;
		self->think = train_next;
		self->activator = self;
	}
}

void
train_use(edict_t *self, edict_t *other /* unused */,
	   	edict_t *activator)
{
	if (!self || !activator)
	{
		return;
	}

	self->activator = activator;

	if (self->spawnflags & TRAIN_START_ON)
	{
		if (!(self->spawnflags & TRAIN_TOGGLE))
		{
			return;
		}

		self->spawnflags &= ~TRAIN_START_ON;
		VectorClear(self->velocity);
		self->nextthink = 0;
	}
	else
	{
		if (self->target_ent)
		{
			train_resume(self);
		}
		else
		{
			train_next(self);
		}
	}
}

void
SP_func_train(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->movetype = MOVETYPE_PUSH;

	VectorClear(self->s.angles);
	self->blocked = train_blocked;

	if (self->spawnflags & TRAIN_BLOCK_STOPS)
	{
		self->dmg = 0;
	}
	else
	{
		if (!self->dmg)
		{
			self->dmg = 100;
		}
	}

	self->solid = SOLID_BSP;
	gi.setmodel(self, self->model);

	if (st.noise)
	{
		self->moveinfo.sound_middle = gi.soundindex(st.noise);
	}

	if (!self->speed)
	{
		self->speed = 100;
	}

	self->moveinfo.speed = self->speed;
	self->moveinfo.accel = self->moveinfo.decel = self->moveinfo.speed;

	self->use = train_use;

	gi.linkentity(self);

	if (self->target)
	{
		/* start trains on the second frame, to make
		   sure their targets have had a chance to spawn */
		self->nextthink = level.time + FRAMETIME;
		self->think = func_train_find;
	}
	else
	{
		gi.dprintf("func_train without a target at %s\n", vtos(self->absmin));
	}
}

/*
 * QUAKED trigger_elevator (0.3 0.1 0.6) (-8 -8 -8) (8 8 8)
 */
void
trigger_elevator_use(edict_t *self, edict_t *other,
	   	edict_t *activator /* unused */)
{
	edict_t *target;

	if (!self || !other)
	{
		return;
	}

	if (self->movetarget->nextthink)
	{
		return;
	}

	if (!other->pathtarget)
	{
		gi.dprintf("elevator used with no pathtarget\n");
		return;
	}

	target = G_PickTarget(other->pathtarget);

	if (!target)
	{
		gi.dprintf("elevator used with bad pathtarget: %s\n",
				other->pathtarget);
		return;
	}

	self->movetarget->target_ent = target;
	train_resume(self->movetarget);
}

void
trigger_elevator_init(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (!self->target)
	{
		gi.dprintf("trigger_elevator has no target\n");
		return;
	}

	self->movetarget = G_PickTarget(self->target);

	if (!self->movetarget)
	{
		gi.dprintf("trigger_elevator unable to find target %s\n", self->target);
		return;
	}

	if (strcmp(self->movetarget->classname, "func_train") != 0)
	{
		gi.dprintf("trigger_elevator target %s is not a train\n", self->target);
		return;
	}

	self->use = trigger_elevator_use;
	self->svflags = SVF_NOCLIENT;
}

void
SP_trigger_elevator(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->think = trigger_elevator_init;
	self->nextthink = level.time + FRAMETIME;
}

/* ==================================================================== */

/*
 * QUAKED func_timer (0.3 0.1 0.6) (-8 -8 -8) (8 8 8) START_ON
 *
 * "wait"	base time between triggering all targets, default is 1
 * "random"	wait variance, default is 0
 *
 * so, the basic time between firing is a random time
 * between (wait - random) and (wait + random)
 *
 * "delay"			delay before first firing when turned on, default is 0
 * "pausetime"		additional delay used only the very first time
 *                  and only if spawned with START_ON
 *
 * These can used but not touched.
 */
void
func_timer_think(edict_t *self)
{
	if (!self)
	{
		return;
	}

	G_UseTargets(self, self->activator);
	self->nextthink = level.time + self->wait + crandom() * self->random;
}

void
func_timer_use(edict_t *self, edict_t *other /* unused */, edict_t *activator)
{
	if (!self)
	{
		return;
	}

	self->activator = activator;

	/* if on, turn it off */
	if (self->nextthink)
	{
		self->nextthink = 0;
		return;
	}

	/* turn it on */
	if (self->delay)
	{
		self->nextthink = level.time + self->delay;
	}
	else
	{
		func_timer_think(self);
	}
}

void
SP_func_timer(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (!self->wait)
	{
		self->wait = 1.0;
	}

	self->use = func_timer_use;
	self->think = func_timer_think;

	if (self->random >= self->wait)
	{
		self->random = self->wait - FRAMETIME;
		gi.dprintf("func_timer at %s has random >= wait\n",
				vtos(self->s.origin));
	}

	if (self->spawnflags & 1)
	{
		self->nextthink = level.time + 1.0 + st.pausetime + self->delay +
						  self->wait + crandom() * self->random;
		self->activator = self;
	}

	self->svflags = SVF_NOCLIENT;
}

/* ==================================================================== */

/*
 * QUAKED func_conveyor (0 .5 .8) ? START_ON TOGGLE
 *
 * Conveyors are stationary brushes that move what's on them.
 * The brush should be have a surface with at least one current
 * content enabled.
 *
 * speed	default 100
 */
void
func_conveyor_use(edict_t *self, edict_t *other /* unused */,
	   	edict_t *activator /* unused */)
{
	if (!self)
	{
		return;
	}

	if (self->spawnflags & 1)
	{
		self->speed = 0;
		self->spawnflags &= ~1;
	}
	else
	{
		self->speed = self->count;
		self->spawnflags |= 1;
	}

	if (!(self->spawnflags & 2))
	{
		self->count = 0;
	}
}

void
SP_func_conveyor(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (!self->speed)
	{
		self->speed = 100;
	}

	if (!(self->spawnflags & 1))
	{
		self->count = self->speed;
		self->speed = 0;
	}

	self->use = func_conveyor_use;

	gi.setmodel(self, self->model);
	self->solid = SOLID_BSP;
	gi.linkentity(self);
}

/* ==================================================================== */

/*
 * QUAKED func_door_secret (0 .5 .8) ? always_shoot 1st_left 1st_down
 * A secret door. Slide back and then to the side.
 *
 * open_once	doors never closes
 * 1st_left		1st move is left of arrow
 * 1st_down		1st move is down from arrow
 * always_shoot	door is shootebale even if targeted
 *
 * "angle"		determines the direction
 * "dmg"		damage to inflic when blocked (default 2)
 * "wait"		how long to hold in the open position (default 5, -1 means hold)
 */
void
door_secret_use(edict_t *self, edict_t *other /* unused */,
	   	edict_t *activator /*unused */)
{
	if (!self)
	{
		return;
	}

	/* make sure we're not already moving */
	if (!VectorCompare(self->s.origin, vec3_origin))
	{
		return;
	}

	Move_Calc(self, self->pos1, door_secret_move1);
	door_use_areaportals(self, true);
}

void
door_secret_move1(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->nextthink = level.time + 1.0;
	self->think = door_secret_move2;
}

void
door_secret_move2(edict_t *self)
{
	if (!self)
	{
		return;
	}

	Move_Calc(self, self->pos2, door_secret_move3);
}

void
door_secret_move3(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->wait == -1)
	{
		return;
	}

	self->nextthink = level.time + self->wait;
	self->think = door_secret_move4;
}

void
door_secret_move4(edict_t *self)
{
	if (!self)
	{
		return;
	}

	Move_Calc(self, self->pos1, door_secret_move5);
}

void
door_secret_move5(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->nextthink = level.time + 1.0;
	self->think = door_secret_move6;
}

void
door_secret_move6(edict_t *self)
{
	if (!self)
	{
		return;
	}

	Move_Calc(self, vec3_origin, door_secret_done);
}

void
door_secret_done(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (!(self->targetname) || (self->spawnflags & SECRET_ALWAYS_SHOOT))
	{
		self->health = 0;
		self->takedamage = DAMAGE_YES;
	}

	door_use_areaportals(self, false);
}

void
door_secret_blocked(edict_t *self, edict_t *other)
{
	if (!self || !other)
	{
		return;
	}

	if (!(other->svflags & SVF_MONSTER) && (!other->client))
	{
		/* give it a chance to go away on it's own terms (like gibs) */
		T_Damage(other, self, self, vec3_origin, other->s.origin,
				vec3_origin, 100000, 1, 0, MOD_CRUSH);

		/* if it's still there, nuke it */
		if (other)
		{
			/* Hack for entities without their origin near the model */
			VectorMA(other->absmin, 0.5, other->size, other->s.origin);
			BecomeExplosion1(other);
		}

		return;
	}

	if (level.time < self->touch_debounce_time)
	{
		return;
	}

	self->touch_debounce_time = level.time + 0.5;

	T_Damage(other, self, self, vec3_origin, other->s.origin,
			vec3_origin, self->dmg, 1, 0, MOD_CRUSH);
}

void
door_secret_die(edict_t *self, edict_t *inflictor /* unused */,
		edict_t *attacker, int damage /* unused */,
		vec3_t point /* unused */)
{
	if (!self || !attacker)
	{
		return;
	}

	self->takedamage = DAMAGE_NO;
	door_secret_use(self, attacker, attacker);
}

void
SP_func_door_secret(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	vec3_t forward, right, up;
	float side;
	float width;
	float length;

	ent->moveinfo.sound_start = gi.soundindex("doors/dr1_strt.wav");
	ent->moveinfo.sound_middle = gi.soundindex("doors/dr1_mid.wav");
	ent->moveinfo.sound_end = gi.soundindex("doors/dr1_end.wav");

	ent->movetype = MOVETYPE_PUSH;
	ent->solid = SOLID_BSP;
	gi.setmodel(ent, ent->model);

	ent->blocked = door_secret_blocked;
	ent->use = door_secret_use;

	if (!(ent->targetname) || (ent->spawnflags & SECRET_ALWAYS_SHOOT))
	{
		ent->health = 0;
		ent->takedamage = DAMAGE_YES;
		ent->die = door_secret_die;
	}

	if (!ent->dmg)
	{
		ent->dmg = 2;
	}

	if (!ent->wait)
	{
		ent->wait = 5;
	}

	ent->moveinfo.accel = ent->moveinfo.decel =
			ent->moveinfo.speed = 50;

	/* calculate positions */
	AngleVectors(ent->s.angles, forward, right, up);
	VectorClear(ent->s.angles);
	side = 1.0 - (ent->spawnflags & SECRET_1ST_LEFT);

	if (ent->spawnflags & SECRET_1ST_DOWN)
	{
		width = fabs(DotProduct(up, ent->size));
	}
	else
	{
		width = fabs(DotProduct(right, ent->size));
	}

	length = fabs(DotProduct(forward, ent->size));

	if (ent->spawnflags & SECRET_1ST_DOWN)
	{
		VectorMA(ent->s.origin, -1 * width, up, ent->pos1);
	}
	else
	{
		VectorMA(ent->s.origin, side * width, right, ent->pos1);
	}

	VectorMA(ent->pos1, length, forward, ent->pos2);

	if (ent->health)
	{
		ent->takedamage = DAMAGE_YES;
		ent->die = door_killed;
		ent->max_health = ent->health;
	}
	else if (ent->targetname && ent->message)
	{
		gi.soundindex("misc/talk.wav");
		ent->touch = door_touch;
	}

	ent->classname = "func_door";

	gi.linkentity(ent);
}

/* ==================================================================== */

/*
 * QUAKED func_killbox (1 0 0) ?
 *
 * Kills everything inside when fired,
 * irrespective of protection.
 */
void
use_killbox(edict_t *self, edict_t *other /* unused */,
		edict_t *activator /* unused */)
{
	if (!self)
	{
		return;
	}

	KillBox(self);

	/* Hack to make sure that really everything is killed */
	self->count--;

	if (!self->count)
	{
		self->think = G_FreeEdict;
		self->nextthink = level.time + 1;
	}
}

void
SP_func_killbox(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	gi.setmodel(ent, ent->model);
	ent->use = use_killbox;
	ent->svflags = SVF_NOCLIENT;
}
