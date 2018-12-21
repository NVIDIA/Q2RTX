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

#ifndef UTILS_H
#define UTILS_H

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
    COLOR_NONE
} color_index_t;

extern const char *const colorNames[10];

qboolean Com_WildCmpEx(const char *filter, const char *string, int term, qboolean ignorecase);
#define Com_WildCmp(filter, string)  Com_WildCmpEx(filter, string, 0, qfalse)

#if USE_CLIENT || USE_MVD_CLIENT
qboolean Com_ParseTimespec(const char *s, int *frames);
#endif

void Com_PlayerToEntityState(const player_state_t *ps, entity_state_t *es);

unsigned Com_HashString(const char *s, unsigned size);
unsigned Com_HashStringLen(const char *s, size_t len, unsigned size);

size_t Com_FormatTime(char *buffer, size_t size, time_t t);
size_t Com_FormatTimeLong(char *buffer, size_t size, time_t t);
size_t Com_TimeDiff(char *buffer, size_t size, time_t *p, time_t now);
size_t Com_TimeDiffLong(char *buffer, size_t size, time_t *p, time_t now);

size_t Com_FormatSize(char *dest, size_t destsize, off_t bytes);
size_t Com_FormatSizeLong(char *dest, size_t destsize, off_t bytes);

void Com_PageInMemory(void *buffer, size_t size);

color_index_t Com_ParseColor(const char *s, color_index_t last);

#if (USE_REF == REF_GL) || (USE_REF == REF_GLPT)
unsigned Com_ParseExtensionString(const char *s, const char *const extnames[]);
#endif

#endif // UTILS_H
