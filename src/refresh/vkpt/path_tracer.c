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

#include "shared/shared.h"
#include "vkpt.h"
#include "vk_util.h"

#include <assert.h>

#define RAY_GEN_ACCEL_STRUCTURE_BINDING_IDX 0

#define SIZE_SCRATCH_BUFFER (1 << 24)
/* not really a changeable parameter! */
#define NUM_INSTANCES 2

static VkPhysicalDeviceRayTracingPropertiesNV rt_properties = {
	.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV,
	.pNext = NULL,
	.maxRecursionDepth     = 0, /* updated during init */
	.shaderGroupHandleSize = 0
};

static BufferResource_t          buf_accel_scratch;
static BufferResource_t          buf_instances    [MAX_SWAPCHAIN_IMAGES];
static VkAccelerationStructureNV accel_static;
static VkAccelerationStructureNV accel_dynamic    [MAX_SWAPCHAIN_IMAGES];
static VkAccelerationStructureNV accel_top        [MAX_SWAPCHAIN_IMAGES];
static VkDeviceMemory            mem_accel_static;
static VkDeviceMemory            mem_accel_top    [MAX_SWAPCHAIN_IMAGES];
static VkDeviceMemory            mem_accel_dynamic[MAX_SWAPCHAIN_IMAGES];

static BufferResource_t buf_shader_binding_table, buf_shader_binding_table_rtx;

static VkDescriptorPool      rt_descriptor_pool;
static VkDescriptorSet       rt_descriptor_set[MAX_SWAPCHAIN_IMAGES];
static VkDescriptorSetLayout rt_descriptor_set_layout;
static VkPipelineLayout      rt_pipeline_layout;
static VkPipeline            rt_pipeline, rt_pipeline_rtx;

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
			.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_NV \
						   | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_NV, \
			__VA_ARGS__  \
		};  \
	 \
		vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, \
				VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV, 0, 1, \
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

	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		buffer_create(buf_instances + i, NUM_INSTANCES * sizeof(QvkGeometryInstance_t), VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
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
		qvk.desc_set_layout_vertex_buffer,
		qvk.desc_set_layout_light_hierarchy
	};

	/* create pipeline */
	VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
		.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = LENGTH(desc_set_layouts),
		.pSetLayouts    = desc_set_layouts,
	};

	_VK(vkCreatePipelineLayout(qvk.device, &pipeline_layout_create_info, NULL, &rt_pipeline_layout));
	ATTACH_LABEL_VARIABLE(rt_pipeline_layout, PIPELINE_LAYOUT);

	VkDescriptorPoolSize pool_sizes[] = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,             MAX_SWAPCHAIN_IMAGES },
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, MAX_SWAPCHAIN_IMAGES }
	};

	VkDescriptorPoolCreateInfo pool_create_info = {
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets       = MAX_SWAPCHAIN_IMAGES,
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

	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		_VK(vkAllocateDescriptorSets(qvk.device, &descriptor_set_alloc_info, rt_descriptor_set + i));
		ATTACH_LABEL_VARIABLE(rt_descriptor_set[i], DESCRIPTOR_SET);
	}

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

	VkWriteDescriptorSet write_desc_accel = {
		.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext           = &desc_accel_struct_info,
		.dstSet          = rt_descriptor_set[idx],
		.dstBinding      = RAY_GEN_ACCEL_STRUCTURE_BINDING_IDX,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV
	};

	vkUpdateDescriptorSets(qvk.device, 1, &write_desc_accel, 0, NULL);

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

	VkMemoryRequirements2 mem_req;
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
				.indexCount   = num_vertices, /* idiotic, doesn't work without */
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
	if(accel_static) {
		qvkDestroyAccelerationStructureNV(qvk.device, accel_static, NULL);
		accel_static = VK_NULL_HANDLE;
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

static VkResult
vkpt_pt_create_accel_bottom(
		VkBuffer vertex_buffer,
		size_t buffer_offset,
		int num_vertices,
		VkAccelerationStructureNV *accel,
		VkDeviceMemory *mem_accel,
		VkCommandBuffer cmd_buf
		)
{
	assert(accel);
	assert(!*accel);
	assert(mem_accel);
	assert(!*mem_accel);

	VkGeometryNV geometry = get_geometry(vertex_buffer, buffer_offset, num_vertices);

	VkAccelerationStructureCreateInfoNV accel_create_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV,
		.info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV,
			.instanceCount = 0,
			.geometryCount = 1,
			.pGeometries   = &geometry,
			.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV
		}
	};

	qvkCreateAccelerationStructureNV(qvk.device, &accel_create_info, NULL, accel);

	VkAccelerationStructureMemoryRequirementsInfoNV mem_req_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV,
		.accelerationStructure = *accel,
		.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV
	};
	VkMemoryRequirements2 mem_req;
	qvkGetAccelerationStructureMemoryRequirementsNV(qvk.device, &mem_req_info, &mem_req);

	VkMemoryAllocateInfo mem_alloc_info = {
		.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize  = mem_req.memoryRequirements.size,
		.memoryTypeIndex = get_memory_type(mem_req.memoryRequirements.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};

	_VK(vkAllocateMemory(qvk.device, &mem_alloc_info, NULL, mem_accel));

	VkBindAccelerationStructureMemoryInfoNV bind_info = {
		.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV,
		.accelerationStructure = *accel,
		.memory = *mem_accel,
	};

	_VK(qvkBindAccelerationStructureMemoryNV(qvk.device, 1, &bind_info));

	assert(get_scratch_buffer_size(*accel) < SIZE_SCRATCH_BUFFER);

	VkAccelerationStructureInfoNV as_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV,
		.geometryCount = 1,
		.pGeometries = &geometry,
		/* setting .flags here somehow screws up the build */
	};

	qvkCmdBuildAccelerationStructureNV(cmd_buf, &as_info,
			VK_NULL_HANDLE, /* instance buffer */
			0 /* instance offset */,
			VK_FALSE,  /* update */
			*accel,
			VK_NULL_HANDLE, /* source acceleration structure ?? */
			buf_accel_scratch.buffer,
			0 /* scratch offset */);

	MEM_BARRIER_BUILD_ACCEL(cmd_buf);

	return VK_SUCCESS;
}

