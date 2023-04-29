#include "../../header/local.h"
#include "../../header/anim.h"

#include "sentien.h"

void target_laser_think (edict_t *self);
void target_laser_on (edict_t *self);
void target_laser_off (edict_t *self);

/*=========================================================================
   Sentien sound routines ... used from animation frames (and code).
  =========================================================================*/
static int sound_idle1;
static int sound_idle2;
static int sound_idle3;
static int sound_walk;
static int sound_fend;
static int sound_pain1;
static int sound_pain2;
static int sound_pain3;
static int sound_die1;
static int sound_die2;
static int sound_att1;
static int sound_att2;
static int sound_att3;


void sentien_sound_footstep(edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_walk, 1, ATTN_NORM, 0);
}

void sentien_sound_idle1(edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_idle1, 1, ATTN_NORM, 0);
}

void sentien_sound_idle2(edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_idle2, 1, ATTN_NORM, 0);
}

void sentien_sound_idle3(edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_idle3, 1, ATTN_NORM, 0);
}

void sentian_sound_att1(edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_att1, 1, ATTN_NORM, 0);
}

void sentian_sound_att2(edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_att2, 1, ATTN_NORM, 0);
}

void sentian_sound_att3(edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_att3, 1, ATTN_NORM, 0);
}

void sentian_sound_fend(edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_fend, 1, ATTN_NORM, 0);
}

void sentian_sound_pain1(edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_pain1, 1, ATTN_NORM, 0);
}

void sentian_sound_pain2(edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_pain2, 1, ATTN_NORM, 0);
}

void sentian_sound_pain3(edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_pain3, 1, ATTN_NORM, 0);
}

void sentian_sound_die1(edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_die1, 1, ATTN_NORM, 0);
}

void sentian_sound_die2(edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_die2, 1, ATTN_NORM, 0);
}


/*=========================================================================
   Sentien standing frames
  =========================================================================*/
mframe_t sentien_frames_stand1 []=
{
   {ai_stand, 0, sentien_sound_idle1},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL}
};

mframe_t sentien_frames_stand2 []=
{
   {ai_stand, 0, sentien_sound_idle2},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
};

mframe_t sentien_frames_stand3 []=
{
   {ai_stand, 0, sentien_sound_idle1},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
   {ai_stand, 0, NULL},
};

void sentien_stand(edict_t *self);
void sentien_stand_whatnow(edict_t *self);
void sentien_stand_earwax(edict_t *self);

mmove_t   sentien_move_stand1 = {FRAME_stand1start, FRAME_stand1end, 
                               sentien_frames_stand1, sentien_stand_whatnow};

mmove_t   sentien_move_stand2 = {FRAME_stand2start, FRAME_stand2end, 
                               sentien_frames_stand2, sentien_stand_whatnow};

mmove_t   sentien_move_stand3 = {FRAME_stand3start, FRAME_stand3end, 
                               sentien_frames_stand3, sentien_stand_earwax};

void sentien_stand(edict_t *self)
{
	if (!self)
	{
		return;
	}

	target_laser_off(self->laser);

	self->monsterinfo.currentmove = &sentien_move_stand1;
}

void sentien_stand_whatnow(edict_t *self)
{
	float r;
	r = random();

	if (!self)
	{
		return;
	}

	if(r < self->random)
	{
		self->monsterinfo.currentmove = &sentien_move_stand1;
		self->random -= 0.05;
	}
	else 
	{
		r = random();
		if(r < 0.5)
			self->monsterinfo.currentmove = &sentien_move_stand2;
		else
			self->monsterinfo.currentmove = &sentien_move_stand3;

		self->random = 1;
	}
}

void sentien_stand_earwax(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if(random() > 0.80)
	{
		//more ear wax damn it, try again
		self->monsterinfo.currentmove = &sentien_move_stand3;
	}
	else
		sentien_stand_whatnow(self);
}

