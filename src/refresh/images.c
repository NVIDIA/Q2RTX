/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2003-2008 Andrey Nazarov

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
#include "refresh/images.h"
#include "format/pcx.h"
#include "format/wal.h"

#if USE_PNG
#define PNG_SKIP_SETJMP_CHECK
#include <png.h>
#endif // USE_PNG

#if USE_JPG
#include <jpeglib.h>
#endif

#define R_COLORMAP_PCX    "pics/colormap.pcx"

#define IMG_LOAD(x) \
    static qerror_t IMG_Load##x(byte *rawdata, size_t rawlen, \
        image_t *image, byte **pic)

#define IMG_SAVE(x) \
    static qerror_t IMG_Save##x(qhandle_t f, const char *filename, \
        byte *pic, int width, int height, int row_stride, int param)

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
static void IMG_FloodFill(byte *skin, int skinwidth, int skinheight)
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

static qerror_t _IMG_LoadPCX(byte *rawdata, size_t rawlen, byte *pixels,
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
    qboolean    has_alpha = qfalse;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            p = *in;
            if (p == 255) {
                has_alpha = qtrue;
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
    qerror_t    ret;

    ret = _IMG_LoadPCX(rawdata, rawlen, buffer, NULL, &w, &h);
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
=========================================================

TARGA IMAGES

=========================================================
*/

#if USE_TGA

#define TARGA_HEADER_SIZE  18

#define TGA_DECODE(x) \
    static qerror_t tga_decode_##x(byte *in, byte **row_pointers, int cols, int rows, byte *max_in)

typedef qerror_t (*tga_decode_t)(byte *, byte **, int, int, byte *);

TGA_DECODE(bgr)
{
    int col, row;
    byte *out_row;

    for (row = 0; row < rows; row++) {
        out_row = row_pointers[row];
        for (col = 0; col < cols; col++, out_row += 4, in += 3) {
            out_row[0] = in[2];
            out_row[1] = in[1];
            out_row[2] = in[0];
            out_row[3] = 255;
        }
    }

    return Q_ERR_SUCCESS;
}

TGA_DECODE(bgra)
{
    int col, row;
    byte *out_row;

    for (row = 0; row < rows; row++) {
        out_row = row_pointers[row];
        for (col = 0; col < cols; col++, out_row += 4, in += 4) {
            out_row[0] = in[2];
            out_row[1] = in[1];
            out_row[2] = in[0];
            out_row[3] = in[3];
        }
    }

    return Q_ERR_SUCCESS;
}

TGA_DECODE(bgr_rle)
{
    int col, row;
    byte *out_row;
    uint32_t color;
    int j, packet_header, packet_size;

    for (row = 0; row < rows; row++) {
        out_row = row_pointers[row];

        for (col = 0; col < cols;) {
            packet_header = *in++;
            packet_size = 1 + (packet_header & 0x7f);

            if (packet_header & 0x80) {
                // run-length packet
                if (in + 3 > max_in) {
                    return Q_ERR_BAD_RLE_PACKET;
                }
                color = MakeColor(in[2], in[1], in[0], 255);
                in += 3;
                for (j = 0; j < packet_size; j++) {
                    *(uint32_t *)out_row = color;
                    out_row += 4;

                    if (++col == cols) {
                        // run spans across rows
                        col = 0;
                        if (++row == rows)
                            goto break_out;
                        out_row = row_pointers[row];
                    }
                }
            } else {
                // non run-length packet
                if (in + 3 * packet_size > max_in) {
                    return Q_ERR_BAD_RLE_PACKET;
                }
                for (j = 0; j < packet_size; j++) {
                    out_row[0] = in[2];
                    out_row[1] = in[1];
                    out_row[2] = in[0];
                    out_row[3] = 255;
                    out_row += 4;
                    in += 3;

                    if (++col == cols) {
                        // run spans across rows
                        col = 0;
                        if (++row == rows)
                            goto break_out;
                        out_row = row_pointers[row];
                    }
                }
            }
        }
    }

break_out:
    return Q_ERR_SUCCESS;
}

TGA_DECODE(bgra_rle)
{
    int col, row;
    byte *out_row;
    uint32_t color;
    int j, packet_header, packet_size;

    for (row = 0; row < rows; row++) {
        out_row = row_pointers[row];

        for (col = 0; col < cols;) {
            packet_header = *in++;
            packet_size = 1 + (packet_header & 0x7f);

            if (packet_header & 0x80) {
                // run-length packet
                if (in + 4 > max_in) {
                    return Q_ERR_BAD_RLE_PACKET;
                }
                color = MakeColor(in[2], in[1], in[0], in[3]);
                in += 4;
                for (j = 0; j < packet_size; j++) {
                    *(uint32_t *)out_row = color;
                    out_row += 4;

                    if (++col == cols) {
                        // run spans across rows
                        col = 0;
                        if (++row == rows)
                            goto break_out;
                        out_row = row_pointers[row];
                    }
                }
            } else {
                // non run-length packet
                if (in + 4 * packet_size > max_in) {
                    return Q_ERR_BAD_RLE_PACKET;
                }
                for (j = 0; j < packet_size; j++) {
                    out_row[0] = in[2];
                    out_row[1] = in[1];
                    out_row[2] = in[0];
                    out_row[3] = in[3];
                    out_row += 4;
                    in += 4;

                    if (++col == cols) {
                        // run spans across rows
                        col = 0;
                        if (++row == rows)
                            goto break_out;
                        out_row = row_pointers[row];
                    }
                }
            }
        }
    }

break_out:
    return Q_ERR_SUCCESS;
}

IMG_LOAD(TGA)
{
    size_t offset;
    byte *pixels;
    byte *row_pointers[MAX_TEXTURE_SIZE];
    unsigned i, bpp, id_length, colormap_type, image_type, w, h, pixel_size, attributes;
    tga_decode_t decode;
    qerror_t ret;

    if (rawlen < TARGA_HEADER_SIZE) {
        return Q_ERR_FILE_TOO_SMALL;
    }

    id_length = rawdata[0];
    colormap_type = rawdata[1];
    image_type = rawdata[2];
    w = LittleShortMem(&rawdata[12]);
    h = LittleShortMem(&rawdata[14]);
    pixel_size = rawdata[16];
    attributes = rawdata[17];

    // skip TARGA image comment
    offset = TARGA_HEADER_SIZE + id_length;
    if (offset + 4 > rawlen) {
        return Q_ERR_BAD_EXTENT;
    }

    if (colormap_type) {
        Com_DPrintf("%s: %s: color mapped images are not supported\n", __func__, image->name);
        return Q_ERR_INVALID_FORMAT;
    }

    if (pixel_size == 32) {
        bpp = 4;
    } else if (pixel_size == 24) {
        bpp = 3;
    } else {
        Com_DPrintf("%s: %s: only 32 and 24 bit targa RGB images supported\n", __func__, image->name);
        return Q_ERR_INVALID_FORMAT;
    }

    if (w < 1 || h < 1 || w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE) {
        Com_DPrintf("%s: %s: invalid image dimensions\n", __func__, image->name);
        return Q_ERR_INVALID_FORMAT;
    }

    if (image_type == 2) {
        if (offset + w * h * bpp > rawlen) {
            return Q_ERR_BAD_EXTENT;
        }
        if (pixel_size == 32) {
            decode = tga_decode_bgra;
        } else {
            decode = tga_decode_bgr;
        }
    } else if (image_type == 10) {
        if (pixel_size == 32) {
            decode = tga_decode_bgra_rle;
        } else {
            decode = tga_decode_bgr_rle;
        }
    } else {
        Com_DPrintf("%s: %s: only type 2 and 10 targa RGB images supported\n", __func__, image->name);
        return Q_ERR_INVALID_FORMAT;
    }

    pixels = IMG_AllocPixels(w * h * 4);
    if (attributes & 32) {
        for (i = 0; i < h; i++) {
            row_pointers[i] = pixels + i * w * 4;
        }
    } else {
        for (i = 0; i < h; i++) {
            row_pointers[i] = pixels + (h - i - 1) * w * 4;
        }
    }

    ret = decode(rawdata + offset, row_pointers, w, h, rawdata + rawlen);
    if (ret < 0) {
        IMG_FreePixels(pixels);
        return ret;
    }

    *pic = pixels;

    image->upload_width = image->width = w;
    image->upload_height = image->height = h;

    if (pixel_size == 24)
        image->flags |= IF_OPAQUE;

    return Q_ERR_SUCCESS;
}

IMG_SAVE(TGA)
{
    byte header[TARGA_HEADER_SIZE], *p;
    ssize_t ret;
    int i, j;

    memset(&header, 0, sizeof(header));
    header[ 2] = 2;        // uncompressed type
    header[12] = width & 255;
    header[13] = width >> 8;
    header[14] = height & 255;
    header[15] = height >> 8;
    header[16] = 24;     // pixel size

    ret = FS_Write(&header, sizeof(header), f);
    if (ret < 0) {
        return ret;
    }

    // swap RGB to BGR
    for (i = 0; i < height; i++) {
        p = pic + i * row_stride;
        for (j = 0; j < width; j++) {
            byte tmp;

            tmp = p[2];
            p[2] = p[0];
            p[0] = tmp;

            p += 3;
        }
    }

    if (row_stride == width * 3) {
        ret = FS_Write(pic, width * height * 3, f);
        if (ret < 0) {
            return ret;
        }
    } else {
        for (i = 0; i < height; i++) {
            ret = FS_Write(pic + i * row_stride, width * 3, f);
            if (ret < 0) {
                return ret;
            }
        }
    }

    return Q_ERR_SUCCESS;
}

#endif // USE_TGA

/*
=========================================================

JPEG IMAGES

=========================================================
*/

#if USE_JPG

typedef struct my_error_mgr {
    struct jpeg_error_mgr   pub;
    jmp_buf                 setjmp_buffer;
    const char              *filename;
    qerror_t                error;
} *my_error_ptr;

METHODDEF(void) my_output_message(j_common_ptr cinfo)
{
    char buffer[JMSG_LENGTH_MAX];
    my_error_ptr jerr = (my_error_ptr)cinfo->err;

    (*cinfo->err->format_message)(cinfo, buffer);

    Com_EPrintf("libjpeg: %s: %s\n", jerr->filename, buffer);
}

METHODDEF(void) my_error_exit(j_common_ptr cinfo)
{
    my_error_ptr jerr = (my_error_ptr)cinfo->err;

    (*cinfo->err->output_message)(cinfo);

    jerr->error = Q_ERR_LIBRARY_ERROR;
    longjmp(jerr->setjmp_buffer, 1);
}

#if JPEG_LIB_VERSION < 80

METHODDEF(void) mem_init_source(j_decompress_ptr cinfo) { }

METHODDEF(boolean) mem_fill_input_buffer(j_decompress_ptr cinfo)
{
    my_error_ptr jerr = (my_error_ptr)cinfo->err;

    jerr->error = Q_ERR_FILE_TOO_SMALL;
    longjmp(jerr->setjmp_buffer, 1);
    return TRUE;
}

METHODDEF(void) mem_skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
    struct jpeg_source_mgr *src = cinfo->src;
    my_error_ptr jerr = (my_error_ptr)cinfo->err;

    if (num_bytes < 1) {
        return;
    }

    if (src->bytes_in_buffer < num_bytes) {
        jerr->error = Q_ERR_FILE_TOO_SMALL;
        longjmp(jerr->setjmp_buffer, 1);
    }

    src->next_input_byte += (size_t)num_bytes;
    src->bytes_in_buffer -= (size_t)num_bytes;
}

METHODDEF(void) mem_term_source(j_decompress_ptr cinfo) { }

METHODDEF(void) my_mem_src(j_decompress_ptr cinfo, byte *data, size_t size)
{
    cinfo->src = (struct jpeg_source_mgr *)(*cinfo->mem->alloc_small)(
                     (j_common_ptr)cinfo, JPOOL_PERMANENT, sizeof(struct jpeg_source_mgr));

    cinfo->src->init_source = mem_init_source;
    cinfo->src->fill_input_buffer = mem_fill_input_buffer;
    cinfo->src->skip_input_data = mem_skip_input_data;
    cinfo->src->resync_to_restart = jpeg_resync_to_restart;
    cinfo->src->term_source = mem_term_source;
    cinfo->src->bytes_in_buffer = size;
    cinfo->src->next_input_byte = data;
}

#define jpeg_mem_src my_mem_src

#endif

IMG_LOAD(JPG)
{
    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;
    JSAMPROW row_pointer;
    byte buffer[MAX_TEXTURE_SIZE * 3];
    byte *pixels;
    byte *in, *out;
    int i;
    qerror_t ret;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    jerr.pub.output_message = my_output_message;
    jerr.filename = image->name;
    jerr.error = Q_ERR_FAILURE;

    if (setjmp(jerr.setjmp_buffer)) {
        ret = jerr.error;
        goto fail;
    }

    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, rawdata, rawlen);
    jpeg_read_header(&cinfo, TRUE);

    if (cinfo.out_color_space != JCS_RGB && cinfo.out_color_space != JCS_GRAYSCALE) {
        Com_DPrintf("%s: %s: invalid image color space\n", __func__, image->name);
        ret = Q_ERR_INVALID_FORMAT;
        goto fail;
    }

    jpeg_start_decompress(&cinfo);

    if (cinfo.output_components != 3 && cinfo.output_components != 1) {
        Com_DPrintf("%s: %s: invalid number of color components\n", __func__, image->name);
        ret = Q_ERR_INVALID_FORMAT;
        goto fail;
    }

    if (cinfo.output_width > MAX_TEXTURE_SIZE || cinfo.output_height > MAX_TEXTURE_SIZE) {
        Com_DPrintf("%s: %s: invalid image dimensions\n", __func__, image->name);
        ret = Q_ERR_INVALID_FORMAT;
        goto fail;
    }

    pixels = out = IMG_AllocPixels(cinfo.output_height * cinfo.output_width * 4);
    row_pointer = (JSAMPROW)buffer;

    if (setjmp(jerr.setjmp_buffer)) {
        IMG_FreePixels(pixels);
        ret = jerr.error;
        goto fail;
    }

    if (cinfo.output_components == 3) {
        while (cinfo.output_scanline < cinfo.output_height) {
            jpeg_read_scanlines(&cinfo, &row_pointer, 1);

            in = buffer;
            for (i = 0; i < cinfo.output_width; i++, out += 4, in += 3) {
                out[0] = in[0];
                out[1] = in[1];
                out[2] = in[2];
                out[3] = 255;
            }
        }
    } else {
        while (cinfo.output_scanline < cinfo.output_height) {
            jpeg_read_scanlines(&cinfo, &row_pointer, 1);

            in = buffer;
            for (i = 0; i < cinfo.output_width; i++, out += 4, in += 1) {
                out[0] = out[1] = out[2] = in[0];
                out[3] = 255;
            }
        }
    }

    image->upload_width = image->width = cinfo.output_width;
    image->upload_height = image->height = cinfo.output_height;
    image->flags |= IF_OPAQUE;

    jpeg_finish_decompress(&cinfo);

    *pic = pixels;
    ret = Q_ERR_SUCCESS;

fail:
    jpeg_destroy_decompress(&cinfo);
    return ret;
}

