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

#include "config.h"
#include "q_shared.h"
#include "com_public.h"
#include "protocol.h"
#include "q_msg.h"

sizebuf_t	msg_write;
byte		msg_write_buffer[MAX_MSGLEN];

sizebuf_t	msg_read;
byte		msg_read_buffer[MAX_MSGLEN];

/*
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

static const entity_state_t	nullEntityState;
static const player_state_t	nullPlayerState;
static const usercmd_t		nullUserCmd;


/*
=============
MSG_Init
=============
*/
void MSG_Init( void ) {
	// initialize default buffers
	SZ_Init( &msg_read, msg_read_buffer, sizeof( msg_read_buffer ) );
	SZ_Init( &msg_write, msg_write_buffer, sizeof( msg_write_buffer ) );

	// don't allow them to overflow
	msg_read.allowoverflow = qfalse;
	msg_write.allowoverflow = qfalse;

}


//
// writing functions
//

/*
=============
MSG_BeginWriting
=============
*/
void MSG_BeginWriting( void ) {
	msg_write.cursize = 0;
	msg_write.bitpos = 0;
	msg_write.overflowed = qfalse;
}

/*
=============
MSG_WriteChar
=============
*/
void MSG_WriteChar( int c ) {
	byte	*buf;
	
#ifdef PARANOID
	if( c < -128 || c > 127 )
		Com_Error( ERR_FATAL, "MSG_WriteChar: range error" );
#endif

	buf = SZ_GetSpace( &msg_write, 1 );
	buf[0] = c;
}

/*
=============
MSG_WriteByte
=============
*/
void MSG_WriteByte( int c ) {
	byte	*buf;
	
#ifdef PARANOID
	if( c < 0 || c > 255 )
		Com_Error( ERR_FATAL, "MSG_WriteByte: range error" );
#endif

	buf = SZ_GetSpace( &msg_write, 1 );
	buf[0] = c;
}

/*
=============
MSG_WriteShort
=============
*/
void MSG_WriteShort( int c ) {
	byte	*buf;
	
#ifdef PARANOID
	if (c < ((short)0x8000) || c > (short)0x7fff)
		Com_Error (ERR_FATAL, "MSG_WriteShort: range error");
#endif

	buf = SZ_GetSpace( &msg_write, 2 );
	buf[0] = c & 0xff;
	buf[1] = c >> 8;
}

/*
=============
MSG_WriteLong
=============
*/
void MSG_WriteLong( int c ) {
	byte	*buf;
	
	buf = SZ_GetSpace( &msg_write, 4 );
	buf[0] = c & 0xff;
	buf[1] = ( c >> 8 ) & 0xff;
	buf[2] = ( c >> 16 ) & 0xff;
	buf[3] = c >> 24;
}

/*
=============
MSG_WriteString
=============
*/
void MSG_WriteString( const char *string ) {
	int length;
	int c;

	if( !string ) {
		MSG_WriteByte( 0 );
		return;
	}

	length = strlen( string );
	if( length > MAX_NET_STRING - 1 ) {
		Com_WPrintf( "MSG_WriteString: overflow: %d chars", length );
		MSG_WriteByte( 0 );
		return;
	}

	while( *string ) {
		c = *string++;
		if( c == '\xFF' ) {
			c = '.';
		}
		MSG_WriteByte( c );
	}

	MSG_WriteByte( 0 );
}

/*
=============
MSG_WriteCoord
=============
*/
void MSG_WriteCoord( float f ) {
	MSG_WriteShort( ( int )( f * 8 ) );
}

/*
=============
MSG_WritePos
=============
*/
void MSG_WritePos( const vec3_t pos ) {
	MSG_WriteShort( ( int )( pos[0] * 8 ) );
	MSG_WriteShort( ( int )( pos[1] * 8 ) );
	MSG_WriteShort( ( int )( pos[2] * 8 ) );
}

/*
=============
MSG_WriteAngle
=============
*/
void MSG_WriteAngle( float f ) {
	MSG_WriteByte( ( int )( f * 256 / 360 ) & 255 );
}

/*
=============
MSG_WriteAngle16
=============
*/
void MSG_WriteAngle16( float f ) {
	MSG_WriteShort( ANGLE2SHORT( f ) );
}

/*
=============
MSG_WriteDeltaUsercmd
=============
*/
int MSG_WriteDeltaUsercmd( const usercmd_t *from, const usercmd_t *cmd ) {
	int		bits;

	if( !from ) {
		from = &nullUserCmd;
	}

//
// send the movement message
//
	bits = 0;
	if( cmd->angles[0] != from->angles[0] )
		bits |= CM_ANGLE1;
	if( cmd->angles[1] != from->angles[1] )
		bits |= CM_ANGLE2;
	if( cmd->angles[2] != from->angles[2] )
		bits |= CM_ANGLE3;
	if( cmd->forwardmove != from->forwardmove )
		bits |= CM_FORWARD;
	if( cmd->sidemove != from->sidemove )
		bits |= CM_SIDE;
	if( cmd->upmove != from->upmove )
		bits |= CM_UP;
	if( cmd->buttons != from->buttons )
		bits |= CM_BUTTONS;
	if( cmd->impulse != from->impulse )
		bits |= CM_IMPULSE;

    MSG_WriteByte ( bits );

	if( bits & CM_ANGLE1 )
		MSG_WriteShort( cmd->angles[0] );
	if( bits & CM_ANGLE2 )
		MSG_WriteShort( cmd->angles[1] );
	if( bits & CM_ANGLE3 )
		MSG_WriteShort( cmd->angles[2] );
	
	if( bits & CM_FORWARD )
		MSG_WriteShort( cmd->forwardmove );
	if( bits & CM_SIDE )
	  	MSG_WriteShort( cmd->sidemove );
	if( bits & CM_UP )
		MSG_WriteShort( cmd->upmove );

 	if( bits & CM_BUTTONS )
	  	MSG_WriteByte( cmd->buttons );
 	if( bits & CM_IMPULSE )
	    MSG_WriteByte( cmd->impulse );

    MSG_WriteByte( cmd->msec );

	return bits;
}

/*
=============
MSG_WriteBits
=============
*/
void MSG_WriteBits( int value, int bits ) {
	int i, bitpos;

	if( bits == 0 || bits < -31 || bits > 32 ) {
		Com_Error( ERR_FATAL, "MSG_WriteBits: bad bits: %d", bits );
	}

	if( msg_write.maxsize - msg_write.cursize < 4 ) {
		Com_Error( ERR_FATAL, "MSG_WriteBits: overflow" );
	}

	if( bits < 0 ) {
		bits = -bits;
	}

	bitpos = msg_write.bitpos;
	if( ( bitpos & 7 ) == 0 ) {
		// optimized case
		switch( bits ) {
		case 8:
			MSG_WriteByte( value );
			return;
		case 16:
			MSG_WriteShort( value );
			return;
		case 32:
			MSG_WriteLong( value );
			return;
		default:
			break;
		}
	}
	for( i = 0; i < bits; i++, bitpos++ ) {
		if( ( bitpos & 7 ) == 0 ) {
			msg_write.data[ bitpos >> 3 ] = 0;
		}
		msg_write.data[ bitpos >> 3 ] |= ( value & 1 ) << ( bitpos & 7 );
		value >>= 1;
	}
	msg_write.bitpos = bitpos;
	msg_write.cursize = ( bitpos + 7 ) >> 3;
}

/*
=============
MSG_WriteDeltaUsercmd_Enhanced
=============
*/
int MSG_WriteDeltaUsercmd_Enhanced( const usercmd_t *from, const usercmd_t *cmd ) {
	int		bits, delta;

	if( !from ) {
		from = &nullUserCmd;
	}

//
// send the movement message
//
	bits = 0;
	if( cmd->angles[0] != from->angles[0] )
		bits |= CM_ANGLE1;
	if( cmd->angles[1] != from->angles[1] )
		bits |= CM_ANGLE2;
	if( cmd->angles[2] != from->angles[2] )
		bits |= CM_ANGLE3;
	if( cmd->forwardmove != from->forwardmove )
		bits |= CM_FORWARD;
	if( cmd->sidemove != from->sidemove )
		bits |= CM_SIDE;
	if( cmd->upmove != from->upmove )
		bits |= CM_UP;
	if( cmd->buttons != from->buttons )
		bits |= CM_BUTTONS;
	if( cmd->msec != from->msec )
		bits |= CM_IMPULSE;

	if( !bits ) {
		MSG_WriteBits( 0, 1 );
		return 0;
	}

	MSG_WriteBits( 1, 1 );
    MSG_WriteBits( bits, 8 );

	if( bits & CM_ANGLE1 ) {
		delta = cmd->angles[0] - from->angles[0];
		if( delta >= -128 && delta <= 127 ) {
			MSG_WriteBits( 1, 1 );
			MSG_WriteBits( delta, -8 );
		} else {
			MSG_WriteBits( 0, 1 );
			MSG_WriteBits( cmd->angles[0], -16 );
		}
	}
	if( bits & CM_ANGLE2 ) {
		delta = cmd->angles[1] - from->angles[1];
		if( delta >= -128 && delta <= 127 ) {
			MSG_WriteBits( 1, 1 );
			MSG_WriteBits( delta, -8 );
		} else {
			MSG_WriteBits( 0, 1 );
			MSG_WriteBits( cmd->angles[1], -16 );
		}
	}
	if( bits & CM_ANGLE3 ) {
		MSG_WriteBits( cmd->angles[2], -16 );
	}
	
	if( bits & CM_FORWARD ) {
		MSG_WriteBits( cmd->forwardmove, -16 );
	}
	if( bits & CM_SIDE ) {
		MSG_WriteBits( cmd->sidemove, -16 );
	}
	if( bits & CM_UP ) {
		MSG_WriteBits( cmd->upmove, -16 );
	}

	if( bits & CM_BUTTONS ) {
        int buttons = ( cmd->buttons & 3 ) | ( cmd->buttons >> 5 );
		MSG_WriteBits( buttons, 3 );
	}
	if( bits & CM_IMPULSE ) {
		MSG_WriteBits( cmd->msec, 8 );
	}

	return bits;
}

