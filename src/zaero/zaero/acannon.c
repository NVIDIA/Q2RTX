#include "../header/local.h"

void angleToward(edict_t *self, vec3_t point, float speed);

// spawnflags
#define AC_SF_START_OFF			1
#define AC_SF_BERSERK			2
#define AC_SF_BERSERK_TOGGLE	4

// variables
#define AC_RANGE	2048
#define AC_TIMEOUT	2.0
#define AC_EXPLODE_DMG	150
#define AC_EXPLODE_RADIUS	384
#define AC_TURN_SPEED	6.0
#define AC_TURN_DELAY	1.0
// states
#define AC_S_IDLE	0
#define AC_S_ACTIVATING	1
#define AC_S_ACTIVE	2
#define AC_S_DEACTIVATING 3
// models
char* models[] = {	NULL, 
					"models/objects/acannon/chain/tris.md2",
					"models/objects/acannon/rocket/tris.md2",
					"models/objects/acannon/laser/tris.md2",
					"models/objects/acannon/laser/tris.md2" };
char* floorModels[] = {	NULL, 
					"",
					"models/objects/acannon/rocket2/tris.md2",
					"models/objects/acannon/laser2/tris.md2",
					"models/objects/acannon/laser2/tris.md2" };

// pitch extents
const int acPitchExtents[2][2] = {	{0,60}, // max, min
									{-60,0} 
								};

// frames filler/chain/rocket/laser
const int acIdleStart[] = { 0, 0, 0, 0, 0 };
const int acIdleEnd[] = { 0, 0, 0, 0, 0 };
const int acActStart[] = { 0, 1, 1, 1, 1 };
const int acActEnd[] = { 0, 9, 9, 9, 9 };
const int acActiveStart[] = { 0, 10, 10, 10, 10 };
const int acActiveEnd[] = { 0, 10, 10, 10, 10 };

typedef struct ac_anim_frame_s
{
	qboolean last;
	qboolean fire;
	int frame;
} ac_anim_frame_t;

typedef struct ac_anim_s
{
	int firstNonPause;
	ac_anim_frame_t frames[32];
} ac_anim_t;

ac_anim_t acFiringFrames[5] = 
{
	// dummy
	{
		0,
		{ { true, false, -1 } }
	},

	// chaingun
	{
		6,
		{
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			// start of firing sequence
			{ false, true, 11 },
			{ false, false, 12 },
			{ false, true, 13 },
			{ false, false, 14 },
			{ false, true, 15 },
			{ false, false, 16 },
			{ false, true, 17 },
			{ false, false, 18 },
			{ false, true, 19 },
			{ false, false, 20 },
			{ false, true, 21 },
			{ true, false, 2 },
		}
	},

	// rockets
	{
		6,
		{
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			// start of firing sequence
			{ false, true, 11 },
			{ false, false, 11 },
			{ false, false, 12 },
			{ false, false, 12 },
			{ false, false, 13 },
			{ false, false, 13 },
			{ false, false, 14 },
			{ false, false, 14 },
			{ false, false, 15 },
			{ false, false, 15 },
			{ false, false, 16 },
			{ true, false, 16 },
		}
	},

	// laser
	{
		6,
		{
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			// start of firing sequence
			{ false, true, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ true, false, 11 },
		}
	},

	// slow laser
	{
		6,
		{
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			// start of firing sequence
			{ false, true, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ false, false, 11 },
			{ true, false, 11 },
		}
	}
};

vec3_t fireOffset[5] = {	{0,0,0},
							{24,-4,0},
							{0,-4,0},
							{24,-5,0},
							{24,-5,0} };
const int acDeactStart[] = { 0, 23, 23, 23, 23 };
const int acDeactEnd[] = { 0, 31, 31, 31, 31 };
const qboolean turretIdle[] = { false, false, true, true }; // collapse when idle?

// turret animations
const int turretIdleStart = 0;
const int turretIdleEnd = 0;
const int turretActStart = 1;
const int turretActEnd = 9;
const int turretActiveStart = 10;
const int turretActiveEnd = 10;
const int turretDeactStart = 23;
const int turretDeactEnd = 31;

// bullet params
#define AC_BULLET_DMG	4.0
#define AC_BULLET_KICK	2.0
// rocket params
#define AC_ROCKET_DMG	100
#define AC_ROCKET_SPEED	650
#define AC_ROCKET_RADIUS_DMG	120
#define AC_ROCKET_DMG_RADIUS	120
// blaster params
#define AC_BLASTER_DMG		20
#define AC_BLASTER_SPEED	1000

