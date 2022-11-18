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
#include "precomputed_sky.h"
#include "refresh/refresh.h"
#define _USE_MATH_DEFINES
#include <math.h>

#include <assert.h>
#include "dds.h"

// ----------------------------------------------------------------------------

const struct AtmosphereParameters* Constants = NULL;

#define EARTH_SURFACE_RADIUS (6360.0f)
#define EARTH_ATMOSPHERE_RADIUS (6420.f)
#define STROGGOS_SURFACE_RADIUS (6360.0f)
#define STROGGOS_ATMOSPHERE_RADIUS (6520.f)
#define DIST_TO_HORIZON(LOW,HIGH) ( HIGH*HIGH - LOW*LOW )

static struct AtmosphereParameters Params_Earth = {
	.solar_irradiance = { 1.47399998f, 1.85039997f, 1.91198003f },
	.sun_angular_radius = 0.00467499997f,
	.rayleigh_scattering = { 0.00580233941f, 0.0135577619f, 0.0331000052f },
	.bottom_radius = EARTH_SURFACE_RADIUS,
	.mie_scattering = { 0.0014985f, 0.0014985f, 0.0014985f },
	.top_radius = EARTH_ATMOSPHERE_RADIUS,
	.mie_phase_function_g = 0.8f,
	.SqDistanceToHorizontalBoundary = DIST_TO_HORIZON(EARTH_SURFACE_RADIUS, EARTH_ATMOSPHERE_RADIUS),
	.AtmosphereHeight = EARTH_ATMOSPHERE_RADIUS - EARTH_SURFACE_RADIUS,
};

static struct AtmosphereParameters Params_Stroggos = {
	.solar_irradiance = { 2.47399998, 1.85039997, 1.01198006 },
	.sun_angular_radius = 0.00934999995f,
	.rayleigh_scattering = { 0.0270983186, 0.0414223559, 0.0647224262 },
	.bottom_radius = STROGGOS_SURFACE_RADIUS,
	.mie_scattering = { 0.00342514296, 0.00342514296, 0.00342514296 },
	.top_radius = STROGGOS_ATMOSPHERE_RADIUS,
	.mie_phase_function_g = 0.9f,
	.SqDistanceToHorizontalBoundary = DIST_TO_HORIZON(STROGGOS_SURFACE_RADIUS, STROGGOS_ATMOSPHERE_RADIUS),
	.AtmosphereHeight = STROGGOS_ATMOSPHERE_RADIUS - STROGGOS_SURFACE_RADIUS,
};

// ----------------------------------------------------------------------------

struct ImageGPUInfo
{
	VkImage			Image;
	VkDeviceMemory	DeviceMemory;
	VkImageView		View;
};



void ReleaseInfo(struct ImageGPUInfo* Info)
{
	vkFreeMemory(qvk.device, Info->DeviceMemory, NULL);
	vkDestroyImage(qvk.device, Info->Image, NULL);
	vkDestroyImageView(qvk.device, Info->View, NULL);
	memset(Info, 0, sizeof(*Info));
}

struct ImageGPUInfo	SkyTransmittance;
struct ImageGPUInfo	SkyInscatter;
struct ImageGPUInfo	SkyIrradiance;
struct ImageGPUInfo	SkyClouds;

struct ImageGPUInfo TerrainAlbedo;
struct ImageGPUInfo TerrainNormals;
struct ImageGPUInfo TerrainDepth;

VkDescriptorSetLayout	uniform_precomputed_descriptor_layout;
VkDescriptorSet         desc_set_precomputed_ubo;

#define PRECOMPUTED_SKY_BINDING_IDX					0
#define PRECOMPUTED_SKY_UBO_DESC_SET_IDX			3

static BufferResource_t atmosphere_params_buffer;
static VkDescriptorPool desc_pool_precomputed_ubo;

float terrain_shadowmap_viewproj[16] = { 0.f };

// ----------------------------------------------------------------------------

