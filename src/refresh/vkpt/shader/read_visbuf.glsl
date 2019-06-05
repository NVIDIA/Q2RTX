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

#define VISBUF_WORLD_INSTANCE_FLAG (1 << 31)
#define VISBUF_STATIC_GEOMETRY ~0u

uint
pack_instance_id_triangle_idx(uint instance_id, uint triangle_idx)
{
	return instance_id | (triangle_idx << 10);
}

void
unpack_instance_id_triangle_idx(uint u, out uint instance_id, out uint triangle_idx)
{
	instance_id  = (u &  VISBUF_WORLD_INSTANCE_FLAG) | (u & ((1 << 10) - 1));
	triangle_idx = (u & ~VISBUF_WORLD_INSTANCE_FLAG) >> 10;
}

bool visbuf_is_world_instance(uint u)     { return (u & VISBUF_WORLD_INSTANCE_FLAG) != 0; }
bool visbuf_is_static_world_model(uint u) { return u == VISBUF_STATIC_GEOMETRY; }
