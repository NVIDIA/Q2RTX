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
#include <stdio.h>
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
qboolean IMG_LoadPCX( const char *filename, byte **pic, byte *palette, int *width, int *height ) {
    byte    *raw, *end;
    dpcx_t  *pcx;
    size_t  len, x, y, w, h;
    int     dataByte, runLength;
    byte    *out, *pix;

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
        return qfalse;
    }
    if( len < sizeof( *pcx ) ) {
        Com_WPrintf( "LoadPCX: %s: file too short\n", filename );
        goto fail2;
    }

    //
    // parse the PCX file
    //
    w = LittleShort( pcx->xmax ) + 1;
    h = LittleShort( pcx->ymax ) + 1;
    if( pcx->manufacturer != 0x0a || pcx->version != 5
        || pcx->encoding != 1 || pcx->bits_per_pixel != 8
        || w > 640 || h > 480 )
    {
        Com_WPrintf( "LoadPCX: %s: unsupported format\n", filename );
        goto fail2;
    }

    //
    // get palette
    //
    if( palette ) {
        if( len < 768 ) {
            Com_WPrintf( "LoadPCX: %s: palette too short\n", filename );
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
                    Com_WPrintf( "LoadPCX: %s: read past end of file\n", filename );
                    goto fail1;
                }
                dataByte = *raw++;

                if( ( dataByte & 0xC0 ) == 0xC0 ) {
                    runLength = dataByte & 0x3F;
                    if( x + runLength > w ) {
                        Com_WPrintf( "LoadPCX: %s: run length overrun\n", filename );
                        goto fail1;
                    }
                    if( raw >= end ) {
                        Com_WPrintf( "LoadPCX: %s: read past end of file\n", filename );
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
    return qtrue;

fail1:
    IMG_FreePixels( out );
fail2:
    FS_FreeFile( pcx );
    return qfalse;
}

/*
==============
IMG_WritePCX
==============
*/
qboolean IMG_WritePCX( const char *filename, const byte *data, int width,
    int height, int rowbytes, byte *palette ) 
{
    int         i, j, length;
    dpcx_t      *pcx;
    byte        *pack;
    qboolean    ret = qfalse;
    fileHandle_t    f;

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
    FS_FOpenFile( filename, &f, FS_MODE_WRITE );
    if( !f ) {
        goto fail;
    }

    length = pack - ( byte * )pcx;
    if( FS_Write( pcx, length, f ) == length ) {
        ret = qtrue;
    }
    
    FS_FCloseFile( f );

fail:
    FS_FreeFile( pcx );
    return ret;
} 

#if USE_TGA

/*
=========================================================

TARGA LOADING

=========================================================
*/

#define TGA_DECODE( x ) \
    static qboolean tga_decode_##x( byte *data, byte *pixels, \
        int columns, int rows, byte *maxp )

typedef qboolean (*tga_decode_t)( byte *, byte *, int, int, byte * );

TGA_DECODE( bgr ) {
    int col, row;
    uint32_t *pixbuf;

    for( row = rows - 1; row >= 0; row-- ) {
        pixbuf = ( uint32_t * )pixels + row * columns;

        for( col = 0; col < columns; col++ ) {
            *pixbuf++ = MakeColor( data[2], data[1], data[0], 255 );
            data += 3;
        }
    }

    return qtrue;
}

TGA_DECODE( bgra ) {
    int col, row;
    uint32_t *pixbuf;

    for( row = rows - 1; row >= 0; row-- ) {
        pixbuf = ( uint32_t * )pixels + row * columns;

        for( col = 0; col < columns; col++ ) {
            *pixbuf++ = MakeColor( data[2], data[1], data[0], data[3] );
            data += 4;
        }
    }

    return qtrue;
}

TGA_DECODE( bgr_flip ) {
    int count;
    uint32_t *pixbuf;

    pixbuf = ( uint32_t * )pixels;
    count = rows * columns;
    do {
        *pixbuf++ = MakeColor( data[2], data[1], data[0], 255 );
        data += 3;
    } while( --count );

    return qtrue;
}

TGA_DECODE( bgra_flip ) {
    int count;
    uint32_t *pixbuf;

    pixbuf = ( uint32_t * )pixels;
    count = rows * columns;
    do {
        *pixbuf++ = MakeColor( data[2], data[1], data[0], data[3] );
        data += 4;
    } while( --count );

    return qtrue;
}

TGA_DECODE( bgr_rle ) {
    int col, row;
    uint32_t *pixbuf, color;
    byte packetHeader, packetSize;
    int j;

    for( row = rows - 1; row >= 0; row-- ) {
        pixbuf = ( uint32_t * )pixels + row * columns;

        for( col = 0; col < columns; ) {
            packetHeader = *data++;
            packetSize = 1 + ( packetHeader & 0x7f );

            if( packetHeader & 0x80 ) {
                /* run-length packet */
                if( data + 3 > maxp ) {
                    return qfalse;
                }
                color = MakeColor( data[2], data[1], data[0], 255 );
                data += 3;
                for( j = 0; j < packetSize; j++ ) {
                    *pixbuf++ = color;

                    col++;
                    if( col == columns ) {
                         /* run spans across rows */
                        col = 0;
                        
                        if( row > 0 )
                            row--;
                        else
                            goto breakOut;

                        pixbuf = ( uint32_t * )pixels + row * columns;
                    }
                }
            } else {
                /* non run-length packet */
                if( data + 3 * packetSize > maxp ) {
                    return qfalse;
                }
                for( j = 0; j < packetSize; j++ ) {
                    *pixbuf++ = MakeColor( data[2], data[1], data[0], 255 );
                    data += 3;

                    col++;
                    if( col == columns ) { 
                        /* run spans across rows */
                        col = 0;
                        if( row > 0 )
                            row--;
                        else
                            goto breakOut;
                        pixbuf = ( uint32_t * )pixels + row * columns;
                    }                        
                }
            }
        }
breakOut: ;

    }

    return qtrue;

}

TGA_DECODE( bgra_rle ) {
    int col, row;
    uint32_t *pixbuf, color;
    byte packetHeader, packetSize;
    int j;

    for( row = rows - 1; row >= 0; row-- ) {
        pixbuf = ( uint32_t * )pixels + row * columns;

        for( col = 0; col < columns; ) {
            packetHeader = *data++;
            packetSize = 1 + ( packetHeader & 0x7f );

            if( packetHeader & 0x80 ) {
                /* run-length packet */
                if( data + 4 > maxp ) {
                    return qfalse;
                }
                color = MakeColor( data[2], data[1], data[0], data[3] );
                data += 4;
                for( j = 0; j < packetSize; j++ ) {
                    *pixbuf++ = color;

                    col++;
                    if( col == columns ) {
                         /* run spans across rows */
                        col = 0;
                        
                        if( row > 0 )
                            row--;
                        else
                            goto breakOut;

                        pixbuf = ( uint32_t * )pixels + row * columns;
                    }
                }
            } else {
                /* non run-length packet */
                if( data + 4 * packetSize > maxp ) {
                    return qfalse;
                }
                for( j = 0; j < packetSize; j++ ) {
                    *pixbuf++ = MakeColor( data[2], data[1], data[0], data[3] );
                    data += 4;

                    col++;
                    if( col == columns ) { 
                        /* run spans across rows */
                        col = 0;
                        if( row > 0 )
                            row--;
                        else
                            goto breakOut;
                        pixbuf = ( uint32_t * )pixels + row * columns;
                    }                        
                }
            }
        }
breakOut: ;

    }

    return qtrue;

}

#define TARGA_HEADER_SIZE  18 

/*
=============
LoadTGA
=============
*/
void IMG_LoadTGA( const char *filename, byte **pic, int *width, int *height ) {
    byte *buffer;
    size_t length;
    byte *pixels;
    int offset, w, h;
    tga_decode_t decode;
    int id_length, image_type, pixel_size, attributes, bpp;

    if( !filename || !pic ) {
        Com_Error( ERR_FATAL, "LoadTGA: NULL" );
    }

    *pic = NULL;

    //
    // load the file
    //
    length = FS_LoadFile( filename, ( void ** )&buffer );
    if( !buffer ) {
        return;
    }

    if( length < TARGA_HEADER_SIZE ) {
        Com_WPrintf( "LoadTGA: %s: file too small\n", filename );
        goto finish;
    }

    id_length = buffer[0];
    image_type = buffer[2];
    w = MakeShort( buffer[12], buffer[13] );
    h = MakeShort( buffer[14], buffer[15] );
    pixel_size = buffer[16];
    attributes = buffer[17];
    
    // skip TARGA image comment
    offset = TARGA_HEADER_SIZE + id_length;
    if( offset + 4 > length ) {
        Com_WPrintf( "LoadTGA: %s: offset out of range\n", filename );
        goto finish;
    }

    if( pixel_size == 32 ) {
        bpp = 4;
    } else if( pixel_size == 24 ) {
        bpp = 3;
    } else {
        Com_WPrintf( "LoadTGA: %s: only 32 and 24 bit targa RGB "
                     "images supported, this one is %d bit\n",
                     filename, pixel_size );
        goto finish;
    }

    if( w < 1 || h < 1 || w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE ) {
        Com_WPrintf( "LoadTGA: %s: bad dimensions: %dx%d\n",
            filename, w, h );
        goto finish;
    }

    if( image_type == 2 ) {
        if( offset + w * h * bpp > length ) {
            Com_WPrintf( "LoadTGA: %s: malformed targa image\n", filename );
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
            Com_WPrintf( "LoadTGA: %s: vertically flipped, RLE encoded "
                         "images are not supported\n", filename );
            goto finish;
        }
        if( pixel_size == 32 ) {
            decode = tga_decode_bgra_rle;
        } else {
            decode = tga_decode_bgr_rle;
        }
    } else {
        Com_WPrintf( "LoadTGA: %s: only type 2 and 10 targa RGB "
                     "images supported, this one is %d\n",
                     filename, image_type );
        goto finish;
    }

    pixels = IMG_AllocPixels( w * h * 4 );
    if( decode( buffer + offset, pixels, w, h, buffer + length ) ) {
        *pic = pixels;
        *width = w;
        *height = h;
    } else {
        IMG_FreePixels( pixels );
    }
finish:
    FS_FreeFile( buffer );
}

/*
=========================================================

TARGA WRITING

=========================================================
*/

/*
=================
IMG_WriteTGA
=================
*/
qboolean IMG_WriteTGA( const char *filename, const byte *bgr, int width, int height ) {
    int length;
    fileHandle_t f;
    byte header[TARGA_HEADER_SIZE];
    
    FS_FOpenFile( filename, &f, FS_MODE_WRITE );
    if( !f ) {
        return qfalse;
    }

    memset( &header, 0, sizeof( header ) );
    header[ 2] = 2;        // uncompressed type
    header[12] = width & 255;
    header[13] = width >> 8;
    header[14] = height & 255;
    header[15] = height >> 8;
    header[16] = 24;     // pixel size

    if( FS_Write( &header, sizeof( header ), f ) != sizeof( header ) ) {
        goto fail;
    }

    length = width * height * 3;
    if( FS_Write( bgr, length, f ) != length ) {
        goto fail;
    }
    
    FS_FCloseFile( f );
    return qtrue;
    
fail:
    FS_FCloseFile( f );
    return qfalse;
}

#endif // USE_TGA

/*
=========================================================

JPEG LOADING

=========================================================
*/

#if USE_JPG

typedef struct my_error_mgr {
    struct jpeg_error_mgr   pub;
    jmp_buf                 setjmp_buffer;
    const char              *filename;
} *my_error_ptr;

METHODDEF( void )my_output_message( j_common_ptr cinfo ) {
    char buffer[JMSG_LENGTH_MAX];
    my_error_ptr myerr = ( my_error_ptr )cinfo->err;

    (*cinfo->err->format_message)( cinfo, buffer );

    Com_WPrintf( "LoadJPG: %s: %s\n", myerr->filename, buffer );
}

METHODDEF( void )my_error_exit( j_common_ptr cinfo ) {
    my_error_ptr myerr = ( my_error_ptr )cinfo->err;

    (*cinfo->err->output_message)( cinfo );

    longjmp( myerr->setjmp_buffer, 1 );
}


METHODDEF( void )mem_init_source( j_decompress_ptr cinfo ) { }

METHODDEF( boolean )mem_fill_input_buffer( j_decompress_ptr cinfo ) {
    my_error_ptr jerr = ( my_error_ptr )cinfo->err;

    longjmp( jerr->setjmp_buffer, 1 );
    return TRUE;
}


METHODDEF( void )mem_skip_input_data( j_decompress_ptr cinfo, long num_bytes ) {
    struct jpeg_source_mgr *src = cinfo->src;
    my_error_ptr jerr = ( my_error_ptr )cinfo->err;
    
    if( src->bytes_in_buffer < num_bytes ) {
        longjmp( jerr->setjmp_buffer, 1 );
    }
    
    src->next_input_byte += ( size_t )num_bytes;
    src->bytes_in_buffer -= ( size_t )num_bytes;
}

METHODDEF( void )mem_term_source( j_decompress_ptr cinfo ) { }


METHODDEF( void )jpeg_mem_src( j_decompress_ptr cinfo, byte *data, size_t size ) {
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
LoadJPG
=================
*/
void IMG_LoadJPG( const char *filename, byte **pic, int *width, int *height ) {
    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;
    JSAMPARRAY buffer;
    int row_stride;
    byte *rawdata;
    size_t rawlength;
    byte *pixels;
    byte *src;
    uint32_t *dst;
    int i;

    if( !filename || !pic ) {
        Com_Error( ERR_FATAL, "LoadJPG: NULL" );
    }

    *pic = pixels = NULL;

    rawlength = FS_LoadFile( filename, ( void ** )&rawdata );
    if( !rawdata ) {
        return;
    }

    cinfo.err = jpeg_std_error( &jerr.pub );
    jerr.pub.error_exit = my_error_exit;
    jerr.pub.output_message = my_output_message;
    jerr.filename = filename;

    jpeg_create_decompress( &cinfo );
    
    if( setjmp( jerr.setjmp_buffer ) ) {
        jpeg_destroy_decompress( &cinfo );
        if( pixels ) {
            IMG_FreePixels( pixels );
        }
        FS_FreeFile( rawdata );
        return;
    }
 
    jpeg_mem_src( &cinfo, rawdata, rawlength );
    jpeg_read_header( &cinfo, TRUE );
    jpeg_start_decompress( &cinfo );

    if( cinfo.output_components != 3 /*&& cinfo.output_components != 4*/ ) {
        Com_WPrintf( "LoadJPG: %s: unsupported number of color components: %i\n",
            filename, cinfo.output_components );
        jpeg_destroy_decompress( &cinfo );
        FS_FreeFile( rawdata );
        return;
    }

    *width = cinfo.output_width;
    *height = cinfo.output_height;

    pixels = IMG_AllocPixels( cinfo.output_width * cinfo.output_height * 4 );

    row_stride = cinfo.output_width * cinfo.output_components;

    buffer = (*cinfo.mem->alloc_sarray)( ( j_common_ptr )&cinfo, JPOOL_IMAGE, row_stride, 1 );

    dst = ( uint32_t * )pixels;
    while( cinfo.output_scanline < cinfo.output_height ) {
        jpeg_read_scanlines( &cinfo, buffer, 1 );

        src = ( byte * )buffer[0];
        for( i = 0; i < cinfo.output_width; i++, src += 3 ) {
            *dst++ = MakeColor( src[0], src[1], src[2], 255 );
        }
    }

    jpeg_finish_decompress( &cinfo );
    jpeg_destroy_decompress( &cinfo );

    FS_FreeFile( rawdata );

    *pic = pixels;

}

/*
=========================================================

JPEG WRITING

=========================================================
*/

#define OUTPUT_BUF_SIZE        4096

typedef struct my_destination_mgr {
    struct jpeg_destination_mgr pub; /* public fields */

    fileHandle_t hFile;     /* target stream */
    JOCTET *buffer;         /* start of buffer */
} *my_dest_ptr;


METHODDEF( void ) vfs_init_destination( j_compress_ptr cinfo ) {
    my_dest_ptr dest = ( my_dest_ptr )cinfo->dest;

    /* Allocate the output buffer --- it will be released when done with image */
    dest->buffer = ( JOCTET * )(*cinfo->mem->alloc_small)( ( j_common_ptr )cinfo, JPOOL_IMAGE, OUTPUT_BUF_SIZE * sizeof( JOCTET ) );

    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = OUTPUT_BUF_SIZE;
}

METHODDEF( boolean ) vfs_empty_output_buffer( j_compress_ptr cinfo ) {
    my_dest_ptr dest = ( my_dest_ptr )cinfo->dest;
    my_error_ptr jerr = ( my_error_ptr )cinfo->err;

    if( FS_Write( dest->buffer, OUTPUT_BUF_SIZE, dest->hFile ) != OUTPUT_BUF_SIZE ) {
        longjmp( jerr->setjmp_buffer, 1 );
    }

    dest->pub.next_output_byte = dest->buffer;
    dest->pub.free_in_buffer = OUTPUT_BUF_SIZE;

    return TRUE;

}

METHODDEF( void ) vfs_term_destination( j_compress_ptr cinfo ) {
    my_dest_ptr dest = ( my_dest_ptr )cinfo->dest;
    my_error_ptr jerr = ( my_error_ptr )cinfo->err;
    int remaining = OUTPUT_BUF_SIZE - dest->pub.free_in_buffer;

    /* Write any data remaining in the buffer */
    if( remaining > 0 ) {
        if( FS_Write( dest->buffer, remaining, dest->hFile ) != remaining ) {
            longjmp( jerr->setjmp_buffer, 1 );
        }
    }

}


METHODDEF( void ) jpeg_vfs_dst( j_compress_ptr cinfo, fileHandle_t hFile ) {
    my_dest_ptr dest;

    dest = ( my_dest_ptr )(*cinfo->mem->alloc_small)( ( j_common_ptr )cinfo, JPOOL_PERMANENT, sizeof( struct my_destination_mgr ) );
    cinfo->dest = &dest->pub;

    dest->pub.init_destination = vfs_init_destination;
    dest->pub.empty_output_buffer = vfs_empty_output_buffer;
    dest->pub.term_destination = vfs_term_destination;
    dest->hFile = hFile;

}

/*
=================
IMG_WriteJPG
=================
*/
qboolean IMG_WriteJPG( const char *filename, const byte *rgb, int width, int height, int quality ) {
    struct jpeg_compress_struct cinfo;
    struct my_error_mgr jerr;
    fileHandle_t hFile;
    JSAMPROW row_pointer[1];
    int row_stride;

    FS_FOpenFile( filename, &hFile, FS_MODE_WRITE );
    if( !hFile ) {
        Com_DPrintf( "WriteJPG: %s: couldn't create file\n", filename );
        return qfalse;
    }

    cinfo.err = jpeg_std_error( &jerr.pub );
    jerr.pub.error_exit = my_error_exit;

    if( setjmp( jerr.setjmp_buffer ) ) {
        Com_DPrintf( "WriteJPG: %s: JPEGLIB signaled an error\n", filename );
        jpeg_destroy_compress( &cinfo );
        FS_FCloseFile( hFile );
        return qfalse;
    }

    jpeg_create_compress( &cinfo );

    jpeg_vfs_dst( &cinfo, hFile );

    cinfo.image_width = width;    // image width and height, in pixels
    cinfo.image_height = height;
    cinfo.input_components = 3;        // # of    color components per pixel
    cinfo.in_color_space = JCS_RGB;    // colorspace of input image

    clamp( quality, 0, 100 );

    jpeg_set_defaults( &cinfo );
    jpeg_set_quality( &cinfo, quality, TRUE    );

    jpeg_start_compress( &cinfo, TRUE );

    row_stride = width * 3;    // JSAMPLEs per row in image_buffer

    while( cinfo.next_scanline < cinfo.image_height    ) {
        row_pointer[0] = ( byte * )( &rgb[( cinfo.image_height - cinfo.next_scanline - 1 ) * row_stride] );
        jpeg_write_scanlines( &cinfo, row_pointer, 1 );
    }

    jpeg_finish_compress( &cinfo );
    FS_FCloseFile( hFile );

    jpeg_destroy_compress( &cinfo );

    return qtrue;
}

#endif /* USE_JPG */


#if USE_PNG

/*
=========================================================

PNG LOADING

=========================================================
*/

struct pngReadStruct {
    byte *data;
    byte *maxp;
};

static void QDECL png_vfs_read_fn( png_structp png_ptr, png_bytep buf, png_size_t size ) {
    struct pngReadStruct *r = png_get_io_ptr( png_ptr );

    if( r->data + size > r->maxp ) {
        png_error( png_ptr, "read error" );
    } else {
        memcpy( buf, r->data, size );
        r->data += size;
    }
}

static void QDECL png_console_error_fn( png_structp png_ptr, png_const_charp error_msg ) {
    char *f = png_get_error_ptr( png_ptr );

    Com_EPrintf( "LoadPNG: %s: %s\n", f, error_msg );
    longjmp( png_jmpbuf( png_ptr ), -1 );
}

static void QDECL png_console_warning_fn( png_structp png_ptr, png_const_charp warning_msg ) {
    char *f = png_get_error_ptr( png_ptr );

    Com_WPrintf( "LoadPNG: %s: %s\n", f, warning_msg );
}

/*
=================
LoadPNG
=================
*/
void IMG_LoadPNG( const char *filename, byte **pic, int *width, int *height ) {
    byte *rawdata;
    size_t rawlength;
    byte *pixels;
    png_bytep row_pointers[MAX_TEXTURE_SIZE];
    png_uint_32 w, h, rowbytes, row;
    int bitdepth, colortype;
    png_structp png_ptr;
    png_infop info_ptr;
    struct pngReadStruct r;

    if( !filename || !pic ) {
        Com_Error( ERR_FATAL, "LoadPNG: NULL" );
    }

    *pic = pixels = NULL;

    rawlength = FS_LoadFile( filename, ( void ** )&rawdata );
    if( !rawdata ) {
        return;
    }

    png_ptr = png_create_read_struct( PNG_LIBPNG_VER_STRING,
        ( png_voidp )filename, png_console_error_fn, png_console_warning_fn );
    if( !png_ptr ) {
        goto fail;
    }

    info_ptr = png_create_info_struct( png_ptr );
    if( !info_ptr ) {
        png_destroy_read_struct( &png_ptr, NULL, NULL );
        goto fail;
    }

    if( setjmp( png_jmpbuf( png_ptr ) ) ) {
        png_destroy_read_struct( &png_ptr, &info_ptr, NULL );
        if( pixels ) {
            IMG_FreePixels( pixels );
        }
        goto fail;
    }

    r.data = rawdata;
    r.maxp = rawdata + rawlength;
    png_set_read_fn( png_ptr, ( png_voidp )&r, png_vfs_read_fn );

    png_read_info( png_ptr, info_ptr );

    if( !png_get_IHDR( png_ptr, info_ptr, &w, &h, &bitdepth, &colortype,
        NULL, NULL, NULL ) )
    {
        png_destroy_read_struct( &png_ptr, &info_ptr, NULL );
        goto fail;
    }

    if( w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE ) {
        Com_EPrintf( "LoadPNG: %s: oversize image dimensions: %lux%lu\n",
            filename, w, h );
        png_destroy_read_struct( &png_ptr, &info_ptr, NULL );
        goto fail;
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

    png_destroy_read_struct( &png_ptr, &info_ptr, NULL );

    *pic = pixels;
    *width = w;
    *height = h;

fail:
    FS_FreeFile( rawdata );
}

static void QDECL png_vfs_write_fn( png_structp png_ptr, png_bytep buf, png_size_t size ) {
    fileHandle_t *f = png_get_io_ptr( png_ptr );
    FS_Write( buf, size, *f );
}

static void QDECL png_vfs_flush_fn( png_structp png_ptr ) {
    //fileHandle_t *f = png_get_io_ptr( png_ptr );
    //FS_Flush( *f );
}

qboolean IMG_WritePNG( const char *filename, const byte *rgb, int width, int height, int compression ) {
    png_structp png_ptr;
    png_infop info_ptr;
    fileHandle_t f;
    qboolean ret = qfalse;
    png_bytepp row_pointers = NULL;
    int row_stride;
    int i;

    FS_FOpenFile( filename, &f, FS_MODE_WRITE );
    if( !f ) {
        Com_DPrintf( "WritePNG: %s: couldn't create file\n", filename );
        return qfalse;
    }

    png_ptr = png_create_write_struct( PNG_LIBPNG_VER_STRING,
        ( png_voidp )filename, png_console_error_fn, png_console_warning_fn );
    if( !png_ptr ) {
        goto fail;
    }

    info_ptr = png_create_info_struct( png_ptr );
    if( !info_ptr ) {
        png_destroy_write_struct( &png_ptr, NULL );
        goto fail;
    }

    if( setjmp( png_jmpbuf( png_ptr ) ) ) {
        png_destroy_write_struct( &png_ptr, &info_ptr );
        goto fail;
    }

    png_set_write_fn( png_ptr, ( png_voidp )&f,
        png_vfs_write_fn, png_vfs_flush_fn );

    png_set_IHDR( png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT );

    clamp( compression, Z_NO_COMPRESSION, Z_BEST_COMPRESSION );
    png_set_compression_level( png_ptr, compression );

    row_pointers = FS_AllocTempMem( sizeof( png_bytep ) * height );
    row_stride = width * 3;
    for( i = 0; i < height; i++ ) {
        row_pointers[i] = ( png_bytep )rgb + ( height - i - 1 ) * row_stride;
    }

    png_set_rows( png_ptr, info_ptr, row_pointers );

    png_write_png( png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, NULL );

    png_destroy_write_struct( &png_ptr, &info_ptr );

    ret = qtrue;

fail:
    if( row_pointers ) {
        FS_FreeFile( row_pointers );
    }
    FS_FCloseFile( f );
    return ret;
}

#endif /* USE_PNG */

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
    int        i;
    image_t    *image;
    int        texels, count;

    Com_Printf( "------------------\n");
    texels = count = 0;

    for( i = 0, image = r_images; i < r_numImages; i++, image++ ) {
        if( !image->registration_sequence )
            continue;
        texels += image->upload_width * image->upload_height;
        switch( image->type ) {
        case it_skin:
            Com_Printf( "M" );
            break;
        case it_sprite:
            Com_Printf( "S" );
            break;
        case it_wall:
            Com_Printf( "W" );
            break;
        case it_pic:
            Com_Printf( "P" );
            break;
        case it_sky:
            Com_Printf( "Y" );
            break;
        case it_charset:
            Com_Printf( "C" );
            break;
        default:
            Com_Printf( " " );
            break;
        }

        Com_Printf( " %4i %4i %s: %s\n",
            image->upload_width,
            image->upload_height,
            ( image->flags & if_paletted ) ? "PAL" : "RGB",
            image->name );
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
image_t *IMG_Find( const char *name, imagetype_t type ) {
    image_t *image;
    byte *pic;
    int width, height;
    char buffer[MAX_QPATH];
    char *ext;
    size_t length;
    unsigned hash, extHash;
    imageflags_t flags;
#if USE_PNG || USE_JPG || USE_TGA
    char *s;
#endif

    if( !name ) {
        Com_Error( ERR_FATAL, "%s: NULL", __func__ );
    }

    length = strlen( name );
    if( length >= MAX_QPATH ) {
        Com_Error( ERR_FATAL, "%s: oversize name", __func__ );
    }

    if( length <= 4 ) {
        return NULL; // must have at least 1 char of base name 
    }

    length -= 4;
    if( name[length] != '.' ) {
        return NULL;
    }
    
    strcpy( buffer, name );
    buffer[length] = 0;

    hash = Com_HashPath( buffer, RIMAGES_HASH );

    if( ( image = IMG_Lookup( buffer, type, hash, length ) ) != NULL ) {
        image->registration_sequence = registration_sequence;
        return image;
    }

    ext = buffer + length;
    Q_strlwr( ext + 1 );
    extHash = MakeLong( '.', ext[1], ext[2], ext[3] );

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
                IMG_LoadPNG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_JPG
            case 'j': // try *.jpg
                strcpy( ext, ".jpg" );
                IMG_LoadJPG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_TGA
            case 't': // try *.tga
                strcpy( ext, ".tga" );
                IMG_LoadTGA( buffer, &pic, &width, &height );
                break;
            }
#endif
            if( pic ) {
                // replacing 8 bit texture with 32 bit texture
                if( extHash == EXTENSION_WAL ) {
                    flags |= if_replace_wal;
                } else if( extHash == EXTENSION_PCX ) {
                    flags |= if_replace_pcx;
                }
                goto create;
            }
        }

        switch( extHash ) {
        case EXTENSION_PNG:
        case EXTENSION_TGA:
        case EXTENSION_JPG:
        case EXTENSION_PCX:
            strcpy( ext, ".pcx" );
            IMG_LoadPCX( buffer, &pic, NULL, &width, &height );
            if( pic ) {
                flags |= if_paletted;
                goto create;
            }
            return NULL;
        case EXTENSION_WAL:
            strcpy( ext, ".wal" );
            if( ( image = IMG_LoadWAL( buffer ) ) != NULL ) {
                goto append;
            }
        }

        return NULL;
    }
#endif

    switch( extHash ) {
    case EXTENSION_PNG:
#if USE_PNG
        // try *.png
        strcpy( ext, ".png" );
        IMG_LoadPNG( buffer, &pic, &width, &height );
        if( pic ) {
            goto create;
        }
#endif
#if USE_JPG || USE_TGA
        for( s = r_texture_formats->string; *s; s++ ) {
            switch( *s ) {
#if USE_JPG
            case 'j': // try *.jpg
                strcpy( ext, ".jpg" );
                IMG_LoadJPG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_TGA
            case 't': // try *.tga
                strcpy( ext, ".tga" );
                IMG_LoadTGA( buffer, &pic, &width, &height );
                break;
            }
#endif
            if( pic ) {
                goto create;
            }
        }
#endif
        // try *.pcx
        strcpy( ext, ".pcx" );
        IMG_LoadPCX( buffer, &pic, NULL, &width, &height );
        if( pic ) {
            flags |= if_paletted;
            goto create;
        }
        return NULL;

    case EXTENSION_TGA:
#if USE_TGA
        strcpy( ext, ".tga" );
        IMG_LoadTGA( buffer, &pic, &width, &height );
        if( pic ) {
            goto create;
        }
#endif

#if USE_PNG || USE_JPG
        for( s = r_texture_formats->string; *s; s++ ) {
            switch( *s ) {
#if USE_PNG
            case 'p': // try *.png
                strcpy( ext, ".png" );
                IMG_LoadPNG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_JPG
            case 'j': // try *.jpg
                strcpy( ext, ".jpg" );
                IMG_LoadJPG( buffer, &pic, &width, &height );
                break;
#endif
            }
            if( pic ) {
                goto create;
            }
        }
#endif
        // try *.pcx
        strcpy( ext, ".pcx" );
        IMG_LoadPCX( buffer, &pic, NULL, &width, &height );
        if( pic ) {
            flags |= if_paletted;
            goto create;
        }
        return NULL;

    case EXTENSION_JPG:
#if USE_JPG
        strcpy( ext, ".jpg" );
        IMG_LoadJPG( buffer, &pic, &width, &height );
        if( pic ) {
            goto create;
        }
#endif

#if USE_PNG || USE_TGA
        for( s = r_texture_formats->string; *s; s++ ) {
            switch( *s ) {
#if USE_PNG
            case 'p': // try *.png
                strcpy( ext, ".png" );
                IMG_LoadPNG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_TGA
            case 't': // try *.tga
                strcpy( ext, ".tga" );
                IMG_LoadTGA( buffer, &pic, &width, &height );
                break;
            }
#endif
            if( pic ) {
                goto create;
            }
        }
#endif

        // try *.pcx
        strcpy( ext, ".pcx" );
        IMG_LoadPCX( buffer, &pic, NULL, &width, &height );
        if( pic ) {
            flags |= if_paletted;
            goto create;
        }
        return NULL;


    case EXTENSION_PCX:
        strcpy( ext, ".pcx" );
        IMG_LoadPCX( buffer, &pic, NULL, &width, &height );
        if( pic ) {
            flags |= if_paletted;
            goto create;
        }

#if USE_PNG || USE_JPG || USE_TGA
        for( s = r_texture_formats->string; *s; s++ ) {
            switch( *s ) {
#if USE_PNG
            case 'p': // try *.png
                strcpy( ext, ".png" );
                IMG_LoadPNG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_JPG
            case 'j': // try *.jpg
                strcpy( ext, ".jpg" );
                IMG_LoadJPG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_TGA
            case 't': // try *.tga
                strcpy( ext, ".tga" );
                IMG_LoadTGA( buffer, &pic, &width, &height );
                break;
            }
#endif
            if( pic ) {
                goto create;
            }
        }
#endif
        return NULL;

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
                IMG_LoadPNG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_JPG
            case 'j': // try *.jpg
                strcpy( ext, ".jpg" );
                IMG_LoadJPG( buffer, &pic, &width, &height );
                break;
#endif
#if USE_TGA
            case 't': // try *.tga
                strcpy( ext, ".tga" );
                IMG_LoadTGA( buffer, &pic, &width, &height );
                break;
            }
#endif
            if( pic ) {
                goto create;
            }
        }
#endif
        return NULL;

    default:
        return NULL;
    }

