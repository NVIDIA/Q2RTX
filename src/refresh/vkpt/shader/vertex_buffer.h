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

#ifndef _VERTEX_BUFFER_H_
#define _VERTEX_BUFFER_H_

#define MAX_VERT_BSP            (1 << 21)

#define MAX_VERT_MODEL          (1 << 23)
#define MAX_IDX_MODEL           (1 << 22)
#define MAX_PRIM_MODEL          (MAX_IDX_MODEL / 3)

#define MAX_LIGHT_LISTS         (1 << 14)
#define MAX_LIGHT_LIST_NODES    (1 << 19)

#define MAX_LIGHT_POLYS         4096
#define LIGHT_POLY_VEC4S        4

// should match the same constant declared in material.h
#define MAX_PBR_MATERIALS      4096

#define LIGHT_TEXTURE_SCALE     0

#define ALIGN_SIZE_4(x, n)  ((x * n + 3) & (~3))

#define BSP_VERTEX_BUFFER_BINDING_IDX 0
#define MODEL_DYNAMIC_VERTEX_BUFFER_BINDING_IDX 1
#define LIGHT_BUFFER_BINDING_IDX 2
#define READBACK_BUFFER_BINDING_IDX 3
#define TONE_MAPPING_BUFFER_BINDING_IDX 4
#define SUN_COLOR_BUFFER_BINDING_IDX 5
#define SUN_COLOR_UBO_BINDING_IDX 6
#define LIGHT_STATS_BUFFER_BINDING_IDX 7

#define SUN_COLOR_ACCUMULATOR_FIXED_POINT_SCALE 0x100000
#define SKY_COLOR_ACCUMULATOR_FIXED_POINT_SCALE 0x100

#ifdef VKPT_SHADER
#define uint32_t uint
#endif

#define BSP_VERTEX_BUFFER_LIST \
	VERTEX_BUFFER_LIST_DO(float,    3, positions_bsp,         (MAX_VERT_BSP        )) \
	VERTEX_BUFFER_LIST_DO(float,    2, tex_coords_bsp,        (MAX_VERT_BSP        )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, tangents_bsp,          (MAX_VERT_BSP / 3    )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, materials_bsp,         (MAX_VERT_BSP / 3    )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, clusters_bsp,          (MAX_VERT_BSP / 3    )) \
	VERTEX_BUFFER_LIST_DO(float,    1, texel_density_bsp,     (MAX_VERT_BSP / 3    )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, sky_visibility,        (MAX_LIGHT_LISTS / 32)) \

