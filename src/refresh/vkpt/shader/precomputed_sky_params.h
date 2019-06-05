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

#ifndef _PRECOMPUTED_SKY_PARAMS_
#define _PRECOMPUTED_SKY_PARAMS_

#define SKY_LUM_SCALE 0.001f
#define SUN_LUM_SCALE 0.00001f

#define SKY_SPECTRAL_R_TO_L (683.000000f * SKY_LUM_SCALE)
#define SUN_SPECTRAL_R_TO_L_R (98242.786222f * SUN_LUM_SCALE)
#define SUN_SPECTRAL_R_TO_L_G (69954.398112f * SUN_LUM_SCALE)
#define SUN_SPECTRAL_R_TO_L_B (66475.012354f * SUN_LUM_SCALE)

#ifdef VKPT_SHADER

const vec3 SKY_SPECTRAL_RADIANCE_TO_LUMINANCE = vec3(SKY_SPECTRAL_R_TO_L, SKY_SPECTRAL_R_TO_L, SKY_SPECTRAL_R_TO_L);
const vec3 SUN_SPECTRAL_RADIANCE_TO_LUMINANCE = vec3(SUN_SPECTRAL_R_TO_L_R, SUN_SPECTRAL_R_TO_L_G, SUN_SPECTRAL_R_TO_L_B);

#endif

#endif // _PRECOMPUTED_SKY_PARAMS_
