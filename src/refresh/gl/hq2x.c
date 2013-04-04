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
#include "common/cvar.h"
#include "refresh/images.h"

static const uint8_t hqTable[256] = {
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  7,  8, 3,  5, 13, 15,
    1, 1, 2, 10, 1, 1, 2, 10, 3,  5,  8,  8, 3,  5,  6,  8,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5, 12, 14, 3,  5,  9, 16,
    1, 1, 2, 10, 1, 1, 2, 10, 3,  5,  9,  8, 3,  5,  6, 16,
    1, 1, 2,  4, 1, 1, 2,  4, 3, 11,  8,  8, 3, 11,  9,  8,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  9,  8, 3,  5,  9,  8,
    1, 1, 2,  4, 1, 1, 2,  4, 3, 11,  6,  8, 3, 11,  6, 16,
    1, 1, 2,  4, 1, 1, 2, 10, 3,  5,  9,  8, 3, 11,  6, 16,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  7,  8, 3,  5, 13, 15,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  9,  8, 3,  5,  9,  8,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5, 12, 14, 3,  5,  9, 16,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  9, 14, 3,  5,  6, 16,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  9,  8, 3,  5,  9, 15,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  9,  8, 3,  5,  6,  8,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  9,  8, 3,  5,  6, 16,
    1, 1, 2,  4, 1, 1, 2,  4, 3,  5,  6,  8, 3,  5,  6, 16,
};

static uint8_t  rotTable[256];
static uint8_t  equBitmap[65536 / CHAR_BIT];

static inline int same(int A, int B)
{
    return Q_IsBitSet(equBitmap, (A << 8) + B);
}

static inline int diff(int A, int B)
{
    return !same(A, B);
}

static inline uint64_t grow(uint64_t n)
{
    n |= n << 24;
    n &= UINT64_C(0xff00ff00ff00ff);
    return n;
}

static inline uint32_t pack(uint64_t n)
{
    n &= UINT64_C(0xff00ff00ff00ff);
    n |= n >> 24;
    return n;
}

static inline uint32_t generic(int A, int B, int C, int w1, int w2, int w3, int s)
{
    uint32_t a = d_8to24table[A];
    uint32_t b = d_8to24table[B];
    uint32_t c = d_8to24table[C];

    // if transparent, scan around for another color to avoid alpha fringes
    if (A == 255) {
        if (B == 255)
            a = c & U32_RGB;
        else
            a = b & U32_RGB;
    }
    if (B == 255)
        b = a & U32_RGB;
    if (C == 255)
        c = a & U32_RGB;

    return pack((grow(a) * w1 + grow(b) * w2 + grow(c) * w3) >> s);
}

static uint32_t blend_1(int A)
{
    return d_8to24table[A];
}

static uint32_t blend_1_1(int A, int B)
{
    return generic(A, B, 0, 1, 1, 0, 1);
}

static uint32_t blend_3_1(int A, int B)
{
    return generic(A, B, 0, 3, 1, 0, 2);
}

static uint32_t blend_7_1(int A, int B)
{
    return generic(A, B, 0, 7, 1, 0, 3);
}

static uint32_t blend_5_3(int A, int B)
{
    return generic(A, B, 0, 5, 3, 0, 3);
}

static uint32_t blend_2_1_1(int A, int B, int C)
{
    return generic(A, B, C, 2, 1, 1, 2);
}

static uint32_t blend_5_2_1(int A, int B, int C)
{
    return generic(A, B, C, 5, 2, 1, 3);
}

static uint32_t blend_6_1_1(int A, int B, int C)
{
    return generic(A, B, C, 6, 1, 1, 3);
}

static uint32_t blend_2_3_3(int A, int B, int C)
{
    return generic(A, B, C, 2, 3, 3, 3);
}

static uint32_t blend_14_1_1(int A, int B, int C)
{
    return generic(A, B, C, 14, 1, 1, 4);
}

static uint32_t hq2x_blend(int rule, int E, int A, int B, int D, int F, int H)
{
    switch (rule) {
    case 1:
        return blend_2_1_1(E, D, B);
    case 2:
        return blend_2_1_1(E, A, D);
    case 3:
        return blend_2_1_1(E, A, B);
    case 4:
        return blend_3_1(E, D);
    case 5:
        return blend_3_1(E, B);
    case 6:
        return blend_3_1(E, A);
    case 7:
        return same(B, D) ? blend_2_1_1(E, D, B) : blend_3_1(E, A);
    case 8:
        return same(B, D) ? blend_2_1_1(E, D, B) : blend_1(E);
    case 9:
        return same(B, D) ? blend_6_1_1(E, D, B) : blend_3_1(E, A);
    case 10:
        return same(B, F) ? blend_5_2_1(E, B, D) : blend_3_1(E, D);
    case 11:
        return same(D, H) ? blend_5_2_1(E, D, B) : blend_3_1(E, B);
    case 12:
    case 13:
        return same(B, D) ? blend_2_3_3(E, D, B) : blend_3_1(E, A);
    case 14:
    case 15:
        return same(B, D) ? blend_2_3_3(E, D, B) : blend_1(E);
    case 16:
        return same(B, D) ? blend_14_1_1(E, D, B) : blend_1(E);
    default:
        Com_Error(ERR_FATAL, "%s: bad rule %d", __func__, rule);
        return 0;
    }
}

