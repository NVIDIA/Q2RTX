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

#include "material.h"
#include "vkpt.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>


pbr_material_t r_materials[MAX_PBR_MATERIALS];
pbr_material_t r_global_materials[MAX_PBR_MATERIALS];
pbr_material_t r_map_materials[MAX_PBR_MATERIALS];
uint32_t num_global_materials = 0;
uint32_t num_map_materials = 0;

static uint32_t load_material_file(const char* file_name, pbr_material_t* dest, uint32_t max_items);

void MAT_Init()
{
	memset(r_materials, 0, sizeof(r_materials));
	memset(r_global_materials, 0, sizeof(r_global_materials));
	memset(r_map_materials, 0, sizeof(r_map_materials));
	num_global_materials = 0;
	num_map_materials = 0;

	// find all *.mat files in the root
	int num_files;
	void** list = FS_ListFiles("", ".mat", 0, &num_files);
	
	// read the files in reverse alpha order to make later files override earlier ones
	for (int i = num_files - 1; i >= 0; i--) {
		char* file_name = list[i];
		
		int mat_slots_available = MAX_PBR_MATERIALS - num_global_materials;
		if (mat_slots_available > 0) {
			uint32_t count = load_material_file(file_name, r_global_materials + num_global_materials,
				mat_slots_available);
			num_global_materials += count;
			
			Com_Printf("Loaded %d materials from %s\n", count, file_name);
		}
		else {
			Com_WPrintf("Coundn't load materials from %s: no free slots.\n", file_name);
		}
		
		Z_Free(file_name);
	}
	Z_Free(list);
}

static void MAT_SetIndex(pbr_material_t* mat)
{
	uint32_t mat_index = (uint32_t)(mat - r_materials);
	mat->flags = (mat->flags & ~MATERIAL_INDEX_MASK) | (mat_index & MATERIAL_INDEX_MASK);
	mat->next_frame = mat_index;
}

static void MAT_Reset(pbr_material_t * mat)
{
	memset(mat, 0, sizeof(pbr_material_t));
	mat->bump_scale = 1.0f;
	mat->rough_override = -1.0f;
	mat->metalness_factor = 1.f;
	mat->emissive_factor = 1.f;
	mat->flags = MATERIAL_KIND_REGULAR;
	mat->num_frames = 1;
}

//
// material kinds (translations between names & bit flags)
//

static struct MaterialKind {
	const char * name;
	uint32_t flag;
} materialKinds[] = {
	{"INVALID", MATERIAL_KIND_INVALID},
	{"REGULAR", MATERIAL_KIND_REGULAR},
	{"CHROME", MATERIAL_KIND_CHROME},
	{"GLASS", MATERIAL_KIND_GLASS},
	{"WATER", MATERIAL_KIND_WATER},
	{"LAVA", MATERIAL_KIND_LAVA},
	{"SKY", MATERIAL_KIND_SKY},
	{"SLIME", MATERIAL_KIND_SLIME},
	{"INVISIBLE", MATERIAL_KIND_INVISIBLE},
	{"SCREEN", MATERIAL_KIND_SCREEN},
	{"CAMERA", MATERIAL_KIND_CAMERA},
};

static int nMaterialKinds = sizeof(materialKinds) / sizeof(struct MaterialKind);

static uint32_t getMaterialKind(const char * kindname)
{
	for (int i = 0; i < nMaterialKinds; ++i)
		if (Q_stricmp(kindname, materialKinds[i].name) == 0)
			return materialKinds[i].flag;
	return MATERIAL_KIND_REGULAR;
}

static const char * getMaterialKindName(uint32_t flag)
{
	for (int i = 0; i < nMaterialKinds; ++i)
		if ((flag & MATERIAL_KIND_MASK) == materialKinds[i].flag)
			return materialKinds[i].name;
	return NULL;
}

static size_t truncate_extension(char const* src, char dest[MAX_QPATH])
{
	size_t len = strlen(src);
	assert(len < MAX_QPATH);

	if (len > 4 && src[len - 4] == '.')
		len -= 4;
	
	Q_strlcpy(dest, src, len + 1);
	
	return len;
}

static pbr_material_t* allocate_material()
{
	for (uint32_t i = 0; i < MAX_PBR_MATERIALS; i++)
	{
		pbr_material_t* mat = r_materials + i;
		if (!mat->registration_sequence)
			return mat;
	}

	Com_Error(ERR_FATAL, "Couldn't allocate a new material: insufficient r_materials slots.\n"
		"Increase MAX_PBR_MATERIALS and rebuild the engine.\n");
	return NULL;
}

