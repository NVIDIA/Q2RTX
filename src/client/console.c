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
// console.c

#include "client.h"

#define CON_TIMES       16
#define CON_TIMES_MASK  (CON_TIMES - 1)

#define CON_TOTALLINES          1024    // total lines in console scrollback
#define CON_TOTALLINES_MASK     (CON_TOTALLINES - 1)

#define CON_LINEWIDTH   126     // fixed width, do not need more

typedef enum {
    CHAT_NONE,
    CHAT_DEFAULT,
    CHAT_TEAM
} chatMode_t;

typedef enum {
    CON_POPUP,
    CON_DEFAULT,
    CON_REMOTE
} consoleMode_t;

typedef struct {
    byte    color;
    byte    ts_len;
    char    text[CON_LINEWIDTH];
} consoleLine_t;

typedef struct console_s {
    bool    initialized;

    consoleLine_t   text[CON_TOTALLINES];
    int     current;        // line where next message will be printed
    int     x;              // offset in current line for next print
    int     display;        // bottom of console displays this line
    int     color;
    int     newline;

    int     linewidth;      // characters across screen
    int     vidWidth, vidHeight;
    float   scale;
    color_t ts_color;

    unsigned    times[CON_TIMES];   // cls.realtime time the line was generated
                                    // for transparent notify lines
    bool    skipNotify;

    qhandle_t   backImage;
    qhandle_t   charsetImage;

    float   currentHeight;  // aproaches scr_conlines at scr_conspeed
    float   destHeight;     // 0.0 to 1.0 lines of console to display

    commandPrompt_t chatPrompt;
    commandPrompt_t prompt;

    chatMode_t chat;
    consoleMode_t mode;
    netadr_t remoteAddress;
    char *remotePassword;

    load_state_t loadstate;
} console_t;

static console_t    con;

static cvar_t   *con_notifytime;
static cvar_t   *con_notifylines;
static cvar_t   *con_clock;
static cvar_t   *con_height;
static cvar_t   *con_speed;
static cvar_t   *con_alpha;
static cvar_t   *con_scale;
static cvar_t   *con_font;
static cvar_t   *con_background;
static cvar_t   *con_scroll;
static cvar_t   *con_history;
static cvar_t   *con_timestamps;
static cvar_t   *con_timestampsformat;
static cvar_t   *con_timestampscolor;
static cvar_t   *con_auto_chat;

// ============================================================================

/*
================
Con_SkipNotify
================
*/
void Con_SkipNotify(bool skip)
{
    con.skipNotify = skip;
}

/*
================
Con_ClearTyping
================
*/
void Con_ClearTyping(void)
{
    // clear any typing
    IF_Clear(&con.prompt.inputLine);
    Prompt_ClearState(&con.prompt);
}

/*
================
Con_Close

Instantly removes the console. Unless `force' is true, does not remove the console
if user has typed something into it since the last call to Con_Popup.
================
*/
void Con_Close(bool force)
{
    if (con.mode > CON_POPUP && !force) {
        return;
    }

    // if not connected, console or menu should be up
    if (cls.state < ca_active && !(cls.key_dest & KEY_MENU)) {
        return;
    }

    Con_ClearTyping();
    Con_ClearNotify_f();

    Key_SetDest(cls.key_dest & ~KEY_CONSOLE);

    con.destHeight = con.currentHeight = 0;
    con.mode = CON_POPUP;
    con.chat = CHAT_NONE;
}

/*
================
Con_Popup

Drop to connection screen. Unless `force' is true, does not change console mode to popup.
================
*/
void Con_Popup(bool force)
{
    if (force) {
        con.mode = CON_POPUP;
    }

    Key_SetDest(cls.key_dest | KEY_CONSOLE);
    Con_RunConsole();
}

/*
================
Con_ToggleConsole_f

Toggles console up/down animation.
================
*/
static void toggle_console(consoleMode_t mode, chatMode_t chat)
{
    SCR_EndLoadingPlaque();    // get rid of loading plaque

    Con_ClearTyping();
    Con_ClearNotify_f();

    if (cls.key_dest & KEY_CONSOLE) {
        Key_SetDest(cls.key_dest & ~KEY_CONSOLE);
        con.mode = CON_POPUP;
        con.chat = CHAT_NONE;
        return;
    }

    // toggling console discards chat message
    Key_SetDest((cls.key_dest | KEY_CONSOLE) & ~KEY_MESSAGE);
    con.mode = mode;
    con.chat = chat;
}

