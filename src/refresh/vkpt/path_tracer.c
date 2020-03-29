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

#define SIZE_SCRATCH_BUFFER (1 << 24)

#define INSTANCE_MAX_NUM 12

static VkPhysicalDeviceRayTracingPropertiesKHR rt_properties = {
	.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_KHR,
	.pNext = NULL,
	.maxRecursionDepth     = 0, /* updated during init */
	.shaderGroupHandleSize = 0
};

typedef struct accel_bottom_match_info_s {
	int fast_build;
	uint32_t vertex_count;
	uint32_t index_count;
} accel_bottom_match_info_t;

typedef struct accel_top_match_info_s {
	uint32_t instanceCount;
} accel_top_match_info_t;

static BufferResource_t           buf_accel_scratch;
static size_t                     scratch_buf_ptr = 0;
static BufferResource_t           buf_instances    [MAX_FRAMES_IN_FLIGHT];
static VkAccelerationStructureKHR accel_static;
static VkAccelerationStructureKHR accel_transparent;
static VkAccelerationStructureKHR accel_sky;
static VkAccelerationStructureKHR accel_custom_sky;
static int                        transparent_primitive_offset = 0;
static int                        sky_primitive_offset = 0;
static int                        custom_sky_primitive_offset = 0;
static int                        transparent_model_primitive_offset = 0;
static int                        transparent_models_present = 0;
static int                        viewer_model_primitive_offset = 0;
static int                        viewer_weapon_primitive_offset = 0;
static int                        explosions_primitive_offset = 0;
static int                        explosions_present = 0;
static VkAccelerationStructureKHR accel_dynamic    [MAX_FRAMES_IN_FLIGHT];
static accel_bottom_match_info_t  accel_dynamic_match[MAX_FRAMES_IN_FLIGHT];
static VkAccelerationStructureKHR accel_transparent_models[MAX_FRAMES_IN_FLIGHT];
static accel_bottom_match_info_t  accel_transparent_models_match[MAX_FRAMES_IN_FLIGHT];
static VkAccelerationStructureKHR accel_viewer_models[MAX_FRAMES_IN_FLIGHT];
static accel_bottom_match_info_t  accel_viewer_models_match[MAX_FRAMES_IN_FLIGHT];
static VkAccelerationStructureKHR accel_viewer_weapon[MAX_FRAMES_IN_FLIGHT];
static accel_bottom_match_info_t  accel_viewer_weapon_match[MAX_FRAMES_IN_FLIGHT];
static VkAccelerationStructureKHR accel_explosions[MAX_FRAMES_IN_FLIGHT];
static accel_bottom_match_info_t  accel_explosions_match[MAX_FRAMES_IN_FLIGHT];
static VkAccelerationStructureKHR accel_top        [MAX_FRAMES_IN_FLIGHT];
static accel_top_match_info_t     accel_top_match[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory             mem_accel_static;
static VkDeviceMemory             mem_accel_transparent;
static VkDeviceMemory             mem_accel_sky;
static VkDeviceMemory             mem_accel_custom_sky;
static VkDeviceMemory             mem_accel_top[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory             mem_accel_dynamic[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory             mem_accel_transparent_models[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory             mem_accel_viewer_models[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory             mem_accel_viewer_weapon[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory             mem_accel_explosions[MAX_FRAMES_IN_FLIGHT];

static BufferResource_t buf_shader_binding_table;

static VkDescriptorPool      rt_descriptor_pool;
static VkDescriptorSet       rt_descriptor_set[MAX_FRAMES_IN_FLIGHT];
static VkDescriptorSetLayout rt_descriptor_set_layout;
static VkPipelineLayout      rt_pipeline_layout;
static VkPipeline            rt_pipeline;

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
    VkDeviceAddress acceleration_structure_address;
} QvkGeometryInstance_t;

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
	VkPhysicalDeviceProperties2 dev_props2 = {
		.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext      = &rt_properties,
	};

	vkGetPhysicalDeviceProperties2(qvk.physical_device, &dev_props2);

	Com_Printf("Maximum recursion depth: %d\n",  rt_properties.maxRecursionDepth);
	Com_Printf("Shader group handle size: %d\n", rt_properties.shaderGroupHandleSize);

	buffer_create(&buf_accel_scratch, SIZE_SCRATCH_BUFFER, VK_BUFFER_USAGE_RAY_TRACING_BIT_KHR,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	for(int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		buffer_create(buf_instances + i, INSTANCE_MAX_NUM * sizeof(QvkGeometryInstance_t), VK_BUFFER_USAGE_RAY_TRACING_BIT_KHR,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}

	/* create descriptor set layout */
	VkDescriptorSetLayoutBinding bindings[] = {
		{
			.binding         = RAY_GEN_ACCEL_STRUCTURE_BINDING_IDX,
			.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
			.descriptorCount = 1,
			.stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
		},
		{
			.binding         = RAY_GEN_PARTICLE_COLOR_BUFFER_BINDING_IDX,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
			.descriptorCount = 1,
			.stageFlags      = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
		},
		{
			.binding         = RAY_GEN_BEAM_COLOR_BUFFER_BINDING_IDX,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
			.descriptorCount = 1,
			.stageFlags      = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
		},
		{
			.binding         = RAY_GEN_SPRITE_INFO_BUFFER_BINDING_IDX,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
			.descriptorCount = 1,
			.stageFlags      = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
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
		.stageFlags		= VK_SHADER_STAGE_RAYGEN_BIT_KHR,
		.offset			= 0,
		.size			= sizeof(int) * 2,
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
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,             MAX_FRAMES_IN_FLIGHT },
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
	VkWriteDescriptorSetAccelerationStructureKHR desc_accel_struct_info = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures    = accel_top + idx
	};

	VkBufferView particle_color_buffer_view = get_transparency_particle_color_buffer_view();
	VkBufferView beam_color_buffer_view = get_transparency_beam_color_buffer_view();
	VkBufferView sprite_info_buffer_view = get_transparency_sprite_info_buffer_view();

	VkWriteDescriptorSet writes[] = {
		{
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext           = &desc_accel_struct_info,
			.dstSet          = rt_descriptor_set[idx],
			.dstBinding      = RAY_GEN_ACCEL_STRUCTURE_BINDING_IDX,
			.descriptorCount = 1,
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
	};

	vkUpdateDescriptorSets(qvk.device, LENGTH(writes), writes, 0, NULL);

	return VK_SUCCESS;
}


static size_t
get_scratch_buffer_size(VkAccelerationStructureKHR ac)
{
	VkAccelerationStructureMemoryRequirementsInfoKHR mem_req_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_KHR,
		.accelerationStructure = ac,
		.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_KHR
	};

	VkMemoryRequirements2 mem_req = { 0 };
	mem_req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
	qvkGetAccelerationStructureMemoryRequirementsKHR(qvk.device, &mem_req_info, &mem_req);

	return mem_req.memoryRequirements.size;
}

static inline VkAccelerationStructureGeometryKHR
get_geometry(VkDeviceAddress address) 
{
	size_t size_per_vertex = sizeof(float) * 3;
	VkAccelerationStructureGeometryKHR geometry = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometry = {
			.triangles = {
				.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
				.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
				.vertexData = { .deviceAddress = address },
				.vertexStride = size_per_vertex,
				.indexType = VK_INDEX_TYPE_NONE_KHR
			}
		}
	};
	return geometry;
}

VkResult
vkpt_pt_destroy_static()
{
	if(mem_accel_static) {
		vkFreeMemory(qvk.device, mem_accel_static, NULL);
		mem_accel_static = VK_NULL_HANDLE;
	}
	if (mem_accel_transparent) {
		vkFreeMemory(qvk.device, mem_accel_transparent, NULL);
		mem_accel_transparent = VK_NULL_HANDLE;
	}
	if (mem_accel_sky) {
		vkFreeMemory(qvk.device, mem_accel_sky, NULL);
		mem_accel_sky = VK_NULL_HANDLE;
	}
	if (mem_accel_custom_sky) {
		vkFreeMemory(qvk.device, mem_accel_custom_sky, NULL);
		mem_accel_custom_sky = VK_NULL_HANDLE;
	}
	if(accel_static) {
		qvkDestroyAccelerationStructureKHR(qvk.device, accel_static, NULL);
		accel_static = VK_NULL_HANDLE;
	}
	if (accel_transparent) {
		qvkDestroyAccelerationStructureKHR(qvk.device, accel_transparent, NULL);
		accel_transparent = VK_NULL_HANDLE;
	}
	if (accel_sky) {
		qvkDestroyAccelerationStructureKHR(qvk.device, accel_sky, NULL);
		accel_sky = VK_NULL_HANDLE;
	}
	if (accel_custom_sky) {
		qvkDestroyAccelerationStructureKHR(qvk.device, accel_custom_sky, NULL);
		accel_custom_sky = VK_NULL_HANDLE;
	}
	return VK_SUCCESS;
}

VkResult
vkpt_pt_destroy_dynamic(int idx)
{
	if(mem_accel_dynamic[idx]) {
		vkFreeMemory(qvk.device, mem_accel_dynamic[idx], NULL);
		mem_accel_dynamic[idx] = VK_NULL_HANDLE;
	}
	if(accel_dynamic[idx]) {
		qvkDestroyAccelerationStructureKHR(qvk.device, accel_dynamic[idx], NULL);
		accel_dynamic[idx] = VK_NULL_HANDLE;
	}
	return VK_SUCCESS;
}

VkResult
vkpt_pt_destroy_transparent_models(int idx)
{
	if (mem_accel_transparent_models[idx]) {
		vkFreeMemory(qvk.device, mem_accel_transparent_models[idx], NULL);
		mem_accel_transparent_models[idx] = VK_NULL_HANDLE;
	}
	if (accel_transparent_models[idx]) {
		qvkDestroyAccelerationStructureKHR(qvk.device, accel_transparent_models[idx], NULL);
		accel_transparent_models[idx] = VK_NULL_HANDLE;
	}
	return VK_SUCCESS;
}

VkResult
vkpt_pt_destroy_viewer_models(int idx)
{
	if(mem_accel_viewer_models[idx]) {
		vkFreeMemory(qvk.device, mem_accel_viewer_models[idx], NULL);
		mem_accel_viewer_models[idx] = VK_NULL_HANDLE;
	}
	if(accel_viewer_models[idx]) {
		qvkDestroyAccelerationStructureKHR(qvk.device, accel_viewer_models[idx], NULL);
		accel_viewer_models[idx] = VK_NULL_HANDLE;
	}
	return VK_SUCCESS;
}

VkResult
vkpt_pt_destroy_viewer_weapon(int idx)
{
	if(mem_accel_viewer_weapon[idx]) {
		vkFreeMemory(qvk.device, mem_accel_viewer_weapon[idx], NULL);
		mem_accel_viewer_weapon[idx] = VK_NULL_HANDLE;
	}
	if(accel_viewer_weapon[idx]) {
		qvkDestroyAccelerationStructureKHR(qvk.device, accel_viewer_weapon[idx], NULL);
		accel_viewer_weapon[idx] = VK_NULL_HANDLE;
	}
	return VK_SUCCESS;
}

VkResult
vkpt_pt_destroy_explosions(int idx)
{
	if (mem_accel_explosions[idx]) {
		vkFreeMemory(qvk.device, mem_accel_explosions[idx], NULL);
		mem_accel_explosions[idx] = VK_NULL_HANDLE;
	}
	if (accel_explosions[idx]) {
		qvkDestroyAccelerationStructureKHR(qvk.device, accel_explosions[idx], NULL);
		accel_explosions[idx] = VK_NULL_HANDLE;
	}
	return VK_SUCCESS;
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
		VkDeviceAddress address,
		int num_vertices,
		VkAccelerationStructureKHR *accel,
		accel_bottom_match_info_t *match,
		VkDeviceMemory *mem_accel,
		VkCommandBuffer cmd_buf,
		int fast_build
		)
{
	assert(accel);
	assert(mem_accel);

	VkAccelerationStructureGeometryKHR geometry = get_geometry(address);

	int doFree = 0;
	int doAlloc = 0;

	if (!match || !accel_matches(match, fast_build, num_vertices, num_vertices) || *accel == VK_NULL_HANDLE) {
		doAlloc = 1;
		doFree = (*accel != VK_NULL_HANDLE);
	}

	if (doFree) {
		if (*mem_accel) {
			vkFreeMemory(qvk.device, *mem_accel, NULL);
			*mem_accel = VK_NULL_HANDLE;
		}
		if (*accel) {
			qvkDestroyAccelerationStructureKHR(qvk.device, *accel, NULL);
			*accel = VK_NULL_HANDLE;
		}
	}

	if (doAlloc) {
		// Only dynamic geometries have match info
		if (match)
			num_vertices *= DYNAMIC_GEOMETRY_BLOAT_FACTOR;

		VkAccelerationStructureCreateGeometryTypeInfoKHR geometry_create = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR,
			.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
			.maxPrimitiveCount = num_vertices / 3,
			.indexType = VK_INDEX_TYPE_NONE_KHR,
			.maxVertexCount = num_vertices,
			.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
			.allowsTransforms = VK_FALSE
		};

		VkAccelerationStructureCreateInfoKHR accel_create_info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
			.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
			.flags = fast_build ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
			.maxGeometryCount = 1,
			.pGeometryInfos = &geometry_create,
		};

		qvkCreateAccelerationStructureKHR(qvk.device, &accel_create_info, NULL, accel);

		VkAccelerationStructureMemoryRequirementsInfoKHR mem_req_info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_KHR,
			.accelerationStructure = *accel,
			.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_KHR,
			.buildType = VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR
		};
		VkMemoryRequirements2 mem_req = { 0 };
		mem_req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
		qvkGetAccelerationStructureMemoryRequirementsKHR(qvk.device, &mem_req_info, &mem_req);

		_VK(allocate_gpu_memory(mem_req.memoryRequirements, mem_accel));

		VkBindAccelerationStructureMemoryInfoKHR bind_info = {
			.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_KHR,
			.accelerationStructure = *accel,
			.memory = *mem_accel,
		};

		_VK(qvkBindAccelerationStructureMemoryKHR(qvk.device, 1, &bind_info));

		if (match) {
			match->fast_build = fast_build;
			match->vertex_count = num_vertices;
			match->index_count = num_vertices;
		}
	}

	size_t scratch_buf_size = get_scratch_buffer_size(*accel);
	assert(scratch_buf_ptr + scratch_buf_size < SIZE_SCRATCH_BUFFER);

	VkAccelerationStructureGeometryKHR* geometries = &geometry;

	VkAccelerationStructureBuildGeometryInfoKHR as_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags = fast_build ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.update = VK_FALSE,
		.srcAccelerationStructure = VK_NULL_HANDLE,
		.dstAccelerationStructure = *accel,
		.geometryArrayOfPointers = VK_TRUE,
		.geometryCount = 1,
		.ppGeometries = &geometries,
		.scratchData = buf_accel_scratch.address + scratch_buf_ptr
	};

	VkAccelerationStructureBuildOffsetInfoKHR offset = { .primitiveCount = num_vertices / 3 };

	VkAccelerationStructureBuildOffsetInfoKHR* offsets = &offset;

	qvkCmdBuildAccelerationStructureKHR(cmd_buf, 1, &as_info, &offsets);

	scratch_buf_ptr += scratch_buf_size;

	return VK_SUCCESS;
}

VkResult
vkpt_pt_create_static(
		VkDeviceAddress address,
		int num_vertices, 
		int num_vertices_transparent,
		int num_vertices_sky,
		int num_vertices_custom_sky
		)
{
	VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

	scratch_buf_ptr = 0;

	VkResult ret = vkpt_pt_create_accel_bottom(
		address,
		num_vertices,
		&accel_static,
		NULL,
		&mem_accel_static,
		cmd_buf,
		VK_FALSE);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	scratch_buf_ptr = 0;

	ret = vkpt_pt_create_accel_bottom(
		address + num_vertices * sizeof(float) * 3,
		num_vertices_transparent,
		&accel_transparent,
		NULL,
		&mem_accel_transparent,
		cmd_buf,
		VK_FALSE);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	scratch_buf_ptr = 0;

	ret = vkpt_pt_create_accel_bottom(
		address + (num_vertices + num_vertices_transparent) * sizeof(float) * 3,
		num_vertices_sky,
		&accel_sky,
		NULL,
		&mem_accel_sky,
		cmd_buf,
		VK_FALSE);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	scratch_buf_ptr = 0;

	ret = vkpt_pt_create_accel_bottom(
		address + (num_vertices + num_vertices_transparent + num_vertices_sky) * sizeof(float) * 3,
		num_vertices_custom_sky,
		&accel_custom_sky,
		NULL,
		&mem_accel_custom_sky,
		cmd_buf,
		VK_FALSE);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	scratch_buf_ptr = 0;

	transparent_primitive_offset = num_vertices / 3;
	sky_primitive_offset = transparent_primitive_offset + num_vertices_transparent / 3;
	custom_sky_primitive_offset = sky_primitive_offset + num_vertices_sky / 3;

	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, qtrue);
	vkpt_wait_idle(qvk.queue_graphics, &qvk.cmd_buffers_graphics);

	return ret;
}

