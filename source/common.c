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
// common.c -- misc functions used in client and server
#include "com_local.h"
#include <setjmp.h>
#ifdef USE_ZLIB
#include <zlib.h>
#endif

commonAPI_t	com;

static jmp_buf  abortframe;		// an ERR_DROP occured, exit the entire frame

static char		com_errorMsg[MAXPRINTMSG];

cvar_t	*host_speeds;
cvar_t	*developer;
cvar_t	*timescale;
cvar_t	*fixedtime;
cvar_t	*dedicated;

cvar_t	*logfile_active;	// 1 = create new, 2 = append to existing
cvar_t	*logfile_flush;		// 1 = flush after each print
cvar_t	*logfile_name;
cvar_t	*logfile_prefix;

cvar_t	*sv_running;
cvar_t	*sv_paused;
cvar_t	*cl_running;
cvar_t	*cl_paused;
cvar_t	*com_timedemo;
cvar_t	*com_sleep;
cvar_t	*com_date_format;
cvar_t	*com_time_format;
cvar_t	*com_debug_break;

fileHandle_t	com_logFile;
qboolean        com_logNewline;
uint32		com_framenum;
uint32		com_eventTime;
uint32      com_localTime;
qboolean    com_initialized;

// host_speeds times
int		time_before_game;
int		time_after_game;
int		time_before_ref;
int		time_after_ref;

void Con_Init( void );
void Prompt_Init( void );
void SCR_EndLoadingPlaque( void );

/*
============================================================================

CLIENT / SERVER interactions

============================================================================
*/

static int	rd_target;
static char	*rd_buffer;
static int	rd_buffersize;
static void	(*rd_flush)(int target, char *buffer);

void Com_BeginRedirect (int target, char *buffer, int buffersize, void (*flush))
{
	if (!target || !buffer || !buffersize || !flush)
		return;
	rd_target = target;
	rd_buffer = buffer;
	rd_buffersize = buffersize;
	rd_flush = flush;

	*rd_buffer = 0;
}

void Com_AbortRedirect (void)
{
	rd_target = 0;
	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
}

void Com_EndRedirect (void)
{
	rd_flush(rd_target, rd_buffer);

	rd_target = 0;
	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
}

static void LogFile_Close( void ) {
	if( !com_logFile ) {
		return;
	}

	Com_Printf( "Closing %s\n", FS_GetFileName( com_logFile ) );

	FS_FCloseFile( com_logFile );
	com_logFile = 0;
}

static void LogFile_Open( void ) {
	uint32		mode;

	mode = logfile_active->integer > 1 ? FS_MODE_APPEND : FS_MODE_WRITE;

	if( logfile_flush->integer ) {
		mode |= FS_FLUSH_SYNC;
	}

	FS_FOpenFile( logfile_name->string, &com_logFile, mode );

	if( !com_logFile ) {
		Com_WPrintf( "Couldn't open %s\n", logfile_name->string );
		Cvar_SetInteger( "logfile", 0 );
		return;
	}

    com_logNewline = qtrue;
	Com_Printf( "Logging console to %s\n", logfile_name->string );
}

static void logfile_active_changed( cvar_t *self ) {
	if( !self->integer ) {
		LogFile_Close();
	} else {
		LogFile_Open();
	}	
}

static void logfile_param_changed( cvar_t *self ) {
	if( logfile_active->integer ) {
		LogFile_Close();
		LogFile_Open();
	}	
}

static void LogFile_Output( const char *string ) {
	char text[MAXPRINTMSG];
    char timebuf[MAX_QPATH];
	char *p, *maxp;
	int length;
	time_t	clock;
	struct tm	*tm;
    int c;

    if( logfile_prefix->string[0] ) {
        time( &clock );
        tm = localtime( &clock );
        length = strftime( timebuf, sizeof( timebuf ),
            logfile_prefix->string, tm );
    } else {
        length = 0;
    }

	p = text;
    maxp = text + sizeof( text ) - 1;
	while( *string ) {
		if( Q_IsColorString( string ) ) {
			string += 2;
			continue;
		}
        if( com_logNewline ) {
            if( length && p + length < maxp ) {
                strcpy( p, timebuf );
                p += length;
            }
            com_logNewline = qfalse;
        }

        if( p == maxp ) {
            break;
        }

        c = *string++;
        c &= 127;
        if( c == '\n' ) {
            com_logNewline = qtrue;
        }
		
		*p++ = c;
	}
	*p = 0;

	length = p - text;
	FS_Write( text, length, com_logFile );
}

/*
=============
Com_Printf

Both client and server can use this, and it will output
to the apropriate place.
=============
*/
void Com_Printf( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];
	static int	recursive;
	int			length;

	if( recursive == 2 ) {
		return;
	}

	recursive++;

	va_start( argptr, fmt );
	length = Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );

	if( rd_target ) {
		if( length + strlen( rd_buffer ) > rd_buffersize - 1 ) {
			rd_flush( rd_target, rd_buffer );
			*rd_buffer = 0;
		}
		Q_strcat( rd_buffer, rd_buffersize, msg );
	} else {
		Con_Print( msg );
			
		// also echo to debugging console
		Sys_ConsoleOutput( msg );

		// logfile
		if( com_logFile ) {
			LogFile_Output( msg );
		}
	}

	recursive--;
}


