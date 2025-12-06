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

#pragma once

//
// shared.h -- included first by ALL program modules
//

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>
#include <time.h>

#if HAVE_ENDIAN_H
#include <endian.h>
#endif

#include "shared/platform.h"

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define USE_LITTLE_ENDIAN   1
#elif __BYTE_ORDER == __BIG_ENDIAN
#define USE_BIG_ENDIAN      1
#endif

#define q_countof(a)        (sizeof(a) / sizeof(a[0]))

typedef unsigned char byte;
typedef enum { qfalse, qtrue } qboolean;    // ABI compat only, don't use
typedef int qhandle_t;

// angle indexes
#define PITCH               0       // up / down
#define YAW                 1       // left / right
#define ROLL                2       // fall over

#define MAX_STRING_CHARS    4096    // max length of a string passed to Cmd_TokenizeString
#define MAX_STRING_TOKENS   256     // max tokens resulting from Cmd_TokenizeString
#define MAX_TOKEN_CHARS     1024    // max length of an individual token
#define MAX_NET_STRING      2048    // max length of a string used in network protocol

#define MAX_QPATH           64      // max length of a quake game pathname
#define MAX_OSPATH          256     // max length of a filesystem pathname

//
// per-level limits
//
#define MAX_CLIENTS         256     // absolute limit
#define MAX_EDICTS_OLD      1024    // must change protocol to increase more
#define MAX_MODELS_OLD      256     // these are sent over the net as bytes
#define MAX_SOUNDS_OLD      256     // so they cannot be blindly increased
#define MAX_IMAGES_OLD      256
#define MAX_LIGHTSTYLES     256
#define MAX_ITEMS           256
#define MAX_GENERAL         (MAX_CLIENTS * 2) // general config strings

#if USE_PROTOCOL_EXTENSIONS
#define MAX_EDICTS          8192    // sent as ENTITYNUM_BITS, can't be increased
#define MAX_MODELS          8192    // half is reserved for inline BSP models
#define MAX_SOUNDS          2048
#define MAX_IMAGES          2048
#else
#define MAX_EDICTS          MAX_EDICTS_OLD
#define MAX_MODELS          MAX_MODELS_OLD
#define MAX_SOUNDS          MAX_SOUNDS_OLD
#define MAX_IMAGES          MAX_IMAGES_OLD
#endif

#define MODELINDEX_PLAYER   (MAX_MODELS_OLD - 1)

#define MAX_CLIENT_NAME     16

typedef enum {
    ERR_FATAL,          // exit the entire game with a popup window
    ERR_DROP,           // print to console and disconnect from game
    ERR_DISCONNECT,     // like drop, but not an error
    ERR_RECONNECT       // make server broadcast 'reconnect' message
} error_type_t;

typedef enum {
    PRINT_ALL,          // general messages
    PRINT_TALK,         // print in green color
    PRINT_DEVELOPER,    // only print when "developer 1"
    PRINT_WARNING,      // print in yellow color
    PRINT_ERROR,        // print in red color
    PRINT_NOTICE        // print in cyan color
} print_type_t;

void    Com_LPrintf(print_type_t type, const char *fmt, ...)
q_printf(2, 3);
void    Com_Error(error_type_t code, const char *fmt, ...)
q_noreturn q_printf(2, 3);

#define Com_Printf(...) Com_LPrintf(PRINT_ALL, __VA_ARGS__)
#define Com_WPrintf(...) Com_LPrintf(PRINT_WARNING, __VA_ARGS__)
#define Com_EPrintf(...) Com_LPrintf(PRINT_ERROR, __VA_ARGS__)
#define Com_NPrintf(...) Com_LPrintf(PRINT_NOTICE, __VA_ARGS__)

