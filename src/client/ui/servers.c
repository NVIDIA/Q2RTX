/*
Copyright (C) 2012 Andrey Nazarov

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

#include "ui.h"
#include "common/files.h"
#include "common/net/net.h"
#include "client/video.h"
#include "system/system.h"

/*
=============================================================================

SERVERS MENU

=============================================================================
*/

#define MAX_STATUS_RULES    64
#define MAX_STATUS_SERVERS  1024

#define SLOT_EXTRASIZE  q_offsetof(serverslot_t, name)

#define COL_NAME    0
#define COL_MOD     1
#define COL_MAP     2
#define COL_PLAYERS 3
#define COL_RTT     4
#define COL_MAX     5

// how many times to (re)ping
#define PING_STAGES     3

typedef struct {
    enum {
        SLOT_IDLE,
        SLOT_PENDING,
        SLOT_ERROR,
        SLOT_VALID
    } status;
    netadr_t    address;
    char        *hostname; // original domain name, only used for favorites
    int         numRules;
    char        *rules[MAX_STATUS_RULES];
    int         numPlayers;
    char        *players[MAX_STATUS_PLAYERS];
    unsigned    timestamp;
    uint32_t    color;
    char        name[1];
} serverslot_t;

typedef struct {
    menuFrameWork_t menu;
    menuList_t      list;
    menuList_t      info;
    menuList_t      players;
    void            *names[MAX_STATUS_SERVERS];
    char            *args;
    unsigned        timestamp;
    int             pingstage;
    int             pingindex;
    int             pingtime;
    int             pingextra;
    const char      *status_c;
    char            status_r[32];
} m_servers_t;

static m_servers_t  m_servers;

static cvar_t   *ui_sortservers;
static cvar_t   *ui_colorservers;
static cvar_t   *ui_pingrate;

static void UpdateSelection(void)
{
    serverslot_t *s = NULL;

    if (m_servers.list.numItems) {
        if (m_servers.list.curvalue >= 0) {
            s = m_servers.list.items[m_servers.list.curvalue];
            if (s->status == SLOT_VALID) {
                m_servers.status_c = "Press Enter to connect; Space to refresh";
            } else {
                m_servers.status_c = "Press Space to refresh; Alt+Space to refresh all";
            }
        } else if (m_servers.pingstage) {
            m_servers.status_c = "Pinging servers; Press Backspace to abort";
        } else {
            m_servers.status_c = "Select a server; Press Alt+Space to refresh";
        }
    } else {
        m_servers.status_c = "No servers found; Press Space to refresh";
    }

    if (s && s->status == SLOT_VALID && s->numRules && uis.width >= 640) {
        m_servers.info.generic.flags &= ~QMF_HIDDEN;
        if (m_servers.info.items != (void **)s->rules || m_servers.info.numItems != s->numRules) {
            m_servers.info.items = (void **)s->rules;
            m_servers.info.numItems = s->numRules;
            m_servers.info.curvalue = -1;
            m_servers.info.prestep = 0;
        }
    } else {
        m_servers.info.generic.flags |= QMF_HIDDEN;
        m_servers.info.items = NULL;
        m_servers.info.numItems = 0;
    }

    if (s && s->status == SLOT_VALID && s->numPlayers) {
        m_servers.players.generic.flags &= ~QMF_HIDDEN;
        if (m_servers.players.items != (void **)s->players || m_servers.players.numItems != s->numPlayers) {
            m_servers.players.items = (void **)s->players;
            m_servers.players.numItems = s->numPlayers;
            m_servers.players.curvalue = -1;
            m_servers.players.prestep = 0;
        }
    } else {
        m_servers.players.generic.flags |= QMF_HIDDEN;
        m_servers.players.items = NULL;
        m_servers.players.numItems = 0;
    }
}

static void UpdateStatus(void)
{
    serverslot_t *slot;
    int i, totalplayers = 0, totalservers = 0;

    for (i = 0; i < m_servers.list.numItems; i++) {
        slot = m_servers.list.items[i];
        if (slot->status == SLOT_VALID) {
            totalservers++;
            totalplayers += slot->numPlayers;
        }
    }

    Q_snprintf(m_servers.status_r, sizeof(m_servers.status_r),
               "%d player%s on %d server%s",
               totalplayers, totalplayers == 1 ? "" : "s",
               totalservers, totalservers == 1 ? "" : "s");
}

