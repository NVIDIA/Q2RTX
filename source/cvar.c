/*
Copyright (C) 1997-2001 Id Software, Inc.

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
// cvar.c -- dynamic variable tracking

#include "com_local.h"

cvarAPI_t	cvar;

cvar_t	*cvar_vars;

int	    cvar_latchedModified;
int	    cvar_infoModified;

#define Cvar_Malloc( size )		Z_TagMalloc( size, TAG_CVAR )

#define CVARHASH_SIZE	256

static cvar_t *cvarHash[CVARHASH_SIZE];

static cvar_t	*cvar_silent;

/*
============
Cvar_FindVar
============
*/
cvar_t *Cvar_FindVar( const char *var_name ) {
	cvar_t	*var;
	int hash;

	hash = Com_HashString( var_name, CVARHASH_SIZE );
	
	for( var = cvarHash[hash]; var; var = var->hashNext ) {
		if( !strcmp( var_name, var->name ) ) {
			return var;
		}
	}

	return NULL;
}

xgenerator_t Cvar_FindGenerator( const char *var_name ) {
	cvar_t *var = Cvar_FindVar( var_name );

    return var ? var->generator : NULL;
}

/*
============
Cvar_Exists
============
*/
qboolean Cvar_Exists( const char *name ) {
	if( Cvar_FindVar( name ) ) {
		return qtrue;
	}
	return qfalse;
}

/*
============
Cvar_VariableValue
============
*/
float Cvar_VariableValue( const char *var_name ) {
	cvar_t	*var;
	
	var = Cvar_FindVar( var_name );
	if( !var )
		return 0;

	return var->value;
}

/*
============
Cvar_VariableInteger
============
*/
int Cvar_VariableInteger( const char *var_name ) {
	cvar_t	*var;
	
	var = Cvar_FindVar( var_name );
	if( !var )
		return 0;

	return var->integer;
}


/*
============
Cvar_VariableString
============
*/
char *Cvar_VariableString( const char *var_name ) {
	cvar_t *var;
	
	var = Cvar_FindVar( var_name );
	if( !var )
		return "";

	return var->string;
}

/*
============
Cvar_VariableStringBuffer
============
*/
void Cvar_VariableStringBuffer( const char *var_name, char *buffer, int size ) {
	Q_strncpyz( buffer, Cvar_VariableString( var_name ), size );
}

void Cvar_Variable_g( genctx_t *ctx ) {
    cvar_t *c;
    
    for( c = cvar_vars; c; c = c->next ) {
        if( !Prompt_AddMatch( ctx, c->name ) ) {
            break;
        }
    }
}

void Cvar_Default_g( genctx_t *ctx ) {
    cvar_t *c = ctx->data;

    if( c ) {
        if( strcmp( c->string, c->default_string ) ) {
            Prompt_AddMatch( ctx, c->string );
        }
        Prompt_AddMatch( ctx, c->default_string );
    }
}

static void Cvar_ParseString( cvar_t *var ) {
	char *s = var->string;
	
	if( s[0] == '0' && s[1] == 'x' ) {
		var->integer = COM_ParseHex( s + 2 );
		var->value = ( float )var->integer;
	} else {
		var->integer = atoi( var->string );
		var->value = atof( var->string );
	}
}


