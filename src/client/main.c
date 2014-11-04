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
// cl_main.c  -- client main loop

#include "client.h"

cvar_t  *rcon_address;

cvar_t  *cl_noskins;
cvar_t  *cl_footsteps;
cvar_t  *cl_timeout;
cvar_t  *cl_predict;
cvar_t  *cl_gun;
cvar_t  *cl_gunalpha;
cvar_t  *cl_maxfps;
cvar_t  *cl_async;
cvar_t  *r_maxfps;
cvar_t  *cl_autopause;

cvar_t  *cl_kickangles;
cvar_t  *cl_rollhack;
cvar_t  *cl_noglow;
cvar_t  *cl_nolerp;

#ifdef _DEBUG
cvar_t  *cl_shownet;
cvar_t  *cl_showmiss;
cvar_t  *cl_showclamp;
#endif

cvar_t  *cl_thirdperson;
cvar_t  *cl_thirdperson_angle;
cvar_t  *cl_thirdperson_range;

cvar_t  *cl_disable_particles;
cvar_t  *cl_disable_explosions;
cvar_t  *cl_chat_notify;
cvar_t  *cl_chat_sound;
cvar_t  *cl_chat_filter;

cvar_t  *cl_disconnectcmd;
cvar_t  *cl_changemapcmd;
cvar_t  *cl_beginmapcmd;

cvar_t  *cl_gibs;
#if USE_FPS
cvar_t  *cl_updaterate;
#endif

cvar_t  *cl_protocol;

cvar_t  *gender_auto;

cvar_t  *cl_vwep;

//
// userinfo
//
cvar_t  *info_password;
cvar_t  *info_spectator;
cvar_t  *info_name;
cvar_t  *info_skin;
cvar_t  *info_rate;
cvar_t  *info_fov;
cvar_t  *info_msg;
cvar_t  *info_hand;
cvar_t  *info_gender;
cvar_t  *info_uf;

#if USE_REF == REF_GL
extern cvar_t *gl_modulate_world;
extern cvar_t *gl_modulate_entities;
extern cvar_t *gl_brightness;
#endif

client_static_t cls;
client_state_t  cl;

centity_t   cl_entities[MAX_EDICTS];

// used for executing stringcmds
cmdbuf_t    cl_cmdbuf;
char        cl_cmdbuf_text[MAX_STRING_CHARS];

//======================================================================

typedef enum {
    REQ_FREE,
    REQ_STATUS_CL,
    REQ_STATUS_UI,
    REQ_INFO,
    REQ_RCON
} requestType_t;

typedef struct {
    requestType_t type;
    netadr_t adr;
    unsigned time;
} request_t;

#define MAX_REQUESTS    64
#define REQUEST_MASK    (MAX_REQUESTS - 1)

static request_t    clientRequests[MAX_REQUESTS];
static unsigned     nextRequest;

static request_t *CL_AddRequest(const netadr_t *adr, requestType_t type)
{
    request_t *r;

    r = &clientRequests[nextRequest++ & REQUEST_MASK];
    r->adr = *adr;
    r->type = type;
    r->time = cls.realtime;

    return r;
}

static request_t *CL_FindRequest(void)
{
    request_t *r;
    int i, count;

    count = MAX_REQUESTS;
    if (count > nextRequest)
        count = nextRequest;

    // find the most recent request sent to this address
    for (i = 0; i < count; i++) {
        r = &clientRequests[(nextRequest - i - 1) & REQUEST_MASK];
        if (!r->type) {
            continue;
        }
        if (r->adr.type == NA_BROADCAST) {
            if (cls.realtime - r->time > 3000) {
                continue;
            }
            if (!NET_IsLanAddress(&net_from)) {
                continue;
            }
        } else {
            if (cls.realtime - r->time > 6000) {
                break;
            }
            if (!NET_IsEqualBaseAdr(&net_from, &r->adr)) {
                continue;
            }
        }

        return r;
    }

    return NULL;
}

//======================================================================

static void CL_UpdateGunSetting(void)
{
    int nogun;

    if (!cls.netchan) {
        return;
    }
    if (cls.serverProtocol < PROTOCOL_VERSION_R1Q2) {
        return;
    }

    if (cl_gun->integer == -1) {
        nogun = 2;
    } else if (cl_gun->integer == 0 || info_hand->integer == 2) {
        nogun = 1;
    } else {
        nogun = 0;
    }

    MSG_WriteByte(clc_setting);
    MSG_WriteShort(CLS_NOGUN);
    MSG_WriteShort(nogun);
    MSG_FlushTo(&cls.netchan->message);
}

static void CL_UpdateGibSetting(void)
{
    if (!cls.netchan) {
        return;
    }
    if (cls.serverProtocol != PROTOCOL_VERSION_Q2PRO) {
        return;
    }

    MSG_WriteByte(clc_setting);
    MSG_WriteShort(CLS_NOGIBS);
    MSG_WriteShort(!cl_gibs->integer);
    MSG_FlushTo(&cls.netchan->message);
}

static void CL_UpdateFootstepsSetting(void)
{
    if (!cls.netchan) {
        return;
    }
    if (cls.serverProtocol != PROTOCOL_VERSION_Q2PRO) {
        return;
    }

    MSG_WriteByte(clc_setting);
    MSG_WriteShort(CLS_NOFOOTSTEPS);
    MSG_WriteShort(!cl_footsteps->integer);
    MSG_FlushTo(&cls.netchan->message);
}

static void CL_UpdatePredictSetting(void)
{
    if (!cls.netchan) {
        return;
    }
    if (cls.serverProtocol != PROTOCOL_VERSION_Q2PRO) {
        return;
    }

    MSG_WriteByte(clc_setting);
    MSG_WriteShort(CLS_NOPREDICT);
    MSG_WriteShort(!cl_predict->integer);
    MSG_FlushTo(&cls.netchan->message);
}

#if USE_FPS
static void CL_UpdateRateSetting(void)
{
    if (!cls.netchan) {
        return;
    }
    if (cls.serverProtocol != PROTOCOL_VERSION_Q2PRO) {
        return;
    }

    MSG_WriteByte(clc_setting);
    MSG_WriteShort(CLS_FPS);
    MSG_WriteShort(cl_updaterate->integer);
    MSG_FlushTo(&cls.netchan->message);
}
#endif

void CL_UpdateRecordingSetting(void)
{
    int rec;

    if (!cls.netchan) {
        return;
    }
    if (cls.serverProtocol < PROTOCOL_VERSION_R1Q2) {
        return;
    }

    if (cls.demo.recording) {
        rec = 1;
    } else {
        rec = 0;
    }

#if USE_CLIENT_GTV
    if (cls.gtv.state == ca_active) {
        rec |= 1;
    }
#endif

    MSG_WriteByte(clc_setting);
    MSG_WriteShort(CLS_RECORDING);
    MSG_WriteShort(rec);
    MSG_FlushTo(&cls.netchan->message);
}

/*
===================
CL_ClientCommand
===================
*/
void CL_ClientCommand(const char *string)
{
    if (!cls.netchan) {
        return;
    }

    Com_DDPrintf("%s: %s\n", __func__, string);

    MSG_WriteByte(clc_stringcmd);
    MSG_WriteString(string);
    MSG_FlushTo(&cls.netchan->message);
}

/*
===================
CL_ForwardToServer

adds the current command line as a clc_stringcmd to the client message.
things like godmode, noclip, etc, are commands directed to the server,
so when they are typed in at the console, they will need to be forwarded.
===================
*/
qboolean CL_ForwardToServer(void)
{
    char    *cmd;

    cmd = Cmd_Argv(0);
    if (cls.state != ca_active || *cmd == '-' || *cmd == '+') {
        return qfalse;
    }

    CL_ClientCommand(Cmd_RawArgsFrom(0));
    return qtrue;
}

/*
==================
CL_ForwardToServer_f
==================
*/
static void CL_ForwardToServer_f(void)
{
    if (cls.state < ca_connected) {
        Com_Printf("Can't \"%s\", not connected\n", Cmd_Argv(0));
        return;
    }

    if (cls.demo.playback) {
        return;
    }

    // don't forward the first argument
    if (Cmd_Argc() > 1) {
        CL_ClientCommand(Cmd_RawArgs());
    }
}

/*
==================
CL_Pause_f
==================
*/
static void CL_Pause_f(void)
{
#if USE_MVD_CLIENT
    if (sv_running->integer == ss_broadcast) {
        Cbuf_InsertText(&cmd_buffer, "mvdpause @@\n");
        return;
    }
#endif

    // activate manual pause
    if (cl_paused->integer == 2) {
        Cvar_Set("cl_paused", "0");
    } else {
        Cvar_Set("cl_paused", "2");
    }

    CL_CheckForPause();
}

/*
=================
CL_CheckForResend

Resend a connect message if the last one has timed out
=================
*/
void CL_CheckForResend(void)
{
    char tail[MAX_QPATH];
    char userinfo[MAX_INFO_STRING];
    int maxmsglen;

    if (cls.demo.playback) {
        return;
    }

    // if the local server is running and we aren't
    // then connect
    if (cls.state < ca_connecting && sv_running->integer > ss_loading) {
        strcpy(cls.servername, "localhost");
        cls.serverAddress.type = NA_LOOPBACK;
        cls.serverProtocol = cl_protocol->integer;
        if (cls.serverProtocol < PROTOCOL_VERSION_DEFAULT ||
            cls.serverProtocol > PROTOCOL_VERSION_Q2PRO) {
            cls.serverProtocol = PROTOCOL_VERSION_Q2PRO;
        }

        // we don't need a challenge on the localhost
        cls.state = ca_connecting;
        cls.connect_time -= CONNECT_FAST;
        cls.connect_count = 0;

        cls.passive = qfalse;

        Con_Popup(qtrue);
        UI_OpenMenu(UIMENU_NONE);
    }

    // resend if we haven't gotten a reply yet
    if (cls.state != ca_connecting && cls.state != ca_challenging) {
        return;
    }

    if (cls.realtime - cls.connect_time < CONNECT_DELAY) {
        return;
    }

    cls.connect_time = cls.realtime;    // for retransmit requests
    cls.connect_count++;

    if (cls.state == ca_challenging) {
        Com_Printf("Requesting challenge... %i\n", cls.connect_count);
        OOB_PRINT(NS_CLIENT, &cls.serverAddress, "getchallenge\n");
        return;
    }

    //
    // We have gotten a challenge from the server, so try and connect.
    //
    Com_Printf("Requesting connection... %i\n", cls.connect_count);

    cls.userinfo_modified = 0;

    // use maximum allowed msglen for loopback
    maxmsglen = net_maxmsglen->integer;
    if (NET_IsLocalAddress(&cls.serverAddress)) {
        maxmsglen = MAX_PACKETLEN_WRITABLE;
    }

    // add protocol dependent stuff
    switch (cls.serverProtocol) {
    case PROTOCOL_VERSION_R1Q2:
        Q_snprintf(tail, sizeof(tail), " %d %d",
                   maxmsglen, PROTOCOL_VERSION_R1Q2_CURRENT);
        cls.quakePort = net_qport->integer & 0xff;
        break;
    case PROTOCOL_VERSION_Q2PRO:
        Q_snprintf(tail, sizeof(tail), " %d %d %d %d",
                   maxmsglen, net_chantype->integer, USE_ZLIB,
                   PROTOCOL_VERSION_Q2PRO_CURRENT);
        cls.quakePort = net_qport->integer & 0xff;
        break;
    default:
        tail[0] = 0;
        cls.quakePort = net_qport->integer;
        break;
    }

    Cvar_BitInfo(userinfo, CVAR_USERINFO);
    Netchan_OutOfBand(NS_CLIENT, &cls.serverAddress,
                      "connect %i %i %i \"%s\"%s\n", cls.serverProtocol, cls.quakePort,
                      cls.challenge, userinfo, tail);
}

