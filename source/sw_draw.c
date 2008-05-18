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

// draw.c

#include "sw_local.h"


//=============================================================================



#define DOSTEP do {				\
            tbyte = *src++;     \
            if( tbyte != 255 )  \
                *dst = tbyte;	\
            dst++;              \
		} while( 0 )

#define DOMSTEP do {			\
            if( *src++ != 255 )  \
                *dst = tbyte;	\
            dst++;              \
		} while( 0 )

#define DOSTRETCH do {				\
			tbyte = src[u >> 16];	\
            if( tbyte != 255 )      \
                *dst = tbyte;	    \
            dst++;                  \
			u += ustep;				\
		} while( 0 )

#define ROW1 do {	\
		DOSTEP;		\
	} while( --count );

#define ROW4 do {	\
		DOSTEP;		\
		DOSTEP;		\
		DOSTEP;		\
		DOSTEP;		\
					\
		count -= 4;	\
	} while( count );

#define ROW8 do {	\
		DOSTEP;		\
		DOSTEP;		\
		DOSTEP;		\
		DOSTEP;		\
		DOSTEP;		\
		DOSTEP;		\
		DOSTEP;		\
		DOSTEP;		\
					\
		count -= 8;	\
	} while( count );

#define MROW8 do {	\
		DOMSTEP;		\
		DOMSTEP;		\
		DOMSTEP;		\
		DOMSTEP;		\
		DOMSTEP;		\
		DOMSTEP;		\
		DOMSTEP;		\
		DOMSTEP;		\
					\
		count -= 8;	\
	} while( count );

#define STRETCH1 do {	\
		DOSTRETCH;		\
	} while( --count );

#define STRETCH4 do {	\
		DOSTRETCH;		\
		DOSTRETCH;		\
		DOSTRETCH;		\
		DOSTRETCH;		\
						\
		count -= 4;		\
	} while( count );

#define STRETCH8 do {	\
		DOSTRETCH;		\
		DOSTRETCH;		\
		DOSTRETCH;		\
		DOSTRETCH;		\
		DOSTRETCH;		\
		DOSTRETCH;		\
		DOSTRETCH;		\
		DOSTRETCH;		\
						\
		count -= 8;		\
	} while( count );

typedef struct {
	int colorIndex;
	int colorFlags;
	clipRect_t clipRect;
	int flags;
} drawStatic_t;

static drawStatic_t	draw;

static int	colorIndices[8];

void Draw_Init( void ) {
	int i;

	memset( &draw, 0, sizeof( draw ) );
	draw.colorIndex = -1;

	for( i = 0; i < 8; i++ ) {
		colorIndices[i] = R_IndexForColor( colorTable[i] );
	}
}

void Draw_SetColor( int flags, const color_t color ) {
	draw.flags &= ~DRAW_COLOR_MASK;

	if( flags == DRAW_COLOR_CLEAR ) {
		draw.colorIndex = -1;
		return;
	}

	if( flags == DRAW_COLOR_ALPHA ) {
		return;
	}

	if( flags == DRAW_COLOR_INDEXED ) {
		draw.colorIndex = *( uint32_t * )color & 255;
		return;
	}

	if( flags & DRAW_COLOR_RGB ) {
		draw.colorIndex = R_IndexForColor( color );
	}
	if( flags & DRAW_COLOR_ALPHA ) {
	}

	draw.flags |= flags;
}

void Draw_SetClipRect( int flags, const clipRect_t *clip ) {
	draw.flags &= ~DRAW_CLIP_MASK;

	if( flags == DRAW_CLIP_DISABLED ) {
		return;
	}
	draw.flags |= flags;
	draw.clipRect = *clip;
}

/*
=============
Draw_GetPicSize
=============
*/
qboolean Draw_GetPicSize( int *w, int *h, qhandle_t hPic ) {
	image_t *gl;

	gl = R_ImageForHandle( hPic );
    if( w ) {
    	*w = gl->width;
    }
    if( h ) {
    	*h = gl->height;
    }
    return gl->flags & if_transparent;
}

qhandle_t R_RegisterFont( const char *name ) {
    qhandle_t R_RegisterPic( const char *name );

    return R_RegisterPic( name );
}

