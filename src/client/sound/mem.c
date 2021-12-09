/*
Copyright (C) 1997-2001 Id Software, Inc.

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
// snd_mem.c: sound caching

#include "sound.h"

wavinfo_t s_info;

#if USE_SNDDMA
/*
================
ResampleSfx
================
*/
static sfxcache_t *ResampleSfx(sfx_t *sfx)
{
    int         outcount;
    int         srcsample;
    float       stepscale;
    int         i;
    int         samplefrac, fracstep;
    sfxcache_t  *sc;

    stepscale = (float)s_info.rate / dma.speed;      // this is usually 0.5, 1, or 2

    outcount = s_info.samples / stepscale;
    if (!outcount) {
        Com_DPrintf("%s resampled to zero length\n", s_info.name);
        sfx->error = Q_ERR_TOO_FEW;
        return NULL;
    }

    sc = sfx->cache = S_Malloc(outcount * s_info.width + sizeof(sfxcache_t) - 1);

    sc->length = outcount;
    sc->loopstart = s_info.loopstart == -1 ? -1 : s_info.loopstart / stepscale;
    sc->width = s_info.width;

// resample / decimate to the current source rate
//Com_Printf("%s: %f, %d\n",sfx->name,stepscale,sc->width);
    if (stepscale == 1) {
// fast special case
        if (sc->width == 1) {
            memcpy(sc->data, s_info.data, outcount);
        } else {
#if __BYTE_ORDER == __LITTLE_ENDIAN
            memcpy(sc->data, s_info.data, outcount << 1);
#else
            for (i = 0; i < outcount; i++) {
                ((uint16_t *)sc->data)[i] = LittleShort(((uint16_t *)s_info.data)[i]);
            }
#endif
        }
    } else {
// general case
        samplefrac = 0;
        fracstep = stepscale * 256;
        if (sc->width == 1) {
            for (i = 0; i < outcount; i++) {
                srcsample = samplefrac >> 8;
                samplefrac += fracstep;
                sc->data[i] = s_info.data[srcsample];
            }
        } else {
            for (i = 0; i < outcount; i++) {
                srcsample = samplefrac >> 8;
                samplefrac += fracstep;
                ((uint16_t *)sc->data)[i] = LittleShort(((uint16_t *)s_info.data)[srcsample]);
            }
        }
    }

    return sc;
}
#endif

/*
===============================================================================

WAV loading

===============================================================================
*/

static byte     *data_p;
static byte     *iff_end;
static byte     *iff_data;
static uint32_t iff_chunk_len;

static int GetLittleShort(void)
{
    int val;

    if (data_p + 2 > iff_end) {
        return -1;
    }

    val = LittleShortMem(data_p);
    data_p += 2;
    return val;
}

static int GetLittleLong(void)
{
    int val;

    if (data_p + 4 > iff_end) {
        return -1;
    }

    val = LittleLongMem(data_p);
    data_p += 4;
    return val;
}

static void FindNextChunk(uint32_t search)
{
    uint32_t chunk, len;
    size_t remaining;

    while (data_p + 8 < iff_end) {
        chunk = RawLongMem(data_p); data_p += 4;
        len = LittleLongMem(data_p); data_p += 4;
        remaining = (size_t)(iff_end - data_p);
        if (len > remaining) {
            len = remaining;
        }
        if (chunk == search) {
            iff_chunk_len = len;
            return;
        }
        data_p += ALIGN(len, 2);
    }

    // didn't find the chunk
    data_p = NULL;
}

static void FindChunk(uint32_t search)
{
    data_p = iff_data;
    FindNextChunk(search);
}

#define TAG_RIFF    MakeRawLong('R', 'I', 'F', 'F')
#define TAG_WAVE    MakeRawLong('W', 'A', 'V', 'E')
#define TAG_fmt     MakeRawLong('f', 'm', 't', ' ')
#define TAG_cue     MakeRawLong('c', 'u', 'e', ' ')
#define TAG_LIST    MakeRawLong('L', 'I', 'S', 'T')
#define TAG_MARK    MakeRawLong('M', 'A', 'R', 'K')
#define TAG_data    MakeRawLong('d', 'a', 't', 'a')

