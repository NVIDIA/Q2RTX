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

#include "shared/shared.h"
#include "common/bsp.h"
#include "common/cmd.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/files.h"
#include "common/math.h"
#include "client/video.h"
#include "client/client.h"
#include "refresh/refresh.h"
#include "refresh/images.h"
#include "refresh/models.h"
#include "system/hunk.h"
#include "vkpt.h"
#include "shader/light_hierarchy.h"

#include "shader/vertex_buffer.h"

#include <vulkan/vulkan.h>
#include <SDL.h>
#include <SDL_vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

cvar_t *vkpt_reconstruction;
cvar_t *cvar_rtx;
cvar_t *vkpt_profiler;

static bsp_t *bsp_world_model;

typedef enum {
	VKPT_INIT_DEFAULT            = 0,
	VKPT_INIT_SWAPCHAIN_RECREATE = (1 << 0),
	VKPT_INIT_RELOAD_SHADER      = (1 << 1),
} VkptInitFlags_t;

typedef struct VkptInit_s {
	const char *name;
	VkResult (*initialize)();
	VkResult (*destroy)();
	VkptInitFlags_t flags;
	int is_initialized;
} VkptInit_t;
VkptInit_t vkpt_initialization[] = {
	{ "profiler", vkpt_profiler_initialize,            vkpt_profiler_destroy,                VKPT_INIT_DEFAULT,            0 },
	{ "shader",   vkpt_load_shader_modules,            vkpt_destroy_shader_modules,          VKPT_INIT_RELOAD_SHADER,      0 },
	{ "vbo",      vkpt_vertex_buffer_create,           vkpt_vertex_buffer_destroy,           VKPT_INIT_DEFAULT,            0 },
	{ "ubo",      vkpt_uniform_buffer_create,          vkpt_uniform_buffer_destroy,          VKPT_INIT_DEFAULT,            0 },
	{ "textures", vkpt_textures_initialize,            vkpt_textures_destroy,                VKPT_INIT_DEFAULT,            0 },
	{ "images",   vkpt_create_images,                  vkpt_destroy_images,                  VKPT_INIT_SWAPCHAIN_RECREATE, 0 },
	{ "draw",     vkpt_draw_initialize,                vkpt_draw_destroy,                    VKPT_INIT_DEFAULT,            0 },
	{ "lh",       vkpt_lh_initialize,                  vkpt_lh_destroy,                      VKPT_INIT_DEFAULT,            0 },
	{ "pt",       vkpt_pt_init,                        vkpt_pt_destroy,                      VKPT_INIT_DEFAULT,            0 },
	{ "pt|",      vkpt_pt_create_pipelines,            vkpt_pt_destroy_pipelines,            VKPT_INIT_SWAPCHAIN_RECREATE
	                                                                                       | VKPT_INIT_RELOAD_SHADER,      0 },
	{ "draw|",    vkpt_draw_create_pipelines,          vkpt_draw_destroy_pipelines,          VKPT_INIT_SWAPCHAIN_RECREATE
	                                                                                       | VKPT_INIT_RELOAD_SHADER,      0 },
	{ "vbo|",     vkpt_vertex_buffer_create_pipelines, vkpt_vertex_buffer_destroy_pipelines, VKPT_INIT_RELOAD_SHADER,      0 },
	{ "asvgf",    vkpt_asvgf_initialize,               vkpt_asvgf_destroy,                   VKPT_INIT_DEFAULT,            0 },
	{ "asvgf|",   vkpt_asvgf_create_pipelines,         vkpt_asvgf_destroy_pipelines,         VKPT_INIT_RELOAD_SHADER,      0 },
};

VkResult
vkpt_initialize_all(VkptInitFlags_t init_flags)
{
	vkDeviceWaitIdle(qvk.device);
	for(int i = 0; i < LENGTH(vkpt_initialization); i++) {
		VkptInit_t *init = vkpt_initialization + i;
		if((init->flags & init_flags) != init_flags)
			continue;
		Com_Printf("initializing %s\n", vkpt_initialization[i].name);
		assert(!init->is_initialized);
		init->is_initialized = init->initialize
			? (init->initialize() == VK_SUCCESS)
			: 1;
		assert(init->is_initialized);
	}
	return VK_SUCCESS;
}

VkResult
vkpt_destroy_all(VkptInitFlags_t destroy_flags)
{
	vkDeviceWaitIdle(qvk.device);
	for(int i = LENGTH(vkpt_initialization) - 1; i >= 0; i--) {
		VkptInit_t *init = vkpt_initialization + i;
		if((init->flags & destroy_flags) != destroy_flags)
			continue;
		Com_Printf("destroying %s\n", vkpt_initialization[i].name);
		assert(init->is_initialized);
		init->is_initialized = init->destroy
			? !(init->destroy() == VK_SUCCESS)
			: 0;
		assert(!init->is_initialized);
	}
	return VK_SUCCESS;
}

void
vkpt_reload_shader()
{
	char buf[1024];
#ifdef _WIN32
	FILE *f = _popen("bash -c \"make -C/home/cschied/quake2-pt compile_shaders\"", "r");
#else
	FILE *f = popen("make -j compile_shaders", "r");
#endif
	if(f) {
		while(fgets(buf, sizeof buf, f)) {
			Com_Printf("%s", buf);
		}
		fclose(f);
	}
	vkpt_destroy_all(VKPT_INIT_RELOAD_SHADER);
	vkpt_initialize_all(VKPT_INIT_RELOAD_SHADER);
}

refcfg_t r_config;
int registration_sequence;
vkpt_refdef_t vkpt_refdef = {
	.z_near = 1.0f,
	.z_far  = 4096.0f,
};

QVK_t qvk = {
	.win_width          = 1920,
	.win_height         = 1080,
	.frame_counter      = 0,
};

#define _VK_EXTENSION_DO(a) PFN_##a q##a;
_VK_EXTENSION_LIST
#undef _VK_EXTENSION_DO

const char *vk_requested_layers[] = {
	"VK_LAYER_LUNARG_standard_validation"
};

const char *vk_requested_instance_extensions[] = {
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
};

const char *vk_requested_device_extensions[] = {
	VK_NV_RAY_TRACING_EXTENSION_NAME,
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
#ifdef VKPT_ENABLE_VALIDATION
	VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
#endif
};

static const VkApplicationInfo vk_app_info = {
	.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
	.pApplicationName   = "quake 2 pathtracing",
	.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
	.pEngineName        = "vkpt",
	.engineVersion      = VK_MAKE_VERSION(1, 0, 0),
	.apiVersion         = VK_API_VERSION_1_1,
};

/* use this to override file names */
static const char *shader_module_file_names[NUM_QVK_SHADER_MODULES];

int register_model_dirty;

void
get_vk_extension_list(
		const char *layer,
		uint32_t *num_extensions,
		VkExtensionProperties **ext)
{
	_VK(vkEnumerateInstanceExtensionProperties(layer, num_extensions, NULL));
	*ext = malloc(sizeof(**ext) * *num_extensions);
	_VK(vkEnumerateInstanceExtensionProperties(layer, num_extensions, *ext));
}

void
get_vk_layer_list(
		uint32_t *num_layers,
		VkLayerProperties **ext)
{
	_VK(vkEnumerateInstanceLayerProperties(num_layers, NULL));
	*ext = malloc(sizeof(**ext) * *num_layers);
	_VK(vkEnumerateInstanceLayerProperties(num_layers, *ext));
}

int
layer_supported(const char *name)
{
	assert(qvk.layers);
	for(int i = 0; i < qvk.num_layers; i++)
		if(!strcmp(name, qvk.layers[i].layerName))
			return 1;
	return 0;
}

