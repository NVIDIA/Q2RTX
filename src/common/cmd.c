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
// cmd.c -- Quake script command processing module

#include "shared/shared.h"
#include "shared/list.h"
#include "common/cmd.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/error.h"
#include "common/files.h"
#include "common/prompt.h"
#include "common/utils.h"
#include "client/client.h"

#define Cmd_Malloc(size)        Z_TagMalloc(size, TAG_CMD)
#define Cmd_CopyString(string)  Z_TagCopyString(string, TAG_CMD)

static char *Cmd_ArgsRange(int from, int to);

/*
=============================================================================

                        COMMAND BUFFER

=============================================================================
*/

char            cmd_buffer_text[CMD_BUFFER_SIZE];
cmdbuf_t        cmd_buffer;

// points to the buffer current command is executed from
cmdbuf_t        *cmd_current;

/*
============
Cmd_Wait_f

Causes execution of the remainder of the command buffer to be delayed until
next frame.  This allows commands like:
bind g "impulse 5 ; +attack ; wait ; -attack ; impulse 2"
============
*/
static void Cmd_Wait_f(void)
{
    int count;

    count = atoi(Cmd_Argv(1));
    if (count < 1) {
        count = 1;
    }
    cmd_current->waitCount += count;
}

/*
============
Cbuf_Init
============
*/
void Cbuf_Init(void)
{
    memset(&cmd_buffer, 0, sizeof(cmd_buffer));
    cmd_buffer.from = FROM_CONSOLE;
    cmd_buffer.text = cmd_buffer_text;
    cmd_buffer.maxsize = sizeof(cmd_buffer_text);
    cmd_buffer.exec = Cmd_ExecuteString;
}

/*
============
Cbuf_AddText

Adds command text at the end of the buffer
============
*/
void Cbuf_AddText(cmdbuf_t *buf, const char *text)
{
    size_t l = strlen(text);

    if (buf->cursize + l > buf->maxsize) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }
    memcpy(buf->text + buf->cursize, text, l);
    buf->cursize += l;
}

char *Cbuf_Alloc(cmdbuf_t *buf, size_t len)
{
    char *text;

    if (buf->cursize + len > buf->maxsize) {
        return NULL;
    }
    text = buf->text + buf->cursize;
    buf->cursize += len;

    return text;
}

/*
============
Cbuf_InsertText

Adds command text at the beginning of command buffer.
Adds a \n to the text.
============
*/
void Cbuf_InsertText(cmdbuf_t *buf, const char *text)
{
    size_t l = strlen(text);

// add the entire text of the file
    if (!l) {
        return;
    }
    if (buf->cursize + l + 1 > buf->maxsize) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    memmove(buf->text + l + 1, buf->text, buf->cursize);
    memcpy(buf->text, text, l);
    buf->text[l] = '\n';
    buf->cursize += l + 1;
}

/*
============
Cbuf_Execute
============
*/
void Cbuf_Execute(cmdbuf_t *buf)
{
    int     i;
    char    *text;
    char    line[MAX_STRING_CHARS];
    int     quotes;

    while (buf->cursize) {
        if (buf->waitCount > 0) {
            // skip out while text still remains in buffer, leaving it
            // for next frame (counter is decremented externally now)
            return;
        }

// find a \n or ; line break
        text = buf->text;

        quotes = 0;
        for (i = 0; i < buf->cursize; i++) {
            if (text[i] == '"')
                quotes++;
            if (!(quotes & 1) && text[i] == ';')
                break;    // don't break if inside a quoted string
            if (text[i] == '\n')
                break;
        }

        // check for overflow
        if (i > sizeof(line) - 1) {
            i = sizeof(line) - 1;
        }

        memcpy(line, text, i);
        line[i] = 0;

// delete the text from the command buffer and move remaining commands down
// this is necessary because commands (exec, alias) can insert data at the
// beginning of the text buffer
        if (i == buf->cursize) {
            buf->cursize = 0;
        } else {
            i++;
            buf->cursize -= i;
            memmove(text, text + i, buf->cursize);
        }

// execute the command line
        cmd_current = buf;
        buf->exec(buf, line);
    }

    buf->aliasCount = 0;        // don't allow infinite alias loops
}

/*
==============================================================================

                        SCRIPT COMMANDS

==============================================================================
*/

#define ALIAS_HASH_SIZE    64

#define FOR_EACH_ALIAS_HASH(alias, hash) \
    LIST_FOR_EACH(cmdalias_t, alias, &cmd_aliasHash[hash], hashEntry)
#define FOR_EACH_ALIAS(alias) \
    LIST_FOR_EACH(cmdalias_t, alias, &cmd_alias, listEntry)

typedef struct cmdalias_s {
    list_t  hashEntry;
    list_t  listEntry;
    char    *value;
    char    name[1];
} cmdalias_t;

static list_t   cmd_alias;
static list_t   cmd_aliasHash[ALIAS_HASH_SIZE];

/*
===============
Cmd_AliasFind
===============
*/
static cmdalias_t *Cmd_AliasFind(const char *name)
{
    unsigned hash;
    cmdalias_t *alias;

    hash = Com_HashString(name, ALIAS_HASH_SIZE);
    FOR_EACH_ALIAS_HASH(alias, hash) {
        if (!strcmp(name, alias->name)) {
            return alias;
        }
    }

    return NULL;
}

char *Cmd_AliasCommand(const char *name)
{
    cmdalias_t *a;

    a = Cmd_AliasFind(name);
    if (!a) {
        return NULL;
    }

    return a->value;
}

void Cmd_AliasSet(const char *name, const char *cmd)
{
    cmdalias_t  *a;
    unsigned    hash;
    size_t      len;

    // if the alias already exists, reuse it
    a = Cmd_AliasFind(name);
    if (a) {
        Z_Free(a->value);
        a->value = Cmd_CopyString(cmd);
        return;
    }

    len = strlen(name);
    a = Cmd_Malloc(sizeof(cmdalias_t) + len);
    memcpy(a->name, name, len + 1);
    a->value = Cmd_CopyString(cmd);

    List_Append(&cmd_alias, &a->listEntry);

    hash = Com_HashString(name, ALIAS_HASH_SIZE);
    List_Append(&cmd_aliasHash[hash], &a->hashEntry);
}

