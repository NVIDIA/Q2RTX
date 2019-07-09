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

enum {
	SEED_RNG,
	FWD_PROJECT,
	GRADIENT_IMAGE,
	GRADIENT_ATROUS,
	TEMPORAL,
	ATROUS_LF,
	ATROUS_ITER_0,
	ATROUS_ITER_1,
	ATROUS_ITER_2,
	ATROUS_ITER_3,
	TAA,
	CHECKERBOARD_INTERLEAVE,
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
	VkSpecializationMapEntry specEntries[] = {
		{ .constantID = 0, .offset = 0, .size = sizeof(uint32_t) }
	};

	uint32_t spec_data[] = { 
		0, 
		1, 
		2, 
		3
	};

	VkSpecializationInfo specInfo[] = {
		{ .mapEntryCount = 1, .pMapEntries = specEntries, .dataSize = sizeof(uint32_t), .pData = &spec_data[0] },
		{ .mapEntryCount = 1, .pMapEntries = specEntries, .dataSize = sizeof(uint32_t), .pData = &spec_data[1] },
		{ .mapEntryCount = 1, .pMapEntries = specEntries, .dataSize = sizeof(uint32_t), .pData = &spec_data[2] },
		{ .mapEntryCount = 1, .pMapEntries = specEntries, .dataSize = sizeof(uint32_t), .pData = &spec_data[3] },
	};

	VkComputePipelineCreateInfo pipeline_info[ASVGF_NUM_PIPELINES] = {
		[SEED_RNG] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_ASVGF_SEED_RNG_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_atrous,
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
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE(QVK_MOD_ASVGF_GRADIENT_ATROUS_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_atrous,
		},
		[TEMPORAL] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_ASVGF_TEMPORAL_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_general,
		},
		[ATROUS_LF] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_ASVGF_LF_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_atrous,
		},
		[ATROUS_ITER_0] = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE_SPEC(QVK_MOD_ASVGF_ATROUS_COMP, VK_SHADER_STAGE_COMPUTE_BIT, &specInfo[0]),
			.layout = pipeline_layout_atrous,
		},
		[ATROUS_ITER_1] = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE_SPEC(QVK_MOD_ASVGF_ATROUS_COMP, VK_SHADER_STAGE_COMPUTE_BIT, &specInfo[1]),
			.layout = pipeline_layout_atrous,
		},
		[ATROUS_ITER_2] = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE_SPEC(QVK_MOD_ASVGF_ATROUS_COMP, VK_SHADER_STAGE_COMPUTE_BIT, &specInfo[2]),
			.layout = pipeline_layout_atrous,
		},
		[ATROUS_ITER_3] = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE_SPEC(QVK_MOD_ASVGF_ATROUS_COMP, VK_SHADER_STAGE_COMPUTE_BIT, &specInfo[3]),
			.layout = pipeline_layout_atrous,
		},
		[CHECKERBOARD_INTERLEAVE] = {
			.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage  = SHADER_STAGE(QVK_MOD_CHECKERBOARD_INTERLEAVE_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_general,
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

#define BARRIER_TO_CLEAR(cmd_buf, img) \
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
				.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED, \
				.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, \
		); \
	} while(0)

#define BARRIER_FROM_CLEAR(cmd_buf, img) \
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


