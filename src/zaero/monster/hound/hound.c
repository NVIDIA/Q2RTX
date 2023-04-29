/*
==============================================================================

hound

==============================================================================
*/

#include "../../header/local.h"
#include "hound.h"


static int	sound_pain1;
static int	sound_pain2;
static int	sound_die;
static int	sound_launch;
static int	sound_impact;
static int	sound_sight;
static int  sound_bite;
static int  sound_bitemiss;
static int  sound_jump;


void hound_stand (edict_t *self);
void hound_run (edict_t *self);
void hound_walk (edict_t *self);


void hound_launch (edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound (self, CHAN_WEAPON, sound_launch, 1, ATTN_NORM, 0);
}

void hound_sight (edict_t *self, edict_t *other)
{
	if (!self)
	{
		return;
	}

	gi.sound (self, CHAN_WEAPON, sound_sight, 1, ATTN_NORM, 0);
}

//
// STAND
//


mframe_t hound_frames_stand1 [] =
{
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},  // 10

	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL}
};
mmove_t hound_stand1 = {FRAME_stand1start, FRAME_stand1end, hound_frames_stand1, hound_stand};



mframe_t hound_frames_stand2 [] =
{
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL}, // 10

	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL},
	{ai_schoolStand, 0, NULL}, // 20

	{ai_schoolStand, 0, NULL}
};
mmove_t hound_stand2 = {FRAME_stand2start, FRAME_stand2end, hound_frames_stand2, hound_stand};



void hound_stand (edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (random() < 0.8)
	{
		self->monsterinfo.currentmove = &hound_stand1;
	}
	else
	{
		self->monsterinfo.currentmove = &hound_stand2;
	}
}

//
// RUN
//


mframe_t hound_frames_run [] =
{
	{ai_schoolRun, 60, NULL},
	{ai_schoolRun, 60, NULL},
	{ai_schoolRun, 40, NULL},
	{ai_schoolRun, 30, NULL},
	{ai_schoolRun, 30, NULL},
	{ai_schoolRun, 30, NULL},
	{ai_schoolRun, 40, NULL}
};
mmove_t hound_move_run = {FRAME_runStart, FRAME_runEnd, hound_frames_run, NULL};


void hound_run (edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
		hound_stand(self);
	else
		self->monsterinfo.currentmove = &hound_move_run;
}


//
// WALK
//

mframe_t hound_frames_walk [] =
{
	{ai_schoolWalk,  7, NULL},
	{ai_schoolWalk,  7, NULL},
	{ai_schoolWalk,  7, NULL},
	{ai_schoolWalk,  7, NULL},
	{ai_schoolWalk,  7, NULL},
	{ai_schoolWalk,  7, NULL},
	{ai_schoolWalk,  7, NULL},
	{ai_schoolWalk,  7, NULL}
};
mmove_t hound_move_walk = {FRAME_walkStart, FRAME_walkEnd, hound_frames_walk, hound_walk};


void hound_walk (edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->monsterinfo.currentmove = &hound_move_walk;
}


//
// PAIN
//


mframe_t hound_frames_pain1 [] =
{
	{ai_move, 6,	NULL},
	{ai_move, 16, NULL},
	{ai_move, -6, NULL},
	{ai_move, -7, NULL},
};
mmove_t hound_move_pain1 = {FRAME_pain1Start, FRAME_pain1End, hound_frames_pain1, hound_run};

mframe_t hound_frames_pain2 [] =
{
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 6,	NULL},
	{ai_move, 16, NULL},
	{ai_move, -6, NULL},
	{ai_move, -7, NULL},
	{ai_move, 0,	NULL},
};
mmove_t hound_move_pain2 = {FRAME_pain2Start, FRAME_pain2End, hound_frames_pain2, hound_run};


void hound_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	if (!self)
	{
		return;
	}

	if (self->health < (self->max_health / 2))
		self->s.skinnum = 1;

	if (random() < 0.5)
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 3;

	if (skill->value == SKILL_HARDPLUS)
		return;		// no pain anims in nightmare

	if (random() < 0.5)
	  self->monsterinfo.currentmove = &hound_move_pain1;
  else
	  self->monsterinfo.currentmove = &hound_move_pain2;
}

//
// MELEE
//


void hound_bite (edict_t *self)
{
	vec3_t	aim;

	if (!self)
	{
		return;
	}

	VectorSet (aim, MELEE_DISTANCE, self->mins[0], 8);
	if (fire_hit (self, aim, (30 + (rand() %5)), 100))
		gi.sound (self, CHAN_WEAPON, sound_bite, 1, ATTN_NORM, 0);
	else
		gi.sound (self, CHAN_WEAPON, sound_bitemiss, 1, ATTN_NORM, 0);
}