VkResult
vkpt_pt_create_static(
		VkBuffer vertex_buffer,
		size_t buffer_offset,
		int num_vertices
		)
{
	VkCommandBufferAllocateInfo cmd_buf_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = qvk.command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	VkCommandBuffer cmd_buf;
	_VK(vkAllocateCommandBuffers(qvk.device, &cmd_buf_info, &cmd_buf));

	VkCommandBufferBeginInfo cmd_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(cmd_buf, &cmd_begin_info);

	VkResult ret = vkpt_pt_create_accel_bottom(
		vertex_buffer,
		buffer_offset,
		num_vertices,
		&accel_static,
		&mem_accel_static,
		cmd_buf);

	vkEndCommandBuffer(cmd_buf);

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd_buf,
	};

	vkQueueSubmit(qvk.queue_graphics, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(qvk.queue_graphics);

	vkFreeCommandBuffers(qvk.device, qvk.command_pool, 1, &cmd_buf);

	return ret;
}

VkResult
vkpt_pt_create_dynamic(
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
		mem_accel_dynamic + idx,
		qvk.cmd_buf_current);
}

void
vkpt_pt_destroy_toplevel(int idx)
{
	if(accel_top[idx]) {
		qvkDestroyAccelerationStructureNV(qvk.device, accel_top[idx], NULL);
		accel_top[idx] = VK_NULL_HANDLE;
	}
	if(mem_accel_top[idx]) {
		vkFreeMemory(qvk.device, mem_accel_top[idx], NULL);
		mem_accel_top[idx] = VK_NULL_HANDLE;
	}
}

