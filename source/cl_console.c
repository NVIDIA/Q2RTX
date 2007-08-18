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
// console.c

#include "cl_local.h"
#include "prompt.h"

#define	CON_TIMES		4
#define CON_TIMES_MASK	( CON_TIMES - 1 )

#define CON_TOTALLINES			1024	// total lines in console scrollback
#define CON_TOTALLINES_MASK		( CON_TOTALLINES - 1 )

/* max chars in a single line.
 * this should be large enough to hold (maxscreenwidth / charwidth) chars,
 * plus some extra color escape codes
 */
#define CON_LINEWIDTH	512

typedef struct console_s {
	qboolean	initialized;

	char	text[CON_TOTALLINES][CON_LINEWIDTH];
	int		current;		// line where next message will be printed
	int		x;				// offset in current line for next print
	int		display;		// bottom of console displays this line

	int 	linewidth;		// characters across screen
	int		vidWidth, vidHeight;
	float	scale;

	int		times[CON_TIMES];	// cls.realtime time the line was generated
								// for transparent notify lines
	qboolean	skipNotify;

	qhandle_t	conbackImage;
	qhandle_t	charsetImage;

	int		charWidth, charHeight;

	float	currentHeight;	// aproaches scr_conlines at scr_conspeed
	float	destHeight;		// 0.0 to 1.0 lines of console to display
	float	maxHeight;

	commandPrompt_t chatPrompt;
	commandPrompt_t prompt;

	qboolean	chatTeam;
    qboolean    chatMode;
} console_t;

static console_t	con;

static cvar_t	*con_notifytime;
static cvar_t	*con_clock;
static cvar_t	*con_height;
static cvar_t	*con_speed;
static cvar_t	*con_alpha;
static cvar_t	*con_scale;
static cvar_t	*con_font;
static cvar_t	*con_background;
static cvar_t	*con_scroll;

// ============================================================================

/*
================
Con_SkipNotify
================
*/
void Con_SkipNotify( qboolean skip ) {
	con.skipNotify = skip;
}

/*
================
Con_ClearTyping
================
*/
void Con_ClearTyping( void ) {
	// clear any typing
	IF_Clear( &con.prompt.inputLine );
    con.prompt.tooMany = qfalse;
}

/*
================
Con_Close
================
*/
void Con_Close( void ) {
	Key_SetDest( cls.key_dest & ~KEY_CONSOLE );
	con.currentHeight = 0;
    con.chatMode = qfalse;

	Con_ClearTyping();
	Con_ClearNotify_f();

    if( !( cls.key_dest & KEY_MENU ) ) {
    	Cvar_Set( "cl_paused", "0" );
    }
}

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f( void ) {
	Con_ClearTyping();
	Con_ClearNotify_f();

	if( cls.key_dest & KEY_CONSOLE ) {
		Key_SetDest( cls.key_dest & ~KEY_CONSOLE );
        if( !( cls.key_dest & KEY_MENU ) ) {
		    Cvar_Set( "cl_paused", "0" );
        }
		return;
	}

	// FIXME: use old q2 style
	Key_SetDest( ( cls.key_dest | KEY_CONSOLE ) & ~KEY_MESSAGE );

	// only pause in single player
	if( cls.demoplayback || ( sv_running->integer && Cvar_VariableInteger( "maxclients" ) == 1 ) ) {
		Cvar_Set( "cl_paused", "1" );
	}

    con.chatMode = qfalse;
}

/*
================
Con_ToggleChat_f
================
*/
void Con_ToggleChat_f( void ) {
    Con_ToggleConsole_f();

	if( ( cls.key_dest & KEY_CONSOLE ) && cls.state == ca_active ) {
        con.chatMode = qtrue;
	}
}

/*
================
Con_Clear_f
================
*/
void Con_Clear_f( void ) {
	memset( con.text, 0, sizeof( con.text ) );
	con.display = con.current;
}

static const char *Con_Dump_g( const char *partial, int state ) {
	return Com_FileNameGenerator( "", ".txt", partial, qtrue, state );
}
						
