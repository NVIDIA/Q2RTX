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
#include "cl_local.h"

/*

key up events are sent even if in console mode

*/

#define MAXCMDLINE 256

static int	anykeydown;

static int		key_waiting;
static char		*keybindings[256];

// if qtrue, can't be rebound while in console
static qboolean	consolekeys[256];

// if qtrue, can't be rebound while in menu
static qboolean	menubound[256];

#ifndef USE_CHAR_EVENTS
// key to map to if shift held down in console
// used unless char events are provided by OS
static int		keyshift[256];
#endif /* !USE_CHAR_EVENTS */

static int		key_repeats[256];	// if > 1, it is autorepeating
static qboolean	keydown[256];

static qboolean	key_overstrike;

typedef struct keyname_s {
	char	*name;
	int		keynum;
} keyname_t;

static const keyname_t keynames[] = {
	{"TAB", K_TAB},
	{"ENTER", K_ENTER},
	{"ESCAPE", K_ESCAPE},
	{"SPACE", K_SPACE},
	{"BACKSPACE", K_BACKSPACE},
	{"UPARROW", K_UPARROW},
	{"DOWNARROW", K_DOWNARROW},
	{"LEFTARROW", K_LEFTARROW},
	{"RIGHTARROW", K_RIGHTARROW},

	{"ALT", K_ALT},
	{"LALT", K_LALT},
	{"RALT", K_RALT},
	{"CTRL", K_CTRL},
	{"LCTRL", K_LCTRL},
	{"RCTRL", K_RCTRL},
	{"SHIFT", K_SHIFT},
	{"LSHIFT", K_LSHIFT},
	{"RSHIFT", K_RSHIFT},
	
	{"F1", K_F1},
	{"F2", K_F2},
	{"F3", K_F3},
	{"F4", K_F4},
	{"F5", K_F5},
	{"F6", K_F6},
	{"F7", K_F7},
	{"F8", K_F8},
	{"F9", K_F9},
	{"F10", K_F10},
	{"F11", K_F11},
	{"F12", K_F12},

	{"INS", K_INS},
	{"DEL", K_DEL},
	{"PGDN", K_PGDN},
	{"PGUP", K_PGUP},
	{"HOME", K_HOME},
	{"END", K_END},

	{"NUMLOCK", K_NUMLOCK},
	{"CAPSLOCK", K_CAPSLOCK},
	{"SCROLLOCK", K_SCROLLOCK},
	{"LWINKEY", K_LWINKEY},
	{"RWINKEY", K_RWINKEY},
	{"MENU", K_MENU},

	{"MOUSE1", K_MOUSE1},
	{"MOUSE2", K_MOUSE2},
	{"MOUSE3", K_MOUSE3},
	{"MOUSE4", K_MOUSE4},
	{"MOUSE5", K_MOUSE5},
	{"MOUSE6", K_MOUSE6},
	{"MOUSE7", K_MOUSE7},
	{"MOUSE8", K_MOUSE8},

	{"JOY1", K_JOY1},
	{"JOY2", K_JOY2},
	{"JOY3", K_JOY3},
	{"JOY4", K_JOY4},

	{"AUX1", K_AUX1},
	{"AUX2", K_AUX2},
	{"AUX3", K_AUX3},
	{"AUX4", K_AUX4},
	{"AUX5", K_AUX5},
	{"AUX6", K_AUX6},
	{"AUX7", K_AUX7},
	{"AUX8", K_AUX8},
	{"AUX9", K_AUX9},
	{"AUX10", K_AUX10},
	{"AUX11", K_AUX11},
	{"AUX12", K_AUX12},
	{"AUX13", K_AUX13},
	{"AUX14", K_AUX14},
	{"AUX15", K_AUX15},
	{"AUX16", K_AUX16},
	{"AUX17", K_AUX17},
	{"AUX18", K_AUX18},
	{"AUX19", K_AUX19},
	{"AUX20", K_AUX20},
	{"AUX21", K_AUX21},
	{"AUX22", K_AUX22},
	{"AUX23", K_AUX23},
	{"AUX24", K_AUX24},
	{"AUX25", K_AUX25},
	{"AUX26", K_AUX26},
	{"AUX27", K_AUX27},
	{"AUX28", K_AUX28},
	{"AUX29", K_AUX29},
	{"AUX30", K_AUX30},
	{"AUX31", K_AUX31},
	{"AUX32", K_AUX32},

	{"KP_HOME",			K_KP_HOME },
	{"KP_UPARROW",		K_KP_UPARROW },
	{"KP_PGUP",			K_KP_PGUP },
	{"KP_LEFTARROW",	K_KP_LEFTARROW },
	{"KP_5",			K_KP_5 },
	{"KP_RIGHTARROW",	K_KP_RIGHTARROW },
	{"KP_END",			K_KP_END },
	{"KP_DOWNARROW",	K_KP_DOWNARROW },
	{"KP_PGDN",			K_KP_PGDN },
	{"KP_ENTER",		K_KP_ENTER },
	{"KP_INS",			K_KP_INS },
	{"KP_DEL",			K_KP_DEL },
	{"KP_SLASH",		K_KP_SLASH },
	{"KP_MINUS",		K_KP_MINUS },
	{"KP_PLUS",			K_KP_PLUS },
	{"KP_MULTIPLY",		K_KP_MULTIPLY },

	{"MWHEELUP", K_MWHEELUP },
	{"MWHEELDOWN", K_MWHEELDOWN },

	{"PAUSE", K_PAUSE},

	{"SEMICOLON", ';'},	// because a raw semicolon seperates commands

	{NULL,0}
};

