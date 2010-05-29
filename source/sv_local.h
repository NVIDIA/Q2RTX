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


//define    PARANOID            // speed sapping error checking

#include "com_local.h"
#include "q_list.h"
#include "files.h"
#include "sys_public.h"
#include "bsp.h"
#include "cmodel.h"
#include "pmove.h"
#include "protocol.h"
#include "q_msg.h"
#include "net_sock.h"
#include "net_chan.h"
#include "g_public.h"
#include "sv_public.h"
#include "cl_public.h"
#if USE_MVD_CLIENT
#include "mvd_public.h"
#endif
#include "io_sleep.h"
#if USE_ZLIB
#include <zlib.h>
#endif

//=============================================================================

#define MAX_MASTERS         8       // max recipients for heartbeat packets
#define HEARTBEAT_SECONDS   300

#define SV_Malloc( size )       Z_TagMalloc( size, TAG_SERVER )
#define SV_Mallocz( size )      Z_TagMallocz( size, TAG_SERVER )
#define SV_CopyString( s )      Z_TagCopyString( s, TAG_SERVER )

#ifdef _DEBUG
#define SV_DPrintf(level,...) \
    if( sv_debug && sv_debug->integer > level ) \
        Com_LPrintf( PRINT_DEVELOPER, __VA_ARGS__ )
#else
#define SV_DPrintf(...)
#endif

#define SV_BASELINES_SHIFT          6
#define SV_BASELINES_PER_CHUNK      ( 1 << SV_BASELINES_SHIFT )
#define SV_BASELINES_MASK           ( SV_BASELINES_PER_CHUNK - 1 )
#define SV_BASELINES_CHUNKS         ( MAX_EDICTS >> SV_BASELINES_SHIFT )

#define SV_InfoSet( var, val ) \
    Cvar_FullSet( var, val, CVAR_SERVERINFO|CVAR_ROM, FROM_CODE )

// game features this server supports
#define SV_FEATURES (GMF_CLIENTNUM|GMF_PROPERINUSE|GMF_MVDSPEC|\
                     GMF_WANT_ALL_DISCONNECTS|GMF_ENHANCED_SAVEGAMES)

typedef struct {
    unsigned    numEntities;
    unsigned    firstEntity;
    player_state_t ps;
    int         areabytes;
    byte        areabits[MAX_MAP_AREA_BYTES];  // portalarea visibility bits
    unsigned    sentTime;                   // for ping calculations
    int         clientNum;
} client_frame_t;

typedef struct {
    int solid32;
} server_entity_t;

#define SV_FPS          10
#define SV_FRAMETIME    100

typedef struct {
    server_state_t  state;      // precache commands are only valid during load
    int             spawncount; // random number generated each server spawn

    int         framenum;
#if 0
    int         framemult; // 0 or 1
    unsigned    frametime; // 100 or 50
#endif
    unsigned    frameresidual;

    char        name[MAX_QPATH];            // map name, or cinematic name
    cm_t        cm;

    char        configstrings[MAX_CONFIGSTRINGS][MAX_QPATH];

    server_entity_t entities[MAX_EDICTS];

    unsigned    tracecount;
} server_t;

#define EDICT_POOL(c,n) ((edict_t *)((byte *)(c)->pool->edicts + (c)->pool->edict_size*(n)))

#define EDICT_NUM(n) ((edict_t *)((byte *)ge->edicts + ge->edict_size*(n)))
#define NUM_FOR_EDICT(e) ((int)(((byte *)(e)-(byte *)ge->edicts ) / ge->edict_size))

#define MAX_TOTAL_ENT_LEAFS        128

typedef enum {
    cs_free,        // can be reused for a new connection
    cs_zombie,      // client has been disconnected, but don't reuse
                    // connection for a couple seconds
    cs_assigned,    // client_t assigned, but no data received from client yet
    cs_connected,   // netchan fully established, but not in game yet
    cs_primed,      // sent serverdata, client is precaching
    cs_spawned      // client is fully in game
} clstate_t;

#if USE_AC_SERVER

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

#endif // USE_AC_SERVER

#define MSG_POOLSIZE        1024
#define MSG_TRESHOLD        ( 64 - 10 )        // keep pmsg_s 64 bytes aligned

#define MSG_RELIABLE    1
#define MSG_CLEAR       2

#define MAX_SOUND_PACKET   14

