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

#include "shared/shared.h"
#include "vkpt.h"
#include "vk_util.h"
#include "shader/vertex_buffer.h"
#include "../../client/client.h"

#include <assert.h>

#define RAY_GEN_ACCEL_STRUCTURE_BINDING_IDX 0
#define RAY_GEN_PARTICLE_COLOR_BUFFER_BINDING_IDX 1
#define RAY_GEN_BEAM_COLOR_BUFFER_BINDING_IDX 2
#define RAY_GEN_SPRITE_INFO_BUFFER_BINDING_IDX 3

#define SIZE_SCRATCH_BUFFER (1 << 25)

#define INSTANCE_MAX_NUM 12

static uint32_t shaderGroupHandleSize = 0;
static uint32_t shaderGroupBaseAlignment = 0;
static uint32_t minAccelerationStructureScratchOffsetAlignment = 0;

typedef struct accel_bottom_match_info_s {
	int fast_build;
	uint32_t vertex_count;
	uint32_t index_count;
} accel_bottom_match_info_t;

typedef struct accel_top_match_info_s {
	uint32_t instanceCount;
} accel_top_match_info_t;

typedef struct {
	VkAccelerationStructureKHR accel_khr;
	VkAccelerationStructureNV accel_nv;
	accel_bottom_match_info_t match;
	BufferResource_t mem;
	qboolean present;
} blas_t;

typedef enum {
	PIPELINE_PRIMARY_RAYS,
	PIPELINE_REFLECT_REFRACT_1,
	PIPELINE_REFLECT_REFRACT_2,
    PIPELINE_DIRECT_LIGHTING,
    PIPELINE_DIRECT_LIGHTING_CAUSTICS,
    PIPELINE_INDIRECT_LIGHTING_FIRST,
    PIPELINE_INDIRECT_LIGHTING_SECOND,

	PIPELINE_COUNT
} pipeline_index_t;

static BufferResource_t           buf_accel_scratch;
static size_t                     scratch_buf_ptr = 0;
static BufferResource_t           buf_instances[MAX_FRAMES_IN_FLIGHT];
static int                        transparent_primitive_offset = 0;
static int                        sky_primitive_offset = 0;
static int                        custom_sky_primitive_offset = 0;
static int                        transparent_model_primitive_offset = 0;
static int                        viewer_model_primitive_offset = 0;
static int                        viewer_weapon_primitive_offset = 0;
static int                        explosions_primitive_offset = 0;
static blas_t                     blas_static;
static blas_t                     blas_transparent;
static blas_t                     blas_sky;
static blas_t                     blas_custom_sky;
static blas_t                     blas_dynamic[MAX_FRAMES_IN_FLIGHT];
static blas_t                     blas_transparent_models[MAX_FRAMES_IN_FLIGHT];
static blas_t                     blas_viewer_models[MAX_FRAMES_IN_FLIGHT];
static blas_t                     blas_viewer_weapon[MAX_FRAMES_IN_FLIGHT];
static blas_t                     blas_explosions[MAX_FRAMES_IN_FLIGHT];
static blas_t                     blas_particles[MAX_FRAMES_IN_FLIGHT];
static blas_t                     blas_beams[MAX_FRAMES_IN_FLIGHT];
static blas_t                     blas_sprites[MAX_FRAMES_IN_FLIGHT];

static VkAccelerationStructureKHR accel_top_khr[MAX_FRAMES_IN_FLIGHT];
static VkAccelerationStructureNV  accel_top_nv[MAX_FRAMES_IN_FLIGHT];
static accel_top_match_info_t     accel_top_match[MAX_FRAMES_IN_FLIGHT];
static BufferResource_t           mem_accel_top[MAX_FRAMES_IN_FLIGHT];

static BufferResource_t      buf_shader_binding_table;

static VkDescriptorPool      rt_descriptor_pool;
static VkDescriptorSet       rt_descriptor_set[MAX_FRAMES_IN_FLIGHT];
static VkDescriptorSetLayout rt_descriptor_set_layout;
static VkPipelineLayout      rt_pipeline_layout;
static VkPipeline            rt_pipelines[PIPELINE_COUNT];

cvar_t*                      cvar_pt_enable_particles = NULL;
cvar_t*                      cvar_pt_enable_beams = NULL;
cvar_t*                      cvar_pt_enable_sprites = NULL;

extern cvar_t *cvar_pt_caustics;
extern cvar_t *cvar_pt_reflect_refract;


typedef struct QvkGeometryInstance_s {
	float    transform[12];
	uint32_t instance_id     : 24;
	uint32_t mask            :  8;
	uint32_t instance_offset : 24;
	uint32_t flags           :  8;
	uint64_t acceleration_structure; // handle on NV, address on KHR
} QvkGeometryInstance_t;

typedef struct {
	int gpu_index;
	int bounce;
} pt_push_constants_t;

#define MEM_BARRIER_BUILD_ACCEL(cmd_buf, ...) \
	do { \
		VkMemoryBarrier mem_barrier = {  \
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,  \
			.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR \
						   | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR, \
			.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR, \
			__VA_ARGS__  \
		};  \
	 \
		vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, \
				VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, \
				&mem_barrier, 0, 0, 0, 0); \
	} while(0)

