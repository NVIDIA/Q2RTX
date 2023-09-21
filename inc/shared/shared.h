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

#ifndef SHARED_H
#define SHARED_H

//
// shared.h -- included first by ALL program modules
//

#if HAVE_CONFIG_H
#include "config.h"
#endif

/**
*	Include based on whether the unit including is .c or .cpp
**/
#ifndef __cplusplus
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
#else//__cplusplus
#include <cctype>
#include <cinttypes>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdbool>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <type_traits>
#include <algorithm>
#include <array>
#include <string_view>
#include <numeric>
#include <functional>
#include <random>

#endif//__cplusplus


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

// WID: C++20: For C++ we use int to align with C qboolean type.
#ifdef __cplusplus

// Include our 'sharedcpp.h' header.
#include "shared_cpp.h"

#else // __cplusplus

typedef unsigned char byte;
typedef enum { qfalse, qtrue } qboolean;    // ABI compat only, don't use
typedef int qhandle_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif //__cplusplus

// WID: C++20: In case of C++ including this..
#ifdef __cplusplus
// We extern "C"
extern "C" {
#endif

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
#define MAX_EDICTS          1024    // must change protocol to increase more
#define MAX_LIGHTSTYLES     256
#define MAX_MODELS          256     // these are sent over the net as bytes
#define MAX_SOUNDS          256     // so they cannot be blindly increased
#define MAX_IMAGES          256
#define MAX_ITEMS           256
#define MAX_GENERAL         (MAX_CLIENTS * 2) // general config strings

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

typedef int fixed4_t;
typedef int fixed8_t;
typedef int fixed16_t;

#ifndef M_PI
#define M_PI        3.14159265358979323846  // matches value in gcc v2 math.h
#endif

struct cplane_s;

extern const vec3_t vec3_origin;

typedef struct vrect_s {
    int             x, y, width, height;
} vrect_t;

#define DEG2RAD(a)      ((a) * (M_PI / 180))
#define RAD2DEG(a)      ((a) * (180 / M_PI))

#define ALIGN(x, a)     (((x) + (a) - 1) & ~((a) - 1))

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

static inline unsigned npot32(unsigned k)
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

#define clamp(a,b,c)    ((a)<(b)?(a)=(b):(a)>(c)?(a)=(c):(a))
#define cclamp(a,b,c)   ((b)>(c)?clamp(a,c,b):clamp(a,b,c))

// WID: C++20:
//#ifdef __cplusplus
//#define max std::max
//#define min std::min
//#else 
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif

#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
//#endif

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

// buffer safe operations
size_t Q_strlcpy(char *dst, const char *src, size_t size);
size_t Q_strlcat(char *dst, const char *src, size_t size);

// WID: The define replacement is found in shared_cpp.h for C++
#ifndef __cplusplus
#define Q_concat(dest, size, ...) \
    Q_concat_array(dest, size, (const char *[]){__VA_ARGS__, NULL})
size_t Q_concat_array(char *dest, size_t size, const char **arr);
#endif // __cplusplus

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

#define CVAR_ARCHIVE    1   // set to cause it to be saved to vars.rc
#define CVAR_USERINFO   2   // added to userinfo  when changed
#define CVAR_SERVERINFO 4   // added to serverinfo when changed
#define CVAR_NOSET      8   // don't allow change from console at all,
                            // but can be set from the command line
#define CVAR_LATCH      16  // save changes until server restart

struct cvar_s;
struct genctx_s;

typedef void (*xchanged_t)(struct cvar_s *);
typedef void (*xgenerator_t)(struct genctx_s *);

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
    int         integer;
    char        *default_string;
    xchanged_t      changed;
    xgenerator_t    generator;
    struct cvar_s   *hashNext;
} cvar_t;

#endif      // CVAR

/*
==============================================================

COLLISION DETECTION

==============================================================
*/

