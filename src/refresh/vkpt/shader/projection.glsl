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

void view_to_lonlat(vec3 view, out float lon, out float lat)
{
	lon = atan(view.x, view.z);
	lat = atan(-view.y, sqrt(view.x * view.x + view.z * view.z));
}

void lonlat_to_view(float lon, float lat, out vec3 view)
{
	view.x = sin(lon) * cos(lat);
	view.y = -sin(lat);
	view.z = cos(lon) * cos(lat);
}

bool rectilinear_forward(vec3 view_pos, out vec2 screen_pos, out float distance, bool previous)
{
	vec4 clip_pos;
	if (previous)
		clip_pos = global_ubo.P_prev * vec4(view_pos, 1);
	else
		clip_pos = global_ubo.P * vec4(view_pos, 1);

	vec3 normalized = clip_pos.xyz / clip_pos.w;
	screen_pos.xy = normalized.xy * 0.5 + vec2(0.5);
	distance = length(view_pos);

	return screen_pos.y > 0 && screen_pos.y < 1 && screen_pos.x > 0 && screen_pos.x < 1 && view_pos.z > 0;
}

vec3 rectlinear_reverse(vec2 screen_pos, float distance, bool previous)
{
	vec4 clip_pos = vec4(screen_pos.xy * 2.0 - vec2(1.0), 1, 1);
	vec3 view_dir;
	if (previous)
		view_dir = normalize((global_ubo.invP_prev * clip_pos).xyz);
	else
		view_dir = normalize((global_ubo.invP * clip_pos).xyz);

	return view_dir * distance;
}

bool cylindrical_forward(vec3 view_pos, out vec2 screen_pos, out float distance, bool previous)
{
	float cylindrical_hfov = previous ? global_ubo.cylindrical_hfov_prev : global_ubo.cylindrical_hfov;
	float y = view_pos.y / length(view_pos.xz);
	if (previous)
		y *= global_ubo.P_prev[1][1];
	else
		y *= global_ubo.P[1][1];
	screen_pos.y = y * 0.5 + 0.5;

	float angle = atan(view_pos.x, view_pos.z);
	screen_pos.x = (angle / cylindrical_hfov) + 0.5;

	distance = length(view_pos);

	return screen_pos.y > 0 && screen_pos.y < 1 && screen_pos.x > 0 && screen_pos.x < 1;
}

vec3 cylindrical_reverse(vec2 screen_pos, float distance, bool previous)
{
	float cylindrical_hfov = previous ? global_ubo.cylindrical_hfov_prev : global_ubo.cylindrical_hfov;
	vec4 clip_pos = vec4(0, screen_pos.y * 2.0 - 1.0, 1, 1);
	vec3 view_dir;
	if (previous)
		view_dir = (global_ubo.invP_prev * clip_pos).xyz;
	else
		view_dir = (global_ubo.invP * clip_pos).xyz;

	float xangle = (screen_pos.x - 0.5) * cylindrical_hfov;
	view_dir.x = sin(xangle);
	view_dir.z = cos(xangle);

	view_dir = normalize(view_dir);

	return view_dir * distance;
}

vec2 get_projection_fov_scale(bool previous)
{
	return previous ? global_ubo.projection_fov_scale_prev : global_ubo.projection_fov_scale;
}

bool equirectangular_forward(vec3 view_pos, out vec2 screen_pos, out float distance, bool previous)
{
	float lat, lon;
	distance = length(view_pos);
	view_pos = normalize(view_pos);
	view_to_lonlat(view_pos, lon, lat);
	screen_pos.x = lon;
	screen_pos.y = lat;
	screen_pos = screen_pos / get_projection_fov_scale(previous) * 0.5 + 0.5;
	return true;
}

vec3 equirectangular_reverse(vec2 screen_pos, float distance, bool previous)
{
	vec3 view_dir;
	screen_pos = (screen_pos * 2.0 - 1.0) * get_projection_fov_scale(previous);
	float x = screen_pos.x;
	float y = screen_pos.y;
	lonlat_to_view(x, y, view_dir);
	return view_dir * distance;
}

bool mercator_forward(vec3 view_pos, out vec2 screen_pos, out float distance, bool previous)
{
	float lat, lon;
	distance = length(view_pos);
	view_pos = normalize(view_pos);
	view_to_lonlat(view_pos, lon, lat);
	screen_pos.x = lon;
	screen_pos.y = log(tan(M_PI * 0.25 + lat * 0.5));
	screen_pos = screen_pos / get_projection_fov_scale(previous) * 0.5 + 0.5;
	return true;
}

