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

/*
 * gl_main.c
 * 
 */

#include "gl_local.h"

#ifdef USE_PNG
#include <png.h>
#endif

#if 0
#ifdef USE_JPEG
#ifndef USE_PNG
#include <setjmp.h>
#endif
#include <stdio.h>
#include <jpeglib.h>
#endif
#endif


/* declare imports for this module */
cmdAPI_t	cmd;
cvarAPI_t	cvar;
fsAPI_t		fs;
commonAPI_t	com;
sysAPI_t	sys;
videoAPI_t	video;

glRefdef_t glr;
glStatic_t gl_static;
glconfig_t gl_config;
statCounters_t  c;

int registration_sequence;

cvar_t *gl_partscale;
#ifdef USE_JPEG
cvar_t *gl_screenshot_quality;
#endif
cvar_t *gl_znear;
cvar_t *gl_zfar;
cvar_t *gl_modulate;
cvar_t *gl_log;
cvar_t *gl_drawworld;
cvar_t *gl_drawentities;
cvar_t *gl_showtris;
cvar_t *gl_cull_nodes;
cvar_t *gl_cull_models;
cvar_t *gl_showstats;
cvar_t *gl_clear;
cvar_t *gl_novis;
cvar_t *gl_lockpvs;
cvar_t *gl_sort;
cvar_t *gl_primitives;
cvar_t *gl_sort;
cvar_t *gl_subdivide;
cvar_t *gl_fastsky;
cvar_t *gl_dynamic;
cvar_t *gl_fullbright;
cvar_t *gl_hwgamma;
cvar_t *gl_fullscreen;

static void GL_SetupFrustum( void ) {
    cplane_t *f;
    vec3_t forward, left, up;
    vec_t fovSin, fovCos, angle;

    angle = DEG2RAD( glr.fd.fov_x / 2 );
    fovSin = sin( angle );
    fovCos = cos( angle );
    
    VectorScale( glr.viewaxis[0], fovSin, forward );
    VectorScale( glr.viewaxis[1], fovCos, left );

    /* right side */
    f = &glr.frustumPlanes[0];
    VectorAdd( forward, left, f->normal );
    f->dist = DotProduct( glr.fd.vieworg, f->normal );
    SetPlaneSignbits( f );
	SetPlaneType( f );
    
    /* left side */
    f = &glr.frustumPlanes[1];
    VectorSubtract( forward, left, f->normal );
    f->dist = DotProduct( glr.fd.vieworg, f->normal );
    SetPlaneSignbits( f );
	SetPlaneType( f );
    
    angle = DEG2RAD( glr.fd.fov_y / 2 );
    fovSin = sin( angle );
    fovCos = cos( angle );

    VectorScale( glr.viewaxis[0], fovSin, forward );
    VectorScale( glr.viewaxis[2], fovCos, up );
    
    /* up side */
    f = &glr.frustumPlanes[2];
    VectorAdd( forward, up, f->normal );
    f->dist = DotProduct( glr.fd.vieworg, f->normal );
    SetPlaneSignbits( f );
	SetPlaneType( f );
    
    /* down side */
    f = &glr.frustumPlanes[3];
    VectorSubtract( forward, up, f->normal );
    f->dist = DotProduct( glr.fd.vieworg, f->normal );
    SetPlaneSignbits( f );
	SetPlaneType( f );

}

static void GL_Blend( void ) {
    color_t color;
    
    if( glr.fd.blend[3] == 0 ) {
        return;
    }

    color[0] = glr.fd.blend[0] * 255;
    color[1] = glr.fd.blend[1] * 255;
    color[2] = glr.fd.blend[2] * 255;
    color[3] = glr.fd.blend[3] * 255;

    GL_StretchPic( 0, 0, gl_config.vidWidth, gl_config.vidHeight, 0, 0, 1, 1,
        color, r_whiteimage );
}

glCullResult_t GL_CullBox( vec3_t bounds[2] ) {
    int i, bits;
	glCullResult_t cull;

	if( !gl_cull_models->integer ) {
		return CULL_IN;
	}
    
	cull = CULL_IN;
    for( i = 0; i < 4; i++ ) {
        bits = BoxOnPlaneSide( bounds[0], bounds[1], &glr.frustumPlanes[i] );
        if( bits == BOX_BEHIND ) {
            return CULL_OUT;
        }
		if( bits != BOX_INFRONT ) {
			cull = CULL_CLIP;
		}
    }

    return cull;
}