void Con_ToggleConsole_f(void)
{
    toggle_console(CON_DEFAULT, CHAT_NONE);
}

/*
================
Con_Clear_f
================
*/
static void Con_Clear_f(void)
{
    memset(con.text, 0, sizeof(con.text));
    con.display = con.current;
}

static void Con_Dump_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        FS_File_g("condumps", ".txt", FS_SEARCH_STRIPEXT, ctx);
    }
}

/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
static void Con_Dump_f(void)
{
    int     l;
    qhandle_t f;
    char    name[MAX_OSPATH];

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: %s <filename>\n", Cmd_Argv(0));
        return;
    }

    f = FS_EasyOpenFile(name, sizeof(name), FS_MODE_WRITE | FS_FLAG_TEXT,
                        "condumps/", Cmd_Argv(1), ".txt");
    if (!f) {
        return;
    }

    // skip empty lines
    for (l = con.current - CON_TOTALLINES + 1; l <= con.current; l++) {
        if (con.text[l & CON_TOTALLINES_MASK].text[0]) {
            break;
        }
    }

    // write the remaining lines
    for (; l <= con.current; l++) {
        char buffer[CON_LINEWIDTH + 1];
        char *p = con.text[l & CON_TOTALLINES_MASK].text;
        int i;

        for (i = 0; i < CON_LINEWIDTH && p[i]; i++)
            buffer[i] = Q_charascii(p[i]);
        buffer[i] = '\n';

        FS_Write(buffer, i + 1, f);
    }

    if (FS_CloseFile(f))
        Com_EPrintf("Error writing %s\n", name);
    else
        Com_Printf("Dumped console text to %s.\n", name);
}

/*
================
Con_ClearNotify_f
================
*/
void Con_ClearNotify_f(void)
{
    int     i;

    for (i = 0; i < CON_TIMES; i++)
        con.times[i] = 0;
}

/*
================
Con_MessageMode_f
================
*/
static void start_message_mode(chatMode_t mode)
{
    if (cls.state != ca_active || cls.demo.playback) {
        Com_Printf("You must be in a level to chat.\n");
        return;
    }

    // starting messagemode closes console
    if (cls.key_dest & KEY_CONSOLE) {
        Con_Close(true);
    }

    con.chat = mode;
    IF_Replace(&con.chatPrompt.inputLine, COM_StripQuotes(Cmd_RawArgs()));
    Key_SetDest(cls.key_dest | KEY_MESSAGE);
}

static void Con_MessageMode_f(void)
{
    start_message_mode(CHAT_DEFAULT);
}

static void Con_MessageMode2_f(void)
{
    start_message_mode(CHAT_TEAM);
}

/*
================
Con_RemoteMode_f
================
*/
static void Con_RemoteMode_f(void)
{
    netadr_t adr;
    char *s;

    if (Cmd_Argc() != 3) {
        Com_Printf("Usage: %s <address> <password>\n", Cmd_Argv(0));
        return;
    }

    s = Cmd_Argv(1);
    if (!NET_StringToAdr(s, &adr, PORT_SERVER)) {
        Com_Printf("Bad address: %s\n", s);
        return;
    }

    s = Cmd_Argv(2);

    if (!(cls.key_dest & KEY_CONSOLE)) {
        toggle_console(CON_REMOTE, CHAT_NONE);
    } else {
        con.mode = CON_REMOTE;
        con.chat = CHAT_NONE;
    }

    Z_Free(con.remotePassword);

    con.remoteAddress = adr;
    con.remotePassword = Z_CopyString(s);
}

static void CL_RemoteMode_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        Com_Address_g(ctx);
    }
}

/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize(void)
{
    con.scale = R_ClampScale(con_scale);

    con.vidWidth = Q_rint(r_config.width * con.scale);
    con.vidHeight = Q_rint(r_config.height * con.scale);

    con.linewidth = Q_clip(con.vidWidth / CHAR_WIDTH - 2, 0, CON_LINEWIDTH);
    con.prompt.inputLine.visibleChars = con.linewidth;
    con.prompt.widthInChars = con.linewidth;
    con.chatPrompt.inputLine.visibleChars = con.linewidth;

    if (con_timestamps->integer) {
        char temp[CON_LINEWIDTH];
        con.prompt.widthInChars -= Com_FormatLocalTime(temp, con.linewidth, con_timestampsformat->string);
    }
}

