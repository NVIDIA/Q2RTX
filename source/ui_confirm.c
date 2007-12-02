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
=======================================================================

CONFIRM MENU

=======================================================================
*/

typedef struct m_confirmMenu_s {
	menuFrameWork_t	menu;
	menuStatic_t text;

	void (*action)( qboolean yes );
} m_confirmMenu_t;

static m_confirmMenu_t	m_confirm;

static int ConfirmMenu_Callback( int id, int msg, int param ) {
	switch( msg ) {
	case QM_KEY:
		switch( param ) {
		case 'Y':
		case 'y':
			//UI_PopMenu();
			m_confirm.action( qtrue );
			return QMS_IN;
		case 'N':
		case 'n':
			UI_PopMenu();
			m_confirm.action( qfalse );
			return QMS_OUT;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return QMS_NOTHANDLED;
}

static void ConfirmMenu_Init( const char *text, void (*action)( qboolean yes ) ) {
	memset( &m_confirm, 0, sizeof( m_confirm ) );

	m_confirm.menu.callback = ConfirmMenu_Callback;
	m_confirm.action = action;

	m_confirm.text.generic.type = MTYPE_STATIC;
	m_confirm.text.generic.name = text;
	m_confirm.text.generic.x = uis.width / 2;
	m_confirm.text.generic.y = uis.height / 2;
	m_confirm.text.generic.uiFlags = UI_CENTER;
	
	Menu_AddItem( &m_confirm.menu, ( void * )&m_confirm.text );
}


void M_Menu_Confirm_f( const char *text, void (*action)( qboolean yes ) ) {
	if( !text || !action ) {
		Com_Error( ERR_FATAL, "M_Menu_Confirm_f: bad params" );
	}
	ConfirmMenu_Init( text, action );
	UI_PushMenu( &m_confirm.menu );
}

/*
=======================================================================

ERROR MENU

=======================================================================
*/

typedef struct m_errorMenu_s {
	menuFrameWork_t	menu;
	menuStatic_t text;
} m_errorMenu_t;

static m_errorMenu_t	m_error;

static int ErrorMenu_Callback( int id, int msg, int param ) {
	if( msg == QM_KEY ) {
		UI_PopMenu();
		return QMS_OUT;
	}

	return QMS_NOTHANDLED;
}

static void ErrorMenu_Init( comErrorType_t type, const char *text ) {
	memset( &m_error, 0, sizeof( m_error ) );

	m_error.menu.callback = ErrorMenu_Callback;

	m_error.text.generic.type = MTYPE_STATIC;
	m_error.text.generic.flags = QMF_CUSTOM_COLOR;
	m_error.text.generic.name = text;
	m_error.text.generic.x = uis.width / 2;
	m_error.text.generic.y = uis.height / 2;
	m_error.text.generic.uiFlags = UI_CENTER|UI_MULTILINE;
	if( type == ERR_DROP ) {
		*( uint32 * )m_error.text.generic.color = *( uint32 * )colorRed;
	} else {
		*( uint32 * )m_error.text.generic.color = *( uint32 * )colorYellow;
	}
	
	Menu_AddItem( &m_error.menu, ( void * )&m_error.text );
}


void M_Menu_Error_f( comErrorType_t type, const char *text ) {
	if( type == ERR_SILENT ) {
		return;
	}
	if( !text ) {
		return;
	}
	ErrorMenu_Init( type, text );
	UI_PushMenu( &m_error.menu );
}
