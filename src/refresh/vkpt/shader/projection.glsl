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

bool projection_view_to_screen(vec3 view_pos, out vec2 screen_pos, out float distance)
{
	if(global_ubo.cylindrical_hfov > 0)
	{
		float y = view_pos.y / length(view_pos.xz);
		y *= global_ubo.P[1][1];
		screen_pos.y = y * 0.5 + 0.5;

		float angle = atan(view_pos.x, view_pos.z);
		screen_pos.x = (angle / global_ubo.cylindrical_hfov) + 0.5;

		distance = length(view_pos);

		return screen_pos.y > 0 && screen_pos.y < 1 && screen_pos.x > 0 && screen_pos.x < 1;
	}
	else
	{
		vec4 clip_pos = global_ubo.P * vec4(view_pos, 1);

		vec3 normalized = clip_pos.xyz / clip_pos.w;
		screen_pos.xy = normalized.xy * 0.5 + vec2(0.5);
		distance = length(view_pos);

		return screen_pos.y > 0 && screen_pos.y < 1 && screen_pos.x > 0 && screen_pos.x < 1 && view_pos.z > 0;
	}
}

vec3 projection_screen_to_view(vec2 screen_pos, float distance)
{
	if(global_ubo.cylindrical_hfov > 0)
	{
		vec4 clip_pos = vec4(0, screen_pos.y * 2.0 - 1.0, 1, 1);
		vec3 view_dir = (global_ubo.invP * clip_pos).xyz;

		float xangle = (screen_pos.x - 0.5) * global_ubo.cylindrical_hfov;
		view_dir.x = sin(xangle);
		view_dir.z = cos(xangle);

		view_dir = normalize(view_dir);

		return view_dir * distance;
	}
	else
	{
		vec4 clip_pos = vec4(screen_pos.xy * 2.0 - vec2(1.0), 1, 1);
		vec3 view_dir = normalize((global_ubo.invP * clip_pos).xyz);

		return view_dir * distance;
	}
}