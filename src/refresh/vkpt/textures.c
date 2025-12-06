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

#include "vkpt.h"
#include "vk_util.h"
#include "refresh/images.h"
#include "device_memory_allocator.h"

#include <assert.h>

#include "color.h"
#include "material.h"
#include "../stb/stb_image.h"
#include "../stb/stb_image_resize2.h"
#include "../stb/stb_image_write.h"

#define MAX_RBUFFERS 16

typedef struct UnusedResources
{
	VkImage         images[MAX_RIMAGES];
	VkImageView     image_views[MAX_RIMAGES];
	VkImageView     image_views_mip0[MAX_RIMAGES];
	DeviceMemory    image_memory[MAX_RIMAGES];
	uint32_t        image_num;
	VkBuffer        buffers[MAX_RBUFFERS];
	VkDeviceMemory  buffer_memory[MAX_RBUFFERS];
	uint32_t        buffer_num;
} UnusedResources;

#define DESTROY_LATENCY (MAX_FRAMES_IN_FLIGHT * 4)

typedef struct TextureSystem
{
	UnusedResources unused_resources[DESTROY_LATENCY];
} TextureSystem;

static TextureSystem texture_system = { 0 };

static VkImage          tex_images     [MAX_RIMAGES] = { 0 };
static VkImageView      tex_image_views[MAX_RIMAGES] = { 0 };
static VkImageView      tex_image_views_mip0[MAX_RIMAGES] = { 0 };
static VkDeviceMemory   mem_blue_noise, mem_envmap;
static VkImage          img_blue_noise;
static VkImageView      imv_blue_noise;
static VkImage          img_envmap;
static VkImageView      imv_envmap;
static VkDescriptorPool desc_pool_textures;

static VkImage          tex_invalid_texture_image = VK_NULL_HANDLE;
static VkImageView      tex_invalid_texture_image_view = VK_NULL_HANDLE;
static DeviceMemory     tex_invalid_texture_image_memory = { 0 };

static VkDeviceMemory mem_images[NUM_VKPT_IMAGES];

static DeviceMemory            tex_image_memory[MAX_RIMAGES] = { 0 };
static VkBindImageMemoryInfo   tex_bind_image_info[MAX_RIMAGES] = { 0 };
static DeviceMemoryAllocator*  tex_device_memory_allocator = NULL;

// Resources for the normal map normalization pass that runs on texture upload
static VkDescriptorSetLayout   normalize_desc_set_layout = NULL;
static VkPipelineLayout        normalize_pipeline_layout = NULL;
static VkPipeline              normalize_pipeline = NULL;
static VkDescriptorSet         normalize_descriptor_sets[MAX_FRAMES_IN_FLIGHT] = { NULL };

// Array for tracking when the textures have been uploaded.
// On frame N (which is modulo MAX_FRAMES_IN_FLIGHT), a texture is uploaded, and that writes N into this array.
// At the same time, its storage image descriptor is created in normalize_descriptor_sets[N], and the texture
// is normalized using the normalize_pipeline. When vkpt_textures_end_registration() runs on the next frame
// with the same N, it sees the texture again, and deletes its descriptor from that descriptor set.
static uint32_t                tex_upload_frames[MAX_RIMAGES] = { 0 };

static int image_loading_dirty_flag = 0;
static uint8_t descriptor_set_dirty_flags[MAX_FRAMES_IN_FLIGHT] = { 0 }; // initialized in vkpt_textures_initialize

static const float megabyte = 1048576.0f;

extern cvar_t* cvar_pt_nearest;
extern cvar_t* cvar_pt_bilerp_chars;
extern cvar_t* cvar_pt_bilerp_pics;

void vkpt_textures_prefetch()
{
    char * buffer = NULL;
    char const * filename = "prefetch.txt";
    FS_LoadFile(filename, (void**)&buffer);
    if (buffer == NULL)
    {
        Com_EPrintf("Can't load '%s'\n", filename);
        return;
    }

    char const * ptr = buffer;
	char linebuf[MAX_QPATH];
	while (sgets(linebuf, sizeof(linebuf), &ptr))
	{
		char* line = strtok(linebuf, " \t\r\n");
		if (!line)
			continue;

		MAT_Find(line, IT_SKIN, IF_PERMANENT);
	}
    // Com_Printf("Loaded '%s'\n", filename);
    FS_FreeFile(buffer);
}

void vkpt_invalidate_texture_descriptors()
{
	for (int index = 0; index < MAX_FRAMES_IN_FLIGHT; index++)
		descriptor_set_dirty_flags[index] = 1;
}

static void textures_destroy_unused_set(uint32_t set_index)
{
	UnusedResources* unused_resources = texture_system.unused_resources + set_index;

	for (uint32_t i = 0; i < unused_resources->image_num; i++)
	{
		if (unused_resources->image_views[i] != VK_NULL_HANDLE)
			vkDestroyImageView(qvk.device, unused_resources->image_views[i], NULL);

		if (unused_resources->image_views_mip0[i] != VK_NULL_HANDLE)
			vkDestroyImageView(qvk.device, unused_resources->image_views_mip0[i], NULL);

		if(unused_resources->images[i] != VK_NULL_HANDLE)
		vkDestroyImage(qvk.device, unused_resources->images[i], NULL);

		if(unused_resources->image_memory[i].memory != VK_NULL_HANDLE)
		free_device_memory(tex_device_memory_allocator, &unused_resources->image_memory[i]);
	}
	unused_resources->image_num = 0;

	for (uint32_t i = 0; i < unused_resources->buffer_num; i++)
	{
		vkDestroyBuffer(qvk.device, unused_resources->buffers[i], NULL);
		vkFreeMemory(qvk.device, unused_resources->buffer_memory[i], NULL);
	}
	unused_resources->buffer_num = 0;
}

void vkpt_textures_destroy_unused()
{
	textures_destroy_unused_set((qvk.frame_counter) % DESTROY_LATENCY);
}

static void
destroy_envmap(void)
{
	if (imv_envmap != VK_NULL_HANDLE) {
		vkDestroyImageView(qvk.device, imv_envmap, NULL);
		imv_envmap = NULL;
	}
	if (img_envmap != VK_NULL_HANDLE) {
		vkDestroyImage(qvk.device, img_envmap, NULL);
		img_envmap = NULL;
	}
	if (mem_envmap != VK_NULL_HANDLE) {
		vkFreeMemory(qvk.device, mem_envmap, NULL);
		mem_envmap = VK_NULL_HANDLE;
	}
}

VkResult
vkpt_textures_upload_envmap(int w, int h, byte *data)
{
	vkDeviceWaitIdle(qvk.device);

	destroy_envmap();
	
	const int num_images = 6;
	size_t img_size = w * h * 4;

	BufferResource_t buf_img_upload;
	buffer_create(&buf_img_upload, img_size * num_images, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	byte *envmap = (byte *) buffer_map(&buf_img_upload);
	memcpy(envmap, data, img_size * num_images);
	buffer_unmap(&buf_img_upload);
	envmap = NULL;

	VkImageCreateInfo img_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.extent = {
			.width  = w,
			.height = h,
			.depth  = 1,
		},
		.imageType             = VK_IMAGE_TYPE_2D,
		.format                = VK_FORMAT_R8G8B8A8_UNORM,
		.mipLevels             = 1,
		.arrayLayers           = num_images,
		.samples               = VK_SAMPLE_COUNT_1_BIT,
		.tiling                = VK_IMAGE_TILING_OPTIMAL,
		.usage                 = VK_IMAGE_USAGE_STORAGE_BIT
		                       | VK_IMAGE_USAGE_TRANSFER_DST_BIT
		                       | VK_IMAGE_USAGE_SAMPLED_BIT,
		.flags                 = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
		.sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = qvk.queue_idx_graphics,
		.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	_VK(vkCreateImage(qvk.device, &img_info, NULL, &img_envmap));
	ATTACH_LABEL_VARIABLE(img_envmap, IMAGE);

	VkMemoryRequirements mem_req;
	vkGetImageMemoryRequirements(qvk.device, img_envmap, &mem_req);
	assert(mem_req.size >= buf_img_upload.size);

	_VK(allocate_gpu_memory(mem_req, &mem_envmap));

	_VK(vkBindImageMemory(qvk.device, img_envmap, mem_envmap, 0));

	VkImageViewCreateInfo img_view_info = {
		.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.viewType   = VK_IMAGE_VIEW_TYPE_CUBE,
		.format     = VK_FORMAT_R8G8B8A8_UNORM,
		.image      = img_envmap,
		.subresourceRange = {
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel   = 0,
			.levelCount     = 1,
			.baseArrayLayer = 0,
			.layerCount     = num_images,
		},
		.components     = {
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_B,
			VK_COMPONENT_SWIZZLE_A,
		},
	};
	_VK(vkCreateImageView(qvk.device, &img_view_info, NULL, &imv_envmap));
	ATTACH_LABEL_VARIABLE(imv_envmap, IMAGE_VIEW);

	VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

	for(int layer = 0; layer < num_images; layer++) {

		VkImageSubresourceRange subresource_range = {
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel   = 0,
			.levelCount     = 1,
			.baseArrayLayer = layer,
			.layerCount     = 1,
		};

		IMAGE_BARRIER(cmd_buf,
				.image            = img_envmap,
				.subresourceRange = subresource_range,
				.srcAccessMask    = 0,
				.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
		);

		VkBufferImageCopy cpy_info = {
			.bufferOffset = img_size * layer,
			.imageSubresource = { 
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel       = 0,
				.baseArrayLayer = layer,
				.layerCount     = 1,
			},
			.imageOffset    = { 0, 0, 0 },
			.imageExtent    = { w, h, 1 }
		};
		vkCmdCopyBufferToImage(cmd_buf, buf_img_upload.buffer, img_envmap,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cpy_info);


		IMAGE_BARRIER(cmd_buf,
				.image            = img_envmap,
				.subresourceRange = subresource_range,
				.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		);
	}

	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, true);

	{
	VkDescriptorImageInfo desc_img_info = {
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.imageView   = imv_envmap,
		.sampler     = qvk.tex_sampler,
	};

	VkWriteDescriptorSet s = {
		.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet          = qvk.desc_set_textures_even,
		.dstBinding      = BINDING_OFFSET_ENVMAP,
		.dstArrayElement = 0,
		.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.pImageInfo      = &desc_img_info,
	};

	vkUpdateDescriptorSets(qvk.device, 1, &s, 0, NULL);

	s.dstSet = qvk.desc_set_textures_odd;
	vkUpdateDescriptorSets(qvk.device, 1, &s, 0, NULL);
	}

	vkQueueWaitIdle(qvk.queue_graphics);

	buffer_destroy(&buf_img_upload);

	return VK_SUCCESS;
}

