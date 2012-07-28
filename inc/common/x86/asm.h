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

#ifndef X86_ASM_H
#define X86_ASM_H

#if HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef UNDERSCORES
#define C(label) label
#else
#define C(label) _##label
#endif

// cplane_t structure
#define pl_normal	0
#define pl_dist		12
#define pl_type		16
#define pl_signbits	17
#define pl_pad		18
#define pl_size		20

#endif // X86_ASM_H
