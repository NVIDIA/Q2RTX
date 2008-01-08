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
=============================================================================

JOIN SERVER MENU

=============================================================================
*/

#define ID_LIST		101

#define MAX_STATUS_RULES	64
#define MAX_STATUS_SERVERS  64

typedef struct serverSlot_s {
	qboolean		valid;
	char			*rules[MAX_STATUS_RULES];
	int				numRules;
	char			*players[MAX_STATUS_PLAYERS];
	int				numPlayers;
	char			address[MAX_QPATH];
	char			realAddress[MAX_QPATH];
} serverSlot_t;

typedef struct m_joinServer_s {
	menuFrameWork_t	menu;
	menuList_t		list;
	menuList_t		info;
	menuList_t		players;
    qboolean        active;
    qboolean        cursorSet;

	serverSlot_t	servers[MAX_STATUS_SERVERS];
	char			*names[MAX_STATUS_SERVERS];
} m_joinServer_t;

static m_joinServer_t	m_join;

static void UpdateSelection( void ) {
	serverSlot_t *s;

	s = &m_join.servers[m_join.list.curvalue];

	if( s->valid ) {
		m_join.info.generic.flags &= ~QMF_HIDDEN;
		m_join.info.items = ( void ** )s->rules;
		m_join.info.numItems = s->numRules;

		if( s->numPlayers ) {
			m_join.players.generic.flags &= ~QMF_HIDDEN;
			m_join.players.items = ( void ** )s->players;
			m_join.players.numItems = s->numPlayers;
		} else {
			m_join.players.generic.flags |= QMF_HIDDEN;
			m_join.players.items = NULL;
			m_join.players.numItems = 0;
		}

		m_join.menu.statusbar = "Press Enter to connect; Space to refresh";
	} else {
		m_join.info.generic.flags |= QMF_HIDDEN;
		m_join.info.items = NULL;
		m_join.info.numItems = 0;

		m_join.players.generic.flags |= QMF_HIDDEN;
		m_join.players.items = NULL;
		m_join.players.numItems = 0;

		m_join.menu.statusbar = "Press Space to refresh; Hold ALT to refresh all";
	}
}

static void ClearSlot( serverSlot_t *slot ) {
    int i;

    for( i = 0; i < slot->numRules; i++ ) {
        com.Free( slot->rules[i] );
        slot->rules[i] = NULL;
    }
    for( i = 0; i < slot->numPlayers; i++ ) {
        com.Free( slot->players[i] );
        slot->players[i] = NULL;
    }
    slot->numRules = slot->numPlayers = 0;
    slot->valid = qfalse;
}

void UI_AddToServerList( const serverStatus_t *status ) {
	serverSlot_t *slot;
	int		i, j;
	char *host, *map;
	const char *info = status->infostring;
	char key[MAX_STRING_CHARS];
	char value[MAX_STRING_CHARS];
	const playerStatus_t *player;

    if( !m_join.active ) {
        return;
    }

	// see if already added
	for( i = 0, slot = m_join.servers; i < m_join.list.numItems; i++, slot++ ) {
		if( !strcmp( status->address, slot->realAddress ) ) {
			break;
		}
	}

	if( i == m_join.list.numItems ) {
		// create new slot
		if( m_join.list.numItems == MAX_STATUS_SERVERS ) {
			return;
		}
        strcpy( slot->realAddress, status->address );
        strcpy( slot->address, status->address );
        if( !m_join.cursorSet ) {
            m_join.list.curvalue = i;
            m_join.cursorSet = qtrue;
        }
		m_join.list.numItems++;
        m_join.names[m_join.list.numItems] = NULL;
	}

    host = Info_ValueForKey( info, "hostname" );
    if( !host[0] ) {
        host = slot->address;
    }

	map = Info_ValueForKey( info, "mapname" );
    if( !map[0] ) {
        map = "???";
    }

    j = atoi( Info_ValueForKey( info, "maxclients" ) );
    Com_sprintf( key, sizeof( key ), "%d/%d", status->numPlayers, j );

	if( m_join.names[i] ) {
		com.Free( m_join.names[i] );
	}
	m_join.names[i] = UI_FormatColumns( 0, host, map, key, NULL );

    ClearSlot( slot );

	do {
		Info_NextPair( &info, key, value );
		if( !key[0] ) {
			break;
		}
		slot->rules[slot->numRules++] =
            UI_FormatColumns( 0, key, value, NULL );
	} while( info && slot->numRules < MAX_STATUS_RULES );

	for( i = 0, player = status->players ; i < status->numPlayers; i++, player++ ) {
        Com_sprintf( key, sizeof( key ), "%d", player->score );
        Com_sprintf( value, sizeof( value ), "%d", player->ping );
		slot->players[i] = UI_FormatColumns( 0,
			key, value, player->name, NULL );
	}
	slot->numPlayers = status->numPlayers;

	slot->valid = qtrue;

	UpdateSelection();
}

