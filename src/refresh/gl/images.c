/*
Copyright (C) 1997-2001 Id Software, Inc.
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

#include "gl.h"
#include "common/prompt.h"

static int gl_filter_min;
static int gl_filter_max;
static float gl_filter_anisotropy;
static int gl_tex_alpha_format;
static int gl_tex_solid_format;

static int  upload_width;
static int  upload_height;
static image_t  *upload_image;
mtexinfo_t    *upload_texinfo;

static cvar_t *gl_noscrap;
static cvar_t *gl_round_down;
static cvar_t *gl_picmip;
static cvar_t *gl_maxmip;
static cvar_t *gl_downsample_skins;
static cvar_t *gl_gamma_scale_pics;
static cvar_t *gl_bilerp_chars;
static cvar_t *gl_bilerp_pics;
static cvar_t *gl_texturemode;
static cvar_t *gl_texturesolidmode;
static cvar_t *gl_texturealphamode;
static cvar_t *gl_anisotropy;
static cvar_t *gl_saturation;
static cvar_t *gl_intensity;
static cvar_t *gl_gamma;
static cvar_t *gl_invert;

static qboolean GL_Upload8(byte *data, int width, int height, qboolean mipmap);

typedef struct {
    const char *name;
    int minimize, maximize;
} glmode_t;

static const glmode_t filterModes[] = {
    { "GL_NEAREST", GL_NEAREST, GL_NEAREST },
    { "GL_LINEAR", GL_LINEAR, GL_LINEAR },
    { "GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },
    { "GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR },
    { "GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST },
    { "GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR }
};

static const int numFilterModes = q_countof(filterModes);

typedef struct {
    const char *name;
    int mode;
} gltmode_t;

static const gltmode_t alphaModes[] = {
    { "default", 4 },
    { "GL_RGBA", GL_RGBA },
    { "GL_RGBA8", GL_RGBA8 },
    { "GL_RGB5_A1", GL_RGB5_A1 },
    { "GL_RGBA4", GL_RGBA4 },
    { "GL_RGBA2", GL_RGBA2 }
};

static const int numAlphaModes = q_countof(alphaModes);

static const gltmode_t solidModes[] = {
    { "default", 4 },
    { "GL_RGB", GL_RGB },
    { "GL_RGB8", GL_RGB8 },
    { "GL_RGB5", GL_RGB5 },
    { "GL_RGB4", GL_RGB4 },
    { "GL_R3_G3_B2", GL_R3_G3_B2 },
    { "GL_LUMINANCE", GL_LUMINANCE },
#ifdef GL_RGB2_EXT
    { "GL_RGB2", GL_RGB2_EXT }
#endif
};

static const int numSolidModes = q_countof(solidModes);

static void gl_texturemode_changed(cvar_t *self)
{
    int     i;
    image_t *image;

    for (i = 0; i < numFilterModes; i++) {
        if (!Q_stricmp(filterModes[i].name, self->string))
            break;
    }

    if (i == numFilterModes) {
        Com_WPrintf("Bad texture mode: %s\n", self->string);
        Cvar_Reset(self);
        gl_filter_min = GL_LINEAR_MIPMAP_LINEAR;
        gl_filter_max = GL_LINEAR;
    } else {
        gl_filter_min = filterModes[i].minimize;
        gl_filter_max = filterModes[i].maximize;
    }

    // change all the existing mipmap texture objects
    for (i = 0, image = r_images; i < r_numImages; i++, image++) {
        if (image->type == IT_WALL || image->type == IT_SKIN) {
            GL_BindTexture(image->texnum);
            qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                             gl_filter_min);
            qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                             gl_filter_max);
        }
    }
}

static void gl_texturemode_g(genctx_t *ctx)
{
    int i;

    for (i = 0; i < numFilterModes; i++) {
        if (!Prompt_AddMatch(ctx, filterModes[i].name)) {
            break;
        }
    }
}

static void gl_anisotropy_changed(cvar_t *self)
{
    int     i;
    image_t *image;

    if (gl_config.maxAnisotropy < 2) {
        return;
    }

    gl_filter_anisotropy = self->value;
    clamp(gl_filter_anisotropy, 1, gl_config.maxAnisotropy);

    // change all the existing mipmap texture objects
    for (i = 0, image = r_images; i < r_numImages; i++, image++) {
        if (image->type == IT_WALL || image->type == IT_SKIN) {
            GL_BindTexture(image->texnum);
            qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                             gl_filter_anisotropy);
        }
    }
}

static void gl_bilerp_chars_changed(cvar_t *self)
{
    int     i;
    image_t *image;
    GLfloat param = self->integer ? GL_LINEAR : GL_NEAREST;

    // change all the existing charset texture objects
    for (i = 0, image = r_images; i < r_numImages; i++, image++) {
        if (image->type == IT_FONT) {
            GL_BindTexture(image->texnum);
            qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, param);
            qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, param);
        }
    }
}

static void gl_bilerp_pics_changed(cvar_t *self)
{
    int     i;
    image_t *image;
    GLfloat param = self->integer ? GL_LINEAR : GL_NEAREST;

    // change all the existing pic texture objects
    for (i = 0, image = r_images; i < r_numImages; i++, image++) {
        if (image->type == IT_PIC) {
            GL_BindTexture(image->texnum);
            qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, param);
            qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, param);
        }
    }

    // change scrap texture object
    if (!gl_noscrap->integer) {
        param = self->integer > 1 ? GL_LINEAR : GL_NEAREST;
        GL_BindTexture(TEXNUM_SCRAP);
        qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, param);
        qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, param);
    }
}


/*
===============
GL_TextureAlphaMode
===============
*/
static void GL_TextureAlphaMode(void)
{
    int     i;

    for (i = 0; i < numAlphaModes; i++) {
        if (!Q_stricmp(alphaModes[i].name, gl_texturealphamode->string)) {
            gl_tex_alpha_format = alphaModes[i].mode;
            return;
        }
    }

    Com_WPrintf("Bad texture alpha mode: %s\n", gl_texturealphamode->string);
    Cvar_Reset(gl_texturealphamode);
    gl_tex_alpha_format = alphaModes[0].mode;
}

