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

#include "vkpt.h"

#include "shader/vertex_buffer.h"
#include "material.h"

#include <assert.h>
#include "conversion.h"


static VkDescriptorPool desc_pool_vertex_buffer;
static VkPipeline       pipeline_instance_geometry;
static VkPipeline       pipeline_animate_materials;
static VkPipelineLayout pipeline_layout_instance_geometry;

typedef struct {
	BufferResource_t buffer;
	BufferResource_t staging_buffer;
	int registration_sequence;
	model_geometry_t geom_opaque;
	model_geometry_t geom_transparent;
	model_geometry_t geom_masked;
	size_t vertex_data_offset;
	uint32_t total_tris;
	qboolean is_static;
} model_vbo_t;

model_vbo_t model_vertex_data[MAX_MODELS];
static BufferResource_t null_buffer;

// Per Vulkan spec, acceleration structure offset must be a multiple of 256
// https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkAccelerationStructureCreateInfoKHR.html
#define ACCEL_STRUCT_ALIGNMENT 256

void vkpt_append_model_geometry(model_geometry_t* info, uint32_t num_prims, uint32_t prim_offset, const char* model_name)
{
	if (num_prims == 0)
		return;

	if (info->num_geometries >= MAX_MODEL_MESHES)
	{
		Com_WPrintf("Model '%s' exceeds the maximum supported number of meshes (%d)\n", model_name);
		return;
	}

	VkAccelerationStructureGeometryKHR geometry = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
			.geometry = {
				.triangles = {
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
					.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
					.vertexStride = sizeof(vec3),
					.maxVertex = num_prims * 3,
					.indexType = VK_INDEX_TYPE_NONE_KHR
				}
		}
	};

	VkAccelerationStructureBuildRangeInfoKHR build_range = {
		.primitiveCount = num_prims
	};

	uint32_t geom_index = info->num_geometries;

	info->geometries[geom_index] = geometry;
	info->build_ranges[geom_index] = build_range;
	info->prim_counts[geom_index] = num_prims;
	info->prim_offsets[geom_index] = prim_offset;

	++info->num_geometries;
}

static void suballocate_model_blas_memory(model_geometry_t* info, size_t* vbo_size, const char* model_name)
{
	VkAccelerationStructureBuildSizesInfoKHR build_sizes = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
	};

	info->build_sizes = build_sizes;

	if (info->num_geometries == 0)
		return;

	VkAccelerationStructureBuildGeometryInfoKHR blasBuildinfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = info->num_geometries,
		.pGeometries = info->geometries
	};

	qvkGetAccelerationStructureBuildSizesKHR(qvk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&blasBuildinfo, info->prim_counts, &info->build_sizes);

	if (info->build_sizes.buildScratchSize > buf_accel_scratch.size)
	{
		Com_WPrintf("Model '%s' requires %llu bytes scratch buffer to build its BLAS, while only %zu are available.\n",
			model_name, info->build_sizes.buildScratchSize, buf_accel_scratch.size);

		info->num_geometries = 0;
	}
	else
	{
		*vbo_size = align(*vbo_size, ACCEL_STRUCT_ALIGNMENT);

		info->blas_data_offset = *vbo_size;
		*vbo_size += info->build_sizes.accelerationStructureSize;
	}
}

static void create_model_blas(model_geometry_t* info, VkBuffer buffer)
{
	if (info->num_geometries == 0)
		return;

	VkAccelerationStructureCreateInfoKHR blasCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.buffer = buffer,
		.offset = info->blas_data_offset,
		.size = info->build_sizes.accelerationStructureSize,
	};

	_VK(qvkCreateAccelerationStructureKHR(qvk.device, &blasCreateInfo, NULL, &info->accel));
	
	VkAccelerationStructureDeviceAddressInfoKHR  as_device_address_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = info->accel
	};

	info->blas_device_address = qvkGetAccelerationStructureDeviceAddressKHR(qvk.device, &as_device_address_info);
}

static void build_model_blas(VkCommandBuffer cmd_buf, model_geometry_t* info, size_t first_vertex_offset, const BufferResource_t* buffer)
{
	if (!info->accel)
		return;

	assert(buffer->address);

	uint32_t total_prims = 0;

	for (uint32_t index = 0; index < info->num_geometries; index++)
	{
		VkAccelerationStructureGeometryKHR* geometry = info->geometries + index;

		geometry->geometry.triangles.vertexData.deviceAddress = buffer->address + info->prim_offsets[index] * sizeof(mat3) + first_vertex_offset;

		total_prims += info->prim_counts[index];
	}

	VkAccelerationStructureBuildGeometryInfoKHR blasBuildinfo = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = info->num_geometries,
		.pGeometries = info->geometries,
		.dstAccelerationStructure = info->accel,
		.scratchData = {
			.deviceAddress = buf_accel_scratch.address
		}
	};

	const VkAccelerationStructureBuildRangeInfoKHR* pBlasBuildRange = info->build_ranges;

	qvkCmdBuildAccelerationStructuresKHR(cmd_buf, 1, &blasBuildinfo, &pBlasBuildRange);

	VkMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
					   | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
		.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
	};

	vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1,
		&barrier, 0, 0, 0, 0);
}

