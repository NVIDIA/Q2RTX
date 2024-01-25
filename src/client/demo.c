/*
Copyright (C) 2003-2006 Andrey Nazarov

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
// cl_demo.c - demo recording and playback
//

#include "client.h"

static byte     demo_buffer[MAX_MSGLEN];

static cvar_t   *cl_demosnaps;
static cvar_t   *cl_demomsglen;
static cvar_t   *cl_demowait;
static cvar_t   *cl_demosuspendtoggle;

// =========================================================================

/*
====================
CL_WriteDemoMessage

Dumps the current demo message, prefixed by the length.
Stops demo recording and returns false on write error.
====================
*/
bool CL_WriteDemoMessage(sizebuf_t *buf)
{
    uint32_t msglen;
    int ret;

    if (buf->overflowed) {
        SZ_Clear(buf);
        Com_WPrintf("Demo message overflowed (should never happen).\n");
        return true;
    }

    if (!buf->cursize)
        return true;

    msglen = LittleLong(buf->cursize);
    ret = FS_Write(&msglen, 4, cls.demo.recording);
    if (ret != 4)
        goto fail;
    ret = FS_Write(buf->data, buf->cursize, cls.demo.recording);
    if (ret != buf->cursize)
        goto fail;

    Com_DDPrintf("%s: wrote %zu bytes\n", __func__, buf->cursize);

    SZ_Clear(buf);
    return true;

fail:
    SZ_Clear(buf);
    Com_EPrintf("Couldn't write demo: %s\n", Q_ErrorString(ret));
    CL_Stop_f();
    return false;
}

// writes a delta update of an entity_state_t list to the message.
static void emit_packet_entities(server_frame_t *from, server_frame_t *to)
{
    entity_packed_t oldpack, newpack;
    centity_state_t *oldent, *newent;
    int     oldindex, newindex;
    int     oldnum, newnum;
    int     i, from_num_entities;

    if (!from)
        from_num_entities = 0;
    else
        from_num_entities = from->numEntities;

    newindex = 0;
    oldindex = 0;
    oldent = newent = NULL;
    while (newindex < to->numEntities || oldindex < from_num_entities) {
        if (newindex >= to->numEntities) {
            newnum = 9999;
        } else {
            i = (to->firstEntity + newindex) & PARSE_ENTITIES_MASK;
            newent = &cl.entityStates[i];
            newnum = newent->number;
        }

        if (oldindex >= from_num_entities) {
            oldnum = 9999;
        } else {
            i = (from->firstEntity + oldindex) & PARSE_ENTITIES_MASK;
            oldent = &cl.entityStates[i];
            oldnum = oldent->number;
        }

        if (newnum == oldnum) {
            // Delta update from old position. Because the force parm is false,
            // this will not result in any bytes being emitted if the entity has
            // not changed at all. Note that players are always 'newentities',
            // this updates their old_origin always and prevents warping in case
            // of packet loss.
            msgEsFlags_t flags = cls.demo.esFlags;
            if (newent->number <= cl.maxclients)
                flags |= MSG_ES_NEWENTITY;
            CL_PackEntity(&oldpack, oldent);
            CL_PackEntity(&newpack, newent);
            MSG_WriteDeltaEntity(&oldpack, &newpack, flags);
            oldindex++;
            newindex++;
            continue;
        }

        if (newnum < oldnum) {
            // this is a new entity, send it from the baseline
            CL_PackEntity(&oldpack, &cl.baselines[newnum]);
            CL_PackEntity(&newpack, newent);
            MSG_WriteDeltaEntity(&oldpack, &newpack, cls.demo.esFlags | MSG_ES_FORCE | MSG_ES_NEWENTITY);
            newindex++;
            continue;
        }

        if (newnum > oldnum) {
            // the old entity isn't present in the new message
            CL_PackEntity(&oldpack, oldent);
            MSG_WriteDeltaEntity(&oldpack, NULL, MSG_ES_FORCE);
            oldindex++;
            continue;
        }
    }

    MSG_WriteShort(0);      // end of packetentities
}

static void emit_delta_frame(server_frame_t *from, server_frame_t *to,
                             int fromnum, int tonum)
{
    player_packed_t oldpack, newpack;

    MSG_WriteByte(svc_frame);
    MSG_WriteLong(tonum);
    MSG_WriteLong(fromnum); // what we are delta'ing from
    if (cls.serverProtocol != PROTOCOL_VERSION_OLD)
        MSG_WriteByte(0);   // rate dropped packets

    // send over the areabits
    MSG_WriteByte(to->areabytes);
    MSG_WriteData(to->areabits, to->areabytes);

    // delta encode the playerstate
    MSG_WriteByte(svc_playerinfo);
    MSG_PackPlayer(&newpack, &to->ps);
    if (from) {
        MSG_PackPlayer(&oldpack, &from->ps);
        MSG_WriteDeltaPlayerstate_Default(&oldpack, &newpack, cl.psFlags);
    } else {
        MSG_WriteDeltaPlayerstate_Default(NULL, &newpack, cl.psFlags);
    }

    // delta encode the entities
    MSG_WriteByte(svc_packetentities);
    emit_packet_entities(from, to);
}

