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
#include "common/error.h"

static const char *const error_table[] = {
    "Unspecified error",
    "Unknown file format",
    "Invalid file format",
    "Bad lump extent",
    "Odd lump size",
    "Too many elements",
    "Too few elements",
    "Index out of range",
    "Invalid quake path",
    "File name too short",
    "Unexpected end of file",
    "File too small",
    "Not a regular file",
    "Bad run length packet",
    "String truncation avoided",
    "Runaway loop avoided",
    "Infinite loop avoided",
    "Library error",
    "Out of slots",
#if USE_ZLIB
    "Inflate failed",
    "Deflate failed",
    "Coherency check failed",
#endif
};

static const int num_errors = q_countof(error_table);

const char *Q_ErrorString(int error)
{
    int e;

    if (error >= 0) {
        return "Success";
    }

    if (error > -ERRNO_MAX) {
#if EINVAL > 0
        e = -error;
#else
        e = error;
#endif
        return strerror(e);
    }

    e = _Q_ERR(error);

    return error_table[e >= num_errors ? 0 : e];
}

