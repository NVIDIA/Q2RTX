
#include "header/local.h"
#include "header/bot.h"

game_locals_t	game;
level_locals_t	level;
game_import_t	gi;
game_export_t	globals;
spawn_temp_t	st;

int	sm_meat_index;
int	snd_fry;
int meansOfDeath;

edict_t		*g_edicts;

cvar_t	*deathmatch;
cvar_t	*coop;
cvar_t	*dmflags;
cvar_t	*skill;
cvar_t	*fraglimit;
cvar_t	*timelimit;

cvar_t	*filterban;

//ZOID
cvar_t	*capturelimit;
//ZOID
cvar_t	*password;
cvar_t	*spectator_password;
cvar_t	*maxclients;
cvar_t	*maxspectators;
cvar_t	*maxentities;
cvar_t	*g_select_empty;
cvar_t	*dedicated;

cvar_t	*sv_maxvelocity;
cvar_t	*sv_gravity;

cvar_t	*sv_rollspeed;
cvar_t	*sv_rollangle;
cvar_t	*gun_x;
cvar_t	*gun_y;
cvar_t	*gun_z;

cvar_t	*run_pitch;
cvar_t	*run_roll;
cvar_t	*bob_up;
cvar_t	*bob_pitch;
cvar_t	*bob_roll;

cvar_t	*sv_cheats;

cvar_t  *aimfix;

//ponpoko
cvar_t	*gamepath;
cvar_t	*chedit;
cvar_t	*vwep;
cvar_t	*maplist;
cvar_t	*botlist;
cvar_t	*autospawn;
cvar_t	*zigmode;
float	spawncycle;
float	ctfjob_update;
//ponpoko

void SpawnEntities (char *mapname, char *entities, char *spawnpoint);
void ClientThink (edict_t *ent, usercmd_t *cmd);
qboolean ClientConnect (edict_t *ent, char *userinfo, qboolean loadgame);
void ClientUserinfoChanged (edict_t *ent, char *userinfo);
void ClientDisconnect (edict_t *ent);
void ClientBegin (edict_t *ent, qboolean loadgame);
void ClientCommand (edict_t *ent);
void WriteGame (char *filename);
void ReadGame (char *filename);
void WriteLevel (char *filename);
void ReadLevel (char *filename);
void InitGame (void);
void G_RunFrame (void);

void SetBotFlag1(edict_t *ent);	//チーム1の旗
void SetBotFlag2(edict_t *ent);  //チーム2の旗

//===================================================================

void ShutdownGame (void)
{
	gi.dprintf ("==== ShutdownGame ====\n");

//	Bot_LevelChange();

	gi.FreeTags (TAG_LEVEL);
	gi.FreeTags (TAG_GAME);
	SetBotFlag1(NULL);
	SetBotFlag2(NULL);
}

/*
 * Returns a pointer to the structure with
 * all entry points and global variables
 *
 * yquake2 does not use q_exported which
 * will cause Q2RTX to not find the dll
 */
q_exported game_export_t *
GetGameAPI(game_import_t *import)
{
	gi = *import;
	globals.apiversion = GAME_API_VERSION;
	globals.Init = InitGame;
	globals.Shutdown = ShutdownGame;
	globals.SpawnEntities = SpawnEntities;

	globals.WriteGame = WriteGame;
	globals.ReadGame = ReadGame;
	globals.WriteLevel = WriteLevel;
	globals.ReadLevel = ReadLevel;

	globals.ClientThink = ClientThink;
	globals.ClientConnect = ClientConnect;
	globals.ClientUserinfoChanged = ClientUserinfoChanged;
	globals.ClientDisconnect = ClientDisconnect;
	globals.ClientBegin = ClientBegin;
	globals.ClientCommand = ClientCommand;

	globals.RunFrame = G_RunFrame;

	globals.ServerCommand = ServerCommand;

	globals.edict_size = sizeof(edict_t);

	return &globals;
}

#ifndef GAME_HARD_LINKED
// this is only here so the functions in q_shared.c and q_shwin.c can link
void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, error);
	vsprintf (text, error, argptr);
	va_end (argptr);

	gi.error (ERR_FATAL, "%s", text);
}

void Com_Printf (char *msg, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, msg);
	vsprintf (text, msg, argptr);
	va_end (argptr);

	gi.dprintf ("%s", text);
}

#endif

//======================================================================


/*
=================
ClientEndServerFrames
=================
*/
void ClientEndServerFrames (void)
{
	int		i;
	edict_t	*ent;

	// calc the player views now that all pushing
	// and damage has been added
	for (i=0 ; i<maxclients->value ; i++)
	{
		ent = g_edicts + 1 + i;
		if (!ent->inuse || !ent->client)
			continue;
		if(!(ent->svflags & SVF_MONSTER))  
			ClientEndServerFrame (ent);
	}

}


