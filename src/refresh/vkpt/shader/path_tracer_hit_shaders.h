/*
Copyright (C) 2020, NVIDIA CORPORATION. All rights reserved.

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

void pt_logic_particle(inout RayPayload ray_payload, int primitiveID, float hitT, vec2 bary)
{
	const vec3 barycentric = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
	const vec2 uv = vec2(0.0, 0.0) * barycentric.x + vec2(1.0, 0.0) * barycentric.y + vec2(1.0, 1.0) * barycentric.z;

	const float factor = pow(clamp(1.0 - length(vec2(0.5) - uv) * 2.0, 0.0, 1.0), global_ubo.pt_particle_softness);

	if (factor > 0.0)
	{
		const int particle_index = primitiveID / 2;
		vec4 color = texelFetch(particle_color_buffer, particle_index);
		color.a *= factor;
		color.rgb *= color.a;

		color.rgb *= global_ubo.prev_adapted_luminance * 500;

		if(ray_payload.max_transparent_distance < hitT)
		{
			ray_payload.transparency = packHalf4x16(alpha_blend_premultiplied(unpackHalf4x16(ray_payload.transparency), color));
			ray_payload.max_transparent_distance = hitT;
		}
		else
			ray_payload.transparency = packHalf4x16(alpha_blend_premultiplied(color, unpackHalf4x16(ray_payload.transparency)));
	}
}

void pt_logic_beam(inout RayPayload ray_payload, int primitiveID, float hitT, vec2 bary)
{
	const float x = bary.x + bary.y;
	const float factor = pow(clamp(1.0 - abs(0.5 - x) * 2.0, 0.0, 1.0), global_ubo.pt_beam_softness);

	if (factor > 0.0)
	{
		const int particle_index = primitiveID / 2;
		vec4 color = texelFetch(beam_color_buffer, particle_index);

		color.a *= factor;
		color.rgb *= color.a;

		color.rgb *= global_ubo.prev_adapted_luminance * 20;

	    int texnum = global_ubo.current_frame_idx & (NUM_BLUE_NOISE_TEX - 1);
	    ivec2 texpos = ivec2(rt_LaunchID.xy) & ivec2(BLUE_NOISE_RES - 1);
	    float noise = texelFetch(TEX_BLUE_NOISE, ivec3(texpos, texnum), 0).r;
	    color.rgb *= noise * noise + 0.1;

		if(ray_payload.max_transparent_distance < hitT)
		{
			ray_payload.transparency = packHalf4x16(alpha_blend_premultiplied(unpackHalf4x16(ray_payload.transparency), color));
			ray_payload.max_transparent_distance = hitT;
		}
		else
			ray_payload.transparency = packHalf4x16(alpha_blend_premultiplied(color, unpackHalf4x16(ray_payload.transparency)));

	}
}

void pt_logic_sprite(inout RayPayload ray_payload, int primitiveID, float hitT, vec2 bary)
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

	if(ray_payload.max_transparent_distance < hitT)
		ray_payload.transparency = packHalf4x16(alpha_blend_premultiplied(unpackHalf4x16(ray_payload.transparency), color));
	else
		ray_payload.transparency = packHalf4x16(alpha_blend_premultiplied(color, unpackHalf4x16(ray_payload.transparency)));

	ray_payload.max_transparent_distance = hitT;
}

void pt_logic_explosion(inout RayPayload ray_payload, int primitiveID, uint instanceCustomIndex, float hitT, vec3 worldRayDirection, vec2 bary)
{
	const uint primitive_id = primitiveID + instanceCustomIndex & AS_INSTANCE_MASK_OFFSET;
	const Triangle triangle = get_instanced_triangle(primitive_id);

	const vec3 barycentric = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);
	const vec2 tex_coord = triangle.tex_coords * barycentric;

	MaterialInfo minfo = get_material_info(triangle.material_id);
	vec4 emission = global_textureLod(minfo.diffuse_texture, tex_coord, 0);

	if((triangle.material_id & MATERIAL_KIND_MASK) == MATERIAL_KIND_EXPLOSION)
	{
		const vec3 normal = triangle.normals * barycentric;
		emission.rgb = mix(emission.rgb, get_explosion_color(normal, worldRayDirection.xyz), triangle.alpha);
		emission.rgb *= global_ubo.pt_explosion_brightness;
	}

	emission.a *= triangle.alpha;
	emission.rgb *= emission.a;

	emission.rgb *= global_ubo.prev_adapted_luminance * 500;

	if(ray_payload.max_transparent_distance < hitT)
	{
		ray_payload.transparency = packHalf4x16(alpha_blend_premultiplied(unpackHalf4x16(ray_payload.transparency), emission));
		ray_payload.max_transparent_distance = hitT;
	}
	else
		ray_payload.transparency = packHalf4x16(alpha_blend_premultiplied(emission, unpackHalf4x16(ray_payload.transparency)));

}