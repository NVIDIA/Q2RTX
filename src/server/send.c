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
// sv_send.c

#include "server.h"

/*
=============================================================================

MISC

=============================================================================
*/

char sv_outputbuf[SV_OUTPUTBUF_LENGTH];

void SV_FlushRedirect(int redirected, char *outputbuf, size_t len)
{
    byte    buffer[MAX_PACKETLEN_DEFAULT];

    if (redirected == RD_PACKET) {
        memcpy(buffer, "\xff\xff\xff\xffprint\n", 10);
        memcpy(buffer + 10, outputbuf, len);
        NET_SendPacket(NS_SERVER, buffer, len + 10, &net_from);
    } else if (redirected == RD_CLIENT) {
        MSG_WriteByte(svc_print);
        MSG_WriteByte(PRINT_HIGH);
        MSG_WriteData(outputbuf, len);
        MSG_WriteByte(0);
        SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
    }
}

/*
=======================
SV_RateDrop

Returns true if the client is over its current
bandwidth estimation and should not be sent another packet
=======================
*/
static bool SV_RateDrop(client_t *client)
{
    size_t  total;
    int     i;

    // never drop over the loopback
    if (!client->rate) {
        return false;
    }

    total = 0;
    for (i = 0; i < RATE_MESSAGES; i++) {
        total += client->message_size[i];
    }

#if USE_FPS
    total = total * sv.framediv / client->framediv;
#endif

    if (total > client->rate) {
        SV_DPrintf(0, "Frame %d suppressed for %s (total = %zu)\n",
                   client->framenum, client->name, total);
        client->frameflags |= FF_SUPPRESSED;
        client->suppress_count++;
        client->message_size[client->framenum % RATE_MESSAGES] = 0;
        return true;
    }

    return false;
}

static void SV_CalcSendTime(client_t *client, size_t size)
{
    // never drop over the loopback
    if (!client->rate) {
        client->send_time = svs.realtime;
        client->send_delta = 0;
        return;
    }

    if (client->state == cs_spawned)
        client->message_size[client->framenum % RATE_MESSAGES] = size;

    client->send_time = svs.realtime;
    client->send_delta = size * 1000 / client->rate;
}

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/


/*
=================
SV_ClientPrintf

Sends text across to be displayed if the level passes.
NOT archived in MVD stream.
=================
*/
void SV_ClientPrintf(client_t *client, int level, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    size_t      len;

    if (level < client->messagelevel)
        return;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_print);
    MSG_WriteByte(level);
    MSG_WriteData(string, len + 1);

    SV_ClientAddMessage(client, MSG_RELIABLE | MSG_CLEAR);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients.
NOT archived in MVD stream.
=================
*/
void SV_BroadcastPrintf(int level, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    client_t    *client;
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_print);
    MSG_WriteByte(level);
    MSG_WriteData(string, len + 1);

    FOR_EACH_CLIENT(client) {
        if (client->state != cs_spawned)
            continue;
        if (level < client->messagelevel)
            continue;
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}

void SV_ClientCommand(client_t *client, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_stufftext);
    MSG_WriteData(string, len + 1);

    SV_ClientAddMessage(client, MSG_RELIABLE | MSG_CLEAR);
}

/*
=================
SV_BroadcastCommand

Sends command to all active clients.
NOT archived in MVD stream.
=================
*/
void SV_BroadcastCommand(const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    client_t    *client;
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_stufftext);
    MSG_WriteData(string, len + 1);

    FOR_EACH_CLIENT(client) {
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}


