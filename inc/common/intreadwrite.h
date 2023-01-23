/*
Copyright (C) 2022 Andrey Nazarov

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

#if (defined __GNUC__)

struct unaligned16 { uint16_t u; } __attribute__((packed, may_alias));
struct unaligned32 { uint32_t u; } __attribute__((packed, may_alias));
struct unaligned64 { uint64_t u; } __attribute__((packed, may_alias));

#define RN16(p) (((const struct unaligned16 *)(p))->u)
#define RN32(p) (((const struct unaligned32 *)(p))->u)
#define RN64(p) (((const struct unaligned64 *)(p))->u)

#define WN16(p, v)  (((struct unaligned16 *)(p))->u = (v))
#define WN32(p, v)  (((struct unaligned32 *)(p))->u = (v))
#define WN64(p, v)  (((struct unaligned64 *)(p))->u = (v))

#elif (defined _MSC_VER)

#define RN16(p) (*(const uint16_t *)(p))
#define RN32(p) (*(const uint32_t *)(p))
#define RN64(p) (*(const uint64_t *)(p))

#define WN16(p, v)  (*(uint16_t *)(p) = (v))
#define WN32(p, v)  (*(uint32_t *)(p) = (v))
#define WN64(p, v)  (*(uint64_t *)(p) = (v))

#endif

#if USE_LITTLE_ENDIAN

#ifdef RN16
#define RL16(p) RN16(p)
#define RL32(p) RN32(p)
#define RL64(p) RN64(p)
#endif

#ifdef WN16
#define WL16(p, v) WN16(p, v)
#define WL32(p, v) WN32(p, v)
#define WL64(p, v) WN64(p, v)
#endif

#endif  // USE_LITTLE_ENDIAN

#ifndef RL16
#define RL16(p) ((((const uint8_t *)(p))[1] << 8) | ((const uint8_t *)(p))[0])
#endif

#ifndef RL32
#define RL32(p)                                     \
    (((uint32_t)((const uint8_t *)(p))[3] << 24) |  \
     ((uint32_t)((const uint8_t *)(p))[2] << 16) |  \
     ((uint32_t)((const uint8_t *)(p))[1] <<  8) |  \
     ((uint32_t)((const uint8_t *)(p))[0]))
#endif

#ifndef RL64
#define RL64(p)                                     \
    (((uint64_t)((const uint8_t *)(p))[7] << 56) |  \
     ((uint64_t)((const uint8_t *)(p))[6] << 48) |  \
     ((uint64_t)((const uint8_t *)(p))[5] << 40) |  \
     ((uint64_t)((const uint8_t *)(p))[4] << 32) |  \
     ((uint64_t)((const uint8_t *)(p))[3] << 24) |  \
     ((uint64_t)((const uint8_t *)(p))[2] << 16) |  \
     ((uint64_t)((const uint8_t *)(p))[1] <<  8) |  \
     ((uint64_t)((const uint8_t *)(p))[0]))
#endif

#ifndef WL16
#define WL16(p, v)                              \
    do {                                        \
        uint16_t _v = (v);                      \
        ((uint8_t *)p)[0] =  _v       & 0xff;   \
        ((uint8_t *)p)[1] = (_v >> 8) & 0xff;   \
    } while (0)
#endif

#ifndef WL32
#define WL32(p, v)                              \
    do {                                        \
        uint32_t _v = (v);                      \
        ((uint8_t *)p)[0] =  _v        & 0xff;  \
        ((uint8_t *)p)[1] = (_v >>  8) & 0xff;  \
        ((uint8_t *)p)[2] = (_v >> 16) & 0xff;  \
        ((uint8_t *)p)[3] = (_v >> 24) & 0xff;  \
    } while (0)
#endif

#ifndef WL64
#define WL64(p, v)                              \
    do {                                        \
        uint64_t _v = (v);                      \
        ((uint8_t *)p)[0] =  _v        & 0xff;  \
        ((uint8_t *)p)[1] = (_v >>  8) & 0xff;  \
        ((uint8_t *)p)[2] = (_v >> 16) & 0xff;  \
        ((uint8_t *)p)[3] = (_v >> 24) & 0xff;  \
        ((uint8_t *)p)[4] = (_v >> 32) & 0xff;  \
        ((uint8_t *)p)[5] = (_v >> 40) & 0xff;  \
        ((uint8_t *)p)[6] = (_v >> 48) & 0xff;  \
        ((uint8_t *)p)[7] = (_v >> 56) & 0xff;  \
    } while (0)
#endif