#define Q_assert(expr) \
    do { if (!(expr)) Com_Error(ERR_FATAL, "%s: assertion `%s' failed", __func__, #expr); } while (0)

// game print flags
#define PRINT_LOW           0       // pickup messages
#define PRINT_MEDIUM        1       // death messages
#define PRINT_HIGH          2       // critical messages
#define PRINT_CHAT          3       // chat messages    

// destination class for gi.multicast()
typedef enum {
    MULTICAST_ALL,
    MULTICAST_PHS,
    MULTICAST_PVS,
    MULTICAST_ALL_R,
    MULTICAST_PHS_R,
    MULTICAST_PVS_R
} multicast_t;

typedef char configstring_t[MAX_QPATH];

/*
==============================================================

MATHLIB

==============================================================
*/

typedef float vec_t;
typedef vec_t vec2_t[2];
typedef vec_t vec3_t[3];
typedef vec_t vec4_t[4];
typedef vec_t vec5_t[5];

typedef vec_t quat_t[4];

typedef float mat4_t[16];

typedef union {
    uint32_t u32;
    uint8_t u8[4];
} color_t;

extern const vec3_t vec3_origin;

typedef struct vrect_s {
    int             x, y, width, height;
} vrect_t;

#ifndef M_PIf
#define M_PIf       3.14159265358979323846f
#define M_SQRT2f    1.41421356237309504880f
#define M_SQRT1_2f  0.70710678118654752440f
#endif

#define DEG2RAD(a)      ((a) * (M_PIf / 180))
#define RAD2DEG(a)      ((a) * (180 / M_PIf))

#define ALIGN(x, a)     (((x) + (a) - 1) & ~((a) - 1))

#define BIT(n)          (1U << (n))
#define BIT_ULL(n)      (1ULL << (n))

#define SWAP(type, a, b) \
    do { type SWAP_tmp = a; a = b; b = SWAP_tmp; } while (0)

#define DotProduct(x,y)         ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#define CrossProduct(v1,v2,cross) \
        ((cross)[0]=(v1)[1]*(v2)[2]-(v1)[2]*(v2)[1], \
         (cross)[1]=(v1)[2]*(v2)[0]-(v1)[0]*(v2)[2], \
         (cross)[2]=(v1)[0]*(v2)[1]-(v1)[1]*(v2)[0])
#define VectorSubtract(a,b,c) \
        ((c)[0]=(a)[0]-(b)[0], \
         (c)[1]=(a)[1]-(b)[1], \
         (c)[2]=(a)[2]-(b)[2])
#define VectorAdd(a,b,c) \
        ((c)[0]=(a)[0]+(b)[0], \
         (c)[1]=(a)[1]+(b)[1], \
         (c)[2]=(a)[2]+(b)[2])
#define VectorAdd3(a,b,c,d) \
        ((d)[0]=(a)[0]+(b)[0]+(c)[0], \
         (d)[1]=(a)[1]+(b)[1]+(c)[1], \
         (d)[2]=(a)[2]+(b)[2]+(c)[2])
#define VectorCopy(a,b)     ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2])
#define VectorClear(a)      ((a)[0]=(a)[1]=(a)[2]=0)
#define VectorNegate(a,b)   ((b)[0]=-(a)[0],(b)[1]=-(a)[1],(b)[2]=-(a)[2])
#define VectorInverse(a)    ((a)[0]=-(a)[0],(a)[1]=-(a)[1],(a)[2]=-(a)[2])
#define VectorSet(v, x, y, z)   ((v)[0]=(x),(v)[1]=(y),(v)[2]=(z))
#define VectorAvg(a,b,c) \
        ((c)[0]=((a)[0]+(b)[0])*0.5f, \
         (c)[1]=((a)[1]+(b)[1])*0.5f, \
         (c)[2]=((a)[2]+(b)[2])*0.5f)
#define VectorMA(a,b,c,d) \
        ((d)[0]=(a)[0]+(b)*(c)[0], \
         (d)[1]=(a)[1]+(b)*(c)[1], \
         (d)[2]=(a)[2]+(b)*(c)[2])
#define VectorVectorMA(a,b,c,d) \
        ((d)[0]=(a)[0]+(b)[0]*(c)[0], \
         (d)[1]=(a)[1]+(b)[1]*(c)[1], \
         (d)[2]=(a)[2]+(b)[2]*(c)[2])
#define VectorEmpty(v) ((v)[0]==0&&(v)[1]==0&&(v)[2]==0)
#define VectorCompare(v1,v2)    ((v1)[0]==(v2)[0]&&(v1)[1]==(v2)[1]&&(v1)[2]==(v2)[2])
#define VectorLength(v)     (sqrtf(DotProduct((v),(v))))
#define VectorLengthSquared(v)      (DotProduct((v),(v)))
#define VectorScale(in,scale,out) \
        ((out)[0]=(in)[0]*(scale), \
         (out)[1]=(in)[1]*(scale), \
         (out)[2]=(in)[2]*(scale))
#define VectorVectorScale(in,scale,out) \
        ((out)[0]=(in)[0]*(scale)[0], \
         (out)[1]=(in)[1]*(scale)[1], \
         (out)[2]=(in)[2]*(scale)[2])
#define DistanceSquared(v1,v2) \
        (((v1)[0]-(v2)[0])*((v1)[0]-(v2)[0])+ \
        ((v1)[1]-(v2)[1])*((v1)[1]-(v2)[1])+ \
        ((v1)[2]-(v2)[2])*((v1)[2]-(v2)[2]))
#define Distance(v1,v2) (sqrtf(DistanceSquared(v1,v2)))
#define LerpAngles(a,b,c,d) \
        ((d)[0]=LerpAngle((a)[0],(b)[0],c), \
         (d)[1]=LerpAngle((a)[1],(b)[1],c), \
         (d)[2]=LerpAngle((a)[2],(b)[2],c))
#define LerpVector(a,b,c,d) \
    ((d)[0]=(a)[0]+(c)*((b)[0]-(a)[0]), \
     (d)[1]=(a)[1]+(c)*((b)[1]-(a)[1]), \
     (d)[2]=(a)[2]+(c)*((b)[2]-(a)[2]))
#define LerpVector2(a,b,c,d,e) \
    ((e)[0]=(a)[0]*(c)+(b)[0]*(d), \
     (e)[1]=(a)[1]*(c)+(b)[1]*(d), \
     (e)[2]=(a)[2]*(c)+(b)[2]*(d))
#define PlaneDiff(v,p)   (DotProduct(v,(p)->normal)-(p)->dist)

#define Vector2Subtract(a,b,c)  ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1])
#define Vector2Add(a,b,c)       ((c)[0]=(a)[0]+(b)[0],(c)[1]=(a)[1]+(b)[1])

#define Vector4Subtract(a,b,c)  ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1],(c)[2]=(a)[2]-(b)[2],(c)[3]=(a)[3]-(b)[3])
#define Vector4Add(a,b,c)       ((c)[0]=(a)[0]+(b)[0],(c)[1]=(a)[1]+(b)[1],(c)[2]=(a)[2]+(b)[2],(c)[3]=(a)[3]+(b)[3])
#define Vector4Copy(a,b)        ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2],(b)[3]=(a)[3])
#define Vector4Clear(a)         ((a)[0]=(a)[1]=(a)[2]=(a)[3]=0)
#define Vector4Negate(a,b)      ((b)[0]=-(a)[0],(b)[1]=-(a)[1],(b)[2]=-(a)[2],(b)[3]=-(a)[3])
#define Vector4Set(v, a, b, c, d)   ((v)[0]=(a),(v)[1]=(b),(v)[2]=(c),(v)[3]=(d))
#define Vector4Compare(v1,v2)    ((v1)[0]==(v2)[0]&&(v1)[1]==(v2)[1]&&(v1)[2]==(v2)[2]&&(v1)[3]==(v2)[3])
#define Vector4MA(a,b,c,d) \
        ((d)[0]=(a)[0]+(b)*(c)[0], \
         (d)[1]=(a)[1]+(b)*(c)[1], \
         (d)[2]=(a)[2]+(b)*(c)[2], \
         (d)[3]=(a)[3]+(b)*(c)[3])

#define QuatCopy(a,b)			((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2],(b)[3]=(a)[3])

void AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);
vec_t VectorNormalize(vec3_t v);        // returns vector length
vec_t VectorNormalize2(const vec3_t v, vec3_t out);
void ClearBounds(vec3_t mins, vec3_t maxs);
void AddPointToBounds(const vec3_t v, vec3_t mins, vec3_t maxs);
vec_t RadiusFromBounds(const vec3_t mins, const vec3_t maxs);
void UnionBounds(const vec3_t a[2], const vec3_t b[2], vec3_t c[2]);

static inline void AnglesToAxis(const vec3_t angles, vec3_t axis[3])
{
    AngleVectors(angles, axis[0], axis[1], axis[2]);
    VectorInverse(axis[1]);
}

static inline void TransposeAxis(vec3_t axis[3])
{
    SWAP(vec_t, axis[0][1], axis[1][0]);
    SWAP(vec_t, axis[0][2], axis[2][0]);
    SWAP(vec_t, axis[1][2], axis[2][1]);
}

static inline void RotatePoint(vec3_t point, const vec3_t axis[3])
{
    vec3_t temp;

    VectorCopy(point, temp);
    point[0] = DotProduct(temp, axis[0]);
    point[1] = DotProduct(temp, axis[1]);
    point[2] = DotProduct(temp, axis[2]);
}

static inline uint32_t Q_npot32(uint32_t k)
{
    if (k == 0)
        return 1;

    k--;
    k = k | (k >> 1);
    k = k | (k >> 2);
    k = k | (k >> 4);
    k = k | (k >> 8);
    k = k | (k >> 16);

    return k + 1;
}

static inline float LerpAngle(float a2, float a1, float frac)
{
    if (a1 - a2 > 180)
        a1 -= 360;
    if (a1 - a2 < -180)
        a1 += 360;
    return a2 + frac * (a1 - a2);
}

static inline float anglemod(float a)
{
    a = (360.0f / 65536) * ((int)(a * (65536 / 360.0f)) & 65535);
    return a;
}

static inline int Q_align(int value, int align)
{
    int mod = value % align;
    return mod ? value + align - mod : value;
}

static inline int Q_gcd(int a, int b)
{
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}

void Q_srand(uint32_t seed);
uint32_t Q_rand(void);
uint32_t Q_rand_uniform(uint32_t n);

static inline int Q_clip(int a, int b, int c)
{
    if (a < b)
        return b;
    if (a > c)
        return c;
    return a;
}

static inline float Q_clipf(float a, float b, float c)
{
#if defined(__GNUC__) && defined(__SSE__)
    __asm__("maxss %1, %0 \n\t"
            "minss %2, %0 \n\t"
            : "+&x"(a) : "xm"(b), "xm"(c));
    return a;
#else
    if (a < b)
        return b;
    if (a > c)
        return c;
    return a;
#endif
}

static inline float Q_circ_clipf(float a, float b, float c)
{
    return b > c ? Q_clipf(a, c, b) : Q_clipf(a, b, c);
}

static inline int8_t Q_clip_int8(int a)
{
    return ((a + 0x80U) & ~0xFF) ? (a >> 31) ^ 0x7F : a;
}

static inline int16_t Q_clip_int16(int a)
{
    return ((a + 0x8000U) & ~0xFFFF) ? (a >> 31) ^ 0x7FFF : a;
}

static inline int32_t Q_clip_int32(int64_t a)
{
    return ((a + 0x80000000ULL) & ~0xFFFFFFFFULL) ? (a >> 63) ^ 0x7FFFFFFF : a;
}

#ifdef _LP64
#define Q_clipl_int32(a)    Q_clip_int32(a)
#else
#define Q_clipl_int32(a)    (a)
#endif

static inline uint8_t Q_clip_uint8(int a)
{
    return (a & ~0xFF) ? ~a >> 31 : a;
}

static inline uint16_t Q_clip_uint16(int a)
{
    return (a & ~0xFFFF) ? ~a >> 31 : a;
}

#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif

#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

#define frand()     ((int32_t)Q_rand() * 0x1p-32f + 0.5f)
#define crand()     ((int32_t)Q_rand() * 0x1p-31f)

#define Q_rint(x)   ((x) < 0 ? ((int)((x) - 0.5f)) : ((int)((x) + 0.5f)))

#define Q_IsBitSet(data, bit)   (((data)[(bit) >> 3] & (1 << ((bit) & 7))) != 0)
#define Q_SetBit(data, bit)     ((data)[(bit) >> 3] |= (1 << ((bit) & 7)))
#define Q_ClearBit(data, bit)   ((data)[(bit) >> 3] &= ~(1 << ((bit) & 7)))

//=============================================

// fast "C" macros
#define Q_isupper(c)    ((c) >= 'A' && (c) <= 'Z')
#define Q_islower(c)    ((c) >= 'a' && (c) <= 'z')
#define Q_isdigit(c)    ((c) >= '0' && (c) <= '9')
#define Q_isalpha(c)    (Q_isupper(c) || Q_islower(c))
#define Q_isalnum(c)    (Q_isalpha(c) || Q_isdigit(c))
#define Q_isprint(c)    ((c) >= 32 && (c) < 127)
#define Q_isgraph(c)    ((c) > 32 && (c) < 127)
#define Q_isspace(c)    (c == ' ' || c == '\f' || c == '\n' || \
                         c == '\r' || c == '\t' || c == '\v')

