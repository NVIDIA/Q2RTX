/*
Copyright (C) 2018 Christoph Schied
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

#ifndef _GLSL_UTILS_GLSL
#define _GLSL_UTILS_GLSL

#include "constants.h"

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

float
linear_to_srgb(float linear)
{
	return (linear <= 0.0031308f)
		? linear * 12.92f
		: pow(linear, (1.0f / 2.4f)) * (1.055f) - 0.055f;
}

vec3
linear_to_srgb(vec3 l)
{
	return vec3(
			linear_to_srgb(l.x),
			linear_to_srgb(l.y),
			linear_to_srgb(l.z));
}

float
luminance(in vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}

float
clamped_luminance(vec3 c)
{
    float l = luminance(c);
    return min(l, 150.0);
}

vec3
clamp_color(vec3 color, float max_val)
{
    float m = max(color.r, max(color.g, color.b));
    if(m < max_val)
        return color;

    return color * (max_val / m);
}

vec3
decode_normal(uint enc)
{
    // Decode RG16_UNORM
    uvec2 u = uvec2(enc & 0xffffu, enc >> 16);
    vec2 p = vec2(u) / float(0xffff);

    // Convert to [-1..1]
    p = p * 2.0 - 1.0;
    
    // Decode the octahedron
    // https://twitter.com/Stubbesaurus/status/937994790553227264
    vec3 n = vec3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));
    float t = max(0, -n.z);
    n.xy += mix(vec2(t), vec2(-t), greaterThanEqual(n.xy, vec2(0)));
    
    return normalize(n);
}

uint
encode_normal(vec3 normal)
{
    // Project the sphere onto the octahedron (|x|+|y|+|z| = 1) and then onto the xy-plane
    float invL1Norm = 1.0 / (abs(normal.x) + abs(normal.y) + abs(normal.z));
    vec2 p = normal.xy * invL1Norm;

    // Wrap the octahedral faces from the negative-Z space
    p = (normal.z < 0) ? (1.0 - abs(p.yx)) * mix(vec2(-1.0), vec2(1.0), greaterThanEqual(p.xy, vec2(0))) : p;

    // Convert to [0..1]
    p = clamp(p.xy * 0.5 + 0.5, vec2(0), vec2(1));

    // Encode as RG16_UNORM
    uvec2 u = uvec2(p * 0xffffu);
    return u.x | (u.y << 16);
}

float
reconstruct_view_depth(float depth, float near, float far)
{
    float z_ndc = 2.0 * depth - 1.0;
    return 2.0 * near * far / (near + far - z_ndc * (far - near));
}

// Catmull-Rom filtering code from http://vec3.ca/bicubic-filtering-in-fewer-taps/
void
BicubicCatmullRom(vec2 UV, vec2 texSize, out vec2 Sample[3], out vec2 Weight[3])
{
    const vec2 invTexSize = 1.0 / texSize;

    vec2 tc = floor( UV - 0.5 ) + 0.5;
    vec2 f = UV - tc;
    vec2 f2 = f * f;
    vec2 f3 = f2 * f;

    vec2 w0 = f2 - 0.5 * (f3 + f);
    vec2 w1 = 1.5 * f3 - 2.5 * f2 + 1;
    vec2 w3 = 0.5 * (f3 - f2);
    vec2 w2 = 1 - w0 - w1 - w3;

    Weight[0] = w0;
    Weight[1] = w1 + w2;
    Weight[2] = w3;

    Sample[0] = tc - 1;
    Sample[1] = tc + w2 / Weight[1];
    Sample[2] = tc + 2;

    Sample[0] *= invTexSize;
    Sample[1] *= invTexSize;
    Sample[2] *= invTexSize;
}

/* uv is in pixel coordinates */
vec4
sample_texture_catmull_rom(sampler2D tex, vec2 uv)
{
    vec4 sum = vec4(0);
    vec2 sampleLoc[3], sampleWeight[3];
    BicubicCatmullRom(uv, vec2(textureSize(tex, 0)), sampleLoc, sampleWeight);
    for(int i = 0; i < 3; i++) {
        for(int j = 0; j < 3; j++) {
            vec2 uv = vec2(sampleLoc[j].x, sampleLoc[i].y);
            vec4 c = textureLod(tex, uv, 0);
            sum += c * vec4(sampleWeight[j].x * sampleWeight[i].y);        
        }
    }
    return sum;
}

