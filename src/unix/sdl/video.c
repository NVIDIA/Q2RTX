/*
Copyright (C) 2003-2005 Andrey Nazarov

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
// video.c
//

#include "video.h"
#include "../res/q2pro.xbm"

sdl_state_t    sdl;

/*
===============================================================================

COMMON SDL VIDEO RELATED ROUTINES

===============================================================================
*/

qboolean VID_SDL_SetMode(int flags, int forcedepth)
{
    SDL_Surface *surf;
    vrect_t rc;
    int depth;

    flags &= ~(SDL_FULLSCREEN | SDL_RESIZABLE);
    sdl.flags &= ~QVF_FULLSCREEN;

    if (vid_fullscreen->integer > 0) {
        VID_GetFullscreen(&rc, NULL, &depth);
        if (forcedepth) {
            depth = forcedepth;
        }
        Com_DPrintf("...setting fullscreen mode: %dx%d:%d\n",
                    rc.width, rc.height, depth);
        surf = SDL_SetVideoMode(rc.width, rc.height, depth,
                                flags | SDL_FULLSCREEN);
        if (surf) {
            sdl.flags |= QVF_FULLSCREEN;
            goto success;
        }
        Com_EPrintf("Fullscreen video mode failed: %s\n", SDL_GetError());
        Cvar_Set("vid_fullscreen", "0");
    }

    flags |= SDL_RESIZABLE;
    VID_GetGeometry(&rc);
    Com_DPrintf("...setting windowed mode: %dx%d\n", rc.width, rc.height);
    surf = SDL_SetVideoMode(rc.width, rc.height, forcedepth, flags);
    if (!surf) {
        return qfalse;
    }

success:
    // init some stuff for the first time
    if (sdl.surface != surf) {
        sdl.surface = surf;
        VID_SDL_SurfaceChanged();
        CL_Activate(ACT_ACTIVATED);
    }
    R_ModeChanged(rc.width, rc.height, sdl.flags, surf->pitch, surf->pixels);
    SCR_ModeChanged();
    return qtrue;
}

char *VID_GetDefaultModeList(void)
{
    return Z_CopyString(VID_MODELIST);
}

/*
============
VID_SetMode
============
*/
void VID_SetMode(void)
{
    if (!sdl.surface) {
        Com_Error(ERR_FATAL, "%s: NULL surface", __func__);
    }

    if (!VID_SDL_SetMode(sdl.surface->flags, sdl.surface->format->BitsPerPixel)) {
        Com_Error(ERR_FATAL, "Couldn't change video mode: %s", SDL_GetError());
    }
}

void VID_FatalShutdown(void)
{
    SDL_ShowCursor(SDL_ENABLE);
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    SDL_Quit();
}

qboolean VID_SDL_Init(void)
{
    SDL_Color    color;
    byte *dst;
    char buffer[MAX_QPATH];
    int i, ret;
    cvar_t *vid_hwgamma;

    if (SDL_WasInit(SDL_INIT_EVERYTHING) == 0) {
        ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE);
    } else {
        ret = SDL_InitSubSystem(SDL_INIT_VIDEO);
    }

    if (ret == -1) {
        Com_EPrintf("Couldn't initialize SDL video: %s\n", SDL_GetError());
        return qfalse;
    }

    if (SDL_VideoDriverName(buffer, sizeof(buffer)) != NULL) {
        Com_Printf("Using SDL video driver: %s\n", buffer);
    }

    sdl.icon = SDL_CreateRGBSurface(SDL_SWSURFACE, q2icon_width,
                                    q2icon_height, 8, 0, 0, 0, 0);
    if (sdl.icon) {
        SDL_SetColorKey(sdl.icon, SDL_SRCCOLORKEY, 0);

        // transparent pixel
        color.r = 255; color.g = 255; color.b = 255;
        SDL_SetColors(sdl.icon, &color, 0, 1);

        // colored pixel
        color.r =   0; color.g = 128; color.b = 128;
        SDL_SetColors(sdl.icon, &color, 1, 1);

        // expand the bitmap
        dst = sdl.icon->pixels;
        for (i = 0; i < q2icon_width * q2icon_height; i++) {
            *dst++ = Q_IsBitSet(q2icon_bits, i);
        }

        SDL_WM_SetIcon(sdl.icon, NULL);
    }

    vid_hwgamma = Cvar_Get("vid_hwgamma", "0", CVAR_REFRESH);

    if (vid_hwgamma->integer) {
        if (SDL_GetGammaRamp(sdl.gamma[0], sdl.gamma[1], sdl.gamma[2]) != -1 &&
            SDL_SetGammaRamp(sdl.gamma[0], sdl.gamma[1], sdl.gamma[2]) != -1) {
            Com_Printf("...enabling hardware gamma\n");
            sdl.flags |= QVF_GAMMARAMP;
        } else {
            Com_Printf("...hardware gamma not supported\n");
            Cvar_Reset(vid_hwgamma);
        }
    }

    SDL_WM_SetCaption(PRODUCT, APPLICATION);

    SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

    return qtrue;
}

