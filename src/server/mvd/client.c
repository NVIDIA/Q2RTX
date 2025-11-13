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
// mvd_client.c -- MVD/GTV client
//

#include "client.h"
#include "server/mvd/protocol.h"

#define FOR_EACH_GTV(gtv) \
    LIST_FOR_EACH(gtv_t, gtv, &mvd_gtv_list, entry)

#define GTV_DEFAULT_BACKOFF (5 * 1000)          // 5 seconds
#define GTV_MAXIMUM_BACKOFF (5 * 3600 * 1000)   // 5 hours

#define GTV_PING_INTERVAL   (60 * 1000)     // 1 minute

typedef enum {
    GTV_DISCONNECTED, // disconnected
    GTV_CONNECTING, // connect() in progress
    GTV_PREPARING,  // waiting for server hello
    GTV_CONNECTED,  // keeping connection alive
    GTV_RESUMING,   // stream start request sent
    GTV_WAITING,    // server is suspended
    GTV_READING,    // server is resumed
    GTV_SUSPENDING, // stream stop request sent
    GTV_NUM_STATES
} gtv_state_t;

typedef struct gtv_s {
    list_t      entry;

    // common stuff
    int         id;
    char        name[MAX_MVD_NAME];
    gtv_state_t state;
    mvd_t       *mvd;
    void (*drop)(struct gtv_s *);
    void (*destroy)(struct gtv_s *);
    void (*run)(struct gtv_s *);

    // connection variables
    char        *username, *password;
    netstream_t stream;
    char        address[MAX_QPATH];
    byte        *data;
    size_t      msglen;
    unsigned    flags;
#if USE_ZLIB
    bool        z_act; // true when actively inflating
    z_stream    z_str;
    fifo_t      z_buf;
#endif
    unsigned    last_rcvd;
    unsigned    last_sent;
    unsigned    retry_time;
    unsigned    retry_backoff;

    // demo related variables
    qhandle_t       demoplayback;
    int             demoloop, demoskip;
    string_entry_t  *demohead, *demoentry;
    int64_t         demosize, demoofs;
    float           demoprogress;
    bool            demowait;
} gtv_t;

static const char *const gtv_states[GTV_NUM_STATES] = {
    "disconnected",
    "connecting",
    "preparing",
    "connected",
    "resuming",
    "waiting",
    "reading",
    "suspending"
};

static const char *const mvd_states[MVD_NUM_STATES] = {
    "DEAD", "WAIT", "READ"
};

LIST_DECL(mvd_gtv_list);
LIST_DECL(mvd_channel_list);

mvd_t       mvd_waitingRoom;
bool        mvd_dirty;
int         mvd_chanid;

bool        mvd_active;
unsigned    mvd_last_activity;

jmp_buf     mvd_jmpbuf;

#if USE_DEBUG
cvar_t      *mvd_shownet;
#endif

static cvar_t  *mvd_timeout;
static cvar_t  *mvd_suspend_time;
static cvar_t  *mvd_wait_delay;
static cvar_t  *mvd_wait_percent;
static cvar_t  *mvd_buffer_size;
static cvar_t  *mvd_username;
static cvar_t  *mvd_password;
static cvar_t  *mvd_snaps;

// ====================================================================

void MVD_StopRecord(mvd_t *mvd)
{
    uint16_t msglen;

    msglen = 0;
    FS_Write(&msglen, 2, mvd->demorecording);

    FS_CloseFile(mvd->demorecording);
    mvd->demorecording = 0;

    Z_Freep((void**)&mvd->demoname);
}

static void MVD_Free(mvd_t *mvd)
{
    int i;

    for (i = 0; i < mvd->numsnapshots; i++) {
        Z_Free(mvd->snapshots[i]);
    }
    Z_Free(mvd->snapshots);

    // stop demo recording
    if (mvd->demorecording) {
        MVD_StopRecord(mvd);
    }

    for (i = 0; i < mvd->maxclients; i++) {
        MVD_FreePlayer(&mvd->players[i]);
    }

    Z_Free(mvd->players);

    CM_FreeMap(&mvd->cm);

    Z_Free(mvd->delay.data);

    List_Remove(&mvd->entry);
    Z_Free(mvd);
}

static void MVD_Destroy(mvd_t *mvd)
{
    mvd_client_t *client, *next;

    // update channel menus
    if (!LIST_EMPTY(&mvd->entry)) {
        mvd_dirty = true;
    }

    // cause UDP clients to reconnect
    LIST_FOR_EACH_SAFE(mvd_client_t, client, next, &mvd->clients, entry) {
        MVD_SwitchChannel(client, &mvd_waitingRoom);
    }

    // destroy any existing GTV connection
    if (mvd->gtv) {
        mvd->gtv->mvd = NULL; // don't double destroy
        mvd->gtv->destroy(mvd->gtv);
    }

    // free all channel data
    MVD_Free(mvd);
}

void MVD_Destroyf(mvd_t *mvd, const char *fmt, ...)
{
    va_list     argptr;
    char        text[MAXERRORMSG];

    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    Com_Printf("[%s] =X= %s\n", mvd->name, text);

    // notify spectators
    if (COM_DEDICATED) {
        MVD_BroadcastPrintf(mvd, PRINT_HIGH, 0,
                            "[MVD] %s\n", text);
    }

    MVD_Destroy(mvd);

    longjmp(mvd_jmpbuf, -1);
}

#if USE_CLIENT
static mvd_t *find_local_channel(void)
{
    mvd_client_t *client;
    mvd_t *mvd;

    FOR_EACH_MVD(mvd) {
        FOR_EACH_MVDCL(client, mvd) {
            if (NET_IsLocalAddress(&client->cl->netchan.remote_address)) {
                return mvd;
            }
        }
    }
    return NULL;
}
#endif

mvd_t *MVD_SetChannel(int arg)
{
    char *s = Cmd_Argv(arg);
    mvd_t *mvd;
    int id;

    if (LIST_EMPTY(&mvd_channel_list)) {
        Com_Printf("No active channels.\n");
        return NULL;
    }

    if (!*s) {
        if (LIST_SINGLE(&mvd_channel_list)) {
            return LIST_FIRST(mvd_t, &mvd_channel_list, entry);
        }
        Com_Printf("Please specify an exact channel ID.\n");
        return NULL;
    }

#if USE_CLIENT
    // special value of @@ returns the channel local client is on
    if (!dedicated->integer && !strcmp(s, "@@")) {
        if ((mvd = find_local_channel()) != NULL) {
            return mvd;
        }
    } else
#endif
        if (COM_IsUint(s)) {
            id = Q_atoi(s);
            FOR_EACH_MVD(mvd) {
                if (mvd->id == id) {
                    return mvd;
                }
            }
        } else {
            FOR_EACH_MVD(mvd) {
                if (!strcmp(mvd->name, s)) {
                    return mvd;
                }
            }
        }

    Com_Printf("No such channel ID: %s\n", s);
    return NULL;
}

/*
====================================================================

COMMON GTV STUFF

====================================================================
*/

static void q_noreturn q_printf(2, 3)
gtv_dropf(gtv_t *gtv, const char *fmt, ...)
{
    va_list     argptr;
    char        text[MAXERRORMSG];

    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    Com_Printf("[%s] =!= %s\n", gtv->name, text);

    gtv->drop(gtv);

    longjmp(mvd_jmpbuf, -1);
}

static void q_noreturn q_printf(2, 3)
gtv_destroyf(gtv_t *gtv, const char *fmt, ...)
{
    va_list     argptr;
    char        text[MAXERRORMSG];

    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    Com_Printf("[%s] =X= %s\n", gtv->name, text);

    gtv->destroy(gtv);

    longjmp(mvd_jmpbuf, -1);
}

static mvd_t *create_channel(gtv_t *gtv)
{
    mvd_t *mvd;

    mvd = MVD_Mallocz(sizeof(*mvd));
    mvd->gtv = gtv;
    mvd->id = gtv->id;
    Q_strlcpy(mvd->name, gtv->name, sizeof(mvd->name));
    mvd->ge.edicts = mvd->edicts;
    mvd->ge.edict_size = sizeof(edict_t);
    mvd->ge.max_edicts = MAX_EDICTS;
    mvd->pm_type = PM_SPECTATOR;
    mvd->min_packets = mvd_wait_delay->integer;
    mvd->csr = &cs_remap_old;
    List_Init(&mvd->clients);
    List_Init(&mvd->entry);

    return mvd;
}

static gtv_t *gtv_set_conn(int arg)
{
    char *s = Cmd_Argv(arg);
    gtv_t *gtv;
    int id;

    if (LIST_EMPTY(&mvd_gtv_list)) {
        Com_Printf("No GTV connections.\n");
        return NULL;
    }

    if (!*s) {
        if (LIST_SINGLE(&mvd_gtv_list)) {
            return LIST_FIRST(gtv_t, &mvd_gtv_list, entry);
        }
        Com_Printf("Please specify an exact connection ID.\n");
        return NULL;
    }

    if (COM_IsUint(s)) {
        id = Q_atoi(s);
        FOR_EACH_GTV(gtv) {
            if (gtv->id == id) {
                return gtv;
            }
        }
    } else {
        FOR_EACH_GTV(gtv) {
            if (!strcmp(gtv->name, s)) {
                return gtv;
            }
        }
    }

    Com_Printf("No such connection ID: %s\n", s);
    return NULL;
}

