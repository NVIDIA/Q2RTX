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

#include "shared/shared.h"
#include "shared/list.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/error.h"
#include "common/files.h"
#include "common/prompt.h"
#include "system/system.h"
#include "client/client.h"
#include "format/pak.h"

#include <fcntl.h>

#if USE_ZLIB
#include <zlib.h>
#endif

/*
=============================================================================

QUAKE FILESYSTEM

- transparently merged from several sources
- relative to the single virtual root
- case insensitive at pakfiles level,
  but may be case sensitive at host OS level
- uses / as path separators internally

=============================================================================
*/

#define MAX_FILE_HANDLES    32

#if USE_ZLIB
#define ZIP_MAXFILES    0x8000  // 32k files
#define ZIP_BUFSIZE     0x10000 // inflate in blocks of 64k

#define ZIP_BUFREADCOMMENT      1024
#define ZIP_SIZELOCALHEADER     30
#define ZIP_SIZECENTRALHEADER   20
#define ZIP_SIZECENTRALDIRITEM  46

#define ZIP_LOCALHEADERMAGIC    0x04034b50
#define ZIP_CENTRALHEADERMAGIC  0x02014b50
#define ZIP_ENDHEADERMAGIC      0x06054b50
#endif

#ifdef _DEBUG
#define FS_DPrintf(...) \
    if (fs_debug && fs_debug->integer) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__)
#else
#define FS_DPrintf(...)
#endif

#define PATH_NOT_CHECKED    -1

#define FOR_EACH_SYMLINK(link, list) \
    LIST_FOR_EACH(symlink_t, link, list, entry)

#define FOR_EACH_SYMLINK_SAFE(link, next, list) \
    LIST_FOR_EACH_SAFE(symlink_t, link, next, list, entry)

//
// in memory
//

typedef enum {
    FS_FREE,
    FS_REAL,
    FS_PAK,
#if USE_ZLIB
    FS_ZIP,
    FS_GZ,
#endif
    FS_BAD
} filetype_t;

#if USE_ZLIB
typedef struct {
    z_stream    stream;
    size_t      rest_in;
    byte        buffer[ZIP_BUFSIZE];
} zipstream_t;
#endif

typedef struct packfile_s {
    char        *name;
    size_t      namelen;
    size_t      filepos;
    size_t      filelen;
#if USE_ZLIB
    size_t      complen;
    unsigned    compmtd;    // compression method, 0 (stored) or Z_DEFLATED
    qboolean    coherent;   // true if local file header has been checked
#endif

    struct packfile_s *hash_next;
} packfile_t;

typedef struct {
    filetype_t  type;       // FS_PAK or FS_ZIP
    unsigned    refcount;   // for tracking pack users
    FILE        *fp;
    unsigned    num_files;
    packfile_t  *files;
    packfile_t  **file_hash;
    unsigned    hash_size;
    char        *names;
    char        *filename;
} pack_t;

typedef struct searchpath_s {
    struct searchpath_s *next;
    unsigned    mode;
    pack_t      *pack;        // only one of filename / pack will be used
    char        filename[1];
} searchpath_t;

typedef struct {
    filetype_t  type;
    unsigned    mode;
    FILE        *fp;
#if USE_ZLIB
    void        *zfp;       // gzFile for FS_GZ or zipstream_t for FS_ZIP
#endif
    packfile_t  *entry;     // pack entry this handle is tied to
    pack_t      *pack;      // points to the pack entry is from
    qboolean    unique;     // if true, then pack must be freed on close
    qerror_t    error;      // stream error indicator from read/write operation
    size_t      rest_out;   // remaining unread length for FS_PAK/FS_ZIP
    size_t      length;     // total cached file length
} file_t;

typedef struct {
    list_t  entry;
    size_t  targlen;
    size_t  namelen;
    char    *target;
    char    name[1];
} symlink_t;

// these point to user home directory
char                fs_gamedir[MAX_OSPATH];
//static char       fs_basedir[MAX_OSPATH];

static searchpath_t *fs_searchpaths;
static searchpath_t *fs_base_searchpaths;

static list_t       fs_hard_links;
static list_t       fs_soft_links;

static file_t       fs_files[MAX_FILE_HANDLES];

#ifdef _DEBUG
static int          fs_count_read;
static int          fs_count_open;
static int          fs_count_strcmp;
static int          fs_count_strlwr;
#define FS_COUNT_READ       fs_count_read++
#define FS_COUNT_OPEN       fs_count_open++
#define FS_COUNT_STRCMP     fs_count_strcmp++
#define FS_COUNT_STRLWR     fs_count_strlwr++
#else
#define FS_COUNT_READ       (void)0
#define FS_COUNT_OPEN       (void)0
#define FS_COUNT_STRCMP     (void)0
#define FS_COUNT_STRLWR     (void)0
#endif

#ifdef _DEBUG
static cvar_t       *fs_debug;
#endif

cvar_t              *fs_game;

#if USE_ZLIB
// local stream used for all file loads
static zipstream_t  fs_zipstream;

static void open_zip_file(file_t *file);
static void close_zip_file(file_t *file);
static ssize_t tell_zip_file(file_t *file);
static ssize_t read_zip_file(file_t *file, void *buf, size_t len);
#endif

// for tracking users of pack_t instance
// allows FS to be restarted while reading something from pack
static pack_t *pack_get(pack_t *pack);
static void pack_put(pack_t *pack);

/*

All of Quake's data access is through a hierchal file system,
but the contents of the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding all game directories.
The base directory is only used during filesystem initialization.

The "game directory" is the first tree on the search path and directory that
all generated files (savegames, screenshots, demos, config files) will be saved to.

*/

#ifdef _WIN32
char *FS_ReplaceSeparators(char *s, int separator)
{
    char *p;

    p = s;
    while (*p) {
        if (*p == '/' || *p == '\\') {
            *p = separator;
        }
        p++;
    }

    return s;
}
#endif

static inline qboolean validate_char(int c)
{
    if (!Q_isprint(c))
        return qfalse;

#ifdef _WIN32
    if (strchr("<>:\"|?*", c))
        return qfalse;
#endif

    return qtrue;
}

/*
================
FS_ValidatePath

Checks for bad (OS specific) and mixed case characters in path.
================
*/
int FS_ValidatePath(const char *s)
{
    int res = PATH_VALID;

    for (; *s; s++) {
        if (!validate_char(*s))
            return PATH_INVALID;

        if (Q_isupper(*s))
            res = PATH_MIXED_CASE;
    }

    return res;
}

/*
================
FS_SanitizeFilenameVariable

Checks that console variable is a valid single filename (not a path), otherwise
resets it to default value, printing a warning.
================
*/
void FS_SanitizeFilenameVariable(cvar_t *var)
{
    if (!FS_ValidatePath(var->string)) {
        Com_Printf("'%s' contains invalid characters for a filename.\n", var->name);
        goto reset;
    }

    if (strchr(var->string, '/') || strchr(var->string, '\\')) {
        Com_Printf("'%s' should be a single filename, not a path.\n", var->name);
        goto reset;
    }

    return;

reset:
    Com_Printf("...falling back to %s\n", var->default_string);
    Cvar_Reset(var);
}

/*
================
FS_NormalizePath

Simplifies the path, converting backslashes to slashes and removing ./ and ../
components, as well as duplicated slashes. Any leading slashes are also skipped.

May operate in place if in == out.

    ///foo       -> foo
    foo\bar      -> foo/bar
    foo/..       -> <empty>
    foo/../bar   -> bar
    foo/./bar    -> foo/bar
    foo//bar     -> foo/bar
    ./foo        -> foo
================
*/
size_t FS_NormalizePath(char *out, const char *in)
{
    char *start = out;
    uint32_t pre = '/';

    while (1) {
        int c = *in++;

        if (c == '/' || c == '\\' || c == 0) {
            if ((pre & 0xffffff) == (('/' << 16) | ('.' << 8) | '.')) {
                out -= 4;
                if (out < start) {
                    // can't go past root
                    out = start;
                    if (c == 0)
                        break;
                } else {
                    while (out > start && *out != '/')
                        out--;
                    if (c == 0)
                        break;
                    if (out > start)
                        // save the slash
                        out++;
                }
                pre = '/';
                continue;
            }

            if ((pre & 0xffff) == (('/' << 8) | '.')) {
                // eat the dot
                out--;
                if (c == 0) {
                    if (out > start)
                        // eat the slash
                        out--;
                    break;
                }
                pre = '/';
                continue;
            }

            if ((pre & 0xff) == '/') {
                if (c == 0)
                    break;
                continue;
            }

            if (c == 0)
                break;
            c = '/';
        }

        pre = (pre << 8) | c;
        *out++ = c;
    }

    *out = 0;
    return out - start;
}

/*
================
FS_NormalizePathBuffer

Buffer safe version of FS_NormalizePath. Return value >= size signifies
overflow, empty string is stored in output buffer in this case.
================
*/
size_t FS_NormalizePathBuffer(char *out, const char *in, size_t size)
{
    size_t len = strlen(in);

    if (len >= size) {
        if (size)
            *out = 0;
        return len;
    }

    return FS_NormalizePath(out, in);
}

// =============================================================================

static file_t *alloc_handle(qhandle_t *f)
{
    file_t *file;
    int i;

    for (i = 0, file = fs_files; i < MAX_FILE_HANDLES; i++, file++) {
        if (file->type == FS_FREE) {
            *f = i + 1;
            return file;
        }
    }

    return NULL;
}

static file_t *file_for_handle(qhandle_t f)
{
    file_t *file;

    if (f < 1 || f > MAX_FILE_HANDLES)
        return NULL;

    file = &fs_files[f - 1];
    if (file->type == FS_FREE)
        return NULL;

    if (file->type < FS_FREE || file->type >= FS_BAD)
        Com_Error(ERR_FATAL, "%s: bad file type", __func__);

    return file;
}

static void cleanup_path(char *s)
{
    for (; *s; s++) {
        if (!validate_char(*s))
            *s = '_';
    }
}

// expects a buffer of at least MAX_OSPATH bytes!
static symlink_t *expand_links(list_t *list, char *buffer, size_t *len_p)
{
    symlink_t   *link;
    size_t      namelen = *len_p;

    FOR_EACH_SYMLINK(link, list) {
        if (link->namelen > namelen) {
            continue;
        }
        if (!FS_pathcmpn(buffer, link->name, link->namelen)) {
            size_t newlen = namelen - link->namelen + link->targlen;

            if (newlen < MAX_OSPATH) {
                memmove(buffer + link->targlen, buffer + link->namelen,
                        namelen - link->namelen + 1);
                memcpy(buffer, link->target, link->targlen);
            }

            *len_p = newlen;
            return link;
        }
    }

    return NULL;
}

/*
================
FS_Length
================
*/
ssize_t FS_Length(qhandle_t f)
{
    file_t *file = file_for_handle(f);

    if (!file)
        return Q_ERR_BADF;

    if ((file->mode & FS_MODE_MASK) == FS_MODE_READ)
        return file->length;

    return Q_ERR_NOSYS;
}

/*
============
FS_Tell
============
*/
ssize_t FS_Tell(qhandle_t f)
{
    file_t *file = file_for_handle(f);
    long ret;

    if (!file)
        return Q_ERR_BADF;

    switch (file->type) {
    case FS_REAL:
        ret = ftell(file->fp);
        if (ret == -1) {
            return Q_Errno();
        }
        return ret;
    case FS_PAK:
        return file->length - file->rest_out;
#if USE_ZLIB
    case FS_ZIP:
        return tell_zip_file(file);
    case FS_GZ:
        ret = gztell(file->zfp);
        if (ret == -1) {
            return Q_ERR_LIBRARY_ERROR;
        }
        return ret;
#endif
    default:
        return Q_ERR_NOSYS;
    }
}

