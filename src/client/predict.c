/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "client.h"
#include "common/x86/fpu.h"

/*
===================
CL_CheckPredictionError
===================
*/
void CL_CheckPredictionError(void)
{
    int         frame;
    int         delta[3];
    unsigned    cmd;
    int         len;

    if (!cls.netchan) {
        return;
    }

    if (sv_paused->integer) {
        VectorClear(cl.prediction_error);
        return;
    }

    if (!cl_predict->integer || (cl.frame.ps.pmove.pm_flags & PMF_NO_PREDICTION))
        return;

    // calculate the last usercmd_t we sent that the server has processed
    frame = cls.netchan->incoming_acknowledged & CMD_MASK;
    cmd = cl.history[frame].cmdNumber;

    // compare what the server returned with what we had predicted it to be
    VectorSubtract(cl.frame.ps.pmove.origin, cl.predicted_origins[cmd & CMD_MASK], delta);

    // save the prediction error for interpolation
    len = abs(delta[0]) + abs(delta[1]) + abs(delta[2]);
    if (len < 1 || len > 640) {
        // > 80 world units is a teleport or something
        VectorClear(cl.prediction_error);
        return;
    }

    SHOWMISS("prediction miss on %i: %i (%d %d %d)\n",
             cl.frame.number, len, delta[0], delta[1], delta[2]);

    // don't predict steps against server returned data
    if (cl.predicted_step_frame <= cmd)
        cl.predicted_step_frame = cmd + 1;

    VectorCopy(cl.frame.ps.pmove.origin, cl.predicted_origins[cmd & CMD_MASK]);

    // save for error interpolation
    VectorScale(delta, 0.125f, cl.prediction_error);
}

/*
====================
CL_ClipMoveToEntities

====================
*/
static void CL_ClipMoveToEntities(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, trace_t *tr)
{
    int         i;
    trace_t     trace;
    mnode_t     *headnode;
    centity_t   *ent;
    mmodel_t    *cmodel;

    for (i = 0; i < cl.numSolidEntities; i++) {
        ent = cl.solidEntities[i];

        if (ent->current.solid == PACKED_BSP) {
            // special value for bmodel
            cmodel = cl.model_clip[ent->current.modelindex];
            if (!cmodel)
                continue;
            headnode = cmodel->headnode;
        } else {
            headnode = CM_HeadnodeForBox(ent->mins, ent->maxs);
        }

        if (tr->allsolid)
            return;

        CM_TransformedBoxTrace(&trace, start, end,
                               mins, maxs, headnode,  MASK_PLAYERSOLID,
                               ent->current.origin, ent->current.angles);

        CM_ClipEntity(tr, &trace, (struct edict_s *)ent);
    }
}


/*
================
CL_PMTrace
================
*/
static trace_t q_gameabi CL_Trace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end)
{
    trace_t    t;

    // check against world
    CM_BoxTrace(&t, start, end, mins, maxs, cl.bsp->nodes, MASK_PLAYERSOLID);
    if (t.fraction < 1.0)
        t.ent = (struct edict_s *)1;

    // check all other solid models
    CL_ClipMoveToEntities(start, mins, maxs, end, &t);

    return t;
}

static int CL_PointContents(vec3_t point)
{
    int         i;
    centity_t   *ent;
    mmodel_t    *cmodel;
    int         contents;

    contents = CM_PointContents(point, cl.bsp->nodes);

    for (i = 0; i < cl.numSolidEntities; i++) {
        ent = cl.solidEntities[i];

        if (ent->current.solid != PACKED_BSP) // special value for bmodel
            continue;

        cmodel = cl.model_clip[ent->current.modelindex];
        if (!cmodel)
            continue;

        contents |= CM_TransformedPointContents(
                        point, cmodel->headnode,
                        ent->current.origin,
                        ent->current.angles);
    }

    return contents;
}

/*
=================
CL_PredictMovement

Sets cl.predicted_origin and cl.predicted_angles
=================
*/
void CL_PredictAngles(void)
{
    cl.predicted_angles[0] = cl.viewangles[0] + SHORT2ANGLE(cl.frame.ps.pmove.delta_angles[0]);
    cl.predicted_angles[1] = cl.viewangles[1] + SHORT2ANGLE(cl.frame.ps.pmove.delta_angles[1]);
    cl.predicted_angles[2] = cl.viewangles[2] + SHORT2ANGLE(cl.frame.ps.pmove.delta_angles[2]);
}

void CL_PredictMovement(void)
{
    unsigned    ack, current, frame;
    pmove_t     pm;
    int         step, oldz;

    if (cls.state != ca_active) {
        return;
    }

    if (cls.demo.playback) {
        return;
    }

    if (sv_paused->integer) {
        return;
    }

    if (!cl_predict->integer || (cl.frame.ps.pmove.pm_flags & PMF_NO_PREDICTION)) {
        // just set angles
        CL_PredictAngles();
        return;
    }

    ack = cl.history[cls.netchan->incoming_acknowledged & CMD_MASK].cmdNumber;
    current = cl.cmdNumber;

    // if we are too far out of date, just freeze
    if (current - ack > CMD_BACKUP - 1) {
        SHOWMISS("%i: exceeded CMD_BACKUP\n", cl.frame.number);
        return;
    }

    if (!cl.cmd.msec && current == ack) {
        SHOWMISS("%i: not moved\n", cl.frame.number);
        return;
    }

    X86_PUSH_FPCW;
    X86_SINGLE_FPCW;

    // copy current state to pmove
    memset(&pm, 0, sizeof(pm));
    pm.trace = CL_Trace;
    pm.pointcontents = CL_PointContents;

    pm.s = cl.frame.ps.pmove;
#if USE_SMOOTH_DELTA_ANGLES
    VectorCopy(cl.delta_angles, pm.s.delta_angles);
#endif

    // run frames
    while (++ack <= current) {
        pm.cmd = cl.cmds[ack & CMD_MASK];
        Pmove(&pm, &cl.pmp);

        // save for debug checking
        VectorCopy(pm.s.origin, cl.predicted_origins[ack & CMD_MASK]);
    }

    // run pending cmd
    if (cl.cmd.msec) {
        pm.cmd = cl.cmd;
        pm.cmd.forwardmove = cl.localmove[0];
        pm.cmd.sidemove = cl.localmove[1];
        pm.cmd.upmove = cl.localmove[2];
        Pmove(&pm, &cl.pmp);
        frame = current;

        // save for debug checking
        VectorCopy(pm.s.origin, cl.predicted_origins[(current + 1) & CMD_MASK]);
    } else {
        frame = current - 1;
    }

    X86_POP_FPCW;

    if (pm.s.pm_type != PM_SPECTATOR && (pm.s.pm_flags & PMF_ON_GROUND)) {
        oldz = cl.predicted_origins[cl.predicted_step_frame & CMD_MASK][2];
        step = pm.s.origin[2] - oldz;
        if (step > 63 && step < 160) {
            cl.predicted_step = step * 0.125f;
            cl.predicted_step_time = cls.realtime;
            cl.predicted_step_frame = frame + 1;    // don't double step
        }
    }

    if (cl.predicted_step_frame < frame) {
        cl.predicted_step_frame = frame;
    }

    // copy results out for rendering
    VectorScale(pm.s.origin, 0.125f, cl.predicted_origin);
    VectorScale(pm.s.velocity, 0.125f, cl.predicted_velocity);
    VectorCopy(pm.viewangles, cl.predicted_angles);
}