VkResult
vkpt_pt_init()
{
	if (qvk.use_khr_ray_tracing)
	{
		VkPhysicalDeviceAccelerationStructurePropertiesKHR accel_struct_properties = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR,
			.pNext = NULL
		};

		VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_properties_khr = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
			.pNext = &accel_struct_properties
		};

		VkPhysicalDeviceProperties2 dev_props2 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
			.pNext = &rt_properties_khr,
		};

		vkGetPhysicalDeviceProperties2(qvk.physical_device, &dev_props2);

		shaderGroupBaseAlignment = rt_properties_khr.shaderGroupBaseAlignment;
		shaderGroupHandleSize = rt_properties_khr.shaderGroupHandleSize;
		minAccelerationStructureScratchOffsetAlignment = accel_struct_properties.minAccelerationStructureScratchOffsetAlignment;
	}
	else
	{
		VkPhysicalDeviceRayTracingPropertiesNV rt_properties_nv = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV,
			.pNext = NULL
		};

		VkPhysicalDeviceProperties2 dev_props2 = {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
			.pNext = &rt_properties_nv,
		};

		vkGetPhysicalDeviceProperties2(qvk.physical_device, &dev_props2);

		shaderGroupBaseAlignment = rt_properties_nv.shaderGroupBaseAlignment;
		shaderGroupHandleSize = rt_properties_nv.shaderGroupHandleSize;
	}

	buffer_create(&buf_accel_scratch, SIZE_SCRATCH_BUFFER, 
		qvk.use_khr_ray_tracing ? VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	for(int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		buffer_create(buf_instances + i, INSTANCE_MAX_NUM * sizeof(QvkGeometryInstance_t), 
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | (qvk.use_khr_ray_tracing ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT : VK_BUFFER_USAGE_RAY_TRACING_BIT_NV),
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}

	/* create descriptor set layout */
	VkDescriptorSetLayoutBinding bindings[] = {
		{
			.binding         = RAY_GEN_ACCEL_STRUCTURE_BINDING_IDX,
			.descriptorType  = qvk.use_khr_ray_tracing ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR : VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV,
			.descriptorCount = 1,
			.stageFlags      = qvk.use_ray_query ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_RAYGEN_BIT_KHR,
		},
		{
			.binding         = RAY_GEN_PARTICLE_COLOR_BUFFER_BINDING_IDX,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
			.descriptorCount = 1,
			.stageFlags      = qvk.use_ray_query ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
		},
		{
			.binding         = RAY_GEN_BEAM_COLOR_BUFFER_BINDING_IDX,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
			.descriptorCount = 1,
			.stageFlags      = qvk.use_ray_query ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
		},
		{
			.binding         = RAY_GEN_SPRITE_INFO_BUFFER_BINDING_IDX,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
			.descriptorCount = 1,
			.stageFlags      = qvk.use_ray_query ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
		},
	};

	VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = LENGTH(bindings),
		.pBindings    = bindings
	};
	_VK(vkCreateDescriptorSetLayout(qvk.device, &layout_info, NULL, &rt_descriptor_set_layout));
	ATTACH_LABEL_VARIABLE(rt_descriptor_set_layout, DESCRIPTOR_SET_LAYOUT);


	VkDescriptorSetLayout desc_set_layouts[] = {
		rt_descriptor_set_layout,
		qvk.desc_set_layout_ubo,
		qvk.desc_set_layout_textures,
		qvk.desc_set_layout_vertex_buffer
	};

	/* create pipeline */
	VkPushConstantRange push_constant_range = {
		.stageFlags		= qvk.use_ray_query ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_RAYGEN_BIT_KHR,
		.offset			= 0,
		.size			= sizeof(pt_push_constants_t),
	};

	VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
		.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = LENGTH(desc_set_layouts),
		.pSetLayouts    = desc_set_layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_constant_range,
	};

	_VK(vkCreatePipelineLayout(qvk.device, &pipeline_layout_create_info, NULL, &rt_pipeline_layout));
	ATTACH_LABEL_VARIABLE(rt_pipeline_layout, PIPELINE_LAYOUT);

	VkDescriptorPoolSize pool_sizes[] = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_FRAMES_IN_FLIGHT * LENGTH(bindings) },
		{ qvk.use_khr_ray_tracing ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR : VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, MAX_FRAMES_IN_FLIGHT }
	};

	VkDescriptorPoolCreateInfo pool_create_info = {
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets       = MAX_FRAMES_IN_FLIGHT,
		.poolSizeCount = LENGTH(pool_sizes),
		.pPoolSizes    = pool_sizes
	};

	_VK(vkCreateDescriptorPool(qvk.device, &pool_create_info, NULL, &rt_descriptor_pool));
	ATTACH_LABEL_VARIABLE(rt_descriptor_pool, DESCRIPTOR_POOL);

	VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {
		.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool     = rt_descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts        = &rt_descriptor_set_layout,
	};

	for(int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		_VK(vkAllocateDescriptorSets(qvk.device, &descriptor_set_alloc_info, rt_descriptor_set + i));
		ATTACH_LABEL_VARIABLE(rt_descriptor_set[i], DESCRIPTOR_SET);
	}

	cvar_pt_enable_particles = Cvar_Get("pt_enable_particles", "1", 0);
	cvar_pt_enable_beams = Cvar_Get("pt_enable_beams", "1", 0);
	cvar_pt_enable_sprites= Cvar_Get("pt_enable_sprites", "1", 0);

	return VK_SUCCESS;
}

VkResult
vkpt_pt_update_descripter_set_bindings(int idx)
{
	/* update descriptor set bindings */
	VkWriteDescriptorSetAccelerationStructureKHR desc_accel_struct_info_khr = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = accel_top_khr + idx
	};

	VkWriteDescriptorSetAccelerationStructureNV desc_accel_struct_info_nv = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV,
		.accelerationStructureCount = 1,
		.pAccelerationStructures = accel_top_nv + idx
	};

	VkBufferView particle_color_buffer_view = get_transparency_particle_color_buffer_view();
	VkBufferView beam_color_buffer_view = get_transparency_beam_color_buffer_view();
	VkBufferView sprite_info_buffer_view = get_transparency_sprite_info_buffer_view();

	VkWriteDescriptorSet writes[] = {
		{
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext           = qvk.use_khr_ray_tracing ? (const void*)&desc_accel_struct_info_khr : (const void*)&desc_accel_struct_info_nv,
			.dstSet          = rt_descriptor_set[idx],
			.dstBinding      = RAY_GEN_ACCEL_STRUCTURE_BINDING_IDX,
			.descriptorCount = 1,
			.descriptorType  = qvk.use_khr_ray_tracing ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR : VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV
		},
		{
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = rt_descriptor_set[idx],
			.dstBinding      = RAY_GEN_PARTICLE_COLOR_BUFFER_BINDING_IDX,
			.descriptorCount = 1,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
			.pTexelBufferView = &particle_color_buffer_view
		},
		{
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = rt_descriptor_set[idx],
			.dstBinding      = RAY_GEN_BEAM_COLOR_BUFFER_BINDING_IDX,
			.descriptorCount = 1,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
			.pTexelBufferView = &beam_color_buffer_view
		},
		{
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = rt_descriptor_set[idx],
			.dstBinding      = RAY_GEN_SPRITE_INFO_BUFFER_BINDING_IDX,
			.descriptorCount = 1,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
			.pTexelBufferView = &sprite_info_buffer_view
		},
	};

	vkUpdateDescriptorSets(qvk.device, LENGTH(writes), writes, 0, NULL);

	return VK_SUCCESS;
}

static size_t
get_scratch_buffer_size_nv(VkAccelerationStructureNV ac)
{
	VkAccelerationStructureMemoryRequirementsInfoNV mem_req_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV,
		.accelerationStructure = ac,
		.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV
	};

	VkMemoryRequirements2 mem_req = { 0 };
	mem_req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
	qvkGetAccelerationStructureMemoryRequirementsNV(qvk.device, &mem_req_info, &mem_req);

	return mem_req.memoryRequirements.size;
}

static void destroy_blas(blas_t* blas)
{
	buffer_destroy(&blas->mem);

	if (blas->accel_khr)
	{
		qvkDestroyAccelerationStructureKHR(qvk.device, blas->accel_khr, NULL);
		blas->accel_khr = VK_NULL_HANDLE;
	}

	if (blas->accel_nv)
	{
		qvkDestroyAccelerationStructureNV(qvk.device, blas->accel_nv, NULL);
		blas->accel_nv = VK_NULL_HANDLE;
	}

	blas->match.fast_build = 0;
	blas->match.index_count = 0;
	blas->match.vertex_count = 0;
}

void vkpt_pt_destroy_static()
{
	destroy_blas(&blas_static);
	destroy_blas(&blas_transparent);
	destroy_blas(&blas_sky);
	destroy_blas(&blas_custom_sky);
}

static void vkpt_pt_destroy_dynamic(int idx)
{
	destroy_blas(&blas_dynamic[idx]);
	destroy_blas(&blas_transparent_models[idx]);
	destroy_blas(&blas_viewer_models[idx]);
	destroy_blas(&blas_viewer_weapon[idx]);
	destroy_blas(&blas_explosions[idx]);
	destroy_blas(&blas_particles[idx]);
	destroy_blas(&blas_beams[idx]);
	destroy_blas(&blas_sprites[idx]);
}

static inline int accel_matches(accel_bottom_match_info_t *match,
								int fast_build,
								uint32_t vertex_count,
								uint32_t index_count) {
	return match->fast_build == fast_build &&
		   match->vertex_count >= vertex_count &&
		   match->index_count >= index_count;
}

// How much to bloat the dynamic geometry allocations
// to try to avoid later allocations.
#define DYNAMIC_GEOMETRY_BLOAT_FACTOR 2


