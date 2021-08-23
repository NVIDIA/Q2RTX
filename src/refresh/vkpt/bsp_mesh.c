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

#include "vkpt.h"
#include "shader/global_textures.h"
#include "material.h"

#include <assert.h>
#include <float.h>

#define TINYOBJ_LOADER_C_IMPLEMENTATION
#include <tinyobj_loader_c.h>

extern cvar_t *cvar_pt_enable_nodraw;
extern cvar_t *cvar_pt_enable_surface_lights;
extern cvar_t *cvar_pt_enable_surface_lights_warp;
extern cvar_t *cvar_pt_bsp_radiance_scale;

static void
remove_collinear_edges(float* positions, float* tex_coords, int* num_vertices)
{
	int num_vertices_local = *num_vertices;

	for (int i = 1; i < num_vertices_local;)
	{
		float* p0 = positions + (i - 1) * 3;
		float* p1 = positions + (i % num_vertices_local) * 3;
		float* p2 = positions + ((i + 1) % num_vertices_local) * 3;

		vec3_t e1, e2;
		VectorSubtract(p1, p0, e1);
		VectorSubtract(p2, p1, e2);
		float l1 = VectorLength(e1);
		float l2 = VectorLength(e2);

		qboolean remove = qfalse;
		if (l1 == 0)
		{
			remove = qtrue;
		}
		else if (l2 > 0)
		{
			VectorScale(e1, 1.f / l1, e1);
			VectorScale(e2, 1.f / l2, e2);

			float dot = DotProduct(e1, e2);
			if (dot > 0.999f)
				remove = qtrue;
		}

		if (remove)
		{
			if (num_vertices_local - i >= 1)
			{
				memcpy(p1, p2, (num_vertices_local - i - 1) * 3 * sizeof(float));

				if (tex_coords)
				{
					float* t1 = tex_coords + (i % num_vertices_local) * 2;
					float* t2 = tex_coords + ((i + 1) % num_vertices_local) * 2;
					memcpy(t1, t2, (num_vertices_local - i - 1) * 2 * sizeof(float));
				}
			}

			num_vertices_local--;
		}
		else
		{
			i++;
		}
	}

	*num_vertices = num_vertices_local;
}

#define DUMP_WORLD_MESH_TO_OBJ 0
#if DUMP_WORLD_MESH_TO_OBJ
static FILE* obj_dump_file = NULL;
static int obj_vertex_num = 0;
#endif

static int
create_poly(
	const mface_t *surf,
	uint32_t  material_id,
	float    *positions_out,
	float    *tex_coord_out,
	uint32_t *material_out,
	float    *emissive_factors_out)
{
	static const int max_vertices = 32;
	float positions [3 * /*max_vertices*/ 32];
	float tex_coords[2 * /*max_vertices*/ 32];
	mtexinfo_t *texinfo = surf->texinfo;
	assert(surf->numsurfedges < max_vertices);
	
	float sc[2] = { 1.f, 1.f };
	if (texinfo->material)
	{
		image_t* image_diffuse = texinfo->material->image_base;
		if (image_diffuse && image_diffuse->width && image_diffuse->height) {
			sc[0] = 1.0f / (float)image_diffuse->width;
			sc[1] = 1.0f / (float)image_diffuse->height;
		}
	}

	float pos_center[3] = { 0 };
	float tc_center[2];

	for (int i = 0; i < surf->numsurfedges; i++) {
		msurfedge_t *src_surfedge = surf->firstsurfedge + i;
		medge_t     *src_edge     = src_surfedge->edge;
		mvertex_t   *src_vert     = src_edge->v[src_surfedge->vert];

		float *p = positions + i * 3;
		float *t = tex_coords + i * 2;

		VectorCopy(src_vert->point, p);

		pos_center[0] += src_vert->point[0];
		pos_center[1] += src_vert->point[1];
		pos_center[2] += src_vert->point[2];

		t[0] = (DotProduct(p, texinfo->axis[0]) + texinfo->offset[0]) * sc[0];
		t[1] = (DotProduct(p, texinfo->axis[1]) + texinfo->offset[1]) * sc[1];

#if DUMP_WORLD_MESH_TO_OBJ
		if (obj_dump_file)
		{
			fprintf(obj_dump_file, "v %.3f %.3f %.3f\n", src_vert->point[0], src_vert->point[1], src_vert->point[2]);
		}
#endif
	}

#if DUMP_WORLD_MESH_TO_OBJ
	if (obj_dump_file)
	{
		fprintf(obj_dump_file, "f ");
		for (int i = 0; i < surf->numsurfedges; i++) {
			fprintf(obj_dump_file, "%d ", obj_vertex_num);
			obj_vertex_num++;
		}
		fprintf(obj_dump_file, "\n");
	}
#endif

	pos_center[0] /= (float)surf->numsurfedges;
	pos_center[1] /= (float)surf->numsurfedges;
	pos_center[2] /= (float)surf->numsurfedges;

	tc_center[0] = (DotProduct(pos_center, texinfo->axis[0]) + texinfo->offset[0]) * sc[0];
	tc_center[1] = (DotProduct(pos_center, texinfo->axis[1]) + texinfo->offset[1]) * sc[1];

	int num_vertices = surf->numsurfedges;

	qboolean is_sky = MAT_IsKind(material_id, MATERIAL_KIND_SKY);
	if (is_sky)
	{
		// process skybox geometry in the same way as we process it for analytic light generation
		// to avoid mismatches between lights and geometry
		remove_collinear_edges(positions, tex_coords, &num_vertices);
	}

#define CP_V(idx, src) \
    do { \
        if(positions_out) { \
            memcpy(positions_out + (idx) * 3, src, sizeof(float) * 3); \
        } \
    } while(0)

#define CP_T(idx, src) \
    do { \
        if(tex_coord_out) { \
            memcpy(tex_coord_out + (idx) * 2, src, sizeof(float) * 2); \
        } \
    } while(0)

	int k = 0;
	/* switch between triangle fan around center or first vertex */
	//int tess_center = 0;
	int tess_center = num_vertices > 4 && !is_sky;

	const int num_triangles = tess_center
		? num_vertices
		: num_vertices - 2;

	const float emissive_factor = (texinfo->c.flags & SURF_LIGHT) && texinfo->material->bsp_radiance
		? (float)texinfo->radiance * cvar_pt_bsp_radiance_scale->value
		: 1.f;
	
	for (int i = 0; i < num_triangles; i++)
	{
		int i1 = (i + 2 - tess_center) % num_vertices;
		int i2 = (i + 1 - tess_center) % num_vertices;

		CP_V(k, tess_center ? pos_center : positions);
		CP_T(k, tess_center ? tc_center : tex_coords);
		k++;

		CP_V(k, positions + i1 * 3);
		CP_T(k, tex_coords + i1 * 2);
		k++;

		CP_V(k, positions + i2 * 3);
		CP_T(k, tex_coords + i2 * 2);
		k++;
		
		if (material_out) {
			material_out[i] = material_id;
		}

		if (emissive_factors_out) {
			emissive_factors_out[i] = emissive_factor;
		}
	}

#undef CP_V
#undef CP_T
#undef CP_M

	assert(k % 3 == 0);
	return k;
}

static int
belongs_to_model(bsp_t *bsp, mface_t *surf)
{
	for (int i = 0; i < bsp->nummodels; i++) {
		if (surf >= bsp->models[i].firstface
		&& surf < bsp->models[i].firstface + bsp->models[i].numfaces)
			return 1;
	}
	return 0;
}

static int filter_static_masked(int flags)
{
	const pbr_material_t* mat = MAT_ForIndex(flags & MATERIAL_INDEX_MASK);

	if (mat && mat->image_mask)
		return 1;

	return 0;
}

static int filter_static_opaque(int flags)
{
	if (filter_static_masked(flags))
		return 0;
	
	flags &= MATERIAL_KIND_MASK;
	if (flags == MATERIAL_KIND_SKY || flags == MATERIAL_KIND_WATER || flags == MATERIAL_KIND_SLIME || flags == MATERIAL_KIND_GLASS || flags == MATERIAL_KIND_TRANSPARENT)
		return 0;

	return 1;
}

static int filter_static_transparent(int flags)
{
	flags &= MATERIAL_KIND_MASK;
	if (flags == MATERIAL_KIND_WATER || flags == MATERIAL_KIND_SLIME || flags == MATERIAL_KIND_GLASS || flags == MATERIAL_KIND_TRANSPARENT)
		return 1;
	
	return 0;
}

static int filter_static_sky(int flags)
{
	if (MAT_IsKind(flags, MATERIAL_KIND_SKY))
		return 1;

	return 0;
}

static int filter_all(int flags)
{
	if (MAT_IsKind(flags, MATERIAL_KIND_SKY))
		return 0;

	return 1;
}

