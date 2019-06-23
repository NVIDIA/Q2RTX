/*
Copyright (C) 2010 Andrey Nazarov

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

#include "sound.h"
#include "AL/efx-presets.h"

#if USE_FIXED_LIBAL
#include "qal/fixed.h"
#else
#include "qal/dynamic.h"
#endif

#define AL_METER_OF_Q2_UNIT 0.0315f

// translates from AL coordinate system to quake
#define AL_UnpackVector(v)  -v[1],v[2],-v[0]
#define AL_CopyVector(a,b)  ((b)[0]=-(a)[1],(b)[1]=(a)[2],(b)[2]=-(a)[0])

// OpenAL implementation should support at least this number of sources
#define MIN_CHANNELS 16

int active_buffers = 0;
qboolean streamPlaying = qfalse;
static ALuint s_srcnums[MAX_CHANNELS];
static ALuint streamSource = 0;
static int s_framecount;
static ALuint underwaterFilter;
static ALuint ReverbEffect;
static ALuint ReverbEffectSlot;

void AL_SoundInfo(void)
{
	Com_Printf("===============\n");
	Com_Printf("SOUND INFO:\n");
    Com_Printf("AL_VENDOR: %s\n", qalGetString(AL_VENDOR));
	Com_Printf("\n");
    Com_Printf("AL_RENDERER: %s\n", qalGetString(AL_RENDERER));
	Com_Printf("\n");
    Com_Printf("AL_VERSION: %s\n", qalGetString(AL_VERSION));
	Com_Printf("\n");
    Com_Printf("AL_EXTENSIONS:\n%s\n", qalGetString(AL_EXTENSIONS));
	Com_Printf("\n");
	QALC_PrintExtensions();
	Com_Printf("\n");
    Com_Printf("Number of sources: %d\n", s_numchannels);
	Com_Printf("===============\n");
}

/*
* Set up the stream sources
*/
static void
AL_InitStreamSource()
{
	qalSource3f(streamSource, AL_POSITION, 0.0, 0.0, 0.0);
	qalSource3f(streamSource, AL_VELOCITY, 0.0, 0.0, 0.0);
	qalSource3f(streamSource, AL_DIRECTION, 0.0, 0.0, 0.0);
	qalSourcef(streamSource, AL_ROLLOFF_FACTOR, 0.0);
	qalSourcei(streamSource, AL_BUFFER, 0);
	qalSourcei(streamSource, AL_LOOPING, AL_FALSE);
	qalSourcei(streamSource, AL_SOURCE_RELATIVE, AL_TRUE);
}

/*
* Silence / stop all OpenAL streams
*/
static void
AL_StreamDie(void)
{
	int numBuffers;

	streamPlaying = qfalse;
	qalSourceStop(streamSource);

	/* Un-queue any buffers, and delete them */
	qalGetSourcei(streamSource, AL_BUFFERS_QUEUED, &numBuffers);

	while (numBuffers--)
	{
		ALuint buffer;
		qalSourceUnqueueBuffers(streamSource, 1, &buffer);
		qalDeleteBuffers(1, &buffer);
		active_buffers--;
	}
}

/*
* Updates stream sources by removing all played
* buffers and restarting playback if necessary.
*/
static void
AL_StreamUpdate(void)
{
	int numBuffers;
	ALint state;

	qalGetSourcei(streamSource, AL_SOURCE_STATE, &state);

	if (state == AL_STOPPED)
	{
		streamPlaying = qfalse;
	}
	else
	{
		/* Un-queue any already played buffers and delete them */
		qalGetSourcei(streamSource, AL_BUFFERS_PROCESSED, &numBuffers);

		while (numBuffers--)
		{
			ALuint buffer;
			qalSourceUnqueueBuffers(streamSource, 1, &buffer);
			qalDeleteBuffers(1, &buffer);
			active_buffers--;
		}
	}

	/* Start the streamSource playing if necessary */
	qalGetSourcei(streamSource, AL_BUFFERS_QUEUED, &numBuffers);

	if (!streamPlaying && numBuffers)
	{
		qalSourcePlay(streamSource);
		streamPlaying = qtrue;
	}
}

