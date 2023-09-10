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
#include "refresh/refresh.h"
#include "client/client.h"
#include "refresh/images.h"

#include <assert.h>

#include "vkpt.h"
#include "shader/global_textures.h"

enum {
	STRETCH_PIC_SDR,
	STRETCH_PIC_HDR,
	STRETCH_PIC_NUM_PIPELINES
};

#define TEXNUM_WHITE (~0)
#define MAX_STRETCH_PICS (1<<14)

static drawStatic_t draw = {
	.scale = 1.0f,
	.alpha_scale = 1.0f
};

static int num_stretch_pics = 0;
typedef struct {
	float x, y, w,   h;
	float s, t, w_s, h_t;
	uint32_t color, tex_handle;
} StretchPic_t;

// Not using global UBO b/c it's only filled when a world is drawn, but here we need it all the time
typedef struct {
	float ui_hdr_nits;
	float tm_hdr_saturation_scale;
} StretchPic_UBO_t;

static clipRect_t clip_rect;
static bool clip_enable = false;

static StretchPic_t stretch_pic_queue[MAX_STRETCH_PICS];

static VkPipelineLayout        pipeline_layout_stretch_pic;
static VkPipelineLayout        pipeline_layout_final_blit;
static VkRenderPass            render_pass_stretch_pic;
static VkPipeline              pipeline_stretch_pic[STRETCH_PIC_NUM_PIPELINES];
static VkPipeline              pipeline_final_blit;
static VkFramebuffer*          framebuffer_stretch_pic = NULL;
static BufferResource_t        buf_stretch_pic_queue[MAX_FRAMES_IN_FLIGHT];
static BufferResource_t        buf_ubo[MAX_FRAMES_IN_FLIGHT];
static VkDescriptorSetLayout   desc_set_layout_sbo;
static VkDescriptorSetLayout   desc_set_layout_ubo;
static VkDescriptorPool        desc_pool_sbo;
static VkDescriptorPool        desc_pool_ubo;
static VkDescriptorSet         desc_set_sbo[MAX_FRAMES_IN_FLIGHT];
static VkDescriptorSet         desc_set_ubo[MAX_FRAMES_IN_FLIGHT];

extern cvar_t* cvar_ui_hdr_nits;
extern cvar_t* cvar_tm_hdr_saturation_scale;

VkExtent2D
vkpt_draw_get_extent(void)
{
	return qvk.extent_unscaled;
}

static inline void enqueue_stretch_pic(
		float x, float y, float w, float h,
		float s1, float t1, float s2, float t2,
		uint32_t color, int tex_handle)
{
	if (draw.alpha_scale == 0.f)
		return;

	if(num_stretch_pics == MAX_STRETCH_PICS) {
		Com_EPrintf("Error: stretch pic queue full!\n");
		assert(0);
		return;
	}
	assert(tex_handle);
	StretchPic_t *sp = stretch_pic_queue + num_stretch_pics++;

	if (clip_enable)
	{
		if (x >= clip_rect.right || x + w <= clip_rect.left || y >= clip_rect.bottom || y + h <= clip_rect.top)
			return;

		if (x < clip_rect.left)
		{
			float dw = clip_rect.left - x;
			s1 += dw / w * (s2 - s1);
			w -= dw;
			x = clip_rect.left;

			if (w <= 0) return;
		}

		if (x + w > clip_rect.right)
		{
			float dw = (x + w) - clip_rect.right;
			s2 -= dw / w * (s2 - s1);
			w -= dw;

			if (w <= 0) return;
		}

		if (y < clip_rect.top)
		{
			float dh = clip_rect.top - y;
			t1 += dh / h * (t2 - t1);
			h -= dh;
			y = clip_rect.top;

			if (h <= 0) return;
		}

		if (y + h > clip_rect.bottom)
		{
			float dh = (y + h) - clip_rect.bottom;
			t2 -= dh / h * (t2 - t1);
			h -= dh;

			if (h <= 0) return;
		}
	}

	float width = r_config.width * draw.scale;
	float height = r_config.height * draw.scale;

	x = 2.0f * x / width - 1.0f;
	y = 2.0f * y / height - 1.0f;

	w = 2.0f * w / width;
	h = 2.0f * h / height;

	sp->x = x;
	sp->y = y;
	sp->w = w;
	sp->h = h;

	sp->s   = s1;
	sp->t   = t1;
	sp->w_s = s2 - s1;
	sp->h_t = t2 - t1;

	if (draw.alpha_scale < 1.f)
	{
		float alpha = (color >> 24) & 0xff;
		alpha *= draw.alpha_scale;
		alpha = max(0.f, min(255.f, alpha));
		color = (color & 0xffffff) | ((int)(alpha) << 24);
	}

	sp->color = color;
	sp->tex_handle = tex_handle;
	if(tex_handle >= 0 && tex_handle < MAX_RIMAGES
	&& !r_images[tex_handle].registration_sequence) {
		sp->tex_handle = TEXNUM_WHITE;
	}
}

