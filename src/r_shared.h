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


// absolute limit for OpenGL renderer
#if USE_REF == REF_GL
#define MAX_TEXTURE_SIZE            2048
#else
#define MAX_TEXTURE_SIZE            512
#endif

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
    if_scrap        = ( 1 << 2 ),
    if_replace_wal  = ( 1 << 3 ),
    if_replace_pcx  = ( 1 << 4 ),
    if_auto         = ( 1 << 5 )
} imageflags_t;

typedef enum {
    it_skin,
    it_sprite,
    it_wall,
    it_pic,
    it_sky,
    it_charset
} imagetype_t;

#define EXTENSION_PNG    MakeRawLong( '.', 'p', 'n', 'g' )
#define EXTENSION_TGA    MakeRawLong( '.', 't', 'g', 'a' )
#define EXTENSION_JPG    MakeRawLong( '.', 'j', 'p', 'g' )
#define EXTENSION_PCX    MakeRawLong( '.', 'p', 'c', 'x' )
#define EXTENSION_WAL    MakeRawLong( '.', 'w', 'a', 'l' )

typedef struct image_s {
    list_t          entry;
    char            name[MAX_QPATH]; // game path, without extension
    //int             baselength; // length of the path without extension
    imagetype_t     type;
    int             width, height; // source image
    int             upload_width, upload_height; // after power of two and picmip
    int             registration_sequence; // 0 = free
#if USE_REF == REF_GL
    unsigned        texnum; // gl texture binding
    float           sl, sh, tl, th;
#else
    byte            *pixels[4]; // mip levels
#endif
    imageflags_t    flags;
} image_t;

#define MAX_RIMAGES     1024

extern image_t     r_images[MAX_RIMAGES];
extern int         r_numImages;

extern int registration_sequence;

extern image_t *r_notexture;

extern uint32_t        d_8to24table[256];

// these are implemented in r_images.c
image_t *IMG_Alloc( const char *name );
image_t *IMG_Find( const char *name, imagetype_t type );
image_t *IMG_Create( const char *name, byte *pic, int width, int height,
                       imagetype_t type, imageflags_t flags );
void IMG_FreeUnused( void );
void IMG_FreeAll( void );
void IMG_Init( void );
void IMG_Shutdown( void );
void IMG_GetPalette( byte **dest );

image_t *IMG_ForHandle( qhandle_t h );
qhandle_t R_RegisterSkin( const char *name );
qhandle_t R_RegisterPic( const char *name );
qhandle_t R_RegisterFont( const char *name );

qerror_t IMG_LoadPCX( const char *filename, byte **pic, byte *palette,
                    int *width, int *height );
qerror_t IMG_WritePCX( qhandle_t f, const char *filename, const byte *data, int width,
                    int height, int rowbytes, byte *palette ); 

#if USE_TGA || USE_JPG || USE_PNG
typedef qerror_t (img_load_t)( const char *, byte **, int *, int * );
typedef qerror_t (img_save_t)( qhandle_t, const char *, const byte *, int, int, int );
#endif

#if USE_TGA
qerror_t IMG_LoadTGA( const char *filename, byte **pic, int *width, int *height );
qerror_t IMG_SaveTGA( qhandle_t f, const char *filename, const byte *bgr,
                        int width, int height, int unused );
#endif

#if USE_JPG
qerror_t IMG_LoadJPG( const char *filename, byte **pic, int *width, int *height );
qerror_t IMG_SaveJPG( qhandle_t f, const char *filename, const byte *rgb,
                        int width, int height, int quality );
#endif

#if USE_PNG
qerror_t IMG_LoadPNG( const char *filename, byte **pic, int *width, int *height );
qerror_t IMG_SavePNG( qhandle_t f, const char *filename, const byte *rgb,
                        int width, int height, int compression ); 
#endif

// these are implemented in [gl,sw]_images.c
void IMG_Unload( image_t *image );
void IMG_Load( image_t *image, byte *pic, int width, int height,
                imagetype_t type, imageflags_t flags );
image_t *IMG_LoadWAL( const char *name );




