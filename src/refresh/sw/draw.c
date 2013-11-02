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

#define STEP                                                    \
    dst[0] = (dst[0] * (255 - src[3]) + src[2] * src[3]) >> 8;  \
    dst[1] = (dst[1] * (255 - src[3]) + src[1] * src[3]) >> 8;  \
    dst[2] = (dst[2] * (255 - src[3]) + src[0] * src[3]) >> 8;  \
    dst += VID_BYTES;                                           \
    src += TEX_BYTES;

#define STEP_M                                                  \
    tmp[0] = (src[0] * color.u8[0]) >> 8;                       \
    tmp[1] = (src[1] * color.u8[1]) >> 8;                       \
    tmp[2] = (src[2] * color.u8[2]) >> 8;                       \
    tmp[3] = (src[3] * color.u8[3]) >> 8;                       \
    dst[0] = (dst[0] * (255 - tmp[3]) + tmp[2] * tmp[3]) >> 8;  \
    dst[1] = (dst[1] * (255 - tmp[3]) + tmp[1] * tmp[3]) >> 8;  \
    dst[2] = (dst[2] * (255 - tmp[3]) + tmp[0] * tmp[3]) >> 8;  \
    dst += VID_BYTES;                                           \
    src += TEX_BYTES;

#define STRETCH                                                     \
    _src = src + (u >> 16) * TEX_BYTES;                             \
    dst[0] = (dst[0] * (255 - _src[3]) + _src[2] * _src[3]) >> 8;   \
    dst[1] = (dst[1] * (255 - _src[3]) + _src[1] * _src[3]) >> 8;   \
    dst[2] = (dst[2] * (255 - _src[3]) + _src[0] * _src[3]) >> 8;   \
    dst += VID_BYTES;                                               \
    u += ustep;

#define STRETCH_M                                               \
    _src = src + (u >> 16) * TEX_BYTES;                         \
    tmp[0] = (_src[0] * color.u8[0]) >> 8;                      \
    tmp[1] = (_src[1] * color.u8[1]) >> 8;                      \
    tmp[2] = (_src[2] * color.u8[2]) >> 8;                      \
    tmp[3] = (_src[3] * color.u8[3]) >> 8;                      \
    dst[0] = (dst[0] * (255 - tmp[3]) + tmp[2] * tmp[3]) >> 8;  \
    dst[1] = (dst[1] * (255 - tmp[3]) + tmp[1] * tmp[3]) >> 8;  \
    dst[2] = (dst[2] * (255 - tmp[3]) + tmp[0] * tmp[3]) >> 8;  \
    dst += VID_BYTES;                                           \
    u += ustep;

#define ROW1(DO)    \
    do {            \
        DO;         \
    } while (--count)

#define ROW4(DO)    \
    do {            \
        DO;         \
        DO;         \
        DO;         \
        DO;         \
                    \
        count -= 4; \
    } while (count)

#define ROW8(DO)    \
    do {            \
        DO;         \
        DO;         \
        DO;         \
        DO;         \
        DO;         \
        DO;         \
        DO;         \
        DO;         \
                    \
        count -= 8; \
    } while (count)

typedef struct {
    color_t     colors[2];
    clipRect_t  clip;
} drawStatic_t;

static drawStatic_t draw;

float R_ClampScale(cvar_t *var)
{
    if (var) {
        Cvar_SetValue(var, 1.0f, FROM_CODE);
    }
    return 1.0f;
}

void R_SetScale(float scale)
{
}

void R_InitDraw(void)
{
    memset(&draw, 0, sizeof(draw));
    draw.colors[0].u32 = U32_WHITE;
    draw.colors[1].u32 = U32_WHITE;
    draw.clip.left = 0;
    draw.clip.top = 0;
    draw.clip.right = r_config.width;
    draw.clip.bottom = r_config.height;
}

void R_ClearColor(void)
{
    draw.colors[0].u32 = U32_WHITE;
    draw.colors[1].u32 = U32_WHITE;
}

void R_SetAlpha(float alpha)
{
    draw.colors[0].u8[3] = alpha * 255;
    draw.colors[1].u8[3] = alpha * 255;
}

void R_SetColor(uint32_t color)
{
    draw.colors[0].u32 = color;
    draw.colors[1].u8[3] = draw.colors[0].u8[3];
}

void R_SetClipRect(const clipRect_t *clip)
{
    if (!clip) {
clear:
        draw.clip.left = 0;
        draw.clip.top = 0;
        draw.clip.right = r_config.width;
        draw.clip.bottom = r_config.height;
        return;
    }

    draw.clip = *clip;

    if (draw.clip.left < 0)
        draw.clip.left = 0;
    if (draw.clip.top < 0)
        draw.clip.top = 0;
    if (draw.clip.right > r_config.width)
        draw.clip.right = r_config.width;
    if (draw.clip.bottom > r_config.height)
        draw.clip.bottom = r_config.height;
    if (draw.clip.right < draw.clip.left)
        goto clear;
    if (draw.clip.bottom < draw.clip.top)
        goto clear;
}

