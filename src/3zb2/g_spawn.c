
#include "header/local.h"
#include "header/bot.h"
typedef struct
{
	char	*name;
	void	(*spawn)(edict_t *ent);
} spawn_t;


void SP_item_health (edict_t *self);
void SP_item_health_small (edict_t *self);
void SP_item_health_large (edict_t *self);
void SP_item_health_mega (edict_t *self);

void SP_info_player_start (edict_t *ent);
void SP_info_player_deathmatch (edict_t *ent);
void SP_info_player_coop (edict_t *ent);
void SP_info_player_intermission (edict_t *ent);

void SP_func_plat (edict_t *ent);
void SP_func_rotating (edict_t *ent);
void SP_func_button (edict_t *ent);
void SP_func_door (edict_t *ent);
void SP_func_door_secret (edict_t *ent);
void SP_func_door_rotating (edict_t *ent);
void SP_func_water (edict_t *ent);
void SP_func_train (edict_t *ent);
void SP_func_conveyor (edict_t *self);
void SP_func_wall (edict_t *self);
void SP_func_object (edict_t *self);
void SP_func_explosive (edict_t *self);
void SP_func_timer (edict_t *self);
void SP_func_areaportal (edict_t *ent);
void SP_func_clock (edict_t *ent);
void SP_func_killbox (edict_t *ent);

void SP_trigger_always (edict_t *ent);
void SP_trigger_once (edict_t *ent);
void SP_trigger_multiple (edict_t *ent);
void SP_trigger_relay (edict_t *ent);
void SP_trigger_push (edict_t *ent);
void SP_trigger_hurt (edict_t *ent);
void SP_trigger_key (edict_t *ent);
void SP_trigger_counter (edict_t *ent);
void SP_trigger_elevator (edict_t *ent);
void SP_trigger_gravity (edict_t *ent);
void SP_trigger_monsterjump (edict_t *ent);

void SP_target_temp_entity (edict_t *ent);
void SP_target_speaker (edict_t *ent);
void SP_target_explosion (edict_t *ent);
void SP_target_changelevel (edict_t *ent);
void SP_target_secret (edict_t *ent);
void SP_target_goal (edict_t *ent);
void SP_target_splash (edict_t *ent);
void SP_target_spawner (edict_t *ent);
void SP_target_blaster (edict_t *ent);
void SP_target_crosslevel_trigger (edict_t *ent);
void SP_target_crosslevel_target (edict_t *ent);
void SP_target_laser (edict_t *self);
void SP_target_help (edict_t *ent);
void SP_target_actor (edict_t *ent);
void SP_target_lightramp (edict_t *self);
void SP_target_earthquake (edict_t *ent);
void SP_target_character (edict_t *ent);
void SP_target_string (edict_t *ent);

void SP_worldspawn (edict_t *ent);
void SP_viewthing (edict_t *ent);

void SP_light (edict_t *self);
void SP_light_mine1 (edict_t *ent);
void SP_light_mine2 (edict_t *ent);
void SP_info_null (edict_t *self);
void SP_info_notnull (edict_t *self);
void SP_path_corner (edict_t *self);
void SP_point_combat (edict_t *self);

void SP_misc_explobox (edict_t *self);
void SP_misc_banner (edict_t *self);
void SP_misc_satellite_dish (edict_t *self);
void SP_misc_actor (edict_t *self);
void SP_misc_gib_arm (edict_t *self);
void SP_misc_gib_leg (edict_t *self);
void SP_misc_gib_head (edict_t *self);
void SP_misc_insane (edict_t *self);
void SP_misc_deadsoldier (edict_t *self);
void SP_misc_viper (edict_t *self);
void SP_misc_viper_bomb (edict_t *self);
void SP_misc_bigviper (edict_t *self);
void SP_misc_strogg_ship (edict_t *self);
void SP_misc_teleporter (edict_t *self);
void SP_misc_teleporter_dest (edict_t *self);
void SP_misc_blackhole (edict_t *self);
void SP_misc_eastertank (edict_t *self);
void SP_misc_easterchick (edict_t *self);
void SP_misc_easterchick2 (edict_t *self);

void SP_monster_berserk (edict_t *self);
void SP_monster_gladiator (edict_t *self);
void SP_monster_gunner (edict_t *self);
void SP_monster_infantry (edict_t *self);
void SP_monster_soldier_light (edict_t *self);
void SP_monster_soldier (edict_t *self);
void SP_monster_soldier_ss (edict_t *self);
void SP_monster_tank (edict_t *self);
void SP_monster_medic (edict_t *self);
void SP_monster_flipper (edict_t *self);
void SP_monster_chick (edict_t *self);
void SP_monster_parasite (edict_t *self);
void SP_monster_flyer (edict_t *self);
void SP_monster_brain (edict_t *self);
void SP_monster_floater (edict_t *self);
void SP_monster_hover (edict_t *self);
void SP_monster_mutant (edict_t *self);
void SP_monster_supertank (edict_t *self);
void SP_monster_boss2 (edict_t *self);
void SP_monster_jorg (edict_t *self);
void SP_monster_boss3_stand (edict_t *self);

void SP_monster_commander_body (edict_t *self);

void SP_turret_breach (edict_t *self);
void SP_turret_base (edict_t *self);
void SP_turret_driver (edict_t *self);

// RAFAEL 14-APR-98
void SP_monster_soldier_hypergun (edict_t *self);
void SP_monster_soldier_lasergun (edict_t *self);
void SP_monster_soldier_ripper (edict_t *self);
void SP_monster_fixbot (edict_t *self);
void SP_monster_gekk (edict_t *self);
void SP_monster_chick_heat (edict_t *self);
void SP_monster_gladb (edict_t *self);
void SP_monster_boss5 (edict_t *self);
void SP_rotating_light (edict_t *self);
void SP_object_repair (edict_t *self);
void SP_misc_crashviper (edict_t *ent);
void SP_misc_viper_missile (edict_t *self);
void SP_misc_amb4 (edict_t *ent);
void SP_target_mal_laser (edict_t *ent);
void SP_misc_transport (edict_t *ent);
// END 14-APR-98

