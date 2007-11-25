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

DEMOS MENU

=======================================================================
*/

#define MAX_MENU_DEMOS	1024

#define DEMO_EXTENSIONS ".dm2;.dm2.gz;.mvd2;.mvd2.gz"

#define FFILE_UP	1
#define FFILE_FOLDER	2
#define FFILE_DEMO	3

#define ID_LIST			105

typedef struct m_demos_s {
	menuFrameWork_t		menu;
	menuList_t		list;
	menuList_t		playerList;
	menuStatic_t	banner;

	int				count;
	char		*names[MAX_MENU_DEMOS+1];
	int			types[MAX_MENU_DEMOS];
	char		*playerNames[MAX_DEMOINFO_CLIENTS+1];

	// Demo file info
	demoInfo_t	demo;
} m_demos_t;

static m_demos_t	m_demos;

static void Demos_FreeInfo( void ) {
	memset( &m_demos.demo, 0, sizeof( m_demos.demo ) );

	m_demos.playerList.generic.flags |= QMF_HIDDEN;
}

static void Demos_LoadInfo( int index ) {
	char buffer[MAX_OSPATH];
	int i, numNames, localPlayerNum;

	if( m_demos.types[index] != FFILE_DEMO ) {
		m_demos.playerList.generic.flags |= QMF_HIDDEN;
		m_demos.menu.statusbar = NULL;
		return;
	}

	Q_concat( buffer, sizeof( buffer ),
        uis.m_demos_browse + 1, "/", m_demos.names[index], NULL );

	client.GetDemoInfo( buffer, &m_demos.demo );

	if( !m_demos.demo.mapname[0] ) {
	    m_demos.menu.statusbar = "Couldn't read demo info";
		m_demos.playerList.generic.flags |= QMF_HIDDEN;
        return;
	}

	localPlayerNum = 0;

	numNames = 0;
	for( i = 0; i < MAX_DEMOINFO_CLIENTS; i++ ) {
		if( !m_demos.demo.clients[i][0] ) {
			continue;
		}

		if( i == m_demos.demo.clientNum ) {
			localPlayerNum = numNames;
		}

		// ( m_demos.demo.mvd || i == m_demos.demo.clientNum ) ? colorYellow : NULL

		m_demos.playerNames[numNames++] = m_demos.demo.clients[i];
	}

	if( numNames ) {
		m_demos.playerList.generic.flags &= ~QMF_HIDDEN;
	} else {
		m_demos.playerList.generic.flags |= QMF_HIDDEN;
	}

	m_demos.playerNames[numNames] = NULL;

    m_demos.menu.statusbar = "Press Enter to play";

	MenuList_Init( &m_demos.playerList );
	MenuList_SetValue( &m_demos.playerList, localPlayerNum );
}

static char *Demos_BuildName( const char *path, const char *name,
                              fsFileInfo_t *info )
{
    char buffer[MAX_OSPATH];
    demoInfo_t demo;
    char *s;

	Q_concat( buffer, sizeof( buffer ), path, "/", name, NULL );

	client.GetDemoInfo( buffer, &demo );
	if( !demo.mapname[0] ) {
        return NULL;
    }

    if( info->fileSize >= 1000000 ) {
        sprintf( buffer, "%2.1fM", ( float )info->fileSize / 1000000 );
    } else if( info->fileSize >= 1000 ) {
        sprintf( buffer, "%3dK", info->fileSize / 1000 );
    } else {
        sprintf( buffer, "%3db", info->fileSize );
    }

//    Com_sprintf( buffer, sizeof( buffer ), "%4d", info->fileSize >> 10 );

    if( demo.clientNum >= 0 && demo.clientNum < MAX_DEMOINFO_CLIENTS ) {
        s = UI_FormatColumns( 4, name, buffer, demo.mapname,
            demo.clients[demo.clientNum] );
    } else {
        s = UI_FormatColumns( 3, name, buffer, demo.mapname );
    }

    return s;
}

