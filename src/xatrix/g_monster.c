/* =======================================================================
 *
 * Monster utility functions.
 *
 * =======================================================================
 */

#include "header/local.h"

void monster_start_go(edict_t *self);

/* Monster weapons */

void
monster_fire_bullet(edict_t *self, vec3_t start, vec3_t dir, int damage,
		int kick, int hspread, int vspread, int flashtype)
{
	if (!self)
	{
		return;
	}

	fire_bullet(self, start, dir, damage, kick, hspread, vspread, MOD_UNKNOWN);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}

void
monster_fire_shotgun(edict_t *self, vec3_t start, vec3_t aimdir, int damage,
		int kick, int hspread, int vspread, int count, int flashtype)
{
 	if (!self)
	{
		return;
	}

	fire_shotgun(self, start, aimdir, damage, kick, hspread, vspread,
			count, MOD_UNKNOWN);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}

void
monster_fire_blaster(edict_t *self, vec3_t start, vec3_t dir, int damage,
		int speed, int flashtype, int effect)
{
 	if (!self)
	{
		return;
	}

	fire_blaster(self, start, dir, damage, speed, effect, false);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}

void
monster_fire_blueblaster(edict_t *self, vec3_t start, vec3_t dir, int damage,
		int speed, int flashtype, int effect)
{
	if (!self)
	{
		return;
	}

	fire_blueblaster(self, start, dir, damage, speed, effect);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(MZ_BLUEHYPERBLASTER);
	gi.multicast(start, MULTICAST_PVS);
}

void
monster_fire_ionripper(edict_t *self, vec3_t start, vec3_t dir, int damage,
		int speed, int flashtype, int effect)
{
 	if (!self)
	{
		return;
	}

	fire_ionripper(self, start, dir, damage, speed, effect);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}

void
monster_fire_heat(edict_t *self, vec3_t start, vec3_t dir, int damage,
		int speed, int flashtype)
{
 	if (!self)
	{
		return;
	}

	fire_heat(self, start, dir, damage, speed, damage, damage);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}

void
dabeam_hit(edict_t *self)
{
	edict_t *ignore;
	vec3_t start;
	vec3_t end;
	trace_t tr;

  	if (!self)
	{
		return;
	}

	ignore = self;
	VectorCopy(self->s.origin, start);
	VectorMA(start, 2048, self->movedir, end);

	while (1)
	{
		tr = gi.trace(start, NULL, NULL, end, ignore,
				CONTENTS_SOLID | CONTENTS_MONSTER | CONTENTS_DEADMONSTER);

		if (!tr.ent)
		{
			break;
		}

		/* hurt it if we can */
		if ((tr.ent->takedamage) && !(tr.ent->flags & FL_IMMUNE_LASER) &&
			(tr.ent != self->owner))
		{
			T_Damage(tr.ent, self, self->owner, self->movedir, tr.endpos,
					vec3_origin, self->dmg, skill->value, DAMAGE_ENERGY,
					MOD_TARGET_LASER);
		}

		if (self->dmg < 0) /* healer ray */
		{
			/* when player is at 100 health
			   just undo health fix */
			if (tr.ent->client && (tr.ent->health > 100))
			{
				tr.ent->health += self->dmg;
			}
		}

		/* if we hit something that's not a monster or
		   player or is immune to lasers, we're done */
		if (!(tr.ent->svflags & SVF_MONSTER) && (!tr.ent->client))
		{
			if (self->spawnflags & 0x80000000)
			{
				self->spawnflags &= ~0x80000000;
				gi.WriteByte(svc_temp_entity);
				gi.WriteByte(TE_LASER_SPARKS);
				gi.WriteByte(10);
				gi.WritePosition(tr.endpos);
				gi.WriteDir(tr.plane.normal);
				gi.WriteByte(self->s.skinnum);
				gi.multicast(tr.endpos, MULTICAST_PVS);
			}

			break;
		}

		ignore = tr.ent;
		VectorCopy(tr.endpos, start);
	}

	VectorCopy(tr.endpos, self->s.old_origin);
	self->nextthink = level.time + 0.1;
	self->think = G_FreeEdict;
}