/*
============
Cvar_Get

If the variable already exists, the value will not be set
The flags will be or'ed in if the variable exists.
============
*/
cvar_t *Cvar_Get( const char *var_name, const char *var_value, int flags ) {
	cvar_t	*var, *c, **p;
	unsigned hash;
	int length;

	if( !var_name ) {
		Com_Error( ERR_FATAL, "Cvar_Get: NULL var_name" );
	}
	if( !var_value ) {
		var_value = "";
	}
	
	if( flags & CVAR_INFOMASK ) {
		if( Info_SubValidate( var_name ) == -1 ) {
			Com_WPrintf( "Invalid info cvar name '%s'.\n", var_name );
			return NULL;
		}
		if( Info_SubValidate( var_value ) == -1 ) {
			Com_WPrintf( "Invalid info cvar value '%s' for '%s'.\n", var_value, var_name );
			return NULL;
		}
	}

	var = Cvar_FindVar( var_name );
	if( var ) {
		if( ( var->flags & CVAR_LATCHED ) && var->latched_string ) {
			if( strcmp( var->latched_string, var->string ) ) {
				// update latched cvar
				Z_Free( var->string );
				var->string = Cvar_CopyString( var->latched_string );
				Cvar_ParseString( var );

				if( var->flags & CVAR_USERINFO ) {
					CL_UpdateUserinfo( var, CVAR_SET_DIRECT );
				}
				
				var->modified = qtrue;				
			}

			Z_Free( var->latched_string );
			var->latched_string = NULL;
		}

		if( !( flags & CVAR_USER_CREATED ) ) {
			if( var->flags & CVAR_USER_CREATED ) {
				// update default string if cvar was set from command line
				Z_Free( var->default_string );
				var->default_string = Cvar_CopyString( var_value );
				var->flags &= ~CVAR_USER_CREATED;
			} else if( !( var->flags & CVAR_DEFAULTS_MIXED ) ) {
				if( strcmp( var->default_string, var_value ) ) {
					Com_DPrintf( "Cvar '%s' given initial values: '%s' and '%s'\n",
                        var->name, var->default_string, var_value );
					var->flags |= CVAR_DEFAULTS_MIXED;
				}
			}
#if 1
            if( ( var->flags & CVAR_LATCHED ) && !( flags & CVAR_LATCHED ) ) {
                if( var->latched_string ) {
                    Z_Free( var->latched_string );
                    var->latched_string = NULL;
                }
                var->flags &= ~CVAR_LATCHED;
            }
#endif
		} else {
			flags &= ~CVAR_USER_CREATED;
		}

		var->flags |= flags & ~CVAR_GAME;
		return var;
	}

	// once allocated, cvar name is never changed
	length = strlen( var_name ) + 1;
	var = Cvar_Malloc( sizeof( *var ) + length );
	var->name = ( char * )( var + 1 );
	memcpy( var->name, var_name, length );
	var->string = Cvar_CopyString( var_value );
    var->latched_string = NULL;
	var->default_string = Cvar_CopyString( var_value );
	Cvar_ParseString( var );
	var->flags = flags;
    var->changed = NULL;
    var->generator = Cvar_Default_g;
	var->modified = qtrue;

	// sort the variable in
    for( c = cvar_vars, p = &cvar_vars; c; p = &c->next, c = c->next ) {
        if( strcmp( var->name, c->name ) < 0 ) {
            break;
        }
    }
	var->next = c;
	*p = var;

	// link the variable in
	hash = Com_HashString( var_name, CVARHASH_SIZE );
	var->hashNext = cvarHash[hash];
	cvarHash[hash] = var;

	return var;
}

