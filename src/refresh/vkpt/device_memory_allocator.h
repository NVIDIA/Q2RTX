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

#pragma once
#include <stdint.h>

typedef enum
{
	DMA_SUCCESS = 0,
	DMA_NOT_ENOUGH_MEMORY
} DMAResult;

typedef struct DeviceMemory
{
	VkDeviceMemory memory;
	uint64_t memory_offset;
	uint64_t size;
	uint64_t alignment;
	uint32_t memory_type;
} DeviceMemory;

typedef struct DeviceMemoryAllocator DeviceMemoryAllocator;

DeviceMemoryAllocator* create_device_memory_allocator(VkDevice device, const char *debug_label);
DMAResult allocate_device_memory(DeviceMemoryAllocator* allocator, DeviceMemory* device_memory);
void free_device_memory(DeviceMemoryAllocator* allocator, const DeviceMemory* device_memory);
void destroy_device_memory_allocator(DeviceMemoryAllocator* allocator);
void get_device_malloc_stats(DeviceMemoryAllocator* allocator, size_t* memory_allocated, size_t* memory_used);