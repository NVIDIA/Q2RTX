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

#define RF_SHELL_MASK \
    ( RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | \
      RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM )

static vec3_t oldscale;
static vec3_t newscale;
static vec3_t translate;
static float frontlerp;
static float backlerp;

typedef void (*tessfunc_t)( const maliasmesh_t *, int, int );

#if USE_DOTSHADING
static const vec_t *shadelight;
static vec3_t shadedir;

// matches the anormtab.h precalculations
static inline vec_t shadedot( const vec_t *normal ) {
    vec_t d = DotProduct( normal, shadedir );

    if( d < 0 ) {
        d *= 0.3f;
    }

    return d + 1;
}

#endif // USE_DOTSHADING

static inline void get_static_normal( vec_t *normal, const maliasvert_t *vert ) {
    unsigned lat = vert->norm[0];
    unsigned lng = vert->norm[1];

    normal[0] = TAB_SIN( lat ) * TAB_COS( lng );
    normal[1] = TAB_SIN( lat ) * TAB_SIN( lng );
    normal[2] = TAB_COS( lat );
}

static void tess_static_mesh( const maliasmesh_t *mesh, int oldframe, int newframe ) {
    maliasvert_t *src_vert = &mesh->verts[newframe * mesh->numverts];
    vec_t *dst_vert = tess.vertices;
    int i, count = mesh->numverts;
    vec3_t normal;

    if( glr.ent->flags & RF_SHELL_MASK ) {
        for( i = 0; i < count; i++ ) {
            get_static_normal( normal, src_vert );

            dst_vert[0] = normal[0] * POWERSUIT_SCALE +
                src_vert->pos[0] * newscale[0] + translate[0];
            dst_vert[1] = normal[1] * POWERSUIT_SCALE +
                src_vert->pos[1] * newscale[1] + translate[1];
            dst_vert[2] = normal[2] * POWERSUIT_SCALE +
                src_vert->pos[2] * newscale[2] + translate[2];
            dst_vert += 4;

            src_vert++;
        }
#if USE_DOTSHADING
    } else if( shadelight ) {
        vec_t d;

        for( i = 0; i < count; i++ ) {
            dst_vert[0] = src_vert->pos[0] * newscale[0] + translate[0];
            dst_vert[1] = src_vert->pos[1] * newscale[1] + translate[1];
            dst_vert[2] = src_vert->pos[2] * newscale[2] + translate[2];

            get_static_normal( normal, src_vert );

            d = shadedot( normal );
            dst_vert[3] = shadelight[0] * d;
            dst_vert[4] = shadelight[1] * d;
            dst_vert[5] = shadelight[2] * d;
            dst_vert[6] = shadelight[3];
            dst_vert += VERTEX_SIZE;
            
            src_vert++;
        }
#endif
    } else {
        for( i = 0; i < count; i++ ) {
            dst_vert[0] = src_vert->pos[0] * newscale[0] + translate[0];
            dst_vert[1] = src_vert->pos[1] * newscale[1] + translate[1];
            dst_vert[2] = src_vert->pos[2] * newscale[2] + translate[2];
            dst_vert += 4;

            src_vert++;
        }
    }

    c.trisDrawn += count;
}

static inline void get_lerped_normal( vec_t *normal,
    const maliasvert_t *oldvert, const maliasvert_t *newvert )
{
    vec3_t oldnorm, newnorm, tmp;
    vec_t len;

    get_static_normal( oldnorm, oldvert );
    get_static_normal( newnorm, newvert );

    LerpVector2( oldnorm, newnorm, backlerp, frontlerp, tmp );

    // normalize result
    len = 1 / VectorLength( tmp );
    VectorScale( tmp, len, normal );
}