static VkResult
load_blue_noise(void)
{
	const int num_images = NUM_BLUE_NOISE_TEX / 4;
	const int res = BLUE_NOISE_RES;
	size_t img_size = res * res;
	size_t total_size = img_size * sizeof(uint16_t);

	BufferResource_t buf_img_upload;
	buffer_create(&buf_img_upload, total_size * NUM_BLUE_NOISE_TEX, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	uint16_t *bn_tex = (uint16_t *) buffer_map(&buf_img_upload);

	for(int i = 0; i < num_images; i++) {
		int w, h, n;
		char buf[1024];

		snprintf(buf, sizeof buf, "blue_noise/%d_%d/HDR_RGBA_%04d.png", res, res, i);

		byte* filedata = 0;
		uint16_t *data = 0;
		int filelen = FS_LoadFile(buf, (void**)&filedata);

		if (filedata) {
			data = stbi_load_16_from_memory(filedata, (int)filelen, &w, &h, &n, 4);
			Z_Free(filedata);
		}

		if(!data) {
			Com_EPrintf("error loading blue noise tex %s\n", buf);
			buffer_unmap(&buf_img_upload);
			buffer_destroy(&buf_img_upload);
			return VK_ERROR_INITIALIZATION_FAILED;
		}

		/* loaded images are RGBA, want to upload as texture array though */
		for(int k = 0; k < 4; k++) {
			for(int j = 0; j < img_size; j++)
				bn_tex[(i * 4 + k) * img_size + j] = data[j * 4 + k];
		}

		stbi_image_free(data);
	}
	buffer_unmap(&buf_img_upload);
	bn_tex = NULL;

	VkImageCreateInfo img_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.extent = {
			.width  = BLUE_NOISE_RES,
			.height = BLUE_NOISE_RES,
			.depth  = 1,
		},
		.imageType             = VK_IMAGE_TYPE_2D,
		.format                = VK_FORMAT_R16_UNORM,
		.mipLevels             = 1,
		.arrayLayers           = NUM_BLUE_NOISE_TEX,
		.samples               = VK_SAMPLE_COUNT_1_BIT,
		.tiling                = VK_IMAGE_TILING_OPTIMAL,
		.usage                 = VK_IMAGE_USAGE_STORAGE_BIT
		                       | VK_IMAGE_USAGE_TRANSFER_DST_BIT
		                       | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = qvk.queue_idx_graphics,
		.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	_VK(vkCreateImage(qvk.device, &img_info, NULL, &img_blue_noise));
	ATTACH_LABEL_VARIABLE(img_blue_noise, IMAGE);

	VkMemoryRequirements mem_req;
	vkGetImageMemoryRequirements(qvk.device, img_blue_noise, &mem_req);
	assert(mem_req.size >= buf_img_upload.size);

	_VK(allocate_gpu_memory(mem_req, &mem_blue_noise));

	_VK(vkBindImageMemory(qvk.device, img_blue_noise, mem_blue_noise, 0));

	VkImageViewCreateInfo img_view_info = {
		.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.viewType   = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
		.format     = VK_FORMAT_R16_UNORM,
		.image      = img_blue_noise,
		.subresourceRange = {
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel   = 0,
			.levelCount     = 1,
			.baseArrayLayer = 0,
			.layerCount     = NUM_BLUE_NOISE_TEX,
		},
		.components     = {
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_R,
		},
	};
	_VK(vkCreateImageView(qvk.device, &img_view_info, NULL, &imv_blue_noise));
	ATTACH_LABEL_VARIABLE(imv_blue_noise, IMAGE_VIEW);

	VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

	for(int layer = 0; layer < NUM_BLUE_NOISE_TEX; layer++) {

		VkImageSubresourceRange subresource_range = {
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel   = 0,
			.levelCount     = 1,
			.baseArrayLayer = layer,
			.layerCount     = 1,
		};

		IMAGE_BARRIER(cmd_buf,
				.image            = img_blue_noise,
				.subresourceRange = subresource_range,
				.srcAccessMask    = 0,
				.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
				.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
		);

		VkBufferImageCopy cpy_info = {
			.bufferOffset = total_size * layer,
			.imageSubresource = { 
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel       = 0,
				.baseArrayLayer = layer,
				.layerCount     = 1,
			},
			.imageOffset    = { 0, 0, 0 },
			.imageExtent    = { BLUE_NOISE_RES, BLUE_NOISE_RES, 1 }
		};
		vkCmdCopyBufferToImage(cmd_buf, buf_img_upload.buffer, img_blue_noise,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cpy_info);


		IMAGE_BARRIER(cmd_buf,
				.image            = img_blue_noise,
				.subresourceRange = subresource_range,
				.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		);
	}

	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, true);
	
	VkDescriptorImageInfo desc_img_info = {
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.imageView   = imv_blue_noise,
		.sampler     = qvk.tex_sampler,
	};

	VkWriteDescriptorSet s = {
		.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet          = qvk.desc_set_textures_even,
		.dstBinding      = BINDING_OFFSET_BLUE_NOISE,
		.dstArrayElement = 0,
		.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.pImageInfo      = &desc_img_info,
	};

	vkUpdateDescriptorSets(qvk.device, 1, &s, 0, NULL);

	s.dstSet = qvk.desc_set_textures_odd;
	vkUpdateDescriptorSets(qvk.device, 1, &s, 0, NULL);

	vkQueueWaitIdle(qvk.queue_graphics);

	buffer_destroy(&buf_img_upload);

	return VK_SUCCESS;
}

static int
get_num_miplevels(int w, int h)
{
	return 1 + log2(max(w, h));
}


/*
================
IMG_Load
================
*/

struct filterscratch_s
{
	int num_comps;
	int pad_left, pad_right;
	float *ptr;
};

static void filterscratch_init(struct filterscratch_s* scratch, unsigned kernel_size, int stripe_size, int num_comps)
{
	scratch->num_comps = num_comps;
	scratch->pad_left = kernel_size / 2;
	scratch->pad_right = kernel_size - scratch->pad_left - 1;
	int num_scratch_pixels = scratch->pad_left + stripe_size + scratch->pad_right;
	scratch->ptr = Z_Malloc(num_scratch_pixels * num_comps * sizeof(float));
}

static void filterscratch_free(struct filterscratch_s* scratch)
{
	Z_Free(scratch->ptr);
}

static void filterscratch_fill_from_float_image(struct filterscratch_s *scratch, float *current_stripe, int stripe_size, int element_stride)
{
	const int num_comps = scratch->num_comps;
	int src = -scratch->pad_left;
	float *dest_ptr = scratch->ptr;
	if ((stripe_size >= scratch->pad_left) && (stripe_size >= scratch->pad_right))
	{
		for (; src < 0; src++)
		{
			const float *src_data = current_stripe + (src + stripe_size) * element_stride * num_comps;
			memcpy(dest_ptr, src_data, num_comps * sizeof(float));
			dest_ptr += num_comps;
		}
		for (; src < stripe_size; src++)
		{
			const float *src_data = current_stripe + src * element_stride * num_comps;
			memcpy(dest_ptr, src_data, num_comps * sizeof(float));
			dest_ptr += num_comps;
		}
		for (; src < stripe_size + scratch->pad_right; src++)
		{
			const float *src_data = current_stripe + (src - stripe_size) * element_stride * num_comps;
			memcpy(dest_ptr, src_data, num_comps * sizeof(float));
			dest_ptr += num_comps;
		}
	}
	else
	{
		// The (probably) rarer case - filter kernel is larger than image
		while(src < 0)
			src += stripe_size;
		for (int i = 0; i < scratch->pad_left + stripe_size + scratch->pad_right; i++)
		{
			const float *src_data = current_stripe + src * element_stride * num_comps;
			memcpy(dest_ptr, src_data, num_comps * sizeof(float));
			dest_ptr += scratch->num_comps;
			src = (src + 1) % stripe_size;
		}
	}
}

/* Apply a (separable) filter along one dimension of an image.
 * Whether this is done along the X or Y dimension depends on the "stripe size"
 * and "stripe stride" options. See filter_image() for how to use it practically. */
static void filter_one_dimension_float(float* pixels, int num_comps,
									   const float kernel[], unsigned kernel_size,
									   int stripe_size, int num_stripes,
									   int stripe_stride, int element_stride)
{
	struct filterscratch_s scratch;
	filterscratch_init(&scratch, kernel_size, stripe_size, num_comps);
	float *current_stripe = pixels;
	float* values = alloca(num_comps * sizeof(float));
	for (int s = 0; s < num_stripes; s++)
	{
		// back up image data to scratch buffer
		filterscratch_fill_from_float_image(&scratch, current_stripe, stripe_size, element_stride);
		// filter the stripe
		for (int i = 0; i < stripe_size; i++)
		{
			memset(values, 0, num_comps * sizeof(float));
			for (int j = 0; j < kernel_size; j++)
			{
				float f = kernel[j];
				float *src_p = scratch.ptr + (i + j) * num_comps;
				for (int c = 0; c < num_comps; c++)
				{
					values[c] += f * src_p[c];
				}
			}
			memcpy(current_stripe + i * element_stride * num_comps, values, num_comps * sizeof(float));
		}
		current_stripe += stripe_stride * num_comps;
	}
	filterscratch_free(&scratch);
}

// Apply a (separable) filter to an image.
static void filter_float_image(float* pixels, int num_comps, const float kernel[], unsigned kernel_size, int width, int height)
{
	// Filter horizontally
	filter_one_dimension_float(pixels, num_comps, kernel, kernel_size, width, height, width, 1);
	// Filter vertically
	filter_one_dimension_float(pixels, num_comps, kernel, kernel_size, height, width, 1, width);
}

struct bilerp_s
{
	int current_output_row;
	float *current_input_data;
	float *next_input_data;
	float *output_data;
};

static void bilerp_init(struct bilerp_s* bilerp, int input_w)
{
	bilerp->current_output_row = -1;
	bilerp->current_input_data = IMG_AllocPixels(input_w * sizeof(float) * 3);
	bilerp->next_input_data = IMG_AllocPixels(input_w * sizeof(float) * 3);
	bilerp->output_data = IMG_AllocPixels(input_w * 2 * sizeof(float) * 3);
}

static void bilerp_free(struct bilerp_s* bilerp)
{
	Z_Free(bilerp->current_input_data);
	Z_Free(bilerp->next_input_data);
	Z_Free(bilerp->output_data);
}

