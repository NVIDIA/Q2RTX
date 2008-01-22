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

PLAYER CONFIG MENU

=============================================================================
*/

#define ID_MODEL 103
#define ID_SKIN	104

typedef struct m_playerConfig_s {
	menuFrameWork_t	menu;
	menuField_t		nameField;
	menuSpinControl_t		modelBox;
	menuSpinControl_t		skinBox;
	menuSpinControl_t		handBox;
	menuAction_t	downloadAction;
	menuAction_t	back;
	menuAction_t	apply;

	refdef_t	refdef;
	entity_t	entities[2];

	int		time;
	int		oldTime;

	char *pmnames[MAX_PLAYERMODELS];
} m_playerConfig_t;

static m_playerConfig_t	m_playerConfig;

static const char *handedness[] = {
	"right",
	"left",
	"center",
	0
};

static void ApplyChanges( void ) {
	char scratch[MAX_OSPATH];

	cvar.Set( "name", m_playerConfig.nameField.field.text );

	Q_concat( scratch, sizeof( scratch ),
		uis.pmi[m_playerConfig.modelBox.curvalue].directory, "/",
		uis.pmi[m_playerConfig.modelBox.curvalue].skindisplaynames[m_playerConfig.skinBox.curvalue], NULL );

	cvar.Set( "skin", scratch );

	cvar.SetInteger( "hand", m_playerConfig.handBox.curvalue );
}

static void ReloadMedia( void ) {
	char scratch[MAX_QPATH];
    char *model = uis.pmi[m_playerConfig.modelBox.curvalue].directory;
    char *skin = uis.pmi[m_playerConfig.modelBox.curvalue].skindisplaynames[m_playerConfig.skinBox.curvalue];

	Q_concat( scratch, sizeof( scratch ), "players/", model, "/tris.md2", NULL );
	m_playerConfig.entities[0].model = ref.RegisterModel( scratch );

	Q_concat( scratch, sizeof( scratch ), "players/", model, "/", skin, ".pcx", NULL );
	m_playerConfig.entities[0].skin = ref.RegisterSkin( scratch );

	Q_concat( scratch, sizeof( scratch ), "players/", model, "/w_railgun.md2", NULL );
	m_playerConfig.entities[1].model = ref.RegisterModel( scratch );
}

static void PlayerConfig_RunFrame( void ) {
	int frame;

	if( m_playerConfig.time < uis.realtime ) {
		m_playerConfig.oldTime = m_playerConfig.time;

		m_playerConfig.time += 120;
		if( m_playerConfig.time < uis.realtime ) {
			m_playerConfig.time = uis.realtime;
		}

		frame = ( m_playerConfig.time / 120 ) % 40;

		m_playerConfig.entities[0].oldframe = m_playerConfig.entities[0].frame;
		m_playerConfig.entities[1].oldframe = m_playerConfig.entities[1].frame;
		m_playerConfig.entities[0].frame = frame;
		m_playerConfig.entities[1].frame = frame;

	}
}

static void PlayerConfig_MenuDraw( menuFrameWork_t *self ) {
	float backlerp;

	m_playerConfig.refdef.time = uis.realtime * 0.001f;

	PlayerConfig_RunFrame();

	if( m_playerConfig.time == m_playerConfig.oldTime ) {
		backlerp = 0;
	} else {
		backlerp = 1 - ( float )( uis.realtime - m_playerConfig.oldTime ) /
			( float )( m_playerConfig.time - m_playerConfig.oldTime );
	}

	m_playerConfig.entities[0].backlerp = backlerp;
	m_playerConfig.entities[1].backlerp = backlerp;

	//m_playerConfig.entities[0].angles[1] = anglemod( uis.realtime / 20.0f );
	//m_playerConfig.entities[1].angles[1] = anglemod( uis.realtime / 20.0f );

	Menu_Draw( self );	

	ref.RenderFrame( &m_playerConfig.refdef );

	//Com_sprintf( scratch, sizeof( scratch ), "/players/%s/%s_i.pcx", 
	//	uis.pmi[m_playerConfig.modelBox.curvalue].directory,
	//	uis.pmi[m_playerConfig.modelBox.curvalue].skindisplaynames[m_playerConfig.skinBox.curvalue] );
	//ref.DrawStretchPic( m_playerConfig.menu.x - 40, refdef.y, scratch );
}

static void Resize( void ) {
	int x = uis.width / 2 - 130;
	int y = uis.height / 2 - 97;

	m_playerConfig.refdef.x = uis.width / uis.scale / 2;
	m_playerConfig.refdef.y = 60;
	m_playerConfig.refdef.width = uis.width / uis.scale / 2;
	m_playerConfig.refdef.height = uis.height / uis.scale - 122;

	m_playerConfig.refdef.fov_x = 40;
	m_playerConfig.refdef.fov_y = Com_CalcFov( m_playerConfig.refdef.fov_x,
		m_playerConfig.refdef.width, m_playerConfig.refdef.height );


	m_playerConfig.nameField.generic.x		= x;
	m_playerConfig.nameField.generic.y		= y;
	y += 32;

	m_playerConfig.modelBox.generic.x	= x;
	m_playerConfig.modelBox.generic.y	= y;
    y += 16;

	m_playerConfig.skinBox.generic.x	= x;
	m_playerConfig.skinBox.generic.y	= y;
    y += 16;

	m_playerConfig.handBox.generic.x	= x;
	m_playerConfig.handBox.generic.y	= y;
}