void hound_bite2 (edict_t *self)
{
	vec3_t	aim;

	if (!self)
	{
		return;
	}

	VectorSet (aim, MELEE_DISTANCE, self->mins[0], 8);
	fire_hit (self, aim, (30 + (rand() %5)), 100);
}



mframe_t hound_frames_attack1 [] =
{
	{ai_schoolCharge, 0,	hound_launch},
	{ai_schoolCharge, 0,	NULL},
	{ai_schoolCharge, 0,	hound_bite},
	{ai_schoolCharge, 0,	hound_bite2}
};
mmove_t hound_move_attack1 = {FRAME_attack1Start, FRAME_attack1End, hound_frames_attack1, hound_run};


mframe_t hound_frames_attack2 [] =
{
	{ai_schoolCharge, 0,	hound_launch},
	{ai_schoolCharge, 0,	NULL},
	{ai_schoolCharge, 0,	NULL},
	{ai_schoolCharge, 0,	NULL},
	{ai_schoolCharge, 0,	NULL},
	{ai_schoolCharge, 0,	NULL},
	{ai_schoolCharge, 0,	NULL},
	{ai_schoolCharge, 0,	NULL},
	{ai_schoolCharge, 0,	hound_bite},
	{ai_schoolCharge, 0,	hound_bite2},
	{ai_schoolCharge, 0,	hound_bite2},
	{ai_schoolCharge, 0,	hound_bite2},
	{ai_schoolCharge, 0,	NULL},
};
mmove_t hound_move_attack2 = {FRAME_attack2Start, FRAME_attack2End, hound_frames_attack2, hound_run};


void hound_attack (edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (random() < 0.6)
	{
		self->monsterinfo.currentmove = &hound_move_attack1;
	}
	else
	{
		self->monsterinfo.currentmove = &hound_move_attack2;
	}
}

//
// ATTACK
//
void hound_jump_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (!self || !other)
	{
		return;
	}

	if (self->health <= 0)
	{
		self->touch = NULL;
		return;
	}

	if (other->takedamage && strcmp(self->classname, other->classname) != 0)
	{
		if (VectorLength(self->velocity) > 400)
		{
			vec3_t	point;
			vec3_t	normal;
			int		damage;

			VectorCopy (self->velocity, normal);
			VectorNormalize(normal);
			VectorMA (self->s.origin, self->maxs[0], normal, point);
			damage = 40 + 10 * random();
			T_Damage (other, self, self, self->velocity, point, normal, damage, damage, 0, MOD_UNKNOWN);
		}
	}

	if (!M_CheckBottom (self))
	{
		if (self->groundentity)
		{
			self->monsterinfo.nextframe = FRAME_leapLoop;
			self->touch = NULL;
		}
		return;
	}

	self->touch = NULL;
}

void hound_jump_takeoff (edict_t *self)
{
	vec3_t	forward;

	if (!self)
	{
		return;
	}

	gi.sound (self, CHAN_VOICE, sound_jump, 1, ATTN_NORM, 0);
	AngleVectors (self->s.angles, forward, NULL, NULL);
	self->s.origin[2] += 1;
	VectorScale (forward, 400, self->velocity);
	self->velocity[2] = 200;
	self->groundentity = NULL;
	self->monsterinfo.aiflags |= AI_JUMPING;
	self->monsterinfo.attack_finished = level.time + 3;
	self->touch = hound_jump_touch;
}

void hound_check_landing (edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->groundentity)
	{
		gi.sound (self, CHAN_WEAPON, sound_impact, 1, ATTN_NORM, 0);
		self->monsterinfo.attack_finished = 0;
		self->monsterinfo.aiflags &= ~AI_JUMPING;
		return;
	}

	if (level.time > self->monsterinfo.attack_finished)
		self->monsterinfo.nextframe = FRAME_leapLoop;
	else
		self->monsterinfo.nextframe = FRAME_leapEndStart;
}

void hound_check_landing2 (edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->owner = NULL;

	if (self->groundentity)
	{
		gi.sound (self, CHAN_WEAPON, sound_impact, 1, ATTN_NORM, 0);
		self->monsterinfo.attack_finished = 0;
		self->monsterinfo.aiflags &= ~AI_JUMPING;
		return;
	}

	if (level.time > self->monsterinfo.attack_finished)
		self->monsterinfo.nextframe = FRAME_hattack1Loop;
	else
		self->monsterinfo.nextframe = FRAME_hattack1LoopEnd;
}


