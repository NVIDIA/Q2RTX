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

#include "gl_local.h"

drawStatic_t draw;

void Draw_SetColor( uint32 flags, const color_t color ) {
	draw.flags &= ~DRAW_COLOR_MASK;

	if( flags == DRAW_COLOR_CLEAR ) {
        *( uint32 * )draw.color = *( uint32 * )colorWhite;
		return;
	}
	if( flags == DRAW_COLOR_ALPHA ) {
        draw.color[3] = *( float * )color * 255;
	} else if( flags == DRAW_COLOR_INDEXED ) {
		*( uint32 * )draw.color = d_8to24table[ *( uint32 * )color & 255 ];
	} else {
		if( flags & DRAW_COLOR_RGB ) {
			VectorCopy( color, draw.color );
		}
		if( flags & DRAW_COLOR_ALPHA ) {
			draw.color[3] = color[3];
		}
	}

	draw.flags |= flags;
}

void Draw_SetClipRect( uint32 flags, const clipRect_t *clip ) {
	clipRect_t rc;

	if( ( draw.flags & DRAW_CLIP_MASK ) == flags ) {
		return;
	}

	GL_Flush2D();

	if( flags == DRAW_CLIP_DISABLED ) {
		qglDisable( GL_SCISSOR_TEST );
		draw.flags &= ~DRAW_CLIP_MASK;
		return;
	}

	rc.left = 0;
	rc.top = 0;
	if( flags & DRAW_CLIP_LEFT ) {
		rc.left = clip->left;
	}
	if( flags & DRAW_CLIP_TOP ) {
		rc.top = clip->top;
	}

	rc.right = gl_config.vidWidth;
	rc.bottom = gl_config.vidHeight;
	if( flags & DRAW_CLIP_RIGHT ) {
		rc.right = clip->right;
	}
	if( flags & DRAW_CLIP_BOTTOM ) {
		rc.bottom = clip->bottom;
	}

	qglEnable( GL_SCISSOR_TEST );
	qglScissor( rc.left, gl_config.vidHeight - rc.bottom,
		rc.right - rc.left, rc.bottom - rc.top );
	draw.flags = ( draw.flags & ~DRAW_CLIP_MASK ) | flags;
}

void Draw_SetScale( float *scale ) {
	float f = scale ? *scale : 1;

	if( draw.scale == f ) {
		return;
	}
	
	GL_Flush2D();

	qglMatrixMode( GL_PROJECTION );
	qglLoadIdentity();

	qglOrtho( 0, gl_config.vidWidth * f, gl_config.vidHeight * f, 0, -1, 1 );

	draw.scale = f;
}

void Draw_GetPicSize( int *w, int *h, qhandle_t hPic ) {
	image_t *image;

	image = R_ImageForHandle( hPic );
	*w = image->upload_width;
	*h = image->upload_height;
}

void Draw_GetFontSize( int *w, int *h, qhandle_t hFont ) {
    *w = 8;
    *h = 8;
}

qhandle_t GL_RegisterFont( const char *name ) {
	image_t	*image;
	char	fullname[MAX_QPATH];

	if( name[0] != '/' && name[0] != '\\' ) {
		Com_sprintf( fullname, sizeof( fullname ), "pics/%s", name );
		COM_DefaultExtension( fullname, ".pcx", sizeof( fullname ) );
		image = R_FindImage( fullname, it_charset );
	} else {
		image = R_FindImage( name + 1, it_charset );
	}

	if( !image ) {
		return 0;
	}

	return ( image - r_images );
}

void Draw_StretchPicST( int x, int y, int w, int h, float s1, float t1,
        float s2, float t2, qhandle_t hPic )
{
	/* TODO: scrap support */
    GL_StretchPic( x, y, w, h, s1, t1, s2, t2, draw.color,
		R_ImageForHandle( hPic ) );
}

void Draw_StretchPic( int x, int y, int w, int h, qhandle_t hPic ) {
	image_t *image;

	image = R_ImageForHandle( hPic );
    GL_StretchPic( x, y, w, h, image->sl, image->tl, image->sh, image->th,
		draw.color, image );
}

void Draw_Pic( int x, int y, qhandle_t hPic ) {
	image_t *image;

	image = R_ImageForHandle( hPic );
    GL_StretchPic( x, y, image->width, image->height,
		image->sl, image->tl, image->sh, image->th,
			draw.color, image );
}

#define DOSTRETCH do {							\
			tbyte = src[u >> 16];				\
            *dst++ = gl_static.palette[tbyte];	\
			u += ustep;							\
		} while( 0 )

