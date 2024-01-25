/*
Copyright (C) 2003-2008 Andrey Nazarov

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

//
// sv_mvd.c - GTV server and local MVD recorder
//

#include "server.h"
#include "server/mvd/protocol.h"

#define FOR_EACH_GTV(client) \
    LIST_FOR_EACH(gtv_client_t, client, &gtv_client_list, entry)

#define FOR_EACH_ACTIVE_GTV(client) \
    LIST_FOR_EACH(gtv_client_t, client, &gtv_active_list, active)

typedef struct {
    list_t      entry;
    list_t      active;
    clstate_t   state;
    netstream_t stream;
#if USE_ZLIB
    z_stream    z;
#endif
    unsigned    msglen;
    unsigned    lastmessage;

    unsigned    flags;
    unsigned    maxbuf;
    unsigned    bufcount;

    byte        buffer[MAX_GTC_MSGLEN + 4]; // recv buffer
    byte        *data; // send buffer

    char        name[MAX_CLIENT_NAME];
    char        version[MAX_QPATH];
} gtv_client_t;

typedef struct {
    bool            enabled;
    bool            active;
    client_t        *dummy;
    unsigned        layout_time;
    unsigned        clients_active;
    unsigned        players_active;

    msgEsFlags_t    esFlags;
    msgPsFlags_t    psFlags;

    // reliable data, may not be discarded
    sizebuf_t       message;

    // unreliable data, may be discarded
    sizebuf_t       datagram;

    // delta compressor buffers
    player_packed_t  *players;  // [maxclients]
    entity_packed_t  *entities; // [MAX_EDICTS]

    // local recorder
    qhandle_t       recording;
    int             numlevels; // stop after that many levels
    int             numframes; // stop after that many frames

    // TCP client pool
    gtv_client_t    *clients; // [sv_mvd_maxclients]
} mvd_server_t;

static mvd_server_t     mvd;

// TCP client lists
static LIST_DECL(gtv_client_list);
static LIST_DECL(gtv_active_list);

static LIST_DECL(gtv_white_list);
static LIST_DECL(gtv_black_list);

static cvar_t   *sv_mvd_enable;
static cvar_t   *sv_mvd_maxclients;
static cvar_t   *sv_mvd_bufsize;
static cvar_t   *sv_mvd_password;
static cvar_t   *sv_mvd_noblend;
static cvar_t   *sv_mvd_nogun;
static cvar_t   *sv_mvd_nomsgs;
static cvar_t   *sv_mvd_maxsize;
static cvar_t   *sv_mvd_maxtime;
static cvar_t   *sv_mvd_maxmaps;
static cvar_t   *sv_mvd_begincmd;
static cvar_t   *sv_mvd_scorecmd;
static cvar_t   *sv_mvd_autorecord;
static cvar_t   *sv_mvd_capture_flags;
static cvar_t   *sv_mvd_disconnect_time;
static cvar_t   *sv_mvd_suspend_time;
static cvar_t   *sv_mvd_allow_stufftext;
static cvar_t   *sv_mvd_spawn_dummy;

static bool     mvd_enable(void);
static void     mvd_disable(void);
static void     mvd_error(const char *reason);

static void     write_stream(gtv_client_t *client, void *data, size_t len);
static void     write_message(gtv_client_t *client, gtv_serverop_t op);
#if USE_ZLIB
static void     flush_stream(gtv_client_t *client, int flush);
#endif

static void     rec_stop(void);
static bool     rec_allowed(void);
static void     rec_start(qhandle_t demofile);
static void     rec_write(void);


/*
==============================================================================

DUMMY MVD CLIENT

MVD dummy is a fake client maintained entirely server side.
Representing MVD observers, this client is used to obtain base playerstate
for freefloat observers, receive scoreboard updates and text messages, etc.

==============================================================================
*/

static cmdbuf_t    dummy_buffer;
static char        dummy_buffer_text[MAX_STRING_CHARS];

static void dummy_wait_f(void)
{
    int count = Q_atoi(Cmd_Argv(1));
    dummy_buffer.waitCount += max(count, 1);
}

static void dummy_command(void)
{
    sv_client = mvd.dummy;
    sv_player = sv_client->edict;
    ge->ClientCommand(sv_player);
    sv_client = NULL;
    sv_player = NULL;
}

static void dummy_forward_f(void)
{
    Cmd_Shift();
    if (Cmd_Argc() > 0) {
        Com_DPrintf("dummy cmd: %s\n", Cmd_ArgsFrom(0));
        dummy_command();
    }
}

static void dummy_record_f(void)
{
    char buffer[MAX_OSPATH];
    qhandle_t f;

    if (!sv_mvd_autorecord->integer) {
        return;
    }

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <filename>\n", Cmd_Argv(0));
        return;
    }

    if (!rec_allowed()) {
        return;
    }

    f = FS_EasyOpenFile(buffer, sizeof(buffer), FS_MODE_WRITE,
                        "demos/", Cmd_Argv(1), ".mvd2");
    if (!f) {
        return;
    }

    if (!mvd_enable()) {
        FS_CloseFile(f);
        return;
    }

    Com_Printf("Auto-recording local MVD to %s\n", buffer);

    rec_start(f);
}

static void dummy_stop_f(void)
{
    if (!sv_mvd_autorecord->integer) {
        return;
    }

    if (!mvd.recording) {
        Com_Printf("Not recording a local MVD.\n");
        return;
    }

    Com_Printf("Stopped local MVD auto-recording.\n");
    rec_stop();
}

static const ucmd_t dummy_cmds[] = {
    { "cmd", dummy_forward_f },
    { "set", Cvar_Set_f },
    { "alias", Cmd_Alias_f },
    { "play", NULL },
    { "stopsound", NULL },
    { "exec", NULL },
    { "screenshot", NULL },
    { "wait", dummy_wait_f },
    { "record", dummy_record_f },
    { "stop", dummy_stop_f },
    { NULL, NULL }
};

static void dummy_exec_string(cmdbuf_t *buf, const char *line)
{
    char *cmd, *alias;
    const ucmd_t *u;
    cvar_t *v;

    if (!line[0]) {
        return;
    }

    Cmd_TokenizeString(line, true);

    cmd = Cmd_Argv(0);
    if (!cmd[0]) {
        return;
    }
    if ((u = Com_Find(dummy_cmds, cmd)) != NULL) {
        if (u->func) {
            u->func();
        }
        return;
    }

    alias = Cmd_AliasCommand(cmd);
    if (alias) {
        if (++dummy_buffer.aliasCount == ALIAS_LOOP_COUNT) {
            Com_WPrintf("%s: runaway alias loop\n", __func__);
            return;
        }
        Cbuf_InsertText(&dummy_buffer, alias);
        return;
    }

    v = Cvar_FindVar(cmd);
    if (v) {
        Cvar_Command(v);
        return;
    }

    Com_DPrintf("dummy forward: %s\n", line);
    dummy_command();
}

static void dummy_add_message(client_t *client, byte *data,
                              size_t length, bool reliable)
{
    char *text;

    if (!length || !reliable || data[0] != svc_stufftext) {
        return; // not interesting
    }

    if (sv_mvd_allow_stufftext->integer <= 0) {
        return; // not allowed
    }

    data[length] = 0;
    text = (char *)(data + 1);
    Com_DPrintf("dummy stufftext: %s\n", Com_MakePrintable(text));
    Cbuf_AddText(&dummy_buffer, text);
}

static void dummy_spawn(void)
{
    if (!mvd.dummy)
        return;

    sv_client = mvd.dummy;
    sv_player = sv_client->edict;
    ge->ClientBegin(sv_player);
    sv_client = NULL;
    sv_player = NULL;

    if (sv_mvd_begincmd->string[0]) {
        Cbuf_AddText(&dummy_buffer, sv_mvd_begincmd->string);
        Cbuf_AddText(&dummy_buffer, "\n");
    }

    mvd.layout_time = svs.realtime;

    mvd.dummy->state = cs_spawned;
}