mframe_t hound_frames_handlerjump [] =
{
	{ai_charge,  0,	NULL},
	{ai_charge,  20,	hound_jump_takeoff},
	{ai_move,  40,	NULL},
	{ai_move,  30,	hound_check_landing2},
	{ai_move,   0,	NULL},
	{ai_move,  0,	NULL},
	{ai_move,  0,	NULL},
};


mmove_t hound_move_handlerjump = {FRAME_hattack1Sep, FRAME_hattack1End, hound_frames_handlerjump, hound_run};



mframe_t hound_frames_jump [] =
{
	{ai_charge,	 20,	NULL},
	{ai_charge,	20,	hound_jump_takeoff},
	{ai_move,	40,	NULL},
	{ai_move,	30,	hound_check_landing},
	{ai_move,	 0,	NULL},
	{ai_move,	 0,	NULL},
	{ai_move,	 0,	NULL}
};
mmove_t hound_move_jump = {FRAME_leapStart, FRAME_leapEnd, hound_frames_jump, hound_run};

void hound_jump (edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->monsterinfo.currentmove = &hound_move_jump;
}

/*
=== 
attack check routines
===
*/

qboolean hound_check_melee (edict_t *self)
{
	if(!self) {
		return false;
	}

	if (range (self, self->enemy) == RANGE_MELEE)
		return true;
	return false;
}


qboolean hound_check_jump (edict_t *self)
{
	vec3_t	v;
	float	distance;

	if(!self) {
		return false;
	}

	if (self->absmin[2] > (self->enemy->absmin[2] + 0.75 * self->enemy->size[2]))
		return false;

	if (self->absmax[2] < (self->enemy->absmin[2] + 0.25 * self->enemy->size[2]))
		return false;

	v[0] = self->s.origin[0] - self->enemy->s.origin[0];
	v[1] = self->s.origin[1] - self->enemy->s.origin[1];
	v[2] = 0;
	distance = VectorLength(v);

	if (distance < 100)
		return false;
	if (distance > 100)
	{
		if (random() < 0.9)
			return false;
	}

	return true;
}


qboolean hound_checkattack (edict_t *self)
{
	if(!self) {
		return false;
	}

	if (!self->enemy || self->enemy->health <= 0)
		return false;

	if (hound_check_melee(self))
	{
		self->monsterinfo.attack_state = AS_MELEE;
		return true;
	}

	if (hound_check_jump(self))
	{
		self->monsterinfo.attack_state = AS_MISSILE;
		return true;
	}

	return false;
}


/*
===
Death Stuff Starts
===
*/

void hound_dead (edict_t *self)
{
	if (!self)
	{
		return;
	}

	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, -8);
	self->movetype = MOVETYPE_TOSS;
	self->svflags |= SVF_DEADMONSTER;
	self->nextthink = 0;
	gi.linkentity (self);
}

mframe_t hound_frames_death [] =
{
	{ai_move, 0,	 NULL},
	{ai_move, 0,	 NULL},
	{ai_move, 0,	 NULL},
	{ai_move, 0,	 NULL},
	{ai_move, 0,	 NULL},
	{ai_move, 0,	 NULL},
	{ai_move, 0,	 NULL},
	{ai_move, 0,	 NULL},
	{ai_move, 0,	 NULL},
	{ai_move, 0,	 NULL},
	{ai_move, 0,	 NULL},
	{ai_move, 0,	 NULL}
};
mmove_t hound_move_death = {FRAME_die1Start, FRAME_die1End, hound_frames_death, hound_dead};

void hound_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	int		n;

	if (!self)
	{
		return;
	}

	// check for gib
	if (self->health <= self->gib_health)
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);
		for (n= 0; n < 2; n++)
			ThrowGib (self, "models/objects/gibs/bone/tris.md2", damage, GIB_ORGANIC);
		for (n= 0; n < 4; n++)
			ThrowGib (self, "models/objects/gibs/sm_meat/tris.md2", damage, GIB_ORGANIC);
		ThrowHead (self, "models/objects/gibs/head2/tris.md2", damage, GIB_ORGANIC);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	// regular death
	gi.sound (self, CHAN_VOICE, sound_die, 1, ATTN_NORM, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;
	self->monsterinfo.currentmove = &hound_move_death;
}


/*
===
End Death Stuff
===
*/

