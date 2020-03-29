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

#ifndef  __VK_UTIL_H__
#define  __VK_UTIL_H__

#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>

char * sgets(char * str, int num, char const ** input);

#ifdef VKPT_DEVICE_GROUPS
#define VKPT_MAX_GPUS 2
#else
#define VKPT_MAX_GPUS 1
#endif

typedef struct BufferResource_s {
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkDeviceAddress address;
	size_t size;
	int is_mapped;
} BufferResource_t;

VkResult
buffer_create(
		BufferResource_t *buf,
		VkDeviceSize size, 
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags mem_properties);

VkResult buffer_destroy(BufferResource_t *buf);
void buffer_unmap(BufferResource_t *buf);
void *buffer_map(BufferResource_t *buf);
void buffer_unmap(BufferResource_t *buf);

VkDeviceAddress get_buffer_device_address(VkBuffer buffer);

uint32_t get_memory_type(uint32_t mem_req_type_bits, VkMemoryPropertyFlags mem_prop);


#define IMAGE_BARRIER(cmd_buf, ...) \
	do { \
		VkImageMemoryBarrier img_mem_barrier = { \
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, \
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, \
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, \
			__VA_ARGS__ \
		}; \
		vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, \
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, \
				1, &img_mem_barrier); \
	} while(0)

#define BUFFER_BARRIER(cmd_buf, ...) \
	do { \
		VkBufferMemoryBarrier buf_mem_barrier = { \
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, \
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, \
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, \
			__VA_ARGS__ \
		}; \
		vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, \
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 1, &buf_mem_barrier, \
				0, NULL); \
	} while(0)


#define CREATE_PIPELINE_LAYOUT(dev, layout, ...) \
	do { \
		VkPipelineLayoutCreateInfo pipeline_layout_info = { \
			.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, \
			__VA_ARGS__ \
		}; \
		_VK(vkCreatePipelineLayout(dev, &pipeline_layout_info, NULL, layout)); \
	} while(0) \

const char *qvk_format_to_string(VkFormat format);
const char *qvk_result_to_string(VkResult result);

// #define VKPT_ENABLE_VALIDATION

#ifdef VKPT_ENABLE_VALIDATION
#define ATTACH_LABEL_VARIABLE(a, type) \
	do { \
		/*Com_Printf("attaching object label 0x%08lx %s\n", (uint64_t) a, #a);*/ \
		VkDebugMarkerObjectNameInfoEXT name_info = { \
			.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT, \
			.object = (uint64_t) a, \
			.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_##type##_EXT, \
			.pObjectName = #a \
		}; \
		qvkDebugMarkerSetObjectNameEXT(qvk.device, &name_info); \
	} while(0)

#define ATTACH_LABEL_VARIABLE_NAME(a, type, name) \
	do { \
		/*Com_Printf("attaching object label 0x%08lx %s\n", (uint64_t) a, name);*/ \
		VkDebugMarkerObjectNameInfoEXT name_info = { \
			.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT, \
			.object = (uint64_t) a, \
			.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_##type##_EXT, \
			.pObjectName = name, \
		}; \
		qvkDebugMarkerSetObjectNameEXT(qvk.device, &name_info); \
	} while(0)

#define BEGIN_CMD_LABEL(cmd_buf, label) \
	do { \
		VkDebugUtilsLabelEXT label_info; \
		label_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT; \
		label_info.pNext = NULL; \
		label_info.pLabelName = label; \
		label_info.color[0] = label_info.color[1] = label_info.color[2] = label_info.color[3] = 1.0f; \
		qvkCmdBeginDebugUtilsLabelEXT(cmd_buf, &label_info); \
	} while (0)

#define END_CMD_LABEL(cmd_buf) \
	do { \
		qvkCmdEndDebugUtilsLabelEXT(cmd_buf); \
	} while (0)

#else
#define ATTACH_LABEL_VARIABLE(a, type) do{}while(0)
#define ATTACH_LABEL_VARIABLE_NAME(a, type, name) do{}while(0)
#define BEGIN_CMD_LABEL(cmd_buf, label) do{}while(0)
#define END_CMD_LABEL(cmd_buf) do{}while(0)
#endif

static inline size_t align(size_t x, size_t alignment)
{
	return (x + (alignment - 1)) & ~(alignment - 1);
}

#ifdef VKPT_IMAGE_DUMPS
void save_to_pfm_file(char* prefix, uint64_t frame_counter, uint64_t width, uint64_t height, char* data, uint64_t rowPitch, int32_t type);
#endif

#endif  /*__VK_UTIL_H__*/
