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
// cl_newfx.c -- MORE entity effects parsing and management

#include "client.h"

#if USE_DLIGHTS
void CL_Flashlight(int ent, vec3_t pos)
{
    cdlight_t   *dl;

    dl = CL_AllocDlight(ent);
    VectorCopy(pos,  dl->origin);
    dl->radius = 400;
    //dl->minlight = 250;
    dl->die = cl.time + 100;
    dl->color[0] = 1;
    dl->color[1] = 1;
    dl->color[2] = 1;
}

/*
======
CL_ColorFlash - flash of light
======
*/
void CL_ColorFlash(vec3_t pos, int ent, int intensity, float r, float g, float b)
{
    cdlight_t   *dl;

    dl = CL_AllocDlight(ent);
    VectorCopy(pos,  dl->origin);
    dl->radius = intensity;
    //dl->minlight = 250;
    dl->die = cl.time + 100;
    dl->color[0] = r;
    dl->color[1] = g;
    dl->color[2] = b;
}
#endif


/*
======
CL_DebugTrail
======
*/
void CL_DebugTrail(vec3_t start, vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    cparticle_t *p;
    float       dec;
    vec3_t      right, up;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    MakeNormalVectors(vec, right, up);

    dec = 3;
    VectorScale(vec, dec, vec);
    VectorCopy(start, move);

    while (len > 0) {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        VectorClear(p->accel);
        VectorClear(p->vel);
        p->alpha = 1.0;
        p->alphavel = -0.1;
        p->color = 0x74 + (rand() & 7);
        VectorCopy(move, p->org);
        VectorAdd(move, vec, move);
    }
}

void CL_ForceWall(vec3_t start, vec3_t end, int color)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    VectorScale(vec, 4, vec);

    // FIXME: this is a really silly way to have a loop
    while (len > 0) {
        len -= 4;

        if (frand() > 0.3) {
            p = CL_AllocParticle();
            if (!p)
                return;
            VectorClear(p->accel);

            p->time = cl.time;

            p->alpha = 1.0;
            p->alphavel =  -1.0 / (3.0 + frand() * 0.5);
            p->color = color;
            for (j = 0; j < 3; j++) {
                p->org[j] = move[j] + crand() * 3;
                p->accel[j] = 0;
            }
            p->vel[0] = 0;
            p->vel[1] = 0;
            p->vel[2] = -40 - (crand() * 10);
        }

        VectorAdd(move, vec, move);
    }
}


/*
===============
CL_BubbleTrail2 (lets you control the # of bubbles by setting the distance between the spawns)

===============
*/
void CL_BubbleTrail2(vec3_t start, vec3_t end, int dist)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         i, j;
    cparticle_t *p;
    float       dec;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    dec = dist;
    VectorScale(vec, dec, vec);

    for (i = 0; i < len; i += dec) {
        p = CL_AllocParticle();
        if (!p)
            return;

        VectorClear(p->accel);
        p->time = cl.time;

        p->alpha = 1.0;
        p->alphavel = -1.0 / (1 + frand() * 0.1);
        p->color = 4 + (rand() & 7);
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand() * 2;
            p->vel[j] = crand() * 10;
        }
        p->org[2] -= 4;
        p->vel[2] += 20;

        VectorAdd(move, vec, move);
    }
}

void CL_Heatbeam(vec3_t start, vec3_t forward)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    int         i;
    float       c, s;
    vec3_t      dir;
    float       ltime;
    float       step = 32.0, rstep;
    float       start_pt;
    float       rot;
    float       variance;
    vec3_t      end;

    VectorMA(start, 4096, forward, end);

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    ltime = (float) cl.time / 1000.0;
    start_pt = fmod(ltime * 96.0, step);
    VectorMA(move, start_pt, vec, move);

    VectorScale(vec, step, vec);

    rstep = M_PI / 10.0;
    for (i = start_pt; i < len; i += step) {
        if (i > step * 5) // don't bother after the 5th ring
            break;

        for (rot = 0; rot < M_PI * 2; rot += rstep) {
            p = CL_AllocParticle();
            if (!p)
                return;

            p->time = cl.time;
            VectorClear(p->accel);
            variance = 0.5;
            c = cos(rot) * variance;
            s = sin(rot) * variance;

            // trim it so it looks like it's starting at the origin
            if (i < 10) {
                VectorScale(cl.v_right, c * (i / 10.0), dir);
                VectorMA(dir, s * (i / 10.0), cl.v_up, dir);
            } else {
                VectorScale(cl.v_right, c, dir);
                VectorMA(dir, s, cl.v_up, dir);
            }

            p->alpha = 0.5;
            p->alphavel = -1000.0;
            p->color = 223 - (rand() & 7);
            for (j = 0; j < 3; j++) {
                p->org[j] = move[j] + dir[j] * 3;
                p->vel[j] = 0;
            }
        }

        VectorAdd(move, vec, move);
    }
}


