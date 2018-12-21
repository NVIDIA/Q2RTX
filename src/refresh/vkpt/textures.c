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

#include "vkpt.h"
#include "vk_util.h"

#include <assert.h>

#include "include/stb_image.h"
#include "include/stb_image_resize.h"
#include "include/stb_image_write.h"

static VkSampler        tex_sampler, tex_sampler_nearest; // todo: rename to make consistent

static VkImage          tex_images     [MAX_RIMAGES]; // todo: rename to make consistent
static VkImageView      tex_image_views[MAX_RIMAGES]; // todo: rename to make consistent
static VkDeviceMemory   img_memory, mem_blue_noise, mem_envmap; // todo: rename to make consistent
static VkImage          img_blue_noise;
static VkImageView      imv_blue_noise;
static VkImage          img_envmap;
static VkImageView      imv_envmap;
static VkDescriptorPool desc_pool_textures;

static VkDeviceMemory mem_images;


static int image_loading_dirty_flag = 0;

static void
load_material(int material_idx, const image_t *image)
{
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

	VkMemoryAllocateInfo mem_alloc_info = {
		.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize  = mem_req.size,
		.memoryTypeIndex = get_memory_type(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};
	_VK(vkAllocateMemory(qvk.device, &mem_alloc_info, NULL, &mem_envmap));

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

	VkCommandBufferAllocateInfo cmd_alloc = {
		.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandPool        = qvk.command_pool,
		.commandBufferCount = 1,
	};
	VkCommandBuffer cmd_buf;
	vkAllocateCommandBuffers(qvk.device, &cmd_alloc, &cmd_buf);
	VkCommandBufferBeginInfo cmd_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd_buf, &cmd_begin_info);

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

	vkEndCommandBuffer(cmd_buf);
	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd_buf,
	};
	vkQueueSubmit(qvk.queue_graphics, 1, &submit_info, VK_NULL_HANDLE);


	VkDescriptorImageInfo desc_img_info = {
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.imageView   = imv_envmap,
		.sampler     = tex_sampler,
	};

	VkWriteDescriptorSet s = {
		.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet          = qvk.desc_set_textures,
		.dstBinding      = BINDING_OFFSET_ENVMAP,
		.dstArrayElement = 0,
		.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.pImageInfo      = &desc_img_info,
	};

	vkUpdateDescriptorSets(qvk.device, 1, &s, 0, NULL);

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


		snprintf(buf, sizeof buf, "blue_noise_textures/%d_%d/HDR_RGBA_%04d.png", res, res, i);
		uint16_t *data = stbi_load_16(buf, &w, &h, &n, 4);
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

	VkMemoryAllocateInfo mem_alloc_info = {
		.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize  = mem_req.size,
		.memoryTypeIndex = get_memory_type(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};
	_VK(vkAllocateMemory(qvk.device, &mem_alloc_info, NULL, &mem_blue_noise));

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

	VkCommandBufferAllocateInfo cmd_alloc = {
		.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandPool        = qvk.command_pool,
		.commandBufferCount = 1,
	};
	VkCommandBuffer cmd_buf;
	vkAllocateCommandBuffers(qvk.device, &cmd_alloc, &cmd_buf);
	VkCommandBufferBeginInfo cmd_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd_buf, &cmd_begin_info);

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

	vkEndCommandBuffer(cmd_buf);
	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd_buf,
	};
	vkQueueSubmit(qvk.queue_graphics, 1, &submit_info, VK_NULL_HANDLE);


	VkDescriptorImageInfo desc_img_info = {
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.imageView   = imv_blue_noise,
		.sampler     = tex_sampler_nearest,
	};

	VkWriteDescriptorSet s = {
		.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet          = qvk.desc_set_textures,
		.dstBinding      = BINDING_OFFSET_BLUE_NOISE,
		.dstArrayElement = 0,
		.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.pImageInfo      = &desc_img_info,
	};

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

/* we match on radiance in the bsp, unfortunately this
   does not catch all lights :( we therefore have this
   explicit lists of light textures here */
