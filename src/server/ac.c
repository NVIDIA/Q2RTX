/*
Copyright (C) 2006 r1ch.net

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

//
// r1ch.net anticheat server interface for Quake II
//

#include "server.h"

typedef enum {
    ACS_BAD,
    ACS_CLIENTACK,
    ACS_VIOLATION,
    ACS_NOACCESS,
    ACS_FILE_VIOLATION,
    ACS_READY,
    ACS_QUERYREPLY,
    ACS_PONG,
    ACS_UPDATE_REQUIRED,
    ACS_DISCONNECT,
    ACS_ERROR
} ac_serverbyte_t;

typedef enum {
    ACC_BAD,
    ACC_VERSION,
    ACC_PREF,
    ACC_REQUESTCHALLENGE,
    ACC_CLIENTDISCONNECT,
    ACC_QUERYCLIENT,
    ACC_PING,
    ACC_UPDATECHECKS,
    ACC_SETPREFERENCES
} ac_clientbyte_t;

typedef enum {
    OP_INVALID,
    OP_EQUAL,
    OP_NEQUAL,
    OP_GTEQUAL,
    OP_LTEQUAL,
    OP_LT,
    OP_GT,
    OP_STREQUAL,
    OP_STRNEQUAL,
    OP_STRSTR
} ac_opcode_t;

typedef enum {
    AC_CLIENT_R1Q2  = 0x01,
    AC_CLIENT_EGL   = 0x02,
    AC_CLIENT_APRGL = 0x04,
    AC_CLIENT_APRSW = 0x08,
    AC_CLIENT_Q2PRO = 0x10
} ac_client_t;

typedef struct ac_file_s {
    struct ac_file_s *next;
    byte hash[20];
    int flags;
    char path[1];
} ac_file_t;

typedef struct ac_cvar_s {
    struct ac_cvar_s *next;
    int num_values;
    char **values;
    ac_opcode_t op;
    char *def, *name;
} ac_cvar_t;

typedef struct {
    bool connected;
    bool ready;
    bool ping_pending;
    unsigned last_ping;
    netstream_t stream;
    size_t msglen;
} ac_locals_t;

typedef struct {
    time_t retry_time;
    int retry_backoff;

    ac_file_t *files;
    int num_files;

    ac_cvar_t *cvars;
    int num_cvars;

    string_entry_t *tokens;

    char hashlist_name[MAX_QPATH];
} ac_static_t;

#define ACP_BLOCKPLAY   BIT(0)

#define ACH_REQUIRED    BIT(0)
#define ACH_NEGATIVE    BIT(1)

#define AC_PROTOCOL_VERSION     0xAC03

#define AC_DEFAULT_BACKOFF  30

#define AC_PING_INTERVAL    60000
#define AC_PING_TIMEOUT     15000

#define AC_MESSAGE  "\220\xe1\xee\xf4\xe9\xe3\xe8\xe5\xe1\xf4\221 "

#define AC_SEND_SIZE    131072
#define AC_RECV_SIZE    1024

static ac_locals_t  ac;
static ac_static_t  acs;

static LIST_DECL(ac_required_list);
static LIST_DECL(ac_exempt_list);

static byte     ac_send_buffer[AC_SEND_SIZE];
static byte     ac_recv_buffer[AC_RECV_SIZE];

static cvar_t   *ac_required;
static cvar_t   *ac_server_address;
static cvar_t   *ac_error_action;
static cvar_t   *ac_message;
static cvar_t   *ac_badfile_action;
static cvar_t   *ac_badfile_message;
static cvar_t   *ac_badfile_max;
static cvar_t   *ac_show_violation_reason;
static cvar_t   *ac_client_disconnect_action;
static cvar_t   *ac_disable_play;

static const char ac_clients[][8] = {
    "???",
    "R1Q2",
    "EGL",
    "Apr GL",
    "Apr SW",
    "Q2PRO"
};

static const int ac_num_clients = q_countof(ac_clients);


/*
==============================================================================

FILE PARSING

==============================================================================
*/

#define AC_HASHES_NAME  "anticheat-hashes.txt"
#define AC_CVARS_NAME   "anticheat-cvars.txt"
#define AC_TOKENS_NAME  "anticheat-tokens.txt"

#define AC_MAX_INCLUDES 16

typedef void (*ac_parse_t)(char *, int, const char *);

typedef struct {
    char str[4];
    ac_opcode_t code;
    int max_values;
} ac_cvarop_t;

static const ac_cvarop_t ac_cvarops[] = {
    { "=", OP_EQUAL, 254 },
    { "==", OP_EQUAL, 254 },
    { "!=", OP_NEQUAL, 254 },
    { ">=", OP_GTEQUAL, 1 },
    { "<=", OP_LTEQUAL, 1 },
    { "<", OP_LT, 1 },
    { ">", OP_GT, 1 },
    { "eq", OP_STREQUAL, 254 },
    { "ne", OP_STRNEQUAL, 254 },
    { "~", OP_STRSTR, 254 },
    { "" }
};

static char *AC_SimpleParse(char **data_p, size_t *len_p)
{
    char *data, *p;

    data = *data_p;
    if (!data) {
        if (len_p) {
            *len_p = 0;
        }
        return NULL;
    }

    p = Q_strchrnul(data, '\t');
    if (*p) {
        *p = 0;
        *data_p = p + 1;
    } else {
        *data_p = NULL;
    }

    if (len_p) {
        *len_p = p - data;
    }

    return data;
}

