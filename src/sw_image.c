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

/*
================
IMG_Unload
================
*/
void IMG_Unload( image_t *image ) {
    Z_Free( image->pixels[0] );
    image->pixels[0] = NULL;
}

/*
================
IMG_Load
================
*/
void IMG_Load( image_t *image, byte *pic, int width, int height ) {
    int         i, c, b;

    image->upload_width = width;
    image->upload_height = height;

    c = width * height;
    if( image->type == it_wall ) {
        size_t size = MIPSIZE( c );

        image->pixels[0] = R_Malloc( size );
        image->pixels[1] = image->pixels[0] + c;
        image->pixels[2] = image->pixels[1] + c / 4;
        image->pixels[3] = image->pixels[2] + c / 16;

        memcpy( image->pixels[0], pic, size );
    } else {
        image->pixels[0] = pic;

        for( i = 0; i < c; i++ ) {
            b = pic[i];
            if( b == 255 ) {
                image->flags |= if_transparent;
            }
        }
    }
}

void R_BuildGammaTable( void ) {
    int     i, inf;
    float   g = vid_gamma->value;

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
    static byte buffer[MIPSIZE( NTX * NTX )];
    int     x, y, m;
    byte    *p;
    image_t *ntx;

// create a simple checkerboard texture for the default
    ntx = R_NOTEXTURE;
    ntx->type = it_wall;
    ntx->flags = 0;
    ntx->width = ntx->height = NTX;
    ntx->upload_width = ntx->upload_height = NTX;
    ntx->pixels[0] = buffer;
    ntx->pixels[1] = ntx->pixels[0] + NTX * NTX;
    ntx->pixels[2] = ntx->pixels[1] + NTX * NTX / 4;
    ntx->pixels[3] = ntx->pixels[2] + NTX * NTX / 16;

    for( m = 0; m < 4; m++ ) {
        p = ntx->pixels[m];
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
    static const char colormap[] = "pics/16to8.dat";
    qhandle_t f;
    ssize_t ret;

    ret = FS_FOpenFile( colormap, &f, FS_MODE_READ );
    if( !f ) {
        goto fail;
    }

    ret = FS_Read( d_16to8table, sizeof( d_16to8table ), f );

    FS_FCloseFile( f );

    if( ret < 0 ) {
        goto fail;
    }

    if( ret == sizeof( d_16to8table ) ) {
        return; // success
    }

    ret = Q_ERR_FILE_TOO_SMALL;
fail:
    Com_Error( ERR_FATAL, "Couldn't load %s: %s",
        colormap, Q_ErrorString( ret ) );
}

/*
===============
R_InitImages
===============
*/
void R_InitImages( void ) {
    registration_sequence = 1;

    vid.colormap = IMG_GetPalette();
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