static inline void _bilerp_get_next_output_line(struct bilerp_s *bilerp, const float** output_line, const float* next_input, int input_w)
{
	if(bilerp->current_output_row == -1) {
		memcpy(bilerp->next_input_data, next_input, input_w * sizeof(float) * 3);
		bilerp->current_output_row = 0;
	}

	if((bilerp->current_output_row & 1) == 0) {
		// Even output line: use input lines
		// Swap next_input_data into current_input_data
		float *tmp = bilerp->next_input_data;
		bilerp->next_input_data = bilerp->current_input_data;
		bilerp->current_input_data = tmp;
	} else {
		// Odd output line: interpolate between input lines
		memcpy(bilerp->next_input_data, next_input, input_w * sizeof(float) * 3);

		float *color_ptr = bilerp->current_input_data;
		float *next_color_ptr = bilerp->next_input_data;
		for (int x = 0; x < input_w; x++) {
			color_ptr[0] = (color_ptr[0] + next_color_ptr[0]) * 0.5f;
			color_ptr[1] = (color_ptr[1] + next_color_ptr[1]) * 0.5f;
			color_ptr[2] = (color_ptr[2] + next_color_ptr[2]) * 0.5f;
			color_ptr += 3;
			next_color_ptr += 3;
		}
	}

	float *out_ptr = bilerp->output_data;
	float color[3];
	const float* color_ptr = bilerp->current_input_data;
	for (int out_x = 0; out_x < input_w * 2 - 1; out_x++) {
		if((out_x & 1) == 0) {
			// Even output row: direct value
			memcpy(color, color_ptr, 3 * sizeof(float));
		} else {
			// Odd output row: interpolate between colors
			color_ptr += 3;
			color[0] = (color[0] + color_ptr[0]) * 0.5f;
			color[1] = (color[1] + color_ptr[1]) * 0.5f;
			color[2] = (color[2] + color_ptr[2]) * 0.5f;
		}
		memcpy(out_ptr, color, 3 * sizeof(float));
		out_ptr += 3;
	}

	// Last row: interpolate between last and first pixel
	color[0] = (color_ptr[0] + bilerp->current_input_data[0]) * 0.5f;
	color[1] = (color_ptr[1] + bilerp->current_input_data[1]) * 0.5f;
	color[2] = (color_ptr[2] + bilerp->current_input_data[2]) * 0.5f;
	memcpy(out_ptr, color, 3 * sizeof(float));

	*output_line = bilerp->output_data;

	bilerp->current_output_row++;
}

static inline void bilerp_get_next_output_line_from_rgb_f32(struct bilerp_s *bilerp, const float** output_line, const float* input_data, int input_w, int input_h)
{
	const float *next_input = NULL;
	if (bilerp->current_output_row == -1) {
		next_input = input_data;
	} else if ((bilerp->current_output_row & 1) != 0) {
		int in_y = (bilerp->current_output_row + 1) >> 1;
		// Wraparound last line
		if(in_y >= input_h)
			in_y = 0;
		next_input = input_data + in_y * input_w * 3;
	}
	_bilerp_get_next_output_line(bilerp, output_line, next_input, input_w);
}

// Fake an emissive texture from a diffuse texture by using pixels brighter than a certain amount
static void apply_fake_emissive_threshold(image_t *image, int bright_threshold_int)
{
	int w = image->upload_width;
	int h = image->upload_height;

	float *bright_mask = IMG_AllocPixels(w * h * sizeof(float));

	/* Extract "bright" pixels by choosing all those that have one component
	   larger than some threshold. */
	byte bright_threshold = Q_clip_uint8(bright_threshold_int);

	float *current_bright_mask = bright_mask;
	byte *src_pixel = image->pix_data;
	float max_src_lum = 0;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			float src_lum = LUMINANCE(decode_srgb(src_pixel[0]), decode_srgb(src_pixel[1]), decode_srgb(src_pixel[2]));
			byte max_comp = max(src_pixel[0], src_pixel[1]);
			max_comp = max(src_pixel[2], max_comp);
			if (max_comp < bright_threshold) {
				*current_bright_mask = 0;
			} else {
				*current_bright_mask = src_lum;
			}
			if (src_lum > max_src_lum)
				max_src_lum = src_lum;
			current_bright_mask++;
			src_pixel += 4;
		}
	}
	float src_lum_scale = max_src_lum > 0 ? 1.0f / max_src_lum : 1.0f;

	// Blur those "bright" pixels
	const float filter[] = { 0.0093f, 0.028002f, 0.065984f, 0.121703f, 0.175713f, 0.198596f, 0.175713f, 0.121703f, 0.065984f, 0.028002f, 0.0093f };
	filter_float_image(bright_mask, 1, filter, sizeof(filter) / sizeof(filter[0]), w, h);

	// Do a pass to find max luminance of bright_mask...
	current_bright_mask = bright_mask;
	float max_lum = 0;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			float lum = *current_bright_mask;
			if (lum > max_lum)
				max_lum = lum;

			current_bright_mask++;
		}
	}
	// ...and use it to normalize max luminance to 1
	float lum_scale = max_lum > 0 ? 1.0f / max_lum : 1.0f;

	/* Combine blurred "bright" mask with original image (to retain some colorization).
	   Produce float output for upsampling pass */
	float *final = IMG_AllocPixels(w * h * 3 * sizeof(float));

	float *out_final = final;
	current_bright_mask = bright_mask;
	byte *current_img_pixel = image->pix_data;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			vec3_t color_img;
			color_img[0] = decode_srgb(current_img_pixel[0]);
			color_img[1] = decode_srgb(current_img_pixel[1]);
			color_img[2] = decode_srgb(current_img_pixel[2]);

			/* The formula for the "emissive" color is objectively weird,
			   but is subjectively suitable for typical "light" textures...
			   It keeps the "light" pixels but avoids bleeding from neighbouring
			   "other" pixels */
			float src_lum = LUMINANCE(color_img[0], color_img[1], color_img[2]);
			/* Normalize source luminance to increase resulting emissive intensity
			 * on textures that are relatively dark */
			src_lum *= src_lum_scale;
			src_lum *= src_lum;
			float scale = *current_bright_mask * src_lum * lum_scale;
			out_final[0] = color_img[0] * scale;
			out_final[1] = color_img[1] * scale;
			out_final[2] = color_img[2] * scale;

			out_final += 3;
			current_bright_mask++;
			current_img_pixel += 4;
		}
	}
	Z_Free(bright_mask);

	// Interpolate final image to 2x size, apply a mild filter, to have it look less blocky
	int width_2x = w * 2;
	int height_2x = h * 2;
	float *final_2x = IMG_AllocPixels(width_2x * height_2x * 3 * sizeof(float));

	struct bilerp_s bilerp_final;
	bilerp_init(&bilerp_final, w);
	float *out_final_2x = final_2x;
	for (int out_y = 0; out_y < height_2x; out_y++) {
		float *img_line;
		bilerp_get_next_output_line_from_rgb_f32(&bilerp_final, (const float**)&img_line, final, w, h);
		memcpy(out_final_2x, img_line, width_2x * 3 * sizeof(float));
		out_final_2x += width_2x * 3;
	}
	bilerp_free(&bilerp_final);
	Z_Free(final);

	const float filter_final[] = { 0.157731f, 0.684538f, 0.157731f };
	filter_float_image(final_2x, 3, filter_final, sizeof(filter_final) / sizeof(filter_final[0]), width_2x, height_2x);

	// Final -> SRGB
	int new_size = width_2x * height_2x * 4;
	Z_Free(image->pix_data);
	image->pix_data = IMG_AllocPixels(new_size);
	image->upload_width = width_2x;
	image->upload_height = height_2x;

	float* current_pixel = final_2x;
	byte *out_pixel = image->pix_data;
	for (int y = 0; y < height_2x; y++) {
		for (int x = 0; x < width_2x; x++) {
			out_pixel[0] = encode_srgb(current_pixel[0]);
			out_pixel[1] = encode_srgb(current_pixel[1]);
			out_pixel[2] = encode_srgb(current_pixel[2]);
			out_pixel[3] = 255;

			current_pixel += 3;
			out_pixel += 4;
		}
	}

	Z_Free(final_2x);
}

image_t *vkpt_fake_emissive_texture(image_t *image, int bright_threshold_int)
{
	if(!image)
		return NULL;

	if((image->upload_width == 1) && (image->upload_height == 1))
	{
		// Not much to do...
		return image;
	}

	// Construct a new name for the fake emissive image
	const char emissive_image_suffix[] = "*E.wal"; // 'fake' extension needed for image lookup logic
	char emissive_image_name[MAX_QPATH];
	Q_strlcpy(emissive_image_name, image->name, sizeof(emissive_image_name));
	size_t pos = strlen(emissive_image_name) - 4;
	if (pos + sizeof(emissive_image_suffix) > sizeof(emissive_image_name))
		pos = sizeof(emissive_image_name) - sizeof(emissive_image_suffix);
	Q_strlcpy(emissive_image_name + pos, emissive_image_suffix, sizeof(emissive_image_name) - pos);

	// See if we previously created a fake emissive texture for the same base texture
	image_t *prev_image = IMG_FindExisting(emissive_image_name, image->type);
	if(prev_image != R_NOTEXTURE)
	{
		prev_image->registration_sequence = registration_sequence;
		return prev_image;
	}

	image_t *new_image = IMG_Clone(image, emissive_image_name);
	if(new_image == R_NOTEXTURE)
		return image;

	new_image->flags |= IF_FAKE_EMISSIVE | (Q_clip_uint8(bright_threshold_int) << IF_FAKE_EMISSIVE_THRESH_SHIFT);
	apply_fake_emissive_threshold(new_image, bright_threshold_int);

	return new_image;
}

void
vkpt_extract_emissive_texture_info(image_t *image)
{
	int w = image->upload_width;
	int h = image->upload_height;

	byte* current_pixel = image->pix_data;
	vec3_t emissive_color;
	VectorClear(emissive_color);

	int min_x = w;
	int max_x = -1;
	int min_y = h;
	int max_y = -1;
	
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			if(current_pixel[0] + current_pixel[1] + current_pixel[2] > 0)
			{
				vec3_t color;
				color[0] = decode_srgb(current_pixel[0]);
				color[1] = decode_srgb(current_pixel[1]);
				color[2] = decode_srgb(current_pixel[2]);

				color[0] = max(0.f, color[0] + EMISSIVE_TRANSFORM_BIAS);
				color[1] = max(0.f, color[1] + EMISSIVE_TRANSFORM_BIAS);
				color[2] = max(0.f, color[2] + EMISSIVE_TRANSFORM_BIAS);

				VectorAdd(emissive_color, color, emissive_color);

				min_x = min(min_x, x);
				min_y = min(min_y, y);
				max_x = max(max_x, x);
				max_y = max(max_y, y);
			}
			
			current_pixel += 4;
		}
	}

	if (min_x <= max_x && min_y <= max_y)
	{
		float normalization = 1.f / (float)((max_x - min_x + 1) * (max_y - min_y + 1));
		VectorScale(emissive_color, normalization, image->light_color);
	}
	else
	{
		VectorSet(image->light_color, 0.f, 0.f, 0.f);
	}

	image->min_light_texcoord[0] = (float)min_x / (float)w;
	image->min_light_texcoord[1] = (float)min_y / (float)h;
	image->max_light_texcoord[0] = (float)(max_x + 1) / (float)w;
	image->max_light_texcoord[1] = (float)(max_y + 1) / (float)h;

	image->entire_texture_emissive = (min_x == 0) && (min_y == 0) && (max_x == w - 1) && (max_y == h - 1);

	image->processing_complete = true;
}

