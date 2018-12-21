/*
Copyright (C) 2018 Addis Dittebrandt
Copyright (C) 2018 Christoph Schied
Copyright (C) 2018 Florian Simon

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

#ifndef _LIGHT_HIERARCHY_
#define _LIGHT_HIERARCHY_

#define LH_STATIC_BINDING_IDX     0
#define MAX_LIGHT_HIERARCHY_NODES (1 << 20)

#define LH_COMPACT_NODES

#ifndef VKPT_SHADER

#include "vkpt.h"

typedef struct lh_cone_s
{
    vec3_t axis; // mean direction of lights in node
    float th_o; // max normal deviaton
    float th_e; // max emission angle
}
lh_cone_t;

typedef struct lh_child_s
{
    int i; // child index; in leaf: prim offset if c[0], num prims in c[1]
    float aabb[6];
    lh_cone_t cone;
    float energy;
}
lh_child_t;

typedef struct lh_node_s
{
    lh_child_t c[2];
}
lh_node_t;

/* this is the data structure actually uploaded to the gpu */
/* the struct is aliased with triangle data which is copied
 * on top of the struct beginning at c[0].aabb[0] */
typedef struct compact_lh_node_s {
	struct {
		float aabb[6]; 
		uint32_t axis;
		float th_o;
		//float energy;
	} c[2];
	int idx[2];
} compact_lh_node_t;

typedef struct light_hierarchy_s
{
    lh_node_t* nodes;
    int num_nodes;
    int max_num_nodes;

    int num_lights;
    int num_light_vertices;
    int num_light_indices;
}
light_hierarchy_t;


int lh_build_binned(void *dst, const float *positions, const uint32_t *colors, int num_prims, int num_bins);
void  lh_dump(light_hierarchy_t *lh, const char *path);
float lh_importance( const float p[3], const float n[3], lh_node_t* node, int child);

#else

#include "utils.glsl"

#define M_PI 3.1415926535897932384626433832795

struct LHCompactChild {
	float aabb[6];
	uint  axis;
	float th_o;
	//float energy;
};

struct LHCompactNode {
	LHCompactChild c[2];
	int idx[2];
};

layout(
	set     = LIGHT_HIERARCHY_SET_IDX,
	binding = LH_STATIC_BINDING_IDX,
   	std430)
buffer BufferLightHierarchyNodes {
	LHCompactNode lh_nodes[];
};

vec3
sample_triangle(vec2 xi)
{
	float sqrt_xi = sqrt(xi.x);
	return vec3(
		1.0 - sqrt_xi,
		sqrt_xi * (1.0 - xi.y),
		sqrt_xi * xi.y);
}

vec3
array_to_vec(float arr[3])
{
	return vec3(arr[0], arr[1], arr[2]);
}

void
aabb_to_vec(float aabb[6], out vec3 amin, out vec3 amax)
{
	amin = vec3(aabb[0], aabb[1], aabb[2]);
	amax = vec3(aabb[3], aabb[4], aabb[5]);
}

float
_acos(float x)
{
	//return acos(clamp(x, -1.0, 1.0));

	float a = -0.939115566365855;
	float b = 0.9217841528914573;
	float c = -1.2845906244690837;
	float d = 0.295624144969963174;
	return 3.1415926535 * 0.5 + (a * x + b * x * x * x) / (1 + c * x * x + d * x * x * x * x);

	//return 1.57079f - 1.57079f * x; /* not good enough */
}

#if 0
float
lh_importance(vec3 p, vec3 n, LHCompactChild child)
{
	vec3 amin, amax;
	aabb_to_vec(child.aabb, amin, amax);
	vec3 axis = decode_normal(child.axis);
	bool inside = all(greaterThan(p, amin))
	           && all(lessThan(p, amax));

	vec3 c = 0.5f * (amin + amax);
	vec3 pc = c - p;
	float d_sq = dot(pc, pc);
	//float len_pc = sqrt(d_sq);
	vec3 pc_n = pc / sqrt(d_sq);

	float th = _acos(-dot(axis, pc_n));
	float th_i = _acos(dot(n, pc_n));
	float cos_th_u = 1.0;

#define EDGE_DIST(x0, x1, x2)  \
	do { \
		vec3 e = vec3( \
				x0 == 0 ? amin[0] : amax[0], \
				x1 == 0 ? amin[1] : amax[1], \
				x2 == 0 ? amin[2] : amax[2]); \
		vec3 pe = normalize(e - p); \
		cos_th_u = min(cos_th_u, dot(pc_n, pe)); \
	} while(false)

	EDGE_DIST(0, 0, 0);
	EDGE_DIST(0, 0, 1);
	EDGE_DIST(0, 1, 0);
	EDGE_DIST(0, 1, 1);
	EDGE_DIST(1, 0, 0);
	EDGE_DIST(1, 0, 1);
	EDGE_DIST(1, 1, 0);
	EDGE_DIST(1, 1, 1);

	float th_u = _acos(cos_th_u);


	float th_h = max(th - child.th_o - th_u, 0.0f);
	float th_ih = max(th_i - th_u, 0.0f);

	float cos_th_h = th_h < 1.570796 ? cos(th_h) : 0.10f;
	return (abs(cos(th_ih)) * cos_th_h) / d_sq;
}
#endif

vec3
aabb_vertex(vec3 aabb[2], int i)
{
	float x = aabb[(i>>0) & 1].x;
	float y = aabb[(i>>1) & 1].y;
	float z = aabb[(i>>2) & 1].z;
	return vec3(x, y, z);
}

