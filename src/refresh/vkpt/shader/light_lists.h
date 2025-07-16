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

mat3
project_triangle(mat3 positions, vec3 p)
{
	positions[0] = positions[0] - p;
	positions[1] = positions[1] - p;
	positions[2] = positions[2] - p;
	
	positions[0] = normalize(positions[0]);
	positions[1] = normalize(positions[1]);
	positions[2] = normalize(positions[2]);

	return positions;
}

float
projected_tri_area(mat3 positions, vec3 p, vec3 n, vec3 V, float phong_exp, float phong_scale, float phong_weight)
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
	float specular = phong(n, L, V, phong_exp) * phong_scale;
	float brdf = mix(1.0, specular, phong_weight);

	// Project triangle to unit sphere
	vec3 A = normalize(positions[0]);
	vec3 B = normalize(positions[1]);
	vec3 C = normalize(positions[2]);

	// Area of spherical triangle
	float area = 2 * atan(abs(dot(A, cross(B, C))), 1 + dot(A, B) + dot(B, C) + dot(A, C));
	float pa = max(area - 1e-5, 0.);
	return pa * brdf;
}

float
projected_sphere_area(mat3 positions, vec3 p, vec3 n, vec3 V, float phong_exp, float phong_scale, float phong_weight)
{
	vec3 position = positions[0] - p;
	float sphere_radius = positions[1].x;
	float dist = length(position);
	float rdist = 1.0 / dist;
	vec3 L = position * rdist;

	if (dot(n, L) <= 0)
		return 0.0;

	float specular = phong(n, L, V, phong_exp) * phong_scale;
	float brdf = mix(1.0, specular, phong_weight);

	float irradiance = 2 * (1 - sqrt(max(0, 1 - square(sphere_radius * rdist))));
	irradiance = min(irradiance,  2 * M_PI); //max solid angle

	return irradiance * brdf;
}

float
spot_falloff(in mat3 positions, in float cosTheta)
{
	float falloff;
	const uint spot_style = floatBitsToUint(positions[1].y);
	const uint spot_data = floatBitsToUint(positions[1].z);

	if(spot_style == DYNLIGHT_SPOT_EMISSION_PROFILE_FALLOFF) {
		const vec2 spot_falloff = unpackHalf2x16(spot_data);
		const float cosTotalWidth = spot_falloff.x;
		const float cosFalloffStart = spot_falloff.y;

		if(cosTheta < cosTotalWidth)
			falloff = 0;
		else if (cosTheta > cosFalloffStart)
			falloff = 1;
		else {
			float delta = (cosTheta - cosTotalWidth) / (cosFalloffStart - cosTotalWidth);
			falloff = (delta * delta) * (delta * delta);
		}
	} else if(spot_style == DYNLIGHT_SPOT_EMISSION_PROFILE_AXIS_ANGLE_TEXTURE) {
		const float theta = acos(cosTheta);
		const float totalWidth = unpackHalf2x16(spot_data).x;
		const uint texture_num = spot_data >> 16;

		if (cosTheta >= 0) {
			// Use the angle directly as texture coordinate for better angular resolution next to the center of the beam
			float tc = clamp(theta / totalWidth, 0, 1);
			falloff = global_texture(texture_num, vec2(tc, 0)).r;
		} else
			falloff = 0;
	}

	return falloff;
}

float
projected_spotlight_area(mat3 positions, vec3 p, vec3 n, vec3 V, float phong_exp, float phong_scale, float phong_weight)
{
	vec3 position = positions[0] - p;

	float dist = length(position);
	float rdist = 1.0 / dist;
	vec3 L = position * rdist;

	if (dot(n, L) <= 0)
		return 0.0;

	float specular = phong(n, L, V, phong_exp) * phong_scale;
	float brdf = mix(1.0, specular, phong_weight);

	float cosTheta = dot(-L, positions[2]); // cosine of angle to spot direction
	float falloff = spot_falloff(positions, cosTheta);

	float irradiance = 2 * falloff * square(rdist);

	irradiance = min(irradiance,  2 * M_PI); //max solid angle

	return irradiance * brdf;
}