VkResult
vkpt_vertex_buffer_upload_bsp_mesh(bsp_mesh_t* bsp_mesh)
{
	assert(bsp_mesh);

	vkDeviceWaitIdle(qvk.device);

	size_t vbo_size = bsp_mesh->num_primitives * sizeof(VboPrimitive);
	bsp_mesh->vertex_data_offset = vbo_size;
	vbo_size += bsp_mesh->num_primitives * sizeof(mat3);
	size_t staging_size = vbo_size;

	suballocate_model_blas_memory(&bsp_mesh->geom_opaque,      &vbo_size, "bsp:opaque");
	suballocate_model_blas_memory(&bsp_mesh->geom_transparent, &vbo_size, "bsp:transparent");
	suballocate_model_blas_memory(&bsp_mesh->geom_masked,      &vbo_size, "bsp:masked");
	suballocate_model_blas_memory(&bsp_mesh->geom_sky,         &vbo_size, "bsp:sky");
	suballocate_model_blas_memory(&bsp_mesh->geom_custom_sky,  &vbo_size, "bsp:custom_sky");

	for (int i = 0; i < bsp_mesh->num_models; i++)
	{
		bsp_model_t* model = bsp_mesh->models + i;

		char name[MAX_QPATH];
		Q_snprintf(name, sizeof(name), "bsp:models[%d]", i);

		suballocate_model_blas_memory(&model->geometry, &vbo_size, name);
	}
	
	VkResult res = buffer_create(&qvk.buf_world, vbo_size,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	if (res != VK_SUCCESS) return res;

	BufferResource_t staging_buffer;

	res = buffer_create(&staging_buffer, staging_size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	if (res != VK_SUCCESS) return res;

	create_model_blas(&bsp_mesh->geom_opaque,      qvk.buf_world.buffer);
	create_model_blas(&bsp_mesh->geom_transparent, qvk.buf_world.buffer);
	create_model_blas(&bsp_mesh->geom_masked,      qvk.buf_world.buffer);
	create_model_blas(&bsp_mesh->geom_sky,         qvk.buf_world.buffer);
	create_model_blas(&bsp_mesh->geom_custom_sky,  qvk.buf_world.buffer);

	for (int i = 0; i < bsp_mesh->num_models; i++)
	{
		bsp_model_t* model = bsp_mesh->models + i;
		create_model_blas(&model->geometry, qvk.buf_world.buffer);
	}

	uint8_t* staging_data = buffer_map(&staging_buffer);
	memcpy(staging_data, bsp_mesh->primitives, bsp_mesh->num_primitives * sizeof(VboPrimitive));

	mat3* positions = (mat3*)(staging_data + bsp_mesh->vertex_data_offset);  // NOLINT(clang-diagnostic-cast-align)
	for (uint32_t prim = 0; prim < bsp_mesh->num_primitives; ++prim)
	{
		VectorCopy(bsp_mesh->primitives[prim].pos0, positions[prim][0]);
		VectorCopy(bsp_mesh->primitives[prim].pos1, positions[prim][1]);
		VectorCopy(bsp_mesh->primitives[prim].pos2, positions[prim][2]);
	}

	buffer_unmap(&staging_buffer);
	
	VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

	VkBufferCopy copyRegion = {
		.size = staging_buffer.size,
	};
	vkCmdCopyBuffer(cmd_buf, staging_buffer.buffer, qvk.buf_world.buffer, 1, &copyRegion);
	
	BUFFER_BARRIER(cmd_buf,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR,
		.buffer = qvk.buf_world.buffer,
		.offset = 0,
		.size = VK_WHOLE_SIZE,
	);

	build_model_blas(cmd_buf, &bsp_mesh->geom_opaque,      bsp_mesh->vertex_data_offset, &qvk.buf_world);
	build_model_blas(cmd_buf, &bsp_mesh->geom_transparent, bsp_mesh->vertex_data_offset, &qvk.buf_world);
	build_model_blas(cmd_buf, &bsp_mesh->geom_masked,      bsp_mesh->vertex_data_offset, &qvk.buf_world);
	build_model_blas(cmd_buf, &bsp_mesh->geom_sky,         bsp_mesh->vertex_data_offset, &qvk.buf_world);
	build_model_blas(cmd_buf, &bsp_mesh->geom_custom_sky,  bsp_mesh->vertex_data_offset, &qvk.buf_world);

	bsp_mesh->geom_opaque.instance_mask = AS_FLAG_OPAQUE;
	bsp_mesh->geom_opaque.instance_flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
	bsp_mesh->geom_opaque.sbt_offset = SBTO_OPAQUE;

	bsp_mesh->geom_transparent.instance_mask = AS_FLAG_TRANSPARENT;
	bsp_mesh->geom_transparent.instance_flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
	bsp_mesh->geom_transparent.sbt_offset = SBTO_OPAQUE;

	bsp_mesh->geom_masked.instance_mask = AS_FLAG_OPAQUE;
	bsp_mesh->geom_masked.instance_flags = VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR | VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	bsp_mesh->geom_masked.sbt_offset = SBTO_MASKED;

	bsp_mesh->geom_sky.instance_mask = AS_FLAG_SKY;
	bsp_mesh->geom_sky.instance_flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
	bsp_mesh->geom_sky.sbt_offset = SBTO_OPAQUE;

	bsp_mesh->geom_custom_sky.instance_mask = AS_FLAG_CUSTOM_SKY;
	bsp_mesh->geom_custom_sky.instance_flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
	bsp_mesh->geom_custom_sky.sbt_offset = SBTO_OPAQUE;

	for (int i = 0; i < bsp_mesh->num_models; i++)
	{
		bsp_model_t* model = bsp_mesh->models + i;
		build_model_blas(cmd_buf, &model->geometry, bsp_mesh->vertex_data_offset, &qvk.buf_world);

		model->geometry.instance_mask = model->transparent ? bsp_mesh->geom_transparent.instance_mask : bsp_mesh->geom_opaque.instance_mask;
		model->geometry.instance_flags = model->masked ? bsp_mesh->geom_masked.instance_flags : bsp_mesh->geom_opaque.instance_flags;
		model->geometry.sbt_offset = model->masked ? bsp_mesh->geom_masked.sbt_offset : bsp_mesh->geom_opaque.sbt_offset;
	}

	if (qvk.buf_light_stats[0].buffer)
	{
		vkCmdFillBuffer(cmd_buf, qvk.buf_light_stats[0].buffer, 0, qvk.buf_light_stats[0].size, 0);
		vkCmdFillBuffer(cmd_buf, qvk.buf_light_stats[1].buffer, 0, qvk.buf_light_stats[1].size, 0);
		vkCmdFillBuffer(cmd_buf, qvk.buf_light_stats[2].buffer, 0, qvk.buf_light_stats[2].size, 0);
	}

	vkpt_submit_command_buffer(cmd_buf, qvk.queue_graphics, (1 << qvk.device_count) - 1, 0, NULL, NULL, NULL, 0, NULL, NULL, NULL);

	vkDeviceWaitIdle(qvk.device);

	buffer_destroy(&staging_buffer);


	VkDescriptorBufferInfo buf_info = {
		.buffer = qvk.buf_world.buffer,
		.offset = 0,
		.range = bsp_mesh->num_primitives * sizeof(VboPrimitive),
	};

	VkWriteDescriptorSet output_buf_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = qvk.desc_set_vertex_buffer,
		.dstBinding = PRIMITIVE_BUFFER_BINDING_IDX,
		.dstArrayElement = VERTEX_BUFFER_WORLD,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.pBufferInfo = &buf_info,
	};

	vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);


	return VK_SUCCESS;
}

