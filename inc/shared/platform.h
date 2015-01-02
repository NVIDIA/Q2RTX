/*
Copyright (C) 2012 Andrey Nazarov

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

//
// platform.h -- platform-specific definitions
//

#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <direct.h>
#else
#include <unistd.h>
#endif

#ifdef _WIN32
#define PRIz    "Iu"
#else
#define PRIz    "zu"
#endif

#ifdef _WIN32
#define LIBSUFFIX   ".dll"
#else
#define LIBSUFFIX   ".so"
#endif

#ifdef _WIN32
#define PATH_SEP_CHAR       '\\'
#define PATH_SEP_STRING     "\\"
#else
#define PATH_SEP_CHAR       '/'
#define PATH_SEP_STRING     "/"
#endif

#if (defined _WIN32)
#define LIBGL   "opengl32"
#define LIBAL   "openal32"
#elif (defined __OpenBSD__)
#define LIBGL   "libGL.so"
#define LIBAL   "libopenal.so"
#elif (defined __APPLE__)
#define LIBGL   "/System/Library/Frameworks/OpenGL.framework/OpenGL"
#define LIBAL   "/System/Library/Frameworks/OpenAL.framework/OpenAL"
#else
#define LIBGL   "libGL.so.1"
#define LIBAL   "libopenal.so.1"
#endif

#ifdef _WIN32
#define os_mkdir(p)         _mkdir(p)
#define os_unlink(p)        _unlink(p)
#define os_stat(p, s)       _stat(p, s)
#define os_fstat(f, s)      _fstat(f, s)
#define os_fileno(f)        _fileno(f)
#define os_access(p, m)     _access(p, m)
#define Q_ISREG(m)          (((m) & _S_IFMT) == _S_IFREG)
#define Q_ISDIR(m)          (((m) & _S_IFMT) == _S_IFDIR)
#define Q_STATBUF           struct _stat
#else
#define os_mkdir(p)         mkdir(p, 0775)
#define os_unlink(p)        unlink(p)
#define os_stat(p, s)       stat(p, s)
#define os_fstat(f, s)      fstat(f, s)
#define os_fileno(f)        fileno(f)
#define os_access(p, m)     access(p, m)
#define Q_ISREG(m)          S_ISREG(m)
#define Q_ISDIR(m)          S_ISDIR(m)
#define Q_STATBUF           struct stat
#endif

#ifndef F_OK
#define F_OK    0
#define X_OK    1
#define W_OK    2
#define R_OK    4
#endif

#ifdef __GNUC__

#define q_printf(f, a)      __attribute__((format(printf, f, a)))
#define q_noreturn          __attribute__((noreturn))
#define q_noinline          __attribute__((noinline))
#define q_malloc            __attribute__((malloc))
#if __GNUC__ >= 4
#define q_sentinel          __attribute__((sentinel))
#else
#define q_sentinel
#endif

#define q_likely(x)         __builtin_expect(!!(x), 1)
#define q_unlikely(x)       __builtin_expect(!!(x), 0)
#if __GNUC__ >= 4
#define q_offsetof(t, m)    __builtin_offsetof(t, m)
#else
#define q_offsetof(t, m)    ((size_t)&((t *)0)->m)
#endif

#if USE_GAME_ABI_HACK
#define q_gameabi           __attribute__((callee_pop_aggregate_return(0)))
#else
#define q_gameabi
#endif

#ifdef _WIN32
#define q_exported          __attribute__((dllexport))
#else
#define q_exported          __attribute__((visibility("default")))
#endif

#define q_unused            __attribute__((unused))

#else /* __GNUC__ */

#define q_printf(f, a)
#define q_noreturn
#define q_noinline
#define q_malloc
#define q_sentinel

#define q_likely(x)         !!(x)
#define q_unlikely(x)       !!(x)
#define q_offsetof(t, m)    ((size_t)&((t *)0)->m)

#define q_gameabi

#ifdef _WIN32
#define q_exported          __declspec(dllexport)
#else
#define q_exported
#endif

#define q_unused

#endif /* !__GNUC__ */
