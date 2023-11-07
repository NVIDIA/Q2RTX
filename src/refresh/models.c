/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2008 Andrey Nazarov
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

#include "shared/shared.h"
#include "shared/list.h"
#include "common/common.h"
#include "common/files.h"
#include "system/hunk.h"
#include "format/md2.h"
#if USE_MD3
#include "format/md3.h"
#endif
#include "format/sp2.h"
#include "format/iqm.h"
#include "refresh/images.h"
#include "refresh/models.h"
#include "../client/client.h"
#include "gl/gl.h"

// during registration it is possible to have more models than could actually
// be referenced during gameplay, because we don't want to free anything until
// we are sure we won't need it.
#define MAX_RMODELS     (MAX_MODELS * 2)

model_t      r_models[MAX_RMODELS];
int          r_numModels;

cvar_t    *cl_testmodel;
cvar_t    *cl_testfps;
cvar_t    *cl_testalpha;
qhandle_t  cl_testmodel_handle = -1;
vec3_t     cl_testmodel_position;

static model_t *MOD_Alloc(void)
{
    model_t *model;
    int i;

    for (i = 0, model = r_models; i < r_numModels; i++, model++) {
        if (!model->type) {
            break;
        }
    }

    if (i == r_numModels) {
        if (r_numModels == MAX_RMODELS) {
            return NULL;
        }
        r_numModels++;
    }

    return model;
}

static model_t *MOD_Find(const char *name)
{
    model_t *model;
    int i;

    for (i = 0, model = r_models; i < r_numModels; i++, model++) {
        if (!model->type) {
            continue;
        }
        if (!FS_pathcmp(model->name, name)) {
            return model;
        }
    }

    return NULL;
}

static void MOD_List_f(void)
{
    static const char types[4] = "FASE";
    int     i, count;
    model_t *model;
    size_t  bytes;

    Com_Printf("------------------\n");
    bytes = count = 0;

    for (i = 0, model = r_models; i < r_numModels; i++, model++) {
        if (!model->type) {
            continue;
        }
        Com_Printf("%c %8zu : %s\n", types[model->type],
                   model->hunk.mapped, model->name);
        bytes += model->hunk.mapped;
        count++;
    }
    Com_Printf("Total models: %d (out of %d slots)\n", count, r_numModels);
    Com_Printf("Total resident: %zu\n", bytes);
}

void MOD_FreeUnused(void)
{
    model_t *model;
    int i;

    for (i = 0, model = r_models; i < r_numModels; i++, model++) {
        if (!model->type) {
            continue;
        }
        if (model->registration_sequence == registration_sequence) {
            // make sure it is paged in
            Com_PageInMemory(model->hunk.base, model->hunk.cursize);
        } else {
            // don't need this model
            Hunk_Free(&model->hunk);
            memset(model, 0, sizeof(*model));
        }
    }
}

void MOD_FreeAll(void)
{
    model_t *model;
    int i;

    for (i = 0, model = r_models; i < r_numModels; i++, model++) {
        if (!model->type) {
            continue;
        }

        Hunk_Free(&model->hunk);
        memset(model, 0, sizeof(*model));
    }

    r_numModels = 0;
}

int MOD_ValidateMD2(dmd2header_t *header, size_t length)
{
    size_t end;

    // check ident and version
    if (header->ident != MD2_IDENT)
        return Q_ERR_UNKNOWN_FORMAT;
    if (header->version != MD2_VERSION)
        return Q_ERR_UNKNOWN_FORMAT;

    // check triangles
    if (header->num_tris < 1)
        return Q_ERR_TOO_FEW;
    if (header->num_tris > TESS_MAX_INDICES / 3)
        return Q_ERR_TOO_MANY;

    end = header->ofs_tris + sizeof(dmd2triangle_t) * header->num_tris;
    if (header->ofs_tris < sizeof(*header) || end < header->ofs_tris || end > length)
        return Q_ERR_BAD_EXTENT;
    if (header->ofs_tris % q_alignof(dmd2triangle_t))
        return Q_ERR_BAD_ALIGN;

    // check st
    if (header->num_st < 3)
        return Q_ERR_TOO_FEW;
    if (header->num_st > INT_MAX / sizeof(dmd2stvert_t))
        return Q_ERR_TOO_MANY;

    end = header->ofs_st + sizeof(dmd2stvert_t) * header->num_st;
    if (header->ofs_st < sizeof(*header) || end < header->ofs_st || end > length)
        return Q_ERR_BAD_EXTENT;
    if (header->ofs_st % q_alignof(dmd2stvert_t))
        return Q_ERR_BAD_ALIGN;

    // check xyz and frames
    if (header->num_xyz < 3)
        return Q_ERR_TOO_FEW;
    if (header->num_xyz > MD2_MAX_VERTS)
        return Q_ERR_TOO_MANY;
    if (header->num_frames < 1)
        return Q_ERR_TOO_FEW;
    if (header->num_frames > MD2_MAX_FRAMES)
        return Q_ERR_TOO_MANY;

    end = sizeof(dmd2frame_t) + (header->num_xyz - 1) * sizeof(dmd2trivertx_t);
    if (header->framesize < end || header->framesize > MD2_MAX_FRAMESIZE)
        return Q_ERR_BAD_EXTENT;
    if (header->framesize % q_alignof(dmd2frame_t))
        return Q_ERR_BAD_ALIGN;

    end = header->ofs_frames + (size_t)header->framesize * header->num_frames;
    if (header->ofs_frames < sizeof(*header) || end < header->ofs_frames || end > length)
        return Q_ERR_BAD_EXTENT;
    if (header->ofs_frames % q_alignof(dmd2frame_t))
        return Q_ERR_BAD_ALIGN;

    // check skins
    if (header->num_skins) {
        if (header->num_skins > MD2_MAX_SKINS)
            return Q_ERR_TOO_MANY;

        end = header->ofs_skins + (size_t)MD2_MAX_SKINNAME * header->num_skins;
        if (header->ofs_skins < sizeof(*header) || end < header->ofs_skins || end > length)
            return Q_ERR_BAD_EXTENT;
    }

    if (header->skinwidth < 1 || header->skinwidth > MD2_MAX_SKINWIDTH)
        return Q_ERR_INVALID_FORMAT;
    if (header->skinheight < 1 || header->skinheight > MD2_MAX_SKINHEIGHT)
        return Q_ERR_INVALID_FORMAT;

    return Q_ERR_SUCCESS;
}

