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

typedef enum {
	ss_dead,			// no map loaded
	ss_loading,			// spawning level edicts
	ss_game,			// actively running
	ss_broadcast
} server_state_t;

typedef enum {
	KILL_RESTART,
	KILL_DISCONNECT,
	KILL_DROP
} killtype_t;

void SV_ProcessEvents( void );
void SV_Init (void);
void SV_Shutdown( const char *finalmsg, killtype_t type );
void SV_Frame (int msec);
qboolean MVD_GetDemoPercent( int *percent, int *bufferPercent );

