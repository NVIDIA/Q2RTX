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

static int upload_width;
static int upload_height;
static bool upload_alpha;

static int max_texture_size;

static cvar_t *gl_noscrap;
static cvar_t *gl_round_down;
static cvar_t *gl_picmip;
static cvar_t *gl_downsample_skins;
static cvar_t *gl_gamma_scale_pics;
static cvar_t *gl_bilerp_chars;
static cvar_t *gl_bilerp_pics;
static cvar_t *gl_upscale_pcx;
static cvar_t *gl_texturemode;
static cvar_t *gl_texturebits;
static cvar_t *gl_anisotropy;
static cvar_t *gl_saturation;
static cvar_t *gl_gamma;
static cvar_t *gl_invert;
static cvar_t *gl_partshape;

cvar_t *gl_intensity;

static int GL_UpscaleLevel(int width, int height, imagetype_t type, imageflags_t flags);
static void GL_Upload32(byte *data, int width, int height, int baselevel, imagetype_t type, imageflags_t flags);
static void GL_Upscale32(byte *data, int width, int height, int maxlevel, imagetype_t type, imageflags_t flags);
static void GL_SetFilterAndRepeat(imagetype_t type, imageflags_t flags);
static void GL_InitRawTexture(void);

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
    { "GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR },
    { "MAG_NEAREST", GL_LINEAR_MIPMAP_LINEAR, GL_NEAREST },
};

static const int numFilterModes = q_countof(filterModes);

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
        if (image->type == IT_WALL || image->type == IT_SKIN || image->type == IT_SKY) {
            GL_ForceTexture(0, image->texnum);
            GL_SetFilterAndRepeat(image->type, image->flags);
        }
    }
}

static void gl_texturemode_g(genctx_t *ctx)
{
    int i;

    ctx->ignorecase = true;
    for (i = 0; i < numFilterModes; i++)
        Prompt_AddMatch(ctx, filterModes[i].name);
}

static void gl_anisotropy_changed(cvar_t *self)
{
    int     i;
    image_t *image;
    GLfloat value = 1;

    if (!(gl_config.caps & QGL_CAP_TEXTURE_ANISOTROPY))
        return;

    qglGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &value);
    gl_filter_anisotropy = Cvar_ClampValue(self, 1, value);

    // change all the existing mipmap texture objects
    for (i = 0, image = r_images; i < r_numImages; i++, image++) {
        if (image->type == IT_WALL || image->type == IT_SKIN) {
            GL_ForceTexture(0, image->texnum);
            GL_SetFilterAndRepeat(image->type, image->flags);
        }
    }
}

static void gl_bilerp_chars_changed(cvar_t *self)
{
    int     i;
    image_t *image;

    // change all the existing charset texture objects
    for (i = 0, image = r_images; i < r_numImages; i++, image++) {
        if (image->type == IT_FONT) {
            GL_ForceTexture(0, image->texnum);
            GL_SetFilterAndRepeat(image->type, image->flags);
        }
    }
}

static void gl_bilerp_pics_changed(cvar_t *self)
{
    int     i;
    image_t *image;

    // change all the existing pic texture objects
    for (i = 0, image = r_images; i < r_numImages; i++, image++) {
        if (image->type == IT_PIC) {
            GL_ForceTexture(0, image->texnum);
            GL_SetFilterAndRepeat(image->type, image->flags);
        }
    }

    GL_InitRawTexture();
}

