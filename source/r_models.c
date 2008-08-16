/*
Copyright (C) 2008 Andrey Nazarov
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "com_local.h"
#include "files.h"
#include "sys_public.h"
#include "q_list.h"
#include "d_md2.h"
#if USE_MD3
#include "d_md3.h"
#endif
#include "d_sp2.h"
#include "r_shared.h"
#include "r_models.h"

static model_t		r_models[MAX_MODELS];
static int			r_numModels;

#if USE_MD3
static cvar_t       *r_override_models;
#endif

static model_t *MOD_Alloc( const char *name ) {
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

	return model;
}

static model_t *MOD_Find( const char *name ) {
	model_t *model;
	int i;

	for( i = 0, model = r_models; i < r_numModels; i++, model++ ) {
		if( !model->name[0] ) {
			continue;
		}
		if( !FS_pathcmp( model->name, name ) ) {
            return model;
		}
	}

	return NULL;
}

static void MOD_List_f( void ) {
	int		i, count;
	model_t	*model;
	size_t	bytes;

	Com_Printf( "------------------\n");
	bytes = count = 0;

	for( i = 0, model = r_models; i < r_numModels; i++, model++ ) {
		if( !model->name[0] ) {
			continue;
		}
		Com_Printf( "%8"PRIz" : %s\n", model->pool.mapped, model->name );
		bytes += model->pool.mapped;
        count++;
	}
	Com_Printf( "Total models: %d (out of %d slots)\n", count, r_numModels );
	Com_Printf( "Total resident: %"PRIz"\n", bytes );
}

void MOD_FreeUnused( void ) {
	model_t *model;
	int i;

	for( i = 0, model = r_models; i < r_numModels; i++, model++ ) {
		if( !model->name[0] ) {
			continue;
		}
		if( model->registration_sequence == registration_sequence ) {
            // make sure it is paged in
			Com_PageInMemory( model->pool.base, model->pool.cursize );
        } else {
            // don't need this model
    		Hunk_Free( &model->pool );
            memset( model, 0, sizeof( *model ) );
        }
	}
}

void MOD_FreeAll( void ) {
	model_t *model;
	int i;

	for( i = 0, model = r_models; i < r_numModels; i++, model++ ) {
		if( !model->name[0] ) {
			continue;
		}

		Hunk_Free( &model->pool );
        memset( model, 0, sizeof( *model ) );
	}

	r_numModels = 0;
}

qboolean MOD_ValidateMD2( model_t *model, dmd2header_t *header, size_t length ) {
    size_t end;

    // check ident and version
	if( header->ident != MD2_IDENT ) {
        Com_WPrintf( "%s is not an MD2 file\n", model->name );
		return qfalse;
	}
	if( header->version != MD2_VERSION ) {
        Com_WPrintf( "%s has bad version: %d instead of %d\n",
            model->name, header->version, MD2_VERSION );
		return qfalse;
	}

    // check triangles
	if( header->num_tris < 1 || header->num_tris > MD2_MAX_TRIANGLES ) {
		Com_WPrintf( "%s has bad number of triangles\n", model->name );
		return qfalse;
	}
    end = header->ofs_tris + sizeof( dmd2triangle_t ) * header->num_tris;
    if( header->ofs_tris < sizeof( header ) || end < header->ofs_tris || end > length ) {
		Com_WPrintf( "%s has bad triangles offset\n", model->name );
		return qfalse;
    }

    // check st
	if( header->num_st < 3 || header->num_st > MD2_MAX_VERTS ) {
		Com_WPrintf( "%s has bad number of st\n", model->name );
		return qfalse;
	}
    end = header->ofs_st + sizeof( dmd2stvert_t ) * header->num_st;
    if( header->ofs_st < sizeof( header ) || end < header->ofs_st || end > length ) {
		Com_WPrintf( "%s has bad st offset\n", model->name );
		return qfalse;
    }

    // check xyz and frames
	if( header->num_xyz < 3 || header->num_xyz > MD2_MAX_VERTS ) {
		Com_WPrintf( "%s has bad number of xyz\n", model->name );
		return qfalse;
	}
	if( header->num_frames < 1 || header->num_frames > MD2_MAX_FRAMES ) {
		Com_WPrintf( "%s has bad number of frames\n", model->name );
		return qfalse;
	}
    end = sizeof( dmd2frame_t ) + ( header->num_xyz - 1 ) * sizeof( dmd2trivertx_t );
    if( header->framesize < end || header->framesize > MD2_MAX_FRAMESIZE ) {
		Com_WPrintf( "%s has bad frame size\n", model->name );
		return qfalse;
    }
    end = header->ofs_frames + header->framesize * header->num_frames;
    if( header->ofs_frames < sizeof( header ) || end < header->ofs_frames || end > length ) {
		Com_WPrintf( "%s has bad frames offset\n", model->name );
		return qfalse;
    }

    // check skins
	if( header->num_skins > MAX_ALIAS_SKINS ) {
		Com_WPrintf( "%s has bad number of skins\n", model->name );
		return qfalse;
	}
	if( header->num_skins ) {
        end = header->ofs_skins + MD2_MAX_SKINNAME * header->num_skins;
        if( header->ofs_skins < sizeof( header ) || end < header->ofs_skins || end > length ) {
    		Com_WPrintf( "%s has bad skins offset\n", model->name );
	    	return qfalse;
        }
	}
	if( header->skinwidth < 1 || header->skinwidth > MD2_MAX_SKINWIDTH ) {
		Com_WPrintf( "%s has bad skin width\n", model->name );
		return qfalse;
	}
	if( header->skinheight < 1 || header->skinheight > MD2_MAX_SKINHEIGHT ) {
		Com_WPrintf( "%s has bad skin height\n", model->name );
		return qfalse;
	}
    return qtrue;
}

static qboolean MOD_LoadSP2( model_t *model, const void *rawdata, size_t length ) {
    dsp2header_t header;
    dsp2frame_t *src_frame;
    mspriteframe_t *dst_frame;
    unsigned w, h, x, y;
    char buffer[SP2_MAX_FRAMENAME];
    image_t *image;
    int i;

    if( length < sizeof( header ) ) {
        Com_EPrintf( "%s is too small\n", model->name );
        return qfalse;
    }

    // byte swap the header
    header = *( dsp2header_t * )rawdata;
    for( i = 0; i < sizeof( header )/4; i++ ) {
        (( uint32_t * )&header)[i] = LittleLong( (( uint32_t * )&header)[i] );
    }

    if( header.ident != SP2_IDENT ) {
        Com_EPrintf( "%s is not an SP2 file\n", model->name );
        return qfalse;
    }
    if( header.version != SP2_VERSION ) {
        Com_EPrintf( "%s has bad version: %d instead of %d\n",
            model->name, header.version, SP2_VERSION );
        return qfalse;
    }
    if( header.numframes < 1 || header.numframes > SP2_MAX_FRAMES ) {
        Com_EPrintf( "%s has bad number of frames: %d\n",
            model->name, header.numframes );
        return qfalse;
    }
    if( sizeof( dsp2header_t ) + sizeof( dsp2frame_t ) * header.numframes > length ) {
        Com_EPrintf( "%s has frames out of bounds\n", model->name );
        return qfalse;
    }

    Hunk_Begin( &model->pool, 0x10000 );

    model->spriteframes = Model_Malloc( sizeof( mspriteframe_t ) * header.numframes );
    model->numframes = header.numframes;

    src_frame = ( dsp2frame_t * )( ( byte * )rawdata + sizeof( dsp2header_t ) );
    dst_frame = model->spriteframes;
    for( i = 0; i < header.numframes; i++ ) {
        w = LittleLong( src_frame->width );
        h = LittleLong( src_frame->height );
        if( w < 1 || h < 1 || w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE ) {
            Com_WPrintf( "%s has bad frame dimensions\n", model->name );
            w = 1;
            h = 1;
        }
        dst_frame->width = w;
        dst_frame->height = h;

        // FIXME: are these signed?
        x = LittleLong( src_frame->origin_x );
        y = LittleLong( src_frame->origin_y );
        if( x > 8192 || y > 8192 ) {
            Com_WPrintf( "%s has bad frame origin\n", model->name );
            x = y = 0;
        }
        dst_frame->origin_x = x;
        dst_frame->origin_y = y;

        memcpy( buffer, src_frame->name, sizeof( buffer ) );
        buffer[sizeof( buffer ) - 1] = 0;
        image = IMG_Find( buffer, it_sprite );
        if( !image ) {
            image = r_notexture;
        }
        dst_frame->image = image;

        src_frame++;
        dst_frame++;
    }

    Hunk_End( &model->pool );

    return qtrue;
}


qhandle_t R_RegisterModel( const char *name ) {
	int index;
	size_t namelen, filelen;
	model_t *model;
	byte *rawdata;
	uint32_t ident;
	qboolean success;

	if( name[0] == '*' ) {
		// inline bsp model
		index = atoi( name + 1 );
		return ~index;
	}

	namelen = strlen( name );
	if( namelen >= MAX_QPATH ) {
		Com_Error( ERR_DROP, "%s: oversize name", __func__ );
	}

	model = MOD_Find( name );
	if( model ) {
        MOD_Reference( model );
		goto finish;
	}

    filelen = 0;
	rawdata = NULL;

#if USE_MD3
	if( r_override_models->integer ) {
	    char buffer[MAX_QPATH];

		if( namelen > 4 && !Q_stricmp( name + namelen - 4, ".md2" ) ) {
			memcpy( buffer, name, namelen + 1 );
			buffer[namelen - 1] = '3';
		    filelen = FS_LoadFile( buffer, ( void ** )&rawdata );
        }
	}
	if( !rawdata )
#endif
    {
		filelen = FS_LoadFile( name, ( void ** )&rawdata );
		if( !rawdata ) {
			Com_DPrintf( "Couldn't load %s\n", name );
			return 0;
		}
	}

	if( filelen < 4 ) {
		Com_WPrintf( "%s: %s: file too short\n", __func__, name );
		return 0;
	}

	model = MOD_Alloc( name );

	ident = LittleLong( *( uint32_t * )rawdata );
	switch( ident ) {
	case MD2_IDENT:
		success = MOD_LoadMD2( model, rawdata, filelen );
		break;
#if USE_MD3
	case MD3_IDENT:
		success = MOD_LoadMD3( model, rawdata, filelen );
		break;
#endif
	case SP2_IDENT:
		success = MOD_LoadSP2( model, rawdata, filelen );
		break;
	default:
		Com_WPrintf( "%s: %s: unknown ident: %x\n", __func__, name, ident );
		success = qfalse;
		break;
	}

	FS_FreeFile( rawdata );

	if( !success ) {
        memset( model, 0, sizeof( *model ) );
		return 0;
	}

finish:
	index = ( model - r_models ) + 1;
	return index;
}

model_t *MOD_ForHandle( qhandle_t h ) {
	model_t *model;

	if( !h ) {
		return NULL;
	}
	if( h < 0 || h > r_numModels ) {
		Com_Error( ERR_DROP, "%s: %d out of range", __func__, h );
	}
	model = &r_models[ h - 1 ];
	if( !model->name[0] ) {
		return NULL;
	}
	return model;
}

void MOD_Init( void ) {
	if( r_numModels ) {
		Com_Error( ERR_FATAL, "%s: %d models not freed", __func__, r_numModels );
	}

#if USE_MD3
	r_override_models = Cvar_Get( "r_override_models", "0",
        CVAR_ARCHIVE|CVAR_FILES );
#endif

	Cmd_AddCommand( "modellist", MOD_List_f );
}

void MOD_Shutdown( void ) {
    MOD_FreeAll();
	Cmd_RemoveCommand( "modellist" );
}

