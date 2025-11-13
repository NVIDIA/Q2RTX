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

#pragma once

#include "common/protocol.h"
#include "common/sizebuf.h"

#define MAX_PACKETENTITY_BYTES  64  // rough estimate

// entity and player states are pre-quantized before sending to make delta
// comparsion easier
typedef struct {
    uint16_t    number;
    int16_t     origin[3];
    int16_t     angles[3];
    int16_t     old_origin[3];
    uint16_t    modelindex;
    uint16_t    modelindex2;
    uint16_t    modelindex3;
    uint16_t    modelindex4;
    uint32_t    skinnum;
    uint32_t    effects;
    uint32_t    renderfx;
    uint32_t    solid;
    uint32_t    morefx;
    uint16_t    frame;
    uint16_t    sound;
    uint8_t     event;
    uint8_t     alpha;
    uint8_t     scale;
    uint8_t     loop_volume;
    uint8_t     loop_attenuation;
} entity_packed_t;

typedef struct {
    pmove_state_t   pmove;
    int16_t         viewangles[3];
    int8_t          viewoffset[3];
    int8_t          kick_angles[3];
    int8_t          gunangles[3];
    int8_t          gunoffset[3];
    uint16_t        gunindex;
    uint8_t         gunframe;
    uint8_t         blend[4];
    uint8_t         fov;
    uint8_t         rdflags;
    int16_t         stats[MAX_STATS];
} player_packed_t;

typedef enum {
    MSG_PS_IGNORE_GUNINDEX      = BIT(0),   // ignore gunindex
    MSG_PS_IGNORE_GUNFRAMES     = BIT(1),   // ignore gunframe/gunoffset/gunangles
    MSG_PS_IGNORE_BLEND         = BIT(2),   // ignore blend
    MSG_PS_IGNORE_VIEWANGLES    = BIT(3),   // ignore viewangles
    MSG_PS_IGNORE_DELTAANGLES   = BIT(4),   // ignore delta_angles
    MSG_PS_IGNORE_PREDICTION    = BIT(5),   // mutually exclusive with IGNORE_VIEWANGLES
    MSG_PS_EXTENSIONS           = BIT(6),   // enable protocol extensions
    MSG_PS_FORCE                = BIT(7),   // send even if unchanged (MVD stream only)
    MSG_PS_REMOVE               = BIT(8),   // player is removed (MVD stream only)
} msgPsFlags_t;

typedef enum {
    MSG_ES_FORCE        = BIT(0),   // send even if unchanged
    MSG_ES_NEWENTITY    = BIT(1),   // send old_origin
    MSG_ES_FIRSTPERSON  = BIT(2),   // ignore origin/angles
    MSG_ES_LONGSOLID    = BIT(3),   // higher precision bbox encoding
    MSG_ES_UMASK        = BIT(4),   // client has 16-bit mask MSB fix
    MSG_ES_BEAMORIGIN   = BIT(5),   // client has RF_BEAM old_origin fix
    MSG_ES_SHORTANGLES  = BIT(6),   // higher precision angles encoding
    MSG_ES_EXTENSIONS   = BIT(7),   // enable protocol extensions
    MSG_ES_REMOVE       = BIT(8),   // entity is removed (MVD stream only)
} msgEsFlags_t;

extern sizebuf_t    msg_write;
extern byte         msg_write_buffer[MAX_MSGLEN];

extern sizebuf_t    msg_read;
extern byte         msg_read_buffer[MAX_MSGLEN];

extern const entity_packed_t    nullEntityState;
extern const player_packed_t    nullPlayerState;
extern const usercmd_t          nullUserCmd;

void    MSG_Init(void);

void    MSG_BeginWriting(void);
void    MSG_WriteChar(int c);
void    MSG_WriteByte(int c);
void    MSG_WriteShort(int c);
void    MSG_WriteLong(int c);
void    MSG_WriteLong64(int64_t c);
void    MSG_WriteString(const char *s);
void    MSG_WritePos(const vec3_t pos);
void    MSG_WriteAngle(float f);
#if USE_CLIENT
void    MSG_FlushBits(void);
void    MSG_WriteBits(int value, int bits);
int     MSG_WriteDeltaUsercmd(const usercmd_t *from, const usercmd_t *cmd, int version);
int     MSG_WriteDeltaUsercmd_Enhanced(const usercmd_t *from, const usercmd_t *cmd);
#endif
void    MSG_WriteDir(const vec3_t vector);
void    MSG_PackEntity(entity_packed_t *out, const entity_state_t *in, const entity_state_extension_t *ext);
void    MSG_WriteDeltaEntity(const entity_packed_t *from, const entity_packed_t *to, msgEsFlags_t flags);
void    MSG_PackPlayer(player_packed_t *out, const player_state_t *in);
void    MSG_WriteDeltaPlayerstate_Default(const player_packed_t *from, const player_packed_t *to, msgPsFlags_t flags);
int     MSG_WriteDeltaPlayerstate_Enhanced(const player_packed_t *from, player_packed_t *to, msgPsFlags_t flags);
void    MSG_WriteDeltaPlayerstate_Packet(const player_packed_t *from, const player_packed_t *to, int number, msgPsFlags_t flags);

static inline void *MSG_WriteData(const void *data, size_t len)
{
    return memcpy(SZ_GetSpace(&msg_write, len), data, len);
}

