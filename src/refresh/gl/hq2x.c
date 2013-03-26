/*
Copyright (C) 2013 Andrey Nazarov

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

/*
HQ2x filter
authors: byuu and blargg
license: public domain

note: this is a clean reimplementation of the original HQ2x filter, which was
written by Maxim Stepin (MaxSt). it is not 100% identical, but very similar.
*/

#include "shared/shared.h"
#include "refresh/images.h"

#define MASK_G      0x0000ff00
#define MASK_RB     0x00ff00ff
#define MASK_RGB    0x00ffffff
#define MASK_ALPHA  0xff000000

static const uint8_t hqTable[256] = {
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 15, 12, 5,  3, 17, 13,
    4, 4, 6, 18, 4, 4, 6, 18, 5,  3, 12, 12, 5,  3,  1, 12,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 17, 13, 5,  3, 16, 14,
    4, 4, 6, 18, 4, 4, 6, 18, 5,  3, 16, 12, 5,  3,  1, 14,
    4, 4, 6,  2, 4, 4, 6,  2, 5, 19, 12, 12, 5, 19, 16, 12,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 16, 12, 5,  3, 16, 12,
    4, 4, 6,  2, 4, 4, 6,  2, 5, 19,  1, 12, 5, 19,  1, 14,
    4, 4, 6,  2, 4, 4, 6, 18, 5,  3, 16, 12, 5, 19,  1, 14,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 15, 12, 5,  3, 17, 13,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 16, 12, 5,  3, 16, 12,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 17, 13, 5,  3, 16, 14,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 16, 13, 5,  3,  1, 14,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 16, 12, 5,  3, 16, 13,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 16, 12, 5,  3,  1, 12,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3, 16, 12, 5,  3,  1, 14,
    4, 4, 6,  2, 4, 4, 6,  2, 5,  3,  1, 12, 5,  3,  1, 14,
};

static uint8_t  rotTable[256];
static uint8_t  equBitmap[65536 / CHAR_BIT];

static int same(int A, int B)
{
    return Q_IsBitSet(equBitmap, (A << 8) + B);
}

static int diff(int A, int B)
{
    return !same(A, B);
}

static uint32_t generic(int A, int B, int C, int w1, int w2, int w3, int s)
{
    uint32_t a = d_8to24table[A];
    uint32_t b = d_8to24table[B];
    uint32_t c = d_8to24table[C];
    uint32_t g, rb, alpha;

    if ((A & B & C) == 255)
        return 0;

    // if transparent, scan around for another color to avoid alpha fringes
    if (A == 255) {
        if (B == 255)
            a = c & MASK_RGB;
        else
            a = b & MASK_RGB;
    }
    if (B == 255)
        b = a & MASK_RGB;
    if (C == 255)
        c = a & MASK_RGB;

    g      = ((a & MASK_G)  * w1 + (b & MASK_G)  * w2 + (c & MASK_G)  * w3) >> s;
    rb     = ((a & MASK_RB) * w1 + (b & MASK_RB) * w2 + (c & MASK_RB) * w3) >> s;
    alpha  = ((a >> 24)     * w1 + (b >> 24)     * w2 + (c >> 24)     * w3) << (24 - s);

    return (g & MASK_G) | (rb & MASK_RB) | (alpha & MASK_ALPHA);
}

static uint32_t blend0(int A)
{
    if (A == 255)
        return 0;

    return d_8to24table[A];
}

static uint32_t blend1(int A, int B)
{
    return generic(A, B, 0, 3, 1, 0, 2);
}

static uint32_t blend2(int A, int B, int C)
{
    return generic(A, B, C, 2, 1, 1, 2);
}

static uint32_t blend3(int A, int B, int C)
{
    return generic(A, B, C, 5, 2, 1, 3);
}

static uint32_t blend4(int A, int B, int C)
{
    return generic(A, B, C, 6, 1, 1, 3);
}

static uint32_t blend5(int A, int B, int C)
{
    return generic(A, B, C, 2, 3, 3, 3);
}

static uint32_t blend6(int A, int B, int C)
{
    return generic(A, B, C, 14, 1, 1, 4);
}

