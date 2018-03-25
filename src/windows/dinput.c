/*
Copyright (C) 2003-2006 Andrey Nazarov

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

//
// win_dinput.c - DirectInput 7 mouse driver
//

#include "client.h"

#define DIRECTINPUT_VERSION 0x0700
#include <dinput.h>

#ifndef DIDFT_OPTIONAL
#define DIDFT_OPTIONAL      0x80000000
#endif

static HMODULE  hDirectInput;

typedef HRESULT(WINAPI *LPDIRECTINPUTCREATE)(HINSTANCE, DWORD, LPDIRECTINPUT *, LPUNKNOWN);

static LPDIRECTINPUTCREATE pDirectInputCreate;

static qboolean         di_grabbed; // qfalse when not focus app
static qboolean         di_initialized;
static LPDIRECTINPUT    di;
static LPDIRECTINPUTDEVICE  di_mouse;

#define DEFINE_STATIC_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        static const GUID _##name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }

DEFINE_STATIC_GUID(GUID_SysMouse,  0x6F1D2B60, 0xD5A0, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_STATIC_GUID(GUID_XAxis, 0xA36D02E0, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_STATIC_GUID(GUID_YAxis, 0xA36D02E1, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
DEFINE_STATIC_GUID(GUID_ZAxis, 0xA36D02E2, 0xC9F3, 0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);

static const DIOBJECTDATAFORMAT mouseObjectDataFormat[] = {
    { &_GUID_XAxis, DIMOFS_X,       DIDFT_RELAXIS | DIDFT_ANYINSTANCE,                0 },
    { &_GUID_YAxis, DIMOFS_Y,       DIDFT_RELAXIS | DIDFT_ANYINSTANCE,                0 },
    { &_GUID_ZAxis, DIMOFS_Z,       DIDFT_RELAXIS | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL, 0 },
    { NULL,         DIMOFS_BUTTON0, DIDFT_BUTTON | DIDFT_ANYINSTANCE,                 0 },
    { NULL,         DIMOFS_BUTTON1, DIDFT_BUTTON | DIDFT_ANYINSTANCE,                 0 },
    { NULL,         DIMOFS_BUTTON2, DIDFT_BUTTON | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL,  0 },
    { NULL,         DIMOFS_BUTTON3, DIDFT_BUTTON | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL,  0 },
    { NULL,         DIMOFS_BUTTON4, DIDFT_BUTTON | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL,  0 },
    { NULL,         DIMOFS_BUTTON5, DIDFT_BUTTON | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL,  0 },
    { NULL,         DIMOFS_BUTTON6, DIDFT_BUTTON | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL,  0 },
    { NULL,         DIMOFS_BUTTON7, DIDFT_BUTTON | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL,  0 }
};

static const DIDATAFORMAT mouseDataFormat = {
    sizeof(DIDATAFORMAT),
    sizeof(DIOBJECTDATAFORMAT),
    DIDF_RELAXIS,
    sizeof(DIMOUSESTATE2),
    q_countof(mouseObjectDataFormat),
    (LPDIOBJECTDATAFORMAT)mouseObjectDataFormat
};

static const DIPROPDWORD mouseBufferSize = {
    {
        sizeof(DIPROPDWORD),
        sizeof(DIPROPHEADER),
        0,
        DIPH_DEVICE
    },
    32
};

/*
===========
DI_GetMouseEvents
===========
*/
static void DI_GetMouseEvents(void)
{
    DIDEVICEOBJECTDATA data[16];
    LPDIDEVICEOBJECTDATA p, last;
    DWORD   numElements, button;
    int     value;
    HRESULT hr;

    if (!di_grabbed) {
        return;
    }

    do {
        numElements = 16;
        hr = IDirectInputDevice_GetDeviceData(di_mouse, sizeof(data[0]), data, &numElements, 0);
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
            IDirectInputDevice_Acquire(di_mouse);
            return;
        }
        if (FAILED(hr)) {
            Com_EPrintf("GetDeviceData failed with error 0x%lX\n", hr);
            return;
        }
        last = data + numElements;
        for (p = data; p < last; p++) {
            switch (p->dwOfs) {
            case DIMOFS_BUTTON0:
            case DIMOFS_BUTTON1:
            case DIMOFS_BUTTON2:
            case DIMOFS_BUTTON3:
            case DIMOFS_BUTTON4:
            case DIMOFS_BUTTON5:
            case DIMOFS_BUTTON6:
            case DIMOFS_BUTTON7:
                button = p->dwOfs - DIMOFS_BUTTON0;
                if (p->dwData & 0x80) {
                    Key_Event(K_MOUSE1 + button, qtrue, p->dwTimeStamp);
                } else {
                    Key_Event(K_MOUSE1 + button, qfalse, p->dwTimeStamp);
                }
                break;
            case DIMOFS_Z:
                value = p->dwData;
                if (!value) {
                    break;
                }
                if (value > 0) {
                    Key_Event(K_MWHEELUP, qtrue, p->dwTimeStamp);
                    Key_Event(K_MWHEELUP, qfalse, p->dwTimeStamp);
                } else {
                    Key_Event(K_MWHEELDOWN, qtrue, p->dwTimeStamp);
                    Key_Event(K_MWHEELDOWN, qfalse, p->dwTimeStamp);
                }
                break;
            default:
                break;
            }
        }
    } while (hr == DI_BUFFEROVERFLOW);
}

