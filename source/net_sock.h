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

// net.h -- quake's interface to the networking layer

#define	PORT_ANY	-1

#define MAX_PACKETLEN	4096		// max length of a single packet
#define	PACKET_HEADER	10			// two ints and a short (worst case)
#define MAX_PACKETLEN_DEFAULT	1400		// default quake2 limit
#define MAX_PACKETLEN_WRITABLE    ( MAX_PACKETLEN - PACKET_HEADER )
#define MAX_PACKETLEN_WRITABLE_DEFAULT    ( MAX_PACKETLEN_DEFAULT - PACKET_HEADER )

typedef enum netadrtype_e {
	NA_BAD,
	NA_LOOPBACK,
	NA_BROADCAST,
	NA_IP
} netadrtype_t;

typedef enum netsrc_e {
	NS_CLIENT,
	NS_SERVER,
	NS_COUNT
} netsrc_t;

typedef enum netflag_e {
	NET_NONE	= 0,
	NET_CLIENT	= ( 1 << 0 ),
	NET_SERVER	= ( 1 << 1 )
} netflag_t;

typedef enum netstat_e {
    NET_OK,
    NET_AGAIN,
    NET_CLOSED,
    NET_ERROR,
} neterr_t;

typedef struct netadr_s {
	netadrtype_t	type;
	uint8_t     ip[4];
	uint16_t    port;
} netadr_t;

static inline qboolean NET_IsEqualAdr( const netadr_t *a, const netadr_t *b ) {
	if( a->type != b->type ) {
		return qfalse;
	}

	switch( a->type ) {
	case NA_LOOPBACK:
		return qtrue;
	case NA_IP:
	case NA_BROADCAST:
		if( *( uint32_t * )a->ip == *( uint32_t * )b->ip && a->port == b->port ) {
			return qtrue;
		}
		return qfalse;
	default:
		break;
	}

	return qfalse;
}

static inline qboolean NET_IsEqualBaseAdr( const netadr_t *a, const netadr_t *b ) {
	if( a->type != b->type ) {
		return qfalse;
	}

	switch( a->type ) {
	case NA_LOOPBACK:
		return qtrue;
	case NA_IP:
	case NA_BROADCAST:
		if( *( uint32_t * )a->ip == *( uint32_t * )b->ip ) {
			return qtrue;
		}
		return qfalse;
	default:
		break;
	}

	return qfalse;
}

static inline qboolean NET_IsLanAddress( const netadr_t *adr ) {
	switch( adr->type ) {
	case NA_LOOPBACK:
		return qtrue;
	case NA_IP:
	case NA_BROADCAST:
		if( adr->ip[0] == 127 || adr->ip[0] == 10 ) {
			return qtrue;
		}
		if( *( uint16_t * )adr->ip == MakeShort( 192, 168 ) ||
            *( uint16_t * )adr->ip == MakeShort( 172,  16 ) )
		{
			return qtrue;
		}
		return qfalse;
	default:
		break;
	}

	return qfalse;
}

void		NET_Init( void );
void		NET_Shutdown( void );

void		NET_Config( netflag_t flag );
qboolean    NET_GetAddress( netsrc_t sock, netadr_t *adr );

neterr_t	NET_GetPacket( netsrc_t sock );
neterr_t	NET_SendPacket( netsrc_t sock, const netadr_t *to, size_t length, const void *data );
qboolean	NET_GetLoopPacket( netsrc_t sock );

char *		NET_AdrToString( const netadr_t *a );
qboolean	NET_StringToAdr( const char *s, netadr_t *a, int port );
void		NET_Sleep( int msec );

#if USE_CLIENT
#define		NET_IsLocalAddress( adr )	( (adr)->type == NA_LOOPBACK )
#else
#define		NET_IsLocalAddress( adr )	0
#endif

const char	*NET_ErrorString( void );

extern cvar_t       *net_ip;
extern cvar_t       *net_port;

extern netadr_t     net_from;


