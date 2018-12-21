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
#include "refresh/refresh.h"
#include "client/client.h"
#include "refresh/images.h"

#include <assert.h>

#include "vkpt.h"
#include "shader/global_textures.h"

#define TEXNUM_WHITE (~0)
#define MAX_STRETCH_PICS (1<<18)

drawStatic_t draw = {
	.scale = 1.0f,
};

static int num_stretch_pics = 0;
typedef struct {
	float x, y, w,   h;
	float s, t, w_s, h_t;
	uint32_t color, tex_handle;
} StretchPic_t;

static StretchPic_t stretch_pic_queue[MAX_STRETCH_PICS];

static VkPipelineLayout        pipeline_layout_stretch_pic;
static VkRenderPass            render_pass_stretch_pic;
static VkPipeline              pipeline_stretch_pic;
static VkFramebuffer           framebuffer_stretch_pic[MAX_SWAPCHAIN_IMAGES];
static BufferResource_t        buf_stretch_pic_queue[MAX_SWAPCHAIN_IMAGES];
static VkDescriptorSetLayout   desc_set_layout_sbo;
static VkDescriptorPool        desc_pool_sbo;
static VkDescriptorSet         desc_set_sbo[MAX_SWAPCHAIN_IMAGES];


static inline void enqueue_stretch_pic(
		float x, float y, float w, float h,
		float s1, float t1, float s2, float t2,
		uint32_t color, int tex_handle)
{
	if(num_stretch_pics == MAX_STRETCH_PICS) {
		Com_EPrintf("Error: stretch pic queue full!\n");
		assert(0);
		return;
	}
	assert(tex_handle);
	StretchPic_t *sp = stretch_pic_queue + num_stretch_pics++;

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
	
	sp->color = color;
	sp->tex_handle = tex_handle;
	if(tex_handle >= 0 && tex_handle < MAX_RIMAGES
	&& !r_images[tex_handle].registration_sequence) {
		sp->tex_handle = TEXNUM_WHITE;
	}
}

static void
create_render_pass()
{
	LOG_FUNC();
	VkAttachmentDescription color_attachment = {
		.format         = qvk.surf_format.format,
		.samples        = VK_SAMPLE_COUNT_1_BIT,
		.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD,
		//.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
		.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
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
	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		_VK(buffer_create(buf_stretch_pic_queue + i, sizeof(StretchPic_t) * MAX_STRETCH_PICS, 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
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
		.descriptorCount = qvk.num_swap_chain_images,
	};

	VkDescriptorPoolCreateInfo pool_info = {
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = 1,
		.pPoolSizes    = &pool_size,
		.maxSets       = qvk.num_swap_chain_images,
	};

	_VK(vkCreateDescriptorPool(qvk.device, &pool_info, NULL, &desc_pool_sbo));
	ATTACH_LABEL_VARIABLE(desc_pool_sbo, DESCRIPTOR_POOL);


	VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {
		.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool     = desc_pool_sbo,
		.descriptorSetCount = 1,
		.pSetLayouts        = &desc_set_layout_sbo,
	};

	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
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
	}
	return VK_SUCCESS;
}

VkResult
vkpt_draw_destroy()
{
	LOG_FUNC();
	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		buffer_destroy(buf_stretch_pic_queue + i);
	}
	vkDestroyRenderPass(qvk.device, render_pass_stretch_pic, NULL);
	vkDestroyDescriptorPool(qvk.device, desc_pool_sbo, NULL);
	vkDestroyDescriptorSetLayout(qvk.device, desc_set_layout_sbo, NULL);

	return VK_SUCCESS;
}

VkResult
vkpt_draw_destroy_pipelines()
{
	LOG_FUNC();
	vkDestroyPipeline(qvk.device, pipeline_stretch_pic,    NULL);
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_stretch_pic, NULL);
	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		vkDestroyFramebuffer(qvk.device, framebuffer_stretch_pic[i], NULL);
	}
	return VK_SUCCESS;
}

