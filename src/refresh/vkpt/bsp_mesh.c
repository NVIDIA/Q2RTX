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
#include "cameras.h"
#include "conversion.h"

#include <assert.h>
#include <float.h>

#define TINYOBJ_LOADER_C_IMPLEMENTATION
#include <tinyobj_loader_c.h>

extern cvar_t *cvar_pt_enable_nodraw;
extern cvar_t *cvar_pt_enable_surface_lights;
extern cvar_t *cvar_pt_enable_surface_lights_warp;
extern cvar_t* cvar_pt_bsp_radiance_scale;
extern cvar_t *cvar_pt_bsp_sky_lights;

static void
remove_collinear_edges(float* positions, float* tex_coords, mbasis_t* bases, int* num_vertices)
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

		bool remove = false;
		if (l1 == 0)
		{
			remove = true;
		}
		else if (l2 > 0)
		{
			VectorScale(e1, 1.f / l1, e1);
			VectorScale(e2, 1.f / l2, e2);

			float dot = DotProduct(e1, e2);
			if (dot > 0.999f)
				remove = true;
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

				if (bases)
				{
					mbasis_t* b1 = bases + (i % num_vertices_local);
					mbasis_t* b2 = bases + ((i + 1) % num_vertices_local);
					memcpy(b1, b2, (num_vertices_local - i - 1) * sizeof(mbasis_t));
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

// direct port of the encode_normal function from utils.glsl
uint32_t
encode_normal(const vec3_t normal)
{
	float invL1Norm = 1.0f / (fabsf(normal[0]) + fabsf(normal[1]) + fabsf(normal[2]));

	vec2_t p = { normal[0] * invL1Norm, normal[1] * invL1Norm };
	vec2_t pp = { p[0], p[1] };

	if (normal[2] < 0.f)
	{
		pp[0] = (1.f - fabsf(p[1])) * ((p[0] >= 0.f) ? 1.f : -1.f);
		pp[1] = (1.f - fabsf(p[0])) * ((p[1] >= 0.f) ? 1.f : -1.f);
	}

	pp[0] = pp[0] * 0.5f + 0.5f;
	pp[1] = pp[1] * 0.5f + 0.5f;

	pp[0] = Q_clipf(pp[0], 0.f, 1.f);
	pp[1] = Q_clipf(pp[1], 0.f, 1.f);

	uint32_t ux = (uint32_t)(pp[0] * 0xffffu);
	uint32_t uy = (uint32_t)(pp[1] * 0xffffu);

	return ux | (uy << 16);
}

// Compute emissive factor for a surface
static float
compute_emissive(mtexinfo_t *texinfo)
{
	if(!texinfo->material)
		return 1.f;

	const float bsp_emissive = (float)texinfo->radiance * cvar_pt_bsp_radiance_scale->value;

	return ((texinfo->c.flags & SURF_LIGHT) && texinfo->material->bsp_radiance)
		? bsp_emissive
		: texinfo->material->default_radiance;
}

#define DUMP_WORLD_MESH_TO_OBJ 0
#if DUMP_WORLD_MESH_TO_OBJ
static FILE* obj_dump_file = NULL;
static int obj_vertex_num = 0;
#endif

static uint32_t
create_poly(
	const bsp_t* bsp,
	const mface_t* surf,
	uint material_id,
	uint32_t primitive_index,
	uint32_t max_prim,
	VboPrimitive* primitives_out)
{
	static const int max_vertices = 32;
	float positions [3 * /*max_vertices*/ 32];
	float tex_coords[2 * /*max_vertices*/ 32];
	mbasis_t bases  [    /*max_vertices*/ 32];
	mtexinfo_t *texinfo = surf->texinfo;
	assert(surf->numsurfedges < max_vertices);
	(void)max_vertices;
	
	float sc[2] = { 1.f, 1.f };
	if (texinfo->material && texinfo->material->original_width && texinfo->material->original_height) {
		sc[0] = 1.0f / (float)texinfo->material->original_width;
		sc[1] = 1.0f / (float)texinfo->material->original_height;
	}
	
	for (int i = 0; i < surf->numsurfedges; i++) {
		msurfedge_t *src_surfedge = surf->firstsurfedge + i;
		medge_t     *src_edge     = src_surfedge->edge;
		mvertex_t   *src_vert     = src_edge->v[src_surfedge->vert];

		float *p = positions + i * 3;
		float *t = tex_coords + i * 2;

		VectorCopy(src_vert->point, p);
		
		t[0] = (DotProduct(p, texinfo->axis[0]) + texinfo->offset[0]) * sc[0];
		t[1] = (DotProduct(p, texinfo->axis[1]) + texinfo->offset[1]) * sc[1];

		if (bsp->basisvectors)
		{
			bases[i] = *(bsp->bases + surf->firstbasis + i);
		}
		
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
	
	if (bsp->basisvectors)
	{
		// Check the handedness using the basis of the first vertex
		
		const vec3_t* normal = bsp->basisvectors + bases->normal;
		const vec3_t* tangent = bsp->basisvectors + bases->tangent;
		const vec3_t* bitangent = bsp->basisvectors + bases->bitangent;
		
		vec3_t cross;
		CrossProduct(*normal, *tangent, cross);
		float dot = DotProduct(cross, *bitangent);

		if (dot < 0.0f)
		{
			material_id |= MATERIAL_FLAG_HANDEDNESS;
		}
	}

	int num_vertices = surf->numsurfedges;

	bool is_sky = MAT_IsKind(material_id, MATERIAL_KIND_SKY);
	if (is_sky)
	{
		// process skybox geometry in the same way as we process it for analytic light generation
		// to avoid mismatches between lights and geometry
		remove_collinear_edges(positions, tex_coords, bases, &num_vertices);
	}

	if (num_vertices < 3)
		return 0;
	
	const uint32_t num_triangles = (uint32_t)num_vertices - 2;

	if (!primitives_out)
		return num_triangles;

	const float emissive_factor = compute_emissive(texinfo);

	float alpha = 1.f;
	if (MAT_IsKind(material_id, MATERIAL_KIND_TRANSPARENT))
		alpha = (texinfo->c.flags & SURF_TRANS33) ? 0.33f : (texinfo->c.flags & SURF_TRANS66) ? 0.66f : 1.0f;

	const uint32_t emissive_and_alpha = floatToHalf(emissive_factor) | (floatToHalf(alpha) << 16);

	bool write_normals = bsp->basisvectors != NULL;
	
	for (uint32_t i = 0; i < num_triangles; i++)
	{
		// The prititive buffer is allocated based on the expected number of prims generated by the bsp,
		// so just verify that here, mostly for debugging.
		if (primitive_index + i >= max_prim)
		{
			assert(!"Primitive buffer overflow - there's a bug somewhere.");
			return i;
		}

		memset(primitives_out, 0, sizeof(VboPrimitive));
		
		int i1 = (i + 2) % num_vertices;
		int i2 = (i + 1) % num_vertices;

		float* pos = positions;
		float* tc = tex_coords;
		VectorCopy(pos, primitives_out->pos0);
		primitives_out->uv0[0] = tc[0];
		primitives_out->uv0[1] = tc[1];
		
		if (write_normals)
		{
			const mbasis_t* basis = bases;
			const vec3_t* normal = bsp->basisvectors + basis->normal;
			const vec3_t* tangent = bsp->basisvectors + basis->tangent;
			primitives_out->normals[0] = encode_normal(*normal);
			primitives_out->tangents[0] = encode_normal(*tangent);
		}

		pos = positions + i1 * 3;
		tc = tex_coords + i1 * 2;
		VectorCopy(pos, primitives_out->pos1);
		primitives_out->uv1[0] = tc[0];
		primitives_out->uv1[1] = tc[1];
		
		if (write_normals)
		{
			const mbasis_t* basis = bases + i1;
			const vec3_t* normal = bsp->basisvectors + basis->normal;
			const vec3_t* tangent = bsp->basisvectors + basis->tangent;
			primitives_out->normals[1] = encode_normal(*normal);
			primitives_out->tangents[1] = encode_normal(*tangent);
		}
		
		pos = positions + i2 * 3;
		tc = tex_coords + i2 * 2;
		VectorCopy(pos, primitives_out->pos2);
		primitives_out->uv2[0] = tc[0];
		primitives_out->uv2[1] = tc[1];
		
		if (write_normals)
		{
			const mbasis_t* basis = bases + i2;
			const vec3_t* normal = bsp->basisvectors + basis->normal;
			const vec3_t* tangent = bsp->basisvectors + basis->tangent;
			primitives_out->normals[2] = encode_normal(*normal);
			primitives_out->tangents[2] = encode_normal(*tangent);
		}

		primitives_out->material_id = material_id;
		primitives_out->emissive_and_alpha = emissive_and_alpha;
		primitives_out->instance = 0;
		
		++primitives_out;
	}
	
	return num_triangles;
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

// Classification of what kind of sky a surface is
enum sky_class_e
{
	// Not sky
	SKY_CLASS_NO,
	// Sky material (selected by filter_static_sky)
	SKY_CLASS_MATERIAL,
	// User-defined sky enabled by pt_bsp_sky_lights > 1 (selected by filter_nodraw_sky_lights)
	SKY_CLASS_NODRAW_SKYLIGHT
};

static inline enum sky_class_e classify_sky(uint32_t original_material_id, uint32_t material_id, int surf_flags)
{
	if (MAT_IsKind(original_material_id, MATERIAL_KIND_SKY))
		return SKY_CLASS_MATERIAL;

	if (cvar_pt_bsp_sky_lights->integer > 1) {
		int nodraw_skylight_expected = SURF_SKY | SURF_LIGHT | SURF_NODRAW;
		if ((surf_flags & nodraw_skylight_expected) == nodraw_skylight_expected)
			return SKY_CLASS_NODRAW_SKYLIGHT;
	}

	// All other surfaces marked as SKY in the BSP using unexpected materials and that are not (LIGHT|NODRAW)
	if (MAT_IsKind(material_id, MATERIAL_KIND_SKY))
		return SKY_CLASS_MATERIAL;

	return SKY_CLASS_NO;
}

static int filter_static_masked(uint32_t original_material_id, uint32_t material_id, int surf_flags)
{
	if ((surf_flags & SURF_NODRAW) && cvar_pt_enable_nodraw->integer)
		return 0;
	
	const pbr_material_t* mat = MAT_ForIndex(material_id & MATERIAL_INDEX_MASK);

	if (mat && mat->image_mask)
		return 1;

	return 0;
}

static int filter_static_opaque(uint32_t original_material_id, uint32_t material_id, int surf_flags)
{
	if ((surf_flags & SURF_NODRAW) && cvar_pt_enable_nodraw->integer)
		return 0;
	
	if (filter_static_masked(original_material_id, material_id, surf_flags))
		return 0;
	
	uint32_t kind = material_id & MATERIAL_KIND_MASK;
	if (kind == MATERIAL_KIND_SKY ||
		kind == MATERIAL_KIND_WATER ||
		kind == MATERIAL_KIND_SLIME ||
		kind == MATERIAL_KIND_GLASS ||
		kind == MATERIAL_KIND_TRANSPARENT)
		return 0;

	return 1;
}

static int filter_static_transparent(uint32_t original_material_id, uint32_t material_id, int surf_flags)
{
	if ((surf_flags & SURF_NODRAW) && cvar_pt_enable_nodraw->integer)
		return 0;
	
	uint32_t kind = material_id & MATERIAL_KIND_MASK;
	if (kind == MATERIAL_KIND_WATER ||
		kind == MATERIAL_KIND_SLIME ||
		kind == MATERIAL_KIND_GLASS ||
		kind == MATERIAL_KIND_TRANSPARENT)
		return 1;
	
	return 0;
}

static int filter_static_sky(uint32_t original_material_id, uint32_t material_id, int surf_flags)
{
	enum sky_class_e sky_class = classify_sky(original_material_id, material_id, surf_flags);

	if (sky_class == SKY_CLASS_MATERIAL && cvar_pt_enable_nodraw->integer < 2)
		return 1;

	return 0;
}

static int filter_all(uint32_t original_material_id, uint32_t material_id, int surf_flags)
{
	if ((surf_flags & SURF_NODRAW) && cvar_pt_enable_nodraw->integer)
		return 0;
	
	if (MAT_IsKind(material_id, MATERIAL_KIND_SKY))
		return 0;

	return 1;
}

static int filter_nodraw_sky_lights(uint32_t original_material_id, uint32_t material_id, int surf_flags)
{
	enum sky_class_e sky_class = classify_sky(original_material_id, material_id, surf_flags);
	return sky_class == SKY_CLASS_NODRAW_SKYLIGHT;
}

// Computes a point at a small distance above the center of the triangle.
// Returns false if the triangle is degenerate, true otherwise.
bool
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

static bool
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

static bool
is_sky_or_lava_cluster(bsp_mesh_t* wm, mface_t* surf, int cluster, int material_id)
{
	if (cluster < 0)
		return false;

	if (MAT_IsKind(material_id, MATERIAL_KIND_LAVA) && wm->all_lava_emissive)
	{
		vec4_t plane;
		if (get_surf_plane_equation(surf, plane))
		{
			if (plane[2] < 0.f)
				return true;
		}
	}
	else
	{
		for (int i = 0; i < wm->num_sky_clusters; i++)
		{
			if (wm->sky_clusters[i] == cluster)
			{
				return true;
			}
		}
	}

	return false;
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

// Provides an upper estimate (not counting the collinear edge removal, invisible materials etc.)
// for the total number of triangles needed to represent the bsp and one instance of every model.
static int count_triangles(const bsp_t* bsp)
{
	int num_tris = 0;

	for (int i = 0; i < bsp->numfaces; i++)
	{
		mface_t* surf = bsp->faces + i;
		int num_vertices = surf->numsurfedges;

		if (num_vertices >= 3)
			num_tris += (num_vertices - 2);
	}

	return num_tris;
}

static void
collect_surfaces(uint32_t *prim_ctr, bsp_mesh_t *wm, bsp_t *bsp, int model_idx, int (*filter)(uint32_t, uint32_t, int))
{
	mface_t *surfaces = model_idx < 0 ? bsp->faces : bsp->models[model_idx].firstface;
	int num_faces = model_idx < 0 ? bsp->numfaces : bsp->models[model_idx].numfaces;
	bool any_pvs_patches = false;

	for (int i = 0; i < num_faces; i++) {
		mface_t *surf = surfaces + i;

		if (model_idx < 0 && belongs_to_model(bsp, surf)) {
			continue;
		}

		uint32_t material_id = surf->texinfo->material ? surf->texinfo->material->flags : 0;
		uint32_t original_material_id = material_id;
		int surf_flags = surf->drawflags | surf->texinfo->c.flags;

		// ugly hacks for situations when the same texture is used with different effects

		if ((MAT_IsKind(material_id, MATERIAL_KIND_WATER) || MAT_IsKind(material_id, MATERIAL_KIND_SLIME)) && !(surf_flags & SURF_WARP))
			material_id = MAT_SetKind(material_id, MATERIAL_KIND_REGULAR);

		if (MAT_IsKind(material_id, MATERIAL_KIND_GLASS) && !(surf_flags & SURF_TRANS_MASK))
			material_id = MAT_SetKind(material_id, MATERIAL_KIND_REGULAR);
		
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

		if (!filter(original_material_id, material_id, surf_flags))
			continue;

		if ((material_id & MATERIAL_FLAG_LIGHT) && surf->texinfo->material->light_styles)
		{
			int light_style = get_surf_light_style(surf);
			material_id |= (light_style << MATERIAL_LIGHT_STYLE_SHIFT) & MATERIAL_LIGHT_STYLE_MASK;
		}

		if (MAT_IsKind(material_id, MATERIAL_KIND_CAMERA) && wm->num_cameras > 0)
		{
			// Assign a random camera for this face
			int camera_id = Q_rand() % (wm->num_cameras * 4);
			material_id = (material_id & ~MATERIAL_LIGHT_STYLE_MASK) | ((camera_id << MATERIAL_LIGHT_STYLE_SHIFT) & MATERIAL_LIGHT_STYLE_MASK);
		}
		
		VboPrimitive* surface_prims = wm->primitives + *prim_ctr;
		
		uint32_t prims_in_surface = create_poly(bsp, surf, material_id, *prim_ctr, wm->num_primitives_allocated, surface_prims);

		for (uint32_t k = 0; k < prims_in_surface; ++k) 
		{
			if (model_idx < 0)
			{
				// Collect the positions into one array for compatibility with get_triangle_off_center(...)
				float positions[9];
				VectorCopy(surface_prims[k].pos0, positions + 0);
				VectorCopy(surface_prims[k].pos1, positions + 3);
				VectorCopy(surface_prims[k].pos2, positions + 6);
				
				// Compute the BSP node for this specific triangle based on its center.
				// The face lists in the BSP are slightly incorrect, or the original code 
				// in q2vkpt that was extracting them was incorrect.

				vec3_t center, anti_center;
				get_triangle_off_center(positions, center, anti_center, 0.01f);

				int cluster = BSP_PointLeaf(bsp->nodes, center)->cluster;

				// If the small offset for the off-center point was too small, and that point
				// is not inside any cluster, try a larger offset.
				if (cluster < 0) {
					get_triangle_off_center(positions, center, anti_center, 1.f);
					cluster = BSP_PointLeaf(bsp->nodes, center)->cluster;
				}

				surface_prims[k].cluster = cluster;

				if (cluster >= 0 && (MAT_IsKind(material_id, MATERIAL_KIND_SKY) || MAT_IsKind(material_id, MATERIAL_KIND_LAVA)))
				{
					bool is_bsp_sky_light = (surf_flags & (SURF_LIGHT | SURF_SKY)) == (SURF_LIGHT | SURF_SKY);
					if (is_sky_or_lava_cluster(wm, surf, cluster, material_id) || (cvar_pt_bsp_sky_lights->integer && is_bsp_sky_light))
					{
						surface_prims[k].material_id |= MATERIAL_FLAG_LIGHT;
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
								any_pvs_patches = true;
							}
						}
					}
				}
			}
			else
				surface_prims[k].cluster = -1;
		}

		*prim_ctr += prims_in_surface;
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

static inline bool
is_light_material(uint32_t material)
{
	return (material & MATERIAL_FLAG_LIGHT) != 0;
}

static void
collect_one_light_poly_entire_texture(bsp_t *bsp, mface_t *surf, mtexinfo_t *texinfo, int model_idx,
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
	remove_collinear_edges(positions, NULL, NULL, &num_vertices);

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

		light.emissive_factor = emissive_factor;
		
		if (model_idx >= 0)
			light.cluster = -1; // Cluster will be determined when the model is instanced
		else
			light.cluster = BSP_PointLeaf(bsp->nodes, light.off_center)->cluster;
		
		if (model_idx >= 0 || light.cluster >= 0)
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

static bool
collect_frames_emissive_info(pbr_material_t* material, bool* entire_texture_emissive, vec2_t min_light_texcoord, vec2_t max_light_texcoord, vec3_t light_color)
{
	*entire_texture_emissive = false;
	min_light_texcoord[0] = min_light_texcoord[1] = 1.0f;
	max_light_texcoord[0] = max_light_texcoord[1] = 0.0f;

	bool any_emissive_valid = false;
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
		any_emissive_valid = true;

		*entire_texture_emissive |= image->entire_texture_emissive;
		min_light_texcoord[0] = min(min_light_texcoord[0], image->min_light_texcoord[0]);
		min_light_texcoord[1] = min(min_light_texcoord[1], image->min_light_texcoord[1]);
		max_light_texcoord[0] = max(max_light_texcoord[0], image->max_light_texcoord[0]);
		max_light_texcoord[1] = max(max_light_texcoord[1], image->max_light_texcoord[1]);
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

		int flags = surf->drawflags;
		if (surf->texinfo) flags |= surf->texinfo->c.flags;

		// Don't create light polys from SKY surfaces, those are handled separately.
		// Sometimes, textures with a light fixture are used on sky polys (like in rlava1),
		// and that leads to subdivision of those sky polys into a large number of lights.
		if (flags & SURF_SKY)
			continue;

		// Check if any animation frame is a light material
		bool any_light_frame = false;
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

		// Collect emissive texture info from across frames
		bool entire_texture_emissive;
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

		float emissive_factor = compute_emissive(texinfo);
		if(emissive_factor == 0)
			continue;

		int light_style = (texinfo->material->light_styles) ? get_surf_light_style(surf) : 0;

		if (entire_texture_emissive)
		{
			collect_one_light_poly_entire_texture(bsp, surf, texinfo, model_idx, light_color, emissive_factor, light_style,
												  num_lights, allocated_lights, lights);
			continue;
		}

		vec4_t plane;
		if (!get_surf_plane_equation(surf, plane))
		{
			// It's possible that some polygons in the game are degenerate, ignore these.
			continue;
		}

		float tex_scale[2] = { 1.0f / texinfo->material->original_width, 1.0f / texinfo->material->original_height };

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

		if (!surf->texinfo)
			continue;

		int flags = surf->drawflags;
		if (surf->texinfo) flags |= surf->texinfo->c.flags;

		bool is_sky = !!(flags & SURF_SKY);
		bool is_light = !!(flags & SURF_LIGHT);
		bool is_nodraw = !!(flags & SURF_NODRAW);
		bool is_lava = surf->texinfo->material ? MAT_IsKind(surf->texinfo->material->flags, MATERIAL_KIND_LAVA) : false;
		
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
		remove_collinear_edges(positions, NULL, NULL, &num_vertices);

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
			
			if (is_sky_or_lava_cluster(wm, surf, light.cluster, surf->texinfo->material->flags) ||
				(cvar_pt_bsp_sky_lights->integer && is_sky && is_light && (cvar_pt_bsp_sky_lights->integer > 1 || !is_nodraw)))
			{
				light_poly_t* list_light = append_light_poly(&wm->num_light_polys, &wm->allocated_light_polys, &wm->light_polys);
				memcpy(list_light, &light, sizeof(light_poly_t));
			}
		}
	}
}

static bool
is_model_transparent(bsp_mesh_t *wm, bsp_model_t *model)
{
	if (model->geometry.num_geometries == 0)
		return false;

	for (uint prim_idx = 0; prim_idx < model->geometry.prim_counts[0]; prim_idx++)
	{
		uint prim = model->geometry.prim_offsets[0] + prim_idx;
		uint material = wm->primitives[prim].material_id;
		
		if (!(MAT_IsKind(material, MATERIAL_KIND_SLIME) || MAT_IsKind(material, MATERIAL_KIND_WATER) || MAT_IsKind(material, MATERIAL_KIND_GLASS) || MAT_IsKind(material, MATERIAL_KIND_TRANSPARENT)))
			return false;
	}

	return true;
}

static bool
is_model_masked(bsp_mesh_t *wm, bsp_model_t *model)
{
	if (model->geometry.num_geometries == 0)
		return false;

	for (uint prim_idx = 0; prim_idx < model->geometry.prim_counts[0]; prim_idx++)
	{
		uint prim = model->geometry.prim_offsets[0] + prim_idx;
		uint material = wm->primitives[prim].material_id;

		const pbr_material_t* mat = MAT_ForIndex((int)(material & MATERIAL_INDEX_MASK));
		
		if (mat && mat->image_mask)
			return true;
	}

	return false;
}

void
append_aabb(const VboPrimitive* primitives, uint32_t numprims, float* aabb_min, float* aabb_max)
{
	for (uint32_t prim_idx = 0; prim_idx < numprims; prim_idx++)
	{
		const VboPrimitive* prim = primitives + prim_idx;

		for (uint32_t vert_idx = 0; vert_idx < 3; vert_idx++)
		{
			const float* position;
			switch (vert_idx)
			{
			case 0:  position = prim->pos0; break;
			case 1:  position = prim->pos1; break;
			default: position = prim->pos2; break;
			}

			aabb_min[0] = min(aabb_min[0], position[0]);
			aabb_min[1] = min(aabb_min[1], position[1]);
			aabb_min[2] = min(aabb_min[2], position[2]);

			aabb_max[0] = max(aabb_max[0], position[0]);
			aabb_max[1] = max(aabb_max[1], position[1]);
			aabb_max[2] = max(aabb_max[2], position[2]);
		}
	}
}

void
compute_aabb(const VboPrimitive* primitives, uint32_t numprims, float* aabb_min, float* aabb_max)
{
	VectorSet(aabb_min, FLT_MAX, FLT_MAX, FLT_MAX);
	VectorSet(aabb_max, -FLT_MAX, -FLT_MAX, -FLT_MAX);

	append_aabb(primitives, numprims, aabb_min, aabb_max);
}

void
compute_world_tangents(bsp_t* bsp, bsp_mesh_t* wm)
{
	if (bsp->basisvectors)
		return;

	// Compute the tangent basis if it's not provided by the BSPX

	for (int idx_tri = 0; idx_tri < wm->num_primitives; ++idx_tri)
	{
		VboPrimitive* prim = wm->primitives + idx_tri;
		
		float const * pA = prim->pos0;
		float const * pB = prim->pos1;
		float const * pC = prim->pos2;

		float const * tA = prim->uv0;
		float const * tB = prim->uv1;
		float const * tC = prim->uv2;

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

		uint32_t encoded_normal = encode_normal(normal);
		prim->normals[0] = encoded_normal;
		prim->normals[1] = encoded_normal;
		prim->normals[2] = encoded_normal;

		vec3_t tangent;

		vec3_t t;
		VectorScale(normal, DotProduct(normal, sdir), t);
		VectorSubtract(sdir, t, t);
		VectorNormalize2(t, tangent); // Graham-Schmidt : t = normalize(t - n * (n.t))

		uint32_t encoded_tangent = encode_normal(tangent);
		prim->tangents[0] = encoded_tangent;
		prim->tangents[1] = encoded_tangent;
		prim->tangents[2] = encoded_tangent;

		vec3_t cross;
		CrossProduct(normal, t, cross);
		float dot = DotProduct(cross, tdir);

		if (dot < 0.0f)
		{
			prim->material_id |= MATERIAL_FLAG_HANDEDNESS;
		}
	}
}

static void
load_sky_and_lava_clusters(bsp_mesh_t* wm, const char* map_name)
{
	wm->num_sky_clusters = 0;
	wm->all_lava_emissive = false;

    // try a map-specific file first
    char filename[MAX_QPATH];
    Q_snprintf(filename, sizeof(filename), "maps/sky/%s.txt", map_name);

    char* filebuf = NULL;
    FS_LoadFile(filename, (void**)&filebuf);
    
    if (!filebuf)
    {
        Com_DPrintf("Couldn't read %s\n", filename);
        return;
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
			assert(wm->num_sky_clusters < MAX_SKY_CLUSTERS);

			if (!strcmp(word, "!all_lava"))
				wm->all_lava_emissive = true;
			else
			{
				int cluster = Q_atoi(word);
				wm->sky_clusters[wm->num_sky_clusters++] = cluster;
			}

			word = strtok(NULL, delimiters);
		}
	}

	Z_Free(filebuf);
}

static void
mark_clusters_with_sky(const bsp_mesh_t* wm, const model_geometry_t* geom, uint8_t* clusters_with_sky)
{
	for (uint32_t prim_idx = 0; prim_idx < geom->prim_counts[0]; prim_idx++)
	{
		uint32_t prim = geom->prim_offsets[0] + prim_idx;

		int cluster = wm->primitives[prim].cluster;
		if (cluster < 0) continue;
		if ((cluster >> 3) < VIS_MAX_BYTES)
			clusters_with_sky[cluster >> 3] |= (1 << (cluster & 7));
	}
}

static void
compute_sky_visibility(bsp_mesh_t *wm, bsp_t *bsp)
{
	memset(wm->sky_visibility, 0, VIS_MAX_BYTES);

	if (wm->geom_sky.num_geometries == 0 && wm->geom_custom_sky.num_geometries == 0)
		return; 

	uint32_t numclusters = bsp->vis->numclusters;

	uint8_t clusters_with_sky[VIS_MAX_BYTES] = { 0 };

	mark_clusters_with_sky(wm, &wm->geom_sky, clusters_with_sky);
	mark_clusters_with_sky(wm, &wm->geom_custom_sky, clusters_with_sky);

	for (uint32_t cluster = 0; cluster < numclusters; cluster++)
	{
		if (clusters_with_sky[cluster >> 3] & (1 << (cluster & 7)))
		{
			byte* mask = BSP_GetPvs(bsp, (int)cluster);

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

	for (uint prim_idx = 0; prim_idx < wm->geom_opaque.prim_counts[0]; prim_idx++)
	{
		int c = wm->primitives[prim_idx].cluster;

		if(c < 0 || c >= wm->num_clusters)
			continue;

		aabb_t* aabb = wm->cluster_aabbs + c;
		
		const VboPrimitive* prim = wm->primitives + prim_idx;

		for (int i = 0; i < 3; i++)
		{
			const float* position;
			switch(i)
			{
			case 0:  position = prim->pos0; break;
			case 1:  position = prim->pos1; break;
			default: position = prim->pos2; break;
			}

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
get_aabb_corner(const aabb_t* aabb, int corner_idx, vec3_t corner)
{
	corner[0] = (corner_idx & 1) ? aabb->maxs[0] : aabb->mins[0];
	corner[1] = (corner_idx & 2) ? aabb->maxs[1] : aabb->mins[1];
	corner[2] = (corner_idx & 4) ? aabb->maxs[2] : aabb->mins[2];
}

static bool
light_affects_cluster(light_poly_t* light, const aabb_t* aabb)
{
	// Empty cluster, nothing is visible
	if (aabb->mins[0] > aabb->maxs[0])
		return false;

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

	bool all_culled = true;

	// If all 8 corners of the cluster's AABB are behind the light, it's definitely invisible
	for (int corner_idx = 0; corner_idx < 8; corner_idx++)
	{
		vec3_t corner;
		get_aabb_corner(aabb, corner_idx, corner);

		float side = DotProduct(normal, corner) + plane_distance;
		if (side > 0)
			all_culled = false;
	}

	if (all_culled)
	{
		return false;
	}

	return true;
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

static tinyobj_attrib_t custom_sky_attrib;

static uint32_t
bsp_mesh_load_custom_sky(const char* map_name)
{
	char filename[MAX_QPATH];
	Q_snprintf(filename, sizeof(filename), "maps/sky/%s.obj", map_name);

	void* file_buffer = NULL;
	int file_size = FS_LoadFile(filename, &file_buffer);
	if (!file_buffer)
		return 0;

	tinyobj_shape_t* shapes = NULL;
	size_t num_shapes;
	tinyobj_material_t* materials = NULL;
	size_t num_materials;

	unsigned int flags = TINYOBJ_FLAG_TRIANGULATE;
	int ret = tinyobj_parse_obj(&custom_sky_attrib, &shapes, &num_shapes, &materials,
		&num_materials, (const char*)file_buffer, file_size, flags);

	FS_FreeFile(file_buffer);

	if (ret != TINYOBJ_SUCCESS) {
		Com_WPrintf("Couldn't parse sky polygon definition file %s.\n", filename);
		return 0;
	}

	tinyobj_shapes_free(shapes, num_shapes);
	tinyobj_materials_free(materials, num_materials);

	if (custom_sky_attrib.num_face_num_verts == 0)
		tinyobj_attrib_free(&custom_sky_attrib);
		
	return custom_sky_attrib.num_face_num_verts;
}

static uint32_t
bsp_mesh_create_custom_sky_prims(uint32_t* prim_ctr, bsp_mesh_t* wm, const bsp_t* bsp)
{
	int face_offset = 0;
	for (uint32_t nprim = 0; nprim < custom_sky_attrib.num_face_num_verts; nprim++)
	{
		int face_num_verts = custom_sky_attrib.face_num_verts[nprim];
		int i0 = custom_sky_attrib.faces[face_offset + 0].v_idx;
		int i1 = custom_sky_attrib.faces[face_offset + 1].v_idx;
		int i2 = custom_sky_attrib.faces[face_offset + 2].v_idx;

		float positions[9];
		VectorCopy(custom_sky_attrib.vertices + i0 * 3, positions + 0);
		VectorCopy(custom_sky_attrib.vertices + i1 * 3, positions + 3);
		VectorCopy(custom_sky_attrib.vertices + i2 * 3, positions + 6);

		if (*prim_ctr >= wm->num_primitives_allocated)
		{
			assert(!"Primitive buffer overflow.");
			return nprim;
		}
		
		VboPrimitive* prim = wm->primitives + *prim_ctr;

		memset(prim, 0, sizeof(*prim));
		VectorCopy(positions + 0, prim->pos0);
		VectorCopy(positions + 3, prim->pos1);
		VectorCopy(positions + 6, prim->pos2);
		
		vec3_t center;
		get_triangle_off_center(positions, center, NULL, 1.f);

		int cluster = BSP_PointLeaf(bsp->nodes, center)->cluster;
		prim->cluster = cluster;
		prim->material_id = MATERIAL_FLAG_LIGHT | MATERIAL_KIND_SKY;

		light_poly_t* light = append_light_poly(&wm->num_light_polys, &wm->allocated_light_polys, &wm->light_polys);

		memcpy(light->positions, positions, sizeof(prim_positions_t));
		VectorSet(light->color, -1.f, -1.f, -1.f); // special value for the sky
		VectorCopy(center, light->off_center);
		light->material = 0;
		light->style = 0;
		light->cluster = cluster;

		++*prim_ctr;

		face_offset += face_num_verts;
	}

	tinyobj_attrib_free(&custom_sky_attrib);

	return custom_sky_attrib.num_face_num_verts;
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
	vkpt_cameras_load(wm, full_game_map_name);

	wm->models = Z_Malloc(bsp->nummodels * sizeof(bsp_model_t));
	memset(wm->models, 0, bsp->nummodels * sizeof(bsp_model_t));

    wm->num_models = bsp->nummodels;
	wm->num_clusters = bsp->vis->numclusters;

	if (wm->num_clusters + 1 >= MAX_LIGHT_LISTS)
	{
		Com_Error(ERR_FATAL, "The BSP model has too many clusters (%d)", wm->num_clusters);
	}
	
	wm->num_primitives_allocated = count_triangles(bsp);

	uint32_t num_custom_sky_prims = bsp_mesh_load_custom_sky(full_game_map_name);
	if (num_custom_sky_prims > 0)
		wm->num_primitives_allocated += num_custom_sky_prims;

	wm->primitives = Z_Malloc(wm->num_primitives_allocated * sizeof(VboPrimitive));
	wm->num_primitives = 0;

	// clear these here because `bsp_mesh_load_custom_sky` creates lights before `collect_light_polys`
	wm->num_light_polys = 0;
	wm->allocated_light_polys = 0;
	wm->light_polys = NULL;

    uint32_t prim_ctr = 0;

#if DUMP_WORLD_MESH_TO_OBJ
	{
		char filename[MAX_QPATH];
		Q_snprintf(filename, sizeof(filename), "C:\\temp\\%s.obj", map_name);
		obj_dump_file = fopen(filename, "w");
		obj_vertex_num = 1;
	}
#endif

	vkpt_init_model_geometry(&wm->geom_opaque, 1);
	vkpt_init_model_geometry(&wm->geom_transparent, 1);
	vkpt_init_model_geometry(&wm->geom_masked, 1);
	vkpt_init_model_geometry(&wm->geom_sky, 1);
	vkpt_init_model_geometry(&wm->geom_custom_sky, 1);

	uint32_t first_prim = prim_ctr;
	collect_surfaces(&prim_ctr, wm, bsp, -1, filter_static_opaque);
	vkpt_append_model_geometry(&wm->geom_opaque, prim_ctr - first_prim, first_prim, "bsp");

	first_prim = prim_ctr;
	collect_surfaces(&prim_ctr, wm, bsp, -1, filter_static_transparent);
	vkpt_append_model_geometry(&wm->geom_transparent, prim_ctr - first_prim, first_prim, "bsp");

	first_prim = prim_ctr;
	collect_surfaces(&prim_ctr, wm, bsp, -1, filter_static_masked);
	vkpt_append_model_geometry(&wm->geom_masked, prim_ctr - first_prim, first_prim, "bsp");

	first_prim = prim_ctr;
	collect_surfaces(&prim_ctr, wm, bsp, -1, filter_static_sky);
	vkpt_append_model_geometry(&wm->geom_sky, prim_ctr - first_prim, first_prim, "bsp");
	
	first_prim = prim_ctr;
	if (num_custom_sky_prims > 0)
		bsp_mesh_create_custom_sky_prims(&prim_ctr, wm, bsp);
	if (cvar_pt_bsp_sky_lights->integer > 1)
		collect_surfaces(&prim_ctr, wm, bsp, -1, filter_nodraw_sky_lights);
	vkpt_append_model_geometry(&wm->geom_custom_sky, prim_ctr - first_prim, first_prim, "bsp");

    for (int k = 0; k < bsp->nummodels; k++) {
		bsp_model_t* model = wm->models + k;
		first_prim = prim_ctr;
		collect_surfaces(&prim_ctr, wm, bsp, k, filter_all);
		vkpt_init_model_geometry(&model->geometry, 1);
		vkpt_append_model_geometry(&model->geometry, prim_ctr - first_prim, first_prim, "bsp_model");
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

	wm->num_primitives = prim_ctr;
	
	compute_world_tangents(bsp, wm);
	
	for(int i = 0; i < wm->num_models; i++) 
	{
		bsp_model_t* model = wm->models + i;

		compute_aabb(wm->primitives + model->geometry.prim_offsets[0], model->geometry.prim_counts[0], model->aabb_min, model->aabb_max);

		VectorAdd(model->aabb_min, model->aabb_max, model->center);
		VectorScale(model->center, 0.5f, model->center);
	}

	compute_aabb(wm->primitives + wm->geom_opaque.prim_offsets[0], wm->geom_opaque.prim_counts[0], wm->world_aabb.mins, wm->world_aabb.maxs);
	append_aabb(wm->primitives + wm->geom_transparent.prim_offsets[0], wm->geom_transparent.prim_counts[0], wm->world_aabb.mins, wm->world_aabb.maxs);
	append_aabb(wm->primitives + wm->geom_masked.prim_offsets[0], wm->geom_masked.prim_counts[0], wm->world_aabb.mins, wm->world_aabb.maxs);

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

	Z_Free(wm->primitives);

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
		Q_concat(buffer, sizeof(buffer), "textures/", info->name, ".wal");
		FS_NormalizePath(buffer);

		pbr_material_t * mat = MAT_Find(buffer, IT_WALL, flags);
		if (!mat)
			Com_EPrintf("error finding material '%s'\n", buffer);
		
		if(cvar_pt_enable_surface_lights->integer)
		{
			/* Synthesize an emissive material if the BSP surface has the LIGHT flag but the
			   material has no emissive image.
			   - Skip SKY and NODRAW surfaces, they'll be handled differently.
			   - Make WARP surfaces optional, as giving water, slime... an emissive texture clashes visually. */
			bool synth_surface_material = ((info->c.flags & (SURF_LIGHT | SURF_SKY | SURF_NODRAW)) == SURF_LIGHT)
				&& (info->radiance != 0);
			
			bool is_warp_surface = (info->c.flags & SURF_WARP) != 0;
			
			bool material_custom = !mat->source_matfile[0];
			
			synth_surface_material &= (cvar_pt_enable_surface_lights->integer >= 2) || material_custom;
			if (cvar_pt_enable_surface_lights_warp->integer == 0)
				synth_surface_material &= !is_warp_surface;
			
			if (synth_surface_material)
			{
				MAT_SynthesizeEmissive(mat);
				mat->flags |= MATERIAL_FLAG_LIGHT;
				/* If emissive is "fake", treat absence of BSP radiance flag as "not emissive":
				* The assumption is that this is closer to the author's intention */
				mat->default_radiance = 0.0f;
			}
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
