#include "../header/local.h"
#include "../header/player.h"
#include "../header/bot.h"

void droptofloor (edict_t *ent);

edict_t *bot_team_flag1;
edict_t *bot_team_flag2;

void SetBotFlag1(edict_t *ent)	//チーム1の旗
{
	bot_team_flag1 = ent;
}
void SetBotFlag2(edict_t *ent)  //チーム2の旗
{
	bot_team_flag2 = ent;
}
edict_t *GetBotFlag1(void)	//チーム1の旗
{
	return bot_team_flag1;
}
edict_t *GetBotFlag2(void)  //チーム2の旗
{
	return bot_team_flag2;
}

qboolean ChkTFlg( void )
{
	if(bot_team_flag1 != NULL 
		&& bot_team_flag2 != NULL) return true;
	else return false;
}

void SpawnItem2 (edict_t *ent, gitem_t *item)
{
//	PrecacheItem (item);

	ent->item = item;
	ent->nextthink = level.time ;    // items start after other solids
	ent->think = droptofloor;
	ent->s.effects = 0;
	ent->s.renderfx = RF_GLOW;
//	if (ent->model)
//		gi.modelindex (ent->model);
	droptofloor (ent);
	ent->s.modelindex = 0;
	ent->nextthink = level.time + 100 * FRAMETIME;    // items start after other solids
	ent->think = G_FreeEdict;
}

//=====================================

//
// BOT用可視判定
//

qboolean Bot_trace (edict_t *ent,edict_t *other)
{
		trace_t		rs_trace;
		vec3_t	ttx;
		vec3_t	tty;

		VectorCopy (ent->s.origin,ttx);
		VectorCopy (other->s.origin,tty);
		if(ent->maxs[2] >=32)
		{
			if(tty[2] > ttx[2] ) tty[2] += 16;
//			else if(ttx[2] > tty[2] > 100 ) tty[2] += 32;
			ttx[2] += 30;
		}
		else
		{
			ttx[2] -= 12;
		}

		rs_trace = gi.trace (ttx, NULL, NULL, tty ,ent, CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_LAVA | CONTENTS_SLIME /*| CONTENTS_TRANSLUCENT*/);
		if(rs_trace.fraction == 1.0 && !rs_trace.allsolid && !rs_trace.startsolid) return true;
		if( ent->maxs[2] < 32 ) return false;

		if(other->classname[6] == 'F'
			|| other->classname[0] == 'w')
		{}
		else if(other->classname[0]=='i')
		{
			if(other->classname[5]=='q'
			|| other->classname[5]=='f'
			|| other->classname[5]=='t'
			|| other->classname[5]=='i'
			|| other->classname[5]=='h'
			|| other->classname[5]=='a'){}
			else return false;
		}
		else return false;

		if(rs_trace.ent != NULL)
		{
			if(rs_trace.ent->classname[0] == 'f' 
				&& rs_trace.ent->classname[5] == 'd'
				&& rs_trace.ent->targetname == NULL) return true;  
		}
		
		if(ent->s.origin[2] < other->s.origin[2]
			|| ent->s.origin[2]-24 >  other->s.origin[2]) return false;
		
		ttx[2] -= 36;
		rs_trace = gi.trace (ttx, NULL, NULL, other->s.origin ,ent, CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_LAVA | CONTENTS_SLIME /*|CONTENTS_TRANSLUCENT*/);
		if(rs_trace.fraction == 1.0 && !rs_trace.allsolid && !rs_trace.startsolid) return true;
		return false;
}


qboolean Bot_traceX (edict_t *ent,edict_t *other)
{
		trace_t		rs_trace;
		vec3_t	ttx,tty;
		VectorCopy (ent->s.origin,ttx);
		VectorCopy (other->s.origin,tty);
		ttx[2] += 16;
		tty[2] += 16;

		rs_trace = gi.trace (ttx, NULL, NULL, tty ,ent, CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_LAVA | CONTENTS_SLIME);
		if(rs_trace.fraction == 1.0 ) return true;
		return false;
}

qboolean Bot_traceY (edict_t *ent,edict_t *other)
{
		trace_t		rs_trace;
		vec3_t	ttx,tty;
		VectorCopy (ent->s.origin,ttx);
		VectorCopy (other->s.origin,tty);
		if(ent->maxs[2] >=32) ttx[2] += 24;
		else ttx[2] -= 12;
		
		tty[2] += 16;

		rs_trace = gi.trace (ttx, NULL, NULL, tty ,ent, CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_LAVA | CONTENTS_SLIME);
		if(rs_trace.fraction == 1.0 ) return true;
		return false;
}

