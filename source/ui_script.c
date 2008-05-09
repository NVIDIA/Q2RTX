/*
Copyright (C) 2008 Andrey Nazarov

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

static menuSound_t Activate( menuCommon_t *self ) {
    menuAction_t *action = ( menuAction_t * )self;
    Cbuf_AddText( action->cmd );
    return QMS_NOTHANDLED;
}

static void Draw( menuFrameWork_t *self ) {
    static const color_t color = { 0, 0, 255, 32 };
    int y1, y2;

    y1 = ( uis.height - MENU_SPACING * self->nitems ) / 2 - MENU_SPACING;
    y2 = ( uis.height + MENU_SPACING * self->nitems ) / 2 + MENU_SPACING;

    ref.DrawFillEx( 0, y1, uis.width, y2 - y1, color );

    Menu_Draw( self );
}

static void Parse_Values( menuFrameWork_t *menu ) {
    menuSpinControl_t *s;
    int numItems = Cmd_Argc() - 3;
    int i;

    if( numItems < 1 ) {
        Com_Printf( "Usage: %s <name> <cvar> <desc1> [...]\n", Cmd_Argv( 0 ) );
        return;
    }

    s = UI_Mallocz( sizeof( *s ) );
    s->generic.type = MTYPE_SPINCONTROL;
    s->generic.name = UI_CopyString( Cmd_Argv( 1 ) );
    s->cvar = Cvar_Get( Cmd_Argv( 2 ), NULL, CVAR_USER_CREATED );
    s->itemnames = UI_Mallocz( sizeof( char * ) * ( numItems + 1 ) );
    for( i = 0; i < numItems; i++ ) {
        s->itemnames[i] = UI_CopyString( Cmd_Argv( 3 + i ) );
    }
    s->numItems = numItems;

    Menu_AddItem( menu, s );
}

static void Parse_Pairs( menuFrameWork_t *menu ) {
    menuSpinControl_t *s;
    int numItems = Cmd_Argc() - 3;
    int i;

    if( numItems < 2 || ( numItems & 1 ) ) {
        Com_Printf( "Usage: %s <name> <cvar> <desc1> <value1> [...]\n", Cmd_Argv( 0 ) );
        return;
    }

    s = UI_Mallocz( sizeof( *s ) );
    s->generic.type = MTYPE_PAIRS;
    s->generic.name = UI_CopyString( Cmd_Argv( 1 ) );
    s->cvar = Cvar_Get( Cmd_Argv( 2 ), NULL, CVAR_USER_CREATED );
    numItems >>= 1;
    s->itemnames = UI_Mallocz( sizeof( char * ) * ( numItems + 1 ) );
    for( i = 0; i < numItems; i++ ) {
        s->itemnames[i] = UI_CopyString( Cmd_Argv( 3 + i*2 ) );
    }
    s->itemvalues = UI_Mallocz( sizeof( char * ) * ( numItems + 1 ) );
    for( i = 0; i < numItems; i++ ) {
        s->itemvalues[i] = UI_CopyString( Cmd_Argv( 4 + i*2 ) );
    }
    s->numItems = numItems;

    Menu_AddItem( menu, s );
}

static void Parse_Range( menuFrameWork_t *menu ) {
    menuSlider_t *s;

    if( Cmd_Argc() < 7 ) {
        Com_Printf( "Usage: %s <name> <cvar> <add> <mul> <min> <max>\n", Cmd_Argv( 0 ) );
        return;
    }

    s = UI_Mallocz( sizeof( *s ) );
    s->generic.type = MTYPE_SLIDER;
    s->generic.name = UI_CopyString( Cmd_Argv( 1 ) );
    s->cvar = Cvar_Get( Cmd_Argv( 2 ), NULL, CVAR_USER_CREATED );
    s->add = atof( Cmd_Argv( 3 ) );
    s->mul = atof( Cmd_Argv( 4 ) );
    s->minvalue = atoi( Cmd_Argv( 5 ) );
    s->maxvalue = atoi( Cmd_Argv( 6 ) );

    Menu_AddItem( menu, s );
}

static void Parse_Action( menuFrameWork_t *menu ) {
    menuAction_t *a;

    if( Cmd_Argc() < 3 ) {
        Com_Printf( "Usage: %s <name> <command>\n", Cmd_Argv( 0 ) );
        return;
    }

    a = UI_Mallocz( sizeof( *a ) );
    a->generic.type = MTYPE_ACTION;
    a->generic.name = UI_CopyString( Cmd_Argv( 1 ) );
    a->generic.activate = Activate;
    a->generic.uiFlags = UI_CENTER;
    a->cmd = UI_CopyString( Cmd_ArgsFrom( 2 ) );

    Menu_AddItem( menu, a );
}

static void Parse_Bind( menuFrameWork_t *menu ) {
    menuKeybind_t *k;

    if( Cmd_Argc() < 3 ) {
        Com_Printf( "Usage: %s <name> <command>\n", Cmd_Argv( 0 ) );
        return;
    }

    k = UI_Mallocz( sizeof( *k ) );
    k->generic.type = MTYPE_KEYBIND;
    k->generic.name = UI_CopyString( Cmd_Argv( 1 ) );
    k->generic.uiFlags = UI_CENTER;
    k->cmd = UI_CopyString( Cmd_ArgsFrom( 2 ) );

    Menu_AddItem( menu, k );
}

static void Parse_Toggle( menuFrameWork_t *menu ) {
    static const char *yes_no_names[] = { "no", "yes", NULL };
    menuSpinControl_t *s;
    qboolean negate = qfalse;
    menuType_t type = MTYPE_TOGGLE;
    int bit = 0;
    char *b;

    if( Cmd_Argc() < 3 ) {
        Com_Printf( "Usage: %s <name> <cvar> [~][bit]\n", Cmd_Argv( 0 ) );
        return;
    }

    b = Cmd_Argv( 3 );
    if( *b == '~' ) {
        negate = qtrue;
        b++;
    }
    if( *b ) {
        bit = atoi( b );
        if( bit < 0 || bit >= 32 ) {
            Com_Printf( "Invalid bit number: %d\n", bit );
            return;
        }
        type = MTYPE_BITFIELD;
    }

    s = UI_Mallocz( sizeof( *s ) );
    s->generic.type = type;
    s->generic.name = UI_CopyString( Cmd_Argv( 1 ) );
    s->cvar = Cvar_Get( Cmd_Argv( 2 ), NULL, CVAR_USER_CREATED );
    s->itemnames = ( char ** )yes_no_names;
    s->numItems = 2;
    s->negate = negate;
    s->mask = 1 << bit;

    Menu_AddItem( menu, s );
}

static qboolean Parse_File( const char *path, int depth ) {
    char *data, *p, *cmd;
    int argc;
    menuFrameWork_t *menu = NULL;

    FS_LoadFile( path, ( void ** )&data );
    if( !data ) {
        Com_Printf( "Couldn't load %s\n", path );
        return qfalse;
    }

    COM_Compress( data );

    while( *data ) {
        p = strchr( data, '\n' );
        if( p ) {
            *p = 0;
        }

        Cmd_TokenizeString( data, qtrue );

        argc = Cmd_Argc();
        if( argc ) {
            cmd = Cmd_Argv( 0 );
            if( menu ) {
                if( !strcmp( cmd, "end" ) ) {
                    if( menu->nitems ) {
                        if( !menu->title ) {
                            menu->transparent = qtrue;
                            menu->draw = Draw;
                        }
                        List_Append( &ui_menus, &menu->entry );
                    } else {
                        menu->free( menu );
                    }
                    menu = NULL;
                } else if( !strcmp( cmd, "title" ) ) {
                    if( menu->title ) {
                        Z_Free( menu->title );
                    }
                    menu->title = UI_CopyString( Cmd_Argv( 1 ) );
                } else if( !strcmp( cmd, "values" ) ) {
                    Parse_Values( menu );
                } else if( !strcmp( cmd, "pairs" ) ) {
                    Parse_Pairs( menu );
                } else if( !strcmp( cmd, "range" ) ) {
                    Parse_Range( menu );
                } else if( !strcmp( cmd, "action" ) ) {
                    Parse_Action( menu );
                } else if( !strcmp( cmd, "bind" ) ) {
                    Parse_Bind( menu );
                } else if( !strcmp( cmd, "toggle" ) ) {
                    Parse_Toggle( menu );
                } else {
                    Com_WPrintf( "Unknown keyword '%s'\n", cmd );
                    menu->free( menu );
                    menu = NULL;
                    break;
                }
            } else {
                if( !strcmp( cmd, "begin" ) ) {
                    char *s = Cmd_Argv( 1 );
                    if( !*s ) {
                        Com_WPrintf( "Expected menu name after '%s'\n", cmd );
                        break;
                    }
                    menu = UI_FindMenu( s );
                    if( menu ) {
                        if( menu->free ) {
                            List_Remove( &menu->entry );
                            menu->free( menu );
                        } else {
                            Com_WPrintf( "Attempted to override built-in menu '%s'\n", s );
                            break;
                        }
                    }
                    menu = UI_Mallocz( sizeof( *menu ) );
                    menu->name = UI_CopyString( s );
                    menu->push = Menu_Push;
                    menu->pop = Menu_Pop;
                    menu->free = Menu_Free;
                } else if( !strcmp( cmd, "include" ) ) {
                    char *s = Cmd_Argv( 1 );
                    if( !*s ) {
                        Com_WPrintf( "Expected file name after '%s'\n", cmd );
                        break;
                    }
                    if( depth == 16 ) {
                        Com_WPrintf( "Includes too deeply nested\n" );
                    } else {
                        Parse_File( s, depth + 1 );
                    }
                } else {
                    Com_WPrintf( "Unknown keyword '%s'\n", cmd );
                    break;
                }
            }
        }

        if( !p ) {
            break;
        }

        data = p + 1;
    }

    FS_FreeFile( data );

    return qtrue;
}

void UI_LoadStript( void ) {
    Parse_File( "q2pro.menu", 0 );
}

