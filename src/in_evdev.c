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
#include "q_list.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

#if USE_SDL
#include <SDL.h>
#endif

#if USE_UDEV
#include <libudev.h>
#endif

#define FOR_EACH_EVDEV(dev) \
    LIST_FOR_EACH(evdev_t, dev, &evdev.devices, entry)
#define FOR_EACH_EVDEV_SAFE(dev, next) \
    LIST_FOR_EACH_SAFE(evdev_t, dev, next, &evdev.devices, entry)

typedef struct {
    list_t      entry;
    int         fd;
} evdev_t;

static struct {
    qboolean    initialized;
    grab_t      grabbed;
    list_t      devices;
    int         dx, dy;
} evdev;

#define MAX_EVENTS    64
#define EVENT_SIZE    sizeof(struct input_event)

static void evdev_remove(evdev_t *dev)
{
    close(dev->fd);
    List_Remove(&dev->entry);
    Z_Free(dev);
}

static void evdev_read(evdev_t *dev)
{
    struct input_event ev[MAX_EVENTS];
    ssize_t bytes;
    size_t i, count;
    unsigned button, time;

    bytes = read(dev->fd, ev, EVENT_SIZE * MAX_EVENTS);
    if (bytes == -1) {
        if (errno == EAGAIN || errno == EINTR) {
            return;
        }
        Com_EPrintf("Couldn't read event: %s\n", strerror(errno));
        evdev_remove(dev);
        return;
    }

    if (bytes < EVENT_SIZE) {
        return; // should not happen
    }

    count = bytes / EVENT_SIZE;
    for (i = 0; i < count; i++) {
        time = ev[i].time.tv_sec * 1000 + ev[i].time.tv_usec / 1000;
        switch (ev[i].type) {
        case EV_KEY:
            if (ev[i].code >= BTN_MOUSE && ev[i].code < BTN_MOUSE + 8) {
                button = K_MOUSE1 + ev[i].code - BTN_MOUSE;
                Key_Event(button, !!ev[i].value, time);
            }
            break;
        case EV_REL:
            switch (ev[i].code) {
            case REL_X:
                evdev.dx += (int)ev[i].value;
                break;
            case REL_Y:
                evdev.dy += (int)ev[i].value;
                break;
            case REL_WHEEL:
                if ((int)ev[i].value == 1) {
                    Key_Event(K_MWHEELUP, qtrue, time);
                    Key_Event(K_MWHEELUP, qfalse, time);
                } else if ((int)ev[i].value == -1) {
                    Key_Event(K_MWHEELDOWN, qtrue, time);
                    Key_Event(K_MWHEELDOWN, qfalse, time);
                }
                break;
            case REL_HWHEEL:
                if ((int)ev[i].value == 1) {
                    Key_Event(K_MWHEELRIGHT, qtrue, time);
                    Key_Event(K_MWHEELRIGHT, qfalse, time);
                } else if ((int)ev[i].value == -1) {
                    Key_Event(K_MWHEELLEFT, qtrue, time);
                    Key_Event(K_MWHEELLEFT, qfalse, time);
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

static void GetMouseEvents(void)
{
    evdev_t *dev, *next;

    if (!evdev.initialized || !evdev.grabbed) {
        return;
    }

    FOR_EACH_EVDEV_SAFE(dev, next) {
        evdev_read(dev);
    }
}

static qboolean GetMouseMotion(int *dx, int *dy)
{
    if (!evdev.initialized || !evdev.grabbed) {
        return qfalse;
    }

    *dx = evdev.dx;
    *dy = evdev.dy;
    evdev.dx = 0;
    evdev.dy = 0;
    return qtrue;
}

static void ShutdownMouse(void)
{
    evdev_t *dev, *next;

    if (!evdev.initialized) {
        return;
    }

    FOR_EACH_EVDEV_SAFE(dev, next) {
        evdev_remove(dev);
    }

#if USE_SDL
    SDL_ShowCursor(SDL_ENABLE);
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    SDL_WM_SetCaption(PRODUCT, APPLICATION);
#endif

    memset(&evdev, 0, sizeof(evdev));
}

static evdev_t *evdev_add(const char *path)
{
    evdev_t *dev;
    int fd;

    fd = open(path, O_RDONLY);
    if (fd == -1) {
        Com_EPrintf("Couldn't open %s: %s\n", path, strerror(errno));
        return NULL;
    }

    Com_DPrintf("Adding device %s\n", path);

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | FNDELAY);

    dev = Z_Malloc(sizeof(*dev));
    dev->fd = fd;
    List_Append(&evdev.devices, &dev->entry);

    return dev;
}

#if USE_UDEV
static void auto_add_devices(void)
{
    struct udev *udev;
    struct udev_enumerate *enumerate;
    struct udev_list_entry *list, *entry;
    struct udev_device *dev;
    const char *path;

    udev = udev_new();
    if (!udev) {
        Com_EPrintf("Couldn't create udev handle\n");
        return;
    }

    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_add_match_sysname(enumerate, "event[0-9]*");
    udev_enumerate_add_match_property(enumerate, "ID_INPUT_MOUSE", "1");
    udev_enumerate_scan_devices(enumerate);
    list = udev_enumerate_get_list_entry(enumerate);
    udev_list_entry_foreach(entry, list) {
        path = udev_list_entry_get_name(entry);
        dev = udev_device_new_from_syspath(udev, path);
        if (dev) {
            path = udev_device_get_devnode(dev);
            if (path) {
                evdev_add(path);
            }
            udev_device_unref(dev);
        }
    }

    udev_enumerate_unref(enumerate);
    udev_unref(udev);
}
#endif

static qboolean InitMouse(void)
{
    cvar_t *var;

    List_Init(&evdev.devices);

    var = Cvar_Get("in_device", "", 0);

    if (var->string[0]) {
        // add user specified device
        if (!evdev_add(var->string)) {
            return qfalse;
        }
    } else {
#if USE_UDEV
        // find devices automatically
        auto_add_devices();

        if (LIST_EMPTY(&evdev.devices)) {
            Com_EPrintf(
                "Automatic libudev scan was unable to find any "
                "usable mouse device. You may need to set the "
                "'in_device' variable manually before direct "
                "mouse input can be used.\n");
            return qfalse;
        }
#else
        Com_EPrintf(
            "No input device specified, and libudev support "
            "is not compiled in. Please set the 'in_device' variable "
            "manually before direct mouse input can be used.\n");
        return qfalse;
#endif
    }

    Com_Printf("Evdev mouse initialized.\n");
    evdev.initialized = qtrue;

    return qtrue;
}

static void GrabMouse(grab_t grab)
{
    struct input_event ev;
    evdev_t *dev;

    if (!evdev.initialized) {
        return;
    }

    if (evdev.grabbed == grab) {
        evdev.dx = 0;
        evdev.dy = 0;
        return;
    }

    if (grab == IN_GRAB) {
#if USE_SDL
        SDL_WM_GrabInput(SDL_GRAB_ON);
        SDL_WM_SetCaption("[" PRODUCT "]", APPLICATION);
        SDL_ShowCursor(SDL_DISABLE);
#endif
    } else {
#if USE_SDL
        if (evdev.grabbed == IN_GRAB) {
            SDL_WM_GrabInput(SDL_GRAB_OFF);
            SDL_WM_SetCaption(PRODUCT, APPLICATION);
        }
        if (grab == IN_HIDE) {
            SDL_ShowCursor(SDL_DISABLE);
        } else {
            SDL_ShowCursor(SDL_ENABLE);
        }
#endif
    }

    FOR_EACH_EVDEV(dev) {
        if (!grab) {
            continue;
        }

        // pump pending events
        while (read(dev->fd, &ev, EVENT_SIZE) == EVENT_SIZE)
            ;
    }

    evdev.dx = 0;
    evdev.dy = 0;
    evdev.grabbed = grab;
}

static void WarpMouse(int x, int y)
{
#if USE_SDL
    SDL_WarpMouse(x, y);
#endif
}

/*
@@@@@@@@@@@@@@@@@@@
DI_FillAPI
@@@@@@@@@@@@@@@@@@@
*/
void DI_FillAPI(inputAPI_t *api)
{
    api->Init = InitMouse;
    api->Shutdown = ShutdownMouse;
    api->Grab = GrabMouse;
    api->Warp = WarpMouse;
    api->GetEvents = GetMouseEvents;
    api->GetMotion = GetMouseMotion;
}


