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
// snd_dma.c -- main control for any streaming sound output device

#include "sound.h"
#include "common/intreadwrite.h"

#ifndef USE_LITTLE_ENDIAN
#define USE_LITTLE_ENDIAN 0
#endif

#define PAINTBUFFER_SIZE    2048

dma_t       dma;

cvar_t      *s_khz;

static cvar_t       *s_testsound;
static cvar_t       *s_swapstereo;
static cvar_t       *s_mixahead;

static int      snd_scaletable[32][256];
static int      snd_vol;

/*
===============================================================================

SOUND LOADING

===============================================================================
*/

static sfxcache_t *DMA_UploadSfx(sfx_t *sfx)
{
    float stepscale = (float)s_info.rate / dma.speed;   // this is usually 0.5, 1, or 2
    int i, frac, fracstep = stepscale * 256;

    int outcount = s_info.samples / stepscale;
    if (!outcount) {
        Com_DPrintf("%s resampled to zero length\n", s_info.name);
        sfx->error = Q_ERR_TOO_FEW;
        return NULL;
    }

    int size = outcount * s_info.width * s_info.channels;
    sfxcache_t *sc = sfx->cache = S_Malloc(sizeof(sfxcache_t) + size - 1);

    sc->length = outcount;
    sc->loopstart = s_info.loopstart == -1 ? -1 : s_info.loopstart / stepscale;
    sc->width = s_info.width;
    sc->channels = s_info.channels;
    sc->size = size;

// resample / decimate to the current source rate
    if (stepscale == 1) {   // fast special case
        outcount *= s_info.channels;
        if (sc->width == 1 || USE_LITTLE_ENDIAN) {
            memcpy(sc->data, s_info.data, outcount * sc->width);
        } else {
            uint16_t *src = (uint16_t *)s_info.data;
            uint16_t *dst = (uint16_t *)sc->data;
            for (i = 0; i < outcount; i++)
                dst[i] = LittleShort(src[i]);
        }
    } else if (sc->width == 1) {
        if (s_info.channels == 1) {
            for (i = frac = 0; i < outcount; i++, frac += fracstep)
                sc->data[i] = s_info.data[frac >> 8];
        } else {
            for (i = frac = 0; i < outcount; i++, frac += fracstep) {
                sc->data[i*2+0] = s_info.data[(frac >> 8)*2+0];
                sc->data[i*2+1] = s_info.data[(frac >> 8)*2+1];
            }
        }
    } else {
        uint16_t *src = (uint16_t *)s_info.data;
        uint16_t *dst = (uint16_t *)sc->data;
        if (s_info.channels == 1) {
            for (i = frac = 0; i < outcount; i++, frac += fracstep)
                dst[i] = LittleShort(src[frac >> 8]);
        } else {
            for (i = frac = 0; i < outcount; i++, frac += fracstep) {
                dst[i*2+0] = LittleShort(src[(frac >> 8)*2+0]);
                dst[i*2+1] = LittleShort(src[(frac >> 8)*2+1]);
            }
        }
    }

    return sc;
}

static void DMA_PageInSfx(sfx_t *sfx)
{
    sfxcache_t *sc = sfx->cache;
    if (sc)
        Com_PageInMemory(sc->data, sc->size);
}

/*
===============================================================================

PAINTBUFFER TRANSFER

===============================================================================
*/

// clip integer to [-0x8000, 0x7FFF] range (stolen from FFmpeg)
static inline int clip16(int v)
{
    return ((v + 0x8000U) & ~0xFFFF) ? (v >> 31) ^ 0x7FFF : v;
}

static void TransferStereo16(samplepair_t *samp, int endtime)
{
    int ltime = s_paintedtime;
    int size = dma.samples >> 1;

    while (ltime < endtime) {
        // handle recirculating buffer issues
        int lpos = ltime & (size - 1);
        int count = min(size - lpos, endtime - ltime);

        // write a linear blast of samples
        int16_t *out = (int16_t *)dma.buffer + (lpos << 1);
        for (int i = 0; i < count; i++, samp++, out += 2) {
            out[0] = clip16(samp->left >> 8);
            out[1] = clip16(samp->right >> 8);
        }

        ltime += count;
    }
}

