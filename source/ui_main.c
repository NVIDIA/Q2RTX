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

MAIN MENU

=======================================================================
*/

#define	MAIN_ITEMS	4

static const char names[MAIN_ITEMS][16] = {
	"Servers",
	"Demos",
	"Options",
	"Quit"
};

typedef struct mainMenu_s {
	menuFrameWork_t menu;
	menuAction_t    actions[MAIN_ITEMS];
	menuStatic_t	banner;
} mainMenu_t;

static mainMenu_t	m_main;

static void MainMenu_QuitAction( qboolean yes ) {
	if( yes ) {
		cmd.ExecuteText( EXEC_APPEND, "quit\n" );
	}
}

static int MainMenu_Callback( int id, int msg, int param ) {
	switch( msg ) {
	case QM_ACTIVATE:
		switch( id ) {
		case 0:
			M_Menu_Multiplayer_f();
			break;
		case 1:
			M_Menu_Demos_f();
			break;
		case 2:
			M_Menu_Options_f();
			break;
		case 3:
			M_Menu_Confirm_f( "Quit game? y/n", MainMenu_QuitAction );
			break;
		}
		return QMS_IN;
	case QM_DESTROY:
		break;
	default:
		break;
	}

	return QMS_NOTHANDLED;
}

static void MainMenu_Init( void ) {
	int i;
	int x, y;

	x = uis.width / 2;
	y = ( uis.height - MENU_SPACING * MAIN_ITEMS ) / 2;

	memset( &m_main, 0, sizeof( m_main ) );

	m_main.menu.callback = MainMenu_Callback;

	for( i = 0; i < MAIN_ITEMS; i++ ) {
		m_main.actions[i].generic.type = MTYPE_ACTION;
		m_main.actions[i].generic.id = i;
		m_main.actions[i].generic.name = names[i];
		m_main.actions[i].generic.x = x;
		m_main.actions[i].generic.y = y;
		m_main.actions[i].generic.uiFlags = UI_CENTER;
		y += MENU_SPACING;
	
		Menu_AddItem( &m_main.menu, &m_main.actions[i] );
	}

	m_main.actions[0].generic.flags = QMF_HASFOCUS;

	UI_SetupDefaultBanner( &m_main.banner, "Main Menu" );

	Menu_AddItem( &m_main.menu, (void *)&m_main.banner );
}


void M_Menu_Main_f( void ) {
	MainMenu_Init();
	UI_PushMenu( &m_main.menu );
}
