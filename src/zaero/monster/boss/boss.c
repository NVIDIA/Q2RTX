#include "../../header/local.h"
#include "../../header/anim.h"

#include "boss.h"

static int	sound_pain1;
static int	sound_pain2;
static int	sound_pain3;
static int	sound_die1;
static int	sound_die2;
static int	sound_hookimpact;
static int	sound_sight;
static int	sound_hooklaunch;
static int	sound_hookfly;
static int	sound_swing;
static int	sound_idle1;
static int	sound_idle2;
static int	sound_walk;
static int	sound_raisegun;
static int	sound_lowergun;
static int  sound_switchattacks;
static int  sound_plamsaballfly;
static int  sound_plamsaballexplode;
static int  sound_plamsaballfire;
static int  sound_taunt1;
static int  sound_taunt2;
static int  sound_taunt3;


void fire_empnuke(edict_t	*ent, vec3_t center, int radius);
void SV_AddGravity (edict_t *ent);


void zboss_stand (edict_t *self);
void zboss_run (edict_t *self);
void zboss_run2 (edict_t *self);
void zboss_walk (edict_t *self);
void zboss_walk2(edict_t *self);
void zboss_chooseNextAttack(edict_t *self);
void zboss_reelInGraaple(edict_t *self);
void zboss_posthook(edict_t *self);
void HookDragThink (edict_t *self);
void zboss_attack (edict_t *self);


void zboss_walksound (edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound (self, CHAN_BODY, sound_walk, 1, ATTN_NORM, 0);
}


void zboss_sight (edict_t *self, edict_t *other)
{
	if (!self)
	{
		return;
	}

	gi.sound (self, CHAN_VOICE, sound_sight, 1, ATTN_NORM, 0);
}


void possibleBossTaunt(edict_t *self)
{
	if (!self)
	{
		return;
	}

	float r = random();

	if(random() < 0.10)
	{
		if(r < 0.33)
		{
			gi.sound (self, CHAN_VOICE, sound_taunt1, 1, ATTN_NORM, 0);
		}
		else if(r < 0.66)
		{
			gi.sound (self, CHAN_VOICE, sound_taunt2, 1, ATTN_NORM, 0);
		}
		else
		{
			gi.sound (self, CHAN_VOICE, sound_taunt3, 1, ATTN_NORM, 0);
		}
	}
}

//
// STAND
//

mframe_t zboss_frames_stand1 [] =
{
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},  // 9

	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},  // 19

	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},  // 29

	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
};
mmove_t zboss_stand1 = {FRAME_stand1start, FRAME_stand1end, zboss_frames_stand1, zboss_stand};

mframe_t zboss_frames_stand2 [] =
{
	{ai_stand, 0, NULL}, // 32
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL}, // 41

	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL}, // 51

	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL}	 // 56
};
mmove_t zboss_stand2 = {FRAME_stand2start, FRAME_stand2end, zboss_frames_stand2, zboss_stand};


void zboss_standidle (edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (random() < 0.8)
  {
		gi.sound (self, CHAN_VOICE, sound_idle1, 1, ATTN_NORM, 0);
  	self->monsterinfo.currentmove = &zboss_stand1;
  }
  else
  {
		gi.sound (self, CHAN_VOICE, sound_idle2, 1, ATTN_NORM, 0);
  	self->monsterinfo.currentmove = &zboss_stand2;
  }
}

//
// Post WALK/RUN leading into ilde.
//

mframe_t zboss_frames_postwalk [] =
{
	{ai_walk,  3, NULL}, // 177
	{ai_walk,  3, NULL},
	{ai_walk,  3, NULL},
	{ai_walk,  3, NULL},
	{ai_walk,  3, NULL},
	{ai_walk,  3, NULL},
	{ai_walk,  3, NULL},
	{ai_walk,  3, NULL}, // 184
};
mmove_t zboss_move_postwalk = {FRAME_postWalkStart, FRAME_postWalkEnd, zboss_frames_postwalk, zboss_standidle};


void zboss_postWalkRun (edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->monsterinfo.currentmove = &zboss_move_postwalk;
}

//
// WALK
//

mframe_t zboss_frames_prewalk [] =
{
	{ai_walk,  3, NULL}, //154
	{ai_walk,  3, NULL},
	{ai_walk,  3, NULL},
	{ai_walk,  3, NULL},
	{ai_walk,  3, NULL},
	{ai_walk,  3, NULL},
	{ai_walk,  3, NULL}, // 160
};
mmove_t zboss_move_prewalk = {FRAME_preWalkStart, FRAME_preWalkEnd, zboss_frames_prewalk, zboss_walk2};

mframe_t zboss_frames_walk [] =
{
	{ai_walk,  2, NULL},	//161
	{ai_walk,  3, NULL},
	{ai_walk,  3, NULL},
	{ai_walk,  4, NULL},
	{ai_walk,  4, NULL},
	{ai_walk,  4, NULL},
	{ai_walk,  4, NULL},
	{ai_walk,  3, zboss_walksound},
	{ai_walk,  4, NULL},
	{ai_walk,  4, NULL},	// 170
	{ai_walk,  4, NULL},
	{ai_walk,  4, NULL},
	{ai_walk,  3, NULL},
	{ai_walk,  2, NULL},
	{ai_walk,  2, NULL},
	{ai_walk,  3, zboss_walksound},						// 176
};
mmove_t zboss_move_walk = {FRAME_walkStart, FRAME_walkEnd, zboss_frames_walk, zboss_walk2};

void zboss_walk (edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->monsterinfo.currentmove = &zboss_move_prewalk;
}

void zboss_walk2(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->monsterinfo.currentmove = &zboss_move_walk;
}

//
// RUN
//

mframe_t zboss_frames_prerun [] =
{
	{ai_run,  3, NULL}, //154
	{ai_run,  3, NULL},
	{ai_run,  3, NULL},
	{ai_run,  3, NULL},
	{ai_run,  3, NULL},
	{ai_run,  3, NULL},
	{ai_run,  3, NULL}, // 160
};
mmove_t zboss_move_prerun = {FRAME_preWalkStart, FRAME_preWalkEnd, zboss_frames_prerun, zboss_run2};

