/* =======================================================================
 *
 * Final boss, stage 2 (makron).
 *
 * =======================================================================
 */

#include "../../header/local.h"
#include "boss32.h"

qboolean visible(edict_t *self, edict_t *other);

void MakronRailgun(edict_t *self);
void MakronSaveloc(edict_t *self);
void MakronHyperblaster(edict_t *self);
void makron_step_left(edict_t *self);
void makron_step_right(edict_t *self);
void makronBFG(edict_t *self);
void makron_dead(edict_t *self);

static int sound_pain4;
static int sound_pain5;
static int sound_pain6;
static int sound_death;
static int sound_step_left;
static int sound_step_right;
static int sound_attack_bfg;
static int sound_brainsplorch;
static int sound_prerailgun;
static int sound_popup;
static int sound_taunt1;
static int sound_taunt2;
static int sound_taunt3;
static int sound_hit;

void
makron_taunt(edict_t *self)
{
	float r;

  	if (!self)
	{
		return;
	}

	r = random();

	if (r <= 0.3)
	{
		gi.sound(self, CHAN_AUTO, sound_taunt1, 1, ATTN_NONE, 0);
	}
	else if (r <= 0.6)
	{
		gi.sound(self, CHAN_AUTO, sound_taunt2, 1, ATTN_NONE, 0);
	}
	else
	{
		gi.sound(self, CHAN_AUTO, sound_taunt3, 1, ATTN_NONE, 0);
	}
}

/* stand */
mframe_t makron_frames_stand[] = {
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL}, /* 10 */
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL}, /* 20 */
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL}, /* 30 */
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL}, /* 40 */
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL}, /* 50 */
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL},
	{ai_stand, 0, NULL} /* 60 */
};

mmove_t makron_move_stand = {
	FRAME_stand201,
   	FRAME_stand260,
   	makron_frames_stand,
   	NULL
};

void
makron_stand(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	self->monsterinfo.currentmove = &makron_move_stand;
}

mframe_t makron_frames_run[] = {
	{ai_run, 3, makron_step_left},
	{ai_run, 12, NULL},
	{ai_run, 8, NULL},
	{ai_run, 8, NULL},
	{ai_run, 8, makron_step_right},
	{ai_run, 6, NULL},
	{ai_run, 12, NULL},
	{ai_run, 9, NULL},
	{ai_run, 6, NULL},
	{ai_run, 12, NULL}
};

mmove_t makron_move_run = {
	FRAME_walk204,
   	FRAME_walk213,
   	makron_frames_run,
   	NULL
};

void
makron_hit(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_AUTO, sound_hit, 1, ATTN_NONE, 0);
}

void
makron_popup(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_popup, 1, ATTN_NONE, 0);
}

void
makron_step_left(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_step_left, 1, ATTN_NORM, 0);
}

void
makron_step_right(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_BODY, sound_step_right, 1, ATTN_NORM, 0);
}

void
makron_brainsplorch(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_VOICE, sound_brainsplorch, 1, ATTN_NORM, 0);
}

void
makron_prerailgun(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	gi.sound(self, CHAN_WEAPON, sound_prerailgun, 1, ATTN_NORM, 0);
}

mframe_t makron_frames_walk[] = {
	{ai_walk, 3, makron_step_left},
	{ai_walk, 12, NULL},
	{ai_walk, 8, NULL},
	{ai_walk, 8, NULL},
	{ai_walk, 8, makron_step_right},
	{ai_walk, 6, NULL},
	{ai_walk, 12, NULL},
	{ai_walk, 9, NULL},
	{ai_walk, 6, NULL},
	{ai_walk, 12, NULL}
};

mmove_t makron_move_walk = {
	FRAME_walk204,
   	FRAME_walk213,
   	makron_frames_run,
   	NULL
};

void
makron_walk(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	self->monsterinfo.currentmove = &makron_move_walk;
}

