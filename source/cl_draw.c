/*
Copyright (C) 2003-2006 Andrey Nazarov

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

//
// cl_draw.c - draw all 2D elements during gameplay
//

cvar_t      *scr_draw2d;
cvar_t      *scr_showturtle;
cvar_t		*scr_showfollowing;
cvar_t		*scr_showstats;
cvar_t		*scr_lag_x;
cvar_t		*scr_lag_y;
cvar_t		*scr_lag_draw;
cvar_t		*scr_alpha;

#define SCR_DrawString( x, y, flags, string ) \
	UIS_DrawStringEx( x, y, flags, MAX_STRING_CHARS, string, scr_font )

/*
==============
SCR_LoadingString
==============
*/
void SCR_LoadingString( const char *string ) {
    if( cls.state != ca_loading ) {
        return;
    }
	Q_strncpyz( cl.loadingString, string, sizeof( cl.loadingString ) );
	
	if( string[0] ) {
		Con_Printf( "Loading %s...\r", string );
	} else {
		Con_Printf( "\n" );
	}

	SCR_UpdateScreen();
	Com_ProcessEvents();	
}

/*
===============================================================================

STAT PROGRAMS TO TEXT

===============================================================================
*/

#define TH_WIDTH    80
#define TH_HEIGHT   40

static void TH_DrawString( char *dst, int x, int y, char *src, size_t len ) {
    int c;

    if( x + len > TH_WIDTH ) {
        len = TH_WIDTH - x;
    }

    dst += y * ( TH_WIDTH + 1 ) + x;
    while( len-- ) {
        c = *src++;
        c &= 127;
        switch( c ) {
        case 16: c = '['; break;
        case 17: c = ']'; break;
        case 29: c = '<'; break;
        case 30: c = '='; break;
        case 31: c = '>'; break;
        default:
            if( c < 32 ) {
                c = 32;
            }
            break;
        }
        *dst++ = c;
    }
}

static void TH_DrawCenterString( char *dst, int x, int y, char *src, size_t len ) {
    x -= len / 2;
    if( x < 0 ) {
        src -= x;
        x = 0;
    }

    TH_DrawString( dst, x, y, src, len );
}

static void TH_DrawNumber( char *dst, int x, int y, int width, int value ) {
    char num[16];
    int l;

	if( width < 1 )
		return;

	// draw number string
	if( width > 5 )
		width = 5;

	l = Com_sprintf( num, sizeof( num ), "%d", value );
	if( l > width )
		l = width;
	x += width - l;

	TH_DrawString( dst, x, y, num, l );
}

