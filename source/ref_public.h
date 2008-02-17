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
// ref_public.h
//

#ifndef __REF_H
#define __REF_H

#define	MAX_DLIGHTS		32
#define	MAX_ENTITIES	128
#define	MAX_PARTICLES	4096
#define	MAX_LIGHTSTYLES	256

#define POWERSUIT_SCALE		4.0F

#define SHELL_RED_COLOR		0xF2
#define SHELL_GREEN_COLOR	0xD0
#define SHELL_BLUE_COLOR	0xF3

#define SHELL_RG_COLOR		0xDC
//#define SHELL_RB_COLOR		0x86
#define SHELL_RB_COLOR		0x68
#define SHELL_BG_COLOR		0x78

//ROGUE
#define SHELL_DOUBLE_COLOR	0xDF // 223
#define	SHELL_HALF_DAM_COLOR	0x90
#define SHELL_CYAN_COLOR	0x72
//ROGUE

#define SHELL_WHITE_COLOR	0xD7

#define RF_LEFTHAND			0x80000000

typedef struct entity_s {
	qhandle_t			model;			// opaque type outside refresh
	vec3_t				angles;

	/*
	** most recent data
	*/
	vec3_t				origin;		// also used as RF_BEAM's "from"
	int					frame;			// also used as RF_BEAM's diameter

	/*
	** previous data for lerping
	*/
	vec3_t				oldorigin;	// also used as RF_BEAM's "to"
	int					oldframe;

	/*
	** misc
	*/
	float	backlerp;				// 0.0 = current, 1.0 = old
	int		skinnum;				// also used as RF_BEAM's palette index

	int		lightstyle;				// for flashing entities
	float	alpha;					// ignore if RF_TRANSLUCENT isn't set

	qhandle_t	skin;			// NULL for inline skin
	int			flags;
} entity_t;

#define ENTITY_FLAGS  68

typedef struct dlight_s {
	vec3_t	origin;
	vec3_t	color;
	float	intensity;
} dlight_t;

typedef struct particle_s {
	vec3_t	origin;
	int		color;
	float	alpha;
	color_t		rgb;
} particle_t;

typedef struct lightstyle_s {
	float			white;			// highest of RGB
	vec3_t			rgb;			// 0.0 - 2.0
} lightstyle_t;

typedef struct refdef_s {
	int			x, y, width, height;// in virtual screen coordinates
	float		fov_x, fov_y;
	vec3_t		vieworg;
	vec3_t		viewangles;
	vec4_t		blend;			// rgba 0-1 full screen blend
	float		time;				// time is uesed to auto animate
	int			rdflags;			// RDF_UNDERWATER, etc

	byte		*areabits;			// if not NULL, only areas with set bits will be drawn

	lightstyle_t	*lightstyles;	// [MAX_LIGHTSTYLES]

	int			num_entities;
	entity_t	*entities;

	int			num_dlights;
	dlight_t	*dlights;

	int			num_particles;
	particle_t	*particles;
} refdef_t;

typedef enum glHardware_e {
	GL_RENDERER_OTHER,
	GL_RENDERER_SOFTWARE,
	GL_RENDERER_MCD,
	GL_RENDERER_VOODOO,
	GL_RENDERER_VOODOO_RUSH,
	GL_RENDERER_POWERVR,
	GL_RENDERER_GLINT,
	GL_RENDERER_PERMEDIA2,
	GL_RENDERER_INTERGRAPH,
	GL_RENDERER_RENDITION,
    GL_RENDERER_MESADRI
} glHardware_t;

typedef struct glconfig_s {
	glHardware_t	renderer;
	const char *rendererString;
	const char *vendorString;
	const char *versionString;
	const char *extensionsString;

	int		vidWidth;
	int		vidHeight;

    int         flags;
	float		maxAnisotropy;
} glconfig_t;