// tests if specified character is valid quake path character
#define Q_ispath(c)     (Q_isalnum(c) || (c) == '_' || (c) == '-')

// tests if specified character has special meaning to quake console
#define Q_isspecial(c)  ((c) == '\r' || (c) == '\n' || (c) == 127)

static inline int Q_tolower(int c)
{
    if (Q_isupper(c)) {
        c += ('a' - 'A');
    }
    return c;
}

static inline int Q_toupper(int c)
{
    if (Q_islower(c)) {
        c -= ('a' - 'A');
    }
    return c;
}

static inline char *Q_strlwr(char *s)
{
    char *p = s;

    while (*p) {
        *p = Q_tolower(*p);
        p++;
    }

    return s;
}

static inline char *Q_strupr(char *s)
{
    char *p = s;

    while (*p) {
        *p = Q_toupper(*p);
        p++;
    }

    return s;
}

static inline int Q_charhex(int c)
{
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    return -1;
}

// converts quake char to ASCII equivalent
static inline int Q_charascii(int c)
{
    if (Q_isspace(c)) {
        // white-space chars are output as-is
        return c;
    }
    c &= 127; // strip high bits
    if (Q_isprint(c)) {
        return c;
    }
    switch (c) {
        // handle bold brackets
        case 16: return '[';
        case 17: return ']';
    }
    return '.'; // don't output control chars, etc
}

// portable case insensitive compare
int Q_strcasecmp(const char *s1, const char *s2);
int Q_strncasecmp(const char *s1, const char *s2, size_t n);
char *Q_strcasestr(const char *s1, const char *s2);

#define Q_stricmp   Q_strcasecmp
#define Q_stricmpn  Q_strncasecmp
#define Q_stristr   Q_strcasestr

char *Q_strchrnul(const char *s, int c);
void *Q_memccpy(void *dst, const void *src, int c, size_t size);
size_t Q_strnlen(const char *s, size_t maxlen);

#ifdef _WIN32
#define Q_atoi(s) atoi(s)
#else
int Q_atoi(const char *s);
#endif

#define Q_atof(s) strtof(s, NULL)

char *COM_SkipPath(const char *pathname);
size_t COM_StripExtension(char *out, const char *in, size_t size);
void COM_FilePath(const char *in, char *out, size_t size);
size_t COM_DefaultExtension(char *path, const char *ext, size_t size);
char *COM_FileExtension(const char *in);

#define COM_CompareExtension(in, ext) \
    Q_strcasecmp(COM_FileExtension(in), ext)

bool COM_IsFloat(const char *s);
bool COM_IsUint(const char *s);
bool COM_IsPath(const char *s);
bool COM_IsWhite(const char *s);

char *COM_Parse(const char **data_p);
// data is an in/out parm, returns a parsed out token
size_t COM_Compress(char *data);

int SortStrcmp(const void *p1, const void *p2);
int SortStricmp(const void *p1, const void *p2);

size_t COM_strclr(char *s);
char *COM_StripQuotes(char *s);
char *COM_TrimSpace(char *s);