void VID_SDL_Shutdown(void)
{
    if (sdl.flags & QVF_GAMMARAMP) {
        SDL_SetGammaRamp(sdl.gamma[0], sdl.gamma[1], sdl.gamma[2]);
    }
    if (sdl.icon) {
        SDL_FreeSurface(sdl.icon);
    }
    memset(&sdl, 0, sizeof(sdl));

    if (SDL_WasInit(SDL_INIT_EVERYTHING) == SDL_INIT_VIDEO) {
        SDL_Quit();
    } else {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

void VID_UpdateGamma(const byte *table)
{
    Uint16 ramp[256];
    int i;

    if (sdl.flags & QVF_GAMMARAMP) {
        for (i = 0; i < 256; i++) {
            ramp[i] = table[i] << 8;
        }
        SDL_SetGammaRamp(ramp, ramp, ramp);
    }
}

static void activate_event(void)
{
    int state = SDL_GetAppState();
    active_t active;

    if (state & SDL_APPACTIVE) {
        if (state & (SDL_APPMOUSEFOCUS | SDL_APPINPUTFOCUS)) {
            active = ACT_ACTIVATED;
        } else {
            active = ACT_RESTORED;
        }
    } else {
        active = ACT_MINIMIZED;
    }

    CL_Activate(active);
}

static void key_event(SDL_keysym *keysym, qboolean down)
{
    unsigned key1, key2 = 0;

    if (keysym->sym <= 127) {
        // ASCII chars are mapped directly
        Key_Event(keysym->sym, down, com_eventTime);
        return;
    }

#define K(s, d)         case SDLK_ ## s: key1 = K_ ## d; break;
#define KK(s, d1, d2)   case SDLK_ ## s: key1 = K_ ## d1; key2 = K_ ## d2; break;

    switch (keysym->sym) {
        K(KP0,         KP_INS)
        K(KP1,         KP_END)
        K(KP2,         KP_DOWNARROW)
        K(KP3,         KP_PGDN)
        K(KP4,         KP_LEFTARROW)
        K(KP5,         KP_5)
        K(KP6,         KP_RIGHTARROW)
        K(KP7,         KP_HOME)
        K(KP8,         KP_UPARROW)
        K(KP9,         KP_PGUP)
        K(KP_PERIOD,   KP_DEL)
        K(KP_DIVIDE,   KP_SLASH)
        K(KP_MULTIPLY, KP_MULTIPLY)
        K(KP_MINUS,    KP_MINUS)
        K(KP_PLUS,     KP_PLUS)
        K(KP_ENTER,    KP_ENTER)

        K(UP,       UPARROW)
        K(DOWN,     DOWNARROW)
        K(RIGHT,    RIGHTARROW)
        K(LEFT,     LEFTARROW)
        K(INSERT,   INS)
        K(HOME,     HOME)
        K(END,      END)
        K(PAGEUP,   PGUP)
        K(PAGEDOWN, PGDN)

        K(F1,  F1)
        K(F2,  F2)
        K(F3,  F3)
        K(F4,  F4)
        K(F5,  F5)
        K(F6,  F6)
        K(F7,  F7)
        K(F8,  F8)
        K(F9,  F9)
        K(F10, F10)
        K(F11, F11)
        K(F12, F12)

        K(NUMLOCK,   NUMLOCK)
        K(CAPSLOCK,  CAPSLOCK)
        K(SCROLLOCK, SCROLLOCK)
        K(LSUPER,    LWINKEY)
        K(RSUPER,    RWINKEY)
        K(MENU,      MENU)

        KK(RSHIFT, SHIFT, RSHIFT)
        KK(LSHIFT, SHIFT, LSHIFT)
        KK(RCTRL,  CTRL,  RCTRL)
        KK(LCTRL,  CTRL,  LCTRL)
        KK(RALT,   ALT,   RALT)
        KK(LALT,   ALT,   LALT)

#undef K
#undef KK

    default:
        Com_DPrintf("%s: unknown keysym %d\n", __func__, keysym->sym);
        return;
    }

    Key_Event(key1, down, com_eventTime);
    if (key2) {
        Key_Event(key2, down, com_eventTime);
    }
}

static void button_event(int button, qboolean down)
{
    unsigned key;

    if (!sdl.mouse.initialized) {
        return;
    }

#define K(s, d) case SDL_BUTTON_ ## s: key = K_ ## d; break;

    switch (button) {
        K(LEFT,       MOUSE1)
        K(RIGHT,      MOUSE2)
        K(MIDDLE,     MOUSE3)
        K(WHEELUP,    MWHEELUP)
        K(WHEELDOWN,  MWHEELDOWN)

#undef K

    default:
        Com_DPrintf("%s: unknown button %d\n", __func__, button);
        return;
    }

    Key_Event(key, down, com_eventTime);
}

static void resize_event(int w, int h)
{
    if (!sdl.surface) {
        return;
    }
    if (!(sdl.surface->flags & SDL_RESIZABLE)) {
        return;
    }

    Cvar_Set("vid_geometry", va("%dx%d", w, h));
    VID_SetMode();
}

/*
============
VID_PumpEvents
============
*/
void VID_PumpEvents(void)
{
    SDL_Event    event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_ACTIVEEVENT:
            activate_event();
            break;
        case SDL_QUIT:
            Com_Quit(NULL, ERR_DISCONNECT);
            break;
        case SDL_VIDEORESIZE:
            resize_event(event.resize.w, event.resize.h);
            return; // process only one resize event per frame
        case SDL_VIDEOEXPOSE:
            SCR_UpdateScreen();
            break;
        case SDL_KEYDOWN:
            key_event(&event.key.keysym, qtrue);
            break;
        case SDL_KEYUP:
            key_event(&event.key.keysym, qfalse);
            break;
        case SDL_MOUSEBUTTONDOWN:
            button_event(event.button.button, qtrue);
            break;
        case SDL_MOUSEBUTTONUP:
            button_event(event.button.button, qfalse);
            break;
        case SDL_MOUSEMOTION:
            IN_MouseEvent(event.motion.x, event.motion.y);
            break;
        }
    }
}

