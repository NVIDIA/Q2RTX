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
#extension GL_GOOGLE_include_directive    : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier    : enable

#define GLOBAL_TEXTURES_DESC_SET_IDX 1
#include "global_textures.h"

layout(location = 0) in vec4 color;
layout(location = 1) in flat uint tex_id;
layout(location = 2) in vec2 tex_coord;

layout(location = 0) out vec4 outColor;

void
main()
{
	vec4 c = color;
	if(tex_id != ~0u) {
		vec2 tc = tex_coord;
		c *= global_textureLod(tex_id, tc, 0);
	}
	outColor = c;
}