static client_t *dummy_find_slot(void)
{
    client_t *c;
    int i, j;

    // first check if there is a free reserved slot
    j = sv_maxclients->integer - sv_reserved_slots->integer;
    for (i = j; i < sv_maxclients->integer; i++) {
        c = &svs.client_pool[i];
        if (!c->state) {
            return c;
        }
    }

    // then check regular slots
    for (i = 0; i < j; i++) {
        c = &svs.client_pool[i];
        if (!c->state) {
            return c;
        }
    }

    return NULL;
}

#define MVD_USERINFO1 \
    "\\name\\[MVDSPEC]\\skin\\male/grunt"

#define MVD_USERINFO2 \
    "\\mvdspec\\" STRINGIFY(PROTOCOL_VERSION_MVD_CURRENT) "\\ip\\loopback"

static int dummy_create(void)
{
    client_t *newcl;
    char userinfo[MAX_INFO_STRING * 2];
    const char *s;
    int allow;
    int number;

    // do nothing if already created
    if (mvd.dummy)
        return 0;

    if (sv_mvd_spawn_dummy->integer <= 0) {
        Com_DPrintf("Dummy MVD client disabled\n");
        return 0;
    }

    if (sv_mvd_spawn_dummy->integer == 1 && !(g_features->integer & GMF_MVDSPEC)) {
        Com_DPrintf("Dummy MVD client not supported by game\n");
        return 0;
    }

    // find a free client slot
    newcl = dummy_find_slot();
    if (!newcl) {
        Com_EPrintf("No slot for dummy MVD client\n");
        return -1;
    }

    memset(newcl, 0, sizeof(*newcl));
    number = newcl - svs.client_pool;
    newcl->number = newcl->slot = number;
    newcl->protocol = -1;
    newcl->state = cs_connected;
    newcl->AddMessage = dummy_add_message;
    newcl->edict = EDICT_NUM(number + 1);
    newcl->netchan.remote_address.type = NA_LOOPBACK;

    List_Init(&newcl->entry);

    if (g_features->integer & GMF_EXTRA_USERINFO) {
        strcpy(userinfo, MVD_USERINFO1);
        strcpy(userinfo + strlen(userinfo) + 1, MVD_USERINFO2);
    } else {
        strcpy(userinfo, MVD_USERINFO1);
        strcat(userinfo, MVD_USERINFO2);
        userinfo[strlen(userinfo) + 1] = 0;
    }

    mvd.dummy = newcl;

    // get the game a chance to reject this connection or modify the userinfo
    sv_client = newcl;
    sv_player = newcl->edict;
    allow = ge->ClientConnect(newcl->edict, userinfo);
    sv_client = NULL;
    sv_player = NULL;
    if (!allow) {
        s = Info_ValueForKey(userinfo, "rejmsg");
        if (!*s) {
            s = "Connection refused";
        }
        Com_EPrintf("Dummy MVD client rejected by game: %s\n", s);
        mvd.dummy = NULL;
        return -1;
    }

    // parse some info from the info strings
    Q_strlcpy(newcl->userinfo, userinfo, sizeof(newcl->userinfo));
    SV_UserinfoChanged(newcl);

    return 1;
}

static void dummy_run(void)
{
    usercmd_t cmd;

    if (!mvd.dummy)
        return;

    Cbuf_Execute(&dummy_buffer);
    Cbuf_Frame(&dummy_buffer);

    // run ClientThink to prevent timeouts, etc
    memset(&cmd, 0, sizeof(cmd));
    cmd.msec = BASE_FRAMETIME;
    sv_client = mvd.dummy;
    sv_player = sv_client->edict;
    ge->ClientThink(sv_player, &cmd);
    sv_client = NULL;
    sv_player = NULL;

    // check if the layout is constantly updated. if not,
    // game mod has probably closed the scoreboard, open it again
    if (mvd.active && sv_mvd_scorecmd->string[0]) {
        if (svs.realtime - mvd.layout_time > 9000) {
            Cbuf_AddText(&dummy_buffer, sv_mvd_scorecmd->string);
            Cbuf_AddText(&dummy_buffer, "\n");
            mvd.layout_time = svs.realtime;
        }
    }
}

/*
==============================================================================

FRAME UPDATES

As MVD stream operates over reliable transport, there is no concept of
"baselines" and delta compression is always performed from the last
state seen on the map. There is also no support for "nodelta" frames
(except the very first frame sent as part of the gamestate).

This allows building only one update per frame and multicasting it to
several destinations at once.

Additional bandwidth savings are performed by filtering out origin and
angles updates on player entities, as MVD client can easily recover them
from corresponding player states, assuming those are kept in sync by the
game mod. This assumption should be generally true for moving players,
as vanilla Q2 server performs PVS/PHS culling for them using origin from
entity states, but not player states.

==============================================================================
*/

/*
Attempts to determine if the given player entity is active,
and the given player state should be captured into MVD stream.

Entire function is a nasty hack. Ideally a compatible game DLL
should do it for us by providing some SVF_* flag or something.
*/
static bool player_is_active(const edict_t *ent)
{
    int num;

    if ((g_features->integer & GMF_PROPERINUSE) && !ent->inuse) {
        return false;
    }

    // not a client at all?
    if (!ent->client) {
        return false;
    }

    num = NUM_FOR_EDICT(ent) - 1;
    if (num < 0 || num >= sv_maxclients->integer) {
        return false;
    }

    // by default, check if client is actually connected
    // it may not be the case for bots!
    if (sv_mvd_capture_flags->integer & 1) {
        if (svs.client_pool[num].state != cs_spawned) {
            return false;
        }
    }

    // first of all, make sure player_state_t is valid
    if (!ent->client->ps.fov) {
        return false;
    }

    // always capture dummy MVD client
    if (mvd.dummy && ent == mvd.dummy->edict) {
        return true;
    }

    // never capture spectators
    if (ent->client->ps.pmove.pm_type == PM_SPECTATOR) {
        return false;
    }

    // check entity visibility
    if ((ent->svflags & SVF_NOCLIENT) || !HAS_EFFECTS(ent)) {
        // never capture invisible entities
        if (sv_mvd_capture_flags->integer & 2) {
            return false;
        }
    } else {
        // always capture visible entities (default)
        if (sv_mvd_capture_flags->integer & 4) {
            return true;
        }
    }

    // they are likely following someone in case of PM_FREEZE
    if (ent->client->ps.pmove.pm_type == PM_FREEZE) {
        return false;
    }

    // they are likely following someone if PMF_NO_PREDICTION is set
    if (ent->client->ps.pmove.pm_flags & PMF_NO_PREDICTION) {
        return false;
    }

    return true;
}

static bool entity_is_active(const edict_t *ent)
{
    if ((g_features->integer & GMF_PROPERINUSE) && !ent->inuse) {
        return false;
    }

    if (ent->svflags & SVF_NOCLIENT) {
        return false;
    }

    return HAS_EFFECTS(ent);
}

// Initializes MVD delta compressor for the first time on this map.
static void build_gamestate(void)
{
    edict_t *ent;
    int i;

    memset(mvd.players, 0, sizeof(mvd.players[0]) * sv_maxclients->integer);
    memset(mvd.entities, 0, sizeof(mvd.entities[0]) * svs.csr.max_edicts);

    // set base player states
    for (i = 0; i < sv_maxclients->integer; i++) {
        ent = EDICT_NUM(i + 1);

        if (!player_is_active(ent)) {
            continue;
        }

        MSG_PackPlayer(&mvd.players[i], &ent->client->ps);
        PPS_INUSE(&mvd.players[i]) = true;
    }

    // set base entity states
    for (i = 1; i < ge->num_edicts; i++) {
        ent = EDICT_NUM(i);

        if (!entity_is_active(ent)) {
            continue;
        }

        ent->s.number = i;
        MSG_PackEntity(&mvd.entities[i], &ent->s, ENT_EXTENSION(&svs.csr, ent));
    }
}

