/*
Copyright (C) 2018 Tobias Zirr

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
#define MAX_BRUTEFORCE_SAMPLING_DYNAMIC 8
#define SOLID_ANGLE_SAMPLING 1

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#define PHONG_IS_EXP 20.0

float
projected_tri_area(mat3 positions, vec3 p, vec3 n, vec3 v)
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
	float brdf = blinn_phong_based_brdf(v, L, n, PHONG_IS_EXP);

	positions[0] = normalize(positions[0]);
	positions[1] = normalize(positions[1]);
	positions[2] = normalize(positions[2]);

	vec3 a = cross(positions[1] - positions[0], positions[2] - positions[0]);
	float pa = max(-dot(n, a), 0.);
	return pa * brdf;
}

vec3
sample_projected_triangle(vec3 p, mat3 positions, vec2 rnd, out vec3 n, inout float pdf)
{
	n = cross(positions[1] - positions[0], positions[2] - positions[0]);
	n = normalize(n);

	positions[0] = positions[0] - p;
	positions[1] = positions[1] - p;
	positions[2] = positions[2] - p;

	float o = dot(n, positions[0]);

	positions[0] = normalize(positions[0]);
	positions[1] = normalize(positions[1]);
	positions[2] = normalize(positions[2]);
	
	vec3 n2 = cross(positions[1] - positions[0], positions[2] - positions[0]);

	vec3 d = positions * sample_triangle(rnd);
	float dl = length(d);
	// n (p + d * t - p[i]) == 0
	// -n (p - pi) / n d == o / n d == t
	vec3 lo = d * (o / dot(n, d));
	float lol = length(lo);

	pdf *= 2.0 / dot(n2, d) * dot(n, lo);
	pdf *= (dl * dl * dl) / (lol * lol * lol);
	return p + lo;
}

void
sample_light_list(
		uint list_idx,
		vec3 V,
		vec3 p,
		vec3 n,
		out vec3 position_light,
		out vec3 normal_light,
		out vec3 light_color,
		out float pdf,
		vec3 rng)
{
	if(list_idx == ~0u)
		return;
	pdf = 1.0;

	uint list_start = get_light_list_offsets(list_idx);
	uint list_end   = get_light_list_offsets(list_idx + 1);

//#define NO_IS
#ifdef NO_IS
	int current_idx = -1;
	{
		uint n_idx = min(list_start + int(rng.x * float(list_end - list_start)), list_end - 1);
		current_idx = get_light_list_lights(n_idx);
	}
#else

	int stride = 1;
//#define UNSTRIDED
#ifdef UNSTRIDED
	{
		float partitions = ceil(float(list_end - list_start) / float(MAX_BRUTEFORCE_SAMPLING));
		//int partitions = (list_end - list_start + (MAX_BRUTEFORCE_SAMPLING-1)) / MAX_BRUTEFORCE_SAMPLING;
		rng.x *= partitions;
		float fpart = min(floor(rng.x), partitions-1);
		rng.x -= fpart;
		int part_idx = int(fpart);
		int ceq = (list_end - list_start + int(partitions)-1) / int(partitions);
		list_start += ceq * part_idx;
		list_end = min(list_start + ceq, list_end);
		pdf = partitions;
	}
#else
	{
		float partitions = ceil(float(list_end - list_start) / float(MAX_BRUTEFORCE_SAMPLING));
		rng.x *= partitions;
		float fpart = min(floor(rng.x), partitions-1);
		rng.x -= fpart;
		list_start += int(fpart);
		stride = int(partitions);
		pdf = partitions;
	}
#endif

	float mass = 0.;

	float light_masses[MAX_BRUTEFORCE_SAMPLING];

	#pragma unroll
	for(uint i = 0, n_idx = list_start; i < MAX_BRUTEFORCE_SAMPLING; i++, n_idx += stride) {
		if (n_idx >= list_end)
			break;
		
		uint current_idx = get_light_list_lights(n_idx);

		mat3 positions = mat3x3(
				get_positions_bsp(current_idx * 3 + 0),
				get_positions_bsp(current_idx * 3 + 1),
				get_positions_bsp(current_idx * 3 + 2));

		float m = projected_tri_area(positions, p, n, V);
		mass += m;
		light_masses[i] = m;
	}

	if (!(mass > 0)) {
		pdf = 0;
		return;
	}

	rng.x *= mass;
	int current_idx = -1;
	mass *= pdf; // partitions
	pdf = 0;

	#pragma unroll
	for(uint i = 0, n_idx = list_start; i < MAX_BRUTEFORCE_SAMPLING; i++, n_idx += stride) {
		if (n_idx >= list_end)
			break;
#if 0
		
		current_idx = int(get_light_list_lights(n_idx));
		mat3 positions = mat3x3(
				get_positions_bsp(current_idx * 3 + 0),
				get_positions_bsp(current_idx * 3 + 1),
				get_positions_bsp(current_idx * 3 + 2));
		pdf = projected_tri_area(positions, p, n, V);
#else
		pdf = light_masses[i];
		current_idx = int(n_idx);
#endif
		rng.x -= pdf;

		if (!(rng.x > 0))
			break;
	}

	pdf /= mass;

#endif

	// assert: current_idx >= 0?
	if (current_idx >= 0) {
		current_idx = int(get_light_list_lights(current_idx));
		mat3 positions = mat3x3(
				get_positions_bsp(current_idx * 3 + 0),
				get_positions_bsp(current_idx * 3 + 1),
				get_positions_bsp(current_idx * 3 + 2));
#if SOLID_ANGLE_SAMPLING
		position_light = sample_projected_triangle(p, positions, rng.yz, normal_light, pdf);
#else
		vec3 bary = sample_triangle(rng.yz);
		position_light = positions * bary;
		vec3 a = cross(positions[1] - positions[0], positions[2] - positions[0]);
		normal_light = a / length(a);
		float area = length(a);
		pdf *= 2.0 / area;
#endif
		uint light_material = get_materials_bsp(current_idx);
		light_color = global_textureLod(light_material, vec2(0.5), 2).rgb;
	}
}

void
sample_light_list_dynamic(
		uint light_idx,
		vec3 V,
		vec3 p,
		vec3 n,
		out vec3 position_light,
		out vec3 normal_light,
		out vec3 light_color,
		out float pdf,
		vec3 rng)
{
	pdf = 1.0;

	uint list_start = global_ubo.light_offset_cnt[light_idx / 2][(light_idx % 2) * 2];
	light_color = (list_start & (1 << 31)) > 0 ? vec3(-5.0, -5.0, 1.0) : vec3(1, 0.2, 0.0);

	list_start &= ~(1 << 31);
	uint list_end = global_ubo.light_offset_cnt[light_idx / 2][(light_idx % 2) * 2 + 1];
	list_end += list_start;

	int stride = 1;
	{
		float partitions = ceil(float(list_end - list_start) / float(MAX_BRUTEFORCE_SAMPLING_DYNAMIC));
		rng.x *= partitions;
		float fpart = min(floor(rng.x), partitions-1);
		rng.x -= fpart;
		list_start += int(fpart);
		stride = int(partitions);
		pdf = partitions;
	}

	float mass = 0.;
	float light_masses[MAX_BRUTEFORCE_SAMPLING_DYNAMIC];

	#pragma unroll
	for(uint i = 0, n_idx = list_start; i < MAX_BRUTEFORCE_SAMPLING_DYNAMIC; i++, n_idx += stride) {
		if (n_idx >= list_end)
			break;
		
		uint current_idx = n_idx;

		mat3 positions = mat3(
				get_positions_instanced(current_idx * 3 + 0),
				get_positions_instanced(current_idx * 3 + 1),
				get_positions_instanced(current_idx * 3 + 2));

		float m = projected_tri_area(positions, p, n, V);
		mass += m;
		light_masses[i] = m;
	}

	if (!(mass > 0)) {
		pdf = 0;
		return;
	}

	rng.x *= mass;
	int current_idx = -1;
	mass *= pdf; // partitions
	pdf = 0;

	#pragma unroll
	for(uint i = 0, n_idx = list_start; i < MAX_BRUTEFORCE_SAMPLING_DYNAMIC; i++, n_idx += stride) {
		if (n_idx >= list_end)
			break;
		
		current_idx = int(n_idx);
#if 0
		mat3 positions = mat3x3(
				get_positions_instanced(current_idx * 3 + 0),
				get_positions_instanced(current_idx * 3 + 1),
				get_positions_instanced(current_idx * 3 + 2));
		pdf = projected_tri_area(positions, p, n, V);
#else
		pdf = light_masses[i];
		rng.x -= pdf;
#endif

		if (!(rng.x > 0))
			break;
	}

	pdf /= mass;

	// assert: current_idx >= 0?
	if (current_idx >= 0) {
		mat3 positions = mat3x3(
				get_positions_instanced(current_idx * 3 + 0),
				get_positions_instanced(current_idx * 3 + 1),
				get_positions_instanced(current_idx * 3 + 2));

#if SOLID_ANGLE_SAMPLING
		position_light = sample_projected_triangle(p, positions, rng.yz, normal_light, pdf);
#else
		vec3 bary = sample_triangle(rng.yz);
		position_light = positions * bary;
		vec3 a = cross(positions[1] - positions[0], positions[2] - positions[0]);
		normal_light = a / length(a);
		float area = length(a);
		pdf *= 2.0 / area;
#endif
	}
}

#endif /*_LIGHT_LISTS_*/

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
