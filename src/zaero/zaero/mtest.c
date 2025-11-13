#if defined(_DEBUG) && defined(_Z_TESTMODE)

#include "../header/local.h"

void Weapon_Generic (edict_t *ent, 
					 int FRAME_ACTIVATE_LAST, 
					 int FRAME_FIRE_LAST, 
					 int FRAME_IDLE_LAST, 
					 int FRAME_DEACTIVATE_LAST, 
					 int *pause_frames, 
					 int *fire_frames, 
					 void (*fire)(edict_t *ent));
void P_ProjectSource (edict_t *ent, vec3_t distance, vec3_t forward, vec3_t right, vec3_t result);

static char testItem_className[256];
static char testItem_gModel[256];
static char testItem_icon[256];
static char testItem_name[256];
static char testItem_aminationFramesStr[4096];
static int	testItem_aminationFrames[100];
static vec3_t testItem_Size[2];

gitem_t *testItem;
edict_t *testItemDroped = NULL;
int animUpto;
qboolean testitemOriginMove = false;
float animSpeed = 1.0;

float lineSize = 100.0f;

void Weapon_LineDraw_Fire (edict_t *ent);

void Weapon_LineDraw_Think (edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	Weapon_LineDraw_Fire (ent->owner);
	ent->owner->client->ps.gunframe--;
}

void Weapon_LineDraw_Fire (edict_t *ent)
{
	edict_t *beam;
	vec3_t	start;
	vec3_t	forward, right;
	vec3_t	offset;

	if (!ent)
	{
		return;
	}

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorSet(offset, 0, 7,  ent->viewheight - 8);
	P_ProjectSource (ent, offset, forward, right, start);

	if(!ent->client->lineDraw)
	{
		beam = ent->client->lineDraw = G_Spawn();

		beam->classname = "DrawLine";

		beam->flags |= FL_DONTSETOLDORIGIN;

		beam->owner = ent;

		beam->movetype = MOVETYPE_NONE;
		beam->solid = SOLID_NOT;
		beam->s.renderfx |= RF_BEAM|RF_TRANSLUCENT;
		beam->s.modelindex = 1;			// must be non-zero

		VectorSet (beam->mins, -8, -8, -8);
		VectorSet (beam->maxs, 8, 8, 8);

		beam->s.frame = 2;
		beam->s.skinnum = 0xf3f3f1f1;

		beam->think = Weapon_LineDraw_Think;
		beam->nextthink = level.time + (FRAMETIME * 2);
	}
	else
	{
		beam = ent->client->lineDraw;
	}

	VectorCopy (start, beam->s.origin);
	VectorMA (start, lineSize, forward, beam->s.old_origin);

	gi.linkentity (beam);
	ent->client->ps.gunframe++;
}

void Weapon_LineDraw (edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	static int	pause_frames[]	= {19, 32, 0};
	static int	fire_frames[]	= {5, 0};

	Weapon_Generic (ent, 4, 8, 52, 55, pause_frames, fire_frames, Weapon_LineDraw_Fire);
}

static char testWeap_className[256];
static char testWeap_gModel[256];
static char testWeap_vModel[256];
static char testWeap_icon[256];
static char testWeap_name[256];
static char testWeap_aminationFrames[512];
static char testWeap_idleFrames[512];
static char testWeap_fireFrames[512];
static int  testWeap_FRAME_ACTIVATE_LAST;
static int  testWeap_FRAME_FIRE_LAST;
static int  testWeap_FRAME_IDLE_LAST;
static int  testWeap_FRAME_DEACTIVATE_LAST;
static int	testWeap_pause_frames[20];
static int	testWeap_fire_frames[20];

gitem_t *testWeapon;

void convertToNumbers(char *frames, int *arrayFrames)
{
  int num = 0;
  char *bp, *sp;

  sp = bp = frames;

  while(1)
  {
    while(*bp != ',' && *bp != (char)NULL)
    {
      bp++;
    }

    arrayFrames[num] = atoi(sp);

    num++;

    if(*bp == (char)NULL)
    {
      break;
    }

    sp = bp + 1;

    while(*sp == ' ')
    {
      sp++;
    }

    bp = sp;
  }

  arrayFrames[num] = 0;
}

void convertToVector(char *vecStr, vec3_t *size)
{
  int num = 0;
  char *bp, *sp;

  sp = bp = vecStr;

  while(1)
  {
    while(*bp != ',' && *bp != (char)NULL)
    {
      bp++;
    }

    (*size)[num] = atof(sp);

    num++;

    if(num > 2)
    {
      break;
    }

    if(*bp == (char)NULL)
    {
      break;
    }

    sp = bp + 1;

    while(*sp == ' ')
    {
      sp++;
    }

    bp = sp;
  }
}

