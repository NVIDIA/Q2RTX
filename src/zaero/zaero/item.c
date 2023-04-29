#include "../header/local.h"

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

void zCam_TrackEntity(struct edict_s *player, struct edict_s *track, qboolean playerVisiable, qboolean playerOffset);
void zCam_Stop(struct edict_s *player);

void fire_empnuke(edict_t	*ent, vec3_t center, int radius);

/*
	misc_securitycamera

	This is where the visor locates too...
*/
void use_securitycamera (edict_t *self, edict_t *other, edict_t *activator)
{
	if (!self)
	{
		return;
	}

	self->active = !self->active;
}

#define CAMERA_FRAME_FIRST	0
#define CAMERA_FRAME_LAST	59
void securitycamera_think(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->active)
	{
		self->s.frame++;
		if (self->s.frame > CAMERA_FRAME_LAST)
			self->s.frame = CAMERA_FRAME_FIRST;
	}

	if (self->timeout > level.time)
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

void camera_pain(edict_t *self, edict_t *other, float kick, int damage)
{
	if (!self)
	{
		return;
	}

	self->timeout = level.time + FRAMETIME * 2;
}

void SP_misc_securitycamera(edict_t *self)
{
	vec3_t offset, forward, up;

	if (!self)
	{
		return;
	}

	// no message? error
	if (!self->message)
	{
		gi.error("misc_securitycamera w/o message");
		G_FreeEdict(self);
		return;
	}

	self->solid = SOLID_BBOX;
	self->movetype = MOVETYPE_NONE;
	self->s.modelindex = gi.modelindex("models/objects/camera/tris.md2");

	// set the bounding box
	VectorSet(self->mins, -16, -16, -32);
	VectorSet(self->maxs, 16, 16, 0);

	// set the angle of direction
	VectorCopy(self->mangle, self->move_angles);
	VectorSet(self->s.angles, 0, self->mangle[YAW], 0);
	
	// get an offset
	AngleVectors(self->s.angles, forward, NULL, up);
	VectorSet(offset, 0, 0, 0);
	VectorMA(offset, 8, forward, offset);
	VectorMA(offset, -32, up, offset);
	VectorAdd(self->s.origin, offset, self->move_origin);

	if (self->targetname)
	{
		self->use = use_securitycamera;
		self->active = false;
	}
	else
	{
		self->active = true;
	}
	self->think = securitycamera_think;
	self->nextthink = level.time + FRAMETIME;

	self->health = 1;
	self->takedamage = DAMAGE_IMMORTAL; // health will not be deducted
	self->pain = camera_pain;

	gi.linkentity(self);
}

char *camera_statusbar =
"xv 26 yb -75 string \"Tracking %s\" "
// timer
"if 20 "
"	xv	246 "
"	num	3	21 "
"	xv	296 "
"	pic	20 "
"endif "
;

void updateVisorHud(edict_t *ent)
{
	static char buf[1024];

	if (!ent)
	{
		return;
	}

	gi.WriteByte (svc_layout);
	sprintf(buf, camera_statusbar, ent->client->zCameraTrack->message);
	gi.WriteString(buf);
}

void startVisorStatic(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	ent->client->zCameraStaticFramenum = level.time + FRAMETIME * 2;
}

void startVisor(edict_t *ent, edict_t *e)
{
	if (!ent || !e)
	{
		return;
	}

	// don't do anything if we're already at the destination camera
	if (e == ent->client->zCameraTrack)
		return;

	// no more time?
	if (ent->client->pers.visorFrames <= 0)
	{
		gi.cprintf(ent, PRINT_HIGH, "No time left for visor\n");
		return;
	}

	// look thru the camera
	zCam_TrackEntity(ent, e, true, true);

	startVisorStatic(ent);
	updateVisorHud(ent);
	gi.unicast(ent, true);			// reliably send to ent
	ent->client->showscores = true;

	// play activation sound
	gi.sound(ent, CHAN_AUTO, gi.soundindex("items/visor/act.wav"), 1, ATTN_NORM, 0);
}

