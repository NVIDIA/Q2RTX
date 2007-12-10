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

OPTIONS MENU

=======================================================================
*/

#define OPTIONS_ITEMS   7

static const char names[][16] = {
	"Player Setup",
	"Keys",
	"Weapons",
//	"Game",
	"Video",
//	"Sound",
	"Network",
	"Address Book",
    "Credits"
};

typedef struct optionsMenu_s {
	menuFrameWork_t	menu;
	menuAction_t	actions[OPTIONS_ITEMS];
} optionsMenu_t;

static optionsMenu_t	m_options;


static int OptionsMenu_Callback( int id, int msg, int param ) {
	switch( msg ) {
	case QM_ACTIVATE:
		switch( id ) {
		case 0:
			M_Menu_PlayerConfig_f();
			break;
		case 1:
			M_Menu_Keys_f();
			break;
		case 2:
			M_Menu_Weapons_f();
			break;
		/*case 3:
			M_Menu_Interface_f();
			break;*/
		case 3:
			M_Menu_Video_f();
			break;
		/*case 5:
			M_Menu_Sound_f();
			break;*/
		case 4:
			M_Menu_Network_f();
			break;
		case 5:
			M_Menu_AddressBook_f();
			break;
		case 6:
			M_Menu_Credits_f();
			break;
		}
		return QMS_IN;
    case QM_SIZE:
        Menu_Size( &m_options.menu );
        break;
	default:
		break;
	}

	return QMS_NOTHANDLED;
}

static void OptionsMenu_Init( void ) {
	int i;

	memset( &m_options, 0, sizeof( m_options ) );

	m_options.menu.callback = OptionsMenu_Callback;
	m_options.menu.banner = "Options";

	for( i = 0; i < OPTIONS_ITEMS; i++ ) {
		m_options.actions[i].generic.type = MTYPE_ACTION;
		m_options.actions[i].generic.id = i;
		m_options.actions[i].generic.name = names[i];
		m_options.actions[i].generic.uiFlags = UI_CENTER;

		Menu_AddItem( &m_options.menu, &m_options.actions[i] );
	}

	m_options.actions[0].generic.flags = QMF_HASFOCUS;
}


void M_Menu_Options_f( void ) {
	OptionsMenu_Init();
	UI_PushMenu( &m_options.menu );
}