static pbr_material_t* find_material(const char* name, pbr_material_t* first, uint32_t count)
{
	// TODO: optimize this, probably with a hash table like r_imageHash
	for (uint32_t i = 0; i < count; i++)
	{
		pbr_material_t* mat = first + i;
		if (!mat->registration_sequence)
			continue;

		if (!Q_stricmp(name, mat->name))
			return mat;
	}

	return NULL;
}

static qerror_t set_material_attribute(pbr_material_t* mat, const char* attribute, const char* value,
	const char* sourceFile, uint32_t lineno)
{
	assert(mat);

	// valid attribute-value pairs

	enum TokenType { TOKEN_BOOL, TOKEN_FLOAT, TOKEN_STRING };

	static struct Token {
		int index;
		const char* name;
		enum TokenType type;
	} tokens[] = {
		{0, "bump_scale", TOKEN_FLOAT},
		{1, "roughness_override", TOKEN_FLOAT},
		{2, "metalness_factor", TOKEN_FLOAT},
		{3, "emissive_factor", TOKEN_FLOAT},
		{4, "kind", TOKEN_STRING},
		{5, "is_light", TOKEN_BOOL},
		{6, "correct_albedo", TOKEN_BOOL},
		{7, "texture_base", TOKEN_STRING },
		{8, "texture_normals", TOKEN_STRING },
		{9, "texture_emissive", TOKEN_STRING },
	};

	static int ntokens = sizeof(tokens) / sizeof(struct Token);

	if (attribute == NULL || value == NULL)
		goto usage;

	struct Token const* t = NULL;
	for (int i = 0; i < ntokens; ++i)
	{
		if (strcmp(attribute, tokens[i].name) == 0)
			t = &tokens[i];
	}
	
	if (!t)
	{
		if (sourceFile)
			Com_EPrintf("%s:%d: unknown material attribute '%s'\n", attribute);
		else
			Com_EPrintf("Unknown material attribute '%s'\n", attribute);
		goto usage;
	}

	float fvalue = 0.f; qboolean bvalue = qfalse;
	switch (t->type)
	{
	case TOKEN_BOOL:   bvalue = atoi(value) == 0 ? qfalse : qtrue; break;
	case TOKEN_FLOAT:  fvalue = (float)atof(value); break;
	case TOKEN_STRING: break;
	default:
		assert(!"unknown PBR MAT attribute attribute type");
	}

	// set material

	switch (t->index)
	{
	case 0: mat->bump_scale = fvalue; break;
	case 1: mat->rough_override = fvalue; break;
	case 2: mat->metalness_factor = fvalue; break;
	case 3: mat->emissive_factor = fvalue; break;
	case 4: {
		uint32_t kind = getMaterialKind(value);
		if (kind != 0)
			mat->flags = MAT_SetKind(mat->flags, kind);
		else
		{
			if (sourceFile)
				Com_EPrintf("%s:%d: unknown material kind '%s'\n", sourceFile, lineno, value);
			else
				Com_EPrintf("Unknown material kind '%s'\n", value);
			return Q_ERR_FAILURE;
		}
	} break;
	case 5:
		mat->flags = bvalue == qtrue ? mat->flags | MATERIAL_FLAG_LIGHT : mat->flags & ~(MATERIAL_FLAG_LIGHT);
		break;
	case 6:
		mat->flags = bvalue == qtrue ? mat->flags | MATERIAL_FLAG_CORRECT_ALBEDO : mat->flags & ~(MATERIAL_FLAG_CORRECT_ALBEDO);
		break;
	case 7:
		Q_strlcpy(mat->filename_base, value, sizeof(mat->filename_base));
		break;
	case 8:
		Q_strlcpy(mat->filename_normals, value, sizeof(mat->filename_normals));
		break;
	case 9:
		Q_strlcpy(mat->filename_emissive, value, sizeof(mat->filename_emissive));
		break;
	default:
		assert(!"unknown PBR MAT attribute index");
	}

	return Q_ERR_SUCCESS;

usage:
	if (sourceFile)
		return Q_ERR_FAILURE;

	Com_Printf("Usage : set_material <attribute> <value>\n");
	for (int i = 0; i < ntokens; ++i)
	{
		struct Token const* t = &tokens[i];

		const char* typename = "(undefined)";
		switch (t->type)
		{
		case TOKEN_BOOL: typename = "bool [0,1]"; break;
		case TOKEN_FLOAT: typename = "float"; break;
		case TOKEN_STRING: typename = "string"; break;
		}
		Com_Printf("  %s (%s)\n", t->name, typename);
	}
	return Q_ERR_FAILURE;
}