void monster_autocannon_fire(edict_t *self)
{
	vec3_t forward, right, start;

	if (!self)
	{
		return;
	}

	// fire straight ahead
	AngleVectors (self->s.angles, forward, right, NULL);
	if (self->onFloor)
		VectorNegate(right, right);
	VectorMA(self->s.origin, 24, forward, start);
	G_ProjectSource (self->s.origin, fireOffset[self->style], forward, right, start);

	if(EMPNukeCheck(self, start))
	{
		gi.sound (self, CHAN_AUTO, gi.soundindex("items/empnuke/emp_missfire.wav"), 1, ATTN_NORM, 0);
		return;
	}

	// what to fire?
	switch(self->style)
	{
	case 1:
	default:
		fire_bullet(self, start, forward, AC_BULLET_DMG, AC_BULLET_KICK, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MOD_AUTOCANNON);
		gi.WriteByte (svc_muzzleflash);
		gi.WriteShort (self - g_edicts);
		gi.WriteByte (MZ_CHAINGUN2);
		gi.multicast (self->s.origin, MULTICAST_PVS);
		break;
	case 2:
		fire_rocket(self, start, forward, AC_ROCKET_DMG, AC_ROCKET_SPEED, AC_ROCKET_RADIUS_DMG, AC_ROCKET_DMG_RADIUS);
		gi.WriteByte (svc_muzzleflash);
		gi.WriteShort (self - g_edicts);
		gi.WriteByte (MZ_ROCKET);
		gi.multicast (self->s.origin, MULTICAST_PVS);
		break;
	case 3:
	case 4:
		fire_blaster (self, start, forward, AC_BLASTER_DMG, AC_BLASTER_SPEED, EF_HYPERBLASTER, true);
		gi.WriteByte (svc_muzzleflash);
		gi.WriteShort (self - g_edicts);
		gi.WriteByte (MZ_HYPERBLASTER);
		gi.multicast (self->s.origin, MULTICAST_PVS);
		break;
	}
}

qboolean angleBetween(float *ang, float *min, float *max)
{
	// directly between?
	if (*ang > *min && *ang < *max)
		return true;

	// make positive
	while(*min < 0)
		*min += 360.0;
	while(*ang < *min)
		*ang += 360.0;
	while(*max < *min)
		*max += 360.0;

	if (*ang > *min && *ang < *max)
		return true;
	else
		return false;
}

float mod180(float val)
{
	while(val > 180)
		val -= 360.0;
	while(val < -180)
		val += 360.0;
	return val;
}

qboolean canShoot(edict_t *self, edict_t *e)
{
	vec3_t delta;
	vec3_t dangles;

	if (!self || !e)
	{
		return false;
	}

	VectorSubtract(e->s.origin, self->s.origin, delta);
	vectoangles(delta, dangles);
	dangles[PITCH] = mod180(dangles[PITCH]);
	
	if ((!self->onFloor && dangles[PITCH] < 0) ||
		(self->onFloor && dangles[PITCH] > 0)) // facing up or down
		return false;


	if (self->monsterinfo.linkcount > 0)
	{
		float ideal_yaw = self->monsterinfo.attack_state;
		float max_yaw = anglemod(ideal_yaw + self->monsterinfo.linkcount);
		float min_yaw = anglemod(ideal_yaw - self->monsterinfo.linkcount);
		
		if (!angleBetween(&dangles[YAW], &min_yaw, &max_yaw))
			return false;
	}
	
	return true;
}

qboolean autocannonInfront (edict_t *self, edict_t *other)
{
	vec3_t vec;
	vec3_t angle;
	float dot;
	float min = -30.0;
	float max = 30.0;

	if (!self || !other)
	{
		return false;
	}

	// what's the yaw distance between the 2?
	VectorSubtract (other->s.origin, self->s.origin, vec);
	vectoangles(vec, angle);
	dot = angle[YAW] - self->s.angles[YAW];

	if (angleBetween(&dot, &min, &max))
		return true;
	return false;
}

