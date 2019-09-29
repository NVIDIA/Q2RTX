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

#define PRECOMPUTED_SKY_BINDING_IDX					0
#define PRECOMPUTED_SKY_UBO_DESC_SET_IDX			3

/*
Based on E. Bruneton F. Neyet paper "Precomputed Atmospheric Scattering"
*/

struct AtmosphereParameters
{
	vec3		StarIrradiance;						// Irradiance of the star light spectrum translated to RGB
	float		StarAngularRadius;				// Angular radius of the star

	vec3		RayleightScatteringRGB;		// Rayleigh scattering term
	float		PlanetSurfaceRadius;			// Radius of the planet

	vec3		MieScatteringRGB;					// Mie scattering term
	float		PlanetAtmosphereRadius;		// Radius of the outer boundary of the planet atmosphere

	float		MieHenyeyGreensteinG;			// G term of the Henyey-Greensteing Mie phase function approximation
	float		SqDistanceToHorizontalBoundary;	// Square distance from the view point to the top atmosphere boundary in horizontal direction
	float		AtmosphereHeight;					// Height of the atmhosphere layer
	float		reserved;
};

layout(set = PRECOMPUTED_SKY_UBO_DESC_SET_IDX, binding = PRECOMPUTED_SKY_BINDING_IDX, std140) uniform SKY_UBO{
	AtmosphereParameters SkyParams;
};


#define SM_PI 3.1415926535897932384626433832795f

#include "precomputed_sky_params.h"

// ----------------------------------------------------------------------------

#define TRANSMITTANCE_TEXTURE_WIDTH 256.0
#define TRANSMITTANCE_TEXTURE_HEIGHT 64.0
#define SCATTERING_TEXTURE_R_SIZE 32.0
#define SCATTERING_TEXTURE_MU_SIZE 128.0
#define SCATTERING_TEXTURE_MU_S_SIZE 32.0
#define SCATTERING_TEXTURE_NU_SIZE 8.0
#define IRRADIANCE_TEXTURE_WIDTH 64.0
#define IRRADIANCE_TEXTURE_HEIGHT 16.0
#define SCATTERING_TEXTURE_MU_SIZE_HALF 64.0

#define RANGED_TRANSMITTANCE_U(val) ((val)*(TRANSMITTANCE_TEXTURE_WIDTH-1)/(TRANSMITTANCE_TEXTURE_WIDTH)+(0.5/TRANSMITTANCE_TEXTURE_WIDTH))
#define RANGED_TRANSMITTANCE_V(val) ((val)*(TRANSMITTANCE_TEXTURE_HEIGHT-1)/(TRANSMITTANCE_TEXTURE_HEIGHT)+(0.5/TRANSMITTANCE_TEXTURE_HEIGHT))

#define RANGED_SCATTERING_HEIGHT(val) ((val)*(SCATTERING_TEXTURE_R_SIZE-1)/(SCATTERING_TEXTURE_R_SIZE)+(0.5/SCATTERING_TEXTURE_R_SIZE))
#define RANGED_SCATTERING_VIEW(val) ((val)*(SCATTERING_TEXTURE_MU_SIZE_HALF-1)/(SCATTERING_TEXTURE_MU_SIZE_HALF)+(0.5/SCATTERING_TEXTURE_MU_SIZE_HALF))
#define RANGED_SCATTERING_SUN(val) ((val)*(SCATTERING_TEXTURE_MU_S_SIZE-1)/(SCATTERING_TEXTURE_MU_S_SIZE)+(0.5/SCATTERING_TEXTURE_MU_S_SIZE))
#define RANGED_SCATTERING_VIEW_SUN(val) ((val)*(SCATTERING_TEXTURE_NU_SIZE-1)/(SCATTERING_TEXTURE_NU_SIZE)+(0.5/SCATTERING_TEXTURE_NU_SIZE))