void
IMG_Load_RTX(image_t *image, byte *pic)
{
	image->pix_data = pic;
	image_loading_dirty_flag = 1;
}

void
IMG_Unload_RTX(image_t *image)
{
	if(image->pix_data)
		Z_Free(image->pix_data);
	image->pix_data = NULL;

	const uint32_t index = image - r_images;

	if (tex_images[index])
	{
		const uint32_t frame_index = (qvk.frame_counter + MAX_FRAMES_IN_FLIGHT + 1) % DESTROY_LATENCY;
		UnusedResources* unused_resources = texture_system.unused_resources + frame_index;

		const uint32_t unused_index = unused_resources->image_num++;

		unused_resources->images[unused_index] = tex_images[index];
		unused_resources->image_memory[unused_index] = tex_image_memory[index];
		unused_resources->image_views[unused_index] = tex_image_views[index];
		unused_resources->image_views_mip0[unused_index] = tex_image_views_mip0[index];

		tex_images[index] = VK_NULL_HANDLE;
		tex_image_views[index] = VK_NULL_HANDLE;
		tex_image_views_mip0[index] = VK_NULL_HANDLE;
		tex_upload_frames[index] = 0;

		vkpt_invalidate_texture_descriptors();
	}
}

void IMG_ReloadAll(void)
{
    int i, reloaded=0;
    image_t * image;

    for (i = 1, image = r_images + 1; i < r_numImages; i++, image++)
    {
        if (!image->registration_sequence)
            continue;

        if (image->type == IT_FONT || image->type == IT_PIC)
            continue;

        // check if image has been updated since previous load

        char const * filepath = image->filepath[0] ? image->filepath : image->name;

        uint64_t last_modifed;
        if (FS_LastModified(filepath, &last_modifed) != Q_ERR_SUCCESS)
        {
            //Com_EPrintf("Could not find '%s' (%s)\n", image->name, image->filepath);
            continue;
        }

        if (last_modifed <= image->last_modified)
            continue; // skip if file has not been modified since last read

        // image has been modified : try loading in new_image
        image_t new_image;
        if (load_img(filepath, &new_image) == Q_ERR_SUCCESS)
        {
            Z_Free(image->pix_data);

            image->pix_data = new_image.pix_data;
            image->width = new_image.width;
            image->height = new_image.width;
            image->upload_width = new_image.upload_width;
            image->upload_height = new_image.upload_height;
            image->processing_complete = false;

            if (strstr(filepath, "_n."))
            {
                image->flags |= IF_NORMAL_MAP;
            }
            if(image->flags & IF_FAKE_EMISSIVE)
            {
                apply_fake_emissive_threshold(image, (image->flags >> IF_FAKE_EMISSIVE_THRESH_SHIFT) & 0xff);
            }

            IMG_Load(image, new_image.pix_data);

            image->last_modified = last_modifed; // reset time stamp because load_img doesn't

            // destroy Vk ressources to force vkpt_textures_end_registration
            // to recreate them next time a frame is drawn
            if (tex_image_views[i]) {
                vkDestroyImageView(qvk.device, tex_image_views[i], NULL);
                tex_image_views[i] = VK_NULL_HANDLE;
            }
            if (tex_image_views_mip0[i]) {
                vkDestroyImageView(qvk.device, tex_image_views_mip0[i], NULL);
                tex_image_views_mip0[i] = VK_NULL_HANDLE;
            }
            if (tex_images[i]) {
                vkDestroyImage(qvk.device, tex_images[i], NULL);
                tex_images[i] = VK_NULL_HANDLE;
                free_device_memory(tex_device_memory_allocator, &tex_image_memory[i]);
            }
            ++reloaded;
            Com_Printf("Reloaded '%s'\n", image->name);
        }
        //else
        //    Com_EPrintf("Skipped '%s'\n", image->name);
    }
    Com_Printf("Reloaded %d textures\n", reloaded);
}

void create_invalid_texture(void)
{
	const VkImageCreateInfo image_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM,
		.extent = { 1, 1, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};
	_VK(vkCreateImage(qvk.device, &image_create_info, NULL, &tex_invalid_texture_image));

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements(qvk.device, tex_invalid_texture_image, &memory_requirements);

	tex_invalid_texture_image_memory.alignment = memory_requirements.alignment;
	tex_invalid_texture_image_memory.size = memory_requirements.size;
	tex_invalid_texture_image_memory.memory_type = get_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	allocate_device_memory(tex_device_memory_allocator, &tex_invalid_texture_image_memory);

	_VK(vkBindImageMemory(qvk.device, tex_invalid_texture_image, tex_invalid_texture_image_memory.memory,
		tex_invalid_texture_image_memory.memory_offset));

	const VkImageSubresourceRange subresource_range = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.levelCount = 1,
		.layerCount = 1
	};

	const VkImageViewCreateInfo image_view_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = tex_invalid_texture_image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = image_create_info.format,
		.subresourceRange = subresource_range
	};

	_VK(vkCreateImageView(qvk.device, &image_view_create_info, NULL, &tex_invalid_texture_image_view));

	VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

	IMAGE_BARRIER(cmd_buf,
		.image = tex_invalid_texture_image,
		.subresourceRange = subresource_range,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	);

	const VkClearColorValue color = {
		.float32[0] = 1.0f,
		.float32[1] = 0.0f,
		.float32[2] = 1.0f,
		.float32[3] = 1.0f
	};
	const VkImageSubresourceRange range = subresource_range;
	vkCmdClearColorImage(cmd_buf, tex_invalid_texture_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);

	IMAGE_BARRIER(cmd_buf,
		.image = tex_invalid_texture_image,
		.subresourceRange = subresource_range,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
	);

	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, true);

	vkQueueWaitIdle(qvk.queue_graphics);
}

void destroy_invalid_texture(void)
{
	vkDestroyImage(qvk.device, tex_invalid_texture_image, NULL);
	vkDestroyImageView(qvk.device, tex_invalid_texture_image_view, NULL);
	free_device_memory(tex_device_memory_allocator, &tex_invalid_texture_image_memory);
}

static void normalize_write_descriptor(uint32_t frame, uint32_t index, VkImageView image_view)
{
	VkDescriptorImageInfo image_info = {
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		.imageView = image_view
	};

	VkWriteDescriptorSet write_info = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = normalize_descriptor_sets[frame],
		.dstBinding = 0,
		.dstArrayElement = index,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.pImageInfo = &image_info
	};

	vkUpdateDescriptorSets(qvk.device, 1, &write_info, 0, NULL);
}

static void normalize_init(void)
{
	VkDescriptorSetLayoutBinding binding = {
		.binding = 0,
		.descriptorCount = MAX_RIMAGES,
		.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
	};

	VkDescriptorSetLayoutCreateInfo dsl_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding
	};

	_VK(vkCreateDescriptorSetLayout(qvk.device, &dsl_info, NULL, &normalize_desc_set_layout));

	VkPushConstantRange push_range = {
		.size = sizeof(uint32_t),
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
	};

	VkPipelineLayoutCreateInfo pl_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &normalize_desc_set_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &push_range
	};

	_VK(vkCreatePipelineLayout(qvk.device, &pl_info, NULL, &normalize_pipeline_layout));

	VkComputePipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.layout = normalize_pipeline_layout,
		.stage = SHADER_STAGE(QVK_MOD_NORMALIZE_NORMAL_MAP_COMP, VK_SHADER_STAGE_COMPUTE_BIT)
	};

	_VK(vkCreateComputePipelines(qvk.device, NULL, 1, &pipeline_info, NULL, &normalize_pipeline));

	VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = desc_pool_textures,
		.descriptorSetCount = 1,
		.pSetLayouts = &normalize_desc_set_layout
	};

	for (int frame = 0; frame < MAX_FRAMES_IN_FLIGHT; frame++)
	{
		_VK(vkAllocateDescriptorSets(qvk.device, &alloc_info, &normalize_descriptor_sets[frame]));
		ATTACH_LABEL_VARIABLE(normalize_descriptor_sets[frame], DESCRIPTOR_SET);

		for (int i = 0; i < MAX_RIMAGES; i++)
		{
			normalize_write_descriptor(frame, i, tex_invalid_texture_image_view);
		}
	}
}

static void normalize_destroy(void)
{
	vkDestroyPipeline(qvk.device, normalize_pipeline, NULL);
	normalize_pipeline = NULL;

	vkDestroyPipelineLayout(qvk.device, normalize_pipeline_layout, NULL);
	normalize_pipeline_layout = NULL;

	vkDestroyDescriptorSetLayout(qvk.device, normalize_desc_set_layout, NULL);
	normalize_desc_set_layout = NULL;
	
	memset(normalize_descriptor_sets, 0, sizeof(normalize_descriptor_sets));
}

