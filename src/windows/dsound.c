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

#include "client.h"
#include "client/sound/dma.h"
#include <mmsystem.h>
#include <dsound.h>

typedef HRESULT(WINAPI *LPDIRECTSOUNDCREATE)(LPCGUID, LPDIRECTSOUND *, LPUNKNOWN);

#define SECONDARY_BUFFER_SIZE   0x10000

// starts at 0 for disabled
static int  sample16;

static DWORD    locksize;

static MMTIME       mmstarttime;

static LPDIRECTSOUND pDS;
static LPDIRECTSOUNDBUFFER pDSBuf, pDSPBuf;

static HINSTANCE hInstDS;

static DWORD        gSndBufSize;

static const char *DSoundError(int error)
{
    switch (error) {
    case DSERR_BUFFERLOST:
        return "DSERR_BUFFERLOST";
    case DSERR_INVALIDCALL:
        return "DSERR_INVALIDCALL";
    case DSERR_INVALIDPARAM:
        return "DSERR_INVALIDPARAM";
    case DSERR_PRIOLEVELNEEDED:
        return "DSERR_PRIOLEVELNEEDED";
    }

    return "<unknown error>";
}

/*
** DS_DestroyBuffers
*/
static void DS_DestroyBuffers(void)
{
    Com_DPrintf("Destroying DS buffers\n");
    if (pDS) {
        Com_DPrintf("...setting NORMAL coop level\n");
        IDirectSound_SetCooperativeLevel(pDS, win.wnd, DSSCL_NORMAL);
    }

    if (pDSBuf) {
        Com_DPrintf("...stopping and releasing sound buffer\n");
        IDirectSoundBuffer_Stop(pDSBuf);
        IDirectSoundBuffer_Release(pDSBuf);
    }

    // only release primary buffer if it's not also the mixing buffer we just released
    if (pDSPBuf && (pDSBuf != pDSPBuf)) {
        Com_DPrintf("...releasing primary buffer\n");
        IDirectSoundBuffer_Release(pDSPBuf);
    }
    pDSBuf = NULL;
    pDSPBuf = NULL;

    dma.buffer = NULL;
}

/*
==============
DS_Shutdown

Reset the sound device for exiting
===============
*/
static void DS_Shutdown(void)
{
    Com_Printf("Shutting down DirectSound\n");

    if (pDS) {
        DS_DestroyBuffers();

        Com_DPrintf("...releasing DS object\n");
        IDirectSound_Release(pDS);
    }

    if (hInstDS) {
        Com_DPrintf("...freeing DSOUND.DLL\n");
        FreeLibrary(hInstDS);
        hInstDS = NULL;
    }

    pDS = NULL;
    pDSBuf = NULL;
    pDSPBuf = NULL;
}

