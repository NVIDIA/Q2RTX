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

#pragma once

#if USE_REF
void R_ClearDebugLines(void);
void R_AddDebugLine(const vec3_t start, const vec3_t end, uint32_t color, uint32_t time, bool depth_test);
void R_AddDebugText(const vec3_t origin, const vec3_t angles, const char *text,
                    float size, uint32_t color, uint32_t time, bool depth_test);
void R_AddDebugPoint(const vec3_t point, float size, uint32_t color, uint32_t time, bool depth_test);
void R_AddDebugAxis(const vec3_t origin, const vec3_t angles, float size, uint32_t time, bool depth_test);
void R_AddDebugBounds(const vec3_t mins, const vec3_t maxs, uint32_t color, uint32_t time, bool depth_test);
void R_AddDebugSphere(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test);
void R_AddDebugCircle(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test);
void R_AddDebugCylinder(const vec3_t origin, float half_height, float radius, uint32_t color, uint32_t time, bool depth_test);
void R_AddDebugArrow(const vec3_t start, const vec3_t end, float size, uint32_t line_color,
                     uint32_t arrow_color, uint32_t time, bool depth_test);
void R_AddDebugCurveArrow(const vec3_t start, const vec3_t ctrl, const vec3_t end, float size,
                            uint32_t line_color, uint32_t arrow_color, uint32_t time, bool depth_test);
#else
#define R_ClearDebugLines()                     (void)0
#endif
