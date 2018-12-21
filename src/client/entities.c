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
// cl_ents.c -- entity parsing and management

#include "client.h"

extern qhandle_t cl_mod_powerscreen;
extern qhandle_t cl_mod_laser;
extern qhandle_t cl_mod_dmspot;
extern qhandle_t cl_sfx_footsteps[4];

/*
=========================================================================

FRAME PARSING

=========================================================================
*/

static inline qboolean entity_optimized(const entity_state_t *state)
{
    if (cls.serverProtocol != PROTOCOL_VERSION_Q2PRO)
        return qfalse;

    if (state->number != cl.frame.clientNum + 1)
        return qfalse;

    if (cl.frame.ps.pmove.pm_type >= PM_DEAD)
        return qfalse;

    return qtrue;
}

static inline void
entity_update_new(centity_t *ent, const entity_state_t *state, const vec_t *origin)
{
    static int entity_ctr;
    ent->id = ++entity_ctr;
    ent->trailcount = 1024;     // for diminishing rocket / grenade trails

    // duplicate the current state so lerping doesn't hurt anything
    ent->prev = *state;
#if USE_FPS
    ent->prev_frame = state->frame;
    ent->event_frame = cl.frame.number;
#endif

    if (state->event == EV_PLAYER_TELEPORT ||
        state->event == EV_OTHER_TELEPORT ||
        (state->renderfx & (RF_FRAMELERP | RF_BEAM))) {
        // no lerping if teleported
        VectorCopy(origin, ent->lerp_origin);
        return;
    }

    // old_origin is valid for new entities,
    // so use it as starting point for interpolating between
    VectorCopy(state->old_origin, ent->prev.origin);
    VectorCopy(state->old_origin, ent->lerp_origin);
}

static inline void
entity_update_old(centity_t *ent, const entity_state_t *state, const vec_t *origin)
{
    int event = state->event;

#if USE_FPS
    // check for new event
    if (state->event != ent->current.event)
        ent->event_frame = cl.frame.number; // new
    else if (cl.frame.number - ent->event_frame >= cl.framediv)
        ent->event_frame = cl.frame.number; // refreshed
    else
        event = 0; // duplicated
#endif

    if (state->modelindex != ent->current.modelindex
        || state->modelindex2 != ent->current.modelindex2
        || state->modelindex3 != ent->current.modelindex3
        || state->modelindex4 != ent->current.modelindex4
        || event == EV_PLAYER_TELEPORT
        || event == EV_OTHER_TELEPORT
        || fabsf(origin[0] - ent->current.origin[0]) > 512
        || fabsf(origin[1] - ent->current.origin[1]) > 512
        || fabsf(origin[2] - ent->current.origin[2]) > 512
        || cl_nolerp->integer == 1) {
        // some data changes will force no lerping
        ent->trailcount = 1024;     // for diminishing rocket / grenade trails

        // duplicate the current state so lerping doesn't hurt anything
        ent->prev = *state;
#if USE_FPS
        ent->prev_frame = state->frame;
#endif
        // no lerping if teleported or morphed
        VectorCopy(origin, ent->lerp_origin);
        return;
    }

#if USE_FPS
    // start alias model animation
    if (state->frame != ent->current.frame) {
        ent->prev_frame = ent->current.frame;
        ent->anim_start = cl.servertime - cl.frametime;
        Com_DDPrintf("[%d] anim start %d: %d --> %d [%d]\n",
                     ent->anim_start, state->number,
                     ent->prev_frame, state->frame,
                     cl.frame.number);
    }
#endif

    // shuffle the last state to previous
    ent->prev = ent->current;
}

static inline qboolean entity_new(const centity_t *ent)
{
    if (!cl.oldframe.valid)
        return qtrue;   // last received frame was invalid

    if (ent->serverframe != cl.oldframe.number)
        return qtrue;   // wasn't in last received frame

    if (cl_nolerp->integer == 2)
        return qtrue;   // developer option, always new

    if (cl_nolerp->integer == 3)
        return qfalse;  // developer option, lerp from last received frame

    if (cl.oldframe.number != cl.frame.number - 1)
        return qtrue;   // previous server frame was dropped

    return qfalse;
}

static void entity_update(const entity_state_t *state)
{
    centity_t *ent = &cl_entities[state->number];
    const vec_t *origin;
    vec3_t origin_v;

    // if entity is solid, decode mins/maxs and add to the list
    if (state->solid && state->number != cl.frame.clientNum + 1
        && cl.numSolidEntities < MAX_PACKET_ENTITIES) {
        cl.solidEntities[cl.numSolidEntities++] = ent;
        if (state->solid != PACKED_BSP) {
            // encoded bbox
            if (cl.esFlags & MSG_ES_LONGSOLID) {
                MSG_UnpackSolid32(state->solid, ent->mins, ent->maxs);
            } else {
                MSG_UnpackSolid16(state->solid, ent->mins, ent->maxs);
            }
        }
    }

    // work around Q2PRO server bandwidth optimization
    if (entity_optimized(state)) {
        VectorScale(cl.frame.ps.pmove.origin, 0.125f, origin_v);
        origin = origin_v;
    } else {
        origin = state->origin;
    }

    if (entity_new(ent)) {
        // wasn't in last update, so initialize some things
        entity_update_new(ent, state, origin);
    } else {
        entity_update_old(ent, state, origin);
    }

    ent->serverframe = cl.frame.number;
    ent->current = *state;

    // work around Q2PRO server bandwidth optimization
    if (entity_optimized(state)) {
        Com_PlayerToEntityState(&cl.frame.ps, &ent->current);
    }
}

