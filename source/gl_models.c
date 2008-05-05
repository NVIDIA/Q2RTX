/*
Copyright (C) 2003-2006 Andrey Nazarov

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "gl_local.h"

#define Model_Malloc( size )	sys.HunkAlloc( &model->pool, size )

static model_t		r_models[MAX_MODELS];
static int			r_numModels;

static byte         ll2byte[256][256];

static cvar_t       *gl_override_models;

static model_t *Model_Alloc( const char *name ) {
	model_t *model;
	int i;

	for( i = 0, model = r_models; i < r_numModels; i++, model++ ) {
		if( !model->name[0] ) {
			break;
		}
	}

	if( i == r_numModels ) {
		if( r_numModels == MAX_MODELS ) {
			Com_Error( ERR_DROP, "Model_Alloc: MAX_MODELS" );
		}
		r_numModels++;
	}

	strcpy( model->name, name );
	model->registration_sequence = registration_sequence;
	model->type = MODEL_NULL;

	return model;
}

static model_t *Model_Find( const char *name ) {
	model_t *model;
	aliasMesh_t *mesh;
	int i, j;

	for( i = 0, model = r_models; i < r_numModels; i++, model++ ) {
		if( !model->name[0] ) {
			continue;
		}
		if( !Q_stricmp( model->name, name ) ) {
			break;
		}
	}

	if( i == r_numModels ) {
		return NULL;
	}

	switch( model->type ) {
	case MODEL_ALIAS:
		for( i = 0; i < model->numMeshes; i++ ) {
			mesh = &model->meshes[i];
			for( j = 0; j < mesh->numSkins; j++ ) {
				mesh->skins[j]->registration_sequence = registration_sequence;
			}
		}
		break;
	case MODEL_SPRITE:
		for( i = 0; i < model->numFrames; i++ ) {
			model->sframes[i].image->registration_sequence = registration_sequence;
		}
		break;
	default:
		Com_Error( ERR_DROP, "Model_Find: bad model type: %d", model->type );
		break;
	}

	model->registration_sequence = registration_sequence;
	return model;
}

static void Model_List_f( void ) {
	int		i;
	model_t	*model;
	int		bytes;

	Com_Printf( "------------------\n");
	bytes = 0;

	for( i = 0, model = r_models; i < r_numModels; i++, model++ ) {
		if( !model->name[0] ) {
			continue;
		}

		Com_Printf( "%8"PRIz" : %s\n", model->pool.cursize, model->name );
		bytes += model->pool.cursize;
	}
	Com_Printf( "Total resident: %i\n", bytes );
}

void Model_FreeUnused( void ) {
	model_t *model;
	int i;

	for( i = 0, model = r_models; i < r_numModels; i++, model++ ) {
		if( !model->name[0] ) {
			continue;
		}
		if( model->registration_sequence != registration_sequence ) {
			sys.HunkFree( &model->pool );
			model->name[0] = 0;
		}
	}
}

void Model_FreeAll( void ) {
	model_t *model;
	int i;

	for( i = 0, model = r_models; i < r_numModels; i++, model++ ) {
		if( !model->name[0] ) {
			continue;
		}

		sys.HunkFree( &model->pool );
		model->name[0] = 0;
	}

	r_numModels = 0;
}

static qboolean Model_LoadMd2( model_t *model, const byte *rawdata, int length ) {
	dmdl_t header;
	daliasframe_t *src_frame;
	dtrivertx_t *src_vert;
	dtriangle_t *src_tri;
	dstvert_t *src_tc;
	aliasFrame_t *dst_frame;
	aliasVert_t *dst_vert;
	aliasMesh_t *dst_mesh;
	uint32_t *finalIndices;
	tcoord_t *dst_tc;
	int i, j, k;
	uint16_t remap[MAX_TRIANGLES*3];
	uint16_t vertIndices[MAX_TRIANGLES*3];
	uint16_t tcIndices[MAX_TRIANGLES*3];
	int numVerts, numIndices;
	char skinname[MAX_QPATH];
	char *src_skin;
	image_t *skin;
	vec_t scaleS, scaleT;
	int val;
	vec3_t mins, maxs;
	const byte *rawend;

#define ERR( x ) \
    Com_WPrintf( "LoadMD2: %s: " x "\n", model->name )

	if( length < sizeof( header ) ) {
		ERR( "file too short" );
		return qfalse;
	}

	/* byte swap the header */
	header = *( dmdl_t * )rawdata;
	for( i = 0; i < sizeof( header )/4; i++ ) {
		(( uint32_t * )&header)[i] = LittleLong( (( uint32_t * )&header)[i] );
	}

    // check ident and version
	if( header.ident != IDALIASHEADER ) {
		ERR( "not an MD2 file" );
		return qfalse;
	}
	if( header.version != ALIAS_VERSION ) {
        ERR( "bad version" );
		return qfalse;
	}

    // check triangles
	if( header.num_tris < 1 || header.num_tris > MAX_TRIANGLES ) {
		ERR( "bad number of triangles" );
		return qfalse;
	}
    if( header.ofs_tris < sizeof( header ) ||
        header.ofs_tris + sizeof( dtriangle_t ) * header.num_tris > length )
    {
		ERR( "bad triangles offset" );
		return qfalse;
    }

    // check st
	if( header.num_st < 3 || header.num_st > MAX_VERTS ) {
		ERR( "bad number of st" );
		return qfalse;
	}
    if( header.ofs_st < sizeof( header ) ||
        header.ofs_st + sizeof( dstvert_t ) * header.num_st > length )
    {
		ERR( "bad st offset" );
		return qfalse;
    }

    // check xyz and frames
	if( header.num_xyz < 3 || header.num_xyz > MAX_VERTS ) {
		ERR( "bad number of xyz" );
		return qfalse;
	}
	if( header.num_frames < 1 || header.num_frames > MAX_FRAMES ) {
		ERR( "bad number of frames" );
		return qfalse;
	}
    if( header.framesize < sizeof( daliasframe_t ) + ( header.num_xyz - 1 ) * sizeof( dtrivertx_t ) ||
        header.framesize > MAX_FRAMESIZE )
    {
		ERR( "bad frame size" );
		return qfalse;
    }
    if( header.ofs_frames < sizeof( header ) ||
        header.ofs_frames + header.framesize * header.num_frames > length )
    {
		ERR( "bad frames offset" );
		return qfalse;
    }

    // check skins
	if( header.num_skins < 0 || header.num_skins > MAX_MD2SKINS ) {
		ERR( "bad number of skins" );
		return qfalse;
	}
	if( header.num_skins && ( header.ofs_skins < sizeof( header ) ||
        header.ofs_skins + MAX_SKINNAME * header.num_skins > length ) )
    {
		ERR( "bad skins offset" );
		return qfalse;
	}
	if( header.skinwidth <= 0 || header.skinheight <= 0 ) {
		ERR( "bad skin dimensions" );
		return qfalse;
	}

	rawend = rawdata + length;

	model->type = MODEL_ALIAS;
	sys.HunkBegin( &model->pool, 0x400000 );

	/* load all triangle indices */
	src_tri = ( dtriangle_t * )( ( byte * )rawdata + header.ofs_tris );
	for( i = 0; i < header.num_tris; i++ ) {
        for( j = 0; j < 3; j++ ) {
            uint16_t idx_xyz = LittleShort( src_tri->index_xyz[j] );
            uint16_t idx_st = LittleShort( src_tri->index_st[j] );

            if( idx_xyz > header.num_xyz || idx_st > header.num_st ) {
		        ERR( "bad indices" );
		        goto fail;
            }

            vertIndices[i*3+j] = idx_xyz; 
            tcIndices[i*3+j] = idx_st;
        }
		src_tri++;
	}

	numIndices = header.num_tris * 3;

	model->meshes = Model_Malloc( sizeof( aliasMesh_t ) );
	model->numMeshes = 1;

	dst_mesh = model->meshes;
	dst_mesh->indices = Model_Malloc( numIndices * sizeof( uint32_t ) );
	dst_mesh->numTris = header.num_tris;
	dst_mesh->numIndices = numIndices;

	for( i = 0; i < numIndices; i++ ) {
		remap[i] = 0xFFFF;
	}

	numVerts = 0;
	src_tc = ( dstvert_t * )( ( byte * )rawdata + header.ofs_st );
	finalIndices = dst_mesh->indices;
	for( i = 0; i < numIndices; i++ ) {
		if( remap[i] != 0xFFFF ) {
			continue; /* already remapped */
		}

		for( j = i + 1; j < numIndices; j++ ) {
			if( vertIndices[i] == vertIndices[j] &&
				( src_tc[tcIndices[i]].s == src_tc[tcIndices[j]].s &&
					src_tc[tcIndices[i]].t == src_tc[tcIndices[j]].t ) )
			{
				/* duplicate vertex */
				remap[j] = i;
				finalIndices[j] = numVerts;
			} 
		}

		/* new vertex */
		remap[i] = i;
		finalIndices[i] = numVerts++;
	}

	dst_mesh->verts = Model_Malloc( numVerts * header.num_frames * sizeof( aliasVert_t ) );
	dst_mesh->tcoords = Model_Malloc( numVerts * sizeof( tcoord_t ) );
	dst_mesh->numVerts = numVerts;

	/* load all skins */
	src_skin = ( char * )rawdata + header.ofs_skins;
	for( i = 0; i < header.num_skins; i++ ) {
		Q_strncpyz( skinname, src_skin, sizeof( skinname ) );
		skin = R_FindImage( skinname, it_skin );
		if( !skin ) {
			skin = r_notexture;
		}
		dst_mesh->skins[i] = skin;
		src_skin += MAX_SKINNAME;
	}
	dst_mesh->numSkins = header.num_skins;

	/* load all tcoords */
	src_tc = ( dstvert_t * )( ( byte * )rawdata + header.ofs_st );
	dst_tc = dst_mesh->tcoords;
	skin = dst_mesh->skins[0];
	scaleS = 1.0f / header.skinwidth;
	scaleT = 1.0f / header.skinheight;
	for( i = 0; i < numIndices; i++ ) {
		if( remap[i] == i ) {
            float s = ( signed short )LittleShort( src_tc[ tcIndices[i] ].s );
            float t = ( signed short )LittleShort( src_tc[ tcIndices[i] ].t );

			dst_tc[ finalIndices[i] ].st[0] = s * scaleS;
			dst_tc[ finalIndices[i] ].st[1] = t * scaleT;
		}
	}

	/* load all frames */
	model->frames = Model_Malloc( header.num_frames * sizeof( aliasFrame_t ) );
	model->numFrames = header.num_frames;

	src_frame = ( daliasframe_t * )( ( byte * )rawdata + header.ofs_frames );
	dst_frame = model->frames;
	for( j = 0; j < header.num_frames; j++ ) {
		LittleVector( src_frame->scale, dst_frame->scale );
		LittleVector( src_frame->translate, dst_frame->translate );

		/* load frame vertices */
		ClearBounds( mins, maxs );
		for( i = 0; i < numIndices; i++ ) {
			if( remap[i] == i ) {
				src_vert = &src_frame->verts[ vertIndices[i] ];
				dst_vert = &dst_mesh->verts[ j * numVerts + finalIndices[i] ];

				dst_vert->pos[0] = src_vert->v[0];
				dst_vert->pos[1] = src_vert->v[1];
				dst_vert->pos[2] = src_vert->v[2];
				k = src_vert->lightnormalindex;
				if( k >= NUMVERTEXNORMALS ) {
					ERR( "bad normal index" );
					goto fail;
				}
				dst_vert->normalIndex = k;

				for ( k = 0; k < 3; k++ ) {
					val = dst_vert->pos[k];
					if( val < mins[k] )
						mins[k] = val;
					if( val > maxs[k] )
						maxs[k] = val;
				}
			}
		}

		VectorVectorScale( mins, dst_frame->scale, mins );
		VectorVectorScale( maxs, dst_frame->scale, maxs );

		dst_frame->radius = RadiusFromBounds( mins, maxs );

		VectorAdd( mins, dst_frame->translate, dst_frame->bounds[0] );
		VectorAdd( maxs, dst_frame->translate, dst_frame->bounds[1] );

		src_frame = ( daliasframe_t * )( ( byte * )src_frame + header.framesize );
		dst_frame++;
	}

    sys.HunkEnd( &model->pool );
	return qtrue;

