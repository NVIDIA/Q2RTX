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

#include "shared/shared.h"
#include "vkpt.h"
#include "vk_util.h"

static const uint32_t THREAD_GROUP_SIZE = 16;
static const uint32_t FILTER_THREAD_GROUP_SIZE = 16;

struct
{
	VkPipeline pipelines[2];
	VkPipelineLayout pipeline_layouts[2];
	VkDescriptorSetLayout descriptor_set_layouts[2];

	VkDescriptorPool descriptor_pool;
	VkDescriptorSet descriptor_sets[2];

	VkImageView depth_image_view;
	VkImageView shadow_image_view;
	VkImageView temp_image_view;
	VkImage temp_image;
	VkImageView output_image_view;
	VkImage output_image;
	VkSampler shadow_sampler;

	VkBuffer host_buffer;
	VkBuffer device_buffer;
	VkDeviceMemory host_buffer_memory;
	VkDeviceMemory device_buffer_memory;
	void* mapped_host_memory;
	size_t device_buffer_size;
	size_t filter_constant_offset;

	cvar_t* density_scale;
	cvar_t* intensity;
	cvar_t* shadow_bias;
	cvar_t* eccentricity;
	cvar_t* enable;

	uint32_t frame_index;
} god_rays;

typedef struct GodRaysConstants
{
	float matWorldToUvzwShadow[16];

	vec4_t noisePattern[4];

	vec4_t world_center;

	vec3_t world_size;
	float intensity;

	vec3_t world_half_size_inv;
	float eccentricity;

	vec3_t lightDirection;
	float shadowBias;

	vec3_t randomOffset;
	float densityScale;

	vec2_t invViewport;
	int viewportSize[2];
} GodRaysConstants;

typedef struct GodRaysFilterConstants
{
	int viewportSize[2];
} GodRaysFilterConstants;

static void create_uniform_buffer();
static void create_image_views();
static void create_pipeline_layouts();
static void create_pipelines();
static void create_descriptor_sets();
static void update_descriptor_sets();

extern cvar_t *physical_sky_space;

VkResult vkpt_initialize_god_rays()
{
	memset(&god_rays, 0, sizeof(god_rays));

	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(qvk.physical_device, &properties);

	god_rays.density_scale = Cvar_Get("gr_density_scale", "-3.0", 0);
	god_rays.intensity = Cvar_Get("gr_intensity", "1.0", 0);
	god_rays.shadow_bias = Cvar_Get("gr_shadow_bias", "20.0", 0);
	god_rays.eccentricity = Cvar_Get("gr_eccentricity", "0.75", 0);
	god_rays.enable = Cvar_Get("gr_enable", "1", 0);

	god_rays.device_buffer_size = align(sizeof(GodRaysConstants), properties.limits.minUniformBufferOffsetAlignment);
	god_rays.device_buffer_size += align(sizeof(GodRaysFilterConstants), properties.limits.minUniformBufferOffsetAlignment);
	god_rays.filter_constant_offset = align(sizeof(GodRaysConstants), properties.limits.minUniformBufferOffsetAlignment);

	create_uniform_buffer();

	return VK_SUCCESS;
}

VkResult vkpt_destroy_god_rays()
{
	vkDestroyBuffer(qvk.device, god_rays.host_buffer, NULL);
	vkDestroyBuffer(qvk.device, god_rays.device_buffer, NULL);
	vkFreeMemory(qvk.device, god_rays.host_buffer_memory, NULL);
	vkFreeMemory(qvk.device, god_rays.device_buffer_memory, NULL);

	vkDestroySampler(qvk.device, god_rays.shadow_sampler, NULL);
	vkDestroyDescriptorPool(qvk.device, god_rays.descriptor_pool, NULL);

	return VK_SUCCESS;
}

VkResult vkpt_god_rays_create_pipelines()
{
	create_pipeline_layouts();
	create_pipelines();
	create_descriptor_sets();
	
	// this is a noop outside a shader reload
	update_descriptor_sets();

	return VK_SUCCESS;
}

