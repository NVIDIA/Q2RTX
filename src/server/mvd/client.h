/*
Copyright (C) 2003-2006 Andrey Nazarov

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

#include "../server.h"
#include <setjmp.h>

#define MVD_Malloc(size)    Z_TagMalloc(size, TAG_MVD)
#define MVD_Mallocz(size)   Z_TagMallocz(size, TAG_MVD)
#define MVD_CopyString(s)   Z_TagCopyString(s, TAG_MVD)

#define FOR_EACH_MVD(mvd) \
    LIST_FOR_EACH(mvd_t, mvd, &mvd_channel_list, entry)

#define FOR_EACH_MVDCL(cl, mvd) \
    LIST_FOR_EACH(mvd_client_t, cl, &(mvd)->clients, entry)

#define EDICT_MVDCL(ent)  ((mvd_client_t *)((ent)->client))
#define CS_NUM(c, n)      ((char *)(c) + (n) * MAX_QPATH)

#define MVD_InfoSet(var, val) \
    Cvar_FullSet(var, val, CVAR_SERVERINFO | CVAR_GAME, FROM_CODE)

// game features MVD client supports
#define MVD_FEATURES    (GMF_CLIENTNUM | GMF_PROPERINUSE | GMF_WANT_ALL_DISCONNECTS)

#define LAYOUT_MSEC     3000

typedef enum {
    LAYOUT_NONE,        // no layout at all
    LAYOUT_FOLLOW,      // display chase target name
    LAYOUT_SCORES,      // layout of the MVD dummy
    LAYOUT_OLDSCORES,   // saved at intermission time
    LAYOUT_MENU,        // MVD main menu
    LAYOUT_CLIENTS,     // MVD clients list
    LAYOUT_CHANNELS     // MVD channel list
} mvd_layout_t;

#define FLOOD_SAMPLES   16
#define FLOOD_MASK      (FLOOD_SAMPLES - 1)

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
    qboolean        admin;
    qboolean        notified;
    unsigned        begin_time;
    float           fov;
    int             uf;

    mvd_player_t    *target;
    mvd_player_t    *oldtarget;
    int             chase_mask;
    qboolean        chase_auto;
    qboolean        chase_wait;
    byte            chase_bitmap[MAX_CLIENTS / CHAR_BIT];

    mvd_layout_t    layout_type;
    unsigned        layout_time;
    int             layout_cursor;

    unsigned    floodSamples[FLOOD_SAMPLES];
    unsigned    floodHead;
    unsigned    floodTime;

    usercmd_t lastcmd;
    //short delta_angles[3];
    int jump_held;
} mvd_client_t;

#define MAX_MVD_NAME    16

typedef enum {
    MVD_DEAD,       // no gamestate received yet, unusable for observers
    MVD_WAITING,    // buffering more frames, stalled
    MVD_READING,    // reading frames

    MVD_NUM_STATES
} mvd_state_t;

typedef struct {
    list_t entry;
    int framenum;
    off_t filepos;
    size_t msglen;
    byte data[1];
} mvd_snap_t;

struct gtv_s;

// FIXME: entire struct is > 500 kB in size!
// need to eliminate those large static arrays below...
typedef struct mvd_s {
    list_t      entry;

    mvd_state_t     state;
    int             id;
    char            name[MAX_MVD_NAME];
    struct gtv_s    *gtv;
    qboolean        (*read_frame)(struct mvd_s *);
    qboolean        (*forward_cmd)(mvd_client_t *);

    // demo related variables
    qhandle_t   demorecording;
    char        *demoname;
    qboolean    demoseeking;
    int         last_snapshot;
    list_t      snapshots;

    // delay buffer
    fifo_t      delay;
    size_t      msglen;
    unsigned    num_packets, min_packets;
    unsigned    underflows, overflows;
    int         framenum;

    // game state
    char    gamedir[MAX_QPATH];
    char    mapname[MAX_QPATH];
    int     servercount;
    int     maxclients;
    edict_pool_t pool;
    cm_t    cm;
    vec3_t  spawnOrigin;
    vec3_t  spawnAngles;
    int     pm_type;
    byte            dcs[CS_BITMAP_BYTES];
    char            baseconfigstrings[MAX_CONFIGSTRINGS][MAX_QPATH];
    char            configstrings[MAX_CONFIGSTRINGS][MAX_QPATH];
    edict_t         edicts[MAX_EDICTS];
    mvd_player_t    *players; // [maxclients]
    mvd_player_t    *dummy; // &players[clientNum]
    int             numplayers; // number of active players in frame
    int             clientNum;
    mvd_flags_t     flags;
    char        layout[MAX_NET_STRING];
    char        oldscores[MAX_NET_STRING]; // layout is copied here
    qboolean    intermission;
    qboolean    dirty;

    // UDP client list
    list_t      clients;
} mvd_t;


//
// mvd_client.c
//

extern list_t           mvd_channel_list;
extern mvd_t            mvd_waitingRoom;
extern qboolean         mvd_dirty;

extern qboolean     mvd_active;
extern unsigned     mvd_last_activity;

extern jmp_buf  mvd_jmpbuf;

#ifdef _DEBUG
extern cvar_t    *mvd_shownet;
#endif

void MVD_Destroyf(mvd_t *mvd, const char *fmt, ...) q_noreturn q_printf(2, 3);
void MVD_Shutdown(void);

mvd_t *MVD_SetChannel(int arg);

void MVD_File_g(genctx_t *ctx);

void MVD_Spawn(void);

void MVD_StopRecord(mvd_t *mvd);

void MVD_StreamedStop_f(void);
void MVD_StreamedRecord_f(void);

void MVD_Register(void);
int MVD_Frame(void);

//
// mvd_parse.c
//

qboolean MVD_ParseMessage(mvd_t *mvd);
void MVD_ParseEntityString(mvd_t *mvd, const char *data);
void MVD_ClearState(mvd_t *mvd, qboolean full);

//
// mvd_game.c
//

extern mvd_client_t     *mvd_clients;   // [maxclients]

void MVD_SwitchChannel(mvd_client_t *client, mvd_t *mvd);
void MVD_RemoveClient(client_t *client);
void MVD_BroadcastPrintf(mvd_t *mvd, int level,
                         int mask, const char *fmt, ...) q_printf(4, 5);
void MVD_PrepWorldFrame(void);
void MVD_GameClientNameChanged(edict_t *ent, const char *name);
void MVD_GameClientDrop(edict_t *ent, const char *prefix, const char *reason);
void MVD_UpdateClients(mvd_t *mvd);
void MVD_FreePlayer(mvd_player_t *player);
void MVD_UpdateConfigstring(mvd_t *mvd, int index);
void MVD_SetPlayerNames(mvd_t *mvd);
void MVD_LinkEdict(mvd_t *mvd, edict_t *ent);