static void gl_texturealphamode_g(genctx_t *ctx)
{
    int i;

    for (i = 0; i < numAlphaModes; i++) {
        if (!Prompt_AddMatch(ctx, alphaModes[i].name)) {
            break;
        }
    }
}

/*
===============
GL_TextureSolidMode
===============
*/
static void GL_TextureSolidMode(void)
{
    int     i;

    for (i = 0; i < numSolidModes; i++) {
        if (!Q_stricmp(solidModes[i].name, gl_texturesolidmode->string)) {
            gl_tex_solid_format = solidModes[i].mode;
            return;
        }
    }

    Com_WPrintf("Bad texture solid mode: %s\n", gl_texturesolidmode->string);
    Cvar_Reset(gl_texturesolidmode);
    gl_tex_solid_format = solidModes[0].mode;
}

static void gl_texturesolidmode_g(genctx_t *ctx)
{
    int i;

    for (i = 0; i < numSolidModes; i++) {
        if (!Prompt_AddMatch(ctx, solidModes[i].name)) {
            break;
        }
    }
}

/*
=============================================================================

  SCRAP ALLOCATION

  Allocate all the little status bar objects into a single texture
  to crutch up inefficient hardware / drivers

=============================================================================
*/

#define SCRAP_BLOCK_WIDTH       256
#define SCRAP_BLOCK_HEIGHT      256

static int scrap_inuse[SCRAP_BLOCK_WIDTH];
static byte scrap_data[SCRAP_BLOCK_WIDTH * SCRAP_BLOCK_HEIGHT];
static qboolean scrap_dirty;

