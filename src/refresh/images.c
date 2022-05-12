/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2003-2008 Andrey Nazarov
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.

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

//
// images.c -- image reading and writing functions
//

#include "shared/shared.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "../client/client.h"
#include "refresh/images.h"
#include "system/system.h"
#include "format/pcx.h"
#include "format/wal.h"
#include "stb_image.h"
#include "stb_image_write.h"

#include <assert.h>

#define R_COLORMAP_PCX    "pics/colormap.pcx"

#define IMG_LOAD(x) \
    static int IMG_Load##x(byte *rawdata, size_t rawlen, \
        image_t *image, byte **pic)

typedef struct screenshot_s {
    int (*save_cb)(struct screenshot_s *);
    byte *pixels;
    FILE *fp;
    char *filename;
    int width, height, row_stride, status, param;
    bool async;
} screenshot_t;

void stbi_write(void *context, void *data, int size)
{
	fwrite(data, size, 1, ((screenshot_t *) context)->fp);
}

extern cvar_t* vid_rtx;
extern cvar_t* gl_use_hd_assets;

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
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP(off, dx, dy) \
    do { \
        if (pos[off] == fillcolor) { \
            pos[off] = 255; \
            fifo[inpt].x = x + (dx); \
            fifo[inpt].y = y + (dy); \
            inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
        } else if (pos[off] != 255) { \
            fdc = pos[off]; \
        } \
    } while(0)

/*
=================
IMG_FloodFill

Fill background pixels so mipmapping doesn't have haloes
=================
*/
static q_noinline void IMG_FloodFill(byte *skin, int skinwidth, int skinheight)
{
    byte                fillcolor = *skin; // assume this is the pixel to fill
    floodfill_t         fifo[FLOODFILL_FIFO_SIZE];
    int                 inpt = 0, outpt = 0;
    int                 filledcolor = 0; // FIXME: fixed black

    // can't fill to filled color or to transparent color
    // (used as visited marker)
    if (fillcolor == filledcolor || fillcolor == 255) {
        return;
    }

    fifo[inpt].x = 0, fifo[inpt].y = 0;
    inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

    while (outpt != inpt) {
        int         x = fifo[outpt].x, y = fifo[outpt].y;
        int         fdc = filledcolor;
        byte        *pos = &skin[x + skinwidth * y];

        outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

        if (x > 0) FLOODFILL_STEP(-1, -1, 0);
        if (x < skinwidth - 1) FLOODFILL_STEP(1, 1, 0);
        if (y > 0) FLOODFILL_STEP(-skinwidth, 0, -1);
        if (y < skinheight - 1) FLOODFILL_STEP(skinwidth, 0, 1);

        skin[x + skinwidth * y] = fdc;
    }
}

/*
=================================================================

PCX LOADING

=================================================================
*/

static int IMG_DecodePCX(byte *rawdata, size_t rawlen, byte *pixels,
                         byte *palette, int *width, int *height)
{
    byte    *raw, *end;
    dpcx_t  *pcx;
    int     x, y, w, h, scan;
    int     dataByte, runLength;

    //
    // parse the PCX file
    //
    if (rawlen < sizeof(dpcx_t)) {
        return Q_ERR_FILE_TOO_SMALL;
    }

    pcx = (dpcx_t *)rawdata;

    if (pcx->manufacturer != 10 || pcx->version != 5) {
        return Q_ERR_UNKNOWN_FORMAT;
    }

    if (pcx->encoding != 1 || pcx->bits_per_pixel != 8) {
        return Q_ERR_INVALID_FORMAT;
    }

    w = (LittleShort(pcx->xmax) - LittleShort(pcx->xmin)) + 1;
    h = (LittleShort(pcx->ymax) - LittleShort(pcx->ymin)) + 1;
    if (w < 1 || h < 1 || w > 640 || h > 480) {
        return Q_ERR_INVALID_FORMAT;
    }

    if (pcx->color_planes != 1) {
        return Q_ERR_INVALID_FORMAT;
    }

    scan = LittleShort(pcx->bytes_per_line);
    if (scan < w) {
        return Q_ERR_INVALID_FORMAT;
    }

    //
    // get palette
    //
    if (palette) {
        if (rawlen < 768) {
            return Q_ERR_FILE_TOO_SMALL;
        }
        memcpy(palette, (byte *)pcx + rawlen - 768, 768);
    }

    //
    // get pixels
    //
    if (pixels) {
        raw = pcx->data;
        end = (byte *)pcx + rawlen;
        for (y = 0; y < h; y++, pixels += w) {
            for (x = 0; x < scan;) {
                if (raw >= end)
                    return Q_ERR_BAD_RLE_PACKET;
                dataByte = *raw++;

                if ((dataByte & 0xC0) == 0xC0) {
                    runLength = dataByte & 0x3F;
                    if (x + runLength > scan)
                        return Q_ERR_BAD_RLE_PACKET;
                    if (raw >= end)
                        return Q_ERR_BAD_RLE_PACKET;
                    dataByte = *raw++;
                } else {
                    runLength = 1;
                }

                while (runLength--) {
                    if (x < w)
                        pixels[x] = dataByte;
                    x++;
                }
            }
        }
    }

    if (width)
        *width = w;
    if (height)
        *height = h;

    return Q_ERR_SUCCESS;
}

/*
===============
IMG_Unpack8
===============
*/
static int IMG_Unpack8(uint32_t *out, const uint8_t *in, int width, int height)
{
    int         x, y, p;
    bool        has_alpha = false;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            p = *in;
            if (p == 255) {
                has_alpha = true;
                // transparent, so scan around for another color
                // to avoid alpha fringes
                if (y > 0 && *(in - width) != 255)
                    p = *(in - width);
                else if (y < height - 1 && *(in + width) != 255)
                    p = *(in + width);
                else if (x > 0 && *(in - 1) != 255)
                    p = *(in - 1);
                else if (x < width - 1 && *(in + 1) != 255)
                    p = *(in + 1);
                else if (y > 0 && x > 0 && *(in - width - 1) != 255)
                    p = *(in - width - 1);
                else if (y > 0 && x < width - 1 && *(in - width + 1) != 255)
                    p = *(in - width + 1);
                else if (y < height - 1 && x > 0 && *(in + width - 1) != 255)
                    p = *(in + width - 1);
                else if (y < height - 1 && x < width - 1 && *(in + width + 1) != 255)
                    p = *(in + width + 1);
                else
                    p = 0;
                // copy rgb components
                *out = d_8to24table[p] & U32_RGB;
            } else {
                *out = d_8to24table[p];
            }
            in++;
            out++;
        }
    }

    if (has_alpha)
        return IF_PALETTED | IF_TRANSPARENT;

    return IF_PALETTED | IF_OPAQUE;
}