static VkResult
vkpt_pt_create_dynamic(
	VkCommandBuffer cmd_buf,
	int idx,
		VkDeviceAddress address,
		int num_vertices
		)
{
	return vkpt_pt_create_accel_bottom(
		address,
		num_vertices,
		accel_dynamic + idx,
		accel_dynamic_match + idx,
		mem_accel_dynamic + idx,
		cmd_buf,
		VK_TRUE);
}

static VkResult
vkpt_pt_create_transparent_models(
	VkCommandBuffer cmd_buf,
	int idx,
		VkDeviceAddress address,
		int num_vertices,
		int vertex_offset
		)
{
	transparent_model_primitive_offset = vertex_offset / 3;

	if (num_vertices > 0)
	{
		transparent_models_present = VK_TRUE;

		return vkpt_pt_create_accel_bottom(
			address + vertex_offset * 3 * sizeof(float),
			num_vertices,
			accel_transparent_models + idx,
			accel_transparent_models_match + idx,
			mem_accel_transparent_models + idx,
			cmd_buf,
			VK_TRUE);
	}
	else
	{
		transparent_models_present = VK_FALSE;
		return VK_SUCCESS;
	}
}

static VkResult
vkpt_pt_create_viewer_models(
		VkCommandBuffer cmd_buf,
		int idx,
		VkDeviceAddress address,
		int num_vertices,
		int vertex_offset
		)
{
	viewer_model_primitive_offset = vertex_offset / 3;

	return vkpt_pt_create_accel_bottom(
		address + vertex_offset * 3 * sizeof(float),
		num_vertices,
		accel_viewer_models + idx,
		accel_viewer_models_match + idx,
		mem_accel_viewer_models + idx,
		cmd_buf,
		VK_TRUE);
}

