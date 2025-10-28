/* =======================================================================
 *
 * The player trail, used by monsters to locate the player.
 *
 * =======================================================================
 */

#include "../header/local.h"

/*
 * This is a circular list containing the a list of points of where
 * the player has been recently.  It is used by monsters for pursuit.
 *
 * .origin		the spot
 * .owner		forward link
 * .aiment		backward link
 */

#define NEXT(n) (((n) + 1) & (TRAIL_LENGTH - 1))
#define PREV(n) (((n) - 1) & (TRAIL_LENGTH - 1))

#define TRAIL_LENGTH 8

edict_t *trail[TRAIL_LENGTH];
int trail_head;
qboolean trail_active = false;

void
PlayerTrail_Init(void)
{
	int n;

	if (deathmatch->value)
	{
		return;
	}

	for (n = 0; n < TRAIL_LENGTH; n++)
	{
		trail[n] = G_Spawn();
		trail[n]->classname = "player_trail";
	}

	trail_head = 0;
	trail_active = true;
}

void
PlayerTrail_Add(vec3_t spot)
{
	vec3_t temp;

	if (!trail_active)
	{
		return;
	}

	VectorCopy(spot, trail[trail_head]->s.origin);

	trail[trail_head]->timestamp = level.time;

	VectorSubtract(spot, trail[PREV(trail_head)]->s.origin, temp);
	trail[trail_head]->s.angles[1] = vectoyaw(temp);

	trail_head = NEXT(trail_head);
}

void
PlayerTrail_New(vec3_t spot)
{
	if (!trail_active)
	{
		return;
	}

	PlayerTrail_Init();
	PlayerTrail_Add(spot);
}

edict_t *
PlayerTrail_PickFirst(edict_t *self)
{
	int marker;
	int n;

	if (!self)
	{
		return NULL;
	}

	if (!trail_active)
	{
		return NULL;
	}

	for (marker = trail_head, n = TRAIL_LENGTH; n; n--)
	{
		if (trail[marker]->timestamp <= self->monsterinfo.trail_time)
		{
			marker = NEXT(marker);
		}
		else
		{
			break;
		}
	}

	if (visible(self, trail[marker]))
	{
		return trail[marker];
	}

	if (visible(self, trail[PREV(marker)]))
	{
		return trail[PREV(marker)];
	}

	return trail[marker];
}

edict_t *
PlayerTrail_PickNext(edict_t *self)
{
	int marker;
	int n;

	if (!self)
	{
		return NULL;
	}

	if (!trail_active)
	{
		return NULL;
	}

	for (marker = trail_head, n = TRAIL_LENGTH; n; n--)
	{
		if (trail[marker]->timestamp <= self->monsterinfo.trail_time)
		{
			marker = NEXT(marker);
		}
		else
		{
			break;
		}
	}

	return trail[marker];
}

edict_t *
PlayerTrail_LastSpot(void)
{
	return trail[PREV(trail_head)];
}