create:
    image = IMG_Create( buffer, pic, width, height, type, flags );
append:
    List_Append( &r_imageHash[hash], &image->entry );
    return image;
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

/*
================
R_RegisterPic
================
*/
qhandle_t R_RegisterPic( const char *name ) {
    image_t *image;
    char    fullname[MAX_QPATH];

    if( !r_numImages ) {
        return 0;
    }

    if( name[0] == '*' ) {
        image = IMG_Find( name + 1, it_tmp );
    } else if( name[0] == '/' || name[0] == '\\' ) {
        image = IMG_Find( name + 1, it_pic );
    } else {
        Q_concat( fullname, sizeof( fullname ), "pics/", name, NULL );
        COM_DefaultExtension( fullname, ".pcx", sizeof( fullname ) );
        image = IMG_Find( fullname, it_pic );
    }

    if( !image ) {
        return 0;
    }

    return ( image - r_images );
}

/*
================
R_RegisterFont
================
*/
qhandle_t R_RegisterFont( const char *name ) {
    image_t *image;
    char    fullname[MAX_QPATH];

    if( !r_numImages ) {
        return 0;
    }

    if( name[0] == '/' || name[0] == '\\' ) {
        image = IMG_Find( name + 1, it_charset );
    } else {
        Q_concat( fullname, sizeof( fullname ), "pics/", name, NULL );
        COM_DefaultExtension( fullname, ".pcx", sizeof( fullname ) );
        image = IMG_Find( fullname, it_charset );
    }

    if( !image ) {
        return 0;
    }

    return ( image - r_images );
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
    byte pal[768], *src;
    int w, h;

    // get the palette
    if( !IMG_LoadPCX( "pics/colormap.pcx", pic, pal, &w, &h ) ) {
        Com_Error( ERR_FATAL, "Couldn't load pics/colormap.pcx" );
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

