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

#ifndef MSG_H
#define MSG_H

#include "common/protocol.h"
#include "common/sizebuf.h"

// entity and player states are pre-quantized before sending to make delta
// comparsion easier
typedef struct {
    uint16_t    number;
    int16_t     origin[3];
    int16_t     angles[3];
    int16_t     old_origin[3];
    uint8_t     modelindex;
    uint8_t     modelindex2;
    uint8_t     modelindex3;
    uint8_t     modelindex4;
    uint32_t    skinnum;
    uint32_t    effects;
    uint32_t    renderfx;
    uint32_t    solid;
    uint16_t    frame;
    uint8_t     sound;
    uint8_t     event;
} entity_packed_t;

typedef struct {
    pmove_state_t   pmove;
    int16_t         viewangles[3];
    int8_t          viewoffset[3];
    int8_t          kick_angles[3];
    int8_t          gunangles[3];
    int8_t          gunoffset[3];
    uint8_t         gunindex;
    uint8_t         gunframe;
    uint8_t         blend[4];
    uint8_t         fov;
    uint8_t         rdflags;
    int16_t         stats[MAX_STATS];
} player_packed_t;

typedef enum {
    MSG_PS_IGNORE_GUNINDEX      = (1 << 0),
    MSG_PS_IGNORE_GUNFRAMES     = (1 << 1),
    MSG_PS_IGNORE_BLEND         = (1 << 2),
    MSG_PS_IGNORE_VIEWANGLES    = (1 << 3),
    MSG_PS_IGNORE_DELTAANGLES   = (1 << 4),
    MSG_PS_IGNORE_PREDICTION    = (1 << 5),      // mutually exclusive with IGNORE_VIEWANGLES
    MSG_PS_FORCE                = (1 << 7),
    MSG_PS_REMOVE               = (1 << 8)
} msgPsFlags_t;

typedef enum {
    MSG_ES_FORCE        = (1 << 0),
    MSG_ES_NEWENTITY    = (1 << 1),
    MSG_ES_FIRSTPERSON  = (1 << 2),
    MSG_ES_LONGSOLID    = (1 << 3),
    MSG_ES_UMASK        = (1 << 4),
    MSG_ES_BEAMORIGIN   = (1 << 5),
    MSG_ES_SHORTANGLES  = (1 << 6),
    MSG_ES_REMOVE       = (1 << 7)
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
void    MSG_WriteString(const char *s);
void    MSG_WritePos(const vec3_t pos);
void    MSG_WriteAngle(float f);
#if USE_CLIENT
void    MSG_WriteBits(int value, int bits);
int     MSG_WriteDeltaUsercmd(const usercmd_t *from, const usercmd_t *cmd, int version);
int     MSG_WriteDeltaUsercmd_Enhanced(const usercmd_t *from, const usercmd_t *cmd, int version);
#endif
void    MSG_WriteDir(const vec3_t vector);
void    MSG_PackEntity(entity_packed_t *out, const entity_state_t *in, bool short_angles);
void    MSG_WriteDeltaEntity(const entity_packed_t *from, const entity_packed_t *to, msgEsFlags_t flags);
void    MSG_PackPlayer(player_packed_t *out, const player_state_t *in);
void    MSG_WriteDeltaPlayerstate_Default(const player_packed_t *from, const player_packed_t *to);
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
size_t  MSG_ReadString(char *dest, size_t size);
size_t  MSG_ReadStringLine(char *dest, size_t size);
#if USE_CLIENT
void    MSG_ReadPos(vec3_t pos);
void    MSG_ReadDir(vec3_t vector);
#endif
int     MSG_ReadBits(int bits);
void    MSG_ReadDeltaUsercmd(const usercmd_t *from, usercmd_t *cmd);
void    MSG_ReadDeltaUsercmd_Hacked(const usercmd_t *from, usercmd_t *to);
void    MSG_ReadDeltaUsercmd_Enhanced(const usercmd_t *from, usercmd_t *to, int version);
int     MSG_ParseEntityBits(int *bits);
void    MSG_ParseDeltaEntity(const entity_state_t *from, entity_state_t *to, int number, int bits, msgEsFlags_t flags);
#if USE_CLIENT
void    MSG_ParseDeltaPlayerstate_Default(const player_state_t *from, player_state_t *to, int flags);
void    MSG_ParseDeltaPlayerstate_Enhanced(const player_state_t *from, player_state_t *to, int flags, int extraflags);
#endif
void    MSG_ParseDeltaPlayerstate_Packet(const player_state_t *from, player_state_t *to, int flags);

#ifdef _DEBUG
#if USE_CLIENT
void    MSG_ShowDeltaPlayerstateBits_Default(int flags);
void    MSG_ShowDeltaPlayerstateBits_Enhanced(int flags, int extraflags);
void    MSG_ShowDeltaUsercmdBits_Enhanced(int bits);
#endif
#if USE_CLIENT || USE_MVD_CLIENT
void    MSG_ShowDeltaEntityBits(int bits);
void    MSG_ShowDeltaPlayerstateBits_Packet(int flags);
const char *MSG_ServerCommandString(int cmd);
#define MSG_ShowSVC(cmd) \
    Com_LPrintf(PRINT_DEVELOPER, "%3"PRIz":%s\n", msg_read.readcount - 1, \
        MSG_ServerCommandString(cmd))
#endif // USE_CLIENT || USE_MVD_CLIENT
#endif // _DEBUG


//============================================================================

static inline int MSG_PackSolid16(const vec3_t mins, const vec3_t maxs)
{
    int x, zd, zu;

    // assume that x/y are equal and symetric
    x = maxs[0] / 8;
    clamp(x, 1, 31);

    // z is not symetric
    zd = -mins[2] / 8;
    clamp(zd, 1, 31);

    // and z maxs can be negative...
    zu = (maxs[2] + 32) / 8;
    clamp(zu, 1, 63);

    return (zu << 10) | (zd << 5) | x;
}

static inline int MSG_PackSolid32(const vec3_t mins, const vec3_t maxs)
{
    int x, zd, zu;

    // assume that x/y are equal and symetric
    x = maxs[0];
    clamp(x, 1, 255);

    // z is not symetric
    zd = -mins[2];
    clamp(zd, 1, 255);

    // and z maxs can be negative...
    zu = maxs[2] + 32768;
    clamp(zu, 1, 65535);

    return ((unsigned)zu << 16) | (zd << 8) | x;
}

static inline void MSG_UnpackSolid16(int solid, vec3_t mins, vec3_t maxs)
{
    int x, zd, zu;

    x = 8 * (solid & 31);
    zd = 8 * ((solid >> 5) & 31);
    zu = 8 * ((solid >> 10) & 63) - 32;

    mins[0] = mins[1] = -x;
    maxs[0] = maxs[1] = x;
    mins[2] = -zd;
    maxs[2] = zu;
}

static inline void MSG_UnpackSolid32(int solid, vec3_t mins, vec3_t maxs)
{
    int x, zd, zu;

    x = solid & 255;
    zd = (solid >> 8) & 255;
    zu = ((solid >> 16) & 65535) - 32768;

    mins[0] = mins[1] = -x;
    maxs[0] = maxs[1] = x;
    mins[2] = -zd;
    maxs[2] = zu;
}

#endif // MSG_H