void
monster_dabeam(edict_t *self)
{
	vec3_t last_movedir;
	vec3_t point;

  	if (!self)
	{
		return;
	}

	self->movetype = MOVETYPE_NONE;
	self->solid = SOLID_NOT;
	self->s.renderfx |= RF_BEAM | RF_TRANSLUCENT;
	self->s.modelindex = 1;

	self->s.frame = 2;

	if (self->owner->monsterinfo.aiflags & AI_MEDIC)
	{
		self->s.skinnum = 0xf3f3f1f1;
	}
	else
	{
		self->s.skinnum = 0xf2f2f0f0;
	}

	if (self->enemy)
	{
		VectorCopy(self->movedir, last_movedir);
		VectorMA(self->enemy->absmin, 0.5, self->enemy->size, point);

		if (self->owner->monsterinfo.aiflags & AI_MEDIC)
		{
			point[0] += sin(level.time) * 8;
		}

		VectorSubtract(point, self->s.origin, self->movedir);
		VectorNormalize(self->movedir);

		if (!VectorCompare(self->movedir, last_movedir))
		{
			self->spawnflags |= 0x80000000;
		}
	}
	else
	{
		G_SetMovedir(self->s.angles, self->movedir);
	}

	self->think = dabeam_hit;
	self->nextthink = level.time + 0.1;
	VectorSet(self->mins, -8, -8, -8);
	VectorSet(self->maxs, 8, 8, 8);
	gi.linkentity(self);

	self->spawnflags |= 0x80000001;
	self->svflags &= ~SVF_NOCLIENT;
}

void
monster_fire_grenade(edict_t *self, vec3_t start, vec3_t aimdir,
		int damage, int speed, int flashtype)
{
  	if (!self)
	{
		return;
	}

	fire_grenade(self, start, aimdir, damage, speed, 2.5, damage + 40);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}

void
monster_fire_rocket(edict_t *self, vec3_t start, vec3_t dir,
		int damage, int speed, int flashtype)
{
  	if (!self)
	{
		return;
	}

	fire_rocket(self, start, dir, damage, speed, damage + 20, damage);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}

void
monster_fire_railgun(edict_t *self, vec3_t start, vec3_t aimdir,
		int damage, int kick, int flashtype)
{
  	if (!self)
	{
		return;
	}

	fire_rail(self, start, aimdir, damage, kick);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}

void
monster_fire_bfg(edict_t *self, vec3_t start, vec3_t aimdir, int damage,
		int speed, int kick, float damage_radius, int flashtype)
{
  	if (!self)
	{
		return;
	}

	fire_bfg(self, start, aimdir, damage, speed, damage_radius);

	gi.WriteByte(svc_muzzleflash2);
	gi.WriteShort(self - g_edicts);
	gi.WriteByte(flashtype);
	gi.multicast(start, MULTICAST_PVS);
}

/* Monster utility functions */

void
M_FliesOff(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	self->s.effects &= ~EF_FLIES;
	self->s.sound = 0;
}

void
M_FliesOn(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	if (self->waterlevel)
	{
		return;
	}

	self->s.effects |= EF_FLIES;
	self->s.sound = gi.soundindex("infantry/inflies1.wav");
	self->think = M_FliesOff;
	self->nextthink = level.time + 60;
}

void
M_FlyCheck(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	if (self->waterlevel)
	{
		return;
	}

	if (random() > 0.5)
	{
		return;
	}

	self->think = M_FliesOn;
	self->nextthink = level.time + 5 + 10 * random();
}

void
AttackFinished(edict_t *self, float time)
{
  	if (!self)
	{
		return;
	}

	self->monsterinfo.attack_finished = level.time + time;
}

void
M_CheckGround(edict_t *ent)
{
	vec3_t point;
	trace_t trace;

  	if (!ent)
	{
		return;
	}

	if (ent->flags & (FL_SWIM | FL_FLY))
	{
		return;
	}

	if (ent->velocity[2] > 100)
	{
		ent->groundentity = NULL;
		return;
	}

	/* if the hull point one-quarter unit down
	   is solid the entity is on ground */
	point[0] = ent->s.origin[0];
	point[1] = ent->s.origin[1];
	point[2] = ent->s.origin[2] - 0.25;

	trace = gi.trace(ent->s.origin, ent->mins, ent->maxs,
			point, ent, MASK_MONSTERSOLID);

	/* check steepness */
	if ((trace.plane.normal[2] < 0.7) && !trace.startsolid)
	{
		ent->groundentity = NULL;
		return;
	}

	if (!trace.startsolid && !trace.allsolid)
	{
		VectorCopy(trace.endpos, ent->s.origin);
		ent->groundentity = trace.ent;
		ent->groundentity_linkcount = trace.ent->linkcount;
		ent->velocity[2] = 0;
	}
}