float
lh_importance(vec3 p, vec3 n, LHCompactChild child)
{
	vec3 aabb[2];// = child.aabb;
	aabb[0][0] = child.aabb[0];
	aabb[0][1] = child.aabb[1];
	aabb[0][2] = child.aabb[2];
	aabb[1][0] = child.aabb[3];
	aabb[1][1] = child.aabb[4];
	aabb[1][2] = child.aabb[5];
	//LHCone cone = child.cone;

	vec3 axis = decode_normal(child.axis);

	vec3 c = 0.5f * (aabb[0] + aabb[1]);
	vec3 pc = c - p;
	vec3 pc_n = normalize(pc);
	float d_sq = dot(pc, pc);

	float th_s = child.th_o + M_PI / 2.0;
	float th = _acos(dot(axis, -pc_n));
	float th_i = _acos(dot(n, pc_n));
	float _max = -1.0f/0.0f;
	float m_d = 1.0/0.0;
	vec3 m_cu;
	float cos_th_u = 1.0f;
#if 0
	// @compiler: please unroll
	for (int i = 0; i < 8; i++)
	{
		vec3 e = aabb_vertex(aabb, i);
		vec3 pe_n = normalize(e - p);

		_max = max(_max, dot(n, pe_n));

		float d = dot(e, axis);
		m_d = min(m_d, d);
		m_cu = m_d == d ? e : m_cu;

		cos_th_u = min(cos_th_u, dot(pc_n, pe_n));
	}
#endif
#define EDGE_DIST(x0, x1, x2)  \
	do { \
		vec3 e = vec3( \
				x0 == 0 ? aabb[0][0] : aabb[1][0], \
				x1 == 0 ? aabb[0][1] : aabb[1][1], \
				x2 == 0 ? aabb[0][2] : aabb[1][2]); \
		vec3 pe_n = normalize(e - p); \
		_max = max(_max, dot(n, pe_n)); \
		float d = dot(e, axis); \
		m_d = min(m_d, d); \
		m_cu = m_d == d ? e : m_cu; \
		cos_th_u = min(cos_th_u, dot(pc_n, pe_n)); \
	} while(false)

	EDGE_DIST(0, 0, 0);
	EDGE_DIST(0, 0, 1);
	EDGE_DIST(0, 1, 0);
	EDGE_DIST(0, 1, 1);
	EDGE_DIST(1, 0, 0);
	EDGE_DIST(1, 0, 1);
	EDGE_DIST(1, 1, 0);
	EDGE_DIST(1, 1, 1);
	vec3 cup_n = normalize(p - m_cu);

	if (_max <= 0 || (M_PI/2 <= th_s && th_s < _acos(dot(cup_n, axis)))) return 0.0f;

	//if (_max <= 0 || (M_PI >= th_s && cos(th_s) >= (dot(cup_n, cone.axis)))) return 0.0f;
	float th_u = _acos(cos_th_u);

	float th_h = max(th - child.th_o - th_u, 0.0f);
	float th_ih = max(th_i - th_u, 0.0f);

	float cos_th_h = th_h < (M_PI / 2.0) ? cos(th_h) : 0.00f;
	return (abs(cos(th_ih)) * cos_th_h) / d_sq;
	//return (abs(cos(th_ih)) * cos_th_h) / max(1e-2, d_sq);
	//return child.energy * (abs(cos(th_ih)) * cos_th_h) / max(1e-2, d_sq);
}


void
sample_light_hierarchy(vec3 p, vec3 n,
		out vec3 position_light,
		out vec3 normal_light,
		out vec3 light_color,
		out float pdf,
		vec3 rng)
{
	pdf = 1.0;

	int current_idx = 0;

	#pragma unroll
	for(int i = 0; i < 32; i++) {
		LHCompactNode curr = lh_nodes[current_idx];

		float I0 = lh_importance(p, n, curr.c[0]);
		float I1 = lh_importance(p, n, curr.c[1]);
		float thresh = I0 / (I0 + I1);
		//float thresh = (I0 + I1) > 1e-3 ? (I0 / (I0 + I1)) : 0.5;

		if(rng.x < thresh) {
			pdf *= thresh;
			rng.x /= thresh;
			current_idx = curr.idx[0];
		}
		else {
			pdf *= 1.0 - thresh;
			rng.x -= thresh;
			rng.x /= 1.0 - thresh;
			current_idx = curr.idx[1];
		}

		if(current_idx < 0)
			break;
	}

	//pdf = max(0.01, pdf);

	LHCompactNode light_source = lh_nodes[~current_idx];
	mat3 positions = mat3(
			light_source.c[0].aabb[0],
			light_source.c[0].aabb[1],
			light_source.c[0].aabb[2],
			light_source.c[0].aabb[3],
			light_source.c[0].aabb[4],
			light_source.c[0].aabb[5],
			uintBitsToFloat(light_source.c[0].axis), // I hope the compiler doesn't care
			light_source.c[0].th_o,
			light_source.c[1].aabb[0]);
			//light_source.c[0].energy);

	light_color = unpackUnorm4x8(floatBitsToUint(light_source.c[1].aabb[1])).rgb;
	//light_color = unpackUnorm4x8(floatBitsToUint(light_source.c[1].aabb[0])).rgb;

	vec3 bary = sample_triangle(rng.yz);

	position_light = positions * bary;
	normal_light   = cross(positions[1] - positions[0], positions[2] - positions[0]);
	float len = length(normal_light);
	pdf *= 2.0 / len;
	normal_light /= len;
}

#endif

#endif /*_LIGHT_HIERARCHY_*/

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
