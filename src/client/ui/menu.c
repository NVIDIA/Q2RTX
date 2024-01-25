/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.

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
#include "server/server.h"

/*
===================================================================

ACTION CONTROL

===================================================================
*/

static void Action_Free(menuAction_t *a)
{
    Z_Free(a->generic.name);
    Z_Free(a->generic.status);
    Z_Free(a->cmd);
    Z_Free(a);
}

/*
=================
Action_Init
=================
*/
static void Action_Init(menuAction_t *a)
{
    Q_assert(a->generic.name);

    if ((a->generic.uiFlags & UI_CENTER) != UI_CENTER) {
        a->generic.x += RCOLUMN_OFFSET;
    }

    a->generic.rect.x = a->generic.x;
    a->generic.rect.y = a->generic.y;
    UI_StringDimensions(&a->generic.rect, a->generic.uiFlags, a->generic.name);
}


/*
=================
Action_Draw
=================
*/
static void Action_Draw(menuAction_t *a)
{
    int flags;

    flags = a->generic.uiFlags;
    if (a->generic.flags & QMF_HASFOCUS) {
        if ((a->generic.uiFlags & UI_CENTER) != UI_CENTER) {
            if ((uis.realtime >> 8) & 1) {
                UI_DrawChar(a->generic.x - RCOLUMN_OFFSET / 2, a->generic.y, a->generic.uiFlags | UI_RIGHT, 13);
            }
        } else {
            flags |= UI_ALTCOLOR;
            if ((uis.realtime >> 8) & 1) {
                UI_DrawChar(a->generic.x - strlen(a->generic.name) * CHAR_WIDTH / 2 - CHAR_WIDTH, a->generic.y, flags, 13);
            }
        }
    }

    if (a->generic.flags & QMF_GRAYED) {
        R_SetColor(uis.color.disabled.u32);
    }
    UI_DrawString(a->generic.x, a->generic.y, flags, a->generic.name);
    if (a->generic.flags & QMF_GRAYED) {
        R_ClearColor();
    }
}

/*
===================================================================

STATIC CONTROL

===================================================================
*/

/*
=================
Static_Init
=================
*/
static void Static_Init(menuStatic_t *s)
{
    Q_assert(s->generic.name);

    if (!s->maxChars) {
        s->maxChars = MAX_STRING_CHARS;
    }

    s->generic.rect.x = s->generic.x;
    s->generic.rect.y = s->generic.y;

    UI_StringDimensions(&s->generic.rect,
                        s->generic.uiFlags, s->generic.name);
}

/*
=================
Static_Draw
=================
*/
static void Static_Draw(menuStatic_t *s)
{
    if (s->generic.flags & QMF_CUSTOM_COLOR) {
        R_SetColor(s->generic.color.u32);
    }
    UI_DrawString(s->generic.x, s->generic.y, s->generic.uiFlags, s->generic.name);
    if (s->generic.flags & QMF_CUSTOM_COLOR) {
        R_ClearColor();
    }
}

/*
===================================================================

BITMAP CONTROL

===================================================================
*/

static void Bitmap_Free(menuBitmap_t *b)
{
    Z_Free(b->generic.status);
    Z_Free(b->cmd);
    Z_Free(b);
}

static void Bitmap_Init(menuBitmap_t *b)
{
    b->generic.rect.x = b->generic.x;
    b->generic.rect.y = b->generic.y;
    b->generic.rect.width = b->generic.width;
    b->generic.rect.height = b->generic.height;
}

static void Bitmap_Draw(menuBitmap_t *b)
{
    if (b->generic.flags & QMF_HASFOCUS) {
        unsigned frame = (uis.realtime / 100) % NUM_CURSOR_FRAMES;
        R_DrawPic(b->generic.x - CURSOR_OFFSET, b->generic.y, uis.bitmapCursors[frame]);
        R_DrawPic(b->generic.x, b->generic.y, b->pics[1]);
    } else {
        R_DrawPic(b->generic.x, b->generic.y, b->pics[0]);
    }
}

/*
===================================================================

KEYBIND CONTROL

===================================================================
*/

static void Keybind_Free(menuKeybind_t *k)
{
    Z_Free(k->generic.name);
    Z_Free(k->generic.status);
    Z_Free(k->cmd);
    Z_Free(k->altstatus);
    Z_Free(k);
}

/*
=================
Keybind_Init
=================
*/
static void Keybind_Init(menuKeybind_t *k)
{
    size_t len;

    Q_assert(k->generic.name);

    k->generic.uiFlags &= ~(UI_LEFT | UI_RIGHT);

    k->generic.rect.x = k->generic.x + LCOLUMN_OFFSET;
    k->generic.rect.y = k->generic.y;

    UI_StringDimensions(&k->generic.rect,
                        k->generic.uiFlags | UI_RIGHT, k->generic.name);

    if (k->altbinding[0]) {
        len = strlen(k->binding) + 4 + strlen(k->altbinding);
    } else if (k->binding[0]) {
        len = strlen(k->binding);
    } else {
        len = 3;
    }

    k->generic.rect.width += (RCOLUMN_OFFSET - LCOLUMN_OFFSET) + len * CHAR_WIDTH;
}

/*
=================
Keybind_Draw
=================
*/
static void Keybind_Draw(menuKeybind_t *k)
{
    char string[MAX_STRING_CHARS];
    int flags;

    flags = UI_ALTCOLOR;
    if (k->generic.flags & QMF_HASFOCUS) {
        /*if(k->generic.parent->keywait) {
            UI_DrawChar(k->generic.x + RCOLUMN_OFFSET / 2, k->generic.y, k->generic.uiFlags | UI_RIGHT, '=');
        } else*/ if ((uis.realtime >> 8) & 1) {
            UI_DrawChar(k->generic.x + RCOLUMN_OFFSET / 2, k->generic.y, k->generic.uiFlags | UI_RIGHT, 13);
        }
    } else {
        if (k->generic.parent->keywait) {
            R_SetColor(uis.color.disabled.u32);
            flags = 0;
        }
    }

    UI_DrawString(k->generic.x + LCOLUMN_OFFSET, k->generic.y,
                  k->generic.uiFlags | UI_RIGHT | flags, k->generic.name);

    if (k->altbinding[0]) {
        Q_concat(string, sizeof(string), k->binding, " or ", k->altbinding);
    } else if (k->binding[0]) {
        Q_strlcpy(string, k->binding, sizeof(string));
    } else {
        strcpy(string, "???");
    }

    UI_DrawString(k->generic.x + RCOLUMN_OFFSET, k->generic.y,
                  k->generic.uiFlags | UI_LEFT, string);

    R_ClearColor();
}

static void Keybind_Push(menuKeybind_t *k)
{
    int key = Key_EnumBindings(0, k->cmd);

    k->altbinding[0] = 0;
    if (key == -1) {
        strcpy(k->binding, "???");
    } else {
        Q_strlcpy(k->binding, Key_KeynumToString(key), sizeof(k->binding));
        key = Key_EnumBindings(key + 1, k->cmd);
        if (key != -1) {
            Q_strlcpy(k->altbinding, Key_KeynumToString(key), sizeof(k->altbinding));
        }
    }
}

static void Keybind_Pop(menuKeybind_t *k)
{
    Key_WaitKey(NULL, NULL);
}

static void Keybind_Update(menuFrameWork_t *menu)
{
    menuKeybind_t *k;
    int i;

    for (i = 0; i < menu->nitems; i++) {
        k = menu->items[i];
        if (k->generic.type == MTYPE_KEYBIND) {
            Keybind_Push(k);
            Keybind_Init(k);
        }
    }
}

static void Keybind_Remove(const char *cmd)
{
    int key;

    for (key = 0; ; key++) {
        key = Key_EnumBindings(key, cmd);
        if (key == -1) {
            break;
        }
        Key_SetBinding(key, NULL);
    }
}

static bool keybind_cb(void *arg, int key)
{
    menuKeybind_t *k = arg;
    menuFrameWork_t *menu = k->generic.parent;

    // console key is hardcoded
    if (key == '`') {
        UI_StartSound(QMS_BEEP);
        return false;
    }

    // menu key is hardcoded
    if (key != K_ESCAPE) {
        if (k->altbinding[0]) {
            Keybind_Remove(k->cmd);
        }
        Key_SetBinding(key, k->cmd);
    }

    Keybind_Update(menu);

    menu->keywait = false;
    menu->status = k->generic.status;
    Key_WaitKey(NULL, NULL);

    UI_StartSound(QMS_OUT);
    return false;
}

static menuSound_t Keybind_DoEnter(menuKeybind_t *k)
{
    menuFrameWork_t *menu = k->generic.parent;

    menu->keywait = true;
    menu->status = k->altstatus;
    Key_WaitKey(keybind_cb, k);
    return QMS_IN;
}