// Writes a single giant message with all the startup info,
// followed by an uncompressed (baseline) frame.
static void emit_gamestate(void)
{
    char        *string;
    int         i, j;
    player_packed_t *ps;
    entity_packed_t *es;
    size_t      length;
    int         flags, extra, portalbytes;
    byte        portalbits[MAX_MAP_PORTAL_BYTES];

    // don't bother writing if there are no active MVD clients
    if (!mvd.recording && LIST_EMPTY(&gtv_active_list)) {
        return;
    }

    // pack MVD stream flags into extra bits
    extra = 0;
    if (sv_mvd_nomsgs->integer && mvd.dummy) {
        extra |= MVF_NOMSGS << SVCMD_BITS;
    }
    if (svs.csr.extended) {
        extra |= MVF_EXTLIMITS << SVCMD_BITS;
    }

    // send the serverdata
    MSG_WriteByte(mvd_serverdata | extra);
    MSG_WriteLong(PROTOCOL_VERSION_MVD);
    if (svs.csr.extended)
        MSG_WriteShort(PROTOCOL_VERSION_MVD_CURRENT);
    else
        MSG_WriteShort(PROTOCOL_VERSION_MVD_DEFAULT);
    MSG_WriteLong(sv.spawncount);
    MSG_WriteString(fs_game->string);
    if (mvd.dummy)
        MSG_WriteShort(mvd.dummy->number);
    else
        MSG_WriteShort(-1);

    // send configstrings
    for (i = 0; i < svs.csr.end; i++) {
        string = sv.configstrings[i];
        if (!string[0]) {
            continue;
        }
        length = Q_strnlen(string, MAX_QPATH);
        MSG_WriteShort(i);
        MSG_WriteData(string, length);
        MSG_WriteByte(0);
    }
    MSG_WriteShort(i);

    // send baseline frame
    portalbytes = CM_WritePortalBits(&sv.cm, portalbits);
    MSG_WriteByte(portalbytes);
    MSG_WriteData(portalbits, portalbytes);

    // send player states
    for (i = 0, ps = mvd.players; i < sv_maxclients->integer; i++, ps++) {
        flags = mvd.psFlags;
        if (!PPS_INUSE(ps)) {
            flags |= MSG_PS_REMOVE;
        }
        MSG_WriteDeltaPlayerstate_Packet(NULL, ps, i, flags);
    }
    MSG_WriteByte(CLIENTNUM_NONE);

    // send entity states
    for (i = 1, es = mvd.entities + 1; i < ge->num_edicts; i++, es++) {
        flags = mvd.esFlags;
        if ((j = es->number) != 0) {
            if (i <= sv_maxclients->integer) {
                ps = &mvd.players[i - 1];
                if (PPS_INUSE(ps) && ps->pmove.pm_type == PM_NORMAL) {
                    flags |= MSG_ES_FIRSTPERSON;
                }
            }
        } else {
            flags |= MSG_ES_REMOVE;
        }
        es->number = i;
        MSG_WriteDeltaEntity(NULL, es, flags);
        es->number = j;
    }
    MSG_WriteShort(0);
}

static void copy_entity_state(entity_packed_t *dst, const entity_packed_t *src, int flags)
{
    if (!(flags & MSG_ES_FIRSTPERSON)) {
        VectorCopy(src->origin, dst->origin);
        VectorCopy(src->angles, dst->angles);
        VectorCopy(src->old_origin, dst->old_origin);
    }
    dst->modelindex = src->modelindex;
    dst->modelindex2 = src->modelindex2;
    dst->modelindex3 = src->modelindex3;
    dst->modelindex4 = src->modelindex4;
    dst->frame = src->frame;
    dst->skinnum = src->skinnum;
    dst->effects = src->effects;
    dst->renderfx = src->renderfx;
    dst->solid = src->solid;
    dst->sound = src->sound;
    dst->event = 0;
    if (svs.csr.extended) {
        dst->morefx = src->morefx;
        dst->alpha = src->alpha;
        dst->scale = src->scale;
        dst->loop_volume = src->loop_volume;
        dst->loop_attenuation = src->loop_attenuation;
    }
}

/*
Builds a new delta compressed MVD frame by capturing all entity and player
states and calculating portalbits. The same frame is used for all MVD clients,
as well as local recorder.
*/
static void emit_frame(void)
{
    player_packed_t *oldps, newps;
    entity_packed_t *oldes, newes;
    edict_t *ent;
    int flags, portalbytes;
    byte portalbits[MAX_MAP_PORTAL_BYTES];
    int i;

    MSG_WriteByte(mvd_frame);

    // send portal bits
    portalbytes = CM_WritePortalBits(&sv.cm, portalbits);
    MSG_WriteByte(portalbytes);
    MSG_WriteData(portalbits, portalbytes);

    // send player states
    for (i = 0; i < sv_maxclients->integer; i++) {
        oldps = &mvd.players[i];
        ent = EDICT_NUM(i + 1);

        if (!player_is_active(ent)) {
            if (PPS_INUSE(oldps)) {
                // the old player isn't present in the new message
                MSG_WriteDeltaPlayerstate_Packet(NULL, NULL, i, mvd.psFlags);
                PPS_INUSE(oldps) = false;
            }
            continue;
        }

        // quantize
        MSG_PackPlayer(&newps, &ent->client->ps);

        if (PPS_INUSE(oldps)) {
            // delta update from old position
            // because the force parm is false, this will not result
            // in any bytes being emited if the player has not changed at all
            MSG_WriteDeltaPlayerstate_Packet(oldps, &newps, i, mvd.psFlags);
        } else {
            // this is a new player, send it from the last state
            MSG_WriteDeltaPlayerstate_Packet(oldps, &newps, i,
                                             mvd.psFlags | MSG_PS_FORCE);
        }

        // shuffle current state to previous
        *oldps = newps;
        PPS_INUSE(oldps) = true;
    }

    MSG_WriteByte(CLIENTNUM_NONE);      // end of packetplayers

    // send entity states
    for (i = 1; i < ge->num_edicts; i++) {
        oldes = &mvd.entities[i];
        ent = EDICT_NUM(i);

        if (!entity_is_active(ent)) {
            if (oldes->number) {
                // the old entity isn't present in the new message
                MSG_WriteDeltaEntity(oldes, NULL, MSG_ES_FORCE);
                oldes->number = 0;
            }
            continue;
        }

        if (ent->s.number != i) {
            Com_WPrintf("%s: fixing ent->s.number: %d to %d\n",
                        __func__, ent->s.number, i);
            ent->s.number = i;
        }

        // calculate flags
        flags = mvd.esFlags;
        if (i <= sv_maxclients->integer) {
            oldps = &mvd.players[i - 1];
            if (PPS_INUSE(oldps) && oldps->pmove.pm_type == PM_NORMAL) {
                // do not waste bandwidth on origin/angle updates,
                // client will recover them from player state
                flags |= MSG_ES_FIRSTPERSON;
            }
        }

        if (!oldes->number) {
            // this is a new entity, send it from the last state
            flags |= MSG_ES_FORCE | MSG_ES_NEWENTITY;
        }

        // quantize
        MSG_PackEntity(&newes, &ent->s, ENT_EXTENSION(&svs.csr, ent));

        MSG_WriteDeltaEntity(oldes, &newes, flags);

        // shuffle current state to previous
        copy_entity_state(oldes, &newes, flags);
        oldes->number = i;
    }

    MSG_WriteShort(0);      // end of packetentities
}

static void suspend_streams(void)
{
    gtv_client_t *client;

    FOR_EACH_ACTIVE_GTV(client) {
        // send stream suspend marker
        write_message(client, GTS_STREAM_DATA);
#if USE_ZLIB
        flush_stream(client, Z_SYNC_FLUSH);
#endif
        NET_UpdateStream(&client->stream);
    }

    Com_DPrintf("Suspending MVD streams.\n");
    mvd.active = false;
}

static void resume_streams(void)
{
    gtv_client_t *client;

    // build and emit gamestate
    build_gamestate();
    emit_gamestate();

    FOR_EACH_ACTIVE_GTV(client) {
        // send gamestate
        write_message(client, GTS_STREAM_DATA);
#if USE_ZLIB
        flush_stream(client, Z_SYNC_FLUSH);
#endif
        NET_UpdateStream(&client->stream);
    }

    // write it to demofile
    if (mvd.recording) {
        rec_write();
    }

    // clear gamestate
    SZ_Clear(&msg_write);

    SZ_Clear(&mvd.datagram);
    SZ_Clear(&mvd.message);

    Com_DPrintf("Resuming MVD streams.\n");
    mvd.active = true;
}