/*
================
Con_CheckTop

Make sure at least one line is visible if console is backscrolled.
================
*/
static void Con_CheckTop(void)
{
    int top = con.current - CON_TOTALLINES + 1;

    if (top < 0) {
        top = 0;
    }
    if (con.display < top) {
        con.display = top;
    }
}

static void con_media_changed(cvar_t *self)
{
    if (con.initialized && cls.ref_initialized) {
        Con_RegisterMedia();
    }
}

static void con_width_changed(cvar_t *self)
{
    if (con.initialized && cls.ref_initialized) {
        Con_CheckResize();
    }
}

static void con_timestampscolor_changed(cvar_t *self)
{
    if (!SCR_ParseColor(self->string, &con.ts_color)) {
        Com_WPrintf("Invalid value '%s' for '%s'\n", self->string, self->name);
        Cvar_Reset(self);
        con.ts_color.u32 = MakeColor(170, 170, 170, 255);
    }
}

static const cmdreg_t c_console[] = {
    { "toggleconsole", Con_ToggleConsole_f },
    { "messagemode", Con_MessageMode_f },
    { "messagemode2", Con_MessageMode2_f },
    { "remotemode", Con_RemoteMode_f, CL_RemoteMode_c },
    { "clear", Con_Clear_f },
    { "clearnotify", Con_ClearNotify_f },
    { "condump", Con_Dump_f, Con_Dump_c },

    { NULL }
};

/*
================
Con_Init
================
*/
void Con_Init(void)
{
    memset(&con, 0, sizeof(con));

//
// register our commands
//
    Cmd_Register(c_console);

    con_notifytime = Cvar_Get("con_notifytime", "3", 0);
    con_notifytime->changed = cl_timeout_changed;
    con_notifytime->changed(con_notifytime);
    con_notifylines = Cvar_Get("con_notifylines", "4", 0);
    con_clock = Cvar_Get("con_clock", "0", 0);
    con_height = Cvar_Get("con_height", "0.5", 0);
    con_speed = Cvar_Get("scr_conspeed", "3", 0);
    con_alpha = Cvar_Get("con_alpha", "1", 0);
    con_scale = Cvar_Get("con_scale", "0", 0);
    con_scale->changed = con_width_changed;
    con_font = Cvar_Get("con_font", "conchars", 0);
    con_font->changed = con_media_changed;
    con_background = Cvar_Get("con_background", "conback", 0);
    con_background->changed = con_media_changed;
    con_scroll = Cvar_Get("con_scroll", "0", 0);
    con_history = Cvar_Get("con_history", "0", 0);
    con_timestamps = Cvar_Get("con_timestamps", "0", 0);
    con_timestamps->changed = con_width_changed;
    con_timestampsformat = Cvar_Get("con_timestampsformat", "%H:%M:%S ", 0);
    con_timestampsformat->changed = con_width_changed;
    con_timestampscolor = Cvar_Get("con_timestampscolor", "#aaa", 0);
    con_timestampscolor->changed = con_timestampscolor_changed;
    con_timestampscolor_changed(con_timestampscolor);
    con_auto_chat = Cvar_Get("con_auto_chat", "0", 0);

    IF_Init(&con.prompt.inputLine, 0, MAX_FIELD_TEXT - 1);
    IF_Init(&con.chatPrompt.inputLine, 0, MAX_FIELD_TEXT - 1);

    con.prompt.printf = Con_Printf;

    // use default width since no video is initialized yet
    r_config.width = 640;
    r_config.height = 480;
    con.linewidth = -1;
    con.scale = 1;
    con.color = COLOR_NONE;
    con.text[0].color = COLOR_NONE;

    Con_CheckResize();

    con.initialized = true;
}

void Con_PostInit(void)
{
    if (con_history->integer > 0) {
        Prompt_LoadHistory(&con.prompt, COM_HISTORYFILE_NAME);
    }
}

/*
================
Con_Shutdown
================
*/
void Con_Shutdown(void)
{
    if (con_history->integer > 0) {
        Prompt_SaveHistory(&con.prompt, COM_HISTORYFILE_NAME, con_history->integer);
    }
    Prompt_Clear(&con.prompt);
}

