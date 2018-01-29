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

#include "g_local.h"
#include "g_ptrs.h"

//#define _DEBUG
typedef struct {
    fieldtype_t type;
#ifdef _DEBUG
    char *name;
#endif
    size_t ofs;
    size_t size;
} save_field_t;

#ifdef _DEBUG
#define _FA(type, name, size) { type, #name, _OFS(name), size }
#else
#define _FA(type, name, size) { type, _OFS(name), size }
#endif
#define _F(type, name) _FA(type, name, 1)
#define SZ(name, size) _FA(F_ZSTRING, name, size)
#define BA(name, size) _FA(F_BYTE, name, size)
#define B(name) BA(name, 1)
#define SA(name, size) _FA(F_SHORT, name, size)
#define S(name) SA(name, 1)
#define IA(name, size) _FA(F_INT, name, size)
#define I(name) IA(name, 1)
#define FA(name, size) _FA(F_FLOAT, name, size)
#define F(name) FA(name, 1)
#define L(name) _F(F_LSTRING, name)
#define V(name) _F(F_VECTOR, name)
#define T(name) _F(F_ITEM, name)
#define E(name) _F(F_EDICT, name)
#define P(name, type) _FA(F_POINTER, name, type)

static const save_field_t entityfields[] = {
#define _OFS FOFS
    V(s.origin),
    V(s.angles),
    V(s.old_origin),
    I(s.modelindex),
    I(s.modelindex2),
    I(s.modelindex3),
    I(s.modelindex4),
    I(s.frame),
    I(s.skinnum),
    I(s.effects),
    I(s.renderfx),
    I(s.solid),
    I(s.sound),
    I(s.event),

    // [...]

    I(svflags),
    V(mins),
    V(maxs),
    V(absmin),
    V(absmax),
    V(size),
    I(solid),
    I(clipmask),
    E(owner),

    I(movetype),
    I(flags),

    L(model),
    F(freetime),

    L(message),
    L(classname),
    I(spawnflags),

    F(timestamp),

    L(target),
    L(targetname),
    L(killtarget),
    L(team),
    L(pathtarget),
    L(deathtarget),
    L(combattarget),
    E(target_ent),

    F(speed),
    F(accel),
    F(decel),
    V(movedir),
    V(pos1),
    V(pos2),

    V(velocity),
    V(avelocity),
    I(mass),
    F(air_finished),
    F(gravity),

    E(goalentity),
    E(movetarget),
    F(yaw_speed),
    F(ideal_yaw),

    F(nextthink),
    P(prethink, P_prethink),
    P(think, P_think),
    P(blocked, P_blocked),
    P(touch, P_touch),
    P(use, P_use),
    P(pain, P_pain),
    P(die, P_die),

    F(touch_debounce_time),
    F(pain_debounce_time),
    F(damage_debounce_time),
    F(fly_sound_debounce_time),
    F(last_move_time),

    I(health),
    I(max_health),
    I(gib_health),
    I(deadflag),
    I(show_hostile),

    F(powerarmor_time),

    L(map),

    I(viewheight),
    I(takedamage),
    I(dmg),
    I(radius_dmg),
    F(dmg_radius),
    I(sounds),
    I(count),

    E(chain),
    E(enemy),
    E(oldenemy),
    E(activator),
    E(groundentity),
    I(groundentity_linkcount),
    E(teamchain),
    E(teammaster),

    E(mynoise),
    E(mynoise2),

    I(noise_index),
    I(noise_index2),
    F(volume),
    F(attenuation),

    F(wait),
    F(delay),
    F(random),

    F(teleport_time),

    I(watertype),
    I(waterlevel),

    V(move_origin),
    V(move_angles),

    I(light_level),

    I(style),

    T(item),

    V(moveinfo.start_origin),
    V(moveinfo.start_angles),
    V(moveinfo.end_origin),
    V(moveinfo.end_angles),

    I(moveinfo.sound_start),
    I(moveinfo.sound_middle),
    I(moveinfo.sound_end),

    F(moveinfo.accel),
    F(moveinfo.speed),
    F(moveinfo.decel),
    F(moveinfo.distance),

    F(moveinfo.wait),

    I(moveinfo.state),
    V(moveinfo.dir),
    F(moveinfo.current_speed),
    F(moveinfo.move_speed),
    F(moveinfo.next_speed),
    F(moveinfo.remaining_distance),
    F(moveinfo.decel_distance),
    P(moveinfo.endfunc, P_moveinfo_endfunc),

    P(monsterinfo.currentmove, P_monsterinfo_currentmove),
    I(monsterinfo.aiflags),
    I(monsterinfo.nextframe),
    F(monsterinfo.scale),

    P(monsterinfo.stand, P_monsterinfo_stand),
    P(monsterinfo.idle, P_monsterinfo_idle),
    P(monsterinfo.search, P_monsterinfo_search),
    P(monsterinfo.walk, P_monsterinfo_walk),
    P(monsterinfo.run, P_monsterinfo_run),
    P(monsterinfo.dodge, P_monsterinfo_dodge),
    P(monsterinfo.attack, P_monsterinfo_attack),
    P(monsterinfo.melee, P_monsterinfo_melee),
    P(monsterinfo.sight, P_monsterinfo_sight),
    P(monsterinfo.checkattack, P_monsterinfo_checkattack),

    F(monsterinfo.pausetime),
    F(monsterinfo.attack_finished),

    V(monsterinfo.saved_goal),
    F(monsterinfo.search_time),
    F(monsterinfo.trail_time),
    V(monsterinfo.last_sighting),
    I(monsterinfo.attack_state),
    I(monsterinfo.lefty),
    F(monsterinfo.idle_time),
    I(monsterinfo.linkcount),

    I(monsterinfo.power_armor_type),
    I(monsterinfo.power_armor_power),

    {0}
#undef _OFS
};

