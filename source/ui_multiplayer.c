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

typedef struct serverSlot_s {
	qboolean		valid;
	char			*rules[MAX_STATUS_RULES+1];
	int				numRules;
	char			*players[MAX_STATUS_PLAYERS+1];
	int				numPlayers;
	char			address[MAX_QPATH];
	char			realAddress[MAX_QPATH];
} serverSlot_t;

typedef struct m_joinServer_s {
	menuFrameWork_t	menu;
	menuList_t		list;
	menuList_t		info;
	menuList_t		players;
	menuStatic_t	banner;
    qboolean        active;
    qboolean        cursorSet;

	serverSlot_t	servers[MAX_LOCAL_SERVERS];
	char			*names[MAX_LOCAL_SERVERS+1];
} m_joinServer_t;

static m_joinServer_t	m_join;

static void UpdateSelection( void ) {
	serverSlot_t *s;

	s = &m_join.servers[m_join.list.curvalue];

	if( s->valid ) {
		m_join.info.generic.flags &= ~QMF_HIDDEN;
		m_join.info.itemnames = ( const char ** )s->rules;
		m_join.info.numItems = s->numRules;

		if( s->numPlayers ) {
			m_join.players.generic.flags &= ~QMF_HIDDEN;
			m_join.players.itemnames = ( const char ** )s->players;
			m_join.players.numItems = s->numPlayers;
		} else {
			m_join.players.generic.flags |= QMF_HIDDEN;
			m_join.players.itemnames = NULL;
			m_join.players.numItems = 0;
		}

		m_join.menu.statusbar = "Press Enter to connect; Space to refresh";
	} else {
		m_join.info.generic.flags |= QMF_HIDDEN;
		m_join.info.itemnames = NULL;
		m_join.info.numItems = 0;

		m_join.players.generic.flags |= QMF_HIDDEN;
		m_join.players.itemnames = NULL;
		m_join.players.numItems = 0;

		m_join.menu.statusbar = "Press Space to refresh; Hold ALT to refresh all";
	}
}

void UI_AddToServerList( const serverStatus_t *status ) {
	serverSlot_t *slot;
	int		i;
	char *s;
	const char *info;
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
		if( m_join.list.numItems == MAX_LOCAL_SERVERS ) {
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

	slot->valid = qtrue;

	s = UI_FormatColumns( 3,
		Info_ValueForKey( status->infostring, "hostname" ),
		Info_ValueForKey( status->infostring, "mapname" ),
		va( "%i/%s", status->numPlayers,
		Info_ValueForKey( status->infostring, "maxclients" ) ) );

	if( m_join.names[i] ) {
		com.Free( m_join.names[i] );
	}
	m_join.names[i] = s;

	for( i = 0; i < slot->numRules; i++ ) {
		com.Free( slot->rules[i] );
        slot->rules[i] = NULL;
	}
	for( i = 0; i < slot->numPlayers; i++ ) {
		com.Free( slot->players[i] );
        slot->players[i] = NULL;
	}

	slot->numRules = 0;
	info = status->infostring;
	do {
		Info_NextPair( &info, key, value );

		if( !key[0] ) {
			break;
		}

		s = UI_FormatColumns( 2, key, value );
		slot->rules[slot->numRules++] = s;
	} while( info && slot->numRules < MAX_STATUS_RULES );
    slot->rules[slot->numRules] = NULL;

	for( i = 0, player = status->players ; i < status->numPlayers; i++, player++ ) {
		s = UI_FormatColumns( 3,
			va( "%i", player->score ),
			va( "%i", player->ping ),
			player->name );
		slot->players[i] = s;
	}
	slot->numPlayers = status->numPlayers;
    slot->players[slot->numPlayers] = NULL;

	UpdateSelection();
    
}

static void PingSelected( void ) {
	serverSlot_t *slot;
	char address[MAX_QPATH];
    int i;
    char *s;
    
	slot = &m_join.servers[m_join.list.curvalue];
    slot->valid = qfalse;
    
	s = UI_FormatColumns( 3,
            slot->address, "???", "?/?" );
    
	if( m_join.names[m_join.list.curvalue] ) {
		com.Free( m_join.names[m_join.list.curvalue] );
	}
	m_join.names[m_join.list.curvalue] = s;

	for( i = 0; i < slot->numRules; i++ ) {
		com.Free( slot->rules[i] );
        slot->rules[i] = NULL;
	}
	for( i = 0; i < slot->numPlayers; i++ ) {
		com.Free( slot->players[i] );
        slot->players[i] = NULL;
	}

	slot->numRules = 0;
    slot->numPlayers = 0;
    
    UpdateSelection();
    m_join.menu.statusbar = "Pinging servers, please wait...";
    client.UpdateScreen();
    
	strcpy( address, slot->realAddress );
	client.SendStatusRequest( address, sizeof( address ) );

	UpdateSelection();
}

static void AddUnlistedServers( void ) {
	char *s;
	char address[MAX_QPATH];
	serverSlot_t *slot;
	int i, j;
    
    m_join.active = qtrue;

    // ping broadcast
    strcpy( address, "broadcast" );
    client.SendStatusRequest( address, sizeof( address ) );

	for( i = 0; i < MAX_LOCAL_SERVERS; i++ ) {
		cvar.VariableStringBuffer( va( "adr%i", i ), address, sizeof( address ) );
		if( !address[0] ) {
			continue;
		}

		// ignore if already listed
		for( j = 0, slot = m_join.servers; j < m_join.list.numItems; j++, slot++ ) {
			if( !Q_stricmp( address, slot->address ) ) {
				break;
			}
		}

		if( j != m_join.list.numItems ) {
			continue;
		}

		if( m_join.list.numItems == MAX_LOCAL_SERVERS ) {
			break;
		}

        // save original address
        strcpy( slot->address, address );
        strcpy( slot->realAddress, address );

		s = UI_FormatColumns( 3, slot->address, "???", "?/?" );
		m_join.names[m_join.list.numItems++] = s;
        m_join.names[m_join.list.numItems] = NULL;

        // ping and resolve real ip
        client.SendStatusRequest( slot->realAddress, sizeof( slot->realAddress ) );

        client.UpdateScreen();
	}
}

static void FreeListedServers( void ) {
	int i, j;
	serverSlot_t *slot;

	for( i = 0, slot = m_join.servers; i < m_join.list.numItems; i++, slot++ ) {
		for( j = 0; j < slot->numRules; j++ ) {
			com.Free( slot->rules[j] );
		}
		for( j = 0; j < slot->numPlayers; j++ ) {
			com.Free( slot->players[j] );
		}
		memset( slot, 0, sizeof( *slot ) );
	}


	for( i=0 ; i<m_join.list.numItems ; i++ ) {
		if( m_join.names[i] ) {
			com.Free( m_join.names[i] );
			m_join.names[i] = NULL;
		}
	}

	m_join.list.numItems = 0;
	m_join.info.itemnames = NULL;
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

	default:
		break;
	}

	return QMS_NOTHANDLED;
}

