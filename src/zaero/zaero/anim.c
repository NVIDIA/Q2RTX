#if defined(_DEBUG) && defined(_Z_TESTMODE)

#include "../header/local.h"

/*=========================================================================
   Local functions.
  =========================================================================*/
anim_data_t *anim_player_create(edict_t *monster);
void anim_player_think(edict_t *anim_player);

void update_directions(anim_data_t *data);
void update_frame_buffer(anim_data_t *data);
void calculate_buffer_actuals(anim_data_t *data);

/*=========================================================================
   Local variables.
  =========================================================================*/
#define MAX_ANIMATIONS 10

anim_data_t *animations[MAX_ANIMATIONS]; //record of all animations
int animations_count = 0;

/**************************************************************************
   Misc routines.
  **************************************************************************/
qboolean cut_up_string(char **str, char **clipping)
{
	char *end;

	while (**str == ' ')
		(*str)++;

	if(**str == '\0')
	{
		*clipping = NULL;
		return false;
	}

	end = (*str) + 1;
	while (*end != '\0')
	{
		if(*end == ' ')
		{
			*end = '\0';
			end++;
			break;
		}
		end++;
	}

	*clipping = gi.TagMalloc(strlen(*str) + 1, TAG_LEVEL);
	strcpy(*clipping, *str);

	*str = end;

	return true;
}

edict_t *find_targetname(char *targetname)
{
	int i;

	for(i=0;i<globals.num_edicts;i++)
	{
		if(!g_edicts[i].targetname)
			continue;

		if(Q_stricmp(g_edicts[i].targetname, targetname) == 0)
			return g_edicts + i;
	}

	gi.dprintf("name <%s> not found\n", targetname);
	return NULL;
}

static edict_t *the_client = NULL;

edict_t *find_client(void)
{
	int i;

	if(the_client)
		return the_client;

	if(!maxclients->value)
		return NULL;

	for(i=1;i<globals.num_edicts;i++)
	{
		if(!g_edicts[i].inuse)
			continue;

		if(g_edicts[i].client)
		{
			the_client = g_edicts + i;
			break;
		}
	}

	return the_client;
}

/**************************************************************************
   Exported aim correction routine.
  **************************************************************************/
anim_data_t *find_monster_animator(edict_t *monster)
{
	anim_data_t *anim;
	int i;

	if (!monster)
	{
		return NULL;
	}

	for(i=0;i<animations_count;i++)
	{
		anim = animations[i];

		if(anim->monster == monster)
			return anim;
	}

	return NULL;
}

qboolean anim_player_correct_aim(edict_t *self, vec3_t aim)
{
	anim_data_t *anim;

	if (!self)
	{
		return false;
	}

	if(self->extra_data != animations)
		return false;

	anim = find_monster_animator(self);
	if(!anim)
		return false;

	VectorCopy(anim->v_aim, aim);

	return true;
}


/**************************************************************************
   Methods and events for creating animation player edict and for coaxing
  monsters to behave correctly.
  **************************************************************************/

/*=========================================================================
   Replacement black monster events.
  =========================================================================*/
void no_pain(edict_t *self, edict_t *other, float kick, int damage)
{
	return;
}

void no_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	return;
}

void no_happen(edict_t *self)
{
	return;
}

/*=========================================================================
   Construction methods.
  =========================================================================*/
anim_data_t *anim_data_create(edict_t *monster)
{
	anim_data_t *data;

	if (!monster)
	{
		return NULL;
	}

	if(!z_frame_get_sequence(monster->classname))
		return NULL;

	data = gi.TagMalloc(sizeof(anim_data_t), TAG_LEVEL);

	data->monster = monster;
	monster->extra_data = animations;

	data->monster_frames[0].aifunc = NULL;
	data->monster_frames[0].dist = 0.0;
	data->monster_frames[0].thinkfunc = NULL;

	data->monster_move.firstframe = 0;
	data->monster_move.lastframe = 0;
	data->monster_move.frame = data->monster_frames;
	data->monster_move.endfunc = NULL;

	data->monster_sequences = z_frame_get_sequence(monster->classname);

	data->current_sequence = 0;
	data->current_frame = 0;
	data->last_dist = 0.0;
	data->moving_forward = true;

	data->paused = true;
	data->stationary = true;
	data->frame_events = false;
	data->active = true;

	data->facing = DIR_FIXED;
	data->aim = DIR_FIXED;

	return data;
}

