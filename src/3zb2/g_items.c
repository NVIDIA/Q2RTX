#include "header/local.h"
#include "header/bot.h"
#include "header/ctf.h"

qboolean	Pickup_Weapon (edict_t *ent, edict_t *other);
void		Use_Weapon (edict_t *ent, gitem_t *inv);
void		Use_Weapon2 (edict_t *ent, gitem_t *inv);
void		Drop_Weapon (edict_t *ent, gitem_t *inv);
/*
void Weapon_Blaster (edict_t *ent);
void Weapon_Shotgun (edict_t *ent);
void Weapon_SuperShotgun (edict_t *ent);
void Weapon_Machinegun (edict_t *ent);
void Weapon_Chaingun (edict_t *ent);
void Weapon_HyperBlaster (edict_t *ent);
void Weapon_RocketLauncher (edict_t *ent);
void Weapon_Grenade (edict_t *ent);
void Weapon_GrenadeLauncher (edict_t *ent);
void Weapon_Railgun (edict_t *ent);
void Weapon_BFG (edict_t *ent);

// RAFAEL
void Weapon_Ionripper (edict_t *ent);
void Weapon_Phalanx (edict_t *ent);
void Weapon_Trap (edict_t *ent);
*/
gitem_armor_t jacketarmor_info	= { 25,  50, .30, .00, ARMOR_JACKET};
gitem_armor_t combatarmor_info	= { 50, 100, .60, .30, ARMOR_COMBAT};
gitem_armor_t bodyarmor_info	= {100, 200, .80, .60, ARMOR_BODY};

int	jacket_armor_index;
int	combat_armor_index;
int	body_armor_index;
static int	power_screen_index;
static int	power_shield_index;

#define HEALTH_IGNORE_MAX	1
#define HEALTH_TIMED		2

void Use_Quad (edict_t *ent, gitem_t *item);
// RAFAEL
void Use_QuadFire (edict_t *ent, gitem_t *item);

static int	quad_drop_timeout_hack;
// RAFAEL
static int	quad_fire_drop_timeout_hack;

//======================================================================


/*
===============
GetItemByIndex
===============
*/
gitem_t	*GetItemByIndex (int index)
{
	if (index == 0 || index >= game.num_items)
		return NULL;

	return &itemlist[index];
}


/*
===============
FindItemByClassname

===============
*/
gitem_t	*FindItemByClassname (char *classname)
{
	int		i;
	gitem_t	*it;

	it = itemlist;
	for (i=0 ; i<game.num_items ; i++, it++)
	{
		if (!it->classname)
			continue;
		if (!Q_stricmp(it->classname, classname))
			return it;
	}

	return NULL;
}

/*
===============
FindItem

===============
*/
gitem_t	*FindItem (char *pickup_name)
{
	int		i;
	gitem_t	*it;

	it = itemlist;
	for (i=0 ; i<game.num_items ; i++, it++)
	{
		if (!it->pickup_name)
			continue;
		if (!Q_stricmp(it->pickup_name, pickup_name))
			return it;
	}

	return NULL;
}

//======================================================================

void DoRespawn (edict_t *ent)
{
	if (ent->team)
	{
		edict_t	*master;
		int	count;
		int choice;

		master = ent->teammaster;

//ZOID
//in ctf, when we are weapons stay, only the master of a team of weapons
//is spawned
		if (ctf->value &&
			((int)dmflags->value & DF_WEAPONS_STAY) &&
			master->item && (master->item->flags & IT_WEAPON))
			ent = master;
		else {
//ZOID

		for (count = 0, ent = master; ent; ent = ent->chain, count++)
			;

		choice = rand() % count;

		for (count = 0, ent = master; count < choice; ent = ent->chain, count++)
			;
		}
	}

	ent->svflags &= ~SVF_NOCLIENT;
	ent->solid = SOLID_TRIGGER;
	gi.linkentity (ent);

	if(ent->classname[0] == 'R') return;

	// send an effect
	ent->s.event = EV_ITEM_RESPAWN;
}

void SetRespawn (edict_t *ent, float delay)
{
	ent->flags |= FL_RESPAWN;
	ent->svflags |= SVF_NOCLIENT;
	ent->solid = SOLID_NOT;
	ent->nextthink = level.time + delay;
	ent->think = DoRespawn;
	gi.linkentity (ent);
}


//======================================================================

qboolean Pickup_Powerup (edict_t *ent, edict_t *other)
{
	int		quantity;

	quantity = other->client->pers.inventory[ITEM_INDEX(ent->item)];
	if ((skill->value == 1 && quantity >= 2) || (skill->value >= 2 && quantity >= 1))
		return false;

	if ((coop->value) && (ent->item->flags & IT_STAY_COOP) && (quantity > 0))
		return false;

	other->client->pers.inventory[ITEM_INDEX(ent->item)]++;

	if (deathmatch->value)
	{
		if (!(ent->spawnflags & DROPPED_ITEM) )
			SetRespawn (ent, ent->item->quantity);
		if (((int)dmflags->value & DF_INSTANT_ITEMS) || ((ent->item->use == Use_Quad) && (ent->spawnflags & DROPPED_PLAYER_ITEM)))
		{
			if ((ent->item->use == Use_Quad) && (ent->spawnflags & DROPPED_PLAYER_ITEM))
				quad_drop_timeout_hack = (ent->nextthink - level.time) / FRAMETIME;
			ent->item->use (other, ent->item);
		}
		// RAFAEL
		else if (((int)dmflags->value & DF_INSTANT_ITEMS) || ((ent->item->use == Use_QuadFire) && (ent->spawnflags & DROPPED_PLAYER_ITEM)))
		{
			if ((ent->item->use == Use_QuadFire) && (ent->spawnflags & DROPPED_PLAYER_ITEM))
				quad_fire_drop_timeout_hack = (ent->nextthink - level.time) / FRAMETIME;
			ent->item->use (other, ent->item);
		}
	}

	return true;
}

void Drop_General (edict_t *ent, gitem_t *item)
{
	Drop_Item (ent, item);
	ent->client->pers.inventory[ITEM_INDEX(item)]--;
	ValidateSelectedItem (ent);
}

float Get_yaw (vec3_t vec);
//edict_t *GetBotFlag1();	//チーム1の旗
//edict_t *GetBotFlag2();  //チーム2の旗 
//======================================================================
qboolean Pickup_Navi (edict_t *ent, edict_t *other)
{
	edict_t	*flage,*flagf;
	vec3_t	v;
	int i,j,k;
	qboolean	flg;

	if (!(ent->spawnflags & DROPPED_ITEM) && (deathmatch->value))
	if(	ent->item->quantity && ent->classname[6] != 'F') SetRespawn (ent, ent->item->quantity);

	//on door(up & down)
	if( ent->classname[6] == '3' && ent->union_ent)
	{
		if(ent->target_ent == other /*->client->zc.second_target == ent*/)
		{
//gi.bprintf(PRINT_HIGH,"get target!\n");
			other->client->zc.zcstate &= ~STS_WAITS;
			other->client->zc.waitin_obj = ent->union_ent;
			if(ent->union_ent->spawnflags & PDOOR_TOGGLE)
			{
				if(ent->union_ent->moveinfo.state == PSTATE_DOWN
					|| ent->union_ent->moveinfo.state == PSTATE_BOTTOM) other->client->zc.zcstate |= STS_W_ONDOORDWN;
				else other->client->zc.zcstate |= STS_W_ONDOORUP;
			}
			else 
			{
				if(ent->union_ent->moveinfo.state == PSTATE_DOWN
					|| ent->union_ent->moveinfo.state == PSTATE_TOP) other->client->zc.zcstate |= STS_W_ONDOORDWN;
				else if(ent->union_ent->moveinfo.state == PSTATE_UP 
					|| ent->union_ent->moveinfo.state == PSTATE_BOTTOM) other->client->zc.zcstate |= STS_W_ONDOORUP;
			}
			//j = other->client->zc.routeindex - 10;
			//ルートのアップデート
			for(i =  - MAX_DOORSEARCH; i <  MAX_DOORSEARCH  ;i++)
			{
				if(i <= 0) j = other->client->zc.routeindex - (MAX_DOORSEARCH - i) ;
				else j = other->client->zc.routeindex + i;
				if(j < 0) continue;
				if(j >= CurrentIndex) continue;
//			if(!Route[j].index) break;
				if((Route[j].state == GRS_ONDOOR
					&& Route[j].ent == ent->union_ent) || Route[j].state == GRS_PUSHBUTTON)
				{
					k = 1;
					flg = false;
					while(1)
					{
						if((j + k) >= CurrentIndex)
						{
//gi.bprintf(PRINT_HIGH,"overflow!!!\n");
							break;
						}
						if((j + k) >= other->client->zc.routeindex)
						{
							Get_RouteOrigin(j + k,v);
							if(fabs(v[2] - other->s.origin[2])> JumpMax)
							{
								if(0/*Route[j].state == GRS_PUSHBUTTON*/)
								{
									if(fabs(Route[j].ent->union_ent->s.origin[2] + 8 - other->s.origin[2])< JumpMax) flg = true;
								}
								else flg = true;
//gi.bprintf(PRINT_HIGH,"hoooo!!!\n");
								break;
							}
						}
						k++;
					}
					if((j + k) < CurrentIndex && flg)
					{
//gi.bprintf(PRINT_HIGH,"set!!!\n");
						other->client->zc.routeindex = j + k;
						break;
					}
				}
				//j++;
			}
			if(!flg)
			{
				other->client->zc.zcstate |= STS_W_DONT;
//				gi.bprintf(PRINT_HIGH,"failed!\n");
			}
			ent->target_ent = NULL;
		}
//else gi.bprintf(PRINT_HIGH,"not target!\n");
		SetRespawn (ent, 1000000);
		ent->solid = SOLID_NOT;
	}
	//roamnavi やめた
	else if( ent->classname[6] == '2')
	{
		//ルートのアップデート
		for(i = 0;i < 10;i++)
		{
			if((other->client->zc.routeindex + i) >= CurrentIndex) break;
			if(!Route[other->client->zc.routeindex + i].index) break;
			if(Route[other->client->zc.routeindex + i].state == GRS_PUSHBUTTON 
				&& Route[other->client->zc.routeindex + i].ent == ent->union_ent)
			{
				other->client->zc.routeindex += i + 1;
				break;
			}
		}
	}

	if(!ctf->value || ent->classname[6] != 'F') return true;
	//ctf navi
	if( ctf->value && ent->classname[6] == 'F')
	{
		if(other->client->resp.ctf_team == CTF_TEAM1)
		{
			flage = bot_team_flag2;//GetBotFlag2();
			flagf = bot_team_flag1;//GetBotFlag1();
			if( other->moveinfo.state == CARRIER || other->moveinfo.state == SUPPORTER)
			{
				if(ent->owner != NULL)	other->target_ent = ent->owner;
			}
			else if(other->moveinfo.state == GETTER)
			{
				if(flage->solid == SOLID_NOT && flagf->solid != SOLID_NOT)
				{
					if(ent->owner != NULL)	other->target_ent = ent->owner;
				}
				else
				{
					if(ent->target_ent != NULL) other->target_ent = ent->target_ent;
				}				
			}
			else if( other->moveinfo.state == DEFENDER)
			{
				if(ent->owner != NULL)	other->target_ent = ent->owner;
			}
		}
		else if(other->client->resp.ctf_team == CTF_TEAM2)
		{
			flage = bot_team_flag1;//GetBotFlag1();
			flagf = bot_team_flag2;//GetBotFlag2();
			if( other->moveinfo.state == CARRIER || other->moveinfo.state == SUPPORTER)
			{
				if(ent->target_ent != NULL)	other->target_ent = ent->target_ent;
			}
			else if(other->moveinfo.state == GETTER)
			{
				if(flage->solid == SOLID_NOT && flagf->solid != SOLID_NOT)
				{
					if(ent->target_ent != NULL)	other->target_ent = ent->target_ent;
				}
				else
				{
					if(ent->owner != NULL) other->target_ent = ent->owner;
				}				
			}
			else if( other->moveinfo.state == DEFENDER)
			{
				if(ent->target_ent != NULL)	other->target_ent = ent->target_ent;
			}
		}
	}


	return true;
}