IMG_LOAD(PCX)
{
    byte        buffer[640 * 480];
    int         w, h;
    int         ret;

    ret = IMG_DecodePCX(rawdata, rawlen, buffer, NULL, &w, &h);
    if (ret < 0)
        return ret;

    if (image->type == IT_SKIN)
        IMG_FloodFill(buffer, w, h);

    *pic = IMG_AllocPixels(w * h * 4);

    image->upload_width = image->width = w;
    image->upload_height = image->height = h;
    image->flags |= IMG_Unpack8((uint32_t *)*pic, buffer, w, h);

    return Q_ERR_SUCCESS;
}


/*
=================================================================

WAL LOADING

=================================================================
*/

IMG_LOAD(WAL)
{
    miptex_t    *mt;
    size_t      w, h, offset, size, endpos;

    if (rawlen < sizeof(miptex_t)) {
        return Q_ERR_FILE_TOO_SMALL;
    }

    mt = (miptex_t *)rawdata;

    w = LittleLong(mt->width);
    h = LittleLong(mt->height);
    if (w < 1 || h < 1 || w > 512 || h > 512) {
        return Q_ERR_INVALID_FORMAT;
    }

    size = w * h;

    offset = LittleLong(mt->offsets[0]);
    endpos = offset + size;
    if (endpos < offset || endpos > rawlen) {
        return Q_ERR_BAD_EXTENT;
    }

    *pic = IMG_AllocPixels(size * 4);

    image->upload_width = image->width = w;
    image->upload_height = image->height = h;
    image->flags |= IMG_Unpack8((uint32_t *)*pic, (uint8_t *)mt + offset, w, h);

    return Q_ERR_SUCCESS;
}

/*
=================================================================

STB_IMAGE LOADING

=================================================================
*/

static bool supports_extended_pixel_format(void)
{
	return cls.ref_type == REF_TYPE_VKPT;
}

IMG_LOAD(STB)
{
	int w, h, channels;
	byte* data = NULL;
	if(supports_extended_pixel_format())
	{
		int img_comp;
		stbi_info_from_memory(rawdata, rawlen, NULL, NULL, &img_comp);
		bool img_is_16 = stbi_is_16_bit_from_memory(rawdata, rawlen);

		if(img_comp == 1 && img_is_16)
		{
			// Special: 16bpc grayscale
			data = (byte*)stbi_load_16_from_memory(rawdata, rawlen, &w, &h, &channels, 1);
			image->pixel_format = PF_R16_UNORM;
		}
		// else: handle default case (8bpc RGBA) below
	}
	if(!data)
	{
		data = stbi_load_from_memory(rawdata, rawlen, &w, &h, &channels, 4);
		image->pixel_format = PF_R8G8B8A8_UNORM;
	}

	if (!data)
		return Q_ERR_LIBRARY_ERROR;

	*pic = data;

	image->upload_width = image->width = w;
	image->upload_height = image->height = h;

	if (channels == 3)
		image->flags |= IF_OPAQUE;

    return Q_ERR_SUCCESS;
}


/*
=================================================================

STB_IMAGE SAVING

=================================================================
*/

static int IMG_SaveTGA(screenshot_t *s)
{
	stbi_flip_vertically_on_write(1);
	int ret = stbi_write_tga_to_func(stbi_write, s, s->width, s->height, 3, s->pixels);

	if (ret) 
		return Q_ERR_SUCCESS;

	return Q_ERR_LIBRARY_ERROR;
}

static int IMG_SaveJPG(screenshot_t *s)
{
	stbi_flip_vertically_on_write(1);
	int ret = stbi_write_jpg_to_func(stbi_write, s, s->width, s->height, 3, s->pixels, s->param);

	if (ret)
		return Q_ERR_SUCCESS;

	return Q_ERR_LIBRARY_ERROR;
}


static int IMG_SavePNG(screenshot_t *s)
{
	stbi_flip_vertically_on_write(1);
	int ret = stbi_write_png_to_func(stbi_write, s, s->width, s->height, 3, s->pixels, s->row_stride);

	if (ret)
		return Q_ERR_SUCCESS;

	return Q_ERR_LIBRARY_ERROR;
}

static int IMG_SaveHDR(screenshot_t *s)
{
	stbi_flip_vertically_on_write(1);
	// NOTE: The 'pixels' point is byte*, but HDR writing needs float*!
	int ret = stbi_write_hdr_to_func(stbi_write, s, s->width, s->height, 3, (float*)s->pixels);

	if (ret)
		return Q_ERR_SUCCESS;

	return Q_ERR_LIBRARY_ERROR;
}

/*
=========================================================

SCREEN SHOTS

=========================================================
*/

static cvar_t *r_screenshot_format;
static cvar_t *r_screenshot_quality;
static cvar_t *r_screenshot_async;
static cvar_t* r_screenshot_compression;
static cvar_t* r_screenshot_message;
static cvar_t *r_screenshot_template;

static int suffix_pos(const char *s, int ch)
{
    int pos = strlen(s);
    while (pos > 0 && s[pos - 1] == ch)
        pos--;
    return pos;
}

static int parse_template(cvar_t *var, char *buffer, size_t size)
{
    if (FS_NormalizePathBuffer(buffer, var->string, size) < size) {
        FS_CleanupPath(buffer);
        int start = suffix_pos(buffer, 'X');
        int width = strlen(buffer) - start;
        buffer[start] = 0;
        if (width >= 3 && width <= 9)
            return width;
    }

    Com_WPrintf("Bad value '%s' for '%s'. Falling back to '%s'.\n",
                var->string, var->name, var->default_string);
    Cvar_Reset(var);
    Q_strlcpy(buffer, "quake", size);
    return 3;
}