VkResult UploadImage(void* FirstPixel, size_t total_size, unsigned int Width, unsigned int Height, unsigned int Depth, unsigned int ArraySize, unsigned char Cube, VkFormat PixelFormat, uint32_t Binding, struct ImageGPUInfo* Info, const char* DebugName)
{
	BufferResource_t buf_img_upload;
	buffer_create(&buf_img_upload, total_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	void* bn_tex = buffer_map(&buf_img_upload);

	memcpy(bn_tex, FirstPixel, total_size);

	buffer_unmap(&buf_img_upload);
	bn_tex = NULL;

	enum VkImageType ImageType = VK_IMAGE_TYPE_2D;
	if (Depth == 0)
		Depth = 1;
	if (Depth > 1)
		ImageType = VK_IMAGE_TYPE_3D;

	VkImageCreateInfo img_info =
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.extent = {
		.width = Width,
		.height = Height,
		.depth = Depth,
	},
	.imageType = ImageType,
	.format = PixelFormat,
	.mipLevels = 1,
	.arrayLayers = ArraySize,
	.samples = VK_SAMPLE_COUNT_1_BIT,
	.tiling = VK_IMAGE_TILING_OPTIMAL,
	.usage = VK_IMAGE_USAGE_STORAGE_BIT
	| VK_IMAGE_USAGE_TRANSFER_DST_BIT
		| VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = qvk.queue_idx_graphics,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.flags = 0
	};

	if (Cube)
		img_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

	_VK(vkCreateImage(qvk.device, &img_info, NULL, &Info->Image));
	ATTACH_LABEL_VARIABLE_NAME(Info->Image, IMAGE, DebugName);

	VkMemoryRequirements mem_req;
	vkGetImageMemoryRequirements(qvk.device, Info->Image, &mem_req);
	assert(mem_req.size >= buf_img_upload.size);

	VkMemoryAllocateInfo mem_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_req.size,
		.memoryTypeIndex = get_memory_type(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};

#ifdef VKPT_DEVICE_GROUPS
	VkMemoryAllocateFlagsInfo mem_alloc_flags = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
		.flags = VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT,
		.deviceMask = (1 << qvk.device_count) - 1
	};

	if (qvk.device_count > 1) {
		mem_alloc_info.pNext = &mem_alloc_flags;
	}
#endif

	_VK(vkAllocateMemory(qvk.device, &mem_alloc_info, NULL, &Info->DeviceMemory));


	//_VK(allocate_gpu_memory(mem_req, &Info->DeviceMemory));
	_VK(vkBindImageMemory(qvk.device, Info->Image, Info->DeviceMemory, 0));

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
	if (Depth > 1)
		viewType = VK_IMAGE_VIEW_TYPE_3D;
	if (ArraySize == 6 && Cube)
		viewType = VK_IMAGE_VIEW_TYPE_CUBE;

	VkImageViewCreateInfo img_view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.viewType = viewType,
		.format = PixelFormat,
		.image = Info->Image,
		.subresourceRange = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = ArraySize,
	},
	.components = {
		VK_COMPONENT_SWIZZLE_R,
		VK_COMPONENT_SWIZZLE_G,
		VK_COMPONENT_SWIZZLE_B,
		VK_COMPONENT_SWIZZLE_A,			
	},
	};

	_VK(vkCreateImageView(qvk.device, &img_view_info, NULL, &Info->View));
	ATTACH_LABEL_VARIABLE_NAME(Info->View, IMAGE_VIEW, DebugName);

	VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

	VkImageSubresourceRange subresource_range = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = ArraySize,
	};

	IMAGE_BARRIER(cmd_buf,
		.image = Info->Image,
		.subresourceRange = subresource_range,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
	);

	VkBufferImageCopy cpy_info = {
		.bufferOffset = 0,
		.imageSubresource = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.mipLevel = 0,
		.baseArrayLayer = 0,
		.layerCount = ArraySize,
	},
	.imageOffset = { 0, 0, 0 },
	.imageExtent = { Width, Height, Depth }
	};
	vkCmdCopyBufferToImage(cmd_buf, buf_img_upload.buffer, Info->Image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cpy_info);


	IMAGE_BARRIER(cmd_buf,
		.image = Info->Image,
		.subresourceRange = subresource_range,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		);

	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, true);


	VkDescriptorImageInfo desc_img_info = {
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.imageView = Info->View,
		.sampler = qvk.tex_sampler,
	};

	VkWriteDescriptorSet s = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = qvk.desc_set_textures_even,
		.dstBinding = Binding,
		.dstArrayElement = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.pImageInfo = &desc_img_info,
	};

	vkUpdateDescriptorSets(qvk.device, 1, &s, 0, NULL);

	s.dstSet = qvk.desc_set_textures_odd;
	vkUpdateDescriptorSets(qvk.device, 1, &s, 0, NULL);

	vkQueueWaitIdle(qvk.queue_graphics);

	buffer_destroy(&buf_img_upload);

	return VK_SUCCESS;
}