static VkResult
vkpt_pt_create_accel_bottom(
	VkCommandBuffer cmd_buf,
	BufferResource_t* buffer_vertex,
	VkDeviceAddress offset_vertex,
	BufferResource_t* buffer_index,
	VkDeviceAddress offset_index,
	int num_vertices,
	int num_indices,
	blas_t* blas,
	qboolean is_dynamic,
	qboolean fast_build)
{
	assert(blas);

	if (num_vertices == 0)
	{
		blas->present = qfalse;
		return VK_SUCCESS;
	}

	if (qvk.use_khr_ray_tracing)
	{
		assert(buffer_vertex->address);
		if (buffer_index) assert(buffer_index->address);

		const VkAccelerationStructureGeometryTrianglesDataKHR triangles = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
			.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
			.vertexData = {.deviceAddress = buffer_vertex->address + offset_vertex },
			.vertexStride = sizeof(float) * 3,
			.maxVertex = num_vertices - 1,
			.indexData = {.deviceAddress = buffer_index ? (buffer_index->address + offset_index) : 0 },
			.indexType = buffer_index ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_NONE_KHR,
		};

		const VkAccelerationStructureGeometryDataKHR geometry_data = { 
			.triangles = triangles
		};

		const VkAccelerationStructureGeometryKHR geometry = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
			.geometry = geometry_data
		};

		const VkAccelerationStructureGeometryKHR* geometries = &geometry;

		VkAccelerationStructureBuildGeometryInfoKHR buildInfo;

		// Prepare build info now, acceleration is filled later
		buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		buildInfo.pNext = NULL;
		buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		buildInfo.flags = fast_build ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;
		buildInfo.dstAccelerationStructure = VK_NULL_HANDLE;
		buildInfo.geometryCount = 1;
		buildInfo.pGeometries = geometries;
		buildInfo.ppGeometries = NULL;

		int doFree = 0;
		int doAlloc = 0;

		if (!is_dynamic || !accel_matches(&blas->match, fast_build, num_vertices, num_indices) || blas->accel_khr == VK_NULL_HANDLE)
		{
			doAlloc = 1;
			doFree = (blas->accel_khr != VK_NULL_HANDLE);
		}

		if (doFree)
		{
			destroy_blas(blas);
		}

		// Find size to build on the device
		uint32_t max_primitive_count = max(num_vertices, num_indices) / 3; // number of tris
		VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		qvkGetAccelerationStructureBuildSizesKHR(qvk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &max_primitive_count, &sizeInfo);

		if (doAlloc)
		{
			int num_vertices_to_allocate = num_vertices;
			int num_indices_to_allocate = num_indices;

			// Allocate more memory / larger BLAS for dynamic objects
			if (is_dynamic)
			{
				num_vertices_to_allocate *= DYNAMIC_GEOMETRY_BLOAT_FACTOR;
				num_indices_to_allocate *= DYNAMIC_GEOMETRY_BLOAT_FACTOR;

				max_primitive_count = max(num_vertices_to_allocate, num_indices_to_allocate) / 3;
				qvkGetAccelerationStructureBuildSizesKHR(qvk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &max_primitive_count, &sizeInfo);
			}

			// Create acceleration structure
			VkAccelerationStructureCreateInfoKHR createInfo = {
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
				.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
				.size = sizeInfo.accelerationStructureSize
			};

			// Create the buffer for the acceleration structure
			buffer_create(&blas->mem, sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			createInfo.buffer = blas->mem.buffer;

			// Create the acceleration structure
			qvkCreateAccelerationStructureKHR(qvk.device, &createInfo, NULL, &blas->accel_khr);

			blas->match.fast_build = fast_build;
			blas->match.vertex_count = num_vertices_to_allocate;
			blas->match.index_count = num_indices_to_allocate;
		}

		// set where the build lands
		buildInfo.dstAccelerationStructure = blas->accel_khr;

		// Use shared scratch buffer for holding the temporary data of the acceleration structure builder
		buildInfo.scratchData.deviceAddress = buf_accel_scratch.address + scratch_buf_ptr;
		assert(buf_accel_scratch.address);

		// Update the scratch buffer ptr
		scratch_buf_ptr += sizeInfo.buildScratchSize;
		scratch_buf_ptr = align(scratch_buf_ptr, minAccelerationStructureScratchOffsetAlignment);
		assert(scratch_buf_ptr < SIZE_SCRATCH_BUFFER);

		// build offset
		VkAccelerationStructureBuildRangeInfoKHR offset = { .primitiveCount = max(num_vertices, num_indices) / 3 };
		VkAccelerationStructureBuildRangeInfoKHR* offsets = &offset;

		qvkCmdBuildAccelerationStructuresKHR(cmd_buf, 1, &buildInfo, &offsets);
	}
	else // (!qvk.use_khr_ray_tracing)
	{
		VkGeometryNV geometry = {
			.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV,
			.geometry = {
				.triangles = {
					.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV,
					.vertexData = buffer_vertex->buffer,
					.vertexOffset = offset_vertex,
					.vertexCount = num_vertices,
					.vertexStride = sizeof(float) * 3,
					.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
					.indexData = buffer_index ? buffer_index->buffer : VK_NULL_HANDLE,
					.indexType = buffer_index ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_NONE_NV,
					.indexCount = num_indices,
				},
				.aabbs = { .sType = VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV }
			}
		};

		int doFree = 0;
		int doAlloc = 0;

		if (!is_dynamic || !accel_matches(&blas->match, fast_build, num_vertices, num_indices) || blas->accel_nv == VK_NULL_HANDLE) {
			doAlloc = 1;
			doFree = (blas->accel_nv != VK_NULL_HANDLE);
		}

		if (doFree) 
		{
			destroy_blas(blas);
		}

		if (doAlloc) 
		{
			VkGeometryNV allocGeometry = geometry;

			// Allocate more memory / larger BLAS for dynamic objects
			if (is_dynamic)
			{
				allocGeometry.geometry.triangles.indexCount *= DYNAMIC_GEOMETRY_BLOAT_FACTOR;
				allocGeometry.geometry.triangles.vertexCount *= DYNAMIC_GEOMETRY_BLOAT_FACTOR;
			}

			VkAccelerationStructureCreateInfoNV accel_create_info = 
			{
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV,
				.info = {
					.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV,
					.instanceCount = 0,
					.geometryCount = 1,
					.pGeometries = &allocGeometry,
					.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV,
					.flags = fast_build ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_NV : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV
				}
			};

			qvkCreateAccelerationStructureNV(qvk.device, &accel_create_info, NULL, &blas->accel_nv);

			VkAccelerationStructureMemoryRequirementsInfoNV mem_req_info = {
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV,
				.accelerationStructure = blas->accel_nv,
				.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV
			};

			VkMemoryRequirements2 mem_req = { 0 };
			mem_req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
			qvkGetAccelerationStructureMemoryRequirementsNV(qvk.device, &mem_req_info, &mem_req);

			_VK(buffer_create(&blas->mem, mem_req.memoryRequirements.size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));

			VkBindAccelerationStructureMemoryInfoNV bind_info = {
				.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV,
				.accelerationStructure = blas->accel_nv,
				.memory = blas->mem.memory
			};

			_VK(qvkBindAccelerationStructureMemoryNV(qvk.device, 1, &bind_info));

			blas->match.fast_build = fast_build;
			blas->match.vertex_count = allocGeometry.geometry.triangles.vertexCount;
			blas->match.index_count = allocGeometry.geometry.triangles.indexCount;
		}

		size_t scratch_buf_size = get_scratch_buffer_size_nv(blas->accel_nv);
		assert(scratch_buf_ptr + scratch_buf_size < SIZE_SCRATCH_BUFFER);

		VkAccelerationStructureInfoNV as_info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV,
			.geometryCount = 1,
			.pGeometries = &geometry,
			.flags = fast_build ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_NV : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV
		};

		qvkCmdBuildAccelerationStructureNV(cmd_buf, &as_info,
			VK_NULL_HANDLE, /* instance buffer */
			0 /* instance offset */,
			VK_FALSE,  /* update */
			blas->accel_nv,
			VK_NULL_HANDLE,
			buf_accel_scratch.buffer,
			scratch_buf_ptr);

		scratch_buf_ptr += scratch_buf_size;
	}

	blas->present = qtrue;

	return VK_SUCCESS;
}

