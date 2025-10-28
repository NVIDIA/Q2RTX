/*
	Zaero Camera
*/

#include "../header/local.h"

void zCam_SetLocalCopy(struct edict_s *player, char *s);

void zCam_TrackEntity(struct edict_s *player, struct edict_s *track, qboolean playerVisiable, qboolean playerOffset)
{
	if(player->client == NULL)
		return;  // not a true player
	
	player->client->zCameraTrack = track;
	
	// set the player view stuff...
	player->movetype = MOVETYPE_FREEZE;
	player->client->ps.gunindex = 0;
	player->client->ps.fov = 90;

	VectorSet(player->client->zCameraOffset, 0, 0, 0);
	
	// if invisible, turn off model, etc
	if(playerVisiable)
	{
		edict_t *e = NULL;
		player->client->zCameraLocalEntity = e = G_Spawn();
		e->classname = "VisorCopy";
		e->owner = player;
		e->movetype = MOVETYPE_NONE;
		e->solid = SOLID_BBOX;
		e->s.skinnum = player->s.skinnum;
		e->s.modelindex = player->s.modelindex;
		e->s.modelindex2 = player->s.modelindex2;
		VectorCopy(player->mins, e->mins);
		VectorCopy(player->maxs, e->maxs);
		VectorCopy (player->s.origin, e->s.origin);
		VectorCopy (player->s.angles, e->s.angles);
		VectorCopy (player->s.old_origin, e->s.old_origin);
		e->s.frame = player->s.frame;
		e->s.effects = player->s.effects;
		player->svflags |= SVF_NOCLIENT; // so that no one can see our real model

		gi.linkentity(e);
	}
	else
	{
		player->client->zCameraLocalEntity = NULL;
		// to do
	}
}

void zCam_Stop(struct edict_s *player)
{
	if(player->client == NULL)
	{
		return;  // not a true player
	}
  
	player->client->zCameraTrack = NULL;

	// set the player view stuff...
	player->movetype = MOVETYPE_WALK;
	player->client->ps.gunindex = gi.modelindex(player->client->pers.weapon->view_model);

	// if invisible, turn on model, etc
	if(player->client->zCameraLocalEntity)
	{
		G_FreeEdict(player->client->zCameraLocalEntity);
		player->client->zCameraLocalEntity = NULL;
		player->svflags &= ~SVF_NOCLIENT;
	}
	else
	{
		// todo
	}
}

char *getSkinModel(char *s, char *buffer)
{
  char *cp;


  strcpy(buffer, "players/");
  cp = buffer + strlen(buffer);

  while(*s && *s != '/')
  {
    *cp = *s;
    cp++;
    s++;
  }

  strcpy(cp, "/tris.md2");

  return buffer;
}

char *getSkinName(char *s, char *buffer)
{
  strcpy(buffer, "players/");
  strcat(buffer, s);
  strcat(buffer, ".pcx");

  return buffer;
}

void zCam_SetLocalCopy(struct edict_s *player, char *s)
{
  if(player->client->zCameraLocalEntity)
  {
    player->client->zCameraLocalEntity->s.modelindex = gi.modelindex("models/objects/gibs/head2/tris.md2");
  }
}