#define MODEL_DYNAMIC_VERTEX_BUFFER_LIST \
	VERTEX_BUFFER_LIST_DO(float,    3, positions_instanced,   (MAX_VERT_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(float,    3, pos_prev_instanced,    (MAX_VERT_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, normals_instanced,     (MAX_VERT_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, tangents_instanced,    (MAX_PRIM_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(float,    2, tex_coords_instanced,  (MAX_VERT_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(float,    1, alpha_instanced,       (MAX_PRIM_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, clusters_instanced,    (MAX_PRIM_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, materials_instanced,   (MAX_PRIM_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, instance_id_instanced, (MAX_PRIM_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(float,    1, texel_density_instanced, (MAX_PRIM_MODEL    )) \

#define LIGHT_BUFFER_LIST \
	VERTEX_BUFFER_LIST_DO(uint32_t, 4, material_table,        (MAX_PBR_MATERIALS)) \
	VERTEX_BUFFER_LIST_DO(float,    4, light_polys,           (MAX_LIGHT_POLYS * LIGHT_POLY_VEC4S)) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, light_list_offsets,    (MAX_LIGHT_LISTS     )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, light_list_lights,     (MAX_LIGHT_LIST_NODES)) \
	VERTEX_BUFFER_LIST_DO(float,    1, light_styles,          (MAX_LIGHT_STYLES    )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, cluster_debug_mask,    (MAX_LIGHT_LISTS / 32)) \

#define VERTEX_BUFFER_LIST_DO(type, dim, name, size) \
	type name[ALIGN_SIZE_4(size, dim)];

struct BspVertexBuffer
{
	BSP_VERTEX_BUFFER_LIST
};

struct ModelDynamicVertexBuffer
{
	MODEL_DYNAMIC_VERTEX_BUFFER_LIST
};

struct LightBuffer
{
	LIGHT_BUFFER_LIST
};

#undef VERTEX_BUFFER_LIST_DO


struct ToneMappingBuffer
{
	int accumulator[HISTOGRAM_BINS];
	float curve[HISTOGRAM_BINS];
	float normalized[HISTOGRAM_BINS];
	float adapted_luminance;
	float tonecurve;
};

#ifndef VKPT_SHADER
typedef int ivec3_t[3];
typedef int ivec4_t[4];
#else
#define ivec3_t ivec3
#define ivec4_t ivec4
#define vec3_t vec3
#endif

struct ReadbackBuffer
{
	uint32_t material;
	uint32_t cluster;
	float sun_luminance;
	float sky_luminance;
	vec3_t hdr_color;
	float adapted_luminance;
};

struct SunColorBuffer
{
	ivec3_t accum_sun_color;
	int padding1;

	ivec4_t accum_sky_color;

	vec3_t sun_color;
	float sun_luminance;

	vec3_t sky_color;
	float sky_luminance;
};

#ifndef VKPT_SHADER
typedef struct BspVertexBuffer BspVertexBuffer;
typedef struct ModelDynamicVertexBuffer ModelDynamicVertexBuffer;
typedef struct LightBuffer LightBuffer;
typedef struct ReadbackBuffer ReadbackBuffer;
typedef struct ToneMappingBuffer ToneMappingBuffer;
typedef struct SunColorBuffer SunColorBuffer;

typedef struct {
	vec3_t position;
	vec3_t normal;
	vec2_t texcoord;
} model_vertex_t;
#else
#define MODEL_VERTEX_SIZE 8
#define MODEL_VERTEX_POSITION 0
#define MODEL_VERTEX_NORMAL 3
#define MODEL_VERTEX_TEXCOORD 6
#endif

#ifdef VKPT_SHADER

struct MaterialInfo
{
	uint diffuse_texture;
	uint normals_texture;
	uint emissive_texture;
	float bump_scale;
	float roughness_override;
	float specular_scale;
	float emissive_scale;
	float light_style_scale;
	uint num_frames;
	uint next_frame;
};

struct LightPolygon
{
	mat3 positions;
	vec3 color;
	float light_style_scale;
	float prev_style_scale;
};

#ifdef VERTEX_READONLY
layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = BSP_VERTEX_BUFFER_BINDING_IDX) readonly buffer BSP_VERTEX_BUFFER {
	BspVertexBuffer vbo_bsp;
};
#else
layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = BSP_VERTEX_BUFFER_BINDING_IDX) buffer BSP_VERTEX_BUFFER {
	BspVertexBuffer vbo_bsp;
};
#endif

#ifdef VERTEX_READONLY
layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = MODEL_DYNAMIC_VERTEX_BUFFER_BINDING_IDX) readonly buffer MODEL_DYNAMIC_VERTEX_BUFFER {
	ModelDynamicVertexBuffer vbo_model_dynamic;
};
#else
layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = MODEL_DYNAMIC_VERTEX_BUFFER_BINDING_IDX) buffer MODEL_DYNAMIC_VERTEX_BUFFER {
	ModelDynamicVertexBuffer vbo_model_dynamic;
};
#endif

layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = LIGHT_BUFFER_BINDING_IDX) readonly buffer LIGHT_BUFFER {
	LightBuffer lbo;
};

layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = READBACK_BUFFER_BINDING_IDX) buffer READBACK_BUFFER {
	ReadbackBuffer readback;
};

layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = TONE_MAPPING_BUFFER_BINDING_IDX) buffer TONE_MAPPING_BUFFER {
	ToneMappingBuffer tonemap_buffer;
};

layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = SUN_COLOR_BUFFER_BINDING_IDX) buffer SUN_COLOR_BUFFER {
	SunColorBuffer sun_color_buffer;
};

layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = SUN_COLOR_UBO_BINDING_IDX, std140) uniform SUN_COLOR_UBO {
	SunColorBuffer sun_color_ubo;
};

layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = LIGHT_STATS_BUFFER_BINDING_IDX) buffer LIGHT_STATS_BUFFERS {
	uint stats[];
} light_stats_bufers[3];


#define GET_float_1(buf,name) \
float \
get_##name(uint idx) \
{ \
	return buf.name[idx]; \
}

#define GET_float_2(buf,name) \
vec2 \
get_##name(uint idx) \
{ \
	return vec2(buf.name[idx * 2 + 0], buf.name[idx * 2 + 1]); \
}

#define GET_float_3(buf,name) \
vec3 \
get_##name(uint idx) \
{ \
	return vec3(buf.name[idx * 3 + 0], buf.name[idx * 3 + 1], buf.name[idx * 3 + 2]); \
}

#define GET_float_4(buf,name) \
vec4 \
get_##name(uint idx) \
{ \
	return vec4(buf.name[idx * 4 + 0], buf.name[idx * 4 + 1], buf.name[idx * 4 + 2], buf.name[idx * 4 + 3]); \
}

#define GET_uint32_t_1(buf,name) \
uint \
get_##name(uint idx) \
{ \
	return buf.name[idx]; \
}

#define GET_uint32_t_3(buf,name) \
uvec3 \
get_##name(uint idx) \
{ \
	return uvec3(buf.name[idx * 3 + 0], buf.name[idx * 3 + 1], buf.name[idx * 3 + 2]); \
}

#define GET_uint32_t_4(buf,name) \
uvec4 \
get_##name(uint idx) \
{ \
	return uvec4(buf.name[idx * 4 + 0], buf.name[idx * 4 + 1], buf.name[idx * 4 + 2], buf.name[idx * 4 + 3]); \
}

#ifndef VERTEX_READONLY
#define SET_float_1(buf,name) \
void \
set_##name(uint idx, float v) \
{ \
	buf.name[idx] = v; \
}

#define SET_float_2(buf,name) \
void \
set_##name(uint idx, vec2 v) \
{ \
	buf.name[idx * 2 + 0] = v[0]; \
	buf.name[idx * 2 + 1] = v[1]; \
}

#define SET_float_3(buf,name) \
void \
set_##name(uint idx, vec3 v) \
{ \
	buf.name[idx * 3 + 0] = v[0]; \
	buf.name[idx * 3 + 1] = v[1]; \
	buf.name[idx * 3 + 2] = v[2]; \
}

#define SET_float_4(buf,name) \
void \
set_##name(uint idx, vec4 v) \
{ \
	buf.name[idx * 4 + 0] = v[0]; \
	buf.name[idx * 4 + 1] = v[1]; \
	buf.name[idx * 4 + 2] = v[2]; \
	buf.name[idx * 4 + 3] = v[3]; \
}

#define SET_uint32_t_1(buf,name) \
void \
set_##name(uint idx, uint u) \
{ \
	buf.name[idx] = u; \
}

#define SET_uint32_t_3(buf,name) \
void \
set_##name(uint idx, uvec3 v) \
{ \
	buf.name[idx * 3 + 0] = v[0]; \
	buf.name[idx * 3 + 1] = v[1]; \
	buf.name[idx * 3 + 2] = v[2]; \
}
#endif

#ifdef VERTEX_READONLY
#define VERTEX_BUFFER_LIST_DO(type, dim, name, size) \
	GET_##type##_##dim(vbo_bsp,name)
BSP_VERTEX_BUFFER_LIST
#undef VERTEX_BUFFER_LIST_DO
#else
#define VERTEX_BUFFER_LIST_DO(type, dim, name, size) \
	GET_##type##_##dim(vbo_bsp,name) \
	SET_##type##_##dim(vbo_bsp,name)
BSP_VERTEX_BUFFER_LIST
#undef VERTEX_BUFFER_LIST_DO
#endif

#ifdef VERTEX_READONLY
#define VERTEX_BUFFER_LIST_DO(type, dim, name, size) \
	GET_##type##_##dim(vbo_model_dynamic,name)
MODEL_DYNAMIC_VERTEX_BUFFER_LIST
#undef VERTEX_BUFFER_LIST_DO
#else
#define VERTEX_BUFFER_LIST_DO(type, dim, name, size) \
	GET_##type##_##dim(vbo_model_dynamic,name) \
	SET_##type##_##dim(vbo_model_dynamic,name)
MODEL_DYNAMIC_VERTEX_BUFFER_LIST
#undef VERTEX_BUFFER_LIST_DO
#endif

#define VERTEX_BUFFER_LIST_DO(type, dim, name, size) \
	GET_##type##_##dim(lbo,name)
LIGHT_BUFFER_LIST
#undef VERTEX_BUFFER_LIST_DO

struct Triangle
{
	mat3x3 positions;
	mat3x3 positions_prev;
	mat3x3 normals;
	mat3x2 tex_coords;
	vec3   tangent;
	uint   material_id;
	uint   cluster;
	float  alpha;
	float  texel_density;
};

Triangle
get_bsp_triangle(uint prim_id)
{
	Triangle t;
	t.positions[0] = get_positions_bsp(prim_id * 3 + 0);
	t.positions[1] = get_positions_bsp(prim_id * 3 + 1);
	t.positions[2] = get_positions_bsp(prim_id * 3 + 2);

	t.positions_prev = t.positions;

	vec3 normal = normalize(cross(
				t.positions[1] - t.positions[0],
				t.positions[2] - t.positions[0]));

	t.normals[0] = normal;
	t.normals[1] = normal;
	t.normals[2] = normal;

	t.tex_coords[0] = get_tex_coords_bsp(prim_id * 3 + 0);
	t.tex_coords[1] = get_tex_coords_bsp(prim_id * 3 + 1);
	t.tex_coords[2] = get_tex_coords_bsp(prim_id * 3 + 2);

    t.tangent = decode_normal(get_tangents_bsp(prim_id));

	t.material_id = get_materials_bsp(prim_id);

	t.cluster = get_clusters_bsp(prim_id);

	t.texel_density = get_texel_density_bsp(prim_id);

	t.alpha = 1.0;

	return t;
}

Triangle
get_instanced_triangle(uint prim_id)
{
	Triangle t;
	t.positions[0] = get_positions_instanced(prim_id * 3 + 0);
	t.positions[1] = get_positions_instanced(prim_id * 3 + 1);
	t.positions[2] = get_positions_instanced(prim_id * 3 + 2);

	t.positions_prev[0] = get_pos_prev_instanced(prim_id * 3 + 0);
	t.positions_prev[1] = get_pos_prev_instanced(prim_id * 3 + 1);
	t.positions_prev[2] = get_pos_prev_instanced(prim_id * 3 + 2);

	t.normals[0] = decode_normal(get_normals_instanced(prim_id * 3 + 0));
	t.normals[1] = decode_normal(get_normals_instanced(prim_id * 3 + 1));
	t.normals[2] = decode_normal(get_normals_instanced(prim_id * 3 + 2));

	t.tangent = decode_normal(get_tangents_instanced(prim_id));

	t.tex_coords[0] = get_tex_coords_instanced(prim_id * 3 + 0);
	t.tex_coords[1] = get_tex_coords_instanced(prim_id * 3 + 1);
	t.tex_coords[2] = get_tex_coords_instanced(prim_id * 3 + 2);

	t.material_id = get_materials_instanced(prim_id);

	t.cluster = get_clusters_instanced(prim_id);

	t.alpha = get_alpha_instanced(prim_id);

	t.texel_density = get_texel_density_instanced(prim_id);

	return t;
}

#ifndef VERTEX_READONLY
void
store_instanced_triangle(Triangle t, uint instance_id, uint prim_id)
{
	set_positions_instanced(prim_id * 3 + 0, t.positions[0]);
	set_positions_instanced(prim_id * 3 + 1, t.positions[1]);
	set_positions_instanced(prim_id * 3 + 2, t.positions[2]);

	set_pos_prev_instanced(prim_id * 3 + 0, t.positions_prev[0]);
	set_pos_prev_instanced(prim_id * 3 + 1, t.positions_prev[1]);
	set_pos_prev_instanced(prim_id * 3 + 2, t.positions_prev[2]);

	set_normals_instanced(prim_id * 3 + 0, encode_normal(t.normals[0]));
	set_normals_instanced(prim_id * 3 + 1, encode_normal(t.normals[1]));
	set_normals_instanced(prim_id * 3 + 2, encode_normal(t.normals[2]));

	set_tangents_instanced(prim_id, encode_normal(t.tangent));

	set_tex_coords_instanced(prim_id * 3 + 0, t.tex_coords[0]);
	set_tex_coords_instanced(prim_id * 3 + 1, t.tex_coords[1]);
	set_tex_coords_instanced(prim_id * 3 + 2, t.tex_coords[2]);

	set_materials_instanced(prim_id, t.material_id);

	set_instance_id_instanced(prim_id, instance_id);

	set_clusters_instanced(prim_id, t.cluster);

	set_alpha_instanced(prim_id, t.alpha);

	set_texel_density_instanced(prim_id, t.texel_density);
}
#endif

MaterialInfo
get_material_info(uint material_id)
{
	uvec4 data = get_material_table(material_id & MATERIAL_INDEX_MASK);

	MaterialInfo minfo;
	minfo.diffuse_texture = data.x & 0xffff;
	minfo.normals_texture = data.x >> 16;
	minfo.emissive_texture = data.y & 0xffff;
	minfo.num_frames = (data.y >> 28) & 0x000f;
	minfo.next_frame = (data.y >> 16) & 0x0fff;
	minfo.bump_scale = unpackHalf2x16(data.z).x;
	minfo.roughness_override = unpackHalf2x16(data.z).y;
	minfo.specular_scale = unpackHalf2x16(data.w).x;
	minfo.emissive_scale = unpackHalf2x16(data.w).y;

	// Apply the light style for non-camera materials.
	// Camera materials use the same bits to store the camera ID.
	if((material_id & MATERIAL_KIND_MASK) != MATERIAL_KIND_CAMERA)
	{
		uint light_style = (material_id & MATERIAL_LIGHT_STYLE_MASK) >> MATERIAL_LIGHT_STYLE_SHIFT;
		if(light_style != 0) 
		{
			minfo.emissive_scale *= get_light_styles(light_style);
		}
	}

	return minfo;
}

LightPolygon
get_light_polygon(uint index)
{
	vec4 p0 = get_light_polys(index * LIGHT_POLY_VEC4S + 0);
	vec4 p1 = get_light_polys(index * LIGHT_POLY_VEC4S + 1);
	vec4 p2 = get_light_polys(index * LIGHT_POLY_VEC4S + 2);
	vec4 p3 = get_light_polys(index * LIGHT_POLY_VEC4S + 3);

	LightPolygon light;
	light.positions = mat3x3(p0.xyz, p1.xyz, p2.xyz);
	light.color = vec3(p0.w, p1.w, p2.w);
	light.light_style_scale = p3.x;
	light.prev_style_scale = p3.y;
	return light;
}

#endif
#endif
