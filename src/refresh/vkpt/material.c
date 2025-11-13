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
#include <common/prompt.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>


extern cvar_t *cvar_pt_surface_lights_fake_emissive_algo;
extern cvar_t* cvar_pt_surface_lights_threshold;

extern void CL_PrepRefresh(void);

pbr_material_t r_materials[MAX_PBR_MATERIALS];
static pbr_material_t r_global_materials[MAX_PBR_MATERIALS];
static pbr_material_t r_map_materials[MAX_PBR_MATERIALS];
static uint32_t num_global_materials = 0;
static uint32_t num_map_materials = 0;

#define RMATERIALS_HASH 256
static list_t r_materialsHash[RMATERIALS_HASH];

#define RELOAD_MAP		1
#define RELOAD_EMISSIVE	2

static uint32_t load_material_file(const char* file_name, pbr_material_t* dest, uint32_t max_items);
static void material_command(void);
static void material_completer(genctx_t* ctx, int argnum);

static int compare_materials(const void* a, const void* b)
{
	const pbr_material_t* ma = a;
	const pbr_material_t* mb = b;

	int names = strcmp(ma->name, mb->name);
	if (names != 0)
		return names;

	int sources = strcmp(ma->source_matfile, mb->source_matfile);
	if (sources != 0)
		return sources;

	return (int)ma->source_line - (int)mb->source_line;
}

static void sort_and_deduplicate_materials(pbr_material_t* first, uint32_t* pCount)
{
	const uint32_t count = *pCount;
	
	if (count == 0)
		return;

	// sort the materials by name, then by source file, then by line number
	qsort(first, count, sizeof(pbr_material_t), compare_materials);

	// deduplicate the materials with the same name; latter entries override former ones
	uint32_t write_ptr = 0;
	
	for (uint32_t read_ptr = 0; read_ptr < count; read_ptr++) {
		// if there is a next entry and its name is the same, skip the current entry
		if ((read_ptr + 1 < count) && strcmp(first[read_ptr].name, first[read_ptr + 1].name) == 0)
			continue;

		// copy the input entry to the output entry if they are not the same
		if (read_ptr != write_ptr)
			memcpy(first + write_ptr, first + read_ptr, sizeof(pbr_material_t));

		++write_ptr;
	}

	if (write_ptr < count) {
		// if we've removed some entries, clear the garbage at the end
		memset(first + write_ptr, 0, sizeof(pbr_material_t) * (count - write_ptr));

		// return the new count
		*pCount = write_ptr;
	}
}

// Returns whether the current game is a custom game (not baseq2)
static qboolean is_game_custom(void)
{
	return fs_game->string[0] && strcmp(fs_game->string, BASEGAME) != 0;
}

void MAT_Init()
{
	cmdreg_t commands[2];
	commands[0].name = "mat";
	commands[0].function = (xcommand_t)&material_command;
	commands[0].completer = &material_completer;
	commands[1].name = NULL;
	Cmd_Register(commands);
	
	memset(r_materials, 0, sizeof(r_materials));
	memset(r_global_materials, 0, sizeof(r_global_materials));
	memset(r_map_materials, 0, sizeof(r_map_materials));
	num_global_materials = 0;
	num_map_materials = 0;

	// initialize the hash table
	for (int i = 0; i < RMATERIALS_HASH; i++)
	{
		List_Init(r_materialsHash + i);
	}

	// find all *.mat files in the root
	int num_files;
	void** list = FS_ListFiles("materials", ".mat", 0, &num_files);
	
	for (int i = 0; i < num_files; i++) {
		char* file_name = list[i];
		char buffer[MAX_QPATH];
		Q_concat(buffer, sizeof(buffer), "materials/", file_name);
		
		int mat_slots_available = MAX_PBR_MATERIALS - num_global_materials;
		if (mat_slots_available > 0) {
			uint32_t count = load_material_file(buffer, r_global_materials + num_global_materials,
				mat_slots_available);
			num_global_materials += count;
			
			Com_Printf("Loaded %d materials from %s\n", count, buffer);
		}
		else {
			Com_WPrintf("Coundn't load materials from %s: no free slots.\n", buffer);
		}
		
		Z_Free(file_name);
	}
	Z_Free(list);

	sort_and_deduplicate_materials(r_global_materials, &num_global_materials);
}

