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

#include "shared/shared.h"
#include "shared/list.h"
#include "shared/debug.h"
#include "common/math.h"
#include "refresh/debug.h"
#include "refresh/refresh.h"
#include "../server/server.h"
#include <assert.h>

static r_debug_line_t debug_lines[MAX_DEBUG_LINES];
list_t r_debug_lines_free;
list_t r_debug_lines_active;

void R_ClearDebugLines(void)
{
	List_Init(&r_debug_lines_free);
	List_Init(&r_debug_lines_active);
}

static inline uint32_t R_DebugCurrentTime(void)
{
	return sv.framenum * SV_FRAMETIME;
}

bool R_DebugTimeExpired(const uint32_t time)
{
	return time <= R_DebugCurrentTime();
}

void R_ExpireDebugLines(void)
{
    r_debug_line_t *l, *next;

    if (LIST_EMPTY(&r_debug_lines_active))
        return;

    LIST_FOR_EACH_SAFE(r_debug_line_t, l, next, &r_debug_lines_active, entry) {
        if (R_DebugTimeExpired(l->time)) { // expired
            List_Remove(&l->entry);
            List_Insert(&r_debug_lines_free, &l->entry);
        }
    }
}

void R_AddDebugLine(const vec3_t start, const vec3_t end, uint32_t color, uint32_t time, bool depth_test)
{
    if (!R_SupportsDebugLines || !R_SupportsDebugLines())
        return;

    r_debug_line_t *l = LIST_FIRST(r_debug_line_t, &r_debug_lines_free, entry);

    if (LIST_EMPTY(&r_debug_lines_free)) {
        if (LIST_EMPTY(&r_debug_lines_active)) {
            for (int i = 0; i < MAX_DEBUG_LINES; i++)
                List_Append(&r_debug_lines_free, &debug_lines[i].entry);
        } else {
            r_debug_line_t *next;
            LIST_FOR_EACH_SAFE(r_debug_line_t, l, next, &r_debug_lines_active, entry) {
                if (R_DebugTimeExpired(l->time)) {
                    List_Remove(&l->entry);
                    List_Insert(&r_debug_lines_free, &l->entry);
                    break;
                }
            }
        }

        if (LIST_EMPTY(&r_debug_lines_free))
            l = LIST_FIRST(r_debug_line_t, &r_debug_lines_active, entry);
        else
            l = LIST_FIRST(r_debug_line_t, &r_debug_lines_free, entry);
    }

    // unlink from freelist
    List_Remove(&l->entry);
    List_Append(&r_debug_lines_active, &l->entry);

    VectorCopy(start, l->start);
    VectorCopy(end, l->end);
    l->color.u32 = color;
    l->time = time ? (R_DebugCurrentTime() + time) : 0;
    l->depth_test = depth_test;
}

#define U32_RED     MakeColor(255,   0,   0, 255)
#define U32_GREEN   MakeColor(  0, 255,   0, 255)
#define U32_BLUE    MakeColor(  0,   0, 255, 255)

#define GL_DRAWLINE(sx, sy, sz, ex, ey, ez) \
    R_AddDebugLine((const vec3_t) { (sx), (sy), (sz) }, (const vec3_t) { (ex), (ey), (ez) }, color, time, depth_test)

#define GL_DRAWLINEV(s, e) \
    R_AddDebugLine(s, e, color, time, depth_test)

void R_AddDebugPoint(const vec3_t point, float size, uint32_t color, uint32_t time, bool depth_test)
{
    size *= 0.5f;
    GL_DRAWLINE(point[0] - size, point[1], point[2], point[0] + size, point[1], point[2]);
    GL_DRAWLINE(point[0], point[1] - size, point[2], point[0], point[1] + size, point[2]);
    GL_DRAWLINE(point[0], point[1], point[2] - size, point[0], point[1], point[2] + size);
}