void stopCamera(edict_t *self)
{
	if (!self)
	{
		return;
	}

	zCam_Stop(self);
	self->client->showscores = false;
	gi.sound(self, CHAN_AUTO, gi.soundindex("items/visor/deact.wav"), 1, ATTN_NORM, 0);
}

edict_t *findNextCamera(edict_t *old)
{
	edict_t *e = NULL;

	// first of all, are there *any* cameras?
	e = G_Find(NULL, FOFS(classname), "misc_securitycamera");
	if (e == NULL)
		return NULL;

	// start with the current and try to find another good camera
	e = old;
	while(1)
	{
		e = G_Find(e, FOFS(classname), "misc_securitycamera");
		if (e == NULL)
			continue; // loop back around

		if (e == old)
			return e;
		
		if (!e->active)
			continue;

		return e;
	}
	return NULL;
}

void Use_Visor (edict_t *ent, gitem_t *item)
{
	if (!ent || !item)
	{
		return;
	}

	if (ent->client->zCameraTrack == NULL)
	{
		edict_t *e = findNextCamera(NULL);
		if (e == NULL)
		{
			gi.cprintf(ent, PRINT_HIGH, "No cameras are available\n");
			return;
		}

		startVisor(ent, e);
	}
	else
	{
		edict_t *e = findNextCamera(ent->client->zCameraTrack);
		if (e != NULL && 
			e != ent->client->zCameraTrack)
		{
			ent->client->zCameraTrack = e;
			// play sound
			gi.sound(ent, CHAN_AUTO, gi.soundindex("items/visor/act.wav"), 1, ATTN_NORM, 0);
			startVisorStatic(ent);
			updateVisorHud(ent);
			gi.unicast(ent, true);			// reliably send to ent
		}
	}
}

/*
=================
EMP Nuke
=================
*/
void weapon_EMPNuke_fire (edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	fire_empnuke(ent, ent->s.origin, 1024);

	ent->client->pers.inventory[ent->client->ammo_index]--;

	if(ent->client->pers.inventory[ent->client->ammo_index])
	{
		ent->client->weaponstate = WEAPON_ACTIVATING;
		ent->client->ps.gunframe = 0;
	}
	else
	{
		NoAmmoWeaponChange (ent);
		ChangeWeapon(ent);
	}
}

void Weapon_EMPNuke (edict_t *ent)
{
	static int	pause_frames[] = {25, 34, 43, 0};
	static int	fire_frames[]	= {16, 0};

	if (!ent)
	{
		return;
	}

	if (deathmatch->value)
	{
		if (ent->client->ps.gunframe == 0)
		{
			gi.sound(ent, CHAN_AUTO, gi.soundindex("items/empnuke/emp_act.wav"), 1, ATTN_NORM, 0);
		}
		else if (ent->client->ps.gunframe == 11)
		{
			gi.sound(ent, CHAN_AUTO, gi.soundindex("items/empnuke/emp_spin.wav"), 1, ATTN_NORM, 0);
		}
		else if (ent->client->ps.gunframe == 35)
		{
			gi.sound(ent, CHAN_AUTO, gi.soundindex("items/empnuke/emp_idle.wav"), 1, ATTN_NORM, 0);
		}
	}

	Weapon_Generic (ent, 9, 16, 43, 47, pause_frames, fire_frames, weapon_EMPNuke_fire);
}

void empnukeFinish(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	G_FreeEdict(ent);
}

void empBlastAnim(edict_t *ent)
{
	if (!ent)
	{
		return;
	}

	ent->s.frame++;
	ent->s.skinnum++;

	if(ent->s.frame > 5)
	{
		ent->svflags |= SVF_NOCLIENT;
		ent->s.modelindex = 0;
		ent->s.frame = 0;
		ent->s.skinnum = 0;

		ent->think = empnukeFinish;
		ent->nextthink = level.time + 30;
	}
	else
	{
		ent->nextthink = level.time + FRAMETIME;
	}
}