static void PingSelected( void ) {
	serverSlot_t *slot = &m_join.servers[m_join.list.curvalue];

	if( m_join.names[m_join.list.curvalue] ) {
		com.Free( m_join.names[m_join.list.curvalue] );
	}
	m_join.names[m_join.list.curvalue] = UI_FormatColumns( 0,
        slot->address, "???", "?/?", NULL );
    
    ClearSlot( slot );
    
    UpdateSelection();
    m_join.menu.statusbar = "Pinging servers, please wait...";
    client.UpdateScreen();
    
	client.SendStatusRequest( slot->realAddress, 0 );

	UpdateSelection();
}

static void AddUnlistedServers( void ) {
	serverSlot_t *slot;
    cvar_t *var;
	int i, j;
    
    m_join.active = qtrue;

    // ping broadcast
    client.SendStatusRequest( NULL, 0 );

	for( i = 0; i < MAX_STATUS_SERVERS; i++ ) {
		var = cvar.Find( va( "adr%i", i ) );
        if( !var ) {
            break;
        }
		if( !var->string[0] ) {
			continue;
		}

		// ignore if already listed
		for( j = 0, slot = m_join.servers; j < m_join.list.numItems; j++, slot++ ) {
			if( !Q_stricmp( var->string, slot->address ) ) {
				break;
			}
		}

		if( j != m_join.list.numItems ) {
			continue;
		}

		if( m_join.list.numItems == MAX_STATUS_SERVERS ) {
			break;
		}

        // save original address
        Q_strncpyz( slot->address, var->string, sizeof( slot->address ) );
        Q_strncpyz( slot->realAddress, var->string, sizeof( slot->realAddress ) );

		m_join.names[m_join.list.numItems++] = UI_FormatColumns( 0,
            slot->address, "???", "?/?", NULL );

        // ping and resolve real ip
        client.SendStatusRequest( slot->realAddress, sizeof( slot->realAddress ) );

        client.UpdateScreen();
	}
}

static void FreeListedServers( void ) {
	int i;

	for( i = 0; i < m_join.list.numItems; i++ ) {
        ClearSlot( &m_join.servers[i] );
	}

	for( i = 0; i < m_join.list.numItems; i++ ) {
		if( m_join.names[i] ) {
			com.Free( m_join.names[i] );
			m_join.names[i] = NULL;
		}
	}

	m_join.list.numItems = 0;
	m_join.info.items = NULL;
	m_join.info.numItems = 0;
	//m_join.list.curvalue = 0;
	//m_join.list.prestep = 0;
    m_join.active = qfalse;
}


static void PingServers( void ) {
	FreeListedServers();
	client.StopAllSounds();
	UpdateSelection();

    m_join.menu.statusbar = "Pinging servers, please wait...";
    client.UpdateScreen();

	// send out info packets
	AddUnlistedServers();
    
	UpdateSelection();
}