// an entity has just been parsed that has an event value
static void entity_event(int number)
{
    centity_t *cent = &cl_entities[number];

    // EF_TELEPORTER acts like an event, but is not cleared each frame
    if ((cent->current.effects & EF_TELEPORTER) && CL_FRAMESYNC) {
        CL_TeleporterParticles(cent->current.origin);
    }

#if USE_FPS
    if (cent->event_frame != cl.frame.number)
        return;
#endif

    switch (cent->current.event) {
    case EV_ITEM_RESPAWN:
        S_StartSound(NULL, number, CHAN_WEAPON, S_RegisterSound("items/respawn1.wav"), 1, ATTN_IDLE, 0);
        CL_ItemRespawnParticles(cent->current.origin);
        break;
    case EV_PLAYER_TELEPORT:
        S_StartSound(NULL, number, CHAN_WEAPON, S_RegisterSound("misc/tele1.wav"), 1, ATTN_IDLE, 0);
        CL_TeleportParticles(cent->current.origin);
        break;
    case EV_FOOTSTEP:
        if (cl_footsteps->integer)
            S_StartSound(NULL, number, CHAN_BODY, cl_sfx_footsteps[rand() & 3], 1, ATTN_NORM, 0);
        break;
    case EV_FALLSHORT:
        S_StartSound(NULL, number, CHAN_AUTO, S_RegisterSound("player/land1.wav"), 1, ATTN_NORM, 0);
        break;
    case EV_FALL:
        S_StartSound(NULL, number, CHAN_AUTO, S_RegisterSound("*fall2.wav"), 1, ATTN_NORM, 0);
        break;
    case EV_FALLFAR:
        S_StartSound(NULL, number, CHAN_AUTO, S_RegisterSound("*fall1.wav"), 1, ATTN_NORM, 0);
        break;
    }
}

static void set_active_state(void)
{
    cls.state = ca_active;

    cl.serverdelta = Q_align(cl.frame.number, CL_FRAMEDIV);
    cl.time = cl.servertime = 0; // set time, needed for demos
#if USE_FPS
    cl.keytime = cl.keyservertime = 0;
    cl.keyframe = cl.frame; // initialize keyframe to make sure it's valid
#endif

    // initialize oldframe so lerping doesn't hurt anything
    cl.oldframe.valid = qfalse;
    cl.oldframe.ps = cl.frame.ps;
#if USE_FPS
    cl.oldkeyframe.valid = qfalse;
    cl.oldkeyframe.ps = cl.keyframe.ps;
#endif

    cl.frameflags = 0;

    if (cls.netchan) {
        cl.initialSeq = cls.netchan->outgoing_sequence;
    }

    if (cls.demo.playback) {
        // init some demo things
        CL_FirstDemoFrame();
    } else {
        // set initial cl.predicted_origin and cl.predicted_angles
        VectorScale(cl.frame.ps.pmove.origin, 0.125f, cl.predicted_origin);
        VectorScale(cl.frame.ps.pmove.velocity, 0.125f, cl.predicted_velocity);
        if (cl.frame.ps.pmove.pm_type < PM_DEAD &&
            cls.serverProtocol > PROTOCOL_VERSION_DEFAULT) {
            // enhanced servers don't send viewangles
            CL_PredictAngles();
        } else {
            // just use what server provided
            VectorCopy(cl.frame.ps.viewangles, cl.predicted_angles);
        }
    }

    SCR_EndLoadingPlaque();     // get rid of loading plaque
    SCR_LagClear();
    Con_Close(qfalse);          // get rid of connection screen

    CL_CheckForPause();

    CL_UpdateFrameTimes();

    if (!cls.demo.playback) {
        EXEC_TRIGGER(cl_beginmapcmd);
        Cmd_ExecTrigger("#cl_enterlevel");
    }
}

static void
player_update(server_frame_t *oldframe, server_frame_t *frame, int framediv)
{
    player_state_t *ps, *ops;
    centity_t *ent;
    int oldnum;

    // find states to interpolate between
    ps = &frame->ps;
    ops = &oldframe->ps;

    // no lerping if previous frame was dropped or invalid
    if (!oldframe->valid)
        goto dup;

    oldnum = frame->number - framediv;
    if (oldframe->number != oldnum)
        goto dup;

    // no lerping if player entity was teleported (origin check)
    if (abs(ops->pmove.origin[0] - ps->pmove.origin[0]) > 256 * 8 ||
        abs(ops->pmove.origin[1] - ps->pmove.origin[1]) > 256 * 8 ||
        abs(ops->pmove.origin[2] - ps->pmove.origin[2]) > 256 * 8) {
        goto dup;
    }

    // no lerping if player entity was teleported (event check)
    ent = &cl_entities[frame->clientNum + 1];
    if (ent->serverframe > oldnum &&
        ent->serverframe <= frame->number &&
#if USE_FPS
        ent->event_frame > oldnum &&
        ent->event_frame <= frame->number &&
#endif
        (ent->current.event == EV_PLAYER_TELEPORT
         || ent->current.event == EV_OTHER_TELEPORT)) {
        goto dup;
    }

    // no lerping if teleport bit was flipped
    if ((ops->pmove.pm_flags ^ ps->pmove.pm_flags) & PMF_TELEPORT_BIT)
        goto dup;

    // no lerping if POV number changed
    if (oldframe->clientNum != frame->clientNum)
        goto dup;

    // developer option
    if (cl_nolerp->integer == 1)
        goto dup;

    return;

dup:
    // duplicate the current state so lerping doesn't hurt anything
    *ops = *ps;
}