/*
=============
R_DrawStretchData
=============
*/
static void R_DrawStretchData(int x, int y, int w, int h, int xx, int yy,
                              int ww, int hh, int pitch, byte *data, color_t color)
{
    byte    *srcpixels, *dstpixels, *dst, *src;
    int     v, u;
    int     ustep, vstep;
    int     skipv = 0, skipu = 0;
    int     width = w, height = h;
    int     count;
    byte    *_src;
    int     tmp[4];

    if (x < draw.clip.left) {
        skipu = draw.clip.left - x;
        w -= skipu;
        x = draw.clip.left;
    }
    if (y < draw.clip.top) {
        skipv = draw.clip.top - y;
        h -= skipv;
        y = draw.clip.top;
    }

    if (x + w > draw.clip.right)
        w = draw.clip.right - x;
    if (y + h > draw.clip.bottom)
        h = draw.clip.bottom - y;
    if (w <= 0 || h <= 0)
        return;

    srcpixels = data + yy * pitch + xx * TEX_BYTES;
    dstpixels = vid.buffer + y * vid.rowbytes + x * VID_BYTES;

    vstep = hh * 0x10000 / height;

    v = skipv * vstep;
    if (color.u32 == U32_WHITE) {
        if (width == ww) {
            dstpixels += skipu * VID_BYTES;
            do {
                src = &srcpixels[(v >> 16) * pitch];
                dst = dstpixels;
                count = w;

                if (!(w & 7)) {
                    ROW8(STEP);
                } else if (!(w & 3)) {
                    ROW4(STEP);
                } else {
                    ROW1(STEP);
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
                    ROW8(STRETCH);
                } else if (!(w & 3)) {
                    ROW4(STRETCH);
                } else {
                    ROW1(STRETCH);
                }

                v += vstep;
                dstpixels += vid.rowbytes;
            } while (--h);
        }
    } else {
        if (width == ww) {
            dstpixels += skipu * VID_BYTES;
            do {
                src = &srcpixels[(v >> 16) * pitch];
                dst = dstpixels;
                count = w;

                if (!(w & 7)) {
                    ROW8(STEP_M);
                } else if (!(w & 3)) {
                    ROW4(STEP_M);
                } else {
                    ROW1(STEP_M);
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
                    ROW8(STRETCH_M);
                } else if (!(w & 3)) {
                    ROW4(STRETCH_M);
                } else {
                    ROW1(STRETCH_M);
                }

                v += vstep;
                dstpixels += vid.rowbytes;
            } while (--h);
        }
    }
}

/*
=============
R_DrawFixedData
=============
*/
static void R_DrawFixedData(int x, int y, int w, int h,
                            int pitch, byte *data, color_t color)
{
    byte    *srcpixels, *dstpixels;
    byte    *dst, *src;
    int     skipv = 0, skipu = 0;
    int     count;
    int     tmp[4];

    if (x < draw.clip.left) {
        skipu = draw.clip.left - x;
        w -= skipu;
        x = draw.clip.left;
    }
    if (y < draw.clip.top) {
        skipv = draw.clip.top - y;
        h -= skipv;
        y = draw.clip.top;
    }

    if (x + w > draw.clip.right)
        w = draw.clip.right - x;
    if (y + h > draw.clip.bottom)
        h = draw.clip.bottom - y;
    if (w <= 0 || h <= 0)
        return;

    srcpixels = data + skipv * pitch + skipu * TEX_BYTES;
    dstpixels = vid.buffer + y * vid.rowbytes + x * VID_BYTES;

    if (color.u32 == U32_WHITE) {
        if (!(w & 7)) {
            do {
                src = srcpixels;
                dst = dstpixels;
                count = w; ROW8(STEP);
                srcpixels += pitch;
                dstpixels += vid.rowbytes;
            } while (--h);
        } else if (!(w & 3)) {
            do {
                src = srcpixels;
                dst = dstpixels;
                count = w; ROW4(STEP);
                srcpixels += pitch;
                dstpixels += vid.rowbytes;
            } while (--h);
        } else {
            do {
                src = srcpixels;
                dst = dstpixels;
                count = w; ROW1(STEP);
                srcpixels += pitch;
                dstpixels += vid.rowbytes;
            } while (--h);
        }
    } else {
        if (!(w & 7)) {
            do {
                src = srcpixels;
                dst = dstpixels;
                count = w; ROW8(STEP_M);
                srcpixels += pitch;
                dstpixels += vid.rowbytes;
            } while (--h);
        } else if (!(w & 3)) {
            do {
                src = srcpixels;
                dst = dstpixels;
                count = w; ROW4(STEP_M);
                srcpixels += pitch;
                dstpixels += vid.rowbytes;
            } while (--h);
        } else {
            do {
                src = srcpixels;
                dst = dstpixels;
                count = w; ROW1(STEP_M);
                srcpixels += pitch;
                dstpixels += vid.rowbytes;
            } while (--h);
        }
    }
}

