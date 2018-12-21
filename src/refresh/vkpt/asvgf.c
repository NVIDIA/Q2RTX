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

enum {
	SEED_RNG,
	FWD_PROJECT,
	GRADIENT_IMAGE,
	GRADIENT_ATROUS,
	TEMPORAL,
	ATROUS,
	TAA,
	ASVGF_NUM_PIPELINES
};

static VkPipeline       pipeline_asvgf[ASVGF_NUM_PIPELINES];
static VkPipelineLayout pipeline_layout_atrous;
static VkPipelineLayout pipeline_layout_general;
static VkPipelineLayout pipeline_layout_taa;

VkResult
vkpt_asvgf_initialize()
{
	VkDescriptorSetLayout desc_set_layouts[] = {
		qvk.desc_set_layout_ubo, qvk.desc_set_layout_textures,
		qvk.desc_set_layout_vertex_buffer
	};

	VkPushConstantRange push_constant_range_atrous = {
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.offset     = 0,
		.size       = sizeof(uint32_t)
	};

	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_atrous, 
		.setLayoutCount         = LENGTH(desc_set_layouts),
		.pSetLayouts            = desc_set_layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges    = &push_constant_range_atrous
	);
	ATTACH_LABEL_VARIABLE(pipeline_layout_atrous, PIPELINE_LAYOUT);

	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_general, 
		.setLayoutCount         = LENGTH(desc_set_layouts),
		.pSetLayouts            = desc_set_layouts,
	);
	ATTACH_LABEL_VARIABLE(pipeline_layout_general, PIPELINE_LAYOUT);

	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_taa, 
		.setLayoutCount         = LENGTH(desc_set_layouts),
		.pSetLayouts            = desc_set_layouts,
	);
	ATTACH_LABEL_VARIABLE(pipeline_layout_taa, PIPELINE_LAYOUT);
	return VK_SUCCESS;
}

VkResult
vkpt_asvgf_destroy()
{
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_atrous,     NULL);
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_general,   NULL);
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_taa,   NULL);
	return VK_SUCCESS;
}

VkResult
vkpt_asvgf_create_pipelines()
{
	VkComputePipelineCreateInfo pipeline_info[ASVGF_NUM_PIPELINES] = {
		[SEED_RNG] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_ASVGF_SEED_RNG_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_general,
		},
		[FWD_PROJECT] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_ASVGF_FWD_PROJECT_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_general,
		},
		[GRADIENT_IMAGE] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_ASVGF_GRADIENT_IMG_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_general,
		},
		[GRADIENT_ATROUS] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_ASVGF_GRADIENT_ATROUS_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_atrous,
		},
		[TEMPORAL] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_ASVGF_TEMPORAL_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_general,
		},
		[ATROUS] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_ASVGF_ATROUS_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_atrous,
		},
		[TAA] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_ASVGF_TAA_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_general,
		},
	};

	_VK(vkCreateComputePipelines(qvk.device, 0, LENGTH(pipeline_info), pipeline_info, 0, pipeline_asvgf));

	return VK_SUCCESS;
}
	
VkResult
vkpt_asvgf_destroy_pipelines()
{
	for(int i = 0; i < ASVGF_NUM_PIPELINES; i++)
		vkDestroyPipeline(qvk.device, pipeline_asvgf[i], NULL);
	return VK_SUCCESS;
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
		IMAGE_BARRIER(qvk.cmd_buf_current, \
				.image            = img, \
				.subresourceRange = subresource_range, \
				.srcAccessMask    = VK_ACCESS_SHADER_WRITE_BIT, \
				.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT, \
				.oldLayout        = VK_IMAGE_LAYOUT_GENERAL, \
				.newLayout        = VK_IMAGE_LAYOUT_GENERAL, \
		); \
	} while(0)


VkResult
vkpt_asvgf_create_gradient_samples(VkCommandBuffer cmd_buf, uint32_t frame_num)
{
	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo[qvk.current_image_index],
		qvk.desc_set_textures,
		qvk.desc_set_vertex_buffer
	};
	VkClearColorValue clear_grd_smpl_pos = {
		.uint32 = { 0, 0, 0, 0 }
	};

	VkImageSubresourceRange subresource_range = {
		.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel   = 0,
		.levelCount     = 1,
		.baseArrayLayer = 0,
		.layerCount     = 1
	};

	vkCmdClearColorImage(cmd_buf, qvk.images[VKPT_IMG_ASVGF_GRAD_SMPL_POS],
			VK_IMAGE_LAYOUT_GENERAL, &clear_grd_smpl_pos, 1, &subresource_range);

	vkCmdClearColorImage(cmd_buf, qvk.images[VKPT_IMG_DEBUG],
			VK_IMAGE_LAYOUT_GENERAL, &clear_grd_smpl_pos, 1, &subresource_range);

	BARRIER_COMPUTE(qvk.images[VKPT_IMG_ASVGF_RNG_SEED_A + (qvk.frame_counter & 1)]);

	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[SEED_RNG]);
	vkCmdBindDescriptorSets(qvk.cmd_buf_current, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_general, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	vkCmdDispatch(cmd_buf,
			(qvk.extent.width + 31) / 32,
			(qvk.extent.height + 31) / 32,
			1);

	BARRIER_COMPUTE(qvk.images[VKPT_IMG_ASVGF_RNG_SEED_A + (qvk.frame_counter & 1)]);


	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[FWD_PROJECT]);
	vkCmdBindDescriptorSets(qvk.cmd_buf_current, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_general, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	vkCmdDispatch(cmd_buf,
			(qvk.extent.width / GRAD_DWN  + 31) / 32,
			(qvk.extent.height / GRAD_DWN + 31) / 32,
			1);

	BARRIER_COMPUTE(qvk.images[VKPT_IMG_ASVGF_RNG_SEED_A + (qvk.frame_counter & 1)]);
	BARRIER_COMPUTE(qvk.images[VKPT_IMG_ASVGF_GRAD_SMPL_POS]);

	return VK_SUCCESS;
}

