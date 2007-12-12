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

VIDEO MENU

=======================================================================
*/

#define ID_REF		103
#define ID_PICMIP	104
#define ID_LIGHTMAPS	106
#define ID_GAMMATYPE	107
#define ID_TEXTUREFILTER	108
#define ID_GAMMA		109
#define ID_FULLSCREEN 110
#define ID_OVERRIDE	112
#define ID_SATURATION   113

#ifndef REF_HARD_LINKED
static const char *refNames[] = {
	"[software]",
	"[OpenGL  ]",
	NULL
};

static const char *refValues[] = {
	"soft",
	"gl",
	NULL
};
#endif

static const char *yesnoNames[] = {
	"no",
	"yes",
	NULL
};

#if USE_DYNAMIC
static const char *dlightNames[] = {
	"disabled",
	"additive",
    "modulative",
	NULL
};
#endif

/*static const char *onoffNames[] = {
	"off",
	"on",
	NULL
};*/

static const char *gammaNames[] = {
	"[software]",
	"[hardware]",
	NULL
};

static const char *filterNames[] = {
	"[none     ]",
	"[nearest  ]",
	"[linear   ]",
	"[bilinear ]",
	"[trilinear]",
	NULL
};

static const char *filterValues[] = {
	"GL_NEAREST",
	"GL_NEAREST_MIPMAP_NEAREST",
	"GL_LINEAR",
	"GL_LINEAR_MIPMAP_NEAREST",
	"GL_LINEAR_MIPMAP_LINEAR",
	NULL
};

#ifndef REF_HARD_LINKED
static const char *sirdNames[] = {
	"disabled",
	"enabled, default",
	"enabled, inverted",
	"layers only",
	NULL
};
#endif

typedef struct videoMenu_s {
	menuFrameWork_t	menu;
	int needRestart;

    int         nummodes;
    char        *modes[16+1];

#ifndef REF_HARD_LINKED
	menuSpinControl_t		driver;
#endif
	menuSlider_t	screensize;
	menuSlider_t	gamma;
	menuSpinControl_t  	fullscreen;

	menuSlider_t	picmip;
#ifdef _WIN32
	menuSpinControl_t  	vsync;
#endif
	menuSlider_t	lightmaps;
	menuSlider_t	saturation;
	menuSlider_t	anisotropy;
	menuSpinControl_t		gammaType;
	menuSpinControl_t		textureFilter;
	menuSpinControl_t		overrideTextures;
	menuSpinControl_t		overrideModels;
#if USE_DYNAMIC
	menuSpinControl_t		dlight;
#endif

#ifndef REF_HARD_LINKED
	menuSpinControl_t  	stipple;
	menuSpinControl_t  	sird;
#endif
} videoMenu_t;

static videoMenu_t		m_video;


#ifndef REF_HARD_LINKED
static void VideoMenu_ApplyChangesSoft( void ) {
	float gamma;

	/*
	** invert sense so greater = brighter, and scale to a range of 0.5 to 1.3
	*/
	gamma = ( 0.8 - ( m_video.gamma.curvalue / 10.0 - 0.5 ) ) + 0.5;

	cvar.Set( "vid_ref", "soft" );
	cvar.SetValue( "vid_gamma", gamma );
	cmd.ExecuteText( EXEC_NOW, va( "set vid_fullscreen %d\n", m_video.fullscreen.curvalue ) );
	cvar.SetInteger( "sw_stipplealpha", m_video.stipple.curvalue );
	cvar.SetInteger( "viewsize", m_video.screensize.curvalue * 10 );
	cvar.SetInteger( "sw_drawsird", m_video.sird.curvalue );
}
#endif

static void VideoMenu_ApplyChangesGL( void ) {
	float gamma;

	/*
	** invert sense so greater = brighter, and scale to a range of 0.5 to 1.3
	*/
	gamma = ( 0.8 - ( m_video.gamma.curvalue / 10.0 - 0.5 ) ) + 0.5;

	cvar.Set( "vid_ref", "gl" );
	cvar.SetValue( "vid_gamma", gamma );
	cvar.SetInteger( "gl_picmip", 3 - m_video.picmip.curvalue );
	cmd.ExecuteText( EXEC_NOW, va( "set vid_fullscreen %d\n", m_video.fullscreen.curvalue ) );
#ifdef _WIN32
	cvar.SetInteger( "gl_swapinterval", m_video.vsync.curvalue );
#endif
	cvar.SetInteger( "viewsize", m_video.screensize.curvalue * 10 );
	cvar.SetValue( "gl_coloredlightmaps", m_video.lightmaps.curvalue * 0.1f );
	cvar.SetValue( "gl_saturation", m_video.saturation.curvalue * 0.1f );
	cvar.SetValue( "gl_anisotropy", m_video.anisotropy.curvalue );
	cvar.SetInteger( "vid_hwgamma", m_video.gammaType.curvalue );
	cvar.Set( "gl_texturemode", filterValues[m_video.textureFilter.curvalue] );
	cvar.SetInteger( "r_override_textures", m_video.overrideTextures.curvalue );
	cvar.SetInteger( "r_override_models", m_video.overrideModels.curvalue );
#if USE_DYNAMIC
	cvar.SetInteger( "gl_dynamic", m_video.dlight.curvalue );
#endif
}