static void TransferStereo(samplepair_t *samp, int endtime)
{
    int *p = (int *)samp;
    int count = (endtime - s_paintedtime) * dma.channels;
    int out_mask = dma.samples - 1;
    int out_idx = s_paintedtime * dma.channels & out_mask;
    int step = 3 - dma.channels;
    int val;

    if (dma.samplebits == 16) {
        int16_t *out = (int16_t *)dma.buffer;
        while (count--) {
            val = *p >> 8;
            p += step;
            out[out_idx] = clip16(val);
            out_idx = (out_idx + 1) & out_mask;
        }
    } else if (dma.samplebits == 8) {
        uint8_t *out = (uint8_t *)dma.buffer;
        while (count--) {
            val = *p >> 8;
            p += step;
            out[out_idx] = (clip16(val) >> 8) + 128;
            out_idx = (out_idx + 1) & out_mask;
        }
    }
}

static void TransferPaintBuffer(samplepair_t *samp, int endtime)
{
    int i;

    if (s_testsound->integer) {
        // write a fixed sine wave
        for (i = 0; i < endtime - s_paintedtime; i++) {
            samp[i].left = samp[i].right = sin((s_paintedtime + i) * 0.1f) * 20000 * 256;
        }
    }

    if (s_swapstereo->integer) {
        for (i = 0; i < endtime - s_paintedtime; i++) {
            int tmp = samp[i].left;
            samp[i].left = samp[i].right;
            samp[i].right = tmp;
        }
    }

    if (dma.samplebits == 16 && dma.channels == 2) {
        // optimized case
        TransferStereo16(samp, endtime);
    } else {
        // general case
        TransferStereo(samp, endtime);
    }
}

/*
===============================================================================

CHANNEL MIXING

===============================================================================
*/

typedef void (*paintfunc_t)(channel_t *, sfxcache_t *, int, samplepair_t *);

#define PAINTFUNC(name) \
    static void name(channel_t *ch, sfxcache_t *sc, int count, samplepair_t *samp)

PAINTFUNC(PaintMono8)
{
    int *lscale = snd_scaletable[ch->leftvol >> 3];
    int *rscale = snd_scaletable[ch->rightvol >> 3];
    uint8_t *sfx = sc->data + ch->pos;

    for (int i = 0; i < count; i++, samp++, sfx++) {
        samp->left += lscale[*sfx];
        samp->right += rscale[*sfx];
    }
}

PAINTFUNC(PaintStereoDmix8)
{
    int *lscale = snd_scaletable[ch->leftvol >> 4];
    int *rscale = snd_scaletable[ch->rightvol >> 4];
    uint8_t *sfx = sc->data + ch->pos * 2;

    for (int i = 0; i < count; i++, samp++, sfx += 2) {
        samp->left += lscale[sfx[0]] + lscale[sfx[1]];
        samp->right += rscale[sfx[0]] + rscale[sfx[1]];
    }
}

PAINTFUNC(PaintStereoFull8)
{
    int *scale = snd_scaletable[ch->leftvol >> 3];
    uint8_t *sfx = sc->data + ch->pos * 2;

    for (int i = 0; i < count; i++, samp++, sfx += 2) {
        samp->left += scale[sfx[0]];
        samp->right += scale[sfx[1]];
    }
}

PAINTFUNC(PaintMono16)
{
    int leftvol = ch->leftvol * snd_vol;
    int rightvol = ch->rightvol * snd_vol;
    int16_t *sfx = (int16_t *)sc->data + ch->pos;

    for (int i = 0; i < count; i++, samp++, sfx++) {
        samp->left += (*sfx * leftvol) >> 8;
        samp->right += (*sfx * rightvol) >> 8;
    }
}

PAINTFUNC(PaintStereoDmix16)
{
    int leftvol = ch->leftvol * snd_vol;
    int rightvol = ch->rightvol * snd_vol;
    int16_t *sfx = (int16_t *)sc->data + ch->pos * 2;

    for (int i = 0; i < count; i++, samp++, sfx += 2) {
        int sum = sfx[0] + sfx[1];
        samp->left += (sum * leftvol) >> 9;
        samp->right += (sum * rightvol) >> 9;
    }
}

PAINTFUNC(PaintStereoFull16)
{
    int vol = ch->leftvol * snd_vol;
    int16_t *sfx = (int16_t *)sc->data + ch->pos * 2;

    for (int i = 0; i < count; i++, samp++, sfx += 2) {
        samp->left += (sfx[0] * vol) >> 8;
        samp->right += (sfx[1] * vol) >> 8;
    }
}