void MAT_Shutdown()
{
	Cmd_RemoveCommand("mat");
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
	mat->roughness_override = -1.0f;
	mat->metalness_factor = 1.f;
	mat->emissive_factor = 1.f;
	mat->specular_factor = 1.f;
	mat->base_factor = 1.f;
	mat->light_styles = true;
	mat->bsp_radiance = true;
	/* Treat absence of SURF_LIGHT flag as "fully emissive" by default.
	 * Typically works well with explicit emissive image. */
	mat->default_radiance = 1.f;
	mat->flags = MATERIAL_KIND_REGULAR;
	mat->num_frames = 1;
	mat->emissive_threshold = cvar_pt_surface_lights_threshold->integer;
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

static pbr_material_t* allocate_material(void)
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

static pbr_material_t* find_material(const char* name, uint32_t hash, pbr_material_t* first, uint32_t count)
{
	pbr_material_t* mat;
	
	LIST_FOR_EACH(pbr_material_t, mat, &r_materialsHash[hash], entry)
	{
		if (!mat->registration_sequence)
			continue;

		if (!strcmp(name, mat->name))
			return mat;
	}

	return NULL;
}

static pbr_material_t* find_material_sorted(const char* name, pbr_material_t* first, uint32_t count)
{
	// binary search in a sorted table: global and per-map material dictionaries
	int left = 0, right = (int)count - 1;

	while (left <= right)
	{
		int middle = (left + right) / 2;
		pbr_material_t* mat = first + middle;

		int cmp = strcmp(name, mat->name);

		if (cmp < 0)
			right = middle - 1;
		else if (cmp > 0)
			left = middle + 1;
		else
			return mat;
	}

	return NULL;
}

enum AttributeIndex
{
	MAT_BUMP_SCALE,
	MAT_ROUGHNESS_OVERRIDE,
	MAT_METALNESS_FACTOR,
	MAT_EMISSIVE_FACTOR,
	MAT_KIND,
	MAT_IS_LIGHT,
	MAT_BASE_FACTOR,
	MAT_TEXTURE_BASE,
	MAT_TEXTURE_NORMALS,
	MAT_TEXTURE_EMISSIVE,
	MAT_LIGHT_STYLES,
	MAT_BSP_RADIANCE,
	MAT_DEFAULT_RADIANCE,
	MAT_TEXTURE_MASK,
	MAT_SYNTH_EMISSIVE,
	MAT_EMISSIVE_THRESHOLD,
	MAT_SPECULAR_FACTOR,
};
enum AttributeType { ATTR_BOOL, ATTR_FLOAT, ATTR_STRING, ATTR_INT };

static struct MaterialAttribute {
	enum AttributeIndex index;
	const char* name;
	enum AttributeType type;
} c_Attributes[] = {
	{MAT_BUMP_SCALE, "bump_scale", ATTR_FLOAT},
	{MAT_ROUGHNESS_OVERRIDE, "roughness_override", ATTR_FLOAT},
	{MAT_METALNESS_FACTOR, "metalness_factor", ATTR_FLOAT},
	{MAT_EMISSIVE_FACTOR, "emissive_factor", ATTR_FLOAT},
	{MAT_KIND, "kind", ATTR_STRING},
	{MAT_IS_LIGHT, "is_light", ATTR_BOOL},
	{MAT_BASE_FACTOR, "base_factor", ATTR_FLOAT},
	{MAT_TEXTURE_BASE, "texture_base", ATTR_STRING},
	{MAT_TEXTURE_NORMALS, "texture_normals", ATTR_STRING},
	{MAT_TEXTURE_EMISSIVE, "texture_emissive", ATTR_STRING},
	{MAT_LIGHT_STYLES, "light_styles", ATTR_BOOL},
	{MAT_BSP_RADIANCE, "bsp_radiance", ATTR_BOOL},
	{MAT_DEFAULT_RADIANCE, "default_radiance", ATTR_FLOAT},
	{MAT_TEXTURE_MASK, "texture_mask", ATTR_STRING},
	{MAT_SYNTH_EMISSIVE, "synth_emissive", ATTR_BOOL},
	{MAT_EMISSIVE_THRESHOLD, "emissive_threshold", ATTR_INT},
	{MAT_SPECULAR_FACTOR, "specular_factor", ATTR_FLOAT},
};

static int c_NumAttributes = sizeof(c_Attributes) / sizeof(struct MaterialAttribute);

static void set_material_texture(pbr_material_t* mat, const char* svalue, char mat_texture_path[MAX_QPATH],
	image_t** mat_image, imageflags_t flags, bool from_console)
{
	if (strcmp(svalue, "0") == 0) {
		mat_texture_path[0] = 0;
		*mat_image = NULL;
	}
	else if (from_console) {
		image_t* image = IMG_Find(svalue, mat->image_type, flags | IF_EXACT | (mat->image_flags & IF_SRC_MASK));
		if (image != R_NOTEXTURE) {
			Q_strlcpy(mat_texture_path, svalue, MAX_QPATH);
			*mat_image = image;
		}
		else
			Com_WPrintf("Cannot load texture '%s'\n", svalue);
	}
	else {
		Q_strlcpy(mat_texture_path, svalue, MAX_QPATH);
	}
}

static int set_material_attribute(pbr_material_t* mat, const char* attribute, const char* value,
	const char* sourceFile, uint32_t lineno, unsigned int* reload_flags)
{
	assert(mat);

	// valid attribute-value pairs

	if (attribute == NULL || value == NULL)
		return Q_ERR_FAILURE;

	struct MaterialAttribute const* t = NULL;
	for (int i = 0; i < c_NumAttributes; ++i)
	{
		if (strcmp(attribute, c_Attributes[i].name) == 0)
			t = &c_Attributes[i];
	}
	
	if (!t)
	{
		if (sourceFile)
			Com_EPrintf("%s:%d: unknown material attribute '%s'\n", sourceFile, lineno, attribute);
		else
			Com_EPrintf("Unknown material attribute '%s'\n", attribute);
		return Q_ERR_FAILURE;
	}

	char svalue[MAX_QPATH];

	float fvalue = 0.f; bool bvalue = false;
	int ivalue = 0;
	switch (t->type)
	{
	case ATTR_BOOL:   bvalue = Q_atoi(value) == 0 ? false : true; break;
	case ATTR_FLOAT:  fvalue = (float)Q_atof(value); break;
	case ATTR_STRING: {
		char* asterisk = strchr(value, '*');
		if (asterisk) {
			if(*(asterisk + 1) == '*') {
				// double asterisk: insert complete material name, including path
				Q_strlcpy(svalue, value, min(asterisk - value + 1, sizeof(svalue)));
				Q_strlcat(svalue, mat->name, sizeof(svalue));
				Q_strlcat(svalue, asterisk + 2, sizeof(svalue));
			} else {
				// get the base name of the material, i.e. without the path
				// material names have no extensions, so no need to remove that
				char* slash = strrchr(mat->name, '/');
				char* mat_base = slash ? slash + 1 : mat->name;

				// concatenate: the value before the asterisk, material base name, the rest of the value
				Q_strlcpy(svalue, value, min(asterisk - value + 1, sizeof(svalue)));
				Q_strlcat(svalue, mat_base, sizeof(svalue));
				Q_strlcat(svalue, asterisk + 1, sizeof(svalue));
			}
		}
		else
			Q_strlcpy(svalue, value, sizeof(svalue));
		break;
	case ATTR_INT:
		ivalue = Q_atoi(value);
		break;
	}
	default:
		assert(!"unknown PBR MAT attribute attribute type");
	}

	// set material

	switch (t->index)
	{
	case MAT_BUMP_SCALE: mat->bump_scale = fvalue; break;
	case MAT_ROUGHNESS_OVERRIDE: mat->roughness_override = fvalue; break;
	case MAT_METALNESS_FACTOR: mat->metalness_factor = fvalue; break;
	case MAT_EMISSIVE_FACTOR: mat->emissive_factor = fvalue; break;
	case MAT_KIND: {
		uint32_t kind = getMaterialKind(svalue);
		if (kind != 0)
			mat->flags = MAT_SetKind(mat->flags, kind);
		else
		{
			if (sourceFile)
				Com_EPrintf("%s:%d: unknown material kind '%s'\n", sourceFile, lineno, svalue);
			else
				Com_EPrintf("Unknown material kind '%s'\n", svalue);
			return Q_ERR_FAILURE;
		}
		if (reload_flags) *reload_flags |= RELOAD_MAP;
	} break;
	case MAT_IS_LIGHT:
		mat->flags = bvalue == true ? mat->flags | MATERIAL_FLAG_LIGHT : mat->flags & ~(MATERIAL_FLAG_LIGHT);
		if (reload_flags) *reload_flags |= RELOAD_MAP;
		break;
	case MAT_BASE_FACTOR:
		mat->base_factor = fvalue;
		break;
	case MAT_TEXTURE_BASE:
		set_material_texture(mat, svalue, mat->filename_base, &mat->image_base, IF_SRGB, !sourceFile);
		break;
	case MAT_TEXTURE_NORMALS:
		set_material_texture(mat, svalue, mat->filename_normals, &mat->image_normals, IF_NONE, !sourceFile);
		break;
	case MAT_TEXTURE_EMISSIVE:
		set_material_texture(mat, svalue, mat->filename_emissive, &mat->image_emissive, IF_SRGB, !sourceFile);
		break;
	case MAT_LIGHT_STYLES:
		mat->light_styles = bvalue;
		if (reload_flags) *reload_flags |= RELOAD_MAP;
		break;
	case MAT_BSP_RADIANCE:
		mat->bsp_radiance = bvalue;
		if (reload_flags) *reload_flags |= RELOAD_MAP;
		break;
	case MAT_DEFAULT_RADIANCE:
		mat->default_radiance = fvalue;
		if (reload_flags) *reload_flags |= RELOAD_MAP;
		break;
	case MAT_TEXTURE_MASK:
		set_material_texture(mat, svalue, mat->filename_mask, &mat->image_mask, IF_NONE, !sourceFile);
		if (reload_flags) *reload_flags |= RELOAD_MAP;
		break;
	case MAT_SYNTH_EMISSIVE:
		mat->synth_emissive = bvalue;
		if (reload_flags) *reload_flags |= RELOAD_EMISSIVE;
		break;
	case MAT_EMISSIVE_THRESHOLD:
		mat->emissive_threshold = ivalue;
		if (reload_flags) *reload_flags |= RELOAD_EMISSIVE;
		break;
	case MAT_SPECULAR_FACTOR: mat->specular_factor = fvalue; break;
	default:
		assert(!"unknown PBR MAT attribute index");
	}
	
	return Q_ERR_SUCCESS;
}

static uint32_t load_material_file(const char* file_name, pbr_material_t* dest, uint32_t max_items)
{
	assert(max_items >= 1);
	
	char* filebuf = NULL;
	unsigned source = IF_SRC_GAME;

	if (is_game_custom()) {
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

	enum
	{
		INITIAL,
		ACCUMULATING_NAMES,
		READING_PARAMS
	} state = INITIAL;

	const char* ptr = filebuf;
	char linebuf[1024];
	uint32_t count = 0;
	uint32_t lineno = 0;
	
	const char* delimiters = " \t\r\n";
	uint32_t num_materials_in_group = 0;

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


		// if the line ends with a colon (:) or comma (,) it's a new material section
		char last = linebuf[len - 1];
		if (last == ':' || last == ',')
		{
			if (state == READING_PARAMS || state == ACCUMULATING_NAMES)
			{
				++dest;

				if (count > max_items)
				{
					Com_WPrintf("%s:%d: too many materials, expected up to %d.\n", file_name, lineno, max_items);
					Z_Free(filebuf);
					return count;
				}	
			}
			++count;
			
			MAT_Reset(dest);

			// copy the material name but not the colon
			linebuf[len - 1] = 0;
			Q_strlcpy(dest->name, linebuf, sizeof(dest->name));
			dest->image_flags = source;
			dest->registration_sequence = registration_sequence;

			// copy the material file name
			Q_strlcpy(dest->source_matfile, file_name, sizeof(dest->source_matfile));
			dest->source_line = lineno;

			if (state == ACCUMULATING_NAMES)
				++num_materials_in_group;
			else
				num_materials_in_group = 1;

			// state transition - same logic for all states
			state = (last == ',') ? ACCUMULATING_NAMES : READING_PARAMS;
			
			continue;
		}

		// all other lines are material parameters in the form of "key value" pairs
		
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

		if (state == INITIAL)
		{
			Com_WPrintf("%s:%d: expected material name section before any parameters\n", file_name, lineno);
			continue;
		}

		if (state == ACCUMULATING_NAMES)
		{
			Com_WPrintf("%s:%d: expected a final material name section ending with a colon before any parameters\n", file_name, lineno);

			// rollback the current material group
			dest -= (num_materials_in_group - 1);
			num_materials_in_group = 0;
			state = INITIAL;
			continue;
		}

		for (uint32_t i = 0; i < num_materials_in_group; i++) 
		{
			set_material_attribute(dest - i, key, value, file_name, lineno, NULL);
		}
	}

	Z_Free(filebuf);
	return count;
}

static void save_materials(const char* file_name, bool save_all, bool force)
{
	if (!force && FS_FileExistsEx(file_name, FS_TYPE_REAL))
	{
		Com_WPrintf("File '%s' already exists, add 'force' to overwrite\n", file_name);
		return;
	}

	qhandle_t file = 0;
	int err = FS_OpenFile(file_name, &file, FS_MODE_WRITE);
	
	if (err < 0 || !file)
	{
		Com_WPrintf("Cannot open file '%s' for writing: %s\n", file_name, Q_ErrorString(err));
		return;
	}

	uint32_t count = 0;

	for (uint32_t i = 0; i < MAX_PBR_MATERIALS; i++)
	{
		const pbr_material_t* mat = r_materials + i;
		
		if (!mat->registration_sequence)
			continue;

		// when save_all == false, only save auto-generated materials,
		// i.e. those without a source file
		if (!save_all && mat->source_matfile[0])
			continue;

		FS_FPrintf(file, "%s:\n", mat->name);
		
		if (mat->filename_base[0])
			FS_FPrintf(file, "\ttexture_base %s\n", mat->filename_base);
		
		if (mat->filename_normals[0])
			FS_FPrintf(file, "\ttexture_normals %s\n", mat->filename_normals);
		
		if (mat->filename_emissive[0])
			FS_FPrintf(file, "\ttexture_emissive %s\n", mat->filename_emissive);

		if (mat->filename_mask[0])
			FS_FPrintf(file, "\ttexture_mask %s\n", mat->filename_mask);
		
		if (mat->bump_scale != 1.f)
			FS_FPrintf(file, "\tbump_scale %f\n", mat->bump_scale);
		
		if (mat->roughness_override > 0.f)
			FS_FPrintf(file, "\troughness_override %f\n", mat->roughness_override);
		
		if (mat->metalness_factor != 1.f)
			FS_FPrintf(file, "\tmetalness_factor %f\n", mat->metalness_factor);
		
		if (mat->emissive_factor != 1.f)
			FS_FPrintf(file, "\temissive_factor %f\n", mat->emissive_factor);

		if (mat->specular_factor != 1.f)
			FS_FPrintf(file, "\tspecular_factor %f\n", mat->specular_factor);

		if (mat->base_factor != 1.f)
			FS_FPrintf(file, "\tbase_factor %f\n", mat->base_factor);

		if (!MAT_IsKind(mat->flags, MATERIAL_KIND_REGULAR)) {
			const char* kind = getMaterialKindName(mat->flags);
			FS_FPrintf(file, "\tkind %s\n", kind ? kind : "");
		}
		
		if (mat->flags & MATERIAL_FLAG_LIGHT)
			FS_FPrintf(file, "\tis_light 1\n");
		
		if (!mat->light_styles)
			FS_FPrintf(file, "\tlight_styles 0\n");
		
		if (!mat->bsp_radiance)
			FS_FPrintf(file, "\tbsp_radiance 0\n");

		if (mat->default_radiance != 1.f)
			FS_FPrintf(file, "\tdefault_radiance %f\n", mat->default_radiance);

		if (mat->synth_emissive)
			FS_FPrintf(file, "\tsynth_emissive 1\n");

		if (mat->emissive_threshold != cvar_pt_surface_lights_threshold->integer)
			FS_FPrintf(file, "\temissive_threshold %d\n", mat->emissive_threshold);
		
		FS_FPrintf(file, "\n");
		
		++count;
	}

	FS_CloseFile(file);

	Com_Printf("saved %d materials\n", count);
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
			{
				// remove the material from the hash table
				List_Remove(&mat->entry);

				// invalidate the material entry
				MAT_Reset(mat);
			}
		}
	}
}

