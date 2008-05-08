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

static void ParseSpin( menuFrameWork_t *menu ) {
    menuSpinControl_t *s;
    int numItems = Cmd_Argc() - 3;
    int i;

    if( numItems < 1 ) {
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

static void ParseSlider( menuFrameWork_t *menu ) {
    menuSlider_t *s = UI_Mallocz( sizeof( *s ) );

    s->generic.type = MTYPE_SLIDER;
    s->generic.name = UI_CopyString( Cmd_Argv( 1 ) );
    s->cvar = Cvar_Get( Cmd_Argv( 2 ), NULL, CVAR_USER_CREATED );
    s->add = atof( Cmd_Argv( 3 ) );
    s->mul = atof( Cmd_Argv( 4 ) );
    s->minvalue = atoi( Cmd_Argv( 5 ) );
    s->maxvalue = atoi( Cmd_Argv( 6 ) );

    Menu_AddItem( menu, s );
}

static void ParseAction( menuFrameWork_t *menu ) {
    menuAction_t *a = UI_Mallocz( sizeof( *a ) );

    a->generic.type = MTYPE_ACTION;
    a->generic.name = UI_CopyString( Cmd_Argv( 1 ) );
    a->generic.activate = Activate;
    a->generic.uiFlags = UI_CENTER;
    a->cmd = UI_CopyString( Cmd_ArgsFrom( 2 ) );

    Menu_AddItem( menu, a );
}

static void ParseBind( menuFrameWork_t *menu ) {
    menuKeybind_t *k = UI_Mallocz( sizeof( *k ) );

    k->generic.type = MTYPE_KEYBIND;
    k->generic.name = UI_CopyString( Cmd_Argv( 1 ) );
    k->generic.uiFlags = UI_CENTER;
    k->cmd = UI_CopyString( Cmd_ArgsFrom( 2 ) );

    Menu_AddItem( menu, k );
}

static qboolean ParseFile( const char *path ) {
    char *data, *p, *cmd;
    int linenum = 1;
    int argc;
    menuFrameWork_t *menu = NULL;

    FS_LoadFile( path, ( void ** )&data );
    if( !data ) {
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
                    if( !menu->title ) {
                        menu->transparent = qtrue;
                        menu->draw = Draw;
                    }
                    List_Append( &ui_menus, &menu->entry );
                    menu = NULL;
                } else if( !strcmp( cmd, "title" ) ) {
                    if( menu->title ) {
                        Z_Free( menu->title );
                    }
                    menu->title = UI_CopyString( Cmd_Argv( 1 ) );
                } else if( !strcmp( cmd, "spin" ) ) {
                    if( argc < 4 ) {
                        Com_WPrintf( "Insufficient arguments to '%s' on line %d in %s\n",
                            cmd, linenum, path );
                    }
                    ParseSpin( menu );
                } else if( !strcmp( cmd, "slider" ) ) {
                    ParseSlider( menu );
                } else if( !strcmp( cmd, "action" ) ) {
                    ParseAction( menu );
                } else if( !strcmp( cmd, "confirm" ) ) {
                } else if( !strcmp( cmd, "bind" ) ) {
                    ParseBind( menu );
                } else {
                    Com_WPrintf( "Unexpected '%s' on line %d in %s\n",
                        cmd, linenum, path );
                    break;
                }
            } else {
                if( !strcmp( cmd, "begin" ) ) {
                    if( argc < 2 ) {
                        Com_WPrintf( "Insufficient arguments to '%s' on line %d in %s\n",
                            cmd, linenum, path );
                        break;
                    }
                    menu = UI_Mallocz( sizeof( *menu ) );
                    menu->name = UI_CopyString( Cmd_Argv( 1 ) );
                    menu->push = Menu_Push;
                    menu->pop = Menu_Pop;
                    menu->free = Menu_Free;
                } else {
                    Com_WPrintf( "Unexpected '%s' on line %d in %s\n",
                        cmd, linenum, path );
                    break;
                }
            }
        }

        if( !p ) {
            break;
        }

        linenum++; // FIXME: meaniningless
        data = p + 1;
    }

    FS_FreeFile( data );

    return qtrue;
}

void UI_LoadStript( void ) {
    ParseFile( "q2pro.menu" );
}