/*
=============
Draw_StretchData
=============
*/
static void Draw_StretchData( int x, int y, int w, int h, int xx, int yy,
        int ww, int hh, int pitch, byte *data )
{
	byte			*srcpixels, *dstpixels, *dst, *src;
	int				v, u;
	int				ustep, vstep;
	int				skipv, skipu;
	int				width, height;
	byte			tbyte;
	int			count;

	skipv = skipu = 0;
	width = w;
	height = h;

    if( draw.flags & DRAW_CLIP_MASK ) {
	    clipRect_t *clip = &draw.clipRect;

        if( draw.flags & DRAW_CLIP_LEFT ) {
            if( x < clip->left ) {
                skipu = clip->left - x;
                if( w <= skipu ) {
                    return;
                }
                w -= skipu;
                x = clip->left;
            }
        }

        if( draw.flags & DRAW_CLIP_RIGHT ) {
            if( x >= clip->right ) {
                return;
            }
            if( x + w > clip->right ) {
                w = clip->right - x;
            }
        }

        if( draw.flags & DRAW_CLIP_TOP ) {
            if( y < clip->top ) {
                skipv = clip->top - y;
                if( h <= skipv ) {
                    return;
                }
                h -= skipv;
                y = clip->top;
            }
        }

        if( draw.flags & DRAW_CLIP_BOTTOM ) {
            if( y >= clip->bottom ) {
                return;
            }
            if( y + h > clip->bottom ) {
                h = clip->bottom - y;
            }
        }
    }

	srcpixels = data + yy * pitch + xx;
	dstpixels = vid.buffer + y * vid.rowbytes + x;

	vstep = hh * 0x10000 / height;
	
	v = skipv * vstep;
	if( width == ww ) {
	    dstpixels += skipu;
		do {
			src = &srcpixels[( v >> 16 ) * pitch];
		    dst = dstpixels;
			count = w;
			
			if( !( w & 7 ) ) {
				ROW8;
			} else if( !( w & 3 ) ) {
				ROW4;
			} else {
				ROW1;
			}
			
			v += vstep;
			dstpixels += vid.rowbytes;
		} while( --h );
	} else {
		ustep = ww * 0x10000 / width;
		skipu = skipu * ustep;
		do {
			src = &srcpixels[( v >> 16 ) * pitch];
			dst = dstpixels;
			count = w;

			u = skipu;
			if( !( w & 7 ) ) {
				STRETCH8;
			} else if( !( w & 3 ) ) {
				STRETCH4;
			} else {
				STRETCH1;
			}
			
			v += vstep;
			dstpixels += vid.rowbytes;
		} while( --h );
	}

}

/*
=============
Draw_FixedData
=============
*/
static void Draw_FixedData( int x, int y, int w, int h,
        int pitch, byte *data )
{
    byte *srcpixels, *dstpixels;
	byte			*dst, *src;
	int				skipv, skipu;
	byte			tbyte;
	int			count;

	skipv = skipu = 0;

    if( draw.flags & DRAW_CLIP_MASK ) {
	    clipRect_t *clip = &draw.clipRect;

        if( draw.flags & DRAW_CLIP_LEFT ) {
            if( x < clip->left ) {
                skipu = clip->left - x;
                if( w <= skipu ) {
                    return;
                }
                w -= skipu;
                x = clip->left;
            }
        }

        if( draw.flags & DRAW_CLIP_RIGHT ) {
            if( x >= clip->right ) {
                return;
            }
            if( x + w > clip->right ) {
                w = clip->right - x;
            }
        }

        if( draw.flags & DRAW_CLIP_TOP ) {
            if( y < clip->top ) {
                skipv = clip->top - y;
                if( h <= skipv ) {
                    return;
                }
                h -= skipv;
                y = clip->top;
            }
        }

        if( draw.flags & DRAW_CLIP_BOTTOM ) {
            if( y >= clip->bottom ) {
                return;
            }
            if( y + h > clip->bottom ) {
                h = clip->bottom - y;
            }
        }
	}

	srcpixels = data + skipv * pitch + skipu;
	dstpixels = vid.buffer + y * vid.rowbytes + x;

	if( !( w & 7 ) ) {
		do {
            src = srcpixels;
            dst = dstpixels;
            count = w; ROW8;
            srcpixels += pitch;
            dstpixels += vid.rowbytes;
        } while( --h );
	} else if( !( w & 3 ) ) {
		do {
            src = srcpixels;
            dst = dstpixels;
            count = w; ROW4;
            srcpixels += pitch;
            dstpixels += vid.rowbytes;
        } while( --h );
	} else {
		do {
            src = srcpixels;
            dst = dstpixels;
            count = w; ROW1;
            srcpixels += pitch;
            dstpixels += vid.rowbytes;
        } while( --h );
	}

}

