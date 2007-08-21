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
// cmd.c -- Quake script command processing module

#include "com_local.h"
#include "q_list.h"

#define Cmd_Malloc( size )		        Z_TagMalloc( size, TAG_CMD )
#define Cmd_CopyString( string )		Z_TagCopyString( string, TAG_CMD )

cmdAPI_t	cmd;

/*
=============================================================================

						COMMAND BUFFER

=============================================================================
*/

char			cmd_buffer_text[CMD_BUFFER_SIZE];
cmdbuf_t		cmd_buffer;

/*
============
Cmd_Wait_f

Causes execution of the remainder of the command buffer to be delayed until
next frame.  This allows commands like:
bind g "impulse 5 ; +attack ; wait ; -attack ; impulse 2"
============
*/
static void Cmd_Wait_f( void ) {
	if( Cmd_Argc() > 1 ) {
		cmd_buffer.waitCount = atoi( Cmd_Argv( 1 ) );
	} else {
		cmd_buffer.waitCount = 1;
	}
}

/*
============
Cbuf_Init
============
*/
void Cbuf_Init( void ) {
    memset( &cmd_buffer, 0, sizeof( cmd_buffer ) );
	cmd_buffer.text = cmd_buffer_text;
    cmd_buffer.maxsize = sizeof( cmd_buffer_text );
	cmd_buffer.exec = Cmd_ExecuteString;
}

/*
============
Cbuf_AddText

Adds command text at the end of the buffer
============
*/
void Cbuf_AddTextEx( cmdbuf_t *buf, const char *text ) {
	int		l = strlen( text );

	if( buf->cursize + l > buf->maxsize ) {
		Com_WPrintf( "Cbuf_AddText: overflow\n" );
		return;
	}
    memcpy( buf->text + buf->cursize, text, l );
    buf->cursize += l;
}

char *Cbuf_Alloc( cmdbuf_t *buf, int length ) {
    char *text;

	if( buf->cursize + length > buf->maxsize ) {
		return NULL;
	}
    text = buf->text + buf->cursize;
    buf->cursize += length;

    return text;
}

/*
============
Cbuf_InsertText

Adds command text at the beginning of command buffer.
Adds a \n to the text.
============
*/
void Cbuf_InsertTextEx( cmdbuf_t *buf, const char *text ) {
	int		l = strlen( text );

// add the entire text of the file
    if( !l ) {
        return;
    }
	if( buf->cursize + l + 1 > buf->maxsize ) {
		Com_WPrintf( "Cbuf_InsertText: overflow\n" );
		return;
	}

    memmove( buf->text + l + 1, buf->text, buf->cursize );
    memcpy( buf->text, text, l );
    buf->text[l] = '\n';
    buf->cursize += l + 1;
}

/*
============
Cbuf_ExecuteText
============
*/
void Cbuf_ExecuteText( cbufExecWhen_t exec_when, const char *text ) {
	switch( exec_when ) {
	case EXEC_NOW:
		Cmd_ExecuteString( text );
		break;
	case EXEC_INSERT:
		Cbuf_InsertText( text );
		break;
	case EXEC_APPEND:
		Cbuf_AddText( text );
		break;
	default:
		Com_Error( ERR_FATAL, "Cbuf_ExecuteText: bad exec_when" );
	}
}

/*
============
Cbuf_Execute
============
*/
void Cbuf_ExecuteEx( cmdbuf_t *buf ) {
	int		i;
	char	*text;
	char	line[MAX_STRING_CHARS];
	int		quotes;

	while( buf->cursize ) {
		if( buf->waitCount > 0 ) {
			// skip out while text still remains in buffer, leaving it
			// for next frame (counter is decremented externally now)
            return;
		}

// find a \n or ; line break
		text = buf->text;

		quotes = 0;
		for( i = 0; i < buf->cursize; i++ ) {
			if( text[i] == '"' )
				quotes++;
			if( !( quotes & 1 ) && text[i] == ';' )
				break;	// don't break if inside a quoted string
			if( text[i] == '\n' )
				break;
		}

		// check for overflow
		if( i > sizeof( line ) - 1 ) {
			i = sizeof( line ) - 1;
		}

		memcpy( line, text, i );
		line[i] = 0;
		
// delete the text from the command buffer and move remaining commands down
// this is necessary because commands (exec, alias) can insert data at the
// beginning of the text buffer
		if( i == buf->cursize ) {
			buf->cursize = 0;
		} else {
			i++;
			buf->cursize -= i;
			memmove( text, text + i, buf->cursize );
		}

// execute the command line
		buf->exec( line );

	}

	buf->aliasCount = 0;		// don't allow infinite alias loops
}