// lower bits are stronger, and will eat weaker brushes completely
#define CONTENTS_SOLID          1       // an eye is never valid in a solid
#define CONTENTS_WINDOW         2       // translucent, but not watery
#define CONTENTS_AUX            4
#define CONTENTS_LAVA           8
#define CONTENTS_SLIME          16
#define CONTENTS_WATER          32
#define CONTENTS_MIST           64
#define LAST_VISIBLE_CONTENTS   64

// remaining contents are non-visible, and don't eat brushes

#define CONTENTS_AREAPORTAL     0x8000

#define CONTENTS_PLAYERCLIP     0x10000
#define CONTENTS_MONSTERCLIP    0x20000

// currents can be added to any other contents, and may be mixed
#define CONTENTS_CURRENT_0      0x40000
#define CONTENTS_CURRENT_90     0x80000
#define CONTENTS_CURRENT_180    0x100000
#define CONTENTS_CURRENT_270    0x200000
#define CONTENTS_CURRENT_UP     0x400000
#define CONTENTS_CURRENT_DOWN   0x800000

#define CONTENTS_ORIGIN         0x1000000   // removed before bsping an entity

#define CONTENTS_MONSTER        0x2000000   // should never be on a brush, only in game
#define CONTENTS_DEADMONSTER    0x4000000
#define CONTENTS_DETAIL         0x8000000   // brushes to be added after vis leafs
#define CONTENTS_TRANSLUCENT    0x10000000  // auto set if any surface has trans
#define CONTENTS_LADDER         0x20000000



#define SURF_LIGHT      0x1     // value will hold the light strength

#define SURF_SLICK      0x2     // effects game physics

#define SURF_SKY        0x4     // don't draw, but add to skybox
#define SURF_WARP       0x8     // turbulent water warp
#define SURF_TRANS33    0x10
#define SURF_TRANS66    0x20
#define SURF_FLOWING    0x40    // scroll towards angle
#define SURF_NODRAW     0x80    // don't bother referencing the texture

#define SURF_ALPHATEST  0x02000000  // used by kmquake2



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

// 3-5 are non-axial planes snapped to the nearest
#define PLANE_ANYX      3
#define PLANE_ANYY      4
#define PLANE_ANYZ      5

// planes (x&~1) and (x&~1)+1 are always opposites

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
    struct edict_s  *ent;       // not set by CM_*() functions
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
#define PMF_DUCKED          1
#define PMF_JUMP_HELD       2
#define PMF_ON_GROUND       4
#define PMF_TIME_WATERJUMP  8   // pm_time is waterjump
#define PMF_TIME_LAND       16  // pm_time is time before rejump
#define PMF_TIME_TELEPORT   32  // pm_time is non-moving time
#define PMF_NO_PREDICTION   64  // temporarily disables prediction (used for grappling hook)
#define PMF_TELEPORT_BIT    128 // used by q2pro

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
#define BUTTON_ATTACK       1
#define BUTTON_USE          2
#define BUTTON_ANY          128         // any key whatsoever


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
#define EF_ROTATE           0x00000001      // rotate (bonus items)
#define EF_GIB              0x00000002      // leave a trail
#define EF_BLASTER          0x00000008      // redlight + trail
#define EF_ROCKET           0x00000010      // redlight + trail
#define EF_GRENADE          0x00000020
#define EF_HYPERBLASTER     0x00000040
#define EF_BFG              0x00000080
#define EF_COLOR_SHELL      0x00000100
#define EF_POWERSCREEN      0x00000200
#define EF_ANIM01           0x00000400      // automatically cycle between frames 0 and 1 at 2 hz
#define EF_ANIM23           0x00000800      // automatically cycle between frames 2 and 3 at 2 hz
#define EF_ANIM_ALL         0x00001000      // automatically cycle through all frames at 2hz
#define EF_ANIM_ALLFAST     0x00002000      // automatically cycle through all frames at 10hz
#define EF_FLIES            0x00004000
#define EF_QUAD             0x00008000
#define EF_PENT             0x00010000
#define EF_TELEPORTER       0x00020000      // particle fountain
#define EF_FLAG1            0x00040000
#define EF_FLAG2            0x00080000
// RAFAEL
#define EF_IONRIPPER        0x00100000
#define EF_GREENGIB         0x00200000
#define EF_BLUEHYPERBLASTER 0x00400000
#define EF_SPINNINGLIGHTS   0x00800000
#define EF_PLASMA           0x01000000
#define EF_TRAP             0x02000000