void monster_autocannon_findenemy(edict_t *self)
{
	edict_t *e = NULL;

	if (!self)
	{
		return;
	}

	// can we still use our enemy?
	if (self->enemy)
	{
		if (!canShoot(self, self->enemy))
		{
			self->oldenemy = NULL;
			self->enemy = NULL;
		}
		else if (!visible(self, self->enemy))
		{
			self->oldenemy = self->enemy;
			self->enemy = NULL;
		}
		else if (self->enemy->flags & FL_NOTARGET)
		{
			self->oldenemy = NULL;
			self->enemy = NULL;
		}
		else if (self->enemy->health <= 0)
		{
			self->oldenemy = NULL;
			self->enemy = NULL;
		}
	}

	while(self->enemy == NULL)
	{
		e = findradius(e, self->s.origin, AC_RANGE);
		if (e == NULL)
		{
			if (self->oldenemy == NULL)
				return;

			if (level.time > self->timeout)
			{
				self->oldenemy = NULL;
				return;
			}
			self->enemy = self->oldenemy;
			break;
		}


		if (self->spawnflags & AC_SF_BERSERK)
		{
			// attack clients and monsters
			if (!e->client && !(e->svflags & SVF_MONSTER))
				continue;
		}
		else
		{
			// only attack clients
			if (!e->client)
				continue;
		}

		// don't target dead stuff
		if (e->health <= 0)
			continue;

		// don't target notarget stuff
		if (e->flags & FL_NOTARGET)
			continue;

		// don't target other autocannons
		if (Q_stricmp(e->classname, "monster_autocannon") == 0)
			continue;
		
		// don't target self
		if (e == self)
			continue;
		
		// can it be seen?
		if (!visible(self, e))
			continue;

		if (!autocannonInfront(self, e))
			continue;

		if (canShoot(self, e))
			self->enemy = e;
	}
}

void monster_autocannon_turn(edict_t *self)
{
	vec3_t old_angles;

	if (!self)
	{
		return;
	}

	VectorCopy(self->s.angles, old_angles);

	if (!self->enemy)
	{
		if (self->monsterinfo.linkcount > 0)
		{
			int ideal_yaw = self->monsterinfo.attack_state;
			int max_yaw = anglemod(ideal_yaw + self->monsterinfo.linkcount);
			int min_yaw = anglemod(ideal_yaw - self->monsterinfo.linkcount);

			while (max_yaw < min_yaw)
				max_yaw += 360.0;
		
			self->s.angles[YAW] += (self->monsterinfo.lefty ? -AC_TURN_SPEED : AC_TURN_SPEED);
			
			// back and forth
			if (self->s.angles[YAW] > max_yaw)
			{
				self->monsterinfo.lefty = 1;
				self->s.angles[YAW] = max_yaw;
			}
			else if (self->s.angles[YAW] < min_yaw)
			{
				self->monsterinfo.lefty = 0;
				self->s.angles[YAW] = min_yaw;
			}
		}
		else
		{
			self->s.angles[YAW] = anglemod(self->s.angles[YAW] + AC_TURN_SPEED);
		}

		// angle pitch towards 5 to 10...
		if (!self->onFloor)
		{
			if (self->s.angles[PITCH] > 10)
				self->s.angles[PITCH] -= 4;
			else if (self->s.angles[PITCH] < 5)
				self->s.angles[PITCH] += 4;
		}
		else
		{
			if (self->s.angles[PITCH] < -10)
				self->s.angles[PITCH] += 4;
			else if (self->s.angles[PITCH] > -5)
				self->s.angles[PITCH] -= 4;
		}
	}
	else
	{
		// look toward enemy mid point
		if (visible(self, self->enemy))
		{
			vec3_t offset, dest;
			VectorCopy(self->enemy->mins, offset);
			VectorAdd(offset, self->enemy->maxs, offset);
			VectorScale(offset, 0.65, offset);
			VectorAdd(self->enemy->s.origin, offset, dest);
			angleToward(self, dest, AC_TURN_SPEED);
			VectorCopy(dest, self->monsterinfo.last_sighting);
			self->timeout = level.time + AC_TIMEOUT;

			// restrict our range of movement if need be
			if (self->monsterinfo.linkcount > 0)
			{
				float amax = anglemod(self->monsterinfo.attack_state + self->monsterinfo.linkcount);
				float amin = anglemod(self->monsterinfo.attack_state - self->monsterinfo.linkcount);
				self->s.angles[YAW] = anglemod(self->s.angles[YAW]);
				if (!angleBetween(&self->s.angles[YAW], &amin, &amax))
				{
					// which is closer?
					if (self->s.angles[YAW] - amax < amin - self->s.angles[YAW])
						self->s.angles[YAW] = amin;
					else
						self->s.angles[YAW] = amax;
				}
			}

		}
		else // not visible now, so head toward last known spot
			angleToward(self, self->monsterinfo.last_sighting, AC_TURN_SPEED);
	}
	
	// get our angles between 180 and -180
	while(self->s.angles[PITCH] > 180)
		self->s.angles[PITCH] -= 360.0;
	while(self->s.angles[PITCH] < -180)
		self->s.angles[PITCH] += 360;

	// outside of the pitch extents?
	if (self->s.angles[PITCH] > acPitchExtents[self->onFloor][1])
		self->s.angles[PITCH] = acPitchExtents[self->onFloor][1];
	else if (self->s.angles[PITCH] < acPitchExtents[self->onFloor][0])
		self->s.angles[PITCH] = acPitchExtents[self->onFloor][0];
	
	// make sure the turret's angles match the gun's
	self->chain->s.angles[YAW] = self->s.angles[YAW];
	self->chain->s.angles[PITCH] = 0;

	// setup the sound
	if (VectorCompare(self->s.angles, old_angles))
		self->chain->s.sound = 0;
	else
		self->chain->s.sound = gi.soundindex("objects/acannon/ac_idle.wav");
}