#ifndef REF_HARD_LINKED
static void VideoMenu_InitSoft( void ) {
	m_video.picmip.generic.flags |= QMF_HIDDEN;
#ifdef _WIN32
	m_video.vsync.generic.flags |= QMF_HIDDEN;
#endif
	m_video.lightmaps.generic.flags |= QMF_HIDDEN;
	m_video.saturation.generic.flags |= QMF_HIDDEN;
	m_video.anisotropy.generic.flags |= QMF_HIDDEN;
	m_video.gammaType.generic.flags |= QMF_HIDDEN;
	m_video.textureFilter.generic.flags |= QMF_HIDDEN;
	m_video.overrideTextures.generic.flags |= QMF_HIDDEN;
	m_video.overrideModels.generic.flags |= QMF_HIDDEN;
#if USE_DYNAMIC
	m_video.dlight.generic.flags |= QMF_HIDDEN;
#endif

	m_video.stipple.generic.flags &= ~QMF_HIDDEN;
	m_video.sird.generic.flags &= ~QMF_HIDDEN;

    Menu_Init( &m_video.menu );
}
#endif

static void VideoMenu_InitGL( void ) {
	m_video.picmip.generic.flags &= ~QMF_HIDDEN;
#ifdef _WIN32
	m_video.vsync.generic.flags &= ~QMF_HIDDEN;
#endif
	m_video.lightmaps.generic.flags &= ~QMF_HIDDEN;
	m_video.saturation.generic.flags &= ~QMF_HIDDEN;
	m_video.anisotropy.generic.flags &= ~QMF_HIDDEN;
	m_video.gammaType.generic.flags &= ~QMF_HIDDEN;
	m_video.textureFilter.generic.flags &= ~QMF_HIDDEN;
	m_video.overrideTextures.generic.flags &= ~QMF_HIDDEN;
	m_video.overrideModels.generic.flags &= ~QMF_HIDDEN;
#if USE_DYNAMIC
	m_video.dlight.generic.flags &= ~QMF_HIDDEN;
#endif

#ifndef REF_HARD_LINKED
	m_video.stipple.generic.flags |= QMF_HIDDEN;
	m_video.sird.generic.flags |= QMF_HIDDEN;
#endif

}

static int VideoMenu_Callback( int id, int msg, int param ) {
    float gamma;
    int i;

	switch( msg ) {
	case QM_CHANGE:
		switch( id ) {
#ifndef REF_HARD_LINKED
		case ID_REF:
			m_video.needRestart |= 2;
			if( !m_video.driver.curvalue ) {
				VideoMenu_InitSoft();
			} else {
				VideoMenu_InitGL();
			}
            Menu_Init( &m_video.menu );
			break;
#endif
		case ID_GAMMATYPE:
			m_video.needRestart |= 2;
			break;
		case ID_OVERRIDE:
		case ID_PICMIP:
		case ID_LIGHTMAPS:
		case ID_SATURATION:
			m_video.needRestart |= 1;
			break;
		case ID_GAMMA:
            if( cvar.VariableInteger( "vid_hwgamma" ) ) {
	            gamma = ( 0.8 - ( m_video.gamma.curvalue / 10.0 - 0.5 ) ) + 0.5;
	            cmd.ExecuteText( EXEC_NOW, va( "set vid_gamma %f\n", gamma ) );
            } else {
			    m_video.needRestart |= 1;
            }
            break;
		default:
			break;
		}

        if( m_video.needRestart ) {
            if( m_video.needRestart & 2 ) {
                m_video.menu.statusbar = "vid_restart pending";
            } else {
                m_video.menu.statusbar = "fs_restart pending";
            }
        }
		return QMS_NOTHANDLED;
    case QM_DESTROY:
        for( i = 1; i < m_video.nummodes; i++ ) {
            com.Free( m_video.modes[i] );
            m_video.modes[i] = NULL;
        }
        m_video.nummodes = 0;
        break;
    case QM_SIZE:
        Menu_Size( &m_video.menu );
        break;
    case QM_KEY:
        if( param != K_ESCAPE ) {
            break;
        }
        if( !keys.IsDown( K_CTRL ) ) {
#ifndef REF_HARD_LINKED
            if( !m_video.driver.curvalue ) {
                VideoMenu_ApplyChangesSoft();
            } else
#endif
            {
                VideoMenu_ApplyChangesGL();
            }
            if( m_video.needRestart ) {
                if( m_video.needRestart & 2 ) {
                    cmd.ExecuteText( EXEC_APPEND, "vid_restart\n" );
                } else {
                    cmd.ExecuteText( EXEC_APPEND, "fs_restart\n" );
                }
                UI_ForceMenuOff();
                return QMS_SILENT;
            }
        }
        UI_PopMenu();
        return QMS_OUT;
	default:
		break;
	}

	return QMS_NOTHANDLED;

}

