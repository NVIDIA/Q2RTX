/*
Copyright (C) 2018 Tobias Zirr
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

#ifndef _LIGHT_LISTS_
#define _LIGHT_LISTS_

#define MAX_BRUTEFORCE_SAMPLING 8

float
projected_tri_area(mat3 positions, vec3 p, vec3 n, vec3 V, float phong_exp, float phong_weight)
{
	positions[0] = positions[0] - p;
	positions[1] = positions[1] - p;
	positions[2] = positions[2] - p;
	
	vec3 g = cross(positions[1] - positions[0], positions[2] - positions[0]);
	if ( dot(n, positions[0]) <= 0 && dot(n, positions[1]) <= 0 && dot(n, positions[2]) <= 0 )
		return 0;
	if ( dot(g, positions[0]) >= 0 && dot(g, positions[1]) >= 0 && dot(g, positions[2]) >= 0 )
		return 0;

	vec3 L = normalize(positions * vec3(1.0 / 3.0));
	vec3 H = normalize(L - V);
	float phong = pow(max(0.0, dot(H, n)), phong_exp);
	float brdf = mix(1.0, phong, phong_weight);

	positions[0] = normalize(positions[0]);
	positions[1] = normalize(positions[1]);
	positions[2] = normalize(positions[2]);

	vec3 a = cross(positions[1] - positions[0], positions[2] - positions[0]);
	float pa = max(length(a) - 1e-5, 0.);
	return pa * brdf;
}

vec3
sample_projected_triangle(vec3 p, mat3 positions, vec2 rnd, out vec3 light_normal, out float projected_area)
{
	light_normal = cross(positions[1] - positions[0], positions[2] - positions[0]);
	light_normal = normalize(light_normal);

	positions[0] = positions[0] - p;
	positions[1] = positions[1] - p;
	positions[2] = positions[2] - p;

	float o = dot(light_normal, positions[0]);

	positions[0] = normalize(positions[0]);
	positions[1] = normalize(positions[1]);
	positions[2] = normalize(positions[2]);
	
	vec3 projected_normal = cross(positions[1] - positions[0], positions[2] - positions[0]);

	vec3 direction = positions * sample_triangle(rnd);
	float dl = length(direction);

	// n (p + d * t - p[i]) == 0
	// -n (p - pi) / n d == o / n d == t
	vec3 lo = direction * (o / dot(light_normal, direction));
	
	projected_area = length(projected_normal) * 0.5;

	return p + lo;
}

void
sample_light_list(
		uint list_idx,
		vec3 p,
		vec3 n,
		vec3 gn,
		vec3 V, 
		float phong_exp, 
		float phong_weight,
		out vec3 position_light,
		out vec3 light_color,
		out vec3 light_normal,
		out float pdf,
		vec3 rng)
{
	if(list_idx == ~0u)
	{
		position_light = vec3(0);
		light_color = vec3(0);
		pdf = 0;
		return;
	}

	uint list_start = get_light_list_offsets(list_idx);
	uint list_end   = get_light_list_offsets(list_idx + 1);

	float partitions = ceil(float(list_end - list_start) / float(MAX_BRUTEFORCE_SAMPLING));
	rng.x *= partitions;
	float fpart = min(floor(rng.x), partitions-1);
	rng.x -= fpart;
	list_start += int(fpart);
	int stride = int(partitions);

	float mass = 0.;

	float light_masses[MAX_BRUTEFORCE_SAMPLING];

	#pragma unroll
	for(uint i = 0, n_idx = list_start; i < MAX_BRUTEFORCE_SAMPLING; i++, n_idx += stride) {
		if (n_idx >= list_end)
			break;
		
		uint current_idx = get_light_list_lights(n_idx);

		LightPolygon light = get_light_polygon(current_idx);

		float m = projected_tri_area(light.positions, p, n, V, phong_exp, phong_weight);

		float light_lum = luminance(light.color);
		if(light_lum < 0 && global_ubo.environment_type == ENVIRONMENT_DYNAMIC)
			m *= sun_color_ubo.sky_luminance;
		else
			m *= abs(light_lum); // abs because sky lights have negative color

		mass += m;
		light_masses[i] = m;
	}

	if (!(mass > 0)) {
		pdf = 0;
		return;
	}

	rng.x *= mass;
	int current_idx = -1;
	mass *= partitions;
	pdf = 0;

	#pragma unroll
	for(uint i = 0, n_idx = list_start; i < MAX_BRUTEFORCE_SAMPLING; i++, n_idx += stride) {
		if (n_idx >= list_end)
			break;
		pdf = light_masses[i];
		current_idx = int(n_idx);
		rng.x -= pdf;

		if (!(rng.x > 0))
			break;
	}

	pdf /= mass;

	// assert: current_idx >= 0?
	if (current_idx >= 0) {
		current_idx = int(get_light_list_lights(current_idx));

		LightPolygon light = get_light_polygon(current_idx);

		float area;
		position_light = sample_projected_triangle(p, light.positions, rng.yz, light_normal, area);

		vec3 L = normalize(position_light - p);

		if(dot(L, gn) <= 0)
			area = 0;

		float LdotNL = max(0, -dot(light_normal, L));
		float spotlight = sqrt(LdotNL);

		if(light.color.r >= 0)
			light_color = light.color * area * spotlight;
		else
			light_color = env_map(L, true) * area * global_ubo.pt_env_scale;
	}
}

void
sample_light_list_dynamic(
		uint light_idx,
		vec3 p,
		vec3 n,
		vec3 gn,
		out vec3 position_light,
		out vec3 light_color,
		vec2 rng)
{
	vec4 light_center_radius = global_ubo.dynamic_light_data[light_idx * 2];
	float sphere_radius = light_center_radius.w;

	light_color = global_ubo.dynamic_light_data[light_idx * 2 + 1].rgb;

	vec3 c = light_center_radius.xyz - p;
	float dist = length(c);
	float rdist = 1.0 / dist;
	vec3 L = c * rdist;

	float tan_half_angular_size = min(sphere_radius * rdist, 1.0);
	float half_angular_size = atan(tan_half_angular_size);
	float irradiance = half_angular_size * half_angular_size;

	mat3 onb = construct_ONB_frisvad(L);
	vec3 diskpt;
	diskpt.xy = sample_disk(rng.xy);
	diskpt.z = sqrt(max(0, 1 - diskpt.x * diskpt.x - diskpt.y * diskpt.y));

	position_light = light_center_radius.xyz + (onb[0] * diskpt.x + onb[2] * diskpt.y - L * diskpt.z) * sphere_radius;
	light_color *= irradiance;

	if(dot(position_light - p, gn) <= 0)
		light_color = vec3(0);
}

#endif /*_LIGHT_LISTS_*/

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