// frames_written counter starts at 0, but we add 1 to every frame number
// because frame 0 can't be used due to protocol limitation (hack).
#define FRAME_PRE   (cls.demo.frames_written)
#define FRAME_CUR   (cls.demo.frames_written + 1)

/*
====================
CL_EmitDemoFrame

Writes delta from the last frame we got to the current frame.
====================
*/
void CL_EmitDemoFrame(void)
{
    server_frame_t  *oldframe;
    int             lastframe;

    if (!cl.frame.valid)
        return;

    // the first frame is delta uncompressed
    if (cls.demo.last_server_frame == -1) {
        oldframe = NULL;
        lastframe = -1;
    } else {
        oldframe = &cl.frames[cls.demo.last_server_frame & UPDATE_MASK];
        lastframe = FRAME_PRE;
        if (oldframe->number != cls.demo.last_server_frame || !oldframe->valid ||
            cl.numEntityStates - oldframe->firstEntity > MAX_PARSE_ENTITIES) {
            oldframe = NULL;
            lastframe = -1;
        }
    }

    // emit and flush frame
    emit_delta_frame(oldframe, &cl.frame, lastframe, FRAME_CUR);

    if (cls.demo.buffer.cursize + msg_write.cursize > cls.demo.buffer.maxsize) {
        Com_DPrintf("Demo frame overflowed (%zu + %zu > %zu)\n",
                    cls.demo.buffer.cursize, msg_write.cursize, cls.demo.buffer.maxsize);
        cls.demo.frames_dropped++;

        // warn the user if drop rate is too high
        if (cls.demo.frames_written < 10 && cls.demo.frames_dropped == 50)
            Com_WPrintf("Too many demo frames don't fit into %zu bytes.\n"
                        "Try to increase 'cl_demomsglen' value and restart recording.\n",
                        cls.demo.buffer.maxsize);
    } else {
        SZ_Write(&cls.demo.buffer, msg_write.data, msg_write.cursize);
        cls.demo.last_server_frame = cl.frame.number;
        cls.demo.frames_written++;
    }

    SZ_Clear(&msg_write);
}

static size_t format_demo_size(char *buffer, size_t size)
{
    return Com_FormatSizeLong(buffer, size, FS_Tell(cls.demo.recording));
}

static size_t format_demo_status(char *buffer, size_t size)
{
    size_t len = format_demo_size(buffer, size);
    int min, sec, frames = cls.demo.frames_written;

    sec = frames / 10; frames %= 10;
    min = sec / 60; sec %= 60;

    len += Q_scnprintf(buffer + len, size - len, ", %d:%02d.%d",
                       min, sec, frames);

    if (cls.demo.frames_dropped) {
        len += Q_scnprintf(buffer + len, size - len, ", %d frame%s dropped",
                           cls.demo.frames_dropped,
                           cls.demo.frames_dropped == 1 ? "" : "s");
    }

    if (cls.demo.others_dropped) {
        len += Q_scnprintf(buffer + len, size - len, ", %d message%s dropped",
                           cls.demo.others_dropped,
                           cls.demo.others_dropped == 1 ? "" : "s");
    }

    return len;
}

/*
====================
CL_Stop_f

stop recording a demo
====================
*/
void CL_Stop_f(void)
{
    uint32_t msglen;
    char buffer[MAX_QPATH];

    if (!cls.demo.recording) {
        Com_Printf("Not recording a demo.\n");
        return;
    }

// finish up
    msglen = (uint32_t)-1;
    FS_Write(&msglen, 4, cls.demo.recording);

    format_demo_size(buffer, sizeof(buffer));

// close demofile
    FS_CloseFile(cls.demo.recording);
    cls.demo.recording = 0;
    cls.demo.paused = false;
    cls.demo.frames_written = 0;
    cls.demo.frames_dropped = 0;
    cls.demo.others_dropped = 0;

// print some statistics
    Com_Printf("Stopped demo (%s).\n", buffer);

// tell the server we finished recording
    CL_UpdateRecordingSetting();
}

static const cmd_option_t o_record[] = {
    { "h", "help", "display this message" },
    { "z", "compress", "compress demo with gzip" },
    { "e", "extended", "use extended packet size" },
    { "s", "standard", "use standard packet size" },
    { NULL }
};

