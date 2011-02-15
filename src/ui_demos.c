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
#include "files.h"
#include "mdfour.h"

/*
=======================================================================

DEMOS MENU

=======================================================================
*/

#define DEMO_EXTENSIONS ".dm2;.dm2.gz;.mvd2;.mvd2.gz"

#define DEMO_EXTRASIZE  q_offsetof( demoEntry_t, name )

#define DEMO_MVD_POV "\x90\xcd\xd6\xc4\x91"

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
    char    browse[MAX_OSPATH];
    int     selection;
} m_demos_t;

static m_demos_t    m_demos;

static void BuildName( file_info_t *info, char **cache ) {
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
            Q_strlcpy( demo.map, s, sizeof( demo.map ) );
            s = p + 1;
            p = strchr( s, '\\' );
            if( p ) {
                *p = 0;
                Q_strlcpy( demo.pov, s, sizeof( demo.pov ) );
                s = p + 1;
            }
        }
        *cache = s;
    } else {
        Q_concat( buffer, sizeof( buffer ), m_demos.browse, "/", info->name, NULL );
        CL_GetDemoInfo( buffer, &demo );
        if( demo.mvd ) {
            strcpy( demo.pov, DEMO_MVD_POV );
        }
    }

    Com_FormatSize( buffer, sizeof( buffer ), info->size );

    e = UI_FormatColumns( DEMO_EXTRASIZE,
        info->name, buffer, demo.map, demo.pov, NULL );
    e->type = ENTRY_DEMO;
    e->size = info->size;
    e->mtime = info->mtime;

    m_demos.list.items[m_demos.list.numItems++] = e;
}

static void BuildDir( const char *name, int type ) {
    demoEntry_t *e = UI_FormatColumns( DEMO_EXTRASIZE, name, NULL );

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

    Q_concat( buffer, sizeof( buffer ), m_demos.browse, "/" COM_DEMOCACHE_NAME, NULL );
    len = FS_LoadFile( buffer, ( void ** )&cache );
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
    FS_FreeFile( cache );
    return NULL;
}

static void WriteCache( void ) {
    char buffer[MAX_OSPATH];
    qhandle_t f;
    int i;
    char *map, *pov;
    demoEntry_t *e;
    size_t len;

    if( m_demos.list.numItems == m_demos.numDirs ) {
        return;
    }

    len = Q_concat( buffer, sizeof( buffer ), m_demos.browse, "/" COM_DEMOCACHE_NAME, NULL );
    if( len >= sizeof( buffer ) ) {
        return;
    }
    FS_FOpenFile( buffer, &f, FS_MODE_WRITE );
    if( !f ) {
        return;
    }

    for( i = 0; i < 16; i++ ) {
        FS_FPrintf( f, "%02x", m_demos.hash[i] );
    }
    FS_FPrintf( f, "\\" );

    for( i = m_demos.numDirs; i < m_demos.list.numItems; i++ ) {
        e = m_demos.list.items[i];
        map = UI_GetColumn( e->name, 2 );
        pov = UI_GetColumn( e->name, 3 );
        FS_FPrintf( f, "%s\\%s\\", map, pov );
    }
    FS_FCloseFile( f );
}

static void CalcHash( void **list ) {
    struct mdfour md;
    file_info_t *info;
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

    S_StopAllSounds();
    m_demos.menu.status = "Building list...";
    SCR_UpdateScreen();
    
    // alloc entries
    dirlist = FS_ListFiles( m_demos.browse, NULL, FS_PATH_GAME |
        FS_SEARCH_DIRSONLY, &numDirs );
    demolist = FS_ListFiles( m_demos.browse, DEMO_EXTENSIONS, FS_PATH_GAME |
        FS_SEARCH_EXTRAINFO, &numDemos );

    m_demos.list.items = UI_Malloc( sizeof( demoEntry_t * ) * ( numDirs + numDemos + 1 ) );
    m_demos.list.numItems = 0;
    m_demos.list.curvalue = 0;
    m_demos.list.prestep = 0;

    if( m_demos.browse[0] ) {
        BuildDir( "..", ENTRY_UP );
    }

    // add directories
    if( dirlist ) {
        for( i = 0; i < numDirs; i++ ) {
            BuildDir( dirlist[i], ENTRY_DN );
        }
        FS_FreeList( dirlist );
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
            FS_FreeFile( cache );
        } else {
            for( i = 0; i < numDemos; i++ ) {
                BuildName( demolist[i], NULL );
                if( ( i & 7 ) == 0 ) {
                    SCR_UpdateScreen();
                }
            }
        }
        WriteCache();
        FS_FreeList( demolist );
    }

    if( m_demos.list.numItems ) {
        Change( &m_demos.list.generic );
    }
        
    SCR_UpdateScreen();
}

