/*
Copyright (C) 2003-2008 Andrey Nazarov

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#pragma once

#define GTV_PROTOCOL_VERSION 0xED04

#define MAX_GTS_MSGLEN  MAX_MSGLEN  // maximum GTV server message length
#define MAX_GTC_MSGLEN  256         // maximum GTV client message length

// flags used in hello packet
#define GTF_DEFLATE     1
#define GTF_STRINGCMDS  2

typedef enum {
    GTS_HELLO,
    GTS_PONG,
    GTS_STREAM_START,
    GTS_STREAM_STOP,
    GTS_STREAM_DATA,
    GTS_ERROR,
    GTS_BADREQUEST,
    GTS_NOACCESS,
    GTS_DISCONNECT,
    GTS_RECONNECT
} gtv_serverop_t;

typedef enum {
    GTC_HELLO,
    GTC_PING,
    GTC_STREAM_START,
    GTC_STREAM_STOP,
    GTC_STRINGCMD
} gtv_clientop_t;