//============================================================================

/*
===================
Key_GetOverstrikeMode
===================
*/
qboolean Key_GetOverstrikeMode( void ) {
	return key_overstrike;
}

/*
===================
Key_SetOverstrikeMode
===================
*/
void Key_SetOverstrikeMode( qboolean overstrike ) {
	key_overstrike = overstrike;
}

/*
===================
Key_GetDest
===================
*/
keydest_t Key_GetDest( void ) {
	return cls.key_dest;
}

/*
===================
Key_SetDest
===================
*/
void Key_SetDest( keydest_t dest ) {
	int diff;

// make sure at least fullscreen console or main menu is up
    if( cls.state == ca_disconnected && !( dest & (KEY_MENU|KEY_CONSOLE) ) ) {
        dest |= KEY_CONSOLE;
    }

	diff = cls.key_dest ^ dest;
	if( diff & KEY_CONSOLE ) {
        if( dest & KEY_CONSOLE ) {
// release all keys, to keep the character from continuing an
// action started before a console switch
            Key_ClearStates();
        }
    }

	cls.key_dest = dest;

    if( dest & (KEY_CONSOLE|KEY_MENU) ) {
	    // only pause in single player
    	if( cl_paused->integer == 0 ) {
	    	Cvar_Set( "cl_paused", "1" );
    	}
    } else if( cl_paused->integer == 1 ) {
        // only resume after automatic pause
		Cvar_Set( "cl_paused", "0" );
    }

// activate or deactivate mouse
	if( diff & (KEY_CONSOLE|KEY_MENU) ) {
    	IN_Activate();
	}
}

/*
===================
Key_IsDown
===================
*/
qboolean Key_IsDown( int key ) {
	if( key < 0 || key > 255 ) {
		return qfalse;
	}

	return keydown[key];
}

/*
===================
Key_AnyKeyDown
===================
*/
qboolean Key_AnyKeyDown( void ) {
	return anykeydown;
}

/*
===================
Key_StringToKeynum

Returns a key number to be used to index keybindings[] by looking at
the given string.  Single ascii characters return themselves, while
the K_* names are matched up.
===================
*/
int Key_StringToKeynum( const char *str ) {
	const keyname_t	*kn;
	
	if( !str || !str[0] )
		return -1;
	if( !str[1] )
		return str[0];

	for( kn = keynames; kn->name; kn++ ) {
		if( !Q_stricmp( str, kn->name ) )
			return kn->keynum;
	}
	return -1;
}

/*
===================
Key_KeynumToString

Returns a string (either a single ascii char, or a K_* name) for the
given keynum.
FIXME: handle quote special (general escape sequence?)
===================
*/
char *Key_KeynumToString( int keynum ) {
	const keyname_t	*kn;	
	static char	tinystr[2];
	
	if( keynum == -1 )
		return "<KEY NOT FOUND>";
	if( keynum > 32 && keynum < 127 ) {	
		// printable ascii
		tinystr[0] = keynum;
		tinystr[1] = 0;
		return tinystr;
	}
	
	for( kn = keynames; kn->name; kn++ )
		if( keynum == kn->keynum )
			return kn->name;

	return "<UNKNOWN KEYNUM>";
}