static int create_screenshot(char *buffer, size_t size, FILE **f,
                             const char *name, const char *ext)
{
    char temp[MAX_OSPATH];
    int i, ret, width, count;

    if (name && *name) {
        // save to user supplied name
        if (FS_NormalizePathBuffer(temp, name, sizeof(temp)) >= sizeof(temp)) {
            return Q_ERR(ENAMETOOLONG);
        }
        FS_CleanupPath(temp);
        if (Q_snprintf(buffer, size, "%s/screenshots/%s%s", fs_gamedir, temp, ext) >= size) {
            return Q_ERR(ENAMETOOLONG);
        }
        if ((ret = FS_CreatePath(buffer)) < 0) {
            return ret;
        }
        if (!(*f = fopen(buffer, "wb"))) {
            return Q_ERRNO;
        }
        return 0;
    }

    width = parse_template(r_screenshot_template, temp, sizeof(temp));

    // create the directory
    if (Q_snprintf(buffer, size, "%s/screenshots/%s", fs_gamedir, temp) >= size) {
        return Q_ERR(ENAMETOOLONG);
    }
    if ((ret = FS_CreatePath(buffer)) < 0) {
        return ret;
    }

    count = 1;
    for (i = 0; i < width; i++)
        count *= 10;

    // find a file name to save it to
    for (i = 0; i < count; i++) {
        if (Q_snprintf(buffer, size, "%s/screenshots/%s%0*d%s", fs_gamedir, temp, width, i, ext) >= size) {
            return Q_ERR(ENAMETOOLONG);
        }
        if ((*f = Q_fopen(buffer, "wxb"))) {
            return 0;
        }
        ret = Q_ERRNO;
        if (ret != Q_ERR(EEXIST)) {
            return ret;
        }
    }
    
    return Q_ERR_OUT_OF_SLOTS;
}

static bool is_render_hdr()
{
    return R_IsHDR && R_IsHDR();
}

static void screenshot_work_cb(void *arg)
{
    screenshot_t *s = arg;
    s->status = s->save_cb(s);
}

static void screenshot_done_cb(void *arg)
{
    screenshot_t *s = arg;

    if (fclose(s->fp) && !s->status)
        s->status = Q_ERRNO;
    Z_Free(s->pixels);

    if (s->status < 0) {
        Com_EPrintf("Couldn't write %s: %s\n", s->filename, Q_ErrorString(s->status));
        remove(s->filename);
    } else if (r_screenshot_message->integer) {
        Com_Printf("Wrote %s\n", s->filename);
    }

    if (s->async) {
        Z_Free(s->filename);
        Z_Free(s);
    }
}

static void make_screenshot(const char *name, const char *ext,
                            int (*save_cb)(struct screenshot_s *),
                            bool async, int param)
{
    char        buffer[MAX_OSPATH];
    byte        *pixels;
    FILE        *fp;
    int         w, h, ret, row_stride;
    
    if(is_render_hdr()) {
        Com_WPrintf("Screenshot format not supported in HDR mode\n");
        return;
    }
    ret = create_screenshot(buffer, sizeof(buffer), &fp, name, ext);
    if (ret < 0) {
        Com_EPrintf("Couldn't create screenshot: %s\n", Q_ErrorString(ret));
        return;
    }

    if (r_screenshot_message->integer && async)
        Com_Printf("Taking async screenshot...\n");

    pixels = IMG_ReadPixels(&w, &h, &row_stride);

    screenshot_t s = {
        .save_cb = save_cb,
        .pixels = pixels,
        .fp = fp,
        .filename = async ? Z_CopyString(buffer) : buffer,
        .width = w,
        .height = h,
        .row_stride = row_stride,
        .status = -1,
        .param = param,
        .async = async,
    };

    if (async) {
        asyncwork_t work = {
            .work_cb = screenshot_work_cb,
            .done_cb = screenshot_done_cb,
            .cb_arg = Z_CopyStruct(&s),
        };
        Sys_QueueAsyncWork(&work);
    } else {
        screenshot_work_cb(&s);
        screenshot_done_cb(&s);
    }
}

static void make_screenshot_hdr(const char *name, bool async)
{
    char        buffer[MAX_OSPATH];
    float       *pixels;
    int         ret;
    FILE        *fp;
    int         w, h;

    if(!is_render_hdr()) {
        Com_WPrintf("Screenshot format supported in HDR mode only\n");
        return;
    }

    ret = create_screenshot(buffer, sizeof(buffer), &fp, name, ".hdr");
    if (ret < 0) {
        Com_EPrintf("Couldn't create HDR screenshot: %s\n", Q_ErrorString(ret));
        return;
    }

    pixels = IMG_ReadPixelsHDR(&w, &h);

    screenshot_t s = {
        .save_cb = IMG_SaveHDR,
        .pixels = (byte*)pixels,
        .fp = fp,
        .filename = async ? Z_CopyString(buffer) : buffer,
        .width = w,
        .height = h,
        .row_stride = 0,
        .status = -1,
        .param = 0,
        .async = async,
    };

    if (async) {
        asyncwork_t work = {
            .work_cb = screenshot_work_cb,
            .done_cb = screenshot_done_cb,
            .cb_arg = Z_CopyStruct(&s),
        };
        Sys_QueueAsyncWork(&work);
    } else {
        screenshot_work_cb(&s);
        screenshot_done_cb(&s);
    }
}

/*
==================
IMG_ScreenShot_f

Standard function to take a screenshot. Saves in default format unless user
overrides format with a second argument. Screenshot name can't be
specified. This function is always compiled in to give a meaningful warning
if no formats are available.
==================
*/
static void IMG_ScreenShot_f(void)
{
    const char *s;

    if (Cmd_Argc() > 2) {
        Com_Printf("Usage: %s [format]\n", Cmd_Argv(0));
        return;
    }

    if (Cmd_Argc() > 1) {
        s = Cmd_Argv(1);
    } else {
        if(is_render_hdr())
            s = "hdr";
        else
        s = r_screenshot_format->string;
    }

    if (*s == 'h') {
        make_screenshot_hdr(NULL, r_screenshot_async->integer > 0);
        return;
    }

    if (*s == 'j') {
        make_screenshot(NULL, ".jpg", IMG_SaveJPG,
                        r_screenshot_async->integer > 0,
                        r_screenshot_quality->integer);
        return;
    }

    if (*s == 'p') {
        make_screenshot(NULL, ".png", IMG_SavePNG,
                        r_screenshot_async->integer > 0,
                        r_screenshot_compression->integer);
        return;
    }

    make_screenshot(NULL, ".tga", IMG_SaveTGA, r_screenshot_async->integer > 0, 0);
}