VkResult vkpt_god_rays_destroy_pipelines()
{
	for (size_t i = 0; i < LENGTH(god_rays.pipelines); i++) {
		if (god_rays.pipelines[i]) {
			vkDestroyPipeline(qvk.device, god_rays.pipelines[i], NULL);
			god_rays.pipelines[i] = NULL;
		}
	}

	for (size_t i = 0; i < LENGTH(god_rays.pipeline_layouts); i++) {
		if (god_rays.pipeline_layouts[i]) {
			vkDestroyPipelineLayout(qvk.device, god_rays.pipeline_layouts[i], NULL);
			god_rays.pipeline_layouts[i] = NULL;
		}
	}

	for (size_t i = 0; i < LENGTH(god_rays.descriptor_set_layouts); i++) {
		if (god_rays.descriptor_set_layouts[i]) {
			vkDestroyDescriptorSetLayout(qvk.device, god_rays.descriptor_set_layouts[i], NULL);
			god_rays.descriptor_set_layouts[i] = NULL;
		}
	}

	return VK_SUCCESS;
}

VkResult
vkpt_god_rays_update_images()
{
	create_image_views();
	update_descriptor_sets();
	return VK_SUCCESS;
}

VkResult
vkpt_god_rays_noop()
{
	return VK_SUCCESS;
}

void vkpt_record_god_rays_trace_command_buffer(VkCommandBuffer command_buffer)
{
	VkBufferMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = god_rays.device_buffer,
		.size = god_rays.device_buffer_size
	};

	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 0, NULL, 1, &barrier, 0, NULL);

	const VkImageSubresourceRange subresource_range = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.levelCount = 1,
		.layerCount = 1
	};

	VkImageMemoryBarrier pre_barrier = { 0 };

	pre_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	pre_barrier.srcAccessMask = 0;
	pre_barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	pre_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	pre_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	pre_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	pre_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	pre_barrier.image = god_rays.temp_image;
	pre_barrier.subresourceRange = subresource_range;

	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 0, NULL, 0, NULL, 1, &pre_barrier);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, god_rays.pipelines[0]);

	VkDescriptorSet desc_sets[] = {
		god_rays.descriptor_sets[0],
		qvk.desc_set_vertex_buffer,
		qvk.desc_set_ubo
	};

	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, god_rays.pipeline_layouts[0], 0, LENGTH(desc_sets),
		desc_sets, 0, NULL);

	uint32_t group_size = THREAD_GROUP_SIZE;
	uint32_t group_num_x = (qvk.extent.width / 2 + (group_size - 1)) / group_size;
	uint32_t group_num_y = (qvk.extent.height / 2 + (group_size - 1)) / group_size;
	vkCmdDispatch(command_buffer, group_num_x, group_num_y, 1);
}

void vkpt_record_god_rays_filter_command_buffer(VkCommandBuffer command_buffer)
{
	const VkImageSubresourceRange subresource_range = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.levelCount = 1,
		.layerCount = 1
	};

	const VkImageMemoryBarrier pre_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = god_rays.temp_image,
		.subresourceRange = subresource_range
	};

	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 0, NULL, 0, NULL, 1, &pre_barrier);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, god_rays.pipelines[1]);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, god_rays.pipeline_layouts[1], 0, 1,
		&god_rays.descriptor_sets[1], 0, NULL);

	uint32_t group_size = FILTER_THREAD_GROUP_SIZE;
	uint32_t group_num_x = (qvk.extent.width + (group_size - 1)) / group_size;
	uint32_t group_num_y = (qvk.extent.height + (group_size - 1)) / group_size;
	vkCmdDispatch(command_buffer, group_num_x, group_num_y, 1);

	const VkImageMemoryBarrier post_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = god_rays.output_image,
		.subresourceRange = subresource_range
	};

	vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 0, NULL, 0, NULL, 1, &post_barrier);
}

