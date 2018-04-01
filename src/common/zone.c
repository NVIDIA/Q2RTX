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
#include "common/common.h"
#include "common/zone.h"

#define Z_MAGIC     0x1d0d
#define Z_TAIL      0x5b7b

#define Z_TAIL_F(z) \
    *(uint16_t *)((byte *)(z) + (z)->size - sizeof(uint16_t))

#define Z_FOR_EACH(z) \
    for ((z) = z_chain.next; (z) != &z_chain; (z) = (z)->next)

#define Z_FOR_EACH_SAFE(z, n) \
    for ((z) = z_chain.next; (n) = (z)->next, (z) != &z_chain; (z) = (n))

typedef struct zhead_s {
    uint16_t    magic;
    uint16_t    tag;            // for group free
    size_t      size;
#ifdef _DEBUG
    void        *addr;
    time_t      time;
#endif
    struct zhead_s  *prev, *next;
} zhead_t;

// number of overhead bytes
#define Z_EXTRA (sizeof(zhead_t) + sizeof(uint16_t))

static zhead_t      z_chain;

typedef struct {
    zhead_t     z;
    char        data[2];
    uint16_t    tail;
} zstatic_t;

static const zstatic_t z_static[] = {
#define Z_STATIC(x) \
    { { Z_MAGIC, TAG_STATIC, q_offsetof(zstatic_t, tail) + sizeof(uint16_t) }, x, Z_TAIL }

    Z_STATIC("0"),
    Z_STATIC("1"),
    Z_STATIC("2"),
    Z_STATIC("3"),
    Z_STATIC("4"),
    Z_STATIC("5"),
    Z_STATIC("6"),
    Z_STATIC("7"),
    Z_STATIC("8"),
    Z_STATIC("9"),
    Z_STATIC("")

#undef Z_STATIC
};

typedef struct {
    size_t count;
    size_t bytes;
} zstats_t;

static zstats_t z_stats[TAG_MAX];

static const char z_tagnames[TAG_MAX][8] = {
    "game",
    "static",
    "generic",
    "cmd",
    "cvar",
    "fs",
    "refresh",
    "ui",
    "server",
    "mvd",
    "sound",
    "cmodel"
};

static inline void Z_Validate(zhead_t *z, const char *func)
{
    if (z->magic != Z_MAGIC) {
        Com_Error(ERR_FATAL, "%s: bad magic", func);
    }
    if (Z_TAIL_F(z) != Z_TAIL) {
        Com_Error(ERR_FATAL, "%s: bad tail", func);
    }
    if (z->tag == TAG_FREE) {
        Com_Error(ERR_FATAL, "%s: bad tag", func);
    }
}

void Z_Check(void)
{
    zhead_t *z;

    Z_FOR_EACH(z) {
        Z_Validate(z, __func__);
    }
}

void Z_LeakTest(memtag_t tag)
{
    zhead_t *z;
    size_t numLeaks = 0, numBytes = 0;

    Z_FOR_EACH(z) {
        Z_Validate(z, __func__);
        if (z->tag == tag) {
            numLeaks++;
            numBytes += z->size;
        }
    }

    if (numLeaks) {
        Com_WPrintf("************* Z_LeakTest *************\n"
                    "%s leaked %"PRIz" bytes of memory (%"PRIz" object%s)\n"
                    "**************************************\n",
                    z_tagnames[tag < TAG_MAX ? tag : TAG_FREE],
                    numBytes, numLeaks, numLeaks == 1 ? "" : "s");
    }
}

/*
========================
Z_Free
========================
*/
void Z_Free(void *ptr)
{
    zhead_t *z;
    zstats_t *s;

    if (!ptr) {
        return;
    }

    z = (zhead_t *)ptr - 1;

    Z_Validate(z, __func__);

    s = &z_stats[z->tag < TAG_MAX ? z->tag : TAG_FREE];
    s->count--;
    s->bytes -= z->size;

    if (z->tag != TAG_STATIC) {
        z->prev->next = z->next;
        z->next->prev = z->prev;
        z->magic = 0xdead;
        z->tag = TAG_FREE;
        free(z);
    }
}

/*
========================
Z_Realloc
========================
*/
void *Z_Realloc(void *ptr, size_t size)
{
    zhead_t *z;
    zstats_t *s;

    if (!ptr) {
        return Z_Malloc(size);
    }

    if (!size) {
        Z_Free(ptr);
        return NULL;
    }

    z = (zhead_t *)ptr - 1;

    Z_Validate(z, __func__);

    if (z->tag == TAG_STATIC) {
        Com_Error(ERR_FATAL, "%s: couldn't realloc static memory", __func__);
    }

    s = &z_stats[z->tag < TAG_MAX ? z->tag : TAG_FREE];
    s->bytes -= z->size;

    if (size > SIZE_MAX - Z_EXTRA - 3) {
        Com_Error(ERR_FATAL, "%s: bad size", __func__);
    }

    size = (size + Z_EXTRA + 3) & ~3;
    z = realloc(z, size);
    if (!z) {
        Com_Error(ERR_FATAL, "%s: couldn't realloc %"PRIz" bytes", __func__, size);
    }

    z->size = size;
    z->prev->next = z;
    z->next->prev = z;

    s->bytes += size;

    Z_TAIL_F(z) = Z_TAIL;

    return z + 1;
}