static void AC_ParseHash(char *data, int linenum, const char *path)
{
    char *pstr, *hstr;
    size_t pathlen, hashlen;
    int flags;
    byte hash[20];
    ac_file_t *file;
    int i;

    if (*data == '!') {
        Q_strlcpy(acs.hashlist_name, data + 1, sizeof(acs.hashlist_name));
        return;
    }

    pstr = AC_SimpleParse(&data, &pathlen);
    if (!data) {
        Com_WPrintf("ANTICHEAT: Incomplete line %d in %s\n", linenum, path);
        return;
    }
    hstr = AC_SimpleParse(&data, &hashlen);

    if (pathlen < 1 || pathlen >= MAX_QPATH) {
        Com_WPrintf("ANTICHEAT: Invalid quake path length on line %d in %s\n", linenum, path);
        return;
    }
    if (strchr(pstr, '\\') || !Q_isalnum(pstr[0])) {
        Com_WPrintf("ANTICHEAT: Malformed quake path on line %d in %s\n", linenum, path);
        return;
    }

    if (hashlen != 40) {
badhash:
        Com_WPrintf("ANTICHEAT: Malformed hash on line %d in %s\n", linenum, path);
        return;
    }

    for (i = 0; i < 20; i++, hstr += 2) {
        int c1 = Q_charhex(hstr[0]);
        int c2 = Q_charhex(hstr[1]);
        if (c1 == -1 || c2 == -1) {
            goto badhash;
        }
        hash[i] = (c1 << 4) | c2;
    }

    // parse optional flags
    flags = 0;
    if (data) {
        if (strstr(data, "required")) {
            flags |= ACH_REQUIRED;
        }
        if (strstr(data, "negative")) {
            flags |= ACH_NEGATIVE;
        }
    }

    file = SV_Malloc(sizeof(*file) + pathlen);
    memcpy(file->hash, hash, sizeof(file->hash));
    memcpy(file->path, pstr, pathlen + 1);
    file->flags = flags;
    file->next = acs.files;
    acs.files = file;
    acs.num_files++;
}

static void AC_ParseCvar(char *data, int linenum, const char *path)
{
    char *values[256], *p;
    char *name, *opstr, *val, *def;
    size_t len, namelen, vallen, deflen;
    ac_cvar_t *cvar;
    const ac_cvarop_t *op;
    int i, num_values;

    name = AC_SimpleParse(&data, &namelen);
    if (!data) {
        Com_WPrintf("ANTICHEAT: Incomplete line %d in %s\n", linenum, path);
        return;
    }
    opstr = AC_SimpleParse(&data, NULL);
    if (!data) {
        Com_WPrintf("ANTICHEAT: Incomplete line %d in %s\n", linenum, path);
        return;
    }
    val = AC_SimpleParse(&data, &vallen);
    if (!data) {
        Com_WPrintf("ANTICHEAT: Incomplete line %d in %s\n", linenum, path);
        return;
    }
    def = AC_SimpleParse(&data, &deflen);

    if (namelen < 1 || namelen >= 64) {
        Com_WPrintf("ANTICHEAT: Invalid cvar name length on line %d in %s\n", linenum, path);
        return;
    }
    if (deflen < 1 || deflen >= 64) {
        Com_WPrintf("ANTICHEAT: Invalid default value length on line %d in %s\n", linenum, path);
        return;
    }

    for (op = ac_cvarops; op->str[0]; op++) {
        if (!strcmp(opstr, op->str)) {
            break;
        }
    }
    if (!op->str[0]) {
        Com_WPrintf("ANTICHEAT: Unknown opcode '%s' on line %d in %s\n", opstr, linenum, path);
        return;
    }

    num_values = 0;
    while (1) {
        if (num_values == op->max_values) {
            Com_WPrintf("ANTICHEAT: Too many values for opcode '%s' on line %d in %s\n", opstr, linenum, path);
            return;
        }
        if (!val[0]) {
            Com_WPrintf("ANTICHEAT: Empty value on line %d in %s\n", linenum, path);
            return;
        }
        p = strchr(val, ',');
        if (p) {
            *p = 0;
        }
        len = strlen(val);
        if (len >= 64) {
            Com_WPrintf("ANTICHEAT: Too long value on line %d in %s\n", linenum, path);
            return;
        }
        values[num_values++] = val;
        if (!p) {
            break;
        }
        val = p + 1;
    }

    cvar = SV_Malloc(sizeof(*cvar));
    cvar->values = SV_Malloc(num_values * sizeof(char *));
    cvar->name = SV_CopyString(name);
    cvar->def = SV_CopyString(def);
    cvar->num_values = num_values;
    for (i = 0; i < num_values; i++) {
        cvar->values[i] = SV_CopyString(values[i]);
    }
    cvar->op = op->code;
    cvar->next = acs.cvars;
    acs.cvars = cvar;
    acs.num_cvars++;
}

static void AC_FreeCvar(ac_cvar_t *cvar)
{
    for (int i = 0; i < cvar->num_values; i++)
        Z_Free(cvar->values[i]);
    Z_Free(cvar->values);
    Z_Free(cvar->def);
    Z_Free(cvar->name);
    Z_Free(cvar);
}

static void AC_ParseToken(char *data, int linenum, const char *path)
{
    string_entry_t *tok;
    size_t len = strlen(data);

    tok = SV_Malloc(sizeof(*tok) + len);
    memcpy(tok->string, data, len + 1);
    tok->next = acs.tokens;
    acs.tokens = tok;
}

static bool AC_ParseFile(const char *path, ac_parse_t parse, int depth)
{
    char *raw, *data, *p;
    int linenum = 1;
    int ret;

    ret = FS_LoadFile(path, (void **)&raw);
    if (!raw) {
        if (ret != Q_ERR(ENOENT) || depth) {
            Com_WPrintf("ANTICHEAT: Could not %s %s: %s\n",
                        depth ? "include" : "load", path, Q_ErrorString(ret));
        }
        return false;
    }

    data = raw;
    while (*data) {
        p = strchr(data, '\n');
        if (p) {
            if (p > data && *(p - 1) == '\r') {
                *(p - 1) = 0;
            }
            *p = 0;
        }

        switch (*data) {
        case '/':
        case '#':
        case 0:
            break;
        case '\\':
            if (!strncmp(data + 1, "include ", 8)) {
                if (depth == AC_MAX_INCLUDES) {
                    Com_WPrintf("ANTICHEAT: Includes too deeply nested.\n");
                } else {
                    AC_ParseFile(data + 9, parse, depth + 1);
                }
            } else {
                Com_WPrintf("ANTICHEAT: Unknown directive %s on line %d in %s\n", data + 1, linenum, path);
            }
            break;
        default:
            parse(data, linenum, path);
            break;
        }

        if (!p) {
            break;
        }

        linenum++;
        data = p + 1;
    }

    FS_FreeFile(raw);

    return true;
}

