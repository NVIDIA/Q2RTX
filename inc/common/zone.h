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

#ifndef ZONE_H
#define ZONE_H

#define Z_Malloc(size)          Z_TagMalloc(size, TAG_GENERAL)
#define Z_Mallocz(size)         Z_TagMallocz(size, TAG_GENERAL)
#define Z_Reserve(size)         Z_TagReserve(size, TAG_GENERAL)
#define Z_CopyString(string)    Z_TagCopyString(string, TAG_GENERAL)
#define Z_CopyStruct(ptr)       memcpy(Z_Malloc(sizeof(*ptr)), ptr, sizeof(*ptr))

// memory tags to allow dynamic memory to be cleaned up
// game DLL has separate tag namespace starting at TAG_MAX
typedef enum {
    TAG_FREE,       // should have never been set
    TAG_STATIC,

    TAG_GENERAL,
    TAG_CMD,
    TAG_CVAR,
    TAG_FILESYSTEM,
    TAG_RENDERER,
    TAG_UI,
    TAG_SERVER,
    TAG_MVD,
    TAG_SOUND,
    TAG_CMODEL,

    TAG_MAX
} memtag_t;

void    Z_Init(void);
void    Z_Free(void *ptr);
void    *Z_Realloc(void *ptr, size_t size);
void    *Z_TagMalloc(size_t size, memtag_t tag) q_malloc;
void    *Z_TagMallocz(size_t size, memtag_t tag) q_malloc;
char    *Z_TagCopyString(const char *in, memtag_t tag) q_malloc;
void    Z_FreeTags(memtag_t tag);
void    Z_LeakTest(memtag_t tag);
void    Z_Stats_f(void);

void    Z_TagReserve(size_t size, memtag_t tag);
void    *Z_ReservedAlloc(size_t size) q_malloc;
void    *Z_ReservedAllocz(size_t size) q_malloc;
char    *Z_ReservedCopyString(const char *in) q_malloc;

// may return pointer to static memory
char    *Z_CvarCopyString(const char *in);

#endif // ZONE_H