qboolean Pickup_Adrenaline (edict_t *ent, edict_t *other)
{
	if (!deathmatch->value)
		other->max_health += 1;

	if (other->health < other->max_health)
		other->health = other->max_health;

	if (!(ent->spawnflags & DROPPED_ITEM) && (deathmatch->value))
		SetRespawn (ent, ent->item->quantity);

	return true;
}

qboolean Pickup_AncientHead (edict_t *ent, edict_t *other)
{
	other->max_health += 2;

	if (!(ent->spawnflags & DROPPED_ITEM) && (deathmatch->value))
		SetRespawn (ent, ent->item->quantity);

	return true;
}

qboolean Pickup_Bandolier (edict_t *ent, edict_t *other)
{
	gitem_t	*item;
	int		index;

	if (other->client->pers.max_bullets < 250)
		other->client->pers.max_bullets = 250;
	if (other->client->pers.max_shells < 150)
		other->client->pers.max_shells = 150;
	if (other->client->pers.max_cells < 250)
		other->client->pers.max_cells = 250;
	if (other->client->pers.max_slugs < 75)
		other->client->pers.max_slugs = 75;
	// RAFAEL
	if (other->client->pers.max_magslug < 75)
		other->client->pers.max_magslug = 75;

	item = Fdi_BULLETS;//FindItem("Bullets");
	if (item)
	{
		index = ITEM_INDEX(item);
		other->client->pers.inventory[index] += item->quantity;
		if (other->client->pers.inventory[index] > other->client->pers.max_bullets)
			other->client->pers.inventory[index] = other->client->pers.max_bullets;
	}

	item = Fdi_SHELLS;//FindItem("Shells");
	if (item)
	{
		index = ITEM_INDEX(item);
		other->client->pers.inventory[index] += item->quantity;
		if (other->client->pers.inventory[index] > other->client->pers.max_shells)
			other->client->pers.inventory[index] = other->client->pers.max_shells;
	}

	if (!(ent->spawnflags & DROPPED_ITEM) && (deathmatch->value))
		SetRespawn (ent, ent->item->quantity);

	return true;
}

qboolean Pickup_Pack (edict_t *ent, edict_t *other)
{
	gitem_t	*item;
	int		index;

	if (other->client->pers.max_bullets < 300)
		other->client->pers.max_bullets = 300;
	if (other->client->pers.max_shells < 200)
		other->client->pers.max_shells = 200;
	if (other->client->pers.max_rockets < 100)
		other->client->pers.max_rockets = 100;
	if (other->client->pers.max_grenades < 100)
		other->client->pers.max_grenades = 100;
	if (other->client->pers.max_cells < 300)
		other->client->pers.max_cells = 300;
	if (other->client->pers.max_slugs < 100)
		other->client->pers.max_slugs = 100;
	// RAFAEL
	if (other->client->pers.max_magslug < 100)
		other->client->pers.max_magslug = 100;

	item = Fdi_BULLETS;//FindItem("Bullets");
	if (item)
	{
		index = ITEM_INDEX(item);
		other->client->pers.inventory[index] += item->quantity;
		if (other->client->pers.inventory[index] > other->client->pers.max_bullets)
			other->client->pers.inventory[index] = other->client->pers.max_bullets;
	}

	item = Fdi_SHELLS;//FindItem("Shells");
	if (item)
	{
		index = ITEM_INDEX(item);
		other->client->pers.inventory[index] += item->quantity;
		if (other->client->pers.inventory[index] > other->client->pers.max_shells)
			other->client->pers.inventory[index] = other->client->pers.max_shells;
	}

	item = Fdi_CELLS;//FindItem("Cells");
	if (item)
	{
		index = ITEM_INDEX(item);
		other->client->pers.inventory[index] += item->quantity;
		if (other->client->pers.inventory[index] > other->client->pers.max_cells)
			other->client->pers.inventory[index] = other->client->pers.max_cells;
	}

	if (item)
	{
		item = Fdi_GRENADES;//FindItem("Grenades");
		index = ITEM_INDEX(item);
		other->client->pers.inventory[index] += item->quantity;
		if (other->client->pers.inventory[index] > other->client->pers.max_grenades)
			other->client->pers.inventory[index] = other->client->pers.max_grenades;
	}

	item = Fdi_ROCKETS;//FindItem("Rockets");
	if (item)
	{
		index = ITEM_INDEX(item);
		other->client->pers.inventory[index] += item->quantity;
		if (other->client->pers.inventory[index] > other->client->pers.max_rockets)
			other->client->pers.inventory[index] = other->client->pers.max_rockets;
	}

	item = Fdi_SLUGS;//FindItem("Slugs");
	if (item)
	{
		index = ITEM_INDEX(item);
		other->client->pers.inventory[index] += item->quantity;
		if (other->client->pers.inventory[index] > other->client->pers.max_slugs)
			other->client->pers.inventory[index] = other->client->pers.max_slugs;
	}

	// RAFAEL
	item = Fdi_MAGSLUGS;//FindItem ("Mag Slug");
	if (item)
	{
		index = ITEM_INDEX(item);
		other->client->pers.inventory[index] += item->quantity;
		if (other->client->pers.inventory[index] > other->client->pers.max_magslug)
			other->client->pers.inventory[index] = other->client->pers.max_magslug;
	}

	if (!(ent->spawnflags & DROPPED_ITEM) && (deathmatch->value))
		SetRespawn (ent, ent->item->quantity);

	return true;
}

//======================================================================

void Use_Quad (edict_t *ent, gitem_t *item)
{
	int		timeout;

	ent->client->pers.inventory[ITEM_INDEX(item)]--;
	ValidateSelectedItem (ent);

	if (quad_drop_timeout_hack)
	{
		timeout = quad_drop_timeout_hack;
		quad_drop_timeout_hack = 0;
	}
	else
	{
		timeout = 300;
	}

	if (ent->client->quad_framenum > level.framenum)
		ent->client->quad_framenum += timeout;
	else
		ent->client->quad_framenum = level.framenum + timeout;

	gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage.wav"), 1, ATTN_NORM, 0);
}
// =====================================================================

// RAFAEL
void Use_QuadFire (edict_t *ent, gitem_t *item)
{
	int		timeout;

	ent->client->pers.inventory[ITEM_INDEX(item)]--;
	ValidateSelectedItem (ent);

	if (quad_fire_drop_timeout_hack)
	{
		timeout = quad_fire_drop_timeout_hack;
		quad_fire_drop_timeout_hack = 0;
	}
	else
	{
		timeout = 300;
	}

	if (ent->client->quadfire_framenum > level.framenum)
		ent->client->quadfire_framenum += timeout;
	else
		ent->client->quadfire_framenum = level.framenum + timeout;

	gi.sound(ent, CHAN_ITEM, gi.soundindex("items/quadfire1.wav"), 1, ATTN_NORM, 0);
}
//======================================================================

void Use_Breather (edict_t *ent, gitem_t *item)
{
	ent->client->pers.inventory[ITEM_INDEX(item)]--;
	ValidateSelectedItem (ent);

	if (ent->client->breather_framenum > level.framenum)
		ent->client->breather_framenum += 300;
	else
		ent->client->breather_framenum = level.framenum + 300;

//	gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage.wav"), 1, ATTN_NORM, 0);
}

//======================================================================

void Use_Envirosuit (edict_t *ent, gitem_t *item)
{
	ent->client->pers.inventory[ITEM_INDEX(item)]--;
	ValidateSelectedItem (ent);

	if (ent->client->enviro_framenum > level.framenum)
		ent->client->enviro_framenum += 300;
	else
		ent->client->enviro_framenum = level.framenum + 300;

//	gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage.wav"), 1, ATTN_NORM, 0);
}

//======================================================================

void	Use_Invulnerability (edict_t *ent, gitem_t *item)
{
	ent->client->pers.inventory[ITEM_INDEX(item)]--;
	ValidateSelectedItem (ent);

	if (ent->client->invincible_framenum > level.framenum)
		ent->client->invincible_framenum += 300;
	else
		ent->client->invincible_framenum = level.framenum + 300;

	gi.sound(ent, CHAN_ITEM, gi.soundindex("items/protect.wav"), 1, ATTN_NORM, 0);
}

//======================================================================

void	Use_Silencer (edict_t *ent, gitem_t *item)
{
	ent->client->pers.inventory[ITEM_INDEX(item)]--;
	ValidateSelectedItem (ent);
	ent->client->silencer_shots += 30;

//	gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage.wav"), 1, ATTN_NORM, 0);
}

//======================================================================

qboolean Pickup_Key (edict_t *ent, edict_t *other)
{
	other->client->pers.inventory[ITEM_INDEX(ent->item)]++;
	return true;
}

//======================================================================

qboolean Add_Ammo (edict_t *ent, gitem_t *item, int count)
{
	int			index;
	int			max;

	if (!ent->client)
		return false;

	if (item->tag == AMMO_BULLETS)
		max = ent->client->pers.max_bullets;
	else if (item->tag == AMMO_SHELLS)
		max = ent->client->pers.max_shells;
	else if (item->tag == AMMO_ROCKETS)
		max = ent->client->pers.max_rockets;
	else if (item->tag == AMMO_GRENADES)
		max = ent->client->pers.max_grenades;
	else if (item->tag == AMMO_CELLS)
		max = ent->client->pers.max_cells;
	else if (item->tag == AMMO_SLUGS)
		max = ent->client->pers.max_slugs;
	// RAFAEL
	else if (item->tag == AMMO_MAGSLUG)
		max = ent->client->pers.max_magslug;
	// RAFAEL
	else if (item->tag == AMMO_TRAP)
		max = ent->client->pers.max_trap;
	else
		return false;

	index = ITEM_INDEX(item);

	if (ent->client->pers.inventory[index] == max)
		return false;

	ent->client->pers.inventory[index] += count;

//ponko
	if(chedit->value && ent == &g_edicts[1]) ent->client->pers.inventory[index] = 0;
//ponko

	if (ent->client->pers.inventory[index] > max)
		ent->client->pers.inventory[index] = max;

	return true;
}

qboolean Pickup_Ammo (edict_t *ent, edict_t *other)
{
	int		count;

	if (ent->count)
		count = ent->count;
	else
		count = ent->item->quantity;

	if (!Add_Ammo (other, ent->item, count))
		return false;

	if (!(ent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)) && (deathmatch->value))
		SetRespawn (ent, 30);
	return true;
}

void Drop_Ammo (edict_t *ent, gitem_t *item)
{
	edict_t	*dropped;
	int		index;

	index = ITEM_INDEX(item);
	dropped = Drop_Item (ent, item);
	if (ent->client->pers.inventory[index] >= item->quantity)
		dropped->count = item->quantity;
	else
		dropped->count = ent->client->pers.inventory[index];
	ent->client->pers.inventory[index] -= dropped->count;
	ValidateSelectedItem (ent);
}