float
srgb_to_linear(float srgb)
{
    if(srgb <= 0.04045f) {
        return srgb * (1.0f / 12.92f);
    }
    else {
        return pow((srgb + 0.055f) * (1.0f / 1.055f), 2.4f);
    }
}

vec3
srgb_to_linear(vec3 srgb)
{
    return vec3(
            srgb_to_linear(srgb.x),
            srgb_to_linear(srgb.y),
            srgb_to_linear(srgb.z));
}

vec3
viridis_quintic(float x)
{
    x = clamp(x, 0.0, 1.0);
    vec4 x1 = vec4(1.0, x, x * x, x * x * x); // 1 x x2 x3
    vec4 x2 = x1 * x1.w * x; // x4 x5 x6 x7
    return srgb_to_linear(vec3(
        dot(x1.xyzw, vec4(+0.280268003, -0.143510503, +2.225793877,  -14.815088879)) + dot(x2.xy, vec2(+25.212752309, -11.772589584)),
        dot(x1.xyzw, vec4(-0.002117546, +1.617109353, -1.909305070,  +2.701152864 )) + dot(x2.xy, vec2(-1.685288385,  +0.178738871 )),
        dot(x1.xyzw, vec4(+0.300805501, +2.614650302, -12.019139090, +28.933559110)) + dot(x2.xy, vec2(-33.491294770, +13.762053843))));
}


vec3
compute_barycentric(mat3 v, vec3 ray_origin, vec3 ray_direction)
{
	vec3 edge1 = v[1] - v[0];
	vec3 edge2 = v[2] - v[0];

	vec3 pvec = cross(ray_direction, edge2);

	float det = dot(edge1, pvec);
	float inv_det = 1.0f / det;

	vec3 tvec = ray_origin - v[0];

	float alpha = dot(tvec, pvec) * inv_det;

	vec3 qvec = cross(tvec, edge1);

	float beta = dot(ray_direction, qvec) * inv_det;

	return vec3(1.f - alpha - beta, alpha, beta);
}

vec4
alpha_blend(vec4 top, vec4 bottom)
{
    // assume top is alpha-premultiplied, bottom is not; result is premultiplied
    return vec4(top.rgb + bottom.rgb * (1 - top.a) * bottom.a, 1 - (1 - top.a) * (1 - bottom.a)); 
}

vec4 alpha_blend_premultiplied(vec4 top, vec4 bottom)
{
    // assume everything is alpha-premultiplied
    return vec4(top.rgb + bottom.rgb * (1 - top.a), 1 - (1 - top.a) * (1 - bottom.a)); 
}

/* Adjust saturation by changing amplifying or muting difference to gray value.
 * Preserves luminance. */
vec3 apply_saturation_scale(in vec3 color, in float saturation_scale)
{
    float lum = luminance(color);
    vec3 base_gray = vec3(lum);
    vec3 d = color - base_gray;
    return max(base_gray + d * saturation_scale, vec3(0));
}

mat3
construct_ONB_frisvad(vec3 normal)
{
    precise mat3 ret;
    ret[1] = normal;
    if(normal.z < -0.999805696f) {
        ret[0] = vec3(0.0f, -1.0f, 0.0f);
        ret[2] = vec3(-1.0f, 0.0f, 0.0f);
    }
    else {
        precise float a = 1.0f / (1.0f + normal.z);
        precise float b = -normal.x * normal.y * a;
        ret[0] = vec3(1.0f - normal.x * normal.x * a, b, -normal.x);
        ret[2] = vec3(b, 1.0f - normal.y * normal.y * a, -normal.y);
    }
    return ret;
}

vec2
sample_disk(vec2 uv)
{
    float theta = 2.0 * M_PI * uv.x;
    float r = sqrt(uv.y);

    return vec2(cos(theta), sin(theta)) * r;
}

