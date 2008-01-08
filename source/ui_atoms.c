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

#include "ui_local.h"

#ifndef UI_HARD_LINKED
/* declare imports for this module */
cmdAPI_t	cmd;
cvarAPI_t	cvar;
fsAPI_t		fs;
commonAPI_t	com;
sysAPI_t	sys;
refAPI_t	ref;
keyAPI_t	keys;
clientAPI_t client;
#endif

uiStatic_t	uis;

cvar_t	*ui_debug;
static cvar_t	*ui_open;
static cvar_t	*ui_background;
static cvar_t	*ui_scale;

// ===========================================================================

static int EmptyCallback( int id, int msg, int param ) {
	return QMS_NOTHANDLED;
}

/*
=================
UI_PushMenu
=================
*/
void UI_PushMenu( menuFrameWork_t *menu ) {
	int		i, keydest;

	// if this menu is already present, drop back to that level
	// to avoid stacking menus by hotkeys
	for( i = 0; i < uis.menuDepth; i++ ) {
		if( uis.layers[i] == menu ) {
			break;
		}
	}

	if( i == uis.menuDepth ) {
		if( uis.menuDepth >= MAX_MENU_DEPTH )
			Com_Error( ERR_FATAL, "UI_PushMenu: MAX_MENU_DEPTH" );
		uis.layers[uis.menuDepth++] = menu;
	} else {
		uis.menuDepth = i;
	}

	uis.transparent = qfalse;
	for( i = uis.menuDepth - 1; i >= 0; i-- ) {
		if( uis.layers[i]->transparent ) {
	        uis.transparent = qtrue;
			break;
		}
	}

	if( !menu->callback ) {
		menu->callback = EmptyCallback;
	}

    Menu_Init( menu );

	if( !uis.activeMenu ) {
		uis.entersound = qtrue;
	}

	uis.activeMenu = menu;

	keydest = keys.GetDest();
	if( keydest & KEY_CONSOLE ) {
		keydest &= ~KEY_CONSOLE;
	}
	keys.SetDest( keydest | KEY_MENU );

	UI_DoHitTest();
}

void UI_Resize( void ) {
	int i;

    if( uis.glconfig.renderer == GL_RENDERER_SOFTWARE ) {
        uis.clipRect.left = 0;
        uis.clipRect.top = 0;
        uis.clipRect.right = uis.glconfig.vidWidth;
        uis.clipRect.bottom = uis.glconfig.vidHeight;
        uis.scale = 1;
        uis.width = uis.glconfig.vidWidth;
        uis.height = uis.glconfig.vidHeight;
    } else {
        if( ui_scale->value < 1 ) {
            cvar.Set( "ui_scale", "1" );
        } else if( ui_scale->value > 9 ) {
            cvar.Set( "ui_scale", "9" );
        }
        uis.scale = 1 / ui_scale->value;
        uis.width = uis.glconfig.vidWidth * uis.scale;
        uis.height = uis.glconfig.vidHeight * uis.scale;
    }

	for( i = 0; i < uis.menuDepth; i++ ) {
		if( uis.layers[i] ) {
            Menu_Init( uis.layers[i] );
		}
	}
}


/*
=================
UI_ForceMenuOff
=================
*/
void UI_ForceMenuOff( void ) {
	int i;

	for( i = 0; i < uis.menuDepth; i++ ) {
		if( uis.layers[i] ) {
			uis.layers[i]->callback( ID_MENU, QM_DESTROY, qtrue );
		}
	}

	keys.SetDest( keys.GetDest() & ~KEY_MENU );
	uis.menuDepth = 0;
	uis.activeMenu = NULL;
	uis.transparent = qfalse;
	//keys.ClearStates();
}