VkResult
vkpt_pt_create_static(
	int num_vertices, 
	int num_vertices_transparent,
	int num_vertices_sky,
	int num_vertices_custom_sky)
{
	VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);
	VkDeviceAddress address_vertex = offsetof(BspVertexBuffer, positions_bsp);

	scratch_buf_ptr = 0;

	VkResult ret = vkpt_pt_create_accel_bottom(cmd_buf, &qvk.buf_vertex_bsp, address_vertex, NULL, 0, num_vertices, 0, &blas_static, qfalse, qfalse);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	address_vertex += num_vertices * sizeof(float) * 3;
	scratch_buf_ptr = 0;

	ret = vkpt_pt_create_accel_bottom(cmd_buf, &qvk.buf_vertex_bsp, address_vertex, NULL, 0, num_vertices_transparent, 0, &blas_transparent, qfalse, qfalse);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	address_vertex += num_vertices_transparent * sizeof(float) * 3;
	scratch_buf_ptr = 0;

	ret = vkpt_pt_create_accel_bottom(cmd_buf, &qvk.buf_vertex_bsp, address_vertex, NULL, 0, num_vertices_sky, 0, &blas_sky, qfalse, qfalse);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	address_vertex += num_vertices_sky * sizeof(float) * 3;
	scratch_buf_ptr = 0;

	ret = vkpt_pt_create_accel_bottom(cmd_buf, &qvk.buf_vertex_bsp, address_vertex, NULL, 0, num_vertices_custom_sky, 0, &blas_custom_sky, qfalse, qfalse);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	address_vertex += num_vertices_custom_sky * sizeof(float) * 3;
	scratch_buf_ptr = 0;

	transparent_primitive_offset = num_vertices / 3;
	sky_primitive_offset = transparent_primitive_offset + num_vertices_transparent / 3;
	custom_sky_primitive_offset = sky_primitive_offset + num_vertices_sky / 3;

	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, qtrue);
	vkpt_wait_idle(qvk.queue_graphics, &qvk.cmd_buffers_graphics);

	return ret;
}

VkResult
vkpt_pt_create_all_dynamic(
	VkCommandBuffer cmd_buf,
	int idx, 
	const EntityUploadInfo* upload_info)
{
	scratch_buf_ptr = 0;

	uint64_t offset_vertex_base = offsetof(ModelDynamicVertexBuffer, positions_instanced);
	uint64_t offset_vertex = offset_vertex_base;
	uint64_t offset_index = 0;
	vkpt_pt_create_accel_bottom(cmd_buf, &qvk.buf_vertex_model_dynamic, offset_vertex, NULL, offset_index, upload_info->dynamic_vertex_num, 0, blas_dynamic + idx, qtrue, qtrue);

	transparent_model_primitive_offset = upload_info->transparent_model_vertex_offset / 3;
	offset_vertex = offset_vertex_base + upload_info->transparent_model_vertex_offset * sizeof(float) * 3;
	vkpt_pt_create_accel_bottom(cmd_buf, &qvk.buf_vertex_model_dynamic, offset_vertex, NULL, offset_index, upload_info->transparent_model_vertex_num, 0, blas_transparent_models + idx, qtrue, qtrue);

	viewer_model_primitive_offset = upload_info->viewer_model_vertex_offset / 3;
	offset_vertex = offset_vertex_base + upload_info->viewer_model_vertex_offset * sizeof(float) * 3;
	vkpt_pt_create_accel_bottom(cmd_buf, &qvk.buf_vertex_model_dynamic, offset_vertex, NULL, offset_index, upload_info->viewer_model_vertex_num, 0, blas_viewer_models + idx, qtrue, qtrue);

	viewer_weapon_primitive_offset = upload_info->viewer_weapon_vertex_offset / 3;
	offset_vertex = offset_vertex_base + upload_info->viewer_weapon_vertex_offset * sizeof(float) * 3;
	vkpt_pt_create_accel_bottom(cmd_buf, &qvk.buf_vertex_model_dynamic, offset_vertex, NULL, offset_index, upload_info->viewer_weapon_vertex_num, 0, blas_viewer_weapon + idx, qtrue, qtrue);

	explosions_primitive_offset = upload_info->explosions_vertex_offset / 3;
	offset_vertex = offset_vertex_base + upload_info->explosions_vertex_offset * sizeof(float) * 3;
	vkpt_pt_create_accel_bottom(cmd_buf, &qvk.buf_vertex_model_dynamic, offset_vertex, NULL, offset_index, upload_info->explosions_vertex_num, 0, blas_explosions + idx, qtrue, qtrue);

	BufferResource_t* buffer_vertex = NULL;
	BufferResource_t* buffer_index = NULL;
	uint32_t num_vertices = 0;
	uint32_t num_indices = 0;
	vkpt_get_transparency_buffers(VKPT_TRANSPARENCY_PARTICLES, &buffer_vertex, &offset_vertex, &buffer_index, &offset_index, &num_vertices, &num_indices);
	vkpt_pt_create_accel_bottom(cmd_buf, buffer_vertex, offset_vertex, buffer_index, offset_index, num_vertices, num_indices, blas_particles + idx, qtrue, qtrue);

	vkpt_get_transparency_buffers(VKPT_TRANSPARENCY_BEAMS, &buffer_vertex, &offset_vertex, &buffer_index, &offset_index, &num_vertices, &num_indices);
	vkpt_pt_create_accel_bottom(cmd_buf, buffer_vertex, offset_vertex, buffer_index, offset_index, num_vertices, num_indices, blas_beams + idx, qtrue, qtrue);
	
	vkpt_get_transparency_buffers(VKPT_TRANSPARENCY_SPRITES, &buffer_vertex, &offset_vertex, &buffer_index, &offset_index, &num_vertices, &num_indices);
	vkpt_pt_create_accel_bottom(cmd_buf, buffer_vertex, offset_vertex, buffer_index, offset_index, num_vertices, num_indices, blas_sprites + idx, qtrue, qtrue);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	scratch_buf_ptr = 0;

	return VK_SUCCESS;
}

void
vkpt_pt_destroy_toplevel(int idx)
{
	if(accel_top_khr[idx]) {
		qvkDestroyAccelerationStructureKHR(qvk.device, accel_top_khr[idx], NULL);
		accel_top_khr[idx] = VK_NULL_HANDLE;
		accel_top_match[idx].instanceCount = 0;
	}

	if (accel_top_nv[idx]) {
		qvkDestroyAccelerationStructureNV(qvk.device, accel_top_nv[idx], NULL);
		accel_top_nv[idx] = VK_NULL_HANDLE;
		accel_top_match[idx].instanceCount = 0;
	}

	buffer_destroy(&mem_accel_top[idx]);
}

static void
append_blas(QvkGeometryInstance_t *instances, int *num_instances, blas_t* blas, int instance_id, int mask, int flags, int sbt_offset)
{
	if (!blas->present)
		return;

	QvkGeometryInstance_t instance = {
		.transform = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
		},
		.instance_id = instance_id,
		.mask = mask,
		.instance_offset = sbt_offset,
		.flags = flags,
		.acceleration_structure = 0
	};

	if (qvk.use_khr_ray_tracing)
	{
		VkAccelerationStructureDeviceAddressInfoKHR  as_device_address_info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
			.accelerationStructure = blas->accel_khr,
		};

		instance.acceleration_structure = qvkGetAccelerationStructureDeviceAddressKHR(qvk.device, &as_device_address_info);
	}
	else
	{
		_VK(qvkGetAccelerationStructureHandleNV(qvk.device, blas->accel_nv, sizeof(uint64_t), &instance.acceleration_structure));
	}

	assert(*num_instances < INSTANCE_MAX_NUM);
	memcpy(instances + *num_instances, &instance, sizeof(instance));
	++*num_instances;
}

