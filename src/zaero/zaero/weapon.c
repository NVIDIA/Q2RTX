#include "../header/local.h"
#include "../monster/misc/player.h"

extern qboolean is_quad;
extern byte is_silenced;

void playQuadSound(edict_t *ent);
void Weapon_Generic (edict_t *ent, 
					 int FRAME_ACTIVATE_LAST, 
					 int FRAME_FIRE_LAST, 
					 int FRAME_IDLE_LAST, 
					 int FRAME_DEACTIVATE_LAST, 
					 int *pause_frames, 
					 int *fire_frames, 
					 void (*fire)(edict_t *ent));
void NoAmmoWeaponChange (edict_t *ent);
void check_dodge (edict_t *self, vec3_t start, vec3_t dir, int speed);

void Grenade_Explode(edict_t *ent);
void P_ProjectSource (edict_t *ent, vec3_t distance, vec3_t forward, vec3_t right, vec3_t result);

void fire_sconnan (edict_t *self);
void fire_sconnanEffects (edict_t *self);

const int SC_MAXFIRETIME    = 5;         // in seconds...
const int SC_BASEDAMAGE     = 10;        // minimum damage
const int SC_DAMGERANGE     = 990;       // maximum damaged range (max damage possible is SC_BASEDAMAGE + SC_DAMGERANGE)
const int SC_MAXRADIUS      = 500;       // maximum blast radius
const int SC_MAXCELLS       = 100;       // maximum number of cells

vec_t VectorLengthSquared(vec3_t v)
{
	int		i;
	float	length;
	
	length = 0;
	for (i=0 ; i< 3 ; i++)
		length += v[i]*v[i];

	return length;
}

void angleToward(edict_t *self, vec3_t point, float speed)
{
	vec3_t forward;
	float yaw = 0.0;
	float vel = 0.0;
	vec3_t delta;
	vec3_t destAngles;

	if (!self)
	{
		return;
	}

	VectorSubtract(point, self->s.origin, delta);
	vectoangles(delta, destAngles);
	self->ideal_yaw = destAngles[YAW];
	self->yaw_speed = speed;
	M_ChangeYaw(self);
	yaw = self->s.angles[YAW];
	self->ideal_yaw = destAngles[PITCH];
	self->s.angles[YAW] = self->s.angles[PITCH];
	M_ChangeYaw(self);
	self->s.angles[PITCH] = self->s.angles[YAW];
	self->s.angles[YAW] = yaw;
	AngleVectors (self->s.angles, forward, NULL, NULL);
	vel = VectorLength(self->velocity);
	VectorScale(forward, vel, self->velocity);
}

#define MAXROTATION 20

/*
	Laser Trip Bombs
*/
// spawnflags
#define CHECK_BACK_WALL 1

// variables
#define TBOMB_DELAY	1.0
#define TBOMB_TIMEOUT	180
#define TBOMB_DAMAGE 150
#define TBOMB_RADIUS_DAMAGE 384
#define TBOMB_HEALTH 100
#define TBOMB_SHRAPNEL	5
#define TBOMB_SHRAPNEL_DMG	15
#define TBOMB_MAX_EXIST	25

void shrapnel_touch (edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (!ent || !other)
	{
		return;
	}

	// do damage if we can
	if (!other->takedamage)
		return;

	if (VectorCompare(ent->velocity, vec3_origin))
		return;

	T_Damage (other, ent, ent->owner, ent->velocity, ent->s.origin, 
		plane->normal, TBOMB_SHRAPNEL_DMG, 8, 0, MOD_TRIPBOMB);
	G_FreeEdict(ent);
}

void TripBomb_Explode (edict_t *ent)
{
	vec3_t origin;
	int i = 0;

	if (!ent)
	{
		return;
	}

	T_RadiusDamage(ent, ent->owner ? ent->owner : ent, ent->dmg, ent, ent->dmg_radius, MOD_TRIPBOMB);

	VectorMA (ent->s.origin, -0.02, ent->velocity, origin);

	gi.WriteByte (svc_temp_entity);
	if (ent->waterlevel)
	{
		if (ent->groundentity)
			gi.WriteByte (TE_GRENADE_EXPLOSION_WATER);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	}
	else
	{
		if (ent->groundentity)
			gi.WriteByte (TE_GRENADE_EXPLOSION);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION);
	}
	gi.WritePosition (origin);
	gi.multicast (ent->s.origin, MULTICAST_PHS);

	// throw off some debris
	for (i = 0; i < TBOMB_SHRAPNEL; i++)
	{
		edict_t *sh = G_Spawn();
		vec3_t forward, right, up;
		sh->classname = "shrapnel";
		sh->movetype = MOVETYPE_BOUNCE;
		sh->solid = SOLID_BBOX;
		sh->s.effects |= EF_GRENADE;
		sh->s.modelindex = gi.modelindex("models/objects/shrapnel/tris.md2");
		sh->owner = ent->owner;
		VectorSet (sh->avelocity, 300, 300, 300);
		VectorCopy(ent->s.origin, sh->s.origin);
		AngleVectors (ent->s.angles, forward, right, up);
		VectorScale(forward, 500, forward);
		VectorMA(forward, crandom()*500, right, forward);
		VectorMA(forward, crandom()*500, up, forward);
		VectorCopy(forward, sh->velocity);
		sh->touch = shrapnel_touch;
		sh->think = G_FreeEdict;
		sh->nextthink = level.time + 3.0 + crandom() * 1.5;
	}

	G_FreeEdict(ent);
}