static void Draw_FixedDataAsMask( int x, int y, int w, int h,
        int pitch, byte *data, byte tbyte )
{
    byte *srcpixels, *dstpixels;
	byte			*dst, *src;
	int				skipv, skipu;
	int			count;

	skipv = skipu = 0;

    if( draw.flags & DRAW_CLIP_MASK ) {
	    clipRect_t *clip = &draw.clipRect;

        if( draw.flags & DRAW_CLIP_LEFT ) {
            if( x < clip->left ) {
                skipu = clip->left - x;
                if( w <= skipu ) {
                    return;
                }
                w -= skipu;
                x = clip->left;
            }
        }

        if( draw.flags & DRAW_CLIP_RIGHT ) {
            if( x >= clip->right ) {
                return;
            }
            if( x + w > clip->right ) {
                w = clip->right - x;
            }
        }

        if( draw.flags & DRAW_CLIP_TOP ) {
            if( y < clip->top ) {
                skipv = clip->top - y;
                if( h <= skipv ) {
                    return;
                }
                h -= skipv;
                y = clip->top;
            }
        }

        if( draw.flags & DRAW_CLIP_BOTTOM ) {
            if( y <= clip->bottom ) {
                return;
            }
            if( y + h > clip->bottom ) {
                h = clip->bottom - y;
            }
        }
    }

	srcpixels = data + skipv * pitch + skipu;
	dstpixels = vid.buffer + y * vid.rowbytes + x;

	if( !( w & 7 ) ) {
		do {
            src = srcpixels;
            dst = dstpixels;
            count = w; MROW8;
            srcpixels += pitch;
            dstpixels += vid.rowbytes;
        } while( --h );
	} else if( !( w & 3 ) ) {
		do {
            src = srcpixels;
            dst = dstpixels;
            count = w; ROW4;
            srcpixels += pitch;
            dstpixels += vid.rowbytes;
        } while( --h );
	} else {
		do {
            src = srcpixels;
            dst = dstpixels;
            count = w; ROW1;
            srcpixels += pitch;
            dstpixels += vid.rowbytes;
        } while( --h );
	}

}

/*
=============
Draw_StretchPic
=============
*/
void Draw_StretchPicST( int x, int y, int w, int h, float s1, float t1,
        float s2, float t2, qhandle_t hPic )
{
	image_t *image;
	int xx, yy, ww, hh;

	image = R_ImageForHandle( hPic );

	xx = image->width * s1;
	yy = image->height * t1;
	ww = image->width * ( s2 - s1 );
	hh = image->height * ( t2 - t1 );

	//draw_current->drawStretchPic
		Draw_StretchData( x, y, w, h, xx, yy, ww, hh, image->width,
                image->pixels[0] );
}

/*
=============
Draw_StretchPic
=============
*/
void Draw_StretchPic( int x, int y, int w, int h, qhandle_t hPic ) {
	image_t *image;

	image = R_ImageForHandle( hPic );

    if( w == image->width && h == image->height ) {
		Draw_FixedData( x, y, image->width, image->height,
                image->width, image->pixels[0] );
        return;
    }

	//draw_current->drawStretchPic
		Draw_StretchData( x, y, w, h, 0, 0, image->width, image->height,
                image->width, image->pixels[0] );
}

/*
=============
Draw_StretchPic
=============
*/
void Draw_Pic( int x, int y, qhandle_t hPic ) {
	image_t *image;

	image = R_ImageForHandle( hPic );

	//draw_current->drawFixedPic
		Draw_FixedData( x, y, image->width, image->height,
                image->width, image->pixels[0] );

}

/*
=============
Draw_StretchRaw
=============
*/
void Draw_StretchRaw( int x, int y, int w, int h, int cols,
        int rows, const byte *data )
{
	Draw_StretchData( x, y, w, h, 0, 0, cols, rows, cols, ( byte * )data );
}

void Draw_Char( int x, int y, int flags, int ch, qhandle_t hFont ) {
	image_t *image;
    int xx, yy;
    byte *data;

	if( !hFont ) {
		return;
	}
	image = R_ImageForHandle( hFont );
	if( image->width != 128 || image->height != 128 ) {
		return;
	}

    xx = ( ch & 15 ) << 3;
    yy = ( ( ch >> 4 ) & 15 ) << 3;
    data = image->pixels[0] + yy * image->width + xx;
	if( draw.colorIndex != -1 && !( ch & 128 ) ) {
		Draw_FixedDataAsMask( x, y, 8, 8, image->width, data, draw.colorIndex );
	} else {
		Draw_FixedData( x, y, 8, 8, image->width, data );
	}
}

