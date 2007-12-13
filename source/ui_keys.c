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

#define MAX_KEYBINDS	32

static const char *keybinds[][2] = {
	{ "+attack", "attack" },
	{ "weapnext", "next weapon" },
	{ "+forward", "walk forward" },
	{ "+back", "backpedal" },
	{ "+left", "turn left" },
	{ "+right", "turn right" },
	{ "+speed", "run" },
	{ "+moveleft", "step left" },
	{ "+moveright", "step right" },
	{ "+strafe", "sidestep" },
	{ "+lookup", "look up" },
	{ "+lookdown", "look down" },
	{ "centerview", "center view" },
	{ "+mlook", "mouse look" },
	{ "+klook", "keyboard look" },
	{ "+moveup", "up / jump" },
	{ "+movedown", "down / crouch" },

	{ "inven", "inventory" },
	{ "invuse", "use item" },
	{ "invdrop", "drop item" },
	{ "invprev", "prev item" },
	{ "invnext", "next item" },

	{ "help", "help computer" },

	{ "pause", "pause game" }
};

static const int numKeyBinds = sizeof( keybinds ) / sizeof( keybinds[0] );

static const char *weapbinds[][2] = {
	{ "use Blaster", "blaster" },
	{ "use Shotgun", "shotgun" },
	{ "use Super Shotgun", "super shotgun" },
	{ "use Machinegun", "machinegun" },
	{ "use Chaingun", "chaingun" },
	{ "use Grenade Launcher", "grenade launcher" },
	{ "use Rocket Launcher", "rocket launcher" },
	{ "use HyperBlaster", "hyperblaster" },
	{ "use Railgun", "railgun" },
	{ "use BFG10K", "bfg10k" }
};

static const int numWeapBinds = sizeof( weapbinds ) / sizeof( weapbinds[0] );


typedef struct keysMenu_s {
	menuFrameWork_t	menu;
	menuKeybind_t	actions[MAX_KEYBINDS];
	const char		*binds[MAX_KEYBINDS];
	int				current;
	int				total;
} keysMenu_t;

static keysMenu_t	m_keys;

static void KeysMenu_Update( void ) {
	menuKeybind_t *k;
	int i, key;

	for( i = 0, k = m_keys.actions; i < m_keys.total; i++, k++ ) {
		key = keys.EnumBindings( 0, m_keys.binds[i] );
		k->altbinding[0] = 0;
		if( key == -1 ) {
			strcpy( k->binding, "???" );
		} else {
			strcpy( k->binding, keys.KeyNumToString( key ) );
			key = keys.EnumBindings( key + 1, m_keys.binds[i] );
			if( key != -1 ) {
				strcpy( k->altbinding, keys.KeyNumToString( key ) );
			}
		}
	}
}

static void KeysMenu_Remove( const char *binding ) {
	int key;

	for( key = 0; ; key++ ) {
		key = keys.EnumBindings( key, binding );
		if( key == -1 ) {
			break;
		}
		keys.SetBinding( key, NULL );
	}
}


static int KeysMenu_Callback( int id, int msg, int param ) {
	menuKeybind_t *k;
	

	switch( msg ) {
	case QM_ACTIVATE:
		if( id >= 0 && id < m_keys.total ) {
			m_keys.menu.keywait = qtrue;
			m_keys.menu.statusbar = "Press the desired key, Escape to cancel";
		}
		return QMS_IN;
	case QM_GOTFOCUS:
		m_keys.current = id;
		break;
	case QM_KEY:
		if( id == ID_MENU && m_keys.menu.keywait ) {
			if( param != K_ESCAPE && m_keys.current >= 0 && m_keys.current < m_keys.total ) {
				k = &m_keys.actions[m_keys.current];
				if( k->altbinding[0] ) {
					KeysMenu_Remove( m_keys.binds[m_keys.current] );
				}
				keys.SetBinding( param, m_keys.binds[m_keys.current] );
			}
			KeysMenu_Update();
			m_keys.menu.keywait = qfalse;
			m_keys.menu.statusbar = "Press Enter to change, Backspace to clear";
			return QMS_OUT;
		} else if( param == K_BACKSPACE || param == K_DEL ) {
			if( id >= 0 && id < m_keys.total ) {
				KeysMenu_Remove( m_keys.binds[id] );
				KeysMenu_Update();
				return QMS_IN;
			}
		}
		break;
    case QM_SIZE:
        Menu_Size( &m_keys.menu );
        break;
	default:
		break;
	}

	return QMS_NOTHANDLED;
}

static void KeysMenu_Init( const char **names, int total, char *banner ) {
	menuKeybind_t *k;
	int i;

	memset( &m_keys, 0, sizeof( m_keys ) );

	m_keys.menu.callback = KeysMenu_Callback;
    m_keys.menu.banner = banner;
	m_keys.total = total;

	for( i = 0, k = m_keys.actions; i < total; i++, k++ ) {
		m_keys.binds[i] = names[0];
		k->generic.type = MTYPE_KEYBIND;
		k->generic.id = i;
		k->generic.name = names[1];
		k->generic.uiFlags = UI_CENTER;

		Menu_AddItem( &m_keys.menu, &m_keys.actions[i] );

		names += 2;
	}

	KeysMenu_Update();

	m_keys.menu.statusbar = "Press Enter to change, Backspace to clear";

	m_keys.actions[0].generic.flags = QMF_HASFOCUS;
}


void M_Menu_Keys_f( void ) {
	KeysMenu_Init( ( const char ** )keybinds, numKeyBinds, "Key Bindings" );
	UI_PushMenu( &m_keys.menu );
}

void M_Menu_Weapons_f( void ) {
	KeysMenu_Init( ( const char ** )weapbinds, numWeapBinds, "Weapon Bindings" );
	UI_PushMenu( &m_keys.menu );
}



