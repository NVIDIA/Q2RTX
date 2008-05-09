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
#if USE_ZLIB
#include <zlib.h>
#include "unzip.h"
#endif
#include <errno.h>

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

#define MAX_FILES_IN_PK2	0x4000
#define MAX_FILE_HANDLES	8

// macros for dealing portably with files at OS level
#ifdef _WIN32
#define FS_strcmp  Q_strcasecmp
#define FS_strncmp  Q_strncasecmp
#else
#define FS_strcmp  strcmp
#define FS_strncmp  strncmp
#endif

#define	MAX_READ	0x40000		// read in blocks of 256k
#define	MAX_WRITE	0x40000		// write in blocks of 256k


//
// in memory
//

typedef struct packfile_s {
	char	*name;
	size_t	filepos;
	size_t	filelen;

	struct packfile_s *hashNext;
} packfile_t;

typedef struct pack_s {
#if USE_ZLIB
	unzFile zFile;
#endif
	FILE	*fp;
	int		numfiles;
	packfile_t	*files;
	packfile_t	**fileHash;
	int			hashSize;
	char	filename[1];
} pack_t;

typedef struct searchpath_s {
	struct searchpath_s *next;
    int mode;
	struct pack_s	*pack;		// only one of filename / pack will be used
	char	        filename[1];
} searchpath_t;

typedef enum fsFileType_e {
	FS_FREE,
	FS_REAL,
	FS_PAK,
#if USE_ZLIB
	FS_PK2,
	FS_GZIP,
#endif
	FS_BAD
} fsFileType_t;

typedef struct fsFile_s {
	char fullpath[MAX_OSPATH];
	fsFileType_t type;
	unsigned mode;
	FILE *fp;
#if USE_ZLIB
	void *zfp;
#endif
	packfile_t	*pak;
	qboolean unique;
	size_t	length;
} fsFile_t;

typedef struct fsLink_s {
    char *target;
    size_t targlen, namelen;
    struct fsLink_s *next;
    char name[1];
} fsLink_t;

// these point to user home directory
static char		fs_gamedir[MAX_OSPATH];
//static char		fs_basedir[MAX_OSPATH];

static cvar_t	*fs_debug;
static cvar_t	*fs_restrict_mask;

static searchpath_t	*fs_searchpaths;
static searchpath_t	*fs_base_searchpaths;

static fsLink_t     *fs_links;

static fsFile_t		fs_files[MAX_FILE_HANDLES];

static qboolean		fs_fileFromPak;

static int          fs_count_read, fs_count_strcmp, fs_count_open;

cvar_t	*fs_game;

fsAPI_t		fs;

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
FS_DPrintf
================
*/
static void FS_DPrintf( char *format, ... ) {
	va_list argptr;
	char string[MAXPRINTMSG];

	if( !fs_debug || !fs_debug->integer ) {
		return;
	}

	va_start( argptr, format );
	Q_vsnprintf( string, sizeof( string ), format, argptr );
	va_end( argptr );

	Com_Printf( S_COLOR_CYAN "%s", string );
}

