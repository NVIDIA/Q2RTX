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
#define MAX_STATUS_PLAYERS  64

typedef struct {
    char name[MAX_CLIENT_NAME];
    int ping;
    int score;
} playerStatus_t;

typedef struct {
    char    address[MAX_QPATH];
    char    infostring[MAX_STRING_CHARS]; // BIG infostring
    playerStatus_t  players[MAX_STATUS_PLAYERS];
    int numPlayers;
    int ping;
} serverStatus_t;

typedef struct {
    char map[MAX_QPATH];
    char pov[MAX_CLIENT_NAME];
    qboolean mvd;
} demoInfo_t;

typedef enum {
    ACT_MINIMIZED,
    ACT_RESTORED,
    ACT_ACTIVATED
} active_t;

void CL_ProcessEvents( void );
#if USE_ICMP
void CL_ErrorEvent( void );
#endif
void CL_Init (void);
void CL_Disconnect( error_type_t type );
void CL_Shutdown (void);
unsigned CL_Frame (unsigned msec);
void CL_RestartFilesystem( qboolean total );
void CL_Activate( active_t active );
void CL_UpdateUserinfo( cvar_t *var, from_t from );
qboolean CL_SendStatusRequest( char *buffer, size_t size );
demoInfo_t *CL_GetDemoInfo( const char *path, demoInfo_t *info );
qboolean CL_CheatsOK( void );

qboolean CL_ForwardToServer( void );
// adds the current command line as a clc_stringcmd to the client message.
// things like godmode, noclip, etc, are commands directed to the server,
// so when they are typed in at the console, they will need to be forwarded.

void Con_Init( void );
void Con_SetColor( color_index_t color );
void Con_Print( const char *text );
void Con_Printf( const char *fmt, ... );
void Con_Close( qboolean force );

// this is in the client code, but can be used for debugging from server
void SCR_DebugGraph (float value, int color);
void SCR_BeginLoadingPlaque (void);
void SCR_EndLoadingPlaque( void );
void SCR_ModeChanged( void );
void SCR_UpdateScreen( void );

#define U32_BLACK   MakeColor(   0,   0,   0, 255 )
#define U32_RED     MakeColor( 255,   0,   0, 255 )
#define U32_GREEN   MakeColor(   0, 255,   0, 255 )
#define U32_YELLOW  MakeColor( 255, 255,   0, 255 )
#define U32_BLUE    MakeColor(   0,   0, 255, 255 )
#define U32_CYAN    MakeColor(   0, 255, 255, 255 )
#define U32_MAGENTA MakeColor( 255,   0, 255, 255 )
#define U32_WHITE   MakeColor( 255, 255, 255, 255 )

#define CHAR_WIDTH  8
#define CHAR_HEIGHT 8

#define UI_LEFT             0x00000001
#define UI_RIGHT            0x00000002
#define UI_CENTER           (UI_LEFT|UI_RIGHT)
#define UI_BOTTOM           0x00000004
#define UI_TOP              0x00000008
#define UI_MIDDLE           (UI_BOTTOM|UI_TOP)
#define UI_DROPSHADOW       0x00000010
#define UI_ALTCOLOR         0x00000020
#define UI_IGNORECOLOR      0x00000040
#define UI_ALTESCAPES       0x00000080
#define UI_AUTOWRAP         0x00000100
#define UI_MULTILINE        0x00000200
#define UI_DRAWCURSOR       0x00000400

extern const uint32_t   colorTable[8];

qboolean SCR_ParseColor( const char *s, color_t *color );

float V_CalcFov( float fov_x, float width, float height );

void IN_Frame( void );
void IN_Activate( void );
void IN_MouseEvent( int x, int y );
void IN_WarpMouse( int x, int y );

void    Key_Init( void );
void    Key_Event( unsigned key, qboolean down, unsigned time );
void    Key_CharEvent( int key );
void    Key_WriteBindings( qhandle_t f );

char    *VID_GetClipboardData( void );
void    VID_SetClipboardData( const char *data );
void    VID_FatalShutdown( void );