mframe_t zboss_frames_run [] =
{
	{ai_run,  2, NULL},	//161
	{ai_run,  3, NULL},
	{ai_run,  3, NULL},
	{ai_run,  4, NULL},
	{ai_run,  4, NULL},
	{ai_run,  4, NULL},
	{ai_run,  4, NULL},
	{ai_run,  3, zboss_walksound},
	{ai_run,  4, NULL},
	{ai_run,  4, NULL},	// 170
	{ai_run,  4, NULL},
	{ai_run,  4, NULL},
	{ai_run,  3, NULL},
	{ai_run,  2, NULL},
	{ai_run,  2, NULL},
	{ai_run,  3, zboss_walksound},						// 176
};
mmove_t zboss_move_run = {FRAME_walkStart, FRAME_walkEnd, zboss_frames_run, NULL};

void zboss_run (edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
    zboss_stand(self);
	else
		self->monsterinfo.currentmove = &zboss_move_prerun;
}

void zboss_run2 (edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
    zboss_stand(self);
	else
		self->monsterinfo.currentmove = &zboss_move_run;
}

//
// main stand function
//
void zboss_stand (edict_t *self)
{
	if (!self)
	{
		return;
	}

	if(self->monsterinfo.currentmove == &zboss_move_prewalk ||
				self->monsterinfo.currentmove == &zboss_move_walk ||
				self->monsterinfo.currentmove == &zboss_move_prerun ||
				self->monsterinfo.currentmove == &zboss_move_run)
	{
		zboss_postWalkRun(self);
	}
	else
	{
		zboss_standidle(self);
	}
}

//
// PAIN
//

mframe_t zboss_frames_pain1 [] =
{
	{ai_move, 0, NULL},	 // 185
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}	 // 187
};
mmove_t zboss_move_pain1 = {FRAME_pain1Start, FRAME_pain1End, zboss_frames_pain1, zboss_run};

mframe_t zboss_frames_pain2 [] =
{
	{ai_move, 0,	NULL},	 // 188
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL}	 // 192
};
mmove_t zboss_move_pain2 = {FRAME_pain2Start, FRAME_pain2End, zboss_frames_pain2, zboss_run};

mframe_t zboss_frames_pain3 [] =
{
	{ai_move, 0,	NULL},	// 193
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},	// 202

	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},	// 212

	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL} // 217
};
mmove_t zboss_move_pain3 = {FRAME_pain3Start, FRAME_pain3End, zboss_frames_pain3, zboss_run};

void zboss_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	float r;
	float hbreak;

	if (!self)
	{
		return;
	}

	hbreak = (self->max_health / 3.0);

	// set the skin
	if (self->health < hbreak)
	{
		self->s.skinnum = 2;
	}
	else if (self->health < hbreak * 2)
	{
		self->s.skinnum = 1;
	}
	else
	{
		self->s.skinnum = 0;
	}

	r = random();
	if(r < 0.125)
	{
		gi.sound (self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM, 0);
	}
	else if(r < 0.25)
	{
		gi.sound (self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM, 0);
	}
	else if(r < 0.375)
	{
		gi.sound (self, CHAN_VOICE, sound_pain3, 1, ATTN_NORM, 0);
	}
	else if(r < 0.5)
	{
		gi.sound (self, CHAN_VOICE, sound_taunt1, 1, ATTN_NORM, 0);
	}
	else if(r < 0.625)
	{
		gi.sound (self, CHAN_VOICE, sound_taunt2, 1, ATTN_NORM, 0);
	}
	else if(r < 0.75)
	{
		gi.sound (self, CHAN_VOICE, sound_taunt3, 1, ATTN_NORM, 0);
	}

	if(self->bossFireCount && self->bossFireTimeout < level.time)
	{
		self->bossFireCount = 0;
	}

	if(self->bossFireCount > 40 && self->bossFireTimeout > level.time)
	{
		// that's it, we are pissed...
		if(self->zDistance < level.time)
		{
			fire_empnuke(self, self->s.origin, 1024);
			self->zDistance = level.time + 30 + (random() * 5);
		}

		zboss_attack(self);
		self->bossFireCount = 0;
		self->bossFireTimeout = 0;
		return;
	}

	self->bossFireCount++;
	self->bossFireTimeout = level.time + 1;

	if(self->health < (self->max_health / 4) && self->zDistance < level.time)
	{
		fire_empnuke(self, self->s.origin, 1024);
		self->zDistance = level.time + 30 + (random() * 5);
	}

	if (level.time < self->pain_debounce_time)
		return;

	self->pain_debounce_time = level.time + 5;

	if (skill->value == SKILL_HARDPLUS)
		return;		// no pain anims in nightmare

	if(self->laser)
		return;		// while hook is out.

	r = random();

	if(damage > 150 && r < 0.33)
	{
	  self->monsterinfo.currentmove = &zboss_move_pain3;
	}
	else if(damage > 80 && r < 0.66)
	{
	  self->monsterinfo.currentmove = &zboss_move_pain2;
	}
	else if(r < 0.60)
	{
	  self->monsterinfo.currentmove = &zboss_move_pain1;
	}
}

//
// MELEE
//

void zboss_swing (edict_t *self)
{
	if (!self)
	{
		return;
	}

	static	vec3_t	aim = {MELEE_DISTANCE, 0, -24};
	fire_hit (self, aim, (15 + (rand() % 6)), 800);
}

mframe_t zboss_frames_attack2c [] =
{
	{ai_charge, 0,	NULL},						// 110
	{ai_charge, 0,	zboss_swing},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	possibleBossTaunt},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	zboss_swing},
	{ai_charge, 0,	NULL}						// 118
};
mmove_t zboss_move_attack2c = {FRAME_attack2cStart, FRAME_attack2cEnd, zboss_frames_attack2c, zboss_posthook};

void zboss_melee2 (edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->monsterinfo.currentmove = &zboss_move_attack2c;
	gi.sound (self, CHAN_WEAPON, sound_swing, 1, ATTN_NORM, 0);
}

