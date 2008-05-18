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

typedef struct {
	menuFrameWork_t	menu;
	menuStatic_t text;
	confirmAction_t action;
} confirmMenu_t;

static confirmMenu_t	m_confirm;

static menuSound_t ConfirmKeydown( menuFrameWork_t *self, int key ) {
    switch( key ) {
    case 'Y':
    case 'y':
        m_confirm.action( qtrue );
        // UI_PopMenu();
        return QMS_IN;
    case 'N':
    case 'n':
        m_confirm.action( qfalse );
        UI_PopMenu();
        return QMS_OUT;
    default:
    	return QMS_NOTHANDLED;
    }
}

void M_Menu_Confirm( const char *text, confirmAction_t action ) {
	memset( &m_confirm, 0, sizeof( m_confirm ) );

    m_confirm.menu.keydown = ConfirmKeydown;
    m_confirm.menu.image = uis.backgroundHandle;
    *( uint32_t * )m_confirm.menu.color = *( uint32_t * )colorBlack;

	m_confirm.text.generic.type = MTYPE_STATIC;
	m_confirm.text.generic.name = ( char * )text;
	m_confirm.text.generic.uiFlags = UI_CENTER;

    m_confirm.action = action;
	
	Menu_AddItem( &m_confirm.menu, &m_confirm.text );

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

static menuSound_t ErrorKeydown( menuFrameWork_t *self, int key ) {
	UI_PopMenu();
	return QMS_OUT;
}

void M_Menu_Error( comErrorType_t type, const char *text ) {
    color_t color;

	if( !text ) {
        return;
    }
    switch( type ) {
    case ERR_SILENT:
		return;
    case ERR_DROP:
        *( uint32_t * )color = *( uint32_t * )colorRed;
        break;
    default:
        *( uint32_t * )color = *( uint32_t * )colorYellow;
        break;
	}

	memset( &m_error, 0, sizeof( m_error ) );

	m_error.menu.keydown = ErrorKeydown;
    m_error.menu.image = uis.backgroundHandle;
    *( uint32_t * )m_error.menu.color = *( uint32_t * )colorBlack;

	m_error.text.generic.type = MTYPE_STATIC;
	m_error.text.generic.flags = QMF_CUSTOM_COLOR;
	m_error.text.generic.name = ( char * )text;
	m_error.text.generic.uiFlags = UI_CENTER|UI_MULTILINE;
	*( uint32_t * )m_error.text.generic.color = *( uint32_t * )color;
	
	Menu_AddItem( &m_error.menu, ( void * )&m_error.text );

	UI_PushMenu( &m_error.menu );
}