/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
void Con_Dump_f( void ) {
	int		l;
	char	*line;
	fileHandle_t	f;
	char	buffer[CON_LINEWIDTH];
	char	name[MAX_QPATH];

	if( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: %s <filename>\n", Cmd_Argv( 0 ) );
		return;
	}

	Cmd_ArgvBuffer( 1, name, sizeof( name ) );
	COM_DefaultExtension( name, ".txt", sizeof( buffer ) );

	FS_FOpenFile( name, &f, FS_MODE_WRITE );
	if( !f ) {
		Com_EPrintf( "Couldn't open %s for writing.\n", name );
		return;
	}

	// skip empty lines
	for( l = con.current - CON_TOTALLINES + 1 ; l <= con.current ; l++ ) {
		if( con.text[l & CON_TOTALLINES_MASK][0] ) {
			break;
		}
	}

	// write the remaining lines
	for( ; l <= con.current ; l++ ) {
		line = con.text[l & CON_TOTALLINES_MASK];
		Q_ClearColorStr( buffer, line, sizeof( buffer ) );

		FS_FPrintf( f, "%s\n", buffer );
	}

	FS_FCloseFile( f );

	Com_Printf( "Dumped console text to %s.\n", name );
}

						
/*
================
Con_ClearNotify_f
================
*/
void Con_ClearNotify_f( void ) {
	int		i;
	
	for( i = 0; i < CON_TIMES; i++ )
		con.times[i] = 0;
}

						
/*
================
Con_MessageMode_f
================
*/
void Con_MessageMode_f( void ) {
	con.chatTeam = qfalse;
    if( Cmd_Argc() > 1 ) {
        IF_Replace( &con.chatPrompt.inputLine, Cmd_RawArgs() );
    }
	Key_SetDest( cls.key_dest | KEY_MESSAGE );
}

/*
================
Con_MessageMode2_f
================
*/
void Con_MessageMode2_f( void ) {
	con.chatTeam = qtrue;
    if( Cmd_Argc() > 1 ) {
        IF_Replace( &con.chatPrompt.inputLine, Cmd_RawArgs() );
    }
	Key_SetDest( cls.key_dest | KEY_MESSAGE );
}

/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize( void ) {
	int		width;

	con.vidWidth = scr_glconfig.vidWidth * con.scale;
	con.vidHeight = scr_glconfig.vidHeight * con.scale;

	width = ( con.vidWidth / con.charWidth ) - 2;

	if( width == con.linewidth )
		return;

	con.linewidth = width > CON_LINEWIDTH ? CON_LINEWIDTH : width;
	con.prompt.inputLine.visibleChars = con.linewidth;
	con.prompt.widthInChars = con.linewidth;
	con.chatPrompt.inputLine.visibleChars = con.linewidth;
}


static void con_param_changed( cvar_t *self ) {
	if( con.initialized && cls.ref_initialized ) {
		Con_SetupDC();
	}
}

static const cmdreg_t c_console[] = {
	{ "toggleconsole", Con_ToggleConsole_f },
	{ "togglechat", Con_ToggleChat_f },
	{ "messagemode", Con_MessageMode_f },
	{ "messagemode2", Con_MessageMode2_f },
	{ "clear", Con_Clear_f },
	{ "clearnotify", Con_ClearNotify_f },
	{ "condump", Con_Dump_f, Con_Dump_g },

    { NULL }
};

/*
================
Con_Init
================
*/
void Con_Init( void ) {
	memset( &con, 0, sizeof( con ) );

//
// register our commands
//
    Cmd_Register( c_console );

	con_notifytime = Cvar_Get( "con_notifytime", "3", 0 );
	con_clock = Cvar_Get( "con_clock", "0", CVAR_ARCHIVE );
	con_height = Cvar_Get( "con_height", "0.5", CVAR_ARCHIVE );
	con_speed = Cvar_Get( "scr_conspeed", "3", 0 );
	con_alpha = Cvar_Get( "con_alpha", "1", CVAR_ARCHIVE );
	con_scale = Cvar_Get( "con_scale", "1", CVAR_ARCHIVE );
	con_font = Cvar_Get( "con_font", "conchars", CVAR_ARCHIVE );
	con_font->changed = con_param_changed;
	con_background = Cvar_Get( "con_background", "conback", CVAR_ARCHIVE );
	con_background->changed = con_param_changed;
	con_scroll = Cvar_Get( "con_scroll", "0", CVAR_ARCHIVE );

	IF_Init( &con.prompt.inputLine, 1, MAX_FIELD_TEXT );
	IF_Init( &con.chatPrompt.inputLine, 1, MAX_FIELD_TEXT );

	con.prompt.Printf = Con_Printf;

	// use default width if no video initialized yet
	scr_glconfig.vidWidth = 640;
	scr_glconfig.vidHeight = 480;
	con.linewidth = -1;
	con.charWidth = 8;
	con.charHeight = 8;
	con.maxHeight = 1;
	con.scale = 1;

	Con_CheckResize();

	con.initialized = qtrue;
}