static menuSound_t Keybind_Key(menuKeybind_t *k, int key)
{
    menuFrameWork_t *menu = k->generic.parent;

    if (menu->keywait) {
        return QMS_OUT; // never gets there
    }

    if (key == K_BACKSPACE || key == K_DEL) {
        Keybind_Remove(k->cmd);
        Keybind_Update(menu);
        return QMS_IN;
    }

    return QMS_NOTHANDLED;
}


/*
===================================================================

FIELD CONTROL

===================================================================
*/

static void Field_Push(menuField_t *f)
{
    IF_Init(&f->field, f->width, f->width);
    IF_Replace(&f->field, f->cvar->string);
}

static void Field_Pop(menuField_t *f)
{
    Cvar_SetByVar(f->cvar, f->field.text, FROM_MENU);
}

static void Field_Free(menuField_t *f)
{
    Z_Free(f->generic.name);
    Z_Free(f->generic.status);
    Z_Free(f);
}

/*
=================
Field_Init
=================
*/
static void Field_Init(menuField_t *f)
{
    int w = f->width * CHAR_WIDTH;

    f->generic.uiFlags &= ~(UI_LEFT | UI_RIGHT);

    if (f->generic.name) {
        f->generic.rect.x = f->generic.x + LCOLUMN_OFFSET;
        f->generic.rect.y = f->generic.y;
        UI_StringDimensions(&f->generic.rect,
                            f->generic.uiFlags | UI_RIGHT, f->generic.name);
        f->generic.rect.width += (RCOLUMN_OFFSET - LCOLUMN_OFFSET) + w;
    } else {
        f->generic.rect.x = f->generic.x - w / 2;
        f->generic.rect.y = f->generic.y;
        f->generic.rect.width = w;
        f->generic.rect.height = CHAR_HEIGHT;
    }
}


/*
=================
Field_Draw
=================
*/
static void Field_Draw(menuField_t *f)
{
    int flags = f->generic.uiFlags;
    uint32_t color = uis.color.normal.u32;

    if (f->generic.flags & QMF_HASFOCUS) {
        flags |= UI_DRAWCURSOR;
        color = uis.color.active.u32;
    }

    if (f->generic.name) {
        UI_DrawString(f->generic.x + LCOLUMN_OFFSET, f->generic.y,
                      f->generic.uiFlags | UI_RIGHT | UI_ALTCOLOR, f->generic.name);

        R_DrawFill32(f->generic.x + RCOLUMN_OFFSET, f->generic.y - 1,
                     f->field.visibleChars * CHAR_WIDTH, CHAR_HEIGHT + 2, color);

        IF_Draw(&f->field, f->generic.x + RCOLUMN_OFFSET, f->generic.y,
                flags, uis.fontHandle);
    } else {
        R_DrawFill32(f->generic.rect.x, f->generic.rect.y - 1,
                     f->generic.rect.width, CHAR_HEIGHT + 2, color);

        IF_Draw(&f->field, f->generic.rect.x, f->generic.rect.y,
                flags, uis.fontHandle);
    }
}

static bool Field_TestKey(menuField_t *f, int key)
{
    if (f->generic.flags & QMF_NUMBERSONLY) {
        return Q_isdigit(key) || key == '+' || key == '-' || key == '.';
    }

    return Q_isprint(key);
}

/*
=================
Field_Key
=================
*/
static int Field_Key(menuField_t *f, int key)
{
    if (IF_KeyEvent(&f->field, key)) {
        return QMS_SILENT;
    }

    if (Field_TestKey(f, key)) {
        return QMS_SILENT;
    }

    return QMS_NOTHANDLED;
}

/*
=================
Field_Char
=================
*/
static int Field_Char(menuField_t *f, int key)
{
    bool ret;

    if (!Field_TestKey(f, key)) {
        return QMS_BEEP;
    }

    ret = IF_CharEvent(&f->field, key);
    if (f->generic.change) {
        f->generic.change(&f->generic);
    }

    return ret ? QMS_SILENT : QMS_NOTHANDLED;
}

/*
===================================================================

SPIN CONTROL

===================================================================
*/

static void SpinControl_Push(menuSpinControl_t *s)
{
    int val = s->cvar->integer;

    if (val < 0 || val >= s->numItems)
        s->curvalue = -1;
    else
        s->curvalue = val;
}

static void SpinControl_Pop(menuSpinControl_t *s)
{
    if (s->curvalue >= 0 && s->curvalue < s->numItems)
        Cvar_SetInteger(s->cvar, s->curvalue, FROM_MENU);
}

static void SpinControl_Free(menuSpinControl_t *s)
{
    int i;

    Z_Free(s->generic.name);
    Z_Free(s->generic.status);
    for (i = 0; i < s->numItems; i++) {
        Z_Free(s->itemnames[i]);
    }
    Z_Free(s->itemnames);
    Z_Free(s);
}


/*
=================
SpinControl_Init
=================
*/
void SpinControl_Init(menuSpinControl_t *s)
{
    char **n;
    int    maxLength, length;

    s->generic.uiFlags &= ~(UI_LEFT | UI_RIGHT);

    s->generic.rect.x = s->generic.x + LCOLUMN_OFFSET;
    s->generic.rect.y = s->generic.y;

    UI_StringDimensions(&s->generic.rect,
                        s->generic.uiFlags | UI_RIGHT, s->generic.name);

    maxLength = 0;
    s->numItems = 0;
    n = s->itemnames;
    while (*n) {
        length = strlen(*n);

        if (maxLength < length) {
            maxLength = length;
        }
        s->numItems++;
        n++;
    }

    s->generic.rect.width += (RCOLUMN_OFFSET - LCOLUMN_OFFSET) +
                             maxLength * CHAR_WIDTH;
}

/*
=================
SpinControl_DoEnter
=================
*/
static int SpinControl_DoEnter(menuSpinControl_t *s)
{
    if (!s->numItems)
        return QMS_BEEP;

    s->curvalue++;

    if (s->curvalue >= s->numItems)
        s->curvalue = 0;

    if (s->generic.change) {
        s->generic.change(&s->generic);
    }

    return QMS_MOVE;
}

/*
=================
SpinControl_DoSlide
=================
*/
static int SpinControl_DoSlide(menuSpinControl_t *s, int dir)
{
    if (!s->numItems)
        return QMS_BEEP;

    s->curvalue += dir;

    if (s->curvalue < 0) {
        s->curvalue = s->numItems - 1;
    } else if (s->curvalue >= s->numItems) {
        s->curvalue = 0;
    }

    if (s->generic.change) {
        s->generic.change(&s->generic);
    }

    return QMS_MOVE;
}

/*
=================
SpinControl_Draw
=================
*/
static void SpinControl_Draw(menuSpinControl_t *s)
{
    const char *name;

    UI_DrawString(s->generic.x + LCOLUMN_OFFSET, s->generic.y,
                  s->generic.uiFlags | UI_RIGHT | UI_ALTCOLOR, s->generic.name);

    if (s->generic.flags & QMF_HASFOCUS) {
        if ((uis.realtime >> 8) & 1) {
            UI_DrawChar(s->generic.x + RCOLUMN_OFFSET / 2, s->generic.y,
                        s->generic.uiFlags | UI_RIGHT, 13);
        }
    }

    if (s->curvalue < 0 || s->curvalue >= s->numItems)
        name = "???";
    else
        name = s->itemnames[s->curvalue];

    UI_DrawString(s->generic.x + RCOLUMN_OFFSET, s->generic.y,
                  s->generic.uiFlags, name);
}

/*
===================================================================

BITFIELD CONTROL

===================================================================
*/

static void BitField_Push(menuSpinControl_t *s)
{
    if (s->cvar->integer & s->mask) {
        s->curvalue = 1 ^ s->negate;
    } else {
        s->curvalue = 0 ^ s->negate;
    }
}

static void BitField_Pop(menuSpinControl_t *s)
{
    int val = s->cvar->integer;

    if (s->curvalue ^ s->negate) {
        val |= s->mask;
    } else {
        val &= ~s->mask;
    }
    Cvar_SetInteger(s->cvar, val, FROM_MENU);
}

static void BitField_Free(menuSpinControl_t *s)
{
    Z_Free(s->generic.name);
    Z_Free(s->generic.status);
    Z_Free(s);
}

/*
===================================================================

PAIRS CONTROL

===================================================================
*/

static void Pairs_Push(menuSpinControl_t *s)
{
    int i;

    for (i = 0; i < s->numItems; i++) {
        if (!Q_stricmp(s->itemvalues[i], s->cvar->string)) {
            s->curvalue = i;
            return;
        }
    }

    s->curvalue = -1;
}

