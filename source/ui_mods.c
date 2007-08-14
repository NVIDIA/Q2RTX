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

MODS MENU

=======================================================================
*/

#define ID_LIST		102

#define MAX_LISTED_MODS		32

typedef struct modsMenu_s {
	menuFrameWork_t	menu;
	menuList_t list;
	menuStatic_t banner;
	int numMods;
	char *names[MAX_LISTED_MODS+1];
	char *directories[MAX_LISTED_MODS];
	char **modlist;
	int lastClick;
} modsMenu_t;

static modsMenu_t	m_mods;

static int ModsMenu_Callback( int id, int msg, int param ) {
	switch( msg ) {
	case QM_ACTIVATE:		
		cvar.Set( "game", m_mods.directories[m_mods.list.curvalue] );
		cmd.ExecuteText( EXEC_APPEND, "fs_restart" );
		UI_ForceMenuOff();
		return QMS_SILENT;
	case QM_DESTROY:
		if( m_mods.modlist ) {
			fs.FreeFileList( m_mods.modlist );
			m_mods.modlist = NULL;
		}
		break;
	default:
		break;
	}

	return QMS_NOTHANDLED;
}

static void ModsMenu_Init( void ) {
	int i;
	char *p;
	char *current;

	memset( &m_mods, 0, sizeof( m_mods ) );

	m_mods.names[0] = "Quake II";
	m_mods.directories[0] = "";

	current = cvar.VariableString( "game" );
	
	if( ( m_mods.modlist = fs.ListFiles( "$modlist", NULL, 0, &m_mods.numMods ) ) != NULL ) {
		if( m_mods.numMods > MAX_LISTED_MODS - 1 ) {
			m_mods.numMods = MAX_LISTED_MODS - 1;
		}
		for( i=0 ; i<m_mods.numMods ; i++ ) {
			m_mods.directories[i + 1] = m_mods.modlist[i];
			if( ( p = strchr( m_mods.modlist[i], '\n' ) ) != NULL ) {
				*p = 0; // TODO: file list should remain read-only...
				m_mods.names[i + 1] = p + 1;
			} else {
				m_mods.names[i + 1] = m_mods.modlist[i];
			}

			if( *current && !strcmp( m_mods.modlist[i], current ) ) {
				m_mods.list.curvalue = i + 1;
			}
		}

		m_mods.names[i + 1] = NULL;
	}

	m_mods.menu.callback = ModsMenu_Callback;

	m_mods.list.generic.type	= MTYPE_LIST;
	m_mods.list.generic.id		= ID_LIST;
	m_mods.list.generic.flags	= QMF_HASFOCUS;
	m_mods.list.generic.x		= ( uis.glconfig.vidWidth - 300 ) / 2;
	m_mods.list.generic.y		= 32;
	m_mods.list.generic.width	= 0;
	m_mods.list.generic.height	= uis.glconfig.vidHeight - 64;
	m_mods.list.generic.name	= NULL;
	m_mods.list.mlFlags			= MLF_HIDE_SCROLLBAR_EMPTY;
	m_mods.list.itemnames		= ( const char ** )m_mods.names;
	m_mods.list.numcolumns		= 1;
	m_mods.list.columns[0].width	= 300;
	m_mods.list.columns[0].name	= NULL;
	m_mods.list.columns[0].uiFlags	= UI_CENTER;

	UI_SetupDefaultBanner( &m_mods.banner, "Mods" );

	m_mods.menu.statusbar = "Press Enter to load";

	Menu_AddItem( &m_mods.menu, (void *)&m_mods.list );
	Menu_AddItem( &m_mods.menu, (void *)&m_mods.banner );

}


void M_Menu_Mods_f( void ) {
	ModsMenu_Init();
	UI_PushMenu( &m_mods.menu );
}
