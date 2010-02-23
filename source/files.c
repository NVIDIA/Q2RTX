/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "com_local.h"
#include "files.h"
#include "sys_public.h"
#include "cl_public.h"
#include "d_pak.h"
#if USE_ZLIB
#include <zlib.h>
#include "unzip.h"
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

#define MAX_FILES_IN_PK2    0x4000
#define MAX_FILE_HANDLES    8

// macros for dealing portably with files at OS level
#ifdef _WIN32
#define FS_strcmp   Q_strcasecmp
#define FS_strncmp  Q_strncasecmp
#else
#define FS_strcmp   strcmp
#define FS_strncmp  strncmp
#endif

#define MAX_READ    0x40000        // read in blocks of 256k
#define MAX_WRITE   0x40000        // write in blocks of 256k

#ifdef _DEBUG
#define FS_DPrintf(...) \
    if( fs_debug && fs_debug->integer ) \
        Com_LPrintf( PRINT_DEVELOPER, __VA_ARGS__ )
#else
#define FS_DPrintf(...)
#endif

//
// in memory
//

typedef struct packfile_s {
    char    *name;
    size_t  filepos;
    size_t  filelen;

    struct packfile_s *hashNext;
} packfile_t;

typedef struct {
#if USE_ZLIB
    unzFile zFile;
#endif
    FILE    *fp;
    int     numfiles;
    packfile_t  *files;
    packfile_t  **fileHash;
    int         hashSize;
    char    filename[1];
} pack_t;

typedef struct searchpath_s {
    struct searchpath_s *next;
    int mode;
    pack_t *pack;        // only one of filename / pack will be used
    char filename[1];
} searchpath_t;

typedef struct {
    enum {
        FS_FREE,
        FS_REAL,
        FS_PAK,
#if USE_ZLIB
        FS_PK2,
        FS_GZIP,
#endif
        FS_BAD
    } type;
    unsigned mode;
    FILE *fp;
#if USE_ZLIB
    void *zfp;
#endif
    packfile_t *pak;
    qboolean unique;
    size_t length;
} file_t;

typedef struct symlink_s {
    struct symlink_s *next;
    size_t targlen;
    size_t namelen;
    char *target;
    char name[1];
} symlink_t;

// these point to user home directory
char        fs_gamedir[MAX_OSPATH];
//static char        fs_basedir[MAX_OSPATH];

#ifdef _DEBUG
static cvar_t    *fs_debug;
#endif
static cvar_t    *fs_restrict_mask;

static searchpath_t    *fs_searchpaths;
static searchpath_t    *fs_base_searchpaths;

static symlink_t     *fs_links;

static file_t     fs_files[MAX_FILE_HANDLES];

static qboolean     fs_fileFromPak;

#ifdef _DEBUG
static int          fs_count_read, fs_count_strcmp, fs_count_open;
#endif

cvar_t      *fs_game;

/*

All of Quake's data access is through a hierchal file system,
but the contents of the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding the quake.exe and all game directories.
The sys_* files pass this to host_init in quakeparms_t->basedir. 
This can be overridden with the "-basedir" command line parm to allow code debugging in a different directory. 
The base directory is only used during filesystem initialization.

The "game directory" is the first tree on the search path and directory that
all generated files (savegames, screenshots, demos, config files) will be saved to.
This can be overridden with the "-game" command line parameter.
The game directory can never be changed while quake is executing.
This is a precacution against having a malicious server instruct clients to write files over areas they shouldn't.

*/

/*
================
FS_pathcmp

Portably compares quake paths
================
*/
int FS_pathcmp( const char *s1, const char *s2 ) {
    int        c1, c2;
    
    do {
        c1 = *s1++;
        c2 = *s2++;
        
        if( c1 != c2 ) {
            c1 = c1 == '\\' ? '/' : Q_tolower( c1 );
            c2 = c2 == '\\' ? '/' : Q_tolower( c2 );
            if( c1 < c2 )
                return -1;
            if( c1 > c2 )
                return 1;        /* strings not equal */
        }
    } while( c1 );
    
    return 0;        /* strings are equal */
}

int FS_pathcmpn( const char *s1, const char *s2, size_t n ) {
    int        c1, c2;
    
    do {
        c1 = *s1++;
        c2 = *s2++;

        if( !n-- )
            return 0;       /* strings are equal until end point */
        
        if( c1 != c2 ) {
            c1 = c1 == '\\' ? '/' : Q_tolower( c1 );
            c2 = c2 == '\\' ? '/' : Q_tolower( c2 );
            if( c1 < c2 )
                return -1;
            if( c1 > c2 )
                return 1;        /* strings not equal */
        }
    } while( c1 );
    
    return 0;        /* strings are equal */
}

/*
================
FS_AllocHandle
================
*/
static file_t *FS_AllocHandle( fileHandle_t *f ) {
    file_t *file;
    int i;

    for( i = 0, file = fs_files; i < MAX_FILE_HANDLES; i++, file++ ) {
        if( file->type == FS_FREE ) {
            break;
        }
    }

    if( i == MAX_FILE_HANDLES ) {
        Com_Error( ERR_FATAL, "%s: none free", __func__ );
    }

    *f = i + 1;
    return file;
}

/*
================
FS_FileForHandle
================
*/
static file_t *FS_FileForHandle( fileHandle_t f ) {
    file_t *file;

    if( f <= 0 || f >= MAX_FILE_HANDLES + 1 ) {
        Com_Error( ERR_FATAL, "%s: invalid handle: %i", __func__, f );
    }

    file = &fs_files[f - 1];
    if( file->type <= FS_FREE || file->type >= FS_BAD ) {
        Com_Error( ERR_FATAL, "%s: invalid file type: %i", __func__, file->type );
    }

    return file;
}

/*
================
FS_ValidatePath
================
*/
static qboolean FS_ValidatePath( const char *s ) {
    const char *start;

    // check for leading slash
    // check for empty path
    if( *s == '/' || *s == '\\' /*|| *s == 0*/ ) {
        return qfalse;
    }

    start = s;
    while( *s ) {
        // check for ".."
        if( *s == '.' && s[1] == '.' ) {
            return qfalse;
        }
        if( *s == '/' || *s == '\\' ) {
            // check for two slashes in a row
            // check for trailing slash
            if( ( s[1] == '/' || s[1] == '\\' || s[1] == 0 ) ) {
                return qfalse;
            }
        }
#ifdef _WIN32
        if( *s == ':' ) {
            // check for "X:\"
            if( s[1] == '\\' || s[1] == '/' ) {
                return qfalse;
            }
        }
#endif
        s++;
    }

    // check length
    if( s - start > MAX_OSPATH ) {
        return qfalse;
    }

    return qtrue;
}

#ifdef _WIN32
/*
================
FS_ReplaceSeparators
================
*/
char *FS_ReplaceSeparators( char *s, int separator ) {
    char *p;

    p = s;
    while( *p ) {
        if( *p == '/' || *p == '\\' ) {
            *p = separator;
        }
        p++;
    }

    return s;
}
#endif

/*
================
FS_GetFileLength

Returns:
- current length for files opened for writing.
- cached length for files opened for reading.
- INVALID_LENGTH for gzip-compressed files.
================
*/
size_t FS_GetFileLength( fileHandle_t f ) {
    file_t *file = FS_FileForHandle( f );
    file_info_t info;

    switch( file->type ) {
    case FS_REAL:
        if( !Sys_GetFileInfo( file->fp, &info ) ) {
            return INVALID_LENGTH;
        }
        return info.size;
    case FS_PAK:
#if USE_ZLIB
    case FS_PK2:
#endif
        return file->length;
#if USE_ZLIB
    case FS_GZIP:
        return INVALID_LENGTH;
#endif
    default:
        Com_Error( ERR_FATAL, "%s: bad file type", __func__ );
    }

    return INVALID_LENGTH;
}

/*
============
FS_Tell
============
*/
size_t FS_Tell( fileHandle_t f ) {
    file_t *file = FS_FileForHandle( f );
    int length;

    switch( file->type ) {
    case FS_REAL:
        length = ftell( file->fp );
        break;
    case FS_PAK:
        length = ftell( file->fp ) - file->pak->filepos;
        break;
#if USE_ZLIB
    case FS_PK2:
        length = unztell( file->zfp );
        break;
    case FS_GZIP:
        return INVALID_LENGTH;
#endif
    default:
        Com_Error( ERR_FATAL, "%s: bad file type", __func__ );
    }

    return length;
}