// doesn't free hostname!
static void FreeSlot(serverslot_t *slot)
{
    int i;

    for (i = 0; i < slot->numRules; i++)
        Z_Free(slot->rules[i]);
    for (i = 0; i < slot->numPlayers; i++)
        Z_Free(slot->players[i]);
    Z_Free(slot);
}

static serverslot_t *FindSlot(const netadr_t *search, int *index_p)
{
    serverslot_t *slot, *found = NULL;
    int i;

    for (i = 0; i < m_servers.list.numItems; i++) {
        slot = m_servers.list.items[i];
        if (!NET_IsEqualBaseAdr(search, &slot->address))
            continue;
        if (search->port && search->port != slot->address.port)
            continue;
        found = slot;
        break;
    }

    if (index_p)
        *index_p = i;
    return found;
}

static uint32_t ColorForStatus(const serverStatus_t *status, unsigned ping)
{
    if (Q_atoi(Info_ValueForKey(status->infostring, "needpass")) >= 1)
        return uis.color.disabled.u32;

    if (Q_atoi(Info_ValueForKey(status->infostring, "anticheat")) >= 2)
        return uis.color.disabled.u32;

    if (Q_stricmp(Info_ValueForKey(status->infostring, "NoFake"), "ENABLED") == 0)
        return uis.color.disabled.u32;

    if (ping < 30)
        return U32_GREEN;

    return U32_WHITE;
}

/*
=================
UI_StatusEvent

A server status response has been received, validated and parsed.
=================
*/
void UI_StatusEvent(const serverStatus_t *status)
{
    serverslot_t *slot;
    char *hostname;
    const char *host, *mod, *map, *maxclients;
    unsigned timestamp, ping;
    const char *info = status->infostring;
    char key[MAX_INFO_STRING];
    char value[MAX_INFO_STRING];
    int i;

    // ignore unless menu is up
    if (!m_servers.args) {
        return;
    }

    // see if already added
    slot = FindSlot(&net_from, &i);
    if (!slot) {
        // reply to broadcast, create new slot
        if (m_servers.list.numItems >= MAX_STATUS_SERVERS) {
            return;
        }
        m_servers.list.numItems++;
        hostname = UI_CopyString(NET_AdrToString(&net_from));
        timestamp = m_servers.timestamp;
    } else {
        // free previous data
        hostname = slot->hostname;
        timestamp = slot->timestamp;
        FreeSlot(slot);
    }

    host = Info_ValueForKey(info, "hostname");
    if (COM_IsWhite(host)) {
        host = hostname;
    }

    mod = Info_ValueForKey(info, "game");
    if (COM_IsWhite(mod)) {
        mod = "baseq2";
    }

    map = Info_ValueForKey(info, "mapname");
    if (COM_IsWhite(map)) {
        map = "???";
    }

    maxclients = Info_ValueForKey(info, "maxclients");
    if (!COM_IsUint(maxclients)) {
        maxclients = "?";
    }

    if (timestamp > com_eventTime)
        timestamp = com_eventTime;

    ping = com_eventTime - timestamp;
    if (ping > 999)
        ping = 999;

    slot = UI_FormatColumns(SLOT_EXTRASIZE, host, mod, map,
                            va("%d/%s", status->numPlayers, maxclients),
                            va("%u", ping),
                            NULL);
    slot->status = SLOT_VALID;
    slot->address = net_from;
    slot->hostname = hostname;
    slot->color = ColorForStatus(status, ping);

    m_servers.list.items[i] = slot;

    slot->numRules = 0;
    while (slot->numRules < MAX_STATUS_RULES) {
        Info_NextPair(&info, key, value);
        if (!info)
            break;

        if (!key[0])
            strcpy(key, "<MISSING KEY>");

        if (!value[0])
            strcpy(value, "<MISSING VALUE>");

        slot->rules[slot->numRules++] =
            UI_FormatColumns(0, key, value, NULL);
    }

    slot->numPlayers = status->numPlayers;
    for (i = 0; i < status->numPlayers; i++) {
        slot->players[i] =
            UI_FormatColumns(0,
                             va("%d", status->players[i].score),
                             va("%d", status->players[i].ping),
                             status->players[i].name,
                             NULL);
    }

    slot->timestamp = timestamp;

    // don't sort when manually refreshing
    if (m_servers.pingstage)
        m_servers.list.sort(&m_servers.list);

    UpdateStatus();
    UpdateSelection();
}

