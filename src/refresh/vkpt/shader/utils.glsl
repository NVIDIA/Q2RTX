/*
Copyright (C) 2018 Christoph Schied

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
	uint projected0 = enc & 0xffffu;
	uint projected1 = enc >> 16;
	// copy sign bit and paste rest to upper mantissa to get float encoded in [1,2).
	// the employed bits will only go to [1,1.5] for valid encodings.
	uint vec0 = 0x3f800000u | ((projected0 & 0x7fffu)<<8);
	uint vec1 = 0x3f800000u | ((projected1 & 0x7fffu)<<8);
	// transform to [-1,1] to be able to precisely encode the important academic special cases {-1,0,1}
	vec3 n;
	n.x = uintBitsToFloat(floatBitsToUint(2.0f*uintBitsToFloat(vec0) - 2.0f) | ((projected0 & 0x8000u)<<16));
	n.y = uintBitsToFloat(floatBitsToUint(2.0f*uintBitsToFloat(vec1) - 2.0f) | ((projected1 & 0x8000u)<<16));
	n.z = 1.0f - (abs(n.x) + abs(n.y));

	if (n.z < 0.0f) {
		float oldX = n.x;
		n.x = (1.0f - abs(n.y))  * ((oldX < 0.0f) ? -1.0f : 1.0f);
		n.y = (1.0f - abs(oldX)) * ((n.y  < 0.0f) ? -1.0f : 1.0f);
	}
	return normalize(n);
}

uint
encode_normal(vec3 normal)
{
    uint projected0, projected1;
    const float invL1Norm = 1.0f / dot(abs(normal), vec3(1));

    // first find floating point values of octahedral map in [-1,1]:
    float enc0, enc1;
    if (normal[2] < 0.0f) {
        enc0 = (1.0f - abs(normal[1] * invL1Norm)) * ((normal[0] < 0.0f) ? -1.0f : 1.0f);
        enc1 = (1.0f - abs(normal[0] * invL1Norm)) * ((normal[1] < 0.0f) ? -1.0f : 1.0f);
    }
    else {
        enc0 = normal[0] * invL1Norm;
        enc1 = normal[1] * invL1Norm;
    }
    // then encode:
    uint enci0 = floatBitsToUint((abs(enc0) + 2.0f)/2.0f);
    uint enci1 = floatBitsToUint((abs(enc1) + 2.0f)/2.0f);
    // copy over sign bit and truncated mantissa. could use rounding for increased precision here.
    projected0 = ((floatBitsToUint(enc0) & 0x80000000u)>>16) | ((enci0 & 0x7fffffu)>>8);
    projected1 = ((floatBitsToUint(enc1) & 0x80000000u)>>16) | ((enci1 & 0x7fffffu)>>8);
    // avoid -0 cases:
    if((projected0 & 0x7fffu) == 0) projected0 = 0;
    if((projected1 & 0x7fffu) == 0) projected1 = 0;
    return (projected1 << 16) | projected0;
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

#endif /*_GLSL_UTILS_GLSL*/
