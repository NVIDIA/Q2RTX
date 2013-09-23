/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "shared/shared.h"
#include "shared/list.h"
#include "common/cmd.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/field.h"
#include "common/zone.h"
#include "client/keys.h"
#include "client/sound/sound.h"
#include "client/client.h"
#include "client/ui.h"
#include "refresh/refresh.h"

#define UI_Malloc(s)        Z_TagMalloc(s, TAG_UI)
#define UI_Mallocz(s)       Z_TagMallocz(s, TAG_UI)
#define UI_CopyString(s)    Z_TagCopyString(s, TAG_UI)

#define MAXMENUITEMS    64

typedef enum {
    MTYPE_BAD,
    MTYPE_SLIDER,
    MTYPE_LIST,
    MTYPE_ACTION,
    MTYPE_SPINCONTROL,
    MTYPE_SEPARATOR,
    MTYPE_FIELD,
    MTYPE_BITFIELD,
    MTYPE_PAIRS,
    MTYPE_STRINGS,
    MTYPE_VALUES,
    MTYPE_TOGGLE,
    MTYPE_STATIC,
    MTYPE_KEYBIND,
    MTYPE_BITMAP,
    MTYPE_SAVEGAME,
    MTYPE_LOADGAME
} menuType_t;

#define QMF_LEFT_JUSTIFY    0x00000001
#define QMF_GRAYED          0x00000002
#define QMF_NUMBERSONLY     0x00000004
#define QMF_HASFOCUS        0x00000008
#define QMF_HIDDEN          0x00000010
#define QMF_DISABLED        0x00000020
#define QMF_CUSTOM_COLOR    0x00000040

typedef enum {
    QMS_NOTHANDLED,
    QMS_SILENT,
    QMS_IN,
    QMS_MOVE,
    QMS_OUT,
    QMS_BEEP
} menuSound_t;

#define RCOLUMN_OFFSET  (CHAR_WIDTH * 2)
#define LCOLUMN_OFFSET -RCOLUMN_OFFSET

#define GENERIC_SPACING(x)   ((x) + (x) / 4)

#define MENU_SPACING    GENERIC_SPACING(CHAR_HEIGHT)

#define DOUBLE_CLICK_DELAY    300

#define UI_IsItemSelectable(item) \
    ((item)->type != MTYPE_SEPARATOR && \
     (item)->type != MTYPE_STATIC && \
     !((item)->flags & (QMF_GRAYED | QMF_HIDDEN | QMF_DISABLED)))

typedef void (*confirmAction_t)(qboolean);

typedef struct menuFrameWork_s {
    list_t  entry;

    char    *name, *title, *status;

    int     nitems;
    void    *items[MAXMENUITEMS];

    qboolean compact;
    qboolean transparent;
    qboolean keywait;

    qhandle_t image;
    color_t color;
    int y1, y2;

    int mins[2];
    int maxs[2];

    qhandle_t banner;
    vrect_t banner_rc;

    qhandle_t plaque;
    vrect_t plaque_rc;

    qhandle_t logo;
    vrect_t logo_rc;

    qboolean (*push)(struct menuFrameWork_s *);
    void (*pop)(struct menuFrameWork_s *);
    void (*expose)(struct menuFrameWork_s *);
    void (*draw)(struct menuFrameWork_s *);
    void (*size)(struct menuFrameWork_s *);
    void (*free)(struct menuFrameWork_s *);
    menuSound_t (*keydown)(struct menuFrameWork_s *, int);
} menuFrameWork_t;

typedef struct menuCommon_s {
    menuType_t type;
    int id;
    char *name;
    menuFrameWork_t *parent;
    color_t color;
    vrect_t rect;
    char *status;

    int x, y;
    int width, height;

    int flags;
    int uiFlags;

    menuSound_t (*activate)(struct menuCommon_s *);
    menuSound_t (*change)(struct menuCommon_s *);
    menuSound_t (*keydown)(struct menuCommon_s *, int key);
    menuSound_t (*focus)(struct menuCommon_s *, qboolean gain);
} menuCommon_t;

typedef struct menuField_s {
    menuCommon_t generic;
    inputField_t field;
    cvar_t *cvar;
    int width;
} menuField_t;

#define SLIDER_RANGE 10

typedef struct menuSlider_s {
    menuCommon_t generic;
    cvar_t *cvar;
    qboolean modified;

    float minvalue;
    float maxvalue;
    float curvalue;
    float step;
} menuSlider_t;

#define MAX_COLUMNS     8

#define MLIST_SPACING           GENERIC_SPACING(CHAR_HEIGHT)
#define MLIST_BORDER_WIDTH      1
#define MLIST_SCROLLBAR_WIDTH   GENERIC_SPACING(CHAR_WIDTH)
#define MLIST_PRESTEP           3
#define MLIST_PADDING           (MLIST_PRESTEP*2)

#define MLF_HEADER      0x00000001
#define MLF_SCROLLBAR   0x00000002
#define MLF_COLOR       0x00000004

typedef struct menuListColumn_s {
    char *name;
    int width;
    int uiFlags;
} menuListColumn_t;