#define RANGED_IRRADIANCE_U(val) ((val)*(IRRADIANCE_TEXTURE_WIDTH-1)/(IRRADIANCE_TEXTURE_WIDTH)+(0.5/IRRADIANCE_TEXTURE_WIDTH))
#define RANGED_IRRADIANCE_V(val) ((val)*(IRRADIANCE_TEXTURE_HEIGHT-1)/(IRRADIANCE_TEXTURE_HEIGHT)+(0.5/IRRADIANCE_TEXTURE_HEIGHT))

#define SKY_IRRADIANCE_TO_RADIANCE (0.5 / SM_PI)

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

float ClampRadius(AtmosphereParameters atmosphere, float PointHeight) 
{
	return clamp(PointHeight, atmosphere.PlanetSurfaceRadius, atmosphere.PlanetAtmosphereRadius);
}

// ----------------------------------------------------------------------------

bool RayIntersectsGround(AtmosphereParameters atmosphere, float PointHeight, float ViewAngleCos) 
{
	return ViewAngleCos < 0.0 && PointHeight * PointHeight * (ViewAngleCos * ViewAngleCos - 1.0) + atmosphere.PlanetSurfaceRadius * atmosphere.PlanetSurfaceRadius >= 0.0;
}

// ----------------------------------------------------------------------------

float DistanceToTopAtmosphereBoundary(AtmosphereParameters atmosphere, float PlanetRadius, float ViewAngleCos) 
{
	float D = PlanetRadius * PlanetRadius * (ViewAngleCos * ViewAngleCos - 1.0) + atmosphere.PlanetAtmosphereRadius * atmosphere.PlanetAtmosphereRadius;

	return max(0, -PlanetRadius * ViewAngleCos + sqrt(max(0, D)));
}

// ----------------------------------------------------------------------------
/*
According figure 3 in Eric Bruneton paper "Precomputed Atmospheric Scattering"
*/
vec2 GetTransmittanceUV(AtmosphereParameters atmosphere, float PointHeight, float ViewAngleCos) 
{
	float X0 = sqrt(atmosphere.SqDistanceToHorizontalBoundary);	// top atmoshpere bounding in horizontal direction
	float dh =	sqrt(max(0, (PointHeight * PointHeight - atmosphere.PlanetSurfaceRadius * atmosphere.PlanetSurfaceRadius)));		// distance to the horizon from the view point
	float dH = DistanceToTopAtmosphereBoundary(atmosphere, PointHeight, ViewAngleCos);	// distance to the top of the atmosphere from the current position and view angle
	float Xtop = atmosphere.PlanetAtmosphereRadius - PointHeight;		// distance to the top of the atmosphere right above the view point
	float XH = dh + X0;
	float U = (dH - Xtop) / (XH - Xtop);
	float V = dh / X0;
	return vec2(RANGED_TRANSMITTANCE_U(U), RANGED_TRANSMITTANCE_V(V));
}

// ----------------------------------------------------------------------------

vec3 GetTransmittanceToTopAtmosphereBoundary(AtmosphereParameters atmosphere, sampler2D transmittance_texture, float PointHeight, float ViewAngleCos)
{
	vec2 uv = GetTransmittanceUV(atmosphere, PointHeight, ViewAngleCos);
	return vec3(texture(transmittance_texture, uv));
}

// ----------------------------------------------------------------------------

vec3 GetTransmittance(
		AtmosphereParameters atmosphere,
		sampler2D transmittance_texture,
		float PointHeight, float ViewAngleCos, float Destination, bool IntersectsGround)
{
		float DestinationHeight = ClampRadius(atmosphere, sqrt(Destination * Destination + 2.0 * PointHeight * ViewAngleCos * Destination + PointHeight * PointHeight));
		float DestinationViewAngleCos = clamp((PointHeight * ViewAngleCos + Destination) / DestinationHeight, -1.0, 1.0);	

		/*
			Since transmittance stays the same along the view ray intersecting a point and reversed view ray from that point, intersecting the previous view position, 
			we can reverse the transmittance sampling in case if the ray intersects planet surface
		*/
		if (IntersectsGround) 
		{
			return min(
				GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, DestinationHeight, -DestinationViewAngleCos) / GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, PointHeight, -ViewAngleCos),
				vec3(1.0, 1.0, 1.0));
		} 
		else 
		{
			return min(
				GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, PointHeight, ViewAngleCos) / GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, DestinationHeight, DestinationViewAngleCos),
        vec3(1.0, 1.0, 1.0));
    }
}

