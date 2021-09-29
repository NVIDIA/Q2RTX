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

// This file has been adapted from Yamagi Quake 2: 
// https://github.com/yquake2/yquake2/blob/master/src/client/sound/ogg.c

#ifndef _WIN32
#include <sys/time.h>
#endif

#include <errno.h>

#include "shared/shared.h"
#include "sound.h"
#include "client/sound/vorbis.h"

#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

static cvar_t *ogg_shuffle;        /* Shuffle playback */
static cvar_t *ogg_ignoretrack0;  /* Toggle track 0 playing */
static cvar_t *ogg_volume;        /* Music volume. */
static cvar_t* ogg_enable;        /* Music enable flag to toggle from the menu. */
static int ogg_curfile;           /* Index of currently played file. */
static int ogg_numbufs;           /* Number of buffers for OpenAL */
static int ogg_numsamples;        /* Number of sambles read from the current file */
static ogg_status_t ogg_status;   /* Status indicator. */
static stb_vorbis *ogg_file;      /* Ogg Vorbis file. */
static qboolean ogg_started;      /* Initialization flag. */

enum { MAX_NUM_OGGTRACKS = 32 };
static char* ogg_tracks[MAX_NUM_OGGTRACKS];
static int ogg_maxfileindex;


enum GameType {
	other, // incl. baseq2
	xatrix,
	rogue
};

struct {
	qboolean saved;
	int curfile;
	int numsamples;
} ogg_saved_state;

// --------

/*
 * The GOG version of Quake2 has the music tracks in music/TrackXX.ogg
 * That music/ dir is next to baseq2/ (not in it) and contains Track02.ogg to Track21.ogg
 * There
 * - Track02 to Track11 correspond to Quake2 (baseq2) CD tracks 2-11
 * - Track12 to Track21 correspond to the Ground Zero (rogue) addon's CD tracks 2-11
 * - The "The Reckoning" (xatrix) addon also had 11 tracks, that were a mix of the ones
 *   from the main game (baseq2) and the rogue addon.
 *   See below how the CD track is mapped to GOG track numbers
 */
static int getMappedGOGtrack(int track, enum GameType gameType)
{
	if(track <= 0)
		return 0;

	if(track == 1)
		return 0; // 1 is illegal (=> data track on CD), 0 means "no track"

	if(gameType == other)
		return track;
	if(gameType == rogue)
		return track + 10;

	// apparently it's xatrix => map the track to the corresponding TrackXX.ogg from GOG
	switch(track)
	{
		case  2: return 9;  // baseq2 9
		case  3: return 13; // rogue  3
		case  4: return 14; // rogue  4
		case  5: return 7;  // baseq2 6
		case  6: return 16; // rogue  6
		case  7: return 2;  // baseq2 2
		case  8: return 15; // rogue  5
		case  9: return 3;  // baseq2 3
		case 10: return 4;  // baseq2 4
		case 11: return 18; // rogue  8
		default:
			return track;
	}
}

/*
 * Load list of Ogg Vorbis files in "music/".
 */