/*
================
Com_DPrintf

A Com_Printf that only shows up if the "developer" cvar is set
================
*/
void Com_DPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];
		
	if( !developer || !developer->integer )
		return;			// don't confuse non-developers with techie stuff...

	va_start( argptr, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );
	
	Com_Printf( S_COLOR_BLUE"%s", msg );
}

/*
================
Com_WPrintf

================
*/
void Com_WPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );
	
	Com_Printf( S_COLOR_YELLOW"WARNING: %s", msg );
}

/*
================
Com_EPrintf

================
*/
void Com_EPrintf( const char *fmt, ... ) {
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start( argptr, fmt );
	Q_vsnprintf( msg, sizeof( msg ), fmt, argptr );
	va_end( argptr );
	
	Com_Printf( S_COLOR_RED"ERROR: %s", msg );
}


/*
=============
Com_Error

Both client and server can use this, and it will
do the apropriate things.
=============
*/
void Com_Error( comErrorType_t code, const char *fmt, ... ) {
	va_list		argptr;
	static	qboolean	recursive;

	if( recursive ) {
#ifdef _DEBUG
		Sys_DebugBreak();
#endif
		Sys_Error( "recursive error after: %s", com_errorMsg );
	}
	recursive = qtrue;

	va_start( argptr, fmt );
	Q_vsnprintf( com_errorMsg, sizeof( com_errorMsg ), fmt, argptr );
	va_end( argptr );

    /* fix up drity message buffers */
    MSG_Init(); 

    Com_AbortRedirect();
	
	if( code == ERR_DISCONNECT || code == ERR_SILENT ) {
		SV_Shutdown( va( "Server was killed: %s", com_errorMsg ),
            KILL_DISCONNECT );
		CL_Disconnect( code, com_errorMsg );
		Com_Printf( S_COLOR_YELLOW "%s\n", com_errorMsg );
		recursive = qfalse;
		longjmp( abortframe, -1 );
	}

	if( com_debug_break && com_debug_break->integer ) {
		Sys_DebugBreak();
	}

	if( code == ERR_DROP ) {
		Com_Printf( S_COLOR_RED "********************\n"
                                "ERROR: %s\n"
                                "********************\n", com_errorMsg );
		SV_Shutdown( va( "Server crashed: %s\n", com_errorMsg ), KILL_DROP );
		CL_Disconnect( ERR_DROP, com_errorMsg );
		recursive = qfalse;
		longjmp( abortframe, -1 );
	}

	if( com_logFile ) {
		FS_FPrintf( com_logFile, "FATAL: %s\n", com_errorMsg );
	}

	SV_Shutdown( va( "Server fatal crashed: %s\n", com_errorMsg ), KILL_DROP );
	CL_Shutdown();
	Qcommon_Shutdown( qtrue );

	Sys_Error( "%s", com_errorMsg );
}

/*
===================
Com_LevelPrint
===================
*/
void Com_LevelPrint( comPrintType_t type, const char *str ) {
	switch( type ) {
	case PRINT_DEVELOPER:
		Com_DPrintf( "%s", str );
		break;
	case PRINT_WARNING:
		Com_WPrintf( "%s", str );
		break;
	case PRINT_ERROR:
		Com_EPrintf( "%s", str );
		break;
	default:
		Com_Printf( "%s", str );
		break;
	}
}

/*
===================
Com_LevelError
===================
*/
void Com_LevelError( comErrorType_t code, const char *str ) {
	Com_Error( code, "%s", str );
}

/*
=============
Com_Quit

Both client and server can use this, and it will
do the apropriate things.
=============
*/
void Com_Quit( void ) {
	SV_Shutdown( "Server quit\n", KILL_DROP );
	CL_Shutdown();
	Qcommon_Shutdown( qfalse );

	Sys_Quit();
}


// ============================================================================

#ifndef DEDICATED_ONLY

/*
===============
Com_WriteConfiguration

Writes key bindings and archived cvars to config.cfg
===============
*/
static qboolean Com_WriteConfiguration( const char *path ) {
	fileHandle_t f;

	FS_FOpenFile( path, &f, FS_MODE_WRITE );
	if( !f ) {
		Com_WPrintf( "Couldn't write %s\n", path );
		return qfalse;
	}

	FS_FPrintf( f, "// generated by q2pro, do not modify\n" );

	Key_WriteBindings( f );
	Cvar_WriteVariables( f );
	
	FS_FCloseFile( f );

	return qtrue;
}

/*
===============
Com_WriteConfig_f
===============
*/
static void Com_WriteConfig_f( void ) {
	char buffer[MAX_QPATH];

	if( Cmd_Argc() > 2 ) {
		Com_Printf( "Usage: %s [cfgfile]\n", Cmd_Argv( 0 ) );
		return;
	}

	if( Cmd_Argc() < 2 ) {
		strcpy( buffer, COM_CONFIG_NAME );
	} else {
		Cmd_ArgvBuffer( 1, buffer, sizeof( buffer ) );
		COM_DefaultExtension( buffer, ".cfg", sizeof( buffer ) );
	}

	if( Com_WriteConfiguration( buffer ) ) {
		Com_Printf( "Wrote %s\n", buffer );
	}
}

#endif

/*
==============================================================================

						ZONE MEMORY ALLOCATION

just cleared malloc with counters now...

==============================================================================
*/

#define	Z_MAGIC		0x1d0d
#define	Z_TAIL		0x5b7b

typedef struct zhead_s {
	uint16	magic;
	uint16	tag;			// for group free
	size_t	size;
	struct zhead_s	*prev, *next;
} zhead_t;

