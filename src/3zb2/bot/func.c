#include "../header/bot.h"
#include "../header/player.h"


qboolean Get_YenPos(char *Buff,int *curr)
{
	int i;

	i = *curr + 1;

	while(1)
	{
//		if(i >= strlen(Buff)) return false;
		if(Buff[i] == 0 || Buff[i] == 10 || Buff[i] == 13)
		{
			*curr = i;
			return true;
		}
		if(Buff[i] == '\\')
		{
			*curr = i;
			return true;
		}
		if(Buff[i] == '\t') Buff[i] = 0;
		i++;
	}
}
//----------------------------------------------------------------
//Load Bot Info
//
// Load bot's infomation from 3ZBConfig.cfg
//
//----------------------------------------------------------------
void Load_BotInfo()
{
	char	MessageSection[50];
	char	Buff[1024];
	int		i,j,k,l;

	FILE	*fp;

	SpawnWaitingBots = 0;
	ListedBotCount = 0;

	//init message
	memset(ClientMessage,0,sizeof(ClientMessage));
	//set message section
	if(!ctf->value && chedit->value) strcpy(MessageSection,MESS_CHAIN_DM);
	else if(ctf->value && !chedit->value) strcpy(MessageSection,MESS_CTF);
	else if(ctf->value && chedit->value) strcpy(MessageSection,MESS_CHAIN_CTF);
	else strcpy(MessageSection,MESS_DEATHMATCH);

	//init botlist
	ListedBots = 0;
	j = 1;
	for(i = 0;i < MAXBOTS;i++)
	{
		//netname
		sprintf(Buff,"Zigock[%i]",i);
		strcpy(Bot[i].netname,Buff);
		//model
		strcpy(Bot[i].model,"male");
		//skin
		strcpy(Bot[i].model,"grunt");
	
		//param
		Bot[i].param[BOP_WALK] = 0;
		Bot[i].param[BOP_AIM] = 5;
		Bot[i].param[BOP_PICKUP] = 5;
		Bot[i].param[BOP_COMBATSKILL] = 5;
		Bot[i].param[BOP_ROCJ] = 0;
		Bot[i].param[BOP_VRANGE] = 90;
		Bot[i].param[BOP_HRANGE] = 180;
		Bot[i].param[BOP_REACTION] = 0;

		//spawn flag
		Bot[i].spflg = 0;
		//team
		Bot[i].team = j;
		if(++j > 2) j = 1;
	}

	//botlist value
	botlist = gi.cvar ("botlist", "default", CVAR_SERVERINFO | CVAR_LATCH);
	gamepath = gi.cvar ("game", "0", CVAR_NOSET);

	//load info
	sprintf(Buff,"%s/3ZBConfig.cfg",gamepath->string);
	fp = fopen(Buff,"rt");
	if(fp == NULL)
	{
		gi.dprintf("3ZB CFG: file not found: %s\n", Buff);
		return;
	}
	else
	{
		fseek( fp, 0, SEEK_SET);	//先頭へ移動
		while(1)
		{
			if(fgets( Buff, sizeof(Buff), fp ) == NULL) goto MESS_NOTFOUND;
			if(!Q_strncasecmp(MessageSection,Buff,strlen(MessageSection))) break;
		}

		while(1)
		{
			if(fgets( Buff, sizeof(Buff), fp ) == NULL) goto MESS_NOTFOUND;
			if(Buff[0] == '.' || Buff[0] == '[' || Buff[0] == '#') break;
			k = strlen(Buff);
			if((strlen(Buff) + strlen(ClientMessage)) > MAX_STRING_CHARS - 1) break;
			strcat(ClientMessage,Buff);
		}
MESS_NOTFOUND:
		//if(botlist->string == NULL) strcpy(MessageSection,BOTLIST_SECTION_DM);
		//else 
		sprintf(MessageSection,"[%s]",botlist->string);
		fseek( fp, 0, SEEK_SET);	//先頭へ移動
		while(1)
		{
			if(fgets( Buff, sizeof(Buff), fp ) == NULL)
			{
				MessageSection[0] = 0;
				break;
			}
			if(!Q_strncasecmp(MessageSection,Buff,strlen(MessageSection))) break;
		}
		//when not found
		if(MessageSection[0] == 0)
		{
			strcpy(MessageSection,BOTLIST_SECTION_DM);
			fseek( fp, 0, SEEK_SET);	//先頭へ移動
			while(1)
			{
				if(fgets( Buff, sizeof(Buff), fp ) == NULL) goto BOTLIST_NOTFOUND;
				if(!Q_strncasecmp(MessageSection,Buff,strlen(MessageSection))) break;
			}
		}

		i = 0;
		for(i = 0;i < MAXBOTS;i++)
		{
			if(fgets( Buff, sizeof(Buff), fp ) == NULL) break;
			if(Buff[0] == '[') break;
			if(Buff[0] == '\n' || Buff[0] == '#') {i--;continue;}
			j = 2,k = 1;
			if(strncmp(Buff,"\\\\",2))
			{
				i--;
			}
			else
			{
				//netname
				if(Get_YenPos(Buff,&k))
				{
					Buff[k] = 0;
					if(strlen(&Buff[j]) < 21) strcpy(Bot[i].netname,&Buff[j]);
					j = k + 1;
				}
				else break;
				//model name
				if(Get_YenPos(Buff,&k))
				{
					Buff[k] = 0;
					if(strlen(&Buff[j]) < 21) strcpy(Bot[i].model,&Buff[j]);
					j = k + 1;
					k++;
				}
				else break;
				//skin name
				if(Get_YenPos(Buff,&k))
				{
					Buff[k] = 0;
					if(strlen(&Buff[j]) < 21) strcpy(Bot[i].skin,&Buff[j]);
					j = k + 1;
					k++;
				}
				else break;
				for(l = 0;l < MAXBOP;l++)
				{
					//param0-7
					if(Get_YenPos(Buff,&k))
					{
						Buff[k] = 0;
						Bot[i].param[l] = (unsigned char)atoi(&Buff[j]);
						j = k + 1;
						k++;
					}
					else break;
				}
				if(l < MAXBOP) break;
				//team
				if(Get_YenPos(Buff,&k))
				{
					Buff[k] = 0;
					if(Buff[j] == 'R') Bot[i].team = 1;
					else if(Buff[j] == 'B') Bot[i].team = 2;
					else Bot[i].team = 1;
					j = k + 1;
					k++;
				}
				else break;
				//auto spawn
				if(Get_YenPos(Buff,&k))
				{
					Buff[k] = 0;
					Bot[i].spflg = atoi(&Buff[j]);
//gi.dprintf("%i %s\n",Bot[i].spflg,&Buff[j]);
					if(Bot[i].spflg == BOT_SPRESERVED && autospawn->value && !chedit->value) SpawnWaitingBots++; 
					else Bot[i].spflg = BOT_SPAWNNOT;
				}
				else break;
				ListedBots++;
			}
		}
	}
BOTLIST_NOTFOUND:
	fclose(fp);

	gi.dprintf("%i of Bots is listed.\n",ListedBots);	
	spawncycle = level.time + FRAMETIME * 100;
}

