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

#include "vkpt.h"
#include "system/system.h"

enum {
	BLUR,
	COMPOSITE,
	DOWNSCALE,

	BLOOM_NUM_PIPELINES
};

static VkPipeline pipelines[BLOOM_NUM_PIPELINES];
static VkPipelineLayout pipeline_layout_blur;
static VkPipelineLayout pipeline_layout_composite;

struct bloom_blur_push_constants {
	float pixstep_x;
	float pixstep_y;
	float argument_scale;
	float normalization_scale;
	int num_samples;
	int pass;
};

static struct bloom_blur_push_constants push_constants_hblur;
static struct bloom_blur_push_constants push_constants_vblur;

cvar_t *cvar_bloom_enable = NULL;
cvar_t *cvar_bloom_debug = NULL;
cvar_t *cvar_bloom_sigma = NULL;
cvar_t *cvar_bloom_intensity = NULL;
cvar_t *cvar_bloom_sigma_water = NULL;
cvar_t *cvar_bloom_intensity_water = NULL;

static float bloom_intensity;
static float bloom_sigma;
static float under_water_animation;

static void compute_push_constants(void)
{
	float sigma_pixels = bloom_sigma * (float)(qvk.extent_taa_output.height);

	float effective_sigma = sigma_pixels * 0.25f;
	effective_sigma = min(effective_sigma, 100.f);
	effective_sigma = max(effective_sigma, 1.f);

	push_constants_hblur.pixstep_x = 1.f;
	push_constants_hblur.pixstep_y = 0.f;
	push_constants_hblur.argument_scale = -1.f / (2.0 * effective_sigma * effective_sigma);
	push_constants_hblur.normalization_scale = 1.f / (sqrtf(2 * M_PI) * effective_sigma);
	push_constants_hblur.num_samples = roundf(effective_sigma * 4.f);
	push_constants_hblur.pass = 0;

	push_constants_vblur = push_constants_hblur;
	push_constants_vblur.pixstep_x = 0.f;
	push_constants_vblur.pixstep_y = 1.f;
	push_constants_vblur.pass = 1;
}
void vkpt_bloom_reset()
{
	bloom_intensity = cvar_bloom_intensity->value;
	bloom_sigma = cvar_bloom_sigma->value;
	under_water_animation = 0.f;
}

static float mix(float a, float b, float s)
{
	return a * (1.f - s) + b * s;
}

void vkpt_bloom_update(QVKUniformBuffer_t * ubo, float frame_time, bool under_water, bool menu_mode)
{
	if (under_water)
	{
		under_water_animation = min(1.f, under_water_animation + frame_time * 3.f);
		bloom_intensity = cvar_bloom_intensity_water->value;
		bloom_sigma = cvar_bloom_sigma->value;
	}
	else
	{
		under_water_animation = max(0.f, under_water_animation - frame_time * 3.f);
		bloom_intensity = mix(cvar_bloom_intensity->value, cvar_bloom_intensity_water->value, under_water_animation);
		bloom_sigma = mix(cvar_bloom_sigma->value, cvar_bloom_sigma_water->value, under_water_animation);
	}

	static uint32_t menu_start_ms = 0;

	if (menu_mode)
	{
		if (menu_start_ms == 0)
			menu_start_ms = Sys_Milliseconds();
		uint32_t current_ms = Sys_Milliseconds();

		float phase = max(0.f, min(1.f, (float)(current_ms - menu_start_ms) / 150.f));
		ubo->tonemap_hdr_clamp_strength = phase; // Clamp color in HDR mode, to ensure menu is legible
		phase = powf(phase, 0.25f);

		bloom_sigma = phase * 0.03f;

		ubo->bloom_intensity = 1.f;
	}
	else
	{
		menu_start_ms = 0;

		ubo->bloom_intensity = bloom_intensity;
		ubo->tonemap_hdr_clamp_strength = 0.f;
	}
}

VkResult
vkpt_bloom_initialize()
{
	cvar_bloom_enable = Cvar_Get("bloom_enable", "1", 0);
	cvar_bloom_debug = Cvar_Get("bloom_debug", "0", 0);
	cvar_bloom_sigma = Cvar_Get("bloom_sigma", "0.037", 0); // relative to screen height
	cvar_bloom_intensity = Cvar_Get("bloom_intensity", "0.002", 0);
	cvar_bloom_sigma_water = Cvar_Get("bloom_sigma_water", "0.037", 0);
	cvar_bloom_intensity_water = Cvar_Get("bloom_intensity_water", "0.2", 0);

	VkDescriptorSetLayout desc_set_layouts[] = {
		qvk.desc_set_layout_ubo, qvk.desc_set_layout_textures,
		qvk.desc_set_layout_vertex_buffer
	};

	VkPushConstantRange push_constant_range = {
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.offset     = 0,
		.size       = sizeof(push_constants_hblur)
	};

	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_blur,
		.setLayoutCount         = LENGTH(desc_set_layouts),
		.pSetLayouts            = desc_set_layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges    = &push_constant_range
	);
	ATTACH_LABEL_VARIABLE(pipeline_layout_blur, PIPELINE_LAYOUT);

	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_composite,
		.setLayoutCount         = LENGTH(desc_set_layouts),
		.pSetLayouts            = desc_set_layouts,
	);
	ATTACH_LABEL_VARIABLE(pipeline_layout_composite, PIPELINE_LAYOUT);

	return VK_SUCCESS;
}

VkResult
vkpt_bloom_destroy()
{
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_blur, NULL);
	pipeline_layout_blur = NULL;
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_composite, NULL);
	pipeline_layout_composite = NULL;

	return VK_SUCCESS;
}