mframe_t zboss_frames_premelee [] =
{
	{ai_charge, 0,	NULL},	// 57
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	possibleBossTaunt},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},	// 66
};
mmove_t zboss_move_premelee = {FRAME_preHookStart, FRAME_preHookEnd, zboss_frames_premelee, zboss_melee2 };

void zboss_melee (edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound (self, CHAN_BODY, sound_raisegun, 1, ATTN_NORM, 0);
	self->monsterinfo.currentmove = &zboss_move_premelee;
}

//
// ATTACK
//


// Rocket attack

mframe_t zboss_frames_attack1b [] =
{
	{ai_charge, 0,	NULL},	// 92
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL}	// 98
};
mmove_t zboss_move_attack1b = {FRAME_attack1bStart, FRAME_attack1bEnd, zboss_frames_attack1b, zboss_chooseNextAttack };


void zboss_reloadRockets(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->monsterinfo.aiflags &= ~AI_ONESHOTTARGET;
	self->monsterinfo.currentmove = &zboss_move_attack1b;
}


static vec3_t	rocketoffset[]	=
{
	{-5, -50, 33},
	{-5, -39, 27},
	{-5, -39, 39},
	{-5, -44, 27},
	{-5, -44, 39},
	{-5, -48, 29},
	{-5, -48, 29},
};

void FireFlare(edict_t *self)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	dir;
	vec3_t	vec;

	if (!self)
	{
		return;
	}

	int offset = (self->s.frame - 71) / 3;

	AngleVectors (self->s.angles, forward, right, NULL);

	G_ProjectSource (self->s.origin, rocketoffset[offset], forward, right, start);

	if(self->monsterinfo.aiflags & AI_ONESHOTTARGET)
	{
		VectorCopy(	self->monsterinfo.shottarget, vec );
	}
	else
	{
		VectorCopy (self->enemy->s.origin, vec);
		vec[2] += self->enemy->viewheight;
	}

	VectorSubtract (vec, start, dir);
	VectorNormalize (dir);

	if(!(self->monsterinfo.aiflags & AI_ONESHOTTARGET))
	{
		ANIM_AIM(self, dir);
	}
	fire_flare (self, start, dir, 10, 1000, 10, 10);

	// play shooting sound
	gi.sound(self, CHAN_WEAPON, gi.soundindex("weapons/flare/shoot.wav"), 1, ATTN_NORM, 0);
}

void FireRocket(edict_t *self)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	dir;
	vec3_t	vec;

	if (!self)
	{
		return;
	}

	int offset = (self->s.frame - 71) / 3;

	AngleVectors (self->s.angles, forward, right, NULL);

	G_ProjectSource (self->s.origin, rocketoffset[offset], forward, right, start);

	if(self->monsterinfo.aiflags & AI_ONESHOTTARGET)
	{
		VectorCopy(	self->monsterinfo.shottarget, vec );
	}
	else
	{
		VectorCopy (self->enemy->s.origin, vec);
		vec[2] += self->enemy->viewheight;
	}

	vec[0] += (100 - (200 * random()));
	vec[1] += (100 - (200 * random()));
	vec[2] += (40 - (80 * random()));
	VectorSubtract (vec, start, dir);
	VectorNormalize (dir);

	fire_rocket (self, start, dir, 70, 500, 70+20, 70);

	gi.WriteByte (svc_muzzleflash2);
	gi.WriteShort (self - g_edicts);
	gi.WriteByte (MZ2_BOSS2_ROCKET_1);
	gi.multicast (start, MULTICAST_PVS);
}

mframe_t zboss_frames_attack1a [] =
{
	{ai_charge, 0,	FireFlare},	 // 71
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	FireRocket},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	FireRocket},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	FireRocket},
	{ai_charge, 0,	possibleBossTaunt},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	FireFlare},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	FireRocket},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	FireRocket},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},				 // 91
};
mmove_t zboss_move_attack1a = {FRAME_attack1aStart, FRAME_attack1aEnd, zboss_frames_attack1a, zboss_reloadRockets };

// hook

void zboss_reelInGraaple2(edict_t *self)
{
	vec3_t	vec, dir;
	float length;
	edict_t *enemy;
	vec3_t hookoffset = {-5, -24, 34};
	vec3_t forward, right;

	if (!self)
	{
		return;
	}

	enemy = self->laser->enemy;

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource(self->s.origin, hookoffset, forward, right, vec);
	VectorSubtract (vec, self->laser->s.origin, dir);
	length = VectorLength (dir);

	if(length <= 80 || (self->laser->think == HookDragThink && self->laser->powerarmor_time < level.time))
	{
		G_FreeEdict(self->laser);
		self->laser = NULL;

		self->s.modelindex3 = gi.modelindex ("models/monsters/bossz/grapple/tris.md2");

		if(enemy)
		{
			VectorClear(enemy->velocity);
			zboss_melee2(self);
		}
		else
		{
			zboss_chooseNextAttack(self);
		}
	}
	else
	{
		zboss_reelInGraaple(self);
	}
}

mframe_t zboss_frames_attack2b [] =
{
	{ai_charge, 0,	NULL},		 // 107
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL}		 // 109
};
mmove_t zboss_move_attack2b = {FRAME_attack2bStart, FRAME_attack2bEnd, zboss_frames_attack2b, zboss_reelInGraaple2 };

void HookDragThink (edict_t *self)
{
	vec3_t	dir, vec;
	float	speed;
	vec3_t	hookoffset	= {-5, -24, 34};
	vec3_t	forward, right;

	if (!self)
	{
		return;
	}

	if(self->enemy && self->enemy->health > 0)
	{
		VectorCopy (self->enemy->s.origin, self->s.origin);
	}

	VectorSubtract (self->owner->s.origin, self->s.origin, dir);

	AngleVectors (self->owner->s.angles, forward, right, NULL);
	G_ProjectSource(self->owner->s.origin, hookoffset, forward, right, vec);

	VectorSubtract (vec, self->s.origin, dir);
	speed = VectorLength (dir);
	VectorNormalize (dir);

	speed = 1000;
	VectorScale (dir, speed, self->velocity);

	if(self->enemy && self->enemy->health > 0)
	{
		VectorCopy (self->velocity, self->enemy->velocity);
		self->enemy->velocity[2] *= 1.3;
	}

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_MEDIC_CABLE_ATTACK);
	gi.WriteShort (self - g_edicts);
	gi.WritePosition (self->s.origin);
	gi.WritePosition (vec);
	gi.multicast (self->s.origin, MULTICAST_PVS);

	self->nextthink = level.time + FRAMETIME;
}