/*=========================================================================
   Sentien walking frames
  =========================================================================*/
mframe_t sentien_frames_walk_start []=
{
   {ai_walk, 0.0, NULL},
   {ai_walk, 1.5, NULL},
   {ai_walk, 2.9, NULL},
   {ai_walk, 2.4, NULL},
   {ai_walk, 2.1, NULL},
   {ai_walk, 2.6, NULL},
   {ai_walk, 2.1, NULL},
   {ai_walk, 1.8, sentien_sound_footstep},
};

mframe_t sentien_frames_walk []=
{
   {ai_walk, 0.3, NULL},
   {ai_walk, 2.4, NULL},
   {ai_walk, 4.0, NULL},
   {ai_walk, 3.5, NULL},
   {ai_walk, 3.6, NULL},
   {ai_walk, 3.7 * 1.1, NULL},
   {ai_walk, 3.1 * 1.3, NULL},
   {ai_walk, 4.1 * 1.2, sentien_sound_footstep},

   {ai_walk, 2.0, NULL},
   {ai_walk, 2.6, NULL}, // 2.4
   {ai_walk, 3.8, NULL}, // 3.9
   {ai_walk, 3.6, NULL},
   {ai_walk, 3.6, NULL},
   {ai_walk, 4.3, NULL},
   {ai_walk, 4.2 * 1.2, NULL},
   {ai_walk, 5.2, sentien_sound_footstep}, // 4.1
};

mframe_t sentien_frames_walk_end []=
{
   {ai_walk, 0.8, NULL},
   {ai_walk, 1.0, NULL},
   {ai_walk, 1.6, NULL},
   {ai_walk, 1.4, NULL},
   {ai_walk, 1.5, NULL},
   {ai_walk, 1.4, NULL},
   {ai_walk, 1.5, NULL},
   {ai_walk, 1.8, sentien_sound_footstep},
};

void sentien_walk(edict_t *self);

mmove_t   sentien_move_walk_start = {FRAME_walkStartStart, FRAME_walkStartEnd, 
                        sentien_frames_walk_start, sentien_walk};

mmove_t   sentien_move_walk = {FRAME_walkLoopStart, FRAME_walkLoopEnd, 
                            sentien_frames_walk, NULL};

mmove_t   sentien_move_walk_end = {FRAME_walkEndStart, FRAME_walkEndEnd, 
                               sentien_frames_walk_end, sentien_stand};

void sentien_walk(edict_t *self)
{
	if (!self)
	{
		return;
	}

	target_laser_off(self->laser);

	if(self->monsterinfo.currentmove == &sentien_move_walk)
		return;

	if (self->monsterinfo.currentmove == &sentien_move_stand1 ||
		self->monsterinfo.currentmove == &sentien_move_stand2 ||
		self->monsterinfo.currentmove == &sentien_move_stand3)
	{
		self->monsterinfo.currentmove = &sentien_move_walk_start;
	}
	else
	{
		self->monsterinfo.currentmove = &sentien_move_walk;
	}
}


/*=========================================================================
   Sentien running frames
  =========================================================================*/
mframe_t sentien_frames_run_start []=
{
   {ai_run, 0.0, NULL},
   {ai_run, 1.5, NULL},
   {ai_run, 2.9, NULL},
   {ai_run, 2.4, NULL},
   {ai_run, 2.1, NULL},
   {ai_run, 2.6, NULL},
   {ai_run, 2.1, NULL},
   {ai_run, 1.8, sentien_sound_footstep},
};

mframe_t sentien_frames_run []=
{
   {ai_run, 0.3 * 1.2, NULL},
   {ai_run, 2.4, NULL},
   {ai_run, 4.0, NULL},
   {ai_run, 3.5, NULL},
   {ai_run, 3.6, NULL},
   {ai_run, 3.7 * 1.1, NULL},
   {ai_run, 3.1 * 1.3, NULL},
   {ai_run, 4.1 * 1.2, sentien_sound_footstep},

   {ai_run, 2.0, NULL},
   {ai_run, 2.6, NULL}, // 2.4
   {ai_run, 3.8, NULL}, // 3.9
   {ai_run, 3.6, NULL},
   {ai_run, 3.6, NULL},
   {ai_run, 4.3, NULL},
   {ai_run, 4.2 * 1.2, NULL},
   {ai_run, 5.2, sentien_sound_footstep}, // 4.1
};