static void tess_lerped_mesh( const maliasmesh_t *mesh, int oldframe, int newframe ) {
    maliasvert_t *src_oldvert = &mesh->verts[oldframe * mesh->numverts];
    maliasvert_t *src_newvert = &mesh->verts[newframe * mesh->numverts];
    vec_t *dst_vert = tess.vertices;
    int i, count = mesh->numverts;
    vec3_t normal;

    if( glr.ent->flags & RF_SHELL_MASK ) {
        for( i = 0; i < count; i++ ) {
            get_lerped_normal( normal, src_oldvert, src_newvert );

            dst_vert[0] = normal[0] * POWERSUIT_SCALE +
                src_oldvert->pos[0] * oldscale[0] +
                src_newvert->pos[0] * newscale[0] + translate[0];
            dst_vert[1] = normal[1] * POWERSUIT_SCALE +
                src_oldvert->pos[1] * oldscale[1] +
                src_newvert->pos[1] * newscale[1] + translate[1];
            dst_vert[2] = normal[2] * POWERSUIT_SCALE +
                src_oldvert->pos[2] * oldscale[2] +
                src_newvert->pos[2] * newscale[2] + translate[2];
            dst_vert += 4;
 
            src_oldvert++;
            src_newvert++;
        }
#if USE_DOTSHADING
    } else if( shadelight ) {
        vec_t d;

        for( i = 0; i < count; i++ ) {
            dst_vert[0] =
                src_oldvert->pos[0] * oldscale[0] +
                src_newvert->pos[0] * newscale[0] + translate[0];
            dst_vert[1] =
                src_oldvert->pos[1] * oldscale[1] +
                src_newvert->pos[1] * newscale[1] + translate[1];
            dst_vert[2] =
                src_oldvert->pos[2] * oldscale[2] +
                src_newvert->pos[2] * newscale[2] + translate[2];

            get_lerped_normal( normal, src_oldvert, src_newvert );

            d = shadedot( normal );
            dst_vert[3] = shadelight[0] * d;
            dst_vert[4] = shadelight[1] * d;
            dst_vert[5] = shadelight[2] * d;
            dst_vert[6] = shadelight[3];
            dst_vert += VERTEX_SIZE;

            src_oldvert++;
            src_newvert++;
        }
#endif
    } else {
        for( i = 0; i < count; i++ ) {
            dst_vert[0] =
                src_oldvert->pos[0] * oldscale[0] +
                src_newvert->pos[0] * newscale[0] + translate[0];
            dst_vert[1] =
                src_oldvert->pos[1] * oldscale[1] +
                src_newvert->pos[1] * newscale[1] + translate[1];
            dst_vert[2] =
                src_oldvert->pos[2] * oldscale[2] +
                src_newvert->pos[2] * newscale[2] + translate[2];
            dst_vert += 4;

            src_oldvert++;
            src_newvert++;
        }
    }
    
    c.trisDrawn += count;
}

static void GL_SetAliasColor( vec3_t origin, vec_t *color ) {
    int flags = glr.ent->flags;
    float f, m;
    int i;

    if( flags & RF_SHELL_MASK ) {
        VectorClear( color );
        if( flags & RF_SHELL_HALF_DAM ) {
            color[0] = 0.56f;
            color[1] = 0.59f;
            color[2] = 0.45f;
        }
        if( flags & RF_SHELL_DOUBLE ) {
            color[0] = 0.9f;
            color[1] = 0.7f;
        }
        if( flags & RF_SHELL_RED ) {
            color[0] = 1;
        }
        if( flags & RF_SHELL_GREEN ) {
            color[1] = 1;
        }
        if( flags & RF_SHELL_BLUE ) {
            color[2] = 1;
        }
    } else if( flags & RF_FULLBRIGHT ) {
        VectorSet( color, 1, 1, 1 );
    } else if( ( flags & RF_IR_VISIBLE ) && ( glr.fd.rdflags & RDF_IRGOGGLES ) ) {
        VectorSet( color, 1, 0, 0 );
    } else {
        _R_LightPoint( origin, color );

        if( flags & RF_MINLIGHT ) {
            for( i = 0; i < 3; i++ ) {
                if( color[i] > 0.1f ) {
                    break;
                }
            }
            if( i == 3 ) {
                VectorSet( color, 0.1f, 0.1f, 0.1f );
            }
        }

        if( flags & RF_GLOW ) {
            f = 0.1f * sin( glr.fd.time * 7 );
            for( i = 0; i < 3; i++ ) {
                m = color[i] * 0.8f;
                color[i] += f;
                if( color[i] < m )
                    color[i] = m;
            }
        }

        for( i = 0; i < 3; i++ ) {
            clamp( color[i], 0, 1 );
        }
    }
}

