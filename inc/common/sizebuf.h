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

#ifndef SIZEBUF_H
#define SIZEBUF_H

typedef struct {
    bool        allowoverflow;
    bool        allowunderflow;
    bool        overflowed;     // set to true if the buffer size failed
    byte        *data;
    size_t      maxsize;
    size_t      cursize;
    size_t      readcount;
    uint32_t    bits_buf;
    uint32_t    bits_left;
    const char  *tag;           // for debugging
} sizebuf_t;

void SZ_Init(sizebuf_t *buf, void *data, size_t size);
void SZ_TagInit(sizebuf_t *buf, void *data, size_t size, const char *tag);
void SZ_Clear(sizebuf_t *buf);
void *SZ_GetSpace(sizebuf_t *buf, size_t len);
void SZ_WriteByte(sizebuf_t *sb, int c);
void SZ_WriteShort(sizebuf_t *sb, int c);
void SZ_WriteLong(sizebuf_t *sb, int c);
#if USE_MVD_SERVER
void SZ_WriteString(sizebuf_t *sb, const char *s);
#endif

static inline void *SZ_Write(sizebuf_t *buf, const void *data, size_t len)
{
    return memcpy(SZ_GetSpace(buf, len), data, len);
}

void *SZ_ReadData(sizebuf_t *buf, size_t len);
int SZ_ReadByte(sizebuf_t *sb);
int SZ_ReadShort(sizebuf_t *sb);
int SZ_ReadLong(sizebuf_t *sb);

#endif // SIZEBUF_H