void SP_misc_nuke (edict_t *ent);

spawn_t	spawns[] = {
	{"item_health", SP_item_health},
	{"item_health_small", SP_item_health_small},
	{"item_health_large", SP_item_health_large},
	{"item_health_mega", SP_item_health_mega},

	{"info_player_start", SP_info_player_start},
	{"info_player_deathmatch", SP_info_player_deathmatch},
	{"info_player_coop", SP_info_player_coop},
	{"info_player_intermission", SP_info_player_intermission},
//ZOID
	{"info_player_team1", SP_info_player_team1},
	{"info_player_team2", SP_info_player_team2},
//ZOID

	{"func_plat", SP_func_plat},
	{"func_button", SP_func_button},
	{"func_door", SP_func_door},
	{"func_door_secret", SP_func_door_secret},
	{"func_door_rotating", SP_func_door_rotating},
	{"func_rotating", SP_func_rotating},
	{"func_train", SP_func_train},
	{"func_water", SP_func_water},
	{"func_conveyor", SP_func_conveyor},
	{"func_areaportal", SP_func_areaportal},
	{"func_clock", SP_func_clock},
	{"func_wall", SP_func_wall},
	{"func_object", SP_func_object},
	{"func_timer", SP_func_timer},
	{"func_explosive", SP_func_explosive},
	{"func_killbox", SP_func_killbox},

	// RAFAEL
	{"func_object_repair", SP_object_repair},
	{"rotating_light", SP_rotating_light},

	{"trigger_always", SP_trigger_always},
	{"trigger_once", SP_trigger_once},
	{"trigger_multiple", SP_trigger_multiple},
	{"trigger_relay", SP_trigger_relay},
	{"trigger_push", SP_trigger_push},
	{"trigger_hurt", SP_trigger_hurt},
	{"trigger_key", SP_trigger_key},
	{"trigger_counter", SP_trigger_counter},
	{"trigger_elevator", SP_trigger_elevator},
	{"trigger_gravity", SP_trigger_gravity},
	{"trigger_monsterjump", SP_trigger_monsterjump},

	{"target_temp_entity", SP_target_temp_entity},
	{"target_speaker", SP_target_speaker},
	{"target_explosion", SP_target_explosion},
	{"target_changelevel", SP_target_changelevel},
	{"target_secret", SP_target_secret},
	{"target_goal", SP_target_goal},
	{"target_splash", SP_target_splash},
	{"target_spawner", SP_target_spawner},
	{"target_blaster", SP_target_blaster},
	{"target_crosslevel_trigger", SP_target_crosslevel_trigger},
	{"target_crosslevel_target", SP_target_crosslevel_target},
	{"target_laser", SP_target_laser},
	{"target_help", SP_target_help},
#if 0 // remove monster code
	{"target_actor", SP_target_actor},
#endif
	{"target_lightramp", SP_target_lightramp},
	{"target_earthquake", SP_target_earthquake},
	{"target_character", SP_target_character},
	{"target_string", SP_target_string},

	// RAFAEL 15-APR-98
	{"target_mal_laser", SP_target_mal_laser},

	{"worldspawn", SP_worldspawn},
	{"viewthing", SP_viewthing},

	{"light", SP_light},
	{"light_mine1", SP_light_mine1},
	{"light_mine2", SP_light_mine2},
	{"info_null", SP_info_null},
	{"func_group", SP_info_null},
	{"info_notnull", SP_info_notnull},
	{"path_corner", SP_path_corner},
	{"point_combat", SP_point_combat},

	{"misc_explobox", SP_misc_explobox},
	{"misc_banner", SP_misc_banner},
//ZOID
	{"misc_ctf_banner", SP_misc_ctf_banner},
	{"misc_ctf_small_banner", SP_misc_ctf_small_banner},
//ZOID
	{"misc_satellite_dish", SP_misc_satellite_dish},
#if 0 // remove monster code
	{"misc_actor", SP_misc_actor},
#endif
	{"misc_gib_arm", SP_misc_gib_arm},
	{"misc_gib_leg", SP_misc_gib_leg},
	{"misc_gib_head", SP_misc_gib_head},
#if 0 // remove monster code
	{"misc_insane", SP_misc_insane},
#endif
	{"misc_deadsoldier", SP_misc_deadsoldier},
	{"misc_viper", SP_misc_viper},
	{"misc_viper_bomb", SP_misc_viper_bomb},
	{"misc_bigviper", SP_misc_bigviper},
	{"misc_strogg_ship", SP_misc_strogg_ship},
	{"misc_teleporter", SP_misc_teleporter},
	{"misc_teleporter_dest", SP_misc_teleporter_dest},
//ZOID
	{"trigger_teleport", SP_trigger_teleport},
	{"info_teleport_destination", SP_info_teleport_destination},
//ZOID
	{"misc_blackhole", SP_misc_blackhole},
	{"misc_eastertank", SP_misc_eastertank},
	{"misc_easterchick", SP_misc_easterchick},
	{"misc_easterchick2", SP_misc_easterchick2},

	// RAFAEL
//	{"misc_crashviper", SP_misc_crashviper},
//	{"misc_viper_missile", SP_misc_viper_missile},
	{"misc_amb4", SP_misc_amb4},
	// RAFAEL 17-APR-98
	{"misc_transport", SP_misc_transport},
	// END 17-APR-98
	// RAFAEL 12-MAY-98
	{"misc_nuke", SP_misc_nuke},

#if 0 // remove monster code
	{"monster_berserk", SP_monster_berserk},
	{"monster_gladiator", SP_monster_gladiator},
	{"monster_gunner", SP_monster_gunner},
	{"monster_infantry", SP_monster_infantry},
	{"monster_soldier_light", SP_monster_soldier_light},
	{"monster_soldier", SP_monster_soldier},
	{"monster_soldier_ss", SP_monster_soldier_ss},
	{"monster_tank", SP_monster_tank},
	{"monster_tank_commander", SP_monster_tank},
	{"monster_medic", SP_monster_medic},
	{"monster_flipper", SP_monster_flipper},
	{"monster_chick", SP_monster_chick},
	{"monster_parasite", SP_monster_parasite},
	{"monster_flyer", SP_monster_flyer},
	{"monster_brain", SP_monster_brain},
	{"monster_floater", SP_monster_floater},
	{"monster_hover", SP_monster_hover},
	{"monster_mutant", SP_monster_mutant},
	{"monster_supertank", SP_monster_supertank},
	{"monster_boss2", SP_monster_boss2},
	{"monster_boss3_stand", SP_monster_boss3_stand},
	{"monster_jorg", SP_monster_jorg},

	{"monster_commander_body", SP_monster_commander_body},

	{"turret_breach", SP_turret_breach},
	{"turret_base", SP_turret_base},
	{"turret_driver", SP_turret_driver},
#endif

	{NULL, NULL}
};