void GL_DrawAliasModel( model_t *model ) {
    entity_t *ent = glr.ent;
    image_t *image;
    int oldframeIdx, newframeIdx;
    maliasframe_t *newframe, *oldframe;
    maliasmesh_t *mesh, *last;
    tessfunc_t tessfunc;
    vec3_t origin;
    vec3_t bounds[2];
    vec_t radius;
    glStateBits_t bits;
    glCullResult_t cull;
    vec4_t color;
#if USE_CELSHADING
    vec3_t dir;
    float scale;
#endif
    int back, front;

    newframeIdx = ent->frame;
    if( newframeIdx < 0 || newframeIdx >= model->numframes ) {
        Com_DPrintf( "%s: no such frame %d\n", __func__, newframeIdx );
        newframeIdx = 0;
    }

    oldframeIdx = ent->oldframe;
    if( oldframeIdx < 0 || oldframeIdx >= model->numframes ) {
        Com_DPrintf( "%s: no such oldframe %d\n", __func__, oldframeIdx );
        oldframeIdx = 0;
    }

    newframe = model->frames + newframeIdx;

    backlerp = ent->backlerp;
    frontlerp = 1.0f - backlerp;

    // interpolate origin, if necessarry
    if( ent->flags & RF_FRAMELERP ) {
        LerpVector2( ent->oldorigin, ent->origin,
            backlerp, frontlerp, origin );
    } else {
        VectorCopy( ent->origin, origin );
    }

    if( newframeIdx == oldframeIdx || backlerp == 0 ) {
        oldframe = NULL;

        if( glr.entrotated ) {
            cull = GL_CullSphere( origin, newframe->radius );
            if( cull == CULL_OUT ) {
                c.spheresCulled++;
                return;
            }
            if( cull == CULL_CLIP ) {
                cull = GL_CullLocalBox( origin, newframe->bounds );
                if( cull == CULL_OUT ) {
                    c.rotatedBoxesCulled++;
                    return;
                }
            }
        } else {
            VectorAdd( newframe->bounds[0], origin, bounds[0] );
            VectorAdd( newframe->bounds[1], origin, bounds[1] );
            if( GL_CullBox( bounds ) == CULL_OUT ) {
                c.boxesCulled++;
                return;
            }
        }

        VectorCopy( newframe->scale, newscale );
        VectorCopy( newframe->translate, translate );
        tessfunc = tess_static_mesh;
    } else {
        oldframe = model->frames + oldframeIdx;

        if( glr.entrotated ) {
            radius = newframe->radius > oldframe->radius ?
                newframe->radius : oldframe->radius;
            cull = GL_CullSphere( origin, radius );
            if( cull == CULL_OUT ) {
                c.spheresCulled++;
                return;
            }
            UnionBounds( newframe->bounds, oldframe->bounds, bounds );
            if( cull == CULL_CLIP ) {
                cull = GL_CullLocalBox( origin, bounds );
                if( cull == CULL_OUT ) {
                    c.rotatedBoxesCulled++;
                    return;
                }
            }
        } else {
            UnionBounds( newframe->bounds, oldframe->bounds, bounds );
            VectorAdd( bounds[0], origin, bounds[0] );
            VectorAdd( bounds[1], origin, bounds[1] );
            if( GL_CullBox( bounds ) == CULL_OUT ) {
                c.boxesCulled++;
                return;
            }
        }

        VectorScale( oldframe->scale, backlerp, oldscale );
        VectorScale( newframe->scale, frontlerp, newscale );

        LerpVector2( oldframe->translate, newframe->translate,
            backlerp, frontlerp, translate );

        tessfunc = tess_lerped_mesh;
    }

#if USE_CELSHADING
    scale = 0;
    if( gl_celshading->value > 0 && ( ent->flags & RF_SHELL_MASK ) == 0 ) {
        if( gl_celshading->value > 5 ) {
            Cvar_Set( "gl_celshading", "5" );
        }
        VectorSubtract( origin, glr.fd.vieworg, dir );
        scale = VectorLength( dir );
        scale = 1.0f - scale / 700.0f;
    }
#endif

    // setup color
    GL_SetAliasColor( origin, color );

    // setup transparency
    bits = GLS_DEFAULT;
    color[3] = 1;
    if( ent->flags & RF_TRANSLUCENT ) {
        color[3] = ent->alpha;
        bits |= GLS_BLEND_BLEND|GLS_DEPTHMASK_FALSE;
    }

    GL_TexEnv( GL_MODULATE );
    
    qglPushMatrix();
    qglTranslatef( origin[0], origin[1], origin[2] );
    if( glr.entrotated ) {
        qglRotatef( ent->angles[YAW],   0, 0, 1 );
        qglRotatef( ent->angles[PITCH], 0, 1, 0 );
        qglRotatef( ent->angles[ROLL],  1, 0, 0 );
    }

    if( ent->flags & RF_DEPTHHACK ) {
        qglDepthRange( 0, 0.25f );
    }
    if( ( ent->flags & (RF_WEAPONMODEL|RF_LEFTHAND) ) ==
            (RF_WEAPONMODEL|RF_LEFTHAND) )
    {
        qglMatrixMode( GL_PROJECTION );
        qglScalef( -1, 1, 1 );
        qglMatrixMode( GL_MODELVIEW );
        qglCullFace( GL_BACK );
        back = GL_BACK;
        front = GL_FRONT;
    } else {
        back = GL_FRONT;
        front = GL_BACK;
    }

#if USE_DOTSHADING
    shadelight = NULL;
    if( gl_dotshading->integer && ( ent->flags & RF_SHELL_MASK ) == 0 ) {
        float cp, cy, sp, sy;
        vec_t yaw;

        shadelight = color;

        // matches the anormtab.h precalculations
        yaw = -DEG2RAD( ent->angles[YAW] );
        cy = cos( yaw );
        sy = sin( yaw );
        cp = cos( -M_PI/4 );
        sp = sin( -M_PI/4 );
        shadedir[0] = cp*cy;
        shadedir[1] = cp*sy;
        shadedir[2] = -sp;

        qglVertexPointer( 3, GL_FLOAT, 4*VERTEX_SIZE, tess.vertices );
        qglColorPointer( 4, GL_FLOAT, 4*VERTEX_SIZE, tess.vertices + 3 );
        qglEnableClientState( GL_COLOR_ARRAY );
    } else
#endif
    {
        qglVertexPointer( 3, GL_FLOAT, 4*4, tess.vertices );
        qglColor4fv( color );
    }

    last = model->meshes + model->nummeshes;
    for( mesh = model->meshes; mesh < last; mesh++ ) {
        if( ent->flags & RF_SHELL_MASK ) {
            GL_Bits( bits );
            GL_BindTexture( TEXNUM_WHITE );
        } else {
            if( ent->skin ) {
                image = IMG_ForHandle( ent->skin );
            } else {
                if( ( unsigned )ent->skinnum >= MAX_ALIAS_SKINS ) {
                    Com_DPrintf( "%s: no such skin: %d\n",
                        __func__, ent->skinnum );
                    image = mesh->skins[0];
                } else {
                    image = mesh->skins[ent->skinnum];
                    if( !image ) {
                        image = mesh->skins[0];
                    }
                }
            }
            if( !image ) {
                image = R_NOTEXTURE;
            }

            if( ( image->flags & ( if_transparent|if_paletted ) ) == if_transparent ) {
                GL_Bits( bits | GLS_BLEND_BLEND );
            } else {
                GL_Bits( bits );
            }

            GL_BindTexture( image->texnum );
        }

        (*tessfunc)( mesh, oldframeIdx, newframeIdx );

        qglTexCoordPointer( 2, GL_FLOAT, 0, mesh->tcoords );

        if( qglLockArraysEXT ) {
            qglLockArraysEXT( 0, mesh->numverts );
        }

        qglDrawElements( GL_TRIANGLES, mesh->numindices, GL_UNSIGNED_INT,
            mesh->indices );

#if USE_CELSHADING
        if( scale > 0 && scale <= 1 ) {
            qglCullFace( front );
            qglPolygonMode( back, GL_LINE );
            qglDisable( GL_TEXTURE_2D );
            qglLineWidth( gl_celshading->value*scale );
            GL_Bits( bits | GLS_BLEND_BLEND );
            qglColor4f( 0, 0, 0, scale );
            qglDrawElements( GL_TRIANGLES, mesh->numindices, GL_UNSIGNED_INT,
                mesh->indices );
            qglCullFace( back );
            qglPolygonMode( back, GL_FILL );
            qglColor4fv( color );
            qglEnable( GL_TEXTURE_2D );
        }
#endif

        if( gl_showtris->integer ) {
            GL_EnableOutlines();
            qglDrawElements( GL_TRIANGLES, mesh->numindices, GL_UNSIGNED_INT,
                mesh->indices );
            GL_DisableOutlines();
        }

        if( qglUnlockArraysEXT ) {
            qglUnlockArraysEXT();
        }
    }

#if USE_DOTSHADING
    if( shadelight ) {
        qglDisableClientState( GL_COLOR_ARRAY );
    }
#endif

    if( ent->flags & RF_DEPTHHACK ) {
        qglDepthRange( 0, 1 );
    }

    if( ( ent->flags & (RF_WEAPONMODEL|RF_LEFTHAND) ) ==
            (RF_WEAPONMODEL|RF_LEFTHAND) )
    {
        qglMatrixMode( GL_PROJECTION );
        qglScalef( -1, 1, 1 );
        qglMatrixMode( GL_MODELVIEW );
        qglCullFace( GL_FRONT );
    }
    
    qglPopMatrix();
}

