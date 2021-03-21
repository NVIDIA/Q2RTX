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

// "Flare" proxy game private definitions
#ifndef FLAREGAME_LOCAL_H_
#define FLAREGAME_LOCAL_H_

#include "shared/game.h"

struct flaregame_level_s
{
    int framenum;
};

extern struct flaregame_local_s
{
    game_import_t real_gi;
    game_export_t* real_ge;

    game_import_t exported_gi;
    game_export_t exported_ge;

    struct flaregame_level_s level;
} flaregame;

#endif // FLAREGAME_LOCAL_H_