#define	DRAW_COLOR_CLEAR	0
#define	DRAW_COLOR_RGB		0x00000001
#define	DRAW_COLOR_ALPHA	0x00000002
#define	DRAW_COLOR_RGBA	    0x00000003
#define	DRAW_COLOR_INDEXED	0x00000004
#define	DRAW_COLOR_MASK		0x00000007


#define	DRAW_CLIP_DISABLED	0
#define DRAW_CLIP_LEFT		0x00000004
#define DRAW_CLIP_RIGHT		0x00000008
#define DRAW_CLIP_TOP		0x00000010
#define DRAW_CLIP_BOTTOM	0x00000020
#define DRAW_CLIP_MASK		0x0000003C

typedef struct {
	int left, right, top, bottom;
} clipRect_t;

//
// these are the functions exported by the refresh module
//
typedef struct refAPI_s {
	// called when the library is loaded
	qboolean	(*Init)( qboolean total );

	// called before the library is unloaded
	void	(*Shutdown)( qboolean total );

	// All data that will be used in a level should be
	// registered before rendering any frames to prevent disk hits,
	// but they can still be registered at a later time
	// if necessary.
	//
	// EndRegistration will free any remaining data that wasn't registered.
	// Any model_s or skin_s pointers from before the BeginRegistration
	// are no longer valid after EndRegistration.
	//
	// Skins and images need to be differentiated, because skins
	// are flood filled to eliminate mip map edge errors, and pics have
	// an implicit "pics/" prepended to the name. (a pic name that starts with a
	// slash will not use the "pics/" prefix or the ".pcx" postfix)
	void	(*BeginRegistration)( const char *map );
	qhandle_t (*RegisterModel)( const char *name );
	qhandle_t (*RegisterSkin)( const char *name );
	qhandle_t (*RegisterPic)( const char *name );
	qhandle_t (*RegisterFont)( const char *name );
	void	(*SetSky)( const char *name, float rotate, vec3_t axis );
	void	(*EndRegistration)( void );
	void	(*GetModelSize)( qhandle_t hModel, vec3_t mins, vec3_t maxs );

	void	(*RenderFrame)( refdef_t *fd );
	void	(*LightPoint)( vec3_t origin, vec3_t light );

	void	(*SetColor)( int flags, const color_t color );
	void	(*SetClipRect)( int flags, const clipRect_t *clip );
	void	(*SetScale)( float *scale );
    void    (*DrawChar)( int x, int y, int flags, int ch, qhandle_t hFont );
	int 	(*DrawString)( int x, int y, int flags, int maxChars,
            const char *string, qhandle_t hFont ); // returns advanced x coord
    // will return 0 0 if not found
	void	(*DrawGetPicSize)( int *w, int *h, qhandle_t hPic );	
	void	(*DrawGetFontSize)( int *w, int *h, qhandle_t hFont );	
	void	(*DrawPic)( int x, int y, qhandle_t hPic );
	void	(*DrawStretchPic)( int x, int y, int w, int h, qhandle_t hPic );
	void	(*DrawStretchPicST)( int x, int y, int w, int h,
            float s1, float t1, float s2, float t2, qhandle_t hPic );
	void	(*DrawTileClear)( int x, int y, int w, int h, qhandle_t hPic );
	void	(*DrawFill)( int x, int y, int w, int h, int c );
	void	(*DrawFillEx)( int x, int y, int w, int h, const color_t color );

	// Draw images for cinematic rendering (which can have a different palette).
	void	(*DrawStretchRaw)( int x, int y, int w, int h, int cols, int rows, const byte *data );

	/*
	** video mode and refresh state management entry points
	*/
	void	(*CinematicSetPalette)( const byte *palette );	// NULL = game palette
	void	(*BeginFrame)( void );
	void	(*EndFrame)( void );
    void    (*ModeChanged)( int width, int height, int flags,
        int rowbytes, void *pixels );

	void	(*GetConfig)( glconfig_t *dest );
} refAPI_t;

extern refAPI_t		ref;

#endif // __REF_H