/*
=================
UI_ErrorEvent

An ICMP destination-unreachable error has been received.
=================
*/
void UI_ErrorEvent(netadr_t *from)
{
    serverslot_t *slot;
    netadr_t address;
    char *hostname;
    unsigned timestamp, ping;
    int i;

    // ignore unless menu is up
    if (!m_servers.args)
        return;

    slot = FindSlot(from, &i);
    if (!slot)
        return;

    // only mark unreplied slots as invalid
    if (slot->status != SLOT_PENDING)
        return;

    address = slot->address;
    hostname = slot->hostname;
    timestamp = slot->timestamp;
    FreeSlot(slot);

    if (timestamp > com_eventTime)
        timestamp = com_eventTime;

    ping = com_eventTime - timestamp;
    if (ping > 999)
        ping = 999;

    slot = UI_FormatColumns(SLOT_EXTRASIZE, hostname,
                            "???", "???", "down", va("%u", ping), NULL);
    slot->status = SLOT_ERROR;
    slot->address = address;
    slot->hostname = hostname;
    slot->color = U32_WHITE;
    slot->numRules = 0;
    slot->numPlayers = 0;
    slot->timestamp = timestamp;

    m_servers.list.items[i] = slot;
}

static menuSound_t SetRconAddress(void)
{
    serverslot_t *slot;

    if (!m_servers.list.numItems)
        return QMS_BEEP;
    if (m_servers.list.curvalue < 0)
        return QMS_BEEP;

    slot = m_servers.list.items[m_servers.list.curvalue];
    if (slot->status == SLOT_ERROR)
        return QMS_BEEP;

    Cvar_Set("rcon_address", slot->hostname);
    return QMS_OUT;
}

static menuSound_t CopyAddress(void)
{
    serverslot_t *slot;

    if (!m_servers.list.numItems)
        return QMS_BEEP;
    if (m_servers.list.curvalue < 0)
        return QMS_BEEP;

    slot = m_servers.list.items[m_servers.list.curvalue];

    if (vid.set_clipboard_data)
        vid.set_clipboard_data(slot->hostname);
    return QMS_OUT;
}

static menuSound_t PingSelected(void)
{
    serverslot_t *slot;
    netadr_t address;
    char *hostname;

    if (!m_servers.list.numItems)
        return QMS_BEEP;
    if (m_servers.list.curvalue < 0)
        return QMS_BEEP;

    slot = m_servers.list.items[m_servers.list.curvalue];
    address = slot->address;
    hostname = slot->hostname;
    FreeSlot(slot);

    slot = UI_FormatColumns(SLOT_EXTRASIZE, hostname,
                            "???", "???", "?/?", "???", NULL);
    slot->status = SLOT_PENDING;
    slot->address = address;
    slot->hostname = hostname;
    slot->color = U32_WHITE;
    slot->numRules = 0;
    slot->numPlayers = 0;
    slot->timestamp = com_eventTime;

    m_servers.list.items[m_servers.list.curvalue] = slot;

    UpdateStatus();
    UpdateSelection();

    CL_SendStatusRequest(&slot->address);
    return QMS_SILENT;
}

static void AddServer(const netadr_t *address, const char *hostname)
{
    netadr_t tmp;
    serverslot_t *slot;

    if (m_servers.list.numItems >= MAX_STATUS_SERVERS)
        return;

    if (!address) {
        // either address or hostname can be NULL, but not both
        if (!hostname)
            return;

        if (!NET_StringToAdr(hostname, &tmp, PORT_SERVER)) {
            Com_Printf("Bad server address: %s\n", hostname);
            return;
        }

        address = &tmp;
    }

    // ignore if already listed
    if (FindSlot(address, NULL))
        return;

    if (!hostname)
        hostname = NET_AdrToString(address);

    // privileged ports are not allowed
    if (BigShort(address->port) < 1024) {
        Com_Printf("Bad server port: %s\n", hostname);
        return;
    }

    slot = UI_FormatColumns(SLOT_EXTRASIZE, hostname,
                            "???", "???", "?/?", "???", NULL);
    slot->status = SLOT_IDLE;
    slot->address = *address;
    slot->hostname = UI_CopyString(hostname);
    slot->color = U32_WHITE;
    slot->numRules = 0;
    slot->numPlayers = 0;
    slot->timestamp = com_eventTime;

    m_servers.list.items[m_servers.list.numItems++] = slot;
}

