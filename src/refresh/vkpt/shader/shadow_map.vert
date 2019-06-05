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

// ========================================================================== //
// Vertex shader for shadow map rendering. There is no pixel shader.
// Multiplies world position by a matrix that is passed through push constants.
// ========================================================================== //

#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
	vec4 gl_Position;
};

layout(location = 0) in vec3 world_pos;

layout(push_constant, std140) uniform ShadowMapConstants {
	mat4 view_projection_matrix;
} push;

void
main()
{
	gl_Position = push.view_projection_matrix * vec4(world_pos, 1);
	gl_Position.y = -gl_Position.y;
}