/*
===================
Key_SetBinding

Returns the name of the first key found.
===================
*/
char *Key_GetBinding( const char *binding ) {
	int key;

	for( key = 0; key < 256; key++ ) {
		if( keybindings[key] ) {
			if( !Q_stricmp( keybindings[key], binding ) ) {
				return Key_KeynumToString( key );
			}
		}
	}

	return "";
}

/*
===================
Key_EnumBindings
===================
*/
int Key_EnumBindings( int key, const char *binding ) {
	if( key < 0 ) {
		key = 0;
	}
	for( ; key < 256; key++ ) {
		if( keybindings[key] ) {
			if( !Q_stricmp( keybindings[key], binding ) ) {
				return key;
			}
		}
	}

	return -1;
}

/*
===================
Key_SetBinding
===================
*/
void Key_SetBinding( int keynum, const char *binding ) {	
	if( keynum < 0 || keynum > 255 )
		return;

// free old binding
	if( keybindings[keynum] ) {
		Z_Free( keybindings[keynum] );
	}
			
// allocate memory for new binding
	keybindings[keynum] = Z_CopyString( binding );	
}

static void Key_Name_g( genctx_t *ctx ) {
    const keyname_t *k;

    ctx->ignorecase = qtrue;
    for( k = keynames; k->name; k++ ) {
        if( !Prompt_AddMatch( ctx, k->name ) ) {
            break;
        }
    }
}

static void Key_Bound_g( genctx_t *ctx ) {
    int i;

    ctx->ignorecase = qtrue;
    for( i = 0; i < 256; i++ ) {
        if( keybindings[i] ) {
            if( !Prompt_AddMatch( ctx, Key_KeynumToString( i ) ) ) {
                break;
            }
        }
    }
}

static void Key_Bind_c( genctx_t *ctx, int argnum ) {
    if( argnum == 1 ) {
        Key_Name_g( ctx );
    } else {
        Com_Generic_c( ctx, argnum - 2 );
    }
}

static void Key_Unbind_c( genctx_t *ctx, int argnum ) {
    if( argnum == 1 ) {
        Key_Bound_g( ctx );
    }
}

/*
===================
Key_Unbind_f
===================
*/
static void Key_Unbind_f( void ) {
	int		b;

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "unbind <key> : remove commands from a key\n" );
		return;
	}
	
	b = Key_StringToKeynum( Cmd_Argv( 1 ) );
	if( b == -1 ) {
		Com_Printf( "\"%s\" isn't a valid key\n", Cmd_Argv( 1 ) );
		return;
	}

	Key_SetBinding( b, NULL );
}

/*
===================
Key_Unbindall_f
===================
*/
static void Key_Unbindall_f( void ) {
	int		i;
	
	for( i = 0; i < 256; i++ )
		if( keybindings[i] )
			Key_SetBinding( i, NULL );
}


/*
===================
Key_Bind_f
===================
*/
static void Key_Bind_f( void ) {
	int	c, b;
	
	c = Cmd_Argc();

	if( c < 2 ) {
		Com_Printf( "bind <key> [command] : attach a command to a key\n" );
		return;
	}
	b = Key_StringToKeynum( Cmd_Argv( 1 ) );
	if( b == -1 ) {
		Com_Printf( "\"%s\" isn't a valid key\n", Cmd_Argv( 1 ) );
		return;
	}

	if( c == 2 ) {
		if( keybindings[b] )
			Com_Printf( "\"%s\" = \"%s\"\n", Cmd_Argv( 1 ), keybindings[b] );
		else
			Com_Printf( "\"%s\" is not bound\n", Cmd_Argv( 1 ) );
		return;
	}
	
// copy the rest of the command line
	Key_SetBinding( b, Cmd_ArgsFrom( 2 ) );
}

/*
============
Key_WriteBindings

Writes lines containing "bind key value"
============
*/
void Key_WriteBindings( fileHandle_t f ) {
	int		i;

	for( i = 0; i < 256; i++ ) {
		if( keybindings[i] && keybindings[i][0] ) {
			FS_FPrintf( f, "bind %s \"%s\"\n", Key_KeynumToString( i ),
                keybindings[i] );
        }
    }
}


