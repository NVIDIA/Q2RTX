/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2021 Frank Richter

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

// "Proxy" game for adding flare support
#ifndef FLAREGAME_GAME_H_
#define FLAREGAME_GAME_H_

#include "shared/game.h"

game_export_t *FlareGame_Entry(game_export_t *(*entry)(game_import_t *), game_import_t *import);

#endif // FLAREGAME_GAME_H_