/*
==================
CL_DeltaFrame

A valid frame has been parsed.
==================
*/
void CL_DeltaFrame(void)
{
    centity_t           *ent;
    entity_state_t      *state;
    int                 i, j;
    int                 framenum;
    int                 prevstate = cls.state;

    // getting a valid frame message ends the connection process
    if (cls.state == ca_precached)
        set_active_state();

    // set server time
    framenum = cl.frame.number - cl.serverdelta;
    cl.servertime = framenum * CL_FRAMETIME;
#if USE_FPS
    cl.keyservertime = (framenum / cl.framediv) * BASE_FRAMETIME;
#endif

    // rebuild the list of solid entities for this frame
    cl.numSolidEntities = 0;

    // initialize position of the player's own entity from playerstate.
    // this is needed in situations when player entity is invisible, but
    // server sends an effect referencing it's origin (such as MZ_LOGIN, etc)
    ent = &cl_entities[cl.frame.clientNum + 1];
    Com_PlayerToEntityState(&cl.frame.ps, &ent->current);

    for (i = 0; i < cl.frame.numEntities; i++) {
        j = (cl.frame.firstEntity + i) & PARSE_ENTITIES_MASK;
        state = &cl.entityStates[j];

        // set current and prev
        entity_update(state);

        // fire events
        entity_event(state->number);
    }

    if (cls.demo.recording && !cls.demo.paused && !cls.demo.seeking && CL_FRAMESYNC) {
        CL_EmitDemoFrame();
    }

    if (prevstate == ca_precached)
        CL_GTV_Resume();
    else
        CL_GTV_EmitFrame();

    if (cls.demo.playback) {
        // this delta has nothing to do with local viewangles,
        // clear it to avoid interfering with demo freelook hack
        VectorClear(cl.frame.ps.pmove.delta_angles);
    }

    if (cl.oldframe.ps.pmove.pm_type != cl.frame.ps.pmove.pm_type) {
        IN_Activate();
    }

    player_update(&cl.oldframe, &cl.frame, 1);

#if USE_FPS
    if (CL_FRAMESYNC)
        player_update(&cl.oldkeyframe, &cl.keyframe, cl.framediv);
#endif

    CL_CheckPredictionError();

    SCR_SetCrosshairColor();
}

#ifdef _DEBUG
// for debugging problems when out-of-date entity origin is referenced
void CL_CheckEntityPresent(int entnum, const char *what)
{
    centity_t *e;

    if (entnum == cl.frame.clientNum + 1) {
        return; // player entity = current
    }

    e = &cl_entities[entnum];
    if (e->serverframe == cl.frame.number) {
        return; // current
    }

    if (e->serverframe) {
        Com_LPrintf(PRINT_DEVELOPER,
                    "SERVER BUG: %s on entity %d last seen %d frames ago\n",
                    what, entnum, cl.frame.number - e->serverframe);
    } else {
        Com_LPrintf(PRINT_DEVELOPER,
                    "SERVER BUG: %s on entity %d never seen before\n",
                    what, entnum);
    }
}
#endif


/*
==========================================================================

INTERPOLATE BETWEEN FRAMES TO GET RENDERING PARMS

==========================================================================
*/

