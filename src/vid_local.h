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

extern cvar_t       *vid_ref;
extern cvar_t       *vid_geometry;
extern cvar_t       *vid_modelist;
extern cvar_t       *vid_fullscreen;
extern cvar_t       *_vid_fullscreen;

//
// vid_*.c
//
void VID_PumpEvents( void );
void VID_SetMode( void );
char *VID_GetDefaultModeList( void );

//
// cl_ref.c
//
qboolean VID_GetFullscreen( vrect_t *rc, int *freq_p, int *depth_p );
qboolean VID_GetGeometry( vrect_t *rc );
void VID_SetGeometry( vrect_t *rc );
void VID_ToggleFullscreen( void );
