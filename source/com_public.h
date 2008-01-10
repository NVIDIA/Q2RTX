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

// com_public.h -- interfaces for common subsystems

#ifndef _COM_PUBLIC_H_
#define _COM_PUBLIC_H_

/*
==============================================================

CMD

==============================================================
*/

typedef enum cbufExecWhen_e {
	EXEC_NOW,		// don't return until completed
	EXEC_INSERT,	// insert at current position, but don't run yet
	EXEC_APPEND		// add to end of the command buffer
} cbufExecWhen_t;

typedef void ( *xcommand_t )( void );
typedef int ( *xmacro_t )( char *, int );

typedef struct cmdreg_s {
	const char		*name;
	xcommand_t		function;
	xgenerator_t	generator1;
	xgenerator_t	generator2;
	xgenerator_t	generator3;
} cmdreg_t;

typedef struct cmdAPI_s {
	void 	(*ExecuteText)( cbufExecWhen_t exec_when, const char *text );
	int 	(*Argc)( void );
	char 	*(*Argv)( int arg );
	void 	(*ArgvBuffer)( int arg, char *buffer, int bufferSize );
	char 	*(*Args)( void );
	void 	(*ArgsBuffer)( char *buffer, int bufferSize );
	char 	*(*ArgsFrom)( int from );
	void 	(*Register)( const cmdreg_t *regs );
	void 	(*Deregister)( const cmdreg_t *regs );
	void 	(*AddCommand)( const char *cmd_name, xcommand_t function );
	void 	(*RemoveCommand)( const char *cmd_name );
	xcommand_t	(*FindFunction)( const char *name );
	xmacro_t	(*FindMacroFunction)( const char *name );
	xgenerator_t	(*FindGenerator)( const char *name, int index );
} cmdAPI_t;

extern	cmdAPI_t	cmd;

/*
==============================================================

CVAR

==============================================================
*/

#define CVAR_CHEAT			( 1 << 5 )
#define CVAR_PRIVATE		( 1 << 6 )
#define CVAR_ROM			( 1 << 7 )
#define CVAR_LATCHED		( 1 << 8 )
#define CVAR_USER_CREATED	( 1 << 9 )
#define CVAR_DEFAULTS_MIXED	( 1 << 10 )
#define CVAR_GAME      	    ( 1 << 11 )

#define CVAR_INFOMASK		(CVAR_USERINFO|CVAR_SERVERINFO)
#define CVAR_EXTENDED_MASK	(~31)

typedef struct cvarAPI_s {
	float 	(*VariableValue)( const char *var_name );
	int 	(*VariableInteger)( const char *var_name );
	char 	*(*VariableString)( const char *var_name );
	void 	(*VariableStringBuffer)( const char *var_name, char *buffer, int bufferSize );
	cvar_t 	*(*Get)( const char *var_name, const char *var_value, int flags );
	cvar_t 	*(*Set)( const char *var_name, const char *value );
    cvar_t  *(*Find)( const char *var_name );
	void 	(*SetValue)( const char *var_name, float value );
	void 	(*SetInteger)( const char *var_name, int value );
} cvarAPI_t;

extern	cvarAPI_t	cvar;

/*
==============================================================

FILESYSTEM

==============================================================
*/

#define MAX_LISTED_FILES	4096

typedef struct fsFileInfo_s {
	int		size;
	time_t	ctime;
    time_t  mtime;
    char    name[1];
} fsFileInfo_t;

/* bits 0 - 1, enum */
#define		FS_MODE_APPEND			0x00000000
#define		FS_MODE_READ			0x00000001
#define		FS_MODE_WRITE			0x00000002
#define		FS_MODE_RDWR			0x00000003
#define		FS_MODE_MASK			0x00000003

/* bits 0 - 1, enum */
#define		FS_SEARCHDIRS_NO			0x00000000
#define		FS_SEARCHDIRS_YES			0x00000001
#define		FS_SEARCHDIRS_ONLY			0x00000002
#define		FS_SEARCHDIRS_RESERVED		0x00000003
#define		FS_SEARCHDIRS_MASK			0x00000003

/* bit 2, enum */
#define FS_FLUSH_NONE			0x00000000
#define FS_FLUSH_SYNC			0x00000004
#define	FS_FLUSH_MASK			0x00000004

/* bits 3 - 4, enum */
#define	FS_TYPE_ANY			0x00000000
#define	FS_TYPE_REAL		0x00000008
#define	FS_TYPE_PAK			0x00000010
#define	FS_TYPE_RESERVED	0x00000018
#define	FS_TYPE_MASK		0x00000018

/* bits 5 - 6, enum */
#define	FS_PATH_ANY			0x00000000
#define	FS_PATH_RESERVED	0x00000020
#define	FS_PATH_BASE		0x00000040
#define	FS_PATH_GAME		0x00000060
#define	FS_PATH_MASK		0x00000060

/* bits 7 - 10, flag */
#define	FS_SEARCH_BYFILTER		0x00000080
#define	FS_SEARCH_SAVEPATH		0x00000100
#define	FS_SEARCH_EXTRAINFO		0x00000200
#define	FS_SEARCH_NOSORT		0x00000400

