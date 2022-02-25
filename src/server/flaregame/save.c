/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2022 Frank Richter

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

#include "shared/shared.h"
#include "local.h"

#include <setjmp.h>

#define FLARE_SAVE_MAGIC        (('V'<<24)|('A'<<16)|('S'<<8)|'F')  // "FSAV"
#define FLARE_SAVE_VERSION      0

typedef struct iocontext_s
{
    FILE *fp;
    jmp_buf err_jmp;
} iocontext_t;

static void write_data(void *buf, size_t len, iocontext_t *io)
{
    if (fwrite(buf, 1, len, io->fp) != len) {
        flaregame.real_gi.dprintf("%s: couldn't write %zu bytes", __func__, len);
        longjmp(io->err_jmp, -1);
    }
}

static void write_int(iocontext_t *io, int v)
{
    v = LittleLong(v);
    write_data(&v, sizeof(v), io);
}

static void write_float(iocontext_t *io, float v)
{
    v = LittleFloat(v);
    write_data(&v, sizeof(v), io);
}

static void write_vector(iocontext_t *io, vec_t *v)
{
    write_float(io, v[0]);
    write_float(io, v[1]);
    write_float(io, v[2]);
}

static void write_flaregame_ent(iocontext_t *io, struct flaregame_ent_s *ent)
{
    write_vector(io, ent->s.origin);
    write_vector(io, ent->s.angles);

    write_vector(io, ent->velocity);
    write_vector(io, ent->angular_velocity);
    write_int(io, ent->watertype);
    write_int(io, ent->waterlevel);

    int groundentity_num = -1;
    if(ent->groundentity) {
        groundentity_num = ((byte*)ent->groundentity - (byte*)flaregame.real_ge->edicts) / flaregame.real_ge->edict_size;
    }
    write_int(io, groundentity_num);
    write_int(io, ent->groundentity_linkcount);
    write_vector(io, ent->groundentity_origin);

    write_int(io, ent->nextthink);
    write_int(io, ent->eoltime);
}

qboolean FlareSave_Write(FILE *f)
{
    iocontext_t io;
    io.fp = f;
    if(setjmp(io.err_jmp) != 0) {
        return false;
    }

    write_int(&io, FLARE_SAVE_MAGIC);
    write_int(&io, FLARE_SAVE_VERSION);

    write_int(&io, flaregame.level.framenum);

    flaregame_flare_t *flare;
    LIST_FOR_EACH(flaregame_flare_t, flare, &flaregame.active_flares, entry)
    {
        write_int(&io, 1);
        write_flaregame_ent(&io, &flare->ent);
    }
    write_int(&io, 0); // end-of-list marker

    return true;
}

static void read_data(void *buf, size_t len, iocontext_t *io)
{
    if (fread(buf, 1, len, io->fp) != len) {
        flaregame.real_gi.dprintf("%s: couldn't read %zu bytes", __func__, len);
        longjmp(io->err_jmp, -1);
    }
}

static int read_int(iocontext_t *io)
{
    int v;

    read_data(&v, sizeof(v), io);
    return LittleLong(v);
}

static float read_float(iocontext_t *io)
{
    float v;

    read_data(&v, sizeof(v), io);
    return LittleFloat(v);
}

static void read_vector(iocontext_t *io, vec_t *v)
{
    v[0] = read_float(io);
    v[1] = read_float(io);
    v[2] = read_float(io);
}

static void read_flaregame_ent(iocontext_t *io, struct flaregame_ent_s *ent)
{
    read_vector(io, ent->s.origin);
    read_vector(io, ent->s.angles);

    read_vector(io, ent->velocity);
    read_vector(io, ent->angular_velocity);
    ent->watertype = read_int(io);
    ent->waterlevel = read_int(io);

    int groundentity_num = read_int(io);
    if(groundentity_num != -1) {
        ent->groundentity = (edict_t *)((byte *)flaregame.real_ge->edicts + groundentity_num * flaregame.real_ge->edict_size);
    }
    ent->groundentity_linkcount = read_int(io);
    read_vector(io, ent->groundentity_origin);

    ent->nextthink = read_int(io);
    ent->eoltime = read_int(io);
}

qboolean FlareSave_Read(FILE *f)
{
    iocontext_t io;
    io.fp = f;
    if(setjmp(io.err_jmp) != 0) {
        return false;
    }

    int i = read_int(&io);
    if (i != FLARE_SAVE_MAGIC) {
        flaregame.real_gi.dprintf("Not a flare save game");
        return false;
    }

    i = read_int(&io);
    if (i > FLARE_SAVE_VERSION) {
        flaregame.real_gi.dprintf("Flare save game version not supported (got %d, expected %d)", i, FLARE_SAVE_VERSION);
        return false;
    }

    flaregame.level.framenum = read_int(&io);

    int marker = read_int(&io);
    while(marker != 0)
    {
        struct flaregame_ent_s* ent = Flare_Spawn(NULL);
        read_flaregame_ent(&io, ent);
        marker = read_int(&io);
    }

    return true;
}