static void TH_DrawLayoutString( char *dst, const char *s ) {
    char	buffer[MAX_QPATH];
	int		x, y;
	int		value;
	char	*token;
	size_t	len;
	int		width, index;
	clientinfo_t	*ci;

	if( !s[0] )
		return;

	x = 0;
	y = 0;
	width = 3;

	while( s ) {
		token = COM_Parse( &s );
		if( token[2] == 0 ) {
			if( token[0] == 'x' ) {
				if( token[1] == 'l' ) {
					token = COM_Parse( &s );
					x = atoi( token ) / 8;
					continue;
				}

				if( token[1] == 'r' ) {
					token = COM_Parse( &s );
					x = TH_WIDTH + atoi( token ) / 8;
					continue;
				}

				if( token[1] == 'v' ) {
					token = COM_Parse( &s );
					x = TH_WIDTH / 2 - 20 + atoi( token ) / 8;
					continue;
				}
			}

			if( token[0] == 'y' ) {
				if( token[1] == 't' ) {
					token = COM_Parse( &s );
					y = atoi( token ) / 8;
					continue;
				}

				if( token[1] == 'b' ) {
					token = COM_Parse( &s );
					y = TH_HEIGHT + atoi( token ) / 8;
					continue;
				}

				if( token[1] == 'v' ) {
					token = COM_Parse( &s );
					y = TH_HEIGHT / 2 - 15 + atoi( token ) / 8;
					continue;
				}
			}
		}

		if( !strcmp( token, "pic" ) ) {	
			// draw a pic from a stat number
			COM_Parse( &s );
			continue;
		}

		if( !strcmp( token, "client" ) ) {	
			// draw a deathmatch client block
			int		score, ping, time;

			token = COM_Parse( &s );
			x = TH_WIDTH / 2 - 20 + atoi( token ) / 8;
			token = COM_Parse( &s );
			y = TH_HEIGHT / 2 - 15 + atoi( token ) / 8;

			token = COM_Parse( &s );
			value = atoi( token );
			if( value < 0 || value >= MAX_CLIENTS ) {
				Com_Error( ERR_DROP, "%s: invalid client index", __func__ );
			}
			ci = &cl.clientinfo[value];

			token = COM_Parse( &s );
			score = atoi( token );

			token = COM_Parse( &s );
			ping = atoi( token );

			token = COM_Parse( &s );
			time = atoi( token );

            len = strlen( ci->name );
			TH_DrawString( dst, x + 4, y, ci->name, len );
            len = Com_sprintf( buffer, sizeof( buffer ), "Score: %i", score ); 
			TH_DrawString( dst, x + 4, y + 1, buffer, len );
            len = Com_sprintf( buffer, sizeof( buffer ), "Ping:  %i", ping ); 
			TH_DrawString( dst, x + 4, y + 2, buffer, len );
            len = Com_sprintf( buffer, sizeof( buffer ), "Time:  %i", time ); 
			TH_DrawString( dst, x + 4, y + 3, buffer, len );
			continue;
		}

		if( !strcmp( token, "ctf" ) ) {	
			// draw a ctf client block
			int		score, ping;

			token = COM_Parse( &s );
			x = TH_WIDTH / 2 - 20 + atoi( token ) / 8;
			token = COM_Parse( &s );
			y = TH_HEIGHT / 2 - 15 + atoi( token ) / 8;

			token = COM_Parse( &s );
			value = atoi( token );
			if( value < 0 || value >= MAX_CLIENTS ) {
				Com_Error( ERR_DROP, "%s: invalid client index", __func__ );
			}
			ci = &cl.clientinfo[value];

			token = COM_Parse( &s );
			score = atoi( token );

			token = COM_Parse( &s );
			ping = atoi( token );
			if( ping > 999 )
				ping = 999;

			len = Com_sprintf( buffer, sizeof( buffer ), "%3d %3d %-12.12s",
                score, ping, ci->name );
			TH_DrawString( dst, x, y, buffer, len );
			continue;
		}

		if( !strcmp( token, "picn" ) ) {	
			// draw a pic from a name
			COM_Parse( &s );
			continue;
		}

		if( !strcmp( token, "num" ) ) {	
			// draw a number
			token = COM_Parse( &s );
			width = atoi( token );
			token = COM_Parse( &s );
			value = atoi( token );
			if( value < 0 || value >= MAX_STATS ) {
				Com_Error( ERR_DROP, "%s: invalid stat index", __func__ );
			}
			value = cl.frame.ps.stats[value];
			TH_DrawNumber( dst, x, y, width, value );
			continue;
		}

		if( !strcmp( token, "stat_string" ) ) {
			token = COM_Parse( &s );
			index = atoi( token );
			if( index < 0 || index >= MAX_STATS ) {
				Com_Error( ERR_DROP, "%s: invalid string index", __func__ );
			}
			index = cl.frame.ps.stats[index];
			if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
				Com_Error( ERR_DROP, "%s: invalid string index", __func__ );
			}
            len = strlen( cl.configstrings[index] );
			TH_DrawString( dst, x, y, cl.configstrings[index], len );
			continue;
		}

		if( !strncmp( token, "cstring", 7 ) ) {
			token = COM_Parse( &s );
            len = strlen( token );
			TH_DrawCenterString( dst, x + 40 / 2, y, token, len );
			continue;
		}

		if( !strncmp( token, "string", 6 ) ) {
			token = COM_Parse( &s );
            len = strlen( token );
			TH_DrawString( dst, x, y, token, len );
			continue;
		}

		if( !strcmp( token, "if" ) ) {
			token = COM_Parse( &s );
			value = atoi( token );
			if( value < 0 || value >= MAX_STATS ) {
				Com_Error( ERR_DROP, "%s: invalid stat index", __func__ );
			}
			value = cl.frame.ps.stats[value];
			if( !value ) {	// skip to endif
				while( strcmp( token, "endif" ) ) {
					token = COM_Parse( &s );
					if( !s ) {
						break;
					}
				}
			}
			continue;
		}
	}
}