EFXEAXREVERBPROPERTIES ReverbPresets[113] = {
	EFX_REVERB_PRESET_GENERIC,
	EFX_REVERB_PRESET_PADDEDCELL,
	EFX_REVERB_PRESET_ROOM,
	EFX_REVERB_PRESET_BATHROOM,
	EFX_REVERB_PRESET_LIVINGROOM,
	EFX_REVERB_PRESET_STONEROOM,
	EFX_REVERB_PRESET_AUDITORIUM,
	EFX_REVERB_PRESET_CONCERTHALL,
	EFX_REVERB_PRESET_CAVE,
	EFX_REVERB_PRESET_ARENA,
	EFX_REVERB_PRESET_HANGAR,
	EFX_REVERB_PRESET_CARPETEDHALLWAY,
	EFX_REVERB_PRESET_HALLWAY,
	EFX_REVERB_PRESET_STONECORRIDOR,
	EFX_REVERB_PRESET_ALLEY,
	EFX_REVERB_PRESET_FOREST,
	EFX_REVERB_PRESET_CITY,
	EFX_REVERB_PRESET_MOUNTAINS,
	EFX_REVERB_PRESET_QUARRY,
	EFX_REVERB_PRESET_PLAIN,
	EFX_REVERB_PRESET_PARKINGLOT,
	EFX_REVERB_PRESET_SEWERPIPE,
	EFX_REVERB_PRESET_UNDERWATER,
	EFX_REVERB_PRESET_DRUGGED,
	EFX_REVERB_PRESET_DIZZY,
	EFX_REVERB_PRESET_PSYCHOTIC,
	EFX_REVERB_PRESET_CASTLE_SMALLROOM,
	EFX_REVERB_PRESET_CASTLE_SHORTPASSAGE,
	EFX_REVERB_PRESET_CASTLE_MEDIUMROOM,
	EFX_REVERB_PRESET_CASTLE_LARGEROOM,
	EFX_REVERB_PRESET_CASTLE_LONGPASSAGE,
	EFX_REVERB_PRESET_CASTLE_HALL,
	EFX_REVERB_PRESET_CASTLE_CUPBOARD,
	EFX_REVERB_PRESET_CASTLE_COURTYARD,
	EFX_REVERB_PRESET_CASTLE_ALCOVE,
	EFX_REVERB_PRESET_FACTORY_SMALLROOM,
	EFX_REVERB_PRESET_FACTORY_SHORTPASSAGE,
	EFX_REVERB_PRESET_FACTORY_MEDIUMROOM,
	EFX_REVERB_PRESET_FACTORY_LARGEROOM,
	EFX_REVERB_PRESET_FACTORY_LONGPASSAGE,
	EFX_REVERB_PRESET_FACTORY_HALL,
	EFX_REVERB_PRESET_FACTORY_CUPBOARD,
	EFX_REVERB_PRESET_FACTORY_COURTYARD,
	EFX_REVERB_PRESET_FACTORY_ALCOVE,
	EFX_REVERB_PRESET_ICEPALACE_SMALLROOM,
	EFX_REVERB_PRESET_ICEPALACE_SHORTPASSAGE,
	EFX_REVERB_PRESET_ICEPALACE_MEDIUMROOM,
	EFX_REVERB_PRESET_ICEPALACE_LARGEROOM,
	EFX_REVERB_PRESET_ICEPALACE_LONGPASSAGE,
	EFX_REVERB_PRESET_ICEPALACE_HALL,
	EFX_REVERB_PRESET_ICEPALACE_CUPBOARD,
	EFX_REVERB_PRESET_ICEPALACE_COURTYARD,
	EFX_REVERB_PRESET_ICEPALACE_ALCOVE,
	EFX_REVERB_PRESET_SPACESTATION_SMALLROOM,
	EFX_REVERB_PRESET_SPACESTATION_SHORTPASSAGE,
	EFX_REVERB_PRESET_SPACESTATION_MEDIUMROOM,
	EFX_REVERB_PRESET_SPACESTATION_LARGEROOM,
	EFX_REVERB_PRESET_SPACESTATION_LONGPASSAGE,
	EFX_REVERB_PRESET_SPACESTATION_HALL,
	EFX_REVERB_PRESET_SPACESTATION_CUPBOARD,
	EFX_REVERB_PRESET_SPACESTATION_ALCOVE,
	EFX_REVERB_PRESET_WOODEN_SMALLROOM,
	EFX_REVERB_PRESET_WOODEN_SHORTPASSAGE,
	EFX_REVERB_PRESET_WOODEN_MEDIUMROOM,
	EFX_REVERB_PRESET_WOODEN_LARGEROOM,
	EFX_REVERB_PRESET_WOODEN_LONGPASSAGE,
	EFX_REVERB_PRESET_WOODEN_HALL,
	EFX_REVERB_PRESET_WOODEN_CUPBOARD,
	EFX_REVERB_PRESET_WOODEN_COURTYARD,
	EFX_REVERB_PRESET_WOODEN_ALCOVE,
	EFX_REVERB_PRESET_SPORT_EMPTYSTADIUM,
	EFX_REVERB_PRESET_SPORT_SQUASHCOURT,
	EFX_REVERB_PRESET_SPORT_SMALLSWIMMINGPOOL,
	EFX_REVERB_PRESET_SPORT_LARGESWIMMINGPOOL,
	EFX_REVERB_PRESET_SPORT_GYMNASIUM,
	EFX_REVERB_PRESET_SPORT_FULLSTADIUM,
	EFX_REVERB_PRESET_SPORT_STADIUMTANNOY,
	EFX_REVERB_PRESET_PREFAB_WORKSHOP,
	EFX_REVERB_PRESET_PREFAB_SCHOOLROOM,
	EFX_REVERB_PRESET_PREFAB_PRACTISEROOM,
	EFX_REVERB_PRESET_PREFAB_OUTHOUSE,
	EFX_REVERB_PRESET_PREFAB_CARAVAN,
	EFX_REVERB_PRESET_DOME_TOMB,
	EFX_REVERB_PRESET_PIPE_SMALL,
	EFX_REVERB_PRESET_DOME_SAINTPAULS,
	EFX_REVERB_PRESET_PIPE_LONGTHIN,
	EFX_REVERB_PRESET_PIPE_LARGE,
	EFX_REVERB_PRESET_PIPE_RESONANT,
	EFX_REVERB_PRESET_OUTDOORS_BACKYARD,
	EFX_REVERB_PRESET_OUTDOORS_ROLLINGPLAINS,
	EFX_REVERB_PRESET_OUTDOORS_DEEPCANYON,
	EFX_REVERB_PRESET_OUTDOORS_CREEK,
	EFX_REVERB_PRESET_OUTDOORS_VALLEY,
	EFX_REVERB_PRESET_MOOD_HEAVEN,
	EFX_REVERB_PRESET_MOOD_HELL,
	EFX_REVERB_PRESET_MOOD_MEMORY,
	EFX_REVERB_PRESET_DRIVING_COMMENTATOR,
	EFX_REVERB_PRESET_DRIVING_PITGARAGE,
	EFX_REVERB_PRESET_DRIVING_INCAR_RACER,
	EFX_REVERB_PRESET_DRIVING_INCAR_SPORTS,
	EFX_REVERB_PRESET_DRIVING_INCAR_LUXURY,
	EFX_REVERB_PRESET_DRIVING_FULLGRANDSTAND,
	EFX_REVERB_PRESET_DRIVING_EMPTYGRANDSTAND,
	EFX_REVERB_PRESET_DRIVING_TUNNEL,
	EFX_REVERB_PRESET_CITY_STREETS,
	EFX_REVERB_PRESET_CITY_SUBWAY,
	EFX_REVERB_PRESET_CITY_MUSEUM,
	EFX_REVERB_PRESET_CITY_LIBRARY,
	EFX_REVERB_PRESET_CITY_UNDERPASS,
	EFX_REVERB_PRESET_CITY_ABANDONED,
	EFX_REVERB_PRESET_DUSTYROOM,
	EFX_REVERB_PRESET_CHAPEL,
	EFX_REVERB_PRESET_SMALLWATERROOM
};

