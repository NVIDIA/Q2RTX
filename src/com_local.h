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

// qcommon.h -- definitions common between client and server, but not game.dll

#include <config.h>
#include "q_shared.h"
#include <errno.h>
#include "error.h"

#if USE_CLIENT
#define APPLICATION     "q2pro"
#else
#define APPLICATION     "q2proded"
#endif

/*
==============================================================

CMD

Command text buffering and command execution

==============================================================
*/

#define CMD_BUFFER_SIZE     ( 1 << 16 ) // bumped max config size up to 64K

#define ALIAS_LOOP_COUNT    16

// where did current command come from?
typedef enum {
    FROM_STUFFTEXT,
    FROM_RCON,
    //FROM_GAME,
    FROM_CONSOLE,
    FROM_CMDLINE,
    FROM_CODE
} from_t;

typedef struct cmdbuf_s {
    from_t      from;
    char        *text; // may not be NULL terminated
    size_t      cursize;
    size_t      maxsize;
    int         waitCount;
    int         aliasCount; // for detecting runaway loops
    void        (*exec)( struct cmdbuf_s *, const char * );
} cmdbuf_t;

// generic console buffer
extern char         cmd_buffer_text[CMD_BUFFER_SIZE];
extern cmdbuf_t     cmd_buffer;

extern cmdbuf_t     *cmd_current;

/*

Any number of commands can be added in a frame, from several different sources.
Most commands come from either keybindings or console line input, but remote
servers can also send across commands and entire text files can be execed.

The + command line options are also added to the command buffer.

The game starts with a Cbuf_AddText ("exec quake.rc\n"); Cbuf_Execute ();

*/

void Cbuf_Init( void );
// allocates an initial text buffer that will grow as needed

void Cbuf_AddText( cmdbuf_t *buf, const char *text );
// as new commands are generated from the console or keybindings,
// the text is added to the end of the command buffer.

void Cbuf_InsertText( cmdbuf_t *buf, const char *text );
// when a command wants to issue other commands immediately, the text is
// inserted at the beginning of the buffer, before any remaining unexecuted
// commands.

void Cbuf_Execute( cmdbuf_t *buf );
// Pulls off \n terminated lines of text from the command buffer and sends
// them through Cmd_ExecuteString.  Stops when the buffer is empty.
// Normally called once per frame, but may be explicitly invoked.
// Do not call inside a command function!

char *Cbuf_Alloc( cmdbuf_t *buf, size_t len );

//===========================================================================

/*

Command execution takes a null terminated string, breaks it into tokens,
then searches for a command or variable that matches the first token.

*/

typedef struct genctx_s {
    const char  *partial;
    size_t length;
    int argnum;
    char **matches;
    int count, size;
    void *data;
    qboolean ignorecase;
} genctx_t;

typedef void ( *xcommand_t )( void );
typedef void ( *xcommandex_t )( cmdbuf_t * );
typedef size_t ( *xmacro_t )( char *, size_t );
typedef void ( *xcompleter_t )( struct genctx_s *, int );

typedef struct cmd_macro_s {
    struct cmd_macro_s  *next, *hashNext;
    const char          *name;
    xmacro_t            function;
} cmd_macro_t;

typedef struct {
    const char *sh, *lo, *help;
} cmd_option_t;

typedef struct cmdreg_s {
    const char      *name;
    xcommand_t      function;
    xcompleter_t    completer;
} cmdreg_t;

void Cmd_Init( void );

qboolean Cmd_Exists( const char *cmd_name );
// used by the cvar code to check for cvar / command name overlap

void Cmd_ExecTrigger( const char *string );

xcommand_t Cmd_FindFunction( const char *name );
cmd_macro_t *Cmd_FindMacro( const char *name );
xcompleter_t Cmd_FindCompleter( const char *name );

char *Cmd_AliasCommand( const char *name );
void Cmd_AliasSet( const char *name, const char *cmd );

void Cmd_Command_g( genctx_t *ctx );
void Cmd_Alias_g( genctx_t *ctx );
void Cmd_Macro_g( genctx_t *ctx );
void Cmd_Config_g( genctx_t *ctx );
void Cmd_Option_c( const cmd_option_t *opt, xgenerator_t g, genctx_t *ctx, int argnum );
// attempts to match a partial command for automatic command line completion
// returns NULL if nothing fits

void Cmd_TokenizeString( const char *text, qboolean macroExpand );
// Takes a null terminated string.  Does not need to be /n terminated.
// breaks the string up into arg tokens.

void Cmd_ExecuteCommand( cmdbuf_t *buf );
// execute already tokenized string

void Cmd_ExecuteString( cmdbuf_t *buf, const char *text );
// Parses a single line of text into arguments and tries to execute it
// as if it was typed at the console