typedef struct {
    list_t              entry;
    uint16_t            cursize;    // zero means sound packet
    union {
        uint8_t         data[MSG_TRESHOLD];
        struct {
            uint8_t     flags;
            uint8_t     index;
            uint16_t    sendchan;
            uint8_t     volume;
            uint8_t     attenuation;
            uint8_t     timeofs;
            int16_t     pos[3];     // saved in case entity is freed
        };
    };
} message_packet_t;


#define LATENCY_COUNTS  16
#define LATENCY_MASK    ( LATENCY_COUNTS - 1 )

#define RATE_MESSAGES   10

#define FOR_EACH_CLIENT( client ) \
    LIST_FOR_EACH( client_t, client, &svs.udp_client_list, entry )

#define PL_S2C(cl) (cl->frames_sent ? \
    (1.0f-(float)cl->frames_acked/cl->frames_sent)*100.0f : 0.0f)
#define PL_C2S(cl) (cl->netchan->total_received ? \
    ((float)cl->netchan->total_dropped/cl->netchan->total_received)*100.0f : 0.0f)
#define AVG_PING(cl) (cl->avg_ping_count ? \
    cl->avg_ping_time/cl->avg_ping_count : cl->ping)

typedef enum {
    CF_RECONNECTED  = ( 1 << 0 ),
    CF_NODATA       = ( 1 << 1 ),
    CF_DEFLATE      = ( 1 << 2 ),
    CF_DROP         = ( 1 << 3 ),
#if USE_ICMP
    CF_ERROR        = ( 1 << 4 ),
#endif
} client_flags_t;

