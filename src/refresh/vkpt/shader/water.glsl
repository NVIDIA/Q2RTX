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

vec3 get_water_normal(uint material_id, vec3 geo_normal, vec3 tangent, vec3 position, bool local_space)
{
	// Add flow
	if((material_id & MATERIAL_FLAG_FLOWING) != 0)
	{
		position -= tangent * global_ubo.time * 32;
	}	

	// Remove the sign from the normal to make water simulation uniform,
	// regardless of which side we're looking at the surface from.
	// This is necessary to have caustics motion match the waves.
	vec3 unsigned_geo_normal = abs(geo_normal);

	// Construct a basis around the normal, get object-local 2D position.
	mat3 basis = construct_ONB_frisvad(unsigned_geo_normal);
	vec2 p = vec2(dot(position, basis[0]), dot(position, basis[2]));
	
	// Sample the texture and add a few instances of noise.

	const float speed = 2.5;

	vec2 uv1 = p.xy * 0.006 + global_ubo.time * vec2(0.01, 0.02) * speed;
	vec3 a = global_textureLod(global_ubo.water_normal_texture, uv1, 0).xyz;
	a.xy = a.xy * 2 - vec2(1);
	a.xy *= 0.3;

	vec2 uv2 = p.xy * 0.003 + global_ubo.time * vec2(0.013, 0.014) * speed;
	vec3 b = global_textureLod(global_ubo.water_normal_texture, uv2, 0).xyz;
	b.xy = b.xy * 2 - vec2(1);
	b.xy *= 0.5;

	vec2 uv3 = p.xy * 0.0061 + global_ubo.time * vec2(-0.01, -0.02) * speed;
	vec3 c = global_textureLod(global_ubo.water_normal_texture, uv3, 0).xyz;
	c.xy = c.xy * 2 - vec2(1);
	c.xy *= 0.3;

	vec3 n = normalize(a + b + c).xzy;

	if(local_space)
		return n;

	// Back into world space
	n = basis * n;

	// Restore the sign
	if(geo_normal.x < 0) n.x = -n.x;
	if(geo_normal.y < 0) n.y = -n.y;
	if(geo_normal.z < 0) n.z = -n.z;

	return n;
}

vec3 get_extinction_factors(int medium)
{
	vec3 factors = vec3(0);
	if(medium == MEDIUM_WATER)
		factors = vec3(0.035, 0.013, 0.012);
	else if(medium == MEDIUM_SLIME)
		factors = vec3(0.200, 0.010, 0.050);
	else if(medium == MEDIUM_LAVA)
		factors = vec3(0.001, 0.100, 0.300);

	return factors * global_ubo.pt_water_density;
}

vec3 extinction(int medium, float distance)
{
	vec3 factors = get_extinction_factors(medium);

	return exp(-factors * distance);
}