char *Cmd_MacroExpandString( const char *text, qboolean aliasHack );

void Cmd_Register( const cmdreg_t *reg );
void Cmd_AddCommand( const char *cmd_name, xcommand_t function );
// called by the init functions of other parts of the program to
// register commands and functions to call for them.
// The cmd_name is referenced later, so it should not be in temp memory
// if function is NULL, the command will be forwarded to the server
// as a clc_stringcmd instead of executed locally
void Cmd_Deregister( const cmdreg_t *reg );
void Cmd_RemoveCommand( const char *cmd_name );

void Cmd_AddMacro( const char *name, xmacro_t function );

from_t  Cmd_From( void );
int     Cmd_Argc( void );
char    *Cmd_Argv( int arg );
char    *Cmd_Args( void );
char    *Cmd_RawArgs( void );
char    *Cmd_ArgsFrom( int from );
char    *Cmd_RawArgsFrom( int from );
size_t  Cmd_ArgsBuffer( char *buffer, size_t size );
size_t  Cmd_ArgvBuffer( int arg, char *buffer, size_t size );
size_t  Cmd_ArgOffset( int arg );
int     Cmd_FindArgForOffset( size_t offset );
char    *Cmd_RawString( void );
void    Cmd_Shift( void );
// The functions that execute commands get their parameters with these
// functions. Cmd_Argv () will return an empty string, not a NULL
// if arg > argc, so string operations are always safe.

void Cmd_Alias_f( void );

void Cmd_WriteAliases( qhandle_t f );

#define EXEC_TRIGGER( var ) \
    do { \
        if( (var)->string[0] ) { \
            Cbuf_AddText( &cmd_buffer, (var)->string ); \
        } \
    } while( 0 )

extern int cmd_optind;
extern char *cmd_optarg;
extern char *cmd_optopt;

int Cmd_ParseOptions( const cmd_option_t *opt );
void Cmd_PrintHelp( const cmd_option_t *opt );
void Cmd_PrintUsage( const cmd_option_t *opt, const char *suffix );
void Cmd_PrintHint( void );

const char *Cmd_Completer( const cmd_option_t *opt, const char *partial,
    int argnum, int state, xgenerator_t generator );

/*
==============================================================

CVAR

==============================================================
*/

/*

cvar_t variables are used to hold scalar or string variables that can be changed or displayed at the console or prog code as well as accessed directly
in C code.

The user can access cvars from the console in three ways:
r_draworder            prints the current value
r_draworder 0        sets the current value to 0
set r_draworder 0    as above, but creates the cvar if not present
Cvars are restricted from having the same names as commands to keep this
interface from being ambiguous.
*/

#define CVAR_CHEAT          ( 1 << 5 )
#define CVAR_PRIVATE        ( 1 << 6 )
#define CVAR_ROM            ( 1 << 7 )
#define CVAR_MODIFIED       ( 1 << 8 )
#define CVAR_CUSTOM         ( 1 << 9 )
#define CVAR_VOLATILE       ( 1 << 10 )
#define CVAR_GAME           ( 1 << 11 )
#define CVAR_FILES          ( 1 << 13 ) // refresh files for now
#define CVAR_REFRESH        ( 1 << 14 )
#define CVAR_SOUND          ( 1 << 15 )

#define CVAR_INFOMASK       (CVAR_USERINFO|CVAR_SERVERINFO)
#define CVAR_MODIFYMASK     (CVAR_INFOMASK|CVAR_FILES|CVAR_REFRESH|CVAR_SOUND)
#define CVAR_EXTENDED_MASK  (~31)

extern cvar_t   *cvar_vars;
extern int      cvar_modified;

void Cvar_SetByVar( cvar_t *var, const char *value, from_t from );

#define Cvar_Reset( x ) \
    Cvar_SetByVar( x, (x)->default_string, FROM_CODE )

cvar_t *Cvar_UserSet( const char *var_name, const char *value );

cvar_t *Cvar_ForceSet (const char *var_name, const char *value);
// will set the variable even if NOSET or LATCH

cvar_t *Cvar_FullSet( const char *var_name, const char *value,
                      int flags, from_t from );

int Cvar_ClampInteger( cvar_t *var, int min, int max );
float Cvar_ClampValue( cvar_t *var, float min, float max );

xgenerator_t Cvar_FindGenerator( const char *var_name );

void Cvar_Variable_g( genctx_t *ctx );
void Cvar_Default_g( genctx_t *ctx );
// attempts to match a partial variable name for command line completion
// returns NULL if nothing fits

int Cvar_CountLatchedVars( void );
void Cvar_GetLatchedVars (void);
// any CVAR_LATCHEDED variables that have been set will now take effect

void Cvar_FixCheats( void );

