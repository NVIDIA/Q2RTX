/*
Copyright (C) 2013 Andrey Nazarov
Copyright (C) 2019, NVIDIA CORPORATION. All rights reserved.

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
#include <SDL2/SDL_vulkan.h>

#ifdef _WINDOWS
#include <ShellScalingAPI.h>

typedef HRESULT(__stdcall *PFN_SetProcessDpiAwareness_t)(_In_ PROCESS_DPI_AWARENESS value);
PFN_SetProcessDpiAwareness_t PFN_SetProcessDpiAwareness = NULL;
HMODULE h_ShCoreDLL = 0;
#endif

static struct {
    SDL_Window      *window;
#if REF_GL
    SDL_GLContext   *context;
#endif
    vidFlags_t      flags;

    int             width;
    int             height;
    int             win_width;
    int             win_height;

    bool            wayland;
    int             focus_hack;
} sdl;

extern cvar_t* vid_display;
extern cvar_t* vid_displaylist;

SDL_Window* get_sdl_window(void)
{
    return sdl.window;
}

/*
===============================================================================

OPENGL STUFF

===============================================================================
*/

#if REF_GL

static void set_gl_attributes(void)
{
    r_opengl_config_t *cfg = R_GetGLConfig();

    int colorbits = cfg->colorbits > 16 ? 8 : 5;
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, colorbits);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, colorbits);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, colorbits);

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, cfg->depthbits);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, cfg->stencilbits);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    if (cfg->multisamples) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, cfg->multisamples);
    }
    if (cfg->debug) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    }

#if USE_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
}

static void *get_proc_addr(const char *sym)
{
    return SDL_GL_GetProcAddress(sym);
}

static void swap_buffers(void)
{
    SDL_GL_SwapWindow(sdl.window);
}

static void swap_interval(int val)
{
    if (SDL_GL_SetSwapInterval(val) < 0)
        Com_EPrintf("Couldn't set swap interval %d: %s\n", val, SDL_GetError());
}
#endif

/*
===============================================================================

VIDEO

===============================================================================
*/

static void mode_changed(void)
{
    SDL_GetWindowSize(sdl.window, &sdl.win_width, &sdl.win_height);

    Uint32 flags = SDL_GetWindowFlags(sdl.window);
    if (flags & SDL_WINDOW_VULKAN)
        SDL_Vulkan_GetDrawableSize(sdl.window, &sdl.width, &sdl.height);
    else
        SDL_GL_GetDrawableSize(sdl.window, &sdl.width, &sdl.height);

    if (flags & SDL_WINDOW_FULLSCREEN)
        sdl.flags |= QVF_FULLSCREEN;
    else
        sdl.flags &= ~QVF_FULLSCREEN;

    R_ModeChanged(sdl.width, sdl.height, sdl.flags);
    SCR_ModeChanged();
}

static void set_mode(void)
{
    Uint32 flags;
    vrect_t rc;
    int freq;

    if (vid_fullscreen->integer) {
        // move the window onto the selected display
        SDL_Rect display_bounds;
        SDL_GetDisplayBounds(vid_display->integer, &display_bounds);
        SDL_SetWindowPosition(sdl.window, display_bounds.x, display_bounds.y);

        if (VID_GetFullscreen(&rc, &freq, NULL)) {
            SDL_DisplayMode mode = {
                .format         = SDL_PIXELFORMAT_UNKNOWN,
                .w              = rc.width,
                .h              = rc.height,
                .refresh_rate   = freq,
                .driverdata     = NULL
            };
            SDL_SetWindowDisplayMode(sdl.window, &mode);
            flags = SDL_WINDOW_FULLSCREEN;
        } else {
            flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
        }
    } else {
        if (VID_GetGeometry(&rc)) {
            SDL_SetWindowSize(sdl.window, rc.width, rc.height);
            SDL_SetWindowPosition(sdl.window, rc.x, rc.y);
        }
        flags = 0;
    }

    SDL_SetWindowFullscreen(sdl.window, flags);
    mode_changed();
}

static void fatal_shutdown(void)
{
    SDL_SetWindowGrab(sdl.window, SDL_FALSE);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
    SDL_Quit();
}

static char *get_clipboard_data(void)
{
    // returned data must be Z_Free'd
    char *text = SDL_GetClipboardText();
    char *copy = Z_CopyString(text);
    SDL_free(text);
    return copy;
}

