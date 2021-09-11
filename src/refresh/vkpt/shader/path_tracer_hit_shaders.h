/*
Copyright (C) 2020-2021, NVIDIA CORPORATION. All rights reserved.
Copyright (C) 2021 Frank Richter

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

layout(set = 0, binding = 1)
uniform textureBuffer particle_color_buffer;

layout(set = 0, binding = 2)
uniform textureBuffer beam_color_buffer;

layout(set = 0, binding = 3)
uniform utextureBuffer sprite_texure_buffer;

layout(set = 0, binding = 4)
uniform utextureBuffer beam_info_buffer;

void pt_logic_rchit(inout RayPayload ray_payload, int primitiveID, uint instanceCustomIndex, float hitT, vec2 bary)
{
	ray_payload.barycentric    = bary.xy;
	ray_payload.instance_prim  = primitiveID + instanceCustomIndex & AS_INSTANCE_MASK_OFFSET;
	if((instanceCustomIndex & AS_INSTANCE_FLAG_DYNAMIC) != 0)
	{
		ray_payload.instance_prim |= INSTANCE_DYNAMIC_FLAG;
	}
	if((instanceCustomIndex & AS_INSTANCE_FLAG_SKY) != 0)
	{
		ray_payload.instance_prim |= INSTANCE_SKY_FLAG;
	}
	ray_payload.hit_distance   = hitT;
}

bool pt_logic_masked(int primitiveID, uint instanceCustomIndex, vec2 bary)
{
	Triangle triangle;
	uint prim = primitiveID + instanceCustomIndex & AS_INSTANCE_MASK_OFFSET;
	if ((instanceCustomIndex & AS_INSTANCE_FLAG_DYNAMIC) != 0)
		triangle = get_instanced_triangle(prim);
	else
		triangle = get_bsp_triangle(prim);

	MaterialInfo minfo = get_material_info(triangle.material_id);

	if (minfo.mask_texture == 0)
		return true;
	
	vec2 tex_coord = triangle.tex_coords * vec3(1.0 - bary.x - bary.y, bary.x, bary.y);

	perturb_tex_coord(triangle.material_id, global_ubo.time, tex_coord);	

	vec4 mask_value = global_textureLod(minfo.mask_texture, tex_coord, /* mip_level = */ 0);

	return mask_value.x >= 0.5;
}

struct transparency_result_t
{
	vec4 color;
	float thickness;
};

transparency_result_t pt_logic_particle(int primitiveID, vec2 bary)
{
	const vec3 barycentric = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
	const vec2 uv = vec2(0.0, 0.0) * barycentric.x + vec2(1.0, 0.0) * barycentric.y + vec2(1.0, 1.0) * barycentric.z;

	const float factor = pow(clamp(1.0 - length(vec2(0.5) - uv) * 2.0, 0.0, 1.0), global_ubo.pt_particle_softness);

	transparency_result_t result;
	result.color = vec4(0);
	result.thickness = 0;

	if (factor > 0.0)
	{
		const int particle_index = primitiveID / 2;
		vec4 color = texelFetch(particle_color_buffer, particle_index);
		color.a *= factor;
		color.rgb *= color.a;

		color.rgb *= global_ubo.prev_adapted_luminance * 500;

		result.color = color;
	}

	return result;
}

transparency_result_t pt_logic_beam(int primitiveID, vec2 beam_fade_and_thickness)
{
	const float x = beam_fade_and_thickness.x;
	const float factor = pow(clamp(x, 0.0, 1.0), global_ubo.pt_beam_softness);

	transparency_result_t result;
	result.color = vec4(0);
	result.thickness = 0;

	if (factor > 0.0)
	{
		const int beam_index = primitiveID;
		vec4 color = texelFetch(beam_color_buffer, beam_index);

		color.a *= factor;
		color.rgb *= color.a;

		color.rgb *= global_ubo.prev_adapted_luminance * 20;

	    int texnum = global_ubo.current_frame_idx & (NUM_BLUE_NOISE_TEX - 1);
	    ivec2 texpos = ivec2(rt_LaunchID.xy) & ivec2(BLUE_NOISE_RES - 1);
	    float noise = texelFetch(TEX_BLUE_NOISE, ivec3(texpos, texnum), 0).r;
	    color.rgb *= noise * noise + 0.1;

	    result.color = color;
	    result.thickness = beam_fade_and_thickness.y;
	}

	return result;
}