//======================================================================

void MegaHealth_think (edict_t *self)
{
	if (self->owner->health > self->owner->max_health
//ZOID
		&& !CTFHasRegeneration(self->owner)
//ZOID		
		)
	{
		self->nextthink = level.time + 1;
		self->owner->health -= 1;
		return;
	}

	if (!(self->spawnflags & DROPPED_ITEM) && (deathmatch->value))
		SetRespawn (self, 20);
	else
		G_FreeEdict (self);
}

qboolean Pickup_Health (edict_t *ent, edict_t *other)
{
	if (!(ent->style & HEALTH_IGNORE_MAX))
		if (other->health >= other->max_health)
			return false;

//ZOID
	if (other->health >= 250 && ent->count > 25)
		return false;
//ZOID

	other->health += ent->count;

//ZOID
	if (other->health > 250 && ent->count > 25)
		other->health = 250;
//ZOID

	if (ent->count == 2)
		ent->item->pickup_sound = "items/s_health.wav";
	else if (ent->count == 10)
		ent->item->pickup_sound = "items/n_health.wav";
	else if (ent->count == 25)
		ent->item->pickup_sound = "items/l_health.wav";
	else // (ent->count == 100)
		ent->item->pickup_sound = "items/m_health.wav";

	if (!(ent->style & HEALTH_IGNORE_MAX))
	{
		if (other->health > other->max_health)
			other->health = other->max_health;
	}
//ZOID
	if ((ent->style & HEALTH_TIMED)
		&& !CTFHasRegeneration(other)
//ZOID
	)
	{
		ent->think = MegaHealth_think;
		ent->nextthink = level.time + 5;
		ent->owner = other;
		ent->flags |= FL_RESPAWN;
		ent->svflags |= SVF_NOCLIENT;
		ent->solid = SOLID_NOT;
	}
	else
	{
		if (!(ent->spawnflags & DROPPED_ITEM) && (deathmatch->value))
			SetRespawn (ent, 30);
	}

	return true;
}

//======================================================================

int ArmorIndex (edict_t *ent)
{
	if (!ent->client)
		return 0;

	if (ent->client->pers.inventory[jacket_armor_index] > 0)
		return jacket_armor_index;

	if (ent->client->pers.inventory[combat_armor_index] > 0)
		return combat_armor_index;

	if (ent->client->pers.inventory[body_armor_index] > 0)
		return body_armor_index;

	return 0;
}

qboolean Pickup_Armor (edict_t *ent, edict_t *other)
{
	int				old_armor_index;
	gitem_armor_t	*oldinfo;
	gitem_armor_t	*newinfo;
	int				newcount;
	float			salvage;
	int				salvagecount;

	// get info on new armor
	newinfo = (gitem_armor_t *)ent->item->info;

	old_armor_index = ArmorIndex (other);

	// handle armor shards specially
	if (ent->item->tag == ARMOR_SHARD)
	{
		if (!old_armor_index)
			other->client->pers.inventory[jacket_armor_index] = 2;
		else
			other->client->pers.inventory[old_armor_index] += 2;
	}

	// if player has no armor, just use it
	else if (!old_armor_index)
	{
		other->client->pers.inventory[ITEM_INDEX(ent->item)] = newinfo->base_count;
	}

	// use the better armor
	else
	{
		// get info on old armor
		if (old_armor_index == jacket_armor_index)
			oldinfo = &jacketarmor_info;
		else if (old_armor_index == combat_armor_index)
			oldinfo = &combatarmor_info;
		else // (old_armor_index == body_armor_index)
			oldinfo = &bodyarmor_info;

		if (newinfo->normal_protection > oldinfo->normal_protection)
		{
			// calc new armor values
			salvage = oldinfo->normal_protection / newinfo->normal_protection;
			salvagecount = salvage * other->client->pers.inventory[old_armor_index];
			newcount = newinfo->base_count + salvagecount;
			if (newcount > newinfo->max_count)
				newcount = newinfo->max_count;

			// zero count of old armor so it goes away
			other->client->pers.inventory[old_armor_index] = 0;

			// change armor to new item with computed value
			other->client->pers.inventory[ITEM_INDEX(ent->item)] = newcount;
//ponko
			if(chedit->value && other == &g_edicts[1]) 
				other->client->pers.inventory[ITEM_INDEX(ent->item)] = 0;
//ponko	
		}
		else
		{
			// calc new armor values
			salvage = newinfo->normal_protection / oldinfo->normal_protection;
			salvagecount = salvage * newinfo->base_count;
			newcount = other->client->pers.inventory[old_armor_index] + salvagecount;
			if (newcount > oldinfo->max_count)
				newcount = oldinfo->max_count;

			// if we're already maxed out then we don't need the new armor
			if (other->client->pers.inventory[old_armor_index] >= newcount)
				return false;

			// update current armor value
			other->client->pers.inventory[old_armor_index] = newcount;
//ponko
			if(chedit->value && other == &g_edicts[1]) 
				other->client->pers.inventory[old_armor_index] = 0;
//ponko			
		}
	}

	if (!(ent->spawnflags & DROPPED_ITEM) && (deathmatch->value))
		SetRespawn (ent, 20);

	return true;
}

//======================================================================

int PowerArmorType (edict_t *ent)
{
	if (!ent->client)
		return POWER_ARMOR_NONE;

	if (!(ent->flags & FL_POWER_ARMOR))
		return POWER_ARMOR_NONE;

	if (ent->client->pers.inventory[power_shield_index] > 0)
		return POWER_ARMOR_SHIELD;

	if (ent->client->pers.inventory[power_screen_index] > 0)
		return POWER_ARMOR_SCREEN;

	return POWER_ARMOR_NONE;
}

void Use_PowerArmor (edict_t *ent, gitem_t *item)
{
	int		index;

	if (ent->flags & FL_POWER_ARMOR)
	{
		ent->flags &= ~FL_POWER_ARMOR;
		gi.sound(ent, CHAN_ITEM, gi.soundindex("misc/power2.wav"), 1, ATTN_NORM, 0);	//FIXME powering down sound
	}
	else
	{
		index = ITEM_INDEX(Fdi_CELLS/*FindItem("Cells")*/);
		if (!ent->client->pers.inventory[index] && !(ent->svflags & SVF_MONSTER))
		{
			gi.cprintf (ent, PRINT_HIGH, "No cells for power armor.\n");
			return;
		}
		ent->flags |= FL_POWER_ARMOR;
		gi.sound(ent, CHAN_ITEM, gi.soundindex("misc/power1.wav"), 1, ATTN_NORM, 0);	//FIXME powering up sound
	}
}

qboolean Pickup_PowerArmor (edict_t *ent, edict_t *other)
{
	int		quantity;

	quantity = other->client->pers.inventory[ITEM_INDEX(ent->item)];

	other->client->pers.inventory[ITEM_INDEX(ent->item)]++;

//ponko
	if(chedit->value && other == &g_edicts[1]) 
		other->client->pers.inventory[ITEM_INDEX(ent->item)] = 0;
//ponko	

	if (deathmatch->value)
	{
		if (!(ent->spawnflags & DROPPED_ITEM) )
			SetRespawn (ent, ent->item->quantity);
		// auto-use for DM only if we didn't already have one
		if (!quantity)
			ent->item->use (other, ent->item);
	}

	return true;
}

void Drop_PowerArmor (edict_t *ent, gitem_t *item)
{
	if ((ent->flags & FL_POWER_ARMOR) && (ent->client->pers.inventory[ITEM_INDEX(item)] == 1))
		Use_PowerArmor (ent, item);
	Drop_General (ent, item);
}
//======================================================================

/*
===============
Touch_Item
===============
*/
void Touch_Item (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	int k;

	//don't pickup tech when chaining
	if (ctf->value && chedit->value)
	{
		if(ent->classname[5] =='t') return;
	}
	if (strcmp(other->classname, "player"))
		return;
	if (ent->classname[0] == 'R')
	{
		if(!(other->svflags & SVF_MONSTER))	return;
		if(ent->classname[6] == 'F'&& other->target_ent != NULL)
		{
			if(other->target_ent != ent) return;
//			else if(other->moveinfo.state == SUPPORTER) return;
		}
	}
	if (other->health < 1)
		return;		// dead people can't pickup
	if (!ent->item->pickup)
		return;		// not a grabbable item?
	if (!ent->item->pickup(ent, other))
		return;		// player can't hold it

	if (!(other->svflags & SVF_MONSTER))
	{

		// flash the screen
		other->client->bonus_alpha = 0.25;	
	
		// show icon and name on status bar
		other->client->ps.stats[STAT_PICKUP_ICON] = gi.imageindex(ent->item->icon);
		other->client->ps.stats[STAT_PICKUP_STRING] = CS_ITEMS+ITEM_INDEX(ent->item);
		other->client->pickup_msg_time = level.time + 3.0;

		// change selected item
		if (ent->item->use)
			other->client->pers.selected_item = other->client->ps.stats[STAT_SELECTED_ITEM] = ITEM_INDEX(ent->item);
		
	}
	else
	{
		// change selected item
		if (ent->item->use)
		{
			k = Get_KindWeapon(ent->item);
			if(k > WEAP_BLASTER)
			{
				if(Bot[other->client->zc.botindex].param[BOP_PRIWEP] == k) ent->item->use(other,ent->item);
				else if(k != Get_KindWeapon(other->client->pers.weapon))
				{
					if(Bot[other->client->zc.botindex].param[BOP_SECWEP] == k) ent->item->use(other,ent->item);
				}
			}

		
		}	
	}
	if(ent->classname[0] != 'R')
	{
		gi.sound(other, CHAN_ITEM, gi.soundindex(ent->item->pickup_sound), 1, ATTN_NORM, 0);
		PlayerNoise(ent, ent->s.origin, PNOISE_SELF); //ponko
		G_UseTargets (ent, other);
	}
//	else gi.bprintf(PRINT_HIGH,"get %s %x inv %i!\n",ent->classname,ent->spawnflags,other->client->pers.inventory[ITEM_INDEX(ent->item)]);

	k = false;
	//flag set ファンクションの上にある場合は無視
	if(ent->groundentity) if(ent->groundentity->union_ent) k = true;

	//route update
	if(!k && chedit->value && CurrentIndex < MAXNODES && other == &g_edicts[1])
	{
		if((ent->classname[0] == 'w' 
			|| (ent->classname[0] =='i' &&
			(ent->classname[5] == 'q'
			|| ent->classname[5] =='t'
			|| ent->classname[5] =='f'
			|| ent->classname[5] =='i'
			|| ent->classname[5] =='p'
			|| ent->classname[5] =='s'
			|| ent->classname[5] =='b'
			|| ent->classname[5] =='e'
			|| ent->classname[5] =='a'))
			|| (ent->classname[0] =='i' && ent->classname[5] =='h' && ent->classname[12] =='m')
			|| (ent->classname[0] =='a') 
			)
			&& !(ent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)))
		{
//			gi.bprintf(PRINT_HIGH,"woohoo!\n");
			VectorCopy(/*ent->s.origin*/ent->monsterinfo.last_sighting,Route[CurrentIndex].Pt);
			Route[CurrentIndex].ent = ent;
			if(ent == bot_team_flag1) { Route[CurrentIndex].state = GRS_REDFLAG;/*gi.bprintf(PRINT_HIGH,"woohoo!\n");*/}
			else if(ent == bot_team_flag2) { Route[CurrentIndex].state = GRS_BLUEFLAG;/*gi.bprintf(PRINT_HIGH,"woohoo!\n");*/}
			else Route[CurrentIndex].state = GRS_ITEMS;
			if(++CurrentIndex < MAXNODES)
			{
				gi.bprintf(PRINT_HIGH,"Last %i pod(s).\n",MAXNODES - CurrentIndex);
				memset(&Route[CurrentIndex],0,sizeof(route_t));
				Route[CurrentIndex].index = Route[CurrentIndex - 1].index +1;
			}
		}
	}

	//respawn set
	if (ent->flags & FL_RESPAWN)
		ent->flags &= ~FL_RESPAWN;
	else if(ent->classname[6] != 'F') G_FreeEdict (ent);
}