mframe_t sentien_frames_run_end []=
{
   {ai_run, 0.8, NULL},
   {ai_run, 1.0, NULL},
   {ai_run, 1.6, NULL},
   {ai_run, 1.4, NULL},
   {ai_run, 1.5, NULL},
   {ai_run, 1.4, NULL},
   {ai_run, 1.5, NULL},
   {ai_run, 1.8, sentien_sound_footstep},
};

void sentien_run(edict_t *self);

mmove_t   sentien_move_run_start = {FRAME_walkStartStart, FRAME_walkStartEnd, 
                        sentien_frames_run_start, sentien_run};

mmove_t   sentien_move_run = {FRAME_walkLoopStart, FRAME_walkLoopEnd, 
                           sentien_frames_run, NULL};

mmove_t   sentien_move_run_end = {FRAME_walkEndStart, FRAME_walkEndEnd, 
                              sentien_frames_run_end, sentien_stand};

void sentien_run(edict_t *self)
{
	if (!self)
	{
		return;
	}

	target_laser_off(self->laser);

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		self->monsterinfo.currentmove = &sentien_move_stand1;
		return;
	}

	if (self->monsterinfo.currentmove == &sentien_move_run)
		return;

	if (self->monsterinfo.currentmove == &sentien_move_walk ||
		self->monsterinfo.currentmove == &sentien_move_run_start)
	{
		self->monsterinfo.currentmove = &sentien_move_run;
	}
	else
	{
		self->monsterinfo.currentmove = &sentien_move_run_start;
	}
}

/*=========================================================================
   Sentien blaster attack.
  =========================================================================*/
void sentien_do_blast(edict_t *self);

mframe_t sentien_frames_pre_blast_attack []=
{
   {ai_charge, 0, NULL},
   {ai_charge, 0, NULL},
   {ai_charge, 0, NULL},
   {ai_charge, 0, NULL}
};

mframe_t sentien_frames_blast_attack []=
{
   {ai_charge, 0, sentien_do_blast},
   {ai_charge, 0, sentien_do_blast},
   {ai_charge, 0, sentien_do_blast},
   {ai_charge, 0, sentien_do_blast},
   {ai_charge, 0, sentien_do_blast},
   {ai_charge, 0, sentien_do_blast},
};

mframe_t sentien_frames_post_blast_attack []=
{
   {ai_charge, 0, NULL},
   {ai_charge, 0, NULL},
   {ai_charge, 0, NULL},
   {ai_charge, 0, NULL},
};

void sentien_blast_attack(edict_t *self);
void sentien_post_blast_attack(edict_t *self);

mmove_t   sentien_move_pre_blast_attack = {   FRAME_blastPreStart, FRAME_blastPreEnd, 
                              sentien_frames_pre_blast_attack, sentien_blast_attack};

void sentien_post_blast_attack(edict_t *self);
mmove_t   sentien_move_blast_attack = {   FRAME_blastStart, FRAME_blastEnd, 
                           sentien_frames_blast_attack, sentien_post_blast_attack};

mmove_t   sentien_move_post_blast_attack = {   FRAME_blastPostStart, FRAME_blastPostEnd, 
                                 sentien_frames_post_blast_attack, sentien_run};

void sentien_blast_attack(edict_t *self)
{
	if (!self)
	{
		return;
	}

	target_laser_off(self->laser);

	self->monsterinfo.currentmove = &sentien_move_blast_attack;

	// is a player right infront?
	if (visible(self, self->enemy) &&
		infront(self, self->enemy))
	{
		self->monsterinfo.currentmove = &sentien_move_blast_attack;
	}
	else
		self->monsterinfo.currentmove = &sentien_move_post_blast_attack;
}