// buffer safe operations
size_t Q_strlcpy(char *dst, const char *src, size_t size);
size_t Q_strlcat(char *dst, const char *src, size_t size);

#define Q_concat(dest, size, ...) \
    Q_concat_array(dest, size, (const char *[]){__VA_ARGS__, NULL})
size_t Q_concat_array(char *dest, size_t size, const char **arr);

size_t Q_vsnprintf(char *dest, size_t size, const char *fmt, va_list argptr);
size_t Q_vscnprintf(char *dest, size_t size, const char *fmt, va_list argptr);
size_t Q_snprintf(char *dest, size_t size, const char *fmt, ...) q_printf(3, 4);
size_t Q_scnprintf(char *dest, size_t size, const char *fmt, ...) q_printf(3, 4);

char    *va(const char *format, ...) q_printf(1, 2);
char    *vtos(const vec3_t v);

//=============================================

static inline uint16_t ShortSwap(uint16_t s)
{
    s = (s >> 8) | (s << 8);
    return s;
}

static inline uint32_t LongSwap(uint32_t l)
{
    l = ((l >> 8) & 0x00ff00ff) | ((l << 8) & 0xff00ff00);
    l = (l >> 16) | (l << 16);
    return l;
}

static inline float FloatSwap(float f)
{
    union {
        float f;
        uint32_t l;
    } dat1, dat2;

    dat1.f = f;
    dat2.l = LongSwap(dat1.l);
    return dat2.f;
}

static inline float LongToFloat(uint32_t l)
{
    union {
        float f;
        uint32_t l;
    } dat;

    dat.l = l;
    return dat.f;
}

#if USE_LITTLE_ENDIAN
#define BigShort    ShortSwap
#define BigLong     LongSwap
#define BigFloat    FloatSwap
#define LittleShort(x)    ((uint16_t)(x))
#define LittleLong(x)     ((uint32_t)(x))
#define LittleFloat(x)    ((float)(x))
#define MakeRawLong(b1,b2,b3,b4) (((unsigned)(b4)<<24)|((b3)<<16)|((b2)<<8)|(b1))
#define MakeRawShort(b1,b2) (((b2)<<8)|(b1))
#elif USE_BIG_ENDIAN
#define BigShort(x)     ((uint16_t)(x))
#define BigLong(x)      ((uint32_t)(x))
#define BigFloat(x)     ((float)(x))
#define LittleShort ShortSwap
#define LittleLong  LongSwap
#define LittleFloat FloatSwap
#define MakeRawLong(b1,b2,b3,b4) (((unsigned)(b1)<<24)|((b2)<<16)|((b3)<<8)|(b4))
#define MakeRawShort(b1,b2) (((b1)<<8)|(b2))
#else
#error Unknown byte order
#endif

#define MakeLittleLong(b1,b2,b3,b4) (((unsigned)(b4)<<24)|((b3)<<16)|((b2)<<8)|(b1))

#define LittleVector(a,b) \
    ((b)[0]=LittleFloat((a)[0]),\
     (b)[1]=LittleFloat((a)[1]),\
     (b)[2]=LittleFloat((a)[2]))

static inline void LittleBlock(void *out, const void *in, size_t size)
{
    memcpy(out, in, size);
#if USE_BIG_ENDIAN
    for (int i = 0; i < size / 4; i++)
        ((uint32_t *)out)[i] = LittleLong(((uint32_t *)out)[i]);
#endif
}

#if USE_BGRA
#define MakeColor(r, g, b, a)   MakeRawLong(b, g, r, a)
#else
#define MakeColor(r, g, b, a)   MakeRawLong(r, g, b, a)
#endif

//=============================================

//
// key / value info strings
//
#define MAX_INFO_KEY        64
#define MAX_INFO_VALUE      64
#define MAX_INFO_STRING     512

char    *Info_ValueForKey(const char *s, const char *key);
void    Info_RemoveKey(char *s, const char *key);
bool    Info_SetValueForKey(char *s, const char *key, const char *value);
bool    Info_Validate(const char *s);
size_t  Info_SubValidate(const char *s);
void    Info_NextPair(const char **string, char *key, char *value);
void    Info_Print(const char *infostring);

/*
==========================================================

CVARS (console variables)

==========================================================
*/

#ifndef CVAR
#define CVAR

#define CVAR_ARCHIVE    BIT(0)  // set to cause it to be saved to vars.rc
#define CVAR_USERINFO   BIT(1)  // added to userinfo when changed
#define CVAR_SERVERINFO BIT(2)  // added to serverinfo when changed
#define CVAR_NOSET      BIT(3)  // don't allow change from console at all,
                                // but can be set from the command line
#define CVAR_LATCH      BIT(4)  // save changes until server restart

#if USE_CLIENT || USE_SERVER
struct cvar_s;
struct genctx_s;

typedef void (*xchanged_t)(struct cvar_s *);
typedef void (*xgenerator_t)(struct genctx_s *);
#endif

// nothing outside the cvar.*() functions should modify these fields!
typedef struct cvar_s {
    char        *name;
    char        *string;
    char        *latched_string;    // for CVAR_LATCH vars
    int         flags;
    qboolean    modified;   // set each time the cvar is changed
    float       value;
    struct cvar_s *next;

// ------ new stuff ------
#if USE_CLIENT || USE_SERVER
    int         integer;
    char        *default_string;
    xchanged_t      changed;
    xgenerator_t    generator;
    struct cvar_s   *hashNext;
#endif
} cvar_t;

#endif      // CVAR

/*
==============================================================

COLLISION DETECTION

==============================================================
*/

// lower bits are stronger, and will eat weaker brushes completely
#define CONTENTS_SOLID          BIT(0)      // an eye is never valid in a solid
#define CONTENTS_WINDOW         BIT(1)      // translucent, but not watery
#define CONTENTS_AUX            BIT(2)
#define CONTENTS_LAVA           BIT(3)
#define CONTENTS_SLIME          BIT(4)
#define CONTENTS_WATER          BIT(5)
#define CONTENTS_MIST           BIT(6)

// remaining contents are non-visible, and don't eat brushes

#define CONTENTS_AREAPORTAL     BIT(15)

#define CONTENTS_PLAYERCLIP     BIT(16)
#define CONTENTS_MONSTERCLIP    BIT(17)

// currents can be added to any other contents, and may be mixed
#define CONTENTS_CURRENT_0      BIT(18)
#define CONTENTS_CURRENT_90     BIT(19)
#define CONTENTS_CURRENT_180    BIT(20)
#define CONTENTS_CURRENT_270    BIT(21)
#define CONTENTS_CURRENT_UP     BIT(22)
#define CONTENTS_CURRENT_DOWN   BIT(23)

#define CONTENTS_ORIGIN         BIT(24)     // removed before bsping an entity