fail:
	sys.HunkFree( &model->pool );
	return qfalse;

#undef ERR
}

static qboolean Model_LoadMd3( model_t *model, const byte *rawdata, int length ) {
	dmd3header_t header;
	uint32_t offset;
	dmd3frame_t *src_frame;
	dmd3mesh_t *src_mesh;
	dmd3vertex_t *src_vert;
	dmd3coord_t *src_tc;
	dmd3skin_t *src_skin;
	uint32_t *src_idx;
	aliasFrame_t *dst_frame;
	aliasMesh_t *dst_mesh;
	aliasVert_t *dst_vert;
	tcoord_t *dst_tc;
	uint32_t *dst_idx;
	uint32_t numVerts, numTris, numSkins;
	uint32_t totalVerts;
	char skinname[MAX_QPATH];
	image_t *skin;
	const byte *rawend;
	int i, j;

	if( length < sizeof( header ) ) {
		Com_WPrintf( "%s has length < header length\n", model->name );
		return qfalse;
	}

	/* byte swap the header */
	header = *( dmd3header_t * )rawdata;
	for( i = 0; i < sizeof( header )/4; i++ ) {
		(( uint32_t * )&header)[i] = LittleLong( (( uint32_t * )&header)[i] );
	}

	if( header.ident != MD3_IDENT ) {
		Com_WPrintf( "%s is not an MD3 file\n", model->name );
		return qfalse;
	}

	if( header.version != MD3_VERSION ) {
		Com_WPrintf( "%s has bad version: %d != %d\n",
			model->name, header.version, MD3_VERSION );
		return qfalse;
	}

	if( header.num_frames < 1 ) {
		Com_WPrintf( "%s has bad number of frames: %d\n",
			model->name, header.num_frames );
		return qfalse;
	}
	if( header.num_frames > MD3_MAX_FRAMES ) {
		Com_WPrintf( "%s has too many frames: %d > %d\n",
			model->name, header.num_frames, MD3_MAX_FRAMES );
		return qfalse;
	}

	if( header.num_meshes < 1 || header.num_meshes > MD3_MAX_SURFACES ) {
		Com_WPrintf( "%s has bad number of meshes\n", model->name );
		return qfalse;
	}
	
	model->type = MODEL_ALIAS;
	sys.HunkBegin( &model->pool, 0x400000 );
	model->numFrames = header.num_frames;
	model->numMeshes = header.num_meshes;
	model->meshes = Model_Malloc( sizeof( aliasMesh_t ) * header.num_meshes );
	model->frames = Model_Malloc( sizeof( aliasFrame_t ) * header.num_frames );

	rawend = rawdata + length;
	
	/* load all frames */
	src_frame = ( dmd3frame_t * )( rawdata + header.ofs_frames );
	if( ( byte * )( src_frame + header.num_frames ) > rawend ) {
		Com_WPrintf( "%s has bad frames offset\n", model->name );
		goto fail;
	}
	dst_frame = model->frames;
	for( i = 0; i < header.num_frames; i++ ) {
		LittleVector( src_frame->translate, dst_frame->translate );
		VectorSet( dst_frame->scale, MD3_XYZ_SCALE, MD3_XYZ_SCALE, MD3_XYZ_SCALE );

		LittleVector( src_frame->mins, dst_frame->bounds[0] );
		LittleVector( src_frame->maxs, dst_frame->bounds[1] );
		dst_frame->radius = LittleFloat( src_frame->radius );

		src_frame++; dst_frame++;
	}
	
	/* load all meshes */
	src_mesh = ( dmd3mesh_t * )( rawdata + header.ofs_meshes );
	dst_mesh = model->meshes;
	for( i = 0; i < header.num_meshes; i++ ) {
		if( ( byte * )( src_mesh + 1 ) > rawend ) {
			Com_WPrintf( "%s has bad offset for mesh %d\n", model->name, i );
			goto fail;
		}

		numVerts = LittleLong( src_mesh->num_verts );
		numTris = LittleLong( src_mesh->num_tris );

		if( numVerts < 3 || numVerts > TESS_MAX_VERTICES ) {
			Com_WPrintf( "%s has bad number of vertices for mesh %d\n", model->name, i );
			goto fail;
		}
		if( numTris < 1 || numTris > TESS_MAX_INDICES / 3 ) {
			Com_WPrintf( "%s has bad number of faces for mesh %d\n", model->name, i );
			goto fail;
		}
		
		dst_mesh->numTris = numTris;
		dst_mesh->numIndices = numTris * 3;
		dst_mesh->numVerts = numVerts;
		dst_mesh->verts = Model_Malloc( sizeof( aliasVert_t ) * numVerts * header.num_frames );
		dst_mesh->tcoords = Model_Malloc( sizeof( tcoord_t ) * numVerts );
		dst_mesh->indices = Model_Malloc( sizeof( uint32_t ) * numTris * 3 );

		/* load all skins */
		numSkins = LittleLong( src_mesh->num_skins );
		if( numSkins > MAX_MD2SKINS ) {
			Com_WPrintf( "%s has bad number of skins for mesh %d\n", model->name, i );
			goto fail;
		}
		offset = LittleLong( src_mesh->ofs_skins );
		src_skin = ( dmd3skin_t * )( ( byte * )src_mesh + offset );
		if( ( byte * )( src_skin + numSkins ) > rawend ) {
			Com_WPrintf( "%s has bad skins offset for mesh %d\n", model->name, i );
			goto fail;
		}
		for( j = 0; j < numSkins; j++ ) {
			Q_strncpyz( skinname, src_skin->name, sizeof( skinname ) );
			skin = R_FindImage( skinname, it_skin );
			if( !skin ) {
				skin = r_notexture;
			}
			dst_mesh->skins[j] = skin;
		}
		dst_mesh->numSkins = numSkins;
		
		/* load all vertices */
		totalVerts = numVerts * header.num_frames;
		offset = LittleLong( src_mesh->ofs_verts );
		src_vert = ( dmd3vertex_t * )( ( byte * )src_mesh + offset );
		if( ( byte * )( src_vert + totalVerts ) > rawend ) {
			Com_WPrintf( "%s has bad vertices offset for mesh %d\n", model->name, i );
			goto fail;
		}
		dst_vert = dst_mesh->verts;
		for( j = 0; j < totalVerts; j++ ) {
			dst_vert->pos[0] = ( signed short )LittleShort( src_vert->point[0] );
			dst_vert->pos[1] = ( signed short )LittleShort( src_vert->point[1] );
			dst_vert->pos[2] = ( signed short )LittleShort( src_vert->point[2] );

			dst_vert->normalIndex = ll2byte[src_vert->norm[0]]
                [src_vert->norm[1]];

			src_vert++; dst_vert++;
		}
		
		/* load all texture coords */
		offset = LittleLong( src_mesh->ofs_tcs );
		src_tc = ( dmd3coord_t * )( ( byte * )src_mesh + offset );
		if( ( byte * )( src_tc + numVerts ) > rawend ) {
			Com_WPrintf( "%s has bad tcoords offset for mesh %d\n", model->name, i );
			goto fail;
		}
		dst_tc = dst_mesh->tcoords;
		for( j = 0; j < numVerts; j++ ) {
			dst_tc->st[0] = LittleFloat( src_tc->st[0] );
			dst_tc->st[1] = LittleFloat( src_tc->st[1] );
			src_tc++; dst_tc++;
		}

		/* load all triangle indices */
		offset = LittleLong( src_mesh->ofs_indexes );
		src_idx = ( uint32_t * )( ( byte * )src_mesh + offset );
		if( ( byte * )( src_idx + numTris * 3 ) > rawend ) {
			Com_WPrintf( "%s has bad indices offset for mesh %d\n", model->name, i );
			goto fail;
		}
		dst_idx = dst_mesh->indices;
		for( j = 0; j < numTris; j++ ) {
			dst_idx[0] = LittleLong( src_idx[0] );
			dst_idx[1] = LittleLong( src_idx[1] );
			dst_idx[2] = LittleLong( src_idx[2] );
            if( dst_idx[0] >= numVerts || dst_idx[1] >= numVerts || dst_idx[2] >= numVerts ) {
			    Com_WPrintf( "%s has bad indices for triangle %d in mesh %d\n", model->name, j, i );
			    goto fail;
            }
			src_idx += 3; dst_idx += 3;
		}
		
		offset = LittleLong( src_mesh->meshsize );
		src_mesh = ( dmd3mesh_t * )( ( byte * )src_mesh + offset );
		dst_mesh++;
	}

    sys.HunkEnd( &model->pool );
	return qtrue;

fail:
	sys.HunkFree( &model->pool );
	return qfalse;
}

