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

#include "path_tracer.h"
#include "utils.glsl"

#define RAY_GEN_DESCRIPTOR_SET_IDX 0
layout(set = RAY_GEN_DESCRIPTOR_SET_IDX, binding = 0)
uniform accelerationStructureNV topLevelAS;

#define GLOBAL_TEXTURES_DESC_SET_IDX 2
#include "global_textures.h"

#define VERTEX_BUFFER_DESC_SET_IDX 3
#define VERTEX_READONLY 1
#include "vertex_buffer.h"

#include "read_visbuf.glsl"
#include "asvgf.glsl"
#include "brdf.glsl"
#include "water.glsl"

#define DESATURATE_ENVIRONMENT_MAP 1

#define RNG_PRIMARY_OFF_X   0
#define RNG_PRIMARY_OFF_Y   1

#define RNG_NEE_LH(bounce)                (2 + 0 + 9 * bounce)
#define RNG_NEE_TRI_X(bounce)             (2 + 1 + 9 * bounce)
#define RNG_NEE_TRI_Y(bounce)             (2 + 2 + 9 * bounce)
#define RNG_NEE_STATIC_DYNAMIC(bounce)    (2 + 3 + 9 * bounce)
#define RNG_BRDF_X(bounce)                (2 + 4 + 9 * bounce)
#define RNG_BRDF_Y(bounce)                (2 + 5 + 9 * bounce)
#define RNG_BRDF_FRESNEL(bounce)          (2 + 6 + 9 * bounce)
#define RNG_SUNLIGHT_X(bounce)			  (2 + 7 + 9 * bounce)
#define RNG_SUNLIGHT_Y(bounce)			  (2 + 8 + 9 * bounce)

#define PRIMARY_RAY_CULL_MASK        (AS_FLAG_EVERYTHING & ~(AS_FLAG_VIEWER_MODELS))
#define REFLECTION_RAY_CULL_MASK     (AS_FLAG_OPAQUE_STATIC | AS_FLAG_OPAQUE_DYNAMIC | AS_FLAG_PARTICLES | AS_FLAG_EXPLOSIONS | AS_FLAG_SKY)
#define BOUNCE_RAY_CULL_MASK         (AS_FLAG_OPAQUE_STATIC | AS_FLAG_OPAQUE_DYNAMIC | AS_FLAG_SKY)
#define SHADOW_RAY_CULL_MASK         (AS_FLAG_OPAQUE_STATIC | AS_FLAG_OPAQUE_DYNAMIC)

/* no BRDF sampling in last bounce */
#define NUM_RNG_PER_FRAME (RNG_NEE_STATIC_DYNAMIC(1) + 1)

#define BOUNCE_SPECULAR 1

#define DYNAMIC_LIGHT_INTENSITY_DIFFUSE 20

#define MAX_OUTPUT_VALUE 1000

#define RT_PAYLOAD_SHADOW  0
#define RT_PAYLOAD_BRDF 1
layout(location = RT_PAYLOAD_SHADOW) rayPayloadNV RayPayloadShadow ray_payload_shadow;
layout(location = RT_PAYLOAD_BRDF) rayPayloadNV RayPayload ray_payload_brdf;

uint rng_seed;

struct Ray {
	vec3 origin, direction;
	float t_min, t_max;
};

vec3
env_map(vec3 direction, bool remove_sun)
{
	direction = (global_ubo.environment_rotation_matrix * vec4(direction, 0)).xyz;

    vec3 envmap = vec3(0);
    if (global_ubo.environment_type == ENVIRONMENT_DYNAMIC)
    {
	    envmap = textureLod(TEX_PHYSICAL_SKY, direction.xzy, 0).rgb;

	    if(remove_sun)
	    {
			// roughly remove the sun from the env map
			envmap = min(envmap, vec3((1 - dot(direction, global_ubo.sun_direction_envmap)) * 200));
		}
	}
    else if (global_ubo.environment_type == ENVIRONMENT_STATIC)
    {
        envmap = textureLod(TEX_ENVMAP, direction.xzy, 0).rgb;
#if DESATURATE_ENVIRONMENT_MAP
        float avg = (envmap.x + envmap.y + envmap.z) / 3.0;
        envmap = mix(envmap, avg.xxx, 0.1) * 0.5;
#endif
    }
	return envmap;
}

// depends on env_map
#include "light_lists.h"