static model_class_t
get_model_class(const char *name)
{
	if (!strcmp(name, "models/objects/explode/tris.md2"))
		return MCLASS_EXPLOSION;
	else if (!strcmp(name, "models/objects/r_explode/tris.md2"))
		return MCLASS_EXPLOSION;
	else if (!strcmp(name, "models/objects/flash/tris.md2"))
		return MCLASS_FLASH;
	else if (!strcmp(name, "models/objects/smoke/tris.md2"))
		return MCLASS_SMOKE;
	else if (!strcmp(name, "models/objects/minelite/light2/tris.md2"))
        return MCLASS_STATIC_LIGHT;
    else if (!strcmp(name, "models/objects/flare/tris.md2"))
        return MCLASS_FLARE;
	else
		return MCLASS_REGULAR;
}

static int MOD_LoadSP2(model_t *model, const void *rawdata, size_t length, const char* mod_name)
{
    dsp2header_t header;
    dsp2frame_t *src_frame;
    mspriteframe_t *dst_frame;
    char buffer[SP2_MAX_FRAMENAME];
    int i, ret;

    if (length < sizeof(header))
        return Q_ERR_FILE_TOO_SMALL;

    // byte swap the header
    LittleBlock(&header, rawdata, sizeof(header));

    if (header.ident != SP2_IDENT)
        return Q_ERR_UNKNOWN_FORMAT;
    if (header.version != SP2_VERSION)
        return Q_ERR_UNKNOWN_FORMAT;
    if (header.numframes < 1) {
        // empty models draw nothing
        model->type = MOD_EMPTY;
        return Q_ERR_SUCCESS;
    }
    if (header.numframes > SP2_MAX_FRAMES)
        return Q_ERR_TOO_MANY;
    if (sizeof(dsp2header_t) + sizeof(dsp2frame_t) * header.numframes > length)
        return Q_ERR_BAD_EXTENT;

    Hunk_Begin(&model->hunk, sizeof(mspriteframe_t) * header.numframes);
    model->type = MOD_SPRITE;

    CHECK(model->spriteframes = MOD_Malloc(sizeof(mspriteframe_t) * header.numframes));
    model->numframes = header.numframes;

    src_frame = (dsp2frame_t *)((byte *)rawdata + sizeof(dsp2header_t));
    dst_frame = model->spriteframes;
    for (i = 0; i < header.numframes; i++) {
        dst_frame->width = (int32_t)LittleLong(src_frame->width);
        dst_frame->height = (int32_t)LittleLong(src_frame->height);

        dst_frame->origin_x = (int32_t)LittleLong(src_frame->origin_x);
        dst_frame->origin_y = (int32_t)LittleLong(src_frame->origin_y);

        if (!Q_memccpy(buffer, src_frame->name, 0, sizeof(buffer))) {
            Com_WPrintf("%s has bad frame name\n", model->name);
            dst_frame->image = R_NOTEXTURE;
        } else {
            FS_NormalizePath(buffer);
            dst_frame->image = IMG_Find(buffer, IT_SPRITE, IF_SRGB);
        }

        src_frame++;
        dst_frame++;
    }

    Hunk_End(&model->hunk);

    return Q_ERR_SUCCESS;

fail:
    return ret;
}

#define TRY_MODEL_SRC_GAME      1
#define TRY_MODEL_SRC_BASE      0