void MSG_WriteDir( const vec3_t dir ) {
	int		best;
	
	best = DirToByte( dir );
	MSG_WriteByte( best );
}

void MSG_WriteData( const void *data, int length ) {
	memcpy( SZ_GetSpace( &msg_write, length ), data, length );		
}

/* values transmitted over network are discrete, so
 * we use special macros to check for delta conditions
 */
#define Delta_Angle( a, b ) \
  ( ((int)((a)*256/360) & 255) != ((int)((b)*256/360) & 255) )

#define Delta_Coord( a, b ) \
  ( (int)((b)*8) != (int)((a)*8) )

#define Delta_Pos( a, b ) \
  ( (int)((b)[0]*8) != (int)((a)[0]*8) || \
    (int)((b)[1]*8) != (int)((a)[1]*8) || \
    (int)((b)[2]*8) != (int)((a)[2]*8) )

#define Delta_VecChar( a, b ) \
  ( (int)((b)[0]*4) != (int)((a)[0]*4) || \
	(int)((b)[1]*4) != (int)((a)[1]*4) || \
	(int)((b)[2]*4) != (int)((a)[2]*4) )

#define Delta_Blend( a, b ) \
  ( (int)((b)[0]*255) != (int)((a)[0]*255) || \
    (int)((b)[1]*255) != (int)((a)[1]*255) || \
    (int)((b)[2]*255) != (int)((a)[2]*255) || \
    (int)((b)[3]*255) != (int)((a)[3]*255) )

#define Delta_Angle16( a, b ) \
	( ANGLE2SHORT(b) != ANGLE2SHORT(a) )

#define Delta_VecAngle16( a, b ) \
  ( ANGLE2SHORT((b)[0]) != ANGLE2SHORT((a)[0]) || \
    ANGLE2SHORT((b)[1]) != ANGLE2SHORT((a)[1]) || \
    ANGLE2SHORT((b)[2]) != ANGLE2SHORT((a)[2]) )

#define Delta_Fov( a, b ) \
	( (int)(b) != (int)(a) )

/*
==================
MSG_WriteDeltaEntity

Writes part of a packetentities message.
Can delta from either a baseline or a previous packet_entity
==================
*/
void MSG_WriteDeltaEntity( const entity_state_t *from,
                           const entity_state_t *to,
                           msgEsFlags_t         flags )
{
	int		bits;

	if( !to ) {
	    if( !from ) {
		    Com_Error( ERR_DROP, "%s: NULL", __func__ );
	    }
    	if( from->number < 1 || from->number >= MAX_EDICTS ) {
	    	Com_Error( ERR_DROP, "%s: bad number: %d", __func__, from->number );
	    }
		bits = U_REMOVE;
		if( from->number >= 256 ) {
			bits |= U_NUMBER16 | U_MOREBITS1;
		}

		MSG_WriteByte( bits & 255 );
		if( bits & 0x0000ff00 )
			MSG_WriteByte( ( bits >> 8 ) & 255 );

		if( bits & U_NUMBER16 )
			MSG_WriteShort( from->number );
		else
			MSG_WriteByte( from->number );

		return; // remove entity
	}

	if( to->number < 1 || to->number >= MAX_EDICTS ) {
		Com_Error( ERR_DROP, "%s: bad number: %d", __func__, to->number );
	}

	if( !from ) {
		from = &nullEntityState;
	}

// send an update
	bits = 0;

	if( !( flags & MSG_ES_FIRSTPERSON ) ) {
		if( Delta_Coord( to->origin[0], from->origin[0] ) )
			bits |= U_ORIGIN1;
		if( Delta_Coord( to->origin[1], from->origin[1] ) )
			bits |= U_ORIGIN2;
		if( Delta_Coord( to->origin[2], from->origin[2] ) )
			bits |= U_ORIGIN3;

		if( Delta_Angle( to->angles[0], from->angles[0] ) )
			bits |= U_ANGLE1;		
		if( Delta_Angle( to->angles[1], from->angles[1] ) )
			bits |= U_ANGLE2;
		if( Delta_Angle( to->angles[2], from->angles[2] ) )
			bits |= U_ANGLE3;

        if( flags & MSG_ES_NEWENTITY ) {
            if( Delta_Pos( to->old_origin, from->old_origin ) ) {
                bits |= U_OLDORIGIN;
            }
        }
	}
		
	if( to->skinnum != from->skinnum ) {
        if( to->skinnum & 0xffff0000 ) {
			bits |= U_SKIN8|U_SKIN16;
        } else if( to->skinnum & 0x0000ff00 ) {
			bits |= U_SKIN16;
        } else {
			bits |= U_SKIN8;
        }
	}
		
	if( to->frame != from->frame ) {
		if( to->frame < 256 )
			bits |= U_FRAME8;
		else
			bits |= U_FRAME16;
	}

	if( to->effects != from->effects ) {
        if( to->effects & 0xffff0000 ) {
			bits |= U_EFFECTS8|U_EFFECTS16;
        } else if( to->effects & 0x0000ff00 ) {
			bits |= U_EFFECTS16;
        } else {
			bits |= U_EFFECTS8;
        }
	}
	
	if ( to->renderfx != from->renderfx ) {
        if( to->renderfx & 0xffff0000 ) {
			bits |= U_RENDERFX8|U_RENDERFX16;
        } else if( to->renderfx & 0x0000ff00 ) {
			bits |= U_RENDERFX16;
        } else {
			bits |= U_RENDERFX8;
        }
	}
	
	if ( to->solid != from->solid )
		bits |= U_SOLID;

	// event is not delta compressed, just 0 compressed
	if ( to->event  )
		bits |= U_EVENT;
	
	if ( to->modelindex != from->modelindex )
		bits |= U_MODEL;
	if ( to->modelindex2 != from->modelindex2 )
		bits |= U_MODEL2;
	if ( to->modelindex3 != from->modelindex3 )
		bits |= U_MODEL3;
	if ( to->modelindex4 != from->modelindex4 )
		bits |= U_MODEL4;

	if ( to->sound != from->sound )
		bits |= U_SOUND;

	if( ( to->renderfx & RF_BEAM ) )
		bits |= U_OLDORIGIN;

	//
	// write the message
	//
	if( !bits && !( flags & MSG_ES_FORCE ) )
		return;		// nothing to send!

	//----------

	if (to->number >= 256)
		bits |= U_NUMBER16;		// number8 is implicit otherwise

	if (bits & 0xff000000)
		bits |= U_MOREBITS3 | U_MOREBITS2 | U_MOREBITS1;
	else if (bits & 0x00ff0000)
		bits |= U_MOREBITS2 | U_MOREBITS1;
	else if (bits & 0x0000ff00)
		bits |= U_MOREBITS1;

	MSG_WriteByte (bits&255 );

	if (bits & 0xff000000) {
		MSG_WriteByte ((bits>>8)&255 );
		MSG_WriteByte ((bits>>16)&255 );
		MSG_WriteByte ((bits>>24)&255 );
	}
	else if (bits & 0x00ff0000) {
		MSG_WriteByte ((bits>>8)&255 );
		MSG_WriteByte ((bits>>16)&255 );
	}
	else if (bits & 0x0000ff00) {
		MSG_WriteByte ((bits>>8)&255 );
	}

	//----------

	if (bits & U_NUMBER16)
		MSG_WriteShort (to->number);
	else
		MSG_WriteByte (to->number);

	if (bits & U_MODEL)
		MSG_WriteByte (to->modelindex);
	if (bits & U_MODEL2)
		MSG_WriteByte (to->modelindex2);
	if (bits & U_MODEL3)
		MSG_WriteByte (to->modelindex3);
	if (bits & U_MODEL4)
		MSG_WriteByte (to->modelindex4);

	if (bits & U_FRAME8)
		MSG_WriteByte (to->frame);
	if (bits & U_FRAME16)
		MSG_WriteShort (to->frame);

	if ((bits & (U_SKIN8|U_SKIN16)) == (U_SKIN8|U_SKIN16) )		//used for laser colors
		MSG_WriteLong (to->skinnum);
	else if (bits & U_SKIN8)
		MSG_WriteByte (to->skinnum);
	else if (bits & U_SKIN16)
		MSG_WriteShort (to->skinnum);


	if ( (bits & (U_EFFECTS8|U_EFFECTS16)) == (U_EFFECTS8|U_EFFECTS16) )
		MSG_WriteLong (to->effects);
	else if (bits & U_EFFECTS8)
		MSG_WriteByte (to->effects);
	else if (bits & U_EFFECTS16)
		MSG_WriteShort (to->effects);

	if ( (bits & (U_RENDERFX8|U_RENDERFX16)) == (U_RENDERFX8|U_RENDERFX16) )
		MSG_WriteLong (to->renderfx);
	else if (bits & U_RENDERFX8)
		MSG_WriteByte (to->renderfx);
	else if (bits & U_RENDERFX16)
		MSG_WriteShort (to->renderfx);

	if (bits & U_ORIGIN1)
		MSG_WriteCoord (to->origin[0]);		
	if (bits & U_ORIGIN2)
		MSG_WriteCoord (to->origin[1]);
	if (bits & U_ORIGIN3)
		MSG_WriteCoord (to->origin[2]);

	if (bits & U_ANGLE1)
		MSG_WriteAngle(to->angles[0]);
	if (bits & U_ANGLE2){
		MSG_WriteAngle(to->angles[1]);
	}
	if (bits & U_ANGLE3)
		MSG_WriteAngle(to->angles[2]);

	if (bits & U_OLDORIGIN) {
		MSG_WriteCoord (to->old_origin[0]);
		MSG_WriteCoord (to->old_origin[1]);
		MSG_WriteCoord (to->old_origin[2]);
	}

	if (bits & U_SOUND)
		MSG_WriteByte (to->sound);
	if (bits & U_EVENT)
		MSG_WriteByte (to->event);
	if (bits & U_SOLID)
		MSG_WriteShort (to->solid);
}

