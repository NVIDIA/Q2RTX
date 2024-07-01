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
// cvar.c -- dynamic variable tracking

#include "shared/shared.h"
#include "common/cmd.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "common/prompt.h"
#include "common/utils.h"
#include "common/zone.h"
#include "client/client.h"

cvar_t  *cvar_vars;

int     cvar_modified;

#define Cvar_Malloc(size)   Z_TagMalloc(size, TAG_CVAR)

#define CVARHASH_SIZE    256

static cvar_t *cvarHash[CVARHASH_SIZE];

/*
============
Cvar_FindVar
============
*/
cvar_t *Cvar_FindVar(const char *var_name)
{
    cvar_t *var;
    unsigned hash;

    hash = Com_HashString(var_name, CVARHASH_SIZE);

    for (var = cvarHash[hash]; var; var = var->hashNext) {
        if (!strcmp(var_name, var->name)) {
            return var;
        }
    }

    return NULL;
}

xgenerator_t Cvar_FindGenerator(const char *var_name)
{
    cvar_t *var = Cvar_FindVar(var_name);

    return var ? var->generator : NULL;
}

/*
============
Cvar_Exists
============
*/
bool Cvar_Exists(const char *var_name, bool weak)
{
    cvar_t *var = Cvar_FindVar(var_name);

    if (!var)
        return false;
    if (!weak && (var->flags & (CVAR_CUSTOM | CVAR_WEAK)))
        return false;
    return true;
}

/*
============
Cvar_VariableValue
============
*/
float Cvar_VariableValue(const char *var_name)
{
    cvar_t    *var;

    var = Cvar_FindVar(var_name);
    if (!var)
        return 0;

    return var->value;
}

/*
============
Cvar_VariableInteger
============
*/
int Cvar_VariableInteger(const char *var_name)
{
    cvar_t *var;

    var = Cvar_FindVar(var_name);
    if (!var)
        return 0;

    return var->integer;
}


/*
============
Cvar_VariableString
============
*/
const char *Cvar_VariableString(const char *var_name)
{
    cvar_t *var;

    var = Cvar_FindVar(var_name);
    if (!var)
        return "";

    return var->string;
}

void Cvar_Variable_g(genctx_t *ctx)
{
    cvar_t *c;

    for (c = cvar_vars; c; c = c->next)
        Prompt_AddMatch(ctx, c->name);
}

void Cvar_Default_g(genctx_t *ctx)
{
    cvar_t *c = ctx->data;

    if (c) {
        if (strcmp(c->string, c->default_string)) {
            Prompt_AddMatch(ctx, c->string);
        }
        Prompt_AddMatch(ctx, c->default_string);
    }
}

// parse integer and float values
static void parse_string_value(cvar_t *var)
{
    char *s = var->string;

    if (s[0] == '0' && s[1] == 'x') {
        long v = strtol(s, NULL, 16);

        var->integer = Q_clipl_int32(v);
        var->value = (float)var->integer;
    } else {
        var->integer = Q_atoi(s);
        var->value = Q_atof(s);
        if (var->value != 0.0f && !isnormal(var->value))
            var->value = 0.0f;
    }
}

// string value has been changed, do some things
static void change_string_value(cvar_t *var, const char *value, from_t from)
{
    // free the old value string
    Z_Free(var->string);

    var->string = Z_CvarCopyString(value);
    parse_string_value(var);

    if (var->flags & CVAR_USERINFO) {
        CL_UpdateUserinfo(var, from);
    }

    var->modified = true;
    if (from != FROM_CODE) {
        cvar_modified |= var->flags & CVAR_MODIFYMASK;
        var->flags |= CVAR_MODIFIED;
        if (from == FROM_MENU && !(var->flags & CVAR_NOARCHIVEMASK)) {
            var->flags |= CVAR_ARCHIVE;
        }
        if (var->changed) {
            var->changed(var);
        }
    }
}

