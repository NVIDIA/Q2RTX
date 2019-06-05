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

#ifndef _SHADER_PHYSICAL_SKY_
#define _SHADER_PHYSICAL_SKY_

#define PHYSICAL_SKY_FLAG_NONE            (0)

#define PHYSICAL_SKY_FLAG_USE_SKYBOX      (1 << 0)
#define PHYSICAL_SKY_FLAG_DRAW_MOUNTAINS  (1 << 1)
#define PHYSICAL_SKY_FLAG_DRAW_CLOUDS     (1 << 2)

#define PHYSICAL_SKY_MASK_OFFSET (PHYSICAL_SKY_FLAG_DRAW_CLOUDS - 1)


#ifndef VKPT_SHADER



#else
bool useSkybox() {
    return (global_ubo.physical_sky_flags & PHYSICAL_SKY_FLAG_USE_SKYBOX) != 0;
}

bool drawMountains() {
    return (global_ubo.physical_sky_flags & PHYSICAL_SKY_FLAG_DRAW_MOUNTAINS) != 0;
}

bool drawClouds() {
    return (global_ubo.physical_sky_flags & PHYSICAL_SKY_FLAG_DRAW_CLOUDS) != 0;
}
#endif

#endif // _SHADER_PHYSICAL_SKY_
