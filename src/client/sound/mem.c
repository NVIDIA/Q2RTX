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

#define FORMAT_PCM  1

wavinfo_t s_info;

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
    int tag, samples, width, chunk_len, next_chunk;

    tag = SZ_ReadLong(sz);

    if (tag == MakeLittleLong('O','g','g','S') || !COM_CompareExtension(s_info.name, ".ogg")) {
        sz->readcount = 0;
        return OGG_Load(sz);
    }

// find "RIFF" chunk
    if (tag != TAG_RIFF) {
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
    if (s_info.format != FORMAT_PCM) {
        Com_DPrintf("%s has unsupported format\n", s_info.name);
        return false;
    }

    s_info.channels = SZ_ReadShort(sz);
    if (s_info.channels < 1 || s_info.channels > 2) {
        Com_DPrintf("%s has bad number of channels\n", s_info.name);
        return false;
    }

    s_info.rate = SZ_ReadLong(sz);
    if (s_info.rate < 8000 || s_info.rate > 48000) {
        Com_DPrintf("%s has bad rate\n", s_info.name);
        return false;
    }

    sz->readcount += 6;
    width = SZ_ReadShort(sz);
    switch (width) {
    case 8:
        s_info.width = 1;
        break;
    case 16:
        s_info.width = 2;
        break;
    case 24:
        s_info.width = 3;
        break;
    default:
        Com_DPrintf("%s has bad width\n", s_info.name);
        return false;
    }

// find "data" chunk
    sz->readcount = next_chunk;
    chunk_len = FindChunk(sz, TAG_data);
    if (!chunk_len) {
        Com_DPrintf("%s has missing/invalid data chunk\n", s_info.name);
        return false;
    }

// calculate length in samples
    s_info.samples = chunk_len / (s_info.width * s_info.channels);
    if (!s_info.samples) {
        Com_DPrintf("%s has zero length\n", s_info.name);
        return false;
    }

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

static void ConvertSamples(void)
{
    uint16_t *data = (uint16_t *)s_info.data;
    int count = s_info.samples * s_info.channels;

// sigh. truncate 24 bit to 16
    if (s_info.width == 3) {
        for (int i = 0; i < count; i++)
            data[i] = RL32(&s_info.data[i * 3]) >> 8;
        s_info.width = 2;
        return;
    }

#if USE_BIG_ENDIAN
    if (s_info.width == 2) {
        for (int i = 0; i < count; i++)
            data[i] = LittleShort(data[i]);
    }
#endif
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

    if (s_info.format == FORMAT_PCM)
        ConvertSamples();

    sc = s_api.upload_sfx(s);

    if (s_info.format != FORMAT_PCM)
        FS_FreeTempMem(s_info.data);

fail:
    FS_FreeFile(data);
    return sc;
}