/*
=================
UI_PopMenu
=================
*/
void UI_PopMenu( void ) {
	int i;

	if( uis.menuDepth < 1 )
		Com_Error( ERR_FATAL, "UI_PopMenu: depth < 1" );

	if( --uis.menuDepth == 0 ) {
		UI_ForceMenuOff();
		return;
	}

	uis.layers[uis.menuDepth]->callback( ID_MENU, QM_DESTROY, qfalse );
	uis.layers[uis.menuDepth - 1]->callback( ID_MENU, QM_DESTROY_CHILD, qfalse );

	uis.activeMenu = uis.layers[uis.menuDepth - 1];

	uis.transparent = qfalse;
	for( i = uis.menuDepth - 1; i >= 0; i-- ) {
		if( uis.layers[i]->transparent ) {
	        uis.transparent = qtrue;
			break;
		}
	}

	UI_DoHitTest();
}

/*
=================
UI_IsTransparent
=================
*/
qboolean UI_IsTransparent( void ) {
	if( !( keys.GetDest() & KEY_MENU ) ) {
		return qtrue;
	}

	if( !uis.activeMenu ) {
		return qtrue;
	}

	return uis.transparent;
}

/*
=================
UI_OpenMenu
=================
*/
void UI_OpenMenu( uiMenu_t menu ) {
	// close any existing menus
	UI_ForceMenuOff();

	switch( menu ) {
	case UIMENU_MAIN:
		if( ui_open->integer ) {
			M_Menu_Main_f();
		}
		break;
	case UIMENU_MAIN_FORCE:
		M_Menu_Main_f();
		break;
	case UIMENU_INGAME:
		M_Menu_Ingame_f();
		break;
	case UIMENU_NONE:
		break;
	default:
		Com_Error( ERR_FATAL, "UI_OpenMenu: bad menu" );
		break;
	}
}

void UI_ErrorMenu( comErrorType_t type, const char *text ) {
	// close any existing menus
	UI_ForceMenuOff();

	if( !ui_open->integer ) {
		return;
	}

	M_Menu_Main_f();
	M_Menu_Error_f( type, text );
}


//=============================================================================

/*
=================
UI_FormatColumns
=================
*/
void *UI_FormatColumns( int extrasize, ... ) {
	va_list argptr;
	char *buffer, *p;
	int i, j, total = 0;
	char *strings[MAX_COLUMNS];
	int lengths[MAX_COLUMNS];

	va_start( argptr, extrasize );
    for( i = 0; i < MAX_COLUMNS; i++ ) {
	    if( ( p = va_arg( argptr, char * ) ) == NULL ) {
            break;
        }
		strings[i] = p;
		total += lengths[i] = strlen( p ) + 1;
	}
	va_end( argptr );

	buffer = UI_Malloc( extrasize + total + 1 );
    p = buffer + extrasize;
	for( j = 0; j < i; j++ ) {
		memcpy( p, strings[j], lengths[j] );
		p += lengths[j];
	}
	*p = 0;

	return buffer;
}

char *UI_GetColumn( char *s, int n ) {
    int i;

    for( i = 0; i < n && *s; i++ ) {
        s += strlen( s ) + 1;
    }

    return s;
}

/*
=================
UI_CopyString
=================
*/
char *UI_CopyString( const char *in ) {
	char	*out;

	if( !in ) {
		return NULL;
	}
	
	out = UI_Malloc( strlen( in ) + 1 );
	strcpy( out, in );

	return out;
}

/*
=================
UI_CursorInRect
=================
*/
qboolean UI_CursorInRect( vrect_t *rect ) {
	if( uis.mouseCoords[0] < rect->x ) {
        return qfalse;
    }
	if( uis.mouseCoords[0] >= rect->x + rect->width ) {
        return qfalse;
    }
	if( uis.mouseCoords[1] < rect->y ) {
        return qfalse;
    }
    if( uis.mouseCoords[1] >= rect->y + rect->height ) {
		return qfalse;
	}
	return qtrue;
}