float get_spherical_triangle_pdfw(mat3 positions)
{
	// Project triangle to unit sphere
	vec3 A = normalize(positions[0]);
	vec3 B = normalize(positions[1]);
	vec3 C = normalize(positions[2]);

	// Area of spherical triangle
	float area = 2 * atan(abs(dot(A, cross(B, C))), 1 + dot(A, B) + dot(B, C) + dot(A, C));

	// Since the solid angle is distributed uniformly, the PDF wrt to solid angle is simply:
	return 1 / area;
}

/* Sample a triangle, projected to a unit sphere.
 *
 * The implementation is based on the algorithm described in:
 * James Arvo. 1995. Stratified sampling of spherical triangles.
 * Proceedings of the 22nd annual conference on Computer graphics and interactive techniques (SIGGRAPH '95).
 * Association for Computing Machinery, New York, NY, USA, 437–438.
 * https://doi.org/10.1145/218380.218500
 */
vec3
sample_projected_triangle(vec3 pt, mat3 positions, vec2 rnd, out vec3 light_normal, out float pdfw)
{
	light_normal = cross(positions[1] - positions[0], positions[2] - positions[0]);
	light_normal = normalize(light_normal);

	// Use surface point as origin
	positions[0] = positions[0] - pt;
	positions[1] = positions[1] - pt;
	positions[2] = positions[2] - pt;

	// Distance of triangle to origin
	float o = dot(light_normal, positions[0]);

	// Project triangle to unit sphere
	vec3 A = normalize(positions[0]);
	vec3 B = normalize(positions[1]);
	vec3 C = normalize(positions[2]);
	// Planes passing through two vertices and origin. They'll be used to obtain the angles.
	vec3 cross_BC = cross(B, C);
	vec3 norm_AB = normalize(cross(A, B));
	vec3 norm_BC = normalize(cross_BC);
	vec3 norm_CA = normalize(cross(C, A));
	// Side of spherical triangle
	float cos_c = dot(A, B);
	// Angles at vertices
	float cos_alpha = dot(norm_AB, -norm_CA);

	// Area of spherical triangle. From: "On the Measure of Solid Angles", F. Eriksson, 1990.
	float area = 2 * atan(abs(dot(A, cross_BC)), 1 + cos_c + dot(B, C) + dot(A, C));

	// Use one random variable to select the new area.
	float new_area = rnd.x * area;

	float sin_alpha = sqrt(1 - cos_alpha * cos_alpha); // = sin(acos(cos_alpha))
	float sin_new_area = sin(new_area);
	float cos_new_area = cos(new_area);
	// Save the sine and cosine of the angle phi.
	float p = sin_new_area * cos_alpha - cos_new_area * sin_alpha;
	float q = cos_new_area * cos_alpha + sin_new_area * sin_alpha;

	// Compute the pair (u, v) that determines new_beta.
	float u = q - cos_alpha;
	float v = p + sin_alpha * cos_c;

	// Let cos_b be the cosine of the new edge length new_b.
	float cos_b = clamp(((v * q - u * p) * cos_alpha - v) / ((v * p + u * q) * sin_alpha), -1, 1);

	// Compute the third vertex of the sub-triangle.
	vec3 new_C = cos_b * A + sqrt(1 - cos_b * cos_b) * normalize(C - dot(C, A) * A);

	// Use the other random variable to select cos(phi).
	float z = 1 - rnd.y * (1 - dot(new_C, B));

	// Construct the corresponding point on the sphere.
	vec3 direction = z * B + sqrt(1 - z * z) * normalize(new_C - dot(new_C, B) * B);
	// ...which is also the direction!

	// Line-plane intersection
	vec3 lo = direction * (o / dot(light_normal, direction));

	// Since the solid angle is distributed uniformly, the PDF wrt to solid angle is simply:
	pdfw = 1 / area;

	return pt + lo;
}

vec3
sample_projected_sphere(vec3 p, mat3 positions, vec2 rnd, out vec3 light_normal, out float pdfw)
{
	vec3 light_center = positions[0];
	vec3 position = light_center - p;
	float sphere_radius = positions[1].x;
	float dist = length(position);
	float rdist = 1.0 / dist;
	vec3 L = position * rdist;

	float projected_area = 2 * (1 - sqrt(max(0, 1 - square(sphere_radius * rdist))));
	projected_area = min(projected_area,  2 * M_PI); //max solid angle
	pdfw = 1.0 / projected_area;

	mat3 onb = construct_ONB_frisvad(L);
	vec3 diskpt;
	diskpt.xy = sample_disk(rnd);
	diskpt.z = sqrt(max(0, 1 - diskpt.x * diskpt.x - diskpt.y * diskpt.y));

	vec3 position_light = light_center + (onb[0] * diskpt.x + onb[2] * diskpt.y - L * diskpt.z) * sphere_radius;

	light_normal = normalize(position_light - light_center);

	return position_light;
}

