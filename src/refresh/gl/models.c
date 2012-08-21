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

#include "gl.h"
#include "format/md2.h"
#include "format/md3.h"
#include "format/sp2.h"

qerror_t MOD_LoadMD2(model_t *model, const void *rawdata, size_t length)
{
    dmd2header_t header;
    dmd2frame_t *src_frame;
    dmd2trivertx_t *src_vert;
    dmd2triangle_t *src_tri;
    dmd2stvert_t *src_tc;
    maliasframe_t *dst_frame;
    maliasvert_t *dst_vert;
    maliasmesh_t *dst_mesh;
    uint32_t *finalIndices;
    maliastc_t *dst_tc;
    int i, j, k;
    uint16_t remap[MD2_MAX_TRIANGLES * 3];
    uint16_t vertIndices[MD2_MAX_TRIANGLES * 3];
    uint16_t tcIndices[MD2_MAX_TRIANGLES * 3];
    int numverts, numindices;
    char skinname[MAX_QPATH];
    char *src_skin;
    vec_t scaleS, scaleT;
    int val;
    vec3_t mins, maxs;
    qerror_t ret;

    if (length < sizeof(header)) {
        return Q_ERR_FILE_TOO_SMALL;
    }

    // byte swap the header
    header = *(dmd2header_t *)rawdata;
    for (i = 0; i < sizeof(header) / 4; i++) {
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

    Hunk_Begin(&model->hunk, 0x400000);
    model->type = MOD_ALIAS;

    // load all triangle indices
    src_tri = (dmd2triangle_t *)((byte *)rawdata + header.ofs_tris);
    for (i = 0; i < header.num_tris; i++) {
        for (j = 0; j < 3; j++) {
            uint16_t idx_xyz = LittleShort(src_tri->index_xyz[j]);
            uint16_t idx_st = LittleShort(src_tri->index_st[j]);

            if (idx_xyz >= header.num_xyz || idx_st >= header.num_st) {
                ret = Q_ERR_BAD_INDEX;
                goto fail;
            }

            vertIndices[i * 3 + j] = idx_xyz;
            tcIndices[i * 3 + j] = idx_st;
        }
        src_tri++;
    }

    numindices = header.num_tris * 3;

    model->meshes = MOD_Malloc(sizeof(maliasmesh_t));
    model->nummeshes = 1;

    dst_mesh = model->meshes;
    dst_mesh->indices = MOD_Malloc(numindices * sizeof(uint32_t));
    dst_mesh->numtris = header.num_tris;
    dst_mesh->numindices = numindices;

    for (i = 0; i < numindices; i++) {
        remap[i] = 0xFFFF;
    }

    numverts = 0;
    src_tc = (dmd2stvert_t *)((byte *)rawdata + header.ofs_st);
    finalIndices = dst_mesh->indices;
    for (i = 0; i < numindices; i++) {
        if (remap[i] != 0xFFFF) {
            continue; // already remapped
        }

        for (j = i + 1; j < numindices; j++) {
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

    dst_mesh->verts = MOD_Malloc(numverts * header.num_frames * sizeof(maliasvert_t));
    dst_mesh->tcoords = MOD_Malloc(numverts * sizeof(maliastc_t));
    dst_mesh->numverts = numverts;

    // load all skins
    src_skin = (char *)rawdata + header.ofs_skins;
    for (i = 0; i < header.num_skins; i++) {
        if (!Q_memccpy(skinname, src_skin, 0, sizeof(skinname))) {
            ret = Q_ERR_STRING_TRUNCATED;
            goto fail;
        }
        FS_NormalizePath(skinname, skinname);
        dst_mesh->skins[i] = IMG_Find(skinname, IT_SKIN);
        src_skin += MD2_MAX_SKINNAME;
    }
    dst_mesh->numskins = header.num_skins;

    // load all tcoords
    src_tc = (dmd2stvert_t *)((byte *)rawdata + header.ofs_st);
    dst_tc = dst_mesh->tcoords;
    scaleS = 1.0f / header.skinwidth;
    scaleT = 1.0f / header.skinheight;
    for (i = 0; i < numindices; i++) {
        if (remap[i] == i) {
            float s = (int16_t)LittleShort(src_tc[tcIndices[i]].s);
            float t = (int16_t)LittleShort(src_tc[tcIndices[i]].t);

            dst_tc[finalIndices[i]].st[0] = s * scaleS;
            dst_tc[finalIndices[i]].st[1] = t * scaleT;
        }
    }

    // load all frames
    model->frames = MOD_Malloc(header.num_frames * sizeof(maliasframe_t));
    model->numframes = header.num_frames;

    src_frame = (dmd2frame_t *)((byte *)rawdata + header.ofs_frames);
    dst_frame = model->frames;
    for (j = 0; j < header.num_frames; j++) {
        LittleVector(src_frame->scale, dst_frame->scale);
        LittleVector(src_frame->translate, dst_frame->translate);

        // load frame vertices
        ClearBounds(mins, maxs);
        for (i = 0; i < numindices; i++) {
            if (remap[i] == i) {
                src_vert = &src_frame->verts[vertIndices[i]];
                dst_vert = &dst_mesh->verts[j * numverts + finalIndices[i]];

                dst_vert->pos[0] = src_vert->v[0];
                dst_vert->pos[1] = src_vert->v[1];
                dst_vert->pos[2] = src_vert->v[2];
                k = src_vert->lightnormalindex;
                if (k >= NUMVERTEXNORMALS) {
                    ret = Q_ERR_BAD_INDEX;
                    goto fail;
                }
                dst_vert->norm[0] = gl_static.latlngtab[k][0];
                dst_vert->norm[1] = gl_static.latlngtab[k][1];

                for (k = 0; k < 3; k++) {
                    val = dst_vert->pos[k];
                    if (val < mins[k])
                        mins[k] = val;
                    if (val > maxs[k])
                        maxs[k] = val;
                }
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

    Hunk_End(&model->hunk);
    return Q_ERR_SUCCESS;

fail:
    Hunk_Free(&model->hunk);
    return ret;
}

#if USE_MD3
qerror_t MOD_LoadMD3(model_t *model, const void *rawdata, size_t length)
{
    dmd3header_t header;
    uint32_t offset;
    dmd3frame_t *src_frame;
    dmd3mesh_t *src_mesh;
    dmd3vertex_t *src_vert;
    dmd3coord_t *src_tc;
    dmd3skin_t *src_skin;
    uint32_t *src_idx;
    maliasframe_t *dst_frame;
    maliasmesh_t *dst_mesh;
    maliasvert_t *dst_vert;
    maliastc_t *dst_tc;
    uint32_t *dst_idx;
    uint32_t numverts, numtris, numskins;
    uint32_t totalVerts;
    char skinname[MAX_QPATH];
    const byte *rawend;
    int i, j;
    qerror_t ret;

    if (length < sizeof(header)) {
        return Q_ERR_FILE_TOO_SMALL;
    }

    // byte swap the header
    header = *(dmd3header_t *)rawdata;
    for (i = 0; i < sizeof(header) / 4; i++) {
        ((uint32_t *)&header)[i] = LittleLong(((uint32_t *)&header)[i]);
    }

    if (header.ident != MD3_IDENT)
        return Q_ERR_UNKNOWN_FORMAT;
    if (header.version != MD3_VERSION)
        return Q_ERR_UNKNOWN_FORMAT;
    if (header.num_frames < 1)
        return Q_ERR_TOO_FEW;
    if (header.num_frames > MD3_MAX_FRAMES)
        return Q_ERR_TOO_MANY;
    if (header.num_meshes < 1)
        return Q_ERR_TOO_FEW;
    if (header.num_meshes > MD3_MAX_MESHES)
        return Q_ERR_TOO_MANY;

    Hunk_Begin(&model->hunk, 0x400000);
    model->type = MOD_ALIAS;
    model->numframes = header.num_frames;
    model->nummeshes = header.num_meshes;
    model->meshes = MOD_Malloc(sizeof(maliasmesh_t) * header.num_meshes);
    model->frames = MOD_Malloc(sizeof(maliasframe_t) * header.num_frames);

    rawend = (byte *)rawdata + length;

    // load all frames
    src_frame = (dmd3frame_t *)((byte *)rawdata + header.ofs_frames);
    if ((byte *)(src_frame + header.num_frames) > rawend) {
        ret = Q_ERR_BAD_EXTENT;
        goto fail;
    }
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
    src_mesh = (dmd3mesh_t *)((byte *)rawdata + header.ofs_meshes);
    dst_mesh = model->meshes;
    for (i = 0; i < header.num_meshes; i++) {
        if ((byte *)(src_mesh + 1) > rawend) {
            ret = Q_ERR_BAD_EXTENT;
            goto fail;
        }

        numverts = LittleLong(src_mesh->num_verts);
        if (numverts < 3) {
            ret = Q_ERR_TOO_FEW;
            goto fail;
        }
        if (numverts > TESS_MAX_VERTICES) {
            ret = Q_ERR_TOO_MANY;
            goto fail;
        }

        numtris = LittleLong(src_mesh->num_tris);
        if (numtris < 1) {
            ret = Q_ERR_TOO_FEW;
            goto fail;
        }
        if (numtris > TESS_MAX_INDICES / 3) {
            ret = Q_ERR_TOO_MANY;
            goto fail;
        }

        dst_mesh->numtris = numtris;
        dst_mesh->numindices = numtris * 3;
        dst_mesh->numverts = numverts;
        dst_mesh->verts = MOD_Malloc(sizeof(maliasvert_t) * numverts * header.num_frames);
        dst_mesh->tcoords = MOD_Malloc(sizeof(maliastc_t) * numverts);
        dst_mesh->indices = MOD_Malloc(sizeof(uint32_t) * numtris * 3);

        // load all skins
        numskins = LittleLong(src_mesh->num_skins);
        if (numskins > MAX_ALIAS_SKINS) {
            ret = Q_ERR_TOO_MANY;
            goto fail;
        }
        offset = LittleLong(src_mesh->ofs_skins);
        src_skin = (dmd3skin_t *)((byte *)src_mesh + offset);
        if ((byte *)(src_skin + numskins) > rawend) {
            ret = Q_ERR_BAD_EXTENT;
            goto fail;
        }
        for (j = 0; j < numskins; j++) {
            if (!Q_memccpy(skinname, src_skin->name, 0, sizeof(skinname))) {
                ret = Q_ERR_STRING_TRUNCATED;
                goto fail;
            }
            FS_NormalizePath(skinname, skinname);
            dst_mesh->skins[j] = IMG_Find(skinname, IT_SKIN);
        }
        dst_mesh->numskins = numskins;

        // load all vertices
        totalVerts = numverts * header.num_frames;
        offset = LittleLong(src_mesh->ofs_verts);
        src_vert = (dmd3vertex_t *)((byte *)src_mesh + offset);
        if ((byte *)(src_vert + totalVerts) > rawend) {
            ret = Q_ERR_BAD_EXTENT;
            goto fail;
        }
        dst_vert = dst_mesh->verts;
        for (j = 0; j < totalVerts; j++) {
            dst_vert->pos[0] = (int16_t)LittleShort(src_vert->point[0]);
            dst_vert->pos[1] = (int16_t)LittleShort(src_vert->point[1]);
            dst_vert->pos[2] = (int16_t)LittleShort(src_vert->point[2]);

            dst_vert->norm[0] = src_vert->norm[0];
            dst_vert->norm[1] = src_vert->norm[1];

            src_vert++; dst_vert++;
        }

        // load all texture coords
        offset = LittleLong(src_mesh->ofs_tcs);
        src_tc = (dmd3coord_t *)((byte *)src_mesh + offset);
        if ((byte *)(src_tc + numverts) > rawend) {
            ret = Q_ERR_BAD_EXTENT;
            goto fail;
        }
        dst_tc = dst_mesh->tcoords;
        for (j = 0; j < numverts; j++) {
            dst_tc->st[0] = LittleFloat(src_tc->st[0]);
            dst_tc->st[1] = LittleFloat(src_tc->st[1]);
            src_tc++; dst_tc++;
        }

        // load all triangle indices
        offset = LittleLong(src_mesh->ofs_indexes);
        src_idx = (uint32_t *)((byte *)src_mesh + offset);
        if ((byte *)(src_idx + numtris * 3) > rawend) {
            ret = Q_ERR_BAD_EXTENT;
            goto fail;
        }
        dst_idx = dst_mesh->indices;
        for (j = 0; j < numtris; j++) {
            dst_idx[0] = LittleLong(src_idx[0]);
            dst_idx[1] = LittleLong(src_idx[1]);
            dst_idx[2] = LittleLong(src_idx[2]);
            if (dst_idx[0] >= numverts || dst_idx[1] >= numverts || dst_idx[2] >= numverts) {
                ret = Q_ERR_BAD_INDEX;
                goto fail;
            }
            src_idx += 3; dst_idx += 3;
        }

        offset = LittleLong(src_mesh->meshsize);
        src_mesh = (dmd3mesh_t *)((byte *)src_mesh + offset);
        dst_mesh++;
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