void vkpt_record_god_rays_transfer_command_buffer(
	VkCommandBuffer command_buffer, 
	const sun_light_t* sun_light, 
	const aabb_t* world_aabb,
	const float* proj,
	const float* view, 
	const float* shadowmap_viewproj)
{
	god_rays.frame_index = qvk.current_frame_index;


	const size_t host_offset = god_rays.frame_index * god_rays.device_buffer_size;
	GodRaysConstants* constants = (GodRaysConstants*)((uint8_t*)god_rays.mapped_host_memory + host_offset);
	GodRaysFilterConstants* filter_constants = (GodRaysFilterConstants*)((uint8_t*)god_rays.mapped_host_memory + host_offset + god_rays.filter_constant_offset);

	VectorAdd(world_aabb->mins, world_aabb->maxs, constants->world_center);
	VectorScale(constants->world_center, 0.5f, constants->world_center);
	VectorSubtract(world_aabb->maxs, world_aabb->mins, constants->world_size);
	VectorScale(constants->world_size, 0.5f, constants->world_half_size_inv);
	constants->world_half_size_inv[0] = 1.f / constants->world_half_size_inv[0];
	constants->world_half_size_inv[1] = 1.f / constants->world_half_size_inv[1];
	constants->world_half_size_inv[2] = 1.f / constants->world_half_size_inv[2];

	constants->intensity = max(0.f, god_rays.intensity->value);
	constants->densityScale = powf(10.0f, god_rays.density_scale->value);
	constants->eccentricity = god_rays.eccentricity->value;

	// half resolution
	constants->invViewport[0] = 2.0f / qvk.extent.width;
	constants->invViewport[1] = 2.0f / qvk.extent.height;
	constants->viewportSize[0] = qvk.extent.width / 2;
	constants->viewportSize[1] = qvk.extent.height / 2;

	// Light parameters
	VectorCopy(sun_light->direction, constants->lightDirection);

	// Shadow parameters
	memcpy(constants->matWorldToUvzwShadow, shadowmap_viewproj, 16 * sizeof(float));
	constants->shadowBias = god_rays.shadow_bias->value;

	// Random and noise
	vec3_t random_offset = { crand(), crand(), crand() };
	VectorCopy(random_offset, constants->randomOffset);

	const vec4_t noise_pattern[4] = {
		{ 0.059f, 0.529f, 0.176f, 0.647f },
		{ 0.765f, 0.294f, 0.882f, 0.412f },
		{ 0.235f, 0.706f, 0.118f, 0.588f },
		{ 0.941f, 0.471f, 0.824f, 0.353f },
	};
	memcpy(constants->noisePattern, noise_pattern, sizeof(noise_pattern));

	// Filter constants
	filter_constants->viewportSize[0] = qvk.extent.width;
	filter_constants->viewportSize[1] = qvk.extent.height;

	const VkBufferCopy copy = { host_offset, 0, god_rays.device_buffer_size };
	vkCmdCopyBuffer(command_buffer, god_rays.host_buffer, god_rays.device_buffer, 1, &copy);
}

static void create_uniform_buffer()
{
	const VkBufferCreateInfo host_buffer_create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = god_rays.device_buffer_size * MAX_FRAMES_IN_FLIGHT,
		.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
	};

	const VkBufferCreateInfo device_buffer_create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = god_rays.device_buffer_size,
		.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
	};

	_VK(vkCreateBuffer(qvk.device, &host_buffer_create_info, NULL, &god_rays.host_buffer));
	_VK(vkCreateBuffer(qvk.device, &device_buffer_create_info, NULL, &god_rays.device_buffer));

	VkMemoryRequirements host_buffer_requirements;
	VkMemoryRequirements device_buffer_requirements;
	vkGetBufferMemoryRequirements(qvk.device, god_rays.host_buffer, &host_buffer_requirements);
	vkGetBufferMemoryRequirements(qvk.device, god_rays.device_buffer, &device_buffer_requirements);

	const VkMemoryPropertyFlags host_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
		VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	const VkMemoryPropertyFlags device_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	const uint32_t host_memory_type = get_memory_type(host_buffer_requirements.memoryTypeBits, host_flags);
	const uint32_t device_memory_type = get_memory_type(device_buffer_requirements.memoryTypeBits, device_flags);

	const VkMemoryAllocateInfo host_memory_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = host_buffer_requirements.size,
		.memoryTypeIndex = host_memory_type
	};
	const VkMemoryAllocateInfo device_memory_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = device_buffer_requirements.size,
		.memoryTypeIndex = device_memory_type
	};

	_VK(vkAllocateMemory(qvk.device, &host_memory_allocate_info, NULL, &god_rays.host_buffer_memory));
	_VK(vkAllocateMemory(qvk.device, &device_memory_allocate_info, NULL, &god_rays.device_buffer_memory));

	VkBindBufferMemoryInfo bindings[2] = { 0 };
	bindings[0].sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
	bindings[0].buffer = god_rays.host_buffer;
	bindings[0].memory = god_rays.host_buffer_memory;
	bindings[1].sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
	bindings[1].buffer = god_rays.device_buffer;
	bindings[1].memory = god_rays.device_buffer_memory;

	_VK(vkBindBufferMemory2(qvk.device, LENGTH(bindings), bindings));
	_VK(vkMapMemory(qvk.device, god_rays.host_buffer_memory, 0, host_buffer_create_info.size, 0, &god_rays.mapped_host_memory));
}