ivec2 get_image_position()
{
	ivec2 pos;

	bool is_even_checkerboard = push_constants.gpu_index == 0 || push_constants.gpu_index < 0 && gl_LaunchIDNV.z == 0;
	if((global_ubo.current_frame_idx & 1) != 0)
		is_even_checkerboard = !is_even_checkerboard;

	if (is_even_checkerboard) {
		pos.x = int(gl_LaunchIDNV.x * 2) + int(gl_LaunchIDNV.y & 1);
	} else {
		pos.x = int(gl_LaunchIDNV.x * 2 + 1) - int(gl_LaunchIDNV.y & 1);
	}

	pos.y = int(gl_LaunchIDNV.y);
	return pos;
}

ivec2 get_image_size()
{
	return ivec2(global_ubo.width, global_ubo.height);
}

bool
found_intersection(RayPayload rp)
{
	return rp.instance_prim != ~0u;
}

bool
is_sky(RayPayload rp)
{
	return (rp.instance_prim & INSTANCE_SKY_FLAG) != 0;
}

bool
is_dynamic_instance(RayPayload pay_load)
{
	return (pay_load.instance_prim & INSTANCE_DYNAMIC_FLAG) > 0;
}

uint
get_primitive(RayPayload pay_load)
{
	return pay_load.instance_prim & PRIM_ID_MASK;
}

Triangle
get_hit_triangle(RayPayload rp)
{
	uint prim = get_primitive(rp);

	return is_dynamic_instance(rp)
		?  get_instanced_triangle(prim)
		:  get_bsp_triangle(prim);
}

vec3
get_hit_barycentric(RayPayload rp)
{
	vec3 bary;
	bary.yz = rp.barycentric;
	bary.x  = 1.0 - bary.y - bary.z;
	return bary;
}

float
get_rng(uint idx)
{
	uvec3 p = uvec3(rng_seed, rng_seed >> 10, rng_seed >> 20);
	p.z = (p.z * NUM_RNG_PER_FRAME + idx);
	p &= uvec3(BLUE_NOISE_RES - 1, BLUE_NOISE_RES - 1, NUM_BLUE_NOISE_TEX - 1);

	return min(texelFetch(TEX_BLUE_NOISE, ivec3(p), 0).r, 0.9999999999999);
	//return fract(vec2(get_rng_uint(idx)) / vec2(0xffffffffu));
}

bool
is_water(uint material)
{
	return (material & MATERIAL_KIND_MASK) == MATERIAL_KIND_WATER;
}

bool
is_slime(uint material)
{
	return (material & MATERIAL_KIND_MASK) == MATERIAL_KIND_SLIME;
}

bool
is_lava(uint material)
{
	return (material & MATERIAL_KIND_MASK) == MATERIAL_KIND_LAVA;
}

bool
is_glass(uint material)
{
	return (material & MATERIAL_KIND_MASK) == MATERIAL_KIND_GLASS;
}

bool
is_transparent(uint material)
{
	return (material & MATERIAL_KIND_MASK) == MATERIAL_KIND_TRANSPARENT;
}

bool
is_chrome(uint material)
{
	return (material & MATERIAL_KIND_MASK) == MATERIAL_KIND_CHROME;
}

bool
is_screen(uint material)
{
	return (material & MATERIAL_KIND_MASK) == MATERIAL_KIND_SCREEN;
}
void
trace_ray(Ray ray, bool cull_back_faces, int instance_mask)
{
	uint rayFlags = 0;
	if(cull_back_faces)
		rayFlags |=gl_RayFlagsCullBackFacingTrianglesNV;
	
	ray_payload_brdf.transparency = uvec2(0);
    ray_payload_brdf.hit_distance = 0;
    ray_payload_brdf.max_transparent_distance = 0;

	traceNV( topLevelAS, rayFlags, instance_mask,
			SBT_RCHIT_OPAQUE /*sbtRecordOffset*/, 0 /*sbtRecordStride*/, SBT_RMISS_PATH_TRACER /*missIndex*/,
			ray.origin, ray.t_min, ray.direction, ray.t_max, RT_PAYLOAD_BRDF);
}

Ray get_shadow_ray(vec3 p1, vec3 p2, float tmin)
{
	vec3 l = p2 - p1;
	float dist = length(l);
	l /= dist;

	Ray ray;
	ray.origin = p1 + l * tmin;
	ray.t_min = 0;
	ray.t_max = dist - tmin - 0.01;
	ray.direction = l;

	return ray;
}

float
trace_shadow_ray(Ray ray, int cull_mask)
{
	const uint rayFlags = gl_RayFlagsOpaqueNV | gl_RayFlagsTerminateOnFirstHitNV | gl_RayFlagsCullBackFacingTrianglesNV;

	ray_payload_shadow.missed = 0;

	traceNV( topLevelAS, rayFlags, cull_mask,
			SBT_RCHIT_EMPTY /*sbtRecordOffset*/, 0 /*sbtRecordStride*/, SBT_RMISS_SHADOW /*missIndex*/,
			ray.origin, ray.t_min, ray.direction, ray.t_max, RT_PAYLOAD_SHADOW);

	return float(ray_payload_shadow.missed);
}