static void AC_LoadChecks(void)
{
    if (!AC_ParseFile(AC_HASHES_NAME, AC_ParseHash, 0)) {
        Com_Printf("ANTICHEAT: Missing " AC_HASHES_NAME ", "
                   "not using any file checks.\n");
        strcpy(acs.hashlist_name, "none");
    } else if (!acs.num_files) {
        Com_Printf("ANTICHEAT: No file hashes were loaded, "
                   "please check the " AC_HASHES_NAME ".\n");
        strcpy(acs.hashlist_name, "none");
    } else if (!acs.hashlist_name[0]) {
        Q_snprintf(acs.hashlist_name, MAX_QPATH, "unknown (%d %s)",
                   acs.num_files, acs.num_files == 1 ? "entry" : "entries");
    }

    if (!AC_ParseFile(AC_CVARS_NAME, AC_ParseCvar, 0)) {
        Com_Printf("ANTICHEAT: Missing " AC_CVARS_NAME ", "
                   "not using any cvar checks.\n");
    } else if (!acs.num_cvars) {
        Com_Printf("ANTICHEAT: No cvar checks were loaded, "
                   "please check the " AC_CVARS_NAME ".\n");
    }

    AC_ParseFile("anticheat-tokens.txt", AC_ParseToken, 0);
}

static void AC_FreeChecks(void)
{
    ac_file_t *f, *fn;
    ac_cvar_t *c, *cn;
    string_entry_t *t, *tn;

    for (f = acs.files; f; f = fn) {
        fn = f->next;
        Z_Free(f);
    }
    acs.files = NULL;

    for (c = acs.cvars; c; c = cn) {
        cn = c->next;
        AC_FreeCvar(c);
    }
    acs.cvars = NULL;

    for (t = acs.tokens; t; t = tn) {
        tn = t->next;
        Z_Free(t);
    }
    acs.tokens = NULL;

    acs.hashlist_name[0] = 0;
    acs.num_files = 0;
    acs.num_cvars = 0;
}

/*
==============================================================================

REPLY PARSING

==============================================================================
*/

static void AC_Retry(void)
{
    char buf[MAX_QPATH];
    time_t clock;

    Com_FormatTimeLong(buf, sizeof(buf), acs.retry_backoff);
    Com_Printf("ANTICHEAT: Re%s in %s.\n",
               ac.connected ? "connecting" : "trying", buf);
    clock = time(NULL);
    acs.retry_time = clock + acs.retry_backoff;
}

static void AC_Drop(void)
{
    client_t *cl;

    NET_CloseStream(&ac.stream);

    if (!ac.connected) {
        Com_Printf("ANTICHEAT: Server connection failed.\n");
        AC_Retry();
        acs.retry_backoff += 5;
        return;
    }

    FOR_EACH_CLIENT(cl) {
        cl->ac_valid = false;
        cl->ac_file_failures = 0;
    }

    // inform
    if (ac.ready) {
        SV_BroadcastPrintf(PRINT_HIGH, AC_MESSAGE
                           "This server has lost the connection to the anticheat server. "
                           "Any anticheat clients are no longer valid.\n");

        if (ac_required->integer == 2) {
            SV_BroadcastPrintf(PRINT_HIGH, AC_MESSAGE
                               "You will need to reconnect once the server has "
                               "re-established the anticheat connection.\n");
        }
        acs.retry_backoff = AC_DEFAULT_BACKOFF;
    } else {
        acs.retry_backoff += 30; // this generally indicates a server problem
    }

    Com_WPrintf("ANTICHEAT: Lost connection to anticheat server!\n");
    AC_Retry();

    memset(&ac, 0, sizeof(ac));
}

static void AC_Disable(void)
{
    AC_Disconnect();
    Cvar_SetByVar(ac_required, "0", FROM_CODE);
}

static void AC_Announce(client_t *client, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_print);
    MSG_WriteByte(PRINT_HIGH);
    MSG_WriteData(AC_MESSAGE, sizeof(AC_MESSAGE) - 1);
    MSG_WriteData(string, len + 1);

    if (client->state == cs_spawned) {
        FOR_EACH_CLIENT(client) {
            if (client->state == cs_spawned) {
                SV_ClientAddMessage(client, MSG_RELIABLE);
            }
        }
    } else {
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}

static client_t *AC_ParseClient(void)
{
    client_t *cl;
    unsigned clientID;
    unsigned challenge;

    if (msg_read.readcount + 6 > msg_read.cursize) {
        Com_DPrintf("ANTICHEAT: Message too short in %s\n", __func__);
        return NULL;
    }

    clientID = MSG_ReadWord();
    challenge = MSG_ReadLong();

    if (clientID >= sv_maxclients->integer) {
        Com_WPrintf("ANTICHEAT: Illegal client ID: %u\n", clientID);
        return NULL;
    }

    cl = &svs.client_pool[clientID];

    // we check challenge to ensure we don't get
    // a race condition if a client reconnects.
    if (cl->challenge != challenge) {
        return NULL;
    }

    if (cl->state < cs_assigned) {
        return NULL;
    }

    return cl;
}

