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
#include <errno.h>

static cvar_t           *sys_console;

static qboolean         tty_enabled;
static struct termios   tty_orig;
static commandPrompt_t  tty_prompt;
static int              tty_hidden;
static ioentry_t        *tty_io;

static void tty_fatal_error(const char *what)
{
    // avoid recursive calls
    sys_console = NULL;
    tty_enabled = qfalse;
    tty_io = NULL;

    Com_Error(ERR_FATAL, "%s: %s() failed: %s",
              __func__, what, strerror(errno));
}

static int tty_stdout_sleep(void)
{
    struct timeval tv;
    fd_set fd;

    tv.tv_sec = 0;
    tv.tv_usec = 10000;

    FD_ZERO(&fd);
    FD_SET(STDOUT_FILENO, &fd);

    return select(STDOUT_FILENO + 1, NULL, &fd, NULL, &tv);
}

// handles partial writes correctly, but never spins too much
// blocks for 100 ms before giving up and losing data
static void tty_stdout_write(const char *buf, size_t len)
{
    int ret, spins;

    for (spins = 0; len && spins < 10; spins++) {
        ret = write(STDOUT_FILENO, buf, len);
        if (ret < 0) {
            if (errno == EAGAIN) {
                ret = tty_stdout_sleep();
                if (ret >= 0 || errno == EINTR)
                    continue;
                tty_fatal_error("select");
            } else {
                tty_fatal_error("write");
            }

        }

        buf += ret;
        len -= ret;
    }
}

static void tty_hide_input(void)
{
    int i;

    if (!tty_hidden) {
        for (i = 0; i <= tty_prompt.inputLine.cursorPos; i++) {
            tty_stdout_write("\b \b", 3);
        }
    }
    tty_hidden++;
}

static void tty_show_input(void)
{
    if (!tty_hidden) {
        return;
    }

    tty_hidden--;
    if (!tty_hidden) {
        tty_stdout_write("]", 1);
        tty_stdout_write(tty_prompt.inputLine.text,
                     tty_prompt.inputLine.cursorPos);
    }
}

static void tty_write_output(const char *text)
{
    char    buf[MAXPRINTMSG];
    size_t  len;

    for (len = 0; len < MAXPRINTMSG; len++) {
        int c = *text++;
        if (!c) {
            break;
        }
        buf[len] = Q_charascii(c);
    }

    tty_stdout_write(buf, len);
}

static void tty_parse_input(const char *text)
{
    inputField_t *f;
    char *s;
    int i, key;

    f = &tty_prompt.inputLine;
    while (*text) {
        key = *text++;

        if (key == tty_orig.c_cc[VERASE] || key == 127 || key == 8) {
            if (f->cursorPos) {
                f->text[--f->cursorPos] = 0;
                tty_stdout_write("\b \b", 3);
            }
            continue;
        }

        if (key == tty_orig.c_cc[VKILL]) {
            for (i = 0; i < f->cursorPos; i++) {
                tty_stdout_write("\b \b", 3);
            }
            f->cursorPos = 0;
            continue;
        }

        if (key >= 32) {
            if (f->cursorPos == f->maxChars - 1) {
                tty_stdout_write(va("\b \b%c", key), 4);
                f->text[f->cursorPos + 0] = key;
                f->text[f->cursorPos + 1] = 0;
            } else {
                tty_stdout_write(va("%c", key), 1);
                f->text[f->cursorPos + 0] = key;
                f->text[f->cursorPos + 1] = 0;
                f->cursorPos++;
            }
            continue;
        }

        if (key == '\n') {
            tty_hide_input();
            s = Prompt_Action(&tty_prompt);
            if (s) {
                if (*s == '\\' || *s == '/') {
                    s++;
                }
                Sys_Printf("]%s\n", s);
                Cbuf_AddText(&cmd_buffer, s);
            } else {
                tty_stdout_write("]\n", 2);
            }
            tty_show_input();
            continue;
        }

        if (key == '\t') {
            tty_hide_input();
            Prompt_CompleteCommand(&tty_prompt, qfalse);
            f->cursorPos = strlen(f->text);   // FIXME
            tty_show_input();
            continue;
        }

        if (*text) {
            key = *text++;
            if (key == '[' || key == 'O') {
                if (*text) {
                    key = *text++;
                    switch (key) {
                    case 'A':
                        tty_hide_input();
                        Prompt_HistoryUp(&tty_prompt);
                        tty_show_input();
                        break;
                    case 'B':
                        tty_hide_input();
                        Prompt_HistoryDown(&tty_prompt);
                        tty_show_input();
                        break;
#if 0
                    case 'C':
                        if (f->text[f->cursorPos]) {
                            tty_stdout_write("\033[C", 3);
                            f->cursorPos++;
                        }
                        break;
                    case 'D':
                        if (f->cursorPos) {
                            tty_stdout_write("\033[D", 3);
                            f->cursorPos--;
                        }
                        break;
#endif
                    }
                }
            }
        }
    }
}