static void CL_RecentIP_g(genctx_t *ctx)
{
    netadr_t *a;
    int i, j;

    j = cls.recent_head - RECENT_ADDR;
    if (j < 0) {
        j = 0;
    }
    for (i = cls.recent_head - 1; i >= j; i--) {
        a = &cls.recent_addr[i & RECENT_MASK];
        if (a->type) {
            Prompt_AddMatch(ctx, NET_AdrToString(a));
        }
    }
}

static void CL_Connect_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        CL_RecentIP_g(ctx);
        Com_Address_g(ctx);
    } else if (argnum == 2) {
        if (!ctx->partial[0] || (ctx->partial[0] == '3' && !ctx->partial[1])) {
            Prompt_AddMatch(ctx, "34");
            Prompt_AddMatch(ctx, "35");
            Prompt_AddMatch(ctx, "36");
        }
    }
}

/*
================
CL_Connect_f

================
*/
static void CL_Connect_f(void)
{
    char    *server, *p;
    netadr_t    address;
    int protocol;
    int argc = Cmd_Argc();

    if (argc < 2) {
usage:
        Com_Printf("Usage: %s <server> [34|35|36]\n", Cmd_Argv(0));
        return;
    }

    if (argc > 2) {
        protocol = atoi(Cmd_Argv(2));
        if (protocol < PROTOCOL_VERSION_DEFAULT ||
            protocol > PROTOCOL_VERSION_Q2PRO) {
            goto usage;
        }
    } else {
        protocol = cl_protocol->integer;
        if (!protocol) {
            protocol = PROTOCOL_VERSION_Q2PRO;
        }
    }

    server = Cmd_Argv(1);

    // support quake2://<address>[/] scheme
    if (!Q_strncasecmp(server, "quake2://", 9)) {
        server += 9;
        if ((p = strchr(server, '/')) != NULL) {
            *p = 0;
        }
    }

    if (!NET_StringToAdr(server, &address, PORT_SERVER)) {
        Com_Printf("Bad server address\n");
        return;
    }

    // copy early to avoid potential cmd_argv[1] clobbering
    Q_strlcpy(cls.servername, server, sizeof(cls.servername));

    // if running a local server, kill it and reissue
    SV_Shutdown("Server was killed.\n", ERR_DISCONNECT);

    NET_Config(NET_CLIENT);

    CL_Disconnect(ERR_RECONNECT);

    cls.serverAddress = address;
    cls.serverProtocol = protocol;
    cls.protocolVersion = 0;
    cls.passive = qfalse;
    cls.state = ca_challenging;
    cls.connect_time -= CONNECT_FAST;
    cls.connect_count = 0;

    Con_Popup(qtrue);

    CL_CheckForResend();

    Cvar_Set("timedemo", "0");
}

static void CL_FollowIP_f(void)
{
    netadr_t *a;
    int i, j;

    if (Cmd_Argc() > 1) {
        // optional second argument references less recent address
        j = atoi(Cmd_Argv(1)) + 1;
        clamp(j, 1, RECENT_ADDR);
    } else {
        j = 1;
    }

    i = cls.recent_head - j;
    if (i < 0) {
        Com_Printf("No IP address to follow.\n");
        return;
    }

    a = &cls.recent_addr[i & RECENT_MASK];
    if (a->type) {
        char *s = NET_AdrToString(a);
        Com_Printf("Following %s...\n", s);
        Cbuf_InsertText(cmd_current, va("connect %s\n", s));
    }
}

static void CL_PassiveConnect_f(void)
{
    netadr_t address;

    if (cls.passive) {
        cls.passive = qfalse;
        Com_Printf("No longer listening for passive connections.\n");
        return;
    }

    // if running a local server, kill it and reissue
    SV_Shutdown("Server was killed.\n", ERR_DISCONNECT);

    NET_Config(NET_CLIENT);

    CL_Disconnect(ERR_RECONNECT);

    if (!NET_GetAddress(NS_CLIENT, &address)) {
        return;
    }

    cls.passive = qtrue;
    Com_Printf("Listening for passive connections at %s.\n",
               NET_AdrToString(&address));
}

void CL_SendRcon(const netadr_t *adr, const char *pass, const char *cmd)
{
    NET_Config(NET_CLIENT);

    CL_AddRequest(adr, REQ_RCON);

    Netchan_OutOfBand(NS_CLIENT, adr, "rcon \"%s\" %s", pass, cmd);
}


/*
=====================
CL_Rcon_f

  Send the rest of the command line over as
  an unconnected command.
=====================
*/
static void CL_Rcon_f(void)
{
    netadr_t    address;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <command>\n", Cmd_Argv(0));
        return;
    }

    if (!rcon_password->string[0]) {
        Com_Printf("You must set 'rcon_password' before "
                   "issuing an rcon command.\n");
        return;
    }

    if (!cls.netchan) {
        if (!rcon_address->string[0]) {
            Com_Printf("You must either be connected, "
                       "or set the 'rcon_address' cvar "
                       "to issue rcon commands.\n");
            return;
        }
        if (!NET_StringToAdr(rcon_address->string, &address, PORT_SERVER)) {
            Com_Printf("Bad address: %s\n", rcon_address->string);
            return;
        }
    } else {
        address = cls.netchan->remote_address;
    }

    CL_SendRcon(&address, rcon_password->string, Cmd_RawArgs());
}

static void CL_Rcon_c(genctx_t *ctx, int argnum)
{
    Com_Generic_c(ctx, argnum - 1);
}

/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState(void)
{
    S_StopAllSounds();
    CL_ClearEffects();
#if USE_LIGHTSTYLES
    CL_ClearLightStyles();
#endif
    CL_ClearTEnts();
    LOC_FreeLocations();

    // wipe the entire cl structure
    BSP_Free(cl.bsp);
    memset(&cl, 0, sizeof(cl));
    memset(&cl_entities, 0, sizeof(cl_entities));

    if (cls.state > ca_connected) {
        cls.state = ca_connected;
        CL_CheckForPause();
        CL_UpdateFrameTimes();
    }

    // unprotect game cvar
    fs_game->flags &= ~CVAR_ROM;

#if USE_REF == REF_GL
    // unprotect our custom modulate cvars
    gl_modulate_world->flags &= ~CVAR_CHEAT;
    gl_modulate_entities->flags &= ~CVAR_CHEAT;
    gl_brightness->flags &= ~CVAR_CHEAT;
#endif
}

/*
=====================
CL_Disconnect

Goes from a connected state to full screen console state
Sends a disconnect message to the server
This is also called on Com_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect(error_type_t type)
{
    if (!cls.state) {
        return;
    }

    SCR_EndLoadingPlaque(); // get rid of loading plaque

    SCR_ClearChatHUD_f();   // clear chat HUD on server change

    if (cls.state > ca_disconnected && !cls.demo.playback) {
        EXEC_TRIGGER(cl_disconnectcmd);
    }

#if 0
    if (cls.ref_initialized) {
        R_CinematicSetPalette(NULL);
    }
#endif

    //cls.connect_time = 0;
    //cls.connect_count = 0;
    cls.passive = qfalse;
#if USE_ICMP
    cls.errorReceived = qfalse;
#endif

    if (cls.netchan) {
        // send a disconnect message to the server
        MSG_WriteByte(clc_stringcmd);
        MSG_WriteData("disconnect", 11);

        cls.netchan->Transmit(cls.netchan, msg_write.cursize, msg_write.data, 3);

        SZ_Clear(&msg_write);

        Netchan_Close(cls.netchan);
        cls.netchan = NULL;
    }

    // stop playback and/or recording
    CL_CleanupDemos();

    // stop download
    CL_CleanupDownloads();

    CL_ClearState();

    CL_GTV_Suspend();

    cls.state = ca_disconnected;
    cls.userinfo_modified = 0;

    if (type == ERR_DISCONNECT) {
        UI_OpenMenu(UIMENU_DEFAULT);
    } else {
        UI_OpenMenu(UIMENU_NONE);
    }

    CL_CheckForPause();

    CL_UpdateFrameTimes();
}

/*
================
CL_Disconnect_f
================
*/
static void CL_Disconnect_f(void)
{
    if (cls.state > ca_disconnected) {
        Com_Error(ERR_DISCONNECT, "Disconnected from server");
    }
}

static void CL_ServerStatus_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        CL_RecentIP_g(ctx);
        Com_Address_g(ctx);
    }
}

/*
================
CL_ServerStatus_f
================
*/
static void CL_ServerStatus_f(void)
{
    char        *s;
    netadr_t    adr;
    neterr_t    ret;

    if (Cmd_Argc() < 2) {
        if (!cls.netchan) {
            Com_Printf("Usage: %s [address]\n", Cmd_Argv(0));
            return;
        }
        adr = cls.netchan->remote_address;
    } else {
        s = Cmd_Argv(1);
        if (!NET_StringToAdr(s, &adr, PORT_SERVER)) {
            Com_Printf("Bad address: %s\n", s);
            return;
        }
    }

    CL_AddRequest(&adr, REQ_STATUS_CL);

    NET_Config(NET_CLIENT);

    ret = OOB_PRINT(NS_CLIENT, &adr, "status");
    if (ret == NET_ERROR) {
        Com_Printf("%s to %s\n", NET_ErrorString(), NET_AdrToString(&adr));
    }
}

/*
====================
SortPlayers
====================
*/
static int SortPlayers(const void *v1, const void *v2)
{
    const playerStatus_t *p1 = (const playerStatus_t *)v1;
    const playerStatus_t *p2 = (const playerStatus_t *)v2;

    return p2->score - p1->score;
}

/*
====================
CL_ParseStatusResponse
====================
*/
static void CL_ParseStatusResponse(serverStatus_t *status, const char *string)
{
    playerStatus_t *player;
    const char *s;
    size_t infolen;

    // parse '\n' terminated infostring
    s = Q_strchrnul(string, '\n');

    // due to off-by-one error in the original version of Info_SetValueForKey,
    // some servers produce infostrings up to 512 characters long. work this
    // bug around by cutting off the last character(s).
    infolen = s - string;
    if (infolen >= MAX_INFO_STRING)
        infolen = MAX_INFO_STRING - 1;

    // copy infostring off
    memcpy(status->infostring, string, infolen);
    status->infostring[infolen] = 0;

    if (!Info_Validate(status->infostring))
        strcpy(status->infostring, "\\hostname\\badinfo");

    // parse optional player list
    status->numPlayers = 0;
    while (status->numPlayers < MAX_STATUS_PLAYERS) {
        player = &status->players[status->numPlayers];
        player->score = atoi(COM_Parse(&s));
        player->ping = atoi(COM_Parse(&s));
        Q_strlcpy(player->name, COM_Parse(&s), sizeof(player->name));
        if (!s)
            break;
        status->numPlayers++;
    }

    // sort players by frags
    qsort(status->players, status->numPlayers,
          sizeof(status->players[0]), SortPlayers);
}