/*
==================
IMG_ScreenShotXXX_f

Specialized function to take a screenshot in specified format. Screenshot name
can be also specified, as well as quality and compression options.
==================
*/

static void IMG_ScreenShotTGA_f(void)
{
    if (Cmd_Argc() > 2) {
        Com_Printf("Usage: %s [name]\n", Cmd_Argv(0));
        return;
    }

    make_screenshot(Cmd_Argv(1), ".tga", IMG_SaveTGA, r_screenshot_async->integer > 0, 0);
}

static void IMG_ScreenShotJPG_f(void)
{
    int quality;

    if (Cmd_Argc() > 3) {
        Com_Printf("Usage: %s [name] [quality]\n", Cmd_Argv(0));
        return;
    }

    if (Cmd_Argc() > 2) {
        quality = atoi(Cmd_Argv(2));
    } else {
        quality = r_screenshot_quality->integer;
    }

    make_screenshot(Cmd_Argv(1), ".jpg", IMG_SaveJPG,
                    r_screenshot_async->integer > 0, quality);
}

static void IMG_ScreenShotPNG_f(void)
{
    int compression;

    if (Cmd_Argc() > 3) {
        Com_Printf("Usage: %s [name] [compression]\n", Cmd_Argv(0));
        return;
    }

    if (Cmd_Argc() > 2) {
        compression = atoi(Cmd_Argv(2));
    } else {
        compression = r_screenshot_compression->integer;
    }

    make_screenshot(Cmd_Argv(1), ".png", IMG_SavePNG,
                    r_screenshot_async->integer > 0, compression);
}

static void IMG_ScreenShotHDR_f(void)
{
    if (Cmd_Argc() > 2) {
        Com_Printf("Usage: %s [name]\n", Cmd_Argv(0));
        return;
    }

    make_screenshot_hdr(Cmd_Argv(1), r_screenshot_async->integer > 0);
}

/*
=========================================================

IMAGE PROCESSING

=========================================================
*/

void IMG_ResampleTexture(const byte *in, int inwidth, int inheight,
                         byte *out, int outwidth, int outheight)
{
    int i, j;
    const byte  *inrow1, *inrow2;
    unsigned    frac, fracstep;
    unsigned    p1[MAX_TEXTURE_SIZE], p2[MAX_TEXTURE_SIZE];
    const byte  *pix1, *pix2, *pix3, *pix4;
    float       heightScale;

    if (outwidth > MAX_TEXTURE_SIZE) {
        Com_Error(ERR_FATAL, "%s: outwidth > %d", __func__, MAX_TEXTURE_SIZE);
    }

    fracstep = inwidth * 0x10000 / outwidth;

    frac = fracstep >> 2;
    for (i = 0; i < outwidth; i++) {
        p1[i] = 4 * (frac >> 16);
        frac += fracstep;
    }
    frac = 3 * (fracstep >> 2);
    for (i = 0; i < outwidth; i++) {
        p2[i] = 4 * (frac >> 16);
        frac += fracstep;
    }

    heightScale = (float)inheight / outheight;
    inwidth <<= 2;
    for (i = 0; i < outheight; i++) {
        inrow1 = in + inwidth * (int)((i + 0.25f) * heightScale);
        inrow2 = in + inwidth * (int)((i + 0.75f) * heightScale);
        for (j = 0; j < outwidth; j++) {
            pix1 = inrow1 + p1[j];
            pix2 = inrow1 + p2[j];
            pix3 = inrow2 + p1[j];
            pix4 = inrow2 + p2[j];
            out[0] = (pix1[0] + pix2[0] + pix3[0] + pix4[0]) >> 2;
            out[1] = (pix1[1] + pix2[1] + pix3[1] + pix4[1]) >> 2;
            out[2] = (pix1[2] + pix2[2] + pix3[2] + pix4[2]) >> 2;
            out[3] = (pix1[3] + pix2[3] + pix3[3] + pix4[3]) >> 2;
            out += 4;
        }
    }
}

void IMG_MipMap(byte *out, byte *in, int width, int height)
{
    int     i, j;

    width <<= 2;
    height >>= 1;
    for (i = 0; i < height; i++, in += width) {
        for (j = 0; j < width; j += 8, out += 4, in += 8) {
            out[0] = (in[0] + in[4] + in[width + 0] + in[width + 4]) >> 2;
            out[1] = (in[1] + in[5] + in[width + 1] + in[width + 5]) >> 2;
            out[2] = (in[2] + in[6] + in[width + 2] + in[width + 6]) >> 2;
            out[3] = (in[3] + in[7] + in[width + 3] + in[width + 7]) >> 2;
        }
    }
}

/*
=========================================================

IMAGE MANAGER

=========================================================
*/

#define RIMAGES_HASH    256

static list_t   r_imageHash[RIMAGES_HASH];

image_t     r_images[MAX_RIMAGES];
int         r_numImages;

uint32_t    d_8to24table[256];

static const struct {
    char    ext[4];
    int     (*load)(byte *, size_t, image_t *, byte **);
} img_loaders[IM_MAX] = {
    { "pcx", IMG_LoadPCX },
    { "wal", IMG_LoadWAL },
    { "tga", IMG_LoadSTB },
    { "jpg", IMG_LoadSTB },
    { "png", IMG_LoadSTB }
};

static imageformat_t    img_search[IM_MAX];
static int              img_total;

static cvar_t   *r_override_textures;
static cvar_t   *r_texture_formats;

