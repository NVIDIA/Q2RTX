/*
Copyright (C) 2003-2006 Andrey Nazarov
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

#include "gl_local.h"

image_t *r_notexture;
image_t *r_particletexture;
image_t *r_beamtexture;
image_t *r_warptexture;
image_t *r_whiteimage;

int gl_filter_min;
int gl_filter_max;
float gl_filter_anisotropy;
int gl_tex_alpha_format;
int gl_tex_solid_format;

static int  upload_width;
static int  upload_height;
static image_t  *upload_image;
bspTexinfo_t    *upload_texinfo;

static cvar_t *gl_noscrap;
static cvar_t *gl_round_down;
static cvar_t *gl_picmip;
static cvar_t *gl_gamma_scale_pics;
static cvar_t *gl_bilerp_chars;
static cvar_t *gl_texturemode;
static cvar_t *gl_texturesolidmode;
static cvar_t *gl_texturealphamode;
static cvar_t *gl_anisotropy;
static cvar_t *gl_saturation;
static cvar_t *gl_intensity;
static cvar_t *gl_gamma;
static cvar_t *gl_invert;

qboolean GL_Upload8( byte *data, int width, int height, qboolean mipmap );

typedef struct {
	char *name;
	int	minimize, maximize;
} glmode_t;

static glmode_t filterModes[] = {
	{ "GL_NEAREST", GL_NEAREST, GL_NEAREST },
	{ "GL_LINEAR", GL_LINEAR, GL_LINEAR },
	{ "GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },
	{ "GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR },
	{ "GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST },
	{ "GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR }
};

static int numFilterModes = sizeof( filterModes ) / sizeof( filterModes[0] );

typedef struct {
	char *name;
	int mode;
} gltmode_t;

static gltmode_t alphaModes[] = {
	{ "default", 4 },
	{ "GL_RGBA", GL_RGBA },
	{ "GL_RGBA8", GL_RGBA8 },
	{ "GL_RGB5_A1", GL_RGB5_A1 },
	{ "GL_RGBA4", GL_RGBA4 },
	{ "GL_RGBA2", GL_RGBA2 }
};

static int numAlphaModes = sizeof( alphaModes ) / sizeof( alphaModes[0] );

static gltmode_t solidModes[] = {
	{ "default", 4 },
	{ "GL_RGB", GL_RGB },
	{ "GL_RGB8", GL_RGB8 },
	{ "GL_RGB5", GL_RGB5 },
	{ "GL_RGB4", GL_RGB4 },
	{ "GL_R3_G3_B2", GL_R3_G3_B2 },
	{ "GL_LUMINANCE", GL_LUMINANCE },
#ifdef GL_RGB2_EXT
	{ "GL_RGB2", GL_RGB2_EXT }
#endif
};

static int numSolidModes = sizeof( solidModes ) / sizeof( solidModes[0] );

static void gl_texturemode_changed( cvar_t *self ) {
	int		i;
	image_t	*image;

	for( i = 0; i < numFilterModes ; i++ ) {
		if( !Q_stricmp( filterModes[i].name, self->string ) )
			break;
	}

	if( i == numFilterModes ) {
		Com_WPrintf( "Bad texture filter name\n" );
		cvar.Set( "gl_texturemode", "GL_LINEAR_MIPMAP_NEAREST" );
		gl_filter_min = GL_LINEAR_MIPMAP_NEAREST;
		gl_filter_max = GL_LINEAR;
	} else {
		gl_filter_min = filterModes[i].minimize;
		gl_filter_max = filterModes[i].maximize;
	}

	// change all the existing mipmap texture objects
	for( i = 0, image = r_images; i < r_numImages; i++, image++ ) {
		if( image->type == it_wall || image->type == it_skin ) {
			GL_BindTexture( image->texnum );
			qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    gl_filter_min );
			qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    gl_filter_max );
		}
	}
}

static void gl_anisotropy_changed( cvar_t *self ) {
	int		i;
	image_t	*image;

	if( gl_config.maxAnisotropy < 2 ) {
		return;
	}

	gl_filter_anisotropy = self->value;
    clamp( gl_filter_anisotropy, 1, gl_config.maxAnisotropy );

	// change all the existing mipmap texture objects
	for( i = 0, image = r_images; i < r_numImages; i++, image++ ) {
		if( image->type  == it_wall || image->type  == it_skin ) {
			GL_BindTexture( image->texnum );
			qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
				gl_filter_anisotropy );
		}
	}
}

static void gl_bilerp_chars_changed( cvar_t *self ) {
	int		i;
	image_t	*image;
    GLfloat param = self->integer ? GL_LINEAR : GL_NEAREST;

	// change all the existing charset texture objects
	for( i = 0, image = r_images; i < r_numImages; i++, image++ ) {
		if( image->type == it_charset ) {
			GL_BindTexture( image->texnum );
            qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, param );
            qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, param );
		}
	}
}


/*
===============
GL_TextureAlphaMode
===============
*/
static void GL_TextureAlphaMode( void ) {
	int		i;

	for( i = 0; i < numAlphaModes; i++ ) {
		if( !Q_stricmp( alphaModes[i].name, gl_texturealphamode->string ) )
			break;
	}

	if( i == numAlphaModes ) {
		Com_Printf( "Bad texture alpha mode name %s\n",
            gl_texturealphamode->string );
		cvar.Set( "gl_texturealphamode", "default" );
	    gl_tex_alpha_format = alphaModes[0].mode;
		return;
	}

	gl_tex_alpha_format = alphaModes[i].mode;
}

