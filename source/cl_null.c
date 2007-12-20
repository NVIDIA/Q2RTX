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

// cl_null.c -- this file can stub out the entire client system
// for pure dedicated servers

#include "com_local.h"

cvar_t *cl_paused;

void Key_Bind_Null_f( void ) {
}

void CL_Init( void ) {
	cl_paused = Cvar_Get( "cl_paused", "0", CVAR_ROM );
}

void CL_Drop( void ) {
}

void CL_Disconnect( comErrorType_t type, const char *text ) {
}

void CL_Shutdown( void ) {
}

void CL_Frame( int msec ) {
    if( cmd_buffer.waitCount > 0 ) {
        cmd_buffer.waitCount--;
    }
}

void Con_Init( void ) {
}

void Con_Print( const char *text ) {
}

qboolean CL_CheatsOK( void ) {
	return qtrue;
}

void CL_PacketEvent( neterr_t ret ) {
}

void CL_UpdateUserinfo( cvar_t *var, cvarSetSource_t source ) {
}

void Cmd_ForwardToServer( void ) {
	char *cmd;

	cmd = Cmd_Argv( 0 );
	Com_Printf( "Unknown command \"%s\"\n", cmd );
}

void SCR_DebugGraph( float value, int color ) {
}

void SCR_BeginLoadingPlaque( void ) {
}

void SCR_EndLoadingPlaque( void ) {
}

void CL_RestartFilesystem( void ) {
	FS_Restart();
}

void CL_LocalConnect( void ) {
	if( FS_NeedRestart() ) {
		FS_Restart();
	}
}

void CL_PumpEvents( void ) {
}

void CL_AppActivate( qboolean active ) {
}

void Key_WriteBindings( fileHandle_t f ) {
}

void Key_Event( unsigned key, qboolean down, unsigned time ) {
}

void Key_CharEvent( int key ) {
}

void CL_MouseEvent( int dx, int dy ) {
}

void CL_InputFrame( void ) {
}

void Key_Init( void ) {
	Cmd_AddCommand( "bind", Key_Bind_Null_f );
	Cmd_AddCommand( "unbind", Key_Bind_Null_f );
	Cmd_AddCommand( "unbindall", Key_Bind_Null_f );
}