void
M_CatagorizePosition(edict_t *ent)
{
	vec3_t point;
	int cont;

  	if (!ent)
	{
		return;
	}

	/* get waterlevel */
	point[0] = (ent->absmax[0] + ent->absmin[0])/2;
	point[1] = (ent->absmax[1] + ent->absmin[1])/2;
	point[2] = ent->absmin[2] + 2;
	cont = gi.pointcontents(point);

	if (!(cont & MASK_WATER))
	{
		ent->waterlevel = 0;
		ent->watertype = 0;
		return;
	}

	ent->watertype = cont;
	ent->waterlevel = 1;
	point[2] += 26;
	cont = gi.pointcontents(point);

	if (!(cont & MASK_WATER))
	{
		return;
	}

	ent->waterlevel = 2;
	point[2] += 22;
	cont = gi.pointcontents(point);

	if (cont & MASK_WATER)
	{
		ent->waterlevel = 3;
	}
}

void
M_WorldEffects(edict_t *ent)
{
	int dmg;

  	if (!ent)
	{
		return;
	}

	if (ent->health > 0)
	{
		if (!(ent->flags & FL_SWIM))
		{
			if (ent->waterlevel < 3)
			{
				ent->air_finished = level.time + 12;
			}
			else if (ent->air_finished < level.time)
			{
				/* drown! */
				if (ent->pain_debounce_time < level.time)
				{
					dmg = 2 + 2 * floor(level.time - ent->air_finished);

					if (dmg > 15)
					{
						dmg = 15;
					}

					T_Damage(ent, world, world, vec3_origin, ent->s.origin,
							vec3_origin, dmg, 0, DAMAGE_NO_ARMOR, MOD_WATER);
					ent->pain_debounce_time = level.time + 1;
				}
			}
		}
		else
		{
			if (ent->waterlevel > 0)
			{
				ent->air_finished = level.time + 9;
			}
			else if (ent->air_finished < level.time)
			{
				/* suffocate! */
				if (ent->pain_debounce_time < level.time)
				{
					dmg = 2 + 2 * floor(level.time - ent->air_finished);

					if (dmg > 15)
					{
						dmg = 15;
					}

					T_Damage(ent, world, world, vec3_origin, ent->s.origin,
							vec3_origin, dmg, 0, DAMAGE_NO_ARMOR, MOD_WATER);
					ent->pain_debounce_time = level.time + 1;
				}
			}
		}
	}

	if (ent->waterlevel == 0)
	{
		if (ent->flags & FL_INWATER)
		{
			gi.sound(ent, CHAN_BODY, gi.soundindex( "player/watr_out.wav"),
				   	1, ATTN_NORM, 0);
			ent->flags &= ~FL_INWATER;
		}

		return;
	}

	if ((ent->watertype & CONTENTS_LAVA) && !(ent->flags & FL_IMMUNE_LAVA))
	{
		if (ent->damage_debounce_time < level.time)
		{
			ent->damage_debounce_time = level.time + 0.2;
			T_Damage(ent, world, world, vec3_origin, ent->s.origin,
					vec3_origin, 10 * ent->waterlevel, 0, 0, MOD_LAVA);
		}
	}

	if ((ent->watertype & CONTENTS_SLIME) && !(ent->flags & FL_IMMUNE_SLIME))
	{
		if (ent->damage_debounce_time < level.time)
		{
			ent->damage_debounce_time = level.time + 1;
			T_Damage(ent, world, world, vec3_origin, ent->s.origin, vec3_origin,
					4 * ent->waterlevel, 0, 0, MOD_SLIME);
		}
	}

	if (!(ent->flags & FL_INWATER))
	{
		if (!(ent->svflags & SVF_DEADMONSTER))
		{
			if (ent->watertype & CONTENTS_LAVA)
			{
				if (random() <= 0.5)
				{
					gi.sound(ent, CHAN_BODY, gi.soundindex( "player/lava1.wav"),
						   	1, ATTN_NORM, 0);
				}
				else
				{
					gi.sound(ent, CHAN_BODY, gi.soundindex("player/lava2.wav"),
						   	1, ATTN_NORM, 0);
				}
			}
			else if (ent->watertype & CONTENTS_SLIME)
			{
				gi.sound(ent, CHAN_BODY, gi.soundindex("player/watr_in.wav"),
					   	1, ATTN_NORM, 0);
			}
			else if (ent->watertype & CONTENTS_WATER)
			{
				gi.sound(ent, CHAN_BODY, gi.soundindex("player/watr_in.wav"),
					   	1, ATTN_NORM, 0);
			}
		}

		ent->flags |= FL_INWATER;
		ent->damage_debounce_time = 0;
	}
}

