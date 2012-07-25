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

// draw.c

#include "sw.h"


//=============================================================================



#define DOSTEP do {             \
            tbyte = *src++;     \
            if (tbyte != 255)   \
                *dst = tbyte;   \
            dst++;              \
        } while (0)

#define DOMSTEP do {            \
            if (*src++ != 255)  \
                *dst = tbyte;   \
            dst++;              \
        } while (0)

#define DOSTRETCH do {              \
            tbyte = src[u >> 16];   \
            if (tbyte != 255)       \
                *dst = tbyte;       \
            dst++;                  \
            u += ustep;             \
        } while (0)

#define ROW1 do {   \
        DOSTEP;     \
    } while (--count);

#define ROW4 do {   \
        DOSTEP;     \
        DOSTEP;     \
        DOSTEP;     \
        DOSTEP;     \
                    \
        count -= 4; \
    } while (count);

#define ROW8 do {   \
        DOSTEP;     \
        DOSTEP;     \
        DOSTEP;     \
        DOSTEP;     \
        DOSTEP;     \
        DOSTEP;     \
        DOSTEP;     \
        DOSTEP;     \
                    \
        count -= 8; \
    } while (count);

#define MROW8 do {  \
        DOMSTEP;        \
        DOMSTEP;        \
        DOMSTEP;        \
        DOMSTEP;        \
        DOMSTEP;        \
        DOMSTEP;        \
        DOMSTEP;        \
        DOMSTEP;        \
                    \
        count -= 8; \
    } while (count);

#define STRETCH1 do {   \
        DOSTRETCH;      \
    } while (--count);

#define STRETCH4 do {   \
        DOSTRETCH;      \
        DOSTRETCH;      \
        DOSTRETCH;      \
        DOSTRETCH;      \
                        \
        count -= 4;     \
    } while (count);

#define STRETCH8 do {   \
        DOSTRETCH;      \
        DOSTRETCH;      \
        DOSTRETCH;      \
        DOSTRETCH;      \
        DOSTRETCH;      \
        DOSTRETCH;      \
        DOSTRETCH;      \
        DOSTRETCH;      \
                        \
        count -= 8;     \
    } while (count);

typedef struct {
    int colorIndex;
    int colorFlags;
    clipRect_t clipRect;
    int flags;
} drawStatic_t;

static drawStatic_t draw;

static int  colorIndices[8];

void R_SetScale(float *scale)
{
    if (scale) {
        *scale = 1;
    }
}

void R_InitDraw(void)
{
    int i;

    memset(&draw, 0, sizeof(draw));
    draw.colorIndex = -1;

    for (i = 0; i < 8; i++) {
        colorIndices[i] = R_IndexForColor(colorTable[i]);
    }
}

void R_ClearColor(void)
{
    draw.colorIndex = -1;
}

void R_SetAlpha(float alpha)
{
}

void R_SetColor(uint32_t color)
{
    draw.colorIndex = R_IndexForColor(color);
}

void R_SetClipRect(int flags, const clipRect_t *clip)
{
    draw.flags &= ~DRAW_CLIP_MASK;

    if (flags == DRAW_CLIP_DISABLED) {
        return;
    }
    draw.flags |= flags;
    draw.clipRect = *clip;
}

/*
=============
R_GetPicSize
=============
*/
qboolean R_GetPicSize(int *w, int *h, qhandle_t pic)
{
    image_t *image = IMG_ForHandle(pic);

    if (w) {
        *w = image->width;
    }
    if (h) {
        *h = image->height;
    }
    return image->flags & if_transparent;
}

