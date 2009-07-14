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

static inline void _GL_StretchPic(
    float x, float y, float w, float h,
    float s1, float t1, float s2, float t2,
    const byte *color, int texnum, int flags )
{
    vec_t *dst_vert;
    uint32_t *dst_color;
    
    if( tess.numverts + 4 > TESS_MAX_VERTICES ||
        ( tess.numverts && tess.texnum[0] != texnum ) )
    {
        GL_Flush2D();
    }

    tess.texnum[0] = texnum;

    dst_vert = tess.vertices + tess.numverts * 4;
    Vector4Set( dst_vert,      x,     y,     s1, t1 );
    Vector4Set( dst_vert +  4, x + w, y,     s2, t1 );
    Vector4Set( dst_vert +  8, x + w, y + h, s2, t2 );
    Vector4Set( dst_vert + 12, x,     y + h, s1, t2 );

    dst_color = ( uint32_t * )tess.colors + tess.numverts;
    dst_color[0] = *( const uint32_t * )color;
    dst_color[1] = *( const uint32_t * )color;
    dst_color[2] = *( const uint32_t * )color;
    dst_color[3] = *( const uint32_t * )color;
    
    if( flags & if_transparent ) {
        if( ( flags & if_paletted ) && draw.scale == 1 ) {
            tess.flags |= 1;
        } else {
            tess.flags |= 2;
        }
    }
    if( color[3] != 255 ) {
        tess.flags |= 2;
    }

    tess.numverts += 4;
}

#define GL_StretchPic(x,y,w,h,s1,t1,s2,t2,color,image) \
    _GL_StretchPic(x,y,w,h,s1,t1,s2,t2,color,(image)->texnum,(image)->flags)

void GL_Blend( void ) {
    color_t color = {
        glr.fd.blend[0] * 255,
        glr.fd.blend[1] * 255,
        glr.fd.blend[2] * 255,
        glr.fd.blend[3] * 255
    };

    _GL_StretchPic( glr.fd.x, glr.fd.y, glr.fd.width, glr.fd.height, 0, 0, 1, 1,
        color, TEXNUM_WHITE, 0 );
}