static void ParsePlain(void *data, size_t len, size_t chunk)
{
    char *list, *p;

    if (!data)
        return;

    list = data;
    while (*list) {
        p = strchr(list, '\n');
        if (p) {
            if (p > list && *(p - 1) == '\r')
                *(p - 1) = 0;
            *p = 0;
        }

        if (*list)
            AddServer(NULL, list);

        if (!p)
            break;
        list = p + 1;
    }
}

static void ParseBinary(void *data, size_t len, size_t chunk)
{
    netadr_t address;
    byte *ptr;

    if (!data)
        return;

    memset(&address, 0, sizeof(address));
    address.type = NA_IP;

    ptr = data;
    while (len >= chunk) {
        memcpy(address.ip.u8, ptr, 4);
        memcpy(&address.port, ptr + 4, 2);
        ptr += chunk;
        len -= chunk;

        AddServer(&address, NULL);
    }
}

static void ParseAddressBook(void)
{
    cvar_t *var;
    int i;

    for (i = 0; i < MAX_STATUS_SERVERS; i++) {
        var = Cvar_FindVar(va("adr%i", i));
        if (!var)
            break;

        if (var->string[0])
            AddServer(NULL, var->string);
    }
}

static void ParseMasterArgs(netadr_t *broadcast)
{
    void *data;
    int len;
    void (*parse)(void *, size_t, size_t);
    size_t chunk;
    char *s, *p;
    int i, argc;

    Cmd_TokenizeString(m_servers.args, false);

    argc = Cmd_Argc();
    if (!argc) {
        // default action to take when no URLs are given
        ParseAddressBook();
        broadcast->type = NA_BROADCAST;
        broadcast->port = BigShort(PORT_SERVER);
        return;
    }

    for (i = 0; i < argc; i++) {
        s = Cmd_Argv(i);
        if (!*s)
            continue;

        // parse binary format specifier
        parse = ParsePlain;
        chunk = 0;
        if (*s == '+' || *s == '-') {
            parse = ParseBinary;
            chunk = strtoul(s, &p, 10);
            if (s == p) {
                chunk = 6;
                s = p + 1;
            } else {
                if (chunk < 6)
                    goto ignore;
                s = p;
            }
        }

        if (!strncmp(s, "file://", 7)) {
            len = FS_LoadFile(s + 7, &data);
            if (len < 0)
                continue;
            (*parse)(data, len, chunk);
            FS_FreeFile(data);
            continue;
        }

        if (!strncmp(s, "http://", 7) || !strncmp(s, "https://", 8)) {
#if USE_CURL
            len = HTTP_FetchFile(s, &data);
            if (len < 0)
                continue;
            (*parse)(data, len, chunk);
            free(data);
#else
            Com_Printf("Can't fetch '%s', no HTTP support compiled in.\n", s);
#endif
            continue;
        }

        if (!strncmp(s, "favorites://", 12)) {
            ParseAddressBook();
            continue;
        }

        if (!strncmp(s, "broadcast://", 12)) {
            broadcast->type = NA_BROADCAST;
            broadcast->port = BigShort(PORT_SERVER);
            continue;
        }

        if (!strncmp(s, "quake2://", 9)) {
            AddServer(NULL, s + 9);
            continue;
        }

ignore:
        Com_Printf("Ignoring invalid master URL: %s\n", s);
    }
}

static void ClearServers(void)
{
    serverslot_t *slot;
    int i;

    for (i = 0; i < m_servers.list.numItems; i++) {
        slot = m_servers.list.items[i];
        m_servers.list.items[i] = NULL;
        Z_Free(slot->hostname);
        FreeSlot(slot);
    }

    m_servers.list.numItems = 0;
    m_servers.list.curvalue = -1;
    m_servers.list.prestep = 0;
    m_servers.info.items = NULL;
    m_servers.info.numItems = 0;
    m_servers.players.items = NULL;
    m_servers.players.numItems = 0;
    m_servers.pingstage = 0;
}

