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
#include "common/intreadwrite.h"

enum {
    FORMAT_PCM = 1,
    FORMAT_ADPCM_MS = 2,
};

wavinfo_t s_info;

/*
===============================================================================

MS ADPCM decoding

===============================================================================
*/

typedef struct {
    int coeff1;
    int coeff2;
    int delta;
    int sample1;
    int sample2;
} adpcm_channel_t;

static const int16_t AdaptationTable[] = {
    230, 230, 230, 230, 307, 409, 512, 614,
    768, 614, 512, 409, 307, 230, 230, 230
};

static const uint8_t AdaptCoeff1[] = {
    64, 128, 0, 48, 60, 115, 98
};

static const int8_t AdaptCoeff2[] = {
    0, -64, 0, 16, 0, -52, -58
};

static int expand_nibble(adpcm_channel_t *c, int nibble)
{
    int predictor;

    predictor = (c->sample1 * c->coeff1 + c->sample2 * c->coeff2) >> 6;
    predictor += ((nibble & 8) ? (nibble - 16) : nibble) * c->delta;

    c->sample2 = c->sample1;
    c->sample1 = clip16(predictor);
    c->delta = (AdaptationTable[nibble] * c->delta) >> 8;
    clamp(c->delta, 16, INT_MAX / 768);

    return c->sample1;
}

static bool decode_adpcm_block(sizebuf_t *sz, int16_t *samples, int nb_samples)
{
    adpcm_channel_t ch[2];
    int nch = s_info.channels;
    int i, j = nch >> 1;

    for (i = 0; i < nch; i++) {
        int block_predictor = SZ_ReadByte(sz);
        if (block_predictor > 6) {
            Com_DPrintf("%s has bad block predictor\n", s_info.name);
            return false;
        }
        ch[i].coeff1 = AdaptCoeff1[block_predictor];
        ch[i].coeff2 = AdaptCoeff2[block_predictor];
    }

    for (i = 0; i < nch; i++)
        ch[i].delta = SZ_ReadShort(sz);

    for (i = 0; i < nch; i++)
        ch[i].sample1 = SZ_ReadShort(sz);

    for (i = 0; i < nch; i++)
        ch[i].sample2 = SZ_ReadShort(sz);

    for (i = 0; i < nch; i++)
        *samples++ = ch[i].sample2;

    for (i = 0; i < nch; i++)
        *samples++ = ch[i].sample1;

    for (i = 0; i < (nb_samples - 2) >> (2 - nch); i++) {
        int byte = SZ_ReadByte(sz);
        *samples++ = expand_nibble(&ch[0], byte >> 4);
        *samples++ = expand_nibble(&ch[j], byte & 15);
    }

    return true;
}

static bool decode_adpcm(void)
{
    int nb_blocks = s_info.data_chunk_len / s_info.block_align;
    if (nb_blocks < 1) {
        Com_DPrintf("%s has bad number of blocks\n", s_info.name);
        return false;
    }

    int nb_samples = (s_info.block_align - 6 * s_info.channels) * 2 / s_info.channels;
    if (s_info.samples > nb_samples * nb_blocks) {
        Com_DPrintf("%s has bad number of samples\n", s_info.name);
        return false;
    }

    sizebuf_t sz;
    SZ_Init(&sz, s_info.data, s_info.data_chunk_len);
    sz.cursize = sz.maxsize;

    byte *data = FS_AllocTempMem(s_info.channels * s_info.width * nb_samples * nb_blocks);
    int16_t *out = (int16_t *)data;
    for (int i = 0; i < nb_blocks; i++) {
        if (!decode_adpcm_block(&sz, out, nb_samples)) {
            FS_FreeTempMem(data);
            return false;
        }
        out += nb_samples * s_info.channels;
    }

    s_info.data = data;
    return true;
}

/*
===============================================================================

WAV loading

===============================================================================
*/

#define TAG_RIFF    MakeLittleLong('R','I','F','F')
#define TAG_WAVE    MakeLittleLong('W','A','V','E')
#define TAG_fmt     MakeLittleLong('f','m','t',' ')
#define TAG_cue     MakeLittleLong('c','u','e',' ')
#define TAG_LIST    MakeLittleLong('L','I','S','T')
#define TAG_mark    MakeLittleLong('m','a','r','k')
#define TAG_data    MakeLittleLong('d','a','t','a')
#define TAG_fact    MakeLittleLong('f','a','c','t')

static int FindChunk(sizebuf_t *sz, uint32_t search)
{
    while (sz->readcount + 8 < sz->cursize) {
        uint32_t chunk = SZ_ReadLong(sz);
        uint32_t len   = SZ_ReadLong(sz);

        len = min(len, sz->cursize - sz->readcount);
        if (chunk == search)
            return len;

        sz->readcount += ALIGN(len, 2);
    }

    return 0;
}