/*
============
FS_Seek
============
*/
qboolean FS_Seek( fileHandle_t f, size_t offset ) {
    file_t *file = FS_FileForHandle( f );

    switch( file->type ) {
    case FS_REAL:
    case FS_PAK:
        if( fseek( file->fp, offset, SEEK_CUR ) == -1 ) {
            return qfalse;
        }
        break;
#if USE_ZLIB
    case FS_GZIP:
        if( gzseek( file->zfp, offset, SEEK_CUR ) == -1 ) {
            return qfalse;
        }
        break;
    //case FS_PK2:
    //    length = unztell( file->zfp );
    //    break;
#endif
    default:
        return qfalse;
    }

    return qtrue;
}


/*
============
FS_CreatePath

Creates any directories needed to store the given filename.
Expects a fully qualified quake path (i.e. with / separators).
============
*/
static void FS_CreatePath( char *path ) {
    char *ofs;

    if( !*path ) {
        return;
    }

    for( ofs = path + 1; *ofs; ofs++ ) {
        if( *ofs == '/' ) {    
            // create the directory
            *ofs = 0;
            Q_mkdir( path );
            *ofs = '/';
        }
    }
}

qboolean FS_FilterFile( fileHandle_t f ) {
#if USE_ZLIB
    file_t *file = FS_FileForHandle( f );
    int mode;
    char *modeStr;
    void *zfp;

    switch( file->type ) {
    case FS_GZIP:
        return qtrue;
    case FS_REAL:
        break;
    default:
        return qfalse;
    }

    mode = file->mode & FS_MODE_MASK;
    switch( mode ) {
    case FS_MODE_READ:
        modeStr = "rb";
        break;
    case FS_MODE_WRITE:
        modeStr = "wb";
        break;
    default:
        return qfalse;
    }

    fseek( file->fp, 0, SEEK_SET );
    zfp = gzdopen( fileno( file->fp ), modeStr );
    if( !zfp ) {
        return qfalse;
    }

    file->zfp = zfp;
    file->type = FS_GZIP;
    return qtrue;
#else
    return qfalse;
#endif
}


/*
==============
FS_FCloseFile
==============
*/
void FS_FCloseFile( fileHandle_t f ) {
    file_t *file = FS_FileForHandle( f );

    FS_DPrintf( "%s: %u\n", __func__, f );

    switch( file->type ) {
    case FS_REAL:
        fclose( file->fp );
        break;
    case FS_PAK:
        if( file->unique ) {
            fclose( file->fp );
        }
        break;
#if USE_ZLIB
    case FS_GZIP:
        gzclose( file->zfp );
        break;
    case FS_PK2:
        unzCloseCurrentFile( file->zfp );
        if( file->unique ) {
            unzClose( file->zfp );
        }
        break;
#endif
    default:
        break;
    }

    memset( file, 0, sizeof( *file ) );
}

/*
============
FS_FOpenFileWrite
============
*/
static size_t FS_FOpenFileWrite( file_t *file, const char *name ) {
    char fullpath[MAX_OSPATH];
    FILE *fp;
    char *modeStr;
    unsigned mode;
    size_t len;

    if( !FS_ValidatePath( name ) ) {
        FS_DPrintf( "%s: refusing invalid path: %s\n", __func__, name );
        return INVALID_LENGTH;
    }

    if( ( file->mode & FS_PATH_MASK ) == FS_PATH_BASE ) {
        if( sys_homedir->string[0] ) {
            len = Q_concat( fullpath, sizeof( fullpath ),
                sys_homedir->string, "/" BASEGAME "/", name, NULL );
        } else {
            len = Q_concat( fullpath, sizeof( fullpath ),
                sys_basedir->string, "/" BASEGAME "/", name, NULL );
        }
    } else {
        len = Q_concat( fullpath, sizeof( fullpath ),
            fs_gamedir, "/", name, NULL );
    }
    if( len >= sizeof( fullpath ) ) {
        FS_DPrintf( "%s: refusing oversize path: %s\n", __func__, name );
        return INVALID_LENGTH;
    }

    mode = file->mode & FS_MODE_MASK;
    switch( mode ) {
    case FS_MODE_APPEND:
        modeStr = "ab";
        break;
    case FS_MODE_WRITE:
        modeStr = "wb";
        break;
    case FS_MODE_RDWR:
        modeStr = "r+b";
        break;
    default:
        Com_Error( ERR_FATAL, "%s: %s: invalid mode mask",
            __func__, name );
        modeStr = NULL;
        break;
    }

    FS_CreatePath( fullpath );

    fp = fopen( fullpath, modeStr );
    if( !fp ) {
        FS_DPrintf( "%s: %s: couldn't open\n",
            __func__, fullpath );
        return INVALID_LENGTH;
    }

#ifdef __unix__
    if( !Sys_GetFileInfo( fp, NULL ) ) {
        FS_DPrintf( "%s: %s: couldn't get info\n",
            __func__, fullpath );
        fclose( fp );
        return INVALID_LENGTH;
    }
#endif

    FS_DPrintf( "%s: %s: succeeded\n", __func__, fullpath );

    file->fp = fp;
    file->type = FS_REAL;
    file->length = 0;
    file->unique = qtrue;

    if( mode == FS_MODE_WRITE ) {
        return 0;
    }

    if( mode == FS_MODE_RDWR ) {
        fseek( fp, 0, SEEK_END );
    }
    
    return ( size_t )ftell( fp );
}

static size_t FS_FOpenFromPak( file_t *file, pack_t *pak, packfile_t *entry, qboolean unique ) {
    fs_fileFromPak = qtrue;

    // open a new file on the pakfile
#if USE_ZLIB
    if( pak->zFile ) {
        void *zfp;

        if( unique ) {
            zfp = unzReOpen( pak->filename, pak->zFile );
            if( !zfp ) {
                Com_Error( ERR_FATAL, "%s: couldn't reopen %s",
                    __func__, pak->filename );
            }
        } else {
            zfp = pak->zFile;
        }
        if( unzSetCurrentFileInfoPosition( zfp, entry->filepos ) == -1 ) {
            Com_Error( ERR_FATAL, "%s: couldn't seek into %s",
                __func__, pak->filename );
        }
        if( unzOpenCurrentFile( zfp ) != UNZ_OK ) {
            Com_Error( ERR_FATAL, "%s: couldn't open curfile from %s",
                __func__, pak->filename );
        }

        file->zfp = zfp;
        file->type = FS_PK2;
    } else
#endif
    {
        FILE *fp;

        if( unique ) {
            fp = fopen( pak->filename, "rb" );
            if( !fp ) {
                Com_Error( ERR_FATAL, "%s: couldn't reopen %s",
                    __func__, pak->filename );
            }
        } else {
            fp = pak->fp;
        }

        if( fseek( fp, entry->filepos, SEEK_SET ) == -1 ) {
            Com_Error( ERR_FATAL, "%s: couldn't seek into %s",
                __func__, pak->filename );
        }

        file->fp = fp;
        file->type = FS_PAK;
    }

    file->pak = entry;
    file->length = entry->filelen;
    file->unique = unique;

    FS_DPrintf( "%s: %s/%s: succeeded\n",
        __func__, pak->filename, entry->name );

    return file->length;
}

/*
===========
FS_FOpenFileRead

Finds the file in the search path.
Fills file_t and returns file length.
In case of GZIP files, returns *raw* (compressed) length!
Used for streaming data out of either a pak file or
a seperate file.
===========
*/
static size_t FS_FOpenFileRead( file_t *file, const char *name, qboolean unique ) {
    char            fullpath[MAX_OSPATH];
    searchpath_t    *search;
    pack_t          *pak;
    unsigned        hash;
    packfile_t      *entry;
    FILE            *fp;
    file_info_t    info;
    int             valid = -1;
    size_t          len;

    fs_fileFromPak = qfalse;
#ifdef _DEBUG
    fs_count_read++;
#endif

//
// search through the path, one element at a time
//
    hash = Com_HashPath( name, 0 );

    for( search = fs_searchpaths; search; search = search->next ) {
        if( file->mode & FS_PATH_MASK ) {
            if( ( file->mode & search->mode & FS_PATH_MASK ) == 0 ) {
                continue;
            }
        }
    
    // is the element a pak file?
        if( search->pack ) {
            if( ( file->mode & FS_TYPE_MASK ) == FS_TYPE_REAL ) {
                continue;
            }
        // look through all the pak file elements
            pak = search->pack;
            entry = pak->fileHash[ hash & ( pak->hashSize - 1 ) ];
            for( ; entry; entry = entry->hashNext ) {
#ifdef _DEBUG
                fs_count_strcmp++;
#endif
                if( !FS_pathcmp( entry->name, name ) ) {
                    // found it!
                    return FS_FOpenFromPak( file, pak, entry, unique );
                }
            }
        } else {
            if( ( file->mode & FS_TYPE_MASK ) == FS_TYPE_PAK ) {
                continue;
            }
            if( valid == -1 ) {
                if( !FS_ValidatePath( name ) ) {
                    FS_DPrintf( "%s: refusing invalid path: %s\n",
                        __func__, name );
                    valid = 0;
                }
            }
            if( valid == 0 ) {
                continue;
            }
    // check a file in the directory tree
            len = Q_concat( fullpath, sizeof( fullpath ),
                search->filename, "/", name, NULL );
            if( len >= sizeof( fullpath ) ) {
                FS_DPrintf( "%s: refusing oversize path: %s\n",
                    __func__, name );
                continue;
            }

#ifdef _DEBUG
            fs_count_open++;
#endif
            fp = fopen( fullpath, "rb" );
            if( !fp ) {
                continue;
            }

            if( !Sys_GetFileInfo( fp, &info ) ) {
                FS_DPrintf( "%s: %s: couldn't get info\n",
                    __func__, fullpath );
                fclose( fp );
                continue;
            }

            file->fp = fp;
            file->type = FS_REAL;
            file->unique = qtrue;
            file->length = info.size;

            FS_DPrintf( "%s: %s: succeeded\n", __func__, fullpath );

            return file->length;
        }
    }
    
    FS_DPrintf( "%s: %s: not found\n", __func__, name );
    
    return INVALID_LENGTH;
}

