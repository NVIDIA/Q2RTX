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
#include "common/common.h"
#include "common/files.h"
#include "refresh/images.h"
#include "vk_util.h"
#include "shader/constants.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <common/common.h>

//
// CSV parsing
//

#define MAX_CSV_VALUES 32
typedef struct CSV_values_s {
	char * values[MAX_CSV_VALUES];
	int num_values;
} CSV_values_t;

static qerror_t getStringValue(CSV_values_t const * csv, int index, char * dest)
{
	if (index >= csv->num_values)
		return Q_ERR_FAILURE;
	strncpy(dest, csv->values[index], MAX_QPATH);
	return Q_ERR_SUCCESS;
}

static qerror_t getFloatValue(CSV_values_t const * csv, int index, float * dest)
{
	dest[0] = '\0';
	if (index >= csv->num_values || csv->values[index][0] == '\0')
		return Q_ERR_FAILURE;
	*dest = atof(csv->values[index]);
	return Q_ERR_SUCCESS;
}

static qerror_t getIntValue(CSV_values_t const * csv, int index, int * dest)
{
	if (index >= csv->num_values || csv->values[index][0] == '\0')
		return Q_ERR_FAILURE;
	*dest = atoi(csv->values[index]);
	return Q_ERR_SUCCESS;
}

static qerror_t getFlagValue(CSV_values_t const * csv, int index, uint32_t * flags, uint32_t mask)
{
	if (index >= csv->num_values || csv->values[index][0] == '\0')
		return Q_ERR_FAILURE;
	if (atoi(csv->values[index]))
		*flags = *flags | mask;
	return Q_ERR_SUCCESS;
}

static qerror_t parse_CSV_line(char * linebuf, CSV_values_t * csv)
{
	static char delim = ',';

	char * cptr = linebuf, c;
	int inquote = 0;

	int index = 0;
	csv->values[index] = cptr;
	
	while ((c = *cptr) != '\0' && index < MAX_CSV_VALUES)
	{
		if (c == '"')
		{
			if (!inquote)
				csv->values[index] = cptr + 1;
			else
				*cptr = '\0';
			inquote = !inquote;
		}
		else if (c == delim && !inquote)
		{
			*cptr = '\0';
			csv->values[++index] = cptr + 1;
		}
		cptr++;
	}
	csv->num_values = index + 1;
	return Q_ERR_SUCCESS;
}

//
// Helpers
//

static size_t truncateExtension(char const * src, char * dest)
{
	// remove extension if any
	size_t len = strlen(src);
	assert(len < MAX_QPATH);

	if (len > 4 && src[len - 4] == '.')
		len -= 4;

	// make a copy of the name without the extension: we need to compare full names, and "skin_rgh" == "skin" is unacceptable
	memcpy(dest, src, len);
	dest[len] = 0;
	return len;
}

//
// PBR materials cache
//
typedef struct pbr_materials_table_s {
	char filename[MAX_QPATH];
	pbr_material_t materials[MAX_PBR_MATERIALS];
	int num_materials;
	qboolean alpha_sorted;
	int num_custom_materials;
} pbr_materials_table_t;

static pbr_materials_table_t pbr_materials_table = { .num_materials = 0, .num_custom_materials = 0, .alpha_sorted = qtrue };

static pbr_material_t *find_existing_pbr_material(char const *name);

pbr_material_t const * MAT_GetPBRMaterialsTable()
{
	return &pbr_materials_table.materials[0];
}

int MAT_GetNumPBRMaterials()
{
	return pbr_materials_table.num_materials + pbr_materials_table.num_custom_materials;
}

int MAT_GetPBRMaterialIndex(pbr_material_t const * mat)
{
	return mat->flags & MATERIAL_INDEX_MASK;
}

static void MAT_Reset(pbr_material_t * mat, int mat_index)
{
	mat->image_diffuse = mat->image_normals = mat->image_emissive = R_NOTEXTURE;
	mat->bump_scale = mat->specular_scale = mat->emissive_scale = 1.0f;
	mat->rough_override = -1.0f;
	mat->flags = mat_index;
	assert((mat->flags & ~MATERIAL_INDEX_MASK) == 0);
	mat->registration_sequence = 0;
	mat->num_frames = 1;
	mat->next_frame = mat_index;
}

