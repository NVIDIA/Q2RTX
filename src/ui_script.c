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
#include "files.h"

static menuSound_t Activate( menuCommon_t *self ) {
    menuAction_t *action = ( menuAction_t * )self;
    Cbuf_AddText( &cmd_buffer, action->cmd );
    return QMS_NOTHANDLED;
}

static void Parse_Spin( menuFrameWork_t *menu, menuType_t type ) {
    menuSpinControl_t *s;
    int numItems = Cmd_Argc() - 3;
    int i;

    if( numItems < 1 ) {
        Com_Printf( "Usage: %s <name> <cvar> <desc1> [...]\n", Cmd_Argv( 0 ) );
        return;
    }

    s = UI_Mallocz( sizeof( *s ) );
    s->generic.type = type;
    s->generic.name = UI_CopyString( Cmd_Argv( 1 ) );
    s->cvar = Cvar_Ref( Cmd_Argv( 2 ) );
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
    s->cvar = Cvar_Ref( Cmd_Argv( 2 ) );
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

    if( Cmd_Argc() < 5 ) {
        Com_Printf( "Usage: %s <name> <cvar> <min> <max> [step]\n", Cmd_Argv( 0 ) );
        return;
    }

    s = UI_Mallocz( sizeof( *s ) );
    s->generic.type = MTYPE_SLIDER;
    s->generic.name = UI_CopyString( Cmd_Argv( 1 ) );
    s->cvar = Cvar_Ref( Cmd_Argv( 2 ) );
    s->minvalue = atof( Cmd_Argv( 3 ) );
    s->maxvalue = atof( Cmd_Argv( 4 ) );
    if( Cmd_Argc() > 5 ) {
        s->step = atof( Cmd_Argv( 5 ) );
    } else {
        s->step = ( s->maxvalue - s->minvalue ) / SLIDER_RANGE;
    }

    Menu_AddItem( menu, s );
}

static void Parse_Action( menuFrameWork_t *menu ) {
    static const cmd_option_t options[] = {
        { "a", "align" },
        { "s:", "status" },
        { NULL }
    };
    menuAction_t *a;
    int uiFlags = UI_CENTER;
    char *status = NULL;
    int c;

    while( ( c = Cmd_ParseOptions( options ) ) != -1 ) {
        switch( c ) {
        case 'a':
            uiFlags = UI_LEFT|UI_ALTCOLOR;
            break;
        case 's':
            status = cmd_optarg;
            break;
        default:
            return;
        }
    }

    if( Cmd_Argc() - cmd_optind < 2 ) {
        Com_Printf( "Usage: %s <name> <command>\n", Cmd_Argv( 0 ) );
        return;
    }

    a = UI_Mallocz( sizeof( *a ) );
    a->generic.type = MTYPE_ACTION;
    a->generic.name = UI_CopyString( Cmd_Argv( cmd_optind ) );
    a->generic.activate = Activate;
    a->generic.uiFlags = uiFlags;
    a->generic.status = UI_CopyString( status );
    a->cmd = UI_CopyString( Cmd_ArgsFrom( cmd_optind + 1 ) );

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
    s->cvar = Cvar_Ref( Cmd_Argv( 2 ) );
    s->itemnames = ( char ** )yes_no_names;
    s->numItems = 2;
    s->negate = negate;
    s->mask = 1 << bit;

    Menu_AddItem( menu, s );
}

static void Parse_Field( menuFrameWork_t *menu ) {
    static const cmd_option_t o_field[] = {
        { "c", "center" },
        { "i", "integer" },
        { "s:", "status" },
        { "w:", "width" },
        { NULL }
    };
    menuField_t *f;
    qboolean center = qfalse;
    int flags = 0;
    char *status = NULL;
    int width = 16;
    int c;

    while( ( c = Cmd_ParseOptions( o_field ) ) != -1 ) {
        switch( c ) {
        case 'c':
            center = qtrue;
            break;
        case 'i':
            flags |= QMF_NUMBERSONLY;
            break;
        case 's':
            status = cmd_optarg;
            break;
        case 'w':
            width = atoi( cmd_optarg );
            if( width < 1 || width > 32 ) {
                Com_Printf( "Invalid width\n" );
                return;
            }
            break;
        default:
            return;
        }
    }

    f = UI_Mallocz( sizeof( *f ) );
    f->generic.type = MTYPE_FIELD;
    f->generic.name = center ? NULL : UI_CopyString( Cmd_Argv( cmd_optind ) );
    f->generic.status = UI_CopyString( status );
    f->generic.flags = flags;
    f->cvar = Cvar_Ref( Cmd_Argv( center ? cmd_optind : cmd_optind + 1 ) );
    f->width = width;

    Menu_AddItem( menu, f );
}