void
makron_run(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	if (self->monsterinfo.aiflags & AI_STAND_GROUND)
	{
		self->monsterinfo.currentmove = &makron_move_stand;
	}
	else
	{
		self->monsterinfo.currentmove = &makron_move_run;
	}
}

mframe_t makron_frames_pain6[] = {
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}, /* 10 */
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, makron_popup},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}, /* 20 */
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, makron_taunt},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}
};

mmove_t makron_move_pain6 = {
	FRAME_pain601,
   	FRAME_pain627,
   	makron_frames_pain6,
   	makron_run
};

mframe_t makron_frames_pain5[] = {
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}
};

mmove_t makron_move_pain5 = {
	FRAME_pain501,
   	FRAME_pain504,
   	makron_frames_pain5,
   	makron_run
};

mframe_t makron_frames_pain4[] = {
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}
};

mmove_t makron_move_pain4 = {
	FRAME_pain401,
   	FRAME_pain404,
   	makron_frames_pain4,
   	makron_run
};

mframe_t makron_frames_death2[] = {
	{ai_move, -15, NULL},
	{ai_move, 3, NULL},
	{ai_move, -12, NULL},
	{ai_move, 0, makron_step_left},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}, /* 10 */
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 11, NULL},
	{ai_move, 12, NULL},
	{ai_move, 11, makron_step_right},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}, /* 20 */
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}, /* 30 */
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 5, NULL},
	{ai_move, 7, NULL},
	{ai_move, 6, makron_step_left},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, -1, NULL},
	{ai_move, 2, NULL}, /* 40 */
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}, /* 50 */
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, -6, NULL},
	{ai_move, -4, NULL},
	{ai_move, -6, makron_step_right},
	{ai_move, -4, NULL},
	{ai_move, -4, makron_step_left},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}, /* 60 */
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, -2, NULL},
	{ai_move, -5, NULL},
	{ai_move, -3, makron_step_right},
	{ai_move, -8, NULL},
	{ai_move, -3, makron_step_left},
	{ai_move, -7, NULL},
	{ai_move, -4, NULL},
	{ai_move, -4, makron_step_right}, /* 70 */
	{ai_move, -6, NULL},
	{ai_move, -7, NULL},
	{ai_move, 0, makron_step_left},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}, /* 80 */
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, -2, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 2, NULL},
	{ai_move, 0, NULL}, /* 90 */
	{ai_move, 27, makron_hit},
	{ai_move, 26, NULL},
	{ai_move, 0, makron_brainsplorch},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL} /* 95 */
};

mmove_t makron_move_death2 = {
	FRAME_death201,
   	FRAME_death295,
   	makron_frames_death2,
   	makron_dead
};

mframe_t makron_frames_death3[] = {
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

mmove_t makron_move_death3 = {
	FRAME_death301,
   	FRAME_death320,
   	makron_frames_death3,
   	NULL
};

mframe_t makron_frames_sight[] = {
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

mmove_t makron_move_sight = {
	FRAME_active01,
   	FRAME_active13,
   	makron_frames_sight,
   	makron_run
};

void
makronBFG(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	vec3_t forward, right;
	vec3_t start;
	vec3_t dir;
	vec3_t vec;

	AngleVectors(self->s.angles, forward, right, NULL);
	G_ProjectSource(self->s.origin, monster_flash_offset[MZ2_MAKRON_BFG],
			forward, right, start);

	VectorCopy(self->enemy->s.origin, vec);
	vec[2] += self->enemy->viewheight;
	VectorSubtract(vec, start, dir);
	VectorNormalize(dir);
	gi.sound(self, CHAN_VOICE, sound_attack_bfg, 1, ATTN_NORM, 0);
	monster_fire_bfg(self, start, dir, 50, 300, 100, 300, MZ2_MAKRON_BFG);
}

mframe_t makron_frames_attack3[] = {
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, makronBFG},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}
};

mmove_t makron_move_attack3 = {
	FRAME_attak301,
   	FRAME_attak308,
   	makron_frames_attack3,
   	makron_run
};