static void AC_ParseViolation(void)
{
    client_t        *cl;
    char            reason[32];
    char            clientreason[64];

    cl = AC_ParseClient();
    if (!cl) {
        return;
    }

    if (msg_read.readcount + 1 > msg_read.cursize) {
        Com_DPrintf("ANTICHEAT: Message too short in %s\n", __func__);
        return;
    }

    MSG_ReadString(reason, sizeof(reason));

    if (msg_read.readcount < msg_read.cursize) {
        MSG_ReadString(clientreason, sizeof(clientreason));
    } else {
        clientreason[0] = 0;
    }

    // FIXME: should we notify other players about anticheat violations
    // found before clientbegin? one side says yes to expose cheaters,
    // other side says no since client will have no previous message to
    // show that they're trying to join. currently showing messages only
    // for spawned clients.

    // fixme maybe
    if (strcmp(reason, "disconnected")) {
        char    showreason[32];

        if (ac_show_violation_reason->integer)
            Q_snprintf(showreason, sizeof(showreason), " (%s)", reason);
        else
            showreason[0] = 0;

        AC_Announce(cl, "%s was kicked for anticheat violation%s\n",
                    cl->name, showreason);

        Com_Printf("ANTICHEAT VIOLATION: %s[%s] was kicked: %s\n",
                   cl->name, NET_AdrToString(&cl->netchan.remote_address), reason);

        if (clientreason[0])
            SV_ClientPrintf(cl, PRINT_HIGH, "%s\n", clientreason);

        // hack to fix late zombies race condition
        cl->lastmessage = svs.realtime;
        SV_DropClient(cl, NULL);
        return;
    }

    if (!cl->ac_valid) {
        return;
    }

    Com_Printf("ANTICHEAT DISCONNECT: %s[%s] disconnected from "
               "anticheat server\n", cl->name,
               NET_AdrToString(&cl->netchan.remote_address));

    if (ac_client_disconnect_action->integer == 1) {
        AC_Announce(cl, "%s lost connection to anticheat server.\n", cl->name);
        SV_DropClient(cl, NULL);
        return;
    }

    AC_Announce(cl, "%s lost connection to anticheat server, "
                "client is no longer valid.\n", cl->name);
    cl->ac_valid = false;
}

static void AC_ParseClientAck(void)
{
    client_t        *cl;

    cl = AC_ParseClient();
    if (!cl) {
        return;
    }

    if (msg_read.readcount + 1 > msg_read.cursize) {
        Com_DPrintf("ANTICHEAT: Message too short in %s\n", __func__);
        return;
    }

    if (cl->state > cs_primed) {
        Com_DPrintf("ANTICHEAT: %s with client in state %d\n",
                    __func__, cl->state);
        return;
    }

    Com_DPrintf("ANTICHEAT: %s for %s\n", __func__, cl->name);
    cl->ac_client_type = MSG_ReadByte();
    cl->ac_valid = true;
}

static void AC_ParseFileViolation(void)
{
    string_entry_t    *bad;
    client_t    *cl;
    char        path[MAX_QPATH];
    char        hash[MAX_QPATH];
    int         action;
    size_t      pathlen;
    ac_file_t   *f;

    cl = AC_ParseClient();
    if (!cl) {
        return;
    }

    if (msg_read.readcount + 1 > msg_read.cursize) {
        Com_DPrintf("ANTICHEAT: Message too short in %s\n", __func__);
        return;
    }

    pathlen = MSG_ReadString(path, sizeof(path));
    if (pathlen >= sizeof(path)) {
        Com_WPrintf("ANTICHEAT: Oversize path in %s\n", __func__);
        pathlen = sizeof(path) - 1;
    }

    if (msg_read.readcount < msg_read.cursize) {
        MSG_ReadString(hash, sizeof(hash));
    } else {
        strcpy(hash, "no hash?");
    }

    cl->ac_file_failures++;

    action = ac_badfile_action->integer;
    for (f = acs.files; f; f = f->next) {
        if (!strcmp(f->path, path)) {
            if (f->flags & ACH_REQUIRED) {
                action = 0;
                break;
            }
        }
    }

    Com_Printf("ANTICHEAT FILE VIOLATION: %s[%s] has a modified %s [%s]\n",
               cl->name, NET_AdrToString(&cl->netchan.remote_address), path, hash);
    switch (action) {
    case 0:
        AC_Announce(cl, "%s was kicked for modified %s\n", cl->name, path);
        break;
    case 1:
        SV_ClientPrintf(cl, PRINT_HIGH, AC_MESSAGE
                        "Your file %s has been modified. "
                        "Please replace it with a known valid copy.\n", path);
        break;
    case 2:
        // spamalicious :)
        AC_Announce(cl, "%s has a modified %s\n", cl->name, path);
        break;
    }

    // show custom msg
    if (ac_badfile_message->string[0]) {
        SV_ClientPrintf(cl, PRINT_HIGH, "%s\n", ac_badfile_message->string);
    }

    if (!action) {
        SV_DropClient(cl, NULL);
        return;
    }

    if (ac_badfile_max->integer > 0 && cl->ac_file_failures > ac_badfile_max->integer) {
        AC_Announce(cl, "%s was kicked for too many modified files\n", cl->name);
        SV_DropClient(cl, NULL);
        return;
    }

    bad = SV_Malloc(sizeof(*bad) + pathlen);
    memcpy(bad->string, path, pathlen + 1);
    bad->next = cl->ac_bad_files;
    cl->ac_bad_files = bad;
}

static void AC_ParseReady(void)
{
    ac.ready = true;
    ac.last_ping = svs.realtime;
    acs.retry_backoff = AC_DEFAULT_BACKOFF;
    Com_Printf("ANTICHEAT: Ready to serve anticheat clients.\n");
    Cvar_FullSet("anticheat", ac_required->string,
                 CVAR_SERVERINFO | CVAR_ROM, FROM_CODE);
}

static void AC_ParseQueryReply(void)
{
    client_t        *cl;
    int             type, valid;

    cl = AC_ParseClient();
    if (!cl) {
        return;
    }

    if (msg_read.readcount + 2 > msg_read.cursize) {
        Com_DPrintf("ANTICHEAT: Message too short in %s\n", __func__);
        return;
    }

    valid = MSG_ReadByte();
    type = MSG_ReadByte();

    cl->ac_query_sent = AC_QUERY_DONE;
    if (valid == 1) {
        cl->ac_client_type = type;
        cl->ac_valid = true;
    }

    if (cl->state < cs_connected || cl->state > cs_primed) {
        Com_WPrintf("ANTICHEAT: %s with client in state %d\n",
                    __func__, cl->state);
        SV_DropClient(cl, NULL);
        return;
    }

    Com_DPrintf("ANTICHEAT: %s for %s\n", __func__, cl->name);

    // SV_Begin_f will handle possible map change
    sv_client = cl;
    sv_player = cl->edict;
    SV_Begin_f();
    sv_client = NULL;
    sv_player = NULL;
}

