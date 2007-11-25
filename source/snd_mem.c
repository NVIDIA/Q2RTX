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

/*
================
ResampleSfx

code from QFusion (thanks Vic)
================
*/
static void ResampleSfx (sfx_t *sfx, byte *data)
{
	int i, outcount, srcsample, srclength, samplefrac, fracstep;
	sfxcache_t *sc = sfx->cache;

	// this is usually 0.5 (128), 1 (256), or 2 (512)
	fracstep = ((double) sc->speed / (double) dma.speed) * 256.0;

	srclength = sc->length * sc->channels;
	outcount = (double) sc->length * (double) dma.speed / (double) sc->speed;

	sc->length = outcount;
	if (sc->loopstart != -1)
		sc->loopstart = (double) sc->loopstart * (double) dma.speed / (double) sc->speed;

	sc->speed = dma.speed;

// resample / decimate to the current source rate
	if (fracstep == 256)
	{
		if (sc->width == 1) // 8bit
			for (i = 0;i < srclength;i++)
				((signed char *)sc->data)[i] = ((unsigned char *)data)[i] - 128;
		else
			for (i = 0;i < srclength;i++)
				((short *)sc->data)[i] = LittleShort (((short *)data)[i]);
	}
	else
	{
// general case
		Com_DPrintf("ResampleSfx: resampling sound %s\n", sfx->name);
		samplefrac = 0;

		if ((fracstep & 255) == 0) // skipping points on perfect multiple
		{
			srcsample = 0;
			fracstep >>= 8;
			if (sc->width == 2)
			{
				short *out = (void *)sc->data, *in = (void *)data;
				if (sc->channels == 2)
				{
					fracstep <<= 1;
					for (i=0 ; i<outcount ; i++)
					{
						*out++ = LittleShort (in[srcsample  ]);
						*out++ = LittleShort (in[srcsample+1]);
						srcsample += fracstep;
					}
				}
				else
				{
					for (i=0 ; i<outcount ; i++)
					{
						*out++ = LittleShort (in[srcsample  ]);
						srcsample += fracstep;
					}
				}
			}
			else
			{
				signed char *out = (void *)sc->data;
				unsigned char *in = (void *)data;

				if (sc->channels == 2)
				{
					fracstep <<= 1;
					for (i=0 ; i<outcount ; i++)
					{
						*out++ = in[srcsample  ] - 128;
						*out++ = in[srcsample+1] - 128;
						srcsample += fracstep;
					}
				}
				else
				{
					for (i=0 ; i<outcount ; i++)
					{
						*out++ = in[srcsample  ] - 128;
						srcsample += fracstep;
					}
				}
			}
		}
		else
		{
			int sample;
			int a, b;
			if (sc->width == 2)
			{
				short *out = (void *)sc->data, *in = (void *)data;

				if (sc->channels == 2)
				{
					for (i=0 ; i<outcount ; i++)
					{
						srcsample = (samplefrac >> 8) << 1;
						a = in[srcsample  ];
						if (srcsample+2 >= srclength)
							b = 0;
						else
							b = in[srcsample+2];
						sample = (((b - a) * (samplefrac & 255)) >> 8) + a;
						*out++ = (short) sample;
						a = in[srcsample+1];
						if (srcsample+2 >= srclength)
							b = 0;
						else
							b = in[srcsample+3];
						sample = (((b - a) * (samplefrac & 255)) >> 8) + a;
						*out++ = (short) sample;
						samplefrac += fracstep;
					}
				}
				else
				{
					for (i=0 ; i<outcount ; i++)
					{
						srcsample = samplefrac >> 8;
						a = in[srcsample  ];
						if (srcsample+1 >= srclength)
							b = 0;
						else
							b = in[srcsample+1];
						sample = (((b - a) * (samplefrac & 255)) >> 8) + a;
						*out++ = (short) sample;
						samplefrac += fracstep;
					}
				}
			}
			else
			{
				signed char *out = (void *)sc->data;
				unsigned char *in = (void *)data;
				if (sc->channels == 2)
				{
					for (i=0 ; i<outcount ; i++)
					{
						srcsample = (samplefrac >> 8) << 1;
						a = (int) in[srcsample  ] - 128;
						if (srcsample+2 >= srclength)
							b = 0;
						else
							b = (int) in[srcsample+2] - 128;
						sample = (((b - a) * (samplefrac & 255)) >> 8) + a;
						*out++ = (signed char) sample;
						a = (int) in[srcsample+1] - 128;
						if (srcsample+2 >= srclength)
							b = 0;
						else
							b = (int) in[srcsample+3] - 128;
						sample = (((b - a) * (samplefrac & 255)) >> 8) + a;
						*out++ = (signed char) sample;
						samplefrac += fracstep;
					}
				}
				else
				{
					for (i=0 ; i<outcount ; i++)
					{
						srcsample = samplefrac >> 8;
						a = (int) in[srcsample  ] - 128;
						if (srcsample+1 >= srclength)
							b = 0;
						else
							b = (int) in[srcsample+1] - 128;
						sample = (((b - a) * (samplefrac & 255)) >> 8) + a;
						*out++ = (signed char) sample;
						samplefrac += fracstep;
					}
				}
			}
		}
	}
}

