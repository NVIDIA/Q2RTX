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

// 64K is > 1 second at 16-bit, 22050 Hz
#define WAV_BUFFERS             64
#define WAV_MASK                (WAV_BUFFERS - 1)
#define WAV_BUFFER_SIZE         0x0400

static bool wav_init;

// starts at 0 for disabled
static int  sample16;
static int  snd_sent, snd_completed;

static HANDLE       hData;
static HPSTR        lpData;

static HGLOBAL      hWaveHdr;
static LPWAVEHDR    lpWaveHdr;

static HWAVEOUT    hWaveOut;

static DWORD        gSndBufSize;

/*
==============
WAVE_Shutdown

Reset the sound device for exiting
===============
*/
static void WAVE_Shutdown(void)
{
    int     i;

    Com_Printf("Shutting down wave sound\n");

    if (hWaveOut) {
        Com_DPrintf("...resetting waveOut\n");
        waveOutReset(hWaveOut);

        if (lpWaveHdr) {
            Com_DPrintf("...unpreparing headers\n");
            for (i = 0; i < WAV_BUFFERS; i++)
                waveOutUnprepareHeader(hWaveOut, lpWaveHdr + i, sizeof(WAVEHDR));
        }

        Com_DPrintf("...closing waveOut\n");
        waveOutClose(hWaveOut);

        if (hWaveHdr) {
            Com_DPrintf("...freeing WAV header\n");
            GlobalUnlock(hWaveHdr);
            GlobalFree(hWaveHdr);
        }

        if (hData) {
            Com_DPrintf("...freeing WAV buffer\n");
            GlobalUnlock(hData);
            GlobalFree(hData);
        }

    }

    hWaveOut = 0;
    hData = 0;
    hWaveHdr = 0;
    lpData = NULL;
    lpWaveHdr = NULL;
    wav_init = false;
}


/*
==================
WAVE_Init

Crappy windows multimedia base
==================
*/
static sndinitstat_t WAVE_Init(void)
{
    WAVEFORMATEX  format;
    int             i;
    HRESULT         hr;

    Com_DPrintf("Initializing wave sound\n");

    snd_sent = 0;
    snd_completed = 0;

    memset(&dma, 0, sizeof(dma));
    dma.channels = 2;
    dma.samplebits = 16;

    if (s_khz->integer == 44)
        dma.speed = 44100;
    else if (s_khz->integer == 22)
        dma.speed = 22050;
    else
        dma.speed = 11025;

    memset(&format, 0, sizeof(format));
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = dma.channels;
    format.wBitsPerSample = dma.samplebits;
    format.nSamplesPerSec = dma.speed;
    format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
    format.cbSize = 0;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    /* Open a waveform device for output using window callback. */
    Com_DPrintf("...opening waveform device: ");
    while ((hr = waveOutOpen((LPHWAVEOUT)&hWaveOut, WAVE_MAPPER,
                             &format,
                             0, 0L, CALLBACK_NULL)) != MMSYSERR_NOERROR) {
        if (hr != MMSYSERR_ALLOCATED) {
            Com_DPrintf("failed\n");
            return SIS_FAILURE;
        }

        if (MessageBox(NULL,
                       _T("The sound hardware is in use by another app.\n\n")
                       _T("Select Retry to try to start sound again or Cancel to run ") _T("q2pro") _T(" with no sound."),
                       _T("Sound not available"),
                       MB_RETRYCANCEL | MB_SETFOREGROUND | MB_ICONEXCLAMATION) != IDRETRY) {
            Com_DPrintf("hw in use\n");
            return SIS_NOTAVAIL;
        }
    }
    Com_DPrintf("ok\n");

    /*
     * Allocate and lock memory for the waveform data. The memory
     * for waveform data must be globally allocated with
     * GMEM_MOVEABLE and GMEM_SHARE flags.

    */
    Com_DPrintf("...allocating waveform buffer: ");
    gSndBufSize = WAV_BUFFERS * WAV_BUFFER_SIZE;
    hData = GlobalAlloc(GMEM_MOVEABLE /*| GMEM_SHARE*/, gSndBufSize);
    if (!hData) {
        Com_DPrintf(" failed with error %#lx\n", GetLastError());
        WAVE_Shutdown();
        return SIS_FAILURE;
    }
    Com_DPrintf("ok\n");

    Com_DPrintf("...locking waveform buffer: ");
    lpData = GlobalLock(hData);
    if (!lpData) {
        Com_DPrintf(" failed with error %#lx\n", GetLastError());
        WAVE_Shutdown();
        return SIS_FAILURE;
    }
    memset(lpData, 0, gSndBufSize);
    Com_DPrintf("ok\n");

    /*
     * Allocate and lock memory for the header. This memory must
     * also be globally allocated with GMEM_MOVEABLE and
     * GMEM_SHARE flags.
     */
    Com_DPrintf("...allocating waveform header: ");
    hWaveHdr = GlobalAlloc(GMEM_MOVEABLE /*| GMEM_SHARE*/,
                           (DWORD) sizeof(WAVEHDR) * WAV_BUFFERS);
    if (hWaveHdr == NULL) {
        Com_DPrintf("failed with error %#lx\n", GetLastError());
        WAVE_Shutdown();
        return SIS_FAILURE;
    }
    Com_DPrintf("ok\n");

    Com_DPrintf("...locking waveform header: ");
    lpWaveHdr = (LPWAVEHDR) GlobalLock(hWaveHdr);
    if (lpWaveHdr == NULL) {
        Com_DPrintf("failed with error %#lx\n", GetLastError());
        WAVE_Shutdown();
        return SIS_FAILURE;
    }
    memset(lpWaveHdr, 0, sizeof(WAVEHDR) * WAV_BUFFERS);
    Com_DPrintf("ok\n");

    /* After allocation, set up and prepare headers. */
    Com_DPrintf("...preparing headers: ");
    for (i = 0; i < WAV_BUFFERS; i++) {
        lpWaveHdr[i].dwBufferLength = WAV_BUFFER_SIZE;
        lpWaveHdr[i].lpData = lpData + i * WAV_BUFFER_SIZE;

        if (waveOutPrepareHeader(hWaveOut, lpWaveHdr + i, sizeof(WAVEHDR)) !=
            MMSYSERR_NOERROR) {
            Com_DPrintf("failed\n");
            WAVE_Shutdown();
            return SIS_FAILURE;
        }
    }
    Com_DPrintf("ok\n");

    dma.samples = gSndBufSize / (dma.samplebits / 8);
    dma.samplepos = 0;
    dma.submission_chunk = 512;
    dma.buffer = (byte *) lpData;
    sample16 = (dma.samplebits / 8) - 1;

    Com_Printf("Wave sound initialized\n");
    wav_init = true;

    return SIS_SUCCESS;
}