/*
============
Key_Bindlist_f

============
*/
static void Key_Bindlist_f( void ) {
	int		i;

	for( i = 0; i < 256; i++ ) {
		if( keybindings[i] && keybindings[i][0] ) {
			Com_Printf( "%s \"%s\"\n", Key_KeynumToString( i ),
                keybindings[i] );
        }
    }
}

static cmdreg_t c_keys[] = {
	{ "bind", Key_Bind_f, Key_Bind_c },
	{ "unbind", Key_Unbind_f, Key_Unbind_c },
	{ "unbindall", Key_Unbindall_f },
	{ "bindlist", Key_Bindlist_f },

    { NULL }
};

/*
===================
Key_Init
===================
*/
void Key_Init( void ) {
	int		i;
	
//
// init ascii characters in console mode
//
	for( i = 32; i < 128; i++ )
		consolekeys[i] = qtrue;
	consolekeys[K_ENTER] = qtrue;
	consolekeys[K_KP_ENTER] = qtrue;
	consolekeys[K_TAB] = qtrue;
	consolekeys[K_LEFTARROW] = qtrue;
	consolekeys[K_KP_LEFTARROW] = qtrue;
	consolekeys[K_RIGHTARROW] = qtrue;
	consolekeys[K_KP_RIGHTARROW] = qtrue;
	consolekeys[K_UPARROW] = qtrue;
	consolekeys[K_KP_UPARROW] = qtrue;
	consolekeys[K_DOWNARROW] = qtrue;
	consolekeys[K_KP_DOWNARROW] = qtrue;
	consolekeys[K_BACKSPACE] = qtrue;
	consolekeys[K_HOME] = qtrue;
	consolekeys[K_KP_HOME] = qtrue;
	consolekeys[K_END] = qtrue;
	consolekeys[K_KP_END] = qtrue;
	consolekeys[K_PGUP] = qtrue;
	consolekeys[K_KP_PGUP] = qtrue;
	consolekeys[K_PGDN] = qtrue;
	consolekeys[K_KP_PGDN] = qtrue;
	consolekeys[K_SHIFT] = qtrue;
	consolekeys[K_LSHIFT] = qtrue;
	consolekeys[K_RSHIFT] = qtrue;
	consolekeys[K_INS] = qtrue;
	consolekeys[K_KP_INS] = qtrue;
	consolekeys[K_KP_DEL] = qtrue;
	consolekeys[K_KP_SLASH] = qtrue;
	consolekeys[K_KP_PLUS] = qtrue;
	consolekeys[K_KP_MINUS] = qtrue;
	consolekeys[K_KP_5] = qtrue;
	consolekeys[K_DEL] = qtrue;
	consolekeys[K_CTRL] = qtrue;
	consolekeys[K_LCTRL] = qtrue;
	consolekeys[K_RCTRL] = qtrue;
	consolekeys[K_ALT] = qtrue;
	consolekeys[K_LALT] = qtrue;
	consolekeys[K_RALT] = qtrue;
	consolekeys[K_MWHEELUP] = qtrue;
	consolekeys[K_MWHEELDOWN] = qtrue;
	consolekeys[K_MOUSE3] = qtrue;

	consolekeys['`'] = qtrue;
	consolekeys['~'] = qtrue;

#ifndef USE_CHAR_EVENTS

	for( i = 0; i < 256; i++ )
		keyshift[i] = i;
	for( i = 'a'; i <= 'z'; i++ )
		keyshift[i] = i - 'a' + 'A';
	keyshift['1'] = '!';
	keyshift['2'] = '@';
	keyshift['3'] = '#';
	keyshift['4'] = '$';
	keyshift['5'] = '%';
	keyshift['6'] = '^';
	keyshift['7'] = '&';
	keyshift['8'] = '*';
	keyshift['9'] = '(';
	keyshift['0'] = ')';
	keyshift['-'] = '_';
	keyshift['='] = '+';
	keyshift[','] = '<';
	keyshift['.'] = '>';
	keyshift['/'] = '?';
	keyshift[';'] = ':';
	keyshift['\''] = '"';
	keyshift['['] = '{';
	keyshift[']'] = '}';
	keyshift['`'] = '~';
	keyshift['\\'] = '|';
    
#endif /* !USE_CHAR_EVENTS */

	menubound[K_ESCAPE] = qtrue;
	for( i = 0; i < 12; i++ )
		menubound[K_F1+i] = qtrue;

//
// register our functions
//
    Cmd_Register( c_keys );
}

