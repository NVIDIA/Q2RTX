/*
Copyright (C) 2009 Andrey Nazarov

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

typedef struct {
#ifdef _WIN32
    int fd;
#endif
    qboolean inuse: 1;
    qboolean canread: 1;
    qboolean canwrite: 1;
#ifdef _WIN32
    qboolean canexcept: 1;
#endif
    qboolean wantread: 1;
    qboolean wantwrite: 1;
#ifdef _WIN32
    qboolean wantexcept: 1;
#endif
} ioentry_t;

ioentry_t *IO_Add( int fd );
void IO_Remove( int fd );
ioentry_t *IO_Get( int fd );
int IO_Sleep( int msec );
#if USE_AC_SERVER
int IO_Sleepv( int msec, ... );
#endif