static void Resize( void ) {
	int w1 = uis.width * 0.75f;
	int w2 = uis.width * 0.25f;

//
// server list
//
	m_join.list.generic.x = 0;
	m_join.list.generic.y = 8;
	m_join.list.generic.height	= uis.height / 2 - 8;

	m_join.list.columns[0].width = w1 - 144;
	m_join.list.columns[1].width = 80;
	m_join.list.columns[2].width = 64;

//
// server info
//
	m_join.info.generic.y		= uis.height / 2 + 1;
	m_join.info.generic.height	= uis.height / 2 - 8 - 2;

	m_join.info.columns[0].width = w1 / 2;
	m_join.info.columns[1].width = w1 - w1 / 2;

//
// player list
//
	m_join.players.generic.x		= w1 + 10;
	m_join.players.generic.y        = 8;
	m_join.players.generic.height	= uis.height - 16 - 1;

	m_join.players.columns[0].width = 32;
	m_join.players.columns[1].width = 32;
	m_join.players.columns[2].width = w2 - 64;
}

static int JoinServer_MenuCallback( int id, int msg, int param ) {
	switch( msg ) {
	case QM_ACTIVATE:
		if( id != ID_LIST ) {
			break;
		}
		cmd.ExecuteText( EXEC_APPEND, va( "connect \"%s\"\n",
			m_join.servers[m_join.list.curvalue].realAddress ) );
		UI_PopMenu();
		return QMS_IN;

	case QM_KEY:
		if( param != 32 ) {
			break;
		}
        if( !keys.IsDown( K_ALT ) ) {
            PingSelected();
        } else {
    		PingServers();
        }
		return QMS_SILENT;

	case QM_CHANGE:
		if( id != ID_LIST ) {
			break;
		}
		UpdateSelection();
		return QMS_MOVE;

	case QM_DESTROY:
		FreeListedServers();
		break;

	case QM_DESTROY_CHILD:
		FreeListedServers();
		AddUnlistedServers();
		break;

	case QM_SIZE:
        Resize();
        break;

	default:
		break;
	}

	return QMS_NOTHANDLED;
}

void JoinServer_MenuInit( void ) {
	memset( &m_join, 0, sizeof( m_join ) );

//
// server list
//
	m_join.list.generic.type	= MTYPE_LIST;
	m_join.list.generic.id		= ID_LIST;
	m_join.list.generic.flags	= QMF_LEFT_JUSTIFY|QMF_HASFOCUS;
	m_join.list.items		    = ( void ** )m_join.names;
	m_join.list.numcolumns		= 3;

	m_join.list.columns[0].uiFlags  = UI_LEFT;
	m_join.list.columns[0].name     = "Hostname";
	m_join.list.columns[1].uiFlags  = UI_CENTER;
	m_join.list.columns[1].name     = "Map";
	m_join.list.columns[2].uiFlags  = UI_CENTER;
	m_join.list.columns[2].name     = "Players";

//
// server info
//
	m_join.info.generic.type	= MTYPE_LIST;
	m_join.info.generic.flags	= QMF_LEFT_JUSTIFY|QMF_HIDDEN;
	m_join.info.numcolumns		= 2;

	m_join.info.columns[0].uiFlags  = UI_LEFT;
	m_join.info.columns[0].name     = "Key";
	m_join.info.columns[1].uiFlags  = UI_LEFT;
	m_join.info.columns[1].name     = "Value";

//
// player list
//
	m_join.players.generic.type		= MTYPE_LIST;
	m_join.players.generic.flags	= QMF_LEFT_JUSTIFY|QMF_HIDDEN|QMF_DISABLED;
	m_join.players.mlFlags			= MLF_HIDE_SCROLLBAR;
	m_join.players.numcolumns		= 3;

	m_join.players.columns[0].uiFlags   = UI_CENTER;
	m_join.players.columns[0].name      = "Frg";
	m_join.players.columns[1].uiFlags   = UI_CENTER;
	m_join.players.columns[1].name      = "RTT";
	m_join.players.columns[2].uiFlags   = UI_LEFT;
	m_join.players.columns[2].name      = "Name";

	m_join.menu.callback = JoinServer_MenuCallback;
	m_join.menu.banner = "Server Browser";

	Menu_AddItem( &m_join.menu, &m_join.list );
	Menu_AddItem( &m_join.menu, &m_join.info );
	Menu_AddItem( &m_join.menu, &m_join.players );
}


void M_Menu_Multiplayer_f( void ) {
	JoinServer_MenuInit();
	UI_PushMenu( &m_join.menu );
    
    PingServers();
}