//ROGUE
#define EF_TRACKER          0x04000000
#define EF_DOUBLE           0x08000000
#define EF_SPHERETRANS      0x10000000
#define EF_TAGTRAIL         0x20000000
#define EF_HALF_DAMAGE      0x40000000
#define EF_TRACKERTRAIL     0x80000000
//ROGUE

// entity_state_t->renderfx flags
#define RF_MINLIGHT         1       // allways have some light (viewmodel)
#define RF_VIEWERMODEL      2       // don't draw through eyes, only mirrors
#define RF_WEAPONMODEL      4       // only draw through eyes
#define RF_FULLBRIGHT       8       // allways draw full intensity
#define RF_DEPTHHACK        16      // for view weapon Z crunching
#define RF_TRANSLUCENT      32
#define RF_FRAMELERP        64
#define RF_BEAM             128
#define RF_CUSTOMSKIN       256     // skin is an index in image_precache
#define RF_GLOW             512     // pulse lighting for bonus items
#define RF_SHELL_RED        1024
#define RF_SHELL_GREEN      2048
#define RF_SHELL_BLUE       4096
#define RF_NOSHADOW         8192    // used by YQ2

//ROGUE
#define RF_IR_VISIBLE       0x00008000      // 32768
#define RF_SHELL_DOUBLE     0x00010000      // 65536
#define RF_SHELL_HALF_DAM   0x00020000
#define RF_USE_DISGUISE     0x00040000
//ROGUE

// player_state_t->refdef flags
#define RDF_UNDERWATER      1       // warp the screen as apropriate
#define RDF_NOWORLDMODEL    2       // used for player configuration screen

//ROGUE
#define RDF_IRGOGGLES       4
#define RDF_UVGOGGLES       8
//ROGUE

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

//ROGUE
    MZ_ETF_RIFLE = 30,
    MZ_UNUSED,
    MZ_SHOTGUN2,
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

    MZ_SILENCED = 128,  // bit flag ORed with one of the above numbers
};

