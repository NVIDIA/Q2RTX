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
	BA_SUCCESS = 0,
	BA_NOT_ENOUGH_MEMORY,
	BA_INVALID_ALIGNMENT
} BAResult;

typedef struct BuddyAllocator BuddyAllocator;

BuddyAllocator* create_buddy_allocator(uint64_t capacity, uint64_t block_size);
BAResult buddy_allocator_allocate(BuddyAllocator* allocator, uint64_t size, uint64_t alignment, uint64_t* offset);
void buddy_allocator_free(BuddyAllocator* allocator, uint64_t offset, uint64_t size);
void destroy_buddy_allocator(BuddyAllocator* allocator);