static qerror_t seek_pak_file(file_t *file, off_t offset)
{
    packfile_t *entry = file->entry;
    long filepos;

    if (offset > entry->filelen)
        offset = entry->filelen;

    if (entry->filepos > LONG_MAX - offset)
        return Q_ERR_INVAL;

    filepos = entry->filepos + offset;
    if (fseek(file->fp, filepos, SEEK_SET) == -1)
        return Q_Errno();

    file->rest_out = entry->filelen - offset;

    return Q_ERR_SUCCESS;
}

/*
============
FS_Seek

Seeks to an absolute position within the file.
============
*/
qerror_t FS_Seek(qhandle_t f, off_t offset)
{
    file_t *file = file_for_handle(f);

    if (!file)
        return Q_ERR_BADF;

    if (offset > LONG_MAX)
        return Q_ERR_INVAL;

    if (offset < 0)
        offset = 0;

    switch (file->type) {
    case FS_REAL:
        if (fseek(file->fp, (long)offset, SEEK_SET) == -1) {
            return Q_Errno();
        }
        return Q_ERR_SUCCESS;
    case FS_PAK:
        return seek_pak_file(file, offset);
#if USE_ZLIB
    case FS_GZ:
        if (gzseek(file->zfp, (z_off_t)offset, SEEK_SET) == -1) {
            return Q_Errno();
        }
        return Q_ERR_SUCCESS;
#endif
    default:
        return Q_ERR_NOSYS;
    }
}

/*
============
FS_CreatePath

Creates any directories needed to store the given filename.
Expects a fully qualified, normalized system path (i.e. with / separators).
============
*/
qerror_t FS_CreatePath(char *path)
{
    char *ofs;
    int ret;

    ofs = path;

#ifdef _WIN32
    // check for UNC path and skip "//computer/share/" part
    if (*path == '/' && path[1] == '/') {
        char *p;

        p = strchr(path + 2, '/');
        if (p) {
            p = strchr(p + 1, '/');
            if (p) {
                ofs = p + 1;
            }
        }
    }
#endif

    // skip leading slash(es)
    for (; *ofs == '/'; ofs++)
        ;

    for (; *ofs; ofs++) {
        if (*ofs == '/') {
            // create the directory
            *ofs = 0;
            ret = os_mkdir(path);
            *ofs = '/';
            if (ret == -1) {
                qerror_t err = Q_Errno();
                if (err != Q_ERR_EXIST)
                    return err;
            }
        }
    }

    return Q_ERR_SUCCESS;
}

#define FS_ERR_READ(fp) \
    (ferror(fp) ? Q_Errno() : Q_ERR_UNEXPECTED_EOF)
#define FS_ERR_WRITE(fp) \
    (ferror(fp) ? Q_Errno() : Q_ERR_FAILURE)

/*
============
FS_FilterFile

Turns FS_REAL file into FS_GZIP by reopening it through GZIP.
File position is reset to the beginning of file.
============
*/
qerror_t FS_FilterFile(qhandle_t f)
{
#if USE_ZLIB
    file_t *file = file_for_handle(f);
    unsigned mode;
    char *modeStr;
    void *zfp;
    uint32_t magic;
    size_t length;
    int fd;

    if (!file)
        return Q_ERR_BADF;

    switch (file->type) {
    case FS_GZ:
        return Q_ERR_SUCCESS;
    case FS_REAL:
        break;
    default:
        return Q_ERR_NOSYS;
    }

    mode = file->mode & FS_MODE_MASK;
    switch (mode) {
    case FS_MODE_READ:
        // should have at least 10 bytes of header and 8 bytes of trailer
        if (file->length < 18) {
            return Q_ERR_FILE_TOO_SMALL;
        }

        // seek to the header
        if (fseek(file->fp, 0, SEEK_SET) == -1) {
            return Q_Errno();
        }

        // read magic
        if (fread(&magic, 1, 4, file->fp) != 4) {
            return FS_ERR_READ(file->fp);
        }

        // check for gzip header
        if (!CHECK_GZIP_HEADER(magic)) {
            return Q_ERR_INVALID_FORMAT;
        }

        // seek to the trailer
        if (fseek(file->fp, file->length - 4, SEEK_SET) == -1) {
            return Q_Errno();
        }

        // read uncompressed length
        if (fread(&magic, 1, 4, file->fp) != 4) {
            return FS_ERR_READ(file->fp);
        }

        length = LittleLong(magic);
        modeStr = "rb";
        break;

    case FS_MODE_WRITE:
        length = 0;
        modeStr = "wb";
        break;

    default:
        return Q_ERR_NOSYS;
    }

    // rewind back to beginning
    if (fseek(file->fp, 0, SEEK_SET) == -1) {
        return Q_Errno();
    }

    fd = os_fileno(file->fp);
    if (fd == -1)
        return Q_Errno();

    zfp = gzdopen(fd, modeStr);
    if (!zfp) {
        return Q_ERR_FAILURE;
    }

    file->length = length;
    file->zfp = zfp;
    file->type = FS_GZ;
    return Q_ERR_SUCCESS;
#else
    return Q_ERR_NOSYS;
#endif
}


/*
==============
FS_FCloseFile
==============
*/
void FS_FCloseFile(qhandle_t f)
{
    file_t *file = file_for_handle(f);

    if (!file)
        return;

    switch (file->type) {
    case FS_REAL:
        fclose(file->fp);
        break;
    case FS_PAK:
        if (file->unique) {
            fclose(file->fp);
            pack_put(file->pack);
        }
        break;
#if USE_ZLIB
    case FS_GZ:
        gzclose(file->zfp);
        fclose(file->fp);
        break;
    case FS_ZIP:
        if (file->unique) {
            close_zip_file(file);
            pack_put(file->pack);
        }
        break;
#endif
    default:
        break;
    }

    memset(file, 0, sizeof(*file));
}

static qerror_t get_path_info(const char *path, file_info_t *info)
{
    Q_STATBUF st;

    if (os_stat(path, &st) == -1)
        return Q_Errno();

    if (Q_ISDIR(st.st_mode))
        return Q_ERR_ISDIR;

    if (!Q_ISREG(st.st_mode))
        return Q_ERR_FILE_NOT_REGULAR;

    if (info) {
        info->size = st.st_size;
        info->ctime = st.st_ctime;
        info->mtime = st.st_mtime;
    }

    return Q_ERR_SUCCESS;
}

static qerror_t get_fp_info(FILE *fp, file_info_t *info)
{
    Q_STATBUF st;
    int fd;

    fd = os_fileno(fp);
    if (fd == -1)
        return Q_Errno();

    if (os_fstat(fd, &st) == -1)
        return Q_Errno();

    if (Q_ISDIR(st.st_mode))
        return Q_ERR_ISDIR;

    if (!Q_ISREG(st.st_mode))
        return Q_ERR_FILE_NOT_REGULAR;

    if (info) {
        info->size = st.st_size;
        info->ctime = st.st_ctime;
        info->mtime = st.st_mtime;
    }

    return Q_ERR_SUCCESS;
}

static inline FILE *fopen_hack(const char *path, const char *mode)
{
#ifndef _GNU_SOURCE
    if (mode[0] == 'w' && mode[1] == 'x') {
#ifdef _WIN32
        int flags = _O_WRONLY | _O_CREAT | _O_EXCL | _S_IREAD | _S_IWRITE;
        int fd;
        FILE *fp;

        if (mode[2] == 'b')
            flags |= _O_BINARY;

        fd = _open(path, flags);
        if (fd == -1)
            return NULL;

        fp = _fdopen(fd, (flags & _O_BINARY) ? "wb" : "w");
        if (fp == NULL)
            _close(fd);

        return fp;
#else
        int flags = O_WRONLY | O_CREAT | O_EXCL;
        int perm = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
        int fd;
        FILE *fp;

        fd = open(path, flags, perm);
        if (fd == -1)
            return NULL;

        fp = fdopen(fd, "wb");
        if (fp == NULL)
            close(fd);

        return fp;
#endif
    }
#endif // _GNU_SOURCE

    return fopen(path, mode);
}

static ssize_t open_file_write(file_t *file, const char *name)
{
    char normalized[MAX_OSPATH], fullpath[MAX_OSPATH];
    FILE *fp;
    char mode_str[8];
    unsigned mode;
    size_t len;
    long pos;
    qerror_t ret;

    // normalize the path
    len = FS_NormalizePathBuffer(normalized, name, sizeof(normalized));
    if (len >= sizeof(normalized)) {
        return Q_ERR_NAMETOOLONG;
    }

    // reject empty paths
    if (len == 0) {
        return Q_ERR_NAMETOOSHORT;
    }

    // check for bad characters
    if (!FS_ValidatePath(normalized)) {
        ret = Q_ERR_INVALID_PATH;
        goto fail1;
    }

    // expand the path
    if ((file->mode & FS_PATH_MASK) == FS_PATH_BASE) {
        if (sys_homedir->string[0]) {
            len = Q_concat(fullpath, sizeof(fullpath),
                           sys_homedir->string, "/" BASEGAME "/", normalized, NULL);
        } else {
            len = Q_concat(fullpath, sizeof(fullpath),
                           sys_basedir->string, "/" BASEGAME "/", normalized, NULL);
        }
    } else {
        len = Q_concat(fullpath, sizeof(fullpath),
                       fs_gamedir, "/", normalized, NULL);
    }
    if (len >= sizeof(fullpath)) {
        ret = Q_ERR_NAMETOOLONG;
        goto fail1;
    }

    mode = file->mode & FS_MODE_MASK;
    switch (mode) {
    case FS_MODE_APPEND:
        strcpy(mode_str, "a");
        break;
    case FS_MODE_WRITE:
        strcpy(mode_str, "w");
        if (file->mode & FS_FLAG_EXCL)
            strcat(mode_str, "x");
        break;
    case FS_MODE_RDWR:
        // this mode is only used by client downloading code
        // similar to FS_MODE_APPEND, but does not create
        // the file if it does not exist
        strcpy(mode_str, "r+");
        break;
    default:
        ret = Q_ERR_INVAL;
        goto fail1;
    }

    // open in binary mode by default
    if (!(file->mode & FS_FLAG_TEXT))
        strcat(mode_str, "b");

    ret = FS_CreatePath(fullpath);
    if (ret) {
        goto fail1;
    }

    fp = fopen_hack(fullpath, mode_str);
    if (!fp) {
        ret = Q_Errno();
        goto fail1;
    }

#ifndef _WIN32
    // check if this is a regular file
    ret = get_fp_info(fp, NULL);
    if (ret) {
        goto fail2;
    }
#endif

    switch (file->mode & FS_BUF_MASK) {
    case FS_BUF_NONE:
        // make it unbuffered
        setvbuf(fp, NULL, _IONBF, BUFSIZ);
        break;
    case FS_BUF_LINE:
        // make it line buffered
        setvbuf(fp, NULL, _IOLBF, BUFSIZ);
        break;
    case FS_BUF_FULL:
        // make it fully buffered
        setvbuf(fp, NULL, _IOFBF, BUFSIZ);
        break;
    default:
        // use default mode (normally fully buffered)
        break;
    }

    if (mode == FS_MODE_RDWR) {
        // seek to the end of file for appending
        if (fseek(fp, 0, SEEK_END) == -1) {
            ret = Q_Errno();
            goto fail2;
        }
    }

    // return current position (non-zero for appending modes)
    pos = ftell(fp);
    if (pos == -1) {
        ret = Q_Errno();
        goto fail2;
    }

    FS_DPrintf("%s: %s: %lu bytes\n", __func__, fullpath, pos);

    file->type = FS_REAL;
    file->fp = fp;
    file->unique = qtrue;
    file->error = Q_ERR_SUCCESS;
    file->length = 0;

    return pos;

fail2:
    fclose(fp);
fail1:
    FS_DPrintf("%s: %s: %s\n", __func__, normalized, Q_ErrorString(ret));
    return ret;
}

