#include "../header/local.h"


/*QUAKED sound_echo (1 0 0) (-16 -16 -16) (16 16 16)
Echo any sounds that are played within "dmg_radius" radius.
"delay"      "x" where x is the delay between echo's.
"wait"       "x" where x is the sound volume decay percent per echo (eg. 0.1 = %10)
"dmg_radius" "x" where x is the max distance that affects sounds
*/

void SP_sound_echo (edict_t *self)
{
	if (!self)
	{
		return;
	}

	G_FreeEdict(self);
}

/*QUAKED load_mirrorlevel (1 0 0) (-16 -16 -16) (16 16 16)
 "target" the mapname of the mirror map to this one.
*/

void SP_load_mirrorlevel (edict_t *self)
{
	if (!self)
	{
		return;
	}

	G_FreeEdict(self);
}

#ifdef CACHE_SOUND
int	(*actual_soundindex) (char *name);

list_t *soundList;
unsigned int soundNumRejected;

typedef struct
{
  char *name;

} modelsound;

void initSoundList()
{
	soundList = gi.TagMalloc (sizeof(list_t), TAG_LEVEL);
	initializeList(soundList);
	soundNumRejected = 0;
}

int internalSoundIndex(char *name)
{
	int idx = 0;
	int numSounds = listLength(soundList);
	modelsound *sound;
	int i = 0;

	// convert name to lowercase
	for (i = 0; i < strlen(name); i++) 
		name[i] = tolower(name[i]);
	
	// do we already have this sound?
	for (i = 0; i < numSounds; i++)
	{
		sound = (modelsound *)getItem(soundList, i);
		if(strcmp(sound->name, name) == 0)
		{
			return (*actual_soundindex)(name);
		}
	}

	// ok, do we have too many sounds?
	if (numSounds >= MAX_SOUNDS-1)
	{
		soundNumRejected++;
		// ok, we cannot precache anymore
		if (printSoundRejects->value)
			gi.dprintf("%s precache rejected\n", name);
		return 0;
	}
	
	idx = (*actual_soundindex)(name);
	if (idx == 0)
		return 0;

	sound = gi.TagMalloc (sizeof(modelsound), TAG_LEVEL);
	sound->name = gi.TagMalloc (strlen(name) + 1, TAG_LEVEL);
	strcpy(sound->name, name);
	
	addTail(soundList, sound);
	//gi.dprintf("numSounds = %i\n", listLength(&soundList));

	return idx;
}

void printSoundNum()
{
	int numSounds = listLength(soundList);
	gi.dprintf("%i precached sounds\n", numSounds);
	if (printSoundRejects->value)
		gi.dprintf("%i sounds rejected\n", soundNumRejected);
}
#endif

/********************************************
	trigger_laser
*/

/*QUAKED trigger_laser (1 0 0) (-16 -16 -16) (16 16 16) TRIGGER_MULTIPLE
Laser-type trigger
"wait"       "x" where x is the delay before reactivation
"target"     target to trigger
"message"    message to center print
"delay"      delay before trigger
*/

#define TRIGGER_MULTIPLE	1
void trigger_laser_on (edict_t *self);

void trigger_laser_think (edict_t *self)
{
	vec3_t	start;
	vec3_t	end;
	trace_t	tr;
	int		count = 8;

	if (!self)
	{
		return;
	}

	self->nextthink = level.time + FRAMETIME;
	
	VectorCopy (self->s.origin, start);
	VectorMA (start, 2048, self->movedir, end);
	tr = gi.trace (start, NULL, NULL, end, self, CONTENTS_SOLID|CONTENTS_MONSTER|CONTENTS_DEADMONSTER);

	if (!tr.ent)
		return;

	// if we hit something that's not a monster or player
	if (!(tr.ent->svflags & SVF_MONSTER) && (!tr.ent->client))
	{
		if (self->spawnflags & 0x80000000)
		{
			self->spawnflags &= ~0x80000000;
			gi.WriteByte (svc_temp_entity);
			gi.WriteByte (TE_LASER_SPARKS);
			gi.WriteByte (count);
			gi.WritePosition (tr.endpos);
			gi.WriteDir (tr.plane.normal);
			gi.WriteByte (self->s.skinnum);
			gi.multicast (tr.endpos, MULTICAST_PVS);
		}
	}
	else
	{
		// trigger
		G_UseTargets (self, tr.ent);

		if (self->spawnflags & TRIGGER_MULTIPLE)
		{
			// hide for a time
			self->svflags |= SVF_NOCLIENT;
			self->nextthink = level.time + self->wait;
			self->think = trigger_laser_on;
		}
		else
		{
			// remove self
			G_FreeEdict(self);
		}
	}

	VectorCopy (tr.endpos, self->s.old_origin);
}

void trigger_laser_on (edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->svflags &= ~SVF_NOCLIENT;
	self->think = trigger_laser_think;
	trigger_laser_think(self);
}

void SP_trigger_laser(edict_t *self)
{
	if (!self)
	{
		return;
	}

	// if no target
	if (!self->target)
	{
		gi.dprintf("trigger_laser without target\n");
		G_FreeEdict(self);
		return;
	}

	// if no wait, set default
	if (!self->wait)
	{
		self->wait = 4;
	}

	G_SetMovedir (self->s.angles, self->movedir);
	self->s.skinnum = 0xf2f2f0f0;	// colour
	self->s.frame = 2;				// diameter
	self->movetype = MOVETYPE_NONE;
	self->solid = SOLID_NOT;
	self->s.renderfx |= RF_BEAM|RF_TRANSLUCENT;
	self->s.modelindex = 1;
	self->spawnflags |= 0x80000000;
	self->think = trigger_laser_on;
	self->nextthink = level.time + 0.1;
	self->svflags |= SVF_NOCLIENT;
	gi.linkentity (self);
}

/*QUAKED misc_commdish (0 .5 .8) (-16 -16 0) (16 16 40)
*/
void Anim_CommDish(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->s.frame++;

	if(self->s.frame >= 98)
	{
		self->s.frame = 98;
	}
	else
	{
		self->nextthink = level.time + FRAMETIME;
	}
}

void Use_CommDish (edict_t *ent, edict_t *other, edict_t *activator)
{
	if (!ent)
	{
		return;
	}

	ent->nextthink = level.time + FRAMETIME;
	ent->think = Anim_CommDish;
	ent->use = NULL;
	gi.sound (ent, CHAN_AUTO, gi.soundindex ("misc/commdish.wav"), 1, ATTN_NORM, 0);
}

void SP_misc_commdish (edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (deathmatch->value)
	{	// auto-remove for deathmatch
		G_FreeEdict (self);
		return;
	}

	self->solid = SOLID_BBOX;
	self->movetype = MOVETYPE_STEP;

	self->model = "models/objects/satdish/tris.md2";
	self->s.modelindex = gi.modelindex (self->model);
	VectorSet (self->mins, -100, -100, 0);
	VectorSet (self->maxs, 100, 100, 275);

	self->monsterinfo.aiflags = AI_NOSTEP;

	self->think = M_droptofloor;
	self->nextthink = level.time + 2 * FRAMETIME;
	self->use = Use_CommDish;

	gi.linkentity (self);
}