void tripbomb_laser_think (edict_t *self)
{
	vec3_t start;
	vec3_t end;
	vec3_t delta;
	trace_t	tr;
	int		count = 8;

	if (!self)
	{
		return;
	}

	self->nextthink = level.time + FRAMETIME;

	if (level.time > self->timeout)
	{
		// blow up
		self->chain->think = TripBomb_Explode;
		self->chain->nextthink = level.time + FRAMETIME;
		G_FreeEdict(self);
		return;
	}

	// randomly phase out or EMPNuke is in effect
	if (EMPNukeCheck(self, self->s.origin) || random() < 0.1)
	{
		self->svflags |= SVF_NOCLIENT;
		return;
	}

	self->svflags &= ~SVF_NOCLIENT;
	VectorCopy (self->s.origin, start);
	VectorMA (start, 2048, self->movedir, end);
	tr = gi.trace (start, NULL, NULL, end, self, MASK_SHOT);

	if (!tr.ent)
		return;

	VectorSubtract(tr.endpos, self->move_origin, delta);
	if (VectorCompare(self->s.origin, self->move_origin))
	{
		// we haven't done anything yet
		VectorCopy(tr.endpos, self->move_origin);
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
	else if (VectorLength(delta) > 1.0)
	{
		// blow up
		self->chain->think = TripBomb_Explode;
		self->chain->nextthink = level.time + FRAMETIME;
		G_FreeEdict(self);
		return;
	}
	VectorCopy(self->move_origin, self->s.old_origin);
}

void tripbomb_laser_on (edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->svflags &= ~SVF_NOCLIENT;
	self->think = tripbomb_laser_think;

	// play a sound
	gi.sound(self, CHAN_VOICE, gi.soundindex("weapons/ired/las_arm.wav"), 1, ATTN_NORM, 0);
	tripbomb_laser_think(self);
}

void create_tripbomb_laser(edict_t *bomb)
{
	if (!bomb)
	{
		return;
	}

	// create the laser
	edict_t *laser = G_Spawn();
	bomb->chain = laser;
	laser->classname = "laser trip bomb laser";
	VectorCopy(bomb->s.origin, laser->s.origin);
	VectorCopy(bomb->s.origin, laser->move_origin);
	VectorCopy(bomb->s.angles, laser->s.angles);
	G_SetMovedir (laser->s.angles, laser->movedir);
	laser->owner = bomb;
	laser->s.skinnum = 0xb0b1b2b3; // <- faint purple  0xf3f3f1f1 <-blue  red-> 0xf2f2f0f0;
	laser->s.frame = 2;
	laser->movetype = MOVETYPE_NONE;
	laser->solid = SOLID_NOT;
	laser->s.renderfx |= RF_BEAM|RF_TRANSLUCENT;
	laser->s.modelindex = 1;
	laser->chain = bomb;
	laser->spawnflags |= 0x80000001;
	laser->think = tripbomb_laser_on;
	laser->nextthink = level.time + FRAMETIME;
	laser->svflags |= SVF_NOCLIENT;
	laser->timeout = level.time + TBOMB_TIMEOUT;
	gi.linkentity (laser);
}

void use_tripbomb(edict_t *self, edict_t *other, edict_t *activator)
{
	if (!self)
	{
		return;
	}

	if (self->chain)
	{
		// we already have a laser, remove it
		G_FreeEdict(self->chain);
		self->chain = NULL;
	}
	else
		// create the laser
		create_tripbomb_laser(self);
}

void turnOffGlow(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->s.effects &= ~EF_COLOR_SHELL;
	self->s.renderfx &= ~RF_SHELL_GREEN;
	self->think = NULL;
	self->nextthink = 0;
}

void tripbomb_pain(edict_t *self, edict_t *other, float kick, int damage)
{
	if (!self)
	{
		return;
	}

	// turn on the glow
	self->damage_debounce_time = level.time + 0.2;

	// if we don't have a think function, then turn this thing on
	if (self->think == NULL)
	{
		self->s.effects |= EF_COLOR_SHELL;
		self->s.renderfx |= RF_SHELL_GREEN;
		self->nextthink = self->damage_debounce_time;
		self->think = turnOffGlow;
	}
}

void tripbomb_think(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->chain == NULL)
	{
		// check whether we need to create the laser
		if (self->timeout < level.time)
		{
			create_tripbomb_laser(self);
		}
	}

	// do we need to show damage?
	if (self->damage_debounce_time > level.time)
	{
		self->s.effects |= EF_COLOR_SHELL;
		self->s.renderfx |= RF_SHELL_GREEN;
	}
	else
	{
		self->s.effects &= ~EF_COLOR_SHELL;
		self->s.renderfx &= ~RF_SHELL_GREEN;
	}

	self->nextthink = level.time + FRAMETIME;
}

void setupBomb(edict_t *bomb, char *classname, float damage, float damage_radius)
{
	if (!bomb)
	{
		return;
	}

	bomb->classname = classname;
	VectorSet(bomb->mins, -8, -8, -8);
	VectorSet(bomb->maxs, 8, 8, 8);
	bomb->solid = SOLID_BBOX;
	bomb->movetype = MOVETYPE_NONE;
	bomb->s.modelindex = gi.modelindex("models/objects/ired/tris.md2");
	bomb->radius_dmg = damage;
	bomb->dmg = damage;
	bomb->dmg_radius = damage_radius;
	bomb->health = 1;
	bomb->takedamage = DAMAGE_IMMORTAL; // health will not be deducted
	bomb->pain = tripbomb_pain;
}

