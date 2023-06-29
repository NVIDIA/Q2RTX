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

#ifdef VKPT_DEVICE_GROUPS

// performs a global memory barrier across all GPUs in the DG
void
vkpt_mgpu_global_barrier(VkCommandBuffer cmd_buf)
{
	if (qvk.device_count == 1) {
		return;
	}

	VkMemoryBarrier mem_barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.pNext = NULL,
		.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT
	};

	vkCmdPipelineBarrier(cmd_buf,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_DEPENDENCY_DEVICE_GROUP_BIT,
				1, &mem_barrier,
				0, NULL,
				0, NULL);
}

void
vkpt_mgpu_image_copy(VkCommandBuffer cmd_buf,
					int src_image_index,
					int dst_image_index,
					int src_gpu_index,
					int dst_gpu_index,
					VkOffset2D src_offset,
					VkOffset2D dst_offset,
					VkExtent2D size)
{
	VkImageSubresourceLayers subresource_layers = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel = 0,
		.baseArrayLayer = 0,
		.layerCount = 1
	};

	VkImageCopy copy_region = {
		.srcSubresource = subresource_layers,
		.srcOffset.x = src_offset.x,
		.srcOffset.y = src_offset.y,
		.srcOffset.z = 0,
		.dstSubresource = subresource_layers,
		.dstOffset.x = dst_offset.x,
		.dstOffset.y = dst_offset.y,
		.dstOffset.z = 0,
		.extent.width = size.width,
		.extent.height = size.height,
		.extent.depth = 1
	};

	set_current_gpu(cmd_buf, src_gpu_index);

	VkMemoryBarrier mem_barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.pNext = NULL,
		.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
	};

	vkCmdPipelineBarrier(cmd_buf,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				0,
				1, &mem_barrier,
				0, NULL,
				0, NULL);

	vkCmdCopyImage(cmd_buf,
		qvk.images_local[src_gpu_index][src_image_index], VK_IMAGE_LAYOUT_GENERAL,
		qvk.images_local[dst_gpu_index][dst_image_index], VK_IMAGE_LAYOUT_GENERAL,
		1, &copy_region);

	set_current_gpu(cmd_buf, ALL_GPUS);
}
#endif

void
vkpt_image_copy(VkCommandBuffer cmd_buf,
	int src_image_index,
	int dst_image_index,
	VkOffset2D src_offset,
	VkOffset2D dst_offset,
	VkExtent2D size)
{
	VkImageSubresourceLayers subresource_layers = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel = 0,
		.baseArrayLayer = 0,
		.layerCount = 1
	};

	VkImageCopy copy_region = {
		.srcSubresource = subresource_layers,
		.srcOffset.x = src_offset.x,
		.srcOffset.y = src_offset.y,
		.srcOffset.z = 0,
		.dstSubresource = subresource_layers,
		.dstOffset.x = dst_offset.x,
		.dstOffset.y = dst_offset.y,
		.dstOffset.z = 0,
		.extent.width = size.width,
		.extent.height = size.height,
		.extent.depth = 1
	};

	VkMemoryBarrier mem_barrier = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.pNext = NULL,
		.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
	};

	vkCmdPipelineBarrier(cmd_buf,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0,
		1, &mem_barrier,
		0, NULL,
		0, NULL);

	vkCmdCopyImage(cmd_buf,
		qvk.images[src_image_index], VK_IMAGE_LAYOUT_GENERAL,
		qvk.images[dst_image_index], VK_IMAGE_LAYOUT_GENERAL,
		1, &copy_region);
}
