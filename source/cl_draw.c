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
        xmacro_t macro;
    //    int stat;
    //};
} drawobj_t;

static list_t scr_objects;

// draw cl_fps -1 80
static void SCR_Draw_f( void ) {
    int x, y;
    char *s;
    drawobj_t *obj;
    xmacro_t macro;
   // int stat;

    if( Cmd_Argc() != 4 ) {
        Com_Printf( "Usage: %s <name> <x> <y>\n", Cmd_Argv( 0 ) );
        return;
    }

    s = Cmd_Argv( 1 );
    x = atoi( Cmd_Argv( 2 ) );
    y = atoi( Cmd_Argv( 3 ) );

    obj = Z_Malloc( sizeof( *obj ) );
    obj->x = x;
    obj->y = y;

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
        macro = Cmd_FindMacroFunction( s );
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

static void SCR_UnDraw_f( void ) {
    char *s;
    drawobj_t *obj, *next;
    xmacro_t macro;
    cvar_t *cvar;

    if( Cmd_Argc() != 2 ) {
        Com_Printf( "Usage: %s <name>\n", Cmd_Argv( 0 ) );
        return;
    }

    s = Cmd_Argv( 1 );
    if( !strcmp( s, "all" ) ) {
        LIST_FOR_EACH_SAFE( drawobj_t, obj, next, &scr_objects, entry ) {
            Z_Free( obj );
        }
        List_Init( &scr_objects );
        Com_Printf( "Deleted all drawstrings.\n" );
        return;
    }

    cvar = NULL;
	macro = Cmd_FindMacroFunction( s );
    if( !macro ) {
        cvar = Cvar_Get( s, "", CVAR_USER_CREATED );
    }

    LIST_FOR_EACH_SAFE( drawobj_t, obj, next, &scr_objects, entry ) {
        if( obj->macro == macro && obj->cvar == cvar ) {
            List_Remove( &obj->entry );
            Z_Free( obj );
        }
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
            y += scr_hudHeight - 8 + 1;
        }
        if( obj->macro ) {
            obj->macro( buffer, sizeof( buffer ) );
            SCR_DrawString( x, y, flags, buffer );
        } else {
            SCR_DrawString( x, y, flags, obj->cvar->string );
        }
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
float SCR_FadeAlpha( int startTime, int visTime, int fadeTime ) {
	float alpha;
	int timeLeft;

	timeLeft = visTime - ( cls.realtime - startTime );
	if( timeLeft <= 0 ) {
		return 0;
	}

	if( fadeTime > visTime ) {
		fadeTime = visTime;
	}

	alpha = 1;
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

    SCR_DrawPause();

    if( scr_glconfig.renderer == GL_RENDERER_SOFTWARE ) {
        ref.SetClipRect( DRAW_CLIP_DISABLED, NULL );
    }
}

cmdreg_t scr_drawcmds[] = {
#if USE_CHATHUD
    { "clearchat", SCR_ClearChatHUD_f },
#endif
    { "draw", SCR_Draw_f, Cvar_Generator },
    { "undraw", SCR_UnDraw_f/*, SCR_DrawStringGenerator*/ },
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
	scr_showfollowing = Cvar_Get( "scr_showfollowing", "0", 0 );
	scr_lag_x = Cvar_Get( "scr_lag_x", "-1", 0 );
	scr_lag_y = Cvar_Get( "scr_lag_y", "-1", 0 );
    scr_lag_draw = Cvar_Get( "scr_lag_draw", "0", 0 );
	scr_alpha = Cvar_Get( "scr_alpha", "1", 0 );

    Cmd_Register( scr_drawcmds );
}

