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
cvar_t		*scr_showpmove;
cvar_t		*scr_lag_x;
cvar_t		*scr_lag_y;
cvar_t		*scr_lag_draw;
cvar_t		*scr_alpha;

#define SCR_DrawString( x, y, flags, string ) \
	SCR_DrawStringEx( x, y, flags, MAX_STRING_CHARS, string, scr_font )

/*
==============
SCR_DrawStringEx
==============
*/
int SCR_DrawStringEx( int x, int y, int flags, size_t maxlen,
                      const char *s, qhandle_t font )
{
    int w = Q_DrawStrlenTo( s, maxlen ) * CHAR_WIDTH;

    if( ( flags & UI_CENTER ) == UI_CENTER ) {
        x -= w / 2;
    } else if( flags & UI_RIGHT ) {
        x -= w;
    }

    return R_DrawString( x, y, flags, maxlen, s, font );
}


/*
==============
SCR_DrawStringMulti
==============
*/
void SCR_DrawStringMulti( int x, int y, int flags, size_t maxlen,
                          const char *s, qhandle_t font )
{
	char	*p;
	size_t	len;

	while( *s ) {
        p = strchr( s, '\n' );
        if( !p ) {
    	    SCR_DrawStringEx( x, y, flags, maxlen, s, font );
            break;
        }

        len = p - s;
        if( len > maxlen ) {
            len = maxlen;
        }
    	SCR_DrawStringEx( x, y, flags, len, s, font );

		y += CHAR_HEIGHT;
		s = p + 1;
	}
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

		R_DrawFill( x + LAG_WIDTH - i - 1, y + LAG_HEIGHT - v, 1, v, c );
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
    cvar_t *cvar;
    cmd_macro_t *macro;
    int flags;
    color_t color;
} drawobj_t;