VkResult
vkpt_textures_initialize()
{
	vkpt_invalidate_texture_descriptors();
	memset(&texture_system, 0, sizeof(texture_system));

	tex_device_memory_allocator = create_device_memory_allocator(qvk.device, "texture device memory");

	create_invalid_texture();

	VkSamplerCreateInfo sampler_info = {
		.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter               = VK_FILTER_LINEAR,
		.minFilter               = VK_FILTER_LINEAR,
		.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.anisotropyEnable        = VK_TRUE,
		.maxAnisotropy           = 16,
		.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
		.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.minLod                  = 0.0f,
		.maxLod                  = 128.0f,
	};
	_VK(vkCreateSampler(qvk.device, &sampler_info, NULL, &qvk.tex_sampler));
	ATTACH_LABEL_VARIABLE(qvk.tex_sampler, SAMPLER);

	VkSamplerCreateInfo sampler_nearest_info = {
		.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter               = VK_FILTER_NEAREST,
		.minFilter               = VK_FILTER_NEAREST,
		.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.anisotropyEnable        = VK_FALSE,
		.maxAnisotropy           = 16,
		.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
		.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST,
	};
	_VK(vkCreateSampler(qvk.device, &sampler_nearest_info, NULL, &qvk.tex_sampler_nearest));
	ATTACH_LABEL_VARIABLE(qvk.tex_sampler_nearest, SAMPLER);

	VkSamplerCreateInfo sampler_nearest_mipmap_aniso_info = {
		.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter               = VK_FILTER_NEAREST,
		.minFilter               = VK_FILTER_LINEAR,
		.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.anisotropyEnable        = VK_TRUE,
		.maxAnisotropy           = 16,
		.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
		.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.minLod                  = 0.0f,
		.maxLod                  = 128.0f,
	};
	_VK(vkCreateSampler(qvk.device, &sampler_nearest_mipmap_aniso_info, NULL, &qvk.tex_sampler_nearest_mipmap_aniso));
	ATTACH_LABEL_VARIABLE(qvk.tex_sampler_nearest_mipmap_aniso, SAMPLER);

	VkSamplerCreateInfo sampler_linear_clamp_info = {
		.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter               = VK_FILTER_LINEAR,
		.minFilter               = VK_FILTER_LINEAR,
		.addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.anisotropyEnable        = VK_FALSE,
		.maxAnisotropy           = 16,
		.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
		.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
	};
	_VK(vkCreateSampler(qvk.device, &sampler_linear_clamp_info, NULL, &qvk.tex_sampler_linear_clamp));
	ATTACH_LABEL_VARIABLE(qvk.tex_sampler_linear_clamp, SAMPLER);

	VkDescriptorSetLayoutBinding layout_bindings[] = {
		{
			.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = MAX_RIMAGES,
			.binding         = 0,
			.stageFlags      = VK_SHADER_STAGE_ALL,
		},
#define IMG_DO(_name, _binding, ...) \
		{ \
			.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, \
			.descriptorCount = 1, \
			.binding         = BINDING_OFFSET_IMAGES + _binding, \
			.stageFlags      = VK_SHADER_STAGE_ALL, \
		},
	LIST_IMAGES
	LIST_IMAGES_A_B
#undef IMG_DO
#define IMG_DO(_name, _binding, ...) \
		{ \
			.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, \
			.descriptorCount = 1, \
			.binding         = BINDING_OFFSET_TEXTURES + _binding, \
			.stageFlags      = VK_SHADER_STAGE_ALL, \
		},
	LIST_IMAGES
	LIST_IMAGES_A_B
#undef IMG_DO
		{
			.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.binding         = BINDING_OFFSET_BLUE_NOISE,
			.stageFlags      = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.binding         = BINDING_OFFSET_ENVMAP,
			.stageFlags      = VK_SHADER_STAGE_ALL,
		},
        {
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .binding = BINDING_OFFSET_PHYSICAL_SKY,
            .stageFlags = VK_SHADER_STAGE_ALL,
        },
        {
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .binding = BINDING_OFFSET_PHYSICAL_SKY_IMG,
            .stageFlags = VK_SHADER_STAGE_ALL,
        },
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.binding = BINDING_OFFSET_SKY_TRANSMITTANCE,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.binding = BINDING_OFFSET_SKY_SCATTERING,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.binding = BINDING_OFFSET_SKY_IRRADIANCE,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.binding = BINDING_OFFSET_SKY_CLOUDS,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.binding = BINDING_OFFSET_TERRAIN_ALBEDO,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.binding = BINDING_OFFSET_TERRAIN_NORMALS,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.binding = BINDING_OFFSET_TERRAIN_DEPTH,
			.stageFlags = VK_SHADER_STAGE_ALL,
		},
		{
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.binding = BINDING_OFFSET_TERRAIN_SHADOWMAP,
			.stageFlags = VK_SHADER_STAGE_ALL,
		}
	};

	VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = LENGTH(layout_bindings),
		.pBindings    = layout_bindings,
	};

	_VK(vkCreateDescriptorSetLayout(qvk.device, &layout_info, NULL, &qvk.desc_set_layout_textures));
	ATTACH_LABEL_VARIABLE(qvk.desc_set_layout_textures, DESCRIPTOR_SET_LAYOUT);

	VkDescriptorPoolSize pool_sizes[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = MAX_FRAMES_IN_FLIGHT * (MAX_RIMAGES + 2 * NUM_VKPT_IMAGES) + 128,
		},
		{
			.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.descriptorCount = MAX_FRAMES_IN_FLIGHT * MAX_RIMAGES
		}
	};

	VkDescriptorPoolCreateInfo pool_info = {
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = LENGTH(pool_sizes),
		.pPoolSizes    = pool_sizes,
		.maxSets       = MAX_FRAMES_IN_FLIGHT * 2,
	};

	_VK(vkCreateDescriptorPool(qvk.device, &pool_info, NULL, &desc_pool_textures));
	ATTACH_LABEL_VARIABLE(desc_pool_textures, DESCRIPTOR_POOL);
	
	VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {
		.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool     = desc_pool_textures,
		.descriptorSetCount = 1,
		.pSetLayouts        = &qvk.desc_set_layout_textures,
	};

	_VK(vkAllocateDescriptorSets(qvk.device, &descriptor_set_alloc_info, &qvk.desc_set_textures_even));
	_VK(vkAllocateDescriptorSets(qvk.device, &descriptor_set_alloc_info, &qvk.desc_set_textures_odd));

	ATTACH_LABEL_VARIABLE(qvk.desc_set_textures_even, DESCRIPTOR_SET);
	ATTACH_LABEL_VARIABLE(qvk.desc_set_textures_odd, DESCRIPTOR_SET);
	
	normalize_init();

	if(load_blue_noise() != VK_SUCCESS)
		return VK_ERROR_INITIALIZATION_FAILED;

	LOG_FUNC();
	return VK_SUCCESS;
}

static void
destroy_tex_images(void)
{
	for(int i = 0; i < MAX_RIMAGES; i++) {
		if(tex_image_views[i]) {
			vkDestroyImageView(qvk.device, tex_image_views[i], NULL);
			tex_image_views[i] = VK_NULL_HANDLE;
		}
		if (tex_image_views_mip0[i]) {
			vkDestroyImageView(qvk.device, tex_image_views_mip0[i], NULL);
			tex_image_views_mip0[i] = VK_NULL_HANDLE;
		}
		if(tex_images[i]) {
			vkDestroyImage(qvk.device, tex_images[i], NULL);
			tex_images[i] = VK_NULL_HANDLE;

			free_device_memory(tex_device_memory_allocator, &tex_image_memory[i]);
		}
	}

	for(uint32_t i = 0; i < DESTROY_LATENCY; i++) {
		textures_destroy_unused_set(i);
	}
}

VkResult
vkpt_textures_destroy()
{
	destroy_tex_images();
	vkDestroyDescriptorSetLayout(qvk.device, qvk.desc_set_layout_textures, NULL);
	vkDestroyDescriptorPool(qvk.device, desc_pool_textures, NULL);

	vkFreeMemory      (qvk.device, mem_blue_noise,      NULL);
	vkDestroyImage    (qvk.device, img_blue_noise,      NULL);
	vkDestroyImageView(qvk.device, imv_blue_noise,      NULL);
	vkDestroySampler  (qvk.device, qvk.tex_sampler, NULL);
	vkDestroySampler  (qvk.device, qvk.tex_sampler_nearest, NULL);
	vkDestroySampler  (qvk.device, qvk.tex_sampler_nearest_mipmap_aniso, NULL);
	vkDestroySampler  (qvk.device, qvk.tex_sampler_linear_clamp, NULL);

	destroy_envmap();
	destroy_invalid_texture();
	destroy_device_memory_allocator(tex_device_memory_allocator);
	tex_device_memory_allocator = NULL;

	normalize_destroy();

	LOG_FUNC();
	return VK_SUCCESS;
}

#ifdef VKPT_DEVICE_GROUPS
static VkMemoryAllocateFlagsInfo mem_alloc_flags_broadcast = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
		.flags = VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT,
};
#endif

static VkFormat get_image_format(image_t *q_img)
{
	switch(q_img->pixel_format)
	{
	case PF_R8G8B8A8_UNORM:
		return q_img->is_srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
	case PF_R16_UNORM:
		return VK_FORMAT_R16_UNORM;
	}
	assert(false);
	return VK_FORMAT_R8G8B8A8_UNORM;
}