static void load_material_image(image_t** image, const char* filename, pbr_material_t* mat, imagetype_t type, imageflags_t flags)
{
	*image = IMG_Find(filename, type, flags | IF_EXACT | (mat->image_flags & IF_SRC_MASK));
	if (*image == R_NOTEXTURE) {
		/* Also try inexact loading in case of an override texture.
		 * Useful for games that ship a texture in a different directory
		 * (compared to the baseq2 location): the override is still picked
		 * up, but if the same path is written to a materials file, a
		 * warning is emitted, since overrides are in generally in baseq2. */
		if(strncmp(filename, "overrides/", 10) == 0)
			*image = IMG_Find(filename, type, flags | IF_EXACT);
	}
}

static qboolean game_image_identical_to_base(const char* name)
{
	/* Check if a game image is actually different from the base version,
	   as some games (eg rogue) ship image assets that are identical to the
	   baseq2 version.
	   If that is the case, ignore the game image, and just use everything
	   from baseq2, especially overides/other images. */
	qboolean result = false;

	qhandle_t base_file = -1, game_file = -1;
	if((FS_OpenFile(name, &base_file, FS_MODE_READ | FS_PATH_BASE | FS_BUF_NONE) >= 0)
		&& (FS_OpenFile(name, &game_file, FS_MODE_READ | FS_PATH_GAME | FS_BUF_NONE) >= 0))
	{
		int64_t base_len = FS_Length(base_file), game_len = FS_Length(game_file);
		if(base_len == game_len)
		{
			char *base_data = FS_Malloc(base_len);
			char *game_data = FS_Malloc(game_len);
			if(FS_Read(base_data, base_len, base_file) >= 0
				&& FS_Read(game_data, game_len, game_file) >= 0)
			{
				result = memcmp(base_data, game_data, base_len) == 0;
			}
			Z_Free(base_data);
			Z_Free(game_data);
		}
	}
	if (base_file >= 0)
		FS_CloseFile(base_file);
	if (game_file >= 0)
		FS_CloseFile(game_file);

	return result;
}

