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
} m_addressBook_t;

static m_addressBook_t	m_addressBook;

static void SaveChanges( void ) {
	char buffer[8];
	int i;

	for( i = 0; i < MAX_LOCAL_SERVERS; i++ ) {
		Com_sprintf( buffer, sizeof( buffer ), "adr%d", i );
		cvar.Set( buffer, m_addressBook.fields[i].field.text );
	}
}

static int AddressBook_MenuCallback( int id, int msg, int param ) {
	switch( msg ) {
	case QM_DESTROY:
        SaveChanges();
		break;
    case QM_SIZE:
        Menu_Size( &m_addressBook.menu );
        break;
	default:
		break;
	}

	return QMS_NOTHANDLED;
}

static void AddressBook_MenuInit( void ) {
	char buffer[8];
	int i;

	memset( &m_addressBook, 0, sizeof( m_addressBook ) );

	m_addressBook.menu.callback = AddressBook_MenuCallback;

	for( i = 0; i < MAX_LOCAL_SERVERS; i++ ) {
		Com_sprintf( buffer, sizeof( buffer ), "adr%d", i );

		m_addressBook.fields[i].generic.type	= MTYPE_FIELD;

		IF_Init( &m_addressBook.fields[i].field, 30, 60,
            cvar.VariableString( buffer ) );

		Menu_AddItem( &m_addressBook.menu, &m_addressBook.fields[i] );
	}

	m_addressBook.fields[0].generic.flags	= QMF_HASFOCUS;

    m_addressBook.menu.banner = "Address Book";
}

void M_Menu_AddressBook_f( void ) {
	AddressBook_MenuInit();
	UI_PushMenu( &m_addressBook.menu );
}

