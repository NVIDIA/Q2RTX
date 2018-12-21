/*
Copyright (C) 2018 Christoph Schied

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

#include <assert.h>

#include "../light_lists.c.h"

static int
create_poly(
	const mface_t *surf,
	float    *positions_out,
	float    *tex_coord_out,
	uint32_t *material_out)
{
	static const int max_vertices = 32;
	float positions [3 * /*max_vertices*/ 32];
	float tex_coords[2 * /*max_vertices*/ 32];
	mtexinfo_t *texinfo = surf->texinfo;
	assert(surf->numsurfedges < max_vertices);
	int flags = surf->drawflags;
	flags |= (surf->texinfo ? surf->texinfo->c.flags : 0);
	flags &= (surf->texinfo && surf->texinfo->radiance && !(flags & SURF_WARP) ? ~0 : ~SURF_LIGHT);
	if(surf->texinfo && surf->texinfo->image->is_light)
		flags |= SURF_LIGHT;
	float sc[2] = { 1.0f / texinfo->image->width, 1.0f / texinfo->image->height };

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
	}

	pos_center[0] /= (float)surf->numsurfedges;
	pos_center[1] /= (float)surf->numsurfedges;
	pos_center[2] /= (float)surf->numsurfedges;

	tc_center[0] = (DotProduct(pos_center, texinfo->axis[0]) + texinfo->offset[0]) * sc[0];
	tc_center[1] = (DotProduct(pos_center, texinfo->axis[1]) + texinfo->offset[1]) * sc[1];

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