#define ISBITMASK(header,r,g,b,a) ( header.RBitMask == r && header.GBitMask == g && header.BBitMask == b && header.ABitMask == a )

bool LoadImageFromDDS(const char* FileName, uint32_t Binding, struct ImageGPUInfo* Info, const char* DebugName)
{
	unsigned char* data = NULL;
	int len = FS_LoadFile(FileName, (void**)&data);

	if (!data)
	{
		Com_EPrintf("Couldn't read file %s\n", FileName);
		return false;
	}

	bool retval = false;

	const DDS_HEADER* dds = (DDS_HEADER*)data;
	const DDS_HEADER_DXT10* dxt10 = (DDS_HEADER_DXT10*)(data + sizeof(DDS_HEADER));

	if (dds->magic != DDS_MAGIC || dds->size != sizeof(DDS_HEADER) - 4)
	{
		Com_EPrintf("File %s does not have the expected DDS file format\n", FileName);
		goto done;
	}

	int Cube = 0;
	int ArraySize = 1;
	VkFormat PixelFormat = VK_FORMAT_UNDEFINED;
	size_t dds_header_size = sizeof(DDS_HEADER);

	if (dds->ddspf.fourCC == MAKEFOURCC('D', 'X', '1', '0'))
	{
		if (dxt10->dxgiFormat == DXGI_FORMAT_B8G8R8A8_UNORM)
			PixelFormat = VK_FORMAT_R8G8B8A8_UNORM;
		else if (dxt10->dxgiFormat == DXGI_FORMAT_R32_FLOAT)
			PixelFormat = VK_FORMAT_R32_SFLOAT;
		else if (dxt10->dxgiFormat == DXGI_FORMAT_R32G32B32A32_FLOAT)
			PixelFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
		else
		{
			Com_EPrintf("File %s uses an unsupported pixel format (%d)\n", FileName, dxt10->dxgiFormat);
			goto done;
		}

		Cube = (dxt10->miscFlag & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0;
		ArraySize = dxt10->arraySize * (Cube ? 6 : 1);
		dds_header_size = sizeof(DDS_HEADER) + sizeof(DDS_HEADER_DXT10);
	}
	else
	{
		if (dds->caps2 & DDS_CUBEMAP && ((dds->caps2 & DDS_CUBEMAP_ALLFACES) == DDS_CUBEMAP_ALLFACES))
		{
			Cube = 1;
			ArraySize = 6;
		}

		if (ISBITMASK(dds->ddspf, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000))
			PixelFormat = VK_FORMAT_R8G8B8A8_UNORM;
		else if (ISBITMASK(dds->ddspf, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000))
			PixelFormat = VK_FORMAT_R8G8B8A8_UNORM;
		else if (ISBITMASK(dds->ddspf, 0xffffffff, 0x00000000, 0x00000000, 0x00000000))
			PixelFormat = VK_FORMAT_R32_SFLOAT;
		else
			Com_EPrintf("File %s uses an unsupported pixel format.\n", FileName);
	}

	UploadImage(data + dds_header_size, len - dds_header_size, dds->width, dds->height, dds->depth, ArraySize, Cube, PixelFormat, Binding, Info, DebugName);

done:
	FS_FreeFile(data);
	return retval;
}

VkDescriptorSetLayout* SkyGetDescriptorLayout()
{
	return &uniform_precomputed_descriptor_layout;
}

VkDescriptorSet SkyGetDescriptorSet()
{
	return desc_set_precomputed_ubo;
}

VkResult
vkpt_uniform_precomputed_buffer_create(void)
{
	VkDescriptorSetLayoutBinding ubo_layout_binding = {
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.binding = PRECOMPUTED_SKY_BINDING_IDX,
		.stageFlags = VK_SHADER_STAGE_ALL,
	};

	VkDescriptorSetLayoutCreateInfo layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &ubo_layout_binding,
	};

	_VK(vkCreateDescriptorSetLayout(qvk.device, &layout_info, NULL, &uniform_precomputed_descriptor_layout));

	{
		buffer_create(&atmosphere_params_buffer, sizeof(struct AtmosphereParameters),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		ATTACH_LABEL_VARIABLE_NAME(atmosphere_params_buffer.buffer, IMAGE_VIEW, "AtmosphereParameters");
	}

	VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = MAX_FRAMES_IN_FLIGHT,
	};

	VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
		.maxSets = MAX_FRAMES_IN_FLIGHT,
	};

	_VK(vkCreateDescriptorPool(qvk.device, &pool_info, NULL, &desc_pool_precomputed_ubo));

	VkDescriptorSetAllocateInfo descriptor_set_alloc_info = 
	{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = desc_pool_precomputed_ubo,
		.descriptorSetCount = 1,
		.pSetLayouts = &uniform_precomputed_descriptor_layout,
	};

	{
		_VK(vkAllocateDescriptorSets(qvk.device, &descriptor_set_alloc_info, &desc_set_precomputed_ubo));

		BufferResource_t *ubo = &atmosphere_params_buffer;

		VkDescriptorBufferInfo buf_info = {
			.buffer = ubo->buffer,
			.offset = 0,
			.range = sizeof(struct AtmosphereParameters),
		};

		VkWriteDescriptorSet output_buf_write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = desc_set_precomputed_ubo,
			.dstBinding = PRECOMPUTED_SKY_BINDING_IDX,
			.dstArrayElement = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.pBufferInfo = &buf_info,
		};

		vkUpdateDescriptorSets(qvk.device, 1, &output_buf_write, 0, NULL);
	}

	return VK_SUCCESS;
}

