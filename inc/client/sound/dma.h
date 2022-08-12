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

#ifndef DMA_H
#define DMA_H

typedef struct dma_s {
    int         channels;
    int         samples;                // mono samples in buffer
    int         submission_chunk;       // don't mix less than this #
    int         samplepos;              // in mono samples
    int         samplebits;
    int         speed;
    byte        *buffer;
} dma_t;

typedef enum {
    SIS_SUCCESS,
    SIS_FAILURE,
    SIS_NOTAVAIL
} sndinitstat_t;

typedef struct {
    const char *name;
    sndinitstat_t (*init)(void);
    void (*shutdown)(void);
    void (*begin_painting)(void);
    void (*submit)(void);
    void (*activate)(bool active);
} snddma_driver_t;

extern dma_t    dma;
extern int      s_paintedtime;

extern cvar_t   *s_khz;

#endif // DMA_H
