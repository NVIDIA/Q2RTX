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
// cl_ents.c -- entity parsing and management

#include "cl_local.h"

extern	qhandle_t cl_mod_powerscreen;

/*
=========================================================================

FRAME PARSING

=========================================================================
*/

/*
==================
CL_SetEntityState

cl.frame should be set to current frame before calling this function.
==================
*/
static void CL_SetEntityState( entity_state_t *state ) {
	centity_t *ent = &cl_entities[state->number];

	// copy predicted values for own entity
	if( state->number == cl.frame.clientNum + 1 ) {
		VectorCopy( cl.playerEntityOrigin, state->origin );
		VectorCopy( cl.playerEntityAngles, state->angles );
	} else if( state->solid ) {
		cl.solidEntities[cl.numSolidEntities++] = ent;
		if( state->solid != 31 ) {
            int x, zd, zu;
            
            // encoded bbox
            if( LONG_SOLID_SUPPORTED( cls.serverProtocol, cls.protocolVersion ) ) {
                x = (state->solid & 255);
                zd = ((state->solid>>8) & 255);
                zu = ((state->solid>>16) & 65535) - 32768;
            } else {
                x = 8*(state->solid & 31);
                zd = 8*((state->solid>>5) & 31);
                zu = 8*((state->solid>>10) & 63) - 32;
            }

			ent->mins[0] = ent->mins[1] = -x;
			ent->maxs[0] = ent->maxs[1] = x;
			ent->mins[2] = -zd;
			ent->maxs[2] = zu;
        }
    }

	if( ent->serverframe != cl.oldframe.number ) {
		// wasn't in last update, so initialize some things
		ent->trailcount = 1024;		// for diminishing rocket / grenade trails

		// duplicate the current state so lerping doesn't hurt anything
		ent->prev = *state;

		// old_origin is valid for new entities,
        // so use it as starting point for interpolating between
		VectorCopy( state->old_origin, ent->prev.origin );
		VectorCopy( state->old_origin, ent->lerp_origin );
    } else if( state->modelindex != ent->current.modelindex
		|| state->modelindex2 != ent->current.modelindex2
		|| state->modelindex3 != ent->current.modelindex3
		|| state->modelindex4 != ent->current.modelindex4
		|| state->event == EV_PLAYER_TELEPORT
		|| state->event == EV_OTHER_TELEPORT
		|| abs(state->origin[0] - ent->current.origin[0]) > 512
		|| abs(state->origin[1] - ent->current.origin[1]) > 512
		|| abs(state->origin[2] - ent->current.origin[2]) > 512 )
    {
	    // some data changes will force no lerping
		ent->trailcount = 1024;		// for diminishing rocket / grenade trails

		// duplicate the current state so lerping doesn't hurt anything
		ent->prev = *state;
        
        // no lerping
		VectorCopy( state->origin, ent->lerp_origin );
	} else {    // shuffle the last state to previous
		ent->prev = ent->current;
	}

	ent->serverframe = cl.frame.number;
	ent->current = *state;
}