/*
===============
GL_TextureSolidMode
===============
*/
static void GL_TextureSolidMode( void ) {
	int		i;

	for( i = 0; i < numSolidModes; i++ ) {
		if( !Q_stricmp( solidModes[i].name, gl_texturesolidmode->string ) )
			break;
	}

	if( i == numSolidModes ) {
		Com_Printf( "Bad texture texture mode name %s\n",
            gl_texturesolidmode->string );
		cvar.Set( "gl_texturesolidmode", "default" );
	    gl_tex_solid_format = solidModes[0].mode;
		return;
	}

	gl_tex_solid_format = solidModes[i].mode;
}

/*
=============================================================================

  SCRAP ALLOCATION

  Allocate all the little status bar objects into a single texture
  to crutch up inefficient hardware / drivers

=============================================================================
*/

#define	SCRAP_BLOCK_WIDTH		256
#define	SCRAP_BLOCK_HEIGHT		256

#define SCRAP_TEXNUM			( MAX_RIMAGES + 1 )

static int	scrap_inuse[SCRAP_BLOCK_WIDTH];
static byte	scrap_data[SCRAP_BLOCK_WIDTH * SCRAP_BLOCK_HEIGHT];
qboolean	scrap_dirty;

static qboolean Scrap_AllocBlock( int w, int h, int *s, int *t ) {
    int i, j;
    int x, y, maxInuse, minInuse;

	x = 0; y = SCRAP_BLOCK_HEIGHT;
    minInuse = SCRAP_BLOCK_HEIGHT;
    for( i = 0; i < SCRAP_BLOCK_WIDTH - w; i++ ) {
        maxInuse = 0;
        for( j = 0; j < w; j++ ) {
            if( scrap_inuse[ i + j ] >= minInuse ) {
                break;
            }
            if( maxInuse < scrap_inuse[ i + j ] ) {
                maxInuse = scrap_inuse[ i + j ];
            }
        }
        if( j == w ) {
            x = i;
            y = minInuse = maxInuse;
        }
    }

    if( y + h > SCRAP_BLOCK_HEIGHT ) {
        return qfalse;
    }
    
    for( i = 0; i < w; i++ ) {
        scrap_inuse[ x + i ] = y + h;
    }

    *s = x;
    *t = y;
    return qtrue;
}

void Scrap_Init( void ) {
    int i;
    
    for( i = 0; i < SCRAP_BLOCK_WIDTH; i++ ) {
        scrap_inuse[i] = 0;
    }
}