/*
============
Cvar_SetByVar
============
*/
void Cvar_SetByVar( cvar_t *var, const char *value, cvarSetSource_t source ) {
	if( !strcmp( value, var->string ) &&
        !( var->flags & (CVAR_LATCHED|CVAR_LATCH) ) )
    {
		return;		// not changed
    }

	if( var->flags & CVAR_INFOMASK ) {
		if( Info_SubValidate( value ) == -1 ) {
			Com_WPrintf( "Invalid info cvar value '%s' for '%s'.\n", value, var->name );
			return;
		}
	}

	// some cvars may not be changed by user at all
	if( source != CVAR_SET_DIRECT ) {
		if( var->flags & CVAR_ROM ) {
			Com_Printf( "%s is read-only.\n", var->name );
			return;
		}

		if( var->flags & CVAR_CHEAT ) {
			if( !CL_CheatsOK() ) {
				Com_Printf( "%s is cheat protected.\n", var->name );
				return;
			}
		}
	}

	// some cvars may require special processing if set by user from console
	if( source == CVAR_SET_CONSOLE && com_initialized ) {
		if( var->flags & CVAR_NOSET ) {
			Com_Printf( "%s may be set from command line only.\n", var->name );
			return;
		}

		if( var->flags & (CVAR_LATCHED|CVAR_LATCH) ) {
			if( !strcmp( var->string, value ) ) {
				// set back to current value
				if( var->latched_string ) {
					Z_Free( var->latched_string );
					var->latched_string = NULL;
				}
				return;
			}
			if( var->latched_string ) {
				if( !strcmp( var->latched_string, value ) ) {
					return; // latched string not changed
				}
				Z_Free( var->latched_string );
				var->latched_string = NULL;
			}


			if( var->flags & CVAR_LATCH ) {
				if( sv_running->integer ) {
					if( !cvar_silent->integer ) {
						Com_Printf( "%s will be changed for next game.\n",
                            var->name );
					}
					var->latched_string = Cvar_CopyString( value );
					return;
				}
				// server is down, it's ok to update this cvar now
			} else {
				//char *subsystem;

				var->latched_string = Cvar_CopyString( value );
				//cvar_latchedModified |= 1 << var->subsystem;
				if( cvar_silent->integer ) {
					return;
				}

#if 0
				switch( var->subsystem ) {
				case CVAR_SYSTEM_GENERIC:
					subsystem = "desired subsystem";
					break;
				case CVAR_SYSTEM_VIDEO:
					subsystem = "video subsystem";
					break;
				case CVAR_SYSTEM_SOUND:
					subsystem = "sound subsystem";
					break;
				case CVAR_SYSTEM_INPUT:
					subsystem = "input subsystem";
					break;
				case CVAR_SYSTEM_NET:
					subsystem = "network subsystem";
					break;
				case CVAR_SYSTEM_FILES:
					subsystem = "filesystem";
					break;
				default:
					Com_Error( ERR_FATAL, "Cvar_SetByVar: invalid subsystem %u",
                            var->subsystem );
					subsystem = NULL;
					break;
				}
				Com_Printf( "%s will be changed upon restarting the %s.\n",
					var->name, subsystem );
#else
				Com_Printf( "%s will be changed upon restarting.\n", var->name );
#endif
				return;
			}
		}

	}

	// free latched string, if any
	if( var->latched_string ) {
		Z_Free( var->latched_string );
		var->latched_string = NULL;
	}
	
	Z_Free( var->string );	// free the old value string
	
	var->string = Cvar_CopyString( value );
	Cvar_ParseString( var );

	if( var->flags & CVAR_INFOMASK ) {
		cvar_infoModified |= var->flags & CVAR_INFOMASK;
		if( var->flags & CVAR_USERINFO ) {
			CL_UpdateUserinfo( var, source );
		}
	}

	var->modified = qtrue;
	if( source != CVAR_SET_DIRECT && var->changed ) {
		var->changed( var );
	}
}

/*
============
Cvar_SetEx
============
*/
cvar_t *Cvar_SetEx( const char *var_name, const char *value,
        cvarSetSource_t source )
{
	cvar_t	*var;

	var = Cvar_FindVar( var_name );
	if( !var ) {	
		// create it
		return Cvar_Get( var_name, value, CVAR_USER_CREATED );
	}

	Cvar_SetByVar( var, value, source );

	return var;
}

/*
============
Cvar_FullSet
============
*/
cvar_t *Cvar_FullSet( const char *var_name, const char *value, int flags, cvarSetSource_t source ) {
	cvar_t *var;

	var = Cvar_FindVar( var_name );
	if( !var ) {	
		// create it
		return Cvar_Get( var_name, value, flags | CVAR_USER_CREATED );
	}

	Cvar_SetByVar( var, value, source );

	// force retransmit of userinfo variables
	// needed for compatibility with q2admin
	if( ( var->flags | flags ) & CVAR_USERINFO ) {
		cvar_infoModified |= CVAR_USERINFO;
		CL_UpdateUserinfo( var, source );
	}

	var->flags &= ~CVAR_INFOMASK;
	var->flags |= flags;

	return var;
}

/*
============
Cvar_Set
============
*/
cvar_t *Cvar_Set( const char *var_name, const char *value ) {
	return Cvar_SetEx( var_name, value, CVAR_SET_DIRECT );
}

/*
============
Cvar_UserSet
============
*/
cvar_t *Cvar_UserSet( const char *var_name, const char *value ) {
	return Cvar_SetEx( var_name, value, CVAR_SET_CONSOLE );
}


