/*
Copyright (C) 2013 Andrey Nazarov

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

#include "shared/shared.h"
#include "common/cvar.h"
#include "common/common.h"
#include "common/files.h"
#include "common/zone.h"
#include "client/client.h"
#include "client/input.h"
#include "client/keys.h"
#include "client/ui.h"
#include "client/video.h"
#include "refresh/refresh.h"
#include "system/system.h"
#include "../res/q2pro.xbm"
#include <SDL2/SDL.h>

#if USE_REF == REF_GLPT
#include <glad/glad.h>
#endif

SDL_Window       *sdl_window;
static vidFlags_t       sdl_flags;

/*
===============================================================================

OPENGL STUFF

===============================================================================
*/

#if USE_REF == REF_GL || USE_REF == REF_GLPT

static SDL_GLContext    *sdl_context;

static void gl_swapinterval_changed(cvar_t *self)
{
    if (SDL_GL_SetSwapInterval(self->integer) < 0) {
        Com_EPrintf("Couldn't set swap interval %d: %s\n", self->integer, SDL_GetError());
    }
}

static qboolean VID_SDL_GL_LoadLibrary(void)
{
#if USE_FIXED_LIBGL
    Cvar_Get("gl_driver", LIBGL, CVAR_ROM);
    return qtrue;
#else
    cvar_t *gl_driver = Cvar_Get("gl_driver", LIBGL, CVAR_REFRESH);

    // don't allow absolute or relative paths
    FS_SanitizeFilenameVariable(gl_driver);

    while (1) {
        char *s;

        // ugly hack to work around brain-dead servers that actively
        // check and enforce `gl_driver' cvar to `opengl32', unaware
        // of other systems than Windows
        s = gl_driver->string;
        if (!Q_stricmp(s, "opengl32") || !Q_stricmp(s, "opengl32.dll")) {
            Com_Printf("...attempting to load %s instead of %s\n",
                       gl_driver->default_string, s);
            s = gl_driver->default_string;
        }

        if (SDL_GL_LoadLibrary(s) == 0) {
            break;
        }

        Com_EPrintf("Couldn't load OpenGL library: %s\n", SDL_GetError());
        if (!strcmp(s, gl_driver->default_string)) {
            return qfalse;
        }

        // attempt to recover
        Com_Printf("...falling back to %s\n", gl_driver->default_string);
        Cvar_Reset(gl_driver);
    }

    return qtrue;
#endif
}

static void VID_SDL_GL_SetAttributes(void)
{
#if USE_REF == REF_GLPT
    // Set attributes
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#else
    int colorbits = Cvar_ClampInteger(
        Cvar_Get("gl_colorbits", "0", CVAR_REFRESH), 0, 32);
    int depthbits = Cvar_ClampInteger(
        Cvar_Get("gl_depthbits", "0", CVAR_REFRESH), 0, 32);
    int stencilbits = Cvar_ClampInteger(
        Cvar_Get("gl_stencilbits", "8", CVAR_REFRESH), 0, 8);
    int multisamples = Cvar_ClampInteger(
        Cvar_Get("gl_multisamples", "0", CVAR_REFRESH), 0, 32);

    if (colorbits == 0)
        colorbits = 24;

    if (depthbits == 0)
        depthbits = colorbits > 16 ? 24 : 16;

    if (depthbits < 24)
        stencilbits = 0;

    if (colorbits > 16) {
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    } else {
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 5);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
    }

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depthbits);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, stencilbits);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    if (multisamples > 1) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, multisamples);
    }
#endif
}

#if !USE_FIXED_LIBGL
void *VID_GetCoreAddr(const char *sym)
{
    void    *entry = SDL_GL_GetProcAddress(sym);

    if (!entry)
        Com_EPrintf("Couldn't get OpenGL entry point: %s\n", sym);

    return entry;
}
#endif

void *VID_GetProcAddr(const char *sym)
{
    void    *entry = SDL_GL_GetProcAddress(sym);

    if (!entry)
        Com_EPrintf("Couldn't get OpenGL entry point: %s\n", sym);

    return entry;
}

#endif

/*
===============================================================================

VIDEO

===============================================================================
*/

