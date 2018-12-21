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

uint
pack_instance_id_triangle_idx(uint instance_id, uint triangle_idx)
{
	return instance_id | (triangle_idx << 10);
}

void
unpack_instance_id_triangle_idx(uint u, out uint instance_id, out uint triangle_idx)
{
	instance_id  = (u &  (1 << 31)) | (u & ((1 << 10) - 1));
	triangle_idx = (u & ~(1 << 31)) >> 10;
}

bool is_world_instance(uint u)     { return (u & (1 << 31)) > 0; }
bool is_static_world_model(uint u) { return u == ~0u;            }

uint
map_instance_fwd(uint id)
{
	if(is_world_instance(id)) {
	   	if(!is_static_world_model(id)) {
			id &= ~(1 << 31);
			id  = global_ubo.world_prev_to_current[id / 4][id % 4];
			id |= 1 << 31;
		}
	}
	else {
		id = global_ubo.model_prev_to_current[id / 4][id % 4];
	}
	return id;
}

void
visbuf_get_triangle(out Triangle t, uint instance, uint primitive)
{
	if(is_world_instance(instance)) {
		if(!is_static_world_model(instance)) {
			instance  &= ~(1 << 31);
			primitive += global_ubo.bsp_prim_offset[instance / 4][instance % 4];
		}

		t = get_bsp_triangle(primitive);

		if(!is_static_world_model(instance)) {
			mat4 M = global_ubo.bsp_mesh_instances[instance].M;

			t.positions[0] = vec3(M * vec4(t.positions[0], 1.0));
			t.positions[1] = vec3(M * vec4(t.positions[1], 1.0));
			t.positions[2] = vec3(M * vec4(t.positions[2], 1.0));
			
			t.normals[0] = vec3(M * vec4(t.normals[0], 0.0));
			t.normals[1] = vec3(M * vec4(t.normals[1], 0.0));
			t.normals[2] = vec3(M * vec4(t.normals[2], 0.0));

			t.cluster = global_ubo.bsp_cluster_id[instance / 4][instance % 4];
		}
	}
	else {
		uint idx_offset = global_ubo.model_idx_offset[instance / 4][instance % 4];
		uint idx        = primitive;

		ModelInstance mi_curr = global_ubo.model_instances[instance];
		uint vertex_off_curr  = mi_curr.mat_offset_backlerp.y;
		uint vertex_off_prev  = mi_curr.mat_offset_backlerp.z; // referes to animation frame

		Triangle t_curr = get_model_triangle(idx, idx_offset, vertex_off_curr);
		Triangle t_prev = get_model_triangle(idx, idx_offset, vertex_off_prev);

		mat4  M        = mi_curr.M;
		float backlerp = uintBitsToFloat(mi_curr.mat_offset_backlerp.w);

		t.positions[0] = (M * vec4(mix(t_curr.positions[0], t_prev.positions[0], backlerp), 1.0)).xyz;
		t.positions[1] = (M * vec4(mix(t_curr.positions[1], t_prev.positions[1], backlerp), 1.0)).xyz;
		t.positions[2] = (M * vec4(mix(t_curr.positions[2], t_prev.positions[2], backlerp), 1.0)).xyz;

		t.normals[0] = (M * vec4(mix(t_curr.normals[0], t_prev.normals[0], backlerp), 0.0)).xyz;
		t.normals[1] = (M * vec4(mix(t_curr.normals[1], t_prev.normals[1], backlerp), 0.0)).xyz;
		t.normals[2] = (M * vec4(mix(t_curr.normals[2], t_prev.normals[2], backlerp), 0.0)).xyz;

		t.tex_coords  = t_curr.tex_coords;
		t.material_id = mi_curr.mat_offset_backlerp.x;

		t.cluster = global_ubo.model_cluster_id[instance / 4][instance % 4];
	}
}

void
visbuf_get_triangle(out Triangle t, vec4 vis)
{
	uvec2 v = floatBitsToUint(vis.zw);
	visbuf_get_triangle(t, v.x, v.y);
}