static const char *light_texture_names[] = {
	"textures/e1u1/baselt_3.tga",
	"textures/e1u1/baslt3_1.tga",
	"textures/e1u1/baselt_2.tga",
	"textures/e1u1/baselt_1.tga",
	"textures/e1u1/baselt_b.tga",
	"textures/e1u1/baselt_c.tga",
	"textures/e1u1/baselt_a.tga",
	"textures/e1u1/ceil1_1.tga",
	"textures/e1u1/ceil1_2.tga",
	"textures/e1u1/ceil1_3.tga",
	"textures/e1u1/ceil1_4.tga",
	"textures/e1u1/ceil1_5.tga",
	"textures/e1u1/ceil1_6.tga",
	"textures/e1u1/ceil1_7.tga",
	"textures/e1u1/ceil1_8.tga",
	"textures/e1u1/jaildr2_3.tga",
	"textures/e1u1/plite1_1.tga",
	"textures/e1u2/ceil1_1.tga",
	"textures/e1u2/ceil1_3.tga",
	"textures/e1u2/ceil1_8.tga",
	"textures/e1u2/fuse1_1.tga",
	"textures/e1u2/fuse1_2.tga",
	"textures/e1u2/support1_8.tga",
	"textures/e1u2/wslt1_5.tga",
	"textures/e2u3/ceil1_13.tga",
	"textures/e2u3/ceil1_14.tga",
	"textures/e2u3/ceil1_4.tga",
	"textures/e2u3/wstlt1_5.tga",
	"textures/e2u3/wstlt1_8.tga",
	"textures/e1u2/brlava.tga",
	"textures/e3u1/brlava.tga",
	"textures/e1u1/brlava.tga",
	"textures/e1u2/brlava.tga",
	"textures/e1u3/lava.tga",
	"textures/e2u1/brlava.tga",
	"textures/e2u1/lava.tga",
	"textures/e2u2/brlava.tga",
	"textures/e2u2/lava.tga",
	"textures/e2u3/brlava.tga",
	"textures/e2u3/tlava1_3.tga",
	"textures/e3u1/brlava.tga",
};

void
IMG_Load(image_t *image, byte *pic)
{
	//Com_Printf("%s %s %s\n", __func__, image->flags & IF_PERMANENT ? "(permanent) " : "", image->name);
	int w = image->upload_width;
	int h = image->upload_height;

	image->is_light = 0;
	for(int i = 0; i < LENGTH(light_texture_names); i++) {
		if(!strncmp(image->name, light_texture_names[i], strlen(light_texture_names[i]) - 4)) {
			image->is_light = 1;
			break;
		}
	}

	//int num_mip_levels = log2(MAX(w, h));
	image->pix_data = Z_Malloc(w * h * 4 * 2);
	int w_mip = w, h_mip = h;
	byte *mip_off = image->pix_data;
	memcpy(mip_off, pic, w_mip * h_mip * 4);
	int level = 0;
	while(w_mip > 1 || h_mip > 1) {
#if 0
		char buf[1024];
		snprintf(buf, sizeof buf, "/tmp/%s_%02d.png", image->name, level++);
		for(int i = 5; buf[i]; i++) {
			if(buf[i] == '/')
				buf[i] = '_';
		}

     	stbi_write_png(buf, w_mip, h_mip, 4, mip_off, w_mip * 4);
#endif

		int w_mip_next = w_mip >> (w_mip > 1);
		int h_mip_next = h_mip >> (h_mip > 1);
		stbir_resize_uint8_generic(
				//image->pix_data, w, h, w * 4,
				mip_off, w_mip, h_mip, w_mip * 4, 
				mip_off + w_mip * h_mip * 4, w_mip_next, h_mip_next, w_mip_next * 4,
				4, 3, 0, STBIR_EDGE_WRAP, STBIR_FILTER_TRIANGLE, STBIR_COLORSPACE_SRGB, NULL);
				//4, 3, 0, STBIR_EDGE_WRAP, STBIR_FILTER_MITCHELL, STBIR_COLORSPACE_SRGB, NULL);

		mip_off += w_mip * h_mip * 4;
		w_mip = w_mip_next;
		h_mip = h_mip_next;
	}


	//image->pix_data = pic;

	float r = (float) pic[((h / 2) * w + w/ 2) * 4 + 0];
	float g = (float) pic[((h / 2) * w + w/ 2) * 4 + 1];
	float b = (float) pic[((h / 2) * w + w/ 2) * 4 + 2];

	r = powf(r / 255.0f, 2.2f);
	g = powf(g / 255.0f, 2.2f);
	b = powf(b / 255.0f, 2.2f);

	float m = r > g ? r : g;
	m = g > b ? g : b;

	m = 1.0;

	r = (r / m) * 255.0f;
	g = (g / m) * 255.0f;
	b = (b / m) * 255.0f;

	r = r > 255.0 ? 255.0 : r;
	g = g > 255.0 ? 255.0 : g;
	b = b > 255.0 ? 255.0 : b;

	image->light_color  = 0;
	image->light_color |= ((uint32_t) r) <<  0;
	image->light_color |= ((uint32_t) g) <<  8;
	image->light_color |= ((uint32_t) b) << 16;

	image->material_idx = (int) (image - r_images);
	if(image->material_idx >= 0) {
		load_material(image->material_idx, image);
	}

	image_loading_dirty_flag = 1;
	Z_Free(pic);
}