qhandle_t R_RegisterModel(const char *name)
{
    char normalized[MAX_QPATH];
    qhandle_t index;
    size_t namelen;
    int filelen = 0;
    model_t *model;
    byte *rawdata = NULL;
    uint32_t ident;
    mod_load_t load;
    int ret;

    // empty names are legal, silently ignore them
    if (!*name)
        return 0;

    if (*name == '*') {
        // inline bsp model
        index = atoi(name + 1);
        return ~index;
    }

    // normalize the path
    namelen = FS_NormalizePathBuffer(normalized, name, MAX_QPATH);

    // this should never happen
    if (namelen >= MAX_QPATH)
        Com_Error(ERR_DROP, "%s: oversize name", __func__);

    // normalized to empty name?
    if (namelen == 0) {
        Com_DPrintf("%s: empty name\n", __func__);
        return 0;
    }

    // see if it's already loaded
    model = MOD_Find(normalized);
    if (model) {
        MOD_Reference(model);
        goto done;
    }

    // Always prefer models from the game dir, even if format might be 'inferior'
    for (int try_location = Q_stricmp(fs_game->string, BASEGAME) ? TRY_MODEL_SRC_GAME : TRY_MODEL_SRC_BASE;
         try_location >= TRY_MODEL_SRC_BASE;
         try_location--)
    {
        int fs_flags = 0;
        if (try_location > 0)
            fs_flags = try_location == TRY_MODEL_SRC_GAME ? FS_PATH_GAME : FS_PATH_BASE;

        char* extension = normalized + namelen - 4;
        bool try_md3 = cls.ref_type == REF_TYPE_VKPT || (cls.ref_type == REF_TYPE_GL && gl_use_hd_assets->integer);
        if (namelen > 4 && (strcmp(extension, ".md2") == 0) && try_md3)
        {
            memcpy(extension, ".md3", 4);

            filelen = FS_LoadFileFlags(normalized, (void **)&rawdata, fs_flags);

            memcpy(extension, ".md2", 4);
        }
        if (!rawdata)
        {
            filelen = FS_LoadFileFlags(normalized, (void **)&rawdata, fs_flags);
        }
        if (rawdata)
            break;
    }

	if (!rawdata)
	{
		filelen = FS_LoadFile(normalized, (void **)&rawdata);
		if (!rawdata) {
			// don't spam about missing models
			if (filelen == Q_ERR(ENOENT)) {
				return 0;
			}

			ret = filelen;
			goto fail1;
		}
	}

    if (filelen < 4) {
        ret = Q_ERR_FILE_TOO_SMALL;
        goto fail2;
    }

    // check ident
    ident = LittleLong(*(uint32_t *)rawdata);
    switch (ident) {
    case MD2_IDENT:
        load = MOD_LoadMD2;
        break;
#if USE_MD3
    case MD3_IDENT:
        load = MOD_LoadMD3;
        break;
#endif
    case SP2_IDENT:
        load = MOD_LoadSP2;
        break;
    case IQM_IDENT:
        load = MOD_LoadIQM;
        break;
    default:
        ret = Q_ERR_UNKNOWN_FORMAT;
        goto fail2;
    }

    if (!load)
    {
        ret = Q_ERR_UNKNOWN_FORMAT;
        goto fail2;
    }

    model = MOD_Alloc();
    if (!model) {
        ret = Q_ERR_OUT_OF_SLOTS;
        goto fail2;
    }

    memcpy(model->name, normalized, namelen + 1);
    model->registration_sequence = registration_sequence;

    ret = load(model, rawdata, filelen, name);

    FS_FreeFile(rawdata);

    if (ret) {
        memset(model, 0, sizeof(*model));
        goto fail1;
    }

	model->model_class = get_model_class(model->name);

done:
    index = (model - r_models) + 1;
    return index;

fail2:
    FS_FreeFile(rawdata);
fail1:
    Com_EPrintf("Couldn't load %s: %s\n", normalized, Q_ErrorString(ret));
    return 0;
}

model_t *MOD_ForHandle(qhandle_t h)
{
    model_t *model;

    if (!h) {
        return NULL;
    }

    Q_assert(h > 0 && h <= r_numModels);
    model = &r_models[h - 1];
    if (!model->type) {
        return NULL;
    }

    return model;
}

static void MOD_PutTest_f(void)
{
    VectorCopy(cl.refdef.vieworg, cl_testmodel_position);
    cl_testmodel_position[2] -= 46.12f; // player eye-level
}

void MOD_Init(void)
{
    Q_assert(!r_numModels);
    Cmd_AddCommand("modellist", MOD_List_f);
    Cmd_AddCommand("puttest", MOD_PutTest_f);

    // Path to the test model - can be an .md2, .md3 or .iqm file
    cl_testmodel = Cvar_Get("cl_testmodel", "", 0);

    // Test model animation frames per second, can be adjusted at runtime
    cl_testfps = Cvar_Get("cl_testfps", "10", 0);

    // Test model alpha, 0-1
    cl_testalpha = Cvar_Get("cl_testalpha", "1", 0);
}

void MOD_Shutdown(void)
{
    MOD_FreeAll();
    Cmd_RemoveCommand("modellist");
    Cmd_RemoveCommand("puttest");
}