#define CONTENTS_MONSTER        BIT(25)     // should never be on a brush, only in game
#define CONTENTS_DEADMONSTER    BIT(26)
#define CONTENTS_DETAIL         BIT(27)     // brushes to be added after vis leafs
#define CONTENTS_TRANSLUCENT    BIT(28)     // auto set if any surface has trans
#define CONTENTS_LADDER         BIT(29)

#define SURF_LIGHT              BIT(0)      // value will hold the light strength
#define SURF_SLICK              BIT(1)      // effects game physics
#define SURF_SKY                BIT(2)      // don't draw, but add to skybox
#define SURF_WARP               BIT(3)      // turbulent water warp
#define SURF_TRANS33            BIT(4)
#define SURF_TRANS66            BIT(5)
#define SURF_FLOWING            BIT(6)      // scroll towards angle
#define SURF_NODRAW             BIT(7)      // don't bother referencing the texture

#define SURF_ALPHATEST          BIT(25)     // used by kmquake2

#define SURF_N64_UV             BIT(28)
#define SURF_N64_SCROLL_X       BIT(29)
#define SURF_N64_SCROLL_Y       BIT(30)
#define SURF_N64_SCROLL_FLIP    BIT(31)

// content masks
#define MASK_ALL                (-1)
#define MASK_SOLID              (CONTENTS_SOLID|CONTENTS_WINDOW)
#define MASK_PLAYERSOLID        (CONTENTS_SOLID|CONTENTS_PLAYERCLIP|CONTENTS_WINDOW|CONTENTS_MONSTER)
#define MASK_DEADSOLID          (CONTENTS_SOLID|CONTENTS_PLAYERCLIP|CONTENTS_WINDOW)
#define MASK_MONSTERSOLID       (CONTENTS_SOLID|CONTENTS_MONSTERCLIP|CONTENTS_WINDOW|CONTENTS_MONSTER)
#define MASK_WATER              (CONTENTS_WATER|CONTENTS_LAVA|CONTENTS_SLIME)
#define MASK_OPAQUE             (CONTENTS_SOLID|CONTENTS_SLIME|CONTENTS_LAVA)
#define MASK_SHOT               (CONTENTS_SOLID|CONTENTS_MONSTER|CONTENTS_WINDOW|CONTENTS_DEADMONSTER)
#define MASK_CURRENT            (CONTENTS_CURRENT_0|CONTENTS_CURRENT_90|CONTENTS_CURRENT_180|CONTENTS_CURRENT_270|CONTENTS_CURRENT_UP|CONTENTS_CURRENT_DOWN)

// gi.BoxEdicts() can return a list of either solid or trigger entities
// FIXME: eliminate AREA_ distinction?
#define AREA_SOLID      1
#define AREA_TRIGGERS   2

// plane_t structure
typedef struct cplane_s {
    vec3_t  normal;
    float   dist;
    byte    type;           // for fast side tests
    byte    signbits;       // signx + (signy<<1) + (signz<<1)
    byte    pad[2];
} cplane_t;

// 0-2 are axial planes
#define PLANE_X         0
#define PLANE_Y         1
#define PLANE_Z         2
#define PLANE_NON_AXIAL 6

typedef struct csurface_s {
    char        name[16];
    int         flags;
    int         value;
} csurface_t;

// a trace is returned when a box is swept through the world
typedef struct {
    qboolean    allsolid;   // if true, plane is not valid
    qboolean    startsolid; // if true, the initial point was in a solid area
    float       fraction;   // time completed, 1.0 = didn't hit anything
    vec3_t      endpos;     // final position
    cplane_t    plane;      // surface normal at impact
    csurface_t  *surface;   // surface hit
    int         contents;   // contents on other side of surface hit
    struct edict_s  *ent;   // not set by CM_*() functions
} trace_t;

// pmove_state_t is the information necessary for client side movement
// prediction
typedef enum {
    // can accelerate and turn
    PM_NORMAL,
    PM_SPECTATOR,
    // no acceleration or turning
    PM_DEAD,
    PM_GIB,     // different bounding box
    PM_FREEZE
} pmtype_t;

// pmove->pm_flags
#define PMF_DUCKED          BIT(0)
#define PMF_JUMP_HELD       BIT(1)
#define PMF_ON_GROUND       BIT(2)
#define PMF_TIME_WATERJUMP  BIT(3)      // pm_time is waterjump
#define PMF_TIME_LAND       BIT(4)      // pm_time is time before rejump
#define PMF_TIME_TELEPORT   BIT(5)      // pm_time is non-moving time
#define PMF_NO_PREDICTION   BIT(6)      // temporarily disables prediction (used for grappling hook)
#define PMF_TELEPORT_BIT    BIT(7)      // used by q2pro

// this structure needs to be communicated bit-accurate
// from the server to the client to guarantee that
// prediction stays in sync, so no floats are used.
// if any part of the game code modifies this struct, it
// will result in a prediction error of some degree.
typedef struct {
    pmtype_t    pm_type;

    short       origin[3];      // 12.3
    short       velocity[3];    // 12.3
    byte        pm_flags;       // ducked, jump_held, etc
    byte        pm_time;        // each unit = 8 ms
    short       gravity;
    short       delta_angles[3];    // add to command angles to get view direction
                                    // changed by spawns, rotating objects, and teleporters
} pmove_state_t;

//
// button bits
//
#define BUTTON_ATTACK   BIT(0)
#define BUTTON_USE      BIT(1)
#define BUTTON_ANY      BIT(7)  // any key whatsoever

// usercmd_t is sent to the server each client frame
typedef struct usercmd_s {
    byte    msec;
    byte    buttons;
    short   angles[3];
    short   forwardmove, sidemove, upmove;
    byte    impulse;        // remove?
    byte    lightlevel;     // light level the player is standing on
} usercmd_t;

#define MAXTOUCH    32
typedef struct {
    // state (in / out)
    pmove_state_t   s;

    // command (in)
    usercmd_t       cmd;
    qboolean        snapinitial;    // if s has been changed outside pmove

    // results (out)
    int         numtouch;
    struct edict_s  *touchents[MAXTOUCH];

    vec3_t      viewangles;         // clamped
    float       viewheight;

    vec3_t      mins, maxs;         // bounding box size

    struct edict_s  *groundentity;
    int         watertype;
    int         waterlevel;

    // callbacks to test the world
    trace_t     (* q_gameabi trace)(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end);
    int         (*pointcontents)(const vec3_t point);
} pmove_t;

