/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#ifndef FILES_H
#define FILES_H

#include "common/cmd.h"
#include "common/error.h"
#include "common/zone.h"

#define MAX_LISTED_FILES    2048
#define MAX_LISTED_DEPTH    8

typedef struct file_info_s {
    size_t  size;
    time_t  ctime;
    time_t  mtime;
    char    name[1];
} file_info_t;

// bits 0 - 1, enum
#define FS_MODE_APPEND          0x00000000
#define FS_MODE_READ            0x00000001
#define FS_MODE_WRITE           0x00000002
#define FS_MODE_RDWR            0x00000003
#define FS_MODE_MASK            0x00000003

// bits 2 - 3, enum
#define FS_BUF_DEFAULT          0x00000000
#define FS_BUF_FULL             0x00000004
#define FS_BUF_LINE             0x00000008
#define FS_BUF_NONE             0x0000000c
#define FS_BUF_MASK             0x0000000c

// bits 4 - 5, enum
#define FS_TYPE_ANY             0x00000000
#define FS_TYPE_REAL            0x00000010
#define FS_TYPE_PAK             0x00000020
#define FS_TYPE_RESERVED        0x00000030
#define FS_TYPE_MASK            0x00000030

// bits 6 - 7, flag
#define FS_PATH_ANY             0x00000000
#define FS_PATH_BASE            0x00000040
#define FS_PATH_GAME            0x00000080
#define FS_PATH_MASK            0x000000c0

// bits 8 - 12, flag
#define FS_SEARCH_BYFILTER      0x00000100
#define FS_SEARCH_SAVEPATH      0x00000200
#define FS_SEARCH_EXTRAINFO     0x00000400
#define FS_SEARCH_STRIPEXT      0x00000800
#define FS_SEARCH_DIRSONLY      0x00001000
#define FS_SEARCH_MASK          0x00001f00

// bits 8 - 11, flag
#define FS_FLAG_GZIP            0x00000100
#define FS_FLAG_EXCL            0x00000200
#define FS_FLAG_TEXT            0x00000400
#define FS_FLAG_DEFLATE         0x00000800

//
// Limit the maximum file size FS_LoadFile can handle, as a protection from
// malicious paks causing memory exhaustion.
//
// Maximum size of legitimate BSP file on disk is ~12.7 MiB, let's round this
// up to 16 MiB. Assume that no loadable Q2 resource should ever exceed this
// limit.
//
#define MAX_LOADFILE            0x1000000

#define FS_Malloc(size)         Z_TagMalloc(size, TAG_FILESYSTEM)
#define FS_Mallocz(size)        Z_TagMallocz(size, TAG_FILESYSTEM)
#define FS_CopyString(string)   Z_TagCopyString(string, TAG_FILESYSTEM)
#define FS_LoadFile(path, buf)  FS_LoadFileEx(path, buf, 0, TAG_FILESYSTEM)
#define FS_FreeFile(buf)        Z_Free(buf)

// just regular malloc for now
#define FS_AllocTempMem(size)   FS_Malloc(size)
#define FS_FreeTempMem(buf)     Z_Free(buf)

// just regular caseless string comparsion
#define FS_pathcmp      Q_strcasecmp
#define FS_pathcmpn     Q_strncasecmp

#define FS_HashPath(s, size)            Com_HashStringLen(s, SIZE_MAX, size)
#define FS_HashPathLen(s, len, size)    Com_HashStringLen(s, len, size)

void    FS_Init(void);
void    FS_Shutdown(void);
void    FS_Restart(qboolean total);

#if USE_CLIENT
qerror_t FS_RenameFile(const char *from, const char *to);
#endif

qerror_t FS_CreatePath(char *path);

char    *FS_CopyExtraInfo(const char *name, const file_info_t *info);

ssize_t FS_FOpenFile(const char *filename, qhandle_t *f, unsigned mode);
void    FS_FCloseFile(qhandle_t f);
qhandle_t FS_EasyOpenFile(char *buf, size_t size, unsigned mode,
                          const char *dir, const char *name, const char *ext);

// nasty hack to check if file has valid gzip header:
// first 4 bytes of header are interpreted as 32-bit magic value.
// this value is checked to contain gzip ident bytes (0x1f, 0x8b),
// Z_DEFLATED compression byte (0x08), reserved options must be unset
#define CHECK_GZIP_HEADER(magic) \
    ((LittleLong(magic) & 0xe0ffffff) == 0x00088b1f)

qerror_t FS_FilterFile(qhandle_t f);

#define FS_FileExistsEx(path, flags) \
    (FS_LoadFileEx(path, NULL, flags, TAG_FREE) != Q_ERR_NOENT)
#define FS_FileExists(path) \
    FS_FileExistsEx(path, 0)

ssize_t FS_LoadFileEx(const char *path, void **buffer, unsigned flags, memtag_t tag);
// a NULL buffer will just return the file length without loading
// length < 0 indicates error

qerror_t FS_WriteFile(const char *path, const void *data, size_t len);

qboolean FS_EasyWriteFile(char *buf, size_t size, unsigned mode,
                          const char *dir, const char *name, const char *ext,
                          const void *data, size_t len);

ssize_t FS_Read(void *buffer, size_t len, qhandle_t f);
ssize_t FS_Write(const void *buffer, size_t len, qhandle_t f);
// properly handles partial reads

ssize_t FS_FPrintf(qhandle_t f, const char *format, ...) q_printf(2, 3);
ssize_t FS_ReadLine(qhandle_t f, char *buffer, size_t size);

void    FS_Flush(qhandle_t f);

ssize_t FS_Tell(qhandle_t f);
qerror_t FS_Seek(qhandle_t f, off_t offset);

ssize_t  FS_Length(qhandle_t f);

qboolean FS_WildCmp(const char *filter, const char *string);
qboolean FS_ExtCmp(const char *extension, const char *string);

void    **FS_ListFiles(const char *path, const char *filter, unsigned flags, int *count_p);
void    **FS_CopyList(void **list, int count);
file_info_t *FS_CopyInfo(const char *name, size_t size, time_t ctime, time_t mtime);
void    FS_FreeList(void **list);

size_t FS_NormalizePath(char *out, const char *in);
size_t FS_NormalizePathBuffer(char *out, const char *in, size_t size);

#define PATH_INVALID        0
#define PATH_VALID          1
#define PATH_MIXED_CASE     2

int FS_ValidatePath(const char *s);

void FS_SanitizeFilenameVariable(cvar_t *var);

#ifdef _WIN32
char *FS_ReplaceSeparators(char *s, int separator);
#endif

void FS_File_g(const char *path, const char *ext, unsigned flags, genctx_t *ctx);

extern cvar_t   *fs_game;

extern char     fs_gamedir[];

#endif // FILES_H