/*
** DS_CreateBuffers
*/
static bool DS_CreateBuffers(void)
{
    DSBUFFERDESC    dsbuf;
    DSBCAPS         dsbcaps;
    WAVEFORMATEX    format;
    DWORD           dwWrite;

    memset(&format, 0, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = dma.channels;
    format.wBitsPerSample = dma.samplebits;
    format.nSamplesPerSec = dma.speed;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.cbSize = sizeof(format);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    Com_DPrintf("Creating DS buffer\n");

    Com_DPrintf("...setting PRIORITY coop level: ");
    if (DS_OK != IDirectSound_SetCooperativeLevel(pDS, win.wnd, DSSCL_PRIORITY)) {
        Com_DPrintf("failed\n");
        return false;
    }
    Com_DPrintf("ok\n");

// create the secondary buffer we'll actually work with
    memset(&dsbuf, 0, sizeof(dsbuf));
    dsbuf.dwSize = sizeof(DSBUFFERDESC);
    dsbuf.dwFlags = DSBCAPS_CTRLFREQUENCY | DSBCAPS_LOCHARDWARE;
    dsbuf.dwBufferBytes = SECONDARY_BUFFER_SIZE;
    dsbuf.lpwfxFormat = &format;

    memset(&dsbcaps, 0, sizeof(dsbcaps));
    dsbcaps.dwSize = sizeof(dsbcaps);

    Com_DPrintf("...creating secondary buffer: ");
    if (DS_OK != IDirectSound_CreateSoundBuffer(pDS, &dsbuf, &pDSBuf, NULL)) {
        dsbuf.dwFlags = DSBCAPS_CTRLFREQUENCY | DSBCAPS_LOCSOFTWARE;
        if (DS_OK != IDirectSound_CreateSoundBuffer(pDS, &dsbuf, &pDSBuf, NULL)) {
            Com_DPrintf("failed\n");
            return false;
        }

        Com_DPrintf("ok\n...forced to software\n");
    } else  {
        Com_DPrintf("ok\n...locked hardware\n");
    }

    dma.channels = format.nChannels;
    dma.samplebits = format.wBitsPerSample;
    dma.speed = format.nSamplesPerSec;

    if (DS_OK != IDirectSoundBuffer_GetCaps(pDSBuf, &dsbcaps)) {
        Com_DPrintf("*** GetCaps failed ***\n");
        return false;
    }

    // Make sure mixer is active
    if (DS_OK != IDirectSoundBuffer_Play(pDSBuf, 0, 0, DSBPLAY_LOOPING)) {
        Com_DPrintf("*** Play failed ***\n");
        return false;
    }

    Com_DPrintf("   %d channel(s)\n"
                "   %d bits/sample\n"
                "   %d bytes/sec\n",
                dma.channels, dma.samplebits, dma.speed);

    gSndBufSize = dsbcaps.dwBufferBytes;

    IDirectSoundBuffer_Stop(pDSBuf);
    IDirectSoundBuffer_GetCurrentPosition(pDSBuf, &mmstarttime.u.sample, &dwWrite);
    IDirectSoundBuffer_Play(pDSBuf, 0, 0, DSBPLAY_LOOPING);

    dma.samples = gSndBufSize / (dma.samplebits / 8);
    dma.samplepos = 0;
    dma.submission_chunk = 1;
    dma.buffer = NULL;
    sample16 = (dma.samplebits / 8) - 1;

    return true;
}



/*
==================
DS_Init

Direct-Sound support
==================
*/
static sndinitstat_t DS_Init(void)
{
    DSCAPS          dscaps;
    HRESULT         hresult;
    LPDIRECTSOUNDCREATE pDirectSoundCreate;

    memset(&dma, 0, sizeof(dma));
    dma.channels = 2;
    dma.samplebits = 16;

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
    default:
        dma.speed = 11025;
        break;
    }

    Com_DPrintf("Initializing DirectSound\n");

    if (!hInstDS) {
        Com_DPrintf("...loading dsound.dll: ");
        hInstDS = LoadLibrary("dsound.dll");
        if (hInstDS == NULL) {
            Com_DPrintf("failed\n");
            return SIS_FAILURE;
        }
        Com_DPrintf("ok\n");
    }

    pDirectSoundCreate = (LPDIRECTSOUNDCREATE)
                         GetProcAddress(hInstDS, "DirectSoundCreate");
    if (!pDirectSoundCreate) {
        Com_DPrintf("...couldn't get DS proc addr\n");
        return SIS_FAILURE;
    }

    Com_DPrintf("...creating DS object: ");
    while ((hresult = pDirectSoundCreate(NULL, &pDS, NULL)) != DS_OK) {
        if (hresult != DSERR_ALLOCATED) {
            Com_DPrintf("failed\n");
            return SIS_FAILURE;
        }

        if (MessageBox(NULL,
                       "The sound hardware is in use by another app.\n\n"
                       "Select Retry to try to start sound again or Cancel to run " PRODUCT " with no sound.",
                       "Sound not available",
                       MB_RETRYCANCEL | MB_SETFOREGROUND | MB_ICONEXCLAMATION) != IDRETRY) {
            Com_DPrintf("failed, hardware already in use\n");
            return SIS_NOTAVAIL;
        }
    }
    Com_DPrintf("ok\n");

    dscaps.dwSize = sizeof(dscaps);

    if (DS_OK != IDirectSound_GetCaps(pDS, &dscaps)) {
        Com_DPrintf("...couldn't get DS caps\n");
        DS_Shutdown();
        return SIS_FAILURE;
    }

    if (dscaps.dwFlags & DSCAPS_EMULDRIVER) {
        Com_DPrintf("...no DSound driver found\n");
        DS_Shutdown();
        return SIS_FAILURE;
    }

    if (!DS_CreateBuffers()) {
        DS_Shutdown();
        return SIS_FAILURE;
    }

    Com_Printf("DirectSound initialized\n");

    return SIS_SUCCESS;
}