void Cmd_Alias_g(genctx_t *ctx)
{
    cmdalias_t *a;

    FOR_EACH_ALIAS(a) {
        if (!Prompt_AddMatch(ctx, a->name)) {
            break;
        }
    }
}


/*
===============
Cmd_Alias_f

Creates a new command that executes a command string (possibly ; seperated)
===============
*/
void Cmd_Alias_f(void)
{
    cmdalias_t  *a;
    char        *s, *cmd;

    if (Cmd_Argc() < 2) {
        if (LIST_EMPTY(&cmd_alias)) {
            Com_Printf("No alias commands registered.\n");
            return;
        }
        Com_Printf("Registered alias commands:\n");
        FOR_EACH_ALIAS(a) {
            Com_Printf("\"%s\" = \"%s\"\n", a->name, a->value);
        }
        return;
    }

    s = Cmd_Argv(1);
    if (Cmd_Exists(s)) {
        Com_Printf("\"%s\" already defined as a command\n", s);
        return;
    }

    if (Cvar_Exists(s, qtrue)) {
        Com_Printf("\"%s\" already defined as a cvar\n", s);
        return;
    }

    if (Cmd_Argc() < 3) {
        a = Cmd_AliasFind(s);
        if (a) {
            Com_Printf("\"%s\" = \"%s\"\n", a->name, a->value);
        } else {
            Com_Printf("\"%s\" is undefined\n", s);
        }
        return;
    }

    // copy the rest of the command line
    cmd = Cmd_ArgsFrom(2);
    Cmd_AliasSet(s, cmd);
}

static void Cmd_UnAlias_f(void)
{
    static const cmd_option_t options[] = {
        { "h", "help", "display this message" },
        { "a", "all", "delete everything" },
        { 0 }
    };
    char *s;
    cmdalias_t *a, *n;
    unsigned hash;
    int c;

    while ((c = Cmd_ParseOptions(options)) != -1) {
        switch (c) {
        case 'h':
            Com_Printf("Usage: %s [-ha] [name]\n", Cmd_Argv(0));
            Cmd_PrintHelp(options);
            return;
        case 'a':
            LIST_FOR_EACH_SAFE(cmdalias_t, a, n, &cmd_alias, listEntry) {
                Z_Free(a->value);
                Z_Free(a);
            }
            for (hash = 0; hash < ALIAS_HASH_SIZE; hash++) {
                List_Init(&cmd_aliasHash[hash]);
            }
            List_Init(&cmd_alias);
            Com_Printf("Removed all alias commands.\n");
            return;
        default:
            return;
        }
    }

    if (!cmd_optarg[0]) {
        Com_Printf("Missing alias name.\n"
                   "Try %s --help for more information.\n",
                   Cmd_Argv(0));
        return;
    }

    s = Cmd_Argv(1);
    a = Cmd_AliasFind(s);
    if (!a) {
        Com_Printf("\"%s\" is undefined.\n", s);
        return;
    }

    List_Delete(&a->listEntry);
    List_Delete(&a->hashEntry);

    Z_Free(a->value);
    Z_Free(a);
}

#if USE_CLIENT
void Cmd_WriteAliases(qhandle_t f)
{
    cmdalias_t *a;

    FOR_EACH_ALIAS(a) {
        FS_FPrintf(f, "alias \"%s\" \"%s\"\n", a->name, a->value);
    }
}
#endif

static void Cmd_Alias_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        Cmd_Alias_g(ctx);
    } else {
        Com_Generic_c(ctx, argnum - 2);
    }
}

static void Cmd_UnAlias_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        Cmd_Alias_g(ctx);
    }
}

/*
=============================================================================

MESSAGE TRIGGERS

=============================================================================
*/

#define FOR_EACH_TRIGGER(trig) \
    LIST_FOR_EACH(cmd_trigger_t, trig, &cmd_triggers, entry)
#define FOR_EACH_TRIGGER_SAFE(trig, next) \
    LIST_FOR_EACH_SAFE(cmd_trigger_t, trig, next, &cmd_triggers, entry)

typedef struct {
    list_t  entry;
    char    *match;
    char    *command;
} cmd_trigger_t;

static list_t    cmd_triggers;

static cmd_trigger_t *find_trigger(const char *command, const char *match)
{
    cmd_trigger_t *trigger;

    FOR_EACH_TRIGGER(trigger) {
        if (!strcmp(trigger->command, command) && !strcmp(trigger->match, match)) {
            return trigger;
        }
    }

    return NULL;
}

static void list_triggers(void)
{
    cmd_trigger_t *trigger;

    if (LIST_EMPTY(&cmd_triggers)) {
        Com_Printf("No current message triggers\n");
        return;
    }

    Com_Printf("Current message triggers:\n");
    FOR_EACH_TRIGGER(trigger) {
        Com_Printf("\"%s\" = \"%s\"\n", trigger->command, trigger->match);
    }
}

/*
============
Cmd_Trigger_f
============
*/
static void Cmd_Trigger_f(void)
{
    cmd_trigger_t *trigger;
    const char *command, *match;
    size_t cmdlen, matchlen;

    if (Cmd_Argc() == 1) {
        list_triggers();
        return;
    }

    if (Cmd_Argc() < 3) {
        Com_Printf("Usage: %s <command> <match>\n", Cmd_Argv(0));
        return;
    }

    command = Cmd_Argv(1);
    match = Cmd_ArgsFrom(2);

    // don't create the same trigger twice
    if (find_trigger(command, match)) {
        return;
    }

    cmdlen = strlen(command) + 1;
    matchlen = strlen(match) + 1;
    if (matchlen < 4) {
        Com_Printf("Match string is too short\n");
        return;
    }

    trigger = Z_Malloc(sizeof(*trigger) + cmdlen + matchlen);
    trigger->command = (char *)(trigger + 1);
    trigger->match = trigger->command + cmdlen;
    memcpy(trigger->command, command, cmdlen);
    memcpy(trigger->match, match, matchlen);
    List_Append(&cmd_triggers, &trigger->entry);
}