//----------------------------------------------------------------
//Get Number of Client
//
// Total Client
//
//----------------------------------------------------------------

int Get_NumOfPlayer (void) //Botも含めたplayerの数
{
	int i,j;
	edict_t *ent;

	j = 0;
	for (i=0 ; i<maxclients->value ; i++)
	{
		ent = g_edicts + 1 + i;
		if (ent->inuse)	j++;
	}
	return j;
}

//----------------------------------------------------------------
//Get New Client
//
// Get new client edict
//
//----------------------------------------------------------------

edict_t *Get_NewClient (void)
{
	int			i;
	edict_t		*e;
	gclient_t	*client;

	e = &g_edicts[(int)maxclients->value];
	for ( i = maxclients->value ; i >= 1  ; i--, e--)
	{
		client = &game.clients[i - 1];
		// the first couple seconds of server time can involve a lot of
		// freeing and allocating, so relax the replacement policy
		if (!e->inuse && !client->pers.connected && ( e->freetime < 2 || level.time - e->freetime > 0.5 ) )
		{
			G_InitEdict (e);
			return e;
		}
	}
	gi.error ("ED_Alloc: no free edicts shit");
	return NULL;
}


//----------------------------------------------------------------
//Bot Think
//
// Bot's think code
//
//----------------------------------------------------------------
void Bot_Think (edict_t *self)
{
	if (self->linkcount != self->monsterinfo.linkcount)
	{
//		self->monsterinfo.linkcount = self->linkcount;
		M_CheckGround (self);
	}

	if(self->deadflag)
	{
		if(self->client->ctf_grapple) CTFPlayerResetGrapple(self);

		if(self->s.modelindex == skullindex || self->s.modelindex == headindex) self->s.frame = 0;
		else if(self->s.frame < FRAME_crdeath1 && self->s.frame != 0) self->s.frame = FRAME_death308;
		self->s.modelindex2 = 0;	// remove linked weapon model
//ZOID
		self->s.modelindex3 = 0;	// remove linked ctf flag
//ZOID

		self->client->zc.route_trace = false;
		if(self->client->respawn_time <= level.time)
		{
			if(self->svflags & SVF_MONSTER)
			{
				self->client->respawn_time = level.time;
				CopyToBodyQue (self);
				PutBotInServer(self);
			}		
		}
	}
	else
	{
		Bots_Move_NORM (self);
		if(!self->inuse) return;			//removed botself

		ClientBeginServerFrame (self);
	}
	if (self->linkcount != self->monsterinfo.linkcount)
	{
//		self->monsterinfo.linkcount = self->linkcount;
		M_CheckGround (self);
	}
	M_CatagorizePosition (self);
	BotEndServerFrame (self);
	self->nextthink = level.time + FRAMETIME;
	return;	
}

