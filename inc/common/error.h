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

#ifndef ERROR_H
#define ERROR_H

#include <errno.h>

#define ERRNO_MAX       0x5000

#if EINVAL > 0
#define Q_ERR(e)        (e < 1 || e > ERRNO_MAX ? -ERRNO_MAX : -e)
#else
#define Q_ERR(e)        (e > -1 || e < -ERRNO_MAX ? -ERRNO_MAX : e)
#endif

#define _Q_ERR(e)       (-ERRNO_MAX - e)

// These values are extensions to system errno.
#define Q_ERR_SUCCESS           0           // Success
#define Q_ERR_FAILURE           _Q_ERR(0)   // Unspecified error
#define Q_ERR_UNKNOWN_FORMAT    _Q_ERR(1)   // Unknown file format
#define Q_ERR_INVALID_FORMAT    _Q_ERR(2)   // Invalid file format
#define Q_ERR_BAD_EXTENT        _Q_ERR(3)   // Bad lump extent
#define Q_ERR_ODD_SIZE          _Q_ERR(4)   // Odd lump size
#define Q_ERR_TOO_MANY          _Q_ERR(5)   // Too many elements
#define Q_ERR_TOO_FEW           _Q_ERR(6)   // Too few elements
#define Q_ERR_BAD_INDEX         _Q_ERR(7)   // Index out of range
#define Q_ERR_INVALID_PATH      _Q_ERR(8)   // Invalid quake path
#define Q_ERR_NAMETOOSHORT      _Q_ERR(9)   // File name too short
#define Q_ERR_UNEXPECTED_EOF    _Q_ERR(10)  // Unexpected end of file
#define Q_ERR_FILE_TOO_SMALL    _Q_ERR(11)  // File too small
#define Q_ERR_FILE_NOT_REGULAR  _Q_ERR(12)  // Not a regular file
#define Q_ERR_BAD_RLE_PACKET    _Q_ERR(13)  // Bad run length packet
#define Q_ERR_STRING_TRUNCATED  _Q_ERR(14)  // String truncation avoided
#define Q_ERR_RUNAWAY_LOOP      _Q_ERR(15)  // Runaway loop avoided
#define Q_ERR_INFINITE_LOOP     _Q_ERR(16)  // Infinite loop avoided
#define Q_ERR_LIBRARY_ERROR     _Q_ERR(17)  // Library error
#define Q_ERR_OUT_OF_SLOTS      _Q_ERR(18)  // Out of slots
#if USE_ZLIB
#define Q_ERR_INFLATE_FAILED    _Q_ERR(19)  // Inflate failed
#define Q_ERR_DEFLATE_FAILED    _Q_ERR(20)  // Deflate failed
#define Q_ERR_NOT_COHERENT      _Q_ERR(21)  // Coherency check failed
#endif

// These values directly map to system errno.
#define Q_ERR_NOENT             Q_ERR(ENOENT)
#define Q_ERR_NAMETOOLONG       Q_ERR(ENAMETOOLONG)
#define Q_ERR_INVAL             Q_ERR(EINVAL)
#define Q_ERR_NOSYS             Q_ERR(ENOSYS)
#define Q_ERR_SPIPE             Q_ERR(ESPIPE)
#define Q_ERR_FBIG              Q_ERR(EFBIG)
#define Q_ERR_ISDIR             Q_ERR(EISDIR)
#define Q_ERR_AGAIN             Q_ERR(EAGAIN)
#define Q_ERR_MFILE             Q_ERR(EMFILE)
#define Q_ERR_EXIST             Q_ERR(EEXIST)
#define Q_ERR_BADF              Q_ERR(EBADF)
#define Q_ERR_PERM              Q_ERR(EPERM)

// This macro converts system errno into quake error value.
#define Q_ERRNO                 ({int e = errno; Q_ERR(e);})

const char *Q_ErrorString(int error);

#endif // ERROR_H