/*
=================
SV_Multicast

Sends the contents of the write buffer to a subset of the clients,
then clears the write buffer.

Archived in MVD stream.

MULTICAST_ALL    same as broadcast (origin can be NULL)
MULTICAST_PVS    send to clients potentially visible from org
MULTICAST_PHS    send to clients potentially hearable from org
=================
*/
void SV_Multicast(const vec3_t origin, multicast_t to)
{
    client_t    *client;
    byte        mask[VIS_MAX_BYTES];
    mleaf_t     *leaf1 = NULL, *leaf2;
    int         leafnum q_unused = 0;
    int         flags = 0;

    if (!sv.cm.cache) {
        Com_Error(ERR_DROP, "%s: no map loaded", __func__);
    }

    switch (to) {
    case MULTICAST_ALL_R:
        flags |= MSG_RELIABLE;
        // intentional fallthrough
    case MULTICAST_ALL:
        break;
    case MULTICAST_PHS_R:
        flags |= MSG_RELIABLE;
        // intentional fallthrough
    case MULTICAST_PHS:
        leaf1 = CM_PointLeaf(&sv.cm, origin);
        leafnum = leaf1 - sv.cm.cache->leafs;
        BSP_ClusterVis(sv.cm.cache, mask, leaf1->cluster, DVIS_PHS);
        break;
    case MULTICAST_PVS_R:
        flags |= MSG_RELIABLE;
        // intentional fallthrough
    case MULTICAST_PVS:
        leaf1 = CM_PointLeaf(&sv.cm, origin);
        leafnum = leaf1 - sv.cm.cache->leafs;
        BSP_ClusterVis(sv.cm.cache, mask, leaf1->cluster, DVIS_PVS2);
        break;
    default:
        Com_Error(ERR_DROP, "SV_Multicast: bad to: %i", to);
    }

    // send the data to all relevent clients
    FOR_EACH_CLIENT(client) {
        if (client->state < cs_primed) {
            continue;
        }
        // do not send unreliables to connecting clients
        if (!(flags & MSG_RELIABLE) && !CLIENT_ACTIVE(client)) {
            continue;
        }

        if (leaf1) {
            leaf2 = CM_PointLeaf(&sv.cm, client->edict->s.origin);
            if (!CM_AreasConnected(&sv.cm, leaf1->area, leaf2->area))
                continue;
            if (leaf2->cluster == -1)
                continue;
            if (!Q_IsBitSet(mask, leaf2->cluster))
                continue;
        }

        SV_ClientAddMessage(client, flags);
    }

    // add to MVD datagram
    SV_MvdMulticast(leafnum, to);

    // clear the buffer
    SZ_Clear(&msg_write);
}

#if USE_ZLIB
static size_t max_compressed_len(client_t *client)
{
    if (client->netchan->type == NETCHAN_NEW)
        return MAX_MSGLEN - ZPACKET_HEADER;

    return client->netchan->maxpacketlen - ZPACKET_HEADER;
}

static bool can_compress_message(client_t *client)
{
    if (!client->has_zlib)
        return false;

    // older clients have problems seamlessly writing svc_zpackets
    if (client->settings[CLS_RECORDING]) {
        if (client->protocol != PROTOCOL_VERSION_Q2PRO)
            return false;
        if (client->version < PROTOCOL_VERSION_Q2PRO_EXTENDED_LAYOUT)
            return false;
    }

    // compress only sufficiently large layouts
    if (msg_write.cursize < client->netchan->maxpacketlen / 2)
        return false;

    return true;
}

static bool compress_message(client_t *client, int flags)
{
    byte    buffer[MAX_MSGLEN];
    int     ret, len;

    if (!client->has_zlib)
        return false;

    svs.z.next_in = msg_write.data;
    svs.z.avail_in = msg_write.cursize;
    svs.z.next_out = buffer + ZPACKET_HEADER;
    svs.z.avail_out = max_compressed_len(client);

    ret = deflate(&svs.z, Z_FINISH);
    len = svs.z.total_out;

    // prepare for next deflate() or deflateBound()
    deflateReset(&svs.z);

    if (ret != Z_STREAM_END) {
        Com_WPrintf("Error %d compressing %zu bytes message for %s\n",
                    ret, msg_write.cursize, client->name);
        return false;
    }

    buffer[0] = svc_zpacket;
    buffer[1] = len & 255;
    buffer[2] = (len >> 8) & 255;
    buffer[3] = msg_write.cursize & 255;
    buffer[4] = (msg_write.cursize >> 8) & 255;

    len += ZPACKET_HEADER;

    SV_DPrintf(0, "%s: comp: %zu into %d\n",
               client->name, msg_write.cursize, len);

    // did it compress good enough?
    if (len >= msg_write.cursize)
        return false;

    client->AddMessage(client, buffer, len, flags & MSG_RELIABLE);
    return true;
}
#else
#define can_compress_message(client)    false
#define compress_message(client, flags) false
#endif

/*
=======================
SV_ClientAddMessage

Adds contents of the current write buffer to client's message list.
Does NOT clean the buffer for multicast delivery purpose,
unless told otherwise.
=======================
*/
void SV_ClientAddMessage(client_t *client, int flags)
{
    SV_DPrintf(1, "Added %sreliable message to %s: %zu bytes\n",
               (flags & MSG_RELIABLE) ? "" : "un", client->name, msg_write.cursize);

    if (!msg_write.cursize) {
        return;
    }

    if ((flags & MSG_COMPRESS_AUTO) && can_compress_message(client)) {
        flags |= MSG_COMPRESS;
    }

    if (!(flags & MSG_COMPRESS) || !compress_message(client, flags)) {
        client->AddMessage(client, msg_write.data, msg_write.cursize, flags & MSG_RELIABLE);
    }

    if (flags & MSG_CLEAR) {
        SZ_Clear(&msg_write);
    }
}

