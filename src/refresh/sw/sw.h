/*
Copyright (C) 1997-2001 Id Software, Inc.

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
#include "client/client.h"
#include "client/video.h"
#include "refresh/refresh.h"
#include "refresh/images.h"
#include "refresh/models.h"
#include "system/system.h"

#define REF_VERSION     "SOFT 0.01"

/*
====================================================

  CONSTANTS

====================================================
*/

#define DSURF_SKY           2
#define DSURF_TURB          4
#define DSURF_BACKGROUND    8

#define CACHE_SIZE      32

#define VID_BYTES       4   // can be 4 or 3
#define TEX_BYTES       4   // must be 4


#define MAXVERTS        64              // max points in a surface polygon
#define MAXWORKINGVERTS (MAXVERTS + 4)  // max points in an intermediate
                                        // polygon (while processing)

#define MAXHEIGHT       1200
#define MAXWIDTH        1600

#define INFINITE_DISTANCE       0x10000     // distance that's always guaranteed to
                                            // be farther away than anything in
                                            // the scene

#define WARP_WIDTH      320
#define WARP_HEIGHT     240

#define PARTICLE_Z_CLIP 8.0

#define TRANSPARENT_COLOR       0xFF

#define TURB_SIZE               64  // base turbulent texture size
#define TURB_MASK               (TURB_SIZE - 1)

#define CYCLE                   128 // turbulent cycle size

#define DS_SPAN_LIST_END        -128

#define NUMSTACKEDGES           3000
#define MINEDGES                NUMSTACKEDGES
#define MAXEDGES                30000
#define NUMSTACKSURFACES        1000
#define MINSURFACES             NUMSTACKSURFACES
#define MAXSURFACES             10000
#define MAXSPANS                3000

// flags in finalvert_t.flags
#define ALIAS_LEFT_CLIP             0x0001
#define ALIAS_TOP_CLIP              0x0002
#define ALIAS_RIGHT_CLIP            0x0004
#define ALIAS_BOTTOM_CLIP           0x0008
#define ALIAS_Z_CLIP                0x0010
#define ALIAS_XY_CLIP_MASK          0x000F

#define SURFCACHE_SIZE_AT_320X240   1024*768

#define BMODEL_FULLY_CLIPPED    0x10    // value returned by R_BmodelCheckBBox ()
                                        // if bbox is trivially rejected

#define CLIP_EPSILON            0.001

#define BACKFACE_EPSILON        0.01

#define NEAR_CLIP       0.01

#define ALIAS_Z_CLIP_PLANE      4

// turbulence stuff
#define AMP     8*0x10000
#define AMP2    3
#define SPEED   20


/*
====================================================

TYPES

====================================================
*/

typedef unsigned char pixel_t;

typedef struct {
    pixel_t                 *buffer;                // invisible buffer
    int                     rowbytes;               // may be > width if displayed in a window
                                                    // can be negative for stupid dibs
    int                     width;
    int                     height;
} viddef_t;

typedef struct {
    vrect_t         vrect;                          // subwindow in video for refresh
    int             vrectright, vrectbottom;        // right & bottom screen coords
    float           vrectrightedge;                 // rightmost right edge we care about,
                                                    // for use in edge list
    float           fvrectx, fvrecty;               // for floating-point compares
    float           fvrectx_adj, fvrecty_adj;       // left and top edges, for clamping
    int             vrect_x_adj_shift20;            // (vrect.x + 0.5 - epsilon) << 20
    int             vrectright_adj_shift20;         // (vrectright + 0.5 - epsilon) << 20
    float           fvrectright_adj, fvrectbottom_adj;  // right and bottom edges, for clamping
    float           fvrectright;                    // rightmost edge, for Alias clamping
    float           fvrectbottom;                   // bottommost edge, for Alias clamping

    // values for perspective projection
    float           xcenter, ycenter;
    float           xscale, yscale;
    float           xscaleinv, yscaleinv;
    float           xscaleshrink, yscaleshrink;
    float           scale_for_mip;

    // particle values
    int             vrectright_particle, vrectbottom_particle;
    int             pix_min, pix_max, pix_shift;
} oldrefdef_t;

