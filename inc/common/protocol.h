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

//
// protocol.h -- communications protocols
//

#define MAX_MSGLEN  0x10000     // max length of a message, 64k

#define PROTOCOL_VERSION_OLD        26
#define PROTOCOL_VERSION_DEFAULT    34
#define PROTOCOL_VERSION_R1Q2       35
#define PROTOCOL_VERSION_Q2PRO      36
#define PROTOCOL_VERSION_MVD        37 // not used for UDP connections
#define PROTOCOL_VERSION_EXTENDED   3434

#define PROTOCOL_VERSION_R1Q2_MINIMUM           1903    // b6377
#define PROTOCOL_VERSION_R1Q2_UCMD              1904    // b7387
#define PROTOCOL_VERSION_R1Q2_LONG_SOLID        1905    // b7759
#define PROTOCOL_VERSION_R1Q2_CURRENT           1905    // b7759

#define PROTOCOL_VERSION_Q2PRO_MINIMUM          1015    // r335
#define PROTOCOL_VERSION_Q2PRO_RESERVED         1016    // r364
#define PROTOCOL_VERSION_Q2PRO_BEAM_ORIGIN      1017    // r1037-8
#define PROTOCOL_VERSION_Q2PRO_SHORT_ANGLES     1018    // r1037-44
#define PROTOCOL_VERSION_Q2PRO_SERVER_STATE     1019    // r1302
#define PROTOCOL_VERSION_Q2PRO_EXTENDED_LAYOUT  1020    // r1354
#define PROTOCOL_VERSION_Q2PRO_ZLIB_DOWNLOADS   1021    // r1358
#define PROTOCOL_VERSION_Q2PRO_CLIENTNUM_SHORT  1022    // r2161
#define PROTOCOL_VERSION_Q2PRO_CINEMATICS       1023    // r2263
#define PROTOCOL_VERSION_Q2PRO_EXTENDED_LIMITS  1024    // r
#define PROTOCOL_VERSION_Q2PRO_CURRENT          1024    // r

#define PROTOCOL_VERSION_MVD_MINIMUM            2009    // r168
#define PROTOCOL_VERSION_MVD_DEFAULT            2010    // r177
#define PROTOCOL_VERSION_MVD_EXTENDED_LIMITS    2011    // r
#define PROTOCOL_VERSION_MVD_CURRENT            2011    // r

#define R1Q2_SUPPORTED(x) \
    ((x) >= PROTOCOL_VERSION_R1Q2_MINIMUM && \
     (x) <= PROTOCOL_VERSION_R1Q2_CURRENT)

#define Q2PRO_SUPPORTED(x) \
    ((x) >= PROTOCOL_VERSION_Q2PRO_MINIMUM && \
     (x) <= PROTOCOL_VERSION_Q2PRO_CURRENT)

#define MVD_SUPPORTED(x) \
    ((x) >= PROTOCOL_VERSION_MVD_MINIMUM && \
     (x) <= PROTOCOL_VERSION_MVD_CURRENT)

#define VALIDATE_CLIENTNUM(csr, x) \
    ((x) >= -1 && (x) < (csr)->max_edicts - 1)

#define Q2PRO_PF_STRAFEJUMP_HACK    BIT(0)
#define Q2PRO_PF_QW_MODE            BIT(1)
#define Q2PRO_PF_WATERJUMP_HACK     BIT(2)
#define Q2PRO_PF_EXTENSIONS         BIT(3)

//=========================================

#define UPDATE_BACKUP   16  // copies of entity_state_t to keep buffered
                            // must be power of two
#define UPDATE_MASK     (UPDATE_BACKUP - 1)

#define CMD_BACKUP      128 // allow a lot of command backups for very fast systems
                            // increased from 64
#define CMD_MASK        (CMD_BACKUP - 1)


#define SVCMD_BITS              5
#define SVCMD_MASK              (BIT(SVCMD_BITS) - 1)

#define FRAMENUM_BITS           27
#define FRAMENUM_MASK           (BIT(FRAMENUM_BITS) - 1)

#define SUPPRESSCOUNT_BITS      4
#define SUPPRESSCOUNT_MASK      (BIT(SUPPRESSCOUNT_BITS) - 1)

/* Note: both this and MAX_PACKET_ENTITIES are larger than their upstream Q2PRO
 * counterparts. This is because Q2RTX may end up sending, comparatively, quite
 * a few more entities due to it's use of the PVS2 for entity culling */
#define MAX_PACKET_ENTITIES_OLD 1024

#define MAX_PACKET_ENTITIES     1024
#define MAX_PARSE_ENTITIES      (MAX_PACKET_ENTITIES * UPDATE_BACKUP)
#define PARSE_ENTITIES_MASK     (MAX_PARSE_ENTITIES - 1)

#define MAX_PACKET_USERCMDS     32
#define MAX_PACKET_FRAMES       4

#define MAX_PACKET_STRINGCMDS   8
#define MAX_PACKET_USERINFOS    8

