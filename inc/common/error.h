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

#pragma once

#include <errno.h>

#define ERRNO_MAX       0x5000

#if EINVAL > 0
#define Q_ERR(e)        (e < 1 || e > ERRNO_MAX ? -ERRNO_MAX : -e)
#else
#define Q_ERR(e)        (e > -1 || e < -ERRNO_MAX ? -ERRNO_MAX : e)
#endif

#define Q_ERR_(e)       (-ERRNO_MAX - e)

// These values are extensions to system errno.
#define Q_ERR_SUCCESS           0           // Success
#define Q_ERR_FAILURE           Q_ERR_(0)   // Unspecified error
#define Q_ERR_UNKNOWN_FORMAT    Q_ERR_(1)   // Unknown file format
#define Q_ERR_INVALID_FORMAT    Q_ERR_(2)   // Invalid file format
#define Q_ERR_INVALID_PATH      Q_ERR_(3)   // Invalid quake path
#define Q_ERR_UNEXPECTED_EOF    Q_ERR_(4)   // Unexpected end of file
#define Q_ERR_FILE_TOO_SMALL    Q_ERR_(5)   // File too small
#define Q_ERR_FILE_NOT_REGULAR  Q_ERR_(6)   // Not a regular file
#define Q_ERR_OVERRUN           Q_ERR_(7)   // Decompression overrun
#define Q_ERR_STRING_TRUNCATED  Q_ERR_(8)   // String truncation avoided
#define Q_ERR_INFINITE_LOOP     Q_ERR_(9)   // Infinite loop avoided
#define Q_ERR_LIBRARY_ERROR     Q_ERR_(10)  // Library error
#define Q_ERR_OUT_OF_SLOTS      Q_ERR_(11)  // Out of slots
#define Q_ERR_INFLATE_FAILED    Q_ERR_(12)  // Inflate failed
#define Q_ERR_DEFLATE_FAILED    Q_ERR_(13)  // Deflate failed
#define Q_ERR_NOT_COHERENT      Q_ERR_(14)  // Coherency check failed
#define Q_ERR_BAD_COMPRESSION   Q_ERR_(15)  // Bad compression method

// This macro converts system errno into quake error value.
#define Q_ERRNO                 Q_ErrorNumber()

static inline int Q_ErrorNumber(void)
{
    int e = errno;
    return Q_ERR(e);
}

const char *Q_ErrorString(int error);
