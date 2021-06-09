/*
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

#ifndef __MATERIAL_H_
#define __MATERIAL_H_

#include "shared/shared.h"
#include "refresh/refresh.h"

#define MAX_PBR_MATERIALS 4096

typedef struct image_s image_t;

//
// PBR material
//

typedef struct pbr_material_s {
	char name[MAX_QPATH];
	image_t * image_diffuse;
	image_t * image_normals;
	image_t * image_emissive;
	float bump_scale;
	float rough_override;
	float specular_scale;
	float emissive_scale;
	uint32_t flags;
	int registration_sequence;
	int num_frames;
	int next_frame;
	int enable_light_styles;
} pbr_material_t;

// returns index of given material in table
int MAT_GetPBRMaterialIndex(pbr_material_t const * mat);

// Clone a material for use on a surface with LIGHT flag
pbr_material_t *MAT_CloneForRadiance(pbr_material_t *mat, int radiance);

// registration sequence : set material PBR textures
qerror_t MAT_RegisterPBRMaterial(pbr_material_t * mat, image_t * image_diffuse, image_t * image_normals, image_t * image_emissive);

// registration sequence : update registration sequence of images used by the material
void MAT_UpdateRegistration(pbr_material_t * mat);

// update material when a skin is applied
pbr_material_t const * MAT_UpdatePBRMaterialSkin(image_t * image_diffuse);

qerror_t MAT_SetPBRMaterialAttribute(pbr_material_t * mat, char const * token, char const * value);

//
// materials table
//

// initialize materials table (call on client init)
qerror_t MAT_InitializePBRmaterials();

// reset materials textures (call before loading a level)
qerror_t MAT_ResetUnused();

// returns pointer to materials in table
pbr_material_t const * MAT_GetPBRMaterialsTable();

// returns number of materials in table
int MAT_GetNumPBRMaterials();

// find material in table by index (index < num materials)
pbr_material_t * MAT_GetPBRMaterial(int index);

// find a material by key (ex. textures/e1u1/ceil1_1.wal)
pbr_material_t * MAT_FindPBRMaterial(char const * name);

// reload materials CSV file while the game is running
qerror_t MAT_ReloadPBRMaterials();

// save materials CSV file (in edit mode)
qerror_t MAT_SavePBRMaterials();

// prints material properties on console
void MAT_PrintMaterialProperties(pbr_material_t const * mat);

// replaces the material kind field with the given value
uint32_t MAT_SetKind(uint32_t material, uint32_t kind);

// tests if the material is of a given kind
qboolean MAT_IsKind(uint32_t material, uint32_t kind);

// tests if the material is "custom" (not in materials.csv)
qboolean MAT_IsCustom(uint32_t material);

#endif // __MATERIAL_H_