/*
====================
CL_Record_f

record <demoname>

Begins recording a demo from the current position
====================
*/
static void CL_Record_f(void)
{
    char    buffer[MAX_OSPATH];
    int     i, c;
    size_t  len;
    centity_state_t *ent;
    entity_packed_t pack;
    char            *s;
    qhandle_t       f;
    unsigned        mode = FS_MODE_WRITE;
    size_t          size = Cvar_ClampInteger(
                               cl_demomsglen,
                               MIN_PACKETLEN,
                               MAX_MSGLEN);

    while ((c = Cmd_ParseOptions(o_record)) != -1) {
        switch (c) {
        case 'h':
            Cmd_PrintUsage(o_record, "<filename>");
            Com_Printf("Begin client demo recording.\n");
            Cmd_PrintHelp(o_record);
            return;
        case 'z':
            mode |= FS_FLAG_GZIP;
            break;
        case 'e':
            size = MAX_PACKETLEN_WRITABLE;
            break;
        case 's':
            size = MAX_PACKETLEN_WRITABLE_DEFAULT;
            break;
        default:
            return;
        }
    }

    if (cls.demo.recording) {
        format_demo_status(buffer, sizeof(buffer));
        Com_Printf("Already recording (%s).\n", buffer);
        return;
    }

    if (!cmd_optarg[0]) {
        Com_Printf("Missing filename argument.\n");
        Cmd_PrintHint();
        return;
    }

    if (cls.state != ca_active) {
        Com_Printf("You must be in a level to record.\n");
        return;
    }

    //
    // open the demo file
    //
    f = FS_EasyOpenFile(buffer, sizeof(buffer), mode,
                        "demos/", cmd_optarg, ".dm2");
    if (!f) {
        return;
    }

    Com_Printf("Recording client demo to %s.\n", buffer);

    cls.demo.recording = f;
    cls.demo.paused = false;

    // the first frame will be delta uncompressed
    cls.demo.last_server_frame = -1;

    if (cl.csr.extended)
        size = MAX_MSGLEN;

    SZ_Init(&cls.demo.buffer, demo_buffer, size);

    // clear dirty configstrings
    memset(cl.dcs, 0, sizeof(cl.dcs));

    // tell the server we are recording
    CL_UpdateRecordingSetting();

    //
    // write out messages to hold the startup information
    //

    // send the serverdata
    MSG_WriteByte(svc_serverdata);
    if (cl.csr.extended)
        MSG_WriteLong(PROTOCOL_VERSION_EXTENDED);
    else
        MSG_WriteLong(min(cls.serverProtocol, PROTOCOL_VERSION_DEFAULT));
    MSG_WriteLong(cl.servercount);
    MSG_WriteByte(1);      // demos are always attract loops
    MSG_WriteString(cl.gamedir);
    MSG_WriteShort(cl.clientNum);
    MSG_WriteString(cl.configstrings[CS_NAME]);

    // configstrings
    for (i = 0; i < cl.csr.end; i++) {
        s = cl.configstrings[i];
        if (!*s)
            continue;

        len = Q_strnlen(s, MAX_QPATH);
        if (msg_write.cursize + len + 4 > size) {
            if (!CL_WriteDemoMessage(&msg_write))
                return;
        }

        MSG_WriteByte(svc_configstring);
        MSG_WriteShort(i);
        MSG_WriteData(s, len);
        MSG_WriteByte(0);
    }

    // baselines
    for (i = 1; i < cl.csr.max_edicts; i++) {
        ent = &cl.baselines[i];
        if (!ent->number)
            continue;

        if (msg_write.cursize + MAX_PACKETENTITY_BYTES > size) {
            if (!CL_WriteDemoMessage(&msg_write))
                return;
        }

        MSG_WriteByte(svc_spawnbaseline);
        CL_PackEntity(&pack, ent);
        MSG_WriteDeltaEntity(NULL, &pack, cls.demo.esFlags | MSG_ES_FORCE);
    }

    MSG_WriteByte(svc_stufftext);
    MSG_WriteString("precache\n");

    // write it to the demo file
    CL_WriteDemoMessage(&msg_write);

    // the rest of the demo file will be individual frames
}