void
IMG_Unload(image_t *image)
{
	if(image->pix_data)
		Z_Free(image->pix_data);
	image->pix_data = NULL;
}

VkResult
vkpt_textures_initialize()
{
	VkSamplerCreateInfo sampler_info = {
		.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter               = VK_FILTER_LINEAR,
		.minFilter               = VK_FILTER_LINEAR,
		.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.anisotropyEnable        = VK_FALSE,
		.maxAnisotropy           = 16,
		.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
		.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.minLod                  = 0.0f,
		.maxLod                  = 128.0f,
	};
	_VK(vkCreateSampler(qvk.device, &sampler_info, NULL, &tex_sampler));
	ATTACH_LABEL_VARIABLE(tex_sampler, SAMPLER);
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
	_VK(vkCreateSampler(qvk.device, &sampler_nearest_info, NULL, &tex_sampler_nearest));
	ATTACH_LABEL_VARIABLE(tex_sampler_nearest, SAMPLER);

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
#undef IMG_DO
#define IMG_DO(_name, _binding, ...) \
		{ \
			.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, \
			.descriptorCount = 1, \
			.binding         = BINDING_OFFSET_TEXTURES + _binding, \
			.stageFlags      = VK_SHADER_STAGE_ALL, \
		},
	LIST_IMAGES
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
		.maxSets       = 1,
	};

	_VK(vkCreateDescriptorPool(qvk.device, &pool_info, NULL, &desc_pool_textures));
	ATTACH_LABEL_VARIABLE(desc_pool_textures, DESCRIPTOR_POOL);

	VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {
		.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool     = desc_pool_textures,
		.descriptorSetCount = 1,
		.pSetLayouts        = &qvk.desc_set_layout_textures,
	};

	_VK(vkAllocateDescriptorSets(qvk.device, &descriptor_set_alloc_info, &qvk.desc_set_textures));

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
		}
	}
	if(img_memory) {
		vkFreeMemory(qvk.device, img_memory, NULL);
		img_memory = VK_NULL_HANDLE;
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
	vkDestroySampler  (qvk.device, tex_sampler,         NULL);
	vkDestroySampler  (qvk.device, tex_sampler_nearest, NULL);

	if(imv_envmap != VK_NULL_HANDLE) {
		vkDestroyImageView(qvk.device, imv_envmap, NULL);
		imv_envmap = NULL;
	}
	if(img_envmap != VK_NULL_HANDLE) {
		vkDestroyImage(qvk.device, img_envmap, NULL);
		img_envmap = NULL;
	}
	LOG_FUNC();
	return VK_SUCCESS;
}