static void destroy_accel(VkAccelerationStructureKHR* accel)
{
	if (*accel)
	{
		qvkDestroyAccelerationStructureKHR(qvk.device, *accel, NULL);
		*accel = NULL;
	}
}

void vkpt_vertex_buffer_cleanup_bsp_mesh(bsp_mesh_t* bsp_mesh)
{
	destroy_accel(&bsp_mesh->geom_opaque.accel);
	destroy_accel(&bsp_mesh->geom_transparent.accel);
	destroy_accel(&bsp_mesh->geom_masked.accel);
	destroy_accel(&bsp_mesh->geom_sky.accel);
	destroy_accel(&bsp_mesh->geom_custom_sky.accel);
	
	for (int i = 0; i < bsp_mesh->num_models; i++)
	{
		bsp_model_t* model = bsp_mesh->models + i;

		destroy_accel(&model->geometry.accel);
	}
}

VkResult
vkpt_light_buffer_upload_staging(VkCommandBuffer cmd_buf)
{
	BufferResource_t* staging = qvk.buf_light_staging + qvk.current_frame_index;

	assert(!staging->is_mapped);

	VkBufferCopy copyRegion = {
		.size = sizeof(LightBuffer),
	};
	vkCmdCopyBuffer(cmd_buf, staging->buffer, qvk.buf_light.buffer, 1, &copyRegion);

	int buffer_idx = qvk.frame_counter % 3;
	if (qvk.buf_light_stats[buffer_idx].buffer)
	{
		vkCmdFillBuffer(cmd_buf, qvk.buf_light_stats[buffer_idx].buffer, 0, qvk.buf_light_stats[buffer_idx].size, 0);
	}

	return VK_SUCCESS;
}

VkResult
vkpt_iqm_matrix_buffer_upload_staging(VkCommandBuffer cmd_buf)
{
	BufferResource_t* staging = qvk.buf_iqm_matrices_staging + qvk.current_frame_index;

	assert(!staging->is_mapped);

	VkBufferCopy copyRegion = {
		.size = sizeof(IqmMatrixBuffer),
	};
	vkCmdCopyBuffer(cmd_buf, staging->buffer, qvk.buf_iqm_matrices.buffer, 1, &copyRegion);
	
	return VK_SUCCESS;
}

static int local_light_counts[MAX_MAP_LEAFS];
static int cluster_light_counts[MAX_MAP_LEAFS];
static int light_list_tails[MAX_MAP_LEAFS];
static int max_cluster_model_lights[MAX_MAP_LEAFS];
static int max_model_lights;

void vkpt_light_buffer_reset_counts()
{
	memset(max_cluster_model_lights, 0, sizeof(max_cluster_model_lights));
	max_model_lights = 0;
}

void
inject_model_lights(bsp_mesh_t* bsp_mesh, bsp_t* bsp, int num_model_lights, light_poly_t* transformed_model_lights, int model_light_offset, uint32_t* dst_list_offsets, uint32_t* dst_lists)
{
	memset(local_light_counts, 0, bsp_mesh->num_clusters * sizeof(int));
	memset(cluster_light_counts, 0, bsp_mesh->num_clusters * sizeof(int));

	// Count the number of model lights per cluster

	for (int nlight = 0; nlight < num_model_lights; nlight++)
	{
		local_light_counts[transformed_model_lights[nlight].cluster]++;
	}

	// Count the number of model lights visible from each cluster, using the PVS

	for (int c = 0; c < bsp_mesh->num_clusters; c++)
	{
		if (local_light_counts[c])
		{
			const byte* mask = BSP_GetPvs(bsp, c);

			for (int j = 0; j < bsp->visrowsize; j++) {
				if (mask[j]) {
					for (int k = 0; k < 8; ++k) {
						if (mask[j] & (1 << k))
							cluster_light_counts[j * 8 + k] += local_light_counts[c];
					}
				}
			}
		}
	}

	// Update the max light counts per cluster

	for (int c = 0; c < bsp_mesh->num_clusters; c++)
	{
		max_cluster_model_lights[c] = max(max_cluster_model_lights[c], cluster_light_counts[c]);
	}

	// Count the total required list size

	int required_size = bsp_mesh->cluster_light_offsets[bsp_mesh->num_clusters];
	for (int c = 0; c < bsp_mesh->num_clusters; c++)
	{
		required_size += max_cluster_model_lights[c];
	}

	// See if we have enough room in the interaction buffer
	
	if (required_size > MAX_LIGHT_LIST_NODES)
	{
		Com_WPrintf("Insufficient light interaction buffer size (%d needed). Increase MAX_LIGHT_LIST_NODES.\n", required_size);

		// Copy the BSP light lists verbatim
		memcpy(dst_lists, bsp_mesh->cluster_lights, sizeof(uint32_t) * bsp_mesh->cluster_light_offsets[bsp_mesh->num_clusters]);
		memcpy(dst_list_offsets, bsp_mesh->cluster_light_offsets, sizeof(uint32_t) * (bsp_mesh->num_clusters + 1));
		
		return;
	}
	
	// Copy the static light lists, and make room in these lists to inject the model lights

	int tail = 0;
	for (int c = 0; c < bsp_mesh->num_clusters; c++)
	{
		int original_size = bsp_mesh->cluster_light_offsets[c + 1] - bsp_mesh->cluster_light_offsets[c];

		dst_list_offsets[c] = tail;
		memcpy(dst_lists + tail, bsp_mesh->cluster_lights + bsp_mesh->cluster_light_offsets[c], sizeof(uint32_t) * original_size);
		tail += original_size;
		
		assert(tail + max_cluster_model_lights[c] < MAX_LIGHT_LIST_NODES);
		
		if (max_cluster_model_lights[c] > 0) {
			memset(dst_lists + tail, 0xff, sizeof(uint32_t) * max_cluster_model_lights[c]);
		}
		light_list_tails[c] = tail;
		tail += max_cluster_model_lights[c];
	}
	dst_list_offsets[bsp_mesh->num_clusters] = tail;

	// Write the model light indices into the light lists

	for (int nlight = 0; nlight < num_model_lights; nlight++)
	{
		const byte* mask = BSP_GetPvs(bsp, transformed_model_lights[nlight].cluster);

		for (int j = 0; j < bsp->visrowsize; j++) {
			if (mask[j]) {
				for (int k = 0; k < 8; ++k) {
					if (mask[j] & (1 << k))
					{
						int other_cluster = j * 8 + k;
						dst_lists[light_list_tails[other_cluster]++] = model_light_offset + nlight;
					}
				}
			}
		}
	}
}