/*
==============
DS_BeginPainting

Makes sure dma.buffer is valid.

Returns the current sample position (in mono samples read)
inside the recirculating dma buffer, so the mixing code will know
how many sample are required to fill it up.
===============
*/
static void DS_BeginPainting(void)
{
    int     reps, s;
    DWORD   dwSize2;
    DWORD   *pbuf, *pbuf2;
    HRESULT hresult;
    DWORD   dwStatus, dwWrite;
    MMTIME  mmtime;

    if (!pDSBuf)
        return;

    // get sample pos
    mmtime.wType = TIME_SAMPLES;
    IDirectSoundBuffer_GetCurrentPosition(pDSBuf, &mmtime.u.sample, &dwWrite);
    s = (mmtime.u.sample - mmstarttime.u.sample) >> sample16;
    dma.samplepos = s & (dma.samples - 1);

    // if the buffer was lost or stopped, restore it and/or restart it
    if (IDirectSoundBuffer_GetStatus(pDSBuf, &dwStatus) != DS_OK) {
        Com_EPrintf("DS_BeginPainting: Couldn't get sound buffer status\n");
        DS_Shutdown();
        return;
    }

    if (dwStatus & DSBSTATUS_BUFFERLOST)
        IDirectSoundBuffer_Restore(pDSBuf);

    if (!(dwStatus & DSBSTATUS_PLAYING))
        IDirectSoundBuffer_Play(pDSBuf, 0, 0, DSBPLAY_LOOPING);

    // lock the dsound buffer

    reps = 0;
    dma.buffer = NULL;

    while ((hresult = IDirectSoundBuffer_Lock(pDSBuf, 0, gSndBufSize, (void **)&pbuf, &locksize,
                      (void **)&pbuf2, &dwSize2, 0)) != DS_OK) {
        if (hresult != DSERR_BUFFERLOST) {
            Com_EPrintf("DS_BeginPainting: Lock failed with error '%s'\n", DSoundError(hresult));
            DS_Shutdown();
            return;
        }

        IDirectSoundBuffer_Restore(pDSBuf);

        if (++reps > 2)
            return;
    }
    dma.buffer = (byte *)pbuf;
}

/*
==============
DS_Submit

Send sound to device if buffer isn't really the dma buffer
Also unlocks the dsound buffer
===============
*/
static void DS_Submit(void)
{
    if (!pDSBuf)
        return;

    // unlock the dsound buffer
    IDirectSoundBuffer_Unlock(pDSBuf, dma.buffer, locksize, NULL, 0);
}

/*
===========
DS_Activate

Called when the main window gains or loses focus.
The window have been destroyed and recreated
between a deactivate and an activate.
===========
*/
static void DS_Activate(bool active)
{
    if (!pDS) {
        return;
    }
    if (active) {
        if (!DS_CreateBuffers()) {
            Com_EPrintf("DS_Activate: DS_CreateBuffers failed\n");
            DS_Shutdown();
        }
    } else {
        DS_DestroyBuffers();
    }
}

void DS_FillAPI(snddmaAPI_t *api)
{
    api->Init = DS_Init;
    api->Shutdown = DS_Shutdown;
    api->BeginPainting = DS_BeginPainting;
    api->Submit = DS_Submit;
    api->Activate = DS_Activate;
}
