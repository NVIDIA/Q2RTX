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

uint visbuf_pack_instance(uint instance_id, uint primitive_id)
{
	return (instance_id & VISBUF_INSTANCE_ID_MASK) 
		| ((primitive_id << VISBUF_INSTANCE_PRIM_SHIFT) & VISBUF_INSTANCE_PRIM_MASK);
}

uint visbuf_pack_static_prim(uint primitive_id)
{
	return (primitive_id & VISBUF_STATIC_PRIM_MASK)
		| VISBUF_STATIC_PRIM_FLAG;
}

uint visbuf_get_instance_id(uint u)
{
	return u & VISBUF_INSTANCE_ID_MASK;
}

uint visbuf_get_instance_prim(uint u)
{
	return (u & VISBUF_INSTANCE_PRIM_MASK) >> VISBUF_INSTANCE_PRIM_SHIFT;
}

uint visbuf_get_static_prim(uint u)
{
	return u & VISBUF_STATIC_PRIM_MASK;
}

bool visbuf_is_static_prim(uint u)
{
	return (u & VISBUF_STATIC_PRIM_FLAG) != 0;
}

uint visbuf_pack_barycentrics(vec3 bary)
{
	uvec2 encoded = uvec2(round(clamp(bary.yz, vec2(0), vec2(1)) * 0xFFFF));
	return encoded.x | (encoded.y << 16);
}

vec3 visbuf_unpack_barycentrics(uint u)
{
	uvec2 encoded = uvec2(u & 0xFFFF, u >> 16);

	vec3 bary;
	bary.yz = vec2(encoded) / 0xFFFF;
	bary.x = clamp(1.0 - (bary.y + bary.z), 0.0, 1.0);
	
	return bary;
}