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
// server.h


//define	PARANOID			// speed sapping error checking

#include "com_local.h"
#include "q_list.h"
#include "g_public.h"
#if USE_ZLIB
#include <zlib.h>
#endif

//=============================================================================

#define	MAX_MASTERS	8				// max recipients for heartbeat packets

#define SV_Malloc( size )       Z_TagMalloc( size, TAG_SERVER )
#define SV_Mallocz( size )      Z_TagMallocz( size, TAG_SERVER )
#define SV_CopyString( s )		Z_TagCopyString( s, TAG_SERVER )

#define SV_BASELINES_SHIFT			6
#define SV_BASELINES_PER_CHUNK		( 1 << SV_BASELINES_SHIFT )
#define SV_BASELINES_MASK			( SV_BASELINES_PER_CHUNK - 1 )
#define SV_BASELINES_CHUNKS			( MAX_EDICTS >> SV_BASELINES_SHIFT )

#define SV_InfoSet( var, val ) \
	Cvar_FullSet( var, val, CVAR_SERVERINFO|CVAR_NOSET, CVAR_SET_DIRECT )

typedef struct {
	unsigned	numEntities;
	unsigned	firstEntity;
    player_state_t ps;
	int			areabytes;
	byte		areabits[MAX_MAP_AREAS/8]; // portalarea visibility bits
	unsigned	sentTime;			// for ping calculations
    int         clientNum;
} client_frame_t;

#define PAUSED_FRAMES   10

typedef struct server_s {
	server_state_t	state;	// precache commands are only valid during load
    int             spawncount;  // random number generated each server spawn

	unsigned	time;			 // always sv.framenum * 100 msec
	int			framenum;

	char		name[MAX_QPATH];			// map name, or cinematic name
	cm_t		cm;

	char		configstrings[MAX_CONFIGSTRINGS][MAX_QPATH];

    struct {
	    fileHandle_t	file;
	    int     	    paused;
        int             framenum;
        unsigned        layout_time;
        sizebuf_t       datagram;
        sizebuf_t       message; // reliable
	    byte		    dcs[CS_BITMAP_BYTES];
    } mvd;

	unsigned	tracecount;
} server_t;

#define EDICT_POOL(c,n) ((edict_t *)((byte *)(c)->pool->edicts + (c)->pool->edict_size*(n)))

#define EDICT_NUM(n) ((edict_t *)((byte *)ge->edicts + ge->edict_size*(n)))
#define NUM_FOR_EDICT(e) ( ((byte *)(e)-(byte *)ge->edicts ) / ge->edict_size)

#define MAX_TOTAL_ENT_LEAFS		128

typedef enum {
	cs_free,		// can be reused for a new connection
	cs_zombie,		// client has been disconnected, but don't reuse
					// connection for a couple seconds
    cs_assigned,    // client_t assigned, but no data received from client yet
	cs_connected,	// netchan fully established, but not in game yet
	cs_primed,		// sent serverdata, client is precaching
	cs_spawned		// client is fully in game
} clstate_t;

#if USE_ANTICHEAT & 2

typedef enum {
    AC_NORMAL,
    AC_REQUIRED,
    AC_EXEMPT
} ac_required_t;

typedef enum {
    AC_QUERY_UNSENT,
    AC_QUERY_SENT,
    AC_QUERY_DONE
} ac_query_t;

#endif // USE_ANTICHEAT

#define MSG_POOLSIZE		1024
#define MSG_TRESHOLD	    ( 64 - 10 )		// keep pmsg_s 64 bytes aligned

#define MSG_RELIABLE	1
#define MSG_CLEAR		2

typedef struct {
	list_t	    entry;
	uint16_t	cursize;
    byte		data[MSG_TRESHOLD];
} message_packet_t;

typedef struct {
    list_t      entry;
    uint16_t    cursize;
    uint8_t     flags;
    uint8_t     index;
    uint16_t    sendchan;
    uint8_t     volume;
    uint8_t     attenuation;
    uint8_t     timeofs;
} sound_packet_t;

