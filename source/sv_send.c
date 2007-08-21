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
// sv_send.c

#include "sv_local.h"
#include "mvd_local.h"

/*
=============================================================================

MISC

=============================================================================
*/

char sv_outputbuf[SV_OUTPUTBUF_LENGTH];

void SV_FlushRedirect( int sv_redirected, char *outputbuf ) {
	if( sv_redirected == RD_PACKET ) {
		Netchan_OutOfBandPrint( NS_SERVER, &net_from, "print\n%s", outputbuf );
	} else if( sv_redirected == RD_CLIENT ) {
		MSG_WriteByte( svc_print );
		MSG_WriteByte( PRINT_HIGH );
		MSG_WriteString( outputbuf );
		SV_ClientAddMessage( sv_client, MSG_RELIABLE|MSG_CLEAR );
	}
}

/*
=======================
SV_RateDrop

Returns qtrue if the client is over its current
bandwidth estimation and should not be sent another packet
=======================
*/
static qboolean SV_RateDrop( client_t *client ) {
	int		total;
	int		i;

	// never drop over the loopback
	if( !client->rate ) {
		return qfalse;
	}

	total = 0;
	for( i = 0; i < RATE_MESSAGES; i++ ) {
		total += client->message_size[i];
	}

	if( total > client->rate ) {
		client->surpressCount++;
		client->message_size[sv.framenum % RATE_MESSAGES] = 0;
		return qtrue;
	}

	return qfalse;
}

void SV_CalcSendTime( client_t *client, int messageSize ) {
	int delta;

	if( messageSize == -1 ) {
		return;
	}

	// never drop over the loopback
	if( !client->rate ) {
		client->sendTime = 0;
		return;
	}

	client->message_size[sv.framenum % RATE_MESSAGES] = messageSize;

	delta = messageSize * 1000 / client->rate;
	client->sendTime = svs.realtime + delta;
}

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/


