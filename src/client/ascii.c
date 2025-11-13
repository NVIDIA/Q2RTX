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

#include "client.h"

/*
===============================================================================

STAT PROGRAMS TO TEXT

===============================================================================
*/

#define TH_WIDTH    80
#define TH_HEIGHT   40

static void TH_DrawString(char *dst, int x, int y, char *src, size_t len)
{
    int c;

    if (x + len > TH_WIDTH) {
        len = TH_WIDTH - x;
    }

    dst += y * (TH_WIDTH + 1) + x;
    while (len--) {
        c = *src++;
        c &= 127;
        switch (c) {
        case 13: c = '>'; break;
        case 16: c = '['; break;
        case 17: c = ']'; break;
        case 29: c = '<'; break;
        case 30: c = '='; break;
        case 31: c = '>'; break;
        default:
            if (c < 32) {
                c = 32;
            }
            break;
        }
        *dst++ = c;
    }
}

static void TH_DrawCenterString(char *dst, int x, int y, char *src, size_t len)
{
    x -= len / 2;
    if (x < 0) {
        src -= x;
        x = 0;
    }

    TH_DrawString(dst, x, y, src, len);
}

static void TH_DrawNumber(char *dst, int x, int y, int width, int value)
{
    char num[16];
    int l;

    if (width < 1)
        return;

    // draw number string
    if (width > 5)
        width = 5;

    l = Q_scnprintf(num, sizeof(num), "%d", value);
    if (l > width)
        l = width;
    x += width - l;

    TH_DrawString(dst, x, y, num, l);
}

static void TH_DrawLayoutString(char *dst, const char *s)
{
    char    buffer[MAX_QPATH];
    int     x, y;
    int     value;
    char    *token;
    size_t  len;
    int     width, index;
    clientinfo_t    *ci;

    if (!s[0])
        return;

    x = 0;
    y = 0;

    while (s) {
        token = COM_Parse(&s);
        if (token[2] == 0) {
            if (token[0] == 'x') {
                if (token[1] == 'l') {
                    token = COM_Parse(&s);
                    x = Q_atoi(token) / 8;
                    continue;
                }

                if (token[1] == 'r') {
                    token = COM_Parse(&s);
                    x = TH_WIDTH + Q_atoi(token) / 8;
                    continue;
                }

                if (token[1] == 'v') {
                    token = COM_Parse(&s);
                    x = TH_WIDTH / 2 - 20 + Q_atoi(token) / 8;
                    continue;
                }
            }

            if (token[0] == 'y') {
                if (token[1] == 't') {
                    token = COM_Parse(&s);
                    y = Q_atoi(token) / 8;
                    continue;
                }

                if (token[1] == 'b') {
                    token = COM_Parse(&s);
                    y = TH_HEIGHT + Q_atoi(token) / 8;
                    continue;
                }

                if (token[1] == 'v') {
                    token = COM_Parse(&s);
                    y = TH_HEIGHT / 2 - 15 + Q_atoi(token) / 8;
                    continue;
                }
            }
        }

        if (!strcmp(token, "pic")) {
            // draw a pic from a stat number
            COM_Parse(&s);
            continue;
        }

        if (!strcmp(token, "client")) {
            // draw a deathmatch client block
            int     score, ping, time;

            token = COM_Parse(&s);
            x = TH_WIDTH / 2 - 20 + Q_atoi(token) / 8;
            token = COM_Parse(&s);
            y = TH_HEIGHT / 2 - 15 + Q_atoi(token) / 8;

            token = COM_Parse(&s);
            value = Q_atoi(token);
            if (value < 0 || value >= MAX_CLIENTS) {
                Com_Error(ERR_DROP, "%s: invalid client index", __func__);
            }
            ci = &cl.clientinfo[value];

            token = COM_Parse(&s);
            score = Q_atoi(token);

            token = COM_Parse(&s);
            ping = Q_atoi(token);

            token = COM_Parse(&s);
            time = Q_atoi(token);

            len = strlen(ci->name);
            TH_DrawString(dst, x + 4, y, ci->name, len);
            len = Q_scnprintf(buffer, sizeof(buffer), "Score: %i", score);
            TH_DrawString(dst, x + 4, y + 1, buffer, len);
            len = Q_scnprintf(buffer, sizeof(buffer), "Ping:  %i", ping);
            TH_DrawString(dst, x + 4, y + 2, buffer, len);
            len = Q_scnprintf(buffer, sizeof(buffer), "Time:  %i", time);
            TH_DrawString(dst, x + 4, y + 3, buffer, len);
            continue;
        }

        if (!strcmp(token, "ctf")) {
            // draw a ctf client block
            int     score, ping;

            token = COM_Parse(&s);
            x = TH_WIDTH / 2 - 20 + Q_atoi(token) / 8;
            token = COM_Parse(&s);
            y = TH_HEIGHT / 2 - 15 + Q_atoi(token) / 8;

            token = COM_Parse(&s);
            value = Q_atoi(token);
            if (value < 0 || value >= MAX_CLIENTS) {
                Com_Error(ERR_DROP, "%s: invalid client index", __func__);
            }
            ci = &cl.clientinfo[value];

            token = COM_Parse(&s);
            score = Q_atoi(token);

            token = COM_Parse(&s);
            ping = Q_atoi(token);
            if (ping > 999)
                ping = 999;

            len = Q_scnprintf(buffer, sizeof(buffer), "%3d %3d %-12.12s",
                              score, ping, ci->name);
            TH_DrawString(dst, x, y, buffer, len);
            continue;
        }

        if (!strcmp(token, "picn")) {
            // draw a pic from a name
            COM_Parse(&s);
            continue;
        }

        if (!strcmp(token, "num")) {
            // draw a number
            token = COM_Parse(&s);
            width = Q_atoi(token);
            token = COM_Parse(&s);
            value = Q_atoi(token);
            if (value < 0 || value >= MAX_STATS) {
                Com_Error(ERR_DROP, "%s: invalid stat index", __func__);
            }
            value = cl.frame.ps.stats[value];
            TH_DrawNumber(dst, x, y, width, value);
            continue;
        }

        if (!strcmp(token, "stat_string")) {
            token = COM_Parse(&s);
            index = Q_atoi(token);
            if (index < 0 || index >= MAX_STATS) {
                Com_Error(ERR_DROP, "%s: invalid string index", __func__);
            }
            index = cl.frame.ps.stats[index];
            if (index < 0 || index >= cl.csr.end) {
                Com_Error(ERR_DROP, "%s: invalid string index", __func__);
            }
            len = strlen(cl.configstrings[index]);
            TH_DrawString(dst, x, y, cl.configstrings[index], len);
            continue;
        }

        if (!strncmp(token, "cstring", 7)) {
            token = COM_Parse(&s);
            len = strlen(token);
            TH_DrawCenterString(dst, x + 40 / 2, y, token, len);
            continue;
        }

        if (!strncmp(token, "string", 6)) {
            token = COM_Parse(&s);
            len = strlen(token);
            TH_DrawString(dst, x, y, token, len);
            continue;
        }

        if (!strcmp(token, "if")) {
            token = COM_Parse(&s);
            value = Q_atoi(token);
            if (value < 0 || value >= MAX_STATS) {
                Com_Error(ERR_DROP, "%s: invalid stat index", __func__);
            }
            value = cl.frame.ps.stats[value];
            if (!value) {   // skip to endif
                while (strcmp(token, "endif")) {
                    token = COM_Parse(&s);
                    if (!s) {
                        break;
                    }
                }
            }
            continue;
        }
    }
}