anim_data_t *anim_player_create(edict_t *monster)
{
	anim_data_t *data;
	edict_t *anim_player;

	if (!monster)
	{
		return NULL;
	}

	data = anim_data_create(monster);
	if(!data)
		return data;

	monster->monsterinfo.currentmove = &(data->monster_move);
	monster->pain = no_pain;
	monster->die = no_die;
	monster->monsterinfo.stand = no_happen;
	monster->monsterinfo.walk = no_happen;
	monster->monsterinfo.run = no_happen;
	monster->monsterinfo.dodge = NULL;
	monster->monsterinfo.attack = no_happen;
	monster->monsterinfo.melee = NULL;
	monster->monsterinfo.sight = NULL;
	monster->monsterinfo.idle = NULL;

	anim_player = G_Spawn();
	anim_player->extra_data = data;
	anim_player->think = anim_player_think;
	anim_player->nextthink = level.time + 0.1;

	return data;
}

/*=========================================================================
   Animation player behaviour routines.
  =========================================================================*/
void advance_anim_frame(anim_data_t *anim, int count)
{
	mmove_t *seq;

	if (!anim)
	{
		return;
	}

	anim->moving_forward = (count < 0)? false : true;

	anim->current_frame += count;
	if(anim->current_sequence)
	{
		seq = anim->monster_sequences[anim->current_sequence - 1];
		anim->current_frame += seq->lastframe - seq->firstframe + 1;
		anim->current_frame %= seq->lastframe - seq->firstframe + 1;
	}
}

void anim_player_think(edict_t *anim_player)
{
	anim_data_t *data;

	if (!anim_player)
	{
		return;
	}

	data = (anim_data_t *)anim_player->extra_data;

	update_directions(data);

	if(!data->paused)
	advance_anim_frame(data, 1);
	calculate_buffer_actuals(data);
	update_frame_buffer(data);

	anim_player->nextthink = level.time + 0.1;
}

/**************************************************************************
   Rountines to make monster follow the frame buffer, ideal_yaw and
  ideal_aim.
  **************************************************************************/

void update_directions(anim_data_t *data)
{
	edict_t *client = find_client();
	vec3_t ang;
	vec3_t point;

	if (!data)
	{
		return;
	}

	switch(data->facing)
	{
		case DIR_AT_CLIENT:
			VectorSubtract(client->s.origin, data->monster->s.origin, point);
			VectorNormalize(point);
			vectoangles(point, ang);
			data->monster->ideal_yaw = ang[YAW];
			break;
		case DIR_PARA_CLIENT:
			data->monster->ideal_yaw = client->client->v_angle[YAW];
			break;
	}

	switch(data->aim)
	{
		case DIR_AT_CLIENT:
			VectorSubtract(client->s.origin, data->monster->s.origin, point);
			VectorNormalize(point);
			vectoangles(point, data->v_aim);
			break;
		case DIR_PARA_CLIENT:
			AngleVectors(client->client->v_angle, data->v_aim, NULL, NULL);
			break;
	}
}

/*=========================================================================
   Animator ai routine to move and track yaw.
  =========================================================================*/
void ai_animator(edict_t *self, float dist)
{
	if (!self)
	{
		return;
	}

	if(dist != 0.0)
		M_walkmove (self, self->s.angles[YAW], dist);

	M_ChangeYaw(self);
}

/*=========================================================================
   Routine to load the current actual_* data into the frame buffer.
  =========================================================================*/
void update_frame_buffer(anim_data_t *data)
{
	if (!data)
	{
		return;
	}

	// defaults
	data->monster_frames[0].aifunc = NULL;
	data->monster_frames[0].dist = 0.0;
	data->monster_frames[0].thinkfunc = NULL;

	data->monster_move.firstframe = data->actual_frame;
	data->monster_move.lastframe = data->actual_frame;

	if(data->last_actual_frame != data->actual_frame)
	{
		if(!data->stationary)
		{
			if(data->moving_forward)
			{
				data->monster_frames[0].dist =
				data->actual_sequence->frame[data->actual_sequence_idx].dist;
				data->last_dist = data->monster_frames[0].dist;
			}
			else
			{
				data->monster_frames[0].dist = -data->last_dist;
				data->last_dist = data->actual_sequence->frame[data->actual_sequence_idx].dist;
			}
			data->monster_frames[0].aifunc = ai_animator;
		}

		if(data->frame_events)
			data->monster_frames[0].thinkfunc =
				data->actual_sequence->frame[data->actual_sequence_idx].thinkfunc;
	}

	data->last_actual_frame = data->actual_frame;
}

/**************************************************************************
   Routines to calculate actual frames to show.
  **************************************************************************/
