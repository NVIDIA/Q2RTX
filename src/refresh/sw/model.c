/*
Copyright (C) 1997-2001 Id Software, Inc.

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
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include "sw.h"
#include "format/md2.h"

int     registration_sequence;

/*
=================
ProcessTexinfo
=================
*/
static void ProcessTexinfo(bsp_t *bsp)
{
    mtexinfo_t *tex;
    int i;
    vec_t len1, len2;
    char name[MAX_QPATH];
    imageflags_t flags;

    tex = bsp->texinfo;
    for (i = 0; i < bsp->numtexinfo; i++, tex++) {
        len1 = VectorLength(tex->axis[0]);
        len2 = VectorLength(tex->axis[1]);
        len1 = (len1 + len2) / 2;
        if (len1 < 0.32)
            tex->mipadjust = 4;
        else if (len1 < 0.49)
            tex->mipadjust = 3;
        else if (len1 < 0.99)
            tex->mipadjust = 2;
        else
            tex->mipadjust = 1;

        if (tex->c.flags & (SURF_WARP | SURF_FLOWING))
            flags = IF_TURBULENT;
        else
            flags = IF_NONE;

        Q_concat(name, sizeof(name), "textures/", tex->name, ".wal", NULL);
        FS_NormalizePath(name, name);
        tex->image = IMG_Find(name, IT_WALL, flags);
    }
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
static void CalcSurfaceExtents(mface_t *s)
{
    vec_t   mins[2], maxs[2], val;
    int     i, j;
    msurfedge_t *e;
    mvertex_t   *v;
    mtexinfo_t  *tex;
    int     bmins[2], bmaxs[2];

    mins[0] = mins[1] = 999999;
    maxs[0] = maxs[1] = -999999;

    tex = s->texinfo;
    e = s->firstsurfedge;
    for (i = 0; i < s->numsurfedges; i++, e++) {
        v = e->edge->v[e->vert];
        for (j = 0; j < 2; j++) {
            val = DotProduct(v->point, tex->axis[j]) + tex->offset[j];
            if (val < mins[j])
                mins[j] = val;
            if (val > maxs[j])
                maxs[j] = val;
        }
    }

    for (i = 0; i < 2; i++) {
        bmins[i] = floor(mins[i] / 16);
        bmaxs[i] = ceil(maxs[i] / 16);

        s->texturemins[i] = bmins[i] << 4;
        s->extents[i] = (bmaxs[i] - bmins[i]) << 4;
        if (s->extents[i] < 16) {
            s->extents[i] = 16; // take at least one cache block
        } else if (s->extents[i] > 256) {
            Com_Error(ERR_DROP, "Bad surface extents");
        }
    }
}


/*
=================
ProcessFaces
=================
*/
static void ProcessFaces(bsp_t *bsp)
{
    mface_t     *s;
    int         i, j;

    s = bsp->faces;
    for (i = 0; i < bsp->numfaces; i++, s++) {
        // set the drawing flags
        if (s->texinfo->c.flags & SURF_SKY) {
            continue;
        }
        if (s->texinfo->c.flags & (SURF_WARP | SURF_FLOWING)) {
            s->drawflags |= DSURF_TURB;
            for (j = 0; j < 2; j++) {
                s->extents[j] = 16384;
                s->texturemins[j] = -8192;
            }
            continue;
        }
        CalcSurfaceExtents(s);
    }
}




/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

/*
=================
Mod_LoadAliasModel
=================
*/
qerror_t MOD_LoadMD2(model_t *model, const void *rawdata, size_t length)
{
    dmd2header_t header;
    dmd2frame_t *src_frame;
    //dmd2trivertx_t *src_vert;
    dmd2triangle_t *src_tri;
    dmd2stvert_t *src_st;
    maliasframe_t *dst_frame;
    //maliasvert_t *dst_vert;
    maliastri_t *dst_tri;
    maliasst_t *dst_st;
    char skinname[MAX_QPATH];
    char *src_skin;
    int i, j;
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

    // load triangle indices
    model->tris = MOD_Malloc(header.num_tris * sizeof(maliastri_t));
    model->numtris = header.num_tris;

    src_tri = (dmd2triangle_t *)((byte *)rawdata + header.ofs_tris);
    dst_tri = model->tris;
    for (i = 0; i < header.num_tris; i++, src_tri++, dst_tri++) {
        for (j = 0; j < 3; j++) {
            uint16_t idx_xyz = LittleShort(src_tri->index_xyz[j]);
            uint16_t idx_st = LittleShort(src_tri->index_st[j]);

            if (idx_xyz >= header.num_xyz || idx_st >= header.num_st) {
                ret = Q_ERR_BAD_INDEX;
                goto fail;
            }

            dst_tri->index_xyz[j] = idx_xyz;
            dst_tri->index_st[j] = idx_st;
        }
    }

    // load base s and t vertices
    model->sts = MOD_Malloc(header.num_st * sizeof(maliasst_t));
    model->numsts = header.num_st;
    model->skinwidth = header.skinwidth;
    model->skinheight = header.skinheight;

    src_st = (dmd2stvert_t *)((byte *)rawdata + header.ofs_st);
    dst_st = model->sts;
    for (i = 0; i < header.num_st; i++, src_st++, dst_st++) {
        dst_st->s = (int16_t)LittleShort(src_st->s);
        dst_st->t = (int16_t)LittleShort(src_st->t);

        if (dst_st->s < 0 || dst_st->s >= header.skinwidth) {
            ret = Q_ERR_BAD_INDEX;
            goto fail;
        }

        if (dst_st->t < 0 || dst_st->t >= header.skinheight) {
            ret = Q_ERR_BAD_INDEX;
            goto fail;
        }
    }

    // load the frames
    model->frames = MOD_Malloc(header.num_frames * sizeof(maliasframe_t));
    model->numframes = header.num_frames;
    model->numverts = header.num_xyz;

    src_frame = (dmd2frame_t *)((byte *)rawdata + header.ofs_frames);
    dst_frame = model->frames;
    for (i = 0; i < header.num_frames; i++, dst_frame++) {
        for (j = 0; j < 3; j++) {
            dst_frame->scale[j] = LittleFloat(src_frame->scale[j]);
            dst_frame->translate[j] = LittleFloat(src_frame->translate[j]);
        }

        // verts are all 8 bit, so no swapping needed
        dst_frame->verts = MOD_Malloc(header.num_xyz * sizeof(maliasvert_t));
        memcpy(dst_frame->verts, src_frame->verts, header.num_xyz * sizeof(maliasvert_t));

        // check normal indices
        for (j = 0; j < header.num_xyz; j++) {
            if (dst_frame->verts[j].lightnormalindex > NUMVERTEXNORMALS) {
                ret = Q_ERR_BAD_INDEX;
                goto fail;
            }
        }

        src_frame = (dmd2frame_t *)((byte *)src_frame + header.framesize);
    }

    // register all skins
    src_skin = (char *)rawdata + header.ofs_skins;
    for (i = 0; i < header.num_skins; i++) {
        if (!Q_memccpy(skinname, src_skin, 0, sizeof(skinname))) {
            ret = Q_ERR_STRING_TRUNCATED;
            goto fail;
        }
        FS_NormalizePath(skinname, skinname);
        model->skins[i] = IMG_Find(skinname, IT_SKIN, IF_NONE);
        src_skin += MD2_MAX_SKINNAME;
    }
    model->numskins = header.num_skins;

    Hunk_End(&model->hunk);
    return Q_ERR_SUCCESS;

fail:
    Hunk_Free(&model->hunk);
    return ret;
}

void MOD_Reference(model_t *model)
{
    int     i;

    // register any images used by the models
    switch (model->type) {
    case MOD_ALIAS:
        for (i = 0; i < model->numskins; i++) {
            model->skins[i]->registration_sequence = registration_sequence;
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

/*
@@@@@@@@@@@@@@@@@@@@@
R_BeginRegistration

Specifies the model that will be used as the world
@@@@@@@@@@@@@@@@@@@@@
*/
void R_BeginRegistration(const char *model)
{
    char        fullname[MAX_QPATH];
    bsp_t       *bsp;
    qerror_t    ret;
    int         i;

    registration_sequence++;
    r_oldviewcluster = -1;      // force markleafs

    D_FlushCaches();

    Q_concat(fullname, sizeof(fullname), "maps/", model, ".bsp", NULL);
    ret = BSP_Load(fullname, &bsp);
    if (!bsp) {
        Com_Error(ERR_DROP, "%s: couldn't load %s: %s",
                  __func__, fullname, Q_ErrorString(ret));
    }

    if (bsp == r_worldmodel) {
        for (i = 0; i < bsp->numtexinfo; i++) {
            bsp->texinfo[i].image->registration_sequence = registration_sequence;
        }
        bsp->refcount--;
        return;
    }

    BSP_Free(r_worldmodel);
    r_worldmodel = bsp;

    ProcessTexinfo(bsp);
    ProcessFaces(bsp);

    // TODO
    R_NewMap();
}

/*
@@@@@@@@@@@@@@@@@@@@@
R_EndRegistration

@@@@@@@@@@@@@@@@@@@@@
*/
void R_EndRegistration(void)
{
    MOD_FreeUnused();
    IMG_FreeUnused();
}


