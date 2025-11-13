/*
Copyright (C) 2003-2006 Andrey Nazarov

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

#include "ui.h"
#include "client/input.h"
#include "../client.h"
#include "common/prompt.h"

uiStatic_t    uis;

LIST_DECL(ui_menus);

cvar_t    *ui_debug;
static cvar_t    *ui_open;
static cvar_t    *ui_scale;

// ===========================================================================

/*
=================
UI_PushMenu
=================
*/
void UI_PushMenu(menuFrameWork_t *menu)
{
    int i, j;

    if (!menu) {
        return;
    }

    // if this menu is already present, drop back to that level
    // to avoid stacking menus by hotkeys
    for (i = 0; i < uis.menuDepth; i++) {
        if (uis.layers[i] == menu) {
            break;
        }
    }

    if (i == uis.menuDepth) {
        if (uis.menuDepth >= MAX_MENU_DEPTH) {
            Com_EPrintf("UI_PushMenu: MAX_MENU_DEPTH exceeded\n");
            return;
        }
        uis.layers[uis.menuDepth++] = menu;
    } else {
        for (j = i; j < uis.menuDepth; j++) {
            UI_PopMenu();
        }
        uis.menuDepth = i + 1;
    }

    if (menu->push && !menu->push(menu)) {
        uis.menuDepth--;
        return;
    }

    Menu_Init(menu);

    Key_SetDest((Key_GetDest() & ~KEY_CONSOLE) | KEY_MENU);

    Con_Close(true);

    if (!uis.activeMenu) {
        // opening menu moves cursor to the nice location
        IN_WarpMouse(menu->mins[0] / uis.scale, menu->mins[1] / uis.scale);

        uis.mouseCoords[0] = menu->mins[0];
        uis.mouseCoords[1] = menu->mins[1];

        uis.entersound = true;
    }

    uis.activeMenu = menu;

    UI_DoHitTest();

    if (menu->expose) {
        menu->expose(menu);
    }
}

static void UI_Resize(void)
{
    int i;

    uis.scale = R_ClampScale(ui_scale);
    uis.width = Q_rint(r_config.width * uis.scale);
    uis.height = Q_rint(r_config.height * uis.scale);

    for (i = 0; i < uis.menuDepth; i++) {
        Menu_Init(uis.layers[i]);
    }

    //CL_WarpMouse(0, 0);
}


/*
=================
UI_ForceMenuOff
=================
*/
void UI_ForceMenuOff(void)
{
    menuFrameWork_t *menu;
    int i;

    for (i = 0; i < uis.menuDepth; i++) {
        menu = uis.layers[i];
        if (menu->pop) {
            menu->pop(menu);
        }
    }

    Key_SetDest(Key_GetDest() & ~KEY_MENU);
    uis.menuDepth = 0;
    uis.activeMenu = NULL;
    uis.mouseTracker = NULL;
    uis.transparent = false;
}

/*
=================
UI_PopMenu
=================
*/
void UI_PopMenu(void)
{
    menuFrameWork_t *menu;

    Q_assert(uis.menuDepth > 0);

    menu = uis.layers[--uis.menuDepth];
    if (menu->pop) {
        menu->pop(menu);
    }

    if (!uis.menuDepth) {
        UI_ForceMenuOff();

		// Save the config file if the user closes the menu while in-game
		if (cls.state >= ca_active) {
			CL_WriteConfig();
		}

        return;
    }

    uis.activeMenu = uis.layers[uis.menuDepth - 1];
    uis.mouseTracker = NULL;

    UI_DoHitTest();
}

/*
=================
UI_IsTransparent
=================
*/
bool UI_IsTransparent(void)
{
    if (!(Key_GetDest() & KEY_MENU)) {
        return true;
    }

    if (!uis.activeMenu) {
        return true;
    }

    return uis.activeMenu->transparent;
}

menuFrameWork_t *UI_FindMenu(const char *name)
{
    menuFrameWork_t *menu;

    LIST_FOR_EACH(menuFrameWork_t, menu, &ui_menus, entry) {
        if (!strcmp(menu->name, name)) {
            return menu;
        }
    }

    return NULL;
}