static void CL_DumpStatusResponse(const serverStatus_t *status)
{
    int i;

    Com_Printf("Status response from %s\n\n", NET_AdrToString(&net_from));

    Info_Print(status->infostring);

    Com_Printf("\nNum Score Ping Name\n");
    for (i = 0; i < status->numPlayers; i++) {
        Com_Printf("%3i %5i %4i %s\n", i + 1,
                   status->players[i].score,
                   status->players[i].ping,
                   status->players[i].name);
    }
}

/*
====================
CL_ParsePrintMessage
====================
*/
static void CL_ParsePrintMessage(void)
{
    char string[MAX_NET_STRING];
    serverStatus_t status;
    request_t *r;

    MSG_ReadString(string, sizeof(string));

    r = CL_FindRequest();
    if (r) {
        switch (r->type) {
        case REQ_STATUS_CL:
            CL_ParseStatusResponse(&status, string);
            CL_DumpStatusResponse(&status);
            break;
#if USE_UI
        case REQ_STATUS_UI:
            CL_ParseStatusResponse(&status, string);
            UI_StatusEvent(&status);
            break;
#endif
        case REQ_RCON:
            Com_Printf("%s", string);
            return; // rcon may come in multiple packets

        default:
            return;
        }

        if (r->adr.type != NA_BROADCAST)
            r->type = REQ_FREE;
        return;
    }

    // finally, check is this is response from the server we are connecting to
    // and if so, start channenge cycle again
    if ((cls.state == ca_challenging || cls.state == ca_connecting) &&
        NET_IsEqualBaseAdr(&net_from, &cls.serverAddress)) {
        Com_Printf("%s", string);
        cls.state = ca_challenging;
        //cls.connect_count = 0;
        return;
    }

    Com_DPrintf("%s: dropped unrequested packet\n", __func__);
}

/*
=================
CL_ParseInfoMessage

Handle a reply from a ping
=================
*/
static void CL_ParseInfoMessage(void)
{
    char string[MAX_QPATH];
    request_t *r;

    r = CL_FindRequest();
    if (!r)
        return;
    if (r->type != REQ_INFO)
        return;

    MSG_ReadString(string, sizeof(string));
    Com_Printf("%s", string);
    if (r->adr.type != NA_BROADCAST)
        r->type = REQ_FREE;
}

/*
====================
CL_Packet_f

packet <destination> <contents>

Contents allows \n escape character
====================
*/
/*
void CL_Packet_f (void)
{
    char    send[2048];
    int     i, l;
    char    *in, *out;
    netadr_t    adr;

    if (Cmd_Argc() != 3)
    {
        Com_Printf ("packet <destination> <contents>\n");
        return;
    }

    if (!NET_StringToAdr (Cmd_Argv(1), &adr))
    {
        Com_Printf ("Bad address\n");
        return;
    }
    if (!adr.port)
        adr.port = BigShort (PORT_SERVER);

    in = Cmd_Argv(2);
    out = send+4;
    send[0] = send[1] = send[2] = send[3] = (char)0xff;

    l = strlen (in);
    for (i=0; i<l; i++)
    {
        if (in[i] == '\\' && in[i+1] == 'n')
        {
            *out++ = '\n';
            i++;
        }
        else
            *out++ = in[i];
    }
    *out = 0;

    NET_SendPacket (NS_CLIENT, out-send, send, &adr);
}
*/

/*
=================
CL_Changing_f

Just sent as a hint to the client that they should
drop to full console
=================
*/
static void CL_Changing_f(void)
{
    int i, j;
    char *s;

    if (cls.state < ca_connected) {
        return;
    }

    if (cls.demo.recording)
        CL_Stop_f();

    S_StopAllSounds();

    Com_Printf("Changing map...\n");

    if (!cls.demo.playback) {
        EXEC_TRIGGER(cl_changemapcmd);
        Cmd_ExecTrigger("#cl_changelevel");
    }

    SCR_BeginLoadingPlaque();

    cls.state = ca_connected;   // not active anymore, but not disconnected
    cl.mapname[0] = 0;
    cl.configstrings[CS_NAME][0] = 0;

    CL_CheckForPause();

    CL_UpdateFrameTimes();

    // parse additional parameters
    j = Cmd_Argc();
    for (i = 1; i < j; i++) {
        s = Cmd_Argv(i);
        if (!strncmp(s, "map=", 4)) {
            Q_strlcpy(cl.mapname, s + 4, sizeof(cl.mapname));
        }
    }

    SCR_UpdateScreen();
}


/*
=================
CL_Reconnect_f

The server is changing levels
=================
*/
static void CL_Reconnect_f(void)
{
    if (cls.state >= ca_precached) {
        CL_Disconnect(ERR_RECONNECT);
    }

    if (cls.state >= ca_connected) {
        cls.state = ca_connected;

        if (cls.demo.playback) {
            return;
        }
        if (cls.download.file) {
            return; // if we are downloading, we don't change!
        }

        Com_Printf("Reconnecting...\n");

        CL_ClientCommand("new");
        return;
    }

    // issued manually at console
    if (cls.serverAddress.type == NA_UNSPECIFIED) {
        Com_Printf("No server to reconnect to.\n");
        return;
    }
    if (cls.serverAddress.type == NA_LOOPBACK) {
        Com_Printf("Can not reconnect to loopback.\n");
        return;
    }

    Com_Printf("Reconnecting...\n");

    cls.state = ca_challenging;
    cls.connect_time -= CONNECT_FAST;
    cls.connect_count = 0;

    SCR_UpdateScreen();
}

#ifdef USE_UI
/*
=================
CL_SendStatusRequest
=================
*/
void CL_SendStatusRequest(const netadr_t *address)
{
    NET_Config(NET_CLIENT);

    CL_AddRequest(address, REQ_STATUS_UI);

    OOB_PRINT(NS_CLIENT, address, "status");
}
#endif

/*
=================
CL_PingServers_f
=================
*/
static void CL_PingServers_f(void)
{
    netadr_t address;
    cvar_t *var;
    int i;

    NET_Config(NET_CLIENT);

    // send a broadcast packet
    memset(&address, 0, sizeof(address));
    address.type = NA_BROADCAST;
    address.port = BigShort(PORT_SERVER);

    Com_DPrintf("Pinging broadcast...\n");
    CL_AddRequest(&address, REQ_INFO);

    OOB_PRINT(NS_CLIENT, &address, "info 34");

    // send a packet to each address book entry
    for (i = 0; i < 64; i++) {
        var = Cvar_FindVar(va("adr%i", i));
        if (!var)
            break;

        if (!var->string[0])
            continue;

        if (!NET_StringToAdr(var->string, &address, PORT_SERVER)) {
            Com_Printf("Bad address: %s\n", var->string);
            continue;
        }

        Com_DPrintf("Pinging %s...\n", var->string);
        CL_AddRequest(&address, REQ_INFO);

        OOB_PRINT(NS_CLIENT, &address, "info 34");
    }
}

/*
=================
CL_Skins_f

Load or download any custom player skins and models
=================
*/
static void CL_Skins_f(void)
{
    int i;
    char *s;
    clientinfo_t *ci;

    if (cls.state < ca_loading) {
        Com_Printf("Must be in a level to load skins.\n");
        return;
    }

    CL_RegisterVWepModels();

    for (i = 0; i < MAX_CLIENTS; i++) {
        s = cl.configstrings[CS_PLAYERSKINS + i];
        if (!s[0])
            continue;
        ci = &cl.clientinfo[i];
        CL_LoadClientinfo(ci, s);
        if (!ci->model_name[0] || !ci->skin_name[0])
            ci = &cl.baseclientinfo;
        Com_Printf("client %d: %s --> %s/%s\n", i, s,
                   ci->model_name, ci->skin_name);
        SCR_UpdateScreen();
    }
}

static void cl_noskins_changed(cvar_t *self)
{
    int i;
    char *s;
    clientinfo_t *ci;

    if (cls.state < ca_loading) {
        return;
    }

    for (i = 0; i < MAX_CLIENTS; i++) {
        s = cl.configstrings[CS_PLAYERSKINS + i];
        if (!s[0])
            continue;
        ci = &cl.clientinfo[i];
        CL_LoadClientinfo(ci, s);
    }
}

static void cl_vwep_changed(cvar_t *self)
{
    if (cls.state < ca_loading) {
        return;
    }

    CL_RegisterVWepModels();
    cl_noskins_changed(self);
}

static void CL_Name_g(genctx_t *ctx)
{
    int i;
    clientinfo_t *ci;
    char buffer[MAX_CLIENT_NAME];

    if (cls.state < ca_loading) {
        return;
    }

    for (i = 0; i < MAX_CLIENTS; i++) {
        ci = &cl.clientinfo[i];
        if (!ci->name[0]) {
            continue;
        }
        Q_strlcpy(buffer, ci->name, sizeof(buffer));
        if (COM_strclr(buffer) && !Prompt_AddMatch(ctx, buffer)) {
            break;
        }
    }
}


