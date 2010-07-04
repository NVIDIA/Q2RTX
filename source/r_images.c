/*
Copyright (C) 2003-2008 Andrey Nazarov
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

//
// images.c -- image reading and writing functions
//

#include "com_local.h"

#if USE_PNG
#include <png.h>
#endif

#if USE_JPG
#if !USE_PNG
#include <setjmp.h>
#endif
#include <jpeglib.h>
#endif

#include "q_list.h"
#include "files.h"
#include "sys_public.h"
#include "r_shared.h"

/*
=================================================================

PCX LOADING

=================================================================
*/

#include "d_pcx.h"

/*
==============
IMG_LoadPCX
==============
*/
qerror_t IMG_LoadPCX( const char *filename, byte **pic, byte *palette, int *width, int *height ) {
    byte    *raw, *end;
    dpcx_t  *pcx;
    size_t  len, x, y, w, h;
    int     dataByte, runLength;
    byte    *out, *pix;
    qerror_t ret;

    if( !filename ) {
        Com_Error( ERR_FATAL, "LoadPCX: NULL" );
    }
    if( pic ) {
        *pic = NULL;
    }

    //
    // load the file
    //
    len = FS_LoadFile( filename, (void **)&pcx );
    if( !pcx ) {
        return len;
    }

    //
    // parse the PCX file
    //
    if( len < sizeof( *pcx ) ) {
        ret = Q_ERR_FILE_TOO_SMALL;
        goto fail2;
    }

    if( pcx->manufacturer != 0x0a || pcx->version != 5 ) {
        ret = Q_ERR_UNKNOWN_FORMAT;
        goto fail2;
    }

    w = LittleShort( pcx->xmax ) + 1;
    h = LittleShort( pcx->ymax ) + 1;
    if( pcx->encoding != 1 || pcx->bits_per_pixel != 8 || w > 640 || h > 480 ) {
        ret = Q_ERR_INVALID_FORMAT;
        goto fail2;
    }

    //
    // get palette
    //
    if( palette ) {
        if( len < 768 ) {
            ret = Q_ERR_FILE_TOO_SMALL;
            goto fail2;
        }
        memcpy( palette, ( byte * )pcx + len - 768, 768 );
    }

    //
    // get pixels
    //
    if( pic ) {
        pix = out = IMG_AllocPixels( w * h );

        raw = pcx->data;
        end = ( byte * )pcx + len;

        for( y = 0; y < h; y++, pix += w ) {
            for( x = 0; x < w; ) {
                if( raw >= end ) {
                    ret = Q_ERR_BAD_EXTENT;
                    goto fail1;
                }
                dataByte = *raw++;

                if( ( dataByte & 0xC0 ) == 0xC0 ) {
                    runLength = dataByte & 0x3F;
                    if( x + runLength > w ) {
                        ret = Q_ERR_BAD_RLE_PACKET;
                        goto fail1;
                    }
                    if( raw >= end ) {
                        ret = Q_ERR_BAD_RLE_PACKET;
                        goto fail1;
                    }
                    dataByte = *raw++;
                    while( runLength-- ) {
                        pix[x++] = dataByte;
                    }
                } else {
                    pix[x++] = dataByte;
                }
            }
        }

        *pic = out;
    }

    if( width )
        *width = w;
    if( height )
        *height = h;

    FS_FreeFile( pcx );
    return Q_ERR_SUCCESS;

fail1:
    IMG_FreePixels( out );
fail2:
    FS_FreeFile( pcx );
    return ret;
}

#if 0
/*
==============
IMG_SavePCX
==============
*/
qerror_t IMG_SavePCX( const char *filename, const byte *data, int width,
    int height, int rowbytes, byte *palette ) 
{
    int         i, j;
    size_t      len;
    dpcx_t      *pcx;
    byte        *pack;
    qerror_t    ret;

    pcx = FS_AllocTempMem( width * height * 2 + 1000 );
    pcx->manufacturer = 0x0a;   // PCX id
    pcx->version = 5;           // 256 color
    pcx->encoding = 1;          // uncompressed
    pcx->bits_per_pixel = 8;    // 256 color
    pcx->xmin = 0;
    pcx->ymin = 0;
    pcx->xmax = LittleShort( width - 1 );
    pcx->ymax = LittleShort( height - 1 );
    pcx->hres = LittleShort( width );
    pcx->vres = LittleShort( height );
    memset( pcx->palette, 0, sizeof( pcx->palette ) );
    pcx->color_planes = 1;      // chunky image
    pcx->bytes_per_line = LittleShort( width );
    pcx->palette_type = LittleShort( 2 );       // not a grey scale
    memset( pcx->filler, 0, sizeof( pcx->filler ) );

// pack the image
    pack = pcx->data;
    for( i = 0; i < height; i++) {
        for( j = 0; j < width; j++) {
            if( ( *data & 0xc0 ) == 0xc0 ) {
                *pack++ = 0xc1;
            }
            *pack++ = *data++;
        }
        data += rowbytes - width;
    }
            
// write the palette
    *pack++ = 0x0c;     // palette ID byte
    for( i = 0; i < 768; i++ )
        *pack++ = *palette++;
 
// write output file 
    len = pack - ( byte * )pcx;
    ret = FS_WriteFile( filename, pcx, len );
    FS_FreeFile( pcx );

    return ret;
}
#endif

#if USE_TGA

/*
=========================================================

TARGA IMAGES

=========================================================
*/

#define TARGA_HEADER_SIZE  18

#define TGA_DECODE( x ) \
    static qerror_t tga_decode_##x( byte *in, byte *out, int cols, int rows, byte *max_in )

typedef qerror_t (*tga_decode_t)( byte *, byte *, int, int, byte * );

TGA_DECODE( bgr ) {
    int col, row;
    byte *out_row;

    for( row = rows - 1; row >= 0; row-- ) {
        out_row = out + row * cols * 4;
        for( col = 0; col < cols; col++, out_row += 4, in += 3 ) {
            out_row[0] = in[2];
            out_row[1] = in[1];
            out_row[2] = in[0];
            out_row[3] = 255;
        }
    }

    return Q_ERR_SUCCESS;
}

