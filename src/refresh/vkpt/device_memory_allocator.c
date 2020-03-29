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

#include "shared/shared.h"

#define VK_ENABLE_BETA_EXTENSIONS
#include <vulkan/vulkan.h>
#include <assert.h>

#include "device_memory_allocator.h"
#include "buddy_allocator.h"

#include "vkpt.h"

#define ALLOCATOR_CAPACITY 33554432 // 32MiB
#define ALLOCATOR_BLOCK_SIZE 1024 // 1024B

typedef struct SubAllocator
{
	VkDeviceMemory memory;
	BuddyAllocator* buddy_allocator;
	struct SubAllocator* next;
} SubAllocator;

typedef struct DeviceMemoryAllocator
{
	SubAllocator* sub_allocators[VK_MAX_MEMORY_TYPES];
	VkDevice device;
	void* memory;
} DeviceMemoryAllocator;

int create_sub_allocator(DeviceMemoryAllocator* allocator, uint32_t memory_type);

DeviceMemoryAllocator* create_device_memory_allocator(VkDevice device)
{
	char* memory = malloc(sizeof(DeviceMemoryAllocator));

	DeviceMemoryAllocator* allocator = (DeviceMemoryAllocator*)memory;
	memset(allocator, 0, sizeof(DeviceMemoryAllocator));
	allocator->memory = memory;
	allocator->device = device;

	return allocator;
}

DMAResult allocate_device_memory(DeviceMemoryAllocator* allocator, DeviceMemory* device_memory)
{
	const uint32_t memory_type = device_memory->memory_type;
	if (allocator->sub_allocators[memory_type] == NULL)
	{
		if (!create_sub_allocator(allocator, memory_type))
			return DMA_NOT_ENOUGH_MEMORY;
	}

	assert(device_memory->size <= ALLOCATOR_CAPACITY);

	BAResult result = BA_NOT_ENOUGH_MEMORY;
	SubAllocator* sub_allocator = allocator->sub_allocators[memory_type];

	while (result != BA_SUCCESS)
	{
		device_memory->memory = sub_allocator->memory;

		result = buddy_allocator_allocate(sub_allocator->buddy_allocator, device_memory->size,
			device_memory->alignment, &device_memory->memory_offset);

		assert(result <= BA_NOT_ENOUGH_MEMORY);
		if (result == BA_NOT_ENOUGH_MEMORY)
		{
			if (sub_allocator->next != NULL)
			{
				sub_allocator = sub_allocator->next;
			}
			else
			{
				if (!create_sub_allocator(allocator, memory_type))
					return DMA_NOT_ENOUGH_MEMORY;
				sub_allocator = allocator->sub_allocators[memory_type];
			}
		}
	}

	return DMA_SUCCESS;
}

void free_device_memory(DeviceMemoryAllocator* allocator, const DeviceMemory* device_memory)
{
	SubAllocator* sub_allocator = allocator->sub_allocators[device_memory->memory_type];

	while (sub_allocator->memory != device_memory->memory)
		sub_allocator = sub_allocator->next;

	buddy_allocator_free(sub_allocator->buddy_allocator, device_memory->memory_offset, device_memory->size);
}

void destroy_device_memory_allocator(DeviceMemoryAllocator* allocator)
{
	for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; i++)
	{
		SubAllocator* sub_allocator = allocator->sub_allocators[i];
		while (sub_allocator != NULL)
		{
			vkFreeMemory(allocator->device, sub_allocator->memory, NULL);
			sub_allocator = sub_allocator->next;
		}
	}

	free(allocator->memory);
}

int create_sub_allocator(DeviceMemoryAllocator* allocator, uint32_t memory_type)
{
	SubAllocator* sub_allocator = (SubAllocator*)malloc(sizeof(SubAllocator));

	VkMemoryAllocateInfo memory_allocate_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = ALLOCATOR_CAPACITY,
		.memoryTypeIndex = memory_type
	};

#ifdef VKPT_DEVICE_GROUPS
	VkMemoryAllocateFlagsInfoKHR mem_alloc_flags = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR,
		.flags = VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT_KHR,
		.deviceMask = (1 << qvk.device_count) - 1
	};

	if (qvk.device_count > 1) {
		memory_allocate_info.pNext = &mem_alloc_flags;
	}
#endif

	const VkResult result = vkAllocateMemory(allocator->device, &memory_allocate_info, NULL, &sub_allocator->memory);
	if (result != VK_SUCCESS)
		return 0;

	sub_allocator->buddy_allocator = create_buddy_allocator(ALLOCATOR_CAPACITY, ALLOCATOR_BLOCK_SIZE);
	sub_allocator->next = allocator->sub_allocators[memory_type];
	allocator->sub_allocators[memory_type] = sub_allocator;

	return 1;
}

