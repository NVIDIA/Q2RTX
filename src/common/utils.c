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

#include "shared/shared.h"
#include "common/utils.h"

/*
==============================================================================

                        WILDCARD COMPARE

==============================================================================
*/

static qboolean match_raw(int c1, int c2, qboolean ignorecase)
{
    if (c1 != c2) {
        if (!ignorecase) {
            return qfalse;
        }
#ifdef _WIN32
        // ugly hack for file listing
        c1 = c1 == '\\' ? '/' : Q_tolower(c1);
        c2 = c2 == '\\' ? '/' : Q_tolower(c2);
#else
        c1 = Q_tolower(c1);
        c2 = Q_tolower(c2);
#endif
        if (c1 != c2) {
            return qfalse;
        }
    }

    return qtrue;
}

static qboolean match_char(int c1, int c2, qboolean ignorecase)
{
    if (c1 == '?') {
        return !!c2; // match any char except NUL
    }

    return match_raw(c1, c2, ignorecase);
}

static qboolean match_part(const char *filter, const char *string,
                           size_t len, qboolean ignorecase)
{
    qboolean match;

    do {
        // skip over escape character
        if (*filter == '\\') {
            filter++;
            match = match_raw(*filter, *string, ignorecase);
        } else {
            match = match_char(*filter, *string, ignorecase);
        }

        if (!match) {
            return qfalse;
        }

        filter++;
        string++;
    } while (--len);

    return qtrue;
}

// match the longest possible part
static const char *match_filter(const char *filter, const char *string,
                                size_t len, qboolean ignorecase)
{
    const char *ret = NULL;
    size_t remaining = strlen(string);

    while (remaining >= len) {
        if (match_part(filter, string, len, ignorecase)) {
            string += len;
            remaining -= len;
            ret = string;
            continue;
        }
        string++;
        remaining--;
    }

    return ret;
}

/*
=================
Com_WildCmpEx

Wildcard compare. Returns true if string matches the pattern, false otherwise.

- 'term' is handled as an additional filter terminator (besides NUL).
- '*' matches any substring, including the empty string, but prefers longest
possible substrings.
- '?' matches any single character except NUL.
- '\\' can be used to escape any character, including itself. any special
characters lose their meaning in this case.

=================
*/
qboolean Com_WildCmpEx(const char *filter, const char *string,
                       int term, qboolean ignorecase)
{
    const char *sub;
    size_t len;
    qboolean match;

    while (*filter && *filter != term) {
        if (*filter == '*') {
            // skip consecutive wildcards
            do {
                filter++;
            } while (*filter == '*');

            // scan out filter part to match
            for (sub = filter, len = 0; *filter && *filter != term && *filter != '*'; filter++, len++) {
                // skip over escape character
                if (*filter == '\\') {
                    filter++;
                    if (!*filter) {
                        break;
                    }
                }
            }

            // wildcard at the end matches everything
            if (!len) {
                return qtrue;
            }

            string = match_filter(sub, string, len, ignorecase);
            if (!string) {
                return qfalse;
            }
        } else {
            // skip over escape character
            if (*filter == '\\') {
                filter++;
                if (!*filter) {
                    break;
                }
                match = match_raw(*filter, *string, ignorecase);
            } else {
                match = match_char(*filter, *string, ignorecase);
            }

            // match single character
            if (!match) {
                return qfalse;
            }

            filter++;
            string++;
        }
    }

    // match NUL at the end
    return !*string;
}

/*
==============================================================================

                        MISC

==============================================================================
*/

const char *const colorNames[10] = {
    "black", "red", "green", "yellow",
    "blue", "cyan", "magenta", "white",
    "alt", "none"
};

/*
================
Com_ParseColor

Parses color name or index up to the maximum allowed index.
Returns COLOR_NONE in case of error.
================
*/
color_index_t Com_ParseColor(const char *s, color_index_t last)
{
    color_index_t i;

    if (COM_IsUint(s)) {
        i = strtoul(s, NULL, 10);
        return i > last ? COLOR_NONE : i;
    }

    for (i = 0; i <= last; i++) {
        if (!strcmp(colorNames[i], s)) {
            return i;
        }
    }

    return COLOR_NONE;
}

#if (USE_REF == REF_GL) || (USE_REF == REF_GLPT)
/*
================
Com_ParseExtensionString

Helper function to parse an OpenGL-style extension string.
================
*/
unsigned Com_ParseExtensionString(const char *s, const char *const extnames[])
{
    unsigned mask;
    const char *p;
    size_t l1, l2;
    int i;

    if (!s) {
        return 0;
    }

    mask = 0;
    while (*s) {
        p = Q_strchrnul(s, ' ');
        l1 = p - s;
        for (i = 0; extnames[i]; i++) {
            l2 = strlen(extnames[i]);
            if (l1 == l2 && !memcmp(s, extnames[i], l1)) {
                mask |= 1 << i;
                break;
            }
        }
        if (!*p) {
            break;
        }
        s = p + 1;
    }

    return mask;
}
#endif

/*
================
Com_PlayerToEntityState

Restores entity origin and angles from player state
================
*/
void Com_PlayerToEntityState(const player_state_t *ps, entity_state_t *es)
{
    vec_t pitch;

    VectorScale(ps->pmove.origin, 0.125f, es->origin);

    pitch = ps->viewangles[PITCH];
    if (pitch > 180) {
        pitch -= 360;
    }
    es->angles[PITCH] = pitch / 3;
    es->angles[YAW] = ps->viewangles[YAW];
    es->angles[ROLL] = 0;
}

