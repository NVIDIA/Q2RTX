/*
===========================================================================
Copyright (C) 2011 Thilo Schulz <thilo@tjps.eu>
Copyright (C) 2011 Matthias Bentrup <matthias.bentrup@googlemail.com>
Copyright (C) 2011-2019 Zack Middleton <zturtleman@gmail.com>

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include <assert.h>
#include <shared/shared.h>
#include <common/common.h>
#include <format/iqm.h>
#include <refresh/models.h>
#include <refresh/refresh.h>

static bool IQM_CheckRange(const iqmHeader_t* header, uint32_t offset, uint32_t count, size_t size)
{
	// return true if the range specified by offset, count and size
	// doesn't fit into the file
	return (count == 0 ||
		offset > header->filesize ||
		offset + count * size > header->filesize);
}

// "multiply" 3x4 matrices, these are assumed to be the top 3 rows
// of a 4x4 matrix with the last row = (0 0 0 1)
static void Matrix34Multiply(const float* a, const float* b, float* out)
{
	out[0] = a[0] * b[0] + a[1] * b[4] + a[2] * b[8];
	out[1] = a[0] * b[1] + a[1] * b[5] + a[2] * b[9];
	out[2] = a[0] * b[2] + a[1] * b[6] + a[2] * b[10];
	out[3] = a[0] * b[3] + a[1] * b[7] + a[2] * b[11] + a[3];
	out[4] = a[4] * b[0] + a[5] * b[4] + a[6] * b[8];
	out[5] = a[4] * b[1] + a[5] * b[5] + a[6] * b[9];
	out[6] = a[4] * b[2] + a[5] * b[6] + a[6] * b[10];
	out[7] = a[4] * b[3] + a[5] * b[7] + a[6] * b[11] + a[7];
	out[8] = a[8] * b[0] + a[9] * b[4] + a[10] * b[8];
	out[9] = a[8] * b[1] + a[9] * b[5] + a[10] * b[9];
	out[10] = a[8] * b[2] + a[9] * b[6] + a[10] * b[10];
	out[11] = a[8] * b[3] + a[9] * b[7] + a[10] * b[11] + a[11];
}

static void JointToMatrix(const quat_t rot, const vec3_t scale, const vec3_t trans,	float* mat)
{
	float xx = 2.0f * rot[0] * rot[0];
	float yy = 2.0f * rot[1] * rot[1];
	float zz = 2.0f * rot[2] * rot[2];
	float xy = 2.0f * rot[0] * rot[1];
	float xz = 2.0f * rot[0] * rot[2];
	float yz = 2.0f * rot[1] * rot[2];
	float wx = 2.0f * rot[3] * rot[0];
	float wy = 2.0f * rot[3] * rot[1];
	float wz = 2.0f * rot[3] * rot[2];

	mat[0] = scale[0] * (1.0f - (yy + zz));
	mat[1] = scale[0] * (xy - wz);
	mat[2] = scale[0] * (xz + wy);
	mat[3] = trans[0];
	mat[4] = scale[1] * (xy + wz);
	mat[5] = scale[1] * (1.0f - (xx + zz));
	mat[6] = scale[1] * (yz - wx);
	mat[7] = trans[1];
	mat[8] = scale[2] * (xz - wy);
	mat[9] = scale[2] * (yz + wx);
	mat[10] = scale[2] * (1.0f - (xx + yy));
	mat[11] = trans[2];
}

static void Matrix34Invert(const float* inMat, float* outMat)
{
	outMat[0] = inMat[0]; outMat[1] = inMat[4]; outMat[2] = inMat[8];
	outMat[4] = inMat[1]; outMat[5] = inMat[5]; outMat[6] = inMat[9];
	outMat[8] = inMat[2]; outMat[9] = inMat[6]; outMat[10] = inMat[10];
	
	float invSqrLen, *v;
	v = outMat + 0; invSqrLen = 1.0f / DotProduct(v, v); VectorScale(v, invSqrLen, v);
	v = outMat + 4; invSqrLen = 1.0f / DotProduct(v, v); VectorScale(v, invSqrLen, v);
	v = outMat + 8; invSqrLen = 1.0f / DotProduct(v, v); VectorScale(v, invSqrLen, v);

	vec3_t trans;
	trans[0] = inMat[3];
	trans[1] = inMat[7];
	trans[2] = inMat[11];

	outMat[3] = -DotProduct(outMat + 0, trans);
	outMat[7] = -DotProduct(outMat + 4, trans);
	outMat[11] = -DotProduct(outMat + 8, trans);
}

static void QuatSlerp(const quat_t from, const quat_t _to, float fraction, quat_t out)
{
	// cos() of angle
	float cosAngle = from[0] * _to[0] + from[1] * _to[1] + from[2] * _to[2] + from[3] * _to[3];

	// negative handling is needed for taking shortest path (required for model joints)
	quat_t to;
	if (cosAngle < 0.0f)
	{
		cosAngle = -cosAngle;
		to[0] = -_to[0];
		to[1] = -_to[1];
		to[2] = -_to[2];
		to[3] = -_to[3];
	}
	else
	{
		QuatCopy(_to, to);
	}

	float backlerp, lerp;
	if (cosAngle < 0.999999f)
	{
		// spherical lerp (slerp)
		const float angle = acosf(cosAngle);
		const float sinAngle = sinf(angle);
		backlerp = sinf((1.0f - fraction) * angle) / sinAngle;
		lerp = sinf(fraction * angle) / sinAngle;
	}
	else
	{
		// linear lerp
		backlerp = 1.0f - fraction;
		lerp = fraction;
	}

	out[0] = from[0] * backlerp + to[0] * lerp;
	out[1] = from[1] * backlerp + to[1] * lerp;
	out[2] = from[2] * backlerp + to[2] * lerp;
	out[3] = from[3] * backlerp + to[3] * lerp;
}

static vec_t QuatNormalize2(const quat_t v, quat_t out)
{
	float length = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3];

	if (length > 0.f)
	{
		/* writing it this way allows gcc to recognize that rsqrt can be used */
		float ilength = 1 / sqrtf(length);
		/* sqrt(length) = length * (1 / sqrt(length)) */
		length *= ilength;
		out[0] = v[0] * ilength;
		out[1] = v[1] * ilength;
		out[2] = v[2] * ilength;
		out[3] = v[3] * ilength;
	}
	else
	{
		out[0] = out[1] = out[2] = 0;
		out[3] = -1;
	}

	return length;
}

