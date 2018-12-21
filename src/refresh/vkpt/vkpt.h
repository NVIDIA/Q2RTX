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

#ifndef  __VKPT_H__
#define  __VKPT_H__

#include <vulkan/vulkan.h>
#include <SDL.h>
#include <SDL_vulkan.h>

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

#define LENGTH(a) ((sizeof (a)) / (sizeof(*(a))))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

#define LOG_FUNC_(f) do {} while(0)
//#define LOG_FUNC_(f) Com_Printf("%s\n", f)
#define LOG_FUNC() LOG_FUNC_(__func__)

#define FUNC_UNIMPLEMENTED_(f) Com_EPrintf("Calling unimplemented function %s\n", f)
#define FUNC_UNIMPLEMENTED() FUNC_UNIMPLEMENTED_(__func__)

#define MAX_LIGHTS 4096

#define _VK(...) \
	do { \
		VkResult _res = __VA_ARGS__; \
		if(_res != VK_SUCCESS) { \
			Com_EPrintf("error %d executing %s!\n", _res, # __VA_ARGS__); \
		} \
	} while(0)

/* see main.c to override default file path. By default it will strip away
 * QVK_MOD_, fix the file ending, and convert to lower case */
#define LIST_SHADER_MODULES \
	SHADER_MODULE_DO(QVK_MOD_STRETCH_PIC_VERT)                       \
	SHADER_MODULE_DO(QVK_MOD_STRETCH_PIC_FRAG)                       \
	SHADER_MODULE_DO(QVK_MOD_PATH_TRACER_RGEN)                       \
	SHADER_MODULE_DO(QVK_MOD_PATH_TRACER_RCHIT)                      \
	SHADER_MODULE_DO(QVK_MOD_PATH_TRACER_RMISS)                      \
	SHADER_MODULE_DO(QVK_MOD_PATH_TRACER_RTX_RGEN)                   \
	SHADER_MODULE_DO(QVK_MOD_PATH_TRACER_RTX_RCHIT)                  \
	SHADER_MODULE_DO(QVK_MOD_PATH_TRACER_RTX_RMISS)                  \
	SHADER_MODULE_DO(QVK_MOD_INSTANCE_GEOMETRY_COMP)                 \
	SHADER_MODULE_DO(QVK_MOD_ASVGF_SEED_RNG_COMP)                    \
	SHADER_MODULE_DO(QVK_MOD_ASVGF_FWD_PROJECT_COMP)                 \
	SHADER_MODULE_DO(QVK_MOD_ASVGF_GRADIENT_IMG_COMP)                \
	SHADER_MODULE_DO(QVK_MOD_ASVGF_GRADIENT_ATROUS_COMP)             \
	SHADER_MODULE_DO(QVK_MOD_ASVGF_ATROUS_COMP)                      \
	SHADER_MODULE_DO(QVK_MOD_ASVGF_TEMPORAL_COMP)                    \
	SHADER_MODULE_DO(QVK_MOD_ASVGF_TAA_COMP)                         \

#ifndef VKPT_SHADER_DIR
#define VKPT_SHADER_DIR "shader_vkpt"
#endif

#define SHADER_PATH_TEMPLATE VKPT_SHADER_DIR "/%s.spv"

#define SHADER_STAGE(_module, _stage) \
	{ \
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, \
		.stage = _stage, \
		.module = qvk.shader_modules[_module], \
		.pName = "main" \
	}

enum QVK_SHADER_MODULES {
#define SHADER_MODULE_DO(a) a,
	LIST_SHADER_MODULES
#undef SHADER_MODULE_DO
	NUM_QVK_SHADER_MODULES
};

#define MAX_FRAMES_IN_FLIGHT 2

enum {
	SEM_IMG_AVAILABLE   = 0,
	SEM_RENDER_FINISHED = SEM_IMG_AVAILABLE   + MAX_FRAMES_IN_FLIGHT,
	NUM_SEMAPHORES      = SEM_RENDER_FINISHED + MAX_FRAMES_IN_FLIGHT,
};

#define MAX_SWAPCHAIN_IMAGES 4

typedef struct QVK_s {
	VkInstance                  instance;
	VkPhysicalDevice            physical_device;
	VkPhysicalDeviceMemoryProperties mem_properties;
	VkDevice                    device;
	VkQueue                     queue_graphics;
	VkQueue                     queue_compute;
	VkQueue                     queue_transfer;
	int32_t                     queue_idx_graphics;
	int32_t                     queue_idx_compute;
	int32_t                     queue_idx_transfer;
	VkSurfaceKHR                surface;
	VkSwapchainKHR              swap_chain;
	VkSurfaceFormatKHR          surf_format;
	VkPresentModeKHR            present_mode;
	VkExtent2D                  extent;
	VkCommandPool               command_pool;
	uint32_t                    num_swap_chain_images;
	VkImage                     swap_chain_images[MAX_SWAPCHAIN_IMAGES];
	VkImageView                 swap_chain_image_views[MAX_SWAPCHAIN_IMAGES];

	uint32_t                    num_command_buffers;
	VkCommandBuffer             *command_buffers;
	VkCommandBuffer             cmd_buf_current;

	uint32_t                    num_extensions;
	VkExtensionProperties       *extensions;

	uint32_t                    num_layers;
	VkLayerProperties           *layers;

	VkDebugUtilsMessengerEXT    dbg_messenger;

	VkSemaphore                 semaphores[NUM_SEMAPHORES];
	VkFence                     fences_frame_sync[MAX_FRAMES_IN_FLIGHT];


	int                         win_width;
	int                         win_height;
	uint64_t                    frame_counter;

	SDL_Window                  *window;
	uint32_t                    num_sdl2_extensions;
	const char                  **sdl2_extensions;

	uint32_t                    current_image_index;

	VkShaderModule              shader_modules[NUM_QVK_SHADER_MODULES];

	VkDescriptorSetLayout       desc_set_layout_ubo;
	VkDescriptorSet             desc_set_ubo[MAX_SWAPCHAIN_IMAGES];

	VkDescriptorSetLayout       desc_set_layout_textures;
	VkDescriptorSet             desc_set_textures;
	VkImage                     images      [NUM_VKPT_IMAGES]; // todo: rename to make consistent
	VkImageView                 images_views[NUM_VKPT_IMAGES]; // todo: rename to make consistent

	VkDescriptorSetLayout       desc_set_layout_vertex_buffer;
	VkDescriptorSet             desc_set_vertex_buffer;

	VkDescriptorSetLayout       desc_set_layout_light_hierarchy;
	VkDescriptorSet             desc_set_light_hierarchy[MAX_SWAPCHAIN_IMAGES];

	BufferResource_t            buf_vertex;
	BufferResource_t            buf_vertex_staging;
} QVK_t;

extern QVK_t qvk;



#define _VK_EXTENSION_LIST \
	_VK_EXTENSION_DO(vkCreateAccelerationStructureNV) \
	_VK_EXTENSION_DO(vkCreateAccelerationStructureNV) \
	_VK_EXTENSION_DO(vkDestroyAccelerationStructureNV) \
	_VK_EXTENSION_DO(vkGetAccelerationStructureMemoryRequirementsNV) \
	_VK_EXTENSION_DO(vkBindAccelerationStructureMemoryNV) \
	_VK_EXTENSION_DO(vkCmdBuildAccelerationStructureNV) \
	_VK_EXTENSION_DO(vkCmdCopyAccelerationStructureNV) \
	_VK_EXTENSION_DO(vkCmdTraceRaysNV) \
	_VK_EXTENSION_DO(vkCreateRayTracingPipelinesNV) \
	_VK_EXTENSION_DO(vkGetRayTracingShaderGroupHandlesNV) \
	_VK_EXTENSION_DO(vkGetAccelerationStructureHandleNV) \
	_VK_EXTENSION_DO(vkCmdWriteAccelerationStructuresPropertiesNV) \
	_VK_EXTENSION_DO(vkCompileDeferredNV) \
	_VK_EXTENSION_DO(vkDebugMarkerSetObjectNameEXT)


#define _VK_EXTENSION_DO(a) extern PFN_##a q##a;
_VK_EXTENSION_LIST
#undef _VK_EXTENSION_DO

#define WM_MAX_VERTICES (1<<24)
typedef struct bsp_mesh_s {
	uint32_t world_idx_count;
	uint32_t *models_idx_offset;
	uint32_t *models_idx_count;
	vec3_t   *model_centers;
	int       num_models;

	uint32_t world_fluid_offset;
	uint32_t world_fluid_count;

	uint32_t world_light_offset;
	uint32_t world_light_count;

	float *positions, *tex_coords;
	int *indices;
	uint32_t *materials;
	int num_indices;
	int num_vertices;

	int num_clusters;
	int *clusters;

	int num_cluster_lights;
	int *cluster_light_offsets;
	int *cluster_lights;
} bsp_mesh_t;

void bsp_mesh_create_from_bsp(bsp_mesh_t *wm, bsp_t *bsp);
void bsp_mesh_destroy(bsp_mesh_t *wm);
void bsp_mesh_register_textures(bsp_t *bsp);

typedef struct vkpt_refdef_s {
	QVKUniformBuffer_t uniform_buffer;
	refdef_t *fd;
	float view_matrix[16];
	float projection_matrix[16];
	float view_projection_matrix[16];

	float view_matrix_prev[16];
	float projection_matrix_prev[16];
	float view_projection_matrix_prev[16];
	float z_near, z_far;

	bsp_mesh_t bsp_mesh_world;
	int bsp_mesh_world_loaded;

	uint32_t *light_colors;
	float *light_positions;
	int num_static_lights;
	int num_dynamic_lights;
} vkpt_refdef_t;

extern vkpt_refdef_t vkpt_refdef;

void mult_matrix_matrix(float *p, const float *a, const float *b);
void mult_matrix_vector(float *p, const float *a, const float *b);
void create_entity_matrix(float matrix[16], entity_t *e);
void create_projection_matrix(float matrix[16], float znear, float zfar, float fov_x, float fov_y);
void create_view_matrix(float matrix[16], refdef_t *fd);
void inverse(const float *m, float *inv);
void create_orthographic_matrix(float matrix[16], float xmin, float xmax,
		float ymin, float ymax, float znear, float zfar);

#define PROFILER_LIST \
	PROFILER_DO(PROFILER_FRAME_TIME,                 0) \
	PROFILER_DO(PROFILER_INSTANCE_GEOMETRY,          1) \
	PROFILER_DO(PROFILER_BVH_UPDATE,                 1) \
	PROFILER_DO(PROFILER_ASVGF_GRADIENT_SAMPLES,     1) \
	PROFILER_DO(PROFILER_PATH_TRACER,                1) \
	PROFILER_DO(PROFILER_ASVGF_FULL,                 1) \
	PROFILER_DO(PROFILER_ASVGF_RECONSTRUCT_GRADIENT, 2) \
	PROFILER_DO(PROFILER_ASVGF_TEMPORAL,             2) \
	PROFILER_DO(PROFILER_ASVGF_ATROUS,               2) \
	PROFILER_DO(PROFILER_ASVGF_TAA,                  2)

enum {
#define PROFILER_DO(a, ...) a,
	PROFILER_LIST
#undef PROFILER_DO
	NUM_PROFILER_ENTRIES,
};

#define NUM_PROFILER_QUERIES_PER_FRAME (NUM_PROFILER_ENTRIES * 2)

typedef enum {
	PROFILER_START, 
	PROFILER_STOP,
} VKPTProfilerAction;

VkResult vkpt_profiler_initialize();
VkResult vkpt_profiler_destroy();
VkResult vkpt_profiler_query(int idx, VKPTProfilerAction action);
VkResult vkpt_profiler_next_frame(int frame_num);
void draw_profiler();

VkResult vkpt_textures_initialize();
VkResult vkpt_textures_destroy();
VkResult vkpt_textures_end_registration();
VkResult vkpt_textures_upload_envmap(int w, int h, byte *data);

VkResult vkpt_draw_initialize();
VkResult vkpt_draw_destroy();
VkResult vkpt_draw_destroy_pipelines();
VkResult vkpt_draw_create_pipelines();
VkResult vkpt_draw_submit_stretch_pics(VkCommandBuffer *cmd_buf);
VkResult vkpt_draw_clear_stretch_pics();

VkResult vkpt_uniform_buffer_create();
VkResult vkpt_uniform_buffer_destroy();
VkResult vkpt_uniform_buffer_update();

VkResult vkpt_vertex_buffer_create();
VkResult vkpt_vertex_buffer_destroy();
VkResult vkpt_vertex_buffer_upload_bsp_mesh_to_staging(bsp_mesh_t *bsp_mesh);
VkResult vkpt_vertex_buffer_create_pipelines();
VkResult vkpt_vertex_buffer_destroy_pipelines();
VkResult vkpt_vertex_buffer_create_instance(uint32_t num_instances);
VkResult vkpt_vertex_buffer_upload_models_to_staging();
VkResult vkpt_vertex_buffer_upload_staging();

VkResult vkpt_lh_upload_staging();
VkResult vkpt_lh_update(const float *positions, const uint32_t *light_colors, int num_primitives, VkCommandBuffer cmd_buf);
VkResult vkpt_lh_initialize();
VkResult vkpt_lh_destroy();

VkResult vkpt_load_shader_modules();
VkResult vkpt_destroy_shader_modules();
VkResult vkpt_create_images();
VkResult vkpt_destroy_images();

VkResult vkpt_pt_init();
VkResult vkpt_pt_destroy();
VkResult vkpt_pt_create_pipelines();
VkResult vkpt_pt_destroy_pipelines();

VkResult vkpt_pt_create_toplevel(int idx);
VkResult vkpt_pt_create_static(VkBuffer vertex_buffer, size_t buffer_offset, int num_vertices);
VkResult vkpt_pt_destroy_static();
VkResult vkpt_pt_record_cmd_buffer(VkCommandBuffer cmd_buf, uint32_t frame_num);
VkResult vkpt_pt_update_descripter_set_bindings(int idx);
VkResult vkpt_pt_create_dynamic(int idx, VkBuffer vertex_buffer, size_t buffer_offset, int num_vertices);
VkResult vkpt_pt_destroy_dynamic(int idx);

VkResult vkpt_asvgf_initialize();
VkResult vkpt_asvgf_destroy();
VkResult vkpt_asvgf_create_pipelines();
VkResult vkpt_asvgf_destroy_pipelines();
VkResult vkpt_asvgf_record_cmd_buffer(VkCommandBuffer cmd_buf);
VkResult vkpt_asvgf_create_gradient_samples(VkCommandBuffer cmd_buf, uint32_t frame_num);

qerror_t load_img(const char *name, image_t *image);

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
    int             idx_offset;    /* offset in vertex buffer on device */
    int             vertex_offset; /* offset in vertex buffer on device */
    int             *indices;
    vec3_t          *positions;
    vec3_t          *normals;
    vec2_t          *tex_coords;
    image_t         *skins[MAX_ALIAS_SKINS];
    int             numskins;
} maliasmesh_t;

// needed for model.c
#define TESS_MAX_VERTICES   4096
#define TESS_MAX_INDICES    (3 * TESS_MAX_VERTICES)

#define QGL_INDEX_TYPE  GLuint

typedef struct {
    color_t     colors[2]; // 0 - actual color, 1 - transparency (for text drawing)
    float scale;
} drawStatic_t;

extern drawStatic_t draw;
extern cvar_t *cvar_rtx;

#endif  /*__VKPT_H__*/
