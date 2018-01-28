/*
Copyright (C) 1997-2001 Id Software, Inc.

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

#include "shared/shared.h"
#include "shared/list.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/zone.h"
#include "client/keys.h"
#include "client/input.h"
#include "client/client.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

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
    char        *path;
    char        *name;
    int         fd;
} evdev_t;

static struct {
    qboolean    initialized;
    qboolean    grabbed;
    list_t      devices;
    int         dx, dy;
} evdev;

#define MAX_EVENTS    64
#define EVENT_SIZE    sizeof(struct input_event)

static void evdev_remove(evdev_t *dev)
{
    Com_DPrintf("Removing %s [%s]\n", dev->path, dev->name);

    close(dev->fd);
    Z_Free(dev->path);
    Z_Free(dev->name);
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
        if (errno == EAGAIN) {
            return;
        }
        Com_EPrintf("Couldn't read %s: %s\n", dev->path, strerror(errno));
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

    Cmd_RemoveCommand("evdevlist");

    memset(&evdev, 0, sizeof(evdev));
}

static evdev_t *evdev_add(const char *path)
{
    char buffer[MAX_QPATH];
    evdev_t *dev;
    int fd, ret;

    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd == -1) {
        Com_EPrintf("Couldn't open %s: %s\n", path, strerror(errno));
        return NULL;
    }

    ret = ioctl(fd, EVIOCGNAME(MAX_QPATH), buffer);
    if (ret > 0) {
        if (ret > MAX_QPATH)
            ret = MAX_QPATH;
        buffer[ret - 1] = 0;
    } else {
        strcpy(buffer, "Unknown");
    }

    Com_DPrintf("Adding %s [%s]\n", path, buffer);

    dev = Z_Malloc(sizeof(*dev));
    dev->path = Z_CopyString(path);
    dev->name = Z_CopyString(buffer);
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

static void ListDevices_f(void)
{
    evdev_t *dev;

    if (LIST_EMPTY(&evdev.devices)) {
        Com_Printf("No input devices.\n");
        return;
    }

    FOR_EACH_EVDEV(dev) {
        Com_Printf("%s [%s]\n", dev->path, dev->name);
    }
}

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

    Cmd_AddCommand("evdevlist", ListDevices_f);

    Com_Printf("Evdev mouse initialized.\n");
    evdev.initialized = qtrue;

    return qtrue;
}

static void GrabMouse(qboolean grab)
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

#if USE_SDL
    if (grab) {
        SDL_WM_GrabInput(SDL_GRAB_ON);
        SDL_WM_SetCaption("[" PRODUCT "]", APPLICATION);
        SDL_ShowCursor(SDL_DISABLE);
    } else {
        SDL_WM_GrabInput(SDL_GRAB_OFF);
        SDL_WM_SetCaption(PRODUCT, APPLICATION);
        SDL_ShowCursor(SDL_ENABLE);
    }
#endif

    if (grab) {
        // pump pending events
        FOR_EACH_EVDEV(dev) {
            while (read(dev->fd, &ev, EVENT_SIZE) == EVENT_SIZE)
                ;
        }
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