//======================================================================

static void drop_temp_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == ent->owner)
		return;

	Touch_Item (ent, other, plane, surf);
}

static void drop_make_touchable (edict_t *ent)
{
	ent->touch = Touch_Item;
	if (deathmatch->value)
	{
		ent->nextthink = level.time + 29;
		ent->think = G_FreeEdict;
	}
}

edict_t *Drop_Item (edict_t *ent, gitem_t *item)
{
	edict_t	*dropped;
	vec3_t	forward, right;
	vec3_t	offset;

	dropped = G_Spawn();

	dropped->classname = item->classname;
	dropped->item = item;
	dropped->spawnflags = DROPPED_ITEM;
	dropped->s.effects = item->world_model_flags;
	dropped->s.renderfx = RF_GLOW;
	VectorSet (dropped->mins, -15, -15, -15);
	VectorSet (dropped->maxs, 15, 15, 15);
	gi.setmodel (dropped, dropped->item->world_model);
	dropped->solid = SOLID_TRIGGER;
	dropped->movetype = MOVETYPE_TOSS;  
	dropped->touch = drop_temp_touch;
	dropped->owner = ent;

	if (ent->client)
	{
		trace_t	trace;

		AngleVectors (ent->client->v_angle, forward, right, NULL);
		VectorSet(offset, 24, 0, -16);
		G_ProjectSource (ent->s.origin, offset, forward, right, dropped->s.origin);
		trace = gi.trace (ent->s.origin, dropped->mins, dropped->maxs,
			dropped->s.origin, ent, CONTENTS_SOLID);
		VectorCopy (trace.endpos, dropped->s.origin);
	}
	else
	{
		AngleVectors (ent->s.angles, forward, right, NULL);
		VectorCopy (ent->s.origin, dropped->s.origin);
	}

	VectorScale (forward, 100, dropped->velocity);
	dropped->velocity[2] = 300;

	dropped->think = drop_make_touchable;
	dropped->nextthink = level.time + 1;

	gi.linkentity (dropped);

	return dropped;
}

void Use_Item (edict_t *ent, edict_t *other, edict_t *activator)
{
	ent->svflags &= ~SVF_NOCLIENT;
	ent->use = NULL;

	if (ent->spawnflags & 2)	// NO_TOUCH
	{
		ent->solid = SOLID_BBOX;
		ent->touch = NULL;
	}
	else
	{
		ent->solid = SOLID_TRIGGER;
		ent->touch = Touch_Item;
	}

	gi.linkentity (ent);
}

//======================================================================

/*
================
droptofloor
================
*/
void droptofloor (edict_t *ent)
{
	vec3_t  trmin,trmax,min,mins,maxs;
	float	i,j,k,yaw;

	gitem_t		*it;		//j
	edict_t		*it_ent;	//j	

	trace_t		tr,trx;
	vec3_t		dest;
	float		*v;

	v = tv(-15,-15,-15);
	VectorCopy (v, ent->mins);
	v = tv(15,15,15);
	VectorCopy (v, ent->maxs);

	if (ent->model)
		gi.setmodel (ent, ent->model);
	else
		gi.setmodel (ent, ent->item->world_model);
	if(ent->classname[6] == 'F')	ent->s.modelindex = 0; //かくせ

	ent->solid = SOLID_TRIGGER;
	ent->movetype = MOVETYPE_TOSS;  
	ent->touch = Touch_Item;

	v = tv(0,0,-128);
	VectorAdd (ent->s.origin, v, dest);

	tr = gi.trace (ent->s.origin, ent->mins, ent->maxs, dest, ent, MASK_SOLID);
	if (tr.startsolid && !chedit->value && ent->classname[6] != 'F')
	{
//		gi.dprintf ("droptofloor: %s startsolid at %s\n", ent->classname, vtos(ent->s.origin));
		// RAFAEL
		if (strcmp (ent->classname, "foodcube") == 0)
		{
			VectorCopy (ent->s.origin, tr.endpos);
			ent->velocity[2] = 0;
		}
		else
		{
//			gi.dprintf ("droptofloor: %s startsolid at %s\n", ent->classname, vtos(ent->s.origin));
			G_FreeEdict (ent);
			return;
		}
	}

	VectorCopy (tr.endpos, ent->s.origin);

	if (ent->team)
	{
		ent->flags &= ~FL_TEAMSLAVE;
		ent->chain = ent->teamchain;
		ent->teamchain = NULL;

		ent->svflags |= SVF_NOCLIENT;
		ent->solid = SOLID_NOT;
		if (ent == ent->teammaster)
		{
			ent->nextthink = level.time + FRAMETIME;
			ent->think = DoRespawn;
		}
	}

	if (ent->spawnflags & 2)	// NO_TOUCH
	{
		ent->solid = SOLID_BBOX;
		ent->touch = NULL;
		ent->s.effects &= ~EF_ROTATE;
		ent->s.renderfx &= ~RF_GLOW;
	}

	if (ent->spawnflags & 1)	// TRIGGER_SPAWN
	{
		ent->svflags |= SVF_NOCLIENT;
		ent->solid = SOLID_NOT;
		ent->use = Use_Item;
	}

	gi.linkentity (ent);

	if(ent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)) return;

//	if(ctf->value && chedit->value) ent->moveinfo.speed = 0;

	if(ent->classname[0] == 'w' || ent->classname[0] == 'i' || ent->classname[0] == 'a')
	{
		k = 0;
		VectorCopy(ent->s.origin,min);
		VectorSet (mins, -16, -16, -16);
		VectorSet (maxs, 16, 16, 16);
		min[2] -= 128;
		for(i = 0 ; i < 8;i++)
		{
			if(i < 4 )
			{
				yaw = 90 * i -180;
				yaw = yaw * M_PI * 2 / 360;
				for( j = 32 ; j < 100 ; j +=2 )
				{
					trmin[0] = ent->s.origin[0] + cos(yaw) * j;
					trmin[1] = ent->s.origin[1] + sin(yaw) * j;
					trmin[2] = ent->s.origin[2];
					VectorCopy(trmin,trmax);
					trmax[2] -= 128;
					tr = gi.trace (trmin, mins, maxs, trmax,ent, MASK_PLAYERSOLID );
					trx = gi.trace (trmin, mins, maxs, trmax,ent, CONTENTS_WATER );
					VectorCopy(tr.endpos,trmax);
					//trmax[2] += 16;
					if( 0/*(trmin[2] - trx.endpos[2]) <= 39 
						&&ent->classname[0] == 'w' && k == 0 
						&& !(gi.pointcontents (trmin) & CONTENTS_WATER)
						&& (gi.pointcontents (trmax) & CONTENTS_WATER)*/)
					{
						it = FindItem("Roam Navi");
						it_ent = G_Spawn();
						it_ent->classname = it->classname;
						trmin[0] = ent->s.origin[0] + cos(yaw) * (j + trmin[2] - tr.endpos[2] + 100);
						trmin[1] = ent->s.origin[1] + sin(yaw) * (j + trmin[2] - tr.endpos[2] + 100);
						trmin[2] = trmax[2]+8;
						trx = gi.trace (trmax, mins, maxs, trmin,ent, CONTENTS_WATER );
						VectorCopy(trx.endpos,it_ent->s.origin);
						SpawnItem3 (it_ent, it);
						k = -1;
					}
					if(tr.endpos[2] < ent->s.origin[2] - 16 
						&& tr.endpos[2] > min[2] && !tr.allsolid && !tr.startsolid)
					{
						min[2] = tr.endpos[2];
						min[0] = ent->s.origin[0] + cos(yaw) * (j + 16);
						min[1] = ent->s.origin[1] + sin(yaw) * (j + 16);						
						break;
					}
				}
			}
			else
			{
				yaw = 90 * (i - 4)  -135;
				yaw = yaw * M_PI * 2 / 360;
				for( j = 32 ; j < 80 ; j +=2 )
				{
					trmin[0] = ent->s.origin[0] + cos(yaw) *46;
					trmin[1] = ent->s.origin[1] + sin(yaw) *46;
					trmin[2] = ent->s.origin[2];
					VectorCopy(trmin,trmax);
					trmax[2] -= 128;
					tr = gi.trace (trmin, NULL, NULL, trmax,ent, MASK_PLAYERSOLID );
					if(tr.endpos[2] < ent->s.origin[2] - 16 && tr.endpos[2] > min[2] && !tr.allsolid && !tr.startsolid)
					{
						VectorCopy(tr.endpos,min);
						break;
					}
				}
			}
		}
		VectorCopy(min,ent->moveinfo.start_origin);
	}
}

