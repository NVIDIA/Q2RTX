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

#ifndef  __VKPT_H__
#define  __VKPT_H__

#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#if !defined(HAVE_M_PI)
#define HAVE_M_PI
#endif // !defined(HAVE_M_PI)

#include "vk_util.h"

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

#include "shader/global_ubo.h"
#include "shader/global_textures.h"
#include "shader/vertex_buffer.h"

#define LENGTH(a) ((sizeof (a)) / (sizeof(*(a))))

#define LOG_FUNC_(f) do {} while(0)
//#define LOG_FUNC_(f) Com_Printf("%s\n", f)
#define LOG_FUNC() LOG_FUNC_(__func__)

#ifdef USE_DEBUG
#define _VK(...) \
	do { \
		VkResult _res = __VA_ARGS__; \
		if(_res != VK_SUCCESS) { \
			Com_EPrintf("error %d executing %s! %s:%d\n", _res, # __VA_ARGS__, __FILE__, __LINE__); \
		} \
	} while(0)
#else
#define _VK(...) do { __VA_ARGS__; } while(0)
#endif

/* see main.c to override default file path. By default it will strip away
 * QVK_MOD_, fix the file ending, and convert to lower case */
#define LIST_SHADER_MODULES \
	SHADER_MODULE_DO(QVK_MOD_STRETCH_PIC_VERT)                       \
	SHADER_MODULE_DO(QVK_MOD_STRETCH_PIC_FRAG)                       \
	SHADER_MODULE_DO(QVK_MOD_FINAL_BLIT_FRAG)                        \
	SHADER_MODULE_DO(QVK_MOD_FINAL_BLIT_VERT)                        \
	SHADER_MODULE_DO(QVK_MOD_INSTANCE_GEOMETRY_COMP)                 \
	SHADER_MODULE_DO(QVK_MOD_ANIMATE_MATERIALS_COMP)                 \
	SHADER_MODULE_DO(QVK_MOD_ASVGF_GRADIENT_IMG_COMP)                \
	SHADER_MODULE_DO(QVK_MOD_ASVGF_GRADIENT_ATROUS_COMP)             \
	SHADER_MODULE_DO(QVK_MOD_ASVGF_GRADIENT_REPROJECT_COMP)          \
	SHADER_MODULE_DO(QVK_MOD_ASVGF_ATROUS_COMP)                      \
	SHADER_MODULE_DO(QVK_MOD_ASVGF_LF_COMP)                          \
	SHADER_MODULE_DO(QVK_MOD_ASVGF_TEMPORAL_COMP)                    \
	SHADER_MODULE_DO(QVK_MOD_ASVGF_TAAU_COMP)                        \
	SHADER_MODULE_DO(QVK_MOD_BLOOM_BLUR_COMP)                        \
	SHADER_MODULE_DO(QVK_MOD_BLOOM_COMPOSITE_COMP)                   \
	SHADER_MODULE_DO(QVK_MOD_BLOOM_DOWNSCALE_COMP)                   \
	SHADER_MODULE_DO(QVK_MOD_TONE_MAPPING_HISTOGRAM_COMP)            \
	SHADER_MODULE_DO(QVK_MOD_TONE_MAPPING_CURVE_COMP)                \
	SHADER_MODULE_DO(QVK_MOD_TONE_MAPPING_APPLY_COMP)                \
	SHADER_MODULE_DO(QVK_MOD_PHYSICAL_SKY_COMP)                      \
	SHADER_MODULE_DO(QVK_MOD_PHYSICAL_SKY_SPACE_COMP)                \
	SHADER_MODULE_DO(QVK_MOD_SKY_BUFFER_RESOLVE_COMP)                \
	SHADER_MODULE_DO(QVK_MOD_CHECKERBOARD_INTERLEAVE_COMP)           \
	SHADER_MODULE_DO(QVK_MOD_GOD_RAYS_COMP)                          \
	SHADER_MODULE_DO(QVK_MOD_GOD_RAYS_FILTER_COMP)                   \
	SHADER_MODULE_DO(QVK_MOD_SHADOW_MAP_VERT)                        \
	SHADER_MODULE_DO(QVK_MOD_COMPOSITING_COMP)                       \
	SHADER_MODULE_DO(QVK_MOD_FSR_EASU_FP16_COMP)                     \
	SHADER_MODULE_DO(QVK_MOD_FSR_EASU_FP32_COMP)                     \
	SHADER_MODULE_DO(QVK_MOD_FSR_RCAS_FP16_COMP)                     \
	SHADER_MODULE_DO(QVK_MOD_FSR_RCAS_FP32_COMP)                     \
	SHADER_MODULE_DO(QVK_MOD_NORMALIZE_NORMAL_MAP_COMP)              \
	SHADER_MODULE_DO(QVK_MOD_DEBUG_LINE_FRAG)                        \
	SHADER_MODULE_DO(QVK_MOD_DEBUG_LINE_VERT)                        \

#define LIST_RT_RGEN_SHADER_MODULES \
	SHADER_MODULE_DO(QVK_MOD_PRIMARY_RAYS_RGEN)                      \
	SHADER_MODULE_DO(QVK_MOD_REFLECT_REFRACT_RGEN)                   \
	SHADER_MODULE_DO(QVK_MOD_DIRECT_LIGHTING_RGEN)                   \
	SHADER_MODULE_DO(QVK_MOD_INDIRECT_LIGHTING_RGEN)                 \

#define LIST_RT_PIPELINE_SHADER_MODULES \
	SHADER_MODULE_DO(QVK_MOD_PATH_TRACER_RCHIT)                      \
	SHADER_MODULE_DO(QVK_MOD_PATH_TRACER_MASKED_RAHIT)               \
	SHADER_MODULE_DO(QVK_MOD_PATH_TRACER_PARTICLE_RAHIT)             \
	SHADER_MODULE_DO(QVK_MOD_PATH_TRACER_BEAM_RAHIT)                 \
	SHADER_MODULE_DO(QVK_MOD_PATH_TRACER_BEAM_RINT)                  \
	SHADER_MODULE_DO(QVK_MOD_PATH_TRACER_RMISS)                      \
	SHADER_MODULE_DO(QVK_MOD_PATH_TRACER_EXPLOSION_RAHIT)            \
	SHADER_MODULE_DO(QVK_MOD_PATH_TRACER_SPRITE_RAHIT)               \


#define SHADER_STAGE(_module, _stage) \
	{ \
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, \
		.stage = _stage, \
		.module = qvk.shader_modules[_module], \
		.pName = "main" \
	}

#define SHADER_STAGE_SPEC(_module, _stage, _spec) \
	{ \
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, \
		.stage = _stage, \
		.module = qvk.shader_modules[_module], \
		.pName = "main", \
		.pSpecializationInfo = _spec, \
	}

enum QVK_SHADER_MODULES {
#define SHADER_MODULE_DO(a) a,
	LIST_SHADER_MODULES
	LIST_RT_RGEN_SHADER_MODULES
	LIST_RT_PIPELINE_SHADER_MODULES
#undef SHADER_MODULE_DO
	NUM_QVK_SHADER_MODULES
};

#define MAX_FRAMES_IN_FLIGHT 2

typedef struct cmd_buf_group_s {
	uint32_t count_per_frame;
	uint32_t used_this_frame;
	VkCommandBuffer* buffers;
	VkCommandPool command_pool;
#ifdef USE_DEBUG
	void** buffer_begin_addrs;
#endif
} cmd_buf_group_t;

typedef struct semaphore_group_s {
	VkSemaphore image_available;
	VkSemaphore render_finished;
	VkSemaphore transfer_finished;
	VkSemaphore trace_finished;
	bool trace_signaled;
} semaphore_group_t;

typedef struct QVK_s {
	VkInstance                  instance;
	VkPhysicalDevice            physical_device;
	VkPhysicalDeviceMemoryProperties mem_properties;

	// number of GPUs we're rendering to --- if DG is disabled, this is 1
	int							device_count;
#ifdef VKPT_DEVICE_GROUPS
	VkPhysicalDevice			device_group_physical_devices[VKPT_MAX_GPUS];
#endif

	VkDevice                    device;
	VkQueue                     queue_graphics;
	VkQueue                     queue_transfer;
	int32_t                     queue_idx_graphics;
	int32_t                     queue_idx_transfer;
	VkSurfaceKHR                surface;
	VkSwapchainKHR              swap_chain;
	VkSurfaceFormatKHR          surf_format;
	bool                        surf_is_hdr;
	bool                        surf_vsync;
	VkPresentModeKHR            present_mode;
	VkExtent2D                  extent_screen_images;
	VkExtent2D                  extent_render;
	VkExtent2D                  extent_render_prev;
	VkExtent2D                  extent_unscaled;
	VkExtent2D                  extent_taa_images;
	VkExtent2D                  extent_taa_output;
	uint32_t                    gpu_slice_width;
	uint32_t                    gpu_slice_width_prev;
	uint32_t                    num_swap_chain_images;
	VkImage*                    swap_chain_images;
	VkImageView*                swap_chain_image_views;
	
	bool                        use_ray_query;
	bool                        enable_validation;
	bool                        supports_fp16;
	bool                        supports_colorspace;
	bool                        supports_debug_lines;
	bool                        supports_smooth_lines;

	cmd_buf_group_t             cmd_buffers_graphics;
	cmd_buf_group_t             cmd_buffers_transfer;
	semaphore_group_t           semaphores[MAX_FRAMES_IN_FLIGHT][VKPT_MAX_GPUS];

	uint32_t                    num_extensions;
	VkExtensionProperties       *extensions;

	uint32_t                    num_layers;
	VkLayerProperties           *layers;

	VkDebugUtilsMessengerEXT    dbg_messenger;

	VkFence                     fences_frame_sync[MAX_FRAMES_IN_FLIGHT];


	int                         draw_width;
	int                         draw_height;
	uint64_t                    frame_counter;

	int                         effective_aa_mode;

	SDL_Window                  *window;
	uint32_t                    num_sdl2_extensions;
	const char                  **sdl2_extensions;

	uint32_t                    current_swap_chain_image_index;
	uint32_t                    current_frame_index;
	// when set, we'll do a WFI before acquire for this many frames
	uint32_t                    wait_for_idle_frames;
	float                       timestampPeriod;
	bool                        frame_menu_mode;

	VkShaderModule              shader_modules[NUM_QVK_SHADER_MODULES];

	VkDescriptorSetLayout       desc_set_layout_ubo;
	VkDescriptorSet             desc_set_ubo;

	VkDescriptorSetLayout       desc_set_layout_textures;
	VkDescriptorSet             desc_set_textures_even;
	VkDescriptorSet             desc_set_textures_odd;
	VkImage                     images      [NUM_VKPT_IMAGES]; // todo: rename to make consistent
	VkImageView                 images_views[NUM_VKPT_IMAGES]; // todo: rename to make consistent

#ifdef VKPT_DEVICE_GROUPS
	// local per-GPU image bindings for SLI
	VkImage						images_local[NUM_VKPT_IMAGES][VKPT_MAX_GPUS];
	VkImageView					images_views_local[NUM_VKPT_IMAGES][VKPT_MAX_GPUS];
#endif

	VkDescriptorSetLayout       desc_set_layout_vertex_buffer;
	VkDescriptorSet             desc_set_vertex_buffer;
	
	BufferResource_t            buf_world;
	BufferResource_t            buf_primitive_instanced;
	BufferResource_t            buf_positions_instanced;

	BufferResource_t            buf_light;
	BufferResource_t            buf_light_staging[MAX_FRAMES_IN_FLIGHT];
	BufferResource_t            buf_light_stats[NUM_LIGHT_STATS_BUFFERS];
	BufferResource_t            buf_light_counts_history[LIGHT_COUNT_HISTORY];
	
	BufferResource_t            buf_iqm_matrices;
	BufferResource_t            buf_iqm_matrices_staging[MAX_FRAMES_IN_FLIGHT];
	float*                      iqm_matrices_shadow;
	float*                      iqm_matrices_prev;

	BufferResource_t            buf_readback;
	BufferResource_t            buf_readback_staging[MAX_FRAMES_IN_FLIGHT];

	BufferResource_t            buf_tonemap;
	BufferResource_t            buf_sun_color;

    VkSampler                   tex_sampler, 
                                tex_sampler_nearest,
                                tex_sampler_nearest_mipmap_aniso,
                                tex_sampler_linear_clamp;
	
	float                       sintab[256];

	VkImage screenshot_image;
	VkDeviceMemory screenshot_image_memory;
	VkDeviceSize screenshot_image_memory_size;

	image_t *raw_image; // "raw" image, for cinematics

#ifdef VKPT_IMAGE_DUMPS
	// host-visible image for dumping FB data through
	VkImage dump_image;
	VkDeviceMemory dump_image_memory;
	VkDeviceSize dump_image_memory_size;
#endif
} QVK_t;

extern QVK_t qvk;

#define LIST_EXTENSIONS_ACCEL_STRUCT \
	VK_EXTENSION_DO(vkCreateAccelerationStructureKHR) \
	VK_EXTENSION_DO(vkDestroyAccelerationStructureKHR) \
	VK_EXTENSION_DO(vkCmdBuildAccelerationStructuresKHR) \
	VK_EXTENSION_DO(vkCmdCopyAccelerationStructureKHR) \
	VK_EXTENSION_DO(vkGetAccelerationStructureDeviceAddressKHR) \
	VK_EXTENSION_DO(vkCmdWriteAccelerationStructuresPropertiesKHR) \
	VK_EXTENSION_DO(vkGetAccelerationStructureBuildSizesKHR) \
	VK_EXTENSION_DO(vkGetBufferDeviceAddress) \

#define LIST_EXTENSIONS_RAY_PIPELINE \
	VK_EXTENSION_DO(vkCreateRayTracingPipelinesKHR) \
	VK_EXTENSION_DO(vkCmdTraceRaysKHR) \
	VK_EXTENSION_DO(vkGetRayTracingShaderGroupHandlesKHR) \

#define LIST_EXTENSIONS_DEBUG \
	VK_EXTENSION_DO(vkDebugMarkerSetObjectNameEXT) \

#define LIST_EXTENSIONS_INSTANCE \
	VK_EXTENSION_DO(vkCmdBeginDebugUtilsLabelEXT) \
	VK_EXTENSION_DO(vkCmdEndDebugUtilsLabelEXT)

#define VK_EXTENSION_DO(a) extern PFN_##a q##a;
LIST_EXTENSIONS_ACCEL_STRUCT
LIST_EXTENSIONS_RAY_PIPELINE
LIST_EXTENSIONS_DEBUG
LIST_EXTENSIONS_INSTANCE
#undef VK_EXTENSION_DO

#define MAX_SKY_CLUSTERS 1024

typedef mat3 prim_positions_t;

typedef struct
{
	uint8_t* geometry_storage;
	VkAccelerationStructureGeometryKHR* geometries;
	VkAccelerationStructureBuildRangeInfoKHR* build_ranges;
	uint32_t* prim_counts;
	uint32_t* prim_offsets;
	uint32_t num_geometries;
	uint32_t max_geometries;
	VkAccelerationStructureBuildSizesInfoKHR build_sizes;
	VkDeviceSize blas_data_offset;
	VkAccelerationStructureKHR accel;
	VkDeviceAddress blas_device_address;
	VkGeometryInstanceFlagsKHR instance_flags;
	uint32_t instance_mask;
	uint32_t sbt_offset;
} model_geometry_t;

typedef struct {
	BufferResource_t buffer;
	BufferResource_t staging_buffer;
	int registration_sequence;
	model_geometry_t geom_opaque;
	model_geometry_t geom_transparent;
	model_geometry_t geom_masked;
	size_t vertex_data_offset;
	uint32_t total_tris;
	bool is_static;
} model_vbo_t;

typedef struct
{
	model_geometry_t geometry;

	vec3_t center;
	vec3_t aabb_min;
	vec3_t aabb_max;

	int num_light_polys;
	int allocated_light_polys;
	light_poly_t *light_polys;

	bool transparent;
	bool masked;
} bsp_model_t;

typedef struct aabb_s {
	vec3_t mins;
	vec3_t maxs;
} aabb_t;

typedef struct bsp_mesh_s {
	bsp_model_t *models;
	int num_models;

	aabb_t world_aabb;

	VboPrimitive* primitives;
	uint32_t num_primitives_allocated;
	uint32_t num_primitives;
	size_t vertex_data_offset;

	model_geometry_t geom_opaque;
	model_geometry_t geom_transparent;
	model_geometry_t geom_masked;
	model_geometry_t geom_sky;
	model_geometry_t geom_custom_sky;

	int num_clusters;

	int num_cluster_lights;
	int *cluster_light_offsets;
	int *cluster_lights;

	int num_light_polys;
	int allocated_light_polys;
	light_poly_t *light_polys;

	uint32_t sky_clusters[MAX_SKY_CLUSTERS];
	int num_sky_clusters;
	bool all_lava_emissive;

	struct { vec3_t pos; vec3_t dir; } cameras[MAX_CAMERAS];
	int num_cameras;

	byte sky_visibility[VIS_MAX_BYTES];

	aabb_t* cluster_aabbs;
} bsp_mesh_t;

void bsp_mesh_create_from_bsp(bsp_mesh_t *wm, bsp_t *bsp, const char* map_name);
void bsp_mesh_destroy(bsp_mesh_t *wm);
void bsp_mesh_register_textures(bsp_t *bsp);
void bsp_mesh_animate_light_polys(bsp_mesh_t *wm);
uint32_t encode_normal(const vec3_t normal);

typedef struct vkpt_refdef_s {
	QVKUniformBuffer_t uniform_buffer;
	InstanceBuffer uniform_instance_buffer;
	refdef_t *fd;
	float view_matrix[16];
	float view_matrix_inv[16];

	float z_near, z_far;

	bsp_mesh_t bsp_mesh_world;
	int bsp_mesh_world_loaded;

	lightstyle_t prev_lightstyles[MAX_LIGHTSTYLES];
} vkpt_refdef_t;

extern vkpt_refdef_t vkpt_refdef;

extern BufferResource_t buf_accel_scratch;

typedef struct sun_light_s {
	vec3_t direction;
	vec3_t direction_envmap;
	vec3_t color;
	float angular_size_rad;
	bool use_physical_sky;
	bool visible;
} sun_light_t;

typedef struct entity_hash_s {
	unsigned int mesh : 8;
	unsigned int model : 9;
	unsigned int entity : 14;
	unsigned int bsp : 1;
} entity_hash_t;

void mult_matrix_matrix(mat4_t p, const mat4_t a, const mat4_t b);
void mult_matrix_vector(vec4_t v, const mat4_t a, const vec4_t b);
void create_entity_matrix(mat4_t matrix, entity_t *e);
void create_viewweapon_matrix(mat4_t matrix, entity_t *e);
void create_projection_matrix(mat4_t matrix, float znear, float zfar, float fov_x, float fov_y);
void create_view_matrix(mat4_t matrix, refdef_t *fd);
void inverse(const mat4_t m, mat4_t inv);
void create_orthographic_matrix(mat4_t matrix, float xmin, float xmax,
		float ymin, float ymax, float znear, float zfar);

#define PROFILER_LIST \
	PROFILER_DO(FRAME_TIME,                 0) \
	PROFILER_DO(INSTANCE_GEOMETRY,          1) \
	PROFILER_DO(BVH_UPDATE,                 1) \
	PROFILER_DO(PRIMARY_RAYS,               1) \
	PROFILER_DO(REFLECT_REFRACT_1,          1) \
	PROFILER_DO(REFLECT_REFRACT_2,          1) \
	PROFILER_DO(ASVGF_GRADIENT_REPROJECT,   1) \
	PROFILER_DO(DIRECT_LIGHTING,            1) \
	PROFILER_DO(INDIRECT_LIGHTING,          1) \
	PROFILER_DO(INDIRECT_LIGHTING_0,        2) \
	PROFILER_DO(INDIRECT_LIGHTING_1,        2) \
	PROFILER_DO(ASVGF_FULL,                 1) \
	PROFILER_DO(ASVGF_RECONSTRUCT_GRADIENT, 2) \
	PROFILER_DO(ASVGF_TEMPORAL,             2) \
	PROFILER_DO(ASVGF_ATROUS,               2) \
	PROFILER_DO(MGPU_TRANSFERS,             1) \
	PROFILER_DO(INTERLEAVE,                 1) \
	PROFILER_DO(ASVGF_TAA,                  2) \
	PROFILER_DO(BLOOM,                      1) \
	PROFILER_DO(TONE_MAPPING,               1) \
	PROFILER_DO(FSR,                        1) \
	PROFILER_DO(FSR_EASU,                   2) \
	PROFILER_DO(FSR_RCAS,                   2) \
	PROFILER_DO(UPDATE_ENVIRONMENT,         1) \
	PROFILER_DO(GOD_RAYS,                   1) \
	PROFILER_DO(GOD_RAYS_REFLECT_REFRACT,   1) \
	PROFILER_DO(GOD_RAYS_FILTER,            1) \
	PROFILER_DO(SHADOW_MAP,                 1) \
	PROFILER_DO(COMPOSITING,                1) \

enum {
#define PROFILER_DO(a, ...) PROFILER_##a,
	PROFILER_LIST
#undef PROFILER_DO
	NUM_PROFILER_ENTRIES,
};

#define NUM_PROFILER_QUERIES_PER_FRAME (NUM_PROFILER_ENTRIES * 2)

typedef enum {
	PROFILER_START, 
	PROFILER_STOP,
} VKPTProfilerAction;

typedef struct EntityUploadInfo
{
	uint32_t num_instances;
	uint32_t num_prims;
	uint32_t opaque_prim_count;
	uint32_t transparent_prim_offset;
	uint32_t transparent_prim_count;
	uint32_t masked_prim_offset;
	uint32_t masked_prim_count;
	uint32_t viewer_model_prim_offset;
	uint32_t viewer_model_prim_count;
	uint32_t viewer_weapon_prim_offset;
	uint32_t viewer_weapon_prim_count;
	uint32_t explosions_prim_offset;
	uint32_t explosions_prim_count;
	bool weapon_left_handed;
} EntityUploadInfo;

VkDescriptorSet qvk_get_current_desc_set_textures(void);

VkResult vkpt_profiler_initialize(void);
VkResult vkpt_profiler_destroy(void);
VkResult vkpt_profiler_query(VkCommandBuffer cmd_buf, int idx, VKPTProfilerAction action);
VkResult vkpt_profiler_next_frame(VkCommandBuffer cmd_buf);
void draw_profiler(int enable_asvgf);
double vkpt_get_profiler_result(int idx);

VkResult vkpt_readback(ReadbackBuffer* dst);

VkResult vkpt_textures_initialize(void);
VkResult vkpt_textures_destroy(void);
VkResult vkpt_textures_end_registration(void);
VkResult vkpt_textures_upload_envmap(int w, int h, byte *data);
void vkpt_textures_destroy_unused(void);
void vkpt_textures_update_descriptor_set(void);
image_t *vkpt_fake_emissive_texture(image_t *image, int bright_threshold_int);
void vkpt_extract_emissive_texture_info(image_t *image);
void vkpt_textures_prefetch(void);
void vkpt_invalidate_texture_descriptors(void);
void vkpt_init_light_textures(void);

VkCommandBuffer vkpt_begin_command_buffer(cmd_buf_group_t* group);
void vkpt_free_command_buffers(cmd_buf_group_t* group);
void vkpt_reset_command_buffers(cmd_buf_group_t* group);
void vkpt_wait_idle(VkQueue queue, cmd_buf_group_t* group);

void vkpt_submit_command_buffer(
	VkCommandBuffer cmd_buf,
	VkQueue queue,
	uint32_t execute_device_mask,
	int wait_semaphore_count,
	VkSemaphore* wait_semaphores,
	VkPipelineStageFlags* wait_stages,
	uint32_t* wait_device_indices,
	int signal_semaphore_count,
	VkSemaphore* signal_semaphores,
	uint32_t* signal_device_indices,
	VkFence fence);

void vkpt_submit_command_buffer_simple(
	VkCommandBuffer cmd_buf,
	VkQueue queue,
	bool all_gpus);


#define ALL_GPUS (-1)
void set_current_gpu(VkCommandBuffer cmd_buf, int gpu_index);

VkResult allocate_gpu_memory(VkMemoryRequirements mem_req, VkDeviceMemory* pMemory);

#ifdef VKPT_DEVICE_GROUPS
void vkpt_mgpu_global_barrier(VkCommandBuffer cmd_buf);

void
vkpt_mgpu_image_copy(VkCommandBuffer cmd_buf,
					int src_image_index,
					int dst_image_index,
					int src_gpu_index,
					int dst_gpu_index,
					VkOffset2D src_offset,
					VkOffset2D dst_offset,
					VkExtent2D size);
#endif

void
vkpt_image_copy(VkCommandBuffer cmd_buf,
					int src_image_index,
					int dst_image_index,
					VkOffset2D src_offset,
					VkOffset2D dst_offset,
					VkExtent2D size);

VkResult vkpt_draw_initialize(void);
VkResult vkpt_draw_destroy(void);
VkResult vkpt_draw_destroy_pipelines(void);
VkResult vkpt_draw_create_pipelines(void);
VkResult vkpt_draw_submit_stretch_pics(VkCommandBuffer cmd_buf);
VkResult vkpt_final_blit(VkCommandBuffer cmd_buf, unsigned int image_index, VkExtent2D extent, bool filtered, bool warped);
VkResult vkpt_draw_clear_stretch_pics(void);

VkResult vkpt_uniform_buffer_create(void);
VkResult vkpt_uniform_buffer_destroy(void);
VkResult vkpt_uniform_buffer_upload_to_staging(void);
void vkpt_uniform_buffer_copy_from_staging(VkCommandBuffer command_buffer);

void vkpt_init_model_geometry(model_geometry_t* info, uint32_t max_geometries);
void vkpt_destroy_model_geometry(model_geometry_t* info);
void vkpt_append_model_geometry(model_geometry_t* info, uint32_t num_prims, uint32_t prim_offset, const char* model_name);
VkResult vkpt_vertex_buffer_create(void);
VkResult vkpt_vertex_buffer_destroy(void);
void vkpt_vertex_buffer_ensure_primbuf_size(uint32_t prim_count);
VkResult vkpt_vertex_buffer_upload_bsp_mesh(bsp_mesh_t* bsp_mesh);
void vkpt_vertex_buffer_cleanup_bsp_mesh(bsp_mesh_t *bsp_mesh);
VkResult vkpt_vertex_buffer_create_pipelines(void);
VkResult vkpt_vertex_buffer_destroy_pipelines(void);
VkResult vkpt_instance_geometry(VkCommandBuffer cmd_buf, uint32_t num_instances, bool update_world_animations);
void vkpt_vertex_buffer_invalidate_static_model_vbos(int material_index);
VkResult vkpt_vertex_buffer_upload_models(void);
void vkpt_light_buffer_reset_counts(void);
VkResult vkpt_light_buffer_upload_to_staging(bool render_world, bsp_mesh_t *bsp_mesh, bsp_t* bsp, int num_model_lights, light_poly_t* transformed_model_lights, const float* sky_radiance);
VkResult vkpt_light_buffer_upload_staging(VkCommandBuffer cmd_buf);
VkResult vkpt_light_buffers_create(bsp_mesh_t *bsp_mesh);
VkResult vkpt_light_buffers_destroy(void);
bool vkpt_model_is_static(const model_t* model);
const model_vbo_t* vkpt_get_model_vbo(const model_t* model);

VkResult vkpt_iqm_matrix_buffer_upload_staging(VkCommandBuffer cmd_buf);

typedef struct {
	VkImage image;
	VkImageView image_view;
	VkDeviceMemory image_mem;

#ifdef VKPT_DEVICE_GROUPS
	// local per-GPU image bindings for SLI
	VkImage image_local[VKPT_MAX_GPUS];
	VkImageView image_view_local[VKPT_MAX_GPUS];
#endif
} vkpt_lazy_image_t;

VkResult vkpt_load_shader_modules(void);
VkResult vkpt_destroy_shader_modules(void);
VkResult vkpt_create_images(void);
// Fill a "lazy" image with actual Vulkan resources
VkResult vkpt_prepare_lazy_image(vkpt_lazy_image_t *lazy_image, int w, int h, VkFormat format, const char *descr);
VkResult vkpt_destroy_images(void);
// Destroy resources associated with a "lazy" image
VkResult vkpt_destroy_lazy_image(vkpt_lazy_image_t *lazy_image);

VkResult vkpt_pt_init(void);
VkResult vkpt_pt_destroy(void);
VkResult vkpt_pt_create_pipelines(void);
VkResult vkpt_pt_destroy_pipelines(void);

void vkpt_pt_reset_instances(void);
void vkpt_pt_instance_model_blas(const model_geometry_t* geom, const mat4 transform, uint32_t buffer_idx, int model_instance_index, uint32_t override_instance_mask);

VkResult vkpt_pt_create_toplevel(VkCommandBuffer cmd_buf, int idx, const EntityUploadInfo* upload_info, bool weapon_left_handed);
VkResult vkpt_pt_trace_primary_rays(VkCommandBuffer cmd_buf);
VkResult vkpt_pt_trace_reflections(VkCommandBuffer cmd_buf, int bounce);
VkResult vkpt_pt_trace_lighting(VkCommandBuffer cmd_buf, float num_bounce_rays);
VkResult vkpt_pt_update_descripter_set_bindings(int idx);
VkResult vkpt_pt_create_all_dynamic(VkCommandBuffer cmd_buf, int idx, const EntityUploadInfo* upload_info);

VkResult vkpt_asvgf_initialize(void);
VkResult vkpt_asvgf_destroy(void);
VkResult vkpt_asvgf_create_pipelines(void);
VkResult vkpt_asvgf_destroy_pipelines(void);
VkResult vkpt_asvgf_filter(VkCommandBuffer cmd_buf, bool enable_lf);
VkResult vkpt_compositing(VkCommandBuffer cmd_buf);
VkResult vkpt_interleave(VkCommandBuffer cmd_buf);
VkResult vkpt_taa(VkCommandBuffer cmd_buf);
VkResult vkpt_asvgf_gradient_reproject(VkCommandBuffer cmd_buf);

void vkpt_fsr_init_cvars(void);
VkResult vkpt_fsr_initialize(void);
VkResult vkpt_fsr_destroy(void);
VkResult vkpt_fsr_create_pipelines(void);
VkResult vkpt_fsr_destroy_pipelines(void);
bool vkpt_fsr_is_enabled(void);
bool vkpt_fsr_needs_upscale(void);
void vkpt_fsr_update_ubo(QVKUniformBuffer_t *ubo);
VkResult vkpt_fsr_do(VkCommandBuffer cmd_buf);
VkResult vkpt_fsr_final_blit(VkCommandBuffer cmd_buf, bool warp);

VkResult vkpt_bloom_initialize(void);
VkResult vkpt_bloom_destroy(void);
VkResult vkpt_bloom_create_pipelines(void);
VkResult vkpt_bloom_destroy_pipelines(void);
void vkpt_bloom_reset(void);
void vkpt_bloom_update(QVKUniformBuffer_t * ubo, float frame_time, bool under_water, bool menu_mode);
VkResult vkpt_bloom_record_cmd_buffer(VkCommandBuffer cmd_buf);

VkResult vkpt_tone_mapping_initialize(void);
VkResult vkpt_tone_mapping_destroy(void);
VkResult vkpt_tone_mapping_create_pipelines(void);
VkResult vkpt_tone_mapping_reset(VkCommandBuffer cmd_buf);
VkResult vkpt_tone_mapping_destroy_pipelines(void);
VkResult vkpt_tone_mapping_record_cmd_buffer(VkCommandBuffer cmd_buf, float frame_time);
void vkpt_tone_mapping_request_reset(void);
void vkpt_tone_mapping_draw_debug(void);

VkResult vkpt_shadow_map_initialize(void);
VkResult vkpt_shadow_map_destroy(void);
VkResult vkpt_shadow_map_create_pipelines(void);
VkResult vkpt_shadow_map_destroy_pipelines(void);
VkResult vkpt_shadow_map_render(VkCommandBuffer cmd_buf, float* view_projection_matrix,
	uint32_t static_offset, uint32_t num_static_verts,
	uint32_t dynamic_offset, uint32_t num_dynamic_verts,
	uint32_t transparent_offset, uint32_t num_transparent_verts);
VkImageView vkpt_shadow_map_get_view(void);
void vkpt_shadow_map_setup(const sun_light_t* light, const float* bbox_min, const float* bbox_max,
	float* VP, float* depth_scale, bool random_sampling);
void vkpt_shadow_map_reset_instances(void);
void vkpt_shadow_map_add_instance(const float* model_matrix, VkBuffer buffer, size_t vertex_offset, uint32_t prim_count);

int load_img(const char *name, image_t *image);
// Transparency module API

bool initialize_transparency(void);
void destroy_transparency(void);

void update_transparency(VkCommandBuffer command_buffer, const float* view_matrix,
	const particle_t* particles, int particle_num, const entity_t* entities, int entity_num);

typedef enum {
	VKPT_TRANSPARENCY_PARTICLES,
	VKPT_TRANSPARENCY_SPRITES,

	VKPT_TRANSPARENCY_COUNT
} vkpt_transparency_t;

void vkpt_get_transparency_buffers(
	vkpt_transparency_t ttype, 
	BufferResource_t** vertex_buffer,
	uint64_t* vertex_offset, 
	BufferResource_t** index_buffer,
	uint64_t* index_offset,
	uint32_t* num_vertices,
	uint32_t* num_indices);
void vkpt_get_beam_aabb_buffer(
	BufferResource_t** aabb_buffer,
	uint64_t* aabb_offset,
	uint32_t* num_aabbs);

VkBufferView get_transparency_particle_color_buffer_view(void);
VkBufferView get_transparency_beam_color_buffer_view(void);
VkBufferView get_transparency_sprite_info_buffer_view(void);
VkBufferView get_transparency_beam_intersect_buffer_view(void);
void get_transparency_counts(int* particle_num, int* beam_num, int* sprite_num);
void vkpt_build_beam_lights(light_poly_t* light_list, int* num_lights, int max_lights, bsp_t *bsp, entity_t* entities, int num_entites, float adapted_luminance, int* light_entity_ids, int* num_light_entities);
bool vkpt_build_cylinder_light(light_poly_t* light_list, int* num_lights, int max_lights, bsp_t *bsp, vec3_t begin, vec3_t end, vec3_t color, float radius, entity_hash_t hash, int* light_entity_ids);
bool get_triangle_off_center(const float* positions, float* center, float* anti_center, float offset);

VkResult vkpt_initialize_god_rays(void);
VkResult vkpt_destroy_god_rays(void);
VkResult vkpt_god_rays_create_pipelines(void);
VkResult vkpt_god_rays_destroy_pipelines(void);
VkResult vkpt_god_rays_update_images(void);
VkResult vkpt_god_rays_noop(void);
bool vkpt_god_rays_enabled(const sun_light_t* sun_light);
void vkpt_record_god_rays_trace_command_buffer(VkCommandBuffer command_buffer, int pass);
void vkpt_record_god_rays_filter_command_buffer(VkCommandBuffer command_buffer);
void vkpt_god_rays_prepare_ubo(
	QVKUniformBuffer_t * ubo, 
	const aabb_t* world_aabb,
	const float* proj, 
	const float* view, 
	const float* shadowmap_viewproj, 
	float shadowmap_depth_scale);

void vkpt_freecam_reset(void);
void vkpt_freecam_update(float frame_time);
void vkpt_reset_accumulation(void);

typedef struct maliasframe_s {
    vec3_t  scale;
    vec3_t  translate;
    vec3_t  bounds[2];
    vec_t   radius;
} maliasframe_t;

typedef struct maliasmesh_s {
    int             numverts;
    int             numtris;
    int             numindices;
    int             numskins;
    int             tri_offset; /* offset in vertex buffer on device */
    int             *indices;
    vec3_t          *positions;
    vec3_t          *normals;
    vec2_t          *tex_coords;
	vec3_t          *tangents;
	uint32_t        *blend_indices; // iqm only
	uint32_t        *blend_weights; // iqm only
	struct pbr_material_s **materials;
	bool            handedness;
} maliasmesh_t;

// needed for model.c
#define TESS_MAX_VERTICES   16384
#define TESS_MAX_INDICES    (3 * TESS_MAX_VERTICES)

#define QGL_INDEX_TYPE  GLuint

typedef struct {
    color_t     colors[2]; // 0 - actual color, 1 - transparency (for text drawing)
    float scale;
	float alpha_scale;
} drawStatic_t;

// Performance marker debug labels
extern const char *perf_marker_labels[NUM_PROFILER_ENTRIES];

static inline void begin_perf_marker(VkCommandBuffer command_buffer, int index)
{
	_VK(vkpt_profiler_query(command_buffer, index, PROFILER_START));

	const VkDebugUtilsLabelEXT label = {
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
		.pLabelName = perf_marker_labels[index]
	};

	if (qvkCmdBeginDebugUtilsLabelEXT != NULL)
		qvkCmdBeginDebugUtilsLabelEXT(command_buffer, &label);
}

static inline void end_perf_marker(VkCommandBuffer command_buffer, int index)
{
	if (qvkCmdEndDebugUtilsLabelEXT != NULL)
		qvkCmdEndDebugUtilsLabelEXT(command_buffer);

	_VK(vkpt_profiler_query(command_buffer, index, PROFILER_STOP));
}

#define BEGIN_PERF_MARKER(command_buffer, name)  begin_perf_marker(command_buffer, name)
#define END_PERF_MARKER(command_buffer, name)    end_perf_marker(command_buffer, name)

void R_SetClipRect_RTX(const clipRect_t *clip);
void R_ClearColor_RTX(void);
void R_SetAlpha_RTX(float alpha);
void R_SetAlphaScale_RTX(float alpha);
void R_SetColor_RTX(uint32_t color);
void R_LightPoint_RTX(const vec3_t origin, vec3_t light);
void R_SetScale_RTX(float scale);
void R_DrawStretchPic_RTX(int x, int y, int w, int h, qhandle_t pic);
void R_DrawKeepAspectPic_RTX(int x, int y, int w, int h, qhandle_t pic);
void R_DrawPic_RTX(int x, int y, qhandle_t pic);
void R_DrawStretchRaw_RTX(int x, int y, int w, int h);
void R_UpdateRawPic_RTX(int pic_w, int pic_h, const uint32_t *pic);
void R_DiscardRawPic_RTX(void);
void R_TileClear_RTX(int x, int y, int w, int h, qhandle_t pic);
void R_DrawFill8_RTX(int x, int y, int w, int h, int c);
void R_DrawFill32_RTX(int x, int y, int w, int h, uint32_t color);
void R_DrawChar_RTX(int x, int y, int flags, int c, qhandle_t font);
int R_DrawString_RTX(int x, int y, int flags, size_t maxlen, const char *s, qhandle_t font);
bool R_InterceptKey_RTX(unsigned key, bool down);

void IMG_Load_RTX(image_t *image, byte *pic);
void IMG_Unload_RTX(image_t *image);
void IMG_ReadPixels_RTX(screenshot_t *s);
void IMG_ReadPixelsHDR_RTX(screenshot_t *s);

int MOD_LoadMD2_RTX(model_t *model, const void *rawdata, size_t length, const char* mod_name);
int MOD_LoadMD3_RTX(model_t* model, const void* rawdata, size_t length, const char* mod_name);
int MOD_LoadIQM_RTX(model_t *model, const void *rawdata, size_t length, const char* mod_name);
void MOD_Reference_RTX(model_t *model);

bool vkpt_debugdraw_supported(void);
void vkpt_debugdraw_addtext(const vec3_t origin, const vec3_t angles, const char *text, float size, uint32_t color, uint32_t time, bool depth_test);
bool vkpt_debugdraw_have(void);
void vkpt_debugdraw_draw(VkCommandBuffer cmd_buf);
VkResult vkpt_debugdraw_create(void);
VkResult vkpt_debugdraw_create_pipelines(void);
void vkpt_debugdraw_prepare(void);
VkImageView vpkt_debugdraw_imageview(void);
VkResult vkpt_debugdraw_destroy(void);
VkResult vkpt_debugdraw_destroy_pipelines(void);

#endif  /*__VKPT_H__*/

