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
extern cvar_t       *vid_placement;
extern cvar_t       *vid_modelist;
extern cvar_t       *vid_fullscreen;
extern cvar_t       *_vid_fullscreen;

void Video_PumpEvents( void );
void Video_ModeChanged( void );
void Video_FillInputAPI( inputAPI_t *api );
void Video_FillGLAPI( videoAPI_t *api );
void Video_FillSWAPI( videoAPI_t *api );

void Video_GetModeFS( vrect_t *rc, int *freq, int *depth );
void Video_GetPlacement( vrect_t *rc ); 
void Video_SetPlacement( vrect_t *rc ); 
void Video_ToggleFullscreen( void );
