/*
Copyright (C) 2003-2012 Andrey Nazarov

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
#include "common/files.h"
#include "common/zone.h"
#include "client/client.h"
#include "client/input.h"
#include "client/keys.h"
#include "client/ui.h"
#include "client/video.h"
#include "refresh/refresh.h"

#include <SDL.h>

typedef struct {
    SDL_Surface     *surface;
    SDL_Surface     *icon;
    Uint16          gamma[3][256];
    vidFlags_t      flags;
    struct {
        qboolean    initialized;
        qboolean    grabbed;
    } mouse;
} sdl_state_t;

extern sdl_state_t    sdl;

qboolean VID_SDL_Init(void);
void VID_SDL_Shutdown(void);
qboolean VID_SDL_SetMode(int flags, int forcedepth);
void VID_SDL_SurfaceChanged(void);

#if USE_X11
void VID_GLX_SurfaceChanged(void);
#endif