/*
================
Con_Shutdown
================
*/
void Con_Shutdown( void ) {
	Prompt_Clear( &con.prompt );
}


/*
===============
Con_Linefeed
===============
*/
void Con_Linefeed( void ) {
	con.x = 0;

	if( con.display == con.current )
		con.display++;
	con.current++;

	memset( con.text[con.current & CON_TOTALLINES_MASK], 0, sizeof( con.text[0] ) );

    if( con_scroll->integer & 2 ) {
    	con.display = con.current;
    }
}

/*
================
Con_Print

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be displayed on screen
If no console is visible, the text will appear at the top of the game window
================
*/
void Con_Print( const char *txt ) {
    int prevline;
	int color;
	static qboolean	cr;
	char *p;
	int		l;

	if( !con.initialized )
		return;

    prevline = con.current;

	color = 0;
	while( *txt ) {
	// count word length
		l = 0;
		p = ( char * )txt;
		while( *p > 32 || *p == Q_COLOR_ESCAPE ) {
			if( Q_IsColorString( p ) ) {
				p += 2;
			} else {
				l++; p++;
			}
		}

	// word wrap
		p = con.text[con.current & CON_TOTALLINES_MASK];
		if( l < con.linewidth && ( Q_DrawStrlen( p ) + l > con.linewidth ) )
			con.x = 0;

		if( cr ) {
			cr = qfalse;
			con.current--;
		}
		
		if( !con.x ) {
			Con_Linefeed();

		// add color from last line
			if( color ) {
				p = con.text[con.current & CON_TOTALLINES_MASK];
				p[con.x++] = Q_COLOR_ESCAPE;
				p[con.x++] = color;
			}
		}

		switch( *txt ) {
		case '\r':
			cr = qtrue;
		case '\n':
			con.x = 0;
			//color = 0;
			break;
		case Q_COLOR_ESCAPE:
			if( !txt[1] ) {
                break;
            }
            txt++;
			color = *txt;
            p = con.text[con.current & CON_TOTALLINES_MASK];
            p[con.x++] = Q_COLOR_ESCAPE;
            p[con.x++] = color;
            break;
		default:	// display character and advance
			p = con.text[con.current & CON_TOTALLINES_MASK];
			p[con.x++] = *txt;
			if( Q_DrawStrlen( p ) > con.linewidth )
				con.x = 0;
			break;
		}

		txt++;
		
	}

	// update time for transparent overlay
	if( !con.skipNotify ) {
        for( l = prevline + 1; l <= con.current; l++ ) {
    		con.times[l & CON_TIMES_MASK] = cls.realtime;
        }
    }

}

/*
================
Con_Printf

Print text to graphical console only,
bypassing system console and logfiles
================
*/
void Con_Printf( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

    Con_Print( msg );
}

/*
================
Con_SetupDC
================
*/
void Con_SetupDC( void ) {
	if( !( con.charsetImage = ref.RegisterFont( con_font->string ) ) ) {
        /* fall back to default */
		Com_WPrintf( "Couldn't load %s, falling back to default...\n", con_font->string );
		con.charsetImage = ref.RegisterFont( "conchars" );
	}
    if( !con.charsetImage ) {
        Com_Error( ERR_FATAL, "Couldn't load pics/conchars.pcx" );
    }
    ref.DrawGetFontSize( &con.charWidth, &con.charHeight, con.charsetImage );

	con.conbackImage = ref.RegisterPic( con_background->string );
	
}

/*
==============================================================================

DRAWING

==============================================================================
*/

#define CON_PRESTEP		( 10 + con.charHeight * 2 )

