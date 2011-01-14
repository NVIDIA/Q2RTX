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

#include "com_local.h"
#include "files.h"
#include "ref_public.h"
#include "in_public.h"
#include "vid_public.h"
#include "cl_public.h"
#include "sys_public.h"
#include "q_list.h"
#include "bsp.h"
#include "r_shared.h"
#include "r_models.h"
#include "qgl_local.h"
#include "qgl_api.h"

#define USE_DOTSHADING  1
#define USE_CELSHADING  1

/*
 * gl_main.c
 * 
 */

#define MAX_TMUS        2

typedef struct {
    qboolean registering;
    struct {
        bsp_t *cache;
        mempool_t pool;
        vec_t *vertices;
        GLuint bufnum;
    } world;
    GLuint prognum_warp;
    float inverse_intensity;
    float sintab[256];
#define TAB_SIN(x) gl_static.sintab[(x)&255]
#define TAB_COS(x) gl_static.sintab[((x)+64)&255]
    byte latlngtab[NUMVERTEXNORMALS][2];
    byte lightstylemap[MAX_LIGHTSTYLES];
} glStatic_t;

typedef struct {
    refdef_t fd;
    vec3_t viewaxis[3];
    int visframe;
    int drawframe;
    int viewcluster1;
    int viewcluster2;
    cplane_t frustumPlanes[4];
    entity_t    *ent;
    vec3_t      entaxis[3];
    qboolean    entrotated;
    int     num_beams;
} glRefdef_t;

extern glStatic_t gl_static;
extern glconfig_t gl_config;
extern glRefdef_t glr;

typedef struct {
    int nodesVisible;
    int nodesDrawn;
    int facesMarked;
    int facesDrawn;
    int texSwitches;
    int texUploads;
    int trisDrawn;
    int batchesDrawn;
    int nodesCulled;
    int facesCulled;
    int boxesCulled;
    int spheresCulled;
    int rotatedBoxesCulled;
    int batchesDrawn2D;
} statCounters_t;

extern statCounters_t c;

#if USE_CELSHADING
extern cvar_t *gl_celshading;
#endif
#if USE_DOTSHADING
extern cvar_t *gl_dotshading;
#endif
extern cvar_t *gl_partscale;
extern cvar_t *gl_znear;
extern cvar_t *gl_zfar;
extern cvar_t *gl_modulate;
extern cvar_t *gl_showtris;
extern cvar_t *gl_cull_nodes;
#ifdef _DEBUG
extern cvar_t *gl_nobind;
extern cvar_t *gl_test;
#endif
extern cvar_t *gl_clear;
extern cvar_t *gl_novis;
extern cvar_t *gl_lockpvs;
extern cvar_t *gl_lightmap;
extern cvar_t *gl_drawsky;
extern cvar_t *gl_dynamic;
#if USE_DLIGHTS
extern cvar_t *gl_dlight_falloff;
#endif
extern cvar_t *gl_doublelight_entities;
extern cvar_t *gl_fullbright;
extern cvar_t *gl_mode;
extern cvar_t *gl_hwgamma;
extern cvar_t *gl_fullscreen;

typedef enum {
    CULL_OUT,
    CULL_IN,
    CULL_CLIP
} glCullResult_t;

glCullResult_t GL_CullBox( vec3_t bounds[2] );
glCullResult_t GL_CullSphere( const vec3_t origin, float radius );
glCullResult_t GL_CullLocalBox( const vec3_t origin, vec3_t bounds[2] );

//void GL_DrawBox( const vec3_t origin, vec3_t bounds[2] );

qboolean GL_AllocBlock( int width, int height, int *inuse,
    int w, int h, int *s, int *t );

void GL_ClearErrors( void );
qboolean GL_ShowErrors( const char *func );

/*
 * gl_model.c
 * 
 */

typedef struct maliastc_s {
    float st[2];
} maliastc_t;

typedef struct maliasvert_s {
    short pos[3];
    byte norm[2]; // lat, lng
} maliasvert_t;

typedef struct maliasframe_s {
    vec3_t scale;
    vec3_t translate;
    vec3_t bounds[2];
    vec_t radius;
} maliasframe_t;

typedef struct maliasmesh_s {
    int numverts;
    int numtris;
    int numindices;
    uint32_t *indices;
    maliasvert_t *verts;
    maliastc_t *tcoords;
    image_t *skins[MAX_ALIAS_SKINS];
    int numskins;
} maliasmesh_t;

// xyz[3] + st[2] + lmst[2]
// xyz[3] + color[4]
#define VERTEX_SIZE 7

/*
 * gl_surf.c
 * 
 */
#define DLIGHT_CUTOFF       64

#define LIGHT_STYLE(surf,i) \
    &glr.fd.lightstyles[gl_static.lightstylemap[(surf)->styles[i]]]

#define LM_MAX_LIGHTMAPS    32
#define LM_BLOCK_WIDTH      256
#define LM_BLOCK_HEIGHT     256

typedef struct {
    int inuse[LM_BLOCK_WIDTH];
    byte buffer[LM_BLOCK_WIDTH * LM_BLOCK_HEIGHT * 4];
    qboolean dirty;
    int nummaps;
    int highwater;
} lightmap_builder_t;