/*
===============================================================================

FRAME UPDATES - COMMON

===============================================================================
*/

static inline void free_msg_packet(client_t *client, message_packet_t *msg)
{
    List_Remove(&msg->entry);

    if (msg->cursize > MSG_TRESHOLD) {
        Q_assert(msg->cursize <= client->msg_dynamic_bytes);
        client->msg_dynamic_bytes -= msg->cursize;
        Z_Free(msg);
    } else {
        List_Insert(&client->msg_free_list, &msg->entry);
    }
}

#define FOR_EACH_MSG_SAFE(list) \
    LIST_FOR_EACH_SAFE(message_packet_t, msg, next, list, entry)
#define MSG_FIRST(list) \
    LIST_FIRST(message_packet_t, list, entry)

static void free_all_messages(client_t *client)
{
    message_packet_t *msg, *next;

    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        free_msg_packet(client, msg);
    }
    FOR_EACH_MSG_SAFE(&client->msg_reliable_list) {
        free_msg_packet(client, msg);
    }
    client->msg_unreliable_bytes = 0;
    client->msg_dynamic_bytes = 0;
}

static void add_msg_packet(client_t     *client,
                           byte         *data,
                           size_t       len,
                           bool         reliable)
{
    message_packet_t    *msg;

    if (!client->msg_pool) {
        return; // already dropped
    }

    Q_assert(len <= MAX_MSGLEN);

    if (len > MSG_TRESHOLD) {
        if (client->msg_dynamic_bytes > MAX_MSGLEN - len) {
            Com_WPrintf("%s: %s: out of dynamic memory\n",
                        __func__, client->name);
            goto overflowed;
        }
        msg = SV_Malloc(sizeof(*msg) + len - MSG_TRESHOLD);
        client->msg_dynamic_bytes += len;
    } else {
        if (LIST_EMPTY(&client->msg_free_list)) {
            Com_WPrintf("%s: %s: out of message slots\n",
                        __func__, client->name);
            goto overflowed;
        }
        msg = MSG_FIRST(&client->msg_free_list);
        List_Remove(&msg->entry);
    }

    memcpy(msg->data, data, len);
    msg->cursize = (uint16_t)len;

    if (reliable) {
        List_Append(&client->msg_reliable_list, &msg->entry);
    } else {
        List_Append(&client->msg_unreliable_list, &msg->entry);
        client->msg_unreliable_bytes += len;
    }

    return;

overflowed:
    if (reliable) {
        free_all_messages(client);
        SV_DropClient(client, "reliable queue overflowed");
    }
}

// check if this entity is present in current client frame
static bool check_entity(client_t *client, int entnum)
{
    client_frame_t *frame;
    int i, j;

    frame = &client->frames[client->framenum & UPDATE_MASK];

    for (i = 0; i < frame->num_entities; i++) {
        j = (frame->first_entity + i) % svs.num_entities;
        if (svs.entities[j].number == entnum) {
            return true;
        }
    }

    return false;
}

// sounds reliative to entities are handled specially
static void emit_snd(client_t *client, message_packet_t *msg)
{
    int flags, entnum;
    int i;

    entnum = msg->sendchan >> 3;
    flags = msg->flags;

    // check if position needs to be explicitly sent
    if (!(flags & SND_POS) && !check_entity(client, entnum)) {
        SV_DPrintf(0, "Forcing position on entity %d for %s\n",
                   entnum, client->name);
        flags |= SND_POS;   // entity is not present in frame
    }

    MSG_WriteByte(svc_sound);
    MSG_WriteByte(flags);
    MSG_WriteByte(msg->index);

    if (flags & SND_VOLUME)
        MSG_WriteByte(msg->volume);
    if (flags & SND_ATTENUATION)
        MSG_WriteByte(msg->attenuation);
    if (flags & SND_OFFSET)
        MSG_WriteByte(msg->timeofs);

    MSG_WriteShort(msg->sendchan);

    if (flags & SND_POS) {
        for (i = 0; i < 3; i++) {
            MSG_WriteShort(msg->pos[i]);
        }
    }
}