// ----------------------------------------------------------------------------
/*
Acctording to Egor Yusov "Outdoor Light Scattering Sample Update"
Figure 4, eq. 3 and 4
*/
vec4 GetScatteringUVWZ(AtmosphereParameters atmosphere,	float PointHeight, float ViewAngleCos, float SunZenithAngleCos, float SunViewAngleCos, bool IntersectsGround) 
{
	float SquareHeight = PointHeight * PointHeight;
	float SquareViewAngleSin = 1.0f - ViewAngleCos * ViewAngleCos;
	// Distance to top atmosphere boundary for a horizontal ray at ground level.
	float H = sqrt(atmosphere.SqDistanceToHorizontalBoundary);
	// Distance to the horizon.
	float HorizonDistance = sqrt(max(0, (SquareHeight - atmosphere.PlanetSurfaceRadius * atmosphere.PlanetSurfaceRadius)));
	float u_Height = RANGED_SCATTERING_HEIGHT((HorizonDistance / H));

	float discriminant = -SquareHeight * SquareViewAngleSin + atmosphere.PlanetSurfaceRadius * atmosphere.PlanetSurfaceRadius;
	float u_ViewToZeinthCos;
	if (IntersectsGround) 
	{
		// Distance to the ground for the ray (r,mu), and its minimum and maximum
		// values over all mu - obtained for (r,-1) and (r,mu_horizon).
		float d = -PointHeight * ViewAngleCos - sqrt(max(0, discriminant));
		float d_min = PointHeight - atmosphere.PlanetSurfaceRadius;
		float d_max = HorizonDistance;
		float du = d_max == d_min ? 0.0 :	(d - d_min) / (d_max - d_min);
		du = RANGED_SCATTERING_VIEW(du);
		u_ViewToZeinthCos = 0.5 - 0.5 * du;
	}
	else 
	{
		// Distance to the top atmosphere boundary for the ray (r,mu), and its
		// minimum and maximum values over all mu - obtained for (r,1) and
		// (r,mu_horizon).
		float d = -PointHeight * ViewAngleCos + sqrt(max(0, discriminant + H * H));
		float d_min = atmosphere.PlanetAtmosphereRadius - PointHeight;
		float d_max = HorizonDistance + H;
		float du = (d - d_min) / (d_max - d_min);
		du = RANGED_SCATTERING_VIEW(du);
		u_ViewToZeinthCos = 0.5 + 0.5 * du;
	}

	float d = DistanceToTopAtmosphereBoundary(atmosphere, atmosphere.PlanetSurfaceRadius, SunZenithAngleCos);
	float d_min = atmosphere.AtmosphereHeight;
	float d_max = H;
	float a = (d - d_min) / (d_max - d_min);
	float A = 0.41582 * atmosphere.PlanetSurfaceRadius / (d_max - d_min);
	float dy = max(1.0 - a / A, 0.0) / (1.0 + a);
	float u_SunZenithAngleCos = RANGED_SCATTERING_SUN(dy);

	float u_SunViewAngleCos = (SunViewAngleCos + 1.0) / 2.0;
	return vec4(u_SunViewAngleCos, u_SunZenithAngleCos, u_ViewToZeinthCos, u_Height);
}