VkResult
vkpt_pt_create_toplevel(int idx)
{
	vkpt_pt_destroy_toplevel(idx);

	QvkGeometryInstance_t instances[NUM_INSTANCES] = {
		{
			.transform = {
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
			},
			.instance_id     = 0,
			.mask            = 0xff, /* ??? */
			.instance_offset = 0,    /* ??? */
			//.flags           = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV,
			.acceleration_structure_handle = 1337, // will be overwritten
		},
		{
			.transform = {
				1.0f, 0.0f, 0.0f, 0.0f,
				0.0f, 1.0f, 0.0f, 0.0f,
				0.0f, 0.0f, 1.0f, 0.0f,
			},
			.instance_id     = 0,
			.mask            = 0xff, /* ??? */
			.instance_offset = 0,    /* ??? */
			//.flags           = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV,
			.acceleration_structure_handle = 1337, // will be overwritten
		},
	};

	assert(NUM_INSTANCES == 2);

	_VK(qvkGetAccelerationStructureHandleNV(qvk.device, accel_static, sizeof(uint64_t), 
				&instances[0].acceleration_structure_handle));

	_VK(qvkGetAccelerationStructureHandleNV(qvk.device, accel_dynamic[idx], sizeof(uint64_t), 
				&instances[1].acceleration_structure_handle));

	void *instance_data = buffer_map(buf_instances + idx);
	memcpy(instance_data, &instances, sizeof(QvkGeometryInstance_t) * NUM_INSTANCES);

	buffer_unmap(buf_instances + idx);
	instance_data = NULL;

	VkAccelerationStructureCreateInfoNV accel_create_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV,
		.info = {
			.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV,
			.instanceCount = 1,
			.geometryCount = 0,
			.pGeometries   = NULL,
			.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV
		}
	};

	qvkCreateAccelerationStructureNV(qvk.device, &accel_create_info, NULL, accel_top + idx);

	/* XXX: do allocation only once with safety margin */
	VkAccelerationStructureMemoryRequirementsInfoNV mem_req_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV,
		.accelerationStructure = accel_top[idx],
		.type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV
	};
	VkMemoryRequirements2 mem_req;
	qvkGetAccelerationStructureMemoryRequirementsNV(qvk.device, &mem_req_info, &mem_req);

	VkMemoryAllocateInfo mem_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_req.memoryRequirements.size,
		.memoryTypeIndex = get_memory_type(mem_req.memoryRequirements.memoryTypeBits,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};

	_VK(vkAllocateMemory(qvk.device, &mem_alloc_info, NULL, mem_accel_top + idx));

	VkBindAccelerationStructureMemoryInfoNV bind_info = {
		.sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV,
		.accelerationStructure = accel_top[idx],
		.memory = mem_accel_top[idx],
	};

	_VK(qvkBindAccelerationStructureMemoryNV(qvk.device, 1, &bind_info));

	assert(get_scratch_buffer_size(accel_top[idx]) < SIZE_SCRATCH_BUFFER);

	VkAccelerationStructureInfoNV as_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV,
		.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV,
		.geometryCount = 0,
		.pGeometries = NULL,
		.instanceCount = NUM_INSTANCES,
	};

	qvkCmdBuildAccelerationStructureNV(
			qvk.cmd_buf_current,
			&as_info,
			buf_instances[idx].buffer, /* instance buffer */
			0 /* instance offset */,
			VK_FALSE,  /* update */
			accel_top[idx],
			VK_NULL_HANDLE, /* source acceleration structure ?? */
			buf_accel_scratch.buffer,
			0 /* scratch offset */);

	MEM_BARRIER_BUILD_ACCEL(qvk.cmd_buf_current); /* probably not needed here but doesn't matter */

	return VK_SUCCESS;
}

VkResult
vkpt_pt_record_cmd_buffer(VkCommandBuffer cmd_buf, uint32_t frame_num)
{
	VkImageSubresourceRange subresource_range = {
		.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel   = 0,
		.levelCount     = 1,
		.baseArrayLayer = 0,
		.layerCount     = 1
	};

	IMAGE_BARRIER(cmd_buf,
			.image            = qvk.images[VKPT_IMG_PT_COLOR_A],
			.subresourceRange = subresource_range,
			.srcAccessMask    = 0,
			.dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT,
			.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout        = VK_IMAGE_LAYOUT_GENERAL
	);

	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
			rt_pipeline_layout, 0, 1, rt_descriptor_set + qvk.current_image_index, 0, 0);

	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
			rt_pipeline_layout, 1, 1, qvk.desc_set_ubo + qvk.current_image_index, 0, 0);

	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
			rt_pipeline_layout, 2, 1, &qvk.desc_set_textures, 0, 0);

	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
			rt_pipeline_layout, 3, 1, &qvk.desc_set_vertex_buffer, 0, 0);

	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
			rt_pipeline_layout, 4, 1, qvk.desc_set_light_hierarchy + qvk.current_image_index, 0, 0);

	if(!strcmp(cvar_rtx->string, "on")) {
		vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rt_pipeline_rtx);
		qvkCmdTraceRaysNV(cmd_buf,
				buf_shader_binding_table_rtx.buffer, 0,
				buf_shader_binding_table_rtx.buffer, 2 * rt_properties.shaderGroupHandleSize, rt_properties.shaderGroupHandleSize,
				buf_shader_binding_table_rtx.buffer, 1 * rt_properties.shaderGroupHandleSize, rt_properties.shaderGroupHandleSize,
				VK_NULL_HANDLE, 0, 0,
				qvk.extent.width, qvk.extent.height, 1);
	}
	else {
		vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, rt_pipeline);
		qvkCmdTraceRaysNV(cmd_buf,
				buf_shader_binding_table.buffer, 0,
				buf_shader_binding_table.buffer, 2 * rt_properties.shaderGroupHandleSize, rt_properties.shaderGroupHandleSize,
				buf_shader_binding_table.buffer, 1 * rt_properties.shaderGroupHandleSize, rt_properties.shaderGroupHandleSize,
				VK_NULL_HANDLE, 0, 0,
				qvk.extent.width, qvk.extent.height, 1);
	}