static uint32_t blend(int rule, int E, int A, int B, int D, int F, int H)
{
    switch (rule) {
        default:
        case  0: return blend0(E);
        case  1: return blend1(E, A);
        case  2: return blend1(E, D);
        case  3: return blend1(E, B);
        case  4: return blend2(E, D, B);
        case  5: return blend2(E, A, B);
        case  6: return blend2(E, A, D);
        case  7: return blend3(E, B, D);
        case  8: return blend3(E, D, B);
        case  9: return blend4(E, D, B);
        case 10: return blend5(E, D, B);
        case 11: return blend6(E, D, B);
        case 12: return same(B, D) ? blend2(E, D, B) : blend0(E);
        case 13: return same(B, D) ? blend5(E, D, B) : blend0(E);
        case 14: return same(B, D) ? blend6(E, D, B) : blend0(E);
        case 15: return same(B, D) ? blend2(E, D, B) : blend1(E, A);
        case 16: return same(B, D) ? blend4(E, D, B) : blend1(E, A);
        case 17: return same(B, D) ? blend5(E, D, B) : blend1(E, A);
        case 18: return same(B, F) ? blend3(E, B, D) : blend1(E, D);
        case 19: return same(D, H) ? blend3(E, D, B) : blend1(E, B);
    }
}

void HQ2x_Render(uint32_t *output, const uint8_t *input, int width, int height)
{
    int x, y;

    for (y = 0; y < height; y++) {
        const uint8_t *in = input + y * width;
        uint32_t *out0 = output + y * width * 4;
        uint32_t *out1 = output + y * width * 4 + width * 2;

        int prevline = (y == 0 ? 0 : width);
        int nextline = (y == height - 1 ? 0 : width);

        for (x = 0; x < width; x++) {
            int prev = (x == 0 ? 0 : 1);
            int next = (x == width - 1 ? 0 : 1);

            int A = *(in - prevline - prev);
            int B = *(in - prevline);
            int C = *(in - prevline + next);
            int D = *(in - prev);
            int E = *(in);
            int F = *(in + next);
            int G = *(in + nextline - prev);
            int H = *(in + nextline);
            int I = *(in + nextline + next);

            int pattern;
            pattern  = diff(E, A) << 0;
            pattern |= diff(E, B) << 1;
            pattern |= diff(E, C) << 2;
            pattern |= diff(E, D) << 3;
            pattern |= diff(E, F) << 4;
            pattern |= diff(E, G) << 5;
            pattern |= diff(E, H) << 6;
            pattern |= diff(E, I) << 7;

            *(out0 + 0) = blend(hqTable[pattern], E, A, B, D, F, H); pattern = rotTable[pattern];
            *(out0 + 1) = blend(hqTable[pattern], E, C, F, B, H, D); pattern = rotTable[pattern];
            *(out1 + 1) = blend(hqTable[pattern], E, I, H, F, D, B); pattern = rotTable[pattern];
            *(out1 + 0) = blend(hqTable[pattern], E, G, D, H, B, F);

            in++;
            out0 += 2;
            out1 += 2;
        }
    }
}

static void pix2ycc(float c[3], int C)
{
    int r = d_8to24table[C] & 255;
    int g = (d_8to24table[C] >> 8) & 255;
    int b = (d_8to24table[C] >> 16) & 255;

    c[0] = r *  0.299f + g *  0.587f + b *  0.114f;
    c[1] = r * -0.169f + g * -0.331f + b *  0.499f + 128.0f;
    c[2] = r *  0.499f + g * -0.418f + b * -0.081f + 128.0f;
}

void HQ2x_Init(void)
{
    int n;

    for (n = 0; n < 256; n++) {
        rotTable[n] = ((n >> 2) & 0x11) | ((n << 2) & 0x88)
                    | ((n & 0x01) << 5) | ((n & 0x08) << 3)
                    | ((n & 0x10) >> 3) | ((n & 0x80) >> 5);
    }

    memset(equBitmap, 0, sizeof(equBitmap));

    for (n = 0; n < 65536; n++) {
        int A = n >> 8;
        int B = n & 255;
        float a[3];
        float b[3];

        if (A == 255 && B == 255) {
            Q_SetBit(equBitmap, n);
            continue;
        }

        if (A == 255 || B == 255)
            continue;

        pix2ycc(a, A);
        pix2ycc(b, B);

        if (fabs(a[0] - b[0]) > 0x30)
            continue;
        if (fabs(a[1] - b[1]) > 0x07)
            continue;
        if (fabs(a[2] - b[2]) > 0x06)
            continue;

        Q_SetBit(equBitmap, n);
    }
}
