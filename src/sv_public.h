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
    ss_dead,            // no map loaded
    ss_loading,         // spawning level edicts
    ss_game,            // actively running
#if USE_MVD_CLIENT
    ss_broadcast        // running MVD client
#endif
} server_state_t;

#if USE_ICMP
void SV_ErrorEvent( int info );
#endif
void SV_Init (void);
void SV_Shutdown( const char *finalmsg, error_type_t type );
unsigned SV_Frame (unsigned msec);
#if USE_SYSCON
void SV_SetConsoleTitle( void );
#endif
//void SV_ConsoleOutput( const char *msg );

#if USE_MVD_CLIENT && USE_CLIENT
int MVD_GetDemoPercent( qboolean *paused );
#endif