static void set_mvd_active(void)
{
    // zero timeout = always active
    if (!mvd_suspend_time->integer)
        mvd_last_activity = svs.realtime;

    if (svs.realtime - mvd_last_activity > mvd_suspend_time->integer) {
        if (mvd_active) {
            Com_DPrintf("Suspending MVD streams.\n");
            mvd_active = false;
            mvd_dirty = true;
        }
    } else {
        if (!mvd_active) {
            Com_DPrintf("Resuming MVD streams.\n");
            mvd_active = true;
            mvd_dirty = true;
        }
    }
}

/*
==============
MVD_Frame

Called from main server loop.
==============
*/
int MVD_Frame(void)
{
    gtv_t *gtv, *next;
    int connections = 0;

    if (sv.state == ss_broadcast) {
        set_mvd_active();
    }

    // run all GTV connections (but not demos)
    LIST_FOR_EACH_SAFE(gtv_t, gtv, next, &mvd_gtv_list, entry) {
        if (setjmp(mvd_jmpbuf)) {
            SZ_Clear(&msg_write);
            continue;
        }

        gtv->run(gtv);

        connections++;
    }

    return connections;
}

#if USE_CLIENT
bool MVD_GetDemoStatus(float *progress, bool *paused, int *framenum)
{
    mvd_t *mvd;
    gtv_t *gtv;

    if ((mvd = find_local_channel()) == NULL)
        return false;

    if ((gtv = mvd->gtv) == NULL)
        return false;

    if (!gtv->demoplayback)
        return false;

    if (!gtv->demosize)
        return false;

    if (progress)
        *progress = gtv->demoprogress;
    if (paused)
        *paused = mvd->state == MVD_WAITING;
    if (framenum)
        *framenum = mvd->framenum;
    return true;
}
#endif

/*
====================================================================

DEMO PLAYER

====================================================================
*/

static void demo_play_next(gtv_t *gtv, string_entry_t *entry);

static void emit_base_frame(mvd_t *mvd);

static int demo_load_message(qhandle_t f)
{
    uint16_t us;
    int msglen, read;

    read = FS_Read(&us, 2, f);
    if (read != 2) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    if (!us) {
        return 0;
    }

    msglen = LittleShort(us);
    if (msglen > MAX_MSGLEN) {
        return Q_ERR_INVALID_FORMAT;
    }

    read = FS_Read(msg_read_buffer, msglen, f);
    if (read != msglen) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    return read;
}

static int demo_skip_map(qhandle_t f)
{
    int msglen;

    while (1) {
        if ((msglen = demo_load_message(f)) <= 0) {
            return msglen;
        }
        if (msg_read_buffer[0] == mvd_serverdata) {
            break;
        }
    }

    SZ_Init(&msg_read, msg_read_buffer, sizeof(msg_read_buffer));
    msg_read.cursize = msglen;

    return msglen;
}

static int demo_read_message(qhandle_t f)
{
    int msglen;

    if ((msglen = demo_load_message(f)) <= 0) {
        return msglen;
    }

    SZ_Init(&msg_read, msg_read_buffer, sizeof(msg_read_buffer));
    msg_read.cursize = msglen;

    return msglen;
}

static int demo_read_first(qhandle_t f)
{
    uint32_t magic;
    int read;

    // read magic
    read = FS_Read(&magic, 4, f);
    if (read != 4) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    if (magic != MVD_MAGIC) {
        return Q_ERR_UNKNOWN_FORMAT;
    }

    // read the first message
    read = demo_read_message(f);
    return read ? read : Q_ERR_UNEXPECTED_EOF;
}

#define MIN_SNAPSHOTS   64
#define MAX_SNAPSHOTS   250000000

// periodically builds a fake demo packet used to reconstruct delta compression
// state, configstrings and layouts at the given server frame.
static void demo_emit_snapshot(mvd_t *mvd)
{
    mvd_snap_t *snap;
    gtv_t *gtv;
    int64_t pos;
    char *from, *to;
    size_t len;
    int i, bits;

    if (mvd_snaps->integer <= 0)
        return;

    if (mvd->framenum < mvd->last_snapshot + mvd_snaps->integer * 10)
        return;

    if (mvd->numsnapshots >= MAX_SNAPSHOTS)
        return;

    gtv = mvd->gtv;
    if (!gtv)
        return;

    if (!gtv->demosize)
        return;

    pos = FS_Tell(gtv->demoplayback);
    if (pos < gtv->demoofs)
        return;

    // write baseline frame
    MSG_WriteByte(mvd_frame);
    emit_base_frame(mvd);

    // write configstrings
    for (i = 0; i < mvd->csr->end; i++) {
        from = mvd->baseconfigstrings[i];
        to = mvd->configstrings[i];

        if (!strcmp(from, to))
            continue;

        len = Q_strnlen(to, MAX_QPATH);
        MSG_WriteByte(mvd_configstring);
        MSG_WriteShort(i);
        MSG_WriteData(to, len);
        MSG_WriteByte(0);
    }

    // write private configstrings
    for (i = 0; i < mvd->maxclients; i++) {
        mvd_player_t *player = &mvd->players[i];
        mvd_cs_t *cs;

        if (!player->configstrings)
            continue;

        len = 0;
        for (cs = player->configstrings; cs; cs = cs->next)
            len += 4 + strlen(cs->string);

        bits = (len >> 8) & 7;
        MSG_WriteByte(mvd_unicast | (bits << SVCMD_BITS));
        MSG_WriteByte(len & 255);
        MSG_WriteByte(i);
        for (cs = player->configstrings; cs; cs = cs->next) {
            MSG_WriteByte(svc_configstring);
            MSG_WriteShort(cs->index);
            MSG_WriteString(cs->string);
        }
    }

    // write layout
    if (mvd->clientNum != -1) {
        len = 2 + strlen(mvd->layout);
        bits = (len >> 8) & 7;
        MSG_WriteByte(mvd_unicast | (bits << SVCMD_BITS));
        MSG_WriteByte(len & 255);
        MSG_WriteByte(mvd->clientNum);
        MSG_WriteByte(svc_layout);
        MSG_WriteString(mvd->layout);
    }

    snap = MVD_Malloc(sizeof(*snap) + msg_write.cursize - 1);
    snap->framenum = mvd->framenum;
    snap->filepos = pos;
    snap->msglen = msg_write.cursize;
    memcpy(snap->data, msg_write.data, msg_write.cursize);

    if (!mvd->snapshots)
        mvd->snapshots = MVD_Malloc(sizeof(snap) * MIN_SNAPSHOTS);
    else
        mvd->snapshots = Z_Realloc(mvd->snapshots, sizeof(snap) * ALIGN(mvd->numsnapshots + 1, MIN_SNAPSHOTS));
    mvd->snapshots[mvd->numsnapshots++] = snap;

    Com_DPrintf("[%d] snaplen %zu\n", mvd->framenum, msg_write.cursize);

    SZ_Clear(&msg_write);

    mvd->last_snapshot = mvd->framenum;
}

static mvd_snap_t *demo_find_snapshot(mvd_t *mvd, int64_t dest, bool byte_seek)
{
    int l = 0;
    int r = mvd->numsnapshots - 1;

    if (r < 0)
        return NULL;

    do {
        int m = (l + r) / 2;
        mvd_snap_t *snap = mvd->snapshots[m];
        int64_t pos = byte_seek ? snap->filepos : snap->framenum;
        if (pos < dest)
            l = m + 1;
        else if (pos > dest)
            r = m - 1;
        else
            return snap;
    } while (l <= r);

    return mvd->snapshots[max(r, 0)];
}

static void demo_update(gtv_t *gtv)
{
    if (gtv->demosize) {
        int64_t pos = FS_Tell(gtv->demoplayback);

        if (pos > gtv->demoofs)
            gtv->demoprogress = (float)(pos - gtv->demoofs) / gtv->demosize;
        else
            gtv->demoprogress = 0.0f;
    }
}

static void demo_finish(gtv_t *gtv, int ret)
{
    if (ret < 0) {
        gtv_destroyf(gtv, "Couldn't read %s: %s", gtv->demoentry->string, Q_ErrorString(ret));
    }

    demo_play_next(gtv, gtv->demoentry->next);
}

static bool demo_read_frame(mvd_t *mvd)
{
    gtv_t *gtv = mvd->gtv;
    int count;
    int ret;

    if (mvd->state == MVD_WAITING) {
        return false; // paused by user
    }

    if (!gtv) {
        MVD_Destroyf(mvd, "End of MVD stream reached");
    }

    if (gtv->demowait) {
        gtv->demowait = false;
        return false;
    }

    count = gtv->demoskip;
    gtv->demoskip = 0;

    if (count) {
        Com_Printf("[%s] -=- Skipping map%s...\n", gtv->name, count == 1 ? "" : "s");
        do {
            ret = demo_skip_map(gtv->demoplayback);
            if (ret <= 0) {
                goto next;
            }
        } while (--count);
    } else {
        ret = demo_read_message(gtv->demoplayback);
        if (ret <= 0) {
            goto next;
        }
    }

    demo_update(gtv);

    MVD_ParseMessage(mvd);
    demo_emit_snapshot(mvd);
    return true;

next:
    demo_finish(gtv, ret);
    return true;
}

