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

#define A_CPU
#include "fsr/ffx_a.h"
#include "fsr/ffx_fsr1.h"

enum {
	FSR_EASU,
	FSR_RCAS,
	FSR_NUM_PIPELINES
};

static VkPipeline       pipeline_fsr[FSR_NUM_PIPELINES];
static VkPipelineLayout pipeline_layout_fsr;

cvar_t *cvar_flt_fsr_enable = NULL;
cvar_t *cvar_flt_fsr_rcas = NULL;
cvar_t *cvar_flt_fsr_sharpness = NULL;

void vkpt_fsr_init_cvars()
{
	// FSR enable toggle
	cvar_flt_fsr_enable = Cvar_Get("flt_fsr_enable", "0", CVAR_ARCHIVE);
	// FSR RCAS (sharpening) toggle
	cvar_flt_fsr_rcas = Cvar_Get("flt_fsr_rcas", "1", CVAR_ARCHIVE);
	// FSR sharpness setting (float, 0..2)
	cvar_flt_fsr_sharpness = Cvar_Get("flt_fsr_sharpness", "0.2", CVAR_ARCHIVE);
}

VkResult
vkpt_fsr_initialize()
{
	VkDescriptorSetLayout desc_set_layouts[] = {
		qvk.desc_set_layout_ubo, qvk.desc_set_layout_textures
	};

	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_fsr, 
		.setLayoutCount         = LENGTH(desc_set_layouts),
		.pSetLayouts            = desc_set_layouts,
	);
	ATTACH_LABEL_VARIABLE(pipeline_layout_fsr, PIPELINE_LAYOUT);

	return VK_SUCCESS;
}

VkResult
vkpt_fsr_destroy()
{
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_fsr, NULL);

	return VK_SUCCESS;
}

VkResult
vkpt_fsr_create_pipelines()
{
	VkComputePipelineCreateInfo pipeline_info[FSR_NUM_PIPELINES] = {
		[FSR_EASU] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_FSR_EASU_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_fsr,
		},
		[FSR_RCAS] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_FSR_RCAS_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_fsr,
		},
	};

	_VK(vkCreateComputePipelines(qvk.device, 0, LENGTH(pipeline_info), pipeline_info, 0, pipeline_fsr));

	return VK_SUCCESS;
}
	
VkResult
vkpt_fsr_destroy_pipelines()
{
	for(int i = 0; i < FSR_NUM_PIPELINES; i++)
		vkDestroyPipeline(qvk.device, pipeline_fsr[i], NULL);
	return VK_SUCCESS;
}

void vkpt_fsr_update_ubo(QVKUniformBuffer_t *ubo)
{
	FsrEasuCon(&ubo->easu_const0[0], &ubo->easu_const1[0], &ubo->easu_const2[0], &ubo->easu_const3[0],
			   qvk.extent_render.width, qvk.extent_render.height,	   // render dimensions
			   IMG_WIDTH_TAA, IMG_HEIGHT_TAA,						   // container texture dimensions
			   qvk.extent_unscaled.width, qvk.extent_unscaled.height); // display dimensions
	FsrRcasCon(&ubo->rcas_const0[0], cvar_flt_fsr_sharpness->value);
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

static void vkpt_fsr_easu(VkCommandBuffer cmd_buf)
{
	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo,
		qvk_get_current_desc_set_textures()
	};

	BEGIN_PERF_MARKER(cmd_buf, PROFILER_FSR_EASU);

	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_fsr[FSR_EASU]);

	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_fsr, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	VkExtent2D dispatch_size = qvk.extent_unscaled;

	vkCmdDispatch(cmd_buf,
			(dispatch_size.width + 15) / 16,
			(dispatch_size.height + 15) / 16,
			1);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_FSR_EASU_OUTPUT]);

	END_PERF_MARKER(cmd_buf, PROFILER_FSR_EASU);
}

static void vkpt_fsr_rcas(VkCommandBuffer cmd_buf)
{
	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo,
		qvk_get_current_desc_set_textures()
	};

	BEGIN_PERF_MARKER(cmd_buf, PROFILER_FSR_RCAS);

	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_fsr[FSR_RCAS]);

	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_fsr, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	VkExtent2D dispatch_size = qvk.extent_unscaled;

	vkCmdDispatch(cmd_buf,
			(dispatch_size.width + 15) / 16,
			(dispatch_size.height + 15) / 16,
			1);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_FSR_RCAS_OUTPUT]);

	END_PERF_MARKER(cmd_buf, PROFILER_FSR_RCAS);
}

VkResult vkpt_fsr_do(VkCommandBuffer cmd_buf)
{
	BEGIN_PERF_MARKER(cmd_buf, PROFILER_FSR);

	vkpt_fsr_easu(cmd_buf);
	if(cvar_flt_fsr_rcas->integer != 0)
		vkpt_fsr_rcas(cmd_buf);

	END_PERF_MARKER(cmd_buf, PROFILER_FSR);

	return VK_SUCCESS;
}

VkResult vkpt_fsr_final_blit(VkCommandBuffer cmd_buf)
{
	int output_image = cvar_flt_fsr_rcas->integer != 0 ? VKPT_IMG_FSR_RCAS_OUTPUT : VKPT_IMG_FSR_EASU_OUTPUT;
	return vkpt_final_blit_simple(cmd_buf, qvk.images[output_image], qvk.extent_unscaled);
}