static void hq4x_blend(int rule, uint32_t *p00, uint32_t *p01, uint32_t *p10, uint32_t *p11, int E, int A, int B, int D, int F, int H)
{
    switch (rule) {
    case 1:
        *p00 = blend_2_1_1(E, B, D);
        *p01 = blend_5_2_1(E, B, D);
        *p10 = blend_5_2_1(E, D, B);
        *p11 = blend_6_1_1(E, D, B);
        break;
    case 2:
        *p00 = blend_5_3(E, A);
        *p01 = blend_3_1(E, A);
        *p10 = blend_5_2_1(E, D, A);
        *p11 = blend_7_1(E, A);
        break;
    case 3:
        *p00 = blend_5_3(E, A);
        *p01 = blend_5_2_1(E, B, A);
        *p10 = blend_3_1(E, A);
        *p11 = blend_7_1(E, A);
        break;
    case 4:
        *p00 = blend_5_3(E, D);
        *p01 = blend_7_1(E, D);
        *p10 = blend_5_3(E, D);
        *p11 = blend_7_1(E, D);
        break;
    case 5:
        *p00 = blend_5_3(E, B);
        *p01 = blend_5_3(E, B);
        *p10 = blend_7_1(E, B);
        *p11 = blend_7_1(E, B);
        break;
    case 6:
        *p00 = blend_5_3(E, A);
        *p01 = blend_3_1(E, A);
        *p10 = blend_3_1(E, A);
        *p11 = blend_7_1(E, A);
        break;
    case 7:
        if (same(B, D)) {
            *p00 = blend_1_1(B, D);
            *p01 = blend_1_1(B, E);
            *p10 = blend_1_1(D, E);
            *p11 = blend_1(E);
        } else {
            *p00 = blend_5_3(E, A);
            *p01 = blend_3_1(E, A);
            *p10 = blend_3_1(E, A);
            *p11 = blend_7_1(E, A);
        }
        break;
    case 8:
        if (same(B, D)) {
            *p00 = blend_1_1(B, D);
            *p01 = blend_1_1(B, E);
            *p10 = blend_1_1(D, E);
        } else {
            *p00 = blend_1(E);
            *p01 = blend_1(E);
            *p10 = blend_1(E);
        }
        *p11 = blend_1(E);
        break;
    case 9:
        if (same(B, D)) {
            *p00 = blend_2_1_1(E, B, D);
            *p01 = blend_3_1(E, B);
            *p10 = blend_3_1(E, D);
            *p11 = blend_1(E);
        } else {
            *p00 = blend_5_3(E, A);
            *p01 = blend_3_1(E, A);
            *p10 = blend_3_1(E, A);
            *p11 = blend_7_1(E, A);
        }
        break;
    case 10:
        if (same(B, F)) {
            *p00 = blend_3_1(E, B);
            *p01 = blend_3_1(B, E);
        } else {
            *p00 = blend_5_3(E, D);
            *p01 = blend_7_1(E, D);
        }
        *p10 = blend_5_3(E, D);
        *p11 = blend_7_1(E, D);
        break;
    case 11:
        if (same(D, H)) {
            *p00 = blend_3_1(E, D);
            *p10 = blend_3_1(D, E);
        } else {
            *p00 = blend_5_3(E, B);
            *p10 = blend_7_1(E, B);
        }
        *p01 = blend_5_3(E, B);
        *p11 = blend_7_1(E, B);
        break;
    case 12:
        if (same(B, D)) {
            *p00 = blend_1_1(B, D);
            *p01 = blend_2_1_1(B, E, D);
            *p10 = blend_5_3(D, B);
            *p11 = blend_6_1_1(E, D, B);
        } else {
            *p00 = blend_5_3(E, A);
            *p01 = blend_3_1(E, A);
            *p10 = blend_3_1(E, A);
            *p11 = blend_7_1(E, A);
        }
        break;
    case 13:
        if (same(B, D)) {
            *p00 = blend_1_1(B, D);
            *p01 = blend_5_3(B, D);
            *p10 = blend_2_1_1(D, E, B);
            *p11 = blend_6_1_1(E, D, B);
        } else {
            *p00 = blend_5_3(E, A);
            *p01 = blend_3_1(E, A);
            *p10 = blend_3_1(E, A);
            *p11 = blend_7_1(E, A);
        }
        break;
    case 14:
        if (same(B, D)) {
            *p00 = blend_1_1(B, D);
            *p01 = blend_2_1_1(B, E, D);
            *p10 = blend_5_3(D, B);
            *p11 = blend_6_1_1(E, D, B);
        } else {
            *p00 = blend_1(E);
            *p01 = blend_1(E);
            *p10 = blend_1(E);
            *p11 = blend_1(E);
        }
        break;
    case 15:
        if (same(B, D)) {
            *p00 = blend_1_1(B, D);
            *p01 = blend_5_3(B, D);
            *p10 = blend_2_1_1(D, E, B);
            *p11 = blend_6_1_1(E, D, B);
        } else {
            *p00 = blend_1(E);
            *p01 = blend_1(E);
            *p10 = blend_1(E);
            *p11 = blend_1(E);
        }
        break;
    case 16:
        if (same(B, D))
            *p00 = blend_2_1_1(E, B, D);
        else
            *p00 = blend_1(E);
        *p01 = blend_1(E);
        *p10 = blend_1(E);
        *p11 = blend_1(E);
        break;
    default:
        Com_Error(ERR_FATAL, "%s: bad rule %d", __func__, rule);
        break;
    }
}

