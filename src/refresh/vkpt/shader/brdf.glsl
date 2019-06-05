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

float G_Smith_over_NdotV(float roughness, float NdotV, float NdotL)
{
    float k = ((roughness + 1) * (roughness + 1)) / 8;
    float g1 = NdotL / (NdotL * (1 - k) + k);
    float g2 = 1.0 / (NdotV * (1 - k) + k);
    return g1 * g2;
}

float square(float x) { return x * x; }

float GGX(vec3 V, vec3 L, vec3 N, float roughness, float NoH_offset)
{
    vec3 H = normalize(L - V);
    
    float NoL = max(0, dot(N, L));
    float VoH = max(0, -dot(V, H));
    float NoV = max(0, -dot(N, V));
    float NoH = clamp(dot(N, H) + NoH_offset, 0, 1);

    if (NoL > 0)
    {
        float G = G_Smith_over_NdotV(roughness, NoV, NoL);
        float alpha = square(max(roughness, 0.02));
        float D = square(alpha) / (M_PI * square(square(NoH) * square(alpha) + (1 - square(NoH))));

        // Incident light = SampleColor * NoL
        // Microfacet specular = D*G*F / (4*NoL*NoV)
        // F = 1, accounted for elsewhere
        // NoL = 1, accounted for in the diffuse term
        return D * G / 4;
    }

    return 0;
}

vec3 ImportanceSampleGGX(vec2 u, float roughness, mat3 basis)
{
    float a = roughness * roughness;
    u.y *= global_ubo.pt_ndf_trim;

    float phi = M_PI * 2 * u.x;
    float cosTheta = sqrt((1 - u.y) / (1 + (a * a - 1) * u.y));
    float sinTheta = sqrt(1 - cosTheta * cosTheta);

    // Tangent space H
    vec3 tH;
    tH.x = sinTheta * cos(phi);
    tH.y = sinTheta * sin(phi);
    tH.z = cosTheta;

    // World space H

    return normalize(basis[0] * tH.x + basis[2] * tH.y + basis[1] * tH.z);
}

// Compositing function that combines the lighting channels and material
// parameters into the final pixel color (before post-processing effects)
vec3 composite_color(vec3 surf_albedo, float surf_specular, float surf_metallic, 
    vec3 projected_lf, vec3 high_freq, vec3 specular, vec4 transparent)
{
    if(global_ubo.pt_num_bounce_rays == 0)
    {
        projected_lf += vec3(1e-3);
    }

    projected_lf *= global_ubo.flt_scale_lf;
    high_freq *= global_ubo.flt_scale_hf;
    specular *= global_ubo.flt_scale_spec;

    vec3 final_color;
    if(global_ubo.flt_fixed_albedo == 0)
    {
        vec3 diff_color = surf_albedo.rgb * (1 - surf_specular);
        vec3 spec_color = mix(vec3(1), surf_albedo.rgb, surf_metallic) * surf_specular;
        
        final_color = (projected_lf.rgb + high_freq.rgb) * diff_color + specular.rgb * spec_color;
    }
    else
    {
        final_color = (projected_lf.rgb + high_freq.rgb + specular.rgb) * global_ubo.flt_fixed_albedo;
    }

    transparent *= global_ubo.flt_scale_overlay;
    final_color.rgb = final_color.rgb * (1 - transparent.a) + transparent.rgb;

    return final_color;
}