pbr_material_t* MAT_Find(const char* name, imagetype_t type, imageflags_t flags)
{
	char mat_name_no_ext[MAX_QPATH];
	truncate_extension(name, mat_name_no_ext);
	Q_strlwr(mat_name_no_ext);

	uint32_t hash = Com_HashString(mat_name_no_ext, RMATERIALS_HASH);
	
	pbr_material_t* mat = find_material(mat_name_no_ext, hash, r_materials, MAX_PBR_MATERIALS);
	
	if (mat)
	{
		MAT_UpdateRegistration(mat);
		return mat;
	}

	mat = allocate_material();

	pbr_material_t* matdef = find_material_sorted(mat_name_no_ext, r_global_materials, num_global_materials);
	
	if (type == IT_WALL)
	{
		pbr_material_t* map_mat = find_material_sorted(mat_name_no_ext, r_map_materials, num_map_materials);

		if (map_mat)
			matdef = map_mat;
	}

	/* Some games override baseq2 assets without changing the name -
	   e.g. 'action' replaces models/weapons/v_blast with something
	   looking completely differently.
	   Using the material definition from baseq2 makes things look wrong.
	   So try to detect if the game is overriding an image from baseq2
	   with a different image and, if that is the case, ignore the material
	   definition (if it's from baseq2 - to allow for a game-specific material
	   definition).
	   There's also the wrinkle that some games ship with a copy of an image
	   that is identical in baseq2 (see game_image_identical_to_base()),
	   in that case, _do_ use the material definition. */
	if (matdef
		&& (matdef->image_flags & IF_SRC_MASK) == IF_SRC_BASE
		&& is_game_custom()
		&& FS_FileExistsEx(name, FS_PATH_GAME) != 0
		&& !game_image_identical_to_base(name))
	{
		matdef = NULL;
		/* Forcing image to load from game prevents a normal or emissive map in baseq2
		 * from being picked up. */
		flags = (flags & ~IF_SRC_MASK) | IF_SRC_GAME;
	}
	if (matdef)
	{
		memcpy(mat, matdef, sizeof(pbr_material_t));
		uint32_t index = (uint32_t)(mat - r_materials);
		mat->flags = (mat->flags & ~MATERIAL_INDEX_MASK) | index;
		mat->next_frame = index;
		
		
		if (mat->filename_base[0]) {
			load_material_image(&mat->image_base, mat->filename_base, mat, type, flags | IF_SRGB);
			if (mat->image_base == R_NOTEXTURE) {
				Com_WPrintf("Texture '%s' specified in material '%s' could not be found. Using the low-res texture.\n", mat->filename_base, mat_name_no_ext);
				
				mat->image_base = IMG_Find(name, type, flags | IF_SRGB);
				mat->original_width = mat->image_base->width;
				mat->original_height = mat->image_base->height;
				if (mat->image_base == R_NOTEXTURE) {
					mat->image_base = NULL;
				}
			}
			else
			{
				IMG_GetDimensions(name, &mat->original_width, &mat->original_height);
			}
		}

		if (mat->filename_normals[0]) {
			load_material_image(&mat->image_normals, mat->filename_normals, mat, type, flags);
			if (mat->image_normals == R_NOTEXTURE) {
				Com_WPrintf("Texture '%s' specified in material '%s' could not be found.\n", mat->filename_normals, mat_name_no_ext);
				mat->image_normals = NULL;
			}
		}
		
		if (mat->filename_emissive[0]) {
			load_material_image(&mat->image_emissive, mat->filename_emissive, mat, type, flags | IF_SRGB);
			if (mat->image_emissive == R_NOTEXTURE) {
				Com_WPrintf("Texture '%s' specified in material '%s' could not be found.\n", mat->filename_emissive, mat_name_no_ext);
				mat->image_emissive = NULL;
			}
		}
		
		if (mat->filename_mask[0]) {
			mat->image_mask = IMG_Find(mat->filename_mask, type, flags | IF_EXACT | (mat->image_flags & IF_SRC_MASK));
			if (mat->image_mask == R_NOTEXTURE) {
				Com_WPrintf("Texture '%s' specified in material '%s' could not be found.\n", mat->filename_mask, mat_name_no_ext);
				mat->image_mask = NULL;
			}
		}
	}
	else
	{
		MAT_Reset(mat);
		Q_strlcpy(mat->name, mat_name_no_ext, sizeof(mat->name));
		
		mat->image_base = IMG_Find(name, type, flags | IF_SRGB);
		mat->original_width = mat->image_base->width;
		mat->original_height = mat->image_base->height;
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

		// If there is no normals/metalness image, assume that the material is a basic diffuse one.
		if (!mat->image_normals)
		{
			mat->specular_factor = 0.f;
			mat->metalness_factor = 0.f;
		}
	}

	if(mat->synth_emissive && !mat->image_emissive)
		MAT_SynthesizeEmissive(mat);

	if (mat->image_normals)
		mat->image_normals->flags |= IF_NORMAL_MAP;

	if (mat->image_emissive && !mat->image_emissive->processing_complete)
		vkpt_extract_emissive_texture_info(mat->image_emissive);

	mat->image_type = type;
	mat->image_flags |= flags;

	MAT_SetIndex(mat);
	MAT_UpdateRegistration(mat);

	List_Append(&r_materialsHash[hash], &mat->entry);

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
	if (mat->image_mask) mat->image_mask->registration_sequence = registration_sequence;
}