static int tty_get_width(void)
{
#ifdef TIOCGWINSZ
    struct winsize ws;

    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
#endif

    return 80;
}

static void tty_make_nonblock(int fd, int nb)
{
    int ret = fcntl(fd, F_GETFL, 0);
    if (ret != -1 && !!(ret & O_NONBLOCK) != nb)
        fcntl(fd, F_SETFL, ret ^ O_NONBLOCK);
}

qboolean tty_init_input(void)
{
    struct termios tty;
    int width, is_tty = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

    // we want TTY support enabled if started from terminal, but don't want any
    // output by default if launched without one (from X session for example)
    sys_console = Cvar_Get("sys_console", is_tty ? "2" : "0", CVAR_NOSET);
    if (sys_console->integer == 0)
        return qfalse;

    // change stdin/stdout to non-blocking
    tty_make_nonblock(STDIN_FILENO,  1);
    tty_make_nonblock(STDOUT_FILENO, 1);

    // add stdin to the list of descriptors to wait on
    tty_io = NET_AddFd(STDIN_FILENO);
    tty_io->wantread = qtrue;

    if (sys_console->integer == 1)
        goto no_tty1;

    // init optional TTY support
    if (!is_tty)
        goto no_tty2;

    if (tcgetattr(STDIN_FILENO, &tty_orig))
        goto no_tty2;

    tty = tty_orig;
    tty.c_iflag &= ~(INPCK | ISTRIP);
    tty.c_lflag &= ~(ICANON | ECHO);
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSADRAIN, &tty))
        goto no_tty2;

    // determine terminal width
    width = tty_get_width();
    tty_prompt.widthInChars = width;
    tty_prompt.printf = Sys_Printf;
    tty_enabled = qtrue;

    // figure out input line width
    width--;
    if (width > MAX_FIELD_TEXT - 1)
        width = MAX_FIELD_TEXT - 1;
    IF_Init(&tty_prompt.inputLine, width, width);

    // display command prompt
    tty_stdout_write("]", 1);
    return qtrue;

no_tty2:
    Com_Printf("Couldn't initialize TTY support.\n");
    Cvar_Set("sys_console", "1");
no_tty1:
    return qtrue;
}

void tty_shutdown_input(void)
{
    if (sys_console && sys_console->integer) {
        tty_make_nonblock(STDIN_FILENO,  0);
        tty_make_nonblock(STDOUT_FILENO, 0);
    }
    if (tty_io) {
        NET_RemoveFd(STDIN_FILENO);
        tty_io = NULL;
    }
    if (tty_enabled) {
        tty_hide_input();
        tcsetattr(STDIN_FILENO, TCSADRAIN, &tty_orig);
        tty_enabled = qfalse;
    }
}

void Sys_RunConsole(void)
{
    char text[MAX_STRING_CHARS];
    ssize_t ret;

    if (!sys_console || !sys_console->integer) {
        return;
    }

    if (!tty_io || !tty_io->canread) {
        return;
    }

    ret = read(STDIN_FILENO, text, sizeof(text) - 1);
    if (!ret) {
        Com_DPrintf("Read EOF from stdin.\n");
        tty_shutdown_input();
        Cvar_Set("sys_console", "0");
        return;
    }

    // make sure the next call will not block
    tty_io->canread = qfalse;

    if (ret < 0) {
        if (errno == EAGAIN) {
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

void Sys_ConsoleOutput(const char *text)
{
    if (!sys_console || !sys_console->integer) {
        return;
    }

    if (!tty_enabled) {
        tty_write_output(text);
    } else {
        tty_hide_input();
        tty_write_output(text);
        tty_show_input();
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
    static const char color_to_ansi[8] = {
        '0', '1', '2', '3', '4', '6', '5', '7'
    };
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
        buf[3] = color_to_ansi[color];
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

    va_start(argptr, fmt);
    Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    Sys_ConsoleOutput(msg);
}