VkResult
vkpt_textures_end_registration()
{
	if(!image_loading_dirty_flag)
		return VK_SUCCESS;
	image_loading_dirty_flag = 0;

	VkImageCreateInfo img_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.extent = {
			.width  = 1337,
			.height = 1337,
			.depth  = 1
		},
		.imageType             = VK_IMAGE_TYPE_2D,
		.format                = VK_FORMAT_UNDEFINED,
		.mipLevels             = 1,
		.arrayLayers           = 1,
		.samples               = VK_SAMPLE_COUNT_1_BIT,
		.tiling                = VK_IMAGE_TILING_OPTIMAL,
		.usage                 = VK_IMAGE_USAGE_TRANSFER_DST_BIT
		                       | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
		                       | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = qvk.queue_idx_graphics,
		.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VkImageViewCreateInfo img_view_info = {
		.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.viewType   = VK_IMAGE_VIEW_TYPE_2D,
		.format     = VK_FORMAT_UNDEFINED,
		.subresourceRange = {
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel   = 0,
			.levelCount     = 1,
			.baseArrayLayer = 0,
			.layerCount     = 1
		},
		.components     = {
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_B,
			VK_COMPONENT_SWIZZLE_A
		},
	};

#ifdef VKPT_DEVICE_GROUPS
	if (qvk.device_count > 1) {
		mem_alloc_flags_broadcast.deviceMask = (1 << qvk.device_count) - 1;
	}
#endif

	// Phase 1: Create the new texture objects, count the memory required to upload them all.
	// Also, delete any storage image descriptors that may exist for previously uploaded textures.

	uint32_t new_image_num = 0;
	size_t   total_size = 0;
	for(int i = 0; i < MAX_RIMAGES; i++)
	{
		image_t *q_img = r_images + i;

		if (tex_upload_frames[i] == qvk.current_frame_index + 1)
		{
			normalize_write_descriptor(qvk.current_frame_index, i, tex_invalid_texture_image_view);
			tex_upload_frames[i] = 0;
		}

		if (tex_images[i] != VK_NULL_HANDLE || !q_img->registration_sequence || q_img->pix_data == NULL)
			continue;

		img_info.extent.width = q_img->upload_width;
		img_info.extent.height = q_img->upload_height;
		img_info.mipLevels = get_num_miplevels(q_img->upload_width, q_img->upload_height);
		img_info.format = get_image_format(q_img);
		if (!q_img->is_srgb)
			img_info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
		else
			img_info.usage &= ~VK_IMAGE_USAGE_STORAGE_BIT;
		
		_VK(vkCreateImage(qvk.device, &img_info, NULL, tex_images + i));
		ATTACH_LABEL_VARIABLE_NAME(tex_images[i], IMAGE, q_img->name);

		tex_upload_frames[i] = qvk.current_frame_index + 1;

		VkMemoryRequirements mem_req;
		vkGetImageMemoryRequirements(qvk.device, tex_images[i], &mem_req);

		assert(!(mem_req.alignment & (mem_req.alignment - 1)));
		total_size += mem_req.alignment - 1;
		total_size &= ~(mem_req.alignment - 1);
		total_size += mem_req.size;

		DeviceMemory* image_memory = tex_image_memory + i;
		image_memory->size = mem_req.size;
		image_memory->alignment = mem_req.alignment;
		image_memory->memory_type = get_memory_type(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		DMAResult alloc_result = allocate_device_memory(tex_device_memory_allocator, image_memory);
		if (alloc_result != DMA_SUCCESS)
		{
			Com_Error(ERR_FATAL, "Failed to allocate GPU memory for game textures!\n");
			return VK_ERROR_OUT_OF_DEVICE_MEMORY;
		}

		VkBindImageMemoryInfo* bind_info = tex_bind_image_info + new_image_num;
		bind_info->sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
		bind_info->image = tex_images[i];
		bind_info->memory = image_memory->memory;
		bind_info->memoryOffset = image_memory->memory_offset;
		
		new_image_num++;
	}

	if (new_image_num == 0)
		return VK_SUCCESS;

	// Phase 2: Bind the image memory, create the views, create the storage image descriptors where appropriate.

	vkBindImageMemory2(qvk.device, new_image_num, tex_bind_image_info);
	
	for (int i = 0; i < MAX_RIMAGES; i++)
	{
		if (tex_upload_frames[i] != qvk.current_frame_index + 1)
			continue;

		image_t* q_img = r_images + i;
		
		int num_mip_levels = get_num_miplevels(q_img->upload_width, q_img->upload_height);

		img_view_info.image = tex_images[i];
		img_view_info.subresourceRange.levelCount = num_mip_levels;
		img_view_info.format = get_image_format(q_img);
		_VK(vkCreateImageView(qvk.device, &img_view_info, NULL, tex_image_views + i));
		ATTACH_LABEL_VARIABLE_NAME(tex_image_views[i], IMAGE_VIEW, va("tex_image_view:%s", q_img->name));

		if (!q_img->is_srgb)
		{
			img_view_info.subresourceRange.levelCount = 1;
			_VK(vkCreateImageView(qvk.device, &img_view_info, NULL, tex_image_views_mip0 + i));
			ATTACH_LABEL_VARIABLE_NAME(tex_image_views_mip0[i], IMAGE_VIEW, va("tex_image_view_mip0:%s", q_img->name));

			normalize_write_descriptor(qvk.current_frame_index, i, tex_image_views_mip0[i]);
		}
	}

	// Phase 3: Upload the image data.

	BufferResource_t buf_img_upload;
	buffer_create(&buf_img_upload, total_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	
	VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

	char *staging_buffer = buffer_map(&buf_img_upload);

	size_t offset = 0;
	for (int i = 0; i < MAX_RIMAGES; i++)
	{
		image_t *q_img = r_images + i;

		if (tex_upload_frames[i] != qvk.current_frame_index + 1)
			continue;
		
		int num_mip_levels = get_num_miplevels(q_img->upload_width, q_img->upload_height);

		VkMemoryRequirements mem_req;
		vkGetImageMemoryRequirements(qvk.device, tex_images[i], &mem_req);

		assert(!(mem_req.alignment & (mem_req.alignment - 1)));
		offset += mem_req.alignment - 1;
		offset &= ~(mem_req.alignment - 1);

		int wd = q_img->upload_width;
		int ht = q_img->upload_height;

		VkImageSubresourceRange subresource_range = {
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel   = 0,
			.levelCount     = num_mip_levels,
			.baseArrayLayer = 0,
			.layerCount     = 1
		};

		IMAGE_BARRIER(cmd_buf,
			.image            = tex_images[i],
			.subresourceRange = subresource_range,
			.srcAccessMask    = 0,
			.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
		);

		int bytes_per_pixel = q_img->pixel_format == PF_R16_UNORM ? 2 : 4;
		memcpy(staging_buffer + offset, q_img->pix_data, wd * ht * bytes_per_pixel);

		VkBufferImageCopy cpy_info = {
			.bufferOffset = offset,
			.imageSubresource = { 
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel       = 0,
				.baseArrayLayer = 0,
				.layerCount     = 1,
			},
			.imageOffset    = { 0, 0, 0 },
			.imageExtent    = { wd, ht, 1 }
		};

		vkCmdCopyBufferToImage(cmd_buf, buf_img_upload.buffer, tex_images[i],
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cpy_info);

		// Transition mip 0 to VK_IMAGE_LAYOUT_GENERAL for use in the next command list.

		subresource_range.baseMipLevel = 0;
		subresource_range.levelCount = 1;

		IMAGE_BARRIER(cmd_buf,
			.image = tex_images[i],
			.subresourceRange = subresource_range,
			.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL
		);

		offset += mem_req.size;
	}

	buffer_unmap(&buf_img_upload);
	staging_buffer = NULL;

	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, true);

	// Phase 4: Process the normal maps using a compute shader, generate mipmaps.
	// This phase executes in a separate command buffer because Vulkan thinks that
	// the normalization pass may access all descriptors in the set, even those which
	// are not yet transitioned from LAYOUT_UNDEFINED to LAYOUT_GENERAL. So the
	// transitions happen in the previous phase command buffer.

	cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

	for (int i = 0; i < MAX_RIMAGES; i++)
	{
		image_t* q_img = r_images + i;

		if (tex_upload_frames[i] != qvk.current_frame_index + 1)
			continue;

		VkImageSubresourceRange subresource_range = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		};

		bool normalize = (q_img->flags & IF_NORMAL_MAP) && !q_img->is_srgb;

		if (normalize)
		{
			vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, normalize_pipeline);

			vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, normalize_pipeline_layout,
				0, 1, &normalize_descriptor_sets[qvk.current_frame_index], 0, NULL);

			// Push constant: image index.
			vkCmdPushConstants(cmd_buf, normalize_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &i);

			vkCmdDispatch(cmd_buf, 
				(q_img->upload_width + 15) / 16, 
				(q_img->upload_height + 15) / 16, 1);
		}

		int num_mip_levels = get_num_miplevels(q_img->upload_width, q_img->upload_height);

		int wd = q_img->upload_width;
		int ht = q_img->upload_height;
		
		for (int mip = 1; mip < num_mip_levels; mip++) 
		{
			subresource_range.baseMipLevel = mip - 1;
			
			IMAGE_BARRIER(cmd_buf,
				.image = tex_images[i],
				.subresourceRange = subresource_range,
				.srcAccessMask = (normalize && mip == 1) ? VK_ACCESS_SHADER_WRITE_BIT : VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
				.oldLayout = (mip == 1) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			);
			
			int nwd = (wd > 1) ? (wd >> 1) : wd;
			int nht = (ht > 1) ? (ht >> 1) : ht;

			VkImageBlit region = {
				.srcSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = mip - 1,
					.baseArrayLayer = 0,
					.layerCount = 1
				},
				.srcOffsets = { 
					{ 0, 0, 0 }, 
					{ wd, ht, 1 } },

				.dstSubresource = {
					.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel = mip,
					.baseArrayLayer = 0,
					.layerCount = 1
				},
				.dstOffsets = { 
					{ 0, 0, 0 }, 
					{ nwd, nht, 1 } }
			};

			vkCmdBlitImage(
				cmd_buf, 
				tex_images[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
				tex_images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
				1, &region, 
				VK_FILTER_LINEAR);
			
			IMAGE_BARRIER(cmd_buf,
				.image = tex_images[i],
				.subresourceRange = subresource_range,
				.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			);

			wd = nwd;
			ht = nht;
		}

		subresource_range.baseMipLevel = num_mip_levels - 1;

		IMAGE_BARRIER(cmd_buf,
			.image = tex_images[i],
			.subresourceRange = subresource_range,
			.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			);
	}
	
	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, true);

	// Schedule the upload buffer for delayed destruction.

	const uint32_t destroy_frame_index = (qvk.frame_counter + MAX_FRAMES_IN_FLIGHT) % DESTROY_LATENCY;
	UnusedResources* unused_resources = texture_system.unused_resources + destroy_frame_index;

	const uint32_t unused_index = unused_resources->buffer_num++;
	unused_resources->buffers[unused_index] = buf_img_upload.buffer;
	unused_resources->buffer_memory[unused_index] = buf_img_upload.memory;

	vkpt_invalidate_texture_descriptors();

	size_t texture_memory_allocated, texture_memory_used;
	get_device_malloc_stats(tex_device_memory_allocator, &texture_memory_allocated, &texture_memory_used);
	Com_DPrintf("Texture pool: using %.2f MB, allocated %.2f MB\n", 
		(float)texture_memory_used / megabyte, (float)texture_memory_allocated / megabyte);

	return VK_SUCCESS;
}

void vkpt_textures_update_descriptor_set()
{
	if (!descriptor_set_dirty_flags[qvk.current_frame_index])
		return;

	descriptor_set_dirty_flags[qvk.current_frame_index] = 0;
	
	for(int i = 0; i < MAX_RIMAGES; i++) {
		image_t *q_img = r_images + i;
		
		VkImageView image_view = tex_image_views[i];
		if (image_view == VK_NULL_HANDLE)
			image_view = tex_invalid_texture_image_view;
		
		VkSampler sampler = qvk.tex_sampler;

		if (q_img->type == IT_WALL || q_img->type == IT_SKIN) {
			if (cvar_pt_nearest->integer == 1)
				sampler = qvk.tex_sampler_nearest_mipmap_aniso;
			else if (cvar_pt_nearest->integer >= 2)
				sampler = qvk.tex_sampler_nearest;
		} else if (q_img->flags & IF_NEAREST) {
			sampler = qvk.tex_sampler_nearest;
		} else if (q_img->flags & IF_BILERP) {
			sampler = qvk.tex_sampler_linear_clamp;
		} else if (q_img->type == IT_SPRITE) {
			sampler = qvk.tex_sampler_linear_clamp;
		} else if (q_img->type == IT_FONT) {
			sampler = (cvar_pt_bilerp_chars->integer == 0) ? qvk.tex_sampler_nearest : qvk.tex_sampler_linear_clamp;
		} else if (q_img->type == IT_PIC) {
			sampler = (cvar_pt_bilerp_pics->integer == 0) ? qvk.tex_sampler_nearest : qvk.tex_sampler_linear_clamp;
		} 

		VkDescriptorImageInfo img_info = {
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
			.imageView   = image_view,
			.sampler     = sampler,
		};

		VkWriteDescriptorSet descriptor_set_write = {
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = qvk_get_current_desc_set_textures(),
			.dstBinding      = GLOBAL_TEXTURES_TEX_ARR_BINDING_IDX,
			.dstArrayElement = i,
			.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.pImageInfo      = &img_info,
		};

		vkUpdateDescriptorSets(qvk.device, 1, &descriptor_set_write, 0, NULL);
	}
}