static const save_field_t levelfields[] = {
#define _OFS LLOFS
    I(framenum),
    F(time),

    SZ(level_name, MAX_QPATH),
    SZ(mapname, MAX_QPATH),
    SZ(nextmap, MAX_QPATH),

    F(intermissiontime),
    L(changemap),
    I(exitintermission),
    V(intermission_origin),
    V(intermission_angle),

    E(sight_client),

    E(sight_entity),
    I(sight_entity_framenum),
    E(sound_entity),
    I(sound_entity_framenum),
    E(sound2_entity),
    I(sound2_entity_framenum),

    I(pic_health),

    I(total_secrets),
    I(found_secrets),

    I(total_goals),
    I(found_goals),

    I(total_monsters),
    I(killed_monsters),

    I(body_que),

    I(power_cubes),

    {0}
#undef _OFS
};

static const save_field_t clientfields[] = {
#define _OFS CLOFS
    I(ps.pmove.pm_type),

    SA(ps.pmove.origin, 3),
    SA(ps.pmove.velocity, 3),
    B(ps.pmove.pm_flags),
    B(ps.pmove.pm_time),
    S(ps.pmove.gravity),
    SA(ps.pmove.delta_angles, 3),

    V(ps.viewangles),
    V(ps.viewoffset),
    V(ps.kick_angles),

    V(ps.gunangles),
    V(ps.gunoffset),
    I(ps.gunindex),
    I(ps.gunframe),

    FA(ps.blend, 4),

    F(ps.fov),

    I(ps.rdflags),

    SA(ps.stats, MAX_STATS),

    SZ(pers.userinfo, MAX_INFO_STRING),
    SZ(pers.netname, 16),
    I(pers.hand),

    I(pers.connected),

    I(pers.health),
    I(pers.max_health),
    I(pers.savedFlags),

    I(pers.selected_item),
    IA(pers.inventory, MAX_ITEMS),

    I(pers.max_bullets),
    I(pers.max_shells),
    I(pers.max_rockets),
    I(pers.max_grenades),
    I(pers.max_cells),
    I(pers.max_slugs),

    T(pers.weapon),
    T(pers.lastweapon),

    I(pers.power_cubes),
    I(pers.score),

    I(pers.game_helpchanged),
    I(pers.helpchanged),

    I(pers.spectator),

    I(showscores),
    I(showinventory),
    I(showhelp),
    I(showhelpicon),

    I(ammo_index),

    T(newweapon),

    I(damage_armor),
    I(damage_parmor),
    I(damage_blood),
    I(damage_knockback),
    V(damage_from),

    F(killer_yaw),

    I(weaponstate),

    V(kick_angles),
    V(kick_origin),
    F(v_dmg_roll),
    F(v_dmg_pitch),
    F(v_dmg_time),
    F(fall_time),
    F(fall_value),
    F(damage_alpha),
    F(bonus_alpha),
    V(damage_blend),
    V(v_angle),
    F(bobtime),
    V(oldviewangles),
    V(oldvelocity),

    F(next_drown_time),
    I(old_waterlevel),
    I(breather_sound),

    I(machinegun_shots),

    I(anim_end),
    I(anim_priority),
    I(anim_duck),
    I(anim_run),

    // powerup timers
    I(quad_framenum),
    I(invincible_framenum),
    I(breather_framenum),
    I(enviro_framenum),

    I(grenade_blew_up),
    F(grenade_time),
    I(silencer_shots),
    I(weapon_sound),

    F(pickup_msg_time),

    {0}
#undef _OFS
};