static void Pairs_Pop(menuSpinControl_t *s)
{
    if (s->curvalue >= 0 && s->curvalue < s->numItems)
        Cvar_SetByVar(s->cvar, s->itemvalues[s->curvalue], FROM_MENU);
}

static void Pairs_Free(menuSpinControl_t *s)
{
    int i;

    Z_Free(s->generic.name);
    Z_Free(s->generic.status);
    for (i = 0; i < s->numItems; i++) {
        Z_Free(s->itemnames[i]);
        Z_Free(s->itemvalues[i]);
    }
    Z_Free(s->itemnames);
    Z_Free(s->itemvalues);
    Z_Free(s);
}

/*
===================================================================

STRINGS CONTROL

===================================================================
*/

static void Strings_Push(menuSpinControl_t *s)
{
    int i;

    for (i = 0; i < s->numItems; i++) {
        if (!Q_stricmp(s->itemnames[i], s->cvar->string)) {
            s->curvalue = i;
            return;
        }
    }

    s->curvalue = -1;
}

static void Strings_Pop(menuSpinControl_t *s)
{
    if (s->curvalue >= 0 && s->curvalue < s->numItems)
        Cvar_SetByVar(s->cvar, s->itemnames[s->curvalue], FROM_MENU);
}

/*
===================================================================

TOGGLE CONTROL

===================================================================
*/

static void Toggle_Push(menuSpinControl_t *s)
{
    int val = s->cvar->integer;

    if (val == 0 || val == 1)
        s->curvalue = val ^ s->negate;
    else
        s->curvalue = -1;
}

static void Toggle_Pop(menuSpinControl_t *s)
{
    if (s->curvalue == 0 || s->curvalue == 1)
        Cvar_SetInteger(s->cvar, s->curvalue ^ s->negate, FROM_MENU);
}

/*
===================================================================

LIST CONTROL

===================================================================
*/

/*
=================
MenuList_ValidatePrestep
=================
*/
static void MenuList_ValidatePrestep(menuList_t *l)
{
    if (l->prestep > l->numItems - l->maxItems) {
        l->prestep = l->numItems - l->maxItems;
    }
    if (l->prestep < 0) {
        l->prestep = 0;
    }
}

static void MenuList_AdjustPrestep(menuList_t *l)
{
    if (l->numItems > l->maxItems && l->curvalue > 0) {
        if (l->prestep > l->curvalue) {
            l->prestep = l->curvalue;
        } else if (l->prestep < l->curvalue - l->maxItems + 1) {
            l->prestep = l->curvalue - l->maxItems + 1;
        }
    } else {
        l->prestep = 0;
    }
}

/*
=================
MenuList_Init
=================
*/
void MenuList_Init(menuList_t *l)
{
    int        height;
    int        i;

    height = l->generic.height;
    if (l->mlFlags & MLF_HEADER) {
        height -= MLIST_SPACING;
    }

    l->maxItems = height / MLIST_SPACING;

    //clamp(l->curvalue, 0, l->numItems - 1);

    MenuList_ValidatePrestep(l);

    l->generic.rect.x = l->generic.x;
    l->generic.rect.y = l->generic.y;

    l->generic.rect.width = 0;
    for (i = 0; i < l->numcolumns; i++) {
        l->generic.rect.width += l->columns[i].width;
    }

    if (l->mlFlags & MLF_SCROLLBAR) {
        l->generic.rect.width += MLIST_SCROLLBAR_WIDTH;
    }

    l->generic.rect.height = l->generic.height;

    if (l->sortdir && l->sort) {
        l->sort(l);
    }
}

/*
=================
MenuList_SetValue
=================
*/
void MenuList_SetValue(menuList_t *l, int value)
{
    if (value > l->numItems - 1)
        value = l->numItems - 1;
    if (value < 0)
        value = 0;

    if (value != l->curvalue) {
        l->curvalue = value;
        if (l->generic.change) {
            l->generic.change(&l->generic);
        }
    }

    MenuList_AdjustPrestep(l);
}

static menuSound_t MenuList_SetColumn(menuList_t *l, int value)
{
    if (l->sortcol == value) {
        l->sortdir = -l->sortdir;
    } else {
        l->sortcol = value;
        l->sortdir = 1;
    }
    if (l->sort) {
        l->sort(l);
    }
    return QMS_SILENT;
}

// finds a visible column by number, with numeration starting at 1
static menuSound_t MenuList_FindColumn(menuList_t *l, int rel)
{
    int i, j;

    if (!l->sortdir)
        return QMS_NOTHANDLED;

    for (i = 0, j = 0; i < l->numcolumns; i++) {
        if (!l->columns[i].width)
            continue;

        if (++j == rel)
            return MenuList_SetColumn(l, i);
    }

    return QMS_NOTHANDLED;
}

static menuSound_t MenuList_PrevColumn(menuList_t *l)
{
    int col;

    if (!l->sortdir || !l->numcolumns) {
        return QMS_NOTHANDLED;
    }

    col = l->sortcol;
    if (col < 0)
        return MenuList_FindColumn(l, 1);

    do {
        if (col < 0) {
            col = l->numcolumns - 1;
        } else {
            col--;
        }
        if (col == l->sortcol) {
            return QMS_SILENT;
        }
    } while (!l->columns[col].width);

    return MenuList_SetColumn(l, col);
}

static menuSound_t MenuList_NextColumn(menuList_t *l)
{
    int col;

    if (!l->sortdir || !l->numcolumns) {
        return QMS_NOTHANDLED;
    }

    col = l->sortcol;
    if (col < 0)
        return MenuList_FindColumn(l, 1);

    do {
        if (col == l->numcolumns - 1) {
            col = 0;
        } else {
            col++;
        }
        if (col == l->sortcol) {
            return QMS_SILENT;
        }
    } while (!l->columns[col].width);

    return MenuList_SetColumn(l, col);
}

/*
=================
MenuList_Click
=================
*/
static menuSound_t MenuList_Click(menuList_t *l)
{
    int i, j;
    vrect_t rect;

    if (!l->items) {
        return QMS_SILENT;
    }

    // click on scroll bar
    if ((l->mlFlags & MLF_SCROLLBAR) && l->numItems > l->maxItems) {
        int x = l->generic.rect.x + l->generic.rect.width - MLIST_SCROLLBAR_WIDTH;
        int y = l->generic.rect.y + MLIST_SPACING;
        int h = l->generic.height;
        int barHeight, pageHeight, prestepHeight;
        float pageFrac, prestepFrac;

        if (l->mlFlags & MLF_HEADER) {
            y += MLIST_SPACING;
            h -= MLIST_SPACING;
        }

        barHeight = h - MLIST_SPACING * 2;
        pageFrac = (float)l->maxItems / l->numItems;
        prestepFrac = (float)l->prestep / l->numItems;

        pageHeight = Q_rint(barHeight * pageFrac);
        prestepHeight = Q_rint(barHeight * prestepFrac);

        // click above thumb
        rect.x = x;
        rect.y = y;
        rect.width = MLIST_SCROLLBAR_WIDTH;
        rect.height = prestepHeight;
        if (UI_CursorInRect(&rect)) {
            l->prestep -= l->maxItems;
            MenuList_ValidatePrestep(l);
            return QMS_MOVE;
        }

        // click on thumb
        rect.y = y + prestepHeight;
        rect.height = pageHeight;
        if (UI_CursorInRect(&rect)) {
            l->drag_y = uis.mouseCoords[1] - rect.y;
            uis.mouseTracker = &l->generic;
            return QMS_SILENT;
        }

        // click below thumb
        rect.y = y + prestepHeight + pageHeight;
        rect.height = barHeight - prestepHeight - pageHeight;
        if (UI_CursorInRect(&rect)) {
            l->prestep += l->maxItems;
            MenuList_ValidatePrestep(l);
            return QMS_MOVE;
        }

        // click above scrollbar
        rect.y = y - MLIST_SPACING;
        rect.height = MLIST_SPACING;
        if (UI_CursorInRect(&rect)) {
            l->prestep--;
            MenuList_ValidatePrestep(l);
            return QMS_MOVE;
        }

        // click below scrollbar
        rect.y = l->generic.rect.y + l->generic.height - MLIST_SPACING;
        rect.height = MLIST_SPACING;
        if (UI_CursorInRect(&rect)) {
            l->prestep++;
            MenuList_ValidatePrestep(l);
            return QMS_MOVE;
        }
    }

    rect.x = l->generic.rect.x;
    rect.y = l->generic.rect.y;
    rect.width = l->generic.rect.width;
    rect.height = MLIST_SPACING;

    if (l->mlFlags & MLF_SCROLLBAR) {
        rect.width -= MLIST_SCROLLBAR_WIDTH;
    }

    // click on header
    if (l->mlFlags & MLF_HEADER) {
        if (l->sortdir && UI_CursorInRect(&rect)) {
            for (j = 0; j < l->numcolumns; j++) {
                if (!l->columns[j].width) {
                    continue;
                }
                rect.width = l->columns[j].width;
                if (UI_CursorInRect(&rect)) {
                    return MenuList_SetColumn(l, j);
                }
                rect.x += rect.width;
            }
            return QMS_SILENT;
        }
        rect.y += MLIST_SPACING;
    }

    // click on item
    j = min(l->numItems, l->prestep + l->maxItems);
    for (i = l->prestep; i < j; i++) {
        if (UI_CursorInRect(&rect)) {
            if (l->curvalue == i && uis.realtime -
                l->clickTime < DOUBLE_CLICK_DELAY) {
                if (l->generic.activate) {
                    return l->generic.activate(&l->generic);
                }
                return QMS_SILENT;
            }
            l->clickTime = uis.realtime;
            l->curvalue = i;
            if (l->generic.change) {
                return l->generic.change(&l->generic);
            }
            return QMS_SILENT;
        }
        rect.y += MLIST_SPACING;
    }

    return QMS_SILENT;
}

