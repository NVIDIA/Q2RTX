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
// snd_mix.c -- portable code to mix sounds for snd_dma.c

#include "sound.h"

#define    PAINTBUFFER_SIZE    2048

static int snd_scaletable[32][256];
static int snd_vol;

samplepair_t s_rawsamples[S_MAX_RAW_SAMPLES];
int          s_rawend = 0;

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
    if (s_testsound->integer) {
        int i;

        // write a fixed sine wave
        for (i = 0; i < endtime - s_paintedtime; i++) {
            samp[i].left = samp[i].right = sin((s_paintedtime + i) * 0.1f) * 20000 * 256;
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

PAINTFUNC(PaintStereo8)
{
    int vol = ch->master_vol * 255;
    int *scale = snd_scaletable[vol >> 3];
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

PAINTFUNC(PaintStereo16)
{
    int vol = ch->master_vol * 255 * snd_vol;
    int16_t *sfx = (int16_t *)sc->data + ch->pos * 2;

    for (int i = 0; i < count; i++, samp++, sfx += 2) {
        samp->left += (sfx[0] * vol) >> 8;
        samp->right += (sfx[1] * vol) >> 8;
    }
}

static const paintfunc_t paintfuncs[] = {
    PaintMono8,
    PaintStereo8,
    PaintMono16,
    PaintStereo16
};

void S_PaintChannels(int endtime)
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
                    int func = (sc->width - 1) * 2 + (sc->channels - 1);
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


void S_InitScaletable(void)
{
    snd_vol = S_GetLinearVolume(s_volume->value) * 256;
    for (int i = 0; i < 32; i++)
        for (int j = 0; j < 256; j++)
            snd_scaletable[i][j] = (j - 128) * i * 8 * snd_vol;

    s_volume->modified = false;
}

/*
 * Cinematic streaming and voice over network.
 * This could be used for chat over network, but
 * that would be terrible slow.
 */
void
S_RawSamples(int samples, int rate, int width,
    int channels, byte *data, float volume)
{
  if (!s_started) return;

  if (s_started == SS_OAL)
  {
	  AL_RawSamples(samples, rate, width, channels, data, volume);
	  return;
  }

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

void S_UnqueueRawSamples()
{
#ifdef USE_OPENAL
    if (s_started == SS_OAL)
    {
        AL_UnqueueRawSamples();
    }
#endif
}
