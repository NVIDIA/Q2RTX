/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2022 Frank Richter

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

// Flare savegame support
#ifndef FLAREGAME_SAVE_H_
#define FLAREGAME_SAVE_H_

#include "shared/shared.h"

qboolean FlareSave_Write(FILE *f);
qboolean FlareSave_Read(FILE *f);

#endif // FLAREGAME_SAVE_H_