void sentien_post_blast_attack(edict_t *self)
{
	float refire = 0.25;

	if (!self)
	{
		return;
	}

	if (visible(self, self->enemy) &&
		infront(self, self->enemy))
	{
		if(skill->value == SKILL_MEDIUM)
			refire = 0.40;
		else if(skill->value == SKILL_HARD)
			refire = 0.60;
		else if(skill->value >= SKILL_HARDPLUS)
			refire = 0.75;

		if (random() > refire)
			self->monsterinfo.currentmove = &sentien_move_post_blast_attack;
	}
	else
		self->monsterinfo.currentmove = &sentien_move_post_blast_attack;
}

void sentien_fire_bullet (edict_t *self, vec3_t start, vec3_t dir, int damage)
{
	if (!self)
	{
		return;
	}

	if(EMPNukeCheck(self, self->s.origin))
	{
		gi.sound (self, CHAN_AUTO, gi.soundindex("items/empnuke/emp_missfire.wav"), 1, ATTN_NORM, 0);
		return;
	}

	ANIM_AIM(self, dir);
	fire_bullet (self, start, dir, 2, 4,
		DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD,
		MOD_UNKNOWN);

	sentian_sound_att1(self);
}

vec3_t sentien_flash_offset [] =
{
   // frames 116+ (hex fire)
	{23.7, 25.4, 29.6},
	{23.7, 25.3, 26.7},
	{23.7, 27.7, 28.1},
	{23.7, 27.4, 31.2},
	{23.7, 24.9, 32.3},
	{23.7, 22.5, 30.6},
	{23.7, 22.7, 27.8}
};

void sentien_do_blast(edict_t *self)
{
	vec3_t   start;
	vec3_t   forward, right;
	vec3_t   aim;
	vec3_t   end;
	int      idx;

	if (!self)
	{
		return;
	}

	idx = self->s.frame - FRAME_blastStart + 1;

	AngleVectors (self->s.angles, forward, right, NULL);

	G_ProjectSource (self->s.origin, sentien_flash_offset[0],
		forward, right, start);

	VectorCopy (self->enemy->s.origin, end);
	end[2] += self->enemy->viewheight;
	VectorSubtract (end, start, aim);

	//need to compare aim with facing to make sure we are not
	//aiming too far sideways and correct if we are.

	G_ProjectSource (self->s.origin, sentien_flash_offset[idx],
		forward, right, start);

	if(EMPNukeCheck(self, start))
	{
		gi.sound (self, CHAN_AUTO, gi.soundindex("items/empnuke/emp_missfire.wav"), 1, ATTN_NORM, 0);
		return;
	}

	sentien_fire_bullet(self, start, aim, 5);
}


/*=========================================================================
   Sentien laser attack.
  =========================================================================*/
void sentien_do_laser(edict_t *self);

mframe_t sentien_frames_pre_laser_attack []=
{
   {ai_charge, 0, NULL},
   {ai_charge, 0, NULL},
   {ai_charge, 0, NULL},
   {ai_charge, 0, NULL},
   {ai_charge, 0, NULL}
};

mframe_t sentien_frames_laser_attack []=
{
		{NULL, 0, sentien_do_laser},
		{NULL, 0, sentien_do_laser},
		{NULL, 0, sentien_do_laser},
		{NULL, 0, sentien_do_laser},
		{NULL, 0, sentien_do_laser},
		{NULL, 0, sentien_do_laser},
		{NULL, 0, sentien_do_laser},
		{NULL, 0, sentien_do_laser},
		{NULL, 0, sentien_do_laser},
		{NULL, 0, sentien_do_laser},
		{NULL, 0, sentien_do_laser}
};

mframe_t sentien_frames_post_laser_attack []=
{
   {ai_charge, 0, NULL},
   {ai_charge, 0, NULL},
   {ai_charge, 0, NULL},
   {ai_charge, 0, NULL}
};