static void gl_texturebits_changed(cvar_t *self)
{
    if (!(gl_config.caps & QGL_CAP_TEXTURE_BITS)) {
        gl_tex_alpha_format = GL_RGBA;
        gl_tex_solid_format = GL_RGBA;
    } else if (self->integer > 16) {
        gl_tex_alpha_format = GL_RGBA8;
        gl_tex_solid_format = GL_RGB8;
    } else if (self->integer > 8)  {
        gl_tex_alpha_format = GL_RGBA4;
        gl_tex_solid_format = GL_RGB5;
    } else if (self->integer > 0)  {
        gl_tex_alpha_format = GL_RGBA2;
        gl_tex_solid_format = GL_R3_G3_B2;
    } else {
        gl_tex_alpha_format = GL_RGBA;
        gl_tex_solid_format = GL_RGB;
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
static byte scrap_data[SCRAP_BLOCK_WIDTH * SCRAP_BLOCK_HEIGHT * 4];
static bool scrap_dirty;

#define Scrap_AllocBlock(w, h, s, t) \
    GL_AllocBlock(SCRAP_BLOCK_WIDTH, SCRAP_BLOCK_HEIGHT, scrap_inuse, w, h, s, t)

static void Scrap_Init(void)
{
    // make scrap texture initially transparent
    memset(scrap_data, 0, sizeof(scrap_data));
}

static void Scrap_Shutdown(void)
{
    int i;

    for (i = 0; i < SCRAP_BLOCK_WIDTH; i++) {
        scrap_inuse[i] = 0;
    }

    scrap_dirty = false;
}

void Scrap_Upload(void)
{
    byte *data;
    int maxlevel;

    if (!scrap_dirty) {
        return;
    }

    GL_ForceTexture(0, TEXNUM_SCRAP);

    // make a copy so that effects like gamma scaling don't accumulate
    data = FS_AllocTempMem(sizeof(scrap_data));
    memcpy(data, scrap_data, sizeof(scrap_data));

    maxlevel = GL_UpscaleLevel(SCRAP_BLOCK_WIDTH, SCRAP_BLOCK_HEIGHT, IT_PIC, IF_SCRAP);
    if (maxlevel) {
        GL_Upscale32(data, SCRAP_BLOCK_WIDTH, SCRAP_BLOCK_HEIGHT, maxlevel, IT_PIC, IF_SCRAP);
        GL_SetFilterAndRepeat(IT_PIC, IF_SCRAP | IF_UPSCALED);
    } else {
        GL_Upload32(data, SCRAP_BLOCK_WIDTH, SCRAP_BLOCK_HEIGHT, maxlevel, IT_PIC, IF_SCRAP);
        GL_SetFilterAndRepeat(IT_PIC, IF_SCRAP);
    }

    FS_FreeTempMem(data);

    scrap_dirty = false;
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
static int GL_GrayScaleTexture(byte *in, int inwidth, int inheight, imagetype_t type, imageflags_t flags)
{
    int     i, c;
    byte    *p;
    float   r, g, b, y;

    if (type != IT_WALL)
        return gl_tex_solid_format; // only grayscale world textures
    if (flags & IF_TURBULENT)
        return gl_tex_solid_format; // don't grayscale turbulent surfaces
    if (colorscale == 1)
        return gl_tex_solid_format;

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

    if (colorscale == 0 && (gl_config.caps & QGL_CAP_TEXTURE_BITS))
        return GL_LUMINANCE;

    return gl_tex_solid_format;
}

/*
================
GL_LightScaleTexture

Scale up the pixel values in a texture to increase the
lighting range
================
*/
static void GL_LightScaleTexture(byte *in, int inwidth, int inheight, imagetype_t type, imageflags_t flags)
{
    int     i, c;
    byte    *p;

    if (r_config.flags & QVF_GAMMARAMP)
        return;

    p = in;
    c = inwidth * inheight;

    if (type == IT_WALL || type == IT_SKIN) {
        for (i = 0; i < c; i++, p += 4) {
            p[0] = gammaintensitytable[p[0]];
            p[1] = gammaintensitytable[p[1]];
            p[2] = gammaintensitytable[p[2]];
        }
    } else if (gl_gamma_scale_pics->integer) {
        for (i = 0; i < c; i++, p += 4) {
            p[0] = gammatable[p[0]];
            p[1] = gammatable[p[1]];
            p[2] = gammatable[p[2]];
        }
    }
}

static void GL_ColorInvertTexture(byte *in, int inwidth, int inheight, imagetype_t type, imageflags_t flags)
{
    int     i, c;
    byte    *p;

    if (type != IT_WALL)
        return; // only invert world textures
    if (flags & IF_TURBULENT)
        return; // don't invert turbulent surfaces
    if (!gl_invert->integer)
        return;

    p = in;
    c = inwidth * inheight;

    for (i = 0; i < c; i++, p += 4) {
        p[0] = 255 - p[0];
        p[1] = 255 - p[1];
        p[2] = 255 - p[2];
    }
}

static bool GL_TextureHasAlpha(byte *data, int width, int height)
{
    int         i, c;
    byte        *scan;

    c = width * height;
    scan = data + 3;
    for (i = 0; i < c; i++, scan += 4) {
        if (*scan != 255) {
            return true;
        }
    }

    return false;
}

static bool GL_MakePowerOfTwo(int *width, int *height)
{
    if (!(*width & (*width - 1)) && !(*height & (*height - 1)))
        return true;    // already power of two

    if (gl_config.caps & QGL_CAP_TEXTURE_NON_POWER_OF_TWO)
        return false;   // assume full NPOT texture support

    *width = Q_npot32(*width);
    *height = Q_npot32(*height);
    return false;
}

/*
===============
GL_Upload32
===============
*/
static void GL_Upload32(byte *data, int width, int height, int baselevel, imagetype_t type, imageflags_t flags)
{
    byte        *scaled;
    int         scaled_width, scaled_height, comp;
    bool        power_of_two;

    scaled_width = width;
    scaled_height = height;
    power_of_two = GL_MakePowerOfTwo(&scaled_width, &scaled_height);

    if (type == IT_WALL || (type == IT_SKIN && gl_downsample_skins->integer)) {
        // round world textures down, if requested
        if (gl_round_down->integer) {
            if (scaled_width > width)
                scaled_width >>= 1;
            if (scaled_height > height)
                scaled_height >>= 1;
        }

        // let people sample down the world textures for speed
        int shift = Cvar_ClampInteger(gl_picmip, 0, 31);
        scaled_width >>= shift;
        scaled_height >>= shift;
    }

    // don't ever bother with >256 textures
    while (scaled_width > max_texture_size || scaled_height > max_texture_size) {
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
    comp = GL_GrayScaleTexture(data, width, height, type, flags);
    GL_LightScaleTexture(data, width, height, type, flags);
    GL_ColorInvertTexture(data, width, height, type, flags);

    if (scaled_width == width && scaled_height == height) {
        // optimized case, do nothing
        scaled = data;
    } else if (power_of_two) {
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

    if (flags & IF_TRANSPARENT) {
        upload_alpha = true;
    } else if (flags & IF_OPAQUE) {
        upload_alpha = false;
    } else {
        // scan the texture for any non-255 alpha
        upload_alpha = GL_TextureHasAlpha(scaled, scaled_width, scaled_height);
    }

    if (upload_alpha) {
        comp = gl_tex_alpha_format;
    }

    qglTexImage2D(GL_TEXTURE_2D, baselevel, comp, scaled_width,
                  scaled_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, scaled);

    c.texUploads++;

    if (type == IT_WALL || type == IT_SKIN) {
        if (qglGenerateMipmap) {
            qglGenerateMipmap(GL_TEXTURE_2D);
        } else {
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
    }

    if (scaled != data) {
        FS_FreeTempMem(scaled);
    }
}

static int GL_UpscaleLevel(int width, int height, imagetype_t type, imageflags_t flags)
{
    int maxlevel;

    // only upscale pics, fonts and sprites
    if (type != IT_PIC && type != IT_FONT && type != IT_SPRITE)
        return 0;

    // only upscale 8-bit and small 32-bit pics
    if (!(flags & (IF_PALETTED | IF_SCRAP)))
        return 0;

    GL_MakePowerOfTwo(&width, &height);

    maxlevel = Cvar_ClampInteger(gl_upscale_pcx, 0, 2);
    while (maxlevel) {
        int maxsize = max_texture_size >> maxlevel;

        // don't bother upscaling larger than max texture size
        if (width <= maxsize && height <= maxsize)
            break;

        maxlevel--;
    }

    return maxlevel;
}

static void GL_Upscale32(byte *data, int width, int height, int maxlevel, imagetype_t type, imageflags_t flags)
{
    byte    *buffer;

    buffer = FS_AllocTempMem((width * height) << ((maxlevel + 1) * 2));

    if (maxlevel >= 2) {
        HQ4x_Render((uint32_t *)buffer, (uint32_t *)data, width, height);
        GL_Upload32(buffer, width * 4, height * 4, maxlevel - 2, type, flags);
    }

    if (maxlevel >= 1) {
        HQ2x_Render((uint32_t *)buffer, (uint32_t *)data, width, height);
        GL_Upload32(buffer, width * 2, height * 2, maxlevel - 1, type, flags);
    }

    FS_FreeTempMem(buffer);

    GL_Upload32(data, width, height, maxlevel, type, flags);

    if (gl_config.caps & QGL_CAP_TEXTURE_MAX_LEVEL)
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxlevel);

    // adjust LOD for resampled textures
    if (upload_width != width || upload_height != height) {
        float du    = upload_width / (float)width;
        float dv    = upload_height / (float)height;
        float bias  = -log(max(du, dv)) / M_LN2;

        if (gl_config.caps & QGL_CAP_TEXTURE_LOD_BIAS)
            qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, bias);
    }
}

static void GL_SetFilterAndRepeat(imagetype_t type, imageflags_t flags)
{
    if (type == IT_WALL || type == IT_SKIN) {
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_min);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
    } else if (type == IT_SKY) {
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter_max);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter_max);
    } else {
        bool    nearest;

        if (flags & IF_NEAREST) {
            nearest = true;
        } else if(flags & IF_BILERP) {
            nearest = true;
        } else if (type == IT_FONT) {
            nearest = (gl_bilerp_chars->integer == 0);
        } else if (type == IT_PIC) {
            if (flags & IF_SCRAP)
                nearest = (gl_bilerp_pics->integer == 0 || gl_bilerp_pics->integer == 1);
            else
                nearest = (gl_bilerp_pics->integer == 0);
        } else {
            nearest = false;
        }

        if ((flags & IF_UPSCALED) && (gl_config.caps & QGL_CAP_TEXTURE_MAX_LEVEL)) {
            if (nearest) {
                qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
                qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            } else {
                qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
                qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            }
        } else {
            if (nearest) {
                qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            } else {
                qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            }
        }
    }

    if (gl_config.caps & QGL_CAP_TEXTURE_ANISOTROPY) {
        if (type == IT_WALL || type == IT_SKIN)
            qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, gl_filter_anisotropy);
        else
            qglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 1);
    }

    if (type == IT_WALL || type == IT_SKIN || (flags & IF_REPEAT)) {
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    } else if (gl_config.caps & QGL_CAP_TEXTURE_CLAMP_TO_EDGE) {
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    }
}

