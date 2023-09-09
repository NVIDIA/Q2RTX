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

#if defined(__GNUC__)
// Warnings produced by std_vorbis
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wunused-value"
#endif

#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

typedef struct {
	// Initialization flag.
	bool initialized;
	// Ogg Vorbis file.
	stb_vorbis *vf;
} ogg_state_t;

static ogg_state_t  ogg;

typedef enum
{
	PLAY,
	PAUSE,
	STOP
} ogg_status_t;

static cvar_t *ogg_shuffle;        /* Shuffle playback */
static cvar_t *ogg_ignoretrack0;  /* Toggle track 0 playing */
static cvar_t *ogg_volume;        /* Music volume. */
static cvar_t* ogg_enable;        /* Music enable flag to toggle from the menu. */
static int ogg_numsamples;        /* Number of sambles read from the current file */
static ogg_status_t ogg_status;   /* Status indicator. */

enum { MAX_NUM_OGGTRACKS = 128 };
static void     **tracklist;
static int      trackcount;
static int      trackindex;


enum GameType {
	other, // incl. baseq2
	xatrix,
	rogue
};

struct {
	bool saved;
	int trackindex;
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
		case  5: return 7;  // baseq2 7
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

static void tracklist_free(void)
{
	FS_FreeList(tracklist);
	tracklist = NULL;
	trackcount = 0;
}

static void tracklist_set(int index, const char* str)
{
	if (index >= trackcount)
	{
		// Put a NULL element past the last entry, so FS_FreeList() can be used
		tracklist = Z_Realloc(tracklist, (index + 2) * sizeof(char *));
		memset(tracklist + trackcount, 0, ((index + 2) - trackcount) * sizeof(char *));
	}
	else
		Z_Free(tracklist[index]);
	tracklist[index] = Z_CopyString(str);
	trackcount = max(trackcount, index + 1);
}

// --------

static void ogg_stop(void)
{
	if (ogg.initialized)
		stb_vorbis_close(ogg.vf);

	ogg.vf = NULL;
	ogg_status = STOP;

	ogg.initialized = false;
}

/*
 * play the ogg file that corresponds to the CD track with the given number
 */
static void
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
			return;
		}

		// Special case: If ogg_ignoretrack0 is 0 we stopped the music (see above)
		// and trackindex is still holding the last track played (track >1). So
		// this triggers and we return. If ogg_ignoretrack is 1 we didn't stop the
		// music, as soon as the tracks ends OGG_Update() starts it over. Until here
		// everything's okay.
		// But if ogg_ignoretrack0 is 1, the game was just restarted and a save game
		// load send us trackNo 0, we would end up without music. Since we have no
		// way to get the last track before trackNo 0 was set just fall through and
		// shuffle a random track (see below).
		if (trackindex > 0)
		{
			return;
		}
	}

	// Player has requested shuffle playback.
	if((trackNo == 0) || ogg_shuffle->value)
	{
		if(trackcount > 0)
		{
			trackNo = Q_rand() % trackcount;
			int retries = 100;
			while(tracklist[trackNo] == NULL && retries-- > 0)
			{
				trackNo = Q_rand() % trackcount;
			}
		}
	}

	if(trackcount == 0)
	{
		return; // no ogg files at all, ignore this silently instead of printing warnings all the time
	}

	if ((trackNo < 2) || (trackNo >= trackcount))
	{
		Com_Printf("OGG_PlayTrack: %d out of range.\n", trackNo);
		return;
	}

	if(tracklist[trackNo] == NULL)
	{
		Com_Printf("OGG_PlayTrack: Don't have a .ogg file for track %d\n", trackNo);
	}

	/* Check running music. */
	if (ogg_status == PLAY)
	{
		if (trackindex == trackNo)
		{
			return;
		}
		else
		{
			OGG_Stop();
		}
	}

	if (tracklist[trackNo] == NULL)
	{
		Com_Printf("OGG_PlayTrack: I don't have a file for track %d!\n", trackNo);

		return;
	}

	/* Open ogg vorbis file. */
	FILE* f = fopen(tracklist[trackNo], "rb");

	if (f == NULL)
	{
		Com_Printf("OGG_PlayTrack: could not open file %s for track %d: %s.\n", tracklist[trackNo], trackNo, strerror(errno));
		tracklist[trackNo] = NULL;

		return;
	}

	int res = 0;
	ogg.vf = stb_vorbis_open_file(f, true, &res, NULL);

	if (res != 0)
	{
		Com_Printf("OGG_PlayTrack: '%s' is not a valid Ogg Vorbis file (error %i).\n", tracklist[trackNo], res);
		fclose(f);

		return;
	}

	/* Play file. */
	trackindex = trackNo;
	ogg_numsamples = 0;
	if (ogg_enable->integer)
		ogg_status = PLAY;
	else
		ogg_status = PAUSE;

	ogg.initialized = true;
}

void
OGG_Play(void)
{
	int cdtrack = atoi(cl.configstrings[CS_CDTRACK]);
	OGG_PlayTrack(cdtrack);
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

	ogg_stop();

	if (s_started)
		s_api.drop_raw_samples();
}