static bool players_active(void)
{
    int i;
    edict_t *ent;

    for (i = 0; i < sv_maxclients->integer; i++) {
        ent = EDICT_NUM(i + 1);
        if (mvd.dummy && ent == mvd.dummy->edict)
            continue;
        if (player_is_active(ent))
            return true;
    }

    return false;
}

// disconnects MVD dummy if no MVD clients are active for some time
static void check_clients_activity(void)
{
    if (!sv_mvd_disconnect_time->integer || mvd.recording || !LIST_EMPTY(&gtv_active_list)) {
        mvd.clients_active = svs.realtime;
    } else if (svs.realtime - mvd.clients_active > sv_mvd_disconnect_time->integer) {
        mvd_disable();
    }
}

// suspends or resumes MVD streams depending on players activity
static void check_players_activity(void)
{
    if (!sv_mvd_suspend_time->integer || players_active()) {
        mvd.players_active = svs.realtime;
        if (!mvd.active) {
            resume_streams();
        }
    } else if (mvd.active) {
        if (svs.realtime - mvd.players_active > sv_mvd_suspend_time->integer) {
            suspend_streams();
        }
    }
}

static bool mvd_enable(void)
{
    int ret;

    if (!mvd.enabled)
        Com_DPrintf("Enabling server MVD recorder.\n");

    // create and spawn MVD dummy
    ret = dummy_create();
    if (ret < 0)
        return false;

    if (ret > 0)
        dummy_spawn();

    // we are enabled now
    mvd.enabled = true;

    // don't timeout
    mvd.clients_active = svs.realtime;

    // check for activation
    check_players_activity();

    return true;
}

static void mvd_disable(void)
{
    if (mvd.enabled)
        Com_DPrintf("Disabling server MVD recorder.\n");

    // drop (no-op if already dropped) and remove MVD dummy. NULL out pointer
    // before calling SV_DropClient to prevent spurious error message.
    if (mvd.dummy) {
        client_t *tmp = mvd.dummy;
        mvd.dummy = NULL;
        SV_DropClient(tmp, NULL);
        SV_RemoveClient(tmp);
    }

    SZ_Clear(&mvd.datagram);
    SZ_Clear(&mvd.message);

    mvd.enabled = false;
    mvd.active = false;
}

static void rec_frame(size_t total)
{
    uint16_t msglen;
    int ret;

    if (!total)
        return;

    msglen = LittleShort(total);
    ret = FS_Write(&msglen, 2, mvd.recording);
    if (ret != 2)
        goto fail;
    ret = FS_Write(mvd.message.data, mvd.message.cursize, mvd.recording);
    if (ret != mvd.message.cursize)
        goto fail;
    ret = FS_Write(msg_write.data, msg_write.cursize, mvd.recording);
    if (ret != msg_write.cursize)
        goto fail;
    ret = FS_Write(mvd.datagram.data, mvd.datagram.cursize, mvd.recording);
    if (ret != mvd.datagram.cursize)
        goto fail;

    if (sv_mvd_maxsize->integer > 0 && FS_Tell(mvd.recording) > sv_mvd_maxsize->integer) {
        Com_Printf("Stopping MVD recording, maximum size reached.\n");
        rec_stop();
        return;
    }

    if (sv_mvd_maxtime->integer > 0 && ++mvd.numframes > sv_mvd_maxtime->integer) {
        Com_Printf("Stopping MVD recording, maximum duration reached.\n");
        rec_stop();
        return;
    }

    return;

fail:
    Com_EPrintf("Couldn't write local MVD: %s\n", Q_ErrorString(ret));
    rec_stop();
}

/*
==================
SV_MvdBeginFrame
==================
*/
void SV_MvdBeginFrame(void)
{
    if (mvd.enabled)
        check_clients_activity();

    if (mvd.enabled)
        check_players_activity();
}

/*
==================
SV_MvdEndFrame
==================
*/
void SV_MvdEndFrame(void)
{
    gtv_client_t *client;
    size_t total;
    byte header[3];

    if (!SV_FRAMESYNC)
        return;

    // do nothing if not enabled
    if (!mvd.enabled) {
        return;
    }

    dummy_run();

    // do nothing if not active
    if (!mvd.active) {
        return;
    }

    // if reliable message overflowed, kick all clients
    if (mvd.message.overflowed) {
        mvd_error("reliable message overflowed");
        return;
    }

    if (mvd.datagram.overflowed) {
        Com_WPrintf("Unreliable MVD datagram overflowed.\n");
        SZ_Clear(&mvd.datagram);
    }

    // emit a delta update common to all clients
    emit_frame();

    // if reliable message and frame update don't fit, kick all clients
    if (mvd.message.cursize + msg_write.cursize >= MAX_MSGLEN) {
        SZ_Clear(&msg_write);
        mvd_error("frame overflowed");
        return;
    }

    // check if unreliable datagram fits
    if (mvd.message.cursize + msg_write.cursize + mvd.datagram.cursize >= MAX_MSGLEN) {
        Com_WPrintf("Dumping unreliable MVD datagram.\n");
        SZ_Clear(&mvd.datagram);
    }

    // build message header
    total = mvd.message.cursize + msg_write.cursize + mvd.datagram.cursize;
    WL16(header, total + 1);
    header[2] = GTS_STREAM_DATA;

    // send frame to clients
    FOR_EACH_ACTIVE_GTV(client) {
        write_stream(client, header, sizeof(header));
        write_stream(client, mvd.message.data, mvd.message.cursize);
        write_stream(client, msg_write.data, msg_write.cursize);
        write_stream(client, mvd.datagram.data, mvd.datagram.cursize);
#if USE_ZLIB
        if (++client->bufcount > client->maxbuf) {
            flush_stream(client, Z_SYNC_FLUSH);
        }
#endif
        NET_UpdateStream(&client->stream);
    }

    // write frame to demofile
    if (mvd.recording) {
        rec_frame(total);
    }

    // clear frame
    SZ_Clear(&msg_write);

    // clear datagrams
    SZ_Clear(&mvd.datagram);
    SZ_Clear(&mvd.message);
}



/*
==============================================================================

GAME API HOOKS

These hooks are called from PF_* functions to add additional
out-of-band data into the MVD stream.

==============================================================================
*/

/*
==============
SV_MvdMulticast
==============
*/
void SV_MvdMulticast(int leafnum, multicast_t to)
{
    mvd_ops_t   op;
    sizebuf_t   *buf;
    int         bits;

    // do nothing if not active
    if (!mvd.active) {
        return;
    }
    if (msg_write.cursize >= 2048) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }
    if (leafnum >= UINT16_MAX) {
        Com_WPrintf("%s: leafnum out of range\n", __func__);
        return;
    }

    op = mvd_multicast_all + to;
    buf = to < MULTICAST_ALL_R ? &mvd.datagram : &mvd.message;
    bits = (msg_write.cursize >> 8) & 7;

    SZ_WriteByte(buf, op | (bits << SVCMD_BITS));
    SZ_WriteByte(buf, msg_write.cursize & 255);

    if (op != mvd_multicast_all && op != mvd_multicast_all_r) {
        SZ_WriteShort(buf, leafnum);
    }

    SZ_Write(buf, msg_write.data, msg_write.cursize);
}

// Performs some basic filtering of the unicast data that would be
// otherwise discarded by the MVD client.
static bool filter_unicast_data(edict_t *ent)
{
    int cmd = msg_write.data[0];

    // discard any stufftexts, except of play sound hacks
    if (cmd == svc_stufftext) {
        return !memcmp(msg_write.data + 1, "play ", 5);
    }

    // if there is no dummy client, don't discard anything
    if (!mvd.dummy) {
        return true;
    }

    if (cmd == svc_layout) {
        if (ent != mvd.dummy->edict) {
            // discard any layout updates to players
            return false;
        }
        mvd.layout_time = svs.realtime;
    } else if (cmd == svc_print) {
        if (ent != mvd.dummy->edict && sv_mvd_nomsgs->integer) {
            // optionally discard text messages to players
            return false;
        }
    }

    return true;
}

