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


byte d_16to8table[65536];

int R_IndexForColor( const color_t color ) {
	unsigned int r, g, b, c;

	r = ( color[0] >> 3 ) & 31;
	g = ( color[1] >> 2 ) & 63;
	b = ( color[2] >> 3 ) & 31;

	c = r | ( g << 5 ) | ( b << 11 );

	return d_16to8table[c];
}


image_t *R_ImageForHandle( qhandle_t hPic ) {
	if( !hPic ) {
		return r_notexture_mip;
	}

	if( hPic < 1 || hPic >= r_numImages + 1 ) {
		Com_Error( ERR_DROP, "R_ImageForHandle: out of range hPic: %i\n", hPic );
	}

	return &r_images[hPic - 1];
}

#ifdef TRUECOLOR_RENDERER

static void R_MipMap( byte *in, byte *out, int width, int height ) {
	int		i, j;

	width <<= 2;
	height >>= 1;
	for( i = 0; i < height; i++, in += width ) {
		for( j = 0; j < width; j += 8, out += 4, in += 8 ) {
			out[0] = ( in[0] + in[4] + in[width+0] + in[width+4] ) >> 2;
			out[1] = ( in[1] + in[5] + in[width+1] + in[width+5] ) >> 2;
			out[2] = ( in[2] + in[6] + in[width+2] + in[width+6] ) >> 2;
			out[3] = ( in[3] + in[7] + in[width+3] + in[width+7] ) >> 2;
		}
	}
}

qboolean R_ConvertTo32( const byte *data, int width, int height, byte *buffer ) {
	byte *dest;
	byte p;
	int i, size;
	qboolean transparent;

	transparent = qfalse;
	size = width * height;

	dest = buffer;
	for( i = 0; i < size; i++ ) {
		p = data[i];
		*( uint32 * )dest = d_8to24table[p];
		
		if( p == 255 ) {	
			// transparent, so scan around for another color
			// to avoid alpha fringes
			// FIXME: do a full flood fill so mips work...
			if (i > width && data[i-width] != 255)
				p = data[i-width];
			else if (i < size-width && data[i+width] != 255)
				p = data[i+width];
			else if (i > 0 && data[i-1] != 255)
				p = data[i-1];
			else if (i < size-1 && data[i+1] != 255)
				p = data[i+1];
			else
				p = 0;
			// copy rgb components
			dest[0] = d_8to24table[p] & 255;
			dest[1] = ( d_8to24table[p] >> 8 ) & 255;
			dest[2] = ( d_8to24table[p] >> 16 ) & 255;
			
			transparent = qtrue;
		}

		dest += 4;
	}

	return transparent;

}

/*
================
R_LoadWal
================
*/
image_t *R_LoadWal( const char *name ) {
	miptex_t	*mt;
	uint32			ofs;
	image_t		*image;
	uint32			size, length;
	uint32 w, h;
	byte *source[4];

	length = fs.LoadFile( name, ( void ** )&mt );
	if( !mt ) {
		Com_DPrintf( "R_LoadWal: can't load %s\n", name );
		return r_notexture_mip;
	}

	w = LittleLong( mt->width );
	h = LittleLong( mt->height );

	size = w * h * ( 256 + 64 + 16 + 4 ) / 256;
	ofs = LittleLong( mt->offsets[0] );
	if( ofs + size > length ) {
		Com_DPrintf( "R_LoadWal: %s is malformed\n", name );
		fs.FreeFile( ( void * )mt );
		return r_notexture_mip;
	}

	image = R_AllocImage( name );
	image->width = image->upload_width = w;
	image->height = image->upload_height = h;
	image->type = it_wall;

	image->pixels[0] = R_Malloc( size * 4 );
	image->pixels[1] = image->pixels[0] + ( w * h      ) * 4;
	image->pixels[2] = image->pixels[1] + ( w * h /  4 ) * 4;
	image->pixels[3] = image->pixels[2] + ( w * h / 16 ) * 4;

	source[0] = ( byte * )mt + ofs;
	source[1] = source[0] + w * h;
	source[2] = source[1] + w * h /  4;
	source[3] = source[2] + w * h / 16;

	image->transparent = R_ConvertTo32( source[0], w, h, image->pixels[0] );
	R_ConvertTo32( source[1], w >> 1, h >> 1, image->pixels[1] );
	R_ConvertTo32( source[2], w >> 2, h >> 2, image->pixels[2] );
	R_ConvertTo32( source[3], w >> 3, h >> 3, image->pixels[3] );

	return image;
}