static void
create_render_pass(void)
{
	LOG_FUNC();
	VkAttachmentDescription color_attachment = {
		.format         = qvk.surf_format.format,
		.samples        = VK_SAMPLE_COUNT_1_BIT,
		.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD,
		//.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		//.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
		.initialLayout  = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};

	VkAttachmentReference color_attachment_ref = {
		.attachment = 0, /* index in fragment shader */
		.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	VkSubpassDescription subpass = {
		.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments    = &color_attachment_ref,
	};

	VkSubpassDependency dependencies[] = {
		{
			.srcSubpass    = VK_SUBPASS_EXTERNAL,
			.dstSubpass    = 0, /* index for own subpass */
			.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0, /* XXX verify */
			.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT
			               | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		},
	};

	VkRenderPassCreateInfo render_pass_info = {
		.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments    = &color_attachment,
		.subpassCount    = 1,
		.pSubpasses      = &subpass,
		.dependencyCount = LENGTH(dependencies),
		.pDependencies   = dependencies,
	};

	_VK(vkCreateRenderPass(qvk.device, &render_pass_info, NULL, &render_pass_stretch_pic));
	ATTACH_LABEL_VARIABLE(render_pass_stretch_pic, RENDER_PASS);
}

VkResult
vkpt_draw_initialize()
{
	num_stretch_pics = 0;
	LOG_FUNC();
	create_render_pass();
	for(int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		_VK(buffer_create(buf_stretch_pic_queue + i, sizeof(StretchPic_t) * MAX_STRETCH_PICS, 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

		_VK(buffer_create(buf_ubo + i, sizeof(StretchPic_UBO_t),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
	}

	VkDescriptorSetLayoutBinding layout_bindings[] = {
		{
			.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.binding         = 0,
			.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT,
		},
	};

	VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = LENGTH(layout_bindings),
		.pBindings    = layout_bindings,
	};

	_VK(vkCreateDescriptorSetLayout(qvk.device, &layout_info, NULL, &desc_set_layout_sbo));
	ATTACH_LABEL_VARIABLE(desc_set_layout_sbo, DESCRIPTOR_SET_LAYOUT);

	VkDescriptorPoolSize pool_size = {
		.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.descriptorCount = MAX_FRAMES_IN_FLIGHT,
	};

	VkDescriptorPoolCreateInfo pool_info = {
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = 1,
		.pPoolSizes    = &pool_size,
		.maxSets       = MAX_FRAMES_IN_FLIGHT,
	};

	_VK(vkCreateDescriptorPool(qvk.device, &pool_info, NULL, &desc_pool_sbo));
	ATTACH_LABEL_VARIABLE(desc_pool_sbo, DESCRIPTOR_POOL);

	VkDescriptorSetLayoutBinding layout_bindings_ubo[] = {
		{
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.binding         = 2,
			.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	};

	VkDescriptorSetLayoutCreateInfo layout_info_ubo = {
		.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = LENGTH(layout_bindings_ubo),
		.pBindings    = layout_bindings_ubo,
	};

	_VK(vkCreateDescriptorSetLayout(qvk.device, &layout_info_ubo, NULL, &desc_set_layout_ubo));
	ATTACH_LABEL_VARIABLE(desc_set_layout_ubo, DESCRIPTOR_SET_LAYOUT);

	VkDescriptorPoolSize pool_size_ubo = {
		.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = MAX_FRAMES_IN_FLIGHT,
	};

	VkDescriptorPoolCreateInfo pool_info_ubo = {
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = 1,
		.pPoolSizes    = &pool_size_ubo,
		.maxSets       = MAX_FRAMES_IN_FLIGHT,
	};

	_VK(vkCreateDescriptorPool(qvk.device, &pool_info_ubo, NULL, &desc_pool_ubo));
	ATTACH_LABEL_VARIABLE(desc_pool_ubo, DESCRIPTOR_POOL);


	VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {
		.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool     = desc_pool_sbo,
		.descriptorSetCount = 1,
		.pSetLayouts        = &desc_set_layout_sbo,
	};

	VkDescriptorSetAllocateInfo descriptor_set_alloc_info_ubo = {
		.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool     = desc_pool_ubo,
		.descriptorSetCount = 1,
		.pSetLayouts        = &desc_set_layout_ubo,
	};

	for(int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		_VK(vkAllocateDescriptorSets(qvk.device, &descriptor_set_alloc_info, desc_set_sbo + i));
		BufferResource_t *sbo = buf_stretch_pic_queue + i;

		VkDescriptorBufferInfo buf_info = {
			.buffer = sbo->buffer,
			.offset = 0,
			.range  = sizeof(stretch_pic_queue),
		};

		VkWriteDescriptorSet output_buf_write = {
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = desc_set_sbo[i],
			.dstBinding      = 0,
			.dstArrayElement = 0,
			.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.pBufferInfo     = &buf_info,
		};

		vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);

		_VK(vkAllocateDescriptorSets(qvk.device, &descriptor_set_alloc_info_ubo, desc_set_ubo + i));
		BufferResource_t *ubo = buf_ubo + i;

		VkDescriptorBufferInfo buf_info_ubo = {
			.buffer = ubo->buffer,
			.offset = 0,
			.range  = sizeof(StretchPic_UBO_t),
		};

		VkWriteDescriptorSet output_buf_write_ubo = {
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = desc_set_ubo[i],
			.dstBinding      = 2,
			.dstArrayElement = 0,
			.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.pBufferInfo     = &buf_info_ubo,
		};

		vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write_ubo, 0, NULL);
	}
	return VK_SUCCESS;
}

VkResult
vkpt_draw_destroy()
{
	LOG_FUNC();
	for(int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		buffer_destroy(buf_stretch_pic_queue + i);
		buffer_destroy(buf_ubo + i);
	}
	vkDestroyRenderPass(qvk.device, render_pass_stretch_pic, NULL);
	vkDestroyDescriptorPool(qvk.device, desc_pool_sbo, NULL);
	vkDestroyDescriptorSetLayout(qvk.device, desc_set_layout_sbo, NULL);
	vkDestroyDescriptorPool(qvk.device, desc_pool_ubo, NULL);
	vkDestroyDescriptorSetLayout(qvk.device, desc_set_layout_ubo, NULL);

	return VK_SUCCESS;
}

VkResult
vkpt_draw_destroy_pipelines()
{
	LOG_FUNC();
	for(int i = 0; i < STRETCH_PIC_NUM_PIPELINES; i++) {
		vkDestroyPipeline(qvk.device, pipeline_stretch_pic[i], NULL);
	}
	vkDestroyPipeline(qvk.device, pipeline_final_blit, NULL);
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_stretch_pic, NULL);
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_final_blit, NULL);
	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		vkDestroyFramebuffer(qvk.device, framebuffer_stretch_pic[i], NULL);
	}
	free(framebuffer_stretch_pic);
	framebuffer_stretch_pic = NULL;
	
	return VK_SUCCESS;
}

