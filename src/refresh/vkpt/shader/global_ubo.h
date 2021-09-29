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

#ifndef  _GLOBAL_UBO_DESCRIPTOR_SET_LAYOUT_H_
#define  _GLOBAL_UBO_DESCRIPTOR_SET_LAYOUT_H_

#include "constants.h"

#define GLOBAL_UBO_BINDING_IDX               0
#define GLOBAL_INSTANCE_BUFFER_BINDING_IDX   1


#define UBO_CVAR_DO(name, default_value) GLOBAL_UBO_VAR_LIST_DO(float, name)

// The variables listed in UBO_CVAR_LIST are registered as console variables and copied directly into the UBO.
// See main.c for the implementation of that.

// Variables that have "_lf", "_hf" or "_spec" suffix apply to the low-frequency, high-frequency or specular lighting channels, respectively.

#define UBO_CVAR_LIST \
	UBO_CVAR_DO(flt_antilag_hf, 1) /* A-SVGF anti-lag filter strength, [0..inf) */ \
	UBO_CVAR_DO(flt_antilag_lf, 0.2) \
	UBO_CVAR_DO(flt_antilag_spec, 2) \
	UBO_CVAR_DO(flt_antilag_spec_motion, 0.004) /* scaler for motion vector scaled specular anti-blur adjustment */ \
	UBO_CVAR_DO(flt_atrous_depth, 0.5) /* wavelet fitler sensitivity to depth, [0..inf) */ \
	UBO_CVAR_DO(flt_atrous_deflicker_lf, 2) /* max brightness difference between adjacent pixels in the LF channel, (0..inf) */ \
	UBO_CVAR_DO(flt_atrous_hf, 4) /* number of a-trous wavelet filter iterations on the LF channel, [0..4] */ \
	UBO_CVAR_DO(flt_atrous_lf, 4) \
	UBO_CVAR_DO(flt_atrous_spec, 3) \
	UBO_CVAR_DO(flt_atrous_lum_hf, 16) /* wavelet filter sensitivity to luminance, [0..inf) */ \
	UBO_CVAR_DO(flt_atrous_normal_hf, 64) /* wavelet filter sensitivity to normals, [0..inf) */ \
	UBO_CVAR_DO(flt_atrous_normal_lf, 8) \
	UBO_CVAR_DO(flt_atrous_normal_spec, 1) \
	UBO_CVAR_DO(flt_enable, 1) /* switch for the entire SVGF reconstruction, 0 or 1 */ \
	UBO_CVAR_DO(flt_fixed_albedo, 0) /* if nonzero, replaces surface albedo with that value after filtering */ \
	UBO_CVAR_DO(flt_grad_weapon, 0.25) /* gradient scale for the first person weapon, [0..1] */ \
	UBO_CVAR_DO(flt_min_alpha_color_hf, 0.02) /* minimum weight for the new frame data, color channel, (0..1] */ \
	UBO_CVAR_DO(flt_min_alpha_color_lf, 0.01) \
	UBO_CVAR_DO(flt_min_alpha_color_spec, 0.01) \
	UBO_CVAR_DO(flt_min_alpha_moments_hf, 0.01) /* minimum weight for the new frame data, moments channel, (0..1] */  \
	UBO_CVAR_DO(flt_scale_hf, 1) /* overall per-channel output scale, [0..inf) */ \
	UBO_CVAR_DO(flt_scale_lf, 1) \
	UBO_CVAR_DO(flt_scale_overlay, 1.0) /* scale for transparent and emissive objects visible with primary rays */ \
	UBO_CVAR_DO(flt_scale_spec, 1) \
	UBO_CVAR_DO(flt_show_gradients, 0) /* switch for showing the gradient values as overlay image, 0 or 1 */ \
	UBO_CVAR_DO(flt_taa, 2) /* temporal anti-aliasing mode: 0 = off, 1 = regular TAA, 2 = temporal upscale */ \
	UBO_CVAR_DO(flt_taa_anti_sparkle, 0.25) /* strength of the anti-sparkle filter of TAA, [0..1] */ \
	UBO_CVAR_DO(flt_taa_variance, 1.0) /* temporal AA variance window scale, 0 means disable NCC, [0..inf) */ \
	UBO_CVAR_DO(flt_taa_history_weight, 0.95) /* temporal AA weight of the history sample, [0..1) */ \
	UBO_CVAR_DO(flt_temporal_hf, 1) /* temporal filter strength, [0..1] */ \
	UBO_CVAR_DO(flt_temporal_lf, 1) \
	UBO_CVAR_DO(flt_temporal_spec, 1) \
	UBO_CVAR_DO(pt_aperture, 2.0) /* aperture size for the Depth of Field effect, in world units */ \
	UBO_CVAR_DO(pt_aperture_angle, 0) /* rotation of the polygonal aperture, [0..1] */ \
	UBO_CVAR_DO(pt_aperture_type, 0) /* number of aperture polygon edges, circular if less than 3 */ \
	UBO_CVAR_DO(pt_beam_softness, 1.0) /* beam softness */ \
	UBO_CVAR_DO(pt_bump_scale, 1.0) /* scale for normal maps [0..1] */ \
	UBO_CVAR_DO(pt_cameras, 1) /* switch for security cameras, 0 or 1 */ \
	UBO_CVAR_DO(pt_direct_polygon_lights, 1) /* switch for direct lighting from local polygon lights, 0 or 1 */ \
	UBO_CVAR_DO(pt_direct_roughness_threshold, 0.18) /* roughness value where the path tracer switches direct light specular sampling from NDF based to light based, [0..1] */ \
	UBO_CVAR_DO(pt_direct_sphere_lights, 1) /* switch for direct lighting from local sphere lights, 0 or 1 */ \
	UBO_CVAR_DO(pt_direct_sun_light, 1) /* switch for direct lighting from the sun, 0 or 1 */ \
	UBO_CVAR_DO(pt_explosion_brightness, 4.0) /* brightness factor for explosions */ \
	UBO_CVAR_DO(pt_fake_roughness_threshold, 0.20) /* roughness value where the path tracer starts switching indirect light specular sampling from NDF based to SH based, [0..1] */ \
	UBO_CVAR_DO(pt_focus, 200) /* focal distance for the Depth of Field effect, in world units */ \
	UBO_CVAR_DO(pt_fog_brightness, 0.01) /* global multiplier for the color of fog volumes */ \
	UBO_CVAR_DO(pt_indirect_polygon_lights, 1) /* switch for bounce lighting from local polygon lights, 0 or 1 */ \
	UBO_CVAR_DO(pt_indirect_sphere_lights, 1) /* switch for bounce lighting from local sphere lights, 0 or 1 */ \
	UBO_CVAR_DO(pt_light_stats, 1) /* switch for statistical light PDF correction, 0 or 1 */ \
	UBO_CVAR_DO(pt_max_log_sky_luminance, -3) /* maximum sky luminance, log2 scale, used for polygon light selection, (-inf..inf) */ \
	UBO_CVAR_DO(pt_min_log_sky_luminance, -10) /* minimum sky luminance, log2 scale, used for polygon light selection, (-inf..inf) */ \
	UBO_CVAR_DO(pt_metallic_override, -1) /* overrides metallic parameter of all materials if non-negative, [0..1] */ \
	UBO_CVAR_DO(pt_ndf_trim, 0.9) /* trim factor for GGX NDF sampling (0..1] */ \
	UBO_CVAR_DO(pt_num_bounce_rays, 1) /* number of bounce rays, valid values are 0 (disabled), 0.5 (half-res diffuse), 1 (full-res diffuse + specular), 2 (two bounces) */ \
	UBO_CVAR_DO(pt_particle_softness, 1.0) /* particle softness */ \
	UBO_CVAR_DO(pt_reflect_refract, 2) /* number of reflection or refraction bounces: 0, 1 or 2 */ \
	UBO_CVAR_DO(pt_roughness_override, -1) /* overrides roughness of all materials if non-negative, [0..1] */ \
	UBO_CVAR_DO(pt_specular_anti_flicker, 2) /* fade factor for rough reflections of surfaces far away, [0..inf) */ \
	UBO_CVAR_DO(pt_specular_mis, 1) /* enables the use of MIS between specular direct lighting and BRDF specular rays */ \
	UBO_CVAR_DO(pt_show_sky, 0) /* switch for showing the sky polygons, 0 or 1 */ \
	UBO_CVAR_DO(pt_sun_bounce_range, 2000) /* range limiter for indirect lighting from the sun, helps reduce noise, (0..inf) */ \
	UBO_CVAR_DO(pt_sun_specular, 1.0) /* scale for the direct specular reflection of the sun */ \
	UBO_CVAR_DO(pt_texture_lod_bias, 0) /* LOD bias for textures, (-inf..inf) */ \
	UBO_CVAR_DO(pt_toksvig, 1) /* intensity of Toksvig roughness correction, [0..inf) */ \
	UBO_CVAR_DO(pt_thick_glass, 0) /* switch for thick glass refraction: 0 (disabled), 1 (reference mode only), 2 (real-time mode) */ \
	UBO_CVAR_DO(pt_water_density, 0.5) /* scale for light extinction in water and other media, [0..inf) */ \
	UBO_CVAR_DO(tm_debug, 0) /* switch to show the histogram (1) or tonemapping curve (2) */ \
	UBO_CVAR_DO(tm_dyn_range_stops, 7.0) /* Effective display dynamic range in linear stops = log2((max+refl)/(darkest+refl)) (eqn. 6), (-inf..0) */ \
	UBO_CVAR_DO(tm_enable, 1) /* switch for tone mapping, 0 or 1 */ \
	UBO_CVAR_DO(tm_exposure_bias, -1.0) /* exposure bias, log-2 scale */ \
	UBO_CVAR_DO(tm_exposure_speed_down, 1) /* speed of exponential eye adaptation when scene gets darker, 0 means instant */ \
	UBO_CVAR_DO(tm_exposure_speed_up, 2) /* speed of exponential eye adaptation when scene gets brighter, 0 means instant */ \
	UBO_CVAR_DO(tm_blend_scale_border, 1) /* scale factor for full screen blend intensity, at screen border */ \
	UBO_CVAR_DO(tm_blend_scale_center, 0.4) /* scale factor for full screen blend intensity, at screen center */ \
	UBO_CVAR_DO(tm_blend_scale_fade_exp, 4) /* exponent used to interpolate between "border" and "center" factors */ \
	UBO_CVAR_DO(tm_high_percentile, 90) /* high percentile for computing histogram average, (0..100] */ \
	UBO_CVAR_DO(tm_knee_start, 0.6) /* where to switch from a linear to a rational function ramp in the post-tonemapping process, (0..1)  */ \
	UBO_CVAR_DO(tm_low_percentile, 70) /* low percentile for computing histogram average, [0..100) */ \
	UBO_CVAR_DO(tm_max_luminance, 1.0) /* auto-exposure maximum luminance, (0..inf) */ \
	UBO_CVAR_DO(tm_min_luminance, 0.0002) /* auto-exposure minimum luminance, (0..inf) */ \
	UBO_CVAR_DO(tm_noise_blend, 0.5) /* Amount to blend noise values between autoexposed and flat image [0..1] */ \
	UBO_CVAR_DO(tm_noise_stops, -12) /* Absolute noise level in photographic stops, (-inf..inf) */ \
	UBO_CVAR_DO(tm_reinhard, 0.5) /* blend factor between adaptive curve tonemapper (0) and Reinhard curve (1) */ \
	UBO_CVAR_DO(tm_slope_blur_sigma, 12.0) /* sigma for Gaussian blur of tone curve slopes, (0..inf) */ \
	UBO_CVAR_DO(tm_white_point, 10.0) /* how bright colors can be before they become white, (0..inf) */ \

    
