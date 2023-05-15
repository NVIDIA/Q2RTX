/*
Copyright (C) 2018 Christoph Schied
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.
Copyright (C) 2021, Frank Richter. All rights reserved.

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

/*
	FidelityFX Super Resolution 1.0 ("FSR") implementation overview
	===============================================================

	FSR is a combination of an upscaling and a sharpening algorithm to produce
	high-resolution output - of high quality and visually appealing -
	from lower resolution input.

	The input to FSR is the rendered image, in perceptual color space
	(after tone mapping, sRGB), pre-antialiased, without HUD elements.
	In our case the output from TAA fits this nicely.

	Step 1 is upscaling the input image to the output resolution
	using the EASU ("Edge Adaptive Spatial Upsampling") algorithm.
	This is implemented in the "fsr_easu" shader.
	Shader inputs are various render dimensions, found in the global 'qvk'
	structure.

	Step 2 is sharpening the image using the RCAS
	("Robust Contrast Adaptive Sharpening") algorithm.
	This is implemented in the "fsr_rcas" shader.
	Shader input is a "sharpness" value controlled by a cvar.

	For more details see the official documentation:
	https://gpuopen.com/fidelityfx-superresolution/

	Q2RTX cvars
	-----------
	* flt_fsr_enable - 0 = disable FSR, 1 = enable FSR.
	* flt_fsr_sharpness - float in the range [0,2]
		Default is 0.2, a recommendation from the docs.
	* flt_fsr_easu, flt_fsr_rcas - individual toggles for EASU and
	  RCAS steps.
	  The official docs ask nicely to only call it FSR when
	  both EASU and RCAS are used, so these options are not
	  exposed via the settings UI. These cvars are mainly provided
	  for testing purposes.

	Q2RTX FSR shaders
	-----------------
	* Shader sources are the shader/fsr_* files, which in turn include
	  the headers in fsr/. Those headers actually contain the bulk of
	  the implementation (and are used from both C and GLSL).
	* Shaders are compiled to FP16 and FP32 variants. FP16 is recommended
	  for best performance. However, there's actually hardware that can
	  run Q2RTX but doesn't have FP16 shader support (GTX 10 series),
	  so FP32 versions are provided for compatibility.

 */

#if defined(__GNUC__)
// FSR headers define a lot of functions that aren't used
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define A_CPU
#include "fsr/ffx_a.h"
#include "fsr/ffx_fsr1.h"

enum {
	FSR_EASU_TO_RCAS,
	FSR_EASU_TO_DISPLAY,
	FSR_RCAS_AFTER_EASU,
	FSR_RCAS_AFTER_TAAU,
	FSR_NUM_PIPELINES
};

static VkPipeline       pipeline_fsr_sdr[FSR_NUM_PIPELINES];
static VkPipeline       pipeline_fsr_hdr[FSR_NUM_PIPELINES];
static VkPipelineLayout pipeline_layout_fsr;

cvar_t *cvar_flt_fsr_enable = NULL;
cvar_t *cvar_flt_fsr_easu = NULL;
cvar_t *cvar_flt_fsr_rcas = NULL;
cvar_t *cvar_flt_fsr_sharpness = NULL;

