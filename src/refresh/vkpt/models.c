/*
Copyright (C) 2018 Christoph Schied
Copyright (C) 2018 Florian Simon
Copyright (C) 2003-2006 Andrey Nazarov
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

/*
Copyright (C) 2003-2006 Andrey Nazarov

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

#include "format/md2.h"
#include "format/md3.h"
#include "format/sp2.h"
#include "material.h"
#include <assert.h>

static void extract_model_lights(model_t* model)
{
	// Count the triangles in the model that have a material with the is_light flag set
	
	int num_lights = 0;
	
	for (int mesh_idx = 0; mesh_idx < model->nummeshes; mesh_idx++)
	{
		const maliasmesh_t* mesh = model->meshes + mesh_idx;
		for (int skin_idx = 0; skin_idx < mesh->numskins; skin_idx++)
		{
			const pbr_material_t* mat = mesh->materials[skin_idx];
			if ((mat->flags & MATERIAL_FLAG_LIGHT) != 0 && mat->image_emissive)
			{
				if (mesh->numskins != 1)
				{
					Com_DPrintf("Warning: model %s mesh %d has LIGHT material(s) but more than 1 skin (%d), "
						"which is unsupported.\n", model->name, mesh_idx, mesh->numskins);
					return;
				}

				num_lights += mesh->numtris;
			}
		}
	}

	// If there are no light triangles, there's nothing to do
	if (num_lights == 0)
		return;

	// Validate our current implementation limitations, give warnings if they are hit
	
	if (model->numframes > 1)
	{
		Com_DPrintf("Warning: model %s has LIGHT material(s) but more than 1 vertex animation frame, "
			"which is unsupported.\n", model->name);
		return;
	}

	if (model->iqmData && model->iqmData->blend_weights)
	{
		Com_DPrintf("Warning: model %s has LIGHT material(s) and skeletal animations, "
			"which is unsupported.\n", model->name);
		return;
	}

	// Actually extract the lights now
	
	if (!(model->light_polys = Hunk_Alloc(&model->hunk, sizeof(light_poly_t) * num_lights))) {
		Com_DPrintf("Warning: unable to allocate memory for %i light polygons.\n", num_lights);
		return;
	}
	model->num_light_polys = num_lights;

	num_lights = 0;

	for (int mesh_idx = 0; mesh_idx < model->nummeshes; mesh_idx++)
	{
		const maliasmesh_t* mesh = model->meshes + mesh_idx;
		assert(mesh->numskins == 1);
		assert(mesh->indices);
		assert(mesh->positions);
		
		pbr_material_t* mat = mesh->materials[0];
		if ((mat->flags & MATERIAL_FLAG_LIGHT) != 0 && mat->image_emissive)
		{
			for (int tri_idx = 0; tri_idx < mesh->numtris; tri_idx++)
			{
				light_poly_t* light = model->light_polys + num_lights;
				num_lights++;

				int i0 = mesh->indices[tri_idx * 3 + 0];
				int i1 = mesh->indices[tri_idx * 3 + 1];
				int i2 = mesh->indices[tri_idx * 3 + 2];
				
				assert(i0 < mesh->numverts);
				assert(i1 < mesh->numverts);
				assert(i2 < mesh->numverts);

				memcpy(light->positions + 0, mesh->positions + i0, sizeof(vec3_t));
				memcpy(light->positions + 3, mesh->positions + i1, sizeof(vec3_t));
				memcpy(light->positions + 6, mesh->positions + i2, sizeof(vec3_t));
				
				// Cluster is assigned after model instancing and transformation
				light->cluster = -1;

				light->material = mat;

				VectorCopy(mat->image_emissive->light_color, light->color);

				if (!mat->image_emissive->entire_texture_emissive)
				{
					// This extraction doesn't support partially emissive textures, so pretend the entire
					// texture is uniformly emissive and dim the light according to the area fraction.
					light->emissive_factor =
						(mat->image_emissive->max_light_texcoord[0] - mat->image_emissive->min_light_texcoord[0]) *
						(mat->image_emissive->max_light_texcoord[1] - mat->image_emissive->min_light_texcoord[1]);
				}
				else
					light->emissive_factor = 1.f;
				
				get_triangle_off_center(light->positions, light->off_center, NULL, 1.f);
				
			}
		}
	}
}

static void compute_missing_model_tangents(model_t* model)
{
	for (int mesh_idx = 0; mesh_idx < model->nummeshes; mesh_idx++)
	{
		maliasmesh_t* mesh = model->meshes + mesh_idx;

		if (mesh->tangents)
			continue;

		size_t tangent_size = mesh->numverts * model->numframes * sizeof(vec3_t);

		mesh->tangents = MOD_Malloc(tangent_size);

		memset(mesh->tangents, 0, tangent_size);

		int handedness = 0;

		for (int frame = 0; frame < model->numframes; frame++)
		{
			int voffset = frame * mesh->numverts;

			for (int tri = 0; tri < mesh->numtris; tri++)
			{
				int iA = mesh->indices[tri * 3 + 0] + voffset;
				int iB = mesh->indices[tri * 3 + 1] + voffset;
				int iC = mesh->indices[tri * 3 + 2] + voffset;

				const vec3_t* pA = mesh->positions + iA;
				const vec3_t* pB = mesh->positions + iB;
				const vec3_t* pC = mesh->positions + iC;

				const vec2_t* tA = mesh->tex_coords + iA;
				const vec2_t* tB = mesh->tex_coords + iB;
				const vec2_t* tC = mesh->tex_coords + iC;

				vec3_t dP0, dP1;
				VectorSubtract(*pB, *pA, dP0);
				VectorSubtract(*pC, *pA, dP1);

				vec2_t dt0, dt1;
				Vector2Subtract(*tB, *tA, dt0);
				Vector2Subtract(*tC, *tA, dt1);

				float inv_r = dt0[0] * dt1[1] - dt1[0] * dt0[1];

				if (inv_r == 0.f)
					continue;

				float r = 1.f / inv_r;

				vec3_t tangent = {
					(dt1[1] * dP0[0] - dt0[1] * dP1[0]) * r,
					(dt1[1] * dP0[1] - dt0[1] * dP1[1]) * r,
					(dt1[1] * dP0[2] - dt0[1] * dP1[2]) * r };

				VectorNormalize(tangent);

				vec3_t* tangentA = mesh->tangents + iA;
				vec3_t* tangentB = mesh->tangents + iB;
				vec3_t* tangentC = mesh->tangents + iC;

				VectorAdd(*tangentA, tangent, *tangentA);
				VectorAdd(*tangentB, tangent, *tangentB);
				VectorAdd(*tangentC, tangent, *tangentC);

				if (handedness == 0)
				{
					vec3_t bitangent = {
						(dt0[0] * dP1[0] - dt1[0] * dP0[0]) * r,
						(dt0[0] * dP1[1] - dt1[0] * dP0[1]) * r,
						(dt0[0] * dP1[2] - dt1[0] * dP0[2]) * r };

					VectorNormalize(bitangent);

					const vec3_t* normal = mesh->normals + iA;

					vec3_t cross;
					CrossProduct(*normal, tangent, cross);

					float dot = DotProduct(cross, bitangent);

					if (dot < 0.f)
						handedness = -1;
					else if (dot > 0.f)
						handedness = 1;
				}
			}
		}

		for (int vtx = 0; vtx < mesh->numverts * model->numframes; vtx++)
		{
			vec3_t* tangent = mesh->tangents + vtx;

			VectorNormalize(*tangent);
		}

		mesh->handedness = (handedness < 0);
	}
}

int MOD_LoadMD2_RTX(model_t *model, const void *rawdata, size_t length, const char* mod_name)
{
	dmd2header_t    header;
	dmd2frame_t     *src_frame;
	dmd2trivertx_t  *src_vert;
	dmd2triangle_t  *src_tri;
	dmd2stvert_t    *src_tc;
	char            *src_skin;
	maliasframe_t   *dst_frame;
	maliasmesh_t    *dst_mesh;
	int             val;
	uint16_t        remap[TESS_MAX_INDICES];
	uint16_t        vertIndices[TESS_MAX_INDICES];
	uint16_t        tcIndices[TESS_MAX_INDICES];
	uint16_t        finalIndices[TESS_MAX_INDICES];
	int             numverts, numindices;
	char            skinname[MAX_QPATH];
	vec_t           scale_s, scale_t;
	vec3_t          mins, maxs;
	int             ret;

	if (length < sizeof(header)) {
		return Q_ERR_FILE_TOO_SMALL;
	}

	// byte swap the header
	LittleBlock(&header, rawdata, sizeof(header));

	// validate the header
	ret = MOD_ValidateMD2(&header, length);
	if (ret) {
		if (ret == Q_ERR_TOO_FEW) {
			// empty models draw nothing
			model->type = MOD_EMPTY;
			return Q_ERR_SUCCESS;
		}
		return ret;
	}

	// load all triangle indices
	numindices = 0;
	src_tri = (dmd2triangle_t *)((byte *)rawdata + header.ofs_tris);
	for (int i = 0; i < header.num_tris; i++) {
		int good = 1;
		for (int j = 0; j < 3; j++) {
			uint16_t idx_xyz = LittleShort(src_tri->index_xyz[j]);
			uint16_t idx_st = LittleShort(src_tri->index_st[j]);

			// some broken models have 0xFFFF indices
			if (idx_xyz >= header.num_xyz || idx_st >= header.num_st) {
				good = 0;
				break;
			}

			vertIndices[numindices + j] = idx_xyz;
			tcIndices[numindices + j] = idx_st;
		}
		if (good) {
			// only count good triangles
			numindices += 3;
		}
		src_tri++;
	}

	if (numindices < 3) {
		return Q_ERR_TOO_FEW;
	}

	bool all_normals_same = true;
	int same_normal = -1;

	src_frame = (dmd2frame_t *)((byte *)rawdata + header.ofs_frames);
	for (int i = 0; i < numindices; i++)
	{
		int v = vertIndices[i];
		int normal = src_frame->verts[v].lightnormalindex;

		// detect if the model has broken normals - they are all the same in that case
		// it happens with players/w_<weapon>.md2 models for example
		if (same_normal < 0)
			same_normal = normal;
		else if (normal != same_normal)
			all_normals_same = false;
	}

	for (int i = 0; i < numindices; i++) {
		remap[i] = 0xFFFF;
	}

	// remap all triangle indices
	numverts = 0;
	src_tc = (dmd2stvert_t *)((byte *)rawdata + header.ofs_st);
	for (int i = 0; i < numindices; i++) {
		if (remap[i] != 0xFFFF) {
			continue; // already remapped
		}

		// only dedup vertices if we're not regenerating normals
		if (!all_normals_same)
		{
			for (int j = i + 1; j < numindices; j++) {
				if (vertIndices[i] == vertIndices[j] &&
					(src_tc[tcIndices[i]].s == src_tc[tcIndices[j]].s &&
						src_tc[tcIndices[i]].t == src_tc[tcIndices[j]].t)) {
					// duplicate vertex
					remap[j] = i;
					finalIndices[j] = numverts;
				}
			}
		}

		// new vertex
		remap[i] = i;
		finalIndices[i] = numverts++;
	}

	Hunk_Begin(&model->hunk, 50u<<20);
	model->type = MOD_ALIAS;
	model->nummeshes = 1;
	model->numframes = header.num_frames;
	CHECK(model->meshes = MOD_Malloc(sizeof(maliasmesh_t)));
	CHECK(model->frames = MOD_Malloc(header.num_frames * sizeof(maliasframe_t)));

	dst_mesh = model->meshes;
	dst_mesh->numtris    = numindices / 3;
	dst_mesh->numindices = numindices;
	dst_mesh->numverts   = numverts;
	dst_mesh->numskins   = header.num_skins;
	CHECK(dst_mesh->positions  = MOD_Malloc(numverts   * header.num_frames * sizeof(vec3_t)));
	CHECK(dst_mesh->normals    = MOD_Malloc(numverts   * header.num_frames * sizeof(vec3_t)));
	CHECK(dst_mesh->tex_coords = MOD_Malloc(numverts   * header.num_frames * sizeof(vec2_t)));
    CHECK(dst_mesh->indices    = MOD_Malloc(numindices * sizeof(int)));
    CHECK(dst_mesh->materials  = MOD_Malloc(sizeof(struct pbr_material_s*) * header.num_skins));

	if (dst_mesh->numtris != header.num_tris) {
		Com_DPrintf("%s has %d bad triangles\n", model->name, header.num_tris - dst_mesh->numtris);
	}

	// store final triangle indices
	for (int i = 0; i < numindices; i++) {
		dst_mesh->indices[i] = finalIndices[i];
	}

	// load all skins
	src_skin = (char *)rawdata + header.ofs_skins;
	for (int i = 0; i < header.num_skins; i++) {
		if (!Q_memccpy(skinname, src_skin, 0, sizeof(skinname))) {
			ret = Q_ERR_STRING_TRUNCATED;
			goto fail;
		}
		FS_NormalizePath(skinname);

		pbr_material_t * mat = MAT_Find(skinname, IT_SKIN, IF_NONE);
		
		dst_mesh->materials[i] = mat;

        src_skin += MD2_MAX_SKINNAME;
	}

	// load all tcoords
	src_tc = (dmd2stvert_t *)((byte *)rawdata + header.ofs_st);
	scale_s = 1.0f / header.skinwidth;
	scale_t = 1.0f / header.skinheight;

	// load all frames
	src_frame = (dmd2frame_t *)((byte *)rawdata + header.ofs_frames);
	dst_frame = model->frames;
	for (int j = 0; j < header.num_frames; j++) {
		LittleVector(src_frame->scale, dst_frame->scale);
		LittleVector(src_frame->translate, dst_frame->translate);

		// load frame vertices
		ClearBounds(mins, maxs);

		for (int i = 0; i < numindices; i++) {
			if (remap[i] != i) {
				continue;
			}
			src_vert = &src_frame->verts[vertIndices[i]];
			vec3_t *dst_pos = &dst_mesh->positions [j * numverts + finalIndices[i]];
			vec3_t *dst_nrm = &dst_mesh->normals   [j * numverts + finalIndices[i]];
			vec2_t *dst_tc  = &dst_mesh->tex_coords[j * numverts + finalIndices[i]];

			(*dst_tc)[0] = scale_s * src_tc[tcIndices[i]].s;
			(*dst_tc)[1] = scale_t * src_tc[tcIndices[i]].t;

			(*dst_pos)[0] = src_vert->v[0] * dst_frame->scale[0] + dst_frame->translate[0];
			(*dst_pos)[1] = src_vert->v[1] * dst_frame->scale[1] + dst_frame->translate[1];
			(*dst_pos)[2] = src_vert->v[2] * dst_frame->scale[2] + dst_frame->translate[2];

			(*dst_nrm)[0] = 0.0f;
			(*dst_nrm)[1] = 0.0f;
			(*dst_nrm)[2] = 0.0f;

			val = src_vert->lightnormalindex;

			if (val < NUMVERTEXNORMALS) {
				(*dst_nrm)[0] = bytedirs[val][0];
				(*dst_nrm)[1] = bytedirs[val][1];
				(*dst_nrm)[2] = bytedirs[val][2];
			}

			for (int k = 0; k < 3; k++) {
				val = (*dst_pos)[k];
				if (val < mins[k])
					mins[k] = val;
				if (val > maxs[k])
					maxs[k] = val;
			}
		}

		// if all normals are the same, rebuild them as flat triangle normals
		if (all_normals_same)
		{
			for (int tri = 0; tri < numindices / 3; tri++)
			{
				int i0 = j * numverts + finalIndices[tri * 3 + 0];
				int i1 = j * numverts + finalIndices[tri * 3 + 1];
				int i2 = j * numverts + finalIndices[tri * 3 + 2];

				vec3_t *p0 = &dst_mesh->positions[i0];
				vec3_t *p1 = &dst_mesh->positions[i1];
				vec3_t *p2 = &dst_mesh->positions[i2];

				vec3_t e1, e2, n;
				VectorSubtract(*p1, *p0, e1);
				VectorSubtract(*p2, *p0, e2);
				CrossProduct(e2, e1, n);
				VectorNormalize(n);

				VectorCopy(n, dst_mesh->normals[i0]);
				VectorCopy(n, dst_mesh->normals[i1]);
				VectorCopy(n, dst_mesh->normals[i2]);
			}
		}

		VectorVectorScale(mins, dst_frame->scale, mins);
		VectorVectorScale(maxs, dst_frame->scale, maxs);

		dst_frame->radius = RadiusFromBounds(mins, maxs);

		VectorAdd(mins, dst_frame->translate, dst_frame->bounds[0]);
		VectorAdd(maxs, dst_frame->translate, dst_frame->bounds[1]);

		src_frame = (dmd2frame_t *)((byte *)src_frame + header.framesize);
		dst_frame++;
	}

	// fix winding order
	for (int i = 0; i < dst_mesh->numindices; i += 3) {
		int tmp = dst_mesh->indices[i + 1];
		dst_mesh->indices[i + 1] = dst_mesh->indices[i + 2];
		dst_mesh->indices[i + 2] = tmp;
	}

	compute_missing_model_tangents(model);

	extract_model_lights(model);

	Hunk_End(&model->hunk);
	return Q_ERR_SUCCESS;

fail:
	Hunk_Free(&model->hunk);
	return ret;
}

#if USE_MD3

#define TAB_SIN(x) qvk.sintab[(x) & 255]
#define TAB_COS(x) qvk.sintab[((x) + 64) & 255]

static int MOD_LoadMD3Mesh(model_t *model, maliasmesh_t *mesh,
		const byte *rawdata, size_t length, size_t *offset_p)
{
	dmd3mesh_t      header;
	size_t          end;
	dmd3vertex_t    *src_vert;
	dmd3coord_t     *src_tc;
	dmd3skin_t      *src_skin;
	uint32_t        *src_idx;
	vec3_t          *dst_vert;
	vec3_t          *dst_norm;
	vec2_t          *dst_tc;
	int  *dst_idx;
	char            skinname[MAX_QPATH];
	int             i, ret;

	if (length < sizeof(header))
		return Q_ERR_BAD_EXTENT;

	// byte swap the header
	LittleBlock(&header, rawdata, sizeof(header));

	if (header.meshsize < sizeof(header) || header.meshsize > length)
		return Q_ERR_BAD_EXTENT;
	if (header.meshsize % q_alignof(dmd3mesh_t))
		return Q_ERR_BAD_ALIGN;
	if (header.num_verts < 3)
		return Q_ERR_TOO_FEW;
	if (header.num_verts > TESS_MAX_VERTICES)
		return Q_ERR_TOO_MANY;
	if (header.num_tris < 1)
		return Q_ERR_TOO_FEW;
	if (header.num_tris > TESS_MAX_INDICES / 3)
		return Q_ERR_TOO_MANY;
	if (header.num_skins > MD3_MAX_SKINS)
		return Q_ERR_TOO_MANY;
	end = header.ofs_skins + header.num_skins * sizeof(dmd3skin_t);
	if (end < header.ofs_skins || end > length)
		return Q_ERR_BAD_EXTENT;
	if (header.ofs_skins % q_alignof(dmd3skin_t))
		return Q_ERR_BAD_ALIGN;
	end = header.ofs_verts + header.num_verts * model->numframes * sizeof(dmd3vertex_t);
	if (end < header.ofs_verts || end > length)
		return Q_ERR_BAD_EXTENT;
	if (header.ofs_verts % q_alignof(dmd3vertex_t))
		return Q_ERR_BAD_ALIGN;
	end = header.ofs_tcs + header.num_verts * sizeof(dmd3coord_t);
	if (end < header.ofs_tcs || end > length)
		return Q_ERR_BAD_EXTENT;
	if (header.ofs_tcs % q_alignof(dmd3coord_t))
		return Q_ERR_BAD_ALIGN;
	end = header.ofs_indexes + header.num_tris * 3 * sizeof(uint32_t);
	if (end < header.ofs_indexes || end > length)
		return Q_ERR_BAD_EXTENT;
	if (header.ofs_indexes & 3)
		return Q_ERR_BAD_ALIGN;

	mesh->numtris = header.num_tris;
	mesh->numindices = header.num_tris * 3;
	mesh->numverts = header.num_verts;
	mesh->numskins = header.num_skins;
	CHECK(mesh->positions = MOD_Malloc(header.num_verts * model->numframes * sizeof(vec3_t)));
	CHECK(mesh->normals = MOD_Malloc(header.num_verts * model->numframes * sizeof(vec3_t)));
	CHECK(mesh->tex_coords = MOD_Malloc(header.num_verts * model->numframes * sizeof(vec2_t)));
    CHECK(mesh->indices = MOD_Malloc(sizeof(int) * header.num_tris * 3));
    CHECK(mesh->materials = MOD_Malloc(sizeof(struct pbr_material_s*) * header.num_skins));

	// load all skins
	src_skin = (dmd3skin_t *)(rawdata + header.ofs_skins);
	for (i = 0; i < header.num_skins; i++, src_skin++) {
		if (!Q_memccpy(skinname, src_skin->name, 0, sizeof(skinname)))
			return Q_ERR_STRING_TRUNCATED;
		FS_NormalizePath(skinname);

		pbr_material_t * mat = MAT_Find(skinname, IT_SKIN, IF_NONE);
		
		mesh->materials[i] = mat;
    }

	// load all vertices
	src_vert = (dmd3vertex_t *)(rawdata + header.ofs_verts);
	dst_vert = mesh->positions;
	dst_norm = mesh->normals;
    dst_tc = mesh->tex_coords;
    for (int frame = 0; frame < header.num_frames; frame++)
	{
		maliasframe_t *f = &model->frames[frame];
		src_tc = (dmd3coord_t *)(rawdata + header.ofs_tcs);

		for (i = 0; i < header.num_verts; i++) 
		{
			(*dst_vert)[0] = (float)(src_vert->point[0]) / 64.f;
			(*dst_vert)[1] = (float)(src_vert->point[1]) / 64.f;
			(*dst_vert)[2] = (float)(src_vert->point[2]) / 64.f;

			unsigned int lat = src_vert->norm[0];
			unsigned int lng = src_vert->norm[1];

			(*dst_norm)[0] = TAB_SIN(lat) * TAB_COS(lng);
			(*dst_norm)[1] = TAB_SIN(lat) * TAB_SIN(lng);
			(*dst_norm)[2] = TAB_COS(lat);

			VectorNormalize(*dst_norm);

			(*dst_tc)[0] = LittleFloat(src_tc->st[0]);
			(*dst_tc)[1] = LittleFloat(src_tc->st[1]);

			for (int k = 0; k < 3; k++) {
                f->bounds[0][k] = min(f->bounds[0][k], (*dst_vert)[k]);
                f->bounds[1][k] = max(f->bounds[1][k], (*dst_vert)[k]);
            }

			src_vert++; dst_vert++; dst_norm++;
			src_tc++; dst_tc++;
		}
	}


	// load all triangle indices
	src_idx = (uint32_t *)(rawdata + header.ofs_indexes);
	dst_idx = mesh->indices;
	for (i = 0; i < header.num_tris; i++) 
	{
		dst_idx[0] = LittleLong(src_idx[2]);
		dst_idx[1] = LittleLong(src_idx[1]);
		dst_idx[2] = LittleLong(src_idx[0]);

		if (dst_idx[0] >= header.num_verts)
			return Q_ERR_BAD_INDEX;

		src_idx += 3;
		dst_idx += 3;
	}
	
	*offset_p = header.meshsize;

	return Q_ERR_SUCCESS;

fail:
	return ret;
}

int MOD_LoadMD3_RTX(model_t *model, const void *rawdata, size_t length, const char* mod_name)
{
	dmd3header_t    header;
	size_t          end, offset, remaining;
	dmd3frame_t     *src_frame;
	maliasframe_t   *dst_frame;
	const byte      *src_mesh;
	int             i;
	int             ret;

	if (length < sizeof(header))
		return Q_ERR_FILE_TOO_SMALL;

	// byte swap the header
	LittleBlock(&header, rawdata, sizeof(header));

	if (header.ident != MD3_IDENT)
		return Q_ERR_UNKNOWN_FORMAT;
	if (header.version != MD3_VERSION)
		return Q_ERR_UNKNOWN_FORMAT;
	if (header.num_frames < 1)
		return Q_ERR_TOO_FEW;
	if (header.num_frames > MD3_MAX_FRAMES)
		return Q_ERR_TOO_MANY;
	end = header.ofs_frames + sizeof(dmd3frame_t) * header.num_frames;
	if (end < header.ofs_frames || end > length)
		return Q_ERR_BAD_EXTENT;
	if (header.ofs_frames % q_alignof(dmd3frame_t))
		return Q_ERR_BAD_ALIGN;
	if (header.num_meshes < 1)
		return Q_ERR_TOO_FEW;
	if (header.num_meshes > MD3_MAX_MESHES)
		return Q_ERR_TOO_MANY;
	if (header.ofs_meshes > length)
		return Q_ERR_BAD_EXTENT;
	if (header.ofs_meshes % q_alignof(dmd3mesh_t))
		return Q_ERR_BAD_ALIGN;

	Hunk_Begin(&model->hunk, 0x4000000);
	model->type = MOD_ALIAS;
	model->numframes = header.num_frames;
	model->nummeshes = header.num_meshes;
	CHECK(model->meshes = MOD_Malloc(sizeof(maliasmesh_t) * header.num_meshes));
	CHECK(model->frames = MOD_Malloc(sizeof(maliasframe_t) * header.num_frames));

	// load all frames
	src_frame = (dmd3frame_t *)((byte *)rawdata + header.ofs_frames);
	dst_frame = model->frames;
	for (i = 0; i < header.num_frames; i++) {
		LittleVector(src_frame->translate, dst_frame->translate);
		VectorSet(dst_frame->scale, MD3_XYZ_SCALE, MD3_XYZ_SCALE, MD3_XYZ_SCALE);

		ClearBounds(dst_frame->bounds[0], dst_frame->bounds[1]);

		src_frame++; dst_frame++;
	}

	// load all meshes
	src_mesh = (const byte *)rawdata + header.ofs_meshes;
	remaining = length - header.ofs_meshes;
	for (i = 0; i < header.num_meshes; i++) {
		ret = MOD_LoadMD3Mesh(model, &model->meshes[i], src_mesh, remaining, &offset);
		if (ret)
			goto fail;
		src_mesh += offset;
		remaining -= offset;
	}

    // calculate frame bounds
    dst_frame = model->frames;
    for (i = 0; i < header.num_frames; i++) {
        VectorScale(dst_frame->bounds[0], MD3_XYZ_SCALE, dst_frame->bounds[0]);
        VectorScale(dst_frame->bounds[1], MD3_XYZ_SCALE, dst_frame->bounds[1]);

        dst_frame->radius = RadiusFromBounds(dst_frame->bounds[0], dst_frame->bounds[1]);

        VectorAdd(dst_frame->bounds[0], dst_frame->translate, dst_frame->bounds[0]);
        VectorAdd(dst_frame->bounds[1], dst_frame->translate, dst_frame->bounds[1]);

        dst_frame++;
    }

	compute_missing_model_tangents(model);

	extract_model_lights(model);

	Hunk_End(&model->hunk);
	return Q_ERR_SUCCESS;

fail:
	Hunk_Free(&model->hunk);
	return ret;
}
#endif

int MOD_LoadIQM_RTX(model_t* model, const void* rawdata, size_t length, const char* mod_name)
{
	Hunk_Begin(&model->hunk, 0x4000000);
	model->type = MOD_ALIAS;

	int res = MOD_LoadIQM_Base(model, rawdata, length, mod_name), ret;

	if (res != Q_ERR_SUCCESS)
	{
		Hunk_Free(&model->hunk);
		return res;
	}

	char base_path[MAX_QPATH];
	COM_FilePath(mod_name, base_path, sizeof(base_path));

	CHECK(model->meshes = MOD_Malloc(sizeof(maliasmesh_t) * model->iqmData->num_meshes));
	model->nummeshes = (int)model->iqmData->num_meshes;
	model->numframes = 1; // these are baked frames, so that the VBO uploader will only make one copy of the vertices

	for (unsigned model_idx = 0; model_idx < model->iqmData->num_meshes; model_idx++)
	{
		iqm_mesh_t* iqm_mesh = &model->iqmData->meshes[model_idx];
		maliasmesh_t* mesh = &model->meshes[model_idx];

		mesh->indices = iqm_mesh->data->indices ? (int*)iqm_mesh->data->indices + iqm_mesh->first_triangle * 3 : NULL;
		mesh->positions = iqm_mesh->data->positions ? (vec3_t*)(iqm_mesh->data->positions + iqm_mesh->first_vertex * 3) : NULL;
		mesh->normals = iqm_mesh->data->normals ? (vec3_t*)(iqm_mesh->data->normals + iqm_mesh->first_vertex * 3) : NULL;
		mesh->tex_coords = iqm_mesh->data->texcoords ? (vec2_t*)(iqm_mesh->data->texcoords + iqm_mesh->first_vertex * 2) : NULL;
		mesh->tangents = iqm_mesh->data->tangents ? (vec3_t*)(iqm_mesh->data->tangents + iqm_mesh->first_vertex * 3) : NULL;
		mesh->blend_indices = iqm_mesh->data->blend_indices ? (uint32_t*)(iqm_mesh->data->blend_indices + iqm_mesh->first_vertex * 4) : NULL;
		mesh->blend_weights = iqm_mesh->data->blend_weights ? (uint32_t*)(iqm_mesh->data->blend_weights + iqm_mesh->first_vertex * 4) : NULL;

		mesh->numindices = (int)(iqm_mesh->num_triangles * 3);
		mesh->numverts = (int)iqm_mesh->num_vertexes;
		mesh->numtris = (int)iqm_mesh->num_triangles;

		// convert the indices from IQM global space to mesh-local space; fix winding order.
		for (unsigned triangle_idx = 0; triangle_idx < iqm_mesh->num_triangles; triangle_idx++)
		{
			int tri[3];
			tri[0] = mesh->indices[triangle_idx * 3 + 0];
			tri[1] = mesh->indices[triangle_idx * 3 + 1];
			tri[2] = mesh->indices[triangle_idx * 3 + 2];

			mesh->indices[triangle_idx * 3 + 0] = tri[2] - (int)iqm_mesh->first_vertex;
			mesh->indices[triangle_idx * 3 + 1] = tri[1] - (int)iqm_mesh->first_vertex;
			mesh->indices[triangle_idx * 3 + 2] = tri[0] - (int)iqm_mesh->first_vertex;
		}

	    char filename[MAX_QPATH];
		Q_snprintf(filename, sizeof(filename), "%s/%s.pcx", base_path, iqm_mesh->material);
		pbr_material_t* mat = MAT_Find(filename, IT_SKIN, IF_NONE);
		assert(mat); // it's either found or created
		
		mesh->materials[0] = mat;
		mesh->numskins = 1; // looks like IQM only supports one skin?
	}

	compute_missing_model_tangents(model);

	extract_model_lights(model);

	Hunk_End(&model->hunk);
	
	return Q_ERR_SUCCESS;

fail:
	return ret;
}

extern model_vbo_t model_vertex_data[];

void MOD_Reference_RTX(model_t *model)
{
	int mesh_idx, skin_idx, frame_idx;

	// register any images used by the models
	switch (model->type) {
	case MOD_ALIAS:
		for (mesh_idx = 0; mesh_idx < model->nummeshes; mesh_idx++) {
			maliasmesh_t *mesh = &model->meshes[mesh_idx];
			for (skin_idx = 0; skin_idx < mesh->numskins; skin_idx++) {
				MAT_UpdateRegistration(mesh->materials[skin_idx]);
			}
		}
		break;
	case MOD_SPRITE:
		for (frame_idx = 0; frame_idx < model->numframes; frame_idx++) {
			model->spriteframes[frame_idx].image->registration_sequence = registration_sequence;
		}
		break;
	case MOD_EMPTY:
		break;
	default:
		Q_assert(!"bad model type");
	}

	model->registration_sequence = registration_sequence;
	model_vertex_data[model - r_models].registration_sequence = registration_sequence;
}

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
