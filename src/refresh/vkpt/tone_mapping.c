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

// ========================================================================= //
//
// This is the CPU-side code for the tone mapper, which is based on part of
// Eilertsen, Mantiuk, and Unger's paper *Real-time noise-aware tone mapping*,
// with some additional modifications that we found useful.
//
// This file shows how the tone mapping pipeline is created and updated, as
// well as part of how the tone mapper can be controlled by the application
// and by CVARs. (For a CVAR reference, see `global_ubo.h`, and in particular,
// those CVARS starting with `tm_`, for `tonemapper_`.)
//
// The tone mapper consists of three compute shaders, a utilities file, and
// a CPU-side code file. For an overview of the tone mapper, see
// `tone_mapping_histogram.comp`.
//
// Here are the functions in this file:
//   VkResult vkpt_tone_mapping_initialize() - creates our pipeline layouts
//
//   VkResult vkpt_tone_mapping_destroy() - destroys our pipeline layouts
//
//   void vkpt_tone_mapping_request_reset() - tells the tone mapper to calculate
// the next tone curve without blending with previous frames' tone curves
//
//   VkResult vkpt_tone_mapping_create_pipelines() - creates our pipelines
//
//   VkResult vkpt_tone_mapping_reset(VkCommandBuffer cmd_buf) - adds commands
// to the command buffer to clear the histogram image
//
//   VkResult vkpt_tone_mapping_destroy_pipelines() - destroys our pipelines
//
//   VkResult vkpt_tone_mapping_record_cmd_buffer(VkCommandBuffer cmd_buf,
// float frame_time) - records the commands to apply tone mapping to the
// VKPT_IMG_TAA_OUTPUT image in-place, given the time between this frame and
// the previous frame.
//   
// ========================================================================= //

#include "vkpt.h"

extern cvar_t *cvar_profiler_scale;

// Here are each of the pipelines we'll be using, followed by an additional
// enum value to count the number of tone mapping pipelines.
enum {
	TONE_MAPPING_HISTOGRAM,
	TONE_MAPPING_CURVE,
	TONE_MAPPING_APPLY_SDR,
	TONE_MAPPING_APPLY_HDR,
	TM_NUM_PIPELINES
};

static VkPipeline       pipelines[TM_NUM_PIPELINES];
static VkPipelineLayout pipeline_layout_tone_mapping_histogram;
static VkPipelineLayout pipeline_layout_tone_mapping_curve;
static VkPipelineLayout pipeline_layout_tone_mapping_apply;
static int reset_required = 1; // If 1, recomputes tone curve based only on this frame

// Creates our pipeline layouts.
VkResult
vkpt_tone_mapping_initialize()
{
	VkDescriptorSetLayout desc_set_layouts[] = {
		qvk.desc_set_layout_ubo, 
		qvk.desc_set_layout_textures,
		qvk.desc_set_layout_vertex_buffer
	};

	VkPushConstantRange push_constant_range_curve = {
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.offset = 0,
		.size = 16*sizeof(float)
	};

	VkPushConstantRange push_constant_range_apply = {
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.offset = 0,
		.size = 3*sizeof(float)
	};

	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_tone_mapping_histogram,
		.setLayoutCount = LENGTH(desc_set_layouts),
		.pSetLayouts = desc_set_layouts,
		.pushConstantRangeCount = 0,
		.pPushConstantRanges = NULL
	);
	ATTACH_LABEL_VARIABLE(pipeline_layout_tone_mapping_histogram, PIPELINE_LAYOUT);

	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_tone_mapping_curve,
		.setLayoutCount = LENGTH(desc_set_layouts),
		.pSetLayouts = desc_set_layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_constant_range_curve
	);
	ATTACH_LABEL_VARIABLE(pipeline_layout_tone_mapping_curve, PIPELINE_LAYOUT);

	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_tone_mapping_apply,
		.setLayoutCount = LENGTH(desc_set_layouts),
		.pSetLayouts = desc_set_layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_constant_range_apply
	);
	ATTACH_LABEL_VARIABLE(pipeline_layout_tone_mapping_apply, PIPELINE_LAYOUT);

	return VK_SUCCESS;
}