static VkResult
vkpt_pt_create_viewer_weapon(
	VkCommandBuffer cmd_buf,
	int idx,
		VkDeviceAddress address,
		int num_vertices,
		int vertex_offset
		)
{
	viewer_weapon_primitive_offset = vertex_offset / 3;

	return vkpt_pt_create_accel_bottom(
		address + vertex_offset * 3 * sizeof(float),
		num_vertices,
		accel_viewer_weapon + idx,
		accel_viewer_weapon_match + idx,
		mem_accel_viewer_weapon + idx,
		cmd_buf,
		VK_TRUE);
}

static VkResult
vkpt_pt_create_explosions(
	VkCommandBuffer cmd_buf,
	int idx,
	VkDeviceAddress address,
	int num_vertices,
	int vertex_offset
)
{
	if (num_vertices > 0)
	{
		explosions_primitive_offset = vertex_offset / 3;
		explosions_present = VK_TRUE;

		return vkpt_pt_create_accel_bottom(
			address + vertex_offset * 3 * sizeof(float),
			num_vertices,
			accel_explosions + idx,
			accel_explosions_match + idx,
			mem_accel_explosions + idx,
			cmd_buf,
			VK_TRUE);
	}
	else
	{
		explosions_present = VK_FALSE;
		return VK_SUCCESS;
	}
}