#define Scrap_AllocBlock(w, h, s, t) \
    GL_AllocBlock(SCRAP_BLOCK_WIDTH, SCRAP_BLOCK_HEIGHT, scrap_inuse, w, h, s, t)

static void Scrap_Init(void)
{
    // make scrap texture initially transparent
    memset(scrap_data, 255, sizeof(scrap_data));
}

static void Scrap_Shutdown(void)
{
    int i;

    for (i = 0; i < SCRAP_BLOCK_WIDTH; i++) {
        scrap_inuse[i] = 0;
    }
    scrap_dirty = qfalse;
}

void Scrap_Upload(void)
{
    if (!scrap_dirty) {
        return;
    }
    GL_BindTexture(TEXNUM_SCRAP);
    GL_Upload8(scrap_data, SCRAP_BLOCK_WIDTH, SCRAP_BLOCK_HEIGHT, qfalse);
    scrap_dirty = qfalse;
}

/*
====================================================================

IMAGE FLOOD FILLING

====================================================================
*/

typedef struct {
    short       x, y;
} floodfill_t;

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP(off, dx, dy) \
    do { \
        if (pos[off] == fillcolor) { \
            pos[off] = 255; \
            fifo[inpt].x = x + (dx); \
            fifo[inpt].y = y + (dy); \
            inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
        } else if (pos[off] != 255) { \
            fdc = pos[off]; \
        } \
    } while(0)

/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes
=================
*/
static void R_FloodFillSkin(byte *skin, int skinwidth, int skinheight)
{
    byte                fillcolor = *skin; // assume this is the pixel to fill
    floodfill_t         fifo[FLOODFILL_FIFO_SIZE];
    int                 inpt = 0, outpt = 0;
    int                 filledcolor = -1;
    int                 i;

    if (filledcolor == -1) {
        filledcolor = 0;
        // attempt to find opaque black
        for (i = 0; i < 256; ++i)
            if (d_8to24table[i] == 255) {
                // alpha 1.0
                filledcolor = i;
                break;
            }
    }

    // can't fill to filled color or to transparent color
    // (used as visited marker)
    if ((fillcolor == filledcolor) || (fillcolor == 255)) {
        return;
    }

    fifo[inpt].x = 0, fifo[inpt].y = 0;
    inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

    while (outpt != inpt) {
        int         x = fifo[outpt].x, y = fifo[outpt].y;
        int         fdc = filledcolor;
        byte        *pos = &skin[x + skinwidth * y];

        outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

        if (x > 0) FLOODFILL_STEP(-1, -1, 0);
        if (x < skinwidth - 1) FLOODFILL_STEP(1, 1, 0);
        if (y > 0) FLOODFILL_STEP(-skinwidth, 0, -1);
        if (y < skinheight - 1) FLOODFILL_STEP(skinwidth, 0, 1);

        skin[x + skinwidth * y] = fdc;
    }
}

//=======================================================

static byte gammatable[256];
static byte intensitytable[256];
static byte gammaintensitytable[256];
static float colorscale;

/*
================
GL_GrayScaleTexture

Transform to grayscale by replacing color components with
overall pixel luminance computed from weighted color sum
================
*/
static void GL_GrayScaleTexture(byte *in, int inwidth, int inheight)
{
    int     i, c;
    byte    *p;
    float   r, g, b, y;

    p = in;
    c = inwidth * inheight;

    for (i = 0; i < c; i++, p += 4) {
        r = p[0];
        g = p[1];
        b = p[2];
        y = LUMINANCE(r, g, b);
        p[0] = y + (r - y) * colorscale;
        p[1] = y + (g - y) * colorscale;
        p[2] = y + (b - y) * colorscale;
    }
}