static const save_field_t gamefields[] = {
#define _OFS GLOFS
    SZ(helpmessage1, 512),
    SZ(helpmessage2, 512),

    I(maxclients),
    I(maxentities),

    I(serverflags),

    I(num_items),

    I(autosaved),

    {0}
#undef _OFS
};

//=========================================================

static void write_data(void *buf, size_t len, FILE *f)
{
    if (fwrite(buf, 1, len, f) != len) {
        gi.error("%s: couldn't write %"PRIz" bytes", __func__, len);
    }
}

static void write_short(FILE *f, short v)
{
    v = LittleShort(v);
    write_data(&v, sizeof(v), f);
}

static void write_int(FILE *f, int v)
{
    v = LittleLong(v);
    write_data(&v, sizeof(v), f);
}

static void write_float(FILE *f, float v)
{
    v = LittleFloat(v);
    write_data(&v, sizeof(v), f);
}

static void write_string(FILE *f, char *s)
{
    size_t len;

    if (!s) {
        write_int(f, -1);
        return;
    }

    len = strlen(s);
    write_int(f, len);
    write_data(s, len, f);
}

static void write_vector(FILE *f, vec_t *v)
{
    write_float(f, v[0]);
    write_float(f, v[1]);
    write_float(f, v[2]);
}

static void write_index(FILE *f, void *p, size_t size, void *start, int max_index)
{
    size_t diff;

    if (!p) {
        write_int(f, -1);
        return;
    }

    if (p < start || (byte *)p > (byte *)start + max_index * size) {
        gi.error("%s: pointer out of range: %p", __func__, p);
    }

    diff = (byte *)p - (byte *)start;
    if (diff % size) {
        gi.error("%s: misaligned pointer: %p", __func__, p);
    }
    write_int(f, (int)(diff / size));
}

static void write_pointer(FILE *f, void *p, ptr_type_t type)
{
    const save_ptr_t *ptr;
    int i;

    if (!p) {
        write_int(f, -1);
        return;
    }

    for (i = 0, ptr = save_ptrs; i < num_save_ptrs; i++, ptr++) {
        if (ptr->type == type && ptr->ptr == p) {
            write_int(f, i);
            return;
        }
    }

    gi.error("%s: unknown pointer: %p", __func__, p);
}

static void write_field(FILE *f, const save_field_t *field, void *base)
{
    void *p = (byte *)base + field->ofs;
    int i;

    switch (field->type) {
    case F_BYTE:
        write_data(p, field->size, f);
        break;
    case F_SHORT:
        for (i = 0; i < field->size; i++) {
            write_short(f, ((short *)p)[i]);
        }
        break;
    case F_INT:
        for (i = 0; i < field->size; i++) {
            write_int(f, ((int *)p)[i]);
        }
        break;
    case F_FLOAT:
        for (i = 0; i < field->size; i++) {
            write_float(f, ((float *)p)[i]);
        }
        break;
    case F_VECTOR:
        write_vector(f, (vec_t *)p);
        break;

    case F_ZSTRING:
        write_string(f, (char *)p);
        break;
    case F_LSTRING:
        write_string(f, *(char **)p);
        break;

    case F_EDICT:
        write_index(f, *(void **)p, sizeof(edict_t), g_edicts, MAX_EDICTS - 1);
        break;
    case F_CLIENT:
        write_index(f, *(void **)p, sizeof(gclient_t), game.clients, game.maxclients - 1);
        break;
    case F_ITEM:
        write_index(f, *(void **)p, sizeof(gitem_t), itemlist, game.num_items - 1);
        break;

    case F_POINTER:
        write_pointer(f, *(void **)p, field->size);
        break;

    default:
        gi.error("%s: unknown field type", __func__);
    }
}

static void write_fields(FILE *f, const save_field_t *fields, void *base)
{
    const save_field_t *field;

    for (field = fields; field->type; field++) {
        write_field(f, field, base);
    }
}

static void read_data(void *buf, size_t len, FILE *f)
{
    if (fread(buf, 1, len, f) != len) {
        gi.error("%s: couldn't read %"PRIz" bytes", __func__, len);
    }
}

static int read_short(FILE *f)
{
    short v;

    read_data(&v, sizeof(v), f);
    v = LittleShort(v);

    return v;
}

static int read_int(FILE *f)
{
    int v;

    read_data(&v, sizeof(v), f);
    v = LittleLong(v);

    return v;
}

