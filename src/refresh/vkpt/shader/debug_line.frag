/*
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.
Copyright (C) 2025, Frank Richter. All rights reserved.

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

// Fragment program for debug lines

#version 460
#extension GL_GOOGLE_include_directive    : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier    : enable

#define GLOBAL_UBO_DESC_SET_IDX 0
#include "global_ubo.h"

#define GLOBAL_TEXTURES_DESC_SET_IDX 1
#include "global_textures.h"

layout(location = 0) in vec4 line_color;
layout(location = 1) in vec3 view_pos;

layout(location = 0) out vec4 out_color;

void
main()
{
	// PT_VIEW_DEPTH_A is checkerboarded...
	ivec2 pixel_coord = ivec2(gl_FragCoord.xy * vec2(global_ubo.width / float(global_ubo.unscaled_width),
													 global_ubo.height / float(global_ubo.unscaled_height)));
	pixel_coord.x = pixel_coord.x / 2; // good enough?

	float view_depth = texelFetch(TEX_PT_VIEW_DEPTH_A, pixel_coord, 0).x;
	/* Debug line depth check.
	 * Preferably show debug lines on top of geometry, if they follow it closely,
	 * so add a bias to the "view depth" value. Scale bias with distance to account
	 * for limited view depth precision. */
	float dist = length(view_pos);
	float dist_log = log(dist) * 0.4342944819032518 /* 1/log(10) */;
	if (dist > 0 && dist > view_depth + pow(10, int(dist_log) - 2))
		discard;
	out_color = line_color;
}

