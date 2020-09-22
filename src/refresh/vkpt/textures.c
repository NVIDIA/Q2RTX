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

#include "../stb/stb_image.h"
#include "../stb/stb_image_resize.h"
#include "../stb/stb_image_write.h"

#define MAX_RBUFFERS 16

typedef struct UnusedResources
{
	VkImage         images[MAX_RIMAGES];
	VkImageView     image_views[MAX_RIMAGES];
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

static VkImage          tex_images     [MAX_RIMAGES] = { 0 }; // todo: rename to make consistent
static VkImageView      tex_image_views[MAX_RIMAGES] = { 0 }; // todo: rename to make consistent
static VkDeviceMemory   mem_blue_noise, mem_envmap; // todo: rename to make consistent
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

static int image_loading_dirty_flag = 0;
static uint8_t descriptor_set_dirty_flags[MAX_FRAMES_IN_FLIGHT] = { 0 }; // initialized in vkpt_textures_initialize

void vkpt_textures_prefetch()
{
    byte* buffer = NULL;
    ssize_t buffer_size = 0;
    char const * filename = "prefetch.txt";
    buffer_size = FS_LoadFile(filename, (void**)&buffer);
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

		image_t const * img1 = IMG_Find(line, IT_SKIN, IF_PERMANENT | IF_SRGB);

		char other_name[MAX_QPATH];

		// attempt loading a matching normal map
		if (!Q_strlcpy(other_name, line, strlen(line) - 3))
			continue;
		Q_concat(other_name, sizeof(other_name), other_name, "_n.tga", NULL);
		FS_NormalizePath(other_name, other_name);
		image_t const * img2 = IMG_Find(other_name, IT_SKIN, IF_PERMANENT);
		/* if (img2 != R_NOTEXTURE)
			Com_Printf("Prefetched '%s' (%d)\n", other_name, (int)(img2 - r_images)); */

		// attempt loading a matching emissive map
		if (!Q_strlcpy(other_name, line, strlen(line) - 3))
			continue;
		Q_concat(other_name, sizeof(other_name), other_name, "_light.tga", NULL);
		FS_NormalizePath(other_name, other_name);
		image_t const * img3 = IMG_Find(other_name, IT_SKIN, IF_PERMANENT | IF_SRGB);
	}
    // Com_Printf("Loaded '%s'\n", filename);
    FS_FreeFile(buffer);
}

