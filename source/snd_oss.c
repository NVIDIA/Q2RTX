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
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/wait.h>
#ifdef __linux__
#include <linux/soundcard.h>
#else
#include <sys/soundcard.h>
#endif
#include <stdio.h>
#include <errno.h>

#include "com_local.h"
#include "snd_local.h"

static int audio_fd;
static int snd_inited;
static struct audio_buf_info info;

static cvar_t *sndbits;
static cvar_t *sndspeed;
static cvar_t *sndchannels;
static cvar_t *snddevice;

static int tryrates[] = { 22050, 11025, 44100, 48000, 8000 };

static sndinitstat_t OSS_Init ( void ) {
    int rc;
    int fmt;
    int tmp;
    int i;
    int caps;

    if ( snd_inited )
        return SIS_SUCCESS;

    sndbits = Cvar_Get ( "sndbits", "16", CVAR_ARCHIVE|CVAR_LATCHED );
    sndspeed = Cvar_Get ( "sndspeed", "22050", CVAR_ARCHIVE|CVAR_LATCHED );
    sndchannels = Cvar_Get ( "sndchannels", "2", CVAR_ARCHIVE|CVAR_LATCHED );
    snddevice = Cvar_Get ( "snddevice", "/dev/dsp", CVAR_ARCHIVE|CVAR_LATCHED );

// open /dev/dsp, confirm capability to mmap, and get size of dma buffer

    audio_fd = open ( snddevice->string, O_RDWR );

    if ( audio_fd < 0 ) {
        Com_WPrintf ( "Could not open %s: %s\n", snddevice->string,
            strerror ( errno ) );
        return SIS_FAILURE;
    }

    if ( ioctl ( audio_fd, SNDCTL_DSP_GETCAPS, &caps ) == -1 ) {
        Com_WPrintf ( "Could not get caps of %s: %s\n", snddevice->string,
            strerror ( errno ) );
        goto fail;
    }

    if( ( caps & (DSP_CAP_TRIGGER|DSP_CAP_MMAP) ) !=
        (DSP_CAP_TRIGGER|DSP_CAP_MMAP) )
    {
        Com_WPrintf ( "%s does not support TRIGGER and/or MMAP capabilities\n",
            snddevice->string );
        goto fail;
    }


// set sample bits & speed

    dma.samplebits = sndbits->integer;
    if ( dma.samplebits != 16 && dma.samplebits != 8 ) {
        ioctl ( audio_fd, SNDCTL_DSP_GETFMTS, &fmt );
        if ( fmt & AFMT_S16_LE ) {
            dma.samplebits = 16;
        } else if ( fmt & AFMT_U8 ) {
            dma.samplebits = 8;
        }
    }

    dma.speed = sndspeed->integer;
    if ( !dma.speed ) {
        for ( i = 0; i < sizeof ( tryrates ) / 4; i++ ) {
            if ( !ioctl ( audio_fd, SNDCTL_DSP_SPEED, &tryrates[i] ) )
                break;
        }
        if ( i == sizeof ( tryrates ) / 4 ) {
            Com_WPrintf ( "%s supports no valid bitrates", snddevice->string );
            goto fail;
        }
        dma.speed = tryrates[i];
    }

    Cvar_ClampInteger ( sndchannels, 1, 2 );

    dma.channels = sndchannels->integer;

    tmp = 0;
    if ( dma.channels == 2 )
        tmp = 1;
    rc = ioctl ( audio_fd, SNDCTL_DSP_STEREO, &tmp );
    if ( rc < 0 ) {
        Com_WPrintf ( "Could not set %s to %d channels: %s", snddevice->string,
            dma.channels, strerror ( errno ) );
        goto fail;
    }
    if ( tmp )
        dma.channels = 2;
    else
        dma.channels = 1;

    rc = ioctl ( audio_fd, SNDCTL_DSP_SPEED, &dma.speed );
    if ( rc < 0 ) {
        Com_WPrintf ( "Could not set %s speed to %d: %s", snddevice->string,
            dma.speed, strerror ( errno ) );
        goto fail;
    }

    if ( dma.samplebits == 16 ) {
        rc = AFMT_S16_LE;
        rc = ioctl ( audio_fd, SNDCTL_DSP_SETFMT, &rc );
        if ( rc < 0 ) {
            Com_WPrintf ( "Could not support 16-bit data.  Try 8-bit.\n" );
            goto fail;
        }
    } else if ( dma.samplebits == 8 ) {
        rc = AFMT_U8;
        rc = ioctl ( audio_fd, SNDCTL_DSP_SETFMT, &rc );
        if ( rc < 0 ) {
            Com_WPrintf ( "Could not support 8-bit data.\n" );
            goto fail;
        }
    } else {
        Com_WPrintf ( "%d-bit sound not supported.", dma.samplebits );
        goto fail;
    }

    if ( ioctl ( audio_fd, SNDCTL_DSP_GETOSPACE, &info ) ==-1 ) {
        Com_WPrintf ( "Could not do GETOSPACE: %s\n", strerror ( errno ) );
        goto fail;
    }

    dma.samples = info.fragstotal * info.fragsize / ( dma.samplebits >> 3 );
    dma.submission_chunk = 1;

// memory map the dma buffer
    dma.buffer = ( byte * ) mmap ( NULL, info.fragstotal * info.fragsize,
        PROT_WRITE, MAP_FILE|MAP_SHARED, audio_fd, 0 );
    if ( !dma.buffer ) {
        Com_WPrintf ( "Could not mmap %s: %s\n", snddevice->string,
            strerror ( errno ) );
        goto fail;
    }

// toggle the trigger & start her up

    tmp = 0;
    rc  = ioctl ( audio_fd, SNDCTL_DSP_SETTRIGGER, &tmp );
    if ( rc < 0 ) {
        Com_WPrintf ( "Could not toggle (0): %s\n", strerror ( errno ) );
        munmap ( dma.buffer, info.fragstotal * info.fragsize );
        goto fail;
    }

    tmp = PCM_ENABLE_OUTPUT;
    rc = ioctl ( audio_fd, SNDCTL_DSP_SETTRIGGER, &tmp );
    if ( rc < 0 ) {
        Com_WPrintf ( "Could not toggle (PCM_ENABLE_OUTPUT): %s\n",
            strerror ( errno ) );
        munmap ( dma.buffer, info.fragstotal * info.fragsize );
        goto fail;
    }

    Com_Printf ( "OSS initialization succeeded\n" );

    dma.samplepos = 0;

    snd_inited = 1;
    return SIS_SUCCESS;

fail:
    close ( audio_fd );
    return SIS_FAILURE;
}

static void OSS_Shutdown ( void ) {
    if ( snd_inited ) {
        Com_Printf ( "Shutting down OSS\n" );
        ioctl ( audio_fd, SNDCTL_DSP_RESET );
        munmap ( dma.buffer, info.fragstotal * info.fragsize );
        close ( audio_fd );
        snd_inited = qfalse;
    }
}

static void OSS_BeginPainting ( void ) {
    struct count_info count;

    if ( !snd_inited )
        return;

    if ( ioctl ( audio_fd, SNDCTL_DSP_GETOPTR, &count ) == -1 ) {
        Com_EPrintf ( "SNDCTL_DSP_GETOPTR failed on %s: %s\n",
            snddevice->string, strerror ( errno ) );
        OSS_Shutdown();
        return;
    }
    dma.samplepos = count.ptr / ( dma.samplebits >> 3 );
}

static void OSS_Submit ( void ) {
}

static void OSS_Activate ( qboolean active ) {
}

void OSS_FillAPI ( snddmaAPI_t *api ) {
    api->Init = OSS_Init;
    api->Shutdown = OSS_Shutdown;
    api->BeginPainting = OSS_BeginPainting;
    api->Submit = OSS_Submit;
    api->Activate = OSS_Activate;
}