vec3
sample_triangle(vec2 xi)
{
    float sqrt_xi = sqrt(xi.x);
    return vec3(
        1.0 - sqrt_xi,
        sqrt_xi * (1.0 - xi.y),
        sqrt_xi * xi.y);
}

vec3
sample_sphere(vec2 uv)
{
    float y = 2.0 * uv.x - 1;
    float theta = 2.0 * M_PI * uv.y;
    float r = sqrt(1.0 - y * y);
    return vec3(cos(theta) * r, y, sin(theta) * r);
}

vec3
sample_cos_hemisphere(vec2 uv)
{
    vec2 disk = sample_disk(uv);

    return vec3(disk.x, sqrt(max(0.0, 1.0 - dot(disk, disk))), disk.y);
}

#define HEMISPHERE_COSINE 0.5
#define HEMISPHERE_UNIFORMISH 0.4

vec3 
sample_cos_hemisphere_multi(
    float sample_index,
    float sample_count,
    vec2 uv,
    float y_power)
{
    float strata_angle = 2.0 * M_PI / sample_count;
    float azimuth = strata_angle * (sample_index + uv.x);
    
    vec2 azimuthal_direction = vec2(cos(azimuth), sin(azimuth)) * pow(uv.y, y_power);
    float normal_direction = sqrt(max(0.0, 1.0 - dot(azimuthal_direction, azimuthal_direction)));

    return vec3(azimuthal_direction.x, normal_direction, azimuthal_direction.y);
}

vec3
get_explosion_color(vec3 normal, vec3 direction)
{
    float d = abs(dot(direction, normal)) * 2;
    const vec3 c0 = vec3(0.5, 0.05, 0.0);
    const vec3 c1 = vec3(1, 0.8, 0.1);
    const vec3 c2 = vec3(1, 0.9, 0.6) * 2;
    if(d > 1)
        return mix(c1, c2, d-1);
    else
        return mix(c0, c1, d);
}

struct SH
{
    vec4 shY;
    vec2 CoCg;
};

// Switch to enable or disable the *look* of spherical harmonics lighting.
// Does not affect the performance, just for A/B image comparison.
#define ENABLE_SH 1

vec3 project_SH_irradiance(SH sh, vec3 N)
{
#if ENABLE_SH
    float d = dot(sh.shY.xyz, N);
    float Y = 2.0 * (1.023326 * d + 0.886226 * sh.shY.w);
    Y = max(Y, 0.0);

    sh.CoCg *= Y * 0.282095 / (sh.shY.w + 1e-6);

    float   T       = Y - sh.CoCg.y * 0.5;
    float   G       = sh.CoCg.y + T;
    float   B       = T - sh.CoCg.x * 0.5;
    float   R       = B + sh.CoCg.x;

    return max(vec3(R, G, B), vec3(0.0));
#else
    return sh.shY.xyz;
#endif
}

SH irradiance_to_SH(vec3 color, vec3 dir)
{
    SH result;

#if ENABLE_SH
    float   Co      = color.r - color.b;
    float   t       = color.b + Co * 0.5;
    float   Cg      = color.g - t;
    float   Y       = max(t + Cg * 0.5, 0.0);

    result.CoCg = vec2(Co, Cg);

    float   L00     = 0.282095;
    float   L1_1    = 0.488603 * dir.y;
    float   L10     = 0.488603 * dir.z;
    float   L11     = 0.488603 * dir.x;

    result.shY = vec4 (L11, L1_1, L10, L00) * Y;
#else
    result.shY = vec4(color, 0);
    result.CoCg = vec2(0);
#endif

    return result;
}

vec3 SH_to_irradiance(SH sh)
{
    float   Y       = sh.shY.w / 0.282095;

    float   T       = Y - sh.CoCg.y * 0.5;
    float   G       = sh.CoCg.y + T;
    float   B       = T - sh.CoCg.x * 0.5;
    float   R       = B + sh.CoCg.x;

    return max(vec3(R, G, B), vec3(0.0));
}

SH init_SH()
{
    SH result;
    result.shY = vec4(0);
    result.CoCg = vec2(0);
    return result;
}