transparency_result_t pt_logic_sprite(int primitiveID, vec2 bary)
{
	const vec3 barycentric = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
	
	vec2 uv;
	if((primitiveID & 1) == 0)
	   uv = vec2(0.0, 1.0) * barycentric.x + vec2(0.0, 0.0) * barycentric.y + vec2(1.0, 0.0) * barycentric.z;
	else
	   uv = vec2(1.0, 0.0) * barycentric.x + vec2(1.0, 1.0) * barycentric.y + vec2(0.0, 1.0) * barycentric.z;

	const int sprite_index = primitiveID / 2;

	uvec4 info = texelFetch(sprite_texure_buffer, sprite_index);

	uint texture_index = info.x;
	float alpha = uintBitsToFloat(info.y);
	vec4 color = global_textureLod(texture_index, uv, 0);
	
	color.a *= alpha;
	float lum = luminance(color.rgb);
	if(lum > 0)
	{
		float lum2 = pow(lum, 2.2);
		color.rgb = color.rgb * (lum2 / lum) * color.a * alpha;

		color.rgb *= global_ubo.prev_adapted_luminance * 2000;
	}

	transparency_result_t result;
	result.color = color;
	result.thickness = 0;

	return result;
}

transparency_result_t pt_logic_explosion(int primitiveID, uint instanceCustomIndex, vec3 worldRayDirection, vec2 bary)
{
	const uint primitive_id = primitiveID + instanceCustomIndex & AS_INSTANCE_MASK_OFFSET;
	const Triangle triangle = get_instanced_triangle(primitive_id);

	const vec3 barycentric = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
	const vec2 tex_coord = triangle.tex_coords * barycentric;

	MaterialInfo minfo = get_material_info(triangle.material_id);
	vec4 emission = global_textureLod(minfo.base_texture, tex_coord, 0);

	if((triangle.material_id & MATERIAL_KIND_MASK) == MATERIAL_KIND_EXPLOSION)
	{
		const vec3 normal = triangle.normals * barycentric;
		emission.rgb = mix(emission.rgb, get_explosion_color(normal, worldRayDirection.xyz), triangle.alpha);
		emission.rgb *= global_ubo.pt_explosion_brightness;
	}

	emission.a *= triangle.alpha;
	emission.rgb *= emission.a;

	emission.rgb *= global_ubo.prev_adapted_luminance * 500;

	transparency_result_t result;
	result.color = emission;
	result.thickness = 0;

	return result;
}

// Adapted from: http://www.pbr-book.org/3ed-2018/Utilities/Mathematical_Routines.html#SolvingQuadraticEquations
bool solve_quadratic(in float a, in float b, in float c, out vec2 t)
{
	float discrim = b * b - 4 * a * c;
	if (discrim < 0) return false;
	float q;
	if (b < 0)
		q = -0.5 * (b - sqrt(discrim));
	else
		q = -0.5 * (b + sqrt(discrim));
	float t0 = q / a;
	float t1 = c / q;
	t = vec2(min(t0, t1), max(t0, t1));
	return true;
}

bool hit_cylinder(in vec3 o, in vec3 d, in float radius, out vec2 t)
{
	// Adapted from: http://www.pbr-book.org/3ed-2018/Shapes/Cylinders.html#IntersectionTests
	float a = dot(d.xy, d.xy);
	float b = 2 * dot(d.xy, o.xy);
	float c = dot(o.xy, o.xy) - radius * radius;

	return solve_quadratic(a, b, c, t);
}

bool hit_sphere(in vec3 o, in vec3 d, in float radius, out vec2 t)
{
	// Adapted from: http://www.pbr-book.org/3ed-2018/Shapes/Spheres.html#IntersectionTests
	float a = dot(d, d);
	float b = 2 * dot(d, o);
	float c = dot(o, o) - radius * radius;

	return solve_quadratic(a, b, c, t);
}