/*
==============
SV_MvdUnicast
==============
*/
void SV_MvdUnicast(edict_t *ent, int clientNum, bool reliable)
{
    mvd_ops_t   op;
    sizebuf_t   *buf;
    int         bits;

    // do nothing if not active
    if (!mvd.active) {
        return;
    }

    // discard any data to players not in the game
    if (!player_is_active(ent)) {
        return;
    }

    if (!filter_unicast_data(ent)) {
        return;
    }

    if (msg_write.cursize >= 2048) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    // decide where should it go
    if (reliable) {
        op = mvd_unicast_r;
        buf = &mvd.message;
    } else {
        op = mvd_unicast;
        buf = &mvd.datagram;
    }

    // write it
    bits = (msg_write.cursize >> 8) & 7;
    SZ_WriteByte(buf, op | (bits << SVCMD_BITS));
    SZ_WriteByte(buf, msg_write.cursize & 255);
    SZ_WriteByte(buf, clientNum);
    SZ_Write(buf, msg_write.data, msg_write.cursize);
}

/*
==============
SV_MvdConfigstring
==============
*/
void SV_MvdConfigstring(int index, const char *string, size_t len)
{
    if (mvd.active) {
        SZ_WriteByte(&mvd.message, mvd_configstring);
        SZ_WriteShort(&mvd.message, index);
        SZ_Write(&mvd.message, string, len);
        SZ_WriteByte(&mvd.message, 0);
    }
}

/*
==============
SV_MvdBroadcastPrint
==============
*/
void SV_MvdBroadcastPrint(int level, const char *string)
{
    if (mvd.active) {
        SZ_WriteByte(&mvd.message, mvd_print);
        SZ_WriteByte(&mvd.message, level);
        SZ_WriteString(&mvd.message, string);
    }
}

/*
==============
SV_MvdStartSound

FIXME: origin will be incorrect on entities not captured this frame
==============
*/
void SV_MvdStartSound(int entnum, int channel, int flags,
                      int soundindex, int volume,
                      int attenuation, int timeofs)
{
    int extrabits, sendchan;

    // do nothing if not active
    if (!mvd.active) {
        return;
    }

    extrabits = 0;
    if (channel & CHAN_NO_PHS_ADD) {
        extrabits |= 1 << SVCMD_BITS;
    }
    if (channel & CHAN_RELIABLE) {
        // FIXME: write to mvd.message
        extrabits |= 2 << SVCMD_BITS;
    }

    SZ_WriteByte(&mvd.datagram, mvd_sound | extrabits);
    SZ_WriteByte(&mvd.datagram, flags);
    if (flags & SND_INDEX16)
        SZ_WriteShort(&mvd.datagram, soundindex);
    else
        SZ_WriteByte(&mvd.datagram, soundindex);

    if (flags & SND_VOLUME)
        SZ_WriteByte(&mvd.datagram, volume);
    if (flags & SND_ATTENUATION)
        SZ_WriteByte(&mvd.datagram, attenuation);
    if (flags & SND_OFFSET)
        SZ_WriteByte(&mvd.datagram, timeofs);

    sendchan = (entnum << 3) | (channel & 7);
    SZ_WriteShort(&mvd.datagram, sendchan);
}


/*
==============================================================================

TCP CLIENTS HANDLING

==============================================================================
*/


static void remove_client(gtv_client_t *client)
{
    NET_CloseStream(&client->stream);
    List_Remove(&client->entry);
    Z_Freep((void**)&client->data);
    client->state = cs_free;
}

#if USE_ZLIB
static void flush_stream(gtv_client_t *client, int flush)
{
    fifo_t *fifo = &client->stream.send;
    z_streamp z = &client->z;
    byte *data;
    size_t len;
    int ret;

    if (client->state <= cs_zombie) {
        return;
    }
    if (!z->state) {
        return;
    }

    z->next_in = NULL;
    z->avail_in = 0;

    do {
        data = FIFO_Reserve(fifo, &len);
        if (!len) {
            // FIXME: this is not an error when flushing
            return;
        }

        z->next_out = data;
        z->avail_out = (uInt)len;

        ret = deflate(z, flush);

        len -= z->avail_out;
        if (len) {
            FIFO_Commit(fifo, len);
            client->bufcount = 0;
        }
    } while (ret == Z_OK);
}
#endif

static void drop_client(gtv_client_t *client, const char *error)
{
    if (client->state <= cs_zombie) {
        return;
    }

    if (client->state >= cs_connected && error) {
        // notify console
        Com_Printf("TCP client %s[%s] dropped: %s\n", client->name,
                   NET_AdrToString(&client->stream.address), error);
    }

#if USE_ZLIB
    if (client->z.state) {
        // finish zlib stream
        flush_stream(client, Z_FINISH);
        deflateEnd(&client->z);
    }
#endif

    List_Remove(&client->active);
    client->state = cs_zombie;
    client->lastmessage = svs.realtime;
}


static void write_stream(gtv_client_t *client, void *data, size_t len)
{
    fifo_t *fifo = &client->stream.send;

    if (client->state <= cs_zombie) {
        return;
    }

    if (!len) {
        return;
    }

#if USE_ZLIB
    if (client->z.state) {
        z_streamp z = &client->z;

        z->next_in = data;
        z->avail_in = (uInt)len;

        do {
            data = FIFO_Reserve(fifo, &len);
            if (!len) {
                drop_client(client, "overflowed");
                return;
            }

            z->next_out = data;
            z->avail_out = (uInt)len;

            if (deflate(z, Z_NO_FLUSH) != Z_OK) {
                drop_client(client, "deflate() failed");
                return;
            }

            len -= z->avail_out;
            if (len) {
                FIFO_Commit(fifo, len);
                client->bufcount = 0;
            }
        } while (z->avail_in);
    } else
#endif

        if (FIFO_Write(fifo, data, len) != len) {
            drop_client(client, "overflowed");
        }
}

static void write_message(gtv_client_t *client, gtv_serverop_t op)
{
    byte header[3];

    WL16(header, msg_write.cursize + 1);
    header[2] = op;
    write_stream(client, header, sizeof(header));

    write_stream(client, msg_write.data, msg_write.cursize);
}

static bool auth_client(gtv_client_t *client, const char *password)
{
    if (SV_MatchAddress(&gtv_white_list, &client->stream.address))
        return true; // ALLOW whitelisted hosts without password

    if (SV_MatchAddress(&gtv_black_list, &client->stream.address))
        return false; // DENY blacklisted hosts

    if (*sv_mvd_password->string == 0)
        return true; // ALLOW neutral hosts if password IS NOT set

    // ALLOW neutral hosts if password matches, DENY otherwise
    return !strcmp(sv_mvd_password->string, password);
}

static void parse_hello(gtv_client_t *client)
{
    char password[MAX_QPATH];
    int protocol, flags;
    size_t size;
    byte *data;

    if (client->state >= cs_primed) {
        drop_client(client, "duplicated hello message");
        return;
    }

    // client should have already consumed the magic
    if (FIFO_Usage(&client->stream.send)) {
        drop_client(client, "send buffer not empty");
        return;
    }

    protocol = MSG_ReadWord();
    if (protocol != GTV_PROTOCOL_VERSION) {
        write_message(client, GTS_BADREQUEST);
        drop_client(client, "bad protocol version");
        return;
    }

    flags = MSG_ReadLong();
    MSG_ReadLong();
    MSG_ReadString(client->name, sizeof(client->name));
    MSG_ReadString(password, sizeof(password));
    MSG_ReadString(client->version, sizeof(client->version));

    // authorize access
    if (!auth_client(client, password)) {
        write_message(client, GTS_NOACCESS);
        drop_client(client, "not authorized");
        return;
    }

    if (sv_mvd_allow_stufftext->integer >= 0) {
        flags &= ~GTF_STRINGCMDS;
    }

#if !USE_ZLIB
    flags &= ~GTF_DEFLATE;
#endif

    Cvar_ClampInteger(sv_mvd_bufsize, 1, 4);

    // allocate larger send buffer
    size = MAX_GTS_MSGLEN * sv_mvd_bufsize->integer;
    data = SV_Malloc(size);
    client->stream.send.data = data;
    client->stream.send.size = size;
    client->data = data;
    client->flags = flags;
    client->state = cs_primed;

    // send hello
    MSG_WriteLong(flags);
    write_message(client, GTS_HELLO);
    SZ_Clear(&msg_write);

#if USE_ZLIB
    // the rest of the stream will be deflated
    if (flags & GTF_DEFLATE) {
        client->z.zalloc = SV_zalloc;
        client->z.zfree = SV_zfree;
        if (deflateInit(&client->z, Z_DEFAULT_COMPRESSION) != Z_OK) {
            drop_client(client, "deflateInit failed");
            return;
        }
    }
#endif

    Com_Printf("Accepted MVD client %s[%s]\n", client->name,
               NET_AdrToString(&client->stream.address));
}