#define CS_BITMAP_BYTES         (MAX_CONFIGSTRINGS / 8) // 260
#define CS_BITMAP_LONGS         (CS_BITMAP_BYTES / 4)

#define MVD_MAGIC               MakeRawLong('M','V','D','2')

//
// server to client
//
typedef enum {
    svc_bad,

    // these ops are known to the game dll
    svc_muzzleflash,
    svc_muzzleflash2,
    svc_temp_entity,
    svc_layout,
    svc_inventory,

    // the rest are private to the client and server
    svc_nop,
    svc_disconnect,
    svc_reconnect,
    svc_sound,                  // <see code>
    svc_print,                  // [byte] id [string] null terminated string
    svc_stufftext,              // [string] stuffed into client's console buffer
                                // should be \n terminated
    svc_serverdata,             // [long] protocol ...
    svc_configstring,           // [short] [string]
    svc_spawnbaseline,
    svc_centerprint,            // [string] to put in center of the screen
    svc_download,               // [short] size [size bytes]
    svc_playerinfo,             // variable
    svc_packetentities,         // [...]
    svc_deltapacketentities,    // [...]
    svc_frame,

    // r1q2 specific operations
    svc_zpacket,
    svc_zdownload,
    svc_gamestate, // q2pro specific, means svc_playerupdate in r1q2
    svc_setting,

    // q2pro specific operations
    svc_configstringstream,
    svc_baselinestream,

    svc_num_types
} svc_ops_t;

// MVD protocol specific operations
typedef enum {
    mvd_bad,
    mvd_nop,
    mvd_disconnect,     // reserved
    mvd_reconnect,      // reserved
    mvd_serverdata,
    mvd_configstring,
    mvd_frame,
    mvd_frame_nodelta,  // reserved
    mvd_unicast,
    mvd_unicast_r,

    // must match multicast_t order!!!
    mvd_multicast_all,
    mvd_multicast_phs,
    mvd_multicast_pvs,
    mvd_multicast_all_r,
    mvd_multicast_phs_r,
    mvd_multicast_pvs_r,

    mvd_sound,
    mvd_print,
    mvd_stufftext,      // reserved

    mvd_num_types
} mvd_ops_t;

// MVD stream flags (only 3 bits can be used)
typedef enum {
    MVF_NOMSGS      = BIT(0),
    MVF_SINGLEPOV   = BIT(1),
    MVF_EXTLIMITS   = BIT(2)
} mvd_flags_t;

//==============================================

//
// client to server
//
typedef enum {
    clc_bad,
    clc_nop,
    clc_move,               // [usercmd_t]
    clc_userinfo,           // [userinfo string]
    clc_stringcmd,          // [string] message

    // r1q2 specific operations
    clc_setting,

    // q2pro specific operations
    clc_move_nodelta = 10,
    clc_move_batched,
    clc_userinfo_delta
} clc_ops_t;

//==============================================

// player_state_t communication

#define PS_M_TYPE           BIT(0)
#define PS_M_ORIGIN         BIT(1)
#define PS_M_VELOCITY       BIT(2)
#define PS_M_TIME           BIT(3)
#define PS_M_FLAGS          BIT(4)
#define PS_M_GRAVITY        BIT(5)
#define PS_M_DELTA_ANGLES   BIT(6)

#define PS_VIEWOFFSET       BIT(7)
#define PS_VIEWANGLES       BIT(8)
#define PS_KICKANGLES       BIT(9)
#define PS_BLEND            BIT(10)
#define PS_FOV              BIT(11)
#define PS_WEAPONINDEX      BIT(12)
#define PS_WEAPONFRAME      BIT(13)
#define PS_RDFLAGS          BIT(14)
#define PS_RESERVED         BIT(15)

// r1q2 protocol specific extra flags
#define EPS_GUNOFFSET       BIT(0)
#define EPS_GUNANGLES       BIT(1)
#define EPS_M_VELOCITY2     BIT(2)
#define EPS_M_ORIGIN2       BIT(3)
#define EPS_VIEWANGLE2      BIT(4)
#define EPS_STATS           BIT(5)

// q2pro protocol specific extra flags
#define EPS_CLIENTNUM       BIT(6)

//==============================================

// packetized player_state_t communication (MVD specific)

#define PPS_M_TYPE          BIT(0)
#define PPS_M_ORIGIN        BIT(1)
#define PPS_M_ORIGIN2       BIT(2)

#define PPS_VIEWOFFSET      BIT(3)
#define PPS_VIEWANGLES      BIT(4)
#define PPS_VIEWANGLE2      BIT(5)
#define PPS_KICKANGLES      BIT(6)
#define PPS_BLEND           BIT(7)
#define PPS_FOV             BIT(8)
#define PPS_WEAPONINDEX     BIT(9)
#define PPS_WEAPONFRAME     BIT(10)
#define PPS_GUNOFFSET       BIT(11)
#define PPS_GUNANGLES       BIT(12)
#define PPS_RDFLAGS         BIT(13)
#define PPS_STATS           BIT(14)
#define PPS_REMOVE          BIT(15)

