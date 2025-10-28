#include "../header/local.h"

#define Z_RADUISLISTSIZE	2000

void ai_run_melee(edict_t *self);
qboolean FindTarget (edict_t *self);
qboolean SV_StepDirection (edict_t *ent, float yaw, float dist);
void SV_NewChaseDir (edict_t *actor, vec3_t eOrigin, float dist);

/*
=============
zSchoolAllVisiable

Creates a list of all entities in the raduis of Z_RADUISLISTSIZE
==============
*/
void zCreateRaduisList(edict_t *self)
{
	edict_t *head, *list;
	vec3_t vec;

	if (!self)
	{
		return;
	}

	if(self->zRaduisList)
	{
		// already created for this think, don't bother doing it again...
		return;
	}

	head = NULL;
	list = self;

	while(1)
	{
		head = findradius(head, self->s.origin, Z_RADUISLISTSIZE);
		if(head == NULL)
			break;

		if(head != self)
		{
			list->zRaduisList = head;
			VectorSubtract(self->s.origin, head->s.origin, vec);
			head->zDistance = VectorLength(vec);
			list = head;
		}
	}

	list->zRaduisList = NULL;
};

/*
=============
zSchoolAllVisiable

Create list of monsters of the same schooling type that are ahead of you. 
==============
*/
int zSchoolAllVisiable(edict_t *self)
{
	int max;
	edict_t *head, *list;

	if (!self)
	{
		return 0;
	}

	max = 0;

	zCreateRaduisList(self);
	head = self->zRaduisList;
	list = self;

	while (head)
	{
		if(strcmp(head->classname, self->classname) == 0 && (self->monsterinfo.aiflags & AI_SCHOOLING) && (head->health > 0) && 
			(head->zDistance <= self->monsterinfo.zSchoolSightRadius) && (visible(self, head)) && (infront(self, head)))
		{
			list->zSchoolChain = head;
			list = head;
			max++;
		}
		head = head->zRaduisList;
	}

	list->zSchoolChain = NULL;

	return max;
}

/*
=============
zFindRoamYaw

Check direction moving in does not hit a wall... if it does change direction.
==============
*/
int zFindRoamYaw(edict_t *self, float distcheck)
{
	vec3_t	forward, end, angles;
	trace_t	tr;
	float current;

	if (!self)
	{
		return 0;
	}

	current = anglemod(self->s.angles[YAW]);

	if(current <= self->ideal_yaw - 1 || current > self->ideal_yaw + 1)
	{
		if(fabs(current - self->ideal_yaw) <= 359.0)
		{
			return 0;
		}
	}

	AngleVectors (self->s.angles, forward, NULL, NULL);
	VectorMA (self->s.origin, distcheck, forward, end);

	tr = gi.trace (self->s.origin, self->mins, self->maxs, end, self, MASK_SOLID);

	if (tr.fraction < 1.0)
	{
		if(random() > 0.75)
		{
			self->ideal_yaw = vectoyaw(forward);
			self->ideal_yaw = self->ideal_yaw + 180;
		}
		else
		{
			float dir = random() > 0.5 ? -45 : 45;
			float maxtrys = 100;

			VectorCopy(self->s.angles, angles);

			while(tr.fraction < 1.0 && maxtrys)
			{
				// blocked, change ideal yaw...
				self->ideal_yaw = vectoyaw(forward);
				self->ideal_yaw = self->ideal_yaw + (random() * dir);

				angles[YAW] = anglemod (self->ideal_yaw);
				AngleVectors (angles, forward, NULL, NULL);
				VectorMA (self->s.origin, distcheck, forward, end);

				tr = gi.trace (self->s.origin, self->mins, self->maxs, end, self, MASK_SOLID);
				maxtrys--;
			}
		}

		return 1;
	}

	return 0;
};