/*
===============
CL_AddPacketEntities

===============
*/
static void CL_AddPacketEntities(void)
{
    entity_t            ent;
    entity_state_t      *s1;
    float               autorotate;
    int                 i;
    int                 pnum;
    centity_t           *cent;
    int                 autoanim;
    clientinfo_t        *ci;
    unsigned int        effects, renderfx;

    // bonus items rotate at a fixed rate
    autorotate = anglemod(cl.time * 0.1f);

    // brush models can auto animate their frames
    autoanim = 2 * cl.time / 1000;

    memset(&ent, 0, sizeof(ent));

    for (pnum = 0; pnum < cl.frame.numEntities; pnum++) {
        i = (cl.frame.firstEntity + pnum) & PARSE_ENTITIES_MASK;
        s1 = &cl.entityStates[i];

        cent = &cl_entities[s1->number];
        ent.id = cent->id;

        effects = s1->effects;
        renderfx = s1->renderfx;

        // set frame
        if (effects & EF_ANIM01)
            ent.frame = autoanim & 1;
        else if (effects & EF_ANIM23)
            ent.frame = 2 + (autoanim & 1);
        else if (effects & EF_ANIM_ALL)
            ent.frame = autoanim;
        else if (effects & EF_ANIM_ALLFAST)
            ent.frame = cl.time / 100;
        else
            ent.frame = s1->frame;

        // quad and pent can do different things on client
        if (effects & EF_PENT) {
            effects &= ~EF_PENT;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_RED;
        }

        if (effects & EF_QUAD) {
            effects &= ~EF_QUAD;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_BLUE;
        }

        if (effects & EF_DOUBLE) {
            effects &= ~EF_DOUBLE;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_DOUBLE;
        }

        if (effects & EF_HALF_DAMAGE) {
            effects &= ~EF_HALF_DAMAGE;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_HALF_DAM;
        }

        // optionally remove the glowing effect
        if (cl_noglow->integer)
            renderfx &= ~RF_GLOW;

        ent.oldframe = cent->prev.frame;
        ent.backlerp = 1.0 - cl.lerpfrac;

        if (renderfx & RF_FRAMELERP) {
            // step origin discretely, because the frames
            // do the animation properly
            VectorCopy(cent->current.origin, ent.origin);
            VectorCopy(cent->current.old_origin, ent.oldorigin);  // FIXME
        } else if (renderfx & RF_BEAM) {
            // interpolate start and end points for beams
            LerpVector(cent->prev.origin, cent->current.origin,
                       cl.lerpfrac, ent.origin);
            LerpVector(cent->prev.old_origin, cent->current.old_origin,
                       cl.lerpfrac, ent.oldorigin);
        } else {
            if (s1->number == cl.frame.clientNum + 1) {
                // use predicted origin
                VectorCopy(cl.playerEntityOrigin, ent.origin);
                VectorCopy(cl.playerEntityOrigin, ent.oldorigin);
            } else {
                // interpolate origin
                LerpVector(cent->prev.origin, cent->current.origin,
                           cl.lerpfrac, ent.origin);
                VectorCopy(ent.origin, ent.oldorigin);
            }

#if USE_FPS
            // run alias model animation
            if (cent->prev_frame != s1->frame) {
                int delta = cl.time - cent->anim_start;
                float frac;

                if (delta > BASE_FRAMETIME) {
                    Com_DDPrintf("[%d] anim end %d: %d --> %d\n",
                                 cl.time, s1->number,
                                 cent->prev_frame, s1->frame);
                    cent->prev_frame = s1->frame;
                    frac = 1;
                } else if (delta > 0) {
                    frac = delta * BASE_1_FRAMETIME;
                    Com_DDPrintf("[%d] anim run %d: %d --> %d [%f]\n",
                                 cl.time, s1->number,
                                 cent->prev_frame, s1->frame,
                                 frac);
                } else {
                    frac = 0;
                }

                ent.oldframe = cent->prev_frame;
                ent.backlerp = 1.0 - frac;
            }
#endif
        }

        if ((effects & EF_GIB) && !cl_gibs->integer) {
            goto skip;
        }

        // create a new entity

        // tweak the color of beams
        if (renderfx & RF_BEAM) {
            // the four beam colors are encoded in 32 bits of skinnum (hack)
            ent.alpha = 0.30;
            ent.skinnum = (s1->skinnum >> ((rand() % 4) * 8)) & 0xff;
            ent.model = 0;
        } else {
            // set skin
            if (s1->modelindex == 255) {
                // use custom player skin
                ent.skinnum = 0;
                ci = &cl.clientinfo[s1->skinnum & 0xff];
                ent.skin = ci->skin;
                ent.model = ci->model;
                if (!ent.skin || !ent.model) {
                    ent.skin = cl.baseclientinfo.skin;
                    ent.model = cl.baseclientinfo.model;
                    ci = &cl.baseclientinfo;
                }
                if (renderfx & RF_USE_DISGUISE) {
                    char buffer[MAX_QPATH];

                    Q_concat(buffer, sizeof(buffer), "players/", ci->model_name, "/disguise.pcx", NULL);
                    ent.skin = R_RegisterSkin(buffer);
                }
            } else {
                ent.skinnum = s1->skinnum;
                ent.skin = 0;
                ent.model = cl.model_draw[s1->modelindex];
                if (ent.model == cl_mod_laser || ent.model == cl_mod_dmspot)
                    renderfx |= RF_NOSHADOW;
            }
        }

        // only used for black hole model right now, FIXME: do better
        if ((renderfx & RF_TRANSLUCENT) && !(renderfx & RF_BEAM))
            ent.alpha = 0.70;

        // render effects (fullbright, translucent, etc)
        if ((effects & EF_COLOR_SHELL))
            ent.flags = 0;  // renderfx go on color shell entity
        else
            ent.flags = renderfx;

        // calculate angles
        if (effects & EF_ROTATE) {  // some bonus items auto-rotate
            ent.angles[0] = 0;
            ent.angles[1] = autorotate;
            ent.angles[2] = 0;
        } else if (effects & EF_SPINNINGLIGHTS) {
            vec3_t forward;
            vec3_t start;

            ent.angles[0] = 0;
            ent.angles[1] = anglemod(cl.time / 2) + s1->angles[1];
            ent.angles[2] = 180;

            AngleVectors(ent.angles, forward, NULL, NULL);
            VectorMA(ent.origin, 64, forward, start);
            V_AddLight(start, 100, 1, 0, 0);
        } else if (s1->number == cl.frame.clientNum + 1) {
            VectorCopy(cl.playerEntityAngles, ent.angles);      // use predicted angles
        } else { // interpolate angles
            LerpAngles(cent->prev.angles, cent->current.angles,
                       cl.lerpfrac, ent.angles);

            // mimic original ref_gl "leaning" bug (uuugly!)
            if (s1->modelindex == 255 && cl_rollhack->integer) {
                ent.angles[ROLL] = -ent.angles[ROLL];
            }
        }

        if (s1->number == cl.frame.clientNum + 1) {
            if (effects & EF_FLAG1)
                V_AddLight(ent.origin, 225, 1.0, 0.1, 0.1);
            else if (effects & EF_FLAG2)
                V_AddLight(ent.origin, 225, 0.1, 0.1, 1.0);
            else if (effects & EF_TAGTRAIL)
                V_AddLight(ent.origin, 225, 1.0, 1.0, 0.0);
            else if (effects & EF_TRACKERTRAIL)
                V_AddLight(ent.origin, 225, -1.0, -1.0, -1.0);

            if (!cl.thirdPersonView) {
#if 0
                ent.flags |= RF_VIEWERMODEL;    // only draw from mirrors
#else
                goto skip;
#endif
            }
        }

        // if set to invisible, skip
        if (!s1->modelindex) {
            goto skip;
        }

        if (effects & EF_BFG) {
            ent.flags |= RF_TRANSLUCENT;
            ent.alpha = 0.30;
        }

        if (effects & EF_PLASMA) {
            ent.flags |= RF_TRANSLUCENT;
            ent.alpha = 0.6;
        }

        if (effects & EF_SPHERETRANS) {
            ent.flags |= RF_TRANSLUCENT;
            if (effects & EF_TRACKERTRAIL)
                ent.alpha = 0.6;
            else
                ent.alpha = 0.3;
        }

        // add to refresh list
        V_AddEntity(&ent);

        // color shells generate a seperate entity for the main model
        if (effects & EF_COLOR_SHELL) {
            // PMM - at this point, all of the shells have been handled
            // if we're in the rogue pack, set up the custom mixing, otherwise just
            // keep going
            if (!strcmp(fs_game->string, "rogue")) {
                // all of the solo colors are fine.  we need to catch any of the combinations that look bad
                // (double & half) and turn them into the appropriate color, and make double/quad something special
                if (renderfx & RF_SHELL_HALF_DAM) {
                    // ditch the half damage shell if any of red, blue, or double are on
                    if (renderfx & (RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_DOUBLE))
                        renderfx &= ~RF_SHELL_HALF_DAM;
                }

                if (renderfx & RF_SHELL_DOUBLE) {
                    // lose the yellow shell if we have a red, blue, or green shell
                    if (renderfx & (RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_GREEN))
                        renderfx &= ~RF_SHELL_DOUBLE;
                    // if we have a red shell, turn it to purple by adding blue
                    if (renderfx & RF_SHELL_RED)
                        renderfx |= RF_SHELL_BLUE;
                    // if we have a blue shell (and not a red shell), turn it to cyan by adding green
                    else if (renderfx & RF_SHELL_BLUE) {
                        // go to green if it's on already, otherwise do cyan (flash green)
                        if (renderfx & RF_SHELL_GREEN)
                            renderfx &= ~RF_SHELL_BLUE;
                        else
                            renderfx |= RF_SHELL_GREEN;
                    }
                }
            }
            ent.flags = renderfx | RF_TRANSLUCENT;
            ent.alpha = 0.30;
            V_AddEntity(&ent);
        }

        ent.skin = 0;       // never use a custom skin on others
        ent.skinnum = 0;
        ent.flags = 0;
        ent.alpha = 0;

        // duplicate for linked models
        if (s1->modelindex2) {
            if (s1->modelindex2 == 255) {
                // custom weapon
                ci = &cl.clientinfo[s1->skinnum & 0xff];
                i = (s1->skinnum >> 8); // 0 is default weapon model
                if (i < 0 || i > cl.numWeaponModels - 1)
                    i = 0;
                ent.model = ci->weaponmodel[i];
                if (!ent.model) {
                    if (i != 0)
                        ent.model = ci->weaponmodel[0];
                    if (!ent.model)
                        ent.model = cl.baseclientinfo.weaponmodel[0];
                }
            } else
                ent.model = cl.model_draw[s1->modelindex2];

            // PMM - check for the defender sphere shell .. make it translucent
            if (!Q_strcasecmp(cl.configstrings[CS_MODELS + (s1->modelindex2)], "models/items/shell/tris.md2")) {
                ent.alpha = 0.32;
                ent.flags = RF_TRANSLUCENT;
            }

            V_AddEntity(&ent);

            //PGM - make sure these get reset.
            ent.flags = 0;
            ent.alpha = 0;
        }

        if (s1->modelindex3) {
            ent.model = cl.model_draw[s1->modelindex3];
            V_AddEntity(&ent);
        }

        if (s1->modelindex4) {
            ent.model = cl.model_draw[s1->modelindex4];
            V_AddEntity(&ent);
        }

        if (effects & EF_POWERSCREEN) {
            ent.model = cl_mod_powerscreen;
            ent.oldframe = 0;
            ent.frame = 0;
            ent.flags |= (RF_TRANSLUCENT | RF_SHELL_GREEN);
            ent.alpha = 0.30;
            V_AddEntity(&ent);
        }

        // add automatic particle trails
        if (effects & ~EF_ROTATE) {
            if (effects & EF_ROCKET) {
                if (!(cl_disable_particles->integer & NOPART_ROCKET_TRAIL)) {
                    CL_RocketTrail(cent->lerp_origin, ent.origin, cent);
                }
                V_AddLight(ent.origin, 200, 1, 1, 0);
            } else if (effects & EF_BLASTER) {
                if (effects & EF_TRACKER) {
                    CL_BlasterTrail2(cent->lerp_origin, ent.origin);
                    V_AddLight(ent.origin, 200, 0, 1, 0);
                } else {
                    CL_BlasterTrail(cent->lerp_origin, ent.origin);
                    V_AddLight(ent.origin, 200, 1, 1, 0);
                }
            } else if (effects & EF_HYPERBLASTER) {
                if (effects & EF_TRACKER)
                    V_AddLight(ent.origin, 200, 0, 1, 0);
                else
                    V_AddLight(ent.origin, 200, 1, 1, 0);
            } else if (effects & EF_GIB) {
                CL_DiminishingTrail(cent->lerp_origin, ent.origin, cent, effects);
            } else if (effects & EF_GRENADE) {
                if (!(cl_disable_particles->integer & NOPART_GRENADE_TRAIL)) {
                    CL_DiminishingTrail(cent->lerp_origin, ent.origin, cent, effects);
                }
            } else if (effects & EF_FLIES) {
                CL_FlyEffect(cent, ent.origin);
            } else if (effects & EF_BFG) {
                if (effects & EF_ANIM_ALLFAST) {
                    CL_BfgParticles(&ent);
#if USE_DLIGHTS
                    i = 200;
                } else {
                    static const int bfg_lightramp[6] = {300, 400, 600, 300, 150, 75};

                    i = s1->frame; clamp(i, 0, 5);
                    i = bfg_lightramp[i];
#endif
                }
                V_AddLight(ent.origin, i, 0, 1, 0);
            } else if (effects & EF_TRAP) {
                ent.origin[2] += 32;
                CL_TrapParticles(&ent);
#if USE_DLIGHTS
                i = (rand() % 100) + 100;
                V_AddLight(ent.origin, i, 1, 0.8, 0.1);
#endif
            } else if (effects & EF_FLAG1) {
                CL_FlagTrail(cent->lerp_origin, ent.origin, 242);
                V_AddLight(ent.origin, 225, 1, 0.1, 0.1);
            } else if (effects & EF_FLAG2) {
                CL_FlagTrail(cent->lerp_origin, ent.origin, 115);
                V_AddLight(ent.origin, 225, 0.1, 0.1, 1);
            } else if (effects & EF_TAGTRAIL) {
                CL_TagTrail(cent->lerp_origin, ent.origin, 220);
                V_AddLight(ent.origin, 225, 1.0, 1.0, 0.0);
            } else if (effects & EF_TRACKERTRAIL) {
                if (effects & EF_TRACKER) {
#if USE_DLIGHTS
                    float intensity;

                    intensity = 50 + (500 * (sin(cl.time / 500.0) + 1.0));
                    V_AddLight(ent.origin, intensity, -1.0, -1.0, -1.0);
#endif
                } else {
                    CL_Tracker_Shell(cent->lerp_origin);
                    V_AddLight(ent.origin, 155, -1.0, -1.0, -1.0);
                }
            } else if (effects & EF_TRACKER) {
                CL_TrackerTrail(cent->lerp_origin, ent.origin, 0);
                V_AddLight(ent.origin, 200, -1, -1, -1);
            } else if (effects & EF_GREENGIB) {
                CL_DiminishingTrail(cent->lerp_origin, ent.origin, cent, effects);
            } else if (effects & EF_IONRIPPER) {
                CL_IonripperTrail(cent->lerp_origin, ent.origin);
                V_AddLight(ent.origin, 100, 1, 0.5, 0.5);
            } else if (effects & EF_BLUEHYPERBLASTER) {
                V_AddLight(ent.origin, 200, 0, 0, 1);
            } else if (effects & EF_PLASMA) {
                if (effects & EF_ANIM_ALLFAST) {
                    CL_BlasterTrail(cent->lerp_origin, ent.origin);
                }
                V_AddLight(ent.origin, 130, 1, 0.5, 0.5);
            }
        }

skip:
        VectorCopy(ent.origin, cent->lerp_origin);
    }
}