/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void Con_DrawNotify( void ) {
	int		v;
	char	*text;
	int		i;
	int		time;
	int		skip;
	float	alpha;
	int		flags;

	/* only draw notify in game */
	if( cls.state != ca_active ) {
		return; 
	}
	if( cls.key_dest & ( KEY_MENU|KEY_CONSOLE ) ) {
		return;
	}
	if( con.currentHeight ) {
		return;
	}

	flags = 0;

	v = 0;
	for( i = con.current - CON_TIMES + 1; i <= con.current; i++ ) {
		if( i < 0 )
			continue;
		time = con.times[i & CON_TIMES_MASK];
		if( time == 0 )
			continue;
		// alpha fade the last string left on screen
		alpha = SCR_FadeAlpha( time, con_notifytime->value * 1000, 300 );
		if( !alpha )
			continue;
		text = con.text[i & CON_TOTALLINES_MASK];

		if( v || i != con.current ) {
			alpha = 1;	// don't fade
		}

        ref.SetColor( DRAW_COLOR_ALPHA, ( byte * )&alpha );
		ref.DrawString( con.charWidth, v, flags, con.linewidth, text,
            con.charsetImage );

		v += con.charHeight;
	}
    
    ref.SetColor( DRAW_COLOR_CLEAR, NULL );

	if( cls.key_dest & KEY_MESSAGE ) {
		if( con.chatTeam ) {
			text = "say_team:";
			skip = 11;
		} else {
			text = "say:";
			skip = 5;
		}

		ref.DrawString( con.charWidth, v, flags, MAX_STRING_CHARS, text,
            con.charsetImage );
		IF_Draw( &con.chatPrompt.inputLine, skip * con.charWidth, v,
            flags | UI_DRAWCURSOR, con.charsetImage );

	}
	

}

/*
================
Con_DrawSolidConsole

Draws the console with the solid background
================
*/
void Con_DrawSolidConsole( void ) {
	int				i, y;
	int				rows;
	char			*text;
	int				row;
	char			buffer[CON_LINEWIDTH];
	int				vislines;
	float			alpha;
	int				flags;
	clipRect_t		clip;

	vislines = con.vidHeight * con.currentHeight;
	if( vislines <= 0 )
		return;

	if( vislines > con.vidHeight )
		vislines = con.vidHeight;

// setup transparency
	if( cls.state == ca_active &&
		con_alpha->value &&
        ( cls.key_dest & KEY_MENU ) == 0 )
    {
		alpha = 0.5f + 0.5f * ( con.currentHeight / con_height->value );

		Cvar_ClampValue( con_alpha, 0, 1 );
		alpha *= con_alpha->value;

		ref.SetColor( DRAW_COLOR_ALPHA, ( byte * )&alpha );
	}

	clip.left = 0;
	clip.top = 0;
	clip.right = 0;
	clip.bottom = 0;
	ref.SetClipRect( DRAW_CLIP_TOP, &clip );

// draw the background
	if( cls.state != ca_active || ( cls.key_dest & KEY_MENU ) || con_alpha->value ) {
		ref.DrawStretchPic( 0, vislines - con.vidHeight,
            con.vidWidth, con.vidHeight, con.conbackImage );
	}

// setup text rendering flags
	flags = 0;

	ref.SetColor( DRAW_COLOR_RGBA, colorCyan );

// draw clock
	if( con_clock->integer ) {
		extern void Com_Time_m( char *buffer, int bufferSize );

		Com_Time_m( buffer, sizeof( buffer ) );

		UIS_DrawStringEx( con.vidWidth - 8,
			vislines - CON_PRESTEP, flags | UI_RIGHT, MAX_STRING_CHARS,
			    buffer, con.charsetImage );
	}

// draw version
	UIS_DrawStringEx( con.vidWidth - 8,
		vislines - CON_PRESTEP + con.charHeight, flags | UI_RIGHT,
		    MAX_STRING_CHARS, APPLICATION " " VERSION, con.charsetImage );


// draw the text
	y = vislines - CON_PRESTEP;
	rows = y / con.charHeight + 1;		// rows of text to draw

// draw arrows to show the buffer is backscrolled
	if( con.display != con.current ) {
		ref.SetColor( DRAW_COLOR_RGBA, colorRed );
		for( i = 1; i < con.linewidth / 2; i += 4 ) {
			ref.DrawChar( i * con.charWidth, y, flags, '^', con.charsetImage );
		}
	
		y -= con.charHeight;
		rows--;
	}
	
// draw from the bottom up
	ref.SetColor( DRAW_COLOR_CLEAR, NULL );
	row = con.display;
	for( i = 0; i < rows; i++ ) {
		if( row < 0 )
			break;
		if( con.current - row > CON_TOTALLINES - 1 )
			break;		// past scrollback wrap point
			
		text = con.text[row & CON_TOTALLINES_MASK];

		ref.DrawString( con.charWidth, y, flags, con.linewidth, text,
                con.charsetImage );

		y -= con.charHeight;
		row--;

	}

//ZOID
	// draw the download bar
	// figure out width
	if( cls.download ) {
		int x, n, j;

		if( ( text = strrchr( cls.downloadname, '/') ) != NULL )
			text++;
		else
			text = cls.downloadname;

		x = con.linewidth - ( ( con.linewidth * 7 ) / 40 );
		y = x - strlen( text ) - 8;
		i = con.linewidth / 3;
		if ( strlen( text ) > i ) {
			y = x - i - 11;
			strncpy( buffer, text, i );
			buffer[i] = 0;
			strcat( buffer, "..." );
		} else {
			strcpy( buffer, text );
		}
		strcat( buffer, ": " );
		i = strlen( buffer );
		buffer[i++] = '\x80';
		// where's the dot go?
		n = y * cls.downloadpercent / 100;		
		for ( j = 0; j < y; j++ ) {
			if ( j == n ) {
				buffer[i++] = '\x83';
			} else {
				buffer[i++] = '\x81';
			}
		}
		buffer[i++] = '\x82';
		buffer[i] = 0;

		sprintf( buffer + i, " %02d%%", cls.downloadpercent );

		// draw it
		y = vislines - 10;
		ref.DrawString( con.charWidth, y, 0, CON_LINEWIDTH, buffer, con.charsetImage );
	}
//ZOID

// draw the input prompt, user text, and cursor if desired
	if( cls.key_dest & KEY_CONSOLE ) {
		y = vislines - CON_PRESTEP + con.charHeight;

		// draw it
		IF_Draw( &con.prompt.inputLine, 2 * con.charWidth, y,
                flags | UI_DRAWCURSOR, con.charsetImage );

		// draw command prompt
        i = 17;
        if( con.chatMode ) {
            i |= 128;
        } else {
    		ref.SetColor( DRAW_COLOR_RGBA, colorYellow );
        }
		ref.DrawChar( con.charWidth, y, flags, i, con.charsetImage );
	}

	// restore rendering parameters
    ref.SetColor( DRAW_COLOR_CLEAR, NULL );
	ref.SetClipRect( DRAW_CLIP_DISABLED, NULL );

}