/*
===============
ED_CallSpawn

Finds the spawn function for the entity and calls it
===============
*/
void ED_CallSpawn (edict_t *ent)
{
	spawn_t	*s;
	gitem_t	*item;
	int		i;

	if (!ent->classname)
	{
		gi.dprintf ("ED_CallSpawn: NULL classname\n");
		return;
	}

	// check item spawn functions
	for (i=0,item=itemlist ; i<game.num_items ; i++,item++)
	{
		if (!item->classname)
			continue;
		if (!strcmp(item->classname, ent->classname))
		{	// found it
			SpawnItem (ent, item);
//ponko
			if     (!strcmp(ent->classname,"weapon_shotgun"))		mpindex[WEAP_SHOTGUN] = i;
			else if(!strcmp(ent->classname,"weapon_supershotgun"))	mpindex[WEAP_SUPERSHOTGUN] = i;
			else if(!strcmp(ent->classname,"weapon_machinegun"))	mpindex[WEAP_MACHINEGUN] = i;
			else if(!strcmp(ent->classname,"weapon_chaingun"))		mpindex[WEAP_CHAINGUN] = i;
			else if(!strcmp(ent->classname,"ammo_grenades"))		mpindex[WEAP_GRENADES] = i;
			else if(!strcmp(ent->classname,"weapon_grenadelauncher"))	mpindex[WEAP_GRENADELAUNCHER] = i;
			else if(!strcmp(ent->classname,"weapon_rocketlauncher"))	mpindex[WEAP_ROCKETLAUNCHER] = i;
			else if(!strcmp(ent->classname,"weapon_hyperblaster"))	mpindex[WEAP_HYPERBLASTER] = i;
			else if(!strcmp(ent->classname,"weapon_boomer"))		mpindex[WEAP_BOOMER] = i;
			else if(!strcmp(ent->classname,"weapon_railgun"))		mpindex[WEAP_RAILGUN] = i;
			else if(!strcmp(ent->classname,"weapon_phalanx"))		mpindex[WEAP_PHALANX] = i;			
			else if(!strcmp(ent->classname,"weapon_bfg"))			mpindex[WEAP_BFG] = i;
			else if(!strcmp(ent->classname,"item_quad"))			mpindex[MPI_QUAD] = i;
			else if(!strcmp(ent->classname,"item_invulnerability"))	mpindex[MPI_PENTA] = i;
			else if(!strcmp(ent->classname,"item_quadfire"))		mpindex[MPI_QUADF] = i;
//ponko
			return;
		}
	}

	// check normal spawn functions
	for (s=spawns ; s->name ; s++)
	{
		if (!strcmp(s->name, ent->classname))
		{	// found it
			s->spawn (ent);
			return;
		}
	}
//	gi.dprintf ("%s doesn't have a spawn function\n", ent->classname);
}

/*
=============
ED_NewString
=============
*/
char *ED_NewString (char *string)
{
	char	*newb, *new_p;
	int		i,l;
	
	l = strlen(string) + 1;

	newb = gi.TagMalloc (l, TAG_LEVEL);

	new_p = newb;

	for (i=0 ; i< l ; i++)
	{
		if (string[i] == '\\' && i < l-1)
		{
			i++;
			if (string[i] == 'n')
				*new_p++ = '\n';
			else
				*new_p++ = '\\';
		}
		else
			*new_p++ = string[i];
	}
	
	return newb;
}




/*
===============
ED_ParseField

Takes a key/value pair and sets the binary values
in an edict
===============
*/
void ED_ParseField (char *key, char *value, edict_t *ent)
{
	field_t	*f;
	byte	*b;
	float	v;
	vec3_t	vec;

	for (f=fields ; f->name ; f++)
	{
		if (!Q_stricmp(f->name, key))
		{	// found it
			if (f->flags & FFL_SPAWNTEMP)
				b = (byte *)&st;
			else
				b = (byte *)ent;

			switch (f->type)
			{
			case F_LSTRING:
				*(char **)(b+f->ofs) = ED_NewString (value);
				break;
			case F_VECTOR:
				sscanf (value, "%f %f %f", &vec[0], &vec[1], &vec[2]);
				((float *)(b+f->ofs))[0] = vec[0];
				((float *)(b+f->ofs))[1] = vec[1];
				((float *)(b+f->ofs))[2] = vec[2];
				break;
			case F_INT:
				*(int *)(b+f->ofs) = atoi(value);
				break;
			case F_FLOAT:
				*(float *)(b+f->ofs) = atof(value);
				break;
			case F_ANGLEHACK:
				v = atof(value);
				((float *)(b+f->ofs))[0] = 0;
				((float *)(b+f->ofs))[1] = v;
				((float *)(b+f->ofs))[2] = 0;
				break;
			case F_IGNORE:
				break;
			default:
				break;
			}
			return;
		}
	}
//	gi.dprintf ("%s is not a field %s\n", key,ent->classname);
}