/*
=================
UI_OpenMenu
=================
*/
void UI_OpenMenu(uiMenu_t type)
{
    menuFrameWork_t *menu = NULL;

    if (!uis.initialized) {
        return;
    }

    // close any existing menus
    UI_ForceMenuOff();

    switch (type) {
    case UIMENU_DEFAULT:
        if (ui_open->integer) {
            menu = UI_FindMenu("main");
        }
        break;
    case UIMENU_MAIN:
        menu = UI_FindMenu("main");
        break;
    case UIMENU_GAME:
        menu = UI_FindMenu("game");
        if (!menu) {
            menu = UI_FindMenu("main");
        }
        break;
    case UIMENU_NONE:
        break;
    default:
        Q_assert(!"bad menu");
    }

    UI_PushMenu(menu);
}

//=============================================================================

/*
=================
UI_FormatColumns
=================
*/
void *UI_FormatColumns(int extrasize, ...)
{
    va_list argptr;
    char *buffer, *p;
    int i, j;
    size_t total = 0;
    char *strings[MAX_COLUMNS];
    size_t lengths[MAX_COLUMNS];

    va_start(argptr, extrasize);
    for (i = 0; i < MAX_COLUMNS; i++) {
        if ((p = va_arg(argptr, char *)) == NULL) {
            break;
        }
        strings[i] = p;
        total += lengths[i] = strlen(p) + 1;
    }
    va_end(argptr);

    buffer = UI_Malloc(extrasize + total + 1);
    p = buffer + extrasize;
    for (j = 0; j < i; j++) {
        memcpy(p, strings[j], lengths[j]);
        p += lengths[j];
    }
    *p = 0;

    return buffer;
}

char *UI_GetColumn(char *s, int n)
{
    int i;

    for (i = 0; i < n && *s; i++) {
        s += strlen(s) + 1;
    }

    return s;
}

/*
=================
UI_CursorInRect
=================
*/
bool UI_CursorInRect(vrect_t *rect)
{
    if (uis.mouseCoords[0] < rect->x) {
        return false;
    }
    if (uis.mouseCoords[0] >= rect->x + rect->width) {
        return false;
    }
    if (uis.mouseCoords[1] < rect->y) {
        return false;
    }
    if (uis.mouseCoords[1] >= rect->y + rect->height) {
        return false;
    }
    return true;
}

void UI_DrawString(int x, int y, int flags, const char *string)
{
    if ((flags & UI_CENTER) == UI_CENTER) {
        x -= strlen(string) * CHAR_WIDTH / 2;
    } else if (flags & UI_RIGHT) {
        x -= strlen(string) * CHAR_WIDTH;
    }

    R_DrawString(x, y, flags, MAX_STRING_CHARS, string, uis.fontHandle);
}

void UI_DrawChar(int x, int y, int flags, int ch)
{
    R_DrawChar(x, y, flags, ch, uis.fontHandle);
}

void UI_StringDimensions(vrect_t *rc, int flags, const char *string)
{
    rc->height = CHAR_HEIGHT;
    rc->width = CHAR_WIDTH * strlen(string);

    if ((flags & UI_CENTER) == UI_CENTER) {
        rc->x -= rc->width / 2;
    } else if (flags & UI_RIGHT) {
        rc->x -= rc->width;
    }
}

void UI_DrawRect8(const vrect_t *rc, int border, int c)
{
    R_DrawFill8(rc->x, rc->y, border, rc->height, c);   // left
    R_DrawFill8(rc->x + rc->width - border, rc->y, border, rc->height, c);   // right
    R_DrawFill8(rc->x + border, rc->y, rc->width - border * 2, border, c);   // top
    R_DrawFill8(rc->x + border, rc->y + rc->height - border, rc->width - border * 2, border, c);   // bottom
}