#define OUTPUT_BUF_SIZE         0x10000 // 64 KiB

typedef struct my_destination_mgr {
    struct jpeg_destination_mgr pub;

    qhandle_t f;
    JOCTET *buffer;
} *my_dest_ptr;

METHODDEF(void) vfs_init_destination(j_compress_ptr cinfo)
{
    my_dest_ptr dest = (my_dest_ptr)cinfo->dest;

    // Allocate the output buffer --- it will be released when done with image
    dest->buffer = (JOCTET *)(*cinfo->mem->alloc_small)
                   ((j_common_ptr)cinfo, JPOOL_IMAGE, OUTPUT_BUF_SIZE * sizeof(JOCTET));

    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = OUTPUT_BUF_SIZE;
}

METHODDEF(boolean) vfs_empty_output_buffer(j_compress_ptr cinfo)
{
    my_dest_ptr dest = (my_dest_ptr)cinfo->dest;
    my_error_ptr jerr = (my_error_ptr)cinfo->err;
    ssize_t ret;

    ret = FS_Write(dest->buffer, OUTPUT_BUF_SIZE, dest->f);
    if (ret != OUTPUT_BUF_SIZE) {
        jerr->error = ret < 0 ? ret : Q_ERR_FAILURE;
        longjmp(jerr->setjmp_buffer, 1);
    }

    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = OUTPUT_BUF_SIZE;

    return TRUE;
}