typedef struct client_s {
    list_t          entry;

    clstate_t       state;

    client_flags_t  flags;

    char            userinfo[MAX_INFO_STRING];        // name, etc
    char            *versionString;

    char            reconnect_var[16];
    char            reconnect_val[16];

    int             lastframe;            // for delta compression
    usercmd_t       lastcmd;            // for filling in big drops

    int             commandMsec;    // every seconds this is reset, if user
                                    // commands exhaust it, assume time cheating
    int             numMoves;
    int             fps;

    int             frame_latency[LATENCY_COUNTS];
    int             ping, min_ping, max_ping;
    int             avg_ping_time, avg_ping_count;

    size_t          message_size[RATE_MESSAGES];    // used to rate drop packets
    size_t          rate;
    int             surpressCount;        // number of messages rate supressed
    unsigned        send_time, send_delta;    // used to rate drop async packets
    frameflags_t    frameflags;

    edict_t         *edict;                 // EDICT_NUM(clientnum+1)
    char            name[MAX_CLIENT_NAME];  // extracted from userinfo,
                                            // high bits masked
    int             messagelevel;       // for filtering printed messages
    int             number;             // client slot number

    clientSetting_t settings[CLS_MAX];

    msgEsFlags_t    esFlags;

    client_frame_t  frames[UPDATE_BACKUP];    // updates can be delta'd from here
    unsigned        frames_sent, frames_acked, frames_nodelta;

    byte            *download; // file being downloaded
    int             downloadsize; // total bytes (can't use EOF because of paks)
    int             downloadcount; // bytes sent
    char            *downloadname; // name of the file

    unsigned        lastmessage; // svs.realtime when packet was last received

    int             challenge; // challenge of this user, randomly generated
    int             protocol; // major version
    int             version; // minor version

    time_t          connect_time;

    // spectator speed, etc
    pmoveParams_t    pmp;

    // packetized messages
    list_t              msg_free_list;
    list_t              msg_unreliable_list;
    list_t              msg_reliable_list;
    message_packet_t    *msg_pool;
    size_t              msg_unreliable_bytes; // total size of unreliable datagram
    size_t              msg_dynamic_bytes; // total size of dynamic memory allocated

    // baselines are allocated per client
    entity_state_t  *baselines[SV_BASELINES_CHUNKS];

    // server state pointers (hack for MVD channels implementation)
    char            *configstrings;
    char            *gamedir, *mapname;
    edict_pool_t    *pool;
    cm_t            *cm;
    int             slot;
    int             spawncount;
    int             maxclients;

    // netchan type dependent methods
    void            (*AddMessage)( struct client_s *, byte *, size_t, qboolean );
    void            (*WriteFrame)( struct client_s * );
    void            (*WriteDatagram)( struct client_s * );

    netchan_t        *netchan;
    int             numpackets; // for that nasty packetdup hack

#if USE_AC_SERVER
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

// a client can leave the server in one of four ways:
// dropping properly by quiting or disconnecting
// timing out if no valid messages are received for timeout.value seconds
// getting kicked off by the server operator
// a program error, like an overflowed reliable buffer

//=============================================================================

// MAX_CHALLENGES is made large to prevent a denial
// of service attack that could cycle all of them
// out before legitimate users connected
#define    MAX_CHALLENGES    1024

typedef struct {
    netadr_t    adr;
    unsigned    challenge;
    unsigned    time;
} challenge_t;

typedef struct {
    unsigned    limit;
    unsigned    period;
    unsigned    time;
    unsigned    count;
} ratelimit_t;

typedef struct {
    list_t      entry;
    uint32_t    addr;
    uint32_t    mask;
    unsigned    hits;
    time_t      time;   // time of the last hit
    char        comment[1];
} addrmatch_t;

typedef struct {
    list_t  entry;
    int     len;
    char    string[1];
} stuffcmd_t;

typedef enum {
    FA_IGNORE,
    FA_PRINT,
    FA_STUFF,
    FA_KICK,

    FA_MAX
} filteraction_t;

typedef struct {
    list_t          entry;
    filteraction_t  action;
    char            *comment;
    char            string[1];
} filtercmd_t;

typedef struct server_static_s {
    qboolean    initialized;        // sv_init has completed
    unsigned    realtime;            // always increasing, no clamping, etc

    client_t    *udp_client_pool;     // [maxclients]
    list_t      udp_client_list;         // linked list of non-free clients

    unsigned        numEntityStates; // maxclients*UPDATE_BACKUP*MAX_PACKET_ENTITIES
    unsigned        nextEntityStates;    // next entityState to use
    entity_state_t  *entityStates;        // [numEntityStates]

#if USE_ZLIB
    z_stream        z; // for compressing messages at once
#endif

    unsigned        last_heartbeat;

    ratelimit_t     ratelimit_status;
    ratelimit_t     ratelimit_badpass;
    ratelimit_t     ratelimit_badrcon;

    challenge_t    challenges[MAX_CHALLENGES];    // to prevent invalid IPs from connecting
} server_static_t;

//=============================================================================

extern netadr_t    master_adr[MAX_MASTERS];    // address of the master server

extern list_t      sv_banlist;
extern list_t      sv_blacklist;

extern list_t      sv_cmdlist_connect;
extern list_t      sv_cmdlist_begin;

extern list_t      sv_filterlist;

extern server_static_t     svs;        // persistant server info
extern server_t            sv;         // local server

extern pmoveParams_t    sv_pmp;

extern cvar_t       *sv_hostname;
extern cvar_t       *sv_maxclients;
extern cvar_t       *sv_password;
extern cvar_t       *sv_reserved_slots;
extern cvar_t       *sv_airaccelerate;        // development tool
extern cvar_t       *sv_qwmod;                // atu QW Physics modificator                                            
extern cvar_t       *sv_enforcetime;
#if USE_FPS
extern cvar_t       *sv_fps;
#endif
extern cvar_t       *sv_force_reconnect;
extern cvar_t       *sv_iplimit;

#ifdef _DEBUG
extern cvar_t       *sv_debug;
extern cvar_t       *sv_pad_packets;
#endif
extern cvar_t       *sv_novis;
extern cvar_t       *sv_lan_force_rate;
extern cvar_t       *sv_calcpings_method;
extern cvar_t       *sv_changemapcmd;

extern cvar_t       *sv_strafejump_hack;
#ifndef _WIN32
extern cvar_t       *sv_oldgame_hack;
#endif
#if USE_PACKETDUP
extern cvar_t       *sv_packetdup_hack;
#endif
extern cvar_t       *sv_allow_map;
#if !USE_CLIENT
extern cvar_t       *sv_recycle;
#endif

extern cvar_t       *sv_status_limit;
extern cvar_t       *sv_status_show;
extern cvar_t       *sv_badauth_time;
extern cvar_t       *sv_uptime;

extern cvar_t       *g_features;

extern cvar_t       *sv_timeout;
extern cvar_t       *sv_zombietime;
extern cvar_t       *sv_ghostime;

extern client_t     *sv_client;
extern edict_t      *sv_player;


//===========================================================

//
// sv_main.c
//
void SV_DropClient( client_t *drop, const char *reason );
void SV_RemoveClient( client_t *client );
void SV_CleanClient( client_t *client );

void SV_InitOperatorCommands (void);

void SV_UserinfoChanged (client_t *cl);
void SV_UpdateUserinfo( char *userinfo );

qboolean SV_RateLimited( ratelimit_t *r );
void SV_RateInit( ratelimit_t *r, int limit, int period );

addrmatch_t *SV_MatchAddress( list_t *list, netadr_t *address );

int SV_CountClients( void );

#if USE_ZLIB
voidpf SV_Zalloc OF(( voidpf opaque, uInt items, uInt size ));
void SV_Zfree OF(( voidpf opaque, voidpf address ));
#endif

void Master_Heartbeat (void);
void Master_Packet (void);

//
// sv_init.c
//
void SV_InitGame( qboolean ismvd );
void SV_Map (const char *levelstring, qboolean restart);
void SV_ClientReset( client_t *client );

//
// sv_send.c
//
typedef enum {RD_NONE, RD_CLIENT, RD_PACKET} redirect_t;
#define    SV_OUTPUTBUF_LENGTH    (MAX_PACKETLEN_DEFAULT - 16)

#define SV_BeginRedirect( target ) \
    Com_BeginRedirect( target, sv_outputbuf, SV_OUTPUTBUF_LENGTH, SV_FlushRedirect )

extern    char    sv_outputbuf[SV_OUTPUTBUF_LENGTH];

void SV_FlushRedirect( int redirected, char *outputbuf, size_t len );

void SV_SendClientMessages (void);
void SV_SendAsyncPackets( void );

void SV_Multicast (vec3_t origin, multicast_t to);
void SV_ClientPrintf( client_t *cl, int level, const char *fmt, ... ) q_printf( 3, 4 );
void SV_BroadcastPrintf( int level, const char *fmt, ... ) q_printf( 2, 3 );
void SV_ClientCommand( client_t *cl, const char *fmt, ... ) q_printf( 2, 3 );
void SV_BroadcastCommand( const char *fmt, ... ) q_printf( 1, 2 );
void SV_ClientAddMessage( client_t *client, int flags );
void SV_ShutdownClientSend( client_t *client );
void SV_InitClientSend( client_t *newcl );

#if USE_MVD_SERVER

//
// sv_mvd.c
//
void SV_MvdRegister( void );
void SV_MvdInit( void );
void SV_MvdShutdown( killtype_t type );
void SV_MvdBeginFrame( void );
void SV_MvdEndFrame( void );
void SV_MvdRunClients( void );
void SV_MvdStatus_f( void );
void SV_MvdMapChanged( void );
void SV_MvdClientDropped( client_t *client, const char *reason );

void SV_MvdUnicast( edict_t *ent, int clientNum, qboolean reliable );
void SV_MvdMulticast( int leafnum, multicast_t to );
void SV_MvdConfigstring( int index, const char *string, size_t len );
void SV_MvdBroadcastPrint( int level, const char *string );
void SV_MvdStartSound( int entnum, int channel, int flags,
                        int soundindex, int volume,
                        int attenuation, int timeofs );
#endif // USE_MVD_SERVER

#if USE_AC_SERVER

// 
// sv_ac.c
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

#endif // USE_AC_SERVER

//
// sv_user.c
//
void SV_New_f( void );
void SV_Begin_f( void );
void SV_Nextserver (void);
void SV_ExecuteClientMessage (client_t *cl);
void SV_CloseDownload( client_t *client );

//
// sv_ccmds.c
//
void SV_AddMatch_f( list_t *list );
void SV_DelMatch_f( list_t *list );
void SV_ListMatches_f( list_t *list );
client_t *SV_EnhancedSetPlayer( char *s );

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
extern    game_export_t    *ge;

void SV_InitGameProgs( void );
void SV_ShutdownGameProgs (void);
void SV_InitEdict (edict_t *e);

void PF_Pmove( pmove_t *pm );

#if USE_CLIENT
//
// sv_save.c
//
void SV_Savegame_f( void );
void SV_Loadgame_f( void );
#endif

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

trace_t SV_Trace_Native (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end,
    edict_t *passedict, int contentmask);
trace_t *SV_Trace (trace_t *trace, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end,
    edict_t *passedict, int contentmask);
// mins and maxs are relative

// if the entire move stays in a solid volume, trace.allsolid will be set,
// trace.startsolid will be set, and trace.fraction will be 0

// if the starting point is in a solid, it will be allowed to move out
// to an open area

// passedict is explicitly excluded from clipping checks (normally NULL)