void Cvar_Command( cvar_t *v );
// called by Cmd_ExecuteString when Cmd_Argv(0) doesn't match a known
// command.  Returns qtrue if the command was a variable reference that
// was handled. (print or change)

void Cvar_WriteVariables( qhandle_t f, int mask, qboolean modified );
// appends lines containing "set variable value" for all variables
// with matching flags

void Cvar_Init (void);

size_t Cvar_BitInfo( char *info, int bit );

cvar_t *Cvar_ForceSetEx( const char *var_name, const char *value, int flags );

qboolean CL_CheatsOK( void );

qboolean Cvar_Exists( const char *name );

cvar_t *Cvar_Get( const char *var_name, const char *value, int flags );
// creates the variable if it doesn't exist, or returns the existing one
// if it exists, the value will not be changed, but flags will be ORed in
// that allows variables to be unarchived without needing bitflags

cvar_t *Cvar_Ref( const char *var_name );

cvar_t *Cvar_Set( const char *var_name, const char *value );
cvar_t *Cvar_SetEx( const char *var_name, const char *value, from_t from );
// will create the variable if it doesn't exist

void Cvar_SetValue( cvar_t *var, float value, from_t from );
void Cvar_SetInteger( cvar_t *var, int value, from_t from );
void Cvar_SetHex( cvar_t *var, int value, from_t from );
// expands value to a string and calls Cvar_Set

float Cvar_VariableValue( const char *var_name );
int Cvar_VariableInteger( const char *var_name );
// returns 0 if not defined or non numeric

char *Cvar_VariableString( const char *var_name );
// returns an empty string if not defined

#define Cvar_VariableStringBuffer( name, buffer, size ) \
    Q_strlcpy( buffer, Cvar_VariableString( name ), size )

cvar_t *Cvar_FindVar( const char *var_name );

void Cvar_Set_f( void );

/*
==============================================================

ZONE

==============================================================
*/

#define Z_Malloc( size )        Z_TagMalloc( size, TAG_GENERAL )
#define Z_Mallocz( size )       Z_TagMallocz( size, TAG_GENERAL )
#define Z_Reserve( size )       Z_TagReserve( size, TAG_GENERAL )
#define Z_CopyString( string )  Z_TagCopyString( string, TAG_GENERAL )

// memory tags to allow dynamic memory to be cleaned up
// game DLL has separate tag namespace starting at TAG_MAX
typedef enum memtag_e {
    TAG_FREE,       // should have never been set
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

    TAG_MAX
} memtag_t;

// may return pointer to static memory
char    *Cvar_CopyString( const char *in );

void    Z_Free( void *ptr );
void    *Z_TagMalloc( size_t size, memtag_t tag ) q_malloc;
void    *Z_TagMallocz( size_t size, memtag_t tag ) q_malloc;
char    *Z_TagCopyString( const char *in, memtag_t tag ) q_malloc;
void    Z_FreeTags( memtag_t tag );
void    Z_LeakTest( memtag_t tag );
void    Z_Check( void );

void    Z_TagReserve( size_t size, memtag_t tag );
void    *Z_ReservedAlloc( size_t size ) q_malloc;
void    *Z_ReservedAllocz( size_t size ) q_malloc;
char    *Z_ReservedCopyString( const char *in ) q_malloc;

/*
==============================================================

MATH

==============================================================
*/

#define NUMVERTEXNORMALS    162
extern const vec3_t bytedirs[NUMVERTEXNORMALS];

int DirToByte( const vec3_t dir );
void ByteToDir( int index, vec3_t dir );

void SetPlaneType( cplane_t *plane );
void SetPlaneSignbits( cplane_t *plane );

#define BOX_INFRONT     1
#define BOX_BEHIND      2
#define BOX_INTERSECTS  3

int BoxOnPlaneSide( vec3_t emins, vec3_t emaxs, cplane_t *p );

static inline int BoxOnPlaneSideFast( vec3_t emins, vec3_t emaxs, cplane_t *p ) {
    // fast axial cases
    if( p->type < 3 ) {
        if( p->dist <= emins[p->type] )
            return BOX_INFRONT;
        if( p->dist >= emaxs[p->type] )
            return BOX_BEHIND;
        return BOX_INTERSECTS;
    }

    // slow generic case
    return BoxOnPlaneSide( emins, emaxs, p );
}

static inline vec_t PlaneDiffFast( vec3_t v, cplane_t *p ) {
    // fast axial cases
    if( p->type < 3 ) {
        return v[p->type] - p->dist;
    }

    // slow generic case
    return PlaneDiff( v, p );
}


/*
==============================================================

MISC

==============================================================
*/

#define MAXPRINTMSG     4096

typedef enum {
    COLOR_BLACK,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_YELLOW,
    COLOR_BLUE,
    COLOR_CYAN,
    COLOR_MAGENTA,
    COLOR_WHITE,

    COLOR_ALT,
    COLOR_NONE
} color_index_t;

