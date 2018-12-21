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

#ifndef  _TEXTURES_H_
#define  _TEXTURES_H_

#define GRAD_DWN (3)

#define IMG_WIDTH  (qvk.extent.width)
#define IMG_HEIGHT (qvk.extent.height)

#define IMG_WIDTH_GRAD  (qvk.extent.width  / GRAD_DWN)
#define IMG_HEIGHT_GRAD (qvk.extent.height / GRAD_DWN)

/* These are images that are to be used as render targets and buffers, but not textures. */
#define LIST_IMAGES \
	IMG_DO(PT_VISBUF,             0, R32G32B32A32_SFLOAT, rgba32f, IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(PT_COLOR_A,            1, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(PT_COLOR_B,            2, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(PT_ALBEDO,             3, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(PT_MOTION,             4, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(PT_DEPTH_NORMAL_A,     5, R32G32_SFLOAT,       rg32f,   IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(PT_DEPTH_NORMAL_B,     6, R32G32_SFLOAT,       rg32f,   IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(PT_VIS_BUF,            7, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(ASVGF_HIST_COLOR,      8, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(ASVGF_HIST_MOMENTS_A,  9, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(ASVGF_HIST_MOMENTS_B, 10, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(ASVGF_ATROUS_PING,    11, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(ASVGF_ATROUS_PONG,    12, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(ASVGF_COLOR,          13, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(ASVGF_TAA_A,          14, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(ASVGF_TAA_B,          15, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(ASVGF_GRAD_A,         16, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH_GRAD, IMG_HEIGHT_GRAD) \
	IMG_DO(ASVGF_GRAD_B,         17, R16G16B16A16_SFLOAT, rgba16f, IMG_WIDTH_GRAD, IMG_HEIGHT_GRAD) \
	IMG_DO(ASVGF_GRAD_SMPL_POS,  18, R32_UINT,            r32ui,   IMG_WIDTH_GRAD, IMG_HEIGHT_GRAD) \
	IMG_DO(ASVGF_RNG_SEED_A,     19, R32_UINT,            r32ui,   IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(ASVGF_RNG_SEED_B,     20, R32_UINT,            r32ui,   IMG_WIDTH,      IMG_HEIGHT     ) \
	IMG_DO(ASVGF_POS_WS_FWD,     21, R32G32B32A32_SFLOAT, rgba32f, IMG_WIDTH_GRAD, IMG_HEIGHT_GRAD) \
	IMG_DO(ASVGF_VISBUF_FWD,     22, R32G32B32A32_SFLOAT, rgba32f, IMG_WIDTH_GRAD, IMG_HEIGHT_GRAD) \
	IMG_DO(DEBUG,                23, R32G32B32A32_SFLOAT, rgba32f, IMG_WIDTH,      IMG_HEIGHT     ) \

#define NUM_IMAGES 24 /* this really sucks but I don't know how to fix it
                         counting with enum does not work in GLSL */

// todo: make naming consistent!
#define GLOBAL_TEXTURES_TEX_ARR_BINDING_IDX  0
#define BINDING_OFFSET_IMAGES     (1 + GLOBAL_TEXTURES_TEX_ARR_BINDING_IDX)
#define BINDING_OFFSET_TEXTURES   (BINDING_OFFSET_IMAGES + NUM_IMAGES)
#define BINDING_OFFSET_BLUE_NOISE (BINDING_OFFSET_TEXTURES + NUM_IMAGES)
#define BINDING_OFFSET_ENVMAP     (BINDING_OFFSET_BLUE_NOISE + 1)


#define NUM_GLOBAL_TEXTUES 1024

#define NUM_BLUE_NOISE_TEX (128 * 4)
#define BLUE_NOISE_RES     (256)

#define BSP_FLAG_LIGHT         (1 << 31)
#define BSP_FLAG_WATER         (1 << 30)
#define BSP_FLAG_TRANSPARENT   (1 << 29)
#define BSP_TEXTURE_MASK (BSP_FLAG_TRANSPARENT - 1)


#ifndef VKPT_SHADER
/***************************************************************************/
/* HOST CODE                                                               */
/***************************************************************************/

#if MAX_RIMAGES != NUM_GLOBAL_TEXTUES
#error need to fix the constant here as well
#endif


enum QVK_IMAGES {
#define IMG_DO(_name, ...) \
	VKPT_IMG_##_name,
	LIST_IMAGES
#undef IMG_DO
	NUM_VKPT_IMAGES
};

typedef char compile_time_check_num_images[(NUM_IMAGES == NUM_VKPT_IMAGES)*2-1];

#else
/***************************************************************************/
/* SHADER CODE                                                             */
/***************************************************************************/

/* general texture array for world, etc */
layout(
	set = GLOBAL_TEXTURES_DESC_SET_IDX,
	binding = GLOBAL_TEXTURES_TEX_ARR_BINDING_IDX
) uniform sampler2D global_texture_descriptors[];

#define SAMPLER_r32ui   usampler2D
#define SAMPLER_rg32f   sampler2D
#define SAMPLER_rgba32f sampler2D
#define SAMPLER_rgba16f sampler2D

#define IMAGE_r32ui   uimage2D
#define IMAGE_rg32f   image2D
#define IMAGE_rgba32f image2D
#define IMAGE_rgba16f image2D

/* framebuffer images */
#define IMG_DO(_name, _binding, _vkformat, _glslformat, _w, _h) \
	layout(set = GLOBAL_TEXTURES_DESC_SET_IDX, binding = BINDING_OFFSET_IMAGES + _binding, _glslformat) \
	uniform IMAGE_##_glslformat IMG_##_name;
LIST_IMAGES
#undef IMG_DO

/* framebuffer textures */
#define IMG_DO(_name, _binding, _vkformat, _glslformat, _w, _h) \
	layout(set = GLOBAL_TEXTURES_DESC_SET_IDX, binding = BINDING_OFFSET_TEXTURES + _binding) \
	uniform SAMPLER_##_glslformat TEX_##_name;
LIST_IMAGES
#undef IMG_DO

layout(
	set = GLOBAL_TEXTURES_DESC_SET_IDX,
	binding = BINDING_OFFSET_BLUE_NOISE
) uniform sampler2DArray TEX_BLUE_NOISE;

layout(
	set = GLOBAL_TEXTURES_DESC_SET_IDX,
	binding = BINDING_OFFSET_ENVMAP
) uniform samplerCube TEX_ENVMAP;

vec4
global_texture(uint idx, vec2 tex_coord)
{
	idx &= BSP_TEXTURE_MASK;
	if(idx >= NUM_GLOBAL_TEXTUES)
		return vec4(1, 0, 1, 0);
	return texture(global_texture_descriptors[nonuniformEXT(idx)], tex_coord);
}

vec4
global_textureLod(uint idx, vec2 tex_coord, uint lod)
{
	idx &= BSP_TEXTURE_MASK;
	if(idx >= NUM_GLOBAL_TEXTUES)
		return vec4(1, 1, 0, 0);
	return textureLod(global_texture_descriptors[nonuniformEXT(idx)], tex_coord, lod);
}

vec4
global_textureGrad(uint idx, vec2 tex_coord, vec2 d_x, vec2 d_y)
{
	idx &= BSP_TEXTURE_MASK;
	if(idx >= NUM_GLOBAL_TEXTUES)
		return vec4(1, 1, 0, 0);
	return textureGrad(global_texture_descriptors[nonuniformEXT(idx)], tex_coord, d_x, d_y);
}

ivec2
global_textureSize(uint idx, int level)
{
	idx &= BSP_TEXTURE_MASK;
	if(idx >= NUM_GLOBAL_TEXTUES)
		return ivec2(0);
	return textureSize(global_texture_descriptors[nonuniformEXT(idx)], level);
}


#endif


#endif /*_TEXTURES_H_*/
// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