void R_AddDebugAxis(const vec3_t origin, const vec3_t angles, float size, uint32_t time, bool depth_test)
{
    vec3_t axis[3], end;
    uint32_t color;

    if (angles) {
        AnglesToAxis(angles, axis);
    } else {
        VectorSet(axis[0], 1, 0, 0);
        VectorSet(axis[1], 0, 1, 0);
        VectorSet(axis[2], 0, 0, 1);
    }

    color = U32_RED;
    VectorMA(origin, size, axis[0], end);
    GL_DRAWLINEV(origin, end);

    color = U32_GREEN;
    VectorMA(origin, size, axis[1], end);
    GL_DRAWLINEV(origin, end);

    color = U32_BLUE;
    VectorMA(origin, size, axis[2], end);
    GL_DRAWLINEV(origin, end);
}

void R_AddDebugBounds(const vec3_t mins, const vec3_t maxs, uint32_t color, uint32_t time, bool depth_test)
{
    for (int i = 0; i < 4; i++) {
        // draw column
        float x = ((i > 1) ? mins : maxs)[0];
        float y = ((((i + 1) % 4) > 1) ? mins : maxs)[1];
        GL_DRAWLINE(x, y, mins[2], x, y, maxs[2]);

        // draw bottom & top
        int n = (i + 1) % 4;
        float x2 = ((n > 1) ? mins : maxs)[0];
        float y2 = ((((n + 1) % 4) > 1) ? mins : maxs)[1];
        GL_DRAWLINE(x, y, mins[2], x2, y2, mins[2]);
        GL_DRAWLINE(x, y, maxs[2], x2, y2, maxs[2]);
    }
}

// https://danielsieger.com/blog/2021/03/27/generating-spheres.html
void R_AddDebugSphere(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test)
{
    vec3_t verts[160];
    const int n_stacks = min(4 + radius / 32, 10);
    const int n_slices = min(6 + radius / 32, 16);
    const int v0 = 0;
    int v1 = 1;

    for (int i = 0; i < n_stacks - 1; i++) {
        float phi = M_PIf * (i + 1) / n_stacks;
        for (int j = 0; j < n_slices; j++) {
            float theta = 2 * M_PIf * j / n_slices;
            vec3_t v = {
                sinf(phi) * cosf(theta),
                sinf(phi) * sinf(theta),
                cosf(phi)
            };
            VectorMA(origin, radius, v, verts[v1]);
            v1++;
        }
    }

    VectorCopy(origin, verts[v0]);
    VectorCopy(origin, verts[v1]);

    verts[v0][2] += radius;
    verts[v1][2] -= radius;

    for (int i = 0; i < n_slices; i++) {
        int i0 = i + 1;
        int i1 = (i + 1) % n_slices + 1;
        GL_DRAWLINEV(verts[v0], verts[i1]);
        GL_DRAWLINEV(verts[i1], verts[i0]);
        GL_DRAWLINEV(verts[i0], verts[v0]);
        i0 = i + n_slices * (n_stacks - 2) + 1;
        i1 = (i + 1) % n_slices + n_slices * (n_stacks - 2) + 1;
        GL_DRAWLINEV(verts[v1], verts[i0]);
        GL_DRAWLINEV(verts[i0], verts[i1]);
        GL_DRAWLINEV(verts[i1], verts[v1]);
    }

    for (int j = 0; j < n_stacks - 2; j++) {
        int j0 = j * n_slices + 1;
        int j1 = (j + 1) * n_slices + 1;
        for (int i = 0; i < n_slices; i++) {
            int i0 = j0 + i;
            int i1 = j0 + (i + 1) % n_slices;
            int i2 = j1 + (i + 1) % n_slices;
            int i3 = j1 + i;
            GL_DRAWLINEV(verts[i0], verts[i1]);
            GL_DRAWLINEV(verts[i1], verts[i2]);
            GL_DRAWLINEV(verts[i2], verts[i3]);
            GL_DRAWLINEV(verts[i3], verts[i0]);
        }
    }
}