// ReSharper disable CppClangTidyClangDiagnosticCastAlign

/*
=================
MOD_LoadIQM_Base

Load an IQM model and compute the joint poses for every frame.
=================
*/
int MOD_LoadIQM_Base(model_t* model, const void* rawdata, size_t length, const char* mod_name)
{
	iqm_transform_t* transform;
	float* mat, * matInv;
	size_t joint_names;
	iqm_model_t* iqmData;
	char meshName[MAX_QPATH];
	int vertexArrayFormat[IQM_COLOR + 1];
	int ret;

	if (length < sizeof(iqmHeader_t))
	{
		return Q_ERR_UNEXPECTED_EOF;
	}

	const iqmHeader_t* header = rawdata;
	if (strncmp(header->magic, IQM_MAGIC, sizeof(header->magic)) != 0)
	{
		return Q_ERR_INVALID_FORMAT;
	}

	if (header->version != IQM_VERSION)
	{
		Com_SetLastError(va("R_LoadIQM: %s is a unsupported IQM version (%d), only version %d is supported.",
			mod_name, header->version, IQM_VERSION));
		return Q_ERR_UNKNOWN_FORMAT;
	}

	if (header->filesize > length || header->filesize > 16 << 20)
	{
		return Q_ERR_UNEXPECTED_EOF;
	}

	// check ioq3 joint limit
	if (header->num_joints > IQM_MAX_JOINTS)
	{
		Com_SetLastError(va("R_LoadIQM: %s has more than %d joints (%d).",
			mod_name, IQM_MAX_JOINTS, header->num_joints));
		return Q_ERR_INVALID_FORMAT;
	}

	for (uint32_t vertexarray_idx = 0; vertexarray_idx < q_countof(vertexArrayFormat); vertexarray_idx++)
	{
		vertexArrayFormat[vertexarray_idx] = -1;
	}

	if (header->num_meshes)
	{
		// check vertex arrays
		if (IQM_CheckRange(header, header->ofs_vertexarrays, header->num_vertexarrays, sizeof(iqmVertexArray_t)))
		{
			Com_SetLastError("data out of bounds");
			return Q_ERR_INVALID_FORMAT;
		}
		const iqmVertexArray_t* vertexarray = (const iqmVertexArray_t*)((const byte*)header + header->ofs_vertexarrays);
		for (uint32_t vertexarray_idx = 0; vertexarray_idx < header->num_vertexarrays; vertexarray_idx++, vertexarray++)
		{
			if (vertexarray->size <= 0 || vertexarray->size > 4)
			{
				return Q_ERR_INVALID_FORMAT;
			}
			
			uint32_t num_values = header->num_vertexes * vertexarray->size;

			switch (vertexarray->format) {
			case IQM_BYTE:
			case IQM_UBYTE:
				// 1-byte
				if (IQM_CheckRange(header, vertexarray->offset, num_values, sizeof(byte)))
				{
					Com_SetLastError("data out of bounds");
					return Q_ERR_INVALID_FORMAT;
				}
				break;
			case IQM_INT:
			case IQM_UINT:
			case IQM_FLOAT:
				// 4-byte
				if (IQM_CheckRange(header, vertexarray->offset, num_values, sizeof(float)))
				{
					Com_SetLastError("data out of bounds");
					return Q_ERR_INVALID_FORMAT;
				}
				break;
			default:
				// not supported
				return Q_ERR_INVALID_FORMAT;
			}

			if (vertexarray->type < q_countof(vertexArrayFormat))
			{
				vertexArrayFormat[vertexarray->type] = (int)vertexarray->format;
			}

			switch (vertexarray->type)
			{
			case IQM_POSITION:
			case IQM_NORMAL:
				if (vertexarray->format != IQM_FLOAT ||
					vertexarray->size != 3)
				{
					return Q_ERR_INVALID_FORMAT;
				}
				break;
			case IQM_TANGENT:
				if (vertexarray->format != IQM_FLOAT ||
					vertexarray->size != 4)
				{
					return Q_ERR_INVALID_FORMAT;
				}
				break;
			case IQM_TEXCOORD:
				if (vertexarray->format != IQM_FLOAT ||
					vertexarray->size != 2)
				{
					return Q_ERR_INVALID_FORMAT;
				}
				break;
			case IQM_BLENDINDEXES:
				if ((vertexarray->format != IQM_INT &&
					vertexarray->format != IQM_UBYTE) ||
					vertexarray->size != 4)
				{
					return Q_ERR_INVALID_FORMAT;
				}
				break;
			case IQM_BLENDWEIGHTS:
				if ((vertexarray->format != IQM_FLOAT &&
					vertexarray->format != IQM_UBYTE) ||
					vertexarray->size != 4)
				{
					return Q_ERR_INVALID_FORMAT;
				}
				break;
			case IQM_COLOR:
				if (vertexarray->format != IQM_UBYTE ||
					vertexarray->size != 4)
				{
					return Q_ERR_INVALID_FORMAT;
				}
				break;
			default:
				break;
			}
		}

		// check for required vertex arrays
		if (vertexArrayFormat[IQM_POSITION] == -1 || vertexArrayFormat[IQM_NORMAL] == -1 || vertexArrayFormat[IQM_TEXCOORD] == -1)
		{
			Com_SetLastError(va("R_LoadIQM: %s is missing IQM_POSITION, IQM_NORMAL, and/or IQM_TEXCOORD array.", mod_name));
			return Q_ERR_INVALID_FORMAT;
		}

		if (header->num_joints)
		{
			if (vertexArrayFormat[IQM_BLENDINDEXES] == -1 || vertexArrayFormat[IQM_BLENDWEIGHTS] == -1)
			{
				Com_SetLastError(va("R_LoadIQM: %s is missing IQM_BLENDINDEXES and/or IQM_BLENDWEIGHTS array.", mod_name));
				return Q_ERR_INVALID_FORMAT;
			}
		}
		else
		{
			// ignore blend arrays if present
			vertexArrayFormat[IQM_BLENDINDEXES] = -1;
			vertexArrayFormat[IQM_BLENDWEIGHTS] = -1;
		}

		// check triangles
		if (IQM_CheckRange(header, header->ofs_triangles, header->num_triangles, sizeof(iqmTriangle_t)))
		{
			Com_SetLastError("data out of bounds");
			return Q_ERR_INVALID_FORMAT;
		}
		const iqmTriangle_t* triangle = (const iqmTriangle_t*)((const byte*)header + header->ofs_triangles);
		for (uint32_t triangle_idx = 0; triangle_idx < header->num_triangles; triangle_idx++, triangle++)
		{
			if (triangle->vertex[0] > header->num_vertexes ||
				triangle->vertex[1] > header->num_vertexes ||
				triangle->vertex[2] > header->num_vertexes) {
				return Q_ERR_INVALID_FORMAT;
			}
		}

		// check meshes
		if (IQM_CheckRange(header, header->ofs_meshes, header->num_meshes, sizeof(iqmMesh_t)))
		{
			Com_SetLastError("data out of bounds");
			return Q_ERR_INVALID_FORMAT;
		}
		
		const iqmMesh_t* mesh = (const iqmMesh_t*)((const byte*)header + header->ofs_meshes);
		for (uint32_t mesh_idx = 0; mesh_idx < header->num_meshes; mesh_idx++, mesh++)
		{
			if (mesh->name < header->num_text)
			{
				strncpy(meshName, (const char*)header + header->ofs_text + mesh->name, sizeof(meshName) - 1);
			}
			else
			{
				meshName[0] = '\0';
			}

			if (mesh->first_vertex >= header->num_vertexes ||
				mesh->first_vertex + mesh->num_vertexes > header->num_vertexes ||
				mesh->first_triangle >= header->num_triangles ||
				mesh->first_triangle + mesh->num_triangles > header->num_triangles ||
				mesh->name >= header->num_text ||
				mesh->material >= header->num_text) {
				return Q_ERR_INVALID_FORMAT;
			}
		}
	}

	if (header->num_poses != header->num_joints && header->num_poses != 0)
	{
		Com_SetLastError(va("R_LoadIQM: %s has %d poses and %d joints, must have the same number or 0 poses",
			mod_name, header->num_poses, header->num_joints));
		return Q_ERR_INVALID_FORMAT;
	}

	joint_names = 0;

	if (header->num_joints)
	{
		// check joints
		if (IQM_CheckRange(header, header->ofs_joints, header->num_joints, sizeof(iqmJoint_t)))
		{
			Com_SetLastError("data out of bounds");
			return Q_ERR_INVALID_FORMAT;
		}
		
		const iqmJoint_t* joint = (const iqmJoint_t*)((const byte*)header + header->ofs_joints);
		for (uint32_t joint_idx = 0; joint_idx < header->num_joints; joint_idx++, joint++)
		{
			if (joint->parent < -1 ||
				joint->parent >= (int)header->num_joints ||
				joint->name >= header->num_text) {
				return Q_ERR_INVALID_FORMAT;
			}
			joint_names += strlen((const char*)header + header->ofs_text +
				joint->name) + 1;
		}
	}

	if (header->num_poses)
	{
		// check poses
		if (IQM_CheckRange(header, header->ofs_poses, header->num_poses, sizeof(iqmPose_t)))
		{
			Com_SetLastError("data out of bounds");
			return Q_ERR_INVALID_FORMAT;
		}
	}

	if (header->ofs_bounds)
	{
		// check model bounds
		if (IQM_CheckRange(header, header->ofs_bounds, header->num_frames, sizeof(iqmBounds_t)))
		{
			Com_SetLastError("data out of bounds");
			return Q_ERR_INVALID_FORMAT;
		}
	}

	if (header->num_anims)
	{
		// check animations
		const iqmAnim_t* anim = (const iqmAnim_t*)((const byte*)header + header->ofs_anims);
		for (uint32_t anim_idx = 0; anim_idx < header->num_anims; anim_idx++, anim++)
		{
			if (anim->first_frame + anim->num_frames > header->num_frames)
			{
				return Q_ERR_INVALID_FORMAT;
			}
		}
	}

	CHECK(iqmData = MOD_Malloc(sizeof(iqm_model_t)));
	model->iqmData = iqmData;

	// fill header
	iqmData->num_vertexes = (header->num_meshes > 0) ? header->num_vertexes : 0;
	iqmData->num_triangles = (header->num_meshes > 0) ? header->num_triangles : 0;
	iqmData->num_frames = header->num_frames;
	iqmData->num_meshes = header->num_meshes;
	iqmData->num_joints = header->num_joints;
	iqmData->num_poses = header->num_poses;

	if (header->num_meshes)
	{
		CHECK(iqmData->meshes = MOD_Malloc(header->num_meshes * sizeof(iqm_mesh_t)));
		CHECK(iqmData->indices = MOD_Malloc(header->num_triangles * 3 * sizeof(int)));
		CHECK(iqmData->positions = MOD_Malloc(header->num_vertexes * 3 * sizeof(float)));
		CHECK(iqmData->texcoords = MOD_Malloc(header->num_vertexes * 2 * sizeof(float)));
		CHECK(iqmData->normals = MOD_Malloc(header->num_vertexes * 3 * sizeof(float)));

		if (vertexArrayFormat[IQM_TANGENT] != -1)
		{
			CHECK(iqmData->tangents = MOD_Malloc(header->num_vertexes * 4 * sizeof(float)));
		}

		if (vertexArrayFormat[IQM_COLOR] != -1)
		{
			CHECK(iqmData->colors = MOD_Malloc(header->num_vertexes * 4 * sizeof(byte)));
		}

		if (vertexArrayFormat[IQM_BLENDINDEXES] != -1)
		{
			CHECK(iqmData->blend_indices = MOD_Malloc(header->num_vertexes * 4 * sizeof(byte)));
		}

		if (vertexArrayFormat[IQM_BLENDWEIGHTS] != -1)
		{
			CHECK(iqmData->blend_weights = MOD_Malloc(header->num_vertexes * 4 * sizeof(byte)));
		}
	}

	if (header->num_joints)
	{
		CHECK(iqmData->jointNames = MOD_Malloc(joint_names));
		CHECK(iqmData->jointParents = MOD_Malloc(header->num_joints * sizeof(int)));
		CHECK(iqmData->bindJoints = MOD_Malloc(header->num_joints * 12 * sizeof(float))); // bind joint matricies
		CHECK(iqmData->invBindJoints = MOD_Malloc(header->num_joints * 12 * sizeof(float))); // inverse bind joint matricies
	}
	
	if (header->num_poses)
	{
		CHECK(iqmData->poses = MOD_Malloc(header->num_poses * header->num_frames * sizeof(iqm_transform_t))); // pose transforms
	}
	
	if (header->ofs_bounds)
	{
		CHECK(iqmData->bounds = MOD_Malloc(header->num_frames * 6 * sizeof(float))); // model bounds
	}
	else if (header->num_meshes && header->num_frames == 0)
	{
		CHECK(iqmData->bounds = MOD_Malloc(6 * sizeof(float))); // model bounds
	}
	
	if (header->num_meshes)
	{
		const iqmMesh_t* mesh = (const iqmMesh_t*)((const byte*)header + header->ofs_meshes);
		iqm_mesh_t* surface = iqmData->meshes;
		const char* str = (const char*)header + header->ofs_text;
		for (uint32_t mesh_idx = 0; mesh_idx < header->num_meshes; mesh_idx++, mesh++, surface++)
		{
			strncpy(surface->name, str + mesh->name, sizeof(surface->name) - 1);
			Q_strlwr(surface->name); // lowercase the surface name so skin compares are faster
			strncpy(surface->material, str + mesh->material, sizeof(surface->material) - 1);
			Q_strlwr(surface->material);
			surface->data = iqmData;
			surface->first_vertex = mesh->first_vertex;
			surface->num_vertexes = mesh->num_vertexes;
			surface->first_triangle = mesh->first_triangle;
			surface->num_triangles = mesh->num_triangles;
		}

		// copy triangles
		const iqmTriangle_t* triangle = (const iqmTriangle_t*)((const byte*)header + header->ofs_triangles);
		for (uint32_t i = 0; i < header->num_triangles; i++, triangle++)
		{
			iqmData->indices[3 * i + 0] = triangle->vertex[0];
			iqmData->indices[3 * i + 1] = triangle->vertex[1];
			iqmData->indices[3 * i + 2] = triangle->vertex[2];
		}

		// copy vertexarrays and indexes
		const iqmVertexArray_t* vertexarray = (const iqmVertexArray_t*)((const byte*)header + header->ofs_vertexarrays);
		for (uint32_t vertexarray_idx = 0; vertexarray_idx < header->num_vertexarrays; vertexarray_idx++, vertexarray++)
		{
			// skip disabled arrays
			if (vertexarray->type < q_countof(vertexArrayFormat)
				&& vertexArrayFormat[vertexarray->type] == -1)
				continue;

			// total number of values
			uint32_t n = header->num_vertexes * vertexarray->size;

			switch (vertexarray->type)
			{
			case IQM_POSITION:
				memcpy(iqmData->positions,
					(const byte*)header + vertexarray->offset,
					n * sizeof(float));
				break;
			case IQM_NORMAL:
				memcpy(iqmData->normals,
					(const byte*)header + vertexarray->offset,
					n * sizeof(float));
				break;
			case IQM_TANGENT:
				memcpy(iqmData->tangents,
					(const byte*)header + vertexarray->offset,
					n * sizeof(float));
				break;
			case IQM_TEXCOORD:
				memcpy(iqmData->texcoords,
					(const byte*)header + vertexarray->offset,
					n * sizeof(float));
				break;
			case IQM_BLENDINDEXES:
				if (vertexArrayFormat[IQM_BLENDINDEXES] == IQM_UBYTE)
				{
					memcpy(iqmData->blend_indices,
						(const byte*)header + vertexarray->offset,
						n * sizeof(byte));
				}
				else if (vertexArrayFormat[IQM_BLENDINDEXES] == IQM_INT)
				{
					const int* indices = (const int*)((const byte*)header + vertexarray->offset);

					// Convert blend indices from int to byte
					for (uint32_t index_idx = 0; index_idx < n; index_idx++)
					{
						int index = indices[index_idx];
						iqmData->blend_indices[index_idx] = (byte)index;
					}
				}
				else
				{
					// The formats are validated before loading the data.
					assert(!"Unsupported IQM_BLENDINDEXES format");
					memset(iqmData->blend_indices, 0, n * sizeof(byte));
				}
				break;
			case IQM_BLENDWEIGHTS:
				if (vertexArrayFormat[IQM_BLENDWEIGHTS] == IQM_UBYTE)
				{
					memcpy(iqmData->blend_weights,
						(const byte*)header + vertexarray->offset,
						n * sizeof(byte));
				}
				else if(vertexArrayFormat[IQM_BLENDWEIGHTS] == IQM_FLOAT)
				{
					const float* weights = (const float*)((const byte*)header + vertexarray->offset);

					// Convert blend weights from float to byte
					for (uint32_t weight_idx = 0; weight_idx < n; weight_idx++)
					{
						float integer_weight = Q_clipf(weights[weight_idx] * 255.f, 0.f, 255.f);
						iqmData->blend_weights[weight_idx] = (byte)integer_weight;
					}
				}
				else
				{
					// The formats are validated before loading the data.
					assert(!"Unsupported IQM_BLENDWEIGHTS format");
					memset(iqmData->blend_weights, 0, n * sizeof(byte));
				}
				break;
			case IQM_COLOR:
				memcpy(iqmData->colors,
					(const byte*)header + vertexarray->offset,
					n * sizeof(byte));
				break;
			default:
				break;
			}
		}
	}

	if (header->num_joints)
	{
		// copy joint names
		char* str = iqmData->jointNames;
		const iqmJoint_t* joint = (const iqmJoint_t*)((const byte*)header + header->ofs_joints);
		for (uint32_t joint_idx = 0; joint_idx < header->num_joints; joint_idx++, joint++)
		{
			const char* name = (const char*)header + header->ofs_text + joint->name;
			size_t len = strlen(name) + 1;
			memcpy(str, name, len);
			str += len;
		}

		// copy joint parents
		joint = (const iqmJoint_t*)((const byte*)header + header->ofs_joints);
		for (uint32_t joint_idx = 0; joint_idx < header->num_joints; joint_idx++, joint++)
		{
			iqmData->jointParents[joint_idx] = joint->parent;
		}

		// calculate bind joint matrices and their inverses
		mat = iqmData->bindJoints;
		matInv = iqmData->invBindJoints;
		joint = (const iqmJoint_t*)((const byte*)header + header->ofs_joints);
		for (uint32_t joint_idx = 0; joint_idx < header->num_joints; joint_idx++, joint++)
		{
			float baseFrame[12], invBaseFrame[12];

			quat_t rotate;
			QuatNormalize2(joint->rotate, rotate);

			JointToMatrix(rotate, joint->scale, joint->translate, baseFrame);
			Matrix34Invert(baseFrame, invBaseFrame);

			if (joint->parent >= 0)
			{
				Matrix34Multiply(iqmData->bindJoints + 12 * joint->parent, baseFrame, mat);
				mat += 12;
				Matrix34Multiply(invBaseFrame, iqmData->invBindJoints + 12 * joint->parent, matInv);
				matInv += 12;
			}
			else
			{
				memcpy(mat, baseFrame, sizeof(baseFrame));
				mat += 12;
				memcpy(matInv, invBaseFrame, sizeof(invBaseFrame));
				matInv += 12;
			}
		}
	}

	if (header->num_poses)
	{
		// calculate pose transforms
		transform = iqmData->poses;
		const uint16_t* framedata = (const uint16_t*)((const byte*)header + header->ofs_frames);
		for (uint32_t frame_idx = 0; frame_idx < header->num_frames; frame_idx++)
		{
			const iqmPose_t* pose = (const iqmPose_t*)((const byte*)header + header->ofs_poses);
			for (uint32_t pose_idx = 0; pose_idx < header->num_poses; pose_idx++, pose++, transform++)
			{
				vec3_t translate;
				quat_t rotate;
				vec3_t scale;

				translate[0] = pose->channeloffset[0]; if (pose->mask & 0x001) translate[0] += (float)*framedata++ * pose->channelscale[0];
				translate[1] = pose->channeloffset[1]; if (pose->mask & 0x002) translate[1] += (float)*framedata++ * pose->channelscale[1];
				translate[2] = pose->channeloffset[2]; if (pose->mask & 0x004) translate[2] += (float)*framedata++ * pose->channelscale[2];

				rotate[0] = pose->channeloffset[3]; if (pose->mask & 0x008) rotate[0] += (float)*framedata++ * pose->channelscale[3];
				rotate[1] = pose->channeloffset[4]; if (pose->mask & 0x010) rotate[1] += (float)*framedata++ * pose->channelscale[4];
				rotate[2] = pose->channeloffset[5]; if (pose->mask & 0x020) rotate[2] += (float)*framedata++ * pose->channelscale[5];
				rotate[3] = pose->channeloffset[6]; if (pose->mask & 0x040) rotate[3] += (float)*framedata++ * pose->channelscale[6];

				scale[0] = pose->channeloffset[7]; if (pose->mask & 0x080) scale[0] += (float)*framedata++ * pose->channelscale[7];
				scale[1] = pose->channeloffset[8]; if (pose->mask & 0x100) scale[1] += (float)*framedata++ * pose->channelscale[8];
				scale[2] = pose->channeloffset[9]; if (pose->mask & 0x200) scale[2] += (float)*framedata++ * pose->channelscale[9];

				VectorCopy(translate, transform->translate);
				QuatNormalize2(rotate, transform->rotate);
				VectorCopy(scale, transform->scale);
			}
		}
	}

	// copy model bounds
	if (header->ofs_bounds)
	{
		mat = iqmData->bounds;
		const iqmBounds_t* bounds = (const iqmBounds_t*)((const byte*)header + header->ofs_bounds);
		for (uint32_t frame_idx = 0; frame_idx < header->num_frames; frame_idx++)
		{
			mat[0] = bounds->bbmin[0];
			mat[1] = bounds->bbmin[1];
			mat[2] = bounds->bbmin[2];
			mat[3] = bounds->bbmax[0];
			mat[4] = bounds->bbmax[1];
			mat[5] = bounds->bbmax[2];

			mat += 6;
			bounds++;
		}
	}
	else if (header->num_meshes && header->num_frames == 0)
	{
		mat = iqmData->bounds;

		ClearBounds(&iqmData->bounds[0], &iqmData->bounds[3]);
		for (uint32_t vertex_idx = 0; vertex_idx < header->num_vertexes; vertex_idx++)
		{
			AddPointToBounds(&iqmData->positions[vertex_idx * 3], &iqmData->bounds[0], &iqmData->bounds[3]);
		}
	}

	if (header->num_anims)
	{
		iqmData->num_animations = header->num_anims;
		CHECK(iqmData->animations = MOD_Malloc(header->num_anims * sizeof(iqm_anim_t)));

		const iqmAnim_t* src = (const iqmAnim_t*)((const byte*)header + header->ofs_anims);
		iqm_anim_t* dst = iqmData->animations;
		for (uint32_t anim_idx = 0; anim_idx < header->num_anims; anim_idx++, src++, dst++)
		{
			const char* name = (const char*)header + header->ofs_text + src->name;
			strncpy(dst->name, name, sizeof(dst->name));
			dst->name[sizeof(dst->name) - 1] = 0;
			
			dst->first_frame = src->first_frame;
			dst->num_frames = src->num_frames;
			dst->loop = (src->flags & IQM_LOOP) != 0;
		}
	}

	return Q_ERR_SUCCESS;

fail:
	return ret;
}