/*
===============
IMG_List_f
===============
*/
static void IMG_List_f(void)
{
	int        i;
	image_t    *image;
	int        texels, count;

	if (Cmd_Argc() > 1) {

		// save to file
		char path[MAX_OSPATH];

		qhandle_t f = FS_EasyOpenFile(path, sizeof(path), FS_MODE_WRITE | FS_FLAG_TEXT, "", Cmd_Argv(1), ".csv");
		if (!f) {
			Com_EPrintf("Error opening '%s'\n", path);
			return;
		}

		for (i = 1, count = 0, image = r_images + 1; i < r_numImages; i++, image++) {

			if (!image->registration_sequence)
				continue;

			char fmt[MAX_QPATH];
			sprintf(fmt, "%%-%ds, %%-%ds, (%% 5d %% 5d), sRGB:%%d\n", MAX_QPATH, MAX_QPATH);

			FS_FPrintf(f, fmt, 
				image->name, 
				image->filepath, 
				image->width, 
				image->height,
				image->is_srgb);
		}
		FS_FCloseFile(f);

		Com_Printf("Saved '%s'\n", path);

	} else {

		// dump to console
		static const char types[8] = "PFMSWY??";

		Com_Printf("------------------\n");
		texels = count = 0;

		for (i = 1, image = r_images + 1; i < r_numImages; i++, image++) {
			if (!image->registration_sequence)
				continue;

			Com_Printf("%c%c%c%c %4i %4i %s: %s\n",
				types[image->type > IT_MAX ? IT_MAX : image->type],
				(image->flags & IF_TRANSPARENT) ? 'T' : ' ',
				(image->flags & IF_SCRAP) ? 'S' : ' ',
				(image->flags & IF_PERMANENT) ? '*' : ' ',
				image->upload_width,
				image->upload_height,
				(image->flags & IF_PALETTED) ? "PAL" : "RGB",
				image->name);

			texels += image->upload_width * image->upload_height;
			count++;
		}
		Com_Printf("Total images: %d (out of %d slots)\n", count, r_numImages);
		Com_Printf("Total texels: %d (not counting mipmaps)\n", texels);
	}
}

static image_t *alloc_image(void)
{
    int i;
    image_t *image;

    // find a free image_t slot
    for (i = 1, image = r_images + 1; i < r_numImages; i++, image++) {
        if (!image->registration_sequence)
            break;
    }

    if (i == r_numImages) {
        if (r_numImages == MAX_RIMAGES)
            return NULL;
        r_numImages++;
    }

    return image;
}

// finds the given image of the given type.
// case and extension insensitive.
static image_t *lookup_image(const char *name,
                             imagetype_t type, unsigned hash, size_t baselen)
{
    image_t *image;

    // look for it
    LIST_FOR_EACH(image_t, image, &r_imageHash[hash], entry) {
        if (image->type != type) {
            continue;
        }
        if (image->baselen != baselen) {
            continue;
        }
        if (!FS_pathcmpn(image->name, name, baselen)) {
            return image;
        }
    }

    return NULL;
}

#define TRY_IMAGE_SRC_GAME      1
#define TRY_IMAGE_SRC_BASE      0

static int _try_image_format(imageformat_t fmt, image_t *image, int try_src, byte **pic)
{
    byte        *data;
    int         len;
    int         ret;

    // load the file
    int fs_flags = 0;
    if (try_src > 0)
        fs_flags = try_src == TRY_IMAGE_SRC_GAME ? FS_PATH_GAME : FS_PATH_BASE;
    len = FS_LoadFileFlags(image->name, (void **)&data, fs_flags);
    if (!data) {
        return len;
    }

    // decompress the image
    ret = img_loaders[fmt].load(data, len, image, pic);

    FS_FreeFile(data);

    image->filepath[0] = 0;
    if (ret >= 0) {
        strcpy(image->filepath, image->name);
        // record last modified time (skips reload when invoking IMG_ReloadAll)
        image->last_modified = 0;
        FS_LastModified(image->filepath, &image->last_modified);
    }
    return ret < 0 ? ret : fmt;
}

static int try_image_format(imageformat_t fmt, image_t *image, int try_src, byte **pic)
{
    // replace the extension
    memcpy(image->name + image->baselen + 1, img_loaders[fmt].ext, 4);
    return _try_image_format(fmt, image, try_src, pic);
}


// tries to load the image with a different extension
static int try_other_formats(imageformat_t orig, image_t *image, int try_src, byte **pic)
{
    imageformat_t   fmt;
    int             ret;
    int             i;

    // search through all the 32-bit formats
    for (i = 0; i < img_total; i++) {
        fmt = img_search[i];
        if (fmt == orig) {
            continue;   // don't retry twice
        }

        ret = try_image_format(fmt, image, try_src, pic);
        if (ret != Q_ERR_NOENT) {
            return ret; // found something
        }
    }

    // fall back to 8-bit formats
    fmt = (image->type == IT_WALL) ? IM_WAL : IM_PCX;
    if (fmt == orig) {
        return Q_ERR_NOENT; // don't retry twice
    }

    return try_image_format(fmt, image, try_src, pic);
}

int IMG_GetDimensions(const char* name, int* width, int* height)
{
    assert(name);
    assert(width);
    assert(height);
    
    int w = 0;
    int h = 0;

    ssize_t len = strlen(name);
    if (len <= 4)
        return Q_ERR_NAMETOOSHORT;

    imageformat_t format;
    if (Q_stricmp(name + len - 4, ".wal") == 0)
        format = IM_WAL;
    else if (Q_stricmp(name + len - 4, ".pcx") == 0)
        format = IM_PCX;
    else
        return Q_ERR_INVALID_FORMAT;

    qhandle_t f;
    FS_FOpenFile(name, &f, FS_MODE_READ);
    if (!f)
        return Q_ERR_NOENT;

    if (format == IM_WAL)
    {
        miptex_t mt;
        len = FS_Read(&mt, sizeof(mt), f);
        if (len == sizeof(mt)) {
            w = LittleLong(mt.width);
            h = LittleLong(mt.height);
        }
    }
    else if (format == IM_PCX)
    {
        dpcx_t pcx;
        len = FS_Read(&pcx, sizeof(pcx), f);
        if (len == sizeof(pcx)) {
            w = LittleShort(pcx.xmax) + 1;
            h = LittleShort(pcx.ymax) + 1;
        }
    }

    FS_FCloseFile(f);

    if (w < 1 || h < 1 || w > 512 || h > 512) {
        return Q_ERR_INVALID_FORMAT;
    }

    *width = w;
    *height = h;

    return Q_ERR_SUCCESS;
}

static void get_image_dimensions(imageformat_t fmt, image_t *image)
{
    char buffer[MAX_QPATH];
    memcpy(buffer, image->name, image->baselen + 1);
    
    if (fmt == IM_WAL) {
        memcpy(buffer + image->baselen + 1, "wal", 4);
    } else {
        memcpy(buffer + image->baselen + 1, "pcx", 4);
    }

    IMG_GetDimensions(buffer, &image->width, &image->height);
}

