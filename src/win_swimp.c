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

#include "win_local.h"

#ifndef PC_NOCOLLAPSE
#define PC_NOCOLLAPSE   0
#endif

typedef struct {
    BITMAPINFOHEADER    header;
    RGBQUAD             colors[256];
} dibinfo_t;

typedef struct {
    WORD palVersion;
    WORD palNumEntries;
    PALETTEENTRY palEntries[256];
} identitypalette_t;

static const int s_syspalindices[] =  {
    COLOR_ACTIVEBORDER,
    COLOR_ACTIVECAPTION,
    COLOR_APPWORKSPACE,
    COLOR_BACKGROUND,
    COLOR_BTNFACE,
    COLOR_BTNSHADOW,
    COLOR_BTNTEXT,
    COLOR_CAPTIONTEXT,
    COLOR_GRAYTEXT,
    COLOR_HIGHLIGHT,
    COLOR_HIGHLIGHTTEXT,
    COLOR_INACTIVEBORDER,

    COLOR_INACTIVECAPTION,
    COLOR_MENU,
    COLOR_MENUTEXT,
    COLOR_SCROLLBAR,
    COLOR_WINDOW,
    COLOR_WINDOWFRAME,
    COLOR_WINDOWTEXT
};

#define NUM_SYS_COLORS (sizeof(s_syspalindices) / sizeof(s_syspalindices[0]))

typedef struct {
    HDC         dibdc;      // DC compatible with DIB section
    HBITMAP     dibsect;    // DIB section
    byte        *pixels;    // DIB base pointer, NOT used directly for rendering!

    qboolean        palettized;         // qtrue if desktop is paletted
    HPALETTE        pal;                // palette we're using
    HPALETTE        oldpal;         // original system palette
    COLORREF        oldsyscolors[NUM_SYS_COLORS];   // original system colors

    HGDIOBJ         prevobj;
} sww_t;

static sww_t sww;