VkResult
vkpt_pt_create_toplevel(VkCommandBuffer cmd_buf, int idx, qboolean include_world, qboolean weapon_left_handed)
{
	QvkGeometryInstance_t instances[INSTANCE_MAX_NUM];
	int num_instances = 0;

	if (include_world)
	{
		append_blas(instances, &num_instances, &blas_static, 0, AS_FLAG_OPAQUE, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR, 0);
		append_blas(instances, &num_instances, &blas_transparent, transparent_primitive_offset, AS_FLAG_TRANSPARENT, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR, 0);
		append_blas(instances, &num_instances, &blas_sky, AS_INSTANCE_FLAG_SKY | sky_primitive_offset, AS_FLAG_SKY, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR, 0);
		append_blas(instances, &num_instances, &blas_custom_sky, AS_INSTANCE_FLAG_SKY | custom_sky_primitive_offset, AS_FLAG_CUSTOM_SKY, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR, 0);
	}

	append_blas(instances, &num_instances, &blas_dynamic[idx], AS_INSTANCE_FLAG_DYNAMIC, AS_FLAG_OPAQUE, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR, 0);
	append_blas(instances, &num_instances, &blas_transparent_models[idx], AS_INSTANCE_FLAG_DYNAMIC | transparent_model_primitive_offset, AS_FLAG_TRANSPARENT, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR, 0);
	append_blas(instances, &num_instances, &blas_explosions[idx], AS_INSTANCE_FLAG_DYNAMIC | explosions_primitive_offset, AS_FLAG_EXPLOSIONS, 0, 3);
    append_blas(instances, &num_instances, &blas_viewer_weapon[idx], AS_INSTANCE_FLAG_DYNAMIC | viewer_weapon_primitive_offset, AS_FLAG_VIEWER_WEAPON, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR | (weapon_left_handed ? VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR : 0), 0);

	if (cl_player_model->integer == CL_PLAYER_MODEL_FIRST_PERSON)
	{
		append_blas(instances, &num_instances, &blas_viewer_models[idx], AS_INSTANCE_FLAG_DYNAMIC | viewer_model_primitive_offset, AS_FLAG_VIEWER_MODELS, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR | VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, 0);
	}

	if (cvar_pt_enable_particles->integer != 0)
	{
		append_blas(instances, &num_instances, &blas_particles[idx], 0, AS_FLAG_PARTICLES, VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, 1);
	}

	if (cvar_pt_enable_beams->integer != 0)
	{
		append_blas(instances, &num_instances, &blas_beams[idx], 0, AS_FLAG_PARTICLES, VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, 2);
	}

	if (cvar_pt_enable_sprites->integer != 0)
	{
		append_blas(instances, &num_instances, &blas_sprites[idx], 0, AS_FLAG_EXPLOSIONS, VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, 4);
	}
	
	void *instance_data = buffer_map(buf_instances + idx);
	memcpy(instance_data, &instances, sizeof(QvkGeometryInstance_t) * num_instances);

	buffer_unmap(buf_instances + idx);
	instance_data = NULL;

	if (qvk.use_khr_ray_tracing)
	{
		// Build the TLAS
		VkAccelerationStructureGeometryDataKHR geometry = {
			.instances = {
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
				.data = {.deviceAddress = buf_instances[idx].address }
			}
		};
		assert(buf_instances[idx].address);

		VkAccelerationStructureGeometryKHR topASGeometry = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
			.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
			.geometry = geometry
		};

		// Find size to build on the device
		VkAccelerationStructureBuildGeometryInfoKHR buildInfo = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
			.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR,
			.geometryCount = 1,
			.pGeometries = &topASGeometry,
			.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
			.srcAccelerationStructure = VK_NULL_HANDLE
		};

		VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		qvkGetAccelerationStructureBuildSizesKHR(qvk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &num_instances, &sizeInfo);
		assert(sizeInfo.accelerationStructureSize < SIZE_SCRATCH_BUFFER);

		if (accel_top_match[idx].instanceCount < num_instances) {
			vkpt_pt_destroy_toplevel(idx);

			// Create the buffer for the acceleration structure
			buffer_create(&mem_accel_top[idx], sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			// Create TLAS
			// Create acceleration structure
			VkAccelerationStructureCreateInfoKHR createInfo = {
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
				.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
				.size = sizeInfo.accelerationStructureSize,
				.buffer = mem_accel_top[idx].buffer
			};

			// Create the acceleration structure
			qvkCreateAccelerationStructureKHR(qvk.device, &createInfo, NULL, &accel_top_khr[idx]);
		}

		// Update build information
		buildInfo.dstAccelerationStructure = accel_top_khr[idx];
		buildInfo.scratchData.deviceAddress = buf_accel_scratch.address;
		assert(buf_accel_scratch.address);

		VkAccelerationStructureBuildRangeInfoKHR offset = { .primitiveCount = num_instances };

		VkAccelerationStructureBuildRangeInfoKHR* offsets = &offset;

		qvkCmdBuildAccelerationStructuresKHR(
			cmd_buf,
			1,
			&buildInfo,
			&offsets);
	}
	else // (!qvk.use_khr_ray_tracing)
	{
		VkAccelerationStructureCreateInfoNV accel_create_info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV,
			.info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV,
			.instanceCount = num_instances,
			.geometryCount = 0,
			.pGeometries = NULL,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV
		}
		};

		if (accel_top_match[idx].instanceCount < accel_create_info.info.instanceCount) 
		{
			vkpt_pt_destroy_toplevel(idx);

			qvkCreateAccelerationStructureNV(qvk.device, &accel_create_info, NULL, accel_top_nv + idx);

			/* XXX: do allocation only once with safety margin */
			VkAccelerationStructureMemoryRequirementsInfoNV mem_req_info = {
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV,
				.accelerationStructure = accel_top_nv[idx],
				.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV
			};
			VkMemoryRequirements2 mem_req = { 0 };
			mem_req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
			qvkGetAccelerationStructureMemoryRequirementsNV(qvk.device, &mem_req_info, &mem_req);

			buffer_create(&mem_accel_top[idx], mem_req.memoryRequirements.size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VkBindAccelerationStructureMemoryInfoNV bind_info = {
				.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV,
				.accelerationStructure = accel_top_nv[idx],
				.memory = mem_accel_top[idx].memory,
			};

			_VK(qvkBindAccelerationStructureMemoryNV(qvk.device, 1, &bind_info));

			assert(get_scratch_buffer_size_nv(accel_top_nv[idx]) < SIZE_SCRATCH_BUFFER);

			accel_top_match[idx].instanceCount = accel_create_info.info.instanceCount;
		}

		VkAccelerationStructureInfoNV as_info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV,
			.geometryCount = 0,
			.pGeometries = NULL,
			.instanceCount = num_instances,
		};

		// Request the amount of scratch memory, just to make the validation layer happy.
		// Our static scratch buffer is definitely big enough.

		VkAccelerationStructureMemoryRequirementsInfoNV mem_req_info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV,
			.accelerationStructure = accel_top_nv[idx],
			.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV
		};

		VkMemoryRequirements2 mem_req = { .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };

		qvkGetAccelerationStructureMemoryRequirementsNV(qvk.device, &mem_req_info, &mem_req);

		assert(mem_req.memoryRequirements.size <= SIZE_SCRATCH_BUFFER);

		// Build the TLAS

		qvkCmdBuildAccelerationStructureNV(
			cmd_buf,
			&as_info,
			buf_instances[idx].buffer, /* instance buffer */
			0 /* instance offset */,
			VK_FALSE,  /* update */
			accel_top_nv[idx],
			VK_NULL_HANDLE, /* source acceleration structure */
			buf_accel_scratch.buffer,
			0 /* scratch offset */);

	}

	MEM_BARRIER_BUILD_ACCEL(cmd_buf); /* probably not needed here but doesn't matter */

	return VK_SUCCESS;
}