/*
================
GL_LightScaleTexture

Scale up the pixel values in a texture to increase the
lighting range
================
*/
static void GL_LightScaleTexture(byte *in, int inwidth, int inheight, qboolean mipmap)
{
    int     i, c;
    byte    *p;

    p = in;
    c = inwidth * inheight;

    if (mipmap) {
        for (i = 0; i < c; i++, p += 4) {
            p[0] = gammaintensitytable[p[0]];
            p[1] = gammaintensitytable[p[1]];
            p[2] = gammaintensitytable[p[2]];
        }
    } else {
        for (i = 0; i < c; i++, p += 4) {
            p[0] = gammatable[p[0]];
            p[1] = gammatable[p[1]];
            p[2] = gammatable[p[2]];
        }
    }
}

static void GL_ColorInvertTexture(byte *in, int inwidth, int inheight)
{
    int     i, c;
    byte    *p;

    p = in;
    c = inwidth * inheight;

    for (i = 0; i < c; i++, p += 4) {
        p[0] = 255 - p[0];
        p[1] = 255 - p[1];
        p[2] = 255 - p[2];
    }
}

// returns true if image should not be bilinear filtered
// (useful for small images in scarp, charsets, etc)
static inline qboolean is_nearest(void)
{
    if (gls.texnum[gls.tmu] == TEXNUM_SCRAP && gl_bilerp_pics->integer <= 1) {
        return qtrue; // hack for scrap texture
    }
    if (!upload_image) {
        return qfalse;
    }
    if (upload_image->type == IT_FONT) {
        return !gl_bilerp_chars->integer;
    }
    if (upload_image->type == IT_PIC) {
        return !gl_bilerp_pics->integer;
    }
    return qfalse;
}

static inline qboolean is_wall(void)
{
    if (!upload_image) {
        return qfalse;
    }
    if (upload_image->type != IT_WALL) {
        return qfalse; // not a wall texture
    }
    if (!upload_texinfo) {
        return qtrue; // don't know what type of surface it is
    }
    if (upload_texinfo->c.flags & (SURF_SKY | SURF_WARP)) {
        return qfalse; // don't grayscale or invert sky and liquid surfaces
    }
    return qtrue;
}

static inline qboolean is_downsample(void)
{
    if (!upload_image) {
        return qtrue;
    }
    if (upload_image->type != IT_SKIN) {
        return qtrue; // not a skin
    }
    return !!gl_downsample_skins->integer;
}

static inline qboolean is_clamp(void)
{
    if (gls.texnum[gls.tmu] == TEXNUM_SCRAP) {
        return qtrue; // hack for scrap texture
    }
    if (!upload_image) {
        return qfalse;
    }
    if (upload_image->type == IT_FONT) {
        return qtrue;
    }
    if (upload_image->type == IT_PIC) {
        return !Q_stristr(upload_image->name, "backtile"); // hack for backtile
    }
    return qfalse;
}

static inline qboolean is_alpha(byte *data, int width, int height)
{
    int         i, c;
    byte        *scan;

    c = width * height;
    scan = data + 3;
    for (i = 0; i < c; i++, scan += 4) {
        if (*scan != 255) {
            return qtrue;
        }
    }

    return qfalse;
}