METHODDEF(void) vfs_term_destination(j_compress_ptr cinfo)
{
    my_dest_ptr dest = (my_dest_ptr)cinfo->dest;
    my_error_ptr jerr = (my_error_ptr)cinfo->err;
    size_t remaining = OUTPUT_BUF_SIZE - dest->pub.free_in_buffer;
    ssize_t ret;

    // Write any data remaining in the buffer
    if (remaining > 0) {
        ret = FS_Write(dest->buffer, remaining, dest->f);
        if (ret != remaining) {
            jerr->error = ret < 0 ? ret : Q_ERR_FAILURE;
            longjmp(jerr->setjmp_buffer, 1);
        }
    }
}

METHODDEF(void) my_vfs_dst(j_compress_ptr cinfo, qhandle_t f)
{
    my_dest_ptr dest;

    dest = (my_dest_ptr)(*cinfo->mem->alloc_small)
           ((j_common_ptr)cinfo, JPOOL_PERMANENT, sizeof(struct my_destination_mgr));
    cinfo->dest = &dest->pub;

    dest->pub.init_destination = vfs_init_destination;
    dest->pub.empty_output_buffer = vfs_empty_output_buffer;
    dest->pub.term_destination = vfs_term_destination;
    dest->f = f;
}

IMG_SAVE(JPG)
{
    struct jpeg_compress_struct cinfo;
    struct my_error_mgr jerr;
    JSAMPARRAY row_pointers;
    qerror_t ret;
    int i;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    jerr.filename = filename;
    jerr.error = Q_ERR_FAILURE;

    if (setjmp(jerr.setjmp_buffer)) {
        ret = jerr.error;
        goto fail1;
    }

    jpeg_create_compress(&cinfo);

    my_vfs_dst(&cinfo, f);

    cinfo.image_width = width;      // image width and height, in pixels
    cinfo.image_height = height;
    cinfo.input_components = 3;     // # of color components per pixel
    cinfo.in_color_space = JCS_RGB; // colorspace of input image

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, clamp(param, 0, 100), TRUE);

    jpeg_start_compress(&cinfo, TRUE);

    row_pointers = FS_AllocTempMem(sizeof(JSAMPROW) * height);

    for (i = 0; i < height; i++) {
        row_pointers[i] = (JSAMPROW)(pic + (height - i - 1) * row_stride);
    }

    if (setjmp(jerr.setjmp_buffer)) {
        ret = jerr.error;
        goto fail2;
    }

    jpeg_write_scanlines(&cinfo, row_pointers, height);

    jpeg_finish_compress(&cinfo);

    ret = Q_ERR_SUCCESS;