/*
=============
R_DrawStretchData
=============
*/
static void R_DrawStretchData(int x, int y, int w, int h, int xx, int yy,
                              int ww, int hh, int pitch, byte *data)
{
    byte            *srcpixels, *dstpixels, *dst, *src;
    int             v, u;
    int             ustep, vstep;
    int             skipv, skipu;
    int             width, height;
    byte            tbyte;
    int         count;

    skipv = skipu = 0;
    width = w;
    height = h;

    if (draw.flags & DRAW_CLIP_MASK) {
        clipRect_t *clip = &draw.clipRect;

        if (draw.flags & DRAW_CLIP_LEFT) {
            if (x < clip->left) {
                skipu = clip->left - x;
                if (w <= skipu) {
                    return;
                }
                w -= skipu;
                x = clip->left;
            }
        }

        if (draw.flags & DRAW_CLIP_RIGHT) {
            if (x >= clip->right) {
                return;
            }
            if (x + w > clip->right) {
                w = clip->right - x;
            }
        }

        if (draw.flags & DRAW_CLIP_TOP) {
            if (y < clip->top) {
                skipv = clip->top - y;
                if (h <= skipv) {
                    return;
                }
                h -= skipv;
                y = clip->top;
            }
        }

        if (draw.flags & DRAW_CLIP_BOTTOM) {
            if (y >= clip->bottom) {
                return;
            }
            if (y + h > clip->bottom) {
                h = clip->bottom - y;
            }
        }
    }

    srcpixels = data + yy * pitch + xx;
    dstpixels = vid.buffer + y * vid.rowbytes + x;

    vstep = hh * 0x10000 / height;

    v = skipv * vstep;
    if (width == ww) {
        dstpixels += skipu;
        do {
            src = &srcpixels[(v >> 16) * pitch];
            dst = dstpixels;
            count = w;

            if (!(w & 7)) {
                ROW8;
            } else if (!(w & 3)) {
                ROW4;
            } else {
                ROW1;
            }

            v += vstep;
            dstpixels += vid.rowbytes;
        } while (--h);
    } else {
        ustep = ww * 0x10000 / width;
        skipu = skipu * ustep;
        do {
            src = &srcpixels[(v >> 16) * pitch];
            dst = dstpixels;
            count = w;

            u = skipu;
            if (!(w & 7)) {
                STRETCH8;
            } else if (!(w & 3)) {
                STRETCH4;
            } else {
                STRETCH1;
            }

            v += vstep;
            dstpixels += vid.rowbytes;
        } while (--h);
    }

}

/*
=============
R_DrawFixedData
=============
*/
static void R_DrawFixedData(int x, int y, int w, int h,
                            int pitch, byte *data)
{
    byte *srcpixels, *dstpixels;
    byte            *dst, *src;
    int             skipv, skipu;
    byte            tbyte;
    int         count;

    skipv = skipu = 0;

    if (draw.flags & DRAW_CLIP_MASK) {
        clipRect_t *clip = &draw.clipRect;

        if (draw.flags & DRAW_CLIP_LEFT) {
            if (x < clip->left) {
                skipu = clip->left - x;
                if (w <= skipu) {
                    return;
                }
                w -= skipu;
                x = clip->left;
            }
        }

        if (draw.flags & DRAW_CLIP_RIGHT) {
            if (x >= clip->right) {
                return;
            }
            if (x + w > clip->right) {
                w = clip->right - x;
            }
        }

        if (draw.flags & DRAW_CLIP_TOP) {
            if (y < clip->top) {
                skipv = clip->top - y;
                if (h <= skipv) {
                    return;
                }
                h -= skipv;
                y = clip->top;
            }
        }

        if (draw.flags & DRAW_CLIP_BOTTOM) {
            if (y >= clip->bottom) {
                return;
            }
            if (y + h > clip->bottom) {
                h = clip->bottom - y;
            }
        }
    }

    srcpixels = data + skipv * pitch + skipu;
    dstpixels = vid.buffer + y * vid.rowbytes + x;

    if (!(w & 7)) {
        do {
            src = srcpixels;
            dst = dstpixels;
            count = w; ROW8;
            srcpixels += pitch;
            dstpixels += vid.rowbytes;
        } while (--h);
    } else if (!(w & 3)) {
        do {
            src = srcpixels;
            dst = dstpixels;
            count = w; ROW4;
            srcpixels += pitch;
            dstpixels += vid.rowbytes;
        } while (--h);
    } else {
        do {
            src = srcpixels;
            dst = dstpixels;
            count = w; ROW1;
            srcpixels += pitch;
            dstpixels += vid.rowbytes;
        } while (--h);
    }

}

