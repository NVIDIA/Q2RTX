/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2024-2025 Frank Richter
Copyright (C) 2024-2025 Andrey Nazarov
Copyright (C) 2024-2025 Jonathan "Paril" Barkley

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

#ifndef REFRESH_DEBUG_H_
#define REFRESH_DEBUG_H_

#define MAX_DEBUG_LINES     8192
#define MAX_DEBUG_VERTICES  (MAX_DEBUG_LINES * 2)

typedef struct {
	list_t          entry;
	vec3_t          start, end;
	color_t         color;
	uint32_t        time;
	bool            depth_test;
} r_debug_line_t;

extern list_t r_debug_lines_free;
extern list_t r_debug_lines_active;

// Debug lines shared functionality
extern bool R_DebugTimeExpired(const uint32_t time);
extern void R_ExpireDebugLines(void);
extern void R_AddDebugText_Lines(const vec3_t vieworg, const vec3_t origin, const vec3_t angles, const char *text, float size, uint32_t color, uint32_t time, bool depth_test);
extern void R_InitDebugText(void);

#endif // REFRESH_DEBUG_H_