/*
============
Cvar_SetValue
============
*/
void Cvar_SetValue( const char *var_name, float value ) {
	char	val[32];

	if( value == (int)value )
		Com_sprintf( val, sizeof( val ), "%i", (int)value );
	else
		Com_sprintf( val, sizeof( val ), "%f", value);

	Cvar_SetEx( var_name, val, CVAR_SET_DIRECT );
}

/*
============
Cvar_SetInteger
============
*/
void Cvar_SetInteger( const char *var_name, int value ) {
	char	val[32];

	Com_sprintf( val, sizeof( val ), "%i", value );

	Cvar_SetEx( var_name, val, CVAR_SET_DIRECT );
}

/*
============
Cvar_SetInteger
============
*/
void Cvar_SetIntegerHex( const char *var_name, unsigned value ) {
	char	val[32];

	Com_sprintf( val, sizeof( val ), "0x%X", value );

	Cvar_SetEx( var_name, val, CVAR_SET_DIRECT );
}

/*
============
Cvar_ClampInteger
============
*/
void Cvar_ClampInteger( cvar_t *var, int min, int max ) {
	char	val[32];

	if( var->integer < min ) {
		Com_sprintf( val, sizeof( val ), "%i", min );
		Cvar_SetByVar( var, val, CVAR_SET_DIRECT );
	} else if( var->integer > max ) {
		Com_sprintf( val, sizeof( val ), "%i", max );
		Cvar_SetByVar( var, val, CVAR_SET_DIRECT );
	}
}

/*
============
Cvar_ClampValue
============
*/
void Cvar_ClampValue( cvar_t *var, float min, float max ) {
	char	val[32];

	if( var->value < min ) {
		if( min == (int)min ) {
			Com_sprintf( val, sizeof( val ), "%i", (int)min );
		} else {
			Com_sprintf( val, sizeof( val ), "%f", min );
		}
		Cvar_SetByVar( var, val, CVAR_SET_DIRECT );
	} else if( var->value > max ) {
		if( max == (int)max ) {
			Com_sprintf( val, sizeof( val ), "%i", (int)max );
		} else {
			Com_sprintf( val, sizeof( val ), "%f", max );
		}
		Cvar_SetByVar( var, val, CVAR_SET_DIRECT );
	}
}

/*
==================
Cvar_FixCheats
==================
*/
void Cvar_FixCheats( void ) {
   cvar_t *var;

    if ( CL_CheatsOK() ) {
        return;
    }

    // fix any cheating cvars
    for( var = cvar_vars ; var ; var = var->next ) {
		if( var->flags & CVAR_CHEAT ) {
			Cvar_SetByVar( var, var->default_string, CVAR_SET_DIRECT );
		}
    }
}

/*
============
Cvar_GetLatchedVars

Any variables with latched values will now be updated
Only used for game latched cvars now
============
*/
void Cvar_GetLatchedVars( void ) {
	cvar_t	*var;

	for( var = cvar_vars; var; var = var->next ) {
        if( var->flags & CVAR_GAME )
            var->flags &= ~CVAR_SERVERINFO;
		if( !(var->flags & CVAR_LATCH) )
			continue;
		if( var->flags & CVAR_LATCHED )
			continue; // don't update engine cvars
		if( !var->latched_string )
			continue;
		Z_Free( var->string );
		var->string = var->latched_string;
		var->latched_string = NULL;
		Cvar_ParseString( var );
		
		var->modified = qtrue;
	}
}

/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/
void Cvar_Command( cvar_t *v ) {
// perform a variable print or set
	if( Cmd_Argc() < 2 ) {
        Com_Printf( "\"%s\" is \"%s\"", v->name, v->string );
        if( strcmp( v->string, v->default_string ) ) {
            Com_Printf( "  default: \"%s\"", v->default_string );
        }
        if( v->latched_string && strcmp( v->latched_string, v->string ) ) {
            Com_Printf( "  latched: \"%s\"", v->latched_string );
        }
        Com_Printf( "\n" );
	} else {
    	Cvar_SetByVar( v, Cmd_ArgsFrom( 1 ), CVAR_SET_CONSOLE );
    }
}

