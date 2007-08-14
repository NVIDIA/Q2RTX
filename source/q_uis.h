/*
Copyright (C) 2003-2004 Andrey Nazarov

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

//
// ui_shared.h
//

void	UIS_DrawStringEx( int x, int y, uint32 flags, int maxChars,
            const char *string, qhandle_t hFont );
void	UIS_DrawStretchPicByName( int x, int y, int w, int h, const char *pic );
void	UIS_DrawPicByName( int x, int y, const char *pic );
void	UIS_DrawRect( const vrect_t *rect, int width, int color );
void	UIS_DrawRectEx( const vrect_t *rect, int width, const color_t color );

#define UIS_FillRect( rect, color ) \
    ref.DrawFill( (rect)->x, (rect)->y, (rect)->width, (rect)->height, color )

#define UIS_FillRectEx( rect, color ) \
    ref.DrawFillEx( (rect)->x, (rect)->y, (rect)->width, (rect)->height, color )