static void FinishPingStage(void)
{
    m_servers.pingstage = 0;
    m_servers.pingindex = 0;
    m_servers.pingextra = 0;

    // if the user didn't select anything yet, select the first item
    if (m_servers.list.curvalue < 0)
        m_servers.list.curvalue = 0;

    UpdateSelection();
}

static void CalcPingRate(void)
{
    extern cvar_t *info_rate;

    // don't allow more than 100 packets/sec
    int rate = Cvar_ClampInteger(ui_pingrate, 0, 100);

    // assume average 450 bytes per reply packet
    if (!rate)
        rate = Q_clip(info_rate->integer / 450, 1, 100);

    // drop rate by stage
    m_servers.pingtime = (1000 * PING_STAGES) / (rate * m_servers.pingstage);
}

/*
=================
UI_Frame

=================
*/
void UI_Frame(int msec)
{
    serverslot_t *slot;

    if (!m_servers.pingstage)
        return;

    m_servers.pingextra += msec;
    if (m_servers.pingextra < m_servers.pingtime)
        return;

    m_servers.pingextra -= m_servers.pingtime;

    // send out next status packet
    while (m_servers.pingindex < m_servers.list.numItems) {
        slot = m_servers.list.items[m_servers.pingindex++];
        if (slot->status > SLOT_PENDING)
            continue;
        slot->status = SLOT_PENDING;
        slot->timestamp = com_eventTime;
        CL_SendStatusRequest(&slot->address);
        break;
    }

    if (m_servers.pingindex == m_servers.list.numItems) {
        m_servers.pingindex = 0;
        if (--m_servers.pingstage == 0)
            FinishPingStage();
        else
            CalcPingRate();
    }
}

static void PingServers(void)
{
    netadr_t broadcast;

    S_StopAllSounds();

    ClearServers();
    UpdateStatus();

    // update status string now, because fetching and
    // resolving will take some time
    m_servers.status_c = "Resolving servers, please wait...";
    SCR_UpdateScreen();

    // fetch and resolve servers
    memset(&broadcast, 0, sizeof(broadcast));
    ParseMasterArgs(&broadcast);

    m_servers.timestamp = Sys_Milliseconds();

    // optionally ping broadcast
    if (broadcast.type)
        CL_SendStatusRequest(&broadcast);

    if (!m_servers.list.numItems) {
        FinishPingStage();
        return;
    }

    // begin pinging servers
    m_servers.pingstage = PING_STAGES;
    m_servers.pingindex = 0;
    m_servers.pingextra = 0;
    CalcPingRate();
}

static int statuscmp(serverslot_t *s1, serverslot_t *s2)
{
    if (s1->status == s2->status)
        return 0;
    if (s1->status != SLOT_VALID && s2->status == SLOT_VALID)
        return 1;
    if (s2->status != SLOT_VALID && s1->status == SLOT_VALID)
        return -1;
    return 0;
}

static int namecmp(serverslot_t *s1, serverslot_t *s2, int col)
{
    char *n1 = UI_GetColumn(s1->name, col);
    char *n2 = UI_GetColumn(s2->name, col);

    return Q_stricmp(n1, n2) * m_servers.list.sortdir;
}

static int pingcmp(serverslot_t *s1, serverslot_t *s2)
{
    int n1 = Q_atoi(UI_GetColumn(s1->name, COL_RTT));
    int n2 = Q_atoi(UI_GetColumn(s2->name, COL_RTT));

    return (n1 - n2) * m_servers.list.sortdir;
}

static int playercmp(serverslot_t *s1, serverslot_t *s2)
{
    return (s2->numPlayers - s1->numPlayers) * m_servers.list.sortdir;
}

static int addresscmp(serverslot_t *s1, serverslot_t *s2)
{
    if (s1->address.ip.u32 > s2->address.ip.u32)
        return 1;
    if (s1->address.ip.u32 < s2->address.ip.u32)
        return -1;
    if (s1->address.port > s2->address.port)
        return 1;
    if (s1->address.port < s2->address.port)
        return -1;
    return 0;
}