TGA_DECODE( bgra ) {
    int col, row;
    byte *out_row;

    for( row = rows - 1; row >= 0; row-- ) {
        out_row = out + row * cols * 4;
        for( col = 0; col < cols; col++, out_row += 4, in += 4 ) {
            out_row[0] = in[2];
            out_row[1] = in[1];
            out_row[2] = in[0];
            out_row[3] = in[3];
        }
    }

    return Q_ERR_SUCCESS;
}

TGA_DECODE( bgr_flip ) {
    int i, count = rows * cols;

    for( i = 0; i < count; i++, out += 4, in += 3 ) {
        out[0] = in[2];
        out[1] = in[1];
        out[2] = in[0];
        out[3] = 255;
    }

    return Q_ERR_SUCCESS;
}

TGA_DECODE( bgra_flip ) {
    int i, count = rows * cols;

    for( i = 0; i < count; i++, out += 4, in += 3 ) {
        out[0] = in[2];
        out[1] = in[1];
        out[2] = in[0];
        out[3] = in[3];
    }

    return Q_ERR_SUCCESS;
}

TGA_DECODE( bgr_rle ) {
    int col, row;
    byte *out_row;
    uint32_t color;
    unsigned packet_header, packet_size;
    int j;

    for( row = rows - 1; row >= 0; row-- ) {
        out_row = out + row * cols * 4;

        for( col = 0; col < cols; ) {
            packet_header = *in++;
            packet_size = 1 + ( packet_header & 0x7f );

            if( packet_header & 0x80 ) {
                // run-length packet
                if( in + 3 > max_in ) {
                    return Q_ERR_BAD_RLE_PACKET;
                }
                color = MakeColor( in[2], in[1], in[0], 255 );
                in += 3;
                for( j = 0; j < packet_size; j++ ) {
                    *(uint32_t *)out_row = color;
                    out_row += 4;

                    if( ++col == cols ) {
                        // run spans across rows
                        col = 0;
                        if( row > 0 )
                            row--;
                        else
                            goto break_out;
                        out_row = out + row * cols * 4;
                    }
                }
            } else {
                // non run-length packet
                if( in + 3 * packet_size > max_in ) {
                    return Q_ERR_BAD_RLE_PACKET;
                }
                for( j = 0; j < packet_size; j++ ) {
                    out_row[0] = in[2];
                    out_row[1] = in[1];
                    out_row[2] = in[0];
                    out_row[3] = 255;
                    out_row += 4;
                    in += 3;

                    if( ++col == cols ) {
                        // run spans across rows
                        col = 0;
                        if( row > 0 )
                            row--;
                        else
                            goto break_out;
                        out_row = out + row * cols * 4;
                    }                        
                }
            }
        }
    }

break_out:
    return Q_ERR_SUCCESS;
}

TGA_DECODE( bgra_rle ) {
    int col, row;
    byte *out_row;
    uint32_t color;
    unsigned packet_header, packet_size;
    int j;

    for( row = rows - 1; row >= 0; row-- ) {
        out_row = out + row * cols * 4;

        for( col = 0; col < cols; ) {
            packet_header = *in++;
            packet_size = 1 + ( packet_header & 0x7f );

            if( packet_header & 0x80 ) {
                // run-length packet
                if( in + 4 > max_in ) {
                    return Q_ERR_BAD_RLE_PACKET;
                }
                color = MakeColor( in[2], in[1], in[0], in[3] );
                in += 4;
                for( j = 0; j < packet_size; j++ ) {
                    *(uint32_t *)out_row = color;
                    out_row += 4;

                    if( ++col == cols ) {
                        // run spans across rows
                        col = 0;
                        if( row > 0 )
                            row--;
                        else
                            goto break_out;
                        out_row = out + row * cols * 4;
                    }
                }
            } else {
                // non run-length packet
                if( in + 4 * packet_size > max_in ) {
                    return Q_ERR_BAD_RLE_PACKET;
                }
                for( j = 0; j < packet_size; j++ ) {
                    out_row[0] = in[2];
                    out_row[1] = in[1];
                    out_row[2] = in[0];
                    out_row[3] = in[3];
                    out_row += 4;
                    in += 4;

                    if( ++col == cols ) {
                        // run spans across rows
                        col = 0;
                        if( row > 0 )
                            row--;
                        else
                            goto break_out;
                        out_row = out + row * cols * 4;
                    }                        
                }
            }
        }
    }

break_out:
    return Q_ERR_SUCCESS;
}

/*
=============
IMG_LoadTGA
=============
*/
qerror_t IMG_LoadTGA( const char *filename, byte **pic, int *width, int *height ) {
    byte *buffer;
    size_t length, offset;
    byte *pixels;
    unsigned w, h, id_length, image_type, pixel_size, attributes, bpp;
    tga_decode_t decode;
    qerror_t ret;

    if( !filename || !pic ) {
        Com_Error( ERR_FATAL, "LoadTGA: NULL" );
    }

    *pic = NULL;

    //
    // load the file
    //
    length = FS_LoadFile( filename, ( void ** )&buffer );
    if( !buffer ) {
        return length;
    }

    if( length < TARGA_HEADER_SIZE ) {
        ret = Q_ERR_FILE_TOO_SMALL;
        goto finish;
    }

    id_length = buffer[0];
    image_type = buffer[2];
    w = LittleShortMem( &buffer[12] );
    h = LittleShortMem( &buffer[14] );
    pixel_size = buffer[16];
    attributes = buffer[17];
    
    // skip TARGA image comment
    offset = TARGA_HEADER_SIZE + id_length;
    if( offset + 4 > length ) {
        ret = Q_ERR_BAD_EXTENT;
        goto finish;
    }

    if( pixel_size == 32 ) {
        bpp = 4;
    } else if( pixel_size == 24 ) {
        bpp = 3;
    } else {
        Com_DPrintf( "%s: %s: only 32 and 24 bit targa RGB images supported\n", __func__, filename );
        ret = Q_ERR_INVALID_FORMAT;
        goto finish;
    }

    if( w < 1 || h < 1 || w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE ) {
        Com_DPrintf( "%s: %s: invalid image dimensions\n", __func__, filename );
        ret = Q_ERR_INVALID_FORMAT;
        goto finish;
    }

    if( image_type == 2 ) {
        if( offset + w * h * bpp > length ) {
            ret = Q_ERR_BAD_EXTENT;
            goto finish;
        }
        if( attributes & 32 ) {
            if( pixel_size == 32 ) {
                decode = tga_decode_bgra_flip;
            } else {
                decode = tga_decode_bgr_flip;
            }
        } else {
            if( pixel_size == 32 ) {
                decode = tga_decode_bgra;
            } else {
                decode = tga_decode_bgr;
            }
        }
    } else if( image_type == 10 ) {
        if( attributes & 32 ) {
            Com_DPrintf( "%s: %s: vertically flipped, RLE encoded images are not supported\n", __func__, filename );
            ret = Q_ERR_INVALID_FORMAT;
            goto finish;
        }
        if( pixel_size == 32 ) {
            decode = tga_decode_bgra_rle;
        } else {
            decode = tga_decode_bgr_rle;
        }
    } else {
        Com_DPrintf( "%s: %s: only type 2 and 10 targa RGB images supported\n", __func__, filename );
        ret = Q_ERR_INVALID_FORMAT;
        goto finish;
    }

    pixels = IMG_AllocPixels( w * h * 4 );
    ret = decode( buffer + offset, pixels, w, h, buffer + length );
    if( ret < 0 ) {
        IMG_FreePixels( pixels );
        goto finish;
    }

    *pic = pixels;
    *width = w;
    *height = h;

finish:
    FS_FreeFile( buffer );
    return ret;
}

