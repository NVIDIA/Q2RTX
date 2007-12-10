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

#define REF_SOFT	0
#define REF_OPENGL	1
#define REF_3DFX	2
#define REF_POWERVR	3
#define REF_VERITE	4

#define ID_BACK		100
#define ID_APPLY	101
#define ID_DEFAULTS	102
#define ID_REF		103
#define ID_PICMIP	104
#define ID_PALETTEDTEXTURE	105
#define ID_LIGHTMAPS	106
#define ID_GAMMATYPE	107
#define ID_TEXTUREFILTER	108
#define ID_GAMMA		109
#define ID_FULLSCREEN 110
#define ID_MODE	111
#define ID_OVERRIDE	112
#define ID_SATURATION   113

static const char *refs[] = {
	"[software      ]",
	"[default OpenGL]",
#ifdef _WIN32
	"[3Dfx OpenGL   ]",
	"[PowerVR OpenGL]",
#endif
	NULL
};

static const char *yesnoNames[] = {
	"no",
	"yes",
	NULL
};

static const char *dlightNames[] = {
	"disabled",
	"additive",
    "modulative",
	NULL
};

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

static const char *sirdNames[] = {
	"disabled",
	"enabled, default",
	"enabled, inverted",
	"layers only",
	NULL
};

typedef struct videoMenu_s {
	menuFrameWork_t	menu;
	int needRestart;

    int         nummodes;
    char        *modes[16+1];

	menuSpinControl_t		driver;
	menuSlider_t	screensize;
	menuSlider_t	gamma;
	menuSpinControl_t  	fullscreen;
	menuAction_t	defaults;
	menuAction_t	apply;

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
	menuSpinControl_t		dlight;

	menuSpinControl_t  	stipple;
	menuSpinControl_t  	sird;
} videoMenu_t;

static videoMenu_t		m_video;


static void VideoMenu_ResetDefaults( void ) {
	UI_PopMenu();
	M_Menu_Video_f();
}

static void VideoMenu_ApplyChangesSoft( void ) {
	float gamma;

	/*
	** invert sense so greater = brighter, and scale to a range of 0.5 to 1.3
	*/
	gamma = ( 0.8 - ( m_video.gamma.curvalue / 10.0 - 0.5 ) ) + 0.5;

	cvar.Set( "vid_ref", "soft" );
	cvar.SetValue( "vid_gamma", gamma );
	//cvar.SetInteger( "vid_fullscreen", m_video.fullscreen.curvalue );
	cvar.SetInteger( "sw_stipplealpha", m_video.stipple.curvalue );
	cvar.SetInteger( "viewsize", m_video.screensize.curvalue * 10 );
	cvar.SetInteger( "sw_drawsird", m_video.sird.curvalue );

}

static void VideoMenu_ApplyChangesGL( void ) {
	float gamma;

	/*
	** invert sense so greater = brighter, and scale to a range of 0.5 to 1.3
	*/
	gamma = ( 0.8 - ( m_video.gamma.curvalue / 10.0 - 0.5 ) ) + 0.5;

	cvar.Set( "vid_ref", "gl" );
	cvar.SetValue( "vid_gamma", gamma );
	cvar.SetInteger( "gl_picmip", 3 - m_video.picmip.curvalue );
	//cvar.SetInteger( "vid_fullscreen", m_video.fullscreen.curvalue );
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
	cvar.SetInteger( "gl_dynamic", m_video.dlight.curvalue );

#ifdef _WIN32
	switch( m_video.driver.curvalue ) {
	case REF_OPENGL:
		cvar.Set( "gl_driver", "opengl32" );
		break;
	case REF_3DFX:
		cvar.Set( "gl_driver", "3dfxgl" );
		break;
	case REF_POWERVR:
		cvar.Set( "gl_driver", "pvrgl" );
		break;
	case REF_VERITE:
		cvar.Set( "gl_driver", "veritegl" );
		break;
	}
#endif

}

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
	m_video.dlight.generic.flags |= QMF_HIDDEN;

	m_video.stipple.generic.flags &= ~QMF_HIDDEN;
	m_video.sird.generic.flags &= ~QMF_HIDDEN;
}

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
	m_video.dlight.generic.flags &= ~QMF_HIDDEN;

	m_video.stipple.generic.flags |= QMF_HIDDEN;
	m_video.sird.generic.flags |= QMF_HIDDEN;
}