glCullResult_t GL_CullSphere( const vec3_t origin, float radius ) {
	float dist;
	cplane_t *p;
	int i;
	glCullResult_t cull;

	if( !gl_cull_models->integer ) {
		return CULL_IN;
	}

	cull = CULL_IN;
	for( i = 0, p = glr.frustumPlanes; i < 4; i++, p++ ) {
		dist = DotProduct( origin, p->normal ) - p->dist;
		if( dist < -radius ) {
			return CULL_OUT;
		}
		if( dist <= radius ) {
			cull = CULL_CLIP;
		}
	}

	return cull;
}

glCullResult_t GL_CullLocalBox( const vec3_t origin, vec3_t bounds[2] ) {
	vec3_t points[8];
	cplane_t *p;
	int i, j;
	vec_t dot;
	qboolean infront;
	glCullResult_t cull;

	if( !gl_cull_models->integer ) {
		return CULL_IN;
	}

	for( i = 0; i < 8; i++ ) {
		VectorCopy( origin, points[i] );
		VectorMA( points[i], bounds[(i>>0)&1][0], glr.entaxis[0], points[i] );
		VectorMA( points[i], bounds[(i>>1)&1][1], glr.entaxis[1], points[i] );
		VectorMA( points[i], bounds[(i>>2)&1][2], glr.entaxis[2], points[i] );
	}

	cull = CULL_IN;
	for( i = 0, p = glr.frustumPlanes; i < 4; i++, p++ ) {
		infront = qfalse;
		for( j = 0; j < 8; j++ ) {
			dot = DotProduct( points[j], p->normal );
			if( dot >= p->dist ) {
				infront = qtrue;
				if( cull == CULL_CLIP ) {
					break;
				}
			} else {
				cull = CULL_CLIP;
				if( infront ) {
					break;
				}
			}
		}
		if( !infront ) {
			return CULL_OUT;
		}
	}

	return cull;
}

void GL_DrawBox( const vec3_t origin, vec3_t bounds[2] ) {
	static int indices[2][4] = {
		{ 0, 1, 3, 2 },
		{ 4, 5, 7, 6 }
	};
	vec3_t points[8];
	int i, j;

	qglDisable( GL_TEXTURE_2D );
    GL_TexEnv( GL_REPLACE );
    qglDisable( GL_DEPTH_TEST );
    qglColor4f( 1, 1, 1, 1 );

	for( i = 0; i < 8; i++ ) {
		VectorCopy( origin, points[i] );
		VectorMA( points[i], bounds[(i>>0)&1][0], glr.entaxis[0], points[i] );
		VectorMA( points[i], bounds[(i>>1)&1][1], glr.entaxis[1], points[i] );
		VectorMA( points[i], bounds[(i>>2)&1][2], glr.entaxis[2], points[i] );
	}
    
	for( i = 0; i < 2; i++ ) {
		qglBegin( GL_LINE_LOOP );
		for( j = 0; j < 4; j++ ) {
			qglVertex3fv( points[ indices[i][j] ] );
		}
		qglEnd();
	}

	qglBegin( GL_LINES );
	for( i = 0; i < 4; i++ ) {
		qglVertex3fv( points[ i     ] );
		qglVertex3fv( points[ i + 4 ] );
	}
	qglEnd();
	
	qglEnable( GL_DEPTH_TEST );
    qglEnable( GL_TEXTURE_2D );

}

static void GL_DrawSpriteModel( model_t *model ) {
	vec3_t point;
	entity_t *e = glr.ent;
	spriteFrame_t *frame;
	image_t *image;
	int idx, bits;
	float alpha;

	idx = e->frame % model->numFrames;
	frame = &model->sframes[idx];
	image = frame->image;

	GL_TexEnv( GL_MODULATE );

	alpha = 1;
	bits = GLS_DEFAULT;
	if( e->flags & RF_TRANSLUCENT ) {
		alpha = e->alpha;
		bits = GLS_BLEND_BLEND;
	}

	GL_Bits( bits );

	qglColor4f( 1, 1, 1, alpha );

	GL_BindTexture( image->texnum );

	qglBegin( GL_QUADS );

	qglTexCoord2f( 0, 1 );
	VectorMA( e->origin, -frame->y, glr.viewaxis[2], point );
	VectorMA( point, frame->x, glr.viewaxis[1], point );
	qglVertex3fv( point );

	qglTexCoord2f( 0, 0 );
	VectorMA( e->origin, frame->height - frame->y, glr.viewaxis[2], point );
	VectorMA( point, frame->x, glr.viewaxis[1], point );
	qglVertex3fv( point );

	qglTexCoord2f( 1, 0 );
	VectorMA( e->origin, frame->height - frame->y, glr.viewaxis[2], point );
	VectorMA( point, frame->x - frame->width, glr.viewaxis[1], point );
	qglVertex3fv( point );

	qglTexCoord2f( 1, 1 );
	VectorMA( e->origin, -frame->y, glr.viewaxis[2], point );
	VectorMA( point, frame->x - frame->width, glr.viewaxis[1], point );
	qglVertex3fv( point );

	qglEnd();
}