VkResult
vkpt_asvgf_create_gradient_samples(VkCommandBuffer cmd_buf, uint32_t frame_num, int do_gradient_samples)
{
	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo,
		qvk_get_current_desc_set_textures(),
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

	int current_sample_pos_image = VKPT_IMG_ASVGF_GRAD_SMPL_POS_A + (qvk.frame_counter & 1);

	BARRIER_TO_CLEAR(cmd_buf, qvk.images[current_sample_pos_image]);
	vkCmdClearColorImage(cmd_buf, qvk.images[current_sample_pos_image],
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_grd_smpl_pos, 1, &subresource_range);
	BARRIER_FROM_CLEAR(cmd_buf, qvk.images[current_sample_pos_image]);
	
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_RNG_SEED_A + (qvk.frame_counter & 1)]);

	for(uint32_t gpu = 0; gpu < qvk.device_count; gpu++)
	{
		set_current_gpu(cmd_buf, gpu);

		uint32_t push_constants[1] = {
			gpu
		};

		vkCmdPushConstants(cmd_buf, pipeline_layout_atrous,
			VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), push_constants);

		vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[SEED_RNG]);
		vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
			pipeline_layout_general, 0, LENGTH(desc_sets), desc_sets, 0, 0);
		vkCmdDispatch(cmd_buf,
			(qvk.gpu_slice_width + 15) / 16,
			(qvk.extent.height + 15) / 16,
			1);
	}

	set_current_gpu(cmd_buf, ALL_GPUS);

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_RNG_SEED_A + (qvk.frame_counter & 1)]);

	if (do_gradient_samples)
	{
		BEGIN_PERF_MARKER(cmd_buf, PROFILER_ASVGF_DO_GRADIENT_SAMPLES);

		vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[FWD_PROJECT]);
		vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
			pipeline_layout_general, 0, LENGTH(desc_sets), desc_sets, 0, 0);
		vkCmdDispatch(cmd_buf,
			(qvk.gpu_slice_width / GRAD_DWN + 15) / 16,
			(qvk.extent.height / GRAD_DWN + 15) / 16,
			1);

		BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_RNG_SEED_A + (qvk.frame_counter & 1)]);
		BARRIER_COMPUTE(cmd_buf, qvk.images[current_sample_pos_image]);

		END_PERF_MARKER(cmd_buf, PROFILER_ASVGF_DO_GRADIENT_SAMPLES);
	}

	return VK_SUCCESS;
}

VkResult
vkpt_asvgf_filter(VkCommandBuffer cmd_buf, qboolean enable_lf)
{
	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo,
		qvk_get_current_desc_set_textures(),
		qvk.desc_set_vertex_buffer
	};
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_COLOR_LF_SH]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_COLOR_LF_COCG]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_COLOR_HF]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_PT_COLOR_SPEC]);

	BEGIN_PERF_MARKER(cmd_buf, PROFILER_ASVGF_RECONSTRUCT_GRADIENT);

	/* create gradient image */
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[GRADIENT_IMAGE]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_atrous, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	vkCmdDispatch(cmd_buf,
			(qvk.gpu_slice_width / GRAD_DWN + 15) / 16,
			(qvk.extent.height / GRAD_DWN + 15) / 16,
			1);

	// XXX BARRIERS!!!
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_GRAD_LF_PING]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_GRAD_HF_SPEC_PING]);

	//vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[GRADIENT_ATROUS]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_atrous, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	/* reconstruct gradient image */
	const int num_atrous_iterations_gradient = 7;
	for(int i = 0; i < num_atrous_iterations_gradient; i++) {
		uint32_t push_constants[1] = {
			i
		};

		vkCmdPushConstants(cmd_buf, pipeline_layout_atrous,
			VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), push_constants);

		vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[GRADIENT_ATROUS]);

		vkCmdDispatch(cmd_buf,
				(qvk.gpu_slice_width / GRAD_DWN + 15) / 16,
				(qvk.extent.height / GRAD_DWN + 15) / 16,
				1);
		BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_GRAD_LF_PING + !(i & 1)]);
		BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_GRAD_HF_SPEC_PING + !(i & 1)]);

	}

	END_PERF_MARKER(cmd_buf, PROFILER_ASVGF_RECONSTRUCT_GRADIENT);
	BEGIN_PERF_MARKER(cmd_buf, PROFILER_ASVGF_TEMPORAL);

	/* temporal accumulation / filtering */
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[TEMPORAL]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_atrous, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	vkCmdDispatch(cmd_buf,
			(qvk.gpu_slice_width + 14) / 15,
			(qvk.extent.height + 14) / 15,
			1);


	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_LF_SH]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_LF_COCG]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_HF]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_MOMENTS]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_FILTERED_SPEC_A + (qvk.frame_counter & 1)]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_HIST_MOMENTS_HF_A]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_HIST_MOMENTS_HF_B]);

	END_PERF_MARKER(cmd_buf, PROFILER_ASVGF_TEMPORAL);
	BEGIN_PERF_MARKER(cmd_buf, PROFILER_ASVGF_ATROUS);

	//vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[ATROUS]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_atrous, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	/* spatial reconstruction filtering */
	const int num_atrous_iterations = 4;
	for(int i = 0; i < num_atrous_iterations; i++) 
	{
		if (enable_lf)
		{
			uint32_t push_constants[1] = {
				i
			};

			vkCmdPushConstants(cmd_buf, pipeline_layout_atrous,
				VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants), push_constants);

			vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[ATROUS_LF]);

			vkCmdDispatch(cmd_buf,
				(qvk.gpu_slice_width / GRAD_DWN + 15) / 16,
				(qvk.extent.height / GRAD_DWN + 15) / 16,
				1);

			if (i == num_atrous_iterations - 1)
			{
				BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_LF_SH]);
				BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_LF_COCG]);
			}
		}

		int specialization = ATROUS_ITER_0 + i;

		vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[specialization]);
		vkCmdDispatch(cmd_buf,
				(qvk.gpu_slice_width + 15) / 16,
				(qvk.extent.height + 15) / 16,
				1);

		BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_LF_SH]);
		BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_LF_COCG]);
		BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_HF]);
		BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_MOMENTS]);
		BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PONG_LF_SH]);
		BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PONG_LF_COCG]);
		BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PONG_HF]);
		BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PONG_MOMENTS]);
		BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_HIST_COLOR_HF]);
		BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_LF_SH + !(i & 1)]);
		BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_ATROUS_PING_LF_COCG + !(i & 1)]);
	}

	END_PERF_MARKER(cmd_buf, PROFILER_ASVGF_ATROUS);

	return VK_SUCCESS;
}