void
OGG_InitTrackList(void)
{
	for (int i=0; i<MAX_NUM_OGGTRACKS; ++i)
	{
		if (ogg_tracks[i] != NULL)
		{
			free(ogg_tracks[i]);
			ogg_tracks[i] = NULL;
		}
	}

	ogg_maxfileindex = 0;

	const char* potMusicDirs[3] = {0};
	char gameMusicDir[MAX_QPATH] = {0}; // e.g. "xatrix/music"
	cvar_t* gameCvar = Cvar_Get("game", "", CVAR_LATCH | CVAR_SERVERINFO);

	potMusicDirs[0] = "music/"; // $mod/music/
	potMusicDirs[1] = "../music/"; // global music dir (GOG)
	potMusicDirs[2] = "../" BASEGAME "/music/"; // baseq2/music/

	enum GameType gameType = other;

	if (strcmp("xatrix", gameCvar->string) == 0)
	{
		gameType = xatrix;
	}
	else if (strcmp("rogue", gameCvar->string) == 0)
	{
		gameType = rogue;
	}

	for (int potMusicDirIdx = 0; potMusicDirIdx < sizeof(potMusicDirs)/sizeof(potMusicDirs[0]); ++potMusicDirIdx)
	{
		const char* musicDir = potMusicDirs[potMusicDirIdx];

		if (musicDir == NULL)
		{
			break;
		}

		char fullMusicPath[MAX_OSPATH] = {0};
		Q_snprintf(fullMusicPath, MAX_OSPATH, "%s/%s", fs_gamedir, musicDir);

		if(!Sys_IsDir(fullMusicPath))
		{
			continue;
		}

		char testFileName[MAX_OSPATH];
		char testFileName2[MAX_OSPATH];

		// the simple case (like before: $mod/music/02.ogg - 11.ogg or whatever)
		Q_snprintf(testFileName, MAX_OSPATH, "%s02.ogg", fullMusicPath);

		if(Sys_IsFile(testFileName))
		{
			ogg_tracks[2] = strdup(testFileName);

			for(int i=3; i<MAX_NUM_OGGTRACKS; ++i)
			{
				Q_snprintf(testFileName, MAX_OSPATH, "%s%02i.ogg", fullMusicPath, i);

				if(Sys_IsFile(testFileName))
				{
					ogg_tracks[i] = strdup(testFileName);
					ogg_maxfileindex = i;
				}
			}

			return;
		}

		// the GOG case: music/Track02.ogg to Track21.ogg
		int gogTrack = getMappedGOGtrack(8, gameType);

		Q_snprintf(testFileName, MAX_OSPATH, "%sTrack%02i.ogg", fullMusicPath, gogTrack); // uppercase T
		Q_snprintf(testFileName2, MAX_OSPATH, "%strack%02i.ogg", fullMusicPath, gogTrack); // lowercase t

		if(Sys_IsFile(testFileName) || Sys_IsFile(testFileName2))
		{
			for(int i=2; i<MAX_NUM_OGGTRACKS; ++i)
			{
				int gogTrack = getMappedGOGtrack(i, gameType);

				Q_snprintf(testFileName, MAX_OSPATH, "%sTrack%02i.ogg", fullMusicPath, gogTrack); // uppercase T
				Q_snprintf(testFileName2, MAX_OSPATH, "%strack%02i.ogg", fullMusicPath, gogTrack); // lowercase t

				if(Sys_IsFile(testFileName))
				{
					ogg_tracks[i] = strdup(testFileName);
					ogg_maxfileindex = i;
				}
				else if (Sys_IsFile(testFileName2))
				{
					ogg_tracks[i] = strdup(testFileName2);
					ogg_maxfileindex = i;
				}
			}

			return;
		}
	}

	// if tracks have been found above, we would've returned there
	Com_Printf("No Ogg Vorbis music tracks have been found, so there will be no music.\n");
}

// --------

/*
 * Play a portion of the currently opened file.
 */
void
static OGG_Read(void)
{
	short samples[4096] = {0};

	int read_samples = stb_vorbis_get_samples_short_interleaved(ogg_file, ogg_file->channels, samples,
		sizeof(samples) / ogg_file->channels);

	if (read_samples > 0)
	{
		ogg_numsamples += read_samples;

		S_RawSamples(read_samples, ogg_file->sample_rate, ogg_file->channels, ogg_file->channels,
			(byte *)samples, S_GetLinearVolume(ogg_volume->value));
	}
	else
	{
		// We cannot call OGG_Stop() here. It flushes the OpenAL sample
		// queue, thus about 12 seconds of music are lost. Instead we
		// just set the OGG state to stop and open a new file. The new
		// files content is added to the sample queue after the remaining
		// samples from the old file.
		stb_vorbis_close(ogg_file);
		ogg_status = STOP;
		ogg_numbufs = 0;
		ogg_numsamples = 0;

		OGG_PlayTrack(ogg_curfile);
	}
}

/*
 * Stream music.
 */
void
OGG_Stream(void)
{
	if (!ogg_started)
	{
		return;
	}

	if (ogg_status == PLAY)
	{
#ifdef USE_OPENAL
		if (s_started == SS_OAL)
		{
			/* Calculate the number of buffers used
			   for storing decoded OGG/Vorbis data.
			   We take the number of active buffers
			   and add 256. 256 are about 12 seconds
			   worth of sound, more than enough to
			   be resilent against underruns. */
			if (ogg_numbufs == 0 || active_buffers < ogg_numbufs - 256)
			{
				ogg_numbufs = active_buffers + 256;
			}

			/* active_buffers are all active OpenAL buffers,
			   buffering normal sfx _and_ ogg/vorbis samples. */
			while (active_buffers <= ogg_numbufs)
			{
				OGG_Read();
			}
		}
		else /* using SDL */
#endif
		{
			if (s_started == SS_DMA)
			{
				/* Read that number samples into the buffer, that
				   were played since the last call to this function.
				   This keeps the buffer at all times at an "optimal"
				   fill level. */
				while (paintedtime + S_MAX_RAW_SAMPLES - 2048 > s_rawend)
				{
					OGG_Read();
				}
			}
		}
	}
}

// --------

/*
 * play the ogg file that corresponds to the CD track with the given number
 */