static void FreeList( void ) {
    int i;

    if( m_demos.list.items ) {
        for( i = 0; i < m_demos.list.numItems; i++ ) {
            Z_Free( m_demos.list.items[i] );
        }
        Z_Free( m_demos.list.items );
        m_demos.list.items = NULL;
        m_demos.list.numItems = 0;
    }
}

static void LeaveDirectory( void ) {
    char *s;
    int i;

    s = strrchr( m_demos.browse, '/' );
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

    if( s == m_demos.browse ) {
        m_demos.browse[0] = '/';
        m_demos.browse[1] = 0;
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
        baselen = strlen( m_demos.browse );
        len = strlen( e->name );
        if( baselen + 1 + len >= sizeof( m_demos.browse ) ) {
            return QMS_BEEP;
        }
        if( !baselen || m_demos.browse[ baselen - 1 ] != '/' ) {
            m_demos.browse[ baselen++ ] = '/';
        }
        memcpy( m_demos.browse + baselen, e->name, len + 1 );
        
        // rebuild list
        FreeList();
        BuildList();
        MenuList_Init( &m_demos.list );
        return QMS_IN;

    case ENTRY_DEMO:
        Cbuf_AddText( &cmd_buffer, va( "demo \"%s/%s\"\n", m_demos.browse[1] ?
            m_demos.browse : "", e->name ) );
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
    int w = uis.width * 8 / 640;

    if( w > 15 ) {
        w = 15;
    }

    m_demos.list.generic.x      = 0;
    m_demos.list.generic.y      = CHAR_HEIGHT;
    m_demos.list.generic.width  = 0;
    m_demos.list.generic.height = uis.height - CHAR_HEIGHT*2 - 1;

    m_demos.list.columns[0].width = uis.width - ( 13 + w ) * CHAR_WIDTH;
    m_demos.list.columns[1].width = 5*CHAR_WIDTH;
    m_demos.list.columns[2].width = 8*CHAR_WIDTH;
    m_demos.list.columns[3].width = w*CHAR_WIDTH;
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
    m_demos.selection = m_demos.list.curvalue;
    FreeList();
}

static void Expose( menuFrameWork_t *self ) {
    BuildList();
    // move cursor to previous position
    MenuList_SetValue( &m_demos.list, m_demos.selection );
}

static void Free( menuFrameWork_t *self ) {
    memset( &m_demos, 0, sizeof( m_demos ) );
}

void M_Menu_Demos( void ) {
    m_demos.menu.name = "demos";
    m_demos.menu.title = "Demo Browser";

    strcpy( m_demos.browse, "/demos" );

    m_demos.menu.expose     = Expose;
    m_demos.menu.pop        = Pop;
    m_demos.menu.size       = Size;
    m_demos.menu.keydown    = Keydown;
    m_demos.menu.free       = Free;
    m_demos.menu.image = uis.backgroundHandle;
    FastColorCopy( uis.color.background, m_demos.menu.color );

    m_demos.list.generic.type   = MTYPE_LIST;
    m_demos.list.generic.flags  = QMF_HASFOCUS;
    m_demos.list.generic.activate = Activate;
    m_demos.list.generic.change = Change;
    m_demos.list.numcolumns     = 4;
    m_demos.list.sortdir        = 1;
    m_demos.list.extrasize      = DEMO_EXTRASIZE;
    m_demos.list.sort           = Sort;

    m_demos.list.columns[0].name    = m_demos.browse;
    m_demos.list.columns[0].uiFlags = UI_LEFT;
    m_demos.list.columns[1].name    = "Size";
    m_demos.list.columns[1].uiFlags = UI_RIGHT;
    m_demos.list.columns[2].name    = "Map";
    m_demos.list.columns[2].uiFlags = UI_CENTER;
    m_demos.list.columns[3].name    = "POV";
    m_demos.list.columns[3].uiFlags = UI_CENTER;

    Menu_AddItem( &m_demos.menu, &m_demos.list );

    List_Append( &ui_menus, &m_demos.menu.entry );
}


