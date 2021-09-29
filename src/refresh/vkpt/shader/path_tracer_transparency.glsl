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

vec4 evaluate_fog(uvec4 fog, float t1, float t2)
{
	vec2 fog_bounds = unpackHalf2x16(fog.z);
	vec2 fog_density = unpackHalf2x16(fog.w) / 65536.0; // same scale as in find_fog_volume(...)
	vec3 fog_color = unpackHalf4x16(fog.xy).rgb;

	t1 = max(t1, fog_bounds.x);
	t2 = min(t2, fog_bounds.y);

	if (t1 >= t2)
		return vec4(0);

	// solution to the diff equation: dL = -(at+b) dt,
	// where L is luminance and alpha is (1 - L_out / L_in),
	// a and b are the density function parameters
	float alpha = 1.0 - exp((t1 * t1 - t2 * t2) * fog_density.x + (t1 - t2) * fog_density.y);

	return vec4(fog_color * alpha, alpha);
}

void blend_fogs(in RayPayloadEffects rp, float t1, float t2, inout vec4 accumulated_color)
{
	// Blend the further fog volume first...
	if (rp.fog2.w != 0) // test the density for being nonzero
	{
		vec4 fog_color = evaluate_fog(rp.fog2, t1, t2);
		accumulated_color = alpha_blend_premultiplied(fog_color, accumulated_color);
	}

	// ...Then blend the closer volume.
	if (rp.fog1.w != 0)
	{
		vec4 fog_color = evaluate_fog(rp.fog1, t1, t2);
		accumulated_color = alpha_blend_premultiplied(fog_color, accumulated_color);
	}
}

void update_payload_transparency(inout RayPayloadEffects rp, vec4 color, float hitT)
{
	vec4 accumulated_color = unpackHalf4x16(rp.transparency);
	vec2 distances = unpackHalf2x16(rp.distances);

	if(hitT < distances.x || distances.x == 0)
	{
		if (distances.x > 0)
		{
			blend_fogs(rp, hitT, distances.x, accumulated_color);
		}
		accumulated_color = alpha_blend_premultiplied(color, accumulated_color);
		distances.x = hitT;
	}
	else
	{
		if (hitT > distances.y && distances.y > 0)
		{
			blend_fogs(rp, distances.y, hitT, accumulated_color);
		}
		accumulated_color = alpha_blend_premultiplied(accumulated_color, color);
	}
	
	distances.y = max(distances.y, hitT);

	rp.transparency = packHalf4x16(accumulated_color);
	rp.distances = packHalf2x16(distances);
}

vec4 get_payload_transparency(in RayPayloadEffects rp)
{
	return unpackHalf4x16(rp.transparency);
}

vec4 get_payload_transparency_with_fog(in RayPayloadEffects rp, float t_max)
{
	vec2 distances = unpackHalf2x16(rp.distances);

	vec4 accumulator = vec4(0);
	float current_dist = t_max;

	if (distances.y > 0)
	{
		blend_fogs(rp, distances.y, t_max, accumulator);

		vec4 ray_transparency = unpackHalf4x16(rp.transparency);
		accumulator = alpha_blend_premultiplied(ray_transparency, accumulator);

		current_dist = distances.x;
	}

	blend_fogs(rp, 0, current_dist, accumulator);

	return accumulator;
}

#endif // PATH_TRACER_TRANSPARENCY_GLSL_