#define BARRIER_COMPUTE(img) \
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
				.dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT, \
				.oldLayout        = VK_IMAGE_LAYOUT_GENERAL, \
				.newLayout        = VK_IMAGE_LAYOUT_GENERAL, \
		); \
	} while(0)

	BARRIER_COMPUTE(qvk.images[VKPT_IMG_PT_COLOR_A]);
	BARRIER_COMPUTE(qvk.images[VKPT_IMG_PT_ALBEDO]);
	BARRIER_COMPUTE(qvk.images[VKPT_IMG_PT_MOTION]);
	BARRIER_COMPUTE(qvk.images[VKPT_IMG_PT_DEPTH_NORMAL_A]);
	BARRIER_COMPUTE(qvk.images[VKPT_IMG_PT_DEPTH_NORMAL_B]);

	return VK_SUCCESS;
}

VkResult
vkpt_pt_destroy()
{
	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
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
	VkPipelineShaderStageCreateInfo shader_stages[] = {
		SHADER_STAGE(QVK_MOD_PATH_TRACER_RGEN,  VK_SHADER_STAGE_RAYGEN_BIT_NV     ),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_RCHIT, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_RMISS, VK_SHADER_STAGE_MISS_BIT_NV       ),
	};

	VkPipelineShaderStageCreateInfo shader_stages_rtx[] = {
		SHADER_STAGE(QVK_MOD_PATH_TRACER_RTX_RGEN,  VK_SHADER_STAGE_RAYGEN_BIT_NV     ),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_RTX_RCHIT, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV),
		SHADER_STAGE(QVK_MOD_PATH_TRACER_RTX_RMISS, VK_SHADER_STAGE_MISS_BIT_NV       ),
	};

	VkRayTracingShaderGroupCreateInfoNV rt_shader_group_info[] = {
		{ 
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
			.generalShader      = 0,
			.closestHitShader   = VK_SHADER_UNUSED_NV,
			.anyHitShader       = VK_SHADER_UNUSED_NV,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		{ 
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV,
			.generalShader      = VK_SHADER_UNUSED_NV,
			.closestHitShader   = 1,
			.anyHitShader       = VK_SHADER_UNUSED_NV,
			.intersectionShader = VK_SHADER_UNUSED_NV
		},
		{ 
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
			.generalShader      = 2,
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

	VkRayTracingPipelineCreateInfoNV rt_pipeline_info_rtx = {
		.sType             = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV,
		.stageCount        = LENGTH(shader_stages_rtx),
		.pStages           = shader_stages_rtx,
		.groupCount        = LENGTH(rt_shader_group_info),
		.pGroups           = rt_shader_group_info,
		.layout            = rt_pipeline_layout,
		.maxRecursionDepth = 1,
	};

	_VK(qvkCreateRayTracingPipelinesNV(qvk.device, NULL, 1, &rt_pipeline_info,     NULL, &rt_pipeline    ));
	_VK(qvkCreateRayTracingPipelinesNV(qvk.device, NULL, 1, &rt_pipeline_info_rtx, NULL, &rt_pipeline_rtx));

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

	/* rtx on */
	_VK(buffer_create(&buf_shader_binding_table_rtx, shader_binding_table_size,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT));

	shader_binding_table = buffer_map(&buf_shader_binding_table_rtx);
	_VK(qvkGetRayTracingShaderGroupHandlesNV(qvk.device, rt_pipeline_rtx, 0, num_groups,
				shader_binding_table_size, shader_binding_table));
	buffer_unmap(&buf_shader_binding_table_rtx);
	shader_binding_table = NULL;

	return VK_SUCCESS;
}

VkResult
vkpt_pt_destroy_pipelines()
{
	buffer_destroy(&buf_shader_binding_table);
	vkDestroyPipeline(qvk.device, rt_pipeline, NULL);

	buffer_destroy(&buf_shader_binding_table_rtx);
	vkDestroyPipeline(qvk.device, rt_pipeline_rtx, NULL);

	return VK_SUCCESS;
}
