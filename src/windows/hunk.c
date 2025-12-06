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
#include "system/hunk.h"
#include <windows.h>

static DWORD pagesize;

void Hunk_Init(void)
{
    SYSTEM_INFO si = { 0 };

    GetSystemInfo(&si);
    pagesize = si.dwPageSize;
    Q_assert(pagesize && !(pagesize & (pagesize - 1)));
}

void Hunk_Begin(memhunk_t *hunk, size_t maxsize)
{
    Q_assert(maxsize <= SIZE_MAX - (pagesize - 1));

    // reserve a huge chunk of memory, but don't commit any yet
    hunk->cursize = 0;
    hunk->maxsize = ALIGN(maxsize, (size_t)pagesize);
    hunk->base = VirtualAlloc(NULL, hunk->maxsize, MEM_RESERVE, PAGE_NOACCESS);
    if (!hunk->base)
        Com_Error(ERR_FATAL,
                  "VirtualAlloc reserve %zu bytes failed with error %lu",
                  hunk->maxsize, GetLastError());
}

void *Hunk_TryAlloc(memhunk_t *hunk, size_t size)
{
    void *buf;

    Q_assert(size <= SIZE_MAX - 63);
    Q_assert(hunk->cursize <= hunk->maxsize);

    // round to cacheline
    size = ALIGN(size, 64);
    if (size > hunk->maxsize - hunk->cursize)
        return NULL;

    hunk->cursize += size;

    // commit pages as needed
    buf = VirtualAlloc(hunk->base, hunk->cursize, MEM_COMMIT, PAGE_READWRITE);
    if (!buf)
        Com_Error(ERR_FATAL,
                  "VirtualAlloc commit %zu bytes failed with error %lu",
                  hunk->cursize, GetLastError());

    return (byte *)hunk->base + hunk->cursize - size;
}

void *Hunk_Alloc(memhunk_t *hunk, size_t size)
{
    void *buf = Hunk_TryAlloc(hunk, size);
    if (!buf)
        Com_Error(ERR_FATAL, "%s: couldn't allocate %zu bytes", __func__, size);
    return buf;
}

void Hunk_End(memhunk_t *hunk)
{
    Q_assert(hunk->cursize <= hunk->maxsize);

    // for statistics
    hunk->mapped = ALIGN(hunk->cursize, (size_t)pagesize);
}

void Hunk_Free(memhunk_t *hunk)
{
    if (hunk->base && !VirtualFree(hunk->base, 0, MEM_RELEASE))
        Com_Error(ERR_FATAL, "VirtualFree failed with error %lu", GetLastError());

    memset(hunk, 0, sizeof(*hunk));
}