//
int MAT_FreeUnused()
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

		// delete the material from the hash table
		List_Remove(&mat->entry);

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
	Com_Printf("    texture_mask %s\n", mat->filename_mask);
	Com_Printf("    bump_scale %f\n", mat->bump_scale);
	Com_Printf("    roughness_override %f\n", mat->roughness_override);
	Com_Printf("    metalness_factor %f\n", mat->metalness_factor);
	Com_Printf("    emissive_factor %f\n", mat->emissive_factor);
	Com_Printf("    specular_factor %f\n", mat->specular_factor);
	Com_Printf("    base_factor %f\n", mat->base_factor);
	const char * kind = getMaterialKindName(mat->flags);
	Com_Printf("    kind %s\n", kind ? kind : "");
	Com_Printf("    is_light %d\n", (mat->flags & MATERIAL_FLAG_LIGHT) != 0);
	Com_Printf("    light_styles %d\n", mat->light_styles ? 1 : 0);
	Com_Printf("    bsp_radiance %d\n", mat->bsp_radiance ? 1 : 0);
	Com_Printf("    default_radiance %f\n", mat->default_radiance);
	Com_Printf("    synth_emissive %d\n", mat->synth_emissive ? 1 : 0);
	Com_Printf("    emissive_threshold %d\n", mat->emissive_threshold);
}

