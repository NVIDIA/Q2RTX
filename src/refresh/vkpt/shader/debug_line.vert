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

// Vertex program for debug lines

#version 460
#extension GL_GOOGLE_include_directive    : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier    : enable

out gl_PerVertex {
	vec4 gl_Position;
};

layout(location = 0) in vec3 in_view_pos;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec3 out_viewpos;

#define GLOBAL_UBO_DESC_SET_IDX 0
#include "global_ubo.h"

#include "projection.glsl"

void
main()
{
	vec3 view_pos = in_view_pos;
	bool depth_test = view_pos.z >= 0;
	view_pos.z = abs(view_pos.z);
	vec2 screen_pos;
	float dist;
	projection_view_to_screen(view_pos, screen_pos, dist, false);
	screen_pos = screen_pos * 2 + vec2(-1, -1);
	if (depth_test) {
		gl_Position = vec4(screen_pos.x * view_pos.z, screen_pos.y * view_pos.z, 1, view_pos.z);
		out_viewpos = in_view_pos;
	} else {
		gl_Position = vec4(screen_pos.x, screen_pos.y, 0, 1);
		out_viewpos = vec3(0);
	}
	out_color = in_color;
}