static qboolean GetWavinfo(void)
{
    int format;
    int samples, width;
    uint32_t chunk;

// find "RIFF" chunk
    FindChunk(TAG_RIFF);
    if (!data_p) {
        Com_DPrintf("%s has missing/invalid RIFF chunk\n", s_info.name);
        return qfalse;
    }
    chunk = GetLittleLong();
    if (chunk != TAG_WAVE) {
        Com_DPrintf("%s has missing/invalid WAVE chunk\n", s_info.name);
        return qfalse;
    }

    iff_data = data_p;

// get "fmt " chunk
    FindChunk(TAG_fmt);
    if (!data_p) {
        Com_DPrintf("%s has missing/invalid fmt chunk\n", s_info.name);
        return qfalse;
    }
    format = GetLittleShort();
    if (format != 1) {
        Com_DPrintf("%s has non-Microsoft PCM format\n", s_info.name);
        return qfalse;
    }

    format = GetLittleShort();
    if (format != 1) {
        Com_DPrintf("%s has bad number of channels\n", s_info.name);
        return qfalse;
    }

    s_info.rate = GetLittleLong();
    if (s_info.rate < 8000 || s_info.rate > 48000) {
        Com_DPrintf("%s has bad rate\n", s_info.name);
        return qfalse;
    }

    data_p += 4 + 2;

    width = GetLittleShort();
    switch (width) {
    case 8:
        s_info.width = 1;
        break;
    case 16:
        s_info.width = 2;
        break;
    default:
        Com_DPrintf("%s has bad width\n", s_info.name);
        return qfalse;
    }

// get cue chunk
    FindChunk(TAG_cue);
    if (data_p) {
        data_p += 24;
        s_info.loopstart = GetLittleLong();
        if (s_info.loopstart < 0 || s_info.loopstart > INT_MAX) {
            Com_DPrintf("%s has bad loop start\n", s_info.name);
            return qfalse;
        }

        FindNextChunk(TAG_LIST);
        if (data_p) {
            data_p += 20;
            chunk = GetLittleLong();
            if (chunk == TAG_MARK) {
                // this is not a proper parse, but it works with cooledit...
                data_p += 16;
                samples = GetLittleLong();    // samples in loop
                if (samples < 0 || samples > INT_MAX - s_info.loopstart) {
                    Com_DPrintf("%s has bad loop length\n", s_info.name);
                    return qfalse;
                }
                s_info.samples = s_info.loopstart + samples;
            }
        }
    } else {
        s_info.loopstart = -1;
    }

// find data chunk
    FindChunk(TAG_data);
    if (!data_p) {
        Com_DPrintf("%s has missing/invalid data chunk\n", s_info.name);
        return qfalse;
    }

    samples = iff_chunk_len / s_info.width;
    if (!samples) {
        Com_DPrintf("%s has zero length\n", s_info.name);
        return qfalse;
    }

    if (s_info.samples) {
        if (samples < s_info.samples) {
            Com_DPrintf("%s has bad loop length\n", s_info.name);
            return qfalse;
        }
    } else {
        s_info.samples = samples;
    }

    s_info.data = data_p;

    return qtrue;
}

/*
==============
S_LoadSound
==============
*/
sfxcache_t *S_LoadSound(sfx_t *s)
{
    byte        *data;
    sfxcache_t  *sc;
    ssize_t     len;
    char        *name;

    if (s->name[0] == '*')
        return NULL;

// see if still in memory
    sc = s->cache;
    if (sc)
        return sc;

// don't retry after error
    if (s->error)
        return NULL;

// load it in
    if (s->truename)
        name = s->truename;
    else
        name = s->name;

    len = FS_LoadFile(name, (void **)&data);
    if (!data) {
        s->error = len;
        return NULL;
    }

    memset(&s_info, 0, sizeof(s_info));
    s_info.name = name;

    iff_data = data;
    iff_end = data + len;
    if (!GetWavinfo()) {
        s->error = Q_ERR_INVALID_FORMAT;
        goto fail;
    }

#if USE_OPENAL
    if (s_started == SS_OAL)
        sc = AL_UploadSfx(s);
#endif

#if USE_SNDDMA
    if (s_started == SS_DMA)
        sc = ResampleSfx(s);
#endif

fail:
    FS_FreeFile(data);
    return sc;
}