#define BARRIER_COMPUTE(cmd_buf, img) \
	do { \
		VkImageSubresourceRange subresource_range = { \
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT, \
			.baseMipLevel   = 0, \
			.levelCount     = 1, \
			.baseArrayLayer = 0, \
			.layerCount     = 1 \
		}; \
		IMAGE_BARRIER(cmd_buf, \
				.image            = img, \
				.subresourceRange = subresource_range, \
				.srcAccessMask    = VK_ACCESS_SHADER_WRITE_BIT, \
				.dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT, \
				.oldLayout        = VK_IMAGE_LAYOUT_GENERAL, \
				.newLayout        = VK_IMAGE_LAYOUT_GENERAL, \
		); \
	} while(0)

static void setup_rt_pipeline(VkCommandBuffer cmd_buf, VkPipelineBindPoint bind_point, pipeline_index_t index)
{
	vkCmdBindPipeline(cmd_buf, bind_point, rt_pipelines[index]);

	vkCmdBindDescriptorSets(cmd_buf, bind_point,
		rt_pipeline_layout, 0, 1, rt_descriptor_set + qvk.current_frame_index, 0, 0);

	vkCmdBindDescriptorSets(cmd_buf, bind_point,
		rt_pipeline_layout, 1, 1, &qvk.desc_set_ubo, 0, 0);

	VkDescriptorSet desc_set_textures = qvk_get_current_desc_set_textures();
	vkCmdBindDescriptorSets(cmd_buf, bind_point,
		rt_pipeline_layout, 2, 1, &desc_set_textures, 0, 0);

	vkCmdBindDescriptorSets(cmd_buf, bind_point,
		rt_pipeline_layout, 3, 1, &qvk.desc_set_vertex_buffer, 0, 0);
}

static void
dispatch_rays(VkCommandBuffer cmd_buf, pipeline_index_t pipeline_index, pt_push_constants_t push, uint32_t width, uint32_t height, uint32_t depth)
{
	if (qvk.use_ray_query)
	{
		setup_rt_pipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_index);

		vkCmdPushConstants(cmd_buf, rt_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);

		vkCmdDispatch(cmd_buf, (width + 7) / 8, (height + 7) / 8, depth);
	}
	else
	{
		setup_rt_pipeline(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline_index);

		vkCmdPushConstants(cmd_buf, rt_pipeline_layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(push), &push);

		uint32_t sbt_offset = SBT_ENTRIES_PER_PIPELINE * pipeline_index * shaderGroupBaseAlignment;

		if (qvk.use_khr_ray_tracing)
		{
			assert(buf_shader_binding_table.address);

			VkStridedDeviceAddressRegionKHR raygen = {
				.deviceAddress = buf_shader_binding_table.address + sbt_offset,
				.stride = shaderGroupBaseAlignment,
				.size = shaderGroupBaseAlignment
			};

			VkStridedDeviceAddressRegionKHR miss_and_hit = {
				.deviceAddress = buf_shader_binding_table.address + sbt_offset,
				.stride = shaderGroupBaseAlignment,
				.size = (VkDeviceSize)shaderGroupBaseAlignment * SBT_ENTRIES_PER_PIPELINE
			};

			VkStridedDeviceAddressRegionKHR callable = {
				.deviceAddress = VK_NULL_HANDLE,
				.stride = 0,
				.size = 0
			};

			qvkCmdTraceRaysKHR(cmd_buf,
				&raygen,
				&miss_and_hit,
				&miss_and_hit,
				&callable,
				width, height, depth);
		}
		else // (!qvk.use_khr_ray_tracing)
		{
			qvkCmdTraceRaysNV(cmd_buf,
				buf_shader_binding_table.buffer, sbt_offset,
				buf_shader_binding_table.buffer, sbt_offset, shaderGroupBaseAlignment,
				buf_shader_binding_table.buffer, sbt_offset, shaderGroupBaseAlignment,
				VK_NULL_HANDLE, 0, 0,
				width, height, depth);
		}
	}
}

VkResult
vkpt_pt_trace_primary_rays(VkCommandBuffer cmd_buf)
{
	int frame_idx = qvk.frame_counter & 1;
	
	BUFFER_BARRIER(cmd_buf,
			.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.buffer = qvk.buf_readback.buffer,
			.offset = 0,
			.size = VK_WHOLE_SIZE,
	);

	BEGIN_PERF_MARKER(cmd_buf, PROFILER_PRIMARY_RAYS);

	for(int i = 0; i < qvk.device_count; i++)
	{
		set_current_gpu(cmd_buf, i);

		pt_push_constants_t push;
		push.gpu_index = qvk.device_count == 1 ? -1 : i;
		push.bounce = 0;

		dispatch_rays(cmd_buf, PIPELINE_PRIMARY_RAYS, push, qvk.extent_render.width / 2, qvk.extent_render.height, qvk.device_count == 1 ? 2 : 1);
	}

	set_current_gpu(cmd_buf, ALL_GPUS);

	END_PERF_MARKER(cmd_buf, PROFILER_PRIMARY_RAYS);

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_VISBUF_A + frame_idx]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_TRANSPARENT]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_MOTION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_SHADING_POSITION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_VIEW_DIRECTION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_THROUGHPUT]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_BOUNCE_THROUGHPUT]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_GODRAYS_THROUGHPUT_DIST]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_ALBEDO]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_METALLIC_A + frame_idx]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_CLUSTER_A + frame_idx]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_VIEW_DEPTH_A + frame_idx]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_NORMAL_A + frame_idx]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_RNG_SEED_A + frame_idx]);

	return VK_SUCCESS;
}

VkResult
vkpt_pt_trace_reflections(VkCommandBuffer cmd_buf, int bounce)
{
	int frame_idx = qvk.frame_counter & 1;

	for (int i = 0; i < qvk.device_count; i++)
    {
        set_current_gpu(cmd_buf, i);

        pipeline_index_t pipeline = (bounce == 0) ? PIPELINE_REFLECT_REFRACT_1 : PIPELINE_REFLECT_REFRACT_2;

        pt_push_constants_t push;
        push.gpu_index = qvk.device_count == 1 ? -1 : i;
        push.bounce = bounce;

        dispatch_rays(cmd_buf, pipeline, push, qvk.extent_render.width / 2, qvk.extent_render.height, qvk.device_count == 1 ? 2 : 1);
	}

	set_current_gpu(cmd_buf, ALL_GPUS);

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_TRANSPARENT]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_MOTION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_SHADING_POSITION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_VIEW_DIRECTION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_THROUGHPUT]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_GODRAYS_THROUGHPUT_DIST]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_ALBEDO]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_METALLIC_A + frame_idx]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_CLUSTER_A + frame_idx]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_VIEW_DEPTH_A + frame_idx]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_NORMAL_A + frame_idx]);

	return VK_SUCCESS;
}

