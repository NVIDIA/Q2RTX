/*
Copyright (C) 2021, NVIDIA CORPORATION. All rights reserved.

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

#ifndef __FOG_H_
#define __FOG_H_

#include <shared/shared.h>

typedef struct
{
	vec3_t point_a;
	vec3_t point_b;
	vec3_t color;
	float half_extinction_distance;
	int softface; // 0 = none, 1 = xa, 2 = xb, 3 = ya, 4 = yb, 5 = za, 6 = zb
} fog_volume_t;

struct ShaderFogVolume;

void vkpt_fog_init(void);
void vkpt_fog_shutdown(void);
void vkpt_fog_reset(void);
void vkpt_fog_upload(struct ShaderFogVolume* dst);

#endif // __FOG_H_
