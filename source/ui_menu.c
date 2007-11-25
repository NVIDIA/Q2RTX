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

#include "ui_local.h"

static color_t colorField = { 15, 128, 235, 100 };
static color_t colorGray = { 127, 127, 127, 255 };

#define UI_CALLBACK( x, y, z ) \
    (x)->generic.parent->callback( (x)->generic.id, y, z )

/*
=================
Common_DoEnter
=================
*/
static int Common_DoEnter( menuCommon_t *item ) {
	int ret;

	if( ( ret = item->parent->callback( item->id, QM_ACTIVATE, 0 ) ) != QMS_NOTHANDLED ) {
		return ret;
	}

	return QMS_SILENT;
}

/*
===================================================================

ACTION CONTROL

===================================================================
*/

/*
=================
Action_Init
=================
*/
static void Action_Init( menuAction_t *a ) {
	if( !a->generic.name ) {
		Com_Error( ERR_FATAL, "Action_Init: NULL a->generic.name" );
	}

	a->generic.rect.x = a->generic.x;
	a->generic.rect.y = a->generic.y;
	UI_StringDimensions( &a->generic.rect, a->generic.uiFlags, a->generic.name );
}


/*
=================
Action_Draw
=================
*/
static void Action_Draw( menuAction_t *a ) {
	uint32 flags;

	flags = a->generic.uiFlags;
	if( a->generic.flags & QMF_HASFOCUS ) {
		flags |= UI_ALTCOLOR;
	}

	UI_DrawString( a->generic.x, a->generic.y, NULL,
		flags, a->generic.name );

	
}

/*
===================================================================

BITMAP CONTROL

===================================================================
*/

/*
=================
Bitmap_Init
=================
*/
void Bitmap_Init( menuBitmap_t *b ) {
	if( !b->generic.name ) {
		Com_Error( ERR_FATAL, "Bitmap_Init: NULL b->generic.name" );
	}

	b->generic.rect.x = b->generic.x;
	b->generic.rect.y = b->generic.y;
	b->generic.rect.width = b->generic.width;
	b->generic.rect.height = b->generic.height;

	b->pic = ref.RegisterPic( b->generic.name );
	if( !b->pic && b->errorImage ) {
		b->pic = ref.RegisterPic( b->errorImage );
	}

}

