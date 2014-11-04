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

#ifndef NET_H
#define NET_H

#include "common/fifo.h"

// net.h -- quake's interface to the networking layer

#define PORT_ANY            -1
#define PORT_MASTER         27900
#define PORT_SERVER         27910

#define MIN_PACKETLEN                   512     // don't allow smaller packets
#define MAX_PACKETLEN                   4096    // max length of a single packet
#define PACKET_HEADER                   10      // two ints and a short (worst case)
#define MAX_PACKETLEN_DEFAULT           1400    // default quake2 limit
#define MAX_PACKETLEN_WRITABLE          (MAX_PACKETLEN - PACKET_HEADER)
#define MAX_PACKETLEN_WRITABLE_DEFAULT  (MAX_PACKETLEN_DEFAULT - PACKET_HEADER)

// portable network error codes
#define NET_OK       0  // success
#define NET_ERROR   -1  // failure (NET_ErrorString returns error message)
#define NET_AGAIN   -2  // operation would block, try again
#define NET_CLOSED  -3  // peer has closed connection

typedef int neterr_t;

#ifdef _WIN32
typedef intptr_t qsocket_t;
#else
typedef int qsocket_t;
#endif

typedef struct {
#ifdef _WIN32
    qsocket_t fd;
#endif
    qboolean inuse: 1;
    qboolean canread: 1;
    qboolean canwrite: 1;
    qboolean canexcept: 1;
    qboolean wantread: 1;
    qboolean wantwrite: 1;
    qboolean wantexcept: 1;
} ioentry_t;

typedef enum {
    NA_UNSPECIFIED,
    NA_LOOPBACK,
    NA_BROADCAST,
    NA_IP,
    NA_IP6
} netadrtype_t;

typedef enum {
    NS_CLIENT,
    NS_SERVER,
    NS_COUNT
} netsrc_t;

typedef enum {
    NET_NONE    = 0,
    NET_CLIENT  = (1 << 0),
    NET_SERVER  = (1 << 1)
} netflag_t;

typedef union {
    uint8_t u8[16];
    uint16_t u16[8];
    uint32_t u32[4];
    uint64_t u64[2];
} netadrip_t;

typedef struct netadr_s {
    netadrtype_t type;
    netadrip_t ip;
    uint16_t port;
    uint32_t scope_id;  // IPv6 crap
} netadr_t;

typedef enum netstate_e {
    NS_DISCONNECTED,// no socket opened
    NS_CONNECTING,  // connect() not yet completed
    NS_CONNECTED,   // may transmit data
    NS_CLOSED,      // peer has preformed orderly shutdown
    NS_BROKEN       // fatal error has been signaled
} netstate_t;

typedef struct netstream_s {
    qsocket_t   socket;
    netadr_t    address;
    netstate_t  state;
    fifo_t      recv;
    fifo_t      send;
} netstream_t;

static inline qboolean NET_IsEqualAdr(const netadr_t *a, const netadr_t *b)
{
    if (a->type != b->type) {
        return qfalse;
    }

    switch (a->type) {
    case NA_LOOPBACK:
        return qtrue;
    case NA_IP:
    case NA_BROADCAST:
        if (a->ip.u32[0] == b->ip.u32[0] && a->port == b->port) {
            return qtrue;
        }
        return qfalse;
    case NA_IP6:
        if (memcmp(a->ip.u8, b->ip.u8, 16) == 0 && a->port == b->port) {
            return qtrue;
        }
        return qfalse;
    default:
        break;
    }

    return qfalse;
}

static inline qboolean NET_IsEqualBaseAdr(const netadr_t *a, const netadr_t *b)
{
    if (a->type != b->type) {
        return qfalse;
    }

    switch (a->type) {
    case NA_LOOPBACK:
        return qtrue;
    case NA_IP:
    case NA_BROADCAST:
        if (a->ip.u32[0] == b->ip.u32[0]) {
            return qtrue;
        }
        return qfalse;
    case NA_IP6:
        if (memcmp(a->ip.u8, b->ip.u8, 16) == 0) {
            return qtrue;
        }
        return qfalse;
    default:
        break;
    }

    return qfalse;
}