#define	LATENCY_COUNTS	16
#define LATENCY_MASK	( LATENCY_COUNTS - 1 )

#define	RATE_MESSAGES	10

#define FOR_EACH_CLIENT( client ) \
    LIST_FOR_EACH( client_t, client, &svs.udp_client_list, entry )

typedef enum {
    CF_RECONNECTED  = ( 1 << 0 ),
    CF_NODATA       = ( 1 << 1 ),
    CF_DEFLATE      = ( 1 << 2 ),
    CF_DROP         = ( 1 << 3 )
} client_flags_t;

typedef struct client_s {
    list_t          entry;

	clstate_t	    state;

    client_flags_t  flags;

	char			userinfo[MAX_INFO_STRING];		// name, etc
	char			*versionString;

    char            reconnect_var[16];
    char            reconnect_val[16];

	int				lastframe;			// for delta compression
	usercmd_t		lastcmd;			// for filling in big drops

	int				commandMsec;	// every seconds this is reset, if user
									// commands exhaust it, assume time cheating

	int				frame_latency[LATENCY_COUNTS];
	int				ping;

	int				message_size[RATE_MESSAGES];	// used to rate drop packets
	int				rate;
	int				surpressCount;		// number of messages rate supressed
	unsigned		sendTime;			// used to rate drop async packets
    frameflags_t    frameflags;

	edict_t			*edict;				// EDICT_NUM(clientnum+1)
	char			name[MAX_CLIENT_NAME];	// extracted from userinfo,
                                            // high bits masked
	int				messagelevel;		// for filtering printed messages
	int				number;             // client slot number

	clientSetting_t	settings[CLS_MAX];

	client_frame_t	frames[UPDATE_BACKUP];	// updates can be delta'd from here

	byte			*download; // file being downloaded
	int				downloadsize; // total bytes (can't use EOF because of paks)
	int				downloadcount; // bytes sent

	unsigned		lastmessage; // svs.realtime when packet was last received

	int				challenge; // challenge of this user, randomly generated
	int				protocol; // major version
    int             version; // minor version

    time_t          connect_time;

    // spectator speed, etc
	pmoveParams_t	pmp;

    // packetized messages for clients without
    // netchan level fragmentation support
	list_t			    msg_free;
	list_t			    msg_used[2];
	list_t			    msg_sound;
	message_packet_t	*msg_pool;

    // bulk messages for clients with 
    // netchan level fragmentation support
	sizebuf_t		datagram;

    // baselines are allocated per client
    entity_state_t  *baselines[SV_BASELINES_CHUNKS];

    // server state pointers (MVD channels hack)
    char            *configstrings;
    char            *gamedir, *mapname;
    edict_pool_t    *pool;
    cm_t            *cm;
    int             slot;

    // netchan type dependent methods
	void			(*AddMessage)( struct client_s *, byte *, int, qboolean );
	void			(*WriteFrame)( struct client_s * );
	void			(*FinishFrame)( struct client_s * );
	void			(*WriteDatagram)( struct client_s * );

	netchan_t		*netchan;

#if USE_ANTICHEAT & 2
    qboolean        ac_valid;
    ac_query_t      ac_query_sent;
    ac_required_t   ac_required;
    int             ac_file_failures;
    unsigned        ac_query_time;
    int             ac_client_type;
    string_entry_t  *ac_bad_files;
    char            *ac_token;
#endif
} client_t;

typedef enum {
    HTTP_METHOD_BAD,
    HTTP_METHOD_GET,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_POST
} htmethod_t;

typedef enum {
    HTTP_CODING_NONE,
    HTTP_CODING_GZIP,
    HTTP_CODING_DEFLATE,
    HTTP_CODING_UNKNOWN
} htcoding_t;

typedef struct {
    list_t      entry;
	clstate_t	state;
    netstream_t stream;
#if USE_ZLIB
    z_stream    z;
    int         noflush;
#endif
    unsigned    lastmessage; 

    char        request[MAX_NET_STRING];
    int         requestLength;
    htmethod_t  method;
    char        *resource, *host, *agent, *credentials;

    // MVD clients specific
    list_t      mvdEntry;
    struct mvd_s *mvd;
} tcpClient_t;