static qerror_t validateMaterialsTable(pbr_materials_table_t * table)
{	
	table->alpha_sorted = qtrue;
	for (int i = 1; i < table->num_materials; ++i)
	{
		pbr_material_t const * a = &table->materials[i - 1];
		pbr_material_t const * b = &table->materials[i];
		int cmp = Q_strcasecmp(a->name, b->name);
		if (cmp == 0)
		{
			Com_EPrintf("duplicate material names in materials table '%s'\n", table->filename);
			return Q_ERR_FAILURE;
		}
		else if (cmp > 0)
		{
			Com_WPrintf("materials table '%s' is not sorted - fast search disabled\n", table->filename);
			table->alpha_sorted = qfalse;
		}
	}
	return Q_ERR_SUCCESS;
}

//
// material kinds (translations between names & bit flags)
//

static struct MaterialKind {
	char const * name;
	uint32_t flag;
} materialKinds[] = {
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

static uint32_t getMaterialKind(char const * kindname)
{
	for (int i = 0; i < nMaterialKinds; ++i)
		if (Q_stricmp(kindname, materialKinds[i].name) == 0)
			return materialKinds[i].flag;
	return MATERIAL_KIND_REGULAR;
}

static char const * getMaterialKindName(uint32_t flag)
{
	for (int i = 0; i < nMaterialKinds; ++i)
		if ((flag & MATERIAL_KIND_MASK) == materialKinds[i].flag)
			return materialKinds[i].name;
	return NULL;
}

//
// write material table to file
//

static qerror_t writeMaterialsTable(char const * filename, pbr_materials_table_t const * table)
{
	char path[MAX_OSPATH];
	qhandle_t f = FS_EasyOpenFile(path, sizeof(path), FS_MODE_WRITE | FS_FLAG_TEXT, "", filename, "");
	if (!f) {
		Com_EPrintf("Error opening '%s'\n", path);
		return Q_ERR_FAILURE;
	}

	FS_FPrintf(f, "Material,Bump Scale,Roughness Override,Specular Scale,Emissive Scale,Kind,Invisible Flag,Light Flag,Correct Albedo Flag\n");

	for (int i = 0; i < table->num_materials; ++i)
	{
		pbr_material_t const * mat = &table->materials[i];

		// Skip materials generated for LIGHT surfaces
		if(strchr(mat->name, '*') != NULL)
			continue;

		FS_FPrintf(f, "%s,",  mat->name);

		FS_FPrintf(f, "%g,%g,%g,%g,", mat->bump_scale, mat->rough_override, mat->specular_scale, mat->emissive_scale);

		char const * kind = getMaterialKindName(mat->flags);
		FS_FPrintf(f, "%s,", kind ? kind : "");

		FS_FPrintf(f, "%d,", mat->flags & MATERIAL_FLAG_LIGHT ? (mat->enable_light_styles ? 1 : 2) : 0);
		FS_FPrintf(f, "%d\n", mat->flags & MATERIAL_FLAG_CORRECT_ALBEDO ? 1 : 0);
	}
	
	FS_FCloseFile(f);

	Com_Printf("Saved '%s'\n", path);
	return Q_ERR_SUCCESS;
}

static qerror_t parseMaterialsTable(char const * filename, pbr_materials_table_t * table)
{
	assert(table);

	table->num_materials = 0;
	table->num_custom_materials = 0;

	byte * buffer = NULL; ssize_t buffer_size = 0;
	buffer_size = FS_LoadFile(filename, (void**)&buffer);
	if (buffer == NULL)
	{
		Com_EPrintf("cannot load materials table '%s'\n", filename);
		return Q_ERR_FAILURE;
	}

	Q_concat(table->filename, sizeof(table->filename), filename, NULL);

	int currentLine = 0;
	char const * ptr = (char const *)buffer;
	char linebuf[MAX_QPATH * 3];
	while (sgets(linebuf, sizeof(linebuf), &ptr))
	{
		// skip first line because it contains the column names
		if (currentLine == 0 && strncmp(linebuf, "Material", 8) == 0)
			continue;

		pbr_material_t * mat = &table->materials[table->num_materials];

		MAT_Reset(mat, table->num_materials);

		qerror_t status = Q_ERR_SUCCESS;
		CSV_values_t csv = { 0 };

		if (parse_CSV_line(linebuf, &csv)==Q_ERR_SUCCESS)
		{
			if (getStringValue(&csv, 0, mat->name) == Q_ERR_SUCCESS && mat->name[0] != '\0')
			{
				status |= getFloatValue(&csv, 1, &mat->bump_scale);
				status |= getFloatValue(&csv, 2, &mat->rough_override);
				status |= getFloatValue(&csv, 3, &mat->specular_scale);
				status |= getFloatValue(&csv, 4, &mat->emissive_scale);

				char kindname[MAX_QPATH];
				getStringValue(&csv, 5, kindname);
				mat->flags |= getMaterialKind(kindname);
				
				int light_flag = 0;
				status |= getIntValue(&csv, 6, &light_flag);
				if (light_flag != 0) mat->flags |= MATERIAL_FLAG_LIGHT;
				mat->enable_light_styles = (light_flag <= 1);

				status |= getFlagValue(&csv, 7, &mat->flags, MATERIAL_FLAG_CORRECT_ALBEDO);
			}
			else
				status = Q_ERR_FAILURE;
		}
		if (status == Q_ERR_SUCCESS)
		{
			++table->num_materials;
		}
		else
		{
			mat->flags = MAT_SetKind(mat->flags, MATERIAL_KIND_INVALID);
			Com_EPrintf("CSV error in materials table : '%s':%d", table->filename, currentLine);
		}
		++currentLine;
	}

	qerror_t status = validateMaterialsTable(table);

	Com_Printf("Loaded '%s' (fast search = %s)\n", filename, table->alpha_sorted == qtrue ? "true" : "false");

	FS_FreeFile(buffer);

	return status;
}

// load materials file from Q2 FS
static char const * materials_filename = "materials.csv";

// cache materials on game start
qerror_t MAT_InitializePBRmaterials()
{	
	return parseMaterialsTable(materials_filename, &pbr_materials_table);
}

// update materials if the file change (dev only feature)
qerror_t MAT_ReloadPBRMaterials()
{
	pbr_materials_table_t newtable;

	pbr_materials_table_t * old_table = &pbr_materials_table,
		                  * new_table = &newtable;

	if (parseMaterialsTable(materials_filename, new_table) == Q_ERR_SUCCESS)
	{
		if (new_table->num_materials != old_table->num_materials)
		{
			Com_EPrintf("Cannot reload materials table : table has different material count, please restart the game\n");
			return Q_ERR_FAILURE;
		}

		if (new_table->alpha_sorted != old_table->alpha_sorted)
		{
			Com_EPrintf("Cannot reload materials table : table has different material keys, please check your edits\n");
			return Q_ERR_FAILURE;
		}

		for (int i = 0; i < old_table->num_materials; ++i)
		{		
			pbr_material_t * old_mat = &old_table->materials[i],
				           * new_mat = &new_table->materials[i];

			if (strncmp(old_mat->name, new_mat->name, MAX_QPATH) != 0)
			{
				Com_EPrintf("Cannot update material '%s' : incorrect name, please check your edits\n", new_mat->name);
				continue;
			}

			// update CSV values
			old_mat->bump_scale = new_mat->bump_scale;
			old_mat->rough_override = new_mat->rough_override;
			old_mat->specular_scale = new_mat->specular_scale;
			old_mat->emissive_scale = new_mat->emissive_scale;
			old_mat->flags = new_mat->flags;
		}
		Com_Printf("Reloaded '%s' : %d materials updated\n", materials_filename, old_table->num_materials);
	}
	return Q_ERR_FAILURE;
}

qerror_t MAT_SavePBRMaterials()
{
	return writeMaterialsTable(materials_filename, &pbr_materials_table);
}

pbr_material_t *MAT_CloneForRadiance(pbr_material_t *src_mat, int radiance)
{
	char clone_name[MAX_QPATH];
	Q_snprintf(clone_name, sizeof(clone_name), "%s*%d", src_mat->name, radiance);
	pbr_material_t *mat = find_existing_pbr_material(clone_name);

	if(mat)
		return mat;

	// not found - create a material entry
	Com_DPrintf("Synthesizing material for 'emissive' surface with '%s' (%d)\n", src_mat->name, radiance);
	pbr_materials_table_t * table = &pbr_materials_table;
	int index = table->num_materials + table->num_custom_materials;
	table->num_custom_materials++;
	mat = &table->materials[index];
	memcpy(mat, src_mat, sizeof(pbr_material_t));
	strcpy(mat->name, clone_name);
	mat->next_frame = index;
	mat->flags &= ~MATERIAL_INDEX_MASK;
	mat->flags |= index;
	mat->flags |= MATERIAL_FLAG_LIGHT;
	mat->registration_sequence = 0;
	mat->emissive_scale = radiance * 0.001f;

	return mat;
}

qerror_t MAT_RegisterPBRMaterial(pbr_material_t * mat,  image_t * image_diffuse, image_t * image_normals, image_t * image_emissive)
{
	if (!mat)
	{
		//Com_Error(ERR_FATAL, "%s: bad pbr material", __func__);
		return Q_ERR_FAILURE;
	}

	// already registered ?
	if (mat->registration_sequence == registration_sequence)
		return Q_ERR_SUCCESS;

	mat->image_diffuse = image_diffuse;
	mat->image_normals = image_normals;
	mat->image_emissive = image_emissive;

	MAT_UpdateRegistration(mat);

	//if (mat->image_diffuse == R_NOTEXTURE)
	//    mat->flags &= ~(MATERIAL_FLAG_VALID);

	return Q_ERR_SUCCESS;
}

void MAT_UpdateRegistration(pbr_material_t * mat)
{
	if (!mat)
		return;

	mat->registration_sequence = registration_sequence;
	if (mat->image_diffuse) mat->image_diffuse->registration_sequence = registration_sequence;
	if (mat->image_normals) mat->image_normals->registration_sequence = registration_sequence;
	if (mat->image_emissive) mat->image_emissive->registration_sequence = registration_sequence;
}

//
qerror_t MAT_ResetUnused()
{
	pbr_materials_table_t * table = &pbr_materials_table;

	for (int i = 0; i < table->num_materials + table->num_custom_materials; ++i)
	{
		pbr_material_t * mat = table->materials + i;

		if (mat->registration_sequence == registration_sequence)
			continue;

		if (!mat->registration_sequence)
			continue;

		mat->image_diffuse = R_NOTEXTURE;
		mat->image_normals = R_NOTEXTURE;
		mat->image_emissive = R_NOTEXTURE;

		mat->registration_sequence = 0;
	}
	return Q_ERR_SUCCESS;
}


pbr_material_t * MAT_GetPBRMaterial(int index)
{
	if (index < pbr_materials_table.num_materials + pbr_materials_table.num_custom_materials)
		return &pbr_materials_table.materials[index];
	return NULL;
}


static pbr_material_t * find_existing_pbr_material(char const * name)
{
	pbr_materials_table_t * table = &pbr_materials_table;

	// note : key comparison must be case insensitive

	int search_start = 0;

	if (table->alpha_sorted)
	{
		// binary search if names are sorted in alpha order
		int left = 0, right = table->num_materials - 1;

		while (left <= right)
		{
			int middle = floor((left + right) / 2);
			pbr_material_t * mat = &table->materials[middle];
			int cmp = Q_strcasecmp(name, mat->name);
			if (cmp < 0)
				right = middle - 1;
			else if (cmp > 0)
				left = middle + 1;
			else
				return mat;
		}

		search_start = pbr_materials_table.num_materials;
	}
	
	// brute-force search if the table is not sorted or if the material is not found in the main table
	for (int i = search_start; i < table->num_materials + table->num_custom_materials; ++i)
	{
		pbr_material_t * mat = &table->materials[i];
		if (Q_strcasecmp(mat->name, name) == 0)
			return mat;
	}

	return NULL;
}

pbr_material_t * MAT_FindPBRMaterial(char const * name)
{
	char name_copy[MAX_QPATH];
	int len = truncateExtension(name, name_copy);
	assert(len>0);

	pbr_material_t *existing_mat = find_existing_pbr_material(name_copy);
	if(existing_mat)
		return existing_mat;

	// not found - create a material entry
	pbr_materials_table_t * table = &pbr_materials_table;
	int index = table->num_materials + table->num_custom_materials;
	table->num_custom_materials++;
	pbr_material_t * mat = &table->materials[index];
	MAT_Reset(mat, index);
	strcpy(mat->name, name_copy);
	Com_DPrintf("Created a material entry %d for unknown material %s\n", index, name_copy);

	return mat;
}

pbr_material_t const * MAT_UpdatePBRMaterialSkin(image_t * image_diffuse)
{
	pbr_material_t const * material = MAT_FindPBRMaterial(image_diffuse->name);
	assert(material);

	if (material->image_diffuse == image_diffuse)
		return material;

	{
		pbr_material_t * mat = (pbr_material_t *)material;
		//MAT_Reset(mat, MAT_GetPBRMaterialIndex(mat));

		mat->image_diffuse = image_diffuse;

		// update registration sequence of material and its textures
		int registration_sequence = image_diffuse->registration_sequence;

		if (mat->image_normals)
			mat->image_normals->registration_sequence = registration_sequence;

		if (mat->image_emissive)
			mat->image_emissive->registration_sequence = registration_sequence;

		mat->registration_sequence = registration_sequence;
	}
	return material;
}

//
// prints material properties on the console
//

void MAT_PrintMaterialProperties(pbr_material_t const * mat)
{
	Com_Printf("{ %s\n", mat->name);
	Com_Printf("    image_diffuse = '%s'\n", mat->image_diffuse ? mat->image_diffuse->name : "");
	Com_Printf("    image_normals = '%s'\n", mat->image_normals ? mat->image_normals->name : "");
	Com_Printf("    image_emissive = '%s'\n", mat->image_emissive ? mat->image_emissive->name : "");
	Com_Printf("    bump_scale = %f,\n", mat->bump_scale);
	Com_Printf("    rough_override = %f,\n", mat->rough_override);
	Com_Printf("    specular_scale = %f,\n", mat->specular_scale);
	Com_Printf("    emissive_scale = %f,\n", mat->emissive_scale);
	char const * kind = getMaterialKindName(mat->flags);
	Com_Printf("    kind = '%s',\n", kind ? kind : "");
	Com_Printf("    light = %d,\n", (mat->flags & MATERIAL_FLAG_LIGHT) ? (mat->enable_light_styles ? 1 : 2) : 0);
	Com_Printf("    correct_albedo = %d\n", (mat->flags & MATERIAL_FLAG_CORRECT_ALBEDO) != 0);
	Com_Printf("}\n");
}

//
// set material attribute command
//

qerror_t MAT_SetPBRMaterialAttribute(pbr_material_t * mat, char const * token, char const * value)
{
	assert(mat);

	// valid token-value pairs

	enum TokenType { TOKEN_BOOL, TOKEN_FLOAT, TOKEN_STRING };

	static struct Token {
		int index;
		char const * name;
		enum TokenType type;
	} tokens[] = {
		{0, "bump_scale", TOKEN_FLOAT},
		{1, "roughness_override", TOKEN_FLOAT},
		{2, "specular_scale", TOKEN_FLOAT},
		{3, "emissive_scale", TOKEN_FLOAT},
		{4, "kind", TOKEN_STRING},
		{5, "light_flag", TOKEN_BOOL},
		{6, "correct_albedo_flag", TOKEN_BOOL} };

	static int ntokens = sizeof(tokens) / sizeof(struct Token);

	if (token == NULL || value == NULL)
		goto usage;

	struct Token const * t = NULL;
	for (int i = 0; i < ntokens; ++i)
	{
		if (strcmp(token, tokens[i].name) == 0)
			t = &tokens[i];
	}
	if (!t)
	{
		Com_EPrintf("Unknown material token '%s'\n", Cmd_Argv(1));
		goto usage;
	}

	float fvalue = 0.f; qboolean bvalue = qfalse; char const * svalue = NULL;
	switch (t->type)
	{
		case TOKEN_BOOL:   bvalue = atoi(value) == 0 ? qfalse : qtrue; break;
		case TOKEN_FLOAT:  fvalue = (float)atof(value); break;
		case TOKEN_STRING: svalue = value; break;
		default:
			assert("unknown PBR MAT attribute token type");
	}

	// set material

	switch (t->index)
	{
		case 0: mat->bump_scale = fvalue; break;
		case 1: mat->rough_override = fvalue; break;
		case 2: mat->specular_scale = fvalue; break;
		case 3: mat->emissive_scale = fvalue; break;
		case 4: {
			uint32_t kind = getMaterialKind(svalue);
			if (kind != 0)
				mat->flags = MAT_SetKind(mat->flags, kind);
			else 
			{
				Com_EPrintf("Unknown material kind '%s'\n", svalue);
				return Q_ERR_FAILURE;
			}
		} break;
		case 5: mat->flags = bvalue == qtrue ? mat->flags | MATERIAL_FLAG_LIGHT : mat->flags & ~(MATERIAL_FLAG_LIGHT); break;
		case 6: mat->flags = bvalue == qtrue ? mat->flags | MATERIAL_FLAG_CORRECT_ALBEDO : mat->flags & ~(MATERIAL_FLAG_CORRECT_ALBEDO); break;
	}

	return Q_ERR_SUCCESS;

usage:
	Com_Printf("Usage : set_material <token> <value>\n");
	for (int i = 0; i < ntokens; ++i)
	{
		struct Token const * t = &tokens[i];

		char const * typename = "(undefined)";
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

uint32_t MAT_SetKind(uint32_t material, uint32_t kind)
{
	return (material & ~MATERIAL_KIND_MASK) | kind;
}

qboolean MAT_IsKind(uint32_t material, uint32_t kind)
{
	return (material & MATERIAL_KIND_MASK) == kind;
}

qboolean MAT_IsCustom(uint32_t material)
{
	return (material & MATERIAL_INDEX_MASK) >= pbr_materials_table.num_materials;
}