void HookTouch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == ent->owner)
		return;

	if (other->takedamage)
	{
		gi.sound (ent, CHAN_WEAPON, sound_hookimpact, 1, ATTN_NORM, 0);
		T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, 10, 0, 0, MOD_ROCKET);
	}

	if(other->client && other->health > 0)
	{ // alive... Let's drag the bastard back...
		ent->enemy = other;
	}

	ent->powerarmor_time = level.time + 15;
	VectorClear(ent->velocity);
	ent->nextthink = level.time + FRAMETIME;
	ent->think = HookDragThink;
	ent->s.frame = 283;
}


void HookThink(edict_t *self)
{
	vec3_t	vec;
	vec3_t	hookoffset	= {-3, -24, 34};
	vec3_t	forward, right;

	if (!self)
	{
		return;
	}

	if(self->powerarmor_time < level.time)
	{
		self->powerarmor_time = level.time + 15;
		VectorClear(self->velocity);
		self->enemy = NULL;
		self->think = HookDragThink;
		self->s.frame = 283;
	}


	AngleVectors (self->owner->s.angles, forward, right, NULL);
	G_ProjectSource(self->owner->s.origin, hookoffset, forward, right, vec);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_MEDIC_CABLE_ATTACK);
	gi.WriteShort (self - g_edicts);
	gi.WritePosition (self->s.origin);
	gi.WritePosition (vec);
	gi.multicast (self->s.origin, MULTICAST_PVS);

	self->nextthink = level.time + FRAMETIME;
}

void FireHook(edict_t *self)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	dir;
	vec3_t	vec;
	vec3_t	hookoffset	= {-1, -24, 34};
	edict_t	*hook;
	float speed;

	if (!self)
	{
		return;
	}

	AngleVectors (self->s.angles, forward, right, NULL);

	G_ProjectSource (self->s.origin, hookoffset, forward, right, start);
	VectorCopy (self->enemy->s.origin, vec);
	vec[2] += self->enemy->viewheight;
	VectorSubtract (vec, start, dir);
	VectorNormalize (dir);

  ANIM_AIM(self, dir);

	self->s.modelindex3 = 0;

	speed = 1000;

	gi.sound (self, CHAN_WEAPON, sound_hooklaunch, 1, ATTN_NORM, 0);

	self->laser = hook = G_Spawn();
	VectorCopy (start, hook->s.origin);
	VectorCopy (dir, hook->movedir);
	vectoangles (dir, hook->s.angles);
	VectorScale (dir, speed, hook->velocity);
	hook->movetype = MOVETYPE_FLYMISSILE;
	hook->clipmask = MASK_SHOT;
	hook->solid = SOLID_BBOX;
	VectorClear (hook->mins);
	VectorClear (hook->maxs);
	hook->s.modelindex = gi.modelindex ("models/monsters/bossz/grapple/tris.md2");
	hook->s.frame = 282;
	hook->owner = self;
	hook->touch = HookTouch;
	hook->powerarmor_time = level.time + 8000 / speed;
	hook->nextthink = level.time + FRAMETIME;
	hook->think = HookThink;
	hook->s.sound = sound_hookfly; // replace...
	hook->classname = "bosshook";

	gi.linkentity (hook);
}

void zboss_reelInGraaple(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->monsterinfo.currentmove = &zboss_move_attack2b;
}

mframe_t zboss_frames_attack2a [] =
{
	{ai_charge, 0,	NULL},			 // 99
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	possibleBossTaunt},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	FireHook},	 // 104
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},			 // 106
};
mmove_t zboss_move_attack2a = {FRAME_attack2aStart, FRAME_attack2aEnd, zboss_frames_attack2a, zboss_reelInGraaple };

mframe_t zboss_frames_posthook [] =
{
	{ai_charge, 0,	NULL},	 // 136
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},	 // 141
};
mmove_t zboss_move_posthook = {FRAME_postHookStart, FRAME_postHookEnd, zboss_frames_posthook, zboss_run };

void zboss_posthook(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->monsterinfo.currentmove = &zboss_move_posthook;
}

void zboss_chooseHookRocket(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if(random() < 0.2 && !(self->monsterinfo.aiflags & AI_ONESHOTTARGET))
	{
		self->monsterinfo.currentmove = &zboss_move_attack2a;
	}
	else
	{
		self->monsterinfo.currentmove = &zboss_move_attack1a;
	}
}

mframe_t zboss_frames_prehook [] =
{
	{ai_charge, 0,	NULL},	// 57
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},	// 66
};
mmove_t zboss_move_prehook = {FRAME_preHookStart, FRAME_preHookEnd, zboss_frames_prehook, zboss_chooseHookRocket };

// Plasma Cannon

void PlasmaballBlastAnim(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	ent->s.frame++;
	ent->s.skinnum++;

	if(ent->s.frame > 1)
	{
		G_FreeEdict(ent);
		return;
	}
	else
	{
		ent->nextthink = level.time + FRAMETIME;
	}
}

void Plasmaball_Explode (edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	//FIXME: if we are onground then raise our Z just a bit since we are a point?
	if (ent->enemy)
	{
		float	points;
		vec3_t	v;
		vec3_t	dir;

		VectorAdd (ent->enemy->mins, ent->enemy->maxs, v);
		VectorMA (ent->enemy->s.origin, 0.5, v, v);
		VectorSubtract (ent->s.origin, v, v);
		points = ent->dmg - 0.5 * VectorLength (v);
		VectorSubtract (ent->enemy->s.origin, ent->s.origin, dir);
		T_Damage (ent->enemy, ent, ent->owner, dir, ent->s.origin, vec3_origin, (int)points, (int)points, DAMAGE_RADIUS, MOD_UNKNOWN);
	}

	T_RadiusDamage(ent, ent->owner, ent->dmg, ent->enemy, ent->dmg_radius, MOD_UNKNOWN);

	VectorMA (ent->s.origin, -0.02, ent->velocity, ent->s.origin);
	VectorClear(ent->velocity);

	ent->movetype = MOVETYPE_NONE;
	ent->s.modelindex = gi.modelindex("models/objects/b_explode/tris.md2");
	ent->s.effects &= ~EF_BFG & ~EF_ANIM_ALLFAST;
	ent->s.frame = 0;
	ent->s.skinnum = 6;

	gi.sound (ent, CHAN_AUTO, sound_plamsaballexplode, 1, ATTN_NORM, 0);

	ent->think = PlasmaballBlastAnim;
	ent->nextthink = level.time + FRAMETIME;
}