//----------------------------------------------------------------
//Initialize Bot
//
// Initialize bot edict
//
//----------------------------------------------------------------

void InitializeBot (edict_t *ent,int botindex )
{
	gclient_t	*client;
	char		pinfo[200];
	int			index;

	index = ent-g_edicts-1;
	ent->client = &game.clients[index];

	client = ent->client;

	memset (&client->zc,0,sizeof(zgcl_t));
	memset (&client->pers, 0, sizeof(client->pers));
	memset (&client->resp, 0, sizeof(client->resp));

	//set botindex NO.
	client->zc.botindex = botindex;	

	client->resp.enterframe = level.framenum;

	//set netname model skil and CTF team
	sprintf(pinfo,"\\rate\\25000\\msg\\1\\fov\\90\\skin\\%s/%s\\name\\%s\\hand\\0",Bot[botindex].model,Bot[botindex].skin,Bot[botindex].netname);
	ent->client->resp.ctf_team = Bot[botindex].team; //CTF_TEAM1,CTF_TEAM2

	ClientUserinfoChanged (ent, pinfo);

	client->pers.health			= 100;
	client->pers.max_health		= 100;

	client->pers.max_bullets	= 200;
	client->pers.max_shells		= 100;
	client->pers.max_rockets	= 50;
	client->pers.max_grenades	= 50;
	client->pers.max_cells		= 200;
	client->pers.max_slugs		= 50;

	// RAFAEL
	client->pers.max_magslug	= 50;
	client->pers.max_trap		= 5;

	ent->client->pers.connected = false;
	gi.dprintf ("%s connected\n", ent->client->pers.netname);
//	gi.bprintf (PRINT_HIGH, "%s entered the game\n", ent->client->pers.netname);

	if(ctf->value)	gi.bprintf(PRINT_HIGH, "%s joined the %s team.\n",
			client->pers.netname, CTFTeamName(ent->client->resp.ctf_team));
	else 	gi.bprintf (PRINT_HIGH, "%s entered the game\n",
			client->pers.netname);
}