/*
=================
GetNextMap

get next map's file name
=================
*/
void Get_NextMap(void)
{
	FILE	*fp;
	qboolean	firstflag = false;
	char	Buff[MAX_QPATH];
	char	top[MAX_QPATH];
	char	nextmap[MAX_QPATH];
	int		i;
	
	if(!maplist->string) return;

	sprintf(Buff,".\\%s\\3ZBMAPS.LST",gamepath->string);
	fp = fopen(Buff,"r");
	if(fp == NULL) return;
	
	//search section
	while(1)
	{
		if(fgets( Buff, sizeof(Buff), fp ) == NULL) goto NONEXTMAP;

		if(Buff[0] != '[') continue;

		i = 0;
		while(1)
		{
			if(Buff[i] == ']') Buff[i] = 0;

			if(Buff[i] == 0) break;

			if(++i >= sizeof(Buff))
			{
				Buff[i - 1] = 0;
				break;
			}
		}
		//compare map section name
		if(Q_stricmp (&Buff[1], maplist->string) == 0) break;
	}

	//search current mapname
	while(1)
	{
		if(fgets( Buff, sizeof(Buff), fp ) == NULL) goto NONEXTMAP;

		if(Buff[0] == '[')
		{
			if( firstflag )
			{
				strcpy(nextmap,top);
				goto SETNEXTMAP;
			}
			else goto NONEXTMAP;
		}

		if(Buff[0] == '\n') continue;

		sscanf(Buff,"%s",nextmap);

		if(!firstflag)
		{
			firstflag = true;
			strcpy(top,nextmap);
		}

		if(Q_stricmp (level.mapname, nextmap) == 0) break;
	}

	//search nextmap
	while(1)
	{
		if(fgets( Buff, sizeof(Buff), fp ) == NULL)
		{
            strcpy(nextmap, top);
            goto SETNEXTMAP;
        }

		if(Buff[0] == '[')
		{
            strcpy(nextmap, top);
            goto SETNEXTMAP;
        }

		if(Buff[0] == '\n') continue;

		sscanf(Buff,"%s",nextmap);
		break;
	}
SETNEXTMAP:

	strcpy(level.nextmap,nextmap);

NONEXTMAP:
	fclose(fp);

}
/*
=================
EndDMLevel

The timelimit or fraglimit has been exceeded
=================
*/
void EndDMLevel (void)
{
	edict_t		*ent;

	Get_NextMap();

	// stay on same level flag
	if ((int)dmflags->value & DF_SAME_LEVEL)
	{
		ent = G_Spawn ();
		ent->classname = "target_changelevel";
		ent->map = level.mapname;
	}
	else if (level.nextmap[0])
	{	// go to a specific map
		ent = G_Spawn ();
		ent->classname = "target_changelevel";
		ent->map = level.nextmap;
	}
	else
	{	// search for a changeleve
		ent = G_Find (NULL, FOFS(classname), "target_changelevel");
		if (!ent)
		{	// the map designer didn't include a changelevel,
			// so create a fake ent that goes back to the same level
			ent = G_Spawn ();
			ent->classname = "target_changelevel";
			ent->map = level.mapname;
		}
	}

	BeginIntermission (ent);

//PONKO
	Bot_LevelChange();
//PONKO
}

/*
=================
CheckNeedPass
=================
*/
void CheckNeedPass (void)
{
	int need;

	// if password or spectator_password has changed, update needpass
	// as needed
	if (password->modified || spectator_password->modified) 
	{
		password->modified = spectator_password->modified = false;

		need = 0;

		if (*password->string && Q_stricmp(password->string, "none"))
			need |= 1;
		if (*spectator_password->string && Q_stricmp(spectator_password->string, "none"))
			need |= 2;

		gi.cvar_set("needpass", va("%d", need));
	}
}

/*
=================
CheckDMRules
=================
*/
void CheckDMRules (void)
{
	int			i;
	gclient_t	*cl;

	if (level.intermissiontime)
		return;

	if (!deathmatch->value)
		return;

	if (timelimit->value)
	{
		if (level.time >= timelimit->value*60)
		{
			gi.bprintf (PRINT_HIGH, "Timelimit hit.\n");
			EndDMLevel ();
			return;
		}
	}

	if (fraglimit->value)
	{
//ZOID
		if (ctf->value) {
			if (CTFCheckRules()) {
				EndDMLevel ();
			}
		}
//ZOID
		for (i=0 ; i<maxclients->value ; i++)
		{
			cl = game.clients + i;
			if (!g_edicts[i+1].inuse)
				continue;

			if (cl->resp.score >= fraglimit->value)
			{
				gi.bprintf (PRINT_HIGH, "Fraglimit hit.\n");
				EndDMLevel ();
				return;
			}
		}
	}
}