void monster_autocannon_think(edict_t *self)
{
	ac_anim_frame_t frame;
	ac_anim_t anim;
	int lefty = 0;
	edict_t *old_enemy;

	if (!self)
	{
		return;
	}

	self->nextthink = level.time + FRAMETIME;

	// get an enemy
	old_enemy = self->enemy;
	monster_autocannon_findenemy(self);
	if (self->enemy != NULL && old_enemy != self->enemy)
		gi.sound(self, CHAN_VOICE, gi.soundindex("objects/acannon/ac_act.wav"), 1, ATTN_NORM, 0);

	// turn whereever
	lefty = self->monsterinfo.lefty;
	if (level.time > self->delay)
	{
		monster_autocannon_turn(self);
		if (self->monsterinfo.lefty != lefty)
			self->delay = level.time + AC_TURN_DELAY;
	}

	anim = acFiringFrames[self->style];
	frame = anim.frames[self->seq];
		
	// ok, we don't have an enemy
	if (self->enemy == NULL)
	{
		if (self->seq == 0)
		{
			// get into idle animation
			self->s.frame++;
			if (self->s.frame > acActiveEnd[self->style] ||
				self->s.frame < acActiveStart[self->style])
				self->s.frame = acActiveStart[self->style];
			return; // done, we want to wait here
		}

		// set the frame
		self->s.frame = frame.frame;

		// fire
		if (frame.fire)
			monster_autocannon_fire(self);
	
		// if we're not done with the firing sequence, we need to finish it off
		if (frame.last) // end of the loop or firing frame?
			self->seq = 0;
		else
			self->seq++;

		return;
	}

	// we have an enemy but he's not infront, go to the beginning of the firing sequence
	if (!autocannonInfront(self, self->enemy))
	{
		self->s.frame = frame.frame;
		if (self->seq == anim.firstNonPause)
			return; // done, we want to wait here

		if (frame.last) // end of the loop or firing frame?
			self->seq = anim.firstNonPause;
		else
			self->seq++;

		return;
	}


	// we have an enemy, AND he's visible
	// let's kick his ass
	self->s.frame = frame.frame;
	if (frame.fire)
		monster_autocannon_fire(self);
	
	if (frame.last) // end of the loop?
		self->seq = anim.firstNonPause;
	else
		self->seq++;
}

void monster_autocannon_explode (edict_t *ent)
{
	vec3_t origin;

	if (!ent)
	{
		return;
	}

	T_RadiusDamage(ent, ent, AC_EXPLODE_DMG, ent->enemy, AC_EXPLODE_RADIUS, MOD_TRIPBOMB);

	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);
	gi.WriteByte (svc_temp_entity);
	if (ent->waterlevel)
	{
		if (ent->groundentity)
			gi.WriteByte (TE_GRENADE_EXPLOSION_WATER);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	}
	else
	{
		if (ent->groundentity)
			gi.WriteByte (TE_GRENADE_EXPLOSION);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION);
	}
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	// set the pain skin
	ent->chain->chain->s.skinnum = 1; // pain
	ent->chain->chain->rideWith[0] = NULL;
	ent->chain->chain->rideWith[1] = NULL;
	G_FreeEdict(ent->chain);
	G_FreeEdict(ent);
}