char ReverbPresetsNames[113][31] = {
	"GENERIC",
	"PADDEDCELL",
	"ROOM",
	"BATHROOM",
	"LIVINGROOM",
	"STONEROOM",
	"AUDITORIUM",
	"CONCERTHALL",
	"CAVE",
	"ARENA",
	"HANGAR",
	"CARPETEDHALLWAY",
	"HALLWAY",
	"STONECORRIDOR",
	"ALLEY",
	"FOREST",
	"CITY",
	"MOUNTAINS",
	"QUARRY",
	"PLAIN",
	"PARKINGLOT",
	"SEWERPIPE",
	"UNDERWATER",
	"DRUGGED",
	"DIZZY",
	"PSYCHOTIC",
	"CASTLE_SMALLROOM",
	"CASTLE_SHORTPASSAGE",
	"CASTLE_MEDIUMROOM",
	"CASTLE_LARGEROOM",
	"CASTLE_LONGPASSAGE",
	"CASTLE_HALL",
	"CASTLE_CUPBOARD",
	"CASTLE_COURTYARD",
	"CASTLE_ALCOVE",
	"FACTORY_SMALLROOM",
	"FACTORY_SHORTPASSAGE",
	"FACTORY_MEDIUMROOM",
	"FACTORY_LARGEROOM",
	"FACTORY_LONGPASSAGE",
	"FACTORY_HALL",
	"FACTORY_CUPBOARD",
	"FACTORY_COURTYARD",
	"FACTORY_ALCOVE",
	"ICEPALACE_SMALLROOM",
	"ICEPALACE_SHORTPASSAGE",
	"ICEPALACE_MEDIUMROOM",
	"ICEPALACE_LARGEROOM",
	"ICEPALACE_LONGPASSAGE",
	"ICEPALACE_HALL",
	"ICEPALACE_CUPBOARD",
	"ICEPALACE_COURTYARD",
	"ICEPALACE_ALCOVE",
	"SPACESTATION_SMALLROOM",
	"SPACESTATION_SHORTPASSAGE",
	"SPACESTATION_MEDIUMROOM",
	"SPACESTATION_LARGEROOM",
	"SPACESTATION_LONGPASSAGE",
	"SPACESTATION_HALL",
	"SPACESTATION_CUPBOARD",
	"SPACESTATION_ALCOVE",
	"WOODEN_SMALLROOM",
	"WOODEN_SHORTPASSAGE",
	"WOODEN_MEDIUMROOM",
	"WOODEN_LARGEROOM",
	"WOODEN_LONGPASSAGE",
	"WOODEN_HALL",
	"WOODEN_CUPBOARD",
	"WOODEN_COURTYARD",
	"WOODEN_ALCOVE",
	"SPORT_EMPTYSTADIUM",
	"SPORT_SQUASHCOURT",
	"SPORT_SMALLSWIMMINGPOOL",
	"SPORT_LARGESWIMMINGPOOL",
	"SPORT_GYMNASIUM",
	"SPORT_FULLSTADIUM",
	"SPORT_STADIUMTANNOY",
	"PREFAB_WORKSHOP",
	"PREFAB_SCHOOLROOM",
	"PREFAB_PRACTISEROOM",
	"PREFAB_OUTHOUSE",
	"PREFAB_CARAVAN",
	"DOME_TOMB",
	"PIPE_SMALL",
	"DOME_SAINTPAULS",
	"PIPE_LONGTHIN",
	"PIPE_LARGE",
	"PIPE_RESONANT",
	"OUTDOORS_BACKYARD",
	"OUTDOORS_ROLLINGPLAINS",
	"OUTDOORS_DEEPCANYON",
	"OUTDOORS_CREEK",
	"OUTDOORS_VALLEY",
	"MOOD_HEAVEN",
	"MOOD_HELL",
	"MOOD_MEMORY",
	"DRIVING_COMMENTATOR",
	"DRIVING_PITGARAGE",
	"DRIVING_INCAR_RACER",
	"DRIVING_INCAR_SPORTS",
	"DRIVING_INCAR_LUXURY",
	"DRIVING_FULLGRANDSTAND",
	"DRIVING_EMPTYGRANDSTAND",
	"DRIVING_TUNNEL",
	"CITY_STREETS",
	"CITY_SUBWAY",
	"CITY_MUSEUM",
	"CITY_LIBRARY",
	"CITY_UNDERPASS",
	"CITY_ABANDONED",
	"DUSTYROOM",
	"CHAPEL",
	"SMALLWATERROOM"
};