int
layer_requested(const char *name)
{
	for(int i = 0; i < LENGTH(vk_requested_layers); i++)
		if(!strcmp(name, vk_requested_layers[i]))
			return 1;
	return 0;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
vk_debug_callback(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type,
		const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
		void *user_data)
{
	Com_EPrintf("validation layer: %s\n", callback_data->pMessage);
	return VK_FALSE;
}

VkResult
qvkCreateDebugUtilsMessengerEXT(
		VkInstance instance,
		const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkDebugUtilsMessengerEXT* pCallback)
{
	PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)
		vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
	if(func)
		return func(instance, pCreateInfo, pAllocator, pCallback);
	return VK_ERROR_EXTENSION_NOT_PRESENT;
}

VkResult
qvkDestroyDebugUtilsMessengerEXT(
		VkInstance instance,
		VkDebugUtilsMessengerEXT callback,
		const VkAllocationCallbacks* pAllocator)
{
	PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)
		vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
	if(func) {
		func(instance, callback, pAllocator);
		return VK_SUCCESS;
	}
	return VK_ERROR_EXTENSION_NOT_PRESENT;
}

VkResult
create_swapchain()
{
	/* create swapchain (query details and ignore them afterwards :-) )*/
	VkSurfaceCapabilitiesKHR surf_capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(qvk.physical_device, qvk.surface, &surf_capabilities);

	uint32_t num_formats = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(qvk.physical_device, qvk.surface, &num_formats, NULL);
	VkSurfaceFormatKHR *avail_surface_formats = alloca(sizeof(VkSurfaceFormatKHR) * num_formats);
	vkGetPhysicalDeviceSurfaceFormatsKHR(qvk.physical_device, qvk.surface, &num_formats, avail_surface_formats);
	Com_Printf("num surface formats: %d\n", num_formats);

	Com_Printf("available surface formats:\n");
	for(int i = 0; i < num_formats; i++)
		Com_Printf("  %s\n", vk_format_to_string(avail_surface_formats[i].format));


	VkFormat acceptable_formats[] = {
		VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
	};

	//qvk.surf_format.format     = VK_FORMAT_R8G8B8A8_SRGB;
	//qvk.surf_format.format     = VK_FORMAT_B8G8R8A8_SRGB;
	for(int i = 0; i < LENGTH(acceptable_formats); i++) {
		for(int j = 0; j < num_formats; j++)
			if(acceptable_formats[i] == avail_surface_formats[j].format) {
				qvk.surf_format = avail_surface_formats[j];
				goto out;
			}
	}
out:;

	uint32_t num_present_modes = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(qvk.physical_device, qvk.surface, &num_present_modes, NULL);
	VkPresentModeKHR *avail_present_modes = alloca(sizeof(VkPresentModeKHR) * num_present_modes);
	vkGetPhysicalDeviceSurfacePresentModesKHR(qvk.physical_device, qvk.surface, &num_present_modes, avail_present_modes);

	//qvk.present_mode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
	//qvk.present_mode = VK_PRESENT_MODE_FIFO_KHR;
	qvk.present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
	//qvk.present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;

	if(surf_capabilities.currentExtent.width != ~0u) {
		qvk.extent = surf_capabilities.currentExtent;
	}
	else {
		qvk.extent.width  = MIN(surf_capabilities.maxImageExtent.width,  qvk.win_width);
		qvk.extent.height = MIN(surf_capabilities.maxImageExtent.height, qvk.win_height);

		qvk.extent.width  = MAX(surf_capabilities.minImageExtent.width,  qvk.extent.width);
		qvk.extent.height = MAX(surf_capabilities.minImageExtent.height, qvk.extent.height);
	}

	uint32_t num_images = 2;
	//uint32_t num_images = surf_capabilities.minImageCount + 1;
	if(surf_capabilities.maxImageCount > 0)
		num_images = MIN(num_images, surf_capabilities.maxImageCount);

	VkSwapchainCreateInfoKHR swpch_create_info = {
		.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface               = qvk.surface,
		.minImageCount         = num_images,
		.imageFormat           = qvk.surf_format.format,
		.imageColorSpace       = qvk.surf_format.colorSpace,
		.imageExtent           = qvk.extent,
		.imageArrayLayers      = 1, /* only needs to be changed for stereoscopic rendering */ 
		.imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
		                       | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE, /* VK_SHARING_MODE_CONCURRENT if not using same queue */
		.queueFamilyIndexCount = 0,
		.pQueueFamilyIndices   = NULL,
		.preTransform          = surf_capabilities.currentTransform,
		.compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, /* no alpha for window transparency */
		.presentMode           = qvk.present_mode,
		.clipped               = VK_FALSE, /* do not render pixels that are occluded by other windows */
		//.clipped               = VK_TRUE, /* do not render pixels that are occluded by other windows */
		.oldSwapchain          = VK_NULL_HANDLE, /* need to provide previous swapchain in case of window resize */
	};

	if(vkCreateSwapchainKHR(qvk.device, &swpch_create_info, NULL, &qvk.swap_chain) != VK_SUCCESS) {
		Com_EPrintf("error creating swapchain\n");
		return 1;
	}

	vkGetSwapchainImagesKHR(qvk.device, qvk.swap_chain, &qvk.num_swap_chain_images, NULL);
	//qvk.swap_chain_images = malloc(qvk.num_swap_chain_images * sizeof(*qvk.swap_chain_images));
	assert(qvk.num_swap_chain_images < MAX_SWAPCHAIN_IMAGES);
	vkGetSwapchainImagesKHR(qvk.device, qvk.swap_chain, &qvk.num_swap_chain_images, qvk.swap_chain_images);

	//qvk.swap_chain_image_views = malloc(qvk.num_swap_chain_images * sizeof(*qvk.swap_chain_image_views));
	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		VkImageViewCreateInfo img_create_info = {
			.sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image      = qvk.swap_chain_images[i],
			.viewType   = VK_IMAGE_VIEW_TYPE_2D,
			.format     = qvk.surf_format.format,
#if 1
			.components = {
				VK_COMPONENT_SWIZZLE_R,
				VK_COMPONENT_SWIZZLE_G,
				VK_COMPONENT_SWIZZLE_B,
				VK_COMPONENT_SWIZZLE_A
			},
#endif
			.subresourceRange = {
				.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel   = 0,
				.levelCount     = 1,
				.baseArrayLayer = 0,
				.layerCount     = 1
			}
		};

		if(vkCreateImageView(qvk.device, &img_create_info, NULL, qvk.swap_chain_image_views + i) != VK_SUCCESS) {
			Com_EPrintf("error creating image view!");
			return 1;
		}
	}

	return VK_SUCCESS;
}

VkResult
create_command_pool_and_fences()
{
	VkCommandPoolCreateInfo cmd_pool_create_info = {
		.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = qvk.queue_idx_graphics,
		.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
	};

	/* command pool and buffers */
	_VK(vkCreateCommandPool(qvk.device, &cmd_pool_create_info, NULL, &qvk.command_pool));

	qvk.num_command_buffers = qvk.num_swap_chain_images;
	VkCommandBufferAllocateInfo cmd_buf_alloc_info = {
		.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool        = qvk.command_pool,
		.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = qvk.num_command_buffers
	};
	qvk.command_buffers = malloc(qvk.num_command_buffers * sizeof(*qvk.command_buffers));
	_VK(vkAllocateCommandBuffers(qvk.device, &cmd_buf_alloc_info, qvk.command_buffers));


	/* fences and semaphores */
	VkSemaphoreCreateInfo semaphore_info = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	for(int i = 0; i < NUM_SEMAPHORES; i++)
		_VK(vkCreateSemaphore(qvk.device, &semaphore_info, NULL, &qvk.semaphores[i]));

	VkFenceCreateInfo fence_info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT, /* fence's initial state set to be signaled
												  to make program not hang */
	};
	for(int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		_VK(vkCreateFence(qvk.device, &fence_info, NULL, qvk.fences_frame_sync + i));
	}

	return VK_SUCCESS;
}