static inline qboolean NET_IsEqualBaseAdrMask(const netadr_t *a,
                                              const netadr_t *b,
                                              const netadr_t *m)
{
    if (a->type != b->type) {
        return qfalse;
    }

    switch (a->type) {
    case NA_IP:
        return !((a->ip.u32[0] ^ b->ip.u32[0]) & m->ip.u32[0]);
    case NA_IP6:
#if (defined __amd64__) || (defined _M_AMD64)
        return !(((a->ip.u64[0] ^ b->ip.u64[0]) & m->ip.u64[0]) |
                 ((a->ip.u64[1] ^ b->ip.u64[1]) & m->ip.u64[1]));
#else
        return !(((a->ip.u32[0] ^ b->ip.u32[0]) & m->ip.u32[0]) |
                 ((a->ip.u32[1] ^ b->ip.u32[1]) & m->ip.u32[1]) |
                 ((a->ip.u32[2] ^ b->ip.u32[2]) & m->ip.u32[2]) |
                 ((a->ip.u32[3] ^ b->ip.u32[3]) & m->ip.u32[3]));
#endif
    default:
        break;
    }

    return qfalse;
}

static inline qboolean NET_IsLanAddress(const netadr_t *adr)
{
    switch (adr->type) {
    case NA_LOOPBACK:
        return qtrue;
    case NA_IP:
    case NA_BROADCAST:
        if (adr->ip.u8[0] == 127 || adr->ip.u8[0] == 10) {
            return qtrue;
        }
        if (adr->ip.u16[0] == MakeRawShort(192, 168) ||
            adr->ip.u16[0] == MakeRawShort(172,  16)) {
            return qtrue;
        }
        return qfalse;
    case NA_IP6:
        if (adr->ip.u8[0] == 0xfe && (adr->ip.u8[1] & 0xc0) == 0x80) {
            return qtrue;
        }
        return qfalse;
    default:
        break;
    }

    return qfalse;
}

static inline qboolean NET_IsLocalAddress(const netadr_t *adr)
{
#if USE_CLIENT && USE_SERVER
    if (adr->type == NA_LOOPBACK)
        return qtrue;
#endif
    return qfalse;
}

void        NET_Init(void);
void        NET_Shutdown(void);
void        NET_Config(netflag_t flag);
void        NET_UpdateStats(void);

qboolean    NET_GetAddress(netsrc_t sock, netadr_t *adr);
void        NET_GetPackets(netsrc_t sock, void (*packet_cb)(void));
qboolean    NET_SendPacket(netsrc_t sock, const void *data,
                           size_t len, const netadr_t *to);

char        *NET_AdrToString(const netadr_t *a);
qboolean    NET_StringToAdr(const char *s, netadr_t *a, int default_port);
qboolean    NET_StringPairToAdr(const char *host, const char *port, netadr_t *a);

char        *NET_BaseAdrToString(const netadr_t *a);
#define     NET_StringToBaseAdr(s, a)   NET_StringPairToAdr(s, NULL, a)

const char  *NET_ErrorString(void);

void        NET_CloseStream(netstream_t *s);
neterr_t    NET_Listen(qboolean listen);
neterr_t    NET_Accept(netstream_t *s);
neterr_t    NET_Connect(const netadr_t *peer, netstream_t *s);
neterr_t    NET_RunConnect(netstream_t *s);
neterr_t    NET_RunStream(netstream_t *s);
void        NET_UpdateStream(netstream_t *s);

ioentry_t   *NET_AddFd(qsocket_t fd);
void        NET_RemoveFd(qsocket_t fd);
int         NET_Sleep(int msec);
#if USE_AC_SERVER
int         NET_Sleepv(int msec, ...);
#endif

extern cvar_t       *net_ip;
extern cvar_t       *net_port;

extern netadr_t     net_from;

#endif // NET_H