VkResult
vkpt_draw_create_pipelines()
{
	LOG_FUNC();

	assert(desc_set_layout_sbo);
	VkDescriptorSetLayout desc_set_layouts[] = {
		desc_set_layout_sbo, qvk.desc_set_layout_textures, desc_set_layout_ubo
	};
	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_stretch_pic, 
		.setLayoutCount = LENGTH(desc_set_layouts),
		.pSetLayouts    = desc_set_layouts
	);

	desc_set_layouts[0] = qvk.desc_set_layout_ubo;

	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_final_blit,
		.setLayoutCount = LENGTH(desc_set_layouts),
		.pSetLayouts = desc_set_layouts
	);

	VkSpecializationMapEntry specEntries[] = {
		{ .constantID = 0, .offset = 0, .size = sizeof(uint32_t) }
	};

	// "HDR display" flag
	uint32_t spec_data[] = {
		0,
		1,
	};

	VkSpecializationInfo specInfo_SDR = {.mapEntryCount = 1, .pMapEntries = specEntries, .dataSize = sizeof(uint32_t), .pData = &spec_data[0]};
	VkSpecializationInfo specInfo_HDR = {.mapEntryCount = 1, .pMapEntries = specEntries, .dataSize = sizeof(uint32_t), .pData = &spec_data[1]};

	VkPipelineShaderStageCreateInfo shader_info_SDR[] = {
		SHADER_STAGE(QVK_MOD_STRETCH_PIC_VERT, VK_SHADER_STAGE_VERTEX_BIT),
		SHADER_STAGE_SPEC(QVK_MOD_STRETCH_PIC_FRAG, VK_SHADER_STAGE_FRAGMENT_BIT, &specInfo_SDR)
	};

	VkPipelineShaderStageCreateInfo shader_info_HDR[] = {
		SHADER_STAGE(QVK_MOD_STRETCH_PIC_VERT, VK_SHADER_STAGE_VERTEX_BIT),
		SHADER_STAGE_SPEC(QVK_MOD_STRETCH_PIC_FRAG, VK_SHADER_STAGE_FRAGMENT_BIT, &specInfo_HDR)
	};

	VkPipelineVertexInputStateCreateInfo vertex_input_info = {
		.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount   = 0,
		.pVertexBindingDescriptions      = NULL,
		.vertexAttributeDescriptionCount = 0,
		.pVertexAttributeDescriptions    = NULL,
	};

	VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
		.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology               = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
		.primitiveRestartEnable = VK_FALSE,
	};

	VkViewport viewport = {
		.x        = 0.0f,
		.y        = 0.0f,
		.width    = (float) vkpt_draw_get_extent().width,
		.height   = (float) vkpt_draw_get_extent().height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = vkpt_draw_get_extent(),
	};

	VkPipelineViewportStateCreateInfo viewport_state = {
		.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports    = &viewport,
		.scissorCount  = 1,
		.pScissors     = &scissor,
	};

	VkPipelineRasterizationStateCreateInfo rasterizer_state = {
		.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable        = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE, /* skip rasterizer */
		.polygonMode             = VK_POLYGON_MODE_FILL,
		.lineWidth               = 1.0f,
		.cullMode                = VK_CULL_MODE_BACK_BIT,
		.frontFace               = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable         = VK_FALSE,
		.depthBiasConstantFactor = 0.0f,
		.depthBiasClamp          = 0.0f,
		.depthBiasSlopeFactor    = 0.0f,
	};

	VkPipelineMultisampleStateCreateInfo multisample_state = {
		.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.sampleShadingEnable   = VK_FALSE,
		.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT,
		.minSampleShading      = 1.0f,
		.pSampleMask           = NULL,
		.alphaToCoverageEnable = VK_FALSE,
		.alphaToOneEnable      = VK_FALSE,
	};

	VkPipelineColorBlendAttachmentState color_blend_attachment = {
		.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT
			                 | VK_COLOR_COMPONENT_G_BIT
			                 | VK_COLOR_COMPONENT_B_BIT
			                 | VK_COLOR_COMPONENT_A_BIT,
		.blendEnable         = VK_TRUE,
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp        = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alphaBlendOp        = VK_BLEND_OP_ADD,
	};

	VkPipelineColorBlendStateCreateInfo color_blend_state = {
		.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable   = VK_FALSE,
		.logicOp         = VK_LOGIC_OP_COPY,
		.attachmentCount = 1,
		.pAttachments    = &color_blend_attachment,
		.blendConstants  = { 0.0f, 0.0f, 0.0f, 0.0f },
	};

	VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount          = LENGTH(shader_info_SDR),

		.pVertexInputState   = &vertex_input_info,
		.pInputAssemblyState = &input_assembly_info,
		.pViewportState      = &viewport_state,
		.pRasterizationState = &rasterizer_state,
		.pMultisampleState   = &multisample_state,
		.pDepthStencilState  = NULL,
		.pColorBlendState    = &color_blend_state,
		.pDynamicState       = NULL,
		
		.layout              = pipeline_layout_stretch_pic,
		.renderPass          = render_pass_stretch_pic,
		.subpass             = 0,

		.basePipelineHandle  = VK_NULL_HANDLE,
		.basePipelineIndex   = -1,
	};

	pipeline_info.pStages = shader_info_SDR;
	_VK(vkCreateGraphicsPipelines(qvk.device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline_stretch_pic[STRETCH_PIC_SDR]));
	ATTACH_LABEL_VARIABLE(pipeline_stretch_pic[STRETCH_PIC_SDR], PIPELINE);

	pipeline_info.pStages = shader_info_HDR;
	_VK(vkCreateGraphicsPipelines(qvk.device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline_stretch_pic[STRETCH_PIC_HDR]));
	ATTACH_LABEL_VARIABLE(pipeline_stretch_pic[STRETCH_PIC_HDR], PIPELINE);


	VkPipelineShaderStageCreateInfo shader_info_final_blit[] = {
		SHADER_STAGE(QVK_MOD_FINAL_BLIT_VERT, VK_SHADER_STAGE_VERTEX_BIT),
		SHADER_STAGE(QVK_MOD_FINAL_BLIT_LANCZOS_FRAG, VK_SHADER_STAGE_FRAGMENT_BIT)
	};

	pipeline_info.pStages = shader_info_final_blit;
	pipeline_info.layout = pipeline_layout_final_blit;

	_VK(vkCreateGraphicsPipelines(qvk.device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline_final_blit));
	ATTACH_LABEL_VARIABLE(pipeline_final_blit, PIPELINE);

	framebuffer_stretch_pic = malloc(qvk.num_swap_chain_images * sizeof(*framebuffer_stretch_pic));
	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		VkImageView attachments[] = {
			qvk.swap_chain_image_views[i]
		};

		VkFramebufferCreateInfo fb_create_info = {
			.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass      = render_pass_stretch_pic,
			.attachmentCount = 1,
			.pAttachments    = attachments,
			.width           = vkpt_draw_get_extent().width,
			.height          = vkpt_draw_get_extent().height,
			.layers          = 1,
		};

		_VK(vkCreateFramebuffer(qvk.device, &fb_create_info, NULL, framebuffer_stretch_pic + i));
		ATTACH_LABEL_VARIABLE(framebuffer_stretch_pic[i], FRAMEBUFFER);
	}

	return VK_SUCCESS;
}