/*
==============================================================================

						SCRIPT COMMANDS

==============================================================================
*/

typedef struct cmdalias_s {
    list_t  hashEntry;
	list_t  listEntry;
	char	*value;
	char	name[1];
} cmdalias_t;

#define ALIAS_HASH_SIZE	64

static list_t		cmd_alias;
static list_t		cmd_aliasHash[ALIAS_HASH_SIZE];

/*
===============
Cmd_AliasFind
===============
*/
cmdalias_t *Cmd_AliasFind( const char *name ) {
	uint32 hash;
	cmdalias_t *alias;

	hash = Com_HashString( name, ALIAS_HASH_SIZE );
    LIST_FOR_EACH( cmdalias_t, alias, &cmd_aliasHash[hash], hashEntry ) {
		if( !strcmp( name, alias->name ) ) {
			return alias;
		}
	}

	return NULL;
}

char *Cmd_AliasCommand( const char *name ) {
	cmdalias_t	*a;

	a = Cmd_AliasFind( name );
	if( !a ) {
		return NULL;
	}

	return a->value;
}

void Cmd_AliasSet( const char *name, const char *cmd ) {
	cmdalias_t	*a;
	uint32		hash;

	// if the alias already exists, reuse it
	a = Cmd_AliasFind( name );
	if( a ) {
		Z_Free( a->value );
		a->value = Cmd_CopyString( cmd );
		return;
	}

	a = Cmd_Malloc( sizeof( cmdalias_t ) + strlen( name ) );
	strcpy( a->name, name );
	a->value = Cmd_CopyString( cmd );

	List_Append( &cmd_alias, &a->listEntry );

	hash = Com_HashString( name, ALIAS_HASH_SIZE );
	List_Append( &cmd_aliasHash[hash], &a->hashEntry );
}

/*
===============
Cmd_Alias_f

Creates a new command that executes a command string (possibly ; seperated)
===============
*/
void Cmd_Alias_f( void ) {
	cmdalias_t	*a;
	char		*s, *cmd;

	if( Cmd_Argc() < 2 ) {
		if( LIST_EMPTY( &cmd_alias ) ) {
			Com_Printf( "No alias commands registered\n" );
			return;
		}
		Com_Printf( "Current alias commands:\n" );
        LIST_FOR_EACH( cmdalias_t, a, &cmd_alias, listEntry ) {
			Com_Printf( "\"%s\" = \"%s\"\n", a->name, a->value );
		}
		return;
	}

	s = Cmd_Argv( 1 );
	if( Cmd_Exists( s ) ) {
		Com_Printf( "\"%s\" already defined as a command\n", s );
		return;
	}

	if( Cvar_Exists( s ) ) {
		Com_Printf( "\"%s\" already defined as a cvar\n", s );
		return;
	}

	if( Cmd_Argc() < 3 ) {
		a = Cmd_AliasFind( s );
		if( a ) {
			Com_Printf( "\"%s\" = \"%s\"\n", a->name, a->value );
		} else {
			Com_Printf( "\"%s\" is undefined\n", s );
		}
		return;
	}

	// copy the rest of the command line
	cmd = Cmd_ArgsFrom( 2 );
	Cmd_AliasSet( s, cmd );
}

