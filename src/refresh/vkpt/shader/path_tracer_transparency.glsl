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

void update_payload_transparency(inout RayPayload rp, vec4 color, float depth, float hitT)
{
	if(hitT > rp.farthest_transparent_distance)
	{
		rp.close_transparencies = packHalf4x16(alpha_blend_premultiplied(unpackHalf4x16(rp.close_transparencies), unpackHalf4x16(rp.farthest_transparency)));
		rp.closest_max_transparent_distance = rp.farthest_transparent_distance;
		rp.farthest_transparency = packHalf4x16(color);
		rp.farthest_transparent_distance = hitT;
		rp.farthest_transparent_depth = depth;
	}
	else if(rp.closest_max_transparent_distance < hitT)
	{
		rp.close_transparencies = packHalf4x16(alpha_blend_premultiplied(unpackHalf4x16(rp.close_transparencies), color));
		rp.closest_max_transparent_distance = hitT;
	}
	else
		rp.close_transparencies = packHalf4x16(alpha_blend_premultiplied(color, unpackHalf4x16(rp.close_transparencies)));
}

vec4 get_payload_transparency(in RayPayload rp, float solidDist)
{
	float scale_far = 1;
	if (rp.farthest_transparent_depth > 0)
	{
		scale_far = clamp((solidDist - rp.farthest_transparent_distance) / rp.farthest_transparent_depth, 0, 1);
	}

	return alpha_blend_premultiplied(unpackHalf4x16(rp.close_transparencies), unpackHalf4x16(rp.farthest_transparency) * scale_far);
}

vec4 get_payload_transparency_simple(in RayPayload rp)
{
	return alpha_blend_premultiplied(unpackHalf4x16(rp.close_transparencies), unpackHalf4x16(rp.farthest_transparency));
}

#endif // PATH_TRACER_TRANSPARENCY_GLSL_