// entity_state_t->effects
// Effects are things handled on the client side (lights, particles, frame animations)
// that happen constantly on the given entity.
// An entity that has effects will be sent to the client
// even if it has a zero index model.
#define EF_ROTATE           BIT(0)      // rotate (bonus items)
#define EF_GIB              BIT(1)      // leave a trail
#define EF_BOB              BIT(2)      // used by KEX
#define EF_BLASTER          BIT(3)      // redlight + trail
#define EF_ROCKET           BIT(4)      // redlight + trail
#define EF_GRENADE          BIT(5)
#define EF_HYPERBLASTER     BIT(6)
#define EF_BFG              BIT(7)
#define EF_COLOR_SHELL      BIT(8)
#define EF_POWERSCREEN      BIT(9)
#define EF_ANIM01           BIT(10)     // automatically cycle between frames 0 and 1 at 2 hz
#define EF_ANIM23           BIT(11)     // automatically cycle between frames 2 and 3 at 2 hz
#define EF_ANIM_ALL         BIT(12)     // automatically cycle through all frames at 2hz
#define EF_ANIM_ALLFAST     BIT(13)     // automatically cycle through all frames at 10hz
#define EF_FLIES            BIT(14)
#define EF_QUAD             BIT(15)
#define EF_PENT             BIT(16)
#define EF_TELEPORTER       BIT(17)     // particle fountain
#define EF_FLAG1            BIT(18)
#define EF_FLAG2            BIT(19)

// RAFAEL
#define EF_IONRIPPER        BIT(20)
#define EF_GREENGIB         BIT(21)
#define EF_BLUEHYPERBLASTER BIT(22)
#define EF_SPINNINGLIGHTS   BIT(23)
#define EF_PLASMA           BIT(24)
#define EF_TRAP             BIT(25)

//ROGUE
#define EF_TRACKER          BIT(26)
#define EF_DOUBLE           BIT(27)
#define EF_SPHERETRANS      BIT(28)
#define EF_TAGTRAIL         BIT(29)
#define EF_HALF_DAMAGE      BIT(30)
#define EF_TRACKERTRAIL     BIT(31)
//ROGUE

// entity_state_t->morefx flags
//KEX
#define EFX_DUALFIRE            BIT(0)
#define EFX_HOLOGRAM            BIT(1)
#define EFX_FLASHLIGHT          BIT(2)
#define EFX_BARREL_EXPLODING    BIT(3)
#define EFX_TELEPORTER2         BIT(4)
#define EFX_GRENADE_LIGHT       BIT(5)
//KEX

// entity_state_t->renderfx flags
#define RF_MINLIGHT         BIT(0)      // allways have some light (viewmodel)
#define RF_VIEWERMODEL      BIT(1)      // don't draw through eyes, only mirrors
#define RF_WEAPONMODEL      BIT(2)      // only draw through eyes
#define RF_FULLBRIGHT       BIT(3)      // allways draw full intensity
#define RF_DEPTHHACK        BIT(4)      // for view weapon Z crunching
#define RF_TRANSLUCENT      BIT(5)
#define RF_FRAMELERP        BIT(6)
#define RF_BEAM             BIT(7)
#define RF_CUSTOMSKIN       BIT(8)      // skin is an index in image_precache
#define RF_GLOW             BIT(9)      // pulse lighting for bonus items
#define RF_SHELL_RED        BIT(10)
#define RF_SHELL_GREEN      BIT(11)
#define RF_SHELL_BLUE       BIT(12)
#define RF_NOSHADOW         BIT(13)     // used by YQ2
#define RF_CASTSHADOW       BIT(14)     // used by KEX

//ROGUE
#define RF_IR_VISIBLE       BIT(15)
#define RF_SHELL_DOUBLE     BIT(16)
#define RF_SHELL_HALF_DAM   BIT(17)
#define RF_USE_DISGUISE     BIT(18)
//ROGUE

//KEX
#define RF_SHELL_LITE_GREEN BIT(19)
#define RF_CUSTOM_LIGHT     BIT(20)
#define RF_FLARE            BIT(21)
#define RF_OLD_FRAME_LERP   BIT(22)
#define RF_DOT_SHADOW       BIT(23)
#define RF_LOW_PRIORITY     BIT(24)
#define RF_NO_LOD           BIT(25)
#define RF_STAIR_STEP       BIT(26)

#define RF_NO_STEREO        RF_WEAPONMODEL
#define RF_FLARE_LOCK_ANGLE RF_MINLIGHT
#define RF_BEAM_LIGHTNING   (RF_BEAM | RF_GLOW)
//KEX

// player_state_t->refdef flags
#define RDF_UNDERWATER      BIT(0)      // warp the screen as apropriate
#define RDF_NOWORLDMODEL    BIT(1)      // used for player configuration screen

//ROGUE
#define RDF_IRGOGGLES       BIT(2)
#define RDF_UVGOGGLES       BIT(3)
//ROGUE

//KEX
#define RDF_NO_WEAPON_LERP  BIT(4)
//KEX

//
// muzzle flashes / player effects
//
enum {
    MZ_BLASTER,
    MZ_MACHINEGUN,
    MZ_SHOTGUN,
    MZ_CHAINGUN1,
    MZ_CHAINGUN2,
    MZ_CHAINGUN3,
    MZ_RAILGUN,
    MZ_ROCKET,
    MZ_GRENADE,
    MZ_LOGIN,
    MZ_LOGOUT,
    MZ_RESPAWN,
    MZ_BFG,
    MZ_SSHOTGUN,
    MZ_HYPERBLASTER,
    MZ_ITEMRESPAWN,

// RAFAEL
    MZ_IONRIPPER,
    MZ_BLUEHYPERBLASTER,
    MZ_PHALANX,

// KEX
    MZ_BFG2,
    MZ_PHALANX2,

//ROGUE
    MZ_ETF_RIFLE = 30,
    MZ_PROX,        // KEX
    MZ_SHOTGUN2,    // MZ_ETF_RIFLE_2 in KEX
    MZ_HEATBEAM,
    MZ_BLASTER2,
    MZ_TRACKER,
    MZ_NUKE1,
    MZ_NUKE2,
    MZ_NUKE4,
    MZ_NUKE8,
//ROGUE
// Q2RTX
#define MZ_FLARE            40
// Q2RTX

    MZ_SILENCED = BIT(7),  // bit flag ORed with one of the above numbers
};

