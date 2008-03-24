/*
Copyright (C) 1997-2001 Id Software, Inc.

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

//
// qfiles.h: quake file formats
// This file must be identical in the quake and utils directories
//

/*
========================================================================

The .pak files are just a linear collapse of a directory tree

========================================================================
*/

#define IDPAKHEADER		(('K'<<24)+('C'<<16)+('A'<<8)+'P')

typedef struct
{
	char	name[56];
	int		filepos, filelen;
} dpackfile_t;

typedef struct
{
	int		ident;		// == IDPAKHEADER
	int		dirofs;
	int		dirlen;
} dpackheader_t;

#define	MAX_FILES_IN_PACK	4096


/*
========================================================================

PCX files are used for as many images as possible

========================================================================
*/

typedef struct
{
    char	manufacturer;
    char	version;
    char	encoding;
    char	bits_per_pixel;
    unsigned short	xmin,ymin,xmax,ymax;
    unsigned short	hres,vres;
    unsigned char	palette[48];
    char	reserved;
    char	color_planes;
    unsigned short	bytes_per_line;
    unsigned short	palette_type;
    char	filler[58];
    unsigned char	data;			// unbounded
} pcx_t;


/*
========================================================================

.MD2 triangle model file format

========================================================================
*/

#define IDALIASHEADER		(('2'<<24)+('P'<<16)+('D'<<8)+'I')
#define ALIAS_VERSION	8

#define	MAX_TRIANGLES	4096
#define MAX_VERTS		2048
#define MAX_FRAMES		512
#define MAX_MD2SKINS	32
#define	MAX_SKINNAME	64

typedef struct
{
	short	s;
	short	t;
} dstvert_t;

typedef struct 
{
	short	index_xyz[3];
	short	index_st[3];
} dtriangle_t;

typedef struct
{
	byte	v[3];			// scaled byte to fit in frame mins/maxs
	byte	lightnormalindex;
} dtrivertx_t;

#define DTRIVERTX_V0   0
#define DTRIVERTX_V1   1
#define DTRIVERTX_V2   2
#define DTRIVERTX_LNI  3
#define DTRIVERTX_SIZE 4

typedef struct
{
	float		scale[3];	// multiply byte verts by this
	float		translate[3];	// then add this
	char		name[16];	// frame name from grabbing
	dtrivertx_t	verts[1];	// variable sized
} daliasframe_t;

#define MAX_FRAMESIZE   ( sizeof( daliasframe_t ) + sizeof( dtrivertx_t ) * ( MAX_VERTS - 1 ) )


// the glcmd format:
// a positive integer starts a tristrip command, followed by that many
// vertex structures.
// a negative integer starts a trifan command, followed by -x vertexes
// a zero indicates the end of the command list.
// a vertex consists of a floating point s, a floating point t,
// and an integer vertex index.


typedef struct
{
	int			ident;
	int			version;

	int			skinwidth;
	int			skinheight;
	int			framesize;		// byte size of each frame

	int			num_skins;
	int			num_xyz;
	int			num_st;			// greater than num_xyz for seams
	int			num_tris;
	int			num_glcmds;		// dwords in strip/fan command list
	int			num_frames;

	int			ofs_skins;		// each skin is a MAX_SKINNAME string
	int			ofs_st;			// byte offset from start for stverts
	int			ofs_tris;		// offset for dtriangles
	int			ofs_frames;		// offset for first frame
	int			ofs_glcmds;	
	int			ofs_end;		// end of file

} dmdl_t;

/*
 =======================================================================

 .MD3 triangle model file format

 =======================================================================
*/

#define MD3_IDENT			(('3'<<24)+('P'<<16)+('D'<<8)+'I')
#define MD3_VERSION			15

// limits
#define MD3_MAX_LODS		3
#define	MD3_MAX_TRIANGLES	8192	// per surface
#define MD3_MAX_VERTS		4096	// per surface
#define MD3_MAX_SHADERS		256		// per surface
#define MD3_MAX_FRAMES		1024	// per model
#define	MD3_MAX_SURFACES	32		// per model
#define MD3_MAX_TAGS		16		// per frame
#define MD3_MAX_PATH		64

// vertex scales
#define	MD3_XYZ_SCALE		(1.0f/64.0f)

typedef struct {
	float			st[2];
} dmd3coord_t;

typedef struct {
	short			point[3];
	unsigned char		norm[2];
} dmd3vertex_t;

typedef struct {
   	float			mins[3];
	float			maxs[3];
	float			translate[3];
	float			radius;
	char			creator[16];
} dmd3frame_t;

typedef struct {
	char			name[MD3_MAX_PATH];	// tag name
	float			origin[3];
	float			axis[3][3];
} dmd3tag_t;

typedef struct {
	char			name[MD3_MAX_PATH];
	int			unused;			// shader
} dmd3skin_t;

typedef struct {
	int			ident;

	char			name[MD3_MAX_PATH];
	int			flags;

	int			num_frames;
	int			num_skins;
	int			num_verts;
	int			num_tris;

	int			ofs_indexes;
	int			ofs_skins;
	int			ofs_tcs;
	int			ofs_verts;

	int			meshsize;
} dmd3mesh_t;