static bool validate_info_cvar(const char *s)
{
    size_t len = Info_SubValidate(s);

    if (len == SIZE_MAX) {
        Com_Printf("Info cvars should not contain '\\', ';' or '\"' characters.\n");
        return false;
    }

    if (len >= MAX_QPATH) {
        Com_Printf("Info cvars should be less than 64 characters long.\n");
        return false;
    }

    return true;
}


// Cvar_Get has been called from subsystem initialization routine.
// check if the first instance of this cvar has been created by user,
// and if so, enforce any restrictions on the cvar value as defined by flags
static void get_engine_cvar(cvar_t *var, const char *var_value, int flags)
{
    if (var->flags & (CVAR_CUSTOM | CVAR_WEAK)) {
        // update default string if cvar was set from command line
        Z_Free(var->default_string);
        var->default_string = Z_CvarCopyString(var_value);

        // see if it was changed from it's default value
        if (strcmp(var_value, var->string)) {
            if ((flags & CVAR_ROM) ||
                ((flags & CVAR_NOSET) && com_initialized) ||
                ((flags & CVAR_CHEAT) && !CL_CheatsOK()) ||
                ((flags & CVAR_INFOMASK) && !validate_info_cvar(var->string)) ||
                (var->flags & CVAR_WEAK)) {
                // restricted cvars are reset back to default value
                change_string_value(var, var_value, FROM_CODE);
            } else {
                // normal cvars are just flagged as modified by user
                flags |= CVAR_MODIFIED;
            }
        }
    } else {
        flags &= ~CVAR_GAME;
    }

    // some flags are not saved
    var->flags &= ~(CVAR_GAME | CVAR_CUSTOM | CVAR_WEAK);
    var->flags |= flags;

    // clear archive flag if not compatible with other flags
    if (flags & CVAR_NOARCHIVEMASK)
        var->flags &= ~CVAR_ARCHIVE;
}

/*
============
Cvar_Get

If the non-volatile variable already exists, the value will not be set.
The flags will be or'ed in if the variable exists.
============
*/
cvar_t *Cvar_Get(const char *var_name, const char *var_value, int flags)
{
    cvar_t *var, *c, **p;
    unsigned hash;
    size_t length;

    Q_assert(var_name);

    if (!var_value) {
        return Cvar_FindVar(var_name);
    }

    if (flags & CVAR_INFOMASK) {
        if (!validate_info_cvar(var_name)) {
            return NULL;
        }
        if (!validate_info_cvar(var_value)) {
            return NULL;
        }
    }

    var = Cvar_FindVar(var_name);
    if (var) {
        if (!(flags & (CVAR_WEAK | CVAR_CUSTOM))) {
            get_engine_cvar(var, var_value, flags);
        }
        return var;
    }


    // create new variable
    length = strlen(var_name) + 1;
    var = Cvar_Malloc(sizeof(*var) + length);
    var->name = (char *)(var + 1);
    memcpy(var->name, var_name, length);
    var->string = Z_CvarCopyString(var_value);
    var->latched_string = NULL;
    var->default_string = Z_CvarCopyString(var_value);
    parse_string_value(var);
    var->flags = flags;
    var->changed = NULL;
    var->generator = Cvar_Default_g;
    var->modified = true;

    // sort the variable in
    for (c = cvar_vars, p = &cvar_vars; c; p = &c->next, c = c->next) {
        if (strcmp(var->name, c->name) < 0) {
            break;
        }
    }
    var->next = c;
    *p = var;

    // link the variable in
    hash = Com_HashString(var_name, CVARHASH_SIZE);
    var->hashNext = cvarHash[hash];
    cvarHash[hash] = var;

    return var;
}

/*
============
Cvar_WeakGet
============
*/
cvar_t *Cvar_WeakGet(const char *var_name)
{
    return Cvar_Get(var_name, "", CVAR_WEAK);
}

static void set_back_cvar(cvar_t *var)
{
    if (var->flags & CVAR_LATCH) {
        // set back to current value
        Z_Freep((void**)&var->latched_string);
    }
}