void removeOldest()
{
	edict_t *oldestEnt = NULL;
	edict_t *e = NULL;
	int count = 0;

	while(1)
	{
		e = G_Find(e, FOFS(classname), "ired");
		if (e == NULL) // no more
			break;

		count++;

		if (oldestEnt == NULL ||
			e->timestamp < oldestEnt->timestamp)
		{
			oldestEnt = e;
		}
	}

	// do we have too many?
	if (count > TBOMB_MAX_EXIST && oldestEnt != NULL)
	{
		// get this tbomb to explode
		oldestEnt->think = TripBomb_Explode;
		oldestEnt->nextthink = level.time + FRAMETIME;
		G_FreeEdict(oldestEnt->chain);
	}
}

qboolean fire_lasertripbomb(edict_t *self, vec3_t start, vec3_t dir, float timer, float damage, float damage_radius, qboolean quad)
{
	// trace a line
	trace_t tr;
	vec3_t endPos;
	vec3_t _dir;
	edict_t *bomb = NULL;

	if (!self)
	{
		return false;
	}

	VectorScale(dir, 64, _dir);
	VectorAdd(start, _dir, endPos);

	// trace ahead, looking for a wall
	tr = gi.trace(start, NULL, NULL, endPos, self, MASK_SHOT);
	if (tr.fraction == 1.0)
	{
		// not close enough
		return false;
	}

	if (Q_stricmp(tr.ent->classname, "worldspawn") != 0)
	{
		return false;
	}

	// create the bomb
	bomb = G_Spawn();
	VectorMA(tr.endpos, 3, tr.plane.normal, bomb->s.origin);
	vectoangles(tr.plane.normal, bomb->s.angles);
	bomb->owner = self;
	setupBomb(bomb, "ired", damage, damage_radius);
	gi.linkentity(bomb);

	bomb->timestamp = level.time;
	bomb->timeout = level.time + timer;
	bomb->nextthink = level.time + FRAMETIME;
	bomb->think = tripbomb_think;

	// remove the oldest trip bomb
	removeOldest();

	// play a sound
	gi.sound(self, CHAN_VOICE, gi.soundindex("weapons/ired/las_set.wav"), 1, ATTN_NORM, 0);
	return true;
}

void weapon_lasertripbomb_fire (edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (ent->client->ps.gunframe == 10)
	{
		vec3_t	offset;
		vec3_t	forward;
		vec3_t	start;
		int damage = TBOMB_DAMAGE;
		float radius = TBOMB_RADIUS_DAMAGE;
		if (is_quad)
			damage *= 4;

		// place the trip bomb
		VectorSet(offset, 0, 0, ent->viewheight * 0.75);
		AngleVectors (ent->client->v_angle, forward, NULL, NULL);
		VectorAdd(ent->s.origin, offset, start);

		if (fire_lasertripbomb(ent, start, forward, TBOMB_DELAY, damage, radius, is_quad))
		{
			ent->client->pers.inventory[ent->client->ammo_index] -= 1;
			
			// switch models
			ent->client->ps.gunindex = gi.modelindex("models/weapons/v_ired/hand.md2");

			// play quad sound
			playQuadSound(ent);
		}
	}
	else if (ent->client->ps.gunframe == 15)
	{
		// switch models back
		int mi = gi.modelindex("models/weapons/v_ired/tris.md2");
		if (ent->client->ps.gunindex != mi)
		{
			ent->client->ps.gunindex = mi;
			// go back to get another trip bomb
			ent->client->ps.gunframe = 0;
			return;
		}
	}
	else if (ent->client->ps.gunframe == 6)
	{
		ent->client->ps.gunframe = 16;
		return;
	}

	ent->client->ps.gunframe++;
}

void Weapon_LaserTripBomb(edict_t *ent)
{
	static int	pause_frames[]	= {24, 33, 43, 0};
	static int	fire_frames[]	= {6, 10, 15, 0};

	Weapon_Generic(ent, 6, 15, 43, 48, pause_frames,
			fire_frames, weapon_lasertripbomb_fire);
}

void SP_misc_lasertripbomb(edict_t *bomb)
{
	if (!bomb)
	{
		return;
	}

	// precache
	gi.soundindex("weapons/ired/las_set.wav");
	gi.soundindex("weapons/ired/las_arm.wav");
	gi.modelindex("models/objects/shrapnel/tris.md2");
	gi.modelindex("models/objects/ired/tris.md2");

	if (bomb->spawnflags & CHECK_BACK_WALL)
	{
		vec3_t forward, endPos;
		trace_t tr;
		// look backwards toward a wall
		AngleVectors (bomb->s.angles, forward, NULL, NULL);
		VectorMA(bomb->s.origin, -64.0, forward, endPos);
		tr = gi.trace(bomb->s.origin, NULL, NULL, endPos, bomb, MASK_SOLID);
		VectorCopy(tr.endpos, bomb->s.origin);
		vectoangles(tr.plane.normal, bomb->s.angles);
	}

	// set up ourself
	setupBomb(bomb, "misc_ired", TBOMB_DAMAGE, TBOMB_RADIUS_DAMAGE);
	
	if (bomb->targetname)
	{
		bomb->use = use_tripbomb;
	}
	else
	{
		bomb->think = create_tripbomb_laser;
		bomb->nextthink = level.time + TBOMB_DELAY;
	}
	gi.linkentity(bomb);
}