static inline void
copy_light(const light_poly_t* light, float* vblight, const float* sky_radiance)
{
	float style_scale = 1.f;
	float prev_style = 1.f;
	if (light->style != 0 && vkpt_refdef.fd->lightstyles)
	{
		style_scale = vkpt_refdef.fd->lightstyles[light->style].white;
		style_scale = max(0.f, min(2.f, style_scale));

		prev_style = vkpt_refdef.prev_lightstyles[light->style].white;
		prev_style = max(0.f, min(2.f, prev_style));
	}

	float mat_scale = light->material ? light->material->emissive_factor : 1.f;

	VectorCopy(light->positions + 0, vblight + 0);
	VectorCopy(light->positions + 3, vblight + 4);
	VectorCopy(light->positions + 6, vblight + 8);

	if (light->color[0] < 0.f)
	{
		vblight[3] = -sky_radiance[0] * 0.5f;
		vblight[7] = -sky_radiance[1] * 0.5f;
		vblight[11] = -sky_radiance[2] * 0.5f;
	}
	else
	{
		vblight[3] = light->color[0] * mat_scale;
		vblight[7] = light->color[1] * mat_scale;
		vblight[11] = light->color[2] * mat_scale;
	}

	vblight[12] = style_scale;
	vblight[13] = prev_style;
	vblight[14] = 0.f;
	vblight[15] = 0.f;
}

extern char cluster_debug_mask[VIS_MAX_BYTES];

VkResult
vkpt_light_buffer_upload_to_staging(qboolean render_world, bsp_mesh_t *bsp_mesh, bsp_t* bsp, int num_model_lights, light_poly_t* transformed_model_lights, const float* sky_radiance)
{
	assert(bsp_mesh);

	BufferResource_t* staging = qvk.buf_light_staging + qvk.current_frame_index;

	LightBuffer *lbo = (LightBuffer *)buffer_map(staging);
	assert(lbo);

	if (render_world)
	{
		assert(bsp_mesh->num_clusters + 1 < MAX_LIGHT_LISTS);
		assert(bsp_mesh->num_cluster_lights < MAX_LIGHT_LIST_NODES);
		assert(bsp_mesh->num_light_polys + num_model_lights < MAX_LIGHT_POLYS);

		int model_light_offset = bsp_mesh->num_light_polys;
		max_model_lights = max(max_model_lights, num_model_lights);

		if(max_model_lights > 0)
		{
			// If any of the BSP models contain lights, inject these lights right into the visibility lists.
			// The shader doesn't know that these lights are dynamic.

			inject_model_lights(bsp_mesh, bsp, num_model_lights, transformed_model_lights, model_light_offset, lbo->light_list_offsets, lbo->light_list_lights);
		}
		else
		{
			memcpy(lbo->light_list_offsets, bsp_mesh->cluster_light_offsets, (bsp_mesh->num_clusters + 1) * sizeof(uint32_t));
			memcpy(lbo->light_list_lights, bsp_mesh->cluster_lights, bsp_mesh->num_cluster_lights * sizeof(uint32_t));
		}

		for (int nlight = 0; nlight < bsp_mesh->num_light_polys; nlight++)
		{
			light_poly_t* light = bsp_mesh->light_polys + nlight;
			float* vblight = lbo->light_polys + nlight * (LIGHT_POLY_VEC4S * 4);
			copy_light(light, vblight, sky_radiance);
		}

		for (int nlight = 0; nlight < num_model_lights; nlight++)
		{
			light_poly_t* light = transformed_model_lights + nlight;
			float* vblight = lbo->light_polys + (nlight + model_light_offset) * (LIGHT_POLY_VEC4S * 4);
			copy_light(light, vblight, sky_radiance);
		}
	}
	else
	{
		lbo->light_list_offsets[0] = 0;
		lbo->light_list_offsets[1] = 0;
	}

	/* effects.c declares this - hence the assert below:
		typedef struct clightstyle_s {
			...
			float   map[MAX_QPATH];
		} clightstyle_t;
	*/

	assert(MAX_LIGHT_STYLES == MAX_QPATH);
	for (int nstyle = 0; nstyle < MAX_LIGHT_STYLES; nstyle++)
	{
		float style_scale = 1.f;
		if (vkpt_refdef.fd->lightstyles)
		{
			style_scale = vkpt_refdef.fd->lightstyles[nstyle].white;
			style_scale = max(0, min(1, style_scale));
		}
		lbo->light_styles[nstyle] = style_scale;
	}

	// materials
	
	for (int nmat = 0; nmat < MAX_PBR_MATERIALS; nmat++)
	{
		pbr_material_t const * material = r_materials + nmat;
		
		uint32_t* mat_data = lbo->material_table + nmat * MATERIAL_UINTS;
		memset(mat_data, 0, sizeof(uint32_t) * MATERIAL_UINTS);

		if (material->registration_sequence == 0)
			continue;

		if (material->image_base) mat_data[0] |= (material->image_base - r_images);
		if (material->image_normals) mat_data[0] |= (material->image_normals - r_images) << 16;
		if (material->image_emissive) mat_data[1] |= (material->image_emissive - r_images);
		if (material->image_mask) mat_data[1] |= (material->image_mask - r_images) << 16;
		
		mat_data[2] = floatToHalf(material->bump_scale);
		mat_data[2] |= floatToHalf(material->roughness_override) << 16;
		mat_data[3] = floatToHalf(material->metalness_factor);
		mat_data[3] |= floatToHalf(material->emissive_factor) << 16;
		mat_data[4] |= (material->num_frames & 0xffff);
		mat_data[4] |= (material->next_frame & 0xffff) << 16;
		mat_data[5] = floatToHalf(material->specular_factor);
		mat_data[5] |= floatToHalf(material->base_factor) << 16;
	}

	memcpy(lbo->cluster_debug_mask, cluster_debug_mask, MAX_LIGHT_LISTS / 8);
	memcpy(lbo->sky_visibility, bsp_mesh->sky_visibility, MAX_LIGHT_LISTS / 8);

	buffer_unmap(staging);
	lbo = NULL;

	return VK_SUCCESS;
}

