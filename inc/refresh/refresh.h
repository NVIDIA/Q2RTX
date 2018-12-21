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

#ifndef REFRESH_H
#define REFRESH_H

#include "common/cvar.h"
#include "common/error.h"

#define MAX_DLIGHTS     32
#define MAX_ENTITIES    256     // == MAX_PACKET_ENTITIES * 2
#define MAX_PARTICLES   4096
#define MAX_LIGHTSTYLES 256

#define POWERSUIT_SCALE     4.0f
#define WEAPONSHELL_SCALE   0.5f

#define SHELL_RED_COLOR     0xF2
#define SHELL_GREEN_COLOR   0xD0
#define SHELL_BLUE_COLOR    0xF3

#define SHELL_RG_COLOR      0xDC
//#define SHELL_RB_COLOR        0x86
#define SHELL_RB_COLOR      0x68
#define SHELL_BG_COLOR      0x78

//ROGUE
#define SHELL_DOUBLE_COLOR  0xDF // 223
#define SHELL_HALF_DAM_COLOR    0x90
#define SHELL_CYAN_COLOR    0x72
//ROGUE

#define SHELL_WHITE_COLOR   0xD7

// NOTE: these flags are intentionally the same value
#define RF_LEFTHAND         0x80000000
#define RF_NOSHADOW         0x80000000

#define RF_SHELL_MASK       (RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | \
                             RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM)

#define DLIGHT_CUTOFF       64

typedef struct entity_s {
    qhandle_t           model;          // opaque type outside refresh
    vec3_t              angles;

    /*
    ** most recent data
    */
    vec3_t              origin;     // also used as RF_BEAM's "from"
    int                 frame;          // also used as RF_BEAM's diameter

    /*
    ** previous data for lerping
    */
    vec3_t              oldorigin;  // also used as RF_BEAM's "to"
    int                 oldframe;

    /*
    ** misc
    */
    float   backlerp;               // 0.0 = current, 1.0 = old
    int     skinnum;                // also used as RF_BEAM's palette index,
                                    // -1 => use rgba

    float   alpha;                  // ignore if RF_TRANSLUCENT isn't set
    color_t rgba;

    qhandle_t   skin;           // NULL for inline skin
    int         flags;

    int                 id;
} entity_t;

typedef struct dlight_s {
    vec3_t  origin;
#if USE_REF == REF_GL
    vec3_t  transformed;
#endif
    vec3_t  color;
    float   intensity;
} dlight_t;

typedef struct particle_s {
    vec3_t  origin;
    int     color;              // -1 => use rgba
    float   alpha;
    color_t rgba;
} particle_t;

typedef struct lightstyle_s {
    float           white;          // highest of RGB
    vec3_t          rgb;            // 0.0 - 2.0
} lightstyle_t;

#ifdef USE_SMALL_GPU
#define MAX_DECALS 2
#else
#define MAX_DECALS 50
#endif
typedef struct decal_s {
    vec3_t pos;
    vec3_t dir;
    float spread;
    float length;
    float dummy;
} decal_t;

typedef struct refdef_s {
    int         x, y, width, height;// in virtual screen coordinates
    float       fov_x, fov_y;
    vec3_t      vieworg;
    vec3_t      viewangles;
    vec4_t      blend;          // rgba 0-1 full screen blend
    float       time;               // time is uesed to auto animate
    int         rdflags;            // RDF_UNDERWATER, etc

    byte        *areabits;          // if not NULL, only areas with set bits will be drawn

    lightstyle_t    *lightstyles;   // [MAX_LIGHTSTYLES]

    int         num_entities;
    entity_t    *entities;

    int         num_dlights;
    dlight_t    *dlights;

    int         num_particles;
    particle_t  *particles;

    int         decal_beg;
    int         decal_end;
    decal_t     decal[MAX_DECALS];
} refdef_t;

typedef enum {
    QVF_ACCELERATED     = (1 << 0),
    QVF_GAMMARAMP       = (1 << 1),
    QVF_FULLSCREEN      = (1 << 2),
    QVF_VIDEOSYNC       = (1 << 3)
} vidFlags_t;

typedef struct {
    int         width;
    int         height;
    vidFlags_t  flags;
} refcfg_t;

extern refcfg_t r_config;

typedef struct {
    int left, right, top, bottom;
} clipRect_t;

typedef enum {
    IF_NONE         = 0,
    IF_PERMANENT    = (1 << 0),
    IF_TRANSPARENT  = (1 << 1),
    IF_PALETTED     = (1 << 2),
    IF_UPSCALED     = (1 << 3),
    IF_SCRAP        = (1 << 4),
    IF_TURBULENT    = (1 << 5),
    IF_REPEAT       = (1 << 6),
    IF_NEAREST      = (1 << 7),
    IF_OPAQUE       = (1 << 8),
} imageflags_t;

typedef enum {
    IT_PIC,
    IT_FONT,
    IT_SKIN,
    IT_SPRITE,
    IT_WALL,
    IT_SKY,

    IT_MAX
} imagetype_t;

// called when the library is loaded
qboolean    R_Init(qboolean total);

// called before the library is unloaded
void        R_Shutdown(qboolean total);

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
void    R_BeginRegistration(const char *map);
qhandle_t R_RegisterModel(const char *name);
qhandle_t R_RegisterImage(const char *name, imagetype_t type,
                          imageflags_t flags, qerror_t *err_p);
void    R_SetSky(const char *name, float rotate, vec3_t axis);
void    R_EndRegistration(void);

#define R_RegisterPic(name)     R_RegisterImage(name, IT_PIC, IF_PERMANENT, NULL)
#define R_RegisterPic2(name)    R_RegisterImage(name, IT_PIC, IF_NONE, NULL)
#define R_RegisterFont(name)    R_RegisterImage(name, IT_FONT, IF_PERMANENT, NULL)
#define R_RegisterSkin(name)    R_RegisterImage(name, IT_SKIN, IF_NONE, NULL)

void    R_RenderFrame(refdef_t *fd);
void    R_LightPoint(vec3_t origin, vec3_t light);

void    R_ClearColor(void);
void    R_SetAlpha(float clpha);
void    R_SetColor(uint32_t color);
void    R_SetClipRect(const clipRect_t *clip);
float   R_ClampScale(cvar_t *var);
void    R_SetScale(float scale);
void    R_DrawChar(int x, int y, int flags, int ch, qhandle_t font);
int     R_DrawString(int x, int y, int flags, size_t maxChars,
                     const char *string, qhandle_t font);  // returns advanced x coord
qboolean R_GetPicSize(int *w, int *h, qhandle_t pic);   // returns transparency bit
void    R_DrawPic(int x, int y, qhandle_t pic);
void    R_DrawStretchPic(int x, int y, int w, int h, qhandle_t pic);
void    R_TileClear(int x, int y, int w, int h, qhandle_t pic);
void    R_DrawFill8(int x, int y, int w, int h, int c);
void    R_DrawFill32(int x, int y, int w, int h, uint32_t color);

// video mode and refresh state management entry points
void    R_BeginFrame(void);
void    R_EndFrame(void);
void    R_ModeChanged(int width, int height, int flags, int rowbytes, void *pixels);

// add decal to ring buffer
void    R_AddDecal(decal_t *d);

#ifdef _GL_DEBUG
void    R_SetRayProbe(vec3_t p, vec3_t n);
#endif

#endif // REFRESH_H