static void Cmd_UnTrigger_f(void)
{
    cmd_trigger_t *trigger, *next;
    const char *command, *match;

    if (Cmd_Argc() == 1) {
        list_triggers();
        return;
    }

    if (LIST_EMPTY(&cmd_triggers)) {
        Com_Printf("No current message triggers\n");
        return;
    }

    if (Cmd_Argc() == 2) {
        if (!Q_stricmp(Cmd_Argv(1), "all")) {
            int count = 0;

            FOR_EACH_TRIGGER_SAFE(trigger, next) {
                Z_Free(trigger);
                count++;
            }

            Com_Printf("Removed %d trigger%s\n", count, count == 1 ? "" : "s");
            List_Init(&cmd_triggers);
            return;
        }

        Com_Printf("Usage: %s <command> <match>\n", Cmd_Argv(0));
        return;
    }

    command = Cmd_Argv(1);
    match = Cmd_ArgsFrom(2);

    trigger = find_trigger(command, match);
    if (!trigger) {
        Com_Printf("Can't find trigger \"%s\" = \"%s\"\n", command, match);
        return;
    }

    List_Remove(&trigger->entry);
    Z_Free(trigger);
}

/*
============
Cmd_ExecTrigger
============
*/
void Cmd_ExecTrigger(const char *string)
{
    cmd_trigger_t *trigger;
    char *match;

    // execute matching triggers
    FOR_EACH_TRIGGER(trigger) {
        match = Cmd_MacroExpandString(trigger->match, qfalse);
        if (match && Com_WildCmp(match, string)) {
            Cbuf_AddText(&cmd_buffer, trigger->command);
            Cbuf_AddText(&cmd_buffer, "\n");
        }
    }
}

/*
=============================================================================

                    BRANCHING

=============================================================================
*/

/*
============
Cmd_If_f
============
*/
static void Cmd_If_f(void)
{
    char *a, *b, *op;
    qboolean numeric;
    qboolean matched;
    int i, j;

    if (Cmd_Argc() < 5) {
        Com_Printf("Usage: if <expr> <op> <expr> [then] <command> [else <command>]\n");
        return;
    }

    a = Cmd_Argv(1);
    op = Cmd_Argv(2);
    b = Cmd_Argv(3);

    numeric = COM_IsFloat(a) && COM_IsFloat(b);
    if (!strcmp(op, "==")) {
        matched = numeric ? atof(a) == atof(b) : !strcmp(a, b);
    } else if (!strcmp(op, "!=") || !strcmp(op, "<>")) {
        matched = numeric ? atof(a) != atof(b) : strcmp(a, b);
    } else if (!strcmp(op, "<")) {
        if (!numeric) {
error:
            Com_Printf("Can't use '%s' with non-numeric expression(s)\n", op);
            return;
        }
        matched = atof(a) < atof(b);
    } else if (!strcmp(op, "<=")) {
        if (!numeric)
            goto error;
        matched = atof(a) <= atof(b);
    } else if (!strcmp(op, ">")) {
        if (!numeric)
            goto error;
        matched = atof(a) > atof(b);
    } else if (!strcmp(op, ">=")) {
        if (!numeric)
            goto error;
        matched = atof(a) >= atof(b);
    } else if (!Q_stricmp(op, "isin")) {
        matched = strstr(b, a) != NULL;
    } else if (!Q_stricmp(op, "!isin")) {
        matched = strstr(b, a) == NULL;
    } else if (!Q_stricmp(op, "isini")) {
        matched = Q_stristr(b, a) != NULL;
    } else if (!Q_stricmp(op, "!isini")) {
        matched = Q_stristr(b, a) == NULL;
    } else if (!Q_stricmp(op, "eq")) {
        matched = !Q_stricmp(a, b);
    } else if (!Q_stricmp(op, "ne")) {
        matched = Q_stricmp(a, b);
    } else {
        Com_Printf("Unknown operator '%s'\n", op);
        Com_Printf("Valid are: ==, != or <>, <, <=, >, >=, [!]isin[i], eq, ne\n");
        return;
    }

    // skip over optional 'then'
    i = 4;
    if (!Q_stricmp(Cmd_Argv(i), "then")) {
        i++;
    }

    // scan out branch 1 argument range
    for (j = i; i < Cmd_Argc(); i++) {
        if (!Q_stricmp(Cmd_Argv(i), "else")) {
            break;
        }
    }

    if (matched) {
        // execute branch 1
        if (i > j) {
            Cbuf_InsertText(cmd_current, Cmd_ArgsRange(j, i - 1));
        }
    } else {
        // execute branch 2
        if (++i < Cmd_Argc()) {
            Cbuf_InsertText(cmd_current, Cmd_ArgsFrom(i));
        }
    }
}

/*
=============================================================================

                    MACRO EXECUTION

=============================================================================
*/

#define MACRO_HASH_SIZE    64

static cmd_macro_t  *cmd_macros;
static cmd_macro_t  *cmd_macroHash[MACRO_HASH_SIZE];

/*
============
Cmd_FindMacro
============
*/
cmd_macro_t *Cmd_FindMacro(const char *name)
{
    cmd_macro_t *macro;
    unsigned hash;

    hash = Com_HashString(name, MACRO_HASH_SIZE);
    for (macro = cmd_macroHash[hash]; macro; macro = macro->hashNext) {
        if (!strcmp(macro->name, name)) {
            return macro;
        }
    }

    return NULL;
}

void Cmd_Macro_g(genctx_t *ctx)
{
    cmd_macro_t *m;

    for (m = cmd_macros; m; m = m->next) {
        if (!Prompt_AddMatch(ctx, m->name)) {
            break;
        }
    }
}

/*
============
Cmd_AddMacro
============
*/
void Cmd_AddMacro(const char *name, xmacro_t function)
{
    cmd_macro_t *macro;
    unsigned hash;

// fail if the macro is a variable name
    if (Cvar_Exists(name, qfalse)) {
        Com_WPrintf("%s: %s already defined as a cvar\n", __func__, name);
        return;
    }

// fail if the macro already exists
    macro = Cmd_FindMacro(name);
    if (macro) {
        if (macro->function != function) {
            Com_WPrintf("%s: %s already defined\n", __func__, name);
        }
        return;
    }

    macro = Cmd_Malloc(sizeof(cmd_macro_t));
    macro->name = name;
    macro->function = function;
    macro->next = cmd_macros;
    cmd_macros = macro;

    hash = Com_HashString(name, MACRO_HASH_SIZE);
    macro->hashNext = cmd_macroHash[hash];
    cmd_macroHash[hash] = macro;
}