/*
===============
CL_ParticleSteamEffect

Puffs with velocity along direction, with some randomness thrown in
===============
*/
void CL_ParticleSteamEffect(vec3_t org, vec3_t dir, int color, int count, int magnitude)
{
    int         i, j;
    cparticle_t *p;
    float       d;
    vec3_t      r, u;

    MakeNormalVectors(dir, r, u);

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color + (rand() & 7);

        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + magnitude * 0.1 * crand();
        }
        VectorScale(dir, magnitude, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, r, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, u, p->vel);

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY / 2;
        p->alpha = 1.0;

        p->alphavel = -1.0 / (0.5 + frand() * 0.3);
    }
}

void CL_ParticleSteamEffect2(cl_sustain_t *self)
{
    int         i, j;
    cparticle_t *p;
    float       d;
    vec3_t      r, u;
    vec3_t      dir;

    VectorCopy(self->dir, dir);
    MakeNormalVectors(dir, r, u);

    for (i = 0; i < self->count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = self->color + (rand() & 7);

        for (j = 0; j < 3; j++) {
            p->org[j] = self->org[j] + self->magnitude * 0.1 * crand();
        }
        VectorScale(dir, self->magnitude, p->vel);
        d = crand() * self->magnitude / 3;
        VectorMA(p->vel, d, r, p->vel);
        d = crand() * self->magnitude / 3;
        VectorMA(p->vel, d, u, p->vel);

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY / 2;
        p->alpha = 1.0;

        p->alphavel = -1.0 / (0.5 + frand() * 0.3);
    }

    self->nextthink += self->thinkinterval;
}

/*
===============
CL_TrackerTrail
===============
*/
void CL_TrackerTrail(vec3_t start, vec3_t end, int particleColor)
{
    vec3_t      move;
    vec3_t      vec;
    vec3_t      forward, right, up, angle_dir;
    float       len;
    int         j;
    cparticle_t *p;
    int         dec;
    float       dist;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    VectorCopy(vec, forward);
    vectoangles2(forward, angle_dir);
    AngleVectors(angle_dir, forward, right, up);

    dec = 3;
    VectorScale(vec, 3, vec);

    // FIXME: this is a really silly way to have a loop
    while (len > 0) {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0;
        p->alphavel = -2.0;
        p->color = particleColor;
        dist = DotProduct(move, forward);
        VectorMA(move, 8 * cos(dist), up, p->org);
        for (j = 0; j < 3; j++) {
            p->vel[j] = 0;
            p->accel[j] = 0;
        }
        p->vel[2] = 5;

        VectorAdd(move, vec, move);
    }
}

void CL_Tracker_Shell(vec3_t origin)
{
    vec3_t          dir;
    int             i;
    cparticle_t     *p;

    for (i = 0; i < 300; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0;
        p->alphavel = INSTANT_PARTICLE;
        p->color = 0;

        dir[0] = crand();
        dir[1] = crand();
        dir[2] = crand();
        VectorNormalize(dir);

        VectorMA(origin, 40, dir, p->org);
    }
}

void CL_MonsterPlasma_Shell(vec3_t origin)
{
    vec3_t          dir;
    int             i;
    cparticle_t     *p;

    for (i = 0; i < 40; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0;
        p->alphavel = INSTANT_PARTICLE;
        p->color = 0xe0;

        dir[0] = crand();
        dir[1] = crand();
        dir[2] = crand();
        VectorNormalize(dir);

        VectorMA(origin, 10, dir, p->org);
    }
}

