#include "../header/bot.h"
#include "../header/shared.h"
#include "../header/player.h"

qboolean	pickup_pri;

int			targetindex;		//debugtarget
char		ClientMessage[MAX_STRING_CHARS];
botinfo_t	Bot[MAXBOTS];
route_t		Route[MAXNODES];
int			CurrentIndex;
int			SpawnWaitingBots;
float		JumpMax = 0;
qboolean	JmpTableChk = false;
float		JumpTable[FALLCHK_LOOPMAX];
int			botskill;
int			trace_priority;
int			skullindex;
int			headindex;
int			mpindex[MPI_INDEX];	//items in map
int			ListedBotCount;
gitem_t		*zflag_item;
edict_t		*zflag_ent;
int			zigflag_spawn;
int			ListedBots;
edict_t*	ExplIndex[MAX_EXPLINDEX];
edict_t*	LaserIndex[MAX_LASERINDEX];
/*-------------------------*/
//pre searched items
gitem_t	*Fdi_GRAPPLE;
gitem_t	*Fdi_BLASTER;
gitem_t *Fdi_SHOTGUN;
gitem_t *Fdi_SUPERSHOTGUN;
gitem_t *Fdi_MACHINEGUN;
gitem_t *Fdi_CHAINGUN;
gitem_t *Fdi_GRENADES;
gitem_t *Fdi_GRENADELAUNCHER;
gitem_t *Fdi_ROCKETLAUNCHER;
gitem_t *Fdi_HYPERBLASTER;
gitem_t *Fdi_RAILGUN;
gitem_t *Fdi_BFG;
gitem_t *Fdi_PHALANX;
gitem_t *Fdi_BOOMER;
gitem_t *Fdi_TRAP;

gitem_t *Fdi_SHELLS;
gitem_t *Fdi_BULLETS;
gitem_t *Fdi_CELLS;
gitem_t *Fdi_ROCKETS;
gitem_t *Fdi_SLUGS;
gitem_t *Fdi_MAGSLUGS;
/*--------------------------*/

/*
FIRE_PRESTAYFIRE
FIRE_STAYFIRE
FIRE_IGNORE
FIRE_REFUGE
FIRE_EXPAVOID
FIRE_QUADUSE
FIRE_AVOIDINV
FIRE_JUMPROC
*/
//combat flag set
int			FFlg[MAX_BOTSKILL]
=
{
//skill 0
	FIRE_PRESTAYFIRE | FIRE_STAYFIRE | FIRE_REFUGE,
//skill 1
	FIRE_REFUGE | FIRE_PRESTAYFIRE | FIRE_STAYFIRE | FIRE_REFUGE,
//skill 2
	FIRE_REFUGE | FIRE_PRESTAYFIRE | FIRE_IGNORE | FIRE_QUADUSE,
//skill 3
	FIRE_REFUGE | FIRE_EXPAVOID | FIRE_IGNORE | FIRE_QUADUSE,
//skill 4
	FIRE_REFUGE | FIRE_EXPAVOID | FIRE_IGNORE | FIRE_QUADUSE,
//skill 5
	FIRE_EXPAVOID | FIRE_IGNORE | FIRE_QUADUSE | FIRE_AVOIDINV | FIRE_IGNORE | FIRE_JUMPROC,
//skill 6
	FIRE_EXPAVOID | FIRE_IGNORE | FIRE_QUADUSE | FIRE_AVOIDINV | FIRE_IGNORE | FIRE_JUMPROC,
//skill 7
	FIRE_EXPAVOID | FIRE_IGNORE | FIRE_QUADUSE | FIRE_AVOIDINV| FIRE_IGNORE | FIRE_JUMPROC,
//skill 8
	FIRE_EXPAVOID | FIRE_IGNORE | FIRE_QUADUSE | FIRE_AVOIDINV| FIRE_IGNORE | FIRE_JUMPROC,
//skill 9
	FIRE_EXPAVOID | FIRE_IGNORE | FIRE_QUADUSE | FIRE_AVOIDINV| FIRE_IGNORE | FIRE_JUMPROC
};

qboolean BotApplyStrength(edict_t *ent)
{
	static gitem_t *tech = NULL;

	if (!tech)
		tech = FindItemByClassname("item_tech2");
	if (tech && ent->client 
		&& ent->client->pers.inventory[ITEM_INDEX(tech)]) 
		return true;

	return false;
}
qboolean BotApplyResistance(edict_t *ent)
{
	static gitem_t *tech = NULL;

	if (!tech)
		tech = FindItemByClassname("item_tech1");
	if (tech && ent->client 
		&& ent->client->pers.inventory[ITEM_INDEX(tech)]) 
		return true;

	return false;
}

//return foundedenemy
int Bot_SearchEnemy (edict_t *ent)
{
	zgcl_t		*zc;		//zc's address
	qboolean	tmpflg;		//temporary	
	edict_t		*target,*trent;
	trace_t		rs_trace;

	int		i,j,k;
	int		foundedenemy;

	float	pitch,yaw;
	float	vr,hr;

	char	*entcln;

	vec3_t	trmin;

	zc = &ent->client->zc;

	vr = (float)Bot[zc->botindex].param[BOP_VRANGE];
	hr = (float)Bot[zc->botindex].param[BOP_HRANGE];	

	if(vr > 180) vr = 180;
	if(hr > 180) hr = 180;
	if(vr < 10) vr = 10;
	if(hr < 10) hr = 10;
	//-------------------------------------------
	//search for enemy
	foundedenemy = 0;
	target = NULL;

	tmpflg = false;	//viewable flag
	if(zc->first_target != NULL){
		if(	Bot_trace(ent,zc->first_target)){
			tmpflg = true;
			foundedenemy++;
		}
	}

//	if(zc->ctfstate == CTFS_SUPPORTER) zc->ctfstate = CTFS_OFFENCER;

	// blue or red?
	if(ctf->value)
	{
		if(ent->client->resp.ctf_team == CTF_TEAM1) k = ITEM_INDEX(FindItem("Blue Flag"));
		else k = ITEM_INDEX(FindItem("Red Flag"));
	}
	else k = ITEM_INDEX(FindItem("ZB Flag"));

	// decide da sorting first or last
	if(random() < 0.5) j = 0;
	else j = -1;

	if(ent->client->pers.inventory[ITEM_INDEX(zflag_item)])
	{
		ent->client->zc.tmplstate = TMS_LEADER;
		ent->client->zc.followmate = NULL;	
	}

	for ( i = 1 ; i <= maxclients->value && target == NULL ; i++)
	{
		if(j){
			entcln = g_edicts[i].classname;
			trent =  &g_edicts[i];
		}
		else{
			entcln = g_edicts[(int)(maxclients->value) - i +1].classname;
			trent =  &g_edicts[(int)(maxclients->value) - i +1];					
		}

		if(!trent->inuse || ent == trent || zc->first_target == trent ||trent->deadflag) continue;

		if( entcln[0] == 'p' && trent->movetype != MOVETYPE_NOCLIP ){
			if(Bot_traceS(ent,trent))
			{
				VectorSubtract (trent->s.origin, ent->s.origin, trmin);
				//not ctf mode and sameteam
				if(!ctf->value && OnSameTeam(ent,trent))
				{
					if(trent->client->zc.first_target)
					{
						if(Bot_traceS(ent,trent->client->zc.first_target)) target = trent->client->zc.first_target;
					}
					if(trmin[2] < JumpMax && VectorLength(trmin) < 400)
					{
						yaw = (float)Bot[ent->client->zc.botindex].param[BOP_TEAMWORK];
						if(trent->client->pers.inventory[ITEM_INDEX(zflag_item)])
						{
							trent->client->zc.tmplstate = TMS_LEADER;
							trent->client->zc.followmate = NULL;
							if((9 * random()) < yaw)
							{
								ent->client->zc.tmplstate = TMS_FOLLOWER;
								ent->client->zc.followmate = trent;
							}
						}
						else 
						{
							if((9 * random()) < yaw)
							{
//gi.bprintf(PRINT_HIGH,"Team stateON\n");
								//相手がリーダー
								if(trent->client->zc.tmplstate == TMS_LEADER)
								{
									trent->client->zc.followmate = NULL;
									if(ent->client->zc.tmplstate == TMS_LEADER)
									{
										if(random() < 0.5)
										{
											ent->client->zc.tmplstate = TMS_FOLLOWER;
											ent->client->zc.followmate = trent;
										}
										else
										{
											ent->client->zc.tmplstate = TMS_LEADER;
											ent->client->zc.followmate = NULL;
											trent->client->zc.tmplstate = TMS_FOLLOWER;
											trent->client->zc.followmate = ent;											
										}
									}
									else
									{
										ent->client->zc.tmplstate = TMS_FOLLOWER;
										ent->client->zc.followmate = trent;									
									}
								}
								else
								{
									if(ent->client->zc.tmplstate == TMS_LEADER
										|| random() < 0.5)
									{
										ent->client->zc.tmplstate = TMS_LEADER;
										ent->client->zc.followmate = NULL;
										trent->client->zc.tmplstate = TMS_FOLLOWER;
										trent->client->zc.followmate = ent;										
									}
									else 
									{
										ent->client->zc.tmplstate = TMS_FOLLOWER;
										ent->client->zc.followmate = trent;
										trent->client->zc.tmplstate = TMS_LEADER;
										trent->client->zc.followmate = NULL;										
									}
								}
							}
						}
					}
				}
				else if(ctf->value && ent->client->resp.ctf_team == trent->client->resp.ctf_team)
				{
					//have flag
					if(trent->client->pers.inventory[k])
					{
						if(ent->client->zc.ctfstate == CTFS_OFFENCER)
						{
							ent->client->zc.ctfstate = CTFS_SUPPORTER;
							ent->client->zc.followmate = trent;
						}
						// if carrier have enemy
						if(trent->client->zc.first_target != NULL)
						{
							if(trent->client->zc.first_target->classname[0] == 'p')	
							{
								target = trent->client->zc.first_target;
							}
						}
						// if carrier tracing route
						if(trent->client->zc.route_trace && (trent->client->zc.routeindex - 2) > CurrentIndex)	
						{
							zc->routeindex = trent->client->zc.routeindex - 2;
						}
//						ent->s.angles[YAW] = Get_yaw (trmin);
//						if(ent->client->zc.ctfstate == CTFS_SUPPORTER ){
//							zc->first_target = NULL;
//							break;
//						}
					}
				}
				else 
				{
					foundedenemy++;
					if(!tmpflg && target == NULL)
					{
						pitch = Get_pitch(trmin);
						pitch = fabs(pitch - ent->s.angles[PITCH]);
						if(pitch > 180) pitch = 360 - pitch;
						//if(ent->client->zc.zcstate & STS_WAITS) target = trent;
						if(pitch <= vr)
						{ 
							yaw = Get_yaw(trmin);
							yaw = fabs(yaw - ent->s.angles[YAW]);
							if(yaw > 180) yaw = 360 - yaw;
							if(yaw <= hr || (ent->client->zc.zcstate & STS_WAITS))	target = trent;
						}
					}
					//
					if(!tmpflg && target == NULL && trent->mynoise && trent->mynoise2)
					{
						if(Bot[ent->client->zc.botindex].param[BOP_NOISECHK])
						{
							if(trent->mynoise->teleport_time >= (level.time - FRAMETIME))
							{
								VectorSubtract (trent->mynoise->s.origin, ent->s.origin, trmin);
								if(VectorLength(trmin) < 300)
								{
									pitch = (float)Bot[ent->client->zc.botindex].param[BOP_REACTION];
									if((9 * random()) < pitch) target = trent;
								}
							}
							if(target == NULL && trent->mynoise2->teleport_time >= (level.time - FRAMETIME))
							{
								VectorSubtract (trent->mynoise->s.origin, ent->s.origin, trmin);
								if(VectorLength(trmin) < 100)
								{
									pitch = (float)Bot[ent->client->zc.botindex].param[BOP_REACTION];
									if((9 * random()) < pitch) target = trent;									
								}								
							}
						}
					}
				}
			}
			//音のみで場所を判断
			else
			{
				if(Bot[ent->client->zc.botindex].param[BOP_NOISECHK]
					&& Bot[ent->client->zc.botindex].param[BOP_ESTIMATE]
					&& !tmpflg && trent->mynoise)
				{

					if(trent->mynoise->teleport_time >= (level.time - FRAMETIME))
					{
						AngleVectors (trent->client->v_angle, trmin, NULL, NULL);
						VectorScale(trmin,200,trmin);
						VectorAdd(trent->s.origin,trmin,trmin);
						rs_trace = gi.trace(trent->s.origin,NULL,NULL,trmin,trent,MASK_SHOT);
					
						VectorSubtract (ent->s.origin, rs_trace.endpos, trmin);
						if(VectorLength(trmin) < 500)
						{
							VectorCopy(rs_trace.endpos,trmin);
							rs_trace = gi.trace(ent->s.origin,NULL,NULL,trmin,ent,MASK_SHOT);
							pitch = (float)Bot[ent->client->zc.botindex].param[BOP_REACTION];
							
							if(rs_trace.fraction == 1.0 && (9 * random()) < pitch)
							{
								target = trent;
								ent->client->zc.battlemode |= FIRE_ESTIMATE;
								VectorCopy(trmin,ent->client->zc.vtemp);
							}
						}
					}
				}
			}
		}
	}
	if(target && !tmpflg) zc->first_target = target;
	else if(target && zc->first_target) 
	{
		if(Get_KindWeapon(target->client->pers.weapon) > 
			Get_KindWeapon(zc->first_target->client->pers.weapon)) zc->first_target = target;
	}
//	ent->client->zc.zcstate &= ~CTS_COMBS;	//clear combat state

	return (foundedenemy);
}

void Bot_SearchItems (edict_t *ent)
{
	zgcl_t		*zc;		//zc's address
	qboolean	wstayf,q;		//weaponflag	
	edict_t		*target,*trent;

	vec3_t		touchmin,touchmax;
	vec3_t		trmin,trmax;

	gitem_t		*item;
	char		*entcln;

	float		x,yaw;			//temporary
		
	int			i,j,k,conts;

	trace_t		rs_trace;

	zc = &ent->client->zc;


	if((zc->tmpcount++) & 1) return;

	j = 0;
	q = false;//rocket jump needed
	//search Items
	if(ctf->value)
	{
		if(ent->moveinfo.state == SUPPORTER) j = -1;
		else if( ent->target_ent != NULL)
		{
			if( ent->moveinfo.state == CARRIER && ent->target_ent->classname[6] == 'F') j = -1;
		}
		else if( ent->client->ctf_grapple != NULL) j = -1;
	}
	trent = zc->second_target;
	if( trent != NULL && !j )
	{
		x = random();
		if( trent->classname[0] == 'w') j = -1;
		else if( trent->classname[0]=='i'){
			if( trent->classname[5]=='q'
				|| trent->classname[5]=='f'
				|| trent->classname[5]=='t'
				|| trent->classname[5]=='i')
				j = -1;
		}
		else if((trent->classname[0] == 'p' && x > 0.2)
			|| ent->client->ctf_grapple != NULL	)
				j= -1;
	}

	if( j == 0 )
	{
		if ((int)(dmflags->value) & DF_WEAPONS_STAY) wstayf = true;
		else wstayf = false;

		target = NULL;
		VectorCopy (ent->absmin, touchmin);
		VectorCopy (ent->absmax, touchmax);
		
		// when ctf
		if(0/*ctf->value*/){
			touchmin[0] -= 500;
			touchmin[1] -= 500;
			touchmin[2] -= 500;
			touchmax[0] += 500;
			touchmax[1] += 500;
			touchmax[2] += 598;
		}
		// when not
		else
		{
			touchmin[0] -= 300;
			touchmin[1] -= 300;
			touchmin[2] -= 300;
			if(ent->waterlevel) touchmin[2] -= 200;
			touchmax[0] += 300;
			touchmax[1] += 300;
			touchmax[2] += 54;// +290);

			if(ent->health > 70 && ent->client->pers.inventory[ITEM_INDEX(Fdi_ROCKETLAUNCHER/*FindItem("Rocket Launcher")*/)])
			{
				i = ITEM_INDEX(Fdi_ROCKETS/*FindItem("Rockets")*/);
		
				if(ent->client->pers.inventory[i] > 0)
				{
					q = true;
					touchmax[2] += 290;
				}
			}
		}


		if((level.framenum - ent->client->resp.enterframe ) % 64 > 32) j = 0;
		else j = -1;

		k = globals.num_edicts + maxclients->value+1;

		for ( i=maxclients->value+1 ; i<globals.num_edicts ; i++)
		{
			if(j)	//normal
			{
				trent = &g_edicts[i];
				entcln = trent->classname;
			}
			else	//reverse
			{
				trent = &g_edicts[k - i];
				entcln = trent->classname;
			}

			if(!trent->inuse) continue;
			if(trent->solid != SOLID_TRIGGER)
			{
				if( ent->client->weaponstate == WEAPON_READY && !ent->client->zc.route_trace)
				{
					if( entcln )
					{
						if(entcln[0] == 'f' /*&& trent->classname[5] == 'b'*/
							&& trent->health //&&trent->moveinfo.state == PSTATE_BOTTOM
							&& trent->takedamage)//trent->moveinfo.wait == 0)
						{
							if(trent->classname[5] == 'b' 
								&& trent->monsterinfo.attack_finished > level.time) continue;

							trmax[0] = (trent->absmin[0] + trent->absmax[0]) / 2;
							trmax[1] = (trent->absmin[1] + trent->absmax[1]) / 2;
							trmax[2] = (trent->absmin[2] + trent->absmax[2]) / 2;							
							rs_trace = gi.trace (ent->s.origin, NULL, NULL, trmax ,ent, MASK_SHOT);

							if(rs_trace.ent == trent)
							{
					//		gi.bprintf(PRINT_HIGH,"kkkkkkk\n");
					//		continue;
								VectorSubtract (trmax, ent->s.origin,trmin);
								item = Fdi_BLASTER;//FindItem("Blaster");
//								ent->client->ammo_index = 0;
								item->use(ent,item);
								//ent->client->ammo_index = 0;
								//ent->client->pers.weapon = FindItem("Blaster");
								//ShowGun(ent);
								ent->s.angles[YAW] = Get_yaw(trmin);
								ent->s.angles[PITCH] = Get_pitch(trmin);
								ent->client->buttons |= BUTTON_ATTACK;
								ent->client->zc.objshot = true;
							}	 
						}
					}
				}
				continue;
			}

			


			if(trent->absmax[0]	< touchmin[0]) continue;
			if(touchmax[0]		< trent->absmin[0])	continue;
			if(trent->absmax[1]	< touchmin[1]) continue;
			if(touchmax[1]		< trent->absmin[1])	continue;
			if(trent->absmax[2]	< touchmin[2]) continue;
			if(touchmax[2]		< trent->absmin[2])	continue;			

			VectorSubtract (trent->s.origin, ent->s.origin, trmin);
			yaw = VectorLength(trmin);

/*			if( trent->classname[0] == 'w' || trent->classname[0] == 'R') x = 0;
			else if(trent->classname[0]=='i')
			{
				if(trent->classname[5]=='q'
					|| trent->classname[5]=='f'
					|| trent->classname[5]=='t'
					|| trent->classname[5]=='i') x = 0;
			}*/
			/*else*/ if( !ctf->value )	
			{
//				if(yaw >300 /*|| (trent->s.origin[2] - ent->s.origin[2] ) < -64*/)	continue;
			}

			if( Bot_trace(ent,trent) )
			{
				if(entcln[0] == 'i')
				{
					if(entcln[5] == 'h')
					{
						if(Q_stricmp (entcln, "item_health") == 0 )
						{
							if( ent->health < 100 && (!pickup_pri || yaw < 96)) target = trent;
						}
						else if(entcln[12] == 's'){ //item_health_small
							if(fabs(trent->s.origin[2] - ent->s.origin[2] + 8) > JumpMax) continue;
							target = trent;
						}
						else if(entcln[12] == 'l') //item_health_large
						{
							if( ent->health < 100 && (!pickup_pri || yaw < 96)) target = trent;
						}
						else if(entcln[12] == 'm') //item_health_mega
							target = trent;
					}
					else if(entcln[5] == 'a' && !pickup_pri)
					{
						if(entcln[11] == 's'){		//item_armor_shard 
							if(fabs(trent->s.origin[2] - ent->s.origin[2] + 8) > JumpMax) continue;
							target = trent; 
						}
						else if(entcln[11] == 'j')	//item_armor_jacket
						{
							if( ent->client->pers.inventory[ITEM_INDEX(FindItem("Jacket Armor"))] < 50 ) target = trent; 
						}
						else if(entcln[11] == 'c')	//item_armor_combat
						{
							if( ent->client->pers.inventory[ITEM_INDEX(FindItem("Combat Armor"))] < 100) target = trent; 
						}
						else if(entcln[11] == 'b')	//item_armor_body
						{
							if( ent->client->pers.inventory[ITEM_INDEX(FindItem("Body Armor"))] < 200) target = trent; 
						}
						else if(entcln[6] == 'd')	//item_adrenaline 
										target = trent; 
						else if(entcln[6] == 'n')	//item_ancient_head
										target = trent; 
					}
					else if(entcln[5] == 'f')
					{
						if( trent->spawnflags & DROPPED_ITEM ) target = trent;
						else if(entcln[10] == 't' && entcln[14] == '1')
						{ 
							if( ent->client->resp.ctf_team == CTF_TEAM2) target = trent;
							else if( ent->client->pers.inventory[ITEM_INDEX(FindItem("Blue Flag"))]) target = trent;
						}
						else if(entcln[10] == 't' && entcln[14] == '2')
						{ 
							if( ent->client->resp.ctf_team == CTF_TEAM1) target = trent; 			
							else if(ent->client->pers.inventory[ITEM_INDEX(FindItem("Red Flag"))]) target = trent;
						}
					}
					else if(entcln[5] == 't')
					{
						if(!BotApplyResistance(ent)) 
							if(	!BotApplyStrength(ent))
								if( !CTFApplyHaste(ent))
									if( !CTFHasRegeneration(ent)) target = trent;								
					}
					else
					{
						if(entcln[5] == 'q')		//item_quad 
							target = trent; 
						else if(entcln[5] == 'p' && entcln[6] == 'a')	//item_pack 
							target = trent; 
						else if(entcln[5] == 'b' && entcln[6] == 'a')	//item_bandolier 
							target = trent; 
						else if(entcln[5] == 'e')	//item_enviro 
							target = trent; 
						else if(entcln[5] == 'b')	//item_breather 
							target = trent; 
						else if(entcln[5] == 's')	//item_silencer 
							target = trent; 
						else if(entcln[5] == 'i')	//item_invulnerability 
							target = trent; 
						else if(entcln[5] == 'p' && entcln[12] == 'h')	//Q_stricmp (entcln, "item_power_shield") == 0) 
							target = trent; 
						else if(entcln[5] == 'p' && entcln[12] == 'c')	//Q_stricmp (entcln, "item_power_screen") == 0) 
							target = trent; 
					}
				}
				else if(entcln[0] == 'a' && !pickup_pri)
				{
					if(entcln[5] == 'b')							//ammo_bullets
					{
						if( ent->client->pers.inventory[ITEM_INDEX(Fdi_BULLETS/*FindItem("Bullets")*/)]
											< ent->client->pers.max_bullets) target = trent;
					}
					else if(entcln[5] == 's' && entcln[6] == 'h')	//ammo_shells
					{
						if(ent->client->pers.inventory[ITEM_INDEX(Fdi_SHELLS/*FindItem("Shells")*/)]
											< ent->client->pers.max_shells) target = trent;
					}
					else if(entcln[5] == 'g')						//ammo_grenades
					{
						if(ent->client->pers.inventory[ITEM_INDEX(Fdi_GRENADES/*FindItem("Grenades")*/)]
											< ent->client->pers.max_grenades) target = trent;
					}
					else if(entcln[5] == 'r')						//ammo_rockets
					{
						if(ent->client->pers.inventory[ITEM_INDEX(Fdi_ROCKETS/*FindItem("Rockets")*/)]
											< ent->client->pers.max_rockets) target = trent;
					}
					else if(entcln[5] == 's' )						//ammo_slugs
					{
						if(ent->client->pers.inventory[ITEM_INDEX(Fdi_SLUGS/*FindItem("Slugs")*/)]
											< ent->client->pers.max_slugs) target = trent;
					}
					else if(entcln[5] == 'c' )						//ammo_cells
					{
						if(ent->client->pers.inventory[ITEM_INDEX(Fdi_CELLS/*FindItem("Cells")*/)]
											< ent->client->pers.max_cells) target = trent;
					}
					else if(entcln[5] == 'm' )						//ammo_magslug
					{
						if(ent->client->pers.inventory[ITEM_INDEX(Fdi_MAGSLUGS/*FindItem("Mag Slug")*/)]
											< ent->client->pers.max_magslug) target = trent;
					}
					else if(entcln[5] == 't' )						//ammo_trap
					{
						if(ent->client->pers.inventory[ITEM_INDEX(Fdi_TRAP/*FindItem("Trap")*/)]
											< ent->client->pers.max_trap) target = trent;
					}
				}
				else if(entcln[0] == 'w')
				{
					if(entcln[7] == 's' && entcln[8] == 'h' )	//weapon_shotgun
					{
						if(!wstayf || (wstayf && !pickup_pri 
							&& (!ent->client->pers.inventory[ITEM_INDEX(Fdi_SHOTGUN/*FindItem("Shotgun")*/)] 
							|| trent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM) ) )) target = trent;
					}
					else if(entcln[7] == 's')					//weapon_supershotgun
					{
						if(!wstayf || (wstayf 
							&& (!ent->client->pers.inventory[ITEM_INDEX(Fdi_SUPERSHOTGUN/*FindItem("Super Shotgun")*/)]
							|| trent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM))) ) target = trent;
					}
					else if(entcln[7] == 'm')					//weapon_machinegun
					{
						if(!wstayf || (wstayf 
							&& (!ent->client->pers.inventory[ITEM_INDEX(Fdi_MACHINEGUN/*FindItem("Machinegun")*/)]
							|| trent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)))) target = trent;
					}
					else if(entcln[7] == 'c')					//weapon_chaingun
					{
						if(!wstayf || (wstayf 
								&& (!ent->client->pers.inventory[ITEM_INDEX(Fdi_CHAINGUN/*FindItem("Chaingun")*/)]
								|| trent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM))) ) target = trent;
					}
					else if(entcln[7] == 'g')					//weapon_grenadelauncher
					{
						if(!wstayf || (wstayf 
						&& (!ent->client->pers.inventory[ITEM_INDEX(Fdi_GRENADELAUNCHER/*FindItem("Grenade Launcher")*/)]
						|| trent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)))) target = trent;
					}
					else if(entcln[7]=='r' && entcln[8] == 'o')	//weapon_rocketlauncher
						{
						if(!wstayf || (wstayf 
							&& (!ent->client->pers.inventory[ITEM_INDEX(Fdi_ROCKETLAUNCHER/*FindItem("Rocket Launcher")*/)]
							|| trent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)))) target = trent;
					}
					else if(entcln[7] == 'h')					//weapon_hyperblaster
					{
						if(!wstayf || (wstayf 
							&& (!ent->client->pers.inventory[ITEM_INDEX(Fdi_HYPERBLASTER/*FindItem("HyperBlaster")*/)]
							|| trent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)))) target = trent;
					}
					else if(entcln[7] == 'r')					//weapon_railgun
					{
						if(!wstayf || (wstayf 
							&& (!ent->client->pers.inventory[ITEM_INDEX(Fdi_RAILGUN/*FindItem("Railgun")*/)]
							|| trent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)))) target = trent;	
					}
					else if(entcln[7] == 'b' && entcln[8] == 'o')//weapon_boomer
					{
						if(!wstayf || (wstayf 
							&& (!ent->client->pers.inventory[ITEM_INDEX(Fdi_BOOMER/*FindItem("Ionripper")*/)]
							|| trent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)))) target = trent;					
					}
					else if(entcln[7] == 'p')					//weapon_phalanx
					{
						if(!wstayf || (wstayf 
							&& (!ent->client->pers.inventory[ITEM_INDEX(Fdi_PHALANX/*FindItem("Phalanx")*/)]
							|| trent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)))) target = trent;	
					}
					else if(entcln[7] == 'b')					//weapon_bfg
					{
						if(!wstayf || (wstayf 
							&& (!ent->client->pers.inventory[ITEM_INDEX(Fdi_BFG/*FindItem("BFG10K")*/)]
							|| trent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)))) target = trent;					
					}
				}
				else if(entcln[0] == 'R' && !ent->client->zc.route_trace)
				{
					if(entcln[2] == 'n')
					{
						if(ctf->value)
						{
							if(ent->moveinfo.state == CARRIER)
							{
								if(random() < 0.05) target = trent;
							}
							else if(ent->moveinfo.state == DEFENDER)
							{
								if(random() < 0.01) target = trent;
							}
							else if(random() < 0.02) target = trent;
						}
						else if(random() < 0.4)	target = trent;
					}
					else if(ent->client->zc.route_trace)
					{
						if(ent->client->zc.routeindex < CurrentIndex)
						{
							if(entcln[6] == 'X' && Route[ent->client->zc.routeindex].state == GRS_ONTRAIN) target = trent;
							else if(entcln[6] == '2' && Route[ent->client->zc.routeindex].state == GRS_PUSHBUTTON
								&& Route[ent->client->zc.routeindex].ent == trent) target = trent;
							else target = NULL;
						}
					}
					else if(entcln[6] == '3') target = NULL;
				}
			}
			// founded!