/*
===============
GL_Upload32
===============
*/
static qboolean GL_Upload32(byte *data, int width, int height, qboolean mipmap)
{
    byte        *scaled;
    int         scaled_width, scaled_height;
    int         comp;
    qboolean    isalpha, picmip;
    int         maxsize;

    // find the next-highest power of two
    scaled_width = npot32(width);
    scaled_height = npot32(height);

    // save the flag indicating if costly resampling can be avoided
    picmip = scaled_width == width && scaled_height == height;

    maxsize = gl_config.maxTextureSize;

    if (mipmap && is_downsample()) {
        // round world textures down, if requested
        if (gl_round_down->integer) {
            if (scaled_width > width)
                scaled_width >>= 1;
            if (scaled_height > height)
                scaled_height >>= 1;
        }

        // let people sample down the world textures for speed
        scaled_width >>= gl_picmip->integer;
        scaled_height >>= gl_picmip->integer;

        if (gl_maxmip->integer > 0) {
            maxsize = 1 << Cvar_ClampInteger(gl_maxmip, 1, 12);
            if (maxsize > gl_config.maxTextureSize) {
                maxsize = gl_config.maxTextureSize;
            }
        }
    }

    // don't ever bother with >256 textures
    while (scaled_width > maxsize || scaled_height > maxsize) {
        scaled_width >>= 1;
        scaled_height >>= 1;
    }

    if (scaled_width < 1)
        scaled_width = 1;
    if (scaled_height < 1)
        scaled_height = 1;

    upload_width = scaled_width;
    upload_height = scaled_height;

    // set colorscale and lightscale before mipmap
    comp = gl_tex_solid_format;
    if (is_wall() && colorscale != 1) {
        GL_GrayScaleTexture(data, width, height);
        if (colorscale == 0) {
            comp = GL_LUMINANCE;
        }
    }

    if (!(r_config.flags & QVF_GAMMARAMP) &&
        (mipmap || gl_gamma_scale_pics->integer)) {
        GL_LightScaleTexture(data, width, height, mipmap);
    }

    if (is_wall() && gl_invert->integer) {
        GL_ColorInvertTexture(data, width, height);
    }

    if (scaled_width == width && scaled_height == height) {
        // optimized case, do nothing
        scaled = data;
    } else if (picmip) {
        // optimized case, use faster mipmap operation
        scaled = data;
        while (width > scaled_width || height > scaled_height) {
            IMG_MipMap(scaled, scaled, width, height);
            width >>= 1;
            height >>= 1;
        }
    } else {
        scaled = FS_AllocTempMem(scaled_width * scaled_height * 4);
        IMG_ResampleTexture(data, width, height, scaled,
                            scaled_width, scaled_height);
    }

    // scan the texture for any non-255 alpha
    isalpha = is_alpha(scaled, scaled_width, scaled_height);
    if (isalpha) {
        comp = gl_tex_alpha_format;
    }

    qglTexImage2D(GL_TEXTURE_2D, 0, comp, scaled_width, scaled_height, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, scaled);

    c.texUploads++;

    if (mipmap) {
        int miplevel = 0;

        while (scaled_width > 1 || scaled_height > 1) {
            IMG_MipMap(scaled, scaled, scaled_width, scaled_height);
            scaled_width >>= 1;
            scaled_height >>= 1;
            if (scaled_width < 1)
                scaled_width = 1;
            if (scaled_height < 1)
                scaled_height = 1;
            miplevel++;
            qglTexImage2D(GL_TEXTURE_2D, miplevel, comp, scaled_width,
                          scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);
        }
    }

    if (mipmap) {
        qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
        qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
    } else if (is_nearest()) {
        qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    } else {
        qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    if (!mipmap && is_clamp()) {
        qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    } else {
        qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    if (mipmap && gl_config.maxAnisotropy >= 2) {
        qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                         gl_filter_anisotropy);
    }

    if (scaled != data) {
        FS_FreeTempMem(scaled);
    }

    return isalpha;
}

/*
===============
GL_Upload8

Returns has_alpha
===============
*/
static qboolean GL_Upload8(byte *data, int width, int height, qboolean mipmap)
{
    byte    buffer[MAX_PALETTED_PIXELS * 4];
    byte    *dest;
    int     i, s;
    int     p;

    s = width * height;
    if (s > MAX_PALETTED_PIXELS) {
        // should never happen
        Com_Error(ERR_FATAL, "GL_Upload8: too large");
    }

    dest = buffer;
    for (i = 0; i < s; i++) {
        p = data[i];
        *(uint32_t *)dest = d_8to24table[p];

        if (p == 255) {
            // transparent, so scan around for another color
            // to avoid alpha fringes
            // FIXME: do a full flood fill so mips work...
            if (i > width && data[i - width] != 255)
                p = data[i - width];
            else if (i < s - width && data[i + width] != 255)
                p = data[i + width];
            else if (i > 0 && data[i - 1] != 255)
                p = data[i - 1];
            else if (i < s - 1 && data[i + 1] != 255)
                p = data[i + 1];
            else
                p = 0;
            // copy rgb components
            dest[0] = ((byte *)&d_8to24table[p])[0];
            dest[1] = ((byte *)&d_8to24table[p])[1];
            dest[2] = ((byte *)&d_8to24table[p])[2];
        }

        dest += 4;
    }

    return GL_Upload32(buffer, width, height, mipmap);

}

/*
================
IMG_Load
================
*/
void IMG_Load(image_t *image, byte *pic, int width, int height)
{
    qboolean mipmap, transparent;
    byte *src, *dst, *ptr;
    int i, j, s, t;

    if (!pic) {
        Com_Error(ERR_FATAL, "%s: NULL", __func__);
    }

    upload_image = image;

    // load small 8-bit pics onto the scrap
    if (image->type == IT_PIC && (image->flags & IF_PALETTED) &&
        width < 64 && height < 64 && !gl_noscrap->integer &&
        Scrap_AllocBlock(width, height, &s, &t)) {
        src = pic;
        dst = &scrap_data[t * SCRAP_BLOCK_WIDTH + s];
        for (i = 0; i < height; i++) {
            ptr = dst;
            for (j = 0; j < width; j++) {
                *ptr++ = *src++;
            }
            dst += SCRAP_BLOCK_WIDTH;
        }

        image->texnum = TEXNUM_SCRAP;
        image->upload_width = width;
        image->upload_height = height;
        image->flags |= IF_SCRAP | IF_TRANSPARENT;
        image->sl = (s + 0.01f) / (float)SCRAP_BLOCK_WIDTH;
        image->sh = (s + width - 0.01f) / (float)SCRAP_BLOCK_WIDTH;
        image->tl = (t + 0.01f) / (float)SCRAP_BLOCK_HEIGHT;
        image->th = (t + height - 0.01f) / (float)SCRAP_BLOCK_HEIGHT;

        scrap_dirty = qtrue;
        if (!gl_static.registering) {
            Scrap_Upload();
        }

        upload_image = NULL;
        return;
    }

    if (image->type == IT_SKIN && (image->flags & IF_PALETTED))
        R_FloodFillSkin(pic, width, height);

    mipmap = (image->type == IT_WALL || image->type == IT_SKIN);
    image->texnum = (image - r_images);
    GL_BindTexture(image->texnum);
    if (image->flags & IF_PALETTED) {
        transparent = GL_Upload8(pic, width, height, mipmap);
    } else {
        transparent = GL_Upload32(pic, width, height, mipmap);
    }
    if (transparent) {
        image->flags |= IF_TRANSPARENT;
    }
    image->upload_width = upload_width;     // after power of 2 and scales
    image->upload_height = upload_height;
    image->sl = 0;
    image->sh = 1;
    image->tl = 0;
    image->th = 1;

    upload_image = NULL;
}

void IMG_Unload(image_t *image)
{
    if (image->texnum > 0 && image->texnum < MAX_RIMAGES) {
        if (gls.texnum[gls.tmu] == image->texnum)
            gls.texnum[gls.tmu] = 0;
        qglDeleteTextures(1, &image->texnum);
        image->texnum = 0;
    }
}

static void GL_BuildIntensityTable(void)
{
    int i, j;
    float f;

    f = Cvar_ClampValue(gl_intensity, 1, 5);
    gl_static.inverse_intensity = 1 / f;
    for (i = 0; i < 256; i++) {
        j = i * f;
        if (j > 255) {
            j = 255;
        }
        intensitytable[i] = j;
    }
}

static void GL_BuildGammaTables(void)
{
    int i;
    float inf, g = gl_gamma->value;

    if (g == 1.0f) {
        for (i = 0; i < 256; i++) {
            gammatable[i] = i;
            gammaintensitytable[i] = intensitytable[i];
        }
    } else {
        for (i = 0; i < 256; i++) {
            inf = 255 * pow((i + 0.5) / 255.5, g) + 0.5;
            if (inf > 255) {
                inf = 255;
            }
            gammatable[i] = inf;
            gammaintensitytable[i] = intensitytable[gammatable[i]];
        }
    }
}

static void gl_gamma_changed(cvar_t *self)
{
    GL_BuildGammaTables();
    VID_UpdateGamma(gammatable);
}

static const byte dottexture[8][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
};

static void GL_InitDefaultTexture(void)
{
    int i, j;
    byte pixels[8 * 8 * 4];
    byte *dst;
    image_t *ntx;

    dst = pixels;
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            dst[0] = dottexture[i & 3][j & 3] * 255;
            dst[1] = 0;
            dst[2] = 0;
            dst[3] = 255;
            dst += 4;
        }
    }

    GL_BindTexture(TEXNUM_DEFAULT);
    GL_Upload32(pixels, 8, 8, qtrue);

    // fill in notexture image
    ntx = R_NOTEXTURE;
    ntx->width = ntx->upload_width = 8;
    ntx->height = ntx->upload_height = 8;
    ntx->type = IT_WALL;
    ntx->flags = 0;
    ntx->texnum = TEXNUM_DEFAULT;
    ntx->sl = 0;
    ntx->sh = 1;
    ntx->tl = 0;
    ntx->th = 1;
}

