/*
Copyright (C) 2003-2008 Andrey Nazarov

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

#ifndef FIFO_H
#define FIFO_H

typedef struct {
    byte *data;
    size_t size;
    size_t ax, ay, bs;
} fifo_t;

static inline void *FIFO_Reserve(fifo_t *fifo, size_t *len)
{
    size_t tail;

    if (fifo->bs) {
        *len = fifo->ax - fifo->bs;
        return fifo->data + fifo->bs;
    }

    tail = fifo->size - fifo->ay;
    if (tail) {
        *len = tail;
        return fifo->data + fifo->ay;
    }

    *len = fifo->ax;
    return fifo->data;
}

static inline void FIFO_Commit(fifo_t *fifo, size_t len)
{
    size_t tail;

    if (fifo->bs) {
        fifo->bs += len;
        return;
    }

    tail = fifo->size - fifo->ay;
    if (tail) {
        fifo->ay += len;
        return;
    }

    fifo->bs = len;
}

static inline void *FIFO_Peek(fifo_t *fifo, size_t *len)
{
    *len = fifo->ay - fifo->ax;
    return fifo->data + fifo->ax;
}

static inline void FIFO_Decommit(fifo_t *fifo, size_t len)
{
    if (fifo->ax + len < fifo->ay) {
        fifo->ax += len;
        return;
    }

    fifo->ay = fifo->bs;
    fifo->ax = fifo->bs = 0;
}

static inline size_t FIFO_Usage(fifo_t *fifo)
{
    return fifo->ay - fifo->ax + fifo->bs;
}

static inline int FIFO_Percent(fifo_t *fifo)
{
    if (!fifo->size) {
        return 0;
    }
    return (int)(FIFO_Usage(fifo) * 100 / fifo->size);
}

static inline void FIFO_Clear(fifo_t *fifo)
{
    fifo->ax = fifo->ay = fifo->bs = 0;
}

size_t FIFO_Read(fifo_t *fifo, void *buffer, size_t len);
size_t FIFO_Write(fifo_t *fifo, const void *buffer, size_t len);

static inline bool FIFO_TryRead(fifo_t *fifo, void *buffer, size_t len)
{
    if (FIFO_Read(fifo, NULL, len) < len) {
        return false;
    }
    FIFO_Read(fifo, buffer, len);
    return true;
}

static inline bool FIFO_TryWrite(fifo_t *fifo, void *buffer, size_t len)
{
    if (FIFO_Write(fifo, NULL, len) < len) {
        return false;
    }
    FIFO_Write(fifo, buffer, len);
    return true;
}

bool FIFO_ReadMessage(fifo_t *fifo, size_t msglen);

#endif // FIFO_H