static void Con_CarriageRet(void)
{
    consoleLine_t *line = &con.text[con.current & CON_TOTALLINES_MASK];

    // add color from last line
    line->color = con.color;

    // add timestamp
    con.x = 0;
    if (con_timestamps->integer)
        con.x = Com_FormatLocalTime(line->text, con.linewidth, con_timestampsformat->string);
    line->ts_len = con.x;

    // init text (must be after timestamp format which may overflow)
    memset(line->text + con.x, 0, CON_LINEWIDTH - con.x);

    // update time for transparent overlay
    if (!con.skipNotify)
        con.times[con.current & CON_TIMES_MASK] = cls.realtime;
}

static void Con_Linefeed(void)
{
    if (con.display == con.current)
        con.display++;
    con.current++;

    Con_CarriageRet();

    if (con_scroll->integer & 2) {
        con.display = con.current;
    } else {
        Con_CheckTop();
    }
}

void Con_SetColor(color_index_t color)
{
    con.color = color;
}

/*
=================
CL_LoadState
=================
*/
void CL_LoadState(load_state_t state)
{
    con.loadstate = state;
    SCR_UpdateScreen();
    if (vid.pump_events)
        vid.pump_events();
}

/*
================
Con_Print

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be displayed on screen
If no console is visible, the text will appear at the top of the game window
================
*/
void Con_Print(const char *txt)
{
    char *p;
    int l;

    if (!con.initialized)
        return;

    while (*txt) {
        if (con.newline) {
            if (con.newline == '\n') {
                Con_Linefeed();
            } else {
                Con_CarriageRet();
            }
            con.newline = 0;
        }

        // count word length
        for (p = (char *)txt; *p > 32; p++)
            ;
        l = p - txt;

        // word wrap
        if (l < con.linewidth && con.x + l > con.linewidth) {
            Con_Linefeed();
        }

        switch (*txt) {
        case '\r':
        case '\n':
            con.newline = *txt;
            break;
        default:    // display character and advance
            if (con.x == con.linewidth) {
                Con_Linefeed();
            }
            p = con.text[con.current & CON_TOTALLINES_MASK].text;
            p[con.x++] = *txt;
            break;
        }

        txt++;
    }
}

/*
================
Con_Printf

Print text to graphical console only,
bypassing system console and logfiles
================
*/
void Con_Printf(const char *fmt, ...)
{
    va_list     argptr;
    char        msg[MAXPRINTMSG];

    va_start(argptr, fmt);
    Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    Con_Print(msg);
}

/*
================
Con_RegisterMedia
================
*/
void Con_RegisterMedia(void)
{
    con.charsetImage = R_RegisterFont(con_font->string);
    if (!con.charsetImage) {
        if (strcmp(con_font->string, con_font->default_string)) {
            Cvar_Reset(con_font);
            con.charsetImage = R_RegisterFont(con_font->default_string);
        }
        if (!con.charsetImage) {
            Com_Error(ERR_FATAL, "%s", Com_GetLastError());
        }
    }

    con.backImage = R_RegisterPic(con_background->string);
    if (!con.backImage) {
        if (strcmp(con_background->string, con_background->default_string)) {
            Cvar_Reset(con_background);
            con.backImage = R_RegisterPic(con_background->default_string);
        }
    }
}

/*
==============================================================================

DRAWING

==============================================================================
*/

static int Con_DrawLine(int v, int row, float alpha)
{
    consoleLine_t *line = &con.text[row & CON_TOTALLINES_MASK];
    char *s = line->text;
    int flags = 0;
    int x = CHAR_WIDTH;
    int w = con.linewidth;

    if (line->ts_len) {
        R_SetColor(con.ts_color.u32);
        R_SetAlpha(alpha);
        x = R_DrawString(x, v, 0, line->ts_len, s, con.charsetImage);
        s += line->ts_len;
        w -= line->ts_len;
    }
    if (w < 1)
        return x;

    switch (line->color) {
    case COLOR_ALT:
        flags = UI_ALTCOLOR;
        // fall through
    case COLOR_NONE:
        R_ClearColor();
        break;
    default:
        R_SetColor(colorTable[line->color & 7]);
        break;
    }
    R_SetAlpha(alpha);

    return R_DrawString(x, v, flags, w, s, con.charsetImage);
}

#define CON_PRESTEP     (CHAR_HEIGHT * 3 + CHAR_HEIGHT / 4)