static const paintfunc_t paintfuncs[] = {
    PaintMono8,
    PaintStereoDmix8,
    PaintStereoFull8,
    PaintMono16,
    PaintStereoDmix16,
    PaintStereoFull16,
};

static void PaintChannels(int endtime)
{
    samplepair_t paintbuffer[PAINTBUFFER_SIZE];
    channel_t *ch;
    int i;

    while (s_paintedtime < endtime) {
        // if paintbuffer is smaller than DMA buffer
        int end = min(endtime, s_paintedtime + PAINTBUFFER_SIZE);

        // start any playsounds
        while (1) {
            playsound_t *ps = s_pendingplays.next;
            if (ps == &s_pendingplays)
                break;    // no more pending sounds
            if (ps->begin > s_paintedtime) {
                end = min(end, ps->begin);  // stop here
                break;
            }
            S_IssuePlaysound(ps);
        }

        // clear the paint buffer
        memset(paintbuffer, 0, (end - s_paintedtime) * sizeof(samplepair_t));

        // paint in the channels.
        for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
            int ltime = s_paintedtime;

            while (ltime < end) {
                if (!ch->sfx || (!ch->leftvol && !ch->rightvol))
                    break;

                sfxcache_t *sc = S_LoadSound(ch->sfx);
                if (!sc)
                    break;

                // max painting is to the end of the buffer
                int count = min(end, ch->end) - ltime;

                if (count > 0) {
                    int func = (sc->width - 1) * 3 + (sc->channels - 1) * (S_IsFullVolume(ch) + 1);
                    paintfuncs[func](ch, sc, count, &paintbuffer[ltime - s_paintedtime]);
                    ch->pos += count;
                    ltime += count;
                }

                // if at end of loop, restart
                if (ltime >= ch->end) {
                    if (ch->autosound) {
                        // autolooping sounds always go back to start
                        ch->pos = 0;
                        ch->end = ltime + sc->length;
                    } else if (sc->loopstart >= 0) {
                        ch->pos = sc->loopstart;
                        ch->end = ltime + sc->length - ch->pos;
                    } else {
                        // channel just stopped
                        ch->sfx = NULL;
                    }
                }
            }
        }

        if (s_rawend >= s_paintedtime)
        {
          /* add from the streaming sound source */
          int stop = (end < s_rawend) ? end : s_rawend;

          for (int i = s_paintedtime; i < stop; i++)
          {
            int s = i & (S_MAX_RAW_SAMPLES - 1);
            paintbuffer[i - s_paintedtime].left += s_rawsamples[s].left;
            paintbuffer[i - s_paintedtime].right += s_rawsamples[s].right;
          }
        }

        // transfer out according to DMA format
        TransferPaintBuffer(paintbuffer, end);
        s_paintedtime = end;
    }
}

static void InitScaletable(void)
{
    snd_vol = Cvar_ClampValue(s_volume, 0, 1) * 256;

    for (int i = 0; i < 32; i++)
        for (int j = 0; j < 256; j++)
            snd_scaletable[i][j] = (j - 128) * i * 8 * snd_vol;

    s_volume->modified = false;
}

samplepair_t s_rawsamples[S_MAX_RAW_SAMPLES];
int          s_rawend = 0;


/*
 * Cinematic streaming and voice over network.
 * This could be used for chat over network, but
 * that would be terrible slow.
 */