/*
============
Cvar_SetByVar
============
*/
void Cvar_SetByVar(cvar_t *var, const char *value, from_t from)
{
    if (!value) {
        value = "";
    }
    if (!strcmp(value, var->string)) {
        set_back_cvar(var);
        return; // not changed
    }

    if (var->flags & CVAR_INFOMASK) {
        if (!validate_info_cvar(value)) {
            return;
        }
    }

    // some cvars may not be changed by user at all
    if (from != FROM_CODE) {
        if (var->flags & CVAR_ROM) {
            Com_Printf("%s is read-only.\n", var->name);
            return;
        }

        if (var->flags & CVAR_CHEAT) {
            if (!CL_CheatsOK()) {
                Com_Printf("%s is cheat protected.\n", var->name);
                return;
            }
        }

        if ((var->flags & CVAR_NOSET) && com_initialized) {
            // FROM_CMDLINE while com_initialized == true means the second call
            // to Com_AddEarlyCommands() is changing the value, ignore.
            if (from != FROM_CMDLINE)
                Com_Printf("%s may be set from command line only.\n", var->name);
            return;
        }

        if (from == FROM_MENU && var == fs_game) {
            Com_WPrintf("Changing %s from menu is not allowed.\n", var->name);
            return;
        }

        if ((var->flags & CVAR_LATCH) && sv_running->integer) {
            if (var->latched_string && !strcmp(var->latched_string, value)) {
                return; // latched string not changed
            }
            Com_Printf("%s will be changed for next game.\n", var->name);
            Z_Free(var->latched_string);
            var->latched_string = Z_CvarCopyString(value);
            return;
        }
    }

    // free latched string, if any
    Z_Freep((void**)&var->latched_string);

    change_string_value(var, value, from);
}

/*
============
Cvar_SetEx
============
*/
cvar_t *Cvar_SetEx(const char *var_name, const char *value, from_t from)
{
    cvar_t    *var;

    var = Cvar_FindVar(var_name);
    if (!var) {
        // create it
        return Cvar_Get(var_name, value, CVAR_CUSTOM);
    }

    Cvar_SetByVar(var, value, from);

    return var;
}

/*
============
Cvar_FullSet
============
*/
cvar_t *Cvar_FullSet(const char *var_name, const char *value, int flags, from_t from)
{
    cvar_t *var;

    var = Cvar_FindVar(var_name);
    if (!var) {
        // create it
        return Cvar_Get(var_name, value, flags | CVAR_CUSTOM);
    }

    Cvar_SetByVar(var, value, from);

    // force retransmit of userinfo variables
    // needed for compatibility with q2admin
    if ((var->flags | flags) & CVAR_USERINFO) {
        CL_UpdateUserinfo(var, from);
    }

    var->flags &= ~CVAR_INFOMASK;
    var->flags |= flags;

    if (flags & CVAR_NOARCHIVEMASK)
        var->flags &= ~CVAR_ARCHIVE;

    return var;
}

/*
============
Cvar_Set
============
*/
cvar_t *Cvar_Set(const char *var_name, const char *value)
{
    return Cvar_SetEx(var_name, value, FROM_CODE);
}

/*
============
Cvar_UserSet
============
*/
cvar_t *Cvar_UserSet(const char *var_name, const char *value)
{
    return Cvar_SetEx(var_name, value, FROM_CONSOLE);
}


/*
============
Cvar_SetValue
============
*/
void Cvar_SetValue(cvar_t *var, float value, from_t from)
{
    char    val[32];

    if (var->value == value) {
        set_back_cvar(var);
        return; // not changed
    }

    if (value - floorf(value) < 1e-6f)
        Q_snprintf(val, sizeof(val), "%.f", value);
    else
        Q_snprintf(val, sizeof(val), "%f", value);

    Cvar_SetByVar(var, val, from);
}

/*
============
Cvar_SetInteger
============
*/
void Cvar_SetInteger(cvar_t *var, int value, from_t from)
{
    char    val[32];

    if (var->integer == value) {
        set_back_cvar(var);
        return; // not changed
    }

    Q_snprintf(val, sizeof(val), "%i", value);

    Cvar_SetByVar(var, val, from);
}