//=============================================================================

void Con_SetMaxHeight( float frac ) {
	con.maxHeight = frac;
}

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole( void ) {
	Cvar_ClampValue( con_height, 0.1f, con.maxHeight );

	if( cls.state == ca_disconnected && !( cls.key_dest & KEY_MENU ) ) {
		/* draw fullscreen console */
		con.destHeight = con.maxHeight;
		con.currentHeight = con.destHeight;
		return;
	}

	if( cls.state > ca_disconnected && cls.state < ca_active ) {
		if( !cls.ui_initialized ) {
			/* draw half-screen console */
			con.destHeight = min( con.maxHeight, 0.5f );
			con.currentHeight = con.destHeight;
			return;
		}
	}

// decide on the height of the console
	if( cls.key_dest & KEY_CONSOLE ) {
		con.destHeight = con_height->value;		// half screen
	} else {
		con.destHeight = 0;				// none visible
	}

	if( con.currentHeight > con.destHeight ) {
		con.currentHeight -= con_speed->value * cls.frametime;
		if( con.currentHeight < con.destHeight ) {
			con.currentHeight = con.destHeight;
		}
	} else if( con.currentHeight < con.destHeight ) {
		con.currentHeight += con_speed->value * cls.frametime;
		if( con.currentHeight > con.destHeight ) {
			con.currentHeight = con.destHeight;
		}
	}
}

/*
==================
SCR_DrawConsole
==================
*/
void Con_DrawConsole( void ) {
	Cvar_ClampValue( con_scale, 1, 9 );

	con.scale = 1.0f / con_scale->value;
	ref.SetScale( &con.scale );

	Con_CheckResize();
	Con_DrawSolidConsole();
	Con_DrawNotify();	

	ref.SetScale( NULL );
}