static void
DMA_RawSamples(int samples, int rate, int width,
    int channels, byte *data, float volume)
{
  if (s_rawend < s_paintedtime)
    s_rawend = s_paintedtime;

  // mimic the OpenAL behavior: s_volume is master volume
  volume *= s_volume->value;

  // rescale because ogg is always 44100 and dma is configurable via s_khz
  float scale = (float)rate / dma.speed;
  int intVolume = (int)(256 * volume);

  if ((channels == 2) && (width == 2))
  {
    for (int i = 0; ; i++)
    {
      int src = (int)(i * scale);

      if (src >= samples)
      {
        break;
      }

      int dst = s_rawend & (S_MAX_RAW_SAMPLES - 1);
      s_rawend++;
      s_rawsamples[dst].left = ((short *)data)[src * 2] * intVolume;
      s_rawsamples[dst].right = ((short *)data)[src * 2 + 1] * intVolume;
    }
  }
  else if ((channels == 1) && (width == 2))
  {
    for (int i = 0; ; i++)
    {
      int src = (int)(i * scale);

      if (src >= samples)
      {
        break;
      }

      int dst = s_rawend & (S_MAX_RAW_SAMPLES - 1);
      s_rawend++;
      s_rawsamples[dst].left = ((short *)data)[src] * intVolume;
      s_rawsamples[dst].right = ((short *)data)[src] * intVolume;
    }
  }
  else if ((channels == 2) && (width == 1))
  {
    intVolume *= 256;

    for (int i = 0; ; i++)
    {
      int src = (int)(i * scale);

      if (src >= samples)
      {
        break;
      }

      int dst = s_rawend & (S_MAX_RAW_SAMPLES - 1);
      s_rawend++;
      s_rawsamples[dst].left =
        (((byte *)data)[src * 2] - 128) * intVolume;
      s_rawsamples[dst].right =
        (((byte *)data)[src * 2 + 1] - 128) * intVolume;
    }
  }
  else if ((channels == 1) && (width == 1))
  {
    intVolume *= 256;

    for (int i = 0; ; i++)
    {
      int src = (int)(i * scale);

      if (src >= samples)
      {
        break;
      }

      int dst = s_rawend & (S_MAX_RAW_SAMPLES - 1);
      s_rawend++;
      s_rawsamples[dst].left = (((byte *)data)[src] - 128) * intVolume;
      s_rawsamples[dst].right = (((byte *)data)[src] - 128) * intVolume;
    }
  }
}

static
void DMA_UnqueueRawSamples(void)
{
}

/*
===============================================================================

INIT / SHUTDOWN

===============================================================================
*/

#ifdef _WIN32
extern const snddma_driver_t    snddma_wave;
#endif

#if USE_SDL
extern const snddma_driver_t    snddma_sdl;
#endif

#if USE_OSS
extern const snddma_driver_t    snddma_oss;
#endif

static const snddma_driver_t *const s_drivers[] = {
#ifdef _WIN32
    &snddma_wave,
#endif
#if USE_SDL
    &snddma_sdl,
#endif
#if USE_OSS
    &snddma_oss,
#endif
    NULL
};

static snddma_driver_t  snddma;

static void DMA_SoundInfo(void)
{
    Com_Printf("%5d channels\n", dma.channels);
    Com_Printf("%5d samples\n", dma.samples);
    Com_Printf("%5d samplepos\n", dma.samplepos);
    Com_Printf("%5d samplebits\n", dma.samplebits);
    Com_Printf("%5d submission_chunk\n", dma.submission_chunk);
    Com_Printf("%5d speed\n", dma.speed);
    Com_Printf("%p dma buffer\n", dma.buffer);
}

static bool DMA_Init(void)
{
    sndinitstat_t ret = SIS_FAILURE;
    int i;

    s_khz = Cvar_Get("s_khz", "44", CVAR_ARCHIVE | CVAR_SOUND);
    s_mixahead = Cvar_Get("s_mixahead", "0.1", CVAR_ARCHIVE);
    s_testsound = Cvar_Get("s_testsound", "0", 0);
    s_swapstereo = Cvar_Get("s_swapstereo", "0", 0);
    cvar_t *s_driver = Cvar_Get("s_driver", "", CVAR_SOUND);

    for (i = 0; s_drivers[i]; i++) {
        if (!strcmp(s_drivers[i]->name, s_driver->string)) {
            snddma = *s_drivers[i];
            ret = snddma.init();
            break;
        }
    }

    if (ret != SIS_SUCCESS) {
        int tried = i;
        for (i = 0; s_drivers[i]; i++) {
            if (i == tried)
                continue;
            snddma = *s_drivers[i];
            if ((ret = snddma.init()) == SIS_SUCCESS)
                break;
        }
        Cvar_Reset(s_driver);
    }

    if (ret != SIS_SUCCESS)
        return false;

    InitScaletable();

    s_numchannels = MAX_CHANNELS;

    Com_Printf("sound sampling rate: %i\n", dma.speed);

    return true;
}

static void DMA_Shutdown(void)
{
    snddma.shutdown();
    s_numchannels = 0;
}

static void DMA_Activate(void)
{
    if (snddma.activate) {
        S_StopAllSounds();
        snddma.activate(s_active);
    }
}