void accumulate_SH(inout SH accum, SH b, float scale)
{
    accum.shY += b.shY * scale;
    accum.CoCg += b.CoCg * scale;
}

SH mix_SH(SH a, SH b, float s)
{
    SH result;
    result.shY = mix(a.shY, b.shY, vec4(s));
    result.CoCg = mix(a.CoCg, b.CoCg, vec2(s));
    return result;
}

SH load_SH(sampler2D img_shY, sampler2D img_CoCg, ivec2 p)
{
    SH result;
    result.shY = texelFetch(img_shY, p, 0);
    result.CoCg = texelFetch(img_CoCg, p, 0).xy;
    return result;
}

// Use a macro to work around the glslangValidator errors about function argument type mismatch
#define STORE_SH(img_shY, img_CoCg, p, sh) { imageStore(img_shY, p, sh.shY); imageStore(img_CoCg, p, vec4(sh.CoCg, 0, 0)); }

void store_SH(image2D img_shY, image2D img_CoCg, ivec2 p, SH sh)
{
    imageStore(img_shY, p, sh.shY);
    imageStore(img_CoCg, p, vec4(sh.CoCg, 0, 0));
}

uvec2 packHalf4x16(vec4 v)
{
    return uvec2(packHalf2x16(v.xy), packHalf2x16(v.zw));
}

vec4 unpackHalf4x16(uvec2 v)
{
    return vec4(unpackHalf2x16(v.x), unpackHalf2x16(v.y));
}

uint packRGBE(vec3 v)
{
    vec3 va = max(vec3(0), v);
    float max_abs = max(va.r, max(va.g, va.b));
    if(max_abs == 0)
        return 0;

    float exponent = floor(log2(max_abs));

    uint result;
    result = uint(clamp(exponent + 20, 0, 31)) << 27;

    float scale = pow(2, -exponent) * 256.0;
    uvec3 vu = min(uvec3(511), uvec3(round(va * scale)));
    result |= vu.r;
    result |= vu.g << 9;
    result |= vu.b << 18;

    return result;
}

vec3 unpackRGBE(uint x)
{
    int exponent = int(x >> 27) - 20;
    float scale = pow(2, exponent) / 256.0;

    vec3 v;
    v.r = float(x & 0x1ff) * scale;
    v.g = float((x >> 9) & 0x1ff) * scale;
    v.b = float((x >> 18) & 0x1ff) * scale;

    return v;
}

uint get_primary_direction(vec3 dir)
{
    vec3 adir = abs(dir);
    if(adir.x > adir.y && adir.x > adir.z)
        return (dir.x < 0) ? 1 : 0;
    if(adir.y > adir.z)
        return (dir.y < 0) ? 3 : 2;
    return (dir.z < 0) ? 5 : 4;
}

vec2
lava_uv_warp(vec2 uv, float time)
{
    // Lava UV warp that (hopefully) matches the warp in the original Quake 2.
    // Relevant bits of the original rasterizer:

    // #define AMP     8*0x10000
    // #define SPEED   20
    // #define CYCLE   128
    // sintable[i] = AMP + sin(i * M_PI * 2 / CYCLE) * AMP; 
    // #define TURB_SIZE               64  // base turbulent texture size
    // #define TURB_MASK               (TURB_SIZE - 1)
    // turb_s = ((s + turb[(t >> 16) & (CYCLE - 1)]) >> 16) & TURB_MASK;
    // turb_t = ((t + turb[(s >> 16) & (CYCLE - 1)]) >> 16) & TURB_MASK;
    
    return uv.xy + sin(fract(uv.yx * 0.5 + time * 20 / 128) * 2 * M_PI) * 0.125;
}

// applies FLOWING and WARP modifiers to texture coordinates
void perturb_tex_coord(uint material_id, float time, inout vec2 tex_coord)
{
    if((material_id & MATERIAL_FLAG_FLOWING) != 0)
    {
        tex_coord.x -= time * 0.5;
    }

    if((material_id & MATERIAL_FLAG_WARP) != 0)
    {
        tex_coord = lava_uv_warp(tex_coord, time);
    }
}


#endif /*_GLSL_UTILS_GLSL*/