int get_total_frame_count(anim_data_t *data)
{
	int total_frames = 0;
	mmove_t **seq;

	if (!data)
	{
		return 0;
	}

	seq = data->monster_sequences;
	while(*seq)
	{
		total_frames += (*seq)->lastframe - (*seq)->firstframe + 1;
		seq++;
	}

	return total_frames;
}

/*=========================================================================
   Frame selection routine. Note that sequence 0 has the special meaning of
  cycling through all sequences on after another.
  This routine sets the actual_frame and actual_sequence from current_frame
  and current_sequence.
  Assumptions:
      - current_sequence = 0, means all frames with current_frame looping
      - current_sequence > 0, a valid sequence and valid current_frame
  =========================================================================*/
void calculate_buffer_actuals(anim_data_t *data)
{
	int seq_frames, idx;

	if (!data)
	{
		return;
	}

	if(data->current_sequence)
	{
		data->actual_sequence = data->monster_sequences[data->current_sequence - 1];
		data->actual_sequence_idx = data->current_frame;
	}
	else
	{
		data->current_frame %= get_total_frame_count(data);
		data->actual_sequence_idx = data->current_frame;

		idx = 0;
		data->actual_sequence = data->monster_sequences[idx];
		seq_frames = data->actual_sequence->lastframe -
		data->actual_sequence->firstframe + 1;
		while(data->actual_sequence_idx + 1 > seq_frames)
		{
			idx++;
			data->actual_sequence_idx -= seq_frames;
			data->actual_sequence = data->monster_sequences[idx];
			seq_frames = data->actual_sequence->lastframe -
			data->actual_sequence->firstframe + 1;
		}
	}

	data->actual_frame = data->actual_sequence->firstframe +
	data->actual_sequence_idx;
}

/**************************************************************************
   Console command routines.
  **************************************************************************/

void anim_player_report(char *targetname, char *description, qboolean on)
{
	gi.dprintf("%s %s ", targetname, description);
	if(on)
		gi.dprintf("ON\n");
	else
		gi.dprintf("OFF\n");
}

/*=========================================================================
   Advances count frames in the current sequence
  =========================================================================*/
void anim_player_advance_frame(int count)
{
	anim_data_t *anim;
	int i;

	for(i=0;i<animations_count;i++)
	{
		anim = animations[i];
		if(!anim->active)
		continue;

		advance_anim_frame(anim, count);
	}
}

/*=========================================================================
   Advances count sequences.
  =========================================================================*/
void anim_player_advance_sequence(int count)
{
	anim_data_t *anim;
	int i, tcount;

	for(i=0;i<animations_count;i++)
	{
		anim = animations[i];
		if(!anim->active)
		continue;

		tcount = count;

		while(tcount > 0)
		{
			tcount--;
			(anim->current_sequence)++;
			if(!anim->monster_sequences[anim->current_sequence - 1])
			anim->current_sequence = 0;
		}

		while(tcount < 0)
		{
			(anim->current_sequence)--;
			if(anim->current_sequence < 0)
			{
				anim->current_sequence = 0;
				while(anim->monster_sequences[anim->current_sequence])
				(anim->current_sequence)++;
			}
			tcount++;
		}
		anim->current_frame = 0;
	}
}

/*=========================================================================
   Set facing flag.
  =========================================================================*/
void anim_player_set_facing(anim_dir_t facing)
{
	anim_data_t *anim;
	int i;

	if (!facing)
	{
		return;
	}

	for(i=0;i<animations_count;i++)
	{
		anim = animations[i];
		if(!anim->active)
			continue;

		anim->facing = facing;
	}
}

/*=========================================================================
   Set aim flag.
  =========================================================================*/
void anim_player_set_aim(anim_dir_t aim)
{
	anim_data_t *anim;
	int i;

	if (!aim)
	{
		return;
	}

	for(i=0;i<animations_count;i++)
	{
		anim = animations[i];
		if(!anim->active)
			continue;

		anim->aim = aim;
	}
}

/*=========================================================================
   Toggles current monster event playing.
  =========================================================================*/
void anim_player_events(void)
{
	anim_data_t *anim;
	int i;

	for(i=0;i<animations_count;i++)
	{
		anim = animations[i];
		if(!anim->active)
			continue;

		anim->frame_events = !anim->frame_events;

		anim_player_report(anim->monster->targetname, "frame events",
			anim->frame_events);
	}
}

/*=========================================================================
   Toggles current monster stationary.
  =========================================================================*/