void Scrap_Upload( void ) {
	//Com_Printf( "Scrap_Upload()\n" );
    GL_BindTexture( SCRAP_TEXNUM );
	GL_Upload8( scrap_data, SCRAP_BLOCK_WIDTH, SCRAP_BLOCK_HEIGHT, qfalse );
	scrap_dirty = qfalse;
}


/*
====================================================================

IMAGE FLOOD FILLING

====================================================================
*/


typedef struct {
	short		x, y;
} floodfill_t;

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK ( FLOODFILL_FIFO_SIZE - 1 )

#define FLOODFILL_STEP( off, dx, dy ) \
    do { \
        if (pos[off] == fillcolor) { \
            pos[off] = 255; \
            fifo[inpt].x = x + (dx); \
            fifo[inpt].y = y + (dy); \
            inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
        } else if (pos[off] != 255) { \
            fdc = pos[off]; \
        } \
    } while( 0 )

/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes
=================
*/
static void R_FloodFillSkin( byte *skin, int skinwidth, int skinheight ) {
	byte				fillcolor = *skin; // assume this is the pixel to fill
	floodfill_t			fifo[FLOODFILL_FIFO_SIZE];
	int					inpt = 0, outpt = 0;
	int					filledcolor = -1;
	int					i;

	if (filledcolor == -1)
	{
		filledcolor = 0;
		// attempt to find opaque black
		for (i = 0; i < 256; ++i)
			if (d_8to24table[i] == 255) {
				// alpha 1.0
				filledcolor = i;
				break;
			}
	}

	// can't fill to filled color or to transparent color
    // (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255)) {
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt) {
		int			x = fifo[outpt].x, y = fifo[outpt].y;
		int			fdc = filledcolor;
		byte		*pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0) FLOODFILL_STEP( -1, -1, 0 );
		if (x < skinwidth - 1) FLOODFILL_STEP( 1, 1, 0 );
		if (y > 0) FLOODFILL_STEP( -skinwidth, 0, -1 );
		if (y < skinheight - 1)	FLOODFILL_STEP( skinwidth, 0, 1 );
		
		skin[x + skinwidth * y] = fdc;
	}
}

//=======================================================

static byte gammatable[256];
static byte intensitytable[256];
static byte gammaintensitytable[256];

/*
================
GL_Saturation

atu 20061129
================
*/
static void GL_Saturation( byte *in, int inwidth, int inheight ) {
	int		i, c;
	byte	*p;
	int	    r, g, b, min, max, mid;
    float   sat;

	p = in;
	c = inwidth * inheight;

    sat = gl_saturation->value;
    if( sat >= 1 ) {
        return;
    }
    if( sat < 0 ) {
        sat = 0;
    }
	
	for( i = 0; i < c; i++, p += 4 ) {
		r = p[0];
		g = p[1];
		b = p[2];
		min = max = r;
		if ( g < min ) min = g;
		if ( b < min ) min = b;
		if ( g > max ) max = g;
		if ( b > max ) max = b;
		mid = ( min + max ) >> 1;
		p[0] = mid + ( r - mid ) * sat;
		p[1] = mid + ( g - mid ) * sat;
		p[2] = mid + ( b - mid ) * sat;
	}
}

/*
================
GL_LightScaleTexture

Scale up the pixel values in a texture to increase the
lighting range
================
*/
static void GL_LightScaleTexture( byte *in, int inwidth, int inheight, qboolean mipmap ) {
	int		i, c;
	byte	*p;

	p = in;
	c = inwidth * inheight;

	if( mipmap ) {
		for( i = 0; i < c; i++, p += 4 ) {
			p[0] = gammaintensitytable[p[0]];
			p[1] = gammaintensitytable[p[1]];
			p[2] = gammaintensitytable[p[2]];
		}
	} else {
		for( i = 0; i < c; i++, p += 4 ) {
			p[0] = gammatable[p[0]];
			p[1] = gammatable[p[1]];
			p[2] = gammatable[p[2]];
		}
	}
}

static void GL_InvertTexture( byte *in, int inwidth, int inheight ) {
	int		i, c;
	byte	*p;

	p = in;
	c = inwidth * inheight;

    for( i = 0; i < c; i++, p += 4 ) {
        p[0] = 255-p[0];
        p[1] = 255-p[1];
        p[2] = 255-p[2];
    }
}

