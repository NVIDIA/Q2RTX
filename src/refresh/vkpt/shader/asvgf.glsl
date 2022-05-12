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

#ifndef  _ASVGF_GLSL_
#define  _ASVGF_GLSL_

/* 
Some notes about the filters in general.

Q2RTX uses three separate lighting channels:

  - HF (High Frequency) that contains direct diffuse irradiance;

  - LF (Low Frequency) that contains indirect diffuse irradiance encoded with 
    per-pixel spherical harmonics;

  - SPEC (Specular) that contains partial direct and indirect specular 
    irradiance.

The lighting channels go through different denoisers, as described below.


Direct diffuse lighting (HF)
============================

The HF channel goes through A-SVGF, very similar to one used in Q2VKPT. The 
algorithm is explained in two papers:

  1. "Spatiotemporal Variance-Guided Filtering: Real-Time Reconstruction for 
      Path-Traced Global Illumination" by C.Schied et al.
      URL: https://research.nvidia.com/publication/2017-07_Spatiotemporal-Variance-Guided-Filtering%3A

  2. "Gradient Estimation for Real-Time Adaptive Temporal Filtering" by
      C.Schied et al.
      URL: https://cg.ivd.kit.edu/atf.php 

Compared to the original A-SVGF, there are a few notable changes:

  - Instead of computing weighted average of variance in a pixel neighborhood,
    our filter computes weighted averages of the first and second moments of 
    luminance. Then variance is computed based on these moments. This change
    reverses the behavior of variance as it goes through the expanding a-trous
    filters, and instead of using lower variance for larger filters,
    our version uses higher variance for larger filters. That makes the filter
    wider if there is a lot of noise, but still preserves small detail after 
    a few frames of history have been accumulated.

  - Gradient samples (for HF and SPEC channels) are normalized before passing
    them through an a-trous filter, not after. This makes gradients more
    reactive in high-contrast scenarios, like when you shoot the blaster and 
    the projectile crosses some sun light. In the "after" normalization case,
    the stable sun light creates a halo of near-zero gradients around it, and
    the light from the projectile around the sun-lit region is lost.

  - Gradient samples are generally selected for the brightest pixel in a 
    3x3 square, not just a random one. This makes gradients more reactive in
    solar penumbra when the sun is moving. See `asvgf_fwd_project.comp` for more
    information.


Indirect diffuse lighting (LF)
==============================

The LF channel goes through a spatiotemporal filter that resembles SVGF, but
is simpler. Since the indirect diffuse signal is often extremely sparse - a 
situation when there is only one non-zero pixel in a hundred is not uncommon - 
a variance-guided filter doesn't really work. So the LF spatial filter is a 
simple blur guided by depth and normals. 

In order to preserve surface detail, the LF channel uses spherical harmonics. 
Specifically, the incoming radiance is converted to YCoCg color space, and the 
Y (luma) component is decomposed into 4 SH (spherical harmonic) coefficients.
See the SH-related functions in `utils.glsl`, the decomposition code in 
`indirect_lighting.rgen` and the projection code in `asvgf_atrous.comp`.

Also, the LF channel's spatial filter is computed in 1/3 resolution for 
performance reasons.


Specular lighting (SPEC)
========================

The SPEC channel only goes through a temporal filter. All temporal filters are 
fused together into `asvgf_temporal.comp`. The reason for using a temporal-only
filter is that all the spatial filters that we have experimented with in this 
project did not provide adequate image quality on normal-mapped surfaces,
or did not have good enough performance. 

Original Q2VKPT was combining specular component into the single lighting 
channel, dividing it by albedo. That solution doesn't work for surfaces with
any kind of detail in the specular maps because passing the specular signal
divided by albedo through a spatial filter effectively blurs the albedo and 
specularity. Also, materials like metals may have zero diffuse albedo, and 
dividing by that doesn't make sense.

Lastly, the SPEC channel doesn't contain *all* of the specular lighting.
Specular is computed in three different ways, and only two of these land in the
SPEC channel:

  1. Materials with low roughness, between 0 and `pt_direct_roughness_threshold`,
     are processed entirely by the `indirect_lighting.rgen` shader, using rays
     reflected off the primary surface around the stochastically sampled normal.

  2. Materials with higher roughness get specular reflections in two ways - with
     some overlap with (1), see multiple importance sampling.

     a) Analytic lights, including polygonal lights and sphere lights, compute
        specular BRDF in the `direct_lighting.rgen` shader, along with diffuse
        BRDF. Note that some sky lights are converted into analytic lights
        based on information from a sky clusters file (maps/sky/<mapname>.txt).

     b) Non-analytic emissive textures, indirect lighting, and sky lighting 
        are processed in the `indirect_lighting.rgen` shader, but *only* if 
        material roughness is lower than `pt_fake_roughness_threshold` - again,
        with some overlap. 

  3. If roughness is higher than `pt_fake_roughness_threshold`, then specular
     reflection rays are not computed because the result would be too noisy.
     Instead, specular reflections are later derived from the diffuse spherical 
     harmonics. See the `asvgf_atrous.comp` shader for more info.

*/


#include "utils.glsl"

#define STRATUM_OFFSET_SHIFT 3
#define STRATUM_OFFSET_MASK ((1 << STRATUM_OFFSET_SHIFT) - 1)

const float gaussian_kernel[2][2] = {
	{ 1.0 / 4.0, 1.0 / 8.0  },
	{ 1.0 / 8.0, 1.0 / 16.0 }
};

const float wavelet_factor = 0.5;
const float wavelet_kernel[2][2] = {
	{ 1.0, wavelet_factor  },
	{ wavelet_factor, wavelet_factor * wavelet_factor }
};

#endif  /*_ASVGF_GLSL_*/