typedef struct {
	int			ident;
	int			version;

	char			filename[MD3_MAX_PATH];

	int			flags;

	int			num_frames;
	int			num_tags;
	int			num_meshes;
	int			num_skins;

	int			ofs_frames;
	int			ofs_tags;
	int			ofs_meshes;
	int			ofs_end;
} dmd3header_t;


/*
========================================================================

.SP2 sprite file format

========================================================================
*/

#define IDSPRITEHEADER	(('2'<<24)+('S'<<16)+('D'<<8)+'I')
		// little-endian "IDS2"
#define SPRITE_VERSION	2

typedef struct
{
	int		width, height;
	int		origin_x, origin_y;		// raster coordinates inside pic
	char	name[MAX_SKINNAME];		// name of pcx file
} dsprframe_t;

typedef struct {
	int			ident;
	int			version;
	int			numframes;
	dsprframe_t	frames[1];			// variable sized
} dsprite_t;

/*
==============================================================================

  .WAL texture file format

==============================================================================
*/


#define	MIPLEVELS	4
typedef struct miptex_s
{
	char		name[32];
	uint32_t	width, height;
	uint32_t	offsets[MIPLEVELS];		// four mip maps stored
	char		animname[32];			// next frame in animation chain
	int			flags;
	int			contents;
	int			value;
} miptex_t;



/*
==============================================================================

  .BSP file format

==============================================================================
*/

#define IDBSPHEADER	(('P'<<24)+('S'<<16)+('B'<<8)+'I')
		// little-endian "IBSP"

#define BSPVERSION	38


// upper design bounds
// leaffaces, leafbrushes, planes, and verts are still bounded by
// 16 bit short limits
#define	MAX_MAP_MODELS		1024
#define	MAX_MAP_BRUSHES		8192
#define	MAX_MAP_ENTITIES	2048
#define	MAX_MAP_ENTSTRING	0x40000
#define	MAX_MAP_TEXINFO		8192

#define	MAX_MAP_AREAS		256
#define	MAX_MAP_AREAPORTALS	1024
#define	MAX_MAP_PLANES		65536
#define	MAX_MAP_NODES		65536
#define	MAX_MAP_BRUSHSIDES	65536
#define	MAX_MAP_LEAFS		65536
#define	MAX_MAP_VERTS		65536
#define	MAX_MAP_FACES		65536
#define	MAX_MAP_LEAFFACES	65536
#define	MAX_MAP_LEAFBRUSHES 65536
#define	MAX_MAP_PORTALS		65536
#define	MAX_MAP_EDGES		128000
#define	MAX_MAP_SURFEDGES	256000
#define	MAX_MAP_LIGHTING	0x200000
#define	MAX_MAP_VISIBILITY	0x100000

// key / value pair sizes

#define	MAX_KEY		32
#define	MAX_VALUE	1024

//=============================================================================

typedef struct {
	uint32_t		fileofs, filelen;
} lump_t;

#define	LUMP_ENTITIES		0
#define	LUMP_PLANES			1
#define	LUMP_VERTEXES		2
#define	LUMP_VISIBILITY		3
#define	LUMP_NODES			4
#define	LUMP_TEXINFO		5
#define	LUMP_FACES			6
#define	LUMP_LIGHTING		7
#define	LUMP_LEAFS			8
#define	LUMP_LEAFFACES		9
#define	LUMP_LEAFBRUSHES	10
#define	LUMP_EDGES			11
#define	LUMP_SURFEDGES		12
#define	LUMP_MODELS			13
#define	LUMP_BRUSHES		14
#define	LUMP_BRUSHSIDES		15
#define	LUMP_POP			16
#define	LUMP_AREAS			17
#define	LUMP_AREAPORTALS	18
#define	HEADER_LUMPS		19

typedef struct {
	uint32_t		ident;
	uint32_t		version;	
	lump_t		lumps[HEADER_LUMPS];
} dheader_t;

typedef struct {
	float		mins[3], maxs[3];
	float		origin[3];		// for sounds or lights
	uint32_t		headnode;
	uint32_t		firstface, numfaces;	// submodels just draw faces
										// without walking the bsp tree
} dmodel_t;


typedef struct {
	float	point[3];
} dvertex_t;

typedef struct {
	float	normal[3];
	float	dist;
	uint32_t	type;		// PLANE_X - PLANE_ANYZ ?remove? trivial to regenerate
} dplane_t;


// contents flags are seperate bits
// a given brush can contribute multiple content bits
// multiple brushes can be in a single leaf

// these definitions also need to be in q_shared.h!

// lower bits are stronger, and will eat weaker brushes completely
#define	CONTENTS_SOLID			1		// an eye is never valid in a solid
#define	CONTENTS_WINDOW			2		// translucent, but not watery
#define	CONTENTS_AUX			4
#define	CONTENTS_LAVA			8
#define	CONTENTS_SLIME			16
#define	CONTENTS_WATER			32
#define	CONTENTS_MIST			64
#define	LAST_VISIBLE_CONTENTS	64

// remaining contents are non-visible, and don't eat brushes