/*
=================
CL_ConnectionlessPacket

Responses to broadcasts, etc
=================
*/
static void CL_ConnectionlessPacket(void)
{
    char    string[MAX_STRING_CHARS];
    char    *s, *c;
    int     i, j, k;
    size_t  len;

    MSG_BeginReading();
    MSG_ReadLong(); // skip the -1

    len = MSG_ReadStringLine(string, sizeof(string));
    if (len >= sizeof(string)) {
        Com_DPrintf("Oversize message received.  Ignored.\n");
        return;
    }

    Cmd_TokenizeString(string, qfalse);

    c = Cmd_Argv(0);

    Com_DPrintf("%s: %s\n", NET_AdrToString(&net_from), string);

    // challenge from the server we are connecting to
    if (!strcmp(c, "challenge")) {
        int mask = 0;

        if (cls.state < ca_challenging) {
            Com_DPrintf("Challenge received while not connecting.  Ignored.\n");
            return;
        }
        if (!NET_IsEqualBaseAdr(&net_from, &cls.serverAddress)) {
            Com_DPrintf("Challenge from different address.  Ignored.\n");
            return;
        }
        if (cls.state > ca_challenging) {
            Com_DPrintf("Dup challenge received.  Ignored.\n");
            return;
        }

        cls.challenge = atoi(Cmd_Argv(1));
        cls.state = ca_connecting;
        cls.connect_time -= CONNECT_INSTANT; // fire immediately
        //cls.connect_count = 0;

        // parse additional parameters
        j = Cmd_Argc();
        for (i = 2; i < j; i++) {
            s = Cmd_Argv(i);
            if (!strncmp(s, "p=", 2)) {
                s += 2;
                while (*s) {
                    k = strtoul(s, &s, 10);
                    if (k == PROTOCOL_VERSION_R1Q2) {
                        mask |= 1;
                    } else if (k == PROTOCOL_VERSION_Q2PRO) {
                        mask |= 2;
                    }
                    s = strchr(s, ',');
                    if (s == NULL) {
                        break;
                    }
                    s++;
                }
            }
        }

        // choose supported protocol
        switch (cls.serverProtocol) {
        case PROTOCOL_VERSION_Q2PRO:
            if (mask & 2) {
                break;
            }
            cls.serverProtocol = PROTOCOL_VERSION_R1Q2;
        case PROTOCOL_VERSION_R1Q2:
            if (mask & 1) {
                break;
            }
        default:
            cls.serverProtocol = PROTOCOL_VERSION_DEFAULT;
            break;
        }
        Com_DPrintf("Selected protocol %d\n", cls.serverProtocol);

        CL_CheckForResend();
        return;
    }

    // server connection
    if (!strcmp(c, "client_connect")) {
        netchan_type_t type;
        int anticheat = 0;
        char mapname[MAX_QPATH];
        qboolean got_server = qfalse;

        if (cls.state < ca_connecting) {
            Com_DPrintf("Connect received while not connecting.  Ignored.\n");
            return;
        }
        if (!NET_IsEqualBaseAdr(&net_from, &cls.serverAddress)) {
            Com_DPrintf("Connect from different address.  Ignored.\n");
            return;
        }
        if (cls.state > ca_connecting) {
            Com_DPrintf("Dup connect received.  Ignored.\n");
            return;
        }

        if (cls.serverProtocol == PROTOCOL_VERSION_Q2PRO) {
            type = NETCHAN_NEW;
        } else {
            type = NETCHAN_OLD;
        }

        mapname[0] = 0;

        // parse additional parameters
        j = Cmd_Argc();
        for (i = 1; i < j; i++) {
            s = Cmd_Argv(i);
            if (!strncmp(s, "ac=", 3)) {
                s += 3;
                if (*s) {
                    anticheat = atoi(s);
                }
            } else if (!strncmp(s, "nc=", 3)) {
                s += 3;
                if (*s) {
                    type = atoi(s);
                    if (type != NETCHAN_OLD && type != NETCHAN_NEW) {
                        Com_Error(ERR_DISCONNECT,
                                  "Server returned invalid netchan type");
                    }
                }
            } else if (!strncmp(s, "map=", 4)) {
                Q_strlcpy(mapname, s + 4, sizeof(mapname));
            } else if (!strncmp(s, "dlserver=", 9)) {
                if (!got_server) {
                    HTTP_SetServer(s + 9);
                    got_server = qtrue;
                }
            }
        }

        if (!got_server) {
            HTTP_SetServer(NULL);
        }

        Com_Printf("Connected to %s (protocol %d).\n",
                   NET_AdrToString(&cls.serverAddress), cls.serverProtocol);
        if (cls.netchan) {
            // this may happen after svc_reconnect
            Netchan_Close(cls.netchan);
        }
        cls.netchan = Netchan_Setup(NS_CLIENT, type, &cls.serverAddress,
                                    cls.quakePort, 1024, cls.serverProtocol);

#if USE_AC_CLIENT
        if (anticheat) {
            MSG_WriteByte(clc_nop);
            MSG_FlushTo(&cls.netchan->message);
            cls.netchan->Transmit(cls.netchan, 0, NULL, 3);
            S_StopAllSounds();
            cls.connect_count = -1;
            Com_Printf("Loading anticheat, this may take a few moments...\n");
            SCR_UpdateScreen();
            if (!Sys_GetAntiCheatAPI()) {
                Com_Printf("Trying to connect without anticheat.\n");
            } else {
                Com_LPrintf(PRINT_NOTICE, "Anticheat loaded successfully.\n");
            }
        }
#else
        if (anticheat >= 2) {
            Com_Printf("Anticheat required by server, "
                       "but no anticheat support linked in.\n");
        }
#endif

        CL_ClientCommand("new");
        cls.state = ca_connected;
        cls.connect_count = 0;
        strcpy(cl.mapname, mapname);   // for levelshot screen
        return;
    }

    if (!strcmp(c, "passive_connect")) {
        if (!cls.passive) {
            Com_DPrintf("Passive connect received while not connecting.  Ignored.\n");
            return;
        }
        s = NET_AdrToString(&net_from);
        Com_Printf("Received passive connect from %s.\n", s);

        cls.serverAddress = net_from;
        cls.serverProtocol = cl_protocol->integer;
        Q_strlcpy(cls.servername, s, sizeof(cls.servername));
        cls.passive = qfalse;

        cls.state = ca_challenging;
        cls.connect_time -= CONNECT_FAST;
        cls.connect_count = 0;

        CL_CheckForResend();
        return;
    }

    // print command from somewhere
    if (!strcmp(c, "print")) {
        CL_ParsePrintMessage();
        return;
    }

    // server responding to a status broadcast
    if (!strcmp(c, "info")) {
        CL_ParseInfoMessage();
        return;
    }

    Com_DPrintf("Unknown connectionless packet command.\n");
}

/*
=================
CL_PacketEvent
=================
*/
static void CL_PacketEvent(void)
{
    //
    // remote command packet
    //
    if (*(int *)msg_read.data == -1) {
        CL_ConnectionlessPacket();
        return;
    }

    if (cls.state < ca_connected) {
        return;
    }

    if (!cls.netchan) {
        return;     // dump it if not connected
    }

    if (msg_read.cursize < 8) {
        Com_DPrintf("%s: runt packet\n", NET_AdrToString(&net_from));
        return;
    }

    //
    // packet from server
    //
    if (!NET_IsEqualAdr(&net_from, &cls.netchan->remote_address)) {
        Com_DPrintf("%s: sequenced packet without connection\n",
                    NET_AdrToString(&net_from));
        return;
    }

    if (!cls.netchan->Process(cls.netchan))
        return;     // wasn't accepted for some reason

#if USE_ICMP
    cls.errorReceived = qfalse; // don't drop
#endif

    CL_ParseServerMessage();

    // if recording demo, write the message out
    if (cls.demo.recording && !cls.demo.paused && CL_FRAMESYNC) {
        CL_WriteDemoMessage(&cls.demo.buffer);
    }

    // if running GTV server, transmit to client
    CL_GTV_Transmit();

    if (!cls.netchan)
        return;     // might have disconnected

#ifdef _DEBUG
    CL_AddNetgraph();
#endif

    SCR_LagSample();
}

#if USE_ICMP
void CL_ErrorEvent(netadr_t *from)
{
    UI_ErrorEvent(from);

    //
    // error packet from server
    //
    if (cls.state < ca_connected) {
        return;
    }
    if (!cls.netchan) {
        return;     // dump it if not connected
    }
    if (!NET_IsEqualBaseAdr(from, &cls.netchan->remote_address)) {
        return;
    }
    if (from->port && from->port != cls.netchan->remote_address.port) {
        return;
    }

    cls.errorReceived = qtrue; // drop connection soon
}
#endif


//=============================================================================

/*
==============
CL_FixUpGender_f
==============
*/
static void CL_FixUpGender(void)
{
    char *p;
    char sk[MAX_QPATH];

    Q_strlcpy(sk, info_skin->string, sizeof(sk));
    if ((p = strchr(sk, '/')) != NULL)
        *p = 0;
    if (Q_stricmp(sk, "male") == 0 || Q_stricmp(sk, "cyborg") == 0)
        Cvar_Set("gender", "male");
    else if (Q_stricmp(sk, "female") == 0 || Q_stricmp(sk, "crackhor") == 0)
        Cvar_Set("gender", "female");
    else
        Cvar_Set("gender", "none");
    info_gender->modified = qfalse;
}

void CL_UpdateUserinfo(cvar_t *var, from_t from)
{
    int i;

    if (var == info_skin && from > FROM_CONSOLE && gender_auto->integer) {
        CL_FixUpGender();
    }

    if (!cls.netchan) {
        return;
    }

    if (cls.serverProtocol != PROTOCOL_VERSION_Q2PRO) {
        // transmit at next oportunity
        cls.userinfo_modified = MAX_PACKET_USERINFOS;
        goto done;
    }

    if (cls.userinfo_modified == MAX_PACKET_USERINFOS) {
        // can't hold any more
        goto done;
    }

    // check for the same variable being modified twice
    for (i = 0; i < cls.userinfo_modified; i++) {
        if (cls.userinfo_updates[i] == var) {
            Com_DDPrintf("%s: %u: %s [DUP]\n",
                         __func__, com_framenum, var->name);
            return;
        }
    }

    cls.userinfo_updates[cls.userinfo_modified++] = var;

done:
    Com_DDPrintf("%s: %u: %s [%d]\n",
                 __func__, com_framenum, var->name, cls.userinfo_modified);
}

/*
==============
CL_Userinfo_f
==============
*/
static void CL_Userinfo_f(void)
{
    char userinfo[MAX_INFO_STRING];

    Cvar_BitInfo(userinfo, CVAR_USERINFO);

    Com_Printf("User info settings:\n");
    Info_Print(userinfo);
}

/*
=================
CL_RestartSound_f

Restart the sound subsystem so it can pick up
new parameters and flush all sounds
=================
*/
static void CL_RestartSound_f(void)
{
    S_Shutdown();
    S_Init();
    CL_RegisterSounds();
}

/*
=================
CL_PlaySound_f

Moved here from sound code so that command is always registered.
=================
*/
static void CL_PlaySound_c(genctx_t *ctx, int state)
{
    FS_File_g("sound", "*.wav", FS_SEARCH_SAVEPATH | FS_SEARCH_BYFILTER | FS_SEARCH_STRIPEXT, ctx);
}

static void CL_PlaySound_f(void)
{
    int     i;
    char name[MAX_QPATH];

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <sound> [...]\n", Cmd_Argv(0));
        return;
    }

    for (i = 1; i < Cmd_Argc(); i++) {
        Cmd_ArgvBuffer(i, name, sizeof(name));
        COM_DefaultExtension(name, ".wav", sizeof(name));
        S_StartLocalSound(name);
    }
}

static int precache_spawncount;

/*
=================
CL_Begin

Called after all downloads are done. Not used for demos.
=================
*/
void CL_Begin(void)
{
#if USE_REF == REF_GL
    if (!Q_stricmp(cl.gamedir, "gloom")) {
        // cheat protect our custom modulate cvars
        gl_modulate_world->flags |= CVAR_CHEAT;
        gl_modulate_entities->flags |= CVAR_CHEAT;
        gl_brightness->flags |= CVAR_CHEAT;
    }
#endif

    Cvar_FixCheats();

    CL_PrepRefresh();
    CL_LoadState(LOAD_SOUNDS);
    CL_RegisterSounds();
    LOC_LoadLocations();
    CL_LoadState(LOAD_NONE);
    cls.state = ca_precached;

#if USE_FPS
    CL_UpdateRateSetting();
#endif

    CL_ClientCommand(va("begin %i\n", precache_spawncount));

    CL_UpdateGunSetting();
    CL_UpdateBlendSetting();
    CL_UpdateGibSetting();
    CL_UpdateFootstepsSetting();
    CL_UpdatePredictSetting();
    CL_UpdateRecordingSetting();
}