// a client can leave the server in one of four ways:
// dropping properly by quiting or disconnecting
// timing out if no valid messages are received for timeout.value seconds
// getting kicked off by the server operator
// a program error, like an overflowed reliable buffer

//=============================================================================

// MAX_CHALLENGES is made large to prevent a denial
// of service attack that could cycle all of them
// out before legitimate users connected
#define	MAX_CHALLENGES	1024

typedef struct {
	netadr_t	adr;
	unsigned	challenge;
	unsigned	time;
} challenge_t;

typedef struct {
	unsigned limit;
	unsigned period;
	unsigned time;
	unsigned count;
} ratelimit_t;

typedef struct {
    list_t  entry;
    uint32_t    addr;
    uint32_t    mask;
} addrmatch_t;

typedef struct server_static_s {
	qboolean	initialized;			// sv_init has completed
	unsigned	realtime;				// always increasing, no clamping, etc
    unsigned    zombiepoint, ghostpoint, droppoint;

	char		mapcmd[MAX_TOKEN_CHARS];	// ie: *intro.cin+base 

	gametype_t	gametype;

	client_t	*udp_client_pool;	 // [maxclients]
    list_t	    udp_client_list;         // linked list of non-free clients

	unsigned		numEntityStates; // maxclients*UPDATE_BACKUP*MAX_PACKET_ENTITIES
	unsigned		nextEntityStates;	// next entityState to use
	entity_state_t	*entityStates;		// [numEntityStates]

    list_t          tcp_client_pool;
    list_t          tcp_client_list;

    struct {
        list_t          clients;
        client_t        *dummy;
        byte            *message_data;
        byte            *datagram_data;
        player_state_t  *players;  // [maxclients]
        entity_state_t  *entities; // [MAX_EDICTS]
    } mvd;

#if USE_ZLIB
    z_stream        z; // for compressing messages at once
#endif

	unsigned		last_heartbeat;

	ratelimit_t		ratelimit_status;
	ratelimit_t		ratelimit_badpass;
	ratelimit_t		ratelimit_badrcon;

	challenge_t	challenges[MAX_CHALLENGES];	// to prevent invalid IPs from connecting
} server_static_t;

//=============================================================================

extern	netadr_t	net_from;

extern	netadr_t	master_adr[MAX_MASTERS];	// address of the master server

extern  list_t      sv_banlist;
extern  list_t      sv_blacklist;

extern	server_static_t	svs;				// persistant server info
extern	server_t		sv;					// local server

extern  pmoveParams_t	sv_pmp;


extern	cvar_t		*sv_hostname;
extern	cvar_t		*sv_maxclients;
extern	cvar_t		*sv_password;
extern	cvar_t		*sv_reserved_slots;
extern	cvar_t		*sv_noreload;			// don't reload level state when reentering
extern	cvar_t		*sv_airaccelerate;		// development tool
extern	cvar_t		*sv_qwmod;				// atu QW Physics modificator											
extern	cvar_t		*sv_enforcetime;
extern	cvar_t		*sv_force_reconnect;
extern	cvar_t		*sv_iplimit;

extern	cvar_t		*sv_http_enable;
extern	cvar_t		*sv_http_maxclients;
extern	cvar_t		*sv_http_minclients;

extern	cvar_t		*sv_debug_send;
extern	cvar_t		*sv_pad_packets;
extern	cvar_t		*sv_lan_force_rate;
extern  cvar_t      *sv_calcpings_method;

extern cvar_t		*sv_strafejump_hack;
extern cvar_t		*sv_bodyque_hack;
#ifndef _WIN32
extern cvar_t		*sv_oldgame_hack;
#endif

extern cvar_t		*sv_status_limit;
extern cvar_t		*sv_status_show;
extern cvar_t		*sv_badauth_time;
extern cvar_t	    *sv_uptime;

extern cvar_t       *sv_nextserver;

extern cvar_t       *sv_timeout;
extern cvar_t       *sv_zombietime;