static void demo_play_next(gtv_t *gtv, string_entry_t *entry)
{
    int64_t len, ofs;
    int ret;

    if (!entry) {
        if (gtv->demoloop) {
            if (--gtv->demoloop == 0) {
                gtv_destroyf(gtv, "End of play list reached");
            }
        }
        entry = gtv->demohead;
    }

    // close previous file
    if (gtv->demoplayback) {
        FS_CloseFile(gtv->demoplayback);
        gtv->demoplayback = 0;
    }

    // open new file
    len = FS_OpenFile(entry->string, &gtv->demoplayback, FS_MODE_READ | FS_FLAG_GZIP);
    if (!gtv->demoplayback) {
        gtv_destroyf(gtv, "Couldn't open %s: %s", entry->string, Q_ErrorString(len));
    }

    // read the first message
    ret = demo_read_first(gtv->demoplayback);
    if (ret < 0) {
        gtv_destroyf(gtv, "Couldn't read %s: %s", entry->string, Q_ErrorString(ret));
    }

    // create MVD channel
    if (!gtv->mvd) {
        gtv->mvd = create_channel(gtv);
        gtv->mvd->read_frame = demo_read_frame;
    } else {
        gtv->mvd->demoseeking = false;
    }

    Com_Printf("[%s] -=- Reading from %s\n", gtv->name, entry->string);

    // parse gamestate
    MVD_ParseMessage(gtv->mvd);
    if (!gtv->mvd->state) {
        gtv_destroyf(gtv, "First message of %s does not contain gamestate", entry->string);
    }

    gtv->mvd->state = MVD_READING;

    // reset state
    gtv->demoentry = entry;

    // set channel address
    Q_strlcpy(gtv->address, COM_SkipPath(entry->string), sizeof(gtv->address));

    ofs = FS_Tell(gtv->demoplayback);
    if (ofs > 0 && ofs < len) {
        gtv->demoofs = ofs;
        gtv->demosize = len - ofs;
    } else {
        gtv->demosize = gtv->demoofs = 0;
    }

    demo_emit_snapshot(gtv->mvd);
}

static void demo_free_playlist(gtv_t *gtv)
{
    string_entry_t *entry, *next;

    for (entry = gtv->demohead; entry; entry = next) {
        next = entry->next;
        Z_Free(entry);
    }

    gtv->demohead = gtv->demoentry = NULL;
}

static void demo_destroy(gtv_t *gtv)
{
    mvd_t *mvd = gtv->mvd;

    // destroy any associated MVD channel
    if (mvd) {
        mvd->gtv = NULL;
        MVD_Destroy(mvd);
    }

    if (gtv->demoplayback) {
        FS_CloseFile(gtv->demoplayback);
        gtv->demoplayback = 0;
    }

    demo_free_playlist(gtv);

    Z_Free(gtv);
}


/*
====================================================================

GTV CONNECTIONS

====================================================================
*/

static void write_stream(gtv_t *gtv, void *data, size_t len)
{
    if (FIFO_Write(&gtv->stream.send, data, len) != len) {
        gtv_destroyf(gtv, "Send buffer overflowed");
    }

    // don't timeout
    gtv->last_sent = svs.realtime;
}

static void write_message(gtv_t *gtv, gtv_clientop_t op)
{
    byte header[3];

    WL16(header, msg_write.cursize + 1);
    header[2] = op;
    write_stream(gtv, header, sizeof(header));

    write_stream(gtv, msg_write.data, msg_write.cursize);
}

static void q_noreturn gtv_oob_kill(mvd_t *mvd)
{
    gtv_t *gtv = mvd->gtv;

    if (gtv) {
        // don't kill connection!
        gtv->mvd = NULL;
        mvd->gtv = NULL;
    }

    MVD_Destroyf(mvd, "Ran out of buffers");
}

static bool gtv_wait_stop(mvd_t *mvd)
{
    gtv_t *gtv = mvd->gtv;
    int min_packets = mvd->min_packets, usage;

    // if not connected, flush any data left
    if (!gtv || gtv->state != GTV_READING) {
        if (!mvd->num_packets) {
            gtv_oob_kill(mvd);
        }
        min_packets = 1;
    }

    // see how many frames are buffered
    if (mvd->num_packets >= min_packets) {
        Com_Printf("[%s] -=- Waiting finished, reading...\n", mvd->name);
        goto stop;
    }

    // see how much data is buffered
    usage = FIFO_Percent(&mvd->delay);
    if (usage >= mvd_wait_percent->integer) {
        Com_Printf("[%s] -=- Buffering finished, reading...\n", mvd->name);
        goto stop;
    }

    return false;

stop:
    // notify spectators
    if (COM_DEDICATED) {
        MVD_BroadcastPrintf(mvd, PRINT_HIGH, 0,
                            "[MVD] Streaming resumed.\n");
    }
    mvd->state = MVD_READING;
    mvd->dirty = true;
    return true;
}

// ran out of buffers
static void gtv_wait_start(mvd_t *mvd)
{
    gtv_t *gtv = mvd->gtv;

    // if not connected, kill the channel
    if (!gtv) {
        MVD_Destroyf(mvd, "End of MVD stream reached");
    }

    // FIXME: if connection is suspended, kill the channel
    if (gtv->state != GTV_READING) {
        gtv_oob_kill(mvd);
    }

    Com_Printf("[%s] -=- Buffering data...\n", mvd->name);

    // oops, underflowed in the middle of the game,
    // resume as quickly as possible after there is some
    // data available again
    mvd->min_packets = 50 + 5 * mvd->underflows;
    mvd->min_packets = min(mvd->min_packets, mvd_wait_delay->integer);
    mvd->underflows++;
    mvd->state = MVD_WAITING;
    mvd->dirty = true;

    // notify spectators
    if (COM_DEDICATED) {
        MVD_BroadcastPrintf(mvd, PRINT_HIGH, 0,
                            "[MVD] Buffering data, please wait...\n");
    }

    // send ping to force server to flush
    write_message(gtv, GTC_PING);
    NET_UpdateStream(&gtv->stream);
}

static bool gtv_read_frame(mvd_t *mvd)
{
    uint16_t msglen;

    switch (mvd->state) {
    case MVD_WAITING:
        if (!gtv_wait_stop(mvd)) {
            return false;
        }
        break;
    case MVD_READING:
        if (!mvd->num_packets) {
            gtv_wait_start(mvd);
            return false;
        }
        break;
    default:
        MVD_Destroyf(mvd, "%s: bad mvd->state", __func__);
    }

    // NOTE: if we got here, delay buffer MUST contain
    // at least one complete, non-empty packet

    // parse msglen
    if (FIFO_Read(&mvd->delay, &msglen, 2) != 2) {
        MVD_Destroyf(mvd, "%s: partial data", __func__);
    }

    msglen = LittleShort(msglen);
    if (msglen < 1 || msglen > MAX_MSGLEN) {
        MVD_Destroyf(mvd, "%s: invalid msglen", __func__);
    }

    // read this message
    if (!FIFO_ReadMessage(&mvd->delay, msglen)) {
        MVD_Destroyf(mvd, "%s: partial data", __func__);
    }

    // decrement buffered packets counter
    mvd->num_packets--;

    // parse it
    MVD_ParseMessage(mvd);
    return true;
}

static bool gtv_forward_cmd(mvd_client_t *client)
{
    mvd_t *mvd = client->mvd;
    gtv_t *gtv = mvd->gtv;
    char *text;
    size_t len;

    if (!gtv || gtv->state < GTV_CONNECTED) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] Not connected to the game server.\n");
        return false;
    }
    if (!(gtv->flags & GTF_STRINGCMDS)) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] Game server does not allow command forwarding.\n");
        return false;
    }
    if (FIFO_Usage(&gtv->stream.send)) {
        SV_ClientPrintf(client->cl, PRINT_HIGH,
                        "[MVD] Send buffer not empty, please wait.\n");
        return false;
    }

    text = Cmd_Args();
    len = strlen(text);
    if (len > 150) {
        len = 150;
    }

    // send it
    MSG_WriteData(text, len);
    MSG_WriteByte(0);
    write_message(gtv, GTC_STRINGCMD);
    SZ_Clear(&msg_write);
    NET_UpdateStream(&gtv->stream);
    return true;
}

static void send_hello(gtv_t *gtv)
{
    int flags = GTF_STRINGCMDS;

#if USE_ZLIB
    flags |= GTF_DEFLATE;
#endif

    MSG_WriteShort(GTV_PROTOCOL_VERSION);
    MSG_WriteLong(flags);
    MSG_WriteLong(0);   // reserved
    MSG_WriteString(gtv->username ? gtv->username : mvd_username->string);
    MSG_WriteString(gtv->password ? gtv->password : mvd_password->string);
    MSG_WriteString(com_version->string);
    write_message(gtv, GTC_HELLO);
    SZ_Clear(&msg_write);

    Com_Printf("[%s] -=- Sending client hello...\n", gtv->name);

    gtv->state = GTV_PREPARING;
}

static void send_stream_start(gtv_t *gtv)
{
    int maxbuf;

    if (gtv->mvd) {
        maxbuf = gtv->mvd->min_packets;
    } else {
        maxbuf = mvd_wait_delay->integer;
    }
    maxbuf = max(maxbuf / 2, 10);

    // send stream start request
    MSG_WriteShort(maxbuf);
    write_message(gtv, GTC_STREAM_START);
    SZ_Clear(&msg_write);

    Com_Printf("[%s] -=- Sending stream start request...\n", gtv->name);

    gtv->state = GTV_RESUMING;
}