static void write_model_vbo_descriptor(int index, VkBuffer buffer, VkDeviceSize size)
{
	VkDescriptorBufferInfo descriptor_buffer_info = {
		.buffer = buffer,
		.offset = 0,
		.range = size,
	};

	VkWriteDescriptorSet write_descriptor_set = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = qvk.desc_set_vertex_buffer,
		.dstBinding = 0,
		.dstArrayElement = VERTEX_BUFFER_FIRST_MODEL + index,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.pBufferInfo = &descriptor_buffer_info,
	};

	vkUpdateDescriptorSets(qvk.device, 1, &write_descriptor_set, 0, NULL);
}

static void destroy_model_vbo(model_vbo_t* vbo)
{
	buffer_destroy(&vbo->buffer);

	if (vbo->geom_opaque.accel)
	{
		qvkDestroyAccelerationStructureKHR(qvk.device, vbo->geom_opaque.accel, NULL);
	}

	if (vbo->geom_transparent.accel)
	{
		qvkDestroyAccelerationStructureKHR(qvk.device, vbo->geom_transparent.accel, NULL);
	}

	if (vbo->geom_masked.accel)
	{
		qvkDestroyAccelerationStructureKHR(qvk.device, vbo->geom_masked.accel, NULL);
	}

	memset(vbo, 0, sizeof(model_vbo_t));
}

static void
stage_mesh_primitives(uint8_t* staging_data, int* p_write_ptr, float** p_vertex_write_ptr, const model_t* model, const maliasmesh_t* m)
{
	int write_ptr = *p_write_ptr;
	float* vertex_write_ptr = *p_vertex_write_ptr;

	for (int frame = 0; frame < model->numframes; frame++)
	{
		for (int tri = 0; tri < m->numtris; tri++)
		{
			VboPrimitive* dst = (VboPrimitive*)staging_data + write_ptr;

			int i0 = m->indices[tri * 3 + 0];
			int i1 = m->indices[tri * 3 + 1];
			int i2 = m->indices[tri * 3 + 2];

			i0 += frame * m->numverts;
			i1 += frame * m->numverts;
			i2 += frame * m->numverts;

			VectorCopy(m->positions[i0], dst->pos0);
			VectorCopy(m->positions[i1], dst->pos1);
			VectorCopy(m->positions[i2], dst->pos2);

			if (vertex_write_ptr)
			{
				VectorCopy(m->positions[i0], vertex_write_ptr); vertex_write_ptr += 3;
				VectorCopy(m->positions[i1], vertex_write_ptr); vertex_write_ptr += 3;
				VectorCopy(m->positions[i2], vertex_write_ptr); vertex_write_ptr += 3;
			}

			dst->normals[0] = encode_normal(m->normals[i0]);
			dst->normals[1] = encode_normal(m->normals[i1]);
			dst->normals[2] = encode_normal(m->normals[i2]);

			if (m->tangents)
			{
				dst->tangents[0] = encode_normal(m->tangents[i0]);
				dst->tangents[1] = encode_normal(m->tangents[i1]);
				dst->tangents[2] = encode_normal(m->tangents[i2]);
			}

			dst->uv0[0] = m->tex_coords[i0][0];
			dst->uv0[1] = m->tex_coords[i0][1];
			dst->uv1[0] = m->tex_coords[i1][0];
			dst->uv1[1] = m->tex_coords[i1][1];
			dst->uv2[0] = m->tex_coords[i2][0];
			dst->uv2[1] = m->tex_coords[i2][1];

			if (m->blend_indices && m->blend_weights)
			{
				dst->motion0[0] = m->blend_indices[i0];
				dst->motion0[1] = m->blend_indices[i1];
				dst->pad = m->blend_indices[i2];

				dst->motion12[0] = m->blend_weights[i0];
				dst->motion12[1] = m->blend_weights[i1];
				dst->motion12[2] = m->blend_weights[i2];
			}

			dst->cluster = -1;

			++write_ptr;
		}
	}
	
	*p_write_ptr = write_ptr;
	*p_vertex_write_ptr = vertex_write_ptr;

#if 0
	for (int j = 0; j < num_verts; j++)
		Com_Printf("%f %f %f\n",
			m->positions[j][0],
			m->positions[j][1],
			m->positions[j][2]);

	for (int j = 0; j < m->numtris; j++)
		Com_Printf("%d %d %d\n",
			m->indices[j * 3 + 0],
			m->indices[j * 3 + 1],
			m->indices[j * 3 + 2]);
#endif

#if 0
	char buf[1024];
	snprintf(buf, sizeof buf, "model_%04d.obj", i);
	FILE* f = fopen(buf, "wb+");
	assert(f);
	for (int j = 0; j < m->numverts; j++) {
		fprintf(f, "v %f %f %f\n",
			m->positions[j][0],
			m->positions[j][1],
			m->positions[j][2]);
	}
	for (int j = 0; j < m->numindices / 3; j++) {
		fprintf(f, "f %d %d %d\n",
			m->indices[j * 3 + 0] + 1,
			m->indices[j * 3 + 1] + 1,
			m->indices[j * 3 + 2] + 1);
	}
	fclose(f);
#endif
}