// ----------------------------------------------------------------------------
/*
	According to the paper "Precomputed Atmospheric Scattering" part 4 paragraph 2:
	Extracting encoded Mie component from the Rayleigh part  and Mie magnitude:
	Cm ~ C * (CMie / CRayleigh.r) * (BetaRayleigh.r / BetaMie.r) * (BetaMie / BetaRayleigh)
*/
vec3 GetMieFromVec4(AtmosphereParameters atmosphere, vec4 C)
{
	if (C.r == 0.0)
	{
		return vec3(0,0,0);
	}
	else
		return C.rgb * C.a / C.r * (atmosphere.RayleightScatteringRGB.r / atmosphere.MieScatteringRGB.r) * (atmosphere.MieScatteringRGB / atmosphere.RayleightScatteringRGB);
}

// ----------------------------------------------------------------------------
/*
	4D sampling of the scatterinng 3DTexture LUT
*/
vec3 Sample4D(
	AtmosphereParameters atmosphere,
	sampler3D scattering_texture,
	float PointHeight, float ViewAngleCos, float SunZenithAngleCos, float SunViewAngleCos, bool IntersectsGround, 
	out vec3 OutMieScattering)
{
	vec4 uvwz = GetScatteringUVWZ(atmosphere, PointHeight, ViewAngleCos, SunZenithAngleCos, SunViewAngleCos, IntersectsGround);
	float ux = uvwz.x * float(SCATTERING_TEXTURE_NU_SIZE - 1);
	float offset = floor(ux);
	float lerp = fract(ux);
	vec3 uvw0 = vec3((offset + uvwz.y) / float(SCATTERING_TEXTURE_NU_SIZE), uvwz.z, uvwz.w);
	vec3 uvw1 = vec3((offset + 1.0 + uvwz.y) / float(SCATTERING_TEXTURE_NU_SIZE), uvwz.z, uvwz.w);

	vec4 InterpolatedScattering = texture(scattering_texture, uvw0) * (1.0 - lerp) + texture(scattering_texture, uvw1) * lerp;
	OutMieScattering = GetMieFromVec4(atmosphere, InterpolatedScattering);

	return InterpolatedScattering.xyz;
}

// ----------------------------------------------------------------------------

float RayleighPhaseFunction(float nu)
{
	float k = 3.0 / (16.0 * SM_PI);
	return k * (1.0 + nu * nu);
}

// ----------------------------------------------------------------------------

float MiePhaseFunction(float g, float nu)
{
	float k = 3.0 / (8.0 * SM_PI) * (1.0 - g * g) / (2.0 + g * g);
	return k * (1.0 + nu * nu) / pow(1.0 + g * g - 2.0 * g * nu, 1.5);
}

// ----------------------------------------------------------------------------

void GetParameters(AtmosphereParameters atmosphere, vec3 view_ray, vec3 camera, out float PointHeight, out float DotViewAngleCos, out bool bIntersectsAtmoshpere)
{
	PointHeight = length(camera);
	DotViewAngleCos = dot(camera, view_ray);
	float IntersectsAtmoshpere = -DotViewAngleCos - sqrt(DotViewAngleCos * DotViewAngleCos - PointHeight * PointHeight + atmosphere.PlanetAtmosphereRadius * atmosphere.PlanetAtmosphereRadius);
	if (IntersectsAtmoshpere > 0.0)
	{
		// move the camera to the atmoshpere boundary, if we are in space
		camera = camera + view_ray * IntersectsAtmoshpere;
		PointHeight = atmosphere.PlanetAtmosphereRadius;
		DotViewAngleCos += IntersectsAtmoshpere;
		bIntersectsAtmoshpere = true;
	}
	else
		bIntersectsAtmoshpere = false;
}

// ----------------------------------------------------------------------------