static void GL_DrawNullModel( void ) {
	vec3_t point;

	qglDisable( GL_TEXTURE_2D );
    //qglDisable( GL_DEPTH_TEST );
	qglBegin( GL_LINES );

	qglColor3f( 1, 0, 0 );
	qglVertex3fv( glr.ent->origin );
	VectorMA( glr.ent->origin, 16, glr.entaxis[0], point );
	qglVertex3fv( point );

	qglColor3f( 0, 1, 0 );
	qglVertex3fv( glr.ent->origin );
	VectorMA( glr.ent->origin, 16, glr.entaxis[1], point );
	qglVertex3fv( point );

	qglColor3f( 0, 0, 1 );
	qglVertex3fv( glr.ent->origin );
	VectorMA( glr.ent->origin, 16, glr.entaxis[2], point );
	qglVertex3fv( point );

	qglEnd();
	//qglEnable( GL_DEPTH_TEST );
	qglEnable( GL_TEXTURE_2D );
}

static void GL_DrawEntities( int mask ) {
	entity_t *ent, *last;
	modelType_t *model;

	if( !gl_drawentities->integer ) {
		return;
	}

	last = glr.fd.entities + glr.fd.num_entities;
	for( ent = glr.fd.entities; ent != last; ent++ ) {
		if( ent->flags & RF_BEAM ) {
			/* beams are drawn elsewhere in single batch */
            glr.num_beams++;
			continue;
		}
		if( ( ent->flags & RF_TRANSLUCENT ) != mask ) {
			continue;
		}

		glr.ent = ent;
		if( ent->angles[0] || ent->angles[1] || ent->angles[2] ) {
			glr.entrotated = qtrue;
			AngleVectors( ent->angles, glr.entaxis[0], glr.entaxis[1], glr.entaxis[2] );
			VectorInverse( glr.entaxis[1] );
		} else {
			glr.entrotated = qfalse;
			VectorSet( glr.entaxis[0], 1, 0, 0 );
			VectorSet( glr.entaxis[1], 0, 1, 0 );
			VectorSet( glr.entaxis[2], 0, 0, 1 );
		}

		model = GL_ModelForHandle( ent->model );
		if( !model ) {
			GL_DrawNullModel();
			continue;
		}

		switch( *model ) {
		case MODEL_NULL:
			GL_DrawNullModel();
			break;
		case MODEL_BSP:
			GL_DrawBspModel( ( bspSubmodel_t * )model );
			break;
		case MODEL_ALIAS:
			GL_DrawAliasModel( ( model_t * )model );
			break;
		case MODEL_SPRITE:
			GL_DrawSpriteModel( ( model_t * )model );
			break;
		default:
			Com_Error( ERR_FATAL, "GL_DrawEntities: bad model type: %u", *model );
			break;
		}
	}
}

static char *GL_ErrorString( GLenum err ) {
	char *str;

#define MapError( x )	case x: str = #x; break;

	switch( err ) {
		MapError( GL_NO_ERROR )
		MapError( GL_INVALID_ENUM )
		MapError( GL_INVALID_VALUE )
		MapError( GL_INVALID_OPERATION )
		MapError( GL_STACK_OVERFLOW )
		MapError( GL_STACK_UNDERFLOW )
		MapError( GL_OUT_OF_MEMORY )
		default: str = "UNKNOWN ERROR";
	}

#undef MapError

	return str;
}

static void GL_RenderFrame( refdef_t *fd ) {
	GLenum err;

	GL_Flush2D();

    if( !r_world.name[0] && !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
        Com_Error( ERR_FATAL, "GL_RenderView: NULL worldmodel" );
    }

	glr.drawframe++;

    glr.fd = *fd;
    glr.num_beams = 0;

    if( !gl_dynamic->integer ) {
        glr.fd.num_dlights = 0;
    }

    AngleVectors( glr.fd.viewangles, glr.viewaxis[0], glr.viewaxis[1], glr.viewaxis[2] );
	VectorInverse( glr.viewaxis[1] );

	glr.scroll = -64 * ( ( glr.fd.time / 40.0f ) - ( int )( glr.fd.time / 40.0f ) );
	if( glr.scroll == 0 )
		glr.scroll = -64.0f;

    GL_Setup3D();
    
    if( gl_cull_nodes->integer ) {
        GL_SetupFrustum();
    }
    
	if( !( glr.fd.rdflags & RDF_NOWORLDMODEL ) && gl_drawworld->integer ) {
        GL_DrawWorld();
    }

	GL_DrawEntities( 0 );

	GL_DrawBeams();

    GL_DrawParticles();

	GL_DrawEntities( RF_TRANSLUCENT );

	GL_DrawAlphaFaces();
    
	/* go back into 2D mode */
    GL_Setup2D();

    GL_Blend();

    while( ( err = qglGetError() ) != GL_NO_ERROR ) {
		Com_EPrintf( "GL_RenderFrame: %s\n", GL_ErrorString( err ) );
    }
}