VkResult
vkpt_pt_trace_lighting(VkCommandBuffer cmd_buf, float num_bounce_rays)
{
	int frame_idx = qvk.frame_counter & 1;

	BEGIN_PERF_MARKER(cmd_buf, PROFILER_DIRECT_LIGHTING);

	for (int i = 0; i < qvk.device_count; i++)
	{
		set_current_gpu(cmd_buf, i);

		pipeline_index_t pipeline = (cvar_pt_caustics->value != 0) ? PIPELINE_DIRECT_LIGHTING_CAUSTICS : PIPELINE_DIRECT_LIGHTING;

		pt_push_constants_t push;
		push.gpu_index = qvk.device_count == 1 ? -1 : i;
		push.bounce = 0;

		dispatch_rays(cmd_buf, pipeline, push, qvk.extent_render.width / 2, qvk.extent_render.height, qvk.device_count == 1 ? 2 : 1);
	}

	set_current_gpu(cmd_buf, ALL_GPUS);

	END_PERF_MARKER(cmd_buf, PROFILER_DIRECT_LIGHTING);

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_COLOR_LF_SH]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_COLOR_LF_COCG]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_COLOR_HF]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_COLOR_SPEC]);

	BUFFER_BARRIER(cmd_buf,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.buffer = qvk.buf_readback.buffer,
		.offset = 0,
		.size = VK_WHOLE_SIZE,
	);

	BEGIN_PERF_MARKER(cmd_buf, PROFILER_INDIRECT_LIGHTING);

	if (num_bounce_rays > 0)
	{
		for (int i = 0; i < qvk.device_count; i++)
		{
			set_current_gpu(cmd_buf, i);

            int height;
            if (num_bounce_rays == 0.5f)
                height = qvk.extent_render.height / 2;
            else
                height = qvk.extent_render.height;

			for (int bounce_ray = 0; bounce_ray < (int)ceilf(num_bounce_rays); bounce_ray++)
            {
                pipeline_index_t pipeline = (bounce_ray == 0) ? PIPELINE_INDIRECT_LIGHTING_FIRST : PIPELINE_INDIRECT_LIGHTING_SECOND;

                pt_push_constants_t push;
                push.gpu_index = qvk.device_count == 1 ? -1 : i;
                push.bounce = 0;

                dispatch_rays(cmd_buf, pipeline, push, qvk.extent_render.width / 2, height, qvk.device_count == 1 ? 2 : 1);

				BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_COLOR_LF_SH]);
				BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_COLOR_LF_COCG]);
				BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_COLOR_HF]);
				BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_COLOR_SPEC]);
				BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_BOUNCE_THROUGHPUT]);
			}
		}
	}

	END_PERF_MARKER(cmd_buf, PROFILER_INDIRECT_LIGHTING);

	set_current_gpu(cmd_buf, ALL_GPUS);

	return VK_SUCCESS;
}

VkResult
vkpt_pt_destroy()
{
	for(int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vkpt_pt_destroy_toplevel(i);
		buffer_destroy(buf_instances + i);
		vkpt_pt_destroy_dynamic(i);
	}

	vkpt_pt_destroy_static();
	buffer_destroy(&buf_accel_scratch);
	vkDestroyDescriptorSetLayout(qvk.device, rt_descriptor_set_layout, NULL);
	vkDestroyPipelineLayout(qvk.device, rt_pipeline_layout, NULL);
	vkDestroyDescriptorPool(qvk.device, rt_descriptor_pool, NULL);

	return VK_SUCCESS;
}