static zhead_t		z_chain;

typedef struct zstatic_s {
	zhead_t	z;
	char	data[2]; /* !!make sure 'tail' field is aligned properly */
	uint16	tail;
} zstatic_t;

static zstatic_t		z_static[] = {
	{ { Z_MAGIC, TAG_STATIC, sizeof( zstatic_t ) }, { '0', '\0' }, Z_TAIL },
	{ { Z_MAGIC, TAG_STATIC, sizeof( zstatic_t ) }, { '1', '\0' }, Z_TAIL },
	{ { Z_MAGIC, TAG_STATIC, sizeof( zstatic_t ) }, { '2', '\0' }, Z_TAIL },
	{ { Z_MAGIC, TAG_STATIC, sizeof( zstatic_t ) }, { '3', '\0' }, Z_TAIL },
	{ { Z_MAGIC, TAG_STATIC, sizeof( zstatic_t ) }, { '4', '\0' }, Z_TAIL },
	{ { Z_MAGIC, TAG_STATIC, sizeof( zstatic_t ) }, { '5', '\0' }, Z_TAIL },
	{ { Z_MAGIC, TAG_STATIC, sizeof( zstatic_t ) }, { '6', '\0' }, Z_TAIL },
	{ { Z_MAGIC, TAG_STATIC, sizeof( zstatic_t ) }, { '7', '\0' }, Z_TAIL },
	{ { Z_MAGIC, TAG_STATIC, sizeof( zstatic_t ) }, { '8', '\0' }, Z_TAIL },
	{ { Z_MAGIC, TAG_STATIC, sizeof( zstatic_t ) }, { '9', '\0' }, Z_TAIL },
	{ { Z_MAGIC, TAG_STATIC, sizeof( zstatic_t ) }, { '\0'		}, Z_TAIL },
};

typedef struct zstats_s {
	size_t  count;
	size_t	bytes;
	char	*name;
} zstats_t;

static zstats_t		z_stats[TAG_MAX];

void Z_Check( void ) {
	zhead_t	*z;

	for( z = z_chain.next; z != &z_chain; z = z->next ) {
		if( z->magic != Z_MAGIC ) {
			Com_Error( ERR_FATAL, "Z_Check: bad magic" );
        }

		if( *( uint16 * )( ( byte * )z + z->size - sizeof( uint16 ) ) != Z_TAIL ) {
			Com_Error( ERR_FATAL, "Z_Check: bad tail" );
        }

		if( z->tag == TAG_FREE ) {
			Com_Error( ERR_FATAL, "Z_Check: bad tag" );
        }
	}
}

void Z_LeakTest( memtag_t tag ) {
	zhead_t	*z;
	zstats_t *s;
	size_t numLeaks, numBytes;
	
	numLeaks = numBytes = 0;
	for( z = z_chain.next; z != &z_chain; z = z->next ) {
		if( z->magic != Z_MAGIC )
			Com_Error( ERR_FATAL, "Z_LeakTest: bad magic" );

		if( *( uint16 * )( ( byte * )z + z->size - sizeof( uint16 ) ) != Z_TAIL ) {
			Com_Error( ERR_FATAL, "Z_LeakTest: bad tail" );
        }

		if( z->tag == TAG_FREE ) {
			Com_Error( ERR_FATAL, "Z_LeakTest: bad tag" );
        }

		if( z->tag == tag ) {
			numLeaks++;
			numBytes += z->size;
		}
	}

	if( numLeaks ) {
		s = &z_stats[tag < TAG_MAX ? tag : TAG_FREE];
		Com_Printf( S_COLOR_YELLOW "************* Z_LeakTest *************\n"
						           "%s leaked %u bytes of memory (%u object%s)\n"
						           "**************************************\n",
								   s->name, numBytes, numLeaks,
								   ( numLeaks % 10 ) == 1 ? "" : "s" );
	}
}

/*
========================
Z_Free
========================
*/
void Z_Free( void *ptr ) {
	zhead_t	*z;
	zstats_t *s;

	if( !ptr ) {
        return;
	}

	z = ( ( zhead_t * )ptr ) - 1;

	if( z->magic != Z_MAGIC ) {
		Com_Error( ERR_FATAL, "Z_Free: bad magic" );
    }

	if( *( uint16 * )( ( byte * )z + z->size - sizeof( uint16 ) ) != Z_TAIL ) {
		Com_Error( ERR_FATAL, "Z_Free: bad tail" );
    }

	if( z->tag == TAG_FREE ) {
		Com_Error( ERR_FATAL, "Z_Free: bad tag" );
    }

	s = &z_stats[z->tag < TAG_MAX ? z->tag : TAG_FREE];
	s->count--;
	s->bytes -= z->size;
    
	if( z->tag != TAG_STATIC ) {
    	z->prev->next = z->next;
	    z->next->prev = z->prev;

	    free( z );
    }
}