static int slotcmp(const void *p1, const void *p2)
{
    serverslot_t *s1 = *(serverslot_t **)p1;
    serverslot_t *s2 = *(serverslot_t **)p2;
    int r;

    // sort by validity
    r = statuscmp(s1, s2);
    if (r)
        return r;

    // sort by primary column
    switch (m_servers.list.sortcol) {
    case COL_NAME:
        break;
    case COL_MOD:
        r = namecmp(s1, s2, COL_MOD);
        break;
    case COL_MAP:
        r = namecmp(s1, s2, COL_MAP);
        break;
    case COL_PLAYERS:
        r = playercmp(s1, s2);
        break;
    case COL_RTT:
        r = pingcmp(s1, s2);
        break;
    }
    if (r)
        return r;

    // stabilize sort
    r = namecmp(s1, s2, COL_NAME);
    if (r)
        return r;

    return addresscmp(s1, s2);
}

static menuSound_t Sort(menuList_t *self)
{
    MenuList_Sort(&m_servers.list, 0, slotcmp);
    return QMS_SILENT;
}

static void ui_sortservers_changed(cvar_t *self)
{
    int i = Cvar_ClampInteger(self, -COL_MAX, COL_MAX);

    if (i > 0) {
        // ascending
        m_servers.list.sortdir = 1;
        m_servers.list.sortcol = i - 1;
    } else if (i < 0) {
        // descending
        m_servers.list.sortdir = -1;
        m_servers.list.sortcol = -i - 1;
    } else {
        // don't sort
        m_servers.list.sortdir = 1;
        m_servers.list.sortcol = -1;
    }

    if (m_servers.list.numItems) {
        m_servers.list.sort(&m_servers.list);
    }
}

static menuSound_t Connect(menuCommon_t *self)
{
    serverslot_t *slot;

    if (!m_servers.list.numItems)
        return QMS_BEEP;
    if (m_servers.list.curvalue < 0)
        return QMS_BEEP;

    slot = m_servers.list.items[m_servers.list.curvalue];
    if (slot->status == SLOT_ERROR)
        return QMS_BEEP;

    Cbuf_AddText(&cmd_buffer, va("connect %s\n", slot->hostname));
    return QMS_SILENT;
}

static menuSound_t Change(menuCommon_t *self)
{
    UpdateSelection();
    return QMS_MOVE;
}

static void SizeCompact(void)
{
    int w = uis.width - MLIST_SCROLLBAR_WIDTH;

//
// server list
//
    m_servers.list.generic.x            = 0;
    m_servers.list.generic.y            = CHAR_HEIGHT;
    m_servers.list.generic.height       = uis.height / 2 - CHAR_HEIGHT;

    m_servers.list.columns[0].width     = w - 10 * CHAR_WIDTH - MLIST_PADDING * 2;
    m_servers.list.columns[1].width     = 0;
    m_servers.list.columns[2].width     = 0;
    m_servers.list.columns[3].width     = 7 * CHAR_WIDTH + MLIST_PADDING;
    m_servers.list.columns[4].width     = 3 * CHAR_WIDTH + MLIST_PADDING;

//
// player list
//
    m_servers.players.generic.x         = 0;
    m_servers.players.generic.y         = uis.height / 2 + 1;
    m_servers.players.generic.height    = (uis.height + 1) / 2 - CHAR_HEIGHT - 2;

    m_servers.players.columns[0].width  = 3 * CHAR_WIDTH + MLIST_PADDING;
    m_servers.players.columns[1].width  = 3 * CHAR_WIDTH + MLIST_PADDING;
    m_servers.players.columns[2].width  = w - 6 * CHAR_WIDTH - MLIST_PADDING * 2;

    m_servers.players.mlFlags           |= MLF_SCROLLBAR;
}