static void create_image_views()
{
	god_rays.depth_image_view = qvk.images_views[VKPT_IMG_FLAT_VIEW_DEPTH];
	god_rays.shadow_image_view = vkpt_shadow_map_get_view();

	god_rays.temp_image_view = qvk.images_views[VKPT_IMG_PT_TRANSPARENT];
	god_rays.temp_image = qvk.images[VKPT_IMG_PT_TRANSPARENT];

	god_rays.output_image_view = qvk.images_views[VKPT_IMG_FLAT_COLOR];
	god_rays.output_image = qvk.images[VKPT_IMG_FLAT_COLOR];

	const VkSamplerCreateInfo sampler_create_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.compareEnable = VK_TRUE,
		.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
	};

	_VK(vkCreateSampler(qvk.device, &sampler_create_info, NULL, &god_rays.shadow_sampler));
}

static void create_pipeline_layouts()
{
	VkDescriptorSetLayoutBinding bindings[4] = { 0 };
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[2].binding = 2;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	bindings[2].descriptorCount = 1;
	bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	bindings[3].binding = 3;
	bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[3].descriptorCount = 1;
	bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	const VkDescriptorSetLayoutCreateInfo gather_set_layout_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = LENGTH(bindings),
		.pBindings = bindings
	};

	_VK(vkCreateDescriptorSetLayout(qvk.device, &gather_set_layout_create_info, NULL,
		&god_rays.descriptor_set_layouts[0]));


	VkDescriptorSetLayout desc_set_layouts[] = {
		god_rays.descriptor_set_layouts[0],
		qvk.desc_set_layout_vertex_buffer,
		qvk.desc_set_layout_ubo
	};

	const VkPipelineLayoutCreateInfo layout_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pSetLayouts = desc_set_layouts,
		.setLayoutCount = LENGTH(desc_set_layouts)
	};

	_VK(vkCreatePipelineLayout(qvk.device, &layout_create_info, NULL,
		&god_rays.pipeline_layouts[0]));

	VkDescriptorSetLayoutBinding filter_bindings[4] = { 0 };
	filter_bindings[0].binding = 0;
	filter_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	filter_bindings[0].descriptorCount = 1;
	filter_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	filter_bindings[1].binding = 1;
	filter_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	filter_bindings[1].descriptorCount = 1;
	filter_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	filter_bindings[2].binding = 2;
	filter_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	filter_bindings[2].descriptorCount = 1;
	filter_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	filter_bindings[3].binding = 3;
	filter_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	filter_bindings[3].descriptorCount = 1;
	filter_bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	const VkDescriptorSetLayoutCreateInfo filter_set_layout_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = LENGTH(filter_bindings),
		.pBindings = filter_bindings
	};

	_VK(vkCreateDescriptorSetLayout(qvk.device, &filter_set_layout_create_info, NULL,
		&god_rays.descriptor_set_layouts[1]));

	const VkPipelineLayoutCreateInfo filter_layout_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.pSetLayouts = &god_rays.descriptor_set_layouts[1],
		.setLayoutCount = 1
	};

	_VK(vkCreatePipelineLayout(qvk.device, &filter_layout_create_info, NULL,
		&god_rays.pipeline_layouts[1]));
}

static void create_pipelines()
{
	const VkPipelineShaderStageCreateInfo shader = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = qvk.shader_modules[QVK_MOD_GOD_RAYS_COMP],
		.pName = "main"
	};

	const VkPipelineShaderStageCreateInfo filter_shader = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = qvk.shader_modules[QVK_MOD_GOD_RAYS_FILTER_COMP],
		.pName = "main"
	};

	const VkComputePipelineCreateInfo pipeline_create_infos[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = shader,
			.layout = god_rays.pipeline_layouts[0]
		},
		{
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = filter_shader,
			.layout = god_rays.pipeline_layouts[1]
		},
	};

	_VK(vkCreateComputePipelines(qvk.device, VK_NULL_HANDLE, LENGTH(pipeline_create_infos), pipeline_create_infos,
		NULL, god_rays.pipelines));
}