mframe_t makron_frames_attack4[] = {
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, MakronHyperblaster}, /* fire */
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}
};

mmove_t makron_move_attack4 = {
	FRAME_attak401,
   	FRAME_attak426,
   	makron_frames_attack4,
   	makron_run
};

mframe_t makron_frames_attack5[] = {
	{ai_charge, 0, makron_prerailgun},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, NULL},
	{ai_charge, 0, MakronSaveloc},
	{ai_move, 0, MakronRailgun}, /* Fire railgun */
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL},
	{ai_move, 0, NULL}
};

mmove_t makron_move_attack5 = {
	FRAME_attak501,
   	FRAME_attak516,
   	makron_frames_attack5,
   	makron_run
};

void
MakronSaveloc(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	VectorCopy(self->enemy->s.origin, self->pos1);  /* save for aiming the shot */
	self->pos1[2] += self->enemy->viewheight;
}

void
MakronRailgun(edict_t *self)
{
	vec3_t start;
	vec3_t dir;
	vec3_t forward, right;

  	if (!self)
	{
		return;
	}

	AngleVectors(self->s.angles, forward, right, NULL);
	G_ProjectSource(self->s.origin, monster_flash_offset[MZ2_MAKRON_RAILGUN_1],
			forward, right, start);

	/* calc direction to where we targted */
	VectorSubtract(self->pos1, start, dir);
	VectorNormalize(dir);

	monster_fire_railgun(self, start, dir, 50, 100, MZ2_MAKRON_RAILGUN_1);
}

void
MakronHyperblaster(edict_t *self)
{
	vec3_t dir;
	vec3_t vec;
	vec3_t start;
	vec3_t forward, right;
	int flash_number;

  	if (!self)
	{
		return;
	}

	flash_number = MZ2_MAKRON_BLASTER_1 + (self->s.frame - FRAME_attak405);

	AngleVectors(self->s.angles, forward, right, NULL);
	G_ProjectSource(self->s.origin, monster_flash_offset[flash_number],
			forward, right, start);

	if (self->enemy)
	{
		VectorCopy(self->enemy->s.origin, vec);
		vec[2] += self->enemy->viewheight;
		VectorSubtract(vec, start, vec);
		vectoangles(vec, vec);
		dir[0] = vec[0];
	}
	else
	{
		dir[0] = 0;
	}

	if (self->s.frame <= FRAME_attak413)
	{
		dir[1] = self->s.angles[1] - 10 * (self->s.frame - FRAME_attak413);
	}
	else
	{
		dir[1] = self->s.angles[1] + 10 * (self->s.frame - FRAME_attak421);
	}

	dir[2] = 0;

	AngleVectors(dir, forward, NULL, NULL);

	monster_fire_blaster(self, start, forward, 15, 1000,
			MZ2_MAKRON_BLASTER_1, EF_BLASTER);
}

void
makron_pain(edict_t *self, edict_t *other /* unused */,
		float kick /* unused */, int damage)
{
  	if (!self)
	{
		return;
	}

	if (self->health < (self->max_health / 2))
	{
		self->s.skinnum = 1;
	}

	if (level.time < self->pain_debounce_time)
	{
		return;
	}

	/* Lessen the chance of him
	   going into his pain frames */
	if (damage <= 25)
	{
		if (random() < 0.2)
		{
			return;
		}
	}

	self->pain_debounce_time = level.time + 3;

	if (skill->value == SKILL_HARDPLUS)
	{
		return; /* no pain anims in nightmare */
	}

	if (damage <= 40)
	{
		gi.sound(self, CHAN_VOICE, sound_pain4, 1, ATTN_NONE, 0);
		self->monsterinfo.currentmove = &makron_move_pain4;
	}
	else if (damage <= 110)
	{
		gi.sound(self, CHAN_VOICE, sound_pain5, 1, ATTN_NONE, 0);
		self->monsterinfo.currentmove = &makron_move_pain5;
	}
	else
	{
		if (damage <= 150)
		{
			if (random() <= 0.45)
			{
				gi.sound(self, CHAN_VOICE, sound_pain6, 1, ATTN_NONE, 0);
				self->monsterinfo.currentmove = &makron_move_pain6;
			}
		}
		else
		{
			if (random() <= 0.35)
			{
				gi.sound(self, CHAN_VOICE, sound_pain6, 1, ATTN_NONE, 0);
				self->monsterinfo.currentmove = &makron_move_pain6;
			}
		}
	}
}