extern	client_t	*sv_client;
extern	edict_t		*sv_player;



//===========================================================

//
// sv_main.c
//
void SV_DropClient( client_t *drop, const char *reason );
void SV_RemoveClient( client_t *client );

void SV_InitOperatorCommands (void);

void SV_UserinfoChanged (client_t *cl);
void SV_UpdateUserinfo( char *userinfo );

void SV_SendAsyncPackets( void );

qboolean SV_RateLimited( ratelimit_t *r );
void SV_RateInit( ratelimit_t *r, int limit, int period );

addrmatch_t *SV_MatchAddress( list_t *list, netadr_t *address );

int SV_CountClients( void );


void Master_Heartbeat (void);
void Master_Packet (void);

//
// sv_init.c
//
void SV_InitGame( qboolean ismvd );
void SV_Map (const char *levelstring, qboolean restart);
void SV_SpawnServer( const char *server, const char *spawnpoint );
void SV_ClientReset( client_t *client );

//
// sv_send.c
//
typedef enum {RD_NONE, RD_CLIENT, RD_PACKET} redirect_t;
#define	SV_OUTPUTBUF_LENGTH	(MAX_PACKETLEN_DEFAULT - 16)

#define SV_BeginRedirect( target ) \
	Com_BeginRedirect( target, sv_outputbuf, SV_OUTPUTBUF_LENGTH, SV_FlushRedirect )

extern	char	sv_outputbuf[SV_OUTPUTBUF_LENGTH];

void SV_FlushRedirect( int redirected, char *outputbuf, int length );

void SV_DemoCompleted (void);
void SV_SendClientMessages (void);

void SV_Multicast (vec3_t origin, multicast_t to);
void SV_ClientPrintf( client_t *cl, int level, const char *fmt, ... ) q_printf( 3, 4 );
void SV_BroadcastPrintf( int level, const char *fmt, ... ) q_printf( 2, 3 );
void SV_ClientCommand( client_t *cl, const char *fmt, ... ) q_printf( 2, 3 );
void SV_BroadcastCommand( const char *fmt, ... ) q_printf( 1, 2 );
void SV_ClientAddMessage( client_t *client, int flags );
void SV_PacketizedClear( client_t *client );

void SV_OldClientWriteDatagram( client_t *client );
void SV_OldClientAddMessage( client_t *client, byte *data,
							  int length, qboolean reliable );
void SV_OldClientWriteReliableMessages( client_t *client, int maxSize );
void SV_OldClientFinishFrame( client_t *client );

void SV_NewClientWriteDatagram( client_t *client );
void SV_NewClientAddMessage( client_t *client, byte *data,
							  int length, qboolean reliable );
void SV_NewClientFinishFrame( client_t *client );

void SV_CalcSendTime( client_t *client, int messageSize );

//
// sv_mvd.c
//

extern cvar_t	*sv_mvd_enable;
extern cvar_t	*sv_mvd_auth;
extern cvar_t	*sv_mvd_noblend;
extern cvar_t	*sv_mvd_nogun;

void SV_MvdRegister( void );
void SV_MvdBeginFrame( void );
void SV_MvdEndFrame( void );
void SV_MvdUnicast( sizebuf_t *buf, int clientNum, mvd_ops_t op );
void SV_MvdMulticast( sizebuf_t *buf, int leafnum, mvd_ops_t op );
void SV_MvdConfigstring( int index, const char *string );
void SV_MvdBroadcastPrint( int level, const char *string );
void SV_MvdRecStop( void );
qboolean SV_MvdPlayerIsActive( edict_t *ent );
void SV_MvdClientNew( tcpClient_t *client );
void SV_MvdInitStream( void );
void SV_MvdGetStream( const char *uri );
void SV_MvdRemoveDummy( void );
void SV_MvdSpawnDummy( void );
void SV_MvdDropDummy( const char *reason );

//
// sv_http.c
//

extern tcpClient_t  *http_client;
extern char         http_host[MAX_STRING_CHARS];
extern char         http_header[MAX_STRING_CHARS];

void SV_HttpRun( void ); 