VkResult
vkpt_textures_end_registration()
{
	if(!image_loading_dirty_flag)
		return VK_SUCCESS;
	image_loading_dirty_flag = 0;
	vkDeviceWaitIdle(qvk.device);
	destroy_tex_images();

	VkImageCreateInfo img_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.extent = {
			.width  = 1337,
			.height = 1337,
			.depth  = 1
		},
		.imageType             = VK_IMAGE_TYPE_2D,
		.format                = VK_FORMAT_R8G8B8A8_SRGB,
		.mipLevels             = 1,
		.arrayLayers           = 1,
		.samples               = VK_SAMPLE_COUNT_1_BIT,
		.tiling                = VK_IMAGE_TILING_OPTIMAL,
		.usage                 = VK_IMAGE_USAGE_TRANSFER_DST_BIT
		                       | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = qvk.queue_idx_graphics,
		.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VkImageViewCreateInfo img_view_info = {
		.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.viewType   = VK_IMAGE_VIEW_TYPE_2D,
		.format     = VK_FORMAT_R8G8B8A8_SRGB,
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

	uint32_t pix_invalid = 0xffff00ff;
	image_t q_img_invalid = {
		.width = 1,
		.height = 1,
		.upload_width = 1,
		.upload_height = 1,
		.pix_data = (byte *) &pix_invalid
	};

	size_t   total_size = 0;
	uint32_t memory_type_bits = ~0;
	for(int i = 0; i < MAX_RIMAGES; i++) {
		image_t *q_img = r_images + i;
		if(q_img->registration_sequence) {
			img_info.extent.width  = q_img->upload_width;
			img_info.extent.height = q_img->upload_height;
		}
		else {
			q_img = &q_img_invalid;
		}
		img_info.mipLevels = get_num_miplevels(q_img->upload_width, q_img->upload_height);

		_VK(vkCreateImage(qvk.device, &img_info, NULL, tex_images + i));
		ATTACH_LABEL_VARIABLE(tex_images[i], IMAGE);

		VkMemoryRequirements mem_req;
		vkGetImageMemoryRequirements(qvk.device, tex_images[i], &mem_req);

		assert(!(mem_req.alignment & (mem_req.alignment - 1)));
		total_size += mem_req.alignment - 1;
		total_size &= ~(mem_req.alignment - 1);
		total_size += mem_req.size;

		if(memory_type_bits != ~0 && mem_req.memoryTypeBits != memory_type_bits) {
			Com_EPrintf("memory type bits don't match!\n");
		}
		memory_type_bits = mem_req.memoryTypeBits;
	}

	Com_Printf("allocating %.02f MB of image memory for textures\n", (double) total_size / (1024.0 * 1024.0));
	VkMemoryAllocateInfo mem_alloc_info = {
		.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize  = total_size,
		.memoryTypeIndex = get_memory_type(memory_type_bits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};
	_VK(vkAllocateMemory(qvk.device, &mem_alloc_info, NULL, &img_memory));

	BufferResource_t buf_img_upload;
	buffer_create(&buf_img_upload, total_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	VkCommandBufferAllocateInfo cmd_alloc = {
		.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandPool        = qvk.command_pool,
		.commandBufferCount = 1,
	};

	VkCommandBuffer cmd_buf;
	vkAllocateCommandBuffers(qvk.device, &cmd_alloc, &cmd_buf);
	VkCommandBufferBeginInfo cmd_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(cmd_buf, &cmd_begin_info);
	
	char *staging_buffer = buffer_map(&buf_img_upload);

	size_t offset = 0;
	for(int i = 0; i < MAX_RIMAGES; i++) {
		image_t *q_img = r_images + i;
		if(!q_img->registration_sequence)
			q_img = &q_img_invalid;

		int num_mip_levels = get_num_miplevels(q_img->upload_width, q_img->upload_height);

		VkMemoryRequirements mem_req;
		vkGetImageMemoryRequirements(qvk.device, tex_images[i], &mem_req);

		assert(!(mem_req.alignment & (mem_req.alignment - 1)));
		offset += mem_req.alignment - 1;
		offset &= ~(mem_req.alignment - 1);

		uint32_t wd = q_img->upload_width;
		uint32_t ht = q_img->upload_height;


		_VK(vkBindImageMemory(qvk.device, tex_images[i], img_memory, offset));

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

		size_t mip_offset = 0;
		for(int mip = 0; mip < num_mip_levels; mip++) {
			memcpy(staging_buffer + offset + mip_offset, q_img->pix_data + mip_offset, wd * ht * 4);

			VkBufferImageCopy cpy_info = {
				.bufferOffset = offset + mip_offset,
				.imageSubresource = { 
					.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
					.mipLevel       = mip,
					.baseArrayLayer = 0,
					.layerCount     = 1,
				},
				.imageOffset    = { 0, 0, 0 },
				.imageExtent    = { wd, ht, 1 }
			};

			vkCmdCopyBufferToImage(cmd_buf, buf_img_upload.buffer, tex_images[i],
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cpy_info);

			mip_offset += wd * ht * 4;
			wd >>= (wd > 1);
			ht >>= (ht > 1);
		}

		IMAGE_BARRIER(cmd_buf,
				.image            = tex_images[i],
				.subresourceRange = subresource_range,
				.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT,
				.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		);

		img_view_info.image = tex_images[i];
		img_view_info.subresourceRange.levelCount = num_mip_levels;
		_VK(vkCreateImageView(qvk.device, &img_view_info, NULL, tex_image_views + i));
		ATTACH_LABEL_VARIABLE(tex_image_views[i], IMAGE_VIEW);

		offset += mem_req.size;
	}

	buffer_unmap(&buf_img_upload);
	staging_buffer = NULL; 

	vkEndCommandBuffer(cmd_buf);
	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &cmd_buf,
	};

	vkQueueSubmit(qvk.queue_graphics, 1, &submit_info, VK_NULL_HANDLE);

	for(int i = 0; i < MAX_RIMAGES; i++) {
		image_t *q_img = r_images + i;
		if(!q_img->registration_sequence)
			q_img = &q_img_invalid;

		VkDescriptorImageInfo img_info = {
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.imageView   = tex_image_views[i],
			.sampler     = (!strcmp(q_img->name, "pics/conchars.pcx") || !strcmp(q_img->name, "pics/ch1.pcx"))
			               ? tex_sampler_nearest : tex_sampler,
		};

		VkWriteDescriptorSet s = {
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = qvk.desc_set_textures,
			.dstBinding      = GLOBAL_TEXTURES_TEX_ARR_BINDING_IDX,
			.dstArrayElement = i,
			.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.pImageInfo      = &img_info,
		};

		vkUpdateDescriptorSets(qvk.device, 1, &s, 0, NULL);
	}

	vkQueueWaitIdle(qvk.queue_graphics);
	buffer_destroy(&buf_img_upload);

	return VK_SUCCESS;
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
		                       | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT, \
		.sharingMode           = VK_SHARING_MODE_EXCLUSIVE, \
		.queueFamilyIndexCount = qvk.queue_idx_graphics, \
		.initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED, \
	},
LIST_IMAGES
#undef IMG_DO
	};

	size_t total_size = 0;


	/* create images and compute required memory to make a single allocation */
	uint32_t memory_type_bits;
	for(int i = 0; i < NUM_VKPT_IMAGES; i++) {
		_VK(vkCreateImage(qvk.device, images_create_info + i, NULL, qvk.images + i));
		ATTACH_LABEL_VARIABLE(qvk.images[i], IMAGE);

		VkMemoryRequirements mem_req;
		vkGetImageMemoryRequirements(qvk.device, qvk.images[i], &mem_req);

		assert(!(mem_req.alignment & (mem_req.alignment - 1)));
		total_size += mem_req.alignment - 1;
		total_size &= ~(mem_req.alignment - 1);
		total_size += mem_req.size;

		if(i && mem_req.memoryTypeBits != memory_type_bits) {
			Com_EPrintf("memory type bits don't match!\n");
		}
		memory_type_bits = mem_req.memoryTypeBits;
	}

	/* attach labels to images */
#define IMG_DO(_name, _binding, ...) \
	ATTACH_LABEL_VARIABLE_NAME(qvk.images[VKPT_IMG_##_name], IMAGE, #_name);
	LIST_IMAGES
#undef IMG_DO
	/* attach labels to images */
#define IMG_DO(_name, _binding, ...) \
	ATTACH_LABEL_VARIABLE_NAME(qvk.images[VKPT_IMG_##_name], IMAGE, #_name);
	LIST_IMAGES
#undef IMG_DO

	Com_Printf("allocating %.02f MB of image memory\n", (double) total_size / (1024.0 * 1024.0));

	VkMemoryAllocateInfo mem_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = total_size,
		.memoryTypeIndex = get_memory_type(memory_type_bits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};

	_VK(vkAllocateMemory(qvk.device, &mem_alloc_info, NULL, &mem_images));
	ATTACH_LABEL_VARIABLE(mem_images, DEVICE_MEMORY);

	/* bind memory to images */
	size_t offset = 0;
	for(int i = 0; i < NUM_VKPT_IMAGES; i++) {
		VkMemoryRequirements mem_req;
		vkGetImageMemoryRequirements(qvk.device, qvk.images[i], &mem_req);

		assert(!(mem_req.alignment & (mem_req.alignment - 1)));
		offset += mem_req.alignment - 1;
		offset &= ~(mem_req.alignment - 1);

		_VK(vkBindImageMemory(qvk.device, qvk.images[i], mem_images, offset));

		offset += mem_req.size;
	}

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
	};
#undef IMG_DO


	for(int i = 0; i < NUM_VKPT_IMAGES; i++) {
		_VK(vkCreateImageView(qvk.device, images_view_create_info + i, NULL, qvk.images_views + i));
	}

	/* attach labels to image views */
#define IMG_DO(_name, ...) \
	ATTACH_LABEL_VARIABLE_NAME(qvk.images_views[VKPT_IMG_##_name], IMAGE_VIEW, #_name);
	LIST_IMAGES
#undef IMG_DO

#define IMG_DO(_name, ...) \
	[VKPT_IMG_##_name] = { \
		.sampler     = VK_NULL_HANDLE, \
		.imageView   = qvk.images_views[VKPT_IMG_##_name], \
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL \
	},
	VkDescriptorImageInfo desc_output_img_info[] = {
		LIST_IMAGES
	};
#undef IMG_DO

	VkDescriptorImageInfo img_info[] = {
#define IMG_DO(_name, ...) \
		[VKPT_IMG_##_name] = { \
			.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, \
			.imageView   = qvk.images_views[VKPT_IMG_##_name], \
			.sampler     = tex_sampler, \
		},

		LIST_IMAGES
	};
#undef IMG_DO

	/* create information to update descriptor sets */
	VkWriteDescriptorSet output_img_write[] = {
#define IMG_DO(_name, _binding, ...) \
		[VKPT_IMG_##_name] = { \
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, \
			.dstSet          = qvk.desc_set_textures, \
			.dstBinding      = BINDING_OFFSET_IMAGES + _binding, \
			.dstArrayElement = 0, \
			.descriptorCount = 1, \
			.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, \
			.pImageInfo      = desc_output_img_info + VKPT_IMG_##_name, \
		},
	LIST_IMAGES
#undef IMG_DO
#define IMG_DO(_name, _binding, ...) \
		[VKPT_IMG_##_name + NUM_VKPT_IMAGES] = { \
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, \
			.dstSet          = qvk.desc_set_textures, \
			.dstBinding      = BINDING_OFFSET_TEXTURES + _binding, \
			.dstArrayElement = 0, \
			.descriptorCount = 1, \
			.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, \
			.pImageInfo      = img_info + VKPT_IMG_##_name, \
		},
	LIST_IMAGES
#undef IMG_DO
	};

	vkUpdateDescriptorSets(qvk.device, LENGTH(output_img_write), output_img_write, 0, NULL);

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
	}
	vkFreeMemory(qvk.device, mem_images, NULL);
	mem_images = VK_NULL_HANDLE;

	return VK_SUCCESS;
}


// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