static float read_float(FILE *f)
{
    float v;

    read_data(&v, sizeof(v), f);
    v = LittleFloat(v);

    return v;
}


static char *read_string(FILE *f)
{
    int len;
    char *s;

    len = read_int(f);
    if (len == -1) {
        return NULL;
    }

    if (len < 0 || len > 65536) {
        gi.error("%s: bad length", __func__);
    }

    s = gi.TagMalloc(len + 1, TAG_LEVEL);
    read_data(s, len, f);
    s[len] = 0;

    return s;
}

static void read_zstring(FILE *f, char *s, size_t size)
{
    int len;

    len = read_int(f);
    if (len < 0 || len >= size) {
        gi.error("%s: bad length", __func__);
    }

    read_data(s, len, f);
    s[len] = 0;
}

static void read_vector(FILE *f, vec_t *v)
{
    v[0] = read_float(f);
    v[1] = read_float(f);
    v[2] = read_float(f);
}

static void *read_index(FILE *f, size_t size, void *start, int max_index)
{
    int index;
    byte *p;

    index = read_int(f);
    if (index == -1) {
        return NULL;
    }

    if (index < 0 || index > max_index) {
        gi.error("%s: bad index", __func__);
    }

    p = (byte *)start + index * size;
    return p;
}

static void *read_pointer(FILE *f, ptr_type_t type)
{
    int index;
    const save_ptr_t *ptr;

    index = read_int(f);
    if (index == -1) {
        return NULL;
    }

    if (index < 0 || index >= num_save_ptrs) {
        gi.error("%s: bad index", __func__);
    }

    ptr = &save_ptrs[index];
    if (ptr->type != type) {
        gi.error("%s: type mismatch", __func__);
    }

    return ptr->ptr;
}

static void read_field(FILE *f, const save_field_t *field, void *base)
{
    void *p = (byte *)base + field->ofs;
    int i;

    switch (field->type) {
    case F_BYTE:
        read_data(p, field->size, f);
        break;
    case F_SHORT:
        for (i = 0; i < field->size; i++) {
            ((short *)p)[i] = read_short(f);
        }
        break;
    case F_INT:
        for (i = 0; i < field->size; i++) {
            ((int *)p)[i] = read_int(f);
        }
        break;
    case F_FLOAT:
        for (i = 0; i < field->size; i++) {
            ((float *)p)[i] = read_float(f);
        }
        break;
    case F_VECTOR:
        read_vector(f, (vec_t *)p);
        break;

    case F_LSTRING:
        *(char **)p = read_string(f);
        break;
    case F_ZSTRING:
        read_zstring(f, (char *)p, field->size);
        break;

    case F_EDICT:
        *(edict_t **)p = read_index(f, sizeof(edict_t), g_edicts, game.maxentities - 1);
        break;
    case F_CLIENT:
        *(gclient_t **)p = read_index(f, sizeof(gclient_t), game.clients, game.maxclients - 1);
        break;
    case F_ITEM:
        *(gitem_t **)p = read_index(f, sizeof(gitem_t), itemlist, game.num_items - 1);
        break;

    case F_POINTER:
        *(void **)p = read_pointer(f, field->size);
        break;

    default:
        gi.error("%s: unknown field type", __func__);
    }
}

static void read_fields(FILE *f, const save_field_t *fields, void *base)
{
    const save_field_t *field;

    for (field = fields; field->type; field++) {
        read_field(f, field, base);
    }
}

//=========================================================

#define SAVE_MAGIC1     (('1'<<24)|('V'<<16)|('S'<<8)|'S')  // "SSV1"
#define SAVE_MAGIC2     (('1'<<24)|('V'<<16)|('A'<<8)|'S')  // "SAV1"
#define SAVE_VERSION    2

/*
============
WriteGame

This will be called whenever the game goes to a new level,
and when the user explicitly saves the game.

Game information include cross level data, like multi level
triggers, help computer info, and all client states.

A single player death will automatically restore from the
last save position.
============
*/
void WriteGame(const char *filename, qboolean autosave)
{
    FILE    *f;
    int     i;

    if (!autosave)
        SaveClientData();

    f = fopen(filename, "wb");
    if (!f)
        gi.error("Couldn't open %s", filename);

    write_int(f, SAVE_MAGIC1);
    write_int(f, SAVE_VERSION);

    game.autosaved = autosave;
    write_fields(f, gamefields, &game);
    game.autosaved = qfalse;

    for (i = 0; i < game.maxclients; i++) {
        write_fields(f, clientfields, &game.clients[i]);
    }

    fclose(f);
}

