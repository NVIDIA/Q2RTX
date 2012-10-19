/*
Copyright (C) 2003-2012 Andrey Nazarov

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

#include "video.h"

#if USE_X11
#include <SDL_syswm.h>
#include <X11/Xutil.h>
#endif

/*
=================
VID_GetClipboardData
=================
*/
char *VID_GetClipboardData(void)
{
#if USE_X11
    SDL_SysWMinfo info;
    Display *dpy;
    Window win, sowner;
    Atom type, property;
    unsigned long len, bytes_left;
    unsigned char *data;
    int format, result;
    char *ret;

    SDL_VERSION(&info.version);
    if (!SDL_GetWMInfo(&info)) {
        return NULL;
    }

    if (info.subsystem != SDL_SYSWM_X11) {
        return NULL;
    }

    dpy = info.info.x11.display;
    win = info.info.x11.window;

    if (!dpy) {
        return NULL;
    }

    sowner = XGetSelectionOwner(dpy, XA_PRIMARY);
    if (sowner == None) {
        return NULL;
    }

    property = XInternAtom(dpy, "GETCLIPBOARDDATA_PROP", False);

    XConvertSelection(dpy, XA_PRIMARY, XA_STRING, property, win, CurrentTime);

    XSync(dpy, False);

    result = XGetWindowProperty(dpy, win, property, 0, 0, False,
                                AnyPropertyType, &type, &format, &len, &bytes_left, &data);

    if (result != Success) {
        return NULL;
    }

    ret = NULL;
    if (bytes_left) {
        result = XGetWindowProperty(dpy, win, property, 0, bytes_left, True,
                                    AnyPropertyType, &type, &format, &len, &bytes_left, &data);
        if (result == Success) {
            ret = Z_CopyString((char *)data);
        }
    }

    XFree(data);

    return ret;
#else
    return NULL;
#endif
}

/*
=================
VID_SetClipboardData
=================
*/
void VID_SetClipboardData(const char *data)
{
}