/*
=============================================================================

                    COMMAND EXECUTION

=============================================================================
*/

#define CMD_HASH_SIZE    128

#define FOR_EACH_CMD_HASH(cmd, hash) \
    LIST_FOR_EACH(cmd_function_t, cmd, &cmd_hash[hash], hashEntry)
#define FOR_EACH_CMD(cmd) \
    LIST_FOR_EACH(cmd_function_t, cmd, &cmd_functions, listEntry)

typedef struct cmd_function_s {
    list_t          hashEntry;
    list_t          listEntry;

    xcommand_t      function;
    xcompleter_t    completer;
    char            *name;
} cmd_function_t;

static  list_t  cmd_functions;        // possible commands to execute
static  list_t  cmd_hash[CMD_HASH_SIZE];

static  int     cmd_argc;
static  char    *cmd_argv[MAX_STRING_TOKENS]; // pointers to cmd_data[]
static  char    *cmd_null_string = "";

// complete command string, left untouched
static  char    cmd_string[MAX_STRING_CHARS];
static  size_t  cmd_string_len;
static  size_t  cmd_string_tail;

// offsets of individual tokens into cmd_string
static  size_t  cmd_offsets[MAX_STRING_TOKENS];

// sequence of NULL-terminated, normalized tokens
static  char    cmd_data[MAX_STRING_CHARS];

// normalized command arguments
static  char    cmd_args[MAX_STRING_CHARS];

int             cmd_optind;
char            *cmd_optarg;
char            *cmd_optopt;

from_t Cmd_From(void)
{
    return cmd_current->from;
}

size_t Cmd_ArgOffset(int arg)
{
    if (arg < 0) {
        return 0;
    }
    if (arg >= cmd_argc) {
        return cmd_string_len;
    }
    return cmd_offsets[arg];
}

int Cmd_FindArgForOffset(size_t offset)
{
    int i;

    for (i = 1; i < cmd_argc; i++) {
        if (offset < cmd_offsets[i]) {
            break;
        }
    }
    return i - 1;
}

size_t Cmd_WhiteSpaceTail(void)
{
    return cmd_string_tail;
}

/*
============
Cmd_Argc
============
*/
int Cmd_Argc(void)
{
    return cmd_argc;
}

/*
============
Cmd_Argv
============
*/
char *Cmd_Argv(int arg)
{
    if (arg < 0 || arg >= cmd_argc) {
        return cmd_null_string;
    }
    return cmd_argv[arg];
}

/*
============
Cmd_ArgvBuffer
============
*/
size_t Cmd_ArgvBuffer(int arg, char *buffer, size_t size)
{
    char *s;

    if (arg < 0 || arg >= cmd_argc) {
        s = cmd_null_string;
    } else {
        s = cmd_argv[arg];
    }

    return Q_strlcpy(buffer, s, size);
}


/*
============
Cmd_Args

Returns a single string containing argv(1) to argv(argc()-1)
============
*/
char *Cmd_Args(void)
{
    int i;

    if (cmd_argc < 2) {
        return cmd_null_string;
    }

    cmd_args[0] = 0;
    for (i = 1; i < cmd_argc - 1; i++) {
        strcat(cmd_args, cmd_argv[i]);
        strcat(cmd_args, " ");
    }
    strcat(cmd_args, cmd_argv[i]);

    return cmd_args;
}

char *Cmd_RawArgs(void)
{
    if (cmd_argc < 2) {
        return cmd_null_string;
    }
    return cmd_string + cmd_offsets[1];
}

char *Cmd_RawString(void)
{
    return cmd_string;
}

/*
============
Cmd_ArgsBuffer
============
*/
size_t Cmd_ArgsBuffer(char *buffer, size_t size)
{
    return Q_strlcpy(buffer, Cmd_Args(), size);
}

/*
============
Cmd_ArgsFrom

Returns a single string containing argv(1) to argv(from-1)
============
*/
char *Cmd_ArgsFrom(int from)
{
    int i;

    if (from < 0 || from >= cmd_argc) {
        return cmd_null_string;
    }

    cmd_args[0] = 0;
    for (i = from; i < cmd_argc - 1; i++) {
        strcat(cmd_args, cmd_argv[i]);
        strcat(cmd_args, " ");
    }
    strcat(cmd_args, cmd_argv[i]);

    return cmd_args;
}

static char *Cmd_ArgsRange(int from, int to)
{
    int i;

    if (from < 0 || from >= cmd_argc) {
        return cmd_null_string;
    }

    if (to > cmd_argc - 1) {
        to = cmd_argc - 1;
    }

    cmd_args[0] = 0;
    for (i = from; i < to; i++) {
        strcat(cmd_args, cmd_argv[i]);
        strcat(cmd_args, " ");
    }
    strcat(cmd_args, cmd_argv[i]);

    return cmd_args;
}

char *Cmd_RawArgsFrom(int from)
{
    size_t offset;

    if (from < 0 || from >= cmd_argc) {
        return cmd_null_string;
    }

    offset = cmd_offsets[from];

    return cmd_string + offset;
}

void Cmd_Shift(void)
{
    int i;

    if (!cmd_argc) {
        return;
    }

    if (cmd_argc == 1) {
        cmd_string[0] = 0;
        return;
    }

    cmd_argc--;
    for (i = 0; i < cmd_argc; i++) {
        cmd_offsets[i] = cmd_offsets[i + 1];
        cmd_argv[i] = cmd_argv[i + 1];
    }

    memmove(cmd_string, cmd_string + cmd_offsets[1],
            MAX_STRING_CHARS - cmd_offsets[1]);
}

