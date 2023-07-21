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
#include "common/intreadwrite.h"

/* NOTE: This code makes no attempt to be fast!

   It assumes that a int is at least 32 bits long
*/

#define F(X,Y,Z) (((X)&(Y)) | ((~(X))&(Z)))
#define G(X,Y,Z) (((X)&(Y)) | ((X)&(Z)) | ((Y)&(Z)))
#define H(X,Y,Z) ((X)^(Y)^(Z))
#define lshift(x,s) (((x)<<(s)) | ((x)>>(32-(s))))

#define ROUND1(a,b,c,d,k,s) a = lshift(a + F(b,c,d) + M[k], s)
#define ROUND2(a,b,c,d,k,s) a = lshift(a + G(b,c,d) + M[k] + 0x5A827999, s)
#define ROUND3(a,b,c,d,k,s) a = lshift(a + H(b,c,d) + M[k] + 0x6ED9EBA1, s)

/* this applies md4 to 64 byte chunks */
static void mdfour64(struct mdfour *md, const uint32_t *M)
{
    uint32_t A, B, C, D;

    A = md->A; B = md->B; C = md->C; D = md->D;

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

    md->A += A; md->B += B; md->C += C; md->D += D;
}

static void copy64(uint32_t *M, const uint8_t *in)
{
    int i;

    for (i = 0; i < 16; i++, in += 4)
        M[i] = RL32(in);
}

static void copy4(uint8_t *out, uint32_t x)
{
    WL32(out, x);
}

void mdfour_begin(struct mdfour *md)
{
    md->A = 0x67452301;
    md->B = 0xefcdab89;
    md->C = 0x98badcfe;
    md->D = 0x10325476;
    md->count = 0;
}

static void mdfour_tail(struct mdfour *md)
{
    uint8_t buf[128];
    uint32_t M[16];
    uint32_t b = md->count * 8;
    uint32_t n = md->count & 63;

    memset(buf, 0, 128);
    memcpy(buf, md->block, n);
    buf[n] = 0x80;

    if (n <= 55) {
        copy4(buf + 56, b);
        copy64(M, buf);
        mdfour64(md, M);
    } else {
        copy4(buf + 120, b);
        copy64(M, buf);
        mdfour64(md, M);
        copy64(M, buf + 64);
        mdfour64(md, M);
    }
}

void mdfour_update(struct mdfour *md, const uint8_t *in, size_t n)
{
    uint32_t M[16];
    uint32_t index = md->count & 63;
    uint32_t avail = 64 - index;

    md->count += n;

    if (n < avail) {
        memcpy(md->block + index, in, n);
        return;
    }

    if (index) {
        memcpy(md->block + index, in, avail);
        copy64(M, md->block);
        mdfour64(md, M);
        in += avail;
        n -= avail;
    }

    while (n >= 64) {
        copy64(M, in);
        mdfour64(md, M);
        in += 64;
        n -= 64;
    }

    memcpy(md->block, in, n);
}

void mdfour_result(struct mdfour *md, uint8_t *out)
{
    mdfour_tail(md);

    copy4(out, md->A);
    copy4(out + 4, md->B);
    copy4(out + 8, md->C);
    copy4(out + 12, md->D);
}

uint32_t Com_BlockChecksum(const void *buffer, size_t len)
{
    struct mdfour md;

    mdfour_begin(&md);
    mdfour_update(&md, buffer, len);
    mdfour_tail(&md);

    return md.A ^ md.B ^ md.C ^ md.D;
}