/*			if( target != NULL && !ctf->value)
			{
				if(!ent->waterlevel)
				{
					VectorSubtract(target->s.origin,ent->s.origin,touchmax);
					x = touchmax[2];
					touchmax[2] = 0;
					iyaw = VectorLength(touchmax);
					if( x < -39 ) 
					{
						yaw = iyaw/(-x);*/
//						if( /*yaw < 0.5 &&*/ yaw > 2.5 /*&& iyaw > 64*/) target = NULL;
/*						if(target != NULL)
						{
							if((target->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)) && x < -64) target = NULL;
						}
					}
					if(target != NULL && !ent->waterlevel && x < -60)
					{
						if( target->classname[0] == 'w'
							|| (target->classname[0]=='i' && target->classname[5]=='q')
							|| (target->classname[0]=='i' && target->classname[5]=='f')
							|| (target->classname[0]=='i' && target->classname[5]=='t')
							|| (target->classname[0]=='i' && target->classname[5]=='i')) 
						{
							if(iyaw < 64 ) target = NULL;
						}
						else target = NULL;
					}
				}
			}
*/
			if(target != NULL)
			{
				conts = gi.pointcontents (target->s.origin);
				if(conts & CONTENTS_LAVA) target = NULL;
				else if((conts & CONTENTS_SLIME) && ent->client->enviro_framenum <= level.framenum) target = NULL;
			}
			if(target != NULL && !ctf->value )
			{
				if((target->s.origin[2] - ent->s.origin[2]) > 32  && !q)
				{
					x = target->moveinfo.start_origin[2] - ent->s.origin[2];
					if(x > 54 || x < -24) target = NULL;
					else 
					{
						x = target->s.origin[2] - target->moveinfo.start_origin[2];
						if(x > 54 || x < 0) target = NULL;
					}
				}
				else if((target->s.origin[2] - ent->s.origin[2]) <= 100  && q)
				{
					target = NULL;				
				}
			}

			if(target != NULL )
			{
				if(	zc->second_target == NULL )
				{
					zc->second_target = target;
					break;
				}
				else if(zc->second_target->classname[6] == 'F' &&
					zc->second_target->classname[6] == 'F')
				{
					target = NULL;
					continue;
				}
				else
				{
					if( (target->classname[0] == 'R' && target->classname[0] != 'F')
						|| target->classname[0] == 'w'
						|| (target->classname[0]=='i' && target->classname[5]=='q')
						|| (target->classname[0]=='i' && target->classname[5]=='t')
						|| (target->classname[0]=='i' && target->classname[5]=='f')
						|| (target->classname[0]=='i' && target->classname[5]=='i')) 
					{
						VectorSubtract (ent->s.origin, target->s.origin, trmin);
						if(ctf->value 
							&& zc->second_target->classname[6] == 'F'
							&& ent->moveinfo.state != CARRIER)
						{
							zc->second_target = target;
							break;
						}
						else if( VectorLength(trmin) > 24 )
						{
							zc->second_target = target;
							break;
						}
					}
				}
			}
			target = NULL;
		}
	}
}

//-----------------------------------------------------------------------------------------
//バクハツ物回避
//Avoid explotion		
//
#define EXPLO_BOXSIZE	64
qboolean Bot_ExploAvoid(edict_t *ent,vec3_t	v)
{
	int	i;

	vec3_t	absmax,absmin;
	vec3_t	msmax,msmin;

	if(ent->groundentity == NULL && !ent->waterlevel) return true;
	if(!(FFlg[Bot[ent->client->zc.botindex].param[BOP_COMBATSKILL]] & FIRE_EXPAVOID)) return true;
	
	VectorCopy(v,msmax);
	VectorCopy(v,msmin);
	msmax[0] += ent->maxs[0]; msmax[1] += ent->maxs[1]; msmax[2] += ent->maxs[2];
	msmin[0] += ent->mins[0]; msmin[1] += ent->mins[1]; msmin[2] += ent->mins[2];

	for(i = 0;i < MAX_EXPLINDEX;i++)
	{
		if(ExplIndex[i] != NULL) {if(ExplIndex[i]->inuse == false) ExplIndex[i] = NULL;}
		if(ExplIndex[i] != NULL) 
		{
			VectorCopy(ExplIndex[i]->s.origin,absmax);
			VectorCopy(absmax,absmin);
			absmax[0] += EXPLO_BOXSIZE; absmax[1] += EXPLO_BOXSIZE; absmax[2] += EXPLO_BOXSIZE;
			absmin[0] -= EXPLO_BOXSIZE; absmin[1] -= EXPLO_BOXSIZE; absmin[2] -= EXPLO_BOXSIZE;

			if(absmax[0]	< msmin[0]) continue;
			if(msmax[0]	< absmin[0])	continue;
			if(absmax[1]	< msmin[1]) continue;
			if(msmax[1]	< absmin[1])	continue;
			if(absmax[2]	< msmin[2]) continue;
			if(msmax[2]	< absmin[2])	continue;

			return false;
		}
	}
	return true;
}