static void send_stream_stop(gtv_t *gtv)
{
    // send stream stop request
    write_message(gtv, GTC_STREAM_STOP);

    Com_Printf("[%s] -=- Sending stream stop request...\n", gtv->name);

    gtv->state = GTV_SUSPENDING;
}


#if USE_ZLIB
static voidpf gtv_zalloc(voidpf opaque, uInt items, uInt size)
{
    return MVD_Malloc(items * size);
}

static void gtv_zfree(voidpf opaque, voidpf address)
{
    Z_Free(address);
}
#endif

static void parse_hello(gtv_t *gtv)
{
    int flags;

    if (gtv->state >= GTV_CONNECTED) {
        gtv_destroyf(gtv, "Duplicated server hello");
    }

    flags = MSG_ReadLong();

    if (flags & GTF_DEFLATE) {
#if USE_ZLIB
        if (!gtv->z_str.state) {
            gtv->z_str.zalloc = gtv_zalloc;
            gtv->z_str.zfree = gtv_zfree;
            if (inflateInit(&gtv->z_str) != Z_OK) {
                gtv_destroyf(gtv, "inflateInit() failed: %s",
                             gtv->z_str.msg);
            }
        }
        if (!gtv->z_buf.data) {
            gtv->z_buf.data = MVD_Malloc(MAX_GTS_MSGLEN);
            gtv->z_buf.size = MAX_GTS_MSGLEN;
        }
        gtv->z_act = true; // remaining data is deflated
#else
        gtv_destroyf(gtv, "Server sending deflated data");
#endif
    }

    Com_Printf("[%s] -=- Server hello done.\n", gtv->name);

    if (sv.state != ss_broadcast) {
        // the game is just starting
        SV_InitGame(MVD_SPAWN_INTERNAL);
        MVD_Spawn();
    } else {
        // notify spectators
        if (COM_DEDICATED && gtv->mvd) {
            MVD_BroadcastPrintf(gtv->mvd, PRINT_HIGH, 0,
                                "[MVD] Restored connection to the game server!\n");
        }
    }

    gtv->flags = flags;
    gtv->state = GTV_CONNECTED;
}

static void parse_stream_start(gtv_t *gtv)
{
    if (gtv->state != GTV_RESUMING) {
        gtv_destroyf(gtv, "Unexpected stream start ack in state %u", gtv->state);
    }

    Com_Printf("[%s] -=- Stream start ack received.\n", gtv->name);

    gtv->state = GTV_READING;
}

static void parse_stream_stop(gtv_t *gtv)
{
    if (gtv->state != GTV_SUSPENDING) {
        gtv_destroyf(gtv, "Unexpected stream stop ack in state %u", gtv->state);
    }

    Com_Printf("[%s] -=- Stream stop ack received.\n", gtv->name);

    gtv->state = GTV_CONNECTED;
}

static void parse_stream_data(gtv_t *gtv)
{
    mvd_t *mvd = gtv->mvd;
    size_t size;

    if (gtv->state < GTV_WAITING) {
        gtv_destroyf(gtv, "Unexpected stream data packet");
    }

    // ignore any pending data while suspending
    if (gtv->state == GTV_SUSPENDING) {
        msg_read.readcount = msg_read.cursize;
        return;
    }

    // empty data part acts as stream suspend marker
    if (msg_read.readcount == msg_read.cursize) {
        if (gtv->state == GTV_READING) {
            Com_Printf("[%s] -=- Stream suspended by server.\n", gtv->name);
            gtv->state = GTV_WAITING;
        }
        return;
    }

    // non-empty data part acts as stream resume marker
    if (gtv->state == GTV_WAITING) {
        Com_Printf("[%s] -=- Stream resumed by server.\n", gtv->name);
        gtv->state = GTV_READING;
    }

    // create the channel, if not yet created
    if (!mvd) {
        mvd = create_channel(gtv);

        Cvar_ClampInteger(mvd_buffer_size, 2, 256);

        // allocate delay buffer
        size = mvd_buffer_size->integer * MAX_MSGLEN;
        mvd->delay.data = MVD_Malloc(size);
        mvd->delay.size = size;
        mvd->read_frame = gtv_read_frame;
        mvd->forward_cmd = gtv_forward_cmd;

        gtv->mvd = mvd;
    }

    if (!mvd->state) {
        // parse it in place until we get a gamestate
        MVD_ParseMessage(mvd);
    } else {
        byte *data = msg_read.data + 1;
        size_t len = msg_read.cursize - 1;
        uint16_t msglen;

        // see if this packet fits
        if (FIFO_Write(&mvd->delay, NULL, len + 2) != len + 2) {
            if (mvd->state == MVD_WAITING) {
                // if delay buffer overflowed in waiting state,
                // something is seriously wrong, disconnect for safety
                gtv_destroyf(gtv, "Delay buffer overflowed in waiting state");
            }

            // oops, overflowed
            Com_Printf("[%s] =!= Delay buffer overflowed!\n", gtv->name);

            if (COM_DEDICATED) {
                // notify spectators
                MVD_BroadcastPrintf(mvd, PRINT_HIGH, 0,
                                    "[MVD] Delay buffer overflowed!\n");
            }

            // clear entire delay buffer
            // minimize the delay
            FIFO_Clear(&mvd->delay);
            mvd->state = MVD_WAITING;
            mvd->num_packets = 0;
            mvd->min_packets = 50;
            mvd->overflows++;

            // send stream stop request
            write_message(gtv, GTC_STREAM_STOP);
            gtv->state = GTV_SUSPENDING;
            return;
        }

        // write it into delay buffer
        msglen = LittleShort(len);
        FIFO_Write(&mvd->delay, &msglen, 2);
        FIFO_Write(&mvd->delay, data, len);

        // increment buffered packets counter
        mvd->num_packets++;

        msg_read.readcount = msg_read.cursize;
    }
}

static bool parse_message(gtv_t *gtv, fifo_t *fifo)
{
    uint32_t magic;
    uint16_t msglen;
    int cmd;

    // check magic
    if (gtv->state < GTV_PREPARING) {
        if (!FIFO_TryRead(fifo, &magic, 4)) {
            return false;
        }
        if (magic != MVD_MAGIC) {
            gtv_destroyf(gtv, "Not a MVD/GTV stream");
        }

        // send client hello
        send_hello(gtv);
    }

    // parse msglen
    if (!gtv->msglen) {
        if (!FIFO_TryRead(fifo, &msglen, 2)) {
            return false;
        }
        msglen = LittleShort(msglen);
        if (!msglen) {
            gtv_dropf(gtv, "End of MVD/GTV stream");
        }
        if (msglen > MAX_MSGLEN) {
            gtv_destroyf(gtv, "Oversize message");
        }
        gtv->msglen = msglen;
    }

    // read this message
    if (!FIFO_ReadMessage(fifo, gtv->msglen)) {
        return false;
    }

    gtv->msglen = 0;

    cmd = MSG_ReadByte();

    switch (cmd) {
    case GTS_HELLO:
        parse_hello(gtv);
        break;
    case GTS_PONG:
        break;
    case GTS_STREAM_START:
        parse_stream_start(gtv);
        break;
    case GTS_STREAM_STOP:
        parse_stream_stop(gtv);
        break;
    case GTS_STREAM_DATA:
        parse_stream_data(gtv);
        break;
    case GTS_ERROR:
        gtv_destroyf(gtv, "Server side error occured.");
        break;
    case GTS_BADREQUEST:
        gtv_destroyf(gtv, "Server refused to process our request.");
        break;
    case GTS_NOACCESS:
        gtv_destroyf(gtv,
                     "You don't have permission to access "
                     "MVD/GTV stream on this server.");
        break;
    case GTS_DISCONNECT:
        gtv_destroyf(gtv, "Server has been shut down.");
        break;
    case GTS_RECONNECT:
        gtv_dropf(gtv, "Server has been restarted.");
        break;
    default:
        gtv_destroyf(gtv, "Unknown command byte");
    }

    if (msg_read.readcount > msg_read.cursize) {
        gtv_destroyf(gtv, "Read past end of message");
    }

    gtv->last_rcvd = svs.realtime; // don't timeout
    return true;
}

#if USE_ZLIB
static int inflate_stream(fifo_t *dst, fifo_t *src, z_streamp z)
{
    byte    *data;
    size_t  avail_in, avail_out;
    int     ret = Z_BUF_ERROR;

    do {
        data = FIFO_Peek(src, &avail_in);
        if (!avail_in) {
            break;
        }
        z->next_in = data;
        z->avail_in = (uInt)avail_in;

        data = FIFO_Reserve(dst, &avail_out);
        if (!avail_out) {
            break;
        }
        z->next_out = data;
        z->avail_out = (uInt)avail_out;

        ret = inflate(z, Z_SYNC_FLUSH);

        FIFO_Decommit(src, avail_in - z->avail_in);
        FIFO_Commit(dst, avail_out - z->avail_out);
    } while (ret == Z_OK);

    return ret;
}