void
OGG_PlayTrack(int trackNo)
{
    if (s_started == SS_NOT)
        return;

	// Track 0 means "stop music".
	if(trackNo == 0)
	{
		if(ogg_ignoretrack0->value == 0)
		{
			OGG_Stop();
		}

		// Special case: If ogg_ignoretrack0 is 0 we stopped the music (see above)
		// and ogg_curfile is still holding the last track played (track >1). So
		// this triggers and we return. If ogg_ignoretrack is 1 we didn't stop the
		// music, as soon as the tracks ends OGG_Read() starts it over. Until here
		// everything's okay.
		// But if ogg_ignoretrack0 is 1, the game was just restarted and a save game
		// load send us trackNo 0, we would end up without music. Since we have no
		// way to get the last track before trackNo 0 was set just fall through and
		// shuffle a random track (see below).
		if (ogg_curfile > 0)
		{
			return;
		}
	}

	// Player has requested shuffle playback.
	if((trackNo == 0) || ogg_shuffle->value)
	{
		if(ogg_maxfileindex > 0)
		{
			trackNo = rand() % (ogg_maxfileindex+1);
			int retries = 100;
			while(ogg_tracks[trackNo] == NULL && retries-- > 0)
			{
				trackNo = rand() % (ogg_maxfileindex+1);
			}
		}
	}

	if(ogg_maxfileindex == 0)
	{
		return; // no ogg files at all, ignore this silently instead of printing warnings all the time
	}

	if ((trackNo < 2) || (trackNo > ogg_maxfileindex))
	{
		Com_Printf("OGG_PlayTrack: %d out of range.\n", trackNo);
		return;
	}

	if(ogg_tracks[trackNo] == NULL)
	{
		Com_Printf("OGG_PlayTrack: Don't have a .ogg file for track %d\n", trackNo);
	}

	/* Check running music. */
	if (ogg_status == PLAY)
	{
		if (ogg_curfile == trackNo)
		{
			return;
		}
		else
		{
			OGG_Stop();
		}
	}

	if (ogg_tracks[trackNo] == NULL)
	{
		Com_Printf("OGG_PlayTrack: I don't have a file for track %d!\n", trackNo);

		return;
	}

	/* Open ogg vorbis file. */
	FILE* f = fopen(ogg_tracks[trackNo], "rb");

	if (f == NULL)
	{
		Com_Printf("OGG_PlayTrack: could not open file %s for track %d: %s.\n", ogg_tracks[trackNo], trackNo, strerror(errno));
		ogg_tracks[trackNo] = NULL;

		return;
	}

	int res = 0;
	ogg_file = stb_vorbis_open_file(f, qtrue, &res, NULL);

	if (res != 0)
	{
		Com_Printf("OGG_PlayTrack: '%s' is not a valid Ogg Vorbis file (error %i).\n", ogg_tracks[trackNo], res);
		fclose(f);

		return;
	}

	/* Play file. */
	ogg_curfile = trackNo;
	ogg_numsamples = 0;
	if (ogg_enable->integer)
		ogg_status = PLAY;
	else
		ogg_status = PAUSE;
}

// ----

/*
 * List Ogg Vorbis files and print current playback state.
 */
static void
OGG_Info(void)
{
	Com_Printf("Tracks:\n");
	int numFiles = 0;

	for (int i = 2; i <= ogg_maxfileindex; i++)
	{
		if(ogg_tracks[i])
		{
			Com_Printf(" - %02d %s\n", i, ogg_tracks[i]);
			++numFiles;
		}
		else
		{
			Com_Printf(" - %02d <none>\n", i);
		}
	}

	Com_Printf("Total: %d Ogg/Vorbis files.\n", ogg_maxfileindex+1);

	switch (ogg_status)
	{
		case PLAY:
			Com_Printf("State: Playing file %d (%s) at %i samples.\n",
			           ogg_curfile, ogg_tracks[ogg_curfile], stb_vorbis_get_sample_offset(ogg_file));
			break;

		case PAUSE:
			Com_Printf("State: Paused file %d (%s) at %i samples.\n",
			           ogg_curfile, ogg_tracks[ogg_curfile], stb_vorbis_get_sample_offset(ogg_file));
			break;

		case STOP:
			if (ogg_curfile == -1)
			{
				Com_Printf("State: Stopped.\n");
			}
			else
			{
				Com_Printf("State: Stopped file %d (%s).\n", ogg_curfile, ogg_tracks[ogg_curfile]);
			}

			break;
	}
}

/*
 * Stop playing the current file.
 */
void
OGG_Stop(void)
{
	if (ogg_status == STOP)
	{
		return;
	}

#ifdef USE_OPENAL
	if (s_started == SS_OAL)
	{
		AL_UnqueueRawSamples();
	}
#endif

	stb_vorbis_close(ogg_file);
	ogg_status = STOP;
	ogg_numbufs = 0;
}

/*
 * Pause or resume playback.
 */
static void
OGG_TogglePlayback(void)
{
	if (ogg_status == PLAY)
	{
		ogg_status = PAUSE;
		ogg_numbufs = 0;

#ifdef USE_OPENAL
		if (s_started == SS_OAL)
		{
			AL_UnqueueRawSamples();
		}
#endif
	}
	else if (ogg_status == PAUSE)
	{
		ogg_status = PLAY;
	}
}