#if USE_ZLIB

static qerror_t check_header_coherency(FILE *fp, packfile_t *entry)
{
    unsigned flags, comp_mtd;
    size_t comp_len, file_len;
    size_t name_size, xtra_size;
    byte header[ZIP_SIZELOCALHEADER];
    size_t ofs;

    if (fseek(fp, (long)entry->filepos, SEEK_SET) == -1)
        return Q_Errno();
    if (fread(header, 1, sizeof(header), fp) != sizeof(header))
        return FS_ERR_READ(fp);

    // check the magic
    if (LittleLongMem(&header[0]) != ZIP_LOCALHEADERMAGIC)
        return Q_ERR_NOT_COHERENT;

    flags = LittleShortMem(&header[6]);
    comp_mtd = LittleShortMem(&header[8]);
    comp_len = LittleLongMem(&header[18]);
    file_len = LittleLongMem(&header[22]);
    name_size = LittleShortMem(&header[26]);
    xtra_size = LittleShortMem(&header[28]);

    if (comp_mtd != entry->compmtd)
        return Q_ERR_NOT_COHERENT;

    // bit 3 tells that file lengths were not known
    // at the time local header was written, so don't check them
    if ((flags & 8) == 0) {
        if (comp_len != entry->complen)
            return Q_ERR_NOT_COHERENT;
        if (file_len != entry->filelen)
            return Q_ERR_NOT_COHERENT;
    }

    ofs = ZIP_SIZELOCALHEADER + name_size + xtra_size;
    if (entry->filepos > LONG_MAX - ofs) {
        return Q_ERR_SPIPE;
    }

    entry->filepos += ofs;
    entry->coherent = qtrue;
    return Q_ERR_SUCCESS;
}

static voidpf FS_zalloc(voidpf opaque, uInt items, uInt size)
{
    return FS_Malloc(items * size);
}

static void FS_zfree(voidpf opaque, voidpf address)
{
    Z_Free(address);
}

static void open_zip_file(file_t *file)
{
    zipstream_t *s;
    z_streamp z;

    if (file->unique) {
        s = FS_Malloc(sizeof(*s));
        memset(&s->stream, 0, sizeof(s->stream));
    } else {
        s = &fs_zipstream;
    }

    z = &s->stream;
    if (z->state) {
        // already initialized, just reset
        inflateReset(z);
    } else {
        z->zalloc = FS_zalloc;
        z->zfree = FS_zfree;
        if (inflateInit2(z, -MAX_WBITS) != Z_OK) {
            Com_Error(ERR_FATAL, "%s: inflateInit2() failed", __func__);
        }
    }

    z->avail_in = z->avail_out = 0;
    z->total_in = z->total_out = 0;
    z->next_in = z->next_out = NULL;

    s->rest_in = file->entry->complen;
    file->zfp = s;
}

// only called for unique handles
static void close_zip_file(file_t *file)
{
    zipstream_t *s = file->zfp;

    inflateEnd(&s->stream);
    Z_Free(s);

    fclose(file->fp);
}

static ssize_t tell_zip_file(file_t *file)
{
    zipstream_t *s = file->zfp;

    return s->stream.total_out;
}

static ssize_t read_zip_file(file_t *file, void *buf, size_t len)
{
    zipstream_t *s = file->zfp;
    z_streamp z = &s->stream;
    size_t block, result;
    int ret;

    if (len > file->rest_out) {
        len = file->rest_out;
    }
    if (!len) {
        return 0;
    }

    z->next_out = buf;
    z->avail_out = (uInt)len;

    do {
        if (!z->avail_in) {
            if (!s->rest_in) {
                break;
            }

            // fill in the temp buffer
            block = ZIP_BUFSIZE;
            if (block > s->rest_in) {
                block = s->rest_in;
            }

            result = fread(s->buffer, 1, block, file->fp);
            if (result != block) {
                file->error = FS_ERR_READ(file->fp);
                if (!result) {
                    break;
                }
            }

            s->rest_in -= result;
            z->next_in = s->buffer;
            z->avail_in = result;
        }

        ret = inflate(z, Z_SYNC_FLUSH);
        if (ret == Z_STREAM_END) {
            break;
        }
        if (ret != Z_OK) {
            file->error = Q_ERR_INFLATE_FAILED;
            break;
        }
        if (file->error) {
            break;
        }
    } while (z->avail_out);

    len -= z->avail_out;
    file->rest_out -= len;

    if (file->error && len == 0) {
        return file->error;
    }

    return len;
}

#endif

// open a new file on the pakfile
static ssize_t open_from_pak(file_t *file, pack_t *pack, packfile_t *entry, qboolean unique)
{
    FILE *fp;
    qerror_t ret;

    if (unique) {
        fp = fopen(pack->filename, "rb");
        if (!fp) {
            ret = Q_Errno();
            goto fail1;
        }
    } else {
        fp = pack->fp;
        clearerr(fp);
    }

#if USE_ZLIB
    if (pack->type == FS_ZIP && !entry->coherent) {
        ret = check_header_coherency(fp, entry);
        if (ret) {
            goto fail2;
        }
    }
#endif

    if (fseek(fp, (long)entry->filepos, SEEK_SET) == -1) {
        ret = Q_Errno();
        goto fail2;
    }

    file->type = pack->type;
    file->fp = fp;
    file->entry = entry;
    file->pack = pack;
    file->unique = unique;
    file->error = Q_ERR_SUCCESS;
    file->rest_out = entry->filelen;
    file->length = entry->filelen;

#if USE_ZLIB
    if (pack->type == FS_ZIP) {
        if (file->mode & FS_FLAG_DEFLATE) {
            // server wants raw deflated data for downloads
            file->type = FS_PAK;
            file->rest_out = entry->complen;
            file->length = entry->complen;
        } else if (entry->compmtd) {
            open_zip_file(file);
        } else {
            // stored, just pretend it's a packfile
            file->type = FS_PAK;
        }
    }
#endif

    if (unique) {
        // reference source pak
        pack_get(pack);
    }

    FS_DPrintf("%s: %s/%s: %"PRIz" bytes\n",
               __func__, pack->filename, entry->name, file->length);

    return file->length;

fail2:
    if (unique) {
        fclose(fp);
    }
fail1:
    FS_DPrintf("%s: %s/%s: %s\n", __func__, pack->filename, entry->name, Q_ErrorString(ret));
    return ret;
}

static ssize_t open_from_disk(file_t *file, const char *fullpath)
{
    FILE *fp;
    file_info_t info;
    qerror_t ret;

    FS_COUNT_OPEN;

    fp = fopen(fullpath, "rb");
    if (!fp) {
        ret = Q_Errno();
        goto fail;
    }

    ret = get_fp_info(fp, &info);
    if (ret) {
        fclose(fp);
        goto fail;
    }

    file->type = FS_REAL;
    file->fp = fp;
    file->unique = qtrue;
    file->error = Q_ERR_SUCCESS;
    file->length = info.size;

    FS_DPrintf("%s: %s: %"PRIz" bytes\n", __func__, fullpath, info.size);
    return info.size;

fail:
    FS_DPrintf("%s: %s: %s\n", __func__, fullpath, Q_ErrorString(ret));
    return ret;
}

// Finds the file in the search path.
// Fills file_t and returns file length.
// Used for streaming data out of either a pak file or a seperate file.
static ssize_t open_file_read(file_t *file, const char *normalized, size_t namelen, qboolean unique)
{
    char            fullpath[MAX_OSPATH];
    searchpath_t    *search;
    pack_t          *pak;
    unsigned        hash;
    packfile_t      *entry;
    ssize_t         ret;
    int             valid;
    size_t          len;

    FS_COUNT_READ;

    hash = FS_HashPath(normalized, 0);

    valid = PATH_NOT_CHECKED;

// search through the path, one element at a time
    for (search = fs_searchpaths; search; search = search->next) {
        if (file->mode & FS_PATH_MASK) {
            if ((file->mode & search->mode & FS_PATH_MASK) == 0) {
                continue;
            }
        }

        // is the element a pak file?
        if (search->pack) {
            if ((file->mode & FS_TYPE_MASK) == FS_TYPE_REAL) {
                continue;
            }
            // don't bother searching in paks if length exceedes MAX_QPATH
            if (namelen >= MAX_QPATH) {
                continue;
            }
            pak = search->pack;
#if USE_ZLIB
            if ((file->mode & FS_FLAG_DEFLATE) && pak->type != FS_ZIP) {
                continue;
            }
#endif
            // look through all the pak file elements
            entry = pak->file_hash[hash & (pak->hash_size - 1)];
            for (; entry; entry = entry->hash_next) {
                if (entry->namelen != namelen) {
                    continue;
                }
#if USE_ZLIB
                if ((file->mode & FS_FLAG_DEFLATE) && entry->compmtd != Z_DEFLATED) {
                    continue;
                }
#endif
                FS_COUNT_STRCMP;
                if (!FS_pathcmp(entry->name, normalized)) {
                    // found it!
                    return open_from_pak(file, pak, entry, unique);
                }
            }
        } else {
            if ((file->mode & FS_TYPE_MASK) == FS_TYPE_PAK) {
                continue;
            }
#if USE_ZLIB
            if (file->mode & FS_FLAG_DEFLATE) {
                continue;
            }
#endif
            // don't error out immediately if the path is found to be invalid,
            // just stop looking for it in directory tree but continue to search
            // for it in packs, to give broken maps or mods a chance to work
            if (valid == PATH_NOT_CHECKED) {
                valid = FS_ValidatePath(normalized);
            }
            if (valid == PATH_INVALID) {
                continue;
            }
            // check a file in the directory tree
            len = Q_concat(fullpath, sizeof(fullpath),
                           search->filename, "/", normalized, NULL);
            if (len >= sizeof(fullpath)) {
                ret = Q_ERR_NAMETOOLONG;
                goto fail;
            }

            ret = open_from_disk(file, fullpath);
            if (ret != Q_ERR_NOENT)
                return ret;

#ifndef _WIN32
            if (valid == PATH_MIXED_CASE) {
                // convert to lower case and retry
                FS_COUNT_STRLWR;
                Q_strlwr(fullpath + strlen(search->filename) + 1);
                ret = open_from_disk(file, fullpath);
                if (ret != Q_ERR_NOENT)
                    return ret;
            }
#endif
        }
    }

    // return error if path was checked and found to be invalid
    ret = valid ? Q_ERR_NOENT : Q_ERR_INVALID_PATH;

fail:
    FS_DPrintf("%s: %s: %s\n", __func__, normalized, Q_ErrorString(ret));
    return ret;
}

// Normalizes quake path, expands symlinks
static ssize_t expand_open_file_read(file_t *file, const char *name, qboolean unique)
{
    char        normalized[MAX_OSPATH];
    ssize_t     ret;
    size_t      namelen;

// normalize path
    namelen = FS_NormalizePathBuffer(normalized, name, MAX_OSPATH);
    if (namelen >= MAX_OSPATH) {
        return Q_ERR_NAMETOOLONG;
    }

// expand hard symlinks
    if (expand_links(&fs_hard_links, normalized, &namelen) && namelen >= MAX_OSPATH) {
        return Q_ERR_NAMETOOLONG;
    }

// reject empty paths
    if (namelen == 0) {
        return Q_ERR_NAMETOOSHORT;
    }

    ret = open_file_read(file, normalized, namelen, unique);
    if (ret == Q_ERR_NOENT) {
// expand soft symlinks
        if (expand_links(&fs_soft_links, normalized, &namelen)) {
            if (namelen >= MAX_OSPATH) {
                return Q_ERR_NAMETOOLONG;
            }
            ret = open_file_read(file, normalized, namelen, unique);
        }
    }

    return ret;
}