static void inflate_more(gtv_t *gtv)
{
    int ret = inflate_stream(&gtv->z_buf, &gtv->stream.recv, &gtv->z_str);

    switch (ret) {
    case Z_BUF_ERROR:
    case Z_OK:
        break;
    case Z_STREAM_END:
        inflateReset(&gtv->z_str);
        gtv->z_act = false;
        break;
    default:
        gtv_destroyf(gtv, "inflate() failed with error %d", ret);
    }
}
#endif

static neterr_t run_connect(gtv_t *gtv)
{
    neterr_t ret;
    uint32_t magic;

    // run connection
    if ((ret = NET_RunConnect(&gtv->stream)) != NET_OK) {
        return ret;
    }

    Com_Printf("[%s] -=- Connected to the game server!\n", gtv->name);

    // allocate buffers
    if (!gtv->data) {
        gtv->data = MVD_Malloc(MAX_GTS_MSGLEN + MAX_GTC_MSGLEN);
    }
    gtv->stream.recv.data = gtv->data;
    gtv->stream.recv.size = MAX_GTS_MSGLEN;
    gtv->stream.send.data = gtv->data + MAX_GTS_MSGLEN;
    gtv->stream.send.size = MAX_GTC_MSGLEN;

    // don't timeout
    gtv->last_rcvd = svs.realtime;

    // send magic
    magic = MVD_MAGIC;
    write_stream(gtv, &magic, 4);

    return NET_OK;
}

static neterr_t run_stream(gtv_t *gtv)
{
    neterr_t ret;
#if USE_DEBUG
    int count;
    size_t usage;
#endif

    // run network stream
    if ((ret = NET_RunStream(&gtv->stream)) != NET_OK) {
        return ret;
    }

#if USE_DEBUG
    count = 0;
    usage = FIFO_Usage(&gtv->stream.recv);
#endif

#if USE_ZLIB
    if (gtv->z_act) {
        while (1) {
            // decompress more data
            if (gtv->z_act) {
                inflate_more(gtv);
            }
            if (!parse_message(gtv, &gtv->z_buf)) {
                break;
            }
#if USE_DEBUG
            count++;
#endif
        }
    } else
#endif
        while (parse_message(gtv, &gtv->stream.recv)) {
#if USE_DEBUG
            count++;
#endif
        }

#if USE_DEBUG
    if (mvd_shownet->integer == -1) {
        size_t total = usage - FIFO_Usage(&gtv->stream.recv);

        Com_Printf("[%s] %zu bytes, %d msgs\n",
                   gtv->name, total, count);
    }
#endif

    return NET_OK;
}

static void check_timeouts(gtv_t *gtv)
{
    // drop if no data has been received for too long
    if (svs.realtime - gtv->last_rcvd > mvd_timeout->integer) {
        gtv_dropf(gtv, "Server connection timed out.");
    }

    if (gtv->state < GTV_CONNECTED) {
        return;
    }

    // stop/start stream depending on global state
    if (mvd_active) {
        if (gtv->state == GTV_CONNECTED) {
            send_stream_start(gtv);
        }
    } else if (gtv->state == GTV_WAITING || gtv->state == GTV_READING) {
        send_stream_stop(gtv);
    }

    // ping if no data has been sent for too long
    if (svs.realtime - gtv->last_sent > GTV_PING_INTERVAL) {
        write_message(gtv, GTC_PING);
    }
}

static bool check_reconnect(gtv_t *gtv)
{
    netadr_t adr;

    if (svs.realtime - gtv->retry_time < gtv->retry_backoff) {
        return false;
    }

    Com_Printf("[%s] -=- Attempting to reconnect to %s...\n",
               gtv->name, gtv->address);

    gtv->state = GTV_CONNECTING;

    // don't timeout
    gtv->last_sent = gtv->last_rcvd = svs.realtime;

    if (!NET_StringToAdr(gtv->address, &adr, PORT_SERVER)) {
        gtv_dropf(gtv, "Unable to lookup %s", gtv->address);
    }

    if (NET_Connect(&adr, &gtv->stream)) {
        gtv_dropf(gtv, "Unable to connect to %s",
                  NET_AdrToString(&adr));
    }

    return true;
}

static void gtv_run(gtv_t *gtv)
{
    neterr_t ret = NET_AGAIN;

    // check if it is time to reconnect
    if (!gtv->state) {
        if (!check_reconnect(gtv)) {
            return;
        }
    }

    // run network stream
    switch (gtv->stream.state) {
    case NS_CONNECTING:
        ret = run_connect(gtv);
        if (ret == NET_AGAIN) {
            return;
        }
        if (ret == NET_OK) {
    case NS_CONNECTED:
            ret = run_stream(gtv);
        }
        break;
    default:
        return;
    }

    switch (ret) {
    case NET_AGAIN:
    case NET_OK:
        check_timeouts(gtv);
        NET_UpdateStream(&gtv->stream);
        break;
    case NET_ERROR:
        gtv_dropf(gtv, "%s to %s", NET_ErrorString(),
                  NET_AdrToString(&gtv->stream.address));
        break;
    case NET_CLOSED:
        gtv_dropf(gtv, "Server has closed connection.");
        break;
    }
}

static void gtv_destroy(gtv_t *gtv)
{
    mvd_t *mvd = gtv->mvd;

    // drop any associated MVD channel
    if (mvd) {
        mvd->gtv = NULL; // don't double destroy
        if (!mvd->state) {
            // free it here, since it is not yet
            // added to global channel list
            MVD_Free(mvd);
        } else if (COM_DEDICATED) {
            // notify spectators
            MVD_BroadcastPrintf(mvd, PRINT_HIGH, 0,
                                "[MVD] Disconnected from the game server!\n");
        }
    }

    // make sure network connection is closed
    NET_CloseStream(&gtv->stream);

    // unlink from the list of connections
    List_Remove(&gtv->entry);

    // free all memory buffers
    Z_Free(gtv->username);
    Z_Free(gtv->password);
#if USE_ZLIB
    inflateEnd(&gtv->z_str);
    Z_Free(gtv->z_buf.data);
#endif
    Z_Free(gtv->data);
    Z_Free(gtv);
}

static void gtv_drop(gtv_t *gtv)
{
    time_t sec;
    char buffer[MAX_QPATH];

    if (gtv->stream.state < NS_CONNECTED) {
        gtv->retry_backoff += 15 * 1000;
    } else {
        // notify spectators
        if (COM_DEDICATED && gtv->mvd) {
            MVD_BroadcastPrintf(gtv->mvd, PRINT_HIGH, 0,
                                "[MVD] Lost connection to the game server!\n");
        }

        if (gtv->state >= GTV_CONNECTED) {
            gtv->retry_backoff = GTV_DEFAULT_BACKOFF;
        } else {
            gtv->retry_backoff += 30 * 1000;
        }
    }

    if (gtv->retry_backoff > GTV_MAXIMUM_BACKOFF) {
        gtv->retry_backoff = GTV_MAXIMUM_BACKOFF;
    }

    sec = gtv->retry_backoff / 1000;
    Com_FormatTimeLong(buffer, sizeof(buffer), sec);
    Com_Printf("[%s] -=- Reconnecting in %s.\n", gtv->name, buffer);

    NET_CloseStream(&gtv->stream);
#if USE_ZLIB
    inflateReset(&gtv->z_str);
    FIFO_Clear(&gtv->z_buf);
    gtv->z_act = false;
#endif
    gtv->msglen = 0;
    gtv->state = GTV_DISCONNECTED;
    gtv->retry_time = svs.realtime;
}


/*
====================================================================

OPERATOR COMMANDS

====================================================================
*/

void MVD_Spawn(void)
{
    Cvar_SetInteger(sv_running, ss_broadcast, FROM_CODE);
    Cvar_Set("sv_paused", "0");
    Cvar_Set("timedemo", "0");
    SV_InfoSet("port", net_port->string);

#if USE_SYSCON
    SV_SetConsoleTitle();
#endif

    // generate spawncount for Waiting Room
    sv.spawncount = Q_rand() & 0x7fffffff;

#if USE_FPS
    // just fixed base FPS
    sv.framerate = BASE_FRAMERATE;
    sv.frametime = BASE_FRAMETIME;
    sv.framediv = 1;
#endif

    // set externally visible server name
    Q_strlcpy(sv.name, mvd_waitingRoom.mapname, sizeof(sv.name));

    sv.state = ss_broadcast;

    // start as inactive
    mvd_last_activity = INT_MIN;
    set_mvd_active();
}

static void MVD_Spawn_f(void)
{
    SV_InitGame(MVD_SPAWN_ENABLED);
    MVD_Spawn();
}

static void list_generic(void)
{
    mvd_t *mvd;

    Com_Printf(
        "id name         map      spc plr stat buf pckt address       \n"
        "-- ------------ -------- --- --- ---- --- ---- --------------\n");

    FOR_EACH_MVD(mvd) {
        Com_Printf("%2d %-12.12s %-8.8s %3d %3d %-4.4s %3d %4u %s\n",
                   mvd->id, mvd->name, mvd->mapname,
                   List_Count(&mvd->clients), mvd->numplayers,
                   mvd_states[mvd->state],
                   FIFO_Percent(&mvd->delay), mvd->num_packets,
                   mvd->gtv ? mvd->gtv->address : "<disconnected>");
    }
}

