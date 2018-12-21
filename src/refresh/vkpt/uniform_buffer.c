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

#include "vkpt.h"

#include <assert.h>

static BufferResource_t uniform_buffers[MAX_SWAPCHAIN_IMAGES];
static VkDescriptorPool desc_pool_ubo;

VkResult
vkpt_uniform_buffer_create()
{
	VkDescriptorSetLayoutBinding ubo_layout_binding = {
		.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.binding         = GLOBAL_UBO_BINDING_IDX,
		.stageFlags      = VK_SHADER_STAGE_ALL,
	};

	VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings    = &ubo_layout_binding,
	};

	_VK(vkCreateDescriptorSetLayout(qvk.device, &layout_info, NULL, &qvk.desc_set_layout_ubo));

	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		buffer_create(uniform_buffers + i, sizeof(QVKUniformBuffer_t),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	}

	VkDescriptorPoolSize pool_size = {
		.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = qvk.num_swap_chain_images,
	};

	VkDescriptorPoolCreateInfo pool_info = {
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = 1,
		.pPoolSizes    = &pool_size,
		.maxSets       = qvk.num_swap_chain_images,
	};

	_VK(vkCreateDescriptorPool(qvk.device, &pool_info, NULL, &desc_pool_ubo));

	VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = desc_pool_ubo,
		.descriptorSetCount = 1,
		.pSetLayouts = &qvk.desc_set_layout_ubo,
	};

	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		_VK(vkAllocateDescriptorSets(qvk.device, &descriptor_set_alloc_info, qvk.desc_set_ubo + i));
		BufferResource_t *ubo = uniform_buffers + i;

		VkDescriptorBufferInfo buf_info = {
			.buffer = ubo->buffer,
			.offset = 0,
			.range  = sizeof(QVKUniformBuffer_t),
		};

		VkWriteDescriptorSet output_buf_write = {
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = qvk.desc_set_ubo[i],
			.dstBinding      = 0,
			.dstArrayElement = 0,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.pBufferInfo     = &buf_info,
		};

		vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);
	}

	return VK_SUCCESS;
}

VkResult
vkpt_uniform_buffer_destroy()
{
	vkDestroyDescriptorPool(qvk.device, desc_pool_ubo, NULL);
	vkDestroyDescriptorSetLayout(qvk.device, qvk.desc_set_layout_ubo, NULL);
	desc_pool_ubo = VK_NULL_HANDLE;
	qvk.desc_set_layout_ubo = VK_NULL_HANDLE;

	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		buffer_destroy(uniform_buffers + i);
	}

	return VK_SUCCESS;
}

VkResult
vkpt_uniform_buffer_update()
{
	BufferResource_t *ubo = uniform_buffers + qvk.current_image_index;
	assert(ubo);
	assert(ubo->memory != VK_NULL_HANDLE);
	assert(ubo->buffer != VK_NULL_HANDLE);
	assert(qvk.current_image_index < qvk.num_swap_chain_images);

	QVKUniformBuffer_t *mapped_ubo = buffer_map(ubo);
	assert(mapped_ubo);
	memcpy(mapped_ubo, &vkpt_refdef.uniform_buffer, sizeof(QVKUniformBuffer_t));
	buffer_unmap(ubo);
	mapped_ubo = NULL;

	return VK_SUCCESS;
}


// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
