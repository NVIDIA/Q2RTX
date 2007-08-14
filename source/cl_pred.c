/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "cl_local.h"


/*
===================
CL_CheckPredictionError
===================
*/
void CL_CheckPredictionError( void ) {
	int		frame;
	int		delta[3];
	int		i;
	int		len;
	player_state_t *ps;

	if( cls.demoplayback ) {
		return;
	}

	if( sv_paused->integer ) {
		VectorClear( cl.prediction_error );
		return;
	}

	ps = &cl.frame.ps;

	if( !cl_predict->integer || ( ps->pmove.pm_flags & PMF_NO_PREDICTION ) )
		return;

	// calculate the last usercmd_t we sent that the sv_local.has processed
	frame = cls.netchan->incoming_acknowledged & CMD_MASK;
	i = cl.history[frame].cmdNumber & CMD_MASK;

	// compare what the server returned with what we had predicted it to be
	VectorSubtract( ps->pmove.origin, cl.predicted_origins[i], delta );

	// save the prediction error for interpolation
	len = abs( delta[0] ) + abs( delta[1] ) + abs( delta[2] );
	if( len > 640 ) {	// 80 world units
		// a teleport or something
		VectorClear( cl.prediction_error );
	} else {
		if( cl_showmiss->integer && ( delta[0] || delta[1] || delta[2] ) ) {
			Com_Printf( "prediction miss on %i: %i\n", cl.frame.number, len );
		}

		VectorCopy( ps->pmove.origin, cl.predicted_origins[i] );

		// save for error interpolation
		cl.prediction_error[0] = delta[0] * 0.125f;
		cl.prediction_error[1] = delta[1] * 0.125f;
		cl.prediction_error[2] = delta[2] * 0.125f;
	}
}

void CL_BuildSolidList( void ) {
	int i, num;
	entity_state_t	*ent;

	cl.numSolidEntities = 0;
	for( i = 0; i < cl.frame.numEntities; i++ ) {
		num = ( cl.frame.firstEntity + i ) & PARSE_ENTITIES_MASK;
		ent = &cl.entityStates[num];

		if( ent->number == cl.frame.clientNum + 1 ) {
			continue;
		}
		if( !ent->solid ) {
			continue;
		}
		
		cl.solidEntities[cl.numSolidEntities++] = ent;
	}
}


/*
====================
CL_ClipMoveToEntities

====================
*/
void CL_ClipMoveToEntities ( vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, trace_t *tr )
{
	int			i, x, zd, zu;
	trace_t		trace;
	cnode_t		*headnode;
	float		*angles;
	entity_state_t	*ent;
	cmodel_t		*cmodel;
	vec3_t		bmins, bmaxs;

	for (i=0 ; i<cl.numSolidEntities ; i++)
	{
		ent = cl.solidEntities[i];

		if (ent->solid == 31)
		{	// special value for bmodel
			cmodel = cl.model_clip[ent->modelindex];
			if (!cmodel)
				continue;
			headnode = cmodel->headnode;
			angles = ent->angles;
		}
		else
		{	// encoded bbox
			x = 8*(ent->solid & 31);
			zd = 8*((ent->solid>>5) & 31);
			zu = 8*((ent->solid>>10) & 63) - 32;

			bmins[0] = bmins[1] = -x;
			bmaxs[0] = bmaxs[1] = x;
			bmins[2] = -zd;
			bmaxs[2] = zu;

			headnode = CM_HeadnodeForBox (bmins, bmaxs);
			angles = vec3_origin;	// boxes don't rotate
		}

		if (tr->allsolid)
			return;

		CM_TransformedBoxTrace (&trace, start, end,
			mins, maxs, headnode,  MASK_PLAYERSOLID,
			ent->origin, angles);

		tr->allsolid |= trace.allsolid;
		tr->startsolid |= trace.startsolid;
		if( trace.fraction < tr->fraction ) {
			tr->fraction = trace.fraction;
            VectorCopy( trace.endpos, tr->endpos );
            tr->plane = trace.plane;
            tr->surface = trace.surface;
            tr->contents |= trace.contents;
			tr->ent = ( struct edict_s * )ent;
        }
	}
}


/*
================
CL_PMTrace
================
*/
trace_t		CL_PMTrace (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end)
{
	trace_t	t;

	// check against world
	CM_BoxTrace (&t, start, end, mins, maxs, cl.cm.cache->nodes, MASK_PLAYERSOLID);
	if (t.fraction < 1.0)
		t.ent = (struct edict_s *)1;

	// check all other solid models
	CL_ClipMoveToEntities (start, mins, maxs, end, &t);

	return t;
}