// this is different from the violation "disconnected" as this message is
// only sent if the client manually disconnected and exists to prevent the
// race condition of the server seeing the disconnect violation before the
// udp message and thus showing "%s lost connection" right before the
// player leaves the server
static void AC_ParseDisconnect(void)
{
    client_t        *cl;

    cl = AC_ParseClient();
    if (cl) {
        Com_Printf("ANTICHEAT: Dropping %s, disconnect message.\n", cl->name);
        SV_DropClient(cl, NULL);
    }
}

static void AC_ParseError(void)
{
    char string[MAX_STRING_CHARS];

    MSG_ReadString(string, sizeof(string));
    Com_EPrintf("ANTICHEAT: %s\n", string);
    AC_Disable();
}

static bool AC_ParseMessage(void)
{
    uint16_t msglen;
    int cmd;

    // parse msglen
    if (!ac.msglen) {
        if (!FIFO_TryRead(&ac.stream.recv, &msglen, 2)) {
            return false;
        }
        if (!msglen) {
            return true;
        }
        msglen = LittleShort(msglen);
        if (msglen > AC_RECV_SIZE) {
            Com_EPrintf("ANTICHEAT: Oversize message: %u bytes\n", msglen);
            AC_Drop();
            return false;
        }
        ac.msglen = msglen;
    }

    // read this message
    if (!FIFO_ReadMessage(&ac.stream.recv, ac.msglen)) {
        return false;
    }

    ac.msglen = 0;

    cmd = MSG_ReadByte();
    switch (cmd) {
    case ACS_VIOLATION:
        AC_ParseViolation();
        break;
    case ACS_CLIENTACK:
        AC_ParseClientAck();
        break;
    case ACS_FILE_VIOLATION:
        AC_ParseFileViolation();
        break;
    case ACS_READY:
        AC_ParseReady();
        break;
    case ACS_QUERYREPLY:
        AC_ParseQueryReply();
        break;
    case ACS_ERROR:
        AC_ParseError();
        return false;
    case ACS_NOACCESS:
        Com_WPrintf("ANTICHEAT: You do not have permission to "
                    "use the anticheat server. Anticheat disabled.\n");
        AC_Disable();
        return false;
    case ACS_UPDATE_REQUIRED:
        Com_WPrintf("ANTICHEAT: The anticheat server is no longer "
                    "compatible with this version of " APPLICATION ". "
                    "Please make sure you are using the latest " APPLICATION " version. "
                    "Anticheat disabled.\n");
        AC_Disable();
        return false;
    case ACS_DISCONNECT:
        AC_ParseDisconnect();
        return false;
    case ACS_PONG:
        ac.ping_pending = false;
        ac.last_ping = svs.realtime;
        break;
    default:
        Com_EPrintf("ANTICHEAT: Unknown command byte %d, please make "
                    "sure you are using the latest " APPLICATION " version. "
                    "Anticheat disabled.\n", cmd);
        AC_Disable();
        return false;
    }

    if (msg_read.readcount > msg_read.cursize) {
        Com_WPrintf("ANTICHEAT: Read %zu bytes past end of message %d\n",
                    msg_read.readcount - msg_read.cursize, cmd);
    }

    return true;
}

/*
==============================================================================

IN-GAME QUERIES

==============================================================================
*/

static void AC_Write(const char *func)
{
    byte *src = msg_write.data;
    size_t len = msg_write.cursize;

    SZ_Clear(&msg_write);

    if (!FIFO_TryWrite(&ac.stream.send, src, len)) {
        Com_WPrintf("ANTICHEAT: Send buffer exceeded in %s\n", func);
        return;
    }

    NET_UpdateStream(&ac.stream);
}

static void AC_ClientQuery(client_t *cl)
{
    cl->ac_query_sent = AC_QUERY_SENT;
    cl->ac_query_time = svs.realtime;

    if (!ac.ready)
        return;

    //if (ac_nag_time->integer)
    //    cl->anticheat_nag_time = svs.realtime;

    MSG_WriteShort(9);
    MSG_WriteByte(ACC_QUERYCLIENT);
    MSG_WriteLong(cl->number);
    MSG_WriteLong(cl->challenge);
    AC_Write(__func__);
}

bool AC_ClientBegin(client_t *cl)
{
    if (!ac_required->integer) {
        return true; // anticheat is not in use
    }

    if (cl->ac_required == AC_EXEMPT) {
        return true; // client is EXEMPT
    }

    if (cl->ac_valid) {
        return true; // client is VALID
    }

    if (cl->ac_query_sent == AC_QUERY_UNSENT && ac.ready) {
        AC_ClientQuery(cl);
        return false; // not yet QUERIED
    }

    if (cl->ac_required != AC_REQUIRED) {
        return true; // anticheat is NOT REQUIRED
    }

    if (ac.ready) {
        // anticheat connection is UP, client is STILL INVALID
        // AFTER QUERY, anticheat is REQUIRED
        Com_Printf("ANTICHEAT: Rejected connecting client %s[%s], "
                   "no anticheat response.\n", cl->name,
                   NET_AdrToString(&cl->netchan.remote_address));
        SV_ClientPrintf(cl, PRINT_HIGH, "%s\n", ac_message->string);
        SV_DropClient(cl, NULL);
        return false;
    }

    if (ac_error_action->integer == 0) {
        return true; // error action is ALLOW
    }

    // anticheat server connection is DOWN, client is INVALID,
    // anticheat is REQUIRED, error action is DENY
    Com_Printf("ANTICHEAT: Rejected connecting client %s[%s], "
               "no connection to anticheat server.\n", cl->name,
               NET_AdrToString(&cl->netchan.remote_address));
    SV_ClientPrintf(cl, PRINT_HIGH,
                    "This server is unable to take new connections right now. "
                    "Please try again later.\n");
    SV_DropClient(cl, NULL);
    return false;
}

