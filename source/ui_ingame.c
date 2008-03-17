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


/*
=======================================================================

INGAME MENU

=======================================================================
*/

#define	INGAME_ITEMS	5

static const char *names[] = {
	"Options",
	"Servers",
	"Demos",
	"Disconnect",
	"Quit",
	NULL
};

typedef struct ingameMenu_s {
	menuFrameWork_t	menu;
	menuAction_t actions[INGAME_ITEMS];
} ingameMenu_t;

static ingameMenu_t	m_ingame;

static void IngameMenu_QuitAction( qboolean yes ) {
	if( yes ) {
		cmd.ExecuteText( EXEC_APPEND, "quit\n" );
	} else {
		UI_PopMenu();
	}
}


static int IngameMenu_Callback( int id, int msg, int param ) {
	switch( msg ) {
	case QM_ACTIVATE:
		switch( id ) {
		case 0:
			M_Menu_Options_f();
			break;
		case 1:
			M_Menu_Multiplayer_f();
			break;
		case 2:
			M_Menu_Demos_f();
			break;
		case 3:
			cmd.ExecuteText( EXEC_APPEND, "disconnect\n" );
			UI_PopMenu();
			return QMS_SILENT;
		case 4:
			M_Menu_Confirm_f( "Quit game? y/n", IngameMenu_QuitAction );
			break;
		}
		return QMS_IN;
	case QM_SIZE:
        Menu_Size( &m_ingame.menu );
		break;
	default:
		break;
	}

	return QMS_NOTHANDLED;
}

static void IngameMenu_Draw( menuFrameWork_t *self ) {
	int y1, y2;
    static const color_t color = { 0, 0, 255, 32 };

	y1 = ( uis.height - MENU_SPACING * INGAME_ITEMS ) / 2 - MENU_SPACING;
	y2 = ( uis.height + MENU_SPACING * INGAME_ITEMS ) / 2 + MENU_SPACING;

	ref.DrawFillEx( 0, y1, uis.width, y2 - y1, color );

	Menu_Draw( self );
}



static void IngameMenu_Init( void ) {
	int i;

	memset( &m_ingame, 0, sizeof( m_ingame ) );

	m_ingame.menu.callback = IngameMenu_Callback;
	m_ingame.menu.draw = IngameMenu_Draw;
	m_ingame.menu.transparent = qtrue;

	for( i = 0; i < INGAME_ITEMS; i++ ) {
		m_ingame.actions[i].generic.type = MTYPE_ACTION;
		m_ingame.actions[i].generic.id = i;
		m_ingame.actions[i].generic.name = names[i];
		m_ingame.actions[i].generic.uiFlags = UI_CENTER|UI_DROPSHADOW;
	
		Menu_AddItem( &m_ingame.menu, &m_ingame.actions[i] );
	}

	m_ingame.actions[0].generic.flags = QMF_HASFOCUS;

}


void M_Menu_Ingame_f( void ) {
	IngameMenu_Init();
	UI_PushMenu( &m_ingame.menu );
}

