/*
Copyright (C) 2018 Christoph Schied
Copyright (C) 2018 Tobias Zirr
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.
Copyright (C) 2022 Jorge Gustavo Martinez

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

// ========================================================================== //
// This rgen shader computes direct lighting for the first opaque surface.
// The parameters of that surface are loaded from the G-buffer, stored there
// previously by the `primary_rays.rgen` and `reflect_refract.rgen` shaders.
//
// See `path_tracer.h` for an overview of the path tracer.
// ========================================================================== //

#ifndef  _RESTIR_H_
#define  _RESTIR_H_

#include "path_tracer_rgen.h"

#define RESTIR_INVALID_ID       0xFFFF
#define RESTIR_ENV_ID           0xFFFE

#define RESTIR_SPACIAL_DISTANCE 32
#define RESTIR_SPACIAL_SAMPLES  8

#define RESTIR_SAMPLING_M       4
#define RESTIR_M_CLAMP          32
#define RESTIR_M_VC_CLAMP       16

struct Reservoir
{
    uint y;
    uint M;
    float w_sum;
    float W;
    float p_hat;
    vec2 y_pos;
};

void
init_reservoir(inout Reservoir r)
{
	r.y = RESTIR_INVALID_ID;
	r.M = 0;
	r.w_sum = 0.0;
	r.W = 0.0;
	r.p_hat = 0.0;
	r.y_pos = vec2(0.0);
}

bool
update_reservoir(uint xi, float wi, vec2 xi_pos, uint M, float p_hat, inout float rng, inout Reservoir r)
{
	r.w_sum += wi;
	r.M += M;
	float p_s = r.w_sum > 0.0 ? (wi/r.w_sum) : 0.0;
	if(rng < p_s)
	{
		r.y = xi;
		r.y_pos = xi_pos;
		r.p_hat = p_hat;
		rng /= p_s;
		return true;
	}
	else
	{
		rng = (rng - p_s) / (1.0f - p_s);
		return false;
	}
}

uvec4
pack_reservoir(Reservoir r)
{
	uvec4 vec;
	r.W = r.y == RESTIR_INVALID_ID ? 0.0 : r.W;
	vec.x = packHalf2x16(vec2(r.W, r.W));
	vec.x = (vec.x & 0xFFFF0000) | r.y;
	vec.y = packHalf2x16(r.y_pos);
	return vec;
}

void
unpack_reservoir(uvec4 packed, out Reservoir r)
{
	vec2 val = unpackHalf2x16(packed.x);
	r.y = packed.x & 0xFFFF;
	r.W = val.y;
	if(isnan(r.W) || isinf(r.W)) r.W = 0.0;
	r.y_pos = unpackHalf2x16(packed.y);
	r.p_hat = 0.0;
	r.M = r.y == RESTIR_INVALID_ID ? 0 : (global_ubo.pt_restir != 3 ? RESTIR_M_CLAMP : RESTIR_M_VC_CLAMP);
	r.w_sum = 0;
}

// Functions

uint
get_light_current_idx(uint index)
{
	if (index < global_ubo.num_static_lights || index == RESTIR_INVALID_ID || index == RESTIR_ENV_ID)
	{
		return index;
	}
	else
	{
		uint light_id_curr = instance_buffer.mlight_prev_to_current[index - global_ubo.num_static_lights];
		if(light_id_curr != ~0u) return light_id_curr + global_ubo.num_static_lights;
		else
		{
			return RESTIR_INVALID_ID;
		}
	}
}


float
get_unshadowed_path_contrib(
		uint light_idx,
		vec3 position,
		vec3 normal,
		vec3 view_direction,
		float phong_exp,
		float phong_scale,
		float phong_weight,
		vec2 rng)
{
	if(light_idx == RESTIR_ENV_ID) return get_unshadowed_env_path_contrib(normal,view_direction, phong_exp, phong_scale, phong_weight, rng);
	LightPolygon light = get_light_polygon(light_idx);

	float m = 0.0f;
	switch(uint(light.type))
	{
		case LIGHT_POLYGON:
			m = projected_tri_area(light.positions, position, normal, view_direction, phong_exp, phong_scale, phong_weight);
			break;
		case LIGHT_SPHERE:
			m = projected_sphere_area(light.positions, position, normal, view_direction, phong_exp, phong_scale, phong_weight);
			break;
		case LIGHT_SPOT:
			m = projected_spotlight_area(light.positions, position, normal, view_direction, phong_exp, phong_scale, phong_weight);
			break;
	}

	float light_lum = luminance(light.color);

	// Apply light style scaling.
	light_lum *= light.light_style_scale;

	if(light_lum < 0 && global_ubo.environment_type == ENVIRONMENT_DYNAMIC)
	{
		// Set limits on sky luminance to avoid oversampling the sky in shadowed areas, or undersampling at dusk and dawn.
		// Note: the log -> linear conversion of the cvars happens on the CPU, in main.c
		m *= clamp(sun_color_ubo.sky_luminance, global_ubo.pt_min_log_sky_luminance, global_ubo.pt_max_log_sky_luminance);
	}
	else
		m *= abs(light_lum); // abs because sky lights have negative color

	return m;
}


void
process_selected_light_restir(
		uint light_idx,
		vec2 light_position,
		float weight,
		vec3 position,
		vec3 normal,
		vec3 geo_normal,
		int shadow_cull_mask,
		vec3 view_direction,
		vec3 base_reflectivity,
		float specular_factor,
		float roughness,
		int surface_medium,
		bool enable_caustics,
		float direct_specular_weight,
		float phong_exp,
		float phong_scale,
		float phong_weight,
		bool check_vis,
		uint cluster_idx,
		out vec3 diffuse,
		out vec3 specular,
		out float vis)
{
	float polygonal_light_pdfw = 0;
	vec3 contrib_polygonal = vec3(0);
	vec3 L, pos_on_light_polygonal;
	bool polygonal_light_is_sky = false;
	diffuse = vec3(0);
	specular = vec3(0);
	vis = 1.0;

	if(light_idx != RESTIR_ENV_ID)
	{
		LightPolygon light = get_light_polygon(light_idx);

		vec3 light_normal;

		switch(uint(light.type))
		{
			case LIGHT_POLYGON:
				pos_on_light_polygonal = sample_projected_triangle(position, light.positions, light_position , light_normal, polygonal_light_pdfw);
				break;
			case LIGHT_SPHERE:
				pos_on_light_polygonal = sample_projected_sphere(position, light.positions, light_position , light_normal, polygonal_light_pdfw);
				break;
			case LIGHT_SPOT:
				pos_on_light_polygonal = sample_projected_spotlight(position, light.positions, light_position , light_normal, polygonal_light_pdfw);
				break;
		}

		L = normalize(pos_on_light_polygonal - position);

		if(dot(L, geo_normal) <= 0)
			polygonal_light_pdfw = 0;

		if(polygonal_light_pdfw > 0){
			float LdotNL = max(0, -dot(light_normal, L));
			float spotlight = sqrt(LdotNL);
			float inv_pdfw = 1.0 / polygonal_light_pdfw;

			if(light.color.r >= 0)
			{
				contrib_polygonal = light.color * (inv_pdfw * spotlight * light.light_style_scale);
			}
			else
			{
				contrib_polygonal = env_map(L, true) * inv_pdfw * global_ubo.pt_env_scale;
				polygonal_light_is_sky = true;
			}
		}

	}
	else
	{
		vec2 disk = sample_disk(light_position);
		disk.xy *= global_ubo.sun_tan_half_angle;
		L = normalize(global_ubo.sun_direction + global_ubo.sun_tangent * disk.x + global_ubo.sun_bitangent * disk.y);
		polygonal_light_pdfw = global_ubo.sun_solid_angle;
		pos_on_light_polygonal = position + L * 10000;
		contrib_polygonal = env_map(L, false) * polygonal_light_pdfw * global_ubo.pt_env_scale;
	}

	contrib_polygonal *= weight;

	float spec_polygonal = phong(normal, L, view_direction, phong_exp) * phong_scale;

	float l_polygonal  = luminance(abs(contrib_polygonal)) * mix(1, spec_polygonal, phong_weight);

	bool null_light = (l_polygonal == 0);

	Ray shadow_ray = get_shadow_ray(position - view_direction * 0.01, pos_on_light_polygonal, 0);

	if(check_vis) vis *= trace_shadow_ray(shadow_ray, null_light ? 0 : shadow_cull_mask);

	#ifdef ENABLE_SHADOW_CAUSTICS
		if(enable_caustics)
		{
			contrib_polygonal *= trace_caustic_ray(shadow_ray, surface_medium);
		}
	#endif

	if(null_light)
	{
		vis = 0.0f;
		return;
	}

	vec3 radiance = vis * contrib_polygonal;

	if(direct_specular_weight > 0 && polygonal_light_is_sky && global_ubo.pt_specular_mis != 0)
	{
		// MIS with direct specular and indirect specular.
		// Only applied to sky lights, for two reasons:
		//  1) Non-sky lights are trimmed to match the light texture, and indirect rays don't see that;
		//  2) Non-sky lights are usually away from walls, so the direct sampling issue is not as pronounced.

		direct_specular_weight *= get_specular_sampled_lighting_weight(roughness,
			normal, -view_direction, L, polygonal_light_pdfw);
	}

	vec3 F = vec3(0);

	if(vis > 0 && direct_specular_weight > 0)
	{
		vec3 specular_brdf = GGX_times_NdotL(view_direction, L,
			normal, roughness, base_reflectivity, 0.0, specular_factor, F);
		specular = radiance * specular_brdf * direct_specular_weight;
	}

	float NdotL = max(0, dot(normal, L));

	float diffuse_brdf = NdotL / M_PI;
	diffuse = radiance * diffuse_brdf * (vec3(1.0) - F);
}


void
get_direct_illumination_restir(
	vec3 position,
	vec3 normal,
	uint cluster_idx,
	vec3 view_direction,
	float phong_exp,
	float phong_scale,
	float phong_weight,
	int bounce,
	Reservoir prev_r,
	out Reservoir reservoir)
{
	init_reservoir(reservoir);

	if(cluster_idx == ~0u)
		return;

	vec3 contrib_polygonal = vec3(0);

	float rng, p_hat;

	uint list_start = light_buffer.light_list_offsets[cluster_idx];
	uint list_end   = light_buffer.light_list_offsets[cluster_idx + 1];

	rng = get_rng(RNG_NEE_LIGHT_SELECTION(bounce));

	uint add_sun = (global_ubo.sun_visible != 0) && ((cluster_idx == ~0u) || (light_buffer.sky_visibility[cluster_idx >> 5] & (1 << (cluster_idx & 31))) != 0) ? 1 : 0;

	uint sun_idx = add_sun > 0 ? list_end : -1;
	list_end += add_sun;
	float list_size = float(list_end - list_start);
	float partitions = ceil(list_size / float(RESTIR_SAMPLING_M));
	float inv_pdf = list_size;
	float rng_part = rng * partitions;
	float fpart = min(floor(rng_part), partitions-1);

	list_start += int(fpart);
	int stride = int(partitions);
	rng = rng_part - floor(rng_part);

	uint current_idx, current_light_idx;

	vec2 rng2 = vec2(
		get_rng(RNG_NEE_TRI_X(bounce)),
		get_rng(RNG_NEE_TRI_Y(bounce)));

	float samples = 1.;

	#pragma unroll
	for(uint i = 0, n_idx = list_start; i < RESTIR_SAMPLING_M; i++, n_idx += stride)
	{
		if (n_idx >= list_end)
			break;

		current_light_idx = n_idx != sun_idx ? light_buffer.light_list_lights[n_idx] : RESTIR_ENV_ID;

		if(current_light_idx == ~0u) continue;

		p_hat = get_unshadowed_path_contrib(current_light_idx, position, normal, view_direction, phong_exp, phong_scale, phong_weight, rng2);
		if(p_hat > 0)
			update_reservoir(current_light_idx, p_hat * inv_pdf, rng2, 1, p_hat, rng, reservoir);
	}

	reservoir.M = RESTIR_SAMPLING_M;

	//Combine with temporal
	if(prev_r.y != RESTIR_INVALID_ID)
	{
		update_reservoir(prev_r.y, prev_r.p_hat * prev_r.W * prev_r.M ,prev_r.y_pos, prev_r.M, prev_r.p_hat, rng, reservoir);
	}

	reservoir.W = reservoir.w_sum / (reservoir.p_hat * reservoir.M);
	if(isnan(reservoir.W) || isinf(reservoir.W)) reservoir.W = 0.0;
}


#endif  /*_RESTIR_H_*/
