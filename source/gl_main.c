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

glRefdef_t glr;
glStatic_t gl_static;
glconfig_t gl_config;
statCounters_t  c;

int registration_sequence;

cvar_t *gl_partscale;
#if USE_JPG
cvar_t *gl_screenshot_quality;
#endif
#if USE_PNG
cvar_t *gl_screenshot_compression;
#endif
cvar_t *gl_celshading;
cvar_t *gl_znear;
cvar_t *gl_zfar;
cvar_t *gl_modulate;
cvar_t *gl_log;
cvar_t *gl_drawworld;
cvar_t *gl_drawentities;
cvar_t *gl_drawsky;
cvar_t *gl_showtris;
cvar_t *gl_cull_nodes;
cvar_t *gl_cull_models;
cvar_t *gl_showstats;
cvar_t *gl_bind;
cvar_t *gl_clear;
cvar_t *gl_novis;
cvar_t *gl_lockpvs;
cvar_t *gl_lightmap;
#if USE_DYNAMIC
cvar_t *gl_dynamic;
#endif
cvar_t *gl_polyblend;
cvar_t *gl_fullbright;
cvar_t *gl_hwgamma;
cvar_t *gl_fullscreen;
cvar_t *gl_showerrors;
cvar_t *gl_fragment_program;
cvar_t *gl_vertex_buffer_object;

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
    
    if( !gl_polyblend->integer || glr.fd.blend[3] == 0 ) {
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
	mspriteframe_t *frame;
	image_t *image;
	int bits;
	float alpha;

	frame = &model->spriteframes[e->frame % model->numframes];
	image = frame->image;

	GL_TexEnv( GL_MODULATE );

	alpha = ( e->flags & RF_TRANSLUCENT ) ? e->alpha : 1.0f;

    if( alpha == 1.0f ) {
        if( image->flags & if_transparent ) {
            bits = GLS_ALPHATEST_ENABLE;
        } else {
            bits = GLS_DEFAULT;
        }
    } else {
        bits = GLS_BLEND_BLEND;
    }
	GL_Bits( bits );

	qglColor4f( 1, 1, 1, alpha );

	GL_BindTexture( image->texnum );

	qglBegin( GL_QUADS );

	qglTexCoord2f( 0, 1 );
	VectorMA( e->origin, -frame->origin_y, glr.viewaxis[2], point );
	VectorMA( point, frame->origin_x, glr.viewaxis[1], point );
	qglVertex3fv( point );

	qglTexCoord2f( 0, 0 );
	VectorMA( e->origin, frame->height - frame->origin_y, glr.viewaxis[2], point );
	VectorMA( point, frame->origin_x, glr.viewaxis[1], point );
	qglVertex3fv( point );

	qglTexCoord2f( 1, 0 );
	VectorMA( e->origin, frame->height - frame->origin_y, glr.viewaxis[2], point );
	VectorMA( point, frame->origin_x - frame->width, glr.viewaxis[1], point );
	qglVertex3fv( point );

	qglTexCoord2f( 1, 1 );
	VectorMA( e->origin, -frame->origin_y, glr.viewaxis[2], point );
	VectorMA( point, frame->origin_x - frame->width, glr.viewaxis[1], point );
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
	model_t *model;

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

        // inline BSP model
        if( ent->model & 0x80000000 ) {
            bsp_t *bsp = gl_static.world.cache;
            int index = ~ent->model;

            if( !bsp ) {
                Com_Error( ERR_DROP, "%s: inline model without world",
                    __func__ );
            }

            if( index < 1 || index >= bsp->nummodels ) {
                Com_Error( ERR_DROP, "%s: inline model %d out of range",
                    __func__, index );
            }

			GL_DrawBspModel( &bsp->models[index] );
            continue;
        }

		model = MOD_ForHandle( ent->model );
		if( !model ) {
			GL_DrawNullModel();
			continue;
		}

        if( model->frames ) {
			GL_DrawAliasModel( model );
        } else if( model->spriteframes ) {
			GL_DrawSpriteModel( model );
        } else {
			Com_Error( ERR_FATAL, "%s: bad model type", __func__ );
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

void GL_ShowErrors( const char *func ) {
	GLenum err;

    if( gl_showerrors->integer ) {
        while( ( err = qglGetError() ) != GL_NO_ERROR ) {
	    	Com_EPrintf( "%s: %s\n", func, GL_ErrorString( err ) );
        }
    }
}

void R_RenderFrame( refdef_t *fd ) {
	GL_Flush2D();

    if( !gl_static.world.cache && !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
        Com_Error( ERR_FATAL, "GL_RenderView: NULL worldmodel" );
    }

	glr.drawframe++;

    glr.fd = *fd;
    glr.num_beams = 0;

#if USE_DYNAMIC
    if( !gl_dynamic->integer ) {
        glr.fd.num_dlights = 0;
    }
#endif

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

	if( !( glr.fd.rdflags & RDF_NOWORLDMODEL ) && gl_drawworld->integer ) {
		GL_DrawAlphaFaces();
	}
    
	/* go back into 2D mode */
    GL_Setup2D();

    GL_Blend();

    GL_ShowErrors( __func__ );
}

void R_BeginFrame( void ) {
	if( gl_log->integer ) {
		QGL_LogNewFrame();
	}
    
    memset( &c, 0, sizeof( c ) );
    
    GL_Setup2D();

    if( gl_clear->integer ) {
	    qglClear( GL_COLOR_BUFFER_BIT );
    }

    GL_ShowErrors( __func__ );
}

void R_EndFrame( void ) {
    if( gl_showstats->integer ) {
        Draw_Stats();
    }
    GL_Flush2D();

	if( gl_log->modified ) {
		QGL_EnableLogging( gl_log->integer );
		gl_log->modified = qfalse;
	}

    GL_ShowErrors( __func__ );
	
    VID_EndFrame();

//    qglFinish();
}

/* 
============================================================================== 
 
						SCREEN SHOTS 
 
============================================================================== 
*/

#if USE_TGA || USE_JPG || USE_PNG
static char *screenshot_path( char *buffer, const char *ext ) {
    int i;

    if( Cmd_Argc() > 1 ) {
		Com_sprintf( buffer, MAX_OSPATH, SCREENSHOTS_DIRECTORY"/%s", Cmd_Argv( 1 ) );
		COM_AppendExtension( buffer, ext, MAX_OSPATH );
        return buffer;
    }
// 
// find a file name to save it to 
// 
    for( i = 0; i < 1000; i++ ) {
        Com_sprintf( buffer, MAX_OSPATH, SCREENSHOTS_DIRECTORY"/quake%03d%s", i, ext );
        if( FS_LoadFileEx( buffer, NULL, FS_PATH_GAME, TAG_FREE ) == INVALID_LENGTH ) {
            return buffer;	// file doesn't exist
        }
    }

    Com_Printf( "All screenshot slots are full.\n" );
    return NULL;
}
#endif


/* 
================== 
GL_ScreenShot_f
================== 
*/
static void GL_ScreenShot_f( void )  {
#if USE_TGA
	char		buffer[MAX_OSPATH]; 
	byte	    *bgr;
    qboolean    ret;

	if( Cmd_Argc() > 2 ) {
		Com_Printf( "Usage: %s [name]\n", Cmd_Argv( 0 ) );
		return;
	}

    if( !screenshot_path( buffer, ".tga" ) ) {
        return;
    }

	bgr = FS_AllocTempMem( gl_config.vidWidth * gl_config.vidHeight * 3 );

	qglReadPixels( 0, 0, gl_config.vidWidth, gl_config.vidHeight, GL_BGR,
        GL_UNSIGNED_BYTE, bgr );

	ret = IMG_WriteTGA( buffer, bgr, gl_config.vidWidth, gl_config.vidHeight );

	FS_FreeFile( bgr );

    if( ret ) {
    	Com_Printf( "Wrote %s\n", buffer );
    }
#else
    Com_Printf( "Couldn't create screenshot due to no TGA support linked in.\n" );
#endif
}

#if USE_JPG
static void GL_ScreenShotJPG_f( void )  {
	char		buffer[MAX_OSPATH]; 
	byte	    *rgb;
    int         quality;
    qboolean    ret;

	if( Cmd_Argc() > 3 ) {
		Com_Printf( "Usage: %s [name] [quality]\n", Cmd_Argv( 0 ) );
		return;
	}

    if( !screenshot_path( buffer, ".jpg" ) ) {
        return;
    }

	rgb = FS_AllocTempMem( gl_config.vidWidth * gl_config.vidHeight * 3 );

	qglReadPixels( 0, 0, gl_config.vidWidth, gl_config.vidHeight, GL_RGB,
        GL_UNSIGNED_BYTE, rgb );

    if( Cmd_Argc() > 2 ) {
        quality = atoi( Cmd_Argv( 2 ) );
    } else {
        quality = gl_screenshot_quality->integer;
    }

	ret = IMG_WriteJPG( buffer, rgb, gl_config.vidWidth, gl_config.vidHeight, quality );

	FS_FreeFile( rgb );

    if( ret ) {
    	Com_Printf( "Wrote %s\n", buffer );
    }
}
#endif

#if USE_PNG
static void GL_ScreenShotPNG_f( void )  {
	char		buffer[MAX_OSPATH]; 
	byte	    *rgb;
    int         compression;
    qboolean    ret;

	if( Cmd_Argc() > 3 ) {
		Com_Printf( "Usage: %s [name] [compression]\n", Cmd_Argv( 0 ) );
		return;
	}

    if( !screenshot_path( buffer, ".png" ) ) {
        return;
    }

	rgb = FS_AllocTempMem( gl_config.vidWidth * gl_config.vidHeight * 3 );

	qglReadPixels( 0, 0, gl_config.vidWidth, gl_config.vidHeight, GL_RGB,
        GL_UNSIGNED_BYTE, rgb );

    if( Cmd_Argc() > 2 ) {
        compression = atoi( Cmd_Argv( 2 ) );
    } else {
        compression = gl_screenshot_compression->integer;
    }

	ret = IMG_WritePNG( buffer, rgb, gl_config.vidWidth, gl_config.vidHeight, compression );

	FS_FreeFile( rgb );

    if( ret ) {
    	Com_Printf( "Wrote %s\n", buffer );
    }
}
#endif

static void GL_Strings_f( void ) {
	Com_Printf( "GL_VENDOR: %s\n", gl_config.vendorString );
	Com_Printf( "GL_RENDERER: %s\n", gl_config.rendererString );
	Com_Printf( "GL_VERSION: %s\n", gl_config.versionString );
	Com_Printf( "GL_EXTENSIONS: %s\n", gl_config.extensionsString );
}

// ============================================================================== 

static void GL_Register( void ) {
    /* misc */
	gl_partscale = Cvar_Get( "gl_partscale", "2", 0 );
#if USE_JPG
	gl_screenshot_quality = Cvar_Get( "gl_screenshot_quality", "100", 0 );
#endif
#if USE_PNG
	gl_screenshot_compression = Cvar_Get( "gl_screenshot_compression", "6", 0 );
#endif
	gl_celshading = Cvar_Get( "gl_celshading", "0", 0 );
	gl_modulate = Cvar_Get( "gl_modulate", "1", CVAR_ARCHIVE );
    gl_hwgamma = Cvar_Get( "vid_hwgamma", "0", CVAR_ARCHIVE|CVAR_REFRESH );

    /* development variables */
	gl_znear = Cvar_Get( "gl_znear", "2", CVAR_CHEAT );
	gl_zfar = Cvar_Get( "gl_zfar", "16384", 0 );
	gl_log = Cvar_Get( "gl_log", "0", 0 );
	gl_drawworld = Cvar_Get( "gl_drawworld", "1", CVAR_CHEAT );
	gl_drawentities = Cvar_Get( "gl_drawentities", "1", CVAR_CHEAT );
    gl_drawsky = Cvar_Get( "gl_drawsky", "1", 0 );
    gl_showtris = Cvar_Get( "gl_showtris", "0", CVAR_CHEAT );
    gl_showstats = Cvar_Get( "gl_showstats", "0", 0 );
    gl_cull_nodes = Cvar_Get( "gl_cull_nodes", "1", 0 );
	gl_cull_models = Cvar_Get( "gl_cull_models", "1", 0 );
	gl_bind = Cvar_Get( "gl_bind", "1", CVAR_CHEAT );
    gl_clear = Cvar_Get( "gl_clear", "0", 0 );
    gl_novis = Cvar_Get( "gl_novis", "0", 0 );
    gl_lockpvs = Cvar_Get( "gl_lockpvs", "0", CVAR_CHEAT );
    gl_lightmap = Cvar_Get( "gl_lightmap", "0", CVAR_CHEAT );
#if USE_DYNAMIC
    gl_dynamic = Cvar_Get( "gl_dynamic", "2", CVAR_ARCHIVE );
#endif
    gl_polyblend = Cvar_Get( "gl_polyblend", "1", 0 );
    gl_fullbright = Cvar_Get( "r_fullbright", "0", CVAR_CHEAT );
    gl_showerrors = Cvar_Get( "gl_showerrors", "1", 0 );
    gl_fragment_program = Cvar_Get( "gl_fragment_program", "0", CVAR_REFRESH );
    gl_vertex_buffer_object = Cvar_Get( "gl_vertex_buffer_object", "0", CVAR_REFRESH );
    
	Cmd_AddCommand( "screenshot", GL_ScreenShot_f );
#if USE_JPG
	Cmd_AddCommand( "screenshotjpg", GL_ScreenShotJPG_f );
#else
	Cmd_AddCommand( "screenshotjpg", GL_ScreenShot_f );
#endif
#if USE_PNG
	Cmd_AddCommand( "screenshotpng", GL_ScreenShotPNG_f );
#else
	Cmd_AddCommand( "screenshotpng", GL_ScreenShot_f );
#endif
	Cmd_AddCommand( "strings", GL_Strings_f );
}

static void GL_Unregister( void ) {
	Cmd_RemoveCommand( "screenshot" );
	Cmd_RemoveCommand( "screenshotjpg" );
	Cmd_RemoveCommand( "screenshotpng" );
	Cmd_RemoveCommand( "strings" );
}

#define GPA( x )    do { q ## x = ( void * )qglGetProcAddress( #x ); } while( 0 )

static qboolean GL_SetupExtensions( void ) {
	const char *extensions;
    int integer;
	float value;

	extensions = gl_config.extensionsString;
	if( strstr( extensions, "GL_EXT_compiled_vertex_array" ) ) {
		Com_Printf( "...enabling GL_EXT_compiled_vertex_array\n" );
	    GPA( glLockArraysEXT );
	    GPA( glUnlockArraysEXT );
	} else {
		Com_Printf( "GL_EXT_compiled_vertex_array not found\n" );
    }
    
    gl_static.numTextureUnits = 1;
	if( strstr( extensions, "GL_ARB_multitexture" ) ) {
        qglGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &integer );
        if( integer >= 2 ) {
            Com_Printf( "...enabling GL_ARB_multitexture (%d TMUs)\n", integer );
            GPA( glActiveTextureARB );
            GPA( glClientActiveTextureARB );
            if( integer > MAX_TMUS ) {
                integer = MAX_TMUS;
            }
            gl_static.numTextureUnits = integer;
        } else {
            Com_Printf( "...ignoring GL_ARB_multitexture,\n"
                "%d TMU is not enough\n", integer );
        }
	} else {
		Com_Printf( "GL_ARB_multitexture not found\n" );
    }

	gl_config.maxAnisotropy = 1;
	if( strstr( extensions, "GL_EXT_texture_filter_anisotropic" ) ) {
		qglGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &value );
		if( value >= 2 ) {
			Com_Printf( "...enabling GL_EXT_texture_filter_anisotropic (%d max)\n", ( int )value );
			gl_config.maxAnisotropy = value;
		} else {
            Com_Printf( "...ignoring GL_EXT_texture_filter_anisotropic,\n"
                    "%d anisotropy is not enough\n", ( int )value );
		}
	} else {
		Com_Printf( "GL_EXT_texture_filter_anisotropic not found\n" );
    }

	if( strstr( extensions, "GL_ARB_fragment_program" ) ) {
        if( gl_fragment_program->integer ) {
            Com_Printf( "...enabling GL_ARB_fragment_program\n" );
            GPA( glProgramStringARB );
            GPA( glBindProgramARB );
            GPA( glDeleteProgramsARB ); 
            GPA( glGenProgramsARB );
            GPA( glProgramEnvParameter4fvARB );
            GPA( glProgramLocalParameter4fvARB );
        } else {
            Com_Printf( "...ignoring GL_ARB_fragment_program\n" );
        }
	} else {
		Com_Printf( "GL_ARB_fragment_program not found\n" );
    }

	if( strstr( extensions, "GL_ARB_vertex_buffer_object" ) ) {
        if( gl_vertex_buffer_object->integer ) {
            Com_Printf( "...enabling GL_ARB_vertex_buffer_object\n" );
            GPA( glBindBufferARB );
            GPA( glDeleteBuffersARB );
            GPA( glGenBuffersARB );
            GPA( glIsBufferARB );
            GPA( glBufferDataARB );
            GPA( glBufferSubDataARB );
            GPA( glGetBufferSubDataARB );
            GPA( glMapBufferARB );
            GPA( glUnmapBufferARB );
            GPA( glGetBufferParameterivARB );
            GPA( glGetBufferPointervARB );
        } else {
            Com_Printf( "...ignoring GL_ARB_vertex_buffer_object\n" );
        }
	} else {
		Com_Printf( "GL_ARB_vertex_buffer_object not found\n" );
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

#undef GPA

static void GL_IdentifyRenderer( void ) {
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
	registration_sequence = 1;

	GL_InitImages();
    MOD_Init();
	GL_SetDefaultState();
}

// ============================================================================== 

/*
===============
R_Init
===============
*/
qboolean R_Init( qboolean total ) {
	Com_DPrintf( "GL_Init( %i )\n", total );

	if( !total ) {
		GL_PostInit();
		return qtrue;
	}

	Com_Printf( "ref_gl " VERSION ", " __DATE__ "\n" );

	// initialize OS-specific parts of OpenGL
	// create the window and set up the context
	if( !VID_Init() ) {
		return qfalse;
	}

	GL_Register();

	// initialize our QGL dynamic bindings
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
		Com_EPrintf( "Required OpenGL extensions are missing\n" );
		goto fail;
	}

	if( gl_hwgamma->integer && !( gl_config.flags & QVF_GAMMARAMP ) ) {
		Cvar_Set( "vid_hwgamma", "0" );
		Com_Printf( "Hardware gamma is not supported by this video driver\n" );
	}

    GL_IdentifyRenderer();

	QGL_EnableLogging( gl_log->integer );
	gl_log->modified = qfalse;

    GL_PostInit();

    GL_InitPrograms();

	if( (( size_t )tess.vertices) & 15 ) {
		Com_WPrintf( "tess.vertices not 16 byte aligned\n" );
	}

	Com_DPrintf( "Finished GL_Init\n" );

    return qtrue;

fail:
    QGL_Shutdown();
	GL_Unregister();
	VID_Shutdown();
    return qfalse;
}

/*
===============
R_Shutdown
===============
*/
void R_Shutdown( qboolean total ) {
	Com_DPrintf( "GL_Shutdown( %i )\n", total );

    GL_FreeWorld();
	GL_ShutdownImages();
    MOD_Shutdown();

	if( !total ) {
		return;
	}
    
    GL_ShutdownPrograms();

	// shut down OS specific OpenGL stuff like contexts, etc.
	VID_Shutdown();

	// shutdown our QGL subsystem
	QGL_Shutdown();

	GL_Unregister();

    memset( &gl_static, 0, sizeof( gl_static ) );
    memset( &gl_config, 0, sizeof( gl_config ) );
}

/*
===============
R_BeginRegistration
===============
*/
void R_BeginRegistration( const char *name ) {
    char fullname[MAX_QPATH];

	gl_static.registering = qtrue;
    registration_sequence++;

    memset( &glr, 0, sizeof( glr ) );
	glr.viewcluster1 = glr.viewcluster2 = -2;
    
	Q_concat( fullname, sizeof( fullname ), "maps/", name, ".bsp", NULL );
    GL_LoadWorld( fullname ); 
}

/*
===============
R_EndRegistration
===============
*/
void R_EndRegistration( void ) {
    IMG_FreeUnused();
	MOD_FreeUnused();
	Scrap_Upload();
	gl_static.registering = qfalse;
}

/*
===============
R_ModeChanged
===============
*/
void R_ModeChanged( int width, int height, int flags, int rowbytes, void *pixels ) {
	gl_config.vidWidth = width & ~7;
	gl_config.vidHeight = height & ~1;
    gl_config.flags = flags;
}

/*
===============
R_GetConfig
===============
*/
void R_GetConfig( glconfig_t *config ) {
    *config = gl_config;
}