/*
=================
MenuList_Key
=================
*/
static menuSound_t MenuList_Key(menuList_t *l, int key)
{
    //int i;

    if (!l->items) {
        return QMS_NOTHANDLED;
    }

    if (Key_IsDown(K_ALT) && Q_isdigit(key)) {
        return MenuList_FindColumn(l, key - '0');
    }

#if 0
    if (key > 32 && key < 127) {
        if (uis.realtime > l->scratchTime + 1300) {
            l->scratchCount = 0;
            l->scratchTime = uis.realtime;
        }

        if (l->scratchCount >= sizeof(l->scratch) - 1) {
            return QMS_NOTHANDLED;
        }

        l->scratch[l->scratchCount++] = key;
        l->scratch[l->scratchCount] = 0;

        //l->scratchTime = uis.realtime;

        if (!Q_stricmpn(UI_GetColumn((char *)l->items[l->curvalue] + l->extrasize, l->sortcol),
                        l->scratch, l->scratchCount)) {
            return QMS_NOTHANDLED;
        }

        for (i = 0; i < l->numItems; i++) {
            if (!Q_stricmpn(UI_GetColumn((char *)l->items[i] + l->extrasize, l->sortcol), l->scratch, l->scratchCount)) {
                MenuList_SetValue(l, i);
                return QMS_SILENT;
            }
            i++;
        }

        return QMS_NOTHANDLED;
    }

    l->scratchCount = 0;
#endif

    switch (key) {
    case K_LEFTARROW:
    case 'h':
        return MenuList_PrevColumn(l);

    case K_RIGHTARROW:
    case 'l':
        return MenuList_NextColumn(l);

    case K_UPARROW:
    case K_KP_UPARROW:
    case 'k':
        if (l->curvalue < 0) {
            goto home;
        }
        if (l->curvalue > 0) {
            l->curvalue--;
            if (l->generic.change) {
                l->generic.change(&l->generic);
            }
            MenuList_AdjustPrestep(l);
            return QMS_MOVE;
        }
        return QMS_BEEP;

    case K_DOWNARROW:
    case K_KP_DOWNARROW:
    case 'j':
        if (l->curvalue < 0) {
            goto home;
        }
        if (l->curvalue < l->numItems - 1) {
            l->curvalue++;
            if (l->generic.change) {
                l->generic.change(&l->generic);
            }
            MenuList_AdjustPrestep(l);
            return QMS_MOVE;
        }
        return QMS_BEEP;

    case K_HOME:
    case K_KP_HOME:
    home:
        l->prestep = 0;
        l->curvalue = 0;
        if (l->generic.change) {
            l->generic.change(&l->generic);
        }
        return QMS_MOVE;

    case K_END:
    case K_KP_END:
        if (!l->numItems) {
            goto home;
        }
        if (l->numItems > l->maxItems) {
            l->prestep = l->numItems - l->maxItems;
        }
        l->curvalue = l->numItems - 1;
        if (l->generic.change) {
            l->generic.change(&l->generic);
        }
        return QMS_MOVE;

    case K_MWHEELUP:
        if (Key_IsDown(K_CTRL)) {
            l->prestep -= 4;
        } else {
            l->prestep -= 2;
        }
        MenuList_ValidatePrestep(l);
        return QMS_SILENT;

    case K_MWHEELDOWN:
        if (Key_IsDown(K_CTRL)) {
            l->prestep += 4;
        } else {
            l->prestep += 2;
        }
        MenuList_ValidatePrestep(l);
        return QMS_SILENT;

    case K_PGUP:
    case K_KP_PGUP:
        if (l->curvalue < 0) {
            goto home;
        }
        if (l->curvalue > 0) {
            l->curvalue -= l->maxItems - 1;
            if (l->curvalue < 0) {
                l->curvalue = 0;
            }
            if (l->generic.change) {
                l->generic.change(&l->generic);
            }
            MenuList_AdjustPrestep(l);
            return QMS_MOVE;
        }
        return QMS_BEEP;

    case K_PGDN:
    case K_KP_PGDN:
        if (l->curvalue < 0) {
            goto home;
        }
        if (l->curvalue < l->numItems - 1) {
            l->curvalue += l->maxItems - 1;
            if (l->curvalue > l->numItems - 1) {
                l->curvalue = l->numItems - 1;
            }
            if (l->generic.change) {
                l->generic.change(&l->generic);
            }
            MenuList_AdjustPrestep(l);
            return QMS_MOVE;
        }
        return QMS_BEEP;

    case K_MOUSE1:
    //case K_MOUSE2:
    //case K_MOUSE3:
        return MenuList_Click(l);
    }

    return QMS_NOTHANDLED;
}

static menuSound_t MenuList_MouseMove(menuList_t *l)
{
    int y, h, barHeight;

    if (uis.mouseTracker != &l->generic)
        return QMS_NOTHANDLED;

    y = l->generic.y + MLIST_SPACING;
    h = l->generic.height;

    if (l->mlFlags & MLF_HEADER) {
        y += MLIST_SPACING;
        h -= MLIST_SPACING;
    }

    barHeight = h - MLIST_SPACING * 2;
    if (barHeight > 0) {
        l->prestep = (uis.mouseCoords[1] - y - l->drag_y) * l->numItems / barHeight;
        MenuList_ValidatePrestep(l);
    }

    return QMS_SILENT;
}

/*
=================
MenuList_DrawString
=================
*/
static void MenuList_DrawString(int x, int y, int flags,
                                menuListColumn_t *column,
                                const char *string)
{
    clipRect_t rc;

    rc.left = x;
    rc.right = x + column->width - 1;
    rc.top = y + 1;
    rc.bottom = y + CHAR_HEIGHT + 1;

    if ((column->uiFlags & UI_CENTER) == UI_CENTER) {
        x += column->width / 2 - 1;
    } else if (column->uiFlags & UI_RIGHT) {
        x += column->width - MLIST_PRESTEP;
    } else {
        x += MLIST_PRESTEP;
    }

    R_SetClipRect(&rc);
    UI_DrawString(x, y + 1, column->uiFlags | flags, string);
    R_SetClipRect(NULL);
}