static void parse_ping(gtv_client_t *client)
{
    if (client->state < cs_primed) {
        return;
    }

    // send ping reply
    write_message(client, GTS_PONG);

#if USE_ZLIB
    flush_stream(client, Z_SYNC_FLUSH);
#endif
}

static void parse_stream_start(gtv_client_t *client)
{
    int maxbuf;

    if (client->state != cs_primed) {
        drop_client(client, "unexpected stream start message");
        return;
    }

    if (!mvd_enable()) {
        write_message(client, GTS_ERROR);
        drop_client(client, "couldn't create MVD dummy");
        return;
    }

    maxbuf = MSG_ReadShort();
    client->maxbuf = max(maxbuf, 10);
    client->state = cs_spawned;

    List_Append(&gtv_active_list, &client->active);

    // send ack to client
    write_message(client, GTS_STREAM_START);

    // send gamestate if active
    if (mvd.active) {
        emit_gamestate();
        write_message(client, GTS_STREAM_DATA);
        SZ_Clear(&msg_write);
    } else {
        // send stream suspend marker
        write_message(client, GTS_STREAM_DATA);
    }

#if USE_ZLIB
    flush_stream(client, Z_SYNC_FLUSH);
#endif
}

static void parse_stream_stop(gtv_client_t *client)
{
    if (client->state != cs_spawned) {
        drop_client(client, "unexpected stream stop message");
        return;
    }

    client->state = cs_primed;

    List_Delete(&client->active);

    // send ack to client
    write_message(client, GTS_STREAM_STOP);
#if USE_ZLIB
    flush_stream(client, Z_SYNC_FLUSH);
#endif
}

static void parse_stringcmd(gtv_client_t *client)
{
    char string[MAX_GTC_MSGLEN];

    if (client->state < cs_primed) {
        drop_client(client, "unexpected stringcmd message");
        return;
    }

    if (!mvd.dummy || !(client->flags & GTF_STRINGCMDS)) {
        Com_DPrintf("ignored stringcmd from %s[%s]\n", client->name,
                    NET_AdrToString(&client->stream.address));
        return;
    }

    MSG_ReadString(string, sizeof(string));

    Cmd_TokenizeString(string, false);

    Com_DPrintf("dummy stringcmd from %s[%s]: %s\n", client->name,
                NET_AdrToString(&client->stream.address), Com_MakePrintable(string));
    dummy_command();
}

static bool parse_message(gtv_client_t *client)
{
    uint32_t magic;
    uint16_t msglen;
    int cmd;

    if (client->state <= cs_zombie) {
        return false;
    }

    // check magic
    if (client->state < cs_connected) {
        if (!FIFO_TryRead(&client->stream.recv, &magic, 4)) {
            return false;
        }
        if (magic != MVD_MAGIC) {
            drop_client(client, "not a MVD/GTV stream");
            return false;
        }
        client->state = cs_connected;

        // send it back
        write_stream(client, &magic, 4);
        return false;
    }

    // parse msglen
    if (!client->msglen) {
        if (!FIFO_TryRead(&client->stream.recv, &msglen, 2)) {
            return false;
        }
        msglen = LittleShort(msglen);
        if (!msglen) {
            drop_client(client, "end of stream");
            return false;
        }
        if (msglen > MAX_GTC_MSGLEN) {
            drop_client(client, "oversize message");
            return false;
        }
        client->msglen = msglen;
    }

    // read this message
    if (!FIFO_ReadMessage(&client->stream.recv, client->msglen)) {
        return false;
    }

    client->msglen = 0;

    cmd = MSG_ReadByte();
    switch (cmd) {
    case GTC_HELLO:
        parse_hello(client);
        break;
    case GTC_PING:
        parse_ping(client);
        break;
    case GTC_STREAM_START:
        parse_stream_start(client);
        break;
    case GTC_STREAM_STOP:
        parse_stream_stop(client);
        break;
    case GTC_STRINGCMD:
        parse_stringcmd(client);
        break;
    default:
        drop_client(client, "unknown command byte");
        return false;
    }

    if (msg_read.readcount > msg_read.cursize) {
        drop_client(client, "read past end of message");
        return false;
    }

    client->lastmessage = svs.realtime; // don't timeout
    return true;
}

static gtv_client_t *find_slot(void)
{
    gtv_client_t *client;
    int i;

    for (i = 0; i < sv_mvd_maxclients->integer; i++) {
        client = &mvd.clients[i];
        if (!client->state) {
            return client;
        }
    }

    return NULL;
}

static void accept_client(netstream_t *stream)
{
    gtv_client_t *client;
    netstream_t *s;

    // limit number of connections from single IPv4 address or /48 IPv6 network
    if (sv_iplimit->integer > 0) {
        int count = 0;

        FOR_EACH_GTV(client) {
            if (stream->address.type != client->stream.address.type)
                continue;
            if (stream->address.type == NA_IP && stream->address.ip.u32[0] != client->stream.address.ip.u32[0])
                continue;
            if (stream->address.type == NA_IP6 && memcmp(stream->address.ip.u8, client->stream.address.ip.u8, 48 / CHAR_BIT))
                continue;
            count++;
        }
        if (count >= sv_iplimit->integer) {
            Com_Printf("TCP client [%s] rejected: too many connections\n",
                       NET_AdrToString(&stream->address));
            NET_CloseStream(stream);
            return;
        }
    }

    // find a free client slot
    client = find_slot();
    if (!client) {
        Com_Printf("TCP client [%s] rejected: no free slots\n",
                   NET_AdrToString(&stream->address));
        NET_CloseStream(stream);
        return;
    }

    memset(client, 0, sizeof(*client));

    s = &client->stream;
    s->recv.data = client->buffer;
    s->recv.size = MAX_GTC_MSGLEN;
    s->send.data = client->buffer + MAX_GTC_MSGLEN;
    s->send.size = 4; // need no more than that initially
    s->socket = stream->socket;
    s->address = stream->address;
    s->state = stream->state;

    client->lastmessage = svs.realtime;
    client->state = cs_assigned;
    List_SeqAdd(&gtv_client_list, &client->entry);
    List_Init(&client->active);

    Com_DPrintf("TCP client [%s] accepted\n",
                NET_AdrToString(&stream->address));
}

void SV_MvdRunClients(void)
{
    gtv_client_t *client;
    neterr_t    ret;
    netstream_t stream;
    unsigned    delta;

    if (!mvd.clients) {
        return; // do nothing if disabled
    }

    // accept new connections
    ret = NET_Accept(&stream);
    if (ret == NET_ERROR) {
        Com_DPrintf("%s from %s, ignored\n", NET_ErrorString(),
                    NET_AdrToString(&net_from));
    } else if (ret == NET_OK) {
        accept_client(&stream);
    }

    // run existing connections
    FOR_EACH_GTV(client) {
        // check timeouts
        delta = svs.realtime - client->lastmessage;
        switch (client->state) {
        case cs_zombie:
            if (delta > sv_zombietime->integer || !FIFO_Usage(&client->stream.send)) {
                remove_client(client);
                continue;
            }
            break;
        case cs_assigned:
        case cs_connected:
            if (delta > sv_ghostime->integer || delta > sv_timeout->integer) {
                drop_client(client, "request timed out");
                remove_client(client);
                continue;
            }
            break;
        default:
            if (delta > sv_timeout->integer) {
                drop_client(client, "connection timed out");
                remove_client(client);
                continue;
            }
            break;
        }

        // run network stream
        ret = NET_RunStream(&client->stream);
        switch (ret) {
        case NET_AGAIN:
            break;
        case NET_OK:
            // parse the message
            while (parse_message(client))
                ;
            NET_UpdateStream(&client->stream);
            break;
        case NET_CLOSED:
            drop_client(client, "EOF from client");
            remove_client(client);
            break;
        case NET_ERROR:
            drop_client(client, "connection reset by peer");
            remove_client(client);
            break;
        }
    }
}

