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
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>

static long pagesize;

void Hunk_Init(void)
{
    pagesize = sysconf(_SC_PAGESIZE);
    Q_assert(pagesize && !(pagesize & (pagesize - 1)));
}

void Hunk_Begin(memhunk_t *hunk, size_t maxsize)
{
    void *buf;

    Q_assert(maxsize <= SIZE_MAX - (pagesize - 1));

    // reserve a huge chunk of memory, but don't commit any yet
    hunk->cursize = 0;
    hunk->maxsize = ALIGN(maxsize, pagesize);
    buf = mmap(NULL, hunk->maxsize, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANON, -1, 0);
    if (buf == MAP_FAILED)
        Com_Error(ERR_FATAL, "%s: couldn't reserve %zu bytes: %s",
                  __func__, hunk->maxsize, strerror(errno));
    hunk->base = buf;
    hunk->mapped = hunk->maxsize;
}

void *Hunk_TryAlloc(memhunk_t *hunk, size_t size)
{
    void *buf;

    Q_assert(size <= SIZE_MAX - (HUNK_ALIGN - 1));
    Q_assert(hunk->cursize <= hunk->maxsize);

    // round to cacheline
    size = ALIGN(size, HUNK_ALIGN);
    if (size > hunk->maxsize - hunk->cursize)
        return NULL;

    buf = (byte *)hunk->base + hunk->cursize;
    hunk->cursize += size;
    return buf;
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
    size_t newsize;

    Q_assert(hunk->cursize <= hunk->maxsize);
    newsize = ALIGN(hunk->cursize, pagesize);

    if (newsize < hunk->maxsize) {
#if defined(__linux__)
        void *buf = mremap(hunk->base, hunk->maxsize, newsize, 0);
#elif defined(__NetBSD__)
        void *buf = mremap(hunk->base, hunk->maxsize, NULL, newsize, 0);
#else
        void *unmap_base = (byte *)hunk->base + newsize;
        size_t unmap_len = hunk->maxsize - newsize;
        void *buf = munmap(unmap_base, unmap_len) + (byte *)hunk->base;
#endif
        if (buf != hunk->base)
            Com_Error(ERR_FATAL, "%s: couldn't remap virtual block: %s",
                      __func__, strerror(errno));
    }

    hunk->mapped = newsize;
}

void Hunk_Free(memhunk_t *hunk)
{
    if (hunk->base && munmap(hunk->base, hunk->mapped))
        Com_Error(ERR_FATAL, "%s: munmap failed: %s",
                  __func__, strerror(errno));

    memset(hunk, 0, sizeof(*hunk));
}

