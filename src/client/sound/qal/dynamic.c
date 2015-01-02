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

#include "shared/shared.h"
#include "system/system.h"
#include "common/cvar.h"
#include "common/common.h"
#include "common/files.h"
#include "dynamic.h"

#ifdef __APPLE__
#include <OpenAL/alc.h>
#else
#include <AL/alc.h>
#endif

#define QALC_IMP \
    QAL(LPALCCREATECONTEXT, alcCreateContext); \
    QAL(LPALCMAKECONTEXTCURRENT, alcMakeContextCurrent); \
    QAL(LPALCPROCESSCONTEXT, alcProcessContext); \
    QAL(LPALCSUSPENDCONTEXT, alcSuspendContext); \
    QAL(LPALCDESTROYCONTEXT, alcDestroyContext); \
    QAL(LPALCGETCURRENTCONTEXT, alcGetCurrentContext); \
    QAL(LPALCGETCONTEXTSDEVICE, alcGetContextsDevice); \
    QAL(LPALCOPENDEVICE, alcOpenDevice); \
    QAL(LPALCCLOSEDEVICE, alcCloseDevice); \
    QAL(LPALCGETERROR, alcGetError); \
    QAL(LPALCISEXTENSIONPRESENT, alcIsExtensionPresent); \
    QAL(LPALCGETPROCADDRESS, alcGetProcAddress); \
    QAL(LPALCGETENUMVALUE, alcGetEnumValue); \
    QAL(LPALCGETSTRING, alcGetString); \
    QAL(LPALCGETINTEGERV, alcGetIntegerv); \
    QAL(LPALCCAPTUREOPENDEVICE, alcCaptureOpenDevice); \
    QAL(LPALCCAPTURECLOSEDEVICE, alcCaptureCloseDevice); \
    QAL(LPALCCAPTURESTART, alcCaptureStart); \
    QAL(LPALCCAPTURESTOP, alcCaptureStop); \
    QAL(LPALCCAPTURESAMPLES, alcCaptureSamples);

static cvar_t   *al_driver;
static cvar_t   *al_device;

static void *handle;
static ALCdevice *device;
static ALCcontext *context;

#define QAL(type, func)  static type q##func
QALC_IMP
#undef QAL

#define QAL(type, func)  type q##func
QAL_IMP
#undef QAL

void QAL_Shutdown(void)
{
    if (context) {
        qalcMakeContextCurrent(NULL);
        qalcDestroyContext(context);
        context = NULL;
    }
    if (device) {
        qalcCloseDevice(device);
        device = NULL;
    }

#define QAL(type, func)  q##func = NULL
    QALC_IMP
    QAL_IMP
#undef QAL

    if (handle) {
        Sys_FreeLibrary(handle);
        handle = NULL;
    }

    if (al_driver)
        al_driver->flags &= ~CVAR_SOUND;
    if (al_device)
        al_device->flags &= ~CVAR_SOUND;
}

qboolean QAL_Init(void)
{
    al_driver = Cvar_Get("al_driver", LIBAL, 0);
    al_device = Cvar_Get("al_device", "", 0);

    // don't allow absolute or relative paths
    FS_SanitizeFilenameVariable(al_driver);

    Sys_LoadLibrary(al_driver->string, NULL, &handle);
    if (!handle) {
        return qfalse;
    }

#define QAL(type, func)  if ((q##func = Sys_GetProcAddress(handle, #func)) == NULL) goto fail;
    QALC_IMP
    QAL_IMP
#undef QAL

    device = qalcOpenDevice(al_device->string[0] ? al_device->string : NULL);
    if (!device) {
        Com_SetLastError(va("alcOpenDevice(%s) failed", al_device->string));
        goto fail;
    }

    context = qalcCreateContext(device, NULL);
    if (!context) {
        Com_SetLastError("alcCreateContext failed");
        goto fail;
    }

    if (!qalcMakeContextCurrent(context)) {
        Com_SetLastError("alcMakeContextCurrent failed");
        goto fail;
    }

    al_driver->flags |= CVAR_SOUND;
    al_device->flags |= CVAR_SOUND;

    return qtrue;

fail:
    QAL_Shutdown();
    return qfalse;
}