void Plasmaball_Touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (!ent || !other)
	{
		return;
	}

	if (other == ent->owner)
		return;

	if (surf && (surf->flags & SURF_SKY))
	{
		G_FreeEdict (ent);
		return;
	}

	ent->enemy = other;
	Plasmaball_Explode (ent);
}

void fire_plasmaCannon (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float timer, float damage_radius, float distance)
{
	edict_t	*plasmaball;
	vec3_t	dir;
	vec3_t	forward, right, up;

	if (!self)
	{
		return;
	}

	vectoangles (aimdir, dir);
	AngleVectors (dir, forward, right, up);

	plasmaball = G_Spawn();
	VectorCopy (start, plasmaball->s.origin);
	VectorScale (aimdir, speed, plasmaball->velocity);
	VectorMA (plasmaball->velocity, (distance - 500) + crandom() * 10.0, up, plasmaball->velocity);
	VectorMA (plasmaball->velocity, crandom() * 10.0, right, plasmaball->velocity);
	VectorSet (plasmaball->avelocity, 300, 300, 300);
	plasmaball->movetype = MOVETYPE_BOUNCE;
	plasmaball->clipmask = MASK_SHOT;
	plasmaball->solid = SOLID_BBOX;
	VectorClear (plasmaball->mins);
	VectorClear (plasmaball->maxs);
	plasmaball->s.modelindex = gi.modelindex ("sprites/plasma1.sp2");
	plasmaball->s.effects = EF_BFG | EF_ANIM_ALLFAST;
	plasmaball->owner = self;
	plasmaball->touch = Plasmaball_Touch;
	plasmaball->nextthink = level.time + timer;
	plasmaball->think = Plasmaball_Explode;
	plasmaball->dmg = damage;
	plasmaball->dmg_radius = damage_radius;
	plasmaball->classname = "plasmaball";
	plasmaball->s.sound = sound_plamsaballfly;

	gi.sound (self, CHAN_AUTO, sound_plamsaballfire, 1, ATTN_NORM, 0);
	gi.linkentity (plasmaball);
}


static vec3_t cannonoffset[]	=
{
	{-19, -44, 30},
	{-14, -33, 32},
	{-4 , -45, 32},
	{-2 , -34, 32},
	{  7, -49, 32},
	{  6, -36, 34},
	{  6, -36, 34},
};

void FireCannon(edict_t *self)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	dir;
	vec3_t	vec;
	float distance;

	if (!self)
	{
		return;
	}

	int offset = (self->s.frame - 119) / 2;

	AngleVectors (self->s.angles, forward, right, NULL);

	G_ProjectSource (self->s.origin, cannonoffset[offset], forward, right, start);

	if(self->monsterinfo.aiflags & AI_ONESHOTTARGET)
	{
		VectorCopy(	self->monsterinfo.shottarget, vec );
	}
	else
	{
		VectorCopy (self->enemy->s.origin, vec);
		vec[2] += self->enemy->viewheight;
	}

	if(self->timeout)
	{
		if(self->seq)
		{
			VectorNegate(right, right);
		}
		VectorMA (vec, self->timeout, right, vec);
	}
	self->timeout -= 50;

	VectorSubtract (vec, start, dir);
	VectorNormalize (dir);

	VectorSubtract (self->enemy->s.origin, self->s.origin, vec);
	distance = VectorLength (vec);

	if(distance < 700)
	{
		distance = 700;
	}

	if(skill->value < SKILL_HARD)
	{
		fire_plasmaCannon (self, start, dir, 90, 700, 2.5, 90+40, distance);
	}
	else if(skill->value < SKILL_HARDPLUS)
	{
		fire_plasmaCannon (self, start, dir, 90, (int)(distance * 1.2), 2.5, 90+40, distance);
	}
	else
	{
		fire_plasmaCannon (self, start, dir, 90, (int)(distance * 1.6), 2.5, 90+40, distance);
	}
}

mframe_t zboss_frames_attack3 [] =
{
	{ai_charge, 0,	FireCannon},	// 119
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	FireCannon},	// 121
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	FireCannon},	// 123
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	FireCannon},	// 125
	{ai_charge, 0,	possibleBossTaunt},
	{ai_charge, 0,	FireCannon},	// 127
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	FireCannon},	// 129
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	FireCannon},	// 131
	{ai_charge, 0,	NULL},				// 132
};
mmove_t zboss_move_attack3 = {FRAME_attack3Start, FRAME_attack3End, zboss_frames_attack3, zboss_chooseNextAttack };


void zboss_fireCannons(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->monsterinfo.currentmove = &zboss_move_attack3;

	self->seq = 0;
	self->timeout = 150;
}

mframe_t zboss_frames_precannon [] =
{
	{ai_charge, 0,	NULL},	// 67
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},	// 70
};
mmove_t zboss_move_precannon = {FRAME_preCannonStart, FRAME_preCannonEnd, zboss_frames_precannon, zboss_fireCannons };

mframe_t zboss_frames_postcannon [] =
{
	{ai_charge, 0,	NULL},	// 133
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},	// 135
};
mmove_t zboss_move_postcannon = {FRAME_postCannonStart, FRAME_postCannonEnd, zboss_frames_postcannon, zboss_run };


void zboss_postcannon(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->monsterinfo.currentmove = &zboss_move_postcannon;
}

// switching in mid attack...