VkResult
vkpt_vertex_buffer_upload_models()
{
	qboolean any_models_to_upload = qfalse;

	for(int i = 0; i < MAX_MODELS; i++)
	{
		const model_t* model = &r_models[i];
		model_vbo_t* vbo = model_vertex_data + i;

		if (!model->meshes && vbo->buffer.buffer) {
			// model unloaded, destroy the VBO
			write_model_vbo_descriptor(i, null_buffer.buffer, null_buffer.size);
			destroy_model_vbo(vbo);
			//Com_Printf("Unloaded model[%d]\n", i);
			continue;
		}

		if(!model->meshes) {
			// model does not exist
			continue;
		}

		if (model->registration_sequence <= vbo->registration_sequence) {
			// VBO is valid, nothing to do
			continue;
		}

		//Com_Printf("Loading model[%d] %s\n", i, model->name);
		memset(vbo, 0, sizeof(model_vbo_t));

        assert(model->numframes > 0);

		qboolean model_is_static = model->numframes == 1 && (!model->iqmData || !model->iqmData->blend_indices);
		vbo->is_static = model_is_static;
		
		vbo->total_tris = 0;
		for (int nmesh = 0; nmesh < model->nummeshes; nmesh++)
		{
			maliasmesh_t *m = model->meshes + nmesh;

			if (model_is_static)
			{
				if (MAT_IsTransparent(m->materials[0]->flags))
				{
					vkpt_append_model_geometry(&vbo->geom_transparent, m->numtris, vbo->total_tris, model->name);
				}
				else if (MAT_IsMasked(m->materials[0]->flags))
				{
					vkpt_append_model_geometry(&vbo->geom_masked, m->numtris, vbo->total_tris, model->name);
				}
				else
				{
					vkpt_append_model_geometry(&vbo->geom_opaque, m->numtris, vbo->total_tris, model->name);
				}
			}

			vbo->total_tris += m->numtris * model->numframes;
		}
		
		
		vbo->vertex_data_offset = 0;

		size_t vbo_size = vbo->total_tris * sizeof(VboPrimitive);
		size_t staging_size = vbo_size;
		
		if (model_is_static)
		{
			vbo->vertex_data_offset = vbo_size;
			vbo_size += vbo->total_tris * sizeof(vec3) * 3;
			staging_size = vbo_size;

			suballocate_model_blas_memory(&vbo->geom_opaque, &vbo_size, model->name);
			suballocate_model_blas_memory(&vbo->geom_masked, &vbo_size, model->name);
			suballocate_model_blas_memory(&vbo->geom_transparent, &vbo_size, model->name);
		}

		const VkBufferUsageFlags accel_usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

		buffer_create(&vbo->buffer, vbo_size, 
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | (model_is_static ? accel_usage : 0),
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		
		buffer_create(&vbo->staging_buffer, staging_size,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		if (model_is_static)
		{
			create_model_blas(&vbo->geom_opaque, vbo->buffer.buffer);
			create_model_blas(&vbo->geom_masked, vbo->buffer.buffer);
			create_model_blas(&vbo->geom_transparent, vbo->buffer.buffer);
		}

		uint8_t* staging_data = buffer_map(&vbo->staging_buffer);
		memset(staging_data, 0, vbo->staging_buffer.size);
		int write_ptr = 0;
		float* vertex_write_ptr = model_is_static ? (float*)(staging_data + vbo->vertex_data_offset) : NULL;
		
		for (int nmesh = 0; nmesh < model->nummeshes; nmesh++)
		{
			maliasmesh_t* m = model->meshes + nmesh;
			
			m->tri_offset = write_ptr;

			stage_mesh_primitives(staging_data, &write_ptr, &vertex_write_ptr, model, m);
		}

		buffer_unmap(&vbo->staging_buffer);

		vbo->registration_sequence = model->registration_sequence;
		any_models_to_upload = qtrue;
	}

	if (any_models_to_upload)
	{
		VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

		for (int i = 0; i < MAX_MODELS; i++)
		{
			model_vbo_t* vbo = model_vertex_data + i;

			if (vbo->staging_buffer.buffer)
			{
				VkBufferCopy copyRegion = {
					.size = vbo->staging_buffer.size
				};

				vkCmdCopyBuffer(cmd_buf, vbo->staging_buffer.buffer, vbo->buffer.buffer, 1, &copyRegion);

				if (vbo->is_static)
				{
					BUFFER_BARRIER(cmd_buf,
						.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
						.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
						.buffer = vbo->buffer.buffer,
						.offset = 0,
						.size = VK_WHOLE_SIZE);
				}

				build_model_blas(cmd_buf, &vbo->geom_opaque, vbo->vertex_data_offset, &vbo->buffer);
				build_model_blas(cmd_buf, &vbo->geom_transparent, vbo->vertex_data_offset, &vbo->buffer);
				build_model_blas(cmd_buf, &vbo->geom_masked, vbo->vertex_data_offset, &vbo->buffer);

				vbo->geom_opaque.instance_mask = AS_FLAG_OPAQUE;
				vbo->geom_opaque.instance_flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
				vbo->geom_opaque.sbt_offset = SBTO_OPAQUE;

				vbo->geom_transparent.instance_mask = AS_FLAG_TRANSPARENT;
				vbo->geom_transparent.instance_flags = VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
				vbo->geom_transparent.sbt_offset = SBTO_OPAQUE;

				vbo->geom_masked.instance_mask = AS_FLAG_OPAQUE;
				vbo->geom_masked.instance_flags = VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR | VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
				vbo->geom_masked.sbt_offset = SBTO_MASKED;

			}
		}

		vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, qtrue);
		vkQueueWaitIdle(qvk.queue_graphics);

		for (int i = 0; i < MAX_MODELS; i++)
		{
			model_vbo_t* vbo = model_vertex_data + i;

			if (vbo->staging_buffer.buffer)
			{
				buffer_destroy(&vbo->staging_buffer);

				// Just uploaded - create the descriptor, but after vkQueueWaitIdle:
				// otherwise, the descriptor set might be still in use by in-flight shaders.
				write_model_vbo_descriptor(i, vbo->buffer.buffer, vbo->buffer.size);
			}
		}
	}

	return VK_SUCCESS;
}

VkResult
vkpt_vertex_buffer_create()
{
	VkDescriptorSetLayoutBinding vbo_layout_bindings[] = {
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = VERTEX_BUFFER_FIRST_MODEL + MAX_MODELS,
			.binding = PRIMITIVE_BUFFER_BINDING_IDX,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.binding = POSITION_BUFFER_BINIDNG_IDX,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.binding = LIGHT_BUFFER_BINDING_IDX,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.binding = IQM_MATRIX_BUFFER_BINDING_IDX,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.binding = READBACK_BUFFER_BINDING_IDX,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.binding = TONE_MAPPING_BUFFER_BINDING_IDX,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.binding = SUN_COLOR_BUFFER_BINDING_IDX,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.binding = SUN_COLOR_UBO_BINDING_IDX,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 3,
			.binding = LIGHT_STATS_BUFFER_BINDING_IDX,
			.stageFlags = VK_SHADER_STAGE_ALL,
		}
	};

	VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = LENGTH(vbo_layout_bindings),
		.pBindings    = vbo_layout_bindings,
	};

	_VK(vkCreateDescriptorSetLayout(qvk.device, &layout_info, NULL, &qvk.desc_set_layout_vertex_buffer));
	
	buffer_create(&qvk.buf_primitive_instanced, sizeof(VboPrimitive) * MAX_PRIM_INSTANCED,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	buffer_create(&qvk.buf_positions_instanced, sizeof(mat3) * MAX_PRIM_INSTANCED,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	buffer_create(&qvk.buf_light, sizeof(LightBuffer),
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	for (int frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++)
	{
		buffer_create(qvk.buf_light_staging + frame, sizeof(LightBuffer),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}

	buffer_create(&qvk.buf_readback, sizeof(ReadbackBuffer),
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	buffer_create(&qvk.buf_iqm_matrices, sizeof(IqmMatrixBuffer),
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	for (int frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++)
	{
		buffer_create(qvk.buf_iqm_matrices_staging + frame, sizeof(IqmMatrixBuffer),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}

	qvk.iqm_matrices_shadow = Z_Mallocz(sizeof(IqmMatrixBuffer));
	qvk.iqm_matrices_prev = Z_Mallocz(sizeof(IqmMatrixBuffer));

	buffer_create(&qvk.buf_tonemap, sizeof(ToneMappingBuffer),
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	buffer_create(&qvk.buf_sun_color, sizeof(SunColorBuffer),
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	for (int frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++)
	{
		buffer_create(qvk.buf_readback_staging + frame, sizeof(ReadbackBuffer),
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}

	buffer_create(&null_buffer, 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkDescriptorPoolSize pool_size = {
		.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = LENGTH(vbo_layout_bindings) + MAX_MODELS + 128,
	};

	VkDescriptorPoolCreateInfo pool_info = {
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = 1,
		.pPoolSizes    = &pool_size,
		.maxSets       = 2,
	};

	_VK(vkCreateDescriptorPool(qvk.device, &pool_info, NULL, &desc_pool_vertex_buffer));

	VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {
		.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool     = desc_pool_vertex_buffer,
		.descriptorSetCount = 1,
		.pSetLayouts        = &qvk.desc_set_layout_vertex_buffer,
	};

	_VK(vkAllocateDescriptorSets(qvk.device, &descriptor_set_alloc_info, &qvk.desc_set_vertex_buffer));

	VkDescriptorBufferInfo buf_info = {
		.buffer = null_buffer.buffer,
		.offset = 0,
		.range  = null_buffer.size,
	};

	VkWriteDescriptorSet output_buf_write = {
		.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet          = qvk.desc_set_vertex_buffer,
		.dstBinding      = PRIMITIVE_BUFFER_BINDING_IDX,
		.dstArrayElement = VERTEX_BUFFER_WORLD,
		.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = 1,
		.pBufferInfo     = &buf_info,
	};

	vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);

	output_buf_write.dstBinding = PRIMITIVE_BUFFER_BINDING_IDX;
	output_buf_write.dstArrayElement = VERTEX_BUFFER_INSTANCED;
	buf_info.buffer = qvk.buf_primitive_instanced.buffer;
	buf_info.range = qvk.buf_primitive_instanced.size;
	vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);

	output_buf_write.dstBinding = POSITION_BUFFER_BINIDNG_IDX;
	output_buf_write.dstArrayElement = 0;
	buf_info.buffer = qvk.buf_positions_instanced.buffer;
	buf_info.range = qvk.buf_positions_instanced.size;
	vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);

	output_buf_write.dstBinding = LIGHT_BUFFER_BINDING_IDX;
	output_buf_write.dstArrayElement = 0;
	buf_info.buffer = qvk.buf_light.buffer;
	buf_info.range = sizeof(LightBuffer);
	vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);

	output_buf_write.dstBinding = IQM_MATRIX_BUFFER_BINDING_IDX;
	buf_info.buffer = qvk.buf_iqm_matrices.buffer;
	buf_info.range = sizeof(IqmMatrixBuffer);
	vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);

	output_buf_write.dstBinding = READBACK_BUFFER_BINDING_IDX;
	buf_info.buffer = qvk.buf_readback.buffer;
	buf_info.range = sizeof(ReadbackBuffer);
	vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);

	output_buf_write.dstBinding = TONE_MAPPING_BUFFER_BINDING_IDX;
	buf_info.buffer = qvk.buf_tonemap.buffer;
	buf_info.range = sizeof(ToneMappingBuffer);
	vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);

	output_buf_write.dstBinding = SUN_COLOR_BUFFER_BINDING_IDX;
	buf_info.buffer = qvk.buf_sun_color.buffer;
	buf_info.range = sizeof(SunColorBuffer);
	vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);

	output_buf_write.dstBinding = SUN_COLOR_UBO_BINDING_IDX;
	output_buf_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	buf_info.buffer = qvk.buf_sun_color.buffer;
	buf_info.range = sizeof(SunColorBuffer);
	vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);
	
	memset(model_vertex_data, 0, sizeof(model_vertex_data));

	for (int i = 0; i < MAX_MODELS; i++)
	{
		write_model_vbo_descriptor(i, null_buffer.buffer, null_buffer.size);
	}

	return VK_SUCCESS;
}

VkResult
vkpt_readback(ReadbackBuffer* dst)
{
	BufferResource_t* buffer = &qvk.buf_readback_staging[qvk.current_frame_index];
	void* mapped = buffer_map(buffer);

	if (mapped == NULL)
		return VK_ERROR_MEMORY_MAP_FAILED;

	memcpy(dst, mapped, sizeof(ReadbackBuffer));

	buffer_unmap(buffer);

	return VK_SUCCESS;
}

VkResult
vkpt_vertex_buffer_destroy()
{
	vkDestroyDescriptorPool(qvk.device, desc_pool_vertex_buffer, NULL);
	vkDestroyDescriptorSetLayout(qvk.device, qvk.desc_set_layout_vertex_buffer, NULL);
	desc_pool_vertex_buffer = VK_NULL_HANDLE;
	qvk.desc_set_layout_vertex_buffer = VK_NULL_HANDLE;

	for (int model = 0; model < MAX_MODELS; model++)
	{
		destroy_model_vbo(&model_vertex_data[model]);
	}

	buffer_destroy(&null_buffer);

	buffer_destroy(&qvk.buf_world);
	buffer_destroy(&qvk.buf_primitive_instanced);
	buffer_destroy(&qvk.buf_positions_instanced);

	buffer_destroy(&qvk.buf_light);
	buffer_destroy(&qvk.buf_iqm_matrices);
	buffer_destroy(&qvk.buf_readback);
	for (int frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++)
	{
		buffer_destroy(qvk.buf_light_staging + frame);
		buffer_destroy(qvk.buf_iqm_matrices_staging + frame);
		buffer_destroy(qvk.buf_readback_staging + frame);
	}

	buffer_destroy(&qvk.buf_tonemap);
	buffer_destroy(&qvk.buf_sun_color);

	Z_Free(qvk.iqm_matrices_shadow);
	Z_Free(qvk.iqm_matrices_prev);
	qvk.iqm_matrices_shadow = NULL;
	qvk.iqm_matrices_prev = NULL;

	return VK_SUCCESS;
}

VkResult vkpt_light_stats_create(bsp_mesh_t *bsp_mesh)
{
	vkpt_light_stats_destroy();

	// Light statistics: 2 uints (shadowed, unshadowed) per light per surface orientation (6) per cluster.
	uint32_t num_stats = bsp_mesh->num_clusters * bsp_mesh->num_light_polys * 6 * 2;

    // Handle rare cases when the map has zero lights
    if (num_stats == 0)
        num_stats = 1;

	for (int frame = 0; frame < NUM_LIGHT_STATS_BUFFERS; frame++)
	{
		buffer_create(qvk.buf_light_stats + frame, sizeof(uint32_t) * num_stats,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	}

	assert(NUM_LIGHT_STATS_BUFFERS == 3);

	VkDescriptorBufferInfo light_stats_buf_info[] = { {
			.buffer = qvk.buf_light_stats[0].buffer,
			.offset = 0,
			.range = qvk.buf_light_stats[0].size,
		}, {
			.buffer = qvk.buf_light_stats[1].buffer,
			.offset = 0,
			.range = qvk.buf_light_stats[1].size,
		}, {
			.buffer = qvk.buf_light_stats[2].buffer,
			.offset = 0,
			.range = qvk.buf_light_stats[2].size,
		} };

	VkWriteDescriptorSet output_buf_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = qvk.desc_set_vertex_buffer,
		.dstBinding = LIGHT_STATS_BUFFER_BINDING_IDX,
		.dstArrayElement = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = LENGTH(light_stats_buf_info),
		.pBufferInfo = light_stats_buf_info,
	};

	vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);

	return VK_SUCCESS;
}