void UI_DrawString( int x, int y, const color_t color, uint32 flags, const char *string ) {
	if( color ) {
		ref.SetColor( DRAW_COLOR_RGBA, color );
	}
	
	if( ( flags & UI_CENTER ) == UI_CENTER ) {
		x -= Q_DrawStrlen( string ) * 8 / 2;
	} else if( flags & UI_RIGHT ) {
		x -= Q_DrawStrlen( string ) * 8;
	}
	
	ref.DrawString( x, y, flags, MAX_STRING_CHARS, string, uis.fontHandle );
	if( color ) {
		ref.SetColor( DRAW_COLOR_CLEAR, NULL );
	}
}

void UI_DrawChar( int x, int y, uint32 flags, int ch ) {
	ref.DrawChar( x, y, flags, ch, uis.fontHandle );
}

void UI_StringDimensions( vrect_t *rc, uint32 flags, const char *string ) {
	rc->height = 8;
	rc->width = 8 * Q_DrawStrlen( string );
	
	if( ( flags & UI_CENTER ) == UI_CENTER ) {
		rc->x -= rc->width / 2;
	} else if( flags & UI_RIGHT ) {
		rc->x -= rc->width;
	}	
}


//=============================================================================
/* Menu Subsystem */

void M_Menu_Network_f( void );

/*
=================
UI_DoHitTest
=================
*/
qboolean UI_DoHitTest( void ) {
	menuCommon_t *item;

	if( !uis.activeMenu ) {
		return qfalse;
	}

	if( !( item = Menu_HitTest( uis.activeMenu ) ) ) {
		return qfalse;
	}
	if( !UI_IsItemSelectable( item ) ) {
		return qfalse;
	}

	Menu_MouseMove( item );

	if( item->flags & QMF_HASFOCUS ) {
		return qfalse;
	}

	Menu_SetFocus( item );
	
	return qtrue;
}

/*
=================
UI_MouseMove
=================
*/
void UI_MouseMove( int mx, int my ) {
	if( !uis.activeMenu ) {
		return;
	}

	if( !mx && !my ) {
		return;
	}

	uis.mouseCoords[0] += mx;
	uis.mouseCoords[1] += my;

	clamp( uis.mouseCoords[0], 0, uis.width );
	clamp( uis.mouseCoords[1], 0, uis.height );

	if( UI_DoHitTest() ) {
		// TODO: add new mousemove sound
		// cl.StartLocalSound( "misc/menu2.wav" );
	}
}


/*
=================
UI_Draw
=================
*/
void UI_Draw( int realtime ) {
	int i;

	uis.realtime = realtime;

	if( !( keys.GetDest() & KEY_MENU ) ) {
		return;
	}

	if( !uis.activeMenu ) {
		return;
	}

	ref.SetColor( DRAW_COLOR_CLEAR, NULL );
    if( uis.glconfig.renderer == GL_RENDERER_SOFTWARE ) {
        ref.SetClipRect( DRAW_CLIP_MASK, &uis.clipRect );
    } else {
    	ref.SetScale( &uis.scale );
    }

	if( !uis.transparent ) {
		// no transparent menus
		if( uis.backgroundHandle ) {
			ref.DrawStretchPic( 0, 0, uis.width, uis.height,
				uis.backgroundHandle );
		} else {
			ref.DrawFill( 0, 0, uis.width, uis.height, 0 );
		}

		if( uis.activeMenu->draw ) {
			uis.activeMenu->draw( uis.activeMenu );
		} else {
			Menu_Draw( uis.activeMenu );
		}
	} else {
		// draw all layers
		for( i = 0; i < uis.menuDepth; i++ ) {
			if( !uis.layers[i]->transparent ) {
				if( uis.backgroundHandle ) {
					ref.DrawStretchPic( 0, 0, uis.width, uis.height,
						uis.backgroundHandle );
				} else {
					ref.DrawFill( 0, 0, uis.width, uis.height, 0 );
				}
			}

			if( uis.layers[i]->draw ) {
				uis.layers[i]->draw( uis.layers[i] );
			} else {
				Menu_Draw( uis.layers[i] );
			}
		}
	}

	ref.DrawPic( uis.mouseCoords[0] - uis.cursorWidth / 2,
		uis.mouseCoords[1] - uis.cursorHeight / 2, uis.cursorHandle );

	if( ui_debug->integer ) {
		Menu_HitTest( uis.activeMenu );
		UI_DrawString( uis.width - 4, 4, NULL, UI_RIGHT,
			va( "%3i %3i", uis.mouseCoords[0], uis.mouseCoords[1] ) );
	}

	// delay playing the enter sound until after the
	// menu has been drawn, to avoid delay while
	// caching images
	if( uis.entersound ) {
		uis.entersound = qfalse;
		client.StartLocalSound( "misc/menu1.wav" );
	}

    if( uis.glconfig.renderer == GL_RENDERER_SOFTWARE ) {
        ref.SetClipRect( DRAW_CLIP_DISABLED, NULL );
    } else {
	    ref.SetScale( NULL );
    }
	ref.SetColor( DRAW_COLOR_CLEAR, NULL );
}