extern lightmap_builder_t lm;

void GL_AdjustColor( vec_t *color, int what );
void GL_BeginLights( void );
void GL_EndLights( void );
void GL_PushLights( mface_t *surf );

void LM_RebuildSurfaces( void );

void GL_LoadWorld( const char *name );
void GL_FreeWorld( void );

/*
 * gl_state.c
 *
 */
typedef enum {
    GLS_CULL_DISABLE,
    GLS_CULL_FRONT,
    GLS_CULL_BACK
} glCullFace_t;

typedef enum {
    GLS_DEFAULT             = 0,
    GLS_DEPTHMASK_FALSE     = ( 1 << 0 ),
    GLS_DEPTHTEST_DISABLE   = ( 1 << 1 ),
    GLS_BLEND_BLEND         = ( 1 << 2 ),
    GLS_BLEND_ADD           = ( 1 << 3 ),
    GLS_BLEND_MODULATE      = ( 1 << 4 ),
    GLS_ALPHATEST_ENABLE    = ( 1 << 5 )
} glStateBits_t;

#define GLS_BLEND_MASK  (GLS_BLEND_BLEND|GLS_BLEND_ADD|GLS_BLEND_MODULATE)

typedef struct {
    int tmu;
    int texnum[MAX_TMUS];
    GLenum texenv[MAX_TMUS];
    glStateBits_t bits;
    glCullFace_t cull;
    qboolean fp_enabled;
} glState_t;

extern glState_t gls;

void GL_BindTexture( int texnum );
void GL_SelectTMU( int tmu );
void GL_TexEnv( GLenum texenv );
void GL_CullFace( glCullFace_t cull );
void GL_Bits( glStateBits_t bits );
void GL_Setup2D( void );
void GL_Setup3D( void );

void GL_SetDefaultState( void );
void GL_InitPrograms( void );
void GL_ShutdownPrograms( void );
void GL_EnableWarp( void );
void GL_DisableWarp( void );
void GL_EnableOutlines( void );
void GL_DisableOutlines( void );


/*
 * gl_draw.c
 *
 */
typedef struct {
    color_t colors[2]; // 0 - actual color, 1 - transparency (for text drawing)
    int flags;
    float scale;
} drawStatic_t;

extern drawStatic_t draw;

#ifdef _DEBUG
void Draw_Stringf( int x, int y, const char *fmt, ... );
void Draw_Stats( void );
void Draw_Lightmaps( void );
void Draw_Scrap( void );
#endif

void GL_Blend( void );


/*
 * gl_images.c
 *
 */

#define LUMINANCE(r,g,b) ((r)*0.2126f + (g)*0.7152f + (b)*0.0722f)

enum {
    TEXNUM_DEFAULT = 1,
    TEXNUM_SCRAP = MAX_RIMAGES + 1,
    TEXNUM_PARTICLE,
    TEXNUM_BEAM,
    TEXNUM_WHITE,
    TEXNUM_BLACK,
    TEXNUM_LIGHTMAP // must be the last one
};

#define NUM_TEXNUMS (TEXNUM_LIGHTMAP+LM_MAX_LIGHTMAPS-TEXNUM_SCRAP)

extern image_t *r_notexture;

extern mtexinfo_t *upload_texinfo;

void Scrap_Upload( void );

void GL_InitImages( void );
void GL_ShutdownImages( void );

void GL_UpdateGammaTable( qboolean realTime );

image_t *R_ImageForHandle( qhandle_t hPic );
qhandle_t R_RegisterSkin( const char *name );
qhandle_t R_RegisterPic( const char *name );


/*
 * gl_tess.c
 *
 */
#define TESS_MAX_FACES      256
#define TESS_MAX_VERTICES   ( 16 * TESS_MAX_FACES )
#define TESS_MAX_INDICES    ( 3 * TESS_MAX_VERTICES )

typedef struct {
    vec_t vertices[VERTEX_SIZE*TESS_MAX_VERTICES];
    int indices[TESS_MAX_INDICES];
    byte colors[4*TESS_MAX_VERTICES];
    int texnum[MAX_TMUS];
    int numverts;
    int numindices;
    int flags;
} tesselator_t;

extern tesselator_t tess;

void GL_Flush2D( void );
void GL_DrawParticles( void );
void GL_DrawBeams( void );

void GL_AddFace( mface_t *face );
void GL_AddSolidFace( mface_t *face );
void GL_DrawAlphaFaces( void );
void GL_DrawSolidFaces( void );

/*
 * gl_world.c
 *
 */
void GL_DrawBspModel( mmodel_t *model );
void GL_DrawWorld( void );
void _R_LightPoint( vec3_t origin, vec3_t color );

/*
 * gl_sky.c
 *
 */
void R_AddSkySurface( mface_t *surf );
void R_ClearSkyBox( void );
void R_DrawSkyBox( void );
void R_SetSky( const char *name, float rotate, vec3_t axis );

/*
 * gl_mesh.c
 *
 */
void GL_DrawAliasModel( model_t *model );