/*
 * Prints a help message for the 'ogg' cmd.
 */
void
OGG_HelpMsg(void)
{
	Com_Printf("Unknown sub command %s\n\n", Cmd_Argv(1));
	Com_Printf("Commands:\n");
	Com_Printf(" - info: Print information about playback state and tracks\n");
	Com_Printf(" - play <track>: Play track number <track>\n");
	Com_Printf(" - stop: Stop playback\n");
	Com_Printf(" - toggle: Toggle pause\n");
}

/*
 * The 'ogg' cmd. Gives some control and information about the playback state.
 */
void
OGG_Cmd(void)
{
	if (Cmd_Argc() < 2)
	{
		OGG_HelpMsg();
		return;
	}

	if (Q_stricmp(Cmd_Argv(1), "info") == 0)
	{
		OGG_Info();
	}
	else if (Q_stricmp(Cmd_Argv(1), "play") == 0)
	{
		if (Cmd_Argc() != 3)
		{
			Com_Printf("ogg play <track> : Play <track>");
			return;
		}

		int track = (int)strtol(Cmd_Argv(2), NULL, 10);

		if (track < 2 || track > ogg_maxfileindex)
		{
			Com_Printf("invalid track %s, must be an number between 2 and %d\n", Cmd_Argv(1), ogg_maxfileindex);
			return;
		}
		else
		{
			OGG_PlayTrack(track);
		}
	}
	else if (Q_stricmp(Cmd_Argv(1), "stop") == 0)
	{
		OGG_Stop();
	}
	else if (Q_stricmp(Cmd_Argv(1), "toggle") == 0)
	{
		OGG_TogglePlayback();
	}
	else
	{
		OGG_HelpMsg();
	}
}

/*
 * Saves the current state of the subsystem.
 */
void
OGG_SaveState(void)
{
	if (ogg_status != PLAY)
	{
		ogg_saved_state.saved = qfalse;

		return;
	}

	ogg_saved_state.saved = qtrue;
	ogg_saved_state.curfile = ogg_curfile;
	ogg_saved_state.numsamples = ogg_numsamples;
}

/*
 * Recover the previously saved state.
 */
void
OGG_RecoverState(void)
{
	if (!ogg_saved_state.saved)
	{
		return;
	}

	// Mkay, ultra evil hack to recover the state in case of
	// shuffled playback. OGG_PlayTrack() does the shuffeling,
	// so switch it of before and enable after state recovery.
	int shuffle_state = ogg_shuffle->value;
	Cvar_SetValue(ogg_shuffle, 0, FROM_CODE);

	OGG_PlayTrack(ogg_saved_state.curfile);
	stb_vorbis_seek_frame(ogg_file, ogg_saved_state.numsamples);
	ogg_numsamples = ogg_saved_state.numsamples;

	Cvar_SetValue(ogg_shuffle, shuffle_state, FROM_CODE);
}

// --------

static void ogg_enable_changed(cvar_t *self)
{
	if ((ogg_enable->integer && ogg_status == PAUSE) || (!ogg_enable->integer && ogg_status == PLAY))
	{
		OGG_TogglePlayback();
	}
}


/*
 * Initialize the Ogg Vorbis subsystem.
 */
void
OGG_Init(void)
{
	// Cvars
	ogg_shuffle = Cvar_Get("ogg_shuffle", "0", CVAR_ARCHIVE);
	ogg_ignoretrack0 = Cvar_Get("ogg_ignoretrack0", "0", CVAR_ARCHIVE);
	ogg_volume = Cvar_Get("ogg_volume", "1.0", CVAR_ARCHIVE);
	ogg_enable = Cvar_Get("ogg_enable", "1", CVAR_ARCHIVE);
	ogg_enable->changed = ogg_enable_changed;

	// Commands
	Cmd_AddCommand("ogg", OGG_Cmd);

	// Global variables
	ogg_curfile = -1;
	ogg_numsamples = 0;
	ogg_status = STOP;

	ogg_started = qtrue;
}

/*
 * Shutdown the Ogg Vorbis subsystem.
 */
void
OGG_Shutdown(void)
{
	if (!ogg_started)
	{
		return;
	}

	// Music must be stopped.
	OGG_Stop();

	// Free file lsit.
	for(int i=0; i<MAX_NUM_OGGTRACKS; ++i)
	{
		if(ogg_tracks[i] != NULL)
		{
			free(ogg_tracks[i]);
			ogg_tracks[i] = NULL;
		}
	}
	ogg_maxfileindex = 0;

	// Remove console commands
	Cmd_RemoveCommand("ogg");

	ogg_started = qfalse;
}