int
init_vulkan()
{
	/* layers */
	get_vk_layer_list(&qvk.num_layers, &qvk.layers);
	Com_Printf("available vulkan layers: \n");
	for(int i = 0; i < qvk.num_layers; i++) {
		int requested = layer_requested(qvk.layers[i].layerName);
		Com_Printf("%s%s, ", qvk.layers[i].layerName, requested ? " (requested)" : "");
	}
	Com_Printf("\n");

	/* instance extensions */
	int num_inst_ext_combined = qvk.num_sdl2_extensions + LENGTH(vk_requested_instance_extensions);
	char **ext = alloca(sizeof(char *) * num_inst_ext_combined);
	memcpy(ext, qvk.sdl2_extensions, qvk.num_sdl2_extensions * sizeof(*qvk.sdl2_extensions));
	memcpy(ext + qvk.num_sdl2_extensions, vk_requested_instance_extensions, sizeof(vk_requested_instance_extensions));

	get_vk_extension_list(NULL, &qvk.num_extensions, &qvk.extensions); /* valid here? */
	Com_Printf("supported vulkan instance extensions: \n");
	for(int i = 0; i < qvk.num_extensions; i++) {
		int requested = 0;
		for(int j = 0; j < num_inst_ext_combined; j++) {
			if(!strcmp(qvk.extensions[i].extensionName, ext[j])) {
				requested = 1;
				break;
			}
		}
		Com_Printf("%s%s, ", qvk.extensions[i].extensionName, requested ? " (requested)" : "");
	}
	Com_Printf("\n");

	/* create instance */
	VkInstanceCreateInfo inst_create_info = {
		.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo        = &vk_app_info,
#ifdef VKPT_ENABLE_VALIDATION
		.enabledLayerCount       = LENGTH(vk_requested_layers),
		.ppEnabledLayerNames     = vk_requested_layers,
#endif
		.enabledExtensionCount   = num_inst_ext_combined,
		.ppEnabledExtensionNames = (const char * const*)ext,
	};

	_VK(vkCreateInstance(&inst_create_info, NULL, &qvk.instance));

	/* setup debug callback */
	VkDebugUtilsMessengerCreateInfoEXT dbg_create_info = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity =
			  VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType =
			  VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = vk_debug_callback,
		.pUserData = NULL
	};

	_VK(qvkCreateDebugUtilsMessengerEXT(qvk.instance, &dbg_create_info, NULL, &qvk.dbg_messenger));

	/* create surface */
	if(!SDL_Vulkan_CreateSurface(qvk.window, qvk.instance, &qvk.surface)) {
		Com_EPrintf("[vkq2] could not create surface!\n");
		return 1;
	}

	/* pick physical device (iterate over all but pick device 0 anyways) */
	uint32_t num_devices = 0;
	_VK(vkEnumeratePhysicalDevices(qvk.instance, &num_devices, NULL));
	if(num_devices == 0)
		return 1;
	VkPhysicalDevice *devices = alloca(sizeof(VkPhysicalDevice) *num_devices);
	_VK(vkEnumeratePhysicalDevices(qvk.instance, &num_devices, devices));

	int picked_device = -1;
	for(int i = 0; i < num_devices; i++) {
		VkPhysicalDeviceProperties dev_properties;
		VkPhysicalDeviceFeatures   dev_features;
		vkGetPhysicalDeviceProperties(devices[i], &dev_properties);
		vkGetPhysicalDeviceFeatures  (devices[i], &dev_features);

		Com_Printf("dev %d: %s\n", i, dev_properties.deviceName);
		Com_Printf("max number of allocations %d\n", dev_properties.limits.maxMemoryAllocationCount);
		uint32_t num_ext;
		vkEnumerateDeviceExtensionProperties(devices[i], NULL, &num_ext, NULL);

		VkExtensionProperties *ext_properties = alloca(sizeof(VkExtensionProperties) * num_ext);
		vkEnumerateDeviceExtensionProperties(devices[i], NULL, &num_ext, ext_properties);

		Com_Printf("supported extensions:\n");
		for(int j = 0; j < num_ext; j++) {
			Com_Printf("%s, ", ext_properties[j].extensionName);
			if(!strcmp(ext_properties[j].extensionName, VK_NV_RAY_TRACING_EXTENSION_NAME)) {
				if(picked_device < 0)
					picked_device = i;
			}
		}
		Com_Printf("\n");
	}

	if(picked_device < 0) {
		Com_Error(ERR_FATAL, "could not find any suitable device supporting " VK_NV_RAY_TRACING_EXTENSION_NAME"!");
	}

	Com_Printf("picked device %d\n", picked_device);

	qvk.physical_device = devices[picked_device];

	vkGetPhysicalDeviceMemoryProperties(qvk.physical_device, &qvk.mem_properties);

	/* queue family and create physical device */
	uint32_t num_queue_families = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(qvk.physical_device, &num_queue_families, NULL);
	VkQueueFamilyProperties *queue_families = alloca(sizeof(VkQueueFamilyProperties) * num_queue_families);
	vkGetPhysicalDeviceQueueFamilyProperties(qvk.physical_device, &num_queue_families, queue_families);

	Com_Printf("num queue families: %d\n", num_queue_families);

	qvk.queue_idx_graphics = -1;
	qvk.queue_idx_compute  = -1;
	qvk.queue_idx_transfer = -1;

	for(int i = 0; i < num_queue_families; i++) {
		if(!queue_families[i].queueCount)
			continue;
		VkBool32 present_support = 0;
		vkGetPhysicalDeviceSurfaceSupportKHR(qvk.physical_device, i, qvk.surface, &present_support);
		if(!present_support)
			continue;
		if((queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == VK_QUEUE_GRAPHICS_BIT && qvk.queue_idx_graphics < 0) {
			qvk.queue_idx_graphics = i;
		}
		if((queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) == VK_QUEUE_COMPUTE_BIT && qvk.queue_idx_compute < 0) {
			qvk.queue_idx_compute = i;
		}
		if((queue_families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) == VK_QUEUE_TRANSFER_BIT && qvk.queue_idx_transfer < 0) {
			qvk.queue_idx_transfer = i;
		}
	}

	if(qvk.queue_idx_graphics < 0 || qvk.queue_idx_compute < 0 || qvk.queue_idx_transfer < 0) {
		Com_Error(ERR_FATAL, "error: could not find suitable queue family!\n");
		return 1;
	}

	float queue_priorities = 1.0f;
	int num_create_queues = 0;
	VkDeviceQueueCreateInfo queue_create_info[3];

	{
		VkDeviceQueueCreateInfo q = {
			.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueCount       = 1,
			.pQueuePriorities = &queue_priorities,
			.queueFamilyIndex = qvk.queue_idx_graphics,
		};

		queue_create_info[num_create_queues++] = q;
	};
	if(qvk.queue_idx_compute != qvk.queue_idx_graphics) {
		VkDeviceQueueCreateInfo q = {
			.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueCount       = 1,
			.pQueuePriorities = &queue_priorities,
			.queueFamilyIndex = qvk.queue_idx_compute,
		};
		queue_create_info[num_create_queues++] = q;
	};
	if(qvk.queue_idx_transfer != qvk.queue_idx_graphics && qvk.queue_idx_transfer != qvk.queue_idx_compute) {
		VkDeviceQueueCreateInfo q = {
			.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueCount       = 1,
			.pQueuePriorities = &queue_priorities,
			.queueFamilyIndex = qvk.queue_idx_transfer,
		};
		queue_create_info[num_create_queues++] = q;
	};

	VkPhysicalDeviceDescriptorIndexingFeaturesEXT idx_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT,
		.runtimeDescriptorArray = 1,
		.shaderSampledImageArrayNonUniformIndexing = 1,
	};
	VkPhysicalDeviceFeatures2 device_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR,
		.pNext = &idx_features,

		.features = {
			.robustBufferAccess = 1,
			.fullDrawIndexUint32 = 1,
			.imageCubeArray = 1,
			.independentBlend = 1,
			.geometryShader = 1,
			.tessellationShader = 1,
			.sampleRateShading = 0,
			.dualSrcBlend = 1,
			.logicOp = 1,
			.multiDrawIndirect = 1,
			.drawIndirectFirstInstance = 1,
			.depthClamp = 1,
			.depthBiasClamp = 1,
			.fillModeNonSolid = 0,
			.depthBounds = 1,
			.wideLines = 0,
			.largePoints = 0,
			.alphaToOne = 1,
			.multiViewport = 0,
			.samplerAnisotropy = 1,
			.textureCompressionETC2 = 0,
			.textureCompressionASTC_LDR = 0,
			.textureCompressionBC = 0,
			.occlusionQueryPrecise = 0,
			.pipelineStatisticsQuery = 1,
			.vertexPipelineStoresAndAtomics = 1,
			.fragmentStoresAndAtomics = 1,
			.shaderTessellationAndGeometryPointSize = 1,
			.shaderImageGatherExtended = 1,
			.shaderStorageImageExtendedFormats = 1,
			.shaderStorageImageMultisample = 1,
			.shaderStorageImageReadWithoutFormat = 1,
			.shaderStorageImageWriteWithoutFormat = 1,
			.shaderUniformBufferArrayDynamicIndexing = 1,
			.shaderSampledImageArrayDynamicIndexing = 1,
			.shaderStorageBufferArrayDynamicIndexing = 1,
			.shaderStorageImageArrayDynamicIndexing = 1,
			.shaderClipDistance = 1,
			.shaderCullDistance = 1,
			.shaderFloat64 = 1,
			.shaderInt64 = 1,
			.shaderInt16 = 1,
			.shaderResourceResidency = 1,
			.shaderResourceMinLod = 1,
			.sparseBinding = 1,
			.sparseResidencyBuffer = 1,
			.sparseResidencyImage2D = 1,
			.sparseResidencyImage3D = 1,
			.sparseResidency2Samples = 1,
			.sparseResidency4Samples = 1,
			.sparseResidency8Samples = 1,
			.sparseResidency16Samples = 1,
			.sparseResidencyAliased = 1,
			.variableMultisampleRate = 0,
			.inheritedQueries = 1,
		}
	};
	VkDeviceCreateInfo dev_create_info = {
		.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext                   = &device_features,
		.pQueueCreateInfos       = queue_create_info,
		.queueCreateInfoCount    = num_create_queues,
		.enabledExtensionCount   = LENGTH(vk_requested_device_extensions),
		.ppEnabledExtensionNames = vk_requested_device_extensions,
	};

	/* create device and queue */
	_VK(vkCreateDevice(qvk.physical_device, &dev_create_info, NULL, &qvk.device));

	vkGetDeviceQueue(qvk.device, qvk.queue_idx_graphics, 0, &qvk.queue_graphics);
	vkGetDeviceQueue(qvk.device, qvk.queue_idx_compute,  0, &qvk.queue_compute);
	vkGetDeviceQueue(qvk.device, qvk.queue_idx_transfer, 0, &qvk.queue_transfer);