VkResult
vkpt_pt_create_all_dynamic(
	VkCommandBuffer cmd_buf,
	int idx, 
	VkBuffer vertex_buffer, 
	const EntityUploadInfo* upload_info)
{
	scratch_buf_ptr = 0;

	vkpt_pt_create_dynamic(cmd_buf, qvk.current_frame_index,
		qvk.buf_vertex.address + offsetof(VertexBuffer, positions_instanced), upload_info->dynamic_vertex_num);

	vkpt_pt_create_transparent_models(cmd_buf, qvk.current_frame_index,
		qvk.buf_vertex.address + offsetof(VertexBuffer, positions_instanced), upload_info->transparent_model_vertex_num,
		upload_info->transparent_model_vertex_offset);

	vkpt_pt_create_viewer_models(cmd_buf, qvk.current_frame_index,
		qvk.buf_vertex.address + offsetof(VertexBuffer, positions_instanced), upload_info->viewer_model_vertex_num,
		upload_info->viewer_model_vertex_offset);

	vkpt_pt_create_viewer_weapon(cmd_buf, qvk.current_frame_index,
		qvk.buf_vertex.address + offsetof(VertexBuffer, positions_instanced), upload_info->viewer_weapon_vertex_num,
		upload_info->viewer_weapon_vertex_offset);

	vkpt_pt_create_explosions(cmd_buf, qvk.current_frame_index,
		qvk.buf_vertex.address + offsetof(VertexBuffer, positions_instanced), upload_info->explosions_vertex_num,
		upload_info->explosions_vertex_offset);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	scratch_buf_ptr = 0;

	return VK_SUCCESS;
}