// temp entity events
//
// Temp entity events are for things that happen
// at a location seperate from any existing entity.
// Temporary entity messages are explicitly constructed
// and broadcast.
typedef enum {
    TE_GUNSHOT,
    TE_BLOOD,
    TE_BLASTER,
    TE_RAILTRAIL,
    TE_SHOTGUN,
    TE_EXPLOSION1,
    TE_EXPLOSION2,
    TE_ROCKET_EXPLOSION,
    TE_GRENADE_EXPLOSION,
    TE_SPARKS,
    TE_SPLASH,
    TE_BUBBLETRAIL,
    TE_SCREEN_SPARKS,
    TE_SHIELD_SPARKS,
    TE_BULLET_SPARKS,
    TE_LASER_SPARKS,
    TE_PARASITE_ATTACK,
    TE_ROCKET_EXPLOSION_WATER,
    TE_GRENADE_EXPLOSION_WATER,
    TE_MEDIC_CABLE_ATTACK,
    TE_BFG_EXPLOSION,
    TE_BFG_BIGEXPLOSION,
    TE_BOSSTPORT,           // used as '22' in a map, so DON'T RENUMBER!!!
    TE_BFG_LASER,
    TE_GRAPPLE_CABLE,
    TE_WELDING_SPARKS,
    TE_GREENBLOOD,
    TE_BLUEHYPERBLASTER,
    TE_PLASMA_EXPLOSION,
    TE_TUNNEL_SPARKS,

//ROGUE
    TE_BLASTER2,
    TE_RAILTRAIL2,
    TE_FLAME,
    TE_LIGHTNING,
    TE_DEBUGTRAIL,
    TE_PLAIN_EXPLOSION,
    TE_FLASHLIGHT,
    TE_FORCEWALL,
    TE_HEATBEAM,
    TE_MONSTER_HEATBEAM,
    TE_STEAM,
    TE_BUBBLETRAIL2,
    TE_MOREBLOOD,
    TE_HEATBEAM_SPARKS,
    TE_HEATBEAM_STEAM,
    TE_CHAINFIST_SMOKE,
    TE_ELECTRIC_SPARKS,
    TE_TRACKER_EXPLOSION,
    TE_TELEPORT_EFFECT,
    TE_DBALL_GOAL,
    TE_WIDOWBEAMOUT,
    TE_NUKEBLAST,
    TE_WIDOWSPLASH,
    TE_EXPLOSION1_BIG,
    TE_EXPLOSION1_NP,
    TE_FLECHETTE,
//ROGUE

//[Paril-KEX]
    TE_BLUEHYPERBLASTER_2,
    TE_BFG_ZAP,
    TE_BERSERK_SLAM,
    TE_GRAPPLE_CABLE_2,
    TE_POWER_SPLASH,
    TE_LIGHTNING_BEAM,
    TE_EXPLOSION1_NL,
    TE_EXPLOSION2_NL,
//[Paril-KEX]

// Q2RTX
	TE_FLARE,
// Q2RTX

    TE_NUM_ENTITIES
} temp_event_t;

enum {
    SPLASH_UNKNOWN,
    SPLASH_SPARKS,
    SPLASH_BLUE_WATER,
    SPLASH_BROWN_WATER,
    SPLASH_SLIME,
    SPLASH_LAVA,
    SPLASH_BLOOD,
};

// sound channels
// channel 0 never willingly overrides
// other channels (1-7) allways override a playing sound on that channel
enum {
    CHAN_AUTO,
    CHAN_WEAPON,
    CHAN_VOICE,
    CHAN_ITEM,
    CHAN_BODY,

    // modifier flags
    CHAN_NO_PHS_ADD     = BIT(3),   // send to all clients, not just ones in PHS (ATTN 0 will also do this)
    CHAN_RELIABLE       = BIT(4),   // send by reliable message, not datagram
};

// sound attenuation values
#define ATTN_LOOP_NONE          -1  // ugly hack for remaster
#define ATTN_NONE               0   // full volume the entire level
#define ATTN_NORM               1
#define ATTN_IDLE               2
#define ATTN_STATIC             3   // diminish very rapidly with distance

// player_state->stats[] indexes
enum {
    STAT_HEALTH_ICON,
    STAT_HEALTH,
    STAT_AMMO_ICON,
    STAT_AMMO,
    STAT_ARMOR_ICON,
    STAT_ARMOR,
    STAT_SELECTED_ICON,
    STAT_PICKUP_ICON,
    STAT_PICKUP_STRING,
    STAT_TIMER_ICON,
    STAT_TIMER,
    STAT_HELPICON,
    STAT_SELECTED_ITEM,
    STAT_LAYOUTS,
    STAT_FRAGS,
    STAT_FLASHES,           // cleared each frame, 1 = health, 2 = armor
    STAT_CHASE,
    STAT_SPECTATOR,

    MAX_STATS = 32
};

// STAT_LAYOUTS flags
#define LAYOUTS_LAYOUT          BIT(0)
#define LAYOUTS_INVENTORY       BIT(1)
#define LAYOUTS_HIDE_HUD        BIT(2)
#define LAYOUTS_INTERMISSION    BIT(3)
#define LAYOUTS_HELP            BIT(4)
#define LAYOUTS_HIDE_CROSSHAIR  BIT(5)

// dmflags->value flags
#define DF_NO_HEALTH        BIT(0)
#define DF_NO_ITEMS         BIT(1)
#define DF_WEAPONS_STAY     BIT(2)
#define DF_NO_FALLING       BIT(3)
#define DF_INSTANT_ITEMS    BIT(4)
#define DF_SAME_LEVEL       BIT(5)
#define DF_SKINTEAMS        BIT(6)
#define DF_MODELTEAMS       BIT(7)
#define DF_NO_FRIENDLY_FIRE BIT(8)
#define DF_SPAWN_FARTHEST   BIT(9)
#define DF_FORCE_RESPAWN    BIT(10)
#define DF_NO_ARMOR         BIT(11)
#define DF_ALLOW_EXIT       BIT(12)
#define DF_INFINITE_AMMO    BIT(13)
#define DF_QUAD_DROP        BIT(14)
#define DF_FIXED_FOV        BIT(15)

// RAFAEL
#define DF_QUADFIRE_DROP    BIT(16)

//ROGUE
#define DF_NO_MINES         BIT(17)
#define DF_NO_STACK_DOUBLE  BIT(18)
#define DF_NO_NUKES         BIT(19)
#define DF_NO_SPHERES       BIT(20)
//ROGUE

#define UF_AUTOSCREENSHOT   BIT(0)
#define UF_AUTORECORD       BIT(1)
#define UF_LOCALFOV         BIT(2)
#define UF_MUTE_PLAYERS     BIT(3)
#define UF_MUTE_OBSERVERS   BIT(4)
#define UF_MUTE_MISC        BIT(5)
#define UF_PLAYERFOV        BIT(6)

/*
==========================================================

  ELEMENTS COMMUNICATED ACROSS THE NET

==========================================================
*/

// default server FPS
#define BASE_FRAMERATE          10
#define BASE_FRAMETIME          100
#define BASE_1_FRAMETIME        0.01f   // 1/BASE_FRAMETIME
#define BASE_FRAMETIME_1000     0.1f    // BASE_FRAMETIME/1000

// maximum variable FPS factor
#define MAX_FRAMEDIV    6

#define ANGLE2SHORT(x)  ((int)((x)*65536/360) & 65535)
#define SHORT2ANGLE(x)  ((x)*(360.0f/65536))

