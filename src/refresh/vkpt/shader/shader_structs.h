/*
Copyright (C) 2021, NVIDIA CORPORATION. All rights reserved.

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

#ifndef SHADER_STRUCTS_H

#ifdef VKPT_SHADER

#define BEGIN_SHADER_STRUCT(NAME) struct NAME
#define END_SHADER_STRUCT(NAME) ;

#else // VKPT_SHADER

#define BEGIN_SHADER_STRUCT(NAME) typedef struct
#define END_SHADER_STRUCT(NAME) NAME;

typedef uint32_t uint;
typedef float vec2[2];
typedef float vec3[3];
typedef float vec4[4];
typedef uint32_t uvec2[2];
typedef uint32_t uvec3[3];
typedef uint32_t uvec4[4];
typedef int ivec2[2];
typedef int ivec3[3];
typedef int ivec4[4];
typedef float mat3[3][3];
typedef float mat4[4][4];

#endif // VKPT_SHADER

#endif // SHADER_STRUCTS_H