static void SCR_ScoreShot_f( void ) {
    char buffer[( TH_WIDTH + 1 ) * TH_HEIGHT];
	char path[MAX_QPATH];
	fileHandle_t f;
    int i;

    if( cls.state != ca_active ) {
		Com_Printf( "Must be in a level.\n" );
        return;
    }

	if( Cmd_Argc() > 1 ) {
	    Q_concat( path, sizeof( path ), SCORESHOTS_DIRECTORY "/", Cmd_Argv( 1 ), NULL );
    	COM_AppendExtension( path, ".txt", sizeof( path ) );
    } else {
        for( i = 0; i < 1000; i++ ) {
            Com_sprintf( path, sizeof( path ), SCORESHOTS_DIRECTORY "/quake%03d.txt", i );
            if( FS_LoadFileEx( path, NULL, FS_PATH_GAME ) == INVALID_LENGTH ) {
                break;	// file doesn't exist
            }
        }

        if( i == 1000 ) {
            Com_Printf( "All scoreshot slots are full.\n" );
            return;
        }
    }

	FS_FOpenFile( path, &f, FS_MODE_WRITE );
	if( !f ) {
		Com_EPrintf( "Couldn't open %s for writing.\n", path );
		return;
	}

    memset( buffer, ' ', sizeof( buffer ) );
    for( i = 0; i < TH_HEIGHT; i++ ) {
        buffer[ i * ( TH_WIDTH + 1 ) + TH_WIDTH ] = '\n';
    }

    TH_DrawLayoutString( buffer, cl.configstrings[CS_STATUSBAR] );
    TH_DrawLayoutString( buffer, cl.layout );

    FS_Write( buffer, sizeof( buffer ), f );

	FS_FCloseFile( f );

	Com_Printf( "Wrote %s.\n", path );
}


/*
===============================================================================

LAGOMETER

===============================================================================
*/

#define LAG_WIDTH     48
#define LAG_HEIGHT    48

#define LAG_MAX     200

#define LAG_CRIT_BIT	( 1 << 31 )
#define LAG_WARN_BIT	( 1 << 30 )

#define LAG_BASE    0xD5
#define LAG_WARN    0xDC
#define LAG_CRIT    0xF2

static struct {
	int samples[LAG_WIDTH];
    int head;
} lag;

void SCR_LagClear( void ) {
	lag.head = 0;
}

void SCR_LagSample( void ) {
    int i = cls.netchan->incoming_acknowledged & CMD_MASK;
    client_history_t *h = &cl.history[i];
    int ping = cls.realtime - h->sent;

    h->rcvd = cls.realtime;
    if( !h->cmdNumber ) {
        return;
    }

    for( i = 0; i < cls.netchan->dropped; i++ ) {
        lag.samples[lag.head % LAG_WIDTH] = LAG_MAX | LAG_CRIT_BIT;
    	lag.head++;
    }

    if( cl.frameflags & FF_SURPRESSED ) {
        ping |= LAG_WARN_BIT;
    }
    lag.samples[lag.head % LAG_WIDTH] = ping;
	lag.head++;
}