VkResult
vkpt_draw_clear_stretch_pics()
{
	num_stretch_pics = 0;
	return VK_SUCCESS;
}

VkResult
vkpt_draw_submit_stretch_pics(VkCommandBuffer cmd_buf)
{
	if (num_stretch_pics == 0)
		return VK_SUCCESS;

	BufferResource_t *buf_spq = buf_stretch_pic_queue + qvk.current_frame_index;
	StretchPic_t *spq_dev = (StretchPic_t *) buffer_map(buf_spq);
	memcpy(spq_dev, stretch_pic_queue, sizeof(StretchPic_t) * num_stretch_pics);
	buffer_unmap(buf_spq);
	spq_dev = NULL;

	BufferResource_t *ubo_res = buf_ubo + qvk.current_frame_index;
	StretchPic_UBO_t *ubo = (StretchPic_UBO_t *) buffer_map(ubo_res);
	ubo->ui_hdr_nits = cvar_ui_hdr_nits->value;
	ubo->tm_hdr_saturation_scale = cvar_tm_hdr_saturation_scale->value;
	buffer_unmap(ubo_res);
	ubo = NULL;

	VkRenderPassBeginInfo render_pass_info = {
		.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass        = render_pass_stretch_pic,
		.framebuffer       = framebuffer_stretch_pic[qvk.current_swap_chain_image_index],
		.renderArea.offset = { 0, 0 },
		.renderArea.extent = vkpt_draw_get_extent()
	};

	VkDescriptorSet desc_sets[] = {
		desc_set_sbo[qvk.current_frame_index],
		qvk_get_current_desc_set_textures(),
		desc_set_ubo[qvk.current_frame_index],
	};

	vkCmdBeginRenderPass(cmd_buf, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline_layout_stretch_pic, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_stretch_pic[qvk.surf_is_hdr ? STRETCH_PIC_HDR : STRETCH_PIC_SDR]);
	vkCmdDraw(cmd_buf, 4, num_stretch_pics, 0, 0);
	vkCmdEndRenderPass(cmd_buf);

	num_stretch_pics = 0;
	return VK_SUCCESS;
}