/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
static void Con_DrawNotify(void)
{
    int     v;
    const char  *text;
    int     i, j;
    unsigned    time;
    int     skip;
    float   alpha;

    // only draw notify in game
    if (cls.state != ca_active) {
        return;
    }
    if (cls.key_dest & (KEY_MENU | KEY_CONSOLE)) {
        return;
    }
    if (con.currentHeight) {
        return;
    }

    j = con_notifylines->integer;
    if (j > CON_TIMES) {
        j = CON_TIMES;
    }

    v = 0;
    for (i = con.current - j + 1; i <= con.current; i++) {
        if (i < 0)
            continue;
        time = con.times[i & CON_TIMES_MASK];
        if (time == 0)
            continue;
        // alpha fade the last string left on screen
        alpha = SCR_FadeAlpha(time, con_notifytime->integer, 300);
        if (!alpha)
            continue;
        if (v || i != con.current) {
            alpha = 1;  // don't fade
        }

        Con_DrawLine(v, i, alpha);

        v += CHAR_HEIGHT;
    }

    R_ClearColor();

    if (cls.key_dest & KEY_MESSAGE) {
        if (con.chat == CHAT_TEAM) {
            text = "say_team:";
            skip = 11;
        } else {
            text = "say:";
            skip = 5;
        }

        R_DrawString(CHAR_WIDTH, v, 0, MAX_STRING_CHARS, text,
                     con.charsetImage);
        con.chatPrompt.inputLine.visibleChars = con.linewidth - skip + 1;
        IF_Draw(&con.chatPrompt.inputLine, skip * CHAR_WIDTH, v,
                UI_DRAWCURSOR, con.charsetImage);
    }
}