static void textures_destroy_unused_set(uint32_t set_index)
{
	UnusedResources* unused_resources = texture_system.unused_resources + set_index;

	for (uint32_t i = 0; i < unused_resources->image_num; i++)
	{
		if (unused_resources->image_views[i] != VK_NULL_HANDLE)
			vkDestroyImageView(qvk.device, unused_resources->image_views[i], NULL);

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

VkResult
vkpt_textures_upload_envmap(int w, int h, byte *data)
{
	vkDeviceWaitIdle(qvk.device);
	if(imv_envmap != VK_NULL_HANDLE) {
		vkDestroyImageView(qvk.device, imv_envmap, NULL);
		imv_envmap = NULL;
	}
	if(img_envmap != VK_NULL_HANDLE) {
		vkDestroyImage(qvk.device, img_envmap, NULL);
		img_envmap = NULL;
	}

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

	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, qtrue);

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
load_blue_noise()
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
		ssize_t filelen = FS_LoadFile(buf, &filedata);

		if (filedata) {
			data = stbi_load_16_from_memory(filedata, filelen, &w, &h, &n, 4);
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

	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, qtrue);
	
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
	return 1 + log2(MAX(w, h));
}


/*
================
IMG_Load
================
*/

static inline float decode_srgb(byte pix)
{
	float x = (float)pix / 255.f;
	
	if (x < 0.04045f)
		return x / 12.92f;

	return powf((x + 0.055f) / 1.055f, 2.4f);
}

static inline byte encode_srgb(float x)
{
    if (x <= 0.0031308f)
        x *= 12.92f;
    else
        x = 1.055f * powf(x, 1.f / 2.4f) - 0.055f;
     
    x = max(0.f, min(1.f, x));

    return (byte)roundf(x * 255.f);
}

static inline float decode_linear(byte pix)
{
    return (float)pix / 255.f;
}

static inline byte encode_linear(float x)
{
    x = max(0.f, min(1.f, x));

    return (byte)roundf(x * 255.f);
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

	image->processing_complete = qtrue;
}

void
vkpt_normalize_normal_map(image_t *image)
{
    int w = image->upload_width;
    int h = image->upload_height;

    byte* current_pixel = image->pix_data;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) 
        {
            vec3_t color;
            color[0] = decode_linear(current_pixel[0]);
            color[1] = decode_linear(current_pixel[1]);
            color[2] = decode_linear(current_pixel[2]);

            color[0] = color[0] * 2.f - 1.f;
            color[1] = color[1] * 2.f - 1.f;

            if (VectorNormalize(color) == 0.f)
            {
                color[0] = 0.f;
                color[1] = 0.f;
                color[2] = 1.f;
            }

            color[0] = color[0] * 0.5f + 0.5f;
            color[1] = color[1] * 0.5f + 0.5f;
            
            current_pixel[0] = encode_linear(color[0]);
            current_pixel[1] = encode_linear(color[1]);
            current_pixel[2] = encode_linear(color[2]);

            current_pixel += 4;
        }
    }

    image->processing_complete = qtrue;
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
	const uint32_t frame_index = (qvk.frame_counter + MAX_FRAMES_IN_FLIGHT) % DESTROY_LATENCY;
	UnusedResources* unused_resources = texture_system.unused_resources + frame_index;

	const uint32_t unused_index = unused_resources->image_num++;

	unused_resources->images[unused_index] = tex_images[index];
	unused_resources->image_memory[unused_index] = tex_image_memory[index];
	unused_resources->image_views[unused_index] = tex_image_views[index];

	tex_images[index] = VK_NULL_HANDLE;
	tex_image_views[index] = VK_NULL_HANDLE;

	memset(descriptor_set_dirty_flags, 0xff, sizeof(descriptor_set_dirty_flags));
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
            image->processing_complete = qfalse;

            IMG_Load(image, new_image.pix_data);

            if (strstr(filepath, "_n."))
            {
                vkpt_normalize_normal_map(image);
            }

            image->last_modified = last_modifed; // reset time stamp because load_img doesn't

            // destroy Vk ressources to force vkpt_textures_end_registration
            // to recreate them next time a frame is drawn
            if (tex_image_views[i]) {
                vkDestroyImageView(qvk.device, tex_image_views[i], NULL);
                tex_image_views[i] = VK_NULL_HANDLE;
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

void create_invalid_texture()
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
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	);

	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, qtrue);

	vkQueueWaitIdle(qvk.queue_graphics);
}

void destroy_invalid_texture()
{
	vkDestroyImage(qvk.device, tex_invalid_texture_image, NULL);
	vkDestroyImageView(qvk.device, tex_invalid_texture_image_view, NULL);
	free_device_memory(tex_device_memory_allocator, &tex_invalid_texture_image_memory);
}

