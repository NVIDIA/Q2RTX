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
#include "gl_arbfp.h"

glState_t gls;

void GL_BindTexture( int texnum ) {
    if( gls.texnum[gls.tmu] == texnum ) {
        return;
    }
    if( !gl_bind->integer ) {
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

	*( uint32_t * )draw.color = *( uint32_t * )colorWhite;

	if( draw.flags & DRAW_CLIP_MASK ) {
		qglDisable( GL_SCISSOR_TEST );
	}

	draw.flags = 0;

	qglMatrixMode( GL_MODELVIEW );
	qglLoadIdentity();

    GL_Bits( GLS_DEPTHTEST_DISABLE );
    GL_CullFace( GLS_CULL_DISABLE );
}

void GL_Setup3D( void ) {
    GLdouble xmin, xmax, ymin, ymax, aspect;
	int yb = glr.fd.y + glr.fd.height;

	qglViewport( glr.fd.x, gl_config.vidHeight - yb,
        glr.fd.width, glr.fd.height );

	qglMatrixMode( GL_PROJECTION );
	qglLoadIdentity();

    ymax = gl_znear->value * tan( glr.fd.fov_y * M_PI / 360.0 );
    ymin = -ymax;

    aspect = ( GLdouble )glr.fd.width / glr.fd.height;
    xmin = ymin * aspect;
    xmax = ymax * aspect;

    qglFrustum( xmin, xmax, ymin, ymax, gl_znear->value, gl_zfar->value );
	
    qglMatrixMode( GL_MODELVIEW );
	qglLoadIdentity();

	qglRotatef( -90, 1, 0, 0 ); /* put z axis up */
	qglRotatef(  90, 0, 0, 1 ); /* put y axis west, x axis north */
	qglRotatef( -glr.fd.viewangles[ROLL],  1, 0, 0 );
	qglRotatef( -glr.fd.viewangles[PITCH], 0, 1, 0 );
	qglRotatef( -glr.fd.viewangles[YAW],   0, 0, 1 );
	qglTranslatef( -glr.fd.vieworg[0], -glr.fd.vieworg[1], -glr.fd.vieworg[2] );

    AngleVectors( glr.fd.viewangles,
        glr.viewaxis[0], glr.viewaxis[1], glr.viewaxis[2] );
	VectorInverse( glr.viewaxis[1] );

	glr.scroll = -64 * ( ( glr.fd.time / 40.0f ) - ( int )( glr.fd.time / 40.0f ) );
	if( glr.scroll == 0 )
		glr.scroll = -64.0f;

#if 0
    {
        vec4_t ambient = {0,0,0,0}, material={1,1,1,1};

        qglLightModelfv( GL_LIGHT_MODEL_AMBIENT, ambient );
        qglLightModelf( GL_LIGHT_MODEL_TWO_SIDE, 1 );
        qglMaterialfv( GL_BACK, GL_AMBIENT_AND_DIFFUSE, material );

    }

    if( glr.fd.num_dlights > 8 ) {
        glr.fd.num_dlights = 8;
    }
    for( i = 0; i < glr.fd.num_dlights; i++ ) {
        dlight_t *l = &glr.fd.dlights[i];
        qglLightfv( GL_LIGHT0 + i, GL_POSITION, l->origin );
        qglLightfv( GL_LIGHT0 + i, GL_DIFFUSE, l->color );
        qglLightf( GL_LIGHT0 + i, GL_QUADRATIC_ATTENUATION,  0.00001f );
        qglLightf( GL_LIGHT0 + i, GL_LINEAR_ATTENUATION,  0.005f );
        qglEnable( GL_LIGHT0 + i );
    }
    for( ; i < 8; i++ ) {
        qglDisable( GL_LIGHT0 + i );
    }
#endif

    GL_Bits( GLS_DEFAULT );
    GL_CullFace( GLS_CULL_FRONT );

	qglClear( GL_DEPTH_BUFFER_BIT );
}

void GL_SetDefaultState( void ) {
	qglDrawBuffer( GL_BACK );
	qglClearColor( 0, 0, 0, 1 );
	qglClearDepth( 1 );
	qglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	qglEnable( GL_DEPTH_TEST );
	qglDepthFunc( GL_LEQUAL );
	qglDepthRange( 0, 1 );
	qglDepthMask( GL_TRUE );
	qglDisable( GL_BLEND );
	qglDisable( GL_ALPHA_TEST ); 
	qglAlphaFunc( GL_GREATER, 0.666f );
	qglHint( GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST );
    qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

	qglEnableClientState( GL_VERTEX_ARRAY );
    qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
    
	GL_SelectTMU( 0 );
    GL_BindTexture( 0 );
	qglEnable( GL_TEXTURE_2D );
    GL_Bits( GLS_DEFAULT );
}

void GL_EnableOutlines( void ) {
    qglDisable( GL_TEXTURE_2D );
    qglPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
    qglDisable( GL_DEPTH_TEST );
    qglColor4f( 1, 1, 1, 1 );
}

void GL_DisableOutlines( void ) {
   qglEnable( GL_DEPTH_TEST );
   qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
   qglEnable( GL_TEXTURE_2D );
}

void GL_EnableWarp( void ) {
    vec4_t param;

    qglEnable( GL_FRAGMENT_PROGRAM_ARB );
    qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, gl_static.prog_warp );
    param[0] = glr.fd.time * 0.125f;
    param[1] = glr.fd.time * 0.125f;
    param[2] = param[3] = 0;
    qglProgramLocalParameter4fvARB( GL_FRAGMENT_PROGRAM_ARB, 0, param );
}

void GL_DisableWarp( void ) {
    qglDisable( GL_FRAGMENT_PROGRAM_ARB );
}

void GL_InitPrograms( void ) {
    if( !qglProgramStringARB ) {
        return;
    }
    qglGenProgramsARB( 1, &gl_static.prog_warp );
    qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, gl_static.prog_warp );
    qglProgramStringARB( GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
        sizeof( gl_prog_warp ) - 1, gl_prog_warp );
    //Com_Printf( "%s\n", qglGetString( GL_PROGRAM_ERROR_STRING_ARB ) );

    qglGenProgramsARB( 1, &gl_static.prog_light );
    qglBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, gl_static.prog_light );
    qglProgramStringARB( GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
        sizeof( gl_prog_light ) - 1, gl_prog_light );

    GL_ShowErrors( __func__ );
}

void GL_ShutdownPrograms( void ) {
    if( !qglProgramStringARB ) {
        return;
    }
    qglDeleteProgramsARB( 1, &gl_static.prog_warp );
}