/*
===============
Draw_String
===============
*/
int Draw_String( int x, int y, int flags, size_t maxChars,
        const char *string, qhandle_t hFont )
{
    image_t *image;
	byte c, *data;
    int xx, yy;
    int color, mask;

	if( !hFont ) {
		return x;
	}
	image = R_ImageForHandle( hFont );
	if( image->width != 128 || image->height != 128 ) {
		return x;
	}

	mask = 0;
	if( flags & UI_ALTCOLOR ) {
		mask |= 128;
	}

	color = draw.colorIndex;

	while( *string ) {
        if( Q_IsColorString( string ) ) {
            string++;
            c = *string++;
            if( c == COLOR_ALT ) {
                mask |= 128;
            } else if( c == COLOR_RESET ) {
				color = draw.colorIndex;
                mask = 0;
                if( flags & UI_ALTCOLOR ) {
                    mask |= 128;
                }
            } else if( !( flags & UI_IGNORECOLOR ) ) {
                color = colorIndices[ ColorIndex( c ) ];
            }
            continue;
        }

		if( !maxChars-- ) {
			break;
		}

		if( !( c = *string++ ) ) {
			break;
		}

		c |= mask;

		if( ( c & 127 ) == 32 ) {
			x += 8;	/* optimized case */
			continue;
		}

        xx = ( c & 15 ) << 3;
        yy = ( c >> 4 ) << 3;
        data = image->pixels[0] + yy * image->width + xx;
		if( color != -1 && !( c & 128 ) ) {
			Draw_FixedDataAsMask( x, y, 8, 8, image->width, data, color );
		} else {
			Draw_FixedData( x, y, 8, 8, image->width, data );
		}

		x += 8;
	}
    return x;
}

/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear( int x, int y, int w, int h, qhandle_t hPic ) {
	int			i, j;
	byte		*psrc;
	byte		*pdest;
	image_t		*pic;
	int			x2;

	if( !hPic ) {
		return;
	}

	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if (x + w > vid.width)
		w = vid.width - x;
	if (y + h > vid.height)
		h = vid.height - y;
	if (w <= 0 || h <= 0)
		return;

	pic = R_ImageForHandle( hPic );
	if( pic->width != 64 || pic->height != 64 ) {
		return;
	}
	x2 = x + w;
	pdest = vid.buffer + y*vid.rowbytes;
	for (i=0 ; i<h ; i++, pdest += vid.rowbytes)
	{
		psrc = pic->pixels[0] + pic->width * ((i+y)&63);
		for (j=x ; j<x2 ; j++)
			pdest[j] = psrc[j&63];
	}
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c)
{
	byte			*dest;
	int				u, v;

	if (x+w > vid.width)
		w = vid.width - x;
	if (y+h > vid.height)
		h = vid.height - y;
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if (w < 0 || h < 0)
		return;
	dest = vid.buffer + y*vid.rowbytes + x;
	for (v=0 ; v<h ; v++, dest += vid.rowbytes)
		for (u=0 ; u<w ; u++)
			dest[u] = c;
}

static byte Blend33( int pcolor, int dstcolor ) {
	return vid.alphamap[pcolor + dstcolor*256];
}

static byte Blend66( int pcolor, int dstcolor ) {
	return vid.alphamap[pcolor*256+dstcolor];
}

void Draw_FillEx( int x, int y, int w, int h, const color_t color ) {
	int colorIndex;
	byte			*dest;
	int				u, v;
	byte (*blendfunc)( int, int );

	colorIndex = color ? R_IndexForColor( color ) : 0xD7;

	blendfunc = NULL;
	if( color[3] < 172 ) {
		blendfunc = color[3] > 84 ? Blend66 : Blend33;
	}

	if (x+w > vid.width)
		w = vid.width - x;
	if (y+h > vid.height)
		h = vid.height - y;
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if (w < 0 || h < 0)
		return;
	dest = vid.buffer + y*vid.rowbytes + x;
	for (v=0 ; v<h ; v++, dest += vid.rowbytes)
		for (u=0 ; u<w ; u++)
			dest[u] = blendfunc ? blendfunc( colorIndex, dest[u] ) : colorIndex;
	
}


//=============================================================================

/*
================
Draw_FadeScreen

================
*/
void Draw_FadeScreen (void)
{
	int			x,y;
	byte		*pbuf;
	int	t;

	for (y=0 ; y<vid.height ; y++)
	{
		pbuf = (byte *)(vid.buffer + vid.rowbytes*y);
		t = (y & 1) << 1;

		for (x=0 ; x<vid.width ; x++)
		{
			if ((x & 3) != t)
				pbuf[x] = 0;
		}
	}
}