/*
=================
Default_MenuKey
=================
*/
int Default_MenuKey( menuFrameWork_t *m, int key ) {
	menuCommon_t *item;
	
	switch( key ) {
	case K_ESCAPE:
		UI_PopMenu();
		return QMS_OUT;

	case K_KP_UPARROW:
	case K_UPARROW:
    case 'k':
		return Menu_AdjustCursor( m, -1 );
		
	case K_KP_DOWNARROW:
	case K_DOWNARROW:
	case K_TAB:
    case 'j':
		return Menu_AdjustCursor( m, 1 );

	case K_KP_LEFTARROW:
	case K_LEFTARROW:
	case K_MWHEELDOWN:
    case 'h':
		return Menu_SlideItem( m, -1 );

	case K_KP_RIGHTARROW:
	case K_RIGHTARROW:
	case K_MWHEELUP:
    case 'l':
		return Menu_SlideItem( m, 1 );

	case K_MOUSE1:
	case K_MOUSE2:
	case K_MOUSE3:
		item = Menu_HitTest( m );
		if( !item ) {
			return QMS_NOTHANDLED;
		}
			
		if( !( item->flags & QMF_HASFOCUS ) ) {
			return QMS_NOTHANDLED;
		}

		// fall through

	case K_JOY1:
	case K_JOY2:
	case K_JOY3:
	case K_JOY4:
	case K_AUX1:
	case K_AUX2:
	case K_AUX3:
	case K_AUX4:
	case K_AUX5:
	case K_AUX6:
	case K_AUX7:
	case K_AUX8:
	case K_AUX9:
	case K_AUX10:
	case K_AUX11:
	case K_AUX12:
	case K_AUX13:
	case K_AUX14:
	case K_AUX15:
	case K_AUX16:
	case K_AUX17:
	case K_AUX18:
	case K_AUX19:
	case K_AUX20:
	case K_AUX21:
	case K_AUX22:
	case K_AUX23:
	case K_AUX24:
	case K_AUX25:
	case K_AUX26:
	case K_AUX27:
	case K_AUX28:
	case K_AUX29:
	case K_AUX30:
	case K_AUX31:
	case K_AUX32:
	case K_KP_ENTER:
	case K_ENTER:
		return Menu_SelectItem( m );
	}

	return QMS_NOTHANDLED;
}

/*
=================
UI_Keydown
=================
*/
void UI_Keydown( int key ) {
	menuCommon_t *item;
	int sound;

	if( !uis.activeMenu ) {
		return;
	}

	if( ( sound = uis.activeMenu->callback( ID_MENU, QM_KEY, key ) ) == QMS_NOTHANDLED ) {
		if( ( item = Menu_ItemAtCursor( uis.activeMenu ) ) == NULL ||
			( sound = Menu_KeyEvent( item, key ) ) == QMS_NOTHANDLED )
		{
			sound = Default_MenuKey( uis.activeMenu, key );
		}
	}

	switch( sound ) {
	case QMS_IN:
		client.StartLocalSound( "misc/menu1.wav" );
		break;
	case QMS_MOVE:
		client.StartLocalSound( "misc/menu2.wav" );
		break;
	case QMS_OUT:
		client.StartLocalSound( "misc/menu3.wav" );
		break;
	case QMS_BEEP:
		client.StartLocalSound( "misc/talk1.wav" );
		break;
	case QMS_NOTHANDLED:
	default:
		break;
	}

}