/*
==================
MSG_WriteDeltaPlayerstate_Default
==================
*/
void MSG_WriteDeltaPlayerstate_Default( const player_state_t *from, const player_state_t *to ) {
	int				i;
	int				pflags;
	int				statbits;

	if( !to ) {
		Com_Error( ERR_DROP, "%s: NULL", __func__ );
	}

	if( !from ) {
		from = &nullPlayerState;
	}

	//
	// determine what needs to be sent
	//
	pflags = 0;

	if( to->pmove.pm_type != from->pmove.pm_type )
		pflags |= PS_M_TYPE;

	if( to->pmove.origin[0] != from->pmove.origin[0] ||
		to->pmove.origin[1] != from->pmove.origin[1] ||
		to->pmove.origin[2] != from->pmove.origin[2] )
	{
		pflags |= PS_M_ORIGIN;
	}

	if( to->pmove.velocity[0] != from->pmove.velocity[0] ||
		to->pmove.velocity[1] != from->pmove.velocity[1] ||
		to->pmove.velocity[2] != from->pmove.velocity[2] )
	{
		pflags |= PS_M_VELOCITY;
	}

	if( to->pmove.pm_time != from->pmove.pm_time )
		pflags |= PS_M_TIME;

	if( to->pmove.pm_flags != from->pmove.pm_flags )
		pflags |= PS_M_FLAGS;

	if( to->pmove.gravity != from->pmove.gravity )
		pflags |= PS_M_GRAVITY;

	if( to->pmove.delta_angles[0] != from->pmove.delta_angles[0] ||
		to->pmove.delta_angles[1] != from->pmove.delta_angles[1] ||
		to->pmove.delta_angles[2] != from->pmove.delta_angles[2] )
	{
		pflags |= PS_M_DELTA_ANGLES;
	}

	if( Delta_VecChar( to->viewoffset, from->viewoffset ) ) {
		pflags |= PS_VIEWOFFSET;
	}

	if( Delta_VecAngle16( to->viewangles, from->viewangles ) ) {
		pflags |= PS_VIEWANGLES;
	}

	if( Delta_VecChar( to->kick_angles, from->kick_angles ) ) {
		pflags |= PS_KICKANGLES;
	}

	if( Delta_Blend( to->blend, from->blend ) ) {
		pflags |= PS_BLEND;
	}

	if( Delta_Fov( to->fov, from->fov ) )
		pflags |= PS_FOV;

	if( to->rdflags != from->rdflags )
		pflags |= PS_RDFLAGS;

	if( to->gunframe != from->gunframe ||
		Delta_VecChar( to->gunoffset, from->gunoffset ) ||
		Delta_VecChar( to->gunangles, from->gunangles ) )
	{
		pflags |= PS_WEAPONFRAME;
	}

	if( to->gunindex != from->gunindex )
		pflags |= PS_WEAPONINDEX;


	//
	// write it
	//
	MSG_WriteShort( pflags );

	//
	// write the pmove_state_t
	//
	if( pflags & PS_M_TYPE )
		MSG_WriteByte( to->pmove.pm_type );

	if( pflags & PS_M_ORIGIN ) {
		MSG_WriteShort( to->pmove.origin[0] );
		MSG_WriteShort( to->pmove.origin[1] );
		MSG_WriteShort( to->pmove.origin[2] );
	}

	if( pflags & PS_M_VELOCITY ) {
		MSG_WriteShort( to->pmove.velocity[0] );
		MSG_WriteShort( to->pmove.velocity[1] );
		MSG_WriteShort( to->pmove.velocity[2] );
	}

	if( pflags & PS_M_TIME )
		MSG_WriteByte( to->pmove.pm_time );

	if( pflags & PS_M_FLAGS )
		MSG_WriteByte( to->pmove.pm_flags );

	if( pflags & PS_M_GRAVITY )
		MSG_WriteShort( to->pmove.gravity );

	if( pflags & PS_M_DELTA_ANGLES ) {
		MSG_WriteShort( to->pmove.delta_angles[0] );
		MSG_WriteShort( to->pmove.delta_angles[1] );
		MSG_WriteShort( to->pmove.delta_angles[2] );
	}

	//
	// write the rest of the player_state_t
	//
	if( pflags & PS_VIEWOFFSET ) {
		MSG_WriteChar( to->viewoffset[0] * 4 );
		MSG_WriteChar( to->viewoffset[1] * 4 );
		MSG_WriteChar( to->viewoffset[2] * 4 );
	}

	if( pflags & PS_VIEWANGLES ) {
		MSG_WriteAngle16( to->viewangles[0] );
		MSG_WriteAngle16( to->viewangles[1] );
		MSG_WriteAngle16( to->viewangles[2] );
	}

	if( pflags & PS_KICKANGLES ) {
		MSG_WriteChar( to->kick_angles[0] * 4 );
		MSG_WriteChar( to->kick_angles[1] * 4 );
		MSG_WriteChar( to->kick_angles[2] * 4 );
	}

	if( pflags & PS_WEAPONINDEX ) {
		MSG_WriteByte( to->gunindex );
	}

	if( pflags & PS_WEAPONFRAME ) {
		MSG_WriteByte( to->gunframe );
		MSG_WriteChar( to->gunoffset[0] * 4 );
		MSG_WriteChar( to->gunoffset[1] * 4 );
		MSG_WriteChar( to->gunoffset[2] * 4 );
		MSG_WriteChar( to->gunangles[0] * 4 );
		MSG_WriteChar( to->gunangles[1] * 4 );
		MSG_WriteChar( to->gunangles[2] * 4 );
	}

	if( pflags & PS_BLEND ) {
		MSG_WriteByte( to->blend[0] * 255 );
		MSG_WriteByte( to->blend[1] * 255 );
		MSG_WriteByte( to->blend[2] * 255 );
		MSG_WriteByte( to->blend[3] * 255 );
	}

	if( pflags & PS_FOV )
		MSG_WriteByte( to->fov );

	if( pflags & PS_RDFLAGS )
		MSG_WriteByte( to->rdflags );

	// send stats
	statbits = 0;
	for( i = 0; i < MAX_STATS; i++ )
		if( to->stats[i] != from->stats[i] )
			statbits |= 1 << i;

	MSG_WriteLong( statbits );
	for( i = 0; i < MAX_STATS; i++ )
		if( statbits & ( 1 << i ) )
			MSG_WriteShort( to->stats[i] );
}

