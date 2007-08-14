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

//
// cl_ui.c
//

#include "cl_local.h"

uiAPI_t	ui;

static void *ui_library;

#ifdef UI_HARD_LINKED

void CL_ShutdownUI( void ) {
	if( !cls.ui_initialized ) {
		return;
	}

	UI_Shutdown();

	Z_LeakTest( TAG_UI );

	cls.ui_initialized = qfalse;
}

/*
===================
CL_InitUI
===================
*/
void CL_InitUI( void ) {
	if( UI_Init() ) {
		cls.ui_initialized = qtrue;
	}
}

#else

/*
==============
CL_UISetupCallback
==============
*/
static qboolean CL_UISetupCallback( api_type_t type, void *api ) {
	switch( type ) {
	case API_CMD:
		Cmd_FillAPI( ( cmdAPI_t * )api );
		break;
	case API_CVAR:
		Cvar_FillAPI( ( cvarAPI_t * )api );
		break;
	case API_FS:
		FS_FillAPI( ( fsAPI_t * )api );
		break;
	case API_COMMON:
		Com_FillAPI( ( commonAPI_t * )api );
		break;
	case API_SYSTEM:
		Sys_FillAPI( ( sysAPI_t * )api );
		break;
	case API_REFRESH:
		*( refAPI_t * )api = ref;
		break;
	case API_KEYS:
		Key_FillAPI( ( keyAPI_t * )api );
		break;
	case API_CLIENT:
		CL_FillAPI( ( clientAPI_t * )api );
		break;
	default:
		Com_Error( ERR_FATAL, "CL_UISetupCallback: bad api type" );
	}

	return qtrue;
}

static void CL_FreeUI( void ) {
	Sys_FreeLibrary( ui_library );
	ui_library = NULL;
	memset( &ui, 0, sizeof( ui ) );
}

/*
==============
CL_LoadUI
==============
*/
static qboolean CL_LoadUI( const char *name ) {
	moduleEntry_t entry;
	moduleInfo_t info;
	moduleCapability_t caps;
	APISetupCallback_t callback;
    char path[MAX_OSPATH];

	Com_Printf( "------- Loading %s -------\n", name );

    Com_sprintf( path, sizeof( path ), "%s" PATH_SEP_STRING "%s" LIBSUFFIX,
        sys_refdir->string, name );

	if( ( entry = Sys_LoadLibrary( path, "moduleEntry", &ui_library ) ) == NULL ) {
		Com_DPrintf( "Couldn't load %s\n", name );
		return qfalse;
	}

	entry( MQ_GETINFO, &info );
	if( info.api_version != MODULES_APIVERSION ) {
		Com_WPrintf( "%s has incompatible api_version: %i, should be %i\n",
			name, info.api_version, MODULES_APIVERSION );
		goto fail;
	}

	caps = ( moduleCapability_t )entry( MQ_GETCAPS, NULL );
	if( !( caps & MCP_UI ) ) {
		Com_WPrintf( "%s doesn't have UI capability\n", name );
		goto fail;
	}

	callback = ( APISetupCallback_t )entry( MQ_SETUPAPI, ( void * )CL_UISetupCallback );
	if( !callback ) {
		Com_WPrintf( "%s returned NULL callback\n", name );
		goto fail;
	}

	callback( API_UI, &ui );

	cls.ui_initialized = qtrue;
	if( !ui.Init() ) {
		goto fail;
	}

    ui.ModeChanged();

	Com_Printf( "------------------------------------\n" );
	
	return qtrue;

fail:
	CL_FreeUI();
	return qfalse;
}


/*
===================
CL_ShutdownUI
===================
*/
void CL_ShutdownUI( void ) {
	if( !cls.ui_initialized ) {
		return;
	}

	ui.Shutdown();
	CL_FreeUI();

	Z_LeakTest( TAG_UI );

	cls.ui_initialized = qfalse;
}

/*
===================
CL_InitUI
===================
*/
void CL_InitUI( void ) {
	CL_LoadUI( "mod_ui" );
}

/*
===================
UI_Keydown
===================
*/
void UI_Keydown( int key ) {
	if( cls.ui_initialized ) {
		ui.Keydown( key );
	}
}

/*
===================
UI_CharEvent
===================
*/
void UI_CharEvent( int key ) {
	if( cls.ui_initialized ) {
		ui.CharEvent( key );
	}
}

/*
===================
UI_Draw
===================
*/
void UI_Draw( int realtime ) {
	if( cls.ui_initialized ) {
		ui.Draw( realtime );
	}
}

/*
===================
UI_OpenMenu
===================
*/
void UI_OpenMenu( uiMenu_t menu ) {
	if( cls.ui_initialized ) {
		ui.OpenMenu( menu );
	} else {
		Key_SetDest( cls.key_dest & ~KEY_MENU );
	}
}

void UI_ErrorMenu( comErrorType_t type, const char *text ) {
	if( cls.ui_initialized ) {
		ui.ErrorMenu( type, text );
	}
}

/*
===================
UI_AddToServerList
===================
*/
void UI_AddToServerList( const serverStatus_t *status ) {
	if( cls.ui_initialized ) {
		ui.AddToServerList( status );
	}
}

/*
===================
UI_MouseMove
===================
*/
void UI_MouseMove( int dx, int dy ) {
	if( cls.ui_initialized ) {
		ui.MouseMove( dx, dy );
	}
}

/*
===================
UI_IsTransparent
===================
*/
qboolean UI_IsTransparent( void ) {
	if( cls.ui_initialized ) {
		return ui.IsTransparent();
	}
	return qtrue;
}

/*
===================
UI_DrawLoading
===================
*/
void UI_DrawLoading( int realtime ) {
	if( cls.ui_initialized ) {
		ui.DrawLoading( realtime );
	} else {
		/* make sure half-screen console is up */
		if( !( cls.key_dest & KEY_CONSOLE ) ) {
			Key_SetDest( cls.key_dest | KEY_CONSOLE );
			Con_RunConsole();
			Con_DrawConsole();
		}

		/* fill the rest of the screen with black */
		ref.DrawFill( 0, scr_glconfig.vidHeight / 2, scr_glconfig.vidWidth, scr_glconfig.vidHeight, 0 );
	}
}

void UI_ModeChanged( void ) {
    if( cls.ui_initialized ) {
        ui.ModeChanged();
    }
}

#endif /* !UI_HARD_LINKED */