void Draw_StretchRaw( int x, int y, int w, int h, int cols,
        int rows, const byte *data )
{
	uint32	resampled[256*256];
	int width, height;
	const byte *src;
	byte tbyte;
	uint32 *dst;
	int u, v, ustep, vstep;

	vstep = rows * 0x10000 / 256;
	ustep = cols * 0x10000 / 256;

	dst = resampled;
	v = 0;
	height = 256;
	do {
		src = &data[( v >> 16 ) * cols];

		u = 0;
		width = 256/8;
		do {
			DOSTRETCH;
			DOSTRETCH;
			DOSTRETCH;
			DOSTRETCH;
			DOSTRETCH;
			DOSTRETCH;
			DOSTRETCH;
			DOSTRETCH;
		} while( --width );
		
		v += vstep;
	} while( --height );

	qglBindTexture( GL_TEXTURE_2D, 0 );
	qglTexImage2D( GL_TEXTURE_2D, 0, gl_tex_solid_format, 256, 256, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, resampled );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );

	qglBegin( GL_QUADS );
	qglTexCoord2f( 0, 0 ); qglVertex2f( x, y );
	qglTexCoord2f( 1, 0 ); qglVertex2f( x + w, y );
	qglTexCoord2f( 1, 1 ); qglVertex2f( x + w, y + h );
	qglTexCoord2f( 0, 1 ); qglVertex2f( x, y + h );
	qglEnd();
}

#define DIV64	( 1.0f / 64.0f )

void Draw_TileClear( int x, int y, int w, int h, qhandle_t hPic ) {
	image_t *image;

	if( !( image = R_ImageForHandle( hPic ) ) ) {
		GL_StretchPic( x, y, w, h, 0, 0, 1, 1, colorBlack, r_whiteimage );
		return;
	}
	GL_StretchPic( x, y, w, h, x * DIV64, y * DIV64, ( x + w ) * DIV64, ( y + h ) * DIV64, colorWhite, image );
}

void Draw_Fill( int x, int y, int w, int h, int c ) {
    GL_StretchPic( x, y, w, h, 0, 0, 1, 1, ( byte * )&d_8to24table[c & 255],
            r_whiteimage );
}

void Draw_FillEx( int x, int y, int w, int h, const color_t color ) {
    GL_StretchPic( x, y, w, h, 0, 0, 1, 1, color, r_whiteimage );
}

void Draw_FadeScreen( void ) {
}

void Draw_Char( int x, int y, uint32 flags, int ch, qhandle_t hFont ) {
	float s, t;
    
    ch &= 255;
    s = ( ch & 15 ) * 0.0625f;
	t = ( ch >> 4 ) * 0.0625f;

    GL_StretchPic( x, y, 8, 8, s, t, s + 0.0625f, t + 0.0625f,
            draw.color, R_ImageForHandle( hFont ) );
}

void Draw_String( int x, int y, uint32 flags, int maxChars,
        const char *string, qhandle_t hFont )
{
    byte c;
	float s, t;
	image_t *image;
    color_t colors[2];
	int mask;

	image = R_ImageForHandle( hFont );

	mask = 0;
	if( flags & UI_ALTCOLOR ) {
		mask |= 128;
	}

    *( uint32 * )colors[0] = *( uint32 * )draw.color;
	*( uint32 * )colors[1] = MakeColor( 255, 255, 255, draw.color[3] );
	while( maxChars-- && *string ) {
        if( Q_IsColorString( string ) ) {
            c = string[1];
			if( c == COLOR_ALT ) {
				mask |= 128;
			} else if( c == COLOR_RESET ) {
                *( uint32 * )colors[0] = *( uint32 * )draw.color;
				mask = 0;
				if( flags & UI_ALTCOLOR ) {
					mask |= 128;
				}
            } else {
                VectorCopy( colorTable[ ColorIndex( c ) ], colors[0] );
				mask = 0;
            }
            string += 2;
            continue;
        }
        
		c = *string++;
		c |= mask;
        
        if( ( c & 127 ) == 32 ) {
            x += 8;
            continue;
        }

		s = ( c & 15 ) * 0.0625f;
		t = ( c >> 4 ) * 0.0625f;

        GL_StretchPic( x, y, 8, 8, s, t, s + 0.0625f, t + 0.0625f,
			colors[ ( c >> 7 ) & 1 ], image );
		x += 8;
    }
}

image_t *r_charset;

void Draw_Stringf( int x, int y, const char *fmt, ... ) {
	va_list argptr;
	char buffer[MAX_STRING_CHARS];
	char *string;
	byte c;
	float s, t;

	va_start( argptr, fmt );
	Q_vsnprintf( buffer, sizeof( buffer ), fmt, argptr );
	va_end( argptr );

    if( !r_charset ) {
        qhandle_t tmp;
        tmp = GL_RegisterFont( "conchars" );
        if(!tmp) return;
        r_charset = R_ImageForHandle( tmp );
    }
	
	string = buffer;
	while( *string ) {
		c = *string++;

		s = ( c & 15 ) * 0.0625f;
		t = ( c >> 4 ) * 0.0625f;

#if 0
		glBegin( GL_QUADS );
			glTexCoord2f( s, t );
			glVertex2i( x, y );
			glTexCoord2f( s + 0.0625f, t );
			glVertex2i( x + 8, y );
			glTexCoord2f( s + 0.0625f, t + 0.0625f );
			glVertex2i( x + 8, y + 16 );
			glTexCoord2f( s, t + 0.0625f );
			glVertex2i( x, y + 16 );
		glEnd();
#endif
        GL_StretchPic( x, y, 8, 16, s, t, s + 0.0625f, t + 0.0625f,
                colorWhite, r_charset );
		x += 8;
	}

}

