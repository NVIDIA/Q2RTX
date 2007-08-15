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
#ifdef USE_ZLIB
#include <zlib.h>
#include "unzip.h"
#endif

/*
=============================================================================

QUAKE FILESYSTEM

- transparently merged from several sources
- relative to the single virtual root
- case insensitive, at least at pakfiles level
- only '/' separators supported

=============================================================================
*/

#define MAX_FILES_IN_PK2	0x4000
#define MAX_FILE_HANDLES	8

#define FS_Malloc( size )		Z_TagMalloc( size, TAG_FILESYSTEM )
#define FS_CopyString( string )		Z_TagCopyString( string, TAG_FILESYSTEM )

#ifdef _WIN32
#define FS_strcmp  Q_stricmp
#else
#define FS_strcmp  strcmp
#endif

#define	MAX_READ	0x40000		// read in blocks of 256k
#define	MAX_WRITE	0x40000		// write in blocks of 256k


//
// in memory
//

typedef struct packfile_s {
	char	*name;
	int		filepos;
	int		filelen;

	struct packfile_s *hashNext;
} packfile_t;

typedef struct pack_s {
#ifdef USE_ZLIB
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
	struct pack_s	*pack;		// only one of filename / pack will be used
	char	        filename[1];
} searchpath_t;

typedef enum fsFileType_e {
	FS_FREE,
	FS_REAL,
	FS_PAK,
#ifdef USE_ZLIB
	FS_PK2,
	FS_GZIP,
#endif
	FS_BAD
} fsFileType_t;

typedef struct fsFile_s {
	char	name[MAX_QPATH];
	char	fullpath[MAX_OSPATH];
	fsFileType_t type;
	uint32	mode;
	FILE *fp;
#ifdef USE_ZLIB
	void *zfp;
#endif
	packfile_t	*pak;
	qboolean unique;
	int	length;
} fsFile_t;

typedef struct fsLink_s {
    char *target;
    int targetLength, nameLength;
    struct fsLink_s *next;
    char name[1];
} fsLink_t;

static char		fs_gamedir[MAX_OSPATH];

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

void FS_AddGameDirectory( const char *fmt, ... ) q_printf( 1, 2 );

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
static fsFile_t *FS_AllocHandle( fileHandle_t *f, const char *name ) {
	fsFile_t *file;
	int i;

	for( i = 0, file = fs_files; i < MAX_FILE_HANDLES; i++, file++ ) {
		if( file->type == FS_FREE ) {
			break;
		}
	}

	if( i == MAX_FILE_HANDLES ) {
		Com_Error( ERR_FATAL, "FS_AllocHandle: none free" );
	}

	Q_strncpyz( file->name, name, sizeof( file->name ) );
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
		Com_Error( ERR_FATAL, "FS_FileForHandle: invalid handle: %i", f );
	}

	file = &fs_files[f - 1];
	if( file->type == FS_FREE ) {
		Com_Error( ERR_FATAL, "FS_FileForHandle: free file: %i", f );
	}

	if( file->type < FS_FREE || file->type >= FS_BAD ) {
		Com_Error( ERR_FATAL, "FS_FileForHandle: invalid file type: %i", file->type );
	}

	return file;
}