vec3
trace_caustic_ray(Ray ray, int surface_medium)
{
	ray_payload_brdf.hit_distance = -1;

	traceNV(topLevelAS, gl_RayFlagsCullBackFacingTrianglesNV, AS_FLAG_TRANSPARENT,
		SBT_RCHIT_OPAQUE, 0, SBT_RMISS_PATH_TRACER,
			ray.origin, ray.t_min, ray.direction, ray.t_max, RT_PAYLOAD_BRDF);

	float extinction_distance = ray.t_max - ray.t_min;
	float caustic = 1;

	if(found_intersection(ray_payload_brdf))
	{
		Triangle triangle = get_hit_triangle(ray_payload_brdf);
		if(is_water(triangle.material_id) || is_slime(triangle.material_id))
		{
			vec3 geo_normal = triangle.normals[0];
			vec3 position = ray.origin + ray.direction * ray_payload_brdf.hit_distance;
			vec3 w = get_water_normal(triangle.material_id, geo_normal, triangle.tangent, position, true);

			caustic = mix(1, clamp(length(w.xz) * 70, 0, 2), clamp(ray_payload_brdf.hit_distance * 0.02, 0, 1));

			if(abs(geo_normal.z) > 0.9)
			{
				if(surface_medium != MEDIUM_NONE)
				{
					extinction_distance = ray_payload_brdf.hit_distance;
				}
				else
				{
					if(is_water(triangle.material_id))
						surface_medium = MEDIUM_WATER;
					else
						surface_medium = MEDIUM_SLIME;

					extinction_distance = max(0, ray.t_max - ray_payload_brdf.hit_distance);
				}
			}
		}
	}

	//return vec3(caustic);
	return extinction(surface_medium, extinction_distance) * caustic;
}

vec3 rgbToNormal(vec3 rgb, out float len)
{
    vec3 n = vec3(rgb.xy * 2 - 1, rgb.z);

    len = length(n);
    return len > 0 ? n / len : vec3(0);
}


// ================================================================================================
// Converts a Beckmann roughness parameter to a Phong specular power
// ================================================================================================
float RoughnessToSpecPower(in float m) {
    return 2.0f / (m * m) - 2.0f;
}

// ================================================================================================
// Converts a Blinn-Phong specular power to a Beckmann roughness parameter
// ================================================================================================
float SpecPowerToRoughness(in float s) {
    return sqrt(2.0f / (s + 2.0f));
}

float
AdjustRoughnessToksvig(float roughness, float normalMapLen)
{
    float shininess = RoughnessToSpecPower(roughness) * global_ubo.pt_toksvig;
    float ft = normalMapLen / mix(shininess, 1.0f, normalMapLen);
    ft = max(ft, 0.01f);
    return SpecPowerToRoughness(ft * shininess / global_ubo.pt_toksvig);
}

vec3
compute_direct_illumination_static(vec3 position, vec3 normal, vec3 geo_normal, vec3 view_direction, float phong_exp, float phong_weight, int bounce, uint cluster, out vec3 pos_on_light)
{
	float pdf;
	vec3 light_color;
	vec3 light_normal;

	sample_light_list(
			cluster,
			position,
			normal,
			geo_normal,
			view_direction,
			phong_exp,
			phong_weight,
			pos_on_light,
			light_color,
			light_normal,
			pdf,
			vec3(
				get_rng(RNG_NEE_LH(bounce)),
				get_rng(RNG_NEE_TRI_X(bounce)),
				get_rng(RNG_NEE_TRI_Y(bounce)))
			);

	if(pdf == 0)
		return vec3(0);

	return light_color / pdf;
}

vec3
compute_direct_illumination_dynamic(vec3 position, vec3 normal, vec3 geo_normal, uint bounce, out vec3 pos_on_light)
{
	if(global_ubo.num_lights == 0)
		return vec3(0);

	float random_light = get_rng(RNG_NEE_LH(bounce)) * global_ubo.num_lights;
	uint light_idx = min(global_ubo.num_lights - 1, uint(random_light));
	
	vec3 light_color;
	sample_light_list_dynamic(
		light_idx,
		position,
		normal,
		geo_normal,
		pos_on_light,
		light_color,
		vec2(get_rng(RNG_NEE_TRI_X(bounce)), get_rng(RNG_NEE_TRI_Y(bounce))));

    light_color *= DYNAMIC_LIGHT_INTENSITY_DIFFUSE;

    return light_color * float(global_ubo.num_lights);
}