/*
================
droptofloor2
================
*/
void droptofloor2 (edict_t *ent)
{
	vec3_t  trmin,trmax,min,mins,maxs;
	float	i,j,yaw;
	
	trace_t		tr;
	vec3_t		dest;
	float		*v;

	v = tv(-15,-15,-15);
	VectorCopy (v, ent->mins);
	v = tv(8,8,15);
	VectorCopy (v, ent->maxs);
/////////
	if(ent->union_ent && Q_stricmp (ent->classname,"R_navi2")) //2は移動なし
	{
//		dest[0] = (ent->union_ent->s.origin[0] + ent->union_ent->mins[0] + ent->union_ent->s.origin[0] + ent->union_ent->maxs[0])/2;//ent->s.origin[0];
//		dest[1] = (ent->union_ent->s.origin[1] + ent->union_ent->mins[1] + ent->union_ent->s.origin[1] + ent->union_ent->maxs[1])/2;

		dest[0] = (ent->union_ent->s.origin[0] + ent->union_ent->mins[0] + ent->union_ent->s.origin[0] + ent->union_ent->maxs[0])/2;//ent->s.origin[0];
		dest[1] = (ent->union_ent->s.origin[1] + ent->union_ent->mins[1] + ent->union_ent->s.origin[1] + ent->union_ent->maxs[1])/2;

		j = 0;
		for( i = ent->union_ent->s.origin[2] + ent->union_ent->mins[2] /*moveinfo.start_origin[2]+15*/
			; i <= ent->union_ent->s.origin[2] + ent->union_ent->maxs[2] +16/*ent->moveinfo.end_origin[2]+16*/
			; i++)
		{
			dest[2] = i;
			tr = gi.trace(dest,ent->mins,ent->maxs,dest,ent,MASK_SOLID); // | MASK_WATER);
			if((!tr.startsolid && !tr.allsolid) && j == 1)
			{
				j = 2;			
				break;
			}
			else if((tr.startsolid || tr.allsolid) && j == 0 && tr.ent == ent->union_ent) j = 1;

		}
		VectorCopy (dest,ent->s.origin);
		VectorSubtract(ent->s.origin,ent->union_ent->s.origin,ent->moveinfo.dir);
	}
//////////	
/*	if (ent->model)
		gi.setmodel (ent, ent->model);
	else
		gi.setmodel (ent, ent->item->world_model);*/
	ent->s.modelindex = 0;				//かくせ！
//ent->s.modelindex =gi.modelindex ("models/items/armor/body/tris.md2");
	if(Q_stricmp (ent->classname,"R_navi3") == 0) ent->solid = SOLID_NOT;
	else ent->solid = SOLID_TRIGGER;
	ent->movetype = MOVETYPE_TOSS;  
	ent->touch = Touch_Item;
	ent->use = NULL;

	v = tv(0,0,-128);
	VectorAdd (ent->s.origin, v, dest);

	tr = gi.trace (ent->s.origin, ent->mins, ent->maxs, dest, ent, MASK_SOLID);
	if (tr.startsolid && ent->classname[0] != 'R' && ent->classname[6] != 'X')
	{
//		gi.dprintf ("droptofloor: %s startsolid at %s\n", ent->classname, vtos(ent->s.origin));
		G_FreeEdict (ent);
		return;
	}

	VectorCopy (tr.endpos, ent->s.origin);

	if (ent->team)
	{
		ent->flags &= ~FL_TEAMSLAVE;
		ent->chain = ent->teamchain;
		ent->teamchain = NULL;

		ent->svflags |= SVF_NOCLIENT;
		ent->solid = SOLID_NOT;
		if (ent == ent->teammaster)
		{
			ent->nextthink = level.time + FRAMETIME;
			ent->think = DoRespawn;
		}
	}

	if (ent->spawnflags & 2)	// NO_TOUCH
	{
		ent->solid = SOLID_BBOX;
		ent->touch = NULL;
		ent->s.effects &= ~EF_ROTATE;
		ent->s.renderfx &= ~RF_GLOW;
	}

	if (ent->spawnflags & 1)	// TRIGGER_SPAWN
	{
		ent->svflags |= SVF_NOCLIENT;
		ent->solid = SOLID_NOT;
		ent->use = Use_Item;
	}

	gi.linkentity (ent);

	if(1)
	{
		VectorCopy(ent->s.origin,min);
		VectorSet (mins, -15, -15, -15);
		VectorSet (maxs, 8, 8, 0);
		min[2] -= 128;
		for(i = 0 ; i < 8;i++)
		{
			if(i < 4 )
			{
				yaw = 90 * i -180;
				yaw = yaw * M_PI * 2 / 360;
				for( j = 32 ; j < 80 ; j +=2 )
				{
					trmin[0] = ent->s.origin[0] + cos(yaw) * j;
					trmin[1] = ent->s.origin[1] + sin(yaw) * j;
					trmin[2] = ent->s.origin[2];
					VectorCopy(trmin,trmax);
					trmax[2] -= 128;
					tr = gi.trace (trmin, mins, maxs, trmax,ent, MASK_PLAYERSOLID );
					if(tr.endpos[2] < ent->s.origin[2] - 16 && tr.endpos[2] > min[2] && !tr.allsolid && !tr.startsolid)
					{
						min[2] = tr.endpos[2];
						min[0] = ent->s.origin[0] + cos(yaw) * (j + 16);
						min[1] = ent->s.origin[1] + sin(yaw) * (j + 16);						
						break;
					}
				}
			}
			else
			{
				yaw = 90 * (i - 4)  -135;
				yaw = yaw * M_PI * 2 / 360;
				for( j = 32 ; j < 80 ; j +=2 )
				{
					trmin[0] = ent->s.origin[0] + cos(yaw) *46;
					trmin[1] = ent->s.origin[1] + sin(yaw) *46;
					trmin[2] = ent->s.origin[2];
					VectorCopy(trmin,trmax);
					trmax[2] -= 128;
					tr = gi.trace (trmin, NULL, NULL, trmax,ent, MASK_PLAYERSOLID );
					if(tr.endpos[2] < ent->s.origin[2] - 16 && tr.endpos[2] > min[2] && !tr.allsolid && !tr.startsolid)
					{
						VectorCopy(tr.endpos,min);
						break;
					}
				}
			}
		}
		VectorCopy(min,ent->moveinfo.start_origin);
	}
}

/*
===============
PrecacheItem

Precaches all data needed for a given item.
This will be called for each item spawned in a level,
and for each item in each client's inventory.
===============
*/
void PrecacheItem (gitem_t *it)
{
	char	*s, *start;
	char	data[MAX_QPATH];
	int		len;
	gitem_t	*ammo;

	if (!it)
		return;

	if (it->pickup_sound)
		gi.soundindex (it->pickup_sound);
	if (it->world_model)
		gi.modelindex (it->world_model);
	if (it->view_model)
		gi.modelindex (it->view_model);
	if (it->icon)
		gi.imageindex (it->icon);

	// parse everything for its ammo
	if (it->ammo && it->ammo[0])
	{
		ammo = FindItem (it->ammo);
		if (ammo != it)
			PrecacheItem (ammo);
	}

	// parse the space seperated precache string for other items
	s = it->precaches;
	if (!s || !s[0])
		return;

	while (*s)
	{
		start = s;
		while (*s && *s != ' ')
			s++;

		len = s-start;
		if (len >= MAX_QPATH || len < 5)
			gi.error ("PrecacheItem: %s has bad precache string", it->classname);
		memcpy (data, start, len);
		data[len] = 0;
		if (*s)
			s++;

		// determine type based on extension
		if (!strcmp(data+len-3, "md2"))
			gi.modelindex (data);
		else if (!strcmp(data+len-3, "sp2"))
			gi.modelindex (data);
		else if (!strcmp(data+len-3, "wav"))
			gi.soundindex (data);
		if (!strcmp(data+len-3, "pcx"))
			gi.imageindex (data);
	}
}


void ZIGFlagThink(edict_t *ent)
{
	static unsigned short count;
	int i;

	count ++;
	
	if(count > 4)
	{
		edict_t	*tre;
		vec3_t	v,vv;

		i = gi.pointcontents (ent->s.origin);
		if(i & MASK_OPAQUE)
		{
			SelectSpawnPoint (ent, v, vv);
			VectorCopy (v, ent->s.origin);
		}
		for ( i=maxclients->value+1 ; i<globals.num_edicts ; i++)
		{
			tre = &g_edicts[i];
			if(!tre->inuse || tre->deadflag) continue;

			if(tre->classname[0] == 'p' && tre->movetype != MOVETYPE_NOCLIP && tre->client)
			{
				if(tre->client->zc.second_target) continue;
				VectorSubtract(tre->s.origin,ent->s.origin,v);
				if(VectorLength(v) < 350 && Bot_traceS(ent,tre) && v[2] < -JumpMax )
				{
					tre->client->zc.second_target = ent;
				}
			}
		}
		count = 0;
	}
	
	ent->owner = NULL;
	ent->s.frame = 173 + (((ent->s.frame - 173) + 1) % 16);
	ent->nextthink = level.time + FRAMETIME;
}

qboolean ZIGDrop_Flag(edict_t *ent, gitem_t *item)
{
	edict_t *tech;

	if(zflag_ent) return false;

	tech = Drop_Item(ent, item);
	tech->nextthink = level.time + FRAMETIME * 10;//level.time + CTF_TECH_TIMEOUT;
	tech->think = ZIGFlagThink;//TechThink;
	if(ent->client) ent->client->pers.inventory[ITEM_INDEX(item)] = 0;
	/*ent*/tech->s.frame = 173;
	zflag_ent = tech;
	tech->inuse = true;
	return true;
}

qboolean ZIGPickup_Flag (edict_t *ent, edict_t *other)
{
//	gitem_t *item;

//	other->client->pers.selected_item = ITEM_INDEX(item);
	zflag_ent = NULL;
	other->client->pers.inventory[ITEM_INDEX(zflag_item)] = 1;	
//	other->client->resp.score += 1;
	other->s.modelindex3 = gi.modelindex("models/zflag.md2"/*item->world_model*/);
	return true;
}

/*
============
SpawnItem

Sets the clipping size and plants the object on the floor.

Items can't be immediately dropped to floor, because they might
be on an entity that hasn't spawned yet.
============
*/
void SetBotFlag1(edict_t *ent);	//チーム1の旗
void SetBotFlag2(edict_t *ent);  //チーム2の旗

void SpawnItem (edict_t *ent, gitem_t *item)
{
	PrecacheItem (item);

	if (ent->spawnflags)
	{
		if (strcmp(ent->classname, "key_power_cube") != 0)
		{
			ent->spawnflags = 0;
			gi.dprintf("%s at %s has invalid spawnflags set\n", ent->classname, vtos(ent->s.origin));
		}
	}

	// some items will be prevented in deathmatch
	if (deathmatch->value)
	{
		if ( (int)dmflags->value & DF_NO_ARMOR )
		{
			if (item->pickup == Pickup_Armor || item->pickup == Pickup_PowerArmor)
			{
				G_FreeEdict (ent);
				return;
			}
		}
		if ( (int)dmflags->value & DF_NO_ITEMS )
		{
			if (item->pickup == Pickup_Powerup)
			{
				G_FreeEdict (ent);
				return;
			}
		}
		if ( (int)dmflags->value & DF_NO_HEALTH )
		{
			if (item->pickup == Pickup_Health || item->pickup == Pickup_Adrenaline || item->pickup == Pickup_AncientHead)
			{
				G_FreeEdict (ent);
				return;
			}
		}
		if ( (int)dmflags->value & DF_INFINITE_AMMO )
		{
			if ( (item->flags == IT_AMMO) || (strcmp(ent->classname, "weapon_bfg") == 0) )
			{
				G_FreeEdict (ent);
				return;
			}
		}
	}

	if (coop->value && (strcmp(ent->classname, "key_power_cube") == 0))
	{
		ent->spawnflags |= (1 << (8 + level.power_cubes));
		level.power_cubes++;
	}

	// don't let them drop items that stay in a coop game
	if ((coop->value) && (item->flags & IT_STAY_COOP))
	{
		item->drop = NULL;
	}

//ZOID
//Don't spawn the flags unless enabled
	if (!ctf->value &&
		(strcmp(ent->classname, "item_flag_team1") == 0 ||
		strcmp(ent->classname, "item_flag_team2") == 0)) {
		G_FreeEdict(ent);
		return;
	}
//ZOID

	ent->item = item;
	ent->nextthink = level.time + 2 * FRAMETIME;    // items start after other solids
	ent->think = droptofloor;
	ent->s.effects = item->world_model_flags;
	ent->s.renderfx = RF_GLOW;
	if (ent->model)
		gi.modelindex (ent->model);

	VectorCopy(ent->s.origin,ent->monsterinfo.last_sighting);
//ZOID
//flags are server animated and have special handling
	if (strcmp(ent->classname, "item_flag_team1") == 0 ||
		strcmp(ent->classname, "item_flag_team2") == 0) {
		ent->think = CTFFlagSetup;
	}
//ZOID

}

void SpawnItem3 (edict_t *ent, gitem_t *item)
{

	//	PrecacheItem (item);

	ent->item = item;
	ent->nextthink = level.time + 2 * FRAMETIME;    // items start after other solids
	ent->think = droptofloor2;
	ent->s.effects = 0;
	ent->s.renderfx = 0;
	ent->s.modelindex  = 0;
//	if (ent->model)
//		gi.modelindex (0/*ent->model*/);
}

//======================================================================