/*
 * Stream music.
 */
void
OGG_Update(void)
{
	if (!ogg.initialized)
		return;

	if (!s_started)
		return;

	if (!s_active)
		return;

	if (ogg_status != PLAY)
		return;

	while (s_api.need_raw_samples()) {
		short   buffer[4096];
		int     samples;

		samples = stb_vorbis_get_samples_short_interleaved(ogg.vf, ogg.vf->channels, buffer,
														   sizeof(buffer) / sizeof(short));
		if (samples <= 0)
		{
			// We cannot call OGG_Stop() here. It flushes the OpenAL sample
			// queue, thus about 12 seconds of music are lost. Instead we
			// just set the OGG state to stop and open a new file. The new
			// files content is added to the sample queue after the remaining
			// samples from the old file.
			ogg_stop();
			ogg_numsamples = 0;

			OGG_PlayTrack(trackindex);
			break;
		}

		ogg_numsamples += samples;

		if (!s_api.raw_samples(samples, ogg.vf->sample_rate, ogg.vf->channels, ogg.vf->channels,
			(byte *)buffer, S_GetLinearVolume(ogg_volume->value)))
		{
			s_api.drop_raw_samples();
			break;
		}
	}
}

/*
 * Load list of Ogg Vorbis files in "music/".
 */
void
OGG_Reload(void)
{
	tracklist_free();

	const char* potMusicDirs[4] = {0};
	char fullMusicDir[MAX_OSPATH] = {0};
	cvar_t* gameCvar = Cvar_Get("game", "", CVAR_LATCH | CVAR_SERVERINFO);

	Q_snprintf(fullMusicDir, sizeof(fullMusicDir), "%s/" BASEGAME "/music/", sys_basedir->string);

	potMusicDirs[0] = "music/"; // $mod/music/
	potMusicDirs[1] = "../music/"; // global music dir (GOG)
	potMusicDirs[2] = "../" BASEGAME "/music/"; // baseq2/music/
	potMusicDirs[3] = fullMusicDir; // e.g. "/usr/share/games/xatrix/music"

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
		if (strcmp(musicDir, fullMusicDir) == 0) {
			Q_snprintf(fullMusicPath, MAX_OSPATH, "%s", musicDir);
		}
		else
		{
			Q_snprintf(fullMusicPath, MAX_OSPATH, "%s/%s", fs_gamedir, musicDir);
		}

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
			tracklist_set(2, testFileName);

			for(int i=3; i<MAX_NUM_OGGTRACKS; ++i)
			{
				Q_snprintf(testFileName, MAX_OSPATH, "%s%02i.ogg", fullMusicPath, i);

				if(Sys_IsFile(testFileName))
				{
					tracklist_set(i, testFileName);
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
					tracklist_set(i, testFileName);
				}
				else if (Sys_IsFile(testFileName2))
				{
					tracklist_set(i, testFileName2);
				}
			}

			return;
		}
	}

	// if tracks have been found above, we would've returned there
	Com_Printf("No Ogg Vorbis music tracks have been found, so there will be no music.\n");
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

	for (int i = 2; i < trackcount; i++)
	{
		if(tracklist[i])
		{
			Com_Printf(" - %02d %s\n", i, tracklist[i]);
			++numFiles;
		}
		else
		{
			Com_Printf(" - %02d <none>\n", i);
		}
	}

	Com_Printf("Total: %d Ogg/Vorbis files.\n", trackcount);

	switch (ogg_status)
	{
		case PLAY:
			Com_Printf("State: Playing file %d (%s) at %i samples.\n",
			           trackindex, tracklist[trackindex], stb_vorbis_get_sample_offset(ogg.vf));
			break;

		case PAUSE:
			Com_Printf("State: Paused file %d (%s) at %i samples.\n",
			           trackindex, tracklist[trackindex], stb_vorbis_get_sample_offset(ogg.vf));
			break;

		case STOP:
			if (trackindex == -1)
			{
				Com_Printf("State: Stopped.\n");
			}
			else
			{
				Com_Printf("State: Stopped file %d (%s).\n", trackindex, tracklist[trackindex]);
			}

			break;
	}
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

		s_api.drop_raw_samples();
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

		if (track < 2 || track >= trackcount)
		{
			Com_Printf("invalid track %s, must be an number between 2 and %d\n", Cmd_Argv(1), trackcount - 1);
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
		ogg_saved_state.saved = false;
		return;
	}

	ogg_saved_state.saved = true;
	ogg_saved_state.trackindex = trackindex;
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

	OGG_PlayTrack(ogg_saved_state.trackindex);
	stb_vorbis_seek_frame(ogg.vf, ogg_saved_state.numsamples);
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
	trackindex = -1;
	ogg_numsamples = 0;
	ogg_status = STOP;

	OGG_Reload();
}

/*
 * Shutdown the Ogg Vorbis subsystem.
 */
void
OGG_Shutdown(void)
{
	// Music must be stopped.
	ogg_stop();

	// Free file lsit.
	tracklist_free();

	// Remove console commands
	Cmd_RemoveCommand("ogg");
}