static void set_clipboard_data(const char *data)
{
    SDL_SetClipboardText(data);
}

static void update_gamma(const byte *table)
{
    Uint16 ramp[256];
    int i;

    if (sdl.flags & QVF_GAMMARAMP) {
        for (i = 0; i < 256; i++) {
            ramp[i] = table[i] << 8;
        }
        SDL_SetWindowGammaRamp(sdl.window, ramp, ramp, ramp);
    }
}

static int my_event_filter(void *userdata, SDL_Event *event)
{
    // SDL uses relative time, we need absolute
    event->common.timestamp = Sys_Milliseconds();
    return 1;
}

static void VID_GetDisplayList(void)
{
    int num_displays = SDL_GetNumVideoDisplays();
    int string_size = 0;
    int max_display_name_length = 0;

    for (int display = 0; display < num_displays; display++)
    {
        int len = strlen(SDL_GetDisplayName(display));
        max_display_name_length = max(max_display_name_length, len);
        string_size += 12 + len;
    }
    
    string_size += 1;
    max_display_name_length += 1;

    char *display_list = Z_Malloc(string_size);
    char *display_name = Z_Malloc(max_display_name_length);
    display_list[0] = 0;

    for (int display = 0; display < num_displays; display++)
    {
        Q_strlcpy(display_name, SDL_GetDisplayName(display), max_display_name_length);

        // remove quotes from the display name to avoid weird parsing errors later
        for (char* p = display_name; *p; p++)
        {
            if (*p == '\"')
                *p = '\'';
        }

        // Append something like `"1 (Generic PnP Monitor)" 0` to the display list,
        // where the first quoted field is the UI string,
        // and the second field is the value for vid_display.
        // Add 1 to the display index because that's how Windows control panel shows the displays.
        Q_strlcat(display_list, va("\"%d (%s)\" %d ", display + 1, display_name, display), string_size);
    }

    Cvar_SetByVar(vid_displaylist, display_list, FROM_CODE);

    Z_Free(display_list);
    Z_Free(display_name);
}

static char *get_mode_list(void)
{
    SDL_DisplayMode mode;
    size_t size, len;
    char *buf;
    int i, num_modes;

    Cvar_ClampInteger(vid_display, 0, SDL_GetNumVideoDisplays() - 1);

    VID_GetDisplayList();

    num_modes = SDL_GetNumDisplayModes(vid_display->integer);
    if (num_modes < 1)
        return Z_CopyString(VID_MODELIST);

    size = 8 + num_modes * 32 + 1;
    buf = Z_Malloc(size);

    len = Q_strlcpy(buf, "desktop ", size);
    for (i = 0; i < num_modes; i++) {
        if (SDL_GetDisplayMode(vid_display->integer, i, &mode) < 0)
            break;
        if (mode.refresh_rate == 0)
            continue;
        len += Q_scnprintf(buf + len, size - len, "%dx%d@%d ",
                           mode.w, mode.h, mode.refresh_rate);
    }
    buf[len - 1] = 0;

    return buf;
}

static int get_dpi_scale(void)
{
    if (sdl.win_width && sdl.win_height) {
        int scale_x = (sdl.width + sdl.win_width / 2) / sdl.win_width;
        int scale_y = (sdl.height + sdl.win_height / 2) / sdl.win_height;
        if (scale_x == scale_y)
            return Q_clip(scale_x, 1, 10);
    }

    return 1;
}

static void sdl_shutdown(void)
{
#if REF_GL
    if (sdl.context)
        SDL_GL_DeleteContext(sdl.context);
#endif

    if (sdl.window)
        SDL_DestroyWindow(sdl.window);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    memset(&sdl, 0, sizeof(sdl));
}

static bool init(graphics_api_t api)
{
	Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
	vrect_t rc;

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) == -1) {
        Com_EPrintf("Couldn't initialize SDL video: %s\n", SDL_GetError());
        return false;
    }

#if REF_GL
	if (api == GAPI_OPENGL)
	{
    	set_gl_attributes();

		flags |= SDL_WINDOW_OPENGL;
	}