static LIST_DECL( scr_objects );

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
    color_t color = { 0, 0, 0, 0 };
    int flags = UI_IGNORECOLOR;
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
    if( x < 0 ) {
        flags |= UI_RIGHT;
    }
    y = atoi( Cmd_Argv( 3 ) );

    if( argc > 4 ) {
        c = Cmd_Argv( 4 );
        if( !strcmp( c, "alt" ) ) {
            flags |= UI_ALTCOLOR;
        } else {
            if( !COM_ParseColor( c, color ) ) {
                Com_Printf( "Unknown color '%s'\n", c );
                return;
            }
            flags &= ~UI_IGNORECOLOR;
        }
    }

    obj = Z_Malloc( sizeof( *obj ) );
    obj->x = x;
    obj->y = y;
    obj->flags = flags;
    *( uint32_t * )obj->color = *( uint32_t * )color;

    macro = Cmd_FindMacro( s );
    if( macro ) {
        obj->cvar = NULL;
        obj->macro = macro;
    } else {
        obj->cvar = Cvar_Ref( s );
        obj->macro = NULL;
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
        cvar = Cvar_Ref( s );
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
    int x, y;
    drawobj_t *obj;

    LIST_FOR_EACH( drawobj_t, obj, &scr_objects, entry ) {
        x = obj->x;
        y = obj->y;
        if( x < 0 ) {
            x += scr_hudWidth + 1;
        }
        if( y < 0 ) {
            y += scr_hudHeight - CHAR_HEIGHT + 1;
        }
        if( !( obj->flags & UI_IGNORECOLOR ) ) {
            R_SetColor( DRAW_COLOR_RGBA, obj->color );
        }
        if( obj->macro ) {
            obj->macro->function( buffer, sizeof( buffer ) );
            SCR_DrawString( x, y, obj->flags, buffer );
        } else {
            SCR_DrawString( x, y, obj->flags, obj->cvar->string );
        }
        R_SetColor( DRAW_COLOR_CLEAR, NULL );
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

	Q_strlcpy( msg->text, string, sizeof( msg->text ) );
	msg->time = cls.realtime;

	// clamp to single line
	p = strchr( msg->text, '\n' );
	if( p ) {
		*p = 0;
	}
}

#endif

/*
=============================================================================

CONNECTION / LOADING SCREEN

=============================================================================
*/

void SCR_DrawLoading( void ) {
	char *s;
	int x, y;
    int i;
	qhandle_t h;

    if( !cl.mapname[0] || !( h = R_RegisterPic( va( "*levelshots/%s.jpg", cl.mapname ) ) ) ) {
        R_DrawFill( 0, 0, scr_glconfig.vidWidth, scr_glconfig.vidHeight, 0 );
    } else {
        R_DrawStretchPic( 0, 0, scr_glconfig.vidWidth, scr_glconfig.vidHeight, h );
    }

	x = scr_glconfig.vidWidth / 2;
	y = 8;
    
    s = va( "%s %s", cls.demo.playback ? "Playing back " :
        "Connecting to ", cls.servername );
	SCR_DrawString( x, y, UI_CENTER|UI_DROPSHADOW, s );
	y += 16;

    s = cl.configstrings[CS_NAME];
	if( *s ) {
        R_SetColor( DRAW_COLOR_RGB, colorYellow );
		SCR_DrawString( x, y, UI_CENTER|UI_DROPSHADOW, s );
        R_SetColor( DRAW_COLOR_CLEAR, NULL );
	}
	y += 16;

    y = scr_glconfig.vidHeight / 2 - 45;
	x -= 136;
    for( i = ca_challenging; i < cls.state; i++ ) {
        switch( i ) {
        case ca_challenging:
            s = "challenge";
            break;
        case ca_connecting:
            s = "connection";
            break;
        case ca_connected:
            s = "server data";
            break;
        default:
            continue;
        }

	    SCR_DrawString( x, y, 0, va( "Receiving %s...", s ) );
        if( cls.state > i ) {
	        SCR_DrawString( x + 256, y, 0, "ok" );
        }
        y += 10;
    }

	if( cls.state >= ca_loading ) {
        for( i = 0; i <= cl.load_state; i++ ) {
            if( !cl.load_time[i] ) {
                continue;
            }
            switch( i ) {
            case LOAD_MAP:
                s = cl.configstrings[ CS_MODELS + 1 ];
                break;
            case LOAD_MODELS:
                s = "models";
                break;
            case LOAD_IMAGES:
                s = "images";
                break;
            case LOAD_CLIENTS:
                s = "clients";
                break;
            case LOAD_SOUNDS:
                s = "sounds";
                break;
            default:
                continue;
            }
            SCR_DrawString( x, y, 0, va( "Loading %s...", s ) ); 
            if( cl.load_state > i ) {
                unsigned ms = cl.load_time[ i + 1 ] - cl.load_time[i];
                SCR_DrawString( x + 224, y, 0, va( "%3u ms", ms ) );
            }
            y += 10;
        }
    }

	if( cls.state >= ca_precached ) {
	    SCR_DrawString( x, y, 0, "Receiving server frame..." );
    }

	// draw message string
	if( cls.state < ca_connected && cls.messageString[0] ) {
	    x = scr_glconfig.vidWidth / 2;
        R_SetColor( DRAW_COLOR_RGB, colorRed );
		SCR_DrawStringMulti( x, y + 16, UI_CENTER,
            MAX_STRING_CHARS, cls.messageString, scr_font );
        R_SetColor( DRAW_COLOR_CLEAR, NULL );
	}
}

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
    R_DrawPic( x, y, crosshair_pic );
}