void R_AddDebugCircle(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test)
{
    int vert_count = min(5 + radius / 8, 16);
    float rads = (2 * M_PIf) / vert_count;

    for (int i = 0; i < vert_count; i++) {
        float a = i * rads;
        float c = cosf(a);
        float s = sinf(a);
        float x = c * radius + origin[0];
        float y = s * radius + origin[1];

        a = ((i + 1) % vert_count) * rads;
        c = cosf(a);
        s = sinf(a);
        float x2 = c * radius + origin[0];
        float y2 = s * radius + origin[1];

        GL_DRAWLINE(x, y, origin[2], x2, y2, origin[2]);
    }
}

void R_AddDebugCylinder(const vec3_t origin, float half_height, float radius, uint32_t color, uint32_t time, bool depth_test)
{
    int vert_count = min(5 + radius / 8, 16);
    float rads = (2 * M_PIf) / vert_count;

    for (int i = 0; i < vert_count; i++) {
        float a = i * rads;
        float c = cosf(a);
        float s = sinf(a);
        float x = c * radius + origin[0];
        float y = s * radius + origin[1];

        a = ((i + 1) % vert_count) * rads;
        c = cosf(a);
        s = sinf(a);
        float x2 = c * radius + origin[0];
        float y2 = s * radius + origin[1];

        GL_DRAWLINE(x, y, origin[2] - half_height, x2, y2, origin[2] - half_height);
        GL_DRAWLINE(x, y, origin[2] + half_height, x2, y2, origin[2] + half_height);
        GL_DRAWLINE(x, y, origin[2] - half_height, x,  y,  origin[2] + half_height);
    }
}

static void R_DrawArrowCap(const vec3_t apex, const vec3_t dir, float size,
                           uint32_t color, uint32_t time, qboolean depth_test)
{
    vec3_t cap_end;
    VectorMA(apex, size, dir, cap_end);
    R_AddDebugLine(apex, cap_end, color, time, depth_test);

    vec3_t right, up;
    MakeNormalVectors(dir, right, up);

    vec3_t l;
    VectorMA(apex, size, right, l);
    R_AddDebugLine(l, cap_end, color, time, depth_test);

    VectorMA(apex, -size, right, l);
    R_AddDebugLine(l, cap_end, color, time, depth_test);
}

void R_AddDebugArrow(const vec3_t start, const vec3_t end, float size, uint32_t line_color,
                     uint32_t arrow_color, uint32_t time, bool depth_test)
{
    vec3_t dir;
    VectorSubtract(end, start, dir);
    float len = VectorNormalize(dir);

    if (len > size) {
        vec3_t line_end;
        VectorMA(start, len - size, dir, line_end);
        R_AddDebugLine(start, line_end, line_color, time, depth_test);
        R_DrawArrowCap(line_end, dir, size, arrow_color, time, depth_test);
    } else {
        R_DrawArrowCap(end, dir, len, arrow_color, time, depth_test);
    }
}

void R_AddDebugCurveArrow(const vec3_t start, const vec3_t ctrl, const vec3_t end, float size,
                          uint32_t line_color, uint32_t arrow_color, uint32_t time, bool depth_test)
{
    int num_points = Q_clip(Distance(start, end) / 32, 3, 24);
    vec3_t last_point;

    for (int i = 0; i <= num_points; i++) {
        float t = i / (float)num_points;
        float it = 1.0f - t;

        float a = it * it;
        float b = 2.0f * t * it;
        float c = t * t;

        vec3_t p = {
            a * start[0] + b * ctrl[0] + c * end[0],
            a * start[1] + b * ctrl[1] + c * end[1],
            a * start[2] + b * ctrl[2] + c * end[2]
        };

        if (i == num_points)
            R_AddDebugArrow(last_point, p, size, line_color, arrow_color, time, depth_test);
        else if (i)
            R_AddDebugLine(last_point, p, line_color, time, depth_test);

        VectorCopy(p, last_point);
    }
}

void R_AddDebugText(const vec3_t origin, const vec3_t angles, const char *text,
                    float size, uint32_t color, uint32_t time, bool depth_test)
{
    if (R_AddDebugText_)
        R_AddDebugText_(origin, angles, text, size, color, time, depth_test);
}