static void SizeFull(void)
{
    int w = uis.width - MLIST_SCROLLBAR_WIDTH - 21 * CHAR_WIDTH - MLIST_PADDING * 3;

//
// server list
//
    m_servers.list.generic.x            = 0;
    m_servers.list.generic.y            = CHAR_HEIGHT;
    m_servers.list.generic.height       = uis.height / 2 - CHAR_HEIGHT;

    m_servers.list.columns[0].width     = w - 26 * CHAR_WIDTH - MLIST_PADDING * 4;
    m_servers.list.columns[1].width     = 8 * CHAR_WIDTH + MLIST_PADDING;
    m_servers.list.columns[2].width     = 8 * CHAR_WIDTH + MLIST_PADDING;
    m_servers.list.columns[3].width     = 7 * CHAR_WIDTH + MLIST_PADDING;
    m_servers.list.columns[4].width     = 3 * CHAR_WIDTH + MLIST_PADDING;

//
// server info
//
    m_servers.info.generic.x            = 0;
    m_servers.info.generic.y            = uis.height / 2 + 1;
    m_servers.info.generic.height       = (uis.height + 1) / 2 - CHAR_HEIGHT - 2;

    m_servers.info.columns[0].width     = w / 3;
    m_servers.info.columns[1].width     = w - w / 3;

//
// player list
//
    m_servers.players.generic.x         = w + MLIST_SCROLLBAR_WIDTH;
    m_servers.players.generic.y         = CHAR_HEIGHT;
    m_servers.players.generic.height    = uis.height - CHAR_HEIGHT * 2 - 1;

    m_servers.players.columns[0].width  = 3 * CHAR_WIDTH + MLIST_PADDING;
    m_servers.players.columns[1].width  = 3 * CHAR_WIDTH + MLIST_PADDING;
    m_servers.players.columns[2].width  = 15 * CHAR_WIDTH + MLIST_PADDING;

    m_servers.players.mlFlags           &= ~MLF_SCROLLBAR;
}

static void Size(menuFrameWork_t *self)
{
    if (uis.width >= 640)
        SizeFull();
    else
        SizeCompact();
    UpdateSelection();
}

static menuSound_t Keydown(menuFrameWork_t *self, int key)
{
    // ignore autorepeats
    if (Key_IsDown(key) > 1)
        return QMS_NOTHANDLED;

    switch (key) {
    case 'r':
        if (Key_IsDown(K_CTRL))
            return SetRconAddress();
        return QMS_NOTHANDLED;

    case 'c':
        if (Key_IsDown(K_CTRL))
            return CopyAddress();
        return QMS_NOTHANDLED;

    case K_SPACE:
        if (Key_IsDown(K_ALT) || !m_servers.list.numItems) {
            PingServers();
            return QMS_SILENT;
        }
        return PingSelected();

    case K_BACKSPACE:
        if (m_servers.pingstage) {
            FinishPingStage();
            return QMS_OUT;
        }
        return QMS_SILENT;

    default:
        return QMS_NOTHANDLED;
    }
}

static void DrawStatus(void)
{
    int w;

    if (m_servers.pingstage == PING_STAGES)
        w = m_servers.pingindex * uis.width / m_servers.list.numItems;
    else
        w = uis.width;

    R_DrawFill8(0, uis.height - CHAR_HEIGHT, w, CHAR_HEIGHT, 4);
    R_DrawFill8(w, uis.height - CHAR_HEIGHT, uis.width - w, CHAR_HEIGHT, 0);

    if (m_servers.status_c)
        UI_DrawString(uis.width / 2, uis.height - CHAR_HEIGHT, UI_CENTER, m_servers.status_c);

    if (uis.width < 800)
        return;

    if (m_servers.list.numItems)
        UI_DrawString(uis.width, uis.height - CHAR_HEIGHT, UI_RIGHT, m_servers.status_r);

    if (m_servers.list.numItems && m_servers.list.curvalue >= 0) {
        serverslot_t *slot = m_servers.list.items[m_servers.list.curvalue];
        if (slot->status > SLOT_PENDING) {
            UI_DrawString(0, uis.height - CHAR_HEIGHT, UI_LEFT, slot->hostname);
        }
    }
}

static void Draw(menuFrameWork_t *self)
{
    Menu_Draw(self);
    DrawStatus();
}

static bool Push(menuFrameWork_t *self)
{
    // save our arguments for refreshing
    m_servers.args = UI_CopyString(COM_StripQuotes(Cmd_RawArgsFrom(2)));
    return true;
}

static void Pop(menuFrameWork_t *self)
{
    ClearServers();
    Z_Freep((void**)&m_servers.args);
}

static void Expose(menuFrameWork_t *self)
{
    PingServers();
    ui_sortservers_changed(ui_sortservers);
}

static void Free(menuFrameWork_t *self)
{
    Z_Free(m_servers.menu.items);
    memset(&m_servers, 0, sizeof(m_servers));
}

