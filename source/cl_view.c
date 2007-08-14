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
// cl_view.c -- player rendering positioning

#include "cl_local.h"

//=============
//
// development tools for weapons
//
int			gun_frame;
qhandle_t	gun_model;

//=============

cvar_t		*cl_testparticles;
cvar_t		*cl_testentities;
cvar_t		*cl_testlights;
cvar_t		*cl_testblend;

cvar_t		*cl_stats;


int			r_numdlights;
dlight_t	r_dlights[MAX_DLIGHTS];

int			r_numentities;
entity_t	r_entities[MAX_ENTITIES];

int			r_numparticles;
particle_t	r_particles[MAX_PARTICLES];

lightstyle_t	r_lightstyles[MAX_LIGHTSTYLES];

/*
====================
V_ClearScene

Specifies the model that will be used as the world
====================
*/
void V_ClearScene (void)
{
	r_numdlights = 0;
	r_numentities = 0;
	r_numparticles = 0;
}


/*
=====================
V_AddEntity

=====================
*/
void V_AddEntity (entity_t *ent)
{
	if (r_numentities >= MAX_ENTITIES)
		return;
	
	r_entities[r_numentities++] = *ent;
}


/*
=====================
V_AddParticle

=====================
*/
void V_AddParticle( particle_t *p )
{
	if (r_numparticles >= MAX_PARTICLES)
		return;
	r_particles[r_numparticles++] = *p;

}

/*
=====================
V_AddLight

=====================
*/
void V_AddLight (vec3_t org, float intensity, float r, float g, float b)
{
	dlight_t	*dl;

	if (r_numdlights >= MAX_DLIGHTS)
		return;
	dl = &r_dlights[r_numdlights++];
	VectorCopy (org, dl->origin);
	dl->intensity = intensity;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
}


/*
=====================
V_AddLightStyle

=====================
*/
void V_AddLightStyle (int style, vec4_t value)
{
	lightstyle_t	*ls;

	if (style < 0 || style > MAX_LIGHTSTYLES)
		Com_Error (ERR_DROP, "Bad light style %i", style);
	ls = &r_lightstyles[style];

	//ls->white = r+g+b;
	ls->rgb[0] = value[0];
	ls->rgb[1] = value[1];
	ls->rgb[2] = value[2];
	ls->white = value[3];
}

/*
================
V_TestParticles

If cl_testparticles is set, create 4096 particles in the view
================
*/
void V_TestParticles (void)
{
	particle_t	*p;
	int			i, j;
	float		d, r, u;

	r_numparticles = MAX_PARTICLES;
	for (i=0 ; i<r_numparticles ; i++)
	{
		d = i*0.25;
		r = 4*((i&7)-3.5);
		u = 4*(((i>>3)&7)-3.5);
		p = &r_particles[i];

		for (j=0 ; j<3 ; j++)
			p->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j]*d +
			cl.v_right[j]*r + cl.v_up[j]*u;

		p->color = 8;
		p->alpha = cl_testparticles->value;
	}
}

/*
================
V_TestEntities

If cl_testentities is set, create 32 player models
================
*/
void V_TestEntities (void)
{
	int			i, j;
	float		f, r;
	entity_t	*ent;

	r_numentities = 32;
	memset (r_entities, 0, sizeof(r_entities));

	for (i=0 ; i<r_numentities ; i++)
	{
		ent = &r_entities[i];

		r = 64 * ( (i%4) - 1.5 );
		f = 64 * (i/4) + 128;

		for (j=0 ; j<3 ; j++)
			ent->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j]*f +
			cl.v_right[j]*r;

		ent->model = cl.baseclientinfo.model;
		ent->skin = cl.baseclientinfo.skin;
	}
}

/*
================
V_TestLights

If cl_testlights is set, create 32 lights models
================
*/
void V_TestLights (void)
{
	int			i, j;
	float		f, r;
	dlight_t	*dl;

    if( cl_testlights->integer == 2 ) {
        dl = &r_dlights[0];
        r_numdlights = 1;
        
        VectorMA( cl.refdef.vieworg, 256, cl.v_forward, dl->origin );
        VectorSet( dl->color, 1, 1, 1  );
        dl->intensity = 256;
        return;
    }
    
	r_numdlights = 32;
	memset (r_dlights, 0, sizeof(r_dlights));

	for (i=0 ; i<r_numdlights ; i++)
	{
		dl = &r_dlights[i];

		r = 64 * ( (i%4) - 1.5 );
		f = 64 * (i/4) + 128;

		for (j=0 ; j<3 ; j++)
			dl->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j]*f +
			cl.v_right[j]*r;
		dl->color[0] = ((i%6)+1) & 1;
		dl->color[1] = (((i%6)+1) & 2)>>1;
		dl->color[2] = (((i%6)+1) & 4)>>2;
		dl->intensity = 200;
	}
}