void
visbuf_get_triangle_backprj(out Triangle t, uint instance, uint primitive)
{
	if(is_world_instance(instance)) {
		if(!is_static_world_model(instance)) {
			instance  &= ~(1 << 31);
			primitive += global_ubo.bsp_prim_offset[instance / 4][instance % 4];
		}

		t = get_bsp_triangle(primitive);

		if(!is_static_world_model(instance)) {
			instance = global_ubo.world_current_to_prev[instance / 4][instance % 4];
			mat4 M = global_ubo.bsp_mesh_instances_prev[instance].M;

			t.positions[0] = vec3(M * vec4(t.positions[0], 1.0));
			t.positions[1] = vec3(M * vec4(t.positions[1], 1.0));
			t.positions[2] = vec3(M * vec4(t.positions[2], 1.0));
			
			t.normals[0] = vec3(M * vec4(t.normals[0], 0.0));
			t.normals[1] = vec3(M * vec4(t.normals[1], 0.0));
			t.normals[2] = vec3(M * vec4(t.normals[2], 0.0));

			t.cluster = global_ubo.bsp_cluster_id[instance / 4][instance % 4];
		}
	}
	else {
		uint idx_offset = global_ubo.model_idx_offset[instance / 4][instance % 4];
		uint idx        = primitive;

		instance = global_ubo.model_current_to_prev[instance / 4][instance % 4];

		ModelInstance mi_curr = global_ubo.model_instances_prev[instance];
		uint vertex_off_curr  = mi_curr.mat_offset_backlerp.y;
		uint vertex_off_prev  = mi_curr.mat_offset_backlerp.z; // referes to animation frame

		Triangle t_curr = get_model_triangle(idx, idx_offset, vertex_off_curr);
		Triangle t_prev = get_model_triangle(idx, idx_offset, vertex_off_prev);

		mat4  M        = mi_curr.M;
		float backlerp = uintBitsToFloat(mi_curr.mat_offset_backlerp.w);

		t.positions[0] = (M * vec4(mix(t_curr.positions[0], t_prev.positions[0], backlerp), 1.0)).xyz;
		t.positions[1] = (M * vec4(mix(t_curr.positions[1], t_prev.positions[1], backlerp), 1.0)).xyz;
		t.positions[2] = (M * vec4(mix(t_curr.positions[2], t_prev.positions[2], backlerp), 1.0)).xyz;

		t.normals[0] = (M * vec4(mix(t_curr.normals[0], t_prev.normals[0], backlerp), 0.0)).xyz;
		t.normals[1] = (M * vec4(mix(t_curr.normals[1], t_prev.normals[1], backlerp), 0.0)).xyz;
		t.normals[2] = (M * vec4(mix(t_curr.normals[2], t_prev.normals[2], backlerp), 0.0)).xyz;

		t.tex_coords  = t_curr.tex_coords;
		t.material_id = mi_curr.mat_offset_backlerp.x;
	}
}

void
visbuf_get_triangle_backprj(out Triangle t, vec4 vis)
{
	uvec2 v = floatBitsToUint(vis.zw);
	visbuf_get_triangle_backprj(t, v.x, v.y);
}

bool
visbuf_get_triangle_fwdprj(out Triangle t, uint instance, uint primitive)
{
	if(is_world_instance(instance)) {
		if(!is_static_world_model(instance)) {
			instance  &= ~(1 << 31);
			primitive += global_ubo.bsp_prim_offset[instance / 4][instance % 4];
		}

		t = get_bsp_triangle(primitive);

		if(!is_static_world_model(instance)) {
			instance = global_ubo.world_prev_to_current[instance / 4][instance % 4];
			if(instance == ~0u)
				return false;
			mat4 M = global_ubo.bsp_mesh_instances[instance].M;

			t.positions[0] = vec3(M * vec4(t.positions[0], 1.0));
			t.positions[1] = vec3(M * vec4(t.positions[1], 1.0));
			t.positions[2] = vec3(M * vec4(t.positions[2], 1.0));
			
			t.normals[0] = vec3(M * vec4(t.normals[0], 0.0));
			t.normals[1] = vec3(M * vec4(t.normals[1], 0.0));
			t.normals[2] = vec3(M * vec4(t.normals[2], 0.0));

			t.cluster = global_ubo.bsp_cluster_id[instance / 4][instance % 4];
		}
	}
	else {
		instance = global_ubo.model_prev_to_current[instance / 4][instance % 4];
		if(instance == ~0u)
			return false;

		uint idx_offset = global_ubo.model_idx_offset[instance / 4][instance % 4];
		uint idx        = primitive;

		ModelInstance mi_curr = global_ubo.model_instances[instance];
		uint vertex_off_curr  = mi_curr.mat_offset_backlerp.y;
		uint vertex_off_prev  = mi_curr.mat_offset_backlerp.z; // referes to animation frame

		Triangle t_curr = get_model_triangle(idx, idx_offset, vertex_off_curr);
		Triangle t_prev = get_model_triangle(idx, idx_offset, vertex_off_prev);

		mat4  M        = mi_curr.M;
		float backlerp = uintBitsToFloat(mi_curr.mat_offset_backlerp.w);

		t.positions[0] = (M * vec4(mix(t_curr.positions[0], t_prev.positions[0], backlerp), 1.0)).xyz;
		t.positions[1] = (M * vec4(mix(t_curr.positions[1], t_prev.positions[1], backlerp), 1.0)).xyz;
		t.positions[2] = (M * vec4(mix(t_curr.positions[2], t_prev.positions[2], backlerp), 1.0)).xyz;

		t.normals[0] = (M * vec4(mix(t_curr.normals[0], t_prev.normals[0], backlerp), 0.0)).xyz;
		t.normals[1] = (M * vec4(mix(t_curr.normals[1], t_prev.normals[1], backlerp), 0.0)).xyz;
		t.normals[2] = (M * vec4(mix(t_curr.normals[2], t_prev.normals[2], backlerp), 0.0)).xyz;

		t.tex_coords  = t_curr.tex_coords;
		t.material_id = mi_curr.mat_offset_backlerp.x;
	}
	return true;
}

bool
visbuf_get_triangle_fwdprj(out Triangle t, vec4 vis)
{
	uvec2 v = floatBitsToUint(vis.zw);
	return visbuf_get_triangle_fwdprj(t, v.x, v.y);
}