static ssize_t read_pak_file(file_t *file, void *buf, size_t len)
{
    size_t result;

    if (len > file->rest_out) {
        len = file->rest_out;
    }
    if (!len) {
        return 0;
    }

    result = fread(buf, 1, len, file->fp);
    if (result != len) {
        file->error = FS_ERR_READ(file->fp);
        if (!result) {
            return file->error;
        }
    }

    file->rest_out -= result;
    return result;
}

static ssize_t read_phys_file(file_t *file, void *buf, size_t len)
{
    size_t result;

    result = fread(buf, 1, len, file->fp);
    if (result != len && ferror(file->fp)) {
        file->error = Q_Errno();
        if (!result) {
            return file->error;
        }
    }

    return result;
}

/*
=================
FS_Read
=================
*/
ssize_t FS_Read(void *buf, size_t len, qhandle_t f)
{
    file_t *file = file_for_handle(f);
#if USE_ZLIB
    int ret;
#endif

    if (!file)
        return Q_ERR_BADF;

    if ((file->mode & FS_MODE_MASK) != FS_MODE_READ)
        return Q_ERR_INVAL;

    // can't continue after error
    if (file->error)
        return file->error;

    if (len > SSIZE_MAX)
        return Q_ERR_INVAL;

    if (len == 0)
        return 0;

    switch (file->type) {
    case FS_REAL:
        return read_phys_file(file, buf, len);
    case FS_PAK:
        return read_pak_file(file, buf, len);
#if USE_ZLIB
    case FS_GZ:
        ret = gzread(file->zfp, buf, len);
        if (ret < 0) {
            return Q_ERR_LIBRARY_ERROR;
        }
        return ret;
    case FS_ZIP:
        return read_zip_file(file, buf, len);
#endif
    default:
        return Q_ERR_NOSYS;
    }
}

ssize_t FS_ReadLine(qhandle_t f, char *buffer, size_t size)
{
    file_t *file = file_for_handle(f);
    char *s;
    size_t len;

    if (!file)
        return Q_ERR_BADF;

    if ((file->mode & FS_MODE_MASK) != FS_MODE_READ)
        return Q_ERR_INVAL;

    if (file->type != FS_REAL)
        return Q_ERR_NOSYS;

    do {
        s = fgets(buffer, size, file->fp);
        if (!s) {
            return ferror(file->fp) ? Q_Errno() : 0;
        }
        len = strlen(s);
    } while (len < 2);

    s[len - 1] = 0;
    return len - 1;
}

void FS_Flush(qhandle_t f)
{
    file_t *file = file_for_handle(f);

    if (!file)
        return;

    switch (file->type) {
    case FS_REAL:
        fflush(file->fp);
        break;
#if USE_ZLIB
    case FS_GZ:
        gzflush(file->zfp, Z_SYNC_FLUSH);
        break;
#endif
    default:
        break;
    }
}

/*
=================
FS_Write
=================
*/
ssize_t FS_Write(const void *buf, size_t len, qhandle_t f)
{
    file_t  *file = file_for_handle(f);
    size_t  result;

    if (!file)
        return Q_ERR_BADF;

    if ((file->mode & FS_MODE_MASK) == FS_MODE_READ)
        return Q_ERR_INVAL;

    // can't continue after error
    if (file->error)
        return file->error;

    if (len > SSIZE_MAX)
        return Q_ERR_INVAL;

    if (len == 0)
        return 0;

    switch (file->type) {
    case FS_REAL:
        result = fwrite(buf, 1, len, file->fp);
        if (result != len) {
            file->error = FS_ERR_WRITE(file->fp);
            return file->error;
        }
        break;
#if USE_ZLIB
    case FS_GZ:
        if (gzwrite(file->zfp, buf, len) == 0) {
            file->error = Q_ERR_LIBRARY_ERROR;
            return file->error;
        }
        break;
#endif
    default:
        Com_Error(ERR_FATAL, "%s: bad file type", __func__);
    }

    return len;
}

/*
============
FS_FOpenFile
============
*/
ssize_t FS_FOpenFile(const char *name, qhandle_t *f, unsigned mode)
{
    file_t *file;
    qhandle_t handle;
    ssize_t ret;

    if (!name || !f) {
        Com_Error(ERR_FATAL, "%s: NULL", __func__);
    }

    *f = 0;

    if (!fs_searchpaths) {
        return Q_ERR_AGAIN; // not yet initialized
    }

    // allocate new file handle
    file = alloc_handle(&handle);
    if (!file) {
        return Q_ERR_MFILE;
    }

    file->mode = mode;

    if ((mode & FS_MODE_MASK) == FS_MODE_READ) {
        ret = expand_open_file_read(file, name, qtrue);
    } else {
        ret = open_file_write(file, name);
    }

    if (ret >= 0) {
        *f = handle;
    }

    return ret;
}

// reading from outside of source directory is allowed, extension is optional
static qhandle_t easy_open_read(char *buf, size_t size, unsigned mode,
                                const char *dir, const char *name, const char *ext)
{
    ssize_t len;
    qhandle_t f;

    if (*name == '/') {
        // full path is given, ignore directory and extension
        len = Q_strlcpy(buf, name + 1, size);
    } else {
        // first try without extension
        len = Q_concat(buf, size, dir, name, NULL);
        if (len >= size) {
            Q_PrintError("open", Q_ERR_NAMETOOLONG);
            return 0;
        }

        // print normalized path in case of error
        FS_NormalizePath(buf, buf);

        len = FS_FOpenFile(buf, &f, mode);
        if (f) {
            return f; // succeeded
        }
        if (len != Q_ERR_NOENT) {
            goto fail; // fatal error
        }
        if (!COM_CompareExtension(buf, ext)) {
            goto fail; // name already has the extension
        }

        // now try to append extension
        len = Q_strlcat(buf, ext, size);
    }

    if (len >= size) {
        Q_PrintError("open", Q_ERR_NAMETOOLONG);
        return 0;
    }

    len = FS_FOpenFile(buf, &f, mode);
    if (f) {
        return f;
    }

fail:
    Com_Printf("Couldn't open %s: %s\n", buf, Q_ErrorString(len));
    return 0;
}

// writing to outside of destination directory is disallowed, extension is forced
static qhandle_t easy_open_write(char *buf, size_t size, unsigned mode,
                                 const char *dir, const char *name, const char *ext)
{
    char normalized[MAX_OSPATH];
    ssize_t len;
    qhandle_t f;

    // make it impossible to escape the destination directory when writing files
    len = FS_NormalizePathBuffer(normalized, name, sizeof(normalized));
    if (len >= sizeof(normalized)) {
        Q_PrintError("open", Q_ERR_NAMETOOLONG);
        return 0;
    }

    // reject empty filenames
    if (len == 0) {
        Q_PrintError("open", Q_ERR_NAMETOOSHORT);
        return 0;
    }

    // replace any bad characters with underscores to make automatic commands happy
    cleanup_path(normalized);

    // don't append the extension if name already has it
    if (!COM_CompareExtension(normalized, ext)) {
        ext = (mode & FS_FLAG_GZIP) ? "" : NULL;
    }

    len = Q_concat(buf, size, dir, normalized, ext,
                   (mode & FS_FLAG_GZIP) ? ".gz" : NULL, NULL);
    if (len >= size) {
        Q_PrintError("open", Q_ERR_NAMETOOLONG);
        return 0;
    }

    len = FS_FOpenFile(buf, &f, mode);
    if (!f) {
        goto fail1;
    }

    if (mode & FS_FLAG_GZIP) {
        len = FS_FilterFile(f);
        if (len) {
            goto fail2;
        }
    }

    return f;

fail2:
    FS_FCloseFile(f);
fail1:
    Com_EPrintf("Couldn't open %s: %s\n", buf, Q_ErrorString(len));
    return 0;
}

/*
============
FS_EasyOpenFile

Helper function for various console commands. Concatenates
the arguments, checks for path buffer overflow, and attempts
to open the file, printing an error message in case of failure.
============
*/
qhandle_t FS_EasyOpenFile(char *buf, size_t size, unsigned mode,
                          const char *dir, const char *name, const char *ext)
{
    if ((mode & FS_MODE_MASK) == FS_MODE_READ) {
        return easy_open_read(buf, size, mode, dir, name, ext);
    }

    return easy_open_write(buf, size, mode, dir, name, ext);
}

/*
============
FS_LoadFile

opens non-unique file handle as an optimization
a NULL buffer will just return the file length without loading
============
*/
ssize_t FS_LoadFileEx(const char *path, void **buffer, unsigned flags, memtag_t tag)
{
    file_t *file;
    qhandle_t f;
    byte *buf;
    ssize_t len, read;

    if (!path) {
        Com_Error(ERR_FATAL, "%s: NULL", __func__);
    }

    if (buffer) {
        *buffer = NULL;
    }

    if (!fs_searchpaths) {
        return Q_ERR_AGAIN; // not yet initialized
    }

    // allocate new file handle
    file = alloc_handle(&f);
    if (!file) {
        return Q_ERR_MFILE;
    }

    file->mode = (flags & ~FS_MODE_MASK) | FS_MODE_READ;

    // look for it in the filesystem or pack files
    len = expand_open_file_read(file, path, qfalse);
    if (len < 0) {
        return len;
    }

    // NULL buffer just checks for file existence
    if (!buffer) {
        goto done;
    }

    // sanity check file size
    if (len > MAX_LOADFILE) {
        len = Q_ERR_FBIG;
        goto done;
    }

    // allocate chunk of memory, +1 for NUL
    buf = Z_TagMalloc(len + 1, tag);

    // read entire file
    read = FS_Read(buf, len, f);
    if (read != len) {
        len = read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
        Z_Free(buf);
        goto done;
    }

    *buffer = buf;
    buf[len] = 0;

done:
    FS_FCloseFile(f);
    return len;
}

/*
================
FS_WriteFile
================
*/
qerror_t FS_WriteFile(const char *path, const void *data, size_t len)
{
    qhandle_t f;
    ssize_t write;
    qerror_t ret;

    // TODO: write to temp file perhaps?
    write = FS_FOpenFile(path, &f, FS_MODE_WRITE);
    if (!f) {
        return write;
    }

    write = FS_Write(data, len, f);
    ret = write == len ? Q_ERR_SUCCESS : write < 0 ? write : Q_ERR_FAILURE;

    FS_FCloseFile(f);
    return ret;
}

/*
============
FS_EasyWriteFile

Helper function for various console commands. Concatenates
the arguments, checks for path buffer overflow, and attempts
to write the file, printing an error message in case of failure.
============
*/
qboolean FS_EasyWriteFile(char *buf, size_t size, unsigned mode,
                          const char *dir, const char *name, const char *ext,
                          const void *data, size_t len)
{
    qhandle_t f;
    ssize_t write;
    qerror_t ret;

    // TODO: write to temp file perhaps?
    f = easy_open_write(buf, size, mode, dir, name, ext);
    if (!f) {
        return qfalse;
    }

    write = FS_Write(data, len, f);
    ret = write == len ? Q_ERR_SUCCESS : write < 0 ? write : Q_ERR_FAILURE;

    FS_FCloseFile(f);

    if (ret) {
        Com_EPrintf("Couldn't write %s: %s\n", buf, Q_ErrorString(ret));
        return qfalse;
    }

    return qtrue;
}