static void Parse_Blank( menuFrameWork_t *menu ) {
    menuSeparator_t *s;

    s = UI_Mallocz( sizeof( *s ) );
    s->generic.type = MTYPE_SEPARATOR;

    Menu_AddItem( menu, s );
}

static void Parse_Background( menuFrameWork_t *menu ) {
    char *s = Cmd_Argv( 1 );

    if( SCR_ParseColor( s, menu->color ) ) {
        menu->image = 0;
        if( menu->color[3] != 255 ) {
            menu->transparent = qtrue;
        }
    } else {
        menu->image = R_RegisterPic( s );
        menu->transparent = R_GetPicSize( NULL, NULL, menu->image );
    }
}

static void Parse_Color( void ) {
    char *s, *c;

    if( Cmd_Argc() < 3 ) {
        Com_Printf( "Usage: %s <state> <color>\n", Cmd_Argv( 0 ) );
        return;
    }

    s = Cmd_Argv( 1 );
    c = Cmd_Argv( 2 );

    if( !strcmp( s, "normal" ) ) {
        SCR_ParseColor( c, uis.color.normal );
    } else if( !strcmp( s, "active" ) ) {
        SCR_ParseColor( c, uis.color.active );
    } else if( !strcmp( s, "selection" ) ) {
        SCR_ParseColor( c, uis.color.selection );
    } else if( !strcmp( s, "disabled" ) ) {
        SCR_ParseColor( c, uis.color.disabled );
    } else {
        Com_Printf( "Unknown state '%s'\n", s );
    }
}

static qboolean Parse_File( const char *path, int depth ) {
    char *raw, *data, *p, *cmd;
    int argc;
    menuFrameWork_t *menu = NULL;
    qerror_t ret;

    ret = FS_LoadFile( path, ( void ** )&raw );
    if( !raw ) {
        if( ret != Q_ERR_NOENT || depth ) {
            Com_WPrintf( "Couldn't %s %s: %s\n", depth ? "include" : "load",
                path, Q_ErrorString( ret ) );
        }
        return qfalse;
    }

    data = raw;
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
                } else if( !strcmp( cmd, "background" ) ) {
                    Parse_Background( menu );
                } else if( !strcmp( cmd, "values" ) ) {
                    Parse_Spin( menu, MTYPE_SPINCONTROL );
                } else if( !strcmp( cmd, "strings" ) ) {
                    Parse_Spin( menu, MTYPE_STRINGS );
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
                } else if( !strcmp( cmd, "field" ) ) {
                    Parse_Field( menu );
                } else if( !strcmp( cmd, "blank" ) ) {
                    Parse_Blank( menu );
                } else {
                    Com_WPrintf( "Ignoring unknown keyword '%s'\n", cmd );
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
                            menu->free( menu );
                        }
                        List_Remove( &menu->entry );
                    }
                    menu = UI_Mallocz( sizeof( *menu ) );
                    menu->name = UI_CopyString( s );
                    menu->push = Menu_Push;
                    menu->pop = Menu_Pop;
                    menu->free = Menu_Free;
                    menu->image = uis.backgroundHandle;
                    FastColorCopy( uis.color.background, menu->color );
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
                } else if( !strcmp( cmd, "color" ) ) {
                    Parse_Color();
                } else if( !strcmp( cmd, "background" ) ) {
                    char *s = Cmd_Argv( 1 );

                    if( SCR_ParseColor( s, uis.color.background ) ) {
                        uis.backgroundHandle = 0;
                    } else {
                        uis.backgroundHandle = R_RegisterPic( s );
                    }
                } else if( !strcmp( cmd, "font" ) ) {
                    uis.fontHandle = R_RegisterFont( Cmd_Argv( 1 ) );
                } else if( !strcmp( cmd, "cursor" ) ) {
                    uis.cursorHandle = R_RegisterPic( Cmd_Argv( 1 ) );
                    R_GetPicSize( &uis.cursorWidth,
                        &uis.cursorHeight, uis.cursorHandle );
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

    FS_FreeFile( raw );

    return qtrue;
}

void UI_LoadStript( void ) {
    Parse_File( "q2pro.menu", 0 );
}

