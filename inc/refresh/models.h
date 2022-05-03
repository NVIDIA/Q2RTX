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

#ifndef MODELS_H
#define MODELS_H

//
// models.h -- common models manager
//

#include "system/hunk.h"
#include "common/error.h"
#include "images.h"

#define MOD_Malloc(size)    Hunk_TryAlloc(&model->hunk, size)

#define CHECK(x)    if (!(x)) { ret = Q_ERR(ENOMEM); goto fail; }

typedef struct {
    int             width;
    int             height;
    int             origin_x;
    int             origin_y;
    image_t         *image;
} mspriteframe_t;

typedef enum
{
	MCLASS_REGULAR,
	MCLASS_EXPLOSION,
	MCLASS_FLASH,
	MCLASS_SMOKE,
    MCLASS_STATIC_LIGHT,
    MCLASS_FLARE
} model_class_t;

typedef struct
{
	vec3_t translate;
	quat_t rotate;
	vec3_t scale;
} iqm_transform_t;

typedef struct
{
	char name[MAX_QPATH];
	uint32_t first_frame;
	uint32_t num_frames;
	bool loop;
} iqm_anim_t;

// inter-quake-model
typedef struct
{
	uint32_t num_vertexes;
	uint32_t num_triangles;
	uint32_t num_frames;
	uint32_t num_meshes;
	uint32_t num_joints;
	uint32_t num_poses;
	uint32_t num_animations;
	struct iqm_mesh_s* meshes;

	uint32_t* indices;

	// vertex arrays
	float* positions;
	float* texcoords;
	float* normals;
	float* tangents;
	byte* colors;
    byte* blend_indices; // byte4 per vertex
	byte* blend_weights; // byte4 per vertex
	
	char* jointNames;
	int* jointParents;
	float* bindJoints; // [num_joints * 12]
	float* invBindJoints; // [num_joints * 12]
	iqm_transform_t* poses; // [num_frames * num_poses]
	float* bounds;
	
	iqm_anim_t* animations;
} iqm_model_t;

// inter-quake-model mesh
typedef struct iqm_mesh_s
{
	char name[MAX_QPATH];
	char material[MAX_QPATH];
	iqm_model_t* data;
	uint32_t first_vertex, num_vertexes;
	uint32_t first_triangle, num_triangles;
	uint32_t first_influence, num_influences;
} iqm_mesh_t;

typedef struct light_poly_s {
	float positions[9]; // 3x vec3_t
	vec3_t off_center;
	vec3_t color;
	struct pbr_material_s* material;
	int cluster;
	int style;
	float emissive_factor;
	int type;
} light_poly_t;

typedef struct maliasmesh_s maliasmesh_t;
typedef struct maliasframe_s maliasframe_t;

typedef struct {
    enum {
        MOD_FREE,
        MOD_ALIAS,
        MOD_SPRITE,
        MOD_EMPTY
    } type;
    char name[MAX_QPATH];
    int registration_sequence;
    memhunk_t hunk;

    // alias models, sprite models
    int nummeshes;
    int numframes;

    maliasmesh_t *meshes;
    union {
        maliasframe_t *frames;
        mspriteframe_t *spriteframes;
    };

	model_class_t model_class;
	bool sprite_vertical;

	iqm_model_t* iqmData;

	int num_light_polys;
	light_poly_t* light_polys;
} model_t;

extern model_t      r_models[];
extern int          r_numModels;

extern int registration_sequence;

typedef struct entity_s entity_t;

// these are implemented in r_models.c
void MOD_FreeUnused(void);
void MOD_FreeAll(void);
void MOD_Init(void);
void MOD_Shutdown(void);

model_t *MOD_ForHandle(qhandle_t h);
qhandle_t R_RegisterModel(const char *name);

struct dmd2header_s;
struct dmd3mesh_s;
struct dmd3header_s;
const char *MOD_ValidateMD2(const struct dmd2header_s *header, size_t length);
const char *MOD_ValidateMD3Mesh(const model_t *model, const struct dmd3mesh_s *header, size_t length);
const char *MOD_ValidateMD3(const struct dmd3header_s *header, size_t length);

int MOD_LoadIQM_Base(model_t* mod, const void* rawdata, size_t length, const char* mod_name);
bool R_ComputeIQMTransforms(const iqm_model_t* model, const entity_t* entity, float* pose_matrices);

// these are implemented in [gl,sw]_models.c
typedef int (*mod_load_t)(model_t *, const void *, size_t, const char*);
extern int (*MOD_LoadMD2)(model_t *model, const void *rawdata, size_t length, const char* mod_name);
#if USE_MD3
extern int (*MOD_LoadMD3)(model_t *model, const void *rawdata, size_t length, const char* mod_name);
#endif
extern int(*MOD_LoadIQM)(model_t* model, const void* rawdata, size_t length, const char* mod_name);
extern void (*MOD_Reference)(model_t *model);

#endif // MODELS_H