VkResult vkpt_light_stats_destroy()
{
	for (int frame = 0; frame < NUM_LIGHT_STATS_BUFFERS; frame++)
	{
		buffer_destroy(qvk.buf_light_stats + frame);
	}

	return VK_SUCCESS;
}

VkResult
vkpt_vertex_buffer_create_pipelines()
{
	assert(!pipeline_instance_geometry);
	assert(!pipeline_animate_materials);
	assert(!pipeline_layout_instance_geometry);

	assert(qvk.desc_set_layout_ubo);
	assert(qvk.desc_set_layout_vertex_buffer); 

	VkDescriptorSetLayout desc_set_layouts[] = {
		qvk.desc_set_layout_ubo,
		qvk.desc_set_layout_vertex_buffer
	};

	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_instance_geometry, 
		.setLayoutCount = LENGTH(desc_set_layouts),
		.pSetLayouts    = desc_set_layouts,
	);

	VkComputePipelineCreateInfo compute_pipeline_info[] = {
		{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE(QVK_MOD_INSTANCE_GEOMETRY_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_instance_geometry
		},
		{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE(QVK_MOD_ANIMATE_MATERIALS_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_instance_geometry
		},
	};

	VkPipeline pipelines[2];
	_VK(vkCreateComputePipelines(qvk.device, 0, LENGTH(compute_pipeline_info), compute_pipeline_info, 0, pipelines));

	pipeline_instance_geometry = pipelines[0];
	pipeline_animate_materials = pipelines[1];

	return VK_SUCCESS;
}

VkResult
vkpt_vertex_buffer_destroy_pipelines()
{
	assert(pipeline_instance_geometry);
	assert(pipeline_animate_materials);
	assert(pipeline_layout_instance_geometry);

	vkDestroyPipeline(qvk.device, pipeline_instance_geometry, NULL);
	vkDestroyPipeline(qvk.device, pipeline_animate_materials, NULL);
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_instance_geometry, NULL);

	pipeline_instance_geometry = VK_NULL_HANDLE;
	pipeline_animate_materials = VK_NULL_HANDLE;
	pipeline_layout_instance_geometry = VK_NULL_HANDLE;

	return VK_SUCCESS;
}