fail2:
    FS_FreeTempMem(row_pointers);
fail1:
    jpeg_destroy_compress(&cinfo);
    return ret;
}

#endif // USE_JPG


#if USE_PNG

/*
=========================================================

PNG IMAGES

=========================================================
*/

typedef struct {
    png_bytep next_in;
    png_size_t avail_in;
} my_png_io;

typedef struct {
    png_const_charp filename;
    qerror_t error;
} my_png_error;

static void my_png_read_fn(png_structp png_ptr, png_bytep buf, png_size_t size)
{
    my_png_io *io = png_get_io_ptr(png_ptr);

    if (size > io->avail_in) {
        my_png_error *err = png_get_error_ptr(png_ptr);
        err->error = Q_ERR_FILE_TOO_SMALL;
        png_error(png_ptr, "read error");
    } else {
        memcpy(buf, io->next_in, size);
        io->next_in += size;
        io->avail_in -= size;
    }
}

static void my_png_error_fn(png_structp png_ptr, png_const_charp error_msg)
{
    my_png_error *err = png_get_error_ptr(png_ptr);

    if (err->error == Q_ERR_LIBRARY_ERROR) {
        Com_EPrintf("libpng: %s: %s\n", err->filename, error_msg);
    }
    longjmp(png_jmpbuf(png_ptr), -1);
}