// resumes demo recording after pause or seek. tries to fit flushed
// configstrings and frame into single packet for seamless 'stitch'
static void resume_record(void)
{
    int i, j, index;
    size_t len;
    char *s;

    // write dirty configstrings
    for (i = 0; i < CS_BITMAP_LONGS; i++) {
        if (((uint32_t *)cl.dcs)[i] == 0)
            continue;

        index = i << 5;
        for (j = 0; j < 32; j++, index++) {
            if (!Q_IsBitSet(cl.dcs, index))
                continue;

            s = cl.configstrings[index];

            len = Q_strnlen(s, MAX_QPATH);
            if (cls.demo.buffer.cursize + len + 4 > cls.demo.buffer.maxsize) {
                if (!CL_WriteDemoMessage(&cls.demo.buffer))
                    return;
                // multiple packets = not seamless
            }

            SZ_WriteByte(&cls.demo.buffer, svc_configstring);
            SZ_WriteShort(&cls.demo.buffer, index);
            SZ_Write(&cls.demo.buffer, s, len);
            SZ_WriteByte(&cls.demo.buffer, 0);
        }
    }

    // write delta uncompressed frame
    //cls.demo.last_server_frame = -1;
    CL_EmitDemoFrame();

    // FIXME: write layout if it fits? most likely it won't

    // write it to the demo file
    CL_WriteDemoMessage(&cls.demo.buffer);
}

static void CL_Resume_f(void)
{
    if (!cls.demo.recording) {
        Com_Printf("Not recording a demo.\n");
        return;
    }

    if (!cls.demo.paused) {
        Com_Printf("Demo recording is already resumed.\n");
        return;
    }

    resume_record();

    if (!cls.demo.recording)
        // write failed
        return;

    Com_Printf("Resumed demo recording.\n");

    cls.demo.paused = false;

    // clear dirty configstrings
    memset(cl.dcs, 0, sizeof(cl.dcs));
}

static void CL_Suspend_f(void)
{
    if (!cls.demo.recording) {
        Com_Printf("Not recording a demo.\n");
        return;
    }

    if (!cls.demo.paused) {
        Com_Printf("Suspended demo recording.\n");
        cls.demo.paused = true;
        return;
    }

    // only resume if cl_demosuspendtoggle is enabled
    if (!cl_demosuspendtoggle->integer) {
        Com_Printf("Demo recording is already suspended.\n");
        return;
    }

    CL_Resume_f();
}

static int read_first_message(qhandle_t f)
{
    uint32_t    ul;
    uint16_t    us;
    size_t      msglen;
    int         read, type;

    // read magic/msglen
    read = FS_Read(&ul, 4, f);
    if (read != 4) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    // determine demo type
    if (ul == MVD_MAGIC) {
        read = FS_Read(&us, 2, f);
        if (read != 2) {
            return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
        }
        if (!us) {
            return Q_ERR_UNEXPECTED_EOF;
        }
        msglen = LittleShort(us);
        type = 1;
    } else {
        if (ul == (uint32_t)-1) {
            return Q_ERR_UNEXPECTED_EOF;
        }
        msglen = LittleLong(ul);
        type = 0;
    }

    if (msglen > sizeof(msg_read_buffer)) {
        return Q_ERR_INVALID_FORMAT;
    }

    SZ_Init(&msg_read, msg_read_buffer, sizeof(msg_read_buffer));
    msg_read.cursize = msglen;

    // read packet data
    read = FS_Read(msg_read.data, msglen, f);
    if (read != msglen) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    return type;
}

static int read_next_message(qhandle_t f)
{
    uint32_t msglen;
    int read;

    // read msglen
    read = FS_Read(&msglen, 4, f);
    if (read != 4) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    // check for EOF packet
    if (msglen == (uint32_t)-1) {
        return 0;
    }

    msglen = LittleLong(msglen);
    if (msglen > sizeof(msg_read_buffer)) {
        return Q_ERR_INVALID_FORMAT;
    }

    SZ_Init(&msg_read, msg_read_buffer, sizeof(msg_read_buffer));
    msg_read.cursize = msglen;

    // read packet data
    read = FS_Read(msg_read.data, msglen, f);
    if (read != msglen) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    return 1;
}

static void finish_demo(int ret)
{
    const char *s = Cvar_VariableString("nextserver");

    // Only execute nextserver if back-to-back timedemos are complete
    if (!s[0] && cls.timedemo.run_current >= cls.timedemo.runs_total) {
        if (ret == 0) {
            Com_Error(ERR_DISCONNECT, "Demo finished");
        } else {
            Com_Error(ERR_DROP, "Couldn't read demo: %s", Q_ErrorString(ret));
        }
    }

    CL_Disconnect(ERR_RECONNECT);

    if (cls.timedemo.run_current < cls.timedemo.runs_total)
        return;

    Cbuf_AddText(&cmd_buffer, s);
    Cbuf_AddText(&cmd_buffer, "\n");

    Cvar_Set("nextserver", "");
}

static void update_status(void)
{
    if (cls.demo.file_size) {
        int64_t pos = FS_Tell(cls.demo.playback);

        if (pos > cls.demo.file_offset)
            cls.demo.file_progress = (float)(pos - cls.demo.file_offset) / cls.demo.file_size;
        else
            cls.demo.file_progress = 0.0f;
    }
}