//
// monster muzzle flashes
//
enum {
    MZ2_TANK_BLASTER_1 = 1, MZ2_TANK_BLASTER_2, MZ2_TANK_BLASTER_3,
    MZ2_TANK_MACHINEGUN_1, MZ2_TANK_MACHINEGUN_2, MZ2_TANK_MACHINEGUN_3,
    MZ2_TANK_MACHINEGUN_4, MZ2_TANK_MACHINEGUN_5, MZ2_TANK_MACHINEGUN_6,
    MZ2_TANK_MACHINEGUN_7, MZ2_TANK_MACHINEGUN_8, MZ2_TANK_MACHINEGUN_9,
    MZ2_TANK_MACHINEGUN_10, MZ2_TANK_MACHINEGUN_11, MZ2_TANK_MACHINEGUN_12,
    MZ2_TANK_MACHINEGUN_13, MZ2_TANK_MACHINEGUN_14, MZ2_TANK_MACHINEGUN_15,
    MZ2_TANK_MACHINEGUN_16, MZ2_TANK_MACHINEGUN_17, MZ2_TANK_MACHINEGUN_18,
    MZ2_TANK_MACHINEGUN_19, MZ2_TANK_ROCKET_1, MZ2_TANK_ROCKET_2,
    MZ2_TANK_ROCKET_3, MZ2_INFANTRY_MACHINEGUN_1, MZ2_INFANTRY_MACHINEGUN_2,
    MZ2_INFANTRY_MACHINEGUN_3, MZ2_INFANTRY_MACHINEGUN_4,
    MZ2_INFANTRY_MACHINEGUN_5, MZ2_INFANTRY_MACHINEGUN_6,
    MZ2_INFANTRY_MACHINEGUN_7, MZ2_INFANTRY_MACHINEGUN_8,
    MZ2_INFANTRY_MACHINEGUN_9, MZ2_INFANTRY_MACHINEGUN_10,
    MZ2_INFANTRY_MACHINEGUN_11, MZ2_INFANTRY_MACHINEGUN_12,
    MZ2_INFANTRY_MACHINEGUN_13, MZ2_SOLDIER_BLASTER_1, MZ2_SOLDIER_BLASTER_2,
    MZ2_SOLDIER_SHOTGUN_1, MZ2_SOLDIER_SHOTGUN_2, MZ2_SOLDIER_MACHINEGUN_1,
    MZ2_SOLDIER_MACHINEGUN_2, MZ2_GUNNER_MACHINEGUN_1, MZ2_GUNNER_MACHINEGUN_2,
    MZ2_GUNNER_MACHINEGUN_3, MZ2_GUNNER_MACHINEGUN_4, MZ2_GUNNER_MACHINEGUN_5,
    MZ2_GUNNER_MACHINEGUN_6, MZ2_GUNNER_MACHINEGUN_7, MZ2_GUNNER_MACHINEGUN_8,
    MZ2_GUNNER_GRENADE_1, MZ2_GUNNER_GRENADE_2, MZ2_GUNNER_GRENADE_3,
    MZ2_GUNNER_GRENADE_4, MZ2_CHICK_ROCKET_1, MZ2_FLYER_BLASTER_1,
    MZ2_FLYER_BLASTER_2, MZ2_MEDIC_BLASTER_1, MZ2_GLADIATOR_RAILGUN_1,
    MZ2_HOVER_BLASTER_1, MZ2_ACTOR_MACHINEGUN_1, MZ2_SUPERTANK_MACHINEGUN_1,
    MZ2_SUPERTANK_MACHINEGUN_2, MZ2_SUPERTANK_MACHINEGUN_3,
    MZ2_SUPERTANK_MACHINEGUN_4, MZ2_SUPERTANK_MACHINEGUN_5,
    MZ2_SUPERTANK_MACHINEGUN_6, MZ2_SUPERTANK_ROCKET_1, MZ2_SUPERTANK_ROCKET_2,
    MZ2_SUPERTANK_ROCKET_3, MZ2_BOSS2_MACHINEGUN_L1, MZ2_BOSS2_MACHINEGUN_L2,
    MZ2_BOSS2_MACHINEGUN_L3, MZ2_BOSS2_MACHINEGUN_L4, MZ2_BOSS2_MACHINEGUN_L5,
    MZ2_BOSS2_ROCKET_1, MZ2_BOSS2_ROCKET_2, MZ2_BOSS2_ROCKET_3,
    MZ2_BOSS2_ROCKET_4, MZ2_FLOAT_BLASTER_1, MZ2_SOLDIER_BLASTER_3,
    MZ2_SOLDIER_SHOTGUN_3, MZ2_SOLDIER_MACHINEGUN_3, MZ2_SOLDIER_BLASTER_4,
    MZ2_SOLDIER_SHOTGUN_4, MZ2_SOLDIER_MACHINEGUN_4, MZ2_SOLDIER_BLASTER_5,
    MZ2_SOLDIER_SHOTGUN_5, MZ2_SOLDIER_MACHINEGUN_5, MZ2_SOLDIER_BLASTER_6,
    MZ2_SOLDIER_SHOTGUN_6, MZ2_SOLDIER_MACHINEGUN_6, MZ2_SOLDIER_BLASTER_7,
    MZ2_SOLDIER_SHOTGUN_7, MZ2_SOLDIER_MACHINEGUN_7, MZ2_SOLDIER_BLASTER_8,
    MZ2_SOLDIER_SHOTGUN_8, MZ2_SOLDIER_MACHINEGUN_8,

// --- Xian shit below ---
    MZ2_MAKRON_BFG, MZ2_MAKRON_BLASTER_1, MZ2_MAKRON_BLASTER_2,
    MZ2_MAKRON_BLASTER_3, MZ2_MAKRON_BLASTER_4, MZ2_MAKRON_BLASTER_5,
    MZ2_MAKRON_BLASTER_6, MZ2_MAKRON_BLASTER_7, MZ2_MAKRON_BLASTER_8,
    MZ2_MAKRON_BLASTER_9, MZ2_MAKRON_BLASTER_10, MZ2_MAKRON_BLASTER_11,
    MZ2_MAKRON_BLASTER_12, MZ2_MAKRON_BLASTER_13, MZ2_MAKRON_BLASTER_14,
    MZ2_MAKRON_BLASTER_15, MZ2_MAKRON_BLASTER_16, MZ2_MAKRON_BLASTER_17,
    MZ2_MAKRON_RAILGUN_1, MZ2_JORG_MACHINEGUN_L1, MZ2_JORG_MACHINEGUN_L2,
    MZ2_JORG_MACHINEGUN_L3, MZ2_JORG_MACHINEGUN_L4, MZ2_JORG_MACHINEGUN_L5,
    MZ2_JORG_MACHINEGUN_L6, MZ2_JORG_MACHINEGUN_R1, MZ2_JORG_MACHINEGUN_R2,
    MZ2_JORG_MACHINEGUN_R3, MZ2_JORG_MACHINEGUN_R4, MZ2_JORG_MACHINEGUN_R5,
    MZ2_JORG_MACHINEGUN_R6, MZ2_JORG_BFG_1, MZ2_BOSS2_MACHINEGUN_R1,
    MZ2_BOSS2_MACHINEGUN_R2, MZ2_BOSS2_MACHINEGUN_R3, MZ2_BOSS2_MACHINEGUN_R4,
    MZ2_BOSS2_MACHINEGUN_R5,

//ROGUE
    MZ2_CARRIER_MACHINEGUN_L1, MZ2_CARRIER_MACHINEGUN_R1, MZ2_CARRIER_GRENADE,
    MZ2_TURRET_MACHINEGUN, MZ2_TURRET_ROCKET, MZ2_TURRET_BLASTER,
    MZ2_STALKER_BLASTER, MZ2_DAEDALUS_BLASTER, MZ2_MEDIC_BLASTER_2,
    MZ2_CARRIER_RAILGUN, MZ2_WIDOW_DISRUPTOR, MZ2_WIDOW_BLASTER,
    MZ2_WIDOW_RAIL, MZ2_WIDOW_PLASMABEAM, MZ2_CARRIER_MACHINEGUN_L2,
    MZ2_CARRIER_MACHINEGUN_R2, MZ2_WIDOW_RAIL_LEFT, MZ2_WIDOW_RAIL_RIGHT,
    MZ2_WIDOW_BLASTER_SWEEP1, MZ2_WIDOW_BLASTER_SWEEP2,
    MZ2_WIDOW_BLASTER_SWEEP3, MZ2_WIDOW_BLASTER_SWEEP4,
    MZ2_WIDOW_BLASTER_SWEEP5, MZ2_WIDOW_BLASTER_SWEEP6,
    MZ2_WIDOW_BLASTER_SWEEP7, MZ2_WIDOW_BLASTER_SWEEP8,
    MZ2_WIDOW_BLASTER_SWEEP9, MZ2_WIDOW_BLASTER_100, MZ2_WIDOW_BLASTER_90,
    MZ2_WIDOW_BLASTER_80, MZ2_WIDOW_BLASTER_70, MZ2_WIDOW_BLASTER_60,
    MZ2_WIDOW_BLASTER_50, MZ2_WIDOW_BLASTER_40, MZ2_WIDOW_BLASTER_30,
    MZ2_WIDOW_BLASTER_20, MZ2_WIDOW_BLASTER_10, MZ2_WIDOW_BLASTER_0,
    MZ2_WIDOW_BLASTER_10L, MZ2_WIDOW_BLASTER_20L, MZ2_WIDOW_BLASTER_30L,
    MZ2_WIDOW_BLASTER_40L, MZ2_WIDOW_BLASTER_50L, MZ2_WIDOW_BLASTER_60L,
    MZ2_WIDOW_BLASTER_70L, MZ2_WIDOW_RUN_1, MZ2_WIDOW_RUN_2, MZ2_WIDOW_RUN_3,
    MZ2_WIDOW_RUN_4, MZ2_WIDOW_RUN_5, MZ2_WIDOW_RUN_6, MZ2_WIDOW_RUN_7,
    MZ2_WIDOW_RUN_8, MZ2_CARRIER_ROCKET_1, MZ2_CARRIER_ROCKET_2,
    MZ2_CARRIER_ROCKET_3, MZ2_CARRIER_ROCKET_4, MZ2_WIDOW2_BEAMER_1,
    MZ2_WIDOW2_BEAMER_2, MZ2_WIDOW2_BEAMER_3, MZ2_WIDOW2_BEAMER_4,
    MZ2_WIDOW2_BEAMER_5, MZ2_WIDOW2_BEAM_SWEEP_1, MZ2_WIDOW2_BEAM_SWEEP_2,
    MZ2_WIDOW2_BEAM_SWEEP_3, MZ2_WIDOW2_BEAM_SWEEP_4, MZ2_WIDOW2_BEAM_SWEEP_5,
    MZ2_WIDOW2_BEAM_SWEEP_6, MZ2_WIDOW2_BEAM_SWEEP_7, MZ2_WIDOW2_BEAM_SWEEP_8,
    MZ2_WIDOW2_BEAM_SWEEP_9, MZ2_WIDOW2_BEAM_SWEEP_10,
    MZ2_WIDOW2_BEAM_SWEEP_11,
//ROGUE
};