// Destroys our pipeline layouts.
VkResult
vkpt_tone_mapping_destroy()
{
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_tone_mapping_histogram, NULL);
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_tone_mapping_curve, NULL);
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_tone_mapping_apply, NULL);
	return VK_SUCCESS;
}


// Tells the tone mapper to calculate the next tone curve without blending with
// previous frames' tone curves.
void
vkpt_tone_mapping_request_reset()
{
	reset_required = 1;
}


// Creates our pipelines.
VkResult
vkpt_tone_mapping_create_pipelines()
{
	VkSpecializationMapEntry specEntries[] = {
		{ .constantID = 0, .offset = 0, .size = sizeof(uint32_t) }
	};

	// "HDR tone mapping" flag
	uint32_t spec_data[] = {
		0,
		1,
	};

	VkSpecializationInfo specInfo_SDR = {.mapEntryCount = 1, .pMapEntries = specEntries, .dataSize = sizeof(uint32_t), .pData = &spec_data[0]};
	VkSpecializationInfo specInfo_HDR = {.mapEntryCount = 1, .pMapEntries = specEntries, .dataSize = sizeof(uint32_t), .pData = &spec_data[1]};

	VkComputePipelineCreateInfo pipeline_info[TM_NUM_PIPELINES] = {
		[TONE_MAPPING_HISTOGRAM] = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE(QVK_MOD_TONE_MAPPING_HISTOGRAM_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_tone_mapping_histogram,
		},
		[TONE_MAPPING_CURVE] = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE(QVK_MOD_TONE_MAPPING_CURVE_COMP, VK_SHADER_STAGE_COMPUTE_BIT),
			.layout = pipeline_layout_tone_mapping_curve,
		},
		[TONE_MAPPING_APPLY_SDR] = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE_SPEC(QVK_MOD_TONE_MAPPING_APPLY_COMP, VK_SHADER_STAGE_COMPUTE_BIT, &specInfo_SDR),
			.layout = pipeline_layout_tone_mapping_apply,
		},
		[TONE_MAPPING_APPLY_HDR] = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = SHADER_STAGE_SPEC(QVK_MOD_TONE_MAPPING_APPLY_COMP, VK_SHADER_STAGE_COMPUTE_BIT, &specInfo_HDR),
			.layout = pipeline_layout_tone_mapping_apply,
		},
	};

	_VK(vkCreateComputePipelines(qvk.device, 0, LENGTH(pipeline_info), pipeline_info, 0, pipelines));

	reset_required = 1;

	return VK_SUCCESS;
}


// Adds commands to the command buffer to clear the histogram image.
VkResult
vkpt_tone_mapping_reset(VkCommandBuffer cmd_buf)
{
	BUFFER_BARRIER(cmd_buf,
		.buffer = qvk.buf_tonemap.buffer,
		.offset = 0,
		.size = VK_WHOLE_SIZE,
		.srcAccessMask = 0,
		.dstAccessMask = 0
	);

	vkCmdFillBuffer(cmd_buf, qvk.buf_tonemap.buffer,
		0, VK_WHOLE_SIZE, 0);

	BUFFER_BARRIER(cmd_buf,
		.buffer = qvk.buf_tonemap.buffer,
		.offset = 0,
		.size = VK_WHOLE_SIZE,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT
	);

	return VK_SUCCESS;
}


// Destroys our pipelines.
VkResult
vkpt_tone_mapping_destroy_pipelines()
{
	for (int i = 0; i < TM_NUM_PIPELINES; i++)
		vkDestroyPipeline(qvk.device, pipelines[i], NULL);
	return VK_SUCCESS;
}


