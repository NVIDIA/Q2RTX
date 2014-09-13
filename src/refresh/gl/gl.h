/*
Copyright (C) 2003-2006 Andrey Nazarov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "shared/shared.h"
#include "common/bsp.h"
#include "common/cmd.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "common/math.h"
#include "client/video.h"
#include "client/client.h"
#include "refresh/refresh.h"
#include "refresh/images.h"
#include "refresh/models.h"
#include "system/hunk.h"

#if USE_FIXED_LIBGL
#include "qgl/fixed.h"
#else
#include "qgl/dynamic.h"
#endif

/*
 * gl_main.c
 *
 */

#ifdef GL_VERSION_ES_CM_1_0
#define QGL_INDEX_TYPE  GLushort
#define QGL_INDEX_ENUM  GL_UNSIGNED_SHORT
#else
#define QGL_INDEX_TYPE  GLuint
#define QGL_INDEX_ENUM  GL_UNSIGNED_INT
#endif

#define MAX_TMUS        2

#define TAB_SIN(x) gl_static.sintab[(x) & 255]
#define TAB_COS(x) gl_static.sintab[((x) + 64) & 255]

#define NUM_TEXNUMS     6

typedef struct {
    qboolean        registering;
    struct {
        bsp_t       *cache;
        memhunk_t   hunk;
        vec_t       *vertices;
        GLuint      bufnum;
        vec_t       size;
    } world;
    GLuint          prognum_warp;
    GLuint          texnums[NUM_TEXNUMS];
    GLbitfield      stencil_buffer_bit;
    float           entity_modulate;
    uint32_t        inverse_intensity_33;
    uint32_t        inverse_intensity_66;
    uint32_t        inverse_intensity_100;
    float           sintab[256];
    byte            latlngtab[NUMVERTEXNORMALS][2];
    byte            lightstylemap[MAX_LIGHTSTYLES];
} glStatic_t;

typedef struct {
    refdef_t        fd;
    vec3_t          viewaxis[3];
    GLfloat         viewmatrix[16];
    int             visframe;
    int             drawframe;
#if USE_DLIGHTS
    int             dlightframe;
#endif
    int             viewcluster1;
    int             viewcluster2;
    cplane_t        frustumPlanes[4];
    entity_t        *ent;
    qboolean        entrotated;
    vec3_t          entaxis[3];
    GLfloat         entmatrix[16];
    lightpoint_t    lightpoint;
    int             num_beams;
} glRefdef_t;

typedef struct {
    qboolean    es_profile;

    int     version_major;
    int     version_minor;

    unsigned    ext_supported;
    unsigned    ext_enabled;

    int         maxTextureSize;
    int         numTextureUnits;
    float       maxAnisotropy;

    int         colorbits;
    int         depthbits;
    int         stencilbits;
} glConfig_t;

#define AT_LEAST_OPENGL_ANY(major, minor) \
    (gl_config.version_major > major || (gl_config.version_major == major && gl_config.version_minor >= minor))

#define AT_LEAST_OPENGL(major, minor) \
    (!gl_config.es_profile && AT_LEAST_OPENGL_ANY(major, minor))

#define AT_LEAST_OPENGL_ES(major, minor) \
    (gl_config.es_profile && AT_LEAST_OPENGL_ANY(major, minor))

extern glStatic_t gl_static;
extern glConfig_t gl_config;
extern glRefdef_t glr;

extern entity_t gl_world;