#endif

    SDL_SetEventFilter(my_event_filter, NULL);

	if (!VID_GetGeometry(&rc)) {
		rc.x = SDL_WINDOWPOS_UNDEFINED;
		rc.y = SDL_WINDOWPOS_UNDEFINED;
	}

	if (api == GAPI_VULKAN)
	{
		flags |= SDL_WINDOW_VULKAN;
	}

	sdl.window = SDL_CreateWindow(PRODUCT, rc.x, rc.y, rc.width, rc.height, flags);
    if (!sdl.window) {
        Com_EPrintf("Couldn't create SDL window: %s\n", SDL_GetError());
        goto fail;
    }

    SDL_SetWindowMinimumSize(sdl.window, 320, 240);

	uint32_t icon_rgb[q2icon_height][q2icon_width];
	for (int y = 0; y < q2icon_height; y++)
	{
		for (int x = 0; x < q2icon_height; x++)
		{
			byte b = q2icon_bits[(y * q2icon_width + x) / 8];
			if ((b >> (x & 7)) & 1)
				icon_rgb[y][x] = 0xFF7AB632; // NVIDIA green color
			else
				icon_rgb[y][x] = 0x00000000;
		}
	}

    SDL_Surface *icon = SDL_CreateRGBSurfaceFrom(icon_rgb, q2icon_width, q2icon_height, 32, q2icon_width * sizeof(uint32_t), 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
    if (icon) {
        SDL_SetWindowIcon(sdl.window, icon);
        SDL_FreeSurface(icon);
    }

#if REF_GL
	if (api == GAPI_OPENGL)
	{
		sdl.context = SDL_GL_CreateContext(sdl.window);
		if (!sdl.context) {
			Com_EPrintf("Couldn't create OpenGL context: %s\n", SDL_GetError());
			goto fail;
		}
	}

#endif

    cvar_t *vid_hwgamma = Cvar_Get("vid_hwgamma", "0", CVAR_REFRESH);
    if (vid_hwgamma->integer) {
        Uint16  gamma[3][256];

        if (SDL_GetWindowGammaRamp(sdl.window, gamma[0], gamma[1], gamma[2]) == 0 &&
            SDL_SetWindowGammaRamp(sdl.window, gamma[0], gamma[1], gamma[2]) == 0) {
            Com_Printf("...enabling hardware gamma\n");
            sdl.flags |= QVF_GAMMARAMP;
        } else {
            Com_Printf("...hardware gamma not supported\n");
            Cvar_Reset(vid_hwgamma);
        }
    }

    Com_Printf("Using SDL video driver: %s\n", SDL_GetCurrentVideoDriver());

    // activate disgusting wayland hacks
    sdl.wayland = !strcmp(SDL_GetCurrentVideoDriver(), "wayland");

    return true;

fail:
    sdl_shutdown();
    return false;
}

/*
==========================================================================

EVENTS

==========================================================================
*/

static void window_event(SDL_WindowEvent *event)
{
    Uint32 flags = SDL_GetWindowFlags(sdl.window);
    active_t active;
    vrect_t rc;

    // wayland doesn't set SDL_WINDOW_*_FOCUS flags
    if (sdl.wayland) {
        switch (event->event) {
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            sdl.focus_hack = SDL_WINDOW_INPUT_FOCUS;
            break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            sdl.focus_hack = 0;
            break;
        }
        flags |= sdl.focus_hack;
    }

    switch (event->event) {
    case SDL_WINDOWEVENT_MINIMIZED:
    case SDL_WINDOWEVENT_RESTORED:
    case SDL_WINDOWEVENT_ENTER:
    case SDL_WINDOWEVENT_LEAVE:
    case SDL_WINDOWEVENT_FOCUS_GAINED:
    case SDL_WINDOWEVENT_FOCUS_LOST:
    case SDL_WINDOWEVENT_SHOWN:
    case SDL_WINDOWEVENT_HIDDEN:
        if (flags & (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS)) {
            active = ACT_ACTIVATED;
        } else if (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) {
            active = ACT_MINIMIZED;
        } else {
            active = ACT_RESTORED;
        }
        CL_Activate(active);
        break;

    case SDL_WINDOWEVENT_MOVED:
        if (!(flags & SDL_WINDOW_FULLSCREEN)) {
            SDL_GetWindowSize(sdl.window, &rc.width, &rc.height);
            rc.x = event->data1;
            rc.y = event->data2;
            VID_SetGeometry(&rc);
        }
        break;

    case SDL_WINDOWEVENT_RESIZED:
        if (!(flags & SDL_WINDOW_FULLSCREEN)) {
            SDL_GetWindowPosition(sdl.window, &rc.x, &rc.y);
            rc.width = event->data1;
            rc.height = event->data2;
            VID_SetGeometry(&rc);
        }
        mode_changed();
        break;
    }
}