static int parse_next_message(int wait)
{
    int ret;

    ret = read_next_message(cls.demo.playback);
    if (ret < 0 || (ret == 0 && wait == 0)) {
        finish_demo(ret);
        return -1;
    }

    update_status();

    if (ret == 0) {
        cls.demo.eof = true;
        return -1;
    }

    CL_ParseServerMessage();

    // if recording demo, write the message out
    if (cls.demo.recording && !cls.demo.paused && CL_FRAMESYNC) {
        CL_WriteDemoMessage(&cls.demo.buffer);
    }

    // if running GTV server, transmit to client
    CL_GTV_Transmit();

    // save a snapshot once the full packet is parsed
    CL_EmitDemoSnapshot();

    return 0;
}

/*
====================
CL_PlayDemo_f
====================
*/
static void CL_PlayDemo_f(void)
{
    char name[MAX_OSPATH];
    qhandle_t f;
    int type;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <filename>\n", Cmd_Argv(0));
        return;
    }

    f = FS_EasyOpenFile(name, sizeof(name), FS_MODE_READ | FS_FLAG_GZIP,
                        "demos/", Cmd_Argv(1), ".dm2");
    if (!f) {
        return;
    }

    type = read_first_message(f);
    if (type < 0) {
        Com_Printf("Couldn't read %s: %s\n", name, Q_ErrorString(type));
        FS_CloseFile(f);
        return;
    }

    if (type == 1) {
#if USE_MVD_CLIENT
        Cbuf_InsertText(&cmd_buffer, va("mvdplay --replace @@ \"/%s\"\n", name));
#else
        Com_Printf("MVD support was not compiled in.\n");
#endif
        FS_CloseFile(f);
        return;
    }

    // if running a local server, kill it and reissue
    SV_Shutdown("Server was killed.\n", ERR_DISCONNECT);

    CL_Disconnect(ERR_RECONNECT);

    cls.demo.playback = f;
    cls.state = ca_connected;
    Q_strlcpy(cls.servername, COM_SkipPath(name), sizeof(cls.servername));
    cls.serverAddress.type = NA_LOOPBACK;

    Con_Popup(true);
    SCR_UpdateScreen();

    // parse the first message just read
    CL_ParseServerMessage();

    // read and parse messages util `precache' command
    while (cls.state == ca_connected) {
        Cbuf_Execute(&cl_cmdbuf);
        parse_next_message(0);
    }
}

static void CL_Demo_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        FS_File_g("demos", "*.dm2;*.dm2.gz;*.mvd2;*.mvd2.gz", FS_SEARCH_SAVEPATH | FS_SEARCH_BYFILTER, ctx);
    }
}

#define MIN_SNAPSHOTS   64
#define MAX_SNAPSHOTS   250000000

/*
====================
CL_EmitDemoSnapshot

Periodically builds a fake demo packet used to reconstruct delta compression
state, configstrings and layouts at the given server frame.
====================
*/
void CL_EmitDemoSnapshot(void)
{
    demosnap_t *snap;
    int64_t pos;
    char *from, *to;
    size_t len;
    server_frame_t *lastframe, *frame;
    int i, j, lastnum;

    if (cl_demosnaps->integer <= 0)
        return;

    if (cls.demo.frames_read < cls.demo.last_snapshot + cl_demosnaps->integer * 10)
        return;

    if (cls.demo.numsnapshots >= MAX_SNAPSHOTS)
        return;

    if (!cl.frame.valid)
        return;

    if (!cls.demo.file_size)
        return;

    pos = FS_Tell(cls.demo.playback);
    if (pos < cls.demo.file_offset)
        return;

    // write all the backups, since we can't predict what frame the next
    // delta will come from
    lastframe = NULL;
    lastnum = -1;
    for (i = 0; i < UPDATE_BACKUP; i++) {
        j = cl.frame.number - (UPDATE_BACKUP - 1) + i;
        frame = &cl.frames[j & UPDATE_MASK];
        if (frame->number != j || !frame->valid ||
            cl.numEntityStates - frame->firstEntity > MAX_PARSE_ENTITIES) {
            continue;
        }

        emit_delta_frame(lastframe, frame, lastnum, j);
        lastframe = frame;
        lastnum = frame->number;
    }

    // write configstrings
    for (i = 0; i < cl.csr.end; i++) {
        from = cl.baseconfigstrings[i];
        to = cl.configstrings[i];

        if (!strcmp(from, to))
            continue;

        len = Q_strnlen(to, MAX_QPATH);
        MSG_WriteByte(svc_configstring);
        MSG_WriteShort(i);
        MSG_WriteData(to, len);
        MSG_WriteByte(0);
    }

    // write layout
    MSG_WriteByte(svc_layout);
    MSG_WriteString(cl.layout);

    snap = Z_Malloc(sizeof(*snap) + msg_write.cursize - 1);
    snap->framenum = cls.demo.frames_read;
    snap->filepos = pos;
    snap->msglen = msg_write.cursize;
    memcpy(snap->data, msg_write.data, msg_write.cursize);

    cls.demo.snapshots = Z_Realloc(cls.demo.snapshots, sizeof(snap) * ALIGN(cls.demo.numsnapshots + 1, MIN_SNAPSHOTS));
    cls.demo.snapshots[cls.demo.numsnapshots++] = snap;

    Com_DPrintf("[%d] snaplen %zu\n", cls.demo.frames_read, msg_write.cursize);

    SZ_Clear(&msg_write);

    cls.demo.last_snapshot = cls.demo.frames_read;
}