/*
==============
WAVE_BeginPainting

Makes sure dma.buffer is valid.

Returns the current sample position (in mono samples read)
inside the recirculating dma buffer, so the mixing code will know
how many sample are required to fill it up.
===============
*/
static void WAVE_BeginPainting(void)
{
    int     s;

    if (!wav_init) {
        return;
    }

    s = (snd_sent * WAV_BUFFER_SIZE) >> sample16;
    dma.samplepos = s & (dma.samples - 1);
}

/*
==============
WAVE_Submit

Send sound to device if buffer isn't really the dma buffer
Also unlocks the dsound buffer
===============
*/
static void WAVE_Submit(void)
{
    LPWAVEHDR   h;
    int         wResult;

    if (!dma.buffer)
        return;

    if (!wav_init)
        return;

    //
    // find which sound blocks have completed
    //
    while (1) {
        if (snd_completed == snd_sent) {
            Com_DPrintf("WAVE_Submit: Sound overrun\n");
            break;
        }

        if (!(lpWaveHdr[snd_completed & WAV_MASK].dwFlags & WHDR_DONE)) {
            break;
        }

        snd_completed++;    // this buffer has been played
    }

    //
    // submit a few new sound blocks
    //
    while (((snd_sent - snd_completed) >> sample16) < 8) {
        h = lpWaveHdr + (snd_sent & WAV_MASK);
        if (paintedtime / 256 <= snd_sent)
            break;
        snd_sent++;
        /*
         * Now the data block can be sent to the output device. The
         * waveOutWrite function returns immediately and waveform
         * data is sent to the output device in the background.
         */
        wResult = waveOutWrite(hWaveOut, h, sizeof(WAVEHDR));

        if (wResult != MMSYSERR_NOERROR) {
            Com_EPrintf("WAVE_Submit: Failed to write block to device\n");
            WAVE_Shutdown();
            return;
        }
    }
}


/*
===========
WAVE_Activate

Called when the main window gains or loses focus.
The window have been destroyed and recreated
between a deactivate and an activate.
===========
*/
static void WAVE_Activate(bool active)
{
}

void WAVE_FillAPI(snddmaAPI_t *api)
{
    api->Init = WAVE_Init;
    api->Shutdown = WAVE_Shutdown;
    api->BeginPainting = WAVE_BeginPainting;
    api->Submit = WAVE_Submit;
    api->Activate = WAVE_Activate;
}
