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

#ifndef  _GLOBAL_UBO_DESCRIPTOR_SET_LAYOUT_H_
#define  _GLOBAL_UBO_DESCRIPTOR_SET_LAYOUT_H_

#define SHADER_MAX_ENTITIES                  256
#define SHADER_MAX_BSP_ENTITIES              32
#define MAX_LIGHT_SOURCES                    32

#define GLOBAL_UBO_BINDING_IDX               0

/* glsl alignment rules make me very very sad :'( */
#define GLOBAL_UBO_VAR_LIST \
	GLOBAL_UBO_VAR_LIST_DO(int,             current_frame_idx) \
	GLOBAL_UBO_VAR_LIST_DO(int,             width) \
	GLOBAL_UBO_VAR_LIST_DO(int,             height)\
	GLOBAL_UBO_VAR_LIST_DO(int,             num_lights) \
	\
	GLOBAL_UBO_VAR_LIST_DO(int,             under_water) \
	GLOBAL_UBO_VAR_LIST_DO(float,           time) \
	GLOBAL_UBO_VAR_LIST_DO(int,             num_instances_model_bsp) /* 16 bit each */ \
	GLOBAL_UBO_VAR_LIST_DO(int,             padding2) \
	\
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           model_current_to_prev    [SHADER_MAX_ENTITIES / 4]) \
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           model_prev_to_current    [SHADER_MAX_ENTITIES / 4]) \
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           world_current_to_prev    [SHADER_MAX_BSP_ENTITIES / 4]) \
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           world_prev_to_current    [SHADER_MAX_BSP_ENTITIES / 4]) \
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           bsp_prim_offset          [SHADER_MAX_BSP_ENTITIES / 4]) \
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           model_idx_offset         [SHADER_MAX_ENTITIES / 4]) \
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           light_offset_cnt         [MAX_LIGHT_SOURCES]) \
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           model_cluster_id         [SHADER_MAX_ENTITIES / 4]) \
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           bsp_cluster_id           [SHADER_MAX_BSP_ENTITIES / 4]) \
	GLOBAL_UBO_VAR_LIST_DO(vec4,            cam_pos) \
	GLOBAL_UBO_VAR_LIST_DO(mat4,            invVP) \
	GLOBAL_UBO_VAR_LIST_DO(mat4,            VP) \
	GLOBAL_UBO_VAR_LIST_DO(mat4,            VP_prev) \
	GLOBAL_UBO_VAR_LIST_DO(mat4,            V) \
	GLOBAL_UBO_VAR_LIST_DO(ModelInstance,   model_instances          [SHADER_MAX_ENTITIES]) \
	GLOBAL_UBO_VAR_LIST_DO(ModelInstance,   model_instances_prev     [SHADER_MAX_ENTITIES]) \
	GLOBAL_UBO_VAR_LIST_DO(BspMeshInstance, bsp_mesh_instances       [SHADER_MAX_BSP_ENTITIES]) \
	GLOBAL_UBO_VAR_LIST_DO(BspMeshInstance, bsp_mesh_instances_prev  [SHADER_MAX_BSP_ENTITIES]) \
	/* stores the offset into the instance buffer in numberof primitives */ \
	GLOBAL_UBO_VAR_LIST_DO(uvec4,           instance_buf_offset      [(SHADER_MAX_ENTITIES + SHADER_MAX_BSP_ENTITIES) / 4]) \

#ifndef VKPT_SHADER

#if SHADER_MAX_ENTITIES != MAX_ENTITIES
#error need to update constant here
#endif

typedef uint32_t uvec4_t[4];

typedef struct ModelInstance_s {
	float M[16]; // 16
	uint32_t material; int offset_curr, offset_prev; float backlerp; // 4
} ModelInstance_t;

typedef struct BspMeshInstance_s {
	float M[16];
} BspMeshInstance_t;

#define int_t int32_t
typedef struct QVKUniformBuffer_s {
#define GLOBAL_UBO_VAR_LIST_DO(type, name) type##_t name;
	GLOBAL_UBO_VAR_LIST
#undef  GLOBAL_UBO_VAR_LIST_DO
} QVKUniformBuffer_t;
#undef int_t

#else

struct ModelInstance {
	mat4 M;
	uvec4 mat_offset_backlerp;
};

struct BspMeshInstance {
	mat4 M;
};

struct GlobalUniformBuffer {
#define GLOBAL_UBO_VAR_LIST_DO(type, name) type name;
	GLOBAL_UBO_VAR_LIST
#undef  GLOBAL_UBO_VAR_LIST_DO
};

layout(set = GLOBAL_UBO_DESC_SET_IDX, binding = GLOBAL_UBO_BINDING_IDX, std140) uniform UBO {
	GlobalUniformBuffer global_ubo;
};

#endif



#endif  /*_GLOBAL_UBO_DESCRIPTOR_SET_LAYOUT_H_*/