static void Cmd_UnAlias_f( void ) {
	char *s;
	cmdalias_t	*a, *n;
	uint32 hash;

	if( Cmd_CheckParam( "-h", "--help" ) ) {
usage:
		Com_Printf( "Usage: %s [-h] [-a] [name]\n"
			"-h|--help    : display this message\n"
			"-a|--all     : delete everything\n"
			"Either -a or name should be given\n", Cmd_Argv( 0 ) );
		return;
	}

	if( Cmd_CheckParam( "a", "all" ) ) {
        LIST_FOR_EACH_SAFE( cmdalias_t, a, n, &cmd_alias, listEntry ) {
			Z_Free( a->value );
			Z_Free( a );
		}
		for( hash = 0; hash < ALIAS_HASH_SIZE; hash++ ) {
			List_Init( &cmd_aliasHash[hash] );
		}
		List_Init( &cmd_alias );
		Com_Printf( "Removed all aliases\n" );
		return;
	}

	if( Cmd_Argc() < 2 ) {
		goto usage;
	}
	s = Cmd_Argv( 1 );
	a = Cmd_AliasFind( s );
	if( !a ) {
		Com_Printf( "\"%s\" is undefined\n", s );
		return;
	}

	List_Delete( &a->listEntry );
	List_Delete( &a->hashEntry );

	Z_Free( a->value );
	Z_Free( a );
}

/*
=============================================================================

					MACRO EXECUTION

=============================================================================
*/

typedef struct cmd_macro_s {
	struct cmd_macro_s	*next;
	struct cmd_macro_s	*hashNext;

	const char		*name;
	xmacro_t		function;
} cmd_macro_t;

#define MACRO_HASH_SIZE	64

static cmd_macro_t	*cmd_macros;
static cmd_macro_t	*cmd_macroHash[MACRO_HASH_SIZE];

/*
============
Cmd_FindMacro
============
*/
static cmd_macro_t *Cmd_FindMacro( const char *name ) {
	cmd_macro_t *macro;
	uint32 hash;

	hash = Com_HashString( name, MACRO_HASH_SIZE );
	for( macro = cmd_macroHash[hash]; macro ; macro = macro->hashNext ) {
		if( !strcmp( macro->name, name ) ) {
			return macro;
		}
	}

	return NULL;
}

xmacro_t Cmd_FindMacroFunction( const char *name ) {
	cmd_macro_t *macro;

	macro = Cmd_FindMacro( name );
	if( !macro ) {
		return NULL;
	}

	return macro->function;
}

/*
============
Cmd_AddMacro
============
*/
void Cmd_AddMacro( const char *name, xmacro_t function ) {
	cmd_macro_t	*macro;
    cvar_t *var;
	uint32 hash;

    var = Cvar_FindVar( name );
	if( var && !( var->flags & CVAR_USER_CREATED ) ) {
		Com_WPrintf( "Cmd_AddMacro: %s already defined as a cvar\n", name );
		return;
	}
	
// fail if the macro already exists
	if( Cmd_FindMacro( name ) ) {
		Com_WPrintf( "Cmd_AddMacro: %s already defined\n", name );
		return;
	}

	hash = Com_HashString( name, MACRO_HASH_SIZE );

	macro = Cmd_Malloc( sizeof( cmd_macro_t ) );
	macro->name = name;
	macro->function = function;
	macro->next = cmd_macros;
	cmd_macros = macro;
	macro->hashNext = cmd_macroHash[hash];
	cmd_macroHash[hash] = macro;
}


/*
=============================================================================

					COMMAND EXECUTION

=============================================================================
*/

#define CMD_HASH_SIZE	128

typedef struct cmd_function_s {
    list_t          hashEntry;
    list_t          listEntry;

	xcommand_t		function;
	xgenerator_t	generator1;
	xgenerator_t	generator2;
	xgenerator_t	generator3;
	const char		*name;
} cmd_function_t;

static	list_t		cmd_functions;		/* possible commands to execute */
static	list_t		cmd_hash[CMD_HASH_SIZE];

static	int			cmd_argc;
static	char		*cmd_argv[MAX_STRING_TOKENS];
static	char		*cmd_null_string = "";

/* complete command string, quotes preserved */
static	char		cmd_args[MAX_STRING_CHARS];

/* offsets of individual tokens in cmd_args */
static	int			cmd_offsets[MAX_STRING_TOKENS];

/* sequence of NULL-terminated tokens, each cmd_argv[] points here */
static	char		cmd_data[MAX_STRING_CHARS];

int Cmd_ArgOffset( int arg ) {
	if( arg < 0 ) {
		return 0;
	}
	if( arg >= cmd_argc ) {
		return strlen( cmd_args );
	}
	return cmd_offsets[arg];	
}