void SV_HttpRemove( tcpClient_t *client ); 
void SV_HttpDrop( tcpClient_t *client, const char *error ); 
void SV_HttpWrite( tcpClient_t *client, void *data, int length ); 
void SV_HttpFinish( tcpClient_t *client );

void SV_HttpPrintf( const char *fmt, ... ) q_printf( 1, 2 );
void SV_HttpHeader( const char *title ); 
void SV_HttpFooter( void );
void SV_HttpReject( const char *error, const char *reason ); 

#if USE_ANTICHEAT

// 
// sv_ac.c
//
//
char *AC_ClientConnect( client_t *cl );
void AC_ClientDisconnect( client_t *cl );
qboolean AC_ClientBegin( client_t *cl );
void AC_ClientAnnounce( client_t *cl );
void AC_ClientToken( client_t *cl, const char *token );

void AC_Register( void );
void AC_Disconnect( void );
void AC_Connect( qboolean ismvd );
void AC_Run( void );

void AC_List_f( void );
void AC_Info_f( void );

#endif // USE_ANTICHEAT

//
// sv_user.c
//
void SV_New_f( void );
void SV_Begin_f( void );
void SV_Nextserver (void);
void SV_ExecuteClientMessage (client_t *cl);

//
// sv_ccmds.c
//
void SV_AddMatch_f( list_t *list );
void SV_DelMatch_f( list_t *list );
void SV_ListMatches_f( list_t *list );

//
// sv_ents.c
//

#define ES_INUSE( s ) \
	( (s)->modelindex || (s)->effects || (s)->sound || (s)->event )

void SV_BuildProxyClientFrame( client_t *client );
void SV_BuildClientFrame( client_t *client );
void SV_WriteFrameToClient_Default( client_t *client );
void SV_WriteFrameToClient_Enhanced( client_t *client );
qboolean SV_EdictPV( cm_t *cm, edict_t *ent, byte *mask ); 

//
// sv_game.c
//
extern	game_export_t	*ge;
extern  int             gameFeatures;

void SV_InitGameProgs( void );
void SV_ShutdownGameProgs (void);
void SV_InitEdict (edict_t *e);

void PF_Configstring( int index, const char *val );
void PF_Pmove( pmove_t *pm );

//============================================================

//
// high level object sorting to reduce interaction tests
//

void SV_ClearWorld (void);
// called after the world model has been loaded, before linking any entities

void PF_UnlinkEdict (edict_t *ent);
// call before removing an entity, and before trying to move one,
// so it doesn't clip against itself

void SV_LinkEdict( cm_t *cm, edict_t *ent );
void PF_LinkEdict (edict_t *ent);
// Needs to be called any time an entity changes origin, mins, maxs,
// or solid.  Automatically unlinks if needed.
// sets ent->v.absmin and ent->v.absmax
// sets ent->leafnums[] for pvs determination even if the entity
// is not solid

int SV_AreaEdicts (vec3_t mins, vec3_t maxs, edict_t **list, int maxcount, int areatype);
// fills in a table of edict pointers with edicts that have bounding boxes
// that intersect the given area.  It is possible for a non-axial bmodel
// to be returned that doesn't actually intersect the area on an exact
// test.
// returns the number of pointers filled in
// ??? does this always return the world?

//===================================================================

//
// functions that interact with everything apropriate
//
int SV_PointContents (vec3_t p);
// returns the CONTENTS_* value from the world at the given point.
// Quake 2 extends this to also check entities, to allow moving liquids

typedef trace_t (*sv_trace_t)( vec3_t, vec3_t, vec3_t, vec3_t, edict_t *, int );

trace_t SV_Trace (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, edict_t *passedict, int contentmask);
trace_t *SV_Trace_Old (trace_t *trace, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end,
					   edict_t *passedict, int contentmask);
// mins and maxs are relative

// if the entire move stays in a solid volume, trace.allsolid will be set,
// trace.startsolid will be set, and trace.fraction will be 0

// if the starting point is in a solid, it will be allowed to move out
// to an open area

// passedict is explicitly excluded from clipping checks (normally NULL)