#define Model_TempAlloc( ptr, size ) \
    do { ptr = ( void * )data; data += size; offset += size; } while( 0 )

static qboolean Model_WriteMd3( model_t *model, fileHandle_t f ) {
	dmd3header_t *header;
	uint32_t offset, length;
    byte *data;
	dmd3frame_t *dst_frame;
	dmd3mesh_t *dst_mesh;
	dmd3vertex_t *dst_vert;
	dmd3coord_t *dst_tc;
	dmd3skin_t *dst_skin;
	uint32_t *dst_idx;
	aliasFrame_t *src_frame;
	aliasMesh_t *src_mesh;
	aliasVert_t *src_vert;
	tcoord_t *src_tc;
	uint32_t *src_idx;
	uint32_t totalVerts;
	int i, j;
    qboolean ret;
    short pos[3];

    // precalculate total length
    length = sizeof( dmd3header_t ) +
        model->numFrames * sizeof( dmd3frame_t );
	src_mesh = model->meshes;
	for( i = 0; i < model->numMeshes; i++ ) {
        length += sizeof( dmd3mesh_t ) +
            src_mesh->numSkins * sizeof( dmd3skin_t ) +
            src_mesh->numVerts * model->numFrames * sizeof( dmd3vertex_t ) +
            src_mesh->numVerts * sizeof( dmd3coord_t ) +
            src_mesh->numIndices * sizeof( uint32_t );
        src_mesh++;
    }

    data = fs.AllocTempMem( length );
    offset = 0;

    // write header
    Model_TempAlloc( header, sizeof( dmd3header_t ) );
    header->ident = LittleLong( MD3_IDENT );
	header->version = LittleLong( MD3_VERSION );
    memset( header->filename, 0, sizeof( header->filename ) );
    header->flags = 0;
	header->num_frames = LittleLong( model->numFrames );
    header->num_tags = 0;
    header->num_meshes = LittleLong( model->numMeshes );
    header->num_skins = 0;

	// write all frames
    header->ofs_frames = LittleLong( offset );
	src_frame = model->frames;
    Model_TempAlloc( dst_frame, model->numFrames * sizeof( dmd3frame_t ) );
	for( i = 0; i < model->numFrames; i++ ) {
		LittleVector( src_frame->translate, dst_frame->translate );

		LittleVector( src_frame->bounds[0], dst_frame->mins );
		LittleVector( src_frame->bounds[1], dst_frame->maxs );
		dst_frame->radius = LittleFloat( src_frame->radius );

        memset( dst_frame->creator, 0, sizeof( dst_frame->creator ) );

		src_frame++; dst_frame++;
	}
	
	// write all meshes
	header->ofs_meshes = LittleLong( offset );
	src_mesh = model->meshes;
	for( i = 0; i < model->numMeshes; i++ ) {
        offset = 0;
        Model_TempAlloc( dst_mesh, sizeof( dmd3mesh_t ) );
        dst_mesh->ident = LittleLong( MD3_IDENT );
        memset( dst_mesh->name, 0, sizeof( dst_mesh->name ) );
        dst_mesh->flags = 0;
        dst_mesh->num_frames = LittleLong( model->numFrames );
        dst_mesh->num_skins = LittleLong( src_mesh->numSkins );
		dst_mesh->num_tris = LittleLong( src_mesh->numTris );
		dst_mesh->num_verts = LittleLong( src_mesh->numVerts );

		// write all skins
        dst_mesh->ofs_skins = LittleLong( offset );
        Model_TempAlloc( dst_skin, sizeof( dmd3skin_t ) * src_mesh->numSkins );
		for( j = 0; j < src_mesh->numSkins; j++ ) {
            memcpy( dst_skin->name, src_mesh->skins[j]->name, MAX_QPATH );
            dst_skin->unused = 0;
            dst_skin++;
		}
		
		// write all vertices
        dst_mesh->ofs_verts = LittleLong( offset );
		totalVerts = src_mesh->numVerts * model->numFrames;
        src_vert = src_mesh->verts;
        src_frame = model->frames;
        Model_TempAlloc( dst_vert, sizeof( dmd3vertex_t ) * totalVerts );
		for( j = 0; j < totalVerts; j++ ) {
            pos[0] = src_vert->pos[0] * src_frame->scale[0] / MD3_XYZ_SCALE;
            pos[1] = src_vert->pos[1] * src_frame->scale[1] / MD3_XYZ_SCALE;
            pos[2] = src_vert->pos[2] * src_frame->scale[2] / MD3_XYZ_SCALE;

			dst_vert->point[0] = ( signed short )LittleShort( pos[0] );
			dst_vert->point[1] = ( signed short )LittleShort( pos[1] );
			dst_vert->point[2] = ( signed short )LittleShort( pos[2] );

            // TODO: calc normal

            src_vert++; dst_vert++;
		}
		
		// write all texture coords
		dst_mesh->ofs_tcs = LittleLong( offset );
        src_tc = src_mesh->tcoords;
        Model_TempAlloc( dst_tc, sizeof( dmd3coord_t ) * src_mesh->numVerts );
		for( j = 0; j < src_mesh->numVerts; j++ ) {
			dst_tc->st[0] = LittleFloat( src_tc->st[0] );
			dst_tc->st[1] = LittleFloat( src_tc->st[1] );
            src_tc++; dst_tc++;
		}

		// write all triangle indices
		dst_mesh->ofs_indexes = LittleLong( offset );
		src_idx = src_mesh->indices;
		Model_TempAlloc( dst_idx, sizeof( uint32_t ) * src_mesh->numIndices );
		for( j = 0; j < src_mesh->numTris; j++ ) {
			dst_idx[0] = LittleLong( src_idx[0] );
			dst_idx[1] = LittleLong( src_idx[1] );
			dst_idx[2] = LittleLong( src_idx[2] );
			src_idx += 3; dst_idx += 3;
		}
		
	    dst_mesh->meshsize = LittleLong( offset );
	}

    header->ofs_tags = 0;
    header->ofs_end = length;

	// write model to disk
    ret = qtrue;
    if( fs.Write( header, length, f ) != length ) {
        ret = qfalse;
    }

    fs.FreeFile( header );

	return ret;
}