/*
===================
Key_Event

Called by the system between frames for both key up and key down events
Should NOT be called during an interrupt!
===================
*/
void Key_Event( unsigned key, qboolean down, unsigned time ) {
	char	*kb;
	char	cmd[MAX_STRING_CHARS];

    if( key >= 256 ) {
        Com_Error( ERR_FATAL, "%s: bad key", __func__ );
    }

    //Com_Printf("%s: %d\n",Key_KeynumToString(key),down);

	// hack for modal presses
	if( key_waiting == -1 ) {
		if( down )
			key_waiting = key;
		return;
	}

	// update auto-repeat status
	if( down ) {
		key_repeats[key]++;
		if( !( cls.key_dest & (KEY_CONSOLE|KEY_MESSAGE|KEY_MENU) )
            && key != K_BACKSPACE 
			&& key != K_PAUSE 
			&& key != K_PGUP 
			&& key != K_KP_PGUP 
			&& key != K_PGDN
			&& key != K_KP_PGDN
			&& key_repeats[key] > 1 )
		{
			return;	// ignore most autorepeats
		}
			
		if( key >= 200 && !keybindings[key] && !consolekeys[key] ) {
			Com_Printf( "%s is unbound, hit F4 to set.\n",
                Key_KeynumToString( key ) );
        }
	} else {
		key_repeats[key] = 0;
	}

	// console key is hardcoded, so the user can never unbind it
	if( !keydown[K_SHIFT] && ( key == '`' || key == '~' ) ) {
		if( down ) {
    		Con_ToggleConsole_f();
        }
		return;
	}

    // Alt+Enter is hardcoded for all systems
    if( keydown[K_ALT] && key == K_ENTER ) {
        if( down ) {
            extern void VID_ToggleFullscreen( void );

            VID_ToggleFullscreen();
        }
        return;
    }

	// menu key is hardcoded, so the user can never unbind it
	if( key == K_ESCAPE ) {
		if( !down ) {
			return;
		}
		if( key_repeats[key] > 1 ) {
			return;
		}

		if( cls.key_dest == KEY_GAME &&
            !cls.demo.playback &&
			cl.clientNum != -1 &&
			cl.frame.ps.stats[STAT_LAYOUTS] &&
            !cl.putaway )
		{	 
			// put away help computer / inventory
			Cbuf_AddText( "cmd putaway\n" );
            cl.putaway = qtrue;
			return;
		}

		if( cls.state > ca_disconnected && cls.state < ca_active ) {
			if( cls.key_dest & KEY_CONSOLE ) {
				Con_Close();
			} else {
				CL_Disconnect( ERR_SILENT, NULL );
			}
			return;
		}

		if( cls.key_dest & KEY_CONSOLE ) {
			if( cls.state == ca_disconnected && !( cls.key_dest & KEY_MENU ) ) {
#if USE_UI
				UI_OpenMenu( UIMENU_MAIN_FORCE );
#endif
			} else {
    			Con_Close();
            }
		}
#if USE_UI
        else if( cls.key_dest & KEY_MENU ) {
			UI_Keydown( key );
        }
#endif
        else if( cls.key_dest & KEY_MESSAGE ) {
			Key_Message( key );
		}
#if USE_UI
        else if( cls.state == ca_active ) {
			UI_OpenMenu( UIMENU_INGAME );
		} else {
			UI_OpenMenu( UIMENU_MAIN_FORCE );
		}
#endif
		return;
	}

	// track if any key is down for BUTTON_ANY
	keydown[key] = down;
	if( down ) {
		if( key_repeats[key] == 1 )
			anykeydown++;
	} else {
		anykeydown--;
		if( anykeydown < 0 )
			anykeydown = 0;
	}
    
//
// if not a consolekey, send to the interpreter no matter what mode is
//
	if( ( cls.key_dest == KEY_GAME ) ||
		( ( cls.key_dest & KEY_CONSOLE ) && !consolekeys[key] ) ||
		( ( cls.key_dest & KEY_MENU ) && menubound[key] ) )
    {

//
// Key up events only generate commands if the game key binding is
// a button command (leading + sign).
// Button commands include the kenum as a parameter, so multiple
// downs can be matched with ups.
//
        if( !down ) {
            kb = keybindings[key];
            if( kb && kb[0] == '+' ) {
                Com_sprintf( cmd, sizeof( cmd ), "-%s %i %i\n",
                    kb + 1, key, time );
                Cbuf_InsertText( cmd );
            }
#ifndef USE_CHAR_EVENTS
            if( keyshift[key] != key ) {
                kb = keybindings[keyshift[key]];
                if( kb && kb[0] == '+' ) {
                    Com_sprintf( cmd, sizeof( cmd ), "-%s %i %i\n",
                        kb + 1, key, time );
                    Cbuf_InsertText( cmd );
                }
            }
#endif // USE_CHAR_EVENTS
            return;
        }

		if( key_repeats[key] > 1 ) {
			return;
		}

#ifndef USE_CHAR_EVENTS
        if( keydown[K_SHIFT] && keyshift[key] != key && keybindings[keyshift[key]] ) {
            key = keyshift[key];
        }
#endif // USE_CHAR_EVENTS

		kb = keybindings[key];
		if( kb ) {
			if( kb[0] == '+' ) {	
				// button commands add keynum and time as a parm
				Com_sprintf( cmd, sizeof( cmd ), "%s %i %i\n", kb, key, time );
				Cbuf_InsertText( cmd );
			} else {
				Cbuf_InsertText( kb );
				Cbuf_InsertText( "\n" );
			}
		}
		return;
	}

	if( !down )
		return;		// other subsystems only care about key down events

	if( cls.key_dest & KEY_CONSOLE ) {
		Key_Console( key );
	}
#if USE_UI
    else if( cls.key_dest & KEY_MENU ) {
		UI_Keydown( key );
	}
#endif
    else if( cls.key_dest & KEY_MESSAGE ) {
		Key_Message( key );
	}

#ifndef USE_CHAR_EVENTS

	if( keydown[K_CTRL] || keydown[K_ALT] ) {
		return;
	}

	switch( key ) {
	case K_KP_SLASH:
		key = '/';
		break;
	case K_KP_MULTIPLY:
		key = '*';
		break;
	case K_KP_MINUS:
		key = '-';
		break;
	case K_KP_PLUS:
		key = '+';
		break;
	case K_KP_HOME:
		key = '7';
		break;
	case K_KP_UPARROW:
		key = '8';
		break;
	case K_KP_PGUP:
		key = '9';
		break;
	case K_KP_LEFTARROW:
		key = '4';
		break;
	case K_KP_5:
		key = '5';
		break;
	case K_KP_RIGHTARROW:
		key = '6';
		break;
	case K_KP_END:
		key = '1';
		break;
	case K_KP_DOWNARROW:
		key = '2';
		break;
	case K_KP_PGDN:
		key = '3';
		break;
	case K_KP_INS:
		key = '0';
		break;
	case K_KP_DEL:
		key = '.';
		break;
	}

    // if key is printable, generate char events
    if( key < 32 || key >= 127 ) {
        return;
    }
    
	if( keydown[K_SHIFT] ) {
		key = keyshift[key];
    }

	if( cls.key_dest & KEY_CONSOLE ) {
		Char_Console( key );
	}
#if USE_UI
    else if( cls.key_dest & KEY_MENU ) {
		UI_CharEvent( key );
	}
#endif
    else if( cls.key_dest & KEY_MESSAGE ) {
		Char_Message( key );
	}

#endif // !USE_CHAR_EVENTS

}

#ifdef USE_CHAR_EVENTS

/*
===================
Key_CharEvent
===================
*/
void Key_CharEvent( int key ) {
	if( key == '`' || key == '~' ) {
		return;
	}

	if( cls.key_dest & KEY_CONSOLE ) {
		Char_Console( key );
	}
#if USE_UI
    else if( cls.key_dest & KEY_MENU ) {
		UI_CharEvent( key );
	}
#endif
    else if( cls.key_dest & KEY_MESSAGE ) {
		Char_Message( key );
	}
}

#endif // USE_CHAR_EVENTS

/*
===================
Key_ClearStates
===================
*/
void Key_ClearStates( void ) {
	int		i;

	for( i = 0; i < 256; i++ ) {
		if( keydown[i] || key_repeats[i] )
			Key_Event( i, qfalse, 0 );
		keydown[i] = 0;
		key_repeats[i] = 0;
	}

	anykeydown = 0;
}