VkResult
vkpt_uniform_precomputed_buffer_destroy(void)
{
	vkDestroyDescriptorPool(qvk.device, desc_pool_precomputed_ubo, NULL);
	vkDestroyDescriptorSetLayout(qvk.device, uniform_precomputed_descriptor_layout, NULL);
	desc_pool_precomputed_ubo = VK_NULL_HANDLE;
	uniform_precomputed_descriptor_layout = VK_NULL_HANDLE;

	buffer_destroy(&atmosphere_params_buffer);

	return VK_SUCCESS;
}

VkResult
vkpt_uniform_precomputed_buffer_update(void)
{
	BufferResource_t *ubo = &atmosphere_params_buffer;
	assert(ubo->memory != VK_NULL_HANDLE);
	assert(ubo->buffer != VK_NULL_HANDLE);
	assert(qvk.current_frame_index < MAX_FRAMES_IN_FLIGHT);

	struct AtmosphereParameters *mapped_ubo = buffer_map(ubo);
	assert(mapped_ubo);
	memcpy(mapped_ubo, Constants, sizeof(struct AtmosphereParameters));
	buffer_unmap(ubo);
	mapped_ubo = NULL;

	return VK_SUCCESS;
}

#define MATRIX(row, col) (row * 4 + col)

void create_identity_matrix(float matrix[16])
{
	uint32_t size = 16 * sizeof(float);
	memset(matrix, 0, size);
	matrix[MATRIX(0, 0)] = 1.0f;
	matrix[MATRIX(1, 1)] = 1.0f;
	matrix[MATRIX(2, 2)] = 1.0f;
	matrix[MATRIX(3, 3)] = 1.0f;
}

void create_look_at_matrix(float matrix[16], vec3_t EyePosition, vec3_t EyeDirection, vec3_t UpDirection)
{
	vec3_t f; 
	VectorNormalize2(EyeDirection, f);

	vec3_t s;
	CrossProduct(UpDirection, f, s);
	VectorNormalize2(s, s);

	vec3_t u;
	CrossProduct(f, s, u);

	float D0 = DotProduct(s, EyePosition);
	float D1 = DotProduct(u, EyePosition);
	float D2 = DotProduct(f, EyePosition);

	// Set identity
	create_identity_matrix(matrix);

	matrix[MATRIX(0, 0)] = s[0];
	matrix[MATRIX(1, 0)] = s[1];
	matrix[MATRIX(2, 0)] = s[2];
	matrix[MATRIX(0, 1)] = u[0];

	matrix[MATRIX(1, 1)] = u[1];
	matrix[MATRIX(2, 1)] = u[2];
	matrix[MATRIX(0, 2)] = f[0];
	matrix[MATRIX(1, 2)] = f[1];

	matrix[MATRIX(2, 2)] = f[2];
	matrix[MATRIX(3, 0)] = -D0;
	matrix[MATRIX(3, 1)] = -D1;
	matrix[MATRIX(3, 2)] = -D2;
}

