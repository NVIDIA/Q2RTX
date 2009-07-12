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
#include "d_pcx.h"
#include "d_wal.h"

image_t *r_notexture;
image_t *r_particletexture;
image_t *r_beamtexture;
image_t *r_warptexture;
image_t *r_whiteimage;
image_t *r_blackimage;

int gl_filter_min;
int gl_filter_max;
float gl_filter_anisotropy;
int gl_tex_alpha_format;
int gl_tex_solid_format;

static int  upload_width;
static int  upload_height;
static image_t  *upload_image;
mtexinfo_t    *upload_texinfo;

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

static qboolean GL_Upload8( byte *data, int width, int height, qboolean mipmap );

typedef struct {
    char *name;
    int minimize, maximize;
} glmode_t;

static const glmode_t filterModes[] = {
    { "GL_NEAREST", GL_NEAREST, GL_NEAREST },
    { "GL_LINEAR", GL_LINEAR, GL_LINEAR },
    { "GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },
    { "GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR },
    { "GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST },
    { "GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR }
};

static const int numFilterModes = sizeof( filterModes ) / sizeof( filterModes[0] );

typedef struct {
    char *name;
    int mode;
} gltmode_t;

static const gltmode_t alphaModes[] = {
    { "default", 4 },
    { "GL_RGBA", GL_RGBA },
    { "GL_RGBA8", GL_RGBA8 },
    { "GL_RGB5_A1", GL_RGB5_A1 },
    { "GL_RGBA4", GL_RGBA4 },
    { "GL_RGBA2", GL_RGBA2 }
};

static const int numAlphaModes = sizeof( alphaModes ) / sizeof( alphaModes[0] );

static const gltmode_t solidModes[] = {
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

static const int numSolidModes = sizeof( solidModes ) / sizeof( solidModes[0] );

static void gl_texturemode_changed( cvar_t *self ) {
    int     i;
    image_t *image;

    for( i = 0; i < numFilterModes ; i++ ) {
        if( !Q_stricmp( filterModes[i].name, self->string ) )
            break;
    }

    if( i == numFilterModes ) {
        Com_WPrintf( "Bad texture mode: %s\n", self->string );
        Cvar_Reset( self );
        gl_filter_min = GL_LINEAR_MIPMAP_LINEAR;
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

static void gl_texturemode_g( genctx_t *ctx ) {
    int i;
    
    for( i = 0; i < numFilterModes ; i++ ) {
        if( !Prompt_AddMatch( ctx, filterModes[i].name ) ) {
            break;
        }
    }
}

static void gl_anisotropy_changed( cvar_t *self ) {
    int     i;
    image_t *image;

    if( gl_config.maxAnisotropy < 2 ) {
        return;
    }

    gl_filter_anisotropy = self->value;
    clamp( gl_filter_anisotropy, 1, gl_config.maxAnisotropy );

    // change all the existing mipmap texture objects
    for( i = 0, image = r_images; i < r_numImages; i++, image++ ) {
        if( image->type == it_wall || image->type == it_skin ) {
            GL_BindTexture( image->texnum );
            qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                gl_filter_anisotropy );
        }
    }
}

static void gl_bilerp_chars_changed( cvar_t *self ) {
    int     i;
    image_t *image;
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
    int     i;

    for( i = 0; i < numAlphaModes; i++ ) {
        if( !Q_stricmp( alphaModes[i].name, gl_texturealphamode->string ) ) {
            gl_tex_alpha_format = alphaModes[i].mode;
            return;
        }
    }

    Com_WPrintf( "Bad texture alpha mode: %s\n", gl_texturealphamode->string );
    Cvar_Reset( gl_texturealphamode );
    gl_tex_alpha_format = alphaModes[0].mode;
}

static void gl_texturealphamode_g( genctx_t *ctx ) {
    int i;
    
    for( i = 0; i < numAlphaModes; i++ ) {
        if( !Prompt_AddMatch( ctx, alphaModes[i].name ) ) {
            break;
        }
    }
}

/*
===============
GL_TextureSolidMode
===============
*/
static void GL_TextureSolidMode( void ) {
    int     i;

    for( i = 0; i < numSolidModes; i++ ) {
        if( !Q_stricmp( solidModes[i].name, gl_texturesolidmode->string ) ) {
            gl_tex_solid_format = solidModes[i].mode;
            return;
        }
    }

    Com_WPrintf( "Bad texture solid mode: %s\n", gl_texturesolidmode->string );
    Cvar_Reset( gl_texturesolidmode );
    gl_tex_solid_format = solidModes[0].mode;
}

static void gl_texturesolidmode_g( genctx_t *ctx ) {
    int i;
    
    for( i = 0; i < numSolidModes; i++ ) {
        if( !Prompt_AddMatch( ctx, solidModes[i].name ) ) {
            break;
        }
    }
}

/*
=============================================================================

  SCRAP ALLOCATION

  Allocate all the little status bar objects into a single texture
  to crutch up inefficient hardware / drivers

=============================================================================
*/

#define SCRAP_BLOCK_WIDTH       256
#define SCRAP_BLOCK_HEIGHT      256

#define SCRAP_TEXNUM            ( MAX_RIMAGES + 1 )

static int  scrap_inuse[SCRAP_BLOCK_WIDTH];
static byte scrap_data[SCRAP_BLOCK_WIDTH * SCRAP_BLOCK_HEIGHT];
static qboolean scrap_dirty;

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

static void Scrap_Shutdown( void ) {
    GLuint num = SCRAP_TEXNUM;
    int i;

    for( i = 0; i < SCRAP_BLOCK_WIDTH; i++ ) {
        scrap_inuse[i] = 0;
    }
    scrap_dirty = qfalse;

    qglDeleteTextures( 1, &num );
}

void Scrap_Upload( void ) {
    if( !scrap_dirty ) {
        return;
    }
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
    short       x, y;
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
    byte                fillcolor = *skin; // assume this is the pixel to fill
    floodfill_t         fifo[FLOODFILL_FIFO_SIZE];
    int                 inpt = 0, outpt = 0;
    int                 filledcolor = -1;
    int                 i;

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
        int         x = fifo[outpt].x, y = fifo[outpt].y;
        int         fdc = filledcolor;
        byte        *pos = &skin[x + skinwidth * y];

        outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

        if (x > 0) FLOODFILL_STEP( -1, -1, 0 );
        if (x < skinwidth - 1) FLOODFILL_STEP( 1, 1, 0 );
        if (y > 0) FLOODFILL_STEP( -skinwidth, 0, -1 );
        if (y < skinheight - 1) FLOODFILL_STEP( skinwidth, 0, 1 );
        
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
    int     i, c;
    byte    *p;
    float   r, g, b, y, sat;

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
        y = r * 0.2126f + g * 0.7152f + b * 0.0722f;
        p[0] = y + ( r - y ) * sat;
        p[1] = y + ( g - y ) * sat;
        p[2] = y + ( b - y ) * sat;
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
    int     i, c;
    byte    *p;

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
    int     i, c;
    byte    *p;

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
GL_ResampleTexture
================
*/
static void GL_ResampleTexture( const byte *in, int inwidth, int inheight, byte *out, int outwidth, int outheight ) {
    int i, j;
    const byte  *inrow1, *inrow2;
    unsigned    frac, fracstep;
    unsigned    p1[MAX_TEXTURE_SIZE], p2[MAX_TEXTURE_SIZE];
    const byte  *pix1, *pix2, *pix3, *pix4;
    float       heightScale;

    if( outwidth > MAX_TEXTURE_SIZE ) {
        Com_Error( ERR_FATAL, "%s: outwidth > %d", __func__, MAX_TEXTURE_SIZE );
    }

    fracstep = inwidth * 0x10000 / outwidth;

    frac = fracstep >> 2;
    for( i = 0; i < outwidth; i++ ) {
        p1[i] = 4 * ( frac >> 16 );
        frac += fracstep;
    }
    frac = 3 * ( fracstep >> 2 );
    for( i = 0; i < outwidth; i++ ) {
        p2[i] = 4 * ( frac >> 16 );
        frac += fracstep;
    }

    heightScale = ( float )inheight / outheight;
    inwidth <<= 2;
    for( i = 0; i < outheight; i++ ) {
        inrow1 = in + inwidth * ( int )( ( i + 0.25f ) * heightScale );
        inrow2 = in + inwidth * ( int )( ( i + 0.75f ) * heightScale );
        for( j = 0; j < outwidth; j++ ) {
            pix1 = inrow1 + p1[j];
            pix2 = inrow1 + p2[j];
            pix3 = inrow2 + p1[j];
            pix4 = inrow2 + p2[j];
            out[0] = ( pix1[0] + pix2[0] + pix3[0] + pix4[0] ) >> 2;
            out[1] = ( pix1[1] + pix2[1] + pix3[1] + pix4[1] ) >> 2;
            out[2] = ( pix1[2] + pix2[2] + pix3[2] + pix4[2] ) >> 2;
            out[3] = ( pix1[3] + pix2[3] + pix3[3] + pix4[3] ) >> 2;
            out += 4;
        }
    }
}


/*
================
GL_MipMap

Operates in place, quartering the size of the texture
================
*/
static void GL_MipMap( byte *in, int width, int height ) {
    int     i, j;
    byte    *out;

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

static inline qboolean is_a_wall( void ) {
    if( upload_image->type != it_wall ) {
        return qfalse; // not a wall texture
    }
    if( !upload_texinfo ) {
        return qtrue; // don't know what type of surface it is
    }  
    if( upload_texinfo->c.flags & (SURF_SKY|SURF_WARP) ) {
        return qfalse; // don't desaturate or invert sky and liquid surfaces
    }
    return qtrue;
}

static inline qboolean is_alpha( byte *data, int width, int height ) {
    int         i, c;
    byte        *scan;

    c = width * height;
    scan = data + 3;
    for( i = 0; i < c; i++, scan += 4 ) {
        if( *scan != 255 ) {
            return qtrue;
        }
    }

    return qfalse;
}

/*
===============
GL_Upload32
===============
*/
static qboolean GL_Upload32( byte *data, int width, int height, qboolean mipmap ) {
    byte        *scaled;
    int         scaled_width, scaled_height;
    int         comp;
    qboolean    isalpha;

    scaled_width = Q_CeilPowerOfTwo( width );
    scaled_height = Q_CeilPowerOfTwo( height );

    if( mipmap ) {
        if( gl_round_down->integer ) {
            if( scaled_width > width )
                scaled_width >>= 1;
            if( scaled_height > height )
                scaled_height >>= 1;
        }

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
    if( is_a_wall() && gl_saturation->value != 1 ) {   
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

    if( is_a_wall() && gl_invert->integer ) {
        GL_InvertTexture( data, width, height );
    }

    // scan the texture for any non-255 alpha
    isalpha = is_alpha( data, width, height );
    if( isalpha ) {
        comp = gl_tex_alpha_format;
    }

    if( scaled_width == width && scaled_height == height ) {
        // optimized case, do not reallocate
        scaled = data;
    } else {
        scaled = FS_AllocTempMem( scaled_width * scaled_height * 4 );
        GL_ResampleTexture( data, width, height, scaled,
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
        FS_FreeFile( scaled );
    }

    return isalpha;
}

/*
===============
GL_Upload8

Returns has_alpha
===============
*/
static qboolean GL_Upload8( byte *data, int width, int height, qboolean mipmap ) {
    byte    buffer[512*256*4];
    byte    *dest;
    int     i, s;
    int     p;

    s = width * height;
    if( s > 512*256 ) {
        Com_Error( ERR_FATAL, "GL_Upload8: %s is too large: %dx%d",
            upload_image->name, width, height );
    }

    dest = buffer;
    for( i = 0; i < s; i++ ) {
        p = data[i];
        *( uint32_t * )dest = d_8to24table[p];

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

static void GL_GetDimensions( image_t *image, imageflags_t flags ) {
    char buffer[MAX_QPATH];
    size_t length;
    miptex_t    mt;
    dpcx_t pcx;
    fileHandle_t f;

    length = strlen( image->name );
    if( length > 4 && image->name[ length - 4 ] == '.' ) {
        strncpy( buffer, image->name, length - 4 );
        if( flags & if_replace_wal ) {
            strcpy( buffer + length - 4, ".wal" );
            FS_FOpenFile( buffer, &f, FS_MODE_READ );
            if( f ) {
                length = FS_Read( &mt, sizeof( mt ), f );
                if( length == sizeof( mt ) ) {
                    image->width = LittleLong( mt.width );
                    image->height = LittleLong( mt.height );
                }
                FS_FCloseFile( f );
            }
        } else {
            strcpy( buffer + length - 4, ".pcx" );
            FS_FOpenFile( buffer, &f, FS_MODE_READ );
            if( f ) {
                length = FS_Read( &pcx, sizeof( pcx ), f );
                if( length == sizeof( pcx ) ) {
                    image->width = LittleShort( pcx.xmax ) + 1;
                    image->height = LittleShort( pcx.ymax ) + 1;
                }
                FS_FCloseFile( f );
            }
        }
    }
}

/*
================
IMG_Load
================
*/
void IMG_Load( image_t *image, byte *pic, int width, int height,
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
        GL_GetDimensions( image, flags );
    }

    // load small 8-bit pics onto the scrap
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

            FS_FreeFile( pic );

            return;
        }
    }

    if( type == it_skin && ( flags & if_paletted ) )
        R_FloodFillSkin( pic, width, height );

    mipmap = ( type == it_wall || type == it_skin ) ? qtrue : qfalse;
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
    image->upload_width = upload_width;     // after power of 2 and scales
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

    // don't free autogenerated images
    if( flags & if_auto ) {
        return;
    }
        
    // don't free *.wal textures
    if( type == it_wall && ( flags & if_paletted ) ) {
        return;
    }

    FS_FreeFile( pic );
}

void IMG_Unload( image_t *image ) {
    if( !( image->flags & if_scrap ) ) {
        qglDeleteTextures( 1, &image->texnum );
    }
}

/*
================
IMG_LoadWAL
================
*/
image_t *IMG_LoadWAL( const char *name ) {
    miptex_t    *mt;
    size_t      width, height, offset, length, endpos;
    image_t     *image;

    length = FS_LoadFile( name, ( void ** )&mt );
    if( !mt ) {
        return NULL;
    }

    width = LittleLong( mt->width );
    height = LittleLong( mt->height );
    offset = LittleLong( mt->offsets[0] );

    if( width < 1 || height < 1 || width > MAX_TEXTURE_SIZE || height > MAX_TEXTURE_SIZE ) {
        Com_WPrintf( "LoadWAL: %s: bad dimensions\n", name );
        goto fail;
    }
    endpos = offset + width * height;
    if( endpos < offset || endpos > length ) {
        Com_WPrintf( "LoadWAL: %s: bad offset\n", name );
        goto fail;
    }

    image = IMG_Create( name, ( byte * )mt + offset, width, height, it_wall, if_paletted );

    FS_FreeFile( mt );
    return image;

fail:
    FS_FreeFile( mt );
    return NULL;
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
    VID_UpdateGamma( gammatable );
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
    
    r_notexture = IMG_Create( "*notexture", pixels, 8, 8, it_wall, if_auto );
}

#define DLIGHT_TEXTURE_SIZE     16

static void GL_InitParticleTexture( void ) {
    byte pixels[DLIGHT_TEXTURE_SIZE*DLIGHT_TEXTURE_SIZE*4];
    byte *dst;
    float x, y, f;
    int i, j;

    dst = pixels;
    for( i = 0; i < DLIGHT_TEXTURE_SIZE; i++ ) {
        for( j = 0; j < DLIGHT_TEXTURE_SIZE; j++ ) {
            x = j - DLIGHT_TEXTURE_SIZE/2 + 0.5f;
            y = i - DLIGHT_TEXTURE_SIZE/2 + 0.5f;
            f = sqrt( x * x + y * y );
            f = 1.0f - f / ( DLIGHT_TEXTURE_SIZE/2 - 1.5f );
            if( f < 0 ) f = 0;
            else if( f > 1 ) f = 1;
            dst[0] = 255;
            dst[1] = 255;
            dst[2] = 255;
            dst[3] = 255*f;
            dst += 4;
        }
    }
    
    r_particletexture = IMG_Create( "*particleTexture", pixels,
        DLIGHT_TEXTURE_SIZE, DLIGHT_TEXTURE_SIZE, it_pic, if_auto );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP );
}

static void GL_InitWhiteImage( void ) {
    uint32_t pixel;
    
    pixel = MakeColor( 0xff, 0xff, 0xff, 0xff );
    r_whiteimage = IMG_Create( "*whiteimage", ( byte * )&pixel, 1, 1,
        it_pic, if_auto );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );

    pixel = MakeColor( 0, 0, 0, 0xff );
    r_blackimage = IMG_Create( "*blackimage", ( byte * )&pixel, 1, 1,
        it_pic, if_auto );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
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
            dst[0] = 255;// * f;
            dst[1] = 255;// * f;
            dst[2] = 255;// * f;
            dst[3] = 255 * f;
            dst += 4;
        }
    }
    
    r_beamtexture = IMG_Create( "*beamTexture", pixels, 16, 16,
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
    
    r_warptexture = IMG_Create( "*warpTexture", pixels, 8, 8,
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

    gl_bilerp_chars = Cvar_Get( "gl_bilerp_chars", "0", 0 );
    gl_bilerp_chars->changed = gl_bilerp_chars_changed;
    gl_texturemode = Cvar_Get( "gl_texturemode",
        "GL_LINEAR_MIPMAP_LINEAR", CVAR_ARCHIVE );
    gl_texturemode->changed = gl_texturemode_changed;
    gl_texturemode->generator = gl_texturemode_g;
    gl_anisotropy = Cvar_Get( "gl_anisotropy", "1", CVAR_ARCHIVE );
    gl_anisotropy->changed = gl_anisotropy_changed;
    gl_noscrap = Cvar_Get( "gl_noscrap", "0", CVAR_FILES );
    gl_round_down = Cvar_Get( "gl_round_down", "0", CVAR_FILES );
    gl_picmip = Cvar_Get( "gl_picmip", "0", CVAR_FILES );
    gl_gamma_scale_pics = Cvar_Get( "gl_gamma_scale_pics", "0", CVAR_FILES );
    gl_texturealphamode = Cvar_Get( "gl_texturealphamode",
        "default", CVAR_ARCHIVE|CVAR_FILES );
    gl_texturealphamode->generator = gl_texturealphamode_g;
    gl_texturesolidmode = Cvar_Get( "gl_texturesolidmode",
        "default", CVAR_ARCHIVE|CVAR_FILES );
    gl_texturesolidmode->generator = gl_texturesolidmode_g;
    gl_saturation = Cvar_Get( "gl_saturation", "1", CVAR_ARCHIVE|CVAR_FILES );
    gl_intensity = Cvar_Get( "intensity", "1", CVAR_ARCHIVE|CVAR_FILES );
    gl_invert = Cvar_Get( "gl_invert", "0", CVAR_ARCHIVE|CVAR_FILES );
    if( gl_hwgamma->integer ) {
        gl_gamma = Cvar_Get( "vid_gamma", "1", CVAR_ARCHIVE );
        gl_gamma->changed = gl_gamma_changed;
    } else {
        gl_gamma = Cvar_Get( "vid_gamma", "1", CVAR_ARCHIVE|CVAR_FILES );
    }

    IMG_Init();

    IMG_GetPalette( NULL );

    f = Cvar_ClampValue( gl_intensity, 1, 5 );
    gl_static.inverse_intensity = 1 / f;
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
    GLuint num;
    int i;

    gl_bilerp_chars->changed = NULL;
    gl_texturemode->changed = NULL;
    gl_texturemode->generator = NULL;
    gl_texturealphamode->generator = NULL;
    gl_texturesolidmode->generator = NULL;
    gl_anisotropy->changed = NULL;
    gl_gamma->changed = NULL;

    for( i = 0; i < lm.highWater; i++ ) {
        num = LM_TEXNUM + i;
        qglDeleteTextures( 1, &num );
    }
    lm.highWater = 0;

    r_notexture = NULL;
    r_particletexture = NULL;
    r_beamtexture = NULL;
    r_warptexture = NULL;
    r_whiteimage = NULL;
    r_blackimage = NULL;

    IMG_FreeAll();
    IMG_Shutdown();

    Scrap_Shutdown();
}

