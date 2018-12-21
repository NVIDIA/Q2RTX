/*
Copyright (C) 2018 Christoph Schied

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

#extension GL_NV_ray_tracing : require
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier    : enable
#pragma optionNV(unroll all)

#define RAY_GEN_DESCRIPTOR_SET_IDX 0
layout(set = RAY_GEN_DESCRIPTOR_SET_IDX, binding = 0)
uniform accelerationStructureNV topLevelAS;

#define INSTANCE_DYNAMIC_FLAG (1u << 31)
#define PRIM_ID_MASK          (~INSTANCE_DYNAMIC_FLAG)

#define GLOBAL_UBO_DESC_SET_IDX 1
#include "global_ubo.h"

#define GLOBAL_TEXTURES_DESC_SET_IDX 2
#include "global_textures.h"

#define VERTEX_BUFFER_DESC_SET_IDX 3
#include "vertex_buffer.h"

#define LIGHT_HIERARCHY_SET_IDX 4
#include "light_hierarchy.h"

#include "brdf.glsl"
#include "tiny_encryption_algorithm.h"
#include "utils.glsl"
#include "asvgf.glsl"
#include "read_visbuf.glsl"
#include "light_lists.h"
#include "water.glsl"

#define ALBEDO_MULT 1.3

#define NUM_BOUNCES 2

#define RNG_PRIMARY_OFF_X   0
#define RNG_PRIMARY_OFF_Y   1

#define RNG_NEE_LH(bounce)                (2 + 0 + 7 * bounce)
#define RNG_NEE_TRI_X(bounce)             (2 + 1 + 7 * bounce)
#define RNG_NEE_TRI_Y(bounce)             (2 + 2 + 7 * bounce)
#define RNG_NEE_STATIC_DYNAMIC(bounce)    (2 + 3 + 7 * bounce)
#define RNG_BRDF_X(bounce)                (2 + 4 + 7 * bounce)
#define RNG_BRDF_Y(bounce)                (2 + 5 + 7 * bounce)
#define RNG_BRDF_FRESNEL(bounce)          (2 + 6 + 7 * bounce)

/* no BRDF sampling in last bounce */
#define NUM_RNG_PER_FRAME (RNG_NEE_STATIC_DYNAMIC(NUM_BOUNCES - 1) + 1)

struct RayPayload {
	vec2 barycentric;
	uint instance_prim;
};

