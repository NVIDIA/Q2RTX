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

#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
	vec4 gl_Position;
};

layout(location = 0) out vec4 color;
layout(location = 1) out flat uint tex_id;
layout(location = 2) out vec2 tex_coord;

struct StretchPic {
	float x, y, w,   h;
	float s, t, w_s, h_t;
	uint color, tex_handle;
};

layout(set = 0, binding = 0) buffer SBO {
	StretchPic stretch_pics[];
};

vec2 positions[4] = vec2[](
	vec2(0.0, 1.0),
	vec2(0.0, 0.0),
	vec2(1.0, 1.0),
	vec2(1.0, 0.0)
);

void
main()
{
	StretchPic sp = stretch_pics[gl_InstanceIndex];
	vec2 pos      = positions[gl_VertexIndex] * vec2(sp.w, sp.h) + vec2(sp.x, sp.y);
	color         = unpackUnorm4x8(sp.color);
	tex_coord     = vec2(sp.s, sp.t) + positions[gl_VertexIndex] * vec2(sp.w_s, sp.h_t);
	tex_id        = sp.tex_handle;

	gl_Position = vec4(pos, 0.0, 1.0);
}