static int shell_effect_hack(void)
{
    centity_t   *ent;
    int         flags = 0;

    if (cl.frame.clientNum == CLIENTNUM_NONE)
        return 0;

    ent = &cl_entities[cl.frame.clientNum + 1];
    if (ent->serverframe != cl.frame.number)
        return 0;

    if (!ent->current.modelindex)
        return 0;

    if (ent->current.effects & EF_PENT)
        flags |= RF_SHELL_RED;
    if (ent->current.effects & EF_QUAD)
        flags |= RF_SHELL_BLUE;
    if (ent->current.effects & EF_DOUBLE)
        flags |= RF_SHELL_DOUBLE;
    if (ent->current.effects & EF_HALF_DAMAGE)
        flags |= RF_SHELL_HALF_DAM;

    return flags;
}

/*
==============
CL_AddViewWeapon
==============
*/
static void CL_AddViewWeapon(void)
{
    player_state_t *ps, *ops;
    entity_t    gun;        // view model
    int         i, flags;

    // allow the gun to be completely removed
    if (cl_gun->integer < 1) {
        return;
    }

    if (info_hand->integer == 2) {
        return;
    }

    // find states to interpolate between
    ps = CL_KEYPS;
    ops = CL_OLDKEYPS;

    memset(&gun, 0, sizeof(gun));

    if (gun_model) {
        gun.model = gun_model;  // development tool
    } else {
        gun.model = cl.model_draw[ps->gunindex];
    }
    if (!gun.model) {
        return;
    }

    // set up gun position
    for (i = 0; i < 3; i++) {
        gun.origin[i] = cl.refdef.vieworg[i] + ops->gunoffset[i] +
                        CL_KEYLERPFRAC * (ps->gunoffset[i] - ops->gunoffset[i]);
        gun.angles[i] = cl.refdef.viewangles[i] + LerpAngle(ops->gunangles[i],
                        ps->gunangles[i], CL_KEYLERPFRAC);
    }

    // adjust for high fov
    if (ps->fov > 90) {
        vec_t ofs = (90 - ps->fov) * 0.2f;
        VectorMA(gun.origin, ofs, cl.v_forward, gun.origin);
    }

    VectorCopy(gun.origin, gun.oldorigin);      // don't lerp at all

    if (gun_frame) {
        gun.frame = gun_frame;  // development tool
        gun.oldframe = gun_frame;   // development tool
    } else {
        gun.frame = ps->gunframe;
        if (gun.frame == 0) {
            gun.oldframe = 0;   // just changed weapons, don't lerp from old
        } else {
            gun.oldframe = ops->gunframe;
            gun.backlerp = 1.0f - CL_KEYLERPFRAC;
        }
    }

    gun.flags = RF_MINLIGHT | RF_DEPTHHACK | RF_WEAPONMODEL;
    if (info_hand->integer == 1) {
        gun.flags |= RF_LEFTHAND;
    }

    if (cl_gunalpha->value != 1) {
        gun.alpha = Cvar_ClampValue(cl_gunalpha, 0.1f, 1.0f);
        gun.flags |= RF_TRANSLUCENT;
    }

    V_AddEntity(&gun);

    // add shell effect from player entity
    flags = shell_effect_hack();
    if (flags) {
        gun.alpha = 0.30f * cl_gunalpha->value;
        gun.flags |= flags | RF_TRANSLUCENT;
        V_AddEntity(&gun);
    }
}