int Cmd_FindArgForOffset( int offset ) {
	int i;
 
	for( i = 1; i < cmd_argc; i++ ) {
		if( offset < cmd_offsets[i] ) {
			return i - 1;
		}
	}
	return i - 1;	
}

/*
============
Cmd_Argc
============
*/
int Cmd_Argc( void ) {
	return cmd_argc;
}

/*
============
Cmd_Argv
============
*/
char *Cmd_Argv( int arg ) {
	if( arg < 0 || arg >= cmd_argc ) {
		return cmd_null_string;
	}
	return cmd_argv[arg];	
}

/*
============
Cmd_ArgvBuffer
============
*/
void Cmd_ArgvBuffer( int arg, char *buffer, int bufferSize ) {
	char *s;

	if( arg < 0 || arg >= cmd_argc ) {
		s = cmd_null_string;
	} else {
		s = cmd_argv[arg];
	}

	Q_strncpyz( buffer, s, bufferSize );	
}


/*
============
Cmd_Args

Returns a single string containing argv(1) to argv(argc()-1)
============
*/
char *Cmd_Args( void ) {
	static char args[MAX_STRING_CHARS];
	int i;

	if( cmd_argc < 2 ) {
		return cmd_null_string;
	}

	args[0] = 0;
	for( i = 1; i < cmd_argc - 1; i++ ) {
		strcat( args, cmd_argv[i] );
		strcat( args, " " );
	}
	strcat( args, cmd_argv[i] );

	return args;
}

char *Cmd_RawArgs( void ) {
	if( cmd_argc < 2 ) {
		return cmd_null_string;
	}
	return cmd_args + cmd_offsets[1];
}

char *Cmd_RawString( void ) {
	return cmd_args;
}



/*
============
Cmd_ArgsBuffer
============
*/
void Cmd_ArgsBuffer( char *buffer, int bufferSize ) {
	Q_strncpyz( buffer, Cmd_Args(), bufferSize );	
}

/*
============
Cmd_ArgsFrom

Returns a single string containing argv(1) to argv(from-1)
============
*/
char *Cmd_ArgsFrom( int from ) {
	static char args[MAX_STRING_CHARS];
	int i;

	if( from < 0 || from >= cmd_argc ) {
		return cmd_null_string;
	}

	args[0] = 0;
	for( i = from; i < cmd_argc - 1; i++ ) {
		strcat( args, cmd_argv[i] );
		strcat( args, " " );
	}
	strcat( args, cmd_argv[i] );

	return args;
}

char *Cmd_RawArgsFrom( int from ) {
	int offset;

	if( from < 0 || from >= cmd_argc ) {
		return cmd_null_string;
	}

	offset = cmd_offsets[from];

	return cmd_args + offset;
}

/*
============
Cmd_EnumParam
============
*/
int Cmd_EnumParam( int start, const char *sp, const char *lp ) {
	int i;
	char *s;
	
	if( start < 0 || start >= cmd_argc ) {
		return 0;
	}
	for( i = start; i < cmd_argc; i++ ) {
		if( *( s = cmd_argv[i] ) == '-' ) {
			if( *( ++s ) == '-' ) {
				if( !strcmp( s + 1, lp ) ) {
					return i;
				}
			} else if( !strcmp( s, sp ) ) {
				return i;
			}
		}
	}
	
	return 0;
}

/*
============
Cmd_CheckParam
============
*/
int Cmd_CheckParam( const char *sp, const char *lp ) {
	return Cmd_EnumParam( 1, sp, lp );
}

/*
============
Cmd_FindParam
============
*/
char *Cmd_FindParam( const char *sp, const char *lp ) {
	int i;

	if( ( i = Cmd_EnumParam( 1, sp, lp ) ) && ++i != cmd_argc ) {
		return cmd_argv[i];
	}

	return NULL;
}