static int PlayerConfig_MenuCallback( int id, int msg, int param ) {
	switch( msg ) {
	case QM_CHANGE:
		switch( id ) {
		case ID_MODEL:
			m_playerConfig.skinBox.itemnames = ( const char ** )
				uis.pmi[m_playerConfig.modelBox.curvalue].skindisplaynames;
			m_playerConfig.skinBox.curvalue = 0;
			SpinControl_Init( &m_playerConfig.skinBox );
			// fall through
		case ID_SKIN:
			ReloadMedia();
			break;
		default:
			break;
		}
		return QMS_MOVE;
	case QM_DESTROY:
		ApplyChanges();
		break;
    case QM_SIZE:
        Resize();
        break;
	default:
		break;
	}

	return QMS_NOTHANDLED;

}

qboolean PlayerConfig_MenuInit( void ) {
	char currentdirectory[MAX_QPATH];
	char currentskin[MAX_QPATH];
	int i, j;
	int currentdirectoryindex = 0;
	int currentskinindex = 0;
	char *p;
	vec3_t origin = { 80.0f, 5.0f, 0.0f };
	vec3_t angles = { 0.0f, 260.0f, 0.0f };
	
	memset( &m_playerConfig, 0, sizeof( m_playerConfig ) );

	// find and register all player models
	if( !uis.numPlayerModels ) {
		PlayerModel_Load();
		if( !uis.numPlayerModels ) {
			return qfalse;
		}
	}

	cvar.VariableStringBuffer( "skin", currentdirectory, sizeof( currentdirectory ) );

	if( ( p = strchr( currentdirectory, '/' ) ) || ( p = strchr( currentdirectory, '\\' ) ) ) {
		*p++ = 0;
		Q_strncpyz( currentskin, p, sizeof( currentskin ) );
	} else {
		strcpy( currentdirectory, "male" );
		strcpy( currentskin, "grunt" );
	}

	for( i = 0 ; i < uis.numPlayerModels ; i++ ) {
		m_playerConfig.pmnames[i] = uis.pmi[i].directory;
		if( Q_stricmp( uis.pmi[i].directory, currentdirectory ) == 0 ) {
			currentdirectoryindex = i;

			for( j = 0 ; j < uis.pmi[i].nskins ; j++ ) {
				if( Q_stricmp( uis.pmi[i].skindisplaynames[j], currentskin ) == 0 ) {
					currentskinindex = j;
					break;
				}
			}
		}
	}

	m_playerConfig.entities[0].flags = RF_FULLBRIGHT;
	VectorCopy( angles, m_playerConfig.entities[0].angles );
	VectorCopy( origin, m_playerConfig.entities[0].origin );
	VectorCopy( origin, m_playerConfig.entities[0].oldorigin );

	m_playerConfig.entities[1].flags = RF_FULLBRIGHT;
	VectorCopy( angles, m_playerConfig.entities[1].angles );
	VectorCopy( origin, m_playerConfig.entities[1].origin );
	VectorCopy( origin, m_playerConfig.entities[1].oldorigin );

	m_playerConfig.refdef.num_entities = 2;

	m_playerConfig.refdef.entities = m_playerConfig.entities;
	m_playerConfig.refdef.rdflags = RDF_NOWORLDMODEL;

	// set up oldframe correctly
	m_playerConfig.time = uis.realtime - 120;
	m_playerConfig.oldTime = m_playerConfig.time;
	PlayerConfig_RunFrame();

	m_playerConfig.menu.draw = PlayerConfig_MenuDraw;
	m_playerConfig.menu.callback = PlayerConfig_MenuCallback;

	m_playerConfig.nameField.generic.type = MTYPE_FIELD;
	m_playerConfig.nameField.generic.flags = QMF_HASFOCUS;
	m_playerConfig.nameField.generic.name = "name";
	IF_InitText( &m_playerConfig.nameField.field, 16, 16,
		cvar.VariableString( "name" ) );

	m_playerConfig.modelBox.generic.type = MTYPE_SPINCONTROL;
	m_playerConfig.modelBox.generic.id = ID_MODEL;
	m_playerConfig.modelBox.generic.name = "model";
	m_playerConfig.modelBox.curvalue = currentdirectoryindex;
	m_playerConfig.modelBox.itemnames = ( const char ** )m_playerConfig.pmnames;

	m_playerConfig.skinBox.generic.type = MTYPE_SPINCONTROL;
	m_playerConfig.skinBox.generic.id = ID_SKIN;
	m_playerConfig.skinBox.generic.name = "skin";
	m_playerConfig.skinBox.curvalue = currentskinindex;
	m_playerConfig.skinBox.itemnames = ( const char ** )
		uis.pmi[currentdirectoryindex].skindisplaynames;

	m_playerConfig.handBox.generic.type = MTYPE_SPINCONTROL;
	m_playerConfig.handBox.generic.name = "handedness";
	m_playerConfig.handBox.curvalue = cvar.VariableInteger( "hand" );
	clamp( m_playerConfig.handBox.curvalue, 0, 2 );
	m_playerConfig.handBox.itemnames = handedness;

	m_playerConfig.menu.banner = "Player Setup";

	Menu_AddItem( &m_playerConfig.menu, &m_playerConfig.nameField );
	Menu_AddItem( &m_playerConfig.menu, &m_playerConfig.modelBox );
	if( m_playerConfig.skinBox.itemnames ) {
		Menu_AddItem( &m_playerConfig.menu, &m_playerConfig.skinBox );
	}
	Menu_AddItem( &m_playerConfig.menu, &m_playerConfig.handBox );
	//Menu_AddItem( &m_playerConfig.menu, &m_playerConfig.downloadAction );

	ReloadMedia();

	return qtrue;
}


void M_Menu_PlayerConfig_f( void ) {
	if( !PlayerConfig_MenuInit() ) {
		return;
	}
	UI_PushMenu( &m_playerConfig.menu );
}