static void CL_SetupFirstPersonView(void)
{
    player_state_t *ps, *ops;
    vec3_t kickangles;
    float lerp;

    // add kick angles
    if (cl_kickangles->integer) {
        ps = CL_KEYPS;
        ops = CL_OLDKEYPS;

        lerp = CL_KEYLERPFRAC;

        LerpAngles(ops->kick_angles, ps->kick_angles, lerp, kickangles);
        VectorAdd(cl.refdef.viewangles, kickangles, cl.refdef.viewangles);
    }

    // add the weapon
    CL_AddViewWeapon();

    cl.thirdPersonView = qfalse;
}

/*
===============
CL_SetupThirdPersionView
===============
*/
static void CL_SetupThirdPersionView(void)
{
    vec3_t focus;
    float fscale, rscale;
    float dist, angle, range;
    trace_t trace;
    static vec3_t mins = { -4, -4, -4 }, maxs = { 4, 4, 4 };

    // if dead, set a nice view angle
    if (cl.frame.ps.stats[STAT_HEALTH] <= 0) {
        cl.refdef.viewangles[ROLL] = 0;
        cl.refdef.viewangles[PITCH] = 10;
    }

    VectorMA(cl.refdef.vieworg, 512, cl.v_forward, focus);

    cl.refdef.vieworg[2] += 8;

    cl.refdef.viewangles[PITCH] *= 0.5f;
    AngleVectors(cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up);

    angle = DEG2RAD(cl_thirdperson_angle->value);
    range = cl_thirdperson_range->value;
    fscale = cos(angle);
    rscale = sin(angle);
    VectorMA(cl.refdef.vieworg, -range * fscale, cl.v_forward, cl.refdef.vieworg);
    VectorMA(cl.refdef.vieworg, -range * rscale, cl.v_right, cl.refdef.vieworg);

    CM_BoxTrace(&trace, cl.playerEntityOrigin, cl.refdef.vieworg,
                mins, maxs, cl.bsp->nodes, MASK_SOLID);
    if (trace.fraction != 1.0f) {
        VectorCopy(trace.endpos, cl.refdef.vieworg);
    }

    VectorSubtract(focus, cl.refdef.vieworg, focus);
    dist = sqrt(focus[0] * focus[0] + focus[1] * focus[1]);

    cl.refdef.viewangles[PITCH] = -180 / M_PI * atan2(focus[2], dist);
    cl.refdef.viewangles[YAW] -= cl_thirdperson_angle->value;

    cl.thirdPersonView = qtrue;
}

