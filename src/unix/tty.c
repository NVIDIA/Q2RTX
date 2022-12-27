/*
Copyright (C) 2012 Andrey Nazarov

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
#include "common/cmd.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/field.h"
#include "common/net/net.h"
#include "common/prompt.h"
#include "system/system.h"
#include "tty.h"

#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>

enum {
    CTRL_A = 1, CTRL_B = 2, CTRL_D = 4, CTRL_E = 5, CTRL_F = 6, CTRL_H = 8,
    TAB = 9, ENTER = 10, CTRL_K = 11, CTRL_L = 12, CTRL_N = 14, CTRL_P = 16,
    CTRL_R = 18, CTRL_S = 19, CTRL_U = 21, CTRL_W = 23, ESC = 27, SPACE = 32,
    DEL = 127
};

static cvar_t           *sys_console;

static bool             tty_enabled;
static struct termios   tty_orig;
static commandPrompt_t  tty_prompt;
static int              tty_hidden;
static ioentry_t        *tty_input;

static void tty_fatal_error(const char *what)
{
    // avoid recursive calls
    sys_console = NULL;
    tty_enabled = false;
    tty_input = NULL;

    Com_Error(ERR_FATAL, "%s: %s() failed: %s",
              __func__, what, strerror(errno));
}

// handles partial writes correctly, but never spins too much
// blocks for 100 ms before giving up and losing data
static void tty_stdout_write(const char *buf, size_t len)
{
    int ret = write(STDOUT_FILENO, buf, len);
    if (ret == len)
        return;

    if (ret < 0 && errno != EAGAIN)
        tty_fatal_error("write");

    if (ret > 0) {
        buf += ret;
        len -= ret;
    }

    unsigned now = Sys_Milliseconds();
    unsigned deadline = now + 100;
    while (now < deadline) {
        struct pollfd fd = {
            .fd = STDOUT_FILENO,
            .events = POLLOUT,
        };

        ret = poll(&fd, 1, deadline - now);
        if (ret == 0)
            break;

        if (ret < 0 && errno != EINTR)
            tty_fatal_error("poll");

        if (ret > 0) {
            ret = write(STDOUT_FILENO, buf, len);
            if (ret == len)
                break;

            if (ret < 0 && errno != EAGAIN)
                tty_fatal_error("write");

            if (ret > 0) {
                buf += ret;
                len -= ret;
            }
        }

        now = Sys_Milliseconds();
    }
}

q_printf(1, 2)
static void tty_stdout_writef(const char *fmt, ...)
{
    char buf[MAX_STRING_CHARS];
    va_list ap;
    size_t len;

    va_start(ap, fmt);
    len = Q_vscnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    tty_stdout_write(buf, len);
}

static int tty_get_width(void)
{
#ifdef TIOCGWINSZ
    struct winsize ws = { 0 };

    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 2)
        return ws.ws_col;
#endif

    return 80;
}

static void tty_hide_input(void)
{
    if (!tty_hidden) {
        // move to start of line, erase from cursor to end of line
        tty_stdout_write(CONST_STR_LEN("\r\033[K"));
    }
    tty_hidden++;
}

static void tty_show_input(void)
{
    if (tty_hidden && !--tty_hidden) {
        inputField_t *f = &tty_prompt.inputLine;
        size_t pos = f->cursorPos;
        char *text = f->text;

        // update line width after resize
        if (!f->visibleChars) {
            tty_prompt.widthInChars = tty_get_width();
            f->visibleChars = tty_prompt.widthInChars - 1;
        }

        // scroll horizontally
        if (pos >= f->visibleChars) {
            pos = f->visibleChars - 1;
            text += f->cursorPos - pos;
        }

        // move to start of line, print prompt and text,
        // move to start of line, forward N chars
        tty_stdout_writef("\r]%.*s\r\033[%zuC", (int)f->visibleChars, text, pos + 1);
    }
}

static void tty_delete(inputField_t *f)
{
    if (f->text[f->cursorPos]) {
        tty_hide_input();
        memmove(f->text + f->cursorPos, f->text + f->cursorPos + 1, sizeof(f->text) - f->cursorPos - 1);
        tty_show_input();
    }
}

static void tty_move_cursor(inputField_t *f, size_t pos)
{
    size_t oldpos = f->cursorPos;
    f->cursorPos = pos = min(pos, f->maxChars - 1);

    if (oldpos < f->visibleChars && pos < f->visibleChars) {
        if (oldpos == pos - 1) {
            // forward one char
            tty_stdout_write("\033[C", 3);
        } else if (oldpos == pos + 1) {
            // backward one char
            tty_stdout_write("\033[D", 3);
        } else {
            // move to start of line, forward N chars
            tty_stdout_writef("\r\033[%zuC", pos + 1);
        }
    } else {
        tty_hide_input();
        tty_show_input();
    }
}

static void tty_move_right(inputField_t *f)
{
    if (f->text[f->cursorPos] && f->cursorPos < f->maxChars - 1) {
        tty_move_cursor(f, f->cursorPos + 1);
    }
}

static void tty_move_left(inputField_t *f)
{
    if (f->cursorPos > 0) {
        tty_move_cursor(f, f->cursorPos - 1);
    }
}

static void tty_history_up(void)
{
    tty_hide_input();
    Prompt_HistoryUp(&tty_prompt);
    tty_show_input();
}

static void tty_history_down(void)
{
    tty_hide_input();
    Prompt_HistoryDown(&tty_prompt);
    tty_show_input();
}

static void tty_parse_input(const char *text)
{
    inputField_t *f = &tty_prompt.inputLine;
    size_t pos;
    int key;
    char *s;

    while (*text) {
        key = *text++;

        switch (key) {
        case CTRL_A:
            tty_move_cursor(f, 0);
            break;
        case CTRL_E:
            tty_move_cursor(f, strlen(f->text));
            break;

        case CTRL_B:
            tty_move_left(f);
            break;
        case CTRL_F:
            tty_move_right(f);
            break;

        case CTRL_D:
            tty_delete(f);
            break;

        case CTRL_H:
        case DEL:
            if (f->cursorPos > 0) {
                if (f->text[f->cursorPos] == 0 && f->cursorPos < f->visibleChars) {
                    f->text[--f->cursorPos] = 0;
                    tty_stdout_write("\b \b", 3);
                } else {
                    tty_hide_input();
                    memmove(f->text + f->cursorPos - 1, f->text + f->cursorPos, sizeof(f->text) - f->cursorPos);
                    f->cursorPos--;
                    tty_show_input();
                }
            }
            break;

        case CTRL_W:
            pos = f->cursorPos;
            while (pos > 0 && f->text[pos - 1] <= SPACE) {
                pos--;
            }
            while (pos > 0 && f->text[pos - 1] > SPACE) {
                pos--;
            }
            if (pos < f->cursorPos) {
                tty_hide_input();
                memmove(f->text + pos, f->text + f->cursorPos, sizeof(f->text) - f->cursorPos);
                f->cursorPos = pos;
                tty_show_input();
            }
            break;

        case CTRL_U:
            if (f->cursorPos > 0) {
                tty_hide_input();
                memmove(f->text, f->text + f->cursorPos, sizeof(f->text) - f->cursorPos);
                f->cursorPos = 0;
                tty_show_input();
            }
            break;

        case CTRL_K:
            if (f->text[f->cursorPos]) {
                f->text[f->cursorPos] = 0;
                // erase from cursor to end of line
                tty_stdout_write("\033[K", 3);
            }
            break;

        case CTRL_L:
            tty_hide_input();
            // move cursor to top left corner, erase screen
            tty_stdout_write(CONST_STR_LEN("\033[H\033[2J"));
            tty_show_input();
            break;

        case CTRL_N:
            tty_history_down();
            break;
        case CTRL_P:
            tty_history_up();
            break;

        case CTRL_R:
            tty_hide_input();
            Prompt_CompleteHistory(&tty_prompt, false);
            tty_show_input();
            break;

        case CTRL_S:
            tty_hide_input();
            Prompt_CompleteHistory(&tty_prompt, true);
            tty_show_input();
            break;

        case SPACE ... DEL - 1:
            if (f->cursorPos == f->maxChars - 1) {
                // buffer limit reached, replace the character under cursor.
                // when cursor is at the rightmost column, terminal may or may
                // not advance it. force absolute position to keep it in the
                // same place.
                tty_stdout_writef("%c\r\033[%zuC", key, f->cursorPos + 1);
                f->text[f->cursorPos + 0] = key;
                f->text[f->cursorPos + 1] = 0;
            } else if (f->text[f->cursorPos] == 0 && f->cursorPos + 1 < f->visibleChars) {
                tty_stdout_write(&(char){ key }, 1);
                f->text[f->cursorPos + 0] = key;
                f->text[f->cursorPos + 1] = 0;
                f->cursorPos++;
            } else {
                tty_hide_input();
                memmove(f->text + f->cursorPos + 1, f->text + f->cursorPos, sizeof(f->text) - f->cursorPos - 1);
                f->text[f->cursorPos++] = key;
                f->text[f->maxChars] = 0;
                tty_show_input();
            }
            break;

        case ENTER:
            tty_hide_input();
            s = Prompt_Action(&tty_prompt);
            if (s) {
                if (*s == '\\' || *s == '/') {
                    s++;
                }
                Sys_Printf("]%s\n", s);
                Cbuf_AddText(&cmd_buffer, s);
                Cbuf_AddText(&cmd_buffer, "\n");
            } else {
                tty_stdout_write("]\n", 2);
            }
            tty_show_input();
            break;

        case TAB:
            tty_hide_input();
            Prompt_CompleteCommand(&tty_prompt, false);
            tty_show_input();
            break;

        case ESC:
            while (*text == ESC)
                text++;
            if (!*text)
                return;
            key = Q_toupper(*text++);
            switch (key) {
            case 'B':
                pos = f->cursorPos;
                while (pos > 0 && f->text[pos - 1] <= SPACE) {
                    pos--;
                }
                while (pos > 0 && f->text[pos - 1] > SPACE) {
                    pos--;
                }
                tty_move_cursor(f, pos);
                break;

            case 'F':
                pos = f->cursorPos;
                while (f->text[pos] && f->text[pos] <= SPACE) {
                    pos++;
                }
                while (f->text[pos] > SPACE) {
                    pos++;
                }
                tty_move_cursor(f, pos);
                break;

            case 'O':
                while (Q_isdigit(*text))
                    text++;
                if (!*text)
                    return;
                key = Q_toupper(*text++);
                switch (key) {
                case 'A':
                    tty_history_up();
                    break;
                case 'B':
                    tty_history_down();
                    break;
                case 'C':
                    tty_move_right(f);
                    break;
                case 'D':
                    tty_move_left(f);
                    break;
                }
                break;

            case '[':
            csi:
                if (!*text)
                    return;
                key = Q_toupper(*text++);
                switch (key) {
                case 'A':
                    tty_history_up();
                    break;
                case 'B':
                    tty_history_down();
                    break;
                case 'C':
                    tty_move_right(f);
                    break;
                case 'D':
                    tty_move_left(f);
                    break;
                case 'F':
                    tty_move_cursor(f, strlen(f->text));
                    break;
                case 'H':
                    tty_move_cursor(f, 0);
                    break;
                case '0' ... '9':
                    key = strtoul(text - 1, &s, 10);
                    if (*s == ';') {
                        strtoul(s + 1, &s, 10);
                        if (*s != '~') {
                            text = s;
                            goto csi;
                        }
                    }
                    if (!*s)
                        return;
                    text = s + 1;
                    switch (key) {
                    case 3:
                        tty_delete(f);
                        break;
                    case 1:
                    case 7:
                        tty_move_cursor(f, 0);
                        break;
                    case 4:
                    case 8:
                        tty_move_cursor(f, strlen(f->text));
                        break;
                    }
                    break;
                case '[':
                    if (*text)
                        text++;
                    break;
                }
                break;
            }
            break;
        }
    }
}

static void q_unused winch_handler(int signum)
{
    tty_prompt.inputLine.visibleChars = 0;  // force refresh
}

void tty_init_input(void)
{
    bool is_tty = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    const char *def = is_tty ? "2" : COM_DEDICATED ? "1" : "0";

    // hide client stdout by default if not launched from TTY
    sys_console = Cvar_Get("sys_console", def, CVAR_NOSET);
    if (sys_console->integer == 0)
        return;

    // change stdin/stdout to non-blocking
    Sys_SetNonBlock(STDIN_FILENO, true);
    Sys_SetNonBlock(STDOUT_FILENO, true);

    // add stdin to the list of descriptors to wait on
    tty_input = NET_AddFd(STDIN_FILENO);
    tty_input->wantread = true;

    if (sys_console->integer == 1)
        return;

    // init optional TTY support
    if (!is_tty)
        goto no_tty;

    char *term = getenv("TERM");
    if (!term || !*term || !strcmp(term, "dumb"))
        goto no_tty;

    if (tcgetattr(STDIN_FILENO, &tty_orig))
        goto no_tty;

    struct termios tty = tty_orig;
    tty.c_iflag &= ~(INPCK | ISTRIP | IXON);
    tty.c_lflag &= ~(ICANON | ECHO);
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &tty))
        goto no_tty;

#ifdef TIOCGWINSZ
    signal(SIGWINCH, winch_handler);
#endif

    // determine terminal width
    int width = tty_get_width();
    tty_prompt.widthInChars = width;
    tty_prompt.printf = Sys_Printf;
    tty_enabled = true;

    // figure out input line width
    IF_Init(&tty_prompt.inputLine, width - 1, MAX_FIELD_TEXT - 1);

    // display command prompt
    tty_stdout_write("]", 1);
    return;

no_tty:
    Com_Printf("Couldn't initialize TTY support.\n");
    Cvar_Set("sys_console", "1");
}

static void tty_kill_stdin(void)
{
    if (tty_input) {
        NET_RemoveFd(STDIN_FILENO);
        tty_input = NULL;
    }
    if (tty_enabled) {
        tty_hide_input();
        tcsetattr(STDIN_FILENO, TCSADRAIN, &tty_orig);
        tty_enabled = false;
    }
    Cvar_Set("sys_console", "1");
}

void tty_shutdown_input(void)
{
    tty_kill_stdin();
    if (sys_console && sys_console->integer) {
        Sys_SetNonBlock(STDIN_FILENO, false);
        Sys_SetNonBlock(STDOUT_FILENO, false);
    }
    Cvar_Set("sys_console", "0");
}

void Sys_RunConsole(void)
{
    char text[MAX_STRING_CHARS];
    int ret;

    if (!sys_console || !sys_console->integer) {
        return;
    }

    if (!tty_input || !tty_input->canread) {
        return;
    }

    ret = read(STDIN_FILENO, text, sizeof(text) - 1);
    if (!ret) {
        Com_DPrintf("Read EOF from stdin.\n");
        tty_kill_stdin();
        return;
    }

    // make sure the next call will not block
    tty_input->canread = false;

    if (ret < 0) {
        if (errno == EAGAIN || errno == EIO) {
            return;
        }
        tty_fatal_error("read");
    }

    text[ret] = 0;

    if (!tty_enabled) {
        Cbuf_AddText(&cmd_buffer, text);
        return;
    }

    tty_parse_input(text);
}

void Sys_ConsoleOutput(const char *text, size_t len)
{
    if (!sys_console || !sys_console->integer) {
        return;
    }

    if (!len) {
        return;
    }

    if (!tty_enabled) {
        tty_stdout_write(text, len);
    } else {
        static bool hack = false;

        if (!hack) {
            tty_hide_input();
            hack = true;
        }

        tty_stdout_write(text, len);

        if (text[len - 1] == '\n') {
            tty_show_input();
            hack = false;
        }
    }
}

void Sys_SetConsoleTitle(const char *title)
{
    char buf[MAX_STRING_CHARS];
    size_t len;

    if (!sys_console || !sys_console->integer) {
        return;
    }

    if (!tty_enabled) {
        return;
    }

    buf[0] = '\033';
    buf[1] = ']';
    buf[2] = '0';
    buf[3] = ';';

    for (len = 4; len < MAX_STRING_CHARS - 1; len++) {
        int c = *title++;
        if (!c) {
            break;
        }
        buf[len] = Q_charascii(c);
    }

    buf[len++] = '\007';

    tty_stdout_write(buf, len);
}

void Sys_SetConsoleColor(color_index_t color)
{
    char buf[5];
    size_t len;

    if (!sys_console || !sys_console->integer) {
        return;
    }

    if (!tty_enabled) {
        return;
    }

    buf[0] = '\033';
    buf[1] = '[';
    switch (color) {
    case COLOR_NONE:
        buf[2] = '0';
        buf[3] = 'm';
        len = 4;
        break;
    case COLOR_ALT:
        buf[2] = '3';
        buf[3] = '2';
        buf[4] = 'm';
        len = 5;
        break;
    default:
        buf[2] = '3';
        buf[3] = "01234657"[color & 7];
        buf[4] = 'm';
        len = 5;
        break;
    }

    if (color != COLOR_NONE) {
        tty_hide_input();
    }
    tty_stdout_write(buf, len);
    if (color == COLOR_NONE) {
        tty_show_input();
    }
}

void Sys_Printf(const char *fmt, ...)
{
    va_list     argptr;
    char        msg[MAXPRINTMSG];
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vscnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    Sys_ConsoleOutput(msg, len);
}