VkResult
vkpt_final_blit_simple(VkCommandBuffer cmd_buf, VkImage image, VkExtent2D extent)
{
	VkImageSubresourceRange subresource_range = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1
	};

	IMAGE_BARRIER(cmd_buf,
		.image = qvk.swap_chain_images[qvk.current_swap_chain_image_index],
		.subresourceRange = subresource_range,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
	);

	IMAGE_BARRIER(cmd_buf,
		.image = image,
		.subresourceRange = subresource_range,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
	);

	VkOffset3D blit_size = {
		.x = extent.width,
		.y = extent.height,
		.z = 1
	};
	VkOffset3D blit_size_unscaled = {
		.x = qvk.extent_unscaled.width,.y = qvk.extent_unscaled.height,.z = 1
	};
	VkImageBlit img_blit = {
		.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.srcOffsets = { [1] = blit_size },
		.dstOffsets = { [1] = blit_size_unscaled },
	};
	vkCmdBlitImage(cmd_buf,
		image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		qvk.swap_chain_images[qvk.current_swap_chain_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &img_blit, VK_FILTER_NEAREST);

	IMAGE_BARRIER(cmd_buf,
		.image = qvk.swap_chain_images[qvk.current_swap_chain_image_index],
		.subresourceRange = subresource_range,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = 0,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	);

	IMAGE_BARRIER(cmd_buf,
		.image = image,
		.subresourceRange = subresource_range,
		.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL
	);

	return VK_SUCCESS;
}