void
makron_sight(edict_t *self, edict_t *other /* unused */)
{
}

void
makron_attack(edict_t *self)
{
	float r;

  	if (!self)
	{
		return;
	}

	r = random();

	if (r <= 0.3)
	{
		self->monsterinfo.currentmove = &makron_move_attack3;
	}
	else if (r <= 0.6)
	{
		self->monsterinfo.currentmove = &makron_move_attack4;
	}
	else
	{
		self->monsterinfo.currentmove = &makron_move_attack5;
	}
}

/* Makron Torso. This needs to be spawned in */

void
makron_torso_think(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	/* detach from the makron if the legs are gone completely */
	if (self->owner && (!self->owner->inuse || (self->owner->health <= self->owner->gib_health)))
	{
		self->owner = NULL;
	}

	/* if the makron is revived the torso was put back on him */
	if (self->owner && self->owner->deadflag != DEAD_DEAD)
	{
		G_FreeEdict(self);
		return;
	}

	if (++self->s.frame >= FRAME_death320)
	{
		self->s.frame = FRAME_death301;
	}

	self->nextthink = level.time + FRAMETIME;
}

static void
makron_torso_origin(edict_t *self, edict_t *torso)
{
	vec3_t v;
	trace_t tr;

	AngleVectors(self->s.angles, v, NULL, NULL);
	VectorMA(self->s.origin, -84.0f, v, v);

	tr = gi.trace(self->s.origin, torso->mins, torso->maxs, v, self, MASK_SOLID);

	VectorCopy (tr.endpos, torso->s.origin);
}

void
makron_torso_die(edict_t *self, edict_t *inflictor /* unused */, edict_t *attacker /* unused */,
		int damage /* unused */, vec3_t point /* unused */)
{
	int n;

	if (self->health > self->gib_health)
	{
		return;
	}

	gi.sound(self, CHAN_VOICE, gi.soundindex( "misc/udeath.wav"), 1, ATTN_NORM, 0);

	ThrowGib(self, "models/objects/gibs/sm_meat/tris.md2",
			damage, GIB_ORGANIC);

	for (n = 0; n < 4; n++)
	{
		ThrowGib(self, "models/objects/gibs/sm_metal/tris.md2",
				damage, GIB_METALLIC);
	}

	G_FreeEdict(self);
}

void
makron_torso(edict_t *self)
{
	edict_t *torso;

	if (!self)
	{
		return;
	}

	torso = G_SpawnOptional();

	if (!torso)
	{
		return;
	}

	VectorCopy(self->s.angles, torso->s.angles);
	VectorSet(torso->mins, -24, -24, 0);
	VectorSet(torso->maxs, 24, 24, 16);
	makron_torso_origin(self, torso);

	torso->gib_health = -800;
	torso->takedamage = DAMAGE_YES;
	torso->die = makron_torso_die;
	torso->deadflag = DEAD_DEAD;

	torso->owner = self;
	torso->movetype = MOVETYPE_TOSS;
	torso->solid = SOLID_BBOX;
	torso->svflags = SVF_MONSTER|SVF_DEADMONSTER;
	torso->clipmask = MASK_MONSTERSOLID;
	torso->s.frame = FRAME_death301;
	torso->s.modelindex = gi.modelindex("models/monsters/boss3/rider/tris.md2");
	torso->think = makron_torso_think;
	torso->nextthink = level.time + 2 * FRAMETIME;
	torso->s.sound = gi.soundindex("makron/spine.wav");

	gi.linkentity(torso);
}

