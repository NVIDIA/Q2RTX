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

#include "com_local.h"
#include "key_public.h"
#include "in_public.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include <errno.h>

#include <SDL.h>

static cvar_t	*in_device;

static struct {
    qboolean initialized;
    qboolean grabbed;
    int      fd;
    int      dx, dy;
} evdev;

#define MAX_EVENTS    64
#define EVENT_SIZE    sizeof( struct input_event )

/*
===========
Evdev_RunMouse
===========
*/
static void Evdev_RunMouse( void ) {
	struct input_event ev[MAX_EVENTS];
	fd_set fdset;
	struct timeval timeout;
	int bytes, count;
	int dx, dy;
	int i, button;
    unsigned time;

	if( !evdev.initialized || !evdev.grabbed ) {
		return;
	}

    FD_ZERO( &fdset );
    FD_SET( evdev.fd, &fdset );
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    if( select( FD_SETSIZE, &fdset, NULL, NULL, &timeout ) == -1 ) {
        return;
    }
        
    if( !FD_ISSET( evdev.fd, &fdset ) ) {
        return;
    }

    bytes = read( evdev.fd, ev, EVENT_SIZE * MAX_EVENTS );
    if( bytes < EVENT_SIZE ) {
        return;
    }

    dx = dy = 0;
    count = bytes / EVENT_SIZE;
    for( i = 0 ; i < count; i++ ) {
        time = ev[i].time.tv_sec * 1000 + ev[i].time.tv_usec / 1000;
        switch( ev[i].type ) {
        case EV_KEY:
            if( ev[i].code >= BTN_MOUSE && ev[i].code < BTN_MOUSE + 8 ) {
                button = K_MOUSE1 + ev[i].code - BTN_MOUSE;
                if( ev[i].value ) {
                    Key_Event( button, qtrue, time );
                } else {
                    Key_Event( button, qfalse, time );
                }
            }
            break;
        case EV_REL: 
            switch( ev[i].code ) { 
            case REL_X: 
                evdev.dx += ( int )ev[i].value; 
                break; 
            case REL_Y: 
                evdev.dy += ( int )ev[i].value; 
                break; 
            case REL_WHEEL: 
                if( ( int )ev[i].value == 1 ) {
                    Key_Event( K_MWHEELUP, qtrue, time );
                    Key_Event( K_MWHEELUP, qfalse, time );
                } else if( ( int )ev[i].value == -1 ) {
                    Key_Event( K_MWHEELDOWN, qtrue, time );
                    Key_Event( K_MWHEELDOWN, qfalse, time );
                }
                break;
            default:
                break;
            }
        default:
            break;
        }
    }
}

static qboolean Evdev_GetMouseMotion( int *dx, int *dy ) {
	if( !evdev.initialized || !evdev.grabbed ) {
		return qfalse;
	}
    *dx = evdev.dx;
    *dy = evdev.dy;
    evdev.dx = 0;
    evdev.dy = 0;
    return qtrue;
}

/*
===========
Evdev_ShutdownMouse
===========
*/
static void Evdev_ShutdownMouse( void ) {
    if( !evdev.initialized ) {
        return;
    }
    close( evdev.fd );
    memset( &evdev, 0, sizeof( evdev ) );
}

/*
===========
Evdev_StartupMouse
===========
*/
static qboolean Evdev_InitMouse( void ) {
	in_device = Cvar_Get( "in_device", "/dev/input/event2", CVAR_LATCH );
    
    evdev.fd = open( in_device->string, O_RDONLY );
	if( evdev.fd == -1 ) {
        Com_EPrintf( "Couldn't open %s: %s\n", in_device->string,
            strerror( errno ) );
        return qfalse;
    }
    
    fcntl( evdev.fd, F_SETFL, fcntl( evdev.fd, F_GETFL, 0 ) | FNDELAY );

    Com_Printf( "Event interface initialized.\n" );
	evdev.initialized = qtrue;

	return qtrue;
}

/*
===========
Evdev_GrabMouse
===========
*/
static void Evdev_GrabMouse( grab_t grab ) {
	if( !evdev.initialized ) {
		return;
	}

	if( evdev.grabbed == grab ) {
		return;
	}
    
#ifdef EVIOCGRAB
	if( ioctl( evdev.fd, EVIOCGRAB, active ) == -1 ) {
        Com_EPrintf( "Grab/Release failed: %s\n", strerror( errno ) );
    }
#endif // EVIOCGRAB

    if( grab ) {
        struct input_event ev;
        
	    SDL_ShowCursor( SDL_DISABLE );
        
        while( read( evdev.fd, &ev, EVENT_SIZE ) == EVENT_SIZE )
            ;
    } else {
	    SDL_ShowCursor( SDL_ENABLE );
    }

    evdev.dx = 0;
    evdev.dy = 0;
	evdev.grabbed = grab;
}

/*
@@@@@@@@@@@@@@@@@@@
DI_FillAPI
@@@@@@@@@@@@@@@@@@@
*/
void DI_FillAPI( inputAPI_t *api ) {
	api->Init = Evdev_InitMouse;
	api->Shutdown = Evdev_ShutdownMouse;
	api->Grab = Evdev_GrabMouse;
	api->GetEvents = Evdev_GetMouseEvents;
	api->GetMotion = Evdev_GetMouseMotion;
}