int		CL_PMpointcontents (vec3_t point)
{
	int			i;
	entity_state_t	*ent;
	cmodel_t		*cmodel;
	int			contents;

	contents = CM_PointContents (point, cl.cm.cache->nodes);

	for (i=0 ; i<cl.numSolidEntities ; i++)
	{
		ent = cl.solidEntities[i];

		if (ent->solid != 31) // special value for bmodel
			continue;

		cmodel = cl.model_clip[ent->modelindex];
		if (!cmodel)
			continue;

		contents |= CM_TransformedPointContents (point, cmodel->headnode, ent->origin, ent->angles);
	}

	return contents;
}


/*
=================
CL_PredictMovement

Sets cl.predicted_origin and cl.predicted_angles
=================
*/
void CL_PredictMovement( void ) {
	int			ack, current;
	int			frame;
	pmove_t		pm;
	int			step, oldz;
	player_state_t *ps;

	if( cls.state != ca_active ) {
		return;
	}

	if( cls.demoplayback ) {
		return;
	}

	if( sv_paused->integer ) {
		return;
	}

	ps = &cl.frame.ps;

	if( !cl_predict->integer || ( ps->pmove.pm_flags & PMF_NO_PREDICTION ) ) {
		// just set angles
		cl.predicted_angles[0] = cl.viewangles[0] + SHORT2ANGLE( ps->pmove.delta_angles[0] );
		cl.predicted_angles[1] = cl.viewangles[1] + SHORT2ANGLE( ps->pmove.delta_angles[1] );
		cl.predicted_angles[2] = cl.viewangles[2] + SHORT2ANGLE( ps->pmove.delta_angles[2] );
		return;
	}

	ack = cl.history[cls.netchan->incoming_acknowledged & CMD_MASK].cmdNumber;
	current = cl.cmdNumber;

	// if we are too far out of date, just freeze
	if( current - ack > CMD_BACKUP - 1 ) {
		if( cl_showmiss->integer ) {
			Com_Printf( "%i: exceeded CMD_BACKUP\n", cl.frame.number );
		}
		return;	
	}

	if( !cl_async->integer && current == ack ) {
		if( cl_showmiss->integer ) {
			Com_Printf( "%i: not moved\n", cl.frame.number );
		}
		return;
	}

	// copy current state to pmove
	memset( &pm, 0, sizeof( pm ) );
	pm.trace = CL_PMTrace;
	pm.pointcontents = CL_PMpointcontents;

	pm.s = ps->pmove;
#if USE_SMOOTH_DELTA_ANGLES
    VectorCopy( cl.delta_angles, pm.s.delta_angles );
#endif

	// run frames
	while( ++ack <= current ) {	
		pm.cmd = cl.cmds[ack & CMD_MASK];
		Pmove( &pm, &cl.pmp );

		// save for debug checking
		VectorCopy( pm.s.origin, cl.predicted_origins[ack & CMD_MASK] );
	}

	// run pending cmd
	if( cl_async->integer ) {
		if( !cl.cmd.msec ) {
			if( cl_showmiss->integer ) {
				Com_Printf( "%i: not moved\n", cl.frame.number );
			}
			goto finish;
		}
		pm.cmd = cl.cmd;
		pm.cmd.forwardmove = cl.move[0];
		pm.cmd.sidemove = cl.move[1];
		pm.cmd.upmove = cl.move[2];
		Pmove( &pm, &cl.pmp );
		frame = current;

		// save for debug checking
		VectorCopy( pm.s.origin, cl.predicted_origins[( current + 1 ) & CMD_MASK] );
	} else {
		frame = current- 1;
	}
	
	oldz = cl.predicted_origins[frame & CMD_MASK][2];
	step = pm.s.origin[2] - oldz;
	if( cl.predicted_step_frame != frame &&
		step > 63 && step < 160 &&
		( pm.s.pm_flags & PMF_ON_GROUND ) )
	{
		cl.predicted_step = step * 0.125;
		cl.predicted_step_time = cls.realtime - cls.frametime * 500;
		cl.predicted_step_frame = frame;
	}

finish:
	// copy results out for rendering
	VectorScale( pm.s.origin, 0.125f, cl.predicted_origin );
	VectorScale( pm.s.velocity, 0.125f, cl.predicted_velocity );
	VectorCopy( pm.viewangles, cl.predicted_angles );
	if( cl_showmiss->integer ) {
        Com_Printf("%f %f %f\n",cl.predicted_origin[0],
            cl.predicted_origin[1],cl.predicted_origin[2]);
    }
}