void
vkpt_pt_destroy_toplevel(int idx)
{
	if(accel_top[idx]) {
		qvkDestroyAccelerationStructureKHR(qvk.device, accel_top[idx], NULL);
		accel_top[idx] = VK_NULL_HANDLE;
		accel_top_match[idx].instanceCount = 0;
	}
	if(mem_accel_top[idx]) {
		vkFreeMemory(qvk.device, mem_accel_top[idx], NULL);
		mem_accel_top[idx] = VK_NULL_HANDLE;
	}
}

static void
append_blas(QvkGeometryInstance_t *instances, int *num_instances, VkAccelerationStructureKHR blas, int instance_id, int mask, int flags, int sbt_offset)
{
	VkAccelerationStructureDeviceAddressInfoKHR  as_device_address_info = {
		.sType				   = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = blas,
	};

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
		.acceleration_structure_address = qvkGetAccelerationStructureDeviceAddressKHR(qvk.device, &as_device_address_info)
	};

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
		append_blas(instances, &num_instances, accel_static, 0, AS_FLAG_OPAQUE, 0, 0);
		append_blas(instances, &num_instances, accel_transparent, transparent_primitive_offset, AS_FLAG_TRANSPARENT, 0, 0);
		append_blas(instances, &num_instances, accel_sky, AS_INSTANCE_FLAG_SKY | sky_primitive_offset, AS_FLAG_SKY, 0, 0);
		append_blas(instances, &num_instances, accel_custom_sky, AS_INSTANCE_FLAG_SKY | custom_sky_primitive_offset, AS_FLAG_CUSTOM_SKY, 0, 0);
	}
	append_blas(instances, &num_instances, accel_dynamic[idx], AS_INSTANCE_FLAG_DYNAMIC, AS_FLAG_OPAQUE, 0, 0);

	if (transparent_models_present)
	{
		append_blas(instances, &num_instances, accel_transparent_models[idx], AS_INSTANCE_FLAG_DYNAMIC | transparent_model_primitive_offset, AS_FLAG_TRANSPARENT, 0, 0);
	}

	if (cl_player_model->integer == CL_PLAYER_MODEL_FIRST_PERSON)
	{
		append_blas(instances, &num_instances, accel_viewer_models[idx], AS_INSTANCE_FLAG_DYNAMIC | viewer_model_primitive_offset, AS_FLAG_VIEWER_MODELS, VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, 0);
		append_blas(instances, &num_instances, accel_viewer_weapon[idx], AS_INSTANCE_FLAG_DYNAMIC | viewer_weapon_primitive_offset, AS_FLAG_VIEWER_WEAPON, weapon_left_handed ? VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_KHR : 0, 0);
	}

	int particle_num, beam_num, sprite_num;
	get_transparency_counts(&particle_num, &beam_num, &sprite_num);

	if (cvar_pt_enable_particles->integer != 0 && particle_num > 0)
	{
		append_blas(instances, &num_instances, get_transparency_particle_blas(), 0, AS_FLAG_PARTICLES, VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, 1);
	}

	if (cvar_pt_enable_beams->integer != 0 && beam_num > 0)
	{
		append_blas(instances, &num_instances, get_transparency_beam_blas(), 0, AS_FLAG_PARTICLES, VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, 2);
	}

	if (cvar_pt_enable_sprites->integer != 0 && sprite_num > 0)
	{
		append_blas(instances, &num_instances, get_transparency_sprite_blas(), 0, AS_FLAG_EXPLOSIONS, VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR, 4);
	}

	if(explosions_present)
	{
		append_blas(instances, &num_instances, accel_explosions[idx], AS_INSTANCE_FLAG_DYNAMIC | explosions_primitive_offset, AS_FLAG_EXPLOSIONS, 0, 3);
	}

	void *instance_data = buffer_map(buf_instances + idx);
	memcpy(instance_data, &instances, sizeof(QvkGeometryInstance_t) * num_instances);

	buffer_unmap(buf_instances + idx);
	instance_data = NULL;

	VkAccelerationStructureCreateGeometryTypeInfoKHR geometry_create_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_GEOMETRY_TYPE_INFO_KHR,
		.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		.maxPrimitiveCount = num_instances,
		.indexType = VK_INDEX_TYPE_NONE_KHR,
		.vertexFormat = VK_FORMAT_UNDEFINED
	};

	VkAccelerationStructureCreateInfoKHR accel_create_info = {
		.sType			  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.type			  = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.maxGeometryCount = 1,
		.pGeometryInfos	  = &geometry_create_info
	};

	if (accel_top_match[idx].instanceCount < num_instances) {
		vkpt_pt_destroy_toplevel(idx);

		qvkCreateAccelerationStructureKHR(qvk.device, &accel_create_info, NULL, accel_top + idx);

		/* XXX: do allocation only once with safety margin */
		VkAccelerationStructureMemoryRequirementsInfoKHR mem_req_info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_KHR,
			.accelerationStructure = accel_top[idx],
			.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_KHR,
			.buildType = VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR
		};
		VkMemoryRequirements2 mem_req = { 0 };
		mem_req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
		qvkGetAccelerationStructureMemoryRequirementsKHR(qvk.device, &mem_req_info, &mem_req);

		_VK(allocate_gpu_memory(mem_req.memoryRequirements, mem_accel_top + idx));

		VkBindAccelerationStructureMemoryInfoKHR bind_info = {
			.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_KHR,
			.accelerationStructure = accel_top[idx],
			.memory = mem_accel_top[idx],
		};

		_VK(qvkBindAccelerationStructureMemoryKHR(qvk.device, 1, &bind_info));

		assert(get_scratch_buffer_size(accel_top[idx]) < SIZE_SCRATCH_BUFFER);

		accel_top_match[idx].instanceCount = num_instances;
	}

	VkAccelerationStructureGeometryKHR instance_geometry = {
	  .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
	  .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
	  .geometry = {
		  .instances = {
			  .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR,
			  .arrayOfPointers = VK_FALSE,
			  .data = { .deviceAddress = buf_instances[idx].address }
		  },
	  }
	};

	VkAccelerationStructureGeometryKHR* geometries = &instance_geometry;

	VkAccelerationStructureBuildGeometryInfoKHR as_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.update = VK_FALSE,
		.srcAccelerationStructure = VK_NULL_HANDLE,
		.dstAccelerationStructure = accel_top[idx],
		.geometryArrayOfPointers = VK_TRUE,
		.geometryCount = 1,
		.ppGeometries = &geometries,
		.scratchData = buf_accel_scratch.address
	};

	VkAccelerationStructureBuildOffsetInfoKHR offset = { .primitiveCount = num_instances };

	VkAccelerationStructureBuildOffsetInfoKHR* offsets = &offset;

	qvkCmdBuildAccelerationStructureKHR(
			cmd_buf,
			1,
			&as_info,
			&offsets);

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