/*
======================================================================

Sonic Cannon

======================================================================
*/
void weapon_sc_fire (edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (!(ent->client->buttons & BUTTON_ATTACK))
	{
		ent->client->ps.gunframe++;

    if(ent->client->weapon_sound && ent->client->ps.gunframe < 18)
    {
      ent->client->ps.gunframe = 18;
    }
	}
	else
	{
		if(EMPNukeCheck(ent, ent->s.origin))
		{
			gi.sound (ent, CHAN_AUTO, gi.soundindex("items/empnuke/emp_missfire.wav"), 1, ATTN_NORM, 0);
			
			ent->client->ps.gunframe = 18;
			ent->client->weapon_sound = 0;
			ent->weaponsound_time = 0;

	    ent->dmg_radius = 0;
		  ent->client->startFireTime = 0;
			return;
		}

    if(!ent->client->startFireTime)
    {
      ent->client->startFireTime = level.time;
    }
    else if(level.time - ent->client->startFireTime >= SC_MAXFIRETIME)
    {
      ent->client->ps.gunframe = 17;
    }
    else
    {
      int old_cells = (int)ent->dmg_radius;
      ent->dmg_radius = ((level.time - ent->client->startFireTime) /  SC_MAXFIRETIME) * SC_MAXCELLS;

      if(old_cells < (int)ent->dmg_radius)
      {
        old_cells = (int)ent->dmg_radius - old_cells;


        if(ent->client->pers.inventory[ent->client->ammo_index] < old_cells)
        {
          ent->dmg_radius -= (old_cells - ent->client->pers.inventory[ent->client->ammo_index]);
          ent->client->pers.inventory[ent->client->ammo_index] = 0;
        }
        else
        {
          ent->client->pers.inventory[ent->client->ammo_index] -= old_cells;
        }
      }
    }

    if(!ent->client->pers.inventory[ent->client->ammo_index])
    {
      ent->client->ps.gunframe = 17;

			if (level.time >= ent->pain_debounce_time)
			{
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
				ent->pain_debounce_time = level.time + 1;
			}
			NoAmmoWeaponChange (ent);
		}
		else
		{
      if(ent->weaponsound_time < level.time)
      {
	      ent->client->weapon_sound = gi.soundindex("weapons/sonic/sc_fire.wav");
      }
		}

    fire_sconnanEffects (ent);

		ent->client->ps.gunframe++;
		if (ent->client->ps.gunframe == 18 && (level.time - ent->client->startFireTime) < SC_MAXFIRETIME && ent->client->pers.inventory[ent->client->ammo_index])
			ent->client->ps.gunframe = 12;
	}

	if (ent->client->ps.gunframe == 18)
	{
		ent->client->weapon_sound = 0;
    ent->weaponsound_time = 0;

		if(EMPNukeCheck(ent, ent->s.origin))
		{
			gi.sound (ent, CHAN_AUTO, gi.soundindex("items/empnuke/emp_missfire.wav"), 1, ATTN_NORM, 0);
		}
		else
		{
			if (!is_silenced)
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/sonic/sc_cool.wav"), 1, ATTN_NORM, 0);
			else
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/sonic/sc_cool.wav"), 0.4, ATTN_NORM, 0);

			if(ent->dmg_radius)
			{
				fire_sconnan (ent);
			}
		}

    ent->dmg_radius = 0;
    ent->client->startFireTime = 0;
	}
}

void Weapon_SonicCannon (edict_t *ent)
{
	static int	pause_frames[] = {32, 42, 52, 0};
	static int	fire_frames[]	= {12, 13, 14, 15, 16, 17, 0};

	if (!ent)
	{
		return;
	}

	if (ent->client->ps.gunframe == 0)
	{
		if (deathmatch->value)
		{
			if (!is_silenced)
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/sonic/sc_act.wav"), 1, ATTN_NORM, 0);
			else
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/sonic/sc_act.wav"), 0.4, ATTN_NORM, 0);
		}
    ent->weaponsound_time = 0;
    ent->client->startFireTime = 0;
    ent->dmg_radius = 0;
  }
  else if (ent->client->ps.gunframe == 53)
  {
		if (deathmatch->value)
		{
			if (!is_silenced)
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/sonic/sc_dact.wav"), 1, ATTN_NORM, 0);
			else
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/sonic/sc_dact.wav"), 0.4, ATTN_NORM, 0);
		}
  }
  else if((ent->client->buttons & BUTTON_ATTACK) && ent->weaponsound_time == 0)
  {
    ent->weaponsound_time = level.time + 0.4;

		if (!is_silenced)
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/sonic/sc_warm.wav"), 1, ATTN_NORM, 0);
		else
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/sonic/sc_warm.wav"), 0.4, ATTN_NORM, 0);
  }

  Weapon_Generic (ent, 6, 22, 52, 57, pause_frames, fire_frames, weapon_sc_fire);
}

void SpawnDamage (int type, vec3_t origin, vec3_t normal, int damage);

void fire_sconnanEffects (edict_t *self)
{
	vec3_t		start, end;
	vec3_t		forward, right;
	vec3_t		offset, v;
	trace_t		tr;

	if (!self)
	{
		return;
	}

	AngleVectors (self->client->v_angle, forward, right, NULL);

	VectorScale (forward, -3, self->client->kick_origin);
	self->client->kick_angles[0] = -3;

	VectorSet(offset, 0, 7,  self->viewheight-8);
	P_ProjectSource (self, offset, forward, right, start);

	VectorMA (start, 8192, forward, end);

  tr = gi.trace (start, NULL, NULL, end, self, MASK_SHOT|CONTENTS_SLIME|CONTENTS_LAVA);

	VectorMA (tr.endpos, -5, forward, end);

  VectorSet(v, crandom() * 10 - 20, crandom() * 10 - 20, crandom() * 10 - 20);
  SpawnDamage(TE_SHIELD_SPARKS, end, v, 0);
}

