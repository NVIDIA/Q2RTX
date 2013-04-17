/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "sw.h"

static byte gammatable[256];

/*
================
IMG_Unload
================
*/
void IMG_Unload(image_t *image)
{
    Z_Free(image->pixels[0]);
    image->pixels[0] = NULL;
}

static void R_LightScaleTexture(byte *in, int inwidth, int inheight)
{
    int     i, c;
    byte    *p;

    p = in;
    c = inwidth * inheight;

    for (i = 0; i < c; i++, p += TEX_BYTES) {
        p[0] = gammatable[p[0]];
        p[1] = gammatable[p[1]];
        p[2] = gammatable[p[2]];
    }
}

/*
================
IMG_Load
================
*/
void IMG_Load(image_t *image, byte *pic)
{
    int     i, c, b;
    int     width, height;

    width = image->upload_width;
    height = image->upload_height;

    if (image->flags & IF_TURBULENT) {
        image->width = TURB_SIZE;
        image->height = TURB_SIZE;
    }

    b = image->width * image->height;
    c = width * height;

    if (image->type == IT_WALL) {
        image->pixels[0] = R_Malloc(MIPSIZE(b) * TEX_BYTES);
        image->pixels[1] = image->pixels[0] + b * TEX_BYTES;
        image->pixels[2] = image->pixels[1] + b * TEX_BYTES / 4;
        image->pixels[3] = image->pixels[2] + b * TEX_BYTES / 16;

        if (!(r_config.flags & QVF_GAMMARAMP))
            R_LightScaleTexture(pic, width, height);

        if (width == image->width && height == image->height) {
            memcpy(image->pixels[0], pic, width * height * TEX_BYTES);
        } else {
            IMG_ResampleTexture(pic, width, height, image->pixels[0], image->width, image->height);
            image->upload_width = image->width;
            image->upload_height = image->height;
        }

        IMG_MipMap(image->pixels[1], image->pixels[0], image->width >> 0, image->height >> 0);
        IMG_MipMap(image->pixels[2], image->pixels[1], image->width >> 1, image->height >> 1);
        IMG_MipMap(image->pixels[3], image->pixels[2], image->width >> 2, image->height >> 2);

        Z_Free(pic);
    } else {
        image->pixels[0] = pic;

        if (!(image->flags & IF_OPAQUE)) {
            for (i = 0; i < c; i++) {
                b = pic[i * TEX_BYTES + 3];
                if (b != 255) {
                    image->flags |= IF_TRANSPARENT;
                }
            }
        }
    }

    if (image->type == IT_SKIN && !(r_config.flags & QVF_GAMMARAMP))
        R_LightScaleTexture(image->pixels[0], width, height);
}

void R_BuildGammaTable(void)
{
    int     i, inf;
    float   g = vid_gamma->value;

    if (g == 1.0) {
        for (i = 0; i < 256; i++)
            gammatable[i] = i;
        return;
    }

    for (i = 0; i < 256; i++) {
        inf = 255 * pow((i + 0.5) / 255.5, g) + 0.5;
        gammatable[i] = clamp(inf, 0, 255);
    }
}

#define NTX     256

static void R_CreateNotexture(void)
{
    static byte buffer[MIPSIZE(NTX * NTX) * TEX_BYTES];
    int         x, y, m;
    uint32_t    *p;
    image_t     *ntx;

// create a simple checkerboard texture for the default
    ntx = R_NOTEXTURE;
    ntx->type = IT_WALL;
    ntx->flags = 0;
    ntx->width = ntx->height = NTX;
    ntx->upload_width = ntx->upload_height = NTX;
    ntx->pixels[0] = buffer;
    ntx->pixels[1] = ntx->pixels[0] + NTX * NTX * TEX_BYTES;
    ntx->pixels[2] = ntx->pixels[1] + NTX * NTX * TEX_BYTES / 4;
    ntx->pixels[3] = ntx->pixels[2] + NTX * NTX * TEX_BYTES / 16;

    for (m = 0; m < 4; m++) {
        p = (uint32_t *)ntx->pixels[m];
        for (y = 0; y < (NTX >> m); y++) {
            for (x = 0; x < (NTX >> m); x++) {
                if ((x ^ y) & (1 << (3 - m)))
                    *p++ = U32_BLACK;
                else
                    *p++ = U32_WHITE;
            }
        }
    }
}

/*
===============
R_InitImages
===============
*/
void R_InitImages(void)
{
    registration_sequence = 1;

    IMG_GetPalette();

    R_CreateNotexture();

    R_BuildGammaTable();
}

/*
===============
R_ShutdownImages
===============
*/
void R_ShutdownImages(void)
{
    IMG_FreeAll();
}