/*
=============
ExitLevel
=============
*/
void ExitLevel (void)
{
	int		i;
	edict_t	*ent;
	char	command [256];

	Com_sprintf (command, sizeof(command), "gamemap \"%s\"\n", level.changemap);
	gi.AddCommandString (command);
	level.changemap = NULL;
	level.exitintermission = 0;
	level.intermissiontime = 0;
	ClientEndServerFrames ();

	// clear some things before going to next level
	for (i=0 ; i<maxclients->value ; i++)
	{
		ent = g_edicts + 1 + i;
		if (!ent->inuse)
			continue;
		if (ent->health > ent->client->pers.max_health)
			ent->health = ent->client->pers.max_health;
	}

	SetBotFlag1(NULL);
	SetBotFlag2(NULL);

//ZOID
	CTFInit();
//ZOID
}

/*
================
G_RunFrame

Advances the world by 0.1 seconds
================
*/

void G_InitEdict (edict_t *e);

void G_RunFrame (void)
{
	int		i,j;
	static float next_fragadd = 0;
	edict_t	*ent;

	vec3_t	v,vv;
	qboolean haveflag;

	level.framenum++;
	level.time = level.framenum*FRAMETIME;

	// choose a client for monsters to target this frame
//	AI_SetSightClient ();

	// exit intermissions

	if (level.exitintermission)
	{
		ExitLevel ();
		return;
	}

//
// Bot Spawning
//
	if(SpawnWaitingBots && !level.intermissiontime)
	{
		if(spawncycle < level.time)
		{
			Bot_SpawnCall();
			spawncycle = level.time + FRAMETIME * 10 + 0.01 * SpawnWaitingBots;
		}
	}
	else
	{
		if(spawncycle < level.time) spawncycle = level.time + FRAMETIME * 10;
	}
	//
	// treat each object in turn
	// even the world gets a chance to think
	//
	haveflag = false;
	ent = &g_edicts[0];
	for (i=0 ; i<globals.num_edicts ; i++, ent++)
	{
		if (!ent->inuse)
			continue;

		level.current_entity = ent;

		VectorCopy (ent->s.origin, ent->s.old_origin);

		// if the ground entity moved, make sure we are still on it
		if ((ent->groundentity) && (ent->groundentity->linkcount != ent->groundentity_linkcount))
		{
			ent->groundentity = NULL;
			if ( !(ent->flags & (FL_SWIM|FL_FLY)) && (ent->svflags & SVF_MONSTER) )
			{
				M_CheckGround (ent);
			}
		}

		//ctf job assign
		if(ctf->value)
		{
			if(ctfjob_update < level.time)
			{
//gi.bprintf(PRINT_HIGH,"Assigned!!!\n");
				CTFJobAssign();
				ctfjob_update = level.time + FRAMETIME * 2; 
			}
		}
//////////旗のスコアチェック
		if(zigmode->value == 1)
		{
			if(next_fragadd < level.time)
			{
				if(i > 0 && i <= maxclients->value && g_edicts[i].client)
				{
					if(g_edicts[i].client->pers.inventory[ITEM_INDEX(zflag_item)])
					{
						zflag_ent = NULL;
						haveflag = true;
						gi.sound(ent, CHAN_VOICE, gi.soundindex("misc/secret.wav"), 1, ATTN_NORM, 0);
						if (!((int)(dmflags->value) & (DF_MODELTEAMS | DF_SKINTEAMS)))
													g_edicts[i].client->resp.score += 1;
						else
						{
							//旗を持ってるとフラッグを足す
							for ( j = 1 ; j <= maxclients->value ; j++)
							{
								if(g_edicts[j].inuse)
								{
									if(OnSameTeam(&g_edicts[i],&g_edicts[j]))
										g_edicts[j].client->resp.score += 1;
								}
							}
						}	
					}
				}
				if(zflag_ent != NULL)
				{
					if(!zflag_ent->inuse)
					{
	//				item = FindItem("Zig Flag");
						SelectSpawnPoint (ent, v, vv);
			//			VectorCopy (v, ent->s.origin);
						if(ZIGDrop_Flag(ent,zflag_item))
						{
							VectorCopy (v, zflag_ent->s.origin);
						}			
					}
				}
			}
		}
/////////////
		if (i > 0 && i <= maxclients->value && !(ent->svflags & SVF_MONSTER))
		{
			ClientBeginServerFrame (ent);
			continue;
		}

		G_RunEntity (ent);
	}

	if(next_fragadd < level.time)
	{
		if(zflag_ent == NULL && !haveflag && !ctf->value 
			&& zigmode->value == 1 && zigflag_spawn == 2)
		{
			SelectSpawnPoint (ent, v, vv);
			//VectorCopy (v, ent->s.origin);
			if(ZIGDrop_Flag(ent,zflag_item))
			{
				VectorCopy (v, zflag_ent->s.origin);
			}
		}

		next_fragadd = level.time + FRAMETIME * 100;
	}

	// see if it is time to end a deathmatch
	CheckDMRules ();

	// see if needpass needs updated
	CheckNeedPass ();

	// build the playerstate_t structures for all players
	ClientEndServerFrames ();
}

