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

//
// ui_shared.c - basic UI support for client modules
//

#include "config.h"
#include "q_shared.h"
#include "com_public.h"
#include "ref_public.h"
#include "q_uis.h"

/*
===============================================================================

STRING DRAWING

===============================================================================
*/


/*
==============
UIS_DrawStringEx
==============
*/
void UIS_DrawStringEx( int x, int y, int flags, int maxChars,
        const char *string, qhandle_t hFont )
{
	char	*p;
	int		length;
	int		xx;
    int     cw, ch;

	if( maxChars < 1 ) {
		maxChars = MAX_STRING_CHARS;
	}

    ref.DrawGetFontSize( &cw, &ch, hFont );

	if( !( flags & UI_MULTILINE ) ) {
		if( ( flags & UI_CENTER ) == UI_CENTER ) {
			x -= Q_DrawStrlenTo( string, maxChars ) * cw / 2;
		} else if( flags & UI_RIGHT ) {
			x -= Q_DrawStrlenTo( string, maxChars ) * cw;
		}

		ref.DrawString( x, y, flags, maxChars, string, hFont );
		return;
	}

	while( *string ) {
		if( ( p = strchr( string, '\n' ) ) != NULL && p - string < maxChars ) {
			length = p - string;
		} else {
			length = maxChars;
		}

		xx = x;
		if( ( flags & UI_CENTER ) == UI_CENTER ) {
			xx -= Q_DrawStrlenTo( string, length ) * cw / 2;
		} else if( flags & UI_RIGHT ) {
			xx -= Q_DrawStrlenTo( string, length ) * cw;
		}

		ref.DrawString( xx, y, flags, length, string, hFont );

		if( !p ) {
			break;
		}

		y += ch;
		string = p + 1;
	}

	// TODO: UI_AUTOWRAP support
	
}

/*
===============================================================================

DRAWING

===============================================================================
*/


/*
==============
UIS_DrawStretchPicByName
==============
*/
void UIS_DrawStretchPicByName( int x, int y, int w, int h, const char *pic ) {	
	ref.DrawStretchPic( x, y, w, h, ref.RegisterPic( pic ) );
}

/*
==============
UIS_DrawPicByName
==============
*/
void UIS_DrawPicByName( int x, int y, const char *pic )	{
	ref.DrawPic( x, y, ref.RegisterPic( pic ) );
}

/*
================
UIS_DrawRect
================
*/
void UIS_DrawRect( const vrect_t *rect, int width, int color ) {
	int x, y, w, h;

	// TODO: remove this
	x = rect->x;
	y = rect->y;
	w = rect->width;
	h = rect->height;

	ref.DrawFill( x, y, width, h, color ); // left
	ref.DrawFill( x + w - width, y, width, h, color ); // right
	ref.DrawFill( x + width, y, w - width * 2, width, color ); // top
	ref.DrawFill( x + width, y + h - width, w - width * 2, width, color ); // bottom
}

/*
================
UIS_DrawRectEx
================
*/
void UIS_DrawRectEx( const vrect_t *rect, int width, const color_t color ) {
	int x, y, w, h;

	// TODO: remove this
	x = rect->x;
	y = rect->y;
	w = rect->width;
	h = rect->height;

	ref.DrawFillEx( x, y, width, h, color ); // left
	ref.DrawFillEx( x + w - width, y, width, h, color ); // right
	ref.DrawFillEx( x + width, y, w - width * 2, width, color ); // top
	ref.DrawFillEx( x + width, y + h - width, w - width * 2, width, color ); // bottom
}

