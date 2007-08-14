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
#include <setjmp.h>
#include "q_lex.h"

//
// cl_draw.c - draw all 2D elements during gameplay
//

cvar_t      *scr_draw2d;
cvar_t      *scr_showturtle;
cvar_t		*scr_showfollowing;
cvar_t		*scr_lag_placement;
cvar_t		*scr_lag_type;
cvar_t		*scr_lag_background;
cvar_t		*scr_crosshair_names;
cvar_t		*scr_alpha;

#define SCR_DrawString( x, y, flags, string ) \
	UIS_DrawStringEx( x, y, flags, MAX_STRING_CHARS, string, scr_font )

// ============================================================================

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

#define LAG_SAMPLES 1024
#define LAG_MASK	( LAG_SAMPLES - 1 )

#define LAG_HISTORY			16
#define LAG_HISTORY_MASK	( LAG_HISTORY - 1 )

#define SUPRESSED_PACKET_MASK	0x4000
#define DROPPED_PACKET_MASK		0x8000

#define LAG_BASE    0xD5
#define LAG_WARN    0xDC
#define LAG_CRIT    0xF2

typedef struct lagometer_s {
	uint16 ping[LAG_SAMPLES];
	uint16 inSize[LAG_SAMPLES];
	uint8 delta[LAG_SAMPLES];
	int inTime[LAG_HISTORY];
	int inPacketNum;

	uint16 outSize[LAG_SAMPLES];
	int outTime[LAG_HISTORY];
	int outPacketNum;
} lagometer_t;

static lagometer_t		lag;
static vrect_t          lag_rc;
static int              lag_max;

/*
==============
SCR_ClearLagometer
==============
*/
void SCR_ClearLagometer( void ) {
	memset( &lag, 0, sizeof( lag ) );
}

/*
==============
SCR_AddLagometerPacketInfo
==============
*/
void SCR_AddLagometerPacketInfo( void ) {
	uint16 ping;
	int i, j;

	i = cls.netchan->incoming_acknowledged & CMD_MASK;
	ping = ( cls.realtime - cl.history[i].realtime ) & 0x3FFF;

	if( cl.frameflags & FF_SURPRESSED ) {
		ping |= SUPRESSED_PACKET_MASK;
	}
	if( cls.netchan->dropped ) {
		ping |= DROPPED_PACKET_MASK;
	}
	
	i = lag.inPacketNum & LAG_MASK;
	j = lag.inPacketNum & LAG_HISTORY_MASK;
	lag.inTime[j] = cls.realtime;
	lag.ping[i] = ping;
	lag.inSize[i] = msg_read.cursize;
	lag.delta[i] = cl.frame.delta > 0 ? ( cl.frame.number - cl.frame.delta ) : 0;

	lag.inPacketNum++;

}

/*
==============
SCR_AddLagometerOutPacketInfo
==============
*/
void SCR_AddLagometerOutPacketInfo( int size ) {
	int i, j;

	i = lag.outPacketNum & LAG_MASK;
	j = lag.outPacketNum & LAG_HISTORY_MASK;
	lag.outTime[j] = cls.realtime;
	lag.outSize[i] = size;

	lag.outPacketNum++;
}

static void SCR_DrawPingGraph( void ) {
	int i, j, v;
	int color;

	for( i = 0; i < lag_rc.width; i++ ) {
		j = lag.inPacketNum - i - 1;
		if( j < 0 ) {
			break;
		}

		v = lag.ping[j & LAG_MASK];

		color = LAG_BASE;
		if( v & SUPRESSED_PACKET_MASK ) {
			v &= ~SUPRESSED_PACKET_MASK;
			color = LAG_WARN;
		}
		if( v & DROPPED_PACKET_MASK ) {
			v &= ~DROPPED_PACKET_MASK;
			color = LAG_CRIT;
		}

		v = v * lag_rc.height / lag_max;
		if( v > lag_rc.height ) {
			v = lag_rc.height;
		}

		ref.DrawFill( lag_rc.x + lag_rc.width - i - 1, lag_rc.y + lag_rc.height - v, 1, v, color );
		
	}
}

static void SCR_DrawOutPacketGraph( void ) {
	int i, j, v;

	for( i = 0; i < lag_rc.width; i++ ) {
		j = lag.outPacketNum - i - 1;
		if( j < 0 ) {
			break;
		}

		v = lag.outSize[j & LAG_MASK];

		v = v * lag_rc.height / lag_max;
		if( v > lag_rc.height ) {
			v = lag_rc.height;
		}

		ref.DrawFill( lag_rc.x + lag_rc.width - i - 1, lag_rc.y + lag_rc.height - v, 1, v, LAG_BASE );
	}
}