#if 0
/*
============
Cvar_SetHex
============
*/
void Cvar_SetHex(cvar_t *var, int value, from_t from)
{
    char    val[32];

    if (var->integer == value) {
        set_back_cvar(var);
        return; // not changed
    }

    Q_snprintf(val, sizeof(val), "0x%X", value);

    Cvar_SetByVar(var, val, from);
}
#endif

/*
============
Cvar_ClampInteger
============
*/
int Cvar_ClampInteger(cvar_t *var, int min, int max)
{
    if (var->integer < min) {
        Cvar_SetInteger(var, min, FROM_CODE);
        return min;
    }
    if (var->integer > max) {
        Cvar_SetInteger(var, max, FROM_CODE);
        return max;
    }
    return var->integer;
}

/*
============
Cvar_ClampValue
============
*/
float Cvar_ClampValue(cvar_t *var, float min, float max)
{
    if (var->value < min) {
        Cvar_SetValue(var, min, FROM_CODE);
        return min;
    }
    if (var->value > max) {
        Cvar_SetValue(var, max, FROM_CODE);
        return max;
    }
    return var->value;
}

/*
==================
Cvar_FixCheats
==================
*/
void Cvar_FixCheats(void)
{
    cvar_t *var;

    if (CL_CheatsOK()) {
        return;
    }

    // fix any cheating cvars
    for (var = cvar_vars; var; var = var->next) {
        if (var->flags & CVAR_CHEAT) {
            Cvar_SetByVar(var, var->default_string, FROM_CODE);
            if (var->changed)
                var->changed(var);
        }
    }
}

/*
============
Cvar_GetLatchedVars

Any variables with latched values will now be updated
============
*/
void Cvar_GetLatchedVars(void)
{
    cvar_t    *var;

    for (var = cvar_vars; var; var = var->next) {
        if (var->flags & CVAR_GAME)
            var->flags &= ~CVAR_SERVERINFO;
        if (!(var->flags & CVAR_LATCH))
            continue;
        if (!var->latched_string)
            continue;
        Z_Free(var->string);
        var->string = var->latched_string;
        var->latched_string = NULL;
        parse_string_value(var);
        var->modified = true;
        cvar_modified |= var->flags & CVAR_MODIFYMASK;
        if (var->changed) {
            var->changed(var);
        }
    }
}

int Cvar_CountLatchedVars(void)
{
    cvar_t  *var;
    int     total = 0;

    for (var = cvar_vars; var; var = var->next) {
        if (!(var->flags & CVAR_LATCH))
            continue;
        if (!var->latched_string)
            continue;
        total++;
    }

    return total;
}

/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/
void Cvar_Command(cvar_t *v)
{
// perform a variable print or set
    if (Cmd_Argc() < 2) {
        Com_Printf("\"%s\" is \"%s\"", v->name, v->string);
        if (strcmp(v->string, v->default_string)) {
            Com_Printf("  default: \"%s\"", v->default_string);
        }
        if (v->latched_string && strcmp(v->latched_string, v->string)) {
            Com_Printf("  latched: \"%s\"", v->latched_string);
        }
        Com_Printf("\n");
    } else {
        Cvar_SetByVar(v, Cmd_ArgsFrom(1), Cmd_From());
    }
}

static void Cvar_Set_c(genctx_t *ctx, int argnum)
{
    char *s;
    cvar_t *var;
    xgenerator_t g;

    if (argnum == 1) {
        Cvar_Variable_g(ctx);
    } else if (argnum == 2) {
        s = Cmd_Argv(ctx->argnum - 1);
        if ((var = Cvar_FindVar(s)) != NULL) {
            g = var->generator;
            if (g) {
                ctx->data = var;
                g(ctx);
            }
        }
    }
}