#if USE_CLIENT

/*
================
FS_RenameFile
================
*/
qerror_t FS_RenameFile(const char *from, const char *to)
{
    char normalized[MAX_OSPATH];
    char frompath[MAX_OSPATH];
    char topath[MAX_OSPATH];
    size_t len;

    // check from
    len = FS_NormalizePathBuffer(normalized, from, sizeof(normalized));
    if (len >= sizeof(normalized))
        return Q_ERR_NAMETOOLONG;

    if (!FS_ValidatePath(normalized))
        return Q_ERR_INVALID_PATH;

    len = Q_concat(frompath, sizeof(frompath), fs_gamedir, "/", normalized, NULL);
    if (len >= sizeof(frompath))
        return Q_ERR_NAMETOOLONG;

    // check to
    len = FS_NormalizePathBuffer(normalized, to, sizeof(normalized));
    if (len >= sizeof(normalized))
        return Q_ERR_NAMETOOLONG;

    if (!FS_ValidatePath(normalized))
        return Q_ERR_INVALID_PATH;

    len = Q_concat(topath, sizeof(topath), fs_gamedir, "/", normalized, NULL);
    if (len >= sizeof(topath))
        return Q_ERR_NAMETOOLONG;

    // rename it
    if (rename(frompath, topath))
        return Q_Errno();

    return Q_ERR_SUCCESS;
}

#endif // USE_CLIENT

/*
================
FS_FPrintf
================
*/
ssize_t FS_FPrintf(qhandle_t f, const char *format, ...)
{
    va_list argptr;
    char string[MAXPRINTMSG];
    size_t len;

    va_start(argptr, format);
    len = Q_vsnprintf(string, sizeof(string), format, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        return Q_ERR_STRING_TRUNCATED;
    }

    return FS_Write(string, len, f);
}

// references pack_t instance
static pack_t *pack_get(pack_t *pack)
{
    pack->refcount++;
    return pack;
}

// dereferences pack_t instance
static void pack_put(pack_t *pack)
{
    if (!pack) {
        return;
    }
    if (!pack->refcount) {
        Com_Error(ERR_FATAL, "%s: refcount already zero", __func__);
    }
    if (!--pack->refcount) {
        FS_DPrintf("Freeing packfile %s\n", pack->filename);
        fclose(pack->fp);
        Z_Free(pack);
    }
}

// allocates pack_t instance along with filenames and hashes in one chunk of memory
static pack_t *pack_alloc(FILE *fp, filetype_t type, const char *name,
                          unsigned num_files, size_t names_len)
{
    pack_t *pack;
    unsigned hash_size;
    size_t len;

    hash_size = npot32(num_files / 3);

    len = strlen(name) + 1;
    pack = FS_Malloc(sizeof(pack_t) +
                     num_files * sizeof(packfile_t) +
                     hash_size * sizeof(packfile_t *) +
                     len + names_len);
    pack->type = type;
    pack->refcount = 0;
    pack->fp = fp;
    pack->num_files = num_files;
    pack->hash_size = hash_size;
    pack->files = (packfile_t *)(pack + 1);
    pack->file_hash = (packfile_t **)(pack->files + num_files);
    pack->filename = (char *)(pack->file_hash + hash_size);
    pack->names = pack->filename + len;
    memcpy(pack->filename, name, len);
    memset(pack->file_hash, 0, hash_size * sizeof(packfile_t *));

    return pack;
}

// normalizes and inserts the filename into hash table
static void pack_hash_file(pack_t *pack, packfile_t *file)
{
    unsigned hash;

    file->namelen = FS_NormalizePath(file->name, file->name);

    hash = FS_HashPath(file->name, pack->hash_size);
    file->hash_next = pack->file_hash[hash];
    pack->file_hash[hash] = file;
}

// Loads the header and directory, adding the files at the beginning
// of the list so they override previous pack files.
static pack_t *load_pak_file(const char *packfile)
{
    dpackheader_t   header;
    packfile_t      *file;
    dpackfile_t     *dfile;
    unsigned        i, num_files;
    char            *name;
    size_t          len, names_len;
    pack_t          *pack;
    FILE            *fp;
    dpackfile_t     info[MAX_FILES_IN_PACK];

    fp = fopen(packfile, "rb");
    if (!fp) {
        Com_Printf("Couldn't open %s: %s\n", packfile, strerror(errno));
        return NULL;
    }

    if (fread(&header, 1, sizeof(header), fp) != sizeof(header)) {
        Com_Printf("Reading header failed on %s\n", packfile);
        goto fail;
    }

    if (LittleLong(header.ident) != IDPAKHEADER) {
        Com_Printf("%s is not a 'PACK' file\n", packfile);
        goto fail;
    }

    header.dirlen = LittleLong(header.dirlen);
    if (header.dirlen > INT_MAX || header.dirlen % sizeof(dpackfile_t)) {
        Com_Printf("%s has bad directory length\n", packfile);
        goto fail;
    }

    num_files = header.dirlen / sizeof(dpackfile_t);
    if (num_files < 1) {
        Com_Printf("%s has no files\n", packfile);
        goto fail;
    }
    if (num_files > MAX_FILES_IN_PACK) {
        Com_Printf("%s has too many files: %u > %u\n", packfile, num_files, MAX_FILES_IN_PACK);
        goto fail;
    }

    header.dirofs = LittleLong(header.dirofs);
    if (header.dirofs > LONG_MAX - header.dirlen) {
        Com_Printf("%s has bad directory offset\n", packfile);
        goto fail;
    }
    if (fseek(fp, (long)header.dirofs, SEEK_SET)) {
        Com_Printf("Seeking to directory failed on %s\n", packfile);
        goto fail;
    }
    if (fread(info, 1, header.dirlen, fp) != header.dirlen) {
        Com_Printf("Reading directory failed on %s\n", packfile);
        goto fail;
    }

    names_len = 0;
    for (i = 0, dfile = info; i < num_files; i++, dfile++) {
        dfile->filepos = LittleLong(dfile->filepos);
        dfile->filelen = LittleLong(dfile->filelen);
        if (dfile->filelen > INT_MAX || dfile->filepos > INT_MAX - dfile->filelen) {
            Com_Printf("%s has bad directory structure\n", packfile);
            goto fail;
        }
        dfile->name[sizeof(dfile->name) - 1] = 0;
        names_len += strlen(dfile->name) + 1;
    }

// allocate the pack
    pack = pack_alloc(fp, FS_PAK, packfile, num_files, names_len);

// parse the directory
    file = pack->files;
    name = pack->names;
    for (i = 0, dfile = info; i < num_files; i++, dfile++) {
        len = strlen(dfile->name) + 1;

        file->name = memcpy(name, dfile->name, len);
        name += len;

        file->filepos = dfile->filepos;
        file->filelen = dfile->filelen;
#if USE_ZLIB
        file->coherent = qtrue;
#endif

        pack_hash_file(pack, file);
        file++;
    }

    FS_DPrintf("%s: %u files, %u hash\n",
               packfile, pack->num_files, pack->hash_size);

    return pack;

fail:
    fclose(fp);
    return NULL;
}

#if USE_ZLIB

// Locate the central directory of a zipfile (at the end, just before the global comment)
static size_t search_central_header(FILE *fp)
{
    size_t file_size, back_read;
    size_t max_back = 0xffff; // maximum size of global comment
    byte buf[ZIP_BUFREADCOMMENT + 4];
    long ret;

    if (fseek(fp, 0, SEEK_END) == -1)
        return 0;

    ret = ftell(fp);
    if (ret == -1)
        return 0;
    file_size = (size_t)ret;
    if (max_back > file_size)
        max_back = file_size;

    back_read = 4;
    while (back_read < max_back) {
        size_t i, read_size, read_pos;

        if (back_read + ZIP_BUFREADCOMMENT > max_back)
            back_read = max_back;
        else
            back_read += ZIP_BUFREADCOMMENT;

        read_pos = file_size - back_read;

        read_size = back_read;
        if (read_size > ZIP_BUFREADCOMMENT + 4)
            read_size = ZIP_BUFREADCOMMENT + 4;

        if (fseek(fp, (long)read_pos, SEEK_SET) == -1)
            break;
        if (fread(buf, 1, read_size, fp) != read_size)
            break;

        i = read_size - 4;
        do {
            // check the magic
            if (LittleLongMem(buf + i) == ZIP_ENDHEADERMAGIC)
                return read_pos + i;
        } while (i--);
    }

    return 0;
}

// Get Info about the current file in the zipfile, with internal only info
static size_t get_file_info(FILE *fp, size_t pos, packfile_t *file, size_t *len, size_t remaining)
{
    size_t name_size, xtra_size, comm_size;
    size_t comp_len, file_len, file_pos;
    unsigned comp_mtd;
    byte header[ZIP_SIZECENTRALDIRITEM]; // we can't use a struct here because of packing

    *len = 0;

    if (pos > LONG_MAX)
        return 0;
    if (fseek(fp, (long)pos, SEEK_SET) == -1)
        return 0;
    if (fread(header, 1, sizeof(header), fp) != sizeof(header))
        return 0;

    // check the magic
    if (LittleLongMem(&header[0]) != ZIP_CENTRALHEADERMAGIC)
        return 0;

    comp_mtd = LittleShortMem(&header[10]);
    comp_len = LittleLongMem(&header[20]);
    file_len = LittleLongMem(&header[24]);
    name_size = LittleShortMem(&header[28]);
    xtra_size = LittleShortMem(&header[30]);
    comm_size = LittleShortMem(&header[32]);
    file_pos = LittleLongMem(&header[42]);

    if (file_len > LONG_MAX)
        return 0;
    if (comp_len > LONG_MAX || file_pos > LONG_MAX - comp_len)
        return 0;

    if (!file_len || !comp_len) {
        goto skip; // skip directories and empty files
    }
    if (!comp_mtd) {
        if (file_len != comp_len) {
            FS_DPrintf("%s: skipping file stored with file_len != comp_len\n", __func__);
            goto skip;
        }
    } else if (comp_mtd != Z_DEFLATED) {
        FS_DPrintf("%s: skipping file compressed with unknown method\n", __func__);
        goto skip;
    }
    if (!name_size) {
        FS_DPrintf("%s: skipping file with empty name\n", __func__);
        goto skip;
    }
    if (name_size >= MAX_QPATH) {
        FS_DPrintf("%s: skipping file with oversize name\n", __func__);
        goto skip;
    }

    // fill in the info
    if (file) {
        if (name_size >= remaining)
            return 0; // directory changed on disk?
        file->compmtd = comp_mtd;
        file->complen = comp_len;
        file->filelen = file_len;
        file->filepos = file_pos;
        if (fread(file->name, 1, name_size, fp) != name_size)
            return 0;
        file->name[name_size] = 0;
    }

    *len = name_size + 1;

skip:
    return ZIP_SIZECENTRALDIRITEM + name_size + xtra_size + comm_size;
}

