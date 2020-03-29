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

#version 460
#extension GL_GOOGLE_include_directive    : enable

#include "path_tracer.h"

rayPayloadInEXT RayPayload ray_payload;

hitAttributeEXT vec3 hit_attribs;

void
main()
{
	ray_payload.barycentric    = hit_attribs.xy;
	ray_payload.instance_prim  = gl_PrimitiveID + gl_InstanceCustomIndexEXT & AS_INSTANCE_MASK_OFFSET;
	if((gl_InstanceCustomIndexEXT & AS_INSTANCE_FLAG_DYNAMIC) != 0)
	{
		ray_payload.instance_prim |= INSTANCE_DYNAMIC_FLAG;
	}
	if((gl_InstanceCustomIndexEXT & AS_INSTANCE_FLAG_SKY) != 0)
	{
		ray_payload.instance_prim |= INSTANCE_SKY_FLAG;
	}
	ray_payload.hit_distance   = gl_HitTEXT;
}