void monster_autocannon_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	if (!self)
	{
		return;
	}

	// explode
	self->takedamage = DAMAGE_NO;
	self->think = monster_autocannon_explode;
	self->nextthink = level.time + FRAMETIME;
}

void monster_autocannon_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	if (!self || !other)
	{
		return;
	}

	// keep the enemy
	if (other->client || other->svflags & SVF_MONSTER)
		self->enemy = other;
}

void monster_autocannon_activate(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->active = AC_S_ACTIVATING;
	self->nextthink = level.time + FRAMETIME;

	// go thru the activation frames
	if (self->s.frame >= acActStart[self->style] &&
		self->s.frame < acActEnd[self->style])
	{
		if (self->s.frame == acActStart[self->style])
		{
			//gi.sound(self, CHAN_VOICE, gi.soundindex("objects/acannon/ac_out.wav"), 1, ATTN_NORM, 0);
		}
		// continue
		self->s.frame++;
		self->chain->s.frame++;
	}
	else if (self->s.frame == acActEnd[self->style])
	{
		self->s.frame = acActiveStart[self->style];
		self->chain->s.frame = turretActiveStart;
		self->think = monster_autocannon_think;
		self->active = AC_S_ACTIVE;
	}
	else
	{
		self->s.frame = acActStart[self->style];
		self->chain->s.frame = turretActStart;
	}
}

void monster_autocannon_deactivate(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->active = AC_S_DEACTIVATING;
	self->nextthink = level.time + FRAMETIME;
	
	// go thru the deactivation frames
	if (self->s.angles[PITCH] != 0)
	{
		if (self->s.angles[PITCH] > 0)
		{
			self->s.angles[PITCH] -= 5;
			if (self->s.angles[PITCH] < 0)
				self->s.angles[PITCH] = 0;
		}
		else
		{
			self->s.angles[PITCH] += 5;
			if (self->s.angles[PITCH] > 0)
				self->s.angles[PITCH] = 0;
		}
	}
	else if (self->s.frame >= acDeactStart[self->style] &&
		self->s.frame < acDeactEnd[self->style])
	{
		self->chain->s.sound = 0;

		// continue
		self->s.frame++;
		self->chain->s.frame++;
	}
	else if (self->s.frame == acDeactEnd[self->style])
	{
		self->s.frame = acIdleStart[self->style];
		self->chain->s.frame = turretIdleStart;
		self->think = NULL;
		self->nextthink = 0;
		self->chain->s.sound = 0;
		self->active = AC_S_IDLE;
	}
	else
	{
		self->s.frame = acDeactStart[self->style];
		self->chain->s.frame = turretDeactStart;
	}
}

void monster_autocannon_act(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->active == AC_S_IDLE)
	{
		if (acActStart[self->style] != -1)
			self->think = monster_autocannon_activate;
		else
		{
			self->s.frame = acActiveStart[self->style];
			self->chain->s.frame = turretActiveStart;
			self->think = monster_autocannon_think;
			self->active = AC_S_ACTIVE;
		}
		self->nextthink = level.time + FRAMETIME;
	}
	else if (self->active == AC_S_ACTIVE)
	{
		if (acDeactStart[self->style] != -1)
		{
			self->nextthink = level.time + FRAMETIME;
			self->think = monster_autocannon_deactivate;
		}
		else
		{
			if (turretIdle[self->style])
				self->chain->s.frame = turretIdleStart;
			else
				self->chain->s.frame = turretActiveStart;
			self->s.frame = acActiveStart[self->style];
			self->think = NULL;
			self->active = AC_S_IDLE;
			self->nextthink = 0;
		}
	}
}

void monster_autocannon_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (!self)
	{
		return;
	}

	// on/off or berserk toggle?
	if (self->spawnflags & AC_SF_BERSERK_TOGGLE)
	{
		if (self->spawnflags & AC_SF_BERSERK)
			self->spawnflags &= ~AC_SF_BERSERK;
		else
			self->spawnflags |= AC_SF_BERSERK;
	}
	else
		monster_autocannon_act(self);
}

void monster_autocannon_usestub(edict_t *self)
{
	if (!self)
	{
		return;
	}

	// stub
	monster_autocannon_act(self);
}