static void my_png_warning_fn(png_structp png_ptr, png_const_charp warning_msg)
{
    my_png_error *err = png_get_error_ptr(png_ptr);

    Com_WPrintf("libpng: %s: %s\n", err->filename, warning_msg);
}

IMG_LOAD(PNG)
{
    byte *pixels;
    png_bytep row_pointers[MAX_TEXTURE_SIZE];
    png_uint_32 w, h, rowbytes, row, has_tRNS;
    int bitdepth, colortype;
    png_structp png_ptr;
    png_infop info_ptr;
    my_png_io my_io;
    my_png_error my_err;
    qerror_t ret;

    my_err.filename = image->name;
    my_err.error = Q_ERR_LIBRARY_ERROR;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                     (png_voidp)&my_err, my_png_error_fn, my_png_warning_fn);
    if (!png_ptr) {
        return Q_ERR_LIBRARY_ERROR;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        ret = Q_ERR_LIBRARY_ERROR;
        goto fail;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        ret = my_err.error;
        goto fail;
    }

    my_io.next_in = rawdata;
    my_io.avail_in = rawlen;
    png_set_read_fn(png_ptr, (png_voidp)&my_io, my_png_read_fn);

    png_read_info(png_ptr, info_ptr);

    if (!png_get_IHDR(png_ptr, info_ptr, &w, &h, &bitdepth, &colortype, NULL, NULL, NULL)) {
        ret = Q_ERR_LIBRARY_ERROR;
        goto fail;
    }

    if (w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE) {
        Com_DPrintf("%s: %s: invalid image dimensions\n", __func__, image->name);
        ret = Q_ERR_INVALID_FORMAT;
        goto fail;
    }

    switch (colortype) {
    case PNG_COLOR_TYPE_PALETTE:
        png_set_palette_to_rgb(png_ptr);
        break;
    case PNG_COLOR_TYPE_GRAY:
        if (bitdepth < 8) {
            png_set_expand_gray_1_2_4_to_8(png_ptr);
        }
        // fall through
    case PNG_COLOR_TYPE_GRAY_ALPHA:
        png_set_gray_to_rgb(png_ptr);
        break;
    }

    if (bitdepth < 8) {
        png_set_packing(png_ptr);
    } else if (bitdepth == 16) {
        png_set_strip_16(png_ptr);
    }

    has_tRNS = png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS);
    if (has_tRNS) {
        png_set_tRNS_to_alpha(png_ptr);
    }

    png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);

    png_set_interlace_handling(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    pixels = IMG_AllocPixels(h * rowbytes);

    for (row = 0; row < h; row++) {
        row_pointers[row] = pixels + row * rowbytes;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        IMG_FreePixels(pixels);
        ret = my_err.error;
        goto fail;
    }

    png_read_image(png_ptr, row_pointers);

    png_read_end(png_ptr, info_ptr);

    *pic = pixels;

    image->upload_width = image->width = w;
    image->upload_height = image->height = h;

    if (colortype == PNG_COLOR_TYPE_PALETTE)
        image->flags |= IF_PALETTED;

    if (has_tRNS == 0 && (colortype & PNG_COLOR_MASK_ALPHA) == 0)
        image->flags |= IF_OPAQUE;

    ret = Q_ERR_SUCCESS;

fail:
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return ret;
}

static void my_png_write_fn(png_structp png_ptr, png_bytep buf, png_size_t size)
{
    qhandle_t *f = png_get_io_ptr(png_ptr);
    ssize_t ret = FS_Write(buf, size, *f);

    if (ret != size) {
        my_png_error *err = png_get_error_ptr(png_ptr);
        err->error = ret < 0 ? ret : Q_ERR_FAILURE;
        png_error(png_ptr, "write error");
    }
}

static void my_png_flush_fn(png_structp png_ptr)
{
}

IMG_SAVE(PNG)
{
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytepp row_pointers;
    int i;
    my_png_error my_err;
    qerror_t ret;

    my_err.filename = filename;
    my_err.error = Q_ERR_LIBRARY_ERROR;

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                      (png_voidp)&my_err, my_png_error_fn, my_png_warning_fn);
    if (!png_ptr) {
        return Q_ERR_LIBRARY_ERROR;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        ret = Q_ERR_LIBRARY_ERROR;
        goto fail1;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        ret = my_err.error;
        goto fail1;
    }

    png_set_write_fn(png_ptr, (png_voidp)&f,
                     my_png_write_fn, my_png_flush_fn);

    png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_set_compression_level(png_ptr, clamp(param, 0, 9));

    row_pointers = FS_AllocTempMem(sizeof(png_bytep) * height);

    for (i = 0; i < height; i++) {
        row_pointers[i] = (png_bytep)pic + (height - i - 1) * row_stride;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        ret = my_err.error;
        goto fail2;
    }

    png_set_rows(png_ptr, info_ptr, row_pointers);

    png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL);

    ret = Q_ERR_SUCCESS;