#define CP_M(idx) \
    do { \
        if(material_out) { \
            material_out[k] = (int)(texinfo->image - r_images); \
			if(flags & SURF_LIGHT)      material_out[k] |= BSP_FLAG_LIGHT; \
			if(flags & SURF_WARP)       material_out[k] |= BSP_FLAG_WATER; \
			if(flags & SURF_TRANS_MASK) material_out[k] |= BSP_FLAG_TRANSPARENT; \
        } \
    } while(0)

	int k = 0;
	/* switch between triangle fan around center or first vertex */
	//int tess_center = 0;
	int tess_center = surf->numsurfedges > 4;

	const int num_triangles = tess_center
		? surf->numsurfedges
		: surf->numsurfedges - 2;

	for (int i = 0; i < num_triangles; i++) {
		const int e = surf->numsurfedges;

		int i1 = (i + 2 - tess_center) % e;
		int i2 = (i + 1 - tess_center) % e;

		CP_V(k, tess_center ? pos_center : positions);
		CP_T(k, tess_center ? tc_center : tex_coords);
		CP_M(k);
		k++;

		CP_V(k, positions + i1 * 3);
		CP_T(k, tex_coords + i1 * 2);
		CP_M(k);
		k++;

		CP_V(k, positions + i2 * 3);
		CP_T(k, tex_coords + i2 * 2);
		CP_M(k);
		k++;

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

static void
collect_surfaces(int *idx_ctr, bsp_mesh_t *wm, bsp_t *bsp, int model_idx, int skip_mask, int filter_mask)
{
	mface_t *surfaces = model_idx < 0 ? bsp->faces : bsp->models[model_idx].firstface;
	int num_faces = model_idx < 0 ? bsp->numfaces : bsp->models[model_idx].numfaces;

	int *face_clusters = model_idx < 0 ? collect_light_clusters(wm, bsp) : NULL;

	for (int i = 0; i < num_faces; i++) {
		mface_t *surf = surfaces + i;
		int flags = surf->drawflags;
		flags |= (surf->texinfo ? surf->texinfo->c.flags : 0);
		flags &= (surf->texinfo && surf->texinfo->radiance ? ~0 : ~SURF_LIGHT);

#if 0
		if (surf->texinfo && surf->texinfo->image && (flags & SURF_LIGHT)) {
			int material_slot = (int)(surf->texinfo->image - r_images);
			if (material_slot >= 0 && material_slot < MAX_MATERIALS) {
				material_buffer.material_data[material_slot].emission = 1.0;
				material_buffer.dirty = 1;
			}
		}
#endif

		if ((flags & skip_mask))
			continue;

		if (filter_mask && !(flags & filter_mask))
			continue;

		if (model_idx < 0 && belongs_to_model(bsp, surf)) {
			continue;
		}

		if (*idx_ctr + create_poly(surf, NULL, NULL, NULL) >= WM_MAX_VERTICES) {
			Com_Error(ERR_FATAL, "error: exceeding max vertex limit\n");
		}

		int cnt = create_poly(surf,
			&wm->positions[*idx_ctr * 3],
			&wm->tex_coords[*idx_ctr * 2],
			&wm->materials[*idx_ctr / 3]);

		if(face_clusters) {
			for (int it = *idx_ctr / 3, k = 0; k < cnt; k += 3, ++it) {
				wm->clusters[it] = face_clusters[i];
			}
		}

		*idx_ctr += cnt;
	}
}

void
bsp_mesh_create_from_bsp(bsp_mesh_t *wm, bsp_t *bsp)
{
	wm->models_idx_offset = Z_Malloc(bsp->nummodels * sizeof(int));
	wm->models_idx_count = Z_Malloc(bsp->nummodels * sizeof(int));
	memset(wm->models_idx_offset, 0, bsp->nummodels * sizeof(int));
	memset(wm->models_idx_count, 0, bsp->nummodels * sizeof(int));
	wm->model_centers = Z_Malloc(bsp->nummodels * 3 * sizeof(float));

	wm->num_models = bsp->nummodels;

	wm->num_vertices  = 0;
	wm->num_indices   = 0;
	wm->positions     = Z_Malloc(WM_MAX_VERTICES * 3 * sizeof(*wm->positions));
	wm->tex_coords    = Z_Malloc(WM_MAX_VERTICES * 2 * sizeof(*wm->tex_coords));
	wm->materials     = Z_Malloc(WM_MAX_VERTICES / 3 * sizeof(*wm->materials));
	wm->clusters      = Z_Malloc(WM_MAX_VERTICES / 3 * sizeof(*wm->clusters));

	int idx_ctr = 0;

	const int flags_static_world = SURF_NODRAW | SURF_SKY;

	collect_surfaces(&idx_ctr, wm, bsp, -1, flags_static_world, 0);
	wm->world_idx_count = idx_ctr;

	wm->world_fluid_offset = idx_ctr;
	collect_surfaces(&idx_ctr, wm, bsp, -1, 0, SURF_WARP);
	wm->world_fluid_count = idx_ctr - wm->world_fluid_offset;

	wm->world_light_offset = idx_ctr;
	collect_surfaces(&idx_ctr, wm, bsp, -1, flags_static_world, SURF_LIGHT);
	wm->world_light_count = idx_ctr - wm->world_light_offset;

	for (int k = 0; k < bsp->nummodels; k++) {
		wm->models_idx_offset[k] = idx_ctr;
		collect_surfaces(&idx_ctr, wm, bsp, k, flags_static_world, 0);
		wm->models_idx_count[k] = idx_ctr - wm->models_idx_offset[k];
	}

	wm->num_indices = idx_ctr;
	wm->num_vertices = idx_ctr;

	wm->indices = Z_Malloc(idx_ctr * sizeof(int));
	for (int i = 0; i < wm->num_vertices; i++)
		wm->indices[i] = i;

	if (wm->num_vertices >= WM_MAX_VERTICES) {
		Com_Error(ERR_FATAL, "too many vertices\n");
	}

	for(int i = 0; i < wm->num_models; i++) {
		vec3_t aabb_min = {  999999999.0f,  999999999.0f,  999999999.0f };
		vec3_t aabb_max = { -999999999.0f, -999999999.0f, -999999999.0f };

		for(int j = 0; j < wm->models_idx_count[i]; j++) {
			vec3_t v;
			v[0] = wm->positions[(wm->models_idx_offset[i] + j) * 3 + 0];
			v[1] = wm->positions[(wm->models_idx_offset[i] + j) * 3 + 1];
			v[2] = wm->positions[(wm->models_idx_offset[i] + j) * 3 + 2];

			aabb_min[0] = MIN(aabb_min[0], v[0]);
			aabb_min[1] = MIN(aabb_min[1], v[1]);
			aabb_min[2] = MIN(aabb_min[2], v[2]);

			aabb_max[0] = MAX(aabb_max[0], v[0]);
			aabb_max[1] = MAX(aabb_max[1], v[1]);
			aabb_max[2] = MAX(aabb_max[2], v[2]);
		}

		wm->model_centers[i][0] = (aabb_min[0] + aabb_max[0]) * 0.5f;
		wm->model_centers[i][1] = (aabb_min[1] + aabb_max[1]) * 0.5f;
		wm->model_centers[i][2] = (aabb_min[2] + aabb_max[2]) * 0.5f;
	}

	//FILE *f = fopen("/tmp/lights", "a+");
	for(int i = 0; i < wm->num_indices; i++) {
		uint32_t m = wm->materials[i];
		m &= BSP_TEXTURE_MASK;

		if(m >= MAX_RIMAGES)
			continue;

		if(r_images[m].is_light)
			wm->materials[i] |= BSP_FLAG_LIGHT;
		//fprintf(f, "%s\n", r_images[m].name);

	}
	//fclose(f);

	collect_cluster_lights(wm, bsp);
}

void
bsp_mesh_destroy(bsp_mesh_t *wm)
{
	Z_Free(wm->models_idx_offset);
	Z_Free(wm->models_idx_count);
	Z_Free(wm->model_centers);

	Z_Free(wm->positions);
	Z_Free(wm->tex_coords);
	Z_Free(wm->indices);

	memset(wm, 0, sizeof(*wm));
}

void
bsp_mesh_register_textures(bsp_t *bsp)
{
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
		info->image = IMG_Find(buffer, IT_WALL, flags);
	}
}

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
