/*
Copyright (C) 2003-2012 Andrey Nazarov

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

#pragma once

typedef enum {
    COLOR_BLACK,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_YELLOW,
    COLOR_BLUE,
    COLOR_CYAN,
    COLOR_MAGENTA,
    COLOR_WHITE,

    COLOR_ALT,
    COLOR_NONE,

    COLOR_COUNT
} color_index_t;

extern const char *const colorNames[COLOR_COUNT];

bool Com_WildCmpEx(const char *filter, const char *string, int term, bool ignorecase);
#define Com_WildCmp(filter, string)  Com_WildCmpEx(filter, string, 0, false)

#if USE_CLIENT || USE_MVD_CLIENT
bool Com_ParseTimespec(const char *s, int *frames);
#endif

void Com_PlayerToEntityState(const player_state_t *ps, entity_state_t *es);

bool Com_ParseMapName(char *out, const char *in, size_t size);

unsigned Com_HashString(const char *s, unsigned size);
unsigned Com_HashStringLen(const char *s, size_t len, unsigned size);

size_t Com_FormatLocalTime(char *buffer, size_t size, const char *fmt);

size_t Com_FormatTime(char *buffer, size_t size, time_t t);
size_t Com_FormatTimeLong(char *buffer, size_t size, time_t t);
size_t Com_TimeDiff(char *buffer, size_t size, time_t *p, time_t now);
size_t Com_TimeDiffLong(char *buffer, size_t size, time_t *p, time_t now);

size_t Com_FormatSize(char *dest, size_t destsize, int64_t bytes);
size_t Com_FormatSizeLong(char *dest, size_t destsize, int64_t bytes);

void Com_PageInMemory(void *buffer, size_t size);

color_index_t Com_ParseColor(const char *s);

extern const char com_hexchars[16];

size_t Com_EscapeString(char *dst, const char *src, size_t size);

char *Com_MakePrintable(const char *s);

// Some mods actually exploit CS_STATUSBAR to take space up to CS_AIRACCEL
static inline size_t CS_SIZE(const cs_remap_t *csr, int cs)
{
    if (cs >= CS_STATUSBAR && cs < csr->airaccel)
        return MAX_QPATH * (csr->airaccel - cs);

    if (cs >= csr->general && cs < csr->end)
        return MAX_QPATH * (csr->end - cs);

    return MAX_QPATH;
}
