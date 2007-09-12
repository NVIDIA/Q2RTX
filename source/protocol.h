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

//
// protocol.h -- communications protocols
//

#define	MAX_MSGLEN		0x8000		// max length of a message, 32k

#define	PROTOCOL_VERSION_OLD		26
#define	PROTOCOL_VERSION_DEFAULT	34
#define PROTOCOL_VERSION_R1Q2		35
#define PROTOCOL_VERSION_Q2PRO		36
#define PROTOCOL_VERSION_MVD		37 // not used for UDP connections

#define PROTOCOL_VERSION_R1Q2_MINOR		1903	// r1q2-b6377
#define PROTOCOL_VERSION_Q2PRO_MINOR	1010	// q2pro r158
#define PROTOCOL_VERSION_MVD_MINOR		2008	// q2pro r160

//=========================================

#define	PORT_MASTER		27900
#define	PORT_CLIENT		27901
#define	PORT_SERVER		27910
#define PORT_MAX_SEARCH	5

//=========================================

#define	UPDATE_BACKUP	16	// copies of entity_state_t to keep buffered
							// must be power of two
#define	UPDATE_MASK		( UPDATE_BACKUP - 1 )

#define	CMD_BACKUP		128		// allow a lot of command backups for very fast systems
								// increased from 64
#define CMD_MASK		( CMD_BACKUP - 1 )



#define SVCMD_BITS				5
#define SVCMD_MASK				( ( 1 << SVCMD_BITS ) - 1 )

#define	FRAMENUM_BITS			27
#define FRAMENUM_MASK			( ( 1 << FRAMENUM_BITS ) - 1 )

#define SURPRESSCOUNT_BITS		4
#define SURPRESSCOUNT_MASK		( ( 1 << SURPRESSCOUNT_BITS ) - 1 )

#define MAX_PACKET_ENTITIES		128
#define	MAX_PARSE_ENTITIES		2048	// should be MAX_PACKET_ENTITIES * UPDATE_BACKUP
#define PARSE_ENTITIES_MASK		( MAX_PARSE_ENTITIES - 1 )

#define MAX_PACKET_USERCMDS			32
#define MAX_PACKET_FRAMES			4

#define	MAX_PACKET_STRINGCMDS	8
#define	MAX_PACKET_USERINFOS	8

//
// server to client
//
typedef enum svc_ops_e {
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
	svc_sound,					// <see code>
	svc_print,					// [byte] id [string] null terminated string
	svc_stufftext,				// [string] stuffed into client's console buffer
                                // should be \n terminated
	svc_serverdata,				// [long] protocol ...
	svc_configstring,			// [short] [string]
	svc_spawnbaseline,		
	svc_centerprint,			// [string] to put in center of the screen
	svc_download,				// [short] size [size bytes]
	svc_playerinfo,				// variable
	svc_packetentities,			// [...]
	svc_deltapacketentities,	// [...]
	svc_frame,

	// r1q2 specific operations
	svc_zpacket,
	svc_zdownload,

    // q2pro specific operations
    svc_gamestate,

    svc_num_types
} svc_ops_t;

// MVD protocol specific operations
typedef enum mvd_ops_e {
    mvd_bad,
    mvd_nop,
    mvd_disconnect,     // reserved
    mvd_reconnect,      // reserved
    mvd_serverdata,
    mvd_configstring,
    mvd_frame,
    mvd_frame_nodelta,
	mvd_unicast,
    mvd_unicast_r,
	mvd_multicast_all,
	mvd_multicast_pvs,
	mvd_multicast_phs,
	mvd_multicast_all_r,
	mvd_multicast_pvs_r,
	mvd_multicast_phs_r,
    mvd_sound,
    mvd_print,          // reserved
    mvd_stufftext,      // reserved

    mvd_num_types
} mvd_ops_t;

//==============================================

//
// client to server
//
typedef enum clc_ops_e {
	clc_bad,
	clc_nop, 		
	clc_move,				// [usercmd_t]
	clc_userinfo,			// [userinfo string]
	clc_stringcmd,			// [string] message

	// r1q2 specific operations
	clc_setting,

	// q2pro specific operations
	clc_move_nodelta = 10,
	clc_move_batched,
	clc_userinfo_delta
} clc_ops_t;

//==============================================

// player_state_t communication

#define	PS_M_TYPE			(1<<0)
#define	PS_M_ORIGIN			(1<<1)
#define	PS_M_VELOCITY		(1<<2)
#define	PS_M_TIME			(1<<3)
#define	PS_M_FLAGS			(1<<4)
#define	PS_M_GRAVITY		(1<<5)
#define	PS_M_DELTA_ANGLES	(1<<6)

#define	PS_VIEWOFFSET		(1<<7)
#define	PS_VIEWANGLES		(1<<8)
#define	PS_KICKANGLES		(1<<9)
#define	PS_BLEND			(1<<10)
#define	PS_FOV				(1<<11)
#define	PS_WEAPONINDEX		(1<<12)
#define	PS_WEAPONFRAME		(1<<13)
#define	PS_RDFLAGS			(1<<14)
#define	PS_RESERVED			(1<<15)

#define PS_BITS				16
#define PS_MASK				( ( 1 << PS_BITS ) - 1 )

// r1q2 protocol specific extra flags
#define	EPS_GUNOFFSET		(1<<0)
#define	EPS_GUNANGLES		(1<<1)
#define	EPS_M_VELOCITY2		(1<<2)
#define	EPS_M_ORIGIN2		(1<<3)
#define	EPS_VIEWANGLE2		(1<<4)
#define	EPS_STATS			(1<<5)