#endif /* TRUECOLOR_RENDERER */

//=======================================================

void R_FreeImage( image_t *image ) {
	com.Free( image->pixels[0] );
}


/*
================
R_LoadPic

================
*/
void R_LoadImage( image_t *image, byte *pic, int width, int height, imagetype_t type, imageflags_t flags ) {
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
R_LoadWal
================
*/
image_t *R_LoadWal( const char *name ) {
	miptex_t	*mt;
	int			ofs;
	image_t		*image;
	int			size;

	fs.LoadFile( name, ( void ** )&mt );
	if( !mt ) {
		//Com_Printf( "R_LoadWal: can't load %s\n", name );
		return r_notexture_mip;
	}

	image = R_AllocImage( name );
	image->width = image->upload_width = LittleLong( mt->width );
	image->height = image->upload_height = LittleLong( mt->height );
	image->type = it_wall;
	image->flags = if_paletted;
	image->registration_sequence = registration_sequence;

	size = image->width * image->height * ( 256 + 64 + 16 + 4 ) / 256;
	image->pixels[0] = R_Malloc( size );
	image->pixels[1] = image->pixels[0] + image->width * image->height;
	image->pixels[2] = image->pixels[1] + image->width * image->height / 4;
	image->pixels[3] = image->pixels[2] + image->width * image->height / 16;

	ofs = LittleLong( mt->offsets[0] );
	memcpy( image->pixels[0], ( byte * )mt + ofs, size );

	fs.FreeFile( ( void * )mt );

	return image;
}

/*
===============
R_RegisterSkin
===============
*/
qhandle_t R_RegisterSkin( const char *name ) {
	image_t	*image;

	image = R_FindImage( name, it_skin );

	return image ? ( image - r_images ) + 1 : 0;
}

/*
================
R_RegisterPic
================
*/
qhandle_t R_RegisterPic( const char *name ) {
	image_t	*image;
	char	fullname[MAX_QPATH];

	if( name[0] != '/' && name[0] != '\\' ) {
		Q_concat( fullname, sizeof( fullname ), "pics/", name, NULL );
		COM_DefaultExtension( fullname, ".pcx", sizeof( fullname ) );
		image = R_FindImage( fullname, it_pic );
	} else {
		image = R_FindImage( name + 1, it_pic );
	}

	return image ? ( image - r_images ) + 1 : 0;
}

/*
================
R_BuildGammaTable
================
*/
void R_BuildGammaTable( void ) {
	int		i, inf;
	float	g;

	g = vid_gamma->value;

	if( g == 1.0 ) {
		for ( i = 0; i < 256; i++)
			sw_state.gammatable[i] = i;
		return;
	}
	
	for( i = 0; i < 256; i++ ) {
		inf = 255 * pow ( ( i + 0.5 ) / 255.5 , g ) + 0.5;
		clamp( inf, 0, 255 );
		sw_state.gammatable[i] = inf;
	}
}

/*
===============
R_InitImages
===============
*/
void R_InitImages( void ) {
    byte *data;
    int length;

	registration_sequence = 1;

	length = fs.LoadFile( "pics/16to8.dat", ( void ** )&data );
	if( !data ) {
		Com_Error( ERR_FATAL, "Couldn't load pics/16to8.dat" );
    }
    if( length < 65536 ) {
		Com_Error( ERR_FATAL, "Malformed pics/16to8.dat" );
    }
    memcpy( d_16to8table, data, 65536 );

    fs.FreeFile( data );

	R_GetPalette( &vid.colormap );
	vid.alphamap = vid.colormap + 64 * 256;

#ifdef USE_ASM
    {
        /* variable needed by assembly code */
        extern void            *d_pcolormap;
        d_pcolormap = vid.colormap;
    }
#endif
}

/*
===============
R_ShutdownImages
===============
*/
void R_ShutdownImages( void ) {
	if( vid.colormap ) {
		com.Free( vid.colormap );
		vid.colormap = NULL;
	}

	R_FreeAllImages();
}