/*
==================
MSG_WriteDeltaPlayerstate_Enhanced
==================
*/
int MSG_WriteDeltaPlayerstate_Enhanced( const player_state_t    *from,
                                              player_state_t    *to,
                                              msgPsFlags_t      flags )
{
	int				i;
	int				pflags, extraflags;
	int				statbits;

	if( !to ) {
		Com_Error( ERR_DROP, "%s: NULL", __func__ );
	}

	if( !from ) {
		from = &nullPlayerState;
	}

	//
	// determine what needs to be sent
	//
	pflags = 0;
	extraflags = 0;

	if( to->pmove.pm_type != from->pmove.pm_type )
		pflags |= PS_M_TYPE;

	if( to->pmove.origin[0] != from->pmove.origin[0] ||
		to->pmove.origin[1] != from->pmove.origin[1] )
	{
		pflags |= PS_M_ORIGIN;
	}

	if( to->pmove.origin[2] != from->pmove.origin[2] ) {
		extraflags |= EPS_M_ORIGIN2;
	}

	if( !( flags & MSG_PS_IGNORE_PREDICTION ) ) {
		if( to->pmove.velocity[0] != from->pmove.velocity[0] ||
			to->pmove.velocity[1] != from->pmove.velocity[1] )
		{
			pflags |= PS_M_VELOCITY;
		}

		if( to->pmove.velocity[2] != from->pmove.velocity[2] ) {
			extraflags |= EPS_M_VELOCITY2;
		}

		if( to->pmove.pm_time != from->pmove.pm_time )
			pflags |= PS_M_TIME;

		if( to->pmove.pm_flags != from->pmove.pm_flags )
			pflags |= PS_M_FLAGS;

		if( to->pmove.gravity != from->pmove.gravity )
			pflags |= PS_M_GRAVITY;
	} else {
		// save previous state
		VectorCopy( from->pmove.velocity, to->pmove.velocity );
		to->pmove.pm_time = from->pmove.pm_time;
		to->pmove.pm_flags = from->pmove.pm_flags;
		to->pmove.gravity = from->pmove.gravity;
	}

	if( !( flags & MSG_PS_IGNORE_DELTAANGLES ) ) {
		if( to->pmove.delta_angles[0] != from->pmove.delta_angles[0] ||
			to->pmove.delta_angles[1] != from->pmove.delta_angles[1] ||
			to->pmove.delta_angles[2] != from->pmove.delta_angles[2] )
		{
			pflags |= PS_M_DELTA_ANGLES;
		}
	} else {
		// save previous state
		VectorCopy( from->pmove.delta_angles, to->pmove.delta_angles );
	}

	if( Delta_VecChar( from->viewoffset, to->viewoffset ) ) {
		pflags |= PS_VIEWOFFSET;
	}

	if( !( flags & MSG_PS_IGNORE_VIEWANGLES ) ) {
		if( Delta_Angle16( from->viewangles[0], to->viewangles[0] ) ||
			Delta_Angle16( from->viewangles[1], to->viewangles[1] ) )
		{
			pflags |= PS_VIEWANGLES;
		}

		if( Delta_Angle16( from->viewangles[2], to->viewangles[2] ) ) {
			extraflags |= EPS_VIEWANGLE2;
		}
	} else {
		// save previous state
		to->viewangles[0] = from->viewangles[0];
		to->viewangles[1] = from->viewangles[1];
		to->viewangles[2] = from->viewangles[2];
	}

	if( Delta_VecChar( from->kick_angles, to->kick_angles ) ) {
		pflags |= PS_KICKANGLES;
	}

	if( !( flags & MSG_PS_IGNORE_BLEND ) ) {
		if( Delta_Blend( from->blend, to->blend ) ) {
			pflags |= PS_BLEND;
		}
	} else {
		// save previous state
		to->blend[0] = from->blend[0];
		to->blend[1] = from->blend[1];
		to->blend[2] = from->blend[2];
		to->blend[3] = from->blend[3];
	}

	if( Delta_Fov( from->fov, to->fov ) )
		pflags |= PS_FOV;

	if( to->rdflags != from->rdflags )
		pflags |= PS_RDFLAGS;

	if( !( flags & MSG_PS_IGNORE_GUNINDEX ) ) {
		if( to->gunindex != from->gunindex )
			pflags |= PS_WEAPONINDEX;
	} else {
		// save previous state
		to->gunindex = from->gunindex;
	}

	if( !( flags & MSG_PS_IGNORE_GUNFRAMES ) ) {
		if( to->gunframe != from->gunframe )
			pflags |= PS_WEAPONFRAME;

		if( Delta_VecChar( from->gunoffset, to->gunoffset ) ) {
			extraflags |= EPS_GUNOFFSET;
		}

		if( Delta_VecChar( from->gunangles, to->gunangles ) ) {
			extraflags |= EPS_GUNANGLES;
		}
	} else {
		// save previous state 
		to->gunframe = from->gunframe;

		to->gunoffset[0] = from->gunoffset[0];
		to->gunoffset[1] = from->gunoffset[1];
		to->gunoffset[2] = from->gunoffset[2];

		to->gunangles[0] = from->gunangles[0];
		to->gunangles[1] = from->gunangles[1];
		to->gunangles[2] = from->gunangles[2];
	}

	statbits = 0;
	for( i = 0; i < MAX_STATS; i++ ) {
		if( to->stats[i] != from->stats[i] ) {
			statbits |= 1 << i;
		}
	}

	if( statbits ) {
		extraflags |= EPS_STATS;
	}

	//
	// write it
	//
	MSG_WriteShort( pflags );

	//
	// write the pmove_state_t
	//
	if( pflags & PS_M_TYPE )
		MSG_WriteByte( to->pmove.pm_type );

	if( pflags & PS_M_ORIGIN ) {
		MSG_WriteShort( to->pmove.origin[0] );
		MSG_WriteShort( to->pmove.origin[1] );
	}

	if( extraflags & EPS_M_ORIGIN2 ) {
		MSG_WriteShort( to->pmove.origin[2] );
	}

	if( pflags & PS_M_VELOCITY ) {
		MSG_WriteShort( to->pmove.velocity[0] );
		MSG_WriteShort( to->pmove.velocity[1] );
	}

	if( extraflags & EPS_M_VELOCITY2 ) {
		MSG_WriteShort( to->pmove.velocity[2] );
	}

	if( pflags & PS_M_TIME ) {
		MSG_WriteByte( to->pmove.pm_time );
	}

	if( pflags & PS_M_FLAGS ) {
		MSG_WriteByte( to->pmove.pm_flags );
	}

	if( pflags & PS_M_GRAVITY ) {
		MSG_WriteShort( to->pmove.gravity );
	}

	if( pflags & PS_M_DELTA_ANGLES ) {
		MSG_WriteShort( to->pmove.delta_angles[0] );
		MSG_WriteShort( to->pmove.delta_angles[1] );
		MSG_WriteShort( to->pmove.delta_angles[2] );
	}

	//
	// write the rest of the player_state_t
	//
	if( pflags & PS_VIEWOFFSET ) {
		MSG_WriteChar( to->viewoffset[0] * 4 );
		MSG_WriteChar( to->viewoffset[1] * 4 );
		MSG_WriteChar( to->viewoffset[2] * 4 );
	}

	if( pflags & PS_VIEWANGLES ) {
		MSG_WriteAngle16( to->viewangles[0] );
		MSG_WriteAngle16( to->viewangles[1] );
		
	}

	if( extraflags & EPS_VIEWANGLE2 ) {
		MSG_WriteAngle16( to->viewangles[2] );
	}

	if( pflags & PS_KICKANGLES ) {
		MSG_WriteChar( to->kick_angles[0] * 4 );
		MSG_WriteChar( to->kick_angles[1] * 4 );
		MSG_WriteChar( to->kick_angles[2] * 4 );
	}

	if( pflags & PS_WEAPONINDEX ) {
		MSG_WriteByte( to->gunindex );
	}

	if( pflags & PS_WEAPONFRAME ) {
		MSG_WriteByte( to->gunframe );
	}

	if( extraflags & EPS_GUNOFFSET ) {
		MSG_WriteChar( to->gunoffset[0] * 4 );
		MSG_WriteChar( to->gunoffset[1] * 4 );
		MSG_WriteChar( to->gunoffset[2] * 4 );
	}

	if( extraflags & EPS_GUNANGLES ) {
		MSG_WriteChar( to->gunangles[0] * 4 );
		MSG_WriteChar( to->gunangles[1] * 4 );
		MSG_WriteChar( to->gunangles[2] * 4 );
	}

	if( pflags & PS_BLEND ) {
		MSG_WriteByte( to->blend[0] * 255 );
		MSG_WriteByte( to->blend[1] * 255 );
		MSG_WriteByte( to->blend[2] * 255 );
		MSG_WriteByte( to->blend[3] * 255 );
	}

	if( pflags & PS_FOV )
		MSG_WriteByte( to->fov );

	if( pflags & PS_RDFLAGS )
		MSG_WriteByte( to->rdflags );

	// send stats
	if( extraflags & EPS_STATS ) {
		MSG_WriteLong( statbits );
		for( i = 0; i < MAX_STATS; i++ ) {
			if( statbits & ( 1 << i ) ) {
				MSG_WriteShort( to->stats[i] );
			}
		}
	}

	return extraflags;
}

/*
==================
MSG_WriteDeltaPlayerstate_Packet

Throw away most of the pmove_state_t fields as they are used only
for client prediction, and not needed in MVDs.
==================
*/
void MSG_WriteDeltaPlayerstate_Packet(  const player_state_t   *from,
                                        const player_state_t   *to,
                                              int              number,
                                              msgPsFlags_t     flags )
{
	int				i;
	int				pflags;
	int				statbits;

	if( number < 0 || number >= MAX_CLIENTS ) {
		Com_Error( ERR_DROP, "%s: bad number: %d", __func__, number );
	}

	if( !to ) {
		MSG_WriteByte( number );
		MSG_WriteShort( PPS_REMOVE );
		return;
	}

	if( !from ) {
		from = &nullPlayerState;
	}

	//
	// determine what needs to be sent
	//
	pflags = 0;

	if( to->pmove.pm_type != from->pmove.pm_type )
		pflags |= PPS_M_TYPE;

	if( to->pmove.origin[0] != from->pmove.origin[0] ||
		to->pmove.origin[1] != from->pmove.origin[1] )
	{
		pflags |= PPS_M_ORIGIN;
	}

	if( to->pmove.origin[2] != from->pmove.origin[2] ) {
		pflags |= PPS_M_ORIGIN2;
	}

	if( Delta_VecChar( from->viewoffset, to->viewoffset ) ) {
		pflags |= PPS_VIEWOFFSET;
	}

	if( Delta_Angle16( from->viewangles[0], to->viewangles[0] ) ||
		Delta_Angle16( from->viewangles[1], to->viewangles[1] ) )
	{
		pflags |= PPS_VIEWANGLES;
	}

	if( Delta_Angle16( from->viewangles[2], to->viewangles[2] ) ) {
		pflags |= PPS_VIEWANGLE2;
	}

	if( Delta_VecChar( from->kick_angles, to->kick_angles ) ) {
		pflags |= PPS_KICKANGLES;
	}

	if( !( flags & MSG_PS_IGNORE_BLEND ) ) {
		if( Delta_Blend( from->blend, to->blend ) ) {
			pflags |= PPS_BLEND;
		}
	}

	if( Delta_Fov( from->fov, to->fov ) )
		pflags |= PPS_FOV;

	if( to->rdflags != from->rdflags )
		pflags |= PPS_RDFLAGS;

	if( !( flags & MSG_PS_IGNORE_GUNINDEX ) ) {
		if( to->gunindex != from->gunindex )
			pflags |= PPS_WEAPONINDEX;
	}

	if( !( flags & MSG_PS_IGNORE_GUNFRAMES ) ) {
		if( to->gunframe != from->gunframe )
			pflags |= PPS_WEAPONFRAME;

		if( Delta_VecChar( from->gunoffset, to->gunoffset ) ) {
			pflags |= PPS_GUNOFFSET;
		}

		if( Delta_VecChar( from->gunangles, to->gunangles ) ) {
			pflags |= PPS_GUNANGLES;
		}
	}

	statbits = 0;
	for( i = 0; i < MAX_STATS; i++ ) {
		if( to->stats[i] != from->stats[i] ) {
			statbits |= 1 << i;
		    pflags |= PPS_STATS;
		}
	}

	if( !pflags && !( flags & MSG_PS_FORCE ) ) {
		return;
	}

	//
	// write it
	//
	MSG_WriteByte( number );
	MSG_WriteShort( pflags );

	//
	// write some part of the pmove_state_t
	//
	if( pflags & PPS_M_TYPE )
		MSG_WriteByte( to->pmove.pm_type );

	if( pflags & PPS_M_ORIGIN ) {
		MSG_WriteShort( to->pmove.origin[0] );
		MSG_WriteShort( to->pmove.origin[1] );
	}

	if( pflags & PPS_M_ORIGIN2 ) {
		MSG_WriteShort( to->pmove.origin[2] );
	}

	//
	// write the rest of the player_state_t
	//
	if( pflags & PPS_VIEWOFFSET ) {
		MSG_WriteChar( to->viewoffset[0] * 4 );
		MSG_WriteChar( to->viewoffset[1] * 4 );
		MSG_WriteChar( to->viewoffset[2] * 4 );
	}

	if( pflags & PPS_VIEWANGLES ) {
		MSG_WriteAngle16( to->viewangles[0] );
		MSG_WriteAngle16( to->viewangles[1] );
	}

	if( pflags & PPS_VIEWANGLE2 ) {
		MSG_WriteAngle16( to->viewangles[2] );
	}

	if( pflags & PPS_KICKANGLES ) {
		MSG_WriteChar( to->kick_angles[0] * 4 );
		MSG_WriteChar( to->kick_angles[1] * 4 );
		MSG_WriteChar( to->kick_angles[2] * 4 );
	}

	if( pflags & PPS_WEAPONINDEX ) {
		MSG_WriteByte( to->gunindex );
	}

	if( pflags & PPS_WEAPONFRAME ) {
		MSG_WriteByte( to->gunframe );
	}

	if( pflags & PPS_GUNOFFSET ) {
		MSG_WriteChar( to->gunoffset[0] * 4 );
		MSG_WriteChar( to->gunoffset[1] * 4 );
		MSG_WriteChar( to->gunoffset[2] * 4 );
	}

	if( pflags & PPS_GUNANGLES ) {
		MSG_WriteChar( to->gunangles[0] * 4 );
		MSG_WriteChar( to->gunangles[1] * 4 );
		MSG_WriteChar( to->gunangles[2] * 4 );
	}

	if( pflags & PPS_BLEND ) {
		MSG_WriteByte( to->blend[0] * 255 );
		MSG_WriteByte( to->blend[1] * 255 );
		MSG_WriteByte( to->blend[2] * 255 );
		MSG_WriteByte( to->blend[3] * 255 );
	}

	if( pflags & PPS_FOV )
		MSG_WriteByte( to->fov );

	if( pflags & PPS_RDFLAGS )
		MSG_WriteByte( to->rdflags );

	// send stats
	if( pflags & PPS_STATS ) {
		MSG_WriteLong( statbits );
		for( i = 0; i < MAX_STATS; i++ ) {
			if( statbits & ( 1 << i ) ) {
				MSG_WriteShort( to->stats[i] );
			}
		}
	}
}