/* bits 7 - 8, flag */
#define	FS_FLAG_RAW				0x00000080
#define	FS_FLAG_CACHE			0x00000100

typedef struct fsAPI_s {
	void 	(*FCloseFile)( fileHandle_t f );
	int 	(*Read)( void *buffer, int len, fileHandle_t f );
	int 	(*Write)( const void *buffer, int len, fileHandle_t f );
	int 	(*FOpenFile)( const char *filename, fileHandle_t *f, int mode );
    void	(*FPrintf)( fileHandle_t f, const char *format, ... );
    int     (*ReadLine)( fileHandle_t f, char *buffer, int size );
	int 	(*Tell)( fileHandle_t f );
	int 	(*RawTell)( fileHandle_t f );
	int 	(*LoadFile)( const char *path, void **buffer );
	int 	(*LoadFileEx)( const char *path, void **buffer, int flags );
    void    *(*AllocTempMem)( int length );
	void 	(*FreeFile)( void *buffer );
	void 	**(*ListFiles)( const char *path, const char *extension, int flags, int *numFiles );
	void 	(*FreeList)( void **list );
} fsAPI_t;

extern	fsAPI_t		fs;

/*
==============================================================

COMMON

==============================================================
*/

#define	MAXPRINTMSG		4096

// memory tags to allow dynamic memory to be cleaned up
typedef enum memtag_e {
	TAG_FREE,				// should have never been set
	TAG_STATIC,

	TAG_GENERAL,
	TAG_CMD,
	TAG_CVAR,
	TAG_FILESYSTEM,
	TAG_RENDERER,
	TAG_UI,
    TAG_SERVER,
	TAG_MVD,
    TAG_SOUND,
	TAG_CMODEL,

	TAG_MAX,

	TAG_GAME	= 765,		// clear when unloading the dll
	TAG_LEVEL	= 766		// clear when loading a new level
} memtag_t;

typedef struct commonAPI_s {
	void	(* q_noreturn Error)( comErrorType_t code, const char *str );
	void	(*Print)( comPrintType_t type, const char *str );

	void	*(*TagMalloc)( size_t size, memtag_t tag );
	void	*(*Realloc)( void *ptr, size_t size );
	void	(*Free)( void *ptr );
} commonAPI_t;

extern	commonAPI_t	com;

/*
==============================================================

SYSTEM

==============================================================
*/

typedef struct {
	byte	*base;
	int		maxsize;
	int		cursize;
    int     mapped;
} mempool_t;

typedef struct sysAPI_s {
	int		(*Milliseconds)( void );
	char	*(*GetClipboardData)( void );
	void	(*SetClipboardData)( const char *data );
	void	(*HunkBegin)( mempool_t *pool, int maxsize );
	void	*(*HunkAlloc)( mempool_t *pool, int size );
	void	(*HunkEnd)( mempool_t *pool );
	void	(*HunkFree)( mempool_t *pool );
} sysAPI_t;

extern sysAPI_t		sys;

/*
==============================================================

MODULES

==============================================================
*/

// if api_version is different, the dll cannot be used
#define MODULES_APIVERSION	314

typedef enum moduleQuery_e {
	MQ_GETINFO,
	MQ_GETCAPS,
	MQ_SETUPAPI
} moduleQuery_t;

typedef enum moduleCapability_e {
	MCP_EMPTY			= (0<<0),
	MCP_VIDEO_SOFTWARE	= (1<<0),
	MCP_VIDEO_OPENGL	= (1<<1),
	MCP_REFRESH			= (1<<2),
	MCP_SOUND			= (1<<3),
	MCP_INPUT			= (1<<4),
	MCP_UI				= (1<<5)
} moduleCapability_t;

typedef struct moduleInfo_s {
	// if api_version is different, the dll cannot be used
	int		api_version;
	char	fullname[MAX_QPATH];
	char	author[MAX_QPATH];
} moduleInfo_t;

// this is the only function actually exported at the linker level
typedef void	*(*moduleEntry_t)( int, void * );

// API types used in MQ_SETUPAPI query
typedef enum api_type_e {
	API_CMD,
	API_CVAR,
	API_FS,
	API_COMMON,
	API_KEYS,
	API_SYSTEM,
	API_VIDEO_SOFTWARE,
	API_VIDEO_OPENGL,
	API_REFRESH,
	API_INPUT,
	API_UI,
	API_CLIENT
} api_type_t;

// passed along with MQ_SETUPAPI query
typedef void	(*APISetupCallback_t)( int, void * );

#if 0
typedef enum {
    // exported
	API_REFRESH,
	API_UI,
    API_GAME,

    // imported
	API_CMD,
	API_CVAR,
	API_FS,
	API_COMMON,
	API_KEYS,
	API_SYSTEM,
	API_VIDEO_SOFTWARE,
	API_VIDEO_OPENGL
} api_type_t;

typedef void	(*api_callback_t)( int, void * );

// this is the only function actually exported at the linker level
typedef void	*(*moduleEntry_t)( api_type_t, api_callback_t );
#endif

#endif // _COM_PUBLIC_H_
