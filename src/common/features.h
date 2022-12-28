/*
Copyright (C) 2022 Andrey Nazarov

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

static const char *Com_GetFeatures(void)
{
    return
#if USE_AC_SERVER
    "anticheat-server "
#endif
#if USE_AUTOREPLY
    "auto-reply "
#endif
#if USE_CLIENT_GTV
    "client-gtv "
#endif
#if USE_UI
    "client-ui "
#endif
#if USE_DEBUG
    "debug "
#endif
#if USE_DLIGHTS
    "dynamic-lights "
#endif
#if USE_GAME_ABI_HACK
    "game-abi-hack "
#endif
#if USE_ICMP
    "icmp-errors "
#endif
#if USE_CURL
    "libcurl "
#endif
#if USE_JPG
    "libjpeg "
#endif
#if USE_PNG
    "libpng "
#endif
#if USE_MAPCHECKSUM
    "map-checksum "
#endif
#if USE_MD3
    "md3 "
#endif
#if USE_MVD_CLIENT
    "mvd-client "
#endif
#if USE_MVD_SERVER
    "mvd-server "
#endif
#if USE_OGG
    "ogg "
#endif
#if USE_OPENAL
    "openal "
#endif
#if USE_PACKETDUP
    "packetdup-hack "
#endif
#if USE_SAVEGAMES
    "save-games "
#endif
#if USE_SDL
    "sdl2 "
#endif
#if USE_SNDDMA
    "software-sound "
#endif
#if USE_SYSCON
    "system-console "
#endif
#if USE_TESTS
    "tests "
#endif
#if USE_TGA
    "tga "
#endif
#if USE_FPS
    "variable-fps "
#endif
#if USE_WAYLAND
    "wayland "
#endif
#if USE_DBGHELP
    "windows-crash-dumps "
#endif
#if USE_WIN32EGL
    "windows-egl "
#endif
#if USE_WINSVC
    "windows-service "
#endif
#if USE_X11
    "x11 "
#endif
#if USE_ZLIB
    "zlib "
#endif
    ;
}