static void CL_FinishViewValues(void)
{
    centity_t *ent;

    if (!cl_thirdperson->integer)
        goto first;

    if (cl.frame.clientNum == CLIENTNUM_NONE)
        goto first;

    ent = &cl_entities[cl.frame.clientNum + 1];
    if (ent->serverframe != cl.frame.number)
        goto first;

    if (!ent->current.modelindex)
        goto first;

    CL_SetupThirdPersionView();
    return;

first:
    CL_SetupFirstPersonView();
}

#if USE_SMOOTH_DELTA_ANGLES
static inline float LerpShort(int a2, int a1, float frac)
{
    if (a1 - a2 > 32768)
        a1 &= 65536;
    if (a2 - a1 > 32768)
        a1 &= 65536;
    return a2 + frac * (a1 - a2);
}
#endif

static inline float lerp_client_fov(float ofov, float nfov, float lerp)
{
    if (cls.demo.playback) {
        float fov = info_fov->value;

        if (fov < 1)
            fov = 90;
        else if (fov > 160)
            fov = 160;

        if (info_uf->integer & UF_LOCALFOV)
            return fov;

        if (!(info_uf->integer & UF_PLAYERFOV)) {
            if (ofov >= 90)
                ofov = fov;
            if (nfov >= 90)
                nfov = fov;
        }
    }

    return ofov + lerp * (nfov - ofov);
}

