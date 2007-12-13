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
#include "mdfour.h"

/*
=======================================================================

DEMOS MENU

=======================================================================
*/

#define DEMO_EXTENSIONS ".dm2;.dm2.gz;.mvd2;.mvd2.gz"

#define DEMO_EXTRASIZE  q_offsetof( demoEntry_t, name )

#define FFILE_UP	    1
#define FFILE_FOLDER	2
#define FFILE_DEMO	    3

#define ID_LIST			105

typedef struct {
    int type;
    int size;
    time_t mtime;
    char name[1];
} demoEntry_t;

typedef struct m_demos_s {
	menuFrameWork_t	menu;
	menuList_t		list;
	int			    numDirs;
    uint8           hash[16];
} m_demos_t;

static m_demos_t	m_demos;

static void Demos_BuildName( const char *path, fsFileInfo_t *info, char **cache ) {
    char buffer[MAX_OSPATH];
    demoInfo_t demo;
    demoEntry_t *e;

    memset( &demo, 0, sizeof( demo ) );
    strcpy( demo.map, "???" );
    strcpy( demo.pov, "???" );

    if( cache ) {
        char *s = *cache;
        char *p = strchr( s, '\\' );
        if( p ) {
            *p = 0;
            Q_strncpyz( demo.map, s, sizeof( demo.map ) );
            s = p + 1;
            p = strchr( s, '\\' );
            if( p ) {
                *p = 0;
                Q_strncpyz( demo.pov, s, sizeof( demo.pov ) );
                s = p + 1;
            }
        }
        *cache = s;
    } else {
        Q_concat( buffer, sizeof( buffer ), path, "/", info->name, NULL );
        client.GetDemoInfo( buffer, &demo );
    }

    if( info->size >= 1000000 ) {
        sprintf( buffer, "%2.1fM", ( float )info->size / 1000000 );
    } else if( info->size >= 1000 ) {
        sprintf( buffer, "%3dK", info->size / 1000 );
    } else {
        sprintf( buffer, "%3db", info->size );
    }

    e = UI_FormatColumns( DEMO_EXTRASIZE,
        info->name, buffer, demo.map, demo.pov, NULL );
    e->type = FFILE_DEMO;
    e->size = info->size;
    e->mtime = info->mtime;

    m_demos.list.items[m_demos.list.numItems++] = e;
}

static void Demos_BuildDir( const char *name, int type ) {
    demoEntry_t *e;

    e = UI_FormatColumns( DEMO_EXTRASIZE, name, "-", NULL );
    e->type = type;
    e->size = 0;
    e->mtime = 0;

    m_demos.list.items[m_demos.list.numItems++] = e;
}

static inline int char2hex( int c ) {
    if( c >= 'A' && c <= 'F' ) {
        return 10 + ( c - 'A' );
    }
    if( c >= 'a' && c <= 'f' ) {
        return 10 + ( c - 'a' );
    }
    if( c >= '0' && c <= '9' ) {
        return c - '0';
    }
    return 0;
}

static char *Demos_LoadCache( const char *path, void **list ) {
    char buffer[MAX_OSPATH], *cache;
    int i, len;
    uint8 hash[16];

	if( *path == '/' ) {
		path++;
	}

	Q_concat( buffer, sizeof( buffer ), path, "/" COM_DEMOCACHE_NAME, NULL );
    len = fs.LoadFile( buffer, ( void ** )&cache );
    if( !cache ) {
        return NULL;
    }
    if( len < 33 ) {
        goto fail;
    }

	for( i = 0; i < 16; i++ ) {
        int c1 = char2hex( cache[i*2+0] );
        int c2 = char2hex( cache[i*2+1] );
        hash[i] = ( c1 << 4 ) | c2;
	}

    if( cache[32] != '\\' ) {
        goto fail;
    }

    if( memcmp( hash, m_demos.hash, 16 ) ) {
        goto fail;
    }

    Com_DPrintf( "%s: loading from cache\n", __func__ );
    return cache;

fail:
    fs.FreeFile( cache );
    return NULL;
}