/*
=================
UI_CharEvent
=================
*/
void UI_CharEvent( int key ) {
	menuCommon_t *item;
	int sound;

	if( !uis.activeMenu ) {
		return;
	}

	if( ( sound = uis.activeMenu->callback( ID_MENU, QM_CHAR, key ) ) == QMS_NOTHANDLED ) {
		if( ( item = Menu_ItemAtCursor( uis.activeMenu ) ) == NULL ||
			( sound = Menu_CharEvent( item, key ) ) == QMS_NOTHANDLED )
		{
			return;
		}
	}

	switch( sound ) {
	case QMS_IN:
		client.StartLocalSound( "misc/menu1.wav" );
		break;
	case QMS_MOVE:
		client.StartLocalSound( "misc/menu2.wav" );
		break;
	case QMS_OUT:
		client.StartLocalSound( "misc/menu3.wav" );
		break;
	case QMS_BEEP:
		client.StartLocalSound( "misc/talk1.wav" );
		break;
	case QMS_NOTHANDLED:
	default:
		break;
	}

}

typedef struct uicmd_s {
	const char *name;
	void (*func)( void );
} uicmd_t;

static const uicmd_t uicmds[] = {
	{ "menu_main", M_Menu_Main_f },
	{ "menu_game", M_Menu_Game_f },
	{ "menu_loadgame", M_Menu_LoadGame_f },
	{ "menu_savegame", M_Menu_SaveGame_f },
	{ "menu_addressbook", M_Menu_AddressBook_f },
	{ "menu_startserver", M_Menu_StartServer_f },
	{ "menu_dmoptions", M_Menu_DMOptions_f },
	{ "menu_playerconfig", M_Menu_PlayerConfig_f },
	{ "menu_downloadoptions", M_Menu_DownloadOptions_f },
	{ "menu_multiplayer", M_Menu_Multiplayer_f },
	{ "menu_network", M_Menu_Network_f },
	{ "menu_video", M_Menu_Video_f },
	{ "menu_options", M_Menu_Options_f },
	{ "menu_keys", M_Menu_Keys_f },
	{ "menu_quit", M_Menu_Credits_f },
	{ "menu_close", UI_ForceMenuOff },

	{ NULL, NULL }
};

static void ui_background_changed( cvar_t *self ) {
	if( self->string[0] ) {
		uis.backgroundHandle = ref.RegisterPic( self->string );
	} else {
        uis.backgroundHandle = 0;
    }
}

static void ui_scale_changed( cvar_t *self ) {
    UI_Resize();
}

void UI_ModeChanged( void ) {
	ui_scale = cvar.Get( "ui_scale", "1", 0 );
    ui_scale->changed = ui_scale_changed;
	ref.GetConfig( &uis.glconfig );
    UI_Resize();
}

/*
=================
UI_Init
=================
*/
qboolean UI_Init( void ) {
	const uicmd_t *uicmd;

	memset( &uis, 0, sizeof( uis ) );

	for( uicmd = uicmds; uicmd->name; uicmd++ ) {
		cmd.AddCommand( uicmd->name, uicmd->func );
	}

	ui_debug = cvar.Get( "ui_debug", "0", 0 );
	ui_open = cvar.Get( "ui_open", "0", CVAR_ARCHIVE );
	ui_background = cvar.Get( "ui_background", "", 0 );

	uis.fontHandle = ref.RegisterFont( "conchars" );
	uis.cursorHandle = ref.RegisterPic( "ch1" );
	ref.DrawGetPicSize( &uis.cursorWidth, &uis.cursorHeight, uis.cursorHandle );

	if( uis.glconfig.renderer != GL_RENDERER_SOFTWARE ) {
		if( ui_background->string[0] ) {
			uis.backgroundHandle = ref.RegisterPic( ui_background->string );
		}
		ui_background->changed = ui_background_changed;
    }

	// Point to a nice location at startup
	strcpy( uis.m_demos_browse, "/demos" );

	return qtrue;
}