typedef struct {
    float   u, v;
    float   s, t;
    float   zi;
} emitpoint_t;

typedef struct finalvert_s {
    int     u, v, s, t;
    int     l;
    int     zi;
    int     flags;
    float   xyz[3];         // eye space
} finalvert_t;

typedef struct {
    void                *pskin;
    int                 skinwidth;
    int                 skinheight;
} affinetridesc_t;

typedef struct drawsurf_s {
    byte        *surfdat;       // destination for generated surface
    int         rowbytes;       // destination logical width in bytes
    mface_t     *surf;          // description for surface to generate
    fixed8_t    lightadj[MAX_LIGHTMAPS]; // adjust for lightmap levels for dynamic lighting
    image_t     *image;
    int         surfmip;        // mipmapped ratio of surface texels / world pixels
    int         surfwidth;      // in mipmapped texels
    int         surfheight;     // in mipmapped texels
} drawsurf_t;

// clipped bmodel edges
typedef struct bedge_s {
    mvertex_t       *v[2];
    struct bedge_s  *pnext;
} bedge_t;

typedef struct clipplane_s {
    vec3_t              normal;
    float               dist;
    struct clipplane_s  *next;
    byte                leftedge;
    byte                rightedge;
    byte                reserved[2];
} clipplane_t;

#define MAX_BLOCKLIGHTS 1024
#define LIGHTMAP_BYTES  3

typedef int blocklight_t;

typedef struct surfcache_s {
    struct surfcache_s      *next;
    struct surfcache_s      **owner;                // NULL is an empty chunk of memory
    int                     lightadj[MAX_LIGHTMAPS]; // checked for strobe flush
    int                     dlight;
    int                     size;           // including header
    unsigned                width;
    unsigned                height;         // DEBUG only needed for debug
    float                   mipscale;
    image_t                 *image;
    byte                    data[4];        // width*height elements
} surfcache_t;

typedef struct espan_s {
    int             u, v, count;
    struct espan_s  *pnext;
} espan_t;

// used by the polygon drawer (R_POLY.C) and sprite setup code (R_SPRITE.C)
typedef struct {
    int         nump;
    emitpoint_t *pverts;
    byte        *pixels;                // image
    int         pixel_width;            // image width
    int         pixel_height;           // image height
    vec3_t      vup, vright, vpn;       // in worldspace, for plane eq
    float       dist;
    float       s_offset, t_offset;
    float       viewer_position[3];
    void        (*drawspanlet)(void);
    int         alpha;
    int         one_minus_alpha;
} polydesc_t;

// FIXME: compress, make a union if that will help
// insubmodel is only 1, flags is fewer than 32, spanstate could be a byte
typedef struct surf_s {
    struct surf_s   *next;          // active surface stack in r_edge.c
    struct surf_s   *prev;          // used in r_edge.c for active surf stack
    struct espan_s  *spans;         // pointer to linked list of spans to draw
    int             key;            // sorting key (BSP order)
    int             last_u;         // set during tracing
    int             spanstate;      // 0 = not in span
                                    // 1 = in span
                                    // -1 = in inverted span (end before start)
    int             flags;          // currentface flags
    mface_t         *msurf;
    entity_t        *entity;
    float           nearzi;         // nearest 1/z on surface, for mipmapping
    qboolean        insubmodel;
    float           d_ziorigin, d_zistepu, d_zistepv;
} surf_t;

typedef struct edge_s {
    fixed16_t       u;
    fixed16_t       u_step;
    struct edge_s   *prev, *next;
    uint16_t        surfs[2];
    struct edge_s   *nextremove;
    float           nearzi;
    medge_t         *owner;
} edge_t;

typedef struct maliasst_s {
    signed short    s;
    signed short    t;
} maliasst_t;

typedef struct maliastri_s {
    unsigned short  index_xyz[3];
    unsigned short  index_st[3];
} maliastri_t;