static void Demos_WriteCache( const char *path ) {
    char buffer[MAX_OSPATH];
    fileHandle_t f;
    int i;
    char *map, *pov;
    demoEntry_t *e;

    if( m_demos.list.numItems == m_demos.numDirs ) {
        return;
    }

	if( *path == '/' ) {
		path++;
	}

	Q_concat( buffer, sizeof( buffer ), path, "/" COM_DEMOCACHE_NAME, NULL );
    fs.FOpenFile( buffer, &f, FS_MODE_WRITE );
    if( !f ) {
        return;
    }

    for( i = 0; i < 16; i++ ) {
        fs.FPrintf( f, "%02x", m_demos.hash[i] );
    }
    fs.FPrintf( f, "\\" );

	for( i = m_demos.numDirs; i < m_demos.list.numItems; i++ ) {
        e = m_demos.list.items[i];
        map = UI_GetColumn( e->name, 2 );
        pov = UI_GetColumn( e->name, 3 );
        fs.FPrintf( f, "%s\\%s\\", map, pov );
		
#if 0
		{
			char buffer[64];
			struct tm *tm;

			tm=localtime( &e->mtime );
			strftime( buffer, sizeof( buffer ), "%Y-%m-%d %H.%M", tm );
			Com_Printf("%s: %s\n", name,buffer);
		}
#endif
    }
    fs.FCloseFile( f );
}

static void Demos_CalcHash( void **list ) {
	struct mdfour md;
    fsFileInfo_t *info;
    int len;

	mdfour_begin( &md );
    while( *list ) {
        info = *list++;
        len = sizeof( *info ) + strlen( info->name ) - 1;
	    mdfour_update( &md, ( uint8 * )info, len );
    }
    mdfour_result( &md, m_demos.hash );
}

static void Demos_BuildList( const char *path ) {
	int numDirs, numDemos;
	void **dirlist, **demolist;
    char *cache, *p;
	int i;

	if( *path == '/' ) {
		path++;
	}
	
    // alloc entries
	dirlist = fs.ListFiles( path, NULL, FS_PATH_GAME |
        FS_SEARCHDIRS_ONLY, &numDirs );
	demolist = fs.ListFiles( path, DEMO_EXTENSIONS, FS_PATH_GAME |
        FS_SEARCH_EXTRAINFO, &numDemos );

    m_demos.list.items = UI_Malloc( sizeof( demoEntry_t * ) * ( numDirs + numDemos + 1 ) );
    m_demos.list.numItems = 0;
    m_demos.list.curvalue = 0;
    m_demos.list.prestep = 0;

	if( *path ) {
        Demos_BuildDir( "..", FFILE_UP );
        m_demos.list.curvalue = 1;
	}

	// add directories
	if( dirlist ) {
		for( i = 0; i < numDirs; i++ ) {
            Demos_BuildDir( dirlist[i], FFILE_FOLDER );
		}
		fs.FreeList( dirlist );
	}	

	m_demos.numDirs = m_demos.list.numItems;

    // add demos
	if( demolist ) {
        Demos_CalcHash( demolist );
        if( ( cache = Demos_LoadCache( path, demolist ) ) != NULL ) {
            p = cache + 32 + 1;
            for( i = 0; i < numDemos; i++ ) {
                Demos_BuildName( path, demolist[i], &p );
            }
            fs.FreeFile( cache );
        } else {
            for( i = 0; i < numDemos; i++ ) {
                Demos_BuildName( path, demolist[i], NULL );
            }
        }
        Demos_WriteCache( path );
        fs.FreeList( demolist );
    }
}