static void GL_InitParticleTexture(void)
{
    byte pixels[16 * 16 * 4];
    byte *dst;
    float x, y, f;
    int i, j;

    dst = pixels;
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j++) {
            x = j - 16 / 2 + 0.5f;
            y = i - 16 / 2 + 0.5f;
            f = sqrt(x * x + y * y);
            f = 1.0f - f / (16 / 2 - 0.5f);
            dst[0] = 255;
            dst[1] = 255;
            dst[2] = 255;
            dst[3] = 255 * clamp(f, 0, 1);
            dst += 4;
        }
    }

    GL_BindTexture(TEXNUM_PARTICLE);
    GL_Upload32(pixels, 16, 16, qfalse);
    qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
}

static void GL_InitWhiteImage(void)
{
    uint32_t pixel;

    pixel = U32_WHITE;
    GL_BindTexture(TEXNUM_WHITE);
    GL_Upload32((byte *)&pixel, 1, 1, qfalse);
    qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    pixel = U32_BLACK;
    GL_BindTexture(TEXNUM_BLACK);
    GL_Upload32((byte *)&pixel, 1, 1, qfalse);
    qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

static void GL_InitBeamTexture(void)
{
    byte pixels[16 * 16 * 4];
    byte *dst;
    float f;
    int i, j;

    dst = pixels;
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 16; j++) {
            f = abs(j - 16 / 2) - 0.5f;
            f = 1.0f - f / (16 / 2 - 2.5f);
            dst[0] = 255;
            dst[1] = 255;
            dst[2] = 255;
            dst[3] = 255 * clamp(f, 0, 1);
            dst += 4;
        }
    }

    GL_BindTexture(TEXNUM_BEAM);
    GL_Upload32(pixels, 16, 16, qfalse);
    qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