/*
==============================================================================

			LINE TYPING INTO THE CONSOLE AND COMMAND COMPLETION

==============================================================================
*/

/*
====================
Key_Console

Interactive line editing and console scrollback
====================
*/
void Key_Console( int key ) {
	if( key == 'l' && Key_IsDown( K_CTRL ) ) {
		Con_Clear_f();
		return;
	}

	if( key == K_ENTER || key == K_KP_ENTER ) {
		char *cmd;
		
		if( !( cmd = Prompt_Action( &con.prompt ) ) ) {
			Con_Printf( "]\n" );
			return;
		}
		
		// backslash text are commands, else chat
		if( cmd[0] == '\\' || cmd[0] == '/' ) {
			Cbuf_AddText( cmd + 1 );	// skip slash
		} else {
			if( cls.state == ca_active && con.chatMode ) {
				Cbuf_AddText( va( "cmd say \"%s\"", cmd ) );
			} else {
				Cbuf_AddText( cmd );
			}
		}
		Cbuf_AddText( "\n" );

		Con_Printf( "]%s\n", cmd );
	
		if( cls.state == ca_disconnected ) {
			SCR_UpdateScreen ();	// force an update, because the command
									// may take some time
		}
		goto scroll;
	}

	if( key == K_TAB ) {
		Prompt_CompleteCommand( &con.prompt, qtrue );
		goto scroll;
	}

	if( key == K_UPARROW || ( key == 'p' && Key_IsDown( K_CTRL ) ) ) {
		Prompt_HistoryUp( &con.prompt );
		goto scroll;
	}

	if( key == K_DOWNARROW || ( key == 'n' && Key_IsDown( K_CTRL ) ) ) {
		Prompt_HistoryDown( &con.prompt );
		goto scroll;
	}

	if( key == K_PGUP || key == K_MWHEELUP ) {
		if( Key_IsDown( K_CTRL ) ) {
			con.display -= 6;
		} else {
			con.display -= 2;
		}

		if( con.display < 1 ) {
			con.display = 1;
		}
		return;
	}

	if( key == K_PGDN || key == K_MWHEELDOWN ) {
		if( Key_IsDown( K_CTRL ) ) {
			con.display += 6;
		} else {
			con.display += 2;
		}
		if( con.display > con.current ) {
			con.display = con.current;
		}
		return;
	}

	if( key == K_HOME && Key_IsDown( K_CTRL ) ) {
		con.display = 1;
		return;
	}

	if( key == K_END && Key_IsDown( K_CTRL ) ) {
		con.display = con.current;
		return;
	}

	IF_KeyEvent( &con.prompt.inputLine, key );

scroll: 
    if( con_scroll->integer & 1 ) {
    	con.display = con.current;
    }
}

void Char_Console( int key ) {
	IF_CharEvent( &con.prompt.inputLine, key );
}

/*
====================
Key_Message
====================
*/
void Key_Message( int key ) {
	if( key == 'l' && Key_IsDown( K_CTRL ) ) {
		IF_Clear( &con.chatPrompt.inputLine );
		return;
	}

	if( key == K_ENTER || key == K_KP_ENTER ) {
		char *cmd;
		
		if( ( cmd = Prompt_Action( &con.chatPrompt ) ) ) {
		    Cbuf_AddText( con.chatTeam ? "say_team \"" : "say \"" );
		    Cbuf_AddText( cmd );
		    Cbuf_AddText( "\"\n" );
        }
	
		Key_SetDest( cls.key_dest & ~KEY_MESSAGE );
		//IF_Clear( &con.chatPrompt.inputField );
		return;
	}

	if( key == K_ESCAPE ) {
		Key_SetDest( cls.key_dest & ~KEY_MESSAGE );
		IF_Clear( &con.chatPrompt.inputLine );
		return;
	}

	if( key == K_UPARROW || ( key == 'p' && Key_IsDown( K_CTRL ) ) ) {
		Prompt_HistoryUp( &con.chatPrompt );
		return;
	}

	if( key == K_DOWNARROW || ( key == 'n' && Key_IsDown( K_CTRL ) ) ) {
		Prompt_HistoryDown( &con.chatPrompt );
		return;
	}

	IF_KeyEvent( &con.chatPrompt.inputLine, key );
}

void Char_Message( int key ) {
	IF_CharEvent( &con.chatPrompt.inputLine, key );
}



