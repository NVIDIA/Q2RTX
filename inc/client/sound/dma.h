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

typedef struct snddmaAPI_s {
    sndinitstat_t (*Init)(void);
    void (*Shutdown)(void);
    void (*BeginPainting)(void);
    void (*Submit)(void);
    void (*Activate)(bool active);
} snddmaAPI_t;

void WAVE_FillAPI(snddmaAPI_t *api);

#if USE_DSOUND
void DS_FillAPI(snddmaAPI_t *api);
#endif

extern dma_t    dma;
extern int      paintedtime;

extern cvar_t   *s_khz;
extern cvar_t   *s_testsound;

#endif // DMA_H