/*
=================
MenuList_Draw
=================
*/
static void MenuList_Draw(menuList_t *l)
{
    char *s;
    int x, y, xx, yy;
    int i, j, k;
    int width, height;
    float pageFrac, prestepFrac;
    int barHeight;

    x = l->generic.rect.x;
    y = l->generic.rect.y;
    width = l->generic.rect.width;
    height = l->generic.rect.height;

    // draw header
    if (l->mlFlags & MLF_HEADER) {
        xx = x;
        for (j = 0; j < l->numcolumns; j++) {
            int flags = UI_ALTCOLOR;
            uint32_t color = uis.color.normal.u32;

            if (!l->columns[j].width) {
                continue;
            }

            if (l->sortcol == j && l->sortdir) {
                flags = 0;
                if (l->generic.flags & QMF_HASFOCUS) {
                    color = uis.color.active.u32;
                }
            }
            R_DrawFill32(xx, y, l->columns[j].width - 1,
                         MLIST_SPACING - 1, color);

            if (l->columns[j].name) {
                MenuList_DrawString(xx, y, flags,
                                    &l->columns[j], l->columns[j].name);
            }
            xx += l->columns[j].width;
        }
        y += MLIST_SPACING;
        height -= MLIST_SPACING;
    }

    if (l->mlFlags & MLF_SCROLLBAR) {
        barHeight = height - MLIST_SPACING * 2;
        yy = y + MLIST_SPACING;

        // draw scrollbar background
        R_DrawFill32(x + width - MLIST_SCROLLBAR_WIDTH, yy,
                     MLIST_SCROLLBAR_WIDTH - 1, barHeight,
                     uis.color.normal.u32);

        if (l->numItems > l->maxItems) {
            pageFrac = (float)l->maxItems / l->numItems;
            prestepFrac = (float)l->prestep / l->numItems;
        } else {
            pageFrac = 1;
            prestepFrac = 0;
        }

        // draw scrollbar thumb
        R_DrawFill32(x + width - MLIST_SCROLLBAR_WIDTH,
                     yy + Q_rint(barHeight * prestepFrac),
                     MLIST_SCROLLBAR_WIDTH - 1,
                     Q_rint(barHeight * pageFrac),
                     uis.color.selection.u32);
    }

    // draw background
    xx = x;
    for (j = 0; j < l->numcolumns; j++) {
        uint32_t color = uis.color.normal.u32;

        if (!l->columns[j].width) {
            continue;
        }

        if (l->sortcol == j && l->sortdir) {
            if (l->generic.flags & QMF_HASFOCUS) {
                color = uis.color.active.u32;
            }
        }
        R_DrawFill32(xx, y, l->columns[j].width - 1,
                     height, color);

        xx += l->columns[j].width;
    }

    yy = y;
    k = min(l->numItems, l->prestep + l->maxItems);
    for (i = l->prestep; i < k; i++) {
        // draw selection
        if (!(l->generic.flags & QMF_DISABLED) && i == l->curvalue) {
            xx = x;
            for (j = 0; j < l->numcolumns; j++) {
                if (!l->columns[j].width) {
                    continue;
                }
                R_DrawFill32(xx, yy, l->columns[j].width - 1,
                             MLIST_SPACING, uis.color.selection.u32);
                xx += l->columns[j].width;
            }
        }

        // draw contents
        s = (char *)l->items[i] + l->extrasize;
        if (l->mlFlags & MLF_COLOR) {
            R_SetColor(*((uint32_t *)(s - 4)));
        }

        xx = x;
        for (j = 0; j < l->numcolumns; j++) {
            if (!*s) {
                break;
            }

            if (l->columns[j].width) {
                MenuList_DrawString(xx, yy, 0, &l->columns[j], s);
                xx += l->columns[j].width;
            }
            s += strlen(s) + 1;
        }

        yy += MLIST_SPACING;
    }

    if (l->mlFlags & MLF_COLOR) {
        R_SetColor(U32_WHITE);
    }
}

void MenuList_Sort(menuList_t *l, int offset, int (*cmpfunc)(const void *, const void *))
{
    void *n;
    int i;

    if (!l->items)
        return;

    if (offset >= l->numItems)
        return;

    if (l->sortcol < 0 || l->sortcol >= l->numcolumns)
        return;

    if (l->curvalue < 0 || l->curvalue >= l->numItems)
        n = NULL;
    else
        n = l->items[l->curvalue];

    qsort(l->items + offset, l->numItems - offset, sizeof(char *), cmpfunc);

    for (i = 0; i < l->numItems; i++) {
        if (l->items[i] == n) {
            l->curvalue = i;
            break;
        }
    }

    if (n)
        MenuList_AdjustPrestep(l);
}

/*
===================================================================

SLIDER CONTROL

===================================================================
*/

static menuSound_t Slider_DoSlide(menuSlider_t *s, int dir);

static void Slider_Push(menuSlider_t *s)
{
    s->modified = false;
    s->curvalue = Q_circ_clipf(s->cvar->value, s->minvalue, s->maxvalue);
}

static void Slider_Pop(menuSlider_t *s)
{
    if (s->modified) {
        float val = Q_circ_clipf(s->curvalue, s->minvalue, s->maxvalue);
        Cvar_SetValue(s->cvar, val, FROM_MENU);
    }
}

static void Slider_Free(menuSlider_t *s)
{
    Z_Free(s->generic.name);
    Z_Free(s->generic.status);
    Z_Free(s->format);
    Z_Free(s);
}

static void Slider_Init(menuSlider_t *s)
{
    int len = strlen(s->generic.name) * CHAR_WIDTH;

    s->generic.rect.x = s->generic.x + LCOLUMN_OFFSET - len;
    s->generic.rect.y = s->generic.y;

    s->generic.rect.width = (RCOLUMN_OFFSET - LCOLUMN_OFFSET) +
                            len + (SLIDER_RANGE + 2) * CHAR_WIDTH;
    s->generic.rect.height = CHAR_HEIGHT;
}

static menuSound_t Slider_Click(menuSlider_t *s)
{
    vrect_t rect;
    float   pos;
    int     x;

    pos = Q_clipf((s->curvalue - s->minvalue) / (s->maxvalue - s->minvalue), 0, 1);

    x = CHAR_WIDTH + (SLIDER_RANGE - 1) * CHAR_WIDTH * pos;

    // click left of thumb
    rect.x = s->generic.x + RCOLUMN_OFFSET;
    rect.y = s->generic.y;
    rect.width = x;
    rect.height = CHAR_HEIGHT;
    if (UI_CursorInRect(&rect))
        return Slider_DoSlide(s, -1);

    // click on thumb
    rect.x = s->generic.x + RCOLUMN_OFFSET + x;
    rect.y = s->generic.y;
    rect.width = CHAR_WIDTH;
    rect.height = CHAR_HEIGHT;
    if (UI_CursorInRect(&rect)) {
        uis.mouseTracker = &s->generic;
        return QMS_SILENT;
    }

    // click right of thumb
    rect.x = s->generic.x + RCOLUMN_OFFSET + x + CHAR_WIDTH;
    rect.y = s->generic.y;
    rect.width = (SLIDER_RANGE + 1) * CHAR_WIDTH - x;
    rect.height = CHAR_HEIGHT;
    if (UI_CursorInRect(&rect))
        return Slider_DoSlide(s, 1);

    return QMS_SILENT;
}

static menuSound_t Slider_MouseMove(menuSlider_t *s)
{
    float   pos, value;
    int     steps;

    if (uis.mouseTracker != &s->generic)
        return QMS_NOTHANDLED;

    pos = (uis.mouseCoords[0] - (s->generic.x + RCOLUMN_OFFSET + CHAR_WIDTH)) * (1.0f / (SLIDER_RANGE * CHAR_WIDTH));

    value = Q_clipf(pos, 0, 1) * (s->maxvalue - s->minvalue);
    steps = Q_rint(value / s->step);

    s->modified = true;
    s->curvalue = s->minvalue + steps * s->step;
    return QMS_SILENT;
}

static menuSound_t Slider_Key(menuSlider_t *s, int key)
{
    switch (key) {
    case K_END:
        s->modified = true;
        s->curvalue = s->maxvalue;
        return QMS_MOVE;
    case K_HOME:
        s->modified = true;
        s->curvalue = s->minvalue;
        return QMS_MOVE;
    case K_MOUSE1:
        return Slider_Click(s);
    }

    return QMS_NOTHANDLED;
}


/*
=================
Slider_DoSlide
=================
*/
static menuSound_t Slider_DoSlide(menuSlider_t *s, int dir)
{
    s->modified = true;
    s->curvalue = Q_circ_clipf(s->curvalue + dir * s->step, s->minvalue, s->maxvalue);

    if (s->generic.change) {
        menuSound_t sound = s->generic.change(&s->generic);
        if (sound != QMS_NOTHANDLED) {
            return sound;
        }
    }

    return QMS_SILENT;
}

