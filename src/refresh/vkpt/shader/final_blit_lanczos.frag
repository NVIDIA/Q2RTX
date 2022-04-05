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

// ========================================================================== //
// Pixel shader that samples the input texture using a Lanczos filter.
// Applied for the final blit when the resolution scale is not 100%.
// ========================================================================== //

#version 450
#extension GL_GOOGLE_include_directive    : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier    : enable

#include "utils.glsl"

#define GLOBAL_UBO_DESC_SET_IDX 0
#include "global_ubo.h"

#define GLOBAL_TEXTURES_DESC_SET_IDX 1
#include "global_textures.h"

layout(location = 0) in vec2 tex_coord;
layout(location = 0) out vec4 outColor;

// semi-vector form of the ternary operator: (f == val) ? eq : neq
vec2 v_sel(vec2 f, float val, float eq, vec2 neq)
{
    vec2 result;
    result.x = (f.x == val) ? eq : neq.x;
    result.y = (f.y == val) ? eq : neq.y;
    return result;
}

vec3 filter_lanczos(sampler2D img, vec2 uv)
{
	ivec2 size = textureSize(img, 0);
    
    // Lanczos 3
    vec2 UV = uv.xy * size;
    vec2 tc = floor(UV - 0.5) + 0.5;
    vec2 f = UV - tc + 2;

    // compute at f, f-1, f-2, f-3, f-4, and f-5 using trig angle addition
    vec2 fpi = f * M_PI, fpi3 = f * (M_PI / 3.0);
    vec2 sinfpi = sin(fpi), sinfpi3 = sin(fpi3), cosfpi3 = cos(fpi3);
    const float r3 = sqrt(3.0);
    vec2 w0 = v_sel(f, 0, M_PI * M_PI * 1.0 / 3.0, (sinfpi *       sinfpi3) / (f       * f));
    vec2 w1 = v_sel(f, 1, M_PI * M_PI * 2.0 / 3.0, (-sinfpi * (sinfpi3 - r3 * cosfpi3)) / ((f - 1.0)*(f - 1.0)));
    vec2 w2 = v_sel(f, 2, M_PI * M_PI * 2.0 / 3.0, (sinfpi * (-sinfpi3 - r3 * cosfpi3)) / ((f - 2.0)*(f - 2.0)));
    vec2 w3 = v_sel(f, 3, M_PI * M_PI * 2.0 / 3.0, (-sinfpi * (-2.0*sinfpi3)) / ((f - 3.0)*(f - 3.0)));
    vec2 w4 = v_sel(f, 4, M_PI * M_PI * 2.0 / 3.0, (sinfpi * (-sinfpi3 + r3 * cosfpi3)) / ((f - 4.0)*(f - 4.0)));
    vec2 w5 = v_sel(f, 5, M_PI * M_PI * 2.0 / 3.0, (-sinfpi * (sinfpi3 + r3 * cosfpi3)) / ((f - 5.0)*(f - 5.0)));

    // use bilinear texture weights to merge center two samples in each dimension
    vec2 Weight[5];
    Weight[0] = w0;
    Weight[1] = w1;
    Weight[2] = w2 + w3;
    Weight[3] = w4;
    Weight[4] = w5;

    vec2 invTextureSize = 1.0 / vec2(size);

    vec2 Sample[5];
    Sample[0] = invTextureSize * (tc - 2);
    Sample[1] = invTextureSize * (tc - 1);
    Sample[2] = invTextureSize * (tc + w3 / Weight[2]);
    Sample[3] = invTextureSize * (tc + 2);
    Sample[4] = invTextureSize * (tc + 3);

    vec4 o_rgba = vec4(0);

    // 5x5 footprint with corners dropped to give 13 texture taps
    o_rgba += vec4(textureLod(img, vec2(Sample[0].x, Sample[2].y), 0).rgb, 1.0) * Weight[0].x * Weight[2].y;
    o_rgba += vec4(textureLod(img, vec2(Sample[1].x, Sample[1].y), 0).rgb, 1.0) * Weight[1].x * Weight[1].y;
    o_rgba += vec4(textureLod(img, vec2(Sample[1].x, Sample[2].y), 0).rgb, 1.0) * Weight[1].x * Weight[2].y;
    o_rgba += vec4(textureLod(img, vec2(Sample[1].x, Sample[3].y), 0).rgb, 1.0) * Weight[1].x * Weight[3].y;
    o_rgba += vec4(textureLod(img, vec2(Sample[2].x, Sample[0].y), 0).rgb, 1.0) * Weight[2].x * Weight[0].y;
    o_rgba += vec4(textureLod(img, vec2(Sample[2].x, Sample[1].y), 0).rgb, 1.0) * Weight[2].x * Weight[1].y;
    o_rgba += vec4(textureLod(img, vec2(Sample[2].x, Sample[2].y), 0).rgb, 1.0) * Weight[2].x * Weight[2].y;
    o_rgba += vec4(textureLod(img, vec2(Sample[2].x, Sample[3].y), 0).rgb, 1.0) * Weight[2].x * Weight[3].y;
    o_rgba += vec4(textureLod(img, vec2(Sample[2].x, Sample[4].y), 0).rgb, 1.0) * Weight[2].x * Weight[4].y;
    o_rgba += vec4(textureLod(img, vec2(Sample[3].x, Sample[1].y), 0).rgb, 1.0) * Weight[3].x * Weight[1].y;
    o_rgba += vec4(textureLod(img, vec2(Sample[3].x, Sample[2].y), 0).rgb, 1.0) * Weight[3].x * Weight[2].y;
    o_rgba += vec4(textureLod(img, vec2(Sample[3].x, Sample[3].y), 0).rgb, 1.0) * Weight[3].x * Weight[3].y;
    o_rgba += vec4(textureLod(img, vec2(Sample[4].x, Sample[2].y), 0).rgb, 1.0) * Weight[4].x * Weight[2].y;

    return o_rgba.rgb / o_rgba.w;
}

void
main()
{
	vec3 color;

    vec2 uv = tex_coord * vec2(global_ubo.width, global_ubo.height) / vec2(global_ubo.taa_image_width, global_ubo.taa_image_height);

	color = filter_lanczos(TEX_TAA_OUTPUT, uv);
	
	outColor = vec4(color, 1);
}