static void Demos_BuildList( const char *path ) {
	fsFileInfo_t *info;
	int numFiles;
	char **list, *s;
	int pos;
	int i;

	if( *path == '/' ) {
		path++;
	}
	
	if( *path ) {
		m_demos.names[m_demos.count] = UI_FormatColumns( 1, ".." );
		m_demos.types[m_demos.count] = FFILE_UP;
		m_demos.count++;
	}

	// list directories first
	list = fs.ListFiles( path, NULL, FS_PATH_GAME |
        FS_SEARCHDIRS_ONLY, &numFiles );
	if( list ) {
        if( m_demos.count + numFiles > MAX_MENU_DEMOS ) {
            numFiles = MAX_MENU_DEMOS - m_demos.count;
        }
		for( i = 0; i < numFiles; i++ ) {
			m_demos.names[m_demos.count] = UI_FormatColumns( 1, list[i] );
			m_demos.types[m_demos.count] = FFILE_FOLDER;
			m_demos.count++;
		}

		fs.FreeFileList( list );
	}	

	pos = m_demos.count;

    // list demos
	list = fs.ListFiles( path, DEMO_EXTENSIONS, FS_PATH_GAME |
        FS_SEARCH_EXTRAINFO, &numFiles );
	if( list ) {
        if( m_demos.count + numFiles > MAX_MENU_DEMOS ) {
            numFiles = MAX_MENU_DEMOS - m_demos.count;
        }
        for( i = 0; i < numFiles; i++ ) {
            info = ( fsFileInfo_t * )( list[i] + strlen( list[i] ) + 1 );
            s = Demos_BuildName( path, list[i], info );
            if( s ) {
                m_demos.names[m_demos.count] = s;
                m_demos.types[m_demos.count] = FFILE_DEMO;
                m_demos.count++;
            }
        }

        // sort them
        qsort( m_demos.names + pos, m_demos.count - pos,
            sizeof( m_demos.names[0] ), SortStrcmp );

        fs.FreeFileList( list );
    }

    // terminate the list
	m_demos.names[m_demos.count] = NULL;
}

static void Demos_Free( void ) {
	int i;

	Demos_FreeInfo();

	for( i = 0; i < m_demos.count; i++ ) {
		com.Free( m_demos.names[i] );
		m_demos.names[i] = NULL;
		m_demos.types[i] = 0;
	}

	m_demos.count = 0;
	m_demos.list.curvalue = 0;
}

static void Demos_LeaveDirectory( void ) {
	char buffer[MAX_OSPATH];
	char *s;
	int i;

	s = strrchr( uis.m_demos_browse, '/' );
	if( s ) {
		*s = 0;
		strcpy( buffer, s + 1 );
	} else {
		buffer[0] = 0;
	}

	// rebuild list
	Demos_Free();
	Demos_BuildList( uis.m_demos_browse );
	MenuList_Init( &m_demos.list );

	if( s == uis.m_demos_browse ) {
		uis.m_demos_browse[0] = '/';
		uis.m_demos_browse[1] = 0;
	}

	// move cursor to the previous directory
	if( buffer[0] ) {
		for( i = 0; i < m_demos.count; i++ ) {
			if( !strcmp( m_demos.names[i], buffer ) ) {
				MenuList_SetValue( &m_demos.list, i );
				return;
			}
		}
	}

	MenuList_SetValue( &m_demos.list, 0 );
}

static int Demos_Action( void ) {
	char buffer[MAX_OSPATH];
	int length, baseLength;

	switch( m_demos.types[m_demos.list.curvalue] ) {
	case FFILE_UP:
		Demos_LeaveDirectory();
		return QMS_OUT;

	case FFILE_FOLDER:
		baseLength = strlen( uis.m_demos_browse );
		length = strlen( m_demos.names[m_demos.list.curvalue] );
		if( baseLength + length > sizeof( uis.m_demos_browse ) - 2 ) {
			return QMS_BEEP;
		}
		if( uis.m_demos_browse[ baseLength - 1 ] != '/' ) {
			uis.m_demos_browse[ baseLength++ ] = '/';
		}

		strcpy( uis.m_demos_browse + baseLength,
            m_demos.names[m_demos.list.curvalue] );
		
		// rebuild list
		Demos_Free();
		Demos_BuildList( uis.m_demos_browse );
		MenuList_Init( &m_demos.list );
		return QMS_IN;

	case FFILE_DEMO:
	    if( !m_demos.demo.mapname[0] ) {
            return QMS_BEEP;
        }
		Com_sprintf( buffer, sizeof( buffer ), "%s \"%s/%s\"\n",
            m_demos.demo.mvd ? "mvdplay" : "demo",
			uis.m_demos_browse, m_demos.names[m_demos.list.curvalue] );
		cmd.ExecuteText( EXEC_APPEND, buffer );
		//UI_ForceMenuOff();
		return QMS_SILENT;
	}

	return QMS_NOTHANDLED;
}