vec3 mercator_reverse(vec2 screen_pos, float distance, bool previous)
{
	vec3 view_dir;
	screen_pos = (screen_pos * 2.0 - 1.0) * get_projection_fov_scale(previous);
	float x = screen_pos.x;
	float y = screen_pos.y;
	float lon = x;
	float lat = atan(sinh(y));
	lonlat_to_view(lon, lat, view_dir);
	return view_dir * distance;
}

bool stereographic_forward(vec3 view_pos, out vec2 screen_pos, out float distance, bool previous)
{
	distance = length(view_pos);
	view_pos = normalize(view_pos);
	float x = view_pos.x;
	float y = -view_pos.y;
	float z = view_pos.z;
	float theta = acos(z);
	if (theta == 0.0)
	{
		screen_pos = vec2(0.5, 0.5);
	}
	else
	{
		float r = tan(theta * STEREOGRAPHIC_ANGLE);
		float c = r / sqrt(x * x + y * y);
		screen_pos.x = x * c;
		screen_pos.y = y * c;
	}
	screen_pos = screen_pos / get_projection_fov_scale(previous) * 0.5 + 0.5;
	return true;
}

vec3 stereographic_reverse(vec2 screen_pos, float distance, bool previous)
{
	vec3 view_dir;
	screen_pos = (screen_pos * 2.0 - 1.0) * get_projection_fov_scale(previous);
	float x = screen_pos.x;
	float y = screen_pos.y;
	float r = sqrt(x * x + y * y);
	float theta = atan(r) / STEREOGRAPHIC_ANGLE;
	float s = sin(theta);
	view_dir.x = x / r * s;
	view_dir.y = -y / r * s;
	view_dir.z = cos(theta);
	return view_dir * distance;
}

bool panini_forward(vec3 view_pos, out vec2 screen_pos, out float distance, bool previous)
{
	float lat, lon;
	distance = length(view_pos);
	view_pos = normalize(view_pos);
	view_to_lonlat(view_pos, lon, lat);
	float S = (PANINI_D + 1.0) / (PANINI_D + cos(lon));
	screen_pos.x = S * sin(lon);
	screen_pos.y = S * tan(lat);
	screen_pos = screen_pos / get_projection_fov_scale(previous) * 0.5 + 0.5;
	return true;
}

vec3 panini_reverse(vec2 screen_pos, float distance, bool previous)
{
	const float c_D = PANINI_D;		// Stereo
	vec3 view_dir;
	screen_pos = (screen_pos * 2.0 - 1.0) * get_projection_fov_scale(previous);
	float k = screen_pos.x * screen_pos.x / ((c_D + 1.0) * (c_D + 1.0));
	float dscr = k * k * c_D * c_D - (k + 1) * (k * c_D * c_D - 1.0);
	float clon = (-k * c_D + sqrt(dscr)) / (k + 1.0);
	float S = (c_D + 1.0) / (c_D + clon);
	float lon = atan(screen_pos.x, S * clon);
	float lat = atan(screen_pos.y, S);
	lonlat_to_view(lon, lat, view_dir);
	return view_dir * distance;
}


bool projection_view_to_screen(vec3 view_pos, out vec2 screen_pos, out float distance, bool previous)
{
	switch (global_ubo.pt_projection)
	{
	default:
	case PROJECTION_RECTILINEAR:
		rectilinear_forward(view_pos, screen_pos, distance, previous); break;
	case PROJECTION_PANINI:
		panini_forward(view_pos, screen_pos, distance, previous); break;
	case PROJECTION_STEREOGRAPHIC:
		stereographic_forward(view_pos, screen_pos, distance, previous); break;
	case PROJECTION_CYLINDRICAL:
		cylindrical_forward(view_pos, screen_pos, distance, previous); break;
	case PROJECTION_EQUIRECTANGULAR:
		equirectangular_forward(view_pos, screen_pos, distance, previous); break;
	case PROJECTION_MERCATOR:
		mercator_forward(view_pos, screen_pos, distance, previous); break;
	}
	return true;
}

vec3 projection_screen_to_view(vec2 screen_pos, float distance, bool previous)
{
	switch (global_ubo.pt_projection)
	{
	default:
	case PROJECTION_RECTILINEAR:
		return rectlinear_reverse(screen_pos, distance, previous);
	case PROJECTION_PANINI:
		return panini_reverse(screen_pos, distance, previous);
	case PROJECTION_STEREOGRAPHIC:
		return stereographic_reverse(screen_pos, distance, previous);
	case PROJECTION_CYLINDRICAL:
		return cylindrical_reverse(screen_pos, distance, previous);
	case PROJECTION_EQUIRECTANGULAR:
		return equirectangular_reverse(screen_pos, distance, previous);
	case PROJECTION_MERCATOR:
		return mercator_reverse(screen_pos, distance, previous);
	}
}