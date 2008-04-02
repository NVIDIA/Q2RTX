/*
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
// snd_mem.c: sound caching

#include "cl_local.h"
#include "snd_local.h"

typedef struct wavinfo_s {
	int			rate;
	int			width;
	int			channels;
	int			loopstart;
	int			samples;
    byte        *data;
} wavinfo_t;


/*
================
ResampleSfx
================
*/
static sfxcache_t *ResampleSfx( sfx_t *sfx, wavinfo_t *info ) {
	int		outcount;
	int		srcsample;
	float	stepscale;
	int		i;
	int		samplefrac, fracstep;
	sfxcache_t	*sc;

	stepscale = ( float )info->rate / dma.speed;	// this is usually 0.5, 1, or 2

	outcount = info->samples / stepscale;
    if( !outcount ) {
        return NULL;
    }

	sc = sfx->cache = S_Malloc( outcount * info->width + sizeof( sfxcache_t ) - 1 );
	
	sc->length = outcount;
	sc->loopstart = info->loopstart / stepscale;
	sc->speed = dma.speed;
	sc->width = info->width;
	sc->channels = 1;

// resample / decimate to the current source rate

	if (stepscale == 1) {
// fast special case
        if( sc->width == 1 ) {
		    for(i = 0; i < outcount; i++) {
			    ((signed char *)sc->data)[i] = ( signed char )( ( uint8_t )info->data[i] - 128 );
            }
        } else {
		    for(i = 0; i < outcount; i++) {
                ((signed short *)sc->data)[i] = ( signed short )LittleShort( (( uint16_t * )info->data)[i] );
            }
        }
	} else {
// general case
		samplefrac = 0;
		fracstep = stepscale*256;
        if( sc->width == 1 ) {
            for (i = 0; i < outcount; i++) {
                srcsample = samplefrac >> 8;
                samplefrac += fracstep;
                ((signed char *)sc->data)[i] = ( signed char )( ( uint8_t )info->data[srcsample] - 128 );
            }
        } else {
            for (i = 0; i < outcount; i++) {
                srcsample = samplefrac >> 8;
                samplefrac += fracstep;
                ((signed short *)sc->data)[i] = ( signed short )LittleShort( (( uint16_t * )info->data)[srcsample] );
            }
		}
	}

    return sc;
}

/*
===============================================================================

WAV loading

===============================================================================
*/

static byte     *data_p;
static byte 	*iff_end;
static byte 	*iff_data;
static uint32_t iff_chunk_len;

static int GetLittleShort( void ) {
	int val;

    if( data_p + 2 > iff_end ) {
        return -1;
    }

	val = data_p[0];
	val |= data_p[1] << 8;
	data_p += 2;
	return val;
}

static int GetLittleLong( void ) {
	int val;

    if( data_p + 4 > iff_end ) {
        return -1;
    }

	val = data_p[0];
	val |= data_p[1] << 8;
	val |= data_p[2] << 16;
	val |= data_p[3] << 24;
	data_p += 4;
	return val;
}

static void FindNextChunk( const char *name, uint32_t search ) {
    uint32_t chunk, length;
    int i;

	for( i = 0; i < 1000; i++ ) {
		if( data_p + 8 >= iff_end ) {
			data_p = NULL;
			return; // didn't find the chunk
		}

        chunk = GetLittleLong();
		iff_chunk_len = GetLittleLong();
        if( iff_chunk_len > iff_end - data_p ) {
            Com_DPrintf( "%s: oversize chunk %#x in %s\n",
                __func__, chunk, name );
            data_p = NULL;
            return;
        }
		if( chunk == search ) {
			return;
        }
        length = ( iff_chunk_len + 1 ) & ~1;
        data_p += length;
    }

    Com_WPrintf( "%s: too many iterations for chunk %#x in %s\n",
        __func__, search, name );
}

static void FindChunk( const char *name, uint32_t search ) {
	data_p = iff_data;
	FindNextChunk( name, search );
}

#define TAG_RIFF    MakeLong( 'R', 'I', 'F', 'F' )
#define TAG_WAVE    MakeLong( 'W', 'A', 'V', 'E' )
#define TAG_fmt     MakeLong( 'f', 'm', 't', ' ' )
#define TAG_cue     MakeLong( 'c', 'u', 'e', ' ' )
#define TAG_LIST    MakeLong( 'L', 'I', 'S', 'T' )
#define TAG_MARK    MakeLong( 'M', 'A', 'R', 'K' )
#define TAG_data    MakeLong( 'd', 'a', 't', 'a' )