/*
===============================================================================

FRAME UPDATES

===============================================================================
*/

static int DMA_DriftBeginofs(float timeofs)
{
    static int  s_beginofs;
    int         start;

    // drift s_beginofs
    start = cl.servertime * 0.001f * dma.speed + s_beginofs;
    if (start < s_paintedtime) {
        start = s_paintedtime;
        s_beginofs = start - (cl.servertime * 0.001f * dma.speed);
    } else if (start > s_paintedtime + 0.3f * dma.speed) {
        start = s_paintedtime + 0.1f * dma.speed;
        s_beginofs = start - (cl.servertime * 0.001f * dma.speed);
    } else {
        s_beginofs -= 10;
    }

    return timeofs ? start + timeofs * dma.speed : s_paintedtime;
}

static void DMA_ClearBuffer(void)
{
    snddma.begin_painting();
    if (dma.buffer)
        memset(dma.buffer, dma.samplebits == 8 ? 0x80 : 0, dma.samples * dma.samplebits / 8);
    snddma.submit();
}

/*
=================
SpatializeOrigin

Used for spatializing channels and autosounds
=================
*/
static void SpatializeOrigin(const vec3_t origin, float master_vol, float dist_mult, int *left_vol, int *right_vol)
{
    vec_t       dot;
    vec_t       dist;
    vec_t       lscale, rscale, scale;
    vec3_t      source_vec;

// calculate stereo seperation and distance attenuation
    VectorSubtract(origin, listener_origin, source_vec);

    dist = VectorNormalize(source_vec);
    dist -= SOUND_FULLVOLUME;
    if (dist < 0)
        dist = 0;           // close enough to be at full volume
    dist *= dist_mult;      // different attenuation levels

    if (dma.channels == 1) {
        rscale = 1.0f;
        lscale = 1.0f;
    } else {
        dot = DotProduct(listener_right, source_vec);
        rscale = 0.5f * (1.0f + dot);
        lscale = 0.5f * (1.0f - dot);
    }

    // add in distance effect
    scale = (1.0f - dist) * rscale;
    *right_vol = (int)(master_vol * scale);
    if (*right_vol < 0)
        *right_vol = 0;

    scale = (1.0f - dist) * lscale;
    *left_vol = (int)(master_vol * scale);
    if (*left_vol < 0)
        *left_vol = 0;
}

/*
=================
DMA_Spatialize
=================
*/
static void DMA_Spatialize(channel_t *ch)
{
    vec3_t      origin;

    // anything coming from the view entity will always be full volume
    // no attenuation = no spatialization
    if (S_IsFullVolume(ch)) {
        ch->leftvol = ch->master_vol * 255;
        ch->rightvol = ch->master_vol * 255;
        return;
    }

    if (ch->fixed_origin) {
        VectorCopy(ch->origin, origin);
    } else {
        CL_GetEntitySoundOrigin(ch->entnum, origin);
    }

    SpatializeOrigin(origin, ch->master_vol * 255, ch->dist_mult, &ch->leftvol, &ch->rightvol);
}

/*
==================
AddLoopSounds

Entities with a ->sound field will generated looped sounds
that are automatically started, stopped, and merged together
as the entities are sent to the client
==================
*/
static void AddLoopSounds(void)
{
    int         i, j;
    int         sounds[MAX_EDICTS];
    int         left, right, left_total, right_total;
    channel_t   *ch;
    sfx_t       *sfx;
    sfxcache_t  *sc;
    int         num;
    entity_state_t  *ent;
    vec3_t      origin;

    if (cls.state != ca_active || !s_active || sv_paused->integer || !s_ambient->integer)
        return;

    S_BuildSoundList(sounds);

    for (i = 0; i < cl.frame.numEntities; i++) {
        if (!sounds[i])
            continue;

        sfx = S_SfxForHandle(cl.sound_precache[sounds[i]]);
        if (!sfx)
            continue;       // bad sound effect
        sc = sfx->cache;
        if (!sc)
            continue;

        num = (cl.frame.firstEntity + i) & PARSE_ENTITIES_MASK;
        ent = &cl.entityStates[num];

        // find the total contribution of all sounds of this type
        CL_GetEntitySoundOrigin(ent->number, origin);
        SpatializeOrigin(origin, 255.0f, SOUND_LOOPATTENUATE,
                         &left_total, &right_total);
        for (j = i + 1; j < cl.frame.numEntities; j++) {
            if (sounds[j] != sounds[i])
                continue;
            sounds[j] = 0;  // don't check this again later

            num = (cl.frame.firstEntity + j) & PARSE_ENTITIES_MASK;
            ent = &cl.entityStates[num];

            CL_GetEntitySoundOrigin(ent->number, origin);
            SpatializeOrigin(origin, 255.0f, SOUND_LOOPATTENUATE,
                             &left, &right);
            left_total += left;
            right_total += right;
        }

        if (left_total == 0 && right_total == 0)
            continue;       // not audible

        // allocate a channel
        ch = S_PickChannel(0, 0);
        if (!ch)
            return;

        ch->leftvol = min(left_total, 255);
        ch->rightvol = min(right_total, 255);
        ch->master_vol = 1.0f;
        ch->dist_mult = SOUND_LOOPATTENUATE;    // for S_IsFullVolume()
        ch->autosound = true;   // remove next frame
        ch->sfx = sfx;
        ch->pos = s_paintedtime % sc->length;
        ch->end = s_paintedtime + sc->length - ch->pos;
    }
}

