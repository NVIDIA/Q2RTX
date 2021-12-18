/*
Copyright (C) 2013 Andrey Nazarov

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
// gtv.c
//

#include "client.h"
#include "server/mvd/protocol.h"

static byte     gtv_recv_buffer[MAX_GTC_MSGLEN];
static byte     gtv_send_buffer[MAX_GTS_MSGLEN*2];

static byte     gtv_message_buffer[MAX_MSGLEN];

static void build_gamestate(void)
{
    centity_t *ent;
    int i;

    memset(cls.gtv.entities, 0, sizeof(cls.gtv.entities));

    // set base player states
    MSG_PackPlayer(&cls.gtv.ps, &cl.frame.ps);

    // set base entity states
    for (i = 1; i < MAX_EDICTS; i++) {
        ent = &cl_entities[i];

        if (ent->serverframe != cl.frame.number) {
            continue;
        }

        MSG_PackEntity(&cls.gtv.entities[i], &ent->current, false);
    }
}

static void emit_gamestate(void)
{
    char        *string;
    int         i, j;
    entity_packed_t *es;
    size_t      length;
    int         flags;

    // send the serverdata
    MSG_WriteByte(mvd_serverdata | (MVF_SINGLEPOV << SVCMD_BITS));
    MSG_WriteLong(PROTOCOL_VERSION_MVD);
    MSG_WriteShort(PROTOCOL_VERSION_MVD_CURRENT);
    MSG_WriteLong(cl.servercount);
    MSG_WriteString(cl.gamedir);
    MSG_WriteShort(-1);

    // send configstrings
    for (i = 0; i < MAX_CONFIGSTRINGS; i++) {
        string = cl.configstrings[i];
        if (!string[0]) {
            continue;
        }
        length = strlen(string);
        if (length > MAX_QPATH) {
            length = MAX_QPATH;
        }

        MSG_WriteShort(i);
        MSG_WriteData(string, length);
        MSG_WriteByte(0);
    }
    MSG_WriteShort(MAX_CONFIGSTRINGS);

    // send portal bits
    MSG_WriteByte(0);

    // send player state
    MSG_WriteDeltaPlayerstate_Packet(NULL, &cls.gtv.ps,
                                     cl.clientNum, MSG_PS_FORCE);
    MSG_WriteByte(CLIENTNUM_NONE);

    // send entity states
    for (i = 1, es = cls.gtv.entities + 1; i < MAX_EDICTS; i++, es++) {
        flags = MSG_ES_UMASK;
        if ((j = es->number) == 0) {
            flags |= MSG_ES_REMOVE;
        }
        es->number = i;
        MSG_WriteDeltaEntity(NULL, es, flags);
        es->number = j;
    }
    MSG_WriteShort(0);
}

void CL_GTV_EmitFrame(void)
{
    player_packed_t newps;
    entity_packed_t *oldes, newes;
    centity_t *ent;
    int i, flags;

    if (cls.gtv.state != ca_active)
        return;

    if (!CL_FRAMESYNC)
        return;

    if (!cl.frame.valid)
        return;

    MSG_WriteByte(mvd_frame);

    // send portal bits
    MSG_WriteByte(0);

    // send player state
    MSG_PackPlayer(&newps, &cl.frame.ps);

    MSG_WriteDeltaPlayerstate_Packet(&cls.gtv.ps, &newps,
                                     cl.clientNum, MSG_PS_FORCE);

    // shuffle current state to previous
    cls.gtv.ps = newps;

    MSG_WriteByte(CLIENTNUM_NONE);      // end of packetplayers

    // send entity states
    for (i = 1; i < MAX_EDICTS; i++) {
        oldes = &cls.gtv.entities[i];
        ent = &cl_entities[i];

        if (ent->serverframe != cl.frame.number) {
            if (oldes->number) {
                // the old entity isn't present in the new message
                MSG_WriteDeltaEntity(oldes, NULL, MSG_ES_FORCE);
                oldes->number = 0;
            }
            continue;
        }

        // calculate flags
        flags = MSG_ES_UMASK;

        if (!oldes->number) {
            // this is a new entity, send it from the last state
            flags |= MSG_ES_FORCE | MSG_ES_NEWENTITY;
        }

        // quantize
        MSG_PackEntity(&newes, &ent->current, false);

        MSG_WriteDeltaEntity(oldes, &newes, flags);

        // shuffle current state to previous
        *oldes = newes;
    }

    MSG_WriteShort(0);      // end of packetentities

    SZ_Write(&cls.gtv.message, msg_write.data, msg_write.cursize);
    SZ_Clear(&msg_write);
}

static void drop_client(const char *reason)
{
    if (reason)
        Com_Printf("MVD client [%s] dropped: %s\n",
                   NET_AdrToString(&cls.gtv.stream.address), reason);

    NET_UpdateStream(&cls.gtv.stream);

    NET_Sleep(0);

    NET_RunStream(&cls.gtv.stream);
    NET_RunStream(&cls.gtv.stream);

    NET_CloseStream(&cls.gtv.stream);
    cls.gtv.state = ca_disconnected;
}

static void write_stream(void *data, size_t len)
{
    if (cls.gtv.state <= ca_disconnected) {
        return;
    }

    if (FIFO_Write(&cls.gtv.stream.send, data, len) != len) {
        drop_client("overflowed");
    }
}

static void write_message(gtv_serverop_t op)
{
    byte header[3];
    size_t len = msg_write.cursize + 1;

    header[0] = len & 255;
    header[1] = (len >> 8) & 255;
    header[2] = op;
    write_stream(header, sizeof(header));

    write_stream(msg_write.data, msg_write.cursize);
}

void CL_GTV_WriteMessage(byte *data, size_t len)
{
    int bits;

    if (cls.gtv.state != ca_active)
        return;

    if (cls.state != ca_active)
        return;

    if (len == 0)
        return;

    switch (data[0]) {
    case svc_configstring:
        SZ_WriteByte(&cls.gtv.message, mvd_configstring);
        SZ_Write(&cls.gtv.message, data + 1, len - 1);
        break;
    case svc_print:
        SZ_WriteByte(&cls.gtv.message, mvd_print);
        SZ_Write(&cls.gtv.message, data + 1, len - 1);
        break;
    case svc_layout:
    case svc_stufftext:
        bits = ((len >> 8) & 7) << SVCMD_BITS;
        SZ_WriteByte(&cls.gtv.message, mvd_unicast | bits);
        SZ_WriteByte(&cls.gtv.message, len & 255);
        SZ_WriteByte(&cls.gtv.message, cl.clientNum);
        SZ_Write(&cls.gtv.message, data, len);
        break;
    default:
        bits = ((len >> 8) & 7) << SVCMD_BITS;
        SZ_WriteByte(&cls.gtv.message, mvd_multicast_all | bits);
        SZ_WriteByte(&cls.gtv.message, len & 255);
        SZ_Write(&cls.gtv.message, data, len);
        break;
    }
}

void CL_GTV_Resume(void)
{
    if (cls.gtv.state != ca_active)
        return;

    SZ_Init(&cls.gtv.message, gtv_message_buffer, sizeof(gtv_message_buffer));

    build_gamestate();
    emit_gamestate();
    write_message(GTS_STREAM_DATA);
    SZ_Clear(&msg_write);
}

void CL_GTV_Suspend(void)
{
    if (cls.gtv.state != ca_active)
        return;

    // send stream suspend marker
    write_message(GTS_STREAM_DATA);
}

void CL_GTV_Transmit(void)
{
    byte header[3];
    size_t total;

    if (cls.gtv.state != ca_active)
        return;

    if (cls.state != ca_active)
        return;

    if (!CL_FRAMESYNC)
        return;

    if (cls.gtv.message.overflowed) {
        Com_WPrintf("MVD message overflowed.\n");
        goto clear;
    }

    if (!cls.gtv.message.cursize)
        return;

    // build message header
    total = cls.gtv.message.cursize + 1;
    header[0] = total & 255;
    header[1] = (total >> 8) & 255;
    header[2] = GTS_STREAM_DATA;

    // send frame to client
    write_stream(header, sizeof(header));
    write_stream(cls.gtv.message.data, cls.gtv.message.cursize);
    NET_UpdateStream(&cls.gtv.stream);

clear:
    // clear datagram
    SZ_Clear(&cls.gtv.message);
}

static void parse_hello(void)
{
    int protocol;

    if (cls.gtv.state >= ca_precached) {
        drop_client("duplicated hello message");
        return;
    }

    protocol = MSG_ReadWord();
    if (protocol != GTV_PROTOCOL_VERSION) {
        write_message(GTS_BADREQUEST);
        drop_client("bad protocol version");
        return;
    }

    MSG_ReadLong();
    MSG_ReadLong();
    MSG_ReadString(NULL, 0);
    MSG_ReadString(NULL, 0);
    MSG_ReadString(NULL, 0);

    // authorize access
    if (!NET_IsLanAddress(&cls.gtv.stream.address)) {
        write_message(GTS_NOACCESS);
        drop_client("not authorized");
        return;
    }

    cls.gtv.state = ca_precached;

    // send hello
    MSG_WriteLong(0);
    write_message(GTS_HELLO);
    SZ_Clear(&msg_write);

    Com_Printf("Accepted MVD client [%s]\n",
               NET_AdrToString(&cls.gtv.stream.address));
}

static void parse_ping(void)
{
    if (cls.gtv.state < ca_precached) {
        return;
    }

    // send ping reply
    write_message(GTS_PONG);
}

static void parse_stream_start(void)
{
    if (cls.gtv.state != ca_precached) {
        drop_client("unexpected stream start message");
        return;
    }

    // skip maxbuf
    MSG_ReadShort();

    cls.gtv.state = ca_active;

    // tell the server we are recording
    CL_UpdateRecordingSetting();

    // send ack to client
    write_message(GTS_STREAM_START);

    // send gamestate if active
    if (cls.state == ca_active) {
        CL_GTV_Resume();
    } else {
        // send stream suspend marker
        write_message(GTS_STREAM_DATA);
    }
}

static void parse_stream_stop(void)
{
    if (cls.gtv.state != ca_active) {
        drop_client("unexpected stream stop message");
        return;
    }

    cls.gtv.state = ca_precached;

    // tell the server we finished recording
    CL_UpdateRecordingSetting();

    // send ack to client
    write_message(GTS_STREAM_STOP);
}

static bool parse_message(void)
{
    uint32_t magic;
    uint16_t msglen;
    int cmd;

    if (cls.gtv.state <= ca_disconnected) {
        return false;
    }

    // check magic
    if (cls.gtv.state < ca_connected) {
        if (!FIFO_TryRead(&cls.gtv.stream.recv, &magic, 4)) {
            return false;
        }
        if (magic != MVD_MAGIC) {
            drop_client("not a MVD/GTV stream");
            return false;
        }
        cls.gtv.state = ca_connected;

        // send it back
        write_stream(&magic, 4);
        return false;
    }

    // parse msglen
    if (!cls.gtv.msglen) {
        if (!FIFO_TryRead(&cls.gtv.stream.recv, &msglen, 2)) {
            return false;
        }
        msglen = LittleShort(msglen);
        if (!msglen) {
            drop_client("end of stream");
            return false;
        }
        if (msglen > MAX_GTC_MSGLEN) {
            drop_client("oversize message");
            return false;
        }
        cls.gtv.msglen = msglen;
    }

    // read this message
    if (!FIFO_ReadMessage(&cls.gtv.stream.recv, cls.gtv.msglen)) {
        return false;
    }

    cls.gtv.msglen = 0;

    cmd = MSG_ReadByte();
    switch (cmd) {
    case GTC_HELLO:
        parse_hello();
        break;
    case GTC_PING:
        parse_ping();
        break;
    case GTC_STREAM_START:
        parse_stream_start();
        break;
    case GTC_STREAM_STOP:
        parse_stream_stop();
        break;
    case GTC_STRINGCMD:
        break;
    default:
        drop_client("unknown command byte");
        return false;
    }

    if (msg_read.readcount > msg_read.cursize) {
        drop_client("read past end of message");
        return false;
    }

    return true;
}

void CL_GTV_Run(void)
{
    neterr_t ret;

    if (!cls.gtv.state)
        return;

    if (cls.gtv.state == ca_disconnected) {
        ret = NET_Accept(&cls.gtv.stream);
        if (ret != NET_OK)
            return;

        Com_DPrintf("TCP client [%s] accepted\n",
                    NET_AdrToString(&cls.gtv.stream.address));

        cls.gtv.state = ca_connecting;
        cls.gtv.stream.recv.data = gtv_recv_buffer;
        cls.gtv.stream.recv.size = sizeof(gtv_recv_buffer);
        cls.gtv.stream.send.data = gtv_send_buffer;
        cls.gtv.stream.send.size = sizeof(gtv_send_buffer);
    }

    ret = NET_RunStream(&cls.gtv.stream);
    switch (ret) {
    case NET_AGAIN:
        break;
    case NET_OK:
        // parse the message
        while (parse_message())
            ;
        NET_UpdateStream(&cls.gtv.stream);
        break;
    case NET_CLOSED:
        drop_client("EOF from client");
        break;
    case NET_ERROR:
        drop_client("connection reset by peer");
        break;
    }
}

static void CL_GTV_Start_f(void)
{
    neterr_t ret;

    if (cls.gtv.state) {
        Com_Printf("Client GTV already started.\n");
        return;
    }

    ret = NET_Listen(true);
    if (ret == NET_OK) {
        Com_Printf("Listening for GTV connections.\n");
        cls.gtv.state = ca_disconnected;
    } else if (ret == NET_ERROR) {
        Com_EPrintf("%s while opening client TCP port.\n", NET_ErrorString());
    } else {
        Com_EPrintf("Client TCP port already in use.\n");
    }
}

static void CL_GTV_Stop_f(void)
{
    if (!cls.gtv.state) {
        Com_Printf("Client GTV already stopped.\n");
        return;
    }

    NET_Listen(false);

    write_message(GTS_DISCONNECT);
    drop_client(NULL);

    memset(&cls.gtv, 0, sizeof(cls.gtv));
}

static void CL_GTV_Status_f(void)
{
    if (!cls.gtv.state) {
        Com_Printf("Client GTV not running.\n");
        return;
    }

    if (cls.gtv.state == ca_disconnected) {
        Com_Printf("Listening for GTV connections.\n");
        return;
    }

    Com_Printf("TCP client [%s] connected (state %d)\n",
               NET_AdrToString(&cls.gtv.stream.address), cls.gtv.state);
}

void CL_GTV_Init(void)
{
    Cmd_AddCommand("client_gtv_start", CL_GTV_Start_f);
    Cmd_AddCommand("client_gtv_stop", CL_GTV_Stop_f);
    Cmd_AddCommand("client_gtv_status", CL_GTV_Status_f);
}

void CL_GTV_Shutdown(void)
{
    if (cls.gtv.state)
        CL_GTV_Stop_f();
}
