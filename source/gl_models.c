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
#include "d_md2.h"
#include "d_md3.h"
#include "d_sp2.h"

qboolean MOD_LoadMD2( model_t *model, const void *rawdata, size_t length ) {
	dmd2header_t header;
	dmd2frame_t *src_frame;
	dmd2trivertx_t *src_vert;
	dmd2triangle_t *src_tri;
	dmd2stvert_t *src_tc;
	maliasframe_t *dst_frame;
	maliasvert_t *dst_vert;
	maliasmesh_t *dst_mesh;
	uint32_t *finalIndices;
	maliastc_t *dst_tc;
	int i, j, k;
	uint16_t remap[MD2_MAX_TRIANGLES*3];
	uint16_t vertIndices[MD2_MAX_TRIANGLES*3];
	uint16_t tcIndices[MD2_MAX_TRIANGLES*3];
	int numverts, numindices;
	char skinname[MAX_QPATH];
	char *src_skin;
	image_t *skin;
	vec_t scaleS, scaleT;
	int val;
	vec3_t mins, maxs;

	if( length < sizeof( header ) ) {
		Com_WPrintf( "%s is too short\n", model->name );
		return qfalse;
	}

	/* byte swap the header */
	header = *( dmd2header_t * )rawdata;
	for( i = 0; i < sizeof( header )/4; i++ ) {
		(( uint32_t * )&header)[i] = LittleLong( (( uint32_t * )&header)[i] );
	}

    if( !MOD_ValidateMD2( model, &header, length ) ) {
        return qfalse;
    }

	Hunk_Begin( &model->pool, 0x400000 );

	/* load all triangle indices */
	src_tri = ( dmd2triangle_t * )( ( byte * )rawdata + header.ofs_tris );
	for( i = 0; i < header.num_tris; i++ ) {
        for( j = 0; j < 3; j++ ) {
            uint16_t idx_xyz = LittleShort( src_tri->index_xyz[j] );
            uint16_t idx_st = LittleShort( src_tri->index_st[j] );

            if( idx_xyz >= header.num_xyz || idx_st >= header.num_st ) {
		        Com_WPrintf( "%s has bad triangle indices\n", model->name );
		        goto fail;
            }

            vertIndices[i*3+j] = idx_xyz; 
            tcIndices[i*3+j] = idx_st;
        }
		src_tri++;
	}

	numindices = header.num_tris * 3;

	model->meshes = Model_Malloc( sizeof( maliasmesh_t ) );
	model->nummeshes = 1;

	dst_mesh = model->meshes;
	dst_mesh->indices = Model_Malloc( numindices * sizeof( uint32_t ) );
	dst_mesh->numtris = header.num_tris;
	dst_mesh->numindices = numindices;

	for( i = 0; i < numindices; i++ ) {
		remap[i] = 0xFFFF;
	}

	numverts = 0;
	src_tc = ( dmd2stvert_t * )( ( byte * )rawdata + header.ofs_st );
	finalIndices = dst_mesh->indices;
	for( i = 0; i < numindices; i++ ) {
		if( remap[i] != 0xFFFF ) {
			continue; /* already remapped */
		}

		for( j = i + 1; j < numindices; j++ ) {
			if( vertIndices[i] == vertIndices[j] &&
				( src_tc[tcIndices[i]].s == src_tc[tcIndices[j]].s &&
					src_tc[tcIndices[i]].t == src_tc[tcIndices[j]].t ) )
			{
				/* duplicate vertex */
				remap[j] = i;
				finalIndices[j] = numverts;
			} 
		}

		/* new vertex */
		remap[i] = i;
		finalIndices[i] = numverts++;
	}

	dst_mesh->verts = Model_Malloc( numverts * header.num_frames * sizeof( maliasvert_t ) );
	dst_mesh->tcoords = Model_Malloc( numverts * sizeof( maliastc_t ) );
	dst_mesh->numverts = numverts;

	/* load all skins */
	src_skin = ( char * )rawdata + header.ofs_skins;
	for( i = 0; i < header.num_skins; i++ ) {
		Q_strncpyz( skinname, src_skin, sizeof( skinname ) );
		skin = IMG_Find( skinname, it_skin );
		if( !skin ) {
			skin = r_notexture;
		}
		dst_mesh->skins[i] = skin;
		src_skin += MD2_MAX_SKINNAME;
	}
	dst_mesh->numskins = header.num_skins;

	/* load all tcoords */
	src_tc = ( dmd2stvert_t * )( ( byte * )rawdata + header.ofs_st );
	dst_tc = dst_mesh->tcoords;
	skin = dst_mesh->skins[0];
	scaleS = 1.0f / header.skinwidth;
	scaleT = 1.0f / header.skinheight;
	for( i = 0; i < numindices; i++ ) {
		if( remap[i] == i ) {
            float s = ( signed short )LittleShort( src_tc[ tcIndices[i] ].s );
            float t = ( signed short )LittleShort( src_tc[ tcIndices[i] ].t );

			dst_tc[ finalIndices[i] ].st[0] = s * scaleS;
			dst_tc[ finalIndices[i] ].st[1] = t * scaleT;
		}
	}

	/* load all frames */
	model->frames = Model_Malloc( header.num_frames * sizeof( maliasframe_t ) );
	model->numframes = header.num_frames;

	src_frame = ( dmd2frame_t * )( ( byte * )rawdata + header.ofs_frames );
	dst_frame = model->frames;
	for( j = 0; j < header.num_frames; j++ ) {
		LittleVector( src_frame->scale, dst_frame->scale );
		LittleVector( src_frame->translate, dst_frame->translate );

		/* load frame vertices */
		ClearBounds( mins, maxs );
		for( i = 0; i < numindices; i++ ) {
			if( remap[i] == i ) {
				src_vert = &src_frame->verts[ vertIndices[i] ];
				dst_vert = &dst_mesh->verts[ j * numverts + finalIndices[i] ];

				dst_vert->pos[0] = src_vert->v[0];
				dst_vert->pos[1] = src_vert->v[1];
				dst_vert->pos[2] = src_vert->v[2];
				k = src_vert->lightnormalindex;
				if( k >= NUMVERTEXNORMALS ) {
		            Com_WPrintf( "%s has bad normal indices\n", model->name );
					goto fail;
				}
				dst_vert->normalindex = k;

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

		src_frame = ( dmd2frame_t * )( ( byte * )src_frame + header.framesize );
		dst_frame++;
	}

    Hunk_End( &model->pool );
	return qtrue;

fail:
	Hunk_Free( &model->pool );
	return qfalse;
}

#if USE_MD3
static byte         ll2byte[256][256];
static qboolean     ll2byte_inited;        

static void ll2byte_init( void ) {
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
}

qboolean MOD_LoadMD3( model_t *model, const void *rawdata, size_t length ) {
	dmd3header_t header;
	uint32_t offset;
	dmd3frame_t *src_frame;
	dmd3mesh_t *src_mesh;
	dmd3vertex_t *src_vert;
	dmd3coord_t *src_tc;
	dmd3skin_t *src_skin;
	uint32_t *src_idx;
	maliasframe_t *dst_frame;
	maliasmesh_t *dst_mesh;
	maliasvert_t *dst_vert;
	maliastc_t *dst_tc;
	uint32_t *dst_idx;
	uint32_t numverts, numtris, numskins;
	uint32_t totalVerts;
	char skinname[MAX_QPATH];
	image_t *skin;
	const byte *rawend;
	int i, j;

	if( length < sizeof( header ) ) {
		Com_WPrintf( "%s is too small to hold MD3 header\n", model->name );
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
		Com_WPrintf( "%s has bad version: %d instead of %d\n",
			model->name, header.version, MD3_VERSION );
		return qfalse;
	}
	if( header.num_frames < 1 || header.num_frames > MD3_MAX_FRAMES ) {
		Com_WPrintf( "%s has bad number of frames\n", model->name );
		return qfalse;
	}
	if( header.num_meshes < 1 || header.num_meshes > MD3_MAX_MESHES ) {
		Com_WPrintf( "%s has bad number of meshes\n", model->name );
		return qfalse;
	}
	
	Hunk_Begin( &model->pool, 0x400000 );
	model->numframes = header.num_frames;
	model->nummeshes = header.num_meshes;
	model->meshes = Model_Malloc( sizeof( maliasmesh_t ) * header.num_meshes );
	model->frames = Model_Malloc( sizeof( maliasframe_t ) * header.num_frames );

	rawend = ( byte * )rawdata + length;
	
	/* load all frames */
	src_frame = ( dmd3frame_t * )( ( byte * )rawdata + header.ofs_frames );
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
	src_mesh = ( dmd3mesh_t * )( ( byte * )rawdata + header.ofs_meshes );
	dst_mesh = model->meshes;
	for( i = 0; i < header.num_meshes; i++ ) {
		if( ( byte * )( src_mesh + 1 ) > rawend ) {
			Com_WPrintf( "%s has bad offset for mesh %d\n", model->name, i );
			goto fail;
		}

		numverts = LittleLong( src_mesh->num_verts );
		numtris = LittleLong( src_mesh->num_tris );

		if( numverts < 3 || numverts > TESS_MAX_VERTICES ) {
			Com_WPrintf( "%s has bad number of vertices for mesh %d\n", model->name, i );
			goto fail;
		}
		if( numtris < 1 || numtris > TESS_MAX_INDICES / 3 ) {
			Com_WPrintf( "%s has bad number of faces for mesh %d\n", model->name, i );
			goto fail;
		}
		
		dst_mesh->numtris = numtris;
		dst_mesh->numindices = numtris * 3;
		dst_mesh->numverts = numverts;
		dst_mesh->verts = Model_Malloc( sizeof( maliasvert_t ) * numverts * header.num_frames );
		dst_mesh->tcoords = Model_Malloc( sizeof( maliastc_t ) * numverts );
		dst_mesh->indices = Model_Malloc( sizeof( uint32_t ) * numtris * 3 );

		/* load all skins */
		numskins = LittleLong( src_mesh->num_skins );
		if( numskins > MAX_ALIAS_SKINS ) {
			Com_WPrintf( "%s has bad number of skins for mesh %d\n", model->name, i );
			goto fail;
		}
		offset = LittleLong( src_mesh->ofs_skins );
		src_skin = ( dmd3skin_t * )( ( byte * )src_mesh + offset );
		if( ( byte * )( src_skin + numskins ) > rawend ) {
			Com_WPrintf( "%s has bad skins offset for mesh %d\n", model->name, i );
			goto fail;
		}
		for( j = 0; j < numskins; j++ ) {
			Q_strncpyz( skinname, src_skin->name, sizeof( skinname ) );
			skin = IMG_Find( skinname, it_skin );
			if( !skin ) {
				skin = r_notexture;
			}
			dst_mesh->skins[j] = skin;
		}
		dst_mesh->numskins = numskins;
		
		/* load all vertices */
		totalVerts = numverts * header.num_frames;
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

            if( !ll2byte_inited ) {
                ll2byte_init();
                ll2byte_inited = qtrue;
            }
			dst_vert->normalindex = ll2byte[src_vert->norm[0]]
                [src_vert->norm[1]];

			src_vert++; dst_vert++;
		}
		
		/* load all texture coords */
		offset = LittleLong( src_mesh->ofs_tcs );
		src_tc = ( dmd3coord_t * )( ( byte * )src_mesh + offset );
		if( ( byte * )( src_tc + numverts ) > rawend ) {
			Com_WPrintf( "%s has bad tcoords offset for mesh %d\n", model->name, i );
			goto fail;
		}
		dst_tc = dst_mesh->tcoords;
		for( j = 0; j < numverts; j++ ) {
			dst_tc->st[0] = LittleFloat( src_tc->st[0] );
			dst_tc->st[1] = LittleFloat( src_tc->st[1] );
			src_tc++; dst_tc++;
		}

		/* load all triangle indices */
		offset = LittleLong( src_mesh->ofs_indexes );
		src_idx = ( uint32_t * )( ( byte * )src_mesh + offset );
		if( ( byte * )( src_idx + numtris * 3 ) > rawend ) {
			Com_WPrintf( "%s has bad indices offset for mesh %d\n", model->name, i );
			goto fail;
		}
		dst_idx = dst_mesh->indices;
		for( j = 0; j < numtris; j++ ) {
			dst_idx[0] = LittleLong( src_idx[0] );
			dst_idx[1] = LittleLong( src_idx[1] );
			dst_idx[2] = LittleLong( src_idx[2] );
            if( dst_idx[0] >= numverts || dst_idx[1] >= numverts || dst_idx[2] >= numverts ) {
			    Com_WPrintf( "%s has bad indices for triangle %d in mesh %d\n", model->name, j, i );
			    goto fail;
            }
			src_idx += 3; dst_idx += 3;
		}
		
		offset = LittleLong( src_mesh->meshsize );
		src_mesh = ( dmd3mesh_t * )( ( byte * )src_mesh + offset );
		dst_mesh++;
	}

    Hunk_End( &model->pool );
	return qtrue;

fail:
	Hunk_Free( &model->pool );
	return qfalse;
}
#endif

void MOD_Reference( model_t *model ) {
    int i, j;

	if( model->frames ) {
		for( i = 0; i < model->nummeshes; i++ ) {
            maliasmesh_t *mesh = &model->meshes[i];
			for( j = 0; j < mesh->numskins; j++ ) {
				mesh->skins[j]->registration_sequence = registration_sequence;
			}
		}
    } else if( model->spriteframes ) {
		for( i = 0; i < model->numframes; i++ ) {
			model->spriteframes[i].image->registration_sequence = registration_sequence;
		}
    } else {
		Com_Error( ERR_FATAL, "%s: bad model type", __func__ );
	}

	model->registration_sequence = registration_sequence;
}