static int DMA_GetTime(void)
{
    static int      buffers;
    static int      oldsamplepos;
    int fullsamples = dma.samples >> (dma.channels - 1);

// it is possible to miscount buffers if it has wrapped twice between
// calls to S_Update.  Oh well.
    if (dma.samplepos < oldsamplepos) {
        buffers++;      // buffer wrapped
        if (s_paintedtime > 0x40000000) {
            // time to chop things off to avoid 32 bit limits
            buffers = 0;
            s_paintedtime = fullsamples;
            S_StopAllSounds();
        }
    }
    oldsamplepos = dma.samplepos;

    return buffers * fullsamples + (dma.samplepos >> (dma.channels - 1));
}

static void DMA_Update(void)
{
    int         i;
    channel_t   *ch;
    int         samples, soundtime, endtime;

    // rebuild scale tables if volume is modified
    if (s_volume->modified)
        InitScaletable();

    // update spatialization for dynamic sounds
    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (!ch->sfx)
            continue;

        if (ch->autosound) {
            // autosounds are regenerated fresh each frame
            memset(ch, 0, sizeof(*ch));
            continue;
        }

        DMA_Spatialize(ch);     // respatialize channel
        if (!ch->leftvol && !ch->rightvol) {
            memset(ch, 0, sizeof(*ch));
            continue;
        }
    }

    // add loopsounds
    AddLoopSounds();

#if USE_DEBUG
    if (s_show->integer) {
        int total = 0;
        for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
            if (ch->sfx && (ch->leftvol || ch->rightvol)) {
                Com_Printf("%3i %3i %s\n", ch->leftvol, ch->rightvol, ch->sfx->name);
                total++;
            }
        }
        if (s_show->integer > 1 || total) {
            Com_Printf("----(%i)---- painted: %i\n", total, s_paintedtime);
        }
    }
#endif

    snddma.begin_painting();

    if (!dma.buffer)
        return;

    // update DMA time
    soundtime = DMA_GetTime();

    // check to make sure that we haven't overshot
    if (s_paintedtime < soundtime) {
        Com_DPrintf("%s: overflow\n", __func__);
        s_paintedtime = soundtime;
    }

    // mix ahead of current position
    endtime = soundtime + Cvar_ClampValue(s_mixahead, 0, 1) * dma.speed;

    // mix to an even submission block size
    endtime = ALIGN(endtime, dma.submission_chunk);
    samples = dma.samples >> (dma.channels - 1);
    endtime = min(endtime, soundtime + samples);

    PaintChannels(endtime);

    snddma.submit();
}

const sndapi_t snd_dma = {
    .init = DMA_Init,
    .shutdown = DMA_Shutdown,
    .update = DMA_Update,
    .activate = DMA_Activate,
    .sound_info = DMA_SoundInfo,
    .upload_sfx = DMA_UploadSfx,
    .page_in_sfx = DMA_PageInSfx,
    .get_begin_ofs = DMA_DriftBeginofs,
    .play_channel = DMA_Spatialize,
    .stop_all_sounds = DMA_ClearBuffer,
    .raw_samples = DMA_RawSamples,
    .unqueue_raw_samples = DMA_UnqueueRawSamples
};
