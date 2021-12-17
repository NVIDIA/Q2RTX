/*
Copyright (C) 2018 Christoph Schied
Copyright (C) 2019-2021, NVIDIA CORPORATION. All rights reserved.

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

#include "shader_structs.h"

#define MAX_LIGHT_LISTS         (1 << 14)
#define MAX_LIGHT_LIST_NODES    (1 << 19)

#define MAX_IQM_MATRICES        32768

#define MAX_LIGHT_POLYS         4096
#define LIGHT_POLY_VEC4S        4
#define MATERIAL_UINTS          6

// should match the same constant declared in material.h
#define MAX_PBR_MATERIALS      4096

#define LIGHT_TEXTURE_SCALE     0

#define ALIGN_SIZE_4(x, n)  ((x * n + 3) & (~3))

#define PRIMITIVE_BUFFER_BINDING_IDX 0
#define POSITION_BUFFER_BINIDNG_IDX 1
#define LIGHT_BUFFER_BINDING_IDX 2
#define IQM_MATRIX_BUFFER_BINDING_IDX 3
#define READBACK_BUFFER_BINDING_IDX 4
#define TONE_MAPPING_BUFFER_BINDING_IDX 5
#define SUN_COLOR_BUFFER_BINDING_IDX 6
#define SUN_COLOR_UBO_BINDING_IDX 7
#define LIGHT_STATS_BUFFER_BINDING_IDX 8

#define VERTEX_BUFFER_WORLD 0
#define VERTEX_BUFFER_INSTANCED 1
#define VERTEX_BUFFER_FIRST_MODEL 2

#define SUN_COLOR_ACCUMULATOR_FIXED_POINT_SCALE 0x100000
#define SKY_COLOR_ACCUMULATOR_FIXED_POINT_SCALE 0x100

#define VISBUF_INSTANCE_ID_MASK     0x000003FF
#define VISBUF_INSTANCE_PRIM_MASK   0x3FFFFC00
#define VISBUF_INSTANCE_PRIM_SHIFT  10
#define VISBUF_STATIC_PRIM_MASK     0x7FFFFFFF
#define VISBUF_STATIC_PRIM_FLAG 	0x80000000

// A structure that is used in primitive buffers to store complete information about one triangle. 
// Its size is 8x float4 or 128 bytes to align with GPU cache lines.
// Path tracing accesses the primitive information in a very incoherent way, where every thread
// is likely to read a different primitive. Packing the info into one struct should reduce the
// total traffic from video memory by reading entire cache lines instead of sparse values from
// different buffers.
BEGIN_SHADER_STRUCT( VboPrimitive )
{
	vec3 pos0;
	uint material_id;

	vec3 pos1;
	int cluster;

	vec3 pos2;
	float texel_density;

	vec2 uv0;
	vec2 uv1;

	vec2 uv2;
	uvec2 motion0;

	uvec3 motion12;
	float emissive_factor;

	uvec3 normals;
	uint instance;

	uvec3 tangents;
	uint pad;
}
END_SHADER_STRUCT( VboPrimitive )


#ifdef VKPT_SHADER

#include "read_visbuf.glsl"

#ifdef VERTEX_READONLY
#define VERTEX_READONLY_FLAG readonly
#else
#define VERTEX_READONLY_FLAG
#endif

// The buffers with primitive data, currently two of them: world and instanced.
// They are stored in an array to allow branchless access with nonuniformEXT.
layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = PRIMITIVE_BUFFER_BINDING_IDX) VERTEX_READONLY_FLAG buffer PRIMITIVE_BUFFER {
	VboPrimitive primitives[];
} primitive_buffers[];

// The buffer with just the position data for animated models.
layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = POSITION_BUFFER_BINIDNG_IDX) VERTEX_READONLY_FLAG buffer POSITION_BUFFER {
	float positions[];
} instanced_position_buffer;

#endif

#define LIGHT_BUFFER_LIST \
	VERTEX_BUFFER_LIST_DO(uint, 1, material_table,        (MAX_PBR_MATERIALS * MATERIAL_UINTS)) \
	VERTEX_BUFFER_LIST_DO(float,    4, light_polys,           (MAX_LIGHT_POLYS * LIGHT_POLY_VEC4S)) \
	VERTEX_BUFFER_LIST_DO(uint, 1, light_list_offsets,    (MAX_LIGHT_LISTS     )) \
	VERTEX_BUFFER_LIST_DO(uint, 1, light_list_lights,     (MAX_LIGHT_LIST_NODES)) \
	VERTEX_BUFFER_LIST_DO(float,    1, light_styles,          (MAX_LIGHT_STYLES    )) \
	VERTEX_BUFFER_LIST_DO(uint, 1, cluster_debug_mask,    (MAX_LIGHT_LISTS / 32)) \
	VERTEX_BUFFER_LIST_DO(uint, 1, sky_visibility,        (MAX_LIGHT_LISTS / 32)) \

#define IQM_MATRIX_BUFFER_LIST \
	VERTEX_BUFFER_LIST_DO(float,    4, iqm_matrices,          (MAX_IQM_MATRICES)) \

#define VERTEX_BUFFER_LIST_DO(type, dim, name, size) \
	type name[ALIGN_SIZE_4(size, dim)];

struct LightBuffer
{
	LIGHT_BUFFER_LIST
};

struct IqmMatrixBuffer
{
	IQM_MATRIX_BUFFER_LIST
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
	uint material;
	uint cluster;
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
typedef struct LightBuffer LightBuffer;
typedef struct IqmMatrixBuffer IqmMatrixBuffer;
typedef struct ReadbackBuffer ReadbackBuffer;
typedef struct ToneMappingBuffer ToneMappingBuffer;
typedef struct SunColorBuffer SunColorBuffer;
#endif

#ifdef VKPT_SHADER

struct MaterialInfo
{
	uint base_texture;
	uint normals_texture;
	uint emissive_texture;
	uint mask_texture;
	float bump_scale;
	float roughness_override;
	float metalness_factor;
	float emissive_factor;
	float specular_factor;
	float base_factor;
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

layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = LIGHT_BUFFER_BINDING_IDX) readonly buffer LIGHT_BUFFER {
	LightBuffer lbo;
};

layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = IQM_MATRIX_BUFFER_BINDING_IDX) readonly buffer IQM_MATRIX_BUFFER {
	IqmMatrixBuffer iqmbo;
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

#define GET_uint_1(buf,name) \
uint \
get_##name(uint idx) \
{ \
	return buf.name[idx]; \
}

#define GET_uint_3(buf,name) \
uvec3 \
get_##name(uint idx) \
{ \
	return uvec3(buf.name[idx * 3 + 0], buf.name[idx * 3 + 1], buf.name[idx * 3 + 2]); \
}

#define GET_uint_4(buf,name) \
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

#define SET_uint_1(buf,name) \
void \
set_##name(uint idx, uint u) \
{ \
	buf.name[idx] = u; \
}

#define SET_uint_3(buf,name) \
void \
set_##name(uint idx, uvec3 v) \
{ \
	buf.name[idx * 3 + 0] = v[0]; \
	buf.name[idx * 3 + 1] = v[1]; \
	buf.name[idx * 3 + 2] = v[2]; \
}
#endif

#define VERTEX_BUFFER_LIST_DO(type, dim, name, size) \
	GET_##type##_##dim(lbo,name)
LIGHT_BUFFER_LIST
#undef VERTEX_BUFFER_LIST_DO

#define VERTEX_BUFFER_LIST_DO(type, dim, name, size) \
	GET_##type##_##dim(iqmbo,name)
IQM_MATRIX_BUFFER_LIST
#undef VERTEX_BUFFER_LIST_DO

struct Triangle
{
	mat3x3 positions;
	mat3x3 positions_prev;
	mat3x3 normals;
	mat3x2 tex_coords;
	mat3x3 tangents;
	uint   material_id;
	int    cluster;
	uint   instance;
	float  texel_density;
	float  emissive_factor;
};

Triangle
load_triangle(uint buffer_idx, uint prim_id)
{
	VboPrimitive prim = primitive_buffers[nonuniformEXT(buffer_idx)].primitives[prim_id];

	Triangle t;
	t.positions[0] = prim.pos0;
	t.positions[1] = prim.pos1;
	t.positions[2] = prim.pos2;

	t.positions_prev[0] = t.positions[0] + unpackHalf4x16(prim.motion0).xyz;
	t.positions_prev[1] = t.positions[1] + unpackHalf4x16(prim.motion12.xy).xyz;
	t.positions_prev[2] = t.positions[2] + unpackHalf4x16(prim.motion12.yz).yzw;
	
	t.normals[0] = decode_normal(prim.normals.x);
	t.normals[1] = decode_normal(prim.normals.y);
	t.normals[2] = decode_normal(prim.normals.z);

	t.tangents[0] = decode_normal(prim.tangents.x);
	t.tangents[1] = decode_normal(prim.tangents.y);
	t.tangents[2] = decode_normal(prim.tangents.z);

	t.tex_coords[0] = prim.uv0;
	t.tex_coords[1] = prim.uv1;
	t.tex_coords[2] = prim.uv2;

	t.material_id = prim.material_id;
	t.cluster = prim.cluster;
	t.instance = prim.instance;
	t.texel_density = prim.texel_density;
	t.emissive_factor = prim.emissive_factor;

	return t;
}

Triangle
load_and_transform_triangle(int instance_idx, uint buffer_idx, uint prim_id)
{
	Triangle t = load_triangle(buffer_idx, prim_id);

	if (instance_idx >= 0)
	{
		ModelInstance mi = instance_buffer.model_instances[instance_idx];
		
		t.positions[0] = vec3(mi.transform * vec4(t.positions[0], 1.0));
		t.positions[1] = vec3(mi.transform * vec4(t.positions[1], 1.0));
		t.positions[2] = vec3(mi.transform * vec4(t.positions[2], 1.0));

		t.positions_prev[0] = vec3(mi.transform_prev * vec4(t.positions_prev[0], 1.0));
		t.positions_prev[1] = vec3(mi.transform_prev * vec4(t.positions_prev[1], 1.0));
		t.positions_prev[2] = vec3(mi.transform_prev * vec4(t.positions_prev[2], 1.0));

		t.normals[0] = normalize(vec3(mi.transform * vec4(t.normals[0], 0.0)));
		t.normals[1] = normalize(vec3(mi.transform * vec4(t.normals[1], 0.0)));
		t.normals[2] = normalize(vec3(mi.transform * vec4(t.normals[2], 0.0)));

		t.tangents[0] = normalize(vec3(mi.transform * vec4(t.tangents[0], 0.0)));
		t.tangents[1] = normalize(vec3(mi.transform * vec4(t.tangents[1], 0.0)));
		t.tangents[2] = normalize(vec3(mi.transform * vec4(t.tangents[2], 0.0)));

		if (mi.material != 0)
			t.material_id = mi.material;
		t.cluster = mi.cluster;
		t.emissive_factor = mi.alpha;
		t.instance = visbuf_pack_instance(instance_idx, prim_id - mi.render_prim_offset);
	}

	return t;
}

#ifndef VERTEX_READONLY
void
store_triangle(Triangle t, uint buffer_idx, uint prim_id)
{
	VboPrimitive prim;

	prim.pos0 = t.positions[0];
	prim.pos1 = t.positions[1];
	prim.pos2 = t.positions[2];

	prim.motion0 = packHalf4x16(vec4(t.positions_prev[0] - t.positions[0], 0));
	prim.motion12.x = packHalf2x16(t.positions_prev[1].xy - t.positions[1].xy);
	prim.motion12.y = packHalf2x16(vec2(t.positions_prev[1].z - t.positions[1].z, t.positions_prev[2].x - t.positions[2].x));
	prim.motion12.z = packHalf2x16(t.positions_prev[2].yz - t.positions[2].yz);

	prim.normals.x = encode_normal(t.normals[0]);
	prim.normals.y = encode_normal(t.normals[1]);
	prim.normals.z = encode_normal(t.normals[2]);

	prim.tangents.x = encode_normal(t.tangents[0]);
	prim.tangents.y = encode_normal(t.tangents[1]);
	prim.tangents.z = encode_normal(t.tangents[2]);

	prim.uv0 = t.tex_coords[0];
	prim.uv1 = t.tex_coords[1];
	prim.uv2 = t.tex_coords[2];

	prim.material_id = t.material_id;
	prim.cluster = t.cluster;
	prim.instance = t.instance;
	prim.texel_density = t.texel_density;
	prim.emissive_factor = t.emissive_factor;
	prim.pad = 0;

	primitive_buffers[nonuniformEXT(buffer_idx)].primitives[prim_id] = prim;

	if (buffer_idx == VERTEX_BUFFER_INSTANCED)
	{
		for (int vert = 0; vert < 3; vert++)
		{
			for (int axis = 0; axis < 3; axis++)
			{
				instanced_position_buffer.positions[prim_id * 9 + vert * 3 + axis] 
					= t.positions[vert][axis];
			}
		}
	}
}
#endif

MaterialInfo
get_material_info(uint material_id)
{
	uint material_index = material_id & MATERIAL_INDEX_MASK;
	
	uint data[MATERIAL_UINTS];
	data[0] = get_material_table(material_index * MATERIAL_UINTS + 0);
	data[1] = get_material_table(material_index * MATERIAL_UINTS + 1);
	data[2] = get_material_table(material_index * MATERIAL_UINTS + 2);
	data[3] = get_material_table(material_index * MATERIAL_UINTS + 3);
	data[4] = get_material_table(material_index * MATERIAL_UINTS + 4);
	data[5] = get_material_table(material_index * MATERIAL_UINTS + 5);

	MaterialInfo minfo;
	minfo.base_texture = data[0] & 0xffff;
	minfo.normals_texture = data[0] >> 16;
	minfo.emissive_texture = data[1] & 0xffff;
	minfo.mask_texture = data[1] >> 16;
	minfo.bump_scale = unpackHalf2x16(data[2]).x;
	minfo.roughness_override = unpackHalf2x16(data[2]).y;
	minfo.metalness_factor = unpackHalf2x16(data[3]).x;
	minfo.emissive_factor = unpackHalf2x16(data[3]).y;
	minfo.specular_factor = unpackHalf2x16(data[5]).x;
	minfo.base_factor = unpackHalf2x16(data[5]).y;
	minfo.num_frames = data[4] & 0xffff;
	minfo.next_frame = (data[4] >> 16) & (MAX_PBR_MATERIALS - 1);

	// Apply the light style for non-camera materials.
	// Camera materials use the same bits to store the camera ID.
	if((material_id & MATERIAL_KIND_MASK) != MATERIAL_KIND_CAMERA)
	{
		uint light_style = (material_id & MATERIAL_LIGHT_STYLE_MASK) >> MATERIAL_LIGHT_STYLE_SHIFT;
		if(light_style != 0) 
		{
			minfo.emissive_factor *= get_light_styles(light_style);
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

mat3x4
get_iqm_matrix(uint index)
{
	mat3x4 result;
	result[0] = get_iqm_matrices(index * 3 + 0);
	result[1] = get_iqm_matrices(index * 3 + 1);
	result[2] = get_iqm_matrices(index * 3 + 2);
	return result;
}

#endif
#endif