// this is just a small hack to store inuse flag
// in a field left otherwise unused by MVD code
#define PPS_INUSE(ps)       (ps)->pmove.pm_time

//==============================================

// user_cmd_t communication

// ms and light always sent, the others are optional
#define CM_ANGLE1       BIT(0)
#define CM_ANGLE2       BIT(1)
#define CM_ANGLE3       BIT(2)
#define CM_FORWARD      BIT(3)
#define CM_SIDE         BIT(4)
#define CM_UP           BIT(5)
#define CM_BUTTONS      BIT(6)
#define CM_IMPULSE      BIT(7)

// r1q2 button byte hacks
#define BUTTON_MASK     (BUTTON_ATTACK|BUTTON_USE|BUTTON_ANY)
#define BUTTON_FORWARD  BIT(2)
#define BUTTON_SIDE     BIT(3)
#define BUTTON_UP       BIT(4)
#define BUTTON_ANGLE1   BIT(5)
#define BUTTON_ANGLE2   BIT(6)

//==============================================

// a sound without an ent or pos will be a local only sound
#define SND_VOLUME          BIT(0)  // a byte
#define SND_ATTENUATION     BIT(1)  // a byte
#define SND_POS             BIT(2)  // three coordinates
#define SND_ENT             BIT(3)  // a short 0-2: channel, 3-15: entity
#define SND_OFFSET          BIT(4)  // a byte, msec offset from frame start
#define SND_INDEX16         BIT(5)  // index is 16-bit

#define DEFAULT_SOUND_PACKET_VOLUME         1.0f
#define DEFAULT_SOUND_PACKET_ATTENUATION    1.0f

//==============================================

// entity_state_t communication

// try to pack the common update flags into the first byte
#define U_ORIGIN1       BIT(0)
#define U_ORIGIN2       BIT(1)
#define U_ANGLE2        BIT(2)
#define U_ANGLE3        BIT(3)
#define U_FRAME8        BIT(4)      // frame is a byte
#define U_EVENT         BIT(5)
#define U_REMOVE        BIT(6)      // REMOVE this entity, don't add it
#define U_MOREBITS1     BIT(7)      // read one additional byte

// second byte
#define U_NUMBER16      BIT(8)      // NUMBER8 is implicit if not set
#define U_ORIGIN3       BIT(9)
#define U_ANGLE1        BIT(10)
#define U_MODEL         BIT(11)
#define U_RENDERFX8     BIT(12)     // fullbright, etc
#define U_ANGLE16       BIT(13)
#define U_EFFECTS8      BIT(14)     // autorotate, trails, etc
#define U_MOREBITS2     BIT(15)     // read one additional byte

// third byte
#define U_SKIN8         BIT(16)
#define U_FRAME16       BIT(17)     // frame is a short
#define U_RENDERFX16    BIT(18)     // 8 + 16 = 32
#define U_EFFECTS16     BIT(19)     // 8 + 16 = 32
#define U_MODEL2        BIT(20)     // weapons, flags, etc
#define U_MODEL3        BIT(21)
#define U_MODEL4        BIT(22)
#define U_MOREBITS3     BIT(23)     // read one additional byte

// fourth byte
#define U_OLDORIGIN     BIT(24)     // FIXME: get rid of this
#define U_SKIN16        BIT(25)
#define U_SOUND         BIT(26)
#define U_SOLID         BIT(27)
#define U_MODEL16       BIT(28)
#define U_MOREFX8       BIT(29)
#define U_ALPHA         BIT(30)
#define U_MOREBITS4     BIT(31)     // read one additional byte

// fifth byte
#define U_SCALE         BIT_ULL(32)
#define U_MOREFX16      BIT_ULL(33)

#define U_SKIN32        (U_SKIN8 | U_SKIN16)        // used for laser colors
#define U_EFFECTS32     (U_EFFECTS8 | U_EFFECTS16)
#define U_RENDERFX32    (U_RENDERFX8 | U_RENDERFX16)
#define U_MOREFX32      (U_MOREFX8 | U_MOREFX16)

// ==============================================================

// a client with this number will never be included in MVD stream
#define CLIENTNUM_NONE      (MAX_CLIENTS - 1)

// a SOLID_BBOX will never create this value
#define PACKED_BSP      31

typedef enum {
    // r1q2 specific
    CLS_NOGUN,
    CLS_NOBLEND,
    CLS_RECORDING,
    CLS_PLAYERUPDATES,
    CLS_FPS,

    // q2pro specific
    CLS_NOGIBS            = 10,
    CLS_NOFOOTSTEPS,
    CLS_NOPREDICT,

    CLS_MAX
} clientSetting_t;

typedef enum {
    // r1q2 specific
    SVS_PLAYERUPDATES,
    SVS_FPS,

    SVS_MAX
} serverSetting_t;

// q2pro frame flags sent by the server
// only SUPPRESSCOUNT_BITS can be used
#define FF_SUPPRESSED   BIT(0)
#define FF_CLIENTDROP   BIT(1)
#define FF_CLIENTPRED   BIT(2)
#define FF_RESERVED     BIT(3)
