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

#ifndef  _CONSTANTS_H_
#define  _CONSTANTS_H_

#define GRAD_DWN (3)

#define SHADOWMAP_SIZE 4096

#define HISTOGRAM_BINS 128

#define EMISSIVE_TRANSFORM_BIAS -0.001

#define MAX_MIRROR_ROUGHNESS 0.02

#define NUM_GLOBAL_TEXTUES 2048

#define NUM_BLUE_NOISE_TEX (128 * 4)
#define BLUE_NOISE_RES     (256)

#define NUM_LIGHT_STATS_BUFFERS 3

#define PRIMARY_RAY_T_MAX 10000

#define MAX_CAMERAS 8

#define MAX_FOG_VOLUMES 8

#define AA_MODE_OFF 0
#define AA_MODE_TAA 1
#define AA_MODE_UPSCALE 2

// Scaling factors for lighting components when they are stored in textures.
// FP16 and RGBE textures have very limited range, and these factors help bring the signal within that range.
#define STORAGE_SCALE_LF 1024
#define STORAGE_SCALE_HF 32
#define STORAGE_SCALE_SPEC 32
#define STORAGE_SCALE_HDR 128

#define MATERIAL_KIND_MASK           0xf0000000
#define MATERIAL_KIND_INVALID        0x00000000
#define MATERIAL_KIND_REGULAR        0x10000000
#define MATERIAL_KIND_CHROME         0x20000000
#define MATERIAL_KIND_WATER          0x30000000
#define MATERIAL_KIND_LAVA           0x40000000
#define MATERIAL_KIND_SLIME          0x50000000
#define MATERIAL_KIND_GLASS          0x60000000
#define MATERIAL_KIND_SKY            0x70000000
#define MATERIAL_KIND_INVISIBLE      0x80000000
#define MATERIAL_KIND_EXPLOSION      0x90000000
#define MATERIAL_KIND_TRANSPARENT    0xa0000000
#define MATERIAL_KIND_SCREEN         0xb0000000
#define MATERIAL_KIND_CAMERA         0xc0000000
#define MATERIAL_KIND_CHROME_MODEL   0xd0000000

#define MATERIAL_FLAG_LIGHT          0x08000000
#define MATERIAL_FLAG_HANDEDNESS     0x02000000
#define MATERIAL_FLAG_WEAPON         0x01000000
#define MATERIAL_FLAG_WARP           0x00800000
#define MATERIAL_FLAG_FLOWING        0x00400000
#define MATERIAL_FLAG_DOUBLE_SIDED   0x00200000
#define MATERIAL_FLAG_SHELL_RED      0x00100000
#define MATERIAL_FLAG_SHELL_GREEN    0x00080000
#define MATERIAL_FLAG_SHELL_BLUE     0x00040000

#define MATERIAL_LIGHT_STYLE_MASK    0x0003f000
#define MATERIAL_LIGHT_STYLE_SHIFT   12
#define MATERIAL_INDEX_MASK          0x00000fff

#define CHECKERBOARD_FLAG_PRIMARY    1
#define CHECKERBOARD_FLAG_REFLECTION 2
#define CHECKERBOARD_FLAG_REFRACTION 4

// Combines the PRIMARY, REFLECTION, REFRACTION fields
#define CHECKERBOARD_FLAG_FIELD_MASK 7 
// Not really a checkerboard flag, but it's stored in the same channel.
// Signals that the surface is a first-person weapon.
#define CHECKERBOARD_FLAG_WEAPON     8

#define MEDIUM_NONE  0
#define MEDIUM_WATER 1
#define MEDIUM_SLIME 2
#define MEDIUM_LAVA  3
#define MEDIUM_GLASS 4

#define ENVIRONMENT_NONE 0
#define ENVIRONMENT_STATIC 1
#define ENVIRONMENT_DYNAMIC 2

#define SHADER_MAX_ENTITIES                  1024
#define SHADER_MAX_BSP_ENTITIES              128
#define MAX_LIGHT_SOURCES                    32
#define MAX_LIGHT_STYLES                     64

#define TLAS_INDEX_GEOMETRY      0
#define TLAS_INDEX_EFFECTS       1
#define TLAS_COUNT               2

// Geometry TLAS flags
#define AS_FLAG_OPAQUE          (1 << 0)
#define AS_FLAG_TRANSPARENT     (1 << 1)
#define AS_FLAG_VIEWER_MODELS   (1 << 2)
#define AS_FLAG_VIEWER_WEAPON   (1 << 3)
#define AS_FLAG_SKY             (1 << 4)
#define AS_FLAG_CUSTOM_SKY      (1 << 5)

// Effects TLAS flags
#define AS_FLAG_EFFECTS         (1 << 0)

#define AS_INSTANCE_FLAG_DYNAMIC        (1 << 23)
#define AS_INSTANCE_FLAG_SKY            (1 << 22)
#define AS_INSTANCE_MASK_OFFSET (AS_INSTANCE_FLAG_SKY - 1)

#define RT_PAYLOAD_GEOMETRY      0
#define RT_PAYLOAD_EFFECTS       1

#define SBT_RGEN                 0
#define SBT_RMISS_EMPTY          1

#define SBT_RCHIT_GEOMETRY       2
#define SBT_RAHIT_MASKED         3

#define SBT_RCHIT_EFFECTS        4
#define SBT_RAHIT_PARTICLE       5
#define SBT_RAHIT_EXPLOSION      6
#define SBT_RAHIT_SPRITE         7
#define SBT_RINT_BEAM            8
#define SBT_ENTRIES_PER_PIPELINE 9
// vkpt_pt_create_pipelines() relies on all 'transparency' SBT entries coming after SBT_FIRST_TRANSPARENCY
#define SBT_FIRST_TRANSPARENCY SBT_RCHIT_EFFECTS

// SBT indices for geometry and shadow rays
#define SBTO_OPAQUE     (SBT_RCHIT_GEOMETRY - SBT_RCHIT_GEOMETRY)
#define SBTO_MASKED     (SBT_RAHIT_MASKED - SBT_RCHIT_GEOMETRY)
// SBT indices for effect rays
#define SBTO_PARTICLE   (SBT_RAHIT_PARTICLE - SBT_RCHIT_EFFECTS)
#define SBTO_EXPLOSION  (SBT_RAHIT_EXPLOSION - SBT_RCHIT_EFFECTS)
#define SBTO_SPRITE     (SBT_RAHIT_SPRITE - SBT_RCHIT_EFFECTS)
#define SBTO_BEAM       (SBT_RINT_BEAM - SBT_RCHIT_EFFECTS)

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#endif /*_CONSTANTS_H_*/