/*
=================
CL_Precache_f

The server will send this command right
before allowing the client into the server
=================
*/
static void CL_Precache_f(void)
{
    if (cls.state < ca_connected) {
        return;
    }

    cls.state = ca_loading;
    CL_LoadState(LOAD_MAP);

    S_StopAllSounds();

    CL_RegisterVWepModels();

    // demos use different precache sequence
    if (cls.demo.playback) {
        CL_RegisterBspModels();
        CL_PrepRefresh();
        CL_LoadState(LOAD_SOUNDS);
        CL_RegisterSounds();
        CL_LoadState(LOAD_NONE);
        cls.state = ca_precached;
        return;
    }

    precache_spawncount = atoi(Cmd_Argv(1));

    CL_ResetPrecacheCheck();
    CL_RequestNextDownload();

    if (cls.state != ca_precached) {
        cls.state = ca_connected;
    }
}

typedef struct {
    list_t entry;
    unsigned hits;
    char match[1];
} ignore_t;

static list_t cl_ignores;

static ignore_t *find_ignore(const char *match)
{
    ignore_t *ignore;

    LIST_FOR_EACH(ignore_t, ignore, &cl_ignores, entry) {
        if (!strcmp(ignore->match, match)) {
            return ignore;
        }
    }

    return NULL;
}

static void list_ignores(void)
{
    ignore_t *ignore;

    if (LIST_EMPTY(&cl_ignores)) {
        Com_Printf("No ignore filters.\n");
        return;
    }

    Com_Printf("Current ignore filters:\n");
    LIST_FOR_EACH(ignore_t, ignore, &cl_ignores, entry) {
        Com_Printf("\"%s\" (%u hit%s)\n", ignore->match,
                   ignore->hits, ignore->hits == 1 ? "" : "s");
    }
}

static void add_ignore(const char *match)
{
    ignore_t *ignore;
    size_t matchlen;

    // don't create the same ignore twice
    if (find_ignore(match)) {
        return;
    }

    matchlen = strlen(match);
    if (matchlen < 3) {
        Com_Printf("Match string \"%s\" is too short.\n", match);
        return;
    }

    ignore = Z_Malloc(sizeof(*ignore) + matchlen);
    ignore->hits = 0;
    memcpy(ignore->match, match, matchlen + 1);
    List_Append(&cl_ignores, &ignore->entry);
}

static void remove_ignore(const char *match)
{
    ignore_t *ignore;

    ignore = find_ignore(match);
    if (!ignore) {
        Com_Printf("Can't find ignore filter \"%s\"\n", match);
        return;
    }

    List_Remove(&ignore->entry);
    Z_Free(ignore);
}

static void remove_all_ignores(void)
{
    ignore_t *ignore, *next;
    int count = 0;

    LIST_FOR_EACH_SAFE(ignore_t, ignore, next, &cl_ignores, entry) {
        Z_Free(ignore);
        count++;
    }

    Com_Printf("Removed %d ignore filter%s.\n", count, count == 1 ? "" : "s");
    List_Init(&cl_ignores);
}

static void CL_IgnoreText_f(void)
{
    if (Cmd_Argc() == 1) {
        list_ignores();
        return;
    }

    add_ignore(Cmd_ArgsFrom(1));
}

static void CL_UnIgnoreText_f(void)
{
    if (Cmd_Argc() == 1) {
        list_ignores();
        return;
    }

    if (LIST_EMPTY(&cl_ignores)) {
        Com_Printf("No ignore filters.\n");
        return;
    }

    if (!strcmp(Cmd_Argv(1), "all")) {
        remove_all_ignores();
        return;
    }

    remove_ignore(Cmd_ArgsFrom(1));
}

static void CL_IgnoreNick_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        CL_Name_g(ctx);
    }
}

// properly escapes any special characters in nickname
static size_t parse_ignore_nick(int argnum, char *buffer)
{
    char temp[MAX_CLIENT_NAME];
    char *p, *s;
    int c;
    size_t len;

    Cmd_ArgvBuffer(argnum, temp, sizeof(temp));

    s = temp;
    p = buffer;
    len = 0;
    while (*s) {
        c = *s++;
        c &= 127;
        if (c == '?') {
            *p++ = '\\';
            *p++ = '?';
            len += 2;
        } else if (c == '*') {
            *p++ = '\\';
            *p++ = '*';
            len += 2;
        } else if (c == '\\') {
            *p++ = '\\';
            *p++ = '\\';
            len += 2;
        } else if (Q_isprint(c)) {
            *p++ = c;
            len++;
        }
    }

    *p = 0;

    return len;
}

static void CL_IgnoreNick_f(void)
{
    char nick[MAX_CLIENT_NAME * 2];
    char match[MAX_CLIENT_NAME * 3];

    if (Cmd_Argc() == 1) {
        list_ignores();
        return;
    }

    if (!parse_ignore_nick(1, nick)) {
        return;
    }

    Q_snprintf(match, sizeof(match), "%s: *", nick);
    add_ignore(match);

    Q_snprintf(match, sizeof(match), "(%s): *", nick);
    add_ignore(match);
}

static void CL_UnIgnoreNick_f(void)
{
    char nick[MAX_CLIENT_NAME * 2];
    char match[MAX_CLIENT_NAME * 3];

    if (Cmd_Argc() == 1) {
        list_ignores();
        return;
    }

    if (!parse_ignore_nick(1, nick)) {
        return;
    }

    Q_snprintf(match, sizeof(match), "%s: *", nick);
    remove_ignore(match);

    Q_snprintf(match, sizeof(match), "(%s): *", nick);
    remove_ignore(match);
}

/*
=================
CL_CheckForIgnore
=================
*/
qboolean CL_CheckForIgnore(const char *s)
{
    char buffer[MAX_STRING_CHARS];
    ignore_t *ignore;

    if (LIST_EMPTY(&cl_ignores)) {
        return qfalse;
    }

    Q_strlcpy(buffer, s, sizeof(buffer));
    COM_strclr(buffer);

    LIST_FOR_EACH(ignore_t, ignore, &cl_ignores, entry) {
        if (Com_WildCmp(ignore->match, buffer)) {
            ignore->hits++;
            return qtrue;
        }
    }

    return qfalse;
}

static void CL_DumpClients_f(void)
{
    int i;

    if (cls.state != ca_active) {
        Com_Printf("Must be in a level to dump.\n");
        return;
    }

    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!cl.clientinfo[i].name[0]) {
            continue;
        }

        Com_Printf("%3i: %s\n", i, cl.clientinfo[i].name);
    }
}

static void dump_program(const char *text, const char *name)
{
    char buffer[MAX_OSPATH];

    if (cls.state != ca_active) {
        Com_Printf("Must be in a level to dump.\n");
        return;
    }

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s <filename>\n", Cmd_Argv(0));
        return;
    }

    if (!*text) {
        Com_Printf("No %s to dump.\n", name);
        return;
    }

    if (FS_EasyWriteFile(buffer, sizeof(buffer), FS_MODE_WRITE | FS_FLAG_TEXT,
                         "layouts/", Cmd_Argv(1), ".txt", text, strlen(text))) {
        Com_Printf("Dumped %s program to %s.\n", name, buffer);
    }
}

static void CL_DumpStatusbar_f(void)
{
    dump_program(cl.configstrings[CS_STATUSBAR], "status bar");
}

static void CL_DumpLayout_f(void)
{
    dump_program(cl.layout, "layout");
}

static const cmd_option_t o_writeconfig[] = {
    { "a", "aliases", "write aliases" },
    { "b", "bindings", "write bindings" },
    { "c", "cvars", "write archived cvars" },
    { "h", "help", "display this help message" },
    { "m", "modified", "write modified cvars" },
    { NULL }
};

static void CL_WriteConfig_c(genctx_t *ctx, int argnum)
{
    Cmd_Option_c(o_writeconfig, Cmd_Config_g, ctx, argnum);
}

/*
===============
CL_WriteConfig_f
===============
*/
static void CL_WriteConfig_f(void)
{
    char buffer[MAX_OSPATH];
    qboolean aliases = qfalse, bindings = qfalse, modified = qfalse;
    int c, mask = 0;
    qhandle_t f;

    while ((c = Cmd_ParseOptions(o_writeconfig)) != -1) {
        switch (c) {
        case 'a':
            aliases = qtrue;
            break;
        case 'b':
            bindings = qtrue;
            break;
        case 'c':
            mask |= CVAR_ARCHIVE;
            break;
        case 'h':
            Cmd_PrintUsage(o_writeconfig, "<filename>");
            Com_Printf("Save current configuration into file.\n");
            Cmd_PrintHelp(o_writeconfig);
            return;
        case 'm':
            modified = qtrue;
            mask = ~0;
            break;
        default:
            return;
        }
    }

    if (!cmd_optarg[0]) {
        Com_Printf("Missing filename argument.\n");
        Cmd_PrintHint();
        return;
    }

    if (!aliases && !bindings && !mask) {
        bindings = qtrue;
        mask = CVAR_ARCHIVE;
    }

    f = FS_EasyOpenFile(buffer, sizeof(buffer), FS_MODE_WRITE | FS_FLAG_TEXT,
                        "configs/", cmd_optarg, ".cfg");
    if (!f) {
        return;
    }

    FS_FPrintf(f, "// generated by q2pro\n");

    if (bindings) {
        FS_FPrintf(f, "\n// key bindings\n");
        Key_WriteBindings(f);
    }
    if (aliases) {
        FS_FPrintf(f, "\n// command aliases\n");
        Cmd_WriteAliases(f);
    }
    if (mask) {
        FS_FPrintf(f, "\n//%s cvars\n", modified ? "modified" : "archived");
        Cvar_WriteVariables(f, mask, modified);
    }

    FS_FCloseFile(f);

    Com_Printf("Wrote %s.\n", buffer);
}

static void CL_Say_c(genctx_t *ctx, int argnum)
{
    CL_Name_g(ctx);
}

static size_t CL_Mapname_m(char *buffer, size_t size)
{
    return Q_strlcpy(buffer, cl.mapname, size);
}

static size_t CL_Server_m(char *buffer, size_t size)
{
    return Q_strlcpy(buffer, cls.servername, size);
}

static size_t CL_Ups_m(char *buffer, size_t size)
{
    vec3_t vel;

    if (cl.frame.clientNum == CLIENTNUM_NONE) {
        if (size) {
            *buffer = 0;
        }
        return 0;
    }

    if (!cls.demo.playback && cl.frame.clientNum == cl.clientNum &&
        cl_predict->integer) {
        VectorCopy(cl.predicted_velocity, vel);
    } else {
        VectorScale(cl.frame.ps.pmove.velocity, 0.125f, vel);
    }

    return Q_scnprintf(buffer, size, "%d", (int)VectorLength(vel));
}

static size_t CL_Timer_m(char *buffer, size_t size)
{
    int hour, min, sec;

    sec = cl.time / 1000;
    min = sec / 60; sec %= 60;
    hour = min / 60; min %= 60;

    if (hour) {
        return Q_scnprintf(buffer, size, "%i:%i:%02i", hour, min, sec);
    }
    return Q_scnprintf(buffer, size, "%i:%02i", min, sec);
}

