/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#pragma once

/*
==============================================================================

.WAL texture file format

==============================================================================
*/

#define    MIPLEVELS    4

typedef struct {
    char        name[32];
    uint32_t    width, height;
    uint32_t    offsets[MIPLEVELS];     // four mip maps stored
    char        animname[32];           // next frame in animation chain
    uint32_t    flags;
    uint32_t    contents;
    uint32_t    value;
} miptex_t;