#define GLOBAL_UBO_VAR_LIST \
	GLOBAL_UBO_VAR_LIST_DO(int,             current_frame_idx) \
	GLOBAL_UBO_VAR_LIST_DO(int,             width) \
	GLOBAL_UBO_VAR_LIST_DO(int,             height)\
	GLOBAL_UBO_VAR_LIST_DO(int,             current_gpu_slice_width) \
	\
	GLOBAL_UBO_VAR_LIST_DO(int,             medium) \
	GLOBAL_UBO_VAR_LIST_DO(float,           time) \
	GLOBAL_UBO_VAR_LIST_DO(int,             first_person_model) \
	GLOBAL_UBO_VAR_LIST_DO(int,             environment_type) \
	\
	GLOBAL_UBO_VAR_LIST_DO(vec3,            sun_direction) \
	GLOBAL_UBO_VAR_LIST_DO(float,           bloom_intensity) \
	GLOBAL_UBO_VAR_LIST_DO(vec3,            sun_tangent) \
	GLOBAL_UBO_VAR_LIST_DO(float,           sun_tan_half_angle) \
	GLOBAL_UBO_VAR_LIST_DO(vec3,            sun_bitangent) \
	GLOBAL_UBO_VAR_LIST_DO(float,           sun_bounce_scale) \
	GLOBAL_UBO_VAR_LIST_DO(vec3,            sun_color) \
	GLOBAL_UBO_VAR_LIST_DO(float,           sun_cos_half_angle) \
	GLOBAL_UBO_VAR_LIST_DO(vec3,            sun_direction_envmap) \
	GLOBAL_UBO_VAR_LIST_DO(int,             sun_visible) \
	\
	GLOBAL_UBO_VAR_LIST_DO(float,           sky_transmittance) \
	GLOBAL_UBO_VAR_LIST_DO(float,           sky_phase_g) \
	GLOBAL_UBO_VAR_LIST_DO(float,           sky_amb_phase_g) \
	GLOBAL_UBO_VAR_LIST_DO(float,           sun_solid_angle) \
	\
	GLOBAL_UBO_VAR_LIST_DO(vec3,            physical_sky_ground_radiance) \
	GLOBAL_UBO_VAR_LIST_DO(int,             physical_sky_flags) \
	\
	GLOBAL_UBO_VAR_LIST_DO(float,           sky_scattering) \
	GLOBAL_UBO_VAR_LIST_DO(float,           temporal_blend_factor) \
	GLOBAL_UBO_VAR_LIST_DO(int,             planet_albedo_map) \
	GLOBAL_UBO_VAR_LIST_DO(int,             planet_normal_map) \
	\
	GLOBAL_UBO_VAR_LIST_DO(int,             num_sphere_lights) \
	GLOBAL_UBO_VAR_LIST_DO(int ,            num_static_lights) \
	GLOBAL_UBO_VAR_LIST_DO(int,             num_static_primitives) \
	GLOBAL_UBO_VAR_LIST_DO(int,             cluster_debug_index) \
	\
	GLOBAL_UBO_VAR_LIST_DO(int,             water_normal_texture) \
	GLOBAL_UBO_VAR_LIST_DO(float,           pt_env_scale) \
	GLOBAL_UBO_VAR_LIST_DO(float,           cylindrical_hfov) \
	GLOBAL_UBO_VAR_LIST_DO(float,           cylindrical_hfov_prev) \
	\
	GLOBAL_UBO_VAR_LIST_DO(int,             pt_swap_checkerboard) \
	GLOBAL_UBO_VAR_LIST_DO(float,           shadow_map_depth_scale) \
	GLOBAL_UBO_VAR_LIST_DO(float,           god_rays_intensity) \
	GLOBAL_UBO_VAR_LIST_DO(float,           god_rays_eccentricity) \
	\
	GLOBAL_UBO_VAR_LIST_DO(int,             num_cameras) \
	GLOBAL_UBO_VAR_LIST_DO(int,             screen_image_width) \
	GLOBAL_UBO_VAR_LIST_DO(int,             screen_image_height) \
	GLOBAL_UBO_VAR_LIST_DO(int,             prev_gpu_slice_width) \
	\
	GLOBAL_UBO_VAR_LIST_DO(int,             prev_width) \
	GLOBAL_UBO_VAR_LIST_DO(int,             prev_height)\
	GLOBAL_UBO_VAR_LIST_DO(float,           inv_width) \
	GLOBAL_UBO_VAR_LIST_DO(float,           inv_height) \
	\
	GLOBAL_UBO_VAR_LIST_DO(int,             unscaled_width) \
	GLOBAL_UBO_VAR_LIST_DO(int,             unscaled_height) \
	GLOBAL_UBO_VAR_LIST_DO(int,             taa_image_width) \
	GLOBAL_UBO_VAR_LIST_DO(int,             taa_image_height) \
	\
	GLOBAL_UBO_VAR_LIST_DO(int,             taa_output_width) \
	GLOBAL_UBO_VAR_LIST_DO(int,             taa_output_height) \
	GLOBAL_UBO_VAR_LIST_DO(int,             prev_taa_output_width) \
	GLOBAL_UBO_VAR_LIST_DO(int,             prev_taa_output_height) \
	\
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           easu_const0) \
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           easu_const1) \
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           easu_const2) \
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           easu_const3) \
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           rcas_const0) \
	\
	GLOBAL_UBO_VAR_LIST_DO(vec2,            sub_pixel_jitter) \
	GLOBAL_UBO_VAR_LIST_DO(float,           prev_adapted_luminance) \
	GLOBAL_UBO_VAR_LIST_DO(float,           padding1) \
	GLOBAL_UBO_VAR_LIST_DO(vec4,            fs_blend_color) \
	\
	GLOBAL_UBO_VAR_LIST_DO(vec4,            world_center) \
	GLOBAL_UBO_VAR_LIST_DO(vec4,            world_size) \
	GLOBAL_UBO_VAR_LIST_DO(vec4,            world_half_size_inv) \
	\
	GLOBAL_UBO_VAR_LIST_DO(vec4,            sphere_light_data[MAX_LIGHT_SOURCES * 2]) \
	GLOBAL_UBO_VAR_LIST_DO(vec4,            cam_pos) \
	GLOBAL_UBO_VAR_LIST_DO(mat4,            V) \
	GLOBAL_UBO_VAR_LIST_DO(mat4,            invV) \
	GLOBAL_UBO_VAR_LIST_DO(mat4,            V_prev) \
	GLOBAL_UBO_VAR_LIST_DO(mat4,            P) \
	GLOBAL_UBO_VAR_LIST_DO(mat4,            invP) \
	GLOBAL_UBO_VAR_LIST_DO(mat4,            P_prev) \
	GLOBAL_UBO_VAR_LIST_DO(mat4,            invP_prev) \
	GLOBAL_UBO_VAR_LIST_DO(mat4,            environment_rotation_matrix) \
	GLOBAL_UBO_VAR_LIST_DO(mat4,            shadow_map_VP) \
	GLOBAL_UBO_VAR_LIST_DO(mat4,            security_camera_data[MAX_CAMERAS]) \
	GLOBAL_UBO_VAR_LIST_DO(ShaderFogVolume, fog_volumes[MAX_FOG_VOLUMES]) \
	\
	UBO_CVAR_LIST // WARNING: Do not put any other members into global_ubo after this: the CVAR list is not vec4-aligned