vec3
sample_projected_spotlight(vec3 p, mat3 positions, vec2 rnd, out vec3 light_normal, out float pdfw)
{
	vec3 light_center = positions[0];
	float emitter_radius = positions[1].x;

	mat3 onb = construct_ONB_frisvad(positions[2]);
	// Emit light from a small disk around the origin
	vec2 diskpt = sample_disk(rnd);
	vec3 position_light = light_center + (onb[0] * diskpt.x + onb[2] * diskpt.y) * emitter_radius;

	vec3 c = position_light - p;
	float dist = length(c);
	float rdist = 1.0 / dist;
	vec3 L = c * rdist;

	// Direction from emission point to surface, in a basis where +Y is the spot direction
	vec3 L_l = -L * onb;
	float cosTheta = L_l.y; // cosine of angle to spot direction
	float falloff = spot_falloff(positions, cosTheta);

	float projected_area = 2 * falloff * square(rdist);
	projected_area = min(projected_area,  2 * M_PI); //max solid angle
	pdfw = 1.0 / projected_area;

	light_normal = normalize(positions[2]);

	return position_light;
}

uint get_light_stats_addr(uint cluster, uint light, uint side)
{
	uint addr = cluster;
	addr = addr * global_ubo.num_static_lights + light;
	addr = addr * 6 + side;
	addr = addr * 2;
	return addr;
}

