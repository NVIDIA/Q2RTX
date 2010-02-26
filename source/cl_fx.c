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
// cl_fx.c -- entity effects parsing and management

#include "cl_local.h"

void CL_LogoutEffect (vec3_t org, int type);
void CL_ItemRespawnParticles (vec3_t org);

static vec3_t avelocities [NUMVERTEXNORMALS];

extern  qhandle_t cl_mod_smoke;
extern  qhandle_t cl_mod_flash;

/*
==============================================================

LIGHT STYLE MANAGEMENT

==============================================================
*/

typedef struct clightstyle_s {
    list_t  entry;
    int     length;
    vec4_t  value;
    float   map[MAX_QPATH];
} clightstyle_t;

static clightstyle_t    cl_lightstyles[MAX_LIGHTSTYLES];
static LIST_DECL( cl_lightlist );
static int          cl_lastofs;

/*
================
CL_ClearLightStyles
================
*/
void CL_ClearLightStyles( void ) {
    memset( cl_lightstyles, 0, sizeof( cl_lightstyles ) );
    List_Init( &cl_lightlist );
    cl_lastofs = -1;
}

/*
================
CL_RunLightStyles
================
*/
void CL_RunLightStyles( void ) {
    int     ofs;
    clightstyle_t   *ls;

    ofs = cl.time / 100;
    if( ofs == cl_lastofs )
        return;
    cl_lastofs = ofs;

    LIST_FOR_EACH( clightstyle_t, ls, &cl_lightlist, entry ) {
        ls->value[0] = ls->value[1] = ls->value[2] = ls->value[3] =
            ls->map[ofs % ls->length];
    }
}


void CL_SetLightstyle( int index, const char *string, size_t length ) {
    int     i;
    clightstyle_t   *dest;

    dest = &cl_lightstyles[index];
    dest->length = length;

    for( i = 0; i < length; i++ ) {
        dest->map[i] = ( float )( string[i] - 'a' ) / ( float )( 'm' - 'a' );
    }

    if( dest->entry.prev ) {
        List_Delete( &dest->entry );
    }

    if( length > 1 ) {
        List_Append( &cl_lightlist, &dest->entry );
        return;
    }

    dest->value[0] = dest->value[1] = dest->value[2] = dest->value[3] = dest->map[0] = 1;
}

/*
================
CL_AddLightStyles
================
*/
void CL_AddLightStyles( void ) {
    int     i;
    clightstyle_t   *ls;

    for( i = 0, ls = cl_lightstyles; i < MAX_LIGHTSTYLES; i++, ls++ )
        V_AddLightStyle( i, ls->value );
}

/*
==============================================================

DLIGHT MANAGEMENT

==============================================================
*/

cdlight_t       cl_dlights[MAX_DLIGHTS];

/*
================
CL_ClearDlights
================
*/
void CL_ClearDlights (void)
{
    memset (cl_dlights, 0, sizeof(cl_dlights));
}

/*
===============
CL_AllocDlight

===============
*/
cdlight_t *CL_AllocDlight (int key)
{
    int     i;
    cdlight_t   *dl;

// first look for an exact key match
    if (key)
    {
        dl = cl_dlights;
        for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
        {
            if (dl->key == key)
            {
                memset (dl, 0, sizeof(*dl));
                dl->key = key;
                return dl;
            }
        }
    }

// then look for anything else
    dl = cl_dlights;
    for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
    {
        if (dl->die < cl.time)
        {
            memset (dl, 0, sizeof(*dl));
            dl->key = key;
            return dl;
        }
    }

    dl = &cl_dlights[0];
    memset (dl, 0, sizeof(*dl));
    dl->key = key;
    return dl;
}

/*
===============
CL_NewDlight
===============
*/
void CL_NewDlight (int key, float x, float y, float z, float radius, float time)
{
    cdlight_t   *dl;

    dl = CL_AllocDlight (key);
    dl->origin[0] = x;
    dl->origin[1] = y;
    dl->origin[2] = z;
    dl->radius = radius;
    dl->die = cl.time + time;
}


/*
===============
CL_RunDLights

===============
*/
void CL_RunDLights (void)
{
    int         i;
    cdlight_t   *dl;

    dl = cl_dlights;
    for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
    {
        if (!dl->radius)
            continue;
        
        if (dl->die < cl.time)
        {
            dl->radius = 0;
            return;
        }
        dl->radius -= cls.frametime*dl->decay;
        if (dl->radius < 0)
            dl->radius = 0;
    }
}

/*
==============================================================

LASER BEAM MANAGEMENT

==============================================================
*/

#define LASER_FADE_NOT      1
#define LASER_FADE_ALPHA    2
#define LASER_FADE_RGBA     3

typedef struct {
    entity_t    ent;
    vec3_t      start;
    vec3_t      end;
    int         fadeType;
    qboolean    indexed;
    color_t     color;
    float       width;
    int         lifeTime;
    int         startTime;
} laser_t;

#define MAX_LASERS  32

static laser_t     cl_lasers[MAX_LASERS];

static laser_t *alloc_laser( void ) {
    laser_t *l;
    int i;

    for( i=0, l=cl_lasers ; i<MAX_LASERS ; i++, l++ ) {
        if( cl.time - l->startTime >= l->lifeTime ) {
            memset( l, 0, sizeof( *l ) );
            l->startTime = cl.time;
            return l;
        }
    }

    return NULL;
}

void CL_AddLasers( void ) {
    laser_t     *l;
    entity_t    ent;
    int         i;
    //color_t       color;
    int time;
    float f;

    memset( &ent, 0, sizeof( ent ) );

    for( i = 0, l = cl_lasers; i < MAX_LASERS; i++, l++ ) {
        time = l->lifeTime - ( cl.time - l->startTime );
        if( time < 0 ) {
            continue;
        }

        ent.alpha = l->color[3] / 255.0f;

        if( l->fadeType != LASER_FADE_NOT ) {
            f = (float)time / (float)l->lifeTime;

            ent.alpha *= f;
            /*if( l->fadeType == LASER_FADE_RGBA ) {
                *(int *)color = *(int *)l->color;
                color[0] *= f;
                color[1] *= f;
                color[2] *= f;
                ent.skinnum = *(int *)color;
            }*/
        } /*else*/ {
            ent.skinnum = *(int *)l->color;
        }

        ent.flags = RF_TRANSLUCENT|RF_BEAM;
        VectorCopy( l->start, ent.origin );
        VectorCopy( l->end, ent.oldorigin );
        ent.frame = l->width;
        ent.lightstyle = !l->indexed;

        V_AddEntity( &ent );
    }
}

/*
=================
CL_ParseLaser
=================
*/
void CL_ParseLaser( int colors ) {
    laser_t *l;

    l = alloc_laser();
    if (!l)
        return;

    VectorCopy( te.pos1, l->start );
    VectorCopy( te.pos2, l->end );
    l->fadeType = LASER_FADE_NOT;
    l->lifeTime = 100;
    l->indexed = qtrue;
    l->color[0] = ( colors >> ( ( rand() % 4 ) * 8 ) ) & 0xff;
    l->color[1] = 0;
    l->color[2] = 0;
    l->color[3] = 77;
    l->width = 4;
}

// ==============================================================