/*
========================
Z_Realloc
========================
*/
void *Z_Realloc( void *ptr, size_t size ) {
	zhead_t	*z;
	zstats_t *s;

	if( !ptr ) {
        return Z_Malloc( size );
	}

    if( !size ) {
        Z_Free( ptr );
        return NULL;
    }

	z = ( ( zhead_t * )ptr ) - 1;

	if( z->magic != Z_MAGIC ) {
		Com_Error( ERR_FATAL, "Z_Realloc: bad magic" );
    }

	if( *( uint16 * )( ( byte * )z + z->size - sizeof( uint16 ) ) != Z_TAIL ) {
		Com_Error( ERR_FATAL, "Z_Realloc: bad tail" );
    }

	if( z->tag == TAG_FREE ) {
		Com_Error( ERR_FATAL, "Z_Realloc: bad tag" );
    }

	if( z->tag == TAG_STATIC ) {
        Z_Free( ptr );
        return Z_Malloc( size );
    }

	s = &z_stats[z->tag < TAG_MAX ? z->tag : TAG_FREE];
	s->bytes -= z->size;

	size += sizeof( zhead_t ) + sizeof( uint16 );
	size = ( size + 3 ) & ~3;
    
    z = realloc( z, size );
    if( !z ) {
		Com_Error( ERR_FATAL, "Z_Realloc: couldn't reallocate %u bytes", size );
    }

	z->size = size;
    z->prev->next = z;
    z->next->prev = z;

	s->bytes += size;

	*( uint16 * )( ( byte * )z + size - sizeof( uint16 ) ) = Z_TAIL;

	return ( void * )( z + 1 );
}

/*
========================
Z_Stats_f
========================
*/
void Z_Stats_f( void ) {
	size_t z_bytes, z_count;
	zstats_t *s;
	int i;

	Com_Printf( "-----------------\n" );
	Com_Printf( "    bytes  blocks\n\n" );

	z_bytes = 0;
	z_count = 0;
	for( i = 0, s = z_stats; i < TAG_MAX; i++, s++ ) {
        if( !s->count ) {
            continue;
        }
            
		Com_Printf( "%9u  %6u  %s\n", s->bytes, s->count, s->name );
		z_bytes += s->bytes;
		z_count += s->count;
	}

	Com_Printf( "-----------------\n" );
	Com_Printf( "%9u  %6u  %s\n", z_bytes, z_count, "total" );
}

/*
========================
Z_FreeTags
========================
*/
void Z_FreeTags( memtag_t tag ) {
	zhead_t	*z, *next;

	for( z = z_chain.next; z != &z_chain; z = next ) {
		if( z->magic != Z_MAGIC ) {
			Com_Error( ERR_FATAL, "Z_FreeTags: bad magic" );
        }

		if( *( uint16 * )( ( byte * )z + z->size - sizeof( uint16 ) ) !=
                Z_TAIL )
        {
			Com_Error( ERR_FATAL, "Z_FreeTags: bad tail" );
        }

		if( z->tag == TAG_FREE ) {
			Com_Error( ERR_FATAL, "Z_FreeTags: bad tag" );
        }
		next = z->next;
		if( z->tag == tag ) {
			Z_Free( ( void * )( z + 1 ) );
		}
	}
}

/*
========================
Z_TagMalloc
========================
*/
void *Z_TagMalloc( size_t size, memtag_t tag ) {
	zhead_t	*z;
	zstats_t *s;

	if( !size ) {
		return NULL;
	}

	if( tag == TAG_FREE )
		Com_Error( ERR_FATAL, "Z_TagMalloc: bad tag" );
	
	size += sizeof( zhead_t ) + sizeof( uint16 );
	size = ( size + 3 ) & ~3;
	z = malloc( size );
	if( !z ) {
		Com_Error( ERR_FATAL, "Z_TagMalloc: couldn't allocate %u bytes", size );
    }
	z->magic = Z_MAGIC;
	z->tag = tag;
	z->size = size;

	z->next = z_chain.next;
	z->prev = &z_chain;
	z_chain.next->prev = z;
	z_chain.next = z;

	*( uint16 * )( ( byte * )z + size - sizeof( uint16 ) ) = Z_TAIL;

	s = &z_stats[tag < TAG_MAX ? tag : TAG_FREE];
	s->count++;
	s->bytes += size;

	return ( void * )( z + 1 );
}

void *Z_TagMallocz( size_t size, memtag_t tag ) {
    void *ptr;

    if( !size ) {
        return NULL;
    }

    ptr = Z_TagMalloc( size, tag );
    memset( ptr, 0, size );

    return ptr;
}

static byte *z_reserved;
static size_t z_reservedUnuse;
static size_t z_reservedTotal;

void Z_TagReserve( size_t size, memtag_t tag ) {
	z_reserved = Z_TagMalloc( size, tag );
	z_reservedTotal = size;
	z_reservedUnuse = 0;
}

void *Z_ReservedAlloc( size_t size ) {
	void *ptr;

	if( z_reservedUnuse + size > z_reservedTotal ) {
		Com_Error( ERR_FATAL, "Z_ReservedAlloc: out of space" );
	}

	ptr = z_reserved + z_reservedUnuse;
	z_reservedUnuse += size;

	return ptr;

}

void *Z_ReservedAllocz( size_t size ) {
	void *ptr = Z_ReservedAlloc( size );
    memset( ptr, 0, size );

	return ptr;

}

char *Z_ReservedCopyString( const char *in ) {
	char	*out;
	int length;

	if( !in ) {
		return NULL;
	}

	length = strlen( in ) + 1;
	
	out = Z_ReservedAlloc( length );
	strcpy( out, in );

	return out;
}

