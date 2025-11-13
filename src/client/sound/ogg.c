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

typedef enum
{
	TRACK_STYLE_SIMPLE, 	// %02i.ogg
	TRACK_STYLE_MIXEDCASE,	// Track%02i.ogg
	TRACK_STYLE_LOWERCASE	// track%02i.ogg
} track_name_style_t;

typedef struct {
	// Initialization flag.
	bool initialized;
	// Ogg Vorbis file.
	stb_vorbis *vf;
	char path[MAX_OSPATH];
	// music directory (full native path)
	char *music_dir;
	// style of track file names
	track_name_style_t track_name_style;
	// track number mapping function
	int (*map_track)(int);
} ogg_state_t;

static ogg_state_t  ogg;

typedef enum
{
	PLAY,
	PAUSE,
	STOP
} ogg_status_t;

static cvar_t* ogg_enable;        /* Music enable flag to toggle from the menu. */
static cvar_t *ogg_volume;        /* Music volume. */
static cvar_t *ogg_shuffle;       /* Shuffle playback */
static cvar_t *ogg_ignoretrack0;  /* Toggle track 0 playing */
static int ogg_numsamples;        /* Number of sambles read from the current file */
static ogg_status_t ogg_status;   /* Status indicator. */

enum { MAX_NUM_OGGTRACKS = 128 };
static void     **tracklist;
static int      trackcount;
static int      trackindex;


struct {
	bool saved;
	char path[MAX_OSPATH];
	int numsamples;
} ogg_saved_state;

// --------

static int map_track_identity(int track)
{
	return track;
}

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
static int map_track_rogue(int track)
{
	return track + 10;
}

static int map_track_xatrix(int track)
{
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

static int map_track(int track)
{
	if(track <= 0)
		return 0;

	if(track == 1)
		return 0; // 1 is illegal (=> data track on CD), 0 means "no track"

	return ogg.map_track(track);
}

static void tracklist_free(void)
{
	FS_FreeList(tracklist);
	tracklist = NULL;
	trackcount = trackindex = 0;
}

// --------

static void ogg_stop(void)
{
	stb_vorbis_close(ogg.vf);

	ogg.vf = NULL;
	ogg_status = STOP;

	ogg.initialized = false;
}

static void ogg_play(void)
{
	/* Open ogg vorbis file. */
	FILE* f = fopen(ogg.path, "rb");

	if (f == NULL)
	{
		Com_Printf("OGG_PlayTrack: could not open file %s: %s.\n", ogg.path, strerror(errno));

		return;
	}

	int res = 0;
	ogg.vf = stb_vorbis_open_file(f, true, &res, NULL);

	if (res != 0)
	{
		Com_Printf("OGG_PlayTrack: '%s' is not a valid Ogg Vorbis file (error %i).\n", ogg.path, res);
		fclose(f);
		goto fail;
	}

	if (ogg.vf->channels < 1 || ogg.vf->channels > 2) {
		Com_EPrintf("%s has bad number of channels\n", ogg.path);
		goto fail;
	}

	/* Play file. */
	ogg_numsamples = 0;
	if (ogg_enable->integer)
		ogg_status = PLAY;
	else
		ogg_status = PAUSE;

	Com_DPrintf("Playing %s\n", ogg.path);

	ogg.initialized = true;
	return;

fail:
	ogg_stop();
}

static void shuffle(void)
{
	for (int i = trackcount - 1; i > 0; i--) {
		int j = Q_rand_uniform(i + 1);
		SWAP(void *, tracklist[i], tracklist[j]);
	}
}

static void get_track_path(char* buf, size_t size, int track)
{
	switch(ogg.track_name_style)
	{
	case TRACK_STYLE_SIMPLE:
		Q_snprintf(buf, size, "%s%02i.ogg", ogg.music_dir, ogg.map_track(track));
		break;
	case TRACK_STYLE_MIXEDCASE:
		Q_snprintf(buf, size, "%sTrack%02i.ogg", ogg.music_dir, ogg.map_track(track));
		break;
	case TRACK_STYLE_LOWERCASE:
	default:
		Q_snprintf(buf, size, "%strack%02i.ogg", ogg.music_dir, ogg.map_track(track));
		break;
	}
}

/*
 * play the ogg file that corresponds to the CD track with the given number
 */
static void
OGG_PlayTrack(const char* track_str)
{
	if (s_started == SS_NOT)
		return;

	if(trackcount == 0)
	{
		return; // no ogg files at all, ignore this silently instead of printing warnings all the time
	}

	// Track 0 means "stop music".
	if(!*track_str || !strcmp(track_str, "0"))
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
		if (*ogg.path)
		{
			return;
		}
	}

	char current_path[MAX_OSPATH];
	Q_strlcpy(current_path, ogg.path, sizeof(current_path));
	// Player has requested shuffle playback.
	if((!*track_str || !strcmp(track_str, "0")) || (ogg_shuffle->integer && trackcount))
	{
		if (trackindex == 0)
			shuffle();
		Q_snprintf(ogg.path, sizeof(ogg.path), "%s%s.ogg", ogg.music_dir, (const char*)tracklist[trackindex]);
		trackindex = (trackindex + 1) % trackcount;
	} else if (COM_IsUint(track_str)) {
		int trackNo = Q_atoi(track_str);
		get_track_path(ogg.path, sizeof(ogg.path), trackNo);
	 } else {
		Q_snprintf(ogg.path, sizeof(ogg.path), "%s/%s/music/%s.ogg", sys_basedir->string, *fs_game->string ? fs_game->string : BASEGAME, track_str);
	}

	/* Check running music. */
	if (ogg_status == PLAY)
	{
		if (strcmp(current_path, ogg.path) == 0)
		{
			return;
		}
		else
		{
			OGG_Stop();
		}
	}

    ogg_play();
}