fail2:
    FS_FreeTempMem(row_pointers);
fail1:
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return ret;
}

#endif // USE_PNG

/*
=========================================================

SCREEN SHOTS

=========================================================
*/

#if USE_JPG || USE_PNG
static cvar_t *r_screenshot_format;
#endif
#if USE_JPG
static cvar_t *r_screenshot_quality;
#endif
#if USE_PNG
static cvar_t *r_screenshot_compression;
#endif

#if USE_TGA || USE_JPG || USE_PNG
static qhandle_t create_screenshot(char *buffer, size_t size,
                                   const char *name, const char *ext)
{
    qhandle_t f;
    qerror_t ret;
    int i;

    if (name && *name) {
        // save to user supplied name
        return FS_EasyOpenFile(buffer, size, FS_MODE_WRITE,
                               "screenshots/", name, ext);
    }

    // find a file name to save it to
    for (i = 0; i < 1000; i++) {
        Q_snprintf(buffer, size, "screenshots/quake%03d%s", i, ext);
        ret = FS_FOpenFile(buffer, &f, FS_MODE_WRITE | FS_FLAG_EXCL);
        if (f) {
            return f;
        }
        if (ret != Q_ERR_EXIST) {
            Com_EPrintf("Couldn't exclusively open %s for writing: %s\n",
                        buffer, Q_ErrorString(ret));
            return 0;
        }
    }

    Com_EPrintf("All screenshot slots are full.\n");
    return 0;
}

static void make_screenshot(const char *name, const char *ext,
                            qerror_t (*save)(qhandle_t, const char *, byte *, int, int, int, int),
                            int param)
{
    char        buffer[MAX_OSPATH];
    byte        *pixels;
    qerror_t    ret;
    qhandle_t   f;
    int         w, h, rowbytes;

    f = create_screenshot(buffer, sizeof(buffer), name, ext);
    if (!f) {
        return;
    }

    pixels = IMG_ReadPixels(&w, &h, &rowbytes);
    ret = save(f, buffer, pixels, w, h, rowbytes, param);
    FS_FreeTempMem(pixels);

    FS_FCloseFile(f);

    if (ret < 0) {
        Com_EPrintf("Couldn't write %s: %s\n", buffer, Q_ErrorString(ret));
    } else {
        Com_Printf("Wrote %s\n", buffer);
    }
}
#endif // USE_TGA || USE_JPG || USE_PNG

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
#if USE_JPG || USE_PNG
    const char *s;

    if (Cmd_Argc() > 2) {
        Com_Printf("Usage: %s [format]\n", Cmd_Argv(0));
        return;
    }

    if (Cmd_Argc() > 1) {
        s = Cmd_Argv(1);
    } else {
        s = r_screenshot_format->string;
    }

#if USE_JPG
    if (*s == 'j') {
        make_screenshot(NULL, ".jpg", IMG_SaveJPG,
                        r_screenshot_quality->integer);
        return;
    }
#endif

#if USE_PNG
    if (*s == 'p') {
        make_screenshot(NULL, ".png", IMG_SavePNG,
                        r_screenshot_compression->integer);
        return;
    }
#endif
#endif // USE_JPG || USE_PNG

#if USE_TGA
    make_screenshot(NULL, ".tga", IMG_SaveTGA, 0);
#else
    Com_Printf("Can't take screenshot, TGA format not available.\n");
#endif
}

/*
==================
IMG_ScreenShotXXX_f

Specialized function to take a screenshot in specified format. Screenshot name
can be also specified, as well as quality and compression options.
==================
*/

#if USE_TGA
static void IMG_ScreenShotTGA_f(void)
{
    if (Cmd_Argc() > 2) {
        Com_Printf("Usage: %s [name]\n", Cmd_Argv(0));
        return;
    }

    make_screenshot(Cmd_Argv(1), ".tga", IMG_SaveTGA, 0);
}
#endif

#if USE_JPG
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

    make_screenshot(Cmd_Argv(1), ".jpg", IMG_SaveJPG, quality);
}
#endif

#if USE_PNG
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

    make_screenshot(Cmd_Argv(1), ".png", IMG_SavePNG, compression);
}
#endif

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
    char        ext[4];
    qerror_t    (*load)(byte *, size_t, image_t *, byte **);
} img_loaders[IM_MAX] = {
    { "pcx", IMG_LoadPCX },
    { "wal", IMG_LoadWAL },
#if USE_TGA
    { "tga", IMG_LoadTGA },
#endif
#if USE_JPG
    { "jpg", IMG_LoadJPG },
#endif
#if USE_PNG
    { "png", IMG_LoadPNG }
#endif
};