static size_t CL_DemoPos_m(char *buffer, size_t size)
{
    int sec, min, framenum;

    if (cls.demo.playback)
        framenum = cls.demo.frames_read;
    else
#if USE_MVD_CLIENT
        if (MVD_GetDemoPercent(NULL, &framenum) == -1)
#endif
            framenum = 0;

    sec = framenum / 10; framenum %= 10;
    min = sec / 60; sec %= 60;

    return Q_scnprintf(buffer, sizeof(buffer),
                       "%d:%02d.%d", min, sec, framenum);
}

static size_t CL_Fps_m(char *buffer, size_t size)
{
    return Q_scnprintf(buffer, size, "%i", C_FPS);
}

static size_t R_Fps_m(char *buffer, size_t size)
{
    return Q_scnprintf(buffer, size, "%i", R_FPS);
}

static size_t CL_Mps_m(char *buffer, size_t size)
{
    return Q_scnprintf(buffer, size, "%i", C_MPS);
}

static size_t CL_Pps_m(char *buffer, size_t size)
{
    return Q_scnprintf(buffer, size, "%i", C_PPS);
}

static size_t CL_Ping_m(char *buffer, size_t size)
{
    return Q_scnprintf(buffer, size, "%i", cls.measure.ping);
}

static size_t CL_Lag_m(char *buffer, size_t size)
{
    return Q_scnprintf(buffer, size, "%.2f%%", cls.netchan ?
                       ((float)cls.netchan->total_dropped /
                        cls.netchan->total_received) * 100.0f : 0);
}

static size_t CL_Health_m(char *buffer, size_t size)
{
    return Q_scnprintf(buffer, size, "%i", cl.frame.ps.stats[STAT_HEALTH]);
}

static size_t CL_Ammo_m(char *buffer, size_t size)
{
    return Q_scnprintf(buffer, size, "%i", cl.frame.ps.stats[STAT_AMMO]);
}

static size_t CL_Armor_m(char *buffer, size_t size)
{
    return Q_scnprintf(buffer, size, "%i", cl.frame.ps.stats[STAT_ARMOR]);
}

static size_t CL_WeaponModel_m(char *buffer, size_t size)
{
    return Q_scnprintf(buffer, size, "%s",
                       cl.configstrings[cl.frame.ps.gunindex + CS_MODELS]);
}

/*
===============
CL_WriteConfig

Writes key bindings and archived cvars to config.cfg
===============
*/
static void CL_WriteConfig(void)
{
    qhandle_t f;
    qerror_t ret;

    ret = FS_FOpenFile(COM_CONFIG_CFG, &f, FS_MODE_WRITE | FS_FLAG_TEXT);
    if (!f) {
        Com_EPrintf("Couldn't open %s for writing: %s\n",
                    COM_CONFIG_CFG, Q_ErrorString(ret));
        return;
    }

    FS_FPrintf(f, "// generated by " APPLICATION ", do not modify\n");

    Key_WriteBindings(f);
    Cvar_WriteVariables(f, CVAR_ARCHIVE, qfalse);

    FS_FCloseFile(f);
}

/*
====================
CL_RestartFilesystem

Flush caches and restart the VFS.
====================
*/
void CL_RestartFilesystem(qboolean total)
{
    int cls_state;

    if (!cl_running->integer) {
        FS_Restart(total);
        return;
    }

    Com_DPrintf("%s(%d)\n", __func__, total);

    // temporary switch to loading state
    cls_state = cls.state;
    if (cls.state >= ca_precached) {
        cls.state = ca_loading;
    }

    Con_Popup(qfalse);

    UI_Shutdown();

    S_StopAllSounds();
    S_FreeAllSounds();

    // write current config before changing game directory
    CL_WriteConfig();

    if (cls.ref_initialized) {
        R_Shutdown(qfalse);

        FS_Restart(total);

        R_Init(qfalse);

        SCR_RegisterMedia();
        Con_RegisterMedia();
        UI_Init();
    } else {
        FS_Restart(total);
    }

    if (cls_state == ca_disconnected) {
        UI_OpenMenu(UIMENU_DEFAULT);
    } else if (cls_state >= ca_loading && cls_state <= ca_active) {
        CL_LoadState(LOAD_MAP);
        CL_PrepRefresh();
        CL_LoadState(LOAD_SOUNDS);
        CL_RegisterSounds();
        CL_LoadState(LOAD_NONE);
    } else if (cls_state == ca_cinematic) {
        cl.image_precache[0] = R_RegisterPic2(cl.mapname);
    }

    CL_LoadDownloadIgnores();

    // switch back to original state
    cls.state = cls_state;

    Con_Close(qfalse);

    CL_UpdateFrameTimes();

    cvar_modified &= ~CVAR_FILES;
}

void CL_RestartRefresh(qboolean total)
{
    int cls_state;

    if (!cls.ref_initialized) {
        return;
    }

    // temporary switch to loading state
    cls_state = cls.state;
    if (cls.state >= ca_precached) {
        cls.state = ca_loading;
    }

    Con_Popup(qfalse);

    S_StopAllSounds();

    if (total) {
        IN_Shutdown();
        CL_ShutdownRefresh();
        CL_InitRefresh();
        IN_Init();
    } else {
        UI_Shutdown();
        R_Shutdown(qfalse);
        R_Init(qfalse);
        SCR_RegisterMedia();
        Con_RegisterMedia();
        UI_Init();
    }

    if (cls_state == ca_disconnected) {
        UI_OpenMenu(UIMENU_DEFAULT);
    } else if (cls_state >= ca_loading && cls_state <= ca_active) {
        CL_LoadState(LOAD_MAP);
        CL_PrepRefresh();
        CL_LoadState(LOAD_NONE);
    } else if (cls_state == ca_cinematic) {
        cl.image_precache[0] = R_RegisterPic2(cl.mapname);
    }

    // switch back to original state
    cls.state = cls_state;

    Con_Close(qfalse);

    CL_UpdateFrameTimes();

    cvar_modified &= ~CVAR_FILES;
}

/*
====================
CL_ReloadRefresh

Flush caches and reload all models and textures.
====================
*/
static void CL_ReloadRefresh_f(void)
{
    CL_RestartRefresh(qfalse);
}

/*
====================
CL_RestartRefresh

Perform complete restart of the renderer subsystem.
====================
*/
static void CL_RestartRefresh_f(void)
{
    CL_RestartRefresh(qtrue);
}

// execute string in server command buffer
static void exec_server_string(cmdbuf_t *buf, const char *text)
{
    char *s;

    Cmd_TokenizeString(text, qtrue);

    // execute the command line
    if (!Cmd_Argc()) {
        return;        // no tokens
    }

    Com_DPrintf("stufftext: %s\n", text);

    s = Cmd_Argv(0);

    // handle private client commands
    if (!strcmp(s, "changing")) {
        CL_Changing_f();
        return;
    }
    if (!strcmp(s, "precache")) {
        CL_Precache_f();
        return;
    }

    // forbid nearly every command from demos
    if (cls.demo.playback) {
        if (strcmp(s, "play")) {
            return;
        }
    }

    // execute regular commands
    Cmd_ExecuteCommand(buf);
}

static void cl_gun_changed(cvar_t *self)
{
    CL_UpdateGunSetting();
}

static void info_hand_changed(cvar_t *self)
{
    CL_UpdateGunSetting();
}

static void cl_gibs_changed(cvar_t *self)
{
    CL_UpdateGibSetting();
}

static void cl_footsteps_changed(cvar_t *self)
{
    CL_UpdateFootstepsSetting();
}

static void cl_predict_changed(cvar_t *self)
{
    CL_UpdatePredictSetting();
}

#if USE_FPS
static void cl_updaterate_changed(cvar_t *self)
{
    CL_UpdateRateSetting();
}
#endif

static void cl_sync_changed(cvar_t *self)
{
    CL_UpdateFrameTimes();
}

// allow downloads to be permanently disabled as a
// protection measure from malicious (or just stupid) servers
// that force downloads by stuffing commands
static void cl_allow_download_changed(cvar_t *self)
{
    if (self->integer == -1) {
        self->flags |= CVAR_ROM;
    }
}

// ugly hack for compatibility
static void cl_chat_sound_changed(cvar_t *self)
{
    if (!*self->string)
        self->integer = 0;
    else if (!Q_stricmp(self->string, "misc/talk.wav"))
        self->integer = 1;
    else if (!Q_stricmp(self->string, "misc/talk1.wav"))
        self->integer = 2;
    else if (!self->integer && !COM_IsUint(self->string))
        self->integer = 1;
}

static const cmdreg_t c_client[] = {
    { "cmd", CL_ForwardToServer_f },
    { "pause", CL_Pause_f },
    { "pingservers", CL_PingServers_f },
    { "skins", CL_Skins_f },
    { "userinfo", CL_Userinfo_f },
    { "snd_restart", CL_RestartSound_f },
    { "play", CL_PlaySound_f, CL_PlaySound_c },
    //{ "changing", CL_Changing_f },
    { "disconnect", CL_Disconnect_f },
    { "connect", CL_Connect_f, CL_Connect_c },
    { "followip", CL_FollowIP_f },
    { "passive", CL_PassiveConnect_f },
    { "reconnect", CL_Reconnect_f },
    { "rcon", CL_Rcon_f, CL_Rcon_c },
    //{ "precache", CL_Precache_f },
    { "serverstatus", CL_ServerStatus_f, CL_ServerStatus_c },
    { "ignoretext", CL_IgnoreText_f },
    { "unignoretext", CL_UnIgnoreText_f },
    { "ignorenick", CL_IgnoreNick_f, CL_IgnoreNick_c },
    { "unignorenick", CL_UnIgnoreNick_f, CL_IgnoreNick_c },
    { "dumpclients", CL_DumpClients_f },
    { "dumpstatusbar", CL_DumpStatusbar_f },
    { "dumplayout", CL_DumpLayout_f },
    { "writeconfig", CL_WriteConfig_f, CL_WriteConfig_c },
//    { "msgtab", CL_Msgtab_f, CL_Msgtab_g },
    { "vid_restart", CL_RestartRefresh_f },
    { "r_reload", CL_ReloadRefresh_f },

    //
    // forward to server commands
    //
    // the only thing this does is allow command completion
    // to work -- all unknown commands are automatically
    // forwarded to the server
    { "say", NULL, CL_Say_c },
    { "say_team", NULL, CL_Say_c },

    { "wave" }, { "inven" }, { "kill" }, { "use" },
    { "drop" }, { "info" }, { "prog" },
    { "give" }, { "god" }, { "notarget" }, { "noclip" },
    { "invuse" }, { "invprev" }, { "invnext" }, { "invdrop" },
    { "weapnext" }, { "weapprev" },

    { NULL }
};