static void material_command_help(void)
{
	Com_Printf("mat command - interface to the material system\n");
	Com_Printf("usage: mat <command> <arguments...>\n");
	Com_Printf("available commands:\n");
	Com_Printf("    help: print this message\n");
	Com_Printf("    print: print the current material, i.e. one at the crosshair\n");
	Com_Printf("    which: tell where the current material is defined\n");
	Com_Printf("    save <filename> <options>: save the active materials to a file\n");
	Com_Printf("        option 'all': save all materials (otherwise only the undefined ones)\n");
	Com_Printf("        option 'force': overwrite the output file if it exists\n");
	Com_Printf("    <attribute> <value>: set an attribute of the current material\n");
	Com_Printf("        use 'mat print' to list the available attributes\n");
}

static void material_command(void)
{
	if (Cmd_Argc() < 2)
	{
		material_command_help();
		return;
	}

	const char* key = Cmd_Argv(1);

	if (strcmp(key, "help") == 0)
	{
		material_command_help();
		return;
	}

	if (strcmp(key, "save") == 0)
	{
		if (Cmd_Argc() < 3)
		{
			Com_Printf("expected file name\n");
			return;
		}
		
		const char* file_name = Cmd_Argv(2);

		bool save_all = false;
		bool force = false;
		for (int i = 3; i < Cmd_Argc(); i++)
		{
			if (strcmp(Cmd_Argv(i), "all") == 0)
				save_all = true;
			else if (strcmp(Cmd_Argv(i), "force") == 0)
				force = true;
			else {
				Com_Printf("unrecognized argument: %s\n", Cmd_Argv(i));
				return;
			}
		}

		save_materials(file_name, save_all, force);
		return;
	}
	
	pbr_material_t* mat = NULL;

	if (vkpt_refdef.fd)
		mat = MAT_ForIndex(vkpt_refdef.fd->feedback.view_material_index);

	if (!mat)
		return;

	if (strcmp(key, "print") == 0)
	{
		MAT_Print(mat);
		return;
	}
	
	if (strcmp(key, "which") == 0)
	{
		Com_Printf("%s: ", mat->name);
		if (mat->source_matfile[0])
			Com_Printf("%s line %d\n", mat->source_matfile, mat->source_line);
		else
			Com_Printf("automatically generated\n");
		return;
	}

	if (Cmd_Argc() < 3)
	{
		Com_Printf("expected value for attribute\n");
		return;
	}

	unsigned int reload_flags = 0;
	set_material_attribute(mat, key, Cmd_Argv(2), NULL, 0, &reload_flags);

	if ((reload_flags & RELOAD_EMISSIVE) != 0)
	{
		if(mat->image_emissive && strstr(mat->image_emissive->name, "*E"))
		{
			// Prevent previous image being reused to allow emissive_threshold changes
			mat->image_emissive->name[0] = 0;
			mat->image_emissive = NULL;
		}
		if(mat->synth_emissive && !mat->image_emissive)
		{
			// Regenerate emissive image
			MAT_SynthesizeEmissive(mat);

			// In some cases, MAT_SynthesizeEmissive might not create an emissive image - test for that
			if (mat->image_emissive)
			{
				// Make sure it's loaded by CL_PrepRefresh()
				IMG_Load(mat->image_emissive, mat->image_emissive->pix_data);
			}

			reload_flags |= RELOAD_MAP;
		}
	}

	if ((reload_flags & RELOAD_MAP) != 0)
	{
		// Trigger a re-upload and rebuild of the models that use this material.
		// Reason to rebuild: some material changes result in meshes being classified as
		// transparent or masked, which affects the static model BLAS.
		vkpt_vertex_buffer_invalidate_static_model_vbos(vkpt_refdef.fd->feedback.view_material_index);

		// Reload the map and necessary models.
		CL_PrepRefresh();
	}
}