void sentien_laser_attack(edict_t *self);
void sentien_post_laser_attack(edict_t *self);

mmove_t   sentien_move_pre_laser_attack = {   FRAME_laserPreStart, FRAME_laserPreEnd, 
                              sentien_frames_pre_laser_attack, sentien_laser_attack};

mmove_t   sentien_move_laser_attack = {   FRAME_laserStart, FRAME_laserEnd, 
                           sentien_frames_laser_attack, sentien_post_laser_attack};

mmove_t   sentien_move_post_laser_attack = {   FRAME_laserPostStart, FRAME_laserPostEnd, 
                                 sentien_frames_post_laser_attack, sentien_run};

void sentien_laser_attack(edict_t *self)
{
	if (!self)
	{
		return;
	}

	// is a player right infront?
	if (visible(self, self->enemy) &&
		infront(self, self->enemy))
	{
		self->monsterinfo.currentmove = &sentien_move_laser_attack;
	}
	else
	{
		self->monsterinfo.currentmove = &sentien_move_post_laser_attack;
		target_laser_off(self->laser);
	}
}

void sentien_post_laser_attack(edict_t *self)
{
	self->monsterinfo.currentmove = &sentien_move_post_laser_attack;
	target_laser_off(self->laser);
}

void blaster_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);

vec3_t sentien_laser_offset [] =
{
	{43.8, -22.8 + 1, 43.6 - 0.8},
	{44.2, -22.9 + 1, 43.9 - 0.8},
	{43.9, -22.8 + 1, 44.0 - 0.8},
	{43.2, -23.0 + 1, 44.0 - 0.8},
	{42.4, -23.4 + 1, 43.9 - 0.8},
	{42.0, -23.5 + 1, 44.0 - 0.8},
	{42.4, -23.3 + 1, 44.0 - 0.8},
	{43.1, -23.1 + 1, 43.9 - 0.8},
	{43.8, -22.9 + 1, 43.9 - 0.8},
	{44.2, -22.8 + 1, 44.1 - 0.8},
	{43.8, -22.8 + 1, 43.5 - 0.8}
};

void sentien_do_laser(edict_t *self)
{
	vec3_t start, end, forward, right, up;
	vec3_t   aim, ang;
	float      r;
	int idx;

	if(EMPNukeCheck(self, self->s.origin))
	{
		gi.sound (self, CHAN_AUTO, gi.soundindex("items/empnuke/emp_missfire.wav"), 1, ATTN_NORM, 0);
		return;
	}

	// if enemy isn't in range anymore, then get to the post laser routine
	if(self->s.frame == FRAME_laserStart)
	{
		target_laser_off(self->laser);
		self->laser->s.skinnum = 0xf2f2f0f0;
		target_laser_on(self->laser);
	}

	idx = (self->s.frame - FRAME_laserStart);

	AngleVectors(self->s.angles, forward, right, up);
	G_ProjectSource(self->s.origin, sentien_laser_offset[idx],
	forward, right, start);
	VectorCopy(start, self->laser->s.origin);

	if(self->s.frame == FRAME_laserStart)
	{
		VectorCopy (self->enemy->s.origin, end);
		end[2] += self->enemy->viewheight * 66/100;

		r = crandom() * 20;
		VectorMA (end, r, right, end);

		VectorSubtract (end, start, aim);
		VectorNormalize(aim);
		ANIM_AIM(self, aim);

		vectoangles(aim, ang);

		VectorCopy(ang, self->laser->s.angles);

		G_SetMovedir(self->laser->s.angles, self->laser->movedir);

		sentian_sound_att2(self);
	}
}


