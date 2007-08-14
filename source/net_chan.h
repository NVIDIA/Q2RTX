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
// net_chan.h

typedef struct netchan_old_s {
	netchan_t	pub;

// sequencing variables
	int			incoming_reliable_acknowledged;	// single bit
	int			incoming_reliable_sequence;		// single bit, maintained local
	int			reliable_sequence;			// single bit
	int			last_reliable_sequence;		// sequence number of last send

	byte		*message_buf;		// leave space for header

// message is copied to this buffer when it is first transfered
	byte		*reliable_buf;	// unacked reliable message
} netchan_old_t;

typedef struct netchan_new_s {
	netchan_t	pub;

// sequencing variables
	int			incoming_reliable_acknowledged;	// single bit
	int			incoming_reliable_sequence;		// single bit, maintained local
	int			reliable_sequence;			// single bit
	int			last_reliable_sequence;		// sequence number of last send
	int			fragment_sequence;

// reliable staging and holding areas
	byte		message_buf[MAX_MSGLEN];		// leave space for header

// message is copied to this buffer when it is first transfered
	byte		reliable_buf[MAX_MSGLEN];	// unacked reliable message

	sizebuf_t   fragment_in;
	byte		fragment_in_buf[MAX_MSGLEN];

	sizebuf_t	fragment_out;
	byte		fragment_out_buf[MAX_MSGLEN];
} netchan_new_t;

