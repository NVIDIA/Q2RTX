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
// r_surf.c: surface-related refresh code

#include "sw.h"

drawsurf_t  r_drawsurf;

static int          sourcetstep;
static void         *prowdestbase;
static byte         *pbasesource;
static int          surfrowbytes;
static unsigned     *r_lightptr;
static int          r_stepback;
static int          r_lightwidth;
static int          r_numhblocks, r_numvblocks;
static byte         *r_source, *r_sourcemax;

static void R_DrawSurfaceBlock8_mip0(void);
static void R_DrawSurfaceBlock8_mip1(void);
static void R_DrawSurfaceBlock8_mip2(void);
static void R_DrawSurfaceBlock8_mip3(void);

static void (*surfmiptable[4])(void) = {
    R_DrawSurfaceBlock8_mip0,
    R_DrawSurfaceBlock8_mip1,
    R_DrawSurfaceBlock8_mip2,
    R_DrawSurfaceBlock8_mip3
};

static int          sc_size;
static surfcache_t  *sc_rover, *sc_base;

/*
===============
R_TextureAnimation

Returns the proper texture for a given time and base texture
===============
*/
static image_t *R_TextureAnimation(mtexinfo_t *tex)
{
    int     c;

    if (!tex->next)
        return tex->image;

    c = currententity->frame % tex->numframes;
    while (c) {
        tex = tex->next;
        c--;
    }

    return tex->image;
}

/*
===============
R_DrawSurface
===============
*/
static void R_DrawSurface(void)
{
    byte        *basetptr;
    int         smax, tmax, twidth;
    int         u;
    int         soffset, toffset;
    int         horzblockstep;
    byte        *pcolumndest;
    void        (*pblockdrawer)(void);
    image_t     *mt;
    int         blocksize;
    int         blockdivshift;

    surfrowbytes = r_drawsurf.rowbytes;

    mt = r_drawsurf.image;

    r_source = mt->pixels[r_drawsurf.surfmip];

    twidth = (mt->upload_width >> r_drawsurf.surfmip) * TEX_BYTES;

    blocksize = 16 >> r_drawsurf.surfmip;
    blockdivshift = 4 - r_drawsurf.surfmip;

    r_lightwidth = S_MAX(r_drawsurf.surf) * LIGHTMAP_BYTES;

    r_numhblocks = r_drawsurf.surfwidth >> blockdivshift;
    r_numvblocks = r_drawsurf.surfheight >> blockdivshift;

    pblockdrawer = surfmiptable[r_drawsurf.surfmip];
// TODO: only needs to be set when there is a display settings change
    horzblockstep = blocksize * TEX_BYTES;

    smax = mt->upload_width >> r_drawsurf.surfmip;
    tmax = mt->upload_height >> r_drawsurf.surfmip;
    sourcetstep = twidth;
    r_stepback = tmax * twidth;

    r_sourcemax = r_source + tmax * smax * TEX_BYTES;

    soffset = r_drawsurf.surf->texturemins[0];
    toffset = r_drawsurf.surf->texturemins[1];

// << 16 components are to guarantee positive values for %
    soffset = ((soffset >> r_drawsurf.surfmip) + (smax << 16)) % smax;
    toffset = ((toffset >> r_drawsurf.surfmip) + (tmax << 16)) % tmax;

    basetptr = &r_source[toffset * twidth];

    pcolumndest = r_drawsurf.surfdat;

    for (u = 0; u < r_numhblocks; u++) {
        r_lightptr = (unsigned *)blocklights + u * LIGHTMAP_BYTES;

        prowdestbase = pcolumndest;

        pbasesource = basetptr + soffset * TEX_BYTES;

        (*pblockdrawer)();

        soffset += blocksize;
        if (soffset >= smax)
            soffset = 0;

        pcolumndest += horzblockstep;
    }
}

//=============================================================================

#define BLOCK_FUNC R_DrawSurfaceBlock8_mip0
#define BLOCK_SHIFT 4
#include "block.h"

#define BLOCK_FUNC R_DrawSurfaceBlock8_mip1
#define BLOCK_SHIFT 3
#include "block.h"

#define BLOCK_FUNC R_DrawSurfaceBlock8_mip2
#define BLOCK_SHIFT 2
#include "block.h"

#define BLOCK_FUNC R_DrawSurfaceBlock8_mip3
#define BLOCK_SHIFT 1
#include "block.h"

//============================================================================


/*
================
R_InitCaches

================
*/
void R_InitCaches(void)
{
    int     size;
    int     pix;

    // calculate size to allocate
    if (sw_surfcacheoverride->integer) {
        size = Cvar_ClampInteger(sw_surfcacheoverride,
                                 SURFCACHE_SIZE_AT_320X240,
                                 SURFCACHE_SIZE_AT_320X240 * 25);
    } else {
        size = SURFCACHE_SIZE_AT_320X240;

        pix = vid.width * vid.height;
        if (pix > 64000)
            size += (pix - 64000) * 3;
    }

    size *= TEX_BYTES;

    // round up to page size
    size = (size + 8191) & ~8191;

    Com_DPrintf("%ik surface cache\n", size / 1024);

    sc_size = size;
    sc_base = (surfcache_t *)R_Malloc(size);
    sc_rover = sc_base;

    sc_base->next = NULL;
    sc_base->owner = NULL;
    sc_base->size = sc_size;
}

void R_FreeCaches(void)
{
    if (sc_base) {
        Z_Free(sc_base);
        sc_base = NULL;
    }

    sc_size = 0;
    sc_rover = NULL;
}