void
M_droptofloor(edict_t *ent)
{
	vec3_t end;
	trace_t trace;

  	if (!ent)
	{
		return;
	}

	ent->s.origin[2] += 1;
	VectorCopy(ent->s.origin, end);
	end[2] -= 256;

	trace = gi.trace(ent->s.origin, ent->mins, ent->maxs, end,
			ent, MASK_MONSTERSOLID);

	if ((trace.fraction == 1) || trace.allsolid)
	{
		return;
	}

	VectorCopy(trace.endpos, ent->s.origin);

	gi.linkentity(ent);
	M_CheckGround(ent);
	M_CatagorizePosition(ent);
}

void
M_SetEffects(edict_t *ent)
{
  	if (!ent)
	{
		return;
	}

	ent->s.effects &= ~(EF_COLOR_SHELL | EF_POWERSCREEN);
	ent->s.renderfx &= ~(RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE);

	if (ent->monsterinfo.aiflags & AI_RESURRECTING)
	{
		ent->s.effects |= EF_COLOR_SHELL;
		ent->s.renderfx |= RF_SHELL_RED;
	}

	if (ent->health <= 0)
	{
		return;
	}

	if (ent->powerarmor_time > level.time)
	{
		if (ent->monsterinfo.power_armor_type == POWER_ARMOR_SCREEN)
		{
			ent->s.effects |= EF_POWERSCREEN;
		}
		else if (ent->monsterinfo.power_armor_type == POWER_ARMOR_SHIELD)
		{
			ent->s.effects |= EF_COLOR_SHELL;
			ent->s.renderfx |= RF_SHELL_GREEN;
		}
	}
}

void
M_MoveFrame(edict_t *self)
{
	mmove_t *move;
	int index;

  	if (!self)
	{
		return;
	}

	move = self->monsterinfo.currentmove;
	self->nextthink = level.time + FRAMETIME;

	if ((self->monsterinfo.nextframe) &&
		(self->monsterinfo.nextframe >= move->firstframe) &&
		(self->monsterinfo.nextframe <= move->lastframe))
	{
		self->s.frame = self->monsterinfo.nextframe;
		self->monsterinfo.nextframe = 0;
	}
	else
	{
		if (self->s.frame == move->lastframe)
		{
			if (move->endfunc)
			{
				move->endfunc(self);

				/* regrab move, endfunc is very likely to change it */
				move = self->monsterinfo.currentmove;

				/* check for death */
				if (self->svflags & SVF_DEADMONSTER)
				{
					return;
				}
			}
		}

		if ((self->s.frame < move->firstframe) ||
			(self->s.frame > move->lastframe))
		{
			self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
			self->s.frame = move->firstframe;
		}
		else
		{
			if (!(self->monsterinfo.aiflags & AI_HOLD_FRAME))
			{
				self->s.frame++;

				if (self->s.frame > move->lastframe)
				{
					self->s.frame = move->firstframe;
				}
			}
		}
	}

	index = self->s.frame - move->firstframe;

	if (move->frame[index].aifunc)
	{
		if (!(self->monsterinfo.aiflags & AI_HOLD_FRAME))
		{
			move->frame[index].aifunc(self,
					move->frame[index].dist * self->monsterinfo.scale);
		}
		else
		{
			move->frame[index].aifunc(self, 0);
		}
	}

	if (move->frame[index].thinkfunc)
	{
		move->frame[index].thinkfunc(self);
	}
}

void
monster_think(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	M_MoveFrame(self);

	if (self->linkcount != self->monsterinfo.linkcount)
	{
		self->monsterinfo.linkcount = self->linkcount;
		M_CheckGround(self);
	}

	M_CatagorizePosition(self);
	M_WorldEffects(self);
	M_SetEffects(self);
}

/*
 * Using a monster makes it angry
 * at the current activator
 */