/*
=============
MSG_FlushTo
=============
*/
void MSG_FlushTo( sizebuf_t *dest ) {
	memcpy( SZ_GetSpace( dest, msg_write.cursize ), msg_write.data, msg_write.cursize );	
	SZ_Clear( &msg_write );
}

void MSG_Printf( const char *fmt, ... ) {
    char buffer[MAX_STRING_CHARS];
	va_list		argptr;
	int			length;

	va_start( argptr, fmt );
	length = Q_vsnprintf( buffer, sizeof( buffer ), fmt, argptr );
	va_end( argptr );

    MSG_WriteData( buffer, length );
}

//============================================================

//
// reading functions
//

void MSG_BeginReading( void ) {
	msg_read.readcount = 0;
	msg_read.bitpos = 0;
}

// returns -1 if no more characters are available
int MSG_ReadChar (void)
{
	int	c;
	
	if (msg_read.readcount+1 > msg_read.cursize)
		c = -1;
	else
		c = (signed char)msg_read.data[msg_read.readcount];
	msg_read.readcount++;
	msg_read.bitpos = msg_read.readcount << 3;
	
	return c;
}

int MSG_ReadByte( void )
{
	int	c;
	
	if (msg_read.readcount+1 > msg_read.cursize)
		c = -1;
	else
		c = (unsigned char)msg_read.data[msg_read.readcount];
	msg_read.readcount++;
	msg_read.bitpos = msg_read.readcount << 3;
	
	return c;
}

int MSG_ReadShort( void )
{
	int	c;
	
	if (msg_read.readcount+2 > msg_read.cursize)
		c = -1;
	else		
		c = (short)(msg_read.data[msg_read.readcount]
		+ (msg_read.data[msg_read.readcount+1]<<8));
	
	msg_read.readcount += 2;
	msg_read.bitpos = msg_read.readcount << 3;
	
	return c;
}

int MSG_ReadWord( void )
{
	int	c;
	
	if (msg_read.readcount+2 > msg_read.cursize)
		c = -1;
	else		
		c = (unsigned short)(msg_read.data[msg_read.readcount]
		+ (msg_read.data[msg_read.readcount+1]<<8));
	
	msg_read.readcount += 2;
	msg_read.bitpos = msg_read.readcount << 3;
	
	return c;
}

int MSG_ReadLong ( void )
{
	int	c;
	
	if (msg_read.readcount+4 > msg_read.cursize)
		c = -1;
	else
		c = msg_read.data[msg_read.readcount]
		+ (msg_read.data[msg_read.readcount+1]<<8)
		+ (msg_read.data[msg_read.readcount+2]<<16)
		+ (msg_read.data[msg_read.readcount+3]<<24);
	
	msg_read.readcount += 4;
	msg_read.bitpos = msg_read.readcount << 3;
	
	return c;
}

char *MSG_ReadStringLength( int *length ) {
	static char	string[2][MAX_NET_STRING];
	static int index;
	char	*s;
	int		l, c;
	
	s = string[index];
	index ^= 1;

	l = 0;
	do {
		c = MSG_ReadByte();
		if( c == -1 || c == 0 ) {
			break;
		}
		if( c == 0xFF ) {
			c = '.';
		}
		s[l++] = c;
	} while( l < MAX_NET_STRING - 1 );
	
	s[l] = 0;

    if( length ) {
        *length = l;
    }
	
	return s;
}

char *MSG_ReadString( void ) {
	static char	string[2][MAX_NET_STRING];
	static int index;
	char	*s;
	int		l, c;
	
	s = string[index];
	index ^= 1;

	l = 0;
	do {
		c = MSG_ReadByte();
		if( c == -1 || c == 0 ) {
			break;
		}
		if( c == 0xFF ) {
			c = '.';
		}
		s[l++] = c;
	} while( l < MAX_NET_STRING - 1 );
	
	s[l] = 0;
	
	return s;
}

char *MSG_ReadStringLine( void ) {
	static char	string[2][MAX_STRING_CHARS];
	static int index;
	char	*s;
	int		l, c;
	
	s = string[index];
	index ^= 1;

	l = 0;
	do {
		c = MSG_ReadByte();
		if( c == -1 || c == 0 || c == '\n' ) {
			break;
		}
		if( c == 0xFF ) {
			c = '.';
		}
		s[l++] = c;
	} while( l < MAX_STRING_CHARS - 1 );
	
	s[l] = 0;
	
	return s;
}

float MSG_ReadCoord (void)
{
	return MSG_ReadShort() * (1.0/8);
}

void MSG_ReadPos ( vec3_t pos)
{
	pos[0] = MSG_ReadShort() * (1.0/8);
	pos[1] = MSG_ReadShort() * (1.0/8);
	pos[2] = MSG_ReadShort() * (1.0/8);
}

float MSG_ReadAngle (void)
{
	return MSG_ReadChar() * (360.0/256);
}

float MSG_ReadAngle16 (void)
{
	return SHORT2ANGLE(MSG_ReadShort());
}

void MSG_ReadDir( vec3_t dir ) {
	int		b;

	b = MSG_ReadByte();
	if( b < 0 || b >= NUMVERTEXNORMALS )
		Com_Error( ERR_DROP, "MSG_ReadDir: out of range" );
	VectorCopy( bytedirs[b], dir );
}

void MSG_ReadDeltaUsercmd( const usercmd_t *from, usercmd_t *to ) {
	int bits;

	if( from ) {
		memcpy( to, from, sizeof( *to ) );
	} else {
		memset( to, 0, sizeof( *to ) );
	}

	bits = MSG_ReadByte();
		
// read current angles
	if( bits & CM_ANGLE1 )
		to->angles[0] = MSG_ReadShort();
	if( bits & CM_ANGLE2 )
		to->angles[1] = MSG_ReadShort();
	if( bits & CM_ANGLE3 )
		to->angles[2] = MSG_ReadShort();
		
// read movement
	if( bits & CM_FORWARD )
		to->forwardmove = MSG_ReadShort();
	if( bits & CM_SIDE )
		to->sidemove = MSG_ReadShort();
	if( bits & CM_UP )
		to->upmove = MSG_ReadShort();
	
// read buttons
	if( bits & CM_BUTTONS )
		to->buttons = MSG_ReadByte();

	if( bits & CM_IMPULSE )
		to->impulse = MSG_ReadByte();

// read time to run command
	to->msec = MSG_ReadByte();

// read the light level
	to->lightlevel = MSG_ReadByte();
}

int MSG_ReadBits( int bits ) {
	int i, get, bitpos;
	qboolean sgn;
	int value;

	if( bits == 0 || bits < -31 || bits > 32 ) {
		Com_Error( ERR_FATAL, "MSG_ReadBits: bad bits: %d", bits );
	}

	bitpos = msg_read.bitpos;
	if( ( bitpos & 7 ) == 0 ) {
		// optimized case
		switch( bits ) {
		case -8:
			value = MSG_ReadChar();
			return value;
		case 8:
			value = MSG_ReadByte();
			return value;
		case -16:
			value = MSG_ReadShort();
			return value;
		case 32:
			value = MSG_ReadLong();
			return value;
		default:
			break;
		}
	}

	sgn = qfalse;
	if( bits < 0 ) {
		bits = -bits;
		sgn = qtrue;
	}

	value = 0;
	for( i = 0; i < bits; i++, bitpos++ ) {
		get = ( msg_read.data[ bitpos >> 3 ] >> ( bitpos & 7 ) ) & 1;
		value |= get << i;
	}
	msg_read.bitpos = bitpos;
	msg_read.readcount = ( bitpos + 7 ) >> 3;

	if( sgn ) {
		if( value & ( 1 << ( bits - 1 ) ) ) {
			value |= -1 ^ ( ( 1 << bits ) - 1 );
		}
	}

	return value;
}

void MSG_ReadDeltaUsercmd_Enhanced( const usercmd_t *from, usercmd_t *to ) {
	int bits;

	if( from ) {
		memcpy( to, from, sizeof( *to ) );
	} else {
		memset( to, 0, sizeof( *to ) );
	}

	if( !MSG_ReadBits( 1 ) ) {
		return;
	}

	bits = MSG_ReadBits( 8 );
		
// read current angles
	if( bits & CM_ANGLE1 ) {
		if( MSG_ReadBits( 1 ) ) {
			to->angles[0] += MSG_ReadBits( -8 );
		} else {
			to->angles[0] = MSG_ReadBits( -16 );
		}
	}
	if( bits & CM_ANGLE2 ) {
		if( MSG_ReadBits( 1 ) ) {
			to->angles[1] += MSG_ReadBits( -8 );
		} else {
			to->angles[1] = MSG_ReadBits( -16 );
		}
	}
	if( bits & CM_ANGLE3 ) {
		to->angles[2] = MSG_ReadBits( -16 );
	}
		
// read movement
	if( bits & CM_FORWARD ) {
		to->forwardmove = MSG_ReadBits( -16 );
	}
	if( bits & CM_SIDE ) {
		to->sidemove = MSG_ReadBits( -16 );
	}
	if( bits & CM_UP ) {
		to->upmove = MSG_ReadBits( -16 );
	}
	
// read buttons
	if( bits & CM_BUTTONS ) {
		int buttons = MSG_ReadBits( 3 );
        to->buttons = ( buttons & 3 ) | ( ( buttons & 4 ) << 5 );
	}

// read time to run command
	if( bits & CM_IMPULSE ) {
		to->msec = MSG_ReadBits( 8 );
	}
}