#if USE_CLIENT || USE_MVD_CLIENT
/*
================
Com_ParseTimespec

Parses time/frame specification for seeking in demos.
Does not check for integer overflow...
================
*/
qboolean Com_ParseTimespec(const char *s, int *frames)
{
    unsigned long c1, c2, c3;
    char *p;

    c1 = strtoul(s, &p, 10);
    if (!*p) {
        *frames = c1 * 10; // sec
        return qtrue;
    }

    if (*p == '.') {
        c2 = strtoul(p + 1, &p, 10);
        if (*p)
            return qfalse;
        *frames = c1 * 10 + c2; // sec.frac
        return qtrue;
    }

    if (*p == ':') {
        c2 = strtoul(p + 1, &p, 10);
        if (!*p) {
            *frames = c1 * 600 + c2 * 10; // min:sec
            return qtrue;
        }

        if (*p == '.') {
            c3 = strtoul(p + 1, &p, 10);
            if (*p)
                return qfalse;
            *frames = c1 * 600 + c2 * 10 + c3; // min:sec.frac
            return qtrue;
        }

        return qfalse;
    }

    return qfalse;
}
#endif

/*
================
Com_HashString
================
*/
unsigned Com_HashString(const char *s, unsigned size)
{
    unsigned hash, c;

    hash = 0;
    while (*s) {
        c = *s++;
        hash = 127 * hash + c;
    }

    hash = (hash >> 20) ^(hash >> 10) ^ hash;
    return hash & (size - 1);
}

/*
================
Com_HashStringLen

A case-insensitive version of Com_HashString that hashes up to 'len'
characters.
================
*/
unsigned Com_HashStringLen(const char *s, size_t len, unsigned size)
{
    unsigned hash, c;

    hash = 0;
    while (*s && len--) {
        c = Q_tolower(*s++);
        hash = 127 * hash + c;
    }

    hash = (hash >> 20) ^(hash >> 10) ^ hash;
    return hash & (size - 1);
}

/*
===============
Com_PageInMemory

===============
*/
int    paged_total;

void Com_PageInMemory(void *buffer, size_t size)
{
    int        i;

    for (i = size - 1; i > 0; i -= 4096)
        paged_total += ((byte *)buffer)[i];
}

size_t Com_FormatTime(char *buffer, size_t size, time_t t)
{
    int     sec, min, hour, day;

    min = t / 60; sec = t % 60;
    hour = min / 60; min %= 60;
    day = hour / 24; hour %= 24;

    if (day) {
        return Q_scnprintf(buffer, size, "%d+%d:%02d.%02d", day, hour, min, sec);
    }
    if (hour) {
        return Q_scnprintf(buffer, size, "%d:%02d.%02d", hour, min, sec);
    }
    return Q_scnprintf(buffer, size, "%02d.%02d", min, sec);
}

size_t Com_FormatTimeLong(char *buffer, size_t size, time_t t)
{
    int     sec, min, hour, day;
    size_t  len;

    if (!t) {
        return Q_scnprintf(buffer, size, "0 secs");
    }

    min = t / 60; sec = t % 60;
    hour = min / 60; min %= 60;
    day = hour / 24; hour %= 24;

    len = 0;

    if (day) {
        len += Q_scnprintf(buffer + len, size - len,
                           "%d day%s%s", day, day == 1 ? "" : "s", (hour || min || sec) ? ", " : "");
    }
    if (hour) {
        len += Q_scnprintf(buffer + len, size - len,
                           "%d hour%s%s", hour, hour == 1 ? "" : "s", (min || sec) ? ", " : "");
    }
    if (min) {
        len += Q_scnprintf(buffer + len, size - len,
                           "%d min%s%s", min, min == 1 ? "" : "s", sec ? ", " : "");
    }
    if (sec) {
        len += Q_scnprintf(buffer + len, size - len,
                           "%d sec%s", sec, sec == 1 ? "" : "s");
    }

    return len;
}

size_t Com_TimeDiff(char *buffer, size_t size, time_t *p, time_t now)
{
    time_t diff;

    if (*p > now) {
        *p = now;
    }
    diff = now - *p;
    return Com_FormatTime(buffer, size, diff);
}

size_t Com_TimeDiffLong(char *buffer, size_t size, time_t *p, time_t now)
{
    time_t diff;

    if (*p > now) {
        *p = now;
    }
    diff = now - *p;
    return Com_FormatTimeLong(buffer, size, diff);
}

size_t Com_FormatSize(char *dest, size_t destsize, off_t bytes)
{
    if (bytes >= 10000000) {
        return Q_scnprintf(dest, destsize, "%dM", (int)(bytes / 1000000));
    }
    if (bytes >= 1000000) {
        return Q_scnprintf(dest, destsize, "%.1fM", (float)bytes / 1000000);
    }
    if (bytes >= 1000) {
        return Q_scnprintf(dest, destsize, "%dK", (int)(bytes / 1000));
    }
    if (bytes >= 0) {
        return Q_scnprintf(dest, destsize, "%d", (int)bytes);
    }
    return Q_scnprintf(dest, destsize, "???");
}

size_t Com_FormatSizeLong(char *dest, size_t destsize, off_t bytes)
{
    if (bytes >= 10000000) {
        return Q_scnprintf(dest, destsize, "%d MB", (int)(bytes / 1000000));
    }
    if (bytes >= 1000000) {
        return Q_scnprintf(dest, destsize, "%.1f MB", (float)bytes / 1000000);
    }
    if (bytes >= 1000) {
        return Q_scnprintf(dest, destsize, "%d kB", (int)(bytes / 1000));
    }
    if (bytes >= 0) {
        return Q_scnprintf(dest, destsize, "%d byte%s",
                           (int)bytes, bytes == 1 ? "" : "s");
    }
    return Q_scnprintf(dest, destsize, "unknown size");
}