static void ui_colorservers_changed(cvar_t *self)
{
    if (self->integer)
        m_servers.list.mlFlags |= MLF_COLOR;
    else
        m_servers.list.mlFlags &= ~MLF_COLOR;
}

void M_Menu_Servers(void)
{
    ui_sortservers = Cvar_Get("ui_sortservers", "0", 0);
    ui_sortservers->changed = ui_sortservers_changed;
    ui_colorservers = Cvar_Get("ui_colorservers", "0", 0);
    ui_colorservers->changed = ui_colorservers_changed;
    ui_pingrate = Cvar_Get("ui_pingrate", "0", 0);

    m_servers.menu.name     = "servers";
    m_servers.menu.title    = "Server Browser";

    m_servers.menu.draw         = Draw;
    m_servers.menu.expose       = Expose;
    m_servers.menu.push         = Push;
    m_servers.menu.pop          = Pop;
    m_servers.menu.size         = Size;
    m_servers.menu.keydown      = Keydown;
    m_servers.menu.free         = Free;
    m_servers.menu.image        = uis.backgroundHandle;
    m_servers.menu.color.u32    = uis.color.background.u32;
    m_servers.menu.transparent  = uis.transparent;

//
// server list
//
    m_servers.list.generic.type         = MTYPE_LIST;
    m_servers.list.generic.flags        = QMF_LEFT_JUSTIFY | QMF_HASFOCUS;
    m_servers.list.generic.activate     = Connect;
    m_servers.list.generic.change       = Change;
    m_servers.list.items                = m_servers.names;
    m_servers.list.numcolumns           = COL_MAX;
    m_servers.list.sortdir              = 1;
    m_servers.list.sortcol              = -1;
    m_servers.list.sort                 = Sort;
    m_servers.list.extrasize            = SLOT_EXTRASIZE;
    m_servers.list.mlFlags              = MLF_HEADER | MLF_SCROLLBAR;

    m_servers.list.columns[0].uiFlags   = UI_LEFT;
    m_servers.list.columns[0].name      = "Hostname";
    m_servers.list.columns[1].uiFlags   = UI_CENTER;
    m_servers.list.columns[1].name      = "Mod";
    m_servers.list.columns[2].uiFlags   = UI_CENTER;
    m_servers.list.columns[2].name      = "Map";
    m_servers.list.columns[3].uiFlags   = UI_CENTER;
    m_servers.list.columns[3].name      = "Players";
    m_servers.list.columns[4].uiFlags   = UI_RIGHT;
    m_servers.list.columns[4].name      = "RTT";

    ui_colorservers_changed(ui_colorservers);

//
// server info
//
    m_servers.info.generic.type         = MTYPE_LIST;
    m_servers.info.generic.flags        = QMF_LEFT_JUSTIFY | QMF_HIDDEN;
    m_servers.info.generic.activate     = Connect;
    m_servers.info.numcolumns           = 2;
    m_servers.info.mlFlags              = MLF_HEADER | MLF_SCROLLBAR;

    m_servers.info.columns[0].uiFlags   = UI_LEFT;
    m_servers.info.columns[0].name      = "Key";
    m_servers.info.columns[1].uiFlags   = UI_LEFT;
    m_servers.info.columns[1].name      = "Value";

//
// player list
//
    m_servers.players.generic.type      = MTYPE_LIST;
    m_servers.players.generic.flags     = QMF_LEFT_JUSTIFY | QMF_HIDDEN;
    m_servers.players.generic.activate  = Connect;
    m_servers.players.numcolumns        = 3;
    m_servers.players.mlFlags           = MLF_HEADER;

    m_servers.players.columns[0].uiFlags    = UI_RIGHT;
    m_servers.players.columns[0].name       = "Frg";
    m_servers.players.columns[1].uiFlags    = UI_RIGHT;
    m_servers.players.columns[1].name       = "RTT";
    m_servers.players.columns[2].uiFlags    = UI_LEFT;
    m_servers.players.columns[2].name       = "Name";

    Menu_AddItem(&m_servers.menu, &m_servers.list);
    Menu_AddItem(&m_servers.menu, &m_servers.info);
    Menu_AddItem(&m_servers.menu, &m_servers.players);

    List_Append(&ui_menus, &m_servers.menu.entry);
}

