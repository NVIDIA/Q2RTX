/*
Copyright (C) 2003-2008 Andrey Nazarov

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

#ifndef UI_H
#define UI_H

#include "common/net/net.h"
#include "client/client.h"

typedef enum {
    UIMENU_NONE,
    UIMENU_DEFAULT,
    UIMENU_MAIN,
    UIMENU_GAME
} uiMenu_t;

#if USE_UI
void        UI_Init(void);
void        UI_Shutdown(void);
void        UI_ModeChanged(void);
void        UI_KeyEvent(int key, bool down);
void        UI_CharEvent(int key);
void        UI_Draw(int realtime);
void        UI_OpenMenu(uiMenu_t menu);
void        UI_Frame(int msec);
void        UI_StatusEvent(const serverStatus_t *status);
void        UI_ErrorEvent(netadr_t *from);
void        UI_MouseEvent(int x, int y);
bool        UI_IsTransparent(void);
#else
#define     UI_Init()               (void)0
#define     UI_Shutdown()           (void)0
#define     UI_ModeChanged()        (void)0
#define     UI_KeyEvent(key, down)  (void)0
#define     UI_CharEvent(key)       (void)0
#define     UI_Draw(realtime)       (void)0
#define     UI_OpenMenu(menu)       (void)0
#define     UI_Frame(msec)          (void)0
#define     UI_StatusEvent(status)  (void)0
#define     UI_ErrorEvent(from)     (void)0
#define     UI_MouseEvent(x, y)     (void)0
#define     UI_IsTransparent()      true
#endif

#endif // UI_H
