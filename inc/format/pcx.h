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
========================================================================

PCX files are used for as many images as possible

========================================================================
*/

typedef struct {
    uint8_t     manufacturer;
    uint8_t     version;
    uint8_t     encoding;
    uint8_t     bits_per_pixel;
    uint16_t    xmin, ymin, xmax, ymax;
    uint16_t    hres, vres;
    uint8_t     palette[48];
    uint8_t     reserved;
    uint8_t     color_planes;
    uint16_t    bytes_per_line;
    uint16_t    palette_type;
    uint8_t     filler[58];
    uint8_t     data[1];            // unbounded
} dpcx_t;
