/*
Copyright (C) 2018 Christoph Schied
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

#ifndef PATH_TRACER_TRANSPARENCY_GLSL_
#define PATH_TRACER_TRANSPARENCY_GLSL_

#include "path_tracer.h"
#include "utils.glsl"

void update_payload_transparency(inout RayPayload rp, vec4 color, float hitT)
{
	if(rp.max_transparent_distance < hitT)
	{
		rp.transparency = packHalf4x16(alpha_blend_premultiplied(unpackHalf4x16(rp.transparency), color));
		rp.max_transparent_distance = rt_HitT;
	}
	else
		rp.transparency = packHalf4x16(alpha_blend_premultiplied(color, unpackHalf4x16(rp.transparency)));
}

#endif // PATH_TRACER_TRANSPARENCY_GLSL_
