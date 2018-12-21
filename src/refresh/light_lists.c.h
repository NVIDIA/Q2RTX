/*
Copyright (C) 2018 Tobias Zirr

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

#include <assert.h>

// this file extracts light lists for each cluster of the map
// light sampling algorithm in shaders/light_lists.h 
// ===
// NOTE FOR OTHER PROJECTS:
// while in this game, we conveniently leverage the PVS to
// aggregate coarse lists of potentially influencing lights,
// any other method of light culling could be used for that
// purpose, e.g. voxel grids, clipmaps of relevant nearfield/
// farfield resp. local/global light sources.
// In principle, there are many parallels to deferred shading.
// The light sampling algorithm in shaders/light_lists.h takes
// a stochastic constant-size strided subsample of the lists
// emitted here in every frame, therefore it can handle decently
// long lists of light with constant computation time. Quality of
// lighting will ofc degrade with light lists longer than more
// than a few _dozens_ of light sources, but overall, temporal
// accumulation allows for quite a bit of lazyness/convenience/
// measures of practicability in light culling. :-)
// It helps to make sure that every stochastic subset of the list is
// a somewhat good representation of lighting, striding hopefully does
// the trick for many scenarios

static vec3_t* cluster_aabbs(bsp_mesh_t *wm, float dilation) {
	vec3_t* aabbs = Z_Malloc(wm->num_clusters  * 2 * sizeof(vec3_t));
	for (int i = 0; i < wm->num_clusters ; i++) {
		vec3_t* aabb_min = aabbs[2 * i];
		vec3_t* aabb_max = aabb_min + 1;

		aabb_min[0][0] = 2.e32f;
		aabb_min[0][1] = 2.e32f;
		aabb_min[0][2] = 2.e32f;

		aabb_max[0][0] = -2.e32f;
		aabb_max[0][1] = -2.e32f;
		aabb_max[0][2] = -2.e32f;
	}
	for (int i = 0; i < wm->num_indices; i++) {
		if (wm->clusters[i/3] < 0) continue;
		vec3_t* aabb_min = aabbs[2 * wm->clusters[i/3]];
		vec3_t* aabb_max = aabb_min + 1;

		vec3_t v;
		v[0] = wm->positions[wm->indices[i] * 3 + 0];
		v[1] = wm->positions[wm->indices[i] * 3 + 1];
		v[2] = wm->positions[wm->indices[i] * 3 + 2];

		aabb_min[0][0] = MIN(aabb_min[0][0], v[0]);
		aabb_min[0][1] = MIN(aabb_min[0][1], v[1]);
		aabb_min[0][2] = MIN(aabb_min[0][2], v[2]);

		aabb_max[0][0] = MAX(aabb_max[0][0], v[0]);
		aabb_max[0][1] = MAX(aabb_max[0][1], v[1]);
		aabb_max[0][2] = MAX(aabb_max[0][2], v[2]);
	}
	// 'dilate', adjacency/overlap checked for light lists
	for (int i = 0; i < wm->num_clusters ; i++) {
		vec3_t* aabb_min = aabbs[2 * i];
		vec3_t* aabb_max = aabb_min + 1;

		aabb_min[0][0] -= dilation;
		aabb_min[0][1] -= dilation;
		aabb_min[0][2] -= dilation;

		aabb_max[0][0] += dilation;
		aabb_max[0][1] += dilation;
		aabb_max[0][2] += dilation;
	}
	return aabbs;
}

static int aabb_overlap(vec3_t* aabbs, int i, int j) {
	return MAX(aabbs[2*i][0], aabbs[2*j][0]) <= MIN(aabbs[2*i+1][0], aabbs[2*j+1][0])
		&& MAX(aabbs[2*i][1], aabbs[2*j][1]) <= MIN(aabbs[2*i+1][1], aabbs[2*j+1][1])
		&& MAX(aabbs[2*i][2], aabbs[2*j][2]) <= MIN(aabbs[2*i+1][2], aabbs[2*j+1][2]);
}

static void cluster_vis_mask(bsp_t *bsp, byte mask[VIS_MAX_BYTES], int i, vec3_t* aabbs) {
	byte imask[VIS_MAX_BYTES];
	BSP_ClusterVis(bsp, imask, i, DVIS_PVS);
	assert(Q_IsBitSet(imask, i));
	memcpy(mask, imask, sizeof(imask));
	// dilate
	for (int j = 0; j < bsp->visrowsize; j++) {
		if (imask[j]) {
			for (int k = 0; k < 8; ++k) {
				if (imask[j] & (1 << k) && aabb_overlap(aabbs, i, 8 * j + k)) {
					byte jmask[VIS_MAX_BYTES];
					BSP_ClusterVis(bsp, jmask, 8 * j + k, DVIS_PVS);
					for (int l = 0; l < bsp->visrowsize; l++) {
						mask[l] |= jmask[l];
					}
				}
			}
		}
	}
}

static int*
collect_light_clusters(bsp_mesh_t *wm, bsp_t *bsp)
{
	mface_t *surfaces = bsp->faces;
	int num_faces = bsp->numfaces;

	int *face_clusters = Z_Malloc(num_faces * sizeof(int));
	memset(face_clusters, -1, num_faces * sizeof(int));

	int num_leafs = bsp->numleafs;
	for (int i = 0; i < num_leafs; ++i) {
		int cidx = bsp->leafs[i].cluster;
		int nlf = bsp->leafs[i].numleaffaces;
		mface_t** it = bsp->leafs[i].firstleafface;
		for (int j = 0; j < nlf; ++j, ++it) {
			int fidx = *it - surfaces;
			assert(cidx >= 0);
			face_clusters[fidx] = cidx;
		}
	}

	return face_clusters;
}

static void
collect_cluster_lights(bsp_mesh_t *wm, bsp_t *bsp)
{
	int num_clusters = bsp->vis->numclusters; // bsp->visrowsize << 3;
	int num_cluster_bytes = bsp->visrowsize;

	wm->num_clusters = num_clusters;
	wm->cluster_light_offsets = Z_Malloc((num_clusters+1) * sizeof(int));

	int *local_light_counts = Z_Malloc(num_clusters * sizeof(int));
	memset(local_light_counts, 0, num_clusters * sizeof(int));

	int num_tris = wm->num_indices/3;
	for (int i = 0; i < num_tris; i++) {
		if (wm->materials[i] & BSP_FLAG_LIGHT || wm->clusters[i] & BSP_FLAG_LIGHT) {
			int cidx = wm->clusters[i];
			if (cidx >= 0) {
				cidx &= ~BSP_FLAG_LIGHT;
				assert(cidx < num_clusters);
				local_light_counts[cidx]++;
			}
		}
	}

	int *local_light_offsets = Z_Malloc((num_clusters+1) * sizeof(int));
	int num_cluster_lights = 0;
	for (int i = 0; i < num_clusters; i++) {
		local_light_offsets[i] = num_cluster_lights;
		num_cluster_lights += local_light_counts[i];
	}
	local_light_offsets[num_clusters] = num_cluster_lights;

	int *local_cluster_lights = Z_Malloc(num_cluster_lights * sizeof(int));
	for (int i = 0; i < num_tris; i++) {
		if (wm->materials[i] & BSP_FLAG_LIGHT || wm->clusters[i] & BSP_FLAG_LIGHT) {
			int cidx = wm->clusters[i];
			if (cidx >= 0) {
				cidx &= ~BSP_FLAG_LIGHT;
				wm->clusters[i] = cidx; // flags no longer needed
				local_cluster_lights[local_light_offsets[cidx]++] = i;
			}
		}
	}

	// PVS seems slightly broken, try recovering by dilation step
	// that requires AABBs of clusters!
	vec3_t* aabbs = cluster_aabbs(wm, 8.f); // 8 taken from FatPVS

	int *cluster_light_counts = Z_Malloc(num_clusters * sizeof(int));
	memset(cluster_light_counts, 0, num_clusters * sizeof(int));

	byte mask[VIS_MAX_BYTES];
	for (int i = 0; i < num_clusters; i++) {
		local_light_offsets[i] -= local_light_counts[i]; // reset after prev loop
		cluster_vis_mask(bsp, mask, i, aabbs);
		for (int j = 0; j < num_cluster_bytes; j++) {
			if (mask[j]) {
				for (int k = 0; k < 8; ++k) {
					if (mask[j] & (1 << k))
						cluster_light_counts[i] += local_light_counts[j * 8 + k];
				}
			}
		}
	}

	num_cluster_lights = 0;
	for (int i = 0; i < num_clusters; i++) {
		wm->cluster_light_offsets[i] = num_cluster_lights;
		num_cluster_lights += cluster_light_counts[i];
	}
	wm->cluster_light_offsets[num_clusters] = num_cluster_lights;

	wm->num_cluster_lights = num_cluster_lights;
	wm->cluster_lights = Z_Malloc(num_cluster_lights * sizeof(int));

	for (int i = 0; i < num_clusters; i++) {
		cluster_vis_mask(bsp, mask, i, aabbs);
		for (int j = 0; j < num_cluster_bytes; j++) {
			if (mask[j]) {
				for (int k = 0; k < 8; ++k) {
					if (mask[j] & (1 << k)) {
						int ii = j * 8 + k;
						memcpy(wm->cluster_lights + wm->cluster_light_offsets[i]
							, local_cluster_lights + local_light_offsets[ii]
							, sizeof(int) * local_light_counts[ii]);
						wm->cluster_light_offsets[i] += local_light_counts[ii];
					}
				}
			}
		}
	}

	for (int i = 0; i < num_clusters; i++) {
		wm->cluster_light_offsets[i] -= cluster_light_counts[i]; // reset after prev loop
	}

	Z_Free(local_light_counts);
	Z_Free(local_light_offsets);
	Z_Free(local_cluster_lights);
	Z_Free(cluster_light_counts);
	Z_Free(aabbs);
}