void AC_ClientAnnounce(client_t *cl)
{
    if (!ac_required->integer) {
        return; // anticheat is not in use
    }
    if (cl->state <= cs_zombie) {
        return;
    }
    if (cl->ac_required == AC_EXEMPT) {
        SV_BroadcastPrintf(PRINT_MEDIUM, AC_MESSAGE
                           "%s is exempt from using anticheat.\n", cl->name);
    } else if (cl->ac_valid) {
        if (cl->ac_file_failures) {
            SV_BroadcastPrintf(PRINT_MEDIUM, AC_MESSAGE
                               "%s failed %d file check%s.\n",
                               cl->name, cl->ac_file_failures,
                               cl->ac_file_failures == 1 ? "" : "s");
        }
    } else {
        SV_BroadcastPrintf(PRINT_MEDIUM, AC_MESSAGE
                           "%s is not using anticheat.\n", cl->name);
    }
}

char *AC_ClientConnect(client_t *cl)
{
    if (!ac_required->integer) {
        return ""; // anticheat is not in use
    }

    if (SV_MatchAddress(&ac_exempt_list, &net_from)) {
        cl->ac_required = AC_EXEMPT;
        return "";
    }

    if (ac_required->integer == 2) {
        // anticheat is required for everyone
        cl->ac_required = AC_REQUIRED;
    } else {
        cl->ac_required = AC_NORMAL;
        if (SV_MatchAddress(&ac_required_list, &net_from)) {
            cl->ac_required = AC_REQUIRED;
        }
    }

    if (ac.ready && net_from.type == NA_IP) {
        MSG_WriteShort(15);
        MSG_WriteByte(ACC_REQUESTCHALLENGE);
        MSG_WriteData(net_from.ip.u8, 4);
        MSG_WriteData(&net_from.port, 2);
        MSG_WriteLong(cl->number);
        MSG_WriteLong(cl->challenge);
        AC_Write(__func__);
    }

    return " ac=1";
}

void AC_ClientDisconnect(client_t *cl)
{
    cl->ac_query_sent = AC_QUERY_UNSENT;
    cl->ac_valid = false;

    if (!ac.ready)
        return;

    MSG_WriteShort(9);
    MSG_WriteByte(ACC_CLIENTDISCONNECT);
    MSG_WriteLong(cl->number);
    MSG_WriteLong(cl->challenge);
    AC_Write(__func__);
}

void AC_ClientToken(client_t *cl, const char *token)
{
    string_entry_t *tok;
    client_t *other;

    if (!ac_required->integer) {
        return; // anticheat is not in use
    }

    for (tok = acs.tokens; tok; tok = tok->next) {
        if (!strcmp(tok->string, token)) {
            break;
        }
    }

    if (!tok) {
        return;
    }

    FOR_EACH_CLIENT(other) {
        // FIXME: after `svacupdate' this check is incorrect
        if (other->ac_token == tok->string) {
            SV_DropClient(other, "duplicate anticheat token");
        }
    }

    Com_Printf(
        "ANTICHEAT: %s bypassed anticheat requirements with token '%s'\n",
        cl->name, tok->string);
    cl->ac_token = tok->string;
    cl->ac_required = AC_EXEMPT;
}

/*
==============================================================================

STARTUP STUFF

==============================================================================
*/

static void AC_Spin(void)
{
    // sleep on AC server socket
    NET_Sleep1(100, ac.stream.socket);
    Sys_RunConsole();
    AC_Run();
}

static bool AC_Flush(void)
{
    byte *src = msg_write.data;
    size_t ret, len = msg_write.cursize;

    SZ_Clear(&msg_write);

    if (!ac.connected) {
        return false;
    }

    while (1) {
        ret = FIFO_Write(&ac.stream.send, src, len);
        NET_UpdateStream(&ac.stream);

        if (ret == len) {
            break;
        }

        len -= ret;
        src += ret;

        Com_WPrintf("ANTICHEAT: Send buffer length exceeded, "
                    "server may be frozen for a short while!\n");
        do {
            AC_Spin();
            if (!ac.connected) {
                return false;
            }
        } while (FIFO_Usage(&ac.stream.send) > AC_SEND_SIZE / 2);
    }

    return true;
}

static void AC_WriteString(const char *s)
{
    size_t len = strlen(s);

    if (len > 255) {
        len = 255;
    }

    MSG_WriteByte(len);
    MSG_WriteData(s, len);
}

static void AC_SendChecks(void)
{
    ac_file_t *f, *p;
    ac_cvar_t *c;
    int i;

    MSG_WriteShort(9);
    MSG_WriteByte(ACC_UPDATECHECKS);
    MSG_WriteLong(acs.num_files);
    MSG_WriteLong(acs.num_cvars);
    AC_Flush();

    for (f = acs.files, p = NULL; f; p = f, f = f->next) {
        MSG_WriteData(f->hash, sizeof(f->hash));
        MSG_WriteByte(f->flags);
        if (p && !strcmp(f->path, p->path)) {
            MSG_WriteByte(0);
        } else {
            AC_WriteString(f->path);
        }
        AC_Flush();
    }

    for (c = acs.cvars; c; c = c->next) {
        AC_WriteString(c->name);
        MSG_WriteByte(c->op);
        MSG_WriteByte(c->num_values);
        for (i = 0; i < c->num_values; i++) {
            AC_WriteString(c->values[i]);
        }
        AC_WriteString(c->def);
        AC_Flush();
    }
}

static void AC_SendPrefs(void)
{
    int prefs = 0;

    if (ac_disable_play->integer) {
        prefs |= ACP_BLOCKPLAY;
    }

    MSG_WriteShort(5);
    MSG_WriteByte(ACC_SETPREFERENCES);
    MSG_WriteLong(prefs);
    AC_Flush();
}

static void AC_SendPing(void)
{
    ac.last_ping = svs.realtime;
    ac.ping_pending = true;

    MSG_WriteShort(1);
    MSG_WriteByte(ACC_PING);
    AC_Flush();
}

static void AC_SendHello(void)
{
    size_t hostlen = strlen(sv_hostname->string);
    size_t verlen = strlen(com_version->string);

    MSG_WriteByte(0x02);
    MSG_WriteShort(22 + hostlen + verlen);   // why 22 instead of 9?
    MSG_WriteByte(ACC_VERSION);
    MSG_WriteShort(AC_PROTOCOL_VERSION);
    MSG_WriteShort(hostlen);
    MSG_WriteData(sv_hostname->string, hostlen);
    MSG_WriteShort(verlen);
    MSG_WriteData(com_version->string, verlen);
    MSG_WriteLong(net_port->integer);
    AC_Flush();

    AC_SendChecks();
    AC_SendPrefs();
    AC_SendPing();
}