/*
======================
Cmd_MacroExpandString
======================
*/
char *Cmd_MacroExpandString( const char *text, qboolean aliasHack ) {
	int		i, j, count, len;
	qboolean	inquote;
	char	*scan, *start;
	static	char	expanded[MAX_STRING_CHARS];
	char	temporary[MAX_STRING_CHARS];
	char	buffer[MAX_TOKEN_CHARS];
	char	*token;
	cmd_macro_t *macro;
	cvar_t	*var;
	qboolean	rescan;

	len = strlen( text );
	if( len >= MAX_STRING_CHARS ) {
		Com_Printf( "Line exceeded %i chars, discarded.\n", MAX_STRING_CHARS );
		return NULL;
	}

	strcpy( expanded, text );
	scan = expanded;

	inquote = qfalse;
	count = 0;

	for( i = 0; i < len; i++ ) {
		if( !scan[i] ) {
			break;
		}
		if( scan[i] == '"' ) {
			inquote ^= 1;
		}
		if( inquote ) {
			continue;	/* don't expand inside quotes */
		}
		if( scan[i] != '$' ) {
			continue;
		}
		
		/* scan out the complete macro */
		start = scan + i + 1;

		if( !start[0] ) {
			break;	/* end of string */
		}

		/* allow escape syntax */
		if( i && scan[i-1] == '\\' ) {
			memmove( scan + i - 1, scan + i, len - i + 1 );
			i--;
			continue;
		}

		/* fix from jitspoe - skip leading spaces */
		while( *start && *start <= 32 ) {
			start++;
		}

		token = temporary;

		if( *start == '{' ) {
			/* allow ${variable} syntax */
			start++;
            if( *start == '$' ) {
                start++;
            }
			while( *start ) {
				if( *start == '}' ) {
					start++;
					break;
				}
				*token++ = *start++;
			}
		} else {
			/* parse single word */
			while( *start > 32 ) {
				*token++ = *start++;
			}
		}
		
		*token = 0;

		if( token == temporary ) {
			continue;
		}

		rescan = qfalse;
		
		if( aliasHack ) {
			/* expand positional parameters only */
            if( temporary[1]  ) {
                continue;
            }
            if( Q_isdigit( temporary[0] ) ) {
                token = Cmd_Argv( temporary[0] - '0' );
            } else if( temporary[0] == '@' ) {
                token = Cmd_Args();
            } else {
                continue;
            }
		} else {
			/* check for macros first */
			macro = Cmd_FindMacro( temporary );
			if( macro ) {
				macro->function( buffer, sizeof( buffer ) );
				token = buffer;
            } else {
				var = Cvar_FindVar( temporary );
				if( var && !( var->flags & CVAR_PRIVATE ) ) {
					token = var->string;
					rescan = qtrue;
				} else if( !strcmp( temporary, "qt" ) ) {
					token = "\"";
				} else if( !strcmp( temporary, "sc" ) ) {
					token = ";";
				} else {
					token = "";
				}
			}
		}

		j = strlen( token );
		len += j;
		if( len >= MAX_STRING_CHARS ) {
			Com_Printf( "Expanded line exceeded %i chars, discarded.\n",
                    MAX_STRING_CHARS );
			return NULL;
		}

		strncpy( temporary, scan, i );
		strcpy( temporary + i, token );
		strcpy( temporary + i + j, start );

		strcpy( expanded, temporary );
		scan = expanded;
		if( !rescan ) {
			i += j;
		}
		i--;

		if( ++count == 100 ) {
			Com_Printf( "Macro expansion loop, discarded.\n" );
			return NULL;
		}
	}

	if( inquote ) {
		Com_Printf( "Line has unmatched quote, discarded.\n" );
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
void Cmd_TokenizeString( const char *text, qboolean macroExpand ) {
	int		i;
	char	*data, *start, *dest;

// clear the args from the last string
	for( i = 0; i < cmd_argc; i++ ) {
		cmd_argv[i] = NULL;
		cmd_offsets[i] = 0;
	}
		
	cmd_argc = 0;
	cmd_args[0] = 0;
	
    if( !text[0] ) {
        return;
    }

// macro expand the text
	if( macroExpand ) {
		text = Cmd_MacroExpandString( text, qfalse );
		if( !text ) {
			return;
		}
	}

	Q_strncpyz( cmd_args, text, sizeof( cmd_args ) );

	dest = cmd_data;
	start = data = cmd_args;
	while( cmd_argc < MAX_STRING_TOKENS ) {
// skip whitespace up to a /n
		while( *data <= 32 ) {
			if( *data == 0 ) {
				return; // end of text
			}
			if( *data == '\n' ) {
				return; // a newline seperates commands in the buffer
			}
			data++;
		}

// add new argument
		cmd_offsets[cmd_argc] = data - start;
		cmd_argv[cmd_argc] = dest;
		cmd_argc++;

		if( *data == ';' ) {
			data++;
			*dest++ = ';';
			*dest++ = 0;
			continue;
		}

// parse quoted string
		if( *data == '\"' ) {
			data++;
			while( *data != '\"' ) {
				if( *data == 0 ) {
					return; // end of data
				}
				*dest++ = *data++;
			}
			data++;
			*dest++ = 0;
			continue;
		}

// parse reqular token
		while( *data > 32 ) {
			if( *data == '\"' ) {
				break;
			}
			if( *data == ';' ) {
				break;
			}
			*dest++ = *data++;
		}
		*dest++ = 0;

		if( *data == 0 ) {
			return; // end of text
		}
	}
}

/*
============
Cmd_Find
============
*/
cmd_function_t *Cmd_Find( const char *name ) {
	cmd_function_t *cmd;
	uint32 hash;

	hash = Com_HashString( name, CMD_HASH_SIZE );
    LIST_FOR_EACH( cmd_function_t, cmd, &cmd_hash[hash], hashEntry ) {
		if( !strcmp( cmd->name, name ) ) {
			return cmd;
		}
	}

	return NULL;
}

static void Cmd_RegCommand( const cmdreg_t *reg ) {
	cmd_function_t	*cmd;
    cvar_t *var;
	uint32 hash;
	
// fail if the command is a variable name
    var = Cvar_FindVar( reg->name );
	if( var && !( var->flags & CVAR_USER_CREATED ) ) {
		Com_WPrintf( "Cmd_AddCommand: %s already defined as a cvar\n",
            reg->name );
		return;
	}
	
// fail if the command already exists
    cmd = Cmd_Find( reg->name );
	if( cmd ) {
        Com_WPrintf( "Cmd_AddCommand: %s already defined\n", reg->name );
        return;
	}

    cmd = Cmd_Malloc( sizeof( *cmd ) );
    cmd->name = reg->name;
    cmd->function = reg->function;
    cmd->generator1 = reg->generator1;
    cmd->generator2 = reg->generator2;
    cmd->generator3 = reg->generator3;

    List_Append( &cmd_functions, &cmd->listEntry );

    hash = Com_HashString( reg->name, CMD_HASH_SIZE );
    List_Append( &cmd_hash[hash], &cmd->hashEntry );
}

/*
============
Cmd_AddCommand
============
*/
void Cmd_AddCommand( const char *name, xcommand_t function ) {
    cmdreg_t reg;

    reg.name = name;
    reg.function = function;
    reg.generator1 = NULL;
    reg.generator2 = NULL;
    reg.generator3 = NULL;
	Cmd_RegCommand( &reg );
}

void Cmd_Register( const cmdreg_t *reg ) {
    while( reg->name ) {
        Cmd_RegCommand( reg );
        reg++;
    }
}

void Cmd_Deregister( const cmdreg_t *reg ) {
    while( reg->name ) {
        Cmd_RemoveCommand( reg->name );
        reg++;
    }
}

/*
============
Cmd_RemoveCommand
============
*/
void Cmd_RemoveCommand( const char *name ) {
	cmd_function_t	*cmd;

	cmd = Cmd_Find( name );
	if( !cmd ) {
		Com_DPrintf( "Cmd_RemoveCommand: %s not added\n", name );
		return;
	}

    List_Delete( &cmd->listEntry );
    List_Delete( &cmd->hashEntry );
    Z_Free( cmd );

}

/*
============
Cmd_Exists
============
*/
qboolean Cmd_Exists( const char *name ) {
	cmd_function_t *cmd;

    cmd = Cmd_Find( name ); 
	if( !cmd ) {
		return qfalse;
	}

	return qtrue;
}

xcommand_t Cmd_FindFunction( const char *name ) {
	cmd_function_t *cmd;

	cmd = Cmd_Find( name );
	if( !cmd ) {
		return NULL;
	}

	return cmd->function;
}

xgenerator_t Cmd_FindGenerator( const char *name, int index ) {
	cmd_function_t *cmd = Cmd_Find( name );

	if( !cmd ) {
		return NULL;
	}

    switch( index ) {
        case 1: return cmd->generator1;
        case 2: return cmd->generator2;
        case 3: return cmd->generator3;
    }

	return NULL;
}

const char *Cmd_Command_g( const char *partial, int state ) {
    static int length;
    static cmd_function_t *cmd;
    const char *name;
    
    if( !state ) {
        length = strlen( partial );
		cmd = LIST_FIRST( cmd_function_t, &cmd_functions, listEntry );
    }

    while( !LIST_TERM( cmd, &cmd_functions, listEntry ) ) {
        name = cmd->name;
		cmd = LIST_NEXT( cmd_function_t, cmd, listEntry );
		if( !strncmp( partial, name, length ) ) {
            return name;
        }
    }

    return NULL;
}

const char *Cmd_Alias_g( const char *partial, int state ) {
    static int length;
    static cmdalias_t *alias;
    const char *name;
    
    if( !state ) {
        length = strlen( partial );
		alias = LIST_FIRST( cmdalias_t, &cmd_alias, listEntry );
    }

    while( !LIST_TERM( alias, &cmd_alias, listEntry ) ) {
        name = alias->name;
        alias = LIST_NEXT( cmdalias_t, alias, listEntry );
		if( !strncmp( partial, name, length ) ) {
            return name;
        }
    }

    return NULL;
}

const char *Cmd_Mixed_g( const char *partial, int state ) {
    static xgenerator_t g;
    const char *match;

    if( state == 2 ) {
        g( partial, 2 );
        return NULL;
    }

    if( !state ) {
        g = Cmd_Command_g;
    }   

    match = g( partial, state );
    if( match ) {
        return match;
    }

    if( g == Cmd_Command_g ) {
        g( partial, 2 );
        g = Cmd_Alias_g;

        match = g( partial, 0 );
        if( match ) {
            return match;
        }
    }

    return NULL;
}


/*
============
Cmd_ExecuteString

A complete command line has been parsed, so try to execute it
============
*/
void Cmd_ExecuteString( const char *text ) {	
	cmd_function_t	*cmd;
	cmdalias_t		*a;
    cvar_t          *v;

	Cmd_TokenizeString( text, qtrue );
			
	// execute the command line
	if( !cmd_argc ) {
		return;		// no tokens
	}

	// check functions
	cmd = Cmd_Find( cmd_argv[0] );
	if( cmd ) {
        if( cmd->function ) {
            cmd->function();
        } else {
            Cmd_ForwardToServer();
        }
		return;
	}

	// check aliases
	a = Cmd_AliasFind( cmd_argv[0] );
	if( a ) {
		if( cmd_buffer.aliasCount == ALIAS_LOOP_COUNT ) {
			Com_WPrintf( "Runaway alias loop\n" );
			return;
		}
		text = Cmd_MacroExpandString( a->value, qtrue );
		if( text ) {
            cmd_buffer.aliasCount++;
			Cbuf_InsertText( text );
		}
		return;
	}
	
    // check variables
    v = Cvar_FindVar( cmd_argv[0] );
	if( v ) {
        Cvar_Command( v );
		return;
    }

	// send it as a server command if we are connected
	Cmd_ForwardToServer();
}

/*
===============
Cmd_Exec_f
===============
*/
static void Cmd_Exec_f( void ) {
	char	buffer[MAX_QPATH];
	char	*f, *ext;

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "%s <filename> : execute a script file\n", Cmd_Argv( 0 ) );
		return;
	}

	Cmd_ArgvBuffer( 1, buffer, sizeof( buffer ) );

	FS_LoadFile( buffer, ( void ** )&f );
	if( !f ) {
		ext = COM_FileExtension( buffer );
		if( !ext[0] ) {
			// try with *.cfg extension
			COM_DefaultExtension( buffer, ".cfg", sizeof( buffer ) );
			FS_LoadFile( buffer, ( void ** )&f );
		}

		if( !f ) {
			Com_Printf( "Couldn't exec %s\n", buffer );
			return;
		}
	}

	Com_Printf( "Execing %s\n", buffer );
	
    // FIXME: bad thing to do in place
    COM_Compress( f );

	Cbuf_InsertText( f );

	FS_FreeFile( f );
}