void scexplode_think(edict_t *self)
{
	if (!self)
	{
		return;
	}

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_ROCKET_EXPLOSION);
	gi.WritePosition (self->s.origin);
	gi.multicast (self->s.origin, MULTICAST_PHS);

	G_FreeEdict (self);
}

void fire_sconnan (edict_t *self)
{
	vec3_t		start, end, explodepos;
	vec3_t		forward, right, up;
	vec3_t		offset;
	trace_t		tr;
	float damage;
	float radius;

	if (!self)
	{
		return;
	}

	damage = self->dmg_radius / SC_MAXCELLS;
	radius = damage * SC_MAXRADIUS;
	damage = SC_BASEDAMAGE + (damage * SC_DAMGERANGE);

	AngleVectors (self->client->v_angle, forward, right, up);

	VectorScale (forward, -3, self->client->kick_origin);
	self->client->kick_angles[0] = -3;

	VectorSet(offset, 0, 7,  self->viewheight-8);
	P_ProjectSource (self, offset, forward, right, start);

	VectorMA (start, 8192, forward, end);

	tr = gi.trace (start, NULL, NULL, end, self, MASK_SHOT|CONTENTS_SLIME|CONTENTS_LAVA);

	if ((tr.ent != self) && (tr.ent->takedamage))
	{
		T_Damage (tr.ent, self, self, forward, tr.endpos, tr.plane.normal, damage, 0, 0, MOD_SONICCANNON);
	}

	T_RadiusDamagePosition (tr.endpos, self, self, damage, tr.ent, radius, MOD_SONICCANNON);

	VectorMA (tr.endpos, -5, forward, end);

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_ROCKET_EXPLOSION);
	gi.WritePosition (end);
	gi.multicast (self->s.origin, MULTICAST_PHS);

	damage -= 100;
	radius = 0.1;

	while(damage > 0)
	{
		edict_t	*explode;

		VectorMA (end, (50 * crandom()) - 5, forward, explodepos);
		VectorMA (explodepos, (50 * crandom()) - 5, right, explodepos);
		VectorMA (explodepos, (50 * crandom()) - 5, up, explodepos);

		explode = G_Spawn();
		VectorCopy (explodepos, explode->s.origin);

		explode->classname = "sconnanExplode";
		explode->nextthink = level.time + radius;
		explode->think = scexplode_think;

		radius += 0.1;
		damage -= 100;
	}

	// play quad damage sound
	playQuadSound(self);
}

/*
	Flares
*/
#define FLASH_RANGE		256.0
void FoundTarget (edict_t *self);

void flare_flash(edict_t *ent)
{
	edict_t *target = NULL;
	float dist;
	float ratio;
	float dot;
	vec3_t delta;
	vec3_t forward;

	if (!ent)
	{
		return;
	}

	// flash
	while (1)
	{
		// get the next entity near us
		target = findradius(target, ent->s.origin, FLASH_RANGE);
		if (target == NULL)
			break;
		if (!target->client && !(target->svflags & SVF_MONSTER)) 
			continue;
		if (target->deadflag)
			continue;
		if (!visible(ent, target))
			continue;

		// what's the distance, so that closer get's more
		VectorSubtract(ent->s.origin, target->s.origin, delta);
		dist = VectorLength(delta);
		ratio = 1 - (dist/FLASH_RANGE);
		if (ratio < 0)
			ratio = 0;

		// looking to the side get's less
		AngleVectors(target->s.angles, forward, NULL, NULL);
		VectorNormalize(delta);
		dot = Z_MAX(0.0, DotProduct(delta, forward));
		ratio *= dot;// * 1.25;

		// set the flash counter
		if (target->client)
		{
			target->client->flashTime += ratio*25;
			if (target->client->flashTime > 25)
				target->client->flashTime = 25;
			target->client->flashBase = 30;

			if (deathmatch->value &&
				!target->client->pers.gl_polyblend &&
				!(((int)zdmflags->value) & ZDM_NO_GL_POLYBLEND_DAMAGE))
				T_Damage(target, ent, ent->owner, vec3_origin, target->s.origin, vec3_origin, (int)(10.0*ratio), 0, 0, MOD_GL_POLYBLEND);
		}
		else if ((target->svflags & SVF_MONSTER) && strcmp(target->classname, "monster_zboss") != 0)
		{
			target->monsterinfo.flashTime =
				Z_MAX(target->monsterinfo.flashTime, ratio*150); // a little bit more advantageous
			target->monsterinfo.flashBase = 50;
			if (target->enemy == NULL)
			{
				target->enemy = ent->owner;
				FoundTarget(target);
			}
		}
	}
}

void flare_think(edict_t *self)
{
	if (!self)
	{
		return;
	}

	// on our last leg?
	if (level.time > self->timeout)
	{
		self->s.effects &= ~EF_ROCKET;
		self->think = G_FreeEdict;
		self->nextthink = level.time + 4.0;
		self->s.frame = 0;
		self->s.sound = 0;
		return;
	}

	self->s.frame++;
	
	if (self->s.frame > 14)
		self->s.frame = 5;

	// hissing sound
	self->s.sound = gi.soundindex ("weapons/flare/flarehis.wav");

	// do the visual thing
	flare_flash(self);

	// next frame
	self->nextthink = level.time + FRAMETIME;
}