static void SCR_LagDraw( int x, int y ) {
	int i, j, v, c;

	for( i = 0; i < LAG_WIDTH; i++ ) {
		j = lag.head - i - 1;
		if( j < 0 ) {
			break;
		}

		v = lag.samples[j % LAG_WIDTH];

		if( v & LAG_CRIT_BIT ) {
			c = LAG_CRIT;
		} else if( v & LAG_WARN_BIT ) {
			c = LAG_WARN;
		} else {
		    c = LAG_BASE;
        }

		v &= ~(LAG_WARN_BIT|LAG_CRIT_BIT);
		v = v * LAG_HEIGHT / LAG_MAX;
		if( v > LAG_HEIGHT ) {
			v = LAG_HEIGHT;
		}

		ref.DrawFill( x + LAG_WIDTH - i - 1, y + LAG_HEIGHT - v, 1, v, c );
	}
}

/*
===============================================================================

DRAW OBJECTS

===============================================================================
*/

typedef struct {
    list_t  entry;
    int     x, y;
    //int     type;
    //union {
        cvar_t *cvar;
        cmd_macro_t *macro;
    //    int stat;
    //};
    unsigned color;
} drawobj_t;

static list_t scr_objects;

static void SCR_Color_g( genctx_t *ctx ) {
    int color;

    for( color = 0; color < 10; color++ ) {
        if( !Prompt_AddMatch( ctx, colorNames[color] ) ) {
            break;
        }
    }
}

static void SCR_Draw_c( genctx_t *ctx, int argnum ) {
    if( argnum == 1 ) {
        Cvar_Variable_g( ctx );
        Cmd_Macro_g( ctx );
    } else if( argnum == 4 ) {
        SCR_Color_g( ctx );
    }
}

// draw cl_fps -1 80
static void SCR_Draw_f( void ) {
    int x, y;
    const char *s, *c;
    drawobj_t *obj;
    cmd_macro_t *macro;
   // int stat;
    int color = COLOR_RESET;
    int argc = Cmd_Argc();

    if( argc == 1 ) {
        if( LIST_EMPTY( &scr_objects ) ) {
            Com_Printf( "No draw strings registered.\n" );
            return;
        }
        Com_Printf( "Name               X    Y\n"
                    "--------------- ---- ----\n" );
        LIST_FOR_EACH( drawobj_t, obj, &scr_objects, entry ) {
            s = obj->macro ? obj->macro->name : obj->cvar->name;
            Com_Printf( "%-15s %4d %4d\n", s, obj->x, obj->y );
        }
        return;
    }

    if( argc < 4 ) {
        Com_Printf( "Usage: %s <name> <x> <y> [color]\n", Cmd_Argv( 0 ) );
        return;
    }

    s = Cmd_Argv( 1 );
    x = atoi( Cmd_Argv( 2 ) );
    y = atoi( Cmd_Argv( 3 ) );

    if( argc > 4 ) {
        c = Cmd_Argv( 4 );
        for( color = 0; color < 10; color++ ) {
            if( !strcmp( colorNames[color], c ) ) {
                break;
            }
        }
        if( color == 10 ) {
            Com_Printf( "Unknown color '%s'\n", c );
            return;
        }
    }

    obj = Z_Malloc( sizeof( *obj ) );
    obj->x = x;
    obj->y = y;
    obj->color = color;

#if 0
    if( *s == '!' || *s == '#' ) {
        stat = atoi( s + 1 );
        if( stat < 0 || stat >= MAX_STATS ) {
            Com_Printf( "Invalid stat index: %d\n", stat );
        }
        obj->stat = stat;
        obj->cvar = NULL;
        obj->macro = NULL;
    } else
#endif
    {
        macro = Cmd_FindMacro( s );
        if( macro ) {
            obj->cvar = NULL;
            obj->macro = macro;
        } else {
            obj->cvar = Cvar_Get( s, "", CVAR_USER_CREATED );
            obj->macro = NULL;
        }
    }

    List_Append( &scr_objects, &obj->entry );
}

