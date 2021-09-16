/*
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.
Copyright (C) 2021, Frank Richter. All rights reserved.

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

layout(constant_id = 0) const uint spec_input_tex = 0;

#include "utils.glsl"

#define GLOBAL_UBO_DESC_SET_IDX 0
#include "global_ubo.h"

#define GLOBAL_TEXTURES_DESC_SET_IDX 1
#include "global_textures.h"

/* FSR sharpening (RCAS, "Robust Contrast Adaptive Sharpening") pass
 * Also see overview in fsr.c */

#define A_GPU 1
#define A_GLSL 1

// Those headers contain the bulk of the implementation
#include "ffx_a.h"
#include "ffx_fsr1.h"

layout(local_size_x=64) in;

// Provide input for RCAS
fsr_vec4 FsrRcasLoad(load_coord p)
{
	if(spec_input_tex == 0)
		// RCAS after EASU (default case)
		return fsr_vec4(texelFetch(TEX_FSR_EASU_OUTPUT, ivec2(p), 0));
	else
		// RCAS after TAAU (if EASU was disabled via cvar)
		return fsr_vec4(texelFetch(TEX_TAA_OUTPUT, ivec2(p), 0));
}
// Color space conversion
void FsrRcasInput(inout fsr_val r, inout fsr_val g, inout fsr_val b) {}


void main()
{
	/* Code from RCAS application has been lifted from:
	   https://raw.githubusercontent.com/GPUOpen-Effects/FidelityFX-FSR/master/docs/FidelityFX-FSR-Overview-Integration.pdf */

	// Do remapping of local xy in workgroup for a more PS-like swizzle pattern.
	AU2 gxy = ARmp8x8(gl_LocalInvocationID.x) + AU2(gl_WorkGroupID.x << 4u, gl_WorkGroupID.y << 4u);

	// Run the function four times, as recommended by the official docs
	fsr_vec3 color;
	FsrRcas(color.r, color.g, color.b, gxy, global_ubo.rcas_const0);
	imageStore(IMG_FSR_RCAS_OUTPUT, ivec2(gxy), vec4(color, 1));
	gxy.x += 8;

	FsrRcas(color.r, color.g, color.b, gxy, global_ubo.rcas_const0);
	imageStore(IMG_FSR_RCAS_OUTPUT, ivec2(gxy), vec4(color, 1));
	gxy.y += 8;

	FsrRcas(color.r, color.g, color.b, gxy, global_ubo.rcas_const0);
	imageStore(IMG_FSR_RCAS_OUTPUT, ivec2(gxy), vec4(color, 1));
	gxy.x -= 8;

	FsrRcas(color.r, color.g, color.b, gxy, global_ubo.rcas_const0);
	imageStore(IMG_FSR_RCAS_OUTPUT, ivec2(gxy), vec4(color, 1));
}
