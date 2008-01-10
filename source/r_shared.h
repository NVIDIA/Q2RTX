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

#ifdef SOFTWARE_RENDERER
#ifdef TRUECOLOR_RENDERER
#define VID_BPP 32
#define VID_BYTES 4
#define VID_SHIFT 2
#define VID_IS32BIT	1
#else
#define VID_BPP 8
#define VID_BYTES 1
#define VID_SHIFT 0
#define VID_IS32BIT	0
#endif
#endif

#ifdef USE_BGRA_FORMAT
#define MakeColor( r, g, b, a )		MakeLong( b, g, r, a )
#else
#define MakeColor( r, g, b, a )		MakeLong( r, g, b, a )
#endif

// absolute limit for OpenGL renderer
#define MAX_TEXTURE_SIZE			2048

/*

  skins will be outline flood filled and mip mapped
  pics and sprites with alpha will be outline flood filled
  pic won't be mip mapped

  model skin
  sprite frame
  wall texture
  pic

*/

typedef enum {
	if_transparent	= ( 1 << 0 ),
	if_paletted		= ( 1 << 1 ),
	if_scrap		= ( 1 << 2 ),
	if_replace_wal  = ( 1 << 3 ),
	if_replace_pcx  = ( 1 << 4 ),
	if_auto         = ( 1 << 5 )
} imageflags_t;

typedef enum {
	it_skin,
	it_sprite,
	it_wall,
	it_pic,
	it_sky,
	it_charset,
    it_tmp
} imagetype_t;

#define EXTENSION_PNG	MakeLong( '.', 'p', 'n', 'g' )
#define EXTENSION_TGA	MakeLong( '.', 't', 'g', 'a' )
#define EXTENSION_JPG	MakeLong( '.', 'j', 'p', 'g' )
#define EXTENSION_PCX	MakeLong( '.', 'p', 'c', 'x' )
#define EXTENSION_WAL	MakeLong( '.', 'w', 'a', 'l' )

typedef struct image_s {
	list_t	entry;
	char	name[MAX_QPATH];			// game path, without extension
	int		baselength;					// length of the path without extension
	imagetype_t	type;
	int		width, height;				// source image
	int		upload_width, upload_height;	// after power of two and picmip
	int		registration_sequence;		// 0 = free
#ifdef OPENGL_RENDERER
	uint32	texnum;						// gl texture binding
	float	sl, sh, tl, th;
#elif SOFTWARE_RENDERER
	byte		*pixels[4];				// mip levels
#else
#error Neither OPENGL_RENDERER nor SOFTWARE_RENDERER defined
#endif
	imageflags_t flags;
} image_t;


#define MAX_RIMAGES		1024
#define RIMAGES_HASH	256

extern image_t		r_images[MAX_RIMAGES];
extern list_t		r_imageHash[RIMAGES_HASH];
extern int			r_numImages;

extern uint32		d_8to24table[256];

#define R_Malloc( size )	com.TagMalloc( size, TAG_RENDERER )

/* these are implemented in r_images.c */
image_t	*R_FindImage( const char *name, imagetype_t type );
image_t *R_AllocImage( const char *name );
image_t *R_CreateImage( const char *name, byte *pic, int width, int height,
					   imagetype_t type, imageflags_t flags );
void R_FreeUnusedImages( void );
void R_FreeAllImages( void );
void R_InitImageManager( void );
void R_ShutdownImageManager( void );
void R_ResampleTexture( const byte *in, int inwidth, int inheight, byte *out,
					   int outwidth, int outheight );
void R_GetPalette( byte **dest );

void Image_LoadPCX( const char *filename, byte **pic, byte *palette, int *width, int *height );
void Image_LoadTGA( const char *filename, byte **pic, int *width, int *height );
qboolean Image_WriteTGA( const char *filename, const byte *rgb, int width, int height );
#if USE_JPEG
void Image_LoadJPG( const char *filename, byte **pic, int *width, int *height );
qboolean Image_WriteJPG( const char *filename, const byte *rgb, int width, int height, int quality );
#endif
#if USE_PNG
void Image_LoadPNG( const char *filename, byte **pic, int *width, int *height );
qboolean Image_WritePNG( const char *filename, const byte *rgb, int width, int height, int compression ); 
#endif

/* these should be implemented by renderer library itself */
void R_FreeImage( image_t *image );
void R_LoadImage( image_t *image, byte *pic, int width, int height, imagetype_t type, imageflags_t flags );
image_t *R_LoadWal( const char *name );

extern int registration_sequence;
extern image_t *r_notexture;

//
// BSP MODELS
//

#ifdef SOFTWARE_RENDERER

// FIXME: differentiate from texinfo SURF_ flags
#define	SURF_PLANEBACK		0x02
#define	SURF_DRAWSKY		0x04		// sky brush face
#define SURF_FLOW			0x08		//PGM
#define SURF_DRAWTURB		0x10
#define SURF_DRAWBACKGROUND	0x40
#define SURF_DRAWSKYBOX		0x80		// sky box

#define EXTRA_SURFACES	6
#define EXTRA_VERTICES	8
#define EXTRA_EDGES		12
#define EXTRA_SURFEDGES	24

#else // SOFTWARE_RENDERER

#define EXTRA_SURFACES	0
#define EXTRA_VERTICES	0
#define EXTRA_EDGES		0
#define EXTRA_SURFEDGES	0

#endif // !SOFTWARE_RENDERER