void ReadGame(const char *filename)
{
    FILE    *f;
    int     i;

    gi.FreeTags(TAG_GAME);

    f = fopen(filename, "rb");
    if (!f)
        gi.error("Couldn't open %s", filename);

    i = read_int(f);
    if (i != SAVE_MAGIC1) {
        fclose(f);
        gi.error("Not a save game");
    }

    i = read_int(f);
    if (i != SAVE_VERSION) {
        fclose(f);
        gi.error("Savegame from an older version");
    }

    read_fields(f, gamefields, &game);

    // should agree with server's version
    if (game.maxclients != (int)maxclients->value) {
        fclose(f);
        gi.error("Savegame has bad maxclients");
    }
    if (game.maxentities <= game.maxclients || game.maxentities > MAX_EDICTS) {
        fclose(f);
        gi.error("Savegame has bad maxentities");
    }

    g_edicts = gi.TagMalloc(game.maxentities * sizeof(g_edicts[0]), TAG_GAME);
    globals.edicts = g_edicts;
    globals.max_edicts = game.maxentities;

    game.clients = gi.TagMalloc(game.maxclients * sizeof(game.clients[0]), TAG_GAME);
    for (i = 0; i < game.maxclients; i++) {
        read_fields(f, clientfields, &game.clients[i]);
    }

    fclose(f);
}

//==========================================================


/*
=================
WriteLevel

=================
*/
void WriteLevel(const char *filename)
{
    int     i;
    edict_t *ent;
    FILE    *f;

    f = fopen(filename, "wb");
    if (!f)
        gi.error("Couldn't open %s", filename);

    write_int(f, SAVE_MAGIC2);
    write_int(f, SAVE_VERSION);

    // write out level_locals_t
    write_fields(f, levelfields, &level);

    // write out all the entities
    for (i = 0; i < globals.num_edicts; i++) {
        ent = &g_edicts[i];
        if (!ent->inuse)
            continue;
        write_int(f, i);
        write_fields(f, entityfields, ent);
    }
    write_int(f, -1);

    fclose(f);
}


/*
=================
ReadLevel

SpawnEntities will allready have been called on the
level the same way it was when the level was saved.

That is necessary to get the baselines
set up identically.

The server will have cleared all of the world links before
calling ReadLevel.

No clients are connected yet.
=================
*/
void ReadLevel(const char *filename)
{
    int     entnum;
    FILE    *f;
    int     i;
    edict_t *ent;

    // free any dynamic memory allocated by loading the level
    // base state
    gi.FreeTags(TAG_LEVEL);

    f = fopen(filename, "rb");
    if (!f)
        gi.error("Couldn't open %s", filename);

    // wipe all the entities
    memset(g_edicts, 0, game.maxentities * sizeof(g_edicts[0]));
    globals.num_edicts = maxclients->value + 1;

    i = read_int(f);
    if (i != SAVE_MAGIC2) {
        fclose(f);
        gi.error("Not a save game");
    }

    i = read_int(f);
    if (i != SAVE_VERSION) {
        fclose(f);
        gi.error("Savegame from an older version");
    }

    // load the level locals
    read_fields(f, levelfields, &level);

    // load all the entities
    while (1) {
        entnum = read_int(f);
        if (entnum == -1)
            break;
        if (entnum < 0 || entnum >= game.maxentities) {
            gi.error("%s: bad entity number", __func__);
        }
        if (entnum >= globals.num_edicts)
            globals.num_edicts = entnum + 1;

        ent = &g_edicts[entnum];
        read_fields(f, entityfields, ent);
        ent->inuse = qtrue;
        ent->s.number = entnum;

        // let the server rebuild world links for this ent
        memset(&ent->area, 0, sizeof(ent->area));
        gi.linkentity(ent);
    }

    fclose(f);

    // mark all clients as unconnected
    for (i = 0 ; i < maxclients->value ; i++) {
        ent = &g_edicts[i + 1];
        ent->client = game.clients + i;
        ent->client->pers.connected = qfalse;
    }

    // do any load time things at this point
    for (i = 0 ; i < globals.num_edicts ; i++) {
        ent = &g_edicts[i];

        if (!ent->inuse)
            continue;

        // fire any cross-level triggers
        if (ent->classname)
            if (strcmp(ent->classname, "target_crosslevel_target") == 0)
                ent->nextthink = level.time + ent->delay;

        if (ent->think == func_clock_think || ent->use == func_clock_use) {
            char *msg = ent->message;
            ent->message = gi.TagMalloc(CLOCK_MESSAGE_SIZE, TAG_LEVEL);
            if (msg) {
                Q_strlcpy(ent->message, msg, CLOCK_MESSAGE_SIZE);
                gi.TagFree(msg);
            }
        }
    }
}