mframe_t zboss_frames_h2c [] =
{
	{ai_charge, 0,	NULL},	// 142
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},	// 147
};
mmove_t zboss_move_h2c = {FRAME_attackH2CStart, FRAME_attackH2CEnd, zboss_frames_h2c, zboss_fireCannons };


mframe_t zboss_frames_c2h [] =
{
	{ai_charge, 0,	NULL},	// 148
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	possibleBossTaunt},
	{ai_charge, 0,	NULL},
	{ai_charge, 0,	NULL},	// 153
};
mmove_t zboss_move_c2h = {FRAME_attackC2HStart, FRAME_attackC2HEnd, zboss_frames_c2h, zboss_chooseHookRocket };

void zboss_chooseNextAttack(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->enemy == NULL)
		return;

	self->monsterinfo.aiflags &= ~AI_ONESHOTTARGET;

	if(random() < 0.5 && self->enemy)
	{
		if(random() < 0.4)
		{
			if(self->monsterinfo.currentmove == &zboss_move_attack3)
			{
				gi.sound (self, CHAN_BODY, sound_switchattacks, 1, ATTN_NORM, 0);
				self->monsterinfo.currentmove = &zboss_move_c2h;
			}
			else
			{
				zboss_chooseHookRocket(self);
			}
		}
		else
		{
			if(self->monsterinfo.currentmove == &zboss_move_attack3)
			{
				zboss_fireCannons(self);
			}
			else
			{
				gi.sound (self, CHAN_BODY, sound_switchattacks, 1, ATTN_NORM, 0);
				self->monsterinfo.currentmove = &zboss_move_h2c;
			}
		}
	}
	else
	{
		gi.sound (self, CHAN_BODY, sound_lowergun, 1, ATTN_NORM, 0);

		if(self->monsterinfo.currentmove == &zboss_move_attack3)
		{
			zboss_postcannon(self);
		}
		else
		{
			zboss_posthook(self);
		}
	}
}


void zboss_attack (edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->enemy == NULL)
		return;

	gi.sound (self, CHAN_BODY, sound_raisegun, 1, ATTN_NORM, 0);

	if(random() < 0.4)
	{
		self->monsterinfo.currentmove = &zboss_move_prehook;
	}
	else
	{
		self->monsterinfo.currentmove = &zboss_move_precannon;
	}
}

/*
===
Death Stuff Starts
===
*/

void zboss_dead (edict_t *self)
{
	if (!self)
	{
		return;
	}

	VectorSet (self->mins, -32, -74, -30);
	VectorSet (self->maxs, 32, 40, 12);
	self->movetype = MOVETYPE_TOSS;
	self->svflags |= SVF_DEADMONSTER;
	self->nextthink = 0;
	gi.linkentity (self);
}

mframe_t zboss_frames_death1 [] =
{
	{ai_move, 0,	NULL},	// 218
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},	// 227

	{ai_move, 0,	NULL}, // 228
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},	// 236
};
mmove_t zboss_move_death1 = {FRAME_die1Start, FRAME_die1End, zboss_frames_death1, zboss_dead};

void FireDeadRocket1(edict_t *self)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	rocketoffset	= {-26, -26, 25};

	if (!self)
	{
		return;
	}

	AngleVectors (self->s.angles, forward, right, NULL);

	G_ProjectSource (self->s.origin, rocketoffset, forward, right, start);

	fire_rocket (self, start, forward, 70, 500, 70+20, 70);

	gi.WriteByte (svc_muzzleflash2);
	gi.WriteShort (self - g_edicts);
	gi.WriteByte (MZ2_BOSS2_ROCKET_1);
	gi.multicast (start, MULTICAST_PVS);
}

void FireDeadRocket2(edict_t *self)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	rocketoffset	= {-16, -21, 20};

	if (!self)
	{
		return;
	}

	AngleVectors (self->s.angles, forward, right, NULL);

	G_ProjectSource (self->s.origin, rocketoffset, forward, right, start);

	forward[1] += 10;
	fire_rocket (self, start, forward, 70, 500, 70+20, 70);

	gi.WriteByte (svc_muzzleflash2);
	gi.WriteShort (self - g_edicts);
	gi.WriteByte (MZ2_BOSS2_ROCKET_1);
	gi.multicast (start, MULTICAST_PVS);
}

void FireDeadRocket3(edict_t *self)
{
	vec3_t	forward, right, up;
	vec3_t	start;
	vec3_t	rocketoffset	= {-17, -20, 30};

	if (!self)
	{
		return;
	}

	AngleVectors (self->s.angles, forward, right, up);

	G_ProjectSource (self->s.origin, rocketoffset, forward, right, start);

	fire_rocket (self, start, up, 70, 500, 70+20, 70);

	gi.WriteByte (svc_muzzleflash2);
	gi.WriteShort (self - g_edicts);
	gi.WriteByte (MZ2_BOSS2_ROCKET_1);
	gi.multicast (start, MULTICAST_PVS);
}

void FireDeadRocket4(edict_t *self)
{

	vec3_t	forward, right, up;
	vec3_t	start;
	vec3_t	rocketoffset	= {-8, -16, 17};

	if (!self)
	{
		return;
	}
	AngleVectors (self->s.angles, forward, right, up);

	G_ProjectSource (self->s.origin, rocketoffset, forward, right, start);

	fire_rocket (self, start, up, 70, 500, 70+20, 70);

	gi.WriteByte (svc_muzzleflash2);
	gi.WriteShort (self - g_edicts);
	gi.WriteByte (MZ2_BOSS2_ROCKET_1);
	gi.multicast (start, MULTICAST_PVS);
}

void FireDeadRocket5(edict_t *self)
{
	vec3_t	forward, right, up;
	vec3_t	start;
	vec3_t	rocketoffset	= {-10, -16, 30};

	if (!self)
	{
		return;
	}

	AngleVectors (self->s.angles, forward, right, up);

	G_ProjectSource (self->s.origin, rocketoffset, forward, right, start);
	VectorNegate(forward, forward);

	fire_rocket (self, start, forward, 70, 500, 70+20, 70);

	gi.WriteByte (svc_muzzleflash2);
	gi.WriteShort (self - g_edicts);
	gi.WriteByte (MZ2_BOSS2_ROCKET_1);
	gi.multicast (start, MULTICAST_PVS);
}