static pack_t *load_zip_file(const char *packfile)
{
    packfile_t      *file;
    char            *name;
    size_t          len, names_len;
    unsigned        i, num_disk, num_disk_cd, num_files, num_files_cd;
    size_t          header_pos, central_ofs, central_size, central_end;
    size_t          extra_bytes, ofs;
    pack_t          *pack;
    FILE            *fp;
    byte            header[ZIP_SIZECENTRALHEADER];

    fp = fopen(packfile, "rb");
    if (!fp) {
        Com_Printf("Couldn't open %s: %s\n", packfile, strerror(errno));
        return NULL;
    }

    header_pos = search_central_header(fp);
    if (!header_pos) {
        Com_Printf("No central header found in %s\n", packfile);
        goto fail2;
    }
    if (fseek(fp, (long)header_pos, SEEK_SET) == -1) {
        Com_Printf("Couldn't seek to central header in %s\n", packfile);
        goto fail2;
    }
    if (fread(header, 1, sizeof(header), fp) != sizeof(header)) {
        Com_Printf("Reading central header failed on %s\n", packfile);
        goto fail2;
    }

    num_disk = LittleShortMem(&header[4]);
    num_disk_cd = LittleShortMem(&header[6]);
    num_files = LittleShortMem(&header[8]);
    num_files_cd = LittleShortMem(&header[10]);
    if (num_files_cd != num_files || num_disk_cd != 0 || num_disk != 0) {
        Com_Printf("%s is an unsupported multi-part archive\n", packfile);
        goto fail2;
    }
    if (num_files < 1) {
        Com_Printf("%s has no files\n", packfile);
        goto fail2;
    }
    if (num_files > ZIP_MAXFILES) {
        Com_Printf("%s has too many files: %u > %u\n", packfile, num_files, ZIP_MAXFILES);
        goto fail2;
    }

    central_size = LittleLongMem(&header[12]);
    central_ofs = LittleLongMem(&header[16]);
    central_end = central_ofs + central_size;
    if (central_end > header_pos || central_end < central_ofs) {
        Com_Printf("%s has bad central directory offset\n", packfile);
        goto fail2;
    }

// non-zero for sfx?
    extra_bytes = header_pos - central_end;
    if (extra_bytes) {
        Com_Printf("%s has %"PRIz" extra bytes at the beginning, funny sfx archive?\n",
                   packfile, extra_bytes);
    }

// parse the directory
    num_files = 0;
    names_len = 0;
    header_pos = central_ofs + extra_bytes;
    for (i = 0; i < num_files_cd; i++) {
        ofs = get_file_info(fp, header_pos, NULL, &len, 0);
        if (!ofs) {
            Com_Printf("%s has bad central directory structure (pass %d)\n", packfile, 1);
            goto fail2;
        }
        header_pos += ofs;

        if (len) {
            names_len += len;
            num_files++;
        }
    }

    if (!num_files) {
        Com_Printf("%s has no valid files\n", packfile);
        goto fail2;
    }

// allocate the pack
    pack = pack_alloc(fp, FS_ZIP, packfile, num_files, names_len);

// parse the directory
    file = pack->files;
    name = pack->names;
    header_pos = central_ofs + extra_bytes;
    for (i = 0; i < num_files_cd; i++) {
        if (!num_files)
            break;
        file->name = name;
        ofs = get_file_info(fp, header_pos, file, &len, names_len);
        if (!ofs) {
            Com_Printf("%s has bad central directory structure (pass %d)\n", packfile, 2);
            goto fail1; // directory changed on disk?
        }
        header_pos += ofs;

        if (len) {
            // fix absolute position
            file->filepos += extra_bytes;
            file->coherent = qfalse;

            pack_hash_file(pack, file);

            // advance pointers, decrement counters
            file++;
            num_files--;

            name += len;
            names_len -= len;
        }
    }

    FS_DPrintf("%s: %u files, %u skipped, %u hash\n",
               packfile, pack->num_files, num_files_cd - pack->num_files, pack->hash_size);

    return pack;

fail1:
    Z_Free(pack);
fail2:
    fclose(fp);
    return NULL;
}
#endif

// this is complicated as we need pakXX.pak loaded first,
// sorted in numerical order, then the rest of the paks in
// alphabetical order, e.g. pak0.pak, pak2.pak, pak17.pak, abc.pak...
static int pakcmp(const void *p1, const void *p2)
{
    char *s1 = *(char **)p1;
    char *s2 = *(char **)p2;

    if (!Q_stricmpn(s1, "pak", 3)) {
        if (!Q_stricmpn(s2, "pak", 3)) {
            unsigned long n1 = strtoul(s1 + 3, &s1, 10);
            unsigned long n2 = strtoul(s2 + 3, &s2, 10);
            if (n1 > n2) {
                return 1;
            }
            if (n1 < n2) {
                return -1;
            }
            goto alphacmp;
        }
        return -1;
    }
    if (!Q_stricmpn(s2, "pak", 3)) {
        return 1;
    }

alphacmp:
    return Q_stricmp(s1, s2);
}

// sets fs_gamedir, adds the directory to the head of the path,
// then loads and adds pak*.pak, then anything else in alphabethical order.
static void q_printf(2, 3) add_game_dir(unsigned mode, const char *fmt, ...)
{
    va_list         argptr;
    searchpath_t    *search;
    pack_t          *pack;
    void            *files[MAX_LISTED_FILES];
    int             i, count;
    char            path[MAX_OSPATH];
    size_t          len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(fs_gamedir, sizeof(fs_gamedir), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(fs_gamedir)) {
        Com_EPrintf("%s: refusing oversize path\n", __func__);
        return;
    }

#ifdef _WIN32
    FS_ReplaceSeparators(fs_gamedir, '/');
#endif

    // add the directory to the search path
    search = FS_Malloc(sizeof(searchpath_t) + len);
    search->mode = mode;
    search->pack = NULL;
    memcpy(search->filename, fs_gamedir, len + 1);
    search->next = fs_searchpaths;
    fs_searchpaths = search;

#if USE_ZLIB
#define PAK_EXT  ".pak;.pkz"
#else
#define PAK_EXT  ".pak"
#endif

    // add any pack files
    count = 0;
    Sys_ListFiles_r(fs_gamedir, PAK_EXT, 0, 0, &count, files, 0);
    if (!count) {
        return;
    }

    qsort(files, count, sizeof(files[0]), pakcmp);

    for (i = 0; i < count; i++) {
        len = Q_concat(path, sizeof(path), fs_gamedir, "/", files[i], NULL);
        if (len >= sizeof(path)) {
            Com_EPrintf("%s: refusing oversize path\n", __func__);
            continue;
        }
#if USE_ZLIB
        // FIXME: guess packfile type by contents instead?
        if (len > 4 && !Q_stricmp(path + len - 4, ".pkz"))
            pack = load_zip_file(path);
        else
#endif
            pack = load_pak_file(path);
        if (!pack)
            continue;
        search = FS_Malloc(sizeof(searchpath_t));
        search->mode = mode;
        search->filename[0] = 0;
        search->pack = pack_get(pack);
        search->next = fs_searchpaths;
        fs_searchpaths = search;
    }

    for (i = 0; i < count; i++) {
        Z_Free(files[i]);
    }
}

/*
=================
FS_CopyInfo
=================
*/
file_info_t *FS_CopyInfo(const char *name, size_t size, time_t ctime, time_t mtime)
{
    file_info_t *out;
    size_t len;

    if (!name) {
        return NULL;
    }

    len = strlen(name);
    out = FS_Mallocz(sizeof(*out) + len);
    out->size = size;
    out->ctime = ctime;
    out->mtime = mtime;
    memcpy(out->name, name, len + 1);

    return out;
}

void **FS_CopyList(void **list, int count)
{
    void **out;
    int i;

    if (!count) {
        return NULL;
    }

    out = FS_Malloc(sizeof(void *) * (count + 1));
    for (i = 0; i < count; i++) {
        out[i] = list[i];
    }
    out[i] = NULL;

    return out;
}

qboolean FS_WildCmp(const char *filter, const char *string)
{
    do {
        if (Com_WildCmpEx(filter, string, ';', qtrue)) {
            return qtrue;
        }
        filter = strchr(filter, ';');
        if (filter) filter++;
    } while (filter);

    return qfalse;
}

qboolean FS_ExtCmp(const char *ext, const char *name)
{
    int        c1, c2;
    const char *e, *n, *l;

    if (!name[0] || !ext[0]) {
        return qfalse;
    }

    for (l = name; l[1]; l++)
        ;

    for (e = ext; e[1]; e++)
        ;

rescan:
    n = l;
    do {
        c1 = *e--;
        c2 = *n--;

        if (c1 == ';') {
            break; // matched
        }

        if (c1 != c2) {
            c1 = Q_tolower(c1);
            c2 = Q_tolower(c2);
            if (c1 != c2) {
                while (e > ext) {
                    c1 = *e--;
                    if (c1 == ';') {
                        goto rescan;
                    }
                }
                return qfalse;
            }
        }
        if (n < name) {
            return qfalse;
        }
    } while (e >= ext);

    return qtrue;
}

static int infocmp(const void *p1, const void *p2)
{
    file_info_t *n1 = *(file_info_t **)p1;
    file_info_t *n2 = *(file_info_t **)p2;

    return FS_pathcmp(n1->name, n2->name);
}

static int alphacmp(const void *p1, const void *p2)
{
    char *s1 = *(char **)p1;
    char *s2 = *(char **)p2;

    return FS_pathcmp(s1, s2);
}

/*
=================
FS_ListFiles
=================
*/
void **FS_ListFiles(const char *path,
                    const char *filter,
                    unsigned   flags,
                    int        *count_p)
{
    searchpath_t    *search;
    packfile_t      *file;
    void            *files[MAX_LISTED_FILES], *info;
    int             i, j, count, total;
    char            normalized[MAX_OSPATH], buffer[MAX_OSPATH];
    void            **list;
    size_t          len, pathlen;
    char            *s, *p;
    int             valid;

    count = 0;
    valid = PATH_NOT_CHECKED;

    if (!path) {
        path = "";
        pathlen = 0;
    } else {
        // normalize the path
        pathlen = FS_NormalizePathBuffer(normalized, path, sizeof(normalized));
        if (pathlen >= sizeof(normalized)) {
            goto fail;
        }

        path = normalized;
    }

    // can't mix directory search with other flags
    if ((flags & FS_SEARCH_DIRSONLY) && (flags & FS_SEARCH_MASK & ~FS_SEARCH_DIRSONLY)) {
        goto fail;
    }

    for (search = fs_searchpaths; search; search = search->next) {
        if (flags & FS_PATH_MASK) {
            if ((flags & search->mode & FS_PATH_MASK) == 0) {
                continue;
            }
        }
        if (search->pack) {
            if ((flags & FS_TYPE_MASK) == FS_TYPE_REAL) {
                continue; // don't search in paks
            }

            for (i = 0; i < search->pack->num_files; i++) {
                file = &search->pack->files[i];
                s = file->name;

                // check path
                if (pathlen) {
                    if (file->namelen < pathlen) {
                        continue;
                    }
                    if (FS_pathcmpn(s, path, pathlen)) {
                        continue;
                    }
                    if (s[pathlen] != '/') {
                        continue;   // matched prefix must be a directory
                    }
                    if (flags & FS_SEARCH_BYFILTER) {
                        s += pathlen + 1;
                    }
                } else if (path == normalized) {
                    if (!(flags & FS_SEARCH_DIRSONLY) && strchr(s, '/')) {
                        continue;   // must be a file in the root directory
                    }
                }

                // check filter
                if (filter) {
                    if (flags & FS_SEARCH_BYFILTER) {
                        if (!FS_WildCmp(filter, s)) {
                            continue;
                        }
                    } else {
                        if (!FS_ExtCmp(filter, s)) {
                            continue;
                        }
                    }
                }

                // copy name off
                if (flags & (FS_SEARCH_DIRSONLY | FS_SEARCH_STRIPEXT)) {
                    s = strcpy(buffer, s);
                }

                // hacky directory search support for pak files
                if (flags & FS_SEARCH_DIRSONLY) {
                    p = s;
                    if (pathlen) {
                        p += pathlen + 1;
                    }
                    p = strchr(p, '/');
                    if (!p) {
                        continue;   // does not have directory component
                    }
                    *p = 0;
                    for (j = 0; j < count; j++) {
                        if (!FS_pathcmp(files[j], s)) {
                            break;
                        }
                    }
                    if (j != count) {
                        continue;   // already listed this directory
                    }
                }

                // strip path
                if (!(flags & FS_SEARCH_SAVEPATH)) {
                    s = COM_SkipPath(s);
                }

                // strip extension
                if (flags & FS_SEARCH_STRIPEXT) {
                    *COM_FileExtension(s) = 0;
                }

                if (!*s) {
                    continue;
                }

                // copy info off
                if (flags & FS_SEARCH_EXTRAINFO) {
                    info = FS_CopyInfo(s, file->filelen, 0, 0);
                } else {
                    info = FS_CopyString(s);
                }

                files[count++] = info;

                if (count >= MAX_LISTED_FILES) {
                    break;
                }
            }
        } else {
            if ((flags & FS_TYPE_MASK) == FS_TYPE_PAK) {
                continue; // don't search in filesystem
            }

            len = strlen(search->filename);

            if (pathlen) {
                if (len + pathlen + 1 >= MAX_OSPATH) {
                    continue;
                }
                if (valid == PATH_NOT_CHECKED) {
                    valid = FS_ValidatePath(path);
                }
                if (valid == PATH_INVALID) {
                    continue;
                }
                s = memcpy(buffer, search->filename, len);
                s[len++] = '/';
                memcpy(s + len, path, pathlen + 1);
            } else {
                s = search->filename;
            }

            if (flags & FS_SEARCH_BYFILTER) {
                len += pathlen + 1;
            }

            Sys_ListFiles_r(s, filter, flags, len, &count, files, 0);
        }

        if (count >= MAX_LISTED_FILES) {
            break;
        }
    }

    if (!count) {
fail:
        if (count_p) {
            *count_p = 0;
        }
        return NULL;
    }

    if (flags & FS_SEARCH_EXTRAINFO) {
        // TODO
        qsort(files, count, sizeof(files[0]), infocmp);
        total = count;
    } else {
        // sort alphabetically
        qsort(files, count, sizeof(files[0]), alphacmp);

        // remove duplicates
        total = 1;
        for (i = 1; i < count; i++) {
            if (!FS_pathcmp(files[i - 1], files[i])) {
                Z_Free(files[i - 1]);
                files[i - 1] = NULL;
            } else {
                total++;
            }
        }
    }

    list = FS_Malloc(sizeof(void *) * (total + 1));

    total = 0;
    for (i = 0; i < count; i++) {
        if (files[i]) {
            list[total++] = files[i];
        }
    }
    list[total] = NULL;

    if (count_p) {
        *count_p = total;
    }

    return list;
}

