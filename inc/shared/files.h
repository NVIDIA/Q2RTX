/*
Copyright (C) 2023 Andrey Nazarov

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

typedef struct {
    int64_t size;
    int64_t ctime;
    int64_t mtime;
    char    name[1];
} file_info_t;

// file opening mode
#define FS_MODE_READ            0x00000000
#define FS_MODE_WRITE           0x00000001
#define FS_MODE_APPEND          0x00000002
#define FS_MODE_RDWR            0x00000003  // similar to FS_MODE_APPEND, but does not create the file
#define FS_MODE_MASK            0x00000003

// output buffering mode
#define FS_BUF_DEFAULT          0x00000000  // default mode (normally fully buffered)
#define FS_BUF_FULL             0x00000004  // fully buffered
#define FS_BUF_LINE             0x00000008  // line buffered
#define FS_BUF_NONE             0x0000000c  // unbuffered
#define FS_BUF_MASK             0x0000000c

// where to open file from
#define FS_TYPE_ANY             0x00000000  // open from anywhere
#define FS_TYPE_REAL            0x00000010  // open from disk only
#define FS_TYPE_PAK             0x00000020  // open from pack only
#define FS_TYPE_MASK            0x00000030

// where to look for a file
#define FS_PATH_ANY             0x00000000  // look in any search paths
#define FS_PATH_BASE            0x00000040  // look in base search paths
#define FS_PATH_GAME            0x00000080  // look in game search paths
#define FS_PATH_MASK            0x000000c0

// search mode for ListFiles()
#define FS_SEARCH_BYFILTER      0x00000100  // wildcard search instead of extension search
#define FS_SEARCH_SAVEPATH      0x00000200  // preserve file path
#define FS_SEARCH_EXTRAINFO     0x00000400  // return file_info_t *, not char *
#define FS_SEARCH_STRIPEXT      0x00000800  // strip file extension
#define FS_SEARCH_DIRSONLY      0x00001000  // search only directories (can't be mixed with other flags)
#define FS_SEARCH_RECURSIVE     0x00002000  // recursive search (implied by FS_SEARCH_BYFILTER)
#define FS_SEARCH_MASK          0x0000ff00

// misc flags for OpenFile()
#define FS_FLAG_GZIP            0x00000100  // transparently (de)compress with gzip
#define FS_FLAG_EXCL            0x00000200  // create the file, fail if already exists
#define FS_FLAG_TEXT            0x00000400  // open in text mode if from disk
#define FS_FLAG_DEFLATE         0x00000800  // if compressed, read raw deflate data, fail otherwise
#define FS_FLAG_LOADFILE        0x00001000  // open non-unique handle, must be closed very quickly
#define FS_FLAG_MASK            0x0000ff00