//===================================================================

/*
=================
CL_PrepRefresh

Call before entering a new level, or after changing dlls
=================
*/
void CL_PrepRefresh (void)
{
	int			i;
	char		*name;
	float		rotate;
	vec3_t		axis;
	int			time_start, time_map, time_models, time_clients, time_total;

	if (!cl.mapname[0])
		return;		// no map loaded

	time_start = Sys_Milliseconds();

	Con_Close();
	UI_OpenMenu( UIMENU_NONE );

	// register models, pics, and skins
	SCR_LoadingString( cl.configstrings[CS_MODELS+1] );
	ref.BeginRegistration( cl.mapname );
	time_map = Sys_Milliseconds();

	// precache status bar pics
	SCR_LoadingString( "pics" );
	SCR_TouchPics ();

	SCR_LoadingString( "models" );

	CL_RegisterTEntModels ();

	cl.numWeaponModels = 1;
	strcpy(cl.weaponModels[0], "weapon.md2");

	for (i=1 ; i<MAX_MODELS ; i++) {
		name = cl.configstrings[CS_MODELS+i];
        if( !name[0] ) {
            break;
        }
		if (name[0] == '#') {
			// special player weapon model
			if (cl.numWeaponModels < MAX_CLIENTWEAPONMODELS) {
				strcpy( cl.weaponModels[cl.numWeaponModels++], name + 1 );
			}
		}  else {
			cl.model_draw[i] = ref.RegisterModel( name );
			if( name[0] == '*' )
				cl.model_clip[i] = CM_InlineModel( &cl.cm, name );
			else
				cl.model_clip[i] = NULL;
		}
	}
	time_models = Sys_Milliseconds();

	SCR_LoadingString( "images" );
	for (i=1 ; i<MAX_IMAGES; i++) {
        name = cl.configstrings[CS_IMAGES+i];
        if( !name[0] ) {
            break;
        }
		cl.image_precache[i] = ref.RegisterPic (name);
	}

	SCR_LoadingString( "clients" );
	for (i=0 ; i<MAX_CLIENTS ; i++)
	{
        name = cl.configstrings[CS_PLAYERSKINS+i];
        if( !name[0] )
			continue;

    	CL_LoadClientinfo( &cl.clientinfo[i], name );
	}

	CL_LoadClientinfo (&cl.baseclientinfo, "unnamed\\male/grunt");
	time_clients = Sys_Milliseconds();

	SCR_LoadingString( "sky" );

	// set sky textures and speed
	rotate = atof (cl.configstrings[CS_SKYROTATE]);
	sscanf (cl.configstrings[CS_SKYAXIS], "%f %f %f", 
		&axis[0], &axis[1], &axis[2]);
	ref.SetSky (cl.configstrings[CS_SKY], rotate, axis);

	SCR_LoadingString( "" );

	// the renderer can now free unneeded stuff
	ref.EndRegistration ();

	FS_FlushCache();

	// clear any lines of console text
	Con_ClearNotify_f ();

	SCR_UpdateScreen ();

	time_total = Sys_Milliseconds();
	Com_DPrintf( "Map loaded in %d msec (%d,%d,%d,%d)\n", time_total - time_start,
        time_map - time_start, time_models - time_map, time_clients - time_models,
        time_total - time_clients );	
}


//============================================================================

// gun frame debugging functions
void V_Gun_Next_f (void)
{
	gun_frame++;
	Com_Printf ("frame %i\n", gun_frame);
}

void V_Gun_Prev_f (void)
{
	gun_frame--;
	if (gun_frame < 0)
		gun_frame = 0;
	Com_Printf ("frame %i\n", gun_frame);
}

void V_Gun_Model_f (void)
{
	char	name[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		gun_model = 0;
		return;
	}
	Com_sprintf (name, sizeof(name), "models/%s/tris.md2", Cmd_Argv(1));
	gun_model = ref.RegisterModel (name);
}

//============================================================================

static int entitycmpfnc( const entity_t *a, const entity_t *b )
{
	/*
	** all other models are sorted by model then skin
	*/
	if ( a->model == b->model )
	{
		return ( ( int ) a->skin - ( int ) b->skin );
	}
	else
	{
		return ( ( int ) a->model - ( int ) b->model );
	}
}

