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


#define MAX_VERT_BSP            (1 << 21)

#define MAX_VERT_MODEL          (1 << 21)
#define MAX_IDX_MODEL           (1 << 21)

#define MAX_VERT_INSTANCED      (1 << 21)
#define MAX_IDX_INSTANCED       (MAX_VERT_INSTANCED / 3)

#define MAX_LIGHT_LISTS         (1 << 14)
#define MAX_LIGHT_LIST_NODES    (1 << 20)

#define ALIGN_SIZE_4(x, n)  ((x * n + 3) & (~3))

#define VERTEX_BUFFER_BINDING_IDX 0

#ifdef VKPT_SHADER
#define uint32_t uint
#endif

#define VERTEX_BUFFER_LIST \
	VERTEX_BUFFER_LIST_DO(float,    3, positions_bsp,         (MAX_VERT_BSP        )) \
	VERTEX_BUFFER_LIST_DO(float,    2, tex_coords_bsp,        (MAX_VERT_BSP        )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, materials_bsp,         (MAX_VERT_BSP / 3    )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, clusters_bsp,          (MAX_VERT_BSP / 3    )) \
	\
	VERTEX_BUFFER_LIST_DO(float,    3, positions_model,       (MAX_VERT_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(float,    3, normals_model,         (MAX_VERT_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(float,    2, tex_coords_model,      (MAX_VERT_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 3, idx_model,             (MAX_IDX_MODEL       )) \
	\
	VERTEX_BUFFER_LIST_DO(float,    3, positions_instanced,   (MAX_VERT_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(float,    3, normals_instanced,     (MAX_VERT_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(float,    2, tex_coords_instanced,  (MAX_VERT_MODEL      )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, clusters_instanced,    (MAX_IDX_MODEL       )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, materials_instanced,   (MAX_IDX_MODEL       )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, instance_id_instanced, (MAX_IDX_MODEL       )) \
	\
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, light_list_offsets,    (MAX_LIGHT_LISTS     )) \
	VERTEX_BUFFER_LIST_DO(uint32_t, 1, light_list_lights,     (MAX_LIGHT_LIST_NODES)) \


struct VertexBuffer
{
#define VERTEX_BUFFER_LIST_DO(type, dim, name, size) \
	type name[ALIGN_SIZE_4(size, dim)];

	VERTEX_BUFFER_LIST

#undef VERTEX_BUFFER_LIST_DO
};

#ifndef VKPT_SHADER
typedef struct VertexBuffer VertexBuffer;
#endif

#ifdef VKPT_SHADER

layout(set = VERTEX_BUFFER_DESC_SET_IDX, binding = VERTEX_BUFFER_BINDING_IDX) buffer VERTEX_BUFFER {
	VertexBuffer vbo;
};

#define GET_float_2(name) \
vec2 \
get_##name(uint idx) \
{ \
	return vec2(vbo.name[idx * 2 + 0], vbo.name[idx * 2 + 1]); \
}

#define GET_float_3(name) \
vec3 \
get_##name(uint idx) \
{ \
	return vec3(vbo.name[idx * 3 + 0], vbo.name[idx * 3 + 1], vbo.name[idx * 3 + 2]); \
}

#define GET_uint32_t_1(name) \
uint \
get_##name(uint idx) \
{ \
	return vbo.name[idx]; \
}

#define GET_uint32_t_3(name) \
uvec3 \
get_##name(uint idx) \
{ \
	return uvec3(vbo.name[idx * 3 + 0], vbo.name[idx * 3 + 1], vbo.name[idx * 3 + 2]); \
}

#define SET_float_2(name) \
void \
set_##name(uint idx, vec2 v) \
{ \
	vbo.name[idx * 2 + 0] = v[0]; \
	vbo.name[idx * 2 + 1] = v[1]; \
}

#define SET_float_3(name) \
void \
set_##name(uint idx, vec3 v) \
{ \
	vbo.name[idx * 3 + 0] = v[0]; \
	vbo.name[idx * 3 + 1] = v[1]; \
	vbo.name[idx * 3 + 2] = v[2]; \
}

#define SET_uint32_t_1(name) \
void \
set_##name(uint idx, uint u) \
{ \
	vbo.name[idx] = u; \
}

#define SET_uint32_t_3(name) \
void \
set_##name(uint idx, uvec3 v) \
{ \
	vbo.name[idx * 3 + 0] = v[0]; \
	vbo.name[idx * 3 + 1] = v[1]; \
	vbo.name[idx * 3 + 2] = v[2]; \
}

#define VERTEX_BUFFER_LIST_DO(type, dim, name, size) \
	GET_##type##_##dim(name) \
	SET_##type##_##dim(name)
VERTEX_BUFFER_LIST
#undef VERTEX_BUFFER_LIST_DO

struct Triangle
{
	mat3x3 positions;
	mat3x3 normals;
	mat3x2 tex_coords;
	uint   material_id;
	uint   cluster;
};

struct InstancedTriangle
{
	mat3x3 positions;
	mat3x3 normals;
	mat3x2 tex_coords;
	mat3x3 positions_prev;
	uint   material_id;
};

Triangle
get_bsp_triangle(uint prim_id)
{
	Triangle t;
	t.positions[0] = get_positions_bsp(prim_id * 3 + 0);
	t.positions[1] = get_positions_bsp(prim_id * 3 + 1);
	t.positions[2] = get_positions_bsp(prim_id * 3 + 2);

	vec3 normal = normalize(cross(
				t.positions[1] - t.positions[0],
				t.positions[2] - t.positions[0]));

	t.normals[0] = normal;
	t.normals[1] = normal;
	t.normals[2] = normal;

	t.tex_coords[0] = get_tex_coords_bsp(prim_id * 3 + 0);
	t.tex_coords[1] = get_tex_coords_bsp(prim_id * 3 + 1);
	t.tex_coords[2] = get_tex_coords_bsp(prim_id * 3 + 2);

	t.material_id = get_materials_bsp(prim_id);

	t.cluster = get_clusters_bsp(prim_id);

	return t;
}

Triangle
get_model_triangle(uint prim_id, uint idx_offset, uint vert_offset)
{
	uvec3 idx = get_idx_model(prim_id + idx_offset / 3);
	idx += vert_offset;

	Triangle t;
	t.positions[0] = get_positions_model(idx[0]);
	t.positions[1] = get_positions_model(idx[1]);
	t.positions[2] = get_positions_model(idx[2]);

	vec3 normal = normalize(cross(
				t.positions[1] - t.positions[0],
				t.positions[2] - t.positions[0]));

	t.normals[0] = get_normals_model(idx[0]);
	t.normals[1] = get_normals_model(idx[1]);
	t.normals[2] = get_normals_model(idx[2]);

	t.tex_coords[0] = get_tex_coords_model(idx[0]);
	t.tex_coords[1] = get_tex_coords_model(idx[1]);
	t.tex_coords[2] = get_tex_coords_model(idx[2]);

	t.material_id = 0; // needs to come from uniform buffer
	return t;
}

Triangle
get_instanced_triangle(uint prim_id)
{
	Triangle t;
	t.positions[0] = get_positions_instanced(prim_id * 3 + 0);
	t.positions[1] = get_positions_instanced(prim_id * 3 + 1);
	t.positions[2] = get_positions_instanced(prim_id * 3 + 2);

	vec3 normal = normalize(cross(
				t.positions[1] - t.positions[0],
				t.positions[2] - t.positions[0]));

	t.normals[0] = get_normals_instanced(prim_id * 3 + 0);
	t.normals[1] = get_normals_instanced(prim_id * 3 + 1);
	t.normals[2] = get_normals_instanced(prim_id * 3 + 2);

	t.tex_coords[0] = get_tex_coords_instanced(prim_id * 3 + 0);
	t.tex_coords[1] = get_tex_coords_instanced(prim_id * 3 + 1);
	t.tex_coords[2] = get_tex_coords_instanced(prim_id * 3 + 2);

	t.material_id = get_materials_instanced(prim_id);

	t.cluster = ~0u;

	return t;
}

void
store_instanced_triangle(InstancedTriangle t, uint instance_id, uint prim_id)
{
	set_positions_instanced(prim_id * 3 + 0, t.positions[0]);
	set_positions_instanced(prim_id * 3 + 1, t.positions[1]);
	set_positions_instanced(prim_id * 3 + 2, t.positions[2]);

	set_normals_instanced(prim_id * 3 + 0, t.normals[0]);
	set_normals_instanced(prim_id * 3 + 1, t.normals[1]);
	set_normals_instanced(prim_id * 3 + 2, t.normals[2]);

	set_tex_coords_instanced(prim_id * 3 + 0, t.tex_coords[0]);
	set_tex_coords_instanced(prim_id * 3 + 1, t.tex_coords[1]);
	set_tex_coords_instanced(prim_id * 3 + 2, t.tex_coords[2]);

	set_materials_instanced(prim_id, t.material_id);

	set_instance_id_instanced(prim_id, instance_id);
}

#endif