/*
=================
IMG_SaveTGA
=================
*/
qerror_t IMG_SaveTGA( qhandle_t f, const char *filename, const byte *bgr, int width, int height, int unused ) {
    size_t len;
    byte header[TARGA_HEADER_SIZE];
    ssize_t ret;
    
    memset( &header, 0, sizeof( header ) );
    header[ 2] = 2;        // uncompressed type
    header[12] = width & 255;
    header[13] = width >> 8;
    header[14] = height & 255;
    header[15] = height >> 8;
    header[16] = 24;     // pixel size

    ret = FS_Write( &header, sizeof( header ), f );
    if( ret < 0 ) {
        return ret;
    }

    len = width * height * 3;
    ret = FS_Write( bgr, len, f );
    if( ret < 0 ) {
        return ret;
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

METHODDEF( void )my_output_message( j_common_ptr cinfo ) {
    char buffer[JMSG_LENGTH_MAX];
    my_error_ptr jerr = ( my_error_ptr )cinfo->err;

    (*cinfo->err->format_message)( cinfo, buffer );

    Com_EPrintf( "libjpeg: %s: %s\n", jerr->filename, buffer );
}

METHODDEF( void )my_error_exit( j_common_ptr cinfo ) {
    my_error_ptr jerr = ( my_error_ptr )cinfo->err;

    (*cinfo->err->output_message)( cinfo );

    jerr->error = Q_ERR_LIBRARY_ERROR;
    longjmp( jerr->setjmp_buffer, 1 );
}

METHODDEF( void )mem_init_source( j_decompress_ptr cinfo ) { }

METHODDEF( boolean )mem_fill_input_buffer( j_decompress_ptr cinfo ) {
    my_error_ptr jerr = ( my_error_ptr )cinfo->err;

    jerr->error = Q_ERR_FILE_TOO_SMALL;
    longjmp( jerr->setjmp_buffer, 1 );
    return TRUE;
}

METHODDEF( void )mem_skip_input_data( j_decompress_ptr cinfo, long num_bytes ) {
    struct jpeg_source_mgr *src = cinfo->src;
    my_error_ptr jerr = ( my_error_ptr )cinfo->err;
    
    if( src->bytes_in_buffer < num_bytes ) {
        jerr->error = Q_ERR_FILE_TOO_SMALL;
        longjmp( jerr->setjmp_buffer, 1 );
    }
    
    src->next_input_byte += ( size_t )num_bytes;
    src->bytes_in_buffer -= ( size_t )num_bytes;
}

METHODDEF( void )mem_term_source( j_decompress_ptr cinfo ) { }

METHODDEF( void )my_mem_src( j_decompress_ptr cinfo, byte *data, size_t size ) {
    cinfo->src = ( struct jpeg_source_mgr * )(*cinfo->mem->alloc_small)(
        ( j_common_ptr )cinfo, JPOOL_PERMANENT, sizeof( struct jpeg_source_mgr ) );

    cinfo->src->init_source = mem_init_source;
    cinfo->src->fill_input_buffer = mem_fill_input_buffer;
    cinfo->src->skip_input_data = mem_skip_input_data;
    cinfo->src->resync_to_restart = jpeg_resync_to_restart;
    cinfo->src->term_source = mem_term_source;
    cinfo->src->bytes_in_buffer = size;
    cinfo->src->next_input_byte = data;
}

/*
=================
IMG_LoadJPG
=================
*/
qerror_t IMG_LoadJPG( const char *filename, byte **pic, int *width, int *height ) {
    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;
    JSAMPROW row_pointer;
    byte buffer[MAX_TEXTURE_SIZE*3];
    byte *rawdata;
    size_t rawlength;
    byte *volatile pixels;
    byte *in, *out;
    int i;
    qerror_t ret;

    if( !filename || !pic ) {
        Com_Error( ERR_FATAL, "LoadJPG: NULL" );
    }

    *pic = pixels = NULL;

    rawlength = FS_LoadFile( filename, ( void ** )&rawdata );
    if( !rawdata ) {
        return rawlength;
    }

    cinfo.err = jpeg_std_error( &jerr.pub );
    jerr.pub.error_exit = my_error_exit;
    jerr.pub.output_message = my_output_message;
    jerr.filename = filename;
    jerr.error = Q_ERR_FAILURE;

    jpeg_create_decompress( &cinfo );

    if( setjmp( jerr.setjmp_buffer ) ) {
        IMG_FreePixels( pixels );
        ret = jerr.error;
        goto fail;
    }
 
    my_mem_src( &cinfo, rawdata, rawlength );
    jpeg_read_header( &cinfo, TRUE );

    if( cinfo.jpeg_color_space != JCS_RGB && cinfo.jpeg_color_space != JCS_GRAYSCALE ) {
        Com_DPrintf( "%s: %s: invalid image color space\n", __func__, filename );
        ret = Q_ERR_INVALID_FORMAT;
        goto fail;
    }

    jpeg_start_decompress( &cinfo );

    if( cinfo.output_components != 3 && cinfo.output_components != 1 ) {
        Com_DPrintf( "%s: %s: invalid number of color components\n", __func__, filename );
        ret = Q_ERR_INVALID_FORMAT;
        goto fail;
    }

    if( cinfo.output_width > MAX_TEXTURE_SIZE || cinfo.output_height > MAX_TEXTURE_SIZE ) {
        Com_DPrintf( "%s: %s: invalid image dimensions\n", __func__, filename );
        ret = Q_ERR_INVALID_FORMAT;
        goto fail;
    }

    pixels = out = IMG_AllocPixels( cinfo.output_height * cinfo.output_width * 4 );
    row_pointer = ( JSAMPROW )buffer;

    if( cinfo.output_components == 3 ) {
        while( cinfo.output_scanline < cinfo.output_height ) {
            jpeg_read_scanlines( &cinfo, &row_pointer, 1 );

            in = buffer;
            for( i = 0; i < cinfo.output_width; i++, out += 4, in += 3 ) {
                out[0] = in[0];
                out[1] = in[1];
                out[2] = in[2];
                out[3] = 255;
            }
        }
    } else {
        while( cinfo.output_scanline < cinfo.output_height ) {
            jpeg_read_scanlines( &cinfo, &row_pointer, 1 );

            in = buffer;
            for( i = 0; i < cinfo.output_width; i++, out += 4, in += 1 ) {
                out[0] = out[1] = out[2] = in[0];
                out[3] = 255;
            }
        }
    }

    *width = cinfo.output_width;
    *height = cinfo.output_height;

    jpeg_finish_decompress( &cinfo );

    *pic = pixels;
    ret = Q_ERR_SUCCESS;

fail:
    jpeg_destroy_decompress( &cinfo );
    FS_FreeFile( rawdata );
    return ret;
}

#define OUTPUT_BUF_SIZE         0x10000 // 64 KiB

typedef struct my_destination_mgr {
    struct jpeg_destination_mgr pub;

    qhandle_t f;
    JOCTET *buffer;
} *my_dest_ptr;

METHODDEF( void ) vfs_init_destination( j_compress_ptr cinfo ) {
    my_dest_ptr dest = ( my_dest_ptr )cinfo->dest;

    // Allocate the output buffer --- it will be released when done with image
    dest->buffer = ( JOCTET * )(*cinfo->mem->alloc_small)
        ( ( j_common_ptr )cinfo, JPOOL_IMAGE, OUTPUT_BUF_SIZE * sizeof( JOCTET ) );

    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = OUTPUT_BUF_SIZE;
}

METHODDEF( boolean ) vfs_empty_output_buffer( j_compress_ptr cinfo ) {
    my_dest_ptr dest = ( my_dest_ptr )cinfo->dest;
    my_error_ptr jerr = ( my_error_ptr )cinfo->err;
    ssize_t ret;

    ret = FS_Write( dest->buffer, OUTPUT_BUF_SIZE, dest->f );
    if( ret != OUTPUT_BUF_SIZE ) {
        jerr->error = ret < 0 ? ret : Q_ERR_FAILURE;
        longjmp( jerr->setjmp_buffer, 1 );
    }

    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = OUTPUT_BUF_SIZE;

    return TRUE;
}

METHODDEF( void ) vfs_term_destination( j_compress_ptr cinfo ) {
    my_dest_ptr dest = ( my_dest_ptr )cinfo->dest;
    my_error_ptr jerr = ( my_error_ptr )cinfo->err;
    size_t remaining = OUTPUT_BUF_SIZE - dest->pub.free_in_buffer;
    ssize_t ret;

    // Write any data remaining in the buffer
    if( remaining > 0 ) {
        ret = FS_Write( dest->buffer, remaining, dest->f );
        if( ret != remaining ) {
            jerr->error = ret < 0 ? ret : Q_ERR_FAILURE;
            longjmp( jerr->setjmp_buffer, 1 );
        }
    }
}

METHODDEF( void ) my_vfs_dst( j_compress_ptr cinfo, qhandle_t f ) {
    my_dest_ptr dest;

    dest = ( my_dest_ptr )(*cinfo->mem->alloc_small)
        ( ( j_common_ptr )cinfo, JPOOL_PERMANENT, sizeof( struct my_destination_mgr ) );
    cinfo->dest = &dest->pub;

    dest->pub.init_destination = vfs_init_destination;
    dest->pub.empty_output_buffer = vfs_empty_output_buffer;
    dest->pub.term_destination = vfs_term_destination;
    dest->f = f;
}

/*
=================
IMG_SaveJPG
=================
*/
qerror_t IMG_SaveJPG( qhandle_t f, const char *filename, const byte *rgb, int width, int height, int quality ) {
    struct jpeg_compress_struct cinfo;
    struct my_error_mgr jerr;
    volatile JSAMPARRAY row_pointers;
    int row_stride;
    qerror_t ret;
    int i;

    cinfo.err = jpeg_std_error( &jerr.pub );
    jerr.pub.error_exit = my_error_exit;
    jerr.filename = filename;
    jerr.error = Q_ERR_FAILURE;

    row_pointers = NULL;

    if( setjmp( jerr.setjmp_buffer ) ) {
        ret = jerr.error;
        goto fail;
    }

    jpeg_create_compress( &cinfo );

    my_vfs_dst( &cinfo, f );

    cinfo.image_width = width;      // image width and height, in pixels
    cinfo.image_height = height;
    cinfo.input_components = 3;     // # of color components per pixel
    cinfo.in_color_space = JCS_RGB; // colorspace of input image

    jpeg_set_defaults( &cinfo );
    jpeg_set_quality( &cinfo, clamp( quality, 0, 100 ), TRUE );

    jpeg_start_compress( &cinfo, TRUE );

    row_pointers = FS_AllocTempMem( sizeof( JSAMPROW ) * height );
    row_stride = width * 3;    // JSAMPLEs per row in image_buffer

    for( i = 0; i < height; i++ ) {
        row_pointers[i] = ( JSAMPROW )( rgb + ( height - i - 1 ) * row_stride );
    }

    jpeg_write_scanlines( &cinfo, row_pointers, height );

    jpeg_finish_compress( &cinfo );

    ret = Q_ERR_SUCCESS;

fail:
    FS_FreeFile( row_pointers );
    jpeg_destroy_compress( &cinfo );
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

static void my_png_read_fn( png_structp png_ptr, png_bytep buf, png_size_t size ) {
    my_png_io *io = png_get_io_ptr( png_ptr );

    if( size > io->avail_in ) {
        my_png_error *err = png_get_error_ptr( png_ptr );
        err->error = Q_ERR_FILE_TOO_SMALL;
        png_error( png_ptr, "read error" );
    } else {
        memcpy( buf, io->next_in, size );
        io->next_in += size;
        io->avail_in -= size;
    }
}

static void my_png_error_fn( png_structp png_ptr, png_const_charp error_msg ) {
    my_png_error *err = png_get_error_ptr( png_ptr );

    if( err->error == Q_ERR_LIBRARY_ERROR ) {
        Com_EPrintf( "libpng: %s: %s\n", err->filename, error_msg );
    }
    longjmp( png_jmpbuf( png_ptr ), -1 );
}

static void my_png_warning_fn( png_structp png_ptr, png_const_charp warning_msg ) {
    my_png_error *err = png_get_error_ptr( png_ptr );

    Com_WPrintf( "libpng: %s: %s\n", err->filename, warning_msg );
}

/*
=================
IMG_LoadPNG
=================
*/
qerror_t IMG_LoadPNG( const char *filename, byte **pic, int *width, int *height ) {
    byte *rawdata;
    size_t rawlength;
    byte *volatile pixels;
    png_bytep row_pointers[MAX_TEXTURE_SIZE];
    png_uint_32 w, h, rowbytes, row;
    int bitdepth, colortype;
    png_structp png_ptr;
    png_infop info_ptr;
    my_png_io my_io;
    my_png_error my_err;
    qerror_t ret;

    if( !filename || !pic ) {
        Com_Error( ERR_FATAL, "LoadPNG: NULL" );
    }

    *pic = pixels = NULL;

    rawlength = FS_LoadFile( filename, ( void ** )&rawdata );
    if( !rawdata ) {
        return rawlength;
    }

    ret = Q_ERR_LIBRARY_ERROR;

    my_err.filename = filename;
    my_err.error = Q_ERR_LIBRARY_ERROR;

    png_ptr = png_create_read_struct( PNG_LIBPNG_VER_STRING,
        ( png_voidp )&my_err, my_png_error_fn, my_png_warning_fn );
    if( !png_ptr ) {
        goto fail1;
    }

    info_ptr = png_create_info_struct( png_ptr );
    if( !info_ptr ) {
        goto fail2;
    }

    if( setjmp( png_jmpbuf( png_ptr ) ) ) {
        IMG_FreePixels( pixels );
        ret = my_err.error;
        goto fail2;
    }

    my_io.next_in = rawdata;
    my_io.avail_in = rawlength;
    png_set_read_fn( png_ptr, ( png_voidp )&my_io, my_png_read_fn );

    png_read_info( png_ptr, info_ptr );

    if( !png_get_IHDR( png_ptr, info_ptr, &w, &h, &bitdepth, &colortype, NULL, NULL, NULL ) ) {
        goto fail2;
    }

    if( w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE ) {
        Com_DPrintf( "%s: %s: invalid image dimensions\n", __func__, filename );
        ret = Q_ERR_INVALID_FORMAT;
        goto fail2;
    }

    switch( colortype ) {
    case PNG_COLOR_TYPE_PALETTE:
        png_set_palette_to_rgb( png_ptr );
        break;
    case PNG_COLOR_TYPE_GRAY:
        if( bitdepth < 8 ) {
            png_set_gray_1_2_4_to_8( png_ptr );
        }
        // fall through
    case PNG_COLOR_TYPE_GRAY_ALPHA:
        png_set_gray_to_rgb( png_ptr );
        break;
    }

    if( bitdepth < 8 ) {
        png_set_packing( png_ptr );
    } else if( bitdepth == 16 ) {
        png_set_strip_16( png_ptr );
    }

    if( png_get_valid( png_ptr, info_ptr, PNG_INFO_tRNS ) ) {
        png_set_tRNS_to_alpha( png_ptr );
    }

    png_set_filler( png_ptr, 0xff, PNG_FILLER_AFTER );

    png_read_update_info( png_ptr, info_ptr );

    rowbytes = png_get_rowbytes( png_ptr, info_ptr );
    pixels = IMG_AllocPixels( h * rowbytes );

    for( row = 0; row < h; row++ ) {
        row_pointers[row] = pixels + row * rowbytes;
    }

    png_read_image( png_ptr, row_pointers );

    png_read_end( png_ptr, info_ptr );

    *pic = pixels;
    *width = w;
    *height = h;
    ret = Q_ERR_SUCCESS;

fail2:
    png_destroy_read_struct( &png_ptr, &info_ptr, NULL );
fail1:
    FS_FreeFile( rawdata );
    return ret;
}

static void my_png_write_fn( png_structp png_ptr, png_bytep buf, png_size_t size ) {
    qhandle_t *f = png_get_io_ptr( png_ptr );
    ssize_t ret = FS_Write( buf, size, *f );

    if( ret != size ) {
        my_png_error *err = png_get_error_ptr( png_ptr );
        err->error = ret < 0 ? ret : Q_ERR_FAILURE;
        png_error( png_ptr, "write error" );
    }
}

static void my_png_flush_fn( png_structp png_ptr ) { }

/*
=================
IMG_SavePNG
=================
*/
qerror_t IMG_SavePNG( qhandle_t f, const char *filename, const byte *rgb, int width, int height, int compression ) {
    png_structp png_ptr;
    png_infop info_ptr;
    volatile png_bytepp row_pointers;
    int i, row_stride;
    my_png_error my_err;
    qerror_t ret;

    row_pointers = NULL;
    ret = Q_ERR_LIBRARY_ERROR;

    my_err.filename = filename;
    my_err.error = Q_ERR_LIBRARY_ERROR;

    png_ptr = png_create_write_struct( PNG_LIBPNG_VER_STRING,
        ( png_voidp )&my_err, my_png_error_fn, my_png_warning_fn );
    if( !png_ptr ) {
        goto fail1;
    }

    info_ptr = png_create_info_struct( png_ptr );
    if( !info_ptr ) {
        goto fail2;
    }

    if( setjmp( png_jmpbuf( png_ptr ) ) ) {
        ret = my_err.error;
        goto fail3;
    }

    png_set_write_fn( png_ptr, ( png_voidp )&f,
        my_png_write_fn, my_png_flush_fn );

    png_set_IHDR( png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT );

    png_set_compression_level( png_ptr,
        clamp( compression, Z_NO_COMPRESSION, Z_BEST_COMPRESSION ) );

    row_pointers = FS_AllocTempMem( sizeof( png_bytep ) * height );
    row_stride = width * 3;
    for( i = 0; i < height; i++ ) {
        row_pointers[i] = ( png_bytep )rgb + ( height - i - 1 ) * row_stride;
    }

    png_set_rows( png_ptr, info_ptr, row_pointers );

    png_write_png( png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL );

    ret = Q_ERR_SUCCESS;

fail3:
    FS_FreeFile( row_pointers );
fail2:
    png_destroy_write_struct( &png_ptr, &info_ptr );
fail1:
    return ret;
}

#endif // USE_PNG

/*
=========================================================

IMAGE MANAGER

=========================================================
*/

#define RIMAGES_HASH    256

image_t     r_images[MAX_RIMAGES];
list_t      r_imageHash[RIMAGES_HASH];
int         r_numImages;

uint32_t    d_8to24table[256];

#if USE_PNG || USE_JPG || USE_TGA
static cvar_t   *r_override_textures;
static cvar_t   *r_texture_formats;
#endif

/*
===============
IMG_List_f
===============
*/
static void IMG_List_f( void ) {
    static const char types[8] = "MSWPYC?";
    int        i;
    image_t    *image;
    int        texels, count;

    Com_Printf( "------------------\n");
    texels = count = 0;

    for( i = 0, image = r_images; i < r_numImages; i++, image++ ) {
        if( !image->registration_sequence )
            continue;

        Com_Printf( "%c %4i %4i %s: %s\n",
            types[image->type],
            image->upload_width,
            image->upload_height,
            ( image->flags & if_paletted ) ? "PAL" : "RGB",
            image->name );

        texels += image->upload_width * image->upload_height;
        count++;
    }
    Com_Printf( "Total images: %d (out of %d slots)\n", count, r_numImages );
    Com_Printf( "Total texels: %d (not counting mipmaps)\n", texels );
}

image_t *IMG_Alloc( const char *name ) {
    int i;
    image_t *image;

    // find a free image_t slot
    for( i = 0, image = r_images; i < r_numImages; i++, image++ ) {
        if( !image->registration_sequence )
            break;
    }

    if( i == r_numImages ) {
        if( r_numImages == MAX_RIMAGES )
            Com_Error( ERR_FATAL, "%s: MAX_IMAGES exceeded", __func__ );
        r_numImages++;
    }

    strcpy( image->name, name );

    image->registration_sequence = registration_sequence;

    return image;
}

/*
===============
IMG_Lookup

Finds the given image of the given type.
Case and extension insensitive.
===============
*/
static image_t *IMG_Lookup( const char *name, imagetype_t type,
                       unsigned hash, size_t baselength )
{
    image_t *image;

    // look for it
    LIST_FOR_EACH( image_t, image, &r_imageHash[hash], entry ) {
        if( image->type != type ) {
            continue;
        }
        // FIXME
        if( !FS_pathcmpn( image->name, name, baselength ) ) {
            return image;
        }
    }

    return NULL;
}

/*
===============
IMG_Create

Allocates and loads image from supplied data.
===============
*/
image_t *IMG_Create( const char *name, byte *pic, int width, int height,
                        imagetype_t type, imageflags_t flags )
{
    image_t *image;

    image = IMG_Alloc( name );
    IMG_Load( image, pic, width, height, type, flags );
    return image;
}

/*
===============
IMG_Find

Finds or loads the given image, adding it to the hash table.
===============
*/
static qerror_t _IMG_Find( const char *name, imagetype_t type, image_t **image_p ) {
    image_t *image;
    byte *pic;
    int width, height;
    char buffer[MAX_QPATH];
    char *ext;
    size_t len;
    unsigned hash, extHash;
    imageflags_t flags;
#if USE_PNG || USE_JPG || USE_TGA
    char *s;
#endif
    qerror_t err;

    *image_p = NULL;

    if( !name ) {
        Com_Error( ERR_FATAL, "%s: NULL", __func__ );
    }

    len = strlen( name );
    if( len >= MAX_QPATH ) {
        Com_Error( ERR_FATAL, "%s: oversize name", __func__ );
    }

    if( len <= 4 ) {
        return Q_ERR_INVALID_PATH; // must have at least 1 char of base name 
    }

    len -= 4;
    if( name[len] != '.' ) {
        return Q_ERR_INVALID_PATH;
    }
    
    strcpy( buffer, name );
    buffer[len] = 0;

    hash = Com_HashPath( buffer, RIMAGES_HASH );

    if( ( image = IMG_Lookup( buffer, type, hash, len ) ) != NULL ) {
        image->registration_sequence = registration_sequence;
        *image_p = image;
        return Q_ERR_SUCCESS;
    }

    ext = buffer + len;
    Q_strlwr( ext + 1 );
    extHash = MakeRawLong( '.', ext[1], ext[2], ext[3] );

    //
    // create the pic from disk
    //
    pic = NULL;
    flags = 0;

#if USE_PNG || USE_JPG || USE_TGA
    if( r_override_textures->integer ) {
        for( s = r_texture_formats->string; *s; s++ ) {
            switch( *s ) {
#if USE_PNG
            case 'p': // try *.png
                strcpy( ext, ".png" );
                err = IMG_LoadPNG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_JPG
            case 'j': // try *.jpg
                strcpy( ext, ".jpg" );
                err = IMG_LoadJPG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_TGA
            case 't': // try *.tga
                strcpy( ext, ".tga" );
                err = IMG_LoadTGA( buffer, &pic, &width, &height );
                break;
#endif
            default:
                continue;
            }
            if( pic ) {
                // replacing 8 bit texture with 32 bit texture
                if( extHash == EXTENSION_WAL ) {
                    flags |= if_replace_wal;
                } else if( extHash == EXTENSION_PCX ) {
                    flags |= if_replace_pcx;
                }
                goto create;
            }
            if( err != Q_ERR_NOENT ) {
                return err;
            }
        }

        if( extHash == EXTENSION_WAL ) {
            strcpy( ext, ".wal" );
            if( ( image = IMG_LoadWAL( buffer ) ) != NULL ) {
                goto append;
            }
            err = Q_ERR_NOENT;
        } else {
            strcpy( ext, ".pcx" );
            err = IMG_LoadPCX( buffer, &pic, NULL, &width, &height );
            if( pic ) {
                flags |= if_paletted;
                goto create;
            }
        }

        return err;
    }
#endif

    switch( extHash ) {
    case EXTENSION_PNG:
#if USE_PNG
        // try *.png
        strcpy( ext, ".png" );
        err = IMG_LoadPNG( buffer, &pic, &width, &height );
        if( pic ) {
            goto create;
        }
        if( err != Q_ERR_NOENT ) {
            return err;
        }
#endif
#if USE_JPG || USE_TGA
        for( s = r_texture_formats->string; *s; s++ ) {
            switch( *s ) {
#if USE_JPG
            case 'j': // try *.jpg
                strcpy( ext, ".jpg" );
                err = IMG_LoadJPG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_TGA
            case 't': // try *.tga
                strcpy( ext, ".tga" );
                err = IMG_LoadTGA( buffer, &pic, &width, &height );
                break;
#endif
            default:
                continue;
            }
            if( pic ) {
                goto create;
            }
            if( err != Q_ERR_NOENT ) {
                return err;
            }
        }
#endif
        // try *.pcx
        strcpy( ext, ".pcx" );
        err = IMG_LoadPCX( buffer, &pic, NULL, &width, &height );
        if( pic ) {
            flags |= if_paletted;
            goto create;
        }
        return err;

    case EXTENSION_TGA:
#if USE_TGA
        strcpy( ext, ".tga" );
        err = IMG_LoadTGA( buffer, &pic, &width, &height );
        if( pic ) {
            goto create;
        }
        if( err != Q_ERR_NOENT ) {
            return err;
        }
#endif

#if USE_PNG || USE_JPG
        for( s = r_texture_formats->string; *s; s++ ) {
            switch( *s ) {
#if USE_PNG
            case 'p': // try *.png
                strcpy( ext, ".png" );
                err = IMG_LoadPNG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_JPG
            case 'j': // try *.jpg
                strcpy( ext, ".jpg" );
                err = IMG_LoadJPG( buffer, &pic, &width, &height );
                break;
#endif
            default:
                continue;
            }
            if( pic ) {
                goto create;
            }
            if( err != Q_ERR_NOENT ) {
                return err;
            }
        }
#endif
        // try *.pcx
        strcpy( ext, ".pcx" );
        err = IMG_LoadPCX( buffer, &pic, NULL, &width, &height );
        if( pic ) {
            flags |= if_paletted;
            goto create;
        }
        return err;

    case EXTENSION_JPG:
#if USE_JPG
        strcpy( ext, ".jpg" );
        err = IMG_LoadJPG( buffer, &pic, &width, &height );
        if( pic ) {
            goto create;
        }
        if( err != Q_ERR_NOENT ) {
            return err;
        }
#endif

#if USE_PNG || USE_TGA
        for( s = r_texture_formats->string; *s; s++ ) {
            switch( *s ) {
#if USE_PNG
            case 'p': // try *.png
                strcpy( ext, ".png" );
                err = IMG_LoadPNG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_TGA
            case 't': // try *.tga
                strcpy( ext, ".tga" );
                err = IMG_LoadTGA( buffer, &pic, &width, &height );
                break;
#endif
            default:
                continue;
            }
            if( pic ) {
                goto create;
            }
            if( err != Q_ERR_NOENT ) {
                return err;
            }
        }
#endif

        // try *.pcx
        strcpy( ext, ".pcx" );
        err = IMG_LoadPCX( buffer, &pic, NULL, &width, &height );
        if( pic ) {
            flags |= if_paletted;
            goto create;
        }
        return err;


    case EXTENSION_PCX:
        strcpy( ext, ".pcx" );
        err = IMG_LoadPCX( buffer, &pic, NULL, &width, &height );
        if( pic ) {
            flags |= if_paletted;
            goto create;
        }
        if( err != Q_ERR_NOENT ) {
            return err;
        }

#if USE_PNG || USE_JPG || USE_TGA
        for( s = r_texture_formats->string; *s; s++ ) {
            switch( *s ) {
#if USE_PNG
            case 'p': // try *.png
                strcpy( ext, ".png" );
                err = IMG_LoadPNG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_JPG
            case 'j': // try *.jpg
                strcpy( ext, ".jpg" );
                err = IMG_LoadJPG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_TGA
            case 't': // try *.tga
                strcpy( ext, ".tga" );
                err = IMG_LoadTGA( buffer, &pic, &width, &height );
                break;
#endif
            default:
                continue;
            }
            if( pic ) {
                goto create;
            }
            if( err != Q_ERR_NOENT ) {
                return err;
            }
        }
#endif
        return Q_ERR_NOENT;

    case EXTENSION_WAL:
        strcpy( ext, ".wal" );
        if( ( image = IMG_LoadWAL( buffer ) ) != NULL ) {
            goto append;
        }

#if USE_PNG || USE_JPG || USE_TGA
        // FIXME: no way to figure correct texture dimensions here
        for( s = r_texture_formats->string; *s; s++ ) {
            switch( *s ) {
#if USE_PNG
            case 'p': // try *.png
                strcpy( ext, ".png" );
                err = IMG_LoadPNG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_JPG
            case 'j': // try *.jpg
                strcpy( ext, ".jpg" );
                err = IMG_LoadJPG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_TGA
            case 't': // try *.tga
                strcpy( ext, ".tga" );
                err = IMG_LoadTGA( buffer, &pic, &width, &height );
                break;
#endif
            default:
                continue;
            }
            if( pic ) {
                goto create;
            }
            if( err != Q_ERR_NOENT ) {
                return err;
            }
        }
#endif
        return Q_ERR_NOENT;

    default:
        return Q_ERR_INVALID_PATH;
    }

create:
    image = IMG_Create( buffer, pic, width, height, type, flags );
append:
    List_Append( &r_imageHash[hash], &image->entry );
    *image_p = image;
    return Q_ERR_SUCCESS;
}

image_t *IMG_Find( const char *name, imagetype_t type ) {
    image_t *image;
    qerror_t ret;

    ret = _IMG_Find( name, type, &image );
    if( image ) {
        return image;
    }

    // don't spam about missing images
    if( ret != Q_ERR_NOENT ) {
        Com_EPrintf( "Couldn't load %s: %s\n", name, Q_ErrorString( ret ) );
    }

    return NULL;
}

/*
===============
IMG_ForHandle
===============
*/
image_t *IMG_ForHandle( qhandle_t h ) {
    if( h < 0 || h >= r_numImages ) {
        Com_Error( ERR_FATAL, "%s: %d out of range", __func__, h );
    }

    return &r_images[h];
}

/*
===============
R_RegisterSkin
===============
*/
qhandle_t R_RegisterSkin( const char *name ) {
    image_t *image;

    if( !r_numImages ) {
        return 0;
    }
    image = IMG_Find( name, it_skin );
    if( !image ) {
        return 0;
    }

    return ( image - r_images );
}

static qerror_t _register_image( const char *name, imagetype_t type, qhandle_t *handle ) {
    image_t *image;
    char    fullname[MAX_QPATH];
    size_t  len;
    qerror_t ret;

    *handle = 0;

    if( !r_numImages ) {
        return Q_ERR_AGAIN;
    }

    if( name[0] == '/' || name[0] == '\\' ) {
        ret = _IMG_Find( name + 1, type, &image );
    } else {
        len = Q_concat( fullname, sizeof( fullname ), "pics/", name, NULL );
        if( len >= sizeof( fullname ) ) {
            return Q_ERR_NAMETOOLONG;
        }
        len = COM_DefaultExtension( fullname, ".pcx", sizeof( fullname ) );
        if( len >= sizeof( fullname ) ) {
            return Q_ERR_NAMETOOLONG;
        }
        ret = _IMG_Find( fullname, type, &image );
    }

    if( !image ) {
        return ret;
    }

    *handle = ( image - r_images );
    return Q_ERR_SUCCESS;
}

static qhandle_t register_image( const char *name, imagetype_t type ) {
    qhandle_t handle;
    qerror_t ret;

    ret = _register_image( name, type, &handle );
    if( handle ) {
        return handle;
    }

    // don't spam about missing images
    if( ret != Q_ERR_NOENT ) {
        Com_EPrintf( "Couldn't load %s: %s\n", name, Q_ErrorString( ret ) );
    }

    return 0;
}

/*
================
R_RegisterPic
================
*/
qhandle_t R_RegisterPic( const char *name ) {
    return register_image( name, it_pic );
}

qerror_t _R_RegisterPic( const char *name, qhandle_t *handle ) {
    return _register_image( name, it_pic, handle );
}

/*
================
R_RegisterFont
================
*/
qhandle_t R_RegisterFont( const char *name ) {
    return register_image( name, it_charset );
}

qerror_t _R_RegisterFont( const char *name, qhandle_t *handle ) {
    return _register_image( name, it_charset, handle );
}

/*
================
IMG_FreeUnused

Any image that was not touched on this registration sequence
will be freed.
================
*/
void IMG_FreeUnused( void ) {
    image_t *image, *last;
    int count = 0;

    last = r_images + r_numImages;
    for( image = r_images; image < last; image++ ) {
        if( image->registration_sequence == registration_sequence ) {
#if USE_REF == REF_SOFT
            Com_PageInMemory( image->pixels[0], image->width * image->height );
#endif
            continue;        // used this sequence
        }
        if( !image->registration_sequence )
            continue;        // free image_t slot
        if( image->type == it_pic || image->type == it_charset )
            continue;        // don't free pics
        if( image->flags & if_auto ) {
            continue;        // don't free auto textures
        }

        // delete it from hash table
        List_Remove( &image->entry );

        // free it
        IMG_Unload( image );

        memset( image, 0, sizeof( *image ) );
        count++;
    }

    if( count ) {
        Com_DPrintf( "%s: %i images freed\n", __func__, count );
    }
}

void IMG_FreeAll( void ) {
    image_t *image, *last;
    int i, count = 0;
    
    last = r_images + r_numImages;
    for( image = r_images; image < last; image++ ) {
        if( !image->registration_sequence )
            continue;        // free image_t slot
        // free it
        IMG_Unload( image );
        
        memset( image, 0, sizeof( *image ) );
        count++;
    }

    if( count ) {
        Com_DPrintf( "%s: %i images freed\n", __func__, count );
    }
    
    r_numImages = 0;
    for( i = 0; i < RIMAGES_HASH; i++ ) {
        List_Init( &r_imageHash[i] );
    }
}

/*
===============
R_GetPalette

Reads the palette and (optionally) loads
the colormap for software renderer.
===============
*/
void IMG_GetPalette( byte **pic ) {
    int i;
    qerror_t ret;
    byte pal[768], *src;

    // get the palette
    ret = IMG_LoadPCX( "pics/colormap.pcx", pic, pal, NULL, NULL );
    if( ret < 0 ) {
        Com_Error( ERR_FATAL, "Couldn't load pics/colormap.pcx: %s", Q_ErrorString( ret ) );
    }

    for( i = 0, src = pal; i < 255; i++, src += 3 ) {
        d_8to24table[i] = MakeColor( src[0], src[1], src[2], 255 );
    }

    // 255 is transparent
    d_8to24table[i] = MakeColor( src[0], src[1], src[2], 0 );
}

void IMG_Init( void ) {
    int i;

    if( r_numImages ) {
        Com_Error( ERR_FATAL, "%s: %d images not freed", __func__, r_numImages );
    }


#if USE_PNG || USE_JPG || USE_TGA
    r_override_textures = Cvar_Get( "r_override_textures", "1", CVAR_ARCHIVE|CVAR_FILES );
    r_texture_formats = Cvar_Get( "r_texture_formats",
#if USE_PNG
        "p"
#endif
#if USE_JPG
        "j"
#endif
#if USE_TGA
        "t",
#endif
        0 );
#endif
    Cmd_AddCommand( "imagelist", IMG_List_f );

    for( i = 0; i < RIMAGES_HASH; i++ ) {
        List_Init( &r_imageHash[i] );
    }
}

void IMG_Shutdown( void ) {
    Cmd_RemoveCommand( "imagelist" );
}

