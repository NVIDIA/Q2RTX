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

#ifndef CVAR_H
#define CVAR_H

#include "common/cmd.h"

/*
cvar_t variables are used to hold scalar or string variables that can be
changed or displayed at the console or prog code as well as accessed directly
in C code.

The user can access cvars from the console in three ways:
r_draworder            prints the current value
r_draworder 0        sets the current value to 0
set r_draworder 0    as above, but creates the cvar if not present
Cvars are restricted from having the same names as commands to keep this
interface from being ambiguous.
*/

#define CVAR_CHEAT          (1 << 5)  // can't be changed when connected
#define CVAR_PRIVATE        (1 << 6)  // never macro expanded or saved to config
#define CVAR_ROM            (1 << 7)  // can't be changed even from cmdline
#define CVAR_MODIFIED       (1 << 8)  // modified by user
#define CVAR_CUSTOM         (1 << 9)  // created by user
#define CVAR_WEAK           (1 << 10) // doesn't have value
#define CVAR_GAME           (1 << 11) // created by game library
#define CVAR_FILES          (1 << 13) // r_reload when changed
#define CVAR_REFRESH        (1 << 14) // vid_restart when changed
#define CVAR_SOUND          (1 << 15) // snd_restart when changed

#define CVAR_INFOMASK       (CVAR_USERINFO | CVAR_SERVERINFO)
#define CVAR_MODIFYMASK     (CVAR_INFOMASK | CVAR_FILES | CVAR_REFRESH | CVAR_SOUND)
#define CVAR_NOARCHIVEMASK  (CVAR_NOSET | CVAR_CHEAT | CVAR_PRIVATE | CVAR_ROM)
#define CVAR_EXTENDED_MASK  (~31)

extern cvar_t   *cvar_vars;
extern int      cvar_modified;

void Cvar_Init(void);

void Cvar_Variable_g(genctx_t *ctx);
void Cvar_Default_g(genctx_t *ctx);
// attempts to match a partial variable name for command line completion
// returns NULL if nothing fits

int Cvar_CountLatchedVars(void);
void Cvar_GetLatchedVars(void);
// any CVAR_LATCHED variables that have been set will now take effect

void Cvar_FixCheats(void);
// resets all cheating cvars to default

void Cvar_Command(cvar_t *v);
// called by Cmd_ExecuteString when Cmd_Argv(0) doesn't match a known
// command.  Returns true if the command was a variable reference that
// was handled. (print or change)

void Cvar_WriteVariables(qhandle_t f, int mask, bool modified);
// appends lines containing "set variable value" for all variables
// with matching flags

size_t Cvar_BitInfo(char *info, int bit);

cvar_t *Cvar_FindVar(const char *var_name);
xgenerator_t Cvar_FindGenerator(const char *var_name);
bool Cvar_Exists(const char *name, bool weak);

cvar_t *Cvar_Get(const char *var_name, const char *value, int flags);
// creates the variable if it doesn't exist, or returns the existing one
// if it exists, the value will not be changed, but flags will be ORed in
// that allows variables to be unarchived without needing bitflags

cvar_t *Cvar_WeakGet(const char *var_name);
// creates weak variable without value

void Cvar_SetByVar(cvar_t *var, const char *value, from_t from);
// set by cvar pointer

cvar_t *Cvar_SetEx(const char *var_name, const char *value, from_t from);
// will create the variable if it doesn't exist
cvar_t *Cvar_Set(const char *var_name, const char *value);
// will set the variable even if NOSET or LATCH
cvar_t *Cvar_UserSet(const char *var_name, const char *value);
cvar_t *Cvar_FullSet(const char *var_name, const char *value,
                     int flags, from_t from);

#define Cvar_Reset(x) \
    Cvar_SetByVar(x, (x)->default_string, FROM_CODE)

void Cvar_SetValue(cvar_t *var, float value, from_t from);
void Cvar_SetInteger(cvar_t *var, int value, from_t from);
//void Cvar_SetHex(cvar_t *var, int value, from_t from);
// expands value to a string and calls Cvar_Set

int Cvar_ClampInteger(cvar_t *var, int min, int max);
float Cvar_ClampValue(cvar_t *var, float min, float max);

float Cvar_VariableValue(const char *var_name);
int Cvar_VariableInteger(const char *var_name);
// returns 0 if not defined or non numeric

char *Cvar_VariableString(const char *var_name);
// returns an empty string if not defined

#define Cvar_VariableStringBuffer(name, buffer, size) \
    Q_strlcpy(buffer, Cvar_VariableString(name), size)

void Cvar_Set_f(void);

#endif // CVAR_H