/*
================
Con_DrawSolidConsole

Draws the console with the solid background
================
*/
static void Con_DrawSolidConsole(void)
{
    int             i, x, y;
    int             rows;
    const char      *text;
    int             row;
    char            buffer[CON_LINEWIDTH];
    int             vislines;
    float           alpha;
    int             widths[2];

    vislines = con.vidHeight * con.currentHeight;
    if (vislines <= 0)
        return;

    if (vislines > con.vidHeight)
        vislines = con.vidHeight;

// setup transparency
    if (cls.state >= ca_active && !(cls.key_dest & KEY_MENU) && con_alpha->value) {
        alpha = 0.5f + 0.5f * (con.currentHeight / con_height->value);
        R_SetAlpha(alpha * Cvar_ClampValue(con_alpha, 0, 1));
    }

// draw the background
    if (cls.state < ca_active || (cls.key_dest & KEY_MENU) || con_alpha->value) {
        R_DrawKeepAspectPic(0, vislines - con.vidHeight,
                            con.vidWidth, con.vidHeight, con.backImage);
    }

// draw the text
    y = vislines - CON_PRESTEP;
    rows = y / CHAR_HEIGHT + 1;     // rows of text to draw

// draw arrows to show the buffer is backscrolled
    if (con.display != con.current) {
        R_SetColor(U32_RED);
        for (i = 1; i < con.linewidth / 2; i += 4) {
            R_DrawChar(i * CHAR_WIDTH, y, 0, '^', con.charsetImage);
        }

        y -= CHAR_HEIGHT;
        rows--;
    }

// draw from the bottom up
    R_ClearColor();
    row = con.display;
    widths[0] = widths[1] = 0;
    for (i = 0; i < rows; i++) {
        if (row < 0)
            break;
        if (con.current - row > CON_TOTALLINES - 1)
            break;      // past scrollback wrap point

        x = Con_DrawLine(y, row, 1);
        if (i < 2) {
            widths[i] = x;
        }

        y -= CHAR_HEIGHT;
        row--;
    }

    R_ClearColor();

    // draw the download bar
    if (cls.download.current) {
        char pos[16], suf[32];
        int n, j;

        if ((text = strrchr(cls.download.current->path, '/')) != NULL)
            text++;
        else
            text = cls.download.current->path;

        Com_FormatSizeLong(pos, sizeof(pos), cls.download.position);
        n = 4 + Q_scnprintf(suf, sizeof(suf), " %d%% (%s)", cls.download.percent, pos);

        // figure out width
        x = con.linewidth;
        y = x - strlen(text) - n;
        i = x / 3;
        if (strlen(text) > i) {
            y = x - i - n - 3;
            memcpy(buffer, text, i);
            buffer[i] = 0;
            strcat(buffer, "...");
        } else {
            strcpy(buffer, text);
        }
        strcat(buffer, ": ");
        i = strlen(buffer);
        buffer[i++] = '\x80';
        // where's the dot go?
        n = y * cls.download.percent / 100;
        for (j = 0; j < y; j++) {
            if (j == n) {
                buffer[i++] = '\x83';
            } else {
                buffer[i++] = '\x81';
            }
        }
        buffer[i++] = '\x82';
        buffer[i] = 0;

        Q_strlcat(buffer, suf, sizeof(buffer));

        // draw it
        y = vislines - CON_PRESTEP + CHAR_HEIGHT * 2;
        R_DrawString(CHAR_WIDTH, y, 0, con.linewidth, buffer, con.charsetImage);
    } else if (cls.state == ca_loading) {
        // draw loading state
        switch (con.loadstate) {
        case LOAD_MAP:
            text = cl.configstrings[cl.csr.models + 1];
            break;
        case LOAD_MODELS:
            text = "models";
            break;
        case LOAD_IMAGES:
            text = "images";
            break;
        case LOAD_CLIENTS:
            text = "clients";
            break;
        case LOAD_SOUNDS:
            text = "sounds";
            break;
        default:
            text = NULL;
            break;
        }

        if (text) {
            Q_snprintf(buffer, sizeof(buffer), "Loading %s...", text);

            // draw it
            y = vislines - CON_PRESTEP + CHAR_HEIGHT * 2;
            R_DrawString(CHAR_WIDTH, y, 0, con.linewidth, buffer, con.charsetImage);
        }
    }

// draw the input prompt, user text, and cursor if desired
    x = 0;
    if (cls.key_dest & KEY_CONSOLE) {
        y = vislines - CON_PRESTEP + CHAR_HEIGHT;

        // draw command prompt
        i = con.mode == CON_REMOTE ? '#' : 17;
        R_SetColor(U32_YELLOW);
        R_DrawChar(CHAR_WIDTH, y, 0, i, con.charsetImage);
        R_ClearColor();

        // draw input line
        x = IF_Draw(&con.prompt.inputLine, 2 * CHAR_WIDTH, y,
                    UI_DRAWCURSOR, con.charsetImage);
    }

#define APP_VERSION APPLICATION " " LONG_VERSION_STRING
#define VER_WIDTH ((int)(sizeof(APP_VERSION) + 1) * CHAR_WIDTH)

    y = vislines - CON_PRESTEP + CHAR_HEIGHT;
    row = 0;
    // shift version upwards to prevent overdraw
    if (x > con.vidWidth - VER_WIDTH) {
        y -= CHAR_HEIGHT;
        row++;
    }

    R_SetColor(U32_CYAN);

// draw clock
    if (con_clock->integer) {
        x = Com_Time_m(buffer, sizeof(buffer)) * CHAR_WIDTH;
        if (widths[row] + x + CHAR_WIDTH <= con.vidWidth) {
            R_DrawString(con.vidWidth - CHAR_WIDTH - x, y - CHAR_HEIGHT,
                         UI_RIGHT, MAX_STRING_CHARS, buffer, con.charsetImage);
        }
    }

// draw version
    if (!row || widths[0] + VER_WIDTH <= con.vidWidth) {
        SCR_DrawStringEx(con.vidWidth - CHAR_WIDTH, y, UI_RIGHT,
                         MAX_STRING_CHARS, APP_VERSION, con.charsetImage);
    }

    // restore rendering parameters
    R_ClearColor();
}

//=============================================================================

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole(void)
{
    if (cls.disable_screen) {
        con.destHeight = con.currentHeight = 0;
        return;
    }

    if (!(cls.key_dest & KEY_MENU)) {
        if (cls.state == ca_disconnected) {
            // draw fullscreen console
            con.destHeight = con.currentHeight = 1;
            return;
        }
        if (cls.state > ca_disconnected && cls.state < ca_active) {
            // draw half-screen console
            con.destHeight = con.currentHeight = 0.5f;
            return;
        }
    }

// decide on the height of the console
    if (cls.key_dest & KEY_CONSOLE) {
        con.destHeight = Cvar_ClampValue(con_height, 0.1f, 1);
    } else {
        con.destHeight = 0;             // none visible
    }

    if (con_speed->value <= 0) {
        con.currentHeight = con.destHeight;
        return;
    }

    CL_AdvanceValue(&con.currentHeight, con.destHeight, con_speed->value);
}