void SetReverb(int index, int concalled)
{
	EFXEAXREVERBPROPERTIES reverb = ReverbPresets[index];

	if(concalled)
		Com_Printf("Reverb set to: %s\n", ReverbPresetsNames[index]);

	qalEffectf(ReverbEffect, AL_REVERB_DENSITY, reverb.flDensity);
	qalEffectf(ReverbEffect, AL_REVERB_DIFFUSION, reverb.flDiffusion);
	qalEffectf(ReverbEffect, AL_REVERB_GAIN, reverb.flGain);
	qalEffectf(ReverbEffect, AL_REVERB_GAINHF, reverb.flGainHF);
	qalEffectf(ReverbEffect, AL_REVERB_DECAY_TIME, reverb.flDecayTime);
	qalEffectf(ReverbEffect, AL_REVERB_DECAY_HFRATIO, reverb.flDecayHFRatio);
	qalEffectf(ReverbEffect, AL_REVERB_REFLECTIONS_GAIN, reverb.flReflectionsGain);
	qalEffectf(ReverbEffect, AL_REVERB_REFLECTIONS_DELAY, reverb.flReflectionsDelay);
	qalEffectf(ReverbEffect, AL_REVERB_LATE_REVERB_GAIN, reverb.flLateReverbGain);
	qalEffectf(ReverbEffect, AL_REVERB_LATE_REVERB_DELAY, reverb.flLateReverbDelay);
	qalEffectf(ReverbEffect, AL_REVERB_AIR_ABSORPTION_GAINHF, reverb.flAirAbsorptionGainHF);
	qalEffectf(ReverbEffect, AL_REVERB_ROOM_ROLLOFF_FACTOR, reverb.flRoomRolloffFactor);
	qalEffecti(ReverbEffect, AL_REVERB_DECAY_HFLIMIT, reverb.iDecayHFLimit);

	qalAuxiliaryEffectSloti(ReverbEffectSlot, AL_EFFECTSLOT_EFFECT, ReverbEffect);
}