void
OGG_Play(void)
{
	OGG_PlayTrack(cl.configstrings[CS_CDTRACK]);
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
		if (samples == 0) {
			ogg_status = STOP;
			if(OGG_Play(), ogg.initialized)
				samples = stb_vorbis_get_samples_short_interleaved(ogg.vf, ogg.vf->channels, buffer,
																sizeof(buffer) / sizeof(short));
		}

		if (samples <= 0)
			break;

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
OGG_LoadTrackList(void)
{
	tracklist_free();
	Z_Freep((void**)&ogg.music_dir);

	const char* potMusicDirs[4] = {0};
	char fullMusicDir[MAX_OSPATH] = {0};
	cvar_t* gameCvar = Cvar_Get("game", "", CVAR_LATCH | CVAR_SERVERINFO);

	Q_snprintf(fullMusicDir, sizeof(fullMusicDir), "%s/" BASEGAME "/music/", sys_basedir->string);

	potMusicDirs[0] = "music/"; // $mod/music/
	potMusicDirs[1] = "../music/"; // global music dir (GOG)
	potMusicDirs[2] = "../" BASEGAME "/music/"; // baseq2/music/
	potMusicDirs[3] = fullMusicDir; // e.g. "/usr/share/games/xatrix/music"

	if (strcmp("xatrix", gameCvar->string) == 0)
	{
		ogg.map_track = map_track_xatrix;
	}
	else if (strcmp("rogue", gameCvar->string) == 0)
	{
		ogg.map_track = map_track_rogue;
	}
	else
		ogg.map_track = map_track_identity;

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
			ogg.music_dir = Z_CopyString(fullMusicPath);
			ogg.track_name_style = TRACK_STYLE_SIMPLE;
		}
		else
		{
			// the GOG case: music/Track02.ogg to Track21.ogg
			int gogTrack = map_track(8);

			Q_snprintf(testFileName, MAX_OSPATH, "%sTrack%02i.ogg", fullMusicPath, gogTrack); // uppercase T
			Q_snprintf(testFileName2, MAX_OSPATH, "%strack%02i.ogg", fullMusicPath, gogTrack); // lowercase t

			if(Sys_IsFile(testFileName))
			{
				ogg.music_dir = Z_CopyString(fullMusicPath);
				ogg.track_name_style = TRACK_STYLE_MIXEDCASE;
			}
			else if (Sys_IsFile(testFileName2))
			{
				ogg.music_dir = Z_CopyString(fullMusicPath);
				ogg.track_name_style = TRACK_STYLE_LOWERCASE;
			}
		}

		if(ogg.music_dir)
		{
			listfiles_t track_list;
			memset(&track_list, 0, sizeof(track_list));
			track_list.flags = FS_SEARCH_STRIPEXT;
			track_list.filter = ".ogg";
			Sys_ListFiles_r(&track_list, ogg.music_dir, 0);

			tracklist = Z_Mallocz(sizeof(char *) * (track_list.count + 1));
			for (int i = 0; i < track_list.count; i++) {
				tracklist[i] = track_list.files[i];
			}
			trackcount = track_list.count;

			return;
		}
	}

	// if tracks have been found above, we would've returned there
	Com_Printf("No Ogg Vorbis music tracks have been found, so there will be no music.\n");
}