VkResult
vkpt_asvgf_record_cmd_buffer(VkCommandBuffer cmd_buf)
{
	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo[qvk.current_image_index],
		qvk.desc_set_textures,
		qvk.desc_set_vertex_buffer
	};
	BARRIER_COMPUTE(qvk.images[VKPT_IMG_PT_COLOR_A]);
	BARRIER_COMPUTE(qvk.images[VKPT_IMG_PT_COLOR_B]);

	_VK(vkpt_profiler_query(PROFILER_ASVGF_RECONSTRUCT_GRADIENT, PROFILER_START));

	/* create gradient image */
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[GRADIENT_IMAGE]);
	vkCmdBindDescriptorSets(qvk.cmd_buf_current, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_atrous, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	vkCmdDispatch(cmd_buf,
			(qvk.extent.width  / GRAD_DWN + 31) / 32,
			(qvk.extent.height / GRAD_DWN + 31) / 32,
			1);

	// XXX BARRIERS!!!
	BARRIER_COMPUTE(qvk.images[VKPT_IMG_ASVGF_GRAD_A]);
	BARRIER_COMPUTE(qvk.images[VKPT_IMG_DEBUG]);

	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[GRADIENT_ATROUS]);
	vkCmdBindDescriptorSets(qvk.cmd_buf_current, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_atrous, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	/* reconstruct gradient image */
	const int num_atrous_iterations_gradient = 5;
	for(int i = 0; i < num_atrous_iterations_gradient; i++) {
		uint32_t push_constants[1] = {
			i
		};

		vkCmdPushConstants(cmd_buf, pipeline_layout_atrous,
			VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), push_constants);

		vkCmdDispatch(cmd_buf,
				(qvk.extent.width  / GRAD_DWN + 31) / 32,
				(qvk.extent.height / GRAD_DWN + 31) / 32,
				1);
		BARRIER_COMPUTE(qvk.images[VKPT_IMG_ASVGF_GRAD_A + !(i & 1)]);

	}

	_VK(vkpt_profiler_query(PROFILER_ASVGF_RECONSTRUCT_GRADIENT, PROFILER_STOP));


	/* temporal accumulation / filtering */
	_VK(vkpt_profiler_query(PROFILER_ASVGF_TEMPORAL, PROFILER_START));
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[TEMPORAL]);
	vkCmdBindDescriptorSets(qvk.cmd_buf_current, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_atrous, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	vkCmdDispatch(cmd_buf,
			(qvk.extent.width + 31) / 32,
			(qvk.extent.height + 31) / 32,
			1);


	BARRIER_COMPUTE(qvk.images[VKPT_IMG_ASVGF_ATROUS_PING]);
	BARRIER_COMPUTE(qvk.images[VKPT_IMG_ASVGF_HIST_MOMENTS_A]);
	BARRIER_COMPUTE(qvk.images[VKPT_IMG_ASVGF_HIST_MOMENTS_B]);
	_VK(vkpt_profiler_query(PROFILER_ASVGF_TEMPORAL, PROFILER_STOP));

	_VK(vkpt_profiler_query(PROFILER_ASVGF_ATROUS, PROFILER_START));

	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[ATROUS]);
	vkCmdBindDescriptorSets(qvk.cmd_buf_current, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_atrous, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	/* spatial reconstruction filtering */
	const int num_atrous_iterations = 5;
	for(int i = 0; i < num_atrous_iterations; i++) {
		uint32_t push_constants[1] = {
			i
		};

		vkCmdPushConstants(cmd_buf, pipeline_layout_atrous,
			VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), push_constants);

		vkCmdDispatch(cmd_buf,
				(qvk.extent.width + 31) / 32,
				(qvk.extent.height + 31) / 32,
				1);
		BARRIER_COMPUTE(qvk.images[VKPT_IMG_ASVGF_ATROUS_PING]);
		BARRIER_COMPUTE(qvk.images[VKPT_IMG_ASVGF_ATROUS_PONG]);
		BARRIER_COMPUTE(qvk.images[VKPT_IMG_ASVGF_HIST_COLOR]);
	}

	_VK(vkpt_profiler_query(PROFILER_ASVGF_ATROUS, PROFILER_STOP));

	_VK(vkpt_profiler_query(PROFILER_ASVGF_TAA, PROFILER_START));

	/* taa */
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[TAA]);
	vkCmdBindDescriptorSets(qvk.cmd_buf_current, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_taa, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	vkCmdDispatch(cmd_buf,
			(qvk.extent.width + 31) / 32,
			(qvk.extent.height + 31) / 32,
			1);
	BARRIER_COMPUTE(qvk.images[VKPT_IMG_ASVGF_TAA_A]);
	BARRIER_COMPUTE(qvk.images[VKPT_IMG_ASVGF_TAA_B]);

	_VK(vkpt_profiler_query(PROFILER_ASVGF_TAA, PROFILER_STOP));

	return VK_SUCCESS;
}