int Cmd_ParseOptions(const cmd_option_t *opt)
{
    const cmd_option_t *o;
    char *s, *p;

    cmd_optopt = cmd_null_string;

    if (cmd_optind == cmd_argc) {
        cmd_optarg = cmd_null_string;
        return -1; // no more arguments
    }

    s = cmd_argv[cmd_optind];
    if (*s != '-') {
        cmd_optarg = s;
        return -1; // non-option argument
    }
    cmd_optopt = s++;

    if (*s == '-') {
        s++;
        if (*s == 0) {
            if (++cmd_optind < cmd_argc) {
                cmd_optarg = cmd_argv[cmd_optind];
            } else {
                cmd_optarg = cmd_null_string;
            }
            return -1; // special terminator
        }

        // check for long option argument
        if ((p = strchr(s, '=')) != NULL) {
            *p = 0;
        }

        // parse long option
        for (o = opt; o->sh; o++) {
            if (!strcmp(o->lo, s)) {
                break;
            }
        }
        if (!o->sh) {
            goto unknown;
        }

        // parse long option argument
        if (p) {
            if (o->sh[1] != ':') {
                Com_Printf("%s does not take an argument.\n", cmd_argv[cmd_optind]);
                Cmd_PrintHint();
                return '!';
            }
            cmd_optarg = p + 1;
        }
    } else {
        // parse short option
        for (o = opt; o->sh; o++) {
            if (o->sh[0] == *s) {
                break;
            }
        }
        if (!o->sh || s[1]) {
            goto unknown;
        }
        p = NULL;
    }

    // parse option argument
    if (!p && o->sh[1] == ':') {
        if (cmd_optind + 1 == cmd_argc) {
            Com_Printf("Missing argument to %s.\n", cmd_argv[cmd_optind]);
            Cmd_PrintHint();
            return ':';
        }
        cmd_optarg = cmd_argv[++cmd_optind];
    }

    cmd_optind++;
    return o->sh[0];

unknown:
    Com_Printf("Unknown option: %s.\n", cmd_argv[cmd_optind]);
    Cmd_PrintHint();
    return '?';
}

void Cmd_PrintUsage(const cmd_option_t *opt, const char *suffix)
{
    Com_Printf("Usage: %s [-", cmd_argv[0]);
    while (opt->sh) {
        Com_Printf("%c", opt->sh[0]);
        if (opt->sh[1] == ':') {
            Com_Printf(":");
        }
        opt++;
    }
    if (suffix) {
        Com_Printf("] %s\n", suffix);
    } else {
        Com_Printf("]\n");
    }
}

void Cmd_PrintHelp(const cmd_option_t *opt)
{
    char buffer[32];

    Com_Printf("\nAvailable options:\n");
    while (opt->sh) {
        if (opt->sh[1] == ':') {
            Q_concat(buffer, sizeof(buffer),
                     opt->lo, "=<", opt->sh + 2, ">", NULL);
        } else {
            Q_strlcpy(buffer, opt->lo, sizeof(buffer));
        }
        Com_Printf("-%c | --%-16.16s | %s\n", opt->sh[0], buffer, opt->help);
        opt++;
    }
    Com_Printf("\n");
}

void Cmd_PrintHint(void)
{
    Com_Printf("Try '%s --help' for more information.\n", cmd_argv[0]);
}

void Cmd_Option_c(const cmd_option_t *opt, xgenerator_t g, genctx_t *ctx, int argnum)
{
    if (ctx->partial[0] == '-') {
        for (; opt->sh; opt++) {
            if (ctx->count >= ctx->size) {
                break;
            }
            if (ctx->partial[1] == '-') {
                if (!strncmp(opt->lo, ctx->partial + 2, ctx->length - 2)) {
                    ctx->matches[ctx->count++] = Z_CopyString(va("--%s", opt->lo));
                }
            } else if (!ctx->partial[1] || opt->sh[0] == ctx->partial[1]) {
                ctx->matches[ctx->count++] = Z_CopyString(va("-%c", opt->sh[0]));
            }
        }
    } else {
#if 0
        if (argnum > 1) {
            s = cmd_argv[argnum - 1];
        }
#endif
        if (g) {
            g(ctx);
        } else if (!ctx->partial[0] && ctx->count < ctx->size) {
            ctx->matches[ctx->count++] = Z_CopyString("-");
        }
    }
}


/*
======================
Cmd_MacroExpandString
======================
*/
char *Cmd_MacroExpandString(const char *text, qboolean aliasHack)
{
    size_t      i, j, len;
    int         count;
    qboolean    inquote;
    char        *scan, *start;
    static char expanded[MAX_STRING_CHARS];
    char        temporary[MAX_STRING_CHARS];
    char        buffer[MAX_STRING_CHARS];
    char        *token;
    cmd_macro_t *macro;
    cvar_t      *var;
    qboolean    rescan;

    len = strlen(text);
    if (len >= MAX_STRING_CHARS) {
        Com_Printf("Line exceeded %i chars, discarded.\n", MAX_STRING_CHARS);
        return NULL;
    }

    scan = memcpy(expanded, text, len + 1);

    inquote = qfalse;
    count = 0;

    for (i = 0; i < len; i++) {
        if (!scan[i]) {
            break;
        }
        if (scan[i] == '"') {
            inquote ^= 1;
        }
        if (inquote) {
            continue;    // don't expand inside quotes
        }
        if (scan[i] != '$') {
            continue;
        }

        // scan out the complete macro
        start = scan + i + 1;

        if (*start == 0) {
            break;    // end of string
        }

        // allow $$ escape syntax
        if (*start == '$') {
            memmove(scan + i, start, len - i);
            continue;
        }

        // skip leading spaces
        while (*start && *start <= 32) {
            start++;
        }

        token = temporary;

        if (*start == '{') {
            // allow ${variable} syntax
            start++;
            if (*start == '$') {  // allow ${$variable} syntax
                start++;
            }
            while (*start) {
                if (*start == '}') {
                    start++;
                    break;
                }
                *token++ = *start++;
            }
        } else {
            // parse single word
            while (*start > 32) {
                if (*start == '$') {  // allow $var$ syntax
                    start++;
                    break;
                }
                *token++ = *start++;
            }
        }

        *token = 0;

        if (token == temporary) {
            continue;
        }

        rescan = qfalse;

        if (aliasHack) {
            // expand positional parameters only
            if (!strcmp(temporary, "@")) {
                token = Cmd_Args();
            } else {
                int arg1, arg2;
                char *s;

                // parse {arg1-arg2} format for ranges
                arg1 = strtoul(temporary, &s, 10);
                if (s[0] == '-') {
                    if (s[1]) {
                        arg2 = strtoul(s + 1, &s, 10);
                        if (s[0]) {
                            continue; // second part is not a number
                        }
                    } else {
                        arg2 = cmd_argc - 1;
                    }
                    token = Cmd_ArgsRange(arg1, arg2);
                } else if (s[0] == 0) {
                    token = Cmd_Argv(arg1);
                } else {
                    continue; // first part is not a number
                }
            }
        } else {
            // check for macros first
            macro = Cmd_FindMacro(temporary);
            if (macro) {
                macro->function(buffer, MAX_STRING_CHARS - len);
                token = buffer;
            } else {
                // than variables
                var = Cvar_FindVar(temporary);
                if (var && !(var->flags & CVAR_PRIVATE)) {
                    token = var->string;
                    rescan = qtrue;
                } else if (!strcmp(temporary, "qt")) {
                    token = "\"";
                } else if (!strcmp(temporary, "sc")) {
                    token = ";";
                } else {
                    token = "";
                }
            }
        }

        j = strlen(token);
        len += j;
        if (len >= MAX_STRING_CHARS) {
            Com_Printf("Expanded line exceeded %i chars, discarded.\n",
                       MAX_STRING_CHARS);
			Com_Printf("was %s\n", text);
            return NULL;
        }

        strncpy(temporary, scan, i);
        strcpy(temporary + i, token);
        strcpy(temporary + i + j, start);

        strcpy(expanded, temporary);
        scan = expanded;
        if (!rescan) {
            i += j;
        }
        i--;

        if (++count == 100) {
            Com_Printf("Macro expansion loop, discarded.\n");
            return NULL;
        }
    }

    if (inquote) {
        Com_Printf("Line has unmatched quote, discarded.\n");
        return NULL;
    }

    return scan;
}

