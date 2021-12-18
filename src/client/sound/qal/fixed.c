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

#include "shared/shared.h"
#include "common/cvar.h"
#include "common/common.h"
#include "fixed.h"

#ifdef __APPLE__
#include <OpenAL/alc.h>
#else
#include <AL/alc.h>
#endif

static cvar_t   *al_device;

static ALCdevice *device;
static ALCcontext *context;

void QAL_Shutdown(void)
{
    if (context) {
        alcMakeContextCurrent(NULL);
        alcDestroyContext(context);
        context = NULL;
    }
    if (device) {
        alcCloseDevice(device);
        device = NULL;
    }

    if (al_device)
        al_device->flags &= ~CVAR_SOUND;
}

bool QAL_Init(void)
{
    al_device = Cvar_Get("al_device", "", 0);

    device = alcOpenDevice(al_device->string[0] ? al_device->string : NULL);
    if (!device) {
        Com_SetLastError(va("alcOpenDevice(%s) failed", al_device->string));
        goto fail;
    }

    context = alcCreateContext(device, NULL);
    if (!context) {
        Com_SetLastError("alcCreateContext failed");
        goto fail;
    }

    if (!alcMakeContextCurrent(context)) {
        Com_SetLastError("alcMakeContextCurrent failed");
        goto fail;
    }

    al_device->flags |= CVAR_SOUND;

    return true;

fail:
    QAL_Shutdown();
    return false;
}
