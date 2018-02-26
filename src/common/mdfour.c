/*
Copyright (C) 1997-1998  Andrew Tridgell

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
#include "common/mdfour.h"

#if SIZEOF_INT > 4
#define LARGE_INT32
#endif


/* NOTE: This code makes no attempt to be fast!

   It assumes that a int is at least 32 bits long
*/

static struct mdfour *m;

#define F(X,Y,Z) (((X)&(Y)) | ((~(X))&(Z)))
#define G(X,Y,Z) (((X)&(Y)) | ((X)&(Z)) | ((Y)&(Z)))
#define H(X,Y,Z) ((X)^(Y)^(Z))
#ifdef LARGE_INT32
#define lshift(x,s) ((((x)<<(s))&0xFFFFFFFF) | (((x)>>(32-(s)))&0xFFFFFFFF))
#else
#define lshift(x,s) (((x)<<(s)) | ((x)>>(32-(s))))
#endif

#define ROUND1(a,b,c,d,k,s) a = lshift(a + F(b,c,d) + X[k], s)
#define ROUND2(a,b,c,d,k,s) a = lshift(a + G(b,c,d) + X[k] + 0x5A827999,s)
#define ROUND3(a,b,c,d,k,s) a = lshift(a + H(b,c,d) + X[k] + 0x6ED9EBA1,s)

/* this applies md4 to 64 byte chunks */
static void mdfour64(uint32_t *M)
{
    int j;
    uint32_t AA, BB, CC, DD;
    uint32_t X[16];
    uint32_t A, B, C, D;

    for (j = 0; j < 16; j++)
        X[j] = M[j];

    A = m->A; B = m->B; C = m->C; D = m->D;
    AA = A; BB = B; CC = C; DD = D;

    ROUND1(A, B, C, D,  0,  3);  ROUND1(D, A, B, C,  1,  7);
    ROUND1(C, D, A, B,  2, 11);  ROUND1(B, C, D, A,  3, 19);
    ROUND1(A, B, C, D,  4,  3);  ROUND1(D, A, B, C,  5,  7);
    ROUND1(C, D, A, B,  6, 11);  ROUND1(B, C, D, A,  7, 19);
    ROUND1(A, B, C, D,  8,  3);  ROUND1(D, A, B, C,  9,  7);
    ROUND1(C, D, A, B, 10, 11);  ROUND1(B, C, D, A, 11, 19);
    ROUND1(A, B, C, D, 12,  3);  ROUND1(D, A, B, C, 13,  7);
    ROUND1(C, D, A, B, 14, 11);  ROUND1(B, C, D, A, 15, 19);

    ROUND2(A, B, C, D,  0,  3);  ROUND2(D, A, B, C,  4,  5);
    ROUND2(C, D, A, B,  8,  9);  ROUND2(B, C, D, A, 12, 13);
    ROUND2(A, B, C, D,  1,  3);  ROUND2(D, A, B, C,  5,  5);
    ROUND2(C, D, A, B,  9,  9);  ROUND2(B, C, D, A, 13, 13);
    ROUND2(A, B, C, D,  2,  3);  ROUND2(D, A, B, C,  6,  5);
    ROUND2(C, D, A, B, 10,  9);  ROUND2(B, C, D, A, 14, 13);
    ROUND2(A, B, C, D,  3,  3);  ROUND2(D, A, B, C,  7,  5);
    ROUND2(C, D, A, B, 11,  9);  ROUND2(B, C, D, A, 15, 13);

    ROUND3(A, B, C, D,  0,  3);  ROUND3(D, A, B, C,  8,  9);
    ROUND3(C, D, A, B,  4, 11);  ROUND3(B, C, D, A, 12, 15);
    ROUND3(A, B, C, D,  2,  3);  ROUND3(D, A, B, C, 10,  9);
    ROUND3(C, D, A, B,  6, 11);  ROUND3(B, C, D, A, 14, 15);
    ROUND3(A, B, C, D,  1,  3);  ROUND3(D, A, B, C,  9,  9);
    ROUND3(C, D, A, B,  5, 11);  ROUND3(B, C, D, A, 13, 15);
    ROUND3(A, B, C, D,  3,  3);  ROUND3(D, A, B, C, 11,  9);
    ROUND3(C, D, A, B,  7, 11);  ROUND3(B, C, D, A, 15, 15);

    A += AA; B += BB; C += CC; D += DD;

#ifdef LARGE_INT32
    A &= 0xFFFFFFFF; B &= 0xFFFFFFFF;
    C &= 0xFFFFFFFF; D &= 0xFFFFFFFF;
#endif

    for (j = 0; j < 16; j++)
        X[j] = 0;

    m->A = A; m->B = B; m->C = C; m->D = D;
}

static void copy64(uint32_t *M, uint8_t *in)
{
    int i;

    for (i = 0; i < 16; i++, in += 4)
        M[i] = LittleLongMem(in);
}

static void copy4(uint8_t *out, uint32_t x)
{
    out[0] = x & 0xFF;
    out[1] = (x >> 8) & 0xFF;
    out[2] = (x >> 16) & 0xFF;
    out[3] = (x >> 24) & 0xFF;
}

void mdfour_begin(struct mdfour *md)
{
    md->A = 0x67452301;
    md->B = 0xefcdab89;
    md->C = 0x98badcfe;
    md->D = 0x10325476;
    md->totalN = 0;
}


static void mdfour_tail(uint8_t *in, size_t n)
{
    uint8_t buf[128];
    uint32_t M[16];
    uint32_t b;

    m->totalN += n;

    b = m->totalN * 8;

    memset(buf, 0, 128);
    if (n) memcpy(buf, in, n);
    buf[n] = 0x80;

    if (n <= 55) {
        copy4(buf + 56, b);
        copy64(M, buf);
        mdfour64(M);
    } else {
        copy4(buf + 120, b);
        copy64(M, buf);
        mdfour64(M);
        copy64(M, buf + 64);
        mdfour64(M);
    }
}

void mdfour_update(struct mdfour *md, uint8_t *in, size_t n)
{
    uint32_t M[16];

    m = md;

    if (n == 0) mdfour_tail(in, n);

    while (n >= 64) {
        copy64(M, in);
        mdfour64(M);
        in += 64;
        n -= 64;
        m->totalN += 64;
    }

    mdfour_tail(in, n);
}


void mdfour_result(struct mdfour *md, uint8_t *out)
{
    m = md;

    copy4(out, m->A);
    copy4(out + 4, m->B);
    copy4(out + 8, m->C);
    copy4(out + 12, m->D);
}

//===================================================================

uint32_t Com_BlockChecksum(void *buffer, size_t len)
{
    uint32_t digest[4];
    uint32_t val;
    struct mdfour md;

    mdfour_begin(&md);
    mdfour_update(&md, (uint8_t *)buffer, len);
    mdfour_result(&md, (uint8_t *)digest);

    val = digest[0] ^ digest[1] ^ digest[2] ^ digest[3];

    return val;
}