void AL_InitReverbEffect(void)
{
	if (!(qalGenEffects && qalEffecti && qalEffectf && qalDeleteEffects &&qalGenAuxiliaryEffectSlots && qalAuxiliaryEffectSloti))
		return;

	ReverbEffect = 0;
	qalGenEffects(1, &ReverbEffect);

	if (qalGetError() != AL_NO_ERROR)
	{
		Com_Printf("Couldn't generate an OpenAL effect!\n");
		return;
	}

	ReverbEffectSlot = 0;
	qalGenAuxiliaryEffectSlots(1, &ReverbEffectSlot);
	qalEffecti(ReverbEffect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
	SetReverb(s_reverb_preset->integer, 0);
}

void UpdateReverb(void)
{
	static vec3_t mins = { 0, 0, 0 }, maxs = { 0, 0, 0 };
	vec3_t forward = { 1000000, 0, 0 };
	vec3_t backward = { -1000000, 0, 0 };
	vec3_t left = { 0, 1000000, 0 };
	vec3_t right = { 0, -1000000, 0 };
	vec3_t up = { 0, 0, 1000000 };
	trace_t trace1, trace2, trace3, trace4, trace5;
	vec3_t length1, length2, length3, length4, length5;
	float dist1, dist2, dist3, dist4, dist5, average;

	if (ReverbEffect == 0)
		return;

	CM_BoxTrace(&trace1, listener_origin, up, mins, maxs, cl.bsp->nodes, MASK_DEADSOLID);
	CM_BoxTrace(&trace2, listener_origin, forward, mins, maxs, cl.bsp->nodes, MASK_DEADSOLID);
	CM_BoxTrace(&trace3, listener_origin, backward, mins, maxs, cl.bsp->nodes, MASK_DEADSOLID);
	CM_BoxTrace(&trace4, listener_origin, left, mins, maxs, cl.bsp->nodes, MASK_DEADSOLID);
	CM_BoxTrace(&trace5, listener_origin, right, mins, maxs, cl.bsp->nodes, MASK_DEADSOLID);

	VectorSubtract(trace1.endpos, listener_origin, length1);
	VectorSubtract(trace2.endpos, listener_origin, length2);
	VectorSubtract(trace3.endpos, listener_origin, length3);
	VectorSubtract(trace4.endpos, listener_origin, length4);
	VectorSubtract(trace5.endpos, listener_origin, length5);

	dist1 = VectorLength(length1);
	dist2 = VectorLength(length2);
	dist3 = VectorLength(length3);
	dist4 = VectorLength(length4);
	dist5 = VectorLength(length5);

	average = (dist1 + dist2 + dist3 + dist4 + dist5) / 5;

	if (average < 100)
		SetReverb(41, 0);

	if (average > 100 && average < 200)
		SetReverb(26, 0);

	if (average > 200 && average < 330)
		SetReverb(5, 0);

	if (average > 330 && average < 450)
		SetReverb(12, 0);

	if (average > 450 && average < 650)
		SetReverb(18, 0);

	if (average > 650)
		SetReverb(17, 0);
}

qboolean AL_Init(void)
{
    int i;

    Com_DPrintf("Initializing OpenAL\n");


    if (!QAL_Init()) {
        goto fail0;
    }

    // check for linear distance extension
    if (!qalIsExtensionPresent("AL_EXT_LINEAR_DISTANCE")) {
        Com_SetLastError("AL_EXT_LINEAR_DISTANCE extension is missing");
        goto fail1;
    }

	/* generate source names */
	qalGetError();
	qalGenSources(1, &streamSource);

	if (qalGetError() != AL_NO_ERROR)
	{
		Com_Printf("ERROR: Couldn't get a single Source.\n");
		QAL_Shutdown();
		return qfalse;
	}
	else
	{
		for (i = 0; i < MAX_CHANNELS; i++) {
			qalGenSources(1, &s_srcnums[i]);
			if (qalGetError() != AL_NO_ERROR) {
				break;
			}
		}
	}

    Com_DPrintf("Got %d AL sources\n", i);

    if (i < MIN_CHANNELS) {
        Com_SetLastError("Insufficient number of AL sources");
        goto fail1;
    }

    s_numchannels = i;
	AL_InitStreamSource();
	AL_InitUnderwaterFilter();
	AL_InitReverbEffect();

	// exaggerate 2x because realistic is barely noticeable
	if (s_doppler->value) {
		qalDopplerFactor(2.0f);
	}

	if (strstr(qalGetString(AL_RENDERER), "OpenAL Soft"))
		Com_Printf("OpenAL Soft detected.\n");

    Com_Printf("OpenAL initialized.\n");
    return qtrue;

fail1:
    QAL_Shutdown();
fail0:
    Com_EPrintf("Failed to initialize OpenAL: %s\n", Com_GetLastError());
    return qfalse;
}

void AL_Shutdown(void)
{
    Com_Printf("Shutting down OpenAL.\n");

	AL_StopAllChannels();

	qalDeleteSources(1, &streamSource);
	qalDeleteFilters(1, &underwaterFilter);
	qalDeleteEffects(1, &ReverbEffect);

    if (s_numchannels) {
        // delete source names
        qalDeleteSources(s_numchannels, s_srcnums);
        memset(s_srcnums, 0, sizeof(s_srcnums));
        s_numchannels = 0;
    }

    QAL_Shutdown();
}

sfxcache_t *AL_UploadSfx(sfx_t *s)
{
    sfxcache_t *sc;
    ALsizei size = s_info.samples * s_info.width;
    ALenum format = s_info.width == 2 ? AL_FORMAT_MONO16 : AL_FORMAT_MONO8;
    ALuint name;

    if (!size) {
        s->error = Q_ERR_TOO_FEW;
        return NULL;
    }

    qalGetError();
    qalGenBuffers(1, &name);
    qalBufferData(name, format, s_info.data, size, s_info.rate);
    if (qalGetError() != AL_NO_ERROR) {
        s->error = Q_ERR_LIBRARY_ERROR;
        return NULL;
    }

#if 0
    // specify OpenAL-Soft style loop points
    if (s_info.loopstart > 0 && qalIsExtensionPresent("AL_SOFT_loop_points")) {
        ALint points[2] = { s_info.loopstart, s_info.samples };
        qalBufferiv(name, AL_LOOP_POINTS_SOFT, points);
    }
#endif

    // allocate placeholder sfxcache
    sc = s->cache = S_Malloc(sizeof(*sc));
    sc->length = s_info.samples * 1000 / s_info.rate; // in msec
    sc->loopstart = s_info.loopstart;
    sc->width = s_info.width;
    sc->size = size;
    sc->bufnum = name;

    return sc;
}

void AL_DeleteSfx(sfx_t *s)
{
    sfxcache_t *sc;
    ALuint name;

    sc = s->cache;
    if (!sc) {
        return;
    }

    name = sc->bufnum;
    qalDeleteBuffers(1, &name);
}

static void AL_Spatialize(channel_t *ch)
{
	vec3_t      origin;
	vec3_t velocity;
	static vec3_t mins = { 0, 0, 0 }, maxs = { 0, 0, 0 };
	trace_t trace;
	vec3_t distance;
	float dist;
	float final;
	qboolean sourceoccluded = qfalse;


	// anything coming from the view entity will always be full volume
	// no attenuation = no spatialization
	if (ch->entnum == -1 || ch->entnum == listener_entnum || !ch->dist_mult) {
		VectorCopy(listener_origin, origin);
	}
	else if (ch->fixed_origin) {
		VectorCopy(ch->origin, origin);
	}
	else {
		CL_GetEntitySoundOrigin(ch->entnum, origin);
	}

	if (s_doppler->value) {
		CL_GetEntitySoundVelocity(ch->entnum, velocity);
		VectorScale(velocity, AL_METER_OF_Q2_UNIT, velocity);
		qalSource3f(ch->srcnum, AL_VELOCITY, AL_UnpackVector(velocity));
	}

	if (cl.bsp && s_occlusion->integer && underwaterFilter != 0)
	{
		CM_BoxTrace(&trace, origin, listener_origin, mins, maxs, cl.bsp->nodes, MASK_PLAYERSOLID);
		if (trace.fraction < 1.0 && !(ch->entnum == -1 || ch->entnum == listener_entnum || !ch->dist_mult))
		{
			VectorSubtract(origin, listener_origin, distance);
			dist = VectorLength(distance);

			final = 1.0 - ((dist / 1000) * s_occlusion_strength->value);

			qalSourcef(ch->srcnum, AL_GAIN, clamp(final, 0, 1));

			VectorCopy(trace.endpos, origin);

			if (!snd_is_underwater)
				qalSourcei(ch->srcnum, AL_DIRECT_FILTER, underwaterFilter);

			sourceoccluded = qtrue;
		}
		else
		{
			if (!snd_is_underwater)
				qalSourcei(ch->srcnum, AL_DIRECT_FILTER, 0) ;
		}
	}

	if(cl.bsp && !snd_is_underwater && s_reverb_preset_autopick->integer && s_reverb->integer)
		UpdateReverb();

	if(s_reverb->integer && cl.bsp && ReverbEffect != 0 && sourceoccluded == qfalse)
		qalSource3i(ch->srcnum, AL_AUXILIARY_SEND_FILTER, ReverbEffectSlot, 0, AL_FILTER_NULL);
	else
		qalSource3i(ch->srcnum, AL_AUXILIARY_SEND_FILTER, 0, 0, AL_FILTER_NULL);

    qalSource3f(ch->srcnum, AL_POSITION, AL_UnpackVector(origin));
}

void AL_StopChannel(channel_t *ch)
{
#ifdef _DEBUG
    if (s_show->integer > 1)
        Com_Printf("%s: %s\n", __func__, ch->sfx->name);
#endif

    // stop it
    qalSourceStop(ch->srcnum);
    qalSourcei(ch->srcnum, AL_BUFFER, AL_NONE);
    memset(ch, 0, sizeof(*ch));
}

void AL_PlayChannel(channel_t *ch)
{
    sfxcache_t *sc = ch->sfx->cache;

#ifdef _DEBUG
    if (s_show->integer > 1)
        Com_Printf("%s: %s\n", __func__, ch->sfx->name);
#endif

    ch->srcnum = s_srcnums[ch - channels];
    qalGetError();
    qalSourcei(ch->srcnum, AL_BUFFER, sc->bufnum);
    if (ch->autosound /*|| sc->loopstart >= 0*/) {
        qalSourcei(ch->srcnum, AL_LOOPING, AL_TRUE);
    } else {
        qalSourcei(ch->srcnum, AL_LOOPING, AL_FALSE);
    }
    qalSourcef(ch->srcnum, AL_GAIN, ch->master_vol);
    qalSourcef(ch->srcnum, AL_REFERENCE_DISTANCE, SOUND_FULLVOLUME);
    qalSourcef(ch->srcnum, AL_MAX_DISTANCE, 8192);
    qalSourcef(ch->srcnum, AL_ROLLOFF_FACTOR, ch->dist_mult * (8192 - SOUND_FULLVOLUME));

    AL_Spatialize(ch);

    // play it
    qalSourcePlay(ch->srcnum);
    if (qalGetError() != AL_NO_ERROR) {
        AL_StopChannel(ch);
    }
}

static void AL_IssuePlaysounds(void)
{
    playsound_t *ps;

    // start any playsounds
    while (1) {
        ps = s_pendingplays.next;
        if (ps == &s_pendingplays)
            break;  // no more pending sounds
        if (ps->begin > paintedtime)
            break;
        S_IssuePlaysound(ps);
    }
}

void AL_StopAllChannels(void)
{
    int         i;
    channel_t   *ch;

    ch = channels;
    for (i = 0; i < s_numchannels; i++, ch++) {
        if (!ch->sfx)
            continue;
        AL_StopChannel(ch);
    }
}

static channel_t *AL_FindLoopingSound(int entnum, sfx_t *sfx)
{
    int         i;
    channel_t   *ch;

    ch = channels;
    for (i = 0; i < s_numchannels; i++, ch++) {
        if (!ch->sfx)
            continue;
        if (!ch->autosound)
            continue;
        if (entnum && ch->entnum != entnum)
            continue;
        if (ch->sfx != sfx)
            continue;
        return ch;
    }

    return NULL;
}

static void AL_AddLoopSounds(void)
{
    int         i;
    int         sounds[MAX_EDICTS];
    channel_t   *ch, *ch2;
    sfx_t       *sfx;
    sfxcache_t  *sc;
    int         num;
    entity_state_t  *ent;

    if (cls.state != ca_active || sv_paused->integer || !s_ambient->integer) {
        return;
    }

    S_BuildSoundList(sounds);

    for (i = 0; i < cl.frame.numEntities; i++) {
        if (!sounds[i])
            continue;

        sfx = S_SfxForHandle(cl.sound_precache[sounds[i]]);
        if (!sfx)
            continue;       // bad sound effect
        sc = sfx->cache;
        if (!sc)
            continue;

        num = (cl.frame.firstEntity + i) & PARSE_ENTITIES_MASK;
        ent = &cl.entityStates[num];

        ch = AL_FindLoopingSound(ent->number, sfx);
        if (ch) {
            ch->autoframe = s_framecount;
            ch->end = paintedtime + sc->length;
            continue;
        }

        // allocate a channel
        ch = S_PickChannel(0, 0);
        if (!ch)
            continue;

        ch2 = AL_FindLoopingSound(0, sfx);

        ch->autosound = qtrue;  // remove next frame
        ch->autoframe = s_framecount;
        ch->sfx = sfx;
        ch->entnum = ent->number;
        ch->master_vol = 1;
        ch->dist_mult = SOUND_LOOPATTENUATE;
        ch->end = paintedtime + sc->length;

        AL_PlayChannel(ch);

        // attempt to synchronize with existing sounds of the same type
        if (ch2) {
            ALint offset;

            qalGetSourcei(ch2->srcnum, AL_SAMPLE_OFFSET, &offset);
            qalSourcei(ch->srcnum, AL_SAMPLE_OFFSET, offset);
        }
    }
}

void oal_update_underwater()
{
	int i;
	float gain_hf;
	qboolean update = qfalse;
	ALuint filter;

	if (underwaterFilter == 0)
		return;

	if (s_underwater->modified) {
		update = qtrue;
		s_underwater->modified = qfalse;
		snd_is_underwater_enabled = ((int)s_underwater->value != 0);
	}

	if (s_underwater_gain_hf->modified) {
		update = qtrue;
		s_underwater_gain_hf->modified = qfalse;
	}

	if (!update)
		return;

	gain_hf = s_underwater_gain_hf->value;

	if (gain_hf < AL_LOWPASS_MIN_GAINHF)
		gain_hf = AL_LOWPASS_MIN_GAINHF;

	if (gain_hf > AL_LOWPASS_MAX_GAINHF)
		gain_hf = AL_LOWPASS_MAX_GAINHF;

	qalFilterf(underwaterFilter, AL_LOWPASS_GAINHF, gain_hf);

	if (snd_is_underwater_enabled && snd_is_underwater)
		filter = underwaterFilter;
	else
		filter = 0;

	for (i = 0; i < s_numchannels; ++i)
		qalSourcei(s_srcnums[i], AL_DIRECT_FILTER, filter);
}

AL_InitUnderwaterFilter(void)
{
	underwaterFilter = 0;

	if (!(qalGenFilters && qalFilteri && qalFilterf && qalDeleteFilters))
		return;

	/* Generate a filter */
	qalGenFilters(1, &underwaterFilter);

	if (qalGetError() != AL_NO_ERROR)
	{
		Com_Printf("Couldn't generate an OpenAL filter!\n");
		return;
	}

	/* Low pass filter for underwater effect */
	qalFilteri(underwaterFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);

	if (qalGetError() != AL_NO_ERROR)
	{
		Com_Printf("Low pass filter is not supported!\n");
		return;
	}

	qalFilterf(underwaterFilter, AL_LOWPASS_GAIN, AL_LOWPASS_DEFAULT_GAIN);

	s_underwater->modified = qtrue;
	s_underwater_gain_hf->modified = qtrue;
}

void AL_Underwater()
{
	int i;

	if (s_started != SS_OAL)
	{
		return;
	}

	if (underwaterFilter == 0)
		return;

	/* Apply to all sources */
	for (i = 0; i < s_numchannels; i++)
	{
		qalSourcei(s_srcnums[i], AL_DIRECT_FILTER, underwaterFilter);
		SetReverb(22, 0);
	}
}

/*
 * Disables the underwater effect
 */
void AL_Overwater()
{
	int i;

	if (s_started != SS_OAL)
	{
		return;
	}

	if (underwaterFilter == 0)
		return;

	/* Apply to all sources */
	for (i = 0; i < s_numchannels; i++)
	{
		qalSourcei(s_srcnums[i], AL_DIRECT_FILTER, 0);
		SetReverb(s_reverb_preset->integer, 0);
	}
}

void AL_Update(void)
{
    int         i;
    channel_t   *ch;
    vec_t       orientation[6];
	vec3_t listener_velocity;

    if (!s_active) {
        return;
    }

    paintedtime = cl.time;

    // set listener parameters
    qalListener3f(AL_POSITION, AL_UnpackVector(listener_origin));
    AL_CopyVector(listener_forward, orientation);
    AL_CopyVector(listener_up, orientation + 3);
    qalListenerfv(AL_ORIENTATION, orientation);
    qalListenerf(AL_GAIN, s_volume->value);
    qalDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);

	if (s_doppler->value) {
		CL_GetViewVelocity(listener_velocity);
		VectorScale(listener_velocity, AL_METER_OF_Q2_UNIT, listener_velocity);
		qalListener3f(AL_VELOCITY, AL_UnpackVector(listener_velocity));
	}

    // update spatialization for dynamic sounds
    ch = channels;
    for (i = 0; i < s_numchannels; i++, ch++) {
        if (!ch->sfx)
            continue;

        if (ch->autosound) {
            // autosounds are regenerated fresh each frame
            if (ch->autoframe != s_framecount) {
                AL_StopChannel(ch);
                continue;
            }
        } else {
            ALenum state;

            qalGetError();
            qalGetSourcei(ch->srcnum, AL_SOURCE_STATE, &state);
            if (qalGetError() != AL_NO_ERROR || state == AL_STOPPED) {
                AL_StopChannel(ch);
                continue;
            }
        }

#ifdef _DEBUG
        if (s_show->integer) {
            Com_Printf("%.1f %s\n", ch->master_vol, ch->sfx->name);
            //    total++;
        }
#endif

        AL_Spatialize(ch);         // respatialize channel
    }

    s_framecount++;

    // add loopsounds
    AL_AddLoopSounds();

	AL_StreamUpdate();
    AL_IssuePlaysounds();

	oal_update_underwater();
}