void HQ2x_Render(uint32_t *output, const uint8_t *input, int width, int height)
{
    int x, y;

    for (y = 0; y < height; y++) {
        const uint8_t *in = input + y * width;
        uint32_t *out0 = output + (y * 2 + 0) * width * 2;
        uint32_t *out1 = output + (y * 2 + 1) * width * 2;

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

            *(out0 + 0) = hq2x_blend(hqTable[pattern], E, A, B, D, F, H); pattern = rotTable[pattern];
            *(out0 + 1) = hq2x_blend(hqTable[pattern], E, C, F, B, H, D); pattern = rotTable[pattern];
            *(out1 + 1) = hq2x_blend(hqTable[pattern], E, I, H, F, D, B); pattern = rotTable[pattern];
            *(out1 + 0) = hq2x_blend(hqTable[pattern], E, G, D, H, B, F);

            in++;
            out0 += 2;
            out1 += 2;
        }
    }
}

void HQ4x_Render(uint32_t *output, const uint8_t *input, int width, int height)
{
    int x, y;

    for (y = 0; y < height; y++) {
        const uint8_t *in = input + y * width;
        uint32_t *out0 = output + (y * 4 + 0) * width * 4;
        uint32_t *out1 = output + (y * 4 + 1) * width * 4;
        uint32_t *out2 = output + (y * 4 + 2) * width * 4;
        uint32_t *out3 = output + (y * 4 + 3) * width * 4;

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

            hq4x_blend(hqTable[pattern], out0 + 0, out0 + 1, out1 + 0, out1 + 1, E, A, B, D, F, H); pattern = rotTable[pattern];
            hq4x_blend(hqTable[pattern], out0 + 3, out1 + 3, out0 + 2, out1 + 2, E, C, F, B, H, D); pattern = rotTable[pattern];
            hq4x_blend(hqTable[pattern], out3 + 3, out3 + 2, out2 + 3, out2 + 2, E, I, H, F, D, B); pattern = rotTable[pattern];
            hq4x_blend(hqTable[pattern], out3 + 0, out2 + 0, out3 + 1, out2 + 1, E, G, D, H, B, F);

            in++;
            out0 += 4;
            out1 += 4;
            out2 += 4;
            out3 += 4;
        }
    }
}

static void pix2ycc(float c[3], int C)
{
    color_t tmp;

    tmp.u32 = d_8to24table[C];
    c[0] = tmp.u8[0] *  0.299f + tmp.u8[1] *  0.587f + tmp.u8[2] *  0.114f;
    c[1] = tmp.u8[0] * -0.169f + tmp.u8[1] * -0.331f + tmp.u8[2] *  0.499f + 128.0f;
    c[2] = tmp.u8[0] *  0.499f + tmp.u8[1] * -0.418f + tmp.u8[2] * -0.081f + 128.0f;
}

void HQ2x_Init(void)
{
    int n, A, B;

    cvar_t *hqx_y   = Cvar_Get("hqx_y", "48", CVAR_FILES);
    cvar_t *hqx_cb  = Cvar_Get("hqx_cb", "7", CVAR_FILES);
    cvar_t *hqx_cr  = Cvar_Get("hqx_cr", "6", CVAR_FILES);

    for (n = 0; n < 256; n++) {
        rotTable[n] = ((n >> 2) & 0x11) | ((n << 2) & 0x88)
                    | ((n & 0x01) << 5) | ((n & 0x08) << 3)
                    | ((n & 0x10) >> 3) | ((n & 0x80) >> 5);
    }

    memset(equBitmap, 0, sizeof(equBitmap));

    for (A = 0; A < 255; A++) {
        for (B = 0; B <= A; B++) {
            float a[3];
            float b[3];

            pix2ycc(a, A);
            pix2ycc(b, B);

            if (fabs(a[0] - b[0]) > hqx_y->value)
                continue;
            if (fabs(a[1] - b[1]) > hqx_cb->value)
                continue;
            if (fabs(a[2] - b[2]) > hqx_cr->value)
                continue;

            Q_SetBit(equBitmap, (A << 8) + B);
            Q_SetBit(equBitmap, (B << 8) + A);
        }
    }

    Q_SetBit(equBitmap, 65535);
}