/*
============
Cmd_TokenizeString

Parses the given string into command line tokens.
$Cvars will be expanded unless they are in a quoted token
============
*/
void Cmd_TokenizeString(const char *text, qboolean macroExpand)
{
    int     i;
    char    *data, *start, *dest;

// clear the args from the last string
    for (i = 0; i < cmd_argc; i++) {
        cmd_argv[i] = NULL;
        cmd_offsets[i] = 0;
    }

    cmd_argc = 0;
    cmd_string[0] = 0;
    cmd_string_len = 0;
    cmd_string_tail = 0;
    cmd_optind = 1;
    cmd_optarg = cmd_optopt = cmd_null_string;

    if (!text[0]) {
        return;
    }

// macro expand the text
    if (macroExpand) {
        text = Cmd_MacroExpandString(text, qfalse);
        if (!text) {
            return;
        }
    }

    cmd_string_len = Q_strlcpy(cmd_string, text, sizeof(cmd_string));
    if (cmd_string_len >= sizeof(cmd_string)) {
        Com_Printf("Line exceeded %i chars, discarded.\n", MAX_STRING_CHARS);
        return;
    }

// strip off any trailing whitespace
    while (cmd_string_len) {
        if (cmd_string[cmd_string_len - 1] > ' ') {
            break;
        }
        cmd_string[cmd_string_len - 1] = 0;
        cmd_string_len--;
        cmd_string_tail++;
    }

    dest = cmd_data;
    start = data = cmd_string;
    while (cmd_argc < MAX_STRING_TOKENS) {
// skip whitespace up to a /n
        while (*data <= ' ') {
            if (*data == 0) {
                return; // end of text
            }
            if (*data == '\n') {
                return; // a newline seperates commands in the buffer
            }
            data++;
        }

// add new argument
        cmd_offsets[cmd_argc] = data - start;
        cmd_argv[cmd_argc] = dest;
        cmd_argc++;

        if (*data == ';') {
            data++;
            *dest++ = ';';
            *dest++ = 0;
            continue;
        }

// parse quoted string
        if (*data == '\"') {
            data++;
            while (*data != '\"') {
                if (*data == 0) {
                    return; // end of data
                }
                *dest++ = *data++;
            }
            data++;
            *dest++ = 0;
            continue;
        }

// parse reqular token
        while (*data > ' ') {
            if (*data == '\"') {
                break;
            }
            if (*data == ';') {
                break;
            }
            *dest++ = *data++;
        }
        *dest++ = 0;

        if (*data == 0) {
            return; // end of text
        }
    }
}

/*
============
Cmd_Find
============
*/
static cmd_function_t *Cmd_Find(const char *name)
{
    cmd_function_t *cmd;
    unsigned hash;

    hash = Com_HashString(name, CMD_HASH_SIZE);
    FOR_EACH_CMD_HASH(cmd, hash) {
        if (!strcmp(cmd->name, name)) {
            return cmd;
        }
    }

    return NULL;
}

static void Cmd_RegCommand(const cmdreg_t *reg)
{
    cmd_function_t *cmd;
    unsigned hash;

// fail if the command is a variable name
    if (Cvar_Exists(reg->name, qfalse)) {
        Com_WPrintf("%s: %s already defined as a cvar\n", __func__, reg->name);
        return;
    }

// fail if the command already exists
    cmd = Cmd_Find(reg->name);
    if (cmd) {
        if (cmd->function) {
            Com_WPrintf("%s: %s already defined\n", __func__, reg->name);
            return;
        }
        cmd->function = reg->function;
        cmd->completer = reg->completer;
        return;
    }

    cmd = Cmd_Malloc(sizeof(*cmd));
    cmd->name = (char *)reg->name;
    cmd->function = reg->function;
    cmd->completer = reg->completer;

    List_Append(&cmd_functions, &cmd->listEntry);

    hash = Com_HashString(reg->name, CMD_HASH_SIZE);
    List_Append(&cmd_hash[hash], &cmd->hashEntry);
}

/*
============
Cmd_AddCommand
============
*/
void Cmd_AddCommand(const char *name, xcommand_t function)
{
    cmdreg_t reg;

    reg.name = name;
    reg.function = function;
    reg.completer = NULL;
    Cmd_RegCommand(&reg);
}

void Cmd_Register(const cmdreg_t *reg)
{
    while (reg->name) {
        Cmd_RegCommand(reg);
        reg++;
    }
}