void anim_player_still(void)
{
	anim_data_t *anim;
	int i;

	for(i=0;i<animations_count;i++)
	{
		anim = animations[i];
		if(!anim->active)
			continue;

		anim->stationary = !anim->stationary;

		anim_player_report(anim->monster->targetname, "stationary",
		anim->stationary);
	}
}

/*=========================================================================
   Toggles current monster pause.
  =========================================================================*/
void anim_player_pause(void)
{
	anim_data_t *anim;
	int i;

	for(i=0;i<animations_count;i++)
	{
		anim = animations[i];
		if(!anim->active)
			continue;

		anim->paused = !anim->paused;

		anim_player_report(anim->monster->targetname, "pause",
		anim->paused);
	}
}

/*=========================================================================
   Captures a monster or sets the monster current if already captured.
  =========================================================================*/
qboolean anim_player_capture(char *targetname)
{
	edict_t *ent;
	int i;
	anim_data_t *anim;

	//make sure we don't already have this one
	for(i=0;i<animations_count;i++)
	{
		if(Q_stricmp(animations[i]->monster->targetname, targetname) == 0)
		{
			gi.dprintf("Target <%s> already captured\n", targetname);
			return false;
		}
	}

	//make sure we have room to hold the animation reference.
	if(animations_count == MAX_ANIMATIONS)
	{
		gi.dprintf("Maximum of %d animations already used\n", MAX_ANIMATIONS);
		return false;
	}

	ent = find_targetname(targetname);

	if(!ent)
		return false;

	anim = anim_player_create(ent);

	if(anim)
	{
		animations[animations_count++] = anim;
		gi.dprintf("Target <%s> captured\n", targetname);
	}
	else
		gi.dprintf("Target <%s> NOT captured\n", targetname);

	return true;
}

/*=========================================================================
   Toggles current monster stationary.
  =========================================================================*/
void anim_player_set_active(char *targetname, qboolean active)
{
	anim_data_t *anim;
	int i;

	for(i=0;i<animations_count;i++)
	{
		anim = animations[i];

		if((Q_stricmp(anim->monster->targetname, targetname) == 0) ||
			(Q_stricmp("all", targetname) == 0))
			anim->active = active;
	}
}

/*=========================================================================
   Animation player command entry point.
   Called in g_cmds.c
  =========================================================================*/
void anim_player_cmd(edict_t *ent)
{
	char *args, *arg1=NULL, *arg2=NULL;

	if (!ent)
	{
		return;
	}

	args = gi.args();

	if(!cut_up_string(&args, &arg1))
		return;

	cut_up_string(&args, &arg2);

	// string switch
	if(Q_stricmp (arg1, "capture") == 0)
		anim_player_capture(arg2);
	else if(Q_stricmp (arg1, "activate") == 0)
		anim_player_set_active(arg2, true);
	else if(Q_stricmp (arg1, "deactivate") == 0)
		anim_player_set_active(arg2, false);
	else if(Q_stricmp (arg1, "pause") == 0)
		anim_player_pause();
	else if(Q_stricmp (arg1, "still") == 0)
		anim_player_still();
	else if(Q_stricmp (arg1, "events") == 0)
		anim_player_events();
	else if(Q_stricmp (arg1, "s_next") == 0)
		anim_player_advance_sequence(1);
	else if(Q_stricmp (arg1, "s_prior") == 0)
		anim_player_advance_sequence(-1);
	else if(Q_stricmp (arg1, "s_reset") == 0)
		anim_player_advance_sequence(0);
	else if(Q_stricmp (arg1, "f_next") == 0)
		anim_player_advance_frame(1);
	else if(Q_stricmp (arg1, "f_prior") == 0)
		anim_player_advance_frame(-1);
	else if(Q_stricmp (arg1, "face_client") == 0)
		anim_player_set_facing(DIR_AT_CLIENT);
	else if(Q_stricmp (arg1, "face_para") == 0)
		anim_player_set_facing(DIR_PARA_CLIENT);
	else if(Q_stricmp (arg1, "face_fixed") == 0)
		anim_player_set_facing(DIR_FIXED);
	else if(Q_stricmp (arg1, "aim_client") == 0)
		anim_player_set_aim(DIR_AT_CLIENT);
	else if(Q_stricmp (arg1, "aim_para") == 0)
		anim_player_set_aim(DIR_PARA_CLIENT);
	else if(Q_stricmp (arg1, "aim_fixed") == 0)
		anim_player_set_aim(DIR_FIXED);
	else
		gi.dprintf("unknown anim command <%s>\n", arg1);

	//clean up
	gi.TagFree(arg1);
	if(arg2)
		gi.TagFree(arg2);
}

#endif