static const byte scantokey[] = {
    #include "keytables/sdl.h"
};

static const byte scantokey2[] = {
    K_LCTRL, K_LSHIFT, K_LALT, K_LWINKEY, K_RCTRL, K_RSHIFT, K_RALT, K_RWINKEY
};

static void key_event(SDL_KeyboardEvent *event)
{
    unsigned key = event->keysym.scancode;

    if (key < q_countof(scantokey))
        key = scantokey[key];
    else if (key >= SDL_SCANCODE_LCTRL && key < SDL_SCANCODE_LCTRL + q_countof(scantokey2))
        key = scantokey2[key - SDL_SCANCODE_LCTRL];
    else
        key = 0;

    if (!key) {
        Com_DPrintf("%s: unknown scancode %d\n", __func__, event->keysym.scancode);
        return;
    }

    Key_Event2(key, event->state, event->timestamp);
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
        Key_Event(K_MWHEELRIGHT, true, event->timestamp);
        Key_Event(K_MWHEELRIGHT, false, event->timestamp);
    } else if (event->x < 0) {
        Key_Event(K_MWHEELLEFT, true, event->timestamp);
        Key_Event(K_MWHEELLEFT, false, event->timestamp);
    }

    if (event->y > 0) {
        Key_Event(K_MWHEELUP, true, event->timestamp);
        Key_Event(K_MWHEELUP, false, event->timestamp);
    } else if (event->y < 0) {
        Key_Event(K_MWHEELDOWN, true, event->timestamp);
        Key_Event(K_MWHEELDOWN, false, event->timestamp);
    }
}

static void pump_events(void)
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
            if (sdl.win_width && sdl.win_height)
                UI_MouseEvent(event.motion.x * sdl.width / sdl.win_width,
                              event.motion.y * sdl.height / sdl.win_height);
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

static bool get_mouse_motion(int *dx, int *dy)
{
    if (!SDL_GetRelativeMouseMode()) {
        return false;
    }
    SDL_GetRelativeMouseState(dx, dy);
    return true;
}

static void warp_mouse(int x, int y)
{
    SDL_WarpMouseInWindow(sdl.window, x, y);
    SDL_GetRelativeMouseState(NULL, NULL);
}

static void shutdown_mouse(void)
{
    SDL_SetWindowGrab(sdl.window, SDL_FALSE);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_ShowCursor(SDL_ENABLE);
}

static bool init_mouse(void)
{
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        return false;
    }

    Com_Printf("SDL mouse initialized.\n");
    return true;
}

static void grab_mouse(bool grab)
{
    SDL_SetWindowGrab(sdl.window, grab);
    SDL_SetRelativeMouseMode(grab && !(Key_GetDest() & KEY_MENU));
    SDL_GetRelativeMouseState(NULL, NULL);
    SDL_ShowCursor(!(sdl.flags & QVF_FULLSCREEN));
}

static bool probe(void)
{
    return true;
}

const vid_driver_t vid_sdl = {
    .name = "sdl",

    .probe = probe,
    .init = init,
    .shutdown = sdl_shutdown,
    .fatal_shutdown = fatal_shutdown,
    .pump_events = pump_events,

    .get_mode_list = get_mode_list,
    .get_dpi_scale = get_dpi_scale,
    .set_mode = set_mode,
    .update_gamma = update_gamma,

#if REF_GL
    .get_proc_addr = get_proc_addr,
    .swap_buffers = swap_buffers,
    .swap_interval = swap_interval,
#endif

    .get_clipboard_data = get_clipboard_data,
    .set_clipboard_data = set_clipboard_data,

    .init_mouse = init_mouse,
    .shutdown_mouse = shutdown_mouse,
    .grab_mouse = grab_mouse,
    .warp_mouse = warp_mouse,
    .get_mouse_motion = get_mouse_motion,
};