/* death */
void
makron_dead(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	VectorSet(self->mins, -48, -48, 0);
	VectorSet(self->maxs, 48, 48, 24);
	self->movetype = MOVETYPE_TOSS;
	self->svflags |= SVF_DEADMONSTER;
	self->nextthink = 0;
	gi.linkentity(self);
}

void
makron_die(edict_t *self, edict_t *inflictor /* unused */, edict_t *attacker /* unused */,
		int damage /* unused */, vec3_t point /* unused */)
{
	int n;

  	if (!self)
	{
		return;
	}

	self->s.sound = 0;

	/* check for gib */
	if (self->health <= self->gib_health)
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/udeath.wav"), 1, ATTN_NORM, 0);

		for (n = 0; n < 1 /*4*/; n++)
		{
			ThrowGib(self, "models/objects/gibs/sm_meat/tris.md2",
					damage, GIB_ORGANIC);
		}

		for (n = 0; n < 4; n++)
		{
			ThrowGib(self, "models/objects/gibs/sm_metal/tris.md2",
					damage, GIB_METALLIC);
		}

		ThrowHead(self, "models/objects/gibs/gear/tris.md2",
				damage, GIB_METALLIC);
		self->deadflag = DEAD_DEAD;
		return;
	}

	if (self->deadflag == DEAD_DEAD)
	{
		return;
	}

	/* regular death */
	gi.sound(self, CHAN_VOICE, sound_death, 1, ATTN_NONE, 0);
	self->deadflag = DEAD_DEAD;
	self->takedamage = DAMAGE_YES;

	makron_torso(self);

	/* lower bbox since the torso is gone */
	self->maxs[2] = 64;
	gi.linkentity (self);

	self->monsterinfo.currentmove = &makron_move_death2;
}