static demosnap_t *find_snapshot(int64_t dest, bool byte_seek)
{
    int l = 0;
    int r = cls.demo.numsnapshots - 1;

    if (r < 0)
        return NULL;

    do {
        int m = (l + r) / 2;
        demosnap_t *snap = cls.demo.snapshots[m];
        int64_t pos = byte_seek ? snap->filepos : snap->framenum;
        if (pos < dest)
            l = m + 1;
        else if (pos > dest)
            r = m - 1;
        else
            return snap;
    } while (l <= r);

    return cls.demo.snapshots[max(r, 0)];
}

/*
====================
CL_FirstDemoFrame

Called after the first valid frame is parsed from the demo.
====================
*/
void CL_FirstDemoFrame(void)
{
    int64_t len, ofs;

    Com_DPrintf("[%d] first frame\n", cl.frame.number);

    // save base configstrings
    memcpy(cl.baseconfigstrings, cl.configstrings, sizeof(cl.baseconfigstrings[0]) * cl.csr.end);

    // obtain file length and offset of the second frame
    len = FS_Length(cls.demo.playback);
    ofs = FS_Tell(cls.demo.playback);
    if (ofs > 0 && ofs < len) {
        cls.demo.file_offset = ofs;
        cls.demo.file_size = len - ofs;
    }

    // begin timedemo
    if (com_timedemo->integer) {
        if(cls.timedemo.runs_total == 0) {
            cls.timedemo.runs_total = com_timedemo->integer;
            cls.timedemo.run_current = 0;
            cls.timedemo.results = Z_Malloc(cls.timedemo.runs_total * sizeof(unsigned));
        }

        cls.demo.time_frames = 0;
        cls.demo.time_start = Sys_Milliseconds();
    }

    // force initial snapshot
    cls.demo.last_snapshot = INT_MIN;
}

/*
====================
CL_FreeDemoSnapshots
====================
*/
void CL_FreeDemoSnapshots(void)
{
    for (int i = 0; i < cls.demo.numsnapshots; i++)
        Z_Free(cls.demo.snapshots[i]);
    cls.demo.numsnapshots = 0;

    Z_Freep((void**)&cls.demo.snapshots);
}

/*
====================
CL_Seek_f
====================
*/
static void CL_Seek_f(void)
{
    demosnap_t *snap;
    int i, j, ret, index, frames, prev;
    int64_t dest;
    bool byte_seek, back_seek;
    char *from, *to;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s [+-]<timespec|percent>[%%]\n", Cmd_Argv(0));
        return;
    }

#if USE_MVD_CLIENT
    if (sv_running->integer == ss_broadcast) {
        Cbuf_InsertText(&cmd_buffer, va("mvdseek \"%s\" @@\n", Cmd_Argv(1)));
        return;
    }
