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

float square(float x) { return x * x; }

// Converts a square of roughness to a Phong specular power
float RoughnessSquareToSpecPower(in float alpha) {
    return max(0.01, 2.0f / (square(alpha) + 1e-4) - 2.0f);
}

// Converts a Blinn-Phong specular power to a square of roughness
float SpecPowerToRoughnessSquare(in float s) {
    return clamp(sqrt(max(0, 2.0f / (s + 2.0f))), 0, 1);
}

float G1_Smith(float roughness, float NdotL)
{
    float alpha = square(roughness);
    return 2.0 * NdotL / (NdotL + sqrt(square(alpha) + (1.0 - square(alpha)) * square(NdotL)));
}

float G_Smith_over_4_NdotV(float roughness, float NdotV, float NdotL)
{
    float alpha = square(roughness);
    float g1 = NdotL / (NdotL + sqrt(square(alpha) + (1.0 - square(alpha)) * square(NdotL)));
    float g2 =   1.0 / (NdotV + sqrt(square(alpha) + (1.0 - square(alpha)) * square(NdotV)));
    return g1 * g2;
}

vec3 schlick_fresnel(vec3 F0, float HdotV, float specular_factor)
{
    vec3 F = F0 + (vec3(1.0) - F0) * pow(1 - HdotV, 5);
    F *= specular_factor;
    F = clamp(F, vec3(0.0), vec3(1.0));
    return F;
}

vec3 GGX_times_NdotL(vec3 V, vec3 L, vec3 N, float roughness, vec3 F0, float NoH_offset, float specular_factor, out vec3 F)
{
    vec3 H = normalize(L - V);
    
    float NoL = max(0, dot(N, L));
    float VoH = max(0, -dot(V, H));
    float NoV = max(0, -dot(N, V));
    float NoH = clamp(dot(N, H) + NoH_offset, 0, 1);
    
    F = schlick_fresnel(F0, VoH, specular_factor);

    if (NoL > 0 && VoH > 0)
    {
        float G = G_Smith_over_4_NdotV(roughness, NoV, NoL);
        float alpha = square(max(roughness, 0.02));
        float D = square(alpha) / (M_PI * square(square(NoH) * square(alpha) + (1 - square(NoH))));

        // GGX BRDF = D*G*F / (4*NoL*NoV)
        // NoL = 1 by function definition, cancelled out in the rendering integral
        // NoV and 4 are cancelled out with the same terms in G
        return F * (D * G);
    }

    return vec3(0);
}

vec3 ImportanceSampleGGX_VNDF(vec2 u, float roughness, vec3 V, mat3 basis)
{
    float alpha = square(roughness);

    vec3 Ve = -vec3(dot(V, basis[0]), dot(V, basis[2]), dot(V, basis[1]));

    vec3 Vh = normalize(vec3(alpha * Ve.x, alpha * Ve.y, Ve.z));
    
    float lensq = square(Vh.x) + square(Vh.y);
    vec3 T1 = lensq > 0.0 ? vec3(-Vh.y, Vh.x, 0.0) * inversesqrt(lensq) : vec3(1.0, 0.0, 0.0);
    vec3 T2 = cross(Vh, T1);

    float r = sqrt(u.x * global_ubo.pt_ndf_trim);
    float phi = 2.0 * M_PI * u.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - square(t1)) + s * t2;

    vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - square(t1) - square(t2))) * Vh;

    // Tangent space H
    vec3 Ne = vec3(alpha * Nh.x, max(0.0, Nh.z), alpha * Nh.y);

    // World space H
    return normalize(basis * Ne);
}

float ImportanceSampleGGX_VNDF_PDF(float roughness, vec3 N, vec3 V, vec3 L)
{
    vec3 H = normalize(L + V);
    float NoH = clamp(dot(N, H), 0, 1);
    float VoH = clamp(dot(V, H), 0, 1);

    float alpha = square(roughness);
    float D = square(alpha) / (M_PI * square(square(NoH) * square(alpha) + (1 - square(NoH))));
    return (VoH > 0.0) ? D / (4.0 * VoH) : 0.0;
}

float phong(vec3 N, vec3 L, vec3 V, float phong_exp)
{
    vec3 H = normalize(L - V);
    return pow(max(0.0, dot(H, N)), phong_exp);
}

void get_reflectivity(vec3 base_color, float metallic, out vec3 o_albedo, out vec3 o_base_reflectivity)
{
    const float dielectric_specular = 0.04;
    o_albedo = mix(base_color * (1.0 - dielectric_specular), vec3(0), metallic);
    o_base_reflectivity = mix(vec3(dielectric_specular), base_color, metallic);
}

vec3 demodulate_specular(vec3 base_reflectivity, vec3 specular)
{
    if (global_ubo.flt_enable == 0)
        return specular;

    return specular / max(vec3(0.01), base_reflectivity);
}

vec3 modulate_specular(vec3 base_reflectivity, vec3 filtered_specular)
{
    if (global_ubo.flt_enable == 0)
        return filtered_specular;

    return filtered_specular * max(vec3(0.01), base_reflectivity);
}


// Compositing function that combines the lighting channels and material
// parameters into the final pixel color (before post-processing effects)
vec3 composite_color(vec3 surf_base_color, float surf_metallic, vec3 throughput,
    vec3 projected_lf, vec3 high_freq, vec3 specular, vec4 transparent)
{
    if(global_ubo.pt_num_bounce_rays == 0)
    {
        projected_lf += vec3(1e-3);
    }

    projected_lf *= global_ubo.flt_scale_lf;
    high_freq *= global_ubo.flt_scale_hf;
    specular *= global_ubo.flt_scale_spec;

    vec3 albedo, base_reflectivity;
    get_reflectivity(surf_base_color, surf_metallic, albedo, base_reflectivity);

    specular = modulate_specular(base_reflectivity, specular);

    if (global_ubo.flt_fixed_albedo != 0)
        albedo = vec3(global_ubo.flt_fixed_albedo);

    vec3 final_color = (projected_lf.rgb + high_freq.rgb) * albedo + specular.rgb;
    
    final_color *= throughput;

    transparent *= global_ubo.flt_scale_overlay;
    final_color.rgb = final_color.rgb * (1 - transparent.a) + transparent.rgb;

    return final_color;
}