#define _VK_EXTENSION_DO(a) \
		q##a = (PFN_##a) vkGetDeviceProcAddr(qvk.device, #a); \
		if(!q##a) { Com_EPrintf("warning: could not load function %s\n", #a); }
	_VK_EXTENSION_LIST
#undef _VK_EXTENSION_DO

	return 0;
}

qboolean
load_file(const char *path, char **data, size_t *s)
{
	*data = NULL;
	FILE *f = fopen(path, "rb");
	if(!f) {
		//Com_EPrintf("could not open %s\n", path);
		Com_Error(ERR_FATAL, "could not open %s\n", path);
		/* let's try not to crash everything */
		char *ret = malloc(1);
		*s = 1;
		ret[0] = 0;
		return qfalse;
	}
	fseek(f, 0, SEEK_END);
	*s = ftell(f);
	rewind(f);

	*data = malloc(*s + 1);
	//*data = aligned_alloc(4, *s + 1); // XXX lets hope malloc returns aligned memory
	if(fread(*data, 1, *s, f) != *s) {
		//Com_EPrintf("could not read file %s\n", path);
		Com_Error(ERR_FATAL, "could not read file %s\n", path);
		fclose(f);
		*data[0] = 0;
		return qfalse;
	}
	fclose(f);
	return qtrue;
}

static VkShaderModule
create_shader_module_from_file(const char *name, const char *enum_name)
{
	char *data;
	size_t size;

	char path[1024];
	snprintf(path, sizeof path, SHADER_PATH_TEMPLATE, name ? name : (enum_name + 8));
	if(!name) {
		int len = 0;
		for(len = 0; path[len]; len++)
			path[len] = tolower(path[len]);
		while(--len >= 0) {
			if(path[len] == '_') {
				path[len] = '.';
				break;
			}
		}
	}

	if(!load_file(path, &data, &size)) {
		free(data);
		return VK_NULL_HANDLE;
	}

	VkShaderModule module;

	VkShaderModuleCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = (uint32_t *) data,
	};

	_VK(vkCreateShaderModule(qvk.device, &create_info, NULL, &module));

	free(data);

	return module;
}