/*
=================
FS_FreeList
=================
*/
void FS_FreeList(void **list)
{
    void **p;

    if (!list) {
        return;
    }

    for (p = list; *p; p++) {
        Z_Free(*p);
    }

    Z_Free(list);
}

void FS_File_g(const char *path, const char *ext, unsigned flags, genctx_t *ctx)
{
    int i, numFiles;
    void **list;
    char *s;

    list = FS_ListFiles(path, ext, flags, &numFiles);
    if (!list) {
        return;
    }

    for (i = 0; i < numFiles; i++) {
        s = list[i];
        if (ctx->count < ctx->size && !strncmp(s, ctx->partial, ctx->length)) {
            ctx->matches[ctx->count++] = s;
        } else {
            Z_Free(s);
        }
    }

    Z_Free(list);
}

static void print_file_list(const char *path, const char *ext, unsigned flags)
{
    void    **list;
    int     i, listed, total;

    list = FS_ListFiles(path, ext, flags, &total);

    // don't list too many files to avoid console spam
    listed = total > 128 ? 128 : total;
    for (i = 0; i < listed; i++) {
        Com_Printf("%s\n", (char *)list[i]);
    }

    FS_FreeList(list);

    if (listed == total) {
        Com_Printf("%i files listed\n", listed);
    } else {
        Com_Printf("%i files listed (%d files more)\n", listed, total - listed);
    }
}

/*
============
FS_FDir_f
============
*/
static void FS_FDir_f(void)
{
    unsigned flags;
    char *filter;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <filter> [full_path]\n", Cmd_Argv(0));
        return;
    }

    filter = Cmd_Argv(1);

    flags = FS_SEARCH_BYFILTER;
    if (Cmd_Argc() > 2) {
        flags |= FS_SEARCH_SAVEPATH;
    }

    print_file_list(NULL, filter, flags);
}

/*
============
FS_Dir_f
============
*/
static void FS_Dir_f(void)
{
    char    *path, *ext;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <directory> [.extension]\n", Cmd_Argv(0));
        return;
    }

    path = Cmd_Argv(1);
    if (Cmd_Argc() > 2) {
        ext = Cmd_Argv(2);
    } else {
        ext = NULL;
    }

    print_file_list(path, ext, 0);
}

/*
============
FS_WhereIs_f

Verbosely looks up a filename with exactly the same logic as expand_open_file_read.
============
*/
static void FS_WhereIs_f(void)
{
    char            normalized[MAX_OSPATH], fullpath[MAX_OSPATH];
    searchpath_t    *search;
    pack_t          *pak;
    packfile_t      *entry;
    symlink_t       *link;
    unsigned        hash;
    file_info_t     info;
    qerror_t        ret;
    int             total, valid;
    size_t          len, namelen;
    qboolean        report_all;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <path> [all]\n", Cmd_Argv(0));
        return;
    }

// normalize path
    namelen = FS_NormalizePathBuffer(normalized, Cmd_Argv(1), MAX_OSPATH);
    if (namelen >= MAX_OSPATH) {
        Com_Printf("Refusing to lookup oversize path.\n");
        return;
    }

// expand hard symlinks
    link = expand_links(&fs_hard_links, normalized, &namelen);
    if (link) {
        if (namelen >= MAX_OSPATH) {
            Com_Printf("Oversize symbolic link ('%s --> '%s').\n",
                       link->name, link->target);
            return;
        }

        Com_Printf("Symbolic link ('%s' --> '%s') in effect.\n",
                   link->name, link->target);
    }

    report_all = Cmd_Argc() >= 3;
    total = 0;
    link = NULL;

// reject empty paths
    if (namelen == 0) {
        Com_Printf("Refusing to lookup empty path.\n");
        return;
    }

recheck:

// warn about non-standard path length
    if (namelen >= MAX_QPATH) {
        Com_Printf("Not searching for '%s' in pack files "
                   "since path length exceedes %d characters.\n",
                   normalized, MAX_QPATH - 1);
    }

    hash = FS_HashPath(normalized, 0);

    valid = PATH_NOT_CHECKED;

// search through the path, one element at a time
    for (search = fs_searchpaths; search; search = search->next) {
        // is the element a pak file?
        if (search->pack) {
            // don't bother searching in paks if length exceedes MAX_QPATH
            if (namelen >= MAX_QPATH) {
                continue;
            }
            // look through all the pak file elements
            pak = search->pack;
            entry = pak->file_hash[hash & (pak->hash_size - 1)];
            for (; entry; entry = entry->hash_next) {
                if (entry->namelen != namelen) {
                    continue;
                }
                if (!FS_pathcmp(entry->name, normalized)) {
                    // found it!
                    Com_Printf("%s/%s (%"PRIz" bytes)\n", pak->filename,
                               normalized, entry->filelen);
                    if (!report_all) {
                        return;
                    }
                    total++;
                }
            }
        } else {
            if (valid == PATH_NOT_CHECKED) {
                valid = FS_ValidatePath(normalized);
                if (valid == PATH_INVALID) {
                    // warn about invalid path
                    Com_Printf("Not searching for '%s' in physical file "
                               "system since path contains invalid characters.\n",
                               normalized);
                }
            }
            if (valid == PATH_INVALID) {
                continue;
            }

            // check a file in the directory tree
            len = Q_concat(fullpath, MAX_OSPATH,
                           search->filename, "/", normalized, NULL);
            if (len >= MAX_OSPATH) {
                Com_WPrintf("Full path length '%s/%s' exceeded %d characters.\n",
                            search->filename, normalized, MAX_OSPATH - 1);
                if (!report_all) {
                    return;
                }
                continue;
            }

            ret = get_path_info(fullpath, &info);

#ifndef _WIN32
            if (ret == Q_ERR_NOENT && valid == PATH_MIXED_CASE) {
                Q_strlwr(fullpath + strlen(search->filename) + 1);
                ret = get_path_info(fullpath, &info);
                if (ret == Q_ERR_SUCCESS)
                    Com_Printf("Physical path found after converting to lower case.\n");
            }
#endif

            if (ret == Q_ERR_SUCCESS) {
                Com_Printf("%s (%"PRIz" bytes)\n", fullpath, info.size);
                if (!report_all) {
                    return;
                }
                total++;
            } else if (ret != Q_ERR_NOENT) {
                Com_EPrintf("Couldn't get info on '%s': %s\n",
                            fullpath, Q_ErrorString(ret));
                if (!report_all) {
                    return;
                }
            }
        }
    }

    if ((total == 0 || report_all) && link == NULL) {
        // expand soft symlinks
        link = expand_links(&fs_soft_links, normalized, &namelen);
        if (link) {
            if (namelen >= MAX_OSPATH) {
                Com_Printf("Oversize symbolic link ('%s --> '%s').\n",
                           link->name, link->target);
                return;
            }

            Com_Printf("Symbolic link ('%s' --> '%s') in effect.\n",
                       link->name, link->target);
            goto recheck;
        }
    }

    if (total) {
        Com_Printf("%d instances of %s\n", total, normalized);
    } else {
        Com_Printf("%s was not found\n", normalized);
    }
}

/*
============
FS_Path_f
============
*/
static void FS_Path_f(void)
{
    searchpath_t *s;
    int numFilesInPAK = 0;
#if USE_ZLIB
    int numFilesInZIP = 0;
#endif
    Com_Printf("Current search path:\n");
    for (s = fs_searchpaths; s; s = s->next) {
        if (s->pack) {
#if USE_ZLIB
            if (s->pack->type == FS_ZIP)
                numFilesInZIP += s->pack->num_files;
            else
#endif
                numFilesInPAK += s->pack->num_files;
            Com_Printf("%s (%i files)\n", s->pack->filename, s->pack->num_files);
        } else {
            Com_Printf("%s\n", s->filename);
        }
    }

    if (numFilesInPAK) {
        Com_Printf("%i files in PAK files\n", numFilesInPAK);
    }

#if USE_ZLIB
    if (numFilesInZIP) {
        Com_Printf("%i files in PKZ files\n", numFilesInZIP);
    }
#endif
}

#ifdef _DEBUG
/*
================
FS_Stats_f
================
*/
static void FS_Stats_f(void)
{
    searchpath_t *path;
    pack_t *pack, *maxpack = NULL;
    packfile_t *file, *max = NULL;
    int i;
    int len, maxLen = 0;
    int totalHashSize, totalLen;

    totalHashSize = totalLen = 0;
    for (path = fs_searchpaths; path; path = path->next) {
        if (!(pack = path->pack)) {
            continue;
        }
        for (i = 0; i < pack->hash_size; i++) {
            if (!(file = pack->file_hash[i])) {
                continue;
            }
            len = 0;
            for (; file; file = file->hash_next) {
                len++;
            }
            if (maxLen < len) {
                max = pack->file_hash[i];
                maxpack = pack;
                maxLen = len;
            }
            totalLen += len;
            totalHashSize++;
        }
        //totalHashSize += pack->hash_size;
    }

    Com_Printf("Total calls to open_file_read: %d\n", fs_count_read);
    Com_Printf("Total path comparsions: %d\n", fs_count_strcmp);
    Com_Printf("Total calls to open_from_disk: %d\n", fs_count_open);
    Com_Printf("Total mixed-case reopens: %d\n", fs_count_strlwr);

    if (!totalHashSize) {
        Com_Printf("No stats to display\n");
        return;
    }

    Com_Printf("Maximum hash bucket length is %d, average is %.2f\n", maxLen, (float)totalLen / totalHashSize);
    if (max) {
        Com_Printf("Dumping longest bucket (%s):\n", maxpack->filename);
        for (file = max; file; file = file->hash_next) {
            Com_Printf("%s\n", file->name);
        }
    }
}
#endif // _DEBUG