gitem_t	itemlist[] = 
{
	{
		NULL
	},	// leave index 0 alone

	//
	// NAVI
	//

/*QUAKED r_navi (.3 .3 1) (-16 -16 -16) (16 16 16)	0
*/
	{
		"R_naviF", 
		Pickup_Navi,
		NULL,
		NULL,
		NULL,
		NULL,
		"models/items/armor/body/tris.md2", 0,
		NULL,
/* icon */		NULL,
/* pickup */	"Roam NaviF",
/* width */		2,
		5,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},
	{
		"R_naviX", 
		Pickup_Navi,
		NULL,
		NULL,
		NULL,
		NULL,
		"models/items/armor/body/tris.md2", 0,
		NULL,
/* icon */		NULL,
/* pickup */	"Roam Navi",
/* width */		2,
		10,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},
	{
		"R_navi2", 
		Pickup_Navi,
		NULL,
		NULL,
		NULL,
		NULL,
		"models/items/armor/body/tris.md2", 0,
		NULL,
/* icon */		NULL,
/* pickup */	"Roam Navi2",
/* width */		2,
		30,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},
	{
		"R_navi3", 
		Pickup_Navi,
		NULL,
		NULL,
		NULL,
		NULL,
		"models/items/armor/body/tris.md2", 0,
		NULL,
/* icon */		NULL,
/* pickup */	"Roam Navi3",
/* width */		2,
		20,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},
	//
	// ARMOR
	//

/*QUAKED item_armor_body (.3 .3 1) (-16 -16 -16) (16 16 16)	0
*/
	{
		"item_armor_body", 
		Pickup_Armor,
		NULL,
		NULL,
		NULL,
		"misc/ar1_pkup.wav",
		"models/items/armor/body/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"i_bodyarmor",
/* pickup */	"Body Armor",
/* width */		3,
		0,
		NULL,
		IT_ARMOR,
		&bodyarmor_info,
		ARMOR_BODY,
/* precache */ ""
	},

/*QUAKED item_armor_combat (.3 .3 1) (-16 -16 -16) (16 16 16)	1
*/
	{
		"item_armor_combat", 
		Pickup_Armor,
		NULL,
		NULL,
		NULL,
		"misc/ar1_pkup.wav",
		"models/items/armor/combat/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"i_combatarmor",
/* pickup */	"Combat Armor",
/* width */		3,
		0,
		NULL,
		IT_ARMOR,
		&combatarmor_info,
		ARMOR_COMBAT,
/* precache */ ""
	},

/*QUAKED item_armor_jacket (.3 .3 1) (-16 -16 -16) (16 16 16)	2
*/
	{
		"item_armor_jacket", 
		Pickup_Armor,
		NULL,
		NULL,
		NULL,
		"misc/ar1_pkup.wav",
		"models/items/armor/jacket/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"i_jacketarmor",
/* pickup */	"Jacket Armor",
/* width */		3,
		0,
		NULL,
		IT_ARMOR,
		&jacketarmor_info,
		ARMOR_JACKET,
/* precache */ ""
	},

/*QUAKED item_armor_shard (.3 .3 1) (-16 -16 -16) (16 16 16)	3
*/
	{
		"item_armor_shard", 
		Pickup_Armor,
		NULL,
		NULL,
		NULL,
		"misc/ar2_pkup.wav",
		"models/items/armor/shard/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"i_jacketarmor",
/* pickup */	"Armor Shard",
/* width */		3,
		0,
		NULL,
		IT_ARMOR,
		NULL,
		ARMOR_SHARD,
/* precache */ ""
	},


/*QUAKED item_power_screen (.3 .3 1) (-16 -16 -16) (16 16 16)	4
*/
	{
		"item_power_screen", 
		Pickup_Powerup,
		Use_PowerArmor,
		Drop_PowerArmor,
		NULL,
		"misc/ar3_pkup.wav",
		"models/items/armor/screen/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"i_powerscreen",
/* pickup */	"Power Screen",
/* width */		0,
		60,
		NULL,
		IT_ARMOR,
		NULL,
		0,
/* precache */ ""
	},

/*QUAKED item_power_shield (.3 .3 1) (-16 -16 -16) (16 16 16)	5
*/
	{
		"item_power_shield",
		Pickup_Powerup,
		Use_PowerArmor,
		Drop_PowerArmor,
		NULL,
		"misc/ar3_pkup.wav",
		"models/items/armor/shield/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"i_powershield",
/* pickup */	"Power Shield",
/* width */		0,
		60,
		NULL,
		IT_ARMOR,
		NULL,
		0,
/* precache */ "misc/power2.wav misc/power1.wav"
	},


	//
	// WEAPONS 
	//
/* weapon_grapple (.3 .3 1) (-16 -16 -16) (16 16 16)
always owned, never in the world
*/
	{
		"weapon_grapple", 
		NULL,
		Use_Weapon,
		NULL,
		CTFWeapon_Grapple,
		"misc/w_pkup.wav",
		NULL, 0,
		"models/weapons/grapple/tris.md2",
/* icon */		"w_grapple",
/* pickup */	"Grapple",
		0,
		0,
		NULL,
		IT_WEAPON,
//		0,
		NULL,
		0,
/* precache */ "weapons/grapple/grfire.wav weapons/grapple/grpull.wav weapons/grapple/grhang.wav weapons/grapple/grreset.wav weapons/grapple/grhit.wav"
	},
/* weapon_blaster (.3 .3 1) (-16 -16 -16) (16 16 16)	6
always owned, never in the world
*/
	{
		"weapon_blaster", 
		NULL,
		Use_Weapon,
		NULL,
		Weapon_Blaster,
		"misc/w_pkup.wav",
		NULL, 0,
		"models/weapons/v_blast/tris.md2",
/* icon */		"w_blaster",
/* pickup */	"Blaster",
		0,
		0,
		NULL,
		IT_WEAPON,
//		WEAP_BLASTER,
		NULL,
		0,
/* precache */ "weapons/blastf1a.wav misc/lasfly.wav"
	},

/*QUAKED weapon_shotgun (.3 .3 1) (-16 -16 -16) (16 16 16)	7
*/
	{
		"weapon_shotgun", 
		Pickup_Weapon,
		Use_Weapon,
		Drop_Weapon,
		Weapon_Shotgun,
		"misc/w_pkup.wav",
		"models/weapons/g_shotg/tris.md2", EF_ROTATE,
		"models/weapons/v_shotg/tris.md2",
/* icon */		"w_shotgun",
/* pickup */	"Shotgun",
		0,
		1,
		"Shells",
		IT_WEAPON,
//		WEAP_SHOTGUN,
		NULL,
		0,
/* precache */ "weapons/shotgf1b.wav weapons/shotgr1b.wav"
	},

/*QUAKED weapon_supershotgun (.3 .3 1) (-16 -16 -16) (16 16 16)	8
*/
	{
		"weapon_supershotgun", 
		Pickup_Weapon,
		Use_Weapon,
		Drop_Weapon,
		Weapon_SuperShotgun,
		"misc/w_pkup.wav",
		"models/weapons/g_shotg2/tris.md2", EF_ROTATE,
		"models/weapons/v_shotg2/tris.md2",
/* icon */		"w_sshotgun",
/* pickup */	"Super Shotgun",
		0,
		2,
		"Shells",
		IT_WEAPON,
//		WEAP_SUPERSHOTGUN,
		NULL,
		0,
/* precache */ "weapons/sshotf1b.wav"
	},

/*QUAKED weapon_machinegun (.3 .3 1) (-16 -16 -16) (16 16 16)	9
*/
	{
		"weapon_machinegun", 
		Pickup_Weapon,
		Use_Weapon,
		Drop_Weapon,
		Weapon_Machinegun,
		"misc/w_pkup.wav",
		"models/weapons/g_machn/tris.md2", EF_ROTATE,
		"models/weapons/v_machn/tris.md2",
/* icon */		"w_machinegun",
/* pickup */	"Machinegun",
		0,
		1,
		"Bullets",
		IT_WEAPON,
//		WEAP_MACHINEGUN,
		NULL,
		0,
/* precache */ "weapons/machgf1b.wav weapons/machgf2b.wav weapons/machgf3b.wav weapons/machgf4b.wav weapons/machgf5b.wav"
	},

/*QUAKED weapon_chaingun (.3 .3 1) (-16 -16 -16) (16 16 16)	10
*/
	{
		"weapon_chaingun", 
		Pickup_Weapon,
		Use_Weapon,
		Drop_Weapon,
		Weapon_Chaingun,
		"misc/w_pkup.wav",
		"models/weapons/g_chain/tris.md2", EF_ROTATE,
		"models/weapons/v_chain/tris.md2",
/* icon */		"w_chaingun",
/* pickup */	"Chaingun",
		0,
		1,
		"Bullets",
		IT_WEAPON,
//		WEAP_CHAINGUN,
		NULL,
		0,
/* precache */ "weapons/chngnu1a.wav weapons/chngnl1a.wav weapons/machgf3b.wav` weapons/chngnd1a.wav"
	},

/*QUAKED ammo_trap (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
	{
		"ammo_trap",
		Pickup_Ammo,
		Use_Weapon,
		Drop_Ammo,
		Weapon_Trap,
		"misc/am_pkup.wav",
		"models/weapons/g_trap/tris.md2", EF_ROTATE,
		"models/weapons/v_trap/tris.md2",
/* icon */		"a_trap",
/* pickup */	"Trap",
/* width */		3,
		1,
		"trap",
		IT_AMMO|IT_WEAPON,
//		0,
		NULL,
		AMMO_TRAP,
/* precache */ "weapons/trapcock.wav weapons/traploop.wav weapons/trapsuck.wav weapons/trapdown.wav"
// "weapons/hgrent1a.wav weapons/hgrena1b.wav weapons/hgrenc1b.wav weapons/hgrenb1a.wav weapons/hgrenb2a.wav "
	},

/*QUAKED weapon_grenadelauncher (.3 .3 1) (-16 -16 -16) (16 16 16)	11
*/
	{
		"weapon_grenadelauncher",
		Pickup_Weapon,
		Use_Weapon,
		Drop_Weapon,
		Weapon_GrenadeLauncher,
		"misc/w_pkup.wav",
		"models/weapons/g_launch/tris.md2", EF_ROTATE,
		"models/weapons/v_launch/tris.md2",
/* icon */		"w_glauncher",
/* pickup */	"Grenade Launcher",
		0,
		1,
		"Grenades",
		IT_WEAPON,
		NULL,
		0,
/* precache */ "models/objects/grenade/tris.md2 weapons/grenlf1a.wav weapons/grenlr1b.wav weapons/grenlb1b.wav"
	},

/*QUAKED weapon_rocketlauncher (.3 .3 1) (-16 -16 -16) (16 16 16)	12
*/
	{
		"weapon_rocketlauncher",
		Pickup_Weapon,
		Use_Weapon,
		Drop_Weapon,
		Weapon_RocketLauncher,
		"misc/w_pkup.wav",
		"models/weapons/g_rocket/tris.md2", EF_ROTATE,
		"models/weapons/v_rocket/tris.md2",
/* icon */		"w_rlauncher",
/* pickup */	"Rocket Launcher",
		0,
		1,
		"Rockets",
		IT_WEAPON,
		NULL,
		0,
/* precache */ "models/objects/rocket/tris.md2 weapons/rockfly.wav weapons/rocklf1a.wav weapons/rocklr1b.wav models/objects/debris2/tris.md2"
	},