static void VID_SDL_ModeChanged(void)
{
    int width, height;
    void *pixels;
    int rowbytes;

    SDL_GetWindowSize(sdl_window, &width, &height);

    Uint32 flags = SDL_GetWindowFlags(sdl_window);
    if (flags & SDL_WINDOW_FULLSCREEN)
        sdl_flags |= QVF_FULLSCREEN;
    else
        sdl_flags &= ~QVF_FULLSCREEN;

#if USE_REF == REF_SOFT
    SDL_Surface *surf = SDL_GetWindowSurface(sdl_window);
    if (!surf)
        Com_Error(ERR_FATAL, "Couldn't (re)create window surface: %s", SDL_GetError());
    pixels = surf->pixels;
    rowbytes = surf->pitch;
#else
    pixels = NULL;
    rowbytes = 0;
#endif

    R_ModeChanged(width, height, sdl_flags, rowbytes, pixels);
    SCR_ModeChanged();
}

static void VID_SDL_SetMode(void)
{
    Uint32 flags;
    vrect_t rc;
    int freq;

    if (vid_fullscreen->integer) {
        // FIXME: force update by toggling fullscreen mode
        SDL_SetWindowFullscreen(sdl_window, 0);

        if (VID_GetFullscreen(&rc, &freq, NULL)) {
            SDL_DisplayMode mode = {
                .format         = SDL_PIXELFORMAT_UNKNOWN,
                .w              = rc.width,
                .h              = rc.height,
                .refresh_rate   = freq,
                .driverdata     = NULL
            };
            SDL_SetWindowDisplayMode(sdl_window, &mode);
            flags = SDL_WINDOW_FULLSCREEN;
        } else {
            flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
        }
    } else {
        if (VID_GetGeometry(&rc)) {
            SDL_SetWindowSize(sdl_window, rc.width, rc.height);
            SDL_SetWindowPosition(sdl_window, rc.x, rc.y);
        }
        flags = 0;
    }

    SDL_SetWindowFullscreen(sdl_window, flags);
}