static void GL_BeginFrame( void ) {
	GLenum err;

	if( gl_log->integer ) {
		QGL_LogNewFrame();
	}
    
    memset( &c, 0, sizeof( c ) );
    
    GL_Setup2D();

    if( gl_clear->integer ) {
	    qglClear( GL_COLOR_BUFFER_BIT );
    }

    while( ( err = qglGetError() ) != GL_NO_ERROR ) {
		Com_EPrintf( "GL_BeginFrame: %s\n", GL_ErrorString( err ) );
    }
}

static void GL_EndFrame( void ) {
	GLenum err;

    if( gl_showstats->integer ) {
        Draw_Stats();
    }
    GL_Flush2D();

	if( gl_log->modified ) {
		QGL_EnableLogging( gl_log->integer );
		gl_log->modified = qfalse;
	}
	
    while( ( err = qglGetError() ) != GL_NO_ERROR ) {
		Com_EPrintf( "GL_EndFrame: %s\n", GL_ErrorString( err ) );
    }
    video.EndFrame();
}

/* 
============================================================================== 
 
						SCREEN SHOTS 
 
============================================================================== 
*/

#define SCREENSHOTS_DIRECTORY	"screenshots"

#ifdef USE_JPEG

/* 
================== 
GL_ScreenShotJPEG
================== 
*/  
static qboolean GL_ScreenShotJPEG( const char *filename ) {
 	byte	*buffer;
	int		ret;

	buffer = fs.AllocTempMem( gl_config.vidWidth * gl_config.vidHeight * 3 );

	qglReadPixels( 0, 0, gl_config.vidWidth, gl_config.vidHeight, GL_RGB,
        GL_UNSIGNED_BYTE, buffer );

	ret = Image_WriteJPG( filename, buffer, gl_config.vidWidth,
        gl_config.vidHeight, gl_screenshot_quality->integer );

	fs.FreeFile( buffer );
	
	return ret;
}

#endif

/* 
================== 
GL_ScreenShotTGA
================== 
*/  
static qboolean GL_ScreenShotTGA( const char *filename ) {
	byte	*buffer;
	int		ret;

	buffer = fs.AllocTempMem( gl_config.vidWidth * gl_config.vidHeight * 3 );

	qglReadPixels( 0, 0, gl_config.vidWidth, gl_config.vidHeight, GL_BGR,
        GL_UNSIGNED_BYTE, buffer );

	ret = Image_WriteTGA( filename, buffer, gl_config.vidWidth,
        gl_config.vidHeight );

	fs.FreeFile( buffer );
	
	return ret;
}

/* 
================== 
GL_ScreenShot_f
================== 
*/
static void GL_ScreenShot_f( void )  {
	char		picname[MAX_QPATH]; 
	char		checkname[MAX_QPATH];
	int			i;
	qboolean	screenshotJPEG;
	qboolean	silent;
	qboolean	ret;
	char		*ext;

	if( cmd.Argc() > 3 ) {
		Com_Printf( "Usage: %s [name] [silent]\n", cmd.Argv( 0 ) );
		return;
	}

#ifdef USE_JPEG
	if( !Q_stricmp( cmd.Argv( 0 ), "screenshotJPEG" ) ) {
		screenshotJPEG = qtrue;
		ext = ".jpg";
	} else
#endif
	{
		screenshotJPEG = qfalse;
		ext = ".tga";
	}

	silent = qfalse;
	picname[0] = 0;
	for( i = 1; i < cmd.Argc(); i++ ) {
		if( !Q_stricmp( cmd.Argv( i ), "silent" ) ) {
			silent = qtrue;
			continue;
		}

		if( picname[0] ) {
			break;
		}

		Q_strncpyz( picname, cmd.Argv( i ), sizeof( picname ) );

	}

// 
// find a file name to save it to 
// 
	if( !picname[0] ) {
		for( i = 0; i < 1000; i++ ) {
			Com_sprintf( picname, sizeof( picname ), "quake%03d%s", i, ext );
			Com_sprintf( checkname, sizeof( checkname ), SCREENSHOTS_DIRECTORY"/%s", picname );

			if( fs.LoadFile( checkname, NULL ) == -1 ) {
				break;	// file doesn't exist
			}
	
		}

		if( i == 1000 )  {
			if( !silent ) {
				Com_WPrintf( "Couldn't create a screenshot, all slots full\n" );
			}
			return;
 		}
	} else {
		COM_DefaultExtension( picname, ext, sizeof( picname ) );
		Com_sprintf( checkname, sizeof( checkname ), SCREENSHOTS_DIRECTORY"/%s", picname );
	}

#ifdef USE_JPEG
	if( screenshotJPEG ) {
		ret = GL_ScreenShotJPEG( checkname );
	} else
#endif
	{
		ret = GL_ScreenShotTGA( checkname );
	}

	if( silent ) {
		return;
	}

	if( ret ) {
		Com_Printf( "Wrote %s\n", picname );
	} else {
		Com_WPrintf( "Failed to write %s\n", picname );
	}

}


