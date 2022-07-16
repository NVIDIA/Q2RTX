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

#include "shared/shared.h"
#include "common/cvar.h"
#include "client/sound/dma.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <errno.h>

#ifdef __linux__
#include <linux/soundcard.h>
#else
#include <sys/soundcard.h>
#endif

static int audio_fd;
static bool snd_inited;
static struct audio_buf_info info;

static cvar_t *s_bits;
static cvar_t *s_channels;
static cvar_t *s_device;

static const int tryrates[] = { 22050, 11025, 44100, 48000, 8000 };

static sndinitstat_t OSS_Init(void)
{
    int rc;
    int fmt;
    int tmp;
    int i;
    int caps;

    if (snd_inited)
        return SIS_SUCCESS;

    s_bits = Cvar_Get("s_bits", "16", CVAR_SOUND);
    s_channels = Cvar_Get("s_channels", "2", CVAR_SOUND);
    s_device = Cvar_Get("s_device", "/dev/dsp", CVAR_SOUND);

// open /dev/dsp, confirm capability to mmap, and get size of dma buffer

    audio_fd = open(s_device->string, O_RDWR);

    if (audio_fd < 0) {
        Com_WPrintf("Could not open %s: %s\n", s_device->string,
                    strerror(errno));
        return SIS_FAILURE;
    }

    if (ioctl(audio_fd, SNDCTL_DSP_GETCAPS, &caps) == -1) {
        Com_WPrintf("Could not get caps of %s: %s\n", s_device->string,
                    strerror(errno));
        goto fail;
    }

    if ((caps & (DSP_CAP_TRIGGER | DSP_CAP_MMAP)) !=
        (DSP_CAP_TRIGGER | DSP_CAP_MMAP)) {
        Com_WPrintf("%s does not support TRIGGER and/or MMAP capabilities\n",
                    s_device->string);
        goto fail;
    }


// set sample bits & speed

    dma.samplebits = s_bits->integer;
    if (dma.samplebits != 16 && dma.samplebits != 8) {
        ioctl(audio_fd, SNDCTL_DSP_GETFMTS, &fmt);
        if (fmt & AFMT_S16_LE) {
            dma.samplebits = 16;
        } else if (fmt & AFMT_U8) {
            dma.samplebits = 8;
        }
    }

    switch (s_khz->integer) {
    case 48:
        dma.speed = 48000;
        break;
    case 44:
        dma.speed = 44100;
        break;
    case 22:
        dma.speed = 22050;
        break;
    case 11:
        dma.speed = 11025;
        break;
    default:
        for (i = 0; i < sizeof(tryrates) / 4; i++) {
            if (!ioctl(audio_fd, SNDCTL_DSP_SPEED, &tryrates[i]))
                break;
        }
        if (i == sizeof(tryrates) / 4) {
            Com_WPrintf("%s supports no valid bitrates\n", s_device->string);
            goto fail;
        }
        dma.speed = tryrates[i];
        break;
    }

    dma.channels = s_channels->integer;

    tmp = 0;
    if (dma.channels == 2)
        tmp = 1;
    rc = ioctl(audio_fd, SNDCTL_DSP_STEREO, &tmp);
    if (rc < 0) {
        Com_WPrintf("Could not set %s to %d channels: %s\n", s_device->string,
                    dma.channels, strerror(errno));
        goto fail;
    }
    if (tmp)
        dma.channels = 2;
    else
        dma.channels = 1;

    rc = ioctl(audio_fd, SNDCTL_DSP_SPEED, &dma.speed);
    if (rc < 0) {
        Com_WPrintf("Could not set %s speed to %d: %s\n", s_device->string,
                    dma.speed, strerror(errno));
        goto fail;
    }

    if (dma.samplebits == 16) {
        rc = AFMT_S16_LE;
        rc = ioctl(audio_fd, SNDCTL_DSP_SETFMT, &rc);
        if (rc < 0) {
            Com_WPrintf("Could not support 16-bit data.  Try 8-bit.\n");
            goto fail;
        }
    } else if (dma.samplebits == 8) {
        rc = AFMT_U8;
        rc = ioctl(audio_fd, SNDCTL_DSP_SETFMT, &rc);
        if (rc < 0) {
            Com_WPrintf("Could not support 8-bit data.\n");
            goto fail;
        }
    } else {
        Com_WPrintf("%d-bit sound not supported.\n", dma.samplebits);
        goto fail;
    }

    if (ioctl(audio_fd, SNDCTL_DSP_GETOSPACE, &info) == -1) {
        Com_WPrintf("Could not do GETOSPACE: %s\n", strerror(errno));
        goto fail;
    }

    dma.samples = info.fragstotal * info.fragsize / (dma.samplebits >> 3);
    dma.submission_chunk = 1;

// memory map the dma buffer
    dma.buffer = (byte *) mmap(NULL, info.fragstotal * info.fragsize,
                               PROT_WRITE, MAP_FILE | MAP_SHARED, audio_fd, 0);
    if (!dma.buffer) {
        Com_WPrintf("Could not mmap %s: %s\n", s_device->string,
                    strerror(errno));
        goto fail;
    }

// toggle the trigger & start her up

    tmp = 0;
    rc  = ioctl(audio_fd, SNDCTL_DSP_SETTRIGGER, &tmp);
    if (rc < 0) {
        Com_WPrintf("Could not toggle (0): %s\n", strerror(errno));
        munmap(dma.buffer, info.fragstotal * info.fragsize);
        goto fail;
    }

    tmp = PCM_ENABLE_OUTPUT;
    rc = ioctl(audio_fd, SNDCTL_DSP_SETTRIGGER, &tmp);
    if (rc < 0) {
        Com_WPrintf("Could not toggle (PCM_ENABLE_OUTPUT): %s\n",
                    strerror(errno));
        munmap(dma.buffer, info.fragstotal * info.fragsize);
        goto fail;
    }

    Com_Printf("OSS initialization succeeded\n");

    dma.samplepos = 0;

    snd_inited = true;
    return SIS_SUCCESS;

fail:
    close(audio_fd);
    return SIS_FAILURE;
}

static void OSS_Shutdown(void)
{
    if (snd_inited) {
        Com_Printf("Shutting down OSS\n");
        ioctl(audio_fd, SNDCTL_DSP_RESET);
        munmap(dma.buffer, info.fragstotal * info.fragsize);
        close(audio_fd);
        snd_inited = false;
    }
}

static void OSS_BeginPainting(void)
{
    struct count_info count;

    if (!snd_inited)
        return;

    if (ioctl(audio_fd, SNDCTL_DSP_GETOPTR, &count) == -1) {
        Com_EPrintf("SNDCTL_DSP_GETOPTR failed on %s: %s\n",
                    s_device->string, strerror(errno));
        OSS_Shutdown();
        return;
    }
    dma.samplepos = count.ptr / (dma.samplebits >> 3);
}

static void OSS_Submit(void)
{
}

const snddma_driver_t snddma_oss = {
    .name = "oss",
    .init = OSS_Init,
    .shutdown = OSS_Shutdown,
    .begin_painting = OSS_BeginPainting,
    .submit = OSS_Submit,
};