static uint32_t load_material_file(const char* file_name, pbr_material_t* dest, uint32_t max_items)
{
	assert(max_items >= 1);
	
	char* filebuf = NULL;
	unsigned source = IF_SRC_GAME;

	if (fs_game->string[0] && strcmp(fs_game->string, BASEGAME) != 0) {
		// try the game specific path first
		FS_LoadFileEx(file_name, (void**)&filebuf, FS_PATH_GAME, TAG_FILESYSTEM);
	}

	if (!filebuf) {
		// game specific path not found, or we're playing baseq2
		source = IF_SRC_BASE;
		FS_LoadFileEx(file_name, (void**)&filebuf, FS_PATH_BASE, TAG_FILESYSTEM);
	}
	
	if (!filebuf)
		return 0;

	const char* ptr = filebuf;
	char linebuf[1024];
	uint32_t count = 0;
	uint32_t lineno = 0;
	
	const char* delimiters = " \t\r\n";

	while (sgets(linebuf, sizeof(linebuf), &ptr))
	{
		++lineno;
		
		{ char* t = strchr(linebuf, '#'); if (t) *t = 0; }   // remove comments
		
		size_t len = strlen(linebuf);

		// remove trailing whitespace
		while (len > 0 && strchr(delimiters, linebuf[len - 1]))
			--len;
		linebuf[len] = 0;

		if (len == 0)
			continue;


		// if the line ends with a colon (:) it's a new material section
		if (linebuf[len - 1] == ':')
		{
			if (count > 0)
			{
				++dest;

				if (count > max_items)
				{
					Com_WPrintf("%s:%d: too many materials, expected up to %d.\n", file_name, lineno, max_items);
					Z_Free(filebuf);
					return count;
				}
				
				MAT_Reset(dest);
			}
			++count;

			// copy the material name but not the colon
			linebuf[len - 1] = 0;
			Q_strlcpy(dest->name, linebuf, sizeof(dest->name));
			dest->image_flags = source;
			dest->registration_sequence = registration_sequence;

			continue;
		}

		char* key = strtok(linebuf, delimiters);
		char* value = strtok(NULL, delimiters);
		char* extra = strtok(NULL, delimiters);

		if (!key || !value)
		{
			Com_WPrintf("%s:%d: expected key and value\n", file_name, lineno);
			continue;
		}

		if (extra)
		{
			Com_WPrintf("%s:%d: unexpected extra characters after the key and value\n", file_name, lineno);
			continue;
		}

		set_material_attribute(dest, key, value, file_name, lineno);
	}

	Z_Free(filebuf);
	return count;
}

void MAT_ChangeMap(const char* map_name)
{
	// clear the old map-specific materials
	uint32_t old_map_materails = num_map_materials;
	if (num_map_materials > 0) {
		memset(r_map_materials, 0, sizeof(pbr_material_t) * num_map_materials);
	}

	// load the new materials
	char map_name_no_ext[MAX_QPATH];
	truncate_extension(map_name, map_name_no_ext);
	char file_name[MAX_QPATH];
	Q_snprintf(file_name, sizeof(file_name), "%s.mat", map_name_no_ext);
	num_map_materials = load_material_file(file_name, r_map_materials, MAX_PBR_MATERIALS);
	if (num_map_materials > 0) {	
		Com_Printf("Loaded %d materials from %s\n", num_map_materials, file_name);
	}

	// if there are any overrides now or there were some overrides before,
	// unload all wall materials to re-initialize them with the overrides
	if (old_map_materails > 0 || num_map_materials > 0)
	{
		for (uint32_t i = 0; i < MAX_PBR_MATERIALS; i++)
		{
			pbr_material_t* mat = r_materials + i;

			if (mat->registration_sequence && mat->image_type == IT_WALL)
				MAT_Reset(mat);
		}
	}
}