qboolean
Makron_CheckAttack(edict_t *self)
{
	vec3_t spot1, spot2;
	vec3_t temp;
	float chance;
	trace_t tr;
	int enemy_range;
	float enemy_yaw;

  	if (!self)
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
				CONTENTS_SOLID | CONTENTS_MONSTER | CONTENTS_SLIME |
				CONTENTS_LAVA);

		/* do we have a clear shot? */
		if (tr.ent != self->enemy)
		{
			return false;
		}
	}

	enemy_range = range(self, self->enemy);
	VectorSubtract(self->enemy->s.origin, self->s.origin, temp);
	enemy_yaw = vectoyaw(temp);

	self->ideal_yaw = enemy_yaw;

	/* melee attack */
	if (enemy_range == RANGE_MELEE)
	{
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
	else if (enemy_range == RANGE_NEAR)
	{
		chance = 0.4;
	}
	else if (enemy_range == RANGE_MID)
	{
		chance = 0.2;
	}
	else
	{
		return false;
	}

	if (random() < chance)
	{
		self->monsterinfo.attack_state = AS_MISSILE;
		self->monsterinfo.attack_finished = level.time + 2 * random();
		return true;
	}

	if (self->flags & FL_FLY)
	{
		if (random() < 0.3)
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

/* monster_makron */

void
MakronPrecache(void)
{
	sound_pain4 = gi.soundindex("makron/pain3.wav");
	sound_pain5 = gi.soundindex("makron/pain2.wav");
	sound_pain6 = gi.soundindex("makron/pain1.wav");
	sound_death = gi.soundindex("makron/death.wav");
	sound_step_left = gi.soundindex("makron/step1.wav");
	sound_step_right = gi.soundindex("makron/step2.wav");
	sound_attack_bfg = gi.soundindex("makron/bfg_fire.wav");
	sound_brainsplorch = gi.soundindex("makron/brain1.wav");
	sound_prerailgun = gi.soundindex("makron/rail_up.wav");
	sound_popup = gi.soundindex("makron/popup.wav");
	sound_taunt1 = gi.soundindex("makron/voice4.wav");
	sound_taunt2 = gi.soundindex("makron/voice3.wav");
	sound_taunt3 = gi.soundindex("makron/voice.wav");
	sound_hit = gi.soundindex("makron/bhit.wav");

	gi.modelindex("models/monsters/boss3/rider/tris.md2");
}

/*
 * QUAKED monster_makron (1 .5 0) (-30 -30 0) (30 30 90) Ambush Trigger_Spawn Sight
 */
void
SP_monster_makron(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	if (deathmatch->value)
	{
		G_FreeEdict(self);
		return;
	}

	MakronPrecache();

	self->movetype = MOVETYPE_STEP;
	self->solid = SOLID_BBOX;
	self->s.modelindex = gi.modelindex("models/monsters/boss3/rider/tris.md2");
	VectorSet(self->mins, -30, -30, 0);
	VectorSet(self->maxs, 30, 30, 90);

	self->health = 3000;
	self->gib_health = -2000;
	self->mass = 500;

	self->pain = makron_pain;
	self->die = makron_die;
	self->monsterinfo.stand = makron_stand;
	self->monsterinfo.walk = makron_walk;
	self->monsterinfo.run = makron_run;
	self->monsterinfo.dodge = NULL;
	self->monsterinfo.attack = makron_attack;
	self->monsterinfo.melee = NULL;
	self->monsterinfo.sight = makron_sight;
	self->monsterinfo.checkattack = Makron_CheckAttack;

	gi.linkentity(self);

	self->monsterinfo.currentmove = &makron_move_sight;
	self->monsterinfo.scale = MODEL_SCALE;

	walkmonster_start(self);
}

void
MakronSpawn(edict_t *self)
{
	vec3_t vec;
	edict_t *enemy;
	edict_t *oldenemy;

	if (!self)
	{
		return;
	}

	/* spawning can mess with enemy state so clear it temporarily */
	enemy = self->enemy;
	self->enemy = NULL;

	oldenemy = self->oldenemy;
	self->oldenemy = NULL;

	SP_monster_makron(self);
	if (self->think)
	{
		self->think(self);
	}

	/* and re-link enemy state now that he's spawned */
	if (enemy && enemy->inuse && enemy->deadflag != DEAD_DEAD)
	{
		self->enemy = enemy;
	}

	if (oldenemy && oldenemy->inuse && oldenemy->deadflag != DEAD_DEAD)
	{
		self->oldenemy = oldenemy;
	}

	if (!self->enemy)
	{
		self->enemy = self->oldenemy;
		self->oldenemy = NULL;
	}

	enemy = self->enemy;

	if (enemy)
	{
		FoundTarget(self);
		VectorCopy(self->pos1, self->monsterinfo.last_sighting);
	}

	if (enemy && visible(self, enemy))
	{
		VectorSubtract(enemy->s.origin, self->s.origin, vec);
		self->s.angles[YAW] = vectoyaw(vec);
		VectorNormalize(vec);
	}
	else
	{
		AngleVectors(self->s.angles, vec, NULL, NULL);
	}

	VectorScale(vec, 400, self->velocity);
	/* the jump frames are fixed length so best to normalize the up speed */
	self->velocity[2] = 200.0f * (sv_gravity->value / 800.0f);

	self->groundentity = NULL;
	self->s.origin[2] += 1;
	gi.linkentity(self);

	self->pain_debounce_time = level.time + 1;

	self->monsterinfo.currentmove = &makron_move_sight;
}

/*
 * Jorg is just about dead, so set up to launch Makron out
 */
void
MakronToss(edict_t *self)
{
	edict_t *ent;

	if (!self)
	{
		return;
	}

	ent = G_Spawn();
	ent->classname = "monster_makron";
	ent->nextthink = level.time + 0.8;
	ent->think = MakronSpawn;
	ent->target = self->target;
	VectorCopy(self->s.origin, ent->s.origin);
	VectorCopy(self->s.angles, ent->s.angles);
	VectorCopy(self->monsterinfo.last_sighting, ent->pos1);

	ent->enemy = self->enemy;
	ent->oldenemy = self->oldenemy;
}