VkResult
vkpt_textures_initialize()
{
	memset(descriptor_set_dirty_flags, 0xff, sizeof(descriptor_set_dirty_flags));
	memset(&texture_system, 0, sizeof(texture_system));

	tex_device_memory_allocator = create_device_memory_allocator(qvk.device);

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
	VkDescriptorPoolSize pool_size = {
		.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = MAX_RIMAGES + 2 * NUM_VKPT_IMAGES + 128,
	};

	VkDescriptorPoolCreateInfo pool_info = {
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = 1,
		.pPoolSizes    = &pool_size,
		.maxSets       = 2,
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

	if(load_blue_noise() != VK_SUCCESS)
		return VK_ERROR_INITIALIZATION_FAILED;

	LOG_FUNC();
	return VK_SUCCESS;
}

static void
destroy_tex_images()
{
	for(int i = 0; i < MAX_RIMAGES; i++) {
		if(tex_image_views[i]) {
			vkDestroyImageView(qvk.device, tex_image_views[i], NULL);
			tex_image_views[i] = VK_NULL_HANDLE;
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
	vkDestroySampler  (qvk.device, qvk.tex_sampler,         NULL);
	vkDestroySampler  (qvk.device, qvk.tex_sampler_nearest, NULL);
	vkDestroySampler  (qvk.device, qvk.tex_sampler_linear_clamp, NULL);

	if(imv_envmap != VK_NULL_HANDLE) {
		vkDestroyImageView(qvk.device, imv_envmap, NULL);
		imv_envmap = NULL;
	}
	if(img_envmap != VK_NULL_HANDLE) {
		vkDestroyImage(qvk.device, img_envmap, NULL);
		img_envmap = NULL;
	}
	if (mem_envmap != VK_NULL_HANDLE) {
		vkFreeMemory(qvk.device, mem_envmap, NULL);
		mem_envmap = VK_NULL_HANDLE;
	}

	destroy_invalid_texture();
	destroy_device_memory_allocator(tex_device_memory_allocator);
	tex_device_memory_allocator = NULL;

	LOG_FUNC();
	return VK_SUCCESS;
}

#ifdef VKPT_DEVICE_GROUPS
static VkMemoryAllocateFlagsInfoKHR mem_alloc_flags_broadcast = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR,
		.flags = VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT_KHR,
};
#endif

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

	uint32_t new_image_num = 0;
	size_t   total_size = 0;
	for(int i = 0; i < MAX_RIMAGES; i++) {
		image_t *q_img = r_images + i;

		if (tex_images[i] != VK_NULL_HANDLE || !q_img->registration_sequence || q_img->pix_data == NULL)
			continue;

		img_info.extent.width = q_img->upload_width;
		img_info.extent.height = q_img->upload_height;
		img_info.mipLevels = get_num_miplevels(q_img->upload_width, q_img->upload_height);
		img_info.format = q_img->is_srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

		_VK(vkCreateImage(qvk.device, &img_info, NULL, tex_images + i));
		ATTACH_LABEL_VARIABLE(tex_images[i], IMAGE);

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

		allocate_device_memory(tex_device_memory_allocator, image_memory);

		VkBindImageMemoryInfo* bind_info = tex_bind_image_info + new_image_num;
		bind_info->sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
		bind_info->image = tex_images[i];
		bind_info->memory = image_memory->memory;
		bind_info->memoryOffset = image_memory->memory_offset;

		new_image_num++;
	}

	if (new_image_num == 0)
		return VK_SUCCESS;
	vkBindImageMemory2(qvk.device, new_image_num, tex_bind_image_info);

	BufferResource_t buf_img_upload;
	buffer_create(&buf_img_upload, total_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	
	VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

	char *staging_buffer = buffer_map(&buf_img_upload);

	size_t offset = 0;
	for(int i = 0; i < MAX_RIMAGES; i++) {
		image_t *q_img = r_images + i;

		if (tex_images[i] == VK_NULL_HANDLE || tex_image_views[i] != VK_NULL_HANDLE)
			continue;

		int num_mip_levels = get_num_miplevels(q_img->upload_width, q_img->upload_height);

		VkMemoryRequirements mem_req;
		vkGetImageMemoryRequirements(qvk.device, tex_images[i], &mem_req);

		assert(!(mem_req.alignment & (mem_req.alignment - 1)));
		offset += mem_req.alignment - 1;
		offset &= ~(mem_req.alignment - 1);

		uint32_t wd = q_img->upload_width;
		uint32_t ht = q_img->upload_height;

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

		{
			memcpy(staging_buffer + offset, q_img->pix_data, wd * ht * 4);

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
		}

		subresource_range.levelCount = 1;

		for (int mip = 1; mip < num_mip_levels; mip++) 
		{
			subresource_range.baseMipLevel = mip - 1;

			IMAGE_BARRIER(cmd_buf,
				.image = tex_images[i],
				.subresourceRange = subresource_range,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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

			subresource_range.baseMipLevel = mip - 1;

			IMAGE_BARRIER(cmd_buf,
				.image = tex_images[i],
				.subresourceRange = subresource_range,
				.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
				.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
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
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			);

		img_view_info.image = tex_images[i];
		img_view_info.subresourceRange.levelCount = num_mip_levels;
		img_view_info.format = q_img->is_srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
		_VK(vkCreateImageView(qvk.device, &img_view_info, NULL, tex_image_views + i));
		ATTACH_LABEL_VARIABLE(tex_image_views[i], IMAGE_VIEW);

		offset += mem_req.size;
	}

	buffer_unmap(&buf_img_upload);
	staging_buffer = NULL; 

	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, qtrue);
	

	const uint32_t destroy_frame_index = (qvk.frame_counter + MAX_FRAMES_IN_FLIGHT) % DESTROY_LATENCY;
	UnusedResources* unused_resources = texture_system.unused_resources + destroy_frame_index;

	const uint32_t unused_index = unused_resources->buffer_num++;
	unused_resources->buffers[unused_index] = buf_img_upload.buffer;
	unused_resources->buffer_memory[unused_index] = buf_img_upload.memory;

	memset(descriptor_set_dirty_flags, 0xff, sizeof(descriptor_set_dirty_flags));

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
		if (!strcmp(q_img->name, "pics/conchars.pcx") || !strcmp(q_img->name, "pics/ch1.pcx"))
			sampler = qvk.tex_sampler_nearest;
		else if (q_img->type == IT_SPRITE)
			sampler = qvk.tex_sampler_linear_clamp;

		VkDescriptorImageInfo img_info = {
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.imageView   = image_view,
			.sampler     = sampler,
		};

		if (i >= VKPT_IMG_BLOOM_HBLUR &&
			i <= VKPT_IMG_BLOOM_VBLUR) {
			img_info.sampler = qvk.tex_sampler_linear_clamp;
		}

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
									VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
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

VkResult
vkpt_create_images()
{
	VkImageCreateInfo images_create_info[NUM_VKPT_IMAGES] = {
#define IMG_DO(_name, _binding, _vkformat, _glslformat, _w, _h) \
	[VKPT_IMG_##_name] = { \
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, \
		.imageType = VK_IMAGE_TYPE_2D, \
		.format = VK_FORMAT_##_vkformat, \
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
	},
LIST_IMAGES
LIST_IMAGES_A_B
#undef IMG_DO
	};

#ifdef VKPT_DEVICE_GROUPS
#define IMG_DO(_name, _binding, _vkformat, _glslformat, _w, _h) \
	images_create_info[VKPT_IMG_##_name].flags |= \
		VK_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT;
LIST_IMAGES
LIST_IMAGES_A_B
#undef IMG_DO
#endif

	size_t total_size = 0;

	for(int i = 0; i < NUM_VKPT_IMAGES; i++)
	{
		_VK(vkCreateImage(qvk.device, images_create_info + i, NULL, qvk.images + i));
		ATTACH_LABEL_VARIABLE(qvk.images[i], IMAGE);

		VkMemoryRequirements mem_req;
		vkGetImageMemoryRequirements(qvk.device, qvk.images[i], &mem_req);

		total_size += align(mem_req.size, mem_req.alignment);

		_VK(allocate_gpu_memory(mem_req, &mem_images[i]));

		ATTACH_LABEL_VARIABLE(mem_images[i], DEVICE_MEMORY);

		_VK(vkBindImageMemory(qvk.device, qvk.images[i], mem_images[i], 0));

#ifdef VKPT_DEVICE_GROUPS
		if (qvk.device_count > 1) {
			// create per-device local image bindings so we can copy back and forth

			// create copies of the same image object that will receive full per-GPU mappings
			for(int d = 0; d < qvk.device_count; d++)
			{
				_VK(vkCreateImage(qvk.device, images_create_info + i, NULL, &qvk.images_local[d][i]));
				ATTACH_LABEL_VARIABLE(qvk.images_local[d][i], IMAGE);

				uint32_t device_indices[VKPT_MAX_GPUS];

				for (int j = 0; j < VKPT_MAX_GPUS; j++)
				{
					// all GPUs attach to memory on one device for this image object
					device_indices[j] = (uint32_t)d;
				}

				// shut up lunarg
				{
					VkMemoryRequirements mem_req;
					vkGetImageMemoryRequirements(qvk.device, qvk.images_local[d][i], &mem_req);
				}

				VkBindImageMemoryDeviceGroupInfoKHR device_group_info = {
					.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_DEVICE_GROUP_INFO_KHR,
					.pNext = NULL,
					.deviceIndexCount = qvk.device_count,
					.pDeviceIndices = device_indices,
					.splitInstanceBindRegionCount = 0,
					.pSplitInstanceBindRegions = NULL,
				};

				VkBindImageMemoryInfoKHR bind_info = {
					.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO_KHR,
					.pNext = &device_group_info,
					.image = qvk.images_local[d][i],
					.memory = mem_images[i],
					.memoryOffset = 0,
				};

				_VK(qvkBindImageMemory2KHR(qvk.device, 1, &bind_info));
			}
		}
#endif
	}

	Com_Printf("Screen-space image memory: %.2f MB\n", (float)total_size / 1048576.f);

	/* attach labels to images */
#define IMG_DO(_name, _binding, ...) \
	ATTACH_LABEL_VARIABLE_NAME(qvk.images[VKPT_IMG_##_name], IMAGE, #_name);
	LIST_IMAGES
	LIST_IMAGES_A_B
#undef IMG_DO
	/* attach labels to images */
#define IMG_DO(_name, _binding, ...) \
	ATTACH_LABEL_VARIABLE_NAME(qvk.images[VKPT_IMG_##_name], IMAGE, #_name);
	LIST_IMAGES
	LIST_IMAGES_A_B
#undef IMG_DO


#define IMG_DO(_name, _binding, _vkformat, _glslformat, _w, _h) \
	[VKPT_IMG_##_name] = { \
		.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, \
		.viewType   = VK_IMAGE_VIEW_TYPE_2D, \
		.format     = VK_FORMAT_##_vkformat, \
		.image      = qvk.images[VKPT_IMG_##_name], \
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
	},

	VkImageViewCreateInfo images_view_create_info[NUM_VKPT_IMAGES] = {
		LIST_IMAGES
		LIST_IMAGES_A_B
	};
#undef IMG_DO


	for(int i = 0; i < NUM_VKPT_IMAGES; i++) {
		_VK(vkCreateImageView(qvk.device, images_view_create_info + i, NULL, qvk.images_views + i));

#ifdef VKPT_DEVICE_GROUPS
		if (qvk.device_count > 1) {
			for(int d = 0; d < qvk.device_count; d++) {
				VkImageViewCreateInfo info = images_view_create_info[i];
				info.image = qvk.images_local[d][i];
				_VK(vkCreateImageView(qvk.device, &info, NULL, &qvk.images_views_local[d][i]));
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
			.sampler     = qvk.tex_sampler, \
		},

		LIST_IMAGES
		LIST_IMAGES_A_B
	};
#undef IMG_DO

	for(int i = VKPT_IMG_BLOOM_HBLUR; i <= VKPT_IMG_BLOOM_VBLUR; i++) {
		img_info[i].sampler = qvk.tex_sampler_linear_clamp;
	}

	VkWriteDescriptorSet output_img_write[NUM_IMAGES * 2];

	for (int even_odd = 0; even_odd < 2; even_odd++)
	{
		/* create information to update descriptor sets */
#define IMG_DO(_name, _binding, ...) { \
			VkWriteDescriptorSet elem_image = { \
				.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, \
				.dstSet          = even_odd ? qvk.desc_set_textures_odd : qvk.desc_set_textures_even, \
				.dstBinding      = BINDING_OFFSET_IMAGES + _binding, \
				.dstArrayElement = 0, \
				.descriptorCount = 1, \
				.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, \
				.pImageInfo      = desc_output_img_info + VKPT_IMG_##_name, \
			}; \
			VkWriteDescriptorSet elem_texture = { \
				.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, \
				.dstSet          = even_odd ? qvk.desc_set_textures_odd : qvk.desc_set_textures_even, \
				.dstBinding      = BINDING_OFFSET_TEXTURES + _binding, \
				.dstArrayElement = 0, \
				.descriptorCount = 1, \
				.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, \
				.pImageInfo      = img_info + VKPT_IMG_##_name, \
			}; \
			output_img_write[VKPT_IMG_##_name] = elem_image; \
			output_img_write[VKPT_IMG_##_name + NUM_VKPT_IMAGES] = elem_texture; \
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

	create_readback_image(&qvk.screenshot_image, &qvk.screenshot_image_memory, &qvk.screenshot_image_memory_size, qvk.surf_format.format, qvk.extent_unscaled.width, qvk.extent_unscaled.height);
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

	for (int i = 0; i < NUM_VKPT_IMAGES; i++) {
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
					.image = qvk.images_local[d][i],
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

	IMAGE_BARRIER(cmd_buf,
		.image = qvk.screenshot_image,
		.subresourceRange = subresource_range,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_HOST_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		);

#ifdef VKPT_IMAGE_DUMPS
	IMAGE_BARRIER(cmd_buf,
		.image = qvk.dump_image,
		.subresourceRange = subresource_range,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_HOST_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
	);
#endif
	
	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, qtrue);

	vkQueueWaitIdle(qvk.queue_graphics);

	return VK_SUCCESS;
}

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
				vkDestroyImageView(qvk.device, qvk.images_views_local[d][i], NULL);
				vkDestroyImage(qvk.device, qvk.images_local[d][i], NULL);
				qvk.images_views_local[d][i] = VK_NULL_HANDLE;
				qvk.images_local[d][i] = VK_NULL_HANDLE;
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

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