/*
========================
Z_Init
========================
*/
void Z_Init( void ) {
	zstats_t *s;
	int i;
	static char *names[TAG_MAX] = {
		"game",
		"static",
		"generic",
		"cmd",
		"cvar",
		"filesystem",
		"renderer",
		"ui",
		"cgame",
        "server",
		"mvd",
        "sound",
		"cmodel",
        "lua"
	};

	z_chain.next = z_chain.prev = &z_chain;

	for( i = 0, s = z_stats; i < TAG_MAX; i++, s++ ) {
		s->name = names[i] ? names[i] : "unknown";
	}
}

/*
================
Z_TagCopyString
================
*/
char *Z_TagCopyString( const char *in, memtag_t tag ) {
	char	*out;
	int length;

	if( !in ) {
		return NULL;
	}

	length = strlen( in ) + 1;
	
	out = Z_TagMalloc( length, tag );
	strcpy( out, in );

	return out;
}

/*
================
Cvar_CopyString
================
*/
char *Cvar_CopyString( const char *in ) {
	char	*out;
	int length;
    zstatic_t *z;

	if( !in ) {
		return NULL;
	}

	if( !in[0] ) {
        z = &z_static[10];
        z_stats[TAG_STATIC].count++;
        z_stats[TAG_STATIC].bytes += z->z.size;
		return z->data;
	}

	if( !in[1] && Q_isdigit( in[0] ) ) {
        z = &z_static[ in[0] - '0' ];
        z_stats[TAG_STATIC].count++;
        z_stats[TAG_STATIC].bytes += z->z.size;
		return z->data;
	}

	length = strlen( in ) + 1;
	
	out = Z_TagMalloc( length, TAG_CVAR );
	strcpy( out, in );

	return out;
}

/*
==============================================================================

						FIFO

==============================================================================
*/

qboolean FIFO_Read( fifo_t *fifo, void *buffer, int length ) {
    byte *dst = buffer;
    int as = fifo->ay - fifo->ax;
    int wrapped = length - as;

    if( wrapped < 0 ) {
        memcpy( dst, fifo->data + fifo->ax, length );
        fifo->ax += length;
    } else {
        if( wrapped > fifo->bs ) {
            return qfalse;
        }
        memcpy( dst, fifo->data + fifo->ax, as );
        memcpy( dst + as, fifo->data, wrapped );
        fifo->ax = wrapped;
        fifo->ay = fifo->bs;
        fifo->bs = 0;
    }

    return qtrue;
}

qboolean FIFO_Write( fifo_t *fifo, const void *buffer, int length ) {
    const byte *src = buffer;
    int tail, wrapped;

    if( fifo->bs ) {
        if( fifo->bs + length > fifo->ax ) {
            return qfalse;
        }
        memcpy( fifo->data + fifo->bs, src, length );
        fifo->bs += length;
        return qtrue;
    }

    tail = fifo->size - fifo->ay;
    wrapped = length - tail;

    if( wrapped < 0 ) {
        memcpy( fifo->data + fifo->ay, src, length );
        fifo->ay += length;
    } else {
        if( wrapped > fifo->ax ) {
            return qfalse;
        }
        memcpy( fifo->data + fifo->ay, src, tail );
        memcpy( fifo->data, src + tail, wrapped );
        fifo->ay = fifo->size;
        fifo->bs = wrapped;
    }

    return qtrue;
}

/*
==============================================================================

						INIT / SHUTDOWN

==============================================================================
*/

/*
=============
Com_FillAPI
=============
*/
void Com_FillAPI( commonAPI_t *api ) {
	api->Print = Com_LevelPrint;
	api->Error = Com_LevelError;
	api->TagMalloc = Z_TagMalloc;
    api->Realloc = Z_Realloc;
	api->Free = Z_Free;
}

/*
=============
Com_Time_m
=============
*/
void Com_Time_m( char *buffer, int bufferSize ) {
	time_t	clock;
	struct tm	*localTime;

	time( &clock );
	localTime = localtime( &clock );

	strftime( buffer, bufferSize, com_time_format->string, localTime );
}

/*
=============
Com_Date_m
=============
*/
static void Com_Date_m( char *buffer, int bufferSize ) {
	time_t	clock;
	struct tm	*localTime;

	time( &clock );
	localTime = localtime( &clock );

	strftime( buffer, bufferSize, com_date_format->string, localTime );
}

static void Com_LastError_f( void ) {
	if( com_errorMsg[0] ) {
		Com_Printf( "%s\n", com_errorMsg );
	} else {
		Com_Printf( "No error.\n" );
	}
}

#ifdef _DEBUG

/*
=============
Com_Error_f

Just throw a fatal error to
test error shutdown procedures
=============
*/
void Com_Error_f( void ) {
	Com_Error( ERR_FATAL, "%s", Cmd_Argv( 1 ) );
}

void Com_ErrorDrop_f( void ) {
	Com_Error( ERR_DROP, "%s", Cmd_Argv( 1 ) );
}

void Com_Freeze_f( void ) {
	int seconds, time;

	if( Cmd_Argc() < 2 ) {
		Com_Printf( "Usage: %s <seconds>\n", Cmd_Argv( 0 ) );
		return;
	}

	seconds = atoi( Cmd_Argv( 1 ) );
	if( seconds < 1 ) {
		return;
	}

	time = Sys_Milliseconds() + seconds * 1000;
	while( Sys_Milliseconds() < time )
		;

}

void Com_Crash_f( void ) {
	*( uint32 * )0 = 0x123456;
}