// ----

bool OGG_Load(sizebuf_t *sz)
{
	int ret;
	stb_vorbis *vf = stb_vorbis_open_memory(sz->data, sz->cursize, &ret, NULL);
	if (!vf) {
		Com_DPrintf("%s does not appear to be an Ogg bitstream (error %d)\n", s_info.name, ret);
		return false;
	}

	if (vf->channels < 1 || vf->channels > 2) {
		Com_DPrintf("%s has bad number of channels\n", s_info.name);
		goto fail;
	}

	if (vf->sample_rate < 8000 || vf->sample_rate > 48000) {
		Com_DPrintf("%s has bad rate\n", s_info.name);
		goto fail;
	}

	unsigned int samples = stb_vorbis_stream_length_in_samples(vf);
	if (samples < 1 || samples > MAX_LOADFILE >> vf->channels) {
		Com_DPrintf("%s has bad number of samples\n", s_info.name);
		goto fail;
	}

	unsigned int size = samples << vf->channels;
	int offset = 0;

	s_info.channels = vf->channels;
	s_info.rate = vf->sample_rate;
	s_info.width = 2;
	s_info.loopstart = -1;
	s_info.data = FS_AllocTempMem(size);

	while (offset < size) {
		ret = stb_vorbis_get_samples_short_interleaved(vf, vf->channels, (short*)(s_info.data + offset), (size - offset) / sizeof(short));
		if (ret == 0)
			break;

		offset += ret;
	}

	s_info.samples = offset >> s_info.channels;

	stb_vorbis_close(vf);
	return true;

fail:
	stb_vorbis_close(vf);
	return false;
}

/*
 * List Ogg Vorbis files and print current playback state.
 */
