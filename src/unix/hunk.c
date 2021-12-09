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

void Hunk_Begin(memhunk_t *hunk, size_t maxsize)
{
    void *buf;

    if (maxsize > SIZE_MAX - 4095)
        Com_Error(ERR_FATAL, "%s: size > SIZE_MAX", __func__);

    // reserve a huge chunk of memory, but don't commit any yet
    hunk->cursize = 0;
    hunk->maxsize = ALIGN(maxsize, 4096);
    buf = mmap(NULL, hunk->maxsize, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANON, -1, 0);
    if (buf == NULL || buf == (void *)-1)
        Com_Error(ERR_FATAL, "%s: unable to reserve %"PRIz" bytes: %s",
                  __func__, hunk->maxsize, strerror(errno));
    hunk->base = buf;
    hunk->mapped = hunk->maxsize;
}

void *Hunk_Alloc(memhunk_t *hunk, size_t size)
{
    void *buf;

    if (size > SIZE_MAX - 63)
        Com_Error(ERR_FATAL, "%s: size > SIZE_MAX", __func__);

    // round to cacheline
    size = ALIGN(size, 64);

    if (hunk->cursize > hunk->maxsize)
        Com_Error(ERR_FATAL, "%s: cursize > maxsize", __func__);

    if (size > hunk->maxsize - hunk->cursize)
        Com_Error(ERR_FATAL, "%s: couldn't allocate %"PRIz" bytes", __func__, size);

    buf = (byte *)hunk->base + hunk->cursize;
    hunk->cursize += size;
    return buf;
}

void Hunk_End(memhunk_t *hunk)
{
    size_t newsize;

    if (hunk->cursize > hunk->maxsize)
        Com_Error(ERR_FATAL, "%s: cursize > maxsize", __func__);

    newsize = ALIGN(hunk->cursize, 4096);

    if (newsize < hunk->maxsize) {
#if (defined __linux__) && (defined _GNU_SOURCE)
        void *buf = mremap(hunk->base, hunk->maxsize, newsize, 0);
#else
        void *unmap_base = (byte *)hunk->base + newsize;
        size_t unmap_len = hunk->maxsize - newsize;
        void *buf = munmap(unmap_base, unmap_len) + (byte *)hunk->base;
#endif
        if (buf != hunk->base)
            Com_Error(ERR_FATAL, "%s: could not remap virtual block: %s",
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