void Cmd_Deregister(const cmdreg_t *reg)
{
    while (reg->name) {
        Cmd_RemoveCommand(reg->name);
        reg++;
    }
}

/*
============
Cmd_RemoveCommand
============
*/
void Cmd_RemoveCommand(const char *name)
{
    cmd_function_t *cmd;

    cmd = Cmd_Find(name);
    if (!cmd) {
        Com_DPrintf("%s: %s not added\n", __func__, name);
        return;
    }

    List_Delete(&cmd->listEntry);
    List_Delete(&cmd->hashEntry);
    Z_Free(cmd);
}

/*
============
Cmd_Exists
============
*/
qboolean Cmd_Exists(const char *name)
{
    cmd_function_t *cmd = Cmd_Find(name);

    return cmd ? qtrue : qfalse;
}

xcommand_t Cmd_FindFunction(const char *name)
{
    cmd_function_t *cmd = Cmd_Find(name);

    return cmd ? cmd->function : NULL;
}

xcompleter_t Cmd_FindCompleter(const char *name)
{
    cmd_function_t *cmd = Cmd_Find(name);

    return cmd ? cmd->completer : NULL;
}

void Cmd_Command_g(genctx_t *ctx)
{
    cmd_function_t *cmd;

    FOR_EACH_CMD(cmd) {
        if (!Prompt_AddMatch(ctx, cmd->name)) {
            break;
        }
    }
}

void Cmd_ExecuteCommand(cmdbuf_t *buf)
{
    cmd_function_t  *cmd;
    cmdalias_t      *a;
    cvar_t          *v;
    char            *text;

    // check functions
    cmd = Cmd_Find(cmd_argv[0]);
    if (cmd) {
        if (cmd->function) {
            cmd->function();
        } else if (!CL_ForwardToServer()) {
            Com_Printf("Can't \"%s\", not connected\n", cmd_argv[0]);
        }
        return;
    }

    // check aliases
    a = Cmd_AliasFind(cmd_argv[0]);
    if (a) {
        if (buf->aliasCount >= ALIAS_LOOP_COUNT) {
            Com_WPrintf("Runaway alias loop\n");
            return;
        }
        text = Cmd_MacroExpandString(a->value, qtrue);
        if (text) {
            buf->aliasCount++;
            Cbuf_InsertText(buf, text);
        }
        return;
    }

    // check variables
    v = Cvar_FindVar(cmd_argv[0]);
    if (v) {
        Cvar_Command(v);
        return;
    }

    // send it as a server command if we are connected
    if (!CL_ForwardToServer()) {
        Com_Printf("Unknown command \"%s\"\n", cmd_argv[0]);
    }
}

/*
============
Cmd_ExecuteString

A complete command line has been parsed, so try to execute it
============
*/
void Cmd_ExecuteString(cmdbuf_t *buf, const char *text)
{
    Cmd_TokenizeString(text, qtrue);

    // execute the command line
    if (!cmd_argc) {
        return;        // no tokens
    }

    Cmd_ExecuteCommand(buf);
}

qerror_t Cmd_ExecuteFile(const char *path, unsigned flags)
{
    char    *f;
    ssize_t len;
    qerror_t ret;
    cmdbuf_t *buf;

    len = FS_LoadFileEx(path, (void **)&f, flags, TAG_FILESYSTEM);
    if (!f) {
        return len;
    }

    // check for binary file
    if (memchr(f, 0, len)) {
        ret = Q_ERR_INVALID_FORMAT;
        goto finish;
    }

    // sanity check file size after stripping off comments
    len = COM_Compress(f);
    if (len > CMD_BUFFER_SIZE) {
        ret = Q_ERR_FBIG;
        goto finish;
    }

    // FIXME: always insert into main command buffer,
    // no matter where command came from?
    buf = &cmd_buffer;

    // check for exec loop
    if (++buf->aliasCount > ALIAS_LOOP_COUNT) {
        ret = Q_ERR_RUNAWAY_LOOP;
        goto finish;
    }

    // check for overflow
    if (buf->cursize + len + 1 > buf->maxsize) {
        ret = Q_ERR_STRING_TRUNCATED;
        goto finish;
    }

    // everything ok, execute it
    Com_Printf("Execing %s\n", path);
    Cbuf_InsertText(buf, f);
    ret = Q_ERR_SUCCESS;

finish:
    FS_FreeFile(f);
    return ret;
}

/*
===============
Cmd_Exec_f
===============
*/
static void Cmd_Exec_f(void)
{
    char    buffer[MAX_QPATH];
    size_t  len;
    qerror_t ret;

    if (Cmd_Argc() != 2) {
        Com_Printf("%s <filename> : execute a script file\n", Cmd_Argv(0));
        return;
    }

    len = FS_NormalizePathBuffer(buffer, Cmd_Argv(1), sizeof(buffer));
    if (len >= sizeof(buffer)) {
        Q_PrintError("exec", Q_ERR_NAMETOOLONG);
        return;
    }

    if (len == 0) {
        Q_PrintError("exec", Q_ERR_NAMETOOSHORT);
        return;
    }

    ret = Cmd_ExecuteFile(buffer, 0);
    if (ret == Q_ERR_NOENT && COM_CompareExtension(buffer, ".cfg")) {
        // try with .cfg extension
        len = Q_strlcat(buffer, ".cfg", sizeof(buffer));
        if (len >= sizeof(buffer)) {
            Q_PrintError("exec", Q_ERR_NAMETOOLONG);
            return;
        }

        ret = Cmd_ExecuteFile(buffer, 0);
    }

    if (ret) {
        Com_Printf("Couldn't exec %s: %s\n", buffer, Q_ErrorString(ret));
    }
}

void Cmd_Config_g(genctx_t *ctx)
{
    FS_File_g(NULL, "*.cfg", FS_SEARCH_SAVEPATH | FS_SEARCH_BYFILTER | FS_SEARCH_STRIPEXT, ctx);
}

static void Cmd_Exec_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        Cmd_Config_g(ctx);
    }
}

/*
===============
Cmd_Echo_f

Just prints the rest of the line to the console
===============
*/
static void Cmd_Echo_f(void)
{
    Com_Printf("%s\n", Cmd_Args());
}