/*QUAKED weapon_hyperblaster (.3 .3 1) (-16 -16 -16) (16 16 16)	13
*/
	{
		"weapon_hyperblaster", 
		Pickup_Weapon,
		// RAFAEL
		Use_Weapon2,
/*		Use_Weapon,*/
		Drop_Weapon,
		Weapon_HyperBlaster,
		"misc/w_pkup.wav",
		"models/weapons/g_hyperb/tris.md2", EF_ROTATE,
		"models/weapons/v_hyperb/tris.md2",
/* icon */		"w_hyperblaster",
/* pickup */	"HyperBlaster",
		0,
		1,
		"Cells",
		IT_WEAPON,
		NULL,
		0,
/* precache */ "weapons/hyprbu1a.wav weapons/hyprbl1a.wav weapons/hyprbf1a.wav weapons/hyprbd1a.wav misc/lasfly.wav"
	},


/*QUAKED weapon_boomer (.3 .3 1) (-16 -16 -16) (16 16 16)
*/

	{
		"weapon_boomer",
		Pickup_Weapon,
		Use_Weapon,
		Drop_Weapon,
		Weapon_Ionripper,
		"misc/w_pkup.wav",
		"models/weapons/g_boom/tris.md2", EF_ROTATE,
		"models/weapons/v_boomer/tris.md2",
/* icon */	"w_ripper",
/* pickup */ "Ionripper",
		0,
		2,
		"Cells",
		IT_WEAPON,
//		WEAP_BOOMER,
		NULL,
		0,
/* precache */ "weapons/rg_hum.wav weapons/rippfire.wav"
	},
// END 14-APR-98

/*QUAKED weapon_railgun (.3 .3 1) (-16 -16 -16) (16 16 16)	14
*/
	{
		"weapon_railgun", 
		Pickup_Weapon,
		// RAFAEL
		Use_Weapon2,
/*		Use_Weapon,*/
		Drop_Weapon,
		Weapon_Railgun,
		"misc/w_pkup.wav",
		"models/weapons/g_rail/tris.md2", EF_ROTATE,
		"models/weapons/v_rail/tris.md2",
/* icon */		"w_railgun",
/* pickup */	"Railgun",
		0,
		1,
		"Slugs",
		IT_WEAPON,
		NULL,
		0,
/* precache */ "weapons/rg_hum.wav"
	},

// RAFAEL 14-APR-98
/*QUAKED weapon_phalanx (.3 .3 1) (-16 -16 -16) (16 16 16)
*/

	{
		"weapon_phalanx",
		Pickup_Weapon,
		Use_Weapon,
		Drop_Weapon,
		Weapon_Phalanx,
		"misc/w_pkup.wav",
		"models/weapons/g_shotx/tris.md2", EF_ROTATE,
		"models/weapons/v_shotx/tris.md2",
/* icon */	"w_phallanx",
/* pickup */ "Phalanx",
		0,
		1,
		"Mag Slug",
		IT_WEAPON,
//		WEAP_PHALANX,
		NULL,
		0,
/* precache */ "weapons/plasshot.wav"
	},

/*QUAKED weapon_bfg (.3 .3 1) (-16 -16 -16) (16 16 16)	15
*/
	{
		"weapon_bfg",
		Pickup_Weapon,
		Use_Weapon,
		Drop_Weapon,
		Weapon_BFG,
		"misc/w_pkup.wav",
		"models/weapons/g_bfg/tris.md2", EF_ROTATE,
		"models/weapons/v_bfg/tris.md2",
/* icon */		"w_bfg",
/* pickup */	"BFG10K",
		0,
		50,
		"Cells",
		IT_WEAPON,

		NULL,
		0,
/* precache */ "sprites/s_bfg1.sp2 sprites/s_bfg2.sp2 sprites/s_bfg3.sp2 weapons/bfg__f1y.wav weapons/bfg__l1a.wav weapons/bfg__x1b.wav weapons/bfg_hum.wav"
	},

#if 0
//ZOID
/*QUAKED weapon_laser (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
	{
		"weapon_laser",
		Pickup_Weapon,
		Use_Weapon,
		Drop_Weapon,
		Weapon_Laser,
		"misc/w_pkup.wav",
		"models/weapons/g_laser/tris.md2", EF_ROTATE,
		"models/weapons/v_laser/tris.md2",
/* icon */		"w_bfg",
/* pickup */	"Flashlight Laser",
		0,
		1,
		"Cells",
		IT_WEAPON,
		NULL,
		0,
/* precache */ ""
	},
#endif

	//
	// AMMO ITEMS
	//

/*QUAKED ammo_shells (.3 .3 1) (-16 -16 -16) (16 16 16)	16
*/
	{
		"ammo_shells",
		Pickup_Ammo,
		NULL,
		Drop_Ammo,
		NULL,
		"misc/am_pkup.wav",
		"models/items/ammo/shells/medium/tris.md2", 0,
		NULL,
/* icon */		"a_shells",
/* pickup */	"Shells",
/* width */		3,
		10,
		NULL,
		IT_AMMO,
		NULL,
		AMMO_SHELLS,
/* precache */ ""
	},

/*QUAKED ammo_bullets (.3 .3 1) (-16 -16 -16) (16 16 16)	17
*/
	{
		"ammo_bullets",
		Pickup_Ammo,
		NULL,
		Drop_Ammo,
		NULL,
		"misc/am_pkup.wav",
		"models/items/ammo/bullets/medium/tris.md2", 0,
		NULL,
/* icon */		"a_bullets",
/* pickup */	"Bullets",
/* width */		3,
		50,
		NULL,
		IT_AMMO,
		NULL,
		AMMO_BULLETS,
/* precache */ ""
	},

/*QUAKED ammo_cells (.3 .3 1) (-16 -16 -16) (16 16 16)	18
*/
	{
		"ammo_cells",
		Pickup_Ammo,
		NULL,
		Drop_Ammo,
		NULL,
		"misc/am_pkup.wav",
		"models/items/ammo/cells/medium/tris.md2", 0,
		NULL,
/* icon */		"a_cells",
/* pickup */	"Cells",
/* width */		3,
		50,
		NULL,
		IT_AMMO,
		NULL,
		AMMO_CELLS,
/* precache */ ""
	},

/*QUAKED ammo_grenades (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
	{
		"ammo_grenades",
		Pickup_Ammo,
		Use_Weapon,
		Drop_Ammo,
		Weapon_Grenade,
		"misc/am_pkup.wav",
		"models/items/ammo/grenades/medium/tris.md2", 0,
		"models/weapons/v_handgr/tris.md2",
/* icon */		"a_grenades",
/* pickup */	"Grenades",
/* width */		3,
		5,
		"grenades",
		IT_AMMO|IT_WEAPON,
		NULL,
		AMMO_GRENADES,
/* precache */ "weapons/hgrent1a.wav weapons/hgrena1b.wav weapons/hgrenc1b.wav weapons/hgrenb1a.wav weapons/hgrenb2a.wav "
	},

/*QUAKED ammo_rockets (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
	{
		"ammo_rockets",
		Pickup_Ammo,
		NULL,
		Drop_Ammo,
		NULL,
		"misc/am_pkup.wav",
		"models/items/ammo/rockets/medium/tris.md2", 0,
		NULL,
/* icon */		"a_rockets",
/* pickup */	"Rockets",
/* width */		3,
		5,
		NULL,
		IT_AMMO,
		NULL,
		AMMO_ROCKETS,
/* precache */ ""
	},

/*QUAKED ammo_slugs (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
	{
		"ammo_slugs",
		Pickup_Ammo,
		NULL,
		Drop_Ammo,
		NULL,
		"misc/am_pkup.wav",
		"models/items/ammo/slugs/medium/tris.md2", 0,
		NULL,
/* icon */		"a_slugs",
/* pickup */	"Slugs",
/* width */		3,
		10,
		NULL,
		IT_AMMO,
		NULL,
		AMMO_SLUGS,
/* precache */ ""
	},

/*QUAKED ammo_magslug (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
	{
		"ammo_magslug",
		Pickup_Ammo,
		NULL,
		Drop_Ammo,
		NULL,
		"misc/am_pkup.wav",
		"models/objects/ammo/tris.md2", 0,
		NULL,
/* icon */		"a_mslugs",
/* pickup */	"Mag Slug",
/* width */		3,
		10,
		NULL,
		IT_AMMO,
//		0,
		NULL,
		AMMO_MAGSLUG,
/* precache */ ""
	},

	//
	// POWERUP ITEMS
	//
/*QUAKED item_quad (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
	{
		"item_quad", 
		Pickup_Powerup,
		Use_Quad,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/quaddama/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"p_quad",
/* pickup */	"Quad Damage",
/* width */		2,
		60,
		NULL,
		0,
		NULL,
		0,
/* precache */ "items/damage.wav items/damage2.wav items/damage3.wav"
	},

/*QUAKED item_quadfire (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
	{
		"item_quadfire", 
		Pickup_Powerup,
		Use_QuadFire,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/quadfire/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"p_quadfire",

/* pickup */	"DualFire Damage",
/* width */		2,
		60,
		NULL,
//		IT_POWERUP,
		0,
		NULL,
		0,
/* precache */ "items/quadfire1.wav items/quadfire2.wav items/quadfire3.wav"
	},

/*QUAKED item_invulnerability (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
	{
		"item_invulnerability",
		Pickup_Powerup,
		Use_Invulnerability,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/invulner/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"p_invulnerability",
/* pickup */	"Invulnerability",
/* width */		2,
		300,
		NULL,
		0,
		NULL,
		0,
/* precache */ "items/protect.wav items/protect2.wav items/protect3.wav"
	},

/*QUAKED item_silencer (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
	{
		"item_silencer",
		Pickup_Powerup,
		Use_Silencer,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/silencer/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"p_silencer",
/* pickup */	"Silencer",
/* width */		2,
		60,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},

/*QUAKED item_breather (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
	{
		"item_breather",
		Pickup_Powerup,
		Use_Breather,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/breather/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"p_rebreather",
/* pickup */	"Rebreather",
/* width */		2,
		60,
		NULL,
		0,
		NULL,
		0,
/* precache */ "items/airout.wav"
	},

/*QUAKED item_enviro (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
	{
		"item_enviro",
		Pickup_Powerup,
		Use_Envirosuit,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/enviro/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"p_envirosuit",
/* pickup */	"Environment Suit",
/* width */		2,
		60,
		NULL,
		0,
		NULL,
		0,
/* precache */ "items/airout.wav"
	},

/*QUAKED item_ancient_head (.3 .3 1) (-16 -16 -16) (16 16 16)
Special item that gives +2 to maximum health
*/
	{
		"item_ancient_head",
		Pickup_AncientHead,
		NULL,
		NULL,
		NULL,
		"items/pkup.wav",
		"models/items/c_head/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"i_fixme",
/* pickup */	"Ancient Head",
/* width */		2,
		60,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},

/*QUAKED item_adrenaline (.3 .3 1) (-16 -16 -16) (16 16 16)
gives +1 to maximum health
*/
	{
		"item_adrenaline",
		Pickup_Adrenaline,
		NULL,
		NULL,
		NULL,
		"items/pkup.wav",
		"models/items/adrenal/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"p_adrenaline",
/* pickup */	"Adrenaline",
/* width */		2,
		60,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},

/*QUAKED item_bandolier (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
	{
		"item_bandolier",
		Pickup_Bandolier,
		NULL,
		NULL,
		NULL,
		"items/pkup.wav",
		"models/items/band/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"p_bandolier",
/* pickup */	"Bandolier",
/* width */		2,
		60,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},