// q2pro protocol specific extra flags
#define	EPS_CLIENTNUM		(1<<6)

#define EPS_BITS			7
#define EPS_MASK			( ( 1 << EPS_BITS ) - 1 )

//==============================================

// packetized player_state_t communication (MVD specific)

#define	PPS_M_TYPE			(1<<0)
#define	PPS_M_ORIGIN		(1<<1)
#define	PPS_M_ORIGIN2		(1<<2)

#define	PPS_VIEWOFFSET		(1<<3)
#define	PPS_VIEWANGLES		(1<<4)
#define	PPS_VIEWANGLE2		(1<<5)
#define	PPS_KICKANGLES		(1<<6)
#define	PPS_BLEND			(1<<7)
#define	PPS_FOV				(1<<8)
#define	PPS_WEAPONINDEX		(1<<9)
#define	PPS_WEAPONFRAME		(1<<10)
#define	PPS_GUNOFFSET	    (1<<11)
#define	PPS_GUNANGLES	    (1<<12)
#define	PPS_RDFLAGS			(1<<13)
#define	PPS_STATS		    (1<<14)
#define	PPS_REMOVE			(1<<15)

#define PPS_NUM( ps )       (ps)->pmove.pm_flags
#define PPS_INUSE( ps )     (ps)->pmove.pm_time

//==============================================

// user_cmd_t communication

// ms and light always sent, the others are optional
#define	CM_ANGLE1 	(1<<0)
#define	CM_ANGLE2 	(1<<1)
#define	CM_ANGLE3 	(1<<2)
#define	CM_FORWARD	(1<<3)
#define	CM_SIDE		(1<<4)
#define	CM_UP		(1<<5)
#define	CM_BUTTONS	(1<<6)
#define	CM_IMPULSE	(1<<7)

/* q2pro protocol specific extra flag */
#define ECM_LIGHTLEVEL	(1<<0)

//==============================================

// a sound without an ent or pos will be a local only sound
#define	SND_VOLUME		(1<<0)		// a byte
#define	SND_ATTENUATION	(1<<1)		// a byte
#define	SND_POS			(1<<2)		// three coordinates
#define	SND_ENT			(1<<3)		// a short 0-2: channel, 3-12: entity
#define	SND_OFFSET		(1<<4)		// a byte, msec offset from frame start

#define DEFAULT_SOUND_PACKET_VOLUME	1.0
#define DEFAULT_SOUND_PACKET_ATTENUATION 1.0

//==============================================

// entity_state_t communication

// try to pack the common update flags into the first byte
#define	U_ORIGIN1	(1<<0)
#define	U_ORIGIN2	(1<<1)
#define	U_ANGLE2	(1<<2)
#define	U_ANGLE3	(1<<3)
#define	U_FRAME8	(1<<4)		// frame is a byte
#define	U_EVENT		(1<<5)
#define	U_REMOVE	(1<<6)		// REMOVE this entity, don't add it
#define	U_MOREBITS1	(1<<7)		// read one additional byte

// second byte
#define	U_NUMBER16	(1<<8)		// NUMBER8 is implicit if not set
#define	U_ORIGIN3	(1<<9)
#define	U_ANGLE1	(1<<10)
#define	U_MODEL		(1<<11)
#define U_RENDERFX8	(1<<12)		// fullbright, etc
#define	U_EFFECTS8	(1<<14)		// autorotate, trails, etc
#define	U_MOREBITS2	(1<<15)		// read one additional byte

// third byte
#define	U_SKIN8		(1<<16)
#define	U_FRAME16	(1<<17)		// frame is a short
#define	U_RENDERFX16 (1<<18)	// 8 + 16 = 32
#define	U_EFFECTS16	(1<<19)		// 8 + 16 = 32
#define	U_MODEL2	(1<<20)		// weapons, flags, etc
#define	U_MODEL3	(1<<21)
#define	U_MODEL4	(1<<22)
#define	U_MOREBITS3	(1<<23)		// read one additional byte

// fourth byte
#define	U_OLDORIGIN	(1<<24)		// FIXME: get rid of this
#define	U_SKIN16	(1<<25)
#define	U_SOUND		(1<<26)
#define	U_SOLID		(1<<27)

// ==============================================================

#define CLIENTNUM_NONE		( MAX_CLIENTS - 1 )
#define CLIENTNUM_RESERVED	( MAX_CLIENTS - 1 )

typedef enum clientSetting_e {
	/* r1q2 specific */
	CLS_NOGUN,
	CLS_NOBLEND,
	CLS_RECORDING,

	/* q2pro specific */
	CLS_NOGIBS			= 10,
	CLS_NOFOOTSTEPS,
	CLS_NOPREDICT,

	CLS_MAX
} clientSetting_t;

typedef enum gametype_e {
	GT_SINGLEPLAYER,
	GT_COOP,
	GT_DEATHMATCH
} gametype_t;

typedef enum {
    /* these are sent by the server */
    FF_SURPRESSED   = ( 1 << 0 ),
    FF_CLIENTDROP   = ( 1 << 1 ),
    FF_CLIENTPRED   = ( 1 << 2 ),

    /* these are calculated by the client */
    FF_SERVERDROP   = ( 1 << 4 ),
    FF_BADFRAME    = ( 1 << 5 ),
    FF_OLDFRAME     = ( 1 << 6 ),
    FF_OLDENT       = ( 1 << 7 ),
    FF_NODELTA      = ( 1 << 8 )
} frameflags_t;