static void FS_Link_g(genctx_t *ctx)
{
    list_t *list;
    symlink_t *link;

    if (!strncmp(Cmd_Argv(ctx->argnum - 1), "soft", 4))
        list = &fs_soft_links;
    else
        list = &fs_hard_links;

    FOR_EACH_SYMLINK(link, list) {
        if (!Prompt_AddMatch(ctx, link->name)) {
            break;
        }
    }
}

static void FS_Link_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        FS_Link_g(ctx);
    }
}

static void free_all_links(list_t *list)
{
    symlink_t *link, *next;

    FOR_EACH_SYMLINK_SAFE(link, next, list) {
        Z_Free(link->target);
        Z_Free(link);
    }

    List_Init(list);
}

static void FS_UnLink_f(void)
{
    static const cmd_option_t options[] = {
        { "a", "all", "delete all links" },
        { "h", "help", "display this message" },
        { NULL }
    };
    list_t *list;
    symlink_t *link;
    char *name;
    int c;

    if (!strncmp(Cmd_Argv(0), "soft", 4))
        list = &fs_soft_links;
    else
        list = &fs_hard_links;

    while ((c = Cmd_ParseOptions(options)) != -1) {
        switch (c) {
        case 'h':
            Cmd_PrintUsage(options, "<name>");
            Com_Printf("Deletes a symbolic link with the specified name.");
            Cmd_PrintHelp(options);
            return;
        case 'a':
            free_all_links(list);
            Com_Printf("Deleted all symbolic links.\n");
            return;
        default:
            return;
        }
    }

    name = cmd_optarg;
    if (!name[0]) {
        Com_Printf("Missing name argument.\n");
        Cmd_PrintHint();
        return;
    }

    FOR_EACH_SYMLINK(link, list) {
        if (!FS_pathcmp(link->name, name)) {
            List_Remove(&link->entry);
            Z_Free(link->target);
            Z_Free(link);
            return;
        }
    }

    Com_Printf("Symbolic link '%s' does not exist.\n", name);
}

static void FS_Link_f(void)
{
    int argc, count;
    list_t *list;
    symlink_t *link;
    size_t namelen, targlen;
    char name[MAX_OSPATH];
    char target[MAX_OSPATH];

    if (!strncmp(Cmd_Argv(0), "soft", 4))
        list = &fs_soft_links;
    else
        list = &fs_hard_links;

    argc = Cmd_Argc();
    if (argc == 1) {
        count = 0;
        FOR_EACH_SYMLINK(link, list) {
            Com_Printf("%s --> %s\n", link->name, link->target);
            count++;
        }
        Com_Printf("------------------\n"
                   "%d symbolic link%s listed.\n", count, count == 1 ? "" : "s");
        return;
    }

    if (argc != 3) {
        Com_Printf("Usage: %s <name> <target>\n"
                   "Creates symbolic link to target with the specified name.\n"
                   "Virtual quake paths are accepted.\n"
                   "Links are effective only for reading.\n",
                   Cmd_Argv(0));
        return;
    }

    namelen = FS_NormalizePathBuffer(name, Cmd_Argv(1), sizeof(name));
    if (namelen == 0 || namelen >= sizeof(name)) {
        Com_Printf("Invalid symbolic link name.\n");
        return;
    }

    targlen = FS_NormalizePathBuffer(target, Cmd_Argv(2), sizeof(target));
    if (targlen == 0 || targlen >= sizeof(target)) {
        Com_Printf("Invalid symbolic link target.\n");
        return;
    }

    // search for existing link with this name
    FOR_EACH_SYMLINK(link, list) {
        if (!FS_pathcmp(link->name, name)) {
            Z_Free(link->target);
            goto update;
        }
    }

    // create new link
    link = FS_Malloc(sizeof(*link) + namelen);
    memcpy(link->name, name, namelen + 1);
    link->namelen = namelen;
    List_Append(list, &link->entry);

update:
    link->target = FS_CopyString(target);
    link->targlen = targlen;
}

static void free_search_path(searchpath_t *path)
{
    pack_put(path->pack);
    Z_Free(path);
}

static void free_all_paths(void)
{
    searchpath_t *path, *next;

    for (path = fs_searchpaths; path; path = next) {
        next = path->next;
        free_search_path(path);
    }

    fs_searchpaths = NULL;
}

static void free_game_paths(void)
{
    searchpath_t *path, *next;

    for (path = fs_searchpaths; path != fs_base_searchpaths; path = next) {
        next = path->next;
        free_search_path(path);
    }

    fs_searchpaths = fs_base_searchpaths;
}

static void setup_base_paths(void)
{
    // base paths have both BASE and GAME bits set by default
    // the GAME bit will be removed once gamedir is set,
    // and will be put back once gamedir is reset to basegame
    add_game_dir(FS_PATH_BASE | FS_PATH_GAME, "%s/"BASEGAME, sys_basedir->string);
    fs_base_searchpaths = fs_searchpaths;
}

// Sets the gamedir and path to a different directory.
static void setup_game_paths(void)
{
    searchpath_t *path;

    if (fs_game->string[0]) {
        // add system path first
        add_game_dir(FS_PATH_GAME, "%s/%s", sys_basedir->string, fs_game->string);

        // home paths override system paths
        if (sys_homedir->string[0]) {
            add_game_dir(FS_PATH_BASE, "%s/"BASEGAME, sys_homedir->string);
            add_game_dir(FS_PATH_GAME, "%s/%s", sys_homedir->string, fs_game->string);
        }

        // remove the game bit from base paths
        for (path = fs_base_searchpaths; path; path = path->next) {
            path->mode &= ~FS_PATH_GAME;
        }

        // this var is set for compatibility with server browsers, etc
        Cvar_FullSet("gamedir", fs_game->string, CVAR_ROM | CVAR_SERVERINFO, FROM_CODE);

    } else {
        if (sys_homedir->string[0]) {
            add_game_dir(FS_PATH_BASE | FS_PATH_GAME,
                         "%s/"BASEGAME, sys_homedir->string);
        }

        // add the game bit to base paths
        for (path = fs_base_searchpaths; path; path = path->next) {
            path->mode |= FS_PATH_GAME;
        }

        Cvar_FullSet("gamedir", "", CVAR_ROM, FROM_CODE);
    }

    // this var is used by the game library to find it's home directory
    Cvar_FullSet("fs_gamedir", fs_gamedir, CVAR_ROM, FROM_CODE);
}

/*
================
FS_Restart

Unless total is true, reloads paks only up to base dir
================
*/
void FS_Restart(qboolean total)
{
    Com_Printf("----- FS_Restart -----\n");

    if (total) {
        // perform full reset
        free_all_paths();
        setup_base_paths();
    } else {
        // just change gamedir
        free_game_paths();
        Q_snprintf(fs_gamedir, sizeof(fs_gamedir), "%s/"BASEGAME, sys_basedir->string);
#ifdef _WIN32
        FS_ReplaceSeparators(fs_gamedir, '/');
#endif
    }

    setup_game_paths();

    FS_Path_f();

    Com_Printf("----------------------\n");
}

/*
============
FS_Restart_f

Console command to fully re-start the file system.
============
*/
static void FS_Restart_f(void)
{
    CL_RestartFilesystem(qtrue);
}

static const cmdreg_t c_fs[] = {
    { "path", FS_Path_f },
    { "fdir", FS_FDir_f },
    { "dir", FS_Dir_f },
#ifdef _DEBUG
    { "fs_stats", FS_Stats_f },
#endif
    { "whereis", FS_WhereIs_f },
    { "link", FS_Link_f, FS_Link_c },
    { "unlink", FS_UnLink_f, FS_Link_c },
    { "softlink", FS_Link_f, FS_Link_c },
    { "softunlink", FS_UnLink_f, FS_Link_c },
    { "fs_restart", FS_Restart_f },

    { NULL }
};

/*
================
FS_Shutdown
================
*/
void FS_Shutdown(void)
{
    file_t *file;
    int i;

    if (!fs_searchpaths) {
        return;
    }

    // close file handles
    for (i = 0, file = fs_files; i < MAX_FILE_HANDLES; i++, file++) {
        if (file->type != FS_FREE) {
            Com_WPrintf("%s: closing handle %d\n", __func__, i + 1);
            FS_FCloseFile(i + 1);
        }
    }

    // free symbolic links
    free_all_links(&fs_hard_links);
    free_all_links(&fs_soft_links);

    // free search paths
    free_all_paths();

#if USE_ZLIB
    inflateEnd(&fs_zipstream.stream);
#endif

    Z_LeakTest(TAG_FILESYSTEM);

    Cmd_Deregister(c_fs);
}

// this is called when local server starts up and gets it's latched variables,
// client receives a serverdata packet, or user changes the game by hand while
// disconnected
static void fs_game_changed(cvar_t *self)
{
    char *s = self->string;

    // validate it
    if (*s) {
        if (!Q_stricmp(s, BASEGAME)) {
            Cvar_Reset(self);
        } else if (!COM_IsPath(s)) {
            Com_Printf("'%s' should contain characters [A-Za-z0-9_-] only.\n", self->name);
            Cvar_Reset(self);
        }
    }

    // check for the first time startup
    if (!fs_base_searchpaths) {
        // start up with baseq2 by default
        setup_base_paths();

        // check for game override
        setup_game_paths();

        FS_Path_f();
        return;
    }

    // otherwise, restart the filesystem
    CL_RestartFilesystem(qfalse);

    // FIXME: if baseq2/autoexec.cfg exists DO NOT exec default.cfg and config.cfg.
    // this assumes user prefers to do configuration via autoexec.cfg and doesn't
    // want settings and binds messed up whenever gamedir changes after startup.
    if (!FS_FileExistsEx(COM_AUTOEXEC_CFG, FS_TYPE_REAL | FS_PATH_BASE)) {
        Com_AddConfigFile(COM_DEFAULT_CFG, FS_PATH_GAME);
        Com_AddConfigFile(COM_CONFIG_CFG, FS_TYPE_REAL | FS_PATH_GAME);
    }

    // exec autoexec.cfg (must be a real file within the game directory)
    Com_AddConfigFile(COM_AUTOEXEC_CFG, FS_TYPE_REAL | FS_PATH_GAME);

    // exec postexec.cfg (must be a real file)
    Com_AddConfigFile(COM_POSTEXEC_CFG, FS_TYPE_REAL);
}

/*
================
FS_Init
================
*/
void FS_Init(void)
{
    Com_Printf("------- FS_Init -------\n");

    List_Init(&fs_hard_links);
    List_Init(&fs_soft_links);

    Cmd_Register(c_fs);

#ifdef _DEBUG
    fs_debug = Cvar_Get("fs_debug", "0", 0);
#endif

    // get the game cvar and start the filesystem
    fs_game = Cvar_Get("game", DEFGAME, CVAR_LATCH | CVAR_SERVERINFO);
    fs_game->changed = fs_game_changed;
    fs_game_changed(fs_game);

    Com_Printf("-----------------------\n");
}