void
get_direct_illumination(
	vec3 position, 
	vec3 normal, 
	vec3 geo_normal, 
	uint cluster_idx, 
	uint material_id,
	int shadow_cull_mask, 
	vec3 view_direction, 
	float roughness, 
	int surface_medium, 
	bool enable_caustics, 
	float surface_specular, 
	float direct_specular_weight, 
	bool enable_static,
	bool enable_dynamic,
	out vec3 diffuse,
	out vec3 specular)
{
	diffuse = vec3(0);
	specular = vec3(0);

	vec3 pos_on_light_static;
	vec3 pos_on_light_dynamic;

	vec3 contrib_static = vec3(0);
	vec3 contrib_dynamic = vec3(0);

	float phong_exp = RoughnessToSpecPower(roughness);
	float phong_weight = surface_specular * direct_specular_weight;

	/* static illumination */
	if(enable_static) {
		contrib_static = compute_direct_illumination_static(position, normal, geo_normal, view_direction, phong_exp, phong_weight, 0, cluster_idx, pos_on_light_static);
	}

	bool is_static = true;
	float vis = 1;

	/* dynamic illumination */
	if(enable_dynamic) {
		contrib_dynamic = compute_direct_illumination_dynamic(position, normal, geo_normal, 0, pos_on_light_dynamic);
	}

	float l_static  = luminance(abs(contrib_static));
	float l_dynamic = luminance(abs(contrib_dynamic));
	float l_sum = l_static + l_dynamic;

	bool null_light = (l_sum == 0);

	float w = null_light ? 0.5 : l_static / (l_static + l_dynamic);

	float rng = get_rng(RNG_NEE_STATIC_DYNAMIC(0));
	is_static = (rng < w);
	vis = is_static ? (1 / w) : (1 / (1 - w));
	vec3 pos_on_light = null_light ? position : (is_static ? pos_on_light_static : pos_on_light_dynamic);
	vec3 contrib = is_static ? contrib_static : contrib_dynamic;

	// Surfaces marked with this flag are double-sided, so use a positive ray offset
	float min_t = (material_id & (MATERIAL_FLAG_WARP | MATERIAL_FLAG_DOUBLE_SIDED)) != 0 ? 0.01 : -0.01;
	Ray shadow_ray = get_shadow_ray(position, pos_on_light, min_t);

	vis *= trace_shadow_ray(shadow_ray, null_light ? 0 : shadow_cull_mask);
#ifdef ENABLE_SHADOW_CAUSTICS
	if(enable_caustics)
	{
		contrib *= trace_caustic_ray(shadow_ray, surface_medium);
	}
#endif

	if(null_light)
		return;

	diffuse = vis * contrib;

	if(vis > 0 && direct_specular_weight > 0)
	{
		specular = diffuse * (GGX(view_direction, normalize(pos_on_light - position), normal, roughness, 0.0) * direct_specular_weight);
	}

	vec3 L = pos_on_light - position;
	L = normalize(L);

	float NdotL = max(0, dot(normal, L));

	diffuse *= NdotL;
}

void
get_sunlight(
	uint cluster_idx, 
	uint material_id,
	vec3 position, 
	vec3 normal, 
	vec3 geo_normal, 
	vec3 view_direction, 
	float roughness, 
	int surface_medium, 
	bool enable_caustics, 
	out vec3 diffuse, 
	out vec3 specular, 
	int shadow_cull_mask)
{
	diffuse = vec3(0);
	specular = vec3(0);

	if(global_ubo.sun_visible == 0)
		return;

	bool visible = (cluster_idx == ~0u) || (get_sky_visibility(cluster_idx >> 5) & (1 << (cluster_idx & 31))) != 0;

	if(!visible)
		return;

	vec2 rng3 = vec2(get_rng(RNG_SUNLIGHT_X(0)), get_rng(RNG_SUNLIGHT_Y(0)));
	vec2 disk = sample_disk(rng3);
	disk.xy *= global_ubo.sun_tan_half_angle;

	vec3 direction = normalize(global_ubo.sun_direction + global_ubo.sun_tangent * disk.x + global_ubo.sun_bitangent * disk.y);

	float NdotL = dot(direction, normal);
	float GNdotL = dot(direction, geo_normal);

	if(NdotL <= 0 || GNdotL <= 0)
		return;

	float min_t = (material_id & MATERIAL_FLAG_DOUBLE_SIDED) != 0 ? 0.01 : -0.01;
	Ray shadow_ray = get_shadow_ray(position, position + direction * 10000, min_t);
 
	float vis = trace_shadow_ray(shadow_ray, shadow_cull_mask);

	if(vis == 0)
		return;

#ifdef ENABLE_SUN_SHAPE
	// Fetch the sun color from the environment map. 
	// This allows us to get properly shaped shadows from the sun that is partially occluded
	// by clouds or landscape.

	vec3 envmap_direction = (global_ubo.environment_rotation_matrix * vec4(direction, 0)).xyz;
	
    vec3 envmap = textureLod(TEX_PHYSICAL_SKY, envmap_direction.xzy, 0).rgb;

    diffuse = (global_ubo.sun_solid_angle * global_ubo.pt_env_scale) * envmap;
#else
    // Fetch the average sun color from the resolved UBO - it's faster.

    diffuse = sun_color_ubo.sun_color;
#endif

#ifdef ENABLE_SHADOW_CAUSTICS
	if(enable_caustics)
	{
    	diffuse *= trace_caustic_ray(shadow_ray, surface_medium);
	}
#endif

    if(global_ubo.pt_sun_specular > 0)
    {
		float NoH_offset = 0.5 * square(global_ubo.sun_tan_half_angle);
    	specular = diffuse * GGX(view_direction, global_ubo.sun_direction, normal, roughness, NoH_offset);
	}

	diffuse *= NdotL;
}