typedef struct bspTexinfo_s {
	char name[MAX_QPATH];
	uint32 contents;
	uint32 flags;
	vec3_t axis[2];
#ifdef OPENGL_RENDERER
//    vec3_t normalizedAxis[2];
#endif
	vec2_t offset;
	int numFrames;
	struct bspTexinfo_s *animNext;
	image_t *image;
} bspTexinfo_t;

#ifdef OPENGL_RENDERER
typedef enum {
    DSURF_POLY,
    DSURF_WARP,
    DSURF_NOLM,
    DSURF_SKY,

    DSURF_NUM_TYPES
} drawSurfType_t;
#endif

typedef struct bspSurface_s {
#ifdef OPENGL_RENDERER
/* ======> */
    drawSurfType_t  type;
/* <====== */
#endif
    
	int index;
	
	vec3_t origin;
	vec3_t mins;
	vec3_t maxs;
	
	bspTexinfo_t *texinfo;
    byte *lightmap;
    int *firstSurfEdge;
    int numSurfEdges;

    cplane_t *plane;
	int side;

	int texturemins[2];
	int extents[2];

#ifdef OPENGL_RENDERER
    int texnum[2];
    int texflags;
    
    int firstVert;
    int numVerts; // FIXME: duplicates numSurfEdges
    int numIndices;
    
	int drawframe;	
    int dlightframe;
    int dlightbits;

    vec_t *vertices; // used for sky surfaces only

	struct bspSurface_s *next;
#endif
} bspSurface_t;

typedef struct bspNode_s {
/* ======> */
	cplane_t *plane; // never NULL
	int index;
	
	vec3_t mins;
	vec3_t maxs;

	int visframe;

	struct bspNode_s *parent;
/* <====== */
	
	int numFaces;
	bspSurface_t *firstFace;

	struct bspNode_s *children[2];
} bspNode_t;

typedef struct bspLeaf_s {
/* ======> */
	cplane_t *plane;	// always NULL
	int index;
	
	vec3_t mins;
	vec3_t maxs;

	int visframe;

	struct bspNode_s *parent;
/* <====== */
	
	int cluster;
	int area;
	int contents;

	int numLeafFaces;
	bspSurface_t **firstLeafFace;
} bspLeaf_t;

typedef enum {
	MODEL_NULL,
	MODEL_BSP,
	MODEL_ALIAS,
	MODEL_SPRITE
} modelType_t;

typedef struct bspSubmodel_s {
	modelType_t type;

	vec3_t	mins;
	vec3_t	maxs;
	float	radius;
	
	vec3_t	origin;

	int numFaces;
	bspSurface_t *firstFace;

	bspNode_t	*headnode;
} bspSubmodel_t;

typedef struct bspModel_s {
    char    name[MAX_QPATH];
	mempool_t pool;

	bspSubmodel_t *submodels;
	int numSubmodels;

	bspTexinfo_t *texinfos;
	int numTexinfos;

	bspSurface_t *surfaces;
	int numSurfaces;

	bspSurface_t **leafFaces;
	int numLeafFaces;

	cplane_t *planes;
	int numPlanes;

	bspLeaf_t *leafs;
	int numLeafs;

	bspNode_t *nodes;
	int numNodes;

	byte *vis;
	int numClusters;
	int rowsize;

    byte *lightmap;
    uint32  lightmapSize;

//	char *entityString;
    
    dvertex_t	*vertices;
    int			numVertices;

    dedge_t	*edges;
    int		numEdges;

    int		*surfEdges;
    int		numSurfEdges;
} bspModel_t;


extern bspModel_t   r_world;

void Bsp_FreeWorld( void );
void Bsp_LoadWorld( const char *path );
bspLeaf_t *Bsp_FindLeaf( vec3_t origin );
byte *Bsp_ClusterPVS( int clusterNum );

#ifdef OPENGL_RENDERER
extern bspTexinfo_t *upload_texinfo;
void GL_BeginPostProcessing( void );
void GL_EndPostProcessing( void );
#endif

#ifdef BIGENDIAN_TARGET
#define LL(x) ( dst->x = LongSwap( src->x ) )
#define LF(x) ( dst->x = FloatSwap( src->x ) )
#define LV(x) ( dst->x[0] = FloatSwap( src->x[0] ), \
		dst->x[1] = FloatSwap( src->x[1] ), \
		dst->x[2] = FloatSwap( src->x[2] ) )
#define LLV(x) ( dst->x[0] = LongSwap( src->x[0] ), \
		dst->x[1] = LongSwap( src->x[1] ), \
		dst->x[2] = LongSwap( src->x[2] ) )
#define LSV(x) ( dst->x[0] = ShortSwap( src->x[0] ), \
		dst->x[1] = ShortSwap( src->x[1] ), \
		dst->x[2] = ShortSwap( src->x[2] ) )
#else
#define LL(x) ( dst->x = src->x )
#define LF(x) ( dst->x = src->x )
#define LV(x) ( dst->x[0] = src->x[0], \
        dst->x[1] = src->x[1], \
        dst->x[2] = src->x[2] )
#define LLV(x) ( dst->x[0] = src->x[0], \
        dst->x[1] = src->x[1], \
        dst->x[2] = src->x[2] )
#define LSV(x) ( dst->x[0] = src->x[0], \
        dst->x[1] = src->x[1], \
        dst->x[2] = src->x[2] )
#endif