VkResult
vkpt_load_shader_modules()
{
	VkResult ret = VK_SUCCESS;
#define SHADER_MODULE_DO(a) do { \
	qvk.shader_modules[a] = create_shader_module_from_file(shader_module_file_names[a], #a); \
	ret = (ret == VK_SUCCESS && qvk.shader_modules[a]) ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED; \
	if(qvk.shader_modules[a]) { \
		ATTACH_LABEL_VARIABLE_NAME((uint64_t)qvk.shader_modules[a], SHADER_MODULE, #a); \
	}\
	} while(0);

	LIST_SHADER_MODULES

#undef SHADER_MODULE_DO
	return ret;
}

VkResult
vkpt_destroy_shader_modules()
{
#define SHADER_MODULE_DO(a) \
	vkDestroyShaderModule(qvk.device, qvk.shader_modules[a], NULL); \
	qvk.shader_modules[a] = VK_NULL_HANDLE;

	LIST_SHADER_MODULES

#undef SHADER_MODULE_DO

	return VK_SUCCESS;
}

VkResult
destroy_swapchain()
{
	for(int i = 0; i < qvk.num_swap_chain_images; i++) {
		vkDestroyImageView  (qvk.device, qvk.swap_chain_image_views[i], NULL);
	}

	vkDestroySwapchainKHR(qvk.device,   qvk.swap_chain, NULL);
	return VK_SUCCESS;
}

int
destroy_vulkan()
{
	vkDeviceWaitIdle(qvk.device);

	destroy_swapchain();
	vkDestroySurfaceKHR  (qvk.instance, qvk.surface,    NULL);

	for(int i = 0; i < NUM_SEMAPHORES; i++) {
		vkDestroySemaphore(qvk.device, qvk.semaphores[i], NULL);
	}

	for(int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		vkDestroyFence(qvk.device, qvk.fences_frame_sync[i], NULL);
	}

	vkDestroyCommandPool   (qvk.device, qvk.command_pool,     NULL);

	vkDestroyDevice      (qvk.device,   NULL);
	_VK(qvkDestroyDebugUtilsMessengerEXT(qvk.instance, qvk.dbg_messenger, NULL));
	vkDestroyInstance    (qvk.instance, NULL);

	free(qvk.extensions);
	qvk.extensions = NULL;
	qvk.num_extensions = 0;

	free(qvk.layers);
	qvk.layers = NULL;
	qvk.num_layers = 0;

	return 0;
}

static int
get_model_flags(const char *name)
{
	const char *light_sources[] = {
		"models/objects/explode/tris.md2",
		"models/objects/flash/tris.md2",
		"models/objects/r_explode/tris.md2",
		"models/objects/laser/tris.md2",
		"models/objects/rocket/tris.md2",
		//"models/weapons/v_blast/tris.md2", /* make blaster a flash light */
	};

	for(int i = 0; i < LENGTH(light_sources); i++) {
		if(!strcmp(name, light_sources[i]))
			return BSP_FLAG_LIGHT;
	}
	return 0;
}

inline int
is_light(int mat)
{
	return (mat & (BSP_FLAG_LIGHT | BSP_FLAG_WATER)) == BSP_FLAG_LIGHT;
}

static void
upload_entity_transforms(uint32_t *num_instances, uint32_t *num_vertices)
{
	static int entity_frame_num = 0;
	static int model_entity_ids[2][MAX_ENTITIES];
	static int world_entity_ids[2][MAX_ENTITIES];
	static int model_entity_id_count[2];
	static int world_entity_id_count[2];


	entity_frame_num = !entity_frame_num;

	QVKUniformBuffer_t *ubo = &vkpt_refdef.uniform_buffer;
	ubo->num_lights = 0;
	ubo->num_instances_model_bsp = 0;
	memcpy(ubo->bsp_mesh_instances_prev,
			ubo->bsp_mesh_instances,
			sizeof(ubo->bsp_mesh_instances_prev));
	memcpy(ubo->model_instances_prev,
			ubo->model_instances,
			sizeof(ubo->model_instances_prev));
	int model_instance_idx = 0;
	int bsp_mesh_idx = 0;
	int num_instanced_vert = 0; /* need to track this here to find lights */

	static uvec4_t bsp_cluster_id_prev[SHADER_MAX_BSP_ENTITIES / 4];
	static uvec4_t model_cluster_id_prev[SHADER_MAX_ENTITIES / 4];

	memcpy(bsp_cluster_id_prev,   ubo->bsp_cluster_id,   sizeof(ubo->bsp_cluster_id));
	memcpy(model_cluster_id_prev, ubo->model_cluster_id, sizeof(ubo->model_cluster_id));

	int instance_idx = 0;
	for(int i = 0; i < vkpt_refdef.fd->num_entities; i++) {
		entity_t *e = vkpt_refdef.fd->entities + i;

		float M[16];
		create_entity_matrix(M, e);

		/* embedded in bsp */
		if (e->model & 0x80000000) {
			assert(bsp_mesh_idx < SHADER_MAX_BSP_ENTITIES);

			/* update cluster index */
			float pos_center_orig[4];
			float pos_center_trans[4];
			memcpy(pos_center_orig, vkpt_refdef.bsp_mesh_world.model_centers[~e->model], sizeof(float) * 3);
			pos_center_orig[3] = 1.0;
			mult_matrix_vector(pos_center_trans, M, pos_center_orig);
			uint32_t cluster_id = BSP_PointLeaf(bsp_world_model->nodes, pos_center_trans)->cluster;

			memcpy(&ubo->bsp_mesh_instances[bsp_mesh_idx].M, M, sizeof(M));
			int idx = ~e->model;
			int mesh_vert_cnt = vkpt_refdef.bsp_mesh_world.models_idx_count[idx];
			ubo->bsp_prim_offset[bsp_mesh_idx / 4][bsp_mesh_idx % 4] /* insanity due to alignment :( */
				= vkpt_refdef.bsp_mesh_world.models_idx_offset[~e->model] / 3;
			ubo->bsp_cluster_id[bsp_mesh_idx / 4][bsp_mesh_idx % 4] = cluster_id;

			world_entity_ids[entity_frame_num][bsp_mesh_idx] = e->id;

			bsp_mesh_idx++;

			ubo->instance_buf_offset[instance_idx / 4][instance_idx % 4] = num_instanced_vert / 3;
			num_instanced_vert += mesh_vert_cnt;

			ubo->num_instances_model_bsp += 1 << 0;
			instance_idx++;
		}
	}
	for(int i = 0; i < vkpt_refdef.fd->num_entities; i++) {
		entity_t *e = vkpt_refdef.fd->entities + i;

		float M[16];
		create_entity_matrix(M, e);

		model_t *model = NULL;
		if(!(e->model & 0x80000000) && (model = MOD_ForHandle(e->model))) {
			maliasmesh_t *mesh = &model->meshes[0];
			if(!model->meshes) {
				continue;
			}
			image_t *img = mesh->skins[0];
			for(int s = 0; s < mesh->numskins; s++) {
				if((img = mesh->skins[s]))
					break;
			}

			model_entity_ids[entity_frame_num][model_instance_idx] = e->id;

			ModelInstance_t *mi = &vkpt_refdef.uniform_buffer.model_instances[model_instance_idx];
			memcpy(mi->M, M, sizeof(float) * 16);
			mi->offset_curr = mesh->vertex_offset + e->frame    * mesh->numverts;
			mi->offset_prev = mesh->vertex_offset + e->oldframe * mesh->numverts;
			mi->backlerp  = e->backlerp;
			mi->material  = img ? (int)(img - r_images) : ~0;
			mi->material |= get_model_flags(model->name);

			 /* insanity due to alignment :( */
			uint32_t cluster_id = BSP_PointLeaf(bsp_world_model->nodes, e->origin)->cluster;
			ubo->model_idx_offset[model_instance_idx / 4][model_instance_idx % 4] = mesh->idx_offset;
			ubo->model_cluster_id[model_instance_idx / 4][model_instance_idx % 4] = cluster_id;

			uint32_t mat_flags = get_model_flags(model->name);
			if(is_light(mat_flags)) {
				ubo->light_offset_cnt[ubo->num_lights / 2][0 + (ubo->num_lights % 2) * 2] = num_instanced_vert / 3;
				ubo->light_offset_cnt[ubo->num_lights / 2][1 + (ubo->num_lights % 2) * 2] = mesh->numtris;
				ubo->num_lights++;
			}
			else if((e->flags & RF_SHELL_MASK)) { /* quad damage */
				ubo->light_offset_cnt[ubo->num_lights / 2][0 + (ubo->num_lights % 2) * 2] = (1 << 31) | (num_instanced_vert / 3);
				ubo->light_offset_cnt[ubo->num_lights / 2][1 + (ubo->num_lights % 2) * 2] = mesh->numtris;
				ubo->num_lights++;
			}

			ubo->instance_buf_offset[instance_idx / 4][instance_idx % 4] = num_instanced_vert / 3;

			ubo->num_instances_model_bsp += 1 << 16;
			instance_idx++;
			model_instance_idx++;
			num_instanced_vert += mesh->numtris * 3;
		}
	}

	/* anchor for last element */
	ubo->instance_buf_offset[instance_idx / 4][instance_idx % 4] = num_instanced_vert / 3;

	memset(ubo->world_current_to_prev, ~0u, sizeof(ubo->world_current_to_prev));
	memset(ubo->world_prev_to_current, ~0u, sizeof(ubo->world_prev_to_current));
	memset(ubo->model_current_to_prev, ~0u, sizeof(ubo->model_current_to_prev));
	memset(ubo->model_prev_to_current, ~0u, sizeof(ubo->model_prev_to_current));

	world_entity_id_count[entity_frame_num] = bsp_mesh_idx;
	uint32_t *world_current_to_prev = &ubo->world_current_to_prev[0][0];
	uint32_t *world_prev_to_current = &ubo->world_prev_to_current[0][0];
	for(int i = 0; i < world_entity_id_count[entity_frame_num]; i++) {
		for(int j = 0; j < world_entity_id_count[!entity_frame_num]; j++) {
			if(world_entity_ids[entity_frame_num][i] == world_entity_ids[!entity_frame_num][j]) {
				world_current_to_prev[i] = j;
				world_prev_to_current[j] = i;
			}
		}
	}

	model_entity_id_count[entity_frame_num] = model_instance_idx;
	uint32_t *model_current_to_prev = &ubo->model_current_to_prev[0][0];
	uint32_t *model_prev_to_current = &ubo->model_prev_to_current[0][0];
	for(int i = 0; i < model_entity_id_count[entity_frame_num]; i++) {
		for(int j = 0; j < model_entity_id_count[!entity_frame_num]; j++) {
			if(model_entity_ids[entity_frame_num][i] == model_entity_ids[!entity_frame_num][j]) {
				model_current_to_prev[i] = j;
				model_prev_to_current[j] = i;
			}
		}
	}

	*num_instances = instance_idx;
	*num_vertices  = num_instanced_vert;

	for(int i = 0; i < world_entity_id_count[entity_frame_num]; i++) {
		if(ubo->bsp_cluster_id[i / 4][i % 4] == ~0u) {
			uint32_t id_prev = world_current_to_prev[i];
			if(id_prev == ~0u)
				continue;
			ubo->bsp_cluster_id[i / 4][i % 4] = bsp_cluster_id_prev[id_prev / 4][id_prev % 4];
		}
	}

	for(int i = 0; i < model_entity_id_count[entity_frame_num]; i++) {
		if(ubo->model_cluster_id[i / 4][i % 4] == ~0u) {
			uint32_t id_prev = model_current_to_prev[i];
			if(id_prev == ~0u)
				continue;
			ubo->model_cluster_id[i / 4][i % 4] = model_cluster_id_prev[id_prev / 4][id_prev % 4];
		}
	}
}

#if 0
/* code for updating the light hierarchy, potentially buggy */
void
update_lights()
{
	vkpt_refdef.num_dynamic_lights = 0;

	bsp_mesh_t *bsp = &vkpt_refdef.bsp_mesh_world;

	float *light_pos = vkpt_refdef.light_positions
		+ vkpt_refdef.num_static_lights * 9;
	uint32_t *light_col = vkpt_refdef.light_colors
		+ vkpt_refdef.num_static_lights;

	int num_lights = 0;

	for(int i = 0; i < vkpt_refdef.fd->num_entities; i++) {
		model_t *model = NULL;
		entity_t *e = vkpt_refdef.fd->entities + i;
		float M[16];
		create_entity_matrix(M, e);

		/* embedded in bsp */
		if(e->model & 0x80000000) {
			int idx_off = bsp->models_idx_offset[~e->model];
			int ent_is_light = 0;
			for(int j = 0; j < bsp->models_idx_count[~e->model] / 3; j++) { // per prim
				if(is_light(bsp->materials[idx_off / 3 + j])) {
					ent_is_light |= 1;
					for(int k = 0; k < 3; k++) {
						float tmp[4];
						memcpy(tmp, bsp->positions + (idx_off + j * 3 + k) * 3, 3 * sizeof(float));
						tmp[3] = 1.0;
						mult_matrix_vector(light_pos, M, tmp);
						light_pos += 3;
					}
					vkpt_refdef.num_dynamic_lights++;
					*light_col++ = ~0u; // fixme: actually add proper color
				}
			}
			if(ent_is_light)
				num_lights++;
		}
		else if((model = MOD_ForHandle(e->model))) {
			maliasmesh_t *mesh = &model->meshes[0];
			if(!model->meshes) {
				continue;
			}
			image_t *img = mesh->skins[0];
			for(int s = 0; s < mesh->numskins; s++) {
				if((img = mesh->skins[s]))
					break;
			}
			uint32_t mat_flags = get_model_flags(model->name);
			if(!is_light(mat_flags))
				continue;

			num_lights++;
			//Com_Printf("num light tri %d\n", mesh->numtris);

			int   vert_off_curr = e->frame    * mesh->numverts;
			int   vert_off_prev = e->oldframe * mesh->numverts;
			int   idx_cnt       = mesh->numtris * 3;
			float backlerp      = e->backlerp;

			for(int j = 0; j < idx_cnt; j++) {
				int idx = mesh->indices[j];

				float pos[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
				pos[0] += mesh->positions[idx + vert_off_curr][0] * (1.0f - backlerp);
				pos[1] += mesh->positions[idx + vert_off_curr][1] * (1.0f - backlerp);
				pos[2] += mesh->positions[idx + vert_off_curr][2] * (1.0f - backlerp);

				pos[0] += mesh->positions[idx + vert_off_prev][0] * (backlerp);
				pos[1] += mesh->positions[idx + vert_off_prev][1] * (backlerp);
				pos[2] += mesh->positions[idx + vert_off_prev][2] * (backlerp);

				mult_matrix_vector(light_pos, M, pos);
				light_pos += 3;
			}
			for(int j = 0; j < idx_cnt / 3; j++) {
				*light_col++ = 0x000045FF; // fixme: actually add proper color
			}
			vkpt_refdef.num_dynamic_lights += idx_cnt / 3;
		}
	}

	//Com_Printf("num lights: %d\n", num_lights);

#if 0
	static int _cnt = 0;
	char buf[1024];
	snprintf(buf, sizeof buf, "/tmp/light%04d.obj", _cnt++);
	FILE *f = fopen(buf, "wb+");
	for(int i = 0; i < vkpt_refdef.num_static_lights + vkpt_refdef.num_dynamic_lights;
			i++) {

		fprintf(f, "v %f %f %f\n",
				vkpt_refdef.light_positions[i * 9 + 0 + 0],
				vkpt_refdef.light_positions[i * 9 + 0 + 1],
				vkpt_refdef.light_positions[i * 9 + 0 + 2]);
		fprintf(f, "v %f %f %f\n",
				vkpt_refdef.light_positions[i * 9 + 3 + 0],
				vkpt_refdef.light_positions[i * 9 + 3 + 1],
				vkpt_refdef.light_positions[i * 9 + 3 + 2]);
		fprintf(f, "v %f %f %f\n",
				vkpt_refdef.light_positions[i * 9 + 6 + 0],
				vkpt_refdef.light_positions[i * 9 + 6 + 1],
				vkpt_refdef.light_positions[i * 9 + 6 + 2]);
		fprintf(f, "f -1 -2 -3\n");
	}
	fclose(f);
#endif

	vkpt_lh_update(
			vkpt_refdef.light_positions,
			vkpt_refdef.light_colors,
			vkpt_refdef.num_static_lights,
			//vkpt_refdef.num_static_lights + vkpt_refdef.num_dynamic_lights,
			qvk.cmd_buf_current);

}
#endif

static int
get_output_img()
{
	if(!strcmp(cvar_rtx->string, "on")) {
		return VKPT_IMG_PT_COLOR_A + (qvk.frame_counter & 1);
	}

	switch(vkpt_reconstruction->integer) {
	default:
	case 0: return VKPT_IMG_PT_COLOR_A + (qvk.frame_counter & 1); break;
	case 1: return VKPT_IMG_ASVGF_TAA_A + (qvk.frame_counter & 1); break;
	case 2: return VKPT_IMG_DEBUG; break;
	case 3: return VKPT_IMG_ASVGF_GRAD_B; break;
	}
}

 
/* renders the map ingame */
void
R_RenderFrame(refdef_t *fd)
{
	vkpt_refdef.fd = fd;
	LOG_FUNC();
	if(!vkpt_refdef.bsp_mesh_world_loaded)
		return;

	//update_lights(); /* updates the light hierarchy, not present in this version */

	uint32_t num_vert_instanced;
	uint32_t num_instances;
	upload_entity_transforms(&num_instances, &num_vert_instanced);

	float P[16];
	float V[16];
	float VP[16];
	float inv_VP[16];

	QVKUniformBuffer_t *ubo = &vkpt_refdef.uniform_buffer;
	memcpy(ubo->VP_prev, ubo->VP, sizeof(float) * 16);
	create_projection_matrix(P, vkpt_refdef.z_near, vkpt_refdef.z_far, fd->fov_x, fd->fov_y);
	create_view_matrix(V, fd);
	mult_matrix_matrix(VP, P, V);
	memcpy(ubo->V, V, sizeof(float) * 16);
	memcpy(ubo->VP, VP, sizeof(float) * 16);
	inverse(VP, inv_VP);
	memcpy(ubo->invVP, inv_VP, sizeof(float) * 16);
	ubo->current_frame_idx = qvk.frame_counter;
	ubo->width  = qvk.extent.width;
	ubo->height = qvk.extent.height;
	ubo->under_water = !!(fd->rdflags & RDF_UNDERWATER);
	ubo->time = fd->time;
	memcpy(ubo->cam_pos, fd->vieworg, sizeof(float) * 3);

	_VK(vkpt_uniform_buffer_update());

	_VK(vkpt_profiler_query(PROFILER_INSTANCE_GEOMETRY, PROFILER_START));
	vkpt_vertex_buffer_create_instance(num_instances);
	_VK(vkpt_profiler_query(PROFILER_INSTANCE_GEOMETRY, PROFILER_STOP));

	_VK(vkpt_profiler_query(PROFILER_BVH_UPDATE, PROFILER_START));
	vkpt_pt_destroy_dynamic(qvk.current_image_index);

	assert(num_vert_instanced % 3 == 0);
	vkpt_pt_create_dynamic(qvk.current_image_index, qvk.buf_vertex.buffer,
		offsetof(VertexBuffer, positions_instanced), num_vert_instanced); 

	vkpt_pt_create_toplevel(qvk.current_image_index);
	vkpt_pt_update_descripter_set_bindings(qvk.current_image_index);
	_VK(vkpt_profiler_query(PROFILER_BVH_UPDATE, PROFILER_STOP));

	_VK(vkpt_profiler_query(PROFILER_ASVGF_GRADIENT_SAMPLES, PROFILER_START));
	vkpt_asvgf_create_gradient_samples(qvk.cmd_buf_current, qvk.frame_counter);
	_VK(vkpt_profiler_query(PROFILER_ASVGF_GRADIENT_SAMPLES, PROFILER_STOP));

	_VK(vkpt_profiler_query(PROFILER_PATH_TRACER, PROFILER_START));
	vkpt_pt_record_cmd_buffer(qvk.cmd_buf_current, qvk.frame_counter);
	_VK(vkpt_profiler_query(PROFILER_PATH_TRACER, PROFILER_STOP));

	_VK(vkpt_profiler_query(PROFILER_ASVGF_FULL, PROFILER_START));
	vkpt_asvgf_record_cmd_buffer(qvk.cmd_buf_current);
	_VK(vkpt_profiler_query(PROFILER_ASVGF_FULL, PROFILER_STOP));

	VkImageSubresourceRange subresource_range = {
		.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel   = 0,
		.levelCount     = 1,
		.baseArrayLayer = 0,
		.layerCount     = 1
	};

	IMAGE_BARRIER(qvk.cmd_buf_current,
			.image            = qvk.swap_chain_images[qvk.current_image_index],
			.subresourceRange = subresource_range,
			.srcAccessMask    = 0,
			.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
			.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
	);

	int output_img = get_output_img();

	IMAGE_BARRIER(qvk.cmd_buf_current,
			.image            = qvk.images[output_img],
			.subresourceRange = subresource_range,
			.srcAccessMask    = VK_ACCESS_SHADER_WRITE_BIT,
			.dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT,
			.oldLayout        = VK_IMAGE_LAYOUT_GENERAL,
			.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
	);

	VkOffset3D blit_size = {
		.x = qvk.extent.width, .y = qvk.extent.height, .z = 1
	};
	VkImageBlit img_blit = {
		.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
		.srcOffsets = { [1] = blit_size },
		.dstOffsets = { [1] = blit_size },
	};
	vkCmdBlitImage(qvk.cmd_buf_current,
			qvk.images[output_img],                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
			qvk.swap_chain_images[qvk.current_image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &img_blit, VK_FILTER_NEAREST);

	IMAGE_BARRIER(qvk.cmd_buf_current,
			.image            = qvk.swap_chain_images[qvk.current_image_index],
			.subresourceRange = subresource_range,
			.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT,
			.dstAccessMask    = 0,
			.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			.newLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
	);
}

static void
recreate_swapchain()
{
	vkDeviceWaitIdle(qvk.device);
	vkpt_destroy_all(VKPT_INIT_SWAPCHAIN_RECREATE);
	destroy_swapchain();
	SDL_GetWindowSize(qvk.window, &qvk.win_width, &qvk.win_height);
	create_swapchain();
	vkpt_initialize_all(VKPT_INIT_SWAPCHAIN_RECREATE);
}

void
R_BeginFrame()
{
	LOG_FUNC();
retry:;
	int sem_idx = qvk.frame_counter % MAX_FRAMES_IN_FLIGHT;

	vkWaitForFences(qvk.device, 1, qvk.fences_frame_sync + sem_idx, VK_TRUE, ~((uint64_t) 0));
	VkResult res_swapchain = vkAcquireNextImageKHR(qvk.device, qvk.swap_chain, ~((uint64_t) 0),
			qvk.semaphores[SEM_IMG_AVAILABLE + sem_idx], VK_NULL_HANDLE, &qvk.current_image_index);
	if(res_swapchain == VK_ERROR_OUT_OF_DATE_KHR || res_swapchain == VK_SUBOPTIMAL_KHR) {
		recreate_swapchain();
		goto retry;
	}
	else if(res_swapchain != VK_SUCCESS) {
		_VK(res_swapchain);
	}
	vkResetFences(qvk.device, 1, qvk.fences_frame_sync + sem_idx);

	_VK(vkpt_profiler_next_frame(qvk.current_image_index));

	/* cannot be called in R_EndRegistration as it would miss the initially textures (charset etc) */
	if(register_model_dirty) {
		_VK(vkpt_vertex_buffer_upload_models_to_staging());
		_VK(vkpt_vertex_buffer_upload_staging());
		register_model_dirty = 0;
	}
	vkpt_textures_end_registration();
	vkpt_draw_clear_stretch_pics();

	qvk.cmd_buf_current = qvk.command_buffers[qvk.current_image_index];

	VkCommandBufferBeginInfo begin_info = {
		.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags            = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
		.pInheritanceInfo = NULL,
	};

	_VK(vkResetCommandBuffer(qvk.cmd_buf_current, 0));
	_VK(vkBeginCommandBuffer(qvk.cmd_buf_current, &begin_info));
	_VK(vkpt_profiler_query(PROFILER_FRAME_TIME, PROFILER_START));
}

void
R_EndFrame()
{
	LOG_FUNC();

	if(vkpt_profiler->integer)
		draw_profiler();
	vkpt_draw_submit_stretch_pics(&qvk.cmd_buf_current);
	_VK(vkpt_profiler_query(PROFILER_FRAME_TIME, PROFILER_STOP));
	_VK(vkEndCommandBuffer(qvk.cmd_buf_current));

	int sem_idx = qvk.frame_counter % MAX_FRAMES_IN_FLIGHT;

	VkSemaphore          wait_semaphores[]   = { qvk.semaphores[SEM_IMG_AVAILABLE + sem_idx]    };
	VkPipelineStageFlags wait_stages[]       = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT  };
	VkSemaphore          signal_semaphores[] = { qvk.semaphores[SEM_RENDER_FINISHED + sem_idx]  };

	VkSubmitInfo submit_info = {
		.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount   = LENGTH(wait_semaphores),
		.pWaitSemaphores      = wait_semaphores,
		.signalSemaphoreCount = LENGTH(signal_semaphores),
		.pSignalSemaphores    = signal_semaphores,
		.pWaitDstStageMask    = wait_stages,
		.commandBufferCount   = 1,
		.pCommandBuffers      = &qvk.cmd_buf_current,
	};

	_VK(vkQueueSubmit(qvk.queue_graphics, 1, &submit_info, qvk.fences_frame_sync[sem_idx]));

	VkPresentInfoKHR present_info = {
		.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = LENGTH(signal_semaphores),
		.pWaitSemaphores    = signal_semaphores,
		.swapchainCount     = 1,
		.pSwapchains        = &qvk.swap_chain,
		.pImageIndices      = &qvk.current_image_index,
		.pResults           = NULL,
	};

	VkResult res_present = vkQueuePresentKHR(qvk.queue_graphics, &present_info);
	if(res_present == VK_ERROR_OUT_OF_DATE_KHR || res_present == VK_SUBOPTIMAL_KHR) {
		recreate_swapchain();
	}
	qvk.frame_counter++;
}

void
R_ModeChanged(int width, int height, int flags, int rowbytes, void *pixels)
{
	Com_Printf("mode changed %d %d\n", width, height);

	r_config.width  = width;
	r_config.height = height;
	r_config.flags  = flags;
}

float
R_ClampScale(cvar_t *var)
{
	if(!var)
		return 1.0f;

	if(var->value)
		return 1.0f / Cvar_ClampValue(var, 1.0f, 10.0f);

	if(r_config.width * r_config.height >= 2560 * 1440)
		return 0.25f;

	if(r_config.width * r_config.height >= 1280 * 720)
		return 0.5f;

	return 1.0f;
}

/* called when the library is loaded */
qboolean
R_Init(qboolean total)
{
	registration_sequence = 1;
	qvk.window = SDL_CreateWindow("vkq2", 20, 50, r_config.width, r_config.height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	if(!qvk.window) {
		Com_Error(ERR_FATAL, "[vkq2] could not create window `%s'\n", SDL_GetError());
		return qfalse;
	}
	extern SDL_Window *sdl_window;
	sdl_window = qvk.window;

	if (!VID_Init()) {
		Com_Error(ERR_FATAL, "[vkq2] VID_Init failed\n");
		return qfalse;
	}

	vkpt_profiler       = Cvar_Get("vkpt_profiler",       "0",    0);
	vkpt_reconstruction = Cvar_Get("vkpt_reconstruction", "1",    0);
	cvar_rtx            = Cvar_Get("rtx",                 "off",  0);

	qvk.win_width  = r_config.width;
	qvk.win_height = r_config.height;

	if(!SDL_Vulkan_GetInstanceExtensions(qvk.window, &qvk.num_sdl2_extensions, NULL)) {
		Com_Error(ERR_FATAL, "[vkq2] couldn't get extension count\n");
		return qfalse;
	}
	qvk.sdl2_extensions = malloc(sizeof(char*) * qvk.num_sdl2_extensions);
	if(!SDL_Vulkan_GetInstanceExtensions(qvk.window, &qvk.num_sdl2_extensions, qvk.sdl2_extensions)) {
		Com_Error(ERR_FATAL, "[vkq2] couldn't get extensions\n");
		return qfalse;
	}

	Com_Printf("[vkq2] vk extension required by SDL2: \n");
	for(int i = 0; i < qvk.num_sdl2_extensions; i++) {
		Com_Printf("  %s\n", qvk.sdl2_extensions[i]);
	}

	IMG_Init();
	IMG_GetPalette();
	MOD_Init();

	vkpt_refdef.light_positions = calloc(MAX_LIGHTS * 3 * 3, sizeof(float));
	vkpt_refdef.light_colors    = calloc(MAX_LIGHTS, sizeof(uint32_t));

	if(init_vulkan()) {
		Com_Error(ERR_FATAL, "[vkq2] init vulkan failed\n");
		return qfalse;
	}
	_VK(create_swapchain());
	_VK(create_command_pool_and_fences());

	_VK(vkpt_initialize_all(VKPT_INIT_DEFAULT));

	Cmd_AddCommand("reload_shader", (xcommand_t)&vkpt_reload_shader);

	return qtrue;
}

/* called before the library is unloaded */
void
R_Shutdown(qboolean total)
{
	_VK(vkpt_destroy_all(VKPT_INIT_DEFAULT));

	if(destroy_vulkan()) {
		Com_EPrintf("[vkpt] destroy vulkan failed\n");
	}

	IMG_Shutdown();
	MOD_Shutdown(); // todo: currently leaks memory, need to clear submeshes
	VID_Shutdown();
}

// for screenshots
byte *
IMG_ReadPixels(int *width, int *height, int *rowbytes)
{
	return 0; // sorry guys
}

void
R_SetSky(const char *name, float rotate, vec3_t axis)
{
	int     i;
	char    pathname[MAX_QPATH];
	// 3dstudio environment map names
	const char *suf[6] = { "ft", "bk", "up", "dn", "rt", "lf" };

	byte *data = NULL;

	int w_prev, h_prev;
	for (i = 0; i < 6; i++) {
		Q_concat(pathname, sizeof(pathname), "env/", name, suf[i], ".tga", NULL);
		FS_NormalizePath(pathname, pathname);
		image_t img;
		qerror_t ret = load_img(pathname, &img);
		if(ret != Q_ERR_SUCCESS) {
			if(data) {
				Z_Free(data);
			}
			data = Z_Malloc(6 * sizeof(uint32_t));
			for(int j = 0; j < 6; j++)
				((uint32_t *)data)[j] = 0xff00ffffu;
			w_prev = h_prev = 1;
			break;
		}

		size_t s = img.upload_width * img.upload_height * 4;
		if(!data) {
			data = Z_Malloc(s * 6);
			w_prev = img.upload_width;
			h_prev = img.upload_height;
		}

		memcpy(data + s * i, img.pix_data, s);

		assert(w_prev == img.upload_width);
		assert(h_prev == img.upload_height);
	}

	vkpt_textures_upload_envmap(w_prev, h_prev, data);
	Z_Free(data);
}

void R_AddDecal(decal_t *d)
{ }

void
R_BeginRegistration(const char *name)
{
	registration_sequence++;
	LOG_FUNC();
	Com_Printf("loading %s\n", name);
	vkDeviceWaitIdle(qvk.device);

	if(vkpt_refdef.bsp_mesh_world_loaded) {
		bsp_mesh_destroy(&vkpt_refdef.bsp_mesh_world);
		vkpt_refdef.bsp_mesh_world_loaded = 0;
	}

	if(bsp_world_model) {
		BSP_Free(bsp_world_model);
		bsp_world_model = NULL;
	}

	char bsp_path[MAX_QPATH];
	Q_concat(bsp_path, sizeof(bsp_path), "maps/", name, ".bsp", NULL);
	bsp_t *bsp;
	qerror_t ret = BSP_Load(bsp_path, &bsp);
	if(!bsp) {
		Com_Error(ERR_DROP, "%s: couldn't load %s: %s", __func__, bsp_path, Q_ErrorString(ret));
	}
	bsp_world_model = bsp;
	bsp_mesh_register_textures(bsp);
	bsp_mesh_create_from_bsp(&vkpt_refdef.bsp_mesh_world, bsp);
	_VK(vkpt_vertex_buffer_upload_bsp_mesh_to_staging(&vkpt_refdef.bsp_mesh_world));
	_VK(vkpt_vertex_buffer_upload_staging());
	vkpt_refdef.bsp_mesh_world_loaded = 1;
	bsp = NULL;

	_VK(vkpt_pt_destroy_static());
	const bsp_mesh_t *m = &vkpt_refdef.bsp_mesh_world;
	_VK(vkpt_pt_create_static(qvk.buf_vertex.buffer, offsetof(VertexBuffer, positions_bsp), m->world_idx_count));

	{
		int num_prims = 0;
		for(int i = 0; i < m->world_idx_count / 3; i++) {
			num_prims += !!is_light(m->materials[i]);
		}

		vkpt_refdef.num_static_lights = num_prims;

		for(int i = 0, lh_idx = 0; i < m->world_idx_count / 3; i++) {
			if(!is_light(m->materials[i]))
				continue;

			image_t *img = &r_images[m->materials[i] & BSP_TEXTURE_MASK];
			vkpt_refdef.light_colors[lh_idx] = img->light_color;

			for(int j = 0; j < 3; j++) {
				int bsp_idx  = m->indices[i * 3 + j];
				assert(bsp_idx >= 0 && bsp_idx < m->num_vertices);
				float *p_in  = m->positions + bsp_idx * 3;
				float *p_out = vkpt_refdef.light_positions + (lh_idx * 3 + j) * 3;

				p_out[0] = p_in[0];
				p_out[1] = p_in[1];
				p_out[2] = p_in[2];
			}
			lh_idx++;
		}
	}

}

void
R_EndRegistration(void)
{
	LOG_FUNC();
	IMG_FreeUnused();
	MOD_FreeUnused();
}

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