/*
=============
zSchoolMonsters

Roaming schooling ai. 
==============
*/
int zSchoolMonsters(edict_t *self, float dist, int runStyle, float *currentSpeed)
{
	int maxInsight;
	int newRunStyle;

	if (!self)
	{
		return 0;
	}

	maxInsight = zSchoolAllVisiable(self);

	// If you're not out in front
	if(maxInsight > 0)
	{
		float totalSpeed;
		float totalBearing;
		float distanceToNearest, distanceToLeader, dist;
		edict_t *nearestEntity = 0, *list;
		vec3_t vec;

		totalSpeed = 0;
		totalBearing = 0;
		distanceToNearest = 10000;
		distanceToLeader = 0;
		list = self->zSchoolChain;

		while(list)
		{
			// Gather data on those you see
			totalSpeed += list->speed;
			totalBearing += anglemod(list->s.angles[YAW]);

			VectorSubtract(self->s.origin, list->s.origin, vec);
			dist = VectorLength(vec);

			if(dist < distanceToNearest)
			{
				distanceToNearest = dist;
				nearestEntity = list;
			}

			if(dist > distanceToLeader)
			{
				distanceToLeader = dist;
			}

			list = list->zSchoolChain;
		}

		// Rule 1) Match average speed of those in the list
		self->speed = (totalSpeed / maxInsight) * 1.5;

		// Rule 2) Move towards the perceived center of gravity of the herd
		self->ideal_yaw = totalBearing / maxInsight;

		// check if hitting something
		if(!zFindRoamYaw(self, 10))
		{
			// Rule 3) Maintain a minimum distance from those around you
			if(distanceToNearest <= self->monsterinfo.zSchoolMinimumDistance)
			{
				self->ideal_yaw = nearestEntity->s.angles[YAW];
				self->speed = nearestEntity->speed;
			}
		}

	}
	else
	{	
		//You are in front, so slow down a bit
		edict_t *head;

		self->speed = (self->speed * self->monsterinfo.zSchoolDecayRate);

		// check direction
		zFindRoamYaw(self, 100);

		// change directions of the monsters following you...
		zCreateRaduisList(self);
		head = self->zRaduisList;

		while (head)
		{
			if(strcmp(head->classname, self->classname) == 0 && (head->health > 0) && 
			(head->zDistance <= self->monsterinfo.zSchoolSightRadius) && (visible(self, head)))

			{
				head->ideal_yaw = self->ideal_yaw + (-20 +  (random() * 40));
			}
			head = head->zRaduisList;
		}
	}

	if(self->speed > self->monsterinfo.zSchoolMaxSpeed)
	{
		self->speed = self->monsterinfo.zSchoolMaxSpeed;
	}

	if(self->speed < self->monsterinfo.zSchoolMinSpeed)
	{
		self->speed = self->monsterinfo.zSchoolMinSpeed;
	}

	if(self->speed <= self->monsterinfo.zSpeedStandMax)
	{
		newRunStyle = 0;

		if(newRunStyle != runStyle)
		{
			*currentSpeed = 1;
		}
		else
		{
			*currentSpeed = (self->speed - self->monsterinfo.zSchoolMinSpeed) + 1;
		}
	}
	else if(self->speed <= self->monsterinfo.zSpeedWalkMax)
	{
		newRunStyle = 1;

		if(newRunStyle  != runStyle)
		{
			*currentSpeed = 1;
		}
		else
		{
			*currentSpeed = (self->speed - self->monsterinfo.zSpeedStandMax) + 1;
		}
	}
	else 
	{
		newRunStyle = 2;

		if(newRunStyle  != runStyle)
		{
			*currentSpeed = 1;
		}
		else
		{
			*currentSpeed = (self->speed - self->monsterinfo.zSpeedWalkMax) + 1;
		}
	}

	return newRunStyle;
}

/*
=============
ai_schoolStand

Used for standing around and looking for players / schooling monsters of the same type.
Distance is for slight position adjustments needed by the animations
==============
*/
void ai_schoolStand (edict_t *self, float dist)
{
	float speed;

	if (!self)
	{
		return;
	}

	if(!(self->monsterinfo.aiflags & AI_SCHOOLING))
	{
		ai_stand(self, dist);
		return;
	}

	// init school var's for this frame
	self->zRaduisList = NULL;

	if(self->enemy || FindTarget(self))
	{
		ai_stand(self, dist);
		return;
	}
	else
	{
		// run schooling routines
		switch(zSchoolMonsters(self, dist, 0, &speed))
		{
			case 1:
				self->monsterinfo.walk (self);
				break;

			case 2:
				self->monsterinfo.run (self);
				break;
		}
	}

	// do the normal stand stuff
	if (dist)
	M_walkmove (self, self->ideal_yaw, dist);
}

/*
=============
ai_schoolRun

The monster has an enemy it is trying to kill
=============
*/
void ai_schoolRun (edict_t *self, float dist)
{
	float speed;

	if (!self)
	{
		return;
	}

	if(!(self->monsterinfo.aiflags & AI_SCHOOLING))
	{
		ai_run(self, dist);
		return;
	}

	// init school var's for this frame
	self->zRaduisList = NULL;

	if(self->enemy || FindTarget(self))
	{
		ai_run(self, dist);
		return;
	}
	else
	{
		// run schooling routines
		switch(zSchoolMonsters(self, dist, 2, &speed))
		{
			case 0:
				self->monsterinfo.stand (self);
				break;

			case 1:
				self->monsterinfo.walk (self);
				break;
		}
	}

	// do the normal run stuff
	SV_StepDirection (self, self->ideal_yaw, dist);
}

/*
=============
ai_schoolWalk

The monster is walking it's beat
=============
*/
void ai_schoolWalk (edict_t *self, float dist)
{
	float speed;

	if (!self)
	{
		return;
	}

	if(!(self->monsterinfo.aiflags & AI_SCHOOLING))
	{
		ai_walk(self, dist);
		return;
	}

	// init school var's for this frame
	self->zRaduisList = NULL;

	if(self->enemy || FindTarget(self))
	{
		ai_walk(self, dist);
		return;
	}
	else
	{
		// run schooling routines
		switch(zSchoolMonsters(self, dist, 1, &speed))
		{
			case 0:
				self->monsterinfo.stand (self);
				break;

			case 2:
				self->monsterinfo.run (self);
				break;
		}
	}

	// do the normal walk stuff
	SV_StepDirection (self, self->ideal_yaw, dist);
}

/*
=============
ai_schoolCharge

Turns towards target and advances
Use this call with a distnace of 0 to replace ai_face
==============
*/
void ai_schoolCharge (edict_t *self, float dist)
{
	if (!self)
	{
		return;
	}

	ai_charge(self, dist);
}