/*
SWimp_Shutdown

System specific graphics subsystem shutdown routine.
Destroys DIB surfaces as appropriate.
*/
void VID_Shutdown(void)
{
    if (sww.palettized) {
        SetSystemPaletteUse(win.dc, SYSPAL_STATIC);
        SetSysColors(NUM_SYS_COLORS, s_syspalindices, sww.oldsyscolors);
    }

    if (sww.pal) {
        DeleteObject(sww.pal);
    }

    if (sww.oldpal) {
        SelectPalette(win.dc, sww.oldpal, FALSE);
        RealizePalette(win.dc);
    }

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
    dibinfo_t   info;
    BITMAPINFO *pbmiDIB = (BITMAPINFO *)&info;

    if (!sww.dibdc) {
        return;
    }

    // destroy previous DIB section
    if (sww.dibsect) {
        SelectObject(sww.dibdc, sww.prevobj);
        DeleteObject(sww.dibsect);
    }

    // fill in the BITMAPINFO struct
    memset(pbmiDIB, 0, sizeof(dibinfo_t));

    pbmiDIB->bmiHeader.biSize          = sizeof(BITMAPINFOHEADER);
    pbmiDIB->bmiHeader.biWidth         = win.rc.width;
    pbmiDIB->bmiHeader.biHeight        = win.rc.height;
    pbmiDIB->bmiHeader.biPlanes        = 1;
    pbmiDIB->bmiHeader.biCompression   = BI_RGB;
    pbmiDIB->bmiHeader.biBitCount      = 8;
    pbmiDIB->bmiHeader.biClrUsed       = 256;
    pbmiDIB->bmiHeader.biClrImportant  = 256;

    // create the DIB section
    sww.dibsect = CreateDIBSection(win.dc,
                                   pbmiDIB,
                                   DIB_RGB_COLORS,
                                   (void **)&sww.pixels,
                                   NULL,
                                   0);

    if (!sww.dibsect) {
        Com_Error(ERR_FATAL, "DIB_Init: CreateDIBSection failed");
    }

    if (pbmiDIB->bmiHeader.biHeight > 0) {
        // bottom up
        win.buffer  = sww.pixels + (win.rc.height - 1) * win.rc.width;
        win.pitch   = -win.rc.width;
    } else {
        // top down
        win.buffer  = sww.pixels;
        win.pitch   = win.rc.width;
    }

    // clear the DIB memory buffer
    memset(sww.pixels, 0xff, win.rc.width * win.rc.height);

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
    int i;

    // create the window
    Win_Init();

    // set display mode
    Win_SetMode();

    // figure out if we're running in an 8-bit display mode
    if (GetDeviceCaps(win.dc, RASTERCAPS) & RC_PALETTE) {
        sww.palettized = qtrue;
        for (i = 0; i < NUM_SYS_COLORS; i++)
            sww.oldsyscolors[i] = GetSysColor(s_syspalindices[i]);
    } else {
        sww.palettized = qfalse;
    }

    // create logical DC
    sww.dibdc = CreateCompatibleDC(win.dc);
    if (!sww.dibdc) {
        Com_EPrintf("DIB_Init: CreateCompatibleDC failed\n");
        goto fail;
    }

    // call SWimp_ModeChanged and friends
    Win_ModeChanged();

    return qtrue;

fail:
    Com_Printf("GetLastError() = %#lx", GetLastError());
    VID_Shutdown();
    return qfalse;
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

/*
DIB_SetPalette

Sets the color table in our DIB section, and also sets the system palette
into an identity mode if we're running in an 8-bit palettized display mode.

The palette is expected to be 1024 bytes, in the format:

R = offset 0
G = offset 1
B = offset 2
A = offset 3
*/
void VID_UpdatePalette(const byte *_pal)
{
    const byte *pal = _pal;
    RGBQUAD         colors[256];
    int             i;

    // set the DIB color table
    if (sww.dibdc) {
        for (i = 0; i < 256; i++, pal += 4) {
            colors[i].rgbRed   = pal[0];
            colors[i].rgbGreen = pal[1];
            colors[i].rgbBlue  = pal[2];
            colors[i].rgbReserved = 0;
        }

        colors[0].rgbRed = 0;
        colors[0].rgbGreen = 0;
        colors[0].rgbBlue = 0;

        colors[255].rgbRed = 0xff;
        colors[255].rgbGreen = 0xff;
        colors[255].rgbBlue = 0xff;

        if (SetDIBColorTable(sww.dibdc, 0, 256, colors) == 0) {
            Com_EPrintf("DIB_SetPalette: SetDIBColorTable failed\n");
        }
    }

    // for 8-bit color desktop modes we set up the palette for maximum
    // speed by going into an identity palette mode.
    if (sww.palettized) {
        int ret;
        HPALETTE hpalOld;
        identitypalette_t ipal;
        LOGPALETTE      *pLogPal = (LOGPALETTE *)&ipal;

        if (SetSystemPaletteUse(win.dc, SYSPAL_NOSTATIC) == SYSPAL_ERROR) {
            Com_Error(ERR_FATAL, "DIB_SetPalette: SetSystemPaletteUse() failed\n");
        }

        // destroy our old palette
        if (sww.pal) {
            DeleteObject(sww.pal);
            sww.pal = 0;
        }

        // take up all physical palette entries to flush out anything that's
        // currently in the palette
        pLogPal->palVersion     = 0x300;
        pLogPal->palNumEntries  = 256;

        for (i = 0, pal = _pal; i < 256; i++, pal += 4) {
            pLogPal->palPalEntry[i].peRed   = pal[0];
            pLogPal->palPalEntry[i].peGreen = pal[1];
            pLogPal->palPalEntry[i].peBlue  = pal[2];
            pLogPal->palPalEntry[i].peFlags = PC_RESERVED | PC_NOCOLLAPSE;
        }
        pLogPal->palPalEntry[0].peRed       = 0;
        pLogPal->palPalEntry[0].peGreen     = 0;
        pLogPal->palPalEntry[0].peBlue      = 0;
        pLogPal->palPalEntry[0].peFlags     = 0;
        pLogPal->palPalEntry[255].peRed     = 0xff;
        pLogPal->palPalEntry[255].peGreen   = 0xff;
        pLogPal->palPalEntry[255].peBlue    = 0xff;
        pLogPal->palPalEntry[255].peFlags   = 0;

        if ((sww.pal = CreatePalette(pLogPal)) == NULL) {
            Com_Error(ERR_FATAL, "DIB_SetPalette: CreatePalette failed(%lx)\n", GetLastError());
        }

        if ((hpalOld = SelectPalette(win.dc, sww.pal, FALSE)) == NULL) {
            Com_Error(ERR_FATAL, "DIB_SetPalette: SelectPalette failed(%lx)\n", GetLastError());
        }

        if (sww.oldpal == NULL)
            sww.oldpal = hpalOld;

        if ((ret = RealizePalette(win.dc)) != pLogPal->palNumEntries)  {
            Com_Error(ERR_FATAL, "DIB_SetPalette: RealizePalette set %d entries\n", ret);
        }
    }
}


