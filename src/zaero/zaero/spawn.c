#include "../header/local.h"

void ED_CallSpawn (edict_t *ent);

edict_t *FindZSpawn(int i)
{
	edict_t *oldSpot = NULL;
	edict_t *spot = NULL;

	while(i)
	{
		spot = G_Find (oldSpot, FOFS(classname), "info_player_deathmatch");
		if (spot != NULL)
		{
			i--;
		}
		else if (oldSpot == NULL)
		{
			return NULL;
		}

		oldSpot = spot;
	}

	// make 1 last ditch effor
	if (!spot)
		spot = G_Find(NULL, FOFS(classname), "info_player_deathmatch");
	return spot;
}

qboolean SpawnZ(gitem_t *item, edict_t *spot)
{
	edict_t	*ent;
	vec3_t	forward;
	vec3_t  angles;
	vec3_t start;
	vec3_t end;
	trace_t tr;
	int ang = 0;
	int startAng = 0;

	if (!item || !spot)
	{
		return false;
	}

	ent = G_Spawn();

	ent->classname = item->classname;
	VectorSet (ent->mins, -15, -15, -15);
	VectorSet (ent->maxs, 15, 15, 15);
	ent->solid = SOLID_TRIGGER;
	ent->movetype = MOVETYPE_BOUNCE;
	ED_CallSpawn(ent);

	startAng = rand() % 360;
	VectorCopy(spot->s.origin, start);
	start[2] += 16;

	for (ang = startAng; ang < startAng + 360; ang += 15)
	{
		angles[0] = 0;
		angles[1] = ang;
		angles[2] = 0;

		AngleVectors (angles, forward, NULL, NULL);
		VectorMA(start, 128, forward, end);

		tr = gi.trace(start, ent->mins, ent->maxs, end, NULL, MASK_SHOT);
		if (tr.fraction < 1.0)
			continue;

		VectorCopy(end, ent->s.origin);
		gi.linkentity(ent);
		return true;
	}
	G_FreeEdict(ent);
	return false;
}

char *items[] = 
{
	"weapon_soniccannon",
	"weapon_sniperrifle",
	"weapon_flaregun",
	"ammo_ired",
	"ammo_a2k",
	"ammo_flares",
	"ammo_empnuke",
	"ammo_plasmashield",
	NULL
};

void Z_SpawnDMItems()
{
	char **ptr = NULL;
	int added = 0;
	int count = 1;
	
	// only in deathmatch
	if (!deathmatch->value)
		return;

	// only with the flag set
	if ((int)zdmflags->value & ZDM_ZAERO_ITEMS)
		return;

	// scan thru all the items looking for our items
	ptr = items;
	while (*ptr != NULL)
	{
		edict_t *e = G_Find(NULL, FOFS(classname), *ptr);
		if (e != NULL)
			return;

		ptr++;
	}
	
	// try to spawn 1 of each item near a deathmatch spot
	ptr = &items[0];
	while(*ptr != NULL)
	{
		int j = 0;
		gitem_t *i = NULL;
		edict_t *spot = NULL;

		i = FindItemByClassname(*ptr);
		ptr++;
		if (i == NULL)
			continue;

		for (j = 0; j < 4; j++)
		{
			spot = FindZSpawn(count++);
			if (spot == NULL)
				break;

			if (SpawnZ(i, spot))
			{
				added++;
				break;
			}
		}
	}
	gi.dprintf ("%i Zaero entities added\n", added);
}