VkResult
vkpt_instance_geometry(VkCommandBuffer cmd_buf, uint32_t num_instances, qboolean update_world_animations)
{
	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo,
		qvk.desc_set_vertex_buffer
	};
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_instance_geometry);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_instance_geometry, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	vkCmdDispatch(cmd_buf, num_instances, 1, 1);

	if (update_world_animations)
	{
		vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_animate_materials);

		uint num_groups = ((vkpt_refdef.bsp_mesh_world.geom_opaque.prim_counts[0] + vkpt_refdef.bsp_mesh_world.geom_transparent.prim_counts[0]
			+ vkpt_refdef.bsp_mesh_world.geom_masked.prim_counts[0]) + 255) / 256;
		vkCmdDispatch(cmd_buf, num_groups, 1, 1);
	}

	VkBufferMemoryBarrier barrier = {
		.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.srcAccessMask       = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
		.buffer              = qvk.buf_primitive_instanced.buffer,
		.size                = qvk.buf_primitive_instanced.size,
		.srcQueueFamilyIndex = qvk.queue_idx_graphics,
		.dstQueueFamilyIndex = qvk.queue_idx_graphics
	};

	vkCmdPipelineBarrier(
			cmd_buf,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			0,
			0, NULL,
			1, &barrier,
			0, NULL);

	return VK_SUCCESS;
}

qboolean vkpt_model_is_static(const model_t* model)
{
	if (!model)
		return qfalse;

	size_t model_index = model - r_models;
	const model_vbo_t* vbo = &model_vertex_data[model_index];

	return vbo->is_static;
}

const model_geometry_t* vkpt_get_model_geometry(const model_t* model, model_blas_index_t blas_index)
{
	if (!model)
		return NULL;

	size_t model_index = model - r_models;
	const model_vbo_t* vbo = &model_vertex_data[model_index];

	switch(blas_index)
	{
	case MODEL_BLAS_OPAQUE:
		return &vbo->geom_opaque;
	case MODEL_BLAS_TRANSPARENT:
		return &vbo->geom_transparent;
	case MODEL_BLAS_MASKED:
		return &vbo->geom_masked;

	case MODEL_BLAS_COUNT:
	default:
		assert(!"Invalid geometry index");
		return NULL;
	}
}

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