bool pt_logic_beam_intersection(int beam_index,
	vec3 worldRayOrigin, vec3 worldRayDirection,
	float rayTmin, float rayTmax, out vec2 beam_fade_and_thickness, out float tShapeHit)
{
	beam_fade_and_thickness = vec2(0);
	tShapeHit = 0;

	const uvec4 beam_info[3] = { texelFetch(beam_info_buffer, beam_index * 3),
								 texelFetch(beam_info_buffer, beam_index * 3 + 1),
								 texelFetch(beam_info_buffer, beam_index * 3 + 2) };
	/* Transform from world space to "beam space" (really, object space),
	   where the beam starts at the origin and points towards +Z */
	const mat4 world_to_beam = mat4(unpackHalf4x16(beam_info[1].xy),
									unpackHalf4x16(beam_info[1].zw),
									unpackHalf4x16(beam_info[2].xy),
									uintBitsToFloat(beam_info[0]));
	const float beam_radius = uintBitsToFloat(beam_info[2].z);
	const float beam_length = uintBitsToFloat(beam_info[2].w);

	// Ray origin, direction in "beam space"
	const vec3 o = (world_to_beam * vec4(worldRayOrigin, 1)).xyz;
	const vec3 d = (world_to_beam * vec4(worldRayDirection, 0)).xyz;

	vec2 t;
	if(!hit_cylinder(o, d, beam_radius, t)) 
		return false;

	// The intersection Z values (ie "height on beam")
	vec2 hit_z = vec2(o.z) + vec2(d.z) * t;
	/* Check if we hit outside the cylinder bounds -
	   if so, see if we hit the "end spheres",
	   and update the hit location */
	bvec2 hit_below_0 = lessThan(hit_z, vec2(0));
	if(any(hit_below_0))
	{
		vec2 t_sphere;
		if(!hit_sphere(o, d, beam_radius, t_sphere))
			return false;

		if(hit_below_0.x) t.x = max(t.x, t_sphere.x);
		if(hit_below_0.y) t.y = min(t.y, t_sphere.y);
	}

	bvec2 hit_above_end = greaterThan(hit_z, vec2(beam_length));
	if(any(hit_above_end))
	{
		vec2 t_sphere;
		if(!hit_sphere(o - vec3(0, 0, beam_length), d, beam_radius, t_sphere))
			return false;

		if(hit_above_end.x) t.x = max(t.x, t_sphere.x);
		if(hit_above_end.y) t.y = min(t.y, t_sphere.y);
	}

	if((t.x >= rayTmax) || (t.y < rayTmin))
		return false;

	tShapeHit = t.x;
	if (tShapeHit < rayTmin)
	{
		tShapeHit = t.y;
		if (tShapeHit >= rayTmax)
			return false;
	}

	// Compute points on ray and beam center where they're closest to each other
	const vec3 perp_norm = normalize(vec3(d.y, -d.x, 0));
	const vec3 n2 = vec3(-perp_norm.y, perp_norm.x, 0);
	const float t1 = dot(-o, n2) / dot(d, n2);
	const vec3 n1 = cross(d, perp_norm);
	const float t2 = dot(o, n1) / n1.z;

	const vec3 c_ray = o + t1 * d; // Point on ray closest to beam center
	const vec3 c_beam = vec3(0, 0, t2); // Point on beam closest to ray

	/* Compute "distance" to beam center used for beam intensity.
	   Using the closest distance between the ray and the beam line segment
	   looks best when the beam is seen from the side;
	   Using the closest distance between the ray and the beam infinitely
	   extended looks looks best when looking at the beam "head on"
	   (ray parallel to beam) -
	   so mix between those two, based on the ray/beam angle.
	 */
	const float dist_side = distance(c_ray, vec3(0, 0, clamp(t2, 0, beam_length)));
	const float dist_head = distance(c_ray, c_beam);
	const float dist = mix(dist_side, dist_head, abs(d.z));

	float fade = 1.0 - dist / beam_radius;
	float thickness = t.y - t.x;
	fade *= clamp(thickness / (2 * beam_radius), 0, 1);

	beam_fade_and_thickness = vec2(fade, thickness);
	return true;
}