static void r_texture_formats_changed(cvar_t *self)
{
    char *s;
    int i, j;

    // reset the search order
    img_total = 0;

    // parse the string
    for (s = self->string; *s; s++) {
        switch (*s) {
            case 't': case 'T': i = IM_TGA; break;
            case 'j': case 'J': i = IM_JPG; break;
            case 'p': case 'P': i = IM_PNG; break;
            default: continue;
        }

        // don't let format to be specified more than once
        for (j = 0; j < img_total; j++)
            if (img_search[j] == i)
                break;
        if (j != img_total)
            continue;

        img_search[img_total++] = i;
        if (img_total == IM_MAX) {
            break;
        }
    }
}

int
load_img(const char *name, image_t *image)
{
    byte            *pic;
    imageformat_t   fmt;
    int             ret;

	size_t len = strlen(name);

    // must have an extension and at least 1 char of base name
    if (len <= 4) {
        return Q_ERR_NAMETOOSHORT;
    }
    if (name[len - 4] != '.') {
        return Q_ERR_INVALID_PATH;
    }

    memcpy(image->name, name, len + 1);
    image->baselen = len - 4;
    image->type = 0;
    image->flags = 0;
    image->registration_sequence = 1;

    // find out original extension
    for (fmt = 0; fmt < IM_MAX; fmt++) {
        if (!Q_stricmp(image->name + image->baselen + 1, img_loaders[fmt].ext)) {
            break;
        }
    }

    // load the pic from disk
    pic = NULL;

    // Always prefer images from the game dir, even if format might be 'inferior'
    for (int try_location = Q_stricmp(fs_game->string, BASEGAME) ? TRY_IMAGE_SRC_GAME : TRY_IMAGE_SRC_BASE;
         try_location >= TRY_IMAGE_SRC_BASE;
         try_location--)
    {
        int location_flag = try_location == TRY_IMAGE_SRC_GAME ? IF_SRC_GAME : IF_SRC_MASK;
        if(((image->flags & IF_SRC_MASK) != 0) && ((image->flags & IF_SRC_MASK) != location_flag))
            continue;

        // first try with original extension
        ret = _try_image_format(fmt, image, try_location, &pic);
        if (ret == Q_ERR_NOENT) {
            // retry with remaining extensions
            ret = try_other_formats(fmt, image, try_location, &pic);
        }
        if (ret >= 0)
            break;
    }

    // if we are replacing 8-bit texture with a higher resolution 32-bit
    // texture, we need to recover original image dimensions
    if (fmt <= IM_WAL && ret > IM_WAL) {
        get_image_dimensions(fmt, image);
    }

    if (ret < 0) {
        memset(image, 0, sizeof(*image));
        return ret;
    }

#if USE_REF == REF_VKPT
	image->pix_data = pic;
#endif

    return Q_ERR_SUCCESS;
}

// Try to load an image, possibly with an alternative extension
static int try_load_image_candidate(image_t *image, const char *orig_name, size_t orig_len, byte **pic_p, imagetype_t type, imageflags_t flags, bool ignore_extension, int try_location)
{
    int ret;

    image->type = type;
    image->flags = flags;
    image->registration_sequence = registration_sequence;

    // find out original extension
    imageformat_t fmt;
    for (fmt = 0; fmt < IM_MAX; fmt++)
    {
        if (!Q_stricmp(image->name + image->baselen + 1, img_loaders[fmt].ext))
        {
            break;
        }
    }

    // load the pic from disk
    *pic_p = NULL;

    if (fmt == IM_MAX)
    {
        // unknown extension, but give it a chance to load anyway
        ret = try_other_formats(IM_MAX, image, try_location, pic_p);
        if (ret == Q_ERR_NOENT)
        {
            // not found, change error to invalid path
            ret = Q_ERR_INVALID_PATH;
        }
    }
    else if (ignore_extension)
    {
        // forcibly replace the extension
        ret = try_other_formats(IM_MAX, image, try_location, pic_p);
    }
    else
    {
        // first try with original extension
        ret = _try_image_format(fmt, image, try_location, pic_p);
        if (ret == Q_ERR_NOENT && !(flags & IF_EXACT))
        {
            // retry with remaining extensions
            ret = try_other_formats(fmt, image, try_location, pic_p);
        }
    }

    // record last modified time (skips reload when invoking IMG_ReloadAll)
    image->last_modified = 0;
    FS_LastModified(image->name, &image->last_modified);

    // Restore original name if it was overridden
    if(orig_name) {
        memcpy(image->name, orig_name, orig_len + 1);
        image->baselen = orig_len - 4;
    }

    // if we are replacing 8-bit texture with a higher resolution 32-bit
    // texture, we need to recover original image dimensions
    if (fmt <= IM_WAL && ret > IM_WAL)
    {
        get_image_dimensions(fmt, image);
    }

    return ret;
}