static int Demos_MenuCallback( int id, int msg, int param ) {
	switch( msg ) {
	case QM_ACTIVATE:
		switch( id ) {
		case ID_LIST:
			return Demos_Action();
		}
		return QMS_IN;

	case QM_CHANGE:
		switch( id ) {
		case ID_LIST:
			Demos_LoadInfo( m_demos.list.curvalue );
			break;
		default:
			break;
		}
		return QMS_MOVE;

	case QM_KEY:
		switch( id ) {
		case ID_LIST:
			switch( param ) {
			case K_BACKSPACE:
				Demos_LeaveDirectory();
				return QMS_OUT;
			default:
				break;
			}
		default:
			break;
		}
		break;

	case QM_DESTROY:
		uis.m_demos_selection = m_demos.list.curvalue; // save previous position
		Demos_Free();
		break;

	default:
		break;
	}

	return QMS_NOTHANDLED;
}

static void Demos_MenuInit( void ) {
	int w1, w2;

	memset( &m_demos, 0, sizeof( m_demos ) );

	m_demos.menu.callback = Demos_MenuCallback;

	Demos_BuildList( uis.m_demos_browse );

	w1 = ( uis.glconfig.vidWidth - 30 ) * 0.8f;
	w2 = ( uis.glconfig.vidWidth - 30 ) * 0.2f;

	m_demos.list.generic.type	= MTYPE_LIST;
	m_demos.list.generic.id		= ID_LIST;
	m_demos.list.generic.flags  = QMF_HASFOCUS;
	m_demos.list.generic.x		= 10;
	m_demos.list.generic.y		= 32;
	m_demos.list.generic.width	= 0;
	m_demos.list.generic.height	= uis.glconfig.vidHeight - 64;
	m_demos.list.generic.name	= NULL;
	m_demos.list.itemnames		= ( const char ** )m_demos.names;
	m_demos.list.drawNames		= qtrue;
	m_demos.list.numcolumns     = 4;

	m_demos.list.columns[0].width = w1 - 180;
	m_demos.list.columns[0].name = uis.m_demos_browse;
	m_demos.list.columns[0].uiFlags = UI_LEFT;

	m_demos.list.columns[1].width = 40;
	m_demos.list.columns[1].name = "Size";
	m_demos.list.columns[1].uiFlags = UI_CENTER;

	m_demos.list.columns[2].width = 60;
	m_demos.list.columns[2].name = "Map";
	m_demos.list.columns[2].uiFlags = UI_CENTER;

	m_demos.list.columns[3].width = 80;
	m_demos.list.columns[3].name = "POV";
	m_demos.list.columns[3].uiFlags = UI_CENTER;

	m_demos.playerList.generic.type		= MTYPE_LIST;
	m_demos.playerList.generic.flags	= QMF_HIDDEN|QMF_DISABLED;
	m_demos.playerList.generic.x		= w1 + 20;
	m_demos.playerList.generic.y		= 32;
	m_demos.playerList.generic.height	= uis.glconfig.vidHeight - 64;
	m_demos.playerList.itemnames		= ( const char ** )m_demos.playerNames;
	m_demos.playerList.mlFlags			= MLF_HIDE_SCROLLBAR_EMPTY;
	m_demos.playerList.numcolumns		= 1;
	m_demos.playerList.drawNames		= qtrue;

	m_demos.playerList.columns[0].width = w2;
	m_demos.playerList.columns[0].name = "Players";
	m_demos.playerList.columns[0].uiFlags = UI_CENTER;

	UI_SetupDefaultBanner( &m_demos.banner, "Demos" );

	Menu_AddItem( &m_demos.menu, (void *)&m_demos.list );
	Menu_AddItem( &m_demos.menu, (void *)&m_demos.playerList );
	Menu_AddItem( &m_demos.menu, (void *)&m_demos.banner );

	// move cursor to previous position
	MenuList_SetValue( &m_demos.list, uis.m_demos_selection );
}

void M_Menu_Demos_f( void ) {
	Demos_MenuInit();
	UI_PushMenu( &m_demos.menu );
}