void MSG_ReadData ( void *data, int len)
{
	int		i;

	for (i=0 ; i<len ; i++)
		((byte *)data)[i] = MSG_ReadByte ();
}

/*
=================
MSG_ParseEntityBits

Returns the entity number and the header bits
=================
*/
int MSG_ParseEntityBits( int *bits ) {
	int     	b, total;
	int			number;

	total = MSG_ReadByte();
	if( total & U_MOREBITS1 ) {
		b = MSG_ReadByte(); 
		total |= b<<8;
	}
	if( total & U_MOREBITS2 ) {
		b = MSG_ReadByte();
		total |= b<<16;
	}
	if( total & U_MOREBITS3 ) {
		b = MSG_ReadByte();
		total |= b<<24;
	}

	if( total & U_NUMBER16 )
		number = MSG_ReadShort();
	else
		number = MSG_ReadByte();

	*bits = total;

	return number;
}

/*
==================
MSG_ParseDeltaEntity

Can go from either a baseline or a previous packet_entity
==================
*/
void MSG_ParseDeltaEntity( const entity_state_t *from, entity_state_t *to, int number, int bits ) {
	if( !to ) {
		Com_Error( ERR_DROP, "MSG_ParseDeltaEntity: NULL" );
	}

	if( number < 1 || number >= MAX_EDICTS ) {
		Com_Error( ERR_DROP, "MSG_ParseDeltaEntity: bad entity number %i", number );
	}

	// set everything to the state we are delta'ing from
	if( from ) {
		memcpy( to, from, sizeof( *to ) );
		VectorCopy( from->origin, to->old_origin );
	} else {
		memset( to, 0, sizeof( *to ) );
		from = &nullEntityState;
	}
	
	to->number = number;
	to->event = 0;

	if( !bits ) {
		return;
	}

	if( bits & U_MODEL ) {
		to->modelindex = MSG_ReadByte();
	}
	if( bits & U_MODEL2 ) {
		to->modelindex2 = MSG_ReadByte();
	}
	if( bits & U_MODEL3 ) {
		to->modelindex3 = MSG_ReadByte();
	}
	if( bits & U_MODEL4 ) {
		to->modelindex4 = MSG_ReadByte();
	}
		
	if( bits & U_FRAME8 )
		to->frame = MSG_ReadByte();
	if( bits & U_FRAME16 )
		to->frame = MSG_ReadShort();

	if( (bits & (U_SKIN8|U_SKIN16)) == (U_SKIN8|U_SKIN16) )		//used for laser colors
		to->skinnum = MSG_ReadLong();
	else if( bits & U_SKIN8 )
		to->skinnum = MSG_ReadByte();
	else if( bits & U_SKIN16 )
		to->skinnum = MSG_ReadShort();

	if( (bits & (U_EFFECTS8|U_EFFECTS16)) == (U_EFFECTS8|U_EFFECTS16) )
		to->effects = MSG_ReadLong();
	else if( bits & U_EFFECTS8 )
		to->effects = MSG_ReadByte();
	else if( bits & U_EFFECTS16 )
		to->effects = MSG_ReadWord();//Short();

	if( (bits & (U_RENDERFX8|U_RENDERFX16)) == (U_RENDERFX8|U_RENDERFX16) )
		to->renderfx = MSG_ReadLong();
	else if( bits & U_RENDERFX8 )
		to->renderfx = MSG_ReadByte();
	else if( bits & U_RENDERFX16 )
		to->renderfx = MSG_ReadWord();//Short();

	if( bits & U_ORIGIN1 ) {
		to->origin[0] = MSG_ReadCoord();
	}
	if( bits & U_ORIGIN2 ) {
		to->origin[1] = MSG_ReadCoord();
	}
	if( bits & U_ORIGIN3 ) {
		to->origin[2] = MSG_ReadCoord();
	}
		
	if( bits & U_ANGLE1 ) {
		to->angles[0] = MSG_ReadAngle();
	}
	if( bits & U_ANGLE2 ) {
		to->angles[1] = MSG_ReadAngle();
	}
	if( bits & U_ANGLE3 ) {
		to->angles[2] = MSG_ReadAngle();
	}

	if( bits & U_OLDORIGIN ) {
		MSG_ReadPos( to->old_origin );
	}

	if( bits & U_SOUND ) {
		to->sound = MSG_ReadByte();
	}

	if( bits & U_EVENT ) {
		to->event = MSG_ReadByte();
	}

	if( bits & U_SOLID ) {
		to->solid = MSG_ReadWord();
	}
}

/*
===================
MSG_ParseDeltaPlayerstate_Default
===================
*/
void MSG_ParseDeltaPlayerstate_Default( const player_state_t *from, player_state_t *to, int flags ) {
	int			i;
	int			statbits;

	if( !to ) {
		Com_Error( ERR_DROP, "MSG_ParseDeltaPlayerstate_Default: NULL" );
	}

	// clear to old value before delta parsing
	if( from ) {
		memcpy( to, from, sizeof( *to ) );
	} else {
		memset( to, 0, sizeof( *to ) );
	}

	//
	// parse the pmove_state_t
	//
	if( flags & PS_M_TYPE )
		to->pmove.pm_type = MSG_ReadByte();

	if( flags & PS_M_ORIGIN ) {
		to->pmove.origin[0] = MSG_ReadShort();
		to->pmove.origin[1] = MSG_ReadShort();
		to->pmove.origin[2] = MSG_ReadShort();
	}

	if( flags & PS_M_VELOCITY ) {
		to->pmove.velocity[0] = MSG_ReadShort();
		to->pmove.velocity[1] = MSG_ReadShort();
		to->pmove.velocity[2] = MSG_ReadShort();
	}

	if( flags & PS_M_TIME )
		to->pmove.pm_time = MSG_ReadByte();

	if( flags & PS_M_FLAGS )
		to->pmove.pm_flags = MSG_ReadByte();

	if( flags & PS_M_GRAVITY )
		to->pmove.gravity = MSG_ReadShort();

	if( flags & PS_M_DELTA_ANGLES ) {
		to->pmove.delta_angles[0] = MSG_ReadShort();
		to->pmove.delta_angles[1] = MSG_ReadShort();
		to->pmove.delta_angles[2] = MSG_ReadShort();
	}

	//
	// parse the rest of the player_state_t
	//
	if( flags & PS_VIEWOFFSET ) {
		to->viewoffset[0] = MSG_ReadChar() * 0.25f;
		to->viewoffset[1] = MSG_ReadChar() * 0.25f;
		to->viewoffset[2] = MSG_ReadChar() * 0.25f;
	}

	if( flags & PS_VIEWANGLES ) {
		to->viewangles[0] = MSG_ReadAngle16();
		to->viewangles[1] = MSG_ReadAngle16();
		to->viewangles[2] = MSG_ReadAngle16();
	}

	if( flags & PS_KICKANGLES ) {
		to->kick_angles[0] = MSG_ReadChar() * 0.25f;
		to->kick_angles[1] = MSG_ReadChar() * 0.25f;
		to->kick_angles[2] = MSG_ReadChar() * 0.25f;
	}

	if( flags & PS_WEAPONINDEX ) {
		to->gunindex = MSG_ReadByte();
	}

	if( flags & PS_WEAPONFRAME ) {
		to->gunframe = MSG_ReadByte();
		to->gunoffset[0] = MSG_ReadChar() * 0.25f;
		to->gunoffset[1] = MSG_ReadChar() * 0.25f;
		to->gunoffset[2] = MSG_ReadChar() * 0.25f;
		to->gunangles[0] = MSG_ReadChar() * 0.25f;
		to->gunangles[1] = MSG_ReadChar() * 0.25f;
		to->gunangles[2] = MSG_ReadChar() * 0.25f;
	}

	if( flags & PS_BLEND ) {
		to->blend[0] = MSG_ReadByte() / 255.0f;
		to->blend[1] = MSG_ReadByte() / 255.0f;
		to->blend[2] = MSG_ReadByte() / 255.0f;
		to->blend[3] = MSG_ReadByte() / 255.0f;
	}

	if( flags & PS_FOV )
		to->fov = MSG_ReadByte();

	if( flags & PS_RDFLAGS )
		to->rdflags = MSG_ReadByte();

	// parse stats
	statbits = MSG_ReadLong();
	for( i = 0; i < MAX_STATS; i++ )
		if( statbits & ( 1 << i ) )
			to->stats[i] = MSG_ReadShort();
}