/*
================
IMG_Load
================
*/
void IMG_Load_GL(image_t *image, byte *pic)
{
    byte    *src, *dst;
    int     i, s, t, maxlevel;
    int     width, height;

    width = image->upload_width;
    height = image->upload_height;

    // load small pics onto the scrap
    if (image->type == IT_PIC && width < 64 && height < 64 &&
        gl_noscrap->integer == 0 && Scrap_AllocBlock(width, height, &s, &t)) {
        src = pic;
        dst = &scrap_data[(t * SCRAP_BLOCK_WIDTH + s) * 4];
        for (i = 0; i < height; i++) {
            memcpy(dst, src, width * 4);
            src += width * 4;
            dst += SCRAP_BLOCK_WIDTH * 4;
        }

        image->texnum = TEXNUM_SCRAP;
        image->flags |= IF_SCRAP | IF_TRANSPARENT;
        image->sl = (s + 0.01f) / SCRAP_BLOCK_WIDTH;
        image->sh = (s + width - 0.01f) / SCRAP_BLOCK_WIDTH;
        image->tl = (t + 0.01f) / SCRAP_BLOCK_HEIGHT;
        image->th = (t + height - 0.01f) / SCRAP_BLOCK_HEIGHT;

        maxlevel = GL_UpscaleLevel(SCRAP_BLOCK_WIDTH, SCRAP_BLOCK_HEIGHT, IT_PIC, IF_SCRAP);
        if (maxlevel)
            image->flags |= IF_UPSCALED;

        scrap_dirty = true;
    } else {
        qglGenTextures(1, &image->texnum);
        GL_ForceTexture(0, image->texnum);

        maxlevel = GL_UpscaleLevel(width, height, image->type, image->flags);
        if (maxlevel) {
            GL_Upscale32(pic, width, height, maxlevel, image->type, image->flags);
            image->flags |= IF_UPSCALED;
        } else {
            GL_Upload32(pic, width, height, maxlevel, image->type, image->flags);
        }

        GL_SetFilterAndRepeat(image->type, image->flags);

        if (upload_alpha) {
            image->flags |= IF_TRANSPARENT;
        }
        image->upload_width = upload_width << maxlevel;     // after power of 2 and scales
        image->upload_height = upload_height << maxlevel;
        image->sl = 0;
        image->sh = 1;
        image->tl = 0;
        image->th = 1;
    }

    // don't need pics in memory after GL upload
    Z_Free(pic);
}