#define INSTANCE_BUFFER_VAR_LIST \
	INSTANCE_BUFFER_VAR_LIST_DO(int,             model_indices            [SHADER_MAX_ENTITIES + SHADER_MAX_BSP_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(uint,            model_current_to_prev    [SHADER_MAX_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(uint,            model_prev_to_current    [SHADER_MAX_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(uint,            world_current_to_prev    [SHADER_MAX_BSP_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(uint,            world_prev_to_current    [SHADER_MAX_BSP_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(uint,            bsp_prim_offset          [SHADER_MAX_BSP_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(uint,            model_idx_offset         [SHADER_MAX_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(uint,            model_cluster_id         [SHADER_MAX_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(uint,            model_cluster_id_prev    [SHADER_MAX_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(uint,            bsp_cluster_id           [SHADER_MAX_BSP_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(uint,            bsp_cluster_id_prev      [SHADER_MAX_BSP_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(ModelInstance,   model_instances          [SHADER_MAX_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(ModelInstance,   model_instances_prev     [SHADER_MAX_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(BspMeshInstance, bsp_mesh_instances       [SHADER_MAX_BSP_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(BspMeshInstance, bsp_mesh_instances_prev  [SHADER_MAX_BSP_ENTITIES]) \
	/* stores the offset into the instance buffer in numberof primitives */ \
	INSTANCE_BUFFER_VAR_LIST_DO(uint,            model_instance_buf_offset[SHADER_MAX_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(uint,            model_instance_buf_size  [SHADER_MAX_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(uint,            bsp_instance_buf_offset  [SHADER_MAX_BSP_ENTITIES]) \
	INSTANCE_BUFFER_VAR_LIST_DO(uint,            bsp_instance_buf_size    [SHADER_MAX_BSP_ENTITIES]) \

#ifndef VKPT_SHADER

#if SHADER_MAX_ENTITIES != MAX_ENTITIES
#error need to update constant here
#endif

typedef uint32_t uvec4_t[4];
typedef int ivec4_t[4];
typedef uint32_t uint;

typedef struct {
	float M[16]; // mat4

	uint32_t material;
	int offset_curr;
	int offset_prev; // matrix offset for IQM
	float backlerp;

	float alpha;
	int idx_offset;
	int model_index;
	int is_iqm;
} ModelInstance;

typedef struct {
	float M[16];
	int frame; float padding[3];
} BspMeshInstance;

typedef struct ShaderFogVolume {
	vec3_t mins;
	uint is_active;
	vec3_t maxs;
	float pad2;
	vec3_t color;
	float pad3;
	vec4_t density;
} ShaderFogVolume_t;

#define int_t int32_t
typedef struct QVKUniformBuffer_s {
#define GLOBAL_UBO_VAR_LIST_DO(type, name) type##_t name;
	GLOBAL_UBO_VAR_LIST
#undef  GLOBAL_UBO_VAR_LIST_DO
} QVKUniformBuffer_t;

typedef struct QVKInstanceBuffer_s {
#define INSTANCE_BUFFER_VAR_LIST_DO(type, name) type name;
	INSTANCE_BUFFER_VAR_LIST
#undef  INSTANCE_BUFFER_VAR_LIST_DO
} QVKInstanceBuffer_t;
#undef int_t

#else

struct ModelInstance {
	mat4 M;

	uint material;
	int offset_curr;
	int offset_prev; // matrix offset for IQM
	float backlerp;

	float alpha;
	int idx_offset;
	int model_index;
	int is_iqm;
};

struct BspMeshInstance {
	mat4 M;
	ivec4 frame;
};

struct ShaderFogVolume {
	vec3 mins;
	uint is_active;
	vec3 maxs;
	float pad2;
	vec3 color;
	float pad3;
	vec4 density;
};

struct GlobalUniformBuffer {
#define GLOBAL_UBO_VAR_LIST_DO(type, name) type name;
	GLOBAL_UBO_VAR_LIST
#undef  GLOBAL_UBO_VAR_LIST_DO
};

struct GlobalUniformInstanceBuffer {
#define INSTANCE_BUFFER_VAR_LIST_DO(type, name) type name;
	INSTANCE_BUFFER_VAR_LIST
#undef  INSTANCE_BUFFER_VAR_LIST_DO
};

layout(set = GLOBAL_UBO_DESC_SET_IDX, binding = GLOBAL_UBO_BINDING_IDX, std140) uniform UBO {
	GlobalUniformBuffer global_ubo;
};

layout(set = GLOBAL_UBO_DESC_SET_IDX, binding = GLOBAL_INSTANCE_BUFFER_BINDING_IDX) readonly buffer InstanceUBO {
	GlobalUniformInstanceBuffer instance_buffer;
};

#endif

#undef UBO_CVAR_DO

#endif  /*_GLOBAL_UBO_DESCRIPTOR_SET_LAYOUT_H_*/