void sentien_attack(edict_t *self)
{
	vec3_t	vec;
	float	range;
	float	r;

	if (!self)
	{
		return;
	}

	target_laser_off(self->laser);

	//sentien_run(self); // to test walking
	//return;

	VectorSubtract (self->enemy->s.origin, self->s.origin, vec);
	range = VectorLength (vec);

	r = random();

	if(range <= 128)
		self->monsterinfo.currentmove = &sentien_move_pre_blast_attack;
	else if (range <= 500)
	{
		if (r < 0.50)
			self->monsterinfo.currentmove = &sentien_move_pre_blast_attack;
		else 
			self->monsterinfo.currentmove = &sentien_move_pre_laser_attack;
	}
	else
	{
		if (r < 0.25)
			self->monsterinfo.currentmove = &sentien_move_pre_blast_attack;
		else 
			self->monsterinfo.currentmove = &sentien_move_pre_laser_attack;
	}
}


/*=========================================================================
   Sentien fending.
  =========================================================================*/

void sentien_fend_ready (edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->monsterinfo.aiflags & AI_REDUCEDDAMAGE)
		return;
	self->monsterinfo.pausetime = level.time + 1;
}

void sentien_fend_hold (edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (level.time >= self->monsterinfo.pausetime)
	{
		self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
		self->monsterinfo.aiflags &= ~AI_REDUCEDDAMAGE;
	}
	else
	{
		self->monsterinfo.aiflags |= (AI_HOLD_FRAME | AI_REDUCEDDAMAGE);
	}
}


mframe_t sentien_frames_fend [] =
{
	{ai_move, 0,  sentian_sound_fend},
	{ai_move, 0,  NULL},
	{ai_move, 0,  NULL},
	{ai_move, 0,  NULL},
	{ai_move, 0,  sentien_fend_ready},
	{ai_move, 0,  sentien_fend_hold}, 
	{ai_move, 0,  NULL},
	{ai_move, 0,  NULL},
	{ai_move, 0,  NULL},
	{ai_move, 0,  NULL},
	{ai_move, 0,  NULL},
	{ai_move, 0,  NULL},
	{ai_move, 0,  NULL},
	{ai_move, 0,  NULL},
	{ai_move, 0,  NULL},
};
mmove_t sentien_move_fend = {FRAME_dodgeStart, FRAME_dodgeEnd, sentien_frames_fend, sentien_run};

void sentien_fend (edict_t *self, edict_t *attacker, float eta)
{
	if (!self || !attacker)
	{
		return;
	}

	// don't flinch if attacking
	if(self->monsterinfo.currentmove == &sentien_move_laser_attack ||
			self->monsterinfo.currentmove == &sentien_move_blast_attack)
		return;

	if (skill->value == SKILL_EASY)
	{
		if (random() > 0.45)
			return;
	}
	else if(skill->value == SKILL_MEDIUM)
	{
		if (random() > 0.60)
			return;
	}
	else
	{
		if (random() > 0.80)
			return;
	}

	if (!self->enemy)
		self->enemy = attacker;

	self->monsterinfo.currentmove = &sentien_move_fend;
}


/*=========================================================================
   Touch me sentien.
  =========================================================================*/
mframe_t sentien_frames_pain1 [] =
{
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL}
};

mframe_t sentien_frames_pain2 [] =
{
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL}
};

mframe_t sentien_frames_pain3 [] =
{
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL},
   {ai_move, 0, NULL}
};

mmove_t sentien_move_pain1 = {FRAME_pain1Start, FRAME_pain1End,
                            sentien_frames_pain1, sentien_run};
mmove_t sentien_move_pain2 = {FRAME_pain2Start, FRAME_pain2End,
                            sentien_frames_pain2, sentien_run};
mmove_t sentien_move_pain3 = {FRAME_pain3Start, FRAME_pain3End,
                            sentien_frames_pain3, sentien_run};