static void SCR_Draw_g( genctx_t *ctx ) {
    drawobj_t *obj;
    const char *s;

    if( LIST_EMPTY( &scr_objects ) ) {
        return;
    }

    Prompt_AddMatch( ctx, "all" );
    
    LIST_FOR_EACH( drawobj_t, obj, &scr_objects, entry ) {
        s = obj->macro ? obj->macro->name : obj->cvar->name;
        if( !Prompt_AddMatch( ctx, s ) ) {
            break;
        }
    }
}

static void SCR_UnDraw_c( genctx_t *ctx, int argnum ) {
    if( argnum == 1 ) {
        SCR_Draw_g( ctx );
    }
}

static void SCR_UnDraw_f( void ) {
    char *s;
    drawobj_t *obj, *next;
    cmd_macro_t *macro;
    cvar_t *cvar;
    qboolean deleted;

    if( Cmd_Argc() != 2 ) {
        Com_Printf( "Usage: %s <name>\n", Cmd_Argv( 0 ) );
        return;
    }

    if( LIST_EMPTY( &scr_objects ) ) {
        Com_Printf( "No draw strings registered.\n" );
        return;
    }

    s = Cmd_Argv( 1 );
    if( !strcmp( s, "all" ) ) {
        LIST_FOR_EACH_SAFE( drawobj_t, obj, next, &scr_objects, entry ) {
            Z_Free( obj );
        }
        List_Init( &scr_objects );
        Com_Printf( "Deleted all draw strings.\n" );
        return;
    }

    cvar = NULL;
	macro = Cmd_FindMacro( s );
    if( !macro ) {
        cvar = Cvar_Get( s, "", CVAR_USER_CREATED );
    }

    deleted = qfalse;
    LIST_FOR_EACH_SAFE( drawobj_t, obj, next, &scr_objects, entry ) {
        if( obj->macro == macro && obj->cvar == cvar ) {
            List_Remove( &obj->entry );
            Z_Free( obj );
            deleted = qtrue;
        }
    }

    if( !deleted ) {
        Com_Printf( "Draw string '%s' not found.\n", s );
    }
}

static void draw_objects( void ) {
    char buffer[MAX_QPATH];
    int x, y, flags;
    drawobj_t *obj;

    LIST_FOR_EACH( drawobj_t, obj, &scr_objects, entry ) {
        x = obj->x;
        y = obj->y;
        flags = 0;
        if( x < 0 ) {
            x += scr_hudWidth + 1;
            flags |= UI_RIGHT;
        }
        if( y < 0 ) {
            y += scr_hudHeight - CHAR_HEIGHT + 1;
        }
        if( obj->color == 8 ) {
            flags |= UI_ALTCOLOR;
        } else if( obj->color < 8 ) {
            ref.SetColor( DRAW_COLOR_RGB, colorTable[obj->color] );
        }
        if( obj->macro ) {
            obj->macro->function( buffer, sizeof( buffer ) );
            SCR_DrawString( x, y, flags, buffer );
        } else {
            SCR_DrawString( x, y, flags, obj->cvar->string );
        }
        ref.SetColor( DRAW_COLOR_CLEAR, NULL );
    }
}


/*
===============================================================================

CHAT HUD

===============================================================================
*/

#if USE_CHATHUD

#define MAX_CHAT_LENGTH		128
#define MAX_CHAT_LINES		32
#define CHAT_MASK			( MAX_CHAT_LINES - 1 )

typedef struct chatMessage_s {
	char	text[MAX_CHAT_LENGTH];
	int		time;
} chatMessage_t;

static chatMessage_t	scr_chatMsgs[MAX_CHAT_LINES];
static int				scr_currentChatMsg;

/*
==============
SCR_ClearChatHUD_f
==============
*/
void SCR_ClearChatHUD_f( void ) {
	memset( scr_chatMsgs, 0, sizeof( scr_chatMsgs ) );
	scr_currentChatMsg = 0;
}