#if 0

void Draw_FPS( int x, int y ) {
	int time;
	static int realtime;
	static int frameTimes[4] = { 1 };
	static int current;
	int fps;

	time = sys.Milliseconds();
	frameTimes[current & 3] = time - realtime;
	current++;
	realtime = time;

	fps = 4000 / ( frameTimes[0] + frameTimes[1] +
            frameTimes[2] + frameTimes[3] );
	Draw_Stringf( x, y, "FPS: %i", fps );
}

#else

#define FPS_APERTURE    9

int SortCmp( const void *v1, const void *v2 ) {
    int i1 = *( int * )v1;
    int i2 = *( int * )v2;

    if( i1 < i2 ) {
        return -1;
    }
    if( i1 > i2 ) {
        return 1;
    }
    return 0;
}

void Draw_FPS( int x, int y ) {
	int time;
	static int realtime;
	static int frameTimes[FPS_APERTURE];
	static int current;
	int buffer[FPS_APERTURE];
	int fps, i;

	time = sys.Milliseconds();
	frameTimes[current % FPS_APERTURE] = time - realtime;
	current++;
	realtime = time;

    for( i = 0; i < FPS_APERTURE; i++ ) {
        buffer[i] = frameTimes[i];
    }

    qsort( buffer, FPS_APERTURE, sizeof( buffer[0] ), SortCmp );
    if( buffer[4] ) {
	    fps = 1000 / buffer[4];
    	Draw_Stringf( x, y, "FPS: %i", fps );
    }
}
#endif


void Draw_Stats( void ) {
	int x, y;
	//const char *orderStr[2] = { "unordered", "inorder" };
	//const char *enableStr[2] = { "disabled", "enabled" };
	//const char *algStr[2] = { "mergesort", "quicksort" };
    statCounters_t st = c;
   
#if 0
    GL_Flush2D();
   // GL_StretchPic( 0, 0, gl_config.vidWidth, gl_config.vidHeight, -0.5f, -0.5f, 1.5f, 1.5f,
    GL_StretchPic( 0, 0, gl_config.vidWidth, gl_config.vidHeight, 0, 0, 1, 1,
            colorWhite, r_beamtexture );
   // qglBlendFunc( GL_ONE, GL_ONE );
    //
#endif
        
	y = 16;
	x = 16;
    
	Draw_FPS( gl_config.vidWidth - 80, y );
//    qglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    //GL_Flush2D();
	
	Draw_Stringf( x, y, "Nodes visible  : %i", st.nodesVisible ); y += 16;
	Draw_Stringf( x, y, "Nodes culled   : %i", st.nodesCulled ); y += 16;
	//Draw_String( x, y, "Nodes drawn  : %i", c_nodesDrawn ); y += 16;
	//Draw_String( x, y, "Faces marked : %i", c_facesMarked ); y += 16;
	Draw_Stringf( x, y, "Faces drawn    : %i", st.facesDrawn ); y += 16;
    if( st.facesCulled ) {
    	Draw_Stringf( x, y, "Faces culled   : %i", st.facesCulled ); y += 16;
    }
    if( st.boxesCulled ) {
    	Draw_Stringf( x, y, "Boxes culled   : %i", st.boxesCulled ); y += 16;
    }
	if( st.spheresCulled ) {
    	Draw_Stringf( x, y, "Spheres culled : %i", st.spheresCulled ); y += 16;
    }
	if( st.rotatedBoxesCulled ) {
		Draw_Stringf( x, y, "RtBoxes culled : %i", st.rotatedBoxesCulled ); y += 16;
    }
	Draw_Stringf( x, y, "Tris drawn   : %i", st.trisDrawn ); y += 16;
	Draw_Stringf( x, y, "Tex switches : %i", st.texSwitches ); y += 16;
    if( st.batchesDrawn ) {
    	Draw_Stringf( x, y, "Batches drawn: %i", st.batchesDrawn ); y += 16;
    	Draw_Stringf( x, y, "Faces / batch: %i", st.facesDrawn / st.batchesDrawn );
        y += 16;
    	Draw_Stringf( x, y, "Tris / batch : %i", st.trisDrawn / st.batchesDrawn );
        y += 16;
    }
	
	y += 16;
    /*
	Draw_String( x, y, "Drawing order: %s", orderStr[r_drawOrder] ); y += 16;
	Draw_String( x, y, "Depth test   : %s", enableStr[r_depthTest] ); y += 16;
	Draw_String( x, y, "Faces culling: %s", enableStr[r_cullFace] ); y += 16;
	Draw_String( x, y, "Faces sorting: %s", enableStr[enableSort] ); y += 16;
	Draw_String( x, y, "Algorithm    : %s", algStr[doMergeSort] ); y += 16;
    */
	
	y += 16;
}