void FireDeadRocket6(edict_t *self)
{
	vec3_t	forward, right, up;
	vec3_t	start;
	vec3_t	rocketoffset	= {0, -18, 25};

	if (!self)
	{
		return;
	}

	AngleVectors (self->s.angles, forward, right, up);

	G_ProjectSource (self->s.origin, rocketoffset, forward, right, start);
	VectorNegate(forward, forward);
	forward[1]  -= 10;

	fire_rocket (self, start, forward, 70, 500, 70+20, 70);

	gi.WriteByte (svc_muzzleflash2);
	gi.WriteShort (self - g_edicts);
	gi.WriteByte (MZ2_BOSS2_ROCKET_1);
	gi.multicast (start, MULTICAST_PVS);
}

void FireDeadRocket7(edict_t *self)
{
	vec3_t	forward, right, up;
	vec3_t	start;
	vec3_t	rocketoffset	= {17, -27, 30};

	if (!self)
	{
		return;
	}

	AngleVectors (self->s.angles, forward, right, up);

	G_ProjectSource (self->s.origin, rocketoffset, forward, right, start);
	VectorNegate(forward, forward);
	forward[1]  -= 10;

	fire_rocket (self, start, forward, 70, 500, 70+20, 70);

	gi.WriteByte (svc_muzzleflash2);
	gi.WriteShort (self - g_edicts);
	gi.WriteByte (MZ2_BOSS2_ROCKET_1);
	gi.multicast (start, MULTICAST_PVS);
}

void FireDeadCannon1(edict_t *self)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	cannonoffset	= {9, -46, 33};

	if (!self)
	{
		return;
	}

	AngleVectors (self->s.angles, forward, right, NULL);

	G_ProjectSource (self->s.origin, cannonoffset, forward, right, start);

	fire_plasmaCannon (self, start, forward, 90, 700, 2.5, 90+40, 700);

	gi.WriteByte (svc_muzzleflash2);
	gi.WriteShort (self - g_edicts);
	gi.WriteByte (MZ2_GUNNER_GRENADE_1);
	gi.multicast (start, MULTICAST_PVS);
}

void FireDeadCannon2(edict_t *self)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	cannonoffset	= {3, -31, 37};

	if (!self)
	{
		return;
	}

	AngleVectors (self->s.angles, forward, right, NULL);

	G_ProjectSource (self->s.origin, cannonoffset, forward, right, start);

	fire_plasmaCannon (self, start, forward, 90, 700, 2.5, 90+40, 700);

	gi.WriteByte (svc_muzzleflash2);
	gi.WriteShort (self - g_edicts);
	gi.WriteByte (MZ2_GUNNER_GRENADE_1);
	gi.multicast (start, MULTICAST_PVS);
}

void FireDeadCannon3(edict_t *self)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	cannonoffset	= {-21, -19, 24};

	if (!self)
	{
		return;
	}

	AngleVectors (self->s.angles, forward, right, NULL);

	G_ProjectSource (self->s.origin, cannonoffset, forward, right, start);

	fire_plasmaCannon (self, start, forward, 90, 700, 2.5, 90+40, 700);

	gi.WriteByte (svc_muzzleflash2);
	gi.WriteShort (self - g_edicts);
	gi.WriteByte (MZ2_GUNNER_GRENADE_1);
	gi.multicast (start, MULTICAST_PVS);
}

void DeadHookTouch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (!ent || !other)
	{
		return;
	}

	if (other == ent->owner)
		return;

	if (other->takedamage)
	{
		gi.sound (ent, CHAN_WEAPON, sound_hookimpact, 1, ATTN_NORM, 0);
		T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, plane->normal, 10, 0, 0, MOD_ROCKET);
	}

	G_FreeEdict(ent);
}

void FireDeadGrapple(edict_t *self)
{
	vec3_t	forward, right, up;
	vec3_t	start;
	vec3_t	hookoffset	= {-35, 8, 28};
	edict_t	*hook;
	float speed;

	if (!self)
	{
		return;
	}

	if(self->s.modelindex3 == 0)  // hook already out...
		return;

	AngleVectors (self->s.angles, forward, right, up);

	G_ProjectSource (self->s.origin, hookoffset, forward, right, start);

	self->s.modelindex3 = 0;

	speed = 500;

	gi.sound (self, CHAN_WEAPON, sound_hooklaunch, 1, ATTN_NORM, 0);

	hook = G_Spawn();
	VectorCopy (start, hook->s.origin);
	VectorCopy (up, hook->movedir);
	vectoangles (up, hook->s.angles);
	VectorScale (up, speed, hook->velocity);
	hook->movetype = MOVETYPE_FLYMISSILE;
	hook->clipmask = MASK_SHOT;
	hook->solid = SOLID_BBOX;
	VectorClear (hook->mins);
	VectorClear (hook->maxs);
	hook->s.modelindex = gi.modelindex ("models/monsters/bossz/grapple/tris.md2");
	hook->s.frame = 282;
	hook->owner = self;
	hook->touch = DeadHookTouch;
	hook->nextthink = level.time + 8000 / speed;
	hook->think = G_FreeEdict;
	hook->s.sound = sound_hookfly; // replace...
	hook->classname = "bosshook";

	gi.linkentity (hook);
}

mframe_t zboss_frames_death2 [] =
{
	{ai_move, 0,	NULL},							// 237
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},							// 246

	{ai_move, 0,	NULL},							// 247
	{ai_move, 0,	NULL},
	{ai_move, 0,	FireDeadRocket1},	// 249
	{ai_move, 0,	FireDeadRocket2},	// 250
	{ai_move, 0,	FireDeadRocket3},	// 251
	{ai_move, 0,	FireDeadRocket4},	// 252
	{ai_move, 0,	FireDeadRocket5},	// 253
	{ai_move, 0,	FireDeadRocket6},	// 254
	{ai_move, 0,	FireDeadRocket7},	// 255
	{ai_move, 0,	NULL},						  // 256

	{ai_move, 0,	FireDeadCannon1},  // 257
	{ai_move, 0,	FireDeadCannon2},	// 258
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	FireDeadCannon3},	// 264
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},							// 266

	{ai_move, 0,	NULL},							// 267
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},							// 276

	{ai_move, 0,	NULL},							// 277
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	NULL},
	{ai_move, 0,	FireDeadGrapple},	// 281
};
mmove_t zboss_move_death2 = {FRAME_die2Start, FRAME_die2End, zboss_frames_death2, zboss_dead};

