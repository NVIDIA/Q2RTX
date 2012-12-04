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

/*
RW_IMP.C

This file contains ALL Win32 specific stuff having to do with the
software refresh.  When a port is being made the following functions
must be implemented by the port:

SWimp_EndFrame
SWimp_Init
SWimp_SetPalette
SWimp_Shutdown
*/

#include "client.h"

typedef struct {
    HDC         dibdc;      // DC compatible with DIB section
    HBITMAP     dibsect;    // DIB section
    byte        *pixels;    // DIB base pointer, NOT used directly for rendering!
    HGDIOBJ     prevobj;
} sww_t;

static sww_t sww;

/*
SWimp_Shutdown

System specific graphics subsystem shutdown routine.
Destroys DIB surfaces as appropriate.
*/
void VID_Shutdown(void)
{
    if (sww.dibdc) {
        SelectObject(sww.dibdc, sww.prevobj);
        DeleteDC(sww.dibdc);
    }

    if (sww.dibsect) {
        DeleteObject(sww.dibsect);
    }

    memset(&sww, 0, sizeof(sww));

    Win_Shutdown();
}

void SWimp_ModeChanged(void)
{
    BITMAPINFO info;

    if (!sww.dibdc) {
        return;
    }

    // destroy previous DIB section
    if (sww.dibsect) {
        SelectObject(sww.dibdc, sww.prevobj);
        DeleteObject(sww.dibsect);
    }

    // fill in the BITMAPINFO struct
    memset(&info, 0, sizeof(info));

    info.bmiHeader.biSize           = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth          = win.rc.width;
    info.bmiHeader.biHeight         = win.rc.height;
    info.bmiHeader.biPlanes         = 1;
    info.bmiHeader.biCompression    = BI_RGB;
    info.bmiHeader.biBitCount       = 32;

    // create the DIB section
    sww.dibsect = CreateDIBSection(win.dc,
                                   &info,
                                   DIB_RGB_COLORS,
                                   (void **)&sww.pixels,
                                   NULL,
                                   0);

    if (!sww.dibsect) {
        Com_Error(ERR_FATAL, "DIB_Init: CreateDIBSection failed");
    }

    if (info.bmiHeader.biHeight > 0) {
        // bottom up
        win.buffer  = sww.pixels + (win.rc.height - 1) * win.rc.width * 4;
        win.pitch   = -win.rc.width * 4;
    } else {
        // top down
        win.buffer  = sww.pixels;
        win.pitch   = win.rc.width * 4;
    }

    // clear the DIB memory buffer
    memset(sww.pixels, 0, win.rc.width * win.rc.height * 4);

    sww.prevobj = SelectObject(sww.dibdc, sww.dibsect);
    if (!sww.prevobj) {
        Com_Error(ERR_FATAL, "DIB_Init: SelectObject failed\n");
    }
}


/*
SWimp_Init

This routine is responsible for initializing the implementation
specific stuff in a software rendering subsystem.
*/
qboolean VID_Init(void)
{
    // create the window
    Win_Init();

    // set display mode
    Win_SetMode();

    // create logical DC
    sww.dibdc = CreateCompatibleDC(win.dc);
    if (!sww.dibdc) {
        Com_EPrintf("DIB_Init: CreateCompatibleDC failed with error %lu\n", GetLastError());
        Win_Shutdown();
        return qfalse;
    }

    // call SWimp_ModeChanged and friends
    Win_ModeChanged();
    return qtrue;
}

void VID_VideoWait(void)
{
}

qboolean VID_VideoSync(void)
{
    return qtrue;
}

void VID_BeginFrame(void)
{
}

/*
SWimp_EndFrame

This does an implementation specific copy from the backbuffer to the
front buffer.  In the Win32 case it uses BitBlt if we're using DIB sections/GDI.
*/
void VID_EndFrame(void)
{
    BitBlt(win.dc, 0, 0, win.rc.width, win.rc.height, sww.dibdc, 0, 0, SRCCOPY);
}