static void dump_clients(void)
{
    gtv_client_t    *client;
    int count;

    Com_Printf(
        "num name             buf lastmsg address               state\n"
        "--- ---------------- --- ------- --------------------- -----\n");
    count = 0;
    FOR_EACH_GTV(client) {
        Com_Printf("%3d %-16.16s %3zu %7u %-21s ",
                   count, client->name, FIFO_Usage(&client->stream.send),
                   svs.realtime - client->lastmessage,
                   NET_AdrToString(&client->stream.address));

        switch (client->state) {
        case cs_zombie:
            Com_Printf("ZMBI ");
            break;
        case cs_assigned:
            Com_Printf("ASGN ");
            break;
        case cs_connected:
            Com_Printf("CNCT ");
            break;
        case cs_primed:
            Com_Printf("PRIM ");
            break;
        default:
            Com_Printf("SEND ");
            break;
        }
        Com_Printf("\n");

        count++;
    }
}

static void dump_versions(void)
{
    gtv_client_t *client;
    int count;

    Com_Printf(
        "num name             version\n"
        "--- ---------------- -----------------------------------------\n");

    count = 0;
    FOR_EACH_GTV(client) {
        Com_Printf("%3i %-16.16s %-40.40s\n",
                   count, client->name, client->version);
        count++;
    }
}

void SV_MvdStatus_f(void)
{
    if (LIST_EMPTY(&gtv_client_list)) {
        Com_Printf("No TCP clients.\n");
    } else {
        if (Cmd_Argc() > 1) {
            dump_versions();
        } else {
            dump_clients();
        }
    }
    Com_Printf("\n");
}

static void mvd_drop(gtv_serverop_t op)
{
    gtv_client_t *client;

    // drop GTV clients
    FOR_EACH_GTV(client) {
        switch (client->state) {
        case cs_spawned:
        case cs_primed:
            write_message(client, op);
            drop_client(client, NULL);
            NET_UpdateStream(&client->stream);
            break;
        default:
            drop_client(client, NULL);
            remove_client(client);
            break;
        }
    }

    // update I/O status
    NET_Sleep(0);

    // push error message
    FOR_EACH_GTV(client) {
        NET_RunStream(&client->stream);
        NET_RunStream(&client->stream);
        remove_client(client);
    }

    List_Init(&gtv_client_list);
    List_Init(&gtv_active_list);
}

// something bad happened, remove all clients
static void mvd_error(const char *reason)
{
    Com_EPrintf("Fatal MVD error: %s\n", reason);

    // stop recording
    rec_stop();

    mvd_drop(GTS_ERROR);

    mvd_disable();
}

/*
==============================================================================

SERVER HOOKS

These hooks are called by server code when some event occurs.

==============================================================================
*/

/*
==================
SV_MvdMapChanged

Server has just changed the map, spawn the MVD dummy and go!
==================
*/
void SV_MvdMapChanged(void)
{
    gtv_client_t *client;
    int ret;

    if (!mvd.entities) {
        return; // do nothing if disabled
    }

    // spawn MVD dummy now if listening for autorecord command
    if (sv_mvd_autorecord->integer) {
        ret = dummy_create();
        if (ret < 0) {
            return;
        }
        if (ret > 0) {
            Com_DPrintf("Spawning MVD dummy for auto-recording\n");
            Cvar_Set("sv_mvd_suspend_time", "0");
        }
    }

    dummy_spawn();

    if (mvd.active) {
        // build and emit gamestate
        build_gamestate();
        emit_gamestate();

        // send gamestate to all MVD clients
        FOR_EACH_ACTIVE_GTV(client) {
            write_message(client, GTS_STREAM_DATA);
            NET_UpdateStream(&client->stream);
        }
    }

    if (mvd.recording) {
        int maxlevels = sv_mvd_maxmaps->integer;

        // check if it is time to stop recording
        if (maxlevels > 0 && ++mvd.numlevels >= maxlevels) {
            Com_Printf("Stopping MVD recording, "
                       "maximum number of level changes reached.\n");
            rec_stop();
        } else if (mvd.active) {
            // write gamestate to demofile
            rec_write();
        }
    }

    // clear gamestate
    SZ_Clear(&msg_write);

    SZ_Clear(&mvd.datagram);
    SZ_Clear(&mvd.message);
}

/*
==================
SV_MvdClientDropped

Server has just dropped a client. Drop all TCP clients if that was our MVD
dummy client.
==================
*/
void SV_MvdClientDropped(client_t *client)
{
    if (client == mvd.dummy) {
        mvd_error("dummy client was dropped");
    }
}

/*
==================
SV_MvdPreInit

Server is initializing, prepare MVD server for this game.
==================
*/
void SV_MvdPreInit(void)
{
    if (!sv_mvd_enable->integer) {
        return; // do nothing if disabled
    }

    // reserve CLIENTNUM_NONE slot
    Cvar_ClampInteger(sv_maxclients, 1, CLIENTNUM_NONE);

    // reserve the slot for dummy MVD client
    if (!sv_reserved_slots->integer) {
        Cvar_Set("sv_reserved_slots", "1");
    }

    Cvar_ClampInteger(sv_mvd_maxclients, 1, 256);

    // open server TCP socket
    if (sv_mvd_enable->integer > 1) {
        neterr_t ret = NET_Listen(true);
        if (ret == NET_OK) {
            mvd.clients = SV_Mallocz(sizeof(mvd.clients[0]) * sv_mvd_maxclients->integer);
        } else {
            if (ret == NET_ERROR)
                Com_EPrintf("Error opening server TCP port.\n");
            else
                Com_EPrintf("Server TCP port already in use.\n");
            Cvar_Set("sv_mvd_enable", "1");
        }
    }

    dummy_buffer.from = FROM_CONSOLE;
    dummy_buffer.text = dummy_buffer_text;
    dummy_buffer.maxsize = sizeof(dummy_buffer_text);
    dummy_buffer.exec = dummy_exec_string;
}

/*
==================
SV_MvdPostInit
==================
*/
void SV_MvdPostInit(void)
{
    if (!sv_mvd_enable->integer) {
        return; // do nothing if disabled
    }

    // allocate buffers
    SZ_Init(&mvd.message, SV_Malloc(MAX_MSGLEN), MAX_MSGLEN);
    SZ_Init(&mvd.datagram, SV_Malloc(MAX_MSGLEN), MAX_MSGLEN);
    mvd.players = SV_Malloc(sizeof(mvd.players[0]) * sv_maxclients->integer);
    mvd.entities = SV_Malloc(sizeof(mvd.entities[0]) * svs.csr.max_edicts);

    // setup protocol flags
    mvd.esFlags = MSG_ES_UMASK;
    mvd.psFlags = 0;

    if (sv_mvd_noblend->integer) {
        mvd.psFlags |= MSG_PS_IGNORE_BLEND;
    }
    if (sv_mvd_nogun->integer) {
        mvd.psFlags |= MSG_PS_IGNORE_GUNINDEX | MSG_PS_IGNORE_GUNFRAMES;
    }
    if (svs.csr.extended) {
        mvd.esFlags |= MSG_ES_EXTENSIONS;
        mvd.psFlags |= MSG_PS_EXTENSIONS;
    }
}

/*
==================
SV_MvdShutdown

Server is shutting down, clean everything up.
==================
*/
void SV_MvdShutdown(error_type_t type)
{
    // stop recording
    rec_stop();

    // remove MVD dummy
    if (mvd.dummy) {
        SV_RemoveClient(mvd.dummy);
        mvd.dummy = NULL;
    }

    memset(&dummy_buffer, 0, sizeof(dummy_buffer));

    // drop all clients
    mvd_drop(type == ERR_RECONNECT ? GTS_RECONNECT : GTS_DISCONNECT);

    // free static data
    Z_Free(mvd.message.data);
    Z_Free(mvd.datagram.data);
    Z_Free(mvd.players);
    Z_Free(mvd.entities);
    Z_Free(mvd.clients);

    // close server TCP socket
    NET_Listen(false);

    memset(&mvd, 0, sizeof(mvd));
}