static void R_DrawFixedDataAsMask(int x, int y, int w, int h,
                                  int pitch, byte *data, byte tbyte)
{
    byte *srcpixels, *dstpixels;
    byte            *dst, *src;
    int             skipv, skipu;
    int         count;

    skipv = skipu = 0;

    if (draw.flags & DRAW_CLIP_MASK) {
        clipRect_t *clip = &draw.clipRect;

        if (draw.flags & DRAW_CLIP_LEFT) {
            if (x < clip->left) {
                skipu = clip->left - x;
                if (w <= skipu) {
                    return;
                }
                w -= skipu;
                x = clip->left;
            }
        }

        if (draw.flags & DRAW_CLIP_RIGHT) {
            if (x >= clip->right) {
                return;
            }
            if (x + w > clip->right) {
                w = clip->right - x;
            }
        }

        if (draw.flags & DRAW_CLIP_TOP) {
            if (y < clip->top) {
                skipv = clip->top - y;
                if (h <= skipv) {
                    return;
                }
                h -= skipv;
                y = clip->top;
            }
        }

        if (draw.flags & DRAW_CLIP_BOTTOM) {
            if (y <= clip->bottom) {
                return;
            }
            if (y + h > clip->bottom) {
                h = clip->bottom - y;
            }
        }
    }

    srcpixels = data + skipv * pitch + skipu;
    dstpixels = vid.buffer + y * vid.rowbytes + x;

    if (!(w & 7)) {
        do {
            src = srcpixels;
            dst = dstpixels;
            count = w; MROW8;
            srcpixels += pitch;
            dstpixels += vid.rowbytes;
        } while (--h);
    } else if (!(w & 3)) {
        do {
            src = srcpixels;
            dst = dstpixels;
            count = w; ROW4;
            srcpixels += pitch;
            dstpixels += vid.rowbytes;
        } while (--h);
    } else {
        do {
            src = srcpixels;
            dst = dstpixels;
            count = w; ROW1;
            srcpixels += pitch;
            dstpixels += vid.rowbytes;
        } while (--h);
    }

}

/*
=============
R_DrawStretcpic
=============
*/
void R_DrawStretcPicST(int x, int y, int w, int h, float s1, float t1,
                       float s2, float t2, qhandle_t pic)
{
    image_t *image = IMG_ForHandle(pic);
    int xx, yy, ww, hh;

    xx = image->width * s1;
    yy = image->height * t1;
    ww = image->width * (s2 - s1);
    hh = image->height * (t2 - t1);

    R_DrawStretchData(x, y, w, h, xx, yy, ww, hh,
                      image->width, image->pixels[0]);
}

/*
=============
R_DrawStretchPic
=============
*/
void R_DrawStretchPic(int x, int y, int w, int h, qhandle_t pic)
{
    image_t *image = IMG_ForHandle(pic);

    if (w == image->width && h == image->height) {
        R_DrawFixedData(x, y, image->width, image->height,
                        image->width, image->pixels[0]);
        return;
    }

    R_DrawStretchData(x, y, w, h, 0, 0, image->width, image->height,
                      image->width, image->pixels[0]);
}

/*
=============
R_DrawStretcpic
=============
*/
void R_DrawPic(int x, int y, qhandle_t pic)
{
    image_t *image = IMG_ForHandle(pic);

    R_DrawFixedData(x, y, image->width, image->height,
                    image->width, image->pixels[0]);
}

void R_DrawChar(int x, int y, int flags, int ch, qhandle_t font)
{
    image_t *image;
    int xx, yy;
    byte *data;

    if (!font) {
        return;
    }
    image = IMG_ForHandle(font);
    if (image->width != 128 || image->height != 128) {
        return;
    }

    xx = (ch & 15) << 3;
    yy = ((ch >> 4) & 15) << 3;
    data = image->pixels[0] + yy * image->width + xx;
    if (draw.colorIndex != -1 && !(ch & 128)) {
        R_DrawFixedDataAsMask(x, y, 8, 8, image->width, data, draw.colorIndex);
    } else {
        R_DrawFixedData(x, y, 8, 8, image->width, data);
    }
}