void JoinServer_MenuInit( void ) {
	int w1, w2;

	memset( &m_join, 0, sizeof( m_join ) );

	w1 = ( uis.width - 30 ) * 0.75f;
	w2 = ( uis.width - 30 ) * 0.25f;

//
// server list
//
	m_join.list.generic.type	= MTYPE_LIST;
	m_join.list.generic.id		= ID_LIST;
	m_join.list.generic.flags	= QMF_LEFT_JUSTIFY|QMF_HASFOCUS;
	m_join.list.generic.x		= 10;
	m_join.list.generic.y		= 32;
	m_join.list.generic.width	= 0;
	m_join.list.generic.height	= uis.height / 2 - 5 - 32;
	m_join.list.generic.name	= NULL;
	m_join.list.itemnames		= ( const char ** )m_join.names;
	m_join.list.drawNames		= qtrue;
	m_join.list.numcolumns		= 3;

	m_join.list.columns[0].width = w1 - 144;
	m_join.list.columns[0].uiFlags = UI_LEFT;
	m_join.list.columns[0].name = "Hostname";

	m_join.list.columns[1].width = 80;
	m_join.list.columns[1].uiFlags = UI_CENTER;
	m_join.list.columns[1].name = "Map";

	m_join.list.columns[2].width = 64;
	m_join.list.columns[2].uiFlags = UI_CENTER;
	m_join.list.columns[2].name = "Players";

//
// server info
//
	m_join.info.generic.type	= MTYPE_LIST;
	m_join.info.generic.id		= 0;
	m_join.info.generic.flags	= QMF_LEFT_JUSTIFY|QMF_HIDDEN;
	m_join.info.generic.x		= 10;
	m_join.info.generic.y		= uis.height / 2 + 5;
	m_join.info.generic.width	= 0;
	m_join.info.generic.height	= uis.height / 2 - 5 - 32;
	m_join.info.generic.name	= NULL;
	m_join.info.itemnames		= NULL;
	m_join.info.drawNames		= qtrue;
	m_join.info.numcolumns		= 2;

	m_join.info.columns[0].width = w1 / 2;
	m_join.info.columns[0].uiFlags = UI_LEFT;
	m_join.info.columns[0].name = "Key";

	m_join.info.columns[1].width = w1 - w1 / 2;
	m_join.info.columns[1].uiFlags = UI_LEFT;
	m_join.info.columns[1].name = "Value";

//
// player list
//
	m_join.players.generic.type		= MTYPE_LIST;
	m_join.players.generic.id		= 0;
	m_join.players.generic.flags	= QMF_LEFT_JUSTIFY|QMF_HIDDEN|QMF_DISABLED;
	m_join.players.generic.x		= w1 + 20;
	m_join.players.generic.y		= 32;
	m_join.players.generic.width	= 0;
	m_join.players.generic.height	= uis.height - 64;
	m_join.players.generic.name		= NULL;
	m_join.players.mlFlags			= MLF_HIDE_SCROLLBAR;
	m_join.players.itemnames		= NULL;
	m_join.players.drawNames		= qtrue;
	m_join.players.numcolumns		= 3;

	m_join.players.columns[0].width = 32;
	m_join.players.columns[0].uiFlags = UI_CENTER;
	m_join.players.columns[0].name = "Frg";

	m_join.players.columns[1].width = 32;
	m_join.players.columns[1].uiFlags = UI_CENTER;
	m_join.players.columns[1].name = "RTT";

	m_join.players.columns[2].width = w2 - 64;
	m_join.players.columns[2].uiFlags = UI_LEFT;
	m_join.players.columns[2].name = "Name";

	m_join.menu.callback = JoinServer_MenuCallback;

	UI_SetupDefaultBanner( &m_join.banner, "Server Browser" );

	Menu_AddItem( &m_join.menu, &m_join.list );
	Menu_AddItem( &m_join.menu, &m_join.info );
	Menu_AddItem( &m_join.menu, &m_join.players );
	Menu_AddItem( &m_join.menu, &m_join.banner );

}


void M_Menu_Multiplayer_f( void ) {
	JoinServer_MenuInit();
	UI_PushMenu( &m_join.menu );
    
    PingServers();
}