bool
found_intersection(RayPayload rp)
{
	return rp.instance_prim != ~0u;
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

#if defined(SHADER_STAGE_RGEN)
#   define RAY_PAYLOAD_SPEC rayPayloadNV
#elif defined(SHADER_STAGE_RMISS) || defined(SHADER_STAGE_RCHIT)
#   define RAY_PAYLOAD_SPEC rayPayloadInNV
#else
#   error no shader type specified
#endif

#if defined(SHADER_STAGE_RGEN)
#define RT_PAYLOAD_NEE  0
#define RT_PAYLOAD_BRDF 1
layout(location = RT_PAYLOAD_NEE)  RAY_PAYLOAD_SPEC RayPayload ray_payload_nee;
layout(location = RT_PAYLOAD_BRDF) RAY_PAYLOAD_SPEC RayPayload ray_payload_brdf;
#endif
#if defined(SHADER_STAGE_RCHIT) || defined(SHADER_STAGE_RMISS)
rayPayloadInNV RayPayload ray_payload;
#endif


#ifdef SHADER_STAGE_RCHIT

hitAttributeNV vec3 hit_attribs;


void
main()
{
	ray_payload.barycentric    = hit_attribs.xy;
	ray_payload.instance_prim  = int(gl_InstanceID != 0) << 31; /* we only have one instance */
	ray_payload.instance_prim |= gl_PrimitiveID;
}

#endif

#ifdef SHADER_STAGE_RMISS

void
main()
{
	ray_payload.instance_prim = ~0u;
}

#endif

#ifdef SHADER_STAGE_RGEN

struct Ray {
	vec3 origin, direction;
	float t_min, t_max;
};

uint rng_seed;

void
trace_ray(Ray ray)
{
	const uint rayFlags = gl_RayFlagsOpaqueNV;
	//const uint rayFlags = gl_RayFlagsOpaqueNV | gl_RayFlagsCullBackFacingTrianglesNV;
	const uint cullMask = 0xff;

	traceNV( topLevelAS, rayFlags, cullMask,
			0 /*sbtRecordOffset*/, 0 /*sbtRecordStride*/, 0 /*missIndex*/,
			ray.origin, ray.t_min, ray.direction, ray.t_max, RT_PAYLOAD_BRDF);
}

bool
trace_shadow_ray(vec3 p1, vec3 p2)
{
	//const uint rayFlags = gl_RayFlagsOpaqueNV;
	/* measured no performance impact for early hit termination */
	const uint rayFlags = gl_RayFlagsOpaqueNV | gl_RayFlagsTerminateOnFirstHitNV;
	//const uint rayFlags = gl_RayFlagsOpaqueNV | gl_RayFlagsCullBackFacingTrianglesNV;
	const uint cullMask = 0xff;
	const float tmin = 0.01;

	vec3 l = p2 - p1;
	float dist = length(l);
	l /= dist;

	traceNV( topLevelAS, rayFlags, cullMask,
			0 /*sbtRecordOffset*/, 0 /*sbtRecordStride*/, 0 /*missIndex*/,
			p2, tmin, -l, dist - tmin, RT_PAYLOAD_NEE);
#if 0
	/* hack that kills intersections if they hit a light source */
	if(found_intersection(ray_payload_nee)) {
		if(!is_dynamic_instance(ray_payload_nee)) /* hit the static geometry */
			return false;
		uint material_id = get_materials_instanced(get_primitive(ray_payload_nee));

		return (material_id & (BSP_FLAG_LIGHT | BSP_FLAG_WATER)) > 0;
	}
	return true;
#else
	return !found_intersection(ray_payload_nee);
#endif
}

mat3
construct_ONB_frisvad(vec3 normal)
{
	precise mat3 ret;
	ret[1] = normal;
	if(normal.z < -0.999805696f) {
		ret[0] = vec3(0.0f, -1.0f, 0.0f);
		ret[2] = vec3(-1.0f, 0.0f, 0.0f);
	}
	else {
		precise float a = 1.0f / (1.0f + normal.z);
		precise float b = -normal.x * normal.y * a;
		ret[0] = vec3(1.0f - normal.x * normal.x * a, b, -normal.x);
		ret[2] = vec3(b, 1.0f - normal.y * normal.y * a, -normal.y);
	}
	return ret;
}

vec2
sample_disk(vec2 uv)
{
	float theta = 2.0 * 3.141592653589 * uv.x;
	float r = sqrt(uv.y);

	return vec2(cos(theta), sin(theta)) * r;
}

vec3
sample_cos_hemisphere(vec2 uv)
{
	vec2 disk = sample_disk(uv);

	return vec3(disk.x, sqrt(max(0.0, 1.0 - dot(disk, disk))), disk.y);
}

vec3
sample_sphere(vec2 uv)
{
	float y = 2.0 * uv.x - 1;
	float theta = 2.0 * 3.141592653589 * uv.y;
	float r = sqrt(1.0 - y * y);
	return vec3(cos(theta) * r, y, sin(theta) * r);
}

#if 0
uvec2
get_rng_uint(uint idx)
{
	uint pixel_idx = gl_LaunchIDNV.x + gl_LaunchSizeNV.x * gl_LaunchIDNV.y;
	uint sample_idx = idx * 100 + uint(global_ubo.current_frame_idx);
	//uint sample_idx = idx * 100 + uint(global_ubo.current_frame_idx[0]);
	uvec2 _rng = encrypt_tea(uvec2(pixel_idx, sample_idx));
	return _rng;
}
#endif

float
get_rng(uint idx)
{
	uvec3 p = uvec3(rng_seed, rng_seed >> 10, rng_seed >> 20);
	p.z = (p.z * NUM_RNG_PER_FRAME + idx);
	p &= uvec3(BLUE_NOISE_RES - 1, BLUE_NOISE_RES - 1, NUM_BLUE_NOISE_TEX - 1);

	return min(texelFetch(TEX_BLUE_NOISE, ivec3(p), 0).r, 0.9999999999999);
	//return fract(vec2(get_rng_uint(idx)) / vec2(0xffffffffu));
}

vec3
env_map(vec3 direction)
{
	return textureLod(TEX_ENVMAP, direction.xzy, 0).rgb;
}

Ray
get_primary_ray(vec2 pos_cs)
{
	if(global_ubo.under_water > 0)
		pos_cs.x += 20.0 / float(global_ubo.width) * sin(pos_cs.y * 10.0 + 5.0 * global_ubo.time);
	vec4 v_near = global_ubo.invVP * vec4(pos_cs, -1.0, 1.0);
	vec4 v_far  = global_ubo.invVP * vec4(pos_cs,  0.0, 1.0);
	v_near /= v_near.w;
	v_far  /= v_far.w;

	Ray ray;
	ray.origin = v_near.xyz;
	ray.direction = normalize(v_far.xyz - v_near.xyz);
	ray.t_min = 0.01;
	ray.t_max = 10000.0;
	return ray;
}

float
intersect_ray_plane(Ray r, vec3 p, vec3 n)
{
	return dot(p - r.origin, n) / dot(r.direction, n);
}

void
store_first_hit(
		vec3 normal,
		float depth,
		float fwidth_depth,
		vec3 motion,
		vec3 albedo,
		vec4 vis_buf)
{
	ivec2 ipos = ivec2(gl_LaunchIDNV);
	uint n_encoded = encode_normal(normal);
	vec4 depth_normal = vec4(depth, uintBitsToFloat(n_encoded), 0, 0);

	vec4 m = vec4(motion, fwidth_depth);

	if((global_ubo.current_frame_idx & 1) == 0) {
		imageStore(IMG_PT_DEPTH_NORMAL_A, ipos, depth_normal);
	}
	else {
		imageStore(IMG_PT_DEPTH_NORMAL_B, ipos, depth_normal);
	}
	imageStore(IMG_PT_ALBEDO, ipos, vec4(albedo, 1.0));
	imageStore(IMG_PT_VISBUF, ipos, vis_buf);
	imageStore(IMG_PT_MOTION, ipos, m);
}

void
store_no_hit(vec3 albedo, vec3 motion)
{
	store_first_hit(vec3(0, 0, 1), -999, 9999.0, motion, albedo, vec4(-1, -1, uintBitsToFloat(uvec2(~0u))));
}

vec3
compute_direct_illumination_static(vec3 V, vec3 position, vec3 normal, out vec3 L, int bounce, uint cluster, out vec3 pos_on_light)
{
	float pdf;
	vec3 normal_light;
	vec3 light_color;

	sample_light_list(
			cluster,
			V,
			position,
			normal,
			pos_on_light,
			normal_light,
			light_color,
			pdf,
			vec3(
				get_rng(RNG_NEE_LH(bounce)),
				get_rng(RNG_NEE_TRI_X(bounce)),
				get_rng(RNG_NEE_TRI_Y(bounce)))
			);

	if(pdf == 0)
		return vec3(0);

	L = pos_on_light - position;

	float dist_light = length(L);

	L /= dist_light;
	/*
	float visibility = float(trace_shadow_ray(position, position_light, dist_light));
	if(visibility == 0) {
		L = vec3(0);
		return vec3(0);
	}
	L /= dist_light;
	*/

	vec3 light_energy = light_color * 500.0f;

	float cos_l = max(0.0, -dot(normal_light, L));
	cos_l *= cos_l;
	//cos_l *= cos_l;

	float geom = (max(0.0, dot(normal, L)) * cos_l)
	           / max(0.01, dist_light * dist_light);
	return (light_energy * geom) / pdf;
}

vec3
compute_direct_illumination_dynamic(vec3 V, vec3 position, vec3 normal, out vec3 L, uint bounce, out vec3 pos_on_light)
{
	if(global_ubo.num_lights == 0)
		return vec3(0);

	uint light_idx = min(global_ubo.num_lights - 1, uint(get_rng(RNG_NEE_LH(bounce)) * global_ubo.num_lights));
	float rng_triangle = fract(get_rng(RNG_NEE_LH(bounce)) * global_ubo.num_lights);

	float pdf = 1.0;
	vec3 normal_light;
	vec3 light_color;
	sample_light_list_dynamic(
		light_idx,
		V,
		position,
		normal,
		pos_on_light,
		normal_light,
		light_color,
		pdf,
		vec3(rng_triangle, get_rng(RNG_NEE_TRI_X(bounce)), get_rng(RNG_NEE_TRI_Y(bounce))));

	if(pdf == 0)
		return vec3(0);

	L = pos_on_light - position;
	float r2 = dot(L, L);
	float r = sqrt(r2);
	L /= r;

	//float geom = 1.0;
	/* sad panda would like to have MIS to avoid heavy clamping */
	float geom = max(-dot(normal_light, L), 0.0) * max(dot(normal, L), 0) / max(r2, 1000.0);

	//pdf = max(0.001, pdf);
	//geom = min(geom, 500.0);

	return light_color * float(global_ubo.num_lights) * geom * 3000.0 / pdf;
}

bool
is_water(uint material)
{
	return (material & (BSP_FLAG_WATER | BSP_FLAG_LIGHT)) == BSP_FLAG_WATER;
}

bool
is_lava(uint material)
{
	uint flag = BSP_FLAG_WATER | BSP_FLAG_LIGHT;
	return (material & flag) == flag;
}

vec4
path_tracer()
{
	ivec2 ipos = ivec2(gl_LaunchIDNV);
	rng_seed = (global_ubo.current_frame_idx & 1) == 0
		? texelFetch(TEX_ASVGF_RNG_SEED_A, ipos, 0).r
		: texelFetch(TEX_ASVGF_RNG_SEED_B, ipos, 0).r;
	vec3 position;
	vec3 normal;
	vec3 albedo = vec3(1);
	vec3 direction;
	uint material_id;
	vec3 primary_albedo = vec3(1);

	vec3 contrib    = vec3(0);
	vec3 throughput = vec3(1);

	uint cluster_idx = ~0u;

	bool temporal_accum = true;

	{
		/* box muller transform */
		vec2 pixel_offset = vec2(get_rng(RNG_PRIMARY_OFF_X), get_rng(RNG_PRIMARY_OFF_Y));
		pixel_offset.x = sqrt(-2.0 * log(pixel_offset.x));
		pixel_offset = clamp(pixel_offset, vec2(-1), vec2(1)) * 0.5; // temporal filter breaks otherwise

		const vec2 pixel_center = vec2(gl_LaunchIDNV.xy) + vec2(0.5);
		const vec2 inUV = (pixel_center + pixel_offset) / vec2(gl_LaunchSizeNV.xy);

		vec2 screen_coord_cs = inUV * 2.0 - vec2(1.0);

		bool is_gradient;
		{
			uint u = texelFetch(TEX_ASVGF_GRAD_SMPL_POS, ipos / GRAD_DWN, 0).r;

			ivec2 grad_strata_pos = ivec2(
					u >> (STRATUM_OFFSET_SHIFT * 0),
					u >> (STRATUM_OFFSET_SHIFT * 1)) & STRATUM_OFFSET_MASK;

			is_gradient = (u > 0 && all(equal(grad_strata_pos, ipos % GRAD_DWN)));
		}

		Ray ray = get_primary_ray(screen_coord_cs);

		if(is_gradient) {
			/* gradient samples only need to verify visibility but need to
			 * maintain the precise location */
			vec3 pos_ws    = texelFetch(TEX_ASVGF_POS_WS_FWD, ipos / GRAD_DWN, 0).xyz; 
			ray.origin     = global_ubo.cam_pos.xyz;
			ray.direction  = pos_ws - ray.origin;
			float len      = length(ray.direction);
			ray.direction /= len;
			ray.t_max      = len - 0.1;
		}

		trace_ray(ray);

		direction = ray.direction;

		if(!found_intersection(ray_payload_brdf) && !is_gradient) {
			vec3 env = env_map(ray.direction);
			store_no_hit(env_map(ray.direction), vec3(0));
			#ifndef RTX
			return vec4(1, 1, 1, false);
			#else
			return vec4(env, false);
			#endif
		}

		vec4 vis_buf;
		precise vec3 bary;
		Triangle triangle;
		/* reprojection was valid for the gradient sample */
		if(is_gradient && !found_intersection(ray_payload_brdf)) {
			vis_buf = texelFetch(TEX_ASVGF_VISBUF_FWD, ipos / GRAD_DWN, 0);
			bary.yz = vis_buf.xy;
			bary.x  = 1.0 - bary.y - bary.z;
		}
		else {
			uvec2 v;
			if(is_dynamic_instance(ray_payload_brdf)) {
				unpack_instance_id_triangle_idx(
					get_instance_id_instanced(get_primitive(ray_payload_brdf)),
					v.x, v.y);
			}
			else {
				v.x = ~0u;
				v.y = get_primitive(ray_payload_brdf);
			}

			bary = get_hit_barycentric(ray_payload_brdf);
			vis_buf = vec4(bary.yz, uintBitsToFloat(v));

			if(is_gradient) { /* gradient sample became occluded, mask out */
				imageStore(IMG_ASVGF_GRAD_SMPL_POS, ipos / GRAD_DWN, uvec4(0));
			}
			is_gradient = false;
		}
		visbuf_get_triangle(triangle, vis_buf);

		/* world-space */
		position       = triangle.positions * bary;
		normal         = normalize(triangle.normals * bary);
		material_id    = triangle.material_id;
		vec2 tex_coord = triangle.tex_coords * bary;

		/* compute view-space derivatives of depth and clip-space motion vectors */
		/* cannot use ray-t as svgf expects closest distance to plane */
		Ray ray_x = get_primary_ray(screen_coord_cs + vec2(2.0 / float(global_ubo.width), 0));
		Ray ray_y = get_primary_ray(screen_coord_cs - vec2(0, 2.0 / float(global_ubo.height)));

		vec3 bary_x = compute_barycentric(triangle.positions, ray_x.origin, ray_x.direction);
		vec3 bary_y = compute_barycentric(triangle.positions, ray_y.origin, ray_y.direction);

		vec3 pos_ws_x= triangle.positions * bary_x;
		vec3 pos_ws_y= triangle.positions * bary_y;

		vec2 tex_coord_x = triangle.tex_coords * bary_x;
		vec2 tex_coord_y = triangle.tex_coords * bary_y;
		tex_coord_x -= tex_coord;
		tex_coord_y -= tex_coord;

		tex_coord_x *= 0.5;
		tex_coord_y *= 0.5;

		primary_albedo = global_textureGrad(triangle.material_id, tex_coord, tex_coord_x, tex_coord_y).rgb * ALBEDO_MULT;

		vec3 pos_ws_curr = position, pos_ws_prev;
		{
			vec3 bary;
			bary.yz = vis_buf.xy;
			bary.x  = 1.0 - bary.y - bary.z;
			Triangle t_prev;
			visbuf_get_triangle_backprj(t_prev, vis_buf);
			pos_ws_prev = t_prev.positions * bary;
		}

		vec3 pos_curr_cs = (global_ubo.VP      * vec4(pos_ws_curr, 1.0)).xyw;
		vec3 pos_prev_cs = (global_ubo.VP_prev * vec4(pos_ws_prev, 1.0)).xyw;
		float depth_vs_x = (global_ubo.VP      * vec4(pos_ws_x,    1.0)).w;
		float depth_vs_y = (global_ubo.VP      * vec4(pos_ws_y,    1.0)).w;

		pos_curr_cs.xy /= pos_curr_cs.z;
		pos_prev_cs.xy /= pos_prev_cs.z;

		float fwidth_depth = 1.0 / max(1e-4, (abs(depth_vs_x - pos_curr_cs.z) + abs(depth_vs_y - pos_curr_cs.z)));

		vec3 motion = vec3(pos_prev_cs - pos_curr_cs);

		if(is_water(material_id)) {
			vec3 water_normal = waterd(position.xy * 0.1).xzy;
			float F = pow(1.0 - max(0.0, -dot(direction, water_normal)), 5.0);
			direction = reflect(direction, water_normal);
			trace_ray(Ray(position, direction, 0.01, 10000.0));
			throughput *= mix(vec3(0.1, 0.1, 0.15), vec3(1.0), F);
			contrib += (1.0 - F) * vec3(0.2, 0.4, 0.4) * 0.5;

			if(!found_intersection(ray_payload_brdf))
			{
				primary_albedo = env_map(direction); // * throughput;
				store_no_hit(primary_albedo, motion);
				return vec4(contrib, false);
			}

			Triangle triangle = get_hit_triangle(ray_payload_brdf);
			vec3 bary         = get_hit_barycentric(ray_payload_brdf);
			vec2 tex_coord    = triangle.tex_coords * bary;

			/* world-space */
			position       = triangle.positions * bary;
			normal         = normalize(triangle.normals * bary);
			material_id    = triangle.material_id;
			albedo         = global_textureLod(triangle.material_id, tex_coord, 2).rgb * ALBEDO_MULT;
			cluster_idx    = triangle.cluster;
			primary_albedo = albedo;
			albedo         = vec3(1);

			temporal_accum = true;

			if(dot(direction, normal) > 0)
				normal = -normal;
		}

		if((material_id & BSP_FLAG_TRANSPARENT) > 0) {
			temporal_accum = false;
			contrib += vec3(0.05); // XXX hack! makes windows appear a bit milky
		}

		if((material_id & BSP_FLAG_LIGHT) > 0) {
			if(is_lava(material_id)) {
				primary_albedo = lava(position.xy * 0.03);
				//primary_albedo *= 5.0;
				//primary_albedo += vec3(1) * abs(sin(position.x * 0.01 + 1.0 * global_ubo.time)) * max(0.0, (1.0 - luminance(primary_albedo)));
			}
			store_no_hit(primary_albedo, motion);
			#ifndef RTX
			return vec4(1, 1, 1, false);
			#endif
		}
		else {
			store_first_hit(normal, pos_curr_cs.z, fwidth_depth, motion, primary_albedo, vis_buf);
		}

		cluster_idx = triangle.cluster;
	}

#ifdef RTX

	throughput = vec3(1);
	contrib = primary_albedo * throughput;

	const int num_bounces = 10;
	for(int i = 0; i < num_bounces; i++) {

		if(dot(direction, normal) > 0)
			normal = -normal;

		direction = reflect(direction, normal);

		trace_ray(Ray(position, direction, 0.01, 10000.0));

		if(!found_intersection(ray_payload_brdf)) {
			vec3 env = env_map(direction);
			contrib += env * throughput;
			break;
		}

		{
			Triangle triangle = get_hit_triangle(ray_payload_brdf);
			vec3 bary         = get_hit_barycentric(ray_payload_brdf);
			vec2 tex_coord    = triangle.tex_coords * bary;

			/* world-space */
			position       = triangle.positions * bary;
			normal         = normalize(triangle.normals * bary);
			material_id    = triangle.material_id;
			albedo         = global_textureLod(triangle.material_id, tex_coord, i / 2).rgb;
			cluster_idx    = triangle.cluster;

			contrib += albedo * throughput;
			throughput *= 0.8;

		}
	}

	return vec4(contrib * 2.0, false);

#else
	int bounce = 0;
	while(bounce < NUM_BOUNCES) {
	//for(int bounce = 0; bounce < NUM_BOUNCES; bounce++) {

		if((material_id & BSP_FLAG_TRANSPARENT) > 0) {
		}
		else if((material_id & BSP_FLAG_LIGHT) > 0) {
			break;
		}
		else {
			vec3 pos_on_light_static;
			vec3 pos_on_light_dynamic;

			vec3 contrib_static;
			vec3 contrib_dynamic;

			{	/* static illumination */
				vec3 L_nee;
				vec3 nee  = compute_direct_illumination_static(-direction, position, normal, L_nee, bounce, cluster_idx, pos_on_light_static);
				vec3 brdf = bounce == 0 || true 
					? albedo * blinn_phong_based_brdf(-direction, L_nee, normal, 20.0)
					: albedo / M_PI;
				contrib_static = nee * throughput * brdf;
			}
			{	/* dynamic illumination */
				vec3 L_nee;
				vec3 nee  = compute_direct_illumination_dynamic(-direction, position, normal, L_nee, bounce, pos_on_light_dynamic);
				vec3 brdf = bounce == 0
					? albedo * blinn_phong_based_brdf(-direction, L_nee, normal, 20.0)
					: albedo / M_PI;
				contrib_dynamic = nee * throughput * brdf;
			}

			float l_static  = luminance(abs(contrib_static));
			float l_dynamic = luminance(abs(contrib_dynamic));
			float w = l_static / (l_static + l_dynamic);
			if(!isnan(w)) {
				float rng = get_rng(RNG_NEE_STATIC_DYNAMIC(bounce));
				float vis = float(trace_shadow_ray(position, rng < w ? pos_on_light_static : pos_on_light_dynamic));
				if(rng < w) {
					contrib += vis * contrib_static / w;
				}
				else {
					contrib += vis * contrib_dynamic / (1.0 - w);
				}
			}
		}

		if(bounce == NUM_BOUNCES - 1)
			break;

		if((material_id & BSP_FLAG_TRANSPARENT) > 0) {
		}
		else {
			vec2 rng3 = vec2(get_rng(RNG_BRDF_X(bounce)), get_rng(RNG_BRDF_Y(bounce)));
#if 0
			mat3 ONB = construct_ONB_frisvad(normal);

			vec3 diffuse_dir = sample_cos_hemisphere(rng3);
			direction = normalize(ONB * diffuse_dir);
#else
			/* perfect importance sampling for the artistically driven KIT BRDF */

			float rng_frensel = get_rng(RNG_BRDF_FRESNEL(bounce));

			float F = pow(1.0 - max(0.0, -dot(direction, normal)), 5.0);
			F = mix(0.5, 1.0, F);


			vec3 dir_sphere = sample_sphere(rng3);

			if(rng_frensel < F) {
				vec3 dir  = reflect(direction, normal);
				direction = normalize(dir_sphere + dir * 2.0);
				if(dot(direction, normal) < 0.0)
					direction = reflect(direction, normal);
			}
			else {
				direction = normalize(dir_sphere + normal);
			}

#endif


			throughput *= albedo;
		}

		trace_ray(Ray(position, direction, 0.01, 10000.0));

		if(!found_intersection(ray_payload_brdf)) {
			vec3 env = env_map(direction);
			env *= env;
			contrib += throughput * env * 20.0;
			break;
		}

		{
			Triangle triangle = get_hit_triangle(ray_payload_brdf);
			vec3 bary         = get_hit_barycentric(ray_payload_brdf);
			vec2 tex_coord    = triangle.tex_coords * bary;

			/* world-space */
			position       = triangle.positions * bary;
			normal         = normalize(triangle.normals * bary);
			material_id    = triangle.material_id;
			albedo         = global_textureLod(triangle.material_id, tex_coord, 5).rgb + vec3(0.01);
			cluster_idx    = triangle.cluster;

			if(dot(direction, normal) > 0)
				normal = -normal;
		}
		if(is_water(material_id)) {
			albedo /= 0.0001 + max(albedo.r, max(albedo.g, albedo.b));
			if(direction.z > 0)
				contrib += albedo * throughput * 1.0;
			else
				contrib += albedo * throughput * 0.5;
		}

		if((material_id & BSP_FLAG_TRANSPARENT) == 0)
			bounce++;
	}

	return vec4(contrib, temporal_accum); // / (primary_albedo + 0.00001);
#endif
}

void
main() 
{
	vec4 color = path_tracer();
	color.rgb = max(vec3(0), color.rgb);
	if(any(isnan(color.rgb)))
		color.rgb = vec3(0);

	color.rgb = clamp_color(color.rgb, 128.0);

	if((global_ubo.current_frame_idx & 1) == 0) {
		imageStore(IMG_PT_COLOR_A, ivec2(gl_LaunchIDNV.xy), color);
	}
	else {
		imageStore(IMG_PT_COLOR_B, ivec2(gl_LaunchIDNV.xy), color);
	}
}

#endif
// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
