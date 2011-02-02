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

/*
=============================================================================

IMAGE MANAGER

=============================================================================
*/

#if USE_BGRA
#define MakeColor( r, g, b, a )        MakeRawLong( b, g, r, a )
#else
#define MakeColor( r, g, b, a )        MakeRawLong( r, g, b, a )
#endif

#define R_Malloc( size )    Z_TagMalloc( size, TAG_RENDERER )
#define R_Mallocz( size )    Z_TagMallocz( size, TAG_RENDERER )

#if USE_REF == REF_GL
#define IMG_AllocPixels( x )  FS_AllocTempMem( x )
#define IMG_FreePixels( x ) FS_FreeFile( x )
#else
#define IMG_AllocPixels( x )  R_Malloc( x )
#define IMG_FreePixels( x ) Z_Free( x )
#endif

#if USE_REF == REF_SOFT
#define MIPSIZE(c) ((c)*(256+64+16+4)/256)
#else
#define MIPSIZE(c) (c)
#endif

// absolute limit for OpenGL renderer
#if USE_REF == REF_GL
#define MAX_TEXTURE_SIZE            2048
#else
#define MAX_TEXTURE_SIZE            512
#endif

// size of GL_Upload8 internal buffer
#define MAX_PALETTED_PIXELS         (512*256)

/*

  skins will be outline flood filled and mip mapped
  pics and sprites with alpha will be outline flood filled
  pic won't be mip mapped

  model skin
  sprite frame
  wall texture
  pic

*/

typedef enum {
    if_transparent  = ( 1 << 0 ),
    if_paletted     = ( 1 << 1 ),
    if_scrap        = ( 1 << 2 )
} imageflags_t;

typedef enum {
    it_skin,
    it_sprite,
    it_wall,
    it_pic,
    it_sky,
    it_charset
} imagetype_t;

typedef enum {
    im_pcx,
    im_wal,
#if USE_TGA
    im_tga,
#endif
#if USE_JPG
    im_jpg,
#endif
#if USE_PNG
    im_png,
#endif
    im_unknown
} imageformat_t;

typedef struct image_s {
    list_t          entry;
    char            name[MAX_QPATH]; // game path, without extension
    imagetype_t     type;
    imageflags_t    flags;
    int             width, height; // source image
    int             upload_width, upload_height; // after power of two and picmip
    int             registration_sequence; // 0 = free
#if USE_REF == REF_GL
    unsigned        texnum; // gl texture binding
    float           sl, sh, tl, th;
#else
    byte            *pixels[4]; // mip levels
#endif
} image_t;

#define MAX_RIMAGES     1024

extern image_t  r_images[MAX_RIMAGES];
extern int      r_numImages;

extern int registration_sequence;

#define R_NOTEXTURE &r_images[0]

extern uint32_t d_8to24table[256];

// these are implemented in r_images.c
image_t *IMG_Find( const char *name, imagetype_t type );
void IMG_FreeUnused( void );
void IMG_FreeAll( void );
void IMG_Init( void );
void IMG_Shutdown( void );
byte *IMG_GetPalette( void );

image_t *IMG_ForHandle( qhandle_t h );
qhandle_t R_RegisterSkin( const char *name );
qhandle_t R_RegisterPic( const char *name );
qhandle_t R_RegisterFont( const char *name );

// these are implemented in [gl,sw]_images.c
void IMG_Unload( image_t *image );
void IMG_Load( image_t *image, byte *pic, int width, int height );
byte *IMG_ReadPixels( qboolean reverse, int *width, int *height );