/*QUAKED item_pack (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
	{
		"item_pack",
		Pickup_Pack,
		NULL,
		NULL,
		NULL,
		"items/pkup.wav",
		"models/items/pack/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"i_pack",
/* pickup */	"Ammo Pack",
/* width */		2,
		180,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},
#if 0
	//
	// KEYS
	//
/*QUAKED key_data_cd (0 .5 .8) (-16 -16 -16) (16 16 16)
key for computer centers
*/
	{
		"key_data_cd",
		Pickup_Key,
		NULL,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/keys/data_cd/tris.md2", EF_ROTATE,
		NULL,
		"k_datacd",
		"Data CD",
		2,
		0,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},

/*QUAKED key_power_cube (0 .5 .8) (-16 -16 -16) (16 16 16) TRIGGER_SPAWN NO_TOUCH
warehouse circuits
*/
	{
		"key_power_cube",
		Pickup_Key,
		NULL,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/keys/power/tris.md2", EF_ROTATE,
		NULL,
		"k_powercube",
		"Power Cube",
		2,
		0,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},

/*QUAKED key_pyramid (0 .5 .8) (-16 -16 -16) (16 16 16)
key for the entrance of jail3
*/
	{
		"key_pyramid",
		Pickup_Key,
		NULL,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/keys/pyramid/tris.md2", EF_ROTATE,
		NULL,
		"k_pyramid",
		"Pyramid Key",
		2,
		0,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},

/*QUAKED key_data_spinner (0 .5 .8) (-16 -16 -16) (16 16 16)
key for the city computer
*/
	{
		"key_data_spinner",
		Pickup_Key,
		NULL,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/keys/spinner/tris.md2", EF_ROTATE,
		NULL,
		"k_dataspin",
		"Data Spinner",
		2,
		0,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},

/*QUAKED key_pass (0 .5 .8) (-16 -16 -16) (16 16 16)
security pass for the security level
*/
	{
		"key_pass",
		Pickup_Key,
		NULL,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/keys/pass/tris.md2", EF_ROTATE,
		NULL,
		"k_security",
		"Security Pass",
		2,
		0,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},

/*QUAKED key_blue_key (0 .5 .8) (-16 -16 -16) (16 16 16)
normal door key - blue
*/
	{
		"key_blue_key",
		Pickup_Key,
		NULL,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/keys/key/tris.md2", EF_ROTATE,
		NULL,
		"k_bluekey",
		"Blue Key",
		2,
		0,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},

/*QUAKED key_red_key (0 .5 .8) (-16 -16 -16) (16 16 16)
normal door key - red
*/
	{
		"key_red_key",
		Pickup_Key,
		NULL,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/keys/red_key/tris.md2", EF_ROTATE,
		NULL,
		"k_redkey",
		"Red Key",
		2,
		0,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},

// RAFAEL
/*QUAKED key_green_key (0 .5 .8) (-16 -16 -16) (16 16 16)
normal door key - blue
*/
	{
		"key_green_key",
		Pickup_Key,
		NULL,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/keys/green_key/tris.md2", EF_ROTATE,
		NULL,
		"k_green",
		"Green Key",
		2,
		0,
		NULL,
		IT_STAY_COOP|IT_KEY,
//		0,
		NULL,
		0,
/* precache */ ""
	},
/*QUAKED key_commander_head (0 .5 .8) (-16 -16 -16) (16 16 16)
tank commander's head
*/
	{
		"key_commander_head",
		Pickup_Key,
		NULL,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/monsters/commandr/head/tris.md2", EF_GIB,
		NULL,
/* icon */		"k_comhead",
/* pickup */	"Commander's Head",
/* width */		2,
		0,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},

/*QUAKED key_airstrike_target (0 .5 .8) (-16 -16 -16) (16 16 16)
tank commander's head
*/
	{
		"key_airstrike_target",
		Pickup_Key,
		NULL,
		Drop_General,
		NULL,
		"items/pkup.wav",
		"models/items/keys/target/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"i_airstrike",
/* pickup */	"Airstrike Marker",
/* width */		2,
		0,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},
#endif
	{
		NULL,
		Pickup_Health,
		NULL,
		NULL,
		NULL,
		"items/pkup.wav",
		NULL, 0,
		NULL,
/* icon */		"i_health",
/* pickup */	"Health",
/* width */		3,
		0,
		NULL,
		0,
		NULL,
		0,
/* precache */ ""
	},

//ZOID
/*QUAKED item_flag_team1 (1 0.2 0) (-16 -16 -24) (16 16 32)
*/
	{
		"item_flag_team1",
		CTFPickup_Flag,
		NULL,
		CTFDrop_Flag, //Should this be null if we don't want players to drop it manually?
		NULL,
		"ctf/flagtk.wav",
		"players/male/flag1.md2", EF_FLAG1,
		NULL,
/* icon */		"i_ctf1",
/* pickup */	"Red Flag",
/* width */		2,
		0,
		NULL,
		0,
		NULL,
		0,
/* precache */ "ctf/flagcap.wav"
	},

/*QUAKED item_flag_team2 (1 0.2 0) (-16 -16 -24) (16 16 32)
*/
	{
		"item_flag_team2",
		CTFPickup_Flag,
		NULL,
		CTFDrop_Flag, //Should this be null if we don't want players to drop it manually?
		NULL,
		"ctf/flagtk.wav",
		"players/male/flag2.md2", EF_FLAG2,
		NULL,
/* icon */		"i_ctf2",
/* pickup */	"Blue Flag",
/* width */		2,
		0,
		NULL,
		0,
		NULL,
		0,
/* precache */ "ctf/flagcap.wav"
	},

/*QUAKED item_flag_zig (1 0.2 0) (-16 -16 -24) (16 16 32)
*/
	{
		"item_flag_zig",
		ZIGPickup_Flag,
		NULL,
		ZIGDrop_Flag, //Should this be null if we don't want players to drop it manually?
		NULL,
		"3zb/emgcall.wav",
		"models/zflag.md2", EF_FLAG2,
		NULL,
/* icon */		"i_zig",
/* pickup */	"Zig Flag",
/* width */		2,
		0,
		NULL,
		0,
		NULL,
		0,
/* precache */ "ctf/flagcap.wav"
	},
/* Resistance Tech */
	{
		"item_tech1",
		CTFPickup_Tech,
		NULL,
		CTFDrop_Tech, //Should this be null if we don't want players to drop it manually?
		NULL,
		"items/pkup.wav",
		"models/ctf/resistance/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"tech1",
/* pickup */	"Disruptor Shield",
/* width */		2,
		0,
		NULL,
		IT_TECH,
		NULL,
		0,
/* precache */ "ctf/tech1.wav"
	},

/* Strength Tech */
	{
		"item_tech2",
		CTFPickup_Tech,
		NULL,
		CTFDrop_Tech, //Should this be null if we don't want players to drop it manually?
		NULL,
		"items/pkup.wav",
		"models/ctf/strength/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"tech2",
/* pickup */	"Power Amplifier",
/* width */		2,
		0,
		NULL,
		IT_TECH,
		NULL,
		0,
/* precache */ "ctf/tech2.wav ctf/tech2x.wav"
	},

/* Haste Tech */
	{
		"item_tech3",
		CTFPickup_Tech,
		NULL,
		CTFDrop_Tech, //Should this be null if we don't want players to drop it manually?
		NULL,
		"items/pkup.wav",
		"models/ctf/haste/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"tech3",
/* pickup */	"Time Accel",
/* width */		2,
		0,
		NULL,
		IT_TECH,
		NULL,
		0,
/* precache */ "ctf/tech3.wav"
	},

/* Regeneration Tech */
	{
		"item_tech4",
		CTFPickup_Tech,
		NULL,
		CTFDrop_Tech, //Should this be null if we don't want players to drop it manually?
		NULL,
		"items/pkup.wav",
		"models/ctf/regeneration/tris.md2", EF_ROTATE,
		NULL,
/* icon */		"tech4",
/* pickup */	"AutoDoc",
/* width */		2,
		0,
		NULL,
		IT_TECH,
		NULL,
		0,
/* precache */ "ctf/tech4.wav"
	},

//ZOID

	// end of list marker
	{NULL}
};


/*QUAKED item_health (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
void SP_item_health (edict_t *self)
{
	if ( deathmatch->value && ((int)dmflags->value & DF_NO_HEALTH) )
	{
		G_FreeEdict (self);
		return;
	}

	self->model = "models/items/healing/medium/tris.md2";
	self->count = 10;
	SpawnItem (self, FindItem ("Health"));
	gi.soundindex ("items/n_health.wav");
}

/*QUAKED item_health_small (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
void SP_item_health_small (edict_t *self)
{
	if ( deathmatch->value && ((int)dmflags->value & DF_NO_HEALTH) )
	{
		G_FreeEdict (self);
		return;
	}

	self->model = "models/items/healing/stimpack/tris.md2";
	self->count = 2;
	SpawnItem (self, FindItem ("Health"));
	self->style = HEALTH_IGNORE_MAX;
	gi.soundindex ("items/s_health.wav");
}

/*QUAKED item_health_large (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
void SP_item_health_large (edict_t *self)
{
	if ( deathmatch->value && ((int)dmflags->value & DF_NO_HEALTH) )
	{
		G_FreeEdict (self);
		return;
	}

	self->model = "models/items/healing/large/tris.md2";
	self->count = 25;
	SpawnItem (self, FindItem ("Health"));
	gi.soundindex ("items/l_health.wav");
}

/*QUAKED item_health_mega (.3 .3 1) (-16 -16 -16) (16 16 16)
*/
void SP_item_health_mega (edict_t *self)
{
	if ( deathmatch->value && ((int)dmflags->value & DF_NO_HEALTH) )
	{
		G_FreeEdict (self);
		return;
	}

	self->model = "models/items/mega_h/tris.md2";
	self->count = 100;
	SpawnItem (self, FindItem ("Health"));
	gi.soundindex ("items/m_health.wav");
	self->style = HEALTH_IGNORE_MAX|HEALTH_TIMED;
}

// RAFAEL
void SP_item_foodcube (edict_t *self)
{
	if ( deathmatch->value && ((int)dmflags->value & DF_NO_HEALTH) )
	{
		G_FreeEdict (self);
		return;
	}

	self->model = "models/objects/trapfx/tris.md2";
	SpawnItem (self, FindItem ("Health"));
	self->spawnflags |= DROPPED_ITEM;
	self->style = HEALTH_IGNORE_MAX;
	gi.soundindex ("items/s_health.wav");
	self->classname = "foodcube";
}


void InitItems (void)
{
	game.num_items = sizeof(itemlist)/sizeof(itemlist[0]) - 1;
}



/*
===============
SetItemNames

Called by worldspawn
===============
*/
void SetItemNames (void)
{
	int		i;
	gitem_t	*it;

	for (i=0 ; i<game.num_items ; i++)
	{
		it = &itemlist[i];
		gi.configstring (CS_ITEMS+i, it->pickup_name);
	}

	jacket_armor_index = ITEM_INDEX(FindItem("Jacket Armor"));
	combat_armor_index = ITEM_INDEX(FindItem("Combat Armor"));
	body_armor_index   = ITEM_INDEX(FindItem("Body Armor"));
	power_screen_index = ITEM_INDEX(FindItem("Power Screen"));
	power_shield_index = ITEM_INDEX(FindItem("Power Shield"));
}
