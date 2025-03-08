/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2024-2025 Frank Richter
Copyright (C) 2024-2025 Andrey Nazarov
Copyright (C) 2024-2025 Jonathan "Paril" Barkley

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
#include "shared/debug.h"
#include "common/cvar.h"
#include "common/math.h"
#include "common/prompt.h"
#include "debug_fonts/futural.h"
#include "debug_fonts/timesr.h"

typedef struct debug_font_s {
    // Number of glyphs
    int count;
    // Font height
    char height;
    // Widths of the glyphs
    const char* width;
    // Real widths of the glyphs (calculated from data)
    const char* realwidth;
    // Number of chars in each glyph
    const int* size;
    // Pointers to glyph data
    const char **glyph_data;
} debug_font_t;

#define DEBUG_FONT(NAME)        \
    {                           \
        #NAME,                  \
        {                       \
            NAME##_count,       \
            NAME##_height,      \
            NAME##_width,       \
            NAME##_realwidth,   \
            NAME##_size,        \
            NAME                \
        }                       \
    }

static const struct {
    const char *name;
    debug_font_t font;
} debug_fonts[] = {
    DEBUG_FONT(futural),
    DEBUG_FONT(timesr),
};

#undef DEBUG_FONT

static const debug_font_t *dbg_font;

static cvar_t *r_debug_font;

#define GL_DRAWLINEV(s, e) \
    R_AddDebugLine(s, e, color, time, depth_test)

void R_AddDebugText_Lines(const vec3_t vieworg, const vec3_t origin, const vec3_t angles, const char *text, float size, uint32_t color, uint32_t time, bool depth_test)
{
    int total_lines = 1;
    float scale = (1.0f / dbg_font->height) * (size * 32);

    int l = strlen(text);

    for (int i = 0; i < l; i++) {
        if (text[i] == '\n')
            total_lines++;
    }

    if (!angles)
    {
        vec3_t d;
        VectorSubtract(origin, vieworg, d);
        VectorNormalize(d);
        d[2] = 0.0f;
        vectoangles2(d, d);
        angles = (const vec_t *) &d;
    }

    vec3_t right, up;
    AngleVectors(angles, NULL, right, up);

    float y_offset = -((dbg_font->height * scale) * 0.5f) * total_lines;

    const char *c = text;
    for (int line = 0; line < total_lines; line++) {
        const char *c_end = c;
        float width = 0;

        for (; *c_end && *c_end != '\n'; c_end++) {
            width += dbg_font->width[*c_end - ' '] * scale;
        }

        float x_offset = (width * 0.5f);

        for (const char *rc = c; rc != c_end; rc++) {
            char c = *rc - ' ';
            const float char_width = dbg_font->width[(int)c] * scale;
            const int char_size = dbg_font->size[(int)c];
            const char *char_data = dbg_font->glyph_data[(int)c];

            for (int i = 0; i < char_size; i += 4) {
                vec3_t s;
                float r = -char_data[i] * scale + x_offset;
                float u = -(char_data[i + 1] * scale + y_offset);
                VectorMA(origin, -r, right, s);
                VectorMA(s, u, up, s);
                vec3_t e;
                r = -char_data[i + 2] * scale + x_offset;
                u = -(char_data[i + 3] * scale + y_offset);
                VectorMA(origin, -r, right, e);
                VectorMA(e, u, up, e);
                GL_DRAWLINEV(s, e);
            }

            x_offset -= char_width;
        }

        y_offset += dbg_font->height * scale;

        c = c_end + 1;
    }
}

static void r_debug_font_changed(cvar_t* cvar)
{
    int font_idx = -1;
    for (int i = 0; i < q_countof(debug_fonts); i++) {
        if (Q_strcasecmp(cvar->string, debug_fonts[i].name) == 0) {
            font_idx = i;
            break;
        }
    }
    if (font_idx < 0) {
        Com_WPrintf("unknown debug font: %s\n", cvar->string);
        font_idx = 0;
    }
    dbg_font = &debug_fonts[font_idx].font;
}

static void r_debug_font_generator(struct genctx_s *gen)
{
    for (int i = 0; i < q_countof(debug_fonts); i++) {
        Prompt_AddMatch(gen, debug_fonts[i].name);
    }
}

void R_InitDebugText(void)
{
    r_debug_font = Cvar_Get("r_debug_font", debug_fonts[0].name, 0);
    r_debug_font->changed = r_debug_font_changed;
    r_debug_font->generator = r_debug_font_generator;
    r_debug_font_changed(r_debug_font);
}
