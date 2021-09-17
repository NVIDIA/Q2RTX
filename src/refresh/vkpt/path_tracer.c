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
#define RAY_GEN_BEAM_INTERSECT_BUFFER_BINDING_IDX 4

#define SIZE_SCRATCH_BUFFER (1 << 25)

#define INSTANCE_MAX_NUM 14

static uint32_t shaderGroupHandleSize = 0;
static uint32_t shaderGroupBaseAlignment = 0;
static uint32_t minAccelerationStructureScratchOffsetAlignment = 0;

typedef struct {
	int fast_build;
	uint32_t vertex_count;
	uint32_t index_count;
	uint32_t aabb_count;
	uint32_t instance_count;
} accel_match_info_t;

typedef struct {
	VkAccelerationStructureKHR accel;
	accel_match_info_t match;
	BufferResource_t mem;
	qboolean present;
} accel_struct_t;

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
static int                        masked_primitive_offset = 0;
static int                        sky_primitive_offset = 0;
static int                        custom_sky_primitive_offset = 0;
static int                        transparent_model_primitive_offset = 0;
static int                        masked_model_primitive_offset = 0;
static int                        viewer_model_primitive_offset = 0;
static int                        viewer_weapon_primitive_offset = 0;
static int                        explosions_primitive_offset = 0;
static accel_struct_t             blas_static;
static accel_struct_t             blas_transparent;
static accel_struct_t             blas_masked;
static accel_struct_t             blas_sky;
static accel_struct_t             blas_custom_sky;
static accel_struct_t             blas_dynamic[MAX_FRAMES_IN_FLIGHT];
static accel_struct_t             blas_transparent_models[MAX_FRAMES_IN_FLIGHT];
static accel_struct_t             blas_masked_models[MAX_FRAMES_IN_FLIGHT];
static accel_struct_t             blas_viewer_models[MAX_FRAMES_IN_FLIGHT];
static accel_struct_t             blas_viewer_weapon[MAX_FRAMES_IN_FLIGHT];
static accel_struct_t             blas_explosions[MAX_FRAMES_IN_FLIGHT];
static accel_struct_t             blas_particles[MAX_FRAMES_IN_FLIGHT];
static accel_struct_t             blas_beams[MAX_FRAMES_IN_FLIGHT];
static accel_struct_t             blas_sprites[MAX_FRAMES_IN_FLIGHT];