#define COORD2SHORT(x)  ((int)((x)*8.0f))
#define SHORT2COORD(x)  ((x)*(1.0f/8))

//
// config strings are a general means of communication from
// the server to all connected clients.
// Each config string can be at most MAX_QPATH characters.
//
#define CS_NAME             0
#define CS_CDTRACK          1
#define CS_SKY              2
#define CS_SKYAXIS          3       // %f %f %f format
#define CS_SKYROTATE        4
#define CS_STATUSBAR        5       // display program string

#define CS_AIRACCEL_OLD         29      // air acceleration control
#define CS_MAXCLIENTS_OLD       30
#define CS_MAPCHECKSUM_OLD      31      // for catching cheater maps
#define CS_MODELS_OLD           32
#define CS_SOUNDS_OLD           (CS_MODELS_OLD + MAX_MODELS_OLD)
#define CS_IMAGES_OLD           (CS_SOUNDS_OLD + MAX_SOUNDS_OLD)
#define CS_LIGHTS_OLD           (CS_IMAGES_OLD + MAX_IMAGES_OLD)
#define CS_ITEMS_OLD            (CS_LIGHTS_OLD + MAX_LIGHTSTYLES)
#define CS_PLAYERSKINS_OLD      (CS_ITEMS_OLD + MAX_ITEMS)
#define CS_GENERAL_OLD          (CS_PLAYERSKINS_OLD + MAX_CLIENTS)
#define MAX_CONFIGSTRINGS_OLD   (CS_GENERAL_OLD + MAX_GENERAL)

#if USE_PROTOCOL_EXTENSIONS
#define CS_AIRACCEL         59
#define CS_MAXCLIENTS       60
#define CS_MAPCHECKSUM      61
#define CS_MODELS           62
#define CS_SOUNDS           (CS_MODELS + MAX_MODELS)
#define CS_IMAGES           (CS_SOUNDS + MAX_SOUNDS)
#define CS_LIGHTS           (CS_IMAGES + MAX_IMAGES)
#define CS_ITEMS            (CS_LIGHTS + MAX_LIGHTSTYLES)
#define CS_PLAYERSKINS      (CS_ITEMS + MAX_ITEMS)
#define CS_GENERAL          (CS_PLAYERSKINS + MAX_CLIENTS)
#define MAX_CONFIGSTRINGS   (CS_GENERAL + MAX_GENERAL)
#else
#define CS_AIRACCEL         CS_AIRACCEL_OLD
#define CS_MAXCLIENTS       CS_MAXCLIENTS_OLD
#define CS_MAPCHECKSUM      CS_MAPCHECKSUM_OLD
#define CS_MODELS           CS_MODELS_OLD
#define CS_SOUNDS           CS_SOUNDS_OLD
#define CS_IMAGES           CS_IMAGES_OLD
#define CS_LIGHTS           CS_LIGHTS_OLD
#define CS_ITEMS            CS_ITEMS_OLD
#define CS_PLAYERSKINS      CS_PLAYERSKINS_OLD
#define CS_GENERAL          CS_GENERAL_OLD
#define MAX_CONFIGSTRINGS   MAX_CONFIGSTRINGS_OLD
#endif

#if USE_PROTOCOL_EXTENSIONS

typedef struct {
    bool        extended;

    uint16_t    max_edicts;
    uint16_t    max_models;
    uint16_t    max_sounds;
    uint16_t    max_images;

    uint16_t    airaccel;
    uint16_t    maxclients;
    uint16_t    mapchecksum;

    uint16_t    models;
    uint16_t    sounds;
    uint16_t    images;
    uint16_t    lights;
    uint16_t    items;
    uint16_t    playerskins;
    uint16_t    general;

    uint16_t    end;
} cs_remap_t;

extern const cs_remap_t     cs_remap_old;
extern const cs_remap_t     cs_remap_new;

#endif

//==============================================

// entity_state_t->event values
// ertity events are for effects that take place reletive
// to an existing entities origin.  Very network efficient.
// All muzzle flashes really should be converted to events...
typedef enum {
    EV_NONE,
    EV_ITEM_RESPAWN,
    EV_FOOTSTEP,
    EV_FALLSHORT,
    EV_FALL,
    EV_FALLFAR,
    EV_PLAYER_TELEPORT,
    EV_OTHER_TELEPORT,
// KEX
    EV_OTHER_FOOTSTEP,
    EV_LADDER_STEP,
// KEX
} entity_event_t;

// entity_state_t is the information conveyed from the server
// in an update message about entities that the client will
// need to render in some way
typedef struct entity_state_s {
    int     number;         // edict index

    vec3_t  origin;
    vec3_t  angles;
    vec3_t  old_origin;     // for lerping
    int     modelindex;
    int     modelindex2, modelindex3, modelindex4;  // weapons, CTF flags, etc
    int     frame;
    int     skinnum;
    unsigned int        effects;        // PGM - we're filling it, so it needs to be unsigned
    int     renderfx;
    int     solid;          // for client side prediction, 8*(bits 0-4) is x/y radius
                            // 8*(bits 5-9) is z down distance, 8(bits10-15) is z up
                            // gi.linkentity sets this properly
    int     sound;          // for looping sounds, to guarantee shutoff
    int     event;          // impulse events -- muzzle flashes, footsteps, etc
                            // events only go out for a single frame, they
                            // are automatically cleared each frame
} entity_state_t;

//==============================================

// player_state_t is the information needed in addition to pmove_state_t
// to rendered a view.  There will only be 10 player_state_t sent each second,
// but the number of pmove_state_t changes will be reletive to client
// frame rates
typedef struct {
    pmove_state_t   pmove;      // for prediction

    // these fields do not need to be communicated bit-precise

    vec3_t      viewangles;     // for fixed views
    vec3_t      viewoffset;     // add to pmovestate->origin
    vec3_t      kick_angles;    // add to view direction to get render angles
                                // set by weapon kicks, pain effects, etc

    vec3_t      gunangles;
    vec3_t      gunoffset;
    int         gunindex;
    int         gunframe;

    float       blend[4];       // rgba full screen effect

    float       fov;            // horizontal field of view

    int         rdflags;        // refdef flags

    short       stats[MAX_STATS];       // fast status bar updates
} player_state_t;

//==============================================

#if USE_PROTOCOL_EXTENSIONS

#define ENTITYNUM_BITS      13
#define ENTITYNUM_MASK      (BIT(ENTITYNUM_BITS) - 1)

#define GUNINDEX_BITS       13  // upper 3 bits are skinnum
#define GUNINDEX_MASK       (BIT(GUNINDEX_BITS) - 1)

typedef struct {
    int         morefx;
    float       alpha;
    float       scale;
    float       loop_volume;
    float       loop_attenuation;
} entity_state_extension_t;

#endif