vec3 clamp_output(vec3 c)
{
	if(any(isnan(c)) || any(isinf(c)))
		return vec3(0);
	else 
		return clamp(c, vec3(0), vec3(MAX_OUTPUT_VALUE));
}

vec3
correct_albedo(vec3 albedo)
{
    return max(vec3(0), pow(albedo, vec3(ALBEDO_TRANSFORM_POWER)) * ALBEDO_TRANSFORM_SCALE + vec3(ALBEDO_TRANSFORM_BIAS));
}

vec3
correct_emissive(uint material_id, vec3 emissive)
{
	if((material_id & MATERIAL_FLAG_LIGHT) == 0)
		return max(vec3(0), emissive.rgb + vec3(EMISSIVE_TRANSFORM_BIAS));
	else
		return max(vec3(0), pow(emissive.rgb, vec3(EMISSIVE_TRANSFORM_POWER)) + vec3(EMISSIVE_TRANSFORM_BIAS));
}

vec3
sample_emissive_texture(uint material_id, MaterialInfo minfo, vec2 tex_coord, vec2 tex_coord_x, vec2 tex_coord_y, float mip_level)
{
	if (minfo.emissive_texture != 0)
    {
        vec4 image3;
	    if (mip_level >= 0)
	        image3 = global_textureLod(minfo.emissive_texture, tex_coord, mip_level);
	    else
	        image3 = global_textureGrad(minfo.emissive_texture, tex_coord, tex_coord_x, tex_coord_y);

    	vec3 corrected = correct_emissive(material_id, image3.rgb);

	    return corrected * minfo.emissive_scale;
	}

	return vec3(0);
}

vec2
lava_uv_warp(vec2 uv)
{
	// Lava UV warp that (hopefully) matches the warp in the original Quake 2.
	// Relevant bits of the original rasterizer:

	// #define AMP     8*0x10000
	// #define SPEED   20
	// #define CYCLE   128
	// sintable[i] = AMP + sin(i * M_PI * 2 / CYCLE) * AMP; 
	// #define TURB_SIZE               64  // base turbulent texture size
	// #define TURB_MASK               (TURB_SIZE - 1)
	// turb_s = ((s + turb[(t >> 16) & (CYCLE - 1)]) >> 16) & TURB_MASK;
    // turb_t = ((t + turb[(s >> 16) & (CYCLE - 1)]) >> 16) & TURB_MASK;
    
    return uv.xy + sin(fract(uv.yx * 0.5 + global_ubo.time * 20 / 128) * 2 * M_PI) * 0.125;
}

vec3 get_emissive_shell(uint material_id)
{
	vec3 c = vec3(0);

	if((material_id & (MATERIAL_FLAG_SHELL_RED | MATERIAL_FLAG_SHELL_GREEN | MATERIAL_FLAG_SHELL_BLUE)) != 0)
	{ 
	    if((material_id & MATERIAL_FLAG_SHELL_RED) != 0) c.r += 1;
	    if((material_id & MATERIAL_FLAG_SHELL_GREEN) != 0) c.g += 1;
	    if((material_id & MATERIAL_FLAG_SHELL_BLUE) != 0) c.b += 1;

	    if((material_id & MATERIAL_FLAG_WEAPON) != 0) c *= 0.2;
	}

    return c;
}
