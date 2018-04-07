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

#ifndef CMD_H
#define CMD_H

//
// cmd.h -- command text buffering and command execution
//

#define CMD_BUFFER_SIZE     (1 << 16) // bumped max config size up to 64K

#define ALIAS_LOOP_COUNT    16

// where did current command come from?
typedef enum {
    FROM_STUFFTEXT,
    FROM_RCON,
    FROM_MENU,
    FROM_CONSOLE,
    FROM_CMDLINE,
    FROM_CODE
} from_t;

typedef struct cmdbuf_s {
    from_t      from;
    char        *text; // may not be NULL terminated
    size_t      cursize;
    size_t      maxsize;
    int         waitCount;
    int         aliasCount; // for detecting runaway loops
    void        (*exec)(struct cmdbuf_s *, const char *);
} cmdbuf_t;

// generic console buffer
extern char         cmd_buffer_text[CMD_BUFFER_SIZE];
extern cmdbuf_t     cmd_buffer;

extern cmdbuf_t     *cmd_current;

/*
Any number of commands can be added in a frame, from several different sources.
Most commands come from either keybindings or console line input, but remote
servers can also send across commands and entire text files can be execed.
*/

void Cbuf_Init(void);
// allocates an initial text buffer that will grow as needed

void Cbuf_AddText(cmdbuf_t *buf, const char *text);
// as new commands are generated from the console or keybindings,
// the text is added to the end of the command buffer.

void Cbuf_InsertText(cmdbuf_t *buf, const char *text);
// when a command wants to issue other commands immediately, the text is
// inserted at the beginning of the buffer, before any remaining unexecuted
// commands.

void Cbuf_Execute(cmdbuf_t *buf);
// Pulls off \n terminated lines of text from the command buffer and sends
// them through Cmd_ExecuteString.  Stops when the buffer is empty.
// Normally called once per frame, but may be explicitly invoked.
// Do not call inside a command function!

//===========================================================================

/*
Command execution takes a null terminated string, breaks it into tokens,
then searches for a command or variable that matches the first token.
*/

typedef struct genctx_s {
    const char  *partial;
    size_t length;
    int argnum;
    char **matches;
    int count, size;
    void *data;
    qboolean ignorecase;
    qboolean ignoredups;
} genctx_t;

typedef void (*xcommand_t)(void);
typedef void (*xcommandex_t)(cmdbuf_t *);
typedef size_t (*xmacro_t)(char *, size_t);
typedef void (*xcompleter_t)(struct genctx_s *, int);

typedef struct cmd_macro_s {
    struct cmd_macro_s  *next, *hashNext;
    const char          *name;
    xmacro_t            function;
} cmd_macro_t;

typedef struct {
    const char *sh, *lo, *help;
} cmd_option_t;

typedef struct cmdreg_s {
    const char      *name;
    xcommand_t      function;
    xcompleter_t    completer;
} cmdreg_t;

void Cmd_Init(void);

qboolean Cmd_Exists(const char *cmd_name);
// used by the cvar code to check for cvar / command name overlap

void Cmd_ExecTrigger(const char *string);

xcommand_t Cmd_FindFunction(const char *name);
cmd_macro_t *Cmd_FindMacro(const char *name);
xcompleter_t Cmd_FindCompleter(const char *name);

char *Cmd_AliasCommand(const char *name);
void Cmd_AliasSet(const char *name, const char *cmd);

void Cmd_Command_g(genctx_t *ctx);
void Cmd_Alias_g(genctx_t *ctx);
void Cmd_Macro_g(genctx_t *ctx);
void Cmd_Config_g(genctx_t *ctx);
void Cmd_Option_c(const cmd_option_t *opt, xgenerator_t g, genctx_t *ctx, int argnum);
// attempts to match a partial command for automatic command line completion
// returns NULL if nothing fits

void Cmd_TokenizeString(const char *text, qboolean macroExpand);
// Takes a null terminated string.  Does not need to be /n terminated.
// breaks the string up into arg tokens.

void Cmd_ExecuteCommand(cmdbuf_t *buf);
// execute already tokenized string

void Cmd_ExecuteString(cmdbuf_t *buf, const char *text);
// Parses a single line of text into arguments and tries to execute it
// as if it was typed at the console

qerror_t Cmd_ExecuteFile(const char *path, unsigned flags);
// execute a config file

char *Cmd_MacroExpandString(const char *text, qboolean aliasHack);

void Cmd_Register(const cmdreg_t *reg);
void Cmd_AddCommand(const char *cmd_name, xcommand_t function);
// called by the init functions of other parts of the program to
// register commands and functions to call for them.
// The cmd_name is referenced later, so it should not be in temp memory
// if function is NULL, the command will be forwarded to the server
// as a clc_stringcmd instead of executed locally
void Cmd_Deregister(const cmdreg_t *reg);
void Cmd_RemoveCommand(const char *cmd_name);

void Cmd_AddMacro(const char *name, xmacro_t function);

from_t  Cmd_From(void);
int     Cmd_Argc(void);
char    *Cmd_Argv(int arg);
char    *Cmd_Args(void);
char    *Cmd_RawArgs(void);
char    *Cmd_ArgsFrom(int from);
char    *Cmd_RawArgsFrom(int from);
size_t  Cmd_ArgsBuffer(char *buffer, size_t size);
size_t  Cmd_ArgvBuffer(int arg, char *buffer, size_t size);
int     Cmd_ArgOffset(int arg);
int     Cmd_FindArgForOffset(int offset);
char    *Cmd_RawString(void);
void    Cmd_Shift(void);
// The functions that execute commands get their parameters with these
// functions. Cmd_Argv () will return an empty string, not a NULL
// if arg > argc, so string operations are always safe.

void Cmd_Alias_f(void);

void Cmd_WriteAliases(qhandle_t f);

#define EXEC_TRIGGER(var) \
    do { \
        if ((var)->string[0]) { \
            Cbuf_AddText(&cmd_buffer, (var)->string); \
        } \
    } while(0)

extern int cmd_optind;
extern char *cmd_optarg;
extern char *cmd_optopt;

int Cmd_ParseOptions(const cmd_option_t *opt);
void Cmd_PrintHelp(const cmd_option_t *opt);
void Cmd_PrintUsage(const cmd_option_t *opt, const char *suffix);
void Cmd_PrintHint(void);

const char *Cmd_Completer(const cmd_option_t *opt, const char *partial,
                          int argnum, int state, xgenerator_t generator);

#endif // CMD_H