extern const vec3_t monster_flash_offset[256];


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
// Q2RTX
	TE_FLARE,
// Q2RTX

    TE_NUM_ENTITIES
} temp_event_t;

#define SPLASH_UNKNOWN      0
#define SPLASH_SPARKS       1
#define SPLASH_BLUE_WATER   2
#define SPLASH_BROWN_WATER  3
#define SPLASH_SLIME        4
#define SPLASH_LAVA         5
#define SPLASH_BLOOD        6


// sound channels
// channel 0 never willingly overrides
// other channels (1-7) allways override a playing sound on that channel
#define CHAN_AUTO               0
#define CHAN_WEAPON             1
#define CHAN_VOICE              2
#define CHAN_ITEM               3
#define CHAN_BODY               4
// modifier flags
#define CHAN_NO_PHS_ADD         8   // send to all clients, not just ones in PHS (ATTN 0 will also do this)
#define CHAN_RELIABLE           16  // send by reliable message, not datagram


// sound attenuation values
#define ATTN_NONE               0   // full volume the entire level
#define ATTN_NORM               1
#define ATTN_IDLE               2
#define ATTN_STATIC             3   // diminish very rapidly with distance


// player_state->stats[] indexes
#define STAT_HEALTH_ICON        0
#define STAT_HEALTH             1
#define STAT_AMMO_ICON          2
#define STAT_AMMO               3
#define STAT_ARMOR_ICON         4
#define STAT_ARMOR              5
#define STAT_SELECTED_ICON      6
#define STAT_PICKUP_ICON        7
#define STAT_PICKUP_STRING      8
#define STAT_TIMER_ICON         9
#define STAT_TIMER              10
#define STAT_HELPICON           11
#define STAT_SELECTED_ITEM      12
#define STAT_LAYOUTS            13
#define STAT_FRAGS              14
#define STAT_FLASHES            15      // cleared each frame, 1 = health, 2 = armor
#define STAT_CHASE              16
#define STAT_SPECTATOR          17