/*
================
GL_MipMap

Operates in place, quartering the size of the texture
================
*/
static void GL_MipMap( byte *in, int width, int height ) {
	int		i, j;
	byte	*out;

	width <<= 2;
	height >>= 1;
	out = in;
	for( i = 0; i < height; i++, in += width ) {
		for( j = 0; j < width; j += 8, out += 4, in += 8 ) {
			out[0] = ( in[0] + in[4] + in[width+0] + in[width+4] ) >> 2;
			out[1] = ( in[1] + in[5] + in[width+1] + in[width+5] ) >> 2;
			out[2] = ( in[2] + in[6] + in[width+2] + in[width+6] ) >> 2;
			out[3] = ( in[3] + in[7] + in[width+3] + in[width+7] ) >> 2;
		}
	}
}

/*
===============
GL_Upload32
===============
*/
qboolean GL_Upload32( byte *data, int width, int height, qboolean mipmap ) {
	byte		*scaled;
	int			scaled_width, scaled_height;
	int			i, c;
	byte	    *scan;
	int         comp;
    qboolean    isalpha;

	scaled_width = Q_CeilPowerOfTwo( width );
	scaled_height = Q_CeilPowerOfTwo( height );

	if( mipmap ) {
		if( gl_round_down->integer && scaled_width > width )
			scaled_width >>= 1;

		if( gl_round_down->integer && scaled_height > height )
			scaled_height >>= 1;

		// let people sample down the world textures for speed
		scaled_width >>= gl_picmip->integer;
		scaled_height >>= gl_picmip->integer;
	}

	// don't ever bother with >256 textures
	while( scaled_width > gl_static.maxTextureSize ||
            scaled_height > gl_static.maxTextureSize )
    {
		scaled_width >>= 1;
		scaled_height >>= 1;
	}
	
	if( scaled_width < 1 ) {
		scaled_width = 1;
	}
	if( scaled_height < 1 ) {
		scaled_height = 1;
	}

	upload_width = scaled_width;
	upload_height = scaled_height;

	// set saturation and lightscale before mipmap
	comp = gl_tex_solid_format;
    if( upload_image->type == it_wall &&
	    gl_saturation->value != 1 &&
        ( !upload_texinfo ||
          !( upload_texinfo->flags & (SURF_SKY|SURF_WARP) ) ) )
    {	
		GL_Saturation( data, width, height );
        if( gl_saturation->value == 0 ) {
            comp = GL_LUMINANCE;
        }
	}

	if( !gl_hwgamma->integer &&
        ( mipmap || gl_gamma_scale_pics->integer ) )
    {
		GL_LightScaleTexture( data, width, height, mipmap );
	}

    if( upload_image->type == it_wall &&
	    gl_invert->integer &&
        ( !upload_texinfo ||
          !( upload_texinfo->flags & (SURF_SKY|SURF_WARP) ) ) )
    {
		GL_InvertTexture( data, width, height );
    }

	// scan the texture for any non-255 alpha
	c = width * height;
	scan = data + 3;
    isalpha = qfalse;
	for( i = 0; i < c; i++, scan += 4 ) {
		if( *scan != 255 ) {
            isalpha = qtrue;
			comp = gl_tex_alpha_format;
			break;
		}
	}

	if( scaled_width == width && scaled_height == height ) {
        /* optimized case, do not reallocate */
		scaled = data;
	} else {
		scaled = fs.AllocTempMem( scaled_width * scaled_height * 4 );
		R_ResampleTexture( data, width, height, scaled,
                scaled_width, scaled_height );
	}


	qglTexImage2D( GL_TEXTURE_2D, 0, comp, scaled_width, scaled_height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, scaled );

	if( mipmap ) {
		int miplevel = 0;

		while( scaled_width > 1 || scaled_height > 1 ) {
			GL_MipMap( scaled, scaled_width, scaled_height );
			scaled_width >>= 1;
			scaled_height >>= 1;
			if( scaled_width < 1 )
				scaled_width = 1;
			if( scaled_height < 1 )
				scaled_height = 1;
			miplevel++;
			qglTexImage2D( GL_TEXTURE_2D, miplevel, comp, scaled_width,
                    scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled );
		}
	}

    if( mipmap ) {
        qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min );
        qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max );
        
        if( gl_config.maxAnisotropy >= 2 ) {
            qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                    gl_filter_anisotropy );
        }
	} else if( upload_image->type == it_charset && !gl_bilerp_chars->integer ) {
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	} else {
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	}
    

	if( scaled != data ) {
		fs.FreeFile( scaled );
	}

	return isalpha;
}

