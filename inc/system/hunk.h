/*
Copyright (C) 2003-2012 Andrey Nazarov

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

#ifndef HUNK_H
#define HUNK_H

// WID: C++20: In case of C++ including this..
#ifdef __cplusplus
// We extern "C"
extern "C" {
#endif

typedef struct {
    void    *base;
    size_t  maxsize;
    size_t  cursize;
    size_t  mapped;
} memhunk_t;

void    Hunk_Init(void);
void    Hunk_Begin(memhunk_t *hunk, size_t maxsize);
void    *Hunk_Alloc(memhunk_t *hunk, size_t size);
void    Hunk_End(memhunk_t *hunk);
void    Hunk_Free(memhunk_t *hunk);

// WID: C++20: In case of C++ including this..
#ifdef __cplusplus
// We extern "C"
};
#endif

#endif // HUNK_H
