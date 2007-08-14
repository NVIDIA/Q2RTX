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

#define MVD_MAGIC   MakeLong( 'M', 'V', 'D', '2' )

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
	SBOARD_NONE,		// no layout at all
	SBOARD_FOLLOW,		// layout of player being followed
	SBOARD_CLIENTS,		// MVD clients list
	SBOARD_SCORES,		// current scores
    SBOARD_CHANNELS     // MVD channel list
} scoreboard_t;

#define FLOOD_SAMPLES	16
#define	FLOOD_MASK		( FLOOD_SAMPLES - 1 )

typedef struct {
/* =================== */
    player_state_t ps;
    int ping;
/* =================== */

    list_t entry;
    struct mvd_s *mvd;
    qboolean admin;
	qboolean following;
	qboolean savedFollowing;
	qboolean connected;
    int lastframe;
	int followClientNum;
	int savedClientNum;
    int clientNum;
	int pmflags;
	client_t *cl;
	float fov;
    int cursor;
	scoreboard_t scoreboard;
	int layoutTime;
	int floodSamples[FLOOD_SAMPLES];
	int floodHead;
	int floodTime;
	usercmd_t lastcmd;
	short delta_angles[3];
} udpClient_t;

typedef struct {
	int				number;
	int				serverFrame;
	int				firstEntity, numEntities;
	int				firstPlayer, numPlayers;
} mvdFrame_t;

#define MVD_UPDATE_BACKUP       2
#define MVD_UPDATE_MASK         ( MVD_UPDATE_BACKUP - 1 )

#define MVD_ENTITIES_BACKUP     ( MAX_EDICTS * MVD_UPDATE_BACKUP )
#define MVD_ENTITIES_MASK       ( MVD_ENTITIES_BACKUP - 1 )

#define MVD_PLAYERS_BACKUP      ( MAX_CLIENTS * MVD_UPDATE_BACKUP )
#define MVD_PLAYERS_MASK        ( MVD_PLAYERS_BACKUP - 1 )


typedef struct mvdConfigstring_s {
	struct mvdConfigstring_s *next;
	int index;
	char string[1];
} mvdConfigstring_t;

typedef struct {
	entity_state_t s;
	qboolean	linked;
	int			num_clusters;
	int			clusternums[MAX_ENT_CLUSTERS];
	cnode_t		*headnode;
	int			areanum, areanum2;
} entityStateEx_t;

typedef struct {
	char *layout;
	mvdConfigstring_t *configstrings;
} mvdPlayer_t;

typedef struct mvd_s {
    list_t      entry;
    list_t      ready;
    char        name[MAX_QPATH];

	// demo related variables
	fileHandle_t	demofile;
	qboolean	demoplayback;
	qboolean	demorecording;
	int			demofileSize;
	int			demofileFrameOffset;
	int         demofilePercent;
	char		demopath[MAX_QPATH];

	// connection variables
	mvdState_t	state;
	int			servercount;
	int			clientNum;
    netstream_t stream;
    char        response[MAX_NET_STRING];
    int         responseLength;
    int         contentLength;
    int         statusCode;
    char        statusText[MAX_QPATH];
    int         msglen;
#if USE_ZLIB
    z_stream    z;
#endif
    fifo_t      zbuf;

	// delta decoder variables
	int		        nextEntityStates, nextPlayerStates;
	entityStateEx_t	entityStates[MVD_ENTITIES_BACKUP];
	player_state_t	playerStates[MVD_PLAYERS_BACKUP];
	
	mvdFrame_t	frames[MVD_UPDATE_BACKUP];
    int         framenum;

    // game state
    char    gamedir[MAX_QPATH];
    char    mapname[MAX_QPATH];
    int     maxclients;
	cm_t    cm;
	vec3_t  spawnOrigin;
	vec3_t  spawnAngles;
	qboolean        spawnSet;
	char            configstrings[MAX_CONFIGSTRINGS][MAX_QPATH];
	entityStateEx_t *baselines[SV_BASELINES_CHUNKS];
    mvdPlayer_t     players[MAX_CLIENTS];

	// client lists
    list_t udpClients;
    list_t tcpClients;
} mvd_t;


//
// mvd_client.c
//

extern list_t           mvd_channels;
extern list_t           mvd_ready;
extern mvd_t            mvd_waitingRoom;

extern cvar_t	*mvd_shownet;
extern cvar_t	*mvd_debug;
extern cvar_t	*mvd_pause;
extern cvar_t	*mvd_nextserver;
extern cvar_t	*mvd_timeout;
extern cvar_t	*mvd_autoscores;
extern cvar_t	*mvd_safecmd;

void MVD_DPrintf( const char *fmt, ... ) q_printf( 1, 2 );
void MVD_Drop( mvd_t *mvd, const char *fmt, ... )
    q_noreturn q_printf( 2, 3 );
void MVD_Destroy( mvd_t *mvd, const char *fmt, ... )
    q_noreturn q_printf( 2, 3 );
void MVD_Disconnect( mvd_t *mvd );
void MVD_ClearState( mvd_t *mvd );
void MVD_ChangeLevel( mvd_t *mvd ); 
void MVD_GetStream( const char *uri );
void MVD_Free( mvd_t *mvd ); 

const char *MVD_Play_g( const char *partial, int state );

void MVD_Connect_f( void );
void MVD_Spawn_f( void ); 

void MVD_StreamedStop_f( void );
void MVD_StreamedRecord_f( void );

void MVD_Register( void );
void MVD_Frame( void ); 

//
// mvd_parse.c
//

qboolean MVD_Parse( mvd_t *mvd ); 


//
// mvd_game.c
//

extern udpClient_t      *mvd_clients;	/* [svs.maxclients] */
extern game_export_t	mvd_ge;

void MVD_UpdateFollower( udpClient_t *client, player_state_t *src );
void MVD_SwitchChannel( udpClient_t *client, mvd_t *mvd ); 

