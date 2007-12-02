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

GAME MENU

=============================================================================
*/

#define ID_LOADGAME	101
#define ID_START	102

typedef struct m_game_s {
	menuFrameWork_t	menu;
	menuSpinControl_t	skill;
	menuAction_t load;
	menuAction_t start;
	menuStatic_t	banner;
} m_game_t;

static m_game_t		m_game;

static const char *difficulty_names[] = {
	"easy",
	"medium",
	"hard",
	"hard+",
	NULL
};

static void StartGame( void ) {
	cvar.SetInteger( "skill", m_game.skill.curvalue );
	cvar.SetInteger( "deathmatch", 0 );
	cvar.SetInteger( "coop", 0 );
	cvar.SetInteger( "gamerules", 0 );		//PGM

	cmd.ExecuteText( EXEC_APPEND, "map base1\n" );

	// disable updates and start the cinematic going
	UI_ForceMenuOff();

}

static int GameMenu_Callback( int id, int msg, int param ) {
	switch( msg ) {
	case QM_ACTIVATE:
		switch( id ) {
		case ID_LOADGAME:
			M_Menu_LoadGame_f ();
			break;
		case ID_START:
			StartGame();
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

static void GameMenu_Init( void ) {
	int y;

	memset( &m_game, 0, sizeof( m_game ) );

	m_game.menu.callback = GameMenu_Callback;

	y = 120;
	m_game.skill.generic.type	= MTYPE_SPINCONTROL;
	m_game.skill.generic.name	= "skill";
	m_game.skill.generic.x		= uis.width / 2;
	m_game.skill.generic.y		= y;
	m_game.skill.itemnames		= difficulty_names;
	y += 8;

	m_game.load.generic.type	= MTYPE_ACTION;
	m_game.load.generic.id		= ID_LOADGAME;
	m_game.load.generic.name	= "load game";
	m_game.load.generic.x		= uis.width / 2 + LCOLUMN_OFFSET;
	m_game.load.generic.y		= y;
	m_game.load.generic.uiFlags	= UI_RIGHT;
	y += 8;

	m_game.start.generic.type	= MTYPE_ACTION;
	m_game.start.generic.flags	= QMF_HASFOCUS;
	m_game.start.generic.id		= ID_START;
	m_game.start.generic.name	= "start game";
	m_game.start.generic.x		= uis.width / 2 + LCOLUMN_OFFSET;
	m_game.start.generic.y		= y;
	m_game.start.generic.uiFlags	= UI_RIGHT;
	y += 8;

	UI_SetupDefaultBanner( &m_game.banner, "Single player" );

	Menu_AddItem( &m_game.menu, (void *)&m_game.skill );
	Menu_AddItem( &m_game.menu, (void *)&m_game.load );
	Menu_AddItem( &m_game.menu, (void *)&m_game.start );
	Menu_AddItem( &m_game.menu, (void *)&m_game.banner );
}



void M_Menu_Game_f( void ) {
	GameMenu_Init();
	UI_PushMenu( &m_game.menu );
}
