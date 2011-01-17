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
#if USE_CELSHADING
cvar_t *gl_celshading;
#endif
#if USE_DOTSHADING
cvar_t *gl_dotshading;
#endif
cvar_t *gl_znear;
cvar_t *gl_zfar;
cvar_t *gl_modulate;
cvar_t *gl_log;
cvar_t *gl_drawworld;
cvar_t *gl_drawentities;
cvar_t *gl_drawsky;
cvar_t *gl_showtris;
cvar_t *gl_showorigins;
cvar_t *gl_showtearing;
cvar_t *gl_cull_nodes;
cvar_t *gl_cull_models;
#ifdef _DEBUG
cvar_t *gl_showstats;
cvar_t *gl_showscrap;
cvar_t *gl_nobind;
cvar_t *gl_test;
#endif
cvar_t *gl_clear;
cvar_t *gl_novis;
cvar_t *gl_lockpvs;
cvar_t *gl_lightmap;
cvar_t *gl_dynamic;
#if USE_DLIGHTS
cvar_t *gl_dlight_falloff;
#endif
cvar_t *gl_doublelight_entities;
cvar_t *gl_polyblend;
cvar_t *gl_fullbright;
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
    f->type = PLANE_NON_AXIAL;
    SetPlaneSignbits( f );

    /* left side */
    f = &glr.frustumPlanes[1];
    VectorSubtract( forward, left, f->normal );
    f->dist = DotProduct( glr.fd.vieworg, f->normal );
    f->type = PLANE_NON_AXIAL;
    SetPlaneSignbits( f );

    angle = DEG2RAD( glr.fd.fov_y / 2 );
    fovSin = sin( angle );
    fovCos = cos( angle );

    VectorScale( glr.viewaxis[0], fovSin, forward );
    VectorScale( glr.viewaxis[2], fovCos, up );

    /* up side */
    f = &glr.frustumPlanes[2];
    VectorAdd( forward, up, f->normal );
    f->dist = DotProduct( glr.fd.vieworg, f->normal );
    f->type = PLANE_NON_AXIAL;
    SetPlaneSignbits( f );

    /* down side */
    f = &glr.frustumPlanes[3];
    VectorSubtract( forward, up, f->normal );
    f->dist = DotProduct( glr.fd.vieworg, f->normal );
    f->type = PLANE_NON_AXIAL;
    SetPlaneSignbits( f );
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
        dist = PlaneDiff( origin, p );
        if( dist < -radius ) {
            return CULL_OUT;
        }
        if( dist <= radius ) {
            cull = CULL_CLIP;
        }
    }

    return cull;
}