/*
==============
CL_ParseMuzzleFlash
==============
*/
void CL_ParseMuzzleFlash (void)
{
    vec3_t      fv, rv;
    cdlight_t   *dl;
    int         i, weapon;
    centity_t   *pl;
    int         silenced;
    float       volume;
    char        soundname[MAX_QPATH];

    i = MSG_ReadShort ();
    if (i < 1 || i >= MAX_EDICTS)
        Com_Error (ERR_DROP, "CL_ParseMuzzleFlash: bad entity");

    weapon = MSG_ReadByte ();
    silenced = weapon & MZ_SILENCED;
    weapon &= ~MZ_SILENCED;

    pl = &cl_entities[i];

    dl = CL_AllocDlight (i);
    VectorCopy (pl->current.origin,  dl->origin);
    AngleVectors (pl->current.angles, fv, rv, NULL);
    VectorMA (dl->origin, 18, fv, dl->origin);
    VectorMA (dl->origin, 16, rv, dl->origin);
    if (silenced)
        dl->radius = 100 + (rand()&31);
    else
        dl->radius = 200 + (rand()&31);
    dl->minlight = 32;
    dl->die = cl.time; // + 0.1;

    if (silenced)
        volume = 0.2;
    else
        volume = 1;

    switch (weapon)
    {
    case MZ_BLASTER:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/blastf1a.wav"), volume, ATTN_NORM, 0);
        break;
    case MZ_BLUEHYPERBLASTER:
        dl->color[0] = 0;dl->color[1] = 0;dl->color[2] = 1;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/hyprbf1a.wav"), volume, ATTN_NORM, 0);
        break;
    case MZ_HYPERBLASTER:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/hyprbf1a.wav"), volume, ATTN_NORM, 0);
        break;
    case MZ_MACHINEGUN:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        Q_snprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound(soundname), volume, ATTN_NORM, 0);
        break;
    case MZ_SHOTGUN:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/shotgf1b.wav"), volume, ATTN_NORM, 0);
        S_StartSound (NULL, i, CHAN_AUTO,   S_RegisterSound("weapons/shotgr1b.wav"), volume, ATTN_NORM, 0.1);
        break;
    case MZ_SSHOTGUN:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/sshotf1b.wav"), volume, ATTN_NORM, 0);
        break;
    case MZ_CHAINGUN1:
        dl->radius = 200 + (rand()&31);
        dl->color[0] = 1;dl->color[1] = 0.25;dl->color[2] = 0;
        Q_snprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound(soundname), volume, ATTN_NORM, 0);
        break;
    case MZ_CHAINGUN2:
        dl->radius = 225 + (rand()&31);
        dl->color[0] = 1;dl->color[1] = 0.5;dl->color[2] = 0;
        dl->die = cl.time  + 0.1;   // long delay
        Q_snprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound(soundname), volume, ATTN_NORM, 0);
        Q_snprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound(soundname), volume, ATTN_NORM, 0.05);
        break;
    case MZ_CHAINGUN3:
        dl->radius = 250 + (rand()&31);
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        dl->die = cl.time  + 0.1;   // long delay
        Q_snprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound(soundname), volume, ATTN_NORM, 0);
        Q_snprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound(soundname), volume, ATTN_NORM, 0.033);
        Q_snprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (rand() % 5) + 1);
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound(soundname), volume, ATTN_NORM, 0.066);
        break;
    case MZ_RAILGUN:
        dl->color[0] = 0.5;dl->color[1] = 0.5;dl->color[2] = 1.0;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/railgf1a.wav"), volume, ATTN_NORM, 0);
        break;
    case MZ_ROCKET:
        dl->color[0] = 1;dl->color[1] = 0.5;dl->color[2] = 0.2;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/rocklf1a.wav"), volume, ATTN_NORM, 0);
        S_StartSound (NULL, i, CHAN_AUTO,   S_RegisterSound("weapons/rocklr1b.wav"), volume, ATTN_NORM, 0.1);
        break;
    case MZ_GRENADE:
        dl->color[0] = 1;dl->color[1] = 0.5;dl->color[2] = 0;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/grenlf1a.wav"), volume, ATTN_NORM, 0);
        S_StartSound (NULL, i, CHAN_AUTO,   S_RegisterSound("weapons/grenlr1b.wav"), volume, ATTN_NORM, 0.1);
        break;
    case MZ_BFG:
        dl->color[0] = 0;dl->color[1] = 1;dl->color[2] = 0;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/bfg__f1y.wav"), volume, ATTN_NORM, 0);
        break;

    case MZ_LOGIN:
        dl->color[0] = 0;dl->color[1] = 1; dl->color[2] = 0;
        dl->die = cl.time + 1.0;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/grenlf1a.wav"), 1, ATTN_NORM, 0);
        CL_LogoutEffect (pl->current.origin, weapon);
        break;
    case MZ_LOGOUT:
        dl->color[0] = 1;dl->color[1] = 0; dl->color[2] = 0;
        dl->die = cl.time + 1.0;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/grenlf1a.wav"), 1, ATTN_NORM, 0);
        CL_LogoutEffect (pl->current.origin, weapon);
        break;
    case MZ_RESPAWN:
        dl->color[0] = 1;dl->color[1] = 1; dl->color[2] = 0;
        dl->die = cl.time + 1.0;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/grenlf1a.wav"), 1, ATTN_NORM, 0);
        CL_LogoutEffect (pl->current.origin, weapon);
        break;
    // RAFAEL
    case MZ_PHALANX:
        dl->color[0] = 1;dl->color[1] = 0.5; dl->color[2] = 0.5;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/plasshot.wav"), volume, ATTN_NORM, 0);
        break;
    // RAFAEL
    case MZ_IONRIPPER:  
        dl->color[0] = 1;dl->color[1] = 0.5; dl->color[2] = 0.5;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/rippfiref.wav"), volume, ATTN_NORM, 0);
        break;

// ======================
// PGM
    case MZ_ETF_RIFLE:
        dl->color[0] = 0.9;dl->color[1] = 0.7;dl->color[2] = 0;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/nail1.wav"), volume, ATTN_NORM, 0);
        break;
    case MZ_SHOTGUN2:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/shotg2.wav"), volume, ATTN_NORM, 0);
        break;
    case MZ_HEATBEAM:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        dl->die = cl.time + 100;
//      S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/bfg__l1a.wav"), volume, ATTN_NORM, 0);
        break;
    case MZ_BLASTER2:
        dl->color[0] = 0;dl->color[1] = 1;dl->color[2] = 0;
        // FIXME - different sound for blaster2 ??
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/blastf1a.wav"), volume, ATTN_NORM, 0);
        break;
    case MZ_TRACKER:
        // negative flashes handled the same in gl/soft until CL_AddDLights
        dl->color[0] = -1;dl->color[1] = -1;dl->color[2] = -1;
        S_StartSound (NULL, i, CHAN_WEAPON, S_RegisterSound("weapons/disint2.wav"), volume, ATTN_NORM, 0);
        break;      
    case MZ_NUKE1:
        dl->color[0] = 1;dl->color[1] = 0;dl->color[2] = 0;
        dl->die = cl.time + 100;
        break;
    case MZ_NUKE2:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        dl->die = cl.time + 100;
        break;
    case MZ_NUKE4:
        dl->color[0] = 0;dl->color[1] = 0;dl->color[2] = 1;
        dl->die = cl.time + 100;
        break;
    case MZ_NUKE8:
        dl->color[0] = 0;dl->color[1] = 1;dl->color[2] = 1;
        dl->die = cl.time + 100;
        break;
// PGM
// ======================
    }
}