void sentien_pain (edict_t *self, edict_t *other, float kick, int damage)
{
	float r;

	if (!self)
	{
		return;
	}

	if((self->health < (self->max_health / 2)))
		self->s.skinnum |= 1;

	// less than this we don't flinch
	if (damage <= 10)
		return;

	r = random();

	if(r < 0.33)
	{
		sentian_sound_pain1(self);
	}
	else if(r < 0.66)
	{
		sentian_sound_pain2(self);
	}

	if (level.time < self->pain_debounce_time)
		return;

	if(self->monsterinfo.aiflags & AI_HOLD_FRAME)
	{
		return;
	}

	if (skill->value >= SKILL_MEDIUM)
	{
		// don't flinch if attacking
		if(self->monsterinfo.currentmove == &sentien_move_laser_attack ||
			self->monsterinfo.currentmove == &sentien_move_blast_attack)
		return;
	}
	if (skill->value == SKILL_HARDPLUS)
		return;      // no pain anims in nightmare

	target_laser_off(self->laser);

	r = random();
	if (damage > 60 && r < 0.3)
		self->monsterinfo.currentmove = &sentien_move_pain3;
	else if (damage > 30 && r < 0.5)
		self->monsterinfo.currentmove = &sentien_move_pain2;
	else if (r < 0.7)
		self->monsterinfo.currentmove = &sentien_move_pain1;

	self->pain_debounce_time = level.time + 3;
}

/*=========================================================================
   Kill me Elmo.
  =========================================================================*/
mframe_t sentien_frames_death1 [] =
{
   {ai_move, 0,  sentian_sound_die1},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL}
};

mframe_t sentien_frames_death2 [] =
{
   {ai_move, 0,  sentian_sound_die2},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL},
   {ai_move, 0,  NULL}
};

void sentien_dead(edict_t *self);

mmove_t   sentien_move_death1 = {FRAME_die1Start, FRAME_die1End,
                               sentien_frames_death1, sentien_dead};
mmove_t   sentien_move_death2 = {FRAME_die2Start, FRAME_die2End,
                               sentien_frames_death2, sentien_dead};

vec3_t sentien_death_offset [] =
{
   // right, forward
   // VectorSet (self->mins, -50, 6, -16);
   // VectorSet (self->maxs, -12, 44, 0);
	{6, -50, 0},
    {44, -12, 0},
};

#define MIN(x, y) (x < y) ? x: y
#define MAX(x, y) (x < y) ? y: x

void sentien_dead(edict_t *self)
{
	vec3_t   start, end, point;
	vec3_t   forward, right;

	if (!self)
	{
		return;
	}

	AngleVectors (self->s.angles, forward, right, NULL);
	G_ProjectSource (self->s.origin, sentien_death_offset[0],
		forward, right, point);
	VectorSubtract (point, self->s.origin, start);

	G_ProjectSource (self->s.origin, sentien_death_offset[1],
		forward, right, point);
	VectorSubtract (point, self->s.origin, end);

	VectorSet (self->mins, MIN(start[0], end[0]), MIN(start[1], end[1]), -16);
	VectorSet (self->maxs, MAX(start[0], end[0]), MAX(start[1], end[1]), 0);

	self->movetype = MOVETYPE_TOSS;
	self->svflags |= SVF_DEADMONSTER;
	self->nextthink = 0;
	gi.linkentity(self);
}