/*
=============
R_DrawStretchPic
=============
*/
void R_DrawStretchPic(int x, int y, int w, int h, qhandle_t pic)
{
    image_t *image = IMG_ForHandle(pic);

    if (w == image->upload_width && h == image->upload_height)
        R_DrawFixedData(x, y, image->upload_width, image->upload_height,
                        image->upload_width * TEX_BYTES, image->pixels[0], draw.colors[0]);
    else
        R_DrawStretchData(x, y, w, h, 0, 0, image->upload_width, image->upload_height,
                          image->upload_width * TEX_BYTES, image->pixels[0], draw.colors[0]);
}

/*
=============
R_DrawStretcpic
=============
*/
void R_DrawPic(int x, int y, qhandle_t pic)
{
    image_t *image = IMG_ForHandle(pic);

    if (image->width == image->upload_width && image->height == image->upload_height)
        R_DrawFixedData(x, y, image->upload_width, image->upload_height,
                        image->upload_width * TEX_BYTES, image->pixels[0], draw.colors[0]);
    else
        R_DrawStretchData(x, y, image->width, image->height, 0, 0, image->upload_width, image->upload_height,
                          image->upload_width * TEX_BYTES, image->pixels[0], draw.colors[0]);
}

static inline void draw_char(int x, int y, int flags, int ch, image_t *image)
{
    int x2, y2;
    byte *data;

    if ((ch & 127) == 32)
        return;

    if (flags & UI_ALTCOLOR)
        ch |= 0x80;

    if (flags & UI_XORCOLOR)
        ch ^= 0x80;

    x2 = (ch & 15) * CHAR_WIDTH;
    y2 = ((ch >> 4) & 15) * CHAR_HEIGHT;
    data = image->pixels[0] + y2 * image->upload_width * TEX_BYTES + x2 * TEX_BYTES;

    R_DrawFixedData(x, y, CHAR_WIDTH, CHAR_HEIGHT, image->upload_width * TEX_BYTES, data, draw.colors[ch >> 7]);
}

void R_DrawChar(int x, int y, int flags, int ch, qhandle_t font)
{
    image_t *image;

    if (!font)
        return;

    image = IMG_ForHandle(font);
    if (image->upload_width != 128 || image->upload_height != 128)
        return;

    draw_char(x, y, flags, ch & 255, image);
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

    if (!font)
        return x;

    image = IMG_ForHandle(font);
    if (image->upload_width != 128 || image->upload_height != 128)
        return x;

    while (maxChars-- && *string) {
        byte c = *string++;
        draw_char(x, y, flags, c, image);
        x += CHAR_WIDTH;
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

    if (!pic)
        return;

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
    if (image->upload_width != 64 || image->upload_height != 64)
        return;

    x2 = x + w;
    pdest = vid.buffer + y * vid.rowbytes;
    for (i = 0; i < h; i++, pdest += vid.rowbytes) {
        psrc = image->pixels[0] + 64 * TEX_BYTES * ((i + y) & 63);
        for (j = x; j < x2; j++) {
            pdest[j * VID_BYTES + 0] = psrc[(j & 63) * TEX_BYTES + 2];
            pdest[j * VID_BYTES + 1] = psrc[(j & 63) * TEX_BYTES + 1];
            pdest[j * VID_BYTES + 2] = psrc[(j & 63) * TEX_BYTES + 0];
        }
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
    byte        *dest;
    int         u, v;
    color_t     color;

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

    color.u32 = d_8to24table[c & 0xff];

    dest = vid.buffer + y * vid.rowbytes + x * VID_BYTES;
    for (v = 0; v < h; v++, dest += vid.rowbytes) {
        for (u = 0; u < w; u++) {
            dest[u * VID_BYTES + 0] = color.u8[2];
            dest[u * VID_BYTES + 1] = color.u8[1];
            dest[u * VID_BYTES + 2] = color.u8[0];
        }
    }
}

void R_DrawFill32(int x, int y, int w, int h, uint32_t c)
{
    byte        *dest;
    int         u, v;
    color_t     color;
    int         alpha, one_minus_alpha;
    fixed8_t    pre[3];

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

    color.u32 = c;

    alpha = color.u8[3];
    one_minus_alpha = 255 - alpha;

    pre[0] = color.u8[0] * alpha;
    pre[1] = color.u8[1] * alpha;
    pre[2] = color.u8[2] * alpha;

    dest = vid.buffer + y * vid.rowbytes + x * VID_BYTES;
    for (v = 0; v < h; v++, dest += vid.rowbytes) {
        for (u = 0; u < w; u++) {
            dest[u * VID_BYTES + 0] = (dest[u * VID_BYTES + 0] * one_minus_alpha + pre[2]) >> 8;
            dest[u * VID_BYTES + 1] = (dest[u * VID_BYTES + 1] * one_minus_alpha + pre[1]) >> 8;
            dest[u * VID_BYTES + 2] = (dest[u * VID_BYTES + 2] * one_minus_alpha + pre[0]) >> 8;
        }
    }
}