void
monster_use(edict_t *self, edict_t *other /* unused */, edict_t *activator)
{
	if (!self || !activator)
	{
		return;
	}

	if (self->enemy)
	{
		return;
	}

	if (self->health <= 0)
	{
		return;
	}

	if (activator->flags & FL_NOTARGET)
	{
		return;
	}

	if (!(activator->client) && !(activator->monsterinfo.aiflags & AI_GOOD_GUY))
	{
		return;
	}

	/* delay reaction so if the monster is
	   teleported, its sound is still heard */
	self->enemy = activator;
	FoundTarget(self);
}

void
monster_triggered_spawn(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	self->s.origin[2] += 1;
	KillBox(self);

	self->solid = SOLID_BBOX;
	self->movetype = MOVETYPE_STEP;
	self->svflags &= ~SVF_NOCLIENT;
	self->air_finished = level.time + 12;
	gi.linkentity(self);

	monster_start_go(self);

	if (strcmp(self->classname, "monster_fixbot") == 0)
	{
		if (self->spawnflags & 16 || self->spawnflags & 8 || self->spawnflags &
			4)
		{
			self->enemy = NULL;
			return;
		}
	}

	if (self->enemy && !(self->spawnflags & 1) &&
		!(self->enemy->flags & FL_NOTARGET))
	{
		FoundTarget(self);
	}
	else
	{
		self->enemy = NULL;
	}
}

void
monster_triggered_spawn_use(edict_t *self, edict_t *other /* unused */, edict_t *activator)
{
	if (!self || !activator)
	{
		return;
	}

	/* we have a one frame delay here so we
	   don't telefrag the guy who activated us */
	self->think = monster_triggered_spawn;
	self->nextthink = level.time + FRAMETIME;

	if (activator->client)
	{
		self->enemy = activator;
	}

	self->use = monster_use;
}

void
monster_triggered_start(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	self->solid = SOLID_NOT;
	self->movetype = MOVETYPE_NONE;
	self->svflags |= SVF_NOCLIENT;
	self->nextthink = 0;
	self->use = monster_triggered_spawn_use;
}

/*
 * When a monster dies, it fires all of its
 * targets with the current enemy as activator.
 */
void
monster_death_use(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	self->flags &= ~(FL_FLY | FL_SWIM);
	self->monsterinfo.aiflags &= AI_GOOD_GUY;

	if (self->item)
	{
		Drop_Item(self, self->item);
		self->item = NULL;
	}

	if (self->deathtarget)
	{
		self->target = self->deathtarget;
	}

	if (!self->target)
	{
		return;
	}

	G_UseTargets(self, self->enemy);
}

qboolean
monster_start(edict_t *self)
{
  	if (!self)
	{
		return false;
	}

	if (deathmatch->value)
	{
		G_FreeEdict(self);
		return false;
	}

	if ((self->spawnflags & 4) && !(self->monsterinfo.aiflags & AI_GOOD_GUY))
	{
		self->spawnflags &= ~4;
		self->spawnflags |= 1;
	}

	if (!(self->monsterinfo.aiflags & AI_GOOD_GUY))
	{
		level.total_monsters++;
	}

	self->nextthink = level.time + FRAMETIME;
	self->svflags |= SVF_MONSTER;
	self->s.renderfx |= RF_FRAMELERP;
	self->takedamage = DAMAGE_AIM;
	self->air_finished = level.time + 12;
	self->use = monster_use;

	if(!self->max_health)
	{
		self->max_health = self->health;
	}

	self->clipmask = MASK_MONSTERSOLID;

	self->s.skinnum = 0;
	self->deadflag = DEAD_NO;
	self->svflags &= ~SVF_DEADMONSTER;

	if (!self->monsterinfo.checkattack)
	{
		self->monsterinfo.checkattack = M_CheckAttack;
	}

	VectorCopy(self->s.origin, self->s.old_origin);

	if (st.item)
	{
		self->item = FindItemByClassname(st.item);

		if (!self->item)
		{
			gi.dprintf("%s at %s has bad item: %s\n", self->classname,
					vtos(self->s.origin), st.item);
		}
	}

	/* randomize what frame they start on */
	if (self->monsterinfo.currentmove)
	{
		self->s.frame = self->monsterinfo.currentmove->firstframe +
						(rand() %
						 (self->monsterinfo.currentmove->lastframe -
		 self->monsterinfo.currentmove->firstframe + 1));
	}

	return true;
}