static inline void write_snd(client_t *client, message_packet_t *msg, size_t maxsize)
{
    // if this msg fits, write it
    if (msg_write.cursize + MAX_SOUND_PACKET <= maxsize) {
        emit_snd(client, msg);
    }
    List_Remove(&msg->entry);
    List_Insert(&client->msg_free_list, &msg->entry);
}

static inline void write_msg(client_t *client, message_packet_t *msg, size_t maxsize)
{
    // if this msg fits, write it
    if (msg_write.cursize + msg->cursize <= maxsize) {
        MSG_WriteData(msg->data, msg->cursize);
    }
    free_msg_packet(client, msg);
}

static inline void write_unreliables(client_t *client, size_t maxsize)
{
    message_packet_t    *msg, *next;

    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (msg->cursize) {
            write_msg(client, msg, maxsize);
        } else {
            write_snd(client, msg, maxsize);
        }
    }
}

/*
===============================================================================

FRAME UPDATES - OLD NETCHAN

===============================================================================
*/

static void add_message_old(client_t *client, byte *data,
                            size_t len, bool reliable)
{
    if (len > client->netchan->maxpacketlen) {
        if (reliable) {
            SV_DropClient(client, "oversize reliable message");
        } else {
            Com_DPrintf("Dumped oversize unreliable for %s\n", client->name);
        }
        return;
    }

    add_msg_packet(client, data, len, reliable);
}

// this should be the only place data is ever written to netchan message for old clients
static void write_reliables_old(client_t *client, size_t maxsize)
{
    message_packet_t *msg, *next;
    int count;

    if (client->netchan->reliable_length) {
        SV_DPrintf(1, "%s to %s: unacked\n", __func__, client->name);
        return;    // there is still outgoing reliable message pending
    }

    // find at least one reliable message to send
    count = 0;
    FOR_EACH_MSG_SAFE(&client->msg_reliable_list) {
        // stop if this msg doesn't fit (reliables must be delivered in order)
        if (client->netchan->message.cursize + msg->cursize > maxsize) {
            if (!count) {
                // this should never happen
                Com_WPrintf("%s to %s: overflow on the first message\n",
                            __func__, client->name);
            }
            break;
        }

        SV_DPrintf(1, "%s to %s: writing msg %d: %d bytes\n",
                   __func__, client->name, count, msg->cursize);

        SZ_Write(&client->netchan->message, msg->data, msg->cursize);
        free_msg_packet(client, msg);
        count++;
    }
}

// unreliable portion doesn't fit, then throw out low priority effects
static void repack_unreliables(client_t *client, size_t maxsize)
{
    message_packet_t *msg, *next;

    if (msg_write.cursize + 4 > maxsize) {
        return;
    }

    // temp entities first
    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (!msg->cursize || msg->data[0] != svc_temp_entity) {
            continue;
        }
        // ignore some low-priority effects, these checks come from r1q2
        if (msg->data[1] == TE_BLOOD || msg->data[1] == TE_SPLASH ||
            msg->data[1] == TE_GUNSHOT || msg->data[1] == TE_BULLET_SPARKS ||
            msg->data[1] == TE_SHOTGUN) {
            continue;
        }
        write_msg(client, msg, maxsize);
    }

    if (msg_write.cursize + 4 > maxsize) {
        return;
    }

    // then entity sounds
    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (!msg->cursize) {
            write_snd(client, msg, maxsize);
        }
    }

    if (msg_write.cursize + 4 > maxsize) {
        return;
    }

    // then positioned sounds
    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (msg->cursize && msg->data[0] == svc_sound) {
            write_msg(client, msg, maxsize);
        }
    }

    if (msg_write.cursize + 4 > maxsize) {
        return;
    }

    // then everything else left
    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        if (msg->cursize) {
            write_msg(client, msg, maxsize);
        }
    }
}