void PutBotInServer (edict_t *ent)
{
	edict_t		*touch[MAX_EDICTS];
	int			i,j,entcount;
	gitem_t		*item;
	gclient_t	*client;
	vec3_t	spawn_origin, spawn_angles;
	trace_t		rs_trace;


	zgcl_t		*zc;		
	
	zc = &ent->client->zc;

//test
//	item = FindItem("Trap");
//	ent->client->pers.inventory[ITEM_INDEX(item)] = 100;
//test	

	//current weapon
	client = ent->client;
	item = Fdi_BLASTER;//FindItem("Blaster");
	client->pers.selected_item = ITEM_INDEX(item);
	client->pers.inventory[client->pers.selected_item] = 1;
	client->pers.weapon = item;
	client->silencer_shots = 0;
	client->weaponstate = WEAPON_READY;
	client->newweapon = NULL;

	//clear powerups
	client->quad_framenum = 0;
	client->invincible_framenum = 0;
	client->breather_framenum = 0;
	client->enviro_framenum = 0;
	client->grenade_blew_up = false;
	client->grenade_time = 0;

	j = zc->botindex;
	i = zc->routeindex;
	memset (&client->zc,0,sizeof(zgcl_t));
	zc->botindex = j;
	zc->routeindex = i;

//ZOID
	client->ctf_grapple = NULL;

	item = FindItem("Grapple");
	if(ctf->value)	client->pers.inventory[ITEM_INDEX(item)] = 1; //ponpoko
//ZOID

	// clear entity values
	ent->classname = "player";
	ent->movetype = MOVETYPE_STEP;
	ent->solid = SOLID_BBOX;
	ent->model = "players/male/tris.md2";
	VectorSet (ent->mins, -16, -16, -24);
	VectorSet (ent->maxs, 16, 16, 32);

	ent->health = ent->client->pers.health;
	ent->max_health = ent->client->pers.max_health;
	ent->gib_health = -40;

	ent->mass = 200;
	ent->target_ent = NULL;
	ent->s.frame = 0;

	// clear entity state values
	ent->s.modelindex = 255;		// will use the skin specified model
	ent->s.skinnum = ent - g_edicts - 1;
	ShowGun(ent);					// ### Hentai ### special gun model

	ent->s.sound = 0;

	ent->monsterinfo.scale = MODEL_SCALE;

	ent->pain = player_pain;
	ent->die = player_die;
	ent->touch = NULL; 
	
	ent->moveinfo.decel = level.time;
	ent->pain_debounce_time = level.time;
	ent->targetname = NULL;

	ent->moveinfo.speed = 1.0;	//ジャンプ中の移動率について追加
	ent->moveinfo.state = GETTER;	//CTFステータス初期化

	ent->prethink = NULL;
	ent->think = Bot_Think;
	ent->nextthink = level.time + FRAMETIME;
	ent->svflags /*|*/= SVF_MONSTER ;
	ent->s.renderfx = 0;
	ent->s.effects = 0;

	SelectSpawnPoint (ent, spawn_origin, spawn_angles);
	VectorCopy (spawn_origin, ent->s.origin);
	VectorCopy (spawn_angles, ent->s.angles);
	spawn_origin[2] -= 300;
	rs_trace = gi.trace(ent->s.origin,ent->mins,ent->maxs,spawn_origin,ent,MASK_SOLID);
	if(!rs_trace.allsolid) VectorCopy (rs_trace.endpos, ent->s.origin);
	VectorSet(ent->velocity,0,0,0);
	ent->moveinfo.speed = 0;
	ent->groundentity = rs_trace.ent;
	ent->client->ps.pmove.pm_flags &= ~PMF_DUCKED;

	Set_BotAnim(ent,ANIM_BASIC,FRAME_run1,FRAME_run6);
	client->anim_run = true;

	ent->client->ctf_grapple = NULL;
	ent->client->quad_framenum = level.framenum;
	ent->client->invincible_framenum = level.framenum;
	ent->client->enviro_framenum = level.framenum;
	ent->client->breather_framenum = level.framenum;
	ent->client->weaponstate = WEAPON_READY;
	ent->takedamage = DAMAGE_AIM;
	ent->air_finished = level.time + 12;
	ent->clipmask = MASK_PLAYERSOLID;//MASK_MONSTERSOLID;
	ent->flags &= ~FL_NO_KNOCKBACK;

	ent->client->anim_priority = ANIM_BASIC;
//	ent->client->anim_run = true;
	ent->s.frame = FRAME_run1-1;
	ent->client->anim_end = FRAME_run6;
	ent->deadflag = DEAD_NO;
	ent->svflags &= ~SVF_DEADMONSTER;

	zc->waitin_obj = NULL;
	zc->first_target = NULL;
	zc->first_target = NULL;
	zc->zcstate = STS_IDLE;

	if(ent->client->resp.enterframe == level.framenum && !chedit->value)
	{
		gi.WriteByte (svc_muzzleflash);
		gi.WriteShort (ent-g_edicts);
		gi.WriteByte (MZ_LOGIN);
		gi.multicast (ent->s.origin, MULTICAST_PVS);
	}
	else if(!chedit->value)
	{
		gi.WriteByte (svc_muzzleflash);
		gi.WriteShort (ent-g_edicts);
		gi.WriteByte (MZ_RESPAWN);
		gi.multicast (ent->s.origin, MULTICAST_PVS);
	}
	gi.linkentity (ent);
	VectorAdd (spawn_origin, ent->mins, ent->absmin);
	VectorAdd (spawn_origin, ent->maxs, ent->absmax);
	entcount = gi.BoxEdicts ( ent->absmin ,ent->absmax,touch,MAX_EDICTS,AREA_SOLID);
	while (entcount-- > 0)
	{
		if(Q_stricmp (touch[entcount]->classname, "player") == 0)
			if(touch[entcount] != ent)
				T_Damage (touch[entcount], ent, ent, vec3_origin, touch[entcount]->s.origin, vec3_origin, 100000, 0, DAMAGE_NO_PROTECTION, MOD_TELEFRAG);
	}

	if(ctf->value)
	{
		CTFPlayerResetGrapple(ent);
		client->zc.ctfstate = CTFS_OFFENCER;
	}


	gi.linkentity (ent);
	G_TouchTriggers (ent);
}