static inline void make_box_points( const vec3_t    origin,
                                          vec3_t    bounds[2],
                                          vec3_t    points[8] )
{
    int i;

    for( i = 0; i < 8; i++ ) {
        VectorCopy( origin, points[i] );
        VectorMA( points[i], bounds[(i>>0)&1][0], glr.entaxis[0], points[i] );
        VectorMA( points[i], bounds[(i>>1)&1][1], glr.entaxis[1], points[i] );
        VectorMA( points[i], bounds[(i>>2)&1][2], glr.entaxis[2], points[i] );
    }

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

    make_box_points( origin, bounds, points );

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

#if 0
void GL_DrawBox( const vec3_t origin, vec3_t bounds[2] ) {
    static const int indices[2][4] = {
        { 0, 1, 3, 2 },
        { 4, 5, 7, 6 }
    };
    vec3_t points[8];
    int i, j;

    qglDisable( GL_TEXTURE_2D );
    GL_TexEnv( GL_REPLACE );
    qglDisable( GL_DEPTH_TEST );
    qglColor4f( 1, 1, 1, 1 );

    make_box_points( origin, bounds, points );

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
#endif

// shared between lightmap and scrap allocators
qboolean GL_AllocBlock( int width, int height, int *inuse,
    int w, int h, int *s, int *t )
{
    int i, j, k, x, y, max_inuse, min_inuse;

    x = 0; y = height;
    min_inuse = height;
    for( i = 0; i < width - w; i++ ) {
        max_inuse = 0;
        for( j = 0; j < w; j++ ) {
            k = inuse[ i + j ];
            if( k >= min_inuse ) {
                break;
            }
            if( max_inuse < k ) {
                max_inuse = k;
            }
        }
        if( j == w ) {
            x = i;
            y = min_inuse = max_inuse;
        }
    }

    if( y + h > height ) {
        return qfalse;
    }

    for( i = 0; i < w; i++ ) {
        inuse[ x + i ] = y + h;
    }

    *s = x;
    *t = y;
    return qtrue;
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

    bits = GLS_DEPTHMASK_FALSE;
    if( alpha == 1.0f ) {
        if( image->flags & if_transparent ) {
            if( image->flags & if_paletted ) {
                bits |= GLS_ALPHATEST_ENABLE;
            } else {
                bits |= GLS_BLEND_BLEND;
            }
        }
    } else {
        bits |= GLS_BLEND_BLEND;
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
        if( !VectorEmpty( ent->angles ) ) {
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

        if( gl_showorigins->integer ) {
            GL_DrawNullModel();
        }

    }
}

static void GL_DrawTearing( void ) {
    static int i;

    /* alternate colors to make tearing obvious */
    i++;
    if (i & 1) {
        qglClearColor( 1.0f, 1.0f, 1.0f, 1.0f );
        qglColor3f( 1.0f, 1.0f, 1.0f );
    } else {
        qglClearColor( 1.0f, 0.0f, 0.0f, 0.0f );
        qglColor3f( 1.0f, 0.0f, 0.0f );
    }

    qglClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
    qglDisable( GL_TEXTURE_2D );
    qglRectf( 0, 0, gl_config.vidWidth, gl_config.vidHeight );
    qglEnable( GL_TEXTURE_2D );

    qglClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
}

static const char *GL_ErrorString( GLenum err ) {
    const char *str;

    switch( err ) {
#define E(x) case GL_##x: str = "GL_"#x; break;
        E( NO_ERROR )
        E( INVALID_ENUM )
        E( INVALID_VALUE )
        E( INVALID_OPERATION )
        E( STACK_OVERFLOW )
        E( STACK_UNDERFLOW )
        E( OUT_OF_MEMORY )
        default: str = "UNKNOWN ERROR";
#undef E
    }

    return str;
}

void GL_ClearErrors( void ) {
    GLenum err;

    while( ( err = qglGetError() ) != GL_NO_ERROR )
        ;
}

qboolean GL_ShowErrors( const char *func ) {
    GLenum err = qglGetError();

    if( err == GL_NO_ERROR ) {
        return qfalse;
    }

    do {
        if( gl_showerrors->integer ) {
            Com_EPrintf( "%s: %s\n", func, GL_ErrorString( err ) );
        }
    } while( ( err = qglGetError() ) != GL_NO_ERROR );

    return qtrue;
}

void R_RenderFrame( refdef_t *fd ) {
    GL_Flush2D();

    if( !gl_static.world.cache && !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
        Com_Error( ERR_FATAL, "%s: NULL worldmodel", __func__ );
    }

    glr.drawframe++;

    glr.fd = *fd;
    glr.num_beams = 0;

#if USE_DLIGHTS
    if( gl_dynamic->integer != 1 ) {
        glr.fd.num_dlights = 0;
    }
#endif

    if( lm.dirty ) {
        LM_RebuildSurfaces();
        lm.dirty = qfalse;
    }

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

    // go back into 2D mode
    GL_Setup2D();

    if( gl_polyblend->integer && glr.fd.blend[3] != 0 ) {
        GL_Blend();
    }

#ifdef _DEBUG
    if( gl_lightmap->integer > 1 ) {
        Draw_Lightmaps();
    }
#endif

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
#ifdef _DEBUG
    if( gl_showstats->integer ) {
        GL_Flush2D();
        Draw_Stats();
    }
    if( gl_showscrap->integer ) {
        Draw_Scrap();
    }
#endif
    GL_Flush2D();

    if( gl_showtearing->integer ) {
        GL_DrawTearing();
    }

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
static void make_screenshot( const char *ext, img_save_t func, GLenum format, int param ) {
    char        buffer[MAX_OSPATH]; 
    byte        *pixels;
    qerror_t    ret;
    qhandle_t   f;
    int         i;

    if( Cmd_Argc() > 1 ) {
        f = FS_EasyOpenFile( buffer, sizeof( buffer ), FS_MODE_WRITE,
            SCREENSHOTS_DIRECTORY "/", Cmd_Argv( 1 ), ext );
        if( !f ) {
            return;
        }
    } else {
        // find a file name to save it to 
        for( i = 0; i < 1000; i++ ) {
            Q_snprintf( buffer, sizeof( buffer ), SCREENSHOTS_DIRECTORY "/quake%03d%s", i, ext );
            ret = FS_FOpenFile( buffer, &f, FS_MODE_WRITE|FS_FLAG_EXCL );
            if( f ) {
                break;
            }
            if( ret != Q_ERR_EXIST ) {
                Com_EPrintf( "Couldn't exclusively open %s for writing: %s\n",
                    buffer, Q_ErrorString( ret ) );
                return;
            }
        }

        if( i == 1000 ) {
            Com_EPrintf( "All screenshot slots are full.\n" );
            return;
        }
    }

    pixels = FS_AllocTempMem( gl_config.vidWidth * gl_config.vidHeight * 3 );

    qglReadPixels( 0, 0, gl_config.vidWidth, gl_config.vidHeight, format,
        GL_UNSIGNED_BYTE, pixels );

    ret = func( f, buffer, pixels, gl_config.vidWidth, gl_config.vidHeight, param );

    FS_FreeFile( pixels );

    FS_FCloseFile( f );

    if( ret < 0 ) {
        Com_EPrintf( "Couldn't write %s: %s\n", buffer, Q_ErrorString( ret ) );
    } else {
        Com_Printf( "Wrote %s\n", buffer );
    }
}
#endif

/* 
================== 
GL_ScreenShot_f
================== 
*/
static void GL_ScreenShot_f( void )  {
#if USE_TGA
    if( Cmd_Argc() > 2 ) {
        Com_Printf( "Usage: %s [name]\n", Cmd_Argv( 0 ) );
        return;
    }
    make_screenshot( ".tga", IMG_SaveTGA, GL_BGR, 0 );
#else
    Com_Printf( "Couldn't create screenshot due to no TGA support linked in.\n" );
#endif
}

#if USE_JPG
static void GL_ScreenShotJPG_f( void )  {
    int quality;

    if( Cmd_Argc() > 3 ) {
        Com_Printf( "Usage: %s [name] [quality]\n", Cmd_Argv( 0 ) );
        return;
    }

    if( Cmd_Argc() > 2 ) {
        quality = atoi( Cmd_Argv( 2 ) );
    } else {
        quality = gl_screenshot_quality->integer;
    }

    make_screenshot( ".jpg", IMG_SaveJPG, GL_RGB, quality );
}
#endif

#if USE_PNG
static void GL_ScreenShotPNG_f( void )  {
    int compression;

    if( Cmd_Argc() > 3 ) {
        Com_Printf( "Usage: %s [name] [compression]\n", Cmd_Argv( 0 ) );
        return;
    }

    if( Cmd_Argc() > 2 ) {
        compression = atoi( Cmd_Argv( 2 ) );
    } else {
        compression = gl_screenshot_compression->integer;
    }

    make_screenshot( ".png", IMG_SavePNG, GL_RGB, compression );
}
#endif

static void GL_Strings_f( void ) {
    Com_Printf( "GL_VENDOR: %s\n", gl_config.vendorString );
    Com_Printf( "GL_RENDERER: %s\n", gl_config.rendererString );
    Com_Printf( "GL_VERSION: %s\n", gl_config.versionString );
    Com_Printf( "GL_EXTENSIONS: %s\n", gl_config.extensionsString );
    Com_Printf( "GL_MAX_TEXTURE_SIZE: %d\n", gl_config.maxTextureSize );
    Com_Printf( "GL_MAX_TEXTURE_UNITS: %d\n", gl_config.numTextureUnits );
    Com_Printf( "GL_MAX_TEXTURE_MAX_ANISOTROPY: %d\n", (int)gl_config.maxAnisotropy );
}

// ============================================================================== 

static void GL_Register( void ) {
    gl_partscale = Cvar_Get( "gl_partscale", "2", 0 );
#if USE_JPG
    gl_screenshot_quality = Cvar_Get( "gl_screenshot_quality", "100", 0 );
#endif
#if USE_PNG
    gl_screenshot_compression = Cvar_Get( "gl_screenshot_compression", "6", 0 );
#endif
#if USE_CELSHADING
    gl_celshading = Cvar_Get( "gl_celshading", "0", 0 );
#endif
#if USE_DOTSHADING
    gl_dotshading = Cvar_Get( "gl_dotshading", "1", 0 );
#endif
    gl_modulate = Cvar_Get( "gl_modulate", "1", CVAR_ARCHIVE );
    gl_znear = Cvar_Get( "gl_znear", "2", CVAR_CHEAT );
    gl_zfar = Cvar_Get( "gl_zfar", "16384", 0 );
    gl_log = Cvar_Get( "gl_log", "0", 0 );
    gl_drawworld = Cvar_Get( "gl_drawworld", "1", CVAR_CHEAT );
    gl_drawentities = Cvar_Get( "gl_drawentities", "1", CVAR_CHEAT );
    gl_drawsky = Cvar_Get( "gl_drawsky", "1", 0 );
    gl_showtris = Cvar_Get( "gl_showtris", "0", CVAR_CHEAT );
    gl_showorigins = Cvar_Get( "gl_showorigins", "0", CVAR_CHEAT );
    gl_showtearing = Cvar_Get( "gl_showtearing", "0", 0 );
#ifdef _DEBUG
    gl_showstats = Cvar_Get( "gl_showstats", "0", 0 );
    gl_showscrap = Cvar_Get( "gl_showscrap", "0", 0 );
    gl_nobind = Cvar_Get( "gl_nobind", "0", CVAR_CHEAT );
    gl_test = Cvar_Get( "gl_test", "0", 0 );
#endif
    gl_cull_nodes = Cvar_Get( "gl_cull_nodes", "1", 0 );
    gl_cull_models = Cvar_Get( "gl_cull_models", "1", 0 );
    gl_clear = Cvar_Get( "gl_clear", "0", 0 );
    gl_novis = Cvar_Get( "gl_novis", "0", 0 );
    gl_lockpvs = Cvar_Get( "gl_lockpvs", "0", CVAR_CHEAT );
    gl_lightmap = Cvar_Get( "gl_lightmap", "0", CVAR_CHEAT );
    gl_dynamic = Cvar_Get( "gl_dynamic", "2", 0 );
#if USE_DLIGHTS
    gl_dlight_falloff = Cvar_Get( "gl_dlight_falloff", "1", 0 );
#endif
    gl_doublelight_entities = Cvar_Get( "gl_doublelight_entities", "1", 0 );
    gl_polyblend = Cvar_Get( "gl_polyblend", "1", 0 );
    gl_fullbright = Cvar_Get( "r_fullbright", "0", CVAR_CHEAT );
    gl_showerrors = Cvar_Get( "gl_showerrors", "1", 0 );
    
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

    if( !gl_config.extensionsString || !gl_config.extensionsString[0] ) {
        Com_EPrintf( "No OpenGL extensions found, check your drivers\n" );
        return qfalse;
    }

    gl_fragment_program = Cvar_Get( "gl_fragment_program", "1", CVAR_REFRESH );
    gl_vertex_buffer_object = Cvar_Get( "gl_vertex_buffer_object", "1", CVAR_REFRESH );

    extensions = gl_config.extensionsString;
    if( strstr( extensions, "GL_EXT_compiled_vertex_array" ) ) {
        Com_Printf( "...enabling GL_EXT_compiled_vertex_array\n" );
        GPA( glLockArraysEXT );
        GPA( glUnlockArraysEXT );
    } else {
        Com_Printf( "GL_EXT_compiled_vertex_array not found\n" );
    }
    
    gl_config.numTextureUnits = 1;
    if( strstr( extensions, "GL_ARB_multitexture" ) ) {
        qglGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &integer );
        if( integer >= 2 ) {
            Com_Printf( "...enabling GL_ARB_multitexture (%d TMUs)\n", integer );
            GPA( glActiveTextureARB );
            GPA( glClientActiveTextureARB );
            if( integer > MAX_TMUS ) {
                integer = MAX_TMUS;
            }
            gl_config.numTextureUnits = integer;
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
            GPA( glBufferDataARB );
            GPA( glMapBufferARB );
            GPA( glUnmapBufferARB );
        } else {
            Com_Printf( "...ignoring GL_ARB_vertex_buffer_object\n" );
        }
    } else {
        Com_Printf( "GL_ARB_vertex_buffer_object not found\n" );
    }

    if( !qglActiveTextureARB ) {
        Com_EPrintf( "Required OpenGL extensions are missing\n" );
        return qfalse;
    }

    qglGetIntegerv( GL_MAX_TEXTURE_SIZE, &integer );
    if( integer < 256 ) {
        Com_EPrintf( "OpenGL reports invalid maximum texture size\n" );
        return qfalse;
    }

    if( integer & ( integer - 1 ) ) {
        integer = npot32( integer ) >> 1;
    }

    if( integer > MAX_TEXTURE_SIZE ) {
        integer = MAX_TEXTURE_SIZE;
    }

    gl_config.maxTextureSize = integer;

    return qtrue;
}

#undef GPA

static void GL_IdentifyRenderer( void ) {
    char *p;

    // parse renderer
    if( Q_stristr( gl_config.rendererString, "mesa dri" ) ) {
        gl_config.renderer = GL_RENDERER_MESADRI;
    } else {
        gl_config.renderer = GL_RENDERER_OTHER;
    }

    // parse version
    gl_config.version_major = strtoul( gl_config.versionString, &p, 10 );
    if( *p == '.' ) {
        gl_config.version_minor = strtoul( p + 1, NULL, 10 );
    }
}

static void GL_InitTables( void ) {
    vec_t lat, lng;
    const vec_t *v;
    int i;

    for( i = 0; i < NUMVERTEXNORMALS; i++ ) {
        v = bytedirs[i];
        lat = acos( v[2] );
        lng = atan2( v[1], v[0] );
        gl_static.latlngtab[i][0] = lat * (255.0f/(2*M_PI));
        gl_static.latlngtab[i][1] = lng * (255.0f/(2*M_PI));
    }

    for( i = 0; i < 256; i++ ) {
        gl_static.sintab[i] = sin( i * (2*M_PI/255.0f) );
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

    // initialize our QGL dynamic bindings
    QGL_Init();

    // get verious static strings from OpenGL
#define GET_STRING( x )  ( const char * )qglGetString( x )
    gl_config.vendorString = GET_STRING( GL_VENDOR );
    gl_config.rendererString = GET_STRING( GL_RENDERER );
    gl_config.versionString = GET_STRING( GL_VERSION );
    gl_config.extensionsString = GET_STRING( GL_EXTENSIONS );

    // parse renderer/version strings
    GL_IdentifyRenderer();

    // parse extension string
    if( !GL_SetupExtensions() ) {
        goto fail;
    }

    // register our variables
    GL_Register();

    QGL_EnableLogging( gl_log->integer );
    gl_log->modified = qfalse;

    GL_PostInit();

    GL_InitPrograms();

    GL_InitTables();

    if( (( size_t )tess.vertices) & 15 ) {
        Com_WPrintf( "tess.vertices not 16 byte aligned\n" );
    }

    Com_DPrintf( "Finished GL_Init\n" );

    return qtrue;

fail:
    memset( &gl_config, 0, sizeof( gl_config ) );
    QGL_Shutdown();
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