/*
============
GetWavinfo
============
*/
static qboolean GetWavinfo( const char *name, wavinfo_t *info ) {
	int     format;
	int		samples, width;
    uint32_t  chunk;

	memset( info, 0, sizeof( *info ) );

// find "RIFF" chunk
	FindChunk( name, TAG_RIFF );
	if( !data_p ) {
		Com_DPrintf( "%s has missing/invalid RIFF chunk\n", name );
		return qfalse;
	}
    chunk = GetLittleLong();
	if( chunk != TAG_WAVE ) {
		Com_DPrintf( "%s has missing/invalid WAVE chunk\n", name );
		return qfalse;
	}

	iff_data = data_p;

// get "fmt " chunk
	FindChunk( name, TAG_fmt );
	if( !data_p ) {
		Com_DPrintf("%s has missing/invalid fmt chunk\n", name );
		return qfalse;
	}
	format = GetLittleShort();
	if( format != 1 ) {
		Com_DPrintf( "%s has non-Microsoft PCM format\n", name );
		return qfalse;
	}

	info->channels = GetLittleShort();
	if( info->channels != 1 /*&& info->channels != 2*/ ) {
		Com_DPrintf( "%s has bad number of channels\n", name );
        return qfalse;
	}

	info->rate = GetLittleLong();
    if( info->rate <= 0 ) {
		Com_DPrintf( "%s has bad rate\n", name );
        return qfalse;
    }

	data_p += 4+2;

	width = GetLittleShort();
    switch( width ) {
    case 8:
        info->width = 1;
        break;
    case 16:
        info->width = 2;
        break;
    default:
		Com_DPrintf( "%s has bad width\n", name );
        return qfalse;
    }

// get cue chunk
	FindChunk( name, TAG_cue );
	if( data_p ) {
		data_p += 24;
		info->loopstart = GetLittleLong();

		FindNextChunk( name, TAG_LIST );
		if( data_p ) {
            data_p += 20;
            chunk = GetLittleLong();
			if ( chunk == TAG_MARK ) {
            // this is not a proper parse, but it works with cooledit...
				data_p += 16;
				samples = GetLittleLong();	// samples in loop
				info->samples = info->loopstart + samples;
			}
		}
	} else {
		info->loopstart = -1;
    }

// find data chunk
	FindChunk( name, TAG_data );
	if( !data_p ) {
		Com_DPrintf( "%s has missing/invalid data chunk\n", name );
		return qfalse;
	}

	samples = iff_chunk_len / info->width;
    if( !samples ) {
		Com_DPrintf( "%s has zero length\n", name );
        return qfalse;
    }

	if( info->samples ) {
		if( samples < info->samples ) {
			Com_DPrintf( "%s has bad loop length\n", name );
            return qfalse;
        }
	} else {
		info->samples = samples;
    }

    info->data = data_p;

	return qtrue;
}

/*
==============
S_LoadSound
==============
*/
sfxcache_t *S_LoadSound (sfx_t *s) {
    char	namebuffer[MAX_QPATH];
	byte	*data;
	wavinfo_t	info;
	sfxcache_t	*sc;
	size_t		size;
	char	*name;

	if (s->name[0] == '*')
		return NULL;

// see if still in memory
	sc = s->cache;
	if (sc)
		return sc;

// load it in
	if (s->truename)
		name = s->truename;
	else
		name = s->name;

	if (name[0] == '#')
		Q_strncpyz( namebuffer, name + 1, sizeof( namebuffer ) );
	else
		Q_concat( namebuffer, sizeof( namebuffer ), "sound/", name, NULL );

	size = FS_LoadFile (namebuffer, (void **)&data);
	if (!data) {
		Com_DPrintf ("Couldn't load %s\n", namebuffer);
		return NULL;
	}

	iff_data = data;
	iff_end = data + size;
	if( GetWavinfo( name, &info ) ) {
	    if( !( sc = ResampleSfx( s, &info ) ) ) {
            Com_DPrintf( "%s resampled to zero length\n", name );
        }
    }

	FS_FreeFile( data );
	return sc;
}