void R_SetColor( int flags, const color_t color ) {
    draw.flags &= ~DRAW_COLOR_MASK;

    if( flags == DRAW_COLOR_CLEAR ) {
        *( uint32_t * )draw.color = *( uint32_t * )colorWhite;
        return;
    }
    if( flags == DRAW_COLOR_ALPHA ) {
        draw.color[3] = *( float * )color * 255;
    } else if( flags == DRAW_COLOR_INDEXED ) {
        *( uint32_t * )draw.color = d_8to24table[ *( uint32_t * )color & 255 ];
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

void R_SetClipRect( int flags, const clipRect_t *clip ) {
    clipRect_t rc;
    float scale;

    if( ( draw.flags & DRAW_CLIP_MASK ) == flags ) {
        return;
    }

    GL_Flush2D();

    if( flags == DRAW_CLIP_DISABLED ) {
        qglDisable( GL_SCISSOR_TEST );
        draw.flags &= ~DRAW_CLIP_MASK;
        return;
    }

    scale = 1 / draw.scale;

    rc.left = 0;
    rc.top = 0;
    if( flags & DRAW_CLIP_LEFT ) {
        rc.left = clip->left * scale;
        if( rc.left < 0 ) {
            rc.left = 0;
        }
    }
    if( flags & DRAW_CLIP_TOP ) {
        rc.top = clip->top * scale;
        if( rc.top < 0 ) {
            rc.top = 0;
        }
    }

    rc.right = gl_config.vidWidth;
    rc.bottom = gl_config.vidHeight;
    if( flags & DRAW_CLIP_RIGHT ) {
        rc.right = clip->right * scale;
        if( rc.right > gl_config.vidWidth ) {
            rc.right = gl_config.vidWidth;
        }
    }
    if( flags & DRAW_CLIP_BOTTOM ) {
        rc.bottom = clip->bottom * scale;
        if( rc.bottom > gl_config.vidHeight ) {
            rc.bottom = gl_config.vidHeight;
        }
    }

    if( rc.right < rc.left ) {
        rc.right = rc.left;
    }
    if( rc.bottom < rc.top ) {
        rc.bottom = rc.top;
    }

    qglEnable( GL_SCISSOR_TEST );
    qglScissor( rc.left, gl_config.vidHeight - rc.bottom,
        rc.right - rc.left, rc.bottom - rc.top );
    draw.flags = ( draw.flags & ~DRAW_CLIP_MASK ) | flags;
}

void R_SetScale( float *scale ) {
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

qboolean R_GetPicSize( int *w, int *h, qhandle_t pic ) {
    image_t *image = IMG_ForHandle( pic );

    if( w ) {
        *w = image->width;
    }
    if( h ) {
        *h = image->height;
    }
    return image->flags & if_transparent;
}

void R_DrawStretchPicST( int x, int y, int w, int h, float s1, float t1,
        float s2, float t2, qhandle_t pic )
{
    /* TODO: scrap support */
    GL_StretchPic( x, y, w, h, s1, t1, s2, t2, draw.color, IMG_ForHandle( pic ) );
}

void R_DrawStretchPic( int x, int y, int w, int h, qhandle_t pic ) {
    image_t *image = IMG_ForHandle( pic );

    GL_StretchPic( x, y, w, h, image->sl, image->tl, image->sh, image->th,
        draw.color, image );
}

void R_DrawPic( int x, int y, qhandle_t pic ) {
    image_t *image = IMG_ForHandle( pic );

    GL_StretchPic( x, y, image->width, image->height,
        image->sl, image->tl, image->sh, image->th, draw.color, image );
}

#define DIV64 ( 1.0f / 64.0f )

void R_TileClear( int x, int y, int w, int h, qhandle_t pic ) {
    GL_StretchPic( x, y, w, h, x * DIV64, y * DIV64,
        ( x + w ) * DIV64, ( y + h ) * DIV64, colorWhite, IMG_ForHandle( pic ) );
}

void R_DrawFill( int x, int y, int w, int h, int c ) {
    _GL_StretchPic( x, y, w, h, 0, 0, 1, 1, ( byte * )&d_8to24table[c & 255],
        TEXNUM_WHITE, 0 );
}

void R_DrawFillEx( int x, int y, int w, int h, const color_t color ) {
    _GL_StretchPic( x, y, w, h, 0, 0, 1, 1, color, TEXNUM_WHITE, 0 );
}

void R_FadeScreen( void ) {
}

void R_DrawChar( int x, int y, int flags, int ch, qhandle_t font ) {
    float s, t;
    
    ch &= 255;
    s = ( ch & 15 ) * 0.0625f;
    t = ( ch >> 4 ) * 0.0625f;

    GL_StretchPic( x, y, 8, 8, s, t, s + 0.0625f, t + 0.0625f,
        draw.color, IMG_ForHandle( font ) );
}

int R_DrawString( int x, int y, int flags, size_t maxChars,
                 const char *string, qhandle_t font )
{
    byte c;
    float s, t;
    image_t *image;
    color_t colors[2];
    int mask, altmask = 0;

    image = IMG_ForHandle( font );

    if( flags & UI_ALTCOLOR ) {
        altmask |= 128;
    }
    mask = altmask;

    *( uint32_t * )colors[0] = *( uint32_t * )draw.color;
    *( uint32_t * )colors[1] = MakeColor( 255, 255, 255, draw.color[3] );
    while( maxChars-- && *string ) {
        if( Q_IsColorString( string ) ) {
            c = string[1];
            if( c == COLOR_ALT ) {
                mask |= 128;
            } else if( c == COLOR_RESET ) {
                *( uint32_t * )colors[0] = *( uint32_t * )draw.color;
                mask = altmask;
            } else {
                VectorCopy( colorTable[ ColorIndex( c ) ], colors[0] );
                mask = 0;
            }
            string += 2;
            continue;
        }
        
        c = *string++;
        if( ( c & 127 ) == 32 ) {
            x += 8;
            continue;
        }

        c |= mask;
        s = ( c & 15 ) * 0.0625f;
        t = ( c >> 4 ) * 0.0625f;

        GL_StretchPic( x, y, 8, 8, s, t, s + 0.0625f, t + 0.0625f,
            colors[ ( c >> 7 ) & 1 ], image );
        x += 8;
    }
    return x;
}

#ifdef _DEBUG

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
        tmp = R_RegisterFont( "conchars" );
        if(!tmp) return;
        r_charset = IMG_ForHandle( tmp );
    }
    
    string = buffer;
    while( *string ) {
        c = *string++;

        s = ( c & 15 ) * 0.0625f;
        t = ( c >> 4 ) * 0.0625f;

        GL_StretchPic( x, y, 8, 8, s, t, s + 0.0625f, t + 0.0625f,
            colorWhite, r_charset );
        x += 8;
    }
}


void Draw_Stats( void ) {
    int x = 10, y = 10;
        
    Draw_Stringf( x, y, "Nodes visible  : %i", c.nodesVisible ); y += 10;
    Draw_Stringf( x, y, "Nodes culled   : %i", c.nodesCulled ); y += 10;
    Draw_Stringf( x, y, "Faces drawn    : %i", c.facesDrawn ); y += 10;
    if( c.facesCulled ) {
        Draw_Stringf( x, y, "Faces culled   : %i", c.facesCulled ); y += 10;
    }
    if( c.boxesCulled ) {
        Draw_Stringf( x, y, "Boxes culled   : %i", c.boxesCulled ); y += 10;
    }
    if( c.spheresCulled ) {
        Draw_Stringf( x, y, "Spheres culled : %i", c.spheresCulled ); y += 10;
    }
    if( c.rotatedBoxesCulled ) {
        Draw_Stringf( x, y, "RtBoxes culled : %i", c.rotatedBoxesCulled ); y += 10;
    }
    Draw_Stringf( x, y, "Tris drawn   : %i", c.trisDrawn ); y += 10;
    Draw_Stringf( x, y, "Tex switches : %i", c.texSwitches ); y += 10;
    if( c.batchesDrawn ) {
        Draw_Stringf( x, y, "Batches drawn: %i", c.batchesDrawn ); y += 10;
        Draw_Stringf( x, y, "Faces / batch: %i", c.facesDrawn / c.batchesDrawn );
        y += 10;
        Draw_Stringf( x, y, "Tris / batch : %i", c.trisDrawn / c.batchesDrawn );
        y += 10;
    }
}

#endif