/*
=================
FS_LastFileFromPak
=================
*/
qboolean FS_LastFileFromPak( void ) {
    return fs_fileFromPak;
}


/*
=================
FS_ReadFile

Properly handles partial reads
=================
*/
size_t FS_Read( void *buffer, size_t len, fileHandle_t hFile ) {
    size_t      block, remaining = len, read = 0;
    byte        *buf = (byte *)buffer;
    file_t   *file = FS_FileForHandle( hFile );

    // read in chunks for progress bar
    while( remaining ) {
        block = remaining;
        if( block > MAX_READ )
            block = MAX_READ;
        switch( file->type ) {
        case FS_REAL:
        case FS_PAK:
            read = fread( buf, 1, block, file->fp );
            break;
#if USE_ZLIB
        case FS_GZIP:
            read = gzread( file->zfp, buf, block );
            break;
        case FS_PK2:
            read = unzReadCurrentFile( file->zfp, buf, block );
            break;
#endif
        default:
            break;
        }
        if( read == 0 ) {
            return len - remaining;
        }
        if( read > block ) {
            Com_Error( ERR_FATAL, "FS_Read: %"PRIz" bytes read", read );
        }

        remaining -= read;
        buf += read;
    }

    return len;
}

size_t FS_ReadLine( fileHandle_t f, char *buffer, int size ) {
    file_t *file = FS_FileForHandle( f );
    char *s;
    size_t len;

    if( file->type != FS_REAL ) {
        return 0;
    }
    do {
        s = fgets( buffer, size, file->fp );
        if( !s ) {
            return 0;
        }
        len = strlen( s );
    } while( len < 2 );

    s[ len - 1 ] = 0;
    return len - 1;
}

void FS_Flush( fileHandle_t f ) {
    file_t *file = FS_FileForHandle( f );

    switch( file->type ) {
    case FS_REAL:
        fflush( file->fp );
        break;
#if USE_ZLIB
    case FS_GZIP:
        gzflush( file->zfp, Z_SYNC_FLUSH );
        break;
#endif
    default:
        break;
    }
}

/*
=================
FS_Write

Properly handles partial writes
=================
*/
size_t FS_Write( const void *buffer, size_t len, fileHandle_t hFile ) {
    size_t      block, remaining = len, write = 0;
    byte        *buf = (byte *)buffer;
    file_t   *file = FS_FileForHandle( hFile );

    // read in chunks for progress bar
    while( remaining ) {
        block = remaining;
        if( block > MAX_WRITE )
            block = MAX_WRITE;
        switch( file->type ) {
        case FS_REAL:
            write = fwrite( buf, 1, block, file->fp );
            break;
#if USE_ZLIB
        case FS_GZIP:
            write = gzwrite( file->zfp, buf, block );
            break;
#endif
        default:
            Com_Error( ERR_FATAL, "FS_Write: illegal file type" );
            break;
        }
        if( write == 0 ) {
            return len - remaining;
        }
        if( write > block ) {
            Com_Error( ERR_FATAL, "FS_Write: %"PRIz" bytes written", write );
        }

        remaining -= write;
        buf += write;
    }

    if( ( file->mode & FS_FLUSH_MASK ) == FS_FLUSH_SYNC ) {
        switch( file->type ) {
        case FS_REAL:
            fflush( file->fp );
            break;
#if USE_ZLIB
        case FS_GZIP:
            gzflush( file->zfp, Z_SYNC_FLUSH );
            break;
#endif
        default:
            break;
        }
    }

    return len;
}

static char *FS_ExpandLinks( const char *filename ) {
    static char buffer[MAX_OSPATH];
    symlink_t   *link;
    size_t      len;

    len = strlen( filename );
    for( link = fs_links; link; link = link->next ) {
        if( link->namelen > len ) {
            continue;
        }
        if( !FS_pathcmpn( link->name, filename, link->namelen ) ) {
            if( link->targlen + len - link->namelen >= MAX_OSPATH ) {
                FS_DPrintf( "%s: %s: MAX_OSPATH exceeded\n", __func__, filename );
                return ( char * )filename;
            }
            memcpy( buffer, link->target, link->targlen );
            memcpy( buffer + link->targlen, filename + link->namelen,
                len - link->namelen + 1 );
            FS_DPrintf( "%s: %s --> %s\n", __func__, filename, buffer );
            return buffer;
        }
    }

    return ( char * )filename;
}

/*
============
FS_FOpenFile
============
*/
size_t FS_FOpenFile( const char *name, fileHandle_t *f, int mode ) {
    file_t    *file;
    fileHandle_t hFile;
    size_t        ret = INVALID_LENGTH;

    if( !name || !f ) {
        Com_Error( ERR_FATAL, "%s: NULL", __func__ );
    }

    *f = 0;

    if( !fs_searchpaths ) {
        return ret; // not yet initialized
    }

    if( *name == '/' ) {
        name++;
    }

    if( ( mode & FS_MODE_MASK ) == FS_MODE_READ ) {
        name = FS_ExpandLinks( name );
    }

    // allocate new file handle
    file = FS_AllocHandle( &hFile );
    file->mode = mode;

    mode &= FS_MODE_MASK;
    switch( mode ) {
    case FS_MODE_READ:
        ret = FS_FOpenFileRead( file, name, qtrue );
        break;
    case FS_MODE_WRITE:
    case FS_MODE_APPEND:
    case FS_MODE_RDWR:
        ret = FS_FOpenFileWrite( file, name );
        break;
    default:
        Com_Error( ERR_FATAL, "%s: illegal mode: %u", __func__, mode );
        break;
    }

    // if succeeded, store file handle
    if( ret != -1 ) {
        *f = hFile;
    }

    return ret;
}

#if USE_LOADBUF

#define MAX_LOAD_BUFFER        0x100000        // 1 MiB

// static buffer for small, stacked file loads and temp allocations
// the last allocation may be easily undone
static byte loadBuffer[MAX_LOAD_BUFFER];
static byte *loadLast;
static size_t loadSaved;
static size_t loadInuse;
static int loadStack;

// for statistics
static int loadCount;
static int loadCountStatic;

#endif // USE_LOADBUF

/*
============
FS_LoadFile

Filenames are relative to the quake search path
a null buffer will just return the file length without loading
============
*/
size_t FS_LoadFileEx( const char *path, void **buffer, int flags, memtag_t tag ) {
    file_t *file;
    fileHandle_t f;
    byte    *buf;
    size_t    len;

    if( !path ) {
        Com_Error( ERR_FATAL, "%s: NULL", __func__ );
    }

    if( buffer ) {
        *buffer = NULL;
    }

    if( !fs_searchpaths ) {
        return INVALID_LENGTH; // not yet initialized
    }

    if( *path == '/' ) {
        path++;
    }

    path = FS_ExpandLinks( path );

    // allocate new file handle
    file = FS_AllocHandle( &f );
    flags &= ~FS_MODE_MASK;
    file->mode = flags | FS_MODE_READ;

    // look for it in the filesystem or pack files
    len = FS_FOpenFileRead( file, path, qfalse );
    if( len == INVALID_LENGTH ) {
        return len;
    }

    if( buffer ) {
#if USE_ZLIB
        if( file->type == FS_GZIP ) {
            len = INVALID_LENGTH; // unknown length
        } else
#endif
        {
            if( tag == TAG_FREE ) {
                buf = FS_AllocTempMem( len + 1 );
            } else {
                buf = Z_TagMalloc( len + 1, tag );
            }
            if( FS_Read( buf, len, f ) == len ) {
                *buffer = buf;
                buf[len] = 0;
            } else {
                Com_EPrintf( "%s: error reading file: %s\n", __func__, path );
                if( tag == TAG_FREE ) {
                    FS_FreeFile( buf );
                } else {
                    Z_Free( buf );
                }
                len = INVALID_LENGTH;
            }
        }
    }

    FS_FCloseFile( f );

    return len;
}

