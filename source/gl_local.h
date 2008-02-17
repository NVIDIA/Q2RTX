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

#include "config.h"
#include "qgl_local.h"
#include "q_shared.h"
#include "q_files.h"
#include "com_public.h"
#include "ref_public.h"
#include "in_public.h"
#include "vid_public.h"
#include "cl_public.h"
#include "q_list.h"
#include "r_shared.h"
#include "qgl_api.h"

/*
 * gl_main.c
 * 
 */

#define MAX_TMUS        2

typedef struct {
    int numTextureUnits;
    int maxTextureSize;
	qboolean registering;
	uint32_t palette[256]; // cinematic palette
    GLuint prog_warp, prog_light;
    vec_t *vertices;
} glStatic_t;

typedef struct {
    refdef_t fd;
    vec3_t viewaxis[3];
    int visframe;
    int drawframe;
    int viewcluster1;
	int viewcluster2;
    cplane_t frustumPlanes[4];
	entity_t	*ent;
	vec3_t		entaxis[3];
	qboolean	entrotated;
	float	scroll;
	int		num_beams;
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
    int trisDrawn;
    int batchesDrawn;
    int nodesCulled;
    int facesCulled;
	int boxesCulled;
	int spheresCulled;
	int rotatedBoxesCulled;
} statCounters_t;

extern statCounters_t c;

extern cvar_t *gl_celshading;
extern cvar_t *gl_partscale;
extern cvar_t *gl_znear;
extern cvar_t *gl_zfar;
extern cvar_t *gl_modulate;
extern cvar_t *gl_showtris;
extern cvar_t *gl_cull_nodes;
extern cvar_t *gl_bind;
extern cvar_t *gl_clear;
extern cvar_t *gl_novis;
extern cvar_t *gl_lockpvs;
extern cvar_t *gl_lightmap;
extern cvar_t *gl_fastsky;
extern cvar_t *gl_dynamic;
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

void GL_RotateForEntity( void );

void GL_DrawBox( const vec3_t origin, vec3_t bounds[2] );

void GL_ShowErrors( const char *func );

/*
 * gl_model.c
 * 
 */

typedef struct tcoord_s {
    float st[2];
} tcoord_t;

typedef struct aliasVert_s {
	short pos[3];
	byte normalIndex;
	byte pad;
} aliasVert_t;

typedef struct aliasFrame_s {
	vec3_t scale;
	vec3_t translate;
	vec3_t bounds[2];
	vec_t radius;
} aliasFrame_t;

typedef struct aliasMesh_s {
	int numVerts;
	int numTris;
	int numIndices;
	uint32_t *indices;
	aliasVert_t *verts;
	tcoord_t *tcoords;
	image_t *skins[MAX_MD2SKINS];
	int numSkins;
} aliasMesh_t;

typedef struct spriteFrame_s {
	int width, height;
	int x, y;
	image_t *image;
} spriteFrame_t;

typedef struct model_s {
	modelType_t type;

	char name[MAX_QPATH];
	int registration_sequence;
	mempool_t pool;

	/* alias models */
	int numFrames;
	int numMeshes;
	aliasFrame_t *frames;
	aliasMesh_t *meshes;

	/* sprite models */
	spriteFrame_t *sframes;
} model_t;

/* xyz[3] + st[2] + lmst[2] */
#define VERTEX_SIZE 7

void GL_InitModels( void );
void GL_ShutdownModels( void );
void GL_GetModelSize( qhandle_t hModel, vec3_t mins, vec3_t maxs );
qhandle_t GL_RegisterModel( const char *name );
modelType_t *GL_ModelForHandle( qhandle_t hModel );

void Model_FreeUnused( void );
void Model_FreeAll( void );

/*
 * gl_surf.c
 * 
 */
#define LM_MAX_LIGHTMAPS    32
#define LM_BLOCK_WIDTH      256
#define LM_BLOCK_HEIGHT     256
#define LM_TEXNUM			( MAX_RIMAGES + 2 )