/*
=================
R_ComputeIQMTransforms

Compute matrices for this model, returns [model->num_poses] 3x4 matrices in the (pose_matrices) array
=================
*/
bool R_ComputeIQMTransforms(const iqm_model_t* model, const entity_t* entity, float* pose_matrices)
{
	iqm_transform_t relativeJoints[IQM_MAX_JOINTS];

	iqm_transform_t* relativeJoint = relativeJoints;

	const int frame = model->num_frames ? entity->frame % (int)model->num_frames : 0;
	const int oldframe = model->num_frames ? entity->oldframe % (int)model->num_frames : 0;
	const float backlerp = entity->backlerp;

	// copy or lerp animation frame pose
	if (oldframe == frame)
	{
		const iqm_transform_t* pose = &model->poses[frame * model->num_poses];
		for (uint32_t pose_idx = 0; pose_idx < model->num_poses; pose_idx++, pose++, relativeJoint++)
		{
			VectorCopy(pose->translate, relativeJoint->translate);
			QuatCopy(pose->rotate, relativeJoint->rotate);
			VectorCopy(pose->scale, relativeJoint->scale);
		}
	}
	else
	{
		const float lerp = 1.0f - backlerp;
		const iqm_transform_t* pose = &model->poses[frame * model->num_poses];
		const iqm_transform_t* oldpose = &model->poses[oldframe * model->num_poses];
		for (uint32_t pose_idx = 0; pose_idx < model->num_poses; pose_idx++, oldpose++, pose++, relativeJoint++)
		{
			relativeJoint->translate[0] = oldpose->translate[0] * backlerp + pose->translate[0] * lerp;
			relativeJoint->translate[1] = oldpose->translate[1] * backlerp + pose->translate[1] * lerp;
			relativeJoint->translate[2] = oldpose->translate[2] * backlerp + pose->translate[2] * lerp;

			relativeJoint->scale[0] = oldpose->scale[0] * backlerp + pose->scale[0] * lerp;
			relativeJoint->scale[1] = oldpose->scale[1] * backlerp + pose->scale[1] * lerp;
			relativeJoint->scale[2] = oldpose->scale[2] * backlerp + pose->scale[2] * lerp;

			QuatSlerp(oldpose->rotate, pose->rotate, lerp, relativeJoint->rotate);
		}
	}

	// multiply by inverse of bind pose and parent 'pose mat' (bind pose transform matrix)
	relativeJoint = relativeJoints;
	const int* jointParent = model->jointParents;
	const float* invBindMat = model->invBindJoints;
	float* poseMat = pose_matrices;
	for (uint32_t pose_idx = 0; pose_idx < model->num_poses; pose_idx++, relativeJoint++, jointParent++, invBindMat += 12, poseMat += 12)
	{
		float mat1[12], mat2[12];

		JointToMatrix(relativeJoint->rotate, relativeJoint->scale, relativeJoint->translate, mat1);

		if (*jointParent >= 0)
		{
			Matrix34Multiply(&model->bindJoints[(*jointParent) * 12], mat1, mat2);
			Matrix34Multiply(mat2, invBindMat, mat1);
			Matrix34Multiply(&pose_matrices[(*jointParent) * 12], mat1, poseMat);
		}
		else
		{
			Matrix34Multiply(mat1, invBindMat, poseMat);
		}
	}

	return true;
}
