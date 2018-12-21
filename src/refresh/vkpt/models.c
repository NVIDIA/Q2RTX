/*
Copyright (C) 2018 Christoph Schied
Copyright (C) 2018 Florian Simon
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
#include <assert.h>

#if MAX_ALIAS_VERTS > TESS_MAX_VERTICES
#error TESS_MAX_VERTICES
#endif

#if MD2_MAX_TRIANGLES > TESS_MAX_INDICES / 3
#error TESS_MAX_INDICES
#endif

qerror_t MOD_LoadMD2(model_t *model, const void *rawdata, size_t length)
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
	qerror_t        ret;

	if (length < sizeof(header)) {
		return Q_ERR_FILE_TOO_SMALL;
	}

	// byte swap the header
	header = *(dmd2header_t *)rawdata;
	for (int i = 0; i < sizeof(header) / 4; i++) {
		((uint32_t *)&header)[i] = LittleLong(((uint32_t *)&header)[i]);
	}

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

		for (int j = i + 1; j < numindices; j++) {
			if (vertIndices[i] == vertIndices[j] &&
					(src_tc[tcIndices[i]].s == src_tc[tcIndices[j]].s &&
					 src_tc[tcIndices[i]].t == src_tc[tcIndices[j]].t)) {
				// duplicate vertex
				remap[j] = i;
				finalIndices[j] = numverts;
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
	model->meshes = MOD_Malloc(sizeof(maliasmesh_t));
	model->frames = MOD_Malloc(header.num_frames * sizeof(maliasframe_t));

	dst_mesh = model->meshes;
	dst_mesh->numtris    = numindices / 3;
	dst_mesh->numindices = numindices;
	dst_mesh->numverts   = numverts;
	dst_mesh->numskins   = header.num_skins;
	dst_mesh->positions  = MOD_Malloc(numverts   * header.num_frames * sizeof(vec3_t));
	dst_mesh->normals    = MOD_Malloc(numverts   * header.num_frames * sizeof(vec3_t));
	dst_mesh->tex_coords = MOD_Malloc(numverts   * header.num_frames * sizeof(vec2_t));
	dst_mesh->indices    = MOD_Malloc(numindices * sizeof(int));

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
		FS_NormalizePath(skinname, skinname);
		dst_mesh->skins[i] = IMG_Find(skinname, IT_SKIN, IF_NONE);
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

	Hunk_End(&model->hunk);
	return Q_ERR_SUCCESS;

fail:
	Hunk_Free(&model->hunk);
	return ret;
}

#if USE_MD3
static qerror_t MOD_LoadMD3Mesh(model_t *model, maliasmesh_t *mesh,
		const byte *rawdata, size_t length, size_t *offset_p)
{
	dmd3mesh_t      header;
	size_t          end;
	dmd3vertex_t    *src_vert;
	dmd3coord_t     *src_tc;
	dmd3skin_t      *src_skin;
	uint32_t        *src_idx;
	maliasvert_t    *dst_vert;
	maliastc_t      *dst_tc;
	int  *dst_idx;
	uint32_t        index;
	char            skinname[MAX_QPATH];
	int             i;

	if (length < sizeof(header))
		return Q_ERR_BAD_EXTENT;

	// byte swap the header
	header = *(dmd3mesh_t *)rawdata;
	for (i = 0; i < sizeof(header) / 4; i++)
		((uint32_t *)&header)[i] = LittleLong(((uint32_t *)&header)[i]);

	if (header.meshsize < sizeof(header) || header.meshsize > length)
		return Q_ERR_BAD_EXTENT;
	if (header.num_verts < 3)
		return Q_ERR_TOO_FEW;
	if (header.num_verts > TESS_MAX_VERTICES)
		return Q_ERR_TOO_MANY;
	if (header.num_tris < 1)
		return Q_ERR_TOO_FEW;
	if (header.num_tris > TESS_MAX_INDICES / 3)
		return Q_ERR_TOO_MANY;
	if (header.num_skins > MAX_ALIAS_SKINS)
		return Q_ERR_TOO_MANY;
	end = header.ofs_skins + header.num_skins * sizeof(dmd3skin_t);
	if (end < header.ofs_skins || end > length)
		return Q_ERR_BAD_EXTENT;
	end = header.ofs_verts + header.num_verts * model->numframes * sizeof(dmd3vertex_t);
	if (end < header.ofs_verts || end > length)
		return Q_ERR_BAD_EXTENT;
	end = header.ofs_tcs + header.num_verts * sizeof(dmd3coord_t);
	if (end < header.ofs_tcs || end > length)
		return Q_ERR_BAD_EXTENT;
	end = header.ofs_indexes + header.num_tris * 3 * sizeof(uint32_t);
	if (end < header.ofs_indexes || end > length)
		return Q_ERR_BAD_EXTENT;

	mesh->numtris = header.num_tris;
	mesh->numindices = header.num_tris * 3;
	mesh->numverts = header.num_verts;
	mesh->numskins = header.num_skins;
	mesh->verts = MOD_Malloc(sizeof(maliasvert_t) * header.num_verts * model->numframes);
	mesh->tcoords = MOD_Malloc(sizeof(maliastc_t) * header.num_verts);
	mesh->indices = MOD_Malloc(sizeof(int) * header.num_tris * 3);

	// load all skins
	src_skin = (dmd3skin_t *)(rawdata + header.ofs_skins);
	for (i = 0; i < header.num_skins; i++) {
		if (!Q_memccpy(skinname, src_skin->name, 0, sizeof(skinname)))
			return Q_ERR_STRING_TRUNCATED;
		FS_NormalizePath(skinname, skinname);
		mesh->skins[i] = IMG_Find(skinname, IT_SKIN, IF_NONE);
	}

	// load all vertices
	src_vert = (dmd3vertex_t *)(rawdata + header.ofs_verts);
	dst_vert = mesh->verts;
	for (i = 0; i < header.num_verts * model->numframes; i++) {
		dst_vert->pos[0] = (int16_t)LittleShort(src_vert->point[0]);
		dst_vert->pos[1] = (int16_t)LittleShort(src_vert->point[1]);
		dst_vert->pos[2] = (int16_t)LittleShort(src_vert->point[2]);

		dst_vert->norm[0] = src_vert->norm[0];
		dst_vert->norm[1] = src_vert->norm[1];
		assert(!"need to convert from latlong to cartesian");

		src_vert++; dst_vert++;
	}

	// load all texture coords
	src_tc = (dmd3coord_t *)(rawdata + header.ofs_tcs);
	dst_tc = mesh->tcoords;
	for (i = 0; i < header.num_verts; i++) {
		dst_tc->st[0] = LittleFloat(src_tc->st[0]);
		dst_tc->st[1] = LittleFloat(src_tc->st[1]);
		src_tc++; dst_tc++;
	}

	// load all triangle indices
	src_idx = (uint32_t *)(rawdata + header.ofs_indexes);
	dst_idx = mesh->indices;
	for (i = 0; i < header.num_tris * 3; i++) {
		index = LittleLong(*src_idx++);
		if (index >= header.num_verts)
			return Q_ERR_BAD_INDEX;
		*dst_idx++ = index;
	}

	*offset_p = header.meshsize;
	return Q_ERR_SUCCESS;
}

qerror_t MOD_LoadMD3(model_t *model, const void *rawdata, size_t length)
{
	dmd3header_t    header;
	size_t          end, offset, remaining;
	dmd3frame_t     *src_frame;
	maliasframe_t   *dst_frame;
	const byte      *src_mesh;
	int             i;
	qerror_t        ret;

	if (length < sizeof(header))
		return Q_ERR_FILE_TOO_SMALL;

	// byte swap the header
	header = *(dmd3header_t *)rawdata;
	for (i = 0; i < sizeof(header) / 4; i++)
		((uint32_t *)&header)[i] = LittleLong(((uint32_t *)&header)[i]);

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
	if (header.num_meshes < 1)
		return Q_ERR_TOO_FEW;
	if (header.num_meshes > MD3_MAX_MESHES)
		return Q_ERR_TOO_MANY;
	if (header.ofs_meshes > length)
		return Q_ERR_BAD_EXTENT;

	Hunk_Begin(&model->hunk, 0x400000);
	model->type = MOD_ALIAS;
	model->numframes = header.num_frames;
	model->nummeshes = header.num_meshes;
	model->meshes = MOD_Malloc(sizeof(maliasmesh_t) * header.num_meshes);
	model->frames = MOD_Malloc(sizeof(maliasframe_t) * header.num_frames);

	// load all frames
	src_frame = (dmd3frame_t *)((byte *)rawdata + header.ofs_frames);
	dst_frame = model->frames;
	for (i = 0; i < header.num_frames; i++) {
		LittleVector(src_frame->translate, dst_frame->translate);
		VectorSet(dst_frame->scale, MD3_XYZ_SCALE, MD3_XYZ_SCALE, MD3_XYZ_SCALE);

		LittleVector(src_frame->mins, dst_frame->bounds[0]);
		LittleVector(src_frame->maxs, dst_frame->bounds[1]);
		dst_frame->radius = LittleFloat(src_frame->radius);

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

	Hunk_End(&model->hunk);
	return Q_ERR_SUCCESS;

fail:
	Hunk_Free(&model->hunk);
	return ret;
}
#endif

void MOD_Reference(model_t *model)
{
	int i, j;

	// register any images used by the models
	switch (model->type) {
	case MOD_ALIAS:
		for (i = 0; i < model->nummeshes; i++) {
			maliasmesh_t *mesh = &model->meshes[i];
			for (j = 0; j < mesh->numskins; j++) {
				mesh->skins[j]->registration_sequence = registration_sequence;
			}
		}
		break;
	case MOD_SPRITE:
		for (i = 0; i < model->numframes; i++) {
			model->spriteframes[i].image->registration_sequence = registration_sequence;
		}
		break;
	case MOD_EMPTY:
		break;
	default:
		Com_Error(ERR_FATAL, "%s: bad model type", __func__);
	}

	model->registration_sequence = registration_sequence;
}

// vim: shiftwidth=4 noexpandtab tabstop=4 cindent
