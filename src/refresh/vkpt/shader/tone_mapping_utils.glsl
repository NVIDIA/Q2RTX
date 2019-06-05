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

// ==========================================================================
//
// This is the utilities file for the tone mapper, which is based on part of
// Eilertsen, Mantiuk, and Unger's paper *Real-time noise- aware tone mapping*,
// with some additional modifications that we found useful.
//
// This file contains several constants used by multiple tone mapping shaders.
//
// The tone mapper consists of three compute shaders, a utilities file, and
// a CPU-side code file. For an overview of the tone mapper, see
// `tone_mapping_histogram.comp`.
//   
// ==========================================================================

// These constants determine how we convert from floating-point to fixed-point
// when performing atomic additions in the histogram computation code.
#define FIXED_POINT_FRAC_BITS 7
#define FIXED_POINT_FRAC_MULTIPLIER (1 << FIXED_POINT_FRAC_BITS)

// These are the maximum log (base 2) luminance values (i.e. photographic stops)
// represented in the tone mapping histogram.
// Anything below min_log_luminance will not be counted in the tone mapping
// histogram, and will be an inky black on-screen.
const float min_log_luminance = -24;
const float max_log_luminance = 8;

// To map min_log_luminance to 0 and max_log_luminance to 1, we can use the
// function f(x) = (x - min_log_luminance)/(max_log_luminance - min_log_luminance).
// However, if we instead express this as
// x/(max_log_luminance - min_log_luminance) - min_log_luminance/(max_log_luminance - min_log_luminance),
// then we can compute f(x) using a single fused multiply-add instruction.
const float log_luminance_scale = 1.0 / (max_log_luminance - min_log_luminance);
const float log_luminance_bias = -min_log_luminance * log_luminance_scale;