//
// BOT用可視判定 2
//

qboolean Bot_trace2 (edict_t *ent,vec3_t ttz)
{
		trace_t		rs_trace;
		vec3_t	ttx;
		VectorCopy (ent->s.origin,ttx);
		if(ent->maxs[2] >=32) ttx[2] += 24;
		else ttx[2] -= 12;

		rs_trace = gi.trace (ttx, NULL, NULL, ttz ,ent, CONTENTS_SOLID | CONTENTS_LAVA | CONTENTS_SLIME /*| CONTENTS_TRANSLUCENT*/);
		if(rs_trace.fraction != 1.0 ) return false;
		return true;
}

//
// BOT用可視判定 3
//

qboolean Bot_traceS (edict_t *ent,edict_t *other)
{
	trace_t		rs_trace;
	vec3_t	start,end;
	int		mycont;


	VectorCopy(ent->s.origin,start);
	VectorCopy(other->s.origin,end);

	start[2] += ent->viewheight - 8;
	end[2] += other->viewheight - 8;

	if(Bot[ent->client->zc.botindex].param[BOP_NOSTHRWATER]) goto WATERMODE;

	rs_trace = gi.trace (start, NULL, NULL, end ,ent,CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_LAVA | CONTENTS_SLIME);

	if(rs_trace.fraction != 1.0 ) return false;
	return true;

WATERMODE:
	mycont = gi.pointcontents(start);

	if((mycont & CONTENTS_WATER) && !other->waterlevel)
	{
		rs_trace = gi.trace (end, NULL, NULL, start ,ent,CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER);
		if(rs_trace.surface)
		{
			if(rs_trace.surface->flags & SURF_WARP) return false;
		}
		rs_trace = gi.trace (start, NULL, NULL, end ,ent,CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_LAVA | CONTENTS_SLIME);
		if(rs_trace.fraction != 1.0 ) return false;
		return true;
	}
	else if((mycont & CONTENTS_WATER) && other->waterlevel)
	{
		VectorCopy(other->s.origin,end);
		end[2] -= 16;
		rs_trace = gi.trace (start, NULL, NULL, end ,ent,CONTENTS_SOLID | CONTENTS_WINDOW );
		if(rs_trace.fraction != 1.0 ) return false;	
		return true;
	}

	if(other->waterlevel)
	{
		VectorCopy(other->s.origin,end);
		end[2] += 32;
		rs_trace = gi.trace (start, NULL, NULL, end ,ent,CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_WATER);
		if(rs_trace.surface)
		{
			if(rs_trace.surface->flags & SURF_WARP) return false;
		}		
//		if(rs_trace.fraction != 1.0 ) return false;	
//		return true;
	}

	rs_trace = gi.trace (start, NULL, NULL, end ,ent,CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_LAVA | CONTENTS_SLIME);
	if(rs_trace.fraction != 1.0 ) return false;
	return true;
}




//
// VEC値からyawを得る
//

float Get_yaw (vec3_t vec)
{
		vec3_t		out;
		double		yaw;

		VectorCopy(vec,out);
		out[2] = 0;
		VectorNormalize2 (out, out);

		yaw = acos((double) out[0]);
	//	yaw = (float) yaw;
		yaw = yaw / M_PI * 180;

		if(asin((double) out[1]) < 0 ) yaw *= -1;

		return (float)yaw;
}

float Get_pitch (vec3_t vec)
{
		vec3_t		out;
		float		pitch;

		VectorNormalize2 (vec, out);

		pitch = acos((double) out[2]);
	//	yaw = (float) yaw;
		pitch = ((float)pitch) / M_PI * 180;

//		if(asin((double) out[0]) < 0 ) pitch *= -1;

		pitch -= 90;
		if(pitch < -180) pitch += 360;

		return pitch;
}

//
// VEC値とyaw値の角度差を得る
//

float Get_vec_yaw (vec3_t vec,float yaw)
{
		float		vecsyaw;

		vecsyaw = Get_yaw (vec);

		if(vecsyaw > yaw) vecsyaw -= yaw;
		else vecsyaw = yaw - vecsyaw;

		if(vecsyaw >180 ) vecsyaw = 360 - vecsyaw;

		return vecsyaw;
}

//yaw に対するvecの相対
float Get_vec_yaw2 (vec3_t vec,float yaw)
{
		float		vecsyaw;

		vecsyaw = Get_yaw (vec);

		vecsyaw -= yaw;
		if(vecsyaw >180 ) vecsyaw -= 360;
		else if(vecsyaw <-180 ) vecsyaw += 360;

		return vecsyaw;
}