#if 0
void UI_DrawRect32(const vrect_t *rc, int border, uint32_t color)
{
    R_DrawFill32(rc->x, rc->y, border, rc->height, color);   // left
    R_DrawFill32(rc->x + rc->width - border, rc->y, border, rc->height, color);   // right
    R_DrawFill32(rc->x + border, rc->y, rc->width - border * 2, border, color);   // top
    R_DrawFill32(rc->x + border, rc->y + rc->height - border, rc->width - border * 2, border, color);   // bottom
}
#endif

//=============================================================================
/* Menu Subsystem */

/*
=================
UI_DoHitTest
=================
*/
bool UI_DoHitTest(void)
{
    menuCommon_t *item;

    if (!uis.activeMenu) {
        return false;
    }

    if (uis.mouseTracker) {
        item = uis.mouseTracker;
    } else {
        if (!(item = Menu_HitTest(uis.activeMenu))) {
            return false;
        }
    }

    if (!UI_IsItemSelectable(item)) {
        return false;
    }

    Menu_MouseMove(item);

    if (item->flags & QMF_HASFOCUS) {
        return false;
    }

    Menu_SetFocus(item);

    return true;
}

/*
=================
UI_MouseEvent
=================
*/
void UI_MouseEvent(int x, int y)
{
    x = Q_clip(x, 0, r_config.width - 1);
    y = Q_clip(y, 0, r_config.height - 1);

    uis.mouseCoords[0] = Q_rint(x * uis.scale);
    uis.mouseCoords[1] = Q_rint(y * uis.scale);

    UI_DoHitTest();
}

/*
=================
UI_Draw
=================
*/
void UI_Draw(unsigned realtime)
{
    int i;

    uis.realtime = realtime;

    if (!(Key_GetDest() & KEY_MENU)) {
        return;
    }

    if (!uis.activeMenu) {
        return;
    }

    R_ClearColor();
    R_SetScale(uis.scale);

    if (1) {
        // draw top menu
        if (uis.activeMenu->draw) {
            uis.activeMenu->draw(uis.activeMenu);
        } else {
            Menu_Draw(uis.activeMenu);
        }
    } else {
        // draw all layers
        for (i = 0; i < uis.menuDepth; i++) {
            if (uis.layers[i]->draw) {
                uis.layers[i]->draw(uis.layers[i]);
            } else {
                Menu_Draw(uis.layers[i]);
            }
        }
    }

    // draw custom cursor in fullscreen mode
    if (r_config.flags & QVF_FULLSCREEN) {
        R_DrawPic(uis.mouseCoords[0] - uis.cursorWidth / 2,
                  uis.mouseCoords[1] - uis.cursorHeight / 2, uis.cursorHandle);
    }

    if (ui_debug->integer) {
        UI_DrawString(uis.width - 4, 4, UI_RIGHT,
                      va("%3i %3i", uis.mouseCoords[0], uis.mouseCoords[1]));
    }

    // delay playing the enter sound until after the
    // menu has been drawn, to avoid delay while
    // caching images
    if (uis.entersound) {
        uis.entersound = false;
        S_StartLocalSound("misc/menu1.wav");
    }

    R_ClearColor();
    R_SetScale(1.0f);
}

void UI_StartSound(menuSound_t sound)
{
    switch (sound) {
    case QMS_IN:
        S_StartLocalSound("misc/menu1.wav");
        break;
    case QMS_MOVE:
        S_StartLocalSound("misc/menu2.wav");
        break;
    case QMS_OUT:
        S_StartLocalSound("misc/menu3.wav");
        break;
    case QMS_BEEP:
        S_StartLocalSound("misc/talk1.wav");
        break;
    default:
        break;
    }
}

/*
=================
UI_KeyEvent
=================
*/
void UI_KeyEvent(int key, bool down)
{
    menuSound_t sound;

    if (!uis.activeMenu) {
        return;
    }

    if (!down) {
        if (key == K_MOUSE1) {
            uis.mouseTracker = NULL;
        }
        return;
    }

    sound = Menu_Keydown(uis.activeMenu, key);

    UI_StartSound(sound);
}