size_t FS_LoadFile( const char *path, void **buffer ) {
    return FS_LoadFileEx( path, buffer, 0, TAG_FREE );
}

void *FS_AllocTempMem( size_t length ) {
    byte *buf;

#if USE_LOADBUF
    if( loadInuse + length <= MAX_LOAD_BUFFER && !( fs_restrict_mask->integer & 16 ) ) {
        buf = &loadBuffer[loadInuse];
        loadLast = buf;
        loadSaved = loadInuse;
        loadInuse += length;
        loadInuse = ( loadInuse + 31 ) & ~31;
        loadStack++;
        loadCountStatic++;
    } else
#endif
    {
       // Com_Printf(S_COLOR_MAGENTA"alloc %d\n",length);
        buf = FS_Malloc( length );
#if USE_LOADBUF
        loadCount++;
#endif
    }
    return buf;
}

/*
=============
FS_FreeFile
=============
*/
void FS_FreeFile( void *buffer ) {
    if( !buffer ) {
        Com_Error( ERR_FATAL, "%s: NULL", __func__ );
    }
#if USE_LOADBUF
    if( ( byte * )buffer >= loadBuffer && ( byte * )buffer < loadBuffer + MAX_LOAD_BUFFER ) {
        if( loadStack == 0 ) {
            Com_Error( ERR_FATAL, "%s: empty load stack", __func__ );
        }
        loadStack--;
        if( loadStack == 0 ) {
            loadInuse = 0;
      //      Com_Printf(S_COLOR_MAGENTA"clear\n");
        } else if( buffer == loadLast ) {
            loadInuse = loadSaved;
    //        Com_Printf(S_COLOR_MAGENTA"partial\n");
        }
    } else
#endif
    {
        Z_Free( buffer );
    }
}

#if USE_CLIENT

/*
================
FS_RenameFile
================
*/
qboolean FS_RenameFile( const char *from, const char *to ) {
    char frompath[MAX_OSPATH];
    char topath[MAX_OSPATH];
    size_t len;

    if( *from == '/' ) {
        from++;
    }
    if( !FS_ValidatePath( from ) ) {
        FS_DPrintf( "%s: refusing invalid source path: %s\n", __func__, from );
        return qfalse;
    }
    len = Q_concat( frompath, sizeof( frompath ), fs_gamedir, "/", from, NULL );
    if( len >= sizeof( frompath ) ) {
        FS_DPrintf( "%s: refusing oversize source path: %s\n", __func__, frompath );
        return qfalse;
    }

    if( *to == '/' ) {
        to++;
    }
    if( !FS_ValidatePath( to ) ) {
        FS_DPrintf( "%s: refusing invalid destination path: %s\n", __func__, to );
        return qfalse;
    }
    len = Q_concat( topath, sizeof( topath ), fs_gamedir, "/", to, NULL );
    if( len >= sizeof( topath ) ) {
        FS_DPrintf( "%s: refusing oversize destination path: %s\n", __func__, topath );
        return qfalse;
    }

    if( rename( frompath, topath ) ) {
        return qfalse;
    }
    return qtrue;
}

#endif // USE_CLIENT

/*
================
FS_FPrintf
================
*/
void FS_FPrintf( fileHandle_t f, const char *format, ... ) {
    va_list argptr;
    char string[MAXPRINTMSG];
    size_t len;

    va_start( argptr, format );
    len = Q_vscnprintf( string, sizeof( string ), format, argptr );
    va_end( argptr );

    FS_Write( string, len, f );
}

/*
=================
FS_LoadPakFile

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
static pack_t *FS_LoadPakFile( const char *packfile ) {
    dpackheader_t   header;
    int             i;
    packfile_t      *file;
    dpackfile_t     *dfile;
    int             numpackfiles;
    char            *names;
    size_t          namesLength;
    pack_t          *pack;
    FILE            *packhandle;
    dpackfile_t     info[MAX_FILES_IN_PACK];
    int             hashSize;
    unsigned        hash;
    size_t          len;

    packhandle = fopen( packfile, "rb" );
    if( !packhandle ) {
        Com_WPrintf( "Couldn't open %s\n", packfile );
        return NULL;
    }

    if( fread( &header, 1, sizeof( header ), packhandle ) != sizeof( header ) ) {
        Com_WPrintf( "Reading header failed on %s\n", packfile );
        goto fail;
    }

    if( LittleLong( header.ident ) != IDPAKHEADER ) {
        Com_WPrintf( "%s is not a 'PACK' file\n", packfile );
        goto fail;
    }

    header.dirlen = LittleLong( header.dirlen );
    if( header.dirlen % sizeof( dpackfile_t ) ) {
        Com_WPrintf( "%s has bad directory length\n", packfile );
        goto fail;
    }

    numpackfiles = header.dirlen / sizeof( dpackfile_t );
    if( numpackfiles < 1 ) {
        Com_WPrintf( "%s has bad number of files: %i\n", packfile, numpackfiles );
        goto fail;
    }
    if( numpackfiles > MAX_FILES_IN_PACK ) {
        Com_WPrintf( "%s has too many files: %i > %i\n", packfile, numpackfiles, MAX_FILES_IN_PACK );
        goto fail;
    }

    header.dirofs = LittleLong( header.dirofs );
    if( fseek( packhandle, header.dirofs, SEEK_SET ) ) {
        Com_WPrintf( "Seeking to directory failed on %s\n", packfile );
        goto fail;
    }
    if( fread( info, 1, header.dirlen, packhandle ) != header.dirlen ) {
        Com_WPrintf( "Reading directory failed on %s\n", packfile );
        goto fail;
    }

    namesLength = 0;
    for( i = 0, dfile = info; i < numpackfiles; i++, dfile++ ) {
        dfile->name[sizeof( dfile->name ) - 1] = 0;
        namesLength += strlen( dfile->name ) + 1;
    }

    hashSize = Q_CeilPowerOfTwo( numpackfiles );
    if( hashSize > 32 ) {
        hashSize >>= 1;
    }

    len = strlen( packfile );
    len = ( len + 3 ) & ~3;
    pack = FS_Malloc( sizeof( pack_t ) +
        numpackfiles * sizeof( packfile_t ) +
        hashSize * sizeof( packfile_t * ) +
        namesLength + len );
    strcpy( pack->filename, packfile );
    pack->fp = packhandle;
#if USE_ZLIB
    pack->zFile = NULL;
#endif
    pack->numfiles = numpackfiles;
    pack->hashSize = hashSize;
    pack->files = ( packfile_t * )( ( byte * )pack + sizeof( pack_t ) + len );
    pack->fileHash = ( packfile_t ** )( pack->files + numpackfiles );
    names = ( char * )( pack->fileHash + hashSize );
    memset( pack->fileHash, 0, hashSize * sizeof( packfile_t * ) );

// parse the directory
    for( i = 0, file = pack->files, dfile = info; i < pack->numfiles; i++, file++, dfile++ ) {
        len = strlen( dfile->name ) + 1;

        file->name = memcpy( names, dfile->name, len );
        names += len;

        file->filepos = LittleLong( dfile->filepos );
        file->filelen = LittleLong( dfile->filelen );

        hash = Com_HashPath( file->name, hashSize );
        file->hashNext = pack->fileHash[hash];
        pack->fileHash[hash] = file;
    }

    FS_DPrintf( "%s: %d files, %d hash table entries\n",
        packfile, numpackfiles, hashSize );

    return pack;

fail:
    fclose( packhandle );
    return NULL;
}

#if USE_ZLIB
/*
=================
FS_LoadZipFile
=================
*/
static pack_t *FS_LoadZipFile( const char *packfile ) {
    int             i;
    packfile_t      *file;
    char            *names;
    int             numFiles;
    pack_t          *pack;
    unzFile         zFile;
    unz_global_info zGlobalInfo;
    unz_file_info   zInfo;
    char            name[MAX_QPATH];
    size_t          namesLength;
    int             hashSize;
    unsigned        hash;
    size_t          len;

    zFile = unzOpen( packfile );
    if( !zFile ) {
        Com_WPrintf( "unzOpen() failed on %s\n", packfile );
        return NULL;
    }

    if( unzGetGlobalInfo( zFile, &zGlobalInfo ) != UNZ_OK ) {
        Com_WPrintf( "unzGetGlobalInfo() failed on %s\n", packfile );
        goto fail;
    }

    numFiles = zGlobalInfo.number_entry;
    if( numFiles > MAX_FILES_IN_PK2 ) {
        Com_WPrintf( "%s has too many files, %i > %i\n", packfile, numFiles, MAX_FILES_IN_PK2 );
        goto fail;
    }

    if( unzGoToFirstFile( zFile ) != UNZ_OK ) {
        Com_WPrintf( "unzGoToFirstFile() failed on %s\n", packfile );
        goto fail;
    }

    namesLength = 0;
    for( i = 0; i < numFiles; i++ ) {
        if( unzGetCurrentFileInfo( zFile, &zInfo, name, sizeof( name ), NULL, 0, NULL, 0 ) != UNZ_OK ) {
            Com_WPrintf( "unzGetCurrentFileInfo() failed on %s\n", packfile );
            goto fail;
        }

        namesLength += strlen( name ) + 1;

        if( i != numFiles - 1 && unzGoToNextFile( zFile ) != UNZ_OK ) {
            Com_WPrintf( "unzGoToNextFile() failed on %s\n", packfile );

        }
    }

    hashSize = Q_CeilPowerOfTwo( numFiles );
    if( hashSize > 32 ) {
        hashSize >>= 1;
    }

    len = strlen( packfile );
    len = ( len + 3 ) & ~3;
    pack = FS_Malloc( sizeof( pack_t ) +
        numFiles * sizeof( packfile_t ) +
        hashSize * sizeof( packfile_t * ) +
        namesLength + len );
    strcpy( pack->filename, packfile );
    pack->zFile = zFile;
    pack->fp = NULL;
    pack->numfiles = numFiles;
    pack->hashSize = hashSize;
    pack->files = ( packfile_t * )( ( byte * )pack + sizeof( pack_t ) + len );
    pack->fileHash = ( packfile_t ** )( pack->files + numFiles );
    names = ( char * )( pack->fileHash + hashSize );
    memset( pack->fileHash, 0, hashSize * sizeof( packfile_t * ) );

// parse the directory
    unzGoToFirstFile( zFile );

    for( i = 0, file = pack->files; i < numFiles; i++, file++ ) {
        unzGetCurrentFileInfo( zFile, &zInfo, name, sizeof( name ), NULL, 0, NULL, 0 );

        len = strlen( name ) + 1;
        file->name = names;

        strcpy( file->name, name );
        file->filepos = unzGetCurrentFileInfoPosition( zFile );
        file->filelen = zInfo.uncompressed_size;

        hash = Com_HashPath( file->name, hashSize );
        file->hashNext = pack->fileHash[hash];
        pack->fileHash[hash] = file;

        names += len;
        
        if( i != numFiles - 1 ) {
            unzGoToNextFile( zFile );
        }
    }

    FS_DPrintf( "%s: %d files, %d hash table entries\n",
        packfile, numFiles, hashSize );

    return pack;

fail:
    unzClose( zFile );
    return NULL;
}
#endif