/*
==================
D_FlushCaches
==================
*/
void D_FlushCaches(void)
{
    surfcache_t     *c;

    if (!sc_base)
        return;

    for (c = sc_base; c; c = c->next) {
        if (c->owner) {
            *c->owner = NULL;
        }
    }

    sc_rover = sc_base;
    sc_base->next = NULL;
    sc_base->owner = NULL;
    sc_base->size = sc_size;
}

/*
=================
D_SCAlloc
=================
*/
static surfcache_t *D_SCAlloc(int width, int size)
{
    surfcache_t             *new;

    if ((width < 0) || (width > 256))
        Com_Error(ERR_FATAL, "D_SCAlloc: bad cache width %d\n", width);

    if ((size <= 0) || (size > 0x10000 * TEX_BYTES))
        Com_Error(ERR_FATAL, "D_SCAlloc: bad cache size %d\n", size);

    size += sizeof(surfcache_t) - 4;
    size = (size + 3) & ~3;
    if (size > sc_size)
        Com_Error(ERR_FATAL, "D_SCAlloc: %i > cache size of %i", size, sc_size);

// if there is not size bytes after the rover, reset to the start
    if (!sc_rover || (byte *)sc_rover - (byte *)sc_base > sc_size - size) {
        sc_rover = sc_base;
    }

// colect and free surfcache_t blocks until the rover block is large enough
    new = sc_rover;
    if (sc_rover->owner)
        *sc_rover->owner = NULL;

    while (new->size < size) {
        // free another
        sc_rover = sc_rover->next;
        if (!sc_rover)
            Com_Error(ERR_FATAL, "D_SCAlloc: hit the end of memory");
        if (sc_rover->owner)
            *sc_rover->owner = NULL;

        new->size += sc_rover->size;
        new->next = sc_rover->next;
    }

// create a fragment out of any leftovers
    if (new->size - size > 256) {
        sc_rover = (surfcache_t *)((byte *)new + size);
        sc_rover->size = new->size - size;
        sc_rover->next = new->next;
        sc_rover->width = 0;
        sc_rover->owner = NULL;
        new->next = sc_rover;
        new->size = size;
    } else
        sc_rover = new->next;

    new->width = width;
// DEBUG
    if (width > 0)
        new->height = (size - sizeof(*new) + sizeof(new->data)) / width;

    new->owner = NULL;              // should be set properly after return

    return new;
}


/*
=================
D_SCDump
=================
*/
void D_SCDump_f(void)
{
    surfcache_t             *test;

    for (test = sc_base; test; test = test->next) {
        if (test == sc_rover)
            Com_Printf("ROVER:\n");
        Com_Printf("%p : %i bytes     %i width\n", test, test->size,
                   test->width);
    }
}

//=============================================================================

/*
================
D_CacheSurface
================
*/
surfcache_t *D_CacheSurface(mface_t *surface, int miplevel)
{
    surfcache_t     *cache;
    float           surfscale;

//
// if the surface is animating or flashing, flush the cache
//
    r_drawsurf.image = R_TextureAnimation(surface->texinfo);
    r_drawsurf.lightadj[0] = r_newrefdef.lightstyles[surface->styles[0]].white * sw_modulate->value * 256;
    r_drawsurf.lightadj[1] = r_newrefdef.lightstyles[surface->styles[1]].white * sw_modulate->value * 256;
    r_drawsurf.lightadj[2] = r_newrefdef.lightstyles[surface->styles[2]].white * sw_modulate->value * 256;
    r_drawsurf.lightadj[3] = r_newrefdef.lightstyles[surface->styles[3]].white * sw_modulate->value * 256;

//
// see if the cache holds apropriate data
//
    cache = surface->cachespots[miplevel];

    if (cache && !cache->dlight && surface->dlightframe != r_framecount
        && cache->image == r_drawsurf.image
        && cache->lightadj[0] == r_drawsurf.lightadj[0]
        && cache->lightadj[1] == r_drawsurf.lightadj[1]
        && cache->lightadj[2] == r_drawsurf.lightadj[2]
        && cache->lightadj[3] == r_drawsurf.lightadj[3])
        return cache;

//
// determine shape of surface
//
    surfscale = 1.0 / (1 << miplevel);
    r_drawsurf.surfmip = miplevel;
    r_drawsurf.surfwidth = surface->extents[0] >> miplevel;
    r_drawsurf.rowbytes = r_drawsurf.surfwidth * TEX_BYTES;
    r_drawsurf.surfheight = surface->extents[1] >> miplevel;

//
// allocate memory if needed
//
    if (!cache) {   // if a texture just animated, don't reallocate it
        cache = D_SCAlloc(r_drawsurf.surfwidth,
                          r_drawsurf.surfwidth * r_drawsurf.surfheight * TEX_BYTES);
        surface->cachespots[miplevel] = cache;
        cache->owner = &surface->cachespots[miplevel];
        cache->mipscale = surfscale;
    }

    if (surface->dlightframe == r_framecount)
        cache->dlight = 1;
    else
        cache->dlight = 0;

    r_drawsurf.surfdat = (pixel_t *)cache->data;

    cache->image = r_drawsurf.image;
    cache->lightadj[0] = r_drawsurf.lightadj[0];
    cache->lightadj[1] = r_drawsurf.lightadj[1];
    cache->lightadj[2] = r_drawsurf.lightadj[2];
    cache->lightadj[3] = r_drawsurf.lightadj[3];

//
// draw and light the surface texture
//
    r_drawsurf.surf = surface;

    c_surf++;

    // calculate the lightings
    R_BuildLightMap();

    // rasterize the surface into the cache
    R_DrawSurface();

    return cache;
}