/*
===================
MSG_ParseDeltaPlayerstate_Default
===================
*/
void MSG_ParseDeltaPlayerstate_Enhanced(    const player_state_t    *from,
                                                  player_state_t    *to,
                                                  int               flags,
                                                  int               extraflags )
{
	int			i;
	int			statbits;

	if( !to ) {
		Com_Error( ERR_DROP, "MSG_ParseDeltaPlayerstate_Enhanced: NULL" );
	}

	// clear to old value before delta parsing
	if( from ) {
		memcpy( to, from, sizeof( *to ) );
	} else {
		memset( to, 0, sizeof( *to ) );
	}

	//
	// parse the pmove_state_t
	//
	if( flags & PS_M_TYPE )
		to->pmove.pm_type = MSG_ReadByte();

	if( flags & PS_M_ORIGIN ) {
		to->pmove.origin[0] = MSG_ReadShort();
		to->pmove.origin[1] = MSG_ReadShort();
	}

	if( extraflags & EPS_M_ORIGIN2 ) {
		to->pmove.origin[2] = MSG_ReadShort();
	}

	if( flags & PS_M_VELOCITY ) {
		to->pmove.velocity[0] = MSG_ReadShort();
		to->pmove.velocity[1] = MSG_ReadShort();
	}

	if( extraflags & EPS_M_VELOCITY2 ) {
		to->pmove.velocity[2] = MSG_ReadShort();
	}

	if( flags & PS_M_TIME )
		to->pmove.pm_time = MSG_ReadByte();

	if( flags & PS_M_FLAGS )
		to->pmove.pm_flags = MSG_ReadByte();

	if( flags & PS_M_GRAVITY )
		to->pmove.gravity = MSG_ReadShort();

	if( flags & PS_M_DELTA_ANGLES ) {
		to->pmove.delta_angles[0] = MSG_ReadShort();
		to->pmove.delta_angles[1] = MSG_ReadShort();
		to->pmove.delta_angles[2] = MSG_ReadShort();
	}

	//
	// parse the rest of the player_state_t
	//
	if( flags & PS_VIEWOFFSET ) {
		to->viewoffset[0] = MSG_ReadChar() * 0.25f;
		to->viewoffset[1] = MSG_ReadChar() * 0.25f;
		to->viewoffset[2] = MSG_ReadChar() * 0.25f;
	}

	if( flags & PS_VIEWANGLES ) {
		to->viewangles[0] = MSG_ReadAngle16();
		to->viewangles[1] = MSG_ReadAngle16();
	}

	if( extraflags & EPS_VIEWANGLE2 ) {
		to->viewangles[2] = MSG_ReadAngle16();
	}

	if( flags & PS_KICKANGLES ) {
		to->kick_angles[0] = MSG_ReadChar() * 0.25f;
		to->kick_angles[1] = MSG_ReadChar() * 0.25f;
		to->kick_angles[2] = MSG_ReadChar() * 0.25f;
	}

	if( flags & PS_WEAPONINDEX ) {
		to->gunindex = MSG_ReadByte();
	}

	if( flags & PS_WEAPONFRAME ) {
		to->gunframe = MSG_ReadByte();
	}

	if( extraflags & EPS_GUNOFFSET ) {
		to->gunoffset[0] = MSG_ReadChar() * 0.25f;
		to->gunoffset[1] = MSG_ReadChar() * 0.25f;
		to->gunoffset[2] = MSG_ReadChar() * 0.25f;
	}

	if( extraflags & EPS_GUNANGLES ) {
		to->gunangles[0] = MSG_ReadChar() * 0.25f;
		to->gunangles[1] = MSG_ReadChar() * 0.25f;
		to->gunangles[2] = MSG_ReadChar() * 0.25f;
	}

	if( flags & PS_BLEND ) {
		to->blend[0] = MSG_ReadByte() / 255.0f;
		to->blend[1] = MSG_ReadByte() / 255.0f;
		to->blend[2] = MSG_ReadByte() / 255.0f;
		to->blend[3] = MSG_ReadByte() / 255.0f;
	}

	if( flags & PS_FOV )
		to->fov = MSG_ReadByte();

	if( flags & PS_RDFLAGS )
		to->rdflags = MSG_ReadByte();

	// parse stats
	if( extraflags & EPS_STATS ) {
		statbits = MSG_ReadLong();
		for( i = 0; i < MAX_STATS; i++ ) {
			if( statbits & ( 1 << i ) ) {
				to->stats[i] = MSG_ReadShort();
			}
		}
	}
	
}

/*
===================
MSG_ParseDeltaPlayerstate_Packet
===================
*/
void MSG_ParseDeltaPlayerstate_Packet( const player_state_t *from, player_state_t *to, int flags ) {
	int			i;
	int			statbits;

	if( !to ) {
		Com_Error( ERR_DROP, "MSG_ParseDeltaPlayerstate_Packet: NULL" );
	}

	// clear to old value before delta parsing
	if( from ) {
		memcpy( to, from, sizeof( *to ) );
	} else {
		memset( to, 0, sizeof( *to ) );
	}

	//
	// parse the pmove_state_t
	//
	if( flags & PPS_M_TYPE )
		to->pmove.pm_type = MSG_ReadByte();

	if( flags & PPS_M_ORIGIN ) {
		to->pmove.origin[0] = MSG_ReadShort();
		to->pmove.origin[1] = MSG_ReadShort();
	}

	if( flags & PPS_M_ORIGIN2 ) {
		to->pmove.origin[2] = MSG_ReadShort();
	}

	//
	// parse the rest of the player_state_t
	//
	if( flags & PPS_VIEWOFFSET ) {
		to->viewoffset[0] = MSG_ReadChar() * 0.25f;
		to->viewoffset[1] = MSG_ReadChar() * 0.25f;
		to->viewoffset[2] = MSG_ReadChar() * 0.25f;
	}

	if( flags & PPS_VIEWANGLES ) {
		to->viewangles[0] = MSG_ReadAngle16();
		to->viewangles[1] = MSG_ReadAngle16();
	}

	if( flags & PPS_VIEWANGLE2 ) {
		to->viewangles[2] = MSG_ReadAngle16();
	}

	if( flags & PPS_KICKANGLES ) {
		to->kick_angles[0] = MSG_ReadChar() * 0.25f;
		to->kick_angles[1] = MSG_ReadChar() * 0.25f;
		to->kick_angles[2] = MSG_ReadChar() * 0.25f;
	}

	if( flags & PPS_WEAPONINDEX ) {
		to->gunindex = MSG_ReadByte();
	}

	if( flags & PPS_WEAPONFRAME ) {
		to->gunframe = MSG_ReadByte();
	}

	if( flags & PPS_GUNOFFSET ) {
		to->gunoffset[0] = MSG_ReadChar() * 0.25f;
		to->gunoffset[1] = MSG_ReadChar() * 0.25f;
		to->gunoffset[2] = MSG_ReadChar() * 0.25f;
	}

	if( flags & PPS_GUNANGLES ) {
		to->gunangles[0] = MSG_ReadChar() * 0.25f;
		to->gunangles[1] = MSG_ReadChar() * 0.25f;
		to->gunangles[2] = MSG_ReadChar() * 0.25f;
	}

	if( flags & PPS_BLEND ) {
		to->blend[0] = MSG_ReadByte() / 255.0f;
		to->blend[1] = MSG_ReadByte() / 255.0f;
		to->blend[2] = MSG_ReadByte() / 255.0f;
		to->blend[3] = MSG_ReadByte() / 255.0f;
	}

	if( flags & PPS_FOV )
		to->fov = MSG_ReadByte();

	if( flags & PPS_RDFLAGS )
		to->rdflags = MSG_ReadByte();

	// parse stats
	if( flags & PPS_STATS ) {
		statbits = MSG_ReadLong();
		for( i = 0; i < MAX_STATS; i++ ) {
			if( statbits & ( 1 << i ) ) {
				to->stats[i] = MSG_ReadShort();
			}
		}
	}
	
}

#define SHOWBITS( data ) \
	do { Com_Printf( "%s ", data ); } while( 0 )

void MSG_ShowDeltaEntityBits( int bits ) {
	if( bits & U_MODEL ) {
		SHOWBITS( "modelindex" );
	}
	if( bits & U_MODEL2 ) {
		SHOWBITS( "modelindex2" );
	}
	if( bits & U_MODEL3 ) {
		SHOWBITS( "modelindex3" );
	}
	if( bits & U_MODEL4 ) {
		SHOWBITS( "modelindex4" );
	}

	if( bits & U_FRAME8 )
		SHOWBITS( "frame8" );
	if( bits & U_FRAME16 )
		SHOWBITS( "frame16" );

	if( ( bits & ( U_SKIN8 | U_SKIN16 ) ) == ( U_SKIN8 | U_SKIN16 ) )
		SHOWBITS( "skinnum32" );
	else if( bits & U_SKIN8 )
		SHOWBITS( "skinnum8" );
	else if( bits & U_SKIN16 )
		SHOWBITS( "skinnum16" );

	if( ( bits & ( U_EFFECTS8 | U_EFFECTS16 ) ) == ( U_EFFECTS8 | U_EFFECTS16 ) )
		SHOWBITS( "effects32" );
	else if( bits & U_EFFECTS8 )
		SHOWBITS( "effects8" );
	else if( bits & U_EFFECTS16 )
		SHOWBITS( "effects16" );

	if( ( bits & ( U_RENDERFX8 | U_RENDERFX16 ) ) == ( U_RENDERFX8 | U_RENDERFX16 ) )
		SHOWBITS( "renderfx32" );
	else if( bits & U_RENDERFX8 )
		SHOWBITS( "renderfx8" );
	else if( bits & U_RENDERFX16 )
		SHOWBITS( "renderfx16" );

	if( bits & U_ORIGIN1 ) {
		SHOWBITS( "origin[0]" );
	}
	if( bits & U_ORIGIN2 ) {
		SHOWBITS( "origin[1]" );
	}
	if( bits & U_ORIGIN3 ) {
		SHOWBITS( "origin[2]" );
	}
		
	if( bits & U_ANGLE1 ) {
		SHOWBITS( "angles[0]" );
	}
	if( bits & U_ANGLE2 ) {
		SHOWBITS( "angles[2]" );
	}
	if( bits & U_ANGLE3 ) {
		SHOWBITS( "angles[3]" );
	}

	if( bits & U_OLDORIGIN ) {
		SHOWBITS( "old_origin" );
	}

	if( bits & U_SOUND ) {
		SHOWBITS( "sound" );
	}

	if( bits & U_EVENT ) {
		SHOWBITS( "event" );
	}

	if( bits & U_SOLID ) {
		SHOWBITS( "solid" );
	}

}