vec3 GetSkyRadiance(
	AtmosphereParameters atmosphere,
	sampler2D transmittance_texture,
	sampler3D scattering_texture,
	vec3 camera, vec3 view_ray,	vec3 sun_direction, out vec3 transmittance)
{
	transmittance = vec3(1.0, 1.0, 1.0);
	// Compute the distance to the top atmosphere boundary along the view ray,
	// assuming the viewer is in space (or NaN if the view ray does not intersect
	// the atmosphere).
	float PointHeight;
	float DotViewAngleCos;
	bool IntersectsAtmoshpere;
	GetParameters(atmosphere, view_ray, camera, PointHeight, DotViewAngleCos, IntersectsAtmoshpere);

	if (!IntersectsAtmoshpere && PointHeight > atmosphere.PlanetAtmosphereRadius)
	{
		return vec3(0,0,0);
	}

	float ViewAngleCos = DotViewAngleCos / PointHeight;
	float SunZenithAngleCos = dot(camera, sun_direction) / PointHeight;
	float SunViewAngleCos = dot(view_ray, sun_direction);
	bool IntersectsGround = RayIntersectsGround(atmosphere, PointHeight, ViewAngleCos);

	transmittance = IntersectsGround ? 
		vec3(0,0,0) :
		GetTransmittanceToTopAtmosphereBoundary(atmosphere, transmittance_texture, PointHeight, ViewAngleCos);

	vec3 single_mie_scattering;
	vec3 scattering;

	scattering = Sample4D(
		atmosphere, scattering_texture,
		PointHeight, ViewAngleCos, SunZenithAngleCos, SunViewAngleCos, IntersectsGround,
		single_mie_scattering);

	vec3 result = scattering * RayleighPhaseFunction(SunViewAngleCos) + single_mie_scattering * MiePhaseFunction(atmosphere.MieHenyeyGreensteinG, SunViewAngleCos);
	result /= atmosphere.StarIrradiance * (SUN_SPECTRAL_RADIANCE_TO_LUMINANCE / SKY_SPECTRAL_RADIANCE_TO_LUMINANCE);
	result *= SKY_IRRADIANCE_TO_RADIANCE;
	return result;
}

// ----------------------------------------------------------------------------
/*
If the sun is obstructed by obstacles, the single-scattered rays do not get to the line [Viewer]->[View point], so but the LUT is allready
takes into account those rays like there are no obstacles, so the straightforward approach gives artifacts and a strange color after the sunset
We use a hack that samples LUT for a different view-sun angle when it's obstructed by obstacles
*/
vec3 CorrectViewRay(vec3 view_ray, vec3 sun_direction)
{
    if(sun_direction.z == 1)
        return view_ray;

    vec3 dir_axis = normalize(vec3(sun_direction.xy, 0));
    vec3 ortho_axis = vec3(dir_axis.y, -dir_axis.x, 0);

    vec2 view = vec2(dot(view_ray, dir_axis), dot(view_ray, ortho_axis));
    view.x = view.x * 0.75 - 0.25;

    return view.x * dir_axis + view.y * ortho_axis + vec3(0, 0, view_ray.z);
}

// ----------------------------------------------------------------------------

