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

#include "com_local.h"
#include "files.h"
#include "sys_public.h"
#include "io_sleep.h"
#ifdef _WIN32
#error not yet implemented
#else
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#endif

static ioentry_t    entries[FD_SETSIZE];
static int          numfds;

#define CHECK_FD \
    if( fd < 0 || fd >= FD_SETSIZE ) { \
        Com_Error( ERR_FATAL, "%s: fd out of range", __func__ ); \
    }

ioentry_t *IO_Add( int fd ) {
    ioentry_t *e;

    CHECK_FD

    if( fd >= numfds ) {
        numfds = fd + 1;
    }

    e = &entries[fd];
    e->inuse = qtrue;
    return e;
}

void IO_Remove( int fd ) {
    ioentry_t *e;
    int i;

    CHECK_FD

    if( fd == numfds - 1 ) {
        for( i = fd - 1; i >= 0; i-- ) {
            e = &entries[i];
            if( e->inuse ) {
                break;
            }
        }
        numfds = i + 1;
    }

    e = &entries[fd];
    memset( e, 0, sizeof( *e ) );
}

ioentry_t *IO_Get( int fd ) {
    ioentry_t *e;

    CHECK_FD

    e = &entries[fd];
    return e;
}

//void IO_Mask( int fd ) {
//}

/*
====================
IO_Sleep

Sleeps msec or until some file descriptor is ready
====================
*/
int IO_Sleep( int msec ) {
    struct timeval timeout;
    fd_set rfd, wfd;
#ifdef _WIN32
    fd_set efd;
#endif
    ioentry_t *e;
    int i, ret;

    if( !numfds ) {
        // don't bother with select()
        Sys_Sleep( msec );
        return 0;
    }

    FD_ZERO( &rfd );
    FD_ZERO( &wfd );
#ifdef _WIN32
    FD_ZERO( &efd );
#endif

    for( i = 0; i < numfds; i++ ) {
        e = &entries[i];
        if( !e->inuse ) {
            continue;
        }
        e->canread = qfalse;
        if( e->wantread ) {
            FD_SET( i, &rfd );
        }
        e->canwrite = qfalse;
        if( e->wantwrite ) {
            FD_SET( i, &wfd );
        }
#ifdef _WIN32
        e->canexcept = qfalse;
        if( e->wantexcept ) {
            FD_SET( i, &efd );
        }
#endif
    }

    timeout.tv_sec = msec / 1000;
    timeout.tv_usec = ( msec % 1000 ) * 1000;

    ret = select( numfds, &rfd, &wfd,
#ifdef _WIN32
        &efd,
#else
        NULL,
#endif
        &timeout );
    if( ret == -1 ) {
        Com_EPrintf( "%s: %s\n", __func__, strerror( errno ) );
        return ret;
    }

    if( !ret ) {
        return ret;
    }

    for( i = 0; i < numfds; i++ ) {
        e = &entries[i];
        if( !e->inuse ) {
            continue;
        }
        if( e->wantread && FD_ISSET( i, &rfd ) ) {
            e->canread = qtrue;
        }
        if( e->wantwrite && FD_ISSET( i, &wfd ) ) {
            e->canwrite = qtrue;
        }
#ifdef _WIN32
        if( e->wantexcept && FD_ISSET( i, &efd ) ) {
            e->canexcept = qtrue;
        }
#endif
    }

    return ret;
}

#if USE_AC_SERVER

/*
====================
IO_Sleep

Sleeps msec or until some file descriptor from a given subset is ready
====================
*/
int IO_Sleepv( int msec, ... ) {
    va_list argptr;
    struct timeval timeout;
    fd_set rfd, wfd;
#ifdef _WIN32
    fd_set efd;
#endif
    ioentry_t *e;
    int i, ret;

    if( !numfds ) {
        // don't bother with select()
        Sys_Sleep( msec );
        return 0;
    }

    FD_ZERO( &rfd );
    FD_ZERO( &wfd );
#ifdef _WIN32
    FD_ZERO( &efd );
#endif

    va_start( argptr, msec );
    while( 1 ) {
        i = va_arg( argptr, int );
        if( i == -1 ) {
            break;
        }
        e = &entries[i];
        if( !e->inuse ) {
            continue;
        }
        e->canread = qfalse;
        if( e->wantread ) {
            FD_SET( i, &rfd );
        }
        e->canwrite = qfalse;
        if( e->wantwrite ) {
            FD_SET( i, &wfd );
        }
#ifdef _WIN32
        e->canexcept = qfalse;
        if( e->wantexcept ) {
            FD_SET( i, &efd );
        }
#endif
    }
    va_end( argptr );

    timeout.tv_sec = msec / 1000;
    timeout.tv_usec = ( msec % 1000 ) * 1000;

    ret = select( numfds, &rfd, &wfd,
#ifdef _WIN32
        &efd,
#else
        NULL,
#endif
        &timeout );
    if( ret == -1 ) {
        Com_EPrintf( "%s: %s\n", __func__, strerror( errno ) );
        return ret;
    }

    if( !ret ) {
        return ret;
    }

    va_start( argptr, msec );
    while( 1 ) {
        i = va_arg( argptr, int );
        if( i == -1 ) {
            break;
        }
        e = &entries[i];
        if( !e->inuse ) {
            continue;
        }
        if( e->wantread && FD_ISSET( i, &rfd ) ) {
            e->canread = qtrue;
        }
        if( e->wantwrite && FD_ISSET( i, &wfd ) ) {
            e->canwrite = qtrue;
        }
#ifdef _WIN32
        if( e->wantexcept && FD_ISSET( i, &efd ) ) {
            e->canexcept = qtrue;
        }
#endif
    }
    va_end( argptr );

    return ret;
}

#endif // USE_AC_SERVER

