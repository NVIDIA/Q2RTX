/*
Copyright (C) 2003-2006 Andrey Nazarov

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

#define MAX_LOCAL_SERVERS 16
#define MAX_DEMOINFO_CLIENTS	20
#define MAX_STATUS_PLAYERS	64

typedef struct {
	char name[MAX_CLIENT_NAME];
	int ping;
	int score;
} playerStatus_t;

typedef struct {
	char	address[MAX_QPATH];
	char	infostring[MAX_STRING_CHARS]; // BIG infostring
	playerStatus_t	players[MAX_STATUS_PLAYERS];
	int	numPlayers;
	int ping;
} serverStatus_t;

typedef struct {
	char map[MAX_QPATH];
    char pov[MAX_CLIENT_NAME];
} demoInfo_t;

typedef enum {
    ACT_MINIMIZED,
    ACT_RESTORED,
    ACT_ACTIVATED
} active_t;

void CL_ProcessEvents( void );
void CL_Init (void);
void CL_Disconnect( comErrorType_t type, const char *text );
void CL_Shutdown (void);
void CL_Frame (unsigned msec);
void CL_LocalConnect( void );
void CL_RestartFilesystem( void );
void CL_Activate( active_t active );
void CL_UpdateUserinfo( cvar_t *var, cvarSetSource_t source );
qboolean CL_SendStatusRequest( char *buffer, size_t size );
demoInfo_t *CL_GetDemoInfo( const char *path, demoInfo_t *info );

qboolean CL_ForwardToServer( void );
// adds the current command line as a clc_stringcmd to the client message.
// things like godmode, noclip, etc, are commands directed to the server,
// so when they are typed in at the console, they will need to be forwarded.

void Con_Init( void );
void Con_Print( const char *text );
void Con_Printf( const char *fmt, ... );
void Con_Close( void );

// this is in the client code, but can be used for debugging from server
void SCR_DebugGraph (float value, int color);
void SCR_BeginLoadingPlaque (void);
void SCR_EndLoadingPlaque( void );
void SCR_ModeChanged( void );
void SCR_UpdateScreen( void );

void IN_Frame( void );
void IN_Activate( void );
void IN_MouseEvent( int x, int y );
void IN_WarpMouse( int x, int y );

void	Key_Init( void );
void	Key_Event( unsigned key, qboolean down, unsigned time );
void	Key_CharEvent( int key );
void	Key_WriteBindings( fileHandle_t f );

char	*VID_GetClipboardData( void );
void	VID_SetClipboardData( const char *data );
void    VID_FatalShutdown( void );

