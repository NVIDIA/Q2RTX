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

#include "shared/shared.h"
#include "common/fifo.h"
#include "common/msg.h"

size_t FIFO_Read(fifo_t *fifo, void *buffer, size_t len)
{
    size_t wrapped, head = fifo->ay - fifo->ax;

    if (head > len) {
        if (buffer) {
            memcpy(buffer, fifo->data + fifo->ax, len);
            fifo->ax += len;
        }
        return len;
    }

    wrapped = len - head;
    if (wrapped > fifo->bs) {
        wrapped = fifo->bs;
    }
    if (buffer) {
        memcpy(buffer, fifo->data + fifo->ax, head);
        memcpy((byte *)buffer + head, fifo->data, wrapped);
        fifo->ax = wrapped;
        fifo->ay = fifo->bs;
        fifo->bs = 0;
    }

    return head + wrapped;
}

size_t FIFO_Write(fifo_t *fifo, const void *buffer, size_t len)
{
    size_t tail, wrapped, remaining;

    if (fifo->bs) {
        remaining = fifo->ax - fifo->bs;
        if (len > remaining) {
            len = remaining;
        }
        if (buffer) {
            memcpy(fifo->data + fifo->bs, buffer, len);
            fifo->bs += len;
        }
        return len;
    }

    tail = fifo->size - fifo->ay;
    if (tail > len) {
        if (buffer) {
            memcpy(fifo->data + fifo->ay, buffer, len);
            fifo->ay += len;
        }
        return len;
    }

    wrapped = len - tail;
    if (wrapped > fifo->ax) {
        wrapped = fifo->ax;
    }
    if (buffer) {
        memcpy(fifo->data + fifo->ay, buffer, tail);
        memcpy(fifo->data, (byte *)buffer + tail, wrapped);
        fifo->ay = fifo->size;
        fifo->bs = wrapped;
    }

    return tail + wrapped;
}

bool FIFO_ReadMessage(fifo_t *fifo, size_t msglen)
{
    size_t len;
    byte *data;

    data = FIFO_Peek(fifo, &len);
    if (len < msglen) {
        // read in two chunks into message buffer
        if (!FIFO_TryRead(fifo, msg_read_buffer, msglen)) {
            return false; // not yet available
        }
        SZ_Init(&msg_read, msg_read_buffer, sizeof(msg_read_buffer));
    } else {
        // read in a single block without copying any memory
        SZ_Init(&msg_read, data, msglen);
        FIFO_Decommit(fifo, msglen);
    }

    msg_read.cursize = msglen;
    return true;
}


