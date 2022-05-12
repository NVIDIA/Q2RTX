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


#include <shared/shared.h>
#include <shared/list.h>
#include <refresh/refresh.h>

#define MAX_PBR_MATERIALS 4096

typedef struct image_s image_t;

//
// PBR material
//

typedef struct pbr_material_s {
	list_t entry;
	char name[MAX_QPATH];
	char filename_base[MAX_QPATH];
	char filename_normals[MAX_QPATH];
	char filename_emissive[MAX_QPATH];
	char filename_mask[MAX_QPATH];
	char source_matfile[MAX_QPATH];
	uint32_t source_line;
	int original_width;
	int original_height;
	image_t * image_base;
	image_t * image_normals;
	image_t * image_emissive;
	image_t * image_mask;
	float bump_scale;
	float roughness_override;
	float metalness_factor;
	float emissive_factor;
	float specular_factor;
	float base_factor;
	uint32_t flags;
	int registration_sequence;
	int num_frames;
	int next_frame;
	bool light_styles;
	bool bsp_radiance;
	float default_radiance;
	imageflags_t image_flags;
	imagetype_t image_type;
	bool synth_emissive;
	int emissive_threshold;
} pbr_material_t;

extern pbr_material_t r_materials[MAX_PBR_MATERIALS];

// clears the material table, adds the mat command
void MAT_Init(void);

// unregisters the mat command
void MAT_Shutdown(void);

// resets all previously loaded wall materials and loads a map-specific material file
void MAT_ChangeMap(const char* map_name);

// finds or loads a material by name, which must have no extension
// all available textures will be initialized in the returned material
pbr_material_t* MAT_Find(const char* name, imagetype_t type, imageflags_t flags);

// registration sequence: update registration sequence of images used by the material
void MAT_UpdateRegistration(pbr_material_t * mat);

// returns a material by index, if it's valid - NULL otherwise
pbr_material_t* MAT_ForIndex(int index);

// update material when a skin is applied
pbr_material_t* MAT_ForSkin(image_t * image_base);

// reset materials textures (call before loading a level)
int MAT_FreeUnused(void);

// replaces the material kind field with the given value
uint32_t MAT_SetKind(uint32_t material, uint32_t kind);

// tests if the material is of a given kind
bool MAT_IsKind(uint32_t material, uint32_t kind);

// synthesize 'emissive' image for a material, if necessary
void MAT_SynthesizeEmissive(pbr_material_t * mat);

// test if the material is one of the trapsnarent kinds (glass, water, ...)
bool MAT_IsTransparent(uint32_t material);

// test if the material has an alpha mask
bool MAT_IsMasked(uint32_t material);

#endif // __MATERIAL_H_