/*
===============
GL_Upload8

Returns has_alpha
===============
*/
qboolean GL_Upload8( byte *data, int width, int height, qboolean mipmap ) {
	byte	buffer[512*256*4];
	byte		*dest;
	int			i, s;
	int			p;

	s = width * height;
	if( s > 512*256 ) {
		Com_Error( ERR_FATAL, "GL_Upload8: %s is too large: width=%d height=%d",
			upload_image->name, width, height );
	}

    dest = buffer;
    for( i = 0; i < s; i++ ) {
        p = data[i];
        *( uint32 * )dest = d_8to24table[p];

        if (p == 255) {	
            // transparent, so scan around for another color
            // to avoid alpha fringes
            // FIXME: do a full flood fill so mips work...
            if (i > width && data[i-width] != 255)
                p = data[i-width];
            else if (i < s-width && data[i+width] != 255)
                p = data[i+width];
            else if (i > 0 && data[i-1] != 255)
                p = data[i-1];
            else if (i < s-1 && data[i+1] != 255)
                p = data[i+1];
            else
                p = 0;
            // copy rgb components
            dest[0] = ((byte *)&d_8to24table[p])[0];
            dest[1] = ((byte *)&d_8to24table[p])[1];
            dest[2] = ((byte *)&d_8to24table[p])[2];
        }

        dest += 4;
    }

    return GL_Upload32( buffer, width, height, mipmap );

}

/*
===============
R_ImageForHandle
===============
*/
image_t *R_ImageForHandle( qhandle_t hPic ) {
	if( hPic < 0 || hPic >= r_numImages ) {
        *( int * )0 = 1;
		Com_Error( ERR_FATAL, "R_ImageForHandle: %d out of range", hPic );
	}

	return &r_images[hPic];
}

/*
===============
R_RegisterSkin
===============
*/
qhandle_t R_RegisterSkin( const char *name ) {
	image_t	*image;

	image = R_FindImage( name, it_skin );
	if( !image ) {
		return 0;
	}

	return ( image - r_images );
}

/*
================
R_RegisterPic
================
*/
qhandle_t R_RegisterPic( const char *name ) {
	image_t	*image;
	char	fullname[MAX_QPATH];

	if( name[0] == '*' ) {
		image = R_FindImage( name, it_pic );
    } else if( name[0] != '/' && name[0] != '\\' ) {
		Com_sprintf( fullname, sizeof( fullname ), "pics/%s", name );
		COM_DefaultExtension( fullname, ".pcx", sizeof( fullname ) );
		image = R_FindImage( fullname, it_pic );
	} else {
		image = R_FindImage( name + 1, it_pic );
	}

	if( !image ) {
		return 0;
	}

	return ( image - r_images );
}