/*
===============
GL_InitImages
===============
*/
void GL_InitImages(void)
{
    gl_bilerp_chars = Cvar_Get("gl_bilerp_chars", "0", 0);
    gl_bilerp_chars->changed = gl_bilerp_chars_changed;
    gl_bilerp_pics = Cvar_Get("gl_bilerp_pics", "1", 0);
    gl_bilerp_pics->changed = gl_bilerp_pics_changed;
    gl_texturemode = Cvar_Get("gl_texturemode",
                              "GL_LINEAR_MIPMAP_LINEAR", CVAR_ARCHIVE);
    gl_texturemode->changed = gl_texturemode_changed;
    gl_texturemode->generator = gl_texturemode_g;
    gl_anisotropy = Cvar_Get("gl_anisotropy", "1", 0);
    gl_anisotropy->changed = gl_anisotropy_changed;
    gl_noscrap = Cvar_Get("gl_noscrap", "0", CVAR_FILES);
    gl_round_down = Cvar_Get("gl_round_down", "0", CVAR_FILES);
    gl_picmip = Cvar_Get("gl_picmip", "0", CVAR_FILES);
    gl_maxmip = Cvar_Get("gl_maxmip", "0", CVAR_FILES);
    gl_downsample_skins = Cvar_Get("gl_downsample_skins", "1", CVAR_FILES);
    gl_gamma_scale_pics = Cvar_Get("gl_gamma_scale_pics", "0", CVAR_FILES);
    gl_texturealphamode = Cvar_Get("gl_texturealphamode",
                                   "default", CVAR_ARCHIVE | CVAR_FILES);
    gl_texturealphamode->generator = gl_texturealphamode_g;
    gl_texturesolidmode = Cvar_Get("gl_texturesolidmode",
                                   "default", CVAR_ARCHIVE | CVAR_FILES);
    gl_texturesolidmode->generator = gl_texturesolidmode_g;
    gl_saturation = Cvar_Get("gl_saturation", "1", CVAR_FILES);
    gl_intensity = Cvar_Get("intensity", "1", CVAR_FILES);
    gl_invert = Cvar_Get("gl_invert", "0", CVAR_FILES);
    if (r_config.flags & QVF_GAMMARAMP) {
        gl_gamma = Cvar_Get("vid_gamma", "1", CVAR_ARCHIVE);
        gl_gamma->changed = gl_gamma_changed;
        gl_gamma->flags &= ~CVAR_FILES;
    } else {
        gl_gamma = Cvar_Get("vid_gamma", "1", CVAR_ARCHIVE | CVAR_FILES);
    }

    IMG_Init();

    IMG_GetPalette();

    GL_BuildIntensityTable();

    if (r_config.flags & QVF_GAMMARAMP) {
        gl_gamma_changed(gl_gamma);
    } else {
        GL_BuildGammaTables();
    }

    // FIXME: the name 'saturation' is misleading in this context
    colorscale = Cvar_ClampValue(gl_saturation, 0, 1);

    GL_TextureAlphaMode();
    GL_TextureSolidMode();

    gl_texturemode_changed(gl_texturemode);
    gl_anisotropy_changed(gl_anisotropy);
    gl_bilerp_chars_changed(gl_bilerp_chars);
    gl_bilerp_pics_changed(gl_bilerp_pics);

    upload_image = NULL;
    upload_texinfo = NULL;

    Scrap_Init();

    GL_InitDefaultTexture();
    GL_InitParticleTexture();
    GL_InitWhiteImage();
    GL_InitBeamTexture();
}

#ifdef _DEBUG
extern image_t *r_charset;
#endif

/*
===============
GL_ShutdownImages
===============
*/
void GL_ShutdownImages(void)
{
    GLuint texnums[NUM_TEXNUMS];
    int i, j;

    gl_bilerp_chars->changed = NULL;
    gl_bilerp_pics->changed = NULL;
    gl_texturemode->changed = NULL;
    gl_texturemode->generator = NULL;
    gl_texturealphamode->generator = NULL;
    gl_texturesolidmode->generator = NULL;
    gl_anisotropy->changed = NULL;
    gl_gamma->changed = NULL;

    // delete auto textures
    j = TEXNUM_LIGHTMAP + lm.highwater - TEXNUM_DEFAULT;
    for (i = 0; i < j; i++) {
        texnums[i] = TEXNUM_DEFAULT + i;
    }
    qglDeleteTextures(j, texnums);

    lm.highwater = 0;

#ifdef _DEBUG
    r_charset = NULL;
#endif

    IMG_FreeAll();
    IMG_Shutdown();

    Scrap_Shutdown();
}