/*
=================
Slider_Draw
=================
*/
static void Slider_Draw(menuSlider_t *s)
{
    int     i, flags;
    float   pos;

    flags = s->generic.uiFlags & ~(UI_LEFT | UI_RIGHT);

    if (s->generic.flags & QMF_HASFOCUS) {
        if ((uis.realtime >> 8) & 1) {
            UI_DrawChar(s->generic.x + RCOLUMN_OFFSET / 2, s->generic.y, s->generic.uiFlags | UI_RIGHT, 13);
        }
    }

    UI_DrawString(s->generic.x + LCOLUMN_OFFSET, s->generic.y,
                  flags | UI_RIGHT | UI_ALTCOLOR, s->generic.name);

    UI_DrawChar(s->generic.x + RCOLUMN_OFFSET, s->generic.y, flags | UI_LEFT, 128);

    for (i = 0; i < SLIDER_RANGE; i++)
        UI_DrawChar(RCOLUMN_OFFSET + s->generic.x + i * CHAR_WIDTH + CHAR_WIDTH, s->generic.y, flags | UI_LEFT, 129);

    UI_DrawChar(RCOLUMN_OFFSET + s->generic.x + i * CHAR_WIDTH + CHAR_WIDTH, s->generic.y, flags | UI_LEFT, 130);

    pos = Q_clipf((s->curvalue - s->minvalue) / (s->maxvalue - s->minvalue), 0, 1);

    UI_DrawChar(CHAR_WIDTH + RCOLUMN_OFFSET + s->generic.x + (SLIDER_RANGE - 1) * CHAR_WIDTH * pos, s->generic.y, flags | UI_LEFT, 131);

	float display_value = s->curvalue;
	if (s->percentage)
		display_value *= 100.f;

	char sbuf[16];
    if (s->format)
        snprintf(sbuf, sizeof(sbuf), s->format, display_value);
    else
	    snprintf(sbuf, sizeof(sbuf), "%.1f", display_value);

	UI_DrawString(s->generic.x + RCOLUMN_OFFSET + CHAR_WIDTH * (SLIDER_RANGE + 3), s->generic.y, flags | UI_LEFT, sbuf);
}

/*
===================================================================

SEPARATOR CONTROL

===================================================================
*/

/*
=================
Separator_Init
=================
*/
static void Separator_Init(menuSeparator_t *s)
{
    s->generic.rect.x = s->generic.rect.y = 999999;
    s->generic.rect.width = s->generic.rect.height = -999999;
}

/*
=================
Separator_Draw
=================
*/
static void Separator_Draw(menuSeparator_t *s)
{
    if (s->generic.name)
        UI_DrawString(s->generic.x, s->generic.y, UI_RIGHT, s->generic.name);
}

/*
===================================================================

SAVEGAME CONTROL

===================================================================
*/

static void Savegame_Push(menuAction_t *a)
{
    char *info;

    Z_Free(a->generic.name);

    info = SV_GetSaveInfo(a->cmd);
    if (info) {
        a->generic.name = info;
        a->generic.flags &= ~QMF_GRAYED;
    } else {
        a->generic.name = UI_CopyString("<EMPTY>");
        if (a->generic.type == MTYPE_LOADGAME)
            a->generic.flags |= QMF_GRAYED;
    }

    bool is_auto_save = strcmp(a->cmd, "save0") == 0;
    bool is_quick_save = strcmp(a->cmd, "quick") == 0;
    if (is_auto_save || is_quick_save)
    {
        char *new_name = va("%s - %s", a->generic.name, is_auto_save ? "Auto Save" : "Quick Save");
        Z_Free(a->generic.name);
        a->generic.name = Z_CopyString(new_name);
    }

    UI_StringDimensions(&a->generic.rect, a->generic.uiFlags, a->generic.name);
}

/*
===================================================================

MISC

===================================================================
*/

/*
=================
Common_DoEnter
=================
*/
static int Common_DoEnter(menuCommon_t *item)
{
    if (item->activate) {
        menuSound_t sound = item->activate(item);
        if (sound != QMS_NOTHANDLED) {
            return sound;
        }
    }

    return QMS_IN;
}


/*
=================
Menu_AddItem
=================
*/
void Menu_AddItem(menuFrameWork_t *menu, void *item)
{
    Q_assert(menu->nitems < MAX_MENU_ITEMS);

    if (!menu->nitems) {
        menu->items = UI_Malloc(MIN_MENU_ITEMS * sizeof(void *));
    } else {
        menu->items = Z_Realloc(menu->items, ALIGN(menu->nitems + 1, MIN_MENU_ITEMS) * sizeof(void *));
    }

    menu->items[menu->nitems++] = item;
    ((menuCommon_t *)item)->parent = menu;
	((menuCommon_t *)item)->condition = menu->current_condition;
}

static void UI_ClearBounds(int mins[2], int maxs[2])
{
    mins[0] = mins[1] = 9999;
    maxs[0] = maxs[1] = -9999;
}

static void UI_AddRectToBounds(const vrect_t *rc, int mins[2], int maxs[2])
{
    if (mins[0] > rc->x) {
        mins[0] = rc->x;
    } else if (maxs[0] < rc->x + rc->width) {
        maxs[0] = rc->x + rc->width;
    }

    if (mins[1] > rc->y) {
        mins[1] = rc->y;
    } else if (maxs[1] < rc->y + rc->height) {
        maxs[1] = rc->y + rc->height;
    }
}

void Menu_Init(menuFrameWork_t *menu)
{
    void *item;
    int i;
    int focus = 0;
    vrect_t *rc;

    menu->y1 = 0;
    menu->y2 = uis.height;

	for (i = 0; i < menu->nitems; i++) {
		item = menu->items[i];

		menuCondition_t *condition = &((menuCommon_t*)item)->condition;
		if (condition->cvar)
		{
			bool equals = condition->cvar->integer == condition->value;
			if (equals == condition->equals)
				((menuCommon_t *)item)->flags &= ~QMF_HIDDEN;
			else
				((menuCommon_t *)item)->flags |= QMF_HIDDEN;
		}
	}

    if (!menu->size) {
        menu->size = Menu_Size;
    }
    menu->size(menu);

    for (i = 0; i < menu->nitems; i++) {
        item = menu->items[i];

        focus |= ((menuCommon_t *)item)->flags & QMF_HASFOCUS;
        switch (((menuCommon_t *)item)->type) {
        case MTYPE_FIELD:
            Field_Init(item);
            break;
        case MTYPE_SLIDER:
            Slider_Init(item);
            break;
        case MTYPE_LIST:
            MenuList_Init(item);
            break;
        case MTYPE_SPINCONTROL:
        case MTYPE_BITFIELD:
        case MTYPE_PAIRS:
        case MTYPE_VALUES:
        case MTYPE_STRINGS:
        case MTYPE_TOGGLE:
            SpinControl_Init(item);
            break;
        case MTYPE_ACTION:
        case MTYPE_SAVEGAME:
        case MTYPE_LOADGAME:
            Action_Init(item);
            break;
        case MTYPE_SEPARATOR:
            Separator_Init(item);
            break;
        case MTYPE_STATIC:
            Static_Init(item);
            break;
        case MTYPE_KEYBIND:
            Keybind_Init(item);
            break;
        case MTYPE_BITMAP:
            Bitmap_Init(item);
            break;
        default:
            Q_assert(!"unknown item type");
        }
    }

    // set focus to the first non-hidden item by default
    if (!focus && menu->nitems) {
		for (i = 0; i < menu->nitems; i++) {
			item = menu->items[i];
			if (!(((menuCommon_t *)item)->flags & QMF_HIDDEN) && (((menuCommon_t *)item)->type != MTYPE_SEPARATOR)) {
				((menuCommon_t *)item)->flags |= QMF_HASFOCUS;
				if (((menuCommon_t *)item)->status) {
					menu->status = ((menuCommon_t *)item)->status;
				}
				break;
			}
		}
    }

    // calc menu bounding box
    UI_ClearBounds(menu->mins, menu->maxs);

    for (i = 0; i < menu->nitems; i++) {
        item = menu->items[i];
        rc = &((menuCommon_t *)item)->rect;

        UI_AddRectToBounds(rc, menu->mins, menu->maxs);
    }

    // expand
    menu->mins[0] -= MENU_SPACING;
    menu->mins[1] -= MENU_SPACING;
    menu->maxs[0] += MENU_SPACING;
    menu->maxs[1] += MENU_SPACING;

    // clamp
    if (menu->mins[0] < 0) menu->mins[0] = 0;
    if (menu->mins[1] < 0) menu->mins[1] = 0;
    if (menu->maxs[0] > uis.width) menu->maxs[0] = uis.width;
    if (menu->maxs[1] > uis.height) menu->maxs[1] = uis.height;
}