static const char *Cmd_Exec_g( const char *partial, int state ) {
	return Com_FileNameGeneratorByFilter( "", "*.cfg", partial, qtrue, state );
}

/*
===============
Cmd_Echo_f

Just prints the rest of the line to the console
===============
*/
static void Cmd_Echo_f( void ) {
	Com_Printf( "%s\n", Cmd_RawArgs() );
}

static void Cmd_ColoredEcho_f( void ) {
	char buffer[MAX_STRING_CHARS];
	char *src, *dst;

	src = Cmd_RawArgs();
	dst = buffer;
	while( *src ) {
		if( src[0] == '^' && src[1] ) {
			if( src[1] == '^' ) {
				*dst++ = '^';
			} else {
				dst[0] = Q_COLOR_ESCAPE;
				dst[1] = src[1];
				dst += 2;
			}
			src += 2;
		} else {
			*dst++ = *src++;
		}
	}
	*dst = 0;
	Com_Printf( "%s\n", buffer );
}

/*
============
Cmd_List_f
============
*/
static void Cmd_List_f( void ) {
	cmd_function_t	*cmd;
	int				i, total;
	char		*filter = NULL;

	if( cmd_argc > 1 ) {
		filter = cmd_argv[1];
	}

	i = total = 0;
    LIST_FOR_EACH( cmd_function_t, cmd, &cmd_functions, listEntry ) {
		total++;
		if( filter && !Com_WildCmp( filter, cmd->name, qfalse ) ) {
			continue;
		}
		Com_Printf( "%s\n", cmd->name );
		i++;
	}
	Com_Printf( "%i of %i commands\n", i, total );
}