static void GL_Strings_f( void ) {
	Com_Printf( "GL_VENDOR: %s\n", gl_config.vendorString );
	Com_Printf( "GL_RENDERER: %s\n", gl_config.rendererString );
	Com_Printf( "GL_VERSION: %s\n", gl_config.versionString );
	Com_Printf( "GL_EXTENSIONS: %s\n", gl_config.extensionsString );
}

// ============================================================================== 


static void GL_ModeChanged( int width, int height, int flags,
    int rowbytes, void *pixels )
{
	gl_config.vidWidth = width;
	gl_config.vidHeight = height;
    gl_config.flags = flags;
}

static void GL_Register( void ) {
	cvar.Subsystem( CVAR_SYSTEM_VIDEO );

    /* misc */
	gl_partscale = cvar.Get( "gl_partscale", "1.5", 0 );
#ifdef USE_JPEG
	gl_screenshot_quality = cvar.Get( "gl_screenshot_quality", "100",
        CVAR_ARCHIVE );
#endif
	gl_modulate = cvar.Get( "gl_modulate", "1", CVAR_ARCHIVE );
    gl_hwgamma = cvar.Get( "vid_hwgamma", "0", CVAR_ARCHIVE|CVAR_LATCHED );

    /* development variables */
	gl_znear = cvar.Get( "gl_znear", "2", CVAR_CHEAT );
	gl_zfar = cvar.Get( "gl_zfar", "16384", 0 );
	gl_log = cvar.Get( "gl_log", "0", 0 );
	gl_drawworld = cvar.Get( "gl_drawworld", "1", CVAR_CHEAT );
	gl_drawentities = cvar.Get( "gl_drawentities", "1", CVAR_CHEAT );
    gl_showtris = cvar.Get( "gl_showtris", "0", CVAR_CHEAT );
    gl_showstats = cvar.Get( "gl_showstats", "0", 0 );
    gl_cull_nodes = cvar.Get( "gl_cull_nodes", "1", 0 );
	gl_cull_models = cvar.Get( "gl_cull_models", "1", 0 );
    gl_clear = cvar.Get( "gl_clear", "0", 0 );
    gl_novis = cvar.Get( "gl_novis", "0", 0 );
    gl_lockpvs = cvar.Get( "gl_lockpvs", "0", CVAR_CHEAT );
    gl_primitives = cvar.Get( "gl_primitives", "0", 0 );
	gl_sort = cvar.Get( "gl_sort", "0", 0 );
    gl_subdivide = cvar.Get( "gl_subdivide", "1", 0 );
    gl_fastsky = cvar.Get( "gl_fastsky", "0", 0 );
    gl_dynamic = cvar.Get( "gl_dynamic", "2", CVAR_ARCHIVE );
    gl_fullbright = cvar.Get( "r_fullbright", "0", CVAR_CHEAT );
    
	cmd.AddCommand( "screenshot", GL_ScreenShot_f );
#ifdef USE_JPEG
	cmd.AddCommand( "screenshotJPEG", GL_ScreenShot_f );
#endif
	cmd.AddCommand( "strings", GL_Strings_f );

	cvar.Subsystem( CVAR_SYSTEM_GENERIC );
}

static void GL_Unregister( void ) {
	cmd.RemoveCommand( "screenshot" );
#ifdef USE_JPEG
	cmd.RemoveCommand( "screenshotJPEG" );
#endif
	cmd.RemoveCommand( "strings" );

}