static int VideoMenu_Callback( int id, int msg, int param ) {
    float gamma;
    int i;

	switch( msg ) {
	case QM_ACTIVATE:
		switch( id ) {
		case ID_BACK:
			UI_PopMenu();
			return QMS_OUT;
		case ID_APPLY:
        apply:
	        cmd.ExecuteText( EXEC_NOW, va( "set vid_fullscreen %d\n", m_video.fullscreen.curvalue ) );
			if( m_video.driver.curvalue == REF_SOFT ) {
				VideoMenu_ApplyChangesSoft();
			} else {
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
			UI_PopMenu();
			return QMS_OUT;
		case ID_DEFAULTS:
			VideoMenu_ResetDefaults();
			break;
		default:
			break;
		}
		break;
	case QM_CHANGE:
		switch( id ) {
		case ID_REF:
			//m_video.needRestart |= 2;
			if( m_video.driver.curvalue == REF_SOFT ) {
				VideoMenu_InitSoft();
			} else if( param == REF_SOFT ) {
				VideoMenu_InitGL();
			}
			break;
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
                m_video.menu.statusbar = "vid_restart required";
            } else {
                m_video.menu.statusbar = "fs_restart required";
            }
        }
		return QMS_NOTHANDLED;
    case QM_KEY:
        if( param == 'a' ) {
			goto apply;
        }
        if( param == 'u' ) {
			VideoMenu_ResetDefaults();
        }
        break;
    case QM_DESTROY:
        for( i = 1; i < m_video.nummodes; i++ ) {
            com.Free( m_video.modes[i] );
            m_video.modes[i] = NULL;
        }
        m_video.nummodes = 0;
        break;
	default:
		break;
	}

	return QMS_NOTHANDLED;

}