//----------------------------------------------------------------
//Spawn Bot
//
// spawn bots
//
//	int i	index of bot list
//
//----------------------------------------------------------------

qboolean SpawnBot(int i)
{
	edict_t		*bot,*ent;
	int			k,j;


//gi.cprintf (NULL,PRINT_HIGH,"Called %s %s %s\n",Bot[i].netname,Bot[i].model,Bot[i].skin);
//return false;	

	if(	Get_NumOfPlayer () >= game.maxclients )
	{
		gi.cprintf (NULL,PRINT_HIGH,"Can't add bots\n");
		return false;
	}

	bot = Get_NewClient();
	if(bot == NULL) return false;

	InitializeBot( bot , i);
	PutBotInServer ( bot );

	j = targetindex;
	if(chedit->value)
	{
		for(k = CurrentIndex - 1;k > 0 ;k--)
		{
			if(Route[k].index == 0) break;

			if(Route[k].state == GRS_NORMAL)
			{
				if(--j <= 0) break;
			}  
		}

		bot->client->zc.rt_locktime = level.time + FRAMETIME * 20;
		bot->client->zc.route_trace = true;
		bot->client->zc.routeindex = k;
		VectorCopy(Route[k].Pt,bot->s.origin);
		VectorAdd (bot->s.origin, bot->mins, bot->absmin);
		VectorAdd (bot->s.origin, bot->maxs, bot->absmax);
		bot->client->ps.pmove.pm_flags |= PMF_DUCKED;
		gi.linkentity (bot);
//		bot->s.modelindex = 0;

		gi.WriteByte (svc_muzzleflash);
		gi.WriteShort (bot-g_edicts);
		gi.WriteByte (MZ_LOGIN);
		gi.multicast (bot->s.origin, MULTICAST_PVS);

		ent = &g_edicts[1];
		if(ent->inuse && ent->client && !(ent->svflags & SVF_MONSTER))
		{
			ent->takedamage = DAMAGE_NO;
			ent->movetype = MOVETYPE_NOCLIP;
			ent->target_ent = bot;
			ent->solid = SOLID_NOT;
			ent->client->ps.pmove.pm_type = PM_FREEZE;
			ent->client->ps.pmove.pm_flags |= PMF_NO_PREDICTION ;
			VectorCopy(ent->s.origin,ent->moveinfo.start_origin);
		}
	}

	return true;
}

//----------------------------------------------------------------
//Spawn Call
//
// spawn bots
//
//	int i	index of bot list
//
//----------------------------------------------------------------
void Bot_SpawnCall()
{
	int i;

	for(i = 0;i < MAXBOTS;i++)
	{
		if(Bot[i].spflg == BOT_SPRESERVED)
		{
			if(SpawnBot(i)) Bot[i].spflg = BOT_SPAWNED;
			else 
			{
				Bot[i].spflg = BOT_SPAWNNOT;
				targetindex = 0;
			}
			SpawnWaitingBots--;
			break;
		}
	}
}
//----------------------------------------------------------------
//Spawn Bot Reserving
//
// spawn bots reserving
//
//----------------------------------------------------------------
void SpawnBotReserving()
{
	int	i;

	for(i = 0;i < MAXBOTS; i++)
	{
		if(Bot[i].spflg == BOT_SPAWNNOT)
		{
			Bot[i].spflg = BOT_SPRESERVED;
			SpawnWaitingBots++;
			return;
		}
	}
	gi.cprintf (NULL, PRINT_HIGH, "Now max of bots(%i) already spawned.\n",MAXBOTS);
}
//----------------------------------------------------------------
//Spawn Bot Reserving 2
//
// randomized spawn bots reserving
//
//----------------------------------------------------------------
void SpawnBotReserving2(int *red,int *blue)
{
	int	i,j;

	j = (int)(random() * ListedBots);

	for(i = 0;i < ListedBots; i++,j++)
	{
		if(j >= ListedBots) j = 0;
		if(Bot[j].spflg == BOT_SPAWNNOT)
		{
			Bot[j].spflg = BOT_SPRESERVED;
			SpawnWaitingBots++;
			if(*red > *blue) Bot[j].team = 2;
			else Bot[j].team = 1;
			
			if(Bot[j].team == 1) *red = *red + 1;
			else if(Bot[j].team == 2) *blue = *blue + 1;
//gi.cprintf(NULL,PRINT_HIGH,"team %i\n",Bot[j].team);
			return;
		}
	}
	SpawnBotReserving();
}