static void draw_following( void ) {
	char *string;
	int x;

	if( !scr_showfollowing->integer ) {
		return;
	}

	if( !cls.demo.playback && cl.frame.clientNum == cl.clientNum ) {
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

	R_DrawString( x, 2, 0, MAX_STRING_CHARS, string, scr_font );
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
    x = CHAR_WIDTH;
    y = ( scr_hudHeight - j * CHAR_HEIGHT ) / 2;
    for( i = 0; i < j; i++ ) {
        Q_snprintf( buffer, sizeof( buffer ), "%2d: %d", i, cl.frame.ps.stats[i] );
        if( cl.oldframe.ps.stats[i] != cl.frame.ps.stats[i] ) {
            R_SetColor( DRAW_COLOR_RGBA, colorRed );
        }
        R_DrawString( x, y, 0, MAX_STRING_CHARS, buffer, scr_font );
        R_SetColor( DRAW_COLOR_CLEAR, NULL );
        y += CHAR_HEIGHT;
    }
}

static void SCR_DrawPmove( void ) {
	static const char * const types[] = {
        "NORMAL", "SPECTATOR", "DEAD", "GIB", "FREEZE"
    };
	static const char * const flags[] = {
        "DUCKED", "JUMP_HELD", "ON_GROUND",
        "TIME_WATERJUMP", "TIME_LAND", "TIME_TELEPORT",
        "NO_PREDICTION", "TELEPORT_BIT"
    };
    int x = CHAR_WIDTH;
    int y = ( scr_hudHeight - 2 * CHAR_HEIGHT ) / 2;
    unsigned i, j;

    i = cl.frame.ps.pmove.pm_type;
    if( i > PM_FREEZE ) {
        i = PM_FREEZE;
    }
    R_DrawString( x, y, 0, MAX_STRING_CHARS, types[i], scr_font );
    y += CHAR_HEIGHT;

    j = cl.frame.ps.pmove.pm_flags;
    for( i = 0; i < 8; i++ ) {
        if( j & ( 1 << i ) ) {
            x = R_DrawString( x, y, 0, MAX_STRING_CHARS, flags[i], scr_font );
            x += CHAR_WIDTH;
        }
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

        R_SetClipRect( DRAW_CLIP_MASK, &rc );
    }

	R_SetColor( DRAW_COLOR_CLEAR, NULL );

    if( crosshair->integer ) {
    	draw_crosshair();
    }

	Cvar_ClampValue( scr_alpha, 0, 1 );
	R_SetColor( DRAW_COLOR_ALPHA, ( byte * )&scr_alpha->value );

    if( scr_draw2d->integer > 1 ) {
    	SCR_ExecuteLayoutString( cl.configstrings[CS_STATUSBAR] );
    }

	if( ( cl.frame.ps.stats[STAT_LAYOUTS] & 1 ) ||
        ( cls.demo.playback && Key_IsDown( K_F1 ) ) )
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
	        R_DrawFill( x, y, LAG_WIDTH, LAG_HEIGHT, 4 );
        }
        SCR_LagDraw( x, y );
    }

	// draw phone jack
	if( cls.netchan && cls.netchan->outgoing_sequence - cls.netchan->incoming_acknowledged >= CMD_BACKUP ) {
		if( ( cls.realtime >> 8 ) & 3 ) {
			R_DrawStretchPic( x, y, LAG_WIDTH, LAG_HEIGHT, scr_net );
		}
	}

    draw_objects();

	R_SetColor( DRAW_COLOR_CLEAR, NULL );

    if( scr_showturtle->integer && cl.frameflags ) {
        draw_turtle();
    }

    if( scr_showstats->integer ) {
        SCR_DrawStats();
    }
    if( scr_showpmove->integer ) {
        SCR_DrawPmove();
    }

    SCR_DrawPause();

    if( scr_glconfig.renderer == GL_RENDERER_SOFTWARE ) {
        R_SetClipRect( DRAW_CLIP_DISABLED, NULL );
    }
}

static const cmdreg_t scr_drawcmds[] = {
#if USE_CHATHUD
    { "clearchat", SCR_ClearChatHUD_f },
#endif
    { "draw", SCR_Draw_f, SCR_Draw_c },
    { "undraw", SCR_UnDraw_f, SCR_UnDraw_c },
    { NULL }
};

/*
================
SCR_InitDraw
================
*/
void SCR_InitDraw( void ) {
	scr_draw2d = Cvar_Get( "scr_draw2d", "2", 0 );
	scr_showturtle = Cvar_Get( "scr_showturtle", "1", 0 );
	scr_showfollowing = Cvar_Get( "scr_showfollowing", "1", 0 );
	scr_lag_x = Cvar_Get( "scr_lag_x", "-1", 0 );
	scr_lag_y = Cvar_Get( "scr_lag_y", "-1", 0 );
    scr_lag_draw = Cvar_Get( "scr_lag_draw", "0", 0 );
	scr_alpha = Cvar_Get( "scr_alpha", "1", 0 );
	scr_showstats = Cvar_Get( "scr_showstats", "0", 0 );
	scr_showpmove = Cvar_Get( "scr_showpmove", "0", 0 );

    Cmd_Register( scr_drawcmds );
}

void SCR_ShutdownDraw( void ) {
    Cmd_Deregister( scr_drawcmds );
}