#endif

    if (!cls.demo.playback) {
        Com_Printf("Not playing a demo.\n");
        return;
    }

    to = Cmd_Argv(1);

    if (strchr(to, '%')) {
        char *suf;
        float percent = strtof(to, &suf);
        if (suf == to || strcmp(suf, "%") || !isfinite(percent)) {
            Com_Printf("Invalid percentage.\n");
            return;
        }

        if (!cls.demo.file_size) {
            Com_Printf("Unknown file size, can't seek.\n");
            return;
        }

        percent = Q_clipf(percent, 0, 100);
        dest = cls.demo.file_offset + cls.demo.file_size * percent / 100;

        byte_seek = true;
        back_seek = dest < FS_Tell(cls.demo.playback);
    } else {
        if (*to == '-' || *to == '+') {
            // relative to current frame
            if (!Com_ParseTimespec(to + 1, &frames)) {
                Com_Printf("Invalid relative timespec.\n");
                return;
            }
            if (*to == '-')
                frames = -frames;
            dest = cls.demo.frames_read + frames;
        } else {
            // relative to first frame
            if (!Com_ParseTimespec(to, &i)) {
                Com_Printf("Invalid absolute timespec.\n");
                return;
            }
            dest = i;
            frames = i - cls.demo.frames_read;
        }

        if (!frames)
            return; // already there

        byte_seek = false;
        back_seek = frames < 0;
    }

    if (!back_seek && cls.demo.eof && cl_demowait->integer)
        return; // already at end

    // disable effects processing
    cls.demo.seeking = true;

    // clear dirty configstrings
    memset(cl.dcs, 0, sizeof(cl.dcs));

    // stop sounds
    S_StopAllSounds();

    // save previous server frame number
    prev = cl.frame.number;

    Com_DPrintf("[%d] seeking to %"PRId64"\n", cls.demo.frames_read, dest);

    // seek to the previous most recent snapshot
    if (back_seek || cls.demo.last_snapshot > cls.demo.frames_read) {
        snap = find_snapshot(dest, byte_seek);

        if (snap) {
            Com_DPrintf("found snap at %d\n", snap->framenum);
            ret = FS_Seek(cls.demo.playback, snap->filepos, SEEK_SET);
            if (ret < 0) {
                Com_EPrintf("Couldn't seek demo: %s\n", Q_ErrorString(ret));
                goto done;
            }

            // clear end-of-file flag
            cls.demo.eof = false;

            // reset configstrings
            for (i = 0; i < cl.csr.end; i++) {
                from = cl.baseconfigstrings[i];
                to = cl.configstrings[i];

                if (!strcmp(from, to))
                    continue;

                Q_SetBit(cl.dcs, i);
                strcpy(to, from);
            }

            SZ_Init(&msg_read, snap->data, snap->msglen);
            msg_read.cursize = snap->msglen;

            CL_SeekDemoMessage();
            cls.demo.frames_read = snap->framenum;
            Com_DPrintf("[%d] after snap parse %d\n", cls.demo.frames_read, cl.frame.number);
        } else if (back_seek) {
            Com_Printf("Couldn't seek backwards without snapshots!\n");
            goto done;
        }
    }

    // skip forward to destination frame/position
    while (1) {
        int64_t pos = byte_seek ? FS_Tell(cls.demo.playback) : cls.demo.frames_read;
        if (pos >= dest)
            break;

        ret = read_next_message(cls.demo.playback);
        if (ret == 0 && cl_demowait->integer) {
            cls.demo.eof = true;
            break;
        }
        if (ret <= 0) {
            finish_demo(ret);
            return;
        }

        if (CL_SeekDemoMessage())
            goto done;
        CL_EmitDemoSnapshot();
    }

    Com_DPrintf("[%d] after skip %d\n", cls.demo.frames_read, cl.frame.number);

    // update dirty configstrings
    for (i = 0; i < CS_BITMAP_LONGS; i++) {
        if (((uint32_t *)cl.dcs)[i] == 0)
            continue;

        index = i << 5;
        for (j = 0; j < 32; j++, index++) {
            if (Q_IsBitSet(cl.dcs, index))
                CL_UpdateConfigstring(index);
        }
    }

    // don't lerp to old
    memset(&cl.oldframe, 0, sizeof(cl.oldframe));
#if USE_FPS
    memset(&cl.oldkeyframe, 0, sizeof(cl.oldkeyframe));
#endif

    // clear old effects
    CL_ClearEffects();
    CL_ClearTEnts();

    // fix time delta
    cl.serverdelta += cl.frame.number - prev;

    // fire up destination frame
    CL_DeltaFrame();

    if (cls.demo.recording && !cls.demo.paused)
        resume_record();

    update_status();

    cl.frameflags = 0;

done:
    cls.demo.seeking = false;
}

static void parse_info_string(demoInfo_t *info, int clientNum, int index, const cs_remap_t *csr)
{
    char string[MAX_QPATH], *p;

    MSG_ReadString(string, sizeof(string));

    if (index >= csr->playerskins && index < csr->playerskins + MAX_CLIENTS) {
        if (index - csr->playerskins == clientNum) {
            Q_strlcpy(info->pov, string, sizeof(info->pov));
            p = strchr(info->pov, '\\');
            if (p) {
                *p = 0;
            }
        }
    } else if (index == csr->models + 1) {
        Com_ParseMapName(info->map, string, sizeof(info->map));
    }
}

