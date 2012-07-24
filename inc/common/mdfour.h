/*
    mdfour.c

    An implementation of MD4 designed for use in the samba SMB
    authentication protocol

    Copyright (C) 1997-1998  Andrew Tridgell

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

    See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to:

        Free Software Foundation, Inc.
        59 Temple Place - Suite 330
        Boston, MA  02111-1307, USA
*/

#ifndef MDFOUR_H
#define MDFOUR_H

typedef struct mdfour {
    uint32_t A, B, C, D;
    uint32_t totalN;
} mdfour_t;

void mdfour_begin(struct mdfour *md);
void mdfour_update(struct mdfour *md, uint8_t *in, size_t n);
void mdfour_result(struct mdfour *md, uint8_t *out);

uint32_t Com_BlockChecksum(void *buffer, size_t len);

#endif // MDFOUR_H