static void Com_VsnprintfTest_f( void ) {
	char buffer[32];
	int ret;
	qboolean fail;

	/* test if returned length is valid, no overflow, large buffer */
	Com_Printf( "test 1\n" );
	fail = qfalse;
	memset( buffer, '@', sizeof( buffer ) );
	ret = Com_sprintf( buffer, sizeof( buffer ), "%s", "abcd" );
	if( ret != 4 ) {
		Com_Printf( "returned %d instead of %d\n", ret, 4 );
		fail = qtrue;
	}
	if( buffer[4] != 0 ) {
		Com_Printf( "buffer left unterminated\n" );
		fail = qtrue;
	}
	if( buffer[5] != '@' ) {
		Com_Printf( "buffer overflowed\n" );
		fail = qtrue;
	}
	if( !fail ) {
		Com_Printf( "succeeded\n" );
	} else {
		Com_Printf( "failed\n" );
	}

	/* test if returned length is valid, no overflow, small buffer */
	Com_Printf( "test 2\n" );
	fail = qfalse;
	memset( buffer, '@', sizeof( buffer ) );
	ret = Com_sprintf( buffer, 5, "%s", "abcd" );
	if( ret != 4 ) {
		Com_Printf( "returned %d instead of %d\n", ret, 4 );
	}
	if( buffer[4] != 0 ) {
		Com_Printf( "buffer left unterminated\n" );
		fail = qtrue;
	}
	if( buffer[5] != '@' ) {
		Com_Printf( "buffer overflowed\n" );
		fail = qtrue;
	}
	if( !fail ) {
		Com_Printf( "succeeded\n" );
	} else {
		Com_Printf( "failed\n" );
	}

	/* test if returned length is valid, with overflow */
	Com_Printf( "test 3\n" );
	fail = qfalse;
	memset( buffer, '@', sizeof( buffer ) );
	ret = Com_sprintf( buffer, 5, "%s", "abcdefgh" );
	if( ret != 4 ) {
		Com_Printf( "returned %d instead of %d\n", ret, 4 );
		fail = qtrue;
	}
	if( buffer[4] != 0 ) {
		Com_Printf( "buffer left unterminated\n" );
		fail = qtrue;
	}
	if( buffer[5] != '@' ) {
		Com_Printf( "buffer overflowed\n" );
		fail = qtrue;
	}

	if( !fail ) {
		Com_Printf( "succeeded\n" );
	} else {
		Com_Printf( "failed\n" );
	}
}

#endif

const char *Com_FileNameGenerator( const char *path, const char *ext,
        const char *partial, qboolean stripExtension, int state ) {
    static int length, numFiles;
    static char **list;
    static int curpos;
    char *s, *p;

	if( state == 2 ) {
		goto finish;
	}
    
    if( !state ) {
        length = strlen( partial );
        list = FS_ListFiles( path, ext, 0, &numFiles );
        curpos = 0;
    }

    while( curpos < numFiles ) {
        s = list[curpos++];
		if( stripExtension ) {
			p = COM_FileExtension( s );
			*p = 0;
		}
        if( !strncmp( s, partial, length ) ) {
            return s;
        }
    }

finish:
    if( list ) {
        FS_FreeFileList( list );
        list = NULL;
    }
    return NULL;
}

const char *Com_FileNameGeneratorByFilter( const char *path, const char *filter,
        const char *partial, qboolean stripExtension, int state ) {
    static int length, numFiles;
    static char **list;
    static int curpos;
    char *s, *p;

	if( state == 2 ) {
		goto finish;
	}
    
    if( !state ) {
        length = strlen( partial );
        list = FS_ListFiles( path, filter, FS_SEARCH_SAVEPATH |
                FS_SEARCH_BYFILTER, &numFiles );
        curpos = 0;
    }

    while( curpos < numFiles ) {
        s = list[curpos++];
		if( stripExtension ) {
			p = COM_FileExtension( s );
			*p = 0;
		}
        if( !strncmp( s, partial, length ) ) {
            return s;
        }
    }

finish:
    if( list ) {
        FS_FreeFileList( list );
        list = NULL;
    }
    return NULL;
}

#define MAX_LINE_COMMANDS	128

static char	*com_commands[MAX_LINE_COMMANDS];
static int		com_numCommands;

/*
===============
Com_ParseCommandLine

===============
*/
static void Com_ParseCommandLine( char *commandLine ) {
	qboolean inquote = qfalse;

    if( !commandLine ) {
        return;
    }

	com_numCommands = 0;
	while( *commandLine ) {
		if( *commandLine == '\"' ) {
			inquote ^= 1;
		} else if( *commandLine == '+' && !inquote ) {
			com_commands[com_numCommands] = commandLine + 1;
			com_numCommands++;

			*commandLine = 0;

			if( com_numCommands == MAX_LINE_COMMANDS ) {
				break;
			}
		}

		commandLine++;

	}
}

/*
===============
Com_AddEarlyCommands

Adds command line parameters as script statements.
Commands lead with a +, and continue until another +

Set commands are added early, so they are guaranteed to be set before
the client and server initialize for the first time.

Other commands are added late, after all initialization is complete.
===============
*/
static void Com_AddEarlyCommands( qboolean clear ) {
	int		i;
	char	*s;

	for( i = 0; i < com_numCommands; i++ ) {
		s = com_commands[i];
		if( !*s ) {
			continue;
		}
		Cmd_TokenizeString( s, qfalse );
		if( strcmp( Cmd_Argv( 0 ), "set" ) ) {
			continue;
		}
        if( Cmd_Argc() != 3 ) {
            Com_Printf( "Usage: +set <variable> <value>\n" );
        } else {
    		Cvar_SetEx( Cmd_Argv( 1 ), Cmd_Argv( 2 ), CVAR_SET_COMMAND_LINE );
        }
		if( clear ) {
			*s = 0;
		}
	}
}