//レーザーのチェック
qboolean CheckLaser(vec3_t pos,vec3_t maxs,vec3_t mins)
{
	int	i;
	vec3_t v,end,absmax,absmin;
	float L1,L2,L3;

	VectorAdd(pos,maxs,absmax);
	VectorAdd(pos,mins,absmin);

	for(i = 0;i < MAX_LASERINDEX;i++)
	{
		if(LaserIndex[i] == NULL) return false;
		if (!(LaserIndex[i]->spawnflags & 1)) continue;

		VectorSubtract(pos,LaserIndex[i]->s.origin,v);
		L1 = VectorLength(v);
		VectorSubtract(pos,LaserIndex[i]->moveinfo.end_origin,v);
		L2 = VectorLength(v);

		VectorSubtract(LaserIndex[i]->s.origin,LaserIndex[i]->moveinfo.end_origin,v);
		L3 = VectorLength(v);

//		VectorCopy (LaserIndex[i]->s.origin, start);
		VectorMA (LaserIndex[i]->s.origin, L3 * L1 / (L1 + L2), LaserIndex[i]->movedir, end);

		VectorSubtract(pos,end,v);
		L3 = VectorLength(v);
		
		if(L3 > L1 || L3 > L2) continue;

//gi.bprintf(PRINT_HIGH,"Length %f!\n",L3);

		if(end[0]	< absmin[0]) continue;
		if(absmax[0]	< end[0])	continue;
		if(end[1]	< absmin[1]) continue;
		if(absmax[1]	< end[1])	continue;
		if(end[2]	< absmin[2]) continue;
		if(absmax[2]	< end[2])	continue;
//gi.bprintf(PRINT_HIGH,"Laser Checked! %f\n",L3);
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------------------
// BOT移動可能判定new
// bot move test
//  return	false	can't 
//			true	stand
//			2		duck	
int Bot_moveT ( edict_t *ent,float ryaw,vec3_t pos,float dist,float *bottom)
{
	float		i,yaw;
	vec3_t		trstart,trend;
	vec3_t		trmin,trmax,v,vv;
	trace_t		rs_trace;
	float		tracelimit;
	qboolean	moveok;
	int			contents;

	int			tcontents;



	tcontents =/* MASK_BOTSOLID*/MASK_BOTSOLIDX;//MASK_PLAYERSOLID /*| CONTENTS_TRANSLUCENT*/;  //レーザーには触らない
//	if(!ent->waterlevel) tcontents |= CONTENTS_WATER;

	if(/*ent->client->zc.waterstate == WAS_FLOAT*/ent->waterlevel >= 1/*2*/) tracelimit = 75;//75;//61;
	else if(ent->client->ps.pmove.pm_flags & PMF_DUCKED) tracelimit = 26;
	else tracelimit = JumpMax + 5;//61;

	VectorSet (trmin,-16,-16,-24);
	VectorSet (trmax,16,16,3);

	if(ent->client->zc.route_trace) VectorSet (vv,16,16,0);
	else VectorSet (vv,16,16,3);

	if(0/*!(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
		&& (ent->client->zc.n_duckedtime < FRAMETIME * 10 && !ent->client->zc.route_trace)*/) trmax[2] = 31;
//	else if(ent->waterlevel && !ent->groundentity) trmax[2] = 32;
	else if(ent->client->zc.route_trace
		&& !(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
		&& ent->waterlevel < 2)
	{
		Get_RouteOrigin(ent->client->zc.routeindex,v);
		if((v[2] - ent->s.origin[2]) > 20) trmax[2] = 31;
	}

	//移動先がどうなっているのか調べる
	yaw = ryaw*M_PI*2 / 360;
	trend[0] = cos(yaw) * dist ;				//start
	trend[1] = sin(yaw) * dist ;
	trend[2] = 0;
	VectorAdd (trend, ent->s.origin, trstart);

	VectorCopy(trstart,trend);
	trend[2] += 1;
	rs_trace = gi.trace (trstart, trmin, trmax, trend,ent, tcontents);
	
	trmax[2] += 1;
	if(rs_trace.allsolid || rs_trace.startsolid || rs_trace.fraction != 1.0)	//前には進めない場合
	{
		moveok = false;
		VectorCopy (trstart, trend);

		for( i = 4 ; i < (tracelimit + 4) ; i += 4 )
		{	
			trstart[2] = ent->s.origin[2] + i;
			rs_trace = gi.trace (trstart, trmin, vv/*trmax*/, trend,ent, tcontents );
//			rs_trace = gi.trace (trstart, trmin, trmax, trstart,ent, tcontents );
			if(!rs_trace.allsolid && !rs_trace.startsolid && rs_trace.fraction > 0)
			{
				moveok = true;
				break;
			}
		}
		if(!moveok/*i >= tracelimit+4*/)
//		if(i >= tracelimit - 4)
		{
//gi.bprintf(PRINT_HIGH,"apooX %f >= %f\n",i ,tracelimit);
//if(ent->client->ps.pmove.pm_flags & PMF_DUCKED) gi.bprintf(PRINT_HIGH,"apoo1 %f %f \n",i,trmax[2]);
			return false;
		}

//		rs_trace = gi.trace (trstart, trmin, trmax, trend,ent, tcontents );
		*bottom = rs_trace.endpos[2] - ent->s.origin[2];

		if(!ent->client->zc.route_trace) 
		{
//gi.bprintf(PRINT_HIGH,"apoo2\n");
//if(ent->client->zc.waterstate == 1 && ryaw == ent->client->zc.moveyaw) gi.bprintf(PRINT_HIGH,"apoo2\n");
			if(rs_trace.plane.normal[2] < 0.7 && (!ent->client->zc.waterstate && ent->groundentity)) return false;
		}
		else 
		{
			Get_RouteOrigin(ent->client->zc.routeindex,v);
			if(rs_trace.plane.normal[2] < 0.7 && v[2] < ent->s.origin[2]) return false;
		}

		if( *bottom >/*=*/ tracelimit - 5)
		{
//gi.bprintf(PRINT_HIGH,"apooY %f > %f\n",*bottom ,tracelimit - 5);
//gi.bprintf(PRINT_HIGH,"apoo3\n");
//if(ent->client->zc.waterstate == 1 && ryaw == ent->client->zc.moveyaw) gi.bprintf(PRINT_HIGH,"apoo3\n");
			return false;
		}
		pos[0] = rs_trace.endpos[0];
		pos[1] = rs_trace.endpos[1];
		pos[2] = rs_trace.endpos[2];

		if(trmax[2] == 32)
		{
			if(Bot_ExploAvoid(ent,pos))
			{
				if(!CheckLaser(pos,trmax,trmin))
				return true;
			}
			return false;
		}
		
//		trmax[2] = 32;
		VectorCopy(pos,trend);
		trend[2] += 28;
		rs_trace = gi.trace (pos, trmin, trmax, trend,ent, tcontents );
		if(!rs_trace.allsolid && !rs_trace.startsolid && rs_trace.fraction == 1.0)
		{
			if(Bot_ExploAvoid(ent,pos))
			{
				if(!CheckLaser(pos,trmax,trmin))
				return true;
			}
			return false;
		}
		if(Bot_ExploAvoid(ent,pos))
		{
			if(!CheckLaser(pos,trmax,trmin))
			return 2;
		}
		return false;


/*		trmax[2] = 32;
		rs_trace = gi.trace (pos, trmin, trmax, pos,ent, tcontents );
		if(!rs_trace.allsolid && !rs_trace.startsolid)	return true;
		return 2;*/
	} 
	else								//進めたとしても落ちたくない時のためのチェック
	{
		pos[0] = trstart[0];
		pos[1] = trstart[1];
		pos[2] = trstart[2];
		VectorCopy (trstart, trend);

		trstart[2] = trend[2] -8190;
		rs_trace = gi.trace (trend, trmin, trmax, trstart,ent, tcontents | MASK_OPAQUE);

		*bottom = rs_trace.endpos[2] - ent->s.origin[2];

		if(0/*rs_trace.fraction != 1.0 && rs_trace.plane.normal[2] < 0.7 && ent->waterlevel < 2 && ent->groundentity*/)
		{
			i = Get_vec_yaw (rs_trace.plane.normal,ryaw);
			if( i < 90)
			{
				if(*bottom < 0 ) *bottom *= 3.0;
			}
			else 
			{
//if(ent->client->ps.pmove.pm_flags & PMF_DUCKED) gi.bprintf(PRINT_HIGH,"apoo2\n");
//if(ent->client->zc.waterstate == WAS_FLOAT && ryaw == ent->client->zc.moveyaw) gi.bprintf(PRINT_HIGH,"apooX\n");
				return false;
			}
		}
		if(0/*!ent->client->zc.route_trace*/)
		{
			if(rs_trace.plane.normal[2] > 0 && rs_trace.plane.normal[2] < 0.7) *bottom /= rs_trace.plane.normal[2];
		}
/*		else
		{
			
		}*/

		contents = 0;
		if(!ent->waterlevel)
		{
			if (ent->client->enviro_framenum > level.framenum) contents = CONTENTS_LAVA;
			else contents = ( CONTENTS_LAVA | CONTENTS_SLIME);
		}
		if( rs_trace.contents & contents ) *bottom = -9999; /*return false;*/
		else if( rs_trace.surface->flags & SURF_SKY ) *bottom = -9999;		

		if(!ent->waterlevel && (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			&& ent->groundentity == NULL && ent->velocity[2] > 10 && trmax[2] == 4) return 2;


		if(trmax[2] == 32)
		{
			if(Bot_ExploAvoid(ent,pos))
			{
				if(!CheckLaser(pos,trmax,trmin))
				return true;
			}
			return false;
		}
		
//		trmax[2] = 32;
		VectorCopy(pos,trend);
		trend[2] += 28;
		rs_trace = gi.trace (pos, trmin, trmax, trend,ent, tcontents );
		if(!rs_trace.allsolid && !rs_trace.startsolid && rs_trace.fraction == 1.0)
		{
			if(Bot_ExploAvoid(ent,pos))
			{
				if(!CheckLaser(pos,trmax,trmin))
				return true;
			}
			return false;
		}
		if(Bot_ExploAvoid(ent,pos))
		{
			if(!CheckLaser(pos,trmax,trmin))
			return 2;
		}
		return false;
	}
}

int Bot_Watermove ( edict_t *ent,vec3_t pos,float dist,float upd)
{
	trace_t		rs_trace;
	vec3_t		trmin,trmax,touchmin;
	float		i,j;

	float		vec;

	VectorCopy(ent->s.origin,trmax);

	trmax[2] += upd;

	rs_trace = gi.trace (ent->s.origin, ent->mins, ent->maxs, trmax,ent, /*MASK_BOTSOLID*/MASK_BOTSOLIDX);

	if(!rs_trace.allsolid && !rs_trace.startsolid )
	{
		if(rs_trace.fraction > 0)
		{
			VectorCopy(rs_trace.endpos,pos);
			return true;
			if(upd < 0) ent->velocity[2] = 0;
		}
	}
//gi.bprintf(PRINT_HIGH,"Water MOVE NG %f %f!\n",dist,upd);
//	return false;
//	if(upd > -7 && upd < 7)	return false;

	VectorCopy(ent->s.origin,trmin);
	trmin[2] += upd;
	
	vec = -1;
	for(i = 0;i < 360; i += 10)
	{
		if(i && upd > -13 && upd < 0/*13*/) break;
		if(i > 60 && i < 300) continue;

		j = ent->client->zc.moveyaw + i;
		
		if(j > 180) j = j - 360;
		else if(j < -180) j = j + 360;
		else j = i;

		touchmin[0] = cos(j) * 24 ;
		touchmin[1] = sin(j) * 24 ;
		touchmin[2] = 0;

		VectorAdd(trmin,touchmin,trmax);
		rs_trace = gi.trace (trmax/*ent->s.origin*/, ent->mins, ent->maxs, trmin,ent, MASK_BOTSOLIDX);

//		yaw = VectorLength(trmax); 
		if(!rs_trace.allsolid && !rs_trace.startsolid )
		{
			VectorAdd(rs_trace.endpos,touchmin,trmax);
			rs_trace = gi.trace (trmax, ent->mins, ent->maxs, trmax,ent, MASK_BOTSOLIDX);
//gi.bprintf(PRINT_HIGH,"NGAAAAAAAAAAAAAAAAAAAAAAAAAAA!\n");

//			VectorSubtract(rs_trace.endpos,ent->s.origin,trmax);
			if(!rs_trace.allsolid && !rs_trace.startsolid )
			{
//gi.bprintf(PRINT_HIGH,"go go go!\n");
				vec = i;break;
			}
		}
	}

	if(vec == -1)
	{
//gi.bprintf(PRINT_HIGH,"Water MOVE NG %f %f!\n",dist,upd);
		return false;
	}

//gi.bprintf(PRINT_HIGH,"Water MOVE OK %f %f!\n",dist,upd);
	VectorCopy(trmax,pos);
	if(upd < 0) ent->velocity[2] = 0;
	return true;
	
	touchmin[0] = cos(vec) * 16;//dist ;
	touchmin[1] = sin(vec) * 16;//dist ;
	touchmin[2] = 0;

	VectorAdd(ent->s.origin,touchmin,trmin);
	VectorCopy(trmin,trmax);
	trmax[2] += upd;
	rs_trace = gi.trace (trmin, ent->mins, ent->maxs, trmax,ent, MASK_BOTSOLIDX);	

	if(rs_trace.allsolid || rs_trace.startsolid )
	{
		return false;
	}

	VectorCopy(rs_trace.endpos,pos);
	return true;


	VectorCopy(rs_trace.plane.normal,trmin);
	trmin[2] = 0;
	VectorNormalize(trmin);
	VectorAdd(trmax,trmin,trmax);
	for(i = 1.0;i < dist;i += 1.0)
	{
		rs_trace = gi.trace (trmax, ent->mins, ent->maxs,trmax,ent, MASK_BOTSOLIDX/*MASK_PLAYERSOLID*/);
		if(!rs_trace.allsolid && !rs_trace.startsolid)
		{
			VectorCopy(trmax,pos);
			return true;				
		}
		VectorAdd(trmax,trmin,trmax);
	}
//	gi.bprintf(PRINT_HIGH,"failed2\n");
	return false;
}

int Bot_moveW ( edict_t *ent,float ryaw,vec3_t pos,float dist,float *bottom)
{
	float		yaw;
	vec3_t		trstart,trend;
	trace_t		rs_trace;

	int			contents;

	int			tcontents;

	if (ent->client->enviro_framenum > level.framenum) contents = CONTENTS_LAVA;
	else contents = ( CONTENTS_LAVA | CONTENTS_SLIME);

	tcontents = MASK_BOTSOLIDX/*MASK_PLAYERSOLID*/;
	tcontents |= CONTENTS_WATER;

	//移動先がどうなっているのか調べる
	yaw = ryaw*M_PI*2 / 360;
	trend[0] = cos(yaw) * dist ;				//start
	trend[1] = sin(yaw) * dist ;
	trend[2] = 0;
	VectorAdd (trend, ent->s.origin, trstart);

	pos[0] = trstart[0];
	pos[1] = trstart[1];
	pos[2] = trstart[2];
	VectorCopy (trstart, trend);

	trstart[2] = trend[2] -8190;//95;
	rs_trace = gi.trace (trend, ent->mins, ent->maxs, trstart,ent, tcontents);

	if((trend[2] - rs_trace.endpos[2]) >= 95) return false;

	if(rs_trace.contents & contents) return false;
	if(!(rs_trace.contents & CONTENTS_WATER)) return false;

	*bottom = rs_trace.endpos[2] - ent->s.origin[2];
	return true;
}

//-----------------------------------------------------------------------------------------
// Bank check
//	true	safe
//	false	danger
qboolean BankCheck(edict_t *ent,vec3_t pos)
{
	trace_t	rs_trace;
	vec3_t	end,v1,v2;

	VectorSet(v1,-16,-16,-24);
	VectorSet(v2,16,16,16);

	VectorCopy(pos,end);

	end[2] -= 5000;

	rs_trace = gi.trace (pos, v1, v2,end,ent, MASK_BOTSOLIDX/*MASK_PLAYERSOLID*/);

	if(rs_trace.startsolid || rs_trace.allsolid) return false;
	if(rs_trace.plane.normal[2] < 0.8 ) return false;
	return true;
}


//-----------------------------------------------------------------------------------------
// hazard check
//	true	safe
//	false	danger
qboolean HazardCheck(edict_t *ent,vec3_t pos)
{
	trace_t	rs_trace;
	vec3_t	end,v1,v2;
	int		contents;

	VectorSet(v1,-16,-16,-16);
	VectorSet(v2,16,16,16);

	VectorCopy(pos,end);

	end[2] -= 8190;

	if (ent->client->enviro_framenum > level.framenum) contents = CONTENTS_LAVA;
	else contents = ( CONTENTS_LAVA | CONTENTS_SLIME);

	rs_trace = gi.trace (pos, v1, v2,end,ent, MASK_OPAQUE);

	if(rs_trace.contents & contents) return false;
	return true;
}

//-----------------------------------------------------------------------------------------
// bot's shot
/*qboolean Bot_Shot()
{


	if( ent->client->weaponstate == WEAPON_READY)
	{
		if(trent->movetype == MOVETYPE_STOP && trent->classname)
		{
			&& trent->health && trent->moveinfo.state == 1
				&& trent->takedamage)//trent->moveinfo.wait == 0)
			{
				trmax[0] = (trent->absmin[0] + trent->absmax[0]) / 2;
				trmax[1] = (trent->absmin[1] + trent->absmax[1]) / 2;
				trmax[2] = (trent->absmin[2] + trent->absmax[2]) / 2;							
				rs_trace = gi.trace (ent->s.origin, NULL, NULL, trmax ,ent, MASK_SHOT);

				if(rs_trace.ent == trent)
							{
					//		gi.bprintf(PRINT_HIGH,"kkkkkkk\n");
					//		continue;
								VectorSubtract (trmax, ent->s.origin,trmin);
								ent->client->ammo_index = 0;
								ent->client->pers.weapon = FindItem("Blaster");
								ShowGun(ent);
								ent->s.angles[YAW] = Get_yaw(trmin);
								ent->s.angles[PITCH] = Get_pitch(trmin);
								ent->client->buttons |= BUTTON_ATTACK;
								ent->client->zc.objshot = true;
							}	 
						}
					}
				}

*/
//-----------------------------------------------------------------------------------------
// set the bot's combatstate

void Set_Combatstate(edict_t *ent,int foundedenemy)
{
	vec3_t	v;
	gclient_t	*client;
	float	distance;
	edict_t	*target;
	int		enewep;
	int		combskill;
	float	aim;

	client = ent->client;

	target = client->zc.first_target;

	if(client->zc.zcstate & STS_LADDERUP) return;

	if(target == NULL)
	{
		client->zc.zccmbstt &= ~CTS_COMBS; // clear status
		return;
	}

	//target is dead
	if(!target->inuse || target->deadflag || target->solid != SOLID_BBOX)
	{
		client->zc.battleduckcnt = 0;
		client->zc.first_target = NULL;
		client->zc.zccmbstt &= ~CTS_COMBS; //clear status

		if((9 * random()) < Bot[client->zc.botindex].param[BOP_COMBATSKILL])
															UsePrimaryWeapon(ent);
		return;
	}

	if(!Bot_trace(ent,target))
	{
		if(client->zc.targetlock <= level.time)
		{
			client->zc.first_target = NULL;
			return;
		}
		client->zc.zccmbstt |= CTS_ENEM_NSEE;//can't see
//		return;
	}
	else
	{
		ent->client->zc.targetlock = level.time + FRAMETIME * 12;
		ent->client->zc.zccmbstt &= ~CTS_ENEM_NSEE;//can see
		ent->client->zc.battlemode &= ~FIRE_ESTIMATE;
	}

	VectorSubtract(target->s.origin,ent->s.origin,v);
	distance = VectorLength(v);

	//enemy's weapon
	enewep = Get_KindWeapon(target->client->pers.weapon);

	//status set
	aim = 10.0 - (float)Bot[client->zc.botindex].param[BOP_AIM];
	if(aim <= 0 || aim > 10) aim = 5; 
	combskill = (int)Bot[client->zc.botindex].param[BOP_COMBATSKILL];
	if(combskill < 0 || combskill > 9) combskill = 5;


	if(!(client->zc.zccmbstt & CTS_ENEM_NSEE)) Combat_Level0(ent,foundedenemy,enewep,aim,distance,combskill);
	else if(client->zc.zccmbstt & FIRE_REFUGE) Combat_Level0(ent,foundedenemy,enewep,aim,distance,combskill);
	else Combat_LevelX(ent,foundedenemy,enewep,aim,distance,combskill);

	if(client->zc.first_target)
	{
		client->zc.last_target = client->zc.first_target;
		VectorCopy(client->zc.first_target->s.origin,client->zc.last_pos);
	}
	return;
}

//-----------------------------------------------------------------------------------------
// Bot Jump
// return true		sequaense done
// return false		failed
qboolean Get_FlyingSpeed(float bottom,float block,float dist,float *speed)
{
	float tdist;

	if(bottom >= 40)
	{
		if(block > 4 ) return false;
		tdist = (dist * block) / 4 ;
	}
	else if(bottom >= 35)
	{
		if(block > 5) return false;
		tdist = (dist * block) / 5 ;
	}
	else if(bottom >= 30)
	{
		if(block > 6) return false;
		tdist = (dist * block) / 6 ;
	}
	else if(bottom >= 20)
	{
		if(block > 7) return false;
		tdist = (dist * block) / 7 ;
	}
	else if(bottom >= -5)
	{
		if(block > 8 ) return false;
		tdist = (dist * block) / 8 ;
	}
	else if(bottom >= -20)
	{
		if(block > 9) return false;
		tdist = (dist * block) / 7;
	}
	else if(bottom >= -35)
	{
		if(block > 10) return false;
		tdist = (dist * block) / 6 ;
	}
	else if(bottom >= -52)
	{
		if(block > 11) return false;
		tdist = (dist * block) / 5;
	}
	else if(bottom >= -75)
	{
		if(block > 12) return false;
		tdist = (dist * block) / 4;
	}
	else if(bottom >= -95)
	{
		if(block > 13) return false;
		tdist = (dist * block) / 3;
	}
	else if(bottom >= - 125)
	{
		if(block > 14) return false;
		tdist = (dist * block) / 2;
	}
	else
	{
		if(block > 15) return false;
		tdist = (dist * block) / 2;
	}

	*speed = tdist / MOVE_SPD_RUN;
	return true;
}

qboolean Bot_Jump(edict_t *ent,vec3_t pos,float dist)
{
	float	x,yaw,tdist,bottom,speed;
	vec3_t	temppos;
	zgcl_t	*zc;

	zc = &ent->client->zc;

	yaw = zc->moveyaw;

	Bot_moveT (ent,yaw,temppos,dist,&bottom);
	if(bottom > -JumpMax) return false; 

	for( x = 2 ; x <= 16; x += 1)
	{
		tdist = dist * x;
		if( Bot_moveT (ent,yaw,temppos,tdist,&bottom) == true) 
		{
			if( x == 2 && ( bottom > - JumpMax ) && bottom <= 0)
			{
				VectorCopy( pos,ent->s.origin);
				return true;
			}
			if( bottom <= JumpMax && bottom > -JumpMax)
			{
				if(Get_FlyingSpeed(bottom,x,dist,&speed))
				{
					speed *= 1.5;
					if(speed > 1.2) speed = 1.2;
					ent->moveinfo.speed = speed;
					ent->velocity[2] += VEL_BOT_JUMP;
					gi.sound(ent, CHAN_VOICE, gi.soundindex("*jump1.wav"), 1, ATTN_NORM, 0);
					PlayerNoise(ent, ent->s.origin, PNOISE_SELF);	//pon
					Set_BotAnim(ent,ANIM_JUMP,FRAME_jump1-1,FRAME_jump6);
//					ent->s.frame = FRAME_jump1-1;
//					ent->client->anim_end = FRAME_jump6;
//					ent->client->anim_priority = ANIM_JUMP;
					return true;					
				}
			}
			continue;
		}
		else return false;
	}
	return false;
}

//-----------------------------------------------------------------------------------------
// Bot Fall
// return true		sequaense done
// return false		failed
	
qboolean Bot_Fall(edict_t *ent,vec3_t pos,float dist)
{
	zgcl_t	*zc;
	float	x,l,speed,grav,vel,ypos,yori;
	vec3_t	v,vv;
	int		mf = false;
	short	mode = 0;

	zc = &ent->client->zc;

	if(zc->second_target != NULL)// && !(zc.zcstate & STS_COMBS))
	{
		mode = 1;
		ypos = zc->second_target->s.origin[2];

		//if on hazard object cause error
		if(!HazardCheck(ent,zc->second_target->s.origin))
		{
			zc->second_target = NULL;
			return false;
		}		

		yori = ent->s.origin[2];
		VectorSubtract(zc->second_target->s.origin,pos,v);

		grav = ent->gravity * sv_gravity->value * FRAMETIME; 
		if(v[2] > 0) goto JMPCHK;

		vel = ent->velocity[2];
//		grav = ent->gravity * sv_gravity->value * FRAMETIME; 
		l = 1.0;
		for(x = 1;x <= FALLCHK_LOOPMAX;++x ,l += x )
		{
			vel -= grav;// * l;
			yori += vel * FRAMETIME;
			if(ypos >= yori)
			{
				mf = true;
				break;
			}
		}
		VectorCopy(v,vv);
		vv[2] = 0;
		l = VectorLength(vv);
		speed = l / x;
		if(speed <= MOVE_SPD_RUN && mf)
		{
			ent->moveinfo.speed = speed / MOVE_SPD_RUN;
			VectorCopy(pos,ent->s.origin);
			return true;
		}
		goto JUMPCATCH;
	}
	else if(zc->route_trace)
	{
//gi.bprintf(PRINT_HIGH,"fall\n");
		mode = 2;
		Get_RouteOrigin(zc->routeindex,vv);
		ypos = vv[2];

		//if on hazard object cause error
		if(!HazardCheck(ent,vv))
		{
			if(++zc->routeindex >= CurrentIndex) zc->routeindex = 0;

//gi.bprintf(PRINT_HIGH,"OFF 1\n"); //ppx
//gi.bprintf(PRINT_HIGH,"hazard out\n");
//			zc->route_trace = false;
			return false;
		}		
		
		yori = pos[2];
		VectorSubtract(vv,pos,v);

		grav = ent->gravity * sv_gravity->value * FRAMETIME; 	
		if(v[2] >= 0/*-8*/) goto JUMPCATCH;//JMPCHK;

		vel = ent->velocity[2];
		//grav = ent->gravity * sv_gravity->value * FRAMETIME; 
		l = 1.0;
		for(x = 1;x <= FALLCHK_LOOPMAX;++x ,l += x )
		{
			vel -= grav;// * l;
			yori += vel * FRAMETIME;
			if(ypos >= yori)
			{
				mf = true;
				break;
			}
		}

		VectorCopy(v,vv);
		vv[2] = 0;
		//vel考慮の落下
		if(Route[zc->routeindex].state == GRS_ONTRAIN)
		{
			if(1/*Route[zc->routeindex].ent->trainteam == NULL*/)
			{
				vv[0] += FRAMETIME * Route[zc->routeindex].ent->velocity[0] * x;
				vv[1] += FRAMETIME * Route[zc->routeindex].ent->velocity[1] * x;
			}
		}

		l = VectorLength(vv);
		speed = l / x;
		if(speed <= MOVE_SPD_RUN && mf)
		{
//gi.bprintf(PRINT_HIGH,"fall do\n");
			ent->moveinfo.speed = speed / MOVE_SPD_RUN;
			VectorCopy(pos,ent->s.origin);
			return true;
		}
		goto JUMPCATCH;
	}
	goto JMPCHK;

JUMPCATCH:
	vel = ent->velocity[2] + VEL_BOT_JUMP;
	yori = pos[2];
//	l = 1.0;
//	VectorCopy(v,vv);
//	vv[2] = 0;
//	l = VectorLength(vv);
//gi.bprintf(PRINT_HIGH,"J fall\n");
	mf = false;
	for(x = 1;x <= FALLCHK_LOOPMAX;++x /*,l += x*/ )
	{
		vel -= grav;
		yori += vel * FRAMETIME; 

		if(vel > 0)
		{
			if(mf == false)
			{
				if(ypos < yori) mf = 2;
//gi.bprintf(PRINT_HIGH,"pre ok\n"); 
			}
/*			else if(mf == 2)
			{ 
				if(ypos >= yori)
					if((l / x) < MOVE_SPD_RUN)
					{
						mf = true;
						break;
					}
			}*/
		}
		else if(x > 1)
		{
//gi.bprintf(PRINT_HIGH,"oops\n");
			if(mf == false)
			{
				if(ypos < yori) mf = 2;
			}

			else if(mf == 2)
			{
				if(ypos >= yori)
				{
/*					if((l / (x - 1)) <= MOVE_SPD_RUN)
					{*/
//gi.bprintf(PRINT_HIGH,"Go %f\n",l / x);
						mf = true;
						break;
//					}
				}
			}
		}
	}	
	VectorCopy(v,vv);
	vv[2] = 0;
	if(mode == 2)
	{	
		//vel考慮の落下
		if(Route[zc->routeindex].state == GRS_ONTRAIN)
		{
			if(1/*Route[zc->routeindex].ent->trainteam == NULL*/)
			{
//gi.bprintf(PRINT_HIGH,"Go!\n"); //ppx
				vv[0] += FRAMETIME * Route[zc->routeindex].ent->velocity[0] * x;
				vv[1] += FRAMETIME * Route[zc->routeindex].ent->velocity[1] * x;
			}
		}	
	}
	l = VectorLength(vv);
	
	if(x > 1) l = l / (x - 1);
	if(l < MOVE_SPD_RUN && mf == true)
	{
		ent->moveinfo.speed = l / MOVE_SPD_RUN;
		VectorCopy(pos,ent->s.origin);

		ent->velocity[2] += VEL_BOT_JUMP;
		gi.sound(ent, CHAN_VOICE, gi.soundindex("*jump1.wav"), 1, ATTN_NORM, 0);
		PlayerNoise(ent, ent->s.origin, PNOISE_SELF);	//pon
		Set_BotAnim(ent,ANIM_JUMP,FRAME_jump1-1,FRAME_jump6);
//		ent->s.frame = FRAME_jump1-1;
//		ent->client->anim_end = FRAME_jump6;
//		ent->client->anim_priority = ANIM_JUMP;
//gi.bprintf(PRINT_HIGH,"j fall do\n");
		return true;							
	}	

	if(mode == 1) goto JMPCHK;//zc->second_target = NULL;
//ponko	else zc->route_trace = false;
//gi.bprintf(PRINT_HIGH,"j fall false\n");
//	return false;
JMPCHK:
//gi.bprintf(PRINT_HIGH,"NJ \n");
	if(Bot_Jump(ent,pos,dist)) return true;

//gi.bprintf(PRINT_HIGH,"NJ FAIL\n");
	zc->second_target = NULL;
	return false;
}
//-----------------------------------------------------------------------------------------
// target jump

qboolean TargetJump(edict_t *ent,vec3_t tpos)
{
	float	x,l,grav,vel,ypos,yori;
	vec3_t	v,vv;
	int		mf = false;

	grav = ent->gravity * sv_gravity->value * FRAMETIME;

	vel = ent->velocity[2] + VEL_BOT_JUMP;
	yori = ent->s.origin[2];
	ypos = tpos[2];

	//if on hazard object cause error
	if(!HazardCheck(ent,tpos))	return false;

	VectorSubtract(tpos,ent->s.origin,v);

	for(x = 1;x <= FALLCHK_LOOPMAX * 2 ;++x )
	{
		vel -= grav;
		yori += vel * FRAMETIME; 

		if(vel > 0)
		{
			if(mf == false)
			{
				if(ypos < yori) mf = 2;
			}
		}
		else if(x > 1)
		{
			if(mf == false)
			{
				if(ypos < yori) mf = 2;
			}

			else if(mf == 2)
			{
				if(ypos >= yori)
				{
						mf = true;
						break;
				}
			}
		}
	}	
	VectorCopy(v,vv);
	vv[2] = 0;

	l = VectorLength(vv);
	
	if(x > 1) l = l / (x - 1);
	if(l < MOVE_SPD_RUN && mf == true)
	{
		ent->moveinfo.speed = l / MOVE_SPD_RUN;

		ent->velocity[2] += VEL_BOT_JUMP;
		gi.sound(ent, CHAN_VOICE, gi.soundindex("*jump1.wav"), 1, ATTN_NORM, 0);
		PlayerNoise(ent, ent->s.origin, PNOISE_SELF);	//pon
		Set_BotAnim(ent,ANIM_JUMP,FRAME_jump1-1,FRAME_jump6);
		return true;							
	}
	return false;
}

qboolean TargetJump_Turbo(edict_t *ent,vec3_t tpos)
{
	float	x,l,grav,vel,ypos,yori;
	vec3_t	v,vv;
	int		mf = false;
	float	jvel;

	grav = ent->gravity * sv_gravity->value * FRAMETIME;

	vel = ent->velocity[2] + VEL_BOT_JUMP;

//if(vel > (VEL_BOT_JUMP + 100 + ent->gravity * sv_gravity->value * FRAMETIME ))
//	vel = VEL_BOT_JUMP + 100 + ent->gravity * sv_gravity->value * FRAMETIME;
	jvel = vel;

	yori = ent->s.origin[2];
	ypos = tpos[2];

	//if on hazard object cause error
	if(!HazardCheck(ent,tpos))	return false;

	VectorSubtract(tpos,ent->s.origin,v);

	for(x = 1;x <= FALLCHK_LOOPMAX * 2 ;++x )
	{
		vel -= grav;
		yori += vel * FRAMETIME; 

		if(vel > 0)
		{
			if(mf == false)
			{
				if(ypos < yori) mf = 2;
			}
		}
		else if(x > 1)
		{
			if(mf == false)
			{
				if(ypos < yori) mf = 2;
			}

			else if(mf == 2)
			{
				if(ypos >= yori)
				{
						mf = true;
						break;
				}
			}
		}
	}	
	VectorCopy(v,vv);
	vv[2] = 0;

	l = VectorLength(vv);
	
	if(x > 1) l = l / (x - 1);
	if(l < MOVE_SPD_RUN && mf == true)
	{
		ent->moveinfo.speed = l / MOVE_SPD_RUN;
//		VectorCopy(pos,ent->s.origin);

		ent->velocity[2] = jvel;//VEL_BOT_JUMP;
		gi.sound(ent, CHAN_VOICE, gi.soundindex("*jump1.wav"), 1, ATTN_NORM, 0);
		PlayerNoise(ent, ent->s.origin, PNOISE_SELF);	//pon
		Set_BotAnim(ent,ANIM_JUMP,FRAME_jump1-1,FRAME_jump6);
//		ent->s.frame = FRAME_jump1-1;
//		ent->client->anim_end = FRAME_jump6;
//		ent->client->anim_priority = ANIM_JUMP;
		return true;							
	}
	return false;
}
qboolean TargetJump_Chk(edict_t *ent,vec3_t tpos,float defvel)
{
	float	x,l,grav,vel,ypos,yori;
	vec3_t	v,vv;
	int		mf = false;

	grav = ent->gravity * sv_gravity->value * FRAMETIME;

	vel = defvel + VEL_BOT_JUMP;
	yori = ent->s.origin[2];
	ypos = tpos[2];

	//if on hazard object cause error
	if(!HazardCheck(ent,tpos))	return false;

	VectorSubtract(tpos,ent->s.origin,v);

	for(x = 1;x <= FALLCHK_LOOPMAX * 2 ;++x )
	{
		vel -= grav;
		yori += vel * FRAMETIME; 

		if(vel > 0)
		{
			if(mf == false)
			{
				if(ypos < yori) mf = 2;
			}
		}
		else if(x > 1)
		{
			if(mf == false)
			{
				if(ypos < yori) mf = 2;
			}

			else if(mf == 2)
			{
				if(ypos >= yori)
				{
						mf = true;
						break;
				}
			}
		}
	}	
	VectorCopy(v,vv);
	vv[2] = 0;

	l = VectorLength(vv);
	
	if(x > 1) l = l / (x - 1);
	if(l < MOVE_SPD_RUN && mf == true)
	{
		return true;							
	}
	return false;
}
//-----------------------------------------------------------------------------------------
// set anim
void Set_BotAnim(edict_t *ent,int anim,int frame,int end)
{
	if(ent->client->anim_priority < anim)
	{
		ent->s.frame = frame;
		ent->client->anim_end = end;
	}
}


//-----------------------------------------------------------------------------------------
// Get Water State
void Get_WaterState(edict_t *ent)
{
	zgcl_t	*zc;
	vec3_t	trmin,trmax;
	float	x;
	trace_t		rs_trace;

	zc = &ent->client->zc;

	//---------------
	//get waterstate
	if(ent->waterlevel)
	{
		VectorCopy(ent->s.origin,trmax);
		VectorCopy(ent->s.origin,trmin);
		trmax[2] -= 24;
		trmin[2] += 8;

		rs_trace = gi.trace (trmin, NULL,NULL,trmax,ent, MASK_WATER );
		x = trmin[2] - rs_trace.endpos[2];

		if(rs_trace.allsolid || rs_trace.startsolid || (/*x >= 4 &&*/ x < 4.0 )) zc->waterstate = WAS_IN;
		else 
		{

			if(x >= 4.0 && x <= 12.0 ) zc->waterstate = WAS_FLOAT; 
			else zc->waterstate = WAS_NONE;
		}
	}
	else zc->waterstate = WAS_NONE;
}


//-----------------------------------------------------------------------------------------
// Get origin of Route Index
void Get_RouteOrigin(int index,vec3_t pos)
{
	edict_t	*e;

	//when normal or items
	if(Route[index].state <= GRS_ITEMS || Route[index].state >= GRS_GRAPSHOT)
	{
		if(Route[index].state == GRS_ITEMS)
		{
			VectorCopy(Route[index].ent->s.origin,pos);
			pos[2] += 8;
		}
		else VectorCopy(Route[index].Pt,pos);

	}
	//when plat
	else if(Route[index].state == GRS_ONPLAT)
	{
		VectorCopy(Route[index].ent->union_ent->s.origin,pos);
		pos[2] += 8;
	}
	//when train
	else if(Route[index].state == GRS_ONTRAIN)
	{
		if(Route[index].ent->trainteam == NULL)
		{
			VectorCopy(Route[index].ent->union_ent->s.origin,pos);
			pos[2] += 8;
			return;
		}
		if(Route[index].ent->target_ent)
		{
			if(VectorCompare(Route[index].Tcourner,Route[index].ent->target_ent->s.origin))
			{
				VectorCopy(Route[index].ent->union_ent->s.origin,pos);
				pos[2] += 8;
				return;
			}
		}
		e = Route[index].ent->trainteam;
		while(1)
		{
			if(e == Route[index].ent) break;
			if(e->target_ent)
			{
				if(VectorCompare(Route[index].Tcourner,e->target_ent->s.origin))
				{
					VectorCopy(e->union_ent->s.origin,pos);
					pos[2] += 8;
					Route[index].ent = e;
					return;
				}
			}
			e = e->trainteam;
		}
		VectorCopy(Route[index].ent->union_ent->s.origin,pos);
		pos[2] += 8;
		return;
	}
	else if(Route[index].state == GRS_ONDOOR)
	{
		if(Route[index].ent->union_ent)
		{
			VectorCopy(Route[index].ent->union_ent->s.origin,pos);
			pos[2] += 8;
		}
		else if(index + 1 < CurrentIndex)
		{
			if(Route[index + 1].state <= GRS_ITEMS )
			{
				VectorCopy(Route[index + 1].Pt,pos);
				if(Route[index + 1].state == GRS_ITEMS) pos[2] += 8;
				pos[2] += 8;
			}
			//when plat or train
			else if(Route[index + 1].state <= GRS_ONTRAIN)
			{
				VectorCopy(Route[index + 1].ent->union_ent->s.origin,pos);
				pos[2] += 8;
			}
			else if(Route[index + 1].state == GRS_PUSHBUTTON)
			{
				VectorCopy(Route[index + 1].ent->union_ent->s.origin,pos);
				pos[2] += 8;
			}
			else VectorCopy(Route[index + 1].Pt,pos);
		}
		else
		{
			pos[0] = (Route[index].ent->absmin[0] + Route[index].ent->absmax[0]) / 2;
			pos[1] = (Route[index].ent->absmin[1] + Route[index].ent->absmax[1]) / 2;
			pos[2] = Route[index].ent->absmax[2];			
		}
	}
	else if(Route[index].state == GRS_PUSHBUTTON) VectorCopy(Route[index].ent->union_ent->s.origin,pos);
}

//-----------------------------------------------------------------------------------------
// search nearly pod
void Search_NearlyPod(edict_t *ent)
{
	vec3_t	v,v1,v2;
	float x;

	if(Route[ent->client->zc.routeindex].state >= GRS_ITEMS)	return;
//	else if(Route[ent->client->zc.routeindex].state ==/*>=*/ GRS_ITEMS)
//	{
//		if(Route[ent->client->zc.routeindex].ent->solid != SOLID_TRIGGER) return;
//	}

	if((ent->client->zc.routeindex + 1) < CurrentIndex)
	{
		if(Route[ent->client->zc.routeindex + 1].state >= GRS_ITEMS)	return;
		Get_RouteOrigin(ent->client->zc.routeindex + 1,v);
		if(TraceX(ent,v))
		{
			VectorSubtract(v,ent->s.origin,v1);

			Get_RouteOrigin(ent->client->zc.routeindex,v);
			VectorSubtract(v,ent->s.origin,v2);
			x = fabs(v1[2]);

			if(VectorLength(v1) < VectorLength(v2) && x <= JumpMax 
				&& Route[ent->client->zc.routeindex].state <= GRS_ONROTATE)
			{
				ent->client->zc.routeindex++;
			}
			else if(ent->client->zc.waterstate) return;
			else if(v2[2] > JumpMax && fabs(v1[2]) < JumpMax) ent->client->zc.routeindex++;

		}
	}
}

int Get_KindWeapon(gitem_t	*it)
{
	if(it == NULL) return WEAP_BLASTER;

	if(it->weaponthink		== Weapon_Shotgun)		return WEAP_SHOTGUN;
	else if(it->weaponthink == Weapon_SuperShotgun) return WEAP_SUPERSHOTGUN;
	else if(it->weaponthink == Weapon_Machinegun)	return WEAP_MACHINEGUN;
	else if(it->weaponthink == Weapon_Chaingun)		return WEAP_CHAINGUN;
	else if(it->weaponthink == Weapon_Grenade)		return WEAP_GRENADES;
	else if(it->weaponthink == Weapon_Trap)			return WEAP_TRAP;
	else if(it->weaponthink == Weapon_GrenadeLauncher)	return WEAP_GRENADELAUNCHER;
	else if(it->weaponthink == Weapon_RocketLauncher)	return WEAP_ROCKETLAUNCHER;
	else if(it->weaponthink == Weapon_HyperBlaster) return WEAP_HYPERBLASTER;
	else if(it->weaponthink == Weapon_Ionripper)	return WEAP_BOOMER;
	else if(it->weaponthink == Weapon_Railgun)		return WEAP_RAILGUN;
	else if(it->weaponthink == Weapon_Phalanx)		return WEAP_PHALANX;
	else if(it->weaponthink == Weapon_BFG)			return WEAP_BFG;
	else if(it->weaponthink == CTFWeapon_Grapple)	return WEAP_GRAPPLE;
	else return WEAP_BLASTER;
}

//-----------------------------------------------------------------------------------------
//
//
//
// normal bot's AI
//
//
//
//-----------------------------------------------------------------------------------------

void Bots_Move_NORM (edict_t *ent)
{
	float		dist;		//moving distance
	zgcl_t		*zc;		//zc's address
	
	int			foundedenemy;

	gitem_t		*item;

	float		x,yaw,iyaw,f1,f2,f3,bottom;
	int     	tempflag;//,buttonuse;
	vec3_t		temppos;

	trace_t		rs_trace;
	edict_t		*touch[MAX_EDICTS],*trent;
	vec3_t		touchmin,touchmax,v,vv;
	vec3_t		trmin,trmax;
	int			i,j,k;
	qboolean	canrocj,waterjumped;
	edict_t		*it_ent;
	gitem_t		*it;

	edict_t		*e;

	char		*string;

	cplane_t	plane;

	vec3_t		Origin,Velocity;//original param
	float		OYaw;			//

	qboolean	ladderdrop;		

	yaw = 0.0f;
	iyaw = 0.0f;

	trace_priority = TRP_NORMAL;	//trace on

	zc = &ent->client->zc;		//client add

	ent->client->zc.objshot = false;		//object shot clear

	ent->client->buttons &= ~BUTTON_ATTACK;

	//--------------------------------------------------------------------------------------
	//Solid Check
	i = gi.pointcontents (ent->s.origin);
	if(i & CONTENTS_SOLID)
	T_Damage (ent, ent, ent, ent->s.origin, ent->s.origin, ent->s.origin,100 , 1, 0, MOD_CRUSH);

	
	if(VectorCompare(ent->s.origin,ent->s.old_origin))
	{
		if(ent->groundentity == NULL && !ent->waterlevel) 
		{
			VectorCopy(ent->s.origin,v);
			v[2] -= 1.0;
			rs_trace = gi.trace(ent->s.origin,ent->mins,ent->maxs,v,ent,MASK_BOTSOLIDX);		
			if(!rs_trace.allsolid && !rs_trace.startsolid) ent->groundentity = rs_trace.ent;
		}
	}
	//	VectorCopy(ent->s.origin,Origin);
//	VectorCopy(ent->velocity,Velocity);
//	OYaw = ent->s.angles[YAW];

	//--------------------------------------------------------------------------------------
	//Check Debug mode
	if(chedit->value)
	{
		j = false;
		if(!zc->route_trace )
		{
//gi.bprintf(PRINT_HIGH,"route off\n");
			j = true;
		}
		if(zc->routeindex >= CurrentIndex)
		{
//gi.bprintf(PRINT_HIGH,"index overflow\n");
			j = true;
		}
		else if(Route[zc->routeindex].index == 0 && zc->routeindex > 0)
		{
//gi.bprintf(PRINT_HIGH,"index end\n");
			j = true;
		}

		if(j)
		{
			RemoveBot();
			//gi.cprintf(NULL,PRINT_HIGH,"Tracing failed.\n");
			return;
		}
	}
	//--------------------------------------------------------------------------------------
	//get JumpMax
	if(JumpMax == 0)
	{
		x = /*ent->velocity[2] + */ VEL_BOT_JUMP - ent->gravity * sv_gravity->value * FRAMETIME;
		JumpMax = 0;
		while(1)
		{
			JumpMax += x * FRAMETIME; 
			x -= ent->gravity * sv_gravity->value * FRAMETIME;
			if( x < 0 ) break;
		}
//gi.bprintf(PRINT_HIGH,"JumpMax %f",JumpMax);
	}
/*	if(!JmpTableChk)
	{
		
	
	}*/
	//--------------------------------------------------------------------------------------
	//target set
	if(!zc->havetarget && zc->route_trace)
	{
		k = 0;
		//primary weapon
		j = mpindex[Bot[zc->botindex].param[BOP_PRIWEP]];
		//secondary weapon
//		if(j && ent->client->pers.inventory[j]) j = mpindex[Bot[zc->botindex].param[BOP_SECWEP]];

		//ctf
		if(0/*ctf->value && bot_team_flag1 && bot_team_flag2*/)
		{
			it = NULL;
			if(ent->client->resp.ctf_team == CTF_TEAM1) 
			{
				if(zc->ctfstate == CTFS_DEFENDER || zc->ctfstate == CTFS_CARRIER) it = bot_team_flag1->item;
				else if(zc->ctfstate == CTFS_OFFENCER) it = bot_team_flag2->item;
			}
			else if(ent->client->resp.ctf_team == CTF_TEAM2) 
			{
				if(zc->ctfstate == CTFS_DEFENDER || zc->ctfstate == CTFS_CARRIER) it = bot_team_flag2->item;	
				else if(zc->ctfstate == CTFS_OFFENCER) it = bot_team_flag1->item;
			}
			if(it) {k = true;}
		}
if(ctf->value) j = 0;
		if((j && !ent->client->pers.inventory[j]) || k)
		{
			if(!k) it = &itemlist[j];
			if(zc->targetindex < zc->routeindex
				|| zc->targetindex >= CurrentIndex) zc->targetindex = zc->routeindex;
			for(i = zc->targetindex + 1;i < (zc->targetindex + 50);i++)
			{
				if(i > CurrentIndex) break;
				if(Route[i].state == GRS_ITEMS)
				{
					if(Route[i].ent->item == it)
					{
//gi.bprintf(PRINT_HIGH,"Target Flag On\n");
						zc->havetarget = true;
						break;
					}
					else if(!ctf->value && Route[i].ent->solid == SOLID_TRIGGER)
					{
						//Quad
						if((j = mpindex[MPI_QUAD]))
							if(Route[i].ent->item == &itemlist[j])
							{zc->havetarget = true;	break;}
						//Quad fire
						if((j = mpindex[MPI_QUADF]))
							if(Route[i].ent->item == &itemlist[j])
							{zc->havetarget = true;	break;}
						//Quad fire
						if((j = mpindex[MPI_PENTA]))
							if(Route[i].ent->item == &itemlist[j])
							{zc->havetarget = true;	break;}
					}
				}
			}
			zc->targetindex = i;
		}
		else
		{
			//quad
			j = mpindex[MPI_QUAD];
			//quad fire
			if(!j) j = mpindex[MPI_QUADF];

			if(j)
			{
				it = &itemlist[j];
				if(zc->targetindex < zc->routeindex
					|| zc->targetindex >= CurrentIndex) zc->targetindex = zc->routeindex;
				for(i = zc->targetindex + 1;i < (zc->targetindex + 25);i++)
				{
					if(i > CurrentIndex) break;
					if(Route[i].state == GRS_ITEMS)
					{
						if(Route[i].ent->item == it)
						{
							if(Route[i].ent->solid == SOLID_TRIGGER)
							{
								zc->havetarget = true;
								break;
							}
						}
					}
				}
				zc->targetindex = i;
			}
		}
	}
	else if(zc->havetarget)
	{
		if(zc->targetindex < zc->routeindex)
		{
			zc->havetarget = false;
			zc->targetindex = zc->routeindex;
		}

		else if(ctf->value) 
		{
			it = NULL;
			if(ent->client->resp.ctf_team == CTF_TEAM1) 
			{
				if(zc->ctfstate == CTFS_DEFENDER || zc->ctfstate == CTFS_CARRIER) it = bot_team_flag1->item;	
				else if(zc->ctfstate == CTFS_OFFENCER) it = bot_team_flag2->item;
			}
			else if(ent->client->resp.ctf_team == CTF_TEAM2) 
			{
				if(zc->ctfstate == CTFS_DEFENDER || zc->ctfstate == CTFS_CARRIER) it = bot_team_flag2->item;	
				else if(zc->ctfstate == CTFS_OFFENCER) it = bot_team_flag1->item;
			}
			if(Route[zc->targetindex].ent->item != it)
			{
				zc->havetarget = false;
				zc->targetindex = zc->routeindex;
			}
		}
	}
	//--------------------------------------------------------------------------------------
	//can rocket jump?
	it = Fdi_ROCKETLAUNCHER;//FindItem("Rocket Launcher");
	i = ITEM_INDEX(Fdi_ROCKETS/*FindItem("Rockets")*/);

	if(	ent->client->pers.inventory[ITEM_INDEX(it)]
		&& ent->client->pers.inventory[i] > 0) canrocj = true;
	else canrocj = false;
	if(!Bot[zc->botindex].param[BOP_ROCJ]) canrocj = false;

	//--------------------------------------------------------------------------------------
	//ducking check
	if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		if(ent->client->zc.battleduckcnt > 0 && ent->groundentity) goto DCHCANC;

		VectorSet(v,16,16,32);
		VectorCopy(ent->s.origin,v);

		v[2] += 28;

		rs_trace = gi.trace(ent->s.origin,ent->mins,ent->maxs,v,ent,MASK_BOTSOLIDX);
//gi.bprintf(PRINT_HIGH,"try to duck clear!\n");
		if(!rs_trace.startsolid && !rs_trace.allsolid && rs_trace.fraction == 1.0)
		{
//gi.bprintf(PRINT_HIGH,"duck cleared!\n");
			ent->client->ps.pmove.pm_flags &= ~PMF_DUCKED;
			ent->maxs[2] = 32;
		}
//		else gi.bprintf(PRINT_HIGH,"failed %i %i\n",rs_trace.startsolid ,rs_trace.allsolid);
	}
	else if(ent->velocity[2] > 10 && ent->groundentity == NULL
		&& !(zc->zcstate & STS_SJMASK))
	{
		VectorSet(v,16,16,40);
		rs_trace = gi.trace(ent->s.origin,ent->mins,v,ent->s.origin,ent,MASK_BOTSOLIDX);

		if(rs_trace.startsolid || rs_trace.allsolid)
		{
			ent->client->ps.pmove.pm_flags |= PMF_DUCKED;
			ent->maxs[2] = 4;
		}	
	}
DCHCANC://しゃがみっぱなし	
	//--------------------------------------------------------------------------------------
	//movingspeed set
	if(ent->groundentity || ent->waterlevel)
	{
		if(ent->waterlevel) 
		{
			if(!(zc->zcstate & STS_WATERJ)) zc->zcstate &= ~STS_SJMASK;
		}
		else zc->zcstate &= ~STS_SJMASK;
		if(ent->groundentity && !ent->waterlevel) ent->moveinfo.speed = 1.0;
		else if(ent->waterlevel && ent->velocity[2] <= 1) ent->moveinfo.speed = 1.0;
	}

	// if ducking down to da speed
	if(ent->client->ps.pmove.pm_flags & PMF_DUCKED && ent->groundentity) dist = MOVE_SPD_DUCK * ent->moveinfo.speed;
	else
	{
		if( !ent->waterlevel )
		{
			if(chedit->value || !Bot[zc->botindex].param[BOP_WALK] || !ent->groundentity) dist = MOVE_SPD_RUN * ent->moveinfo.speed;
			else dist = MOVE_SPD_WALK * ent->moveinfo.speed;
		}
		else 
		{
			if(ent->groundentity && ent->waterlevel < 2 ) dist = MOVE_SPD_RUN * ent->moveinfo.speed;
			else dist = MOVE_SPD_WATER * ent->moveinfo.speed; 
		}
		if(ent->groundentity) dist *= zc->ground_slope;
	}

	//--------------------------------------------------------------------------------------
	//get waterstate
	Get_WaterState( ent );

	//--------------------------------------------------------------------------------------
	//
	//search for enemy
	//
//	foundedenemy = 0;
//	i = CTS_AIMING ;

	zc->firstinterval += 2;
	if(zc->firstinterval >= 10)
	{
		zc->foundedenemy = Bot_SearchEnemy(ent);
		zc->firstinterval = Bot[zc->botindex].param[BOP_REACTION];
		if(zc->firstinterval > 10) zc->firstinterval = 10;
		if(zc->firstinterval < 0) zc->firstinterval = 0;
	}
	//--------------------------------------------------------------------------------------
	//
	//bot's combat status set
	//
	foundedenemy = zc->foundedenemy;
//	if(ent->client->ctf_grapple && !(ent->client->buttons & BUTTON_ATTACK)) {}
//	else
	Set_Combatstate(ent,foundedenemy);
	if(trace_priority == TRP_ALLKEEP) goto VCHCANSEL;
	//--------------------------------------------------------------------------------------
	//brause target status
	if(zc->second_target != NULL && zc->route_trace) 
								zc->rt_locktime = level.time + FRAMETIME * POD_LOCKFRAME;
	if(zc->second_target != NULL && !(zc->zcstate & STS_WAITSMASK))
	{
		if(zc->second_target->solid != SOLID_TRIGGER || !zc->second_target->inuse)
		{
			zc->second_target = NULL;
		}
		else if(!Bot_trace (ent,zc->second_target))
		{
			zc->second_target = NULL;
		}
		else if((zc->second_target->s.origin[2] - ent->s.origin[2]) > 32 && zc->waterstate != WAS_IN)
		{
			VectorSubtract(zc->second_target->s.origin,ent->s.origin,temppos);
			x = zc->second_target->moveinfo.start_origin[2] - ent->s.origin[2];
			k = false;
			if(temppos[2] > 32)
			{
				if(!canrocj)
				{
					if(x < 0 || x > 32) k = true;
					else if(!Bot_trace2 (ent,zc->second_target->moveinfo.start_origin)) k = true;
				}
				else
				{
					if(temppos[2] > 300) k = true;
				}
			}
			else
			{
				if(temppos[0] <= (ent->absmax[0] + 32) && temppos[0] >= (ent->absmin[0] + 32))
				if(temppos[1] <= (ent->absmax[1] + 32) && temppos[1] >= (ent->absmin[1] + 32))
				if(temppos[2] <= (ent->absmax[2] + 32) && temppos[2] >= (ent->absmin[2] + 32))
						k = true;
			}
			if(k)
			{
				zc->second_target = NULL;
//				if(zc->route_trace) Search_NearlyPod(ent);
			}
		}
	}
//	if(zc->route_trace && (zc->zcstate & STS_LADDERUP)) zc->rt_locktime = level.time + FRAMETIME * POD_LOCKFRAME;
	if(zc->route_trace)
	{
//PON-CTF>>
		if(zc->routeindex > 0)
		{
			if(ent->client->ctf_grapple)
			{
				ent->client->buttons |= BUTTON_ATTACK;
			}

//			if(Route[zc->routeindex].state == GRS_GRAPSHOT)
//			{ 
//				if(Route[zc->routeindex - 1].state == GRS_GRAPRELEASE) zc->routeindex++;
//			}
			if(!zc->first_target && ent->client->pers.weapon != Fdi_GRAPPLE	)
			{
				for(i = 0;i < (5 * 2);i++)
				{
					if((zc->routeindex + i) >= CurrentIndex) break;
					if(Route[zc->routeindex + i].state == GRS_GRAPSHOT)
					{
						item = Fdi_GRAPPLE;//FindItem("Grapple");
						if(	ent->client->pers.inventory[ITEM_INDEX(item)]) item->use(ent,item);
					}
				}
			}
			//撃てGrapple
			else if(Route[zc->routeindex - 1].state == GRS_GRAPSHOT
				&& ent->client->ctf_grapple == NULL
				&& zc->first_target == NULL)
			{
				item = Fdi_GRAPPLE;;//FindItem("Grapple");

				if(	ent->client->pers.inventory[ITEM_INDEX(item)])
				{
					item->use(ent,item);
//					ent->client->ctf_grapplestate = CTF_GRAPPLE_STATE_FLY;
//					ent->client->pers.weapon = item;
//					ent->client->weaponstate = WEAPON_READY;
//					ent->s.sound = 0;
					ShowGun(ent);
					if(ent->client->weaponstate == WEAPON_READY && ent->client->pers.weapon == item)
					{
						vv[0] = ent->s.origin[0];
						vv[1] = ent->s.origin[1];
						vv[2] = ent->s.origin[2] + ent->viewheight-8+2;
//						VectorCopy(Route[zc->routeindex - 1].Pt,ent->s.origin);
						VectorSubtract(Route[zc->routeindex - 1].Tcourner,vv,v);
						ent->s.angles[YAW] = Get_yaw(v);
						ent->s.angles[PITCH] = Get_pitch(v);
						trace_priority = TRP_ANGLEKEEP;
//						item->use(ent,item);
//						if(ent->client->weaponstate == WEAPON_READY)
//						{
							ent->client->buttons |= BUTTON_ATTACK;
//						}
					}
					else
					{
						if(zc->first_target == NULL && ent->groundentity) trace_priority = TRP_ALLKEEP;
						zc->routeindex--;/* trace_priority = TRP_ALLKEEP;*/
					}
				}
			}
			else if(ent->client->ctf_grapple)
			{
				//sticking check
				if(zc->nextcheck < (level.time + FRAMETIME * 10))
				{
					VectorSubtract(zc->pold_origin,ent->s.origin,temppos);
					if(VectorLength(temppos) < 64)
					{
						if(zc->route_trace)
						{
							zc->route_trace = false;
							zc->routeindex++;
							ent->client->buttons &= ~BUTTON_ATTACK;
						}
					}

					if(zc->nextcheck < level.time) 
					{
						VectorCopy(ent->s.origin,zc->pold_origin);
						zc->nextcheck = level.time + FRAMETIME * 40;
					}
				}
				if(ent->client->ctf_grapplestate == CTF_GRAPPLE_STATE_PULL)
				{
					if(ent->groundentity == NULL) zc->zcstate |= STS_ROCJ;

					if(Route[zc->routeindex].state != GRS_GRAPRELEASE)
					{
						for(i = 0;(zc->routeindex - i) > 0;i++)
						{
							if(Route[zc->routeindex - i].state == GRS_GRAPSHOT) break;
						}
						if((zc->routeindex - i) > 0)
						{
							for(j = 0;(zc->routeindex - i + j) < CurrentIndex;j++)
							{
								if(Route[zc->routeindex - i + j].state == GRS_GRAPRELEASE) break;
							}
							if((zc->routeindex - i + j) < CurrentIndex)
									zc->routeindex = zc->routeindex - i + j;  
						}
					}
					if(Route[zc->routeindex].state != GRS_GRAPRELEASE)
					{
						item = Fdi_GRAPPLE;//FindItem("Grapple");
					item->use(ent,item);
//						ent->client->pers.weapon = item;
//						ent->s.sound = 0;
//						ShowGun(ent);
						ent->client->buttons &= ~BUTTON_ATTACK;
					}
				}
				else if(ent->client->ctf_grapplestate == CTF_GRAPPLE_STATE_HANG
					&& Route[zc->routeindex].state != GRS_GRAPRELEASE)
				{
					item = Fdi_GRAPPLE;//FindItem("Grapple");
					item->use(ent,item);
//					ent->client->pers.weapon = item;
//					ent->s.sound = 0;
//					ShowGun(ent);
					ent->client->buttons &= ~BUTTON_ATTACK;
//gi.bprintf(PRINT_HIGH,"Groff 2!\n");
				}
			}

			else if(Route[zc->routeindex - 1].state == GRS_GRAPHOOK
				&& ent->client->ctf_grapple)
			{
				if(ent->client->ctf_grapplestate == CTF_GRAPPLE_STATE_FLY)
					trace_priority = TRP_ALLKEEP;

			}
/*			if(Route[zc->routeindex].state == GRS_GRAPHOOK
				&& ent->client->ctf_grapple)
			{
				trace_priority = TRP_ALLKEEP;
			}*/
			if(Route[zc->routeindex].state == GRS_GRAPRELEASE
				&& ent->client->ctf_grapple) 
			{
				k = 0;
				if(ent->client->ctf_grapplestate == CTF_GRAPPLE_STATE_FLY)
					trace_priority = TRP_ALLKEEP;
				else if(ent->client->ctf_grapplestate == CTF_GRAPPLE_STATE_PULL)
				{
					if(1)
					{
						e = (edict_t*)ent->client->ctf_grapple;
						VectorSubtract(ent->s.origin,e->s.origin,v);
						yaw = VectorLength(v);
						if(yaw <= (Route[zc->routeindex].Tcourner[0] /*+ 32*/))
						{
//							if(yaw < 40) ent->moveinfo.speed = 0;
							item = Fdi_GRAPPLE;//FindItem("Grapple");
							item->use(ent,item);
							ent->client->buttons &= ~BUTTON_ATTACK;
							zc->routeindex++;
							k = true;
//gi.bprintf(PRINT_HIGH,"Groff 1!\n");
						}
						else if(!ent->waterlevel) trace_priority = TRP_ALLKEEP;
					}
//					gi.bprintf(PRINT_HIGH,"length %f < %f\n",Route[zc->routeindex].Tcourner[0],VectorLength(v));
				}
				else if(ent->client->ctf_grapplestate == CTF_GRAPPLE_STATE_HANG)
				{
/*					if((zc->routeindex + 1) < CurrentIndex)
					{
						if(Route[zc->routeindex + 1].state == GRS_GRAPSHOT)
						{
							if(TraceX(ent,Route[zc->routeindex + 1].Tcourner)) k = true; 
						}
						else k = true;
					}*/
					ent->moveinfo.speed = 0;
					k = true;
					if(k)
					{
						item = Fdi_GRAPPLE;//FindItem("Grapple");
						item->use(ent,item);
						ent->client->buttons &= ~BUTTON_ATTACK;
						zc->routeindex++;
//gi.bprintf(PRINT_HIGH,"Groff 0!\n");
					}
				}
				if(k)
				{
					if(zc->routeindex < CurrentIndex)
					{
						if(Route[zc->routeindex].state == GRS_GRAPSHOT)
						{
							if(1/*TraceX(ent,Route[zc->routeindex + 1].Tcourner)*/) zc->routeindex++;
						}
					}
				}
			}
/*			else if(ent->client->ctf_grapple)
			{
				if(ent->client->ctf_grapplestate == CTF_GRAPPLE_STATE_PULL
					&& Route[zc->routeindex - 1].state == GRS_GRAPRELEASE)
				{
					ent->client->buttons &= ~BUTTON_ATTACK;
					CTFResetGrapple(ent->client->ctf_grapple);				
				}
			}*/
		}
//>>PON-CTF
//		if(trace_priority == TRP_ALLKEEP) goto VCHCANSEL;

		if(Route[zc->routeindex].state >= GRS_NORMAL) Search_NearlyPod(ent);

		Get_RouteOrigin(zc->routeindex,v);

		x = v[2] - ent->s.origin[2];

		if(zc->zcstate & STS_WAITSMASK) zc->rt_locktime = level.time + FRAMETIME * POD_LOCKFRAME;
		else if(Route[zc->routeindex].state <= GRS_ITEMS && (x > JumpMax && !zc->waterstate)
			&& !(zc->zcstate & STS_LADDERUP))
		{
			if(zc->rt_locktime <= level.time)
			{
#ifdef _DEBUG
gi.bprintf(PRINT_HIGH,"OFF 2\n"); //ppx
#endif
				zc->route_trace = false;
				zc->rt_releasetime = level.time + FRAMETIME * POD_RELEFRAME;
			}
		}
		else if(!TraceX(ent,v) /*&& ent->client->ctf_grapple == NULL*/)
		{	
			k = false;
			if(ent->groundentity)
			{
				if(ent->groundentity->classname[0] == 'f')
				{
					if(/*!Q_stricmp(ent->groundentity->classname, "func_plat")
						||*/ !Q_stricmp(ent->groundentity->classname, "func_train"))
					{
						zc->rt_locktime = level.time + FRAMETIME * POD_LOCKFRAME;
						k = true;
					}
				}
			}
//			if(Route[zc->routeindex].state == GRS_ONTRAIN 
//				&& /*Route[zc->routeindex].ent->trainteam*/) zc->rt_locktime = level.time + FRAMETIME * POD_LOCKFRAME;
//PON
			if(ent->client->ctf_grapple) 
			{
				if(!VectorCompare(ent->s.origin,ent->s.old_origin)) zc->rt_locktime += FRAMETIME;
			}
//PON
			if(zc->rt_locktime <= level.time && !k)
			{
//				gi.bprintf(PRINT_HIGH,"shit1");
#ifdef _DEBUG
gi.bprintf(PRINT_HIGH,"OFF 3\n"); //ppx
#endif
				zc->route_trace = false;
				zc->rt_releasetime = level.time + FRAMETIME * POD_RELEFRAME;
			}
		}
		else
		{
			if(Route[zc->routeindex].state > GRS_ITEMS 
				&& Route[zc->routeindex].state <= GRS_ONPLAT)
			{
				if(0/*Route[zc->routeindex].state == GRS_ONPLAT && Route[zc->routeindex].ent->moveinfo.state != PSTATE_BOTTOM*/)
				{
#ifdef _DEBUG
gi.bprintf(PRINT_HIGH,"OFF 4\n"); //ppx
#endif
					zc->route_trace = false;
					zc->rt_releasetime = level.time + FRAMETIME * POD_RELEFRAME;
				}
				else if(0/*Route[zc->routeindex].ent->union_ent->solid != SOLID_TRIGGER*/)
				{
#ifdef _DEBUG
gi.bprintf(PRINT_HIGH,"OFF 5\n"); //ppx
#endif
					zc->route_trace = false;
					zc->rt_releasetime = level.time + FRAMETIME * POD_RELEFRAME;
				}
				else zc->rt_locktime = level.time + FRAMETIME * POD_LOCKFRAME;
			}
			else zc->rt_locktime = level.time + FRAMETIME * POD_LOCKFRAME;
		}
	}
	else
	{
		if(ent->client->ctf_grapple)
		{
			item = FindItem("Grapple");
			ent->client->pers.weapon = item;
			ent->s.sound = 0;
			ShowGun(ent);
			ent->client->buttons &= ~BUTTON_ATTACK;		
		}
	}
	if(trace_priority == TRP_ALLKEEP)
	{
		if(ent->client->ctf_grapple)
		{
			rs_trace = gi.trace (ent->s.origin, ent->maxs, ent->mins, ent->s.origin,ent, MASK_BOTSOLIDX);
			if(rs_trace.allsolid || rs_trace.startsolid || rs_trace.fraction != 1.0) 
				ent->client->ps.pmove.pm_flags |= PMF_DUCKED;
		}
		goto VCHCANSEL;
	}
	//--------------------------------------------------------------------------------------
	//search for items
	if(!chedit->value && zc->second_target == NULL)
	{
		pickup_pri = false;			//pickup priority off
		k = false;

		zc->secondinterval++;
		//when tracing routes
		if(zc->route_trace && zc->secondinterval > 40)
		{
			for(i = zc->routeindex ; i < (zc->routeindex + 20); i++)
			{
				if(i >= CurrentIndex) break;
				if(Route[i].state == GRS_ITEMS)
				{
					if(Route[i].ent->solid == SOLID_TRIGGER)
					{
						pickup_pri = true;
						break;
					}
				}
			}
		}
		
		if(1/*!k*/)
		{
			if(zc->secondinterval > 40)
			{
				if(!(zc->zcstate & STS_WAITSMASK ))
				{
					Bot_SearchItems(ent);
					if(zc->second_target != NULL && pickup_pri)
					{
						for(i = zc->routeindex ; i < (zc->routeindex + 20); i++)
						{
							if(i >= CurrentIndex) break;
							if(Route[i].state == GRS_ITEMS)
							{
								if(Route[i].ent == zc->second_target)
								{
									zc->second_target = NULL;
									break;
								}
							}
						}						
					}
				}
			}
		}

		if(zc->secondinterval > 40/*zc->second_target != NULL*/ /*&& !k*/)
		{
			zc->secondinterval = Bot[zc->botindex].param[BOP_PICKUP] * 4;
			if(zc->secondinterval > 36) zc->secondinterval = 36;
			if(zc->secondinterval < 0) zc->secondinterval = 0;
		}

		if(ent->client->zc.objshot) goto VCHCANSEL; //object shot!!
	}

	//--------------------------------------------------------------------------------------
	//go up ladder
	//
	//
	//
	//	梯子を登る
	//
	//
	//--------------------------------------------------------------------------------------

	if(zc->zcstate & STS_LADDERUP)
	{
#ifdef _DEBUG
gi.bprintf(PRINT_HIGH,"ladder UP!! %f %i\n",zc->moveyaw,ent->waterlevel);
#endif
		if(ent->waterlevel > 1)
		{
/*			if(zc->route_trace )
			{
				Get_RouteOrigin(zc->routeindex,v);
				Bot_Watermove ( ent,pos,dist,upd)*/
			ent->velocity[2] = VEL_BOT_WLADRUP;
//			if(VectorCompare(ent->s.origin,ent->s.old_origin)) ent->velocity[2] += 50;
		}
		else
		{
			ent->velocity[2] = VEL_BOT_LADRUP;
//			if(VectorCompare(ent->s.origin,ent->s.old_origin)) ent->velocity[2] += 50;
		}

		VectorCopy(ent->mins,trmin);
		trmin[2] += 20;

		yaw = zc->moveyaw * M_PI * 2 / 360;
		touchmin[0] = cos(yaw) * 32;//96 ;
		touchmin[1] = sin(yaw) * 32;//96 ;
		touchmin[2] = 0;
		
		VectorAdd(ent->s.origin,touchmin,touchmax);

		rs_trace = gi.trace (ent->s.origin, trmin/*ent->mins*/,ent->maxs, touchmax,ent, MASK_BOTSOLID/*MASK_PLAYERSOLID*/);

		plane = rs_trace.plane;

		if(!(rs_trace.contents & CONTENTS_LADDER) && !rs_trace.allsolid /*&& !rs_trace.startsolid*/)
		{
#ifdef _DEBUG
gi.bprintf(PRINT_HIGH,"ladder OFF1!!\ncont %x %x\nall %i\nstart %i\n",rs_trace.contents,i,rs_trace.allsolid,rs_trace.startsolid);
#endif
			if(ent->velocity[2] <= VEL_BOT_LADRUP && !ent->waterlevel) ent->velocity[2] = VEL_BOT_LADRUP;
			zc->zcstate &= ~STS_LADDERUP;
			ent->moveinfo.speed = 0.25;
			if(zc->route_trace)
			{
				Get_RouteOrigin(zc->routeindex,v);
				if(VectorLength(v) > 32)
				{
					VectorSubtract(v,ent->s.origin,v);
					zc->moveyaw = Get_yaw(v);
					if(trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] = zc->moveyaw;
				}
				else zc->routeindex++;
			}
		}
		else
		{	
//pon
			if(!rs_trace.allsolid)
			{
				VectorCopy(rs_trace.endpos,ent->s.origin);
			}

//pon
//			ent->moveinfo.speed = 1.0;
			VectorCopy(ent->s.origin,touchmin);
			touchmin[2] += 8;
			
			rs_trace = gi.trace (ent->s.origin, ent->mins,ent->maxs, touchmin,ent, MASK_BOTSOLID/*MASK_PLAYERSOLID*/ );

			x = rs_trace.endpos[2] - ent->s.origin[2];
			
			if(ent->waterlevel )
			{
				ent->s.origin[2] += x;
			}
			else
			{
				ent->s.origin[2] += x;
			}			
			
			e = rs_trace.ent;

			if(x == 0/*VectorCompare(ent->s.origin,ent->s.old_origin)*/) 
			{
				x = Get_yaw(plane.normal);

				//right
				VectorCopy(ent->s.origin,v);
				yaw = x + 90;
				if(yaw > 180) yaw -= 360; 
				yaw = yaw * M_PI * 2 / 360;
				touchmin[0] = cos(yaw) * 48 ;
				touchmin[1] = sin(yaw) * 48 ;
				touchmin[2] = 0;
				VectorAdd(ent->s.origin,touchmin,trmin);

				VectorCopy(trmin,trmax);
				trmin[2] += 32;
				trmax[2] += 64;
				rs_trace = gi.trace(trmin,NULL,NULL,trmax,ent,MASK_BOTSOLID);
				f1 = rs_trace.fraction;

				//left
				VectorCopy(ent->s.origin,v);
				iyaw = x -90 ;
				if(iyaw < 180) iyaw += 360; 
				iyaw = iyaw * M_PI * 2 / 360;
				touchmin[0] = cos(iyaw) * 48 ;
				touchmin[1] = sin(iyaw) * 48 ;
				touchmin[2] = 0;
				VectorAdd(ent->s.origin,touchmin,trmin);

				VectorCopy(trmin,trmax);
				trmin[2] += 32;
				trmax[2] += 64;
				rs_trace = gi.trace(trmin,NULL,NULL,trmax,ent,MASK_BOTSOLID);
				f2 = rs_trace.fraction;

				x = 0.0;
				if(f1 == 1.0 && f2 != 1.0) x = yaw;
				else if(f1 != 1.0 && f2 == 1.0) x = iyaw;

				if(x != 0.0)
				{
					touchmin[0] = cos(x) * 4 ;
					touchmin[1] = sin(x) * 4 ;
					touchmin[2] = 0;
					VectorAdd(ent->s.origin,touchmin,trmin);
					rs_trace = gi.trace(ent->s.origin,ent->mins,ent->maxs,trmin,ent,MASK_BOTSOLID);
					if(rs_trace.startsolid || rs_trace.allsolid) x = 0;
					else VectorCopy(rs_trace.endpos,ent->s.origin);
				}
				
				if(x == 0.0)
				{
#ifdef _DEBUG
gi.bprintf(PRINT_HIGH,"ladder OFF2!!\n");
#endif
					k = 0;
					if(e)
					{
						if(Q_stricmp (e->classname, "func_door") == 0)
						{
							if(e->moveinfo.state == PSTATE_UP) k = true;
						}
					}
					if(!k)
					{
						zc->moveyaw += 180;
						if(zc->moveyaw > 180) zc->moveyaw -= 360;
						zc->zcstate &= ~STS_LADDERUP;
						ent->moveinfo.speed = 0.25;
					}
				}
			}
		}
			
		if(zc->zcstate & STS_LADDERUP)
		{
			if(zc->route_trace )
			{
				Get_RouteOrigin(zc->routeindex,v);
				if(v[2] < ent->s.origin[2])
				{
					VectorSubtract(ent->s.origin,v,vv);
					vv[2] = 0;
					if(VectorLength(vv) < 32) zc->routeindex++;
				}
			}


			ent->velocity[0] = 0;
			ent->velocity[1] = 0;
			goto VCHCANSEL_L;
		}
	}

	//--------------------------------------------------------------------------------------
	//bot's true moving yaw,yaw pitch set
	// j is used ground entity check section
	//
	//
	//
	//	移動方向決定
	//
	//
	//--------------------------------------------------------------------------------------

	j = 0;
	if(ent->groundentity && ent->waterlevel <= 1 && trace_priority < TRP_ANGLEKEEP) ent->s.angles[PITCH] = 0;
	if(zc->second_target != NULL )
	{
		if((zc->second_target->s.origin[2] - ent->s.origin[2]) > 32 && !ent->waterlevel)
		{
			x = zc->second_target->moveinfo.start_origin[2] - ent->s.origin[2];
			if(x <= 32 && x > -24 && Bot_trace2 (ent,zc->second_target->moveinfo.start_origin))
			{
				VectorSubtract(zc->second_target->moveinfo.start_origin,ent->s.origin,temppos);
				k = false;
				yaw = temppos[2];
				temppos[2] = 0;
				x = VectorLength(temppos);

				if(yaw < -32 && x < 32) k = true;
				
				if(!k)
				{
					if(!ent->groundentity && !ent->waterlevel)
					{
						if(trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] = Get_yaw(temppos);
					}
					else if(ent->groundentity || ent->waterlevel )
					{
						if(trace_priority < TRP_MOVEKEEP) zc->moveyaw = Get_yaw(temppos);
						if(trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] = zc->moveyaw;
					}

					if(x < dist && fabs(temppos[2]) < 24) dist = x;//pon 
				}
				j = -1;
			}
		}
		else 
		{
			VectorSubtract(zc->second_target->s.origin,ent->s.origin,temppos);
			if(ent->waterlevel && !ent->groundentity && trace_priority < TRP_ANGLEKEEP) ent->s.angles[PITCH] = Get_pitch(temppos); 

			if(!ent->groundentity && !ent->waterlevel && trace_priority < TRP_ANGLEKEEP)
			{
				temppos[2] = 0;
				if(VectorLength(temppos) > 32)
				{
					if(trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] = Get_yaw(temppos);
				}
			}
			else if(ent->groundentity || ent->waterlevel )
			{
				k = false;
				yaw = temppos[2];
				temppos[2] = 0;
				x = VectorLength(temppos);

				if(yaw < -32 && x < 32) k = true;

				if(!k)
				{
					if(trace_priority < TRP_MOVEKEEP) zc->moveyaw = Get_yaw(temppos);	//set the movin' yaw
					if(trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] = zc->moveyaw;
					if(x < dist && fabs(yaw) < JumpMax) dist = x; 
				}
			}
			j = -1;
		}
	}
	else
	{
		if(ent->groundentity && !zc->route_trace) 
		{
			if(trace_priority < TRP_MOVEKEEP) zc->moveyaw = ent->s.angles[YAW];
		}
		else if(trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] = zc->moveyaw ;
	}

/*	if(zc->first_target != NULL)
	{
		VectorSubtract(zc->first_target->s.origin,ent->s.origin,temppos);
		ent->s.angles[YAW] = Get_yaw(temppos);	//set the model's yaw
		ent->s.angles[PITCH] = Get_pitch(temppos);
	}
*/
	//チームプレイ時のルーチン
	if(ctf->value ||((int)(dmflags->value) & (DF_MODELTEAMS | DF_SKINTEAMS)))
	{
		if(ctf->value)
		{
			if(zc->ctfstate == CTFS_SUPPORTER)
			{
				if(zc->followmate)
				{
					if(zc->followmate->inuse)
						if(zc->followmate->client->zc.ctfstate != CTFS_CARRIER)
						{
							zc->ctfstate = CTFS_OFFENCER;
							zc->followmate = NULL;						
						}
				}

				if(zc->second_target == NULL) j = 1;
			}
			else j = 0;	
		}
		else
		{
			if(zc->tmplstate == TMS_FOLLOWER && zc->second_target == NULL) j = 1;
			else j = 0;		
		}

		if(j/*zc->tmplstate == TMS_FOLLOWER && zc->second_target == NULL*/)
		{
			if(zc->followmate)
			{
				k = Bot_traceS(ent,zc->followmate);
				if(k || zc->route_trace) zc->matelock = level.time + FRAMETIME * 5;
				if(!zc->followmate->inuse || zc->followmate->deadflag || zc->matelock <= level.time)
				{
					if(ctf->value) zc->ctfstate = CTFS_OFFENCER;
					else zc->tmplstate = TMS_NONE;
					zc->followmate = NULL;
				}
				else
				{
					VectorSubtract(zc->followmate->s.origin,ent->s.origin,v);
					if(VectorLength(v) < 200) 
					{
						if(k && zc->followmate->client->zc.route_trace
							&& (zc->followmate->client->zc.routeindex - 2) > 0
							&& (ent->svflags & SVF_MONSTER))
						{
							zc->routeindex = zc->followmate->client->zc.routeindex - 2;
							zc->route_trace = true;
							if(zc->followmate->client->zc.havetarget)
							{
								zc->targetindex = zc->followmate->client->zc.targetindex;
							}
						}
						else if(!(ent->svflags & SVF_MONSTER))
						{
							zc->moveyaw = Get_yaw(v);
							//if(VectorLength(v) < 100) trace_priority = TRP_ALLKEEP;
							//else trace_priority = TRP_MOVEKEEP;
						}
						if(VectorLength(v) < 100)
						{
							if(!v[0]) v[0] = 1;
							if(!v[1]) v[1] = 1;
							v[0] *= -1;
							v[1] *= -1;
							if(trace_priority < TRP_MOVEKEEP) zc->moveyaw = Get_yaw(v);
							if(trace_priority < TRP_ANGLEKEEP)
							{
								ent->s.angles[YAW] = zc->moveyaw;
								ent->s.angles[PITCH] = Get_pitch(v);
							}
						}
						else if(trace_priority < TRP_MOVEKEEP)
						{
							goto VCHCANSEL;
						}
					}
					else if(ent->groundentity || ent->waterlevel )
					{
						if(zc->followmate->client->zc.route_trace && (ent->svflags & SVF_MONSTER)) zc->routeindex = zc->followmate->client->zc.routeindex;
						else
						{
							if(trace_priority < TRP_MOVEKEEP) zc->moveyaw = Get_yaw(v);
							if(trace_priority < TRP_ANGLEKEEP)
							{
								ent->s.angles[YAW] = zc->moveyaw;
								ent->s.angles[PITCH] = Get_pitch(v);
							}
						}
					}
				}
			}
			else
			{
				if(ctf->value) zc->ctfstate = CTFS_OFFENCER;
				else zc->tmplstate = TMS_NONE;
			}
		}
	}

	//ctf route index fix
	if(ctf->value && !chedit->value)
	{
		j = 0;
		if(ent->client->resp.ctf_team == CTF_TEAM1)
		{
//if(zc->ctfstate == CTFS_CARRIER)
//			gi.bprintf(PRINT_HIGH,"I am carrierX!!\n");
			if(zc->ctfstate == CTFS_DEFENDER 
				|| zc->ctfstate == CTFS_CARRIER
				|| zc->ctfstate == CTFS_SUPPORTER) j = FOR_FLAG1;
			else j = FOR_FLAG2;
		}
		else if(ent->client->resp.ctf_team == CTF_TEAM2)
		{
			if(zc->ctfstate == CTFS_DEFENDER 
				|| zc->ctfstate == CTFS_CARRIER
				|| zc->ctfstate == CTFS_SUPPORTER) j = FOR_FLAG2;
			else j = FOR_FLAG1;
		}

		if(zc->route_trace)
		{
			if(Route[zc->routeindex].state < GRS_ITEMS
				&& Route[zc->routeindex].linkpod[MAXLINKPOD - 1])
			{
				k = Route[zc->routeindex].linkpod[MAXLINKPOD - 1];
				if(j == FOR_FLAG1)
				{
					if(k & CTF_FLAG2_FLAG) 
					{
//gi.bprintf(PRINT_HIGH,"Wrong way 1\n");
						for(i = 0;i < (MAXLINKPOD - 1);i++)
						{
							if(!Route[zc->routeindex].linkpod[i]) break;
							k = Route[Route[zc->routeindex].linkpod[i]].linkpod[MAXLINKPOD - 1];
							if(!(k & CTF_FLAG2_FLAG))
							{
								zc->routeindex = Route[zc->routeindex].linkpod[i];// zc->route_trace = false;
								zc->havetarget = false;
//gi.bprintf(PRINT_HIGH,"fixed for flag 1\n");
							}
						}
					}
					else if(!zc->havetarget && zc->ctfstate == CTFS_CARRIER)
					{
						zc->havetarget = true;
						zc->targetindex = Route[zc->routeindex].linkpod[MAXLINKPOD - 1] & 0x7FFF;
					}
				}
				else if(j == FOR_FLAG2)
				{
					if(!(k & CTF_FLAG2_FLAG))
					{
//gi.bprintf(PRINT_HIGH,"Wrong way 2\n");
						for(i = 0;i < (MAXLINKPOD - 1);i++)
						{
							if(!Route[zc->routeindex].linkpod[i]) break;
							k = Route[Route[zc->routeindex].linkpod[i]].linkpod[MAXLINKPOD - 1];
							if(k & CTF_FLAG2_FLAG)
							{
								zc->routeindex = Route[zc->routeindex].linkpod[i];// zc->route_trace = false;
								zc->havetarget = false;
//gi.bprintf(PRINT_HIGH,"fixed for flag 2\n");
							}
						}
					}
					else if(!zc->havetarget && zc->ctfstate == CTFS_CARRIER)
					{
						zc->havetarget = true;
						zc->targetindex = Route[zc->routeindex].linkpod[MAXLINKPOD - 1] & 0x7FFF;
					}
				}
			}
		}
	}

	if(1/*!(zc->zcstate & STS_WAITSMASK)*/)
	{
		//ルートトレース用index検索
		if(!zc->route_trace && zc->rt_releasetime <= level.time)
		{
			//zc->routeindex;
			if(zc->routeindex >= CurrentIndex) zc->routeindex = 0;
			//fix route index
			for(i = 0;i < CurrentIndex && i < MAX_SEARCH;i++)
			{
				if(Route[zc->routeindex].state == GRS_GRAPHOOK)
				{
					while(1)
					{
						++zc->routeindex;
						if(zc->routeindex >= CurrentIndex){i = CurrentIndex; break;}
						if(Route[zc->routeindex].state == GRS_GRAPRELEASE) {++zc->routeindex; break;}
					}
					continue;
				}
				else if(Route[zc->routeindex].state == GRS_GRAPRELEASE) {++zc->routeindex; continue;}
				else if(ctf->value && !chedit->value)
				{
					if(Route[zc->routeindex].state < GRS_ITEMS
						&& Route[zc->routeindex].linkpod[MAXLINKPOD - 1])
					{
						k = Route[zc->routeindex].linkpod[MAXLINKPOD - 1];
						if(j == FOR_FLAG1)
						{
							if(k & CTF_FLAG2_FLAG)
							{
								zc->routeindex = (k & 0x7FFF);
//gi.bprintf(PRINT_HIGH,"skipped to flag 1 %x\n",k);
							}
						}
						else if(j == FOR_FLAG2)
						{
							if(!(k & CTF_FLAG2_FLAG))
							{
								zc->routeindex = (k & 0x7FFF);
//gi.bprintf(PRINT_HIGH,"skipped to flag 2 %x\n",k);
							}
						}
					}
				}
				Get_RouteOrigin(zc->routeindex,v);
//				VectorSubtract(Route[k].Pt,ent->s.origin,temppos);
				if(Route[zc->routeindex].state <= GRS_ITEMS && TraceX(ent,v))
				{
					if(fabs(v[2] - ent->s.origin[2]) <= JumpMax || zc->waterstate == WAS_IN)
					{
						zc->route_trace = true;
						zc->rt_locktime = level.time + FRAMETIME * POD_LOCKFRAME;
						break;
					}
				}
				if(++zc->routeindex >= CurrentIndex) zc->routeindex = 0;
			}
		}
		else if(zc->route_trace)
		{
			if(Route[zc->routeindex].state == GRS_ONDOOR)
			{
				if(1/*!Route[zc->routeindex].ent->union_ent*/)
				{
					it_ent = Route[zc->routeindex].ent;
					if(zc->routeindex + 1 < CurrentIndex )
					{
						Get_RouteOrigin(zc->routeindex + 1,v);
						zc->route_trace = false;
						j = TraceX(ent,v);
						zc->route_trace = true;
						if((!j || (v[2] - ent->s.origin[2]) > JumpMax )&& it_ent->union_ent) 
						{

							k = false;
							if((it_ent->union_ent->s.origin[2] - ent->s.origin[2]) > JumpMax) k = true;

							VectorSubtract(it_ent->union_ent->s.origin,ent->s.origin,temppos);
							yaw = Get_yaw(temppos);
							if(trace_priority < TRP_ANGLEKEEP)
							{
								ent->s.angles[PITCH] = Get_pitch(temppos);
								ent->s.angles[YAW] = yaw;
							}
							temppos[2] = 0;
							x = VectorLength(temppos);

							if( x == 0/*< dist*/ || k)
							{
								if(it_ent->nextthink >= level.time) zc->rt_locktime = level.time + FRAMETIME * POD_LOCKFRAME;
								goto VCHCANSEL;	//if center position move cancel
							}
							if(x < dist) dist = x;
							if(it_ent->nextthink > level.time) zc->rt_locktime = it_ent->nextthink + FRAMETIME * POD_LOCKFRAME;
							else zc->rt_locktime = level.time + FRAMETIME * POD_LOCKFRAME;
							if(trace_priority < TRP_MOVEKEEP) zc->moveyaw = yaw;
							goto GOMOVE;
						}
					}
					
				}
				zc->routeindex++;
			}

			if(zc->routeindex < CurrentIndex)
			{
				Get_RouteOrigin(zc->routeindex,v);
/*				if(Route[zc->routeindex].state == GRS_ITEMS)
				{
					if(Route[zc->routeindex].ent->solid == SOLID
				}*/

				k = false;
				if(Route[zc->routeindex].state == GRS_PUSHBUTTON)
				{
					it_ent = Route[zc->routeindex].ent;
					if(it_ent->health && (it_ent->takedamage || it_ent->moveinfo.state != PSTATE_TOP))
					{
						k = 2;
					}
					else if(it_ent->health)
					{
						zc->routeindex++;
						if(zc->routeindex < CurrentIndex) Get_RouteOrigin(zc->routeindex,v);
					} 
				}
				else
				{
					//VectorCopy(ent->mins,touchmin);
					//touchmin[2] += 20;
					VectorSet(touchmax,16,16,4);
					VectorSet(touchmin,-16,-16,0);
					rs_trace = gi.trace(ent->s.origin,touchmin,touchmax,v,ent,MASK_SHOT);
					if(rs_trace.fraction != 1.0 && rs_trace.ent)
					{
						if((rs_trace.ent->health || rs_trace.ent->takedamage) 
							&& rs_trace.ent->classname[0] != 'p'
							&& rs_trace.ent->classname[0] != 'b')
						{
//gi.bprintf(PRINT_HIGH,"shushu!\n");
							zc->rt_locktime = level.time + FRAMETIME * POD_LOCKFRAME;
							it_ent = rs_trace.ent;
							k = true;
						}
					}
				}

				//トリガを撃つ
				if(k && !(ent->client->buttons & BUTTON_ATTACK))
				{
//gi.bprintf(PRINT_HIGH,"ooooooo!\n");
					trmin[0] = (it_ent->absmin[0] + it_ent->absmax[0])/2;
					trmin[1] = (it_ent->absmin[1] + it_ent->absmax[1])/2;
					trmin[2] = (it_ent->absmin[2] + it_ent->absmax[2])/2;

					//if button
					if(k == 2)
					{
						VectorSet(touchmin, 0, 0, ent->viewheight-8);
						VectorAdd(ent->s.origin,touchmin,touchmin);

						rs_trace = gi.trace(it_ent->union_ent->s.origin,NULL,NULL,trmin,it_ent->union_ent,MASK_SHOT);
						VectorSubtract(rs_trace.endpos,ent->s.origin,trmax);
					}
					else VectorSubtract(v,ent->s.origin,trmax);

//gi.bprintf(PRINT_HIGH,"shoot!\n");
					//爆発モノの時は持ち替え
					i = Get_KindWeapon(ent->client->pers.weapon);
					if(!zc->first_target && it_ent->takedamage)
					{
						if(i == WEAP_GRENADES 
							|| i == WEAP_GRENADELAUNCHER 
							|| i == WEAP_ROCKETLAUNCHER 
							|| i == WEAP_PHALANX
							|| i == WEAP_BFG)
						{
							item = Fdi_BLASTER;//FindItem("Blaster");
							item->use(ent,item);
//if(ent->client->newweapon) gi.bprintf(PRINT_HIGH,"selected %s\n",ent->client->newweapon->pickup_name);
						}
					}
					if(!zc->first_target || it_ent->takedamage)
					{
						ent->s.angles[YAW] = Get_yaw(trmax);
						ent->s.angles[PITCH] = Get_pitch(trmax);
					}
					if(it_ent->takedamage) ent->client->buttons |= BUTTON_ATTACK;
					if(k == 2)
					{
						if(it_ent->moveinfo.state != PSTATE_TOP) goto VCHCANSEL;
					}
					else
					{
						if(!TraceX(ent,v)) goto VCHCANSEL;
					}
					//if(j)goto VCHCANSEL;
				}

				if(Route[zc->routeindex].state == GRS_ONTRAIN && !zc->waterstate /*< WAS_IN*/
					/*ent->groundentity*/)
				{
					Get_RouteOrigin(zc->routeindex -1 ,trmin);
					if((trmin[2] - ent->s.origin[2]) > /*2*/JumpMax
						&& (v[2] - ent->s.origin[2]) > JumpMax
						&& ent->waterlevel < 3)
					{
						zc->route_trace = false;
#ifdef _DEBUG
gi.bprintf(PRINT_HIGH,"OFF 10\n");
#endif
					}
/*					if(!Q_stricmp(ent->groundentity->classname, "func_train"))
					{ 
						VectorCopy(ent->groundentity->union_ent->s.origin,v);
						if(VectorLength(v) < 16 && (zc->routeindex + 1) < CurrentIndex)
						{
							zc->routeindex++;
							Get_RouteOrigin(zc->routeindex,v);
						}

					}*/

				}
				if(zc->waterstate == WAS_IN) f2 = 20;
				else if(ent->groundentity) f2 = -8;
				else f2 = 0;
				if(ent->client->ps.pmove.pm_flags & PMF_DUCKED) f1 = -16;
				else
				{
					if(zc->waterstate == WAS_IN) f1 = 24;
					else if(/*zc->waterstate == WAS_FLOAT*/ent->waterlevel && ent->waterlevel < 3)
					{
						if(v[0] == ent->s.origin[0] && v[1] == ent->s.origin[1]) f1 = -300;
						else f1 = -(JumpMax + 64);
					}
					else f1 = -(JumpMax + 64);
				}
//到達チェック
				if( Route[zc->routeindex].state == GRS_ONROTATE) yaw = -48;
				else yaw = 12;
				if(v[0] <= (ent->absmax[0] - yaw) && v[0] >= (ent->absmin[0] + yaw))
				{
					if(v[1] <= (ent->absmax[1] - yaw) && v[1] >= (ent->absmin[1] + yaw))
					{
						if((v[2] <= (ent->absmax[2] - f1) && v[2] >= (ent->absmin[2] + f2))
							|| Route[zc->routeindex].state == GRS_ONROTATE
							/*|| zc->waterstate == WAS_FLOAT*/)
						{
							if(zc->routeindex < CurrentIndex /*&& TraceX(ent,Route[zc->routeindex + 1].Pt)*/)
							{
//アイテムリンクチェック1>>
								if(Route[zc->routeindex].state <= GRS_ITEMS)
								{
									if(zc->havetarget)
									{
										for(i = 0;i < (MAXLINKPOD - (ctf->value != 0));i++)
										{
											k = Route[zc->routeindex].linkpod[i];
											if(!k) break;
//gi.bprintf(PRINT_HIGH,"tryto change index1\n");
											if(k > zc->routeindex && k < zc->targetindex)
											{
//gi.bprintf(PRINT_HIGH,"change index1\n");		
												if(1/*!ctf->value*/)
												{
													zc->routeindex = k;
													break;
												}
											}
										}
									}
									else if(random() < 0.2 && !ctf->value)
									{
										for(i = 0;i < (MAXLINKPOD - (ctf->value != 0));i++)
										{
											k = Route[zc->routeindex].linkpod[i];
											if(!k) break;
											if(k > zc->routeindex && k < zc->targetindex)
											{
												if(random() < 0.5)
												{
													zc->routeindex = k;
													break;
												}
											}
										}									
									}
								}
//アイテムリンクチェック<<
								zc->routeindex++;
								//not a normal pod
								if(zc->routeindex < CurrentIndex)
								{
									if(Route[zc->routeindex].state != GRS_NORMAL && Route[zc->routeindex].ent)
									{
										//when items
										if(0/*Route[zc->routeindex].state == GRS_ITEMS*/)
										{
											zc->second_target = Route[zc->routeindex].ent;
											//zc->routeindex++;
										}
									}
								}
								else zc->routeindex = 0;
							}
						}
					}
				}

				if(zc->routeindex < CurrentIndex 
					&& trace_priority && zc->second_target == NULL)
				{
					if(1/*TraceX(ent,Route[zc->routeindex].Pt)*/)
					{
						Get_RouteOrigin(zc->routeindex,v);

						VectorSubtract(v,ent->s.origin,temppos);
						if(trace_priority < TRP_ANGLEKEEP) ent->s.angles[PITCH] = Get_pitch(temppos);

						k = false;
//						if(zc->waterstate != WAS_IN && temppos[2] < 32) k = true;
//						else if(zc->waterstate == WAS_IN) k = true;

						if(ent->groundentity /*|| ent->waterlevel ) &&  
							temppos[2] < 32 || zc->waterstate != WAS_IN)*/ || ent->waterlevel/*zc->waterstate*/ )
						{
							k = false;
							yaw = temppos[2];
							temppos[2] = 0;
							x = VectorLength(temppos);
//							if(yaw > JumpMax) k = true;

							if(!k)
							{
								k = false;
								if(trace_priority < TRP_MOVEKEEP) zc->moveyaw = Get_yaw(temppos);	//set the movin' yaw
								if((ent->groundentity || ent->waterlevel) && trace_priority < TRP_ANGLEKEEP) {ent->s.angles[YAW] = zc->moveyaw; k = true;}

								if(x < dist && fabs(yaw) < 20/*JumpMax*/&& k)
								{
									iyaw = Get_yaw(temppos);
									i = Bot_moveT (ent,iyaw,temppos,x,&bottom);
									rs_trace = gi.trace(/*ent->s.origin*/v,ent->mins,ent->maxs,v,ent,MASK_BOTSOLIDX);
									
									if(Route[zc->routeindex].state == GRS_ITEMS && !i)
									{
										if(x < 30) zc->routeindex++;
									}

									else if((Route[zc->routeindex].state == GRS_ITEMS
										|| Route[zc->routeindex].state == GRS_NORMAL)
										&& !rs_trace.allsolid && !rs_trace.startsolid
										&& HazardCheck(ent,v)
										&& fabs(bottom) < 20 && i && !ent->waterlevel/*&& rs_trace.fraction == 1.0*/)
									{
										j = false;
										if(v[2] < ent->s.origin[2] && bottom < 0) j = true;
										else if(v[2] >= ent->s.origin[2] && bottom >= 0) j = true;
										if(j)
										{
											VectorCopy(temppos,ent->s.origin);
											VectorCopy(v,trmin);
											dist -= x;
//アイテムリンクチェック2>>
											if(Route[zc->routeindex].state <= GRS_ITEMS)
											{
												if(zc->havetarget)
												{
													for(i = 0;i < (MAXLINKPOD - (ctf->value != 0));i++)
													{
														j = Route[zc->routeindex].linkpod[i];
														if(!j) break;
//gi.bprintf(PRINT_HIGH,"tryto change index2\n");
														if(j > zc->routeindex && j < zc->targetindex)
														{
//gi.bprintf(PRINT_HIGH,"change index2\n");
															zc->routeindex = j;
															break;
														}
													}
												}
											}
//アイテムリンクチェック<<
											zc->routeindex++;
											if(i == 2) ent->client->ps.pmove.pm_flags |= PMF_DUCKED;
										
											Get_RouteOrigin(zc->routeindex,v);
											VectorSubtract(v,ent->s.origin,temppos);
											if(trace_priority < TRP_ANGLEKEEP) ent->s.angles[PITCH] = Get_pitch(temppos);
											if(trace_priority < TRP_MOVEKEEP) zc->moveyaw = Get_yaw(temppos);
											if(k && trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] = zc->moveyaw;
										}
									}
									else if((Route[zc->routeindex].state == GRS_ITEMS
										|| Route[zc->routeindex].state == GRS_NORMAL)
										&& fabs(bottom) < 20 && ent->waterlevel
										/*&& !(zc->zcstate & STS_LADDERUP)*/)
									{
										j = false;
										if(v[2] < ent->s.origin[2] && bottom < 0) j = true;
										else if(v[2] >= ent->s.origin[2] && bottom >= 0) j = true;
										if(j)
										{
											VectorCopy(temppos,ent->s.origin);
											//VectorCopy(v,ent->s.origin);
											VectorCopy(v,trmin);
											dist -= x;
											zc->routeindex++;
											Get_RouteOrigin(zc->routeindex,v);
											VectorSubtract(v,ent->s.origin,temppos);
											if(trace_priority < TRP_ANGLEKEEP) ent->s.angles[PITCH] = Get_pitch(temppos);
											if(trace_priority < TRP_MOVEKEEP) zc->moveyaw = Get_yaw(temppos);
											if(k && trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] = zc->moveyaw;
										}
										else dist = x;
									}
									else dist = x; 
								}
								else if(x < dist) dist = x;
							}

							k = false;
							if((zc->routeindex - 1) >= 0 &&
								(Route[zc->routeindex].state == GRS_ONPLAT 
								|| Route[zc->routeindex].state == GRS_ONTRAIN))
							{
								Get_RouteOrigin(zc->routeindex - 1,v);
								if(fabs(v[2] - ent->s.origin[2]) <= JumpMax)
								{
									if(zc->waterstate < WAS_IN
										/*&& Route[zc->routeindex].ent->trainteam == NULL*/
										&& Route[zc->routeindex].ent->nextthink > level.time) k = true;
								}

							}
							if(k && !(zc->zcstate & STS_WAITS)) 
							{
								if((zc->routeindex + 1) < CurrentIndex)
								{
									Get_RouteOrigin(zc->routeindex + 1,v);
									if((v[2] - ent->s.origin[2]) > JumpMax)
									{
										if((Route[zc->routeindex].ent->union_ent->s.origin[2]
											- ent->s.origin[2]) > JumpMax
											/*|| !TraceX(ent,v)*/)
										{
											zc->waitin_obj = Route[zc->routeindex].ent;
											zc->zcstate |= STS_W_COMEPLAT;
											k = false;
											for(i = 1;i <=3;i++)
											{
												if(zc->routeindex - i >= 0)
												{
													Get_RouteOrigin(zc->routeindex - i,v);
													if(zc->waitin_obj->absmax[0] < (v[0] + ent->mins[0])) k = true; 
													else if(zc->waitin_obj->absmax[1] < (v[1] + ent->mins[1])) k = true;
													else if(zc->waitin_obj->absmin[0] > (v[0] + ent->maxs[0])) k = true;
													else if(zc->waitin_obj->absmin[1] > (v[1] + ent->maxs[1])) k = true;
													if(k) break;
												}
											}
											if(k) VectorCopy(v,zc->movtarget_pt);
											else Get_RouteOrigin(zc->routeindex - 1,zc->movtarget_pt);

											goto VCHCANSEL;
										}
									}
								}
							}

						}
						//ent->s.angles[YAW] = zc->moveyaw;
					}
				}
				else if(zc->routeindex >= CurrentIndex)
				{
#ifdef _DEBUG
gi.bprintf(PRINT_HIGH,"OFF 6\n"); //ppx
#endif
					zc->routeindex = 0;
					zc->route_trace = false;				
				}
			}
			else
			{
#ifdef _DEBUG
gi.bprintf(PRINT_HIGH,"OFF 7\n"); //ppx
#endif
				zc->routeindex = 0;
				zc->route_trace = false;				
			}
		}
	}
	else
	{
#ifdef _DEBUG
gi.bprintf(PRINT_HIGH,"OFF 8\n"); //ppx
#endif
		zc->route_trace = false;
	}

	//--------------------------------------------------------------------------------------
	//ground entity check
	//
	//
	//
	//	あしもと確認
	//
	//
	//--------------------------------------------------------------------------------------
	
	if(!(zc->zcstate & STS_W_DOOROPEN ) && (!ent->groundentity || ent->groundentity != zc->waitin_obj))
	{
		k = false;
		if(zc->waitin_obj) if(Q_stricmp (zc->waitin_obj->classname,"func_door")) k = true;

		if(!k)
		{
			zc->zcstate &= ~STS_WAITS;
			zc->waitin_obj = NULL; 
		}
	}

	if(ent->groundentity && /*!j &&*/ !(zc->zcstate & STS_WAITS) )
	{
		it_ent = ent->groundentity;
		if( it_ent->classname[0] == 'f')
		{
			if(Q_stricmp (it_ent->classname, "func_plat") == 0)
			{
/*if(it_ent->moveinfo.state == PSTATE_UP && it_ent->nextthink <= level.time )
gi.bprintf(PRINT_HIGH,"aw shit!!\n");*/
				if(it_ent->pos1[2] > it_ent->pos2[2] 
					&& ((it_ent->moveinfo.state == PSTATE_UP && it_ent->velocity[2] > 0 ) || it_ent->moveinfo.state == PSTATE_BOTTOM)
					/*&& it_ent->s.origin[2] != it_ent->s.old_origin[2]*/)
				{
//gi.bprintf(PRINT_HIGH,"osssre onplat!!\n");
					zc->waitin_obj = it_ent;
					zc->zcstate |= STS_W_ONPLAT;
					if(zc->route_trace)
					{
//gi.bprintf(PRINT_HIGH,"ore onplat!!\n");
						if(Route[zc->routeindex].ent == zc->waitin_obj
							&& Route[zc->routeindex].state == GRS_ONPLAT)
						{
//gi.bprintf(PRINT_HIGH,"YEAH onplat!!\n");
							if(zc->waitin_obj->union_ent->s.origin[2] > (ent->s.origin[2] + 32))
							{
								zc->zcstate &= ~STS_W_ONPLAT;
								zc->zcstate |= STS_W_COMEPLAT;
							}
							else zc->routeindex++;
						}
					/*	for(i = 0;i < 10;i++)
						{
							if((zc->routeindex + i) >= CurrentIndex) break;
							if(!Route[zc->routeindex + i].index) break;
							if(Route[zc->routeindex + i].state == GRS_ONPLAT 
								&& Route[zc->routeindex + i].ent == zc->waitin_obj)
							{
								zc->routeindex += i + 1;
								break;
							}
						}*/
					}
				}
			}
			//on train
			else if(Q_stricmp (it_ent->classname, "func_train") == 0
				&& it_ent->nextthink >= level.time 
				&& ((it_ent->s.origin[2] - it_ent->s.old_origin[2]) > 0
				|| zc->route_trace))
//				&& abs(it_ent->moveinfo.start_origin[2] - it_ent->moveinfo.end_origin[2]) > 54)
			{
//gi.bprintf(PRINT_HIGH,"challenge!!\n");
				//route trace on
				if(zc->route_trace && zc->routeindex > 0)
				{
					j = false;
					k = zc->routeindex - 1;
					for(i = 0;i < 3;i++)
					{
						if((k + i) < CurrentIndex)
						{
							if(Route[k + i].state == GRS_ONTRAIN)
							{
								if(Route[k + i].ent == it_ent) j = true;
								else if(it_ent->trainteam != NULL)
								{
									e = it_ent->trainteam; 
									while(1)
									{
										if(e == it_ent)
										{
											break;
										}
										if(e == Route[k + i].ent)
										{
											j = true;
											it_ent = e;
											Route[k + i].ent = e;
											break;
										}
										e = e->trainteam;
									}
								}
								else if(/*e*/it_ent->target_ent)
								{
									if(VectorCompare(Route[k + i].Tcourner,/*e*/it_ent->target_ent->s.origin))
									{
										j = true;
										//it_ent = e;
										break;									
									}
								}
								if(j) break;
							}
						}
						else break;
					}
					if(j)
					{
//gi.bprintf(PRINT_HIGH,"On train1!!\n");
						zc->zcstate |= STS_W_ONTRAIN;
						zc->waitin_obj = it_ent;
						zc->routeindex = k + i + 1;
					}
				}
/*					if(Route[zc->routeindex - 1].state == GRS_ONTRAIN
//						&& it_ent->trainteam == NULL
						&& it_ent == Route[zc->routeindex - 1].ent)
					{
							zc->zcstate |= STS_W_ONTRAIN;
							zc->waitin_obj = it_ent;
					}
					else if(Route[zc->routeindex].state == GRS_ONTRAIN
						&& it_ent == Route[zc->routeindex].ent
						&& zc->routeindex + 1 < CurrentIndex)
					{
						Get_RouteOrigin(zc->routeindex + 1,v);
						if(!TraceX(ent,v))
						{
							zc->zcstate |= STS_W_ONTRAIN;
							zc->waitin_obj = it_ent;
						}
						zc->routeindex++;
					}
					else if(Route[zc->routeindex].state == GRS_ONTRAIN
						&& it_ent->trainteam
						&& zc->routeindex + 1 < CurrentIndex)
					{
						Get_RouteOrigin(zc->routeindex + 1,v);
						if(!TraceX(ent,v))
						{
							k = false;
							e = it_ent->trainteam; 
							while(1)
							{
								if(e == it_ent) break;
								if(e == Route[zc->routeindex].ent)
								{
									k = true;
									break;
								}
								e = e->trainteam;
							}
							if(k)
							{
								zc->zcstate |= STS_W_ONTRAIN;
								zc->waitin_obj = it_ent;
								zc->routeindex++;
							}
						}
					}
				}*/
				else
				{
//					if(it_ent->moveinfo.start_origin[2] > it_ent->moveinfo.end_origin[2]) x = it_ent->moveinfo.end_origin[2];
//					else x = it_ent->moveinfo.start_origin[2];

					if((it_ent->s.origin[2] - it_ent->s.old_origin[2]) > 0
						/*|| (it_ent->s.origin[2] == it_ent->s.old_origin[2] && x == it_ent->s.origin[2])*/ )
					{
//gi.bprintf(PRINT_HIGH,"On train2!!\n");
						zc->zcstate |= STS_W_ONTRAIN;
						zc->waitin_obj = it_ent;
					}
					else if((it_ent->s.origin[2] - it_ent->s.old_origin[2]) > -2
						&& trace_priority && zc->second_target == NULL)
					{
//gi.bprintf(PRINT_HIGH,"On train3!!\n");
						zc->zcstate |= STS_W_ONTRAIN;
						zc->waitin_obj = it_ent;					
					}
					else zc->zcstate |= STS_W_DONT;
				}
			}
		}
	}

	if((zc->zcstate & STS_W_DONT) && ent->groundentity)
	{
		if(zc->zcstate & STS_W_ONPLAT)
		{
			if(Q_stricmp (ent->groundentity->classname, "func_plat"))
			{
				zc->zcstate &= ~STS_WAITS;
				zc->waitin_obj = NULL;
			}
		}
		else if(zc->zcstate & STS_W_ONTRAIN)
		{
			if(Q_stricmp (ent->groundentity->classname, "func_train"))
			{
				zc->zcstate &= ~STS_WAITS;
				zc->waitin_obj = NULL;
			}
		}
		else if(zc->zcstate & (STS_W_ONDOORUP | STS_W_ONDOORDWN))
		{
			if(Q_stricmp (ent->groundentity->classname, "func_door"))
			{
				zc->zcstate &= ~STS_WAITS;
				zc->waitin_obj = NULL;
			}
		}
		else 
		{
			zc->zcstate &= ~STS_WAITS;
			zc->waitin_obj = NULL;
		}
	}


	//on plat
	else if((
		(zc->zcstate & STS_W_ONPLAT)
		|| (zc->zcstate & STS_W_COMEPLAT)
		|| (zc->zcstate & STS_W_ONDOORUP)
		|| (zc->zcstate & STS_W_ONDOORDWN)) && !(zc->zcstate & STS_W_DONT))
	{
		k = false;
		//if door
		if(zc->zcstate & (STS_W_ONDOORUP | STS_W_ONDOORDWN) )
		{
			// up
			if(zc->zcstate & STS_W_ONDOORUP)
			{
				if(zc->waitin_obj->moveinfo.state == PSTATE_UP
					|| zc->waitin_obj->moveinfo.state == PSTATE_BOTTOM) k = true;
			}
			// down
			else 
			{
				if(zc->waitin_obj->moveinfo.state == PSTATE_TOP
					|| zc->waitin_obj->moveinfo.state == PSTATE_DOWN) k = true;				
			}
		}
		else if(zc->zcstate & STS_W_COMEPLAT) 
		{
			if(Route[zc->routeindex].state == GRS_ONTRAIN)
			{
				if(!TraceX(ent,/*zc->waitin_obj*/Route[zc->routeindex].ent->union_ent->s.origin)) k = true;
				if((/*zc->waitin_obj*/Route[zc->routeindex].ent->union_ent->s.origin[2]
								+ 8 - ent->s.origin[2]) > JumpMax) k = true;
			}
			else
			{
				if((zc->waitin_obj->union_ent->s.origin[2]
											- ent->s.origin[2]) > JumpMax) k = true;
			}		
//			if(zc->waitin_obj->velocity[2] == 0) k = false;
			if(zc->routeindex - 1 > 0 && zc->waterstate < WAS_IN)
			{
				Get_RouteOrigin(zc->routeindex -1 ,trmin);
				if((trmin[2] - ent->s.origin[2]) > JumpMax
						&& (v[2] - ent->s.origin[2]) > JumpMax)
//				Get_RouteOrigin(zc->routeindex - 1,v);
//				if((v[2] - ent->s.origin[2]) > JumpMax) 
					k = false;
			}
		}
		else 
		{
			if(/*!k &&*/ zc->waitin_obj->moveinfo.state == PSTATE_UP
				|| zc->waitin_obj->moveinfo.state == PSTATE_BOTTOM) k = true;

			if(zc->waitin_obj->moveinfo.state == PSTATE_BOTTOM) plat_go_up (zc->waitin_obj);

			if(zc->route_trace)
			{
				Get_RouteOrigin(zc->routeindex,v);
				if(ent->s.origin[2] > v[2] ) k = 2;
			}
		}
		//have target
		if(/*j ||*/ k != true)
		{
			if(k == 2) zc->zcstate |= STS_W_DONT;
			else
			{
				zc->zcstate &= ~STS_WAITS;
				zc->waitin_obj = NULL;
			}
		}
		else 
		{
			if(zc->zcstate & STS_W_COMEPLAT)
			{
				k = false;
				if(zc->routeindex -1 >0)
				{
					if(1/*Route[zc->routeindex - 1].state <= GRS_ITEMS*/)
					{
						//Get_RouteOrigin(zc->routeindex - 1,trmax);
						VectorCopy(zc->movtarget_pt,trmax);
						trmax[2] = 0;
						k = true;
					}
				}
				if(!k) goto VCHCANSEL;
			}
			else
			{
				trmax[0] = (zc->waitin_obj->absmin[0] + zc->waitin_obj->absmax[0]) / 2;
				trmax[1] = (zc->waitin_obj->absmin[1] + zc->waitin_obj->absmax[1]) / 2;
				trmax[2] = 0;
			}
			VectorSubtract(trmax,ent->s.origin,temppos);
			yaw = temppos[2];
			temppos[2] = 0;
			x = VectorLength(temppos);
			if( x == 0) goto VCHCANSEL;	//if center position move cancel
			if( x < dist) dist = x;
				
			if(trace_priority < TRP_MOVEKEEP) zc->moveyaw = Get_yaw(temppos);
		}
	}
	// on train
	else if(zc->zcstate & STS_W_ONTRAIN)
	{
		i = false;
	
		if(zc->route_trace)
		{
			Get_RouteOrigin(zc->routeindex,v);

			if((zc->routeindex - 1) >= 0) 
			{
				if(Route[zc->routeindex - 1].state != GRS_ONTRAIN) i = true;
			}
			else i = true;

			if(TraceX(ent,v))
			{
				x = v[2] - /*zc->waitin_obj->union_ent->s.origin[2];*/ent->s.origin[2];
				if(x <= JumpMax)
				{
//gi.bprintf(PRINT_HIGH,"released!!! %f %i\n",x,Route[zc->routeindex].state);
					i = true;
				}
				else zc->rt_locktime = level.time + FRAMETIME * POD_LOCKFRAME;
			}
			else zc->rt_locktime = level.time + FRAMETIME * POD_LOCKFRAME;
		}
		else if(j || (zc->waitin_obj->s.origin[2] - zc->waitin_obj->s.old_origin[2]) <= 0 ) i = true;

		if(i)
		{
			zc->zcstate |= STS_W_DONT;
			zc->zcstate &= ~STS_WAITS;
//			zc->waitin_obj = NULL;
		}
		else 
		{
			k = false;
			if(zc->route_trace)
			{
				rs_trace = gi.trace(ent->s.origin,NULL,NULL,v,ent,MASK_BOTSOLIDX/*MASK_PLAYERSOLID*/);
				if(rs_trace.ent == zc->waitin_obj )
				{
					rs_trace = gi.trace(v,NULL,NULL,ent->s.origin,ent,MASK_BOTSOLIDX/*MASK_PLAYERSOLID*/);
					if(rs_trace.ent == zc->waitin_obj )
					{
						VectorSubtract(v,ent->s.origin,temppos);
						k = true;
					}
				}
			}
			if(!k)
			{
//gi.bprintf(PRINT_HIGH,"ponko1!\n");
				VectorCopy(zc->waitin_obj->union_ent->s.origin,trmax);
				//trmax[0] = (zc->waitin_obj->absmin[0] + zc->waitin_obj->absmax[0]) / 2;
				//trmax[1] = (zc->waitin_obj->absmin[1] + zc->waitin_obj->absmax[1]) / 2;
				trmax[2] += 8;
				VectorSubtract(trmax,ent->s.origin,temppos);
				yaw = temppos[2];
				temppos[2] = 0;
				x = VectorLength(temppos);

//gi.bprintf(PRINT_HIGH,"ponko2! %f < %f\n",x,dist);

				if( x < dist  /*MOVE_SPD_RUN*/)
				{
					dist = x;
//					goto VCHCANSEL;	//if center position move cancel
				}
			}
			if(trace_priority < TRP_MOVEKEEP) zc->moveyaw = Get_yaw(temppos);
		}
		goto GOMOVE;
	}
	//wait for door open
	else if(zc->zcstate & STS_W_DOOROPEN)
	{
		if(!trace_priority
			|| zc->waitin_obj->moveinfo.state == PSTATE_TOP 
			/*|| (zc->waitin_obj->moveinfo.state == PSTATE_DOWN)*/)
		{
//gi.bprintf(PRINT_HIGH,"release %i %i\n",trace_priority,zc->waitin_obj->moveinfo.state);
			zc->zcstate &= ~STS_WAITS;
			zc->waitin_obj = NULL; 
		}
		else if(zc->waitin_obj->moveinfo.state == PSTATE_BOTTOM 
			|| zc->waitin_obj->moveinfo.state == PSTATE_UP)
		{
			VectorSubtract(zc->movtarget_pt,ent->s.origin,temppos);
			temppos[2] = 0;
			x = VectorLength(temppos);
			dist *= 0.25;
			if(x < 10 || VectorCompare(ent->s.origin,zc->movtarget_pt))
			{
//				if(abs(zc->waitin_obj->s.origin[2] - zc->waitin_obj->s.old_origin[2]) == 0) goto VCHCANSEL;

				if(!zc->waitin_obj->union_ent)
				{
					trmin[0] = (zc->waitin_obj->absmin[0] + zc->waitin_obj->absmax[0]) / 2;
					trmin[1] = (zc->waitin_obj->absmin[1] + zc->waitin_obj->absmax[1]) / 2;
					trmin[2] = (zc->waitin_obj->absmin[2] + zc->waitin_obj->absmax[2]) / 2;
				}
				else VectorCopy(zc->waitin_obj->union_ent->s.origin,trmin);
				trmin[2] += 8;
				VectorSubtract(trmin,ent->s.origin,temppos);
				if(trace_priority < TRP_MOVEKEEP) zc->moveyaw = Get_yaw(temppos);
				if(trace_priority < TRP_ANGLEKEEP)
				{
					ent->s.angles[YAW] = zc->moveyaw;
					ent->s.angles[PITCH] = Get_pitch(temppos);
				}
				goto VCHCANSEL;
			}
			else 
			{
				if(trace_priority < TRP_MOVEKEEP) zc->moveyaw = Get_yaw(temppos);
				if(!zc->waitin_obj->union_ent)
				{
					trmin[0] = (zc->waitin_obj->absmin[0] + zc->waitin_obj->absmax[0]) / 2;
					trmin[1] = (zc->waitin_obj->absmin[1] + zc->waitin_obj->absmax[1]) / 2;
					trmin[2] = (zc->waitin_obj->absmin[2] + zc->waitin_obj->absmax[2]) / 2;
				}
				else VectorCopy(zc->waitin_obj->union_ent->s.origin,trmin);

				trmin[2] += 8;
				VectorSubtract(trmin,ent->s.origin,temppos);
				if(trace_priority < TRP_ANGLEKEEP)
				{
					ent->s.angles[YAW] = Get_yaw(temppos);
					ent->s.angles[PITCH] = Get_pitch(temppos);
				}
			}
		}
	}

//LADDER
	

	//--------------------------------------------------------------------------------------
	//ladder check
/*	front = NULL, left = NULL, right = NULL;
	k = false;
	if(zc->route_trace)
	{
		Get_RouteOrigin(zc->routeindex,v);
		if((v[2] - ent->s.origin[2]) >= 32) k = true;
			
	}
	if(k && !zc->first_target && !zc->second_target && !(ent->client->ps.pmove.pm_flags & PMF_DUCKED))
	{
		tempflag = false;

		VectorCopy(ent->mins,trmin);
		VectorCopy(ent->maxs,trmax);

		trmin[2] += 20;

		//front
		f1 = 32;
		if(zc->route_trace) f1 = 32;

		iyaw = zc->moveyaw;
		yaw = iyaw * M_PI * 2 / 360;
		touchmin[0] = cos(yaw) * f1;//28 ;
		touchmin[1] = sin(yaw) * f1;
		touchmin[2] = 0;

		VectorAdd(ent->s.origin,touchmin,touchmax);
		rs_trace = gi.trace (ent->s.origin, trmin,ent->maxs, touchmax,ent, MASK_BOTSOLID );
		front = rs_trace.ent;

		if(rs_trace.contents & CONTENTS_LADDER) tempflag = true;

		//right
		if(!tempflag)
		{
			iyaw = zc->moveyaw + 90;
			if(iyaw > 180) iyaw -= 360;
			yaw = iyaw * M_PI * 2 / 360;
			touchmin[0] = cos(yaw) * 32 ;
			touchmin[1] = sin(yaw) * 32 ;
			touchmin[2] = 0;

			VectorAdd(ent->s.origin,touchmin,touchmax);
			rs_trace = gi.trace (ent->s.origin, trmin,ent->maxs, touchmax,ent,  MASK_BOTSOLID );
			right = rs_trace.ent;

			if(rs_trace.contents & CONTENTS_LADDER) tempflag = true;
		}
		//left
		if(!tempflag)
		{
			iyaw = zc->moveyaw - 90;
			if(iyaw < -180) iyaw += 360;
			yaw = iyaw * M_PI * 2 / 360;
			touchmin[0] = cos(yaw) * 32 ;
			touchmin[1] = sin(yaw) * 32 ;
			touchmin[2] = 0;

			VectorAdd(ent->s.origin,touchmin,touchmax);
			rs_trace = gi.trace (ent->s.origin, trmin,ent->maxs, touchmax,ent, MASK_BOTSOLID );
			left = rs_trace.ent;

			if(rs_trace.contents & CONTENTS_LADDER)	tempflag = true;
		}

		//ladder
		if(tempflag)
		{
			VectorCopy(rs_trace.endpos,trmax);
			VectorCopy(trmax,touchmax);
			touchmax[2] += 8190;
			rs_trace = gi.trace (temppos, trmin,ent->maxs, touchmax,ent, MASK_BOTSOLID );

			VectorCopy(rs_trace.endpos,temppos);
			VectorAdd(rs_trace.endpos,touchmin,touchmax);
			rs_trace = gi.trace (temppos, trmin,ent->maxs, touchmax,ent, MASK_BOTSOLID);
			
			if(!(rs_trace.contents & CONTENTS_LADDER) && rs_trace.fraction)
			{
//	gi.WriteByte (svc_temp_entity);
//	gi.WriteByte (TE_RAILTRAIL);
//	gi.WritePosition (ent->s.origin);
//	gi.WritePosition (temppos);
//	gi.multicast (ent->s.origin, MULTICAST_PHS);

				ent->velocity[0] = 0;
				ent->velocity[1] = 0;
				if(zc->moveyaw == iyaw)
				{
gi.bprintf(PRINT_HIGH,"ladder On!\n");
					ent->s.angles[YAW] = zc->moveyaw;
					VectorCopy(trmax,ent->s.origin);
					zc->zcstate |= STS_LADDERUP; 
					ent->s.angles[YAW] = zc->moveyaw;
					ent->s.angles[PITCH] = -29;

					if(ent->waterlevel > 1)
					{
						ent->velocity[2] = VEL_BOT_WLADRUP;
//				if(VectorCompare(ent->s.origin,ent->s.old_origin)) ent->velocity[2] += 50;
					}
					else
					{
						ent->velocity[2] = VEL_BOT_LADRUP;
//			if(VectorCompare(ent->s.origin,ent->s.old_origin)) ent->velocity[2] += 50;
					}
//					gi.bprintf(PRINT_HIGH,"ladder!!\n");
					goto VCHCANSEL;
				}
				else
				{
					zc->moveyaw = iyaw;
					ent->s.angles[YAW] = zc->moveyaw;
				}
				goto VCHCANSEL;
			}
		}
	}
	
*/
	//--------------------------------------------------------------------------------------
	//rocket jump
	//	ent->client->weaponstate = WEAPON_READY;
	if (ent->groundentity && ent->client->weaponstate == WEAPON_READY && zc->second_target
		&& trace_priority < TRP_ANGLEKEEP)
	{
		if((zc->second_target->s.origin[2] - ent->s.origin[2]) > 100 
			&& ent->health > 70	&& ent->waterlevel <=1 ) 
		{
			j = false;
			v[0] = ent->s.origin[0];
			v[1] = ent->s.origin[1];
			v[2] = zc->second_target->s.origin[2];
			rs_trace = gi.trace(v,NULL,NULL,zc->second_target->s.origin,zc->second_target,MASK_SOLID);
			if(rs_trace.fraction == 1.0) j = true;

			VectorSubtract (zc->second_target->s.origin,ent->s.origin,trmin);
			VectorCopy(trmin,trmax);
			trmax[2] = 0;
			it = Fdi_ROCKETLAUNCHER;//FindItem("Rocket Launcher");
			i = ITEM_INDEX(Fdi_ROCKETS/*FindItem("Rockets")*/);

//ent->client->pers.inventory[ITEM_INDEX(it)] = 1;
//ent->client->pers.inventory[ent->client->ammo_index] = 1;
			if(	VectorLength(trmax) < 280 && canrocj && j)
			{
				VectorNormalize (trmin);
				ent->s.angles[YAW] = Get_yaw (trmin);
				zc->moveyaw = ent->s.angles[YAW];
				ent->s.angles[PITCH] = 90;
				trmin[2] -= 24;
				VectorNormalize (trmin);
//				it->use(ent,it);
				ent->client->pers.weapon = it;
				ent->client->ammo_index = i;
				ShowGun(ent);

				ent->velocity[2] += VEL_BOT_JUMP;
				zc->zcstate |= STS_ROCJ;
				gi.sound(ent, CHAN_VOICE, gi.soundindex("*jump1.wav"), 1, ATTN_NORM, 0);
				PlayerNoise(ent, ent->s.origin, PNOISE_SELF);	//pon
				Set_BotAnim(ent,ANIM_JUMP,FRAME_jump1-1,FRAME_jump6);
				ent->client->buttons |= BUTTON_ATTACK;
				goto VCHCANSEL;		//移動処理キャンセル
			}
			else zc->second_target = NULL;
		}
		else if((zc->second_target->s.origin[2] - ent->s.origin[2]) > 100)
		{
			if(ent->health <= 70 ) zc->second_target = NULL;
		}
	}

	//--------------------------------------------------------------------------------------
	//bot move to moveyaw
GOMOVE:
	//jumping	======================================================
	if(!ent->groundentity && !ent->waterlevel && !ent->client->zc.trapped)
	{
		if(ent->velocity[2] > VEL_BOT_JUMP && !(zc->zcstate & STS_SJMASKEXW)) ent->velocity[2] = VEL_BOT_JUMP;

		k = false;
		if(ent->client->ps.pmove.pm_flags & PMF_DUCKED) k = true;
		for( x = 0 ; x < 90; x += 10)
		{
			dist = MOVE_SPD_RUN * ent->moveinfo.speed;
			//right trace
			yaw = zc->moveyaw + x;
			if(yaw > 180) yaw -= 360;
			i = Bot_moveT(ent,yaw,temppos,dist,&bottom);
			if(i)// true || (i == 2 && ent->velocity > 0))
			{
				if(bottom <= 24 && bottom > 0 && ent->velocity[2] <= 10 /*&& i == true*/)
				{
					//if(ent->velocity[2] > 0 || bottom >= 0)
							VectorCopy(temppos,ent->s.origin);
					break;
				}
				//turbo
				if(!ent->waterlevel && ent->s.origin[2] > ent->s.old_origin[2]
					&& zc->route_trace
					&& !(zc->zcstate & STS_LADDERUP)
					&& !(zc->zcstate & STS_SJMASK)
					&& (zc->routeindex + 1) < CurrentIndex
					&& ent->velocity[2] >= 100 
					&& ent->velocity[2] < (100 + ent->gravity * sv_gravity->value * FRAMETIME))
				{
					Get_RouteOrigin(zc->routeindex ,v);
					Get_RouteOrigin(zc->routeindex + 1,vv);
					k = 0;

					j = Bot_moveT(ent,yaw,trmin,16,&f1);
					VectorSubtract(v,ent->s.origin,trmin);
					if((vv[2] - v[2]) > JumpMax) k = 1;
					else if((v[2] - ent->s.origin[2]) > JumpMax) k = 2;
					else if(!TargetJump_Chk(ent,vv,0) && VectorLength(trmin) < 64)
					{
//gi.bprintf(PRINT_HIGH,"dist %f!!\n",VectorLength(trmin));
						if(TargetJump_Chk(ent,vv,ent->velocity[2])) k = 1;
					}


					if(!j) k = 0;
					else if( f1 > 10 && f1 < -10) k = 0;
					if(k)
					{
						if(k == 2) VectorCopy(v,vv); 
						if(TargetJump_Turbo(ent,vv))
						{
//gi.bprintf(PRINT_HIGH,"speed %f!!\n",ent->moveinfo.speed);
//if(ent->velocity[2] > (VEL_BOT_JUMP + 100 + ent->gravity * sv_gravity->value * FRAMETIME ))
//	ent->velocity[2] = VEL_BOT_JUMP + 100 + ent->gravity * sv_gravity->value * FRAMETIME;
							VectorSubtract(vv,ent->s.origin,v);
							zc->moveyaw = Get_yaw(v);
							if(ent->velocity[2] > VEL_BOT_JUMP) zc->zcstate |= STS_TURBOJ;
							if(k == 1) zc->routeindex++;
							break;
						}
					}
				}
//				bottom > 0else ent->moveinfo.speed = 0.2;
				if(bottom <= 0)
				{
					VectorCopy(temppos,ent->s.origin);
					if(i == 2 /*&& k*/) ent->client->ps.pmove.pm_flags |= PMF_DUCKED;
					else ent->client->ps.pmove.pm_flags &= ~PMF_DUCKED;
					break;
				}
				else
				{
					ent->moveinfo.speed = 0.3;//0.2;
				}
			}
			else
			{
				ent->moveinfo.speed = 0.3;//0.2;
			}

			if(x == 0) continue;
			//left trace
			yaw = zc->moveyaw - x;
			if(yaw < -180) yaw += 360;
			i = Bot_moveT(ent,yaw,temppos,dist,&bottom);
			if(i )//== true || (i == 2 && ent->velocity > 0))
			{
				if(bottom <= 24 && bottom >0  && ent->velocity[2] <= 10 /*&& i == true*/)
				{
					//if(ent->velocity[2] > 0 || bottom >= 0)
							VectorCopy(temppos,ent->s.origin);
					break;
				}
				//turbo
				if(!ent->waterlevel && ent->s.origin[2] > ent->s.old_origin[2]
					&& zc->route_trace 
					&& !(zc->zcstate & STS_LADDERUP)
					&& !(zc->zcstate & STS_SJMASK)
					&& (zc->routeindex + 1) < CurrentIndex
					&& ent->velocity[2] >= 100 
					&& ent->velocity[2] < (100 + ent->gravity * sv_gravity->value * FRAMETIME))
				{
					Get_RouteOrigin(zc->routeindex ,v);
					Get_RouteOrigin(zc->routeindex + 1,vv);
					k = 0;

					j = Bot_moveT(ent,yaw,trmin,16,&f1);
					VectorSubtract(v,ent->s.origin,trmin);
					if((vv[2] - v[2]) > JumpMax) k = 1;
					else if((v[2] - ent->s.origin[2]) > JumpMax) k = 2;
					else if(!TargetJump_Chk(ent,vv,0) && VectorLength(trmin) < 64)
					{
//gi.bprintf(PRINT_HIGH,"dist %f!!\n",VectorLength(trmin));
						if(TargetJump_Chk(ent,vv,ent->velocity[2])) k = 1;
					}
					
					if(!j) k = 0;
					else if( f1 > 10 && f1 < -10) k = 0;
					if(k )
					{
						if(k == 2) VectorCopy(v,vv); 
						if(TargetJump_Turbo(ent,vv))
						{
//gi.bprintf(PRINT_HIGH,"speed %f!!\n",ent->moveinfo.speed);
//if(ent->velocity[2] > (VEL_BOT_JUMP + 100 + ent->gravity * sv_gravity->value * FRAMETIME ))
//	ent->velocity[2] = VEL_BOT_JUMP + 100 + ent->gravity * sv_gravity->value * FRAMETIME;
//if(ent->moveinfo.speed < 0.5) ent->moveinfo.speed = 0.5;
							VectorSubtract(vv,ent->s.origin,v);
							zc->moveyaw = Get_yaw(v);
							if(ent->velocity[2] > VEL_BOT_JUMP) zc->zcstate |= STS_TURBOJ;
							if(k == 1) zc->routeindex++;
							break;
						}
					}
				}
//				else ent->moveinfo.speed = 0.2;
				if(bottom <= 0)
				{
					VectorCopy(temppos,ent->s.origin);
					if(i == 2 /*&& k*/) ent->client->ps.pmove.pm_flags |= PMF_DUCKED;
					else ent->client->ps.pmove.pm_flags &= ~PMF_DUCKED;
					break;
				}
				else ent->moveinfo.speed = 0.3;//0.2;
			}
			else ent->moveinfo.speed = 0.3;//0.2;
		}
		if(x >= 90 /*&& ent->velocity[2] < 0*/)
		{
//gi.bprintf(PRINT_HIGH,"jump fail!\n");
			if(trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] += ((random() - 0.5) * 360);
			if(ent->s.angles[YAW]>180) ent->s.angles[YAW] -= 360;
			else if(ent->s.angles[YAW]< -180) ent->s.angles[YAW] += 360;
		} 
		goto VCHCANSEL;
	}

	// on ground or in water ======================================================
	waterjumped = false;
	if(ent->groundentity || ent->waterlevel )
	{
		if(ent->groundentity && /*zc->waterstate == WAS_NONE*/ent->waterlevel <= 0 ) k = 1;
		else if(ent->waterlevel)
		{
			k = 2;
			if(zc->route_trace)
			{
				Get_RouteOrigin(zc->routeindex,v);
				VectorSubtract(v,ent->s.origin,vv);
				vv[2] = 0;
				if(v[2] < ent->s.origin[2] && VectorLength(vv) < 24) k = 0;
			}
			if(ent->waterlevel == 3) k = 0;
		}
		else if(ent->waterlevel) k = 0;
		else k = 1;
		if(k) if(ent->client->ps.pmove.pm_flags & PMF_DUCKED) k = 0;

		if(zc->waterstate) f1 = BOTTOM_LIMIT_WATER;
		else f1 = - JumpMax;//BOTTOM_LIMIT;//dropable height

		if(zc->nextcheck < (level.time + FRAMETIME * 10))
		{
			VectorSubtract(zc->pold_origin,ent->s.origin,temppos);
			if(VectorLength(temppos) < 64)
			{
				if(zc->route_trace)
				{
					if(!chedit->value)
					{
						zc->route_trace = false;
						zc->routeindex++;
					}
					zc->second_target = NULL;
				}
				else f1 = BOTTOM_LIMITM;
			}

			if(zc->nextcheck < level.time) 
			{
				VectorCopy(ent->s.origin,zc->pold_origin);
				zc->nextcheck = level.time + FRAMETIME * 40;
			}
		}
		f3 = 20;	//movablegap
		//this v not modify till do special
		if(zc->route_trace) Get_RouteOrigin(zc->routeindex,v);

		if(ent->waterlevel && zc->route_trace)
		{
			if(v[2] + 20 <= ent->s.origin[2])
			{
				f2 = 20,f3 = 0;
			}
			else
			{
				if(zc->waterstate /*== WAS_FLOAT*/) f2 = JumpMax;//TOP_LIMIT_WATER;
				else f2 = JumpMax;
			}
		}
		else
		{
			if(zc->waterstate /*== WAS_FLOAT*/) f2 = JumpMax;//TOP_LIMIT_WATER;
			else f2 = JumpMax;
		}

//if(ent->client->ps.pmove.pm_flags & PMF_DUCKED) gi.bprintf(PRINT_HIGH,"cycle!\n");
		ladderdrop = true;
		for( x = 0 ; x <= 180 && dist != 0; x += 10)
		{
			//right trace
			yaw = zc->moveyaw + x;
			if(yaw > 180) yaw -= 360;
			if((j = Bot_moveT (ent,yaw,temppos,dist,&bottom)))
			{
				//special
				if(x == 0 && /*bottom < 20 &&*/ !ent->waterlevel
					&& !(ent->client->ps.pmove.pm_flags & PMF_DUCKED))
				{
					if( zc->second_target)
					{
						if(((zc->second_target->s.origin[2] + 8 ) - (ent->s.origin[2] + bottom)) > f2)
						{
							if(Bot_Fall(ent,temppos,dist))
							{
								ent->client->ps.pmove.pm_flags &= ~PMF_DUCKED;
								break;
							}
						}
					}
					else if( zc->route_trace)
					{
//						Get_RouteOrigin(zc->routeindex,v);
//						FRAMETIME * (ent->velocity[2] - ent->gravity * sv_gravity->value * FRAMETIME)
						if((v[2] - (ent->s.origin[2] + bottom )) > f2 ||
							(bottom > 20 && v[2] > ent->s.origin[2]))
						{
							ladderdrop = false;
							if(Bot_Fall(ent,temppos,dist) && !zc->waterstate)
							{
								ent->client->ps.pmove.pm_flags &= ~PMF_DUCKED;
								break;
							}
							if((v[2] - ent->s.origin[2]) <= JumpMax)
							{
								if(Route[zc->routeindex].state == GRS_ONTRAIN && zc->waterstate < WAS_IN) break;
								if(zc->routeindex > 0) 
									if(Route[zc->routeindex - 1].state == GRS_ONTRAIN
										&& Route[zc->routeindex - 1].ent == ent->groundentity) break;
							}
						}
						else if(ent->groundentity)
						{
//if(Q_stricmp (ent->groundentity->classname,"worldspawn"))
//gi.bprintf(PRINT_HIGH,"%s!\n",ent->groundentity->classname);
							if(!Q_stricmp (ent->groundentity->classname,"func_rotating"))
							{
								if(Bot_Fall(ent,temppos,dist))
								{
									ent->client->ps.pmove.pm_flags &= ~PMF_DUCKED;
									break;
								}
							}
							else if(Route[zc->routeindex].state == GRS_ONROTATE)
							{
								if(!TraceX(ent,v) || !HazardCheck(ent,v)) break;
								if(!BankCheck(ent,v)) break;
								if(Bot_Fall(ent,temppos,dist))
								{
									ent->client->ps.pmove.pm_flags &= ~PMF_DUCKED;
									break;
								}
							}
							if(0/*dist < 16 && v[2] < (ent->s.origin[2] - 24) 
								&& !(zc->zcstate & STS_WAITSMASK) && ent->waterlevel*/)
							{
								if(Bot_moveT (ent,yaw,trmin,32,&iyaw))
								{
									if(iyaw < 0)
									{
										ent->moveinfo.speed = 0.05;
										VectorCopy(trmin,ent->s.origin);
										break;
									}
								}	
							}
						}
					}
				}
				
				//jumpable1
				if(/*((bottom > 20 && !ent->waterlevel) || (bottom > 0 && (ent->waterlevel == 2 || (ent->waterlevel == 1 && ent->groundentity == NULL))))*/
					bottom > 20
					&& bottom <= f2 && j == true && k
					&& !(ent->client->ps.pmove.pm_flags & PMF_DUCKED))
				{
					ent->moveinfo.speed = 0.15;
					if(k == 1/*!ent->waterlevel*/)
					{
						ent->velocity[2] += VEL_BOT_JUMP;
						gi.sound(ent, CHAN_VOICE, gi.soundindex("*jump1.wav"), 1, ATTN_NORM, 0);
						PlayerNoise(ent, ent->s.origin, PNOISE_SELF);	//pon
					}
					else
					{
						ent->moveinfo.speed = 0.1;
						//waterjumped = true;
						if(ent->velocity[2] < VEL_BOT_WJUMP/*=1*/ || VectorCompare(ent->s.origin,ent->s.old_origin))
						{
							ent->velocity[2] /*+*/= VEL_BOT_WJUMP;//(/*VEL_BOT_WJUMP*/ 110 /*+ bottom*/);
							zc->zcstate |= STS_WATERJ;
							goto VCHCANSEL;
						}
						goto VCHCANSEL;
					}
					Set_BotAnim(ent,ANIM_JUMP,FRAME_jump1-1,FRAME_jump6);
					zc->moveyaw = yaw;
					ent->client->ps.pmove.pm_flags &= ~PMF_DUCKED;
					break;
				}
				//dropable1
				else if(bottom <= f3 &&(bottom >= f1 || /*zc->waterstate*/ent->waterlevel /* 2*/)) 
				{
//					ent->client->anim_priority = ANIM_BASIC;
					if(bottom < 0 && !zc->waterstate/*(ent->waterlevel && !zc->waterstate ent->waterlevel < 2)*/)
					{
						f2 = FRAMETIME * (ent->velocity[2] - ent->gravity * sv_gravity->value * FRAMETIME);
						if(bottom >= f2 && ent->velocity[2] < 0/*20*/) temppos[2] += bottom;
						else temppos[2] += f2;//20;
					}		
					VectorCopy(temppos,ent->s.origin);
					if(f1 > BOTTOM_LIMIT) ent->moveinfo.speed = 0.25;
					if(j != true)
					{
//gi.bprintf(PRINT_HIGH,"ducked1!!\n");
						ent->client->ps.pmove.pm_flags |= PMF_DUCKED;
					}
					else ent->client->ps.pmove.pm_flags &= ~PMF_DUCKED;

					if(x > 30 || !zc->route_trace)
					{
						f2 = zc->moveyaw;
						zc->moveyaw = yaw;
						if(f2 == ent->s.angles[YAW] && trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] = yaw;
					}
					break;
				}
				//dropable?1
				else if ( bottom < f1 && !zc->waterstate/*!ent->waterlevel*/ && x <= 30)
				{
					if( ladderdrop &&  zc->ground_contents & CONTENTS_LADDER && bottom != -9999) 
					{
						VectorCopy(temppos,ent->s.origin);
						zc->moveyaw = yaw;
						ent->moveinfo.speed = 0.2;
						goto VCHCANSEL;
					}

					if( ladderdrop &&  bottom < 0 && !zc->waterstate/*!ent->waterlevel*/)
					{
						if(Bot_moveW ( ent,yaw,temppos,dist,&bottom))
						{
							if(zc->second_target) iyaw = zc->second_target->s.origin[2] - ent->s.origin[2];
							else iyaw = -41;
							if(bottom > -20 && iyaw < -40)
							{
								VectorCopy(temppos,ent->s.origin);
								break;
							}
						}
					}
					//fall1
					if(Bot_Fall(ent,temppos,dist))
					{
//						ent->client->ps.pmove.pm_flags &= ~PMF_DUCKED;
//gi.bprintf(PRINT_HIGH,"drop!\n");
						break;
					}
				}
			}
//else if(ent->client->zc.waterstate == 1 && x == 0) gi.bprintf(PRINT_HIGH,"maaaap %i\n",j);

			if(x == 0 && (zc->battlemode & FIRE_SHIFT)) zc->battlemode &= ~FIRE_SHIFT;
			if(x == 0 || x == 180) continue;
			//left trace
			yaw = zc->moveyaw - x;
			if(yaw < -180) yaw += 360;
			if((j = Bot_moveT (ent,yaw,temppos,dist,&bottom)))
			{
				if(zc->waterstate == WAS_FLOAT) f2 = TOP_LIMIT_WATER;
				else f2 = JumpMax;
				//jumpable2
				if(/*((bottom > 20 && !ent->waterlevel) || (bottom > 0 && (ent->waterlevel == 2 || (ent->waterlevel == 1 && ent->groundentity == NULL))) )*/
					bottom > 20
					&& bottom <= f2 && j == true && k
					&& !(ent->client->ps.pmove.pm_flags & PMF_DUCKED))
				{
					ent->moveinfo.speed = 0.15;
					if(k == 1/*!ent->waterlevel*/)
					{
						ent->velocity[2] += VEL_BOT_JUMP;
						gi.sound(ent, CHAN_VOICE, gi.soundindex("*jump1.wav"), 1, ATTN_NORM, 0);
						PlayerNoise(ent, ent->s.origin, PNOISE_SELF);	//pon
					}
					else
					{
						ent->moveinfo.speed = 0.1;
						//waterjumped = true;
						if(ent->velocity[2] < VEL_BOT_WJUMP/*= 1*/ || VectorCompare(ent->s.origin,ent->s.old_origin))
						{
							ent->velocity[2] /*+*/= VEL_BOT_WJUMP;//(/*VEL_BOT_WJUMP*/ 110 /*+ bottom*/);
							zc->zcstate |= STS_WATERJ;
							goto VCHCANSEL;
						}
						goto VCHCANSEL;
					}
					Set_BotAnim(ent,ANIM_JUMP,FRAME_jump1-1,FRAME_jump6);
					zc->moveyaw = yaw;
					ent->client->ps.pmove.pm_flags &= ~PMF_DUCKED;
					break;
				}
				//dropable2
				else if(bottom <= f3 && (bottom >= f1 || ent->waterlevel /* 2zc->waterstate*/)) 
				{
					//ent->client->anim_priority = ANIM_BASIC;
					if(bottom < 0 && !zc->waterstate/*(ent->waterlevel && !zc->waterstate ent->waterlevel < 2)*/)
					{					
//gi.bprintf(PRINT_HIGH,"ponko\n");
						f2 = FRAMETIME * (ent->velocity[2] - ent->gravity * sv_gravity->value * FRAMETIME);
						if(bottom >= f2 && ent->velocity[2] < 0/*20*/) temppos[2] += bottom;
						else temppos[2] += f2;//20;
					}		
					VectorCopy(temppos,ent->s.origin);
					if(f1 > BOTTOM_LIMIT) ent->moveinfo.speed = 0.25;
					if(j != true)
					{
//gi.bprintf(PRINT_HIGH,"ducked2!!\n");
						ent->client->ps.pmove.pm_flags |= PMF_DUCKED;
					}
					else ent->client->ps.pmove.pm_flags &= ~PMF_DUCKED;
					
					if(x > 30 || !zc->route_trace)
					{
						f2 = zc->moveyaw;
						zc->moveyaw = yaw;
						if(f2 == ent->s.angles[YAW]  && trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] = yaw;
					}
					break;
				}
				//dropable?2
				else if (bottom < f1 && !zc->waterstate/*!ent->waterlevel*/ && x <= 30)
				{
					if( ladderdrop && zc->ground_contents & CONTENTS_LADDER  && bottom != -9999) 
					{
						VectorCopy(temppos,ent->s.origin);
						zc->moveyaw = yaw;
						ent->moveinfo.speed = 0.2;
						goto VCHCANSEL;
					}

					if( ladderdrop && bottom < 0 && !zc->waterstate/*!ent->waterlevel*/)
					{
						if(Bot_moveW ( ent,yaw,temppos,dist,&bottom))
						{
							if(zc->second_target) iyaw = zc->second_target->s.origin[2] - ent->s.origin[2];
							else iyaw = -41;
							if(bottom > -54 && iyaw < -40)
							{
								VectorCopy(temppos,ent->s.origin);
								break;
							}
						}
					}

					//fall2
					if(Bot_Fall(ent,temppos,dist))
					{
//						ent->client->ps.pmove.pm_flags &= ~PMF_DUCKED;
						break;
					}
				}
			}
		}

		if(!zc->route_trace && zc->first_target == NULL)
		{
			if(trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] = yaw;
		}

		if(x >= 70)
		{
			if(zc->second_target) zc->second_target = NULL;
/*			else if(!zc->route_trace && zc->first_target == NULL)
			{
				if(trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] = yaw;
			}*/
			else if( 0/*zc->route_trace*/)
			{
//gi.bprintf(PRINT_HIGH,"OFF 9\n"); //ppx
				k = false;
				if(  x > 90 && ent->groundentity)
				{
					if(!Q_stricmp (ent->groundentity->classname,"func_train")) k = true;
				}
				else if( x > 90 && Route[zc->routeindex].state == GRS_ONTRAIN) k = true;
				if(k && trace_priority < TRP_ANGLEKEEP) 
				{
					VectorCopy(Origin,ent->s.origin);
					VectorCopy(Velocity,ent->velocity);
					ent->s.angles[YAW] = OYaw;
					goto VCHCANSEL;
				}
/*				if(!k)
				{
					if(++zc->routeindex >= CurrentIndex) zc->routeindex = 0;
					zc->route_trace = false;
				}*/
			}
		}

		if(/*zc->waterstate*/ent->waterlevel && !waterjumped)
		{
			k = false;
			VectorCopy(ent->s.origin,temppos);
//			temppos[2] += 26;
//			i = gi.pointcontents (temppos);
			if(zc->second_target != NULL)
			{
				k = 2;
				x = zc->second_target->s.origin[2] - ent->s.origin[2];
				if(x > 13/*8*/) x = 13;//8;
				else if(x < -13/*8*/) x = -13;//8;
				if(x < 0)//アイテム下方
				{
					if( Bot_Watermove (ent,temppos,dist,x))
					{
						VectorCopy(temppos,ent->s.origin);
						k = true;
					}
				}
				else if(x >0 && zc->waterstate == WAS_IN
					&& !(ent->client->ps.pmove.pm_flags & PMF_DUCKED)) //アイテム上方
				{
					if(ent->velocity[2] < 0) ent->velocity[2] = 0; 
					if( Bot_Watermove (ent,temppos,dist,x))
					{
						VectorCopy(temppos,ent->s.origin);
						k = true;
					}
				}
			}
			else if(zc->route_trace )
			{
				Get_RouteOrigin(zc->routeindex,v);

				k = 2;
				x = v[2] - ent->s.origin[2];
				if(x > 13/*8*/) x = 13;//8;
				else if(x < -13/*8*/) x = -13;//8;
				if(x < 0)//アイテム下方
				{
					if( Bot_Watermove (ent,temppos,dist,x))
					{
//gi.bprintf(PRINT_HIGH,"Down! %f\n",x);
						VectorCopy(temppos,ent->s.origin);
						k = true;
					}
				}
				else if(x > 0 && zc->waterstate == WAS_IN
					&& !(ent->client->ps.pmove.pm_flags & PMF_DUCKED)) //アイテム上方
				{
//gi.bprintf(PRINT_HIGH,"UP! %f\n",x);
					if(ent->velocity[2] < -10) ent->velocity[2] = 0; 
					if( Bot_Watermove (ent,temppos,dist,x))
					{
						VectorCopy(temppos,ent->s.origin);
						k = true;
					}
				}
				else if(x == 0)
				{
//gi.bprintf(PRINT_HIGH,"ZERO! %f\n",x);
//					VectorSubtract(v,ent->s.origin,vv);
//					if(VectorLength(vv) < 13) VectorCopy(v,ent->s.origin);
				}
			}
			else if((ent->air_finished - FRAMETIME * 20 ) < level.time
				&& zc->waterstate == WAS_IN)
			{
				if( Bot_Watermove (ent,temppos,dist,13/*8*/))
				{
					VectorCopy(temppos,ent->s.origin);
					k = true;
				}
				else k = 2;
			}

			if(k == true) Get_WaterState(ent);
			if(zc->route_trace && v[2] == ent->s.origin[2]) k = 3;

			if((!ent->groundentity && !zc->waterstate && k && ent->velocity[2] < 1)
				||(zc->waterstate == WAS_IN && (ent->client->ps.pmove.pm_flags & PMF_DUCKED)))
			{
				if( Bot_Watermove (ent,temppos,dist,-7/*8*/) && k != 3)
				{
					VectorCopy(temppos,ent->s.origin);
				}				
			}
			if(zc->waterstate == WAS_IN)  ent->moveinfo.decel = level.time;
			else if(!k)	//水面にずっといたとき
			{
				if( ( level.time - ent->moveinfo.decel) > 4.0 && !zc->route_trace)
				{
					ent->velocity[2] = -200;
					ent->moveinfo.decel = level.time;
				}
			}

			if(ent->groundentity && ent->waterlevel == 1)
			{
				VectorSubtract(ent->s.origin,ent->s.old_origin,temppos);
				if(!temppos[0] && !temppos[1] && !temppos[2]) ent->velocity[2] += 80;
			} 
		}
		//not in water
		else if(zc->route_trace && !dist)
		{
			Get_RouteOrigin(zc->routeindex,v);
			if(v[2] < (ent->s.origin[2] - 20))
			{
				if( Bot_Watermove (ent,temppos,dist,-20))
				{
					VectorCopy(temppos,ent->s.origin);
				}
			}
		}

	}
	//sticking
/*	if(VectorCompare(ent->s.origin,ent->s.old_origin) && ent->waterlevel)
	{
		ent->velocity[2] += 200;
	}
*/
	//--------------------------------------------------------------------------------------
	//player check door and corner
	if(!zc->route_trace && trace_priority && !zc->second_target && random() < 0.2)
	{
		VectorCopy(ent->s.origin,v);
		VectorCopy(ent->mins,touchmin);
		touchmin[2] += 16;
		VectorCopy(ent->maxs,touchmax);
		if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)	touchmax[2] = 0;
		else v[2] += 20;

		//right
		if(random() < 0.5)
		{
			f1 = zc->moveyaw + 90;
			if(f1 > 180) iyaw -= 360;
			f2 = zc->moveyaw + 135;
			if(f2 > 180) iyaw -= 360;
		}
		//left
		else
		{
			f1 = zc->moveyaw - 90;
			if(f1 < 180) iyaw += 360;
			f2 = zc->moveyaw - 135;
			if(f2 < 180) iyaw += 360;
		}

		yaw = f1 * M_PI * 2 / 360;
		trmin[0] = cos(yaw) * 128 ;
		trmin[1] = sin(yaw) * 128 ;
		trmin[2] = 0;
		VectorAdd(v,trmin,trmax);
		rs_trace = gi.trace (v, NULL,NULL, trmax,ent, MASK_BOTSOLIDX/*MASK_PLAYERSOLID*/ );
		x = rs_trace.fraction;

		yaw = f2 * M_PI * 2 / 360;
		trmin[0] = cos(yaw) * 128 ;
		trmin[1] = sin(yaw) * 128 ;
		trmin[2] = 0;
		VectorAdd(v,trmin,trmax);
		rs_trace = gi.trace (v, NULL/*touchmin*/,NULL/*touchmax*/, trmax,ent, MASK_BOTSOLIDX/*MASK_PLAYERSOLID*/ );		
		
		if( x > rs_trace.fraction && x > 0.5) zc->moveyaw = f1;
	}


	//--------------------------------------------------------------------------------------
	//push button

	it_ent = NULL;
	k = 0;

	VectorCopy (ent->absmin, touchmin);
	VectorCopy (ent->absmax, touchmax);

	touchmin[0] -= 48;//32;
	touchmin[1] -= 48;//32;
	touchmin[2] -= 5;
	touchmax[0] += 48;//32;
	touchmax[1] += 48;//32;
	i = gi.BoxEdicts ( touchmin ,touchmax,touch,MAX_EDICTS,AREA_SOLID);

	if(i)
	{
		for(j = i - 1;j >= 0;j--)
		{
			trent = touch[j]; 
			if(trent->classname)
			{
				if(!Q_stricmp (trent->classname,"func_button"))
				{
					k = 1;
					it_ent = trent;
					break;
				}
				else if(!Q_stricmp (trent->classname,"func_door")
					|| !Q_stricmp (trent->classname,"func_door_rotating"))
				{
					if(trent->targetname == NULL && !trent->takedamage && ent->groundentity != trent)
					{
						k = 2;
						it_ent = trent;
						break;
					}
				}
			}
		}
	}
	//when touch da button
	if( it_ent != NULL && k == 1)
	{
		if(it_ent->use && it_ent->moveinfo.state == PSTATE_BOTTOM && !it_ent->health)
		{
			k = false;
			if(zc->route_trace && zc->routeindex - 1 > 0)
			{
				k = true;
				i = zc->routeindex;
				if(Route[i].state == GRS_PUSHBUTTON) k = false;
				else if(Route[--i].state == GRS_PUSHBUTTON) k = false;

				if(!k && Route[i].ent == it_ent) zc->routeindex = i + 1;
				else k = true;
			}
			
//			if(!k) buttonuse = true;//it_ent->use(it_ent,ent,it_ent/*ent*/);
			if(!k && it_ent->target)
			{
				string = it_ent->target;
				e = &g_edicts[(int)maxclients->value+1];
				for ( i=maxclients->value+1 ; i<globals.num_edicts ; i++, e++)
				{
					if(!e->inuse || !e->targetname) continue;
					if (Q_stricmp (string, e->targetname) == 0 )
					{
//gi.bprintf(PRINT_HIGH,"yea4  %i %s\n",e->moveinfo.state,e->classname);
						if(e->classname[0] == 't')
						{
							if(!Q_stricmp (e->classname,"trigger_relay"))
							{
								if(e->target)
								{
									string = e->target;
									e = &g_edicts[(int)maxclients->value];
									i=maxclients->value;
									continue;
								}
							}
							else if(!Q_stricmp (e->classname,"target_laser")
								|| !Q_stricmp (e->classname,"target_mal_laser"))
							{
								if(e->spawnflags & 1)
								{
									it_ent->use(it_ent,ent,it_ent);
									break;
								}
							}
						}
						else if(e->classname[0] == 'f')
						{
							it_ent->use(it_ent,ent,it_ent/*ent*/);
							if(!Q_stricmp (e->classname,"func_door") 
								|| !Q_stricmp ( e->classname,"func_door_rotating")
								/*&& (e->moveinfo.state == 1 || e->moveinfo.state == 2)*/
							)//	&& abs ((e->moveinfo.start_origin[2] - e->moveinfo.end_origin[2])) > 54 )
							{
								k = false;
	//							return true;
								if(!zc->route_trace /*|| zc->routeindex <= 0*/)
								{
									v[0] = (it_ent->absmin[0] + it_ent->absmax[0]) / 2;
									v[1] = (it_ent->absmin[1] + it_ent->absmax[1]) / 2;
									v[2] = (it_ent->absmin[2] + it_ent->absmax[2]) / 2;
									VectorSubtract(it_ent->union_ent->s.origin,v,temppos);
									VectorScale (temppos, 3, v);
									VectorAdd(ent->s.origin,v,zc->movtarget_pt);
								}
								else
								{
									/*Get_RouteOrigin(zc->routeindex - 1,v);
									VectorSubtract(v,ent->s.origin,temppos);
									VectorScale (temppos, 3, v);
									VectorCopy(ent->s.origin,temppos);
									VectorAdd(ent->s.origin,v,zc->movtarget_pt);*/
									VectorCopy(ent->s.origin,zc->movtarget_pt);
								}
								//VectorScale (temppos, 3, v);
								//VectorAdd(ent->s.origin,v,zc->movtarget_pt);
	
								if(fabs (e->moveinfo.start_origin[2] - e->moveinfo.end_origin[2]) > JumpMax )
								{
									if(e->union_ent == NULL)
									{
//gi.bprintf(PRINT_HIGH,"voodoo\n"); //ppx
										it = FindItem("Roam Navi3");
										trent = G_Spawn();
										trent->classname = it->classname;
										trent->s.origin[0] = (e->absmin[0] + e->absmax[0])/2;
										trent->s.origin[1] = (e->absmin[1] + e->absmax[1])/2;
										trent->s.origin[2] = e->absmax[2] + 16;
										trent->union_ent = e;
										e->union_ent = trent;

										//trent->nextthink = level.time + 10;
										//trent->think = G_FreeEdict;

										SpawnItem3 (trent, it);
									}
									else
									{
										trent = e->union_ent;
										trent->solid = SOLID_TRIGGER;
										trent->svflags &= ~SVF_NOCLIENT;
//gi.bprintf(PRINT_HIGH,"SPAWNed\n"); //ppx
									}
//									SpawnItem2 (trent, it);
	
									zc->second_target = trent;
									trent->target_ent = ent;

									//トグル式はすぐ走る
									if(e->spawnflags & PDOOR_TOGGLE)
									{
										f1 = e->moveinfo.start_origin[2] - e->moveinfo.end_origin[2];
										//スタート地点が上
										if(f1 > 0 )
										{
											k = true;
											if(e->moveinfo.state == PSTATE_BOTTOM || e->moveinfo.state == PSTATE_UP )
											{
												if((trent->s.origin[2] - ent->s.origin[2]) > 32) zc->second_target = NULL;
											}
											else if(e->moveinfo.state == PSTATE_TOP || e->moveinfo.state == PSTATE_DOWN)
											{
												if((trent->s.origin[2] - ent->s.origin[2]) < -48) zc->second_target = NULL;
											}
										}
										else
										{
											k = true;
											if(e->moveinfo.state == PSTATE_TOP || e->moveinfo.state == PSTATE_DOWN)
											{
												if((trent->s.origin[2] - ent->s.origin[2]) > 32) zc->second_target = NULL;
											}
											else if(e->moveinfo.state == PSTATE_BOTTOM || e->moveinfo.state == PSTATE_UP)
											{
												if((trent->s.origin[2] - ent->s.origin[2]) < -48) zc->second_target = NULL;
											}
										}
									}
									//ノーマル
									else
									{
										f1 = e->moveinfo.start_origin[2] - e->moveinfo.end_origin[2];
										//スタート地点が上
										if(f1 > 0 )
										{
											if(e->moveinfo.state == PSTATE_BOTTOM || e->moveinfo.state == PSTATE_UP)
											{
												if(fabs(trent->s.origin[2] - ent->s.origin[2]) < JumpMax) k = true; 
											}
										}
										else
										{
											if(e->moveinfo.state == PSTATE_BOTTOM || e->moveinfo.state == PSTATE_UP )
											{
												if(fabs(trent->s.origin[2] - ent->s.origin[2]) < JumpMax) k = true;
											}
										}
									}
//									if(Bot_trace (ent,zc->second_target)) k = true;
								}
								if(!k)
								{
									//gi.bprintf(PRINT_HIGH,"waitset %i\n",e->moveinfo.state);
									zc->waitin_obj = e;
									zc->zcstate &= ~STS_WAITS;
									zc->zcstate |= STS_W_DOOROPEN;	
								}
								else
								{
									if((e->union_ent->s.origin[2] + 8
											- ent->s.origin[2]) > JumpMax)
									{
										zc->route_trace = false;
										zc->zcstate &= ~STS_WAITS;
									}
								}
								break;
							}
						}
					}			
				}
			}
			else if(!k) it_ent->use(it_ent,ent,it_ent/*ent*/);
		}
		
	}
	//when touch da door
	else if( it_ent != NULL && k == 2)
	{
		if(it_ent->moveinfo.state == PSTATE_BOTTOM)
		{
			if(it_ent->flags & FL_TEAMSLAVE)  it_ent->teammaster->use(it_ent->teammaster,ent,it_ent->teammaster/*ent*/);
			else it_ent->use(it_ent,ent,it_ent/*ent*/);
		}

//if(zc->zcstate & STS_WAITSMASK ) gi.bprintf(PRINT_HIGH,"Door Use\n");		

		if(it_ent->moveinfo.state == PSTATE_BOTTOM)
		{
			VectorCopy(ent->s.origin,zc->movtarget_pt);
			zc->waitin_obj = it_ent;
			zc->zcstate &= ~STS_WAITS;
			zc->zcstate |= STS_W_DOOROPEN;	

			if(it_ent->flags & FL_TEAMSLAVE)
			{
				trmin[0] = (it_ent->teammaster->absmin[0] + it_ent->teammaster->absmax[0]) / 2;
				trmin[1] = (it_ent->teammaster->absmin[1] + it_ent->teammaster->absmax[1]) / 2;
				trmax[0] = (it_ent->absmin[0] + it_ent->absmax[0]) / 2;
				trmax[1] = (it_ent->absmin[1] + it_ent->absmax[1]) / 2;				

				temppos[0] = (trmin[0] + trmax[0]) /2;
				temppos[1] = (trmin[1] + trmax[1]) /2;
				if(trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] = Get_yaw(temppos);	
			}
			else 
			{
				trmax[0] = (it_ent->absmin[0] + it_ent->absmax[0]) / 2;
				trmax[1] = (it_ent->absmin[1] + it_ent->absmax[1]) / 2;	
				VectorSubtract(trmax,ent->s.origin,temppos);
				if(trace_priority < TRP_ANGLEKEEP) ent->s.angles[YAW] = Get_yaw(temppos);			
			}
		}
		else if(it_ent->moveinfo.state == PSTATE_UP )
		{
			VectorCopy(ent->s.origin,zc->movtarget_pt);
			zc->waitin_obj = it_ent;
			zc->zcstate &= ~STS_WAITS;
			zc->zcstate |= STS_W_DOOROPEN;	
		}
	}
VCHCANSEL:
	//--------------------------------------------------------------------------------------
	//ladder check
	k = false;
	if(zc->route_trace && (zc->routeindex + 1) < CurrentIndex)
	{
		Get_RouteOrigin(zc->routeindex + 1,v);
		if((v[2] - ent->s.origin[2]) >= 32 /*|| ent->waterlevel*/) k = true;
	}
	if(k && trace_priority && !zc->second_target && !(ent->client->ps.pmove.pm_flags & PMF_DUCKED))
	{
		tempflag = false;

		VectorCopy(ent->mins,trmin);
		VectorCopy(ent->maxs,trmax);

		trmin[2] += 20;

		//front
		f1 = 32;
		if(zc->route_trace) f1 = 32;

		iyaw = zc->moveyaw;
		yaw = iyaw * M_PI * 2 / 360;
		touchmin[0] = cos(yaw) * f1;//28 ;
		touchmin[1] = sin(yaw) * f1;
		touchmin[2] = 0;

		VectorAdd(ent->s.origin,touchmin,touchmax);
		rs_trace = gi.trace (ent->s.origin, trmin,ent->maxs, touchmax,ent, MASK_BOTSOLID );

		if(rs_trace.contents & CONTENTS_LADDER) tempflag = true;

		//upper
		if(!tempflag && !zc->waterstate)
		{		
			trmax[2] += 32;
			rs_trace = gi.trace (ent->s.origin, trmin,trmax, touchmax,ent, MASK_BOTSOLID );
			if(rs_trace.contents & CONTENTS_LADDER) tempflag = 2;
		}
		if(!tempflag && ent->groundentity)
		{
			Get_RouteOrigin(zc->routeindex,v);
			v[2] = ent->s.origin[2];//0;
			rs_trace = gi.trace (ent->s.origin, trmin,ent->maxs, v,ent, MASK_BOTSOLID );
			if(rs_trace.contents & CONTENTS_LADDER) tempflag = 3;
		}

		//right
		if(!tempflag)
		{
			iyaw = zc->moveyaw + 90;
			if(iyaw > 180) iyaw -= 360;
			yaw = iyaw * M_PI * 2 / 360;
			touchmin[0] = cos(yaw) * 32 ;
			touchmin[1] = sin(yaw) * 32 ;
			touchmin[2] = 0;

			VectorAdd(ent->s.origin,touchmin,touchmax);
			rs_trace = gi.trace (ent->s.origin, trmin,ent->maxs, touchmax,ent,  MASK_BOTSOLID );

			if(rs_trace.contents & CONTENTS_LADDER) tempflag = true;
		}
		//left
		if(!tempflag)
		{
			iyaw = zc->moveyaw - 90;
			if(iyaw < -180) iyaw += 360;
			yaw = iyaw * M_PI * 2 / 360;
			touchmin[0] = cos(yaw) * 32 ;
			touchmin[1] = sin(yaw) * 32 ;
			touchmin[2] = 0;

			VectorAdd(ent->s.origin,touchmin,touchmax);
			rs_trace = gi.trace (ent->s.origin, trmin,ent->maxs, touchmax,ent, MASK_BOTSOLID );

			if(rs_trace.contents & CONTENTS_LADDER)	tempflag = true;
		}

		//ladder
		if(tempflag)
		{
#ifdef _DEBUG
gi.bprintf(PRINT_HIGH,"ladder founded! %f\n",iyaw);
#endif
			VectorCopy(rs_trace.endpos,trmax);
			VectorCopy(trmax,touchmax);
			touchmax[2] += 8190;
			rs_trace = gi.trace (/*temppos*/trmax, trmin,ent->maxs, touchmax,ent, MASK_SOLID );

			 e = rs_trace.ent;
//if((rs_trace.contents & CONTENTS_LADDER)) gi.bprintf(PRINT_HIGH,"damn!\n");

			k = 0;
			VectorCopy(rs_trace.endpos,temppos);
			VectorAdd(rs_trace.endpos,touchmin,touchmax);
			rs_trace = gi.trace (temppos, trmin,ent->maxs, touchmax,ent, MASK_BOTSOLID);
			
			if(e)
			{
				if(Q_stricmp (e->classname, "func_door") == 0)
				{
					k = true;
				}
			}

			if((!(rs_trace.contents & CONTENTS_LADDER) || k) /*&& rs_trace.fraction < 1.0*/)
			{
//	gi.WriteByte (svc_temp_entity);
//	gi.WriteByte (TE_RAILTRAIL);
//	gi.WritePosition (ent->s.origin);
//	gi.WritePosition (temppos);
//	gi.multicast (ent->s.origin, MULTICAST_PHS);

				ent->velocity[0] = 0;
				ent->velocity[1] = 0;
				if(zc->moveyaw == iyaw || zc->route_trace)
				{
#ifdef _DEBUG
gi.bprintf(PRINT_HIGH,"ladder On!\n");
#endif
					
/*					x = Get_yaw(rs_trace.plane.normal);
					x += 180;
					if(x > 180) x -= 360;
					zc->moveyaw = x;*/
					if(zc->moveyaw != iyaw)  zc->moveyaw = iyaw;

					ent->s.angles[YAW] = zc->moveyaw;
					if(tempflag != 3) VectorCopy(trmax,ent->s.origin);
					zc->zcstate |= STS_LADDERUP; 
					ent->s.angles[YAW] = zc->moveyaw;
					ent->s.angles[PITCH] = -29;

					if(tempflag == 2)
					{
						ent->velocity[2] += VEL_BOT_JUMP;
						gi.sound(ent, CHAN_VOICE, gi.soundindex("*jump1.wav"), 1, ATTN_NORM, 0);
						PlayerNoise(ent, ent->s.origin, PNOISE_SELF);	//pon
						Set_BotAnim(ent,ANIM_JUMP,FRAME_jump1-1,FRAME_jump6);
						zc->zcstate |= STS_SJMASK;
//						ent->s.frame = FRAME_jump1-1;
//						ent->client->anim_end = FRAME_jump6;
//						ent->client->anim_priority = ANIM_JUMP;
						ent->moveinfo.speed = 0;
					}
					else if(tempflag == 3)
					{
						ent->velocity[2] += VEL_BOT_JUMP;
						gi.sound(ent, CHAN_VOICE, gi.soundindex("*jump1.wav"), 1, ATTN_NORM, 0);
						PlayerNoise(ent, ent->s.origin, PNOISE_SELF);	//pon
						Set_BotAnim(ent,ANIM_JUMP,FRAME_jump1-1,FRAME_jump6);
						zc->zcstate |= STS_SJMASK;
//						ent->s.frame = FRAME_jump1-1;
//						ent->client->anim_end = FRAME_jump6;
//						ent->client->anim_priority = ANIM_JUMP;
						ent->moveinfo.speed = MOVE_SPD_JUMP;
					}
					else if(ent->waterlevel > 1)
					{
						ent->velocity[2] = VEL_BOT_WLADRUP;
//				if(VectorCompare(ent->s.origin,ent->s.old_origin)) ent->velocity[2] += 50;
					}
					else
					{
						ent->velocity[2] = VEL_BOT_LADRUP;
//			if(VectorCompare(ent->s.origin,ent->s.old_origin)) ent->velocity[2] += 50;
					}
//					gi.bprintf(PRINT_HIGH,"ladder!!\n");
//					goto VCHCANSEL;
				}
				else
				{
					zc->moveyaw = iyaw;
					ent->s.angles[YAW] = zc->moveyaw;
				}
//				goto VCHCANSEL;
			}
		}
	}
VCHCANSEL_L:	
	//--------------------------------------------------------------------------------------
	//player sizebox set
	// ducked
	//special duckset
	if(ent->client->zc.battleduckcnt > 0 && ent->groundentity && ent->velocity[2] < 10)
	{
		ent->client->ps.pmove.pm_flags |= PMF_DUCKED;
		ent->client->zc.battleduckcnt--;
	}

	if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->client->zc.n_duckedtime = 0;
		ent->maxs[2] = 4;
		ent->viewheight = -2;
	}
	// not ducked
	else
	{
		if(ent->client->zc.n_duckedtime < FRAMETIME * 10) ent->client->zc.n_duckedtime += FRAMETIME;
		ent->maxs[2] = 32;
		ent->viewheight = 22;
	}

	//--------------------------------------------------------------------------------------
	// angle set
	VectorCopy(ent->s.angles,ent->client->v_angle);
	if(ent->s.angles[PITCH] < -29) ent->s.angles[PITCH] = -29;
	else if(ent->s.angles[PITCH] > 29) ent->s.angles[PITCH] = 29;
	
	//--------------------------------------------------------------------------------------

//ZOID
	if (ent->client->ctf_grapple)
	{
/*		if(ent->client->ctf_grapplestate > CTF_GRAPPLE_STATE_FLY)
		{
			if(ent->waterlevel && !waterjumped) VectorClear(ent->velocity);
		}*/
		CTFGrapplePull(ent->client->ctf_grapple);
		if(ent->client->ctf_grapplestate > CTF_GRAPPLE_STATE_FLY)
		{
			e = (edict_t*)ent->client->ctf_grapple;
			//ent->velocity[2] =  ent->gravity * sv_gravity->value * FRAMETIME;
			if(ent->groundentity && ent->velocity[2] < 0) ent->velocity[2] =  ent->gravity * sv_gravity->value * FRAMETIME;
			else if(VectorCompare(ent->s.origin,ent->s.old_origin))
			{
				ent->velocity[2] +=  JumpMax;//ent->gravity * sv_gravity->value * FRAMETIME * 4;

				if(ent->client->ctf_grapplestate == CTF_GRAPPLE_STATE_PULL)
				{
					VectorSubtract(ent->s.origin,e->s.origin,v);
					yaw = Get_yaw(v);
					
					yaw = yaw * M_PI * 2 / 360;
					ent->velocity[0] += cos(yaw) * 200 ;				//start
					ent->velocity[1] += sin(yaw) * 200 ;
				}
			}
			else ent->velocity[2] +=  ent->gravity * sv_gravity->value * FRAMETIME * 2;
		}
	}
	else
	{
		if(ent->waterlevel > 2) {ent->velocity[0] = 0;ent->velocity[1] = 0;/*VectorClear(ent->velocity);*/}
		else if(ent->waterlevel && !ent->groundentity && ent->velocity[2] < 0) VectorClear(ent->velocity);//ent->velocity[2] = 0; 
	}
//ZOID

/*
ent->velocity[0] = 800 * (random() - 0.5);
ent->velocity[1] = 800 * (random() - 0.5);

ent->client->ps.pmove.pm_flags |= PMF_DUCKED;
*/
	ent->client->zc.trapped = false;		//trapcatch clear

	gi.linkentity (ent);
	G_TouchTriggers (ent);
}