/*
===========
DI_GetMouseMotion
===========
*/
static qboolean DI_GetMouseMotion(int *dx, int *dy)
{
    DIMOUSESTATE2   state;
    HRESULT hr;

    if (!di_grabbed) {
        return qfalse;
    }

    hr = IDirectInputDevice_GetDeviceState(di_mouse, sizeof(state), &state);
    if (FAILED(hr)) {
        Com_EPrintf("GetDeviceState failed with error 0x%lX\n", hr);
        return qfalse;
    }

    *dx = state.lX;
    *dy = state.lY;
    return qtrue;
}

/*
===========
DI_ShutdownMouse
===========
*/
static void DI_ShutdownMouse(void)
{
    Com_Printf("Shutting down DirectInput\n");

    if (di_mouse) {
        if (di_grabbed) {
            IDirectInputDevice_Unacquire(di_mouse);
        }
        IDirectInputDevice_Release(di_mouse);
        di_mouse = NULL;
    }
    if (di) {
        IDirectInput_Release(di);
        di = NULL;
    }
    di_grabbed = qfalse;
    di_initialized = qfalse;

}

/*
===========
DI_StartupMouse
===========
*/
static qboolean DI_InitMouse(void)
{
    HRESULT hr;

    if (!win.wnd) {
        return qfalse;
    }

    Com_Printf("Initializing DirectInput\n");

    if (!hDirectInput) {
        hDirectInput = LoadLibrary("dinput.dll");
        if (!hDirectInput) {
            Com_EPrintf("Failed to load dinput.dll\n");
            return qfalse;
        }

        pDirectInputCreate = (LPDIRECTINPUTCREATE)
                             GetProcAddress(hDirectInput, "DirectInputCreateA");
        if (!pDirectInputCreate) {
            Com_EPrintf("Failed to obtain DirectInputCreate\n");
            goto fail;
        }
    }

    hr = pDirectInputCreate(hGlobalInstance, DIRECTINPUT_VERSION, &di, NULL);
    if (FAILED(hr)) {
        Com_EPrintf("DirectInputCreate failed with error 0x%lX\n", hr);
        goto fail;
    }

    hr = IDirectInput_CreateDevice(di, &_GUID_SysMouse, &di_mouse, NULL);
    if (FAILED(hr)) {
        Com_EPrintf("CreateDevice failed with error 0x%lX\n", hr);
        goto fail;
    }

    hr = IDirectInputDevice_SetDataFormat(di_mouse, &mouseDataFormat);
    if (FAILED(hr)) {
        Com_EPrintf("SetDataFormat failed with error 0x%lX\n", hr);
        goto fail;
    }

    hr = IDirectInputDevice_SetCooperativeLevel(di_mouse, win.wnd, DISCL_EXCLUSIVE | DISCL_FOREGROUND);
    if (FAILED(hr)) {
        Com_EPrintf("SetCooperativeLevel failed with error 0x%lX\n", hr);
        goto fail;
    }

    hr = IDirectInputDevice_SetProperty(di_mouse, DIPROP_BUFFERSIZE, &mouseBufferSize.diph);
    if (FAILED(hr)) {
        Com_EPrintf("SetProperty failed with error 0x%lX\n", hr);
        goto fail;
    }

    di_initialized = qtrue;

    return qtrue;

fail:
    if (di_mouse) {
        IDirectInputDevice_Release(di_mouse);
        di_mouse = NULL;
    }
    if (di) {
        IDirectInput_Release(di);
        di = NULL;
    }
    return qfalse;
}

/*
===========
DI_GrabMouse
===========
*/
static void DI_GrabMouse(qboolean grab)
{
    HRESULT hr;

    if (!di_initialized) {
        return;
    }

    if (di_grabbed == grab) {
        return;
    }

    if (grab) {
        Com_DPrintf("IDirectInputDevice_Acquire\n");
        hr = IDirectInputDevice_Acquire(di_mouse);
        if (FAILED(hr)) {
            Com_EPrintf("Failed to acquire mouse, error 0x%lX\n", hr);
        }
    } else {
        Com_DPrintf("IDirectInputDevice_Unacquire\n");
        hr = IDirectInputDevice_Unacquire(di_mouse);
        if (FAILED(hr)) {
            Com_EPrintf("Failed to unacquire mouse, error 0x%lX\n", hr);
        }
    }

    di_grabbed = grab;

}

/*
@@@@@@@@@@@@@@@@@@@
DI_FillAPI
@@@@@@@@@@@@@@@@@@@
*/
void DI_FillAPI(inputAPI_t *api)
{
    api->Init = DI_InitMouse;
    api->Shutdown = DI_ShutdownMouse;
    api->Grab = DI_GrabMouse;
    api->GetEvents = DI_GetMouseEvents;
    api->GetMotion = DI_GetMouseMotion;
}