static void SCR_ScoreShot_f(void)
{
    char buffer[(TH_WIDTH + 1) * TH_HEIGHT];
    char path[MAX_OSPATH];
    qhandle_t f;
    int i, ret;

    if (cls.state != ca_active) {
        Com_Printf("Must be in a level.\n");
        return;
    }

    if (Cmd_Argc() > 1) {
        f = FS_EasyOpenFile(path, sizeof(path), FS_MODE_WRITE | FS_FLAG_TEXT,
                            "scoreshots/", Cmd_Argv(1), ".txt");
        if (!f) {
            return;
        }
    } else {
        // find a file name to save it to
        for (i = 0; i < 1000; i++) {
            Q_snprintf(path, sizeof(path), "scoreshots/quake%03d.txt", i);
            ret = FS_OpenFile(path, &f, FS_MODE_WRITE | FS_FLAG_TEXT | FS_FLAG_EXCL);
            if (f) {
                break;
            }
            if (ret != Q_ERR(EEXIST)) {
                Com_EPrintf("Couldn't exclusively open %s for writing: %s\n",
                            path, Q_ErrorString(ret));
                return;
            }
        }

        if (i == 1000) {
            Com_EPrintf("All scoreshot slots are full.\n");
            return;
        }
    }

    memset(buffer, ' ', sizeof(buffer));
    for (i = 0; i < TH_HEIGHT; i++) {
        buffer[i * (TH_WIDTH + 1) + TH_WIDTH] = '\n';
    }

    TH_DrawLayoutString(buffer, cl.configstrings[CS_STATUSBAR]);
    TH_DrawLayoutString(buffer, cl.layout);

    FS_Write(buffer, sizeof(buffer), f);

    if (FS_CloseFile(f))
        Com_EPrintf("Error writing %s\n", path);
    else
        Com_Printf("Wrote %s.\n", path);
}

static void SCR_ScoreDump_f(void)
{
    char buffer[(TH_WIDTH + 1) * TH_HEIGHT];
    int i;

    if (cls.state != ca_active) {
        Com_Printf("Must be in a level.\n");
        return;
    }

    memset(buffer, ' ', sizeof(buffer));
    for (i = 0; i < TH_HEIGHT - 1; i++) {
        buffer[i * (TH_WIDTH + 1) + TH_WIDTH] = '\n';
    }
    buffer[i * (TH_WIDTH + 1) + TH_WIDTH] = 0;

    TH_DrawLayoutString(buffer, cl.configstrings[CS_STATUSBAR]);
    TH_DrawLayoutString(buffer, cl.layout);

    Com_Printf("%s\n", buffer);
}

void CL_InitAscii(void)
{
    Cmd_AddCommand("aashot", SCR_ScoreShot_f);
    Cmd_AddCommand("aadump", SCR_ScoreDump_f);
}