/*
========================
Z_Stats_f
========================
*/
void Z_Stats_f(void)
{
    size_t bytes = 0, count = 0;
    zstats_t *s;
    int i;

    Com_Printf("    bytes blocks name\n"
               "--------- ------ -------\n");

    for (i = 0, s = z_stats; i < TAG_MAX; i++, s++) {
        if (!s->count) {
            continue;
        }
        Com_Printf("%9"PRIz" %6"PRIz" %s\n", s->bytes, s->count, z_tagnames[i]);
        bytes += s->bytes;
        count += s->count;
    }

    Com_Printf("--------- ------ -------\n"
               "%9"PRIz" %6"PRIz" total\n",
               bytes, count);
}

/*
========================
Z_FreeTags
========================
*/
void Z_FreeTags(memtag_t tag)
{
    zhead_t *z, *n;

    Z_FOR_EACH_SAFE(z, n) {
        Z_Validate(z, __func__);
        if (z->tag == tag) {
            Z_Free(z + 1);
        }
    }
}

/*
========================
Z_TagMalloc
========================
*/
void *Z_TagMalloc(size_t size, memtag_t tag)
{
    zhead_t *z;
    zstats_t *s;

    if (!size) {
        return NULL;
    }

    if (tag == TAG_FREE) {
        Com_Error(ERR_FATAL, "%s: bad tag", __func__);
    }

    if (size > SIZE_MAX - Z_EXTRA - 3) {
        Com_Error(ERR_FATAL, "%s: bad size", __func__);
    }

    size = (size + Z_EXTRA + 3) & ~3;
    z = malloc(size);
    if (!z) {
        Com_Error(ERR_FATAL, "%s: couldn't allocate %"PRIz" bytes", __func__, size);
    }
    z->magic = Z_MAGIC;
    z->tag = tag;
    z->size = size;

#ifdef _DEBUG
#if (defined __GNUC__)
    z->addr = __builtin_return_address(0);
#elif (defined _MSC_VER)
    z->addr = _ReturnAddress();
#else
    z->addr = NULL;
#endif
    z->time = time(NULL);
#endif

    z->next = z_chain.next;
    z->prev = &z_chain;
    z_chain.next->prev = z;
    z_chain.next = z;

    if (z_perturb && z_perturb->integer) {
        memset(z + 1, z_perturb->integer, size - Z_EXTRA);
    }

    Z_TAIL_F(z) = Z_TAIL;

    s = &z_stats[tag < TAG_MAX ? tag : TAG_FREE];
    s->count++;
    s->bytes += size;

    return z + 1;
}

void *Z_TagMallocz(size_t size, memtag_t tag)
{
    if (!size) {
        return NULL;
    }
    return memset(Z_TagMalloc(size, tag), 0, size);
}

static byte     *z_reserved_data;
static size_t   z_reserved_inuse;
static size_t   z_reserved_total;

void Z_TagReserve(size_t size, memtag_t tag)
{
    z_reserved_data = Z_TagMalloc(size, tag);
    z_reserved_total = size;
    z_reserved_inuse = 0;
}

void *Z_ReservedAlloc(size_t size)
{
    void *ptr;

    if (!size) {
        return NULL;
    }

    if (size > z_reserved_total - z_reserved_inuse) {
        Com_Error(ERR_FATAL, "%s: couldn't allocate %"PRIz" bytes", __func__, size);
    }

    ptr = z_reserved_data + z_reserved_inuse;
    z_reserved_inuse += size;

    return ptr;
}

void *Z_ReservedAllocz(size_t size)
{
    if (!size) {
        return NULL;
    }
    return memset(Z_ReservedAlloc(size), 0, size);
}

char *Z_ReservedCopyString(const char *in)
{
    size_t len;

    if (!in) {
        return NULL;
    }

    len = strlen(in) + 1;
    return memcpy(Z_ReservedAlloc(len), in, len);
}

/*
========================
Z_Init
========================
*/
void Z_Init(void)
{
    z_chain.next = z_chain.prev = &z_chain;
}

/*
================
Z_TagCopyString
================
*/
char *Z_TagCopyString(const char *in, memtag_t tag)
{
    size_t len;

    if (!in) {
        return NULL;
    }

    len = strlen(in) + 1;
    return memcpy(Z_TagMalloc(len, tag), in, len);
}

/*
================
Z_CvarCopyString
================
*/
char *Z_CvarCopyString(const char *in)
{
    size_t len;
    zstatic_t *z;
    zstats_t *s;
    int i;

    if (!in) {
        return NULL;
    }

    if (!in[0]) {
        i = 10;
    } else if (!in[1] && Q_isdigit(in[0])) {
        i = in[0] - '0';
    } else {
        len = strlen(in) + 1;
        return memcpy(Z_TagMalloc(len, TAG_CVAR), in, len);
    }

    // return static storage
    z = (zstatic_t *)&z_static[i];
    s = &z_stats[TAG_STATIC];
    s->count++;
    s->bytes += z->z.size;
    return z->data;
}