static void list_recordings(void)
{
    mvd_t *mvd;
    char buffer[8];

    Com_Printf(
        "id name         map      size name\n"
        "-- ------------ -------- ---- --------------\n");

    FOR_EACH_MVD(mvd) {
        if (mvd->demorecording) {
            Com_FormatSize(buffer, sizeof(buffer), FS_Tell(mvd->demorecording));
        } else {
            strcpy(buffer, "-");
        }
        Com_Printf("%2d %-12.12s %-8.8s %-4s %s\n",
                   mvd->id, mvd->name, mvd->mapname,
                   buffer, mvd->demoname ? mvd->demoname : "-");
    }
}

static void MVD_ListChannels_f(void)
{
    char *s;

    if (LIST_EMPTY(&mvd_channel_list)) {
        Com_Printf("No MVD channels.\n");
        return;
    }

    s = Cmd_Argv(1);
    if (*s == 'r') {
        list_recordings();
    } else {
        list_generic();
    }
}

static void MVD_ListServers_f(void)
{
    gtv_t *gtv;
    unsigned ratio;

    if (LIST_EMPTY(&mvd_gtv_list)) {
        Com_Printf("No GTV connections.\n");
        return;
    }

    Com_Printf(
        "id name         state        ratio lastmsg address       \n"
        "-- ------------ ------------ ----- ------- --------------\n");

    FOR_EACH_GTV(gtv) {
        ratio = 100;
#if USE_ZLIB
        if (gtv->z_act && gtv->z_str.total_out) {
            ratio = 100 * ((double)gtv->z_str.total_in /
                           gtv->z_str.total_out);
        }
#endif
        Com_Printf("%2d %-12.12s %-12.12s %4u%% %7u %s\n",
                   gtv->id, gtv->name, gtv_states[gtv->state],
                   ratio, svs.realtime - gtv->last_rcvd,
                   NET_AdrToString(&gtv->stream.address));
    }
}

void MVD_StreamedStop_f(void)
{
    mvd_t *mvd;

    mvd = MVD_SetChannel(1);
    if (!mvd) {
        Com_Printf("Usage: %s [chanid]\n", Cmd_Argv(0));
        return;
    }

    if (!mvd->demorecording) {
        Com_Printf("[%s] Not recording a demo.\n", mvd->name);
        return;
    }

    MVD_StopRecord(mvd);

    Com_Printf("[%s] Stopped recording.\n", mvd->name);
}

static inline int player_flags(mvd_t *mvd, mvd_player_t *player)
{
    int flags = mvd->psFlags;

    if (!player->inuse)
        flags |= MSG_PS_REMOVE;

    return flags;
}

static inline int entity_flags(mvd_t *mvd, edict_t *ent)
{
    int flags = mvd->esFlags;

    if (!ent->inuse) {
        flags |= MSG_ES_REMOVE;
    } else if (ent->s.number <= mvd->maxclients) {
        mvd_player_t *player = &mvd->players[ent->s.number - 1];
        if (player->inuse && player->ps.pmove.pm_type == PM_NORMAL)
            flags |= MSG_ES_FIRSTPERSON;
    }

    return flags;
}

static void emit_base_frame(mvd_t *mvd)
{
    edict_t         *ent;
    mvd_player_t    *player;
    int             i, portalbytes;
    byte            portalbits[MAX_MAP_PORTAL_BYTES];
    entity_packed_t es;
    player_packed_t ps;

    portalbytes = CM_WritePortalBits(&mvd->cm, portalbits);
    MSG_WriteByte(portalbytes);
    MSG_WriteData(portalbits, portalbytes);

    // send base player states
    for (i = 0; i < mvd->maxclients; i++) {
        player = &mvd->players[i];
        MSG_PackPlayer(&ps, &player->ps);
        MSG_WriteDeltaPlayerstate_Packet(NULL, &ps, i, player_flags(mvd, player));
    }
    MSG_WriteByte(CLIENTNUM_NONE);

    // send base entity states
    for (i = 1; i < mvd->csr->max_edicts; i++) {
        ent = &mvd->edicts[i];
        if (!(ent->svflags & SVF_MONSTER))
            continue;   // entity never seen
        ent->s.number = i;
        MSG_PackEntity(&es, &ent->s, &ent->x);
        MSG_WriteDeltaEntity(NULL, &es, entity_flags(mvd, ent));
    }
    MSG_WriteShort(0);
}

static void emit_gamestate(mvd_t *mvd)
{
    int         i, extra;
    char        *s;
    size_t      len;

    // pack MVD stream flags into extra bits
    extra = mvd->flags << SVCMD_BITS;

    // send the serverdata
    MSG_WriteByte(mvd_serverdata | extra);
    MSG_WriteLong(PROTOCOL_VERSION_MVD);
    MSG_WriteLong(mvd->version);
    MSG_WriteLong(mvd->servercount);
    MSG_WriteString(mvd->gamedir);
    MSG_WriteShort(mvd->clientNum);

    // send configstrings
    for (i = 0; i < mvd->csr->end; i++) {
        s = mvd->configstrings[i];
        if (!*s)
            continue;

        len = Q_strnlen(s, MAX_QPATH);
        MSG_WriteShort(i);
        MSG_WriteData(s, len);
        MSG_WriteByte(0);
    }
    MSG_WriteShort(i);

    // send baseline frame
    emit_base_frame(mvd);

    // TODO: write private layouts/configstrings
}