/*
====================
ED_ParseEdict

Parses an edict out of the given string, returning the new position
ed should be a properly initialized empty edict.
====================
*/
char *ED_ParseEdict (char *data, edict_t *ent)
{
	qboolean	init;
	char		keyname[256];
	char		*com_token;

	init = false;
	memset (&st, 0, sizeof(st));

// go through all the dictionary pairs
	while (1)
	{	
	// parse key
		com_token = COM_Parse (&data);
		if (com_token[0] == '}')
			break;
		if (!data)
			gi.error ("ED_ParseEntity: EOF without closing brace");

		strncpy (keyname, com_token, sizeof(keyname)-1);
		
	// parse value	
		com_token = COM_Parse (&data);
		if (!data)
			gi.error ("ED_ParseEntity: EOF without closing brace");

		if (com_token[0] == '}')
			gi.error ("ED_ParseEntity: closing brace without data");

		init = true;	

	// keynames with a leading underscore are used for utility comments,
	// and are immediately discarded by quake
		if (keyname[0] == '_')
			continue;

		ED_ParseField (keyname, com_token, ent);
	}

	if (!init)
		memset (ent, 0, sizeof(*ent));

	return data;
}


/*
================
G_FindTeams

Chain together all entities with a matching team field.

All but the first will have the FL_TEAMSLAVE flag set.
All but the last will have the teamchain field set to the next one
================
*/
void G_FindTeams (void)
{
	edict_t	*e, *e2, *chain;
	int		i, j;
	int		c, c2;

	c = 0;
	c2 = 0;
	for (i=1, e=g_edicts+i ; i < globals.num_edicts ; i++,e++)
	{
		if (!e->inuse)
			continue;
		if (!e->team)
			continue;
		if (e->flags & FL_TEAMSLAVE)
			continue;
		chain = e;
		e->teammaster = e;
		c++;
		c2++;
		for (j=i+1, e2=e+1 ; j < globals.num_edicts ; j++,e2++)
		{
			if (!e2->inuse)
				continue;
			if (!e2->team)
				continue;
			if (e2->flags & FL_TEAMSLAVE)
				continue;
			if (!strcmp(e->team, e2->team))
			{
				c2++;
				chain->teamchain = e2;
				e2->teammaster = e;
				chain = e2;
				e2->flags |= FL_TEAMSLAVE;
			}
		}
	}

	gi.dprintf ("%i teams with %i entities\n", c, c2);
}


/*
================
G_FindTrainTeam

Chain together all entities with a matching team field.

All but the last will have the teamchain field set to the next one
================
*/
void G_FindTrainTeam(void)
{
	edict_t	*teamlist[MAX_EDICTS + 1];	
	edict_t	*e,*t,*p;

	qboolean	findteam;
	char	*currtarget,*currtargetname;
	char	*targethist[MAX_EDICTS];
	int		lc,i,j,k;
	int		loopindex;

	e = &g_edicts[(int)maxclients->value+1];
	for ( i=maxclients->value+1 ; i<globals.num_edicts ; i++, e++)
	{
		if(e->inuse && e->classname)
		{
			if(!Q_stricmp(e->classname,"path_corner") && e->targetname && e->target)
			{
//		orgtarget = e->target;		//terminal
//		orgtargetname = e->targetname;
				currtarget = e->target;			//target
				currtargetname = e->targetname;	//targetname
				lc = 0;

				memset(&teamlist,0,sizeof(teamlist));
				memset(&targethist,0,sizeof(targethist));
				findteam = false;

				loopindex = 0;
				targethist[0] = e->targetname;

				while(lc < MAX_EDICTS)
				{
					t = &g_edicts[(int)maxclients->value+1];
					for ( j=maxclients->value+1 ; j<globals.num_edicts ; j++, t++)
					{
						if(t->inuse && t->classname)
						{
							if(!Q_stricmp(t->classname,"func_train") 
								&& !Q_stricmp(t->target,currtargetname)
								&& t->trainteam == NULL)
							{
								for(k = 0;k < lc; k++)
								{
									if(teamlist[k] == t) break;
								}
								if(k == lc )
								{
									teamlist[lc] = t;
									lc++;
								}
							}
						}
					}
					p = G_PickTarget(currtarget);
					if(!p) break;
					currtarget = p->target;
					currtargetname = p->targetname;
					if(!p->target) break;
					for(k = 0;k < loopindex;k++)
					{
						if(!Q_stricmp(targethist[k],currtargetname)) break;
					}
					if(k < loopindex)
					{
						findteam = true;
						break;
					}
					targethist[loopindex] = currtargetname;
					loopindex++;
					/*if(!Q_stricmp(currtarget,orgtargetname))
					{
						findteam = true;
						break;
					}*/
				}
				if(findteam && lc > 0)
				{
					gi.dprintf("%i train chaining founded.\n",lc);
					for(k = 0;k < lc; k++)
					{
						if(teamlist[k + 1] == NULL)
						{
							teamlist[k]->trainteam = teamlist[0];
							break;
						}
						teamlist[k]->trainteam = teamlist[k + 1];
					}
				}
			}
		}
	}
}
/*
================
G_FindItemLink




================
*/