static qboolean GL_SetupExtensions( void ) {
	const char *extensions;
    int integer;
	float value;

	extensions = gl_config.extensionsString;
	if( strstr( extensions, "GL_EXT_compiled_vertex_array" ) ) {
		Com_Printf( "...enabling GL_EXT_compiled_vertex_array\n" );
	    qglLockArraysEXT = ( PFNGLLOCKARRAYSEXTPROC )qwglGetProcAddress( "glLockArraysEXT" );
	    qglUnlockArraysEXT = ( PFNGLUNLOCKARRAYSEXTPROC )qwglGetProcAddress( "glUnlockArraysEXT" );
	} else {
		Com_Printf( "GL_EXT_compiled_vertex_array not found\n" );
    }
    
    gl_static.numTextureUnits = 1;
	if( strstr( extensions, "GL_ARB_multitexture" ) ) {
        qglGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &integer );
        if( integer > 1 ) {
            Com_Printf( "...enabling GL_ARB_multitexture (%d texture units)\n", integer );
            qglActiveTextureARB = ( PFNGLACTIVETEXTUREARBPROC )qwglGetProcAddress( "glActiveTextureARB" );
            qglClientActiveTextureARB = ( PFNGLCLIENTACTIVETEXTUREARBPROC )qwglGetProcAddress( "glClientActiveTextureARB" );
            qglMultiTexCoord2fvARB = ( PFNGLMULTITEXCOORD2FVARBPROC )qwglGetProcAddress( "glMultiTexCoord2fvARB" );
            if( integer > MAX_TMUS ) {
                integer = MAX_TMUS;
            }
            gl_static.numTextureUnits = integer;
        } else {
            Com_Printf( "...ignoring GL_ARB_multitexture,\n"
                    "not enough texture units supported (%d)\n", integer );
        }
	} else {
		Com_Printf( "GL_ARB_multitexture not found\n" );
    }

	gl_config.maxAnisotropy = 1;
	if( strstr( extensions, "GL_EXT_texture_filter_anisotropic" ) ) {
		qglGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &value );
		if( value >= 2 ) {
			Com_Printf( "...enabling GL_EXT_texture_filter_anisotropic\n"
				"(max anisotropy is %.1f)\n", value );
			gl_config.maxAnisotropy = value;
		} else {
            Com_Printf( "...ignoring GL_EXT_texture_filter_anisotropic,\n"
                    "not enough anisotropy supported (%.1f)\n", value );
		}
	} else {
		Com_Printf( "GL_EXT_texture_filter_anisotropic not found\n" );
    }

	if( !qglActiveTextureARB ) {
		return qfalse;
	}
    
	qglGetIntegerv( GL_MAX_TEXTURE_SIZE, &gl_static.maxTextureSize );
	if( gl_static.maxTextureSize > MAX_TEXTURE_SIZE ) {
		gl_static.maxTextureSize = MAX_TEXTURE_SIZE;
	}

	return qtrue;
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
	qglEnable( GL_TEXTURE_2D );
    GL_Bits( GLS_DEFAULT );
}

static void GL_SetupRenderer( void ) {
    char renderer_buffer[MAX_STRING_CHARS];

	Q_strncpyz( renderer_buffer, gl_config.rendererString,
            sizeof( renderer_buffer ) );
	Q_strlwr( renderer_buffer );

	if( strstr( renderer_buffer, "voodoo" ) ) {
		if( !strstr( renderer_buffer, "rush" ) ) {
			gl_config.renderer = GL_RENDERER_VOODOO;
		} else {
			gl_config.renderer = GL_RENDERER_VOODOO_RUSH;
		}
	} else if( strstr( renderer_buffer, "permedia" ) ) {
		gl_config.renderer = GL_RENDERER_PERMEDIA2;
	} else if ( strstr( renderer_buffer, "glint" ) ) {
		gl_config.renderer = GL_RENDERER_GLINT;
	} else if( strstr( renderer_buffer, "gdi" ) ) {
		gl_config.renderer = GL_RENDERER_MCD;
	} else if( strstr( renderer_buffer, "glzicd" ) ) {
		gl_config.renderer = GL_RENDERER_INTERGRAPH;
	} else if( strstr( renderer_buffer, "pcx2" ) ) {
		gl_config.renderer = GL_RENDERER_POWERVR;
	} else if( strstr( renderer_buffer, "verite" ) ) {
		gl_config.renderer = GL_RENDERER_RENDITION;
	} else if( strstr( renderer_buffer, "mesa dri" ) ) {
		gl_config.renderer = GL_RENDERER_MESADRI;
	} else {
		gl_config.renderer = GL_RENDERER_OTHER;
	}
}

static void GL_PostInit( void ) {
	GL_InitImages();
    GL_InitModels();
	GL_SetDefaultState();
}