void SP_monster_hound_precache(void)
{
	sound_pain1 = gi.soundindex ("monsters/hound/hpain1.wav");	
	sound_pain2 = gi.soundindex ("monsters/hound/hpain2.wav");	
	sound_die = gi.soundindex ("monsters/hound/hdeth1.wav");	
	sound_launch = gi.soundindex("monsters/hound/hlaunch.wav");
	sound_impact = gi.soundindex("monsters/hound/himpact.wav");
	sound_sight = gi.soundindex("monsters/hound/hsight1.wav");
	sound_jump = gi.soundindex("monsters/hound/hjump.wav");
	sound_bite = gi.soundindex("monsters/hound/hbite1.wav");
	sound_bitemiss = gi.soundindex("monsters/hound/hbite2.wav");
}


/*QUAKED monster_hound (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
*/
void SP_monster_hound (edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (deathmatch->value)
	{
		G_FreeEdict (self);
		return;
	}

	SP_monster_hound_precache();

	self->s.modelindex = gi.modelindex ("models/monsters/guard/hound/tris.md2");
	VectorSet (self->mins, -16, -16, -24);
	VectorSet (self->maxs, 16, 16, 24);
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->yaw_speed = 30;

	self->health = 175;
	self->gib_health = -50;
	self->mass = 250;

	self->pain = hound_pain;
	self->die = hound_die;

	if (self->spawnflags & 0x8)
	{
		self->monsterinfo.aiflags = AI_SCHOOLING;
	}

	self->monsterinfo.zSchoolSightRadius = 500;
	self->monsterinfo.zSchoolMaxSpeed = 4;
	self->monsterinfo.zSchoolMinSpeed = 3;
	self->monsterinfo.zSpeedStandMax = 1;
	self->monsterinfo.zSpeedWalkMax = 3;
	self->monsterinfo.zSchoolDecayRate = 0.95;
	self->monsterinfo.zSchoolMinimumDistance = 100;

	self->monsterinfo.stand = hound_stand;
	self->monsterinfo.walk = hound_walk;
	self->monsterinfo.run = hound_run;
	self->monsterinfo.attack = hound_jump;
	self->monsterinfo.melee = hound_attack;
	self->monsterinfo.sight = hound_sight;
	self->monsterinfo.idle = hound_stand;
	self->monsterinfo.checkattack = hound_checkattack;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &hound_stand1;	
	self->monsterinfo.scale = MODEL_SCALE;

	walkmonster_start (self);
}


void monster_think (edict_t *self);
qboolean monster_start (edict_t *self);
void hound_createHound(edict_t *self, float healthPercent)
{
	edict_t *hound;

	if (!self)
	{
		return;
	}

	hound = G_Spawn();

	hound->s.modelindex = gi.modelindex ("models/monsters/guard/hound/tris.md2");
	VectorSet (hound->mins, -16, -16, -24);
	VectorSet (hound->maxs, 16, 16, 24);
	VectorCopy(self->s.origin, hound->s.origin);
	VectorCopy(self->s.old_origin, hound->s.old_origin);
	VectorCopy(self->s.angles, hound->s.angles);
	hound->movetype = MOVETYPE_STEP;
	hound->solid = SOLID_BBOX;
	hound->takedamage = DAMAGE_YES;
	hound->svflags |= SVF_MONSTER;
	hound->svflags &= ~SVF_DEADMONSTER;
	hound->s.renderfx |= RF_FRAMELERP;
	hound->clipmask = MASK_MONSTERSOLID;
	hound->deadflag = DEAD_NO;
	hound->owner = self;
	hound->yaw_speed = 30;
	hound->enemy = self->enemy;
	hound->ideal_yaw = self->ideal_yaw;

	hound->health = 175.0 * healthPercent;
	hound->gib_health = -50;
	hound->mass = 250;

	hound->pain = hound_pain;
	hound->die = hound_die;

	hound->monsterinfo.stand = hound_stand;
	hound->monsterinfo.walk = hound_walk;
	hound->monsterinfo.run = hound_run;
	hound->monsterinfo.attack = hound_jump;
	hound->monsterinfo.melee = hound_attack;
	hound->monsterinfo.sight = hound_sight;
	hound->monsterinfo.idle = hound_stand;
	hound->monsterinfo.checkattack = hound_checkattack;

	hound->monsterinfo.currentmove = &hound_move_handlerjump;	
	hound->monsterinfo.scale = MODEL_SCALE;

	hound->think = monster_think;
	hound->nextthink = level.time + FRAMETIME;

	gi.linkentity (hound);

	// move the fucker now!!!
	ai_move (hound, 20);
}