void
create_centered_orthographic_matrix(float matrix[16], float xmin, float xmax,
	float ymin, float ymax, float znear, float zfar)
{
	float width, height;

	width = xmax - xmin;
	height = ymax - ymin;
	//float fRange = 1.0f / (znear - zfar);
	float fRange = 1.0f / (zfar - znear);

	matrix[0] = 2 / width;
	matrix[4] = 0;
	matrix[8] = 0;
	matrix[12] = -(xmax + xmin) / width;

	matrix[1] = 0;
	matrix[5] = 2 / height;
	matrix[9] = 0;
	matrix[13] = -(ymax + ymin) / height;

	matrix[2] = 0;
	matrix[6] = 0;
	matrix[10] = fRange;
	matrix[14] = -fRange * znear;

	matrix[3] = 0;
	matrix[7] = 0;
	matrix[11] = 0;
	matrix[15] = 1;
}

void UpdateTerrainShadowMapView(vec3_t forward)
{
	float Proj[16];
	float View[16];

	float BoundingOffset = ShadowmapWorldSize * 0.75f;
	create_centered_orthographic_matrix(Proj, -BoundingOffset, BoundingOffset, -BoundingOffset, BoundingOffset, -ShadowmapWorldSize * 0.75f, ShadowmapWorldSize * 0.75f);
	vec3_t up = { 0, 0, 1.0f };
	vec3_t origin = { 0.f };
	create_look_at_matrix(View, origin, forward, up);

	mult_matrix_matrix(terrain_shadowmap_viewproj, Proj, View);
}


VkResult SkyLoadScatterParameters(SkyPreset preset)
{
	const char* Planet = "UNKNOWN";

	if (preset == SKY_EARTH)
	{
		Planet = "earth";
		Constants = &Params_Earth;
	}
	else if (preset == SKY_STROGGOS)
	{
		Planet = "stroggos";
		Constants = &Params_Stroggos;
	}

	ReleaseInfo(&SkyTransmittance);
	ReleaseInfo(&SkyInscatter);
	ReleaseInfo(&SkyIrradiance);

	char FileBuf[MAX_QPATH];
	Q_snprintf(FileBuf, sizeof(FileBuf), "env/transmittance_%s.dds", Planet);
	LoadImageFromDDS(FileBuf, BINDING_OFFSET_SKY_TRANSMITTANCE, &SkyTransmittance, "SkyTransmittance");
	Q_snprintf(FileBuf, sizeof(FileBuf), "env/inscatter_%s.dds", Planet);
	LoadImageFromDDS(FileBuf, BINDING_OFFSET_SKY_SCATTERING, &SkyInscatter, "SkyInscatter");
	Q_snprintf(FileBuf, sizeof(FileBuf), "env/irradiance_%s.dds", Planet);
	LoadImageFromDDS(FileBuf, BINDING_OFFSET_SKY_IRRADIANCE, &SkyIrradiance, "SkyIrradiance");

	vkpt_uniform_precomputed_buffer_update();

	return VK_SUCCESS;
}

VkResult SkyInitializeDataGPU()
{
	LoadImageFromDDS("env/clouds.dds", BINDING_OFFSET_SKY_CLOUDS, &SkyClouds, "SkyClouds");

	LoadImageFromDDS("env/terrain_albedo.dds", BINDING_OFFSET_TERRAIN_ALBEDO, &TerrainAlbedo, "SkyGBufferAlbedo");
	LoadImageFromDDS("env/terrain_normal.dds", BINDING_OFFSET_TERRAIN_NORMALS, &TerrainNormals, "SkyGBufferNormals");
	LoadImageFromDDS("env/terrain_depth.dds", BINDING_OFFSET_TERRAIN_DEPTH, &TerrainDepth, "SkyGBufferDepth");
	
	vkpt_uniform_precomputed_buffer_create();

	return VK_SUCCESS;
}

void SkyReleaseDataGPU()
{
	ReleaseInfo(&SkyTransmittance);
	ReleaseInfo(&SkyInscatter);
	ReleaseInfo(&SkyIrradiance);

	ReleaseInfo(&SkyClouds);

	ReleaseInfo(&TerrainAlbedo);
	ReleaseInfo(&TerrainNormals);
	ReleaseInfo(&TerrainDepth);

	vkpt_uniform_precomputed_buffer_destroy();
}

// ----------------------------------------------------------------------------
//				--Shadows--
//
//				   Data
// ----------------------------------------------------------------------------

struct ShadowVertex
{
	float x;
	float y;
	float z;
};

struct ShadowFace
{
	uint32_t A;
	uint32_t B;
	uint32_t C;
};

struct Shadowmap
{
	VkImage				TargetTexture;
	VkDeviceMemory		AllocatedMemory;
	VkImageView			DepthView;
	VkSampler			DepthSampler;
	VkFramebuffer		FrameBuffer;
	uint32_t			Width;
	uint32_t			Height;
	VkFormat			DepthFormat;
};

