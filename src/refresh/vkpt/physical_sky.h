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

#ifndef __PHYSICAL_SKY__
#define __PHYSICAL_SKY__


#include "vkpt.h"

#include "shader/sky.h"

//
// physical sun & sky
//

// API

void vkpt_evaluate_sun_light(sun_light_t* light, const vec3_t sky_matrix[3], float time);

VkResult vkpt_physical_sky_initialize(void);
VkResult vkpt_physical_sky_destroy(void);
VkResult vkpt_physical_sky_beginRegistration(void);
VkResult vkpt_physical_sky_endRegistration(void);
VkResult vkpt_physical_sky_create_pipelines(void);
VkResult vkpt_physical_sky_destroy_pipelines(void);
VkResult vkpt_physical_sky_record_cmd_buffer(VkCommandBuffer cmd_buf);
VkResult vkpt_physical_sky_update_ubo(QVKUniformBuffer_t * ubo, const sun_light_t* light, bool render_world);
void vkpt_physical_sky_latch_local_time(void);
bool vkpt_physical_sky_needs_update(void);
void vkpt_next_sun_preset(void);

void InitialiseSkyCVars(void);

void UpdatePhysicalSkyCVars(void);

typedef struct PhysicalSkyDesc {
    vec3_t sunColor;       // sun color
    float sunAngularDiameter; // size of sun in sky
    vec3_t groundAlbedo;   // ground albedo
    uint32_t flags;
	int preset; // SkyPreset
} PhysicalSkyDesc_t;

PhysicalSkyDesc_t const * GetSkyPreset(uint16_t index);

void CalculateDirectionToSun(float DayOfYear, float TimeOfDay, float LatitudeDegrees, vec3_t result);

#endif // __PHYSICAL_SKY__