void vkpt_fsr_init_cvars()
{
	// FSR enable toggle
	cvar_flt_fsr_enable = Cvar_Get("flt_fsr_enable", "0", CVAR_ARCHIVE);
	// FSR EASU (upscaling) toggle
	cvar_flt_fsr_easu = Cvar_Get("flt_fsr_easu", "1", CVAR_ARCHIVE);
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
	for (unsigned int hdr = 0; hdr <= 1; hdr++) {
		VkPipeline* pipeline_fsr = hdr != 0 ? pipeline_fsr_hdr : pipeline_fsr_sdr;

		uint32_t spec_data[] = {
			hdr, 0,
			hdr, 1
		};

		VkSpecializationMapEntry specEntries[] = {
			{.constantID = 0, .offset = 0, .size = sizeof(uint32_t)},
			{.constantID = 1, .offset = 4, .size = sizeof(uint32_t)}
		};

		VkSpecializationInfo specInfoInput[] = {
			{ .mapEntryCount = 2, .pMapEntries = specEntries, .dataSize = sizeof(uint32_t) * 2, .pData = &spec_data[0] },
			{ .mapEntryCount = 2, .pMapEntries = specEntries, .dataSize = sizeof(uint32_t) * 2, .pData = &spec_data[2] },
		};

		enum QVK_SHADER_MODULES qvk_mod_easu = qvk.supports_fp16 ? QVK_MOD_FSR_EASU_FP16_COMP : QVK_MOD_FSR_EASU_FP32_COMP;
		enum QVK_SHADER_MODULES qvk_mod_rcas = qvk.supports_fp16 ? QVK_MOD_FSR_RCAS_FP16_COMP : QVK_MOD_FSR_RCAS_FP32_COMP;
		VkComputePipelineCreateInfo pipeline_info[FSR_NUM_PIPELINES] = {
			[FSR_EASU_TO_RCAS] = {
				.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
				.stage  = SHADER_STAGE_SPEC(qvk_mod_easu, VK_SHADER_STAGE_COMPUTE_BIT, &specInfoInput[0]),
				.layout = pipeline_layout_fsr,
			},
			[FSR_EASU_TO_DISPLAY] = {
				.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
				.stage  = SHADER_STAGE_SPEC(qvk_mod_easu, VK_SHADER_STAGE_COMPUTE_BIT, &specInfoInput[1]),
				.layout = pipeline_layout_fsr,
			},
			[FSR_RCAS_AFTER_EASU] = {
				.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
				.stage  = SHADER_STAGE_SPEC(qvk_mod_rcas, VK_SHADER_STAGE_COMPUTE_BIT, &specInfoInput[0]),
				.layout = pipeline_layout_fsr,
			},
			[FSR_RCAS_AFTER_TAAU] = {
				.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
				.stage  = SHADER_STAGE_SPEC(qvk_mod_rcas, VK_SHADER_STAGE_COMPUTE_BIT, &specInfoInput[1]),
				.layout = pipeline_layout_fsr,
			},
		};

		_VK(vkCreateComputePipelines(qvk.device, 0, LENGTH(pipeline_info), pipeline_info, 0, pipeline_fsr));
	}

	return VK_SUCCESS;
}
	
VkResult
vkpt_fsr_destroy_pipelines()
{
	for(int i = 0; i < FSR_NUM_PIPELINES; i++) {
		vkDestroyPipeline(qvk.device, pipeline_fsr_sdr[i], NULL);
		vkDestroyPipeline(qvk.device, pipeline_fsr_hdr[i], NULL);
	}
	return VK_SUCCESS;
}

bool vkpt_fsr_is_enabled()
{
	if (cvar_flt_fsr_enable->integer == 0)
		return false;

	if ((cvar_flt_fsr_enable->integer == 1)
		&& (qvk.extent_render.width >= qvk.extent_unscaled.width || qvk.extent_render.height >= qvk.extent_unscaled.height))
	{
		// Only apply when upscaling by default (but allow tweaking this from the console)
		return false;
	}

	// Need one of EASU or RCAS enabled
	return (cvar_flt_fsr_easu->integer != 0) || (cvar_flt_fsr_rcas->integer != 0);
}

bool vkpt_fsr_needs_upscale()
{
	return cvar_flt_fsr_easu->integer == 0;
}

void vkpt_fsr_update_ubo(QVKUniformBuffer_t *ubo)
{
	// Set shader constants for FSR upscaling (EASU) pass
	FsrEasuCon(&ubo->easu_const0[0], &ubo->easu_const1[0], &ubo->easu_const2[0], &ubo->easu_const3[0],
			   qvk.extent_render.width, qvk.extent_render.height,	   // render dimensions
			   IMG_WIDTH_TAA, IMG_HEIGHT_TAA,						   // container texture dimensions
			   qvk.extent_unscaled.width, qvk.extent_unscaled.height); // display dimensions

	// Set shader constants for FSR sharpening (RCAS) pass
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

	VkPipeline* pipeline_fsr = qvk.surf_is_hdr ? pipeline_fsr_hdr : pipeline_fsr_sdr;
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_fsr[cvar_flt_fsr_rcas->integer != 0 ? FSR_EASU_TO_RCAS : FSR_EASU_TO_DISPLAY]);

	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_fsr, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	VkExtent2D dispatch_size = qvk.extent_unscaled;

	// Dispatch size as described by the FSR Integration Overview
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

	VkPipeline* pipeline_fsr = qvk.surf_is_hdr ? pipeline_fsr_hdr : pipeline_fsr_sdr;
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_fsr[cvar_flt_fsr_easu->integer != 0 ? FSR_RCAS_AFTER_EASU : FSR_RCAS_AFTER_TAAU]);

	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout_fsr, 0, LENGTH(desc_sets), desc_sets, 0, 0);

	VkExtent2D dispatch_size = qvk.extent_unscaled;

	// Dispatch size as described by the FSR Integration Overview
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

	if(cvar_flt_fsr_easu->integer != 0)
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