// this is complicated as we need pakXX.pak loaded first,
// sorted in numerical order, then the rest of the paks in
// alphabetical order, e.g. pak0.pak, pak2.pak, pak17.pak, abc.pak...
static int QDECL pakcmp( const void *p1, const void *p2 ) {
    char *s1 = *( char ** )p1;
    char *s2 = *( char ** )p2;

    if( !FS_strncmp( s1, "pak", 3 ) ) {
        if( !FS_strncmp( s2, "pak", 3 ) ) {
            int n1 = strtoul( s1 + 3, &s1, 10 );
            int n2 = strtoul( s2 + 3, &s2, 10 );
            if( n1 > n2 ) {
                return 1;
            }
            if( n1 < n2 ) {
                return -1;
            }
            goto alphacmp;
        }
        return -1;
    }
    if( !FS_strncmp( s2, "pak", 3 ) ) {
        return 1;
    }

alphacmp:
    return FS_strcmp( s1, s2 );
}

static void FS_LoadPackFiles( int mode, const char *extension, pack_t *(loadfunc)( const char * ) ) {
    int             i;
    searchpath_t    *search;
    pack_t          *pak;
    void            **list;
    int             numFiles;
    char            path[MAX_OSPATH];

    list = Sys_ListFiles( fs_gamedir, extension, FS_SEARCH_NOSORT, 0, &numFiles );
    if( !list ) {
        return;
    }
    qsort( list, numFiles, sizeof( list[0] ), pakcmp );
    for( i = 0; i < numFiles; i++ ) {
        Q_concat( path, sizeof( path ), fs_gamedir, "/", list[i], NULL );
        pak = (*loadfunc)( path );
        if( !pak )
            continue;
        search = FS_Malloc( sizeof( searchpath_t ) );
        search->mode = mode;
        search->filename[0] = 0;
        search->pack = pak;
        search->next = fs_searchpaths;
        fs_searchpaths = search;    
    }

    FS_FreeList( list );    
}

/*
================
FS_AddGameDirectory

Sets fs_gamedir, adds the directory to the head of the path,
then loads and adds pak*.pak, then anything else in alphabethical order.
================
*/
static void q_printf( 2, 3 ) FS_AddGameDirectory( int mode, const char *fmt, ... ) {
    va_list argptr;
    searchpath_t    *search;
    size_t len;

    va_start( argptr, fmt );
    len = Q_vsnprintf( fs_gamedir, sizeof( fs_gamedir ), fmt, argptr );
    va_end( argptr );

    if( len >= sizeof( fs_gamedir ) ) {
        Com_EPrintf( "%s: refusing oversize path\n", __func__ );
        return;
    }

#ifdef _WIN32
    FS_ReplaceSeparators( fs_gamedir, '/' );
#endif

    //
    // add the directory to the search path
    //
    if( !( fs_restrict_mask->integer & 1 ) ) {
        search = FS_Malloc( sizeof( searchpath_t ) + len );
        search->mode = mode;
        search->pack = NULL;
        memcpy( search->filename, fs_gamedir, len + 1 );
        search->next = fs_searchpaths;
        fs_searchpaths = search;
    }

    //
    // add any pak files in the format *.pak
    //
    if( !( fs_restrict_mask->integer & 2 ) ) {
        FS_LoadPackFiles( mode, ".pak", FS_LoadPakFile );
    }

#if USE_ZLIB
    //
    // add any zip files in the format *.pkz
    //
    if( !( fs_restrict_mask->integer & 4 ) ) {
        FS_LoadPackFiles( mode, ".pkz", FS_LoadZipFile );
    }
#endif
}

/*
=================
FS_CopyInfo
=================
*/
file_info_t *FS_CopyInfo( const char *name, size_t size, time_t ctime, time_t mtime ) {
    file_info_t    *out;
    size_t            len;

    if( !name ) {
        return NULL;
    }

    len = strlen( name );
    out = FS_Mallocz( sizeof( *out ) + len );
    out->size = size;
    out->ctime = ctime;
    out->mtime = mtime;
    memcpy( out->name, name, len + 1 );

    return out;
}

void **FS_CopyList( void **list, int count ) {
    void **out;
    int i;

    out = FS_Malloc( sizeof( void * ) * ( count + 1 ) );
    for( i = 0; i < count; i++ ) {
        out[i] = list[i];
    }
    out[i] = NULL;

    return out;
}

#if 0
// foo*bar
// foobar
// fooblahbar
static qboolean FS_WildCmp_r( const char *filter, const char *string ) {
    while( *filter && *filter != ';' ) {
        if( *filter == '*' ) {
            return FS_WildCmp_r( filter + 1, string ) ||
                ( *string && FS_WildCmp_r( filter, string + 1 ) );
        }
        if( *filter == '[' ) {
            filter++;
            
            continue;
        }
        if( *filter != '?' && Q_toupper( *filter ) != Q_toupper( *string ) ) {
            return qfalse;
        }
            filter++;
            string++;
    }

    return !*string;
}
#endif