void MVD_StreamedRecord_f(void)
{
    char buffer[MAX_OSPATH];
    qhandle_t f;
    mvd_t *mvd;
    uint32_t magic;
    uint16_t msglen;
    unsigned mode = FS_MODE_WRITE;
    int ret;
    int c;

    while ((c = Cmd_ParseOptions(o_record)) != -1) {
        switch (c) {
        case 'h':
            Cmd_PrintUsage(o_record, "<filename> [chanid]");
            Com_Printf("Begin MVD recording on the specified channel.\n");
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

    if ((mvd = MVD_SetChannel(cmd_optind + 1)) == NULL) {
        Cmd_PrintHint();
        return;
    }

    if (mvd->demorecording) {
        Com_Printf("[%s] Already recording into %s.\n",
                   mvd->name, mvd->demoname);
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

    Com_Printf("[%s] Recording into %s\n", mvd->name, buffer);

    mvd->demorecording = f;
    mvd->demoname = MVD_CopyString(buffer);

    emit_gamestate(mvd);

    // write magic
    magic = MVD_MAGIC;
    ret = FS_Write(&magic, 4, f);
    if (ret != 4)
        goto fail;

    // write gamestate
    msglen = LittleShort(msg_write.cursize);
    ret = FS_Write(&msglen, 2, f);
    if (ret != 2)
        goto fail;
    ret = FS_Write(msg_write.data, msg_write.cursize, f);
    if (ret != msg_write.cursize)
        goto fail;

    SZ_Clear(&msg_write);
    return;

fail:
    SZ_Clear(&msg_write);
    Com_EPrintf("[%s] Couldn't write demo: %s\n", mvd->name, Q_ErrorString(ret));
    MVD_StopRecord(mvd);
}

static const cmd_option_t o_mvdconnect[] = {
    { "h", "help", "display this message" },
    { "n:string", "name", "specify channel name as <string>" },
    { "u:string", "user", "specify username as <string>" },
    { "p:string", "pass", "specify password as <string>" },
    { NULL }
};

static void MVD_Connect_c(genctx_t *ctx, int argnum)
{
    Cmd_Option_c(o_mvdconnect, Com_Address_g, ctx, argnum);
}

/*
==============
MVD_Connect_f
==============
*/
static void MVD_Connect_f(void)
{
    netadr_t adr;
    netstream_t stream;
    char *name = NULL, *username = NULL, *password = NULL;
    gtv_t *gtv;
    int c;

    while ((c = Cmd_ParseOptions(o_mvdconnect)) != -1) {
        switch (c) {
        case 'h':
            Cmd_PrintUsage(o_mvdconnect, "<address[:port]>");
            Com_Printf("Connect to the specified MVD/GTV server.\n");
            Cmd_PrintHelp(o_mvdconnect);
            return;
        case 'n':
            name = cmd_optarg;
            break;
        case 'u':
            username = cmd_optarg;
            break;
        case 'p':
            password = cmd_optarg;
            break;
        default:
            return;
        }
    }

    if (!cmd_optarg[0]) {
        Com_Printf("Missing address argument.\n");
        Cmd_PrintHint();
        return;
    }

    // resolve hostname
    if (!NET_StringToAdr(cmd_optarg, &adr, PORT_SERVER)) {
        Com_Printf("Bad server address: %s\n", cmd_optarg);
        return;
    }

    // don't allow multiple connections
    FOR_EACH_GTV(gtv) {
        if (NET_IsEqualAdr(&adr, &gtv->stream.address)) {
            Com_Printf("[%s] =!= Connection to %s already exists.\n",
                       gtv->name, NET_AdrToString(&adr));
            return;
        }
    }

    // create new socket and start connecting
    if (NET_Connect(&adr, &stream)) {
        Com_EPrintf("Unable to connect to %s\n",
                    NET_AdrToString(&adr));
        return;
    }

    // create new connection
    gtv = MVD_Mallocz(sizeof(*gtv));
    gtv->id = mvd_chanid++;
    gtv->state = GTV_CONNECTING;
    gtv->stream = stream;
    gtv->last_sent = gtv->last_rcvd = svs.realtime;
    gtv->run = gtv_run;
    gtv->drop = gtv_drop;
    gtv->destroy = gtv_destroy;
    gtv->username = MVD_CopyString(username);
    gtv->password = MVD_CopyString(password);
    List_Append(&mvd_gtv_list, &gtv->entry);

    // set channel name
    if (name) {
        Q_strlcpy(gtv->name, name, sizeof(gtv->name));
    } else {
        Q_snprintf(gtv->name, sizeof(gtv->name), "net%d", gtv->id);
    }

    Q_strlcpy(gtv->address, cmd_optarg, sizeof(gtv->address));

    Com_Printf("[%s] -=- Connecting to %s...\n",
               gtv->name, NET_AdrToString(&adr));
}

static const cmd_option_t o_mvdisconnect[] = {
    { "a", "all", "destroy all connections" },
    { "h", "help", "display this message" },
    { NULL }
};

static void MVD_Disconnect_c(genctx_t *ctx, int argnum)
{
    Cmd_Option_c(o_mvdisconnect, NULL, ctx, argnum);
}

static void MVD_Disconnect_f(void)
{
    gtv_t *gtv, *next;
    bool all = false;
    int c;

    while ((c = Cmd_ParseOptions(o_mvdisconnect)) != -1) {
        switch (c) {
        case 'h':
            Cmd_PrintUsage(o_mvdisconnect, "[conn_id]");
            Com_Printf("Destroy specified MVD/GTV server connection.\n");
            Cmd_PrintHelp(o_mvdisconnect);
            return;
        case 'a':
            all = true;
            break;
        default:
            return;
        }
    }

    if (all) {
        if (LIST_EMPTY(&mvd_gtv_list)) {
            Com_Printf("No GTV connections.\n");
            return;
        }
        LIST_FOR_EACH_SAFE(gtv_t, gtv, next, &mvd_gtv_list, entry) {
            gtv->destroy(gtv);
        }
        Com_Printf("Destroyed all GTV connections.\n");
        return;
    }

    gtv = gtv_set_conn(cmd_optind);
    if (!gtv) {
        return;
    }

    Com_Printf("[%s] =X= Connection destroyed.\n", gtv->name);
    gtv->destroy(gtv);
}

static void MVD_Kill_f(void)
{
    mvd_t *mvd;

    mvd = MVD_SetChannel(1);
    if (!mvd) {
        return;
    }

    Com_Printf("[%s] =X= Channel was killed.\n", mvd->name);
    MVD_Destroy(mvd);
}

static void MVD_Pause_f(void)
{
    mvd_t *mvd;

    mvd = MVD_SetChannel(1);
    if (!mvd) {
        return;
    }

    if (!mvd->gtv || !mvd->gtv->demoplayback) {
        Com_Printf("[%s] Only demo channels can be paused.\n", mvd->name);
        return;
    }

    switch (mvd->state) {
    case MVD_WAITING:
        //Com_Printf("[%s] Channel was resumed.\n", mvd->name);
        mvd->state = MVD_READING;
        break;
    case MVD_READING:
        //Com_Printf("[%s] Channel was paused.\n", mvd->name);
        mvd->state = MVD_WAITING;
        break;
    default:
        break;
    }
}

static void MVD_Skip_f(void)
{
    mvd_t *mvd;
    int count;

    mvd = MVD_SetChannel(1);
    if (!mvd) {
        Com_Printf("Usage: %s [chan_id] [count]\n", Cmd_Argv(0));
        return;
    }

    count = Q_atoi(Cmd_Argv(2));
    if (count < 1) {
        count = 1;
    }

    if (!mvd->gtv || !mvd->gtv->demoplayback) {
        Com_Printf("[%s] Maps can be skipped only on demo channels.\n", mvd->name);
        return;
    }

    mvd->gtv->demoskip = count;
}

static void MVD_Seek_f(void)
{
    mvd_t *mvd;
    gtv_t *gtv;
    mvd_client_t *client;
    mvd_snap_t *snap;
    int i, j, ret, index, frames;
    int64_t dest;
    char *from, *to;
    edict_t *ent;
    bool gamestate, back_seek, byte_seek;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s [+-]<timespec|percent>[%%] [chanid]\n", Cmd_Argv(0));
        return;
    }

    mvd = MVD_SetChannel(2);
    if (!mvd) {
        return;
    }

    gtv = mvd->gtv;
    if (!gtv || !gtv->demoplayback) {
        Com_Printf("[%s] Seeking is only supported on demo channels.\n", mvd->name);
        return;
    }

    if (mvd->demorecording) {
        // need some sort of nodelta frame support for that :(
        Com_Printf("[%s] Seeking is not yet supported during demo recording, sorry.\n", mvd->name);
        return;
    }

    to = Cmd_Argv(1);

    if (strchr(to, '%')) {
        char *suf;
        float percent = strtof(to, &suf);
        if (suf == to || strcmp(suf, "%") || !isfinite(percent)) {
            Com_Printf("[%s] Invalid percentage.\n", mvd->name);
            return;
        }

        if (!gtv->demosize) {
            Com_Printf("[%s] Unknown file size, can't seek.\n", mvd->name);
            return;
        }

        percent = Q_clipf(percent, 0, 100);
        dest = gtv->demoofs + gtv->demosize * percent / 100;

        byte_seek = true;
        back_seek = dest < FS_Tell(gtv->demoplayback);
    } else {
        if (*to == '-' || *to == '+') {
            // relative to current frame
            if (!Com_ParseTimespec(to + 1, &frames)) {
                Com_Printf("Invalid relative timespec.\n");
                return;
            }
            if (*to == '-')
                frames = -frames;
            dest = mvd->framenum + frames;
        } else {
            // relative to first frame
            if (!Com_ParseTimespec(to, &i)) {
                Com_Printf("Invalid absolute timespec.\n");
                return;
            }
            dest = i;
            frames = i - mvd->framenum;
        }

        if (!frames)
            return; // already there

        byte_seek = false;
        back_seek = frames < 0;
    }

    if (setjmp(mvd_jmpbuf))
        return;

    // disable effects processing
    mvd->demoseeking = true;

    // clear dirty configstrings
    memset(mvd->dcs, 0, sizeof(mvd->dcs));

    Com_DPrintf("[%d] seeking to %"PRId64"\n", mvd->framenum, dest);

    // seek to the previous most recent snapshot
    if (back_seek || mvd->last_snapshot > mvd->framenum) {
        snap = demo_find_snapshot(mvd, dest, byte_seek);

        if (snap) {
            Com_DPrintf("found snap at %d\n", snap->framenum);
            ret = FS_Seek(gtv->demoplayback, snap->filepos, SEEK_SET);
            if (ret < 0) {
                Com_EPrintf("[%s] Couldn't seek demo: %s\n", mvd->name, Q_ErrorString(ret));
                goto done;
            }

            // clear delta state
            MVD_ClearState(mvd, false);

            // reset configstrings
            for (i = 0; i < mvd->csr->end; i++) {
                from = mvd->baseconfigstrings[i];
                to = mvd->configstrings[i];

                if (!strcmp(from, to))
                    continue;

                Q_SetBit(mvd->dcs, i);
                strcpy(to, from);
            }

            // set player names
            MVD_SetPlayerNames(mvd);

            SZ_Init(&msg_read, snap->data, snap->msglen);
            msg_read.cursize = snap->msglen;

            MVD_ParseMessage(mvd);
            mvd->framenum = snap->framenum;
        } else if (back_seek) {
            Com_Printf("[%s] Couldn't seek backwards without snapshots!\n", mvd->name);
            goto done;
        }
    }

    // skip forward to destination frame/position
    while (1) {
        int64_t pos = byte_seek ? FS_Tell(gtv->demoplayback) : mvd->framenum;
        if (pos >= dest)
            break;

        ret = demo_read_message(gtv->demoplayback);
        if (ret <= 0) {
            demo_finish(gtv, ret);
            return;
        }

        gamestate = MVD_ParseMessage(mvd);

        demo_emit_snapshot(mvd);

        if (gamestate) {
            // got a gamestate, abort seek
            Com_DPrintf("got gamestate while seeking!\n");
            goto done;
        }
    }

    Com_DPrintf("[%d] after skip\n", mvd->framenum);

    // update dirty configstrings
    for (i = 0; i < CS_BITMAP_LONGS; i++) {
        if (((uint32_t *)mvd->dcs)[i] == 0)
            continue;

        index = i << 5;
        for (j = 0; j < 32; j++, index++) {
            if (Q_IsBitSet(mvd->dcs, index))
                MVD_UpdateConfigstring(mvd, index);
        }
    }

    // write private configstrings
    FOR_EACH_MVDCL(client, mvd) {
        if (client->cl->state < cs_spawned)
            continue;

        if (client->target)
            MVD_WriteStringList(client, client->target->configstrings);
        else if (mvd->dummy)
            MVD_WriteStringList(client, mvd->dummy->configstrings);

        if (client->layout_type == LAYOUT_SCORES)
            client->layout_time = 0;
    }

    // ouch
    CM_SetPortalStates(&mvd->cm, NULL, 0);

    // init world entity
    ent = &mvd->edicts[0];
    ent->solid = SOLID_BSP;
    ent->inuse = true;

    // relink all seen entities, reset old origins and events
    for (i = 1; i < mvd->csr->max_edicts; i++) {
        ent = &mvd->edicts[i];

        if (ent->svflags & SVF_MONSTER)
            MVD_LinkEdict(mvd, ent);

        if (!ent->inuse)
            continue;

        if (!(ent->s.renderfx & RF_BEAM))
            VectorCopy(ent->s.origin, ent->s.old_origin);

        ent->s.event = EV_OTHER_TELEPORT;
    }

    MVD_UpdateClients(mvd);

    // wait one frame to give entity events a chance to be communicated back to
    // clients
    gtv->demowait = true;

    demo_update(gtv);

done:
    mvd->demoseeking = false;
}

static void MVD_Control_f(void)
{
    static const cmd_option_t options[] = {
        { "h", "help", "display this message" },
        { "l:number", "loop", "replay <number> of times (0 means forever)" },
        { "n:string", "name", "specify channel name as <string>" },
        { NULL }
    };
    mvd_t *mvd;
    char *name = NULL;
    int loop = -1;
    int todo = 0;
    int c;

    while ((c = Cmd_ParseOptions(options)) != -1) {
        switch (c) {
        case 'h':
            Cmd_PrintUsage(options, "[chanid]");
            Com_Printf("Change attributes of existing MVD channel.\n");
            Cmd_PrintHelp(options);
            return;
        case 'l':
            loop = Q_atoi(cmd_optarg);
            if (loop < 0) {
                Com_Printf("Invalid value for %s option.\n", cmd_optopt);
                Cmd_PrintHint();
                return;
            }
            todo |= 1;
            break;
        case 'n':
            name = cmd_optarg;
            todo |= 2;
            break;
        default:
            return;
        }
    }

    if (!todo) {
        Com_Printf("At least one option needed.\n");
        Cmd_PrintHint();
        return;
    }

    mvd = MVD_SetChannel(cmd_optind);
    if (!mvd) {
        Cmd_PrintHint();
        return;
    }

    if (name) {
        Com_Printf("[%s] Channel renamed to %s.\n", mvd->name, name);
        Q_strlcpy(mvd->name, name, sizeof(mvd->name));
    }
    if (loop != -1) {
        //Com_Printf("[%s] Loop count changed to %d.\n", mvd->name, loop);
        //mvd->demoloop = loop;
    }
}

static const cmd_option_t o_mvdplay[] = {
    { "h", "help", "display this message" },
    { "l:number", "loop", "replay <number> of times (0 means forever)" },
    { "n:string", "name", "specify channel name as <string>" },
    //{ "i:chan_id", "insert", "insert new entries before <chan_id> playlist" },
    //{ "a:chan_id", "append", "append new entries after <chan_id> playlist" },
    { "r:chan_id", "replace", "replace <chan_id> playlist with new entries" },
    { NULL }
};

void MVD_File_g(genctx_t *ctx)
{
    FS_File_g("demos", "*.mvd2;*.mvd2.gz", FS_SEARCH_SAVEPATH | FS_SEARCH_BYFILTER, ctx);
}

static void MVD_Play_c(genctx_t *ctx, int argnum)
{
    Cmd_Option_c(o_mvdplay, MVD_File_g, ctx, argnum);
}

static void MVD_Play_f(void)
{
    char *name = NULL;
    char buffer[MAX_OSPATH];
    int loop = -1, chan_id = -1;
    qhandle_t f;
    size_t len;
    gtv_t *gtv = NULL;
    int c, argc;
    string_entry_t *entry, *head;
    int i;

    while ((c = Cmd_ParseOptions(o_mvdplay)) != -1) {
        switch (c) {
        case 'h':
            Cmd_PrintUsage(o_mvdplay, "[/]<filename> [...]");
            Com_Printf("Create new MVD channel and begin demo playback.\n");
            Cmd_PrintHelp(o_mvdplay);
            Com_Printf("Final path is formatted as demos/<filename>.mvd2.\n"
                       "Prepend slash to specify raw path.\n");
            return;
        case 'l':
            loop = Q_atoi(cmd_optarg);
            if (loop < 0) {
                Com_Printf("Invalid value for %s option.\n", cmd_optopt);
                Cmd_PrintHint();
                return;
            }
            break;
        case 'n':
            name = cmd_optarg;
            break;
        case 'r':
            chan_id = cmd_optind - 1;
            break;
        default:
            return;
        }
    }

    argc = Cmd_Argc();
    if (cmd_optind == argc) {
        Com_Printf("Missing filename argument.\n");
        Cmd_PrintHint();
        return;
    }

    if (chan_id != -1) {
        mvd_t *mvd = MVD_SetChannel(chan_id);
        if (mvd) {
            gtv = mvd->gtv;
        }
    }

    // build the playlist
    head = NULL;
    for (i = argc - 1; i >= cmd_optind; i--) {
        // try to open it
        f = FS_EasyOpenFile(buffer, sizeof(buffer), FS_MODE_READ,
                            "demos/", Cmd_Argv(i), ".mvd2");
        if (!f) {
            continue;
        }

        FS_CloseFile(f);

        len = strlen(buffer);
        entry = MVD_Malloc(sizeof(*entry) + len);
        memcpy(entry->string, buffer, len + 1);
        entry->next = head;
        head = entry;
    }

    if (!head) {
        return;
    }

    if (gtv) {
        // free existing playlist
        demo_free_playlist(gtv);
    } else {
        // create new connection
        gtv = MVD_Mallocz(sizeof(*gtv));
        gtv->id = mvd_chanid++;
        gtv->state = GTV_READING;
        gtv->drop = demo_destroy;
        gtv->destroy = demo_destroy;
        gtv->demoloop = 1;
        Q_snprintf(gtv->name, sizeof(gtv->name), "dem%d", gtv->id);
    }

    // set channel name
    if (name) {
        Q_strlcpy(gtv->name, name, sizeof(gtv->name));
    }

    // set loop parameter
    if (loop != -1) {
        gtv->demoloop = loop;
    }

    // set new playlist
    gtv->demohead = head;

    if (setjmp(mvd_jmpbuf)) {
        return;
    }

    demo_play_next(gtv, head);
}


void MVD_Shutdown(void)
{
    gtv_t *gtv, *gtv_next;
    mvd_t *mvd, *mvd_next;

    // kill all GTV connections
    LIST_FOR_EACH_SAFE(gtv_t, gtv, gtv_next, &mvd_gtv_list, entry) {
        gtv->destroy(gtv);
    }

    // kill all MVD channels (including demo GTVs)
    LIST_FOR_EACH_SAFE(mvd_t, mvd, mvd_next, &mvd_channel_list, entry) {
        if (mvd->gtv) {
            mvd->gtv->mvd = NULL; // don't double destroy
            mvd->gtv->destroy(mvd->gtv);
        }
        MVD_Free(mvd);
    }

    List_Init(&mvd_gtv_list);
    List_Init(&mvd_channel_list);

    Z_Freep((void**)&mvd_clients);
    Z_Freep((void**)&mvd_ge.edicts);

    mvd_chanid = 0;

    mvd_active = false;

    Z_LeakTest(TAG_MVD);
}

static const cmdreg_t c_mvd[] = {
    { "mvdplay", MVD_Play_f, MVD_Play_c },
    { "mvdconnect", MVD_Connect_f, MVD_Connect_c },
    { "mvdisconnect", MVD_Disconnect_f, MVD_Disconnect_c },
    { "mvdkill", MVD_Kill_f },
    { "mvdspawn", MVD_Spawn_f },
    { "mvdchannels", MVD_ListChannels_f },
    { "mvdservers", MVD_ListServers_f },
    { "mvdcontrol", MVD_Control_f },
    { "mvdpause", MVD_Pause_f },
    { "mvdskip", MVD_Skip_f },
    { "mvdseek", MVD_Seek_f },

    { NULL }
};

static void mvd_wait_delay_changed(cvar_t *self)
{
    self->integer = 10 * Cvar_ClampValue(self, 0, 60 * 60);
}

/*
==============
MVD_Register
==============
*/
void MVD_Register(void)
{
#if USE_DEBUG
    mvd_shownet = Cvar_Get("mvd_shownet", "0", 0);
#endif
    mvd_timeout = Cvar_Get("mvd_timeout", "90", 0);
    mvd_timeout->changed = sv_sec_timeout_changed;
    mvd_timeout->changed(mvd_timeout);
    mvd_suspend_time = Cvar_Get("mvd_suspend_time", "5", 0);
    mvd_suspend_time->changed = sv_min_timeout_changed;
    mvd_suspend_time->changed(mvd_suspend_time);
    mvd_wait_delay = Cvar_Get("mvd_wait_delay", "20", 0);
    mvd_wait_delay->changed = mvd_wait_delay_changed;
    mvd_wait_delay->changed(mvd_wait_delay);
    mvd_wait_percent = Cvar_Get("mvd_wait_percent", "50", 0);
    mvd_buffer_size = Cvar_Get("mvd_buffer_size", "8", 0);
    mvd_username = Cvar_Get("mvd_username", "unnamed", 0);
    mvd_password = Cvar_Get("mvd_password", "", CVAR_PRIVATE);
    mvd_snaps = Cvar_Get("mvd_snaps", "10", 0);

    Cmd_Register(c_mvd);
}

