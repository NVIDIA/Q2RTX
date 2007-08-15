/*
Copyright (C) 2003-2006 Andrey Nazarov
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

#include "config.h"

#ifdef USE_PNG
#include <png.h>
#endif

#ifdef USE_JPEG
#ifndef USE_PNG
#include <setjmp.h>
#endif
#include <stdio.h>
#include <jpeglib.h>
#endif

#include "q_shared.h"
#include "com_public.h"
#include "q_files.h"
#include "q_list.h"
#include "r_shared.h"

/*
=================================================================

PCX LOADING

=================================================================
*/


/*
==============
Image_LoadPCX
==============
*/
void Image_LoadPCX( const char *filename, byte **pic, byte *palette, int *width, int *height ) {
	byte	*raw, *end;
	pcx_t	*pcx;
	uint32	x, y, w, h;
	int		len;
	int		dataByte, runLength;
	byte	*out, *pix;

	if( !filename || !pic ) {
		Com_Error( ERR_FATAL, "LoadPCX: NULL" );
	}

	*pic = NULL;

	//
	// load the file
	//
	len = fs.LoadFile( filename, (void **)&raw );
	if( !raw ) {
		return;
	}

	//
	// parse the PCX file
	//
	pcx = (pcx_t *)raw;

    w = LittleShort( pcx->xmax ) + 1;
    h = LittleShort( pcx->ymax ) + 1;

	if( pcx->manufacturer != 0x0a
		|| pcx->version != 5
		|| pcx->encoding != 1
		|| pcx->bits_per_pixel != 8
		|| w > 640
		|| h > 480 )
	{
		Com_WPrintf( "LoadPCX: %s: unsupported format\n", filename );
		return;
	}

#ifdef SOFTWARE_RENDERER
	pix = out = R_Malloc( w * h );
#else
	pix = out = fs.AllocTempMem( w * h );
#endif

	if( palette ) {
        if( len < 768 ) {
            goto malformed;
        }
		memcpy( palette, ( byte * )pcx + len - 768, 768 );
	}

	raw = &pcx->data;
    end = ( byte * )pcx + len;

	for( y = 0; y < h; y++, pix += w ) {
		for( x = 0; x < w; ) {
            if( raw == end ) {
                goto malformed;
            }
			dataByte = *raw++;

			if( ( dataByte & 0xC0 ) == 0xC0 ) {
				runLength = dataByte & 0x3F;
                if( x + runLength > w ) {
                    goto malformed;
                }
                if( raw == end ) {
                    goto malformed;
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

	if( width )
		*width = w;
	if( height )
		*height = h;

    *pic = out;

	fs.FreeFile( pcx );
    return;

malformed:
    Com_WPrintf( "LoadPCX: %s: file was malformed\n", filename );
#ifdef SOFTWARE_RENDERER
    com.Free( out );
#else
    fs.FreeFile( out );
#endif
	fs.FreeFile( pcx );
}

/*
==============
Image_WritePCX
==============
*/
qboolean Image_WritePCX( const char *filename, const byte *data, int width,
    int height, int rowbytes, byte *palette ) 
{
	int			i, j, length;
	pcx_t		*pcx;
	byte		*pack;
    qboolean    ret = qfalse;
    fileHandle_t    f;

	pcx = fs.AllocTempMem( width * height * 2 + 1000 );
	pcx->manufacturer = 0x0a;	// PCX id
	pcx->version = 5;			// 256 color
 	pcx->encoding = 1;		    // uncompressed
	pcx->bits_per_pixel = 8;	// 256 color
	pcx->xmin = 0;
	pcx->ymin = 0;
	pcx->xmax = LittleShort( width - 1 );
	pcx->ymax = LittleShort( height - 1 );
	pcx->hres = LittleShort( width );
	pcx->vres = LittleShort( height );
	memset( pcx->palette, 0, sizeof( pcx->palette ) );
	pcx->color_planes = 1;		// chunky image
	pcx->bytes_per_line = LittleShort( width );
	pcx->palette_type = LittleShort( 2 );		// not a grey scale
	memset( pcx->filler, 0, sizeof( pcx->filler ) );

// pack the image
	pack = &pcx->data;
	for( i = 0; i < height; i++) {
		for( j = 0; j < width; j++) {
			if( ( *data & 0xc0 ) != 0xc0 ) {
				*pack++ = *data++;
            } else {
				*pack++ = 0xc1;
				*pack++ = *data++;
			}
		}
		data += rowbytes - width;
	}
			
// write the palette
	*pack++ = 0x0c;	// palette ID byte
	for( i = 0; i < 768; i++ )
		*pack++ = *palette++;
		
// write output file 
	fs.FOpenFile( filename, &f, FS_MODE_WRITE );
	if( !f ) {
        goto fail;
	}

	length = pack - ( byte * )pcx;
	if( fs.Write( pcx, length, f ) == length ) {
        ret = qtrue;
    }
    
	fs.FCloseFile( f );

fail:
	fs.FreeFile( pcx );
    return ret;
} 

#ifdef TRUECOLOR_RENDERER

/*
=========================================================

TARGA LOADING

=========================================================
*/

static qboolean tga_decode_bgr( byte *data, byte *pixels,
		int columns, int rows, byte *maxp )
{
	int		col, row;
	uint32	*pixbuf;

	for( row = rows - 1; row >= 0; row-- ) {
		pixbuf = ( uint32 * )pixels + row * columns;

		for( col = 0; col < columns; col++ ) {
			*pixbuf++ = MakeColor( data[2], data[1], data[0], 255 );
			data += 3;
		}
	}

	return qtrue;
}

static qboolean tga_decode_bgra( byte *data, byte *pixels,
		int columns, int rows, byte *maxp )
{
	int		col, row;
	uint32	*pixbuf;

	for( row = rows - 1; row >= 0; row-- ) {
		pixbuf = ( uint32 * )pixels + row * columns;

		for( col = 0; col < columns; col++ ) {
			*pixbuf++ = MakeColor( data[2], data[1], data[0], data[3] );
			data += 4;
		}
	}

	return qtrue;
}

static qboolean tga_decode_bgr_flip( byte *data, byte *pixels,
		int columns, int rows, byte *maxp )
{
	int		count;
	uint32	*pixbuf;

	pixbuf = ( uint32 * )pixels;
	count = rows * columns;
	do {
		*pixbuf++ = MakeColor( data[2], data[1], data[0], 255 );
		data += 3;
	} while( --count );

	return qtrue;
}

static qboolean tga_decode_bgra_flip( byte *data, byte *pixels,
		int columns, int rows, byte *maxp )
{
	int		count;
	uint32	*pixbuf;

	pixbuf = ( uint32 * )pixels;
	count = rows * columns;
	do {
		*pixbuf++ = MakeColor( data[2], data[1], data[0], data[3] );
		data += 4;
	} while( --count );

	return qtrue;
}

static qboolean tga_decode_bgr_rle( byte *data, byte *pixels,
		int columns, int rows, byte *maxp )
{
	int col, row;
	uint32 *pixbuf, color;
	byte packetHeader, packetSize;
	int j;

	for( row = rows - 1; row >= 0; row-- ) {
		pixbuf = ( uint32 * )pixels + row * columns;

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

						pixbuf = ( uint32 * )pixels + row * columns;
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
						pixbuf = ( uint32 * )pixels + row * columns;
					}						
				}
			}
		}
breakOut: ;

	}

	return qtrue;

}

