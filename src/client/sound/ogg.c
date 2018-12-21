/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307,
 * USA.
 *
 * =======================================================================
 *
 * This file implements an interface to libvorbis for decoding
 * OGG/Vorbis files. Strongly spoken this file isn't part of the sound
 * system but part of the main client. It justs converts Vorbis streams
 * into normal, raw Wave stream which are injected into the backends as 
 * if they were normal "raw" samples. At this moment only background
 * music playback and in theory .cin movie file playback is supported.
 *
 * =======================================================================
 */

#ifdef OGG

#define OV_EXCLUDE_STATIC_CALLBACKS

#ifndef _WIN32
#include <sys/time.h>
#endif
#include <errno.h>
#include <vorbis/vorbisfile.h>

#include "shared/shared.h"
#include "common/cvar.h"
#include "common/files.h"
#include "client/client.h"
#include "client/sound/sound.h"
#include "client/sound/ogg.h"
#include "sound.h"

qboolean ogg_started = qfalse;   /* Initialization flag. */
byte *ogg_buffer = 0;            /* File buffer. */
char ovBuf[4096];                /* Buffer for sound. */
int ogg_curfile = -1;            /* Index of currently played file. */
int ovSection = 0;               /* Position in Ogg Vorbis file. */
ogg_status_t ogg_status = STOP;  /* Status indicator. */
cvar_t *ogg_volume = 0;          /* Music volume. */
OggVorbis_File ovFile;           /* Ogg Vorbis file. */
vorbis_info *ogg_info = 0;       /* Ogg Vorbis file information */

void
OGG_Init(void)
{
    if (ogg_started) return;

    Com_Printf("Starting Ogg Vorbis.\n");

    /* Skip initialization if disabled. */
    cvar_t *cv = Cvar_Get("ogg_enable", "1", CVAR_ARCHIVE);

    if (cv->value != 1)
    {
        Com_Printf("Ogg Vorbis not initializing.\n");
        return;
    }

    /* Cvars. */
    ogg_volume = Cvar_Get("ogg_volume", "0.7", CVAR_ARCHIVE);

    /* Console commands. */
    Cmd_AddCommand("ogg_pause", OGG_PauseCmd);
    Cmd_AddCommand("ogg_play", OGG_PlayCmd);
    Cmd_AddCommand("ogg_reinit", OGG_Reinit);
    Cmd_AddCommand("ogg_resume", OGG_ResumeCmd);
    Cmd_AddCommand("ogg_seek", OGG_SeekCmd);
    Cmd_AddCommand("ogg_status", OGG_StatusCmd);
    Cmd_AddCommand("ogg_stop", OGG_Stop);

    ogg_started = qtrue;
}

void
OGG_Shutdown(void)
{
    if (!ogg_started) return;

    Com_Printf("Shutting down Ogg Vorbis.\n");

    OGG_Stop();

    /* Remove console commands. */
    Cmd_RemoveCommand("ogg_pause");
    Cmd_RemoveCommand("ogg_play");
    Cmd_RemoveCommand("ogg_reinit");
    Cmd_RemoveCommand("ogg_resume");
    Cmd_RemoveCommand("ogg_seek");
    Cmd_RemoveCommand("ogg_status");
    Cmd_RemoveCommand("ogg_stop");

    ogg_started = qfalse;
}

void
OGG_Reinit(void)
{
    OGG_Shutdown();
    OGG_Init();
}

void
OGG_Seek(ogg_seek_t type, double offset)
{
    /* Check if the file is seekable. */
    if (ov_seekable(&ovFile) == 0)
    {
        Com_Printf("OGG_Seek: file is not seekable.\n");
        return;
    }

    /* Get file information. */
    double pos = ov_time_tell(&ovFile);
    double total = ov_time_total(&ovFile, -1);

    switch (type)
    {
        case ABS:
            if ((offset >= 0) && (offset <= total))
            {
                if (ov_time_seek(&ovFile, offset) != 0)
                    Com_Printf("OGG_Seek: could not seek.\n");
                else
                    Com_Printf("%0.2f -> %0.2f of %0.2f.\n", pos, offset, total);
            }
            else
            {
                Com_Printf("OGG_Seek: invalid offset.\n");
            }
            break;
        case REL:
            if ((pos + offset >= 0) && (pos + offset <= total))
            {
                if (ov_time_seek(&ovFile, pos + offset) != 0)
                    Com_Printf("OGG_Seek: could not seek.\n");
                else
                    Com_Printf("%0.2f -> %0.2f of %0.2f.\n",
                            pos, pos + offset, total);
            }
            else
            {
                Com_Printf("OGG_Seek: invalid offset.\n");
            }
            break;
    }
}