void CL_Widowbeamout(cl_sustain_t *self)
{
    static const byte   colortable[4] = {2 * 8, 13 * 8, 21 * 8, 18 * 8};
    vec3_t          dir;
    int             i;
    cparticle_t     *p;
    float           ratio;

    ratio = 1.0 - (((float)self->endtime - (float)cl.time) / 2100.0);

    for (i = 0; i < 300; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0;
        p->alphavel = INSTANT_PARTICLE;
        p->color = colortable[rand() & 3];

        dir[0] = crand();
        dir[1] = crand();
        dir[2] = crand();
        VectorNormalize(dir);

        VectorMA(self->org, (45.0 * ratio), dir, p->org);
    }
}

void CL_Nukeblast(cl_sustain_t *self)
{
    static const byte   colortable[4] = {110, 112, 114, 116};
    vec3_t          dir;
    int             i;
    cparticle_t     *p;
    float           ratio;

    ratio = 1.0 - (((float)self->endtime - (float)cl.time) / 1000.0);

    for (i = 0; i < 700; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0;
        p->alphavel = INSTANT_PARTICLE;
        p->color = colortable[rand() & 3];

        dir[0] = crand();
        dir[1] = crand();
        dir[2] = crand();
        VectorNormalize(dir);

        VectorMA(self->org, (200.0 * ratio), dir, p->org);
    }
}

void CL_WidowSplash(void)
{
    static const byte   colortable[4] = {2 * 8, 13 * 8, 21 * 8, 18 * 8};
    int         i;
    cparticle_t *p;
    vec3_t      dir;

    for (i = 0; i < 256; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = colortable[rand() & 3];

        dir[0] = crand();
        dir[1] = crand();
        dir[2] = crand();
        VectorNormalize(dir);
        VectorMA(te.pos1, 45.0, dir, p->org);
        VectorMA(vec3_origin, 40.0, dir, p->vel);

        p->accel[0] = p->accel[1] = 0;
        p->alpha = 1.0;

        p->alphavel = -0.8 / (0.5 + frand() * 0.3);
    }
}

/*
===============
CL_TagTrail

===============
*/
void CL_TagTrail(vec3_t start, vec3_t end, int color)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    int         dec;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    dec = 5;
    VectorScale(vec, 5, vec);

    while (len >= 0) {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0;
        p->alphavel = -1.0 / (0.8 + frand() * 0.2);
        p->color = color;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand() * 16;
            p->vel[j] = crand() * 5;
            p->accel[j] = 0;
        }

        VectorAdd(move, vec, move);
    }
}

/*
===============
CL_ColorExplosionParticles
===============
*/
void CL_ColorExplosionParticles(vec3_t org, int color, int run)
{
    int         i, j;
    cparticle_t *p;

    for (i = 0; i < 128; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color + (rand() % run);

        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((rand() % 32) - 16);
            p->vel[j] = (rand() % 256) - 128;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0;

        p->alphavel = -0.4 / (0.6 + frand() * 0.2);
    }
}

/*
===============
CL_ParticleSmokeEffect - like the steam effect, but unaffected by gravity
===============
*/
void CL_ParticleSmokeEffect(vec3_t org, vec3_t dir, int color, int count, int magnitude)
{
    int         i, j;
    cparticle_t *p;
    float       d;
    vec3_t      r, u;

    MakeNormalVectors(dir, r, u);

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color + (rand() & 7);

        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + magnitude * 0.1 * crand();
        }
        VectorScale(dir, magnitude, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, r, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, u, p->vel);

        p->accel[0] = p->accel[1] = p->accel[2] = 0;
        p->alpha = 1.0;

        p->alphavel = -1.0 / (0.5 + frand() * 0.3);
    }
}

/*
===============
CL_BlasterParticles2

Wall impact puffs (Green)
===============
*/
void CL_BlasterParticles2(vec3_t org, vec3_t dir, unsigned int color)
{
    int         i, j;
    cparticle_t *p;
    float       d;
    int         count;

    count = 40;
    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color + (rand() & 7);

        d = rand() & 15;
        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((rand() & 7) - 4) + d * dir[j];
            p->vel[j] = dir[j] * 30 + crand() * 40;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0;

        p->alphavel = -1.0 / (0.5 + frand() * 0.3);
    }
}