static void SCR_DrawInPacketGraph( void ) {
	int i, j, v;

	for( i = 0; i < lag_rc.width; i++ ) {
		j = lag.inPacketNum - i - 1;
		if( j < 0 ) {
			break;
		}

		v = lag.inSize[j & LAG_MASK];

		v = v * lag_rc.height / lag_max;
		if( v > lag_rc.height ) {
			v = lag_rc.height;
		}

		ref.DrawFill( lag_rc.x + lag_rc.width - i - 1, lag_rc.y + lag_rc.height - v, 1, v, LAG_BASE );		
	}
}

static void SCR_DrawDeltaGraph( void ) {
	int i, j, v;
	int color;

	for( i = 0; i < lag_rc.width; i++ ) {
		j = lag.inPacketNum - i - 1;
		if( j < 0 ) {
			break;
		}

		v = lag.delta[j & LAG_MASK];

		color = LAG_BASE;
		if( !v ) {
			v = lag_max;
			color = LAG_WARN;
		}

		v = v * lag_rc.height / lag_max;
		if( v > lag_rc.height ) {
			v = lag_rc.height;
		}

		ref.DrawFill( lag_rc.x + lag_rc.width - i - 1, lag_rc.y + lag_rc.height - v, 1, v, color );
	}
}

static void SCR_DrawDisconnect( vrect_t *rc ) {
	if( !cls.netchan ) {
		return;
	}
	if( cls.netchan->outgoing_sequence - cls.netchan->incoming_acknowledged > CMD_BACKUP - 1 ) {
		if( ( cls.realtime >> 8 ) & 3 ) {
			ref.DrawStretchPic( rc->x, rc->y, rc->width, rc->height, scr_net );
		}
	}
}

static void CL_Ping_m( char *buffer, int bufferSize ) {
	int i, j;
	int start;
	int ping;

	start = lag.inPacketNum - LAG_HISTORY + 1;
	if( start < 0 ) {
		start = 0;
	}

	ping = 0;
	if( lag.inPacketNum > 1 ) {
		for( i = start; i < lag.inPacketNum; i++ ) {
			j = lag.ping[i & LAG_MASK];
			ping += j & ~( SUPRESSED_PACKET_MASK | DROPPED_PACKET_MASK );
		}

		ping /= i - start;
	}
	Com_sprintf( buffer, bufferSize, "%i", ping );
}

static void CL_UpRate_m( char *buffer, int bufferSize ) {
	int i;
	float size;
	int startTime, endTime;

	i = lag.outPacketNum - LAG_HISTORY + 1;
	if( i < 0 ) {
		i = 0;
	}

	startTime = lag.outTime[i & LAG_HISTORY_MASK];
	endTime = lag.outTime[( lag.outPacketNum - 1 ) &
        LAG_HISTORY_MASK];

	size = 0;
	if( startTime != endTime ) {
		for( ; i < lag.outPacketNum; i++ ) {
			size += lag.outSize[i & LAG_MASK];
		}

		size /= endTime - startTime;
	}
	Com_sprintf( buffer, bufferSize, "%1.2f", size );
}

static void CL_DnRate_m( char *buffer, int bufferSize ) {
	int i;
	float size;
	int startTime, endTime;

	i = lag.inPacketNum - LAG_HISTORY + 1;
	if( i < 0 ) {
		i = 0;
	}

	startTime = lag.inTime[i & LAG_HISTORY_MASK];
	endTime = lag.inTime[( lag.inPacketNum - 1 ) &
        LAG_HISTORY_MASK];

	size = 0;
	if( startTime != endTime ) {
		for( ; i < lag.inPacketNum; i++ ) {
			size += lag.inSize[i & LAG_MASK];
		}

		size /= endTime - startTime;
	}
	Com_sprintf( buffer, bufferSize, "%1.2f", size );
}

typedef struct {
    list_t  entry;
    int     x, y;
    cvar_t *cvar;
    xmacro_t macro;
} drawstring_t;

static LIST_DECL( scr_drawstrings );

// draw cl_fps -1 80
static void SCR_Draw_f( void ) {
    int x, y;
    char *s;
    drawstring_t *ds;
    xmacro_t macro;

    if( Cmd_Argc() != 4 ) {
        Com_Printf( "Usage: %s <name> <x> <y>\n", Cmd_Argv( 0 ) );
        return;
    }

    s = Cmd_Argv( 1 );
    x = atoi( Cmd_Argv( 2 ) );
    y = atoi( Cmd_Argv( 3 ) );

    ds = Z_Malloc( sizeof( *ds ) );
    ds->x = x;
    ds->y = y;

	macro = Cmd_FindMacroFunction( s );
    if( macro ) {
        ds->cvar = NULL;
        ds->macro = macro;
    } else {
        ds->cvar = Cvar_Get( s, "", CVAR_USER_CREATED );
        ds->macro = NULL;
    }

    List_Append( &scr_drawstrings, &ds->entry );
}