VkResult 
vkpt_interleave(VkCommandBuffer cmd_buf)
{
	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo,
		qvk_get_current_desc_set_textures(),
		qvk.desc_set_vertex_buffer
	};

#ifdef VKPT_DEVICE_GROUPS
	if (qvk.device_count > 1) {
		BEGIN_PERF_MARKER(cmd_buf, PROFILER_MGPU_TRANSFERS);

		// create full interleaved motion and color buffers on GPU 0
		VkOffset2D offset_left = { 0, 0 };
		VkOffset2D offset_right = { qvk.extent.width / 2, 0 };
		VkExtent2D extent = { qvk.extent.width / 2, qvk.extent.height };

		vkpt_mgpu_image_copy(cmd_buf,
							VKPT_IMG_PT_MOTION,
							VKPT_IMG_PT_MOTION,
							1,
							0,
							offset_left,
							offset_right,
							extent);
		
		vkpt_mgpu_image_copy(cmd_buf,
							VKPT_IMG_ASVGF_COLOR,
							VKPT_IMG_ASVGF_COLOR,
							1,
							0,
							offset_left,
							offset_right,
							extent);

		int frame_idx = qvk.frame_counter & 1;

		vkpt_mgpu_image_copy(cmd_buf,
							VKPT_IMG_PT_VIEW_DEPTH_A + frame_idx,
							VKPT_IMG_PT_VIEW_DEPTH_A + frame_idx,
							1,
							0,
							offset_left,
							offset_right,
							extent);

		vkpt_mgpu_global_barrier(cmd_buf);
		
		END_PERF_MARKER(cmd_buf, PROFILER_MGPU_TRANSFERS);
	}
#endif
	
	BEGIN_PERF_MARKER(cmd_buf, PROFILER_INTERLEAVE);

	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[CHECKERBOARD_INTERLEAVE]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_general, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	set_current_gpu(cmd_buf, 0);

	vkCmdDispatch(cmd_buf,
		(qvk.extent.width + 15) / 16,
		(qvk.extent.height + 15) / 16,
		1);

	END_PERF_MARKER(cmd_buf, PROFILER_INTERLEAVE);

	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_FLAT_COLOR]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_FLAT_MOTION]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_FLAT_VIEW_DEPTH]);

	return VK_SUCCESS;
}

VkResult vkpt_taa(VkCommandBuffer cmd_buf)
{
	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo,
		qvk_get_current_desc_set_textures(),
		qvk.desc_set_vertex_buffer
	};

	BEGIN_PERF_MARKER(cmd_buf, PROFILER_ASVGF_TAA);

	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_asvgf[TAA]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_taa, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	vkCmdDispatch(cmd_buf,
			(qvk.extent.width + 15) / 16,
			(qvk.extent.height + 15) / 16,
			1);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_TAA_A]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_TAA_B]);
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_ASVGF_TAA_B]);

	END_PERF_MARKER(cmd_buf, PROFILER_ASVGF_TAA);

	return VK_SUCCESS;
}
