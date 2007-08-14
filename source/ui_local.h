/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#ifndef __QMENU_H__
#define __QMENU_H__

#include "config.h"
#include "q_shared.h"
#include "q_list.h"
#include "q_field.h"
#include "q_uis.h"
#include "com_public.h"
#include "ref_public.h"
#include "key_public.h"
#include "snd_public.h"
#include "cl_public.h"
#include "ui_public.h"

#define UI_Malloc( size )	com.TagMalloc( size, TAG_UI )

#define SMALLCHAR_WIDTH		8
#define SMALLCHAR_HEIGHT	8

#define MAXMENUITEMS	64

#define MTYPE_BAD			0
#define MTYPE_SLIDER		1
#define MTYPE_LIST			2
#define MTYPE_ACTION		3
#define MTYPE_SPINCONTROL	4
#define MTYPE_SEPARATOR  	5
#define MTYPE_FIELD			6
#define MTYPE_BITMAP		7
#define MTYPE_IMAGELIST		8
#define MTYPE_STATIC		9
#define MTYPE_KEYBIND		10

#define QMF_LEFT_JUSTIFY	0x00000001
#define QMF_GRAYED			0x00000002
#define QMF_NUMBERSONLY		0x00000004
#define QMF_HASFOCUS		0x00000008
#define QMF_HIDDEN			0x00000010
#define QMF_DISABLED		0x00000020
#define QMF_CUSTOM_COLOR	0x00000040

#define ID_MENU				-1

#define QM_GOTFOCUS			1
#define QM_LOSTFOCUS		2
#define QM_ACTIVATE			3
#define QM_CHANGE			4
#define QM_KEY				5
#define QM_CHAR				6
#define QM_MOUSE			7
#define QM_DESTROY			8
#define QM_DESTROY_CHILD	9

#define QMS_NOTHANDLED		0
#define QMS_SILENT			1
#define QMS_IN				2
#define QMS_MOVE			3
#define QMS_OUT				4
#define QMS_BEEP			5

#define RCOLUMN_OFFSET  16
#define LCOLUMN_OFFSET -16

#define	MENU_SPACING	12

#define DOUBLE_CLICK_DELAY	300

#define BUTTON_YPOS			( uis.glconfig.vidHeight - ( 60 + 32 ) / 2 )

#define UI_IsItemSelectable( item ) \
	( (item)->type != MTYPE_SEPARATOR && \
	(item)->type != MTYPE_STATIC && \
	!( (item)->flags & (QMF_GRAYED|QMF_HIDDEN|QMF_DISABLED) ) )

typedef struct menuFrameWork_s {
	char	*statusbar;

	int		nitems;
	void	*items[MAXMENUITEMS];

	qboolean transparent;
	qboolean keywait;

	void		(*draw)( struct menuFrameWork_s *self );
	int			(*callback)( int id, int msg, int param );
} menuFrameWork_t;

typedef struct menuCommon_s {
	int type;
	int id;
	const char *name;
	menuFrameWork_t *parent;
	color_t	color;
	vrect_t rect;

	int x, y;
	int width, height;

	uint32 flags;
	uint32 uiFlags;
} menuCommon_t;

typedef struct menuField_s {
	menuCommon_t generic;
	inputField_t field;
} menuField_t;

typedef struct menuSlider_s {
	menuCommon_t generic;

	float minvalue;
	float maxvalue;
	float curvalue;

	float range;
} menuSlider_t;

#define MAX_COLUMNS	8
#define MLIST_SPACING	10
#define MLIST_BORDER_WIDTH	1
#define MLIST_SCROLLBAR_WIDTH	10
#define MLIST_PRESTEP	3

typedef enum menuListFlags_e {
	MLF_NOSELECT				= (1<<0),
	MLF_HIDE_SCROLLBAR			= (1<<1),
	MLF_HIDE_SCROLLBAR_EMPTY	= (1<<2),
	MLF_HIDE_BACKGROUND			= (1<<3)
} menuListFlags_t;

typedef struct menuListColumn_s {
	const char	*name;
	int		width;
	int		uiFlags;
} menuListColumn_t;

typedef struct menuList_s {
	menuCommon_t generic;

	const char	**itemnames;
	int			numItems;
	int			maxItems;
	menuListFlags_t mlFlags;

	int		prestep;
	int		curvalue;
	int		clickTime;

    char    scratch[8];
    int     scratchCount;
    int     scratchTime;

	menuListColumn_t	columns[MAX_COLUMNS];
	int					numcolumns;
	qboolean			drawNames;
} menuList_t;

typedef struct imageList_s {
	menuCommon_t generic;

	const char *name;

	int prestep;
	int curvalue;
	const char **names;
	const qhandle_t *images;
	int clickTime;

	int numcolumns;
	int numRows;

	int imageWidth;
	int imageHeight;
} imageList_t;