static VkResult
create_readback_image(VkImage *image, VkDeviceMemory *memory, VkDeviceSize *memory_size, VkFormat format, uint32_t width, uint32_t height)
{
	VkImageCreateInfo dump_image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent.width = width,
		.extent.height = height,
		.extent.depth = 1,
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_LINEAR,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	_VK(vkCreateImage(qvk.device, &dump_image_info, NULL, image));

	VkMemoryRequirements mem_req;
	vkGetImageMemoryRequirements(qvk.device, *image, &mem_req);

	VkMemoryAllocateInfo mem_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_req.size,
		.memoryTypeIndex = get_memory_type(mem_req.memoryTypeBits,
									VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT),
	};

	_VK(vkAllocateMemory(qvk.device, &mem_alloc_info, NULL, memory));
	_VK(vkBindImageMemory(qvk.device, *image, *memory, 0));

	*memory_size = mem_req.size;

	return VK_SUCCESS;
}

static void
destroy_readback_image(VkImage *image, VkDeviceMemory *memory, VkDeviceSize *memory_size)
{
	vkDestroyImage(qvk.device, *image, NULL);
	*image = VK_NULL_HANDLE;

	vkFreeMemory(qvk.device, *memory, NULL);
	*memory = VK_NULL_HANDLE;
	*memory_size = 0;
}

