/*
Copyright (C) 2010 skuller.net

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <config.h>
#include "q_shared.h"
#include <errno.h>
#include "error.h"

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
    "Unclean quake path",
    "Unexpected end of file",
    "File too small",
    "Bad run length packet",
    "String truncated",
    "Library error",
#if USE_ZLIB
    "Inflate failed",
    "Deflate failed",
    "Coherency check failed",
#endif
};

static const int num_errors =
    sizeof( error_table ) / sizeof( error_table[0] );

const char *Q_ErrorString( qerror_t error ) {
    int e;

    if( error >= 0 ) {
        return "Success";
    }

    if( error > -ERRNO_MAX ) {
#if EINVAL > 0
        e = -error;
#else
        e = error;
#endif
        return strerror( e );
    }

    e = _Q_ERR( error );

    return error_table[e >= num_errors ? 0 : e];
}