static inline void MSG_FlushTo(sizebuf_t *buf)
{
    SZ_Write(buf, msg_write.data, msg_write.cursize);
    SZ_Clear(&msg_write);
}

void    MSG_BeginReading(void);
byte    *MSG_ReadData(size_t len);
int     MSG_ReadChar(void);
int     MSG_ReadByte(void);
int     MSG_ReadShort(void);
int     MSG_ReadWord(void);
int     MSG_ReadLong(void);
int64_t MSG_ReadLong64(void);
size_t  MSG_ReadString(char *dest, size_t size);
size_t  MSG_ReadStringLine(char *dest, size_t size);
#if USE_CLIENT
void    MSG_ReadPos(vec3_t pos);
void    MSG_ReadDir(vec3_t vector);
#endif
int     MSG_ReadBits(int bits);
void    MSG_ReadDeltaUsercmd(const usercmd_t *from, usercmd_t *cmd);
void    MSG_ReadDeltaUsercmd_Hacked(const usercmd_t *from, usercmd_t *to);
void    MSG_ReadDeltaUsercmd_Enhanced(const usercmd_t *from, usercmd_t *to);
int     MSG_ParseEntityBits(uint64_t *bits, msgEsFlags_t flags);
void    MSG_ParseDeltaEntity(entity_state_t *to, entity_state_extension_t *ext, int number, uint64_t bits, msgEsFlags_t flags);
#if USE_CLIENT
void    MSG_ParseDeltaPlayerstate_Default(const player_state_t *from, player_state_t *to, int flags, msgPsFlags_t psflags);
void    MSG_ParseDeltaPlayerstate_Enhanced(const player_state_t *from, player_state_t *to, int flags, int extraflags, msgPsFlags_t psflags);
#endif
void    MSG_ParseDeltaPlayerstate_Packet(const player_state_t *from, player_state_t *to, int flags, msgPsFlags_t psflags);

#if USE_DEBUG
#if USE_CLIENT
void    MSG_ShowDeltaPlayerstateBits_Default(int flags);
void    MSG_ShowDeltaPlayerstateBits_Enhanced(int flags, int extraflags);
void    MSG_ShowDeltaUsercmdBits_Enhanced(int bits);
#endif
#if USE_CLIENT || USE_MVD_CLIENT
void    MSG_ShowDeltaEntityBits(uint64_t bits);
void    MSG_ShowDeltaPlayerstateBits_Packet(int flags);
const char *MSG_ServerCommandString(int cmd);
#endif // USE_CLIENT || USE_MVD_CLIENT
#endif // USE_DEBUG


//============================================================================

/*
==================
MSG_PackSolid*

These functions assume x/y are equal (except *_Ver2) and symmetric. Z does not
have to be symmetric, and z maxs can be negative.
==================
*/
static inline int MSG_PackSolid16(const vec3_t mins, const vec3_t maxs)
{
    int x = maxs[0] / 8;
    int zd = -mins[2] / 8;
    int zu = (maxs[2] + 32) / 8;

    x = Q_clip(x, 1, 31);
    zd = Q_clip(zd, 1, 31);
    zu = Q_clip(zu, 1, 63);

    return (zu << 10) | (zd << 5) | x;
}

static inline uint32_t MSG_PackSolid32_Ver1(const vec3_t mins, const vec3_t maxs)
{
    int x = maxs[0];
    int zd = -mins[2];
    int zu = maxs[2] + 32768;

    x = Q_clip(x, 1, 255);
    zd = Q_clip_uint8(zd);
    zu = Q_clip_uint16(zu);

    return ((uint32_t)zu << 16) | (zd << 8) | x;
}

static inline uint32_t MSG_PackSolid32_Ver2(const vec3_t mins, const vec3_t maxs)
{
    int x = maxs[0];
    int y = maxs[1];
    int zd = -mins[2];
    int zu = maxs[2] + 32;

    x = Q_clip(x, 1, 255);
    y = Q_clip(y, 1, 255);
    zd = Q_clip_uint8(zd);
    zu = Q_clip_uint8(zu);

    return MakeLittleLong(x, y, zd, zu);
}

static inline void MSG_UnpackSolid16(int solid, vec3_t mins, vec3_t maxs)
{
    int x = 8 * (solid & 31);
    int zd = 8 * ((solid >> 5) & 31);
    int zu = 8 * ((solid >> 10) & 63) - 32;

    VectorSet(mins, -x, -x, -zd);
    VectorSet(maxs,  x,  x,  zu);
}

static inline void MSG_UnpackSolid32_Ver1(uint32_t solid, vec3_t mins, vec3_t maxs)
{
    int x = solid & 255;
    int zd = (solid >> 8) & 255;
    int zu = ((solid >> 16) & 65535) - 32768;

    VectorSet(mins, -x, -x, -zd);
    VectorSet(maxs,  x,  x,  zu);
}

static inline void MSG_UnpackSolid32_Ver2(uint32_t solid, vec3_t mins, vec3_t maxs)
{
    int x = solid & 255;
    int y = (solid >> 8) & 255;
    int zd = (solid >> 16) & 255;
    int zu = ((solid >> 24) & 255) - 32;

    VectorSet(mins, -x, -y, -zd);
    VectorSet(maxs,  x,  y,  zu);
}