static void material_completer(genctx_t* ctx, int argnum)
{
	if (argnum == 1) {
		// sub-commands
		
		Prompt_AddMatch(ctx, "print");
		Prompt_AddMatch(ctx, "save");
		Prompt_AddMatch(ctx, "which");

		for (int i = 0; i < c_NumAttributes; i++)
			Prompt_AddMatch(ctx, c_Attributes[i].name);
	}
	else if (argnum == 2)
	{
		if(strcmp(Cmd_Argv(1), "save") == 0)
		{
			// extra arguments for 'save'
			Prompt_AddMatch(ctx, "all");
			Prompt_AddMatch(ctx, "force");
		}
		else if((strcmp(Cmd_Argv(1), "print") == 0) || (strcmp(Cmd_Argv(1), "which") == 0))
		{
			// Nothing to complete for these
		}
		else
		{
			// Material property completion
			struct MaterialAttribute const* t = NULL;
			for (int i = 0; i < c_NumAttributes; ++i)
			{
				if (strcmp(Cmd_Argv(1), c_Attributes[i].name) == 0)
				{
					t = &c_Attributes[i];
					break;
				}
			}

			if(!t)
				return;

			// Attribute-specific completions
			switch(t->index)
			{
			case MAT_KIND:
				for (int i = 0; i < nMaterialKinds; ++i)
				{
					// Use lower-case for completion, that is what you'd typically type
					char kind[32];
					Q_strlcpy(kind, materialKinds[i].name, sizeof(kind));
					Q_strlwr(kind);
					Prompt_AddMatch(ctx, kind);
				}
				return;
			default:
				break;
			}

			// Type-specific completions
			if(t->type == ATTR_BOOL)
			{
				Prompt_AddMatch(ctx, "0");
				Prompt_AddMatch(ctx, "1");
				return;
			}
		}
	}
}