/*
=================
CL_InitLocal
=================
*/
static void CL_InitLocal(void)
{
    cvar_t *var;
    int i;

    cls.state = ca_disconnected;
    cls.connect_time -= CONNECT_INSTANT;

    CL_RegisterInput();
    CL_InitDemos();
    LOC_Init();
    CL_InitAscii();
    CL_InitEffects();
    CL_InitTEnts();
    CL_InitDownloads();
    CL_GTV_Init();

    List_Init(&cl_ignores);

    Cmd_Register(c_client);

    for (i = 0; i < MAX_LOCAL_SERVERS; i++) {
        var = Cvar_Get(va("adr%i", i), "", CVAR_ARCHIVE);
        var->generator = Com_Address_g;
    }

    //
    // register our variables
    //
    cl_gun = Cvar_Get("cl_gun", "1", 0);
    cl_gun->changed = cl_gun_changed;
    cl_gunalpha = Cvar_Get("cl_gunalpha", "1", 0);
    cl_footsteps = Cvar_Get("cl_footsteps", "1", 0);
    cl_footsteps->changed = cl_footsteps_changed;
    cl_noskins = Cvar_Get("cl_noskins", "0", 0);
    cl_noskins->changed = cl_noskins_changed;
    cl_predict = Cvar_Get("cl_predict", "1", 0);
    cl_predict->changed = cl_predict_changed;
    cl_kickangles = Cvar_Get("cl_kickangles", "1", CVAR_CHEAT);
    cl_maxfps = Cvar_Get("cl_maxfps", "60", 0);
    cl_maxfps->changed = cl_sync_changed;
    cl_async = Cvar_Get("cl_async", "1", 0);
    cl_async->changed = cl_sync_changed;
    r_maxfps = Cvar_Get("r_maxfps", "0", 0);
    r_maxfps->changed = cl_sync_changed;
    cl_autopause = Cvar_Get("cl_autopause", "1", 0);
    cl_rollhack = Cvar_Get("cl_rollhack", "1", 0);
    cl_noglow = Cvar_Get("cl_noglow", "0", 0);
    cl_nolerp = Cvar_Get("cl_nolerp", "0", 0);

    // hack for timedemo
    com_timedemo->changed = cl_sync_changed;

    CL_UpdateFrameTimes();

#ifdef _DEBUG
    cl_shownet = Cvar_Get("cl_shownet", "0", 0);
    cl_showmiss = Cvar_Get("cl_showmiss", "0", 0);
    cl_showclamp = Cvar_Get("showclamp", "0", 0);
#endif

    cl_timeout = Cvar_Get("cl_timeout", "120", 0);

    rcon_address = Cvar_Get("rcon_address", "", CVAR_PRIVATE);
    rcon_address->generator = Com_Address_g;

    cl_thirdperson = Cvar_Get("cl_thirdperson", "0", CVAR_CHEAT);
    cl_thirdperson_angle = Cvar_Get("cl_thirdperson_angle", "0", 0);
    cl_thirdperson_range = Cvar_Get("cl_thirdperson_range", "60", 0);

    cl_disable_particles = Cvar_Get("cl_disable_particles", "0", 0);
    cl_disable_explosions = Cvar_Get("cl_disable_explosions", "0", 0);
    cl_gibs = Cvar_Get("cl_gibs", "1", 0);
    cl_gibs->changed = cl_gibs_changed;

#if USE_FPS
    cl_updaterate = Cvar_Get("cl_updaterate", "0", 0);
    cl_updaterate->changed = cl_updaterate_changed;
#endif

    cl_chat_notify = Cvar_Get("cl_chat_notify", "1", 0);
    cl_chat_sound = Cvar_Get("cl_chat_sound", "1", 0);
    cl_chat_sound->changed = cl_chat_sound_changed;
    cl_chat_sound_changed(cl_chat_sound);
    cl_chat_filter = Cvar_Get("cl_chat_filter", "0", 0);

    cl_disconnectcmd = Cvar_Get("cl_disconnectcmd", "", 0);
    cl_changemapcmd = Cvar_Get("cl_changemapcmd", "", 0);
    cl_beginmapcmd = Cvar_Get("cl_beginmapcmd", "", 0);

    cl_protocol = Cvar_Get("cl_protocol", "0", 0);

    gender_auto = Cvar_Get("gender_auto", "1", CVAR_ARCHIVE);

    cl_vwep = Cvar_Get("cl_vwep", "1", CVAR_ARCHIVE);
    cl_vwep->changed = cl_vwep_changed;

    allow_download->changed = cl_allow_download_changed;
    cl_allow_download_changed(allow_download);

    //
    // userinfo
    //
    info_password = Cvar_Get("password", "", CVAR_USERINFO);
    info_spectator = Cvar_Get("spectator", "0", CVAR_USERINFO);
    info_name = Cvar_Get("name", "unnamed", CVAR_USERINFO | CVAR_ARCHIVE);
    info_skin = Cvar_Get("skin", "male/grunt", CVAR_USERINFO | CVAR_ARCHIVE);
    info_rate = Cvar_Get("rate", "5000", CVAR_USERINFO | CVAR_ARCHIVE);
    info_msg = Cvar_Get("msg", "1", CVAR_USERINFO | CVAR_ARCHIVE);
    info_hand = Cvar_Get("hand", "0", CVAR_USERINFO | CVAR_ARCHIVE);
    info_hand->changed = info_hand_changed;
    info_fov = Cvar_Get("fov", "90", CVAR_USERINFO | CVAR_ARCHIVE);
    info_gender = Cvar_Get("gender", "male", CVAR_USERINFO | CVAR_ARCHIVE);
    info_gender->modified = qfalse; // clear this so we know when user sets it manually
    info_uf = Cvar_Get("uf", "", CVAR_USERINFO);


    //
    // macros
    //
    Cmd_AddMacro("cl_mapname", CL_Mapname_m);
    Cmd_AddMacro("cl_server", CL_Server_m);
    Cmd_AddMacro("cl_timer", CL_Timer_m);
    Cmd_AddMacro("cl_demopos", CL_DemoPos_m);
    Cmd_AddMacro("cl_ups", CL_Ups_m);
    Cmd_AddMacro("cl_fps", CL_Fps_m);
    Cmd_AddMacro("r_fps", R_Fps_m);
    Cmd_AddMacro("cl_mps", CL_Mps_m);   // moves per second
    Cmd_AddMacro("cl_pps", CL_Pps_m);   // packets per second
    Cmd_AddMacro("cl_ping", CL_Ping_m);
    Cmd_AddMacro("cl_lag", CL_Lag_m);
    Cmd_AddMacro("cl_health", CL_Health_m);
    Cmd_AddMacro("cl_ammo", CL_Ammo_m);
    Cmd_AddMacro("cl_armor", CL_Armor_m);
    Cmd_AddMacro("cl_weaponmodel", CL_WeaponModel_m);
}

/*
==================
CL_CheatsOK
==================
*/
qboolean CL_CheatsOK(void)
{
    // can cheat when disconnected or playing a demo
    if (cls.state < ca_connected || cls.demo.playback)
        return qtrue;

    // can't cheat on remote servers
    if (!sv_running->integer)
        return qfalse;

    // developer option
    if (Cvar_VariableInteger("cheats"))
        return qtrue;

    // single player can cheat
    if (cls.state > ca_connected && cl.maxclients == 1)
        return qtrue;

#if USE_MVD_CLIENT
    // can cheat when playing MVD
    if (MVD_GetDemoPercent(NULL, NULL) != -1)
        return qtrue;
#endif

    return qfalse;
}

//============================================================================

/*
==================
CL_Activate
==================
*/
void CL_Activate(active_t active)
{
    if (cls.active != active) {
        Com_DDDPrintf("%s: %u\n", __func__, active);
        cls.active = active;
        cls.disable_screen = 0;
        Key_ClearStates();
        IN_Activate();
        S_Activate();
        CL_UpdateFrameTimes();
    }
}

static void CL_SetClientTime(void)
{
    int prevtime;

    if (com_timedemo->integer) {
        cl.time = cl.servertime;
        cl.lerpfrac = 1.0f;
#if USE_FPS
        cl.keytime = cl.keyservertime;
        cl.keylerpfrac = 1.0f;
#endif
        return;
    }

    prevtime = cl.servertime - CL_FRAMETIME;
    if (cl.time > cl.servertime) {
        SHOWCLAMP(1, "high clamp %i\n", cl.time - cl.servertime);
        cl.time = cl.servertime;
        cl.lerpfrac = 1.0f;
    } else if (cl.time < prevtime) {
        SHOWCLAMP(1, "low clamp %i\n", prevtime - cl.time);
        cl.time = prevtime;
        cl.lerpfrac = 0;
    } else {
        cl.lerpfrac = (cl.time - prevtime) * CL_1_FRAMETIME;
    }

    SHOWCLAMP(2, "time %d %d, lerpfrac %.3f\n",
              cl.time, cl.servertime, cl.lerpfrac);

#if USE_FPS
    prevtime = cl.keyservertime - BASE_FRAMETIME;
    if (cl.keytime > cl.keyservertime) {
        SHOWCLAMP(1, "high keyclamp %i\n", cl.keytime - cl.keyservertime);
        cl.keytime = cl.keyservertime;
        cl.keylerpfrac = 1.0f;
    } else if (cl.keytime < prevtime) {
        SHOWCLAMP(1, "low keyclamp %i\n", prevtime - cl.keytime);
        cl.keytime = prevtime;
        cl.keylerpfrac = 0;
    } else {
        cl.keylerpfrac = (cl.keytime - prevtime) * BASE_1_FRAMETIME;
    }

    SHOWCLAMP(2, "keytime %d %d keylerpfrac %.3f\n",
              cl.keytime, cl.keyservertime, cl.keylerpfrac);
#endif
}

static void CL_MeasureStats(void)
{
    int i;

    if (com_localTime - cls.measure.time < 1000) {
        return;
    }

    // measure average ping
    if (cls.netchan) {
        int ack = cls.netchan->incoming_acknowledged;
        int ping = 0;
        int j, k = 0;

        i = ack - 16 + 1;
        if (i < cl.initialSeq) {
            i = cl.initialSeq;
        }
        for (j = i; j <= ack; j++) {
            client_history_t *h = &cl.history[j & CMD_MASK];
            if (h->rcvd > h->sent) {
                ping += h->rcvd - h->sent;
                k++;
            }
        }

        cls.measure.ping = k ? ping / k : 0;
    }

    // measure main/refresh frame counts
    for (i = 0; i < 4; i++) {
        cls.measure.fps[i] = cls.measure.frames[i];
        cls.measure.frames[i] = 0;
    }

    cls.measure.time = com_localTime;
}

#if USE_AUTOREPLY
static void CL_CheckForReply(void)
{
    if (!cl.reply_delta) {
        return;
    }

    if (cls.realtime - cl.reply_time < cl.reply_delta) {
        return;
    }

    CL_ClientCommand(va("say \"%s\"", com_version->string));

    cl.reply_delta = 0;
}
#endif

static void CL_CheckTimeout(void)
{
    unsigned delta;

    if (NET_IsLocalAddress(&cls.netchan->remote_address)) {
        return;
    }

#if USE_ICMP
    if (cls.errorReceived) {
        delta = 5000;
        if (com_localTime - cls.netchan->last_received > delta)  {
            Com_Error(ERR_DISCONNECT, "Server connection was reset.");
        }
    }
#endif

    delta = cl_timeout->value * 1000;
    if (delta && com_localTime - cls.netchan->last_received > delta)  {
        // timeoutcount saves debugger
        if (++cl.timeoutcount > 5) {
            Com_Error(ERR_DISCONNECT, "Server connection timed out.");
        }
    } else {
        cl.timeoutcount = 0;
    }
}