void fire_empnuke(edict_t *ent, vec3_t center, int radius)
{
	edict_t	*empnuke;

	if (!ent)
	{
		return;
	}

	gi.sound(ent, CHAN_VOICE, gi.soundindex("items/empnuke/emp_trg.wav"), 1, ATTN_NORM, 0);

	empnuke = G_Spawn();
	empnuke->owner = ent;
	empnuke->dmg = radius;
	VectorCopy(center, empnuke->s.origin);
	empnuke->classname = "EMPNukeCenter";
	empnuke->movetype = MOVETYPE_NONE;
	empnuke->s.modelindex = gi.modelindex("models/objects/b_explode/tris.md2");
	empnuke->s.skinnum = 0;

	empnuke->think = empBlastAnim;
	empnuke->nextthink = level.time + FRAMETIME;
	gi.linkentity (empnuke);
}

qboolean EMPNukeCheck(edict_t *ent, vec3_t pos)
{
	edict_t	*check = NULL;

	if (!ent)
	{
		return false;
	}

	while ((check = G_Find (check, FOFS(classname), "EMPNukeCenter")) != NULL)
	{
		vec3_t	v;

		if(check->owner != ent)
		{
			VectorSubtract (check->s.origin, pos, v);
			if(VectorLength(v) <= check->dmg)
			{
				return true;
			}
		}
	}

	return false;
}

/*
=================
Plasma Shield
=================
*/
void PlasmaShield_die (edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (deathmatch->value)
	{
		gi.sound(self, CHAN_VOICE, gi.soundindex("items/plasmashield/psdie.wav"), 1, ATTN_NORM, 0);
	}
	G_FreeEdict(self);
}

void PlasmaShield_killed (edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	if (!self)
	{
		return;
	}

	PlasmaShield_die(self);
}

void Use_PlasmaShield (edict_t *ent, gitem_t *item)
{
	int ammoIdx = ITEM_INDEX(item);
	edict_t	*PlasmaShield;
	vec3_t forward, right, up, frontbottomleft, backtopright;

	if (!ent || !item)
	{
		return;
	}

	if(!ent->client->pers.inventory[ammoIdx])
	{
		return;
	}

	if(EMPNukeCheck(ent, ent->s.origin))
	{
		gi.sound (ent, CHAN_AUTO, gi.soundindex("items/empnuke/emp_missfire.wav"), 1, ATTN_NORM, 0);
		return;
	}

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ammoIdx]--;

	if (deathmatch->value)
	{
		gi.sound(ent, CHAN_VOICE, gi.soundindex("items/plasmashield/psfire.wav"), 1, ATTN_NORM, 0);
	}

	PlasmaShield = G_Spawn();
	PlasmaShield->classname = "PlasmaShield";
	PlasmaShield->movetype = MOVETYPE_PUSH;
	PlasmaShield->solid = SOLID_BBOX;
	PlasmaShield->s.modelindex = gi.modelindex("sprites/plasmashield.sp2");
	PlasmaShield->s.effects |= EF_POWERSCREEN;
	PlasmaShield->s.sound = gi.soundindex ("items/plasmashield/psactive.wav");

	AngleVectors (ent->client->v_angle, forward, right, up);
	vectoangles (forward, PlasmaShield->s.angles);

	VectorMA (ent->s.origin, 50, forward, PlasmaShield->s.origin);

	VectorScale(forward, 10, frontbottomleft);
	VectorMA(frontbottomleft, -30, right, frontbottomleft);
	VectorMA(frontbottomleft, -30, up, frontbottomleft);

	VectorScale(forward, 5, backtopright);
	VectorMA(backtopright, 30, right, backtopright);
	VectorMA(backtopright, 50, up, backtopright);

	ClearBounds (PlasmaShield->mins, PlasmaShield->maxs);

	AddPointToBounds (frontbottomleft, PlasmaShield->mins, PlasmaShield->maxs);
	AddPointToBounds (backtopright, PlasmaShield->mins, PlasmaShield->maxs);

	PlasmaShield->health = PlasmaShield->max_health = 4000;
	PlasmaShield->die = PlasmaShield_killed;
	PlasmaShield->takedamage = DAMAGE_YES;

	PlasmaShield->think = PlasmaShield_die;
	PlasmaShield->nextthink = level.time + 10;

	gi.linkentity (PlasmaShield);
}

/*
	misc_crate
*/
void barrel_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf);