void IMG_Unload_GL(image_t *image)
{
    if (image->texnum && !(image->flags & IF_SCRAP)) {
        if (gls.texnums[0] == image->texnum)
            gls.texnums[0] = 0;
        qglDeleteTextures(1, &image->texnum);
        image->texnum = 0;
    }
}

// for screenshots
void IMG_ReadPixels_GL(screenshot_t *s)
{
    int format = gl_config.ver_es ? GL_RGBA : GL_RGB;
    int align = 4;

    qglGetIntegerv(GL_PACK_ALIGNMENT, &align);

    s->bpp = format == GL_RGBA ? 4 : 3;
    s->rowbytes = ALIGN(r_config.width * s->bpp, align);
    s->pixels = Z_Malloc(s->rowbytes * r_config.height);
    s->width = r_config.width;
    s->height = r_config.height;

    qglReadPixels(0, 0, r_config.width, r_config.height,
                  format, GL_UNSIGNED_BYTE, s->pixels);

    // Pack the pixels with ideal pitch because some image writing functions
    // (e.g. stbi_write_jpg) do not support pitch specification for some reason.
    int ideal_pitch = r_config.width * 3;
    if (s->rowbytes > ideal_pitch)
    {
        for (int row = 1; row < r_config.height; row++)
        {
            byte* src = s->pixels + row * s->rowbytes;
            byte* dst = s->pixels + row * ideal_pitch;
            memcpy(dst, src, ideal_pitch);
        }

        s->rowbytes = ideal_pitch;
    }
}