typedef struct {
    int nodesVisible;
    int nodesDrawn;
    int leavesDrawn;
    int facesMarked;
    int facesDrawn;
    int facesTris;
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

// regular variables
extern cvar_t *gl_partscale;
extern cvar_t *gl_partstyle;
extern cvar_t *gl_celshading;
extern cvar_t *gl_dotshading;
extern cvar_t *gl_shadows;
extern cvar_t *gl_modulate;
extern cvar_t *gl_modulate_world;
extern cvar_t *gl_coloredlightmaps;
extern cvar_t *gl_brightness;
extern cvar_t *gl_dynamic;
#if USE_DLIGHTS
extern cvar_t *gl_dlight_falloff;
#endif
extern cvar_t *gl_modulate_entities;
extern cvar_t *gl_doublelight_entities;
extern cvar_t *gl_fragment_program;
extern cvar_t *gl_fontshadow;

// development variables
extern cvar_t *gl_znear;
extern cvar_t *gl_drawsky;
extern cvar_t *gl_showtris;
#ifdef _DEBUG
extern cvar_t *gl_nobind;
extern cvar_t *gl_test;
#endif
extern cvar_t *gl_cull_nodes;
extern cvar_t *gl_hash_faces;
extern cvar_t *gl_clear;
extern cvar_t *gl_novis;
extern cvar_t *gl_lockpvs;
extern cvar_t *gl_lightmap;
extern cvar_t *gl_fullbright;
extern cvar_t *gl_vertexlight;

typedef enum {
    CULL_OUT,
    CULL_IN,
    CULL_CLIP
} glCullResult_t;

glCullResult_t GL_CullBox(vec3_t bounds[2]);
glCullResult_t GL_CullSphere(const vec3_t origin, float radius);
glCullResult_t GL_CullLocalBox(const vec3_t origin, vec3_t bounds[2]);

//void GL_DrawBox(const vec3_t origin, vec3_t bounds[2]);

qboolean GL_AllocBlock(int width, int height, int *inuse,
                       int w, int h, int *s, int *t);

void GL_MultMatrix(GLfloat *out, const GLfloat *a, const GLfloat *b);
void GL_RotateForEntity(vec3_t origin);

void GL_ClearErrors(void);
qboolean GL_ShowErrors(const char *func);

/*
 * gl_model.c
 *
 */

typedef struct maliastc_s {
    float   st[2];
} maliastc_t;

typedef struct masliasvert_s {
    short   pos[3];
    byte    norm[2]; // lat, lng
} maliasvert_t;

typedef struct maliasframe_s {
    vec3_t  scale;
    vec3_t  translate;
    vec3_t  bounds[2];
    vec_t   radius;
} maliasframe_t;

typedef struct maliasmesh_s {
    int             numverts;
    int             numtris;
    int             numindices;
    QGL_INDEX_TYPE  *indices;
    maliasvert_t    *verts;
    maliastc_t      *tcoords;
    image_t         *skins[MAX_ALIAS_SKINS];
    int             numskins;
} maliasmesh_t;

// xyz[3] | color[1]  | st[2]    | lmst[2]
// xyz[3] | unused[1] | color[4]
#define VERTEX_SIZE 8

/*
 * gl_surf.c
 *
 */
#define LIGHT_STYLE(surf, i) \
    &glr.fd.lightstyles[gl_static.lightstylemap[(surf)->styles[i]]]

#define LM_MAX_LIGHTMAPS    32
#define LM_BLOCK_WIDTH      256
#define LM_BLOCK_HEIGHT     256

typedef struct {
    int         inuse[LM_BLOCK_WIDTH];
    byte        buffer[LM_BLOCK_WIDTH * LM_BLOCK_HEIGHT * 4];
    qboolean    dirty;
    int         comp;
    float       add, modulate, scale;
    int         nummaps;
    GLuint      texnums[LM_MAX_LIGHTMAPS];
} lightmap_builder_t;

extern lightmap_builder_t lm;

void GL_AdjustColor(vec3_t color);
void GL_PushLights(mface_t *surf);

void GL_RebuildLighting(void);
void GL_FreeWorld(void);
void GL_LoadWorld(const char *name);

/*
 * gl_state.c
 *
 */
typedef enum {
    GLS_DEFAULT             = 0,
    GLS_DEPTHMASK_FALSE     = (1 << 0),
    GLS_DEPTHTEST_DISABLE   = (1 << 1),
    GLS_BLEND_BLEND         = (1 << 2),
    GLS_BLEND_ADD           = (1 << 3),
    GLS_BLEND_MODULATE      = (1 << 4),
    GLS_ALPHATEST_ENABLE    = (1 << 5),
    GLS_TEXTURE_REPLACE     = (1 << 6),
    GLS_FLOW_ENABLE         = (1 << 7),
    GLS_LIGHTMAP_ENABLE     = (1 << 8),
    GLS_WARP_ENABLE         = (1 << 9),
    GLS_CULL_DISABLE        = (1 << 10),
    GLS_SHADE_SMOOTH        = (1 << 11)
} glStateBits_t;

#define GLS_BLEND_MASK  (GLS_BLEND_BLEND | GLS_BLEND_ADD | GLS_BLEND_MODULATE)

typedef enum {
    GLA_NONE        = 0,
    GLA_VERTEX      = (1 << 0),
    GLA_TC          = (1 << 1),
    GLA_LMTC        = (1 << 2),
    GLA_COLOR       = (1 << 3),
} glArrayBits_t;

typedef struct {
    GLuint          client_tmu;
    GLuint          server_tmu;
    GLuint          texnums[MAX_TMUS];
    glStateBits_t   state_bits;
    glArrayBits_t   array_bits;
    const GLfloat   *currentmatrix;
} glState_t;

extern glState_t gls;

static inline void GL_ActiveTexture(GLuint tmu)
{
    if (gls.server_tmu != tmu) {
        qglActiveTextureARB(GL_TEXTURE0_ARB + tmu);
        gls.server_tmu = tmu;
    }
}

static inline void GL_ClientActiveTexture(GLuint tmu)
{
    if (gls.client_tmu != tmu) {
        qglClientActiveTextureARB(GL_TEXTURE0_ARB + tmu);
        gls.client_tmu = tmu;
    }
}

static inline void GL_VertexPointer(GLint size, GLsizei stride, const GLfloat *pointer)
{
    qglVertexPointer(size, GL_FLOAT, sizeof(GLfloat) * stride, pointer);
}

static inline void GL_TexCoordPointer(GLint size, GLsizei stride, const GLfloat *pointer)
{
    GL_ClientActiveTexture(0);
    qglTexCoordPointer(size, GL_FLOAT, sizeof(GLfloat) * stride, pointer);
}

static inline void GL_LightCoordPointer(GLint size, GLsizei stride, const GLfloat *pointer)
{
    GL_ClientActiveTexture(1);
    qglTexCoordPointer(size, GL_FLOAT, sizeof(GLfloat) * stride, pointer);
}

static inline void GL_ColorBytePointer(GLint size, GLsizei stride, const GLubyte *pointer)
{
    qglColorPointer(size, GL_UNSIGNED_BYTE, sizeof(GLfloat) * stride, pointer);
}

static inline void GL_ColorFloatPointer(GLint size, GLsizei stride, const GLfloat *pointer)
{
    qglColorPointer(size, GL_FLOAT, sizeof(GLfloat) * stride, pointer);
}

static inline void GL_LockArrays(GLsizei count)
{
    if (qglLockArraysEXT) {
        qglLockArraysEXT(0, count);
    }
}

static inline void GL_UnlockArrays(void)
{
    if (qglUnlockArraysEXT) {
        qglUnlockArraysEXT();
    }
}

static inline void GL_LoadMatrix(const GLfloat *matrix)
{
    if (gls.currentmatrix != matrix) {
        qglLoadMatrixf(matrix);
        gls.currentmatrix = matrix;
    }
}

void GL_ForceTexture(GLuint tmu, GLuint texnum);
void GL_BindTexture(GLuint tmu, GLuint texnum);
void GL_StateBits(glStateBits_t bits);
void GL_ArrayBits(glArrayBits_t bits);
void GL_LoadMatrix(const GLfloat *matrix);
void GL_Ortho(GLfloat xmin, GLfloat xmax, GLfloat ymin, GLfloat ymax, GLfloat znear, GLfloat zfar);
void GL_Setup2D(void);
void GL_Setup3D(void);
void GL_SetDefaultState(void);
void GL_InitPrograms(void);
void GL_ShutdownPrograms(void);
void GL_EnableOutlines(void);
void GL_DisableOutlines(void);

/*
 * gl_draw.c
 *
 */
typedef struct {
    color_t     colors[2]; // 0 - actual color, 1 - transparency (for text drawing)
    qboolean    scissor;
    float       scale;
} drawStatic_t;

extern drawStatic_t draw;

#ifdef _DEBUG
void Draw_Stringf(int x, int y, const char *fmt, ...);
void Draw_Stats(void);
void Draw_Lightmaps(void);
void Draw_Scrap(void);
#endif

void GL_Blend(void);


/*
 * gl_images.c
 *
 */

// auto textures
#define TEXNUM_DEFAULT  gl_static.texnums[0]
#define TEXNUM_SCRAP    gl_static.texnums[1]
#define TEXNUM_PARTICLE gl_static.texnums[2]
#define TEXNUM_BEAM     gl_static.texnums[3]
#define TEXNUM_WHITE    gl_static.texnums[4]
#define TEXNUM_BLACK    gl_static.texnums[5]

void Scrap_Upload(void);

void GL_InitImages(void);
void GL_ShutdownImages(void);


/*
 * gl_tess.c
 *
 */
#define TESS_MAX_VERTICES   4096
#define TESS_MAX_INDICES    (3 * TESS_MAX_VERTICES)

typedef struct {
    GLfloat         vertices[VERTEX_SIZE * TESS_MAX_VERTICES];
    QGL_INDEX_TYPE  indices[TESS_MAX_INDICES];
    GLubyte         colors[4 * TESS_MAX_VERTICES];
    GLuint          texnum[MAX_TMUS];
    int             numverts;
    int             numindices;
    int             flags;
} tesselator_t;

extern tesselator_t tess;

void GL_Flush2D(void);
void GL_DrawParticles(void);
void GL_DrawBeams(void);

void GL_BindArrays(void);
void GL_Flush3D(void);
void GL_DrawFace(mface_t *surf);

void GL_AddAlphaFace(mface_t *face);
void GL_AddSolidFace(mface_t *face);
void GL_DrawAlphaFaces(void);
void GL_DrawSolidFaces(void);
void GL_ClearSolidFaces(void);

/*
 * gl_world.c
 *
 */
void GL_DrawBspModel(mmodel_t *model);
void GL_DrawWorld(void);
void GL_SampleLightPoint(vec3_t color);
void GL_LightPoint(vec3_t origin, vec3_t color);

/*
 * gl_sky.c
 *
 */
void R_AddSkySurface(mface_t *surf);
void R_ClearSkyBox(void);
void R_DrawSkyBox(void);
void R_SetSky(const char *name, float rotate, vec3_t axis);

/*
 * gl_mesh.c
 *
 */
void GL_DrawAliasModel(model_t *model);

/*
 * hq2x.c
 *
 */
void HQ2x_Render(uint32_t *output, const uint32_t *input, int width, int height);
void HQ4x_Render(uint32_t *output, const uint32_t *input, int width, int height);
void HQ2x_Init(void);