vec3 GetSkyRadianceToPoint(
    AtmosphereParameters atmosphere,
    sampler2D transmittance_texture,
    sampler3D scattering_texture,
    vec3 camera, vec3 spoint,
    vec3 sun_direction, out vec3 transmittance)
{
  // Compute the distance to the top atmosphere boundary along the view ray,
  // assuming the viewer is in space (or NaN if the view ray does not intersect
  // the atmosphere).
    vec3 view_ray = normalize(spoint - camera);
    view_ray = CorrectViewRay(view_ray, sun_direction);

	float PointHeight;
	float DotViewAngleCos;
	bool IntersectsAtmoshpere;
	GetParameters(atmosphere, view_ray, camera, PointHeight, DotViewAngleCos, IntersectsAtmoshpere);
    
    float ViewAngleCos = DotViewAngleCos / PointHeight;
    float SunZenithCos = dot(camera, sun_direction) / PointHeight;
    float ViewSunCos = dot(view_ray, sun_direction);
    float DistanceToPoint = length(spoint - camera);
    bool IntersectsGround = RayIntersectsGround(atmosphere, PointHeight, ViewAngleCos);
	float ViewAngleCos1 = 0.02;
	float ViewAngleCos2 = -0.06;
	vec3 single_mie_scattering;
	vec3 single_mie_scattering_p;
	vec3 scattering;
	vec3 scattering_p;
	/*
		We have a small region near the horizon where the view angle transits from atmoshphere boundary to the planet surface,
		precomputed maximum distance difference creates variety of precomputed results scattering and tranmittance and that leads to known artifacts near the horizon
		We interpolate between static angles ov view direction to eliminate this artifacts
	*/
	if (ViewAngleCos > ViewAngleCos1 || ViewAngleCos < ViewAngleCos2)
	{
		transmittance = GetTransmittance(atmosphere, transmittance_texture, PointHeight, ViewAngleCos, DistanceToPoint, IntersectsGround);
		scattering = Sample4D(atmosphere, scattering_texture, PointHeight, ViewAngleCos, SunZenithCos, ViewSunCos, IntersectsGround, single_mie_scattering);

		float PointHeight_p = ClampRadius(atmosphere, sqrt(DistanceToPoint * DistanceToPoint + 2.0 * PointHeight * ViewAngleCos * DistanceToPoint + PointHeight * PointHeight));
		float ViewAngle_p = (PointHeight * ViewAngleCos + DistanceToPoint) / PointHeight_p;
		float SunZenithCos_p = (PointHeight * SunZenithCos + DistanceToPoint * ViewSunCos) / PointHeight_p;

		single_mie_scattering_p;
		scattering_p = Sample4D(atmosphere, scattering_texture, PointHeight_p, ViewAngle_p, SunZenithCos_p, ViewSunCos, IntersectsGround, single_mie_scattering_p);
	}
	else
	{
		// iteration higher artifact
		bool IntersectsGround1 = RayIntersectsGround(atmosphere, PointHeight, ViewAngleCos1);
		vec3 single_mie_scattering1;
		vec3 transmittance1 = GetTransmittance(atmosphere, transmittance_texture, PointHeight, ViewAngleCos1, DistanceToPoint, IntersectsGround1);
		vec3 scattering1 = Sample4D(atmosphere, scattering_texture, PointHeight, ViewAngleCos1, SunZenithCos, ViewSunCos, IntersectsGround1, single_mie_scattering1);
				
		float PointHeight_p1 = ClampRadius(atmosphere, sqrt(DistanceToPoint * DistanceToPoint + 2.0 * PointHeight * ViewAngleCos1 * DistanceToPoint + PointHeight * PointHeight));
		float ViewAngle_p1 = (PointHeight * ViewAngleCos1 + DistanceToPoint) / PointHeight_p1;				
		float SunZenithCos_p1 = (PointHeight * SunZenithCos + DistanceToPoint * ViewSunCos) / PointHeight_p1;
		vec3 single_mie_scattering_p1;
		vec3 scattering_p1 = Sample4D(atmosphere, scattering_texture, PointHeight_p1, ViewAngle_p1, SunZenithCos_p1, ViewSunCos, IntersectsGround1, single_mie_scattering_p1);
				
		// iteration lower artifact
		bool IntersectsGround2 = RayIntersectsGround(atmosphere, PointHeight, ViewAngleCos2);
		vec3 single_mie_scattering2;
		vec3 transmittance2 = GetTransmittance(atmosphere, transmittance_texture, PointHeight, ViewAngleCos2, DistanceToPoint, IntersectsGround2);
		vec3 scattering2 = Sample4D(atmosphere, scattering_texture, PointHeight, ViewAngleCos2, SunZenithCos, ViewSunCos, IntersectsGround2, single_mie_scattering2);
				
		float PointHeight_p2 = ClampRadius(atmosphere, sqrt(DistanceToPoint * DistanceToPoint + 2.0 * PointHeight * ViewAngleCos2 * DistanceToPoint + PointHeight * PointHeight));
		float ViewAngle_p2 = (PointHeight * ViewAngleCos2 + DistanceToPoint) / PointHeight_p2;
		float SunZenithCos_p2 = (PointHeight * SunZenithCos + DistanceToPoint * ViewSunCos) / PointHeight_p2;
		vec3 single_mie_scattering_p2;
		vec3 scattering_p2 = Sample4D(atmosphere, scattering_texture, PointHeight_p2, ViewAngle_p2, SunZenithCos_p2, ViewSunCos, IntersectsGround2, single_mie_scattering_p2);

		// combine
		float lerp = (ViewAngleCos1 - ViewAngleCos) / (ViewAngleCos1 - ViewAngleCos2);
		transmittance = mix(transmittance1, transmittance2, lerp);
		scattering = mix(scattering1, scattering2, lerp);
		single_mie_scattering = mix(single_mie_scattering1, single_mie_scattering2, lerp);
		single_mie_scattering_p = mix(single_mie_scattering_p1, single_mie_scattering_p2, lerp);
		scattering_p = mix(scattering_p1, scattering_p2, lerp);
	}

    scattering = scattering - transmittance * scattering_p;
    single_mie_scattering = single_mie_scattering - transmittance * single_mie_scattering_p;

	// From Eric Bruneton's paper: Hack to avoid rendering artifacts when the sun is below the horizon.
    single_mie_scattering = single_mie_scattering *
      smoothstep(0.0f, 0.01f, SunZenithCos);

    vec3 result = scattering * RayleighPhaseFunction(ViewSunCos) + single_mie_scattering * MiePhaseFunction(atmosphere.MieHenyeyGreensteinG, ViewSunCos);
	result /= atmosphere.StarIrradiance * (SUN_SPECTRAL_RADIANCE_TO_LUMINANCE / SKY_SPECTRAL_RADIANCE_TO_LUMINANCE);
	result *= SKY_IRRADIANCE_TO_RADIANCE;
    return result;
}

