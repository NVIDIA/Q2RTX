/*
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

#include <assert.h>
#include "shared/shared.h"
#include "vkpt.h"
#include "vk_util.h"
#include "color.h"
#include "conversion.h"

#define TR_PARTICLE_MAX_NUM    MAX_PARTICLES
#define TR_BEAM_MAX_NUM        MAX_ENTITIES
#define TR_SPRITE_MAX_NUM      MAX_ENTITIES
#define TR_VERTEX_MAX_NUM      ((TR_PARTICLE_MAX_NUM + TR_SPRITE_MAX_NUM) * 4)
#define TR_INDEX_MAX_NUM       ((TR_PARTICLE_MAX_NUM + TR_SPRITE_MAX_NUM) * 6)
#define TR_BEAM_AABB_SIZE      sizeof(VkAabbPositionsKHR)
#define TR_POSITION_SIZE       (3 * sizeof(float))
#define TR_COLOR_SIZE          (4 * sizeof(float))
#define TR_BEAM_INTERSECT_SIZE (12 * sizeof(float))
#define TR_SPRITE_INFO_SIZE    (2 * sizeof(float))

struct
{
	size_t vertex_position_host_offset;
	size_t beam_aabb_host_offset;
	size_t particle_color_host_offset;
	size_t beam_color_host_offset;
	size_t sprite_info_host_offset;
	size_t current_upload_size;

	size_t beam_intersect_host_offset;

	size_t sprite_vertex_device_offset;

	size_t host_buffer_size;
	size_t host_frame_size;
	unsigned int particle_num;
	unsigned int beam_num;
	unsigned int sprite_num;
	unsigned int host_frame_index;
	unsigned int host_buffered_frame_num;
	char* mapped_host_buffer;
	char* host_buffer_shadow;
	BufferResource_t vertex_buffer;
	BufferResource_t index_buffer;
	BufferResource_t beam_aabb_buffer;
	BufferResource_t particle_color_buffer;
	BufferResource_t beam_color_buffer;
	BufferResource_t sprite_info_buffer;
	BufferResource_t beam_intersect_buffer;
	VkBufferView particle_color_buffer_view;
	VkBufferView beam_color_buffer_view;
	VkBufferView sprite_info_buffer_view;
	VkBufferView beam_intersect_buffer_view;
	VkBuffer host_buffer;
	VkDeviceMemory host_buffer_memory;
	VkBufferMemoryBarrier transfer_barriers[6];
} transparency;

// initialization
static void create_buffers(void);
static bool allocate_and_bind_memory_to_buffers(void);
static void create_buffer_views(void);
static void fill_index_buffer(void);

// update
static void write_particle_geometry(const float* view_matrix, const particle_t* particles, int particle_num);
static void write_beam_geometry(const entity_t* entities, int entity_num);
static void write_sprite_geometry(const float* view_matrix, const entity_t* entities, int entity_num);
static void upload_geometry(VkCommandBuffer command_buffer);

cvar_t* cvar_pt_particle_size = NULL;
cvar_t* cvar_pt_beam_width = NULL;
cvar_t* cvar_pt_beam_lights = NULL;
extern cvar_t* cvar_pt_enable_particles;
extern cvar_t* cvar_pt_particle_emissive;
extern cvar_t* cvar_pt_projection;

void cast_u32_to_f32_color(int color_index, const color_t* pcolor, float* color_f32, float hdr_factor)
{
	color_t color;
	if (color_index < 0)
		color.u32 = pcolor->u32;
	else
		color.u32 = d_8to24table[color_index & 0xff];

	for (int i = 0; i < 3; i++)
		color_f32[i] = hdr_factor * decode_srgb(color.u8[i]);
}

bool initialize_transparency()
{
	cvar_pt_particle_size = Cvar_Get("pt_particle_size", "0.5", 0);
	cvar_pt_beam_width = Cvar_Get("pt_beam_width", "1.0", 0);
	cvar_pt_beam_lights = Cvar_Get("pt_beam_lights", "1.0", 0);

	memset(&transparency, 0, sizeof(transparency));

	const size_t particle_vertex_position_max_size = TR_VERTEX_MAX_NUM * TR_POSITION_SIZE;
	const size_t particle_color_size = TR_PARTICLE_MAX_NUM * TR_COLOR_SIZE;
	const size_t particle_data_size = particle_vertex_position_max_size + particle_color_size;

	const size_t beam_aabb_max_size = TR_BEAM_MAX_NUM * TR_BEAM_AABB_SIZE;
	const size_t beam_color_size = TR_BEAM_MAX_NUM * TR_COLOR_SIZE;
	const size_t beam_intersect_size = TR_BEAM_MAX_NUM * TR_BEAM_INTERSECT_SIZE;
	const size_t beam_data_size = beam_aabb_max_size + beam_color_size + beam_intersect_size;

	const size_t sprite_vertex_position_max_size = TR_SPRITE_MAX_NUM * TR_POSITION_SIZE;
	const size_t sprite_info_size = TR_SPRITE_MAX_NUM * TR_SPRITE_INFO_SIZE;
	const size_t sprite_data_size = sprite_vertex_position_max_size + sprite_info_size;
	
	transparency.host_buffered_frame_num = MAX_FRAMES_IN_FLIGHT;
	transparency.host_frame_size = particle_data_size + beam_data_size + sprite_data_size;
	transparency.host_buffer_size = transparency.host_buffered_frame_num * transparency.host_frame_size;

	create_buffers();

	if (allocate_and_bind_memory_to_buffers() != VK_TRUE)
		return false;

	create_buffer_views();
	fill_index_buffer();

	return true;
}

void destroy_transparency()
{
	vkDestroyBufferView(qvk.device, transparency.particle_color_buffer_view, NULL);
	vkDestroyBufferView(qvk.device, transparency.beam_color_buffer_view, NULL);
	vkDestroyBufferView(qvk.device, transparency.sprite_info_buffer_view, NULL);
	vkDestroyBufferView(qvk.device, transparency.beam_intersect_buffer_view, NULL);
	buffer_destroy(&transparency.vertex_buffer);
	buffer_destroy(&transparency.index_buffer);
	buffer_destroy(&transparency.beam_aabb_buffer);
	buffer_destroy(&transparency.particle_color_buffer);
	buffer_destroy(&transparency.beam_color_buffer);
	buffer_destroy(&transparency.sprite_info_buffer);
	buffer_destroy(&transparency.beam_intersect_buffer);

	vkDestroyBuffer(qvk.device, transparency.host_buffer, NULL);
	vkFreeMemory(qvk.device, transparency.host_buffer_memory, NULL);

	if (transparency.host_buffer_shadow)
	{
		Z_Freep((void**)&transparency.host_buffer_shadow);
	}
}

void update_transparency(VkCommandBuffer command_buffer, const float* view_matrix,
	const particle_t* particles, int particle_num, const entity_t* entities, int entity_num)
{
	transparency.host_frame_index = (transparency.host_frame_index + 1) % transparency.host_buffered_frame_num;
	particle_num = min(particle_num, TR_PARTICLE_MAX_NUM);

	uint32_t beam_num = 0;
	uint32_t sprite_num = 0;
	for (int i = 0; i < entity_num; i++)
	{
		if (entities[i].flags & RF_BEAM)
		{
			// write_beam_geometry skips zero-width beams as well
			if(entities[i].frame > 0)
				++beam_num;
		}
		else if ((entities[i].model & 0x80000000) == 0)
		{
			const model_t* model = MOD_ForHandle(entities[i].model);
			if (model && model->type == MOD_SPRITE)
				++sprite_num;
		}
	}
	beam_num = min(beam_num, TR_BEAM_MAX_NUM);
	sprite_num = min(sprite_num, TR_SPRITE_MAX_NUM);

	transparency.beam_num = beam_num;
	transparency.particle_num = particle_num;
	transparency.sprite_num = sprite_num;

	const size_t particle_vertices_size = particle_num * (4 * TR_POSITION_SIZE);
	const size_t sprite_vertices_size = sprite_num * (4 * TR_POSITION_SIZE);

	transparency.vertex_position_host_offset = 0;
	transparency.particle_color_host_offset = transparency.vertex_position_host_offset + particle_vertices_size + sprite_vertices_size;
	transparency.sprite_info_host_offset = transparency.particle_color_host_offset + particle_num * TR_COLOR_SIZE;
	transparency.beam_aabb_host_offset = transparency.sprite_info_host_offset + sprite_num * TR_SPRITE_INFO_SIZE;
	transparency.beam_color_host_offset = transparency.beam_aabb_host_offset + beam_num * TR_BEAM_AABB_SIZE;
	transparency.beam_intersect_host_offset = transparency.beam_color_host_offset + beam_num * TR_COLOR_SIZE;
	transparency.current_upload_size = transparency.beam_intersect_host_offset + beam_num * TR_BEAM_INTERSECT_SIZE;

	if (particle_num > 0 || beam_num > 0 || sprite_num > 0)
	{
		write_particle_geometry(view_matrix, particles, particle_num);
		write_beam_geometry(entities, entity_num);
		write_sprite_geometry(view_matrix, entities, entity_num);
		upload_geometry(command_buffer);
	}
}

void vkpt_get_transparency_buffers(
	vkpt_transparency_t ttype,
	BufferResource_t** vertex_buffer,
	uint64_t* vertex_offset,
	BufferResource_t** index_buffer,
	uint64_t* index_offset,
	uint32_t* num_vertices,
	uint32_t* num_indices)
{
	*vertex_buffer = &transparency.vertex_buffer;
	*index_buffer = &transparency.index_buffer;
	*index_offset = 0;

	switch (ttype)
	{
	case VKPT_TRANSPARENCY_PARTICLES:
		*vertex_offset = 0;
		*num_vertices = transparency.particle_num * 4;
		*num_indices = transparency.particle_num * 6;
		return;

	case VKPT_TRANSPARENCY_SPRITES:
		*vertex_offset = transparency.sprite_vertex_device_offset;
		*num_vertices = transparency.sprite_num * 4;
		*num_indices = transparency.sprite_num * 6;
		return;

	default:
		*vertex_offset = transparency.sprite_vertex_device_offset;
		*num_vertices = 0;
		*num_indices = 0;
		return;
	}
}

void vkpt_get_beam_aabb_buffer(
	BufferResource_t** aabb_buffer,
	uint64_t* aabb_offset,
	uint32_t* num_aabbs)
{
	*aabb_buffer = &transparency.beam_aabb_buffer;
	*aabb_offset = 0;
	*num_aabbs = transparency.beam_num;
}

VkBufferView get_transparency_particle_color_buffer_view()
{
	return transparency.particle_color_buffer_view;
}

VkBufferView get_transparency_beam_color_buffer_view()
{
	return transparency.beam_color_buffer_view;
}

VkBufferView get_transparency_sprite_info_buffer_view()
{
	return transparency.sprite_info_buffer_view;
}

VkBufferView get_transparency_beam_intersect_buffer_view()
{
	return transparency.beam_intersect_buffer_view;
}

void get_transparency_counts(int* particle_num, int* beam_num, int* sprite_num)
{
	*particle_num = transparency.particle_num;
	*beam_num = transparency.beam_num;
	*sprite_num = transparency.sprite_num;
}

static void write_particle_geometry(const float* view_matrix, const particle_t* particles, int particle_num)
{
	const float particle_size = cvar_pt_particle_size->value;

	const vec3_t view_y = { view_matrix[1], view_matrix[5], view_matrix[9] };

	// TODO: remove vkpt_refdef.fd, it's better to calculate it from the view matrix
	const vec3_t view_origin = { vkpt_refdef.fd->vieworg[0], vkpt_refdef.fd->vieworg[1], vkpt_refdef.fd->vieworg[2] };

	// TODO: use better alignment?
	vec3_t* vertex_positions = (vec3_t*)(transparency.host_buffer_shadow + transparency.vertex_position_host_offset);
	float* particle_colors = (float*)(transparency.host_buffer_shadow + transparency.particle_color_host_offset);

	for (int i = 0; i < particle_num; i++)
	{
		const particle_t* particle = particles + i;

		cast_u32_to_f32_color(particle->color, &particle->rgba, particle_colors, particle->brightness);
		particle_colors[3] = particle->alpha;
		particle_colors = particle_colors + 4;

		vec3_t origin;
		VectorCopy(particle->origin, origin);

		vec3_t z_axis;
		VectorSubtract(view_origin, origin, z_axis);
		VectorNormalize(z_axis);

		vec3_t x_axis;
		vec3_t y_axis;
		CrossProduct(z_axis, view_y, x_axis);
		CrossProduct(x_axis, z_axis, y_axis);

		const float size_factor = pow(particle->alpha, 0.05f);
		if (particle->radius == 0.f)
		{
			VectorScale(y_axis, particle_size * size_factor, y_axis);
			VectorScale(x_axis, particle_size * size_factor, x_axis);
		}
		else
		{
			VectorScale(y_axis, particle->radius, y_axis);
			VectorScale(x_axis, particle->radius, x_axis);
		}

		vec3_t temp;
		VectorSubtract(origin, x_axis, temp);
		VectorAdd(temp, y_axis, vertex_positions[0]);

		VectorAdd(origin, x_axis, temp);
		VectorAdd(temp, y_axis, vertex_positions[1]);

		VectorAdd(origin, x_axis, temp);
		VectorSubtract(temp, y_axis, vertex_positions[2]);

		VectorSubtract(origin, x_axis, temp);
		VectorSubtract(temp, y_axis, vertex_positions[3]);

		vertex_positions += 4;
	}
}

static void write_beam_geometry(const entity_t* entities, int entity_num)
{
	const float hdr_factor = cvar_pt_particle_emissive->value;

	if (transparency.beam_num == 0)
		return;

	const size_t beam_aabb_offset = transparency.beam_aabb_host_offset;

	// TODO: use better alignment?
	VkAabbPositionsKHR* aabb_positions = (VkAabbPositionsKHR*)(transparency.host_buffer_shadow + beam_aabb_offset);
	uint32_t* beam_infos = (uint32_t*)(transparency.host_buffer_shadow + transparency.beam_intersect_host_offset);
	float* beam_colors = (float*)(transparency.host_buffer_shadow + transparency.beam_color_host_offset);

	for (int i = 0; i < entity_num; i++)
	{
		if ((entities[i].flags & RF_BEAM) == 0)
			continue;

		const entity_t* beam = entities + i;

		// Adjust beam width. Default "narrow" beams have a width of 4, "fat" beams have 16.
		if (beam->frame == 0)
			continue;
		const float beam_radius = cvar_pt_beam_width->value * beam->frame * 0.5;

		cast_u32_to_f32_color(beam->skinnum, &beam->rgba, beam_colors, hdr_factor);
		beam_colors[3] = beam->alpha;
		beam_colors += 4;

		vec3_t begin;
		vec3_t end;
		VectorCopy(beam->oldorigin, begin);
		VectorCopy(beam->origin, end);

		// Compute AABB for beam beam
		vec3_t beam_aabb_min;
		vec3_t beam_aabb_max;
		for (int i = 0; i < 3; i++) {
			if(begin[i] < end[i]) {
				beam_aabb_min[i] = begin[i];
				beam_aabb_max[i] = end[i];
			} else {
				beam_aabb_min[i] = end[i];
				beam_aabb_max[i] = begin[i];
			}
			beam_aabb_min[i] -= beam_radius; // bit of an overestimation for 'tilted' beams
			beam_aabb_max[i] += beam_radius;
		}
		aabb_positions->minX = beam_aabb_min[0];
		aabb_positions->minY = beam_aabb_min[1];
		aabb_positions->minZ = beam_aabb_min[2];
		aabb_positions->maxX = beam_aabb_max[0];
		aabb_positions->maxY = beam_aabb_max[1];
		aabb_positions->maxZ = beam_aabb_max[2];
		++aabb_positions;

		vec3_t to_end;
		VectorSubtract(end, begin, to_end);

		vec3_t norm_dir;
		VectorCopy(to_end, norm_dir);
		VectorNormalize(norm_dir);

		// Compute a transform from a beam at origin, pointing towards +Z, to actual beam origin/direction
		vec3_t beam_coordsys_x, beam_coordsys_y;
		MakeNormalVectors(norm_dir, beam_coordsys_x, beam_coordsys_y);
		mat4_t world_to_beam;

		world_to_beam[0] = beam_coordsys_x[0];
		world_to_beam[1] = beam_coordsys_y[0];
		world_to_beam[2] = norm_dir[0];
		world_to_beam[3] = 0;

		world_to_beam[4] = beam_coordsys_x[1];
		world_to_beam[5] = beam_coordsys_y[1];
		world_to_beam[6] = norm_dir[1];
		world_to_beam[7] = 0;

		world_to_beam[8] = beam_coordsys_x[2];
		world_to_beam[9] = beam_coordsys_y[2];
		world_to_beam[10] = norm_dir[2];
		world_to_beam[11] = 0;

		world_to_beam[12] = -DotProduct(begin, beam_coordsys_x);
		world_to_beam[13] = -DotProduct(begin, beam_coordsys_y);
		world_to_beam[14] = -DotProduct(begin, norm_dir);
		world_to_beam[15] = 1;

		Vector4Copy((world_to_beam + 12), (float *)(beam_infos));
		// First three columns are normals, so it's fine to pack them to half floats
		packHalf4x16(beam_infos + 4, world_to_beam);
		packHalf4x16(beam_infos + 6, world_to_beam + 4);
		packHalf4x16(beam_infos + 8, world_to_beam + 8);
		*(float *)(beam_infos + 10) = beam_radius;
		*(float *)(beam_infos + 11) = VectorLength(to_end);
		beam_infos += TR_BEAM_INTERSECT_SIZE / sizeof(uint32_t);
	}
}

#define MAX_BEAMS 64

static int compare_beams(const void* _a, const void* _b)
{
	const entity_t* a = *(void**)_a;
	const entity_t* b = *(void**)_b;

	if (a->origin[0] < b->origin[0]) return -1;
	if (a->origin[0] > b->origin[0]) return 1;
	if (a->origin[1] < b->origin[1]) return -1;
	if (a->origin[1] > b->origin[1]) return 1;
	if (a->origin[2] < b->origin[2]) return -1;
	if (a->origin[2] > b->origin[2]) return 1;
	if (a->oldorigin[0] < b->oldorigin[0]) return -1;
	if (a->oldorigin[0] > b->oldorigin[0]) return 1;
	if (a->oldorigin[1] < b->oldorigin[1]) return -1;
	if (a->oldorigin[1] > b->oldorigin[1]) return 1;
	if (a->oldorigin[2] < b->oldorigin[2]) return -1;
	if (a->oldorigin[2] > b->oldorigin[2]) return 1;
	return 0;
}

bool vkpt_build_cylinder_light(light_poly_t* light_list, int* num_lights, int max_lights, bsp_t *bsp, vec3_t begin, vec3_t end, vec3_t color, float radius)
{
	vec3_t dir, norm_dir;
	VectorSubtract(end, begin, dir);
	VectorCopy(dir, norm_dir);
	VectorNormalize(norm_dir);

	vec3_t up = { 0.f, 0.f, 1.f };
	vec3_t left = { 1.f, 0.f, 0.f };
	if (fabsf(norm_dir[2]) < 0.9f)
	{
		CrossProduct(up, norm_dir, left);
		VectorNormalize(left);
		CrossProduct(norm_dir, left, up);
		VectorNormalize(up);
	}
	else
	{
		CrossProduct(norm_dir, left, up);
		VectorNormalize(up);
		CrossProduct(up, norm_dir, left);
		VectorNormalize(left);
	}


	vec3_t vertices[6] = {
		{ 0.f, 1.f, 0.f },
		{ 0.866f, -0.5f, 0.f },
		{ -0.866f, -0.5f, 0.f },
		{ 0.f, -1.f, 1.f },
		{ -0.866f, 0.5f, 1.f },
		{ 0.866f, 0.5f, 1.f },
	};

	const int indices[18] = {
		0, 4, 2,
		2, 4, 3,
		2, 3, 1,
		1, 3, 5,
		1, 5, 0,
		0, 5, 4
	};

	for (int vert = 0; vert < 6; vert++)
	{
		vec3_t transformed;
		VectorCopy(begin, transformed);
		VectorMA(transformed, vertices[vert][0] * radius, up, transformed);
		VectorMA(transformed, vertices[vert][1] * radius, left, transformed);
		VectorMA(transformed, vertices[vert][2], dir, transformed);
		VectorCopy(transformed, vertices[vert]);
	}

	for (int tri = 0; tri < 6; tri++)
	{
		if (*num_lights >= max_lights)
			return false;

		int i0 = indices[tri * 3 + 0];
		int i1 = indices[tri * 3 + 1];
		int i2 = indices[tri * 3 + 2];

		light_poly_t* light = light_list + *num_lights;

		VectorCopy(vertices[i0], light->positions + 0);
		VectorCopy(vertices[i1], light->positions + 3);
		VectorCopy(vertices[i2], light->positions + 6);
		get_triangle_off_center(light->positions, light->off_center, NULL, 1.f);

		light->cluster = BSP_PointLeaf(bsp->nodes, light->off_center)->cluster;
		light->material = NULL;
		light->style = 0;

		VectorCopy(color, light->color);

		if (light->cluster >= 0)
		{
			(*num_lights)++;
		}
	}

	return true;
}

void vkpt_build_beam_lights(light_poly_t* light_list, int* num_lights, int max_lights, bsp_t *bsp, entity_t* entities, int num_entites, float adapted_luminance)
{
	const float hdr_factor = cvar_pt_beam_lights->value * adapted_luminance * 20.f;

	if (hdr_factor <= 0.f)
		return;

	int num_beams = 0;

	static entity_t* beams[MAX_BEAMS];

	for (int i = 0; i < num_entites; i++)
	{
		if(num_beams == MAX_BEAMS)
			break;

		if ((entities[i].flags & RF_BEAM) != 0)
			beams[num_beams++] = entities + i;
	}

	if (num_beams == 0)
		return;

	qsort(beams, num_beams, sizeof(entity_t*), compare_beams);

	for (int i = 0; i < num_beams; i++)
	{
		if (*num_lights >= max_lights)
			return;
		
		const entity_t* beam = beams[i];

		// Adjust beam width. Default "narrow" beams have a width of 4, "fat" beams have 16.
		if (beam->frame == 0)
			continue;
		const float beam_radius = cvar_pt_beam_width->value * beam->frame * 0.5;

		vec3_t begin;
		vec3_t end;
		VectorCopy(beam->oldorigin, begin);
		VectorCopy(beam->origin, end);

		vec3_t to_end;
		VectorSubtract(end, begin, to_end);

		vec3_t norm_dir;
		VectorCopy(to_end, norm_dir);
		VectorNormalize(norm_dir);
		VectorMA(begin, -5.f, norm_dir, begin);
		VectorMA(end, 5.f, norm_dir, end);

		vec3_t color;
		cast_u32_to_f32_color(beam->skinnum, &beam->rgba, color, hdr_factor);

		vkpt_build_cylinder_light(light_list, num_lights, max_lights, bsp, begin, end, color, beam_radius);
	}
}

static void write_sprite_geometry(const float* view_matrix, const entity_t* entities, int entity_num)
{
	if (transparency.sprite_num == 0)
		return;

	const vec3_t view_x = { view_matrix[0], view_matrix[4], view_matrix[8] };
	const vec3_t view_y = { view_matrix[1], view_matrix[5], view_matrix[9] };
	const vec3_t world_y = { 0.f, 0.f, 1.f };

	// TODO: remove vkpt_refdef.fd, it's better to calculate it from the view matrix
	const vec3_t view_origin = { vkpt_refdef.fd->vieworg[0], vkpt_refdef.fd->vieworg[1], vkpt_refdef.fd->vieworg[2] };

	const size_t particle_vertex_data_size = transparency.particle_num * 4 * TR_POSITION_SIZE;
	const size_t sprite_vertex_offset = transparency.vertex_position_host_offset + particle_vertex_data_size;

	// TODO: use better alignment?
	vec3_t* vertex_positions = (vec3_t*)(transparency.host_buffer_shadow + sprite_vertex_offset);
	uint32_t* sprite_info = (uint32_t*)(transparency.host_buffer_shadow + transparency.sprite_info_host_offset);

	int sprite_count = 0;
	for (int i = 0; i < entity_num; i++)
	{
		const entity_t *e = entities + i;

		if (e->model & 0x80000000)
			continue;

		const model_t* model = MOD_ForHandle(e->model);
		if (!model || model->type != MOD_SPRITE)
			continue;

		mspriteframe_t *frame = &model->spriteframes[e->frame % model->numframes];
		image_t *image = frame->image;

		sprite_info[0] = image - r_images;
		memcpy(&sprite_info[1], &e->alpha, sizeof(uint32_t));

		// set up the quad - reference code is in function GL_DrawSpriteModel

		vec3_t up, down, left, right;

		if (cvar_pt_projection->integer == 1)
		{
			// make the sprite always face the camera and always vertical in cylindrical projection mode

			vec3_t to_camera;
			VectorSubtract(view_origin, e->origin, to_camera);
			
			vec3_t cyl_x;
			CrossProduct(world_y, to_camera, cyl_x);
			VectorNormalize(cyl_x);

			VectorScale(cyl_x, frame->origin_x, left);
			VectorScale(cyl_x, frame->origin_x - frame->width, right);

			VectorScale(world_y, -frame->origin_y, down);
			VectorScale(world_y, frame->height - frame->origin_y, up);
		}
		else
		{
			VectorScale(view_x, frame->origin_x, left);
			VectorScale(view_x, frame->origin_x - frame->width, right);

			if (model->sprite_vertical)
			{
				VectorScale(world_y, -frame->origin_y, down);
				VectorScale(world_y, frame->height - frame->origin_y, up);
			}
			else
			{
				VectorScale(view_y, -frame->origin_y, down);
				VectorScale(view_y, frame->height - frame->origin_y, up);
			}
		}

		VectorAdd3(e->origin, down, left, vertex_positions[0]);
		VectorAdd3(e->origin, up, left, vertex_positions[1]);
		VectorAdd3(e->origin, up, right, vertex_positions[2]);
		VectorAdd3(e->origin, down, right, vertex_positions[3]);

		vertex_positions += 4;
		sprite_info += TR_SPRITE_INFO_SIZE / sizeof(int);

		if (++sprite_count >= TR_SPRITE_MAX_NUM)
			return;
	}
}

static void upload_geometry(VkCommandBuffer command_buffer)
{
	transparency.sprite_vertex_device_offset = transparency.particle_num * 4 * TR_POSITION_SIZE;

    const size_t host_buffer_offset = transparency.host_frame_index * transparency.host_frame_size;

	assert(transparency.current_upload_size > 0);
	memcpy(transparency.mapped_host_buffer + host_buffer_offset, transparency.host_buffer_shadow, transparency.current_upload_size);
	transparency.current_upload_size = 0;

	const VkBufferCopy vertices = {
		.srcOffset = host_buffer_offset + transparency.vertex_position_host_offset,
		.dstOffset = 0,
		.size = (transparency.particle_num + transparency.sprite_num) * 4 * TR_POSITION_SIZE
	};

	const VkBufferCopy beam_aabbs = {
		.srcOffset = host_buffer_offset + transparency.beam_aabb_host_offset,
		.dstOffset = 0,
		.size = transparency.beam_num * TR_BEAM_AABB_SIZE
	};

	const VkBufferCopy particle_colors = {
		.srcOffset = host_buffer_offset + transparency.particle_color_host_offset,
		.dstOffset = 0,
		.size = transparency.particle_num * TR_COLOR_SIZE
	};

	const VkBufferCopy beam_colors = {
		.srcOffset = host_buffer_offset + transparency.beam_color_host_offset,
		.dstOffset = 0,
		.size = transparency.beam_num * TR_COLOR_SIZE
	};

	const VkBufferCopy sprite_infos = {
		.srcOffset = host_buffer_offset + transparency.sprite_info_host_offset,
		.dstOffset = 0,
		.size = transparency.sprite_num * TR_SPRITE_INFO_SIZE
	};

	const VkBufferCopy beam_intersect = {
		.srcOffset = host_buffer_offset + transparency.beam_intersect_host_offset,
		.dstOffset = 0,
		.size = transparency.beam_num * TR_BEAM_INTERSECT_SIZE
	};

	if (vertices.size)
		vkCmdCopyBuffer(command_buffer, transparency.host_buffer, transparency.vertex_buffer.buffer,
			1, &vertices);
	
	if (beam_aabbs.size)
		vkCmdCopyBuffer(command_buffer, transparency.host_buffer, transparency.beam_aabb_buffer.buffer,
			1, &beam_aabbs);

	if (particle_colors.size)
		vkCmdCopyBuffer(command_buffer, transparency.host_buffer, transparency.particle_color_buffer.buffer,
			1, &particle_colors);

	if (beam_colors.size)
		vkCmdCopyBuffer(command_buffer, transparency.host_buffer, transparency.beam_color_buffer.buffer,
			1, &beam_colors);

	if (sprite_infos.size)
		vkCmdCopyBuffer(command_buffer, transparency.host_buffer, transparency.sprite_info_buffer.buffer,
			1, &sprite_infos);

	if (beam_intersect.size)
		vkCmdCopyBuffer(command_buffer, transparency.host_buffer, transparency.beam_intersect_buffer.buffer,
			1, &beam_intersect);

	for (size_t i = 0; i < LENGTH(transparency.transfer_barriers); i++)
	{
		transparency.transfer_barriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		transparency.transfer_barriers[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		transparency.transfer_barriers[i].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		transparency.transfer_barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		transparency.transfer_barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	}

	transparency.transfer_barriers[0].buffer = transparency.vertex_buffer.buffer;
	transparency.transfer_barriers[0].size = vertices.size;
	transparency.transfer_barriers[1].buffer = transparency.particle_color_buffer.buffer;
	transparency.transfer_barriers[1].size = particle_colors.size;
	transparency.transfer_barriers[2].buffer = transparency.beam_color_buffer.buffer;
	transparency.transfer_barriers[2].size = beam_colors.size;
	transparency.transfer_barriers[3].buffer = transparency.sprite_info_buffer.buffer;
	transparency.transfer_barriers[3].size = sprite_infos.size;
	transparency.transfer_barriers[4].buffer = transparency.beam_aabb_buffer.buffer;
	transparency.transfer_barriers[4].size = beam_aabbs.size;
	transparency.transfer_barriers[5].buffer = transparency.beam_intersect_buffer.buffer;
	transparency.transfer_barriers[5].size = beam_intersect.size;
}


static void create_buffers(void)
{
	const VkBufferCreateInfo host_buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = transparency.host_buffered_frame_num * transparency.host_frame_size,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
	};

	_VK(vkCreateBuffer(qvk.device, &host_buffer_info, NULL, &transparency.host_buffer));

	buffer_create(
		&transparency.vertex_buffer, 
		TR_VERTEX_MAX_NUM * sizeof(vec3_t),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	buffer_create(
		&transparency.beam_aabb_buffer,
		TR_BEAM_MAX_NUM * sizeof(VkAabbPositionsKHR),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, 
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	buffer_create(
		&transparency.index_buffer,
		TR_INDEX_MAX_NUM * sizeof(uint16_t),
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	buffer_create(
		&transparency.particle_color_buffer,
		TR_PARTICLE_MAX_NUM * TR_COLOR_SIZE,
		VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	buffer_create(
		&transparency.beam_color_buffer,
		TR_BEAM_MAX_NUM * TR_COLOR_SIZE,
		VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	buffer_create(
		&transparency.sprite_info_buffer,
		TR_SPRITE_MAX_NUM * TR_SPRITE_INFO_SIZE,
		VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	buffer_create(
		&transparency.beam_intersect_buffer,
		TR_BEAM_MAX_NUM * TR_BEAM_INTERSECT_SIZE,
		VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

static bool allocate_and_bind_memory_to_buffers(void)
{
	VkMemoryRequirements host_buffer_requirements;
	vkGetBufferMemoryRequirements(qvk.device, transparency.host_buffer, &host_buffer_requirements);

	const VkMemoryPropertyFlags host_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	const uint32_t host_memory_type = get_memory_type(host_buffer_requirements.memoryTypeBits, host_flags);

	const VkMemoryAllocateInfo host_memory_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = host_buffer_requirements.size,
		.memoryTypeIndex = host_memory_type
	};

	_VK(vkAllocateMemory(qvk.device, &host_memory_allocate_info, NULL, &transparency.host_buffer_memory));

	VkBindBufferMemoryInfo bindings[1] = { 0 };

	bindings[0].sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
	bindings[0].buffer = transparency.host_buffer;
	bindings[0].memory = transparency.host_buffer_memory;
	bindings[0].memoryOffset = 0;

	_VK(vkBindBufferMemory2(qvk.device, LENGTH(bindings), bindings));

	const size_t host_buffer_size = transparency.host_buffered_frame_num * transparency.host_frame_size;

	_VK(vkMapMemory(qvk.device, transparency.host_buffer_memory, 0, host_buffer_size, 0,
		(void**)&transparency.mapped_host_buffer));

	transparency.host_buffer_shadow = Z_Mallocz(transparency.host_frame_size);
	
	return true;
}

static void create_buffer_views(void)
{
	const VkBufferViewCreateInfo particle_color_view_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
		.buffer = transparency.particle_color_buffer.buffer,
		.format = VK_FORMAT_R32G32B32A32_SFLOAT,
		.range = TR_PARTICLE_MAX_NUM * TR_COLOR_SIZE
	};

	const VkBufferViewCreateInfo beam_color_view_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
		.buffer = transparency.beam_color_buffer.buffer,
		.format = VK_FORMAT_R32G32B32A32_SFLOAT,
		.range = TR_BEAM_MAX_NUM * TR_COLOR_SIZE
	};

	const VkBufferViewCreateInfo sprite_info_view_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
		.buffer = transparency.sprite_info_buffer.buffer,
		.format = VK_FORMAT_R32G32_UINT,
		.range = TR_SPRITE_MAX_NUM * TR_SPRITE_INFO_SIZE
	};

	const VkBufferViewCreateInfo beam_intersect_view_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
		.buffer = transparency.beam_intersect_buffer.buffer,
		.format = VK_FORMAT_R32G32B32A32_UINT,
		.range = TR_BEAM_MAX_NUM * TR_BEAM_INTERSECT_SIZE
	};

	_VK(vkCreateBufferView(qvk.device, &particle_color_view_info, NULL,
		&transparency.particle_color_buffer_view));

	_VK(vkCreateBufferView(qvk.device, &beam_color_view_info, NULL,
		&transparency.beam_color_buffer_view));

	_VK(vkCreateBufferView(qvk.device, &sprite_info_view_info, NULL,
		&transparency.sprite_info_buffer_view));

	_VK(vkCreateBufferView(qvk.device, &beam_intersect_view_info, NULL,
		&transparency.beam_intersect_buffer_view));
}

static void fill_index_buffer(void)
{
	uint16_t* indices = (uint16_t*)transparency.host_buffer_shadow;

	for (size_t i = 0; i < TR_INDEX_MAX_NUM / 6; i++)
	{
		uint16_t* quad = indices + i * 6;

		const uint16_t base_vertex = i * 4;
		quad[0] = base_vertex + 0;
		quad[1] = base_vertex + 1;
		quad[2] = base_vertex + 2;
		quad[3] = base_vertex + 2;
		quad[4] = base_vertex + 3;
		quad[5] = base_vertex + 0;
	}

	memcpy(transparency.mapped_host_buffer, transparency.host_buffer_shadow, sizeof(uint16_t) * TR_INDEX_MAX_NUM);

	VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_transfer);

	const VkBufferMemoryBarrier pre_barrier = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = transparency.index_buffer.buffer,
		.size = VK_WHOLE_SIZE
	};

	vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, NULL, 1, &pre_barrier, 0, NULL);

	const VkBufferCopy region = {
		.size = TR_INDEX_MAX_NUM * sizeof(uint16_t)
	};

	vkCmdCopyBuffer(cmd_buf, transparency.host_buffer, transparency.index_buffer.buffer, 1, &region);

	const VkBufferMemoryBarrier post_barrier = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = transparency.index_buffer.buffer,
		.size = VK_WHOLE_SIZE
	};

	vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, NULL, 1, &post_barrier, 0, NULL);

	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_transfer, true);
	vkpt_wait_idle(qvk.queue_transfer, &qvk.cmd_buffers_transfer);
}