#define	CONTENTS_AREAPORTAL		0x8000

#define	CONTENTS_PLAYERCLIP		0x10000
#define	CONTENTS_MONSTERCLIP	0x20000

// currents can be added to any other contents, and may be mixed
#define	CONTENTS_CURRENT_0		0x40000
#define	CONTENTS_CURRENT_90		0x80000
#define	CONTENTS_CURRENT_180	0x100000
#define	CONTENTS_CURRENT_270	0x200000
#define	CONTENTS_CURRENT_UP		0x400000
#define	CONTENTS_CURRENT_DOWN	0x800000

#define	CONTENTS_ORIGIN			0x1000000	// removed before bsping an entity

#define	CONTENTS_MONSTER		0x2000000	// should never be on a brush, only in game
#define	CONTENTS_DEADMONSTER	0x4000000
#define	CONTENTS_DETAIL			0x8000000	// brushes to be added after vis leafs
#define	CONTENTS_TRANSLUCENT	0x10000000	// auto set if any surface has trans
#define	CONTENTS_LADDER			0x20000000



#define	SURF_LIGHT		0x1		// value will hold the light strength

#define	SURF_SLICK		0x2		// effects game physics

#define	SURF_SKY		0x4		// don't draw, but add to skybox
#define	SURF_WARP		0x8		// turbulent water warp
#define	SURF_TRANS33	0x10
#define	SURF_TRANS66	0x20
#define	SURF_FLOWING	0x40	// scroll towards angle
#define	SURF_NODRAW		0x80	// don't bother referencing the texture

typedef struct {
	uint32_t		planenum;
	uint32_t		children[2];	// negative numbers are -(leafs+1), not nodes
	int16_t			mins[3];		// for frustom culling
	int16_t			maxs[3];
	uint16_t		firstface;
	uint16_t		numfaces;	// counting both sides
} dnode_t;


typedef struct texinfo_s {
	float		vecs[2][4];		// [s/t][xyz offset]
	uint32_t	flags;			// miptex flags + overrides
	int32_t		value;			// light emission, etc
	char		texture[32];	// texture name (textures/*.wal)
	uint32_t	nexttexinfo;	// for animations, -1 = end of chain
} texinfo_t;


// note that edge 0 is never used, because negative edge nums are used for
// counterclockwise use of the edge in a face
typedef struct {
	uint16_t	v[2];		// vertex numbers
} dedge_t;

#define	MAXLIGHTMAPS	4

typedef struct {
	uint16_t		planenum;
	uint16_t		side;

	uint32_t		firstedge;		// we must support > 64k edges
	uint16_t		numedges;	
	uint16_t		texinfo;

// lighting info
	uint8_t		    styles[MAXLIGHTMAPS];
	uint32_t		lightofs;		// start of [numstyles*surfsize] samples
} dface_t;

typedef struct {
	uint32_t		contents;			// OR of all brushes (not needed?)

	uint16_t		cluster;
	uint16_t		area;

	int16_t			mins[3];			// for frustum culling
	int16_t			maxs[3];

	uint16_t		firstleafface;
	uint16_t		numleaffaces;

	uint16_t		firstleafbrush;
	uint16_t		numleafbrushes;
} dleaf_t;

typedef struct {
	uint16_t	planenum;		// facing out of the leaf
	uint16_t	texinfo;
} dbrushside_t;

typedef struct {
	uint32_t			firstside;
	uint32_t			numsides;
	uint32_t			contents;
} dbrush_t;

#define	ANGLE_UP	-1
#define	ANGLE_DOWN	-2


// the visibility lump consists of a header with a count, then
// byte offsets for the PVS and PHS of each cluster, then the raw
// compressed bit vectors
#define	DVIS_PVS	0
#define	DVIS_PHS	1

#define DVIS_CLUSTERS	8

typedef struct {
	uint32_t		numclusters;
	uint32_t		bitofs[DVIS_CLUSTERS][2];	// bitofs[numclusters][2]
} dvis_t;

// each area has a list of portals that lead into other areas
// when portals are closed, other areas may not be visible or
// hearable even if the vis info says that it should be
typedef struct {
	uint32_t		portalnum;
	uint32_t		otherarea;
} dareaportal_t;

typedef struct {
	uint32_t		numareaportals;
	uint32_t		firstareaportal;
} darea_t;

/*
==============================================================================

  .QFONT file format

==============================================================================
*/

#define QFONT_MAGIC		(('T'<<24)+('N'<<16)+('F'<<8)+'Q') // little-endian "QFNT"

#define QFONT_MAX_GLYPHS	256
#define QFONT_MAX_IMAGES	4

typedef struct dglyph_s {
	char	xBearing;
	char	yBearing;
	byte	xAdvance;
	byte	imageIndex;
	byte	s1, t1; // absolute coordinates
	byte	s2, t2; // absolute coordinates
} dglyph_t;

typedef struct dfont_s {
	int			magic;
	//int			charWidth;
	//int			charHeight;
	char		fontImages[QFONT_MAX_IMAGES][MAX_QPATH];
	dglyph_t	glyphs[QFONT_MAX_GLYPHS];
} dfont_t;