static void Cvar_Set_c( genctx_t *ctx, int argnum ) {
    char *s;
    xgenerator_t g;

    if( argnum == 1 ) {
        Cvar_Variable_g( ctx );
    } else if( argnum == 2 ) {
        s = Cmd_Argv( ctx->argnum - 1 );
        if( ( g = Cvar_FindGenerator( s ) ) != NULL ) {
            g( ctx );
        }
    }
}


/*
============
Cvar_Set_f

Allows setting and defining of arbitrary cvars from console
============
*/
void Cvar_Set_f( void ) {
	int		c, flags;
	char	*f;

	c = Cmd_Argc();
	if( c < 3 ) {
		Com_Printf( "Usage: set <variable> <value> [u / s]\n" );
		return;
	}

    if( c == 3 ) {
        Cvar_SetEx( Cmd_Argv( 1 ), Cmd_Argv( 2 ), CVAR_SET_CONSOLE );
        return;
    }

    if( c == 4 ) {
        f = Cmd_Argv( 3 );
        if( !strcmp( f, "u" ) ) {
            flags = CVAR_USERINFO;
        } else if( !strcmp( f, "s" ) ) {
            flags = CVAR_SERVERINFO;
        } else {
            goto set;
        }
    	Cvar_FullSet( Cmd_Argv( 1 ), Cmd_Argv( 2 ), flags, CVAR_SET_CONSOLE );
        return;
    }

set:
    Cvar_SetEx( Cmd_Argv( 1 ), Cmd_ArgsFrom( 2 ), CVAR_SET_CONSOLE );
}

/*
============
Cvar_SetFlag_f

Allows setting and defining of arbitrary cvars from console
============
*/
static void Cvar_SetFlag_f( void ) {
	char	*s = Cmd_Argv( 0 );
	int		flags;

	if( Cmd_Argc() < 3 ) {
		Com_Printf( "Usage: %s <variable> <value>\n", s );
		return;
	}

	if( !Q_stricmp( s, "setu" ) ) {
		flags = CVAR_USERINFO;
	} else if( !Q_stricmp( s, "sets" ) ) {
		flags = CVAR_SERVERINFO;
	} else if( !Q_stricmp( s, "seta" ) ) {
		flags = CVAR_ARCHIVE;
	} else {
		return;
	}

	Cvar_FullSet( Cmd_Argv( 1 ), Cmd_ArgsFrom( 2 ), flags, CVAR_SET_CONSOLE );
}

#ifndef DEDICATED_ONLY