/*
=================
SV_ClientPrintf

Sends text across to be displayed if the level passes.
NOT archived in MVD stream.
=================
*/
void SV_ClientPrintf( client_t *client, int level, const char *fmt, ... ) {
	va_list		argptr;
	char		string[MAX_STRING_CHARS];
	
	if( level < client->messagelevel )
		return;
	
	va_start( argptr, fmt );
	Q_vsnprintf( string, sizeof( string ), fmt, argptr );
	va_end( argptr );

	MSG_WriteByte( svc_print );
	MSG_WriteByte( level );
	MSG_WriteString( string );

	SV_ClientAddMessage( client, MSG_RELIABLE|MSG_CLEAR );
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients, including MVD clients.
NOT archived in MVD stream.
=================
*/
void SV_BroadcastPrintf( int level, const char *fmt, ... ) {
	va_list		argptr;
	char		string[MAX_STRING_CHARS];
	client_t	*client;
	int			i;

	va_start( argptr, fmt );
	Q_vsnprintf( string, sizeof( string ), fmt, argptr );
	va_end( argptr );

	MSG_WriteByte( svc_print );
	MSG_WriteByte( level );
	MSG_WriteString( string );

	// echo to console
	if( dedicated->integer ) {
		// mask off high bits
		for( i = 0; string[i]; i++ )
			string[i] &= 127;
		Com_Printf( "%s", string );
	}

    FOR_EACH_CLIENT( client ) {
		if( client->state != cs_spawned )
			continue;
		if( level < client->messagelevel )
			continue;
		SV_ClientAddMessage( client, MSG_RELIABLE );
	}

	SZ_Clear( &msg_write );
}

/*
=================
SV_BroadcastCommand

Sends command to all active clients.
NOT archived in MVD stream.
=================
*/
void SV_BroadcastCommand( const char *fmt, ... ) {
	va_list		argptr;
	char		string[MAX_STRING_CHARS];
	client_t	*client;
	
	va_start( argptr, fmt );
	Q_vsnprintf( string, sizeof( string ), fmt, argptr );
	va_end( argptr );

	MSG_WriteByte( svc_stufftext );
	MSG_WriteString( string );

    FOR_EACH_CLIENT( client ) {
    	SV_ClientAddMessage( client, MSG_RELIABLE );
	}

	SZ_Clear( &msg_write );
}


/*
=================
SV_Multicast

Sends the contents of the write buffer to a subset of the clients,
then clears the write buffer.

Archived in MVD stream.

MULTICAST_ALL	same as broadcast (origin can be NULL)
MULTICAST_PVS	send to clients potentially visible from org
MULTICAST_PHS	send to clients potentially hearable from org
=================
*/
void SV_Multicast( vec3_t origin, multicast_t to ) {
	client_t	*client;
	byte		*mask;
	cleaf_t		*leaf;
    int         leafnum;
	int			cluster;
	int			area1, area2;
	int			flags;
	vec3_t		org;
	player_state_t	*ps;

	flags = 0;

	switch( to ) {
	case MULTICAST_ALL_R:
		flags |= MSG_RELIABLE;	// intentional fallthrough
	case MULTICAST_ALL:
		area1 = 0;
        leafnum = 0;
		cluster = 0;
		mask = NULL;
		break;
	case MULTICAST_PHS_R:
		flags |= MSG_RELIABLE;	// intentional fallthrough
	case MULTICAST_PHS:
		leaf = CM_PointLeaf( &sv.cm, origin );
        leafnum = leaf - sv.cm.cache->leafs;
		area1 = CM_LeafArea( leaf );
		cluster = CM_LeafCluster( leaf );
		mask = CM_ClusterPHS( &sv.cm, cluster );
		break;
	case MULTICAST_PVS_R:
		flags |= MSG_RELIABLE;	// intentional fallthrough
	case MULTICAST_PVS:
		leaf = CM_PointLeaf( &sv.cm, origin );
        leafnum = leaf - sv.cm.cache->leafs;
		area1 = CM_LeafArea( leaf );
		cluster = CM_LeafCluster( leaf );
		mask = CM_ClusterPVS( &sv.cm, cluster );
		break;
	default:
		Com_Error( ERR_DROP, "SV_Multicast: bad to: %i", to );
	}

	// send the data to all relevent clients
    FOR_EACH_CLIENT( client ) {
		if( client->state < cs_primed ) {
			continue;
		}
		// do not send unreliables to connecting clients
		if( !( flags & MSG_RELIABLE ) && ( client->state != cs_spawned ||
            client->download || client->nodata ) )
        {
			continue; 
		}

		if( mask ) {
			// find the client's PVS
			ps = &client->edict->client->ps;
			VectorMA( ps->viewoffset, 0.125f, ps->pmove.origin, org );
			leaf = CM_PointLeaf( &sv.cm, org );
			area2 = CM_LeafArea( leaf );
			if( !CM_AreasConnected( &sv.cm, area1, area2 ) ) {
				continue;
            }
			cluster = CM_LeafCluster( leaf );
			if( !Q_IsBitSet( mask, cluster ) ) {
				continue;
			}
		}

		SV_ClientAddMessage( client, flags );

	}

    if( svs.mvdummy ) {
	    SV_MvdMulticast( leafnum, mvd_multicast_all + to );
    }

	// clear the buffer 
	SZ_Clear( &msg_write );
}



/*
=======================
SV_ClientAddMessage

Adds contents of the current write buffer to client's message list.
Does NOT clean the buffer for multicast delivery purpose,
unless told otherwise.
=======================
*/
void SV_ClientAddMessage( client_t *client, int flags ) {
    if( sv_debug_send->integer > 1 ) {
        Com_Printf( S_COLOR_BLUE"%s: add%c: %d\n", client->name,
            ( flags & MSG_RELIABLE ) ? 'r' : 'u', msg_write.cursize );
    }

	if( !msg_write.cursize ) {
		return;
	}

    if( client->state > cs_zombie ) {
        client->AddMessage( client, msg_write.data, msg_write.cursize,
            ( flags & MSG_RELIABLE ) ? qtrue : qfalse );
    }

	if( flags & MSG_CLEAR ) {
		SZ_Clear( &msg_write );
	}
}

static inline void SV_PacketizedRemove( client_t *client, pmsg_t *msg ) {
	List_Delete( &msg->entry );

	if( msg->cursize > MSG_TRESHOLD ) {
		Z_Free( msg );
    } else {
        List_Append( &client->freemsg, &msg->entry );
    }
}

void SV_PacketizedClear( client_t *client ) {
	pmsg_t	*msg, *next;

    LIST_FOR_EACH_SAFE( pmsg_t, msg, next, &client->usedmsg, entry ) {
		SV_PacketizedRemove( client, msg );
	}

    LIST_FOR_EACH_SAFE( pmsg_t, msg, next, &client->relmsg, entry ) {
		SV_PacketizedRemove( client, msg );
	}
}


pmsg_t *SV_PacketizedAdd( client_t *client, byte *data,
							  int length, qboolean reliable )
{
	pmsg_t	*msg;

	if( length > MSG_TRESHOLD ) {
		msg = SV_Malloc( sizeof( *msg ) + length - MSG_TRESHOLD );
	} else {
        if( LIST_EMPTY( &client->freemsg ) ) {
            Com_WPrintf( "Out of message slots for %s!\n", client->name );
            if( reliable ) {
                SV_PacketizedClear( client );
                SV_DropClient( client, "no slot for reliable message" );
            }
            return NULL;
        }
        msg = LIST_FIRST( pmsg_t, &client->freemsg, entry );
    	List_Remove( &msg->entry );
	}

	memcpy( msg->data, data, length );
	msg->cursize = length;

    if( reliable ) {
    	List_Append( &client->relmsg, &msg->entry );
    } else {
    	List_Append( &client->usedmsg, &msg->entry );
    }

    return msg;
}

/*
===============================================================================

FRAME UPDATES - DEFAULT, R1Q2 AND Q2PRO CLIENTS (OLD NETCHAN)

===============================================================================
*/


void SV_OldClientAddMessage( client_t *client, byte *data,
							  int length, qboolean reliable )
{
	if( length > client->netchan->maxpacketlen ) {
		if( reliable ) {
			SV_DropClient( client, "oversize reliable message" );
		} else {
            Com_DPrintf( "Dumped oversize unreliable for %s\n", client->name );
        }
		return;
	}

    SV_PacketizedAdd( client, data, length, reliable );
}

/*
=======================
SV_OldClient_SendReliableMessages

This should be the only place data is
ever written to client->netchan.message
=======================
*/
void SV_OldClientWriteReliableMessages( client_t *client, int maxsize ) {
	pmsg_t	*msg, *next;
    int count;

	if( client->netchan->reliable_length ) {
		if( sv_debug_send->integer > 1 ) {
			Com_Printf( S_COLOR_BLUE"%s: unacked\n", client->name );
		}
		return;	// there is still outgoing reliable message pending
	}

	// find at least one reliable message to send
    count = 0;
    LIST_FOR_EACH_SAFE( pmsg_t, msg, next, &client->relmsg, entry ) {
		// stop if this msg doesn't fit (reliables must be delivered in order)
		if( client->netchan->message.cursize + msg->cursize > maxsize ) {
			if( !count ) {
				Com_WPrintf( "Overflow on the first reliable message "
					"for %s (should not happen).\n", client->name );
			}
			break;
		}

		if( sv_debug_send->integer > 1 ) {
			Com_Printf( S_COLOR_BLUE"%s: wrt%d: %d\n",
                client->name, count, msg->cursize );
		}

		SZ_Write( &client->netchan->message, msg->data, msg->cursize );
		SV_PacketizedRemove( client, msg );
        count++;
	}
}


/*
=======================
OldClient_SendDatagram
=======================
*/
void SV_OldClientWriteDatagram( client_t *client ) {
	pmsg_t	*msg, *next;
	int cursize, maxsize;

	maxsize = client->netchan->maxpacketlen;
	if( client->netchan->reliable_length ) {
		// there is still unacked reliable message pending
		maxsize -= client->netchan->reliable_length;
	} else {
		// find at least one reliable message to send
		// and make sure to reserve space for it
        if( !LIST_EMPTY( &client->relmsg ) ) {
            msg = LIST_FIRST( pmsg_t, &client->relmsg, entry );
            maxsize -= msg->cursize;
        }
	}

	// send over all the relevant entity_state_t
	// and the player_state_t
	client->WriteFrame( client );
	if( msg_write.cursize > maxsize ) {
		if( sv_debug_send->integer ) {
			Com_Printf( S_COLOR_BLUE"Frame overflowed for %s: %d > %d\n",
                client->name, msg_write.cursize, maxsize );
		}
		SZ_Clear( &msg_write );
	}

	// now try to write unreliable messages
	// it is necessary for this to be after the WriteEntities
	// so that entity references will be current
	if( msg_write.cursize + 4 <= maxsize ) {
		// temp entities first
        LIST_FOR_EACH_SAFE( pmsg_t, msg, next, &client->usedmsg, entry ) {
			if( msg->data[0] != svc_temp_entity ) {
				continue;
			}
			// ignore some low-priority effects, these checks come from r1q2
			if( msg->data[1] == TE_BLOOD || msg->data[1] == TE_SPLASH ) {
				continue;
			}
			if( msg->data[1] == TE_GUNSHOT || msg->data[1] == TE_BULLET_SPARKS
                    || msg->data[1] == TE_SHOTGUN )
            {
				continue;
			}

			// if this msg fits, write it
			if( msg_write.cursize + msg->cursize <= maxsize ) {
				MSG_WriteData( msg->data, msg->cursize );
			}

			SV_PacketizedRemove( client, msg );
		}

		if( msg_write.cursize + 4 <= maxsize ) {
			// then sounds
            LIST_FOR_EACH_SAFE( pmsg_t, msg, next, &client->usedmsg, entry ) {
				if( msg->data[0] != svc_sound ) {
					continue;
				}

				// if this msg fits, write it
				if( msg_write.cursize + msg->cursize <= maxsize ) {
					MSG_WriteData( msg->data, msg->cursize );
				}

				SV_PacketizedRemove( client, msg );
			}

			if( msg_write.cursize + 4 <= maxsize ) {
				// then everything else left
                LIST_FOR_EACH_SAFE( pmsg_t, msg, next, &client->usedmsg, entry ) {
					// if this msg fits, write it
					if( msg_write.cursize + msg->cursize <= maxsize ) {
						MSG_WriteData( msg->data, msg->cursize );
					}

					SV_PacketizedRemove( client, msg );
				}
			}
		}
	}
	
    // write at least one reliable message
	SV_OldClientWriteReliableMessages( client,
        client->netchan->maxpacketlen - msg_write.cursize );

	// send the datagram
	cursize = client->netchan->Transmit( client->netchan,
        msg_write.cursize, msg_write.data );

	// record the size for rate estimation
	SV_CalcSendTime( client, cursize );

	SZ_Clear( &msg_write );
}

void SV_OldClientFinishFrame( client_t *client ) {
	pmsg_t	*msg, *next;

	// clear all unreliable messages still left
    LIST_FOR_EACH_SAFE( pmsg_t, msg, next, &client->usedmsg, entry ) {
		SV_PacketizedRemove( client, msg );
	}
}

/*
===============================================================================

FRAME UPDATES - Q2PRO CLIENTS (NEW NETCHAN)

===============================================================================
*/

void SV_NewClientAddMessage( client_t *client, byte *data,
							  int length, qboolean reliable )
{
	sizebuf_t *buf = reliable ? &client->netchan->message : &client->datagram;
	SZ_Write( buf, data, length );
}

void SV_NewClientWriteDatagram( client_t *client ) {
	int cursize;
	int i;

	// send over all the relevant entity_state_t
	// and the player_state_t
	client->WriteFrame( client );

	if( msg_write.overflowed ) {
		// should never really happen
		Com_WPrintf( "Frame overflowed for %s\n", client->name );
		SZ_Clear( &msg_write );
	}

	// copy the accumulated multicast datagram
	// for this client out to the message
	// it is necessary for this to be after the WriteEntities
	// so that entity references will be current
	if( client->datagram.overflowed ) {
		Com_WPrintf( "Datagram overflowed for %s\n", client->name );
	} else if( msg_write.cursize + client->datagram.cursize > msg_write.maxsize ) {
		Com_WPrintf( "Dumping datagram for %s\n", client->name );
	} else {
		MSG_WriteData( client->datagram.data, client->datagram.cursize );
	}

	if( sv_pad_packets->integer ) {
        int pad = msg_write.cursize + sv_pad_packets->integer;

        if( pad > msg_write.maxsize ) {
            pad = msg_write.maxsize;
        }
	    for( i = msg_write.cursize; i < pad; i++ ) {
		    MSG_WriteByte( svc_nop );
	    }
    }

	// send the datagram
	cursize = client->netchan->Transmit( client->netchan,
        msg_write.cursize, msg_write.data );

	// record the size for rate estimation
	SV_CalcSendTime( client, cursize );

	// clear the write buffer
	SZ_Clear( &msg_write );
}

void SV_NewClientFinishFrame( client_t *client ) {
	SZ_Clear( &client->datagram );
}

/*
===============================================================================

COMMON STUFF

===============================================================================
*/

/*
=======================
SV_SendClientMessages

Called each game frame, sends svc_frame messages to spawned clients only.
Clients in earlier connection state are handled in SV_SendAsyncPackets.
=======================
*/
void SV_SendClientMessages( void ) {
	client_t	*client;
    int         msglen;

	// send a message to each connected client
    FOR_EACH_CLIENT( client ) {
		if( client->state != cs_spawned || client->download || client->nodata )
            goto finish;

		// if the reliable message overflowed,
		// drop the client (should never happen)
		if( client->netchan->message.overflowed ) {
			SZ_Clear( &client->netchan->message );
			SV_DropClient( client, "overflowed" );
		}

		// don't overrun bandwidth
		if( SV_RateDrop( client ) ) {
			if( sv_debug_send->integer ) {
				Com_Printf( "Frame %d surpressed for %s\n",
                    sv.framenum, client->name );
			}
			client->surpressCount++;
            goto finish;
		}

		// don't write any frame data until all fragments are sent
		if( client->netchan->fragment_pending ) {
			msglen = client->netchan->TransmitNextFragment( client->netchan );
			SV_CalcSendTime( client, msglen );
            goto finish;
		}

        // build the new frame
        client->BuildFrame( client );

        // write it
	    client->WriteDatagram( client );

finish:
        // clear the unreliable datagram
        client->FinishFrame( client );
	}

}

