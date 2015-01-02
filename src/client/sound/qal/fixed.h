/*
Copyright (C) 2013 Andrey Nazarov

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

#ifdef __APPLE__
#include <OpenAL/al.h>
#else
#include <AL/al.h>
#endif

#ifndef AL_SOFT_loop_points
#define AL_SOFT_loop_points 1
#define AL_LOOP_POINTS_SOFT 0x2015
#endif

#define qalEnable alEnable
#define qalDisable alDisable
#define qalIsEnabled alIsEnabled
#define qalGetString alGetString
#define qalGetBooleanv alGetBooleanv
#define qalGetIntegerv alGetIntegerv
#define qalGetFloatv alGetFloatv
#define qalGetDoublev alGetDoublev
#define qalGetBoolean alGetBoolean
#define qalGetInteger alGetInteger
#define qalGetFloat alGetFloat
#define qalGetDouble alGetDouble
#define qalGetError alGetError
#define qalIsExtensionPresent alIsExtensionPresent
#define qalGetProcAddress alGetProcAddress
#define qalGetEnumValue alGetEnumValue
#define qalListenerf alListenerf
#define qalListener3f alListener3f
#define qalListenerfv alListenerfv
#define qalListeneri alListeneri
#define qalListener3i alListener3i
#define qalListeneriv alListeneriv
#define qalGetListenerf alGetListenerf
#define qalGetListener3f alGetListener3f
#define qalGetListenerfv alGetListenerfv
#define qalGetListeneri alGetListeneri
#define qalGetListener3i alGetListener3i
#define qalGetListeneriv alGetListeneriv
#define qalGenSources alGenSources
#define qalDeleteSources alDeleteSources
#define qalIsSource alIsSource
#define qalSourcef alSourcef
#define qalSource3f alSource3f
#define qalSourcefv alSourcefv
#define qalSourcei alSourcei
#define qalSource3i alSource3i
#define qalSourceiv alSourceiv
#define qalGetSourcef alGetSourcef
#define qalGetSource3f alGetSource3f
#define qalGetSourcefv alGetSourcefv
#define qalGetSourcei alGetSourcei
#define qalGetSource3i alGetSource3i
#define qalGetSourceiv alGetSourceiv
#define qalSourcePlayv alSourcePlayv
#define qalSourceStopv alSourceStopv
#define qalSourceRewindv alSourceRewindv
#define qalSourcePausev alSourcePausev
#define qalSourcePlay alSourcePlay
#define qalSourceStop alSourceStop
#define qalSourceRewind alSourceRewind
#define qalSourcePause alSourcePause
#define qalSourceQueueBuffers alSourceQueueBuffers
#define qalSourceUnqueueBuffers alSourceUnqueueBuffers
#define qalGenBuffers alGenBuffers
#define qalDeleteBuffers alDeleteBuffers
#define qalIsBuffer alIsBuffer
#define qalBufferData alBufferData
#define qalBufferf alBufferf
#define qalBuffer3f alBuffer3f
#define qalBufferfv alBufferfv
#define qalBufferi alBufferi
#define qalBuffer3i alBuffer3i
#define qalBufferiv alBufferiv
#define qalGetBufferf alGetBufferf
#define qalGetBuffer3f alGetBuffer3f
#define qalGetBufferfv alGetBufferfv
#define qalGetBufferi alGetBufferi
#define qalGetBuffer3i alGetBuffer3i
#define qalGetBufferiv alGetBufferiv
#define qalDopplerFactor alDopplerFactor
#define qalDopplerVelocity alDopplerVelocity
#define qalSpeedOfSound alSpeedOfSound
#define qalDistanceModel alDistanceModel

qboolean QAL_Init(void);
void QAL_Shutdown(void);