/*
============
Cvar_WriteVariables

Appends lines containing "set variable value" for all variables
with the archive flag set to true.
============
*/
void Cvar_WriteVariables( fileHandle_t f, int mask, qboolean modified ) {
	cvar_t	*var;
    char *string;

	for( var = cvar_vars; var; var = var->next ) {
        if( var->flags & CVAR_PRIVATE ) {
            continue;
        }
		if( var->flags & mask ) {
            if( ( var->flags & CVAR_LATCHED ) && var->latched_string ) {
                string = var->latched_string;
            } else {
                string = var->string;
            }
            if( !modified || strcmp( string, var->default_string ) ) {
    			FS_FPrintf( f, "set %s \"%s\"\n", var->name, string );
            }
		}
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
    { "w:string", "wildcard", "list cvars matching wildcard" },
    { NULL }
};

static void Cvar_List_c( genctx_t *ctx, int argnum ) {
    Cmd_Option_c( o_cvarlist, NULL, ctx, argnum );
}

static void Cvar_List_f( void ) {
	cvar_t	*var;
	int		i, total;
    qboolean verbose = qfalse, modified = qfalse, latched = qfalse;
    int mask = 0;
	char *wildcard = NULL;
	char buffer[6];
    int c;

    while( ( c = Cmd_ParseOptions( o_cvarlist ) ) != -1 ) {
        switch( c ) {
        case 'a':
            mask |= CVAR_ARCHIVE;
            break;
        case 'c':
            mask |= CVAR_CHEAT;
            break;
        case 'h':
            Cmd_PrintUsage( o_cvarlist, NULL );
            Com_Printf( "List registered console variables.\n" );
            Cmd_PrintHelp( o_cvarlist );
            Com_Printf(
                        "Flags legend:\n"
                        "C: cheat protected\n"
                        "A: archived in config file\n"
                        "U: included in userinfo\n"
                        "S: included in serverinfo\n"
                        "N: set from command line only\n"
                        "R: read-only variable\n"
                        "L: latched (requires subsystem restart)\n"
                        "G: latched (requires game map restart)\n"
                        "?: created by user\n" );
            return;
        case 'l':
            latched = qtrue;
            break;
        case 'm':
            modified = qtrue;
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
            mask |= CVAR_USER_CREATED;
            break;
        case 'u':
            mask |= CVAR_USERINFO;
            break;
        case 'v':
            verbose = qtrue;
            break;
        case 'w':
            wildcard = cmd_optarg;
            break;
        default:
            return;
        }
    }

	buffer[sizeof( buffer ) - 1] = 0;
	i = 0;
	for( var = cvar_vars, total = 0; var; var = var->next, total++ ) {
        if( latched && !var->latched_string ) {
            continue;
        }
        if( mask && !( var->flags & mask ) ) {
            continue;
        }
		if( wildcard && !Com_WildCmp( wildcard, var->name, qtrue ) ) {
			continue;
		}
        if( modified && ( !strcmp( var->latched_string ? var->latched_string :
            var->string, var->default_string ) || ( var->flags & CVAR_ROM ) ) )
        {
            continue;
        }

		if( verbose ) {
			memset( buffer, ' ', sizeof( buffer ) - 1 );

			if( var->flags & CVAR_CHEAT )
				buffer[0] = 'C';

			if( var->flags & CVAR_ARCHIVE )
				buffer[1] = 'A';

			if( var->flags & CVAR_USERINFO )
				buffer[2] = 'U';

			if( var->flags & CVAR_SERVERINFO )
				buffer[3] = 'S';

			if( var->flags & CVAR_ROM )
				buffer[4] = 'R';
            else if( var->flags & CVAR_NOSET )
				buffer[4] = 'N';
			else if( var->flags & CVAR_LATCHED )
				buffer[4] = 'L';
			else if( var->flags & CVAR_LATCH )
				buffer[4] = 'G';
			else if( var->flags & CVAR_USER_CREATED )
				buffer[4] = '?';

			Com_Printf( "%s ", buffer );
		}

		Com_Printf( "%s \"%s\"\n", var->name, var->string );

		i++;
	}
	Com_Printf( "%i of %i cvars\n", i, total );
}

/*
============
Cvar_Toggle_f
============
*/
static void Cvar_Toggle_f( void ) {
	char buffer[MAX_STRING_CHARS];
	cvar_t *var;
	int i;
	int numArgs;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <variable> [values]\n", Cmd_Argv( 0 ) );
		return;
	}

	var = Cvar_FindVar( Cmd_Argv( 1 ) );
	if( !var ) {
		Com_Printf( "%s is not a variable\n", Cmd_Argv( 1 ) );
		return;
	}

	if( Cmd_Argc() < 3 ) {
		if( !strcmp( var->string, "0" ) ) {
			Cvar_SetByVar( var, "1", CVAR_SET_CONSOLE );
		} else if( !strcmp( var->string, "1" ) ) {
			Cvar_SetByVar( var, "0", CVAR_SET_CONSOLE );
		} else {
			Com_Printf( "\"%s\" is \"%s\", can't toggle\n",
                    var->name, var->string );
		}
		return;
	}

	Q_strncpyz( buffer, Cmd_RawArgsFrom( 2 ), sizeof( buffer ) );
	Cmd_TokenizeString( buffer, qfalse );
	numArgs = Cmd_Argc();
	for( i = 0; i < numArgs; i++ ) {
		if( !Q_stricmp( var->string, Cmd_Argv( i ) ) ) {
			i = ( i + 1 ) % numArgs;
			Cvar_SetByVar( var, Cmd_Argv( i ), CVAR_SET_CONSOLE );
			return;
		}
	}

	Com_Printf( "\"%s\" is \"%s\", can't cycle\n", var->name, var->string );
}