void MSG_ShowDeltaPlayerstateBits_Default( int flags ) {
	if( flags & PS_M_TYPE ) {
		SHOWBITS( "pmove.pm_type" );
	}

	if( flags & PS_M_ORIGIN ) {
		SHOWBITS( "pmove.origin" );
	}

	if( flags & PS_M_VELOCITY ) {
		SHOWBITS( "pmove.velocity" );
	}

	if( flags & PS_M_TIME ) {
		SHOWBITS( "pmove.pm_time" );
	}

	if( flags & PS_M_FLAGS ) {
		SHOWBITS( "pmove.pm_flags" );
	}

	if( flags & PS_M_GRAVITY ) {
		SHOWBITS( "pmove.gravity" );
	}

	if( flags & PS_M_DELTA_ANGLES ) {
		SHOWBITS( "pmove.delta_angles" );
	}

	if( flags & PS_VIEWOFFSET ) {
		SHOWBITS( "viewoffset" );
	}

	if( flags & PS_VIEWANGLES ) {
		SHOWBITS( "viewangles" );
	}

	if( flags & PS_KICKANGLES ) {
		SHOWBITS( "kick_angles" );
	}

	if( flags & PS_WEAPONINDEX ) {
		SHOWBITS( "gunindex" );
	}

	if( flags & PS_WEAPONFRAME ) {
		SHOWBITS( "gunframe" );
	}

	if( flags & PS_BLEND ) {
		SHOWBITS( "blend" );
	}

	if( flags & PS_FOV ) {
		SHOWBITS( "fov" );
	}

	if( flags & PS_RDFLAGS ) {
		SHOWBITS( "rdflags" );
	}
	
}

void MSG_ShowDeltaPlayerstateBits_Enhanced( int flags ) {
	int extraflags;

	extraflags = flags >> PS_BITS;
	flags &= PS_MASK;

	if( flags & PS_M_TYPE ) {
		SHOWBITS( "pmove.pm_type" );
	}

	if( flags & PS_M_ORIGIN ) {
		SHOWBITS( "pmove.origin[0,1]" );
	}

	if( extraflags & EPS_M_ORIGIN2 ) {
		SHOWBITS( "pmove.origin[2]" );
	}

	if( flags & PS_M_VELOCITY ) {
		SHOWBITS( "pmove.velocity[0,1]" );
	}

	if( extraflags & EPS_M_VELOCITY2 ) {
		SHOWBITS( "pmove.velocity[2]" );
	}

	if( flags & PS_M_TIME ) {
		SHOWBITS( "pmove.pm_time" );
	}

	if( flags & PS_M_FLAGS ) {
		SHOWBITS( "pmove.pm_flags" );
	}

	if( flags & PS_M_GRAVITY ) {
		SHOWBITS( "pmove.gravity" );
	}

	if( flags & PS_M_DELTA_ANGLES ) {
		SHOWBITS( "pmove.delta_angles" );
	}

	if( flags & PS_VIEWOFFSET ) {
		SHOWBITS( "viewoffset" );
	}

	if( flags & PS_VIEWANGLES ) {
		SHOWBITS( "viewangles[0,1]" );
	}

	if( extraflags & EPS_VIEWANGLE2 ) {
		SHOWBITS( "viewangles[2]" );
	}

	if( flags & PS_KICKANGLES ) {
		SHOWBITS( "kick_angles" );
	}

	if( flags & PS_WEAPONINDEX ) {
		SHOWBITS( "gunindex" );
	}

	if( flags & PS_WEAPONFRAME ) {
		SHOWBITS( "gunframe" );
	}

	if( extraflags & EPS_GUNOFFSET ) {
		SHOWBITS( "gunoffset" );
	}

	if( extraflags & EPS_GUNANGLES ) {
		SHOWBITS( "gunangles" );
	}

	if( flags & PS_BLEND ) {
		SHOWBITS( "blend" );
	}

	if( flags & PS_FOV ) {
		SHOWBITS( "fov" );
	}

	if( flags & PS_RDFLAGS ) {
		SHOWBITS( "rdflags" );
	}

	if( extraflags & EPS_STATS ) {
		SHOWBITS( "stats" );
	}
}

void MSG_ShowDeltaPlayerstateBits_Packet( int flags ) {
	if( flags & PPS_M_TYPE ) {
		SHOWBITS( "pmove.pm_type" );
	}

	if( flags & PPS_M_ORIGIN ) {
		SHOWBITS( "pmove.origin[0,1]" );
	}

	if( flags & PPS_M_ORIGIN2 ) {
		SHOWBITS( "pmove.origin[2]" );
	}

	if( flags & PPS_VIEWOFFSET ) {
		SHOWBITS( "viewoffset" );
	}

	if( flags & PPS_VIEWANGLES ) {
		SHOWBITS( "viewangles[0,1]" );
	}

	if( flags & PPS_VIEWANGLE2 ) {
		SHOWBITS( "viewangles[2]" );
	}

	if( flags & PPS_KICKANGLES ) {
		SHOWBITS( "kick_angles" );
	}

	if( flags & PPS_WEAPONINDEX ) {
		SHOWBITS( "gunindex" );
	}

	if( flags & PPS_WEAPONFRAME ) {
		SHOWBITS( "gunframe" );
	}

	if( flags & PPS_GUNOFFSET ) {
		SHOWBITS( "gunoffset" );
	}

	if( flags & PPS_GUNANGLES ) {
		SHOWBITS( "gunangles" );
	}

	if( flags & PPS_BLEND ) {
		SHOWBITS( "blend" );
	}

	if( flags & PPS_FOV ) {
		SHOWBITS( "fov" );
	}

	if( flags & PPS_RDFLAGS ) {
		SHOWBITS( "rdflags" );
	}

	if( flags & PPS_STATS ) {
		SHOWBITS( "stats" );
	}
}

void MSG_ShowDeltaUsercmdBits_Enhanced( int bits ) {
	if( !bits ) {
		SHOWBITS( "<none>" );
		return;
	}
	if( bits & CM_ANGLE1 )
		SHOWBITS( "angle1" );
	if( bits & CM_ANGLE2 )
		SHOWBITS( "angle2" );
	if( bits & CM_ANGLE3 )
		SHOWBITS( "angle3" );
	
	if( bits & CM_FORWARD )
		SHOWBITS( "forward" );
	if( bits & CM_SIDE )
	  	SHOWBITS( "side" );
	if( bits & CM_UP )
		SHOWBITS( "up" );

 	if( bits & CM_BUTTONS )
	  	SHOWBITS( "buttons" );
 	if( bits & CM_IMPULSE )
	    SHOWBITS( "msec" );
}

static const char *const svc_strings[svc_num_types] = {
	"svc_bad",

	// these ops are known to the game dll
	"svc_muzzleflash",
	"svc_muzzleflash2",
	"svc_temp_entity",
	"svc_layout",
	"svc_inventory",

	// the rest are private to the client and server
	"svc_nop",
	"svc_disconnect",
	"svc_reconnect",
	"svc_sound",
	"svc_print",
	"svc_stufftext",
	"svc_serverdata",
	"svc_configstring",
	"svc_spawnbaseline",
	"svc_centerprint",
	"svc_download",
	"svc_playerinfo",
	"svc_packetentities",
	"svc_deltapacketentities",
	"svc_frame",

	// r1q2 specific operations
	"svc_zpacket",
	"svc_zdownload",

    // q2pro specific operations
    "svc_gamestate"
};

const char *MSG_ServerCommandString( int cmd ) {
    const char *s;

    if( cmd == -1 ) {
        s = "END OF MESSAGE";
    } else if( cmd >= 0 && cmd < svc_num_types ) {
        s = svc_strings[cmd];
    } else {
		s = "UNKNOWN COMMAND";
	}

    return s;
}

//===========================================================================

void SZ_Init( sizebuf_t *buf, void *data, int length ) {
	memset( buf, 0, sizeof( *buf ) );
	buf->data = data;
	buf->maxsize = length;
	buf->allowoverflow = qtrue;
}

void SZ_Clear( sizebuf_t *buf ) {
	buf->cursize = 0;
	buf->readcount = 0;
	buf->bitpos = 0;
	buf->overflowed = qfalse;
}

void *SZ_GetSpace( sizebuf_t *buf, int length ) {
	void	*data;
	
	if( buf->cursize + length > buf->maxsize ) {
		if( !buf->allowoverflow ) {
			Com_Error( ERR_FATAL, "SZ_GetSpace: overflow without allowoverflow set" );
		}
		if( length > buf->maxsize ) {
			Com_Error( ERR_FATAL, "SZ_GetSpace: %i is > full buffer size", length );
		}
			
		Com_DPrintf( "SZ_GetSpace: overflow\n" );
		SZ_Clear( buf ); 
		buf->overflowed = qtrue;
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;
	buf->bitpos = buf->cursize << 3;
	
	return data;
}

void SZ_Write( sizebuf_t *buf, const void *data, int length ) {
	memcpy( SZ_GetSpace( buf, length ), data, length );		
}

void SZ_WriteByte( sizebuf_t *sb, int c ) {
	byte	*buf;

	buf = SZ_GetSpace( sb, 1 );
	buf[0] = c;
}

void SZ_WriteShort( sizebuf_t *sb, int c ) {
	byte	*buf;

	buf = SZ_GetSpace( sb, 2 );
	buf[0] = c & 0xff;
	buf[1] = c >> 8;
}

void SZ_WriteLong( sizebuf_t *sb, int c ) {
	byte	*buf;
	
	buf = SZ_GetSpace( sb, 4 );
	buf[0] = c & 0xff;
	buf[1] = ( c >> 8 ) & 0xff;
	buf[2] = ( c >> 16 ) & 0xff;
	buf[3] = c >> 24;
}

void SZ_WritePos( sizebuf_t *sb, const vec3_t pos ) {
	SZ_WriteShort( sb, ( int )( pos[0] * 8 ) );
	SZ_WriteShort( sb, ( int )( pos[1] * 8 ) );
	SZ_WriteShort( sb, ( int )( pos[2] * 8 ) );
}

void SZ_WriteString( sizebuf_t *sb, const char *string ) {
	int length;
	int c;

	if( !string ) {
		SZ_WriteByte( sb, 0 );
		return;
	}

	length = strlen( string );
	if( length > MAX_NET_STRING - 1 ) {
		Com_WPrintf( "SZ_WriteString: overflow: %d chars", length );
		SZ_WriteByte( sb, 0 );
		return;
	}

	while( *string ) {
		c = *string++;
		if( c == '\xFF' ) {
			c = '.';
		}
		SZ_WriteByte( sb, c );
	}

	SZ_WriteByte( sb, 0 );
}