VkResult
vkpt_bloom_create_pipelines()
{
	VkComputePipelineCreateInfo pipeline_info[BLOOM_NUM_PIPELINES] = {
		[BLUR] = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE(QVK_MOD_BLOOM_BLUR_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_blur,
		},
		[COMPOSITE] = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE(QVK_MOD_BLOOM_COMPOSITE_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_composite,
		},
		[DOWNSCALE] = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE(QVK_MOD_BLOOM_DOWNSCALE_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_composite,
		}
	};

	_VK(vkCreateComputePipelines(qvk.device, 0, LENGTH(pipeline_info), pipeline_info, 0, pipelines));
	return VK_SUCCESS;
}

VkResult
vkpt_bloom_destroy_pipelines()
{
	for(int i = 0; i < BLOOM_NUM_PIPELINES; i++)
		vkDestroyPipeline(qvk.device, pipelines[i], NULL);

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
				.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT, \
				.oldLayout        = VK_IMAGE_LAYOUT_GENERAL, \
				.newLayout        = VK_IMAGE_LAYOUT_GENERAL, \
		); \
	} while(0)

#define BARRIER_TO_COPY_DEST(cmd_buf, img) \
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
				.srcAccessMask    = 0, \
				.dstAccessMask    = 0, \
				.oldLayout        = VK_IMAGE_LAYOUT_GENERAL, \
				.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, \
		); \
	} while(0)

#define BARRIER_FROM_COPY_DEST(cmd_buf, img) \
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
				.srcAccessMask    = 0, \
				.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT, \
				.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, \
				.newLayout        = VK_IMAGE_LAYOUT_GENERAL, \
		); \
	} while(0)

static void bloom_debug_show_image(VkCommandBuffer cmd_buf, int vis_img)
{
	VkImageSubresourceLayers subresource = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel = 0,
		.baseArrayLayer = 0,
		.layerCount = 1
	};

	VkOffset3D offset_UL = {
		.x = 0,
		.y = 0,
		.z = 0
	};

	VkOffset3D offset_UL_input = {
		.x = IMG_WIDTH / 4,
		.y = IMG_HEIGHT / 4,
		.z = 1
	};

	VkOffset3D offset_LR_input = {
		.x = IMG_WIDTH,
		.y = IMG_HEIGHT,
		.z = 1
	};

	VkImageBlit blit_region = {
		.srcSubresource = subresource,
		.srcOffsets[0] = offset_UL,
		.srcOffsets[1] = offset_UL_input,
		.dstSubresource = subresource,
		.dstOffsets[0] = offset_UL,
		.dstOffsets[1] = offset_LR_input,
	};

	BARRIER_TO_COPY_DEST(cmd_buf, qvk.images[VKPT_IMG_TAA_OUTPUT]);

	vkCmdBlitImage(cmd_buf,
		qvk.images[vis_img], VK_IMAGE_LAYOUT_GENERAL,
		qvk.images[VKPT_IMG_TAA_OUTPUT], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &blit_region, VK_FILTER_LINEAR);

	BARRIER_FROM_COPY_DEST(cmd_buf, qvk.images[VKPT_IMG_TAA_OUTPUT]);
}

VkResult
vkpt_bloom_record_cmd_buffer(VkCommandBuffer cmd_buf)
{
	VkExtent2D extent = qvk.extent_taa_output;

	compute_push_constants();

	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo,
		qvk_get_current_desc_set_textures(),
		qvk.desc_set_vertex_buffer
	};

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_TAA_OUTPUT]);

	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[DOWNSCALE]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_composite, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	vkCmdDispatch(cmd_buf,
		(extent.width / 4 + 15) / 16,
		(extent.height / 4 + 15) / 16,
		1);

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_BLOOM_VBLUR]);

	if(cvar_bloom_debug->integer == 1)
	{
		bloom_debug_show_image(cmd_buf, VKPT_IMG_BLOOM_VBLUR);
		return VK_SUCCESS;
	}

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_BLOOM_HBLUR]);

	// apply horizontal blur from BLOOM_VBLUR -> BLOOM_HBLUR
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[BLUR]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_blur, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	vkCmdPushConstants(cmd_buf, pipeline_layout_blur, VK_SHADER_STAGE_COMPUTE_BIT,
		0, sizeof(push_constants_hblur), &push_constants_hblur);
	vkCmdDispatch(cmd_buf,
		(extent.width / 4 + 15) / 16,
		(extent.height / 4 + 15) / 16,
		1);

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_BLOOM_HBLUR]);

	if(cvar_bloom_debug->integer == 2)
	{
		bloom_debug_show_image(cmd_buf, VKPT_IMG_BLOOM_HBLUR);
		return VK_SUCCESS;
	}

	// vertical blur from BLOOM_HBLUR -> BLOOM_VBLUR
	vkCmdPushConstants(cmd_buf, pipeline_layout_blur, VK_SHADER_STAGE_COMPUTE_BIT,
		0, sizeof(push_constants_vblur), &push_constants_vblur);
	vkCmdDispatch(cmd_buf,
		(extent.width / 4 + 15) / 16,
		(extent.height / 4 + 15) / 16,
		1);

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_BLOOM_VBLUR]);

	if(cvar_bloom_debug->integer == 3)
	{
		bloom_debug_show_image(cmd_buf, VKPT_IMG_BLOOM_VBLUR);
		return VK_SUCCESS;
	}

	// composite bloom into TAA_OUTPUT
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[COMPOSITE]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_composite, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	vkCmdDispatch(cmd_buf,
		(extent.width + 15) / 16,
		(extent.height + 15) / 16,
		1);

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_BLOOM_VBLUR]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_TAA_OUTPUT]);


	return VK_SUCCESS;
}