//----------------------------------------------------------------
//Remove Bot
//
// remove bots
//
//	int i	index of bot list
//
//----------------------------------------------------------------
void RemoveBot()
{
	int			i;
	int			botindex;
	edict_t		*e,*ent;
	gclient_t	*client;

	for(i = MAXBOTS - 1;i >= 0;i--)
	{
		if(Bot[i].spflg == BOT_SPAWNED || Bot[i].spflg == BOT_NEXTLEVEL)
		{
			break;
		}
	}

	if(i < 0)
	{
		gi.cprintf (NULL, PRINT_HIGH, "No Bots in server.");
		return;
	}

	botindex = i;

	e = &g_edicts[(int)maxclients->value];
	for ( i = maxclients->value ; i >= 1  ; i--, e--)
	{
		if(!e->inuse) continue;
		client = /*e->client;*/&game.clients[i - 1];
		if(client == NULL) continue;
		// the first couple seconds of server time can involve a lot of
		// freeing and allocating, so relax the replacement policy
		if (!client->pers.connected && (e->svflags & SVF_MONSTER))
		{
			if(client->zc.botindex == botindex)
			{
				if(Bot[botindex].spflg != BOT_NEXTLEVEL) Bot[botindex].spflg = BOT_SPAWNNOT;
				else Bot[botindex].spflg = BOT_SPRESERVED;

				gi.bprintf (PRINT_HIGH, "%s disconnected\n", e->client->pers.netname);
	
				// send effect
				gi.WriteByte (svc_muzzleflash);
				gi.WriteShort (e-g_edicts);
				gi.WriteByte (MZ_LOGOUT);
				gi.multicast (e->s.origin, MULTICAST_PVS);

				e->s.modelindex = 0;
				e->solid = SOLID_NOT;

	if(ctf->value) CTFPlayerResetGrapple(e);
				
				gi.linkentity (e);

				e->inuse = false;
				G_FreeEdict (e);

				if(targetindex)
				{
					ent = &g_edicts[1];

					if(ent->inuse)
					{
						ent->health = 100;
						ent->movetype = MOVETYPE_WALK;
						ent->takedamage = DAMAGE_AIM;
						ent->target_ent = NULL;
						ent->solid = SOLID_BBOX;
						ent->client->ps.pmove.pm_type = PM_NORMAL;
						ent->client->ps.pmove.pm_flags = PMF_DUCKED;
						VectorCopy(ent->moveinfo.start_origin,ent->s.origin);
						VectorCopy(ent->moveinfo.start_origin,ent->s.old_origin);
					}
					targetindex = 0;
				}
				return;
			}
		}
	}
	gi.error ("Can't remove bot.");
}

//----------------------------------------------------------------
//Level Change Removing
//
// 
//
//----------------------------------------------------------------
void Bot_LevelChange()
{
	int i,j,k;

	j = 0,k = 0;

	for(i = 0;i < MAXBOTS;i++)
	{
		if(Bot[i].spflg)
		{
			if(Bot[i].spflg == BOT_SPAWNED)
			{
				k++;
				Bot[i].spflg = BOT_NEXTLEVEL;
			}
			j++;
		}
	}
	for(i = 0;i < k; i++)
	{
		RemoveBot();
	}

	SpawnWaitingBots = k;//j;
}
//----------------------------------------------------------------
//
//	Ragomode menu
//
void ZigockClientJoin(edict_t  *ent,int zclass)
{
	PMenu_Close(ent);

	ent->moveinfo.sound_end = CLS_ALPHA;	//PutClientの前にクラス決定

	ent->svflags &= ~SVF_NOCLIENT;
	PutClientInServer (ent);
	// add a teleportation effect
	ent->s.event = EV_PLAYER_TELEPORT;
	// hold in place briefly
	ent->client->ps.pmove.pm_flags = PMF_TIME_TELEPORT;
	ent->client->ps.pmove.pm_time = 14;

	if(ctf->value)
	{
		gi.bprintf(PRINT_HIGH, "%s joined the %s team.\n",
			ent->client->pers.netname, CTFTeamName(ent->client->resp.ctf_team/*desired_team*/));
	}
}
void ClientJoinAsAlpha(edict_t *ent,pmenu_t *entries)
{
	ZigockClientJoin(ent,1);		
}