static const cmd_option_t o_echo[] = {
    { "h", "help", "display this message" },
    { "e", "escapes", "enable interpretation of backslash escapes" },
    { "c:color", "color", "print text in this color" },
    { "n", "no-newline", "do not output the trailing newline" },
    { NULL }
};

static void Cmd_EchoEx_c(genctx_t *ctx, int argnum)
{
    Cmd_Option_c(o_echo, NULL, ctx, argnum);
}

static char *unescape_string(char *dst, const char *src)
{
    int c1, c2;
    char *p = dst;

    while (*src) {
        if (src[0] == '\\' && src[1]) {
            switch (src[1]) {
            case 'a': *p++ = '\a'; break;
            case 'b': *p++ = '\b'; break;
            case 't': *p++ = '\t'; break;
            case 'n': *p++ = '\n'; break;
            case 'v': *p++ = '\v'; break;
            case 'f': *p++ = '\f'; break;
            case 'r': *p++ = '\r'; break;
            case '\\': *p++ = '\\'; break;
            case 'x':
                if ((c1 = Q_charhex(src[2])) == -1) {
                    break;
                }
                if ((c2 = Q_charhex(src[3])) == -1) {
                    break;
                }
                *p++ = (c1 << 4) | c2;
                src += 2;
                break;
            default:
                *p++ = src[1];
                break;
            }
            src += 2;
        } else {
            *p++ = *src++;
        }
    }
    *p = 0;

    return dst;
}

static void Cmd_EchoEx_f(void)
{
    char buffer[MAX_STRING_CHARS], *s;
    qboolean escapes = qfalse;
    color_index_t color = COLOR_NONE;
    const char *newline = "\n";
    int c;

    while ((c = Cmd_ParseOptions(o_echo)) != -1) {
        switch (c) {
        case 'h':
            Cmd_PrintUsage(o_echo, "[text]");
            Com_Printf("Print a line of text into the console.\n");
            Cmd_PrintHelp(o_echo);
            return;
        case 'e':
            escapes = qtrue;
            break;
        case 'c':
            color = Com_ParseColor(cmd_optarg, COLOR_NONE);
            break;
        case 'n':
            newline = "";
            break;
        default:
            return;
        }
    }

    s = Cmd_RawArgsFrom(cmd_optind);
    if (escapes) {
        s = unescape_string(buffer, s);
    }

    Com_SetColor(color);
    Com_Printf("%s%s", s, newline);
    Com_SetColor(COLOR_NONE);
}

/*
============
Cmd_List_f
============
*/
static void Cmd_List_f(void)
{
    cmd_function_t  *cmd;
    int             i, total;
    char            *filter = NULL;

    if (cmd_argc > 1) {
        filter = cmd_argv[1];
    }

    i = total = 0;
    FOR_EACH_CMD(cmd) {
        total++;
        if (filter && !Com_WildCmp(filter, cmd->name)) {
            continue;
        }
        Com_Printf("%s\n", cmd->name);
        i++;
    }
    Com_Printf("%i of %i commands\n", i, total);
}

/*
============
Cmd_MacroList_f
============
*/
static void Cmd_MacroList_f(void)
{
    cmd_macro_t     *macro;
    int             i, total;
    char            *filter = NULL;
    char            buffer[MAX_QPATH];

    if (cmd_argc > 1) {
        filter = cmd_argv[1];
    }

    i = 0;
    for (macro = cmd_macros, total = 0; macro; macro = macro->next, total++) {
        if (filter && !Com_WildCmp(filter, macro->name)) {
            continue;
        }
        macro->function(buffer, sizeof(buffer));
        Com_Printf("%-16s %s\n", macro->name, buffer);
        i++;
    }
    Com_Printf("%i of %i macros\n", i, total);
}

static void Cmd_Text_f(void)
{
    Cbuf_AddText(cmd_current, Cmd_Args());
    Cbuf_AddText(cmd_current, "\n");
}

static void Cmd_Complete_f(void)
{
    cmd_function_t *cmd;
    char *name;
    unsigned hash;
    size_t len;

    if (cmd_argc < 2) {
        Com_Printf("Usage: %s <command>", cmd_argv[0]);
        return;
    }

    name = cmd_argv[1];

// fail if the command is a variable name
    if (Cvar_Exists(name, qtrue)) {
        Com_Printf("%s is already defined as a cvar\n", name);
        return;
    }

// fail if the command already exists
    cmd = Cmd_Find(name);
    if (cmd) {
        //Com_Printf("%s is already defined\n", name);
        return;
    }

    len = strlen(name) + 1;
    cmd = Cmd_Malloc(sizeof(*cmd) + len);
    cmd->name = (char *)(cmd + 1);
    memcpy(cmd->name, name, len);
    cmd->function = NULL;
    cmd->completer = NULL;

    List_Append(&cmd_functions, &cmd->listEntry);

    hash = Com_HashString(name, CMD_HASH_SIZE);
    List_Append(&cmd_hash[hash], &cmd->hashEntry);
}

static const cmdreg_t c_cmd[] = {
    { "cmdlist", Cmd_List_f },
    { "macrolist", Cmd_MacroList_f },
    { "exec", Cmd_Exec_f, Cmd_Exec_c },
    { "echo", Cmd_Echo_f },
    { "_echo", Cmd_EchoEx_f, Cmd_EchoEx_c },
    { "alias", Cmd_Alias_f, Cmd_Alias_c },
    { "unalias", Cmd_UnAlias_f, Cmd_UnAlias_c },
    { "wait", Cmd_Wait_f },
    { "text", Cmd_Text_f },
    { "complete", Cmd_Complete_f },
    { "trigger", Cmd_Trigger_f },
    { "untrigger", Cmd_UnTrigger_f },
    { "if", Cmd_If_f },

    { NULL }
};

/*
============
Cmd_Init
============
*/
void Cmd_Init(void)
{
    int i;

    List_Init(&cmd_functions);
    for (i = 0; i < CMD_HASH_SIZE; i++) {
        List_Init(&cmd_hash[i]);
    }

    List_Init(&cmd_alias);
    for (i = 0; i < ALIAS_HASH_SIZE; i++) {
        List_Init(&cmd_aliasHash[i]);
    }

    List_Init(&cmd_triggers);

    Cmd_Register(c_cmd);
}