static qboolean GL_Init( qboolean total ) {
	Com_DPrintf( "GL_Init( %i )\n", total );

	if( !total ) {
		GL_PostInit();
		return qtrue;
	}

	Com_Printf( "ref_gl " VERSION ", " __DATE__ "\n" );
#if 0
#ifdef USE_JPEG
    Com_Printf( "w/ libjpeg v%d\n", JPEG_LIB_VERSION );
#endif
#ifdef USE_PNG
    Com_Printf( "w/ libpng v" PNG_LIBPNG_VER_STRING "\n" );
#endif
#endif

	/* initialize OS-specific parts of OpenGL */
	/* create the window and set up the context */
	if( !video.Init() ) {
		return qfalse;
	}

	GL_Register();

	/* initialize our QGL dynamic bindings */
	QGL_Init();

#define GET_STRING( x )  ( const char * )qglGetString( x )

	gl_config.vendorString = GET_STRING( GL_VENDOR );
	gl_config.rendererString = GET_STRING( GL_RENDERER );
	gl_config.versionString = GET_STRING( GL_VERSION );
	gl_config.extensionsString = GET_STRING( GL_EXTENSIONS );
    
	if( !gl_config.extensionsString || !gl_config.extensionsString[0] ) {
		Com_EPrintf( "No OpenGL extensions found, check your drivers\n" );
		goto fail;
	}

	if( !GL_SetupExtensions() ) {
		Com_EPrintf( "Some of the required OpenGL extensions are missing\n" );
		goto fail;
	}

	if( gl_hwgamma->integer && !( gl_config.flags & QVF_GAMMARAMP ) ) {
		cvar.SetInteger( "vid_hwgamma", 0 );
		Com_Printf( "Hardware gamma is not supported by this video driver\n" );
	}

    GL_SetupRenderer();

	QGL_EnableLogging( gl_log->integer );
	gl_log->modified = qfalse;

    GL_PostInit();

	if( (( size_t )tess.vertices) & 15 ) {
		Com_WPrintf( "tess.vertices not 16 byte aligned\n" );
	}

    gl_sort = cvar.Get( "gl_sort",
        gl_config.renderer == GL_RENDERER_MESADRI ? "1" : "0", 0 );

	Com_DPrintf( "Finished GL_Init\n" );

    return qtrue;

fail:
    QGL_Shutdown();
	GL_Unregister();
	video.Shutdown();
    return qfalse;
}

/*
===============
R_Shutdown
===============
*/
void GL_Shutdown( qboolean total ) {
	Com_DPrintf( "GL_Shutdown( %i )\n", total );

    Bsp_FreeWorld();
	GL_ShutdownImages();
    GL_ShutdownModels();

	if( !total ) {
		return;
	}

	/*
	** shut down OS specific OpenGL stuff like contexts, etc.
	*/
	video.Shutdown();

	/*
	** shutdown our QGL subsystem
	*/
	QGL_Shutdown();

	GL_Unregister();

    memset( &gl_static, 0, sizeof( gl_static ) );
    memset( &gl_config, 0, sizeof( gl_config ) );
}

void GL_BeginRegistration( const char *name ) {
    char fullname[MAX_QPATH];
    bspTexinfo_t *texinfo, *lastexinfo;
	bspLeaf_t *leaf, *lastleaf;
	bspNode_t *node, *lastnode;
    int i;

	gl_static.registering = qtrue;
    registration_sequence++;

    memset( &glr, 0, sizeof( glr ) );
	glr.viewcluster1 = glr.viewcluster2 = -2;
    
	Com_sprintf( fullname, sizeof( fullname ), "maps/%s.bsp", name );
   
	/* check if the required world model was already loaded */
    if( !strcmp( r_world.name, fullname ) &&
        !cvar.VariableInteger( "flushmap" ) )
    {
		lastexinfo = r_world.texinfos + r_world.numTexinfos;
        for( texinfo = r_world.texinfos; texinfo != lastexinfo; texinfo++ ) {
            texinfo->image->registration_sequence = registration_sequence;
        }
		lastleaf = r_world.leafs + r_world.numLeafs;
	    for( leaf = r_world.leafs; leaf != lastleaf; leaf++ ) {
            leaf->visframe = 0;
        }
		lastnode = r_world.nodes + r_world.numNodes;
	    for( node = r_world.nodes; node != lastnode; node++ ) {
            node->visframe = 0;
        }
        for( i = 0; i < lm.numMaps; i++ ) {
            lm.lightmaps[i]->registration_sequence = registration_sequence;
        }
		Com_DPrintf( "GL_BeginRegistration: reused old world model\n" );
        return;
    }
     
    Bsp_FreeWorld();
    GL_BeginPostProcessing();
    
    if( !Bsp_LoadWorld( fullname ) ) {
        Com_Error( ERR_DROP, "Couldn't load '%s'\n", fullname );
    }
    
    GL_EndPostProcessing();
}

void GL_EndRegistration( void ) {
    R_FreeUnusedImages();
	Model_FreeUnused();
	if( scrap_dirty ) {
		Scrap_Upload();
	}
	gl_static.registering = qfalse;
}

