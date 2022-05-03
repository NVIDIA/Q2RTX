/*
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.

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

#ifndef CONVERSION_H_
#define CONVERSION_H_

#include <stdint.h>

/*
  Float -> Half converter functions, adapted from
  https://stackoverflow.com/questions/1659440/32-bit-to-16-bit-floating-point-conversion
*/

typedef union
{
	float f;
	int32_t si;
	uint32_t ui;
} f2hBits;

static inline uint16_t floatToHalf(float value)
{
	static int const shift = 13;
	static int const shiftSign = 16;

	static int32_t const infN = 0x7F800000; // flt32 infinity
	static int32_t const maxN = 0x477FE000; // max flt16 normal as a flt32
	static int32_t const minN = 0x38800000; // min flt16 normal as a flt32
	static int32_t const signN = 0x80000000; // flt32 sign bit

	static int32_t const nanN = 0x7F802000; // minimum flt16 nan as a flt32
	static int32_t const maxC = 0x23BFF;

	static int32_t const mulN = 0x52000000; // (1 << 23) / minN

	static int32_t const subC = 0x003FF; // max flt32 subnormal down shifted

	static int32_t const maxD = 0x1C000;
	static int32_t const minD = 0x1C000;

	f2hBits v, s;
	v.f = value;
	uint32_t sign = v.si & signN;
	v.si ^= sign;
	sign >>= shiftSign; // logical shift
	s.si = mulN;
	s.si = (int32_t)(s.f * v.f); // correct subnormals
	v.si ^= (s.si ^ v.si) & -(minN > v.si);
	v.si ^= (infN ^ v.si) & -((infN > v.si) & (v.si > maxN));
	v.si ^= (nanN ^ v.si) & -((nanN > v.si) & (v.si > infN));
	v.ui >>= shift; // logical shift
	v.si ^= ((v.si - maxD) ^ v.si) & -(v.si > maxC);
	v.si ^= ((v.si - minD) ^ v.si) & -(v.si > subC);
	return v.ui | sign;
}

static inline float halfToFloat(uint16_t value)
{
	static int const shift = 13;
	static int const shiftSign = 16;

	static uint32_t const inf = 0x7c00;
	static uint32_t const signC = 0x8000; // flt16 sign bit
	static int32_t const infN = 0x7F800000; // flt32 infinity

	f2hBits v, s;
	v.ui = value;
	s.ui = v.ui & signC;
	v.ui ^= s.ui;
	int32_t is_norm = v.ui < inf;
	v.ui = (s.ui << shiftSign) | (v.ui << shift);
	s.ui = 0x77800000; // bias_mul
	v.f *= s.f;
	v.ui |= -!is_norm & infN;
	return v.f;
}

static inline float uintBitsToFloat(uint32_t ui)
{
	union {
		float f;
		uint32_t ui;
	} bits;
	bits.ui = ui;
	return bits.f;
}

void packHalf4x16(uint32_t* half, float* vec4);

#endif // CONVERSION_H_