void setupCrate(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->solid = SOLID_BBOX;
	self->movetype = MOVETYPE_FALLFLOAT;

	if (!self->mass)
		self->mass = 400;

	self->touch = barrel_touch;
	self->think = M_droptofloor;
	self->nextthink = level.time + 2 * FRAMETIME;

	gi.linkentity(self);
}

void SP_misc_crate(edict_t *self)
{
	if (!self)
	{
		return;
	}

	// setup specific to this size
	self->s.modelindex = gi.modelindex("models/objects/crate/crate64.md2");
	VectorSet (self->mins, -32, -32, 0);
	VectorSet (self->maxs, 32, 32, 64);

	// generic crate setup
	setupCrate(self);
}

void SP_misc_crate_medium(edict_t *self)
{
	if (!self)
	{
		return;
	}

	// setup specific to this size
	self->s.modelindex = gi.modelindex("models/objects/crate/crate48.md2");
	VectorSet (self->mins, -24, -24, 0);
	VectorSet (self->maxs, 24, 24, 48);

	// generic crate setup
	setupCrate(self);
}

void SP_misc_crate_small(edict_t *self)
{
	if (!self)
	{
		return;
	}

	// setup specific to this size
	self->s.modelindex = gi.modelindex("models/objects/crate/crate32.md2");
	VectorSet (self->mins, -16, -16, 0);
	VectorSet (self->maxs, 16, 16, 32);

	// generic crate setup
	setupCrate(self);
}

qboolean thruBarrier(edict_t *targ, edict_t *inflictor)
{
	trace_t tr;
	edict_t *e = inflictor;

	if (!targ || !inflictor)
	{
		return false;
	}

	while(e)
	{
		tr = gi.trace(e->s.origin, NULL, NULL, targ->s.origin, e, MASK_SHOT);
		if (!tr.ent || tr.fraction >= 1.0)
			return false;

		if (tr.ent == targ)
			return false;

		if (tr.ent->classname && Q_stricmp(tr.ent->classname, "func_barrier") == 0)
			return true;

		if(e == tr.ent)
			break;

		e = tr.ent;
	}
	return true;
}

void barrier_think(edict_t *self)
{
	if (!self)
	{
		return;
	}

	if (self->timeout > level.time)
	{
		self->svflags &= ~SVF_NOCLIENT;
	}
	else
	{
		self->svflags |= SVF_NOCLIENT;
	}

	self->nextthink = level.time + FRAMETIME;
}

void barrier_pain(edict_t *self, edict_t *other, float kick, int damage)
{
	if (!self)
	{
		return;
	}

	self->timeout = level.time + FRAMETIME * 2;
	if (self->damage_debounce_time < level.time)
	{
		gi.sound (self, CHAN_AUTO, gi.soundindex("weapons/lashit.wav"), 1, ATTN_NORM, 0);
		self->damage_debounce_time = level.time + FRAMETIME * 2;
	}
}

void barrier_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (!self || !other)
	{
		return;
	}

	if (other == world)
		return;

	self->timeout = level.time + FRAMETIME * 2;
	if (self->touch_debounce_time < level.time)
	{
		gi.sound (self, CHAN_AUTO, gi.soundindex("weapons/lashit.wav"), 1, ATTN_NORM, 0);
		self->touch_debounce_time = level.time + FRAMETIME * 2;
	}

}

void SP_func_barrier(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->solid = SOLID_BBOX;
	self->movetype = MOVETYPE_NONE;
	self->s.modelindex = gi.modelindex("models/objects/wall/tris.md2");
	self->svflags = SVF_NOCLIENT;
	self->s.effects = EF_BFG;

	self->think = barrier_think;
	self->nextthink = level.time + FRAMETIME;
	self->touch = barrier_touch;
	self->health = 1;
	self->takedamage = DAMAGE_IMMORTAL; // health will not be deducted
	self->pain = barrier_pain;

	gi.linkentity(self);
}

void SP_misc_seat(edict_t *self)
{
	if (!self)
	{
		return;
	}

	self->s.modelindex = gi.modelindex("models/objects/seat/tris.md2");
	VectorSet(self->mins, -16, -16, 0);
	VectorSet(self->maxs, 16, 16, 40);

	// make this pushable
	setupCrate(self);
}