void SP_monster_autocannon(edict_t *self)
{
	edict_t *base, *turret;
	vec3_t offset;

	if (!self)
	{
		return;
	}

	if (deathmatch->value)
	{
		G_FreeEdict(self);
		return;
	}
	
	if (self->style > 4 || self->style < 1)
		self->style = 1;

	// if we're on hard or nightmare, use fast lasers
	if (skill->value >= SKILL_HARD && self->style == 4)
		self->style = 3;

	// precache some sounds and models
	gi.soundindex("objects/acannon/ac_idle.wav");
	gi.soundindex("objects/acannon/ac_act.wav");
	gi.modelindex("models/objects/rocket/tris.md2");
	gi.modelindex("models/objects/laser/tris.md2");

	// create the base
	base = G_Spawn();
	base->classname = "autocannon base";
	base->solid = SOLID_BBOX;
	VectorCopy(self->s.origin, base->s.origin);
	if (!self->onFloor)
		base->movetype = MOVETYPE_NONE;
	else
		base->movetype = MOVETYPE_RIDE; // make the base MOVETYPE_RIDE so that it can ride on trains

	if (!self->onFloor)
		base->s.modelindex = gi.modelindex("models/objects/acannon/base/tris.md2");
	else
		base->s.modelindex = gi.modelindex("models/objects/acannon/base2/tris.md2");
	gi.linkentity(base);

	// create the turret
	turret = G_Spawn();
	turret->classname = "autocannon turret";
	turret->solid = SOLID_BBOX;
	turret->movetype = MOVETYPE_NONE;
	turret->chain = base;
	VectorCopy(self->s.origin, turret->s.origin);
	if (!self->onFloor)
		turret->s.modelindex = gi.modelindex("models/objects/acannon/turret/tris.md2");
	else
		turret->s.modelindex = gi.modelindex("models/objects/acannon/turret2/tris.md2");
	if (turretIdle[self->style])
		turret->s.frame = turretIdleStart;
	else
		turret->s.frame = turretActiveStart;
	turret->s.angles[YAW] = self->s.angles[YAW];
	turret->s.angles[PITCH] = 0;
	gi.linkentity(turret);
	
	// fill in the details about ourself
	self->solid = SOLID_BBOX;
	self->movetype = MOVETYPE_NONE;
	if (!self->onFloor)
		VectorSet(offset, 0, 0, -20); // offset down a bit
	else
		VectorSet(offset, 0, 0, 20); // offset up a bit
	VectorAdd(self->s.origin, offset, self->s.origin);

	// set the bounding box
	if (!self->onFloor)
	{
		VectorSet(self->mins, -12, -12, -28);
		VectorSet(self->maxs, 12, 12, 16);
	}
	else
	{
		VectorSet(self->mins, -12, -12, -16);
		VectorSet(self->maxs, 12, 12, 28);
	}
	self->chain = turret;
	if (!self->onFloor)
		self->s.modelindex = gi.modelindex(models[self->style]);
	else
		self->s.modelindex = gi.modelindex(floorModels[self->style]);
	self->s.frame = acIdleStart[self->style];
	self->active = AC_S_IDLE;
	self->monsterinfo.lefty = 0;
	self->monsterinfo.attack_state = self->s.angles[YAW]; // used for centre of back-and-forth "search"
	self->seq = 0;
	if (st.lip)
		self->monsterinfo.linkcount = (st.lip > 0 ? st.lip : 0);

	// default health
	if (!self->health)
		self->health = 100;

	// enable/disable? ... berserk/not
	if (self->targetname)
		self->use = monster_autocannon_use;
	
	if (self->spawnflags & AC_SF_BERSERK_TOGGLE || !(self->spawnflags & AC_SF_START_OFF))
	{
		self->think = monster_autocannon_usestub;
		self->nextthink = level.time + FRAMETIME;
	}

	self->takedamage = DAMAGE_AIM;
	self->die = monster_autocannon_die;
	self->pain = monster_autocannon_pain;

	// last but not least, setup the "rideWith" information
	base->rideWith[0] = turret;
	VectorSubtract(turret->s.origin, base->s.origin, base->rideWithOffset[0]);
	base->rideWith[1] = self;
	VectorSubtract(self->s.origin, base->s.origin, base->rideWithOffset[1]);

	gi.linkentity(self);
}

void SP_monster_autocannon_floor(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->style == 1)
	{
		gi.error("monster_autocannon_floor does not permit bullet style");
		G_FreeEdict(self);
		return;
	}

	if (self->style < 1 || self->style > 4)
		self->style = 2;
	self->onFloor = 1; // signify floor mounted
	// call the other one
	SP_monster_autocannon(self);
}