/*
=================
CL_CheckForPause

=================
*/
void CL_CheckForPause(void)
{
    if (cls.state != ca_active) {
        // only pause when active
        Cvar_Set("cl_paused", "0");
        Cvar_Set("sv_paused", "0");
        return;
    }

    if (cls.key_dest & (KEY_CONSOLE | KEY_MENU)) {
        // only pause in single player
        if (cl_paused->integer == 0 && cl_autopause->integer) {
            Cvar_Set("cl_paused", "1");
        }
    } else if (cl_paused->integer == 1) {
        // only resume after automatic pause
        Cvar_Set("cl_paused", "0");
    }

    // hack for demo playback pause/unpause
    if (cls.demo.playback) {
        // don't pause when running timedemo!
        if (cl_paused->integer && !com_timedemo->integer) {
            if (!sv_paused->integer) {
                Cvar_Set("sv_paused", "1");
                IN_Activate();
            }
        } else {
            if (sv_paused->integer) {
                Cvar_Set("sv_paused", "0");
                IN_Activate();
            }
        }
    }
}

typedef enum {
    SYNC_FULL,
    SYNC_MAXFPS,
    SYNC_SLEEP_10,
    SYNC_SLEEP_60,
    SYNC_SLEEP_VIDEO,
    ASYNC_VIDEO,
    ASYNC_MAXFPS,
    ASYNC_FULL
} sync_mode_t;

#ifdef _DEBUG
static const char *const sync_names[] = {
    "SYNC_FULL",
    "SYNC_MAXFPS",
    "SYNC_SLEEP_10",
    "SYNC_SLEEP_60",
    "SYNC_SLEEP_VIDEO",
    "ASYNC_VIDEO",
    "ASYNC_MAXFPS",
    "ASYNC_FULL"
};
#endif

static int ref_msec, phys_msec, main_msec;
static int ref_extra, phys_extra, main_extra;
static sync_mode_t sync_mode;

static inline int fps_to_msec(int fps)
{
#if 0
    return (1000 + fps / 2) / fps;
#else
    return 1000 / fps;
#endif
}

/*
==================
CL_UpdateFrameTimes

Called whenever async/fps cvars change, but not every frame
==================
*/
void CL_UpdateFrameTimes(void)
{
    if (!cls.state) {
        return; // not yet fully initialized
    }

    // check if video driver supports syncing to vertical retrace
    if (cl_async->integer > 1 && !(r_config.flags & QVF_VIDEOSYNC)) {
        Cvar_Reset(cl_async);
    }

    if (com_timedemo->integer) {
        // timedemo just runs at full speed
        ref_msec = phys_msec = main_msec = 0;
        sync_mode = SYNC_FULL;
    } else if (cls.active == ACT_MINIMIZED) {
        // run at 10 fps if minimized
        ref_msec = phys_msec = 0;
        main_msec = fps_to_msec(10);
        sync_mode = SYNC_SLEEP_10;
    } else if (cls.active == ACT_RESTORED || cls.state != ca_active) {
        // run at 60 fps if not active
        ref_msec = phys_msec = 0;
        if (cl_async->integer > 1) {
            main_msec = 0;
            sync_mode = SYNC_SLEEP_VIDEO;
        } else {
            main_msec = fps_to_msec(60);
            sync_mode = SYNC_SLEEP_60;
        }
    } else if (cl_async->integer > 0) {
        // run physics and refresh separately
        phys_msec = fps_to_msec(Cvar_ClampInteger(cl_maxfps, 10, 120));
        if (cl_async->integer > 1) {
            ref_msec = 0;
            sync_mode = ASYNC_VIDEO;
        } else if (r_maxfps->integer) {
            ref_msec = fps_to_msec(Cvar_ClampInteger(r_maxfps, 10, 1000));
            sync_mode = ASYNC_MAXFPS;
        } else {
            ref_msec = 1;
            sync_mode = ASYNC_FULL;
        }
        main_msec = 0;
    } else {
        // everything ticks in sync with refresh
        phys_msec = ref_msec = 0;
        if (cl_maxfps->integer) {
            main_msec = fps_to_msec(Cvar_ClampInteger(cl_maxfps, 10, 1000));
            sync_mode = SYNC_MAXFPS;
        } else {
            main_msec = 1;
            sync_mode = SYNC_FULL;
        }
    }

    Com_DDDPrintf("%s: mode=%s main_msec=%d ref_msec=%d, phys_msec=%d\n",
                  __func__, sync_names[sync_mode], main_msec, ref_msec, phys_msec);

    ref_extra = phys_extra = main_extra = 0;
}

/*
==================
CL_Frame

==================
*/
unsigned CL_Frame(unsigned msec)
{
    qboolean phys_frame, ref_frame;

    time_after_ref = time_before_ref = 0;

    if (!cl_running->integer) {
        return UINT_MAX;
    }

    main_extra += msec;
    cls.realtime += msec;

    CL_ProcessEvents();

    ref_frame = phys_frame = qtrue;
    switch (sync_mode) {
    case SYNC_FULL:
        // timedemo just runs at full speed
        break;
    case SYNC_SLEEP_10:
        // don't run refresh at all
        ref_frame = qfalse;
        // fall through
    case SYNC_SLEEP_60:
        // run at limited fps if not active
        if (main_extra < main_msec) {
            return main_msec - main_extra;
        }
        break;
    case SYNC_SLEEP_VIDEO:
        // wait for vertical retrace if not active
        VID_VideoWait();
        break;
    case ASYNC_VIDEO:
    case ASYNC_MAXFPS:
    case ASYNC_FULL:
        // run physics and refresh separately
        phys_extra += main_extra;
        if (phys_extra < phys_msec) {
            phys_frame = qfalse;
        } else if (phys_extra > phys_msec * 4) {
            phys_extra = phys_msec;
        }

        if (sync_mode == ASYNC_VIDEO) {
            // sync refresh to vertical retrace
            ref_frame = VID_VideoSync();
        } else {
            ref_extra += main_extra;
            if (ref_extra < ref_msec) {
                ref_frame = qfalse;
            } else if (ref_extra > ref_msec * 4) {
                ref_extra = ref_msec;
            }
        }
        break;
    case SYNC_MAXFPS:
        // everything ticks in sync with refresh
        if (main_extra < main_msec) {
            if (!cl.sendPacketNow) {
                return 0;
            }
            ref_frame = qfalse;
        }
        break;
    }

    Com_DDDDPrintf("main_extra=%d ref_frame=%d ref_extra=%d "
                   "phys_frame=%d phys_extra=%d\n",
                   main_extra, ref_frame, ref_extra,
                   phys_frame, phys_extra);

    // decide the simulation time
    cls.frametime = main_extra * 0.001f;

    if (cls.frametime > 1.0 / 5)
        cls.frametime = 1.0 / 5;

    if (!sv_paused->integer) {
        cl.time += main_extra;
#if USE_FPS
        cl.keytime += main_extra;
#endif
    }

    // read next demo frame
    if (cls.demo.playback)
        CL_DemoFrame(main_extra);

    // calculate local time
    if (cls.state == ca_active && !sv_paused->integer)
        CL_SetClientTime();

#if USE_AUTOREPLY
    // check for version reply
    CL_CheckForReply();
#endif

    // resend a connection request if necessary
    CL_CheckForResend();

    // read user intentions
    CL_UpdateCmd(main_extra);

    // finalize pending cmd
    phys_frame |= cl.sendPacketNow;
    if (phys_frame) {
        CL_FinalizeCmd();
        phys_extra -= phys_msec;
        M_FRAMES++;

        // don't let the time go too far off
        // this can happen due to cl.sendPacketNow
        if (phys_extra < -phys_msec * 4) {
            phys_extra = 0;
        }
    }

    // send pending cmds
    CL_SendCmd();

    // predict all unacknowledged movements
    CL_PredictMovement();

    Con_RunConsole();

    UI_Frame(main_extra);

    if (ref_frame) {
        // update the screen
        if (host_speeds->integer)
            time_before_ref = Sys_Milliseconds();

        SCR_UpdateScreen();

        if (host_speeds->integer)
            time_after_ref = Sys_Milliseconds();

        ref_extra -= ref_msec;
        R_FRAMES++;

run_fx:
        // update audio after the 3D view was drawn
        S_Update();

        // advance local effects for next frame
#if USE_DLIGHTS
        CL_RunDLights();
#endif

#if USE_LIGHTSTYLES
        CL_RunLightStyles();
#endif
    } else if (sync_mode == SYNC_SLEEP_10) {
        // force audio and effects update if not rendering
        CL_CalcViewValues();
        goto run_fx;
    }

    // check connection timeout
    if (cls.netchan)
        CL_CheckTimeout();

    C_FRAMES++;

    CL_MeasureStats();

    cls.framecount++;

    main_extra = 0;
    return 0;
}

/*
============
CL_ProcessEvents
============
*/
qboolean CL_ProcessEvents(void)
{
    if (!cl_running->integer) {
        return qfalse;
    }

    CL_RunRefresh();

    IN_Frame();

    NET_GetPackets(NS_CLIENT, CL_PacketEvent);

    // process console and stuffed commands
    Cbuf_Execute(&cmd_buffer);
    Cbuf_Execute(&cl_cmdbuf);

    HTTP_RunDownloads();

    CL_GTV_Run();

    return cl.sendPacketNow;
}

//============================================================================

/*
====================
CL_Init
====================
*/
void CL_Init(void)
{
    if (dedicated->integer) {
        return; // nothing running on the client
    }

    if (cl_running->integer) {
        return;
    }

    // all archived variables will now be loaded

    // start with full screen console
    cls.key_dest = KEY_CONSOLE;

#ifdef _WIN32
    CL_InitRefresh();
    S_Init();   // sound must be initialized after window is created
#else
    S_Init();
    CL_InitRefresh();
#endif

    CL_InitLocal();
    IN_Init();

#if USE_ZLIB
    if (inflateInit2(&cls.z, -MAX_WBITS) != Z_OK) {
        Com_Error(ERR_FATAL, "%s: inflateInit2() failed", __func__);
    }
#endif

    CL_LoadDownloadIgnores();

    HTTP_Init();

    UI_OpenMenu(UIMENU_DEFAULT);

    Con_PostInit();
    Con_RunConsole();

    cl_cmdbuf.from = FROM_STUFFTEXT;
    cl_cmdbuf.text = cl_cmdbuf_text;
    cl_cmdbuf.maxsize = sizeof(cl_cmdbuf_text);
    cl_cmdbuf.exec = exec_server_string;

    Cvar_Set("cl_running", "1");
}

/*
===============
CL_Shutdown

FIXME: this is a callback from Com_Quit and Com_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void CL_Shutdown(void)
{
    static qboolean isdown = qfalse;

    if (isdown) {
        Com_Printf("CL_Shutdown: recursive shutdown\n");
        return;
    }
    isdown = qtrue;

    if (!cl_running || !cl_running->integer) {
        return;
    }

    CL_GTV_Shutdown();

    CL_Disconnect(ERR_FATAL);

#if USE_ZLIB
    inflateEnd(&cls.z);
#endif

    HTTP_Shutdown();
    S_Shutdown();
    IN_Shutdown();
    Con_Shutdown();
    CL_ShutdownRefresh();
    CL_WriteConfig();

    memset(&cls, 0, sizeof(cls));

    Cvar_Set("cl_running", "0");

    isdown = qfalse;
}

