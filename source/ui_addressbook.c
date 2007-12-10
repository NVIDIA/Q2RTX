/*
Copyright (C) 1997-2001 Id Software, Inc.

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
=============================================================================

ADDRESS BOOK MENU

=============================================================================
*/


typedef struct m_addressBook_s {
	menuFrameWork_t	menu;
	menuField_t		fields[MAX_LOCAL_SERVERS];
	menuStatic_t	banner;
} m_addressBook_t;

static m_addressBook_t	m_addressBook;

static void SaveChanges( void ) {
	char buffer[32];
	int index;

	for( index = 0; index < MAX_LOCAL_SERVERS; index++ ) {
		Com_sprintf( buffer, sizeof( buffer ), "adr%d", index );
		cvar.Set( buffer, m_addressBook.fields[index].field.text );
	}
}

static int AddressBook_MenuCallback( int id, int msg, int param ) {
	switch( msg ) {
	case QM_DESTROY:
        SaveChanges();
		break;
	default:
		break;
	}

	return QMS_NOTHANDLED;
}

static void AddressBook_MenuInit( void ) {
	char buffer[32];
	int i, y;

	memset( &m_addressBook, 0, sizeof( m_addressBook ) );

	m_addressBook.menu.callback = AddressBook_MenuCallback;

	y = 64;
	for( i = 0; i < MAX_LOCAL_SERVERS; i++ ) {
		Com_sprintf( buffer, sizeof( buffer ), "adr%d", i );

		m_addressBook.fields[i].generic.type	= MTYPE_FIELD;
		m_addressBook.fields[i].generic.name	= NULL;
		m_addressBook.fields[i].generic.x		= ( uis.width - 30 * CHAR_WIDTH ) / 2 - RCOLUMN_OFFSET;
		m_addressBook.fields[i].generic.y		= y;
		y += MENU_SPACING;

		IF_InitText( &m_addressBook.fields[i].field, 30, 60, cvar.VariableString( buffer ) );

		Menu_AddItem( &m_addressBook.menu, &m_addressBook.fields[i] );
	}

	UI_SetupDefaultBanner( &m_addressBook.banner, "Address Book" );

	Menu_AddItem( &m_addressBook.menu, &m_addressBook.banner );

	
}

void M_Menu_AddressBook_f( void ) {
	AddressBook_MenuInit();
	UI_PushMenu( &m_addressBook.menu );
}