/*
===============================================================================

WAV loading

===============================================================================
*/

typedef struct wavinfo_s {
	int			rate;
	int			width;
	int			channels;
	int			loopstart;
	int			samples;
} wavinfo_t;

static byte     *data_p;
static byte 	*iff_end;
static byte 	*iff_data;
static uint32 	iff_chunk_len;

static int GetLittleShort(void) {
	int val;

    if( data_p + 2 > iff_end ) {
        return -1;
    }

	val = data_p[0];
	val |= data_p[1] << 8;
	data_p += 2;
	return val;
}

static int GetLittleLong(void) {
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

static void FindNextChunk( uint32 name ) {
    uint32 chunk, length;

	while( 1 ) {
		if( data_p >= iff_end ) {
            // didn't find the chunk
			data_p = NULL;
			return;
		}
		
        chunk = GetLittleLong();
		iff_chunk_len = GetLittleLong();
        if( data_p + iff_chunk_len > iff_end ) {
            Com_DPrintf( "FindNextChunk: oversize chunk %#x\n", chunk );
            data_p = NULL;
            return;
        }
		if( chunk == name ) {
			return;
        }
        length = ( iff_chunk_len + 1 ) & ~1;
        data_p += length;
	}
}

static void FindChunk( uint32 name ) {
	data_p = iff_data;
	FindNextChunk( name );
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
    uint32  chunk;

	memset (info, 0, sizeof(*info));

// find "RIFF" chunk
	FindChunk( TAG_RIFF );
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
	FindChunk( TAG_fmt );
	if( !data_p ) {
		Com_DPrintf("%s has missing/invalid fmt chunk\n", name );
		return qfalse;
	}
	format = GetLittleShort();
	if (format != 1) {
		Com_DPrintf("%s has non-Microsoft PCM format\n", name);
		return qfalse;
	}

	info->channels = GetLittleShort();
	if (info->channels != 1 && info->channels != 2) {
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
	FindChunk( TAG_cue );
	if( data_p ) {
		data_p += 24;
		info->loopstart = GetLittleLong();

		FindNextChunk( TAG_LIST );
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
	FindChunk( TAG_data );
	if( !data_p ) {
		Com_DPrintf( "%s has missing/invalid data chunk\n", name );
		return qfalse;
	}

	samples = iff_chunk_len / info->width;
    if( !samples ) {
		Com_DPrintf( "%s has zero length\n", name );
        return qfalse;
    }

	if ( info->samples ) {
		if (samples < info->samples) {
			Com_DPrintf( "%s has bad loop length\n", name);
            return qfalse;
        }
	} else {
		info->samples = samples;
    }

	return qtrue;
}

/*
==============
S_LoadSound
==============
*/
sfxcache_t *S_LoadSound (sfx_t *s)
{
    char	namebuffer[MAX_QPATH];
	byte	*data;
	wavinfo_t	info;
	int		len;
    float   stepscale;
	sfxcache_t	*sc;
	int		size;
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

	if (!data)
	{
		Com_DPrintf ("Couldn't load %s\n", namebuffer);
		return NULL;
	}

	iff_data = data;
	iff_end = data + size;
	if( !GetWavinfo( name, &info ) ) {
        FS_FreeFile (data);
        return NULL;
    }

    stepscale = ( float )info.rate / dma.speed;
	len = info.samples / stepscale;
    if( !len ) {
        Com_DPrintf( "%s resampled to zero length\n", name );
        FS_FreeFile (data);
        return NULL;
    }

	sc = s->cache = Z_TagMalloc( len * info.width + sizeof( sfxcache_t ) - 1,
        TAG_SOUND );
	
	sc->length = info.samples / info.channels;
	sc->loopstart = info.loopstart;
	sc->speed = info.rate;
	sc->width = info.width;
	sc->channels = info.channels;

	ResampleSfx( s, data_p );

	FS_FreeFile (data);

	return sc;
}