static int UI_VariableInteger( const char *name, int min, int max ) {
    int v = cvar.VariableInteger( name );

    if( v < min ) {
        v = min;
    } else if( v > max ) {
        v = max;
    }

    return v;
}

static float UI_VariableValue( const char *name, float min, float max ) {
    float v = cvar.VariableValue( name );

    if( v < min ) {
        v = min;
    } else if( v > max ) {
        v = max;
    }

    return v;
}

static int UI_VariableBool( const char *name ) {
    int v = cvar.VariableInteger( name );

    return v ? 1 : 0;
}

static int UI_VariableString( const char *name, const char **values ) {
    char *s = cvar.VariableString( name );
    int i;

    for( i = 0; values[i]; i++ ) {
        if( !Q_stricmp( s, values[i] ) ) {
            return i;
        }
    }
    return 0;
}

static void VideoMenu_Init( void ) {
	char *s, *p;

	memset( &m_video, 0, sizeof( m_video ) );

	m_video.menu.callback = VideoMenu_Callback;

    m_video.modes[0] = "windowed";
    m_video.nummodes++;

    s = cvar.VariableString( "vid_modelist" );
    do {
        p = COM_Parse( ( const char ** )&s );
        if( *p ) {
            m_video.modes[m_video.nummodes++] = UI_CopyString( p );
        }
    } while( s && m_video.nummodes < 16 );

    if( m_video.nummodes == 1 ) {
        m_video.modes[m_video.nummodes++] = "default";
    }
    m_video.modes[m_video.nummodes] = NULL;

#ifndef REF_HARD_LINKED
	m_video.driver.generic.type = MTYPE_SPINCONTROL;
	m_video.driver.generic.flags = QMF_HASFOCUS;
	m_video.driver.generic.id = ID_REF;
	m_video.driver.generic.name = "driver";
	m_video.driver.itemnames = refNames;
	m_video.driver.curvalue = UI_VariableString( "vid_ref", refValues );
#endif

	m_video.screensize.generic.type	= MTYPE_SLIDER;
	m_video.screensize.generic.name	= "screen size";
	m_video.screensize.minvalue = 4;
	m_video.screensize.maxvalue = 10;
	m_video.screensize.curvalue = cvar.VariableInteger( "viewsize" ) / 10;

	m_video.gamma.generic.type	= MTYPE_SLIDER;
	m_video.gamma.generic.id	= ID_GAMMA;
	m_video.gamma.generic.name	= "gamma";
	m_video.gamma.minvalue = 5;
	m_video.gamma.maxvalue = 13;
	m_video.gamma.curvalue = ( 1.3 - cvar.VariableValue( "vid_gamma" ) + 0.5 ) * 10;

	m_video.fullscreen.generic.type = MTYPE_SPINCONTROL;
	m_video.fullscreen.generic.id = ID_FULLSCREEN;
	m_video.fullscreen.generic.name = "video mode";
	m_video.fullscreen.itemnames = ( const char ** )m_video.modes;
	m_video.fullscreen.curvalue = UI_VariableInteger( "vid_fullscreen", 0, m_video.nummodes - 1 );

	m_video.gammaType.generic.type = MTYPE_SPINCONTROL;
	m_video.gammaType.generic.id	= ID_GAMMATYPE;
	m_video.gammaType.generic.name	= "gamma correction";
	m_video.gammaType.curvalue = UI_VariableBool( "vid_hwgamma" );
	m_video.gammaType.itemnames = gammaNames;

	m_video.picmip.generic.type	= MTYPE_SLIDER;
	m_video.picmip.generic.id		= ID_PICMIP;
	m_video.picmip.generic.name	= "texture quality";
	m_video.picmip.minvalue = 0;
	m_video.picmip.maxvalue = 3;
	m_video.picmip.curvalue = 3 - UI_VariableInteger( "gl_picmip", 0, 3 );

	m_video.textureFilter.generic.type = MTYPE_SPINCONTROL;
	m_video.textureFilter.generic.id	= ID_TEXTUREFILTER;
	m_video.textureFilter.generic.name	= "texture filter";
	m_video.textureFilter.itemnames = filterNames;
	m_video.textureFilter.curvalue = UI_VariableString( "gl_texturemode", filterValues );

#ifdef _WIN32
	m_video.vsync.generic.type = MTYPE_SPINCONTROL;
	m_video.vsync.generic.name	= "vertical sync";
	m_video.vsync.curvalue = UI_VariableBool( "gl_swapinterval" );
	m_video.vsync.itemnames = yesnoNames;
#endif

	m_video.saturation.generic.type	= MTYPE_SLIDER;
	m_video.saturation.generic.id		= ID_SATURATION;
	m_video.saturation.generic.name	= "texture saturation";
	m_video.saturation.minvalue = 0;
	m_video.saturation.maxvalue = 10;
	m_video.saturation.curvalue = UI_VariableValue( "gl_saturation", 0, 1 ) * 10;

	m_video.lightmaps.generic.type	= MTYPE_SLIDER;
	m_video.lightmaps.generic.id		= ID_LIGHTMAPS;
	m_video.lightmaps.generic.name	= "lightmap saturation";
	m_video.lightmaps.minvalue = 0;
	m_video.lightmaps.maxvalue = 10;
	m_video.lightmaps.curvalue = UI_VariableValue( "gl_coloredlightmaps", 0, 1 ) * 10;

	m_video.anisotropy.generic.type	= MTYPE_SLIDER;
	m_video.anisotropy.generic.name	= "anisotropic filter";
	m_video.anisotropy.minvalue = 0;
	m_video.anisotropy.maxvalue = 16;
	m_video.anisotropy.curvalue = UI_VariableInteger( "gl_anisotropy", 0, 16 );

	m_video.overrideTextures.generic.type = MTYPE_SPINCONTROL;
	m_video.overrideTextures.generic.id = ID_OVERRIDE;
	m_video.overrideTextures.generic.name	= "override textures";
	m_video.overrideTextures.curvalue = UI_VariableBool( "r_override_textures" );
	m_video.overrideTextures.itemnames = yesnoNames;

	m_video.overrideModels.generic.type = MTYPE_SPINCONTROL;
	m_video.overrideModels.generic.id = ID_OVERRIDE;
	m_video.overrideModels.generic.name	= "override models";
	m_video.overrideModels.curvalue = UI_VariableBool( "r_override_models" );
	m_video.overrideModels.itemnames = yesnoNames;

#if USE_DYNAMIC
	m_video.dlight.generic.type = MTYPE_SPINCONTROL;
	m_video.dlight.generic.name	= "dynamic lighting";
	m_video.dlight.curvalue = UI_VariableInteger( "gl_dynamic", 0, 2 );
	m_video.dlight.itemnames = dlightNames;
#endif

#ifndef REF_HARD_LINKED
	m_video.stipple.generic.type = MTYPE_SPINCONTROL;
	m_video.stipple.generic.name	= "stipple alpha";
	m_video.stipple.curvalue = UI_VariableBool( "sw_stipplealpha" );
	m_video.stipple.itemnames = yesnoNames;

	m_video.sird.generic.type = MTYPE_SPINCONTROL;
	m_video.sird.generic.name	= "draw SIRDs";
	m_video.sird.curvalue = UI_VariableInteger( "sw_drawsird", 0, 2 );
	m_video.sird.itemnames = sirdNames;
#endif

	m_video.menu.banner = "Video";

#ifndef REF_HARD_LINKED
	Menu_AddItem( &m_video.menu, &m_video.driver );
#endif
	Menu_AddItem( &m_video.menu, &m_video.screensize );
	Menu_AddItem( &m_video.menu, &m_video.gamma );
	Menu_AddItem( &m_video.menu, &m_video.fullscreen );

	Menu_AddItem( &m_video.menu, &m_video.gammaType );
	Menu_AddItem( &m_video.menu, &m_video.picmip );
	Menu_AddItem( &m_video.menu, &m_video.textureFilter );
#ifdef _WIN32
	Menu_AddItem( &m_video.menu, &m_video.vsync );
#endif
	Menu_AddItem( &m_video.menu, &m_video.saturation );
	Menu_AddItem( &m_video.menu, &m_video.lightmaps );
	Menu_AddItem( &m_video.menu, &m_video.anisotropy );
	Menu_AddItem( &m_video.menu, &m_video.overrideTextures );
	Menu_AddItem( &m_video.menu, &m_video.overrideModels );
#if USE_DYNAMIC
	Menu_AddItem( &m_video.menu, &m_video.dlight );
#endif
#ifndef REF_HARD_LINKED
	Menu_AddItem( &m_video.menu, &m_video.stipple );
	Menu_AddItem( &m_video.menu, &m_video.sird );
#endif

#ifndef REF_HARD_LINKED
	if( !m_video.driver.curvalue ) {
		VideoMenu_InitSoft();
	} else
#endif
    {
		VideoMenu_InitGL();
	}
}

void M_Menu_Video_f( void ) {
	VideoMenu_Init();
	UI_PushMenu( &m_video.menu );
}