pbr_material_t* MAT_Find(const char* name, imagetype_t type, imageflags_t flags)
{
	char mat_name_no_ext[MAX_QPATH];
	truncate_extension(name, mat_name_no_ext);
	
	pbr_material_t* mat = find_material(mat_name_no_ext, r_materials, MAX_PBR_MATERIALS);
	
	if (mat)
	{
		MAT_UpdateRegistration(mat);
		return mat;
	}

	mat = allocate_material();

	pbr_material_t* matdef = find_material(mat_name_no_ext, r_global_materials, num_global_materials);
	
	if (type == IT_WALL)
	{
		pbr_material_t* map_mat = find_material(mat_name_no_ext, r_map_materials, num_map_materials);

		if (map_mat)
			matdef = map_mat;
	}

	if (matdef)
	{
		memcpy(mat, matdef, sizeof(pbr_material_t));
		uint32_t index = (uint32_t)(mat - r_materials);
		mat->flags = (mat->flags & ~MATERIAL_INDEX_MASK) | index;
		mat->next_frame = index;
		
		
		if (mat->filename_base[0]) {
			mat->image_base = IMG_Find(mat->filename_base, type, flags | IF_SRGB | (mat->image_flags & IF_SRC_MASK));
			if (mat->image_base == R_NOTEXTURE) {
				Com_WPrintf("Texture '%s' specified in material '%s' could not be found. Using the low-res texture.\n", mat->filename_base, mat_name_no_ext);
				
				mat->image_base = IMG_Find(name, type, flags | IF_SRGB);
				if (mat->image_base == R_NOTEXTURE) {
					mat->image_base = NULL;
				}
			}
			else
			{
				IMG_GetDimensions(name, &mat->image_base->width, &mat->image_base->height);
			}
		}

		if (mat->filename_normals[0]) {
			mat->image_normals = IMG_Find(mat->filename_normals, type, flags | (mat->image_flags & IF_SRC_MASK));
			if (mat->image_normals == R_NOTEXTURE) {
				Com_WPrintf("Texture '%s' specified in material '%s' could not be found.\n", mat->filename_normals, mat_name_no_ext);
				mat->image_normals = NULL;
			}
		}
		
		if (mat->filename_emissive[0]) {
			mat->image_emissive = IMG_Find(mat->filename_emissive, type, flags | IF_SRGB | (mat->image_flags & IF_SRC_MASK));
			if (mat->image_emissive == R_NOTEXTURE) {
				Com_WPrintf("Texture '%s' specified in material '%s' could not be found.\n", mat->filename_emissive, mat_name_no_ext);
				mat->image_emissive = NULL;
			}
		}
	}
	else
	{
		Q_strlcpy(mat->name, mat_name_no_ext, sizeof(mat->name));
		
		mat->image_base = IMG_Find(name, type, flags | IF_SRGB);
		if (mat->image_base == R_NOTEXTURE)
			mat->image_base = NULL;
		else
			Q_strlcpy(mat->filename_base, mat->image_base->filepath, sizeof(mat->filename_base));

		char file_name[MAX_QPATH];
		
		Q_snprintf(file_name, sizeof(file_name), "%s_n.tga", mat_name_no_ext);
		mat->image_normals = IMG_Find(file_name, type, flags);
		if (mat->image_normals == R_NOTEXTURE)
			mat->image_normals = NULL;
		else
			Q_strlcpy(mat->filename_normals, mat->image_normals->filepath, sizeof(mat->filename_normals));

		Q_snprintf(file_name, sizeof(file_name), "%s_light.tga", mat_name_no_ext);
		mat->image_emissive = IMG_Find(file_name, type, flags | IF_SRGB);
		if (mat->image_emissive == R_NOTEXTURE)
			mat->image_emissive = NULL;
		else
			Q_strlcpy(mat->filename_emissive, mat->image_emissive->filepath, sizeof(mat->filename_emissive));
	}

	if (mat->image_normals && !mat->image_normals->processing_complete)
		vkpt_normalize_normal_map(mat->image_normals);

	if (mat->image_emissive && !mat->image_emissive->processing_complete)
		vkpt_extract_emissive_texture_info(mat->image_emissive);

	mat->image_type = type;
	mat->image_flags |= flags;

	MAT_SetIndex(mat);
	MAT_UpdateRegistration(mat);

	return mat;
}