pmenu_t zgjoinmenu[] = {
	{ "*Quake II",			PMENU_ALIGN_CENTER, NULL, NULL },
	{ "*3rd Zigock Rago",	PMENU_ALIGN_CENTER, NULL, NULL },
	{ NULL,					PMENU_ALIGN_CENTER, NULL, NULL },
	{ NULL,					PMENU_ALIGN_CENTER, NULL, NULL },
	{ "alpha",				PMENU_ALIGN_LEFT, NULL, ClientJoinAsAlpha },
	{ "beta",				PMENU_ALIGN_LEFT, NULL, ClientJoinAsAlpha },
	{ "gamma",				PMENU_ALIGN_LEFT, NULL, ClientJoinAsAlpha },
	{ "delta",				PMENU_ALIGN_LEFT, NULL, ClientJoinAsAlpha },
	{ "epsilon",			PMENU_ALIGN_LEFT, NULL, ClientJoinAsAlpha },
	{ "zeta",				PMENU_ALIGN_LEFT, NULL, ClientJoinAsAlpha },
	{ "eta",				PMENU_ALIGN_LEFT, NULL, ClientJoinAsAlpha },
	{ NULL,					PMENU_ALIGN_LEFT, NULL, NULL },
	{ "Use [ and ] to move cursor",	PMENU_ALIGN_LEFT, NULL, NULL },
	{ "ENTER to select class",	PMENU_ALIGN_LEFT, NULL, NULL },
	{ "ESC to Exit Menu",	PMENU_ALIGN_LEFT, NULL, NULL },
	{ "(TAB to Return)",	PMENU_ALIGN_LEFT, NULL, NULL },
};

void ZigockJoinMenu(edict_t *ent)
{
	PMenu_Open(ent, zgjoinmenu,4, sizeof(zgjoinmenu) / sizeof(pmenu_t));
}

qboolean ZigockStartClient(edict_t *ent)
{
	if (ent->moveinfo.sound_end != CLS_NONE)
		return false;

	// start as 'observer'
	ent->movetype = MOVETYPE_NOCLIP;
	ent->solid = SOLID_NOT;
	ent->svflags |= SVF_NOCLIENT;
	ent->client->ps.gunindex = 0;
	gi.linkentity (ent);

	ZigockJoinMenu(ent);
	return true;
}


//===============================

//	AirStrike

//===============================
static void AirSight_Explode (edict_t *ent)
{
	vec3_t		origin;
	int			mod;

//	if (ent->owner->client && !(ent->owner->svflags & SVF_DEADMONSTER))
//		PlayerNoise(ent->owner, ent->s.origin, PNOISE_IMPACT);

	gi.sound (ent, CHAN_AUTO, gi.soundindex("3zb/airexp.wav"), 1, ATTN_NONE, 0);

	//FIXME: if we are onground then raise our Z just a bit since we are a point?
	mod = MOD_AIRSTRIKE;

	T_RadiusDamage(ent, ent->owner, ent->dmg, ent->enemy, ent->dmg_radius, mod);

	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);
	gi.WriteByte (svc_temp_entity);
	if (ent->waterlevel)
	{
		gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	}
	else
	{
		gi.WriteByte (TE_ROCKET_EXPLOSION);
	}
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	G_FreeEdict (ent);
}