/*
================
FS_AllocHandle
================
*/
static fsFile_t *FS_AllocHandle( fileHandle_t *f ) {
	fsFile_t *file;
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
static fsFile_t *FS_FileForHandle( fileHandle_t f ) {
	fsFile_t *file;

	if( f <= 0 || f >= MAX_FILE_HANDLES + 1 ) {
		Com_Error( ERR_FATAL, "%s: invalid handle: %i", __func__, f );
	}

	file = &fs_files[f - 1];
	if( file->type == FS_FREE ) {
		Com_Error( ERR_FATAL, "%s: free handle: %i", __func__, f );
	}

	if( file->type < FS_FREE || file->type >= FS_BAD ) {
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
		if( *s == ':' ) {
			// check for "X:\"
			if( s[1] == '\\' || s[1] == '/' ) {
				return qfalse;
			}
		}
		s++;
	}

    // check length
	if( s - start > MAX_OSPATH ) {
		return qfalse;
	}

	return qtrue;
}

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


/*
================
FS_GetFileLength

Returns current length for files opened for writing.
Returns cached length for files opened for reading.
Returns compressed length for GZIP files.
================
*/
size_t FS_GetFileLength( fileHandle_t f ) {
	fsFile_t *file = FS_FileForHandle( f );
    fsFileInfo_t info;

	switch( file->type ) {
	case FS_REAL:
        if( !Sys_GetFileInfo( file->fp, &info ) ) {
            return INVALID_LENGTH;
        }
	    return info.size;
	case FS_PAK:
		return file->length;
#if USE_ZLIB
	case FS_PK2:
		return file->length;
	case FS_GZIP:
        return INVALID_LENGTH;
#endif
    default:
	    Com_Error( ERR_FATAL, "%s: bad file type", __func__ );
	}

    return INVALID_LENGTH;
}

const char *FS_GetFileFullPath( fileHandle_t f ) {
	return ( FS_FileForHandle( f ) )->fullpath;
}

/*
============
FS_CreatePath

Creates any directories needed to store the given filename.
Expects a fully qualified quake path (i.e. with / separators).
============
*/
void FS_CreatePath( const char *path ) {
	char buffer[MAX_OSPATH];
	char *ofs;

	Q_strncpyz( buffer, path, sizeof( buffer ) );

	FS_DPrintf( "%s: %s\n", __func__, buffer );
	
	for( ofs = buffer + 1; *ofs; ofs++ ) {
		if( *ofs == '/' ) {	
			// create the directory
			*ofs = 0;
			Sys_Mkdir( buffer );
			*ofs = '/';
		}
	}
}

qboolean FS_FilterFile( fileHandle_t f ) {
#if USE_ZLIB
	fsFile_t *file = FS_FileForHandle( f );
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
	fsFile_t *file = FS_FileForHandle( f );

	FS_DPrintf( "%s: %s\n", __func__, file->fullpath );

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

	// don't clear name and mode, in case
    // this handle will be reopened later
	file->type = FS_FREE;
	file->fp = NULL;
#if USE_ZLIB
	file->zfp = NULL;
#endif
	file->pak = NULL;
	file->unique = qfalse;
}

/*
============
FS_FOpenFileWrite
============
*/
static size_t FS_FOpenFileWrite( fsFile_t *file, const char *name ) {
	FILE *fp;
	char *modeStr;
	unsigned mode;

	if( ( file->mode & FS_PATH_MASK ) == FS_PATH_BASE ) {
        if( sys_homedir->string[0] ) {
    		Q_concat( file->fullpath, sizeof( file->fullpath ),
	    		sys_homedir->string, "/" BASEGAME "/", name, NULL );
        } else {
    		Q_concat( file->fullpath, sizeof( file->fullpath ),
	    		sys_basedir->string, "/" BASEGAME "/", name, NULL );
        }
	} else {
		Q_concat( file->fullpath, sizeof( file->fullpath ),
			fs_gamedir, "/", name, NULL );
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
            __func__, file->fullpath );
		modeStr = NULL;
		break;
	}

	FS_CreatePath( file->fullpath );

	fp = fopen( file->fullpath, modeStr );
	if( !fp ) {
		FS_DPrintf( "%s: %s: fopen(%s): %s\n",
			__func__, file->fullpath, modeStr, strerror( errno ) );
		return INVALID_LENGTH;
	}

#ifdef __unix__
    if( !Sys_GetFileInfo( fp, NULL ) ) {
        FS_DPrintf( "%s: %s: couldn't get info\n",
            __func__, file->fullpath );
        fclose( fp );
        return INVALID_LENGTH;
    }
#endif

	FS_DPrintf( "%s: %s: succeeded\n", __func__, file->fullpath );

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

/*
===========
FS_FOpenFileRead

Finds the file in the search path.
Fills fsFile_t and returns file length.
In case of GZIP files, returns *raw* (compressed) length!
Used for streaming data out of either a pak file or
a seperate file.
===========
*/
static size_t FS_FOpenFileRead( fsFile_t *file, const char *name, qboolean unique ) {
    searchpath_t    *search;
	pack_t			*pak;
	unsigned		hash;
	packfile_t		*entry;
	FILE			*fp;
    fsFileInfo_t    info;

	fs_fileFromPak = qfalse;
    fs_count_read++;

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
                fs_count_strcmp++;
				if( !Q_stricmp( entry->name, name ) ) {	
					// found it!
					fs_fileFromPak = qtrue;

					Q_concat( file->fullpath, sizeof( file->fullpath ), pak->filename, "/", entry->name, NULL );

					// open a new file on the pakfile
#if USE_ZLIB
					if( pak->zFile ) {
                        void *zfp;

						if( unique ) {
							zfp = unzReOpen( pak->filename, pak->zFile );
							if( !zfp ) {
								Com_Error( ERR_FATAL, "%s: %s: unzReOpen failed", __func__, pak->filename );
							}
						} else {
							zfp = pak->zFile;
						}
						if( unzSetCurrentFileInfoPosition( zfp, entry->filepos ) == -1 ) {
							Com_Error( ERR_FATAL, "%s: %s/%s: unzSetCurrentFileInfoPosition failed", __func__, pak->filename, entry->name );
						}
						if( unzOpenCurrentFile( zfp ) != UNZ_OK ) {
							Com_Error( ERR_FATAL, "%s: %s/%s: unzReOpen failed", __func__, pak->filename, entry->name );
						}

						file->zfp = zfp;
						file->type = FS_PK2;
					} else
#endif
                    {
						if( unique ) {
							fp = fopen( pak->filename, "rb" );
							if( !fp ) {
								Com_Error( ERR_FATAL, "%s: couldn't reopen %s", __func__, pak->filename );
							}
						} else {
							fp = pak->fp;
						}

						fseek( fp, entry->filepos, SEEK_SET );

						file->fp = fp;
						file->type = FS_PAK;
					}

					file->pak = entry;
					file->length = entry->filelen;
					file->unique = unique;

					FS_DPrintf( "%s: %s/%s: succeeded\n", __func__, pak->filename, entry->name );

					return file->length;
				}
			}
		} else {
			if( ( file->mode & FS_TYPE_MASK ) == FS_TYPE_PAK ) {
				continue;
			}
	// check a file in the directory tree
			Q_concat( file->fullpath, sizeof( file->fullpath ),
				search->filename, "/", name, NULL );

            fs_count_open++;
			fp = fopen( file->fullpath, "rb" );
			if( !fp ) {
				continue;
			}

            if( !Sys_GetFileInfo( fp, &info ) ) {
                FS_DPrintf( "%s: %s: couldn't get info\n",
                    __func__, file->fullpath );
                fclose( fp );
                continue;
            }

			file->fp = fp;
			file->type = FS_REAL;
			file->unique = qtrue;
			file->length = info.size;

			FS_DPrintf( "%s: %s: succeeded\n", __func__, file->fullpath );

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
	size_t	block, remaining = len, read = 0;
	byte	*buf = (byte *)buffer;
	fsFile_t	*file = FS_FileForHandle( hFile );

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
	fsFile_t	*file = FS_FileForHandle( f );
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
	fsFile_t *file = FS_FileForHandle( f );

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
	size_t	block, remaining = len, write = 0;
	byte	*buf = (byte *)buffer;
	fsFile_t	*file = FS_FileForHandle( hFile );

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
    static char        buffer[MAX_OSPATH];
    fsLink_t    *l;
    size_t      length;

    length = strlen( filename );
    for( l = fs_links; l; l = l->next ) {
        if( l->namelen > length ) {
            continue;
        }
        if( !Q_stricmpn( l->name, filename, l->namelen ) ) {
            if( l->targlen + length - l->namelen >= MAX_OSPATH ) {
                FS_DPrintf( "%s: %s: MAX_OSPATH exceeded\n", __func__, filename );
                return ( char * )filename;
            }
            memcpy( buffer, l->target, l->targlen );
            memcpy( buffer + l->targlen, filename + l->namelen,
                length - l->namelen + 1 );
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
	fsFile_t	*file;
	fileHandle_t hFile;
	size_t		ret = INVALID_LENGTH;

	if( !name || !f ) {
		Com_Error( ERR_FATAL, "FS_FOpenFile: NULL" );
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

	if( !FS_ValidatePath( name ) ) {
		FS_DPrintf( "FS_FOpenFile: refusing invalid path: %s\n", name );
		return ret;
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
		Com_Error( ERR_FATAL, "FS_FOpenFile: illegal mode: %u", mode );
		break;
	}

	// if succeeded, store file handle
	if( ret != -1 ) {
		*f = hFile;
	}

	return ret;
}


/*
============
FS_Tell
============
*/
int FS_Tell( fileHandle_t f ) {
	fsFile_t *file = FS_FileForHandle( f );
	int length;

	switch( file->type ) {
	case FS_REAL:
		length = ftell( file->fp );
		break;
	case FS_PAK:
		length = ftell( file->fp ) - file->pak->filepos;
		break;
#if USE_ZLIB
	case FS_GZIP:
		length = gztell( file->zfp );
		break;
	case FS_PK2:
		length = unztell( file->zfp );
		break;
#endif
	default:
		length = -1;
		break;
	}

	return length;
}

/*
============
FS_RawTell
============
*/
int FS_RawTell( fileHandle_t f ) {
	fsFile_t *file = FS_FileForHandle( f );
	int length;

	switch( file->type ) {
	case FS_REAL:
		length = ftell( file->fp );
		break;
	case FS_PAK:
		length = ftell( file->fp ) - file->pak->filepos;
		break;
#if USE_ZLIB
	case FS_GZIP:
		length = ftell( file->fp );
		break;
	case FS_PK2:
		length = unztell( file->zfp );
		break;
#endif
	default:
		length = -1;
		break;
	}

	return length;
}

#define MAX_LOAD_BUFFER		0x100000		// 1 MiB

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

/*
============
FS_LoadFile

Filenames are relative to the quake search path
a null buffer will just return the file length without loading
============
*/
size_t FS_LoadFileEx( const char *path, void **buffer, int flags, memtag_t tag ) {
	fsFile_t *file;
	fileHandle_t f;
	byte	*buf;
	size_t	len;

	if( !path ) {
		Com_Error( ERR_FATAL, "FS_LoadFile: NULL" );
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

	if( !FS_ValidatePath( path ) ) {
		FS_DPrintf( "FS_LoadFile: refusing invalid path: %s\n", path );
		return INVALID_LENGTH;
	}

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
                Com_EPrintf( "FS_LoadFile: error reading file: %s\n", path );
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

    if( loadInuse + length <= MAX_LOAD_BUFFER && !( fs_restrict_mask->integer & 16 ) ) {
        buf = &loadBuffer[loadInuse];
        loadLast = buf;
        loadSaved = loadInuse;
        loadInuse += length;
        loadInuse = ( loadInuse + 31 ) & ~31;
        loadStack++;
        loadCountStatic++;
    } else {
       // Com_Printf(S_COLOR_MAGENTA"alloc %d\n",length);
        buf = FS_Malloc( length );
		loadCount++;
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
		Com_Error( ERR_FATAL, "FS_FreeFile: NULL" );
	}
	if( ( byte * )buffer >= loadBuffer && ( byte * )buffer < loadBuffer + MAX_LOAD_BUFFER ) {
		if( loadStack == 0 ) {
			Com_Error( ERR_FATAL, "FS_FreeFile: loadStack is zero" );
		}
		loadStack--;
		if( loadStack == 0 ) {
			loadInuse = 0;
      //      Com_Printf(S_COLOR_MAGENTA"clear\n");
		} else if( buffer == loadLast ) {
            loadInuse = loadSaved;
    //        Com_Printf(S_COLOR_MAGENTA"partial\n");
        }
	} else {
		Z_Free( buffer );
	}
}

/*
=============
FS_CopyFile
=============
*/
qboolean FS_CopyFile( const char *src, const char *dst ) {
	fileHandle_t hSrc, hDst;
	byte	buffer[MAX_READ];
	size_t	len, size;

	FS_DPrintf( "FS_CopyFile( '%s', '%s' )\n", src, dst );

	size = FS_FOpenFile( src, &hSrc, FS_MODE_READ );
	if( !hSrc ) {
		return qfalse;
	}

	FS_FOpenFile( dst, &hDst, FS_MODE_WRITE );
	if( !hDst ) {
		FS_FCloseFile( hSrc );
		return qfalse;
	}

	while( size ) {
		len = size;
		if( len > sizeof( buffer ) ) {
			len = sizeof( buffer );
		}
		if( !( len = FS_Read( buffer, len, hSrc ) ) ) {
			break;
		}
		FS_Write( buffer, len, hDst );
		size -= len;
	}

	FS_FCloseFile( hSrc );
	FS_FCloseFile( hDst );

	if( size ) {
		return qfalse;
	}

	return qtrue;
}

/*
================
FS_RemoveFile
================
*/
qboolean FS_RemoveFile( const char *filename ) {
	char path[MAX_OSPATH];

    if( *filename == '/' ) {
        filename++;
    }

	if( !FS_ValidatePath( filename ) ) {
		FS_DPrintf( "FS_RemoveFile: refusing invalid path: %s\n", filename );
		return qfalse;
	}

	Q_concat( path, sizeof( path ), fs_gamedir, "/", filename, NULL );
	//FS_ConvertToSysPath( path );

	if( !Sys_RemoveFile( path ) ) {
		return qfalse;
	}

	return qtrue;
}

/*
================
FS_RemoveFile
================
*/
qboolean FS_RenameFile( const char *from, const char *to ) {
	char frompath[MAX_OSPATH];
	char topath[MAX_OSPATH];

    if( *from == '/' ) {
        from++;
    }
    if( *to == '/' ) {
        to++;
    }

	if( !FS_ValidatePath( from ) || !FS_ValidatePath( to ) ) {
		FS_DPrintf( "FS_RenameFile: refusing invalid path: %s to %s\n", from, to );
		return qfalse;
	}

	Q_concat( frompath, sizeof( frompath ), fs_gamedir, "/", from, NULL );
	Q_concat( topath, sizeof( topath ), fs_gamedir, "/", to, NULL );

	if( !Sys_RenameFile( frompath, topath ) ) {
		FS_DPrintf( "FS_RenameFile: rename failed: %s to %s\n", frompath, topath );
		return qfalse;
	}

	return qtrue;
}

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
	len = Q_vsnprintf( string, sizeof( string ), format, argptr );
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
	dpackheader_t	header;
	int				i;
	packfile_t		*file;
	dpackfile_t		*dfile;
	int				numpackfiles;
	char			*names;
	size_t			namesLength;
	pack_t			*pack;
	FILE			*packhandle;
	dpackfile_t		info[MAX_FILES_IN_PACK];
	int				hashSize;
	unsigned		hash;
	size_t			len;

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
	int				i;
	packfile_t		*file;
	char			*names;
	int				numFiles;
	pack_t			*pack;
	unzFile			zFile;
	unz_global_info	zGlobalInfo;
	unz_file_info	zInfo;
	char			name[MAX_QPATH];
	size_t			namesLength;
	int				hashSize;
	unsigned		hash;
	size_t			len;

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
	int				i;
	searchpath_t	*search;
	pack_t			*pak;
	void            **list;
	int				numFiles;
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
	searchpath_t	*search;
	size_t len;

    va_start( argptr, fmt );
	len = Q_vsnprintf( fs_gamedir, sizeof( fs_gamedir ), fmt, argptr );
    va_end( argptr );

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
fsFileInfo_t *FS_CopyInfo( const char *name, size_t size, time_t ctime, time_t mtime ) {
	fsFileInfo_t	*out;
	size_t		    len;

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
	int		c1, c2;
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
    fsFileInfo_t *n1 = *( fsFileInfo_t ** )p1;
    fsFileInfo_t *n2 = *( fsFileInfo_t ** )p2;

    return Q_stricmp( n1->name, n2->name );
}

static int alphacmp( const void *p1, const void *p2 ) {
    char *s1 = *( char ** )p1;
    char *s2 = *( char ** )p2;

    return Q_stricmp( s1, s2 );
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
        if( !FS_ValidatePath( path ) ) {
		    FS_DPrintf( "%s: refusing invalid path: %s\n", __func__, path );
            return NULL;
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
                        if( Q_stricmpn( s, path, pathlen ) ) {
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
                    if( pathlen && Q_stricmpn( s, path, pathlen ) ) {
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
            // FIXME: use Q_stricmp instead of FS_strcmp here?
            if( !Q_stricmp( listedFiles[ i - 1 ], listedFiles[i] ) ) {
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


/*
=================
FS_CopyFile_f

extract file from *.pak, *.pk2 or *.gz
=================
*/
static void FS_CopyFile_f( void ) {
	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <sourcePath> <destPath>\n", Cmd_Argv( 0 ) );
		return;
	}

	if( FS_CopyFile( Cmd_Argv( 1 ), Cmd_Argv( 2 ) ) ) {
		Com_Printf( "File copied successfully\n" );
	} else {
		Com_Printf( "Failed to copy file\n" );
	}
}

/*
============
FS_FDir_f
============
*/
static void FS_FDir_f( void ) {
	void	**list;
	int		ndirs = 0;
	int     i;
    char    *filter;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <filter> [fullPath]\n", Cmd_Argv( 0 ) );
		return;
	}

    filter = Cmd_Argv( 1 );

	i = FS_SEARCH_BYFILTER;
	if( Cmd_Argc() > 2 ) {
		i |= FS_SEARCH_SAVEPATH;
	}

	if( ( list = FS_ListFiles( NULL, filter, i, &ndirs ) ) != NULL ) {
		for( i = 0; i < ndirs; i++ ) {
			Com_Printf( "%s\n", ( char * )list[i] );
		}
		FS_FreeList( list );
	}
	Com_Printf( "%i files listed\n", ndirs );
}

/*
============
FS_Dir_f
============
*/
static void FS_Dir_f( void ) {
	void	**list;
	int		ndirs = 0;
	int     i;
    char    *ext;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <directory> [.extension]\n", Cmd_Argv( 0 ) );
		return;
	}
    if( Cmd_Argc() > 2 ) {
        ext = Cmd_Argv( 2 );
    } else {
        ext = NULL;
    }
    list = FS_ListFiles( Cmd_Argv( 1 ), ext, 0, &ndirs );
	if( list ) {
		for( i = 0; i < ndirs; i++ ) {
			Com_Printf( "%s\n", ( char * )list[i] );
		}
		FS_FreeList( list );
	}
	Com_Printf( "%i files listed\n", ndirs );
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
    fsFileInfo_t info;
    int total;

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
				if( !Q_stricmp( entry->name, path ) ) {
                    Com_Printf( "%s/%s (%"PRIz" bytes)\n", pak->filename,
                        path, entry->filelen );
	                if( Cmd_Argc() < 3 ) {
                        return;
                    }
                    total++;
                }
            }
        } else {
            Q_concat( fullpath, sizeof( fullpath ),
                search->filename, "/", path, NULL );
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
	searchpath_t	*s;
	int				numFilesInPAK = 0;
#if USE_ZLIB
	int				numFilesInPK2 = 0;
#endif

	Com_Printf( "Current search path:\n" );
	for( s = fs_searchpaths; s; s = s->next ) {
		if( s->pack ) {
#if USE_ZLIB
			if( s->pack->zFile ) {
				numFilesInPK2 += s->pack->numfiles;
			} else
#endif
            {
				numFilesInPAK += s->pack->numfiles;
			}
		}

		if( s->pack )
			Com_Printf( "%s (%i files)\n", s->pack->filename, s->pack->numfiles );
		else
			Com_Printf( "%s\n", s->filename );
	}

	if( !( fs_restrict_mask->integer & 2 ) ) {
		Com_Printf( "%i files in PAK files\n", numFilesInPAK );
	}

#if USE_ZLIB
	if( !( fs_restrict_mask->integer & 4 ) ) {
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

	Com_Printf( "LoadFile counter: %d\n", loadCount );
	Com_Printf( "Static LoadFile counter: %d\n", loadCountStatic );
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
}

static void FS_Link_g( genctx_t *ctx ) {
    fsLink_t *link;

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

static void FS_UnLink_f( void ) {
    static const cmd_option_t options[] = {
        { "a", "all", "delete all links" },
        { "h", "help", "display this message" },
        { NULL }
    };
    fsLink_t *l, *next, **back;
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
            for( l = fs_links; l; l = next ) {
                next = l->next;
                Z_Free( l->target );
                Z_Free( l );
            }
            fs_links = NULL;
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

    for( l = fs_links, back = &fs_links; l; l = l->next ) {
        if( !Q_stricmp( l->name, name ) ) {
            break;
        }
        back = &l->next;
    }
    if( !l ) {
        Com_Printf( "Symbolic link %s does not exist.\n", name );
        return;
    }

    *back = l->next;
    Z_Free( l->target );
    Z_Free( l );
}

static void FS_Link_f( void ) {
    int argc, count;
	size_t length;
    fsLink_t *l;
    char *name, *target;

    argc = Cmd_Argc();
    if( argc == 1 ) {
        for( l = fs_links, count = 0; l; l = l->next, count++ ) {
            Com_Printf( "%s --> %s\n", l->name, l->target );
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
    for( l = fs_links; l; l = l->next ) {
        if( !Q_stricmp( l->name, name ) ) {
            break;
        }
    }

    if( !l ) {
        length = strlen( name );
        l = FS_Malloc( sizeof( *l ) + length );
        strcpy( l->name, name );
        l->namelen = length;
        l->next = fs_links;
        fs_links = l;
    } else {
        Z_Free( l->target );
    }

    target = Cmd_Argv( 2 );
    l->target = FS_CopyString( target );
    l->targlen = strlen( target );
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
	searchpath_t	*path, *next;
    fsLink_t *l, *nextLink;
	fsFile_t *file;
	int i;

	if( !fs_searchpaths ) {
		return;
	}

	if( total ) {
		// close file handles
		for( i = 0, file = fs_files; i < MAX_FILE_HANDLES; i++, file++ ) {
			if( file->type != FS_FREE ) {
				Com_WPrintf( "FS_Shutdown: closing handle %i: %s\n",
                    i + 1, file->fullpath );
				FS_FCloseFile( i + 1 );
			}
		}

        // free symbolic links
        for( l = fs_links; l; l = nextLink ) {
            nextLink = l->next;
            Z_Free( l->target );
            Z_Free( l );
        }

        fs_links = NULL;
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
	Cmd_RemoveCommand( "copyfile" );
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

    Sys_Setenv( "QUAKE2_HOME", fs_gamedir );
}


/*
================
FS_SetupGamedir

Sets the gamedir and path to a different directory.
================
*/
static void FS_SetupGamedir( void ) {
	fs_game = Cvar_Get( "game", "", CVAR_LATCHED|CVAR_SERVERINFO );

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
	Cvar_FullSet( "gamedir", fs_game->string, CVAR_ROM|CVAR_SERVERINFO,
        CVAR_SET_DIRECT );

	FS_AddGameDirectory( FS_PATH_GAME, "%s/%s", sys_basedir->string, fs_game->string );

    // home paths override system paths
    if( sys_homedir->string[0] ) {
        FS_AddGameDirectory( FS_PATH_BASE, "%s/"BASEGAME, sys_homedir->string );
        FS_AddGameDirectory( FS_PATH_GAME, "%s/%s", sys_homedir->string, fs_game->string );
    }
    
    Sys_Setenv( "QUAKE2_HOME", fs_gamedir );
}

qboolean FS_SafeToRestart( void ) {
	fsFile_t	*file;
	int			i;
    
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
	fsFile_t	*file;
	int			i;
	fileHandle_t temp;
	searchpath_t *path, *next;
	
	Com_Printf( "---------- FS_Restart ----------\n" );
	
	// temporary disable logfile
	temp = com_logFile;
	com_logFile = 0;

	// make sure no files are opened for reading
	for( i = 0, file = fs_files; i < MAX_FILE_HANDLES; i++, file++ ) {
		if( file->type == FS_FREE ) {
			continue;
		}
		if( file->mode == FS_MODE_READ ) {
			Com_Error( ERR_FATAL, "FS_Restart: closing handle %i: %s",
                i + 1, file->fullpath );
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

/*
================
FS_FillAPI
================
*/
void FS_FillAPI( fsAPI_t *api ) {
	api->LoadFile = FS_LoadFile;
	api->LoadFileEx = FS_LoadFileEx;
    api->AllocTempMem = FS_AllocTempMem;
	api->FreeFile = FS_FreeFile;
	api->FOpenFile = FS_FOpenFile;
	api->FCloseFile = FS_FCloseFile;
	api->Tell = FS_Tell;
	api->RawTell = FS_RawTell;
	api->Read = FS_Read;
	api->Write = FS_Write;
	api->ListFiles = FS_ListFiles;
	api->FreeList = FS_FreeList;
    api->FPrintf = FS_FPrintf;
    api->ReadLine = FS_ReadLine;
}

static const cmdreg_t c_fs[] = {
    { "path", FS_Path_f },
    { "fdir", FS_FDir_f },
    { "dir", FS_Dir_f },
    { "copyfile", FS_CopyFile_f },
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

	fs_debug = Cvar_Get( "fs_debug", "0", 0 );
	fs_restrict_mask = Cvar_Get( "fs_restrict_mask", "0", CVAR_NOSET );

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

	FS_FillAPI( &fs );
	
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
	if( fs_game->latched_string || fs_restrict_mask->latched_string ) {
		return qtrue;
	}
	
	return qfalse;
}

