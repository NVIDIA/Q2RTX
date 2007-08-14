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

glState_t gls;

void GL_BindTexture( int texnum ) {
    if( gls.texnum[gls.tmu] == texnum ) {
        return;
    }
    
	qglBindTexture( GL_TEXTURE_2D, texnum );
    c.texSwitches++;
    gls.texnum[gls.tmu] = texnum;
}

void GL_SelectTMU( int tmu ) {
    if( gls.tmu == tmu ) {
        return;
    }

    if( tmu < 0 || tmu >= gl_static.numTextureUnits ) {
        Com_Error( ERR_FATAL, "GL_SelectTMU: bad tmu %d", tmu );
    }
    
    qglActiveTextureARB( GL_TEXTURE0_ARB + tmu );
    qglClientActiveTextureARB( GL_TEXTURE0_ARB + tmu );
    
    gls.tmu = tmu;
}

void GL_TexEnv( GLenum texenv ) {
    if( gls.texenv[gls.tmu] == texenv ) {
        return;
    }
    
    switch( texenv ) {
    case GL_REPLACE:
        qglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
        break;
    case GL_MODULATE:
        qglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
        break;
    case GL_BLEND:
        qglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND );
        break;
    case GL_ADD:
        qglTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD );
        break;
    default:
        Com_Error( ERR_FATAL, "GL_TexEnv: bad texenv" );
        break;
    }

    gls.texenv[gls.tmu] = texenv;
}

void GL_CullFace( glCullFace_t cull ) {
    if( gls.cull == cull ) {
        return;
    }
    switch( cull ) {
    case GLS_CULL_DISABLE:
        qglDisable( GL_CULL_FACE );
        break;
    case GLS_CULL_FRONT:
        qglEnable( GL_CULL_FACE );
        qglCullFace( GL_FRONT );
        break;
    case GLS_CULL_BACK:
        qglEnable( GL_CULL_FACE );
        qglCullFace( GL_BACK );
        break;
    default:
        Com_Error( ERR_FATAL, "GL_CullFace: bad cull" );
        break;
    }

    gls.cull = cull;
}

void GL_Bits( glStateBits_t bits ) {
    glStateBits_t diff = bits ^ gls.bits;

    if( !diff ) {
        return;
    }

    if( diff & GLS_BLEND_MASK ) {
		if( bits & GLS_BLEND_MASK ) {
			qglEnable( GL_BLEND );
			if( bits & GLS_BLEND_BLEND ) {
        		qglBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
			} else if( bits & GLS_BLEND_ADD ) {
				qglBlendFunc( GL_SRC_ALPHA, GL_ONE );
			} else if( bits & GLS_BLEND_MODULATE ) {
				qglBlendFunc( GL_DST_COLOR, GL_ONE );
			}
        } else {
            qglDisable( GL_BLEND );
        }
    }
    
    if( diff & GLS_DEPTHMASK_FALSE ) {
        if( bits & GLS_DEPTHMASK_FALSE ) {
            qglDepthMask( GL_FALSE );
        } else {
            qglDepthMask( GL_TRUE );
        }
    }
    
    if( diff & GLS_DEPTHTEST_DISABLE ) {
        if( bits & GLS_DEPTHTEST_DISABLE ) {
            qglDisable( GL_DEPTH_TEST );
        } else {
            qglEnable( GL_DEPTH_TEST );
        }
    }

	if( diff & GLS_ALPHATEST_ENABLE ) {
        if( bits & GLS_ALPHATEST_ENABLE ) {
            qglEnable( GL_ALPHA_TEST );
        } else {
            qglDisable( GL_ALPHA_TEST );
        }
    }
    
    gls.bits = bits;
}

void GL_Setup2D( void ) {
	qglViewport( 0, 0, gl_config.vidWidth, gl_config.vidHeight );

	qglMatrixMode( GL_PROJECTION );
	qglLoadIdentity();

	qglOrtho( 0, gl_config.vidWidth, gl_config.vidHeight, 0, -1, 1 );
	draw.scale = 1;

	*( uint32 * )draw.color = *( uint32 * )colorWhite;

	if( draw.flags & DRAW_CLIP_MASK ) {
		qglDisable( GL_SCISSOR_TEST );
	}

	draw.flags = 0;

	qglMatrixMode( GL_MODELVIEW );
	qglLoadIdentity();

    GL_Bits( GLS_DEPTHTEST_DISABLE );
    GL_CullFace( GLS_CULL_DISABLE );
}

static inline void MYgluPerspective( GLdouble fovy, GLdouble aspect,
		     GLdouble zNear, GLdouble zFar )
{
   GLdouble xmin, xmax, ymin, ymax;

   ymax = zNear * tan( fovy * M_PI / 360.0 );
   ymin = -ymax;

   xmin = ymin * aspect;
   xmax = ymax * aspect;

   qglFrustum( xmin, xmax, ymin, ymax, zNear, zFar );
}

void GL_Setup3D( void ) {
    float screenAspect;
	int yb;

	yb = glr.fd.y + glr.fd.height;
	qglViewport( glr.fd.x, gl_config.vidHeight - yb,
        glr.fd.width, glr.fd.height );

    screenAspect = ( float )glr.fd.width / glr.fd.height;
	qglMatrixMode( GL_PROJECTION );
	qglLoadIdentity();
	MYgluPerspective( glr.fd.fov_y, screenAspect,
        gl_znear->value, gl_zfar->value );
	
	qglMatrixMode( GL_MODELVIEW );
	qglLoadIdentity();

	qglRotatef( -90, 1, 0, 0 ); /* put z axis up */
	qglRotatef(  90, 0, 0, 1 ); /* put y axis west, x axis north */
	qglRotatef( -glr.fd.viewangles[ROLL],  1, 0, 0 );
	qglRotatef( -glr.fd.viewangles[PITCH], 0, 1, 0 );
	qglRotatef( -glr.fd.viewangles[YAW],   0, 0, 1 );
	qglTranslatef( -glr.fd.vieworg[0], -glr.fd.vieworg[1], -glr.fd.vieworg[2] );

    GL_Bits( GLS_DEFAULT );
    GL_CullFace( GLS_CULL_FRONT );

	qglClear( GL_DEPTH_BUFFER_BIT );
}