/*
==================
CL_DeltaFrame

==================
*/
void CL_DeltaFrame( void ) {
	entity_state_t		*state;
	int					i, j;

    // set server time
	cl.servertime = ( cl.frame.number - cl.serverdelta ) * cl.frametime;

    VectorScale( cl.frame.ps.pmove.origin, 0.125f, cl.playerEntityOrigin );

	cl.numSolidEntities = 0;

	for( i = 0; i < cl.frame.numEntities; i++ ) {
		j = ( cl.frame.firstEntity + i ) & PARSE_ENTITIES_MASK;
		state = &cl.entityStates[j];

        // set current and prev
		CL_SetEntityState( state );

		// fire events
		if( state->event ) {
			CL_EntityEvent( state );
		}

		// EF_TELEPORTER acts like an event, but is not cleared each frame
		if( state->effects & EF_TELEPORTER ) {
			CL_TeleporterParticles( state );
        }
	}

    if( cls.demo.recording ) {
        CL_EmitDemoFrame();
    }
    
    if( cl.oldframe.ps.stats[STAT_LAYOUTS] != cl.frame.ps.stats[STAT_LAYOUTS] ) {
        cl.putaway = qfalse;
    }

    if( cl.oldframe.ps.pmove.pm_type != cl.frame.ps.pmove.pm_type ) {
        IN_Activate();
    }
}


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
static void CL_AddPacketEntities( void ) {
	entity_t			ent;
	entity_state_t		*s1;
	float				autorotate;
	int					i;
	int					pnum;
	centity_t			*cent;
	int					autoanim;
	clientinfo_t		*ci;
	unsigned int		effects, renderfx;

	// bonus items rotate at a fixed rate
	autorotate = anglemod( cl.time / 10 );

	// brush models can auto animate their frames
	autoanim = 2 * cl.time / 1000;

	memset( &ent, 0, sizeof( ent ) );

	for( pnum = 0; pnum < cl.frame.numEntities; pnum++ ) {
		i = ( cl.frame.firstEntity + pnum ) & PARSE_ENTITIES_MASK;
		s1 = &cl.entityStates[i];

		cent = &cl_entities[s1->number];

		effects = s1->effects;
		renderfx = s1->renderfx;

		if( ( effects & EF_GIB ) && !cl_gibs->integer ) {
			continue;
		}

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
//======
// PMM
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
// pmm
//======
		ent.oldframe = cent->prev.frame;
		ent.backlerp = 1.0 - cl.lerpfrac;

		if (renderfx & RF_FRAMELERP) {
			// step origin discretely, because the frames
			// do the animation properly
			VectorCopy (cent->current.origin, ent.origin);
			VectorCopy (cent->current.old_origin, ent.oldorigin); // FIXME
		} else if( renderfx & RF_BEAM ) {
			// interpolate start and end points for beams
            LerpVector( cent->prev.origin, cent->current.origin,
                cl.lerpfrac, ent.origin );
            LerpVector( cent->prev.old_origin, cent->current.old_origin,
                cl.lerpfrac, ent.oldorigin );
		} else if( s1->number == cl.frame.clientNum + 1 ) {
			// use predicted origin
			VectorCopy( cl.playerEntityOrigin, ent.origin );
			VectorCopy( cl.playerEntityOrigin, ent.oldorigin );
		} else {	// interpolate origin
            LerpVector( cent->prev.origin, cent->current.origin,
                cl.lerpfrac, ent.origin );
            VectorCopy( ent.origin, ent.oldorigin );
		}

		// create a new entity
	
		// tweak the color of beams
		if ( renderfx & RF_BEAM )
		{	// the four beam colors are encoded in 32 bits of skinnum (hack)
			ent.alpha = 0.30;
			ent.skinnum = (s1->skinnum >> ((rand() % 4)*8)) & 0xff;
			ent.model = 0;
		}
		else
		{
			// set skin
			if (s1->modelindex == 255)
			{	// use custom player skin
				ent.skinnum = 0;
				ci = &cl.clientinfo[s1->skinnum & 0xff];
				ent.skin = ci->skin;
				ent.model = ci->model;
				if (!ent.skin || !ent.model)
				{
					ent.skin = cl.baseclientinfo.skin;
					ent.model = cl.baseclientinfo.model;
                    ci = &cl.baseclientinfo;
				}
//============
//PGM
				if (renderfx & RF_USE_DISGUISE)
				{
                    char buffer[MAX_QPATH];

                    Q_concat( buffer, sizeof( buffer ), "players/", ci->model_name, "/disguise.pcx", NULL );
					ent.skin = ref.RegisterSkin( buffer );
				}
//PGM
//============
			}
			else
			{
				ent.skinnum = s1->skinnum;
				ent.skin = 0;
				ent.model = cl.model_draw[s1->modelindex];
			}
		}

		// only used for black hole model right now, FIXME: do better
		if ((renderfx & RF_TRANSLUCENT) && !(renderfx & RF_BEAM))
			ent.alpha = 0.70;

		// render effects (fullbright, translucent, etc)
		if ((effects & EF_COLOR_SHELL))
			ent.flags = 0;	// renderfx go on color shell entity
		else
			ent.flags = renderfx;

		// calculate angles
		if (effects & EF_ROTATE) {	// some bonus items auto-rotate
			ent.angles[0] = 0;
			ent.angles[1] = autorotate;
			ent.angles[2] = 0;
		}
		// RAFAEL
		else if (effects & EF_SPINNINGLIGHTS)
		{
            vec3_t forward;
            vec3_t start;

			ent.angles[0] = 0;
			ent.angles[1] = anglemod(cl.time/2) + s1->angles[1];
			ent.angles[2] = 180;

            AngleVectors (ent.angles, forward, NULL, NULL);
            VectorMA (ent.origin, 64, forward, start);
            V_AddLight (start, 100, 1, 0, 0);
		} else if( s1->number == cl.frame.clientNum + 1 ) {
			VectorCopy( cl.playerEntityAngles, ent.angles );	// use predicted angles
		} else { // interpolate angles
            LerpAngles(cent->prev.angles, cent->current.angles,
                cl.lerpfrac, ent.angles);

            // mimic original ref_gl "leaning" bug (uuugly!)
            if( s1->modelindex == 255 && cl_rollhack->integer ) {
                ent.angles[ROLL] = -ent.angles[ROLL];
            }
		}

		if( s1->number == cl.frame.clientNum + 1 ) {
			if (effects & EF_FLAG1)
				V_AddLight (ent.origin, 225, 1.0, 0.1, 0.1);
			else if (effects & EF_FLAG2)
				V_AddLight (ent.origin, 225, 0.1, 0.1, 1.0);
			else if (effects & EF_TAGTRAIL)						//PGM
				V_AddLight (ent.origin, 225, 1.0, 1.0, 0.0);	//PGM
			else if (effects & EF_TRACKERTRAIL)					//PGM
				V_AddLight (ent.origin, 225, -1.0, -1.0, -1.0);	//PGM

			if( !cl.thirdPersonView ) {
#if 0
				ent.flags |= RF_VIEWERMODEL;	// only draw from mirrors
#else
                goto skip;
#endif
			}
		}

		// if set to invisible, skip
		if (!s1->modelindex) {
            goto skip;
		}

		if (effects & EF_BFG)
		{
			ent.flags |= RF_TRANSLUCENT;
			ent.alpha = 0.30;
		}

		// RAFAEL
		if (effects & EF_PLASMA)
		{
			ent.flags |= RF_TRANSLUCENT;
			ent.alpha = 0.6;
		}

		if (effects & EF_SPHERETRANS)
		{
			ent.flags |= RF_TRANSLUCENT;
			// PMM - *sigh*  yet more EF overloading
			if (effects & EF_TRACKERTRAIL)
				ent.alpha = 0.6;
			else
				ent.alpha = 0.3;
		}
//pmm

		// add to refresh list
		V_AddEntity (&ent);

		// color shells generate a seperate entity for the main model
		if (effects & EF_COLOR_SHELL)
		{
			// PMM - at this point, all of the shells have been handled
			// if we're in the rogue pack, set up the custom mixing, otherwise just
			// keep going
			if(!strcmp(fs_game->string,"rogue"))
			{
				// all of the solo colors are fine.  we need to catch any of the combinations that look bad
				// (double & half) and turn them into the appropriate color, and make double/quad something special
				if (renderfx & RF_SHELL_HALF_DAM)
				{
                    // ditch the half damage shell if any of red, blue, or double are on
                    if (renderfx & (RF_SHELL_RED|RF_SHELL_BLUE|RF_SHELL_DOUBLE))
                        renderfx &= ~RF_SHELL_HALF_DAM;
				}

				if (renderfx & RF_SHELL_DOUBLE)
				{
                    // lose the yellow shell if we have a red, blue, or green shell
                    if (renderfx & (RF_SHELL_RED|RF_SHELL_BLUE|RF_SHELL_GREEN))
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
			// pmm
			ent.flags = renderfx | RF_TRANSLUCENT;
			ent.alpha = 0.30;
			V_AddEntity (&ent);
		}

		ent.skin = 0;		// never use a custom skin on others
		ent.skinnum = 0;
		ent.flags = 0;
		ent.alpha = 0;

		// duplicate for linked models
		if (s1->modelindex2)
		{
			if (s1->modelindex2 == 255)
			{	// custom weapon
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
			}
			else
				ent.model = cl.model_draw[s1->modelindex2];

			// PMM - check for the defender sphere shell .. make it translucent
			// replaces the previous version which used the high bit on modelindex2 to determine transparency
			if (!Q_strcasecmp (cl.configstrings[CS_MODELS+(s1->modelindex2)], "models/items/shell/tris.md2"))
			{
				ent.alpha = 0.32;
				ent.flags = RF_TRANSLUCENT;
			}
			// pmm

			V_AddEntity (&ent);

			//PGM - make sure these get reset.
			ent.flags = 0;
			ent.alpha = 0;
			//PGM
		}
		if (s1->modelindex3)
		{
			ent.model = cl.model_draw[s1->modelindex3];
			V_AddEntity (&ent);
		}
		if (s1->modelindex4)
		{
			ent.model = cl.model_draw[s1->modelindex4];
			V_AddEntity (&ent);
		}

		if ( effects & EF_POWERSCREEN )
		{
			ent.model = cl_mod_powerscreen;
			ent.oldframe = 0;
			ent.frame = 0;
			ent.flags |= (RF_TRANSLUCENT | RF_SHELL_GREEN);
			ent.alpha = 0.30;
			V_AddEntity (&ent);
		}

		// add automatic particle trails
		if ( (effects&~EF_ROTATE) )
		{
			if (effects & EF_ROCKET)
			{
				if( !( cl_disable_particles->integer & NOPART_ROCKET_TRAIL ) ) {
					CL_RocketTrail( cent->lerp_origin, ent.origin, cent );
				}
				V_AddLight (ent.origin, 200, 1, 1, 0);
			}
			// PGM - Do not reorder EF_BLASTER and EF_HYPERBLASTER. 
			// EF_BLASTER | EF_TRACKER is a special case for EF_BLASTER2... Cheese!
			else if (effects & EF_BLASTER)
			{
//				CL_BlasterTrail (cent->lerp_origin, ent.origin);
//PGM
				if (effects & EF_TRACKER)	// lame... problematic?
				{
					CL_BlasterTrail2 (cent->lerp_origin, ent.origin);
					V_AddLight (ent.origin, 200, 0, 1, 0);		
				}
				else
				{
					CL_BlasterTrail (cent->lerp_origin, ent.origin);
					V_AddLight (ent.origin, 200, 1, 1, 0);
				}
//PGM
			}
			else if (effects & EF_HYPERBLASTER)
			{
				if (effects & EF_TRACKER)						// PGM	overloaded for blaster2.
					V_AddLight (ent.origin, 200, 0, 1, 0);		// PGM
				else											// PGM
					V_AddLight (ent.origin, 200, 1, 1, 0);
			}
			else if (effects & EF_GIB)
			{
				CL_DiminishingTrail (cent->lerp_origin, ent.origin, cent, effects);
			}
			else if (effects & EF_GRENADE)
			{
				if( !( cl_disable_particles->integer & NOPART_GRENADE_TRAIL ) ) {
					CL_DiminishingTrail( cent->lerp_origin, ent.origin, cent, effects );
				}
			}
			else if (effects & EF_FLIES)
			{
				CL_FlyEffect (cent, ent.origin);
			}
			else if (effects & EF_BFG)
			{
				static const int bfg_lightramp[6] = {300, 400, 600, 300, 150, 75};

				if (effects & EF_ANIM_ALLFAST) {
					CL_BfgParticles (&ent);
					i = 200;
				} else {
                    i = s1->frame; clamp( i, 0, 5 );
					i = bfg_lightramp[i];
				}
				V_AddLight (ent.origin, i, 0, 1, 0);
			}
			// RAFAEL
			else if (effects & EF_TRAP)
			{
				ent.origin[2] += 32;
				CL_TrapParticles (&ent);
				i = (rand()%100) + 100;
				V_AddLight (ent.origin, i, 1, 0.8, 0.1);
			}
			else if (effects & EF_FLAG1)
			{
				CL_FlagTrail (cent->lerp_origin, ent.origin, 242);
				V_AddLight (ent.origin, 225, 1, 0.1, 0.1);
			}
			else if (effects & EF_FLAG2)
			{
				CL_FlagTrail (cent->lerp_origin, ent.origin, 115);
				V_AddLight (ent.origin, 225, 0.1, 0.1, 1);
			}
//======
//ROGUE
			else if (effects & EF_TAGTRAIL)
			{
				CL_TagTrail (cent->lerp_origin, ent.origin, 220);
				V_AddLight (ent.origin, 225, 1.0, 1.0, 0.0);
			}
			else if (effects & EF_TRACKERTRAIL)
			{
				if (effects & EF_TRACKER)
				{
					float intensity;

					intensity = 50 + (500 * (sin(cl.time/500.0) + 1.0));
					// FIXME - check out this effect in rendition
					if( scr_glconfig.renderer != GL_RENDERER_SOFTWARE )
						V_AddLight (ent.origin, intensity, -1.0, -1.0, -1.0);
					else
						V_AddLight (ent.origin, -1.0 * intensity, 1.0, 1.0, 1.0);
					}
				else
				{
					CL_Tracker_Shell (cent->lerp_origin);
					V_AddLight (ent.origin, 155, -1.0, -1.0, -1.0);
				}
			}
			else if (effects & EF_TRACKER)
			{
				CL_TrackerTrail (cent->lerp_origin, ent.origin, 0);
				// FIXME - check out this effect in rendition
				if( scr_glconfig.renderer != GL_RENDERER_SOFTWARE )
					V_AddLight (ent.origin, 200, -1, -1, -1);
				else
					V_AddLight (ent.origin, -200, 1, 1, 1);
			}
//ROGUE
//======
			// RAFAEL
			else if (effects & EF_GREENGIB)
			{
				CL_DiminishingTrail (cent->lerp_origin, ent.origin, cent, effects);				
			}
			// RAFAEL
			else if (effects & EF_IONRIPPER)
			{
				CL_IonripperTrail (cent->lerp_origin, ent.origin);
				V_AddLight (ent.origin, 100, 1, 0.5, 0.5);
			}
			// RAFAEL
			else if (effects & EF_BLUEHYPERBLASTER)
			{
				V_AddLight (ent.origin, 200, 0, 0, 1);
			}
			// RAFAEL
			else if (effects & EF_PLASMA)
			{
				if (effects & EF_ANIM_ALLFAST)
				{
					CL_BlasterTrail (cent->lerp_origin, ent.origin);
				}
				V_AddLight (ent.origin, 130, 1, 0.5, 0.5);
			}
		}

skip:
		VectorCopy (ent.origin, cent->lerp_origin);
	}
}


#if 0
static cvar_t *test_model;
static cvar_t *test_pitch;
static cvar_t *test_yaw;

static void CL_AddTestModel( void ) {
	entity_t	test;

    if( !test_model ) {
        test_model = Cvar_Get( "test_model", "", 0 );
        test_pitch = Cvar_Get( "test_pitch", "0", 0 );
        test_yaw = Cvar_Get( "test_yaw", "0", 0 );
    }
    if( !test_model->string[0] ) {
        return;
    }

	memset( &test, 0, sizeof( test ) );
    test.model = ref.RegisterModel( test_model->string );
    if( !test.model ) {
        return;
    }
    test.frame = 1;

    VectorMA( cl.refdef.vieworg, 160, cl.v_forward, test.origin );
//    VectorCopy( cl.refdef.viewangles, test.angles );
    test.angles[YAW]+=test_yaw->value;
    test.angles[PITCH]+=test_pitch->value;
	test.flags = RF_MINLIGHT | RF_DEPTHHACK;

	VectorCopy( test.origin, test.oldorigin );	// don't lerp at all
	V_AddEntity( &test );
}
#endif


/*
==============
CL_AddViewWeapon
==============
*/
static void CL_AddViewWeapon( const player_state_t *ps, const player_state_t *ops ) {
	entity_t	gun;		// view model
	int			i;

	// allow the gun to be completely removed
	if( cl_gun->integer < 1 ) {
		return;
	}

	if( info_hand->integer == 2 ) {
		return;
	}

	// never draw in third person mode
	if( cl.thirdPersonView ) {
		return;
	}

	memset( &gun, 0, sizeof( gun ) );

	if( gun_model ) {
		gun.model = gun_model;	// development tool
	} else {
		gun.model = cl.model_draw[ps->gunindex];
	}
	if( !gun.model ) {
		return;
	}

	// set up gun position
	for( i=0 ; i<3 ; i++ ) {
		gun.origin[i] = cl.refdef.vieworg[i] + ops->gunoffset[i]
			+ cl.lerpfrac * (ps->gunoffset[i] - ops->gunoffset[i]);
		gun.angles[i] = cl.refdef.viewangles[i] + LerpAngle( ops->gunangles[i],
			ps->gunangles[i], cl.lerpfrac );
	}

    // adjust for high fov
    if( cl.frame.ps.fov > 90 ) {
        vec_t ofs = ( 90 - cl.frame.ps.fov ) * 0.2f;
        VectorMA( gun.origin, ofs, cl.v_forward, gun.origin );
    }

	if( gun_frame ) {
		gun.frame = gun_frame;	// development tool
		gun.oldframe = gun_frame;	// development tool
	} else {
		gun.frame = ps->gunframe;
		if( gun.frame == 0 ) {
			gun.oldframe = 0;	// just changed weapons, don't lerp from old
		} else {
			gun.oldframe = ops->gunframe;
		}
	}

	gun.flags = RF_MINLIGHT | RF_DEPTHHACK | RF_WEAPONMODEL;
	if( info_hand->integer == 1 ) {
		gun.flags |= RF_LEFTHAND;
	}
	gun.backlerp = 1.0 - cl.lerpfrac;
	VectorCopy( gun.origin, gun.oldorigin );	// don't lerp at all
	V_AddEntity( &gun );
}

/*
===============
CL_SetupThirdPersionView
===============
*/
static void CL_SetupThirdPersionView( void ) {
	vec3_t focus;
	float fscale, rscale;
	float dist, angle, range;
	trace_t trace;
	static vec3_t mins = { -4, -4, -4 }, maxs = { 4, 4, 4 };

	VectorMA( cl.refdef.vieworg, 512, cl.v_forward, focus );

	cl.refdef.vieworg[2] += 8;

	cl.refdef.viewangles[PITCH] *= 0.5f;
	AngleVectors( cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up );

    angle = DEG2RAD( cl_thirdperson_angle->value );
    range = cl_thirdperson_range->value;
	fscale = cos( angle );
	rscale = sin( angle );
	VectorMA( cl.refdef.vieworg, -range * fscale, cl.v_forward, cl.refdef.vieworg );
	VectorMA( cl.refdef.vieworg, -range * rscale, cl.v_right, cl.refdef.vieworg );

	CM_BoxTrace( &trace, cl.playerEntityOrigin, cl.refdef.vieworg,
            mins, maxs, cl.cm.cache->nodes, MASK_SOLID );
	if( trace.fraction != 1.0f ) {
		VectorCopy( trace.endpos, cl.refdef.vieworg );
	}

	VectorSubtract( focus, cl.refdef.vieworg, focus );
	dist = sqrt( focus[0] * focus[0] + focus[1] * focus[1] );

	cl.refdef.viewangles[PITCH] = -180 / M_PI * atan2( focus[2], dist );
	cl.refdef.viewangles[YAW] -= cl_thirdperson_angle->value;

	cl.thirdPersonView = qtrue;
}

#if USE_SMOOTH_DELTA_ANGLES
static inline float LerpShort( int a2, int a1, float frac ) {
	if (a1 - a2 > 32768)
		a1 &= 65536;
	if (a2 - a1 > 32768)
		a1 &= 65536;
	return a2 + frac * ( a1 - a2 );
}
#endif

/*
===============
CL_CalcViewValues

Sets cl.refdef view values and sound spatalization params
===============
*/
static void CL_CalcViewValues( void ) {
	player_state_t	*ps, *ops;
	vec3_t viewoffset;
	vec3_t kickangles;
    float fov, lerp;
    centity_t *ent;

	// find states to interpolate between
	ps = &cl.frame.ps;
	ops = &cl.oldframe.ps;
	if( !cl.oldframe.valid || cl.oldframe.number != cl.frame.number - 1 ) {
		ops = ps;
	}

	// HACK: see if the player entity was teleported this frame
	if( abs( ops->pmove.origin[0] - ps->pmove.origin[0] ) > 256*8 ||
		abs( ops->pmove.origin[1] - ps->pmove.origin[1] ) > 256*8 ||
		abs( ops->pmove.origin[2] - ps->pmove.origin[2] ) > 256*8 )
	{
		ops = ps;		// don't interpolate
	}

    ent = &cl_entities[cl.frame.clientNum + 1];
    if( ent->serverframe == cl.frame.number &&
        ( ent->current.event == EV_PLAYER_TELEPORT
		|| ent->current.event == EV_OTHER_TELEPORT ) )
    {
		ops = ps;		// don't interpolate
    }

	if( ( ops->pmove.pm_flags ^ ps->pmove.pm_flags ) & PMF_TELEPORT_BIT ) {
		ops = ps;		// don't interpolate
	}

	if( cl.oldframe.clientNum != cl.frame.clientNum ) {
		ops = ps;		// don't interpolate
	}

	lerp = cl.lerpfrac;

	// calculate the origin
	if( !cls.demo.playback && cl_predict->integer && !( ps->pmove.pm_flags & PMF_NO_PREDICTION ) ) {	
		// use predicted values
		unsigned delta = cls.realtime - cl.predicted_step_time;
		float backlerp = lerp - 1.0;

        VectorMA( cl.predicted_origin, backlerp, cl.prediction_error, cl.refdef.vieworg );
		
		// smooth out stair climbing
        if( cl.predicted_step < 16 ) {
            delta <<= 1; // small steps
        }
		if( delta < 100 ) {
			cl.refdef.vieworg[2] -= cl.predicted_step * ( 100 - delta ) * 0.01f;
		}
	} else {	
		// just use interpolated values
		cl.refdef.vieworg[0] = ops->pmove.origin[0] * 0.125f +
			lerp * ( ps->pmove.origin[0] - ops->pmove.origin[0] ) * 0.125f;
		cl.refdef.vieworg[1] = ops->pmove.origin[1] * 0.125f +
			lerp * ( ps->pmove.origin[1] - ops->pmove.origin[1] ) * 0.125f;
		cl.refdef.vieworg[2] = ops->pmove.origin[2] * 0.125f +
			lerp * ( ps->pmove.origin[2] - ops->pmove.origin[2] ) * 0.125f;
	}

	// if not running a demo or on a locked frame, add the local angle movement
	if( cls.demo.playback ) {
		if( cls.key_dest == KEY_GAME && Key_IsDown( K_SHIFT ) ) {
			VectorCopy( cl.viewangles, cl.refdef.viewangles );
		} else {
			LerpAngles( ops->viewangles, ps->viewangles, lerp,
                cl.refdef.viewangles );
		}
	} else if( ps->pmove.pm_type < PM_DEAD ) {	
		// use predicted values
		VectorCopy( cl.predicted_angles, cl.refdef.viewangles );
	} else if( ops->pmove.pm_type < PM_DEAD && cls.serverProtocol > PROTOCOL_VERSION_DEFAULT ) {
		// lerp from predicted angles, since enhanced servers
		// do not send viewangles each frame
		LerpAngles( cl.predicted_angles, ps->viewangles, lerp, cl.refdef.viewangles );
	} else {
		// just use interpolated values
		LerpAngles( ops->viewangles, ps->viewangles, lerp, cl.refdef.viewangles );
	}
	
	LerpVector( ops->viewoffset, ps->viewoffset, lerp, viewoffset );

#if USE_SMOOTH_DELTA_ANGLES
	cl.delta_angles[0] = LerpShort( ops->pmove.delta_angles[0], ps->pmove.delta_angles[0], lerp );
	cl.delta_angles[1] = LerpShort( ops->pmove.delta_angles[1], ps->pmove.delta_angles[1], lerp );
	cl.delta_angles[2] = LerpShort( ops->pmove.delta_angles[2], ps->pmove.delta_angles[2], lerp );
#endif

	if( cls.demo.playback && ( info_uf->integer & UF_LOCALFOV ) ) {
		fov = info_fov->value;
        if( fov < 1 ) {
            fov = 90;
        } else if( fov > 160 ) {
            fov = 160;
        }
        cl.refdef.fov_x = fov;
	} else {
	    // interpolate field of view
		cl.refdef.fov_x = ops->fov + lerp * ( ps->fov - ops->fov );
	}

	// don't interpolate blend color
	if( cl_add_blend->integer ) {
        Vector4Copy( ps->blend, cl.refdef.blend );
	} else {
        Vector4Clear( cl.refdef.blend );
	}

	AngleVectors( cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up );

	VectorCopy( cl.refdef.vieworg, cl.playerEntityOrigin );
	VectorCopy( cl.refdef.viewangles, cl.playerEntityAngles );

	if( cl.playerEntityAngles[PITCH] > 180 ) {
		cl.playerEntityAngles[PITCH] -= 360;
	}

	cl.playerEntityAngles[PITCH] = cl.playerEntityAngles[PITCH] / 3;

	VectorAdd( cl.refdef.vieworg, viewoffset, cl.refdef.vieworg );

    VectorCopy( cl.refdef.vieworg, listener_origin );
    VectorCopy( cl.v_forward, listener_forward );
    VectorCopy( cl.v_right, listener_right );
    VectorCopy( cl.v_up, listener_up );

	if( cl_thirdperson->integer && cl.frame.clientNum != CLIENTNUM_NONE ) {
		// if dead, set a nice view angle
		if( ps->stats[STAT_HEALTH] <= 0 ) {
			cl.refdef.viewangles[ROLL] = 0;
			cl.refdef.viewangles[PITCH] = 10;
		} 
		CL_SetupThirdPersionView();
	} else {
		// add kick angles
		if( cl_kickangles->integer ) {
			LerpAngles( ops->kick_angles, ps->kick_angles, lerp, kickangles );
			VectorAdd( cl.refdef.viewangles, kickangles, cl.refdef.viewangles );
		}

		// add the weapon
		CL_AddViewWeapon( ps, ops );
        //CL_AddTestModel();

	    cl.thirdPersonView = qfalse;
	}
}


/*
===============
CL_AddEntities

Emits all entities, particles, and lights to the refresh
===============
*/
void CL_AddEntities( void ) {
	CL_CalcViewValues();
	CL_AddPacketEntities();
	CL_AddTEnts();
	CL_AddParticles();
	CL_AddDLights();
	CL_AddLightStyles();
	LOC_AddLocationsToScene();
}



/*
===============
CL_GetEntitySoundOrigin

Called to get the sound spatialization origin
===============
*/
void CL_GetEntitySoundOrigin( int entnum, vec3_t org ) {
	centity_t	*ent;
	cmodel_t	*cm;
	vec3_t      mid;

	if( entnum < 0 || entnum >= MAX_EDICTS ) {
		Com_Error( ERR_DROP, "CL_GetEntitySoundOrigin: bad entnum: %d", entnum );
	}
	if( !entnum ) {
		// should this ever happen?
        VectorCopy( listener_origin, org );
		return;
	}
	ent = &cl_entities[entnum];
	VectorCopy( ent->lerp_origin, org );

	if( ent->current.solid == 31 ) {
		cm = cl.model_clip[ent->current.modelindex];
		if( cm ) {
            VectorAvg( cm->mins, cm->maxs, mid );
			VectorAdd( org, mid, org );
		}
	}
}