static void VideoMenu_Init( void ) {
	int y, yy, yyy;
	char *s, *p;
	int i;
	float f;

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

	y = 64;

	m_video.driver.generic.type = MTYPE_SPINCONTROL;
	m_video.driver.generic.flags = QMF_HASFOCUS;
	m_video.driver.generic.id = ID_REF;
	m_video.driver.generic.name = "driver";
	m_video.driver.generic.x = uis.width / 2;
	m_video.driver.generic.y = y;
	m_video.driver.itemnames = refs;
	m_video.driver.curvalue = -1;
	y += MENU_SPACING;

	m_video.screensize.generic.type	= MTYPE_SLIDER;
	m_video.screensize.generic.x		= uis.width / 2;
	m_video.screensize.generic.y		= y;
	m_video.screensize.generic.name	= "screen size";
	m_video.screensize.minvalue = 3;
	m_video.screensize.maxvalue = 12;
	m_video.screensize.curvalue = cvar.VariableInteger( "viewsize" ) / 10;
	y += MENU_SPACING;

	m_video.gamma.generic.type	= MTYPE_SLIDER;
	m_video.gamma.generic.id	= ID_GAMMA;
	m_video.gamma.generic.x	= uis.width / 2;
	m_video.gamma.generic.y	= y;
	m_video.gamma.generic.name	= "gamma";
	m_video.gamma.minvalue = 5;
	m_video.gamma.maxvalue = 13;
	m_video.gamma.curvalue = ( 1.3 - cvar.VariableValue( "vid_gamma" ) + 0.5 ) * 10;
	y += MENU_SPACING;

	m_video.fullscreen.generic.type = MTYPE_SPINCONTROL;
	m_video.fullscreen.generic.id = ID_FULLSCREEN;
	m_video.fullscreen.generic.x = uis.width / 2;
	m_video.fullscreen.generic.y = y;
	m_video.fullscreen.generic.name = "video mode";
	m_video.fullscreen.itemnames = ( const char ** )m_video.modes;
    i = cvar.VariableInteger( "vid_fullscreen" );
    clamp( i, 0, m_video.nummodes - 1 );
	m_video.fullscreen.curvalue = i;
	y += MENU_SPACING;

	yy = y + MENU_SPACING * 2;

	m_video.gammaType.generic.type = MTYPE_SPINCONTROL;
	m_video.gammaType.generic.id	= ID_GAMMATYPE;
	m_video.gammaType.generic.x	= uis.width / 2;
	m_video.gammaType.generic.y	= yy;
	m_video.gammaType.generic.name	= "gamma correction";
	m_video.gammaType.curvalue = cvar.VariableInteger( "vid_hwgamma" ) ? 1 : 0;
	m_video.gammaType.itemnames = gammaNames;
	yy += MENU_SPACING;

	m_video.picmip.generic.type	= MTYPE_SLIDER;
	m_video.picmip.generic.id		= ID_PICMIP;
	m_video.picmip.generic.x		= uis.width / 2;
	m_video.picmip.generic.y		= yy;
	m_video.picmip.generic.name	= "texture quality";
	m_video.picmip.minvalue = 0;
	m_video.picmip.maxvalue = 3;
	i = cvar.VariableInteger( "gl_picmip" );
	clamp( i, 0, 3 );
	m_video.picmip.curvalue = 3 - i;
	yy += MENU_SPACING;

	m_video.textureFilter.generic.type = MTYPE_SPINCONTROL;
	m_video.textureFilter.generic.id	= ID_TEXTUREFILTER;
	m_video.textureFilter.generic.x	= uis.width / 2;
	m_video.textureFilter.generic.y	= yy;
	m_video.textureFilter.generic.name	= "texture filter";
	m_video.textureFilter.itemnames = filterNames;
	s = cvar.VariableString( "gl_texturemode" );
	for( i=0 ; filterValues[i] ; i++ ) {
		if( !Q_stricmp( s, filterValues[i] ) ) {
			m_video.textureFilter.curvalue = i;
			break;
		}
	}
	yy += MENU_SPACING;

#ifdef _WIN32
	m_video.vsync.generic.type = MTYPE_SPINCONTROL;
	m_video.vsync.generic.x	= uis.width / 2;
	m_video.vsync.generic.y	= yy;
	m_video.vsync.generic.name	= "vertical sync";
	m_video.vsync.curvalue = cvar.VariableInteger( "gl_swapinterval" ) ? 1 : 0;
	m_video.vsync.itemnames = yesnoNames;
	yy += MENU_SPACING;
#endif

	m_video.saturation.generic.type	= MTYPE_SLIDER;
	m_video.saturation.generic.id		= ID_SATURATION;
	m_video.saturation.generic.x		= uis.width / 2;
	m_video.saturation.generic.y		= yy;
	m_video.saturation.generic.name	= "texture saturation";
	m_video.saturation.minvalue = 0;
	m_video.saturation.maxvalue = 10;
	f = cvar.VariableValue( "gl_saturation" );
	clamp( f, 0, 1 );
	m_video.saturation.curvalue = f * 10;
	yy += MENU_SPACING;

	m_video.lightmaps.generic.type	= MTYPE_SLIDER;
	m_video.lightmaps.generic.id		= ID_LIGHTMAPS;
	m_video.lightmaps.generic.x		= uis.width / 2;
	m_video.lightmaps.generic.y		= yy;
	m_video.lightmaps.generic.name	= "lightmap saturation";
	m_video.lightmaps.minvalue = 0;
	m_video.lightmaps.maxvalue = 10;
	f = cvar.VariableValue( "gl_coloredlightmaps" );
	clamp( f, 0, 1 );
	m_video.lightmaps.curvalue = f * 10;
	yy += MENU_SPACING;

	m_video.anisotropy.generic.type	= MTYPE_SLIDER;
	m_video.anisotropy.generic.x		= uis.width / 2;
	m_video.anisotropy.generic.y		= yy;
	m_video.anisotropy.generic.name	= "anisotropic filter";
	m_video.anisotropy.minvalue = 0;
	m_video.anisotropy.maxvalue = 16;
	f = cvar.VariableValue( "gl_anisotropy" );
	clamp( f, 0, 16 );
	m_video.anisotropy.curvalue = f;
	yy += MENU_SPACING;

	m_video.overrideTextures.generic.type = MTYPE_SPINCONTROL;
	m_video.overrideTextures.generic.id = ID_OVERRIDE;
	m_video.overrideTextures.generic.x	= uis.width / 2;
	m_video.overrideTextures.generic.y	= yy;
	m_video.overrideTextures.generic.name	= "override textures";
	m_video.overrideTextures.curvalue = cvar.VariableInteger( "r_override_textures" ) ? 1 : 0;
	m_video.overrideTextures.itemnames = yesnoNames;
	yy += MENU_SPACING;

	m_video.overrideModels.generic.type = MTYPE_SPINCONTROL;
	m_video.overrideModels.generic.id = ID_OVERRIDE;
	m_video.overrideModels.generic.x	= uis.width / 2;
	m_video.overrideModels.generic.y	= yy;
	m_video.overrideModels.generic.name	= "override models";
	m_video.overrideModels.curvalue = cvar.VariableInteger( "r_override_models" ) ? 1 : 0;
	m_video.overrideModels.itemnames = yesnoNames;
	yy += MENU_SPACING;

	m_video.dlight.generic.type = MTYPE_SPINCONTROL;
	m_video.dlight.generic.x	= uis.width / 2;
	m_video.dlight.generic.y	= yy;
	m_video.dlight.generic.name	= "dynamic lighting";
    i = cvar.VariableInteger( "gl_dynamic" );
    clamp( i, 0, 2 );
	m_video.dlight.curvalue = i;
	m_video.dlight.itemnames = dlightNames;
	yy += MENU_SPACING;

	yyy = yy;
	yy = y + MENU_SPACING * 2;

	m_video.stipple.generic.type = MTYPE_SPINCONTROL;
	m_video.stipple.generic.x	= uis.width / 2;
	m_video.stipple.generic.y	= yy;
	m_video.stipple.generic.name	= "stipple alpha";
	m_video.stipple.curvalue = cvar.VariableInteger( "sw_stipplealpha" ) ? 1 : 0;
	m_video.stipple.itemnames = yesnoNames;
	yy += MENU_SPACING;

	m_video.sird.generic.type = MTYPE_SPINCONTROL;
	m_video.sird.generic.x	= uis.width / 2;
	m_video.sird.generic.y	= yy;
	m_video.sird.generic.name	= "draw SIRDs";
	i = cvar.VariableInteger( "sw_drawsird" );
	clamp( i, 0, 2 );
	m_video.sird.curvalue = i;
	m_video.sird.itemnames = sirdNames;
	yy += MENU_SPACING * 2;

	yyy += MENU_SPACING * 2;
	m_video.defaults.generic.type = MTYPE_ACTION;
	m_video.defaults.generic.id = ID_DEFAULTS;
	m_video.defaults.generic.uiFlags = UI_CENTER;
	m_video.defaults.generic.name = "undo changes (u)";
	m_video.defaults.generic.x    = uis.width / 2;
	m_video.defaults.generic.y    = yyy;
	yyy += MENU_SPACING;

	m_video.apply.generic.type = MTYPE_ACTION;
	m_video.apply.generic.id = ID_APPLY;
	m_video.apply.generic.uiFlags = UI_CENTER;
	m_video.apply.generic.name = "apply changes (a)";
	m_video.apply.generic.x    = uis.width / 2;
	m_video.apply.generic.y    = yyy;
	yyy += MENU_SPACING;

	m_video.menu.banner = "Video";

	if( !Q_stricmp( cvar.VariableString( "vid_ref" ), "soft" ) ) {
		m_video.driver.curvalue = REF_SOFT;
		VideoMenu_InitSoft();
	} else {
		m_video.driver.curvalue = REF_OPENGL;
#ifdef _WIN32
		s = cvar.VariableString( "gl_driver" );
		if( !Q_stricmp( s, "3dfxgl" ) ) {
			m_video.driver.curvalue = REF_3DFX;
		} else if( !Q_stricmp( s, "pvrgl" ) ) {
			m_video.driver.curvalue = REF_POWERVR;
		}
#endif
		VideoMenu_InitGL();
	}

	Menu_AddItem( &m_video.menu, (void *)&m_video.driver );
	Menu_AddItem( &m_video.menu, (void *)&m_video.screensize );
	Menu_AddItem( &m_video.menu, (void *)&m_video.gamma );
	Menu_AddItem( &m_video.menu, (void *)&m_video.fullscreen );

	Menu_AddItem( &m_video.menu, (void *)&m_video.gammaType );
	Menu_AddItem( &m_video.menu, (void *)&m_video.picmip );
	Menu_AddItem( &m_video.menu, (void *)&m_video.textureFilter );
#ifdef _WIN32
	Menu_AddItem( &m_video.menu, (void *)&m_video.vsync );
#endif
	Menu_AddItem( &m_video.menu, (void *)&m_video.saturation );
	Menu_AddItem( &m_video.menu, (void *)&m_video.lightmaps );
	Menu_AddItem( &m_video.menu, (void *)&m_video.anisotropy );
	Menu_AddItem( &m_video.menu, (void *)&m_video.overrideTextures );
	Menu_AddItem( &m_video.menu, (void *)&m_video.overrideModels );
	Menu_AddItem( &m_video.menu, (void *)&m_video.dlight );

	Menu_AddItem( &m_video.menu, (void *)&m_video.stipple );
	Menu_AddItem( &m_video.menu, (void *)&m_video.sird );
	Menu_AddItem( &m_video.menu, (void *)&m_video.defaults );
	Menu_AddItem( &m_video.menu, (void *)&m_video.apply );
}

void M_Menu_Video_f( void ) {
	VideoMenu_Init();
	UI_PushMenu( &m_video.menu );
}