void fire_flare (edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int radius_damage)
{
	edict_t	*flare;
	vec3_t adir;
	vec3_t up;

	if (!self)
	{
		return;
	}

	vectoangles (dir, adir);
	AngleVectors (adir, NULL, NULL, up);

	flare = G_Spawn();
	VectorCopy (start, flare->s.origin);
	VectorCopy (dir, flare->movedir);
	vectoangles (dir, flare->s.angles);
	VectorScale (dir, speed, flare->velocity);
	VectorMA (flare->velocity, 200 + crandom() * 10.0, up, flare->velocity);
	flare->movetype = MOVETYPE_BOUNCE;
	flare->clipmask = MASK_SHOT;
	flare->solid = SOLID_BBOX;
	flare->s.effects = EF_ROCKET;
	VectorSet(flare->mins, -4, -4, -4);
	VectorSet(flare->maxs, 4, 4, 4);
	flare->s.modelindex = gi.modelindex("models/objects/flare/tris.md2");
	flare->owner = self;
	flare->timeout = level.time + 8000/speed;
	flare->nextthink = level.time + 1.0;
	flare->think = flare_think;
	flare->dmg = damage;
	flare->radius_dmg = radius_damage;
	flare->dmg_radius = damage_radius;
	flare->classname = "flare";

	if (self->client)
		check_dodge (self, flare->s.origin, dir, speed);

	gi.linkentity (flare);
}

void Weapon_FlareLauncher_Fire (edict_t *ent)
{
	vec3_t	offset, start;
	vec3_t	forward, right;

	if (!ent)
	{
		return;
	}

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorSet(offset, 8, 8, ent->viewheight-8);
	P_ProjectSource (ent, offset, forward, right, start);
	fire_flare(ent, start, forward, 1, 600, 1, 1);

	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);

	// play quad sound
	playQuadSound(ent);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index]--;

	// play shooting sound
	if (!is_silenced)
		gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/flare/shoot.wav"), 1, ATTN_NORM, 0);
	else
		gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/flare/shoot.wav"), 0.4, ATTN_NORM, 0);
}

void Weapon_FlareGun (edict_t *ent)
{
	static int	pause_frames[]	= {15, 25, 35, 0};
	static int	fire_frames[]	= {8, 0};

	if (!ent)
	{
		return;
	}

	Weapon_Generic (ent, 5, 14, 44, 48, pause_frames, fire_frames, Weapon_FlareLauncher_Fire);
}

/******************************
	Sniper Rifle
*/
void fire_sniper_bullet (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick)
{
	trace_t tr;
	vec3_t end;
	vec3_t s;
	edict_t *ignore = self;
	int i = 0;

	if (!self)
	{
		return;
	}

	VectorMA (start, 8192, aimdir, end);
	VectorCopy(start, s);
	for(i=0;i<256;++i) // DG: prevent infinite loop (adapted from q2dos)
	{
		tr = gi.trace (s, NULL, NULL, end, ignore, MASK_SHOT_NO_WINDOW);
		if (tr.fraction >= 1.0)
			return;

		// if we hit a plasmashield, then pass thru it
		if (Q_stricmp(tr.ent->classname, "PlasmaShield") == 0)
		{
			ignore = tr.ent;
			VectorCopy(tr.endpos, s);
		}
		else
			break;
	}

	gi.WriteByte (svc_temp_entity);
	if (gi.pointcontents(tr.endpos) & MASK_WATER)
	{
		if (tr.plane.normal[2] > 0.7)
			gi.WriteByte (TE_GRENADE_EXPLOSION_WATER);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION_WATER);
	}
	else
	{
		if (tr.plane.normal[2] > 0.7)
			gi.WriteByte (TE_GRENADE_EXPLOSION);
		else
			gi.WriteByte (TE_ROCKET_EXPLOSION);
	}
	gi.WritePosition (tr.endpos);
	gi.multicast (tr.endpos, MULTICAST_PHS);

	if (tr.ent->takedamage)
		T_Damage (tr.ent, self, self, aimdir, tr.endpos, tr.plane.normal, damage, kick, DAMAGE_NO_ARMOR, MOD_SNIPERRIFLE);
}

void weapon_sniperrifle_fire (edict_t *ent)
{
	vec3_t forward, right;
	vec3_t offset, start;
	int damage;
	int kick;

	if (!ent)
	{
		return;
	}

	if (deathmatch->value)
	{	// normal damage is too extreme in dm
		damage = 150;
		kick = 300;
	}
	else
	{
		damage = 250;
		kick = 400;
	}

	if (is_quad)
	{
		damage *= 4;
		kick *= 4;
	}

	AngleVectors (ent->client->v_angle, forward, right, NULL);
	// centre the shot
	VectorSet(offset, 0, 0, ent->viewheight);
	VectorAdd(ent->s.origin, offset, start);
	fire_sniper_bullet(ent, start, forward, damage, kick);

	if (!is_silenced)
		gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/sniper/fire.wav"), 1, ATTN_NORM, 0);
	else
		gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/sniper/fire.wav"), 0.4, ATTN_NORM, 0);

  	PlayerNoise(ent, start, PNOISE_WEAPON);

	// play quad sound
	playQuadSound(ent);

	VectorScale (forward, -20, ent->client->kick_origin);
	ent->client->kick_angles[0] = -2;
	ent->client->pers.inventory[ent->client->ammo_index] -= ent->client->pers.weapon->quantity;
}

