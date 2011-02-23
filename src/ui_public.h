/*
Copyright (C) 2003-2008 Andrey Nazarov

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

typedef enum {
    UIMENU_NONE,
    UIMENU_DEFAULT,
    UIMENU_MAIN,
    UIMENU_GAME
} uiMenu_t;

qboolean    UI_Init( void );
void        UI_Shutdown( void );
void        UI_ModeChanged( void );
void        UI_Keydown( int key );
void        UI_CharEvent( int key );
void        UI_Draw( int realtime );
void        UI_OpenMenu( uiMenu_t menu );
void        UI_AddToServerList( const serverStatus_t *status );
void        UI_MouseEvent( int x, int y );
qboolean    UI_IsTransparent( void );