/*
===============================================================================

MOUSE DRIVER

===============================================================================
*/

static void AcquireMouse(void)
{
    // move cursor to center of the main window before we grab the mouse
    if (sdl.surface) {
        SDL_WarpMouse(sdl.surface->w / 2, sdl.surface->h / 2);
    }

    // pump mouse motion events generated by SDL_WarpMouse
    SDL_PollEvent(NULL);

    // grab the mouse, so SDL enters relative mouse mode
    SDL_WM_GrabInput(SDL_GRAB_ON);
    SDL_ShowCursor(SDL_DISABLE);

    // pump mouse motion events still pending
    SDL_PollEvent(NULL);

    // clear any deltas generated
    SDL_GetRelativeMouseState(NULL, NULL);
}

static qboolean GetMouseMotion(int *dx, int *dy)
{
    if (!sdl.mouse.grabbed) {
        return qfalse;
    }
    SDL_GetRelativeMouseState(dx, dy);
    return qtrue;
}

static void WarpMouse(int x, int y)
{
    SDL_WarpMouse(x, y);
    SDL_PollEvent(NULL);
    SDL_GetRelativeMouseState(NULL, NULL);
}

static void ShutdownMouse(void)
{
    // release the mouse
    SDL_ShowCursor(SDL_ENABLE);
    SDL_WM_GrabInput(SDL_GRAB_OFF);
    SDL_WM_SetCaption(PRODUCT, APPLICATION);
    memset(&sdl.mouse, 0, sizeof(sdl.mouse));
}

static qboolean InitMouse(void)
{
    if (SDL_WasInit(SDL_INIT_VIDEO) != SDL_INIT_VIDEO) {
        return qfalse;
    }

    Com_Printf("SDL mouse initialized.\n");
    sdl.mouse.initialized = qtrue;

    return qtrue;
}

static void GrabMouse(grab_t grab)
{
    if (!sdl.mouse.initialized) {
        return;
    }

    if (sdl.mouse.grabbed == grab) {
        SDL_GetRelativeMouseState(NULL, NULL);
        return;
    }

    if (grab == IN_GRAB) {
        AcquireMouse();
        SDL_WM_SetCaption("[" PRODUCT "]", APPLICATION);
    } else {
        if (sdl.mouse.grabbed == IN_GRAB) {
            SDL_WM_GrabInput(SDL_GRAB_OFF);
            SDL_WM_SetCaption(PRODUCT, APPLICATION);
        }
        if (grab == IN_HIDE) {
            SDL_ShowCursor(SDL_DISABLE);
        } else {
            SDL_ShowCursor(SDL_ENABLE);
        }
    }

    sdl.mouse.grabbed = grab;
}

/*
============
VID_FillInputAPI
============
*/
void VID_FillInputAPI(inputAPI_t *api)
{
    api->Init = InitMouse;
    api->Shutdown = ShutdownMouse;
    api->Grab = GrabMouse;
    api->Warp = WarpMouse;
    api->GetEvents = NULL;
    api->GetMotion = GetMouseMotion;
}

