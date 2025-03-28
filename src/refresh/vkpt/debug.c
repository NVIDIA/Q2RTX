/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2024-2025 Frank Richter
Copyright (C) 2024-2025 Andrey Nazarov
Copyright (C) 2024-2025 Jonathan "Paril" Barkley

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
#include "common/intreadwrite.h"
#include "shared/list.h"
#include "refresh/debug.h"
#include <assert.h>

static cvar_t *vkpt_debug_linewidth;
static cvar_t *vkpt_debug_distfrac;

static vkpt_lazy_image_t lazy_image_debug_line;
static VkRenderPass render_pass_debug_line;
static VkPipelineLayout pipeline_layout_debug_line;
static VkPipeline pipeline_debug_line;
static VkFramebuffer framebuffer_debug_line;

static uint64_t debug_lines_drawn;

static struct {
	BufferResource_t coord_buf;
	BufferResource_t color_buf;
} debug_line_buffers[MAX_FRAMES_IN_FLIGHT];

bool vkpt_debugdraw_supported(void)
{
	return qvk.supports_debug_lines;
}

void vkpt_debugdraw_addtext(const vec3_t origin, const vec3_t angles, const char *text,
							float size, uint32_t color, uint32_t time, bool depth_test)
{
	if (!qvk.supports_debug_lines)
		return;

	R_AddDebugText_Lines(vkpt_refdef.fd->vieworg, origin, angles, text, size, color, time, depth_test);
}

static inline void mult_matrix_vector3(vec3_t v, const mat4_t a, const vec3_t b)
{
	vec4_t b4 = {b[0], b[1], b[2], 1.f};
	vec4_t v4;
	mult_matrix_vector(v4, a, b4);
	v[0] = v4[0];
	v[1] = v4[1];
	v[2] = v4[2];
}

// Clip line A->B to 'near' plane (points to +Z, small distance from the origin)
static inline bool intersect_z_near(vec3_t a, vec3_t b)
{
	const float clip_z = vkpt_refdef.z_near;

	// Simple cases
	bool a_vis = a[2] >= clip_z;
	bool b_vis = b[2] >= clip_z;
	if (a_vis == b_vis)
		return a_vis;

	vec3_t dir;
	VectorSubtract(b, a, dir);
	float t = (a[2] - clip_z) / (-dir[2]);
	vec3_t intersect_p;
	VectorMA(a, t, dir, intersect_p);
	if (!a_vis)
		VectorCopy(intersect_p, a);
	else
		VectorCopy(intersect_p, b);
	return true;
}

static void VKPT_DrawDebugLines(VkCommandBuffer cmd_buf)
{
	if (LIST_EMPTY(&r_debug_lines_active))
		return;

	BufferResource_t *coord_buf = &debug_line_buffers[qvk.current_frame_index].coord_buf;
	BufferResource_t *color_buf = &debug_line_buffers[qvk.current_frame_index].color_buf;

	vec3_t *coord_ptr = (vec3_t *)buffer_map(coord_buf);
	uint32_t *color_ptr = (uint32_t *)buffer_map(color_buf);

	r_debug_line_t *l, *next;
	int numverts = 0;
	LIST_FOR_EACH_SAFE(r_debug_line_t, l, next, &r_debug_lines_active, entry) {
		vec3_t view_start, view_end;
		mult_matrix_vector3(view_start, vkpt_refdef.view_matrix, l->start);
		mult_matrix_vector3(view_end, vkpt_refdef.view_matrix, l->end);
		if (!intersect_z_near(view_start, view_end))
			// Cull line behind camera.
			continue;
		if (!l->depth_test) {
			// Negative Z coord indicates disabled depth testing
			view_start[2] = -view_start[2];
			view_end[2] = -view_end[2];
		}
		VectorCopy(view_start, coord_ptr[0]);
		VectorCopy(view_end, coord_ptr[1]);
		coord_ptr += 2;
		WN32(color_ptr + 0, l->color.u32);
		WN32(color_ptr + 1, l->color.u32);
		color_ptr += 2;

		numverts += 2;

		if (!l->time) { // one-frame render
			List_Remove(&l->entry);
			List_Insert(&r_debug_lines_free, &l->entry);
		}
	}

	buffer_unmap(coord_buf);
	buffer_unmap(color_buf);

	VkClearValue clear_value = { .color = { .uint32 = { 0, 0, 0, 0 } } };

	VkRenderPassBeginInfo render_pass_info = {
		.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass        = render_pass_debug_line,
		.framebuffer       = framebuffer_debug_line,
		.renderArea.offset = { 0, 0 },
		.renderArea.extent = qvk.extent_unscaled,
		.clearValueCount   = 1,
		.pClearValues      = &clear_value
	};

	VkDescriptorSet desc_sets[] = {
		qvk.desc_set_ubo,
		qvk_get_current_desc_set_textures(),
	};

	vkCmdBeginRenderPass(cmd_buf, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline_layout_debug_line, 0, LENGTH(desc_sets), desc_sets, 0, 0);
	VkDeviceSize offsets[] = { 0, 0 };
	VkBuffer vertex_buffers[] = {coord_buf->buffer, color_buf->buffer};
	vkCmdBindVertexBuffers(cmd_buf, 0, 2, vertex_buffers, offsets);
	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_debug_line);
	vkCmdSetLineWidth(cmd_buf, vkpt_debug_linewidth->value);
	vkCmdDraw(cmd_buf, numverts, 1, 0, 0);
	vkCmdEndRenderPass(cmd_buf);

	debug_lines_drawn = qvk.frame_counter;
}