/*
==============
CL_ParseMuzzleFlash2
==============
*/
void CL_ParseMuzzleFlash2 (void) 
{
    int         ent;
    vec3_t      origin;
    int         flash_number;
    cdlight_t   *dl;
    vec3_t      forward, right;
    char        soundname[64];

    ent = MSG_ReadShort ();
    if (ent < 1 || ent >= MAX_EDICTS)
        Com_Error (ERR_DROP, "CL_ParseMuzzleFlash2: bad entity");

    flash_number = MSG_ReadByte ();
    if( flash_number == -1 ) {
        Com_Error (ERR_DROP, "CL_ParseMuzzleFlash2: read past end of message");
    }

    // locate the origin
    AngleVectors (cl_entities[ent].current.angles, forward, right, NULL);
    origin[0] = cl_entities[ent].current.origin[0] + forward[0] * monster_flash_offset[flash_number][0] + right[0] * monster_flash_offset[flash_number][1];
    origin[1] = cl_entities[ent].current.origin[1] + forward[1] * monster_flash_offset[flash_number][0] + right[1] * monster_flash_offset[flash_number][1];
    origin[2] = cl_entities[ent].current.origin[2] + forward[2] * monster_flash_offset[flash_number][0] + right[2] * monster_flash_offset[flash_number][1] + monster_flash_offset[flash_number][2];

    dl = CL_AllocDlight (ent);
    VectorCopy (origin,  dl->origin);
    dl->radius = 200 + (rand()&31);
    dl->minlight = 32;
    dl->die = cl.time;  // + 0.1;

    switch (flash_number)
    {
    case MZ2_INFANTRY_MACHINEGUN_1:
    case MZ2_INFANTRY_MACHINEGUN_2:
    case MZ2_INFANTRY_MACHINEGUN_3:
    case MZ2_INFANTRY_MACHINEGUN_4:
    case MZ2_INFANTRY_MACHINEGUN_5:
    case MZ2_INFANTRY_MACHINEGUN_6:
    case MZ2_INFANTRY_MACHINEGUN_7:
    case MZ2_INFANTRY_MACHINEGUN_8:
    case MZ2_INFANTRY_MACHINEGUN_9:
    case MZ2_INFANTRY_MACHINEGUN_10:
    case MZ2_INFANTRY_MACHINEGUN_11:
    case MZ2_INFANTRY_MACHINEGUN_12:
    case MZ2_INFANTRY_MACHINEGUN_13:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        CL_ParticleEffect (origin, vec3_origin, 0, 40);
        CL_SmokeAndFlash(origin);
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("infantry/infatck1.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_SOLDIER_MACHINEGUN_1:
    case MZ2_SOLDIER_MACHINEGUN_2:
    case MZ2_SOLDIER_MACHINEGUN_3:
    case MZ2_SOLDIER_MACHINEGUN_4:
    case MZ2_SOLDIER_MACHINEGUN_5:
    case MZ2_SOLDIER_MACHINEGUN_6:
    case MZ2_SOLDIER_MACHINEGUN_7:
    case MZ2_SOLDIER_MACHINEGUN_8:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        CL_ParticleEffect (origin, vec3_origin, 0, 40);
        CL_SmokeAndFlash(origin);
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("soldier/solatck3.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_GUNNER_MACHINEGUN_1:
    case MZ2_GUNNER_MACHINEGUN_2:
    case MZ2_GUNNER_MACHINEGUN_3:
    case MZ2_GUNNER_MACHINEGUN_4:
    case MZ2_GUNNER_MACHINEGUN_5:
    case MZ2_GUNNER_MACHINEGUN_6:
    case MZ2_GUNNER_MACHINEGUN_7:
    case MZ2_GUNNER_MACHINEGUN_8:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        CL_ParticleEffect (origin, vec3_origin, 0, 40);
        CL_SmokeAndFlash(origin);
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("gunner/gunatck2.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_ACTOR_MACHINEGUN_1:
    case MZ2_SUPERTANK_MACHINEGUN_1:
    case MZ2_SUPERTANK_MACHINEGUN_2:
    case MZ2_SUPERTANK_MACHINEGUN_3:
    case MZ2_SUPERTANK_MACHINEGUN_4:
    case MZ2_SUPERTANK_MACHINEGUN_5:
    case MZ2_SUPERTANK_MACHINEGUN_6:
    case MZ2_TURRET_MACHINEGUN:         // PGM
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;

        CL_ParticleEffect (origin, vec3_origin, 0, 40);
        CL_SmokeAndFlash(origin);
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("infantry/infatck1.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_BOSS2_MACHINEGUN_L1:
    case MZ2_BOSS2_MACHINEGUN_L2:
    case MZ2_BOSS2_MACHINEGUN_L3:
    case MZ2_BOSS2_MACHINEGUN_L4:
    case MZ2_BOSS2_MACHINEGUN_L5:
    case MZ2_CARRIER_MACHINEGUN_L1:     // PMM
    case MZ2_CARRIER_MACHINEGUN_L2:     // PMM
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;

        CL_ParticleEffect (origin, vec3_origin, 0, 40);
        CL_SmokeAndFlash(origin);
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("infantry/infatck1.wav"), 1, ATTN_NONE, 0);
        break;

    case MZ2_SOLDIER_BLASTER_1:
    case MZ2_SOLDIER_BLASTER_2:
    case MZ2_SOLDIER_BLASTER_3:
    case MZ2_SOLDIER_BLASTER_4:
    case MZ2_SOLDIER_BLASTER_5:
    case MZ2_SOLDIER_BLASTER_6:
    case MZ2_SOLDIER_BLASTER_7:
    case MZ2_SOLDIER_BLASTER_8:
    case MZ2_TURRET_BLASTER:            // PGM
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("soldier/solatck2.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_FLYER_BLASTER_1:
    case MZ2_FLYER_BLASTER_2:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("flyer/flyatck3.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_MEDIC_BLASTER_1:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("medic/medatck1.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_HOVER_BLASTER_1:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("hover/hovatck1.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_FLOAT_BLASTER_1:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("floater/fltatck1.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_SOLDIER_SHOTGUN_1:
    case MZ2_SOLDIER_SHOTGUN_2:
    case MZ2_SOLDIER_SHOTGUN_3:
    case MZ2_SOLDIER_SHOTGUN_4:
    case MZ2_SOLDIER_SHOTGUN_5:
    case MZ2_SOLDIER_SHOTGUN_6:
    case MZ2_SOLDIER_SHOTGUN_7:
    case MZ2_SOLDIER_SHOTGUN_8:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        CL_SmokeAndFlash(origin);
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("soldier/solatck1.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_TANK_BLASTER_1:
    case MZ2_TANK_BLASTER_2:
    case MZ2_TANK_BLASTER_3:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("tank/tnkatck3.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_TANK_MACHINEGUN_1:
    case MZ2_TANK_MACHINEGUN_2:
    case MZ2_TANK_MACHINEGUN_3:
    case MZ2_TANK_MACHINEGUN_4:
    case MZ2_TANK_MACHINEGUN_5:
    case MZ2_TANK_MACHINEGUN_6:
    case MZ2_TANK_MACHINEGUN_7:
    case MZ2_TANK_MACHINEGUN_8:
    case MZ2_TANK_MACHINEGUN_9:
    case MZ2_TANK_MACHINEGUN_10:
    case MZ2_TANK_MACHINEGUN_11:
    case MZ2_TANK_MACHINEGUN_12:
    case MZ2_TANK_MACHINEGUN_13:
    case MZ2_TANK_MACHINEGUN_14:
    case MZ2_TANK_MACHINEGUN_15:
    case MZ2_TANK_MACHINEGUN_16:
    case MZ2_TANK_MACHINEGUN_17:
    case MZ2_TANK_MACHINEGUN_18:
    case MZ2_TANK_MACHINEGUN_19:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        CL_ParticleEffect (origin, vec3_origin, 0, 40);
        CL_SmokeAndFlash(origin);
        Q_snprintf(soundname, sizeof(soundname), "tank/tnkatk2%c.wav", 'a' + rand() % 5);
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound(soundname), 1, ATTN_NORM, 0);
        break;

    case MZ2_CHICK_ROCKET_1:
    case MZ2_TURRET_ROCKET:         // PGM
        dl->color[0] = 1;dl->color[1] = 0.5;dl->color[2] = 0.2;
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("chick/chkatck2.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_TANK_ROCKET_1:
    case MZ2_TANK_ROCKET_2:
    case MZ2_TANK_ROCKET_3:
        dl->color[0] = 1;dl->color[1] = 0.5;dl->color[2] = 0.2;
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("tank/tnkatck1.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_SUPERTANK_ROCKET_1:
    case MZ2_SUPERTANK_ROCKET_2:
    case MZ2_SUPERTANK_ROCKET_3:
    case MZ2_BOSS2_ROCKET_1:
    case MZ2_BOSS2_ROCKET_2:
    case MZ2_BOSS2_ROCKET_3:
    case MZ2_BOSS2_ROCKET_4:
    case MZ2_CARRIER_ROCKET_1:
//  case MZ2_CARRIER_ROCKET_2:
//  case MZ2_CARRIER_ROCKET_3:
//  case MZ2_CARRIER_ROCKET_4:
        dl->color[0] = 1;dl->color[1] = 0.5;dl->color[2] = 0.2;
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("tank/rocket.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_GUNNER_GRENADE_1:
    case MZ2_GUNNER_GRENADE_2:
    case MZ2_GUNNER_GRENADE_3:
    case MZ2_GUNNER_GRENADE_4:
        dl->color[0] = 1;dl->color[1] = 0.5;dl->color[2] = 0;
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("gunner/gunatck3.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_GLADIATOR_RAILGUN_1:
    // PMM
    case MZ2_CARRIER_RAILGUN:
    case MZ2_WIDOW_RAIL:
    // pmm
        dl->color[0] = 0.5;dl->color[1] = 0.5;dl->color[2] = 1.0;
        break;

// --- Xian's shit starts ---
    case MZ2_MAKRON_BFG:
        dl->color[0] = 0.5;dl->color[1] = 1 ;dl->color[2] = 0.5;
        //S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("makron/bfg_firef.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_MAKRON_BLASTER_1:
    case MZ2_MAKRON_BLASTER_2:
    case MZ2_MAKRON_BLASTER_3:
    case MZ2_MAKRON_BLASTER_4:
    case MZ2_MAKRON_BLASTER_5:
    case MZ2_MAKRON_BLASTER_6:
    case MZ2_MAKRON_BLASTER_7:
    case MZ2_MAKRON_BLASTER_8:
    case MZ2_MAKRON_BLASTER_9:
    case MZ2_MAKRON_BLASTER_10:
    case MZ2_MAKRON_BLASTER_11:
    case MZ2_MAKRON_BLASTER_12:
    case MZ2_MAKRON_BLASTER_13:
    case MZ2_MAKRON_BLASTER_14:
    case MZ2_MAKRON_BLASTER_15:
    case MZ2_MAKRON_BLASTER_16:
    case MZ2_MAKRON_BLASTER_17:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("makron/blaster.wav"), 1, ATTN_NORM, 0);
        break;
    
    case MZ2_JORG_MACHINEGUN_L1:
    case MZ2_JORG_MACHINEGUN_L2:
    case MZ2_JORG_MACHINEGUN_L3:
    case MZ2_JORG_MACHINEGUN_L4:
    case MZ2_JORG_MACHINEGUN_L5:
    case MZ2_JORG_MACHINEGUN_L6:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        CL_ParticleEffect (origin, vec3_origin, 0, 40);
        CL_SmokeAndFlash(origin);
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("boss3/xfiref.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_JORG_MACHINEGUN_R1:
    case MZ2_JORG_MACHINEGUN_R2:
    case MZ2_JORG_MACHINEGUN_R3:
    case MZ2_JORG_MACHINEGUN_R4:
    case MZ2_JORG_MACHINEGUN_R5:
    case MZ2_JORG_MACHINEGUN_R6:
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        CL_ParticleEffect (origin, vec3_origin, 0, 40);
        CL_SmokeAndFlash(origin);
        break;

    case MZ2_JORG_BFG_1:
        dl->color[0] = 0.5;dl->color[1] = 1 ;dl->color[2] = 0.5;
        break;

    case MZ2_BOSS2_MACHINEGUN_R1:
    case MZ2_BOSS2_MACHINEGUN_R2:
    case MZ2_BOSS2_MACHINEGUN_R3:
    case MZ2_BOSS2_MACHINEGUN_R4:
    case MZ2_BOSS2_MACHINEGUN_R5:
    case MZ2_CARRIER_MACHINEGUN_R1:         // PMM
    case MZ2_CARRIER_MACHINEGUN_R2:         // PMM

        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;

        CL_ParticleEffect (origin, vec3_origin, 0, 40);
        CL_SmokeAndFlash(origin);
        break;

// ======
// ROGUE
    case MZ2_STALKER_BLASTER:
    case MZ2_DAEDALUS_BLASTER:
    case MZ2_MEDIC_BLASTER_2:
    case MZ2_WIDOW_BLASTER:
    case MZ2_WIDOW_BLASTER_SWEEP1:
    case MZ2_WIDOW_BLASTER_SWEEP2:
    case MZ2_WIDOW_BLASTER_SWEEP3:
    case MZ2_WIDOW_BLASTER_SWEEP4:
    case MZ2_WIDOW_BLASTER_SWEEP5:
    case MZ2_WIDOW_BLASTER_SWEEP6:
    case MZ2_WIDOW_BLASTER_SWEEP7:
    case MZ2_WIDOW_BLASTER_SWEEP8:
    case MZ2_WIDOW_BLASTER_SWEEP9:
    case MZ2_WIDOW_BLASTER_100:
    case MZ2_WIDOW_BLASTER_90:
    case MZ2_WIDOW_BLASTER_80:
    case MZ2_WIDOW_BLASTER_70:
    case MZ2_WIDOW_BLASTER_60:
    case MZ2_WIDOW_BLASTER_50:
    case MZ2_WIDOW_BLASTER_40:
    case MZ2_WIDOW_BLASTER_30:
    case MZ2_WIDOW_BLASTER_20:
    case MZ2_WIDOW_BLASTER_10:
    case MZ2_WIDOW_BLASTER_0:
    case MZ2_WIDOW_BLASTER_10L:
    case MZ2_WIDOW_BLASTER_20L:
    case MZ2_WIDOW_BLASTER_30L:
    case MZ2_WIDOW_BLASTER_40L:
    case MZ2_WIDOW_BLASTER_50L:
    case MZ2_WIDOW_BLASTER_60L:
    case MZ2_WIDOW_BLASTER_70L:
    case MZ2_WIDOW_RUN_1:
    case MZ2_WIDOW_RUN_2:
    case MZ2_WIDOW_RUN_3:
    case MZ2_WIDOW_RUN_4:
    case MZ2_WIDOW_RUN_5:
    case MZ2_WIDOW_RUN_6:
    case MZ2_WIDOW_RUN_7:
    case MZ2_WIDOW_RUN_8:
        dl->color[0] = 0;dl->color[1] = 1;dl->color[2] = 0;
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("tank/tnkatck3.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_WIDOW_DISRUPTOR:
        dl->color[0] = -1;dl->color[1] = -1;dl->color[2] = -1;
        S_StartSound (NULL, ent, CHAN_WEAPON, S_RegisterSound("weapons/disint2.wav"), 1, ATTN_NORM, 0);
        break;

    case MZ2_WIDOW_PLASMABEAM:
    case MZ2_WIDOW2_BEAMER_1:
    case MZ2_WIDOW2_BEAMER_2:
    case MZ2_WIDOW2_BEAMER_3:
    case MZ2_WIDOW2_BEAMER_4:
    case MZ2_WIDOW2_BEAMER_5:
    case MZ2_WIDOW2_BEAM_SWEEP_1:
    case MZ2_WIDOW2_BEAM_SWEEP_2:
    case MZ2_WIDOW2_BEAM_SWEEP_3:
    case MZ2_WIDOW2_BEAM_SWEEP_4:
    case MZ2_WIDOW2_BEAM_SWEEP_5:
    case MZ2_WIDOW2_BEAM_SWEEP_6:
    case MZ2_WIDOW2_BEAM_SWEEP_7:
    case MZ2_WIDOW2_BEAM_SWEEP_8:
    case MZ2_WIDOW2_BEAM_SWEEP_9:
    case MZ2_WIDOW2_BEAM_SWEEP_10:
    case MZ2_WIDOW2_BEAM_SWEEP_11:
        dl->radius = 300 + (rand()&100);
        dl->color[0] = 1;dl->color[1] = 1;dl->color[2] = 0;
        dl->die = cl.time + 200;
        break;
// ROGUE
// ======

// --- Xian's shit ends ---

    }
}


/*
===============
CL_AddDLights

===============
*/
void CL_AddDLights (void)
{
    int         i;
    cdlight_t   *dl;

    dl = cl_dlights;

//=====
//PGM
    if( scr_glconfig.renderer != GL_RENDERER_SOFTWARE )
    {
        for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
        {
            if (!dl->radius)
                continue;
            V_AddLight (dl->origin, dl->radius,
                dl->color[0], dl->color[1], dl->color[2]);
        }
    }
    else
    {
        for (i=0 ; i<MAX_DLIGHTS ; i++, dl++)
        {
            if (!dl->radius)
                continue;

            // negative light in softwaref. only black allowed
            if ((dl->color[0] < 0) || (dl->color[1] < 0) || (dl->color[2] < 0))
            {
                dl->radius = -(dl->radius);
                dl->color[0] = 1;
                dl->color[1] = 1;
                dl->color[2] = 1;
            }
            V_AddLight (dl->origin, dl->radius,
                dl->color[0], dl->color[1], dl->color[2]);
        }
    }
//PGM
//=====
}



/*
==============================================================

PARTICLE MANAGEMENT

==============================================================
*/

static cparticle_t  *active_particles, *free_particles;

static cparticle_t  particles[MAX_PARTICLES];
static const int    cl_numparticles = MAX_PARTICLES;


/*
===============
CL_ClearParticles
===============
*/
void CL_ClearParticles (void)
{
    int     i;
    
    free_particles = &particles[0];
    active_particles = NULL;

    for (i=0 ;i<cl_numparticles ; i++)
        particles[i].next = &particles[i+1];
    particles[cl_numparticles-1].next = NULL;
}

cparticle_t *CL_AllocParticle( void ) {
    cparticle_t *p;

    if (!free_particles)
        return NULL;
    p = free_particles;
    free_particles = p->next;
    p->next = active_particles;
    active_particles = p;

    return p;
}

/*
===============
CL_ParticleEffect

Wall impact puffs
===============
*/
void CL_ParticleEffect (vec3_t org, vec3_t dir, int color, int count)
{
    int         i, j;
    cparticle_t *p;
    float       d;

    for (i=0 ; i<count ; i++)
    {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color + (rand()&7);

        d = rand()&31;
        for (j=0 ; j<3 ; j++)
        {
            p->org[j] = org[j] + ((rand()&7)-4) + d*dir[j];
            p->vel[j] = crand()*20;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0;

        p->alphavel = -1.0 / (0.5 + frand()*0.3);
    }
}


/*
===============
CL_ParticleEffect2
===============
*/
void CL_ParticleEffect2 (vec3_t org, vec3_t dir, int color, int count)
{
    int         i, j;
    cparticle_t *p;
    float       d;

    for (i=0 ; i<count ; i++)
    {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color;

        d = rand()&7;
        for (j=0 ; j<3 ; j++)
        {
            p->org[j] = org[j] + ((rand()&7)-4) + d*dir[j];
            p->vel[j] = crand()*20;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0;

        p->alphavel = -1.0 / (0.5 + frand()*0.3);
    }
}


// RAFAEL
/*
===============
CL_ParticleEffect3
===============
*/
void CL_ParticleEffect3 (vec3_t org, vec3_t dir, int color, int count)
{
    int         i, j;
    cparticle_t *p;
    float       d;

    for (i=0 ; i<count ; i++)
    {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color;

        d = rand()&7;
        for (j=0 ; j<3 ; j++)
        {
            p->org[j] = org[j] + ((rand()&7)-4) + d*dir[j];
            p->vel[j] = crand()*20;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = PARTICLE_GRAVITY;
        p->alpha = 1.0;

        p->alphavel = -1.0 / (0.5 + frand()*0.3);
    }
}

/*
===============
CL_TeleporterParticles
===============
*/
void CL_TeleporterParticles (vec3_t org)
{
    int         i, j;
    cparticle_t *p;

    for (i=0 ; i<8 ; i++)
    {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = 0xdb;

        for (j=0 ; j<2 ; j++)
        {
            p->org[j] = org[j] - 16 + (rand()&31);
            p->vel[j] = crand()*14;
        }

        p->org[2] = org[2] - 8 + (rand()&7);
        p->vel[2] = 80 + (rand()&7);

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0;

        p->alphavel = -0.5;
    }
}


/*
===============
CL_LogoutEffect

===============
*/
void CL_LogoutEffect (vec3_t org, int type)
{
    int         i, j;
    cparticle_t *p;

    for (i=0 ; i<500 ; i++)
    {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;

        if (type == MZ_LOGIN)
            p->color = 0xd0 + (rand()&7);   // green
        else if (type == MZ_LOGOUT)
            p->color = 0x40 + (rand()&7);   // red
        else
            p->color = 0xe0 + (rand()&7);   // yellow

        p->org[0] = org[0] - 16 + frand()*32;
        p->org[1] = org[1] - 16 + frand()*32;
        p->org[2] = org[2] - 24 + frand()*56;

        for (j=0 ; j<3 ; j++)
            p->vel[j] = crand()*20;

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0;

        p->alphavel = -1.0 / (1.0 + frand()*0.3);
    }
}


/*
===============
CL_ItemRespawnParticles

===============
*/
void CL_ItemRespawnParticles (vec3_t org)
{
    int         i, j;
    cparticle_t *p;

    for (i=0 ; i<64 ; i++)
    {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;

        p->color = 0xd4 + (rand()&3);   // green

        p->org[0] = org[0] + crand()*8;
        p->org[1] = org[1] + crand()*8;
        p->org[2] = org[2] + crand()*8;

        for (j=0 ; j<3 ; j++)
            p->vel[j] = crand()*8;

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY*0.2;
        p->alpha = 1.0;

        p->alphavel = -1.0 / (1.0 + frand()*0.3);
    }
}


/*
===============
CL_ExplosionParticles
===============
*/
void CL_ExplosionParticles (vec3_t org)
{
    int         i, j;
    cparticle_t *p;

    for (i=0 ; i<256 ; i++)
    {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = 0xe0 + (rand()&7);

        for (j=0 ; j<3 ; j++)
        {
            p->org[j] = org[j] + ((rand()%32)-16);
            p->vel[j] = (rand()%384)-192;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0;

        p->alphavel = -0.8 / (0.5 + frand()*0.3);
    }
}

/*
===============
CL_BigTeleportParticles
===============
*/
void CL_BigTeleportParticles (vec3_t org)
{
    int         i;
    cparticle_t *p;
    float       angle, dist;
    static const int colortable[4] = {2*8,13*8,21*8,18*8};

    for (i=0 ; i<4096 ; i++)
    {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;

        p->color = colortable[rand()&3];

        angle = M_PI*2*(rand()&1023)/1023.0;
        dist = rand()&31;
        p->org[0] = org[0] + cos(angle)*dist;
        p->vel[0] = cos(angle)*(70+(rand()&63));
        p->accel[0] = -cos(angle)*100;

        p->org[1] = org[1] + sin(angle)*dist;
        p->vel[1] = sin(angle)*(70+(rand()&63));
        p->accel[1] = -sin(angle)*100;

        p->org[2] = org[2] + 8 + (rand()%90);
        p->vel[2] = -100 + (rand()&31);
        p->accel[2] = PARTICLE_GRAVITY*4;
        p->alpha = 1.0;

        p->alphavel = -0.3 / (0.5 + frand()*0.3);
    }
}


/*
===============
CL_BlasterParticles

Wall impact puffs
===============
*/
void CL_BlasterParticles (vec3_t org, vec3_t dir)
{
    int         i, j;
    cparticle_t *p;
    float       d;

    for (i=0 ; i<40 ; i++)
    {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = 0xe0 + (rand()&7);

        d = rand()&15;
        for (j=0 ; j<3 ; j++)
        {
            p->org[j] = org[j] + ((rand()&7)-4) + d*dir[j];
            p->vel[j] = dir[j] * 30 + crand()*40;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0;

        p->alphavel = -1.0 / (0.5 + frand()*0.3);
    }
}


/*
===============
CL_BlasterTrail

===============
*/
void CL_BlasterTrail (vec3_t start, vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    int         dec;

    VectorCopy (start, move);
    VectorSubtract (end, start, vec);
    len = VectorNormalize (vec);

    dec = 5;
    VectorScale (vec, 5, vec);

    // FIXME: this is a really silly way to have a loop
    while (len > 0)
    {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear (p->accel);
        
        p->time = cl.time;

        p->alpha = 1.0;
        p->alphavel = -1.0 / (0.3+frand()*0.2);
        p->color = 0xe0;
        for (j=0 ; j<3 ; j++)
        {
            p->org[j] = move[j] + crand();
            p->vel[j] = crand()*5;
            p->accel[j] = 0;
        }

        VectorAdd (move, vec, move);
    }
}

/*
===============
CL_QuadTrail

===============
*/
void CL_QuadTrail (vec3_t start, vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    int         dec;

    VectorCopy (start, move);
    VectorSubtract (end, start, vec);
    len = VectorNormalize (vec);

    dec = 5;
    VectorScale (vec, 5, vec);

    while (len > 0)
    {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear (p->accel);
        
        p->time = cl.time;

        p->alpha = 1.0;
        p->alphavel = -1.0 / (0.8+frand()*0.2);
        p->color = 115;
        for (j=0 ; j<3 ; j++)
        {
            p->org[j] = move[j] + crand()*16;
            p->vel[j] = crand()*5;
            p->accel[j] = 0;
        }

        VectorAdd (move, vec, move);
    }
}

/*
===============
CL_FlagTrail

===============
*/
void CL_FlagTrail (vec3_t start, vec3_t end, float color)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    int         dec;

    VectorCopy (start, move);
    VectorSubtract (end, start, vec);
    len = VectorNormalize (vec);

    dec = 5;
    VectorScale (vec, 5, vec);

    while (len > 0)
    {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear (p->accel);
        
        p->time = cl.time;

        p->alpha = 1.0;
        p->alphavel = -1.0 / (0.8+frand()*0.2);
        p->color = color;
        for (j=0 ; j<3 ; j++)
        {
            p->org[j] = move[j] + crand()*16;
            p->vel[j] = crand()*5;
            p->accel[j] = 0;
        }

        VectorAdd (move, vec, move);
    }
}

/*
===============
CL_DiminishingTrail

===============
*/
void CL_DiminishingTrail (vec3_t start, vec3_t end, centity_t *old, int flags)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    float       dec;
    float       orgscale;
    float       velscale;

    VectorCopy (start, move);
    VectorSubtract (end, start, vec);
    len = VectorNormalize (vec);

    dec = 0.5;
    VectorScale (vec, dec, vec);

    if (old->trailcount > 900)
    {
        orgscale = 4;
        velscale = 15;
    }
    else if (old->trailcount > 800)
    {
        orgscale = 2;
        velscale = 10;
    }
    else
    {
        orgscale = 1;
        velscale = 5;
    }

    while (len > 0)
    {
        len -= dec;

        // drop less particles as it flies
        if ((rand()&1023) < old->trailcount)
        {
            p = CL_AllocParticle();
            if (!p)
                return;
            VectorClear (p->accel);
        
            p->time = cl.time;

            if (flags & EF_GIB)
            {
                p->alpha = 1.0;
                p->alphavel = -1.0 / (1+frand()*0.4);
                p->color = 0xe8 + (rand()&7);
                for (j=0 ; j<3 ; j++)
                {
                    p->org[j] = move[j] + crand()*orgscale;
                    p->vel[j] = crand()*velscale;
                    p->accel[j] = 0;
                }
                p->vel[2] -= PARTICLE_GRAVITY;
            }
            else if (flags & EF_GREENGIB)
            {
                p->alpha = 1.0;
                p->alphavel = -1.0 / (1+frand()*0.4);
                p->color = 0xdb + (rand()&7);
                for (j=0; j< 3; j++)
                {
                    p->org[j] = move[j] + crand()*orgscale;
                    p->vel[j] = crand()*velscale;
                    p->accel[j] = 0;
                }
                p->vel[2] -= PARTICLE_GRAVITY;
            }
            else
            {
                p->alpha = 1.0;
                p->alphavel = -1.0 / (1+frand()*0.2);
                p->color = 4 + (rand()&7);
                for (j=0 ; j<3 ; j++)
                {
                    p->org[j] = move[j] + crand()*orgscale;
                    p->vel[j] = crand()*velscale;
                }
                p->accel[2] = 20;
            }
        }

        old->trailcount -= 5;
        if (old->trailcount < 100)
            old->trailcount = 100;
        VectorAdd (move, vec, move);
    }
}

void MakeNormalVectors (vec3_t forward, vec3_t right, vec3_t up)
{
    float       d;

    // this rotate and negat guarantees a vector
    // not colinear with the original
    right[1] = -forward[0];
    right[2] = forward[1];
    right[0] = forward[2];

    d = DotProduct (right, forward);
    VectorMA (right, -d, forward, right);
    VectorNormalize (right);
    CrossProduct (right, forward, up);
}

/*
===============
CL_RocketTrail

===============
*/
void CL_RocketTrail (vec3_t start, vec3_t end, centity_t *old)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    float       dec;

    // smoke
    CL_DiminishingTrail (start, end, old, EF_ROCKET);

    // fire
    VectorCopy (start, move);
    VectorSubtract (end, start, vec);
    len = VectorNormalize (vec);

    dec = 1;
    VectorScale (vec, dec, vec);

    while (len > 0)
    {
        len -= dec;

        if ( (rand()&7) == 0)
        {
            p = CL_AllocParticle();
            if (!p)
                return;
            
            VectorClear (p->accel);
            p->time = cl.time;

            p->alpha = 1.0;
            p->alphavel = -1.0 / (1+frand()*0.2);
            p->color = 0xdc + (rand()&3);
            for (j=0 ; j<3 ; j++)
            {
                p->org[j] = move[j] + crand()*5;
                p->vel[j] = crand()*20;
            }
            p->accel[2] = -PARTICLE_GRAVITY;
        }
        VectorAdd (move, vec, move);
    }
}

/*
===============
CL_RailTrail

===============
*/
void CL_OldRailTrail (vec3_t start, vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    float       dec;
    vec3_t      right, up;
    int         i;
    float       d, c, s;
    vec3_t      dir;
    byte        clr = 0x74;

    VectorCopy (start, move);
    VectorSubtract (end, start, vec);
    len = VectorNormalize (vec);

    MakeNormalVectors (vec, right, up);

    for (i=0 ; i<len ; i++)
    {
        p = CL_AllocParticle();
        if (!p)
            return;
        
        p->time = cl.time;
        VectorClear (p->accel);

        d = i * 0.1;
        c = cos(d);
        s = sin(d);

        VectorScale (right, c, dir);
        VectorMA (dir, s, up, dir);

        p->alpha = 1.0;
        p->alphavel = -1.0 / (1+frand()*0.2);
        p->color = clr + (rand()&7);
        for (j=0 ; j<3 ; j++)
        {
            p->org[j] = move[j] + dir[j]*3;
            p->vel[j] = dir[j]*6;
        }

        VectorAdd (move, vec, move);
    }

    dec = 0.75;
    VectorScale (vec, dec, vec);
    VectorCopy (start, move);

    while (len > 0)
    {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        VectorClear (p->accel);

        p->alpha = 1.0;
        p->alphavel = -1.0 / (0.6+frand()*0.2);
        p->color = rand()&15;

        for (j=0 ; j<3 ; j++)
        {
            p->org[j] = move[j] + crand()*3;
            p->vel[j] = crand()*3;
            p->accel[j] = 0;
        }

        VectorAdd (move, vec, move);
    }
}

static color_t  railcore_color;
static color_t  railspiral_color;

static cvar_t *cl_railtrail_type;
static cvar_t *cl_railtrail_time;
static cvar_t *cl_railcore_color;
static cvar_t *cl_railcore_width;
static cvar_t *cl_railspiral_color;
static cvar_t *cl_railspiral_radius;


static void cl_railcore_color_changed( cvar_t *self ) {
    if( !SCR_ParseColor( self->string, railcore_color ) ) {
        Com_WPrintf( "Invalid value '%s' for '%s'\n", self->string, self->name );
        *( uint32_t *)railcore_color = *( uint32_t * )colorRed;
    }
}

static void cl_railspiral_color_changed( cvar_t *self ) {
    if( !SCR_ParseColor( self->string, railspiral_color ) ) {
        Com_WPrintf( "Invalid value '%s' for '%s'\n", self->string, self->name );
        *( uint32_t *)railspiral_color = *( uint32_t * )colorBlue;
    }
}

static void CL_NewRailCore( vec3_t start, vec3_t end ) {
    laser_t *l;

    l = alloc_laser();
    if (!l)
        return;

    VectorCopy( start, l->start );
    VectorCopy( end, l->end );
    l->fadeType = LASER_FADE_RGBA;
    l->lifeTime = 1000 * cl_railtrail_time->value;
    l->indexed = qfalse;
    l->width = cl_railcore_width->value;
    *( uint32_t * )l->color = *( uint32_t * )railcore_color;
}

static void CL_NewRailSpiral( vec3_t start, vec3_t end ) {
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
//  float       dec;
    vec3_t      right, up;
    int         i;
    float       d, c, s;
    vec3_t      dir;

    VectorCopy( start, move );
    VectorSubtract( end, start, vec );
    len = VectorNormalize( vec );

    MakeNormalVectors( vec, right, up );

    for( i=0 ; i<len ; i++ ) {
        p = CL_AllocParticle();
        if (!p)
            return;
        
        p->time = cl.time;
        VectorClear( p->accel );

        d = i * 0.1;
        c = cos( d );
        s = sin( d );

        VectorScale( right, c, dir );
        VectorMA( dir, s, up, dir );

        p->alpha = railspiral_color[3] / 255.0f;
        p->alphavel = -1.0 / ( cl_railtrail_time->value + frand() * 0.2 );
        p->color = 0xff;
        *( uint32_t * )p->rgb = *( uint32_t * )railspiral_color;
        for( j=0 ; j<3 ; j++ ) {
            p->org[j] = move[j] + dir[j] * cl_railspiral_radius->value;
            p->vel[j] = dir[j] * 6;
        }

        VectorAdd( move, vec, move );
    }
}

void CL_RailTrail( vec3_t start, vec3_t end ) {
    if( !cl_railtrail_type->integer || scr_glconfig.renderer == GL_RENDERER_SOFTWARE ) {
        CL_OldRailTrail( start, end );
    } else {
        CL_NewRailCore( start, end );
        if( cl_railtrail_type->integer > 1 ) {
            CL_NewRailSpiral( start, end );
        }
    }
}



// RAFAEL
/*
===============
CL_IonripperTrail
===============
*/
void CL_IonripperTrail (vec3_t start, vec3_t ent)
{
    vec3_t  move;
    vec3_t  vec;
    float   len;
    int     j;
    cparticle_t *p;
    int     dec;
    int     left = 0;

    VectorCopy (start, move);
    VectorSubtract (ent, start, vec);
    len = VectorNormalize (vec);

    dec = 5;
    VectorScale (vec, 5, vec);

    while (len > 0)
    {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear (p->accel);

        p->time = cl.time;
        p->alpha = 0.5;
        p->alphavel = -1.0 / (0.3 + frand() * 0.2);
        p->color = 0xe4 + (rand()&3);

        for (j=0; j<3; j++)
        {
            p->org[j] = move[j];
            p->accel[j] = 0;
        }
        if (left)
        {
            left = 0;
            p->vel[0] = 10;
        }
        else 
        {
            left = 1;
            p->vel[0] = -10;
        }

        p->vel[1] = 0;
        p->vel[2] = 0;

        VectorAdd (move, vec, move);
    }
}


/*
===============
CL_BubbleTrail

===============
*/
void CL_BubbleTrail (vec3_t start, vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         i, j;
    cparticle_t *p;
    float       dec;

    VectorCopy (start, move);
    VectorSubtract (end, start, vec);
    len = VectorNormalize (vec);

    dec = 32;
    VectorScale (vec, dec, vec);

    for (i=0 ; i<len ; i+=dec)
    {
        p = CL_AllocParticle();
        if (!p)
            return;

        VectorClear (p->accel);
        p->time = cl.time;

        p->alpha = 1.0;
        p->alphavel = -1.0 / (1+frand()*0.2);
        p->color = 4 + (rand()&7);
        for (j=0 ; j<3 ; j++)
        {
            p->org[j] = move[j] + crand()*2;
            p->vel[j] = crand()*5;
        }
        p->vel[2] += 6;

        VectorAdd (move, vec, move);
    }
}


/*
===============
CL_FlyParticles
===============
*/

#define BEAMLENGTH          16
void CL_FlyParticles (vec3_t origin, int count)
{
    int         i;
    cparticle_t *p;
    float       angle;
    float       sr, sp, sy, cr, cp, cy;
    vec3_t      forward;
    float       dist = 64;
    float       ltime;


    if (count > NUMVERTEXNORMALS)
        count = NUMVERTEXNORMALS;

    if (!avelocities[0][0])
    {
        for (i=0 ; i<NUMVERTEXNORMALS*3 ; i++)
            avelocities[0][i] = (rand()&255) * 0.01;
    }


    ltime = (float)cl.time / 1000.0;
    for (i=0 ; i<count ; i+=2)
    {
        angle = ltime * avelocities[i][0];
        sy = sin(angle);
        cy = cos(angle);
        angle = ltime * avelocities[i][1];
        sp = sin(angle);
        cp = cos(angle);
        angle = ltime * avelocities[i][2];
        sr = sin(angle);
        cr = cos(angle);
    
        forward[0] = cp*cy;
        forward[1] = cp*sy;
        forward[2] = -sp;

        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;

        dist = sin(ltime + i)*64;
        p->org[0] = origin[0] + bytedirs[i][0]*dist + forward[0]*BEAMLENGTH;
        p->org[1] = origin[1] + bytedirs[i][1]*dist + forward[1]*BEAMLENGTH;
        p->org[2] = origin[2] + bytedirs[i][2]*dist + forward[2]*BEAMLENGTH;

        VectorClear (p->vel);
        VectorClear (p->accel);

        p->color = 0;
        p->colorvel = 0;

        p->alpha = 1;
        p->alphavel = -100;
    }
}

void CL_FlyEffect (centity_t *ent, vec3_t origin)
{
    int     n;
    int     count;
    int     starttime;

    if (ent->fly_stoptime < cl.time)
    {
        starttime = cl.time;
        ent->fly_stoptime = cl.time + 60000;
    }
    else
    {
        starttime = ent->fly_stoptime - 60000;
    }

    n = cl.time - starttime;
    if (n < 20000)
        count = n * 162 / 20000.0;
    else
    {
        n = ent->fly_stoptime - cl.time;
        if (n < 20000)
            count = n * 162 / 20000.0;
        else
            count = 162;
    }

    CL_FlyParticles (origin, count);
}


/*
===============
CL_BfgParticles
===============
*/

#define BEAMLENGTH          16
void CL_BfgParticles (entity_t *ent)
{
    int         i;
    cparticle_t *p;
    float       angle;
    float       sr, sp, sy, cr, cp, cy;
    vec3_t      forward;
    float       dist = 64;
    vec3_t      v;
    float       ltime;
    
    if (!avelocities[0][0])
    {
        for (i=0 ; i<NUMVERTEXNORMALS*3 ; i++)
            avelocities[0][i] = (rand()&255) * 0.01;
    }


    ltime = (float)cl.time / 1000.0;
    for (i=0 ; i<NUMVERTEXNORMALS ; i++)
    {
        angle = ltime * avelocities[i][0];
        sy = sin(angle);
        cy = cos(angle);
        angle = ltime * avelocities[i][1];
        sp = sin(angle);
        cp = cos(angle);
        angle = ltime * avelocities[i][2];
        sr = sin(angle);
        cr = cos(angle);
    
        forward[0] = cp*cy;
        forward[1] = cp*sy;
        forward[2] = -sp;

        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;

        dist = sin(ltime + i)*64;
        p->org[0] = ent->origin[0] + bytedirs[i][0]*dist + forward[0]*BEAMLENGTH;
        p->org[1] = ent->origin[1] + bytedirs[i][1]*dist + forward[1]*BEAMLENGTH;
        p->org[2] = ent->origin[2] + bytedirs[i][2]*dist + forward[2]*BEAMLENGTH;

        VectorClear (p->vel);
        VectorClear (p->accel);

        VectorSubtract (p->org, ent->origin, v);
        dist = VectorLength(v) / 90.0;
        p->color = floor (0xd0 + dist * 7);
        p->colorvel = 0;

        p->alpha = 1.0 - dist;
        p->alphavel = -100;
    }
}


/*
===============
CL_TrapParticles
===============
*/
// RAFAEL
void CL_TrapParticles (entity_t *ent)
{
    vec3_t      move;
    vec3_t      vec;
    vec3_t      start, end;
    float       len;
    int         j;
    cparticle_t *p;
    int         dec;

    ent->origin[2]-=14;
    VectorCopy (ent->origin, start);
    VectorCopy (ent->origin, end);
    end[2]+=64;

    VectorCopy (start, move);
    VectorSubtract (end, start, vec);
    len = VectorNormalize (vec);

    dec = 5;
    VectorScale (vec, 5, vec);

    // FIXME: this is a really silly way to have a loop
    while (len > 0)
    {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear (p->accel);
        
        p->time = cl.time;

        p->alpha = 1.0;
        p->alphavel = -1.0 / (0.3+frand()*0.2);
        p->color = 0xe0;
        for (j=0 ; j<3 ; j++)
        {
            p->org[j] = move[j] + crand();
            p->vel[j] = crand()*15;
            p->accel[j] = 0;
        }
        p->accel[2] = PARTICLE_GRAVITY;

        VectorAdd (move, vec, move);
    }

    {

    
    int         i, j, k;
    cparticle_t *p;
    float       vel;
    vec3_t      dir;
    vec3_t      org;

    
    ent->origin[2]+=14;
    VectorCopy (ent->origin, org);


    for (i=-2 ; i<=2 ; i+=4)
        for (j=-2 ; j<=2 ; j+=4)
            for (k=-2 ; k<=4 ; k+=4)
            {
                p = CL_AllocParticle();
                if (!p)
                    return;

                p->time = cl.time;
                p->color = 0xe0 + (rand()&3);

                p->alpha = 1.0;
                p->alphavel = -1.0 / (0.3 + (rand()&7) * 0.02);
                
                p->org[0] = org[0] + i + ((rand()&23) * crand());
                p->org[1] = org[1] + j + ((rand()&23) * crand());
                p->org[2] = org[2] + k + ((rand()&23) * crand());
    
                dir[0] = j * 8;
                dir[1] = i * 8;
                dir[2] = k * 8;
    
                VectorNormalize (dir);                      
                vel = 50 + (rand()&63);
                VectorScale (dir, vel, p->vel);

                p->accel[0] = p->accel[1] = 0;
                p->accel[2] = -PARTICLE_GRAVITY;
            }
    }
}


/*
===============
CL_BFGExplosionParticles
===============
*/
//FIXME combined with CL_ExplosionParticles
void CL_BFGExplosionParticles (vec3_t org)
{
    int         i, j;
    cparticle_t *p;

    for (i=0 ; i<256 ; i++)
    {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = 0xd0 + (rand()&7);

        for (j=0 ; j<3 ; j++)
        {
            p->org[j] = org[j] + ((rand()%32)-16);
            p->vel[j] = (rand()%384)-192;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0;

        p->alphavel = -0.8 / (0.5 + frand()*0.3);
    }
}


/*
===============
CL_TeleportParticles

===============
*/
void CL_TeleportParticles (vec3_t org)
{
    int         i, j, k;
    cparticle_t *p;
    float       vel;
    vec3_t      dir;

    for (i=-16 ; i<=16 ; i+=4)
        for (j=-16 ; j<=16 ; j+=4)
            for (k=-16 ; k<=32 ; k+=4)
            {
                p = CL_AllocParticle();
                if (!p)
                    return;

                p->time = cl.time;
                p->color = 7 + (rand()&7);

                p->alpha = 1.0;
                p->alphavel = -1.0 / (0.3 + (rand()&7) * 0.02);
                
                p->org[0] = org[0] + i + (rand()&3);
                p->org[1] = org[1] + j + (rand()&3);
                p->org[2] = org[2] + k + (rand()&3);
    
                dir[0] = j*8;
                dir[1] = i*8;
                dir[2] = k*8;
    
                VectorNormalize (dir);                      
                vel = 50 + (rand()&63);
                VectorScale (dir, vel, p->vel);

                p->accel[0] = p->accel[1] = 0;
                p->accel[2] = -PARTICLE_GRAVITY;
            }
}



extern int          r_numparticles;
extern particle_t   r_particles[MAX_PARTICLES];

/*
===============
CL_AddParticles
===============
*/
void CL_AddParticles (void)
{
    cparticle_t     *p, *next;
    float           alpha;
    float           time = 0, time2;
    //vec3_t            org;
    int             color;
    cparticle_t     *active, *tail;
    particle_t      *part;

    active = NULL;
    tail = NULL;

    for (p=active_particles ; p ; p=next)
    {
        next = p->next;

        // PMM - added INSTANT_PARTICLE handling for heat beam
        if (p->alphavel != INSTANT_PARTICLE)
        {
            time = (cl.time - p->time)*0.001;
            alpha = p->alpha + time*p->alphavel;
            if (alpha <= 0)
            {   // faded out
                p->next = free_particles;
                free_particles = p;
                continue;
            }
        }
        else
        {
            alpha = p->alpha;
        }

        if (r_numparticles >= MAX_PARTICLES)
            break;
        part = &r_particles[r_numparticles++];

        p->next = NULL;
        if (!tail)
            active = tail = p;
        else
        {
            tail->next = p;
            tail = p;
        }

        if (alpha > 1.0)
            alpha = 1;
        color = p->color;

        time2 = time*time;

        part->origin[0] = p->org[0] + p->vel[0]*time + p->accel[0]*time2;
        part->origin[1] = p->org[1] + p->vel[1]*time + p->accel[1]*time2;
        part->origin[2] = p->org[2] + p->vel[2]*time + p->accel[2]*time2;

        if( color == 255 ) {
            *( uint32_t * )part->rgb = *( uint32_t * )p->rgb;
        }

        part->color = color;
        part->alpha = alpha;

//      V_AddParticle( &part );
        // PMM
        if (p->alphavel == INSTANT_PARTICLE)
        {
            p->alphavel = 0.0;
            p->alpha = 0.0;
        }
    }

    active_particles = active;
}


/*
==============
CL_EntityEvent

An entity has just been parsed that has an event value
==============
*/
extern qhandle_t cl_sfx_footsteps[4];

void CL_EntityEvent (int number)
{
    centity_t *cent = &cl_entities[number];

    // EF_TELEPORTER acts like an event, but is not cleared each frame
    if( cent->current.effects & EF_TELEPORTER ) {
        CL_TeleporterParticles( cent->current.origin );
    }

    switch (cent->current.event)
    {
    case EV_ITEM_RESPAWN:
        S_StartSound (NULL, number, CHAN_WEAPON, S_RegisterSound("items/respawn1.wav"), 1, ATTN_IDLE, 0);
        CL_ItemRespawnParticles (cent->current.origin);
        break;
    case EV_PLAYER_TELEPORT:
        S_StartSound (NULL, number, CHAN_WEAPON, S_RegisterSound("misc/tele1.wav"), 1, ATTN_IDLE, 0);
        CL_TeleportParticles (cent->current.origin);
        break;
    case EV_FOOTSTEP:
        if (cl_footsteps->integer)
            S_StartSound (NULL, number, CHAN_BODY, cl_sfx_footsteps[rand()&3], 1, ATTN_NORM, 0);
        break;
    case EV_FALLSHORT:
        S_StartSound (NULL, number, CHAN_AUTO, S_RegisterSound ("player/land1.wav"), 1, ATTN_NORM, 0);
        break;
    case EV_FALL:
        S_StartSound (NULL, number, CHAN_AUTO, S_RegisterSound ("*fall2.wav"), 1, ATTN_NORM, 0);
        break;
    case EV_FALLFAR:
        S_StartSound (NULL, number, CHAN_AUTO, S_RegisterSound ("*fall1.wav"), 1, ATTN_NORM, 0);
        break;
    }
}


/*
==============
CL_ClearEffects

==============
*/
void CL_ClearEffects (void)
{
    CL_ClearParticles ();
    CL_ClearDlights ();
    CL_ClearLightStyles ();
    memset (cl_lasers, 0, sizeof(cl_lasers));
}

void CL_InitEffects( void ) {
    cl_railtrail_type = Cvar_Get( "cl_railtrail_type", "0", 0 );
    cl_railtrail_time = Cvar_Get( "cl_railtrail_time", "1.0", 0 );
    cl_railcore_color = Cvar_Get( "cl_railcore_color", "red", 0 );
    cl_railcore_color->changed = cl_railcore_color_changed;
    cl_railcore_color->generator = Com_Color_g;
    cl_railcore_color_changed( cl_railcore_color );
    cl_railcore_width = Cvar_Get( "cl_railcore_width", "2", 0 );
    cl_railspiral_color = Cvar_Get( "cl_railspiral_color", "blue", 0 );
    cl_railspiral_color->changed = cl_railspiral_color_changed;
    cl_railspiral_color->generator = Com_Color_g;
    cl_railspiral_color_changed( cl_railspiral_color );
    cl_railspiral_radius = Cvar_Get( "cl_railspiral_radius", "3", 0 );
}

