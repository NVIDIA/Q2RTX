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

#define AL_NO_PROTOTYPES

#ifdef __APPLE__
#include <OpenAL/al.h>
#else
#include <AL/al.h>
#endif

#ifndef AL_SOFT_loop_points
#define AL_SOFT_loop_points 1
#define AL_LOOP_POINTS_SOFT 0x2015
#endif

#define QAL_IMP \
    QAL(LPALENABLE, alEnable); \
    QAL(LPALDISABLE, alDisable); \
    QAL(LPALISENABLED, alIsEnabled); \
    QAL(LPALGETSTRING, alGetString); \
    QAL(LPALGETBOOLEANV, alGetBooleanv); \
    QAL(LPALGETINTEGERV, alGetIntegerv); \
    QAL(LPALGETFLOATV, alGetFloatv); \
    QAL(LPALGETDOUBLEV, alGetDoublev); \
    QAL(LPALGETBOOLEAN, alGetBoolean); \
    QAL(LPALGETINTEGER, alGetInteger); \
    QAL(LPALGETFLOAT, alGetFloat); \
    QAL(LPALGETDOUBLE, alGetDouble); \
    QAL(LPALGETERROR, alGetError); \
    QAL(LPALISEXTENSIONPRESENT, alIsExtensionPresent); \
    QAL(LPALGETPROCADDRESS, alGetProcAddress); \
    QAL(LPALGETENUMVALUE, alGetEnumValue); \
    QAL(LPALLISTENERF, alListenerf); \
    QAL(LPALLISTENER3F, alListener3f); \
    QAL(LPALLISTENERFV, alListenerfv); \
    QAL(LPALLISTENERI, alListeneri); \
    QAL(LPALLISTENER3I, alListener3i); \
    QAL(LPALLISTENERIV, alListeneriv); \
    QAL(LPALGETLISTENERF, alGetListenerf); \
    QAL(LPALGETLISTENER3F, alGetListener3f); \
    QAL(LPALGETLISTENERFV, alGetListenerfv); \
    QAL(LPALGETLISTENERI, alGetListeneri); \
    QAL(LPALGETLISTENER3I, alGetListener3i); \
    QAL(LPALGETLISTENERIV, alGetListeneriv); \
    QAL(LPALGENSOURCES, alGenSources); \
    QAL(LPALDELETESOURCES, alDeleteSources); \
    QAL(LPALISSOURCE, alIsSource); \
    QAL(LPALSOURCEF, alSourcef); \
    QAL(LPALSOURCE3F, alSource3f); \
    QAL(LPALSOURCEFV, alSourcefv); \
    QAL(LPALSOURCEI, alSourcei); \
    QAL(LPALSOURCE3I, alSource3i); \
    QAL(LPALSOURCEIV, alSourceiv); \
    QAL(LPALGETSOURCEF, alGetSourcef); \
    QAL(LPALGETSOURCE3F, alGetSource3f); \
    QAL(LPALGETSOURCEFV, alGetSourcefv); \
    QAL(LPALGETSOURCEI, alGetSourcei); \
    QAL(LPALGETSOURCE3I, alGetSource3i); \
    QAL(LPALGETSOURCEIV, alGetSourceiv); \
    QAL(LPALSOURCEPLAYV, alSourcePlayv); \
    QAL(LPALSOURCESTOPV, alSourceStopv); \
    QAL(LPALSOURCEREWINDV, alSourceRewindv); \
    QAL(LPALSOURCEPAUSEV, alSourcePausev); \
    QAL(LPALSOURCEPLAY, alSourcePlay); \
    QAL(LPALSOURCESTOP, alSourceStop); \
    QAL(LPALSOURCEREWIND, alSourceRewind); \
    QAL(LPALSOURCEPAUSE, alSourcePause); \
    QAL(LPALSOURCEQUEUEBUFFERS, alSourceQueueBuffers); \
    QAL(LPALSOURCEUNQUEUEBUFFERS, alSourceUnqueueBuffers); \
    QAL(LPALGENBUFFERS, alGenBuffers); \
    QAL(LPALDELETEBUFFERS, alDeleteBuffers); \
    QAL(LPALISBUFFER, alIsBuffer); \
    QAL(LPALBUFFERDATA, alBufferData); \
    QAL(LPALBUFFERF, alBufferf); \
    QAL(LPALBUFFER3F, alBuffer3f); \
    QAL(LPALBUFFERFV, alBufferfv); \
    QAL(LPALBUFFERI, alBufferi); \
    QAL(LPALBUFFER3I, alBuffer3i); \
    QAL(LPALBUFFERIV, alBufferiv); \
    QAL(LPALGETBUFFERF, alGetBufferf); \
    QAL(LPALGETBUFFER3F, alGetBuffer3f); \
    QAL(LPALGETBUFFERFV, alGetBufferfv); \
    QAL(LPALGETBUFFERI, alGetBufferi); \
    QAL(LPALGETBUFFER3I, alGetBuffer3i); \
    QAL(LPALGETBUFFERIV, alGetBufferiv); \
    QAL(LPALDOPPLERFACTOR, alDopplerFactor); \
    QAL(LPALDOPPLERVELOCITY, alDopplerVelocity); \
    QAL(LPALSPEEDOFSOUND, alSpeedOfSound); \
    QAL(LPALDISTANCEMODEL, alDistanceModel);

#define QAL(type, func)  extern type q##func
QAL_IMP
#undef QAL

qboolean QAL_Init(void);
void QAL_Shutdown(void);