static void GL_BuildIntensityTable(void)
{
    int i, j;
    float f;

    f = Cvar_ClampValue(gl_intensity, 1, 5);
    if (gl_static.use_shaders)
        f = 1;
    for (i = 0; i < 256; i++) {
        j = i * f;
        if (j > 255) {
            j = 255;
        }
        intensitytable[i] = j;
    }

    j = 255.0f / f;
    gl_static.inverse_intensity_33 = MakeColor(j, j, j, 85);
    gl_static.inverse_intensity_66 = MakeColor(j, j, j, 170);
    gl_static.inverse_intensity_100 = MakeColor(j, j, j, 255);
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
            inf = 255 * pow(i / 255.0, g);
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
    if (vid.update_gamma)
        vid.update_gamma(gammatable);
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

    GL_ForceTexture(0, TEXNUM_DEFAULT);
    GL_Upload32(pixels, 8, 8, 0, IT_WALL, IF_TURBULENT);
    GL_SetFilterAndRepeat(IT_WALL, IF_TURBULENT);

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
    int shape = Cvar_ClampInteger(gl_partshape, 0, 2);
    int flags = IF_TRANSPARENT;

    if (shape == 0 || shape == 2) {
        dst = pixels;
        for (i = 0; i < 16; i++) {
            for (j = 0; j < 16; j++) {
                x = j - 16 / 2 + 0.5f;
                y = i - 16 / 2 + 0.5f;
                f = sqrtf(x * x + y * y);
                f = 1.0f - f / ((16 - shape) / 2 - 0.5f);
                f *= 1 << shape;
                dst[0] = 255;
                dst[1] = 255;
                dst[2] = 255;
                dst[3] = 255 * Q_clipf(f, 0, 1 - shape * 0.2f);
                dst += 4;
            }
        }
    } else {
        flags |= IF_NEAREST;
        memset(pixels, 0, sizeof(pixels));
        for (i = 3; i <= 12; i++) {
            for (j = 3; j <= 12; j++) {
                dst = pixels + (i * 16 + j) * 4;
                dst[0] = 255;
                dst[1] = 255;
                dst[2] = 255;
                dst[3] = 255 * 0.6f;
            }
        }
    }

    GL_ForceTexture(0, TEXNUM_PARTICLE);
    GL_Upload32(pixels, 16, 16, 0, IT_SPRITE, flags);
    GL_SetFilterAndRepeat(IT_SPRITE, flags);
}

