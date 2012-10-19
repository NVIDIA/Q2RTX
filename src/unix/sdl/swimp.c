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

#include "video.h"

void VID_SDL_SurfaceChanged(void)
{
}

qboolean VID_Init(void)
{
    if (!VID_SDL_Init()) {
        return qfalse;
    }

    if (!VID_SDL_SetMode(SDL_SWSURFACE | SDL_HWPALETTE | SDL_RESIZABLE, 8)) {
        Com_EPrintf("Couldn't set video mode: %s\n", SDL_GetError());
        VID_SDL_Shutdown();
        return qfalse;
    }

    return qtrue;
}

void VID_Shutdown(void)
{
    VID_SDL_Shutdown();
}

void VID_UpdatePalette(const byte *palette)
{
    SDL_Color    colors[256];
    SDL_Color    *c;

    for (c = colors; c < colors + 256; c++) {
        c->r = palette[0];
        c->g = palette[1];
        c->b = palette[2];
        palette += 4;
    }

    SDL_SetPalette(sdl.surface, SDL_LOGPAL, colors, 0, 256);
}

void VID_VideoWait(void)
{
}

qboolean VID_VideoSync(void)
{
    return qtrue;
}

void VID_BeginFrame(void)
{
    SDL_LockSurface(sdl.surface);
}

void VID_EndFrame(void)
{
    SDL_UnlockSurface(sdl.surface);
    SDL_Flip(sdl.surface);
}