/*
=================
Com_AddLateCommands

Adds command line parameters as script statements
Commands lead with a + and continue until another +

Returns qtrue if any late commands were added, which
will keep the demoloop from immediately starting

Assumes +set commands are already filtered out
=================
*/
static qboolean Com_AddLateCommands( void ) {
	int		i;
	char	*s;
	qboolean ret = qfalse;

	for( i = 0; i < com_numCommands; i++ ) {
		s = com_commands[i];
		if( !*s ) {
			continue;
		}
		Cbuf_AddText( s );
		Cbuf_AddText( "\n" );
		ret = qtrue;
	}

	if( ret ) {
		Cbuf_Execute();
	}

	return ret;
}


/*
=================
Qcommon_Init
=================
*/
void Qcommon_Init( char *commandLine ) {
	static const char *version = APPLICATION " " VERSION " " __DATE__ " " BUILDSTRING " " CPUSTRING;

	if( _setjmp( abortframe ) )
		Sys_Error( "Error during initialization: %s", com_errorMsg );

	Com_Printf( S_COLOR_CYAN "%s\n", version );

	Com_ParseCommandLine( commandLine );

	// prepare enough of the subsystems to handle
	// cvar and command buffer management
	Z_Init();
	MSG_Init();
	Cbuf_Init();
	Cmd_Init();
	Cvar_Init();
	Key_Init();
	Prompt_Init();
	Con_Init();
	
	Com_FillAPI( &com );

	//
	// init commands and vars
	//
	host_speeds = Cvar_Get ("host_speeds", "0", 0);
	developer = Cvar_Get ("developer", "0", 0);
	timescale = Cvar_Get ("timescale", "1", CVAR_CHEAT );
	fixedtime = Cvar_Get ("fixedtime", "0", CVAR_CHEAT );
	logfile_active = Cvar_Get( "logfile", "0", 0 );
	logfile_flush = Cvar_Get( "logfile_flush", "0", 0 );
	logfile_name = Cvar_Get( "logfile_name", COM_LOGFILE_NAME, 0 );
	logfile_prefix = Cvar_Get( "logfile_prefix", "", 0 );
#ifdef DEDICATED_ONLY
	dedicated = Cvar_Get ("dedicated", "1", CVAR_ROM);
#else
	dedicated = Cvar_Get ("dedicated", "0", CVAR_NOSET);
#endif
	sv_running = Cvar_Get( "sv_running", "0", CVAR_ROM );
	sv_paused = Cvar_Get( "sv_paused", "0", CVAR_ROM );
	cl_running = Cvar_Get( "cl_running", "0", CVAR_ROM );
	cl_paused = Cvar_Get( "cl_paused", "0", CVAR_ROM );
	com_timedemo = Cvar_Get( "timedemo", "0", CVAR_CHEAT );
	com_sleep = Cvar_Get( "com_sleep", "1", 0 );
	com_date_format = Cvar_Get( "com_date_format", "%Y-%m-%d", 0 );
	com_time_format = Cvar_Get( "com_time_format", "%H:%M:%S", 0 );
	com_debug_break = Cvar_Get( "com_debug_break", "0", 0 );
	Cvar_Get( "version", version, CVAR_SERVERINFO|CVAR_ROM );

	Cmd_AddCommand ("z_stats", Z_Stats_f);

#ifndef DEDICATED_ONLY
	if( !dedicated->integer ) {
		Cmd_AddCommand( "writeconfig", Com_WriteConfig_f );
	}
#endif

	Cmd_AddMacro( "com_date", Com_Date_m );
	Cmd_AddMacro( "com_time", Com_Time_m );

    // add any system-wide configuration files
    Sys_AddDefaultConfig();
    Cbuf_Execute();
	
	// we need to add the early commands twice, because
	// a basedir or cddir needs to be set before execing
	// config files, but we want other parms to override
	// the settings of the config files
	Com_AddEarlyCommands( qfalse );

    // do not accept CVAR_NOSET variable changes anymore
    com_initialized = qtrue;

	Sys_Init();
	FS_Init();

    Sys_RunConsole();

	// after FS is initialized, open logfile
	logfile_active->changed = logfile_active_changed;
	logfile_flush->changed = logfile_param_changed;
	logfile_name->changed = logfile_param_changed;
	logfile_active_changed( logfile_active );

	Cbuf_AddText( "exec "COM_DEFAULTCFG_NAME"\n" );
	Cbuf_Execute();
	
	Cbuf_AddText( "exec "COM_CONFIG_NAME"\n" );
	Cbuf_Execute();

	Cbuf_AddText( "exec "COM_AUTOEXECCFG_NAME"\n" );
	Cbuf_Execute();

	Com_AddEarlyCommands( qtrue );

#ifdef _DEBUG
	Cmd_AddCommand( "error", Com_Error_f );
	Cmd_AddCommand( "errordrop", Com_ErrorDrop_f );
	Cmd_AddCommand( "freeze", Com_Freeze_f );
	Cmd_AddCommand( "crash", Com_Crash_f );
	Cmd_AddCommand( "vstest", Com_VsnprintfTest_f );
#endif

	Cmd_AddCommand( "lasterror", Com_LastError_f );

	Cmd_AddCommand( "quit", Com_Quit );

	srand( Sys_Milliseconds() );

	Netchan_Init();
	CM_Init();
	SV_Init();
	CL_Init();
	NET_Init();

	if( dedicated->integer ) {
		NET_Config( NET_SERVER );
	}

    Sys_RunConsole();

	// add + commands from command line
	if( !Com_AddLateCommands() ) {
		// if the user didn't give any commands, run default action
		if( dedicated->integer ) {
			Cbuf_AddText( "dedicated_start\n" );
		} else {
			// TODO
			//Cbuf_AddText( "d1\n" );
		}
		Cbuf_Execute();
	} else {
		// the user asked for something explicit
		// so drop the loading plaque
		SCR_EndLoadingPlaque();
	}


	Com_Printf( "====== " APPLICATION " initialized ======\n\n" );
	Com_Printf( S_COLOR_CYAN APPLICATION " " VERSION ", " __DATE__ "\n"
#ifdef USE_ZLIB
                S_COLOR_RESET   "w/ zlib " ZLIB_VERSION "\n"
#endif
    );
	Com_Printf( "http://q2pro.sf.net\n\n" );

	com_numCommands = 0;

	com_eventTime = Sys_Realtime();
}