static void create_descriptor_sets()
{
	const VkDescriptorPoolSize pool_sizes[4] = {
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 3 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 },
	};

	const VkDescriptorPoolCreateInfo pool_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = LENGTH(god_rays.descriptor_sets),
		.poolSizeCount = LENGTH(pool_sizes),
		.pPoolSizes = pool_sizes
	};

	_VK(vkCreateDescriptorPool(qvk.device, &pool_create_info, NULL, &god_rays.descriptor_pool));

	const VkDescriptorSetAllocateInfo allocate_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = god_rays.descriptor_pool,
		.descriptorSetCount = LENGTH(god_rays.descriptor_set_layouts),
		.pSetLayouts = god_rays.descriptor_set_layouts
	};

	_VK(vkAllocateDescriptorSets(qvk.device, &allocate_info, god_rays.descriptor_sets));
}

static void fill_descriptor_writes(VkWriteDescriptorSet* writes,
	VkDescriptorImageInfo* image_infos, VkDescriptorBufferInfo* buffer_infos)
{
	image_infos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_infos[0].imageView = god_rays.depth_image_view;

	image_infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	image_infos[1].imageView = god_rays.shadow_image_view;
	image_infos[1].sampler = god_rays.shadow_sampler;

	image_infos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_infos[2].imageView = god_rays.temp_image_view;

	buffer_infos[0].buffer = god_rays.device_buffer;
	buffer_infos[0].range = sizeof(GodRaysConstants);

	writes[0].dstBinding = 0;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	writes[0].pImageInfo = image_infos;

	writes[1].dstBinding = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[1].pImageInfo = image_infos + 1;

	writes[2].dstBinding = 2;
	writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[2].pImageInfo = image_infos + 2;

	writes[3].dstBinding = 3;
	writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[3].pBufferInfo = buffer_infos;
}

static void fill_filter_descriptor_writes(VkWriteDescriptorSet* writes,
	VkDescriptorImageInfo* image_infos, VkDescriptorBufferInfo* buffer_info)
{
	image_infos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_infos[0].imageView = god_rays.depth_image_view;

	image_infos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_infos[1].imageView = god_rays.temp_image_view;

	image_infos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_infos[2].imageView = god_rays.output_image_view;

	buffer_info[0].buffer = god_rays.device_buffer;
	buffer_info[0].range = sizeof(GodRaysFilterConstants);
	buffer_info[0].offset = god_rays.filter_constant_offset;

	writes[0].dstBinding = 0;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	writes[0].pImageInfo = image_infos;

	writes[1].dstBinding = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	writes[1].pImageInfo = image_infos + 1;

	writes[2].dstBinding = 2;
	writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[2].pImageInfo = image_infos + 2;

	writes[3].dstBinding = 3;
	writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[3].pBufferInfo = buffer_info;
}

static void update_descriptor_sets()
{
	// if we end up here during init before we've called create_image_views(), punt --- we will be called again later
	if (god_rays.depth_image_view == NULL)
		return;

	VkWriteDescriptorSet writes[8] = { 0 };

	for (size_t i = 0; i < 4; i++)
	{
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = god_rays.descriptor_sets[0];
		writes[i].descriptorCount = 1;
	}

	VkDescriptorImageInfo image_infos[3] = { 0 };
	VkDescriptorBufferInfo buffer_info = { 0 };
	fill_descriptor_writes(writes, image_infos, &buffer_info);

	for (size_t i = 4; i < 8; i++)
	{
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = god_rays.descriptor_sets[1];
		writes[i].descriptorCount = 1;
	}

	VkDescriptorImageInfo filter_image_infos[3] = { 0 };
	VkDescriptorBufferInfo filter_buffer_info = { 0 };
	fill_filter_descriptor_writes(writes + 4, filter_image_infos, &filter_buffer_info);

	vkUpdateDescriptorSets(qvk.device, LENGTH(writes), writes, 0, NULL);
}

qboolean vkpt_god_rays_enabled(const sun_light_t* sun_light, int medium)
{
	return god_rays.enable->integer
		&& god_rays.intensity->value > 0.f
		&& sun_light->visible
		&& !physical_sky_space->integer  // god rays look weird in space because they also appear outside of the station
		&& medium == MEDIUM_NONE;  // god rays don't look right under water because they do not reflect or refract
}