#define SNIPER_CHARGE_TIME	30
void Weapon_SniperRifle(edict_t *ent)
{
	/*
		Activate/Deactivate
		0 - 8	: Activate
		9 - 18	: Fire
		19 - 27 : Idle 1
		28 - 36	: Idle 2
		37 - 41	: Deactivate

		Zoom
		0 - 1 Zoom
		Hold 1 while zoomed
	*/
	const static int activateStart = 0;
	const static int activateEnd = 8;
	const static int deactivateStart = 37;
	const static int deactivateEnd = 41;
	const static int spFov = 15;
	const static int dmFov = 30;

	if (!ent)
	{
		return;
	}

	if (ent->client->weaponstate == WEAPON_DROPPING)
	{
		ent->client->sniperFramenum = 0;
		if (ent->client->ps.gunframe == deactivateStart)
		{
			// back to old fov
			ent->client->ps.fov = 90;	
			if (deathmatch->value)
				gi.sound(ent, CHAN_WEAPON2, gi.soundindex("weapons/sniper/snip_bye.wav"), 1, ATTN_NORM, 0);
		}
		else if (ent->client->ps.gunframe == deactivateEnd)
		{
			ChangeWeapon(ent);
			return;
		}

		ent->client->ps.gunframe++;
		return;
	}

	if (ent->client->weaponstate == WEAPON_ACTIVATING)
	{
		if (ent->client->ps.gunframe == activateStart)
		{
			// play the activation sound
			if (deathmatch->value)
				gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/sniper/snip_act.wav"), 1, ATTN_NORM, 0);
		}
		else if (ent->client->ps.gunframe == activateEnd)
		{
			ent->client->weaponstate = WEAPON_READY;
			ent->client->ps.gunindex = (deathmatch->value ? 
				gi.modelindex("models/weapons/v_sniper/dmscope/tris.md2") :
				gi.modelindex("models/weapons/v_sniper/scope/tris.md2") );
			ent->client->ps.gunframe = 0;
			ent->client->ps.fov = (deathmatch->value ? dmFov : spFov);
			ent->client->sniperFramenum = level.framenum + SNIPER_CHARGE_TIME;
			return;
		}

		ent->client->ps.gunframe++;
		return;
	}

	if ((ent->client->newweapon) && (ent->client->weaponstate != WEAPON_FIRING))
	{
		// back to other gun model
		ent->client->ps.gunindex = gi.modelindex("models/weapons/v_sniper/tris.md2");
		ent->client->weaponstate = WEAPON_DROPPING;
		ent->client->ps.gunframe = deactivateStart;
		return;
	}

	if (ent->client->weaponstate == WEAPON_READY)
	{
		ent->client->ps.gunindex = (deathmatch->value ? 
			gi.modelindex("models/weapons/v_sniper/dmscope/tris.md2") :
			gi.modelindex("models/weapons/v_sniper/scope/tris.md2") );
		
		ent->client->ps.fov = (deathmatch->value ? dmFov : spFov);

		// beep if the sniper frame num is a multiple of 10
		if (ent->client->sniperFramenum >= level.framenum)
		{
			if ((ent->client->sniperFramenum - level.framenum) % 10 == 1)
				gi.sound(ent, CHAN_WEAPON2, gi.soundindex("weapons/sniper/beep.wav"), 1, ATTN_NORM, 0);
		}

		if ( ((ent->client->latched_buttons|ent->client->buttons) & BUTTON_ATTACK) )
		{
			if (level.framenum >= ent->client->sniperFramenum)
			{
				ent->client->latched_buttons &= ~BUTTON_ATTACK;
				if ((!ent->client->ammo_index) || 
					( ent->client->pers.inventory[ent->client->ammo_index] >= ent->client->pers.weapon->quantity))
				{
					ent->client->weaponstate = WEAPON_FIRING;

					// start the animation
					ent->client->anim_priority = ANIM_ATTACK;
					if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
					{
						ent->s.frame = FRAME_crattak1-1;
						ent->client->anim_end = FRAME_crattak9;
					}
					else
					{
						ent->s.frame = FRAME_attack1-1;
						ent->client->anim_end = FRAME_attack8;
					}
				}
				else
				{
					if (level.time >= ent->pain_debounce_time)
					{
						gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
						ent->pain_debounce_time = level.time + 1;
					}
					NoAmmoWeaponChange (ent);
				}
			}
		}
	}

	if (ent->client->weaponstate == WEAPON_FIRING)
	{
		ent->client->ps.gunindex = (deathmatch->value ? 
				gi.modelindex("models/weapons/v_sniper/dmscope/tris.md2") :
				gi.modelindex("models/weapons/v_sniper/scope/tris.md2") );
			
		ent->client->ps.fov = (deathmatch->value ? dmFov : spFov);

		// fire
		weapon_sniperrifle_fire(ent);
		
		// start recharge
		ent->client->weaponstate = WEAPON_READY;
		ent->client->sniperFramenum = level.framenum + SNIPER_CHARGE_TIME;
	}
}

/*****************************
	Armageddon 2000
*/

void weapon_a2k_exp_think(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->s.frame++;
	self->s.skinnum++;

	if (self->s.frame == 6)
	{
		G_FreeEdict(self);
		return;
	}
	self->nextthink = level.time + FRAMETIME;
}

void Z_RadiusDamageVisible(edict_t *inflictor, edict_t *attacker, float damage, edict_t *ignore, float radius, int mod)
{
	float	points;
	edict_t	*ent = NULL;
	vec3_t	v;
	vec3_t	dir;

	if (!inflictor || !attacker || !ignore)
	{
		return;
	}

	while ((ent = findradius(ent, inflictor->s.origin, radius)) != NULL)
	{
		if (ent == ignore)
			continue;
		if (!ent->takedamage)
			continue;
		if (!visible(inflictor, ent))
			continue;

		VectorAdd (ent->mins, ent->maxs, v);
		VectorMA (ent->s.origin, 0.5, v, v);
		VectorSubtract (inflictor->s.origin, v, v);
		points = damage - 0.5 * VectorLength (v);
		if (ent == attacker)
			points = points * 0.5;
		if (points > 0)
		{
			if (CanDamage (ent, inflictor))
			{
				VectorSubtract (ent->s.origin, inflictor->s.origin, dir);
				T_Damage (ent, inflictor, attacker, dir, inflictor->s.origin, vec3_origin, (int)points, (int)points, DAMAGE_RADIUS, mod);
			}
		}
	}
}

