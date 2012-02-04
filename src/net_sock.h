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

// net.h -- quake's interface to the networking layer

#define PORT_ANY            -1
#define PORT_ANY_STRING     "-1"
#define PORT_MASTER         27900
#define PORT_SERVER         27910
#define PORT_SERVER_STRING  "27910"

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
    NA_BAD,
    NA_LOOPBACK,
    NA_BROADCAST,
    NA_IP
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
    uint8_t u8[4];
    uint16_t u16[2];
    uint32_t u32;
} netadrip_t;

typedef struct netadr_s {
    netadrtype_t type;
    netadrip_t ip;
    uint16_t port;
} netadr_t;

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
        if (a->ip.u32 == b->ip.u32 && a->port == b->port) {
            return qtrue;
        }
        // fall through
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
        if (a->ip.u32 == b->ip.u32) {
            return qtrue;
        }
        // fall through
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
        // fall through
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
qboolean    NET_GetAddress(netsrc_t sock, netadr_t *adr);

void        NET_UpdateStats(void);

qboolean    NET_GetPacket(netsrc_t sock);
qboolean    NET_SendPacket(netsrc_t sock, const void *data, size_t len, const netadr_t *to);
qboolean    NET_GetLoopPacket(netsrc_t sock);

char        *NET_AdrToString(const netadr_t *a);
qboolean    NET_StringToAdr(const char *s, netadr_t *a, int port);

const char  *NET_ErrorString(void);

ioentry_t   *NET_AddFd(qsocket_t fd);
void        NET_RemoveFd(qsocket_t fd);
int         NET_Sleep(int msec);
#if USE_AC_SERVER
int         NET_Sleepv(int msec, ...);
#endif

extern cvar_t       *net_ip;
extern cvar_t       *net_port;

extern netadr_t     net_from;