bool vkpt_debugdraw_have(void)
{
	return !LIST_EMPTY(&r_debug_lines_active);
}

void vkpt_debugdraw_draw(VkCommandBuffer cmd_buf)
{
	VKPT_DrawDebugLines(cmd_buf);
}

VkResult vkpt_debugdraw_create(void)
{
	LOG_FUNC();

	vkpt_debug_linewidth = Cvar_Get("pt_debug_linewidth", "2", 0);
	vkpt_debug_distfrac = Cvar_Get("pt_debug_distfrac", "0.004", 0);

	if (!qvk.supports_debug_lines)
		return VK_SUCCESS;

	static_assert(MAX_DEBUG_VERTICES <= UINT16_MAX, "MAX_DEBUG_VERTICES doesn't fit in index type");

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		_VK(buffer_create(&debug_line_buffers[i].coord_buf, sizeof(vec3_t) * MAX_DEBUG_VERTICES,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
		ATTACH_LABEL_VARIABLE_NAME(debug_line_buffers[i].coord_buf.buffer, BUFFER, va("debug lines coord buffer %d", i));

		_VK(buffer_create(&debug_line_buffers[i].color_buf, sizeof(uint32_t) * MAX_DEBUG_VERTICES,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
		ATTACH_LABEL_VARIABLE_NAME(debug_line_buffers[i].color_buf.buffer, BUFFER, va("debug lines color buffer %d", i));
	}

	VkAttachmentDescription color_attachment = {
		.format         = VK_FORMAT_R8G8B8A8_UNORM,
		.samples        = VK_SAMPLE_COUNT_1_BIT,
		.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
		.initialLayout  = VK_IMAGE_LAYOUT_GENERAL,
		.finalLayout    = VK_IMAGE_LAYOUT_GENERAL,
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

	_VK(vkCreateRenderPass(qvk.device, &render_pass_info, NULL, &render_pass_debug_line));
	ATTACH_LABEL_VARIABLE(render_pass_debug_line, RENDER_PASS);

	return VK_SUCCESS;
}

VkResult vkpt_debugdraw_create_pipelines(void)
{
	LOG_FUNC();

	return VK_SUCCESS;
}

void vkpt_debugdraw_prepare(void)
{
	if (!qvk.supports_debug_lines)
		return;

	if (pipeline_debug_line)
		return;

	LOG_FUNC();

	vkpt_prepare_lazy_image(&lazy_image_debug_line, IMG_WIDTH_UNSCALED, IMG_HEIGHT_UNSCALED, VK_FORMAT_R8G8B8A8_UNORM, "debug lines");

	VkDescriptorSetLayout desc_set_layouts[] = {
		qvk.desc_set_layout_ubo, qvk.desc_set_layout_textures
	};
	CREATE_PIPELINE_LAYOUT(qvk.device, &pipeline_layout_debug_line,
		.setLayoutCount = LENGTH(desc_set_layouts),
		.pSetLayouts    = desc_set_layouts
	);

	VkPipelineShaderStageCreateInfo shader_info[] = {
		SHADER_STAGE(QVK_MOD_DEBUG_LINE_VERT, VK_SHADER_STAGE_VERTEX_BIT),
		SHADER_STAGE(QVK_MOD_DEBUG_LINE_FRAG, VK_SHADER_STAGE_FRAGMENT_BIT)
	};

	VkVertexInputBindingDescription vertex_input_bindings[] = {
		{ .binding = 0, .stride = sizeof(vec3_t), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
		{ .binding = 1, .stride = sizeof(uint32_t), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
	};
	VkVertexInputAttributeDescription vertex_input_attributes[] = {
		{ .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 },
		{ .location = 1, .binding = 1, .format = VK_FORMAT_R8G8B8A8_UNORM, .offset = 0 },
	};

	VkPipelineVertexInputStateCreateInfo vertex_input_info = {
		.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount   = LENGTH(vertex_input_bindings),
		.pVertexBindingDescriptions      = vertex_input_bindings,
		.vertexAttributeDescriptionCount = LENGTH(vertex_input_attributes),
		.pVertexAttributeDescriptions    = vertex_input_attributes,
	};

	VkPipelineInputAssemblyStateCreateInfo input_assembly_info = {
		.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology               = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
		.primitiveRestartEnable = VK_FALSE,
	};

	VkViewport viewport = {
		.x        = 0.0f,
		.y        = 0.0f,
		.width    = (float) qvk.extent_unscaled.width,
		.height   = (float) qvk.extent_unscaled.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = qvk.extent_unscaled,
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
		.polygonMode             = VK_POLYGON_MODE_LINE,
		.lineWidth               = 1.0f,
		.cullMode                = VK_CULL_MODE_NONE,
		.frontFace               = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable         = VK_FALSE,
		.depthBiasConstantFactor = 0.0f,
		.depthBiasClamp          = 0.0f,
		.depthBiasSlopeFactor    = 0.0f,
	};
	VkPipelineRasterizationLineStateCreateInfoKHR line_state = {
		.sType                 = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_KHR,
		.lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_KHR,
	};
	if (qvk.supports_smooth_lines)
		rasterizer_state.pNext = &line_state;

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

	VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_LINE_WIDTH };

	VkPipelineDynamicStateCreateInfo dynamic_state_info = {
		.sType              = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount  = LENGTH(dynamic_states),
		.pDynamicStates     = dynamic_states,
	};

	VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount          = LENGTH(shader_info),
		.pStages             = shader_info,

		.pVertexInputState   = &vertex_input_info,
		.pInputAssemblyState = &input_assembly_info,
		.pViewportState      = &viewport_state,
		.pRasterizationState = &rasterizer_state,
		.pMultisampleState   = &multisample_state,
		.pDepthStencilState  = NULL,
		.pColorBlendState    = &color_blend_state,
		.pDynamicState       = &dynamic_state_info,

		.layout              = pipeline_layout_debug_line,
		.renderPass          = render_pass_debug_line,
		.subpass             = 0,

		.basePipelineHandle  = VK_NULL_HANDLE,
		.basePipelineIndex   = -1,
	};

	_VK(vkCreateGraphicsPipelines(qvk.device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline_debug_line));
	ATTACH_LABEL_VARIABLE(pipeline_debug_line, PIPELINE);

	VkImageView attachments[] = {
		lazy_image_debug_line.image_view
	};

	VkFramebufferCreateInfo fb_create_info = {
		.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass      = render_pass_debug_line,
		.attachmentCount = 1,
		.pAttachments    = attachments,
		.width           = qvk.extent_unscaled.width,
		.height          = qvk.extent_unscaled.height,
		.layers          = 1,
	};

	_VK(vkCreateFramebuffer(qvk.device, &fb_create_info, NULL, &framebuffer_debug_line));
	ATTACH_LABEL_VARIABLE(framebuffer_debug_line, FRAMEBUFFER);
}

VkImageView vpkt_debugdraw_imageview(void)
{
	return debug_lines_drawn == qvk.frame_counter ? lazy_image_debug_line.image_view : VK_NULL_HANDLE;
}

VkResult vkpt_debugdraw_destroy(void)
{
	LOG_FUNC();

	if (!qvk.supports_debug_lines)
		return VK_SUCCESS;

	vkDestroyRenderPass(qvk.device, render_pass_debug_line, NULL);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		buffer_destroy(&debug_line_buffers[i].coord_buf);
		buffer_destroy(&debug_line_buffers[i].color_buf);
	}

	return VK_SUCCESS;
}

VkResult vkpt_debugdraw_destroy_pipelines(void)
{
	LOG_FUNC();

	if (!qvk.supports_debug_lines)
		return VK_SUCCESS;

	vkDestroyFramebuffer(qvk.device, framebuffer_debug_line, NULL);
	vkDestroyPipeline(qvk.device, pipeline_debug_line, NULL);
	pipeline_debug_line = NULL;
	vkDestroyPipelineLayout(qvk.device, pipeline_layout_debug_line, NULL);

	vkpt_destroy_lazy_image(&lazy_image_debug_line);

	return VK_SUCCESS;
}