// Computes a point at a small distance above the center of the triangle.
// Returns qfalse if the triangle is degenerate, qtrue otherwise.
qboolean
get_triangle_off_center(const float* positions, float* center, float* anti_center, float offset)
{
	const float* v0 = positions + 0;
	const float* v1 = positions + 3;
	const float* v2 = positions + 6;

	// Compute the triangle center

	VectorCopy(v0, center);
	VectorAdd(center, v1, center);
	VectorAdd(center, v2, center);
	VectorScale(center, 1.f / 3.f, center);

	// Compute the normal

	vec3_t e1, e2, normal;
	VectorSubtract(v1, v0, e1);
	VectorSubtract(v2, v0, e2);
	CrossProduct(e1, e2, normal);
	float length = VectorNormalize(normal);

	// Offset the center by a fraction of the normal to make sure that the point is
	// inside a BSP leaf and not on a boundary plane.

	VectorScale(normal, offset, normal);
	VectorAdd(center, normal, center);

	if (anti_center)
	{
		VectorMA(center, -2.f, normal, anti_center);
	}

	return (length > 0.f);
}

static int
get_surf_light_style(const mface_t* surf)
{
	for (int nstyle = 0; nstyle < 4; nstyle++)
	{
		if (surf->styles[nstyle] != 0 && surf->styles[nstyle] != 255)
		{
			return surf->styles[nstyle];
		}
	}

	return 0;
}

static qboolean
get_surf_plane_equation(mface_t* surf, float* plane)
{
	// Go over multiple planes defined by different edge pairs of the surface.
	// Some of the edges may be collinear or almost-collinear to each other,
	// so we can't just take the first pair of edges.
	// We can't even take the first pair of edges with nonzero cross product
	// because of numerical issues - they may be almost-collinear.
	// So this function finds the plane equation from the triangle with the
	// largest area, i.e. the longest cross product.
	
	float maxlen = 0.f;
	for (int i = 0; i < surf->numsurfedges - 1; i++)
	{
		float* v0 = surf->firstsurfedge[i + 0].edge->v[surf->firstsurfedge[i + 0].vert]->point;
		float* v1 = surf->firstsurfedge[i + 1].edge->v[surf->firstsurfedge[i + 1].vert]->point;
		float* v2 = surf->firstsurfedge[(i + 2) % surf->numsurfedges].edge->v[surf->firstsurfedge[(i + 2) % surf->numsurfedges].vert]->point;
		vec3_t e0, e1;
		VectorSubtract(v1, v0, e0);
		VectorSubtract(v2, v1, e1);
		vec3_t normal;
		CrossProduct(e0, e1, normal);
		float len = VectorLength(normal);
		if (len > maxlen)
		{
			VectorScale(normal, 1.0f / len, plane);
			plane[3] = -DotProduct(plane, v0);
			maxlen = len;
		}
	}
	return (maxlen > 0.f);
}

static qboolean
is_sky_or_lava_cluster(bsp_mesh_t* wm, mface_t* surf, int cluster, int material_id)
{
	if (cluster < 0)
		return qfalse;

	if (MAT_IsKind(material_id, MATERIAL_KIND_LAVA) && wm->all_lava_emissive)
	{
		vec4_t plane;
		if (get_surf_plane_equation(surf, plane))
		{
			if (plane[2] < 0.f)
				return qtrue;
		}
	}
	else
	{
		for (int i = 0; i < wm->num_sky_clusters; i++)
		{
			if (wm->sky_clusters[i] == cluster)
			{
				return qtrue;
			}
		}
	}

	return qfalse;
}

static void merge_pvs_rows(bsp_t* bsp, byte* src, byte* dst)
{
	for (int i = 0; i < bsp->visrowsize; i++)
	{
		dst[i] |= src[i];
	}
}