#define MAX_STATS               32


// dmflags->value flags
#define DF_NO_HEALTH        0x00000001  // 1
#define DF_NO_ITEMS         0x00000002  // 2
#define DF_WEAPONS_STAY     0x00000004  // 4
#define DF_NO_FALLING       0x00000008  // 8
#define DF_INSTANT_ITEMS    0x00000010  // 16
#define DF_SAME_LEVEL       0x00000020  // 32
#define DF_SKINTEAMS        0x00000040  // 64
#define DF_MODELTEAMS       0x00000080  // 128
#define DF_NO_FRIENDLY_FIRE 0x00000100  // 256
#define DF_SPAWN_FARTHEST   0x00000200  // 512
#define DF_FORCE_RESPAWN    0x00000400  // 1024
#define DF_NO_ARMOR         0x00000800  // 2048
#define DF_ALLOW_EXIT       0x00001000  // 4096
#define DF_INFINITE_AMMO    0x00002000  // 8192
#define DF_QUAD_DROP        0x00004000  // 16384
#define DF_FIXED_FOV        0x00008000  // 32768

// RAFAEL
#define DF_QUADFIRE_DROP    0x00010000  // 65536

//ROGUE
#define DF_NO_MINES         0x00020000
#define DF_NO_STACK_DOUBLE  0x00040000
#define DF_NO_NUKES         0x00080000
#define DF_NO_SPHERES       0x00100000
//ROGUE