/*
==================
SCR_DrawConsole
==================
*/
void Con_DrawConsole(void)
{
    R_SetScale(con.scale);
    Con_DrawSolidConsole();
    Con_DrawNotify();
    R_SetScale(1.0f);
}


/*
==============================================================================

            LINE TYPING INTO THE CONSOLE AND COMMAND COMPLETION

==============================================================================
*/

static void Con_Say(char *msg)
{
    CL_ClientCommand(va("say%s \"%s\"", con.chat == CHAT_TEAM ? "_team" : "", msg));
}

// don't close console after connecting
static void Con_InteractiveMode(void)
{
    if (con.mode == CON_POPUP) {
        con.mode = CON_DEFAULT;
    }
}

static void Con_Action(void)
{
    char *cmd = Prompt_Action(&con.prompt);

    Con_InteractiveMode();

    if (!cmd) {
        Con_Printf("]\n");
        return;
    }

    // backslash text are commands, else chat
    int backslash = cmd[0] == '\\' || cmd[0] == '/';

    if (con.mode == CON_REMOTE) {
        CL_SendRcon(&con.remoteAddress, con.remotePassword, cmd + backslash);
    } else {
        if (!backslash && cls.state == ca_active) {
            switch (con_auto_chat->integer) {
            case CHAT_DEFAULT:
                Cbuf_AddText(&cmd_buffer, "cmd say ");
                break;
            case CHAT_TEAM:
                Cbuf_AddText(&cmd_buffer, "cmd say_team ");
                break;
            }
        }
        Cbuf_AddText(&cmd_buffer, cmd + backslash);
        Cbuf_AddText(&cmd_buffer, "\n");
    }

    Con_Printf("]%s\n", cmd);

    if (cls.state == ca_disconnected) {
        // force an update, because the command may take some time
        SCR_UpdateScreen();
    }
}

static void Con_Paste(char *(*func)(void))
{
    char *cbd, *s;

    Con_InteractiveMode();

    if (!func || !(cbd = func())) {
        return;
    }

    s = cbd;
    while (*s) {
        int c = *s++;
        switch (c) {
        case '\n':
            if (*s) {
                Con_Action();
            }
            break;
        case '\r':
        case '\t':
            IF_CharEvent(&con.prompt.inputLine, ' ');
            break;
        default:
            if (!Q_isprint(c)) {
                c = '?';
            }
            IF_CharEvent(&con.prompt.inputLine, c);
            break;
        }
    }

    Z_Free(cbd);
}

// console lines are not necessarily NUL-terminated
static void Con_ClearLine(char *buf, int row)
{
    consoleLine_t *line = &con.text[row & CON_TOTALLINES_MASK];
    char *s = line->text + line->ts_len;
    int w = con.linewidth - line->ts_len;

    while (w-- > 0 && *s)
        *buf++ = *s++ & 127;
    *buf = 0;
}

static void Con_SearchUp(void)
{
    char buf[CON_LINEWIDTH + 1];
    char *s = con.prompt.inputLine.text;
    int top = con.current - CON_TOTALLINES + 1;

    if (top < 0)
        top = 0;

    if (!*s)
        return;

    for (int row = con.display - 1; row >= top; row--) {
        Con_ClearLine(buf, row);
        if (Q_stristr(buf, s)) {
            con.display = row;
            break;
        }
    }
}

static void Con_SearchDown(void)
{
    char buf[CON_LINEWIDTH + 1];
    char *s = con.prompt.inputLine.text;

    if (!*s)
        return;

    for (int row = con.display + 1; row <= con.current; row++) {
        Con_ClearLine(buf, row);
        if (Q_stristr(buf, s)) {
            con.display = row;
            break;
        }
    }
}