// finds or loads the given image, adding it to the hash table.
static int find_or_load_image(const char *name, size_t len,
                                   imagetype_t type, imageflags_t flags,
                                   image_t **image_p)
{
    image_t         *image;
    byte            *pic;
    unsigned        hash;
    int             ret = Q_ERR_NOENT;

    *image_p = NULL;

    // must have an extension and at least 1 char of base name
    if (len <= 4) {
        return Q_ERR_NAMETOOSHORT;
    }
    if (name[len - 4] != '.') {
        return Q_ERR_INVALID_PATH;
    }

    hash = FS_HashPathLen(name, len - 4, RIMAGES_HASH);

    // look for it
    if ((image = lookup_image(name, type, hash, len - 4)) != NULL) {
        image->flags |= flags & IF_PERMANENT;
        image->registration_sequence = registration_sequence;
        *image_p = image;
        return Q_ERR_SUCCESS;
    }

    // allocate image slot
    image = alloc_image();
    if (!image) {
        return Q_ERR_OUT_OF_SLOTS;
    }

	int override_textures = !!r_override_textures->integer;
	if (cls.ref_type == REF_TYPE_GL && (type != IT_PIC) && !gl_use_hd_assets->integer)
		override_textures = 0;
    if (flags & IF_EXACT)
        override_textures = 0;

    if(override_textures)
    {
        const char *last_slash = strrchr(name, '/');
        if (!last_slash)
            last_slash = name;
        else
            last_slash += 1;

        strcpy(image->name, "overrides/");
        strcat(image->name, last_slash);
        image->baselen = strlen(image->name) - 4;
        ret = try_load_image_candidate(image, name, len, &pic, type, flags, true, -1);
        memcpy(image->name, name, len + 1);
        image->baselen = len - 4;
    }

    // Try non-overridden image
    if (ret < 0)
    {
        bool is_not_baseq2 = fs_game->string[0] && strcmp(fs_game->string, BASEGAME) != 0;
    	
        // Always prefer images from the game dir, even if format might be 'inferior'
        for (int try_location = is_not_baseq2 ? TRY_IMAGE_SRC_GAME : TRY_IMAGE_SRC_BASE;
            try_location >= TRY_IMAGE_SRC_BASE;
            try_location--)
        {
            int location_flag = try_location == TRY_IMAGE_SRC_GAME ? IF_SRC_GAME : IF_SRC_BASE;
            if(((flags & IF_SRC_MASK) != 0) && ((flags & IF_SRC_MASK) != location_flag))
                continue;

            // fill in some basic info
            memcpy(image->name, name, len + 1);
            image->baselen = len - 4;
            ret = try_load_image_candidate(image, NULL, 0, &pic, type, flags, !!override_textures, try_location);
            image->flags |= location_flag;

            if (ret >= 0)
                break;
        }
    }

    if (ret < 0) {
        memset(image, 0, sizeof(*image));
        return ret;
    }

    List_Append(&r_imageHash[hash], &image->entry);

	image->is_srgb = !!(flags & IF_SRGB);

    // upload the image
    IMG_Load(image, pic);

    *image_p = image;
    return Q_ERR_SUCCESS;
}

image_t *IMG_Find(const char *name, imagetype_t type, imageflags_t flags)
{
    image_t *image;
    size_t len;
    int ret;

    if (!name) {
        Com_Error(ERR_FATAL, "%s: NULL", __func__);
    }

    // this should never happen
    len = strlen(name);
    if (len >= MAX_QPATH) {
        Com_Error(ERR_FATAL, "%s: oversize name", __func__);
    }

    ret = find_or_load_image(name, len, type, flags, &image);
    if (image) {
        return image;
    }

    // don't spam about missing images
    if (ret != Q_ERR_NOENT) {
        Com_EPrintf("Couldn't load %s: %s\n", name, Q_ErrorString(ret));
    }

    return R_NOTEXTURE;
}

image_t *IMG_FindExisting(const char *name, imagetype_t type)
{
    image_t *image;
    size_t len;
    unsigned hash;

    if (!name) {
        Com_Error(ERR_FATAL, "%s: NULL", __func__);
    }

    // this should never happen
    len = strlen(name);
    if (len >= MAX_QPATH) {
        Com_Error(ERR_FATAL, "%s: oversize name", __func__);
    }

    // must have an extension and at least 1 char of base name
    if (len <= 4) {
        return R_NOTEXTURE;
    }
    if (name[len - 4] != '.') {
        return R_NOTEXTURE;
    }

    hash = FS_HashPathLen(name, len - 4, RIMAGES_HASH);

    // look for it
    if ((image = lookup_image(name, type, hash, len - 4)) != NULL) {
        return image;
    }

    return R_NOTEXTURE;
}

/*
===============
IMG_Clone
===============
*/
image_t *IMG_Clone(image_t *image, const char* new_name)
{
    if(image == R_NOTEXTURE)
        return image;

    image_t* new_image = alloc_image();
    if (!new_image)
        return R_NOTEXTURE;

    memcpy(new_image, image, sizeof(image_t));

#if USE_REF == REF_VKPT
    size_t image_size = image->upload_width * image->upload_height * 4;
    if(image->pix_data != NULL)
    {
        new_image->pix_data = IMG_AllocPixels(image_size);
        memcpy(new_image->pix_data, image->pix_data, image_size);
    }
#else
    for (int m = 0; m < 4; m++)
    {
        if(image->pixels[m] != NULL)
        {
            size_t mip_size = (image->upload_width >> m) * (image->upload_height >> m) * 4;
            new_image->pixels[m] = IMG_AllocPixels(mip_size);
            memcpy(new_image->pixels[m], image->pixels[m], mip_size);
        }
    }
#endif

    if(new_name)
    {
        Q_strlcpy(new_image->name, new_name, sizeof(new_image->name));
        new_image->baselen = strlen(new_image->name) - 4;
        assert(new_image->name[new_image->baselen] == '.');
    }
    unsigned hash = FS_HashPathLen(new_image->name, new_image->baselen, RIMAGES_HASH);
    List_Append(&r_imageHash[hash], &new_image->entry);
    return new_image;
}

/*
===============
IMG_ForHandle
===============
*/
image_t *IMG_ForHandle(qhandle_t h)
{
    if (h < 0 || h >= r_numImages) {
        Com_Error(ERR_FATAL, "%s: %d out of range", __func__, h);
    }

    return &r_images[h];
}

/*
===============
R_RegisterImage
===============
*/
qhandle_t R_RegisterImage(const char *name, imagetype_t type,
                          imageflags_t flags, int *err_p)
{
    image_t     *image;
    char        fullname[MAX_QPATH];
    size_t      len;
    int         err;

    // empty names are legal, silently ignore them
    if (!*name) {
        if (err_p)
            *err_p = Q_ERR_NAMETOOSHORT;
        return 0;
    }

    // no images = not initialized
    if (!r_numImages) {
        if (err_p)
            *err_p = Q_ERR_AGAIN;
        return 0;
    }

    if (type == IT_SKIN) {
        len = FS_NormalizePathBuffer(fullname, name, sizeof(fullname));
    } else if (*name == '/' || *name == '\\') {
        len = FS_NormalizePathBuffer(fullname, name + 1, sizeof(fullname));
    } else {
        len = Q_concat(fullname, sizeof(fullname), "pics/", name);
        if (len < sizeof(fullname)) {
            FS_NormalizePath(fullname, fullname);
            len = COM_DefaultExtension(fullname, ".pcx", sizeof(fullname));
        }
    }

    if (len >= sizeof(fullname)) {
        err = Q_ERR_NAMETOOLONG;
        goto fail;
    }

    err = find_or_load_image(fullname, len, type, flags, &image);
    if (image) {
        if (err_p)
            *err_p = Q_ERR_SUCCESS;
        return image - r_images;
    }

fail:
    // don't spam about missing images
    if (err_p)
        *err_p = err;
    else if (err != Q_ERR_NOENT)
        Com_EPrintf("Couldn't load %s: %s\n", fullname, Q_ErrorString(err));

    return 0;
}