struct ShadowmapGeometry
{
	BufferResource_t		Vertexes;
	BufferResource_t		Indexes;
	uint32_t				IndexCount;
};

// ----------------------------------------------------------------------------
//				Data
// ----------------------------------------------------------------------------

struct Shadowmap				ShadowmapData;
struct ShadowmapGeometry		ShadowmapGrid;

extern VkPipelineLayout        pipeline_layout_smap;
extern VkRenderPass            render_pass_smap;
extern VkPipeline              pipeline_smap;

// ----------------------------------------------------------------------------
//				Functions
// ----------------------------------------------------------------------------

struct ShadowmapGeometry FillVertexAndIndexBuffers(const char* FileName, unsigned int SideSize, float size_km)
{
	struct  ShadowmapGeometry result = { 0 };

	unsigned char* file_data = NULL;
	FS_LoadFile(FileName, (void**)&file_data);

	if (!file_data)
	{
		Com_EPrintf("Couldn't read file %s\n", FileName);
		goto done;
	}

	DDS_HEADER* dds = (DDS_HEADER*)file_data;
	DDS_HEADER_DXT10* dxt10 = (DDS_HEADER_DXT10*)(file_data + sizeof(DDS_HEADER));

	if (dds->magic != DDS_MAGIC || dds->size != sizeof(DDS_HEADER) - 4 || dds->ddspf.fourCC != MAKEFOURCC('D', 'X', '1', '0') || dxt10->dxgiFormat != DXGI_FORMAT_R32_FLOAT)
	{
		Com_EPrintf("File %s does not have the expected DDS file format\n", FileName);
		goto done;
	}

	float* file_pixels = (float*)(file_data + sizeof(DDS_HEADER) + sizeof(DDS_HEADER_DXT10));
	

	size_t VertexBufferSize = SideSize * SideSize * sizeof(struct ShadowVertex);	
	size_t IndexBufferSize = (SideSize - 1) * (SideSize - 1) * 2 * sizeof(struct ShadowFace);
	size_t IndexCount = IndexBufferSize / sizeof(uint32_t);

	VertexBufferSize = align(VertexBufferSize, 64 * 1024);
	IndexBufferSize = align(IndexBufferSize, 64 * 1024);