static void write_datagram_old(client_t *client)
{
    message_packet_t *msg;
    size_t maxsize, cursize;

    // determine how much space is left for unreliable data
    maxsize = client->netchan->maxpacketlen;
    if (client->netchan->reliable_length) {
        // there is still unacked reliable message pending
        maxsize -= client->netchan->reliable_length;
    } else {
        // find at least one reliable message to send
        // and make sure to reserve space for it
        if (!LIST_EMPTY(&client->msg_reliable_list)) {
            msg = MSG_FIRST(&client->msg_reliable_list);
            maxsize -= msg->cursize;
        }
    }

    // send over all the relevant entity_state_t
    // and the player_state_t
    client->WriteFrame(client);
    if (msg_write.cursize > maxsize) {
        SV_DPrintf(0, "Frame %d overflowed for %s: %zu > %zu\n",
                   client->framenum, client->name, msg_write.cursize, maxsize);
        SZ_Clear(&msg_write);
    }

    // now write unreliable messages
    // it is necessary for this to be after the WriteFrame
    // so that entity references will be current
    if (msg_write.cursize + client->msg_unreliable_bytes > maxsize) {
        // throw out some low priority effects
        repack_unreliables(client, maxsize);
    } else {
        // all messages fit, write them in order
        write_unreliables(client, maxsize);
    }

    // write at least one reliable message
    write_reliables_old(client, client->netchan->maxpacketlen - msg_write.cursize);

    // send the datagram
    cursize = client->netchan->Transmit(client->netchan,
                                        msg_write.cursize,
                                        msg_write.data,
                                        client->numpackets);

    // record the size for rate estimation
    SV_CalcSendTime(client, cursize);

    // clear the write buffer
    SZ_Clear(&msg_write);
}

/*
===============================================================================

FRAME UPDATES - NEW NETCHAN

===============================================================================
*/

static void add_message_new(client_t *client, byte *data,
                            size_t len, bool reliable)
{
    if (reliable) {
        // don't packetize, netchan level will do fragmentation as needed
        SZ_Write(&client->netchan->message, data, len);
    } else {
        // still have to packetize, relative sounds need special processing
        add_msg_packet(client, data, len, false);
    }
}

static void write_datagram_new(client_t *client)
{
    size_t cursize;

    // send over all the relevant entity_state_t
    // and the player_state_t
    client->WriteFrame(client);

    if (msg_write.overflowed) {
        // should never really happen
        Com_WPrintf("Frame overflowed for %s\n", client->name);
        SZ_Clear(&msg_write);
    }

    // now write unreliable messages
    // for this client out to the message
    // it is necessary for this to be after the WriteFrame
    // so that entity references will be current
    if (msg_write.cursize + client->msg_unreliable_bytes > msg_write.maxsize) {
        Com_WPrintf("Dumping datagram for %s\n", client->name);
    } else {
        write_unreliables(client, msg_write.maxsize);
    }

#if USE_DEBUG
    if (sv_pad_packets->integer) {
        size_t pad = msg_write.cursize + sv_pad_packets->integer;

        if (pad > msg_write.maxsize) {
            pad = msg_write.maxsize;
        }
        for (; pad > 0; pad--) {
            MSG_WriteByte(svc_nop);
        }
    }
#endif

    // send the datagram
    cursize = client->netchan->Transmit(client->netchan,
                                        msg_write.cursize,
                                        msg_write.data,
                                        client->numpackets);

    // record the size for rate estimation
    SV_CalcSendTime(client, cursize);

    // clear the write buffer
    SZ_Clear(&msg_write);
}


/*
===============================================================================

COMMON STUFF

===============================================================================
*/

static void finish_frame(client_t *client)
{
    message_packet_t *msg, *next;

    FOR_EACH_MSG_SAFE(&client->msg_unreliable_list) {
        free_msg_packet(client, msg);
    }
    client->msg_unreliable_bytes = 0;
}

#if USE_DEBUG && USE_FPS
static void check_key_sync(client_t *client)
{
    int div = sv.framediv / client->framediv;
    int key1 = !(sv.framenum % sv.framediv);
    int key2 = !(client->framenum % div);

    if (key1 != key2) {
        Com_LPrintf(PRINT_DEVELOPER,
                    "[%d] frame %d for %s not synced (%d != %d)\n",
                    sv.framenum, client->framenum, client->name, key1, key2);
    }
}
#endif

/*
=======================
SV_SendClientMessages

Called each game frame, sends svc_frame messages to spawned clients only.
Clients in earlier connection state are handled in SV_SendAsyncPackets.
=======================
*/
void SV_SendClientMessages(void)
{
    client_t    *client;
    size_t      cursize;

    // send a message to each connected client
    FOR_EACH_CLIENT(client) {
        if (!CLIENT_ACTIVE(client))
            goto finish;

        if (!SV_CLIENTSYNC(client))
            continue;

#if USE_DEBUG && USE_FPS
        if (developer->integer)
            check_key_sync(client);
#endif

        // if the reliable message overflowed,
        // drop the client (should never happen)
        if (client->netchan->message.overflowed) {
            SZ_Clear(&client->netchan->message);
            SV_DropClient(client, "reliable message overflowed");
            goto finish;
        }

        // don't overrun bandwidth
        if (SV_RateDrop(client))
            goto advance;

        // don't write any frame data until all fragments are sent
        if (client->netchan->fragment_pending) {
            client->frameflags |= FF_SUPPRESSED;
            cursize = client->netchan->TransmitNextFragment(client->netchan);
            SV_CalcSendTime(client, cursize);
            goto advance;
        }

        // build the new frame and write it
        SV_BuildClientFrame(client);
        client->WriteDatagram(client);

advance:
        // advance for next frame
        client->framenum++;

finish:
        // clear all unreliable messages still left
        finish_frame(client);
    }
}