static void setup_rt_pipeline(VkCommandBuffer cmd_buf)
{
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		rt_pipeline_layout, 0, 1, rt_descriptor_set + qvk.current_frame_index, 0, 0);

	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		rt_pipeline_layout, 1, 1, &qvk.desc_set_ubo, 0, 0);

	VkDescriptorSet desc_set_textures = qvk_get_current_desc_set_textures();
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		rt_pipeline_layout, 2, 1, &desc_set_textures, 0, 0);

	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		rt_pipeline_layout, 3, 1, &qvk.desc_set_vertex_buffer, 0, 0);

	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rt_pipeline);
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

	setup_rt_pipeline(cmd_buf);

	BEGIN_PERF_MARKER(cmd_buf, PROFILER_PRIMARY_RAYS);

	for(int i = 0; i < qvk.device_count; i++)
	{
		set_current_gpu(cmd_buf, i);

		int idx = qvk.device_count == 1 ? -1 : i;
		vkCmdPushConstants(cmd_buf, rt_pipeline_layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(int), &idx);

		VkStridedBufferRegionKHR raygen = {
			.buffer = buf_shader_binding_table.buffer,
			.offset = SBT_RGEN_PRIMARY_RAYS * rt_properties.shaderGroupHandleSize,
			.stride = rt_properties.shaderGroupHandleSize,
			.size   = rt_properties.shaderGroupHandleSize
		};

		VkStridedBufferRegionKHR miss_and_hit = {
			.buffer = buf_shader_binding_table.buffer,
			.offset = 0,
			.stride = rt_properties.shaderGroupHandleSize,
			.size   = rt_properties.shaderGroupHandleSize
		};

		VkStridedBufferRegionKHR callable = {
			.buffer = VK_NULL_HANDLE,
			.offset = 0,
			.stride = 0,
			.size   = 0
		};

		qvkCmdTraceRaysKHR(cmd_buf,
				&raygen,
				&miss_and_hit,
				&miss_and_hit,
				&callable,
				qvk.extent_render.width / 2, qvk.extent_render.height, qvk.device_count == 1 ? 2 : 1);
	}

	set_current_gpu(cmd_buf, ALL_GPUS);

	END_PERF_MARKER(cmd_buf, PROFILER_PRIMARY_RAYS);

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_VISBUF]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_TRANSPARENT]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_TEX_GRADIENTS]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_MOTION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_SHADING_POSITION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_VIEW_DIRECTION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_THROUGHPUT]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_BOUNCE_THROUGHPUT]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_GODRAYS_THROUGHPUT_DIST]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_ALBEDO]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_METALLIC]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_CLUSTER]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_VIEW_DEPTH_A + frame_idx]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_NORMAL_A + frame_idx]);

	return VK_SUCCESS;
}