void
monster_start_go(edict_t *self)
{
	vec3_t v;

  	if (!self)
	{
		return;
	}

	if (self->health <= 0)
	{
		return;
	}

	/* check for target to combat_point
	   and change to combattarget */
	if (self->target)
	{
		qboolean notcombat;
		qboolean fixup;
		edict_t *target;

		target = NULL;
		notcombat = false;
		fixup = false;

		while ((target = G_Find(target, FOFS(targetname), self->target)) != NULL)
		{
			if (strcmp(target->classname, "point_combat") == 0)
			{
				self->combattarget = self->target;
				fixup = true;
			}
			else
			{
				notcombat = true;
			}
		}

		if (notcombat && self->combattarget)
		{
			gi.dprintf("%s at %s has target with mixed types\n",
					self->classname, vtos(self->s.origin));
		}

		if (fixup)
		{
			self->target = NULL;
		}
	}

	/* validate combattarget */
	if (self->combattarget)
	{
		edict_t *target;

		target = NULL;

		while ((target = G_Find(target, FOFS(targetname),
							self->combattarget)) != NULL)
		{
			if (strcmp(target->classname, "point_combat") != 0)
			{
				gi.dprintf("%s at (%i %i %i) has a bad combattarget %s : %s at (%i %i %i)\n",
						self->classname, (int)self->s.origin[0], (int)self->s.origin[1],
						(int)self->s.origin[2], self->combattarget, target->classname,
						(int)target->s.origin[0], (int)target->s.origin[1],
						(int)target->s.origin[2]);
			}
		}
	}

	if (self->target)
	{
		self->goalentity = self->movetarget = G_PickTarget(self->target);

		if (!self->movetarget)
		{
			gi.dprintf("%s can't find target %s at %s\n", self->classname,
					self->target, vtos(self->s.origin));
			self->target = NULL;
			self->monsterinfo.pausetime = 100000000;
			self->monsterinfo.stand(self);
		}
		else if (strcmp(self->movetarget->classname, "path_corner") == 0)
		{
			VectorSubtract(self->goalentity->s.origin, self->s.origin, v);
			self->ideal_yaw = self->s.angles[YAW] = vectoyaw(v);
			self->monsterinfo.walk(self);
			self->target = NULL;
		}
		else
		{
			self->goalentity = self->movetarget = NULL;
			self->monsterinfo.pausetime = 100000000;
			self->monsterinfo.stand(self);
		}
	}
	else
	{
		self->monsterinfo.pausetime = 100000000;
		self->monsterinfo.stand(self);
	}

	self->think = monster_think;
	self->nextthink = level.time + FRAMETIME;
}

void
walkmonster_start_go(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	if (!(self->spawnflags & 2) && (level.time < 1))
	{
		M_droptofloor(self);

		if (self->groundentity)
		{
			if (!M_walkmove(self, 0, 0))
			{
				gi.dprintf("%s in solid at %s\n", self->classname,
						vtos(self->s.origin));
			}
		}
	}

	if (!self->yaw_speed)
	{
		self->yaw_speed = 20;
	}

	self->viewheight = 25;

	monster_start_go(self);

	if (self->spawnflags & 2)
	{
		monster_triggered_start(self);
	}
}

void
walkmonster_start(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	self->think = walkmonster_start_go;
	monster_start(self);
}

void
flymonster_start_go(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	if (!M_walkmove(self, 0, 0))
	{
		gi.dprintf("%s in solid at %s\n", self->classname, vtos(self->s.origin));
	}

	if (!self->yaw_speed)
	{
		self->yaw_speed = 10;
	}

	self->viewheight = 25;

	monster_start_go(self);

	if (self->spawnflags & 2)
	{
		monster_triggered_start(self);
	}
}

void
flymonster_start(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	self->flags |= FL_FLY;
	self->think = flymonster_start_go;
	monster_start(self);
}

void
swimmonster_start_go(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	if (!self->yaw_speed)
	{
		self->yaw_speed = 10;
	}

	self->viewheight = 10;

	monster_start_go(self);

	if (self->spawnflags & 2)
	{
		monster_triggered_start(self);
	}
}

void
swimmonster_start(edict_t *self)
{
  	if (!self)
	{
		return;
	}

	self->flags |= FL_SWIM;
	self->think = swimmonster_start_go;
	monster_start(self);
}