static void GL_InitWhiteImage(void)
{
    uint32_t pixel;

    pixel = U32_WHITE;
    GL_ForceTexture(0, TEXNUM_WHITE);
    GL_Upload32((byte *)&pixel, 1, 1, 0, IT_SPRITE, IF_REPEAT | IF_NEAREST);
    GL_SetFilterAndRepeat(IT_SPRITE, IF_REPEAT | IF_NEAREST);

    pixel = U32_BLACK;
    GL_ForceTexture(0, TEXNUM_BLACK);
    GL_Upload32((byte *)&pixel, 1, 1, 0, IT_SPRITE, IF_REPEAT | IF_NEAREST);
    GL_SetFilterAndRepeat(IT_SPRITE, IF_REPEAT | IF_NEAREST);
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
            dst[3] = 255 * Q_clipf(f, 0, 1);
            dst += 4;
        }
    }

    GL_ForceTexture(0, TEXNUM_BEAM);
    GL_Upload32(pixels, 16, 16, 0, IT_SPRITE, IF_NONE);
    GL_SetFilterAndRepeat(IT_SPRITE, IF_NONE);
}

static void GL_InitRawTexture(void)
{
    GL_ForceTexture(0, TEXNUM_RAW);
    GL_SetFilterAndRepeat(IT_PIC, IF_NONE);
}

bool GL_InitWarpTexture(void)
{
    GL_ForceTexture(0, gl_static.warp_texture);
    qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, glr.fd.width, glr.fd.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    qglBindFramebuffer(GL_FRAMEBUFFER, gl_static.warp_framebuffer);
    qglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_static.warp_texture, 0);

    qglBindRenderbuffer(GL_RENDERBUFFER, gl_static.warp_renderbuffer);
    qglRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_STENCIL, glr.fd.width, glr.fd.height);
    qglBindRenderbuffer(GL_RENDERBUFFER, 0);

    qglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, gl_static.warp_renderbuffer);

    GLenum status = qglCheckFramebufferStatus(GL_FRAMEBUFFER);
    qglBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        if (gl_showerrors->integer)
            Com_EPrintf("%s: framebuffer status %#x\n", __func__, status);
        return false;
    }

    return true;
}

static void GL_DeleteWarpTexture(void)
{
    if (gl_static.warp_framebuffer) {
        qglDeleteFramebuffers(1, &gl_static.warp_framebuffer);
        gl_static.warp_framebuffer = 0;
    }
    if (gl_static.warp_renderbuffer) {
        qglDeleteRenderbuffers(1, &gl_static.warp_renderbuffer);
        gl_static.warp_renderbuffer = 0;
    }
    if (gl_static.warp_texture) {
        qglDeleteTextures(1, &gl_static.warp_texture);
        gl_static.warp_texture = 0;
    }
}

static void gl_partshape_changed(cvar_t *self)
{
    GL_InitParticleTexture();
}