void G_FindItemLink(void)
{ 
	int i,j,k;

	if(CurrentIndex <= 0) return;

	//search
	for(i = 0;i < CurrentIndex;i++)
	{
		if(Route[i].state == GRS_ITEMS)
		{
			for(j = 0;j < CurrentIndex;j++)
			{
				//found!!
				if(j != i && Route[i].ent == Route[j].ent)
				{
					for(k = 0;k < (MAXLINKPOD - (ctf->value != 0));k++)
					{
						//search blanked index
						if(!Route[i].linkpod[k])
						{
							Route[i].linkpod[k] = j;
							break;
						}
					}
				}
			}
		}
	}
}


/*
================
RTJump_Chk
================
*/

qboolean RTJump_Chk(vec3_t apos,vec3_t tpos)
{
	float	x,l,grav,vel,ypos,yori;
	vec3_t	v,vv;
	int		mf = false;

	grav = 1.0 * sv_gravity->value * FRAMETIME; 

	vel = VEL_BOT_JUMP;
	yori = apos[2];
	ypos = tpos[2];

	VectorSubtract(tpos,apos,v);

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

/*
================
G_FindRouteLink
================
*/
void G_FindRouteLink(edict_t *ent)
{
	trace_t	rs_trace;

	qboolean	tpbool;

	int i,j,k,l,total = 0;
	vec3_t	v,vv;
	float	x;


	//旗を発生させる
	if(!ctf->value && zigmode->value == 1)
	{
		SelectSpawnPoint (ent, v, vv);
	//	VectorCopy (v, ent->s.origin);
		if(ZIGDrop_Flag(ent,zflag_item))
		{
			VectorCopy (v, zflag_ent->s.origin);
		}
		//ZIGDrop_Flag(ent,item);
		zigflag_spawn = 2;
	}
gi.dprintf("Linking routes...\n");

	//get JumpMax
	if(JumpMax == 0)
	{
		x = VEL_BOT_JUMP - ent->gravity * sv_gravity->value * FRAMETIME;
		JumpMax = 0;
		while(1)
		{
			JumpMax += x * FRAMETIME; 
			x -= ent->gravity * sv_gravity->value * FRAMETIME;
			if( x < 0 ) break;
		}
	}

	//search
	for(i = 0;i < CurrentIndex;i++)
	{
		if(Route[i].state == GRS_NORMAL)
		{
			for(j = 0;j < CurrentIndex;j++)
			{
				if(abs(i - j) <= 50 || j == i || Route[j].state != GRS_NORMAL) continue;

				VectorSubtract(Route[j].Pt,Route[i].Pt,v);
				if(v[2] > JumpMax || v[2] < -500) continue;
				v[2] = 0;
				if(VectorLength(v) > 200) continue;

				if(fabs(v[2]) > 20 || VectorLength(v) > 64)
				{
					if(!RTJump_Chk(Route[i].Pt,Route[j].Pt))
									continue;
				}

				tpbool = false;
				for(l = -5;l < 6;l++)
				{
					if( (i + l) < 0 || (i + l) >= CurrentIndex) continue;
					for(k = 0;k < (MAXLINKPOD - (ctf->value != 0));k++)
					{
						//search blanked index
						if(!Route[i + l].linkpod[k]) break;
						if(abs(Route[i + l].linkpod[k] - j) < 50)
						{
							tpbool = true;
							break;
						}
					}
					if(tpbool) break;
				}
				if(tpbool) continue;

//				VectorSubtract(Route[j].Pt,Route[i].Pt,v);

				rs_trace = gi.trace(Route[j].Pt,NULL,NULL,Route[i].Pt,ent,MASK_SOLID); 
				//found!!
				if(!rs_trace.startsolid && !rs_trace.allsolid && rs_trace.fraction == 1.0)
				{
					for(k = 0;k < (MAXLINKPOD - (ctf->value != 0));k++)
					{
						//search blanked index
						if(!Route[i].linkpod[k])
						{
							Route[i].linkpod[k] = j;
							total++;
							break;
						}
					}
				}
			}
		}
	}
//gi.dprintf("yare!!!!!!!\n");
	if(ctf->value && bot_team_flag1 && bot_team_flag2)
	{
		j = 0;
		k = 0;
		//search
		for(i = (CurrentIndex - 1);i >= 0 ;i--)
		{
			if(Route[i].state < GRS_ITEMS)
			{
				if(Route[i].state == GRS_REDFLAG || Route[i].state == GRS_BLUEFLAG)
				{
					if(Route[i].ent == bot_team_flag1){ j = FOR_FLAG1; k = i;}
					else if(Route[i].ent == bot_team_flag2) {j = FOR_FLAG2;k = i;}
				}

				if(j == FOR_FLAG1)		Route[i].linkpod[MAXLINKPOD - 1] = (CTF_FLAG1_FLAG | k);
				else if(j == FOR_FLAG2)	Route[i].linkpod[MAXLINKPOD - 1] = (CTF_FLAG2_FLAG | k);
				else Route[i].linkpod[MAXLINKPOD - 1] = 0;
			}
		}
	}

gi.dprintf("Total %i linking done.\n",total);
	G_FreeEdict (ent);
}

/*
================
G_SpawnRouteLink
================
*/
void G_SpawnRouteLink(void)
{
	edict_t *e;

	if(CurrentIndex <= 0) return;

	e = G_Spawn();

	e->nextthink = level.time + FRAMETIME * 2;
	e->think = G_FindRouteLink;
}

/*
==============
SpawnEntities

Creates a server's entity / program execution context by
parsing textual entity definitions out of an ent file.
==============
*/
//void SetBotFlag1(edict_t *ent);	//チーム1の旗
//void SetBotFlag2(edict_t *ent);  //チーム2の旗

//void CTFSetupNavSpawn();	//ナビの設置
void SpawnEntities (char *mapname, char *entities, char *spawnpoint)
{
	edict_t		*ent;
	int			inhibit;
	char		*com_token;
	int			i;
	float		skill_level;

	int			laser = 0;
//ponko
	memset(mpindex,0,sizeof(mpindex));	//target item index
	memset(LaserIndex,0,sizeof(LaserIndex));
//ponko

	skill_level = floor (skill->value);
	if (skill_level < 0)
		skill_level = 0;
	if (skill_level > 4)
		skill_level = 4;
	if (skill->value != skill_level)
		gi.cvar_forceset("skill", va("%f", skill_level));

	SaveClientData ();

	gi.FreeTags (TAG_LEVEL);

	memset (&level, 0, sizeof(level));
	memset (g_edicts, 0, game.maxentities * sizeof (g_edicts[0]));

	strncpy (level.mapname, mapname, sizeof(level.mapname)-1);
	strncpy (game.spawnpoint, spawnpoint, sizeof(game.spawnpoint)-1);

	// set client fields on player ents
	for (i=0 ; i<game.maxclients ; i++)
		g_edicts[i+1].client = game.clients + i;

	ent = NULL;
	inhibit = 0;

// parse ents
	while (1)
	{
		// parse the opening brace	
		com_token = COM_Parse (&entities);
		if (!entities)
			break;
		if (com_token[0] != '{')
			gi.error ("ED_LoadFromFile: found %s when expecting {",com_token);

		if (!ent)
			ent = g_edicts;
		else
			ent = G_Spawn ();
		entities = ED_ParseEdict (entities, ent);
		
		// remove things (except the world) from different skill levels or deathmatch
		if (ent != g_edicts)
		{
			if (deathmatch->value)
			{
				if ( ent->spawnflags & SPAWNFLAG_NOT_DEATHMATCH )
				{
					G_FreeEdict (ent);	
					inhibit++;
					continue;
				}
			}
			else
			{
				if ( /* ((coop->value) && (ent->spawnflags & SPAWNFLAG_NOT_COOP)) || */
					((skill->value == 0) && (ent->spawnflags & SPAWNFLAG_NOT_EASY)) ||
					((skill->value == 1) && (ent->spawnflags & SPAWNFLAG_NOT_MEDIUM)) ||
					(((skill->value == 2) || (skill->value == 3)) && (ent->spawnflags & SPAWNFLAG_NOT_HARD))
					)
					{
						G_FreeEdict (ent);	
						inhibit++;
						continue;
					}
			}

			ent->spawnflags &= ~(SPAWNFLAG_NOT_EASY|SPAWNFLAG_NOT_MEDIUM|SPAWNFLAG_NOT_HARD|SPAWNFLAG_NOT_COOP|SPAWNFLAG_NOT_DEATHMATCH);
		}

		ED_CallSpawn (ent);
//laser index
		if (Q_stricmp(ent->classname, "target_laser") == 0)
		{
			if(laser < MAX_LASERINDEX) LaserIndex[laser++] = ent;
		}
//PON-CTF
	if(ent->solid == SOLID_TRIGGER && ctf->value && chedit->value) ent->moveinfo.speed = 0;
	if (Q_stricmp(ent->classname, "item_flag_team1") == 0) bot_team_flag1 = ent;
	else if (Q_stricmp(ent->classname, "item_flag_team2") == 0) bot_team_flag2 = ent;
//PON-CTF
	}	

	gi.dprintf ("%i entities inhibited\n", inhibit);

	G_FindTeams ();

	PlayerTrail_Init ();

	//func_trainのリンク
	G_FindTrainTeam();


//ZOID
	CTFSetupTechSpawn();

//ZOID

//ponko	
	CTFSetupNavSpawn();	//ナビの設置
	if(!chedit->value) G_FindItemLink();	//アイテムのリンク(通常時のみ)

	G_SpawnRouteLink();

	if(zigmode->value == 1) zigflag_spawn = 1;
	else zigflag_spawn = 0;
	//旗のアイテムアドレス取得
	zflag_item =  FindItem("Zig Flag");
	zflag_ent = NULL;		//初期化
//	if(CurrentIndex > 0)
//ponko

	ctfjob_update = level.time;
}


//===================================================================

#if 0
	// cursor positioning
	xl <value>
	xr <value>
	yb <value>
	yt <value>
	xv <value>
	yv <value>

	// drawing
	statpic <name>
	pic <stat>
	num <fieldwidth> <stat>
	string <stat>

	// control
	if <stat>
	ifeq <stat> <value>
	ifbit <stat> <value>
	endif

#endif

char *single_statusbar = 
"yb	-24 "

// health
"xv	0 "
"hnum "
"xv	50 "
"pic 0 "

// ammo
"if 2 "
"	xv	100 "
"	anum "
"	xv	150 "
"	pic 2 "
"endif "

// armor
"if 4 "
"	xv	200 "
"	rnum "
"	xv	250 "
"	pic 4 "
"endif "

// selected item
"if 6 "
"	xv	296 "
"	pic 6 "
"endif "

"yb	-50 "

// picked up item
"if 7 "
"	xv	0 "
"	pic 7 "
"	xv	26 "
"	yb	-42 "
"	stat_string 8 "
"	yb	-50 "
"endif "

// timer
"if 9 "
"	xv	262 "
"	num	2	10 "
"	xv	296 "
"	pic	9 "
"endif "

//  help / weapon icon 
"if 11 "
"	xv	148 "
"	pic	11 "
"endif "
;

char *dm_statusbar =
"yb	-24 "

// health
"xv	0 "
"hnum "
"xv	50 "
"pic 0 "

// ammo
"if 2 "
"	xv	100 "
"	anum "
"	xv	150 "
"	pic 2 "
"endif "

// armor
"if 4 "
"	xv	200 "
"	rnum "
"	xv	250 "
"	pic 4 "
"endif "

// selected item
"if 6 "
"	xv	296 "
"	pic 6 "
"endif "

"yb	-50 "

// picked up item
"if 7 "
"	xv	0 "
"	pic 7 "
"	xv	26 "
"	yb	-42 "
"	stat_string 8 "
"	yb	-50 "
"endif "

// timer
"if 9 "
"	xv	246 "
"	num	2	10 "
"	xv	296 "
"	pic	9 "
"endif "

//  help / weapon icon 
"if 11 "
"	xv	148 "
"	pic	11 "
"endif "

//  frags
"xr	-50 "
"yt 2 "
"num 3 14 "

//sight
"if 31 "
"   xv 96 "
"   yv 56 "
"   pic 31 "
"endif"
;


/*QUAKED worldspawn (0 0 0) ?

Only used for the world.
"sky"	environment map name
"skyaxis"	vector axis for rotating sky
"skyrotate"	speed of rotation in degrees/second
"sounds"	music cd track number
"gravity"	800 is default gravity
"message"	text to print at user logon
*/
void SP_worldspawn (edict_t *ent)
{
	ent->movetype = MOVETYPE_PUSH;
	ent->solid = SOLID_BSP;
	ent->inuse = true;			// since the world doesn't use G_Spawn()
	ent->s.modelindex = 1;		// world model is always index 1

	//---------------

	// reserve some spots for dead player bodies
	InitBodyQue ();

	// set configstrings for items
	SetItemNames ();

	if (st.nextmap)
		strcpy (level.nextmap, st.nextmap);

	// make some data visible to the server

	if (ent->message && ent->message[0])
	{
		gi.configstring (CS_NAME, ent->message);
		strncpy (level.level_name, ent->message, sizeof(level.level_name)-1);
		level.level_name[sizeof(level.level_name)-1] = '\0';
	}
	else
	{
		strncpy (level.level_name, level.mapname, sizeof(level.level_name)-1);
		level.level_name[sizeof(level.level_name)-1] = '\0';
	}

	if (st.sky && st.sky[0])
		gi.configstring (CS_SKY, st.sky);
	else
		gi.configstring (CS_SKY, "unit1_");

	gi.configstring (CS_SKYROTATE, va("%f", st.skyrotate) );

	gi.configstring (CS_SKYAXIS, va("%f %f %f",
		st.skyaxis[0], st.skyaxis[1], st.skyaxis[2]) );

	gi.configstring (CS_CDTRACK, va("%i", ent->sounds) );

	gi.configstring (CS_MAXCLIENTS, va("%i", (int)(maxclients->value) ) );

	// status bar program
	if (deathmatch->value)
//ZOID
		if (ctf->value) {
			gi.configstring (CS_STATUSBAR, ctf_statusbar);
			//precaches
			gi.imageindex("sbfctf1");
			gi.imageindex("sbfctf2");
//			gi.imageindex("ctfsb1");
//			gi.imageindex("ctfsb2");
			gi.imageindex("i_ctf1");
			gi.imageindex("i_ctf2");
			gi.imageindex("i_ctf1d");
			gi.imageindex("i_ctf2d");
			gi.imageindex("i_ctf1t");
			gi.imageindex("i_ctf2t");
			gi.imageindex("i_ctfj");

/*			if(ctf->value == 2)
			{
				gi.modelindex("models/weapons/v_hook/tris.md2");
				gi.soundindex("weapons/grapple/grhit.wav");
				gi.soundindex("weapons/grapple/grpull.wav");
				gi.soundindex("weapons/grapple/grfire.wav");
			}*/
		} else
//ZOID
		gi.configstring (CS_STATUSBAR, dm_statusbar);
	else
		gi.configstring (CS_STATUSBAR, single_statusbar);

	//---------------


	// help icon for statusbar
	gi.imageindex ("i_help");
	level.pic_health = gi.imageindex ("i_health");
	gi.imageindex ("help");
	gi.imageindex ("field_3");

	if (!st.gravity)
		gi.cvar_set("sv_gravity", "800");
	else
		gi.cvar_set("sv_gravity", st.gravity);

	snd_fry = gi.soundindex ("player/fry.wav");	// standing in lava / slime

	PrecacheItem (FindItem ("Blaster"));

	gi.soundindex ("player/lava1.wav");
	gi.soundindex ("player/lava2.wav");

	gi.soundindex ("misc/pc_up.wav");
	gi.soundindex ("misc/talk1.wav");

	gi.soundindex ("misc/udeath.wav");

	// gibs
	gi.soundindex ("items/respawn1.wav");

	// sexed sounds
	gi.soundindex ("*death1.wav");
	gi.soundindex ("*death2.wav");
	gi.soundindex ("*death3.wav");
	gi.soundindex ("*death4.wav");
	gi.soundindex ("*fall1.wav");
	gi.soundindex ("*fall2.wav");	
	gi.soundindex ("*gurp1.wav");		// drowning damage
	gi.soundindex ("*gurp2.wav");	
	gi.soundindex ("*jump1.wav");		// player jump
	gi.soundindex ("*pain25_1.wav");
	gi.soundindex ("*pain25_2.wav");
	gi.soundindex ("*pain50_1.wav");
	gi.soundindex ("*pain50_2.wav");
	gi.soundindex ("*pain75_1.wav");
	gi.soundindex ("*pain75_2.wav");
	gi.soundindex ("*pain100_1.wav");
	gi.soundindex ("*pain100_2.wav");

/*	if (coop->value || deathmatch->value)
	{*/
		gi.modelindex ("#w_blaster.md2");
		gi.modelindex ("#w_shotgun.md2");
		gi.modelindex ("#w_sshotgun.md2");
		gi.modelindex ("#w_machinegun.md2");
		gi.modelindex ("#w_chaingun.md2");
		gi.modelindex ("#a_grenades.md2");
		gi.modelindex ("#w_glauncher.md2");
		gi.modelindex ("#w_rlauncher.md2");
		gi.modelindex ("#w_hyperblaster.md2");
		gi.modelindex ("#w_railgun.md2");
		gi.modelindex ("#w_bfg.md2");

		gi.modelindex ("#w_phalanx.md2");
		gi.modelindex ("#w_ripper.md2");
//	}


	//-------------------

	gi.soundindex ("player/gasp1.wav");		// gasping for air
	gi.soundindex ("player/gasp2.wav");		// head breaking surface, not gasping

	gi.soundindex ("player/watr_in.wav");	// feet hitting water
	gi.soundindex ("player/watr_out.wav");	// feet leaving water

	gi.soundindex ("player/watr_un.wav");	// head going underwater
	
	gi.soundindex ("player/u_breath1.wav");
	gi.soundindex ("player/u_breath2.wav");

	gi.soundindex ("items/pkup.wav");		// bonus item pickup
	gi.soundindex ("world/land.wav");		// landing thud
	gi.soundindex ("misc/h2ohit1.wav");		// landing splash

	gi.soundindex ("items/damage.wav");
	gi.soundindex ("items/protect.wav");
	gi.soundindex ("items/protect4.wav");
	gi.soundindex ("weapons/noammo.wav");

	gi.soundindex ("infantry/inflies1.wav");

	sm_meat_index = gi.modelindex ("models/objects/gibs/sm_meat/tris.md2");
	gi.modelindex ("models/objects/gibs/arm/tris.md2");
	gi.modelindex ("models/objects/gibs/bone/tris.md2");
	gi.modelindex ("models/objects/gibs/bone2/tris.md2");
	gi.modelindex ("models/objects/gibs/chest/tris.md2");
	skullindex = gi.modelindex ("models/objects/gibs/skull/tris.md2");
	headindex = gi.modelindex ("models/objects/gibs/head2/tris.md2");

//
// Setup light animation tables. 'a' is total darkness, 'z' is doublebright.
//

	// 0 normal
	gi.configstring(CS_LIGHTS+0, "m");
	
	// 1 FLICKER (first variety)
	gi.configstring(CS_LIGHTS+1, "mmnmmommommnonmmonqnmmo");
	
	// 2 SLOW STRONG PULSE
	gi.configstring(CS_LIGHTS+2, "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba");
	
	// 3 CANDLE (first variety)
	gi.configstring(CS_LIGHTS+3, "mmmmmaaaaammmmmaaaaaabcdefgabcdefg");
	
	// 4 FAST STROBE
	gi.configstring(CS_LIGHTS+4, "mamamamamama");
	
	// 5 GENTLE PULSE 1
	gi.configstring(CS_LIGHTS+5,"jklmnopqrstuvwxyzyxwvutsrqponmlkj");
	
	// 6 FLICKER (second variety)
	gi.configstring(CS_LIGHTS+6, "nmonqnmomnmomomno");
	
	// 7 CANDLE (second variety)
	gi.configstring(CS_LIGHTS+7, "mmmaaaabcdefgmmmmaaaammmaamm");
	
	// 8 CANDLE (third variety)
	gi.configstring(CS_LIGHTS+8, "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa");
	
	// 9 SLOW STROBE (fourth variety)
	gi.configstring(CS_LIGHTS+9, "aaaaaaaazzzzzzzz");
	
	// 10 FLUORESCENT FLICKER
	gi.configstring(CS_LIGHTS+10, "mmamammmmammamamaaamammma");

	// 11 SLOW PULSE NOT FADE TO BLACK
	gi.configstring(CS_LIGHTS+11, "abcdefghijklmnopqrrqponmlkjihgfedcba");
	
	// styles 32-62 are assigned by the light program for switchable lights

	// 63 testing
	gi.configstring(CS_LIGHTS+63, "a");

//----------------------------------------------


	//pre searched items
	Fdi_GRAPPLE			= FindItem ("Grapple");
	Fdi_BLASTER			= FindItem ("Blaster");
	Fdi_SHOTGUN			= FindItem ("Shotgun");
	Fdi_SUPERSHOTGUN	= FindItem ("Super Shotgun");
	Fdi_MACHINEGUN		= FindItem ("Machinegun");
	Fdi_CHAINGUN		= FindItem ("Chaingun");
	Fdi_GRENADES		= FindItem ("Grenades");
	Fdi_GRENADELAUNCHER	= FindItem ("Grenade Launcher");
	Fdi_ROCKETLAUNCHER	= FindItem ("Rocket Launcher");
	Fdi_HYPERBLASTER	= FindItem ("HyperBlaster");
	Fdi_RAILGUN			= FindItem ("Railgun");
	Fdi_BFG				= FindItem ("BFG10K");
	Fdi_PHALANX			= FindItem ("Phalanx");
	Fdi_BOOMER			= FindItem ("Ionripper");
	Fdi_TRAP			= FindItem ("Trap");

	Fdi_SHELLS			= FindItem ("Shells");
	Fdi_BULLETS			= FindItem ("Bullets");
	Fdi_CELLS			= FindItem ("Cells");
	Fdi_ROCKETS			= FindItem ("Rockets");
	Fdi_SLUGS			= FindItem ("Slugs");
	Fdi_MAGSLUGS		= FindItem ("Mag Slug");

	memset(ExplIndex,0,sizeof(ExplIndex));
}