void
sample_lights(
		uint list_idx,
		vec3 p,
		vec3 n,
		vec3 gn,
		vec3 V, 
		float phong_exp, 
		float phong_scale,
		float phong_weight,
		bool is_gradient,
		out vec3 position_light,
		out vec3 light_color,
		out int light_index,
		out float pdfw,
		out bool is_sky_light,
		vec3 rng)
{
	position_light = vec3(0);
	light_index = -1;
	light_color = vec3(0);
	pdfw = 0;
	is_sky_light = false;

	if(list_idx == ~0u)
		return;

	uint list_start = light_buffer.light_list_offsets[list_idx];
	uint list_end   = light_buffer.light_list_offsets[list_idx + 1];
	/* The light count we base light selection on may differ from the current count
	 * to avoid gradient estimation breaking (see comment on light_counts_history).
	 * Obtain the frame number for the historical count from the RNG seed
	 * (which is also the historical RNG seed) */
	uint history_index = (rng_seed >> RNG_SEED_SHIFT_FRAME) % LIGHT_COUNT_HISTORY;
	uint light_count = light_counts_history[history_index].sample_light_counts[list_idx];

	float partitions = ceil(float(light_count) / float(MAX_BRUTEFORCE_SAMPLING));
	rng.x *= partitions;
	float fpart = min(floor(rng.x), partitions-1);
	rng.x -= fpart;
	list_start += int(fpart);
	int stride = int(partitions);

	float mass = 0.;

	float light_masses[MAX_BRUTEFORCE_SAMPLING];

	#pragma unroll
	for(uint i = 0, n_idx = list_start; i < MAX_BRUTEFORCE_SAMPLING; i++, n_idx += stride) {
		if (n_idx >= list_start + light_count)
			break;
		
		if(n_idx >= list_end)
		{
			light_masses[i] = 0;
			continue;
		}

		uint current_idx = light_buffer.light_list_lights[n_idx];

		// In case of polygon light overflow, the host code will still populate the light lists
		// with invalid indices. Skip those lights here, so they have pdf=0 and will not be selected.
		if (current_idx >= MAX_LIGHT_POLYS)
		{
			light_masses[i] = 0;
			continue;
		}

		LightPolygon light = get_light_polygon(current_idx);

		float m = 0.0f;
		switch(uint(light.type)){
			case LIGHT_POLYGON:
				m = projected_tri_area(light.positions, p, n, V, phong_exp, phong_scale, phong_weight);
				break;
			case LIGHT_SPHERE:
				m = projected_sphere_area(light.positions, p, n, V, phong_exp, phong_scale, phong_weight);
				break;
			case LIGHT_SPOT:
				m = projected_spotlight_area(light.positions, p, n, V, phong_exp, phong_scale, phong_weight);
				break;
		}

		float light_lum = luminance(light.color);

		// Apply light style scaling.
		// For gradient pixels, use the style from the previous frame here
		// in order to keep the CDF consistent and make sure that the same light is picked,
		// regardless of animations. This makes the image more stable around blinking lights,
		// especially in shadowed areas.
		light_lum *= is_gradient ? light.prev_style_scale : light.light_style_scale;	

		if(light_lum < 0 && global_ubo.environment_type == ENVIRONMENT_DYNAMIC)
		{
			// Set limits on sky luminance to avoid oversampling the sky in shadowed areas, or undersampling at dusk and dawn.
			// Note: the log -> linear conversion of the cvars happens on the CPU, in main.c
			m *= clamp(sun_color_ubo.sky_luminance, global_ubo.pt_min_log_sky_luminance, global_ubo.pt_max_log_sky_luminance);
		}
		else
			m *= abs(light_lum); // abs because sky lights have negative color

		// Apply CDF adjustment based on light shadowing statistics from one of the previous frames.
		// See comments in function `get_direct_illumination` in `path_tracer_rgen.h`
		if(global_ubo.pt_light_stats != 0 
			&& m > 0 
			&& current_idx < global_ubo.num_static_lights)
		{
			uint buffer_idx = global_ubo.current_frame_idx;
			// Regular pixels get shadowing stats from the previous frame;
			// Gradient pixels get the stats from two frames ago because they need to match
			// the light sampling from the previous frame.
			buffer_idx += is_gradient ? (NUM_LIGHT_STATS_BUFFERS - 2) : (NUM_LIGHT_STATS_BUFFERS - 1);
			buffer_idx = buffer_idx % NUM_LIGHT_STATS_BUFFERS;

			uint addr = get_light_stats_addr(list_idx, current_idx, get_primary_direction(n));

			uint num_hits = light_stats_bufers[buffer_idx].stats[addr];
			uint num_misses = light_stats_bufers[buffer_idx].stats[addr + 1];
			uint num_total = num_hits + num_misses;

			if(num_total > 0)
			{
				// Adjust the mass, but set a lower limit on the factor to avoid
				// extreme changes in the sampling.
				m *= max(float(num_hits) / float(num_total), 0.1);
			}
		}

		mass += m;
		light_masses[i] = m;
	}

	if (mass <= 0)
		return;

	rng.x *= mass;
	int current_idx = -1;
	mass *= partitions;
	float pdf = 0;

	#pragma unroll
	for(uint i = 0, n_idx = list_start; i < MAX_BRUTEFORCE_SAMPLING; i++, n_idx += stride) {
		if (n_idx >= list_start + light_count)
			break;
		pdf = light_masses[i];
		current_idx = int(n_idx);
		rng.x -= pdf;

		if (rng.x <= 0)
			break;
	}

	if(rng.x > 0)
		return;

	pdf /= mass;

	// assert: current_idx >= 0?
	if (current_idx >= 0) {
		current_idx = int(light_buffer.light_list_lights[current_idx]);

		LightPolygon light = get_light_polygon(current_idx);

		vec3 light_normal;

		switch(uint(light.type)){
			case LIGHT_POLYGON:
				position_light = sample_projected_triangle(p, light.positions, rng.yz, light_normal, pdfw);
				break;
			case LIGHT_SPHERE:
				position_light = sample_projected_sphere(p, light.positions, rng.yz, light_normal, pdfw);
				break;
			case LIGHT_SPOT:
				position_light = sample_projected_spotlight(p, light.positions, rng.yz, light_normal, pdfw);
				break;
		}

		vec3 L = normalize(position_light - p);

		if(dot(L, gn) <= 0)
			pdfw = 0;

		if (pdfw > 0)
		{
			float LdotNL = max(0, -dot(light_normal, L));
			float spotlight = sqrt(LdotNL);
			float inv_pdfw = 1.0 / pdfw;

			if(light.color.r >= 0)
			{
				light_color = light.color * (inv_pdfw * spotlight * light.light_style_scale);
			}
			else
			{
				light_color = env_map(L, true) * inv_pdfw * global_ubo.pt_env_scale;
				is_sky_light = true;
			}
		}

		light_index = current_idx;
	}

	light_color /= pdf;
}

#endif /*_LIGHT_LISTS_*/

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