static VkDeviceSize available_video_memory(void)
{
	VkDeviceSize mem = 0;
	for (uint32_t heap_num = 0; heap_num < qvk.mem_properties.memoryHeapCount; heap_num++)
	{
		if((qvk.mem_properties.memoryHeaps[heap_num].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
			mem += qvk.mem_properties.memoryHeaps[heap_num].size;
	}
	return mem;
}

#define IMAGE_CREATE_INFO(_vkformat, _w, _h)	\
	{ \
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, \
		.imageType = VK_IMAGE_TYPE_2D, \
		.format = _vkformat, \
		.extent = { \
			.width  = _w, \
			.height = _h, \
			.depth  = 1 \
		}, \
		.mipLevels             = 1, \
		.arrayLayers           = 1, \
		.samples               = VK_SAMPLE_COUNT_1_BIT, \
		.tiling                = VK_IMAGE_TILING_OPTIMAL, \
		.usage                 = VK_IMAGE_USAGE_STORAGE_BIT \
		                       | VK_IMAGE_USAGE_TRANSFER_SRC_BIT \
		                       | VK_IMAGE_USAGE_TRANSFER_DST_BIT \
		                       | VK_IMAGE_USAGE_SAMPLED_BIT \
		                       | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, \
		.sharingMode           = VK_SHARING_MODE_EXCLUSIVE, \
		.queueFamilyIndexCount = qvk.queue_idx_graphics, \
		.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED, \
	}

#define IMAGEVIEW_CREATE_INFO(_image, _vkformat)	\
	{ \
		.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, \
		.viewType   = VK_IMAGE_VIEW_TYPE_2D, \
		.format     = _vkformat, \
		.image      = _image, \
		.subresourceRange = { \
			.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT, \
			.baseMipLevel   = 0, \
			.levelCount     = 1, \
			.baseArrayLayer = 0, \
			.layerCount     = 1 \
		}, \
		.components     = { \
			VK_COMPONENT_SWIZZLE_R, \
			VK_COMPONENT_SWIZZLE_G, \
			VK_COMPONENT_SWIZZLE_B, \
			VK_COMPONENT_SWIZZLE_A \
		}, \
	}

#define IMAGE_WRITE_DESCRIPTOR_SET(_even_odd, _binding, _p_image_info) \
	{ \
		.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, \
		.dstSet          = _even_odd ? qvk.desc_set_textures_odd : qvk.desc_set_textures_even, \
		.dstBinding      = BINDING_OFFSET_IMAGES + _binding, \
		.dstArrayElement = 0, \
		.descriptorCount = 1, \
		.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, \
		.pImageInfo      = _p_image_info, \
	}

#define TEXTURE_WRITE_DESCRIPTOR_SET(_even_odd, _binding, _p_image_info) \
	{ \
		.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, \
		.dstSet          = _even_odd ? qvk.desc_set_textures_odd : qvk.desc_set_textures_even, \
		.dstBinding      = BINDING_OFFSET_TEXTURES + _binding, \
		.dstArrayElement = 0, \
		.descriptorCount = 1, \
		.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, \
		.pImageInfo      = _p_image_info, \
	}

static VkResult create_image(const VkImageCreateInfo *image_create_info, VkImage *image, VkDeviceMemory *image_mem, VkImage *images_local, VkDeviceSize *image_size)
{
	_VK(vkCreateImage(qvk.device, image_create_info, NULL, image));
	ATTACH_LABEL_VARIABLE(*image, IMAGE);

	VkMemoryRequirements mem_req;
	vkGetImageMemoryRequirements(qvk.device, *image, &mem_req);

	*image_size = align(mem_req.size, mem_req.alignment);

	VkResult alloc_result = allocate_gpu_memory(mem_req, image_mem);
	if (alloc_result != VK_SUCCESS)
		return alloc_result;

	ATTACH_LABEL_VARIABLE(*image_mem, DEVICE_MEMORY);

	_VK(vkBindImageMemory(qvk.device, *image, *image_mem, 0));

#ifdef VKPT_DEVICE_GROUPS
	if (qvk.device_count > 1) {
		// create per-device local image bindings so we can copy back and forth

		// create copies of the same image object that will receive full per-GPU mappings
		for(int d = 0; d < qvk.device_count; d++)
		{
			_VK(vkCreateImage(qvk.device, image_create_info, NULL, images_local + d));
			ATTACH_LABEL_VARIABLE(images_local[d], IMAGE);

			uint32_t device_indices[VKPT_MAX_GPUS];

			for (int j = 0; j < VKPT_MAX_GPUS; j++)
			{
				// all GPUs attach to memory on one device for this image object
				device_indices[j] = (uint32_t)d;
			}

			// shut up lunarg
			{
				VkMemoryRequirements mem_req;
				vkGetImageMemoryRequirements(qvk.device, images_local[d], &mem_req);
			}

			VkBindImageMemoryDeviceGroupInfo device_group_info = {
				.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_DEVICE_GROUP_INFO,
				.pNext = NULL,
				.deviceIndexCount = qvk.device_count,
				.pDeviceIndices = device_indices,
				.splitInstanceBindRegionCount = 0,
				.pSplitInstanceBindRegions = NULL,
			};

			VkBindImageMemoryInfo bind_info = {
				.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
				.pNext = &device_group_info,
				.image = images_local[d],
				.memory = *image_mem,
				.memoryOffset = 0,
			};

			_VK(vkBindImageMemory2(qvk.device, 1, &bind_info));
		}
	}
#endif

	return VK_SUCCESS;
}

VkResult
vkpt_create_images()
{
	VkImageCreateInfo images_create_info[NUM_IMAGES] = {
#define IMG_DO(_name, _binding, _vkformat, _glslformat, _w, _h) \
	[VKPT_IMG_##_name] = IMAGE_CREATE_INFO(VK_FORMAT_##_vkformat, _w, _h),
LIST_IMAGES
LIST_IMAGES_A_B
#undef IMG_DO
	};

#ifdef VKPT_DEVICE_GROUPS
	if (qvk.device_count > 1)
	{
#define IMG_DO(_name, _binding, _vkformat, _glslformat, _w, _h) \
	images_create_info[VKPT_IMG_##_name].flags |= \
		VK_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT;
LIST_IMAGES
LIST_IMAGES_A_B
#undef IMG_DO
	}
#endif

	VkDeviceSize total_size = 0;

	for(int i = 0; i < NUM_IMAGES; i++)
	{
		VkImage *images_local = NULL;
	#ifdef VKPT_DEVICE_GROUPS
		images_local = qvk.images_local[i];
	#endif
		VkDeviceSize image_size = 0;
		VkResult create_result = create_image(images_create_info + i, qvk.images + i, mem_images + i, images_local, &image_size);
		total_size += image_size;
		if (create_result != VK_SUCCESS)
		{
            Com_Printf("Memory allocation error. Current total = %.2f MB, failed chunk = %.2f MB\n", (float)total_size / megabyte, (float)image_size / megabyte);
            Com_Error(ERR_FATAL, "Failed to allocate GPU memory for screen-space textures!\n");
			return create_result;
		}
	}

	VkDeviceSize video_memory_size = available_video_memory();
	if(total_size > video_memory_size / 2)
	{
		Com_WPrintf("Warning: The renderer uses %.2f MB for internal screen-space resources, which is\n"
		            "more than half of the available video memory (%.2f MB). This may cause poor performance.\n"
		            "Consider limiting the maximum dynamic resolution scale, using a lower fixed resolution\n"
		            "scale, or lowering the output resolution.\n",
		            (float)total_size / megabyte, (float)video_memory_size / megabyte);
	}
	else
	{
		Com_DPrintf("Screen-space image memory: %.2f MB\n", (float)total_size / megabyte);
	}

	/* attach labels to images */
#define IMG_DO(_name, _binding, ...) \
	ATTACH_LABEL_VARIABLE_NAME(qvk.images[VKPT_IMG_##_name], IMAGE, #_name);	\
	ATTACH_LABEL_VARIABLE_NAME(mem_images[VKPT_IMG_##_name], DEVICE_MEMORY, "mem:" #_name);
	LIST_IMAGES
	LIST_IMAGES_A_B
#undef IMG_DO


#define IMG_DO(_name, _binding, _vkformat, _glslformat, _w, _h) \
	[VKPT_IMG_##_name] = IMAGEVIEW_CREATE_INFO(qvk.images[VKPT_IMG_##_name], VK_FORMAT_##_vkformat),

	VkImageViewCreateInfo images_view_create_info[NUM_IMAGES] = {
		LIST_IMAGES
		LIST_IMAGES_A_B
	};
#undef IMG_DO


	for(int i = 0; i < NUM_IMAGES; i++) {
		_VK(vkCreateImageView(qvk.device, images_view_create_info + i, NULL, qvk.images_views + i));

#ifdef VKPT_DEVICE_GROUPS
		if (qvk.device_count > 1) {
			for(int d = 0; d < qvk.device_count; d++) {
				VkImageViewCreateInfo info = images_view_create_info[i];
				info.image = qvk.images_local[i][d];
				_VK(vkCreateImageView(qvk.device, &info, NULL, &qvk.images_views_local[i][d]));
			}
		}
#endif
	}

	/* attach labels to image views */
#define IMG_DO(_name, ...) \
	ATTACH_LABEL_VARIABLE_NAME(qvk.images_views[VKPT_IMG_##_name], IMAGE_VIEW, #_name);
	LIST_IMAGES
	LIST_IMAGES_A_B
#undef IMG_DO

#define IMG_DO(_name, ...) \
	[VKPT_IMG_##_name] = { \
		.sampler     = VK_NULL_HANDLE, \
		.imageView   = qvk.images_views[VKPT_IMG_##_name], \
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL \
	},
	VkDescriptorImageInfo desc_output_img_info[] = {
		LIST_IMAGES
		LIST_IMAGES_A_B
	};
#undef IMG_DO

	VkDescriptorImageInfo img_info[] = {
#define IMG_DO(_name, ...) \
		[VKPT_IMG_##_name] = { \
			.imageLayout = VK_IMAGE_LAYOUT_GENERAL, \
			.imageView   = qvk.images_views[VKPT_IMG_##_name], \
			.sampler     = qvk.tex_sampler_nearest, \
		},

		LIST_IMAGES
		LIST_IMAGES_A_B
	};
#undef IMG_DO

	for(int i = VKPT_IMG_BLOOM_HBLUR; i <= VKPT_IMG_BLOOM_VBLUR; i++) {
		img_info[i].sampler = qvk.tex_sampler_linear_clamp;
	}
	img_info[VKPT_IMG_ASVGF_TAA_A].sampler = qvk.tex_sampler;
	img_info[VKPT_IMG_ASVGF_TAA_B].sampler = qvk.tex_sampler;
	img_info[VKPT_IMG_TAA_OUTPUT].sampler = qvk.tex_sampler;

	VkWriteDescriptorSet output_img_write[NUM_IMAGES * 2];

	for (int even_odd = 0; even_odd < 2; even_odd++)
	{
		/* create information to update descriptor sets */
#define IMG_DO(_name, _binding, ...) { \
			VkWriteDescriptorSet elem_image = IMAGE_WRITE_DESCRIPTOR_SET(even_odd, _binding, desc_output_img_info + VKPT_IMG_##_name); \
			VkWriteDescriptorSet elem_texture = TEXTURE_WRITE_DESCRIPTOR_SET(even_odd, _binding, img_info + VKPT_IMG_##_name); \
			output_img_write[2 * VKPT_IMG_##_name] = elem_image; \
			output_img_write[2 * VKPT_IMG_##_name + 1] = elem_texture; \
		}
		LIST_IMAGES;
		if (even_odd)
		{
			LIST_IMAGES_B_A;
		}
		else
		{
			LIST_IMAGES_A_B;
		}
#undef IMG_DO

		vkUpdateDescriptorSets(qvk.device, LENGTH(output_img_write), output_img_write, 0, NULL);
	}

	if (qvk.extent_unscaled.width * qvk.extent_unscaled.height > 0)
	{
		create_readback_image(&qvk.screenshot_image, &qvk.screenshot_image_memory, &qvk.screenshot_image_memory_size, qvk.surf_format.format, qvk.extent_unscaled.width, qvk.extent_unscaled.height);
    }
#ifdef VKPT_IMAGE_DUMPS
	create_readback_image(&qvk.dump_image, &qvk.dump_image_memory, &qvk.dump_image_memory_size, VK_FORMAT_R16G16B16A16_SFLOAT, qvk.extent_screen_images.width, qvk.extent_screen_images.height);
#endif

	VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

	VkImageSubresourceRange subresource_range = {
		.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel   = 0,
		.levelCount     = 1,
		.baseArrayLayer = 0,
		.layerCount     = 1
	};

	for (int i = 0; i < NUM_IMAGES; i++) {
		IMAGE_BARRIER(cmd_buf,
			.image = qvk.images[i],
			.subresourceRange = subresource_range,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		);

#ifdef VKPT_DEVICE_GROUPS
		if (qvk.device_count > 1) {
			for(int d = 0; d < qvk.device_count; d++) 
			{
				set_current_gpu(cmd_buf, d);

				IMAGE_BARRIER(cmd_buf,
					.image = qvk.images_local[i][d],
					.subresourceRange = subresource_range,
					.srcAccessMask = 0,
					.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					.newLayout = VK_IMAGE_LAYOUT_GENERAL
				);
			}

			set_current_gpu(cmd_buf, ALL_GPUS);
		}
#endif

	}

	IMAGE_BARRIER_STAGES(cmd_buf,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_HOST_BIT,
		.image = qvk.screenshot_image,
		.subresourceRange = subresource_range,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_HOST_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		);

#ifdef VKPT_IMAGE_DUMPS
	IMAGE_BARRIER_STAGES(cmd_buf,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_HOST_BIT,
		.image = qvk.dump_image,
		.subresourceRange = subresource_range,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_HOST_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
	);
#endif
	
	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, true);

	vkQueueWaitIdle(qvk.queue_graphics);

	return VK_SUCCESS;
}

static VkResult lazy_image_create(vkpt_lazy_image_t *lazy_image, int w, int h, VkFormat format, const char *descr)
{
	VkImageCreateInfo image_create_info = IMAGE_CREATE_INFO(format, w, h);

#ifdef VKPT_DEVICE_GROUPS
	if (qvk.device_count > 1)
		image_create_info.flags |= VK_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT;
#endif

	VkDeviceSize image_size = 0;
	VkResult create_result = create_image(&image_create_info, &lazy_image->image, &lazy_image->image_mem, lazy_image->image_local, &image_size);
	if (create_result != VK_SUCCESS)
	{
		Com_Printf("Memory allocation error. Failed chunk = %.2f MB\n", (float)image_size / megabyte);
		Com_Error(ERR_FATAL, "Failed to allocate GPU memory for screen-space textures!\n");
		return create_result;
	}

	Com_DPrintf("Allocated memory for image '%s': %.2f MB\n", descr, (float)image_size / megabyte);

	/* attach label to image */
	ATTACH_LABEL_VARIABLE_NAME(lazy_image->image, IMAGE, descr);

	VkImageViewCreateInfo image_view_create_info = IMAGEVIEW_CREATE_INFO(lazy_image->image, format);

	_VK(vkCreateImageView(qvk.device, &image_view_create_info, NULL, &lazy_image->image_view));

#ifdef VKPT_DEVICE_GROUPS
	if (qvk.device_count > 1) {
		for(int d = 0; d < qvk.device_count; d++) {
			image_view_create_info.image = lazy_image->image_local[d];
			_VK(vkCreateImageView(qvk.device, &image_view_create_info, NULL, &lazy_image->image_view_local[d]));
		}
	}
#endif

	/* attach label to image view */
	ATTACH_LABEL_VARIABLE_NAME(lazy_image->image_view, IMAGE_VIEW, descr);

	VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

	VkImageSubresourceRange subresource_range = {
		.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel   = 0,
		.levelCount     = 1,
		.baseArrayLayer = 0,
		.layerCount     = 1
	};

	IMAGE_BARRIER(cmd_buf,
		.image = lazy_image->image,
		.subresourceRange = subresource_range,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
	);

#ifdef VKPT_DEVICE_GROUPS
	if (qvk.device_count > 1) {
		for(int d = 0; d < qvk.device_count; d++) {
			set_current_gpu(cmd_buf, d);

			IMAGE_BARRIER(cmd_buf,
				.image = lazy_image->image_local[d],
				.subresourceRange = subresource_range,
				.srcAccessMask = 0,
				.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
				.newLayout = VK_IMAGE_LAYOUT_GENERAL
			);
		}

		set_current_gpu(cmd_buf, ALL_GPUS);
	}
#endif

	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, true);

	vkQueueWaitIdle(qvk.queue_graphics);

	return VK_SUCCESS;
}

VkResult vkpt_prepare_lazy_image(vkpt_lazy_image_t *lazy_image, int w, int h, VkFormat format, const char *descr)
{
	if (!lazy_image->image) {
		VkResult create_res = lazy_image_create(lazy_image, w, h, format, descr);
		if (create_res != VK_SUCCESS)
			return create_res;
	}

	return VK_SUCCESS;
}

#undef IMAGE_CREATE_INFO
#undef IMAGEVIEW_CREATE_INFO
#undef IMAGE_WRITE_DESCRIPTOR_SET
#undef TEXTURE_WRITE_DESCRIPTOR_SET

VkResult
vkpt_destroy_images()
{
	for(int i = 0; i < NUM_VKPT_IMAGES; i++) {
		vkDestroyImageView(qvk.device, qvk.images_views[i], NULL);
		vkDestroyImage    (qvk.device, qvk.images[i],       NULL);

		qvk.images_views[i] = VK_NULL_HANDLE;
		qvk.images[i]       = VK_NULL_HANDLE;

#ifdef VKPT_DEVICE_GROUPS
		if (qvk.device_count > 1) {
			for (int d = 0; d < qvk.device_count; d++)
			{
				vkDestroyImageView(qvk.device, qvk.images_views_local[i][d], NULL);
				vkDestroyImage(qvk.device, qvk.images_local[i][d], NULL);
				qvk.images_views_local[i][d] = VK_NULL_HANDLE;
				qvk.images_local[i][d] = VK_NULL_HANDLE;
			}
		}
#endif

		if (mem_images[i])
		{
			vkFreeMemory(qvk.device, mem_images[i], NULL);
			mem_images[i] = VK_NULL_HANDLE;
		}
	}

	destroy_readback_image(&qvk.screenshot_image, &qvk.screenshot_image_memory, &qvk.screenshot_image_memory_size);
#ifdef VKPT_IMAGE_DUMPS	
	destroy_readback_image(&qvk.dump_image, &qvk.dump_image_memory, &qvk.dump_image_memory_size);
#endif

	return VK_SUCCESS;
}

VkResult vkpt_destroy_lazy_image(vkpt_lazy_image_t *lazy_image)
{
	vkDestroyImageView(qvk.device, lazy_image->image_view, NULL);
	vkDestroyImage    (qvk.device, lazy_image->image,      NULL);

	lazy_image->image_view = VK_NULL_HANDLE;
	lazy_image->image      = VK_NULL_HANDLE;

#ifdef VKPT_DEVICE_GROUPS
	if (qvk.device_count > 1) {
		for (int d = 0; d < qvk.device_count; d++)
		{
			vkDestroyImageView(qvk.device, lazy_image->image_view_local[d], NULL);
			vkDestroyImage(qvk.device, lazy_image->image_local[d], NULL);
			lazy_image->image_view_local[d] = VK_NULL_HANDLE;
			lazy_image->image_local[d] = VK_NULL_HANDLE;
		}
	}
#endif

	if (lazy_image->image_mem)
	{
		vkFreeMemory(qvk.device, lazy_image->image_mem, NULL);
		lazy_image->image_mem = VK_NULL_HANDLE;
	}

	return VK_SUCCESS;
}

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