static void Cvar_Toggle_c( genctx_t *ctx, int argnum ) {
    char *s;
    xgenerator_t g;

    if( argnum == 1 ) {
        Cvar_Variable_g( ctx );
    } else {
        s = Cmd_Argv( ctx->argnum - argnum + 1 );
        if( ( g = Cvar_FindGenerator( s ) ) != NULL ) {
            g( ctx );
        }
    }
}

/*
============
Cvar_Inc_f
============
*/
static void Cvar_Inc_f( void ) {
	cvar_t *var;
	float val;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <variable> [value]\n", Cmd_Argv( 0 ) );
		return;
	}

	var = Cvar_FindVar( Cmd_Argv( 1 ) );
	if( !var ) {
		Com_Printf( "%s is not a variable\n", Cmd_Argv( 1 ) );
		return;
	}

	if( !COM_IsNumeric( var->string ) ) {
		Com_Printf( "\"%s\" is \"%s\", can't increment\n",
            var->name, var->string );
		return;
	}

	val = 1;
	if( Cmd_Argc() > 2 ) {
		val = atof( Cmd_Argv( 2 ) );
	}
    if( !strcmp( Cmd_Argv( 0 ), "dec" ) ) {
        val = -val;
    }

	Cvar_SetValue( var->name, var->value + val );
}

/*
============
Cvar_Reset_f
============
*/
static void Cvar_Reset_f( void ) {
	cvar_t *var;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <variable>\n", Cmd_Argv( 0 ) );
		return;
	}

	var = Cvar_FindVar( Cmd_Argv( 1 ) );
	if( !var ) {
		Com_Printf( "%s is not a variable\n", Cmd_Argv( 1 ) );
		return;
	}

	Cvar_SetByVar( var, var->default_string, CVAR_SET_CONSOLE );
}

static void Cvar_Reset_c( genctx_t *ctx, int argnum ) {
    if( argnum == 1 ) {
        Cvar_Variable_g( ctx );
    }
}

int Cvar_BitInfo( char *info, int bit ) {
	char newi[MAX_INFO_STRING];
	cvar_t	*var;
	int length, total = 0;

	for( var = cvar_vars; var; var = var->next ) {
		if( !( var->flags & bit ) ) {
            continue;
        }
        if( !var->string[0] ) {
            continue;
        }
        length = Q_concat( newi, sizeof( newi ),
            "\\", var->name, "\\", var->string, NULL );
        if( total + length >= MAX_INFO_STRING ) {
            break;
        }
        memcpy( info + total, newi, length );
        total += length;
	}
	info[total] = 0;
	return total;
}


/*
============
Cvar_FillAPI
============
*/
void Cvar_FillAPI( cvarAPI_t *api ) {
	api->Get = Cvar_Get;
	api->Set = Cvar_Set;
	api->Find = Cvar_FindVar;
	api->SetValue = Cvar_SetValue;
	api->SetInteger = Cvar_SetInteger;
	api->VariableValue = Cvar_VariableValue;
	api->VariableInteger = Cvar_VariableInteger;
	api->VariableString = Cvar_VariableString;
	api->VariableStringBuffer = Cvar_VariableStringBuffer;
}

static const cmdreg_t c_cvar[] = {
    { "set", Cvar_Set_f, Cvar_Set_c },
    { "setu", Cvar_SetFlag_f, Cvar_Set_c },
    { "sets", Cvar_SetFlag_f, Cvar_Set_c },
    { "seta", Cvar_SetFlag_f, Cvar_Set_c },
    { "cvarlist", Cvar_List_f, Cvar_List_c },
    { "toggle", Cvar_Toggle_f, Cvar_Toggle_c },
    { "inc", Cvar_Inc_f, Cvar_Reset_c },
    { "dec", Cvar_Inc_f, Cvar_Reset_c },
    { "reset", Cvar_Reset_f, Cvar_Reset_c },

    { NULL }
};

/*
============
Cvar_Init
============
*/
void Cvar_Init( void ) {
	cvar_silent = Cvar_Get( "cvar_silent", "0", 0 );
    
    Cmd_Register( c_cvar );

	Cvar_FillAPI( &cvar );
}