static void AC_CheckTimeouts(void)
{
    client_t *cl;

    if (ac.ping_pending) {
        if (svs.realtime - ac.last_ping > AC_PING_TIMEOUT) {
            Com_Printf("ANTICHEAT: Server ping timeout, disconnecting.\n");
            AC_Drop();
            return;
        }
    } else if (ac.ready) {
        if (svs.realtime - ac.last_ping > AC_PING_INTERVAL) {
            AC_SendPing();
        }
    }

    FOR_EACH_CLIENT(cl) {
        if (cl->state < cs_connected || cl->state > cs_primed) {
            continue;
        }
        if (cl->ac_query_sent != AC_QUERY_SENT) {
            continue;
        }
        if (svs.realtime - cl->ac_query_time > 5000) {
            Com_WPrintf("ANTICHEAT: Query timed out for %s, possible network problem.\n", cl->name);
            cl->ac_valid = false;
            sv_client = cl;
            sv_player = cl->edict;
            SV_Begin_f();
            sv_client = NULL;
            sv_player = NULL;
        }
    }
}

static bool AC_Reconnect(void)
{
    netadr_t address;

    if (!NET_StringToAdr(ac_server_address->string, &address, PORT_SERVER)) {
        Com_WPrintf("ANTICHEAT: Unable to lookup %s.\n",
                    ac_server_address->string);
        goto fail;
    }

    if (NET_Connect(&address, &ac.stream)) {
        Com_EPrintf("ANTICHEAT: Unable to connect to %s.\n",
                    NET_AdrToString(&address));
        goto fail;
    }

    ac.stream.send.data = ac_send_buffer;
    ac.stream.send.size = AC_SEND_SIZE;
    ac.stream.recv.data = ac_recv_buffer;
    ac.stream.recv.size = AC_RECV_SIZE;
    acs.retry_time = 0;
    return true;

fail:
    acs.retry_backoff += 60;
    AC_Retry();
    return false;
}


void AC_Run(void)
{
    neterr_t ret = NET_AGAIN;
    time_t clock;

    if (acs.retry_time) {
        clock = time(NULL);
        if (acs.retry_time < clock) {
            Com_Printf("ANTICHEAT: Attempting to reconnect to anticheat server...\n");
            AC_Reconnect();
        }
        return;
    }

    if (!ac.stream.state) {
        return;
    }

    if (!ac.connected) {
        ret = NET_RunConnect(&ac.stream);
        if (ret == NET_OK) {
            Com_Printf("ANTICHEAT: Connected to anticheat server!\n");
            ac.connected = true;
            AC_SendHello();
        }
    }

    if (ac.connected) {
        ret = NET_RunStream(&ac.stream);
        if (ret == NET_OK) {
            while (AC_ParseMessage())
                ;
            NET_UpdateStream(&ac.stream);
        }
        AC_CheckTimeouts();
    }

    switch (ret) {
    case NET_ERROR:
        Com_EPrintf("ANTICHEAT: %s to %s.\n", NET_ErrorString(),
                    NET_AdrToString(&ac.stream.address));
        // fall through
    case NET_CLOSED:
        AC_Drop();
        break;
    default:
        break;
    }
}

void AC_Connect(unsigned mvd_spawn)
{
    int attempts;

    if (!ac_required->integer) {
        return;
    }

#if USE_CLIENT
    if (!dedicated->integer) {
        Com_Printf("ANTICHEAT: Only supported on dedicated servers, disabling.\n");
        Cvar_SetByVar(ac_required, "0", FROM_CODE);
        return;
    }
#endif
    if (mvd_spawn) {
        Com_Printf("ANTICHEAT: Only supported on game servers, disabling.\n");
        Cvar_SetByVar(ac_required, "0", FROM_CODE);
        return;
    }

    AC_LoadChecks();

    Com_Printf("ANTICHEAT: Attempting to connect to %s...\n", ac_server_address->string);
    Sys_RunConsole();

    acs.retry_backoff = AC_DEFAULT_BACKOFF;
    if (!AC_Reconnect()) {
        return;
    }

    // synchronize startup
    for (attempts = 0; attempts < 50; attempts++) {
        AC_Spin();
        if (ac.ready || !ac.stream.state) {
            return;
        }
    }

    Com_WPrintf("ANTICHEAT: Still not ready, resuming server initialization.\n");
}

void AC_Disconnect(void)
{
    NET_CloseStream(&ac.stream);

    AC_FreeChecks();

    memset(&ac, 0, sizeof(ac));
    memset(&acs, 0, sizeof(acs));
    Cvar_FullSet("anticheat", "0", CVAR_ROM, FROM_CODE);
}

void AC_List_f(void)
{
    client_t    *cl;
    char        *sub;
    int         i;

    if (!svs.initialized) {
        Com_Printf("No server running.\n");
        return;
    }

    if (!ac_required->integer) {
        Com_Printf("The anticheat module is not in use on this server.\n"
                   "For information on anticheat, please visit http://antiche.at/\n");
        return;
    }

    sub = Cmd_Argv(1);

    Com_Printf(
        "+----------------+--------+-----+------+\n"
        "|  Player Name   |AC Valid|Files|Client|\n"
        "+----------------+--------+-----+------+\n");

    FOR_EACH_CLIENT(cl) {
        if (cl->state < cs_spawned) {
            continue;
        }

        if (*sub && !strstr(cl->name, sub)) {
            continue;
        }

        if (cl->ac_required == AC_EXEMPT) {
            Com_Printf("|%-16s| exempt | N/A | N/A  |\n", cl->name);
        } else if (cl->ac_valid) {
            i = cl->ac_client_type;
            if (i < 0 || i >= ac_num_clients) {
                i = 0;
            }
            Com_Printf("|%-16s|   yes  | %3d |%-6s|\n", cl->name,
                       cl->ac_file_failures, ac_clients[i]);
        } else {
            Com_Printf("|%-16s|   NO   | N/A | N/A  |\n", cl->name);
        }
    }

    Com_Printf("+----------------+--------+-----+------+\n");

    if (ac.ready) {
        Com_Printf("File check list in use: %s\n", acs.hashlist_name);
    }

    Com_Printf(
        "This Quake II server is %sconnected to the anticheat server.\n"
        "For information on anticheat, please visit http://antiche.at/\n",
        ac.ready ? "" : "NOT ");
}