/*
=================
Bitmap_Draw
=================
*/
static void Bitmap_Draw( menuBitmap_t *b ) {
	ref.DrawStretchPic( b->generic.x, b->generic.y,
		b->generic.width, b->generic.height, b->pic );
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
static void Static_Init( menuStatic_t *s ) {
	if( !s->generic.name ) {
		Com_Error( ERR_FATAL, "Static_Init: NULL s->generic.name" );
	}

	if( !s->maxChars ) {
		s->maxChars = MAX_STRING_CHARS;
	}

	s->generic.rect.x = s->generic.x;
	s->generic.rect.y = s->generic.y;

	UI_StringDimensions( &s->generic.rect,
		s->generic.uiFlags, s->generic.name );

}

/*
=================
Static_Draw
=================
*/
static void Static_Draw( menuStatic_t *s ) {
	UI_DrawString( s->generic.x, s->generic.y,
		( s->generic.flags & QMF_CUSTOM_COLOR ) ? s->generic.color : NULL,
		s->generic.uiFlags, s->generic.name );
}

/*
===================================================================

STATIC CONTROL

===================================================================
*/

/*
=================
Keybind_Init
=================
*/
static void Keybind_Init( menuKeybind_t *k ) {
	if( !k->generic.name ) {
		Com_Error( ERR_FATAL, "Keybind_Init: NULL k->generic.name" );
	}

	k->generic.uiFlags &= ~( UI_LEFT | UI_RIGHT );

	k->generic.rect.x = k->generic.x + LCOLUMN_OFFSET;
	k->generic.rect.y = k->generic.y;

	UI_StringDimensions( &k->generic.rect,
		k->generic.uiFlags | UI_RIGHT, k->generic.name );

	k->generic.rect.x = k->generic.x;
	k->generic.rect.y = k->generic.y;

	k->generic.rect.width += ( RCOLUMN_OFFSET - LCOLUMN_OFFSET ) +
		Q_DrawStrlen( k->binding ) * SMALLCHAR_WIDTH;
}

/*
=================
Keybind_Draw
=================
*/
static void Keybind_Draw( menuKeybind_t *k ) {
	char string[MAX_STRING_CHARS];
	byte *color;
	uint32 flags;

	color = NULL;
	flags = UI_ALTCOLOR;
	if( k->generic.flags & QMF_HASFOCUS ) {
		if( k->generic.parent->keywait ) {
			//UI_DrawChar( k->generic.x + RCOLUMN_OFFSET / 2, k->generic.y, k->generic.uiFlags | UI_RIGHT, '=' );
		} else if( ( uis.realtime >> 8 ) & 1 ) {
			UI_DrawChar( k->generic.x + RCOLUMN_OFFSET / 2, k->generic.y, k->generic.uiFlags | UI_RIGHT, 13 );
		}
	} else {
		if( k->generic.parent->keywait ) {
			color = colorGray;
			flags = 0;
		}
	}

	UI_DrawString( k->generic.x + LCOLUMN_OFFSET, k->generic.y, color,
		k->generic.uiFlags | UI_RIGHT | flags, k->generic.name );

	if( k->altbinding[0] ) {
		Q_concat( string, sizeof( string ), k->binding, " or ", k->altbinding, NULL );
	} else if( k->binding[0] ) {
		strcpy( string, k->binding );
	} else {
		strcpy( string, "???" );
	}

	UI_DrawString( k->generic.x + RCOLUMN_OFFSET, k->generic.y, color,
		k->generic.uiFlags | UI_LEFT, string );
}

/*
===================================================================

FIELD CONTROL

===================================================================
*/

/*
=================
Field_Init
=================
*/
static void Field_Init( menuField_t *f ) {
	f->generic.uiFlags &= ~( UI_LEFT | UI_RIGHT );

	f->generic.rect.x = f->generic.x + LCOLUMN_OFFSET;
	f->generic.rect.y = f->generic.y;
	
	if( f->generic.name ) {
		UI_StringDimensions( &f->generic.rect,
			f->generic.uiFlags | UI_RIGHT, f->generic.name );
	} else {
		f->generic.rect.width = 0;
		f->generic.rect.height = SMALLCHAR_HEIGHT;
	}
	
	f->generic.rect.width += RCOLUMN_OFFSET +
		f->field.visibleChars * SMALLCHAR_WIDTH;

}


/*
=================
Field_Draw
=================
*/
static void Field_Draw( menuField_t *f ) {
	uint32	flags;

	if( f->generic.name ) {
		UI_DrawString( f->generic.x + LCOLUMN_OFFSET, f->generic.y, NULL,
			f->generic.uiFlags | UI_RIGHT | UI_ALTCOLOR, f->generic.name );
	}

	ref.DrawFillEx( f->generic.x + RCOLUMN_OFFSET, f->generic.y,
		f->field.visibleChars * SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT, colorField );

	flags = f->generic.uiFlags;
	if( f->generic.flags & QMF_HASFOCUS ) {
		flags |= UI_DRAWCURSOR;
	}

	IF_Draw( &f->field, f->generic.x + RCOLUMN_OFFSET, f->generic.y,
		flags, uis.fontHandle );
}

/*
=================
Field_Key
=================
*/
static int Field_Key( menuField_t *f, int key ) {
	qboolean ret;

	ret = IF_KeyEvent( &f->field, key );
	return ret ? QMS_SILENT : QMS_NOTHANDLED;
}

/*
=================
Field_Key
=================
*/
static int Field_Char( menuField_t *f, int key ) {
	int ret;

	if( f->generic.flags & QMF_NUMBERSONLY ) {
		if( key < '0' || key > '9' ) {
			return QMS_BEEP;
		}
	}

	ret = IF_CharEvent( &f->field, key );
	UI_CALLBACK( f, QM_CHANGE, ret );

	return ret ? QMS_SILENT : QMS_NOTHANDLED;
}

/*
===================================================================

SPIN CONTROL

===================================================================
*/

/*
=================
SpinControl_Init
=================
*/
void SpinControl_Init( menuSpinControl_t *s ) {
	const char **n;
	int	maxLength, length;

	s->generic.uiFlags &= ~( UI_LEFT | UI_RIGHT );

	s->generic.rect.x = s->generic.x + LCOLUMN_OFFSET;
	s->generic.rect.y = s->generic.y;

	UI_StringDimensions( &s->generic.rect,
		s->generic.uiFlags | UI_RIGHT, s->generic.name );

	maxLength = 0;
	s->numItems = 0;
	n = s->itemnames;
	while( *n ) {
		length = Q_DrawStrlen( *n );
		
		if( maxLength < length ) {
			maxLength = length;
		}
		s->numItems++;
		n++;
	}

	s->generic.rect.width += ( RCOLUMN_OFFSET - LCOLUMN_OFFSET ) +
		maxLength * SMALLCHAR_WIDTH;

}

/*
=================
SpinControl_DoEnter
=================
*/
static int SpinControl_DoEnter( menuSpinControl_t *s ) {
	int		oldvalue = s->curvalue;

	s->curvalue++;

	if( s->curvalue >= s->numItems )
		s->curvalue = 0;

	UI_CALLBACK( s, QM_CHANGE, oldvalue );

	return QMS_MOVE;
}

/*
=================
SpinControl_DoSlide
=================
*/
static int SpinControl_DoSlide( menuSpinControl_t *s, int dir ) {
	int		oldvalue = s->curvalue;

	s->curvalue += dir;

	if( s->curvalue < 0 ) {
		s->curvalue = s->numItems - 1;
	} else if( s->curvalue >= s->numItems ) {
		s->curvalue = 0;
	}

	UI_CALLBACK( s, QM_CHANGE, oldvalue );

	return QMS_MOVE;
}

/*
=================
SpinControl_Draw
=================
*/
static void SpinControl_Draw( menuSpinControl_t *s ) {
	UI_DrawString( s->generic.x + LCOLUMN_OFFSET, s->generic.y, NULL,
		s->generic.uiFlags | UI_RIGHT | UI_ALTCOLOR, s->generic.name );

	if( s->generic.flags & QMF_HASFOCUS ) {
		if( ( uis.realtime >> 8 ) & 1 ) {
			UI_DrawChar( s->generic.x + RCOLUMN_OFFSET / 2, s->generic.y,
				s->generic.uiFlags | UI_RIGHT, 13 );
		}
	}

	UI_DrawString( s->generic.x + RCOLUMN_OFFSET, s->generic.y, NULL,
		s->generic.uiFlags, s->itemnames[s->curvalue] );
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
void MenuList_ValidatePrestep( menuList_t *l ) {
	if( l->numItems <= l->maxItems || l->prestep < 0 ) {
		l->prestep = 0;
		return;
	}
	
	if( l->prestep > l->numItems - l->maxItems ) {
		l->prestep = l->numItems - l->maxItems;
	}
}

/*
=================
MenuList_Init
=================
*/
void MenuList_Init( menuList_t *l ) {
	const char	**n;
	int		height;
	int		i;

	l->numItems = 0;
	if( l->itemnames ) {
		n = l->itemnames;
		while( *n ) {
			l->numItems++;
			n++;
		}
	}

	height = l->generic.height;
	if( l->drawNames ) {
		height -= MLIST_SPACING;
	}

	l->maxItems = height / MLIST_SPACING;

	clamp( l->curvalue, 0, l->numItems - 1 );

	MenuList_ValidatePrestep( l );

	l->generic.rect.x = l->generic.x;
	l->generic.rect.y = l->generic.y;

	l->generic.rect.width = 0;
	for( i = 0; i < l->numcolumns; i++ ) {
		l->generic.rect.width += l->columns[i].width;
	}

	l->generic.rect.height = l->generic.height;

}

/*
=================
MenuList_SetValue
=================
*/
void MenuList_SetValue( menuList_t *l, int value ) {
	clamp( value, 0, l->numItems - 1 );

	l->curvalue = value;

	// set prestep accordingly
	if( l->numItems > l->maxItems  ) {
		if( l->prestep > l->curvalue ) {
			l->prestep = l->curvalue;
		} else if( l->prestep < l->curvalue - l->maxItems + 1 ) {
			l->prestep = l->curvalue - l->maxItems + 1;
		}
	} else {
		l->prestep = 0;
	}

	UI_CALLBACK( l, QM_CHANGE, l->curvalue );
}


/*
=================
MenuList_HitTest
=================
*/
int MenuList_HitTest( menuList_t *l, int mx, int my ) {
	const char **n;
	int i;
	vrect_t rect;

	if( !l->itemnames ) {
		return -1;
	}

	rect.x = l->generic.rect.x;
	rect.y = l->generic.rect.y;
	rect.width = l->generic.rect.width;
	rect.height = MLIST_SPACING;

	if( l->drawNames ) {
		rect.y += MLIST_SPACING;
	}

	n = l->itemnames + l->prestep;
	for( i = 0; i < l->maxItems && *n; i++ ) {
		if( UI_CursorInRect( &rect, mx, my ) ) {
			return n - l->itemnames;
		}
		
		rect.y += MLIST_SPACING;
		n++;
	
	}

	return -1;
}

/*
=================
MenuList_Key
=================
*/
static int MenuList_Key( menuList_t *l, int key ) {
	int i;

	if( !l->itemnames ) {
		return QMS_NOTHANDLED;
	}

	if( key > 32 && key < 127 ) {
		const char **n;

        if( uis.realtime > l->scratchTime + 1300 ) {
            l->scratchCount = 0;
            l->scratchTime = uis.realtime;
        }

        if( l->scratchCount >= sizeof( l->scratch ) - 1 ) {
		    return QMS_NOTHANDLED;
        }

        l->scratch[l->scratchCount++] = key;
        l->scratch[l->scratchCount] = 0;

        //l->scratchTime = uis.realtime;

        if( !Q_stricmpn( l->itemnames[l->curvalue],
            l->scratch, l->scratchCount ) )
        {
            return QMS_NOTHANDLED;
        }

		i = 0;
		n = l->itemnames;
		while( *n ) {
			if( !Q_stricmpn( *n, l->scratch, l->scratchCount ) ) {
				MenuList_SetValue( l, i );
				return QMS_SILENT;
			}
			i++;
			n++;
		}

		return QMS_NOTHANDLED;
	}

    l->scratchCount = 0;

	switch( key ) {
	case K_UPARROW:
	case K_KP_UPARROW:
		if( l->curvalue > 0 ) {
			l->curvalue--;
			UI_CALLBACK( l, QM_CHANGE, l->curvalue );

			// scroll contents up
			if( l->prestep > l->curvalue ) {
				l->prestep = l->curvalue;
			}
			return QMS_MOVE;
		}
		return QMS_BEEP;

	case K_DOWNARROW:
	case K_KP_DOWNARROW:
		if( l->curvalue < l->numItems - 1 ) {
			l->curvalue++;
			UI_CALLBACK( l, QM_CHANGE, l->curvalue );

			// scroll contents down
			if( l->prestep < l->curvalue - l->maxItems + 1 ) {
				l->prestep = l->curvalue - l->maxItems + 1;
			}
			return QMS_MOVE;
		}
		return QMS_BEEP;

	case K_HOME:
	case K_KP_HOME:
		l->prestep = 0;
		l->curvalue = 0;
		UI_CALLBACK( l, QM_CHANGE, l->curvalue );
		return QMS_MOVE;

	case K_END:
	case K_KP_END:
		if( l->numItems > l->maxItems ) {
			l->prestep = l->numItems - l->maxItems;
		}
		l->curvalue = l->numItems - 1;
		UI_CALLBACK( l, QM_CHANGE, l->curvalue );
		return QMS_MOVE;

	case K_MWHEELUP:
		if( keys.IsDown( K_CTRL ) ) {
			l->prestep -= 4;
		} else {
			l->prestep -= 2;
		}
		MenuList_ValidatePrestep( l );
		return QMS_SILENT;

	case K_MWHEELDOWN:
		if( keys.IsDown( K_CTRL ) ) {
			l->prestep += 4;
		} else {
			l->prestep += 2;
		}
		MenuList_ValidatePrestep( l );
		return QMS_SILENT;

	case K_PGUP:
	case K_KP_PGUP:
		l->prestep -= l->maxItems;
		MenuList_ValidatePrestep( l );
		return QMS_SILENT;

	case K_PGDN:
	case K_KP_PGDN:
		l->prestep += l->maxItems;
		MenuList_ValidatePrestep( l );
		return QMS_SILENT;

	case K_MOUSE1:
		i = MenuList_HitTest( l, uis.mouseCoords[0], uis.mouseCoords[1] );
		if( i != -1 ) {
			if( l->curvalue == i && uis.realtime - l->clickTime < DOUBLE_CLICK_DELAY ) {
				return UI_CALLBACK( l, QM_ACTIVATE, i );
			}
			l->clickTime = uis.realtime;
			l->curvalue = i;
			UI_CALLBACK( l, QM_CHANGE, i );
		}
		return QMS_SILENT;
    case K_MOUSE2:
    case K_MOUSE3:
		i = MenuList_HitTest( l, uis.mouseCoords[0], uis.mouseCoords[1] );
        if( i != -1 ) {
			l->curvalue = i;
			UI_CALLBACK( l, QM_CHANGE, i );
        }
        return QMS_SILENT;
	}

	return QMS_NOTHANDLED;
}

/*
=================
MenuList_MouseMove
=================
*/
int MenuList_MouseMove( menuList_t *l ) {
	return QMS_NOTHANDLED;
}

/*
=================
MenuList_DrawcolumnString
=================
*/
static void MenuList_DrawcolumnString( int x, int y, uint32 flags,
									   menuListColumn_t *column,
									   const char *string )
{
	clipRect_t rc;

	rc.left = x;
	rc.right = x + column->width - 1;
	rc.top = 0;
	rc.bottom = 0;

	if( ( column->uiFlags & UI_CENTER ) == UI_CENTER ) {
		x += column->width / 2;
	} else if( column->uiFlags & UI_RIGHT ) {
		x += column->width - MLIST_PRESTEP;
	} else {
		x += MLIST_PRESTEP;
	}

	ref.SetClipRect( DRAW_CLIP_RIGHT|DRAW_CLIP_LEFT, &rc );
	UI_DrawString( x + 1, y + 1, NULL, column->uiFlags | flags, string );
    if( uis.glconfig.renderer == GL_RENDERER_SOFTWARE ) {
        ref.SetClipRect( DRAW_CLIP_MASK, &uis.clipRect );
    } else {
        ref.SetClipRect( DRAW_CLIP_DISABLED, NULL );
    }
}



/*
=================
MenuList_Draw
=================
*/
static void MenuList_Draw( menuList_t *l ) {
	const char **n;
	int x, y, xx, yy;
	int i, j;
	int width, height;
	const char *s;
	vrect_t rect;
	float pageFrac, prestepFrac;
	int barHeight;

	if( !l->itemnames ) {
		return;
	}

	x = l->generic.rect.x;
	y = l->generic.rect.y;
	width = l->generic.rect.width;
	height = l->generic.rect.height;

	// draw header
	if( l->drawNames ) {
		xx = x;
		for( j = 0; j < l->numcolumns; j++ ) {
			ref.DrawFillEx( xx, y,
				l->columns[j].width - 1,
				MLIST_SPACING - 1,
				colorField );

			if( l->columns[j].name ) {
				MenuList_DrawcolumnString( xx, y, UI_ALTCOLOR,
					&l->columns[j], l->columns[j].name );
			}
			xx += l->columns[j].width;
		}
		y += MLIST_SPACING;
		height -= MLIST_SPACING;
	}

	if( !( l->mlFlags & MLF_HIDE_SCROLLBAR ) &&
		( !( l->mlFlags & MLF_HIDE_SCROLLBAR_EMPTY ) || l->numItems > l->maxItems ) )
	{
		barHeight = height - MLIST_SPACING * 2;
		yy = y + MLIST_SPACING;

		if( !( l->mlFlags & MLF_HIDE_BACKGROUND ) ) {
			rect.x = x + width;
			rect.y = yy;
			rect.width = MLIST_SCROLLBAR_WIDTH - 1;
			rect.height = barHeight;

			// draw scrollbar background
			UIS_FillRectEx( &rect, colorField );
		}

		if( l->numItems > l->maxItems ) {
			pageFrac = ( float )l->maxItems / l->numItems;
			prestepFrac = ( float )l->prestep / l->numItems;
		} else {
			pageFrac = 1;
			prestepFrac = 0;
		}

		rect.x = x + width;
		rect.y = yy + Q_rint( barHeight * prestepFrac );
		rect.width = MLIST_SCROLLBAR_WIDTH - 1;
		rect.height = Q_rint( barHeight * pageFrac );

		// draw scrollbar thumb
		UIS_FillRectEx( &rect, colorField );

	}

	xx = x;
	for( j = 0; j < l->numcolumns; j++ ) {
		ref.DrawFillEx( xx, y,
			l->columns[j].width - 1,
			height,
			colorField );
		
		xx += l->columns[j].width;
	}

	yy = y;
	n = l->itemnames + l->prestep;
	for( i = 0; i < l->maxItems && *n; i++ ) {
		// draw selection
		if( !( l->generic.flags & QMF_DISABLED ) && ( n - l->itemnames == l->curvalue ) ) {
			ref.DrawFillEx( x, yy, width - 1, MLIST_SPACING, colorField );
		}

		// draw contents
		s = *n;
		xx = x;
		for( j = 0; j < l->numcolumns; j++ ) {
			if( !*s ) {
				break;
			}
			
			MenuList_DrawcolumnString( xx, yy, 0, &l->columns[j], s );
			
			xx += l->columns[j].width;
			s += strlen( s ) + 1;
		}

		yy += MLIST_SPACING;
		n++;
	}
}

/*
=================
MenuList_GetSize
=================
*/
void MenuList_GetSize( vrect_t *rc, menuList_t *l ) {
	rc->x = l->generic.rect.x;
	rc->y = l->generic.rect.y;
	rc->width = l->generic.rect.width;
	rc->height = l->generic.rect.height;

	// TODO: move this somewhere else...
	if( !( l->mlFlags & MLF_HIDE_SCROLLBAR ) ) {
		rc->width += MLIST_SCROLLBAR_WIDTH;
	}
}

/*
===================================================================

IMAGE LIST CONTROL

===================================================================
*/

/*
=================
ImageList_NumItems
=================
*/
static int ImageList_NumItems( const imageList_t *l ) {
	const qhandle_t *n;
	int  numItems;

	numItems = 0;
	n = l->images;
	while( *n ) {
		numItems++;
		n++;
	}

	return numItems;
}

/*
=================
ImageList_HitTest
=================
*/
int ImageList_HitTest( imageList_t *l, int mx, int my ) {
	int i, j;
	int xx, yy;
	const qhandle_t *pics;
	vrect_t rect;

	pics = l->images + l->prestep;


	xx = l->generic.x;
	yy = l->generic.y;

	if( l->name ) {
		yy += SMALLCHAR_HEIGHT * 2;
	}

	for( i=0 ; i<l->numRows && *pics ; i++ ) {
		xx = l->generic.x;
		for( j=0 ; j<l->numcolumns && *pics ; j++ ) {
			rect.x = xx;
			rect.y = yy;
			rect.width = l->imageWidth;
			rect.height = l->imageHeight;
			if( UI_CursorInRect( &rect, mx, my ) ) {
				return l->prestep + i * l->numcolumns + j;
			}
			
			xx += SMALLCHAR_WIDTH * 2 + l->imageWidth;
			pics++;
		
		}
		yy += SMALLCHAR_HEIGHT * 2 + l->imageHeight;
		
	}

	return -1;
}

/*
=================
ImageList_UpdatePos
=================
*/
static void ImageList_UpdatePos( imageList_t *l ) {
	l->prestep = l->curvalue - l->curvalue % ( l->numRows * l->numcolumns );
	UI_CALLBACK( l, QM_CHANGE, l->curvalue );
}

/*
=================
ImageList_Key
=================
*/
static int ImageList_Key( imageList_t *l, int key ) {
	int i;
	int screenPos, numItems;

	if( key > 32 && key < 127 ) {
		const char **n;

		i = 0;
		n = l->names;
		while( *n ) {
			if( Q_tolower( *n[0] ) == Q_tolower( key ) ) {
				l->curvalue = i;
				ImageList_UpdatePos( l );
				return QMS_SILENT;
			}
			i++;
			n++;
		}

		return QMS_NOTHANDLED;
	}

	screenPos = l->curvalue % ( l->numRows * l->numcolumns );
	numItems = ImageList_NumItems( l );

	switch( key ) {
	case K_UPARROW:
	case K_KP_UPARROW:
		if( screenPos >= l->numcolumns ) {
			l->curvalue -= l->numcolumns;
			ImageList_UpdatePos( l );
			return QMS_MOVE;
		}
		return QMS_BEEP;
	case K_DOWNARROW:
	case K_KP_DOWNARROW:
		if( screenPos < l->numRows * ( l->numcolumns - 1 ) && l->curvalue + l->numcolumns < numItems ) {
			l->curvalue += l->numcolumns;
			ImageList_UpdatePos( l );
			return QMS_MOVE;
		}
		return QMS_BEEP;
	case K_LEFTARROW:
	case K_KP_LEFTARROW:
		if( screenPos % l->numcolumns > 0 ) {
			l->curvalue--;
			ImageList_UpdatePos( l );
			return QMS_MOVE;
		}
		if( l->curvalue >= l->numRows * l->numcolumns ) {
			l->curvalue -= l->numRows * ( l->numcolumns - 1 ) + 1;
			ImageList_UpdatePos( l );
			return QMS_MOVE;
		}
		return QMS_BEEP;
	case K_RIGHTARROW:
	case K_KP_RIGHTARROW:
		if( screenPos % l->numcolumns < l->numcolumns - 1 && l->curvalue < numItems - 1 ) {
			l->curvalue++;
			ImageList_UpdatePos( l );
			return QMS_MOVE;
		}
		if( l->curvalue - l->curvalue % ( l->numRows * l->numcolumns ) < numItems - numItems % ( l->numRows * l->numcolumns ) ) {
			l->curvalue += l->numRows * ( l->numcolumns - 1 ) + 1;
			if( l->curvalue > numItems - 1 ) {
				l->curvalue = ( numItems - 1 ) - ( numItems - 1 ) % l->numcolumns;
			}
			ImageList_UpdatePos( l );
			return QMS_MOVE;
		}
		return QMS_BEEP;
	case K_HOME:
	case K_KP_HOME:
		l->prestep = 0;
		return QMS_SILENT;
	case K_END:
	case K_KP_END:
		l->prestep = ImageList_NumItems( l ) - l->numRows * l->numcolumns;
		return QMS_SILENT;
	case K_MWHEELUP:
	case K_PGUP:
	case K_KP_PGUP:
		if( l->prestep > 0 ) {
			l->prestep -= l->numRows * l->numcolumns;
		}
		return QMS_SILENT;
	case K_MWHEELDOWN:
	case K_PGDN:
	case K_KP_PGDN:
		if( l->prestep < ImageList_NumItems( l ) - l->numRows * l->numcolumns ) {
			l->prestep += l->numRows * l->numcolumns;
		}
		return QMS_SILENT;
	case K_MOUSE1:
		i = ImageList_HitTest( l, uis.mouseCoords[0], uis.mouseCoords[1] );
		if( i != -1 ) {
			if( l->curvalue == i && uis.realtime - l->clickTime < DOUBLE_CLICK_DELAY ) {
				return UI_CALLBACK( l, QM_ACTIVATE, i );
			}
			l->clickTime = uis.realtime;
			l->curvalue = i;
			ImageList_UpdatePos( l );
		}
		return QMS_SILENT;
	}

	return QMS_NOTHANDLED;
}

/*
=================
ImageList_Draw
=================
*/
static void ImageList_Draw( imageList_t *l ) {
#if 0
	int i, j;
	int xx, yy;
	const char **n;
	const qhandle_t *pics;
	color_t color = { 15, 128, 235, 255 };

	pics = l->images + l->prestep;
	n = l->names  + l->prestep;

	xx = l->generic.x;
	yy = l->generic.y;

	if( l->name ) {
		UI_DrawString( xx + ( l->imageWidth + SMALLCHAR_WIDTH * 2 ) * l->numcolumns / 2, yy, colorGreen, UI_CENTER, l->name );
		yy += SMALLCHAR_HEIGHT * 2;
	}

	for( i=0 ; i<l->numRows && *pics ; i++ ) {
		xx = l->generic.x;
		for( j=0 ; j<l->numcolumns && *pics ; j++ ) {
			if( i * l->numcolumns + j == l->curvalue - l->prestep ) {
				
				color[3] = 255;
		
				UIS_DrawStretchPicByName( xx - 6, yy - 6, l->imageWidth + 12, l->imageHeight + 12, "frame" );
			}

			ref.DrawStretchPic( xx, yy, l->imageWidth, l->imageHeight, *pics );

			if( *n ) {
				UI_DrawString( xx + l->imageWidth / 2, yy + l->imageHeight, NULL, UI_CENTER, *n );
			}
			xx += SMALLCHAR_WIDTH * 2 + l->imageWidth;
			pics++;
			n++;
		}
		yy += SMALLCHAR_HEIGHT * 2 + l->imageHeight;
		
	}
#endif
}

/*
=================
ImageList_GetSize
=================
*/
void ImageList_GetSize( vrect_t *rc, imageList_t *l ) {
	rc->x = l->generic.x;
	rc->y = l->generic.y;
	rc->width = l->numcolumns * ( l->imageWidth + SMALLCHAR_WIDTH * 2 );
	rc->height = l->numRows * ( l->imageHeight + SMALLCHAR_HEIGHT * 2 );
}

/*
===================================================================

SLIDER CONTROL

===================================================================
*/

#define SLIDER_RANGE 10

static void Slider_Init( menuSlider_t *s ) {
	if( s->curvalue > s->maxvalue )
		s->curvalue = s->maxvalue;
	else if( s->curvalue < s->minvalue )
		s->curvalue = s->minvalue;
}

static int Slider_Key( menuSlider_t *s, int key ) {
	switch( key ) {
	case K_END:
		s->curvalue = s->maxvalue;
		return QMS_MOVE;
	case K_HOME:
		s->curvalue = s->minvalue;
		return QMS_MOVE;
	}

	return QMS_NOTHANDLED;
}


/*
=================
Slider_DoSlide
=================
*/
static int Slider_DoSlide( menuSlider_t *s, int dir ) {
	int ret;

	s->curvalue += dir;

	if( s->curvalue > s->maxvalue )
		s->curvalue = s->maxvalue;
	else if( s->curvalue < s->minvalue )
		s->curvalue = s->minvalue;
	
	if( ( ret = UI_CALLBACK( s, QM_CHANGE, s->curvalue ) ) != QMS_NOTHANDLED ) {
		return ret;
	}

	return QMS_SILENT;
}

/*
=================
Slider_Draw
=================
*/
static void Slider_Draw( menuSlider_t *s ) {
	int	i, flags;
    float pos;

	flags = s->generic.uiFlags & ~( UI_LEFT | UI_RIGHT );

	if( s->generic.flags & QMF_HASFOCUS ) {
		if( ( uis.realtime >> 8 ) & 1 ) {
			UI_DrawChar( s->generic.x + RCOLUMN_OFFSET / 2, s->generic.y, s->generic.uiFlags | UI_RIGHT, 13 );
		}
	}

	UI_DrawString( s->generic.x + LCOLUMN_OFFSET, s->generic.y, NULL,
		flags | UI_RIGHT | UI_ALTCOLOR, s->generic.name );

	UI_DrawChar( s->generic.x + RCOLUMN_OFFSET, s->generic.y, flags | UI_LEFT, 128 );

	for( i = 0 ; i < SLIDER_RANGE ; i++ )
		UI_DrawChar( RCOLUMN_OFFSET + s->generic.x + i * SMALLCHAR_WIDTH + SMALLCHAR_WIDTH, s->generic.y, flags | UI_LEFT, 129 );

	UI_DrawChar( RCOLUMN_OFFSET + s->generic.x + i * SMALLCHAR_WIDTH + SMALLCHAR_WIDTH, s->generic.y, flags | UI_LEFT, 130 );

    if( s->maxvalue <= s->minvalue ) {
        pos = 0;
    } else {
	    pos = ( s->curvalue - s->minvalue ) /
            ( float )( s->maxvalue - s->minvalue );
    	clamp( pos, 0, 1 );
    }

	UI_DrawChar( SMALLCHAR_WIDTH + RCOLUMN_OFFSET + s->generic.x + ( SLIDER_RANGE - 1 ) * SMALLCHAR_WIDTH * pos, s->generic.y, flags | UI_LEFT, 131 );
}

/*
=================
Slider_GetSize
=================
*/
static void Slider_GetSize( vrect_t *rc, menuSlider_t *s ) {
	int len = strlen( s->generic.name ) * SMALLCHAR_WIDTH;

	rc->x = s->generic.x + LCOLUMN_OFFSET - len;
	rc->y = s->generic.y;

	rc->width = 32 + len + ( SLIDER_RANGE + 2 ) * SMALLCHAR_WIDTH;
	rc->height = SMALLCHAR_HEIGHT;
}


/*
===================================================================

MISC

===================================================================
*/

/*
=================
Separator_Draw
=================
*/
static void Separator_Draw( menuSeparator_t *s ) {
	if( s->generic.name )
		UI_DrawString( s->generic.x, s->generic.y, NULL, UI_RIGHT, s->generic.name );
}

/*
=================
Menu_AddItem
=================
*/
void Menu_AddItem( menuFrameWork_t *menu, void *item ) {
	if( menu->nitems >= MAXMENUITEMS ) {
		return;
	}

	menu->items[menu->nitems++] = item;
	((menuCommon_t *)item)->parent = menu;

	switch( ((menuCommon_t *)item)->type ) {
	case MTYPE_FIELD:
		Field_Init( (menuField_t *)item );
		break;
	case MTYPE_SLIDER:
		Slider_Init( (menuSlider_t *)item );
		break;
	case MTYPE_LIST:
		MenuList_Init( (menuList_t *)item );
		break;
	case MTYPE_SPINCONTROL:
		SpinControl_Init( (menuSpinControl_t *)item );
		break;
	case MTYPE_ACTION:
		Action_Init( (menuAction_t *)item );
		break;
	case MTYPE_SEPARATOR:
		//Separator_Init( (menuSeparator_t *)item );
		break;
	case MTYPE_BITMAP:
		Bitmap_Init( (menuBitmap_t *)item );
		break;
	case MTYPE_IMAGELIST:
		//ImageList_Init( (imageList_t *)item );
		break;
	case MTYPE_STATIC:
		Static_Init( (menuStatic_t *)item );
		break;
	case MTYPE_KEYBIND:
		Keybind_Init( (menuKeybind_t *)item );
		break;
	default:
		Com_Error( ERR_FATAL, "Menu_AddItem: unknown item type" );
		break;
	}
	

}

menuCommon_t *Menu_ItemAtCursor( menuFrameWork_t *m ) {
	menuCommon_t *item;
	int i;

	for( i=0 ; i<m->nitems ; i++ ) {
		item = (menuCommon_t *)m->items[i];

		if( item->flags & QMF_HASFOCUS ) {
			return item;
		}
	}

	return NULL;

}

void Menu_SetFocus( menuCommon_t *focus ) {
	menuFrameWork_t *menu;
	menuCommon_t *item;
	int i;

	if( focus->flags & QMF_HASFOCUS ) {
		return;
	}

	menu = focus->parent;

	for( i=0 ; i<menu->nitems ; i++ ) {
		item = (menuCommon_t *)menu->items[i];

		if( item == focus ) {
			item->flags |= QMF_HASFOCUS;
			menu->callback( item->id, QM_GOTFOCUS, 0 );
		} else if( item->flags & QMF_HASFOCUS ) {
			item->flags &= ~QMF_HASFOCUS;
			menu->callback( item->id, QM_LOSTFOCUS, 0 );
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
int Menu_AdjustCursor( menuFrameWork_t *m, int dir ) {
	menuCommon_t *item;
	int cursor, pos;
	int i;

	pos = 0;
	for( i=0 ; i<m->nitems ; i++ ) {
		item = (menuCommon_t *)m->items[i];

		if( item->flags & QMF_HASFOCUS ) {
			pos = i;
			break;
		}
	}

	/*
	** crawl in the direction indicated until we find a valid spot
	*/
    cursor = pos;
	if( dir == 1 ) {
		do {
			cursor++;
			if( cursor >= m->nitems )
				cursor = 0;

			item = (menuCommon_t *)m->items[cursor];
			if( UI_IsItemSelectable( item ) )
				break;
		} while( cursor != pos );
	} else {
		do {
			cursor--;
			if( cursor < 0 )
				cursor = m->nitems - 1;

			item = (menuCommon_t *)m->items[cursor];
			if( UI_IsItemSelectable( item ) )
				break;
		} while( cursor != pos );
	}

	Menu_SetFocus( item );

	return QMS_MOVE;
}





/*
=================
Menu_Draw
=================
*/
void Menu_Draw( menuFrameWork_t *menu ) {
	menuCommon_t *item;
	int i;

//
// draw contents
//
	for( i = 0; i < menu->nitems; i++ ) {
		item = ( menuCommon_t * )menu->items[i];
		if( item->flags & QMF_HIDDEN ) {
			continue;
		}

		switch( item->type ) {
		case MTYPE_FIELD:
			Field_Draw( (menuField_t *)item );
			break;
		case MTYPE_SLIDER:
			Slider_Draw( (menuSlider_t *)item );
			break;
		case MTYPE_LIST:
			MenuList_Draw( (menuList_t *)item );
			break;
		case MTYPE_SPINCONTROL:
			SpinControl_Draw( (menuSpinControl_t *)item );
			break;
		case MTYPE_ACTION:
			Action_Draw( (menuAction_t *)item );
			break;
		case MTYPE_SEPARATOR:
			Separator_Draw( (menuSeparator_t *)item );
			break;
		case MTYPE_BITMAP:
			Bitmap_Draw( (menuBitmap_t *)item );
			break;
		case MTYPE_IMAGELIST:
			ImageList_Draw( (imageList_t *)item );
			break;
		case MTYPE_STATIC:
			Static_Draw( (menuStatic_t *)item );
			break;
		case MTYPE_KEYBIND:
			Keybind_Draw( (menuKeybind_t *)item );
			break;
		default:
			Com_Error( ERR_FATAL, "Menu_Draw: unknown item type" );
			break;
		}
	}

//
// draw status bar
//
	if( menu->statusbar ) {
		ref.DrawFill( 0, uis.glconfig.vidHeight - 8, uis.glconfig.vidWidth, 8, 4 );
		UI_DrawString( uis.glconfig.vidWidth / 2, uis.glconfig.vidHeight - 8, NULL, UI_CENTER, menu->statusbar );
	}

}

int Menu_SelectItem( menuFrameWork_t *s ) {
	menuCommon_t *item;

	if( !( item = Menu_ItemAtCursor( s ) ) ) {
		return QMS_NOTHANDLED;
	}

	switch( item->type ) {
	//case MTYPE_SLIDER:
	//	return Slider_DoSlide( (menuSlider_t *)item, 1 );
	case MTYPE_SPINCONTROL:
		return SpinControl_DoEnter( (menuSpinControl_t *)item );
	case MTYPE_FIELD:
	case MTYPE_ACTION:
	case MTYPE_BITMAP:
	case MTYPE_LIST:
	case MTYPE_IMAGELIST:
	case MTYPE_KEYBIND:
		return Common_DoEnter( item );
	}

	return QMS_NOTHANDLED;
}



int Menu_SlideItem( menuFrameWork_t *s, int dir ) {
	menuCommon_t *item;

	if( !( item = Menu_ItemAtCursor( s ) ) ) {
		return QMS_NOTHANDLED;
	}

	switch( item->type ) {
	case MTYPE_SLIDER:
		return Slider_DoSlide( (menuSlider_t *)item, dir );
	case MTYPE_SPINCONTROL:
		return SpinControl_DoSlide( (menuSpinControl_t *)item, dir );
	}

	return QMS_NOTHANDLED;
	
}

int Menu_KeyEvent( menuCommon_t *item, int key ) {
	int ret;

	if( ( ret = item->parent->callback( item->id, QM_KEY, key ) ) != QMS_NOTHANDLED ) {
		return ret;
	}

	switch( item->type ) {
	case MTYPE_FIELD:
		return Field_Key( (menuField_t *)item, key );
	case MTYPE_LIST:
		return MenuList_Key( (menuList_t *)item, key );
	case MTYPE_IMAGELIST:
		return ImageList_Key( (imageList_t *)item, key );
	case MTYPE_SLIDER:
		return Slider_Key( (menuSlider_t *)item, key );
	}

	return QMS_NOTHANDLED;
}

int Menu_CharEvent( menuCommon_t *item, int key ) {
	int ret;

	if( ( ret = item->parent->callback( item->id, QM_CHAR, key ) ) != QMS_NOTHANDLED ) {
		return ret;
	}

	switch( item->type ) {
	case MTYPE_FIELD:
		return Field_Char( (menuField_t *)item, key );
	}

	return QMS_NOTHANDLED;
}

int Menu_MouseMove( menuCommon_t *item ) {
	int ret;

	if( ( ret = item->parent->callback( item->id, QM_MOUSE, 0 ) ) != QMS_NOTHANDLED ) {
		return ret;
	}

	switch( item->type ) {
	//case MTYPE_FIELD:
		//return Field_MouseMove( (menuField_t *)item );
	case MTYPE_LIST:
		return MenuList_MouseMove( (menuList_t *)item );
	//case MTYPE_IMAGELIST:
		//return ImageList_MouseMove( (imageList_t *)item );
	}

	return QMS_NOTHANDLED;
}



menuCommon_t *Menu_HitTest( menuFrameWork_t *menu, int mx, int my ) {
	vrect_t rect;
	int i;
	menuCommon_t *item;
	
	for( i=0 ; i<menu->nitems ; i++ ) {
		rect.x = rect.y = 999999;
		rect.width = rect.height = -999999;

		item = (menuCommon_t *)menu->items[i];

		if( item->flags & QMF_HIDDEN ) {
			continue;
		}

		switch( item->type ) {
		default:
			rect = item->rect;
			break;
		case MTYPE_SLIDER:
			Slider_GetSize( &rect, (menuSlider_t *)item );
			break;
		case MTYPE_LIST:
			MenuList_GetSize( &rect, (menuList_t *)item );
			break;
		case MTYPE_SEPARATOR:
			break;
		case MTYPE_IMAGELIST:
			ImageList_GetSize( &rect, (imageList_t *)item );
			break;
		}

		if( ui_debug->integer ) {
			UIS_DrawRect( &rect, 1, 223 );
		}

		if( UI_CursorInRect( &rect, mx, my ) ) {
			return item;
		}

	}

	return NULL;
	
}