void sentien_die (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	int n;

	if (!self)
	{
		return;
	}

	target_laser_off(self->laser);

	//gib code to go here
	if (self->health <= self->gib_health)
	{
		gi.sound (self, CHAN_VOICE, gi.soundindex ("misc/udeath.wav"), 1, ATTN_NORM, 0);
		for (n= 0; n < 1 /*4*/; n++)
			ThrowGib (self, "models/objects/gibs/sm_meat/tris.md2", damage, GIB_ORGANIC);
		for (n= 0; n < 4; n++)
			ThrowGib (self, "models/objects/gibs/sm_metal/tris.md2", damage, GIB_METALLIC);
		ThrowGib (self, "models/objects/gibs/chest/tris.md2", damage, GIB_ORGANIC);
		ThrowHead (self, "models/objects/gibs/gear/tris.md2", damage, GIB_METALLIC);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
		return;

	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;
	self->s.skinnum |= 1;

	if (random() < 0.80)
		self->monsterinfo.currentmove = &sentien_move_death1;
	else
		self->monsterinfo.currentmove = &sentien_move_death2;
}


/*=========================================================================
   Spawn code.
  =========================================================================*/
void create_sentien_laser(edict_t *self)
{
	vec3_t start;
	vec3_t forward, right;

	if (!self)
	{
		return;
	}

	self->laser = G_Spawn();
	self->laser->movetype = MOVETYPE_NONE;
	self->laser->solid = SOLID_BBOX;//SOLID_NOT;
	self->laser->s.renderfx = RF_BEAM|RF_TRANSLUCENT;
	self->laser->s.modelindex = 2;
	self->laser->classname = "laser_yaya";
	self->laser->s.frame = 2;
	self->laser->owner = self;
	self->laser->s.skinnum = 0xd0d1d2d3;
	self->laser->dmg = 8;
	self->laser->think = target_laser_think;

	AngleVectors(self->s.angles, forward, right, NULL);
	G_ProjectSource(self->s.origin, sentien_laser_offset[0],
		forward, right, start);

	VectorCopy(start, self->laser->s.origin);
	VectorCopy(self->s.angles, self->laser->s.angles);

	G_SetMovedir(self->laser->s.angles, self->laser->movedir);

	gi.linkentity (self->laser);
	target_laser_off(self->laser);
}


void SP_monster_sentien_precache(void)
{
	sound_idle1 = gi.soundindex("monsters/sentien/sen_idle1.wav");
	sound_idle2 = gi.soundindex("monsters/sentien/sen_idle2.wav");
	sound_walk = gi.soundindex("monsters/sentien/sen_walk.wav");
	sound_fend = gi.soundindex("monsters/sentien/sen_fend.wav");
	sound_pain1 = gi.soundindex("monsters/sentien/sen_pain1.wav");
	sound_pain2 = gi.soundindex("monsters/sentien/sen_pain2.wav");
	sound_die1 = gi.soundindex("monsters/sentien/sen_die1.wav");
	sound_die2 = gi.soundindex("monsters/sentien/sen_die2.wav");
	sound_att1 = gi.soundindex("monsters/sentien/sen_att1.wav");
	sound_att2 = gi.soundindex("monsters/sentien/sen_att2.wav");
}


void SP_monster_sentien(edict_t *self)
{
	if (!self)
	{
		return;
	}

	SP_monster_sentien_precache();

	self->mass = 500;
	self->s.modelindex = gi.modelindex ("models/monsters/sentien/tris.md2");
	VectorSet (self->mins, -32, -32, -16);
	VectorSet (self->maxs, 32, 32, 72);
	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->health = 900;
	self->gib_health = -425;
	self->yaw_speed = 10;
	self->random = 1;

	// setup the functions
	self->pain = sentien_pain;
	self->die = sentien_die;
	self->monsterinfo.stand = sentien_stand;
	self->monsterinfo.walk = sentien_walk;
	self->monsterinfo.run = sentien_run;
	self->monsterinfo.dodge = sentien_fend;
	self->monsterinfo.attack = sentien_attack;
	self->monsterinfo.melee = NULL;
	self->monsterinfo.sight = NULL;
	self->monsterinfo.idle = NULL;

	self->monsterinfo.reducedDamageAmount = 0.85;

	self->laser = NULL;
	gi.linkentity(self);

	create_sentien_laser(self);

	if(skill->value == SKILL_HARD)
	{
		self->laser->dmg *= 1.5;
		self->yaw_speed *= 1.5;
	}
	else if(skill->value >= SKILL_HARDPLUS)
	{
		self->laser->dmg *= 2.5;
		self->yaw_speed *= 2;
	}


	self->monsterinfo.currentmove = &sentien_move_stand1;
	self->monsterinfo.scale = MODEL_SCALE;

	walkmonster_start(self);
}

