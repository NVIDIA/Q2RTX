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

#include "common.h"
#include "key_public.h"
#include "in_public.h"
#include "cl_public.h"
#include "io_sleep.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

#include <SDL.h>

static cvar_t   *in_device;

static struct {
    qboolean initialized;
    qboolean grabbed;
    int      fd;
    int      dx, dy;
    ioentry_t *io;
} evdev;

#define MAX_EVENTS    64
#define EVENT_SIZE    sizeof( struct input_event )

static void GetMouseEvents( void ) {
    struct input_event ev[MAX_EVENTS];
    ssize_t i, bytes, count;
    unsigned button, time;

    if( !evdev.initialized || !evdev.grabbed || !evdev.io->canread ) {
        return;
    }

    bytes = read( evdev.fd, ev, EVENT_SIZE * MAX_EVENTS );
    if( bytes < ( ssize_t )EVENT_SIZE ) {
        return;
    }

    count = bytes / EVENT_SIZE;
    for( i = 0 ; i < count; i++ ) {
        time = ev[i].time.tv_sec * 1000 + ev[i].time.tv_usec / 1000;
        switch( ev[i].type ) {
        case EV_KEY:
            if( ev[i].code >= BTN_MOUSE && ev[i].code < BTN_MOUSE + 8 ) {
                button = K_MOUSE1 + ev[i].code - BTN_MOUSE;
                Key_Event( button, !!ev[i].value, time );
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
            case REL_HWHEEL:
                if( ( int )ev[i].value == 1 ) {
                    Key_Event( K_MWHEELRIGHT, qtrue, time );
                    Key_Event( K_MWHEELRIGHT, qfalse, time );
                } else if( ( int )ev[i].value == -1 ) {
                    Key_Event( K_MWHEELLEFT, qtrue, time );
                    Key_Event( K_MWHEELLEFT, qfalse, time );
                }
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
}

static qboolean GetMouseMotion( int *dx, int *dy ) {
    if( !evdev.initialized || !evdev.grabbed ) {
        return qfalse;
    }
    *dx = evdev.dx;
    *dy = evdev.dy;
    evdev.dx = 0;
    evdev.dy = 0;
    return qtrue;
}

static void ShutdownMouse( void ) {
    if( !evdev.initialized ) {
        return;
    }
    IO_Remove( evdev.fd );
    close( evdev.fd );
    memset( &evdev, 0, sizeof( evdev ) );
}

static qboolean InitMouse( void ) {
    in_device = Cvar_Get( "in_device", "", 0 );
    if( !in_device->string[0] ) {
        Com_WPrintf( "No evdev input device specified.\n" );
        return qfalse;
    }
 
    evdev.fd = open( in_device->string, O_RDONLY );
    if( evdev.fd == -1 ) {
        Com_EPrintf( "Couldn't open %s: %s\n", in_device->string,
            strerror( errno ) );
        return qfalse;
    }
 
    fcntl( evdev.fd, F_SETFL, fcntl( evdev.fd, F_GETFL, 0 ) | FNDELAY );
    evdev.io = IO_Add( evdev.fd );

    Com_Printf( "Evdev interface initialized.\n" );
    evdev.initialized = qtrue;

    return qtrue;
}

static void GrabMouse( grab_t grab ) {
    if( !evdev.initialized ) {
        return;
    }

    if( evdev.grabbed == grab ) {
        evdev.dx = 0;
        evdev.dy = 0;
        return;
    }
    
#if 0//def EVIOCGRAB
    if( ioctl( evdev.fd, EVIOCGRAB, active ) == -1 ) {
        Com_EPrintf( "Grab/Release failed: %s\n", strerror( errno ) );
    }
#endif // EVIOCGRAB

    if( grab == IN_GRAB ) {
        SDL_WM_GrabInput( SDL_GRAB_ON );
        SDL_WM_SetCaption( "[" PRODUCT "]", APPLICATION );
        SDL_ShowCursor( SDL_DISABLE );
        evdev.io->wantread = qtrue;
    } else {
        if( evdev.grabbed == IN_GRAB ) {
            SDL_WM_GrabInput( SDL_GRAB_OFF );
            SDL_WM_SetCaption( PRODUCT, APPLICATION );
        }
        if( grab == IN_HIDE ) {
            SDL_ShowCursor( SDL_DISABLE );
        } else {
            SDL_ShowCursor( SDL_ENABLE );
        }
        evdev.io->wantread = !!grab;
    }

    // pump pending events
    if( grab ) {
        struct input_event ev;

        while( read( evdev.fd, &ev, EVENT_SIZE ) == EVENT_SIZE )
            ;
    }

    evdev.dx = 0;
    evdev.dy = 0;
    evdev.grabbed = grab;
}

static void WarpMouse( int x, int y ) {
    SDL_WarpMouse( x, y );
}

/*
@@@@@@@@@@@@@@@@@@@
DI_FillAPI
@@@@@@@@@@@@@@@@@@@
*/
void DI_FillAPI( inputAPI_t *api ) {
    api->Init = InitMouse;
    api->Shutdown = ShutdownMouse;
    api->Grab = GrabMouse;
    api->Warp = WarpMouse;
    api->GetEvents = GetMouseEvents;
    api->GetMotion = GetMouseMotion;
}