/*
=================
UI_CharEvent
=================
*/
void UI_CharEvent(int key)
{
    menuCommon_t *item;
    menuSound_t sound;

    if (!uis.activeMenu) {
        return;
    }

    if ((item = Menu_ItemAtCursor(uis.activeMenu)) == NULL ||
        (sound = Menu_CharEvent(item, key)) == QMS_NOTHANDLED) {
        return;
    }

    UI_StartSound(sound);
}

static void UI_Menu_g(genctx_t *ctx)
{
    menuFrameWork_t *menu;

    LIST_FOR_EACH(menuFrameWork_t, menu, &ui_menus, entry)
        Prompt_AddMatch(ctx, menu->name);
}

static void UI_PushMenu_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        UI_Menu_g(ctx);
    }
}

static void UI_PushMenu_f(void)
{
    menuFrameWork_t *menu;
    char *s;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <menu>\n", Cmd_Argv(0));
        return;
    }
    s = Cmd_Argv(1);
    menu = UI_FindMenu(s);
    if (menu) {
        UI_PushMenu(menu);
    } else {
        Com_Printf("No such menu: %s\n", s);
    }
}

static void UI_PopMenu_f(void)
{
    if (uis.activeMenu) {
        UI_PopMenu();
    }
}


static const cmdreg_t c_ui[] = {
    { "forcemenuoff", UI_ForceMenuOff },
    { "pushmenu", UI_PushMenu_f, UI_PushMenu_c },
    { "popmenu", UI_PopMenu_f },

    { NULL, NULL }
};

static void ui_scale_changed(cvar_t *self)
{
    UI_Resize();
}

void UI_ModeChanged(void)
{
    ui_scale = Cvar_Get("ui_scale", "0", 0);
    ui_scale->changed = ui_scale_changed;
    UI_Resize();
}

static void UI_FreeMenus(void)
{
    menuFrameWork_t *menu, *next;

    LIST_FOR_EACH_SAFE(menuFrameWork_t, menu, next, &ui_menus, entry) {
        if (menu->free) {
            menu->free(menu);
        }
    }
    List_Init(&ui_menus);
}


/*
=================
UI_Init
=================
*/
void UI_Init(void)
{
    char buffer[MAX_QPATH];
    int i;

    Cmd_Register(c_ui);

    ui_debug = Cvar_Get("ui_debug", "0", 0);
    ui_open = Cvar_Get("ui_open", "1", 0);

    UI_ModeChanged();

    uis.fontHandle = R_RegisterFont("conchars");
    uis.cursorHandle = R_RegisterPic("ch1");
    R_GetPicSize(&uis.cursorWidth, &uis.cursorHeight, uis.cursorHandle);

    for (i = 0; i < NUM_CURSOR_FRAMES; i++) {
        Q_snprintf(buffer, sizeof(buffer), "m_cursor%d", i);
        uis.bitmapCursors[i] = R_RegisterPic(buffer);
    }

    uis.color.background.u32    = MakeColor(0,   0,   0, 255);
    uis.color.normal.u32        = MakeColor(15, 128, 235, 100);
    uis.color.active.u32        = MakeColor(15, 128, 235, 100);
    uis.color.selection.u32     = MakeColor(15, 128, 235, 100);
    uis.color.disabled.u32      = MakeColor(127, 127, 127, 255);

    strcpy(uis.weaponModel, "w_railgun.md2");

    // load custom menus
    UI_LoadScript();

    // load built-in menus
    M_Menu_PlayerConfig();
    M_Menu_Servers();
    M_Menu_Demos();

    Com_DPrintf("Registered %d menus.\n", List_Count(&ui_menus));

    uis.initialized = true;
}

/*
=================
UI_Shutdown
=================
*/
void UI_Shutdown(void)
{
    if (!uis.initialized) {
        return;
    }
    UI_ForceMenuOff();

    ui_scale->changed = NULL;

    PlayerModel_Free();

    UI_FreeMenus();

    Cmd_Deregister(c_ui);

    memset(&uis, 0, sizeof(uis));

    Z_LeakTest(TAG_UI);
}