void Menu_Size(menuFrameWork_t *menu)
{
    menuCommon_t *item;
    int x, y, w, h;
    int i, widest = -1;

    // count visible items
    for (i = 0, h = 0; i < menu->nitems; i++) {
        item = menu->items[i];
        if (item->flags & QMF_HIDDEN) {
            continue;
        }
        if (item->type == MTYPE_BITMAP) {
            h += GENERIC_SPACING(item->height);
            if (widest < item->width) {
                widest = item->width;
            }
        } else {
            h += MENU_SPACING;
        }
    }

    // account for banner
    if (menu->banner) {
        h += GENERIC_SPACING(menu->banner_rc.height);
    }

    // set menu top/bottom
    if (menu->compact) {
        menu->y1 = (uis.height - h) / 2 - MENU_SPACING;
        menu->y2 = (uis.height + h) / 2 + MENU_SPACING;
    } else {
        menu->y1 = 0;
        menu->y2 = uis.height;
    }

    // set menu horizontal base
    if (widest == -1) {
        x = uis.width / 2;
    } else {
        // if menu has bitmaps, it is expected to have plaque and logo
        // align them horizontally to avoid going off screen on small resolution
        w = widest + CURSOR_WIDTH;
        if (menu->plaque_rc.width > menu->logo_rc.width) {
            w += menu->plaque_rc.width;
        } else {
            w += menu->logo_rc.width;
        }
        x = (uis.width + w) / 2 - widest;
    }

    // set menu vertical base
    y = (uis.height - h) / 2;

    // banner is horizontally centered and
    // positioned on top of all menu items
    if (menu->banner) {
        menu->banner_rc.x = (uis.width - menu->banner_rc.width) / 2;
        menu->banner_rc.y = y;
        y += GENERIC_SPACING(menu->banner_rc.height);
    }

    // plaque and logo are vertically centered and
    // positioned to the left of bitmaps and cursor
    h = 0;
    if (menu->plaque) {
        h += menu->plaque_rc.height;
    }
    if (menu->logo) {
        h += menu->logo_rc.height + 5;
    }

    if (menu->plaque) {
        menu->plaque_rc.x = x - CURSOR_WIDTH - menu->plaque_rc.width;
        menu->plaque_rc.y = (uis.height - h) / 2;
    }

    if (menu->logo) {
        menu->logo_rc.x = x - CURSOR_WIDTH - menu->logo_rc.width;
        menu->logo_rc.y = (uis.height + h) / 2 - menu->logo_rc.height;
    }
	
    // align items
    for (i = 0; i < menu->nitems; i++) {
        item = menu->items[i];
        if (item->flags & QMF_HIDDEN) {
            continue;
        }
        item->x = x;
        item->y = y;
        if (item->type == MTYPE_BITMAP) {
            y += GENERIC_SPACING(item->height);
        } else {
            y += MENU_SPACING;
        }
    }

	// footer is horizontally centered and
	// positioned below all menu items
	if (menu->footer) {
		menu->footer_rc.x = (uis.width - menu->footer_rc.width) / 2;

		menu->footer_rc.y = y;

		if (menu->plaque)
			menu->footer_rc.y = max(menu->footer_rc.y, menu->plaque_rc.y + menu->plaque_rc.height);

		if (menu->logo)
			menu->footer_rc.y = max(menu->footer_rc.y, menu->logo_rc.y + menu->logo_rc.height);

		menu->footer_rc.y += menu->footer_rc.height;
	}
}

menuCommon_t *Menu_ItemAtCursor(menuFrameWork_t *m)
{
    menuCommon_t *item;
    int i;

    for (i = 0; i < m->nitems; i++) {
        item = m->items[i];
        if (item->flags & QMF_HASFOCUS) {
            return item;
        }
    }

    return NULL;
}

void Menu_SetFocus(menuCommon_t *focus)
{
    menuFrameWork_t *menu;
    menuCommon_t *item;
    int i;

    if (focus->flags & QMF_HASFOCUS) {
        return;
    }

    menu = focus->parent;

    for (i = 0; i < menu->nitems; i++) {
        item = (menuCommon_t *)menu->items[i];

        if (item == focus) {
            item->flags |= QMF_HASFOCUS;
            if (item->focus) {
                item->focus(item, true);
            } else if (item->status) {
                menu->status = item->status;
            }
        } else if (item->flags & QMF_HASFOCUS) {
            item->flags &= ~QMF_HASFOCUS;
            if (item->focus) {
                item->focus(item, false);
            } else if (menu->status == item->status
                       && menu->status != focus->status) {
                menu->status = NULL;
            }
        }
    }

}

/*
=================
Menu_AdjustCursor

This function takes the given menu, the direction, and attempts
to adjust the menu's cursor so that it's at the next available
slot.
=================
*/
menuSound_t Menu_AdjustCursor(menuFrameWork_t *m, int dir)
{
    menuCommon_t *item;
    int cursor, pos;
    int i;

    if (!m->nitems) {
        return QMS_NOTHANDLED;
    }

    pos = 0;
    for (i = 0; i < m->nitems; i++) {
        item = (menuCommon_t *)m->items[i];

        if (item->flags & QMF_HASFOCUS) {
            pos = i;
            break;
        }
    }

    /*
    ** crawl in the direction indicated until we find a valid spot
    */
    cursor = pos;
    if (dir == 1) {
        do {
            cursor++;
            if (cursor >= m->nitems)
                cursor = 0;

            item = (menuCommon_t *)m->items[cursor];
            if (UI_IsItemSelectable(item))
                break;
        } while (cursor != pos);
    } else {
        do {
            cursor--;
            if (cursor < 0)
                cursor = m->nitems - 1;

            item = (menuCommon_t *)m->items[cursor];
            if (UI_IsItemSelectable(item))
                break;
        } while (cursor != pos);
    }

    Menu_SetFocus(item);

    return QMS_MOVE;
}

static void Menu_DrawStatus(menuFrameWork_t *menu)
{
    int     linewidth = uis.width / CHAR_WIDTH;
    int     x, y, l, count;
    char    *txt, *p;
    int     lens[8];
    char    *ptrs[8];

    txt = menu->status;
    x = 0;

    count = 0;
    ptrs[0] = txt;

    while (*txt) {
        // count word length
        for (p = txt; *p > 32; p++)
            ;
        l = p - txt;

        // word wrap
        if ((l < linewidth && x + l > linewidth) || (x == linewidth)) {
            if (count == 7)
                break;
            lens[count++] = x;
            ptrs[count] = txt;
            x = 0;
        }

        // display character and advance
        txt++;
        x++;
    }

    lens[count++] = x;

    for (l = 0; l < count; l++) {
        x = (uis.width - lens[l] * CHAR_WIDTH) / 2;
        y = menu->y2 - (count - l) * CHAR_HEIGHT;
        R_DrawString(x, y, 0, lens[l], ptrs[l], uis.fontHandle);
    }
}

/*
=================
Menu_Draw
=================
*/
void Menu_Draw(menuFrameWork_t *menu)
{
    void *item;
    int i;

//
// draw background
//
    if (menu->image) {
        R_DrawKeepAspectPic(0, menu->y1, uis.width,
                            menu->y2 - menu->y1, menu->image);
    } else {
        R_DrawFill32(0, menu->y1, uis.width,
                     menu->y2 - menu->y1, menu->color.u32);
    }

//
// draw title bar
//
    if (menu->title) {
        UI_DrawString(uis.width / 2, menu->y1,
                      UI_CENTER | UI_ALTCOLOR, menu->title);
    }

//
// draw banner, plaque and logo
//
    if (menu->banner) {
        R_DrawPic(menu->banner_rc.x, menu->banner_rc.y, menu->banner);
    }
    if (menu->plaque) {
        R_DrawPic(menu->plaque_rc.x, menu->plaque_rc.y, menu->plaque);
    }
    if (menu->logo) {
        R_DrawPic(menu->logo_rc.x, menu->logo_rc.y, menu->logo);
	}
	if (menu->footer) {
		R_DrawStretchPic(menu->footer_rc.x, menu->footer_rc.y, menu->footer_rc.width, menu->footer_rc.height, menu->footer);
	}

//
// draw contents
//
    for (i = 0; i < menu->nitems; i++) {
        item = menu->items[i];
        if (((menuCommon_t *)item)->flags & QMF_HIDDEN) {
            continue;
        }

        switch (((menuCommon_t *)item)->type) {
        case MTYPE_FIELD:
            Field_Draw(item);
            break;
        case MTYPE_SLIDER:
            Slider_Draw(item);
            break;
        case MTYPE_LIST:
            MenuList_Draw(item);
            break;
        case MTYPE_SPINCONTROL:
        case MTYPE_BITFIELD:
        case MTYPE_PAIRS:
        case MTYPE_VALUES:
        case MTYPE_STRINGS:
        case MTYPE_TOGGLE:
            SpinControl_Draw(item);
            break;
        case MTYPE_ACTION:
        case MTYPE_SAVEGAME:
        case MTYPE_LOADGAME:
            Action_Draw(item);
            break;
        case MTYPE_SEPARATOR:
            Separator_Draw(item);
            break;
        case MTYPE_STATIC:
            Static_Draw(item);
            break;
        case MTYPE_KEYBIND:
            Keybind_Draw(item);
            break;
        case MTYPE_BITMAP:
            Bitmap_Draw(item);
            break;
        default:
            Q_assert(!"unknown item type");
        }

        if (ui_debug->integer) {
            UI_DrawRect8(&((menuCommon_t *)item)->rect, 1, 223);
        }
    }

//
// draw status bar
//
    if (menu->status) {
        Menu_DrawStatus(menu);
    }
}