// Shorthand to record a resource barrier on a single image, to prevent threads
// from trying to read from this image before it has been written to by the
// previous stage.
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


// Records the commands to apply tone mapping to the VKPT_IMG_TAA_OUTPUT image
// in-place, given the time between this frame and the previous frame.
VkResult
vkpt_tone_mapping_record_cmd_buffer(VkCommandBuffer cmd_buf, float frame_time)
{
	if (reset_required)
	{
		// Clear the histogram image.
		vkpt_tone_mapping_reset(cmd_buf);
	}

	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo,
		qvk_get_current_desc_set_textures(),
		qvk.desc_set_vertex_buffer
	};
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_TAA_OUTPUT]);


	// Record instructions to run the compute shader that updates the histogram.
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[TONE_MAPPING_HISTOGRAM]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_tone_mapping_histogram, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	vkCmdDispatch(cmd_buf,
		(qvk.extent_taa_output.width + 15) / 16,
		(qvk.extent_taa_output.height + 15) / 16,
		1);

	BUFFER_BARRIER(cmd_buf,
		.buffer = qvk.buf_tonemap.buffer,
		.offset = 0,
		.size = VK_WHOLE_SIZE,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
	);


	// Record instructions to run the compute shader that computes the tone
	// curve and autoexposure constants from the histogram.
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[TONE_MAPPING_CURVE]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_tone_mapping_curve, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	// Compute the push constants for the tone curve generation shader:
	// whether to ignore previous tone curve results, how much to blend this
	// frame's tone curve with the previous frame's tone curve, and the kernel
	// used to filter the tone curve's slopes.
	// This is one of the things we added to Eilertsen's tone mapper, and helps
	// prevent artifacts that occur when the tone curve's slopes become flat or
	// change too suddenly (much like when you specify a curve in an image 
	// processing application that is too intense). This especially helps on
	// shadow edges in some scenes.
	// In addition, we assume the kernel is symmetric; this allows us to only
	// specify half of it in our push constant buffer.
	
	// Note that the second argument of Cvar_Get only specifies the default
	// value in code if none is set; the value of tm_slope_blur_sigma specified
	// in global_ubo.h will override this.
	float slope_blur_sigma = Cvar_Get("tm_slope_blur_sigma", "6.0", 0)->value;
	float push_constants_tm2_curve[16] = {
		 reset_required ? 1.0 : 0.0, // 1 means reset the histogram
		 frame_time, // Frame time
		 0.0, 0.0, 0.0, 0.0, // Slope kernel filter
		 0.0, 0.0, 0.0, 0.0,
		 0.0, 0.0, 0.0, 0.0,
		 0.0, 0.0
	};

	// Compute Gaussian curve and sum, taking symmetry into account.
	float gaussian_sum = 0.0;
	for (int i = 0; i < 14; ++i)
	{
		float kernel_value = exp(-i * i / (2.0 * slope_blur_sigma * slope_blur_sigma));
		gaussian_sum += kernel_value * (i == 0 ? 1 : 2);
		push_constants_tm2_curve[i + 2] = kernel_value;
	}
	// Normalize the result (since even with an analytic normalization factor,
	// the results may not sum to one).
	for (int i = 0; i < 14; ++i) {
		push_constants_tm2_curve[i + 2] /= gaussian_sum;
	}

	vkCmdPushConstants(cmd_buf, pipeline_layout_tone_mapping_curve,
		VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants_tm2_curve), push_constants_tm2_curve);

	vkCmdDispatch(cmd_buf, 1, 1, 1);

	BUFFER_BARRIER(cmd_buf,
		.buffer = qvk.buf_tonemap.buffer,
		.offset = 0,
		.size = VK_WHOLE_SIZE,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT
	);


	// Record instructions to apply our tone curve to the final image, apply
	// the autoexposure tone mapper to the final image, and blend the results
	// of the two techniques.
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[qvk.surf_is_hdr ? TONE_MAPPING_APPLY_HDR : TONE_MAPPING_APPLY_SDR]);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_tone_mapping_apply, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	// At the end of the hue-preserving tone mapper, the luminance of every
	// pixel is mapped to the range [0,1]. However, because this tone
	// mapper adjusts luminance while preserving hue and saturation, the values
	// of some RGB channels may lie outside [0,1]. To finish off the tone
	// mapping pipeline and since we want the brightest colors in the scene to
	// be desaturated a bit for display, we apply a subtle user-configurable
	// Reinhard tone mapping curve to the brighest values in the image at this
	// point, preserving pixels with luminance below tm_knee_start.
	//
	// If we wanted to support an arbitrary SDR color grading pipeline here or
	// implement an additional filmic tone mapping pass, for instance, this is
	// roughly around where it would be applied. For applications that need to
	// output both SDR and HDR images but for which creating custom grades
	// for each format is impractical, one common approach is to
	// (roughly) use the HDR->SDR transformation to map an SDR color grading
	// function back to an HDR color grading function.

	// Defines the white point and where we switch from an identity transform
	// to a Reinhard transform in the additional tone mapper we apply at the
	// end of the previous tone mapping pipeline.
	// Must be between 0 and 1; pixels with luminances above this value have
	// their RGB values slowly clamped to 1, up to tm_white_point.
	float knee_start = Cvar_Get("tm_knee_start", "0.9", 0)->value;
	// Should be greater than 1; defines those RGB values that get mapped to 1.
	float knee_white_point = Cvar_Get("tm_white_point", "10.0", 0)->value;

	// We modify Reinhard to smoothly blend with the identity transform up to tm_knee_start.
	// We need to find w, a, and b such that in y(x) = (wx+a)/(x+b),
	// * y(knee_start) = tm_knee_start
	// * dy/dx(knee_start) = 1
	// * y(knee_white_point) = tm_white_point.
	// The solution is as follows:
	float knee_w = (knee_start*(knee_start - 2.0) + knee_white_point) / (knee_white_point - 1.0);
	float knee_a = -knee_start * knee_start;
	float knee_b = knee_w - 2.0*knee_start;

	float push_constants_tm2_apply[3] = {
		knee_w, // knee_w in piecewise knee adjustment
		knee_a, // knee_a in piecewise knee adjustment
		knee_b, // knee_b in piecewise knee adjustment
	};

	vkCmdPushConstants(cmd_buf, pipeline_layout_tone_mapping_apply,
		VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants_tm2_apply), push_constants_tm2_apply);

	vkCmdDispatch(cmd_buf,
		(qvk.extent_taa_output.width + 15) / 16,
		(qvk.extent_taa_output.height + 15) / 16,
		1);

	// Because VKPT_IMG_TAA_OUTPUT changed, we make sure to wait for the image
	// to be written before continuing. This could be ensured in several
	// other ways as well.
	BARRIER_COMPUTE(cmd_buf, qvk.images[VKPT_IMG_TAA_OUTPUT]);

	reset_required = 0;

	return VK_SUCCESS;
}

void
vkpt_tone_mapping_draw_debug()
{
	float profiler_scale = R_ClampScale(cvar_profiler_scale);
	int x = (int)(10.f * profiler_scale);
	int y = (int)((float)qvk.extent_unscaled.height * profiler_scale) - 10;

	qhandle_t font;
	font = R_RegisterFont("conchars");
	if(!font)
		return;

	R_SetScale(profiler_scale);

	if(vkpt_refdef.fd)
	{
		char buf[256];
		snprintf(buf, sizeof buf, "Adapted luminance: %8.6f (ev: %f)", vkpt_refdef.fd->feedback.adapted_luminance, log2f(vkpt_refdef.fd->feedback.adapted_luminance));
		R_DrawString(x, y, 0, 128, buf, font);
	}

	R_SetScale(1.0f);
}