/*
===============
CL_CalcViewValues

Sets cl.refdef view values and sound spatialization params.
Usually called from CL_AddEntities, but may be directly called from the main
loop if rendering is disabled but sound is running.
===============
*/
void CL_CalcViewValues(void)
{
    player_state_t *ps, *ops;
    vec3_t viewoffset;
    float lerp;

    if (!cl.frame.valid) {
        return;
    }

    // find states to interpolate between
    ps = &cl.frame.ps;
    ops = &cl.oldframe.ps;

    lerp = cl.lerpfrac;

    // calculate the origin
    if (!cls.demo.playback && cl_predict->integer && !(ps->pmove.pm_flags & PMF_NO_PREDICTION)) {
        // use predicted values
        unsigned delta = cls.realtime - cl.predicted_step_time;
        float backlerp = lerp - 1.0;

        VectorMA(cl.predicted_origin, backlerp, cl.prediction_error, cl.refdef.vieworg);

        // smooth out stair climbing
        if (cl.predicted_step < 127 * 0.125f) {
            delta <<= 1; // small steps
        }
        if (delta < 100) {
            cl.refdef.vieworg[2] -= cl.predicted_step * (100 - delta) * 0.01f;
        }
    } else {
        // just use interpolated values
        cl.refdef.vieworg[0] = ops->pmove.origin[0] * 0.125f +
                               lerp * (ps->pmove.origin[0] - ops->pmove.origin[0]) * 0.125f;
        cl.refdef.vieworg[1] = ops->pmove.origin[1] * 0.125f +
                               lerp * (ps->pmove.origin[1] - ops->pmove.origin[1]) * 0.125f;
        cl.refdef.vieworg[2] = ops->pmove.origin[2] * 0.125f +
                               lerp * (ps->pmove.origin[2] - ops->pmove.origin[2]) * 0.125f;
    }

    // if not running a demo or on a locked frame, add the local angle movement
    if (cls.demo.playback) {
        if (cls.key_dest == KEY_GAME && Key_IsDown(K_SHIFT)) {
            VectorCopy(cl.viewangles, cl.refdef.viewangles);
        } else {
            LerpAngles(ops->viewangles, ps->viewangles, lerp,
                       cl.refdef.viewangles);
        }
    } else if (ps->pmove.pm_type < PM_DEAD) {
        // use predicted values
        VectorCopy(cl.predicted_angles, cl.refdef.viewangles);
    } else if (ops->pmove.pm_type < PM_DEAD && cls.serverProtocol > PROTOCOL_VERSION_DEFAULT) {
        // lerp from predicted angles, since enhanced servers
        // do not send viewangles each frame
        LerpAngles(cl.predicted_angles, ps->viewangles, lerp, cl.refdef.viewangles);
    } else {
        // just use interpolated values
        LerpAngles(ops->viewangles, ps->viewangles, lerp, cl.refdef.viewangles);
    }

#if USE_SMOOTH_DELTA_ANGLES
    cl.delta_angles[0] = LerpShort(ops->pmove.delta_angles[0], ps->pmove.delta_angles[0], lerp);
    cl.delta_angles[1] = LerpShort(ops->pmove.delta_angles[1], ps->pmove.delta_angles[1], lerp);
    cl.delta_angles[2] = LerpShort(ops->pmove.delta_angles[2], ps->pmove.delta_angles[2], lerp);
#endif

    // don't interpolate blend color
    Vector4Copy(ps->blend, cl.refdef.blend);

#if USE_FPS
    ps = &cl.keyframe.ps;
    ops = &cl.oldkeyframe.ps;

    lerp = cl.keylerpfrac;
#endif

    // interpolate field of view
    cl.fov_x = lerp_client_fov(ops->fov, ps->fov, lerp);
    cl.fov_y = V_CalcFov(cl.fov_x, 4, 3);

    LerpVector(ops->viewoffset, ps->viewoffset, lerp, viewoffset);

    AngleVectors(cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up);

    VectorCopy(cl.refdef.vieworg, cl.playerEntityOrigin);
    VectorCopy(cl.refdef.viewangles, cl.playerEntityAngles);

    if (cl.playerEntityAngles[PITCH] > 180) {
        cl.playerEntityAngles[PITCH] -= 360;
    }

    cl.playerEntityAngles[PITCH] = cl.playerEntityAngles[PITCH] / 3;

    VectorAdd(cl.refdef.vieworg, viewoffset, cl.refdef.vieworg);

    VectorCopy(cl.refdef.vieworg, listener_origin);
    VectorCopy(cl.v_forward, listener_forward);
    VectorCopy(cl.v_right, listener_right);
    VectorCopy(cl.v_up, listener_up);
}

/*
===============
CL_AddEntities

Emits all entities, particles, and lights to the refresh
===============
*/
void CL_AddEntities(void)
{
    CL_CalcViewValues();
    CL_FinishViewValues();
    CL_AddPacketEntities();
    CL_AddTEnts();
    CL_AddParticles();
#if USE_DLIGHTS
    CL_AddDLights();
#endif
#if USE_LIGHTSTYLES
    CL_AddLightStyles();
#endif
    LOC_AddLocationsToScene();
}

/*
===============
CL_GetEntitySoundOrigin

Called to get the sound spatialization origin
===============
*/
void CL_GetEntitySoundOrigin(int entnum, vec3_t org)
{
    centity_t   *ent;
    mmodel_t    *cm;
    vec3_t      mid;

    if (entnum < 0 || entnum >= MAX_EDICTS) {
        Com_Error(ERR_DROP, "%s: bad entnum: %d", __func__, entnum);
    }

    if (!entnum || entnum == listener_entnum) {
        // should this ever happen?
        VectorCopy(listener_origin, org);
        return;
    }

    // interpolate origin
    // FIXME: what should be the sound origin point for RF_BEAM entities?
    ent = &cl_entities[entnum];
    LerpVector(ent->prev.origin, ent->current.origin, cl.lerpfrac, org);

    // offset the origin for BSP models
    if (ent->current.solid == PACKED_BSP) {
        cm = cl.model_clip[ent->current.modelindex];
        if (cm) {
            VectorAvg(cm->mins, cm->maxs, mid);
            VectorAdd(org, mid, org);
        }
    }
}