void VID_SetMode(void)
{
    VID_SDL_SetMode();
    VID_SDL_ModeChanged();
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

void VID_EndFrame(void)
{
#if USE_REF == REF_GL || USE_REF == REF_GLPT
    SDL_GL_SwapWindow(sdl_window);
#elif USE_REF == REF_VKPT
	/* subsystem does it itself */
#else
    SDL_UpdateWindowSurface(sdl_window);
#endif
}

void VID_FatalShutdown(void)
{
    SDL_SetWindowGrab(sdl_window, SDL_FALSE);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
    SDL_Quit();
}

char *VID_GetClipboardData(void)
{
    // returned data must be Z_Free'd
    char *text = SDL_GetClipboardText();
    char *copy = Z_CopyString(text);
    SDL_free(text);
    return copy;
}

void VID_SetClipboardData(const char *data)
{
    SDL_SetClipboardText(data);
}

void VID_UpdateGamma(const byte *table)
{
    Uint16 ramp[256];
    int i;

    if (sdl_flags & QVF_GAMMARAMP) {
        for (i = 0; i < 256; i++) {
            ramp[i] = table[i] << 8;
        }
        SDL_SetWindowGammaRamp(sdl_window, ramp, ramp, ramp);
    }
}

static int VID_SDL_InitSubSystem(void)
{
    int ret;

    ret = SDL_WasInit(SDL_INIT_EVERYTHING);
    if (ret == 0)
        ret = SDL_Init(SDL_INIT_VIDEO);
    else if (!(ret & SDL_INIT_VIDEO))
        ret = SDL_InitSubSystem(SDL_INIT_VIDEO);
    else
        ret = 0;

    if (ret == -1)
        Com_EPrintf("Couldn't initialize SDL video: %s\n", SDL_GetError());

    return ret;
}

static int VID_SDL_EventFilter(void *userdata, SDL_Event *event)
{
    // SDL uses relative time, we need absolute
    event->common.timestamp = Sys_Milliseconds();
    return 1;
}

char *VID_GetDefaultModeList(void)
{
    SDL_DisplayMode mode;
    size_t size, len;
    char *buf;
    int i, num_modes;

    if (VID_SDL_InitSubSystem())
        return NULL;

    num_modes = SDL_GetNumDisplayModes(0);
    if (num_modes < 1)
        return Z_CopyString(VID_MODELIST);

    size = 8 + num_modes * 32 + 1;
    buf = Z_Malloc(size);

    len = Q_strlcpy(buf, "desktop ", size);
    for (i = 0; i < num_modes; i++) {
        if (SDL_GetDisplayMode(0, i, &mode) < 0)
            break;
        if (mode.refresh_rate == 0)
            continue;
        len += Q_scnprintf(buf + len, size - len, "%dx%d@%d ",
                           mode.w, mode.h, mode.refresh_rate);
    }

    if (len == 0)
        buf[0] = 0;
    else if (buf[len - 1] == ' ')
        buf[len - 1] = 0;

    return buf;
}

qboolean VID_Init(void)
{
    Uint32 flags = SDL_WINDOW_RESIZABLE;
    vrect_t rc;

    if (VID_SDL_InitSubSystem()) {
        return qfalse;
    }

#if USE_REF == REF_GL || USE_REF == REF_GLPT
    if (!VID_SDL_GL_LoadLibrary()) {
        goto fail;
    }

    VID_SDL_GL_SetAttributes();
    flags |= SDL_WINDOW_OPENGL;
#endif

    SDL_SetEventFilter(VID_SDL_EventFilter, NULL);

    if (!VID_GetGeometry(&rc)) {
        rc.x = SDL_WINDOWPOS_UNDEFINED;
        rc.y = SDL_WINDOWPOS_UNDEFINED;
    }

#if !(USE_REF == REF_VKPT)
    sdl_window = SDL_CreateWindow(PRODUCT, rc.x, rc.y, rc.width, rc.height, flags);
    if (!sdl_window) {
        Com_EPrintf("Couldn't create SDL window: %s\n", SDL_GetError());
        goto fail;
    }

    SDL_SetWindowMinimumSize(sdl_window, 320, 240);

    SDL_Surface *icon = SDL_CreateRGBSurfaceFrom(q2icon_bits, q2icon_width, q2icon_height,
                                                 1, q2icon_width / 8, 0, 0, 0, 0);
    if (icon) {
        SDL_Color colors[2] = {
            { 255, 255, 255 },
            {   0, 128, 128 }
        };
        SDL_SetPaletteColors(icon->format->palette, colors, 0, 2);
        SDL_SetColorKey(icon, SDL_TRUE, 0);
        SDL_SetWindowIcon(sdl_window, icon);
        SDL_FreeSurface(icon);
    }

    VID_SDL_SetMode();

#endif


#if USE_REF == REF_GL || USE_REF == REF_GLPT
    sdl_context = SDL_GL_CreateContext(sdl_window);
    if (!sdl_context) {
        Com_EPrintf("Couldn't create OpenGL context: %s\n", SDL_GetError());
        goto fail;
    }

    cvar_t *gl_swapinterval = Cvar_Get("gl_swapinterval", "0", 0);
    gl_swapinterval->changed = gl_swapinterval_changed;
    gl_swapinterval_changed(gl_swapinterval);
#endif
#if USE_REF == REF_GLPT
    if(!gladLoadGLLoader(SDL_GL_GetProcAddress)) {
        goto fail;
    }
	Com_Printf("Vendor:          %s\n", glGetString(GL_VENDOR));
	Com_Printf("Renderer:        %s\n", glGetString(GL_RENDERER));
	Com_Printf("Version OpenGL:  %s\n", glGetString(GL_VERSION));
	Com_Printf("Version GLSL:    %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
#endif

    cvar_t *vid_hwgamma = Cvar_Get("vid_hwgamma", "0", CVAR_REFRESH);
    if (vid_hwgamma->integer) {
        Uint16  gamma[3][256];

        if (SDL_GetWindowGammaRamp(sdl_window, gamma[0], gamma[1], gamma[2]) == 0 &&
            SDL_SetWindowGammaRamp(sdl_window, gamma[0], gamma[1], gamma[2]) == 0) {
            Com_Printf("...enabling hardware gamma\n");
            sdl_flags |= QVF_GAMMARAMP;
        } else {
            Com_Printf("...hardware gamma not supported\n");
            Cvar_Reset(vid_hwgamma);
        }
    }

    VID_SDL_ModeChanged();
    return qtrue;

fail:
    VID_Shutdown();
    return qfalse;
}

void VID_Shutdown(void)
{
#if USE_REF == REF_GL || USE_REF == REF_GLPT
    if (sdl_context) {
        SDL_GL_DeleteContext(sdl_context);
        sdl_context = NULL;
    }
#endif
    if (sdl_window) {
        SDL_DestroyWindow(sdl_window);
        sdl_window = NULL;
    }
    sdl_flags = 0;
    if (SDL_WasInit(SDL_INIT_EVERYTHING) == SDL_INIT_VIDEO) {
        SDL_Quit();
    } else {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
}

/*
==========================================================================

EVENTS

==========================================================================
*/

static void window_event(SDL_WindowEvent *event)
{
    Uint32 flags = SDL_GetWindowFlags(sdl_window);
    active_t active;
    vrect_t rc;

    switch (event->event) {
    case SDL_WINDOWEVENT_MINIMIZED:
    case SDL_WINDOWEVENT_RESTORED:
    case SDL_WINDOWEVENT_ENTER:
    case SDL_WINDOWEVENT_LEAVE:
    case SDL_WINDOWEVENT_FOCUS_GAINED:
    case SDL_WINDOWEVENT_FOCUS_LOST:
        if (flags & (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS)) {
            active = ACT_ACTIVATED;
        } else if (flags & SDL_WINDOW_MINIMIZED) {
            active = ACT_MINIMIZED;
        } else {
            active = ACT_RESTORED;
        }
        CL_Activate(active);
        break;

    case SDL_WINDOWEVENT_MOVED:
        if (!(flags & SDL_WINDOW_FULLSCREEN)) {
            SDL_GetWindowSize(sdl_window, &rc.width, &rc.height);
            rc.x = event->data1;
            rc.y = event->data2;
            VID_SetGeometry(&rc);
        }
        break;

    case SDL_WINDOWEVENT_RESIZED:
        if (!(flags & SDL_WINDOW_FULLSCREEN)) {
            SDL_GetWindowPosition(sdl_window, &rc.x, &rc.y);
            rc.width = event->data1;
            rc.height = event->data2;
            VID_SetGeometry(&rc);
        }
        VID_SDL_ModeChanged();
        break;
    }
}

static const byte scantokey[128] = {
//  0               1               2               3               4               5               6                   7
//  8               9               A               B               C               D               E                   F
    0,              0,              0,              0,              'a',            'b',            'c',                'd',            // 0
    'e',            'f',            'g',            'h',            'i',            'j',            'k',                'l',
    'm',            'n',            'o',            'p',            'q',            'r',            's',                't',            // 1
    'u',            'v',            'w',            'x',            'y',            'z',            '1',                '2',
    '3',            '4',            '5',            '6',            '7',            '8',            '9',                '0',            // 2
    K_ENTER,        K_ESCAPE,       K_BACKSPACE,    K_TAB,          K_SPACE,        '-',            '=',                '[',
    ']',            '\\',           0,              ';',            '\'',           '`',            ',',                '.',            // 3
    '/' ,           K_CAPSLOCK,     K_F1,           K_F2,           K_F3,           K_F4,           K_F5,               K_F6,
    K_F7,           K_F8,           K_F9,           K_F10,          K_F11,          K_F12,          K_PRINTSCREEN,      K_SCROLLOCK,    // 4
    K_PAUSE,        K_INS,          K_HOME,         K_PGUP,         K_DEL,          K_END,          K_PGDN,             K_RIGHTARROW,
    K_LEFTARROW,    K_DOWNARROW,    K_UPARROW,      K_NUMLOCK,      K_KP_SLASH,     K_KP_MULTIPLY,  K_KP_MINUS,         K_KP_PLUS,      // 5
    K_KP_ENTER,     K_KP_END,       K_KP_DOWNARROW, K_KP_PGDN,      K_KP_LEFTARROW, K_KP_5,         K_KP_RIGHTARROW,    K_KP_HOME,
    K_KP_UPARROW,   K_KP_PGUP,      K_KP_INS,       K_KP_DEL,       K_102ND,        0,              0,                  0,              // 6
    0,              0,              0,              0,              0,              0,              0,                  0,
    0,              0,              0,              0,              0,              0,              K_MENU,             0,              // 7
    K_LCTRL,        K_LSHIFT,       K_LALT,         K_LWINKEY,      K_RCTRL,        K_RSHIFT,       K_RALT,             K_RWINKEY,      // E
};

static void key_event(SDL_KeyboardEvent *event)
{
    byte result;

    if(event->keysym.sym < 127)
      result = event->keysym.sym; // direct mapping of ascii
    else if (event->keysym.scancode >= 224 && event->keysym.scancode < 224 + 8)
      result = scantokey[event->keysym.scancode - 104];
    else if (event->keysym.scancode > 0x30)
      result = scantokey[event->keysym.scancode];
    else result = 0;

    if (result == 0) {
        Com_DPrintf("%s: unknown scancode %d\n", __func__, event->keysym.scancode);
        return;
    }

    if (result == K_LALT || result == K_RALT)
        Key_Event(K_ALT, event->state, event->timestamp);
    else if (result == K_LCTRL || result == K_RCTRL)
        Key_Event(K_CTRL, event->state, event->timestamp);
    else if (result == K_LSHIFT || result == K_RSHIFT)
        Key_Event(K_SHIFT, event->state, event->timestamp);

    Key_Event(result, event->state, event->timestamp);
}

static void mouse_button_event(SDL_MouseButtonEvent *event)
{
    unsigned key;

    switch (event->button) {
    case SDL_BUTTON_LEFT:
        key = K_MOUSE1;
        break;
    case SDL_BUTTON_RIGHT:
        key = K_MOUSE2;
        break;
    case SDL_BUTTON_MIDDLE:
        key = K_MOUSE3;
        break;
    case SDL_BUTTON_X1:
        key = K_MOUSE4;
        break;
    case SDL_BUTTON_X2:
        key = K_MOUSE5;
        break;
    default:
        Com_DPrintf("%s: unknown button %d\n", __func__, event->button);
        return;
    }

    Key_Event(key, event->state, event->timestamp);
}

static void mouse_wheel_event(SDL_MouseWheelEvent *event)
{
    if (event->x > 0) {
        Key_Event(K_MWHEELRIGHT, qtrue, event->timestamp);
        Key_Event(K_MWHEELRIGHT, qfalse, event->timestamp);
    } else if (event->x < 0) {
        Key_Event(K_MWHEELLEFT, qtrue, event->timestamp);
        Key_Event(K_MWHEELLEFT, qfalse, event->timestamp);
    }

    if (event->y > 0) {
        Key_Event(K_MWHEELUP, qtrue, event->timestamp);
        Key_Event(K_MWHEELUP, qfalse, event->timestamp);
    } else if (event->y < 0) {
        Key_Event(K_MWHEELDOWN, qtrue, event->timestamp);
        Key_Event(K_MWHEELDOWN, qfalse, event->timestamp);
    }
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
        case SDL_QUIT:
            Com_Quit(NULL, ERR_DISCONNECT);
            break;
        case SDL_WINDOWEVENT:
            window_event(&event.window);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            key_event(&event.key);
            break;
        case SDL_MOUSEMOTION:
            UI_MouseEvent(event.motion.x, event.motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            mouse_button_event(&event.button);
            break;
        case SDL_MOUSEWHEEL:
            mouse_wheel_event(&event.wheel);
            break;
        }
    }
}

/*
===============================================================================

MOUSE

===============================================================================
*/

static qboolean GetMouseMotion(int *dx, int *dy)
{
    if (!SDL_GetRelativeMouseMode()) {
        return qfalse;
    }
    SDL_GetRelativeMouseState(dx, dy);
    return qtrue;
}

static void WarpMouse(int x, int y)
{
    SDL_WarpMouseInWindow(sdl_window, x, y);
    SDL_GetRelativeMouseState(NULL, NULL);
}

static void ShutdownMouse(void)
{
    SDL_SetWindowGrab(sdl_window, SDL_FALSE);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
}

static qboolean InitMouse(void)
{
    if (SDL_WasInit(SDL_INIT_VIDEO) != SDL_INIT_VIDEO) {
        return qfalse;
    }

    Com_Printf("SDL mouse initialized.\n");
    return qtrue;
}

static void GrabMouse(qboolean grab)
{
    SDL_bool relative = grab && !(Key_GetDest() & KEY_MENU);
    int cursor = (sdl_flags & QVF_FULLSCREEN) ? SDL_DISABLE : SDL_ENABLE;

    SDL_SetWindowGrab(sdl_window, (SDL_bool)grab);
    SDL_SetRelativeMouseMode(relative);
    SDL_GetRelativeMouseState(NULL, NULL);
    SDL_ShowCursor(cursor);
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