VkResult
vkpt_pt_create_pipelines()
{
	VkSpecializationMapEntry specEntry = {
		.constantID = 0,
		.offset = 0,
		.size = sizeof(uint32_t),
	};

	uint32_t numbers[2] = { 0, 1 };

	VkSpecializationInfo specInfo[2] = {
		{
			.mapEntryCount = 1,
			.pMapEntries = &specEntry,
			.dataSize = sizeof(uint32_t),
			.pData = &numbers[0],
		},
		{
			.mapEntryCount = 1,
			.pMapEntries = &specEntry,
			.dataSize = sizeof(uint32_t),
			.pData = &numbers[1],
		}
	};

	uint32_t num_shader_groups = SBT_ENTRIES_PER_PIPELINE * PIPELINE_COUNT;
	char* shader_handles = alloca(num_shader_groups * shaderGroupHandleSize);

	VkPipelineShaderStageCreateInfo shader_stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
			.pName = "main"
		},
		SHADER_STAGE(QVK_MOD_PATH_TRACER_RMISS,               VK_SHADER_STAGE_MISS_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_SHADOW_RMISS,        VK_SHADER_STAGE_MISS_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_RCHIT,               VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_PARTICLE_RAHIT,      VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_BEAM_RAHIT,          VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_EXPLOSION_RAHIT,     VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_SPRITE_RAHIT,        VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
	};

	for (pipeline_index_t index = 0; index < PIPELINE_COUNT; index++)
	{
		switch (index)
		{
		case PIPELINE_PRIMARY_RAYS:
			shader_stages[0].module = qvk.shader_modules[QVK_MOD_PRIMARY_RAYS_RGEN];
			shader_stages[0].pSpecializationInfo = NULL;
			break;
		case PIPELINE_REFLECT_REFRACT_1:
			shader_stages[0].module = qvk.shader_modules[QVK_MOD_REFLECT_REFRACT_RGEN];
			shader_stages[0].pSpecializationInfo = &specInfo[0];
			break;
		case PIPELINE_REFLECT_REFRACT_2:
			shader_stages[0].module = qvk.shader_modules[QVK_MOD_REFLECT_REFRACT_RGEN];
			shader_stages[0].pSpecializationInfo = &specInfo[1];
			break;
		case PIPELINE_DIRECT_LIGHTING:
			shader_stages[0].module = qvk.shader_modules[QVK_MOD_DIRECT_LIGHTING_RGEN];
			shader_stages[0].pSpecializationInfo = &specInfo[0];
			break;
		case PIPELINE_DIRECT_LIGHTING_CAUSTICS:
			shader_stages[0].module = qvk.shader_modules[QVK_MOD_DIRECT_LIGHTING_RGEN];
			shader_stages[0].pSpecializationInfo = &specInfo[1];
			break;
		case PIPELINE_INDIRECT_LIGHTING_FIRST:
			shader_stages[0].module = qvk.shader_modules[QVK_MOD_INDIRECT_LIGHTING_RGEN];
			shader_stages[0].pSpecializationInfo = &specInfo[0];
			break;
		case PIPELINE_INDIRECT_LIGHTING_SECOND:
			shader_stages[0].module = qvk.shader_modules[QVK_MOD_INDIRECT_LIGHTING_RGEN];
			shader_stages[0].pSpecializationInfo = &specInfo[1];
			break;
		default:
			assert(!"invalid pipeline index");
			break;
		}

		if (qvk.use_ray_query)
		{
			VkComputePipelineCreateInfo compute_pipeline_info = {
				.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
				.layout = rt_pipeline_layout,
				.stage = {
					.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
					.stage = VK_SHADER_STAGE_COMPUTE_BIT,
					.pName = "main",
					.module = shader_stages[0].module,
					.pSpecializationInfo = shader_stages[0].pSpecializationInfo
				}
			};

			VkResult res = vkCreateComputePipelines(qvk.device, 0, 1, &compute_pipeline_info, NULL, &rt_pipelines[index]);

			if (res != VK_SUCCESS)
			{
				Com_EPrintf("Failed to create ray tracing compute pipeline #%d, vkCreateComputePipelines error code is %s\n", index, qvk_result_to_string(res));
				return res;
			}
		}
		else if (qvk.use_khr_ray_tracing)
		{
			VkRayTracingShaderGroupCreateInfoKHR rt_shader_group_info[] = {
				[SBT_RGEN] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
					.generalShader      = 0,
					.closestHitShader   = VK_SHADER_UNUSED_KHR,
					.anyHitShader       = VK_SHADER_UNUSED_KHR,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				},
				[SBT_RMISS_PATH_TRACER] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
					.generalShader      = 1,
					.closestHitShader   = VK_SHADER_UNUSED_KHR,
					.anyHitShader       = VK_SHADER_UNUSED_KHR,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				},
				[SBT_RMISS_SHADOW] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
					.generalShader      = 2,
					.closestHitShader   = VK_SHADER_UNUSED_KHR,
					.anyHitShader       = VK_SHADER_UNUSED_KHR,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				},
				[SBT_RCHIT_OPAQUE] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
					.generalShader      = VK_SHADER_UNUSED_KHR,
					.closestHitShader   = 3,
					.anyHitShader       = VK_SHADER_UNUSED_KHR,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				},
				[SBT_RAHIT_PARTICLE] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
					.generalShader      = VK_SHADER_UNUSED_KHR,
					.closestHitShader   = VK_SHADER_UNUSED_KHR,
					.anyHitShader       = 4,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				},
				[SBT_RAHIT_BEAM] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
					.generalShader      = VK_SHADER_UNUSED_KHR,
					.closestHitShader   = VK_SHADER_UNUSED_KHR,
					.anyHitShader       = 5,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				},
				[SBT_RAHIT_EXPLOSION] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
					.generalShader      = VK_SHADER_UNUSED_KHR,
					.closestHitShader   = VK_SHADER_UNUSED_KHR,
					.anyHitShader       = 6,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				},
				[SBT_RAHIT_SPRITE] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
					.generalShader      = VK_SHADER_UNUSED_KHR,
					.closestHitShader   = VK_SHADER_UNUSED_KHR,
					.anyHitShader       = 7,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				},
				[SBT_RCHIT_EMPTY] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
					.generalShader      = VK_SHADER_UNUSED_KHR,
					.closestHitShader   = VK_SHADER_UNUSED_KHR,
					.anyHitShader       = VK_SHADER_UNUSED_KHR,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				},
			};

			VkPipelineLibraryCreateInfoKHR library_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR };
			VkRayTracingPipelineCreateInfoKHR rt_pipeline_info = {
				.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
				.pNext = NULL,
				.flags = 0,
				.stageCount = LENGTH(shader_stages),
				.pStages = shader_stages,
				.groupCount = LENGTH(rt_shader_group_info),
				.pGroups = rt_shader_group_info,
				.maxPipelineRayRecursionDepth = 1,
				.pLibraryInfo = &library_info,
				.pLibraryInterface = NULL,
				.pDynamicState = NULL,
				.layout = rt_pipeline_layout,
				.basePipelineHandle = VK_NULL_HANDLE,
				.basePipelineIndex = 0
			};

			assert(LENGTH(rt_shader_group_info) == SBT_ENTRIES_PER_PIPELINE);

			VkResult res = qvkCreateRayTracingPipelinesKHR(qvk.device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rt_pipeline_info, NULL, &rt_pipelines[index]);
			
			if (res != VK_SUCCESS)
			{
				Com_EPrintf("Failed to create ray tracing pipeline #%d, vkCreateRayTracingPipelinesKHR error code is %s\n", index, qvk_result_to_string(res));
				return res;
			}

			_VK(qvkGetRayTracingShaderGroupHandlesKHR(
				qvk.device, rt_pipelines[index], 0, SBT_ENTRIES_PER_PIPELINE,
				/* dataSize = */ SBT_ENTRIES_PER_PIPELINE * shaderGroupHandleSize, 
				/* pData = */ shader_handles + SBT_ENTRIES_PER_PIPELINE * shaderGroupHandleSize * index));
		}
		else // (!qvk.use_khr_ray_tracing)
		{
			VkRayTracingShaderGroupCreateInfoNV rt_shader_group_info[] = 
			{
				[SBT_RGEN] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
					.generalShader      = 0,
					.closestHitShader   = VK_SHADER_UNUSED_NV,
					.anyHitShader       = VK_SHADER_UNUSED_NV,
					.intersectionShader = VK_SHADER_UNUSED_NV
				},
				[SBT_RMISS_PATH_TRACER] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
					.generalShader      = 1,
					.closestHitShader   = VK_SHADER_UNUSED_NV,
					.anyHitShader       = VK_SHADER_UNUSED_NV,
					.intersectionShader = VK_SHADER_UNUSED_NV
				},
				[SBT_RMISS_SHADOW] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
					.generalShader      = 2,
					.closestHitShader   = VK_SHADER_UNUSED_NV,
					.anyHitShader       = VK_SHADER_UNUSED_NV,
					.intersectionShader = VK_SHADER_UNUSED_NV
				},
				[SBT_RCHIT_OPAQUE] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV,
					.generalShader      = VK_SHADER_UNUSED_NV,
					.closestHitShader   = 3,
					.anyHitShader       = VK_SHADER_UNUSED_NV,
					.intersectionShader = VK_SHADER_UNUSED_NV
				},
				[SBT_RAHIT_PARTICLE] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV,
					.generalShader      = VK_SHADER_UNUSED_NV,
					.closestHitShader   = VK_SHADER_UNUSED_NV,
					.anyHitShader       = 4,
					.intersectionShader = VK_SHADER_UNUSED_NV
				},
				[SBT_RAHIT_BEAM] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV,
					.generalShader      = VK_SHADER_UNUSED_NV,
					.closestHitShader   = VK_SHADER_UNUSED_NV,
					.anyHitShader       = 5,
					.intersectionShader = VK_SHADER_UNUSED_NV
				},
				[SBT_RAHIT_EXPLOSION] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV,
					.generalShader      = VK_SHADER_UNUSED_NV,
					.closestHitShader   = VK_SHADER_UNUSED_NV,
					.anyHitShader       = 6,
					.intersectionShader = VK_SHADER_UNUSED_NV
				},
				[SBT_RAHIT_SPRITE] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV,
					.generalShader      = VK_SHADER_UNUSED_NV,
					.closestHitShader   = VK_SHADER_UNUSED_NV,
					.anyHitShader       = 7,
					.intersectionShader = VK_SHADER_UNUSED_NV
				},
				[SBT_RCHIT_EMPTY] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV,
					.generalShader      = VK_SHADER_UNUSED_NV,
					.closestHitShader   = VK_SHADER_UNUSED_NV,
					.anyHitShader       = VK_SHADER_UNUSED_NV,
					.intersectionShader = VK_SHADER_UNUSED_NV
				},
			};

			VkRayTracingPipelineCreateInfoNV rt_pipeline_info = {
				.sType             = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV,
				.stageCount        = LENGTH(shader_stages),
				.pStages           = shader_stages,
				.groupCount        = LENGTH(rt_shader_group_info),
				.pGroups           = rt_shader_group_info,
				.layout            = rt_pipeline_layout,
				.maxRecursionDepth = 1,
			};

			assert(LENGTH(rt_shader_group_info) == SBT_ENTRIES_PER_PIPELINE);

			VkResult res = qvkCreateRayTracingPipelinesNV(qvk.device, NULL, 1, &rt_pipeline_info, NULL, &rt_pipelines[index]);

			if (res != VK_SUCCESS)
			{
				Com_EPrintf("Failed to create ray tracing pipeline #%d, vkCreateRayTracingPipelinesNV error code is %s\n", index, qvk_result_to_string(res));
				return res;
			}

			_VK(qvkGetRayTracingShaderGroupHandlesNV(
				qvk.device, rt_pipelines[index], 0, SBT_ENTRIES_PER_PIPELINE,
				/* dataSize = */ SBT_ENTRIES_PER_PIPELINE* shaderGroupHandleSize,
				/* pData = */ shader_handles + SBT_ENTRIES_PER_PIPELINE * shaderGroupHandleSize * index));
		}
	}

	if (qvk.use_ray_query)
	{
		// No SBT in RQ mode, just return
		return VK_SUCCESS;
	}

	// create the SBT buffer
	uint32_t shader_binding_table_size = num_shader_groups * shaderGroupBaseAlignment;
	_VK(buffer_create(&buf_shader_binding_table, shader_binding_table_size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));

	// copy/unpack the shader handles into the SBT:
	// shaderGroupBaseAlignment is likely greater than shaderGroupHandleSize (64 vs 32 on NV)
	char* shader_binding_table = (char*)buffer_map(&buf_shader_binding_table);
	for (uint32_t group = 0; group < num_shader_groups; group++)
	{
		memcpy(
			shader_binding_table + group * shaderGroupBaseAlignment,
			shader_handles + group * shaderGroupHandleSize,
			shaderGroupHandleSize);
	}
	buffer_unmap(&buf_shader_binding_table);
	shader_binding_table = NULL;

	return VK_SUCCESS;
}

VkResult
vkpt_pt_destroy_pipelines()
{
	buffer_destroy(&buf_shader_binding_table);

	for (pipeline_index_t index = 0; index < PIPELINE_COUNT; index++)
	{
		vkDestroyPipeline(qvk.device, rt_pipelines[index], NULL);
		rt_pipelines[index] = VK_NULL_HANDLE;
	}

	return VK_SUCCESS;
}