/*
===============
CL_BlasterTrail2

Green!
===============
*/
void CL_BlasterTrail2(vec3_t start, vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    int         dec;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    dec = 5;
    VectorScale(vec, 5, vec);

    // FIXME: this is a really silly way to have a loop
    while (len > 0) {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0;
        p->alphavel = -1.0 / (0.3 + frand() * 0.2);
        p->color = 0xd0;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand();
            p->vel[j] = crand() * 5;
            p->accel[j] = 0;
        }

        VectorAdd(move, vec, move);
    }
}

/*
===============
CL_IonripperTrail
===============
*/
void CL_IonripperTrail(vec3_t start, vec3_t ent)
{
    vec3_t  move;
    vec3_t  vec;
    float   len;
    int     j;
    cparticle_t *p;
    int     dec;
    int     left = 0;

    VectorCopy(start, move);
    VectorSubtract(ent, start, vec);
    len = VectorNormalize(vec);

    dec = 5;
    VectorScale(vec, 5, vec);

    while (len > 0) {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;
        p->alpha = 0.5;
        p->alphavel = -1.0 / (0.3 + frand() * 0.2);
        p->color = 0xe4 + (rand() & 3);

        for (j = 0; j < 3; j++) {
            p->org[j] = move[j];
            p->accel[j] = 0;
        }
        if (left) {
            left = 0;
            p->vel[0] = 10;
        } else {
            left = 1;
            p->vel[0] = -10;
        }

        p->vel[1] = 0;
        p->vel[2] = 0;

        VectorAdd(move, vec, move);
    }
}

/*
===============
CL_TrapParticles
===============
*/
void CL_TrapParticles(entity_t *ent)
{
    vec3_t      move;
    vec3_t      vec;
    vec3_t      start, end;
    float       len;
    int         j;
    cparticle_t *p;
    int         dec;

    ent->origin[2] -= 14;
    VectorCopy(ent->origin, start);
    VectorCopy(ent->origin, end);
    end[2] += 64;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    dec = 5;
    VectorScale(vec, 5, vec);

    // FIXME: this is a really silly way to have a loop
    while (len > 0) {
        len -= dec;

        p = CL_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cl.time;

        p->alpha = 1.0;
        p->alphavel = -1.0 / (0.3 + frand() * 0.2);
        p->color = 0xe0;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand();
            p->vel[j] = crand() * 15;
            p->accel[j] = 0;
        }
        p->accel[2] = PARTICLE_GRAVITY;

        VectorAdd(move, vec, move);
    }

    {
        int         i, j, k;
        cparticle_t *p;
        float       vel;
        vec3_t      dir;
        vec3_t      org;

        ent->origin[2] += 14;
        VectorCopy(ent->origin, org);

        for (i = -2; i <= 2; i += 4)
            for (j = -2; j <= 2; j += 4)
                for (k = -2; k <= 4; k += 4) {
                    p = CL_AllocParticle();
                    if (!p)
                        return;

                    p->time = cl.time;
                    p->color = 0xe0 + (rand() & 3);

                    p->alpha = 1.0;
                    p->alphavel = -1.0 / (0.3 + (rand() & 7) * 0.02);

                    p->org[0] = org[0] + i + ((rand() & 23) * crand());
                    p->org[1] = org[1] + j + ((rand() & 23) * crand());
                    p->org[2] = org[2] + k + ((rand() & 23) * crand());

                    dir[0] = j * 8;
                    dir[1] = i * 8;
                    dir[2] = k * 8;

                    VectorNormalize(dir);
                    vel = 50 + (rand() & 63);
                    VectorScale(dir, vel, p->vel);

                    p->accel[0] = p->accel[1] = 0;
                    p->accel[2] = -PARTICLE_GRAVITY;
                }
    }
}

/*
===============
CL_ParticleEffect3
===============
*/
void CL_ParticleEffect3(vec3_t org, vec3_t dir, int color, int count)
{
    int         i, j;
    cparticle_t *p;
    float       d;

    for (i = 0; i < count; i++) {
        p = CL_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        p->color = color;

        d = rand() & 7;
        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((rand() & 7) - 4) + d * dir[j];
            p->vel[j] = crand() * 20;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = PARTICLE_GRAVITY;
        p->alpha = 1.0;

        p->alphavel = -1.0 / (0.5 + frand() * 0.3);
    }
}