VkResult
vkpt_pt_trace_reflections(VkCommandBuffer cmd_buf, int bounce)
{
	int frame_idx = qvk.frame_counter & 1;

	setup_rt_pipeline(cmd_buf);

	for (int i = 0; i < qvk.device_count; i++)
	{
		set_current_gpu(cmd_buf, i);

		int idx = qvk.device_count == 1 ? -1 : i;
		vkCmdPushConstants(cmd_buf, rt_pipeline_layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(int), &idx);
		vkCmdPushConstants(cmd_buf, rt_pipeline_layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, sizeof(int), sizeof(int), &bounce);

		int shader = (bounce == 0) ? SBT_RGEN_REFLECT_REFRACT1 : SBT_RGEN_REFLECT_REFRACT2;

		VkStridedBufferRegionKHR raygen = {
			.buffer = buf_shader_binding_table.buffer,
			.offset = shader * rt_properties.shaderGroupHandleSize,
			.stride = rt_properties.shaderGroupHandleSize,
			.size   = rt_properties.shaderGroupHandleSize
		};

		VkStridedBufferRegionKHR miss_and_hit = {
			.buffer = buf_shader_binding_table.buffer,
			.offset = 0,
			.stride = rt_properties.shaderGroupHandleSize,
			.size   = rt_properties.shaderGroupHandleSize
		};

		VkStridedBufferRegionKHR callable = {
			.buffer = VK_NULL_HANDLE,
			.offset = 0,
			.stride = 0,
			.size   = 0
		};

		qvkCmdTraceRaysKHR(cmd_buf,
			&raygen,
			&miss_and_hit,
			&miss_and_hit,
			&callable,
			qvk.extent_render.width / 2, qvk.extent_render.height, qvk.device_count == 1 ? 2 : 1);
	}

	set_current_gpu(cmd_buf, ALL_GPUS);

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_TRANSPARENT]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_MOTION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_SHADING_POSITION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_VIEW_DIRECTION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_THROUGHPUT]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_GODRAYS_THROUGHPUT_DIST]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_ALBEDO]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_METALLIC]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_CLUSTER]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_VIEW_DEPTH_A + frame_idx]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_NORMAL_A + frame_idx]);

	return VK_SUCCESS;
}