static void
OGG_Info_f(void)
{
	Com_Printf("Tracks:\n");
	int numFiles = 0;

	// Print unshuffled track list
	for (int i = 2; i < MAX_NUM_OGGTRACKS; i++)
	{
		char ogg_path[MAX_OSPATH];
		get_track_path(ogg_path, sizeof(ogg_path), i);

		if(Sys_IsFile(ogg_path))
		{
			Com_Printf(" - %02d %s\n", i, ogg_path);
			++numFiles;
		}
	}

	Com_Printf("Total: %d Ogg/Vorbis files.\n", numFiles);

	switch (ogg_status)
	{
		case PLAY:
			Com_Printf("State: Playing file %s at %i samples.\n",
			           ogg.path, stb_vorbis_get_sample_offset(ogg.vf));
			break;

		case PAUSE:
			Com_Printf("State: Paused file %s at %i samples.\n",
			           ogg.path, stb_vorbis_get_sample_offset(ogg.vf));
			break;

		case STOP:
			if (trackindex == -1)
			{
				Com_Printf("State: Stopped.\n");
			}
			else
			{
				Com_Printf("State: Stopped file %s.\n", ogg.path);
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
static void
OGG_HelpMsg(void)
{
	Com_Printf("Unknown sub command %s\n\n", Cmd_Argv(1));
	Com_Printf("Commands:\n");
	Com_Printf(" - info: Print information about playback state and tracks\n");
	Com_Printf(" - play <track>: Play track number <track>\n");
	Com_Printf(" - stop: Stop playback\n");
	Com_Printf(" - toggle: Toggle pause\n");
}

static void OGG_Play_f(void)
{
	if (Cmd_Argc() < 3)
	{
		Com_Printf("Usage: %s %s <track>\n", Cmd_Argv(0), Cmd_Argv(1));
		return;
	}

	if (!s_started) {
		Com_Printf("Sound system not started.\n");
		return;
	}

	if (cls.state == ca_cinematic) {
		Com_Printf("Can't play music in cinematic mode.\n");
		return;
	}

	OGG_PlayTrack(Cmd_Argv(2));
}

/*
 * The 'ogg' cmd. Gives some control and information about the playback state.
 */
static void OGG_Cmd_c(genctx_t *ctx, int argnum)
{
	if (argnum == 1) {
		Prompt_AddMatch(ctx, "info");
		Prompt_AddMatch(ctx, "play");
		Prompt_AddMatch(ctx, "stop");
		return;
	}

	if (argnum == 2 && !strcmp(Cmd_Argv(1), "play")) {
		// Autocomplete: track number
		for (int i = 2; i < MAX_NUM_OGGTRACKS; i++)
		{
			char ogg_path[MAX_OSPATH];
			get_track_path(ogg_path, sizeof(ogg_path), i);

			if(Sys_IsFile(ogg_path))
			{
				Prompt_AddMatch(ctx, va("%d", i));
			}
		}

		// Autocomplete: add track filenames
		char game_music[MAX_OSPATH];
		Q_snprintf(game_music, sizeof(game_music), "%s/%s/music", sys_basedir->string, *fs_game->string ? fs_game->string : BASEGAME);
		listfiles_t track_list;
		memset(&track_list, 0, sizeof(track_list));
		track_list.flags = FS_SEARCH_STRIPEXT;
		track_list.filter = ".ogg";
		Sys_ListFiles_r(&track_list, game_music, 0);

		for (int i = 0; i < track_list.count; i++) {
			Prompt_AddMatch(ctx, track_list.files[i]);
			Z_Free(track_list.files[i]);
		}
	}
}

static void
OGG_Cmd_f(void)
{
	const char *cmd = Cmd_Argv(1);

	if (!strcmp(cmd, "info"))
		OGG_Info_f();
	else if (!strcmp(cmd, "play"))
		OGG_Play_f();
	else if (!strcmp(cmd, "stop"))
		OGG_Stop();
	else if (!strcmp(cmd, "toggle"))
		OGG_TogglePlayback();
	else
		OGG_HelpMsg();
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
	Q_strlcpy(ogg_saved_state.path, ogg.path, sizeof(ogg_saved_state.path));
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

	Q_strlcpy(ogg.path, ogg_saved_state.path, sizeof(ogg.path));
	ogg_play();
	stb_vorbis_seek_frame(ogg.vf, ogg_saved_state.numsamples);
	ogg_numsamples = ogg_saved_state.numsamples;

	Cvar_SetValue(ogg_shuffle, shuffle_state, FROM_CODE);
}

// --------

static void ogg_enable_changed(cvar_t *self)
{
	if (cls.state < ca_precached || cls.state > ca_active)
		return;
	if ((ogg_enable->integer && ogg_status == PAUSE) || (!ogg_enable->integer && ogg_status == PLAY))
	{
		OGG_TogglePlayback();
	}
}

static void ogg_volume_changed(cvar_t *self)
{
    Cvar_ClampValue(self, 0, 1);
}

static const cmdreg_t c_ogg[] = {
    { "ogg", OGG_Cmd_f, OGG_Cmd_c },
    { NULL }
};

/*
 * Initialize the Ogg Vorbis subsystem.
 */
void
OGG_Init(void)
{
	// Cvars
	ogg_enable = Cvar_Get("ogg_enable", "1", CVAR_ARCHIVE);
	ogg_enable->changed = ogg_enable_changed;
	ogg_volume = Cvar_Get("ogg_volume", "1.0", CVAR_ARCHIVE);
	ogg_volume->changed = ogg_volume_changed;
	ogg_shuffle = Cvar_Get("ogg_shuffle", "0", CVAR_ARCHIVE);
	ogg_ignoretrack0 = Cvar_Get("ogg_ignoretrack0", "0", CVAR_ARCHIVE);

	// Commands
	Cmd_Register(c_ogg);

	// Global variables
	trackindex = -1;
	ogg_numsamples = 0;
	ogg_status = STOP;

	OGG_LoadTrackList();
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

	Z_Freep((void**)&ogg.music_dir);

	// Remove console commands
	Cmd_RemoveCommand("ogg");
}