typedef struct menuSpinControl_s {
	menuCommon_t generic;

	const char	**itemnames;
	int			numItems;
	int			curvalue;
} menuSpinControl_t;

typedef struct menuAction_s {
	menuCommon_t generic;
} menuAction_t;

typedef struct menuSeparator_s {
	menuCommon_t generic;
} menuSeparator_t;

typedef struct menuBitmap_s {
	menuCommon_t generic;
	qhandle_t	pic;
	const char *errorImage;
} menuBitmap_t;

typedef struct menuStatic_s {
	menuCommon_t	generic;
	int				maxChars;
} menuStatic_t;

typedef struct menuKeybind_s {
	menuCommon_t	generic;
	char			binding[32];
	char			altbinding[32];
} menuKeybind_t;

#define MAX_PLAYERMODELS 32

typedef struct playerModelInfo_s {
	int		nskins;
	char	**skindisplaynames;
	char **weaponNames;
	int numWeapons;
	char	directory[MAX_QPATH];
} playerModelInfo_t;

void PlayerModel_Load( void );
void PlayerModel_Free( void );

#define	MAX_MENU_DEPTH	8

typedef struct uiStatic_s {
	int realtime;
	glconfig_t glconfig;
    clipRect_t clipRect;
	int menuDepth;
	menuFrameWork_t	*layers[MAX_MENU_DEPTH];
	menuFrameWork_t *activeMenu;
	int mouseCoords[2];
	qboolean	entersound;		// play after drawing a frame, so caching
								// won't disrupt the sound
	qboolean	transparent;
	int		numPlayerModels;
	playerModelInfo_t	pmi[MAX_PLAYERMODELS];

	qhandle_t	backgroundHandle;
	qhandle_t	fontHandle;
	qhandle_t	cursorHandle;
	int			cursorWidth, cursorHeight;
} uiStatic_t;

extern uiStatic_t	uis;

extern cvar_t		*ui_debug;

void		UI_PushMenu( menuFrameWork_t *menu );
void		UI_ForceMenuOff( void );
void		UI_PopMenu( void );
qboolean	UI_DoHitTest( void );
qboolean	UI_CursorInRect( vrect_t *rect, int mx, int my );
char		*UI_FormatColumns( int numArgs, ... );
void		UI_AddToServerList( const serverStatus_t *status );
char		*UI_CopyString( const char *in );
void		UI_DrawLoading( int realtime );
void		UI_SetupDefaultBanner( menuStatic_t *banner, const char *name );
void		UI_DrawString( int x, int y, const color_t color, uint32 flags, const char *string );
void		UI_DrawChar( int x, int y, uint32 flags, int ch );
void		UI_StringDimensions( vrect_t *rc, uint32 flags, const char *string );

void		Menu_Draw( menuFrameWork_t *menu );
void		Menu_AddItem( menuFrameWork_t *menu, void *item );
int			Menu_SelectItem( menuFrameWork_t *menu );
int			Menu_SlideItem( menuFrameWork_t *menu, int dir );
int			Menu_KeyEvent( menuCommon_t *item, int key );
int			Menu_CharEvent( menuCommon_t *item, int key );
int			Menu_MouseMove( menuCommon_t *item );
void		Menu_SetFocus( menuCommon_t *item );
int			Menu_AdjustCursor( menuFrameWork_t *menu, int dir );
menuCommon_t	*Menu_ItemAtCursor( menuFrameWork_t *menu );
menuCommon_t	*Menu_HitTest( menuFrameWork_t *menu, int x, int y );
void		MenuList_Init( menuList_t *l );
void		MenuList_SetValue( menuList_t *l, int value );

void SpinControl_Init( menuSpinControl_t *s );
void Bitmap_Init( menuBitmap_t *b );

void M_Menu_Error_f( comErrorType_t type, const char *text );
void M_Menu_Confirm_f( const char *text, void (*action)( qboolean yes ) );
void M_Menu_Main_f (void);
	void M_Menu_Game_f (void);
		void M_Menu_LoadGame_f (void);
		void M_Menu_SaveGame_f (void);
		void M_Menu_PlayerConfig_f (void);
			void M_Menu_DownloadOptions_f (void);
		void M_Menu_Credits_f( void );
	void M_Menu_Multiplayer_f( void );
		void M_Menu_JoinServer_f (void);
			void M_Menu_AddressBook_f( void );
		void M_Menu_StartServer_f (void);
			void M_Menu_DMOptions_f (void);
	void M_Menu_Video_f (void);
	void M_Menu_Options_f (void);
		void M_Menu_Keys_f (void);
		void M_Menu_Weapons_f( void );
	void M_Menu_Quit_f (void);
	void M_Menu_Demos_f( void );

	void M_Menu_Credits( void );
	void M_Menu_Network_f( void );

	void M_Menu_Mods_f( void );
	void M_Menu_Ingame_f( void );


#endif