/*
==============
SCR_AddToChatHUD
==============
*/
void SCR_AddToChatHUD( const char *string ) {
	chatMessage_t *msg;
	char *p;

	scr_currentChatMsg++;
	msg = &scr_chatMsgs[scr_currentChatMsg & CHAT_MASK];

	Q_strncpyz( msg->text, string, sizeof( msg->text ) );
	msg->time = cls.realtime;

	// clamp to single line
	p = strchr( msg->text, '\n' );
	if( p ) {
		*p = 0;
	}
}

#endif

// ============================================================================

/*
=================
SCR_FadeAlpha
=================
*/
float SCR_FadeAlpha( unsigned startTime, unsigned visTime, unsigned fadeTime ) {
	float alpha;
    unsigned timeLeft, delta = cls.realtime - startTime;

	if( delta >= visTime ) {
		return 0;
	}

	if( fadeTime > visTime ) {
		fadeTime = visTime;
	}

	alpha = 1;
	timeLeft = visTime - delta;
	if( timeLeft < fadeTime ) {
		alpha = ( float )timeLeft / fadeTime;
	}

	return alpha;
}

static void draw_crosshair( void ) {
	int x, y;

    if( crosshair->modified ) {
        crosshair->modified = qfalse;
        SCR_TouchPics();
    }

    x = ( scr_hudWidth - crosshair_width ) / 2;
    y = ( scr_hudHeight - crosshair_height ) / 2;
    ref.DrawPic( x, y, crosshair_pic );
}

static void draw_following( void ) {
	char *string;
	int x;

	if( !scr_showfollowing->integer ) {
		return;
	}

	if( !cls.demoplayback && cl.frame.clientNum == cl.clientNum ) {
		return;
	}
    if( cl.frame.ps.stats[STAT_LAYOUTS] ) {
        return;
    }

	string = cl.clientinfo[cl.frame.clientNum].name;
	if( !string[0] ) {
		return;
	}

	x = ( scr_hudWidth - strlen( string ) * CHAR_WIDTH ) / 2;

	ref.DrawString( x, 48, 0, MAX_STRING_CHARS, string, scr_font );
}

static void draw_turtle( void ) {
    int x = 8;
    int y = scr_hudHeight - 88;

#define DF( f ) if( cl.frameflags & FF_ ## f ) { \
                    SCR_DrawString( x, y, UI_ALTCOLOR, #f ); \
                    y += 8; \
                }

    DF( SURPRESSED )
    DF( CLIENTPRED )
    else
    DF( CLIENTDROP )
    DF( SERVERDROP )
    DF( BADFRAME )
    DF( OLDFRAME )
    DF( OLDENT )
    DF( NODELTA )

#undef DF
}

static void SCR_DrawStats( void ) {
    char buffer[MAX_QPATH];
    int i, j;
    int x, y;

    j = scr_showstats->integer;
    if( j > MAX_STATS ) {
        j = MAX_STATS;
    }
    x = 8;
    y = ( scr_hudHeight - j * CHAR_HEIGHT ) / 2;
    for( i = 0; i < j; i++ ) {
        Com_sprintf( buffer, sizeof( buffer ), "%2d: %d", i, cl.frame.ps.stats[i] );
        if( cl.oldframe.ps.stats[i] != cl.frame.ps.stats[i] ) {
            ref.SetColor( DRAW_COLOR_RGBA, colorRed );
        }
        ref.DrawString( x, y, 0, MAX_STRING_CHARS, buffer, scr_font );
        ref.SetColor( DRAW_COLOR_CLEAR, NULL );
        y += CHAR_HEIGHT;
    }
}

