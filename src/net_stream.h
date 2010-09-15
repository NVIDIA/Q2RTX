/*
Copyright (C) 2003-2008 Andrey Nazarov

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

typedef enum netstate_e {
    NS_DISCONNECTED,// no socket opened
    NS_CONNECTING,  // connect() not yet completed
    NS_CONNECTED,   // may transmit data
    NS_CLOSED,      // peer has preformed orderly shutdown
    NS_BROKEN       // fatal error has been signaled
} netstate_t;

typedef struct netstream_s {
    int         socket;
    netadr_t    address;
    netstate_t  state;
    fifo_t      recv;
    fifo_t      send;
} netstream_t;

void NET_Close( netstream_t *s );
neterr_t NET_Listen( qboolean listen );
neterr_t NET_Accept( netstream_t *s );
neterr_t NET_Connect( const netadr_t *peer, netstream_t *s );
neterr_t NET_RunConnect( netstream_t *s );
neterr_t NET_RunStream( netstream_t *s );
void NET_UpdateStream( netstream_t *s );