/*
===============
GL_InitImages
===============
*/
void GL_InitImages(void)
{
    GLint integer = 0;

    gl_bilerp_chars = Cvar_Get("gl_bilerp_chars", "0", 0);
    gl_bilerp_chars->changed = gl_bilerp_chars_changed;
    gl_bilerp_pics = Cvar_Get("gl_bilerp_pics", "1", 0);
    gl_bilerp_pics->changed = gl_bilerp_pics_changed;
    gl_texturemode = Cvar_Get("gl_texturemode",
                              "GL_LINEAR_MIPMAP_LINEAR", CVAR_ARCHIVE);
    gl_texturemode->changed = gl_texturemode_changed;
    gl_texturemode->generator = gl_texturemode_g;
    gl_texturebits = Cvar_Get("gl_texturebits", "0", CVAR_FILES);
    gl_anisotropy = Cvar_Get("gl_anisotropy", "8", 0);
    gl_anisotropy->changed = gl_anisotropy_changed;
    gl_noscrap = Cvar_Get("gl_noscrap", "0", CVAR_FILES);
    gl_round_down = Cvar_Get("gl_round_down", "0", CVAR_FILES);
    gl_picmip = Cvar_Get("gl_picmip", "0", CVAR_FILES);
    gl_downsample_skins = Cvar_Get("gl_downsample_skins", "1", CVAR_FILES);
    gl_gamma_scale_pics = Cvar_Get("gl_gamma_scale_pics", "0", CVAR_FILES);
    gl_upscale_pcx = Cvar_Get("gl_upscale_pcx", "0", CVAR_FILES);
    gl_saturation = Cvar_Get("gl_saturation", "1", CVAR_FILES);
    gl_intensity = Cvar_Get("intensity", "2", 0);
    gl_invert = Cvar_Get("gl_invert", "0", CVAR_FILES);
    gl_gamma = Cvar_Get("vid_gamma", "0.8", CVAR_ARCHIVE);
    gl_partshape = Cvar_Get("gl_partshape", "0", 0);
    gl_partshape->changed = gl_partshape_changed;

    if (r_config.flags & QVF_GAMMARAMP) {
        gl_gamma->changed = gl_gamma_changed;
        gl_gamma->flags &= ~CVAR_FILES;
    } else {
        gl_gamma->flags |= CVAR_FILES;
    }

    if (gl_static.use_shaders)
        gl_intensity->flags &= ~CVAR_FILES;
    else
        gl_intensity->flags |= CVAR_FILES;

    qglGetIntegerv(GL_MAX_TEXTURE_SIZE, &integer);

    if (integer & (integer - 1)) {
        integer = Q_npot32(integer) >> 1;
    }

    max_texture_size = min(integer, MAX_TEXTURE_SIZE);

    IMG_Init();

    IMG_GetPalette();

    if (gl_upscale_pcx->integer) {
        HQ2x_Init();
    }

    GL_BuildIntensityTable();

    if (r_config.flags & QVF_GAMMARAMP) {
        gl_gamma_changed(gl_gamma);
    } else {
        GL_BuildGammaTables();
    }

    // FIXME: the name 'saturation' is misleading in this context
    colorscale = Cvar_ClampValue(gl_saturation, 0, 1);

    gl_texturemode_changed(gl_texturemode);
    gl_texturebits_changed(gl_texturebits);
    gl_anisotropy_changed(gl_anisotropy);
    gl_bilerp_chars_changed(gl_bilerp_chars);
    gl_bilerp_pics_changed(gl_bilerp_pics);

    qglGenTextures(NUM_TEXNUMS, gl_static.texnums);
    qglGenTextures(LM_MAX_LIGHTMAPS, lm.texnums);

    if (gl_static.use_shaders) {
        qglGenTextures(1, &gl_static.warp_texture);
        qglGenRenderbuffers(1, &gl_static.warp_renderbuffer);
        qglGenFramebuffers(1, &gl_static.warp_framebuffer);
    }

    Scrap_Init();

    GL_InitDefaultTexture();
    GL_InitParticleTexture();
    GL_InitWhiteImage();
    GL_InitBeamTexture();
    GL_InitRawTexture();

#if USE_DEBUG
    r_charset = R_RegisterFont("conchars");
#endif

    GL_ShowErrors(__func__);
}

/*
===============
GL_ShutdownImages
===============
*/
void GL_ShutdownImages(void)
{
    gl_bilerp_chars->changed = NULL;
    gl_bilerp_pics->changed = NULL;
    gl_texturemode->changed = NULL;
    gl_texturemode->generator = NULL;
    gl_anisotropy->changed = NULL;
    gl_gamma->changed = NULL;
    gl_partshape->changed = NULL;

    // delete auto textures
    qglDeleteTextures(NUM_TEXNUMS, gl_static.texnums);
    qglDeleteTextures(LM_MAX_LIGHTMAPS, lm.texnums);

    GL_DeleteWarpTexture();

#if USE_DEBUG
    r_charset = 0;
#endif

    IMG_FreeAll();
    IMG_Shutdown();

    Scrap_Shutdown();
}