qhandle_t R_RegisterRawImage(const char *name, int width, int height, byte* pic, imagetype_t type, imageflags_t flags)
{
    image_t         *image;
    unsigned        hash;

    int len = strlen(name);
    hash = FS_HashPathLen(name, len, RIMAGES_HASH);

    // look for it
    if ((image = lookup_image(name, type, hash, len)) != NULL) {
        image->flags |= flags & IF_PERMANENT;
        image->registration_sequence = registration_sequence;
        return image - r_images;
    }

    // allocate image slot
    image = alloc_image();
    if (!image) {
        return 0;
    }

    memcpy(image->name, name, len + 1);
    image->baselen = len;
    image->type = type;
    image->flags = flags;
    image->registration_sequence = registration_sequence;
    image->last_modified = 0;
    image->width = width;
    image->height = height;
    image->upload_width = width;
    image->upload_height = height;

    List_Append(&r_imageHash[hash], &image->entry);

    image->is_srgb = !!(flags & IF_SRGB);

    // upload the image
    IMG_Load(image, pic);

    return image - r_images;
}

void R_UnregisterImage(qhandle_t handle)
{
    if (!handle)
        return;

    image_t* image = r_images + handle;

    if (image->registration_sequence)
    {
        image->registration_sequence = -1;
        IMG_FreeUnused();
    }
}

/*
=============
R_GetPicSize
=============
*/
bool R_GetPicSize(int *w, int *h, qhandle_t pic)
{
    image_t *image = IMG_ForHandle(pic);

    if (w) {
        *w = image->width;
    }
    if (h) {
        *h = image->height;
    }
    return image->flags & IF_TRANSPARENT;
}

/*
================
IMG_FreeUnused

Any image that was not touched on this registration sequence
will be freed.
================
*/
void IMG_FreeUnused(void)
{
    image_t *image;
    int i, count = 0;

    for (i = 1, image = r_images + 1; i < r_numImages; i++, image++) {
        if (image->registration_sequence == registration_sequence) {
            continue;        // used this sequence
        }
        if (!image->registration_sequence)
            continue;        // free image_t slot
        if (image->flags & (IF_PERMANENT | IF_SCRAP))
            continue;        // don't free pics

        // delete it from hash table
        List_Remove(&image->entry);

        // free it
        IMG_Unload(image);

        memset(image, 0, sizeof(*image));
        count++;
    }

    if (count) {
        Com_DPrintf("%s: %i images freed\n", __func__, count);
    }
}

void IMG_FreeAll(void)
{
    image_t *image;
    int i, count = 0;

    for (i = 1, image = r_images + 1; i < r_numImages; i++, image++) {
        if (!image->registration_sequence)
            continue;        // free image_t slot
        // free it
        IMG_Unload(image);

        memset(image, 0, sizeof(*image));
        count++;
    }

    if (count) {
        Com_DPrintf("%s: %i images freed\n", __func__, count);
    }

    for (i = 0; i < RIMAGES_HASH; i++) {
        List_Init(&r_imageHash[i]);
    }

    // &r_images[0] == R_NOTEXTURE
    r_numImages = 1;
}

/*
===============
R_GetPalette

===============
*/
void IMG_GetPalette(void)
{
    byte        pal[768], *src, *data;
    int         i, ret, len;

    // get the palette
    len = FS_LoadFile(R_COLORMAP_PCX, (void **)&data);
    if (!data) {
        ret = len;
        goto fail;
    }

    ret = IMG_DecodePCX(data, len, NULL, pal, NULL, NULL);

    FS_FreeFile(data);

    if (ret < 0) {
        goto fail;
    }

    for (i = 0, src = pal; i < 255; i++, src += 3) {
        d_8to24table[i] = MakeColor(src[0], src[1], src[2], 255);
    }

    // 255 is transparent
    d_8to24table[i] = MakeColor(src[0], src[1], src[2], 0);
    return;

fail:
    Com_Error(ERR_FATAL, "Couldn't load %s: %s", R_COLORMAP_PCX, Q_ErrorString(ret));
}

static const cmdreg_t img_cmd[] = {
    { "imagelist", IMG_List_f },
    { "screenshot", IMG_ScreenShot_f },
    { "screenshottga", IMG_ScreenShotTGA_f },
    { "screenshotjpg", IMG_ScreenShotJPG_f },
    { "screenshotpng", IMG_ScreenShotPNG_f },
    { NULL }
};

void IMG_Init(void)
{
    int i;

    if (r_numImages) {
        Com_Error(ERR_FATAL, "%s: %d images not freed", __func__, r_numImages);
    }


    r_override_textures = Cvar_Get("r_override_textures", "1", CVAR_FILES);
    r_texture_formats = Cvar_Get("r_texture_formats", "pjt", 0);
    r_texture_formats->changed = r_texture_formats_changed;
    r_texture_formats_changed(r_texture_formats);

    r_screenshot_format = Cvar_Get("gl_screenshot_format", "png", CVAR_ARCHIVE);
    r_screenshot_async = Cvar_Get("gl_screenshot_async", "1", 0);
    r_screenshot_quality = Cvar_Get("gl_screenshot_quality", "100", CVAR_ARCHIVE);
    r_screenshot_compression = Cvar_Get("gl_screenshot_compression", "6", CVAR_ARCHIVE);
    r_screenshot_message = Cvar_Get("gl_screenshot_message", "0", CVAR_ARCHIVE);
    r_screenshot_template = Cvar_Get("gl_screenshot_template", "quakeXXX", 0);

    Cmd_Register(img_cmd);

    for (i = 0; i < RIMAGES_HASH; i++) {
        List_Init(&r_imageHash[i]);
    }

    // &r_images[0] == R_NOTEXTURE
    r_numImages = 1;
}

void IMG_Shutdown(void)
{
    Cmd_Deregister(img_cmd);
    r_numImages = 0;
}