	BufferResource_t upload_buffer;
	buffer_create(&upload_buffer, VertexBufferSize + IndexBufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	byte* mapped_upload_buf = (byte*)buffer_map(&upload_buffer);

	struct ShadowVertex* vertexes = (struct ShadowVertex*)mapped_upload_buf;
	struct ShadowFace* indexes = (struct ShadowFace*)(mapped_upload_buf + VertexBufferSize);

	if (!vertexes || !indexes)
		return result;

	result.IndexCount = IndexCount;

	float delta = size_km / (float)SideSize;
	float X = -0.5f * size_km;
	float Y = -0.5f * size_km;

	for (unsigned int y = 0; y < SideSize; y++)
	{
		for (unsigned int x = 0; x < SideSize; x++)
		{
			unsigned int index = y * SideSize + x;

			float Z = file_pixels[index];

			vertexes[index].x = X;
			vertexes[index].y = Y;
			vertexes[index].z = (x == 0) || (y == 0) || (x == SideSize - 1) || (y == SideSize - 1) ? -6.f : Z * 3.f;
			X += delta;
		}
		X = -0.5f * size_km;
		Y += delta;
	}

	unsigned int i = 0;
	for (unsigned int y = 0; y < SideSize-1; y++)
	{
		for (unsigned int x = 0; x < SideSize-1; x++)
		{
			unsigned int upper_index = (y + 1) * SideSize + x;
			unsigned int lower_index = y * SideSize + x;
			indexes[i].A = lower_index;
			indexes[i].B = upper_index + 1;
			indexes[i].C = lower_index + 1;
			i++;
			indexes[i].A = lower_index;
			indexes[i].B = upper_index;
			indexes[i].C = upper_index + 1;
			i++;
		}
	}

	buffer_unmap(&upload_buffer);


	buffer_create(&result.Vertexes, VertexBufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ATTACH_LABEL_VARIABLE_NAME(result.Vertexes.buffer, BUFFER, "Shadowmap Vertex Buffer");

	buffer_create(&result.Indexes, IndexBufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	ATTACH_LABEL_VARIABLE_NAME(result.Indexes.buffer, BUFFER, "Shadowmap Index Buffer");

	VkCommandBuffer cmd_buf = vkpt_begin_command_buffer(&qvk.cmd_buffers_graphics);

	BUFFER_BARRIER(cmd_buf, 
		.buffer = upload_buffer.buffer, 
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
		.offset = 0,
		.size = VK_WHOLE_SIZE);

	BUFFER_BARRIER(cmd_buf,
		.buffer = result.Vertexes.buffer,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.offset = 0,
		.size = VK_WHOLE_SIZE);

	BUFFER_BARRIER(cmd_buf,
		.buffer = result.Indexes.buffer,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.offset = 0,
		.size = VK_WHOLE_SIZE);

	VkBufferCopy region = {
		.srcOffset = 0,
		.dstOffset = 0,
		.size = VertexBufferSize
	};

	vkCmdCopyBuffer(cmd_buf, upload_buffer.buffer, result.Vertexes.buffer, 1, &region);

	region.srcOffset = VertexBufferSize;
	region.size = IndexBufferSize;

	vkCmdCopyBuffer(cmd_buf, upload_buffer.buffer, result.Indexes.buffer, 1, &region);

	BUFFER_BARRIER(cmd_buf,
		.buffer = result.Vertexes.buffer,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
		.offset = 0,
		.size = VK_WHOLE_SIZE);

	BUFFER_BARRIER(cmd_buf,
		.buffer = result.Indexes.buffer,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_INDEX_READ_BIT,
		.offset = 0,
		.size = VK_WHOLE_SIZE);

	IMAGE_BARRIER(cmd_buf,
		.image = ShadowmapData.TargetTexture,
		.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,.levelCount = 1,.layerCount = 1 },
		.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	vkpt_submit_command_buffer_simple(cmd_buf, qvk.queue_graphics, true);

	vkQueueWaitIdle(qvk.queue_graphics);
	buffer_destroy(&upload_buffer);

done:
	if (file_data)
		FS_FreeFile(file_data);

	return result;
}


// ----------------------------------------------------------------------------

void ReleaseShadowmap(struct Shadowmap* InOutShadowmap)
{
	if (InOutShadowmap->DepthSampler)
		vkDestroySampler(qvk.device, InOutShadowmap->DepthSampler, NULL);

	if (InOutShadowmap->DepthView)
		vkDestroyImageView(qvk.device, InOutShadowmap->DepthView, NULL);
	if (InOutShadowmap->TargetTexture)
		vkDestroyImage(qvk.device, InOutShadowmap->TargetTexture, NULL);
	if (InOutShadowmap->AllocatedMemory)
		vkFreeMemory(qvk.device, InOutShadowmap->AllocatedMemory, NULL);

	if (InOutShadowmap->FrameBuffer)
		vkDestroyFramebuffer(qvk.device, InOutShadowmap->FrameBuffer, NULL);
}

void CreateShadowMap(struct Shadowmap* InOutShadowmap)
{
	InOutShadowmap->DepthFormat = VK_FORMAT_D32_SFLOAT;
	InOutShadowmap->Width = ShadowmapSize;
	InOutShadowmap->Height = ShadowmapSize;

	VkImageCreateInfo ShadowTexInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.extent.width = InOutShadowmap->Width,
		.extent.height = InOutShadowmap->Height,
		.extent.depth = 1,
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.format = InOutShadowmap->DepthFormat,																// Depth stencil attachment
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,		// We will sample directly from the depth attachment for the shadow mapping
	};

	_VK(vkCreateImage(qvk.device, &ShadowTexInfo, NULL, &InOutShadowmap->TargetTexture));
	ATTACH_LABEL_VARIABLE_NAME(InOutShadowmap->TargetTexture, IMAGE_VIEW, "EnvShadowMap");

	VkMemoryRequirements memReqs = {0};
	vkGetImageMemoryRequirements(qvk.device, InOutShadowmap->TargetTexture, &memReqs);

	VkMemoryAllocateInfo memAlloc =
	{
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = memReqs.size,
		.memoryTypeIndex = get_memory_type(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
	};

	_VK(vkAllocateMemory(qvk.device, &memAlloc, NULL, &InOutShadowmap->AllocatedMemory));
	_VK(vkBindImageMemory(qvk.device, InOutShadowmap->TargetTexture, InOutShadowmap->AllocatedMemory, 0));

	VkImageViewCreateInfo depthStencilView = 
	{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = InOutShadowmap->DepthFormat,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		},
		.image = InOutShadowmap->TargetTexture,
	};

	_VK(vkCreateImageView(qvk.device, &depthStencilView, NULL, &InOutShadowmap->DepthView));

	VkDescriptorImageInfo desc_img_info = {
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.imageView = InOutShadowmap->DepthView,
		.sampler = qvk.tex_sampler,
	};

	VkWriteDescriptorSet s = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = qvk.desc_set_textures_even,
		.dstBinding = BINDING_OFFSET_TERRAIN_SHADOWMAP,
		.dstArrayElement = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.descriptorCount = 1,
		.pImageInfo = &desc_img_info,
	};

	vkUpdateDescriptorSets(qvk.device, 1, &s, 0, NULL);

	s.dstSet = qvk.desc_set_textures_odd;
	vkUpdateDescriptorSets(qvk.device, 1, &s, 0, NULL);

	VkSamplerCreateInfo sampler = 
	{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = sampler.addressModeU,
		.addressModeW = sampler.addressModeU,
		.mipLodBias = 0.0f,
		.maxAnisotropy = 1.0f,
		.minLod = 0.0f,
		.maxLod = 1.0f,
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
	};

	_VK(vkCreateSampler(qvk.device, &sampler, NULL, &InOutShadowmap->DepthSampler));

	// Create frame buffer
	VkFramebufferCreateInfo fbufCreateInfo = 
	{
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = render_pass_smap,
		.attachmentCount = 1,
		.pAttachments = &InOutShadowmap->DepthView,
		.width = InOutShadowmap->Width,
		.height = InOutShadowmap->Height,
		.layers = 1,
	};
	_VK(vkCreateFramebuffer(qvk.device, &fbufCreateInfo, NULL, &InOutShadowmap->FrameBuffer));
}

// ----------------------------------------------------------------------------

void InitializeShadowmapResources()
{
	CreateShadowMap(&ShadowmapData);
	ShadowmapGrid = FillVertexAndIndexBuffers("env/terrain_heightmap.dds", ShadowmapGridSize, ShadowmapWorldSize);
}

void ReleaseShadowmapResources()
{
	buffer_destroy(&ShadowmapGrid.Indexes);
	buffer_destroy(&ShadowmapGrid.Vertexes);
	memset(&ShadowmapGrid, 0, sizeof(ShadowmapGrid));

	ReleaseShadowmap(&ShadowmapData);
}
// ----------------------------------------------------------------------------

void RecordCommandBufferShadowmap(VkCommandBuffer cmd_buf)
{
	IMAGE_BARRIER(cmd_buf,
		.image = ShadowmapData.TargetTexture,
		.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 },
		.srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	);
	