/*
================
R_LoadImage
================
*/
void R_LoadImage( image_t *image, byte *pic, int width, int height,
        imagetype_t type, imageflags_t flags )
{
	qboolean mipmap, transparent;
	byte *src, *dst, *ptr;
	int i, j, s, t;

	image->width = width;
	image->height = height;
	image->type = type;
	upload_image = image;

	// HACK: get dimensions from 8-bit texture
	if( flags & (if_replace_wal|if_replace_pcx) ) {
		char buffer[MAX_QPATH];
		int length;
	    miptex_t	mt;
        pcx_t pcx;
        fileHandle_t f;

		length = strlen( image->name );
		if( length > 4 && image->name[ length - 4 ] == '.' ) {
		    strncpy( buffer, image->name, length - 4 );
            if( flags & if_replace_wal ) {
                strcpy( buffer + length - 4, ".wal" );
                fs.FOpenFile( buffer, &f, FS_MODE_READ );
                if( f ) {
                    length = fs.Read( &mt, sizeof( mt ), f );
                    if( length == sizeof( mt ) ) {
                        image->width = LittleLong( mt.width );
                        image->height = LittleLong( mt.height );
                    }
                    fs.FCloseFile( f );
                }
            } else {
                strcpy( buffer + length - 4, ".pcx" );
                fs.FOpenFile( buffer, &f, FS_MODE_READ );
                if( f ) {
                    length = fs.Read( &pcx, sizeof( pcx ), f );
                    if( length == sizeof( pcx ) ) {
                        image->width = LittleShort( pcx.xmax );
                        image->height = LittleShort( pcx.ymax );
                    }
                    fs.FCloseFile( f );
                }
            }
		}
	}
	
	if( type == it_pic && ( flags & if_paletted ) &&
		width < 64 && height < 64 && !gl_noscrap->integer )
	{
		if( Scrap_AllocBlock( width, height, &s, &t ) ) {
			src = pic;
			dst = &scrap_data[t * SCRAP_BLOCK_WIDTH + s];
			for( i = 0; i < height; i++ ) {
				ptr = dst;
				for( j = 0; j < width; j++ ) {
					 *ptr++ = *src++;
				}
				dst += SCRAP_BLOCK_WIDTH;
			}

			flags |= if_scrap | if_transparent;

			image->texnum = SCRAP_TEXNUM;
			image->upload_width = width;
			image->upload_height = height;
			image->flags = flags;
			image->sl = ( s + 0.01f ) / ( float )SCRAP_BLOCK_WIDTH;
			image->sh = ( s + width - 0.01f ) / ( float )SCRAP_BLOCK_WIDTH;
			image->tl = ( t + 0.01f ) / ( float )SCRAP_BLOCK_HEIGHT;
			image->th = ( t + height - 0.01f ) / ( float )SCRAP_BLOCK_HEIGHT;

			scrap_dirty = qtrue;
			if( !gl_static.registering ) {
				Scrap_Upload();
			}

			fs.FreeFile( pic );

			return;
		}
	}

	if( type == it_skin && ( flags & if_paletted ) )
		R_FloodFillSkin( pic, width, height );

	mipmap = qfalse;
    if( type == it_wall || type == it_skin ) {
        mipmap = qtrue;
    }
    image->texnum = ( image - r_images ) + 1;
	GL_BindTexture( image->texnum );
	if( flags & if_paletted ) {
		transparent = GL_Upload8( pic, width, height, mipmap );
    } else {
		transparent = GL_Upload32( pic, width, height, mipmap );
    }
	if( transparent ) {
		flags |= if_transparent;
	}
	image->upload_width = upload_width;		// after power of 2 and scales
	image->upload_height = upload_height;
	image->flags = flags;
	image->sl = 0;
	image->sh = 1;
	image->tl = 0;
	image->th = 1;

#if 0
    if( width != upload_width || height != upload_height ) {
        Com_Printf( "Resampled '%s' from %dx%d to %dx%d\n",
                image->name, width, height, upload_width, upload_height );
    }
#endif

	/* don't free autogenerated images */
	if( flags & if_auto ) {
		return;
	}
		
	/* don't free *.wal textures */
	if( type == it_wall && ( flags & if_paletted ) ) {
		return;
	}

	fs.FreeFile( pic );
}


/*
================
R_LoadWal
================
*/
image_t *R_LoadWal( const char *name ) {
	miptex_t	*mt;
	uint32			width, height, ofs;
	uint32		length;
	image_t		*image;

	length = fs.LoadFile( name, ( void ** )&mt );
	if( !mt ) {
		Com_DPrintf( "GL_LoadWal: can't load %s\n", name);
		return r_notexture;
	}

	width = LittleLong( mt->width );
	height = LittleLong( mt->height );
	ofs = LittleLong( mt->offsets[0] );

	if( ofs + width * height > length ) {
		Com_DPrintf( "GL_LoadWal: '%s' is malformed\n", name );
		fs.FreeFile( ( void * )mt );
		return NULL;
	}

	image = R_CreateImage( name, ( byte * )mt + ofs, width, height, it_wall, if_paletted );

	fs.FreeFile( ( void * )mt );

	return image;
}

