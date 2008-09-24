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

//
// q_msg.h
//

#define SZ_MSG_WRITE        MakeLong( 'w', 'r', 'i', 't' )
#define SZ_MSG_READ         MakeLong( 'r', 'e', 'a', 'd' )
#define SZ_NC_SEND_OLD      MakeLong( 'n', 'c', '1', 's' )
#define SZ_NC_SEND_NEW      MakeLong( 'n', 'c', '2', 's' )
#define SZ_NC_SEND_FRG      MakeLong( 'n', 'c', '2', 'f' )
#define SZ_NC_FRG_IN        MakeLong( 'n', 'c', '2', 'i' )
#define SZ_NC_FRG_OUT       MakeLong( 'n', 'c', '2', 'o' )

typedef struct sizebuf_s {
    uint32_t    tag;
	qboolean	allowoverflow;
	qboolean	overflowed;		// set to qtrue if the buffer size failed
	byte	*data;
	size_t	maxsize;
	size_t	cursize;
	size_t	readcount;
	size_t	bitpos;
} sizebuf_t;

void SZ_Init( sizebuf_t *buf, void *data, size_t length );
void SZ_TagInit( sizebuf_t *buf, void *data, size_t length, uint32_t tag );
void SZ_Clear( sizebuf_t *buf );
void *SZ_GetSpace( sizebuf_t *buf, size_t length );
void SZ_WriteByte( sizebuf_t *sb, int c );
void SZ_WriteShort( sizebuf_t *sb, int c );
void SZ_WriteLong( sizebuf_t *sb, int c );
void SZ_WriteString( sizebuf_t *sb, const char *string );

static inline void *SZ_Write( sizebuf_t *buf, const void *data, size_t length ) {
	return memcpy( SZ_GetSpace( buf, length ), data, length );		
}


//============================================================================

typedef enum {
	MSG_PS_IGNORE_GUNINDEX		= ( 1 << 0 ),
	MSG_PS_IGNORE_GUNFRAMES		= ( 1 << 1 ),
	MSG_PS_IGNORE_BLEND			= ( 1 << 2 ),
	MSG_PS_IGNORE_VIEWANGLES	= ( 1 << 3 ),
	MSG_PS_IGNORE_DELTAANGLES	= ( 1 << 4 ),
	MSG_PS_IGNORE_PREDICTION	= ( 1 << 5 ),	// mutually exclusive with IGNORE_VIEWANGLES
	MSG_PS_FORCE				= ( 1 << 7 ),
	MSG_PS_REMOVE				= ( 1 << 8 )
} msgPsFlags_t;

typedef enum {
	MSG_ES_FORCE				= ( 1 << 0 ),
	MSG_ES_NEWENTITY			= ( 1 << 1 ),
	MSG_ES_FIRSTPERSON			= ( 1 << 2 ),
	MSG_ES_LONGSOLID			= ( 1 << 3 ),
	MSG_ES_REMOVE	     		= ( 1 << 4 )
} msgEsFlags_t;
	
extern sizebuf_t	msg_write;
extern byte	        msg_write_buffer[MAX_MSGLEN];

extern sizebuf_t	msg_read;
extern byte	        msg_read_buffer[MAX_MSGLEN];

extern const entity_state_t	nullEntityState;
extern const player_state_t	nullPlayerState;
extern const usercmd_t		nullUserCmd;

void	MSG_Init( void );

void	MSG_BeginWriting( void );
void	MSG_WriteChar( int c );
void	MSG_WriteByte( int c );
void	MSG_WriteShort( int c );
void	MSG_WriteLong( int c );
void	MSG_WriteFloat( float f );
void	MSG_WriteString( const char *s );
void	MSG_WriteCoord( float f );
void	MSG_WritePos( const vec3_t pos );
void	MSG_WriteAngle( float f );
void	MSG_WriteAngle16( float f );
void	MSG_WriteBits( int value, int bits );
int		MSG_WriteDeltaUsercmd( const usercmd_t *from, const usercmd_t *cmd, int version );
int		MSG_WriteDeltaUsercmd_Enhanced( const usercmd_t *from, const usercmd_t *cmd, int version );
void	MSG_WriteDir ( const vec3_t vector);
void	MSG_WriteDeltaEntity( const entity_state_t *from, const entity_state_t *to, msgEsFlags_t flags );
void	MSG_WriteDeltaPlayerstate_Default( const player_state_t *from, const player_state_t *to );
int		MSG_WriteDeltaPlayerstate_Enhanced( const player_state_t *from, player_state_t *to, msgPsFlags_t flags );
void	MSG_WriteDeltaPlayerstate_Packet( const player_state_t *from, const player_state_t *to, int number, msgPsFlags_t flags );
void	MSG_FlushTo( sizebuf_t *dest );
void    MSG_Printf( const char *fmt, ... ) q_printf( 1, 2 ); 

static inline void *MSG_WriteData( const void *data, size_t length ) {
	return memcpy( SZ_GetSpace( &msg_write, length ), data, length );		
}

void	MSG_BeginReading( void );
int		MSG_ReadChar( void );
int		MSG_ReadByte( void );
int		MSG_ReadShort( void );
int     MSG_ReadWord( void );
int		MSG_ReadLong( void );
float	MSG_ReadFloat( void );
size_t  MSG_ReadString( char *dest, size_t size );
size_t  MSG_ReadStringLine( char *dest, size_t size );
float	MSG_ReadCoord( void );
void	MSG_ReadPos( vec3_t pos );
float	MSG_ReadAngle( void );
float	MSG_ReadAngle16 ( void );
int		MSG_ReadBits( int bits );
void	MSG_ReadDeltaUsercmd( const usercmd_t *from, usercmd_t *cmd );
void	MSG_ReadDeltaUsercmd_Hacked( const usercmd_t *from, usercmd_t *to );
void	MSG_ReadDeltaUsercmd_Enhanced( const usercmd_t *from, usercmd_t *to, int version );
void	MSG_ReadDir( vec3_t vector );
void	MSG_ReadData( void *buffer, int size );
int		MSG_ParseEntityBits( int *bits );
void	MSG_ParseDeltaEntity( const entity_state_t *from, entity_state_t *to, int number, int bits );
void	MSG_ParseDeltaPlayerstate_Default( const player_state_t *from, player_state_t *to, int flags );
void	MSG_ParseDeltaPlayerstate_Enhanced( const player_state_t *from, player_state_t *to, int flags, int extraflags );
void	MSG_ParseDeltaPlayerstate_Packet( const player_state_t *from, player_state_t *to, int flags );

void MSG_ShowDeltaEntityBits( int bits );
void MSG_ShowDeltaPlayerstateBits_Default( int flags );
void MSG_ShowDeltaPlayerstateBits_Enhanced( int flags );
void MSG_ShowDeltaPlayerstateBits_Packet( int flags );
void MSG_ShowDeltaUsercmdBits_Enhanced( int bits );
const char *MSG_ServerCommandString( int cmd );

#define MSG_ShowSVC( cmd ) do { \
	Com_Printf( "%3"PRIz":%s\n", msg_read.readcount - 1, \
        MSG_ServerCommandString( cmd ) ); \
    } while( 0 )