/*
====================
Key_Console

Interactive line editing and console scrollback
====================
*/
void Key_Console(int key)
{
    if (key == 'l' && Key_IsDown(K_CTRL)) {
        Con_Clear_f();
        return;
    }

    if (key == 'd' && Key_IsDown(K_CTRL)) {
        con.mode = CON_DEFAULT;
        return;
    }

    if (key == K_ENTER || key == K_KP_ENTER) {
        Con_Action();
        goto scroll;
    }

    if (key == 'v' && Key_IsDown(K_CTRL)) {
        Con_Paste(vid.get_clipboard_data);
        goto scroll;
    }

    if ((key == K_INS && Key_IsDown(K_SHIFT)) || key == K_MOUSE3) {
        Con_Paste(vid.get_selection_data);
        goto scroll;
    }

    if (key == K_TAB) {
        if (con_timestamps->integer)
            Con_CheckResize();
        Prompt_CompleteCommand(&con.prompt, true);
        goto scroll;
    }

    if (key == 'r' && Key_IsDown(K_CTRL)) {
        Prompt_CompleteHistory(&con.prompt, false);
        goto scroll;
    }

    if (key == 's' && Key_IsDown(K_CTRL)) {
        Prompt_CompleteHistory(&con.prompt, true);
        goto scroll;
    }

    if (key == K_UPARROW && Key_IsDown(K_CTRL)) {
        Con_SearchUp();
        return;
    }

    if (key == K_DOWNARROW && Key_IsDown(K_CTRL)) {
        Con_SearchDown();
        return;
    }

    if (key == K_UPARROW || (key == 'p' && Key_IsDown(K_CTRL))) {
        Prompt_HistoryUp(&con.prompt);
        goto scroll;
    }

    if (key == K_DOWNARROW || (key == 'n' && Key_IsDown(K_CTRL))) {
        Prompt_HistoryDown(&con.prompt);
        goto scroll;
    }

    if (key == K_PGUP || key == K_MWHEELUP) {
        if (Key_IsDown(K_CTRL)) {
            con.display -= 6;
        } else {
            con.display -= 2;
        }
        Con_CheckTop();
        return;
    }

    if (key == K_PGDN || key == K_MWHEELDOWN) {
        if (Key_IsDown(K_CTRL)) {
            con.display += 6;
        } else {
            con.display += 2;
        }
        if (con.display > con.current) {
            con.display = con.current;
        }
        return;
    }

    if (key == K_HOME && Key_IsDown(K_CTRL)) {
        con.display = 0;
        Con_CheckTop();
        return;
    }

    if (key == K_END && Key_IsDown(K_CTRL)) {
        con.display = con.current;
        return;
    }

    if (IF_KeyEvent(&con.prompt.inputLine, key)) {
        Prompt_ClearState(&con.prompt);
        Con_InteractiveMode();
    }

scroll:
    if (con_scroll->integer & 1) {
        con.display = con.current;
    }
}

void Char_Console(int key)
{
    if (IF_CharEvent(&con.prompt.inputLine, key)) {
        Con_InteractiveMode();
    }
}

/*
====================
Key_Message
====================
*/
void Key_Message(int key)
{
    if (key == 'l' && Key_IsDown(K_CTRL)) {
        IF_Clear(&con.chatPrompt.inputLine);
        return;
    }

    if (key == K_ENTER || key == K_KP_ENTER) {
        char *cmd = Prompt_Action(&con.chatPrompt);

        if (cmd) {
            Con_Say(cmd);
        }
        Key_SetDest(cls.key_dest & ~KEY_MESSAGE);
        return;
    }

    if (key == K_ESCAPE) {
        Key_SetDest(cls.key_dest & ~KEY_MESSAGE);
        IF_Clear(&con.chatPrompt.inputLine);
        return;
    }

    if (key == 'r' && Key_IsDown(K_CTRL)) {
        Prompt_CompleteHistory(&con.chatPrompt, false);
        return;
    }

    if (key == 's' && Key_IsDown(K_CTRL)) {
        Prompt_CompleteHistory(&con.chatPrompt, true);
        return;
    }

    if (key == K_UPARROW || (key == 'p' && Key_IsDown(K_CTRL))) {
        Prompt_HistoryUp(&con.chatPrompt);
        return;
    }

    if (key == K_DOWNARROW || (key == 'n' && Key_IsDown(K_CTRL))) {
        Prompt_HistoryDown(&con.chatPrompt);
        return;
    }

    if (IF_KeyEvent(&con.chatPrompt.inputLine, key)) {
        Prompt_ClearState(&con.chatPrompt);
    }
}

void Char_Message(int key)
{
    IF_CharEvent(&con.chatPrompt.inputLine, key);
}