static void SCR_UnDraw_f( void ) {
    char *s;
    drawstring_t *ds, *next;
    xmacro_t macro;
    cvar_t *cvar;

    if( Cmd_Argc() != 2 ) {
        Com_Printf( "Usage: %s <name>\n", Cmd_Argv( 0 ) );
        return;
    }

    s = Cmd_Argv( 1 );
    if( !strcmp( s, "all" ) ) {
        LIST_FOR_EACH_SAFE( drawstring_t, ds, next, &scr_drawstrings, entry ) {
            Z_Free( ds );
        }
        List_Init( &scr_drawstrings );
        Com_Printf( "Deleted all drawstrings.\n" );
        return;
    }

    cvar = NULL;
	macro = Cmd_FindMacroFunction( s );
    if( !macro ) {
        cvar = Cvar_Get( s, "", CVAR_USER_CREATED );
    }

    LIST_FOR_EACH_SAFE( drawstring_t, ds, next, &scr_drawstrings, entry ) {
        if( ds->macro == macro && ds->cvar == cvar ) {
            List_Remove( &ds->entry );
            Z_Free( ds );
        }
    }
}

/*
===============================================================================

CHAT HUD

===============================================================================
*/

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

/*
===============================================================================

CROSSHAIR

===============================================================================
*/

/*
=================
SCR_ScanForCrosshairEntity
=================
*/
static void SCR_ScanForCrosshairEntity( void ) {
	trace_t		trace, worldtrace;
	vec3_t		end;
	entity_state_t	*ent;
	int			i, x, zd, zu;
	cnode_t		*headnode;
	vec3_t		bmins, bmaxs;
	vec3_t forward, light;

	AngleVectors( cl.refdef.viewangles, forward, NULL, NULL );
	VectorMA( cl.refdef.vieworg, 8192, forward, end );

	CM_BoxTrace( &worldtrace, cl.refdef.vieworg, end,
		vec3_origin, vec3_origin, cl.cm.cache->nodes, MASK_SOLID );

	for( i = 0; i < cl.numSolidEntities; i++ ) {
		ent = cl.solidEntities[i];

		if( ent->solid == 31 ) {
			continue;
		}

		if( ent->modelindex != 255 ) {
			continue; // not a player
		}

		if( ent->frame >= 173 || ent->frame <= 0 ) {
			continue; // player is dead
		}

		x = 8*(ent->solid & 31);
		zd = 8*((ent->solid>>5) & 31);
		zu = 8*((ent->solid>>10) & 63) - 32;

		bmins[0] = bmins[1] = -x;
		bmaxs[0] = bmaxs[1] = x;
		bmins[2] = -zd;
		bmaxs[2] = zu;

		headnode = CM_HeadnodeForBox( bmins, bmaxs );

		CM_TransformedBoxTrace( &trace, cl.refdef.vieworg, end,
			vec3_origin, vec3_origin, headnode, MASK_PLAYERSOLID,
				ent->origin, vec3_origin );

		if( trace.allsolid || trace.startsolid ||
                trace.fraction < worldtrace.fraction )
        {
            /* TODO: find nearest entity */
			ref.LightPoint( ent->origin, light );
			if( VectorLengthSquared( light ) > 0.02f ) {
				cl.crosshairPlayer = &cl.clientinfo[ent->skinnum & 0xFF];
				cl.crosshairTime = cls.realtime;
			}
			break;
		}
	}
}

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

/*
=================
SCR_DrawCrosshair
=================
*/
static void SCR_DrawCrosshair( void ) {
	int x, y, w, h;
	float alpha;
	qhandle_t handle;

	w = h = 0;
	if( crosshair->integer ) {
		if( crosshair->modified ) {
			crosshair->modified = qfalse;
			SCR_TouchPics();
		}

        handle = crosshair_pic;
        w = crosshair_width;
        h = crosshair_height;

		x = ( scr_hudWidth - w ) / 2;
		y = ( scr_hudHeight - h ) / 2;
		ref.DrawPic( x, y, handle );
	}

	Cvar_ClampValue( scr_crosshair_names, 0, 5 );
	if( scr_crosshair_names->value ) {
		if( cl.numSolidEntities ) {
			SCR_ScanForCrosshairEntity();
		}
		if( cl.crosshairPlayer ) {
			alpha = SCR_FadeAlpha( cl.crosshairTime,
				scr_crosshair_names->value * 1000, 300 );
			if( alpha ) {
				x = ( scr_hudWidth - strlen( cl.crosshairPlayer->name ) *
					TINYCHAR_WIDTH ) / 2;
				y = ( scr_hudHeight + h ) / 2;

				ref.SetColor( DRAW_COLOR_ALPHA, ( byte * )&alpha );
				ref.DrawString( x, y, 0, MAX_STRING_CHARS,
					cl.crosshairPlayer->name, scr_font );
			}
		}
	}

    ref.SetColor( DRAW_COLOR_CLEAR, NULL );
}