static void Demos_Free( void ) {
	int i;

    if( m_demos.list.items ) {
        for( i = 0; i < m_demos.list.numItems; i++ ) {
            com.Free( m_demos.list.items[i] );
        }
        com.Free( m_demos.list.items );
        m_demos.list.items = NULL;
        m_demos.list.numItems = 0;
    }
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
		for( i = 0; i < m_demos.numDirs; i++ ) {
            demoEntry_t *e = m_demos.list.items[i];
			if( !strcmp( e->name, buffer ) ) {
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
    demoEntry_t *e = m_demos.list.items[m_demos.list.curvalue];

	switch( e->type ) {
	case FFILE_UP:
		Demos_LeaveDirectory();
		return QMS_OUT;

	case FFILE_FOLDER:
		baseLength = strlen( uis.m_demos_browse );
		length = strlen( e->name );
		if( baseLength + length > sizeof( uis.m_demos_browse ) - 2 ) {
			return QMS_BEEP;
		}
		if( uis.m_demos_browse[ baseLength - 1 ] != '/' ) {
			uis.m_demos_browse[ baseLength++ ] = '/';
		}

		strcpy( uis.m_demos_browse + baseLength, e->name );
		
		// rebuild list
		Demos_Free();
		Demos_BuildList( uis.m_demos_browse );
		MenuList_Init( &m_demos.list );
		return QMS_IN;

	case FFILE_DEMO:
		Com_sprintf( buffer, sizeof( buffer ), "%s \"%s/%s\"\n",
            /*m_demos.demo.mvd ? "mvdplay" : */"demo",
			uis.m_demos_browse, e->name );
		cmd.ExecuteText( EXEC_APPEND, buffer );
		return QMS_SILENT;
	}

	return QMS_NOTHANDLED;
}

static void Demos_Size( void ) {
    int w = uis.width * 96 / 640;

    if( w > 8*15 ) {
        w = 8*15;
    }

	m_demos.list.generic.x		= 0;
	m_demos.list.generic.y		= 8;
	m_demos.list.generic.width	= 0;
	m_demos.list.generic.height	= uis.height - 17;
    
	m_demos.list.columns[0].width = uis.width - 100 - w;
	m_demos.list.columns[1].width = 40;
	m_demos.list.columns[2].width = 60;
	m_demos.list.columns[3].width = w;
}

static int sizecmp( const void *p1, const void *p2 ) {
    demoEntry_t *e1 = *( demoEntry_t ** )p1;
    demoEntry_t *e2 = *( demoEntry_t ** )p2;

    return ( e1->size - e2->size ) * m_demos.list.sortdir;
}

static int namecmp( const void *p1, const void *p2 ) {
    demoEntry_t *e1 = *( demoEntry_t ** )p1;
    demoEntry_t *e2 = *( demoEntry_t ** )p2;
    char *s1 = UI_GetColumn( e1->name, m_demos.list.sortcol );
    char *s2 = UI_GetColumn( e2->name, m_demos.list.sortcol );

    return Q_stricmp( s1, s2 ) * m_demos.list.sortdir;
}

static int Demos_MenuCallback( int id, int msg, int param ) {
	switch( msg ) {
	case QM_ACTIVATE:
		if( id == ID_LIST ) {
			return Demos_Action();
		}
		break;

	case QM_CHANGE:
		if( id == ID_LIST ) {
            demoEntry_t *e = m_demos.list.items[m_demos.list.curvalue];
            if( e->type == FFILE_DEMO ) {
                m_demos.menu.statusbar = "Press Enter to play demo";
            } else {
                m_demos.menu.statusbar = "Press Enter to change directory";
            }
            return QMS_SILENT;
        }
		break;

	case QM_KEY:
		if( id == ID_LIST && param == K_BACKSPACE ) {
			Demos_LeaveDirectory();
			return QMS_OUT;
		}
		break;

	case QM_SORT:
		if( id == ID_LIST ) {
            switch( param ) {
            case 0:
            case 2:
            case 3:
                MenuList_Sort( &m_demos.list, m_demos.numDirs, namecmp );
                break;
            case 1:
                MenuList_Sort( &m_demos.list, m_demos.numDirs, sizecmp );
                break;
            }
            return QMS_SILENT;
        }
		break;

	case QM_DESTROY:
		uis.m_demos_selection = m_demos.list.curvalue; // save previous position
		Demos_Free();
		break;

	case QM_SIZE:
        Demos_Size();
        break;

	default:
		break;
	}

	return QMS_NOTHANDLED;
}

static void Demos_MenuInit( void ) {
	memset( &m_demos, 0, sizeof( m_demos ) );

	Demos_BuildList( uis.m_demos_browse );

	m_demos.menu.banner = "Demo Browser";
	m_demos.menu.callback = Demos_MenuCallback;

	m_demos.list.generic.type	= MTYPE_LIST;
	m_demos.list.generic.id		= ID_LIST;
	m_demos.list.generic.flags  = QMF_HASFOCUS;
	m_demos.list.numcolumns     = 4;
	m_demos.list.sortdir        = 1;
	m_demos.list.extrasize      = DEMO_EXTRASIZE;

	m_demos.list.columns[0].name    = uis.m_demos_browse;
	m_demos.list.columns[0].uiFlags = UI_LEFT;
	m_demos.list.columns[1].name    = "Size";
	m_demos.list.columns[1].uiFlags = UI_CENTER;
	m_demos.list.columns[2].name    = "Map";
	m_demos.list.columns[2].uiFlags = UI_CENTER;
	m_demos.list.columns[3].name    = "POV";
	m_demos.list.columns[3].uiFlags = UI_CENTER;

	Menu_AddItem( &m_demos.menu, &m_demos.list );
}

void M_Menu_Demos_f( void ) {
	Demos_MenuInit();
	UI_PushMenu( &m_demos.menu );

	// move cursor to previous position
	MenuList_SetValue( &m_demos.list, uis.m_demos_selection );
}