VkResult
vkpt_final_blit_filtered(VkCommandBuffer cmd_buf)
{
	VkRenderPassBeginInfo render_pass_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = render_pass_stretch_pic,
		.framebuffer = framebuffer_stretch_pic[qvk.current_swap_chain_image_index],
		.renderArea.offset = { 0, 0 },
		.renderArea.extent = vkpt_draw_get_extent()
	};

	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo,
		qvk_get_current_desc_set_textures()
	};

	vkCmdBeginRenderPass(cmd_buf, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline_layout_final_blit, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_final_blit);
	vkCmdDraw(cmd_buf, 4, 1, 0, 0);
	vkCmdEndRenderPass(cmd_buf);

	return VK_SUCCESS;
}

void R_SetClipRect_RTX(const clipRect_t *clip) 
{ 
	if (clip)
	{
		clip_enable = true;
		clip_rect = *clip;
	}
	else
	{
		clip_enable = false;
	}
}

void
R_ClearColor_RTX(void)
{
	draw.colors[0].u32 = U32_WHITE;
	draw.colors[1].u32 = U32_WHITE;
}

void
R_SetAlpha_RTX(float alpha)
{
    alpha = powf(fabsf(alpha), 0.4545f); // un-sRGB the alpha
	draw.colors[0].u8[3] = draw.colors[1].u8[3] = alpha * 255;
}