/*
=================
UI_Shutdown
=================
*/
void UI_Shutdown( void ) {
	const uicmd_t *uicmd;

    UI_ForceMenuOff();

	ui_background->changed = NULL;
    ui_scale->changed = NULL;

	PlayerModel_Free();

	for( uicmd = uicmds; uicmd->name; uicmd++ ) {
		cmd.RemoveCommand( uicmd->name );
	}
}

#ifndef UI_HARD_LINKED

// this is only here so the functions in q_shared.c can link

void Com_Printf( const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	com.Print( PRINT_ALL, text );
}

void Com_DPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	com.Print( PRINT_DEVELOPER, text );
}

void Com_WPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	com.Print( PRINT_WARNING, text );
}

void Com_EPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	com.Print( PRINT_ERROR, text );
}

void Com_Error( comErrorType_t type, const char *error, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	va_start( argptr, error );
	Q_vsnprintf( text, sizeof( text ), error, argptr );
	va_end( argptr );

	com.Error( type, text );
}

#endif

#ifndef UI_HARD_LINKED

/*
=================
UI_FillAPI
=================
*/
static void UI_FillAPI( uiAPI_t *api ) {
	api->Init = UI_Init;
	api->Shutdown = UI_Shutdown;
    api->ModeChanged = UI_ModeChanged;
	api->Draw = UI_Draw;
	api->DrawLoading = UI_DrawLoading;
	api->MouseMove = UI_MouseMove;
	api->Keydown = UI_Keydown;
	api->CharEvent = UI_CharEvent;
	api->OpenMenu = UI_OpenMenu;
	api->ErrorMenu = UI_ErrorMenu;
	api->AddToServerList = UI_AddToServerList;
	api->IsTransparent = UI_IsTransparent;
}

/*
=================
UI_APISetupCallback
=================
*/
static qboolean UI_APISetupCallback( api_type_t type, void *api ) {
	switch( type ) {
	case API_UI:
		UI_FillAPI( ( uiAPI_t * )api );
		break;
	default:
		return qfalse;
	}

	return qtrue;
}

/*
@@@@@@@@@@@@@@@@@@@@@
moduleEntry

@@@@@@@@@@@@@@@@@@@@@
*/
EXPORTED void *moduleEntry( int query, void *data ) {
	moduleInfo_t *info;
	moduleCapability_t caps;
	APISetupCallback_t callback;

	switch( query ) {
	case MQ_GETINFO:
		info = ( moduleInfo_t * )data;
		info->api_version = MODULES_APIVERSION;
		Q_strncpyz( info->fullname, "User interface library",
                sizeof( info->fullname ) );
		Q_strncpyz( info->author, "Andrey Nazarov", sizeof( info->author ) );
		return ( void * )qtrue;

	case MQ_GETCAPS:
		caps = MCP_UI;
		return ( void * )caps;

	case MQ_SETUPAPI:
		if( ( callback = ( APISetupCallback_t )data ) == NULL ) {
			return NULL;
		}
		callback( API_CMD, &cmd );
		callback( API_CVAR, &cvar );
		callback( API_FS, &fs );
		callback( API_COMMON, &com );
		callback( API_SYSTEM, &sys );
		callback( API_REFRESH, &ref );
		callback( API_KEYS, &keys );
		callback( API_CLIENT, &client );

		return ( void * )UI_APISetupCallback;

	}

	/* quiet compiler warning */
	return NULL;
}

#endif