/*
====================
CL_GetDemoInfo
====================
*/
demoInfo_t *CL_GetDemoInfo(const char *path, demoInfo_t *info)
{
    qhandle_t f;
    int c, index, clientNum, type;
    const cs_remap_t *csr = &cs_remap_old;

    FS_OpenFile(path, &f, FS_MODE_READ | FS_FLAG_GZIP);
    if (!f) {
        return NULL;
    }

    type = read_first_message(f);
    if (type < 0) {
        goto fail;
    }

    info->mvd = type;

    if (type == 0) {
        if (MSG_ReadByte() != svc_serverdata) {
            goto fail;
        }
        c = MSG_ReadLong();
        if (c == PROTOCOL_VERSION_EXTENDED) {
            csr = &cs_remap_new;
        } else if (c < PROTOCOL_VERSION_OLD || c > PROTOCOL_VERSION_DEFAULT) {
            goto fail;
        }
        MSG_ReadLong();
        MSG_ReadByte();
        MSG_ReadString(NULL, 0);
        clientNum = MSG_ReadShort();
        MSG_ReadString(NULL, 0);

        while (1) {
            c = MSG_ReadByte();
            if (c == -1) {
                if (read_next_message(f) <= 0) {
                    break;
                }
                continue; // parse new message
            }
            if (c != svc_configstring) {
                break;
            }
            index = MSG_ReadWord();
            if (index < 0 || index >= csr->end) {
                goto fail;
            }
            parse_info_string(info, clientNum, index, csr);
        }
    } else {
        c = MSG_ReadByte();
        if ((c & SVCMD_MASK) != mvd_serverdata) {
            goto fail;
        }
        if (MSG_ReadLong() != PROTOCOL_VERSION_MVD) {
            goto fail;
        }
        if (c & (MVF_EXTLIMITS << SVCMD_BITS)) {
            csr = &cs_remap_new;
        }
        MSG_ReadWord();
        MSG_ReadLong();
        MSG_ReadString(NULL, 0);
        clientNum = MSG_ReadShort();

        while (1) {
            index = MSG_ReadWord();
            if (index == csr->end) {
                break;
            }
            if (index < 0 || index >= csr->end) {
                goto fail;
            }
            parse_info_string(info, clientNum, index, csr);
        }
    }

    FS_CloseFile(f);
    return info;

fail:
    FS_CloseFile(f);
    return NULL;
}

// =========================================================================

void CL_CleanupDemos(void)
{
    if (cls.demo.recording) {
        CL_Stop_f();
    }

    if (cls.demo.playback) {
        FS_CloseFile(cls.demo.playback);

        if (com_timedemo->integer && cls.demo.time_frames) {
            unsigned msec = Sys_Milliseconds();

            if (msec > cls.demo.time_start) {
                cls.timedemo.results[cls.timedemo.run_current] = msec - cls.demo.time_start;

                cls.timedemo.run_current++;
                if (cls.timedemo.run_current >= cls.timedemo.runs_total) {
                    // Print timedemo results
                    for (int i = 0; i < cls.timedemo.runs_total; i++) {
                        float sec = cls.timedemo.results[i] * 0.001f;
                        float fps = cls.demo.time_frames / sec;

                        Com_Printf("%u frames, %3.2f seconds: %f fps\n",
                                cls.demo.time_frames, sec, fps);
                    }
                    // Clean up
                    Z_Free(cls.timedemo.results);
                    memset(&cls.timedemo, 0, sizeof(cls.timedemo));
                } else {
                    // Restart demo
                    Cbuf_InsertText(&cmd_buffer, va("demo %s", cls.servername));
                }
            }
        }
    }

    CL_FreeDemoSnapshots();

    memset(&cls.demo, 0, sizeof(cls.demo));
}

/*
====================
CL_DemoFrame
====================
*/
void CL_DemoFrame(int msec)
{
    if (cls.state < ca_connected) {
        return;
    }

    if (cls.state != ca_active) {
        parse_next_message(0);
        return;
    }

    if (com_timedemo->integer) {
        parse_next_message(0);
        cl.time = cl.servertime;
        cls.demo.time_frames++;
        return;
    }

    // wait at the end of demo
    if (cls.demo.eof) {
        if (!cl_demowait->integer)
            finish_demo(0);
        return;
    }

    // cl.time has already been advanced for this client frame
    // read the next frame to start lerp cycle again
    while (cl.servertime < cl.time) {
        if (parse_next_message(cl_demowait->integer))
            break;
        if (cls.state != ca_active)
            break;
    }
}

static const cmdreg_t c_demo[] = {
    { "demo", CL_PlayDemo_f, CL_Demo_c },
    { "record", CL_Record_f, CL_Demo_c },
    { "stop", CL_Stop_f },
    { "suspend", CL_Suspend_f },
    { "resume", CL_Resume_f },
    { "seek", CL_Seek_f },

    { NULL }
};

/*
====================
CL_InitDemos
====================
*/
void CL_InitDemos(void)
{
    cl_demosnaps = Cvar_Get("cl_demosnaps", "10", 0);
    cl_demomsglen = Cvar_Get("cl_demomsglen", va("%d", MAX_PACKETLEN_WRITABLE_DEFAULT), 0);
    cl_demowait = Cvar_Get("cl_demowait", "0", 0);
    cl_demosuspendtoggle = Cvar_Get("cl_demosuspendtoggle", "1", 0);

    Cmd_Register(c_demo);
}