uint32_t MAT_SetKind(uint32_t material, uint32_t kind)
{
	return (material & ~MATERIAL_KIND_MASK) | kind;
}

bool MAT_IsKind(uint32_t material, uint32_t kind)
{
	return (material & MATERIAL_KIND_MASK) == kind;
}

static image_t* get_fake_emissive_image(image_t* diffuse, int bright_threshold_int)
{
	switch(cvar_pt_surface_lights_fake_emissive_algo->integer)
	{
	case 0:
		return diffuse;
	case 1:
		return vkpt_fake_emissive_texture(diffuse, bright_threshold_int);
	default:
		return NULL;
	}
}

void MAT_SynthesizeEmissive(pbr_material_t * mat)
{
	if (!mat->image_emissive) {
		mat->image_emissive = get_fake_emissive_image(mat->image_base, mat->emissive_threshold);
		mat->synth_emissive = true;

		if (mat->image_emissive) {
			vkpt_extract_emissive_texture_info(mat->image_emissive);
		}
	}
}

bool MAT_IsTransparent(uint32_t material)
{
	return MAT_IsKind(material, MATERIAL_KIND_SLIME)
		|| MAT_IsKind(material, MATERIAL_KIND_WATER)
		|| MAT_IsKind(material, MATERIAL_KIND_GLASS)
		|| MAT_IsKind(material, MATERIAL_KIND_TRANSPARENT)
		|| MAT_IsKind(material, MATERIAL_KIND_TRANSP_MODEL);
}

bool MAT_IsMasked(uint32_t material)
{
	const pbr_material_t* mat = MAT_ForIndex((int)(material & MATERIAL_INDEX_MASK));

	return mat && mat->image_mask;
}