static int FS_WildCmp_r( const char *filter, const char *string ) {
    switch( *filter ) {
    case '\0':
    case ';':
        return !*string;

    case '*':
        return FS_WildCmp_r( filter + 1, string ) || (*string && FS_WildCmp_r( filter, string + 1 ));

    case '?':
        return *string && FS_WildCmp_r( filter + 1, string + 1 );

    default:
        return ((*filter == *string) || (Q_toupper( *filter ) == Q_toupper( *string ))) && FS_WildCmp_r( filter + 1, string + 1 );
    }
}

qboolean FS_WildCmp( const char *filter, const char *string ) {
    do {
        if( FS_WildCmp_r( filter, string ) ) {
            return qtrue;
        }
        filter = strchr( filter, ';' );
        if( filter ) filter++;
    } while( filter );

    return qfalse;
}

qboolean FS_ExtCmp( const char *ext, const char *name ) {
    int        c1, c2;
    const char *e, *n, *l;

    if( !name[0] || !ext[0] ) {
        return qfalse;
    }

    for( l = name; l[1]; l++ )
        ;

    for( e = ext; e[1]; e++ )
        ;

rescan:
    n = l;
    do {
        c1 = *e--;
        c2 = *n--;

        if( c1 == ';' ) {
            break; // matched
        }
        
        if( c1 != c2 ) {
            c1 = Q_tolower( c1 );
            c2 = Q_tolower( c2 );
            if( c1 != c2 ) {
                while( e > ext ) {
                    c1 = *e--;
                    if( c1 == ';' ) {
                        goto rescan;
                    }
                }
                return qfalse;
            }
        }
        if( n < name ) {
            return qfalse;
        }
    } while( e >= ext );

    return qtrue;
}

static int infocmp( const void *p1, const void *p2 ) {
    file_info_t *n1 = *( file_info_t ** )p1;
    file_info_t *n2 = *( file_info_t ** )p2;

    return FS_pathcmp( n1->name, n2->name );
}

static int alphacmp( const void *p1, const void *p2 ) {
    char *s1 = *( char ** )p1;
    char *s2 = *( char ** )p2;

    return FS_pathcmp( s1, s2 );
}

/*
=================
FS_ListFiles
=================
*/
void **FS_ListFiles( const char *path,
                     const char *extension,
                     int        flags,
                     int        *numFiles )
{
    searchpath_t *search;
    void *listedFiles[MAX_LISTED_FILES];
    int count, total;
    char buffer[MAX_OSPATH];
    void **dirlist;
    int dircount;
    void **list;
    int i;
    size_t len, pathlen;
    char *s;
    int valid = -1;

    if( flags & FS_SEARCH_BYFILTER ) {
        if( !extension ) {
            Com_Error( ERR_FATAL, "FS_ListFiles: NULL filter" );
        }
    }

    count = 0;

    if( numFiles ) {
        *numFiles = 0;
    }

    if( !path ) {
        path = "";
        pathlen = 0;
    } else {
        if( *path == '/' ) {
            path++;
        }
        pathlen = strlen( path );
    }

    for( search = fs_searchpaths; search; search = search->next ) {
        if( flags & FS_PATH_MASK ) {
            if( ( flags & search->mode & FS_PATH_MASK ) == 0 ) {
                continue;
            }
        }
        if( search->pack ) {
            if( ( flags & FS_TYPE_MASK ) == FS_TYPE_REAL ) {
                // don't search in paks
                continue;
            }

            // TODO: add directory search support for pak files
            if( ( flags & FS_SEARCHDIRS_MASK ) == FS_SEARCHDIRS_ONLY ) {
                continue;
            }

            if( flags & FS_SEARCH_BYFILTER ) {
                for( i = 0; i < search->pack->numfiles; i++ ) {
                    s = search->pack->files[i].name;
                    
                    // check path
                    if( pathlen ) {
                        if( FS_pathcmpn( s, path, pathlen ) ) {
                            continue;
                        }
                        s += pathlen + 1;
                    }

                    // check filter
                    if( !FS_WildCmp( extension, s ) ) {
                        continue;
                    }

                    // copy filename
                    if( count == MAX_LISTED_FILES ) {
                        break;
                    }

                    if( !( flags & FS_SEARCH_SAVEPATH ) ) {
                        s = COM_SkipPath( s );
                    }
                    if( flags & FS_SEARCH_EXTRAINFO ) {
                        listedFiles[count++] = FS_CopyInfo( s,
                            search->pack->files[i].filelen, 0, 0 );
                    } else {
                        listedFiles[count++] = FS_CopyString( s );
                    }
                }
            } else {
                for( i = 0; i < search->pack->numfiles; i++ ) {
                    s = search->pack->files[i].name;
                    
                    // check path
                    if( pathlen && FS_pathcmpn( s, path, pathlen ) ) {
                        continue;
                    }

                    // check extension
                    if( extension && !FS_ExtCmp( extension, s ) ) {
                        continue;
                    }
                    
                    // copy filename
                    if( count == MAX_LISTED_FILES ) {
                        break;
                    }
                    if( !( flags & FS_SEARCH_SAVEPATH ) ) {
                        s = COM_SkipPath( s );
                    }
                    if( flags & FS_SEARCH_EXTRAINFO ) {
                        listedFiles[count++] = FS_CopyInfo( s,
                            search->pack->files[i].filelen, 0, 0 );
                    } else {
                        listedFiles[count++] = FS_CopyString( s );
                    }
                }
            }
        } else {
            if( ( flags & FS_TYPE_MASK ) == FS_TYPE_PAK ) {
                // don't search in OS filesystem
                continue;
            }

            len = strlen( search->filename );

            if( pathlen ) {
                if( len + pathlen + 1 >= MAX_OSPATH ) {
                    continue;
                }
                if( valid == -1 ) {
                    if( !FS_ValidatePath( path ) ) {
                        FS_DPrintf( "%s: refusing invalid path: %s\n",
                            __func__, path );
                        valid = 0;
                    }
                }
                if( valid == 0 ) {
                    continue;
                }
                memcpy( buffer, search->filename, len );
                buffer[len++] = '/';
                memcpy( buffer + len, path, pathlen + 1 );
                s = buffer;
            } else {
                s = search->filename;
            }

            if( flags & FS_SEARCH_BYFILTER ) {
                len += pathlen + 1;
            }

            dirlist = Sys_ListFiles( s, extension,
                flags|FS_SEARCH_NOSORT, len, &dircount );
            if( !dirlist ) {
                continue;
            }

            if( count + dircount > MAX_LISTED_FILES ) {
                dircount = MAX_LISTED_FILES - count;
            }
            for( i = 0; i < dircount; i++ ) {
                listedFiles[count++] = dirlist[i];
            }

            Z_Free( dirlist );
            
        }
        if( count == MAX_LISTED_FILES ) {
            break;
        }
    }

    if( !count ) {
        return NULL;
    }

    if( flags & FS_SEARCH_EXTRAINFO ) {
        // TODO
        qsort( listedFiles, count, sizeof( listedFiles[0] ), infocmp );
        total = count;
    } else {
        // sort alphabetically (ignoring FS_SEARCH_NOSORT)
        qsort( listedFiles, count, sizeof( listedFiles[0] ), alphacmp );

        // remove duplicates
        total = 1;
        for( i = 1; i < count; i++ ) {
            if( !FS_pathcmp( listedFiles[ i - 1 ], listedFiles[i] ) ) {
                Z_Free( listedFiles[ i - 1 ] );
                listedFiles[i-1] = NULL;
            } else {
                total++;
            }
        }
    }

    list = FS_Malloc( sizeof( void * ) * ( total + 1 ) );

    total = 0;
    for( i = 0; i < count; i++ ) {
        if( listedFiles[i] ) {
            list[total++] = listedFiles[i];
        }
    }
    list[total] = NULL;

    if( numFiles ) {
        *numFiles = total;
    }

    return list;
}

/*
=================
FS_FreeList
=================
*/
void FS_FreeList( void **list ) {
    void **p = list;

    while( *p ) {
        Z_Free( *p++ );
    }

    Z_Free( list );
}

void FS_File_g( const char *path, const char *ext, int flags, genctx_t *ctx ) {
    int i, numFiles;
    void **list;
    char *s, *p;

    list = FS_ListFiles( path, ext, flags, &numFiles );
    if( !list ) {
        return;
    }

    for( i = 0; i < numFiles; i++ ) {
        s = list[i];
        if( flags & 0x80000000 ) {
            p = COM_FileExtension( s );
            *p = 0;
        }
        if( ctx->count < ctx->size && !strncmp( s, ctx->partial, ctx->length ) ) {
            ctx->matches[ctx->count++] = s;
        } else {
            Z_Free( s );
        }
    }

    Z_Free( list );
}