typedef struct menuList_s {
    menuCommon_t generic;

    void        **items;
    int         numItems;
    int         maxItems;
    int         mlFlags;
    int         extrasize;

    int        prestep;
    int        curvalue;
    int        clickTime;

    char    scratch[8];
    int     scratchCount;
    int     scratchTime;

    int     drag_y;

    menuListColumn_t    columns[MAX_COLUMNS];
    int                 numcolumns;
    int                 sortdir, sortcol;

    menuSound_t (*sort)(struct menuList_s *);
} menuList_t;

typedef struct menuSpinControl_s {
    menuCommon_t generic;
    cvar_t *cvar;

    char    **itemnames;
    char    **itemvalues;
    int     numItems;
    int     curvalue;

    int         mask;
    qboolean    negate;
} menuSpinControl_t;

typedef struct menuAction_s {
    menuCommon_t generic;
    char *cmd;
} menuAction_t;

typedef struct menuSeparator_s {
    menuCommon_t generic;
} menuSeparator_t;

typedef struct menuStatic_s {
    menuCommon_t    generic;
    int             maxChars;
} menuStatic_t;

typedef struct menuBitmap_s {
    menuCommon_t generic;
    qhandle_t pics[2];
    char *cmd;
} menuBitmap_t;

typedef struct menuKeybind_s {
    menuCommon_t    generic;
    char            binding[32];
    char            altbinding[32];
    char            *cmd;
    char            *altstatus;
} menuKeybind_t;

#define MAX_PLAYERMODELS 1024

typedef struct playerModelInfo_s {
    int nskins;
    char **skindisplaynames;
    char *directory;
} playerModelInfo_t;

void PlayerModel_Load(void);
void PlayerModel_Free(void);

#define MAX_MENU_DEPTH    8

// animated menu cursor
#define CURSOR_WIDTH    32
#define CURSOR_OFFSET   25

#define NUM_CURSOR_FRAMES 15

typedef struct uiStatic_s {
    qboolean initialized;
    int realtime;
    int width, height; // scaled
    float scale;
    int menuDepth;
    menuFrameWork_t *layers[MAX_MENU_DEPTH];
    menuFrameWork_t *activeMenu;
    menuCommon_t *mouseTracker;
    int mouseCoords[2];
    qboolean entersound;        // play after drawing a frame, so caching
                                // won't disrupt the sound
    qboolean transparent;
    int numPlayerModels;
    playerModelInfo_t pmi[MAX_PLAYERMODELS];
    char weaponModel[32];

    qhandle_t backgroundHandle;
    qhandle_t fontHandle;
    qhandle_t cursorHandle;
    int cursorWidth, cursorHeight;

    qhandle_t bitmapCursors[NUM_CURSOR_FRAMES];

    struct {
        color_t background;
        color_t normal;
        color_t active;
        color_t selection;
        color_t disabled;
    } color;
} uiStatic_t;

extern uiStatic_t   uis;

extern list_t       ui_menus;

extern cvar_t       *ui_debug;

void        UI_PushMenu(menuFrameWork_t *menu);
void        UI_ForceMenuOff(void);
void        UI_PopMenu(void);
void        UI_StartSound(menuSound_t sound);
qboolean    UI_DoHitTest(void);
qboolean    UI_CursorInRect(vrect_t *rect);
void        *UI_FormatColumns(int extrasize, ...) q_sentinel;
char        *UI_GetColumn(char *s, int n);
void        UI_DrawString(int x, int y, int flags, const char *string);
void        UI_DrawChar(int x, int y, int flags, int ch);
void        UI_DrawRect8(const vrect_t *rect, int border, int c);
//void        UI_DrawRect32(const vrect_t *rect, int border, uint32_t color);
void        UI_StringDimensions(vrect_t *rc, int flags, const char *string);

void        UI_LoadScript(void);
menuFrameWork_t *UI_FindMenu(const char *name);

void        Menu_Init(menuFrameWork_t *menu);
void        Menu_Size(menuFrameWork_t *menu);
void        Menu_Draw(menuFrameWork_t *menu);
void        Menu_AddItem(menuFrameWork_t *menu, void *item);
menuSound_t Menu_SelectItem(menuFrameWork_t *menu);
menuSound_t Menu_SlideItem(menuFrameWork_t *menu, int dir);
menuSound_t Menu_KeyEvent(menuCommon_t *item, int key);
menuSound_t Menu_CharEvent(menuCommon_t *item, int key);
menuSound_t Menu_MouseMove(menuCommon_t *item);
menuSound_t Menu_Keydown(menuFrameWork_t *menu, int key);
void        Menu_SetFocus(menuCommon_t *item);
menuSound_t     Menu_AdjustCursor(menuFrameWork_t *menu, int dir);
menuCommon_t    *Menu_ItemAtCursor(menuFrameWork_t *menu);
menuCommon_t    *Menu_HitTest(menuFrameWork_t *menu);
void        MenuList_Init(menuList_t *l);
void        MenuList_SetValue(menuList_t *l, int value);
void        MenuList_Sort(menuList_t *l, int offset,
                          int (*cmpfunc)(const void *, const void *));
void SpinControl_Init(menuSpinControl_t *s);
qboolean    Menu_Push(menuFrameWork_t *menu);
void        Menu_Pop(menuFrameWork_t *menu);
void        Menu_Free(menuFrameWork_t *menu);

void M_Menu_PlayerConfig(void);
void M_Menu_Demos(void);
void M_Menu_Servers(void);