static qboolean tga_decode_bgra_rle( byte *data, byte *pixels,
		int columns, int rows, byte *maxp )
{
	int col, row;
	uint32 *pixbuf, color;
	byte packetHeader, packetSize;
	int j;

	for( row = rows - 1; row >= 0; row-- ) {
		pixbuf = ( uint32 * )pixels + row * columns;

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

						pixbuf = ( uint32 * )pixels + row * columns;
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
						pixbuf = ( uint32 * )pixels + row * columns;
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
void Image_LoadTGA( const char *filename, byte **pic,
		int *width, int *height )
{
	byte	*buffer;
	int		length;
	byte		*pixels;
	int 		offset, w, h;
	qboolean (*decode)( byte *, byte *, int, int, byte * );
	int id_length, image_type, pixel_size, attributes, bpp;

	if( !filename || !pic ) {
		Com_Error( ERR_FATAL, "LoadTGA: NULL" );
	}

	*pic = NULL;

	//
	// load the file
	//
	length = fs.LoadFile( filename, ( void ** )&buffer );
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
		Com_WPrintf( "LoadTGA: %s: has strange dimensions: %dx%d\n",
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

	pixels = fs.AllocTempMem( w * h * 4 );

	if( (*decode)( buffer + offset, pixels, w, h, buffer + length ) == qfalse ) {
		fs.FreeFile( pixels );
		goto finish;
	}

	*pic = pixels;
	*width = w;
	*height = h;
	
finish:
	fs.FreeFile( buffer );
}

/*
=========================================================

TARGA WRITING

=========================================================
*/

/*
=================
Image_WriteTGA
=================
*/
qboolean Image_WriteTGA( const char *filename, const byte *bgr,
        int width, int height )
{
	int length;
	fileHandle_t f;
	byte header[TARGA_HEADER_SIZE];
    
	fs.FOpenFile( filename, &f, FS_MODE_WRITE );
	if( !f ) {
		return qfalse;
	}

    memset( &header, 0, sizeof( header ) );
	header[ 2] = 2;		// uncompressed type
	header[12] = width & 255;
    header[13] = width >> 8;
	header[14] = height & 255;
    header[15] = height >> 8;
	header[16] = 24;     // pixel size

	if( fs.Write( &header, sizeof( header ), f ) != sizeof( header ) ) {
        goto fail;
    }

	length = width * height * 3;
	if( fs.Write( bgr, length, f ) != length ) {
        goto fail;
    }
    
	fs.FCloseFile( f );
	return qtrue;
    
fail:
	fs.FCloseFile( f );
	return qfalse;
}


/*
=========================================================

JPEG LOADING

=========================================================
*/

#ifdef USE_JPEG

typedef struct my_error_mgr {
	struct jpeg_error_mgr	pub;
	jmp_buf					setjmp_buffer;
} *my_error_ptr;

METHODDEF( void )my_error_exit( j_common_ptr cinfo ) {
	my_error_ptr myerr = ( my_error_ptr )cinfo->err;

	(*cinfo->err->output_message)( cinfo );

	longjmp( myerr->setjmp_buffer, 1 );
}


METHODDEF( void ) mem_init_source( j_decompress_ptr cinfo ) { }

METHODDEF( boolean ) mem_fill_input_buffer( j_decompress_ptr cinfo ) {
	my_error_ptr jerr = ( my_error_ptr )cinfo->err;

	longjmp( jerr->setjmp_buffer, 1 );
	return TRUE;
}


METHODDEF( void ) mem_skip_input_data( j_decompress_ptr cinfo, long num_bytes ) {
	struct jpeg_source_mgr *src = cinfo->src;
	my_error_ptr jerr = ( my_error_ptr )cinfo->err;
    
	if( src->bytes_in_buffer < num_bytes ) {
		longjmp( jerr->setjmp_buffer, 1 );
	}
	
	src->next_input_byte += ( size_t )num_bytes;
	src->bytes_in_buffer -= ( size_t )num_bytes;
}

METHODDEF( void ) mem_term_source( j_decompress_ptr cinfo ) { }


METHODDEF( void ) jpeg_mem_src( j_decompress_ptr cinfo, byte *memory, int size ) {
	cinfo->src = (struct jpeg_source_mgr *)(*cinfo->mem->alloc_small)( ( j_common_ptr )cinfo, JPOOL_PERMANENT, sizeof( struct jpeg_source_mgr ) );

	cinfo->src->init_source = mem_init_source;
	cinfo->src->fill_input_buffer = mem_fill_input_buffer;
	cinfo->src->skip_input_data = mem_skip_input_data;
	cinfo->src->resync_to_restart = jpeg_resync_to_restart;
	cinfo->src->term_source = mem_term_source;
	cinfo->src->bytes_in_buffer = size;
	cinfo->src->next_input_byte = memory;
}

/*
=================
LoadJPG
=================
*/
void Image_LoadJPG( const char *filename, byte **pic, int *width, int *height ) {
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;
	JSAMPARRAY buffer;
	int row_stride;
	byte *rawdata;
	int rawlength;
	byte *pixels;
	byte *src, *dest;
	int i;

	if( !filename || !pic ) {
		Com_Error( ERR_FATAL, "LoadJPG: NULL" );
	}

	*pic = NULL;
	pixels = NULL;

	rawlength = fs.LoadFile( filename, ( void ** )&rawdata );
	if( !rawdata ) {
		return;
	}

	if( rawlength < 10 || *( uint32 * )( rawdata + 6 ) != MakeLong( 'J', 'F', 'I', 'F' ) ) {
		Com_WPrintf( "LoadJPG: %s: not a valid JPEG file\n", filename );
		fs.FreeFile( rawdata );
		return;
	}

	cinfo.err = jpeg_std_error( &jerr.pub );
	jerr.pub.error_exit = my_error_exit;

	jpeg_create_decompress( &cinfo );
	
	if( setjmp( jerr.setjmp_buffer ) ) {
		Com_WPrintf( "LoadJPG: %s: JPEGLIB signaled an error\n", filename );
		jpeg_destroy_decompress( &cinfo );
		if( pixels ) {
			fs.FreeFile( pixels );
		}
		fs.FreeFile( rawdata );
		return;
	}
 
	jpeg_mem_src( &cinfo, rawdata, rawlength );
	jpeg_read_header( &cinfo, TRUE );
	jpeg_start_decompress( &cinfo );

	if( cinfo.output_components != 3 /*&& cinfo.output_components != 4*/ ) {
		Com_WPrintf( "LoadJPG: %s: unsupported number of color components: %i\n",
				filename, cinfo.output_components );
		jpeg_destroy_decompress( &cinfo );
		fs.FreeFile( rawdata );
		return;
	}

	*width = cinfo.output_width;
	*height = cinfo.output_height;

	pixels = fs.AllocTempMem( cinfo.output_width * cinfo.output_height * 4 );

	row_stride = cinfo.output_width * cinfo.output_components;

	buffer = (*cinfo.mem->alloc_sarray)( ( j_common_ptr )&cinfo, JPOOL_IMAGE, row_stride, 1 );

	dest = pixels;
	while( cinfo.output_scanline < cinfo.output_height ) {
		jpeg_read_scanlines( &cinfo, buffer, 1 );

		src = ( byte * )buffer[0];
		for( i = 0; i < cinfo.output_width; i++ ) {
			*( uint32 * )dest = MakeColor( src[0], src[1], src[2], 255 );
			src += 3;
			dest += 4;
		}
	}

	jpeg_finish_decompress( &cinfo );
	jpeg_destroy_decompress( &cinfo );

	fs.FreeFile( rawdata );

	*pic = pixels;

}

/*
=========================================================

JPEG WRITING

=========================================================
*/

#define OUTPUT_BUF_SIZE		4096

typedef struct my_destination_mgr {
	struct jpeg_destination_mgr pub; /* public fields */

	fileHandle_t	hFile;		/* target stream */
	JOCTET *buffer;		/* start of buffer */
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

	if( fs.Write( dest->buffer, OUTPUT_BUF_SIZE, dest->hFile ) != OUTPUT_BUF_SIZE ) {
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
		if( fs.Write( dest->buffer, remaining, dest->hFile ) != remaining ) {
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
Image_WriteJPG
=================
*/
qboolean Image_WriteJPG( const char *filename, const byte *rgb, int width, int height, int quality ) {
	struct jpeg_compress_struct	cinfo;
	struct my_error_mgr	jerr;
	fileHandle_t hFile;
	JSAMPROW row_pointer[1];
	int	row_stride;

	fs.FOpenFile( filename, &hFile, FS_MODE_WRITE );
	if( !hFile ) {
		Com_DPrintf( "WriteJPG: %s: couldn't create file\n", filename );
		return qfalse;
	}

	cinfo.err = jpeg_std_error( &jerr.pub );
	jerr.pub.error_exit = my_error_exit;

	if( _setjmp( jerr.setjmp_buffer ) ) {
		Com_DPrintf( "WriteJPG: %s: JPEGLIB signaled an error\n", filename );
		jpeg_destroy_compress( &cinfo );
		fs.FCloseFile( hFile );
		return qfalse;
	}

	jpeg_create_compress( &cinfo );

	jpeg_vfs_dst( &cinfo, hFile );

	cinfo.image_width = width;	// image width and height, in pixels
	cinfo.image_height = height;
	cinfo.input_components = 3;		// # of	color components per pixel
	cinfo.in_color_space = JCS_RGB;	// colorspace of input image

	clamp( quality,	0, 100 );

	jpeg_set_defaults( &cinfo );
	jpeg_set_quality( &cinfo, quality, TRUE	);

	jpeg_start_compress( &cinfo, TRUE );

	row_stride = width * 3;	// JSAMPLEs per row in image_buffer

	while( cinfo.next_scanline < cinfo.image_height	) {
		row_pointer[0] = ( byte * )( &rgb[( cinfo.image_height - cinfo.next_scanline - 1 ) * row_stride] );
		jpeg_write_scanlines( &cinfo, row_pointer, 1 );
	}

	jpeg_finish_compress( &cinfo );
	fs.FCloseFile( hFile );

	jpeg_destroy_compress( &cinfo );

	return qtrue;
}

#endif /* USE_JPEG */


#ifdef USE_PNG

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
		size = r->maxp - r->data;
	}
	memcpy( buf, r->data, size );
	r->data += size;
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
void Image_LoadPNG( const char *filename, byte **pic, int *width, int *height ) {
	byte *rawdata;
	int rawlength;
	byte *pixels;
	byte *row_pointers[MAX_TEXTURE_SIZE];
	png_uint_32 w, h, rowbytes, row;
    int bitdepth, colortype;
	png_structp png_ptr;
	png_infop info_ptr;
	struct pngReadStruct r;

	if( !filename || !pic ) {
		Com_Error( ERR_FATAL, "LoadPNG: NULL" );
	}

	*pic = NULL;
	pixels = NULL;

	rawlength = fs.LoadFile( filename, ( void ** )&rawdata );
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
            fs.FreeFile( pixels );
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
	pixels = fs.AllocTempMem( h * rowbytes );

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
	fs.FreeFile( rawdata );
}

#endif /* USE_PNG */

#endif /* TRUECOLOR_RENDERER */

/*
=========================================================

IMAGE MANAGER

=========================================================
*/

image_t		r_images[MAX_RIMAGES];
list_t		r_imageHash[RIMAGES_HASH];
int			r_numImages;

uint32		d_8to24table[256];

#ifdef TRUECOLOR_RENDERER

static cvar_t	*r_override_textures;

/*
================
R_ResampleTexture
================
*/
void R_ResampleTexture( const byte *in, int inwidth, int inheight, byte *out, int outwidth, int outheight ) {
	int		i, j;
	const byte	*inrow1, *inrow2;
	uint32		frac, fracstep;
	uint32		p1[MAX_TEXTURE_SIZE], p2[MAX_TEXTURE_SIZE];
	const byte	*pix1, *pix2, *pix3, *pix4;
	float		heightScale;

	if( outwidth > MAX_TEXTURE_SIZE ) {
		Com_Error( ERR_FATAL, "ResampleTexture: outwidth > %d",
            MAX_TEXTURE_SIZE );
	}

	fracstep = inwidth * 0x10000 / outwidth;

	frac = fracstep >> 2;
	for( i = 0; i < outwidth; i++ ) {
		p1[i] = 4 * ( frac >> 16 );
		frac += fracstep;
	}
	frac = 3 * ( fracstep >> 2 );
	for( i = 0; i < outwidth; i++ ) {
		p2[i] = 4 * ( frac >> 16 );
		frac += fracstep;
	}

	heightScale = ( float )inheight / outheight;
	inwidth <<= 2;
	for( i = 0; i < outheight; i++ ) {
		inrow1 = in + inwidth * ( int )( ( i + 0.25f ) * heightScale );
		inrow2 = in + inwidth * ( int )( ( i + 0.75f ) * heightScale );
		for( j = 0; j < outwidth; j++ ) {
			pix1 = inrow1 + p1[j];
			pix2 = inrow1 + p2[j];
			pix3 = inrow2 + p1[j];
			pix4 = inrow2 + p2[j];
			out[0] = ( pix1[0] + pix2[0] + pix3[0] + pix4[0] ) >> 2;
			out[1] = ( pix1[1] + pix2[1] + pix3[1] + pix4[1] ) >> 2;
			out[2] = ( pix1[2] + pix2[2] + pix3[2] + pix4[2] ) >> 2;
			out[3] = ( pix1[3] + pix2[3] + pix3[3] + pix4[3] ) >> 2;
			out += 4;
		}
	}
}

#endif /* TRUECOLOR_RENDERER */

/*
===============
R_ImageList_f
===============
*/
static void R_ImageList_f( void ) {
	int		i;
	image_t	*image;
	int		texels;

	Com_Printf( "------------------\n");
	texels = 0;

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
		case it_lightmap:
			Com_Printf( "L" );
			break;
		case it_charset:
			Com_Printf( "C" );
			break;
		default:
			Com_Printf( " " );
			break;
		}

		Com_Printf( " %4i %4i %s: %s\n", image->upload_width,
			image->upload_height, ( image->flags & if_paletted ) ? "PAL" : "RGB",
                image->name );
	}
	Com_Printf( "Total texel count (not counting mipmaps): %i\n", texels );
}

static image_t *R_AllocImageInternal( const char *name, uint32 hash ) {
	int i;
	image_t *image;
	
	// find a free image_t slot
	for( i = 0, image = r_images; i < r_numImages; i++, image++ ) {
		if( !image->registration_sequence )
			break;
	}
	
	if( i == r_numImages ) {
		if( r_numImages == MAX_RIMAGES )
			Com_Error( ERR_FATAL, "R_AllocImage: MAX_IMAGES" );
		r_numImages++;
	}

	strcpy( image->name, name );
	List_Append( &r_imageHash[hash], &image->entry );

	image->registration_sequence = registration_sequence;

	return image;
}

/*
===============
R_LookupImage

Finds the given image of the given type.
Case and extension insensitive.
===============
*/
static image_t *R_LookupImage( const char *name, imagetype_t type,
					   uint32 hash, int baselength )
{
	image_t	*image;

	// look for it
    LIST_FOR_EACH( image_t, image, &r_imageHash[hash], entry ) {
		if( image->type != type ) {
			continue;
		}
		if(	!Q_stricmpn( image->name, name, baselength ) ) {
			return image;
		}
	}

	return NULL;
}

image_t *R_AllocImage( const char *name ) {
	char buffer[MAX_QPATH];
	char *ext;
	uint32 hash;
	image_t *image;
	int length;

	if( !name || !name[0] ) {
		Com_Error( ERR_FATAL, "R_AllocImage: NULL" );
	}

	length = strlen( name );
	if( length >= MAX_QPATH ) {
		Com_Error( ERR_FATAL, "R_AllocImage: oversize name: %d chars", length );
	}

	strcpy( buffer, name );

	ext = COM_FileExtension( buffer );
	if( *ext == '.' ) {
		*ext = 0;
	} else {
		ext = NULL;
	}
	hash = Com_HashPath( buffer, RIMAGES_HASH );
	if( ext ) {
		*ext = '.';
	}

	image = R_AllocImageInternal( buffer, hash );

	return image;
}


image_t *R_CreateImage( const char *name, byte *pic, int width, int height,
        imagetype_t type, imageflags_t flags )
{
	image_t *image;

	image = R_AllocImage( name );
	R_LoadImage( image, pic, width, height, type, flags );

	return image;
}


#ifdef TRUECOLOR_RENDERER

/*
===============
R_FindImage

Finds or loads the given image (8 or 32 bit)
===============
*/
image_t	*R_FindImage( const char *name, imagetype_t type ) {
	image_t	*image;
	byte	*pic;
	int		width, height;
	char	buffer[MAX_QPATH];
	char	*ext;
	int		length;
	uint32	hash, extHash;
	imageflags_t flags;

	if( !name || !name[0] ) {
		Com_Error( ERR_FATAL, "R_FindImage: NULL" );
	}

	length = strlen( name );
	if( length >= MAX_QPATH ) {
		Com_Error( ERR_FATAL, "R_FindImage: oversize name: %d chars", length );
	}

	if( length <= 4 ) {
		/* must have at least 1 char of basename 
		 * and 4 chars of extension part */
		return NULL;
	}

	length -= 4;
	if( name[length] != '.' ) {
		return NULL;
	}
	
	strcpy( buffer, name );
	buffer[length] = 0;

	hash = Com_HashPath( buffer, RIMAGES_HASH );

	if( ( image = R_LookupImage( buffer, type, hash, length ) ) != NULL ) {
		image->registration_sequence = registration_sequence;
		return image;
	}

	ext = buffer + length;
	Q_strlwr( ext + 1 );
	extHash = MakeLong( '.', ext[1], ext[2], ext[3] );

	//
	// load the pic from disk
	//
	pic = NULL;
	image = NULL;
	flags = 0;

	if( r_override_textures->integer ) {
#ifdef USE_PNG
		// try *.png
		strcpy( ext, ".png" );
		Image_LoadPNG( buffer, &pic, &width, &height );

		if( !pic )
#endif
		{
			// try *.tga
			strcpy( ext, ".tga" );
			Image_LoadTGA( buffer, &pic, &width, &height );

#ifdef USE_JPEG
			if( !pic ) {
				// try *.jpg
				strcpy( ext, ".jpg" );
				Image_LoadJPG( buffer, &pic, &width, &height );
			}
#endif
		}

		if( pic ) {
		    // replacing 8 bit texture with 32 bit texture
			if( extHash == EXTENSION_WAL ) {
				flags |= if_replace_wal;
			} else if( extHash == EXTENSION_PCX ) {
				flags |= if_replace_pcx;
            }
			goto load;
		}

        switch( extHash ) {
        case EXTENSION_PNG:
        case EXTENSION_TGA:
        case EXTENSION_JPG:
        case EXTENSION_PCX:
			strcpy( ext, ".pcx" );
			Image_LoadPCX( buffer, &pic, NULL, &width, &height );
			if( pic ) {
				flags |= if_paletted;
				goto load;
			}
			return NULL;
		case EXTENSION_WAL:
			strcpy( ext, ".wal" );
			image = R_LoadWal( buffer );
			return image;
		}

		return NULL;
	}

	switch( extHash ) {
	case EXTENSION_PNG:
#ifdef USE_PNG
		// try *.png
		strcpy( ext, ".png" );
		Image_LoadPNG( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}
#endif

		// try *.tga
		strcpy( ext, ".tga" );
		Image_LoadTGA( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}

#ifdef USE_JPEG
		// try *.jpg
		strcpy( ext, ".jpg" );
		Image_LoadJPG( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}
#endif

		// try *.pcx
		strcpy( ext, ".pcx" );
		Image_LoadPCX( buffer, &pic, NULL, &width, &height );
		if( pic ) {
			flags |= if_paletted;
			goto load;
		}
		return NULL;

	case EXTENSION_TGA:
		strcpy( ext, ".tga" );
		Image_LoadTGA( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}

#ifdef USE_PNG
		// try *.png
		strcpy( ext, ".png" );
		Image_LoadPNG( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}
#endif

#ifdef USE_JPEG
		// try *.jpg
		strcpy( ext, ".jpg" );
		Image_LoadJPG( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}
#endif

		// try *.pcx
		strcpy( ext, ".pcx" );
		Image_LoadPCX( buffer, &pic, NULL, &width, &height );
		if( pic ) {
			flags |= if_paletted;
			goto load;
		}
		return NULL;

	case EXTENSION_JPG:
#ifdef USE_JPEG
		strcpy( ext, ".jpg" );
		Image_LoadJPG( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}
#endif

#ifdef USE_PNG
		// try *.png
		strcpy( ext, ".png" );
		Image_LoadPNG( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}
#endif

		// try *.tga
		strcpy( ext, ".tga" );
		Image_LoadTGA( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}

		// try *.pcx
		strcpy( ext, ".pcx" );
		Image_LoadPCX( buffer, &pic, NULL, &width, &height );
		if( pic ) {
			flags |= if_paletted;
			goto load;
		}
		return NULL;


	case EXTENSION_PCX:
		strcpy( ext, ".pcx" );
		Image_LoadPCX( buffer, &pic, NULL, &width, &height );
		if( pic ) {
			flags |= if_paletted;
			goto load;
		}

#ifdef USE_PNG
		// try *.png
		strcpy( ext, ".png" );
		Image_LoadPNG( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}
#endif

		// try *.tga
		strcpy( ext, ".tga" );
		Image_LoadTGA( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}

#ifdef USE_JPEG
		// try *.jpg
		strcpy( ext, ".jpg" );
		Image_LoadJPG( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}
#endif

		return NULL;

	case EXTENSION_WAL:
		strcpy( ext, ".wal" );
		image = R_LoadWal( buffer );
		if( image ) {
			return image;
		}

		// FIXME: no way to figure correct texture dimensions here

#ifdef USE_PNG
		// try *.png
		strcpy( ext, ".png" );
		Image_LoadPNG( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}
#endif

		// try *.tga
		strcpy( ext, ".tga" );
		Image_LoadTGA( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}

#ifdef USE_JPEG
		// try *.jpg
		strcpy( ext, ".jpg" );
		Image_LoadJPG( buffer, &pic, &width, &height );
		if( pic ) {
			goto load;
		}
#endif

		return NULL;

	default:
		return NULL;
		
	}

load:
	image = R_AllocImageInternal( buffer, hash );
	R_LoadImage( image, pic, width, height, type, flags );
	return image;
}

#else /* TRUECOLOR_RENDERER */

/*
===============
R_FindImage

Finds or loads the given image (8 bit)
===============
*/
image_t	*R_FindImage( const char *name, imagetype_t type ) {
	image_t	*image;
	byte	*pic;
	int		width, height;
	char	buffer[MAX_QPATH];
	char	*ext;
	int		length;
	uint32	hash, extHash;

	if( !name || !name[0] ) {
		Com_Error( ERR_FATAL, "R_FindImage: NULL" );
	}

	length = strlen( name );
	if( length >= MAX_QPATH ) {
		Com_Error( ERR_FATAL, "R_FindImage: oversize name: %d chars", length );
	}

	if( length <= 4 ) {
		/* must have at least 1 char of basename 
		 * and 4 chars of extension part */
		return NULL;
	}

	length -= 4;
	if( name[length] != '.' ) {
		return NULL;
	}
	
	strcpy( buffer, name );
	Q_strlwr( buffer );
	buffer[length] = 0;

	hash = Com_HashPath( buffer, RIMAGES_HASH );

	if( ( image = R_LookupImage( buffer, type, hash, length ) ) != NULL ) {
		image->registration_sequence = registration_sequence;
		return image;
	}

	ext = buffer + length;
	extHash = MakeLong( '.', ext[1], ext[2], ext[3] );

	*ext = '.';

	//
	// load the pic from disk
	//
	if( extHash == EXTENSION_JPG || extHash == EXTENSION_TGA || extHash == EXTENSION_PNG ) {
		strcpy( ext, ".pcx" );
		extHash = EXTENSION_PCX;
	}
	if( extHash == EXTENSION_PCX ) {
		Image_LoadPCX( buffer, &pic, NULL, &width, &height );
		if( !pic ) {
			return NULL;
		}
		image = R_AllocImageInternal( buffer, hash );
		R_LoadImage( image, pic, width, height, type, if_paletted );
		return image;
	}

	if( extHash == EXTENSION_WAL ) {
		image = R_LoadWal( buffer );
		return image;
	}

	return NULL;
}

#endif /* !TRUECOLOR_RENDERER */

/*
================
R_FreeUnusedImages

Any image that was not touched on this registration sequence
will be freed.
================
*/
void R_FreeUnusedImages( void ) {
	image_t	*image, *last;

	last = r_images + r_numImages;
	for( image = r_images; image != last; image++ ) {
		if( image->registration_sequence == registration_sequence ) {
#ifdef SOFTWARE_RENDERER
			Com_PageInMemory( image->pixels[0], image->width * image->height * VID_BYTES );
#endif
			continue;		// used this sequence
		}
		if( !image->registration_sequence )
			continue;		// free image_t slot
		if( image->type == it_pic || image->type == it_charset )
			continue;		// don't free pics
		if( image->flags & if_auto ) {
			if( image->type != it_lightmap ) {
				continue;	// never free r_notexture or particle texture
							// always free lightmaps
			}
		}

		// delete it from hash table
		List_Remove( &image->entry );

		// free it
		R_FreeImage( image );

        memset( image, 0, sizeof( *image ) );
	}
}

void R_FreeAllImages( void ) {
	image_t *image, *last;
    int i;
	
	last = r_images + r_numImages;
	for( image = r_images; image != last; image++ ) {
		if( !image->registration_sequence )
			continue;		// free image_t slot
		// free it
		R_FreeImage( image );
		
        memset( image, 0, sizeof( *image ) );
	}

	Com_DPrintf( "R_FreeAllImages: %i images freed\n", r_numImages );
	
	r_numImages = 0;
    for( i = 0; i < RIMAGES_HASH; i++ ) {
    	List_Init( &r_imageHash[i] );
    }
}

/*
===============
R_GetPalette
===============
*/
void R_GetPalette( byte **dest ) {
	int		i;
	byte	*pic, *src;
    byte    palette[768];
	int		width, height;

	/* get the palette */
	Image_LoadPCX( "pics/colormap.pcx", &pic, palette, &width, &height );
	if( !pic ) {
		Com_Error( ERR_FATAL, "Couldn't load pics/colormap.pcx" );
    }

	src = palette;
	for( i = 0; i < 255; i++ ) {
		d_8to24table[i] = MakeColor( src[0], src[1], src[2], 255 );
		src += 3;
	}

	/* 255 is transparent*/
	d_8to24table[i] = MakeColor( src[0], src[1], src[2], 0 );

	if( dest ) {
		*dest = R_Malloc( width * height );
        memcpy( *dest, pic, width * height );
	}

	fs.FreeFile( pic );
}

void R_InitImageManager( void ) {
    int i;

#ifdef TRUECOLOR_RENDERER
	r_override_textures = cvar.Get( "r_override_textures", "0", CVAR_ARCHIVE|CVAR_LATCHED );
#endif
	cmd.AddCommand( "imagelist", R_ImageList_f );

    for( i = 0; i < RIMAGES_HASH; i++ ) {
    	List_Init( &r_imageHash[i] );
    }
}

void R_ShutdownImageManager( void ) {
	cmd.RemoveCommand( "imagelist" );
}