// ----------------------------------------------------------------------------

vec2 GetIrradianceUV(AtmosphereParameters atmosphere, float PointHeight, float SunZenithCos)
{
    float uHeight = (PointHeight - atmosphere.PlanetSurfaceRadius) / atmosphere.AtmosphereHeight;
    float vViewAngle = SunZenithCos * 0.5 + 0.5;
    return vec2(RANGED_IRRADIANCE_U(vViewAngle), RANGED_IRRADIANCE_V(uHeight));
}

// ----------------------------------------------------------------------------

vec3 GetIrradiance(
    AtmosphereParameters atmosphere,
    sampler2D irradiance_texture,
    float PointHeight, float SunZenithCos)
{
    vec2 uv = GetIrradianceUV(atmosphere, PointHeight, SunZenithCos);
    return vec3(texture(irradiance_texture, uv).xyz);
}

// ----------------------------------------------------------------------------

vec3 GetSkyIrradiance(
    AtmosphereParameters atmosphere,
    sampler2D transmittance_texture,
    sampler2D irradiance_texture,
    vec3 spoint, vec3 normal, vec3 sun_direction)
{
    float PointHeight = length(spoint);
    float SunZenithCos = dot(spoint, sun_direction) / PointHeight;
	
	vec3 sky_irradiance = GetIrradiance(atmosphere, irradiance_texture, PointHeight, SunZenithCos);
	sky_irradiance /= atmosphere.StarIrradiance * (SUN_SPECTRAL_RADIANCE_TO_LUMINANCE / SKY_SPECTRAL_RADIANCE_TO_LUMINANCE);
	sky_irradiance *= SKY_IRRADIANCE_TO_RADIANCE;
	return sky_irradiance;
}

// ----------------------------------------------------------------------------
