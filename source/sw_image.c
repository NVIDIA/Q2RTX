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

#include "sw_local.h"
#include "d_wal.h"

image_t  	*r_notexture;

byte d_16to8table[65536];

/*
================
IMG_Unload
================
*/
void IMG_Unload( image_t *image ) {
    if( image->flags & if_auto ) {
        return;
    }
	Z_Free( image->pixels[0] );
    image->pixels[0] = NULL;
}

/*
================
IMG_Load
================
*/
void IMG_Load( image_t *image, byte *pic, int width, int height, imagetype_t type, imageflags_t flags ) {
	int			i, c, b;

	image->registration_sequence = registration_sequence;

	image->width = image->upload_width = width;
	image->height = image->upload_height = height;
	image->type = type;

	c = width * height;
	image->pixels[0] = pic;

	for( i = 0; i < c; i++ ) {
		b = pic[i];
		if( b == 255 ) {
			flags |= if_transparent;
		}
	}

	image->flags = flags;
}

/*
================
IMG_LoadWAL
================
*/
image_t *IMG_LoadWAL( const char *name ) {
	miptex_t	*mt;
	image_t		*image;
	size_t		width, height, offset, endpos, filelen, size;

	filelen = FS_LoadFile( name, ( void ** )&mt );
	if( !mt ) {
		return NULL;
	}

    image = NULL;

	width = LittleLong( mt->width );
	height = LittleLong( mt->height );
	offset = LittleLong( mt->offsets[0] );

    if( width < 1 || height < 1 || width > MAX_TEXTURE_SIZE || height > MAX_TEXTURE_SIZE ) {
		Com_WPrintf( "LoadWAL: %s: bad dimensions\n", name );
        goto fail;
    }

	size = width * height * ( 256 + 64 + 16 + 4 ) / 256;
    endpos = offset + size;
	if( endpos < offset || endpos > filelen ) {
		Com_WPrintf( "LoadWAL: %s: bad offset\n", name );
        goto fail;
	}

	image = IMG_Alloc( name );
	image->width = image->upload_width = width;
	image->height = image->upload_height = height;
	image->type = it_wall;
	image->flags = if_paletted;
	image->registration_sequence = registration_sequence;

	image->pixels[0] = R_Malloc( size );
	image->pixels[1] = image->pixels[0] + width * height;
	image->pixels[2] = image->pixels[1] + width * height / 4;
	image->pixels[3] = image->pixels[2] + width * height / 16;

	memcpy( image->pixels[0], ( byte * )mt + offset, size );

fail:
	FS_FreeFile( mt );

	return image;
}

static void R_BuildGammaTable( void ) {
	int		i, inf;
	float	g = vid_gamma->value;

	if( g == 1.0 ) {
		for ( i = 0; i < 256; i++)
			sw_state.gammatable[i] = i;
		return;
	}

	for( i = 0; i < 256; i++ ) {
		inf = 255 * pow ( ( i + 0.5 ) / 255.5 , g ) + 0.5;
		sw_state.gammatable[i] = clamp( inf, 0, 255 );
	}
}

#define NTX     16

static void R_CreateNotexture( void ) {
    static byte buffer[NTX * NTX * ( 256 + 64 + 16 + 4 ) / 256];
	int		x, y, m;
	byte	*p;
	
// create a simple checkerboard texture for the default
	r_notexture = IMG_Alloc( "*notexture" );
    r_notexture->type = it_wall;	
    r_notexture->flags = if_auto;	
	r_notexture->width = r_notexture->height = NTX;
	r_notexture->upload_width = r_notexture->upload_height = NTX;
	r_notexture->pixels[0] = buffer;
	r_notexture->pixels[1] = r_notexture->pixels[0] + NTX * NTX;
	r_notexture->pixels[2] = r_notexture->pixels[1] + NTX * NTX / 4;
	r_notexture->pixels[3] = r_notexture->pixels[2] + NTX * NTX / 16;
	
	for( m = 0; m < 4; m++ ) {
		p = r_notexture->pixels[m];
		for ( y = 0; y < ( 16 >> m ); y++ ) {
			for( x = 0; x < ( 16 >> m ); x++ ) {
				if( ( y < ( 8 >> m ) ) ^ ( x < ( 8 >> m ) ) )
					*p++ = 0;
				else
					*p++ = 1;
			}
        }
	}	
}

int R_IndexForColor( const color_t color ) {
	unsigned int r, g, b, c;

	r = ( color[0] >> 3 ) & 31;
	g = ( color[1] >> 2 ) & 63;
	b = ( color[2] >> 3 ) & 31;

	c = r | ( g << 5 ) | ( b << 11 );

	return d_16to8table[c];
}

static void R_Get16to8( void ) {
    fileHandle_t f;
    size_t r;

	FS_FOpenFile( "pics/16to8.dat", &f, FS_MODE_READ );
	if( !f ) {
		Com_Error( ERR_FATAL, "Couldn't load pics/16to8.dat" );
    }
    r = FS_Read( d_16to8table, sizeof( d_16to8table ), f );
    if( r != sizeof( d_16to8table ) ) {
		Com_Error( ERR_FATAL, "Malformed pics/16to8.dat" );
    }

    FS_FCloseFile( f );
}

/*
===============
R_InitImages
===============
*/
void R_InitImages( void ) {
	registration_sequence = 1;

	IMG_GetPalette( &vid.colormap );
	vid.alphamap = vid.colormap + 64 * 256;

#if USE_ASM
    {
        /* variable needed by assembly code */
        extern void            *d_pcolormap;
        d_pcolormap = vid.colormap;
    }
#endif

    R_Get16to8();

    R_CreateNotexture();

	R_BuildGammaTable();
}

/*
===============
R_ShutdownImages
===============
*/
void R_ShutdownImages( void ) {
	if( vid.colormap ) {
		Z_Free( vid.colormap );
		vid.colormap = NULL;
	}

	IMG_FreeAll();
}