void R_FreeImage( image_t *image ) {
    if( !( image->flags & if_scrap ) ) {
    	qglDeleteTextures( 1, &image->texnum );
    }
}

static void GL_BuildGammaTables( void ) {
	int i;
	float inf, g = gl_gamma->value;

	if( gl_config.renderer == GL_RENDERER_VOODOO || g == 1.0f ) {
        for( i = 0; i < 256; i++ ) {
            gammatable[i] = i;
			gammaintensitytable[i] = intensitytable[i];
        }
	} else {
    	for( i = 0; i < 256; i++ ) {
			inf = 255 * pow( ( i + 0.5 ) / 255.5, g ) + 0.5;
			if( inf > 255 ) {
                inf = 255;
            }
			gammatable[i] = inf;
			gammaintensitytable[i] = intensitytable[gammatable[i]];
	    }
    }
}

static void gl_gamma_changed( cvar_t *self ) {
    GL_BuildGammaTables();
    video.UpdateGamma( gammatable );
}

static const byte dottexture[8][8] = {
	{0,0,0,0,0,0,0,0},
	{0,0,1,1,0,0,0,0},
	{0,1,1,1,1,0,0,0},
	{0,1,1,1,1,0,0,0},
	{0,0,1,1,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},
};

static void GL_InitDefaultTexture( void ) {
    int i, j;
    byte pixels[8*8*4];
    byte *dst;
    
    dst = pixels;
    for( i = 0; i < 8; i++ ) {
        for( j = 0; j < 8; j++ ) {
            dst[0] = dottexture[ i & 3 ][ j & 3 ] * 255;
            dst[1] = 0;
            dst[2] = 0;
            dst[3] = 255;
            dst += 4;
		}
	}
    
    r_notexture = R_CreateImage( "*notexture", pixels, 8, 8, it_wall, if_auto );
}

static void GL_InitParticleTexture( void ) {
    int i, j;
    byte pixels[8*8*4];
    byte *dst;
    
    dst = pixels;
    for( i = 0; i < 8; i++ ) {
        for( j = 0; j < 8; j++ ) {
            dst[0] = 255;
            dst[1] = 255;
            dst[2] = 255;
            dst[3] = dottexture[ i ][ j ] * 255;
            dst += 4;
		}
	}
    
    r_particletexture = R_CreateImage( "*particletexture", pixels, 8, 8,
            it_sprite, if_auto );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP );
}

static void GL_InitWhiteImage( void ) {
    int i, j;
    byte pixels[8*8*4];
    byte *dst;
    
    dst = pixels;
    for( i = 0; i < 8; i++ ) {
        for( j = 0; j < 8; j++ ) {
            *( uint32 * )dst = ( uint32 )-1;
            dst += 4;
		}
	}
    
    r_whiteimage = R_CreateImage( "*whiteimage", pixels, 8, 8,
        it_pic, if_auto );
}

static void GL_InitBeamTexture( void ) {
    byte pixels[16*16*4];
    byte *dst;
    float f;
    int i, j;

    dst = pixels;
    for( i = 0; i < 16; i++ ) {
        for( j = 0; j < 16; j++ ) {
            f = fabs( j - 16/2 ) - 0.5f;
            f = 1.0f - f / ( 16/2 - 2.5f );
            if( f < 0 ) f = 0;
            else if( f > 1 ) f = 1;
            dst[0] = 255 * f;
            dst[1] = 255 * f;
            dst[2] = 255 * f;
            dst[3] = 255;
            dst += 4;
        }
    }
    
    r_beamtexture = R_CreateImage( "*beamTexture", pixels, 16, 16,
        it_pic, if_auto );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
}