typedef struct maliasvert_s {
    uint8_t    v[3];
    uint8_t    lightnormalindex;
} maliasvert_t;

typedef struct maliasframe_s {
    vec3_t          scale;
    vec3_t          translate;
    vec3_t          bounds[2];
    vec_t           radius;
    maliasvert_t    *verts;
} maliasframe_t;

typedef struct {
    finalvert_t *a, *b, *c;
} aliastriangleparms_t;


/*
====================================================

VARS

====================================================
*/

extern int      r_framecount;       // sequence # of current frame since Quake started
extern qboolean r_dowarp;

extern affinetridesc_t  r_affinetridesc;

void D_DrawSurfaces(void);
void D_WarpScreen(void);

//=======================================================================//

extern drawsurf_t       r_drawsurf;

extern int              c_surf;

extern byte             r_warpbuffer[WARP_WIDTH * WARP_HEIGHT * VID_BYTES];

extern float    d_sdivzstepu, d_tdivzstepu, d_zistepu;
extern float    d_sdivzstepv, d_tdivzstepv, d_zistepv;
extern float    d_sdivzorigin, d_tdivzorigin, d_ziorigin;

extern fixed16_t    sadjust, tadjust;
extern fixed16_t    bbextents, bbextentt;

void D_DrawTurbulent16(espan_t *pspan, int *warptable);
void D_DrawSpans16(espan_t *pspans);
void D_DrawZSpans(espan_t *pspans);

surfcache_t     *D_CacheSurface(mface_t *surface, int miplevel);

extern pixel_t  *d_viewbuffer;
extern int      d_screenrowbytes;
extern short    *d_pzbuffer;
extern int      d_zrowbytes;
extern int      d_zwidth;
extern byte     *d_spantable[MAXHEIGHT];
extern short    *d_zspantable[MAXHEIGHT];

extern int      d_minmip;
extern float    d_scalemip[3];

extern viddef_t         vid;
extern oldrefdef_t      r_refdef;

//===================================================================

extern int      cachewidth;
extern pixel_t  *cacheblock;

extern int      r_drawnpolycount;

extern int      sintable[CYCLE * 2];
extern int      intsintable[CYCLE * 2];
extern int      blanktable[CYCLE * 2];

extern vec3_t   vup, base_vup;
extern vec3_t   vpn, base_vpn;
extern vec3_t   vright, base_vright;

extern surf_t   *auxsurfaces;
extern surf_t   *surfaces, *surface_p, *surf_max;

// surfaces are generated in back to front order by the bsp, so if a surf
// pointer is greater than another one, it should be drawn in front
// surfaces[1] is the background, and is used as the active surface stack.
// surfaces[0] is a dummy, because index 0 is used to indicate no surface
//  attached to an edge_t

//===================================================================

extern vec3_t   sxformaxis[4];  // s axis transformed into viewspace
extern vec3_t   txformaxis[4];  // t axis transformed into viewspac

extern void R_TransformVector(vec3_t in, vec3_t out);

//===========================================================================

extern cvar_t   *sw_aliasstats;
extern cvar_t   *sw_clearcolor;
extern cvar_t   *sw_drawflat;
extern cvar_t   *sw_draworder;
extern cvar_t   *sw_maxedges;
extern cvar_t   *sw_maxsurfs;
extern cvar_t   *sw_mipcap;
extern cvar_t   *sw_mipscale;
extern cvar_t   *sw_mode;
extern cvar_t   *sw_reportsurfout;
extern cvar_t   *sw_reportedgeout;
extern cvar_t   *sw_surfcacheoverride;
extern cvar_t   *sw_waterwarp;
extern cvar_t   *sw_drawsird;
extern cvar_t   *sw_dynamic;
extern cvar_t   *sw_modulate;

extern cvar_t   *r_fullbright;
extern cvar_t   *r_drawentities;
extern cvar_t   *r_drawworld;
extern cvar_t   *r_lerpmodels;

extern cvar_t   *r_speeds;

extern cvar_t   *vid_fullscreen;
extern cvar_t   *vid_gamma;