static bool GetWavinfo(sizebuf_t *sz)
{
    int samples, width, chunk_len, next_chunk;

// find "RIFF" chunk
    if (SZ_ReadLong(sz) != TAG_RIFF) {
        Com_DPrintf("%s has missing/invalid RIFF chunk\n", s_info.name);
        return false;
    }

    sz->readcount += 4;
    if (SZ_ReadLong(sz) != TAG_WAVE) {
        Com_DPrintf("%s has missing/invalid WAVE chunk\n", s_info.name);
        return false;
    }

// save position after "WAVE" tag
    next_chunk = sz->readcount;

// find "fmt " chunk
    if (!FindChunk(sz, TAG_fmt)) {
        Com_DPrintf("%s has missing/invalid fmt chunk\n", s_info.name);
        return false;
    }

    s_info.format = SZ_ReadShort(sz);
    switch (s_info.format) {
    case FORMAT_PCM:
    case FORMAT_ADPCM_MS:
        break;
    default:
        Com_DPrintf("%s has unsupported format\n", s_info.name);
        return false;
    }

    s_info.channels = SZ_ReadShort(sz);
    switch (s_info.channels) {
    case 1:
    case 2:
        break;
    default:
        Com_DPrintf("%s has bad number of channels\n", s_info.name);
        return false;
    }

    s_info.rate = SZ_ReadLong(sz);
    if (s_info.rate < 8000 || s_info.rate > 48000) {
        Com_DPrintf("%s has bad rate\n", s_info.name);
        return false;
    }

    sz->readcount += 4;

    if (s_info.format == FORMAT_PCM) {
        sz->readcount += 2;
        width = SZ_ReadShort(sz);
        switch (width) {
        case 8:
            s_info.width = 1;
            break;
        case 16:
            s_info.width = 2;
            break;
        default:
            Com_DPrintf("%s has bad width\n", s_info.name);
            return false;
        }
    } else {
        s_info.block_align = SZ_ReadShort(sz);
        if (s_info.block_align < 6 * s_info.channels) {
            Com_DPrintf("%s has bad block align\n", s_info.name);
            return false;
        }

        // find "fact" chunk
        sz->readcount = next_chunk;
        if (!FindChunk(sz, TAG_fact)) {
            Com_DPrintf("%s has missing/invalid fact chunk\n", s_info.name);
            return false;
        }

        s_info.samples = SZ_ReadLong(sz);
        if (s_info.samples < 1) {
            Com_DPrintf("%s has bad number of samples\n", s_info.name);
            return false;
        }

        // MS ADPCM is always 16-bit
        s_info.width = 2;
    }

// find "data" chunk
    sz->readcount = next_chunk;
    chunk_len = FindChunk(sz, TAG_data);
    if (!chunk_len) {
        Com_DPrintf("%s has missing/invalid data chunk\n", s_info.name);
        return false;
    }

// calculate length in samples
    if (s_info.format == FORMAT_PCM) {
        s_info.samples = chunk_len / (s_info.width * s_info.channels);
        if (!s_info.samples) {
            Com_DPrintf("%s has zero length\n", s_info.name);
            return false;
        }
    }

    s_info.data_chunk_len = chunk_len;
    s_info.data = sz->data + sz->readcount;
    s_info.loopstart = -1;

// find "cue " chunk
    sz->readcount = next_chunk;
    chunk_len = FindChunk(sz, TAG_cue);
    if (!chunk_len) {
        return true;
    }

// save position after "cue " chunk
    next_chunk = sz->readcount + ALIGN(chunk_len, 2);

    sz->readcount += 24;
    samples = SZ_ReadLong(sz);
    if (samples < 0 || samples >= s_info.samples) {
        Com_DPrintf("%s has bad loop start\n", s_info.name);
        return true;
    }
    s_info.loopstart = samples;

// if the next chunk is a "LIST" chunk, look for a cue length marker
    sz->readcount = next_chunk;
    if (!FindChunk(sz, TAG_LIST)) {
        return true;
    }

    sz->readcount += 20;
    if (SZ_ReadLong(sz) != TAG_mark) {
        return true;
    }

// this is not a proper parse, but it works with cooledit...
    sz->readcount -= 8;
    samples = SZ_ReadLong(sz);  // samples in loop
    if (samples < 1 || samples > s_info.samples - s_info.loopstart) {
        Com_DPrintf("%s has bad loop length\n", s_info.name);
        return true;
    }
    s_info.samples = s_info.loopstart + samples;

    return true;
}

/*
==============
S_LoadSound
==============
*/
sfxcache_t *S_LoadSound(sfx_t *s)
{
    sizebuf_t   sz;
    byte        *data;
    sfxcache_t  *sc;
    int         len;
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

    SZ_Init(&sz, data, len);
    sz.cursize = len;

    if (!GetWavinfo(&sz)) {
        s->error = Q_ERR_INVALID_FORMAT;
        goto fail;
    }

    if (s_info.format == FORMAT_ADPCM_MS && !decode_adpcm()) {
        s->error = Q_ERR_INVALID_FORMAT;
        goto fail;
    }

#if USE_BIG_ENDIAN
    if (s_info.format == FORMAT_PCM && s_info.width == 2) {
        uint16_t *data = (uint16_t *)s_info.data;
        int count = s_info.samples * s_info.channels;

        for (int i = 0; i < count; i++)
            data[i] = LittleShort(data[i]);
    }
#endif

    sc = s_api.upload_sfx(s);

    if (s_info.format == FORMAT_ADPCM_MS)
        FS_FreeTempMem(s_info.data);

fail:
    FS_FreeFile(data);
    return sc;
}