static void GL_InitWarpTexture( void ) {
    byte pixels[8*8*4];
    byte *dst;
    int i, j;

    dst = pixels;
    for( i = 0; i < 8; i++ ) {
        for( j = 0; j < 8; j++ ) {
            dst[0] = rand() & 255;
            dst[1] = rand() & 255;
            dst[2] = 255;
            dst[3] = 255;
            dst += 4;
        }
    }
    
    r_warptexture = R_CreateImage( "*warpTexture", pixels, 8, 8,
        it_pic, if_auto );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
}

/*
===============
GL_InitImages
===============
*/
void GL_InitImages( void ) {
	int i, j;
	float f;

	registration_sequence = 1;

	if( r_numImages ) {
		Com_Error( ERR_FATAL, "GL_InitImages: %d images still not freed",
            r_numImages );
	}

	gl_bilerp_chars = cvar.Get( "gl_bilerp_chars", "0", 0 );
    gl_bilerp_chars->changed = gl_bilerp_chars_changed;
	gl_texturemode = cvar.Get( "gl_texturemode", "GL_LINEAR_MIPMAP_NEAREST",
            CVAR_ARCHIVE );
    gl_texturemode->changed = gl_texturemode_changed;
	gl_anisotropy = cvar.Get( "gl_anisotropy", "1", CVAR_ARCHIVE );
    gl_anisotropy->changed = gl_anisotropy_changed;
	gl_noscrap = cvar.Get( "gl_noscrap", "0", CVAR_LATCHED );
    gl_round_down = cvar.Get( "gl_round_down", "0", CVAR_LATCHED );
    gl_picmip = cvar.Get( "gl_picmip", "0", CVAR_LATCHED );
    gl_gamma_scale_pics = cvar.Get( "gl_gamma_scale_pics", "0", CVAR_LATCHED );
	gl_texturealphamode = cvar.Get( "gl_texturealphamode", "default",
            CVAR_ARCHIVE|CVAR_LATCHED );
	gl_texturesolidmode = cvar.Get( "gl_texturesolidmode", "default",
            CVAR_ARCHIVE|CVAR_LATCHED );
	gl_saturation = cvar.Get( "gl_saturation", "1", CVAR_ARCHIVE|CVAR_LATCHED );
	gl_intensity = cvar.Get( "intensity", "1", CVAR_ARCHIVE|CVAR_LATCHED );
	gl_invert = cvar.Get( "gl_invert", "0", CVAR_ARCHIVE|CVAR_LATCHED );
    if( gl_hwgamma->integer ) {
        gl_gamma = cvar.Get( "vid_gamma", "1", 0 );
        gl_gamma->changed = gl_gamma_changed;
    } else {
        gl_gamma = cvar.Get( "vid_gamma", "1", CVAR_LATCHED );
    }

	R_InitImageManager();

	Scrap_Init();

	R_GetPalette( NULL );

	if( gl_intensity->value < 1 ) {
		cvar.SetValue( "intensity", 1 );
	}
	f = gl_intensity->value;
	for( i = 0; i < 256; i++ ) {
		j = i * f;
		if( j > 255 ) {
			j = 255;
		}
		intensitytable[i] = j;
	}

    if( gl_hwgamma->integer ) {
        gl_gamma_changed( gl_gamma );
    } else {
        GL_BuildGammaTables();
    }

    GL_TextureAlphaMode();
    GL_TextureSolidMode();

    gl_texturemode_changed( gl_texturemode );
	gl_anisotropy_changed( gl_anisotropy );
	gl_bilerp_chars_changed( gl_bilerp_chars );

	/* make sure r_notexture == &r_images[0] */
	GL_InitDefaultTexture();
    GL_InitWarpTexture();
    GL_InitParticleTexture();
    GL_InitWhiteImage();
	GL_InitBeamTexture();
}

/*
===============
GL_ShutdownImages
===============
*/
void GL_ShutdownImages( void ) {
    gl_bilerp_chars->changed = NULL;
    gl_texturemode->changed = NULL;
    gl_anisotropy->changed = NULL;
    gl_gamma->changed = NULL;

	R_FreeAllImages();
	R_ShutdownImageManager();
}

