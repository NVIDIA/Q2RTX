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

extern game_export_t    mvd_ge;

extern list_t mvd_gtv_list;

struct client_s;

void MVD_Register( void );
void MVD_Shutdown( void );
void MVD_RemoveClient( struct client_s *client );
int MVD_Frame( void );
void MVD_PrepWorldFrame( void );

void MVD_GameClientDrop( edict_t *ent, const char *reason );
void MVD_GameClientNameChanged( edict_t *ent, const char *name );

void MVD_StreamedStop_f( void );
void MVD_StreamedRecord_f( void );
void MVD_File_g( genctx_t *ctx );