void InitTestWeapon(void)
{
  FILE *wCfgFile;
  char fname[256];

  testWeapon = FindItemByClassname ("weapon_test");
  if(!testWeapon)
  {
    return;
  }

  strcpy(fname, gamedir->string);
  strcat(fname, "/testweapon.cfg");

  wCfgFile = fopen(fname, "rt");
  if(!wCfgFile)
  {
    return;
  }

  if(!fgets(testWeap_className, 256, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }

  testWeap_className[strlen(testWeap_className) - 1] = 0;

  if(!fgets(testWeap_gModel, 256, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }

  testWeap_gModel[strlen(testWeap_gModel) - 1] = 0;

  if(!fgets(testWeap_vModel, 256, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }

  testWeap_vModel[strlen(testWeap_vModel) - 1] = 0;

  if(!fgets(testWeap_icon, 256, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }

  testWeap_icon[strlen(testWeap_icon) - 1] = 0;

  if(!fgets(testWeap_name, 256, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }

  testWeap_name[strlen(testWeap_name) - 1] = 0;

  if(!fgets(testWeap_aminationFrames, 512, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }

  if(!fgets(testWeap_idleFrames, 512, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }

  if(!fgets(testWeap_fireFrames, 512, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }

  fclose(wCfgFile);


  sscanf(testWeap_aminationFrames, "%d,%d,%d,%d", &testWeap_FRAME_ACTIVATE_LAST, &testWeap_FRAME_FIRE_LAST, &testWeap_FRAME_IDLE_LAST, &testWeap_FRAME_DEACTIVATE_LAST);

  convertToNumbers(testWeap_idleFrames, testWeap_pause_frames);
  convertToNumbers(testWeap_fireFrames, testWeap_fire_frames);

  testWeapon->classname = testWeap_className;
  testWeapon->world_model = testWeap_gModel;
  testWeapon->view_model = testWeap_vModel;
  testWeapon->icon = testWeap_icon;
  testWeapon->pickup_name = testWeap_name;
}

void Weapon_Test_Fire (edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/blastf1a.wav"), 1, ATTN_NORM, 0);
	ent->client->ps.gunframe++;
}

void Weapon_Test (edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	Weapon_Generic (ent, testWeap_FRAME_ACTIVATE_LAST,
		testWeap_FRAME_FIRE_LAST, testWeap_FRAME_IDLE_LAST,
		testWeap_FRAME_DEACTIVATE_LAST,
		testWeap_pause_frames, testWeap_fire_frames, Weapon_Test_Fire);
}

void testitem_think (edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if(testItem_aminationFrames[animUpto])
	{
		ent->s.frame++;
		if(ent->s.frame >= testItem_aminationFrames[animUpto])
		{
			if(animUpto)
			{
				ent->s.frame = testItem_aminationFrames[animUpto - 1];
			}
			else
			{
				ent->s.frame = 0;
			}
		}
	}

	ent->nextthink = level.time + (FRAMETIME * animSpeed);
}

void Cmd_TestItem (edict_t *ent)
{
	char *cmd;

	if (!ent)
	{
		return;
	}

	cmd = gi.argv(1);

	if (Q_stricmp (cmd, "animnext") == 0)
	{
		if(testItem_aminationFrames[animUpto] && testItem_aminationFrames[animUpto + 1])
		{
			animUpto++;

			gi.cprintf (ent, PRINT_HIGH, "Animation %d set\n", animUpto);
		}
	}
	else if (Q_stricmp (cmd, "animprev") == 0)
	{
		if(animUpto && testItem_aminationFrames[animUpto - 1])
		{
			animUpto--;

			gi.cprintf (ent, PRINT_HIGH, "Animation %d set\n", animUpto);
		}
	}
	else if (Q_stricmp (cmd, "animspeed") == 0)
	{
		float as = atof(gi.argv(2));

		if(as < 1.0)
		{
			gi.cprintf (ent, PRINT_HIGH, "AnimSpeed must be greater than or equal to 1\n");
			return;
		}

		animSpeed = as;
	}
	else if (Q_stricmp (cmd, "movestart") == 0)
	{
		if(testItemDroped)
		{
			testitemOriginMove = true;
			gi.cprintf (ent, PRINT_HIGH, "testitem move start\n");
		}
	}
	else if (Q_stricmp (cmd, "moveend") == 0)
	{
		if(testItemDroped)
		{
			testitemOriginMove = false;
			gi.cprintf (ent, PRINT_HIGH, "testitem move end\n");
		}
	}
	else if (Q_stricmp (cmd, "rotatestart") == 0)
	{
		if(testItemDroped)
		{
			testItemDroped->s.effects = EF_ROTATE;
			gi.cprintf (ent, PRINT_HIGH, "Rotate On\n");
		}
	}
	else if (Q_stricmp (cmd, "rotateend") == 0)
	{
		if(testItemDroped)
		{
			testItemDroped->s.effects = 0;
			gi.cprintf (ent, PRINT_HIGH, "Rotate Off\n");
		}
	}
	else
	{
		gi.cprintf (ent, PRINT_HIGH, "Bad testitem command\n");
	}
}

void InitTestItem(void)
{
  FILE *wCfgFile;
  char fname[256];

  testItem = FindItemByClassname ("item_test");
  if(!testItem)
  {
    return;
  }

  strcpy(fname, gamedir->string);
  strcat(fname, "/testitem.cfg");

  wCfgFile = fopen(fname, "rt");
  if(!wCfgFile)
  {
    return;
  }

  if(!fgets(testItem_className, 256, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }

  testItem_className[strlen(testItem_className) - 1] = 0;

  if(!fgets(testItem_gModel, 256, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }

  testItem_gModel[strlen(testItem_gModel) - 1] = 0;

  if(!fgets(testItem_icon, 256, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }

  testItem_icon[strlen(testItem_icon) - 1] = 0;

  if(!fgets(testItem_name, 256, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }

  testItem_name[strlen(testItem_name) - 1] = 0;

  if(!fgets(testItem_aminationFramesStr, 4096, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }

  convertToNumbers(testItem_aminationFramesStr, testItem_aminationFrames);


  if(!fgets(testItem_aminationFramesStr, 4096, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }
  
  convertToVector(testItem_aminationFramesStr, &(testItem_Size[0]));

  if(!fgets(testItem_aminationFramesStr, 4096, wCfgFile))
  {
    fclose(wCfgFile);
    return;
  }
  
  convertToVector(testItem_aminationFramesStr, &(testItem_Size[1]));

  testItem->classname = testItem_className;
  testItem->world_model = testItem_gModel;
  testItem->view_model = testItem_gModel;
  testItem->icon = testItem_icon;
  testItem->pickup_name = testItem_name;
}

qboolean Pickup_TestItem (edict_t *ent, edict_t *other)
{
	if (!ent)
	{
		return false;
	}

	other->client->pers.inventory[ITEM_INDEX(ent->item)]++;

	return true;
}

void drop_temp_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf);

void Drop_TestItem (edict_t *ent, gitem_t *item)
{
	vec3_t	forward, right;
	vec3_t	offset;

	if (!ent || !item)
	{
		return;
	}

	testitemOriginMove = false;

	testItemDroped = G_Spawn();

	testItemDroped->classname = item->classname;
	testItemDroped->item = item;
	testItemDroped->spawnflags = DROPPED_ITEM;
	testItemDroped->s.effects = item->world_model_flags;
	testItemDroped->s.renderfx = RF_GLOW;
	VectorCopy(testItem_Size[0], testItemDroped->mins);
	VectorCopy(testItem_Size[1], testItemDroped->maxs);
	gi.setmodel (testItemDroped, testItemDroped->item->world_model);
	testItemDroped->s.skinnum = 0;
	testItemDroped->solid = SOLID_TRIGGER;
	testItemDroped->movetype = MOVETYPE_TOSS;  
	testItemDroped->touch = drop_temp_touch;
	testItemDroped->owner = ent;

	if (ent->client)
	{
		trace_t	trace;

		AngleVectors (ent->client->v_angle, forward, right, NULL);
		VectorSet(offset, 24, 0, -16);
		G_ProjectSource (ent->s.origin, offset, forward, right, testItemDroped->s.origin);
		trace = gi.trace (ent->s.origin, testItemDroped->mins, testItemDroped->maxs,
		testItemDroped->s.origin, ent, CONTENTS_SOLID);
		VectorCopy (trace.endpos, testItemDroped->s.origin);
	}
	else
	{
		AngleVectors (ent->s.angles, forward, right, NULL);
		VectorCopy (ent->s.origin, testItemDroped->s.origin);
	}

	VectorScale (forward, 100, testItemDroped->velocity);
	testItemDroped->velocity[2] = 300;

	testItemDroped->think = testitem_think;
	testItemDroped->nextthink = level.time + 1;

	gi.linkentity (testItemDroped);

	ent->client->pers.inventory[ITEM_INDEX(item)]--;
	ValidateSelectedItem (ent);
}

#endif