	VkClearValue clearValues;
	clearValues.depthStencil.depth = 1.0f;
	clearValues.depthStencil.stencil = 0;

	VkRenderPassBeginInfo renderPassBeginInfo =
	{
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = render_pass_smap,
		.framebuffer = ShadowmapData.FrameBuffer,
		.renderArea.extent.width = ShadowmapData.Width,
		.renderArea.extent.height = ShadowmapData.Height,
		.clearValueCount = 1,
		.pClearValues = &clearValues,
	};

	vkCmdBeginRenderPass(cmd_buf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_smap);

	VkViewport viewport = 
	{
		.width = ShadowmapData.Width,
		.height = ShadowmapData.Height,
		.minDepth = 0,
		.maxDepth = 1.0f,
	};

	vkCmdSetViewport(cmd_buf, 0, 1, &viewport);

	VkRect2D rect2D = 
	{
		.extent.width = ShadowmapData.Width,
		.extent.height = ShadowmapData.Height,
		.offset.x = 0,
		.offset.y = 0,
	};

	vkCmdSetScissor(cmd_buf, 0, 1, &rect2D);

	vkCmdPushConstants(cmd_buf, pipeline_layout_smap, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 16, terrain_shadowmap_viewproj);

	VkDeviceSize offsets[1] = { 0 };
	vkCmdBindVertexBuffers(cmd_buf, 0, 1, &ShadowmapGrid.Vertexes.buffer, offsets);
	vkCmdBindIndexBuffer(cmd_buf, ShadowmapGrid.Indexes.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(cmd_buf, ShadowmapGrid.IndexCount, 1, 0, 0, 0);
	vkCmdEndRenderPass(cmd_buf);

	IMAGE_BARRIER(cmd_buf,
		.image = ShadowmapData.TargetTexture,
		.subresourceRange = { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,.levelCount = 1,.layerCount = 1 },
		.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	);
}