VkResult
vkpt_pt_trace_lighting(VkCommandBuffer cmd_buf, float num_bounce_rays)
{
	int frame_idx = qvk.frame_counter & 1;

	setup_rt_pipeline(cmd_buf);

	BEGIN_PERF_MARKER(cmd_buf, PROFILER_DIRECT_LIGHTING);

	for (int i = 0; i < qvk.device_count; i++)
	{
		set_current_gpu(cmd_buf, i);

		int idx = qvk.device_count == 1 ? -1 : i;
		vkCmdPushConstants(cmd_buf, rt_pipeline_layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(int), &idx);

		int rgen_index = SBT_RGEN_DIRECT_LIGHTING;
		if (cvar_pt_caustics->value != 0)
			rgen_index = SBT_RGEN_DIRECT_LIGHTING_CAUSTICS;

		VkStridedBufferRegionKHR raygen = {
			.buffer = buf_shader_binding_table.buffer,
			.offset = rgen_index * rt_properties.shaderGroupHandleSize,
			.stride = rt_properties.shaderGroupHandleSize,
			.size   = rt_properties.shaderGroupHandleSize
		};

		VkStridedBufferRegionKHR miss_and_hit = {
			.buffer = buf_shader_binding_table.buffer,
			.offset = 0,
			.stride = rt_properties.shaderGroupHandleSize,
			.size   = rt_properties.shaderGroupHandleSize
		};

		VkStridedBufferRegionKHR callable = {
			.buffer = VK_NULL_HANDLE,
			.offset = 0,
			.stride = 0,
			.size   = 0
		};

		qvkCmdTraceRaysKHR(cmd_buf,
			&raygen,
			&miss_and_hit,
			&miss_and_hit,
			&callable,
			qvk.extent_render.width / 2, qvk.extent_render.height, qvk.device_count == 1 ? 2 : 1);
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

			int idx = qvk.device_count == 1 ? -1 : i;
			vkCmdPushConstants(cmd_buf, rt_pipeline_layout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(idx), &idx);

			for (int bounce_ray = 0; bounce_ray < (int)ceilf(num_bounce_rays); bounce_ray++)
			{
				int height;
				if (num_bounce_rays == 0.5f)
					height = qvk.extent_render.height / 2;
				else
					height = qvk.extent_render.height;

				int rgen_index = (bounce_ray == 0) 
					? SBT_RGEN_INDIRECT_LIGHTING_FIRST 
					: SBT_RGEN_INDIRECT_LIGHTING_SECOND;

				VkStridedBufferRegionKHR raygen = {
					.buffer = buf_shader_binding_table.buffer,
					.offset = rgen_index * rt_properties.shaderGroupHandleSize,
					.stride = rt_properties.shaderGroupHandleSize,
					.size   = rt_properties.shaderGroupHandleSize
				};

				VkStridedBufferRegionKHR miss_and_hit = {
					.buffer = buf_shader_binding_table.buffer,
					.offset = 0,
					.stride = rt_properties.shaderGroupHandleSize,
					.size   = rt_properties.shaderGroupHandleSize
				};

				VkStridedBufferRegionKHR callable = {
					.buffer = VK_NULL_HANDLE,
					.offset = 0,
					.stride = 0,
					.size   = 0
				};

				qvkCmdTraceRaysKHR(cmd_buf,
					&raygen,
					&miss_and_hit,
					&miss_and_hit,
					&callable,
					qvk.extent_render.width / 2, height, qvk.device_count == 1 ? 2 : 1);

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
	for(int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		vkpt_pt_destroy_toplevel(i);
		buffer_destroy(buf_instances + i);
		vkpt_pt_destroy_dynamic(i);
		vkpt_pt_destroy_transparent_models(i);
		vkpt_pt_destroy_viewer_models(i);
		vkpt_pt_destroy_viewer_weapon(i);
		vkpt_pt_destroy_explosions(i);
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

	VkPipelineShaderStageCreateInfo shader_stages[] = {
		SHADER_STAGE(QVK_MOD_PRIMARY_RAYS_RGEN,               VK_SHADER_STAGE_RAYGEN_BIT_KHR),
		SHADER_STAGE_SPEC(QVK_MOD_REFLECT_REFRACT_RGEN,       VK_SHADER_STAGE_RAYGEN_BIT_KHR, &specInfo[0]),
		SHADER_STAGE_SPEC(QVK_MOD_REFLECT_REFRACT_RGEN,       VK_SHADER_STAGE_RAYGEN_BIT_KHR, &specInfo[1]),
		SHADER_STAGE_SPEC(QVK_MOD_DIRECT_LIGHTING_RGEN,       VK_SHADER_STAGE_RAYGEN_BIT_KHR, &specInfo[0]),
		SHADER_STAGE_SPEC(QVK_MOD_DIRECT_LIGHTING_RGEN,       VK_SHADER_STAGE_RAYGEN_BIT_KHR, &specInfo[1]),
		SHADER_STAGE_SPEC(QVK_MOD_INDIRECT_LIGHTING_RGEN,     VK_SHADER_STAGE_RAYGEN_BIT_KHR, &specInfo[0]),
		SHADER_STAGE_SPEC(QVK_MOD_INDIRECT_LIGHTING_RGEN,     VK_SHADER_STAGE_RAYGEN_BIT_KHR, &specInfo[1]),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_RMISS,               VK_SHADER_STAGE_MISS_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_SHADOW_RMISS,        VK_SHADER_STAGE_MISS_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_RCHIT,               VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_PARTICLE_RAHIT,      VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_BEAM_RAHIT,          VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_EXPLOSION_RAHIT,     VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_SPRITE_RAHIT,        VK_SHADER_STAGE_ANY_HIT_BIT_KHR),
	};

	VkRayTracingShaderGroupCreateInfoKHR rt_shader_group_info[] = {
		[SBT_RGEN_PRIMARY_RAYS] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader      = 0,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		},
		[SBT_RGEN_REFLECT_REFRACT1] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader      = 1,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		},
		[SBT_RGEN_REFLECT_REFRACT2] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader      = 2,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		},
		[SBT_RGEN_DIRECT_LIGHTING] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader      = 3,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		},
		[SBT_RGEN_DIRECT_LIGHTING_CAUSTICS] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader      = 4,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		},
		[SBT_RGEN_INDIRECT_LIGHTING_FIRST] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader      = 5,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		},
		[SBT_RGEN_INDIRECT_LIGHTING_SECOND] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader      = 6,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		},
		[SBT_RMISS_PATH_TRACER] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader      = 7,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		},
		[SBT_RMISS_SHADOW] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader      = 8,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		},
		[SBT_RCHIT_OPAQUE] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
			.generalShader      = VK_SHADER_UNUSED_KHR,
			.closestHitShader   = 9,
			.anyHitShader       = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		},
		[SBT_RAHIT_PARTICLE] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
			.generalShader      = VK_SHADER_UNUSED_KHR,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = 10,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		},
		[SBT_RAHIT_BEAM] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
			.generalShader      = VK_SHADER_UNUSED_KHR,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = 11,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		},
		[SBT_RAHIT_EXPLOSION] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
			.generalShader      = VK_SHADER_UNUSED_KHR,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = 12,
			.intersectionShader = VK_SHADER_UNUSED_KHR
		},
		[SBT_RAHIT_SPRITE] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
			.generalShader      = VK_SHADER_UNUSED_KHR,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = 13,
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

	VkRayTracingPipelineCreateInfoKHR rt_pipeline_info = {
		.sType             = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		.stageCount        = LENGTH(shader_stages),
		.pStages           = shader_stages,
		.groupCount        = LENGTH(rt_shader_group_info),
		.pGroups           = rt_shader_group_info,
		.layout            = rt_pipeline_layout,
		.maxRecursionDepth = 1,
		.libraries         = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR },
		.pLibraryInterface = NULL
	};

	_VK(qvkCreateRayTracingPipelinesKHR(qvk.device, NULL, 1, &rt_pipeline_info, NULL, &rt_pipeline ));

	uint32_t num_groups = LENGTH(rt_shader_group_info);
	uint32_t shader_binding_table_size = rt_properties.shaderGroupHandleSize * num_groups;

	/* pt */
	_VK(buffer_create(&buf_shader_binding_table, shader_binding_table_size,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));

	void *shader_binding_table = buffer_map(&buf_shader_binding_table);
	_VK(qvkGetRayTracingShaderGroupHandlesKHR(qvk.device, rt_pipeline, 0, num_groups,
				shader_binding_table_size, shader_binding_table));
	buffer_unmap(&buf_shader_binding_table);
	shader_binding_table = NULL;

	return VK_SUCCESS;
}

VkResult
vkpt_pt_destroy_pipelines()
{
	buffer_destroy(&buf_shader_binding_table);
	vkDestroyPipeline(qvk.device, rt_pipeline, NULL);

	return VK_SUCCESS;
}