static qboolean Model_LoadSp2( model_t *model, const byte *rawdata, int length ) {
	dsprite_t *header;
	dsprframe_t *src_frame;
	spriteFrame_t *dst_frame;
	unsigned ident, version, numframes, width, height;
	char buffer[MAX_SKINNAME];
	image_t *image;
    int i;

	if( length < sizeof( *header ) ) {
		Com_EPrintf( "%s has length < header length\n", model->name );
		return qfalse;
	}

	/* byte swap the header */
	header = ( dsprite_t * )rawdata;
	for( i = 0; i < sizeof( header )/4; i++ ) {
		(( uint32_t * )&header)[i] = LittleLong( (( uint32_t * )&header)[i] );
	}

	ident = LittleLong( header->ident );
	version = LittleLong( header->version );
	numframes = LittleLong( header->numframes );

	if( ident != IDSPRITEHEADER ) {
		Com_EPrintf( "%s is not an sp2 file\n", model->name );
		return qfalse;
	}
	if( version != SPRITE_VERSION ) {
		Com_EPrintf( "%s has bad version: %d != %d\n",
			model->name, version, ALIAS_VERSION );
		return qfalse;
	}
	if( numframes < 1 || numframes > MAX_MD2SKINS ) {
		Com_EPrintf( "%s has bad number of frames: %d\n",
			model->name, numframes );
		return qfalse;
	}

	model->type = MODEL_SPRITE;
	sys.HunkBegin( &model->pool, 0x10000 );

	model->sframes = Model_Malloc( sizeof( spriteFrame_t ) * numframes );
	model->numFrames = numframes;

	src_frame = header->frames;
	dst_frame = model->sframes;
	for( i = 0; i < numframes; i++ ) {
		width = LittleLong( src_frame->width );
		height = LittleLong( src_frame->height );
		if( width < 1 || height < 1 || width > MAX_TEXTURE_SIZE || height > MAX_TEXTURE_SIZE ) {
			Com_WPrintf( "%s has bad image dimensions for frame #%d: %ux%u\n",
				model->name, width, height, i );
			width = 1;
			height = 1;
		}
		dst_frame->width = width;
		dst_frame->height = height;
		dst_frame->x = LittleLong( src_frame->origin_x );
		dst_frame->y = LittleLong( src_frame->origin_y );

		Q_strncpyz( buffer, src_frame->name, sizeof( buffer ) );
		image = R_FindImage( buffer, it_sprite );
		if( !image ) {
			Com_DPrintf( "Couldn't find image '%s' for frame #%d for sprite '%s'\n",
				buffer, i, model->name );
			image = r_notexture;
		}
		dst_frame->image = image;

		dst_frame++; src_frame++;
	}

    sys.HunkEnd( &model->pool );

	return qtrue;
}