/*
=================
SCR_Draw2D
=================
*/
void SCR_Draw2D( void ) {
    clipRect_t rc;
    int x, y;

	if( !scr_draw2d->integer || ( cls.key_dest & KEY_MENU ) ) {
		return;
	}
    
    // avoid DoS by making sure nothing is drawn out of bounds
    if( scr_glconfig.renderer == GL_RENDERER_SOFTWARE ) {
        rc.left = 0;
        rc.top = 0;
        rc.right = scr_hudWidth;
        rc.bottom = scr_hudHeight;

        ref.SetClipRect( DRAW_CLIP_MASK, &rc );
    }

	ref.SetColor( DRAW_COLOR_CLEAR, NULL );

    if( crosshair->integer ) {
    	draw_crosshair();
    }

	Cvar_ClampValue( scr_alpha, 0, 1 );
	ref.SetColor( DRAW_COLOR_ALPHA, ( byte * )&scr_alpha->value );

    if( scr_draw2d->integer > 1 ) {
    	SCR_ExecuteLayoutString( cl.configstrings[CS_STATUSBAR] );
    }

	if( ( cl.frame.ps.stats[STAT_LAYOUTS] & 1 ) ||
        ( cls.demoplayback && Key_IsDown( K_F1 ) ) )
    {
		SCR_ExecuteLayoutString( cl.layout );
	}

	if( cl.frame.ps.stats[STAT_LAYOUTS] & 2 ) {
		SCR_DrawInventory();
	}

	if( cl.frame.clientNum != CLIENTNUM_NONE ) {
		draw_following();
	}

	SCR_DrawCenterString();

    x = scr_lag_x->integer;
    y = scr_lag_y->integer;

    if( x < 0 ) {
        x += scr_hudWidth - LAG_WIDTH + 1;
    }
    if( y < 0 ) {
        y += scr_hudHeight - LAG_HEIGHT + 1;
    }

	// draw ping graph
    if( scr_lag_draw->integer ) {
        if( scr_lag_draw->integer > 1 ) {
	        ref.DrawFill( x, y, LAG_WIDTH, LAG_HEIGHT, 4 );
        }
        SCR_LagDraw( x, y );
    }

	// draw phone jack
	if( cls.netchan && cls.netchan->outgoing_sequence - cls.netchan->incoming_acknowledged >= CMD_BACKUP ) {
		if( ( cls.realtime >> 8 ) & 3 ) {
			ref.DrawStretchPic( x, y, LAG_WIDTH, LAG_HEIGHT, scr_net );
		}
	}

    draw_objects();

	ref.SetColor( DRAW_COLOR_CLEAR, NULL );

    if( scr_showturtle->integer && cl.frameflags ) {
        draw_turtle();
    }

    if( scr_showstats->integer ) {
        SCR_DrawStats();
    }

    SCR_DrawPause();

    if( scr_glconfig.renderer == GL_RENDERER_SOFTWARE ) {
        ref.SetClipRect( DRAW_CLIP_DISABLED, NULL );
    }
}

cmdreg_t scr_drawcmds[] = {
#if USE_CHATHUD
    { "clearchat", SCR_ClearChatHUD_f },
#endif
    { "draw", SCR_Draw_f, SCR_Draw_c },
    { "undraw", SCR_UnDraw_f, SCR_UnDraw_c },
    { "scoreshot", SCR_ScoreShot_f },
    { NULL }
};

/*
================
SCR_InitDraw
================
*/
void SCR_InitDraw( void ) {
    List_Init( &scr_objects );

	scr_draw2d = Cvar_Get( "scr_draw2d", "2", 0 );
	scr_showturtle = Cvar_Get( "scr_showturtle", "1", 0 );
	scr_showfollowing = Cvar_Get( "scr_showfollowing", "1", 0 );
	scr_lag_x = Cvar_Get( "scr_lag_x", "-1", 0 );
	scr_lag_y = Cvar_Get( "scr_lag_y", "-1", 0 );
    scr_lag_draw = Cvar_Get( "scr_lag_draw", "0", 0 );
	scr_alpha = Cvar_Get( "scr_alpha", "1", 0 );
	scr_showstats = Cvar_Get( "scr_showstats", "0", 0 );

    Cmd_Register( scr_drawcmds );
}