/*
===============================================================================

FOLLOWING

===============================================================================
*/

/*
================
SCR_DrawFollowing
================
*/
static void SCR_DrawFollowing( void ) {
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

	x = ( scr_hudWidth - strlen( string ) * TINYCHAR_WIDTH ) / 2;

	ref.DrawString( x, 48, 0, MAX_STRING_CHARS, string, scr_font );
}

/*
=================
SCR_DrawDefaultHud
=================
*/
static void SCR_DrawDefaultHud( void ) {
	//vrect_t rc;
    char buffer[MAX_QPATH];
    int x, y, flags;
    char *s;
    drawstring_t *ds;

//	SCR_DrawCrosshair();

	Cvar_ClampValue( scr_alpha, 0, 1 );
	ref.SetColor( DRAW_COLOR_ALPHA, ( byte * )&scr_alpha->value );

	SCR_ExecuteLayoutString( cl.configstrings[CS_STATUSBAR] );

	if( ( cl.frame.ps.stats[STAT_LAYOUTS] & 1 ) ||
        ( cls.demoplayback && Key_IsDown( K_F1 ) ) )
    {
		SCR_ExecuteLayoutString( cl.layout );
	}

	if( cl.frame.ps.stats[STAT_LAYOUTS] & 2 ) {
		SCR_DrawInventory();
	}

	if( cl.frame.clientNum != CLIENTNUM_NONE ) {
		SCR_DrawFollowing();
	}

	SCR_DrawCenterString();

	lag_rc.x = scr_hudWidth - 48;
	lag_rc.y = scr_hudHeight - 48;
	lag_rc.width = 48;
	lag_rc.height = 48;

    lag_max = 60;

    switch( scr_lag_type->integer ) {
    case 1:
        SCR_DrawPingGraph();
        break;
    case 2:
        SCR_DrawOutPacketGraph();
        break;
    case 3:
        SCR_DrawInPacketGraph();
        break;
    case 4:
        SCR_DrawDeltaGraph();
        break;
    }

    LIST_FOR_EACH( drawstring_t, ds, &scr_drawstrings, entry ) {
        if( ds->macro ) {
            ds->macro( buffer, sizeof( buffer ) );
            s = buffer;
        } else {
            s = ds->cvar->string;
        }
        x = ds->x;
        y = ds->y;
        flags = 0;
        if( x < 0 ) {
            x += scr_hudWidth;
            flags = UI_RIGHT;
        }
        if( y < 0 ) {
            y += scr_hudHeight - 8;
        }
        SCR_DrawString( x, y, flags, s );
    }


	// draw phone jack
	SCR_DrawDisconnect( &lag_rc );

}

static void SCR_DrawTurtle( void ) {
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

// ============================================================================

/*
=================
SCR_Draw2D
=================
*/
void SCR_Draw2D( void ) {
    clipRect_t rc;

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

	SCR_DrawCrosshair();

	SCR_DrawDefaultHud();

	ref.SetColor( DRAW_COLOR_CLEAR, NULL );

    if( scr_showturtle->integer && cl.frameflags ) {
        SCR_DrawTurtle();
    }

    if( scr_glconfig.renderer == GL_RENDERER_SOFTWARE ) {
        ref.SetClipRect( DRAW_CLIP_DISABLED, NULL );
    }
}

cmdreg_t scr_drawcmds[] = {
    { "clearchat", SCR_ClearChatHUD_f },
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
	scr_draw2d = Cvar_Get( "scr_draw2d", "1", 0 );
	scr_showturtle = Cvar_Get( "scr_showturtle", "1", 0 );
	scr_showfollowing = Cvar_Get( "scr_showfollowing", "0", 0 );
	scr_lag_placement = Cvar_Get( "scr_lag_placement", "48x48-1-1", 0 );
    scr_lag_type = Cvar_Get( "scr_lag_type", "0", 0 );
    scr_lag_background = Cvar_Get( "scr_lag_background", "0", 0 );
	scr_crosshair_names = Cvar_Get( "scr_crosshair_names", "0", 0 );
	scr_alpha = Cvar_Get( "scr_alpha", "1", 0 );

	Cmd_AddMacro( "cl_ping", CL_Ping_m );
	Cmd_AddMacro( "cl_uprate", CL_UpRate_m );
	Cmd_AddMacro( "cl_dnrate", CL_DnRate_m );
	
    Cmd_Register( scr_drawcmds );
}