void GL_GetModelSize( qhandle_t hModel, vec3_t mins, vec3_t maxs ) {
    VectorClear( mins );
    VectorClear( maxs );
}

qhandle_t GL_RegisterModel( const char *name ) {
	int index, length = 0;
	size_t namelen;
	model_t *model;
	byte *rawdata;
	uint32_t ident;
	qboolean success;
	char buffer[MAX_QPATH];

	if( name[0] == '*' ) {
		/* inline bsp model */
		index = atoi( name + 1 );
		if( index < 1 || index >= r_world.numSubmodels ) {
			Com_Error( ERR_DROP, "%s: bad inline model index: %d", __func__, index );
		}
		return ~index;
	}

	if( !strcmp( r_world.name, name ) ) {
		/* TODO: change client code and remove this hack */
		return 0;
	}

	namelen = strlen( name );
	if( namelen > MAX_QPATH - 1 ) {
		Com_Error( ERR_DROP, "%s: oversize name", __func__ );
	}

	model = Model_Find( name );
	if( model ) {
		goto finish;
	}

	rawdata = NULL;
    buffer[0] = 0;
	if( gl_override_models->integer ) {
		if( namelen > 4 && !strcmp( name + namelen - 4, ".md2" ) ) {
			strcpy( buffer, name );
			buffer[namelen - 1] = '3';
		    length = fs.LoadFile( buffer, ( void ** )&rawdata );
        }
	}
	if( !rawdata ) {
		length = fs.LoadFile( name, ( void ** )&rawdata );
		if( !rawdata ) {
			Com_DPrintf( "Couldn't load %s\n", name );
			return 0;
		}
	}

	if( length < 4 ) {
		Com_WPrintf( "LoadXXX: %s: file too short\n", name );
		return 0;
	}

	model = Model_Alloc( name );

	ident = LittleLong( *( uint32_t * )rawdata );
	switch( ident ) {
	case IDALIASHEADER:
		success = Model_LoadMd2( model, rawdata, length );
        if( success && gl_override_models->integer > 1 ) {
            fileHandle_t f;

            fs.FOpenFile( buffer, &f, FS_MODE_WRITE );
            if( f ) {
                Model_WriteMd3( model, f );
                fs.FCloseFile( f );
            }
        }
		break;
	case MD3_IDENT:
		success = Model_LoadMd3( model, rawdata, length );
		break;
	case IDSPRITEHEADER:
		success = Model_LoadSp2( model, rawdata, length );
		break;
	case IDBSPHEADER:
		Com_WPrintf( "LoadXXX: %s: loaded BSP model after the world\n", name );
		success = qfalse;
		break;
	default:
		Com_WPrintf( "LoadXXX: %s: unknown ident: %x\n", name, ident );
		success = qfalse;
		break;
	}

	fs.FreeFile( rawdata );

	if( !success ) {
		model->name[0] = 0;
		return 0;
	}

finish:
	index = ( model - r_models ) + 1;
	return index;
}

