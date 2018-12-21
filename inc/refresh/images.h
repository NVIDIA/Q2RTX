/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2003-2006 Andrey Nazarov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef IMAGES_H
#define IMAGES_H

//
// images.h -- common image manager
//

#include "shared/list.h"
#include "common/files.h"
#include "common/zone.h"
#include "common/error.h"
#include "refresh/refresh.h"

#define R_Malloc(size)      Z_TagMalloc(size, TAG_RENDERER)
#define R_Mallocz(size)     Z_TagMallocz(size, TAG_RENDERER)

#if USE_REF == REF_GL || USE_REF == REF_GLPT
#define IMG_AllocPixels(x)  FS_AllocTempMem(x)
#define IMG_FreePixels(x)   FS_FreeTempMem(x)
#else
#define IMG_AllocPixels(x)  R_Malloc(x)
#define IMG_FreePixels(x)   Z_Free(x)
#endif

#if USE_REF == REF_SOFT
#define MIPSIZE(c) ((c) * (256 + 64 + 16 + 4) / 256)
#else
#define MIPSIZE(c) (c)
#endif

#define LUMINANCE(r, g, b) ((r) * 0.2126f + (g) * 0.7152f + (b) * 0.0722f)

#define U32_ALPHA   MakeColor(  0,   0,   0, 255)
#define U32_RGB     MakeColor(255, 255, 255,   0)

// absolute limit for OpenGL renderer
#define MAX_TEXTURE_SIZE            2048

typedef enum {
    IM_PCX,
    IM_WAL,
#if USE_TGA
    IM_TGA,
#endif
#if USE_JPG
    IM_JPG,
#endif
#if USE_PNG
    IM_PNG,
#endif
    IM_MAX
} imageformat_t;

typedef struct image_s {
    list_t          entry;
    char            name[MAX_QPATH]; // game path
    int             baselen; // without extension
    imagetype_t     type;
    imageflags_t    flags;
    int             width, height; // source image
    int             upload_width, upload_height; // after power of two and picmip
    int             registration_sequence; // 0 = free
#if USE_REF == REF_GL || USE_REF == REF_GLPT
    unsigned        texnum; // gl texture binding
    float           sl, sh, tl, th;
#elif USE_REF == REF_VKPT
    byte            *pix_data; // todo: add miplevels
    int             material_idx;
    uint32_t        light_color; // use this color if this is a light source
    int             is_light;
#else
    byte            *pixels[4]; // mip levels
#endif
#if USE_REF == REF_GLPT
    uint64_t        texhandle; // gl handle for bindless textures
#endif
} image_t;

#define MAX_RIMAGES     1024

extern image_t  r_images[MAX_RIMAGES];
extern int      r_numImages;

extern int registration_sequence;

#define R_NOTEXTURE &r_images[0]

extern uint32_t d_8to24table[256];

// these are implemented in src/refresh/images.c
image_t *IMG_Find(const char *name, imagetype_t type, imageflags_t flags);
void IMG_FreeUnused(void);
void IMG_FreeAll(void);
void IMG_Init(void);
void IMG_Shutdown(void);
void IMG_GetPalette(void);

image_t *IMG_ForHandle(qhandle_t h);

void IMG_ResampleTexture(const byte *in, int inwidth, int inheight,
                         byte *out, int outwidth, int outheight);
void IMG_MipMap(byte *out, byte *in, int width, int height);

// these are implemented in src/refresh/[gl,sw]/images.c
void IMG_Unload(image_t *image);
void IMG_Load(image_t *image, byte *pic);
byte *IMG_ReadPixels(int *width, int *height, int *rowbytes);

#endif // IMAGES_H

/* vim: set ts=8 sw=4 tw=0 et : */