void AirSight_Think(edict_t *ent)
{
//	gi.sound (ent, CHAN_AUTO, gi.soundindex("medic/medatck1.wav"), 1, ATTN_NORM, 0);
	gi.sound (ent, CHAN_BODY, gi.soundindex("3zb/airloc.wav"), 1, ATTN_NONE, 0);

	ent->dmg = 120 + random() * 60;
	ent->dmg_radius = 200;

	ent->s.modelindex = gi.modelindex ("sprites/airsight.sp2");
	VectorCopy(ent->target_ent->s.origin,ent->s.origin);

	if( ent->owner->client->resp.ctf_team == CTF_TEAM2 && ctf->value)
	{ 
		ent->s.frame = 1;
	}
	else ent->s.frame = 0;

	ent->think = AirSight_Explode;
	ent->nextthink = level.time + FRAMETIME * 6;
	gi.linkentity (ent);
}
void AirStrike_Think(edict_t *ent)
{
	int	i,j;
	edict_t	*target,*sight;
	trace_t	rs_trace;

	vec3_t	point;

	ent->nextthink = level.time + ent->moveinfo.speed * 0.5 /300;
	ent->think = G_FreeEdict;
//	ent->s.modelindex = gi.modelindex ("models/ships/bigviper/tris.md2");

	VectorCopy(ent->s.origin,point);
	point[2] = ent->moveinfo.start_angles[2];

	j = 1;
	for ( i = 1 ; i <= maxclients->value; i++)
	{
		target =  &g_edicts[i];
		if(!target->inuse || target->deadflag || target == ent->owner) continue;

		if( target->classname[0] == 'p')
		{
			//ctf ならチームメイト無視
			if(!ctf->value || (ctf->value && ent->owner->client->resp.ctf_team != target->client->resp.ctf_team))
			{
				rs_trace = gi.trace (point,NULL,NULL,target->s.origin,ent, CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_LAVA | CONTENTS_SLIME);

				if(rs_trace.fraction == 1.0)
				{
					sight = G_Spawn();

					sight->classname = "airstrike";
					sight->think = AirSight_Think;
					sight->nextthink = level.time + FRAMETIME * 2 * (float)j;
					sight->movetype = MOVETYPE_NOCLIP;
					sight->solid = SOLID_NOT;
					sight->owner = ent->owner;
					sight->target_ent = target;
					gi.linkentity (sight);
					j++;
				} 
			}
		}
	}

}
void Cmd_AirStrike(edict_t *ent)
{
	edict_t	*viper;
	trace_t	rs_trace;

	vec3_t	strpoint,tts,tte,tmp;

	vec_t	f;

	VectorCopy(ent->s.origin,strpoint);
	strpoint[2] += 8190;
	
	rs_trace = gi.trace (ent->s.origin,NULL,NULL,strpoint,ent, MASK_SHOT);

	if(!(rs_trace.surface && (rs_trace.surface->flags & SURF_SKY)))
	{
		gi.cprintf(ent,PRINT_HIGH,"can't call Viper.\n");
		return;
	}
/*	if((rs_trace.endpos[2] - ent->s.origin[2]) < 300)
	{
		gi.cprintf(ent,PRINT_HIGH,"can't call Viper.\n");	
	}*/

	VectorCopy(rs_trace.endpos,strpoint);
	strpoint[2] -= 16;	//ちょっとだけ下へずらす

	f = ent->s.angles[YAW]*M_PI*2 / 360;
	tts[0] = cos(f) * (-8190) ;
	tts[1] = sin(f) * (-8190) ;
	tts[2] = 0;

	tte[0] = cos(f) *8190 ;
	tte[1] = sin(f) *8190 ;
	tte[2] = 0;

	viper = G_Spawn();
	VectorClear (viper->mins);
	VectorClear (viper->maxs);
	viper->movetype = /*MOVETYPE_FLYMISSILE;//MOVETYPE_STEP;*/MOVETYPE_NOCLIP;
	viper->solid = SOLID_NOT;
	viper->owner = ent;
	viper->s.modelindex = gi.modelindex ("models/ships/viper/tris.md2");

	VectorCopy(ent->s.angles,viper->s.angles);
	viper->s.angles[2] = 0;
	rs_trace = gi.trace (strpoint,NULL,NULL,tts,ent,  MASK_SHOT);
	tts[0] = cos(f) * (-600) ;
	tts[1] = sin(f) * (-600) ;
	VectorAdd(rs_trace.endpos,tts,tmp);
	VectorCopy(tmp,viper->s.origin);


	viper->velocity[0] = cos(f) * 300; 
	viper->velocity[1] = sin(f) * 300;
	viper->velocity[2] = 0;

	rs_trace = gi.trace (strpoint,NULL,NULL,tte,ent,  MASK_SHOT);
	VectorSubtract(viper->s.origin,rs_trace.endpos,tts);
	f = VectorLength(tts);

	gi.sound (viper, CHAN_AUTO, gi.soundindex("world/flyby1.wav"), 1, ATTN_NONE, 0);

	gi.sound (ent, CHAN_AUTO, gi.soundindex("world/incoming.wav"), 1, ATTN_NONE, 0);

	viper->nextthink = level.time + f *0.75 /300;
	viper->think = AirStrike_Think;
	viper->moveinfo.speed = f;

//	viper->s.sound = gi.soundindex ("weapons/rockfly.wav");

	//	viper->s.effects |= EF_ROTATE | EF_COLOR_SHELL;
//	viper->s.renderfx |= RF_SHELL_BLUE | RF_SHELL_GREEN;
	VectorCopy(strpoint,viper->moveinfo.start_angles);	//strikepoint
	
//	viper->think = Pod_think;
//	viper->nextthink = level.time + FRAMETIME;
	viper->classname = "viper";	
	viper->s.origin[2] += 16;
	gi.linkentity (viper);
}