// Play Ogg Vorbis file (with absolute or relative index).
qboolean
OGG_Open(ogg_seek_t type, int offset)
{
    int size;     /* File size. */
    int pos = -1; /* Absolute position. */
    int res;      /* Error indicator. */

    switch (type)
    {
        case ABS:
            /* Absolute index. */
            pos = offset;
            break;
        case REL:
            /* Simulate a loopback. */
            if ((ogg_curfile == -1) && (offset < 1))
                offset++;
            if (ogg_curfile + offset < 1)
                offset = 1;
            pos = ogg_curfile + offset;
            break;
    }

    /* Check running music. */
    if (ogg_status == PLAY)
    {
        if (ogg_curfile == pos)
            return qtrue;
        else
            OGG_Stop();
    }

    char filename[1024];
    snprintf(filename, sizeof(filename), "%s/%d.ogg", OGG_DIR, pos);
    /* Find file. */
    if ((size = FS_LoadFile(filename, (void **)&ogg_buffer)) == -1)
    {
        Com_Printf("OGG_Open: could not open %d (%s): %s.\n",
                pos, filename, strerror(errno));
        return qfalse;
    }

    /* Open ogg vorbis file. */
    if ((res = ov_open(NULL, &ovFile, (char *)ogg_buffer, size)) < 0)
    {
        Com_Printf("OGG_Open: '%s' is not a valid Ogg Vorbis file (error %i).\n",
                filename, res);
        FS_FreeFile(ogg_buffer);
        ogg_buffer = NULL;
        return qfalse;
    }

    ogg_info = ov_info(&ovFile, 0);

    if (!ogg_info)
    {
        Com_Printf("OGG_Open: Unable to get stream information for %s.\n",
                filename);
        ov_clear(&ovFile);
        FS_FreeFile(ogg_buffer);
        ogg_buffer = NULL;
        return qfalse;
    }

    /* Play file. */
    ovSection = 0;
    ogg_curfile = pos;
    ogg_status = PLAY;

    return qtrue;
}

// Play a portion of the currently opened file.
int
OGG_Read(void)
{
    if(!ogg_info) return 0;

    /* Read and resample. */
    int res = ov_read(&ovFile, ovBuf, sizeof(ovBuf),
            0 /* big endian */, OGG_SAMPLEWIDTH, 1,
            &ovSection);
    S_RawSamples(res / (OGG_SAMPLEWIDTH * ogg_info->channels),
            ogg_info->rate, OGG_SAMPLEWIDTH, ogg_info->channels,
            (byte *)ovBuf, ogg_volume->value);

    /* Check for end of file. */
    if (res == 0) OGG_Stop();

    return res;
}

// Stop playing the current file.
void
OGG_Stop(void)
{
    if (ogg_status == STOP)
        return;

    ov_clear(&ovFile);
    ogg_status = STOP;
    ogg_info = 0;

    if (ogg_buffer)
    {
        FS_FreeFile(ogg_buffer);
        ogg_buffer = 0;
    }
}

// Stream music.
void
OGG_Stream(void)
{
    if (!ogg_started)
        return;

    if (ogg_status == PLAY)
    {
        /* Read that number samples into the buffer, that
           were played since the last call to this function.
           This keeps the buffer at all times at an "optimal"
           fill level. */
        while (paintedtime + S_MAX_RAW_SAMPLES - 2048 > s_rawend)
        {
            if(!ogg_info) return;
            OGG_Read();
        }
    }
}

// Parse play controls.
void
OGG_ParseCmd(char *arg)
{
    cvar_t *ogg_enable = Cvar_Get("ogg_enable", "1", CVAR_ARCHIVE);

    switch (arg[0])
    {
        case '#':
            OGG_Open(ABS, atol(arg+1));
            break;
        case '>':
            if (strlen(arg) > 1)
                OGG_Open(REL, atol(arg+1));
            else
                OGG_Open(REL, 1);
            break;
        case '<':
            if (strlen(arg) > 1)
                OGG_Open(REL, -atol(arg+1));
            else
                OGG_Open(REL, -1);
            break;
        default:
            if (ogg_enable->value != 0)
                OGG_Open(ABS, atol(arg));
            break;
    }
}

void
OGG_PauseCmd(void)
{
    ogg_status = PAUSE;
}

void
OGG_PlayCmd(void)
{
    if (Cmd_Argc() < 2)
        Com_Printf("Usage: ogg_play {filename | #n | >n | <n}\n");
    else
        OGG_ParseCmd(Cmd_Argv(1));
}

void
OGG_ResumeCmd(void)
{
    ogg_status = PLAY;
}

// Change position in the file being played.
void
OGG_SeekCmd(void)
{
    if (ogg_status != STOP)
        return;

    if (Cmd_Argc() < 2)
    {
        Com_Printf("Usage: ogg_seek {n | <n | >n}\n");
        return;
    }

    switch (Cmd_Argv(1)[0])
    {
        case '>':
            OGG_Seek(REL, strtod(Cmd_Argv(1) + 1, (char **)NULL));
            break;
        case '<':
            OGG_Seek(REL, -strtod(Cmd_Argv(1) + 1, (char **)NULL));
            break;
        default:
            OGG_Seek(ABS, strtod(Cmd_Argv(1), (char **)NULL));
            break;
    }
}

void
OGG_StatusCmd(void)
{
    switch (ogg_status)
    {
        case PLAY:
            Com_Printf("Playing file %d at %0.2f seconds.\n",
                    ogg_curfile + 1,
                    ov_time_tell(&ovFile));
            break;
        case PAUSE:
            Com_Printf("Paused file %d at %0.2f seconds.\n",
                    ogg_curfile + 1,
                    ov_time_tell(&ovFile));
            break;
        case STOP:
            if (ogg_curfile == -1)
                Com_Printf("Stopped.\n");
            else
                Com_Printf("Stopped file %d.\n",
                        ogg_curfile + 1);
            break;
    }
}
#endif
/* vim: set ts=8 sw=4 tw=0 et : */