/*
==============
Com_ProcessEvents
==============
*/
void Com_ProcessEvents( void ) {
	neterr_t ret;

    do {
        ret = NET_GetPacket( NS_SERVER );
        if( ret == NET_AGAIN ) {
            break;
        }
		SV_PacketEvent( ret );
    } while( ret == NET_OK );

    Sys_RunConsole();

#ifndef DEDICATED_ONLY
    do {
        ret = NET_GetPacket( NS_CLIENT );
        if( ret == NET_AGAIN ) {
            break;
        }
		if( cl_running->integer ) {
			CL_PacketEvent( ret );
		}
    } while( ret == NET_OK );

	CL_PumpEvents();
	CL_InputFrame();
#endif
}

#ifndef DEDICATED_ONLY
/*
==============
Com_ProcessLoopback
==============
*/
static void Com_ProcessLoopback( void ) {
	int i;

	memset( &net_from, 0, sizeof( net_from ) );
	net_from.type = NA_LOOPBACK;

	// Process loopback packets
	for( i = 0; i < 2; i++ ) {
		while( NET_GetLoopPacket( NS_SERVER ) ) {
			if( sv_running->integer ) {
				SV_PacketEvent( NET_OK );
			}
		}

		while( NET_GetLoopPacket( NS_CLIENT ) ) {
			if( cl_running->integer ) {
				CL_PacketEvent( NET_OK );
			}
		}
	}
}
#endif

/*
=================
Qcommon_Frame
=================
*/
void Qcommon_Frame( void ) {
	int		time_before, time_event, time_between, time_after;
	uint32	realTime, msec;

	if( _setjmp( abortframe ) ) {
		return;			// an ERR_DROP was thrown
	}

	time_before = time_event = time_between = time_after = 0;

	if( host_speeds->integer )
		time_before = Sys_Milliseconds();

	do {
		Com_ProcessEvents();
		realTime = Sys_Realtime();
        if( com_eventTime > realTime ) {
            com_eventTime = realTime;
        }
		msec = realTime - com_eventTime;
	} while( msec < 1 );
	com_eventTime = realTime;

    if( msec > 250 ) {
        Com_DPrintf( "Hitch warning: %u msec frame time\n", msec );
        msec = 100; // time was unreasonable,
                    // host OS was hibernated or something
    }

	if( fixedtime->integer ) {
		Cvar_ClampInteger( fixedtime, 1, 1000 );
		msec = fixedtime->integer;
	} else if( timescale->value > 0 ) {
		msec *= timescale->value;
		if( msec < 1 )
			msec = 1;
	}

	// this is the only place where console commands are processed.
    Cbuf_Execute();

	if( host_speeds->integer )
		time_event = Sys_Milliseconds();

	SV_Frame( msec );

	if( host_speeds->integer )
		time_between = Sys_Milliseconds();

#ifndef DEDICATED_ONLY
	Com_ProcessLoopback();
#endif

	CL_Frame( msec );

	if( host_speeds->integer )
		time_after = Sys_Milliseconds();

	if( host_speeds->integer ) {
		int			all, ev, sv, gm, cl, rf;

		all = time_after - time_before;
		ev = time_event - time_before;
		sv = time_between - time_event;
		cl = time_after - time_between;
		gm = time_after_game - time_before_game;
		rf = time_after_ref - time_before_ref;
		sv -= gm;
		cl -= rf;

		Com_Printf( "all:%3i ev:%3i sv:%3i gm:%3i cl:%3i rf:%3i\n",
			all, ev, sv, gm, cl, rf );
	}
    
	cvar_infoModified = 0;

    com_localTime += msec;
	com_framenum++;
}

/*
=================
Qcommon_Shutdown
=================
*/
void Qcommon_Shutdown( qboolean fatalError ) {
#ifndef DEDICATED_ONLY
	if( !fatalError && dedicated && !dedicated->integer ) {
		Com_WriteConfiguration( COM_CONFIG_NAME );
	}
#endif
    NET_Shutdown();
	LogFile_Close();
	FS_Shutdown( qtrue );
}