static void print_file_list( const char *path, const char *ext, int flags ) {
    void    **list;
    int     ndirs = 0;
    int     i;

    if( ( list = FS_ListFiles( path, ext, flags, &ndirs ) ) != NULL ) {
        for( i = 0; i < ndirs; i++ ) {
            Com_Printf( "%s\n", ( char * )list[i] );
        }
        FS_FreeList( list );
    }
    Com_Printf( "%i files listed\n", ndirs );
}

/*
============
FS_FDir_f
============
*/
static void FS_FDir_f( void ) {
    int     flags;
    char    *filter;

    if( Cmd_Argc() < 2 ) {
        Com_Printf( "Usage: %s <filter> [full_path]\n", Cmd_Argv( 0 ) );
        return;
    }

    filter = Cmd_Argv( 1 );

    flags = FS_SEARCH_BYFILTER;
    if( Cmd_Argc() > 2 ) {
        flags |= FS_SEARCH_SAVEPATH;
    }

    print_file_list( NULL, filter, flags );
}

/*
============
FS_Dir_f
============
*/
static void FS_Dir_f( void ) {
    char    *path, *ext;

    if( Cmd_Argc() < 2 ) {
        Com_Printf( "Usage: %s <directory> [.extension]\n", Cmd_Argv( 0 ) );
        return;
    }

    path = Cmd_Argv( 1 );
    if( Cmd_Argc() > 2 ) {
        ext = Cmd_Argv( 2 );
    } else {
        ext = NULL;
    }

    print_file_list( path, ext, 0 );
}

/*
============
FS_WhereIs_f
============
*/
static void FS_WhereIs_f( void ) {
    searchpath_t *search;
    pack_t *pak;
    packfile_t *entry;
    unsigned hash;
    char filename[MAX_OSPATH];
    char fullpath[MAX_OSPATH];
    char *path;
    file_info_t info;
    int total;
    size_t len;

    if( Cmd_Argc() < 2 ) {
        Com_Printf( "Usage: %s <path> [all]\n", Cmd_Argv( 0 ) );
        return;
    }

    Cmd_ArgvBuffer( 1, filename, sizeof( filename ) );

    path = FS_ExpandLinks( filename );
    if( path != filename ) {
        Com_Printf( "%s linked to %s\n", filename, path );
    }

    hash = Com_HashPath( path, 0 );
    
    total = 0;
    for( search = fs_searchpaths; search; search = search->next ) {
        // is the element a pak file?
        if( search->pack ) {
            // look through all the pak file elements
            pak = search->pack;
            entry = pak->fileHash[ hash & ( pak->hashSize - 1 ) ];
            for( ; entry; entry = entry->hashNext ) {
                if( !FS_pathcmp( entry->name, path ) ) {
                    Com_Printf( "%s/%s (%"PRIz" bytes)\n", pak->filename,
                        path, entry->filelen );
                    if( Cmd_Argc() < 3 ) {
                        return;
                    }
                    total++;
                }
            }
        } else {
            len = Q_concat( fullpath, sizeof( fullpath ),
                search->filename, "/", path, NULL );
            if( len >= sizeof( fullpath ) ) {
                continue;
            }
            //FS_ConvertToSysPath( fullpath );
            if( Sys_GetPathInfo( fullpath, &info ) ) {
                Com_Printf( "%s/%s (%"PRIz" bytes)\n", search->filename, filename, info.size );
                if( Cmd_Argc() < 3 ) {
                    return;
                }
                total++;
            }
        }
        
    }

    if( total ) {
        Com_Printf( "%d instances of %s\n", total, path );
    } else {
        Com_Printf( "%s was not found\n", path );
    }
}

/*
============
FS_Path_f
============
*/
void FS_Path_f( void ) {
    searchpath_t *s;
    int numFilesInPAK = 0;
#if USE_ZLIB
    int numFilesInPK2 = 0;
#endif

    Com_Printf( "Current search path:\n" );
    for( s = fs_searchpaths; s; s = s->next ) {
        if( s->pack ) {
#if USE_ZLIB
            if( s->pack->zFile )
                numFilesInPK2 += s->pack->numfiles;
            else
#endif
                numFilesInPAK += s->pack->numfiles;
            Com_Printf( "%s (%i files)\n", s->pack->filename, s->pack->numfiles );
        } else {
            Com_Printf( "%s\n", s->filename );
        }
    }

    if( numFilesInPAK ) {
        Com_Printf( "%i files in PAK files\n", numFilesInPAK );
    }

#if USE_ZLIB
    if( numFilesInPK2 ) {
        Com_Printf( "%i files in PKZ files\n", numFilesInPK2 );
    }
#endif
}

/*
================
FS_Stats_f
================
*/
static void FS_Stats_f( void ) {
    searchpath_t *path;
    pack_t *pack, *maxpack = NULL;
    packfile_t *file, *max = NULL;
    int i;
    int len, maxLen = 0;
    int totalHashSize, totalLen;

    totalHashSize = totalLen = 0;
    for( path = fs_searchpaths; path; path = path->next ) {
        if( !( pack = path->pack ) ) {
            continue;
        }
        for( i = 0; i < pack->hashSize; i++ ) {
            if( !( file = pack->fileHash[i] ) ) {
                continue;
            }
            len = 0;
            for( ; file ; file = file->hashNext ) {
                len++;
            }
            if( maxLen < len ) {
                max = pack->fileHash[i];
                maxpack = pack;
                maxLen = len;
            }
            totalLen += len;
            totalHashSize++;
        }
        //totalHashSize += pack->hashSize;
    }

#ifdef _DEBUG
#if USE_LOADBUF
    Com_Printf( "LoadFile counter: %d\n", loadCount );
    Com_Printf( "Static LoadFile counter: %d\n", loadCountStatic );
#endif
    Com_Printf( "Total calls to OpenFileRead: %d\n", fs_count_read );
    Com_Printf( "Total path comparsions: %d\n", fs_count_strcmp );
    Com_Printf( "Total calls to fopen: %d\n", fs_count_open );

    if( !totalHashSize ) {
        Com_Printf( "No stats to display\n" );
        return;
    }

    Com_Printf( "Maximum hash bucket length is %d, average is %.2f\n", maxLen, ( float )totalLen / totalHashSize );
    if( max ) {
        Com_Printf( "Dumping longest bucket (%s):\n", maxpack->filename );
        for( file = max; file ; file = file->hashNext ) {
            Com_Printf( "%s\n", file->name );
        }
    }
#endif // _DEBUG
}

static void FS_Link_g( genctx_t *ctx ) {
    symlink_t *link;

    for( link = fs_links; link; link = link->next ) {
        if( !Prompt_AddMatch( ctx, link->name ) ) {
            break;
        }
    }
}

static void FS_Link_c( genctx_t *ctx, int argnum ) {
    if( argnum == 1 ) {
        FS_Link_g( ctx );
    }
}

static void free_all_links( void ) {
    symlink_t *link, *next;

    for( link = fs_links; link; link = next ) {
        next = link->next;
        Z_Free( link->target );
        Z_Free( link );
    }
    fs_links = NULL;
}

static void FS_UnLink_f( void ) {
    static const cmd_option_t options[] = {
        { "a", "all", "delete all links" },
        { "h", "help", "display this message" },
        { NULL }
    };
    symlink_t *link, **next_p;
    char *name;
    int c;

    while( ( c = Cmd_ParseOptions( options ) ) != -1 ) {
        switch( c ) {
        case 'h':
            Com_Printf( "Usage: %s [-ah] <name> -- delete symbolic link\n",
                Cmd_Argv( 0 ) );
            Cmd_PrintHelp( options );
            return;
        case 'a':
            free_all_links();
            Com_Printf( "Deleted all symbolic links.\n" );
            return;
        default:
            return;
        }
    }

    name = cmd_optarg;
    if( !name[0] ) {
        Com_Printf( "Missing name argument.\n"
            "Try '%s --help' for more information.\n", Cmd_Argv( 0 ) );
        return;
    }

    for( link = fs_links, next_p = &fs_links; link; link = link->next ) {
        if( !FS_pathcmp( link->name, name ) ) {
            break;
        }
        next_p = &link->next;
    }
    if( !link ) {
        Com_Printf( "Symbolic link %s does not exist.\n", name );
        return;
    }

    *next_p = link->next;
    Z_Free( link->target );
    Z_Free( link );
}