void AC_Info_f(void)
{
    client_t *cl;
    string_entry_t *bad;
    char *substring, *filesubstring;
    int clientID;

    if (!svs.initialized) {
        Com_Printf("No server running.\n");
        return;
    }

    if (!ac_required->integer) {
        Com_Printf("The anticheat module is not in use on this server.\n"
                   "For information on anticheat, please visit http://antiche.at/\n");
        return;
    }

    if (Cmd_Argc() == 1) {
        if (!sv_client) {
            Com_Printf("Usage: %s [substring|id] [filesubstring]\n", Cmd_Argv(0));
            return;
        }
        cl = sv_client;
        filesubstring = "";
    } else {
        substring = Cmd_Argv(1);
        filesubstring = Cmd_Argv(2);

        if (COM_IsUint(substring)) {
            clientID = Q_atoi(substring);
            if (clientID < 0 || clientID >= sv_maxclients->integer) {
                Com_Printf("Invalid client ID.\n");
                return;
            }
            cl = &svs.client_pool[clientID];
            if (cl->state < cs_spawned) {
                Com_Printf("Player is not active.\n");
                return;
            }
        } else {
            FOR_EACH_CLIENT(cl) {
                if (cl->state < cs_spawned) {
                    continue;
                }
                if (strstr(cl->name, substring)) {
                    goto found;
                }
            }
            Com_Printf("Player not found.\n");
            return;
        }
    }

found:
    if (!cl->ac_valid) {
        Com_Printf("%s is not using anticheat.\n", cl->name);
        return;
    }

    if (cl->ac_bad_files) {
        Com_Printf("File check failures for %s:\n", cl->name);
        for (bad = cl->ac_bad_files; bad; bad = bad->next) {
            if (!filesubstring[0] || strstr(bad->string, filesubstring)) {
                Com_Printf("%s\n", bad->string);
            }
        }
    } else {
        Com_Printf("%s has no file check failures.\n", cl->name);
    }
}

static void AC_Invalidate_f(void)
{
    client_t    *cl;

    if (!svs.initialized) {
        Com_Printf("No server running.\n");
        return;
    }
    if (!ac.ready) {
        Com_Printf("Anticheat is not ready.\n");
        return;
    }

    FOR_EACH_CLIENT(cl) {
        if (cl->state > cs_connected) {
            AC_ClientDisconnect(cl);
        }
    }

    Com_Printf("All clients marked as invalid.\n");
}

static void AC_Update_f(void)
{
    client_t    *cl;

    if (!svs.initialized) {
        Com_Printf("No server running.\n");
        return;
    }
    if (!ac_required->integer) {
        Com_Printf("Anticheat is not in use.\n");
        return;
    }

    AC_FreeChecks();
    AC_LoadChecks();

    if (ac.connected) {
        AC_SendChecks();
    }

    // reset all tokens
    FOR_EACH_CLIENT(cl) {
        cl->ac_token = NULL;
    }

    Com_Printf("Anticheat configuration updated.\n");
}

static void AC_AddException_f(void)
{
    SV_AddMatch_f(&ac_exempt_list);
}
static void AC_DelException_f(void)
{
    SV_DelMatch_f(&ac_exempt_list);
}
static void AC_ListExceptions_f(void)
{
    SV_ListMatches_f(&ac_exempt_list);
}

static void AC_AddRequirement_f(void)
{
    SV_AddMatch_f(&ac_required_list);
}
static void AC_DelRequirement_f(void)
{
    SV_DelMatch_f(&ac_required_list);
}
static void AC_ListRequirements_f(void)
{
    SV_ListMatches_f(&ac_required_list);
}

static const cmdreg_t c_ac[] = {
    { "svaclist", AC_List_f },
    { "svacinfo", AC_Info_f },
    { "svacupdate", AC_Update_f },
    { "svacinvalidate", AC_Invalidate_f },

    { "addacexception", AC_AddException_f },
    { "delacexception", AC_DelException_f },
    { "listacexceptions", AC_ListExceptions_f },

    { "addacrequirement", AC_AddRequirement_f },
    { "delacrequirement", AC_DelRequirement_f },
    { "listacrequirements", AC_ListRequirements_f },

    { NULL }
};

static void ac_disable_play_changed(cvar_t *self)
{
    if (ac.connected) {
        AC_SendPrefs();
    }
}

void AC_Register(void)
{
    ac_required = Cvar_Get("sv_anticheat_required", "0", CVAR_LATCH);
    ac_server_address = Cvar_Get("sv_anticheat_server_address", "anticheat.r1ch.net", CVAR_LATCH);
    ac_error_action = Cvar_Get("sv_anticheat_error_action", "0", 0);
    ac_message = Cvar_Get("sv_anticheat_message",
                          "This server requires the r1ch.net anticheat module. "
                          "Please see http://antiche.at/ for more details.", 0);
    ac_badfile_action = Cvar_Get("sv_anticheat_badfile_action", "0", 0);
    ac_badfile_message = Cvar_Get("sv_anticheat_badfile_message", "", 0);
    ac_badfile_max = Cvar_Get("sv_anticheat_badfile_max", "0", 0);
    ac_show_violation_reason = Cvar_Get("sv_anticheat_show_violation_reason", "0", 0);
    ac_client_disconnect_action = Cvar_Get("sv_anticheat_client_disconnect_action", "0", 0);
    ac_disable_play = Cvar_Get("sv_anticheat_disable_play", "0", 0);
    ac_disable_play->changed = ac_disable_play_changed;

    Cmd_Register(c_ac);
}


