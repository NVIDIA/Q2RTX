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

#include "shared/shared.h"
#include "common/protocol.h"
#include "common/sizebuf.h"
#include "common/intreadwrite.h"

void SZ_TagInit(sizebuf_t *buf, void *data, size_t size, const char *tag)
{
    memset(buf, 0, sizeof(*buf));
    buf->data = data;
    buf->maxsize = size;
    buf->tag = tag;
}

void SZ_Init(sizebuf_t *buf, void *data, size_t size)
{
    memset(buf, 0, sizeof(*buf));
    buf->data = data;
    buf->maxsize = size;
    buf->allowoverflow = true;
    buf->allowunderflow = true;
    buf->tag = "none";
}

void SZ_Clear(sizebuf_t *buf)
{
    buf->cursize = 0;
    buf->readcount = 0;
    buf->overflowed = false;
}

void *SZ_GetSpace(sizebuf_t *buf, size_t len)
{
    void    *data;

    if (buf->cursize > buf->maxsize) {
        Com_Error(ERR_FATAL,
                  "%s: %s: already overflowed",
                  __func__, buf->tag);
    }

    if (len > buf->maxsize - buf->cursize) {
        if (len > buf->maxsize) {
            Com_Error(ERR_FATAL,
                      "%s: %s: %zu is > full buffer size %zu",
                      __func__, buf->tag, len, buf->maxsize);
        }

        if (!buf->allowoverflow) {
            Com_Error(ERR_FATAL,
                      "%s: %s: overflow without allowoverflow set",
                      __func__, buf->tag);
        }

        //Com_DPrintf("%s: %s: overflow\n", __func__, buf->tag);
        SZ_Clear(buf);
        buf->overflowed = true;
    }

    data = buf->data + buf->cursize;
    buf->cursize += len;
    return data;
}

void SZ_WriteByte(sizebuf_t *sb, int c)
{
    byte    *buf;

    buf = SZ_GetSpace(sb, 1);
    buf[0] = c;
}

void SZ_WriteShort(sizebuf_t *sb, int c)
{
    byte    *buf;

    buf = SZ_GetSpace(sb, 2);
    WL16(buf, c);
}

void SZ_WriteLong(sizebuf_t *sb, int c)
{
    byte    *buf;

    buf = SZ_GetSpace(sb, 4);
    WL32(buf, c);
}

void SZ_WriteString(sizebuf_t *sb, const char *s)
{
    size_t len;

    if (!s) {
        SZ_WriteByte(sb, 0);
        return;
    }

    len = strlen(s);
    if (len >= MAX_NET_STRING) {
        Com_WPrintf("%s: overflow: %zu chars", __func__, len);
        SZ_WriteByte(sb, 0);
        return;
    }

    SZ_Write(sb, s, len + 1);
}

void *SZ_ReadData(sizebuf_t *buf, size_t len)
{
    void    *data;

    if (buf->readcount > buf->cursize || len > buf->cursize - buf->readcount) {
        if (!buf->allowunderflow) {
            Com_Error(ERR_DROP, "%s: read past end of message", __func__);
        }
        buf->readcount = buf->cursize + 1;
        return NULL;
    }

    data = buf->data + buf->readcount;
    buf->readcount += len;
    return data;
}

int SZ_ReadByte(sizebuf_t *sb)
{
    byte *buf = SZ_ReadData(sb, 1);
    return buf ? *buf : -1;
}

int SZ_ReadShort(sizebuf_t *sb)
{
    byte *buf = SZ_ReadData(sb, 2);
    return buf ? (int16_t)RL16(buf) : -1;
}

int SZ_ReadLong(sizebuf_t *sb)
{
    byte *buf = SZ_ReadData(sb, 4);
    return buf ? (int32_t)RL32(buf) : -1;
}
