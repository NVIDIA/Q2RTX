/*
Copyright (C) 2018 Christoph Schied

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

#ifndef  _ASVGF_GLSL_
#define  _ASVGF_GLSL_

#include "utils.glsl"

#define STRATUM_OFFSET_SHIFT 3
#define STRATUM_OFFSET_MASK ((1 << STRATUM_OFFSET_SHIFT) - 1)

void
read_normal_depth(sampler2D tex, ivec2 p, out float z, out vec3 n)
{
	vec2 v = texelFetch(tex, p, 0).rg; 
	z = v.x;
	n = decode_normal(floatBitsToUint(v.y));
}

const float gaussian_kernel[2][2] = {
	{ 1.0 / 4.0, 1.0 / 8.0  },
	{ 1.0 / 8.0, 1.0 / 16.0 }
};


#endif  /*_ASVGF_GLSL_*/