/*
============
Cvar_Set_f

Allows setting and defining of arbitrary cvars from console
============
*/
void Cvar_Set_f(void)
{
    int     c, flags;
    char    *f;

    c = Cmd_Argc();
    if (c < 3) {
        Com_Printf("Usage: set <variable> <value> [u / s]\n");
        return;
    }

    if (c == 3) {
        Cvar_SetEx(Cmd_Argv(1), Cmd_Argv(2), Cmd_From());
        return;
    }

    if (c == 4) {
        f = Cmd_Argv(3);
        if (!strcmp(f, "u")) {
            flags = CVAR_USERINFO;
        } else if (!strcmp(f, "s")) {
            flags = CVAR_SERVERINFO;
        } else {
            goto set;
        }
        Cvar_FullSet(Cmd_Argv(1), Cmd_Argv(2), flags, Cmd_From());
        return;
    }

set:
    Cvar_SetEx(Cmd_Argv(1), Cmd_ArgsFrom(2), Cmd_From());
}

/*
============
Cvar_SetFlag_f

Allows setting and defining of arbitrary cvars from console
============
*/
static void Cvar_SetFlag_f(void)
{
    char    *s = Cmd_Argv(0);
    int     flags;

    if (Cmd_Argc() < 3) {
        Com_Printf("Usage: %s <variable> <value>\n", s);
        return;
    }

    if (!strcmp(s, "seta")) {
        cvar_t *var = Cvar_SetEx(Cmd_Argv(1), Cmd_ArgsFrom(2), Cmd_From());
        if (var && !(var->flags & CVAR_NOARCHIVEMASK))
            var->flags |= CVAR_ARCHIVE;
        return;
    }

    if (!strcmp(s, "setu")) {
        flags = CVAR_USERINFO;
    } else if (!strcmp(s, "sets")) {
        flags = CVAR_SERVERINFO;
    } else {
        return;
    }

    Cvar_FullSet(Cmd_Argv(1), Cmd_ArgsFrom(2), flags, Cmd_From());
}

#if USE_CLIENT

/*
============
Cvar_WriteVariables

Appends lines containing "set variable value" for all variables
with the archive flag set to true.
============
*/
void Cvar_WriteVariables(qhandle_t f, int mask, bool modified)
{
    cvar_t  *var;
    const char  *s, *a;

    for (var = cvar_vars; var; var = var->next) {
        if (var->flags & CVAR_NOARCHIVEMASK)
            continue;
        if (!(var->flags & mask))
            continue;

        s = var->latched_string ? var->latched_string : var->string;
        if (modified && !strcmp(s, var->default_string))
            continue;

        a = !modified && (var->flags & CVAR_ARCHIVE) ? "a" : "";
        FS_FPrintf(f, "set%s %s \"%s\"\n", a, var->name, s);
    }
}

#endif

/*
============
Cvar_List_f

============
*/

static const cmd_option_t o_cvarlist[] = {
    { "a", "archive", "list archived cvars" },
    { "c", "cheat", "list cheat protected cvars" },
    { "h", "help", "display this help message" },
    { "l", "latched", "list latched cvars" },
    { "m", "modified", "list modified cvars" },
    { "n", "noset", "list command line cvars" },
    { "r", "rom", "list read-only cvars" },
    { "s", "serverinfo", "list serverinfo cvars" },
    { "t", "custom", "list user-created cvars" },
    { "u", "userinfo", "list userinfo cvars" },
    { "v", "verbose", "display flags of each cvar" },
    { NULL }
};

static void Cvar_List_c(genctx_t *ctx, int argnum)
{
    Cmd_Option_c(o_cvarlist, NULL, ctx, argnum);
}