pbr_material_t *MAT_CloneForRadiance(pbr_material_t *src_mat, int radiance)
{
	char clone_name[MAX_QPATH];
	Q_snprintf(clone_name, sizeof(clone_name), "%s*%d", src_mat->name, radiance);
	pbr_material_t *mat = find_material(clone_name, r_materials, MAX_PBR_MATERIALS);

	if(mat)
		return mat;

	// not found - create a material entry
	Com_DPrintf("Synthesizing material for 'emissive' surface with '%s' (%d)\n", src_mat->name, radiance);

	mat = allocate_material();
	uint32_t index = (uint32_t)(mat - r_materials);

	memcpy(mat, src_mat, sizeof(pbr_material_t));
	strcpy(mat->name, clone_name);
	mat->next_frame = index;
	mat->flags &= ~MATERIAL_INDEX_MASK;
	mat->flags |= index;
	mat->flags |= MATERIAL_FLAG_LIGHT;
	mat->registration_sequence = 0;
	mat->emissive_factor = (float)radiance * 0.001f;

	return mat;
}

void MAT_UpdateRegistration(pbr_material_t * mat)
{
	if (!mat)
		return;

	mat->registration_sequence = registration_sequence;
	if (mat->image_base) mat->image_base->registration_sequence = registration_sequence;
	if (mat->image_normals) mat->image_normals->registration_sequence = registration_sequence;
	if (mat->image_emissive) mat->image_emissive->registration_sequence = registration_sequence;
}

//
qerror_t MAT_FreeUnused()
{
	for (uint32_t i = 0; i < MAX_PBR_MATERIALS; ++i)
	{
		pbr_material_t * mat = r_materials + i;

		if (mat->registration_sequence == registration_sequence)
			continue;

		if (!mat->registration_sequence)
			continue;

		if (mat->image_flags & IF_PERMANENT)
			continue;

		MAT_Reset(mat);
	}

	return Q_ERR_SUCCESS;
}

pbr_material_t* MAT_ForIndex(int index)
{
	if (index < 0 || index >= MAX_PBR_MATERIALS)
		return NULL;

	pbr_material_t* mat = r_materials + index;
	if (mat->registration_sequence == 0)
		return NULL;

	return mat;
}

pbr_material_t* MAT_ForSkin(image_t* image_base)
{
	// find the material
	pbr_material_t* mat = MAT_Find(image_base->name, IT_SKIN, IF_NONE);
	assert(mat);

	// if it's already using this skin, do nothing
	if (mat->image_base == image_base)
		return mat;
	
	mat->image_base = image_base;

	// update registration sequence of material and its textures
	int rseq = image_base->registration_sequence;

	if (mat->image_normals)
		mat->image_normals->registration_sequence = rseq;

	if (mat->image_emissive)
		mat->image_emissive->registration_sequence = rseq;

	mat->registration_sequence = rseq;

	return mat;
}

//
// prints material properties on the console
//

void MAT_Print(pbr_material_t const * mat)
{
	Com_Printf("%s:\n", mat->name);
	Com_Printf("    texture_base %s\n", mat->filename_base);
	Com_Printf("    texture_normals %s\n", mat->filename_normals);
	Com_Printf("    texture_emissive %s\n", mat->filename_emissive);
	Com_Printf("    bump_scale %f\n", mat->bump_scale);
	Com_Printf("    rough_override %f\n", mat->rough_override);
	Com_Printf("    metalness_factor %f\n", mat->metalness_factor);
	Com_Printf("    emissive_factor %f\n", mat->emissive_factor);
	const char * kind = getMaterialKindName(mat->flags);
	Com_Printf("    kind %s\n", kind ? kind : "");
	Com_Printf("    is_light %d\n", (mat->flags & MATERIAL_FLAG_LIGHT) != 0);
	Com_Printf("    correct_albedo %d\n", (mat->flags & MATERIAL_FLAG_CORRECT_ALBEDO) != 0);
}

//
// set material attribute command
//

qerror_t MAT_SetAttribute(pbr_material_t* mat, const char* attribute, const char* value)
{
	return set_material_attribute(mat, attribute, value, NULL, 0);
}

uint32_t MAT_SetKind(uint32_t material, uint32_t kind)
{
	return (material & ~MATERIAL_KIND_MASK) | kind;
}

qboolean MAT_IsKind(uint32_t material, uint32_t kind)
{
	return (material & MATERIAL_KIND_MASK) == kind;
}