/*
==============================================================================

LOCAL MVD RECORDER

==============================================================================
*/

static void rec_write(void)
{
    uint16_t msglen;
    int ret;

    if (!msg_write.cursize)
        return;

    msglen = LittleShort(msg_write.cursize);
    ret = FS_Write(&msglen, 2, mvd.recording);
    if (ret != 2)
        goto fail;
    ret = FS_Write(msg_write.data, msg_write.cursize, mvd.recording);
    if (ret == msg_write.cursize)
        return;

fail:
    Com_EPrintf("Couldn't write local MVD: %s\n", Q_ErrorString(ret));
    rec_stop();
}

// Stops server local MVD recording.
static void rec_stop(void)
{
    uint16_t msglen;

    if (!mvd.recording) {
        return;
    }

    // write demo EOF marker
    msglen = 0;
    FS_Write(&msglen, 2, mvd.recording);

    FS_CloseFile(mvd.recording);
    mvd.recording = 0;
}

static bool rec_allowed(void)
{
    if (!mvd.entities) {
        Com_Printf("MVD recording is disabled on this server.\n");
        return false;
    }

    if (mvd.recording) {
        Com_Printf("Already recording a local MVD.\n");
        return false;
    }

    return true;
}

static void rec_start(qhandle_t demofile)
{
    uint32_t magic;

    mvd.recording = demofile;
    mvd.numlevels = 0;
    mvd.numframes = 0;
    mvd.clients_active = svs.realtime;

    magic = MVD_MAGIC;
    FS_Write(&magic, 4, demofile);

    if (mvd.active) {
        emit_gamestate();
        rec_write();
        SZ_Clear(&msg_write);
    }
}

/*
==============
SV_MvdRecord_f

Begins server MVD recording.
Every entity, every playerinfo and every message will be recorded.
==============
*/
void SV_MvdRecord_f(void)
{
    char buffer[MAX_OSPATH];
    qhandle_t f;
    unsigned mode = FS_MODE_WRITE;
    int c;

    if (sv.state != ss_game) {
        Com_Printf("No server running.\n");
        return;
    }

    while ((c = Cmd_ParseOptions(o_record)) != -1) {
        switch (c) {
        case 'h':
            Cmd_PrintUsage(o_record, "<filename>");
            Com_Printf("Begin local MVD recording.\n");
            Cmd_PrintHelp(o_record);
            return;
        case 'z':
            mode |= FS_FLAG_GZIP;
            break;
        default:
            return;
        }
    }

    if (!cmd_optarg[0]) {
        Com_Printf("Missing filename argument.\n");
        Cmd_PrintHint();
        return;
    }

    if (!rec_allowed()) {
        return;
    }

    //
    // open the demo file
    //
    f = FS_EasyOpenFile(buffer, sizeof(buffer), mode,
                        "demos/", cmd_optarg, ".mvd2");
    if (!f) {
        return;
    }

    if (!mvd_enable()) {
        FS_CloseFile(f);
        return;
    }

    Com_Printf("Recording local MVD to %s\n", buffer);

    rec_start(f);
}


/*
==============
SV_MvdStop_f

Ends server MVD recording
==============
*/
void SV_MvdStop_f(void)
{
    if (!mvd.recording) {
        Com_Printf("Not recording a local MVD.\n");
        return;
    }

    Com_Printf("Stopped local MVD recording.\n");
    rec_stop();
}

/*
==============================================================================

CVARS AND COMMANDS

==============================================================================
*/

static void SV_MvdStuff_f(void)
{
    if (mvd.dummy) {
        Cbuf_AddText(&dummy_buffer, COM_StripQuotes(Cmd_RawArgs()));
        Cbuf_AddText(&dummy_buffer, "\n");
    } else {
        Com_Printf("Can't '%s', dummy MVD client is not active\n", Cmd_Argv(0));
    }
}

static void SV_AddGtvHost_f(void)
{
    SV_AddMatch_f(&gtv_white_list);
}
static void SV_DelGtvHost_f(void)
{
    SV_DelMatch_f(&gtv_white_list);
}
static void SV_ListGtvHosts_f(void)
{
    SV_ListMatches_f(&gtv_white_list);
}

static void SV_AddGtvBan_f(void)
{
    SV_AddMatch_f(&gtv_black_list);
}
static void SV_DelGtvBan_f(void)
{
    SV_DelMatch_f(&gtv_black_list);
}
static void SV_ListGtvBans_f(void)
{
    SV_ListMatches_f(&gtv_black_list);
}

static const cmdreg_t c_svmvd[] = {
    { "mvdstuff", SV_MvdStuff_f },
    { "addgtvhost", SV_AddGtvHost_f },
    { "delgtvhost", SV_DelGtvHost_f },
    { "listgtvhosts", SV_ListGtvHosts_f },
    { "addgtvban", SV_AddGtvBan_f },
    { "delgtvban", SV_DelGtvBan_f },
    { "listgtvbans", SV_ListGtvBans_f },

    { NULL }
};

static void sv_mvd_maxsize_changed(cvar_t *self)
{
    self->integer = 1000 * Cvar_ClampValue(self, 0, 2000000);
}

static void sv_mvd_maxtime_changed(cvar_t *self)
{
    self->integer = 60 * BASE_FRAMERATE * Cvar_ClampValue(self, 0, 24 * 24 * 60 * BASE_FRAMETIME);
}

void SV_MvdRegister(void)
{
    sv_mvd_enable = Cvar_Get("sv_mvd_enable", "0", CVAR_LATCH);
    sv_mvd_maxclients = Cvar_Get("sv_mvd_maxclients", "8", CVAR_LATCH);
    sv_mvd_bufsize = Cvar_Get("sv_mvd_bufsize", "2", CVAR_LATCH);
    sv_mvd_password = Cvar_Get("sv_mvd_password", "", CVAR_PRIVATE);
    sv_mvd_maxsize = Cvar_Get("sv_mvd_maxsize", "0", 0);
    sv_mvd_maxsize->changed = sv_mvd_maxsize_changed;
    sv_mvd_maxsize_changed(sv_mvd_maxsize);
    sv_mvd_maxtime = Cvar_Get("sv_mvd_maxtime", "0", 0);
    sv_mvd_maxtime->changed = sv_mvd_maxtime_changed;
    sv_mvd_maxtime_changed(sv_mvd_maxtime);
    sv_mvd_maxmaps = Cvar_Get("sv_mvd_maxmaps", "1", 0);
    sv_mvd_noblend = Cvar_Get("sv_mvd_noblend", "0", CVAR_LATCH);
    sv_mvd_nogun = Cvar_Get("sv_mvd_nogun", "0", CVAR_LATCH);
    sv_mvd_nomsgs = Cvar_Get("sv_mvd_nomsgs", "1", CVAR_LATCH);
    sv_mvd_begincmd = Cvar_Get("sv_mvd_begincmd",
                               "wait 50; putaway; wait 10; help;", 0);
    sv_mvd_scorecmd = Cvar_Get("sv_mvd_scorecmd",
                               "putaway; wait 10; help;", 0);
    sv_mvd_autorecord = Cvar_Get("sv_mvd_autorecord", "0", CVAR_LATCH);
    sv_mvd_capture_flags = Cvar_Get("sv_mvd_capture_flags", "5", 0);
    sv_mvd_disconnect_time = Cvar_Get("sv_mvd_disconnect_time", "15", 0);
    sv_mvd_disconnect_time->changed = sv_min_timeout_changed;
    sv_mvd_disconnect_time->changed(sv_mvd_disconnect_time);
    sv_mvd_suspend_time = Cvar_Get("sv_mvd_suspend_time", "5", 0);
    sv_mvd_suspend_time->changed = sv_min_timeout_changed;
    sv_mvd_suspend_time->changed(sv_mvd_suspend_time);
    sv_mvd_allow_stufftext = Cvar_Get("sv_mvd_allow_stufftext", "0", CVAR_LATCH);
    sv_mvd_spawn_dummy = Cvar_Get("sv_mvd_spawn_dummy", "1", 0);

    Cmd_Register(c_svmvd);
}