menuSound_t Menu_SelectItem(menuFrameWork_t *s)
{
    menuCommon_t *item;

    if (!(item = Menu_ItemAtCursor(s))) {
        return QMS_NOTHANDLED;
    }

    switch (item->type) {
    //case MTYPE_SLIDER:
    //    return Slider_DoSlide((menuSlider_t *)item, 1);
    case MTYPE_SPINCONTROL:
    case MTYPE_BITFIELD:
    case MTYPE_PAIRS:
    case MTYPE_VALUES:
    case MTYPE_STRINGS:
    case MTYPE_TOGGLE:
        return SpinControl_DoEnter((menuSpinControl_t *)item);
    case MTYPE_KEYBIND:
        return Keybind_DoEnter((menuKeybind_t *)item);
    case MTYPE_FIELD:
    case MTYPE_ACTION:
    case MTYPE_LIST:
    case MTYPE_BITMAP:
    case MTYPE_SAVEGAME:
    case MTYPE_LOADGAME:
        return Common_DoEnter(item);
    default:
        return QMS_NOTHANDLED;
    }
}

menuSound_t Menu_SlideItem(menuFrameWork_t *s, int dir)
{
    menuCommon_t *item;

    if (!(item = Menu_ItemAtCursor(s))) {
        return QMS_NOTHANDLED;
    }

    switch (item->type) {
    case MTYPE_SLIDER:
        return Slider_DoSlide((menuSlider_t *)item, dir);
    case MTYPE_SPINCONTROL:
    case MTYPE_BITFIELD:
    case MTYPE_PAIRS:
    case MTYPE_VALUES:
    case MTYPE_STRINGS:
    case MTYPE_TOGGLE:
        return SpinControl_DoSlide((menuSpinControl_t *)item, dir);
    default:
        return QMS_NOTHANDLED;
    }
}

menuSound_t Menu_KeyEvent(menuCommon_t *item, int key)
{
    if (item->keydown) {
        menuSound_t sound = item->keydown(item, key);
        if (sound != QMS_NOTHANDLED) {
            return sound;
        }
    }

    switch (item->type) {
    case MTYPE_FIELD:
        return Field_Key((menuField_t *)item, key);
    case MTYPE_LIST:
        return MenuList_Key((menuList_t *)item, key);
    case MTYPE_SLIDER:
        return Slider_Key((menuSlider_t *)item, key);
    case MTYPE_KEYBIND:
        return Keybind_Key((menuKeybind_t *)item, key);
    default:
        return QMS_NOTHANDLED;
    }
}

menuSound_t Menu_CharEvent(menuCommon_t *item, int key)
{
    switch (item->type) {
    case MTYPE_FIELD:
        return Field_Char((menuField_t *)item, key);
    default:
        return QMS_NOTHANDLED;
    }
}

menuSound_t Menu_MouseMove(menuCommon_t *item)
{
    switch (item->type) {
    case MTYPE_LIST:
        return MenuList_MouseMove((menuList_t *)item);
    case MTYPE_SLIDER:
        return Slider_MouseMove((menuSlider_t *)item);
    default:
        return QMS_NOTHANDLED;
    }
}

static menuSound_t Menu_DefaultKey(menuFrameWork_t *m, int key)
{
    menuCommon_t *item;

    switch (key) {
    case K_ESCAPE:
    case K_MOUSE2:
        UI_PopMenu();
        return QMS_OUT;

    case K_KP_UPARROW:
    case K_UPARROW:
    case 'k':
        return Menu_AdjustCursor(m, -1);

    case K_KP_DOWNARROW:
    case K_DOWNARROW:
    case K_TAB:
    case 'j':
        return Menu_AdjustCursor(m, 1);

    case K_KP_LEFTARROW:
    case K_LEFTARROW:
    case K_MWHEELDOWN:
    case 'h':
        return Menu_SlideItem(m, -1);

    case K_KP_RIGHTARROW:
    case K_RIGHTARROW:
    case K_MWHEELUP:
    case 'l':
        return Menu_SlideItem(m, 1);

    case K_MOUSE1:
    //case K_MOUSE2:
    case K_MOUSE3:
        item = Menu_HitTest(m);
        if (!item) {
            return QMS_NOTHANDLED;
        }

        if (!(item->flags & QMF_HASFOCUS)) {
            return QMS_NOTHANDLED;
        }
        // fall through

    case K_KP_ENTER:
    case K_ENTER:
        return Menu_SelectItem(m);
    }

    return QMS_NOTHANDLED;
}

menuSound_t Menu_Keydown(menuFrameWork_t *menu, int key)
{
    menuCommon_t *item;
    menuSound_t sound;

    if (menu->keywait) {
    }

    if (menu->keydown) {
        sound = menu->keydown(menu, key);
        if (sound != QMS_NOTHANDLED) {
            return sound;
        }
    }

    item = Menu_ItemAtCursor(menu);
    if (item) {
        sound = Menu_KeyEvent(item, key);
        if (sound != QMS_NOTHANDLED) {
            return sound;
        }
    }

    sound = Menu_DefaultKey(menu, key);
    return sound;
}


menuCommon_t *Menu_HitTest(menuFrameWork_t *menu)
{
    int i;
    menuCommon_t *item;

    if (menu->keywait) {
        return NULL;
    }

    for (i = 0; i < menu->nitems; i++) {
        item = menu->items[i];
        if (item->flags & QMF_HIDDEN) {
            continue;
        }

        if (UI_CursorInRect(&item->rect)) {
            return item;
        }
    }

    return NULL;
}

bool Menu_Push(menuFrameWork_t *menu)
{
    void *item;
    int i;

    for (i = 0; i < menu->nitems; i++) {
        item = menu->items[i];

        switch (((menuCommon_t *)item)->type) {
        case MTYPE_SLIDER:
            Slider_Push(item);
            break;
        case MTYPE_BITFIELD:
            BitField_Push(item);
            break;
        case MTYPE_PAIRS:
            Pairs_Push(item);
            break;
        case MTYPE_STRINGS:
            Strings_Push(item);
            break;
        case MTYPE_SPINCONTROL:
            SpinControl_Push(item);
            break;
        case MTYPE_TOGGLE:
            Toggle_Push(item);
            break;
        case MTYPE_KEYBIND:
            Keybind_Push(item);
            break;
        case MTYPE_FIELD:
            Field_Push(item);
            break;
        case MTYPE_SAVEGAME:
        case MTYPE_LOADGAME:
            Savegame_Push(item);
            break;
        default:
            break;
        }
    }
    return true;
}

void Menu_Pop(menuFrameWork_t *menu)
{
    void *item;
    int i;

    for (i = 0; i < menu->nitems; i++) {
        item = menu->items[i];

        switch (((menuCommon_t *)item)->type) {
        case MTYPE_SLIDER:
            Slider_Pop(item);
            break;
        case MTYPE_BITFIELD:
            BitField_Pop(item);
            break;
        case MTYPE_PAIRS:
            Pairs_Pop(item);
            break;
        case MTYPE_STRINGS:
            Strings_Pop(item);
            break;
        case MTYPE_SPINCONTROL:
            SpinControl_Pop(item);
            break;
        case MTYPE_TOGGLE:
            Toggle_Pop(item);
            break;
        case MTYPE_KEYBIND:
            Keybind_Pop(item);
            break;
        case MTYPE_FIELD:
            Field_Pop(item);
            break;
        default:
            break;
        }
    }
}

void Menu_Free(menuFrameWork_t *menu)
{
    void *item;
    int i;

    for (i = 0; i < menu->nitems; i++) {
        item = menu->items[i];

        switch (((menuCommon_t *)item)->type) {
        case MTYPE_ACTION:
        case MTYPE_SAVEGAME:
        case MTYPE_LOADGAME:
            Action_Free(item);
            break;
        case MTYPE_SLIDER:
            Slider_Free(item);
            break;
        case MTYPE_BITFIELD:
        case MTYPE_TOGGLE:
            BitField_Free(item);
            break;
        case MTYPE_PAIRS:
            Pairs_Free(item);
            break;
        case MTYPE_SPINCONTROL:
        case MTYPE_STRINGS:
            SpinControl_Free(item);
            break;
        case MTYPE_KEYBIND:
            Keybind_Free(item);
            break;
        case MTYPE_FIELD:
            Field_Free(item);
            break;
        case MTYPE_SEPARATOR:
            Z_Free(item);
            break;
        case MTYPE_BITMAP:
            Bitmap_Free(item);
            break;
        default:
            break;
        }
    }

    Z_Free(menu->items);
    Z_Free(menu->title);
    Z_Free(menu->name);
    Z_Free(menu);
}