#define UF_AUTOSCREENSHOT   1
#define UF_AUTORECORD       2
#define UF_LOCALFOV         4
#define UF_MUTE_PLAYERS     8
#define UF_MUTE_OBSERVERS   16
#define UF_MUTE_MISC        32
#define UF_PLAYERFOV        64

/*
==========================================================

  ELEMENTS COMMUNICATED ACROSS THE NET

==========================================================
*/

// Default server FPS: 10hz
//#define BASE_FRAMERATE          10
//#define BASE_FRAMETIME          100
//#define BASE_1_FRAMETIME        0.01f   // 1/BASE_FRAMETIME
//#define BASE_FRAMETIME_1000     0.1f    // BASE_FRAMETIME/1000

// Default Server FPS: 40hz
#define BASE_FRAMERATE          40	// OLD: 10
#define BASE_FRAMETIME          25	// NEW 1000 / 40 = 25 OLD: 1000 / 10 = 100
#define BASE_1_FRAMETIME        0.04f   // OLD: 0.01f 1/BASE_FRAMETIME
#define BASE_FRAMETIME_1000     0.025f    // OLD 0.1f BASE_FRAMETIME/1000

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

#define CS_AIRACCEL         29      // air acceleration control
#define CS_MAXCLIENTS       30
#define CS_MAPCHECKSUM      31      // for catching cheater maps

#define CS_MODELS           32
#define CS_SOUNDS           (CS_MODELS+MAX_MODELS)
#define CS_IMAGES           (CS_SOUNDS+MAX_SOUNDS)
#define CS_LIGHTS           (CS_IMAGES+MAX_IMAGES)
#define CS_ITEMS            (CS_LIGHTS+MAX_LIGHTSTYLES)
#define CS_PLAYERSKINS      (CS_ITEMS+MAX_ITEMS)
#define CS_GENERAL          (CS_PLAYERSKINS+MAX_CLIENTS)
#define MAX_CONFIGSTRINGS   (CS_GENERAL+MAX_GENERAL)

// Some mods actually exploit CS_STATUSBAR to take space up to CS_AIRACCEL
#define CS_SIZE(cs) \
    ((cs) >= CS_STATUSBAR && (cs) < CS_AIRACCEL ? \
      MAX_QPATH * (CS_AIRACCEL - (cs)) : MAX_QPATH)


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
    EV_OTHER_TELEPORT
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
							
	// [Paril-KEX] for custom interpolation stuff
	int32_t        old_frame;
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

// WID: C++20: In case of C++ including this..
#ifdef __cplusplus
// We extern "C"
};
#endif

#endif // SHARED_H