void
R_SetAlphaScale_RTX(float alpha)
{
	draw.alpha_scale = alpha;
}

void
R_SetColor_RTX(uint32_t color)
{
	draw.colors[0].u32   = color;
	draw.colors[1].u8[3] = draw.colors[0].u8[3];
}

void
R_LightPoint_RTX(const vec3_t origin, vec3_t light)
{
	VectorSet(light, 1, 1, 1);
}

void
R_SetScale_RTX(float scale)
{
	draw.scale = scale;
}

void
R_DrawStretchPic_RTX(int x, int y, int w, int h, qhandle_t pic)
{
	enqueue_stretch_pic(
		x,    y,    w,    h,
		0.0f, 0.0f, 1.0f, 1.0f,
		draw.colors[0].u32, pic);
}

void
R_DrawPic_RTX(int x, int y, qhandle_t pic)
{
	image_t *image = IMG_ForHandle(pic);
	R_DrawStretchPic(x, y, image->width, image->height, pic);
}

void
R_DrawStretchRaw_RTX(int x, int y, int w, int h)
{
	if(!qvk.raw_image)
		return;
	R_DrawStretchPic(x, y, w, h, qvk.raw_image - r_images);
}

void
R_UpdateRawPic_RTX(int pic_w, int pic_h, const uint32_t *pic)
{
	if(qvk.raw_image)
		R_UnregisterImage(qvk.raw_image - r_images);

	size_t raw_size = pic_w * pic_h * 4;
	byte *raw_data = Z_Malloc(raw_size);
	memcpy(raw_data, pic, raw_size);
	static int raw_id;
	qvk.raw_image = r_images + R_RegisterRawImage(va("**raw[%d]**", raw_id++), pic_w, pic_h, raw_data, IT_SPRITE, IF_SRGB);
}

void
R_DiscardRawPic_RTX(void)
{
	if(qvk.raw_image) {
		R_UnregisterImage(qvk.raw_image - r_images);
		qvk.raw_image = NULL;
	}
}

#define DIV64 (1.0f / 64.0f)

void
R_TileClear_RTX(int x, int y, int w, int h, qhandle_t pic)
{
	enqueue_stretch_pic(x, y, w, h,
		x * DIV64, y * DIV64, (x + w) * DIV64, (y + h) * DIV64,
		U32_WHITE, pic);
}

void
R_DrawFill8_RTX(int x, int y, int w, int h, int c)
{
	if(!w || !h)
		return;
	enqueue_stretch_pic(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f,
		d_8to24table[c & 0xff], TEXNUM_WHITE);
}

void
R_DrawFill32_RTX(int x, int y, int w, int h, uint32_t color)
{
	if(!w || !h)
		return;
	enqueue_stretch_pic(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f,
		color, TEXNUM_WHITE);
}

static inline void
draw_char(int x, int y, int flags, int c, qhandle_t font)
{
	if ((c & 127) == 32) {
		return;
	}

	if (flags & UI_ALTCOLOR) {
		c |= 0x80;
	}
	if (flags & UI_XORCOLOR) {
		c ^= 0x80;
	}

	float s = (c & 15) * 0.0625f;
	float t = (c >> 4) * 0.0625f;

	float eps = 1e-5f; /* fixes some ugly artifacts */

	enqueue_stretch_pic(x, y, CHAR_WIDTH, CHAR_HEIGHT,
		s + eps, t + eps, s + 0.0625f - eps, t + 0.0625f - eps,
		draw.colors[c >> 7].u32, font);
}

void
R_DrawChar_RTX(int x, int y, int flags, int c, qhandle_t font)
{
	draw_char(x, y, flags, c & 255, font);
}

int
R_DrawString_RTX(int x, int y, int flags, size_t maxlen, const char *s, qhandle_t font)
{
	while(maxlen-- && *s) {
		byte c = *s++;
		draw_char(x, y, flags, c, font);
		x += CHAR_WIDTH;
	}

	return x;
}

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