void GL_SetPalette( const byte *pal ) {
	int i;

	if( pal == NULL ) {
		for( i = 0; i < 256; i++ ) {
			gl_static.palette[i] = d_8to24table[i];
		}
		return;
	}

	for( i = 0; i < 256; i++ ) {
		gl_static.palette[i] = MakeColor( pal[0], pal[1], pal[2], 255 );
		pal += 3;
	}
}

void GL_GetConfig( glconfig_t *config ) {
    *config = gl_config;
}

#ifndef REF_HARD_LINKED
// this is only here so the functions in q_shared.c can link

void Com_Printf( const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	com.Print( PRINT_ALL, text );
}

void Com_DPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	com.Print( PRINT_DEVELOPER, text );
}

void Com_WPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	com.Print( PRINT_WARNING, text );
}

void Com_EPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	com.Print( PRINT_ERROR, text );
}

void Com_Error( comErrorType_t type, const char *error, ... ) {
	va_list		argptr;
	char		text[MAXPRINTMSG];

	va_start( argptr, error );
	Q_vsnprintf( text, sizeof( text ), error, argptr );
	va_end( argptr );

	com.Error( type, text );
}

#endif /* !REF_HARD_LINKED */

/*
=================
Ref_FillAPI
=================
*/
static void Ref_FillAPI( refAPI_t *api ) {
	api->BeginRegistration = GL_BeginRegistration;
	api->RegisterModel = GL_RegisterModel;
	api->RegisterSkin = R_RegisterSkin;
	api->RegisterPic = R_RegisterPic;
	api->RegisterFont = GL_RegisterFont;
	api->SetSky = R_SetSky;
	api->EndRegistration = GL_EndRegistration;
	api->GetModelSize = GL_GetModelSize;

	api->RenderFrame = GL_RenderFrame;
	api->LightPoint = GL_LightPoint;

    api->SetColor = Draw_SetColor;
    api->SetClipRect = Draw_SetClipRect;
	api->SetScale = Draw_SetScale;
	api->DrawString = Draw_String;
	api->DrawChar = Draw_Char;
	api->DrawGetPicSize = Draw_GetPicSize;
	api->DrawGetFontSize = Draw_GetFontSize;
	api->DrawPic = Draw_Pic;
	api->DrawStretchPicST = Draw_StretchPicST;
	api->DrawStretchPic = Draw_StretchPic;
	api->DrawTileClear = Draw_TileClear;
	api->DrawFill = Draw_Fill;
	api->DrawStretchRaw = Draw_StretchRaw;
	api->DrawFillEx = Draw_FillEx;

	api->Init = GL_Init;
	api->Shutdown = GL_Shutdown;

	api->CinematicSetPalette = GL_SetPalette;
	api->BeginFrame = GL_BeginFrame;
	api->EndFrame = GL_EndFrame;
    api->ModeChanged = GL_ModeChanged;

	api->GetConfig = GL_GetConfig;
}

/*
=================
Ref_APISetupCallback
=================
*/
qboolean Ref_APISetupCallback( api_type_t type, void *api ) {
	switch( type ) {
	case API_REFRESH:
		Ref_FillAPI( ( refAPI_t * )api );
		break;
	default:
		return qfalse;
	}

	return qtrue;
}

#ifndef REF_HARD_LINKED

/*
@@@@@@@@@@@@@@@@@@@@@
moduleEntry

@@@@@@@@@@@@@@@@@@@@@
*/
EXPORTED void *moduleEntry( int query, void *data ) {
	moduleInfo_t *info;
	moduleCapability_t caps;
	APISetupCallback_t callback;

	switch( query ) {
	case MQ_GETINFO:
		info = ( moduleInfo_t * )data;
		info->api_version = MODULES_APIVERSION;
		Q_strncpyz( info->fullname, "OpenGL Refresh Driver",
                sizeof( info->fullname ) );
		Q_strncpyz( info->author, "Andrey Nazarov", sizeof( info->author ) );
		return ( void * )qtrue;

	case MQ_GETCAPS:
		caps = MCP_REFRESH;
		return ( void * )caps;

	case MQ_SETUPAPI:
		if( ( callback = ( APISetupCallback_t )data ) == NULL ) {
			return NULL;
		}
		callback( API_CMD, &cmd );
		callback( API_CVAR, &cvar );
		callback( API_FS, &fs );
		callback( API_COMMON, &com );
		callback( API_SYSTEM, &sys );
		callback( API_VIDEO_OPENGL, &video );

		return ( void * )Ref_APISetupCallback;

	}

	/* quiet compiler warning */
	return NULL;
}

#endif /* !REF_HARD_LINKED */