/*
===============
R_DrawString
===============
*/
int R_DrawString(int x, int y, int flags, size_t maxChars,
                 const char *string, qhandle_t font)
{
    image_t *image;
    byte c, *data;
    int xx, yy;
    int color;
    qboolean alt;

    if (!font) {
        return x;
    }
    image = IMG_ForHandle(font);
    if (image->width != 128 || image->height != 128) {
        return x;
    }

    alt = (flags & UI_ALTCOLOR) ? qtrue : qfalse;
    color = draw.colorIndex;

    while (maxChars-- && *string) {
        c = *string++;
        if ((c & 127) == 32) {
            x += 8;
            continue;
        }

        c |= alt << 7;
        xx = (c & 15) << 3;
        yy = (c >> 4) << 3;

        data = image->pixels[0] + yy * image->width + xx;
        if (color != -1 && !(c & 128)) {
            R_DrawFixedDataAsMask(x, y, 8, 8, image->width, data, color);
        } else {
            R_DrawFixedData(x, y, 8, 8, image->width, data);
        }

        x += 8;
    }
    return x;
}

/*
=============
R_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void R_TileClear(int x, int y, int w, int h, qhandle_t pic)
{
    int         i, j;
    byte        *psrc;
    byte        *pdest;
    image_t     *image;
    int         x2;

    if (!pic) {
        return;
    }

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > vid.width)
        w = vid.width - x;
    if (y + h > vid.height)
        h = vid.height - y;
    if (w <= 0 || h <= 0)
        return;

    image = IMG_ForHandle(pic);
    if (image->width != 64 || image->height != 64) {
        return;
    }
    x2 = x + w;
    pdest = vid.buffer + y * vid.rowbytes;
    for (i = 0; i < h; i++, pdest += vid.rowbytes) {
        psrc = image->pixels[0] + image->width * ((i + y) & 63);
        for (j = x; j < x2; j++)
            pdest[j] = psrc[j & 63];
    }
}


/*
=============
R_DrawFill

Fills a box of pixels with a single color
=============
*/
void R_DrawFill8(int x, int y, int w, int h, int c)
{
    byte            *dest;
    int             u, v;

    if (x + w > vid.width)
        w = vid.width - x;
    if (y + h > vid.height)
        h = vid.height - y;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w < 0 || h < 0)
        return;

    dest = vid.buffer + y * vid.rowbytes + x;
    for (v = 0; v < h; v++, dest += vid.rowbytes)
        for (u = 0; u < w; u++)
            dest[u] = c;
}

void R_DrawFill32(int x, int y, int w, int h, uint32_t color)
{
    int             c;
    byte            *dest;
    int             u, v;
    int             alpha;

    if (x + w > vid.width)
        w = vid.width - x;
    if (y + h > vid.height)
        h = vid.height - y;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w < 0 || h < 0)
        return;

    c = R_IndexForColor(color);
    alpha = (LittleLong(color) >> 24) & 0xff;

    dest = vid.buffer + y * vid.rowbytes + x;
    if (alpha < 172) {
        if (alpha > 84) {
            for (v = 0; v < h; v++, dest += vid.rowbytes) {
                for (u = 0; u < w; u++) {
                    dest[u] = vid.alphamap[c * 256 + dest[u]];
                }
            }
        } else {
            for (v = 0; v < h; v++, dest += vid.rowbytes) {
                for (u = 0; u < w; u++) {
                    dest[u] = vid.alphamap[c + dest[u] * 256];
                }
            }
        }
    } else {
        for (v = 0; v < h; v++, dest += vid.rowbytes) {
            for (u = 0; u < w; u++) {
                dest[u] = c;
            }
        }
    }
}


//=============================================================================

/*
================
R_DrawFadeScreen

================
*/
void R_DrawFadeScreen(void)
{
    int         x, y;
    byte        *pbuf;
    int t;

    for (y = 0; y < vid.height; y++) {
        pbuf = (byte *)(vid.buffer + vid.rowbytes * y);
        t = (y & 1) << 1;

        for (x = 0; x < vid.width; x++) {
            if ((x & 3) != t)
                pbuf[x] = 0;
        }
    }
}