#define A2K_FRAMENUM	50
void weapon_a2k_fire (edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (ent->client->ps.gunframe == 14)
	{
		ent->client->a2kFramenum = level.framenum + A2K_FRAMENUM;
		ent->client->pers.inventory[ent->client->ammo_index]--;
		ent->client->ps.gunframe++;
		
		gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/a2k/countdn.wav"), 1, ATTN_NORM, 0);

		// play quad sound
		playQuadSound(ent);
	}
	else if (ent->client->a2kFramenum == level.framenum)
	{
		// boom
		edict_t *exp = NULL;
		float damage = 2500;
		float dmg_radius = 512;
		// play quad sound
		playQuadSound(ent);
		if (is_quad)
		{
			damage *= 4;
			dmg_radius *= 4;
		}
		// do some damage
		T_RadiusDamage(ent, ent, damage, NULL, dmg_radius, MOD_A2K);
		
		// ok, now, do who ever's visible within 1024 units
		Z_RadiusDamageVisible(ent, ent, damage, NULL, dmg_radius * 2, MOD_A2K);

		exp = G_Spawn();
		exp->classname = "A2K Explosion";
		exp->solid = SOLID_NOT;
		exp->movetype = MOVETYPE_NONE;
		VectorClear(exp->mins);
		VectorClear(exp->maxs);
		VectorCopy(ent->s.origin, exp->s.origin);
		exp->s.modelindex = gi.modelindex("models/objects/b_explode/tris.md2");
		exp->s.frame = 0;
		exp->s.skinnum = 6;
		exp->think = weapon_a2k_exp_think;
		exp->nextthink = level.time + FRAMETIME;
		gi.linkentity(exp);
		gi.positioned_sound(exp->s.origin, exp, CHAN_AUTO, gi.soundindex("weapons/a2k/ak_exp01.wav"), 1, ATTN_NORM, 0);
		ent->client->ps.gunframe++;
		ent->client->weapon_sound = 0;
		return;
	}
	else if (ent->client->ps.gunframe == 19)
	{
		// don't increase the gunframe
		return;
	}
}

/*
	00-09 Active
	10-19 Boom (14 actual fire frame)
	20-29 Idle1
	30-39 Idle2
	40-49 Idle3
	50-55 Away
*/
void Weapon_A2k (edict_t *ent)
{
	static int	pause_frames[]	= {20, 30, 40, 0};
	static int	fire_frames[]	= {14, 19, 0};

	if (!ent)
	{
		return;
	}

	Weapon_Generic (ent, 9, 19, 49, 55, pause_frames, fire_frames, weapon_a2k_fire);
}

/********************************************
	Push

	0 - Start push
	4 - Contact
	8 - End Push
*/

qboolean push_hit (edict_t *self, vec3_t start, vec3_t aim, int damage, int kick)
{
	trace_t tr;
	vec3_t end;
	vec3_t v;

	if (!self) {
		return false;
	}

	//see if enemy is in range
	VectorMA(start, 64, aim, end);
	tr = gi.trace(start, NULL, NULL, end, self, MASK_SHOT);
	if (tr.fraction >= 1)
		return false;

	// play sound
	gi.sound(self, CHAN_WEAPON, gi.soundindex("weapons/push/contact.wav"), 1, ATTN_NORM, 0);

	if (tr.ent->svflags & SVF_MONSTER ||
		tr.ent->client)
	{
		// do our special form of knockback here
		VectorMA (tr.ent->absmin, 0.75, tr.ent->size, v);
		VectorSubtract (v, start, v);
		VectorNormalize (v);
		VectorMA (tr.ent->velocity, kick, v, tr.ent->velocity);
		if (tr.ent->velocity[2] > 0)
			tr.ent->groundentity = NULL;
	}
	else if (tr.ent->movetype == MOVETYPE_FALLFLOAT)
	{
		if (tr.ent->touch)
		{
			float mass = tr.ent->mass;
			tr.ent->mass *= 0.25;
			tr.ent->touch(tr.ent, self, NULL, NULL);
			tr.ent->mass = mass;
		}
	}

	// ok, we hit something, damage it
	if (!tr.ent->takedamage)
		return false;

	// do the damage
	T_Damage (tr.ent, self, self, aim, tr.endpos, vec3_origin, damage, kick/2, DAMAGE_NO_KNOCKBACK, MOD_HIT);

	return true;
}

void Action_Push (edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	if (ent->client->ps.gunframe == 0)
	{
		ent->client->ps.gunframe++;
	}
	else if (ent->client->ps.gunframe == 4)
	{
		vec3_t forward;
		vec3_t offset;
		vec3_t start;
		
		// contact
		AngleVectors(ent->client->v_angle, forward, NULL, NULL);
		VectorSet(offset, 0, 0, ent->viewheight * 0.5);
		VectorAdd(ent->s.origin, offset, start);
		push_hit(ent, start, forward, 2, 512);
		ent->client->ps.gunframe++;
	}
	else if (ent->client->ps.gunframe == 8)
	{
		// go back to old weapon
		ent->client->newweapon = ent->client->pers.lastweapon;
		ChangeWeapon(ent);
	}
	else
		ent->client->ps.gunframe++;
}
