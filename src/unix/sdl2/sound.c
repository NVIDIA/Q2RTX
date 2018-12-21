/*
Copyright (C) 2007-2008 Andrey Nazarov

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

//
// snd_sdl.c
//

#include "shared/shared.h"
#include "common/zone.h"
#include "client/sound/dma.h"
#include <SDL2/SDL.h>

static void Filler(void *userdata, Uint8 *stream, int len)
{
    int size = dma.samples << 1;
    int pos = dma.samplepos << 1;
    int wrapped = pos + len - size;

    if (wrapped < 0) {
        memcpy(stream, dma.buffer + pos, len);
        dma.samplepos += len >> 1;
    } else {
        int remaining = size - pos;
        memcpy(stream, dma.buffer + pos, remaining);
        memcpy(stream + remaining, dma.buffer, wrapped);
        dma.samplepos = wrapped >> 1;
    }
}

static void Shutdown(void)
{
    Com_Printf("Shutting down SDL audio.\n");

    SDL_CloseAudio();
    if (SDL_WasInit(SDL_INIT_EVERYTHING) == SDL_INIT_AUDIO) {
        SDL_Quit();
    } else {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    if (dma.buffer) {
        Z_Free(dma.buffer);
        dma.buffer = NULL;
    }
}

static sndinitstat_t Init(void)
{
    SDL_AudioSpec desired, obtained;
    int ret;

    if (SDL_WasInit(SDL_INIT_EVERYTHING) == 0) {
        ret = SDL_Init(SDL_INIT_AUDIO);
    } else {
        ret = SDL_InitSubSystem(SDL_INIT_AUDIO);
    }
    if (ret == -1) {
        Com_EPrintf("Couldn't initialize SDL audio: %s\n", SDL_GetError());
        return SIS_FAILURE;
    }

    memset(&desired, 0, sizeof(desired));
    switch (s_khz->integer) {
    case 48:
        desired.freq = 48000;
        break;
    case 44:
        desired.freq = 44100;
        break;
    case 22:
        desired.freq = 22050;
        break;
    default:
        desired.freq = 11025;
        break;
    }

    desired.format = AUDIO_S16LSB;
    desired.samples = 512;
    desired.channels = 2;
    desired.callback = Filler;
    ret = SDL_OpenAudio(&desired, &obtained);
    if (ret == -1) {
        Com_EPrintf("Couldn't open SDL audio: %s\n", SDL_GetError());
        return SIS_FAILURE;
    }

    if (obtained.format != AUDIO_S16LSB) {
        Com_EPrintf("SDL audio format %d unsupported.\n", obtained.format);
        goto fail;
    }

    if (obtained.channels != 1 && obtained.channels != 2) {
        Com_EPrintf("SDL audio channels %d unsupported.\n", obtained.channels);
        goto fail;
    }

    dma.speed = obtained.freq;
    dma.channels = obtained.channels;
    dma.samples = 0x8000 * obtained.channels;
    dma.submission_chunk = 1;
    dma.samplebits = 16;
    dma.buffer = Z_Mallocz(dma.samples * 2);
    dma.samplepos = 0;

    Com_Printf("Using SDL audio driver: %s\n", SDL_GetCurrentAudioDriver());

    SDL_PauseAudio(0);

    return SIS_SUCCESS;

fail:
    Shutdown();
    return SIS_FAILURE;
}

static void BeginPainting(void)
{
    SDL_LockAudio();
}

static void Submit(void)
{
    SDL_UnlockAudio();
}

static void Activate(qboolean active)
{
    if (active) {
        SDL_PauseAudio(0);
    } else {
        SDL_PauseAudio(1);
    }
}

void WAVE_FillAPI(snddmaAPI_t *api)
{
    api->Init = Init;
    api->Shutdown = Shutdown;
    api->BeginPainting = BeginPainting;
    api->Submit = Submit;
    api->Activate = Activate;
}