static void write_pending_download(client_t *client)
{
    sizebuf_t   *buf = &client->netchan->message;
    int         chunk;

    if (!client->download)
        return;

    if (!client->downloadpending)
        return;

    if (client->netchan->reliable_length)
        return;

    if (buf->cursize >= client->netchan->maxpacketlen - 4)
        return;

    chunk = min(client->downloadsize - client->downloadcount,
                client->netchan->maxpacketlen - buf->cursize - 4);

    client->downloadpending = false;
    client->downloadcount += chunk;

    SZ_WriteByte(buf, client->downloadcmd);
    SZ_WriteShort(buf, chunk);
    SZ_WriteByte(buf, client->downloadcount * 100 / client->downloadsize);
    SZ_Write(buf, client->download + client->downloadcount - chunk, chunk);

    if (client->downloadcount == client->downloadsize) {
        SV_CloseDownload(client);
        SV_AlignKeyFrames(client);
    }
}

/*
==================
SV_SendAsyncPackets

If the client is just connecting, it is pointless to wait another 100ms
before sending next command and/or reliable acknowledge, send it as soon
as client rate limit allows.

For spawned clients, this is not used, as we are forced to send svc_frame
packets synchronously with game DLL ticks.
==================
*/
void SV_SendAsyncPackets(void)
{
    bool        retransmit;
    client_t    *client;
    netchan_t   *netchan;
    size_t      cursize;

    FOR_EACH_CLIENT(client) {
        // don't overrun bandwidth
        if (svs.realtime - client->send_time < client->send_delta) {
            continue;
        }

        netchan = client->netchan;

        // make sure all fragments are transmitted first
        if (netchan->fragment_pending) {
            cursize = netchan->TransmitNextFragment(netchan);
            SV_DPrintf(0, "%s: frag: %zu\n", client->name, cursize);
            goto calctime;
        }

        // spawned clients are handled elsewhere
        if (CLIENT_ACTIVE(client) && !SV_PAUSED) {
            continue;
        }

        // see if it's time to resend a (possibly dropped) packet
        retransmit = (com_localTime - netchan->last_sent > 1000);

        // don't write new reliables if not yet acknowledged
        if (netchan->reliable_length && !retransmit && client->state != cs_zombie) {
            continue;
        }

        // just update reliable if needed
        if (netchan->type == NETCHAN_OLD) {
            write_reliables_old(client, netchan->maxpacketlen);
        }

        // now fill up remaining buffer space with download
        write_pending_download(client);

        if (netchan->message.cursize || netchan->reliable_ack_pending ||
            netchan->reliable_length || retransmit) {
            cursize = netchan->Transmit(netchan, 0, "", 1);
            SV_DPrintf(0, "%s: send: %zu\n", client->name, cursize);
calctime:
            SV_CalcSendTime(client, cursize);
        }
    }
}

void SV_InitClientSend(client_t *newcl)
{
    int i;

    List_Init(&newcl->msg_free_list);
    List_Init(&newcl->msg_unreliable_list);
    List_Init(&newcl->msg_reliable_list);

    newcl->msg_pool = SV_Malloc(sizeof(message_packet_t) * MSG_POOLSIZE);
    for (i = 0; i < MSG_POOLSIZE; i++) {
        List_Append(&newcl->msg_free_list, &newcl->msg_pool[i].entry);
    }

    // setup protocol
    if (newcl->netchan->type == NETCHAN_NEW) {
        newcl->AddMessage = add_message_new;
        newcl->WriteDatagram = write_datagram_new;
    } else {
        newcl->AddMessage = add_message_old;
        newcl->WriteDatagram = write_datagram_old;
    }
}

void SV_ShutdownClientSend(client_t *client)
{
    free_all_messages(client);

    Z_Free(client->msg_pool);
    client->msg_pool = NULL;

    List_Init(&client->msg_free_list);
}