/*
* Queues raw samples for playback. Used
* by the background music an cinematics.
*/
void
AL_RawSamples(int samples, int rate, int width, int channels,
	byte *data, float volume)
{
	ALuint buffer;
	ALuint format = 0;

	/* Work out format */
	if (width == 1)
	{
		if (channels == 1)
		{
			format = AL_FORMAT_MONO8;
		}
		else if (channels == 2)
		{
			format = AL_FORMAT_STEREO8;
		}
	}
	else if (width == 2)
	{
		if (channels == 1)
		{
			format = AL_FORMAT_MONO16;
		}
		else if (channels == 2)
		{
			format = AL_FORMAT_STEREO16;
		}
	}

	/* Create a buffer, and stuff the data into it */
	qalGenBuffers(1, &buffer);
	qalBufferData(buffer, format, (ALvoid *)data,
		(samples * width * channels), rate);
	active_buffers++;

	/* set volume */
	if (volume > 1.0f)
	{
		volume = 1.0f;
	}

	qalSourcef(streamSource, AL_GAIN, volume);

	/* Shove the data onto the streamSource */
	qalSourceQueueBuffers(streamSource, 1, &buffer);

	/* emulate behavior of S_RawSamples for s_rawend */
	s_rawend += samples;
}

/*
* Kills all raw samples still in flight.
* This is used to stop music playback
* when silence is triggered.
*/
void
AL_UnqueueRawSamples()
{
	AL_StreamDie();
}
