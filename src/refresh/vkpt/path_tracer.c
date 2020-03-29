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

static VkPhysicalDeviceRayTracingPropertiesNV rt_properties = {
	.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV,
	.pNext = NULL,
	.maxRecursionDepth     = 0, /* updated during init */
	.shaderGroupHandleSize = 0
};

typedef struct accel_bottom_match_info_s {
	VkGeometryFlagsNV	flags;
	uint32_t vertexCount;
	uint32_t indexCount;
} accel_bottom_match_info_t;

typedef struct accel_top_match_info_s {
	uint32_t instanceCount;
} accel_top_match_info_t;

static BufferResource_t          buf_accel_scratch;
static size_t                    scratch_buf_ptr = 0;
static BufferResource_t          buf_instances    [MAX_FRAMES_IN_FLIGHT];
static VkAccelerationStructureNV accel_static;
static VkAccelerationStructureNV accel_transparent;
static VkAccelerationStructureNV accel_sky;
static VkAccelerationStructureNV accel_custom_sky;
static int                       transparent_primitive_offset = 0;
static int                       sky_primitive_offset = 0;
static int                       custom_sky_primitive_offset = 0;
static int                       transparent_model_primitive_offset = 0;
static int                       transparent_models_present = 0;
static int                       viewer_model_primitive_offset = 0;
static int                       viewer_weapon_primitive_offset = 0;
static int                       explosions_primitive_offset = 0;
static int                       explosions_present = 0;
static VkAccelerationStructureNV accel_dynamic    [MAX_FRAMES_IN_FLIGHT];
static accel_bottom_match_info_t accel_dynamic_match[MAX_FRAMES_IN_FLIGHT];
static VkAccelerationStructureNV accel_transparent_models[MAX_FRAMES_IN_FLIGHT];
static accel_bottom_match_info_t accel_transparent_models_match[MAX_FRAMES_IN_FLIGHT];
static VkAccelerationStructureNV accel_viewer_models[MAX_FRAMES_IN_FLIGHT];
static accel_bottom_match_info_t accel_viewer_models_match[MAX_FRAMES_IN_FLIGHT];
static VkAccelerationStructureNV accel_viewer_weapon[MAX_FRAMES_IN_FLIGHT];
static accel_bottom_match_info_t accel_viewer_weapon_match[MAX_FRAMES_IN_FLIGHT];
static VkAccelerationStructureNV accel_explosions[MAX_FRAMES_IN_FLIGHT];
static accel_bottom_match_info_t accel_explosions_match[MAX_FRAMES_IN_FLIGHT];
static VkAccelerationStructureNV accel_top        [MAX_FRAMES_IN_FLIGHT];
static accel_top_match_info_t    accel_top_match[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory            mem_accel_static;
static VkDeviceMemory            mem_accel_transparent;
static VkDeviceMemory            mem_accel_sky;
static VkDeviceMemory            mem_accel_custom_sky;
static VkDeviceMemory            mem_accel_top[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory            mem_accel_dynamic[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory            mem_accel_transparent_models[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory            mem_accel_viewer_models[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory            mem_accel_viewer_weapon[MAX_FRAMES_IN_FLIGHT];
static VkDeviceMemory            mem_accel_explosions[MAX_FRAMES_IN_FLIGHT];

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
    uint64_t acceleration_structure_handle;
} QvkGeometryInstance_t;

#define MEM_BARRIER_BUILD_ACCEL(cmd_buf, ...) \
	do { \
		VkMemoryBarrier mem_barrier = {  \
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,  \
			.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV \
						   | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV, \
			.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV, \
			__VA_ARGS__  \
		};  \
	 \
		vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, \
				VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV, 0, 1, \
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

	buffer_create(&buf_accel_scratch, SIZE_SCRATCH_BUFFER, VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	for(int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		buffer_create(buf_instances + i, INSTANCE_MAX_NUM * sizeof(QvkGeometryInstance_t), VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}

	/* create descriptor set layout */
	VkDescriptorSetLayoutBinding bindings[] = {
		{
			.binding         = RAY_GEN_ACCEL_STRUCTURE_BINDING_IDX,
			.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV,
			.descriptorCount = 1,
			.stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_NV,
		},
		{
			.binding         = RAY_GEN_PARTICLE_COLOR_BUFFER_BINDING_IDX,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
			.descriptorCount = 1,
			.stageFlags      = VK_SHADER_STAGE_ANY_HIT_BIT_NV,
		},
		{
			.binding         = RAY_GEN_BEAM_COLOR_BUFFER_BINDING_IDX,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
			.descriptorCount = 1,
			.stageFlags      = VK_SHADER_STAGE_ANY_HIT_BIT_NV,
		},
		{
			.binding         = RAY_GEN_SPRITE_INFO_BUFFER_BINDING_IDX,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
			.descriptorCount = 1,
			.stageFlags      = VK_SHADER_STAGE_ANY_HIT_BIT_NV,
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
		.stageFlags		= VK_SHADER_STAGE_RAYGEN_BIT_NV,
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
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, MAX_FRAMES_IN_FLIGHT }
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
	VkWriteDescriptorSetAccelerationStructureNV desc_accel_struct_info = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV,
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
			.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV
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
get_scratch_buffer_size(VkAccelerationStructureNV ac)
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

static inline VkGeometryNV
get_geometry(VkBuffer buffer, size_t offset, uint32_t num_vertices) 
{
	size_t size_per_vertex = sizeof(float) * 3;
	VkGeometryNV geometry = {
		.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV,
		.geometry = {
			.triangles = {
				.sType        = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV,
				.vertexData   = buffer,
				.vertexOffset = offset,
				.vertexCount  = num_vertices,
				.vertexStride = size_per_vertex,
				.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
				.indexType    = VK_INDEX_TYPE_NONE_NV,
				.indexCount   = 0,
			},
			.aabbs = { .sType = VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV }
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
		qvkDestroyAccelerationStructureNV(qvk.device, accel_static, NULL);
		accel_static = VK_NULL_HANDLE;
	}
	if (accel_transparent) {
		qvkDestroyAccelerationStructureNV(qvk.device, accel_transparent, NULL);
		accel_transparent = VK_NULL_HANDLE;
	}
	if (accel_sky) {
		qvkDestroyAccelerationStructureNV(qvk.device, accel_sky, NULL);
		accel_sky = VK_NULL_HANDLE;
	}
	if (accel_custom_sky) {
		qvkDestroyAccelerationStructureNV(qvk.device, accel_custom_sky, NULL);
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
		qvkDestroyAccelerationStructureNV(qvk.device, accel_dynamic[idx], NULL);
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
		qvkDestroyAccelerationStructureNV(qvk.device, accel_transparent_models[idx], NULL);
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
		qvkDestroyAccelerationStructureNV(qvk.device, accel_viewer_models[idx], NULL);
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
		qvkDestroyAccelerationStructureNV(qvk.device, accel_viewer_weapon[idx], NULL);
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
		qvkDestroyAccelerationStructureNV(qvk.device, accel_explosions[idx], NULL);
		accel_explosions[idx] = VK_NULL_HANDLE;
	}
	return VK_SUCCESS;
}

static int accel_matches(accel_bottom_match_info_t *match,
						 VkGeometryFlagsNV flags,
						 uint32_t vertex_count,
						 uint32_t index_count) {
	return match->flags == flags &&
		match->vertexCount >= vertex_count &&
		match->indexCount >= index_count;
}

// How much to bloat the dynamic geometry allocations
// to try to avoid later allocations.
#define DYNAMIC_GEOMETRY_BLOAT_FACTOR 2

static VkResult
vkpt_pt_create_accel_bottom(
		VkBuffer vertex_buffer,
		size_t buffer_offset,
		int num_vertices,
		VkAccelerationStructureNV *accel,
		accel_bottom_match_info_t *match,
		VkDeviceMemory *mem_accel,
		VkCommandBuffer cmd_buf,
		int fast_build
		)
{
	assert(accel);
	assert(mem_accel);

	VkGeometryNV geometry = get_geometry(vertex_buffer, buffer_offset, num_vertices);

	int doFree = 0;
	int doAlloc = 0;

	if (!match || !accel_matches(match, geometry.flags, num_vertices, num_vertices) || *accel == VK_NULL_HANDLE) {
		doAlloc = 1;
		doFree = (*accel != VK_NULL_HANDLE);
	}

	if (doFree) {
		if (*mem_accel) {
			vkFreeMemory(qvk.device, *mem_accel, NULL);
			*mem_accel = VK_NULL_HANDLE;
		}
		if (*accel) {
			qvkDestroyAccelerationStructureNV(qvk.device, *accel, NULL);
			*accel = VK_NULL_HANDLE;
		}
	}

	if (doAlloc) {
		VkGeometryNV allocGeometry = geometry;

		// Only dynamic geometries have match info
		if (match) {
			allocGeometry.geometry.triangles.indexCount *= DYNAMIC_GEOMETRY_BLOAT_FACTOR;
			allocGeometry.geometry.triangles.vertexCount *= DYNAMIC_GEOMETRY_BLOAT_FACTOR;
		}

		VkAccelerationStructureCreateInfoNV accel_create_info = {
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

		qvkCreateAccelerationStructureNV(qvk.device, &accel_create_info, NULL, accel);

		VkAccelerationStructureMemoryRequirementsInfoNV mem_req_info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV,
			.accelerationStructure = *accel,
			.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV
		};
		VkMemoryRequirements2 mem_req = { 0 };
		mem_req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
		qvkGetAccelerationStructureMemoryRequirementsNV(qvk.device, &mem_req_info, &mem_req);

		_VK(allocate_gpu_memory(mem_req.memoryRequirements, mem_accel));

		VkBindAccelerationStructureMemoryInfoNV bind_info = {
			.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV,
			.accelerationStructure = *accel,
			.memory = *mem_accel,
		};

		_VK(qvkBindAccelerationStructureMemoryNV(qvk.device, 1, &bind_info));

		if (match) {
			match->flags = allocGeometry.flags;
			match->vertexCount = allocGeometry.geometry.triangles.vertexCount;
			match->indexCount = num_vertices;
		}
	}

	size_t scratch_buf_size = get_scratch_buffer_size(*accel);
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
			*accel,
			VK_NULL_HANDLE, /* source acceleration structure ?? */
			buf_accel_scratch.buffer,
			scratch_buf_ptr);

	scratch_buf_ptr += scratch_buf_size;

	return VK_SUCCESS;
}

VkResult
vkpt_pt_create_static(
		VkBuffer vertex_buffer,
		size_t buffer_offset,
		int num_vertices, 
		int num_vertices_transparent,
		int num_vertices_sky,
		int num_vertices_custom_sky
		)
{
	VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

	scratch_buf_ptr = 0;

	VkResult ret = vkpt_pt_create_accel_bottom(
		vertex_buffer,
		buffer_offset,
		num_vertices,
		&accel_static,
		NULL,
		&mem_accel_static,
		cmd_buf,
		VK_FALSE);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	scratch_buf_ptr = 0;

	ret = vkpt_pt_create_accel_bottom(
		vertex_buffer,
		buffer_offset + num_vertices * sizeof(float) * 3,
		num_vertices_transparent,
		&accel_transparent,
		NULL,
		&mem_accel_transparent,
		cmd_buf,
		VK_FALSE);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	scratch_buf_ptr = 0;

	ret = vkpt_pt_create_accel_bottom(
		vertex_buffer,
		buffer_offset + (num_vertices + num_vertices_transparent) * sizeof(float) * 3,
		num_vertices_sky,
		&accel_sky,
		NULL,
		&mem_accel_sky,
		cmd_buf,
		VK_FALSE);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	scratch_buf_ptr = 0;

	ret = vkpt_pt_create_accel_bottom(
		vertex_buffer,
		buffer_offset + (num_vertices + num_vertices_transparent + num_vertices_sky) * sizeof(float) * 3,
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
		VkBuffer vertex_buffer,
		size_t buffer_offset,
		int num_vertices
		)
{
	return vkpt_pt_create_accel_bottom(
		vertex_buffer,
		buffer_offset,
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
		VkBuffer vertex_buffer,
		size_t buffer_offset,
		int num_vertices,
		int vertex_offset
		)
{
	transparent_model_primitive_offset = vertex_offset / 3;

	if (num_vertices > 0)
	{
		transparent_models_present = VK_TRUE;

		return vkpt_pt_create_accel_bottom(
			vertex_buffer,
			buffer_offset + vertex_offset * 3 * sizeof(float),
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
		VkBuffer vertex_buffer,
		size_t buffer_offset,
		int num_vertices,
		int vertex_offset
		)
{
	viewer_model_primitive_offset = vertex_offset / 3;

	return vkpt_pt_create_accel_bottom(
		vertex_buffer,
		buffer_offset + vertex_offset * 3 * sizeof(float),
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
		VkBuffer vertex_buffer,
		size_t buffer_offset,
		int num_vertices,
		int vertex_offset
		)
{
	viewer_weapon_primitive_offset = vertex_offset / 3;

	return vkpt_pt_create_accel_bottom(
		vertex_buffer,
		buffer_offset + vertex_offset * 3 * sizeof(float),
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
	VkBuffer vertex_buffer,
	size_t buffer_offset,
	int num_vertices,
	int vertex_offset
)
{
	if (num_vertices > 0)
	{
		explosions_primitive_offset = vertex_offset / 3;
		explosions_present = VK_TRUE;

		return vkpt_pt_create_accel_bottom(
			vertex_buffer,
			buffer_offset + vertex_offset * 3 * sizeof(float),
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

	vkpt_pt_create_dynamic(cmd_buf, qvk.current_frame_index, qvk.buf_vertex.buffer,
		offsetof(VertexBuffer, positions_instanced), upload_info->dynamic_vertex_num);

	vkpt_pt_create_transparent_models(cmd_buf, qvk.current_frame_index, qvk.buf_vertex.buffer,
		offsetof(VertexBuffer, positions_instanced), upload_info->transparent_model_vertex_num,
		upload_info->transparent_model_vertex_offset);

	vkpt_pt_create_viewer_models(cmd_buf, qvk.current_frame_index, qvk.buf_vertex.buffer,
		offsetof(VertexBuffer, positions_instanced), upload_info->viewer_model_vertex_num,
		upload_info->viewer_model_vertex_offset);

	vkpt_pt_create_viewer_weapon(cmd_buf, qvk.current_frame_index, qvk.buf_vertex.buffer,
		offsetof(VertexBuffer, positions_instanced), upload_info->viewer_weapon_vertex_num,
		upload_info->viewer_weapon_vertex_offset);

	vkpt_pt_create_explosions(cmd_buf, qvk.current_frame_index, qvk.buf_vertex.buffer,
		offsetof(VertexBuffer, positions_instanced), upload_info->explosions_vertex_num,
		upload_info->explosions_vertex_offset);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);
	scratch_buf_ptr = 0;

	return VK_SUCCESS;
}

void
vkpt_pt_destroy_toplevel(int idx)
{
	if(accel_top[idx]) {
		qvkDestroyAccelerationStructureNV(qvk.device, accel_top[idx], NULL);
		accel_top[idx] = VK_NULL_HANDLE;
		accel_top_match[idx].instanceCount = 0;
	}
	if(mem_accel_top[idx]) {
		vkFreeMemory(qvk.device, mem_accel_top[idx], NULL);
		mem_accel_top[idx] = VK_NULL_HANDLE;
	}
}

static void
append_blas(QvkGeometryInstance_t *instances, int *num_instances, VkAccelerationStructureNV blas, int instance_id, int mask, int flags, int sbt_offset)
{
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
		.acceleration_structure_handle = 1337, // will be overwritten
	};

	_VK(qvkGetAccelerationStructureHandleNV(qvk.device, blas, sizeof(uint64_t), &instance.acceleration_structure_handle));

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
		append_blas(instances, &num_instances, accel_viewer_models[idx], AS_INSTANCE_FLAG_DYNAMIC | viewer_model_primitive_offset, AS_FLAG_VIEWER_MODELS, VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV, 0);
		append_blas(instances, &num_instances, accel_viewer_weapon[idx], AS_INSTANCE_FLAG_DYNAMIC | viewer_weapon_primitive_offset, AS_FLAG_VIEWER_WEAPON, weapon_left_handed ? VK_GEOMETRY_INSTANCE_TRIANGLE_FRONT_COUNTERCLOCKWISE_BIT_NV : 0, 0);
	}

	int particle_num, beam_num, sprite_num;
	get_transparency_counts(&particle_num, &beam_num, &sprite_num);

	if (cvar_pt_enable_particles->integer != 0 && particle_num > 0)
	{
		append_blas(instances, &num_instances, get_transparency_particle_blas(), 0, AS_FLAG_PARTICLES, VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV, 1);
	}

	if (cvar_pt_enable_beams->integer != 0 && beam_num > 0)
	{
		append_blas(instances, &num_instances, get_transparency_beam_blas(), 0, AS_FLAG_PARTICLES, VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV, 2);
	}

	if (cvar_pt_enable_sprites->integer != 0 && sprite_num > 0)
	{
		append_blas(instances, &num_instances, get_transparency_sprite_blas(), 0, AS_FLAG_EXPLOSIONS, VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV, 4);
	}

	if(explosions_present)
	{
		append_blas(instances, &num_instances, accel_explosions[idx], AS_INSTANCE_FLAG_DYNAMIC | explosions_primitive_offset, AS_FLAG_EXPLOSIONS, 0, 3);
	}

	void *instance_data = buffer_map(buf_instances + idx);
	memcpy(instance_data, &instances, sizeof(QvkGeometryInstance_t) * num_instances);

	buffer_unmap(buf_instances + idx);
	instance_data = NULL;

	VkAccelerationStructureCreateInfoNV accel_create_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV,
		.info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV,
			.instanceCount = num_instances,
			.geometryCount = 0,
			.pGeometries   = NULL,
			.type		  = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV
		}
	};

	if (accel_top_match[idx].instanceCount < accel_create_info.info.instanceCount) {
		vkpt_pt_destroy_toplevel(idx);

		qvkCreateAccelerationStructureNV(qvk.device, &accel_create_info, NULL, accel_top + idx);

		/* XXX: do allocation only once with safety margin */
		VkAccelerationStructureMemoryRequirementsInfoNV mem_req_info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV,
			.accelerationStructure = accel_top[idx],
			.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV
		};
		VkMemoryRequirements2 mem_req = { 0 };
		mem_req.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
		qvkGetAccelerationStructureMemoryRequirementsNV(qvk.device, &mem_req_info, &mem_req);

		_VK(allocate_gpu_memory(mem_req.memoryRequirements, mem_accel_top + idx));

		VkBindAccelerationStructureMemoryInfoNV bind_info = {
			.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV,
			.accelerationStructure = accel_top[idx],
			.memory = mem_accel_top[idx],
		};

		_VK(qvkBindAccelerationStructureMemoryNV(qvk.device, 1, &bind_info));

		assert(get_scratch_buffer_size(accel_top[idx]) < SIZE_SCRATCH_BUFFER);

		accel_top_match[idx].instanceCount = accel_create_info.info.instanceCount;
	}

	VkAccelerationStructureInfoNV as_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV,
		.geometryCount = 0,
		.pGeometries = NULL,
		.instanceCount = num_instances,
	};

	qvkCmdBuildAccelerationStructureNV(
			cmd_buf,
			&as_info,
			buf_instances[idx].buffer, /* instance buffer */
			0 /* instance offset */,
			VK_FALSE,  /* update */
			accel_top[idx],
			VK_NULL_HANDLE, /* source acceleration structure ?? */
			buf_accel_scratch.buffer,
			0 /* scratch offset */);

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
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
		rt_pipeline_layout, 0, 1, rt_descriptor_set + qvk.current_frame_index, 0, 0);

	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
		rt_pipeline_layout, 1, 1, &qvk.desc_set_ubo, 0, 0);

	VkDescriptorSet desc_set_textures = qvk_get_current_desc_set_textures();
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
		rt_pipeline_layout, 2, 1, &desc_set_textures, 0, 0);

	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
		rt_pipeline_layout, 3, 1, &qvk.desc_set_vertex_buffer, 0, 0);

	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rt_pipeline);
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
		vkCmdPushConstants(cmd_buf, rt_pipeline_layout, VK_SHADER_STAGE_RAYGEN_BIT_NV, 0, sizeof(int), &idx);

		qvkCmdTraceRaysNV(cmd_buf,
				buf_shader_binding_table.buffer, SBT_RGEN_PRIMARY_RAYS * rt_properties.shaderGroupHandleSize,
				buf_shader_binding_table.buffer, 0, rt_properties.shaderGroupHandleSize,
				buf_shader_binding_table.buffer, 0, rt_properties.shaderGroupHandleSize,
				VK_NULL_HANDLE, 0, 0,
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
		vkCmdPushConstants(cmd_buf, rt_pipeline_layout, VK_SHADER_STAGE_RAYGEN_BIT_NV, 0, sizeof(int), &idx);
		vkCmdPushConstants(cmd_buf, rt_pipeline_layout, VK_SHADER_STAGE_RAYGEN_BIT_NV, sizeof(int), sizeof(int), &bounce);

		int shader = (bounce == 0) ? SBT_RGEN_REFLECT_REFRACT1 : SBT_RGEN_REFLECT_REFRACT2;

		qvkCmdTraceRaysNV(cmd_buf,
			buf_shader_binding_table.buffer, shader * rt_properties.shaderGroupHandleSize,
			buf_shader_binding_table.buffer, 0, rt_properties.shaderGroupHandleSize,
			buf_shader_binding_table.buffer, 0, rt_properties.shaderGroupHandleSize,
			VK_NULL_HANDLE, 0, 0,
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
		vkCmdPushConstants(cmd_buf, rt_pipeline_layout, VK_SHADER_STAGE_RAYGEN_BIT_NV, 0, sizeof(int), &idx);

		int rgen_index = SBT_RGEN_DIRECT_LIGHTING;
		if (cvar_pt_caustics->value != 0)
			rgen_index = SBT_RGEN_DIRECT_LIGHTING_CAUSTICS;

		qvkCmdTraceRaysNV(cmd_buf,
			buf_shader_binding_table.buffer, rgen_index * rt_properties.shaderGroupHandleSize,
			buf_shader_binding_table.buffer, 0, rt_properties.shaderGroupHandleSize,
			buf_shader_binding_table.buffer, 0, rt_properties.shaderGroupHandleSize,
			VK_NULL_HANDLE, 0, 0,
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
			vkCmdPushConstants(cmd_buf, rt_pipeline_layout, VK_SHADER_STAGE_RAYGEN_BIT_NV, 0, sizeof(idx), &idx);

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

				qvkCmdTraceRaysNV(cmd_buf,
					buf_shader_binding_table.buffer, rgen_index * rt_properties.shaderGroupHandleSize,
					buf_shader_binding_table.buffer, 0, rt_properties.shaderGroupHandleSize,
					buf_shader_binding_table.buffer, 0, rt_properties.shaderGroupHandleSize,
					VK_NULL_HANDLE, 0, 0,
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
		SHADER_STAGE(QVK_MOD_PRIMARY_RAYS_RGEN,               VK_SHADER_STAGE_RAYGEN_BIT_NV),
		SHADER_STAGE_SPEC(QVK_MOD_REFLECT_REFRACT_RGEN,       VK_SHADER_STAGE_RAYGEN_BIT_NV, &specInfo[0]),
		SHADER_STAGE_SPEC(QVK_MOD_REFLECT_REFRACT_RGEN,       VK_SHADER_STAGE_RAYGEN_BIT_NV, &specInfo[1]),
		SHADER_STAGE_SPEC(QVK_MOD_DIRECT_LIGHTING_RGEN,       VK_SHADER_STAGE_RAYGEN_BIT_NV, &specInfo[0]),
		SHADER_STAGE_SPEC(QVK_MOD_DIRECT_LIGHTING_RGEN,       VK_SHADER_STAGE_RAYGEN_BIT_NV, &specInfo[1]),
		SHADER_STAGE_SPEC(QVK_MOD_INDIRECT_LIGHTING_RGEN,     VK_SHADER_STAGE_RAYGEN_BIT_NV, &specInfo[0]),
		SHADER_STAGE_SPEC(QVK_MOD_INDIRECT_LIGHTING_RGEN,     VK_SHADER_STAGE_RAYGEN_BIT_NV, &specInfo[1]),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_RMISS,               VK_SHADER_STAGE_MISS_BIT_NV),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_SHADOW_RMISS,        VK_SHADER_STAGE_MISS_BIT_NV),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_RCHIT,               VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_PARTICLE_RAHIT,      VK_SHADER_STAGE_ANY_HIT_BIT_NV),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_BEAM_RAHIT,          VK_SHADER_STAGE_ANY_HIT_BIT_NV),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_EXPLOSION_RAHIT,     VK_SHADER_STAGE_ANY_HIT_BIT_NV),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_SPRITE_RAHIT,        VK_SHADER_STAGE_ANY_HIT_BIT_NV),
	};

	VkRayTracingShaderGroupCreateInfoNV rt_shader_group_info[] = {
		[SBT_RGEN_PRIMARY_RAYS] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
			.generalShader      = 0,
			.closestHitShader   = VK_SHADER_UNUSED_NV,
			.anyHitShader       = VK_SHADER_UNUSED_NV,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		[SBT_RGEN_REFLECT_REFRACT1] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
			.generalShader      = 1,
			.closestHitShader   = VK_SHADER_UNUSED_NV,
			.anyHitShader       = VK_SHADER_UNUSED_NV,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		[SBT_RGEN_REFLECT_REFRACT2] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
			.generalShader      = 2,
			.closestHitShader   = VK_SHADER_UNUSED_NV,
			.anyHitShader       = VK_SHADER_UNUSED_NV,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		[SBT_RGEN_DIRECT_LIGHTING] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
			.generalShader      = 3,
			.closestHitShader   = VK_SHADER_UNUSED_NV,
			.anyHitShader       = VK_SHADER_UNUSED_NV,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		[SBT_RGEN_DIRECT_LIGHTING_CAUSTICS] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
			.generalShader      = 4,
			.closestHitShader   = VK_SHADER_UNUSED_NV,
			.anyHitShader       = VK_SHADER_UNUSED_NV,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		[SBT_RGEN_INDIRECT_LIGHTING_FIRST] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
			.generalShader      = 5,
			.closestHitShader   = VK_SHADER_UNUSED_NV,
			.anyHitShader       = VK_SHADER_UNUSED_NV,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		[SBT_RGEN_INDIRECT_LIGHTING_SECOND] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
			.generalShader      = 6,
			.closestHitShader   = VK_SHADER_UNUSED_NV,
			.anyHitShader       = VK_SHADER_UNUSED_NV,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		[SBT_RMISS_PATH_TRACER] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
			.generalShader      = 7,
			.closestHitShader   = VK_SHADER_UNUSED_NV,
			.anyHitShader       = VK_SHADER_UNUSED_NV,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		[SBT_RMISS_SHADOW] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
			.generalShader      = 8,
			.closestHitShader   = VK_SHADER_UNUSED_NV,
			.anyHitShader       = VK_SHADER_UNUSED_NV,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		[SBT_RCHIT_OPAQUE] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV,
			.generalShader      = VK_SHADER_UNUSED_NV,
			.closestHitShader   = 9,
			.anyHitShader       = VK_SHADER_UNUSED_NV,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		[SBT_RAHIT_PARTICLE] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV,
			.generalShader      = VK_SHADER_UNUSED_NV,
			.closestHitShader   = VK_SHADER_UNUSED_NV,
			.anyHitShader       = 10,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		[SBT_RAHIT_BEAM] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV,
			.generalShader      = VK_SHADER_UNUSED_NV,
			.closestHitShader   = VK_SHADER_UNUSED_NV,
			.anyHitShader       = 11,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		[SBT_RAHIT_EXPLOSION] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV,
			.generalShader      = VK_SHADER_UNUSED_NV,
			.closestHitShader   = VK_SHADER_UNUSED_NV,
			.anyHitShader       = 12,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		[SBT_RAHIT_SPRITE] = {
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV,
			.generalShader      = VK_SHADER_UNUSED_NV,
			.closestHitShader   = VK_SHADER_UNUSED_NV,
			.anyHitShader       = 13,
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

	_VK(qvkCreateRayTracingPipelinesNV(qvk.device, NULL, 1, &rt_pipeline_info,     NULL, &rt_pipeline    ));

	uint32_t num_groups = LENGTH(rt_shader_group_info);
	uint32_t shader_binding_table_size = rt_properties.shaderGroupHandleSize * num_groups;

	/* pt */
	_VK(buffer_create(&buf_shader_binding_table, shader_binding_table_size,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));

	void *shader_binding_table = buffer_map(&buf_shader_binding_table);
	_VK(qvkGetRayTracingShaderGroupHandlesNV(qvk.device, rt_pipeline, 0, num_groups,
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