static void Cvar_List_f(void)
{
    cvar_t    *var;
    int        i, total;
    bool verbose = false, modified = false, latched = false;
    int mask = 0;
    char *wildcard = NULL;
    char buffer[5];
    int c;

    while ((c = Cmd_ParseOptions(o_cvarlist)) != -1) {
        switch (c) {
        case 'a':
            mask |= CVAR_ARCHIVE;
            break;
        case 'c':
            mask |= CVAR_CHEAT;
            break;
        case 'h':
            Cmd_PrintUsage(o_cvarlist, "[wildcard]");
            Com_Printf("List registered console variables.\n");
            Cmd_PrintHelp(o_cvarlist);
            Com_Printf(
                "Flags legend:\n"
                "C: cheat protected\n"
                "A: archived in config file\n"
                "U: included in userinfo\n"
                "S: included in serverinfo\n"
                "N: set from command line only\n"
                "R: read-only variable\n"
                "L: latched\n"
                "?: created by user\n");
            return;
        case 'l':
            latched = true;
            break;
        case 'm':
            modified = true;
            break;
        case 'n':
            mask |= CVAR_NOSET;
            break;
        case 'r':
            mask |= CVAR_ROM;
            break;
        case 's':
            mask |= CVAR_SERVERINFO;
            break;
        case 't':
            mask |= CVAR_CUSTOM | CVAR_WEAK;
            break;
        case 'u':
            mask |= CVAR_USERINFO;
            break;
        case 'v':
            verbose = true;
            break;
        default:
            return;
        }
    }
    if (cmd_optind < Cmd_Argc())
        wildcard = Cmd_Argv(cmd_optind);

    buffer[sizeof(buffer) - 1] = 0;
    i = 0;
    for (var = cvar_vars, total = 0; var; var = var->next, total++) {
        if (latched && !var->latched_string) {
            continue;
        }
        if (mask && !(var->flags & mask)) {
            continue;
        }
        if (wildcard && !Com_WildCmp(wildcard, var->name)) {
            continue;
        }
        if (modified && (!strcmp(var->latched_string ? var->latched_string :
                                 var->string, var->default_string) || (var->flags & CVAR_ROM))) {
            continue;
        }

        if (verbose) {
            memset(buffer, '-', sizeof(buffer) - 1);

            if (var->flags & CVAR_CHEAT)
                buffer[0] = 'C';
            else if (var->flags & CVAR_ARCHIVE)
                buffer[0] = 'A';

            if (var->flags & CVAR_USERINFO)
                buffer[1] = 'U';

            if (var->flags & CVAR_SERVERINFO)
                buffer[2] = 'S';

            if (var->flags & CVAR_ROM)
                buffer[3] = 'R';
            else if (var->flags & CVAR_NOSET)
                buffer[3] = 'N';
            else if (var->flags & CVAR_LATCH)
                buffer[3] = 'L';
            else if (var->flags & (CVAR_CUSTOM | CVAR_WEAK))
                buffer[3] = '?';

            Com_Printf("%s ", buffer);
        }

        Com_Printf("%s \"%s\"\n", var->name, var->string);

        i++;
    }
    Com_Printf("%i of %i cvars\n", i, total);
}

/*
============
Cvar_Toggle_f
============
*/
static void Cvar_Toggle_f(void)
{
    cvar_t *var;
    int i, argc = Cmd_Argc();

    if (argc < 2) {
        Com_Printf("Usage: %s <variable> [values]\n", Cmd_Argv(0));
        return;
    }

    var = Cvar_FindVar(Cmd_Argv(1));
    if (!var) {
        Com_Printf("%s is not a variable\n", Cmd_Argv(1));
        return;
    }

    if (argc < 3) {
        if (!strcmp(var->string, "0")) {
            Cvar_SetByVar(var, "1", Cmd_From());
        } else if (!strcmp(var->string, "1")) {
            Cvar_SetByVar(var, "0", Cmd_From());
        } else {
            Com_Printf("\"%s\" is \"%s\", can't toggle\n",
                       var->name, var->string);
        }
        return;
    }

    for (i = 0; i < argc - 2; i++) {
        if (!Q_stricmp(var->string, Cmd_Argv(2 + i))) {
            i = (i + 1) % (argc - 2);
            Cvar_SetByVar(var, Cmd_Argv(2 + i), Cmd_From());
            return;
        }
    }

    Com_Printf("\"%s\" is \"%s\", can't cycle\n", var->name, var->string);
}

static void Cvar_Toggle_c(genctx_t *ctx, int argnum)
{
    char *s;
    xgenerator_t g;

    if (argnum == 1) {
        Cvar_Variable_g(ctx);
    } else {
        s = Cmd_Argv(ctx->argnum - argnum + 1);
        if ((g = Cvar_FindGenerator(s)) != NULL) {
            g(ctx);
        }
    }
}

