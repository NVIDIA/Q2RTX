/*
Copyright (C) 2003-2006 Andrey Nazarov

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

#define MVD_DEBUG( s ) do { if( mvd_debug->integer ) \
    Com_Printf( S_COLOR_BLUE "%s: %s", __func__, s ); } while( 0 )

#define MVD_Malloc( size )     Z_TagMalloc( size, TAG_MVD )
#define MVD_Mallocz( size )     Z_TagMallocz( size, TAG_MVD )
#define MVD_CopyString( s )		Z_TagCopyString( s, TAG_MVD )

#define EDICT_MVDCL( ent )  (( udpClient_t * )( (ent)->client ))
#define CS_NUM( c, n )      ( ( char * )(c) + (n) * MAX_QPATH )

typedef enum {
    MVD_DEAD,       // not active at all
	MVD_CONNECTING, // connect() in progress
	MVD_CONNECTED,  // HTTP request sent
    MVD_CHECKING,   // got response, checking magic
    MVD_PREPARING,  // got magic, waiting for gamestate
    MVD_WAITING,    // stalled, buffering more data
    MVD_READING,    // actively running
	MVD_DISCONNECTED // disconnected, running until EOB
} mvdState_t;

#define LAYOUT_MSEC		3000

typedef enum {
	LAYOUT_NONE,		// no layout at all
    LAYOUT_FOLLOW,      // display chase target name
	LAYOUT_SCORES,		// layout of the MVD dummy
    LAYOUT_MENU,        // MVD main menu
	LAYOUT_CLIENTS,		// MVD clients list
    LAYOUT_CHANNELS     // MVD channel list
} mvd_layout_t;

#define FLOOD_SAMPLES	16
#define	FLOOD_MASK		( FLOOD_SAMPLES - 1 )


typedef struct mvd_cs_s {
	struct mvd_cs_s *next;
	int index;
	char string[1];
} mvd_cs_t;

typedef struct {
    player_state_t ps;
    qboolean inuse;
    char name[16];
	mvd_cs_t *configstrings;
} mvd_player_t;

typedef struct {
/* =================== */
    player_state_t ps;
    int ping;
    int clientNum;
/* =================== */

    list_t          entry;
    struct mvd_s    *mvd;
	client_t        *cl;
    qboolean    admin;
	unsigned    begin_time;
	mvd_player_t *target, *oldtarget;
	float       fov;
    int         uf;

	mvd_layout_t    layout_type;
	unsigned        layout_time;
    int             layout_cursor;

	unsigned    floodSamples[FLOOD_SAMPLES];
	unsigned    floodHead;
	unsigned    floodTime;

	usercmd_t lastcmd;
	//short delta_angles[3];
    qboolean jump_held;
} udpClient_t;

typedef struct mvd_s {
    list_t      entry;
    list_t      ready;
    list_t      active;

    int         id;
    char        name[MAX_QPATH];

	// demo related variables
	fileHandle_t	demoplayback;
	fileHandle_t	demorecording;
    int             demoloop;
    string_entry_t  *demohead, *demoentry;

	// connection variables
	mvdState_t	state;
	int			servercount;
	int			clientNum;
    netstream_t stream;
	char		address[MAX_QPATH];
    char        response[MAX_NET_STRING];
    size_t      responseLength;
    size_t      contentLength;
    htcoding_t  contentCoding;
    int         statusCode;
    char        statusText[MAX_QPATH];
    size_t      msglen;
#if USE_ZLIB
    z_stream    z;
#endif
    fifo_t      zbuf;
    unsigned    framenum;
    unsigned    lastReceived;
    unsigned    waitTime, waitDelay;

    // game state
    char    gamedir[MAX_QPATH];
    char    mapname[MAX_QPATH];
    int     maxclients;
    edict_pool_t pool;
	cm_t    cm;
	vec3_t  spawnOrigin;
	vec3_t  spawnAngles;
    int     pm_type;
	char            configstrings[MAX_CONFIGSTRINGS][MAX_QPATH];
    edict_t         edicts[MAX_EDICTS];
    mvd_player_t    *players; // [maxclients]
    mvd_player_t    *dummy; // &players[clientNum]
    int             numplayers; // number of active players in frame
    char        layout[MAX_STRING_CHARS];
    qboolean    intermission;

	// client lists
    list_t udpClients;
    list_t tcpClients;
} mvd_t;


//
// mvd_client.c
//

extern list_t           mvd_channels;
extern list_t           mvd_ready;
extern list_t           mvd_active;
extern mvd_t            mvd_waitingRoom;
extern qboolean         mvd_dirty;

extern cvar_t	*mvd_shownet;
extern cvar_t	*mvd_debug;
extern cvar_t	*mvd_pause;
extern cvar_t	*mvd_nextserver;
extern cvar_t	*mvd_timeout;
extern cvar_t	*mvd_autoscores;
extern cvar_t	*mvd_safecmd;

void MVD_DPrintf( const char *fmt, ... ) q_printf( 1, 2 );
void MVD_Dropf( mvd_t *mvd, const char *fmt, ... )
    q_noreturn q_printf( 2, 3 );
void MVD_Destroyf( mvd_t *mvd, const char *fmt, ... )
    q_noreturn q_printf( 2, 3 );
void MVD_Disconnect( mvd_t *mvd );
void MVD_BeginWaiting( mvd_t *mvd );
void MVD_ClearState( mvd_t *mvd );
void MVD_ChangeLevel( mvd_t *mvd ); 
void MVD_Finish( mvd_t *mvd, const char *reason ) q_noreturn;
void MVD_GetStream( const char *uri );
void MVD_GetStatus( void );
void MVD_Free( mvd_t *mvd ); 
void MVD_Shutdown( void );

mvd_t *MVD_SetChannel( int arg );

void MVD_File_g( genctx_t *ctx );

void MVD_Connect_f( void );
void MVD_Spawn_f( void ); 

void MVD_StreamedStop_f( void );
void MVD_StreamedRecord_f( void );

void MVD_Register( void );
int MVD_Frame( void ); 

//
// mvd_parse.c
//

qboolean MVD_Parse( mvd_t *mvd ); 
void MVD_ParseEntityString( mvd_t *mvd, const char *data );


//
// mvd_game.c
//

extern udpClient_t      *mvd_clients;	/* [svs.maxclients] */
extern game_export_t	mvd_ge;

void MVD_UpdateClient( udpClient_t *client );
void MVD_SwitchChannel( udpClient_t *client, mvd_t *mvd );
void MVD_RemoveClient( client_t *client );
void MVD_BroadcastPrintf( mvd_t *mvd, int level,
    int mask, const char *fmt, ... ) q_printf( 4, 5 );
void MVD_PrepWorldFrame( void );
void MVD_GameClientNameChanged( edict_t *ent, const char *name );
void MVD_GameClientDrop( edict_t *ent, const char *reason );
void MVD_CheckActive( mvd_t *mvd );