void zboss_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	int		n;

	if (!self)
	{
		return;
	}

	if(self->laser)
	{
		G_FreeEdict(self->laser);
		self->laser = NULL;
	}

	if (self->health <= self->gib_health)
	{
		self->s.modelindex2 = 0;
		self->s.modelindex3 = 0;

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

	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;

	// todo
	if (random() < 0.5)
	{
		gi.sound (self, CHAN_VOICE, sound_die1, 1, ATTN_NORM, 0);
		self->monsterinfo.currentmove = &zboss_move_death1;
	}
	else
	{
		gi.sound (self, CHAN_VOICE, sound_die2, 1, ATTN_NORM, 0);
		self->monsterinfo.currentmove = &zboss_move_death2;
	}
}


/*
===
End Death Stuff
===
*/
void SP_monster_zboss_precache(void)
{
	sound_pain1 = gi.soundindex ("monsters/bossz/bpain1.wav");
	sound_pain2 = gi.soundindex ("monsters/bossz/bpain2.wav");
	sound_pain3 = gi.soundindex ("monsters/bossz/bpain3.wav");
	sound_die1 = gi.soundindex ("monsters/bossz/bdeth1.wav");
	sound_die2 = gi.soundindex ("monsters/bossz/bdeth2.wav");
	sound_hooklaunch = gi.soundindex("monsters/bossz/bhlaunch.wav");
	sound_hookimpact = gi.soundindex("monsters/bossz/bhimpact.wav");
	sound_hookfly	= gi.soundindex("monsters/bossz/bhfly.wav");
	sound_sight = gi.soundindex("monsters/bossz/bsight1.wav");
	sound_swing = gi.soundindex("monsters/bossz/bswing.wav");
	sound_idle1 = gi.soundindex("monsters/bossz/bidle1.wav");
	sound_idle2 = gi.soundindex("monsters/bossz/bidle2.wav");
	sound_walk = gi.soundindex("monsters/bossz/bwalk.wav");
	sound_raisegun = gi.soundindex("monsters/bossz/braisegun.wav");
	sound_lowergun = gi.soundindex("monsters/bossz/blowergun.wav");
	sound_switchattacks = gi.soundindex("monsters/bossz/bswitch.wav");
	sound_plamsaballfly = gi.soundindex("monsters/bossz/bpbfly.wav");
	sound_plamsaballexplode =	gi.soundindex("monsters/bossz/bpbexplode.wav");
	sound_plamsaballfire = gi.soundindex("monsters/bossz/bpbfire.wav");
	sound_taunt1 = gi.soundindex("monsters/bossz/btaunt1.wav");
	sound_taunt2 = gi.soundindex("monsters/bossz/btaunt2.wav");
	sound_taunt3 = gi.soundindex("monsters/bossz/btaunt3.wav");
}

/*QUAKED monster_zboss (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
*/
void SP_monster_zboss (edict_t *self)
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

	SP_monster_zboss_precache();

	// precache some models and sounds
	gi.modelindex("sprites/plasma1.sp2");
	gi.modelindex("models/objects/b_explode/tris.md2");
	gi.soundindex("items/empnuke/emp_trg.wav");

	self->s.modelindex = gi.modelindex ("models/monsters/bossz/mech/tris.md2");
	self->s.modelindex2 = gi.modelindex ("models/monsters/bossz/pilot/tris.md2");
	self->s.modelindex3 = gi.modelindex ("models/monsters/bossz/grapple/tris.md2");
	VectorSet (self->mins, -32, -74, -30);
	VectorSet (self->maxs, 32, 50, 74);
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->monsterinfo.aiflags = AI_MONREDUCEDDAMAGE;
	self->monsterinfo.reducedDamageAmount = 0.25;

	if(skill->value == SKILL_EASY)
	{
		self->health = 3000;
	}
	else if(skill->value == SKILL_MEDIUM)
	{
		self->health = 4500;
	}
	else if(skill->value == SKILL_HARD)
	{
		self->health = 6000;
	}
	else
	{
		self->health = 8000;
	}

	self->gib_health = -700;
	self->mass = 1000;

	self->pain = zboss_pain;
	self->die = zboss_die;

	self->monsterinfo.stand = zboss_stand;
	self->monsterinfo.walk = zboss_walk;
	self->monsterinfo.run = zboss_run;
	self->monsterinfo.attack = zboss_attack;
	self->monsterinfo.melee = zboss_melee;
	self->monsterinfo.sight = zboss_sight;
	self->monsterinfo.idle = possibleBossTaunt;

	gi.linkentity (self);

	self->monsterinfo.currentmove = &zboss_stand1;
	self->monsterinfo.scale = MODEL_SCALE;

	walkmonster_start (self);
}

/*QUAKED target_zboss_target
*/

void trigger_zboss (edict_t *self, edict_t *other, edict_t *activator)
{
	if (!self)
	{
		return;
	}

	edict_t	*boss = NULL;

	while ((boss = G_Find (boss, FOFS(targetname), self->target)) != NULL)
	{
		if(boss->health > 0)
		{
			VectorCopy(	self->s.origin, boss->monsterinfo.shottarget );
			boss->monsterinfo.aiflags |= AI_ONESHOTTARGET;
			boss->monsterinfo.attack(boss);
		}
  }
}

void SP_target_zboss_target(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if(!self->target)
	{
		gi.dprintf("target_zboss_target does not have a target");
		G_FreeEdict (self);
		return;
	}

	self->movetype = MOVETYPE_NONE;
	self->svflags |= SVF_NOCLIENT;

	self->solid = SOLID_NOT;
	self->use = trigger_zboss;

	gi.linkentity (self);
}