void V_SetLightLevel( void ) {
	vec3_t shadelight;

	// save off light value for server to look at (BIG HACK!)
	ref.LightPoint( cl.refdef.vieworg, shadelight );

	// pick the greatest component, which should be the same
	// as the mono value returned by software
	if( shadelight[0] > shadelight[1] ) {
		if( shadelight[0] > shadelight[2] ) {
			cl.lightlevel = 150.0f*shadelight[0];
		} else {
			cl.lightlevel = 150.0f*shadelight[2];
		}
	} else {
		if( shadelight[1] > shadelight[2] ) {
			cl.lightlevel = 150.0f*shadelight[1];
		} else {
			cl.lightlevel = 150.0f*shadelight[2];
		}
	}
}

/*
==================
V_RenderView

==================
*/
void V_RenderView( void ) {
	// an invalid frame will just use the exact previous refdef
	// we can't use the old frame if the video mode has changed, though...
	if( cl.frame.valid ) {
		V_ClearScene ();

		// build a refresh entity list and calc cl.sim*
		// this also calls CL_CalcViewValues which loads
		// v_forward, etc.
		CL_AddEntities ();

		if (cl_testparticles->integer)
			V_TestParticles ();
		if (cl_testentities->integer)
			V_TestEntities ();
		if (cl_testlights->integer)
			V_TestLights ();
		if (cl_testblend->integer)
		{
			cl.refdef.blend[0] = 1;
			cl.refdef.blend[1] = 0.5;
			cl.refdef.blend[2] = 0.25;
			cl.refdef.blend[3] = 0.5;
		}

		// never let it sit exactly on a node line, because a water plane can
		// dissapear when viewed with the eye exactly on it.
		// the server protocol only specifies to 1/8 pixel, so add 1/16 in each axis
		cl.refdef.vieworg[0] += 1.0/16;
		cl.refdef.vieworg[1] += 1.0/16;
		cl.refdef.vieworg[2] += 1.0/16;

		cl.refdef.x = scr_vrect.x;
		cl.refdef.y = scr_vrect.y;
		cl.refdef.width = scr_vrect.width;
		cl.refdef.height = scr_vrect.height;

		cl.refdef.fov_y = Com_CalcFov (cl.refdef.fov_x, cl.refdef.width, cl.refdef.height);
		cl.refdef.time = cl.time*0.001;

        if( cl.frame.areabytes ) {
    		cl.refdef.areabits = cl.frame.areabits;
        } else {
            cl.refdef.areabits = NULL;
        }

		if (!cl_add_entities->integer)
			r_numentities = 0;
		if (!cl_add_particles->integer)
			r_numparticles = 0;
		if (!cl_add_lights->integer)
			r_numdlights = 0;
		//if (!cl_add_blend->value)
		//{
		//	VectorClear (cl.refdef.blend);
		//}

		cl.refdef.num_entities = r_numentities;
		cl.refdef.entities = r_entities;
		cl.refdef.num_particles = r_numparticles;
		cl.refdef.particles = r_particles;
		cl.refdef.num_dlights = r_numdlights;
		cl.refdef.dlights = r_dlights;
		cl.refdef.lightstyles = r_lightstyles;

		cl.refdef.rdflags = cl.frame.ps.rdflags;

		// sort entities for better cache locality
        qsort( cl.refdef.entities, cl.refdef.num_entities, sizeof( cl.refdef.entities[0] ), (int (*)(const void *, const void *))entitycmpfnc );
	}

	ref.RenderFrame (&cl.refdef);
	if (cl_stats->integer)
		Com_Printf ("ent:%i  lt:%i  part:%i\n", r_numentities, r_numdlights, r_numparticles);

	V_SetLightLevel();

}


/*
=============
V_Viewpos_f
=============
*/
void V_Viewpos_f (void)
{
	Com_Printf ("(%i %i %i) : %i\n", (int)cl.refdef.vieworg[0],
		(int)cl.refdef.vieworg[1], (int)cl.refdef.vieworg[2], 
		(int)cl.refdef.viewangles[YAW]);
}

/*
=============
V_Init
=============
*/
void V_Init (void)
{
	Cmd_AddCommand ("gun_next", V_Gun_Next_f);
	Cmd_AddCommand ("gun_prev", V_Gun_Prev_f);
	Cmd_AddCommand ("gun_model", V_Gun_Model_f);

	Cmd_AddCommand ("viewpos", V_Viewpos_f);

	crosshair = Cvar_Get ("crosshair", "0", CVAR_ARCHIVE);

	cl_testblend = Cvar_Get ("cl_testblend", "0", 0);
	cl_testparticles = Cvar_Get ("cl_testparticles", "0", 0);
	cl_testentities = Cvar_Get ("cl_testentities", "0", 0);
	cl_testlights = Cvar_Get ("cl_testlights", "0", CVAR_CHEAT);

	cl_stats = Cvar_Get ("cl_stats", "0", 0);
}