#if USE_PNG || USE_JPG || USE_TGA
static imageformat_t    img_search[IM_MAX];
static int              img_total;

static cvar_t   *r_override_textures;
static cvar_t   *r_texture_formats;
#endif

/*
===============
IMG_List_f
===============
*/
static void IMG_List_f(void)
{
    static const char types[8] = "PFMSWY??";
    int        i;
    image_t    *image;
    int        texels, count;

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

static int _try_image_format(imageformat_t fmt, image_t *image, byte **pic)
{
    byte        *data;
    ssize_t     len;
    qerror_t    ret;

    // load the file
    len = FS_LoadFile(image->name, (void **)&data);
    if (!data) {
        return len;
    }

    // decompress the image
    ret = img_loaders[fmt].load(data, len, image, pic);

    FS_FreeFile(data);

    return ret < 0 ? ret : fmt;
}

static int try_image_format(imageformat_t fmt, image_t *image, byte **pic)
{
    // replace the extension
    memcpy(image->name + image->baselen + 1, img_loaders[fmt].ext, 4);
    return _try_image_format(fmt, image, pic);
}


#if USE_PNG || USE_JPG || USE_TGA

// tries to load the image with a different extension
static int try_other_formats(imageformat_t orig, image_t *image, byte **pic)
{
    imageformat_t   fmt;
    qerror_t        ret;
    int             i;

    // search through all the 32-bit formats
    for (i = 0; i < img_total; i++) {
        fmt = img_search[i];
        if (fmt == orig) {
            continue;   // don't retry twice
        }

        ret = try_image_format(fmt, image, pic);
        if (ret != Q_ERR_NOENT) {
            return ret; // found something
        }
    }

    // fall back to 8-bit formats
    fmt = (image->type == IT_WALL) ? IM_WAL : IM_PCX;
    if (fmt == orig) {
        return Q_ERR_NOENT; // don't retry twice
    }

    return try_image_format(fmt, image, pic);
}

static void get_image_dimensions(imageformat_t fmt, image_t *image)
{
    char        buffer[MAX_QPATH];
    ssize_t     len;
    miptex_t    mt;
    dpcx_t      pcx;
    qhandle_t   f;
    unsigned    w, h;

    memcpy(buffer, image->name, image->baselen + 1);

    w = h = 0;
    if (fmt == IM_WAL) {
        memcpy(buffer + image->baselen + 1, "wal", 4);
        FS_FOpenFile(buffer, &f, FS_MODE_READ);
        if (f) {
            len = FS_Read(&mt, sizeof(mt), f);
            if (len == sizeof(mt)) {
                w = LittleLong(mt.width);
                h = LittleLong(mt.height);
            }
            FS_FCloseFile(f);
        }
    } else {
        memcpy(buffer + image->baselen + 1, "pcx", 4);
        FS_FOpenFile(buffer, &f, FS_MODE_READ);
        if (f) {
            len = FS_Read(&pcx, sizeof(pcx), f);
            if (len == sizeof(pcx)) {
                w = LittleShort(pcx.xmax) + 1;
                h = LittleShort(pcx.ymax) + 1;
            }
            FS_FCloseFile(f);
        }
    }

    if (w < 1 || h < 1 || w > 512 || h > 512) {
        return;
    }

    image->width = w;
    image->height = h;
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
#if USE_TGA
            case 't': case 'T': i = IM_TGA; break;
#endif
#if USE_JPG
            case 'j': case 'J': i = IM_JPG; break;
#endif
#if USE_PNG
            case 'p': case 'P': i = IM_PNG; break;
#endif
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

#endif // USE_PNG || USE_JPG || USE_TGA

qerror_t
load_img(const char *name, image_t *image)
{
    byte            *pic;
    imageformat_t   fmt;
    qerror_t        ret;

	size_t len = strlen(name);

    // must have an extension and at least 1 char of base name
    if (len <= 4) {
        return Q_ERR_NAMETOOSHORT;
    }
    if (name[len - 4] != '.') {
        return Q_ERR_INVALID_PATH;
    }

    // fill in some basic info
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

#if USE_PNG || USE_JPG || USE_TGA
	// first try with original extension
	ret = _try_image_format(fmt, image, &pic);
	if (ret == Q_ERR_NOENT) {
		// retry with remaining extensions
		ret = try_other_formats(fmt, image, &pic);
    }

    // if we are replacing 8-bit texture with a higher resolution 32-bit
    // texture, we need to recover original image dimensions
    if (fmt <= IM_WAL && ret > IM_WAL) {
        get_image_dimensions(fmt, image);
    }
#else
    if (fmt == IM_MAX) {
        ret = Q_ERR_INVALID_PATH;
    } else {
        ret = _try_image_format(fmt, image, &pic);
    }
#endif

    if (ret < 0) {
        memset(image, 0, sizeof(*image));
        return ret;
    }

	image->pix_data = pic;

    return Q_ERR_SUCCESS;
}

// finds or loads the given image, adding it to the hash table.
static qerror_t find_or_load_image(const char *name, size_t len,
                                   imagetype_t type, imageflags_t flags,
                                   image_t **image_p)
{
    image_t         *image;
    byte            *pic;
    unsigned        hash;
    imageformat_t   fmt;
    qerror_t        ret;

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

    // fill in some basic info
    memcpy(image->name, name, len + 1);
    image->baselen = len - 4;
    image->type = type;
    image->flags = flags;
    image->registration_sequence = registration_sequence;

    // find out original extension
    for (fmt = 0; fmt < IM_MAX; fmt++) {
        if (!Q_stricmp(image->name + image->baselen + 1, img_loaders[fmt].ext)) {
            break;
        }
    }

    // load the pic from disk
    pic = NULL;

#if USE_PNG || USE_JPG || USE_TGA
    if (fmt == IM_MAX) {
        // unknown extension, but give it a chance to load anyway
        ret = try_other_formats(IM_MAX, image, &pic);
        if (ret == Q_ERR_NOENT) {
            // not found, change error to invalid path
            ret = Q_ERR_INVALID_PATH;
        }
    } else if (r_override_textures->integer) {
        // forcibly replace the extension
        ret = try_other_formats(IM_MAX, image, &pic);
    } else {
        // first try with original extension
        ret = _try_image_format(fmt, image, &pic);
        if (ret == Q_ERR_NOENT) {
            // retry with remaining extensions
            ret = try_other_formats(fmt, image, &pic);
        }
    }

    // if we are replacing 8-bit texture with a higher resolution 32-bit
    // texture, we need to recover original image dimensions
    if (fmt <= IM_WAL && ret > IM_WAL) {
        get_image_dimensions(fmt, image);
    }
#else
    if (fmt == IM_MAX) {
        ret = Q_ERR_INVALID_PATH;
    } else {
        ret = _try_image_format(fmt, image, &pic);
    }
#endif

    if (ret < 0) {
        memset(image, 0, sizeof(*image));
        return ret;
    }

    List_Append(&r_imageHash[hash], &image->entry);

    // upload the image
    IMG_Load(image, pic);

    *image_p = image;
    return Q_ERR_SUCCESS;
}

image_t *IMG_Find(const char *name, imagetype_t type, imageflags_t flags)
{
    image_t *image;
    size_t len;
    qerror_t ret;

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
                          imageflags_t flags, qerror_t *err_p)
{
    image_t     *image;
    char        fullname[MAX_QPATH];
    size_t      len;
    qerror_t    err;

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
        len = Q_concat(fullname, sizeof(fullname), "pics/", name, NULL);
        if (len >= sizeof(fullname)) {
            err = Q_ERR_NAMETOOLONG;
            goto fail;
        }
        FS_NormalizePath(fullname, fullname);
        len = COM_DefaultExtension(fullname, ".pcx", sizeof(fullname));
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
    return !!(image->flags & IF_TRANSPARENT);
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
#if USE_REF == REF_SOFT
            // TODO: account for MIPSIZE, TEX_BYTES
            Com_PageInMemory(image->pixels[0], image->upload_width * image->upload_height * 4);
#endif
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
    qerror_t    ret;
    ssize_t     len;
    int         i;

    // get the palette
    len = FS_LoadFile(R_COLORMAP_PCX, (void **)&data);
    if (!data) {
        ret = len;
        goto fail;
    }

    ret = _IMG_LoadPCX(data, len, NULL, pal, NULL, NULL);

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
#if USE_TGA
    { "screenshottga", IMG_ScreenShotTGA_f },
#endif
#if USE_JPG
    { "screenshotjpg", IMG_ScreenShotJPG_f },
#endif
#if USE_PNG
    { "screenshotpng", IMG_ScreenShotPNG_f },
#endif

    { NULL }
};

void IMG_Init(void)
{
    int i;

    if (r_numImages) {
        Com_Error(ERR_FATAL, "%s: %d images not freed", __func__, r_numImages);
    }


#if USE_PNG || USE_JPG || USE_TGA
    r_override_textures = Cvar_Get("r_override_textures", "1", CVAR_FILES);
    r_texture_formats = Cvar_Get("r_texture_formats",
#if USE_PNG
                                 "p"
#endif
#if USE_JPG
                                 "j"
#endif
#if USE_TGA
                                 "t"
#endif
                                 , 0);
    r_texture_formats->changed = r_texture_formats_changed;
    r_texture_formats_changed(r_texture_formats);

#if USE_JPG
    r_screenshot_format = Cvar_Get("gl_screenshot_format", "jpg", 0);
#elif USE_PNG
    r_screenshot_format = Cvar_Get("gl_screenshot_format", "png", 0);
#endif
#if USE_JPG
    r_screenshot_quality = Cvar_Get("gl_screenshot_quality", "100", 0);
#endif
#if USE_PNG
    r_screenshot_compression = Cvar_Get("gl_screenshot_compression", "6", 0);
#endif
#endif // USE_PNG || USE_JPG || USE_TGA

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