/*
============
Cvar_Inc_f
============
*/
static void Cvar_Inc_f(void)
{
    cvar_t *var;
    float value;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <variable> [value]\n", Cmd_Argv(0));
        return;
    }

    var = Cvar_FindVar(Cmd_Argv(1));
    if (!var) {
        Com_Printf("%s is not a variable\n", Cmd_Argv(1));
        return;
    }

    if (!COM_IsFloat(var->string)) {
        Com_Printf("\"%s\" is \"%s\", can't %s\n",
                   var->name, var->string, Cmd_Argv(0));
        return;
    }

    value = 1;
    if (Cmd_Argc() > 2) {
        value = Q_atof(Cmd_Argv(2));
    }
    if (!strcmp(Cmd_Argv(0), "dec")) {
        value = -value;
    }
    Cvar_SetValue(var, var->value + value, Cmd_From());
}

static void Cvar_Inc_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        Cvar_Variable_g(ctx);
    }
}

/*
============
Cvar_Reset_f
============
*/
static void Cvar_Reset_f(void)
{
    cvar_t *var;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <variable>\n", Cmd_Argv(0));
        return;
    }

    var = Cvar_FindVar(Cmd_Argv(1));
    if (!var) {
        Com_Printf("%s is not a variable\n", Cmd_Argv(1));
        return;
    }

    Cvar_SetByVar(var, var->default_string, Cmd_From());
}

static void Cvar_Reset_c(genctx_t *ctx, int argnum)
{
    cvar_t *var;

    if (argnum == 1)
        for (var = cvar_vars; var; var = var->next)
            if (strcmp(var->latched_string ? var->latched_string : var->string, var->default_string))
                Prompt_AddMatch(ctx, var->name);
}

static void Cvar_ResetAll_f(void)
{
    cvar_t *var;

    for (var = cvar_vars; var; var = var->next) {
        if (var->flags & CVAR_ROM)
            continue;
        if ((var->flags & CVAR_NOSET) && com_initialized)
            continue;
        if (var == fs_game)
            continue;
        Cvar_SetByVar(var, var->default_string, Cmd_From());
    }
}

size_t Cvar_BitInfo(char *info, int bit)
{
    char    newi[MAX_INFO_STRING], *v;
    cvar_t  *var;
    size_t  len, total = 0;
    int     c;

    for (var = cvar_vars; var; var = var->next) {
        if (!(var->flags & bit)) {
            continue;
        }
        if (var->flags & CVAR_PRIVATE) {
            continue;
        }
        if (!var->string[0]) {
            continue;
        }
        len = Q_concat(newi, sizeof(newi), "\\", var->name, "\\", var->string);
        if (len >= sizeof(newi)) {
            continue;
        }
        if (total + len >= MAX_INFO_STRING) {
            break;
        }

        // only copy ascii values
        v = newi;
        while (*v) {
            c = *v++;
            c &= 127;        // strip high bits
            if (Q_isprint(c))
                info[total++] = c;
        }
    }

    info[total] = 0;
    return total;
}

static const cmdreg_t c_cvar[] = {
    { "set", Cvar_Set_f, Cvar_Set_c },
    { "setu", Cvar_SetFlag_f, Cvar_Set_c },
    { "sets", Cvar_SetFlag_f, Cvar_Set_c },
    { "seta", Cvar_SetFlag_f, Cvar_Set_c },
    { "cvarlist", Cvar_List_f, Cvar_List_c },
    { "toggle", Cvar_Toggle_f, Cvar_Toggle_c },
    { "inc", Cvar_Inc_f, Cvar_Inc_c },
    { "dec", Cvar_Inc_f, Cvar_Inc_c },
    { "reset", Cvar_Reset_f, Cvar_Reset_c },
    { "resetall", Cvar_ResetAll_f },

    { NULL }
};

/*
============
Cvar_Init
============
*/
void Cvar_Init(void)
{
    Cmd_Register(c_cvar);
}