static void FS_Link_f( void ) {
    int argc, count;
    size_t len;
    symlink_t *link;
    char *name, *target;

    argc = Cmd_Argc();
    if( argc == 1 ) {
        for( link = fs_links, count = 0; link; link = link->next, count++ ) {
            Com_Printf( "%s --> %s\n", link->name, link->target );
        }
        Com_Printf( "------------------\n"
                "%d symbolic links listed.\n", count );
        return;
    }
    if( argc != 3 ) {
        Com_Printf( "Usage: %s <name> <target>\n"
            "Creates symbolic link to target with the specified name.\n"
            "Virtual quake paths are accepted.\n"
            "Links are effective only for reading.\n",
            Cmd_Argv( 0 ) );
        return;
    }

    name = Cmd_Argv( 1 );
    for( link = fs_links; link; link = link->next ) {
        if( !FS_pathcmp( link->name, name ) ) {
            Z_Free( link->target );
            goto update;
        }
    }

    len = strlen( name );
    link = FS_Malloc( sizeof( *link ) + len );
    memcpy( link->name, name, len + 1 );
    link->namelen = len;
    link->next = fs_links;
    fs_links = link;

update:
    target = Cmd_Argv( 2 );
    link->target = FS_CopyString( target );
    link->targlen = strlen( target );
}

static void FS_FreeSearchPath( searchpath_t *path ) {
    pack_t *pak;

    if( ( pak = path->pack ) != NULL ) {
#if USE_ZLIB
        if( pak->zFile ) {
            unzClose( pak->zFile );
        } else
#endif
        {
            fclose( pak->fp );
        }
        Z_Free( pak );
    }

    Z_Free( path );
}

/*
================
FS_Shutdown
================
*/
void FS_Shutdown( qboolean total ) {
    searchpath_t    *path, *next;
    file_t       *file;
    int             i;

    if( !fs_searchpaths ) {
        return;
    }

    if( total ) {
        // close file handles
        for( i = 0, file = fs_files; i < MAX_FILE_HANDLES; i++, file++ ) {
            if( file->type != FS_FREE ) {
                Com_WPrintf( "%s: closing handle %d\n", __func__, i + 1 );
                FS_FCloseFile( i + 1 );
            }
        }

        // free symbolic links
        free_all_links();
    }

    // free search paths
    for( path = fs_searchpaths; path; path = next ) {
        next = path->next;
        FS_FreeSearchPath( path );
    }

    fs_searchpaths = NULL;

    if( total ) {
        Z_LeakTest( TAG_FILESYSTEM );
    }

    Cmd_RemoveCommand( "path" );
    Cmd_RemoveCommand( "fdir" );
    Cmd_RemoveCommand( "dir" );
    Cmd_RemoveCommand( "fs_stats" );
    Cmd_RemoveCommand( "link" );
    Cmd_RemoveCommand( "unlink" );
}

/*
================
FS_DefaultGamedir
================
*/
static void FS_DefaultGamedir( void ) {
    if( sys_homedir->string[0] ) {
        FS_AddGameDirectory( FS_PATH_BASE|FS_PATH_GAME,
            "%s/"BASEGAME, sys_homedir->string );
    }

    Cvar_Set( "game", "" );
    Cvar_Set( "gamedir", "" );

    Cvar_Set( "fs_gamedir", fs_gamedir );
}


/*
================
FS_SetupGamedir

Sets the gamedir and path to a different directory.
================
*/
static void FS_SetupGamedir( void ) {
    fs_game = Cvar_Get( "game", "", CVAR_FILES|CVAR_LATCH|CVAR_SERVERINFO );

    cvar_modified &= ~CVAR_FILES;

    if( !fs_game->string[0] || !FS_strcmp( fs_game->string, BASEGAME ) ) {
        FS_DefaultGamedir();
        return;
    }

    if( !FS_ValidatePath( fs_game->string ) ||
        strchr( fs_game->string, '/' ) ||
        strchr( fs_game->string, '\\' ) )
    {
        Com_WPrintf( "Gamedir should be a single filename, not a path.\n" );
        FS_DefaultGamedir();
        return;
    }

    // this one is left for compatibility with server browsers, etc
    Cvar_FullSet( "gamedir", fs_game->string, CVAR_ROM|CVAR_SERVERINFO, FROM_CODE );

    FS_AddGameDirectory( FS_PATH_GAME, "%s/%s", sys_basedir->string, fs_game->string );

    // home paths override system paths
    if( sys_homedir->string[0] ) {
        FS_AddGameDirectory( FS_PATH_BASE, "%s/"BASEGAME, sys_homedir->string );
        FS_AddGameDirectory( FS_PATH_GAME, "%s/%s", sys_homedir->string, fs_game->string );
    }
    
    Cvar_Set( "fs_gamedir", fs_gamedir );
}

qboolean FS_SafeToRestart( void ) {
    file_t   *file;
    int         i;
    
    // make sure no files are opened for reading
    for( i = 0, file = fs_files; i < MAX_FILE_HANDLES; i++, file++ ) {
        if( file->type == FS_FREE ) {
            continue;
        }
        if( file->mode == FS_MODE_READ ) {
            return qfalse;
        }
    }

    return qtrue;
}

/*
================
FS_Restart
================
*/
void FS_Restart( void ) {
    file_t    *file;
    int             i;
    fileHandle_t temp;
    searchpath_t *path, *next;
    
    Com_Printf( "---------- FS_Restart ----------\n" );
    
    // temporary disable logfile
    temp = com_logFile;
    com_logFile = 0;

    // make sure no files from paks are opened
    for( i = 0, file = fs_files; i < MAX_FILE_HANDLES; i++, file++ ) {
        switch( file->type ) {
        case FS_FREE:
        case FS_REAL:
#if USE_ZLIB
        case FS_GZIP:
#endif
            break;
        default:
            Com_Error( ERR_FATAL, "%s: closing handle %d", __func__, i + 1 );
        }
    }
    
    if( fs_restrict_mask->latched_string ) {
        // perform full reset
        FS_Shutdown( qfalse );
        FS_Init();
    } else {
        // just change gamedir
        for( path = fs_searchpaths; path != fs_base_searchpaths; path = next ) {
            next = path->next;
            FS_FreeSearchPath( path );
        }

        fs_searchpaths = fs_base_searchpaths;

        FS_SetupGamedir();
        FS_Path_f();
    }

    // re-enable logfile
    com_logFile = temp;
    
    Com_Printf( "--------------------------------\n" );
}

/*
============
FS_Restart_f
 
Console command to re-start the file system.
============
*/
static void FS_Restart_f( void ) {
    if( !FS_SafeToRestart() ) {
        Com_Printf( "Can't \"%s\", there are some open file handles.\n", Cmd_Argv( 0 ) );
        return;
    }
    
    CL_RestartFilesystem();
}

static const cmdreg_t c_fs[] = {
    { "path", FS_Path_f },
    { "fdir", FS_FDir_f },
    { "dir", FS_Dir_f },
    { "fs_stats", FS_Stats_f },
    { "whereis", FS_WhereIs_f },
    { "link", FS_Link_f, FS_Link_c },
    { "unlink", FS_UnLink_f, FS_Link_c },
    { "fs_restart", FS_Restart_f },

    { NULL }
};

/*
================
FS_Init
================
*/
void FS_Init( void ) {
    unsigned start, end;

    start = Sys_Milliseconds();

    Com_Printf( "---------- FS_Init ----------\n" );

    Cmd_Register( c_fs );

#ifdef _DEBUG
    fs_debug = Cvar_Get( "fs_debug", "0", 0 );
#endif
    fs_restrict_mask = Cvar_Get( "fs_restrict_mask", "0", CVAR_NOSET );
    Cvar_Get( "fs_gamedir", "", CVAR_ROM );

    if( ( fs_restrict_mask->integer & 7 ) == 7 ) {
        Com_WPrintf( "Invalid fs_restrict_mask value %d. "
            "Falling back to default.\n",
                fs_restrict_mask->integer );
        Cvar_Set( "fs_restrict_mask", "0" );
    }

    // start up with baseq2 by default
    FS_AddGameDirectory( FS_PATH_BASE|FS_PATH_GAME, "%s/"BASEGAME, sys_basedir->string );

    fs_base_searchpaths = fs_searchpaths;

    // check for game override
    FS_SetupGamedir();

    FS_Path_f();

    end = Sys_Milliseconds();
    Com_DPrintf( "%i msec to init filesystem\n", end - start );
    Com_Printf( "-----------------------------\n" );
}

/*
================
FS_NeedRestart
================
*/
qboolean FS_NeedRestart( void ) {
    if( cvar_modified & CVAR_FILES ) {
        return qtrue;
    }
    if( fs_game->latched_string || fs_restrict_mask->latched_string ) {
        return qtrue;
    }
    return qfalse;
}