/*
============
Cmd_MacroList_f
============
*/
static void Cmd_MacroList_f( void ) {
	cmd_macro_t	*macro;
	int				i, total;
	char		*filter = NULL;
	char		buffer[MAX_QPATH];

	if( cmd_argc > 1 ) {
		filter = cmd_argv[1];
	}

	i = 0;
	for( macro = cmd_macros, total = 0; macro; macro = macro->next, total++ ) {
		if( filter && !Com_WildCmp( filter, macro->name, qfalse ) ) {
			continue;
		}
		macro->function( buffer, sizeof( buffer ) );
		Com_Printf( "%-16s %s\n", macro->name, buffer );
		i++;
	}
	Com_Printf( "%i of %i macros\n", i, total );
}

/*
============
Cmd_FillAPI
============
*/
void Cmd_FillAPI( cmdAPI_t *api ) {
	api->AddCommand = Cmd_AddCommand;
	api->Register = Cmd_Register;
	api->Deregister = Cmd_Deregister;
	api->RemoveCommand = Cmd_RemoveCommand;
	api->Argc = Cmd_Argc;
	api->Argv = Cmd_Argv;
	api->ArgsFrom = Cmd_ArgsFrom;
	api->ExecuteText = Cbuf_ExecuteText;
	api->FindFunction = Cmd_FindFunction;
	api->FindMacroFunction = Cmd_FindMacroFunction;
	api->FindGenerator = Cmd_FindGenerator;
}

static const cmdreg_t c_cmd[] = {
    { "cmdlist", Cmd_List_f },
    { "macrolist", Cmd_MacroList_f },
    { "exec", Cmd_Exec_f, Cmd_Exec_g },
    { "echo", Cmd_Echo_f },
    { "_echo", Cmd_ColoredEcho_f },
    { "alias", Cmd_Alias_f, Cmd_Alias_g, Cmd_Mixed_g },
    { "unalias", Cmd_UnAlias_f, Cmd_Alias_g },
    { "wait", Cmd_Wait_f },

    { NULL }
};

/*
============
Cmd_Init
============
*/
void Cmd_Init( void ) {
    int i;

    List_Init( &cmd_functions );
    for( i = 0; i < CMD_HASH_SIZE; i++ ) {
        List_Init( &cmd_hash[i] );
    }

    List_Init( &cmd_alias );
    for( i = 0; i < ALIAS_HASH_SIZE; i++ ) {
        List_Init( &cmd_aliasHash[i] );
    }

    Cmd_Register( c_cmd );
	Cmd_FillAPI( &cmd );
}