modelType_t *GL_ModelForHandle( qhandle_t hModel ) {
	model_t *model;
	bspSubmodel_t *submodel;

	if( !hModel ) {
		return NULL;
	}

	if( hModel & 0x80000000 ) {
		hModel = ~hModel;

		if( hModel < 1 || hModel >= r_world.numSubmodels ) {
			Com_Error( ERR_DROP, "GL_ModelForHandle: submodel %d out of range", hModel );
		}

		submodel = &r_world.submodels[hModel];
		return &submodel->type;
	}

	if( hModel > r_numModels ) {
		Com_Error( ERR_DROP, "GL_ModelForHandle: %d out of range", hModel );
	}
	model = &r_models[ hModel - 1 ];
	if( !model->name[0] ) {
		return NULL;
	}
	return &model->type;
	
}

void GL_InitModels( void ) {
    float s[2], c[2];
    vec3_t normal;
    int i, j;

    for( i = 0; i < 256; i++ ) {
        for( j = 0; j < 256; j++ ) {
            s[0] = sin( i / 255.0f );
            c[0] = cos( i / 255.0f );
            
            s[1] = sin( j / 255.0f );
            c[1] = cos( j / 255.0f );

            VectorSet( normal, s[0] * c[1], s[0] * s[1], c[0] );
            ll2byte[i][j] = DirToByte( normal );
        }
    }

	gl_override_models = cvar.Get( "r_override_models", "0",
        CVAR_ARCHIVE|CVAR_LATCHED );


	cmd.AddCommand( "modellist", Model_List_f );
}

void GL_ShutdownModels( void ) {
    Model_FreeAll();
	cmd.RemoveCommand( "modellist" );
}