VkResult
vkpt_draw_create_pipelines()
{
	LOG_FUNC();

	assert(desc_set_layout_sbo);
	VkDescriptorSetLayout desc_set_layouts[] = {
		desc_set_layout_sbo, qvk.desc_set_layout_textures
	};
	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_stretch_pic, 
		.setLayoutCount = LENGTH(desc_set_layouts),
		.pSetLayouts    = desc_set_layouts
	);

	VkPipelineShaderStageCreateInfo shader_info[] = {
		SHADER_STAGE(QVK_MOD_STRETCH_PIC_VERT, VK_SHADER_STAGE_VERTEX_BIT),
		SHADER_STAGE(QVK_MOD_STRETCH_PIC_FRAG, VK_SHADER_STAGE_FRAGMENT_BIT)
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
		.width    = (float) qvk.extent.width,
		.height   = (float) qvk.extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = qvk.extent,
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
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
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
		.sType      = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = LENGTH(shader_info),
		.pStages    = shader_info,

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

	_VK(vkCreateGraphicsPipelines(qvk.device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline_stretch_pic));
	ATTACH_LABEL_VARIABLE(pipeline_stretch_pic, PIPELINE);

	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		VkImageView attachments[] = {
			qvk.swap_chain_image_views[i]
		};

		VkFramebufferCreateInfo fb_create_info = {
			.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass      = render_pass_stretch_pic,
			.attachmentCount = 1,
			.pAttachments    = attachments,
			.width           = qvk.extent.width,
			.height          = qvk.extent.height,
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
vkpt_draw_submit_stretch_pics(VkCommandBuffer *cmd_buf)
{
	BufferResource_t *buf_spq = buf_stretch_pic_queue + qvk.current_image_index;
	StretchPic_t *spq_dev = (StretchPic_t *) buffer_map(buf_spq);
	memcpy(spq_dev, stretch_pic_queue, sizeof(stretch_pic_queue));
	buffer_unmap(buf_spq);
	spq_dev = NULL;

	VkClearValue clear_color = { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } };
	
	VkRenderPassBeginInfo render_pass_info = {
		.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass        = render_pass_stretch_pic,
		.framebuffer       = framebuffer_stretch_pic[qvk.current_image_index],
		.renderArea.offset = { 0, 0 },
		.renderArea.extent = qvk.extent,
		.clearValueCount   = 1,
		.pClearValues      = &clear_color
	};

	VkDescriptorSet desc_sets[] = {
		desc_set_sbo[qvk.current_image_index],
		qvk.desc_set_textures
	};

	vkCmdBeginRenderPass(*cmd_buf, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindDescriptorSets(*cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline_layout_stretch_pic, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	vkCmdBindPipeline(*cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_stretch_pic);
	vkCmdDraw(*cmd_buf, 4, num_stretch_pics, 0, 0);
	vkCmdEndRenderPass(*cmd_buf);

	num_stretch_pics = 0;
	return VK_SUCCESS;
}

void R_SetClipRect(const clipRect_t *clip) { FUNC_UNIMPLEMENTED(); }

void
R_ClearColor(void)
{
	draw.colors[0].u32 = U32_WHITE;
	draw.colors[1].u32 = U32_WHITE;
}

void
R_SetAlpha(float alpha)
{
	draw.colors[0].u8[3] = draw.colors[1].u8[3] = alpha * 255;
}

void
R_SetColor(uint32_t color)
{
	draw.colors[0].u32   = color;
	draw.colors[1].u8[3] = draw.colors[0].u8[3];
}

void
R_LightPoint(vec3_t origin, vec3_t light)
{
	VectorSet(light, 1, 1, 1);
}

void
R_SetScale(float scale)
{
	draw.scale = scale;
}

void
R_DrawStretchPic(int x, int y, int w, int h, qhandle_t pic)
{
	enqueue_stretch_pic(
		x,    y,    w,    h,
		0.0f, 0.0f, 1.0f, 1.0f,
		draw.colors[0].u32, pic);
}

void
R_DrawPic(int x, int y, qhandle_t pic)
{
	image_t *image = IMG_ForHandle(pic);
	R_DrawStretchPic(x, y, image->width, image->height, pic);
}

#define DIV64 (1.0f / 64.0f)

void
R_TileClear(int x, int y, int w, int h, qhandle_t pic)
{
	enqueue_stretch_pic(x, y, w, h,
		x * DIV64, y * DIV64, (x + w) * DIV64, (y + h) * DIV64,
		U32_WHITE, pic);
}

void
R_DrawFill8(int x, int y, int w, int h, int c)
{
	if(!w || !h)
		return;
	enqueue_stretch_pic(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f,
		d_8to24table[c & 0xff], TEXNUM_WHITE);
}

void
R_DrawFill32(int x, int y, int w, int h, uint32_t color)
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
R_DrawChar(int x, int y, int flags, int c, qhandle_t font)
{
	draw_char(x, y, flags, c & 255, font);
}

int
R_DrawString(int x, int y, int flags, size_t maxlen, const char *s, qhandle_t font)
{
	while(maxlen-- && *s) {
		byte c = *s++;
		draw_char(x, y, flags, c, font);
		x += CHAR_WIDTH;
	}

	return x;
}

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