static accel_struct_t             tlas_geometry[MAX_FRAMES_IN_FLIGHT];
static accel_struct_t             tlas_effects[MAX_FRAMES_IN_FLIGHT];

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
	VkDeviceAddress acceleration_structure;
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
	VkPhysicalDeviceAccelerationStructurePropertiesKHR accel_struct_properties = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR,
		.pNext = NULL
	};

	VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_pipeline_properties = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
		.pNext = &accel_struct_properties
	};

	VkPhysicalDeviceProperties2 dev_props2 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext = &ray_pipeline_properties,
	};

	vkGetPhysicalDeviceProperties2(qvk.physical_device, &dev_props2);

	shaderGroupBaseAlignment = ray_pipeline_properties.shaderGroupBaseAlignment;
	shaderGroupHandleSize = ray_pipeline_properties.shaderGroupHandleSize;
	minAccelerationStructureScratchOffsetAlignment = accel_struct_properties.minAccelerationStructureScratchOffsetAlignment;

	buffer_create(&buf_accel_scratch, SIZE_SCRATCH_BUFFER, 
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	for(int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		buffer_create(buf_instances + i, INSTANCE_MAX_NUM * sizeof(QvkGeometryInstance_t), 
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}

	/* create descriptor set layout */
	VkDescriptorSetLayoutBinding bindings[] = {
		{
			.binding         = RAY_GEN_ACCEL_STRUCTURE_BINDING_IDX,
			.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
			.descriptorCount = TLAS_COUNT,
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
		{
			.binding         = RAY_GEN_BEAM_INTERSECT_BUFFER_BINDING_IDX,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
			.descriptorCount = 1,
			.stageFlags      = qvk.use_ray_query ? VK_SHADER_STAGE_COMPUTE_BIT : VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
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
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, MAX_FRAMES_IN_FLIGHT }
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
	const VkAccelerationStructureKHR tlas[TLAS_COUNT] = {
		[TLAS_INDEX_GEOMETRY] = tlas_geometry[idx].accel,
		[TLAS_INDEX_EFFECTS] = tlas_effects[idx].accel
	};
	
	VkWriteDescriptorSetAccelerationStructureKHR desc_accel_struct = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = TLAS_COUNT,
		.pAccelerationStructures = tlas
	};
	
	VkBufferView particle_color_buffer_view = get_transparency_particle_color_buffer_view();
	VkBufferView beam_color_buffer_view = get_transparency_beam_color_buffer_view();
	VkBufferView sprite_info_buffer_view = get_transparency_sprite_info_buffer_view();
	VkBufferView beam_intersect_buffer_view = get_transparency_beam_intersect_buffer_view();

	VkWriteDescriptorSet writes[] = {
		{
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext           = (const void*)&desc_accel_struct,
			.dstSet          = rt_descriptor_set[idx],
			.dstBinding      = RAY_GEN_ACCEL_STRUCTURE_BINDING_IDX,
			.descriptorCount = TLAS_COUNT,
			.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
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
		{
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = rt_descriptor_set[idx],
			.dstBinding      = RAY_GEN_BEAM_INTERSECT_BUFFER_BINDING_IDX,
			.descriptorCount = 1,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
			.pTexelBufferView = &beam_intersect_buffer_view
		},
	};

	vkUpdateDescriptorSets(qvk.device, LENGTH(writes), writes, 0, NULL);

	return VK_SUCCESS;
}

static void destroy_accel_struct(accel_struct_t* blas)
{
	buffer_destroy(&blas->mem);

	if (blas->accel)
	{
		qvkDestroyAccelerationStructureKHR(qvk.device, blas->accel, NULL);
		blas->accel = VK_NULL_HANDLE;
	}
	
	blas->match.fast_build = 0;
	blas->match.index_count = 0;
	blas->match.vertex_count = 0;
	blas->match.aabb_count = 0;
	blas->match.instance_count = 0;
}

void vkpt_pt_destroy_static()
{
	destroy_accel_struct(&blas_static);
	destroy_accel_struct(&blas_transparent);
	destroy_accel_struct(&blas_masked);
	destroy_accel_struct(&blas_sky);
	destroy_accel_struct(&blas_custom_sky);
}

static void vkpt_pt_destroy_dynamic(int idx)
{
	destroy_accel_struct(&blas_dynamic[idx]);
	destroy_accel_struct(&blas_transparent_models[idx]);
	destroy_accel_struct(&blas_masked_models[idx]);
	destroy_accel_struct(&blas_viewer_models[idx]);
	destroy_accel_struct(&blas_viewer_weapon[idx]);
	destroy_accel_struct(&blas_explosions[idx]);
	destroy_accel_struct(&blas_particles[idx]);
	destroy_accel_struct(&blas_beams[idx]);
	destroy_accel_struct(&blas_sprites[idx]);
}

static inline int accel_matches(accel_match_info_t *match,
								int fast_build,
								uint32_t vertex_count,
								uint32_t index_count) {
	return match->fast_build == fast_build &&
		   match->vertex_count >= vertex_count &&
		   match->index_count >= index_count;
}

static inline int accel_matches_aabb(accel_match_info_t *match,
								int fast_build,
								uint32_t aabb_count) {
	return match->fast_build == fast_build &&
		   match->aabb_count >= aabb_count;
}

static inline int accel_matches_top_level(accel_match_info_t *match,
								int fast_build,
								uint32_t instance_count) {
	return match->fast_build == fast_build &&
		   match->instance_count >= instance_count;
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
	accel_struct_t* blas,
	qboolean is_dynamic,
	qboolean fast_build)
{
	assert(blas);

	if (num_vertices == 0)
	{
		blas->present = qfalse;
		return VK_SUCCESS;
	}
	
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

	if (!is_dynamic || !accel_matches(&blas->match, fast_build, num_vertices, num_indices) || blas->accel == VK_NULL_HANDLE)
	{
		doAlloc = 1;
		doFree = (blas->accel != VK_NULL_HANDLE);
	}

	if (doFree)
	{
		destroy_accel_struct(blas);
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
		qvkCreateAccelerationStructureKHR(qvk.device, &createInfo, NULL, &blas->accel);

		blas->match.fast_build = fast_build;
		blas->match.vertex_count = num_vertices_to_allocate;
		blas->match.index_count = num_indices_to_allocate;
		blas->match.aabb_count = 0;
		blas->match.instance_count = 0;
	}

	// set where the build lands
	buildInfo.dstAccelerationStructure = blas->accel;

	// Use shared scratch buffer for holding the temporary data of the acceleration structure builder
	buildInfo.scratchData.deviceAddress = buf_accel_scratch.address + scratch_buf_ptr;
	assert(buf_accel_scratch.address);

	// Update the scratch buffer ptr
	scratch_buf_ptr += sizeInfo.buildScratchSize;
	scratch_buf_ptr = align(scratch_buf_ptr, minAccelerationStructureScratchOffsetAlignment);
	assert(scratch_buf_ptr < SIZE_SCRATCH_BUFFER);

	// build offset
	VkAccelerationStructureBuildRangeInfoKHR offset = { .primitiveCount = max(num_vertices, num_indices) / 3 };
	const VkAccelerationStructureBuildRangeInfoKHR* offsets = &offset;

	qvkCmdBuildAccelerationStructuresKHR(cmd_buf, 1, &buildInfo, &offsets);

	blas->present = qtrue;

	return VK_SUCCESS;
}

static VkResult
vkpt_pt_create_accel_bottom_aabb(
	VkCommandBuffer cmd_buf,
	BufferResource_t* buffer_aabb,
	VkDeviceAddress offset_aabb,
	int num_aabbs,
	accel_struct_t* blas,
	qboolean is_dynamic,
	qboolean fast_build)
{
	assert(blas);

	if (num_aabbs == 0)
	{
		blas->present = qfalse;
		return VK_SUCCESS;
	}

	assert(buffer_aabb->address);

	const VkAccelerationStructureGeometryAabbsDataKHR aabbs = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR,
		.data = {.deviceAddress = buffer_aabb->address + offset_aabb },
		.stride = sizeof(VkAabbPositionsKHR)
	};

	const VkAccelerationStructureGeometryDataKHR geometry_data = { 
		.aabbs = aabbs
	};

	const VkAccelerationStructureGeometryKHR geometry = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR,
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

	if (!is_dynamic || !accel_matches_aabb(&blas->match, fast_build, num_aabbs) || blas->accel == VK_NULL_HANDLE)
	{
		doAlloc = 1;
		doFree = (blas->accel != VK_NULL_HANDLE);
	}

	if (doFree)
	{
		destroy_accel_struct(blas);
	}

	// Find size to build on the device
	uint32_t max_primitive_count = num_aabbs;
	VkAccelerationStructureBuildSizesInfoKHR sizeInfo = { .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	qvkGetAccelerationStructureBuildSizesKHR(qvk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &max_primitive_count, &sizeInfo);

	if (doAlloc)
	{
		int num_aabs_to_allocate = num_aabbs;

		// Allocate more memory / larger BLAS for dynamic objects
		if (is_dynamic)
		{
			num_aabs_to_allocate *= DYNAMIC_GEOMETRY_BLOAT_FACTOR;

			max_primitive_count = num_aabs_to_allocate;
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
		qvkCreateAccelerationStructureKHR(qvk.device, &createInfo, NULL, &blas->accel);

		blas->match.fast_build = fast_build;
		blas->match.vertex_count = 0;
		blas->match.index_count = 0;
		blas->match.aabb_count = num_aabs_to_allocate;
		blas->match.instance_count = 0;
	}

	// set where the build lands
	buildInfo.dstAccelerationStructure = blas->accel;

	// Use shared scratch buffer for holding the temporary data of the acceleration structure builder
	buildInfo.scratchData.deviceAddress = buf_accel_scratch.address + scratch_buf_ptr;
	assert(buf_accel_scratch.address);

	// Update the scratch buffer ptr
	scratch_buf_ptr += sizeInfo.buildScratchSize;
	scratch_buf_ptr = align(scratch_buf_ptr, minAccelerationStructureScratchOffsetAlignment);
	assert(scratch_buf_ptr < SIZE_SCRATCH_BUFFER);

	// build offset
	VkAccelerationStructureBuildRangeInfoKHR offset = { .primitiveCount = num_aabbs };
	const VkAccelerationStructureBuildRangeInfoKHR* offsets = &offset;

	qvkCmdBuildAccelerationStructuresKHR(cmd_buf, 1, &buildInfo, &offsets);

	blas->present = qtrue;

	return VK_SUCCESS;
}

VkResult
vkpt_pt_create_static(
	int num_vertices, 
	int num_vertices_transparent,
	int num_vertices_masked,
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

	ret = vkpt_pt_create_accel_bottom(cmd_buf, &qvk.buf_vertex_bsp, address_vertex, NULL, 0, num_vertices_masked, 0, &blas_masked, qfalse, qfalse);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	address_vertex += num_vertices_masked * sizeof(float) * 3;
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
	masked_primitive_offset = transparent_primitive_offset + num_vertices_transparent / 3;
	sky_primitive_offset = masked_primitive_offset + num_vertices_masked / 3;
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

	masked_model_primitive_offset = upload_info->masked_model_vertex_offset / 3;
	offset_vertex = offset_vertex_base + upload_info->masked_model_vertex_offset * sizeof(float) * 3;
	vkpt_pt_create_accel_bottom(cmd_buf, &qvk.buf_vertex_model_dynamic, offset_vertex, NULL, offset_index, upload_info->masked_model_vertex_num, 0, blas_masked_models + idx, qtrue, qtrue);

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

	BufferResource_t *buffer_aabb = NULL;
	uint64_t offset_aabb = 0;
	uint32_t num_aabbs = 0;
	vkpt_get_beam_aabb_buffer(&buffer_aabb, &offset_aabb, &num_aabbs);
	vkpt_pt_create_accel_bottom_aabb(cmd_buf, buffer_aabb, offset_aabb, num_aabbs, blas_beams + idx, qtrue, qtrue);
	
	vkpt_get_transparency_buffers(VKPT_TRANSPARENCY_SPRITES, &buffer_vertex, &offset_vertex, &buffer_index, &offset_index, &num_vertices, &num_indices);
	vkpt_pt_create_accel_bottom(cmd_buf, buffer_vertex, offset_vertex, buffer_index, offset_index, num_vertices, num_indices, blas_sprites + idx, qtrue, qtrue);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	scratch_buf_ptr = 0;

	return VK_SUCCESS;
}

static void
append_blas(QvkGeometryInstance_t *instances, uint32_t *num_instances, accel_struct_t* blas, int instance_id, int mask, int flags, int sbt_offset)
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
	
	VkAccelerationStructureDeviceAddressInfoKHR  as_device_address_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = blas->accel,
	};

	instance.acceleration_structure = qvkGetAccelerationStructureDeviceAddressKHR(qvk.device, &as_device_address_info);
	
	assert(*num_instances < INSTANCE_MAX_NUM);
	memcpy(instances + *num_instances, &instance, sizeof(instance));
	++*num_instances;
}

static void
build_tlas(VkCommandBuffer cmd_buf, accel_struct_t* as, VkDeviceAddress instance_data, uint32_t num_instances)
{
	// Build the TLAS
	VkAccelerationStructureGeometryDataKHR geometry = {
		.instances = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
			.data = {.deviceAddress = instance_data}
		}
	};

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

	if (!accel_matches_top_level(&as->match, qtrue, num_instances))
	{
		destroy_accel_struct(as);

		// Create the buffer for the acceleration structure
		buffer_create(&as->mem, sizeInfo.accelerationStructureSize,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		// Create TLAS
		// Create acceleration structure
		VkAccelerationStructureCreateInfoKHR createInfo = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
			.size = sizeInfo.accelerationStructureSize,
			.buffer = as->mem.buffer
		};

		// Create the acceleration structure
		qvkCreateAccelerationStructureKHR(qvk.device, &createInfo, NULL, &as->accel);

		as->match.fast_build = qtrue;
		as->match.index_count = 0;
		as->match.vertex_count = 0;
		as->match.aabb_count = 0;
		as->match.instance_count = num_instances;
	}

	// Update build information
	buildInfo.dstAccelerationStructure = as->accel;
	buildInfo.scratchData.deviceAddress = buf_accel_scratch.address + scratch_buf_ptr;
	assert(buf_accel_scratch.address);
	
	// Update the scratch buffer ptr
	scratch_buf_ptr += sizeInfo.buildScratchSize;
	scratch_buf_ptr = align(scratch_buf_ptr, minAccelerationStructureScratchOffsetAlignment);
	assert(scratch_buf_ptr < SIZE_SCRATCH_BUFFER);

	VkAccelerationStructureBuildRangeInfoKHR offset = { .primitiveCount = num_instances };

	const VkAccelerationStructureBuildRangeInfoKHR* offsets = &offset;

	qvkCmdBuildAccelerationStructuresKHR(
		cmd_buf,
		1,
		&buildInfo,
		&offsets);
}

VkResult
vkpt_pt_create_toplevel(VkCommandBuffer cmd_buf, int idx, qboolean include_world, qboolean weapon_left_handed)
{
	QvkGeometryInstance_t instances[INSTANCE_MAX_NUM];
	uint32_t num_instances = 0;

	if (include_world)
	{
		append_blas(instances, &num_instances, &blas_static, 0, AS_FLAG_OPAQUE, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR, SBTO_OPAQUE);
		append_blas(instances, &num_instances, &blas_transparent, transparent_primitive_offset, AS_FLAG_TRANSPARENT, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR, SBTO_OPAQUE);
		append_blas(instances, &num_instances, &blas_masked, masked_primitive_offset, AS_FLAG_OPAQUE, VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR | VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, SBTO_MASKED);
		append_blas(instances, &num_instances, &blas_sky, AS_INSTANCE_FLAG_SKY | sky_primitive_offset, AS_FLAG_SKY, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR, SBTO_OPAQUE);
		append_blas(instances, &num_instances, &blas_custom_sky, AS_INSTANCE_FLAG_SKY | custom_sky_primitive_offset, AS_FLAG_CUSTOM_SKY, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR, SBTO_OPAQUE);
	}

	append_blas(instances, &num_instances, &blas_dynamic[idx], AS_INSTANCE_FLAG_DYNAMIC, AS_FLAG_OPAQUE, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR, SBTO_OPAQUE);
	append_blas(instances, &num_instances, &blas_transparent_models[idx], AS_INSTANCE_FLAG_DYNAMIC | transparent_model_primitive_offset, AS_FLAG_TRANSPARENT, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR, SBTO_OPAQUE);
	append_blas(instances, &num_instances, &blas_masked_models[idx], AS_INSTANCE_FLAG_DYNAMIC | masked_model_primitive_offset, AS_FLAG_OPAQUE, VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR | VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, SBTO_MASKED);
    append_blas(instances, &num_instances, &blas_viewer_weapon[idx], AS_INSTANCE_FLAG_DYNAMIC | viewer_weapon_primitive_offset, AS_FLAG_VIEWER_WEAPON, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR | (weapon_left_handed ? VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR : 0), SBTO_OPAQUE);

	if (cl_player_model->integer == CL_PLAYER_MODEL_FIRST_PERSON)
	{
		append_blas(instances, &num_instances, &blas_viewer_models[idx], AS_INSTANCE_FLAG_DYNAMIC | viewer_model_primitive_offset, AS_FLAG_VIEWER_MODELS, VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR | VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, SBTO_OPAQUE);
	}
	
	uint32_t num_instances_geometry = num_instances;

	append_blas(instances, &num_instances, &blas_explosions[idx], AS_INSTANCE_FLAG_DYNAMIC | explosions_primitive_offset, AS_FLAG_EFFECTS, 0, SBTO_EXPLOSION);
	
	if (cvar_pt_enable_particles->integer != 0)
	{
		append_blas(instances, &num_instances, &blas_particles[idx], 0, AS_FLAG_EFFECTS, VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, SBTO_PARTICLE);
	}

	if (cvar_pt_enable_beams->integer != 0)
	{
		append_blas(instances, &num_instances, &blas_beams[idx], 0, AS_FLAG_EFFECTS, VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, SBTO_BEAM);
	}

	if (cvar_pt_enable_sprites->integer != 0)
	{
		append_blas(instances, &num_instances, &blas_sprites[idx], 0, AS_FLAG_EFFECTS, VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, SBTO_SPRITE);
	}

	uint32_t num_instances_effects = num_instances - num_instances_geometry;
	
	void *instance_data = buffer_map(buf_instances + idx);
	memcpy(instance_data, &instances, sizeof(QvkGeometryInstance_t) * num_instances);

	buffer_unmap(buf_instances + idx);
	instance_data = NULL;

	scratch_buf_ptr = 0;
	build_tlas(cmd_buf, &tlas_geometry[idx], buf_instances[idx].address, num_instances_geometry);
	build_tlas(cmd_buf, &tlas_effects[idx], buf_instances[idx].address + num_instances_geometry * sizeof(QvkGeometryInstance_t), num_instances_effects);

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
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_BASE_COLOR_A + frame_idx]);
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
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_BASE_COLOR_A + frame_idx]);
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
		destroy_accel_struct(tlas_geometry + i);
		destroy_accel_struct(tlas_effects + i);
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
	memset(shader_handles, 0, num_shader_groups * shaderGroupHandleSize);

	VkPipelineShaderStageCreateInfo shader_stages[] = {
		// Stages used by all pipelines. Count must match num_base_shader_stages below!
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
			.pName = "main"
			// Shader module is set below
		},
		SHADER_STAGE(QVK_MOD_PATH_TRACER_RMISS,               VK_SHADER_STAGE_MISS_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_RCHIT,               VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_MASKED_RAHIT,        VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
		// Stages used by all pipelines that consider transparency
		SHADER_STAGE(QVK_MOD_PATH_TRACER_PARTICLE_RAHIT,      VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_EXPLOSION_RAHIT,     VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_SPRITE_RAHIT,        VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
		// Must be last
		SHADER_STAGE(QVK_MOD_PATH_TRACER_BEAM_RAHIT,          VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_BEAM_RINT,           VK_SHADER_STAGE_INTERSECTION_BIT_KHR),
	};
	const unsigned num_base_shader_stages = 5;
	const unsigned num_transparent_no_beam_shader_stages = 8;

	for (pipeline_index_t index = 0; index < PIPELINE_COUNT; index++)
	{
		qboolean needs_beams = index <= PIPELINE_REFLECT_REFRACT_2;
		qboolean needs_transparency = needs_beams || index == PIPELINE_INDIRECT_LIGHTING_FIRST;
		unsigned int num_shader_stages = needs_beams ? LENGTH(shader_stages) : (needs_transparency ? num_transparent_no_beam_shader_stages : num_base_shader_stages);

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
		else
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
				[SBT_RMISS_EMPTY] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
					.generalShader      = 1,
					.closestHitShader   = VK_SHADER_UNUSED_KHR,
					.anyHitShader       = VK_SHADER_UNUSED_KHR,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				},
				[SBT_RCHIT_GEOMETRY] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
					.generalShader      = VK_SHADER_UNUSED_KHR,
					.closestHitShader   = 2,
					.anyHitShader       = VK_SHADER_UNUSED_KHR,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				},
				[SBT_RAHIT_MASKED] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
					.generalShader      = VK_SHADER_UNUSED_KHR,
					.closestHitShader   = 2,
					.anyHitShader       = 3,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				},
				[SBT_RCHIT_EFFECTS] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
					.generalShader      = VK_SHADER_UNUSED_KHR,
					.closestHitShader   = VK_SHADER_UNUSED_KHR,
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
				[SBT_RAHIT_EXPLOSION] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
					.generalShader      = VK_SHADER_UNUSED_KHR,
					.closestHitShader   = VK_SHADER_UNUSED_KHR,
					.anyHitShader       = 5,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				},
				[SBT_RAHIT_SPRITE] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
					.generalShader      = VK_SHADER_UNUSED_KHR,
					.closestHitShader   = VK_SHADER_UNUSED_KHR,
					.anyHitShader       = 6,
					.intersectionShader = VK_SHADER_UNUSED_KHR
				},
				[SBT_RINT_BEAM] = {
					.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
					.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR,
					.generalShader      = VK_SHADER_UNUSED_KHR,
					.closestHitShader   = VK_SHADER_UNUSED_KHR,
					.anyHitShader       = 7,
					.intersectionShader = 8
				}
			};

			unsigned int num_shader_groups = needs_beams ? LENGTH(rt_shader_group_info) : (needs_transparency ? SBT_RINT_BEAM : SBT_FIRST_TRANSPARENCY);

			VkPipelineLibraryCreateInfoKHR library_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR };
			VkRayTracingPipelineCreateInfoKHR rt_pipeline_info = {
				.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
				.pNext = NULL,
				.flags = 0,
				.stageCount = num_shader_stages,
				.pStages = shader_stages,
				.groupCount = num_shader_groups,
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
				qvk.device, rt_pipelines[index], 0, num_shader_groups,
				/* dataSize = */ num_shader_groups * shaderGroupHandleSize,
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