typedef enum {
    KILL_RESTART,
    KILL_DISCONNECT,
    KILL_DROP
} killtype_t;

typedef struct {
    const char *name;
    void (* const func)( void );
} ucmd_t;

static inline const ucmd_t *Com_Find( const ucmd_t *u, const char *c ) {
    for( ; u->name; u++ ) {
        if( !strcmp( c, u->name ) ) {
            return u;
        }
    }
    return NULL;
}

typedef struct string_entry_s {
    struct string_entry_s *next;
    char string[1];
} string_entry_t;

typedef void (*rdflush_t)( int target, char *buffer, size_t len );

void        Com_BeginRedirect (int target, char *buffer, size_t buffersize, rdflush_t flush);
void        Com_EndRedirect (void);

#ifdef _WIN32
void        Com_AbortFrame( void );
#endif

void        Com_Quit( const char *reason, killtype_t type ) q_noreturn;

void        Com_SetColor( color_index_t color );

byte        COM_BlockSequenceCRCByte (byte *base, size_t length, int sequence);

void        Com_Address_g( genctx_t *ctx );
void        Com_Generic_c( genctx_t *ctx, int argnum );
#if USE_CLIENT
void        Com_Color_g( genctx_t *ctx );
#endif
void        Com_PlayerToEntityState( const player_state_t *ps, entity_state_t *es );

int         Com_WildCmp( const char *filter, const char *string, qboolean ignoreCase );
unsigned    Com_HashString( const char *s, unsigned size );

qboolean    Prompt_AddMatch( genctx_t *ctx, const char *s );

size_t      Com_FormatTime( char *buffer, size_t size, time_t t );
size_t      Com_FormatTimeLong( char *buffer, size_t size, time_t t );
size_t      Com_TimeDiff( char *buffer, size_t size,
                time_t *p, time_t now );
size_t      Com_TimeDiffLong( char *buffer, size_t size,
                time_t *p, time_t now );

size_t      Com_Time_m( char *buffer, size_t size );
size_t      Com_Uptime_m( char *buffer, size_t size );
size_t      Com_UptimeLong_m( char *buffer, size_t size );

size_t      Com_FormatSize( char *dest, size_t destsize, off_t bytes );
size_t      Com_FormatSizeLong( char *dest, size_t destsize, off_t bytes );

uint32_t    Com_BlockChecksum( void *buffer, size_t len );

void        Com_PageInMemory( void *buffer, size_t size );

color_index_t Com_ParseColor( const char *s, color_index_t last );

#ifdef __unix__
void        Com_FlushLogs( void );
#endif

#if USE_CLIENT
#define Com_IsDedicated() ( dedicated->integer != 0 )
#else
#define Com_IsDedicated() 1
#endif

#ifdef _DEBUG
#define Com_DPrintf(...) \
    if( developer && developer->integer ) \
        Com_LPrintf( PRINT_DEVELOPER, __VA_ARGS__ )
#else
#define Com_DPrintf(...)
#endif

#ifdef _DEBUG
extern cvar_t   *developer;
#endif
extern cvar_t   *dedicated;
#if USE_CLIENT
extern cvar_t   *host_speeds;
#endif
extern cvar_t   *com_version;

#if USE_CLIENT
extern cvar_t   *cl_running;
extern cvar_t   *cl_paused;
#endif
extern cvar_t   *sv_running;
extern cvar_t   *sv_paused;
extern cvar_t   *com_timedemo;
extern cvar_t   *com_sleep;

extern cvar_t   *allow_download;
extern cvar_t   *allow_download_players;
extern cvar_t   *allow_download_models;
extern cvar_t   *allow_download_sounds;
extern cvar_t   *allow_download_maps;
extern cvar_t   *allow_download_textures;
extern cvar_t   *allow_download_pics;
extern cvar_t   *allow_download_others;

extern cvar_t   *rcon_password;

#if USE_CLIENT
// host_speeds times
extern unsigned     time_before_game;
extern unsigned     time_after_game;
extern unsigned     time_before_ref;
extern unsigned     time_after_ref;
#endif

extern unsigned     com_eventTime; // system time of the last event
extern unsigned     com_localTime; // milliseconds since Q2 startup
extern unsigned     com_framenum;
extern qboolean     com_initialized;
extern time_t       com_startTime;

extern qhandle_t    com_logFile;

#if USE_CLIENT || USE_MVD_CLIENT || USE_MVD_SERVER
extern const cmd_option_t o_record[];
#endif

extern const char   colorNames[10][8];

void Qcommon_Init( int argc, char **argv );
void Qcommon_Frame( void );
void Qcommon_Shutdown( void );