#define NOLIGHT_MASK \
    (SURF_SKY|SURF_WARP|SURF_TRANS33|SURF_TRANS66)

typedef struct {
    int inuse[LM_BLOCK_WIDTH];
    byte buffer[LM_BLOCK_WIDTH * LM_BLOCK_HEIGHT * 4];
    int numMaps;
    int highWater;
} lightmapBuilder_t;

extern lightmapBuilder_t lm;

void GL_PostProcessSurface( bspSurface_t *surf );
void GL_BeginPostProcessing( void );
void GL_EndPostProcessing( void );

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

#define GLS_BLEND_MASK	(GLS_BLEND_BLEND|GLS_BLEND_ADD|GLS_BLEND_MODULATE)

typedef struct {
    int tmu;
    int texnum[MAX_TMUS];
    GLenum texenv[MAX_TMUS];
    glStateBits_t bits;
    glCullFace_t cull;
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
    color_t color;
	int flags;
	float scale;
} drawStatic_t;

extern drawStatic_t	draw;

qhandle_t GL_RegisterFont( const char *name );

void Draw_SetColor( int flags, const color_t color );
void Draw_SetClipRect( int flags, const clipRect_t *clip );
void Draw_SetScale( float *scale );
void Draw_GetPicSize( int *w, int *h, qhandle_t hPic );
void Draw_GetFontSize( int *w, int *h, qhandle_t hFont );
void Draw_StretchPicST( int x, int y, int w, int h, float s1, float t1,
        float s2, float t2, qhandle_t hPic );
void Draw_StretchPic( int x, int y, int w, int h, qhandle_t hPic );
void Draw_Pic( int x, int y, qhandle_t hPic );
void Draw_StretchRaw( int x, int y, int w, int h, int cols,
        int rows, const byte *data );
void Draw_TileClear( int x, int y, int w, int h, qhandle_t hPic );
void Draw_Fill( int x, int y, int w, int h, int c );
void Draw_FillEx( int x, int y, int w, int h, const color_t color );
void Draw_FadeScreen( void );
void Draw_Char( int x, int y, int flags, int ch, qhandle_t hFont );
int  Draw_String( int x, int y, int flags, int maxChars,
        const char *string, qhandle_t hFont );
void Draw_Stringf( int x, int y, const char *fmt, ... );
void Draw_Stats( void );


/*
 * gl_images.c
 *
 */
extern image_t *r_notexture;
extern image_t *r_particletexture;
extern image_t *r_beamtexture;
extern image_t *r_warptexture;
extern image_t *r_whiteimage;

extern int gl_filter_min;
extern int gl_filter_max;
extern float gl_filter_anisotropy;
extern int gl_tex_alpha_format;
extern int gl_tex_solid_format;

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
    int numVertices;
    int numIndices;
	int flags;
} tesselator_t;

extern tesselator_t tess;

void EndSurface_Multitextured( void );
void EndSurface_Single( void );

void Tess_DrawSurfaceTriangles( int *indices, int numIndices );

void GL_StretchPic( float x, float y, float w, float h,
        float s1, float t1, float s2, float t2, const byte *color, image_t *image );
void GL_Flush2D( void );
void GL_DrawParticles( void );
void GL_DrawBeams( void );

void GL_AddFace( bspSurface_t *face );
void GL_AddSolidFace( bspSurface_t *face );
void GL_DrawAlphaFaces( void );
void GL_DrawSolidFaces( void );

/*
 * gl_world.c
 *
 */
extern vec3_t modelViewOrigin;

void GL_MarkLeaves( void );
void GL_MarkLights( void );
void GL_DrawBspModel( bspSubmodel_t *model );
void GL_DrawWorld( void );
void GL_LightPoint( vec3_t origin, vec3_t dest );

/*
 * gl_sky.c
 *
 */
void R_AddSkySurface( bspSurface_t *surf );
void R_ClearSkyBox( void );
void R_DrawSkyBox( void );
void R_SetSky( const char *name, float rotate, vec3_t axis );

/*
 * gl_mesh.c
 *
 */
void GL_DrawAliasModel( model_t *model );