#define FOREACH_BIT_BEGIN(SET,ROWSIZE,VAR) \
	for (int _byte_idx = 0; _byte_idx < (ROWSIZE); _byte_idx++) { \
	if (SET[_byte_idx]) { \
		for (int _bit_idx = 0; _bit_idx < 8; _bit_idx++) { \
			if (SET[_byte_idx] & (1 << _bit_idx)) { \
				int VAR = (_byte_idx << 3) | _bit_idx;

#define FOREACH_BIT_END  } } } }

static void connect_pvs(bsp_t* bsp, int cluster_a, byte* pvs_a, int cluster_b, byte* pvs_b)
{
	FOREACH_BIT_BEGIN(pvs_a, bsp->visrowsize, vis_cluster_a)
		if (vis_cluster_a != cluster_a && vis_cluster_a != cluster_b)
		{
			merge_pvs_rows(bsp, pvs_b, BSP_GetPvs(bsp, vis_cluster_a));
		}
	FOREACH_BIT_END

	FOREACH_BIT_BEGIN(pvs_b, bsp->visrowsize, vis_cluster_b)
		if (vis_cluster_b != cluster_a && vis_cluster_b != cluster_b)
		{
			merge_pvs_rows(bsp, pvs_a, BSP_GetPvs(bsp, vis_cluster_b));
		}
	FOREACH_BIT_END

	merge_pvs_rows(bsp, pvs_a, pvs_b);
	merge_pvs_rows(bsp, pvs_b, pvs_a);
}

static void make_pvs_symmetric(bsp_t* bsp)
{
	for (int cluster = 0; cluster < bsp->vis->numclusters; cluster++)
	{
		byte* pvs = BSP_GetPvs(bsp, cluster);

		FOREACH_BIT_BEGIN(pvs, bsp->visrowsize, vis_cluster)
			if (vis_cluster != cluster)
			{
				byte* vis_pvs = BSP_GetPvs(bsp, vis_cluster);
				Q_SetBit(vis_pvs, cluster);
			}
		FOREACH_BIT_END
	}
}

static void build_pvs2(bsp_t* bsp)
{
	size_t matrix_size = bsp->visrowsize * bsp->vis->numclusters;

	bsp->pvs2_matrix = Z_Mallocz(matrix_size);

	for (int cluster = 0; cluster < bsp->vis->numclusters; cluster++)
	{
		byte* pvs = BSP_GetPvs(bsp, cluster);
		byte* dest_pvs = BSP_GetPvs2(bsp, cluster);
		memcpy(dest_pvs, pvs, bsp->visrowsize);

		FOREACH_BIT_BEGIN(pvs, bsp->visrowsize, vis_cluster)
			byte* pvs2 = BSP_GetPvs(bsp, vis_cluster);
			merge_pvs_rows(bsp, pvs2, dest_pvs);
		FOREACH_BIT_END
	}

}

static void
collect_surfaces(int *idx_ctr, bsp_mesh_t *wm, bsp_t *bsp, int model_idx, int (*filter)(int))
{
	mface_t *surfaces = model_idx < 0 ? bsp->faces : bsp->models[model_idx].firstface;
	int num_faces = model_idx < 0 ? bsp->numfaces : bsp->models[model_idx].numfaces;
	qboolean any_pvs_patches = qfalse;

	for (int i = 0; i < num_faces; i++) {
		mface_t *surf = surfaces + i;

		if (model_idx < 0 && belongs_to_model(bsp, surf)) {
			continue;
		}

		uint32_t material_id = surf->texinfo->material ? surf->texinfo->material->flags : 0;
		uint32_t surf_flags = surf->drawflags | surf->texinfo->c.flags;

		// ugly hacks for situations when the same texture is used with different effects

		if ((MAT_IsKind(material_id, MATERIAL_KIND_WATER) || MAT_IsKind(material_id, MATERIAL_KIND_SLIME)) && !(surf_flags & SURF_WARP))
			material_id = MAT_SetKind(material_id, MATERIAL_KIND_REGULAR);

		if (MAT_IsKind(material_id, MATERIAL_KIND_GLASS) && !(surf_flags & SURF_TRANS_MASK))
			material_id = MAT_SetKind(material_id, MATERIAL_KIND_REGULAR);

		if ((surf_flags & SURF_NODRAW) && cvar_pt_enable_nodraw->integer)
			continue;

		// custom transparent surfaces
		if (surf_flags & SURF_SKY)
			material_id = MAT_SetKind(material_id, MATERIAL_KIND_SKY);

		if (MAT_IsKind(material_id, MATERIAL_KIND_REGULAR) && (surf_flags & SURF_TRANS_MASK) && !(material_id & MATERIAL_FLAG_LIGHT))
			material_id = MAT_SetKind(material_id, MATERIAL_KIND_TRANSPARENT);

		if (MAT_IsKind(material_id, MATERIAL_KIND_SCREEN) && (surf_flags & SURF_TRANS_MASK))
			material_id = MAT_SetKind(material_id, MATERIAL_KIND_GLASS);

		if (surf_flags & SURF_WARP)
			material_id |= MATERIAL_FLAG_WARP;

		if (surf_flags & SURF_FLOWING)
			material_id |= MATERIAL_FLAG_FLOWING;

		if (!filter(material_id))
			continue;

		if ((material_id & MATERIAL_FLAG_LIGHT) && surf->texinfo->material->light_styles)
		{
			int light_style = get_surf_light_style(surf);
			material_id |= (light_style << MATERIAL_LIGHT_STYLE_SHIFT) & MATERIAL_LIGHT_STYLE_MASK;
		}

		if (MAT_IsKind(material_id, MATERIAL_KIND_CAMERA) && wm->num_cameras > 0)
		{
			// Assign a random camera for this face
			int camera_id = rand() % (wm->num_cameras * 4);
			material_id = (material_id & ~MATERIAL_LIGHT_STYLE_MASK) | ((camera_id << MATERIAL_LIGHT_STYLE_SHIFT) & MATERIAL_LIGHT_STYLE_MASK);
		}

		if (*idx_ctr + create_poly(surf, material_id, NULL, NULL, NULL, NULL) >= MAX_VERT_BSP) {
			Com_Error(ERR_FATAL, "error: exceeding max vertex limit\n");
		}

		int cnt = create_poly(surf, material_id,
			&wm->positions[*idx_ctr * 3],
			&wm->tex_coords[*idx_ctr * 2],
			&wm->materials[*idx_ctr / 3],
			&wm->emissive_factors[*idx_ctr / 3]);

		for (int it = *idx_ctr / 3, k = 0; k < cnt; k += 3, ++it) 
		{
			if (model_idx < 0)
			{
				// Compute the BSP node for this specific triangle based on its center.
				// The face lists in the BSP are slightly incorrect, or the original code 
				// in q2vkpt that was extracting them was incorrect.

				vec3_t center, anti_center;
				get_triangle_off_center(wm->positions + it * 9, center, anti_center, 0.01f);

				int cluster = BSP_PointLeaf(bsp->nodes, center)->cluster;

				// If the small offset for the off-center point was too small, and that point
				// is not inside any cluster, try a larger offset.
				if (cluster < 0) {
					get_triangle_off_center(wm->positions + it * 9, center, anti_center, 1.f);
					cluster = BSP_PointLeaf(bsp->nodes, center)->cluster;
				}
				
				wm->clusters[it] = cluster;

				if (cluster >= 0 && (MAT_IsKind(material_id, MATERIAL_KIND_SKY) || MAT_IsKind(material_id, MATERIAL_KIND_LAVA)))
				{
					if(is_sky_or_lava_cluster(wm, surf, cluster, material_id))
					{
						wm->materials[it] |= MATERIAL_FLAG_LIGHT;
					}
				}

				if (!bsp->pvs_patched)
				{
					if (MAT_IsKind(material_id, MATERIAL_KIND_SLIME) || MAT_IsKind(material_id, MATERIAL_KIND_WATER) || MAT_IsKind(material_id, MATERIAL_KIND_GLASS) || MAT_IsKind(material_id, MATERIAL_KIND_TRANSPARENT))
					{
						int anti_cluster = BSP_PointLeaf(bsp->nodes, anti_center)->cluster;

						if (cluster >= 0 && anti_cluster >= 0 && cluster != anti_cluster)
						{
							byte* pvs_cluster = BSP_GetPvs(bsp, cluster);
							byte* pvs_anti_cluster = BSP_GetPvs(bsp, anti_cluster);

							if (!Q_IsBitSet(pvs_cluster, anti_cluster) || !Q_IsBitSet(pvs_anti_cluster, cluster))
							{
								connect_pvs(bsp, cluster, pvs_cluster, anti_cluster, pvs_anti_cluster);
								any_pvs_patches = qtrue;
							}
						}
					}
				}
			}
			else
				wm->clusters[it] = -1;
		}

		*idx_ctr += cnt;
	}

	if (any_pvs_patches)
		make_pvs_symmetric(bsp);
}

/*
  Sutherland-Hodgman polygon clipping algorithm, mostly copied from
  https://rosettacode.org/wiki/Sutherland-Hodgman_polygon_clipping#C
*/

typedef struct {
	float x, y;
} point2_t;

#define MAX_POLY_VERTS 32
typedef struct {
	point2_t v[MAX_POLY_VERTS];
	int len;
} poly_t;

static inline float
dot2(point2_t* a, point2_t* b)
{
	return a->x * b->x + a->y * b->y;
}

static inline float
cross2(point2_t* a, point2_t* b)
{
	return a->x * b->y - a->y * b->x;
}

static inline point2_t
vsub(point2_t* a, point2_t* b)
{
	point2_t res;
	res.x = a->x - b->x;
	res.y = a->y - b->y;
	return res;
}

/* tells if vec c lies on the left side of directed edge a->b
 * 1 if left, -1 if right, 0 if colinear
 */
static int
left_of(point2_t* a, point2_t* b, point2_t* c)
{
	float x;
	point2_t tmp1 = vsub(b, a);
	point2_t tmp2 = vsub(c, b);
	x = cross2(&tmp1, &tmp2);
	return x < 0 ? -1 : x > 0;
}

static int
line_sect(point2_t* x0, point2_t* x1, point2_t* y0, point2_t* y1, point2_t* res)
{
	point2_t dx, dy, d;
	dx = vsub(x1, x0);
	dy = vsub(y1, y0);
	d = vsub(x0, y0);
	/* x0 + a dx = y0 + b dy ->
	   x0 X dx = y0 X dx + b dy X dx ->
	   b = (x0 - y0) X dx / (dy X dx) */
	float dyx = cross2(&dy, &dx);
	if (!dyx) return 0;
	dyx = cross2(&d, &dx) / dyx;
	if (dyx <= 0 || dyx >= 1) return 0;

	res->x = y0->x + dyx * dy.x;
	res->y = y0->y + dyx * dy.y;
	return 1;
}

static void
poly_append(poly_t* p, point2_t* v)
{
	assert(p->len < MAX_POLY_VERTS - 1);
	p->v[p->len++] = *v;
}

static int
poly_winding(poly_t* p)
{
	return left_of(p->v, p->v + 1, p->v + 2);
}

static void
poly_edge_clip(poly_t* sub, point2_t* x0, point2_t* x1, int left, poly_t* res)
{
	int i, side0, side1;
	point2_t tmp;
	point2_t* v0 = sub->v + sub->len - 1;
	point2_t* v1;
	res->len = 0;

	side0 = left_of(x0, x1, v0);
	if (side0 != -left) poly_append(res, v0);

	for (i = 0; i < sub->len; i++) {
		v1 = sub->v + i;
		side1 = left_of(x0, x1, v1);
		if (side0 + side1 == 0 && side0)
			/* last point and current straddle the edge */
			if (line_sect(x0, x1, v0, v1, &tmp))
				poly_append(res, &tmp);
		if (i == sub->len - 1) break;
		if (side1 != -left) poly_append(res, v1);
		v0 = v1;
		side0 = side1;
	}
}

static void
clip_polygon(poly_t* input, poly_t* clipper, poly_t* output)
{
	int i;
	poly_t p1_m, p2_m;
	poly_t* p1 = &p1_m;
	poly_t* p2 = &p2_m;
	poly_t* tmp;

	int dir = poly_winding(clipper);
	poly_edge_clip(input, clipper->v + clipper->len - 1, clipper->v, dir, p2);
	for (i = 0; i < clipper->len - 1; i++) {
		tmp = p2; p2 = p1; p1 = tmp;
		if (p1->len == 0) {
			p2->len = 0;
			break;
		}
		poly_edge_clip(p1, clipper->v + i, clipper->v + i + 1, dir, p2);
	}

	output->len = p2->len;
	if (output->len)
		memcpy(output->v, p2->v, p2->len * sizeof(point2_t));
}

static light_poly_t*
append_light_poly(int* num_lights, int* allocated, light_poly_t** lights)
{
	if (*num_lights == *allocated)
	{
		*allocated = max(*allocated * 2, 128);
		*lights = Z_Realloc(*lights, *allocated * sizeof(light_poly_t));
	}
	return *lights + (*num_lights)++;
}

static inline qboolean
is_light_material(uint32_t material)
{
	return (material & MATERIAL_FLAG_LIGHT) != 0;
}

static void
collect_one_light_poly_entire_texture(bsp_t *bsp, mface_t *surf, mtexinfo_t *texinfo,
									  const vec3_t light_color, float emissive_factor, int light_style,
									  int *num_lights, int *allocated_lights, light_poly_t **lights)
{
	float positions[3 * /*max_vertices*/ 32];

	for (int i = 0; i < surf->numsurfedges; i++)
	{
		msurfedge_t *src_surfedge = surf->firstsurfedge + i;
		medge_t     *src_edge = src_surfedge->edge;
		mvertex_t   *src_vert = src_edge->v[src_surfedge->vert];

		float *p = positions + i * 3;

		VectorCopy(src_vert->point, p);
	}

	int num_vertices = surf->numsurfedges;
	remove_collinear_edges(positions, NULL, &num_vertices);

	const int num_triangles = surf->numsurfedges - 2;

	for (int i = 0; i < num_triangles; i++)
	{
		const int e = surf->numsurfedges;

		int i1 = (i + 2) % e;
		int i2 = (i + 1) % e;

		light_poly_t light;
		VectorCopy(positions, light.positions + 0);
		VectorCopy(positions + i1 * 3, light.positions + 3);
		VectorCopy(positions + i2 * 3, light.positions + 6);
		VectorScale(light_color, emissive_factor, light.color);

		light.material = texinfo->material;
		light.style = light_style;

		if(!get_triangle_off_center(light.positions, light.off_center, NULL, 1.f))
			continue;

		light.cluster = BSP_PointLeaf(bsp->nodes, light.off_center)->cluster;
		light.emissive_factor = emissive_factor;

		if(light.cluster >= 0)
		{
			light_poly_t* list_light = append_light_poly(num_lights, allocated_lights, lights);
			memcpy(list_light, &light, sizeof(light_poly_t));
		}
	}
}

static void
collect_one_light_poly(bsp_t *bsp, mface_t *surf, mtexinfo_t *texinfo, int model_idx, const vec4_t plane,
					   const float tex_scale[], const vec2_t min_light_texcoord, const vec2_t max_light_texcoord,
					   const vec3_t light_color, float emissive_factor, int light_style,
					   int* num_lights, int* allocated_lights, light_poly_t** lights)
{
	// Scale the texture axes according to the original resolution of the game's .wal textures
	vec4_t tex_axis0, tex_axis1;
	VectorScale(texinfo->axis[0], tex_scale[0], tex_axis0);
	VectorScale(texinfo->axis[1], tex_scale[1], tex_axis1);
	tex_axis0[3] = texinfo->offset[0] * tex_scale[0];
	tex_axis1[3] = texinfo->offset[1] * tex_scale[1];

	// The texture basis is not normalized, so we need the lengths of the axes to convert
	// texture coordinates back into world space
	float tex_axis0_inv_square_length = 1.0f / DotProduct(tex_axis0, tex_axis0);
	float tex_axis1_inv_square_length = 1.0f / DotProduct(tex_axis1, tex_axis1);

	// Find the normal of the texture plane
	vec3_t tex_normal;
	CrossProduct(tex_axis0, tex_axis1, tex_normal);
	VectorNormalize(tex_normal);

	float surf_normal_dot_tex_normal = DotProduct(tex_normal, plane);

	if (surf_normal_dot_tex_normal == 0.f)
	{
		// Surface is perpendicular to texture plane, which means we can't un-project
		// texture coordinates back onto the surface. This shouldn't happen though,
		// so it should be safe to skip such lights.
		return;
	}

	// Construct the surface polygon in texture space, and find its texture extents

	poly_t tex_poly;
	tex_poly.len = surf->numsurfedges;

	point2_t tex_min = { FLT_MAX, FLT_MAX };
	point2_t tex_max = { -FLT_MAX, -FLT_MAX };

	for (int i = 0; i < surf->numsurfedges; i++)
	{
		msurfedge_t *src_surfedge = surf->firstsurfedge + i;
		medge_t     *src_edge = src_surfedge->edge;
		mvertex_t   *src_vert = src_edge->v[src_surfedge->vert];
		
		point2_t t;
		t.x = DotProduct(src_vert->point, tex_axis0) + tex_axis0[3];
		t.y = DotProduct(src_vert->point, tex_axis1) + tex_axis1[3];

		tex_poly.v[i] = t;

		tex_min.x = min(tex_min.x, t.x);
		tex_min.y = min(tex_min.y, t.y);
		tex_max.x = max(tex_max.x, t.x);
		tex_max.y = max(tex_max.y, t.y);
	}

	// Instantiate a square polygon for every repetition of the texture in this surface,
	// then clip the original surface against that square polygon.

	for (float y_tile = floorf(tex_min.y); y_tile <= ceilf(tex_max.y); y_tile++)
	{
		for (float x_tile = floorf(tex_min.x); x_tile <= ceilf(tex_max.x); x_tile++)
		{
			float x_min = x_tile + min_light_texcoord[0];
			float x_max = x_tile + max_light_texcoord[0];
			float y_min = y_tile + min_light_texcoord[1];
			float y_max = y_tile + max_light_texcoord[1];

			// The square polygon, for this repetition, according to the extents of emissive pixels

			poly_t clipper;
			clipper.len = 4;
			clipper.v[0].x = x_min; clipper.v[0].y = y_min;
			clipper.v[1].x = x_max; clipper.v[1].y = y_min;
			clipper.v[2].x = x_max; clipper.v[2].y = y_max;
			clipper.v[3].x = x_min; clipper.v[3].y = y_max;

			// Clip it

			poly_t instance;
			clip_polygon(&tex_poly, &clipper, &instance);

			if (instance.len < 3)
			{
				// The square polygon was outside of the original surface
				continue;
			}

			// Map the clipped polygon back onto the surface plane

			vec3_t instance_positions[MAX_POLY_VERTS];
			for (int vert = 0; vert < instance.len; vert++)
			{
				// Find a world space point on the texture projection plane

				vec3_t p0, p1, point_on_texture_plane;
				VectorScale(tex_axis0, (instance.v[vert].x - tex_axis0[3]) * tex_axis0_inv_square_length, p0);
				VectorScale(tex_axis1, (instance.v[vert].y - tex_axis1[3]) * tex_axis1_inv_square_length, p1);
				VectorAdd(p0, p1, point_on_texture_plane);

				// Shoot a ray from that point in the texture normal direction,
				// and intersect it with the surface plane.

				// plane: P.N + d = 0
				// ray: P = At + B
				// (At + B).N + d = 0
				// (A.N)t + B.N + d = 0
				// t = -(B.N + d) / (A.N)

				float bn = DotProduct(point_on_texture_plane, plane);

				float ray_t = -(bn + plane[3]) / surf_normal_dot_tex_normal;

				vec3_t p2;
				VectorScale(tex_normal, ray_t, p2);
				VectorAdd(p2, point_on_texture_plane, instance_positions[vert]);
			}

			// Create triangles for the polygon, using a triangle fan topology

			const int num_triangles = instance.len - 2;

			for (int i = 0; i < num_triangles; i++)
			{
				const int e = instance.len;

				int i1 = (i + 2) % e;
				int i2 = (i + 1) % e;

				light_poly_t* light = append_light_poly(num_lights, allocated_lights, lights);
				light->material = texinfo->material;
				light->style = light_style;
				light->emissive_factor = emissive_factor;
				VectorCopy(instance_positions[0], light->positions + 0);
				VectorCopy(instance_positions[i1], light->positions + 3);
				VectorCopy(instance_positions[i2], light->positions + 6);
				VectorScale(light_color, emissive_factor, light->color);
				
				get_triangle_off_center(light->positions, light->off_center, NULL, 1.f);

				if (model_idx < 0)
				{
					// Find the cluster for this triangle
					light->cluster = BSP_PointLeaf(bsp->nodes, light->off_center)->cluster;

					if (light->cluster < 0)
					{
						// Cluster not found - which happens sometimes.
						// The lighting system can't work with lights that have no cluster, so remove the triangle.
						(*num_lights)--;
					}
				}
				else
				{
					// It's a model: cluster will be determined after model instantiation.
					light->cluster = -1;
				}
			}
		}
	}

}

static qboolean
collect_frames_emissive_info(pbr_material_t* material, qboolean* entire_texture_emissive, vec2_t min_light_texcoord, vec2_t max_light_texcoord, vec3_t light_color)
{
	*entire_texture_emissive = qfalse;
	min_light_texcoord[0] = min_light_texcoord[1] = 1.0f;
	max_light_texcoord[0] = max_light_texcoord[1] = 0.0f;

	qboolean any_emissive_valid = qfalse;
	pbr_material_t *current_material = material;
	do
	{
		const image_t *image = current_material->image_emissive;
		if (!image)
		{
			current_material = r_materials + current_material->next_frame;
			continue;
		}
		if(!any_emissive_valid)
		{
			// emissive light color of first frame
			memcpy(light_color, image->light_color, sizeof(vec3_t));
		}
		any_emissive_valid = qtrue;

		*entire_texture_emissive |= image->entire_texture_emissive;
		min_light_texcoord[0] = MIN(min_light_texcoord[0], image->min_light_texcoord[0]);
		min_light_texcoord[1] = MIN(min_light_texcoord[1], image->min_light_texcoord[1]);
		max_light_texcoord[0] = MAX(max_light_texcoord[0], image->max_light_texcoord[0]);
		max_light_texcoord[1] = MAX(max_light_texcoord[1], image->max_light_texcoord[1]);
		current_material = r_materials + current_material->next_frame;
	} while (current_material != material);

	return any_emissive_valid;
}

static void
collect_light_polys(bsp_mesh_t *wm, bsp_t *bsp, int model_idx, int* num_lights, int* allocated_lights, light_poly_t** lights)
{
	mface_t *surfaces = model_idx < 0 ? bsp->faces : bsp->models[model_idx].firstface;
	int num_faces = model_idx < 0 ? bsp->numfaces : bsp->models[model_idx].numfaces;

	for (int i = 0; i < num_faces; i++)
	{
		mface_t *surf = surfaces + i;

		if (model_idx < 0 && belongs_to_model(bsp, surf))
			continue;

		mtexinfo_t *texinfo = surf->texinfo;

		if(!texinfo->material)
			continue;

		// Check if any animation frame is a light material
		qboolean any_light_frame = qfalse;
		{
			pbr_material_t *current_material = texinfo->material;
			do
			{
				any_light_frame |= is_light_material(current_material->flags);
				current_material = r_materials + current_material->next_frame;
			} while (current_material != texinfo->material);
		}
		if(!any_light_frame)
			continue;

		uint32_t material_id = texinfo->material->flags;

		// Collect emissive texture info from across frames
		qboolean entire_texture_emissive;
		vec2_t min_light_texcoord;
		vec2_t max_light_texcoord;
		vec3_t light_color;

		if (!collect_frames_emissive_info(texinfo->material, &entire_texture_emissive, min_light_texcoord, max_light_texcoord, light_color))
		{
			// This algorithm relies on information from the emissive texture,
			// specifically the extents of the emissive pixels in that texture.
			// Ignore surfaces that don't have an emissive texture attached.
			continue;
		}

		float emissive_factor = (texinfo->c.flags & SURF_LIGHT) && texinfo->material->bsp_radiance
			? (float)texinfo->radiance * cvar_pt_bsp_radiance_scale->value
			: 1.f;

		int light_style = (texinfo->material->light_styles) ? get_surf_light_style(surf) : 0;

		if (entire_texture_emissive)
		{
			collect_one_light_poly_entire_texture(bsp, surf, texinfo, light_color, emissive_factor, light_style,
												  num_lights, allocated_lights, lights);
			continue;
		}

		vec4_t plane;
		if (!get_surf_plane_equation(surf, plane))
		{
			// It's possible that some polygons in the game are degenerate, ignore these.
			continue;
		}

		image_t* image_diffuse = texinfo->material->image_base;
		float tex_scale[2] = { 1.0f / image_diffuse->width, 1.0f / image_diffuse->height };

		collect_one_light_poly(bsp, surf, texinfo, model_idx, plane,
							   tex_scale, min_light_texcoord, max_light_texcoord,
							   light_color, emissive_factor, light_style,
							   num_lights, allocated_lights, lights);
	}
}

static void
collect_sky_and_lava_light_polys(bsp_mesh_t *wm, bsp_t* bsp)
{
	for (int i = 0; i < bsp->numfaces; i++)
	{
		mface_t *surf = bsp->faces + i;

		if (belongs_to_model(bsp, surf))
			continue;

		int flags = surf->drawflags;
		if (surf->texinfo) flags |= surf->texinfo->c.flags;

		qboolean is_sky = !!(flags & SURF_SKY);
		qboolean is_lava = surf->texinfo->material ? MAT_IsKind(surf->texinfo->material->flags, MATERIAL_KIND_LAVA) : qfalse;

		is_lava &= (surf->texinfo->material->image_emissive != NULL);

		if (!is_sky && !is_lava)
			continue;

		float positions[3 * /*max_vertices*/ 32];

		for (int i = 0; i < surf->numsurfedges; i++)
		{
			msurfedge_t *src_surfedge = surf->firstsurfedge + i;
			medge_t     *src_edge = src_surfedge->edge;
			mvertex_t   *src_vert = src_edge->v[src_surfedge->vert];

			float *p = positions + i * 3;

			VectorCopy(src_vert->point, p);
		}

		int num_vertices = surf->numsurfedges;
		remove_collinear_edges(positions, NULL, &num_vertices);

		const int num_triangles = num_vertices - 2;

		for (int i = 0; i < num_triangles; i++)
		{
			int i1 = (i + 2) % num_vertices;
			int i2 = (i + 1) % num_vertices;

			light_poly_t light;
			VectorCopy(positions, light.positions + 0);
			VectorCopy(positions + i1 * 3, light.positions + 3);
			VectorCopy(positions + i2 * 3, light.positions + 6);

			if (is_sky)
			{
				VectorSet(light.color, -1.f, -1.f, -1.f); // special value for the sky
				light.material = 0;
			}
			else
			{
				VectorCopy(surf->texinfo->material->image_emissive->light_color, light.color);
				light.material = surf->texinfo->material;
			}

			light.style = 0;

			if (!get_triangle_off_center(light.positions, light.off_center, NULL, 1.f))
				continue;

			light.cluster = BSP_PointLeaf(bsp->nodes, light.off_center)->cluster;
			
			if (is_sky_or_lava_cluster(wm, surf, light.cluster, surf->texinfo->material->flags))
			{
				light_poly_t* list_light = append_light_poly(&wm->num_light_polys, &wm->allocated_light_polys, &wm->light_polys);
				memcpy(list_light, &light, sizeof(light_poly_t));
			}
		}
	}
}

static qboolean
is_model_transparent(bsp_mesh_t *wm, bsp_model_t *model)
{
	if (model->idx_count == 0)
		return qfalse;

	for (int i = 0; i < model->idx_count / 3; i++)
	{
		int prim = model->idx_offset / 3 + i;
		int material = wm->materials[prim];
		
		if (!(MAT_IsKind(material, MATERIAL_KIND_SLIME) || MAT_IsKind(material, MATERIAL_KIND_WATER) || MAT_IsKind(material, MATERIAL_KIND_GLASS) || MAT_IsKind(material, MATERIAL_KIND_TRANSPARENT)))
			return qfalse;
	}

	return qtrue;
}

static qboolean
is_model_masked(bsp_mesh_t *wm, bsp_model_t *model)
{
	if (model->idx_count == 0)
		return qfalse;

	for (int i = 0; i < model->idx_count / 3; i++)
	{
		int prim = model->idx_offset / 3 + i;
		int material = wm->materials[prim];

		const pbr_material_t* mat = MAT_ForIndex(material & MATERIAL_INDEX_MASK);
		
		if (mat && mat->image_mask)
			return qtrue;
	}

	return qfalse;
}

// direct port of the encode_normal function from utils.glsl
uint32_t
encode_normal(vec3_t normal)
{
	float invL1Norm = 1.0f / (fabsf(normal[0]) + fabsf(normal[1]) + fabsf(normal[2]));

    vec2_t p = { normal[0] * invL1Norm, normal[1] * invL1Norm };
	vec2_t pp = { p[0], p[1] };

    if(normal[2] < 0.f)
    {
    	pp[0] = (1.f - fabsf(p[1])) * ((p[0] >= 0.f) ? 1.f : -1.f);
    	pp[1] = (1.f - fabsf(p[0])) * ((p[1] >= 0.f) ? 1.f : -1.f);
    }

    pp[0] = pp[0] * 0.5f + 0.5f;
    pp[1] = pp[1] * 0.5f + 0.5f;

    clamp(pp[0], 0.f, 1.f);
    clamp(pp[1], 0.f, 1.f);

    uint32_t ux = (uint32_t)(pp[0] * 0xffffu);
    uint32_t uy = (uint32_t)(pp[1] * 0xffffu);

    return ux | (uy << 16);
}

void
compute_aabb(const float* positions, int numvert, float* aabb_min, float* aabb_max)
{
	VectorSet(aabb_min, FLT_MAX, FLT_MAX, FLT_MAX);
	VectorSet(aabb_max, -FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (int i = 0; i < numvert; i++)
	{
		float const* position = positions + i * 3;

		aabb_min[0] = min(aabb_min[0], position[0]);
		aabb_min[1] = min(aabb_min[1], position[1]);
		aabb_min[2] = min(aabb_min[2], position[2]);

		aabb_max[0] = max(aabb_max[0], position[0]);
		aabb_max[1] = max(aabb_max[1], position[1]);
		aabb_max[2] = max(aabb_max[2], position[2]);
	}
}

void
compute_world_tangents(bsp_mesh_t* wm)
{
	// compute tangent space
	uint32_t ntriangles = wm->num_indices / 3;

	// tangent space is co-planar to triangle : only need to compute
	// 1 vertex because all 3 verts share the same tangent space
	wm->tangents = Z_Malloc(MAX_VERT_BSP * sizeof(uint32_t) / 3);
	wm->texel_density = Z_Malloc(MAX_VERT_BSP * sizeof(float) / 3);

	for (int idx_tri = 0; idx_tri < ntriangles; ++idx_tri)
	{
		uint32_t iA = wm->indices[idx_tri * 3 + 0]; // no vertex indexing
		uint32_t iB = wm->indices[idx_tri * 3 + 1];
		uint32_t iC = wm->indices[idx_tri * 3 + 2];

		float const * pA = wm->positions + (iA * 3);
		float const * pB = wm->positions + (iB * 3);
		float const * pC = wm->positions + (iC * 3);

		float const * tA = wm->tex_coords + (iA * 2);
		float const * tB = wm->tex_coords + (iB * 2);
		float const * tC = wm->tex_coords + (iC * 2);

		vec3_t dP0, dP1;
		VectorSubtract(pB, pA, dP0);
		VectorSubtract(pC, pA, dP1);

		vec2_t dt0, dt1;
		Vector2Subtract(tB, tA, dt0);
		Vector2Subtract(tC, tA, dt1);

		float r = 1.f / (dt0[0] * dt1[1] - dt1[0] * dt0[1]);

		vec3_t sdir = {
			(dt1[1] * dP0[0] - dt0[1] * dP1[0]) * r,
			(dt1[1] * dP0[1] - dt0[1] * dP1[1]) * r,
			(dt1[1] * dP0[2] - dt0[1] * dP1[2]) * r };

		vec3_t tdir = {
			(dt0[0] * dP1[0] - dt1[0] * dP0[0]) * r,
			(dt0[0] * dP1[1] - dt1[0] * dP0[1]) * r,
			(dt0[0] * dP1[2] - dt1[0] * dP0[2]) * r };

		vec3_t normal;
		CrossProduct(dP0, dP1, normal);
		VectorNormalize(normal);

		vec3_t tangent;

		vec3_t t;
		VectorScale(normal, DotProduct(normal, sdir), t);
		VectorSubtract(sdir, t, t);
		VectorNormalize2(t, tangent); // Graham-Schmidt : t = normalize(t - n * (n.t))

		wm->tangents[idx_tri] = encode_normal(tangent);

		vec3_t cross;
		CrossProduct(normal, t, cross);
		float dot = DotProduct(cross, tdir);

		if (dot < 0.0f)
		{
			wm->materials[idx_tri] |= MATERIAL_FLAG_HANDEDNESS;
		}

		float texel_density = 0.f;
		int material_idx = wm->materials[idx_tri] & MATERIAL_INDEX_MASK;
		pbr_material_t* mat = MAT_ForIndex(material_idx);
		if (mat && mat->image_base)
		{
			dt0[0] *= mat->image_base->width;
			dt0[1] *= mat->image_base->height;
			dt1[0] *= mat->image_base->width;
			dt1[1] *= mat->image_base->height;

			float WL0 = VectorLength(dP0);
			float WL1 = VectorLength(dP1);
			float TL0 = sqrt(dt0[0] * dt0[0] + dt0[1] * dt0[1]);
			float TL1 = sqrt(dt1[0] * dt1[0] + dt1[1] * dt1[1]);
			float L0 = (WL0 > 0) ? (TL0 / WL0) : 0.f;
			float L1 = (WL1 > 0) ? (TL1 / WL1) : 0.f;

			texel_density = max(L0, L1);
		}

		wm->texel_density[idx_tri] = texel_density;
	}
}

static void
load_sky_and_lava_clusters(bsp_mesh_t* wm, const char* map_name)
{
	wm->num_sky_clusters = 0;
	wm->all_lava_emissive = qfalse;

    // try a map-specific file first
    char filename[MAX_QPATH];
    Q_snprintf(filename, sizeof(filename), "maps/sky/%s.txt", map_name);

    qboolean found_map = qfalse;

    char* filebuf = NULL;
    FS_LoadFile(filename, (void**)&filebuf);
    
    if (filebuf)
    {
        // we have a map-specific file - no need to look for map name
        found_map = qtrue;
    }
    else
    {
        // try to load the global file
        FS_LoadFile("sky_clusters.txt", (void**)&filebuf);
        if (!filebuf)
        {
            Com_WPrintf("Couldn't read sky_clusters.txt\n");
            return;
        }
    }

	char const * ptr = (char const *)filebuf;
	char linebuf[1024];

	while (sgets(linebuf, sizeof(linebuf), &ptr))
	{
		{ char* t = strchr(linebuf, '#'); if (t) *t = 0; }   // remove comments
		{ char* t = strchr(linebuf, '\n'); if (t) *t = 0; }  // remove newline

		const char* delimiters = " \t\r\n";
		
		const char* word = strtok(linebuf, delimiters);
		while (word)
		{
			if (word[0] >= 'a' && word[0] <= 'z' || word[0] >= 'A' && word[0] <= 'Z')
			{
				qboolean matches = strcmp(word, map_name) == 0;

				if (!found_map && matches)
				{
					found_map = qtrue;
				}
				else if (found_map && !matches)
				{
					Z_Free(filebuf);
					return;
				}
			}
			else if (found_map)
			{
				assert(wm->num_sky_clusters < MAX_SKY_CLUSTERS);

				if (!strcmp(word, "!all_lava"))
					wm->all_lava_emissive = qtrue;
				else
				{
					int cluster = atoi(word);
					wm->sky_clusters[wm->num_sky_clusters++] = cluster;
				}
			}

			word = strtok(NULL, delimiters);
		}
	}

	Z_Free(filebuf);
}

static void
load_cameras(bsp_mesh_t* wm, const char* map_name)
{
	wm->num_cameras = 0;

	char* filebuf = NULL;
	FS_LoadFile("cameras.txt", (void**)&filebuf);
	if (!filebuf)
	{
		Com_WPrintf("Couldn't read cameras.txt\n");
		return;
	}

	char const * ptr = (char const *)filebuf;
	char linebuf[1024];
	qboolean found_map = qfalse;

	while (sgets(linebuf, sizeof(linebuf), &ptr))
	{
		{ char* t = strchr(linebuf, '#'); if (t) *t = 0; }   // remove comments
		{ char* t = strchr(linebuf, '\n'); if (t) *t = 0; }  // remove newline


		vec3_t pos, dir;
		if (linebuf[0] >= 'a' && linebuf[0] <= 'z' || linebuf[0] >= 'A' && linebuf[0] <= 'Z')
		{
			const char* delimiters = " \t\r\n";
			const char* word = strtok(linebuf, delimiters);
			qboolean matches = strcmp(word, map_name) == 0;

			if (!found_map && matches)
			{
				found_map = qtrue;
			}
			else if (found_map && !matches)
			{
				Z_Free(filebuf);
				return;
			}
		}
		else if (found_map && sscanf(linebuf, "(%f, %f, %f) (%f, %f, %f)", &pos[0], &pos[1], &pos[2], &dir[0], &dir[1], &dir[2]) == 6)
		{
			if (wm->num_cameras < MAX_CAMERAS)
			{
				VectorCopy(pos, wm->cameras[wm->num_cameras].pos);
				VectorCopy(dir, wm->cameras[wm->num_cameras].dir);
				wm->num_cameras++;
			}
		}
	}

	Z_Free(filebuf);
}

static void
compute_sky_visibility(bsp_mesh_t *wm, bsp_t *bsp)
{
	memset(wm->sky_visibility, 0, VIS_MAX_BYTES);

	if (wm->world_sky_count == 0 && wm->world_custom_sky_count == 0)
		return; 

	int numclusters = bsp->vis->numclusters;

	char clusters_with_sky[VIS_MAX_BYTES];

	memset(clusters_with_sky, 0, VIS_MAX_BYTES);
	
	for (int i = 0; i < (wm->world_sky_count + wm->world_custom_sky_count) / 3; i++)
	{
		int prim = wm->world_sky_offset / 3 + i;

		int cluster = wm->clusters[prim];
		if ((cluster >> 3) < VIS_MAX_BYTES)
			clusters_with_sky[cluster >> 3] |= (1 << (cluster & 7));
	}

	for (int cluster = 0; cluster < numclusters; cluster++)
	{
		if (clusters_with_sky[cluster >> 3] & (1 << (cluster & 7)))
		{
			byte* mask = BSP_GetPvs(bsp, cluster);

			for (int i = 0; i < bsp->visrowsize; i++)
				wm->sky_visibility[i] |= mask[i];
		}
	}
}

static void
compute_cluster_aabbs(bsp_mesh_t* wm)
{
	wm->cluster_aabbs = Z_Malloc(wm->num_clusters * sizeof(aabb_t));
	for (int c = 0; c < wm->num_clusters; c++)
	{
		VectorSet(wm->cluster_aabbs[c].mins, FLT_MAX, FLT_MAX, FLT_MAX);
		VectorSet(wm->cluster_aabbs[c].maxs, -FLT_MAX, -FLT_MAX, -FLT_MAX);
	}

	for (int tri = 0; tri < wm->world_idx_count / 3; tri++)
	{
		int c = wm->clusters[tri];

		if(c < 0 || c >= wm->num_clusters)
			continue;

		aabb_t* aabb = wm->cluster_aabbs + c;

		for (int i = 0; i < 3; i++)
		{
			float const* position = wm->positions + tri * 9 + i * 3;

			aabb->mins[0] = min(aabb->mins[0], position[0]);
			aabb->mins[1] = min(aabb->mins[1], position[1]);
			aabb->mins[2] = min(aabb->mins[2], position[2]);

			aabb->maxs[0] = max(aabb->maxs[0], position[0]);
			aabb->maxs[1] = max(aabb->maxs[1], position[1]);
			aabb->maxs[2] = max(aabb->maxs[2], position[2]);
		}
	}
}

static void
get_aabb_corner(aabb_t* aabb, int corner_idx, vec3_t corner)
{
	corner[0] = (corner_idx & 1) ? aabb->maxs[0] : aabb->mins[0];
	corner[1] = (corner_idx & 2) ? aabb->maxs[1] : aabb->mins[1];
	corner[2] = (corner_idx & 4) ? aabb->maxs[2] : aabb->mins[2];
}

static qboolean
light_affects_cluster(light_poly_t* light, aabb_t* aabb)
{
	// Empty cluster, nothing is visible
	if (aabb->mins[0] > aabb->maxs[0])
		return qfalse;

	const float* v0 = light->positions + 0;
	const float* v1 = light->positions + 3;
	const float* v2 = light->positions + 6;
	
	// Get the light plane equation
	vec3_t e1, e2, normal;
	VectorSubtract(v1, v0, e1);
	VectorSubtract(v2, v0, e2);
	CrossProduct(e1, e2, normal);
	VectorNormalize(normal);
	
	float plane_distance = -DotProduct(normal, v0);

	qboolean all_culled = qtrue;

	// If all 8 corners of the cluster's AABB are behind the light, it's definitely invisible
	for (int corner_idx = 0; corner_idx < 8; corner_idx++)
	{
		vec3_t corner;
		get_aabb_corner(aabb, corner_idx, corner);

		float side = DotProduct(normal, corner) + plane_distance;
		if (side > 0)
			all_culled = qfalse;
	}

	if (all_culled)
	{
		return qfalse;
	}

	return qtrue;
}

static void
collect_cluster_lights(bsp_mesh_t *wm, bsp_t *bsp)
{
#define MAX_LIGHTS_PER_CLUSTER 1024
	int* cluster_lights = Z_Malloc(MAX_LIGHTS_PER_CLUSTER * wm->num_clusters * sizeof(int));
	int* cluster_light_counts = Z_Mallocz(wm->num_clusters * sizeof(int));

	// Construct an array of visible lights for each cluster.
	// The array is in `cluster_lights`, with MAX_LIGHTS_PER_CLUSTER stride.

	for (int nlight = 0; nlight < wm->num_light_polys; nlight++)
	{
		light_poly_t* light = wm->light_polys + nlight;

		if(light->cluster < 0)
			continue;

		const byte* pvs = (const byte*)BSP_GetPvs(bsp, light->cluster);

		FOREACH_BIT_BEGIN(pvs, bsp->visrowsize, other_cluster)
			aabb_t* cluster_aabb = wm->cluster_aabbs + other_cluster;
			if (light_affects_cluster(light, cluster_aabb))
			{
				int* num_cluster_lights = cluster_light_counts + other_cluster;
				if (*num_cluster_lights < MAX_LIGHTS_PER_CLUSTER)
				{
					cluster_lights[other_cluster * MAX_LIGHTS_PER_CLUSTER + *num_cluster_lights] = nlight;
					(*num_cluster_lights)++;
				}
			}
		FOREACH_BIT_END
	}

	// Count the total number of cluster <-> light relations to allocate memory

	wm->num_cluster_lights = 0;
	for (int cluster = 0; cluster < wm->num_clusters; cluster++)
	{
		wm->num_cluster_lights += cluster_light_counts[cluster];
	}

	wm->cluster_lights = Z_Mallocz(wm->num_cluster_lights * sizeof(int));
	wm->cluster_light_offsets = Z_Mallocz((wm->num_clusters + 1) * sizeof(int));

	// Com_Printf("Total interactions: %d, culled bbox: %d, culled proj: %d\n", wm->num_cluster_lights, lights_culled_bbox, lights_culled_proj);

	// Compact the previously constructed array into wm->cluster_lights

	int list_offset = 0;
	for (int cluster = 0; cluster < wm->num_clusters; cluster++)
	{
		assert(list_offset >= 0);
		wm->cluster_light_offsets[cluster] = list_offset;
		int count = cluster_light_counts[cluster];
		memcpy(
			wm->cluster_lights + list_offset, 
			cluster_lights + MAX_LIGHTS_PER_CLUSTER * cluster, 
			count * sizeof(int));
		list_offset += count;
	}
	wm->cluster_light_offsets[wm->num_clusters] = list_offset;

	Z_Free(cluster_lights);
	Z_Free(cluster_light_counts);
#undef MAX_LIGHTS_PER_CLUSTER
}

static qboolean
bsp_mesh_load_custom_sky(int *idx_ctr, bsp_mesh_t *wm, bsp_t *bsp, const char* map_name)
{
	char filename[MAX_QPATH];
	Q_snprintf(filename, sizeof(filename), "maps/sky/%s.obj", map_name);

	void* file_buffer = NULL;
	ssize_t file_size = FS_LoadFile(filename, &file_buffer);
	if (!file_buffer)
		return qfalse;

	tinyobj_attrib_t attrib;
	tinyobj_shape_t* shapes = NULL;
	size_t num_shapes;
	tinyobj_material_t* materials = NULL;
	size_t num_materials;

	unsigned int flags = TINYOBJ_FLAG_TRIANGULATE;
	int ret = tinyobj_parse_obj(&attrib, &shapes, &num_shapes, &materials,
		&num_materials, (const char*)file_buffer, file_size, flags);

	FS_FreeFile(file_buffer);

	if (ret != TINYOBJ_SUCCESS) {
		Com_WPrintf("Couldn't parse sky polygon definition file %s.\n", filename);
		return qfalse;
	}

	int face_offset = 0;
	for (int nprim = 0; nprim < attrib.num_face_num_verts; nprim++)
	{
		int face_num_verts = attrib.face_num_verts[nprim];
		int i0 = attrib.faces[face_offset + 0].v_idx;
		int i1 = attrib.faces[face_offset + 1].v_idx;
		int i2 = attrib.faces[face_offset + 2].v_idx;

		vec3_t v0, v1, v2;
		VectorCopy(attrib.vertices + i0 * 3, v0);
		VectorCopy(attrib.vertices + i1 * 3, v1);
		VectorCopy(attrib.vertices + i2 * 3, v2);

		int wm_index = *idx_ctr;
		int wm_prim = wm_index / 3;

		VectorCopy(v0, wm->positions + wm_index * 3 + 0);
		VectorCopy(v1, wm->positions + wm_index * 3 + 3);
		VectorCopy(v2, wm->positions + wm_index * 3 + 6);

		wm->tex_coords[wm_index * 2 + 0] = 0.f;
		wm->tex_coords[wm_index * 2 + 1] = 0.f;
		wm->tex_coords[wm_index * 2 + 2] = 0.f;
		wm->tex_coords[wm_index * 2 + 3] = 0.f;
		wm->tex_coords[wm_index * 2 + 4] = 0.f;
		wm->tex_coords[wm_index * 2 + 5] = 0.f;

		vec3_t center;
		get_triangle_off_center(wm->positions + wm_index * 3, center, NULL, 1.f);

		int cluster = BSP_PointLeaf(bsp->nodes, center)->cluster;
		wm->clusters[wm_prim] = cluster;
		wm->materials[wm_prim] = MATERIAL_FLAG_LIGHT | MATERIAL_KIND_SKY;

		light_poly_t* light = append_light_poly(&wm->num_light_polys, &wm->allocated_light_polys, &wm->light_polys);

		VectorCopy(v0, light->positions + 0);
		VectorCopy(v1, light->positions + 3);
		VectorCopy(v2, light->positions + 6);
		VectorSet(light->color, -1.f, -1.f, -1.f); // special value for the sky
		VectorCopy(center, light->off_center);
		light->material = 0;
		light->style = 0;
		light->cluster = cluster;

		*idx_ctr += 3;

		face_offset += face_num_verts;
	}

	tinyobj_attrib_free(&attrib);
	tinyobj_shapes_free(shapes, num_shapes);
	tinyobj_materials_free(materials, num_materials);

	return qtrue;
}

void
bsp_mesh_create_from_bsp(bsp_mesh_t *wm, bsp_t *bsp, const char* map_name)
{
	const char* full_game_map_name = map_name;
	if (strcmp(map_name, "demo1") == 0)
		full_game_map_name = "base1";
	else if (strcmp(map_name, "demo2") == 0)
		full_game_map_name = "base2";
	else if (strcmp(map_name, "demo3") == 0)
		full_game_map_name = "base3";

	load_sky_and_lava_clusters(wm, full_game_map_name);
	load_cameras(wm, full_game_map_name);

	wm->models = Z_Malloc(bsp->nummodels * sizeof(bsp_model_t));
	memset(wm->models, 0, bsp->nummodels * sizeof(bsp_model_t));

    wm->num_models = bsp->nummodels;
	wm->num_clusters = bsp->vis->numclusters;

	if (wm->num_clusters + 1 >= MAX_LIGHT_LISTS)
	{
		Com_Error(ERR_FATAL, "The BSP model has too many clusters (%d)", wm->num_clusters);
	}

    wm->num_vertices = 0;
    wm->num_indices = 0;
    wm->positions = Z_Malloc(MAX_VERT_BSP * 3 * sizeof(*wm->positions));
    wm->tex_coords = Z_Malloc(MAX_VERT_BSP * 2 * sizeof(*wm->tex_coords));
    wm->materials = Z_Malloc(MAX_VERT_BSP / 3 * sizeof(*wm->materials));
    wm->clusters = Z_Malloc(MAX_VERT_BSP / 3 * sizeof(*wm->clusters));
	wm->emissive_factors = Z_Malloc(MAX_VERT_BSP / 3 * sizeof(*wm->emissive_factors));

	// clear these here because `bsp_mesh_load_custom_sky` creates lights before `collect_light_polys`
	wm->num_light_polys = 0;
	wm->allocated_light_polys = 0;
	wm->light_polys = NULL;

    int idx_ctr = 0;

#if DUMP_WORLD_MESH_TO_OBJ
	{
		char filename[MAX_QPATH];
		Q_snprintf(filename, sizeof(filename), "C:\\temp\\%s.obj", map_name);
		obj_dump_file = fopen(filename, "w");
		obj_vertex_num = 1;
	}
#endif

	collect_surfaces(&idx_ctr, wm, bsp, -1, filter_static_opaque);
    wm->world_idx_count = idx_ctr;

    wm->world_transparent_offset = idx_ctr;
    collect_surfaces(&idx_ctr, wm, bsp, -1, filter_static_transparent);
    wm->world_transparent_count = idx_ctr - wm->world_transparent_offset;

	wm->world_masked_offset = idx_ctr;
	collect_surfaces(&idx_ctr, wm, bsp, -1, filter_static_masked);
	wm->world_masked_count = idx_ctr - wm->world_masked_offset;

	wm->world_sky_offset = idx_ctr;
	collect_surfaces(&idx_ctr, wm, bsp, -1, filter_static_sky);
	wm->world_sky_count = idx_ctr - wm->world_sky_offset;

	wm->world_custom_sky_offset = idx_ctr;
	bsp_mesh_load_custom_sky(&idx_ctr, wm, bsp, full_game_map_name);
	wm->world_custom_sky_count = idx_ctr - wm->world_custom_sky_offset;

    for (int k = 0; k < bsp->nummodels; k++) {
		bsp_model_t* model = wm->models + k;
        model->idx_offset = idx_ctr;
        collect_surfaces(&idx_ctr, wm, bsp, k, filter_all);
        model->idx_count = idx_ctr - model->idx_offset;
    }

#if DUMP_WORLD_MESH_TO_OBJ
	fclose(obj_dump_file);
	obj_dump_file = NULL;
#endif

	if (!bsp->pvs_patched)
	{
		build_pvs2(bsp);

		if (!BSP_SavePatchedPVS(bsp))
		{
			Com_EPrintf("Couldn't save patched PVS for %s.\n", bsp->name);
		}
	}

    wm->num_indices = idx_ctr;
    wm->num_vertices = idx_ctr;

    wm->indices = Z_Malloc(idx_ctr * sizeof(int));
    for (int i = 0; i < wm->num_vertices; i++)
        wm->indices[i] = i;

	compute_world_tangents(wm);
	
    if (wm->num_vertices >= MAX_VERT_BSP) {
		Com_Error(ERR_FATAL, "The BSP model has too many vertices (%d)", wm->num_vertices);
	}

	for(int i = 0; i < wm->num_models; i++) 
	{
		bsp_model_t* model = wm->models + i;

		compute_aabb(wm->positions + model->idx_offset * 3, model->idx_count, model->aabb_min, model->aabb_max);

		VectorAdd(model->aabb_min, model->aabb_max, model->center);
		VectorScale(model->center, 0.5f, model->center);
	}

	compute_aabb(wm->positions, wm->world_idx_count, wm->world_aabb.mins, wm->world_aabb.maxs);

	vec3_t margin = { 1.f, 1.f, 1.f };
	VectorSubtract(wm->world_aabb.mins, margin, wm->world_aabb.mins);
	VectorAdd(wm->world_aabb.maxs, margin, wm->world_aabb.maxs);

	compute_cluster_aabbs(wm);

	collect_light_polys(wm, bsp, -1, &wm->num_light_polys, &wm->allocated_light_polys, &wm->light_polys);
	collect_sky_and_lava_light_polys(wm, bsp);

	for (int k = 0; k < bsp->nummodels; k++)
	{
		bsp_model_t* model = wm->models + k;

		model->num_light_polys = 0;
		model->allocated_light_polys = 0;
		model->light_polys = NULL;
		
		collect_light_polys(wm, bsp, k, &model->num_light_polys, &model->allocated_light_polys, &model->light_polys);

		model->transparent = is_model_transparent(wm, model);
		model->masked = is_model_masked(wm, model);
	}

	collect_cluster_lights(wm, bsp);

	compute_sky_visibility(wm, bsp);
}

void
bsp_mesh_destroy(bsp_mesh_t *wm)
{
	Z_Free(wm->models);

	Z_Free(wm->positions);
	Z_Free(wm->tex_coords);
    Z_Free(wm->tangents);
	Z_Free(wm->indices);
	Z_Free(wm->clusters);
	Z_Free(wm->materials);
	Z_Free(wm->texel_density);
	Z_Free(wm->emissive_factors);

	Z_Free(wm->light_polys);
	Z_Free(wm->cluster_lights);
	Z_Free(wm->cluster_light_offsets);
	Z_Free(wm->cluster_aabbs);

	memset(wm, 0, sizeof(*wm));
}

void
bsp_mesh_register_textures(bsp_t *bsp)
{
	MAT_ChangeMap(bsp->name);
	
	for (int i = 0; i < bsp->numtexinfo; i++) {
		mtexinfo_t *info = bsp->texinfo + i;
		imageflags_t flags;
		if (info->c.flags & SURF_WARP)
			flags = IF_TURBULENT;
		else
			flags = IF_NONE;

		char buffer[MAX_QPATH];
		Q_concat(buffer, sizeof(buffer), "textures/", info->name, ".wal", NULL);
		FS_NormalizePath(buffer, buffer);

		pbr_material_t * mat = MAT_Find(buffer, IT_WALL, flags);
		if (!mat)
			Com_EPrintf("error finding material '%s'\n", buffer);
		
		if(cvar_pt_enable_surface_lights->integer)
		{
			/* Synthesize an emissive material if the BSP surface has the LIGHT flag but the
			   material has no emissive image.
			   - Skip SKY and NODRAW surfaces, they'll be handled differently.
			   - Make WARP surfaces optional, as giving water, slime... an emissive texture clashes visually. */
			qboolean synth_surface_material = ((info->c.flags & (SURF_LIGHT | SURF_SKY | SURF_NODRAW)) == SURF_LIGHT)
				&& (info->radiance != 0);
			
			qboolean is_warp_surface = (info->c.flags & SURF_WARP) != 0;
			
			qboolean material_custom = !mat->source_matfile[0];
			
			synth_surface_material &= (cvar_pt_enable_surface_lights->integer >= 2) || material_custom;
			if (cvar_pt_enable_surface_lights_warp->integer == 0)
				synth_surface_material &= !is_warp_surface;
			
			if (synth_surface_material)
				MAT_SynthesizeEmissive(mat);
		}
		
		info->material = mat;
	}

	// link the animation sequences
	for (int i = 0; i < bsp->numtexinfo; i++) 
	{
		mtexinfo_t *texinfo = bsp->texinfo + i;
		pbr_material_t* material = texinfo->material;

		if (texinfo->numframes > 1)
		{
			assert(texinfo->next);
			assert(texinfo->next->material);

			material->num_frames = texinfo->numframes;
			material->next_frame = texinfo->next->material->flags & MATERIAL_INDEX_MASK;
		}
	}
}

static void animate_light_polys(int num_light_polys, light_poly_t *light_polys)
{
	for (int i = 0; i < num_light_polys; i++)
	{
		pbr_material_t *material = light_polys[i].material;
		if(!material || (material->num_frames <= 1))
			continue;

		pbr_material_t *new_material = r_materials + material->next_frame;
		light_polys[i].material = new_material;
		float emissive_factor = new_material->emissive_factor * light_polys[i].emissive_factor;
		if(new_material->image_emissive)
			VectorScale(new_material->image_emissive->light_color, emissive_factor, light_polys[i].color);
		else
			VectorSet(light_polys[i].color, 0, 0, 0);
	}
}

void bsp_mesh_animate_light_polys(bsp_mesh_t *wm)
{
	animate_light_polys(wm->num_light_polys, wm->light_polys);
	for (int k = 0; k < wm->num_models; k++)
	{
		bsp_model_t* model = wm->models + k;
		animate_light_polys(model->num_light_polys, model->light_polys);
	}
}

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
