/*
Copyright (C) 2003-2008 Andrey Nazarov

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

#define ENTRY_UP        1
#define ENTRY_DN        2
#define ENTRY_DEMO      3

typedef struct {
    int type;
    int size;
    time_t mtime;
    char name[1];
} demoEntry_t;

typedef struct m_demos_s {
    menuFrameWork_t menu;
    menuList_t      list;
    int             numDirs;
    uint8_t         hash[16];
} m_demos_t;

static m_demos_t    m_demos;

static void BuildName( fsFileInfo_t *info, char **cache ) {
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
        Q_concat( buffer, sizeof( buffer ), uis.m_demos_browse, "/", info->name, NULL );
        client.GetDemoInfo( buffer, &demo );
    }

    if( info->size >= 1000000 ) {
        sprintf( buffer, "%2.1fM", ( float )info->size / 1000000 );
    } else if( info->size >= 1000 ) {
        sprintf( buffer, "%3"PRIz"K", info->size / 1000 );
    } else {
        sprintf( buffer, "%3"PRIz"b", info->size );
    }

    e = UI_FormatColumns( DEMO_EXTRASIZE,
        info->name, buffer, demo.map, demo.pov, NULL );
    e->type = ENTRY_DEMO;
    e->size = info->size;
    e->mtime = info->mtime;

    m_demos.list.items[m_demos.list.numItems++] = e;
}

static void BuildDir( const char *name, int type ) {
    demoEntry_t *e = UI_FormatColumns( DEMO_EXTRASIZE, name, "-", NULL );

    e->type = type;
    e->size = 0;
    e->mtime = 0;

    m_demos.list.items[m_demos.list.numItems++] = e;
}

static char *LoadCache( void **list ) {
    char buffer[MAX_OSPATH], *cache;
    int i;
    size_t len;
    uint8_t hash[16];

    Q_concat( buffer, sizeof( buffer ), uis.m_demos_browse, "/" COM_DEMOCACHE_NAME, NULL );
    len = fs.LoadFile( buffer, ( void ** )&cache );
    if( !cache ) {
        return NULL;
    }
    if( len < 33 ) {
        goto fail;
    }

    for( i = 0; i < 16; i++ ) {
        int c1 = Q_charhex( cache[i*2+0] );
        int c2 = Q_charhex( cache[i*2+1] );
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

static void WriteCache( void ) {
    char buffer[MAX_OSPATH];
    fileHandle_t f;
    int i;
    char *map, *pov;
    demoEntry_t *e;

    if( m_demos.list.numItems == m_demos.numDirs ) {
        return;
    }

    Q_concat( buffer, sizeof( buffer ), uis.m_demos_browse, "/" COM_DEMOCACHE_NAME, NULL );
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
    }
    fs.FCloseFile( f );
}

static void CalcHash( void **list ) {
    struct mdfour md;
    fsFileInfo_t *info;
    size_t len;

    mdfour_begin( &md );
    while( *list ) {
        info = *list++;
        len = sizeof( *info ) + strlen( info->name ) - 1;
        mdfour_update( &md, ( uint8_t * )info, len );
    }
    mdfour_result( &md, m_demos.hash );
}

static menuSound_t Change( menuCommon_t *self ) {
    demoEntry_t *e = m_demos.list.items[m_demos.list.curvalue];

    switch( e->type ) {
    case ENTRY_DEMO:
        m_demos.menu.status = "Press Enter to play demo";
        break;
    default:
        m_demos.menu.status = "Press Enter to change directory";
        break;
    }
    return QMS_SILENT;
}

static void BuildList( void ) {
    int numDirs, numDemos;
    void **dirlist, **demolist;
    char *cache, *p;
    int i;

    client.StopAllSounds();
    m_demos.menu.status = "Building list...";
    client.UpdateScreen();
    
    // alloc entries
    dirlist = fs.ListFiles( uis.m_demos_browse, NULL, FS_PATH_GAME |
        FS_SEARCHDIRS_ONLY, &numDirs );
    demolist = fs.ListFiles( uis.m_demos_browse, DEMO_EXTENSIONS, FS_PATH_GAME |
        FS_SEARCH_EXTRAINFO, &numDemos );

    m_demos.list.items = UI_Malloc( sizeof( demoEntry_t * ) * ( numDirs + numDemos + 1 ) );
    m_demos.list.numItems = 0;
    m_demos.list.curvalue = 0;
    m_demos.list.prestep = 0;

    if( uis.m_demos_browse[0] ) {
        BuildDir( "..", ENTRY_UP );
    }

    // add directories
    if( dirlist ) {
        for( i = 0; i < numDirs; i++ ) {
            BuildDir( dirlist[i], ENTRY_DN );
        }
        fs.FreeList( dirlist );
    }    

    m_demos.numDirs = m_demos.list.numItems;

    // add demos
    if( demolist ) {
        CalcHash( demolist );
        if( ( cache = LoadCache( demolist ) ) != NULL ) {
            p = cache + 32 + 1;
            for( i = 0; i < numDemos; i++ ) {
                BuildName( demolist[i], &p );
            }
            fs.FreeFile( cache );
        } else {
            for( i = 0; i < numDemos; i++ ) {
                BuildName( demolist[i], NULL );
                if( ( i & 7 ) == 0 ) {
                    client.UpdateScreen();
                }
            }
        }
        WriteCache();
        fs.FreeList( demolist );
    }

    if( m_demos.list.numItems ) {
        Change( &m_demos.list.generic );
    }
        
    client.UpdateScreen();
}

static void FreeList( void ) {
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

static void LeaveDirectory( void ) {
    char *s;
    int i;

    s = strrchr( uis.m_demos_browse, '/' );
    if( !s ) {
        return;
    }
    *s = 0;

    // rebuild list
    FreeList();
    BuildList();
    MenuList_Init( &m_demos.list );

    // move cursor to the previous directory
    for( i = 0; i < m_demos.numDirs; i++ ) {
        demoEntry_t *e = m_demos.list.items[i];
        if( !strcmp( e->name, s + 1 ) ) {
            MenuList_SetValue( &m_demos.list, i );
            break;
        }
    }

    if( s == uis.m_demos_browse ) {
        uis.m_demos_browse[0] = '/';
        uis.m_demos_browse[1] = 0;
    }
}

static menuSound_t Activate( menuCommon_t *self ) {
    size_t len, baselen;
    demoEntry_t *e = m_demos.list.items[m_demos.list.curvalue];

    switch( e->type ) {
    case ENTRY_UP:
        LeaveDirectory();
        return QMS_OUT;

    case ENTRY_DN:
        baselen = strlen( uis.m_demos_browse );
        len = strlen( e->name );
        if( baselen + 1 + len >= sizeof( uis.m_demos_browse ) ) {
            return QMS_BEEP;
        }
        if( !baselen || uis.m_demos_browse[ baselen - 1 ] != '/' ) {
            uis.m_demos_browse[ baselen++ ] = '/';
        }
        memcpy( uis.m_demos_browse + baselen, e->name, len + 1 );
        
        // rebuild list
        FreeList();
        BuildList();
        MenuList_Init( &m_demos.list );
        return QMS_IN;

    case ENTRY_DEMO:
        cmd.ExecuteText( EXEC_APPEND, va( "demo \"%s/%s\"\n",
            uis.m_demos_browse[1] ? uis.m_demos_browse : "", e->name ) );
        return QMS_SILENT;
    }

    return QMS_NOTHANDLED;
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

static menuSound_t Sort( menuList_t *self, int column ) {
    switch( column ) {
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

static void Size( menuFrameWork_t *self ) {
    int w = uis.width * 96 / 640;

    if( w > 8*15 ) {
        w = 8*15;
    }

    m_demos.list.generic.x      = 0;
    m_demos.list.generic.y      = 8;
    m_demos.list.generic.width  = 0;
    m_demos.list.generic.height = uis.height - 17;
    
    m_demos.list.columns[0].width = uis.width - 100 - w;
    m_demos.list.columns[1].width = 40;
    m_demos.list.columns[2].width = 60;
    m_demos.list.columns[3].width = w;
}

static menuSound_t Keydown( menuFrameWork_t *self, int key ) {
    if( key == K_BACKSPACE ) {
        LeaveDirectory();
        return QMS_OUT;
    }
    return QMS_NOTHANDLED;
}

static void Pop( menuFrameWork_t *self ) {
    // save previous position
    uis.m_demos_selection = m_demos.list.curvalue;
    FreeList();
}

static qboolean Push( menuFrameWork_t *self ) {
    BuildList();
    // move cursor to previous position
    MenuList_SetValue( &m_demos.list, uis.m_demos_selection );
    return qtrue;
}

void M_Menu_Demos( void ) {
    m_demos.menu.name = "demos";
    m_demos.menu.title = "Demo Browser";

    m_demos.menu.push       = Push;
    m_demos.menu.pop        = Pop;
    m_demos.menu.size       = Size;
    m_demos.menu.keydown    = Keydown;

    m_demos.list.generic.type   = MTYPE_LIST;
    m_demos.list.generic.flags  = QMF_HASFOCUS;
    m_demos.list.generic.activate = Activate;
    m_demos.list.generic.change = Change;
    m_demos.list.numcolumns     = 4;
    m_demos.list.sortdir        = 1;
    m_demos.list.extrasize      = DEMO_EXTRASIZE;
    m_demos.list.sort           = Sort;

    m_demos.list.columns[0].name    = uis.m_demos_browse;
    m_demos.list.columns[0].uiFlags = UI_LEFT;
    m_demos.list.columns[1].name    = "Size";
    m_demos.list.columns[1].uiFlags = UI_CENTER;
    m_demos.list.columns[2].name    = "Map";
    m_demos.list.columns[2].uiFlags = UI_CENTER;
    m_demos.list.columns[3].name    = "POV";
    m_demos.list.columns[3].uiFlags = UI_CENTER;

    Menu_AddItem( &m_demos.menu, &m_demos.list );

    List_Append( &ui_menus, &m_demos.menu.entry );
}