qboolean FS_ValidatePath( const char *s ) {
	const char *start;
	int back;

	// check for leading slash
	// check for empty path
	if( *s == '/' || *s == '\\' || *s == 0 ) {
		return qfalse;
	}

	back = 0;
	start = s;
	while( *s ) {
		// check for ".."
		if( *s == '.' && s[1] == '.' ) {
			if( back > 1 ) {
				return qfalse;
			}
			back++;
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
	if( s - start > MAX_QPATH ) {
		return qfalse;
	}

	return qtrue;
}

/*
================
FS_ConvertToSysPath
================
*/
static char *FS_ConvertToSysPath( char *path ) {
	char *s;

	s = path;
	while( *s ) {
		if( *s == '/' || *s == '\\' ) {
			*s = PATH_SEP_CHAR;
		}
		s++;
	}

	return path;

}

/*
================
FS_GetFileLength

Gets current file length.
For GZIP files, returns uncompressed length
(very slow operation because it has to uncompress the whole file).
================
*/
int FS_GetFileLength( fileHandle_t f ) {
	fsFile_t *file = FS_FileForHandle( f );
	int pos, length;

	if( ( file->mode & FS_MODE_MASK ) == FS_MODE_READ ) {
#ifdef USE_ZLIB
		if( file->type == FS_GZIP ) {
			goto gzipHack;
		}
#endif
		return file->length;
	}

	switch( file->type ) {
	case FS_REAL:
		pos = ftell( file->fp );
		fseek( file->fp, 0, SEEK_END );
		length = ftell( file->fp );
		fseek( file->fp, pos, SEEK_SET );
		break;
#ifdef USE_ZLIB
	case FS_GZIP:
gzipHack:
		pos = gztell( file->zfp );
		gzseek( file->zfp, 0x7FFFFFFF, SEEK_SET );
		length = gztell( file->zfp );
		gzseek( file->zfp, pos, SEEK_SET );
		break;
#endif
	default:
		Com_Error( ERR_FATAL, "FS_GetFileLength: bad file type" );
		length = -1;
		break;
	}

	return length;
}

int FS_GetFileLengthNoCache( fileHandle_t f ) {
	fsFile_t *file = FS_FileForHandle( f );
	int pos, length;

	switch( file->type ) {
	case FS_REAL:
		pos = ftell( file->fp );
		fseek( file->fp, 0, SEEK_END );
		length = ftell( file->fp );
		fseek( file->fp, pos, SEEK_SET );
		break;
	case FS_PAK:
		length = file->length;
		break;
#ifdef USE_ZLIB
	case FS_GZIP:
		pos = gztell( file->zfp );
		gzseek( file->zfp, 0x7FFFFFFF, SEEK_SET );
		length = gztell( file->zfp );
		gzseek( file->zfp, pos, SEEK_SET );
		break;
	case FS_PK2:
		length = file->length;
		break;
#endif
	default:
		Com_Error( ERR_FATAL, "FS_GetFileLengthNoCache: bad file type" );
		length = -1;
		break;
	}

	return length;
}

/*
================
FS_GetFileName
================
*/
const char *FS_GetFileName( fileHandle_t f ) {
	return ( FS_FileForHandle( f ) )->name;
}

const char *FS_GetFileFullPath( fileHandle_t f ) {
	return ( FS_FileForHandle( f ) )->fullpath;
}

/*
============
FS_CreatePath

Creates any directories needed to store the given filename.
Expects a fully qualified path.
============
*/
void FS_CreatePath( const char *path ) {
	char buffer[MAX_OSPATH];
	char *ofs;

	Q_strncpyz( buffer, path, sizeof( buffer ) );
	FS_ConvertToSysPath( buffer );

	FS_DPrintf( "FS_CreatePath( '%s' )\n", buffer );
	
	for( ofs = buffer + 1; *ofs; ofs++ ) {
		if( *ofs == PATH_SEP_CHAR ) {	
			// create the directory
			*ofs = 0;
			Sys_Mkdir( buffer );
			*ofs = PATH_SEP_CHAR;
		}
	}
}


/*
==============
FS_FCloseFile
==============
*/
void FS_FCloseFile( fileHandle_t f ) {
	fsFile_t *file = FS_FileForHandle( f );

	FS_DPrintf( "FS_FCloseFile( '%s' )\n", file->fullpath );

	switch( file->type ) {
	case FS_REAL:
		fclose( file->fp );
		break;
	case FS_PAK:
		if( file->unique ) {
			fclose( file->fp );
		}
		break;
#ifdef USE_ZLIB
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

	/* don't clear name and mode, so post-restart reopening works */
	file->type = FS_FREE;
	file->fp = NULL;
#ifdef USE_ZLIB
	file->zfp = NULL;
#endif
	file->pak = NULL;
	file->unique = qfalse;
}

/*
============
FS_FOpenFileWrite

In case of GZIP files, returns *raw* (compressed) length!
============
*/
static int FS_FOpenFileWrite( fsFile_t *file ) {
	FILE *fp;
#ifdef USE_ZLIB
	gzFile zfp;
	char *ext;
#endif
	char *modeStr;
	fsFileType_t type;
	uint32 mode;

#ifdef _WIN32
	/* allow writing into basedir on Windows */
	if( ( file->mode & FS_PATH_MASK ) == FS_PATH_BASE ) {
		Com_sprintf( file->fullpath, sizeof( file->fullpath ),
			"%s/" BASEGAME "/%s", sys_basedir->string, file->name );
	} else
#endif
	{
		Com_sprintf( file->fullpath, sizeof( file->fullpath ),
			"%s/%s", fs_gamedir, file->name );
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
		Com_Error( ERR_FATAL, "FS_FOpenFileWrite( '%s' ): invalid mode mask",
			file->fullpath );
		modeStr = NULL;
		break;
	}

	FS_ConvertToSysPath( file->fullpath );

	FS_CreatePath( file->fullpath );

	fp = fopen( file->fullpath, modeStr );
	
	if( !fp ) {
		FS_DPrintf( "FS_FOpenFileWrite: fopen( '%s', '%s' ) failed\n",
			file->fullpath, modeStr );
		return -1;
	}

	type = FS_REAL;
#ifdef USE_ZLIB
	if( !( file->mode & FS_FLAG_RAW ) ) {
		ext = COM_FileExtension( file->fullpath );
		if( !strcmp( ext, ".gz" ) ) {
			zfp = gzdopen( fileno( fp ), modeStr );
			if( !zfp ) {
				FS_DPrintf( "FS_FOpenFileWrite: gzopen( '%s', '%s' ) failed\n",
					file->fullpath, modeStr );
				fclose( fp );
				return -1;
			}
			file->zfp = zfp;
			type = FS_GZIP;
		}
	}
#endif

	FS_DPrintf( "FS_FOpenFileWrite( '%s' )\n", file->fullpath );

	file->fp = fp;
	file->type = type;
	file->length = 0;
	file->unique = qtrue;

	if( mode == FS_MODE_WRITE ) {
		return 0;
	}

	if( mode == FS_MODE_RDWR ) {
		fseek( fp, 0, SEEK_END );
	}
	
	return ftell( fp );
}

static searchpath_t *FS_SearchPath( uint32 flags ) {
	if( ( flags & FS_PATH_MASK ) == FS_PATH_BASE ) {
		return fs_base_searchpaths;
	}
	return fs_searchpaths;
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
static int FS_FOpenFileRead( fsFile_t *file, qboolean unique ) {
    searchpath_t    *search;
	pack_t			*pak;
	uint32			hash;
	packfile_t		*entry;
	FILE			*fp;
#ifdef USE_ZLIB
	void			*zfp;
	char			*ext;
#endif
	fsFileType_t	type;
	int				pos, length;

	fs_fileFromPak = qfalse;
    fs_count_read++;

//
// search through the path, one element at a time
//
	hash = Com_HashPath( file->name, 0 );

	for( search = FS_SearchPath( file->mode ); search; search = search->next ) {
		if( ( file->mode & FS_PATH_MASK ) == FS_PATH_GAME ) {
			if( fs_searchpaths != fs_base_searchpaths && search == fs_base_searchpaths ) {
				/* consider baseq2 a gamedir if no gamedir loaded */
				break;
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
				if( !Q_stricmp( entry->name, file->name ) ) {	
					// found it!
					fs_fileFromPak = qtrue;

					Com_sprintf( file->fullpath, sizeof( file->fullpath ), "%s/%s", pak->filename, file->name );

					// open a new file on the pakfile
#ifdef USE_ZLIB
					if( pak->zFile ) {
						if( unique ) {
							zfp = unzReOpen( pak->filename, pak->zFile );
							if( !zfp ) {
								Com_Error( ERR_FATAL, "FS_FOpenFileRead: unzReOpen( '%s' ) failed", pak->filename );
							}
						} else {
							zfp = pak->zFile;
						}
						if( unzSetCurrentFileInfoPosition( zfp, entry->filepos ) == -1 ) {
							Com_Error( ERR_FATAL, "FS_FOpenFileRead: unzSetCurrentFileInfoPosition( '%s/%s' ) failed", pak->filename, entry->name );
						}
						if( unzOpenCurrentFile( zfp ) != UNZ_OK ) {
							Com_Error( ERR_FATAL, "FS_FOpenFileRead: unzReOpen( '%s/%s' ) failed", pak->filename, entry->name );
						}

						file->zfp = zfp;
						file->type = FS_PK2;
					} else
#endif
                    {
						if( unique ) {
							fp = fopen( pak->filename, "rb" );
							if( !fp ) {
								Com_Error( ERR_FATAL, "Couldn't reopen %s", pak->filename );
							}
						} else {
							fp = pak->fp;
						}

						fseek( fp, entry->filepos, SEEK_SET );

						file->fp = fp;
						file->type = FS_PAK;
					}

					length = entry->filelen;
					file->pak = entry;
					file->length = length;
					file->unique = unique;

					FS_DPrintf( "FS_FOpenFileRead( '%s/%s' )\n", pak->filename, file->name );

					return length;
				}
			}
		} else {
			if( ( file->mode & FS_TYPE_MASK ) == FS_TYPE_PAK ) {
				continue;
			}
	// check a file in the directory tree
			Com_sprintf( file->fullpath, sizeof( file->fullpath ), "%s/%s",
				search->filename, file->name );

			FS_ConvertToSysPath( file->fullpath );

            fs_count_open++;
			fp = fopen( file->fullpath, "rb" );
			if( !fp ) {
				continue;
			}

			type = FS_REAL;
#ifdef USE_ZLIB
			if( !( file->mode & FS_FLAG_RAW ) ) {
				ext = COM_FileExtension( file->fullpath );
				if( !strcmp( ext, ".gz" ) ) {
					zfp = gzdopen( fileno( fp ), "rb" );
					if( !zfp ) {
						Com_WPrintf( "gzopen( '%s', 'rb' ) failed, "
							"not a GZIP file?\n", file->fullpath );
						fclose( fp );
						return -1;
					}
					file->zfp = zfp;
					type = FS_GZIP;
				}
			}
#endif
			
			FS_DPrintf( "FS_FOpenFileRead( '%s' )\n", file->fullpath );

			file->fp = fp;
			file->type = type;
			file->unique = qtrue;

			pos = ftell( fp );
			fseek( fp, 0, SEEK_END );
			length = ftell( fp );
			fseek( fp, pos, SEEK_SET );

			file->length = length;

			return length;
		}
	}
	
	FS_DPrintf( "FS_FOpenFileRead( '%s' ): couldn't find\n", file->name );
	
	return -1;
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
int FS_Read( void *buffer, int len, fileHandle_t hFile ) {
	int		block, remaining = len;
	int		read = 0;
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
#ifdef USE_ZLIB
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
		if( read == -1 ) {
			Com_Error( ERR_FATAL, "FS_Read: -1 bytes read" );
        }

		remaining -= read;
		buf += read;
	}

	return len;
}

/*
=================
FS_Write

Properly handles partial writes
=================
*/
int FS_Write( const void *buffer, int len, fileHandle_t hFile ) {
	int		block, remaining = len;
	int		write = 0;
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
#ifdef USE_ZLIB
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
		if( write == -1 ) {
			Com_Error( ERR_FATAL, "FS_Write: -1 bytes written" );
		}

		remaining -= write;
		buf += write;
	}

	if( ( file->mode & FS_FLUSH_MASK ) == FS_FLUSH_SYNC ) {
		switch( file->type ) {
		case FS_REAL:
			fflush( file->fp );
			break;
#ifdef USE_ZLIB
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
    static char        buffer[MAX_QPATH];
    fsLink_t    *l;
    int         length;

    length = strlen( filename );
    for( l = fs_links; l; l = l->next ) {
        if( l->nameLength > length ) {
            continue;
        }
        if( !Q_stricmpn( l->name, filename, l->nameLength ) ) {
            if( l->targetLength + length - l->nameLength > MAX_QPATH - 1 ) {
                FS_DPrintf( "FS_ExpandLinks( '%s' ): MAX_QPATH exceeded\n",
                        filename );
                return ( char * )filename;
            }
            memcpy( buffer, l->target, l->targetLength );
            strcpy( buffer + l->targetLength, filename + l->nameLength );
            FS_DPrintf( "FS_ExpandLinks( '%s' ) --> '%s'\n", filename, buffer );
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
int FS_FOpenFile( const char *filename, fileHandle_t *f, uint32 mode ) {
	fsFile_t	*file;
	fileHandle_t hFile;
	int			ret = -1;

	if( !filename || !f ) {
		Com_Error( ERR_FATAL, "FS_FOpenFile: NULL" );
	}

	*f = 0;

	if( !fs_searchpaths ) {
		return -1; /* not yet initialized */
	}

    if( ( mode & FS_MODE_MASK ) == FS_MODE_READ ) {
        filename = FS_ExpandLinks( filename );
    }

	if( !FS_ValidatePath( filename ) ) {
		FS_DPrintf( "FS_FOpenFile: refusing invalid path: %s\n", filename );
		return -1;
	}

	/* allocate new file handle */
	file = FS_AllocHandle( &hFile, filename );
	file->mode = mode;

	mode &= FS_MODE_MASK;
	switch( mode ) {
	case FS_MODE_READ:
		ret = FS_FOpenFileRead( file, qtrue );
		break;
	case FS_MODE_WRITE:
	case FS_MODE_APPEND:
	case FS_MODE_RDWR:
		ret = FS_FOpenFileWrite( file );
		break;
	default:
		Com_Error( ERR_FATAL, "FS_FOpenFile: illegal mode: %u", mode );
		break;
	}

	/* if succeeded, store file handle */
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
#ifdef USE_ZLIB
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
#ifdef USE_ZLIB
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
static int loadSaved;
static int loadInuse;
static int loadStack;

// for statistics
static int loadCount;
static int loadCountStatic;

// very simple one-file cache for *.bsp files
static byte *cachedBytes;
static char cachedPath[MAX_QPATH];
static int	cachedLength;
static int  cachedInuse;


/*
============
FS_LoadFile

Filenames are relative to the quake search path
a null buffer will just return the file length without loading
============
*/
int FS_LoadFileEx( const char *path, void **buffer, uint32 flags ) {
	fsFile_t *file;
	fileHandle_t f;
	byte	*buf;
	int		length;

	if( !path ) {
		Com_Error( ERR_FATAL, "FS_LoadFile: NULL" );
	}

	if( buffer ) {
		*buffer = NULL;
	}

	if( !fs_searchpaths ) {
		return -1; /* not yet initialized */
	}

    path = FS_ExpandLinks( path );

	if( !FS_ValidatePath( path ) ) {
		FS_DPrintf( "FS_LoadFile: refusing invalid path: %s\n", path );
		return -1;
	}

	if( buffer && ( flags & FS_FLAG_CACHE ) ) {
		if( !Q_stricmp( cachedPath, path ) ) {
			//Com_Printf( S_COLOR_MAGENTA"cached: %s\n", path );
			*buffer = cachedBytes;
			cachedInuse++;
			return cachedLength;
		}
	}

	/* allocate new file handle */
	file = FS_AllocHandle( &f, path );
	flags &= ~FS_MODE_MASK;
	file->mode = flags | FS_MODE_READ;

	/* look for it in the filesystem or pack files */
	length = FS_FOpenFileRead( file, qfalse );
	if( length == -1 ) {
		return -1;
	}

	/* get real file length */
	length = FS_GetFileLength( f );
	
	if( buffer ) {
		if( !( flags & FS_FLAG_CACHE ) && loadInuse + length < MAX_LOAD_BUFFER && !( fs_restrict_mask->integer & 16 ) ) {
  //          Com_Printf(S_COLOR_MAGENTA"static: %s: %d\n",path,length);
			buf = &loadBuffer[loadInuse];
            loadLast = buf;
            loadSaved = loadInuse;
			loadInuse += length + 1;
			loadInuse = ( loadInuse + 3 ) & ~3;
			loadStack++;
			loadCountStatic++;
		} else {
//            Com_Printf(S_COLOR_MAGENTA"alloc: %s: %d\n",path,length);
			buf = FS_Malloc( length + 1 );
			loadCount++;
		}
		*buffer = buf;

		FS_Read( buf, length, f );
		buf[length] = 0;

		if( flags & FS_FLAG_CACHE ) {
			FS_FlushCache();
			cachedBytes = buf;
			cachedLength = length;
			strcpy( cachedPath, path );
			cachedInuse = 1;
		}
	}

	FS_FCloseFile( f );

	return length;
}

int FS_LoadFile( const char *path, void **buffer ) {
	return FS_LoadFileEx( path, buffer, 0 );
}

void *FS_AllocTempMem( int length ) {
    byte *buf;

    if( loadInuse + length <= MAX_LOAD_BUFFER && !( fs_restrict_mask->integer & 16 ) ) {
        buf = &loadBuffer[loadInuse];
        loadLast = buf;
        loadSaved = loadInuse;
        loadInuse += length;
        loadInuse = ( loadInuse + 3 ) & ~3;
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
	if( ( byte * )buffer == cachedBytes ) {
		if( cachedInuse == 0 ) {
			Com_Error( ERR_FATAL, "FS_FreeFile: cachedInuse is zero" );
		}
		cachedInuse--;
		return;
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

void FS_FlushCache( void ) {
	if( cachedInuse ) {
		Com_Error( ERR_FATAL, "FS_FlushCache: cachedInuse is nonzero" );
	}
	if( !cachedBytes ) {
		return;
	}
	Z_Free( cachedBytes );
	cachedBytes = NULL;
	cachedLength = 0;
	cachedPath[0] = 0;
}

/*
=============
FS_CopyFile
=============
*/
qboolean FS_CopyFile( const char *src, const char *dst ) {
	fileHandle_t hSrc, hDst;
	byte	buffer[MAX_READ];
	int		len;
	int		size;

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

	if( !FS_ValidatePath( filename ) ) {
		FS_DPrintf( "FS_RemoveFile: refusing invalid path: %s\n", filename );
		return qfalse;
	}

	Com_sprintf( path, sizeof( path ), "%s/%s", fs_gamedir, filename );
	FS_ConvertToSysPath( path );

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

	if( !FS_ValidatePath( from ) || !FS_ValidatePath( to ) ) {
		FS_DPrintf( "FS_RenameFile: %s, %s: refusing invalid path\n", from, to );
		return qfalse;
	}

	Com_sprintf( frompath, sizeof( frompath ), "%s/%s", fs_gamedir, from );
	Com_sprintf( topath, sizeof( topath ), "%s/%s", fs_gamedir, to );

	FS_ConvertToSysPath( frompath );
	FS_ConvertToSysPath( topath );

	if( !Sys_RenameFile( frompath, topath ) ) {
		FS_DPrintf( "FS_RenameFile: Sys_RenameFile( '%s', '%s' ) failed\n", frompath, topath );
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
	int len;

	va_start( argptr, format );
	len = Q_vsnprintf( string, sizeof( string ), format, argptr );
	va_end( argptr );

	FS_Write( string, len, f );
}

uint32 _Com_HashPath( const char *string, int hashSize ) {
	uint32 hash;
	uint32 c;

	hash = 0;
	while( *string ) {
		c = *string++;
		if( c == '\\' ) {
			c = '/';
		} else {
		    c = Q_tolower( c );
        }
		hash = 127 * hash + c;
	}

	hash = ( hash >> 20 ) ^ ( hash >> 10 ) ^ hash;
	return hash & ( hashSize - 1 );
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
	int				namesLength;
	pack_t			*pack;
	FILE			*packhandle;
	dpackfile_t		info[MAX_FILES_IN_PACK];
	int				hashSize;
	uint32			hash;
	int				len;

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
	pack = FS_Malloc( sizeof( pack_t ) +
		numpackfiles * sizeof( packfile_t ) +
		hashSize * sizeof( packfile_t * ) +
		namesLength + len );
	strcpy( pack->filename, packfile );
	pack->fp = packhandle;
#ifdef USE_ZLIB
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
		file->name = names;

		strcpy( file->name, dfile->name );

		file->filepos = LittleLong( dfile->filepos );
		file->filelen = LittleLong( dfile->filelen );

		hash = Com_HashPath( file->name, hashSize );
		file->hashNext = pack->fileHash[hash];
		pack->fileHash[hash] = file;

		names += len;
	}

	FS_DPrintf( "Added pakfile %s, %d files, %d hash table entries\n",
		packfile, numpackfiles, hashSize );

	return pack;

fail:
	fclose( packhandle );
	return NULL;
}

#ifdef USE_ZLIB
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
	int				namesLength;
	int				hashSize;
	uint32			hash;
	int				len;

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

	FS_DPrintf( "Added zipfile '%s', %d files, %d hash table entries\n",
		packfile, numFiles, hashSize );

	return pack;

fail:
	unzClose( zFile );
	return NULL;
}
#endif

static int QDECL pakcmp( const void *p1, const void *p2 ) {
	const char *s1 = *( const char ** )p1;
	const char *s2 = *( const char ** )p2;

    if( !strncmp( s1, "pak", 3 ) ) {
        if( strncmp( s2, "pak", 3 ) ) {
            return -1;
        }
    } else if( !strncmp( s2, "pak", 3 ) ) {
        return 1;
    }

	return strcmp( s1, s2 );
}

static void FS_LoadPackFiles( const char *extension, pack_t *(loadfunc)( const char * ) ) {
	int				i;
	searchpath_t	*search;
	pack_t			*pak;
	char **			list;
	int				numFiles;
    char            path[MAX_OSPATH];

	list = Sys_ListFiles( fs_gamedir, extension, FS_SEARCH_NOSORT, &numFiles );
	if( !list ) {
		return;
	}
	qsort( list, numFiles, sizeof( list[0] ), pakcmp );
	for( i = 0; i < numFiles; i++ ) {
        Com_sprintf( path, sizeof( path ), "%s/%s", fs_gamedir, list[i] );
		pak = (*loadfunc)( path );
		if( !pak )
			continue;
		search = FS_Malloc( sizeof( searchpath_t ) );
		search->filename[0] = 0;
		search->pack = pak;
		search->next = fs_searchpaths;
		fs_searchpaths = search;	
	}

	Sys_FreeFileList( list );
		
}

/*
================
FS_AddGameDirectory

Sets fs_gamedir, adds the directory to the head of the path,
then loads and adds pak*.pak, then anything else in alphabethical order.
================
*/
void FS_AddGameDirectory( const char *fmt, ... ) {
    va_list argptr;
	searchpath_t	*search;
	int length;

    va_start( argptr, fmt );
	Q_vsnprintf( fs_gamedir, sizeof( fs_gamedir ), fmt, argptr );
    va_end( argptr );

#ifdef _WIN32
	Com_ReplaceSeparators( fs_gamedir, '/' );
#endif

	//
	// add the directory to the search path
	//
	if( !( fs_restrict_mask->integer & 1 ) ) {
		length = strlen( fs_gamedir );
		search = FS_Malloc( sizeof( searchpath_t ) + length );
		search->pack = NULL;
		strcpy( search->filename, fs_gamedir );
		search->next = fs_searchpaths;
		fs_searchpaths = search;
	}

	//
	// add any pak files in the format *.pak
	//
	if( !( fs_restrict_mask->integer & 2 ) ) {
		FS_LoadPackFiles( ".pak", FS_LoadPakFile );
	}

#ifdef USE_ZLIB
	//
	// add any zip files in the format *.pk2
	//
	if( !( fs_restrict_mask->integer & 4 ) ) {
		FS_LoadPackFiles( ".pk2", FS_LoadZipFile );
	}
#endif
}


/*
=================
FS_GetModList
=================
*/
#define MAX_LISTED_MODS		32

static void FS_GetModList( char **list, int *count ) {
	char path[MAX_OSPATH];
	FILE *fp;
	char **dirlist;
	int numDirs;
	int i;

	if( !( dirlist = Sys_ListFiles( sys_basedir->string, NULL, FS_SEARCHDIRS_ONLY, &numDirs ) ) ) {
		return;
	}

	for( i = 0; i < numDirs; i++ ) {
		if( !strcmp( dirlist[i], BASEGAME ) ) {
			continue;
		}

#ifdef _WIN32
		Com_sprintf( path, sizeof( path ), "%s/%s/gamex86.dll", sys_basedir->string, dirlist[i] );
#else
		Com_sprintf( path, sizeof( path ), "%s/%s/gamei386.so", sys_basedir->string, dirlist[i] );
#endif

		if( !( fp = fopen( path, "rb" ) ) ) {
			continue;
		}
		fclose( fp );

		Com_sprintf( path, sizeof( path ), "%s/%s/description.txt", sys_basedir->string, dirlist[i] );

		if( ( fp = fopen( path, "r" ) ) != NULL ) {
			Q_strncpyz( path, va( "%s\n", dirlist[i] ), sizeof( path ) - MAX_QPATH );
			fgets( path + strlen( path ), MAX_QPATH, fp );
			fclose( fp );
			list[*count] = FS_CopyString( path );
		} else {
			list[*count] = FS_CopyString( dirlist[i] );
		}
		
		if( (*count)++ == MAX_LISTED_MODS ) {
			break;
		}
	}

	Sys_FreeFileList( dirlist );
}

/*
=================
FS_CopyExtraInfo
=================
*/
char *FS_CopyExtraInfo( const char *name, const fsFileInfo_t *info ) {
	char	*out;
	int		length;

	if( !name ) {
		return NULL;
	}

	length = strlen( name ) + 1;
	
	out = FS_Malloc( sizeof( *info ) + length );
	strcpy( out, name );

	memcpy( out + length, info, sizeof( *info ) );

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

qboolean FS_ExtCmp( const char *ext, const char *string ) {
	int		c1, c2;
    const char *s;
	
rescan:
    s = string;
    do {
        c1 = *ext++;
        c2 = *s++;

        if( c1 == ';' ) {
            if( c2 ) {
                goto rescan;
            }
            return qtrue;
        }
        
        c1 = Q_tolower( c1 );
        c2 = Q_tolower( c2 );
        if( c1 != c2 ) {
            while( c1 ) {
                c1 = *ext++;
                if( c1 == ';' ) {
                    goto rescan;
                }
            }
            return qfalse;
        }
    } while( c1 );

    return qtrue;
}


/*
=================
FS_ListFiles
=================
*/
char **FS_ListFiles( const char *path, const char *extension,
        uint32 flags, int *numFiles )
{
	searchpath_t *search;
	char *listedFiles[MAX_LISTED_FILES];
	int count, total;
	char buffer[MAX_QPATH];
	char **dirlist;
	int numFilesInDir;
	char **list;
	int i, length;
	char *name, *filename;
	fsFileInfo_t	info;

	if( flags & FS_SEARCH_BYFILTER ) {
		if( !extension ) {
			Com_Error( ERR_FATAL, "FS_ListFiles: NULL filter" );
		}
	}

	if( !path ) {
		path = "";
	}

	count = 0;

	if( numFiles ) {
		*numFiles = 0;
	}

	if( !strcmp( path, "$modlist" ) ) {
		FS_GetModList( listedFiles, &count );
	} else {
		switch( flags & FS_PATH_MASK ) {
		case FS_PATH_BASE:
			search = fs_base_searchpaths;
			break;
		default:
			search = fs_searchpaths;
			break;
		}

		memset( &info, 0, sizeof( info ) );

		for( ; search; search = search->next ) {
			if( search->pack ) {
				if( ( flags & FS_TYPE_MASK ) == FS_TYPE_REAL ) {
					/* don't search in paks */
					continue;
				}

				// TODO: add directory search support for pak files
				if( ( flags & FS_SEARCHDIRS_MASK ) == FS_SEARCHDIRS_ONLY ) {
					continue;
				}
				if( ( flags & FS_PATH_MASK ) == FS_PATH_GAME ) {
					if( fs_searchpaths != fs_base_searchpaths && search == fs_base_searchpaths ) {
						/* consider baseq2 a gamedir if no gamedir loaded */
						break;
					}
				}
				if( flags & FS_SEARCH_BYFILTER ) {
				    for( i = 0; i < search->pack->numfiles; i++ ) {
					    name = search->pack->files[i].name;
                        
						// check path
						filename = name;
						if( *path ) {
							length = strlen( path );
							if( Q_stricmpn( name, path, length ) ) {
								continue;
							}
							filename += length + 1;
						}

						// check filter
						if( !FS_WildCmp( extension, filename ) ) {
							continue;
						}

						// copy filename
						if( count == MAX_LISTED_FILES ) {
							break;
						}

						if( !( flags & FS_SEARCH_SAVEPATH ) ) {
							filename = COM_SkipPath( filename );
						}
						if( flags & FS_SEARCH_EXTRAINFO ) {
							info.fileSize = search->pack->files[i].filelen;
							listedFiles[count++] = FS_CopyExtraInfo( filename, &info );
						} else {
							listedFiles[count++] = FS_CopyString( filename );
						}
                    }
                } else {
    				for( i = 0; i < search->pack->numfiles; i++ ) {
	    				name = search->pack->files[i].name;
                        
						// check path
						if( *path ) {
							COM_FilePath( name, buffer, sizeof( buffer ) );
							if( Q_stricmp( path, buffer ) ) {
								continue;
							}
						}

						// check extension
						if( extension && !FS_ExtCmp( extension, COM_FileExtension( name ) ) ) {
							continue;
						}
						
						// copy filename
						if( count == MAX_LISTED_FILES ) {
							break;
						}
						if( !( flags & FS_SEARCH_SAVEPATH ) ) {
							name = COM_SkipPath( name );
						}
						if( flags & FS_SEARCH_EXTRAINFO ) {
							info.fileSize = search->pack->files[i].filelen;
							listedFiles[count++] = FS_CopyExtraInfo( name, &info );
						} else {
							listedFiles[count++] = FS_CopyString( name );
						}
					}
				}
			} else {
				if( ( flags & FS_TYPE_MASK ) == FS_TYPE_PAK ) {
					/* don't search in OS filesystem */
					continue;
				}

				Q_strncpyz( buffer, search->filename, sizeof( buffer ) );
				if( *path ) {
					Q_strcat( buffer, sizeof( buffer ), "/" );
					Q_strcat( buffer, sizeof( buffer ), path );
				}

				if( flags & FS_SEARCH_BYFILTER ) {
					dirlist = Sys_ListFiles( buffer, extension, flags|FS_SEARCH_NOSORT, &numFilesInDir );
				} else {
					dirlist = Sys_ListFiles( buffer, extension, flags|FS_SEARCH_NOSORT, &numFilesInDir );
				}

				if( !dirlist ) {
					continue;
				}

				for( i = 0; i < numFilesInDir; i++ ) {
					if( count == MAX_LISTED_FILES ) {
						break;
					}
					name = dirlist[i];
					if( ( flags & FS_SEARCH_SAVEPATH ) && !( flags & FS_SEARCH_BYFILTER ) ) {
						// skip search path
						name += strlen( search->filename ) + 1;
					}
					if( flags & FS_SEARCH_EXTRAINFO ) {
						listedFiles[count++] = FS_CopyExtraInfo( name, ( fsFileInfo_t * )( dirlist[i] + strlen( dirlist[i] ) + 1 ) );
					} else {
						listedFiles[count++] = FS_CopyString( name );
					}
				}
				Sys_FreeFileList( dirlist );
				
			}
			if( count == MAX_LISTED_FILES ) {
				break;
			}
		}
	}

	if( !count ) {
		return NULL;
	}

	// sort alphabetically (ignoring FS_SEARCH_NOSORT)
	qsort( listedFiles, count, sizeof( listedFiles[0] ), SortStrcmp );

	// remove duplicates
	total = 1;
	for( i = 1; i < count; i++ ) {
		if( !FS_strcmp( listedFiles[ i - 1 ], listedFiles[i] ) ) {
			Z_Free( listedFiles[ i - 1 ] );
			listedFiles[i-1] = NULL;
		} else {
			total++;
		}
	}

	list = FS_Malloc( sizeof( char * ) * ( total + 1 ) );

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
FS_FreeFileList
=================
*/
void FS_FreeFileList( char **list ) {
	char **p;

	if( !list ) {
		Com_Error( ERR_FATAL, "FS_FreeFileList: NULL" );
	}

	p = list;
	while( *p ) {
		Z_Free( *p++ );
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
	char	**dirnames;
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
    
	if( ( dirnames = FS_ListFiles( NULL, filter, i, &ndirs ) ) != NULL ) {
		for( i = 0; i < ndirs; i++ ) {
			Com_Printf( "%s\n", dirnames[i] );
		}
		FS_FreeFileList( dirnames );
	}
	Com_Printf( "%i files listed\n", ndirs );
	
}

/*
============
FS_Dir_f
============
*/
static void FS_Dir_f( void ) {
	char	**dirnames;
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
    dirnames = FS_ListFiles( Cmd_Argv( 1 ), ext, 0, &ndirs );
	if( dirnames ) {
		for( i = 0; i < ndirs; i++ ) {
			Com_Printf( "%s\n", dirnames[i] );
		}
		FS_FreeFileList( dirnames );
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
    uint32 hash;
    char filename[MAX_QPATH];
    char fullpath[MAX_OSPATH];
    char *path;
    fsFileInfo_t info;
    int total;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <path>\n", Cmd_Argv( 0 ) );
		return;
	}

    Cmd_ArgvBuffer( 1, filename, sizeof( filename ) );
    Q_strlwr( filename );

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
                    Com_Printf( "%s/%s (%d bytes)\n", pak->filename,
                        path, entry->filelen );
                    total++;
                 //   return;
                }
            }
        } else {
            Com_sprintf( fullpath, sizeof( fullpath ), "%s/%s",
                search->filename, path );
			FS_ConvertToSysPath( fullpath );
            if( Sys_GetFileInfo( fullpath, &info ) ) {
                Com_Printf( "%s/%s (%d bytes)\n", search->filename, filename,
                    info.fileSize );
                total++;
                // return;
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
#ifdef USE_ZLIB
	int				numFilesInPK2 = 0;
#endif

	Com_Printf( "Current search path:\n" );
	for( s = fs_searchpaths; s; s = s->next ) {
		if( s->pack ) {
#ifdef USE_ZLIB
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

#ifdef USE_ZLIB
	if( !( fs_restrict_mask->integer & 4 ) ) {
		Com_Printf( "%i files in PK2 files\n", numFilesInPK2 );
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

static const char *FS_Link_g( const char *partial, int state ) {
    static int length;
    static fsLink_t *link;
    char *name;

    if( !state ) {
        length = strlen( partial );
        link = fs_links;
    }

	while( link ) {
        name = link->name;
        link = link->next;
		if( !Q_stricmpn( partial, name, length ) ) {
			return name;
		}
	}

    return NULL;
}

static void FS_UnLink_f( void ) {
    fsLink_t *l, *next, **back;
    char *name;

    if( Cmd_CheckParam( "h", "help" ) ) {
usage:
        Com_Printf( "Usage: %s <name>\n"
                "Deletes the specified symbolic link.\n",
                    Cmd_Argv( 0 ) );
        return;
    }

    if( Cmd_CheckParam( "a", "all" ) ) {
        for( l = fs_links; l; l = next ) {
            next = l->next;
            Z_Free( l->target );
            Z_Free( l );
        }
        fs_links = NULL;
        return;
    }

    if( Cmd_Argc() != 2 ) {
        goto usage;
    }

    name = Cmd_Argv( 1 );
    for( l = fs_links, back = &fs_links; l; l = l->next ) {
        if( !Q_stricmp( l->name, name ) ) {
            break;
        }
        back = &l->next;
    }

    if( !l ) {
        Com_Printf( "Symbolic link '%s' does not exist.\n", name );
        return;
    }

    *back = l->next;
    Z_Free( l->target );
    Z_Free( l );
}

static void FS_Link_f( void ) {
    int argc, length, count;
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
    if( Cmd_CheckParam( "h", "help" ) || argc != 3 ) {
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
        l->nameLength = length;
        l->next = fs_links;
        fs_links = l;
    } else {
        Z_Free( l->target );
    }

    target = Cmd_Argv( 2 );
    l->target = FS_CopyString( target );
    l->targetLength = strlen( target );
}

/*
================
FS_NextPath

Allows enumerating all of the directories in the search path
================
*/
char *FS_NextPath( char *prevpath ) {
	searchpath_t	*s;
	char			*prev;

	prev = NULL;
	for( s = fs_searchpaths; s; s = s->next ) {
		if( s->pack )
			continue;
		if( prevpath == prev )
			return s->filename;
		prev = s->filename;
	}

	return NULL;
}



/*
============
FS_Gamedir

Called to find where to write a file (demos, savegames, etc)
============
*/
char *FS_Gamedir( void ) {
	return fs_gamedir;
}

static void FS_FreeSearchPath( searchpath_t *path ) {
	pack_t *pak;

	if( ( pak = path->pack ) != NULL ) {
#ifdef USE_ZLIB
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
		/* close file handles */
		for( i = 0, file = fs_files; i < MAX_FILE_HANDLES; i++, file++ ) {
			if( file->type != FS_FREE ) {
				Com_WPrintf( "FS_Shutdown: closing handle %i: '%s'\n",
                    i + 1, file->name );
				FS_FCloseFile( i + 1 );
			}
		}

        /* free symbolic links */
        for( l = fs_links; l; l = nextLink ) {
            nextLink = l->next;
            Z_Free( l->target );
            Z_Free( l );
        }

        fs_links = NULL;
	}

	/* free search paths */
	for( path = fs_searchpaths; path; path = next ) {
		next = path->next;
		FS_FreeSearchPath( path );
	}

	fs_searchpaths = NULL;

	FS_FlushCache();

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
    	FS_AddGameDirectory( "%s/"BASEGAME, sys_homedir->string );
    } else {
        // already added as basedir
    	Com_sprintf( fs_gamedir, sizeof( fs_gamedir ), "%s/"BASEGAME,
            sys_basedir->string );
    }

	Cvar_Set( "game", "" );
	Cvar_Set( "gamedir", "" );
}


/*
================
FS_SetupGamedir

Sets the gamedir and path to a different directory.
================
*/
static void FS_SetupGamedir( void ) {
	fs_game = Cvar_Get( "game", "", CVAR_LATCHED|CVAR_SERVERINFO );
	fs_game->subsystem = CVAR_SYSTEM_FILES;

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

	FS_AddGameDirectory( "%s/%s", sys_basedir->string, fs_game->string );

    // home paths override system paths
    if( sys_homedir->string[0] ) {
        FS_AddGameDirectory( "%s/"BASEGAME, sys_homedir->string );
        FS_AddGameDirectory( "%s/%s", sys_homedir->string, fs_game->string );
    }
}

qboolean FS_SafeToRestart( void ) {
	fsFile_t	*file;
	int			i;
    
	/* make sure no files are opened for reading */
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
	
	/* temporary disable logfile */
	temp = com_logFile;
	com_logFile = 0;

	/* make sure no files are opened for reading */
	for( i = 0, file = fs_files; i < MAX_FILE_HANDLES; i++, file++ ) {
		if( file->type == FS_FREE ) {
			continue;
		}
		if( file->mode == FS_MODE_READ ) {
			Com_Error( ERR_FATAL, "FS_Restart: closing handle %i: %s",
                i + 1, file->name );
		}
	}
	
	if( fs_restrict_mask->latched_string ) {
		/* perform full reset */
		FS_Shutdown( qfalse );
		FS_Init();
	} else {
		/* just change gamedir */	
		for( path = fs_searchpaths; path != fs_base_searchpaths; path = next ) {
			next = path->next;
			FS_FreeSearchPath( path );
		}

		fs_searchpaths = fs_base_searchpaths;

		FS_SetupGamedir();
		FS_Path_f();
	}

	/* re-enable logfile */
	com_logFile = temp;
	
	Com_Printf( "--------------------------------\n" );
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
	api->FreeFileList = FS_FreeFileList;
}

static const cmdreg_t c_fs[] = {
    { "path", FS_Path_f },
    { "fdir", FS_FDir_f },
    { "dir", FS_Dir_f },
    { "copyfile", FS_CopyFile_f },
    { "fs_stats", FS_Stats_f },
    { "whereis", FS_WhereIs_f },
    { "link", FS_Link_f, FS_Link_g },
    { "unlink", FS_UnLink_f, FS_Link_g },

    { NULL }
};

/*
================
FS_Init
================
*/
void FS_Init( void ) {
	int	startTime;

	startTime = Sys_Milliseconds();

	Com_Printf( "---------- FS_Init ----------\n" );

    Cmd_Register( c_fs );

	fs_debug = Cvar_Get( "fs_debug", "0", 0 );
	fs_restrict_mask = Cvar_Get( "fs_restrict_mask", "4", CVAR_NOSET );
	fs_restrict_mask->subsystem = CVAR_SYSTEM_FILES;

	if( ( fs_restrict_mask->integer & 7 ) == 7 ) {
		Com_WPrintf( "Invalid fs_restrict_mask value %d. "
            "Falling back to default.\n",
			    fs_restrict_mask->integer );
		Cvar_SetInteger( "fs_restrict_mask", 4 );
	}

	// start up with baseq2 by default
	FS_AddGameDirectory( "%s/"BASEGAME, sys_basedir->string );

	fs_base_searchpaths = fs_searchpaths;

	// check for game override
	FS_SetupGamedir();

	FS_Path_f();

	FS_FillAPI( &fs );
	
	Com_DPrintf( "%i msec to init filesystem\n",
        Sys_Milliseconds() - startTime );
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


// TODO: remove it
int	Developer_searchpath( int who ) {
	if( !strcmp( fs_game->string, "xatrix" ) ) {
		return 1;
	}

	if( !strcmp( fs_game->string, "rogue" ) ) {
		return 1;
	}

	return 0;


}