extern clipplane_t  view_clipplanes[4];
extern int          *pfrustum_indexes[4];


//=============================================================================

void R_RenderWorld(void);

//=============================================================================

extern cplane_t     screenedge[4];

extern vec3_t       r_origin;

extern entity_t     r_worldentity;
extern model_t      *currentmodel;
extern entity_t     *currententity;
extern vec3_t       modelorg;
extern vec3_t       r_entorigin;
extern vec3_t       entity_rotation[3];

extern int          r_visframecount;

extern mface_t      *r_alpha_surfaces;

//=============================================================================

//
// current entity info
//
extern qboolean     insubmodel;

void R_DrawAlphaSurfaces(void);

void R_DrawSprite(void);
void R_DrawBeam(entity_t *e);

void R_RenderFace(mface_t *fa, int clipflags);
void R_RenderBmodelFace(bedge_t *pedges, mface_t *psurf);
void R_TransformPlane(cplane_t *p, float *normal, float *dist);
void R_TransformFrustum(void);

void R_DrawSubmodelPolygons(mmodel_t *pmodel, int clipflags, mnode_t *topnode);
void R_DrawSolidClippedSubmodelPolygons(mmodel_t *pmodel, mnode_t *topnode);

void R_AliasDrawModel(void);
void R_BeginEdgeFrame(void);
void R_ScanEdges(void);
void R_MarkLights(mnode_t *headnode);

void R_RotateBmodel(void);

extern int      c_faceclip;
extern int      r_polycount;
extern int      r_wholepolycount;

extern fixed16_t    sadjust, tadjust;
extern fixed16_t    bbextents, bbextentt;

extern mvertex_t    *r_ptverts, *r_ptvertsmax;

extern int          r_currentkey;
extern int          r_currentbkey;

void R_DrawParticles(void);

extern int          r_amodels_drawn;
extern edge_t       *auxedges;
extern int          r_numallocatededges;
extern edge_t       *r_edges, *edge_p, *edge_max;

extern edge_t   *newedges[MAXHEIGHT];
extern edge_t   *removeedges[MAXHEIGHT];

extern fixed8_t r_aliasblendcolor[3];

extern int      r_alias_alpha;
extern int      r_alias_one_minus_alpha;

extern int      r_outofsurfaces;
extern int      r_outofedges;

extern int      r_maxvalidedgeoffset;

extern aliastriangleparms_t aliastriangleparms;

void R_DrawTriangle(void);
void R_AliasClipTriangle(finalvert_t *index0, finalvert_t *index1, finalvert_t *index2);
void R_AliasProjectAndClipTestFinalVert(finalvert_t *fv);

extern float    r_time1;
extern int      r_frustum_indexes[4 * 6];
extern int      r_maxsurfsseen, r_maxedgesseen, r_cnumsurfs;
extern qboolean r_surfsonstack;

extern mleaf_t  *r_viewleaf;
extern int      r_viewcluster, r_oldviewcluster;

extern int      r_clipflags;
extern int      r_dlightframecount;

extern bsp_t    *r_worldmodel;

extern blocklight_t     blocklights[MAX_BLOCKLIGHTS * LIGHTMAP_BYTES];   // allow some very large lightmaps

void R_PrintAliasStats(void);
void R_PrintTimes(void);
void R_LightPoint(vec3_t p, vec3_t color);
void R_SetupFrame(void);
void R_BuildLightMap(void);

extern refdef_t     r_newrefdef;

//====================================================================

void R_NewMap(void);

void R_InitCaches(void);
void R_FreeCaches(void);
void D_FlushCaches(void);
void D_SCDump_f(void);

void R_InitTurb(void);

void R_InitImages(void);
void R_ShutdownImages(void);

void R_BuildGammaTable(void);

void R_InitSkyBox(void);
void R_EmitSkyBox(void);

void R_ApplySIRDAlgorithum(void);

void R_IMFlatShadedQuad(vec3_t a, vec3_t b, vec3_t c, vec3_t d, color_t color, float alpha);

void R_InitDraw(void);

