/*
Copyright (C) 2023 Andrey Nazarov

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

#pragma once

#if USE_CLIENT

typedef struct asyncwork_s {
    void (*work_cb)(void *);
    void (*done_cb)(void *);
    void *cb_arg;
    struct asyncwork_s *next;
} asyncwork_t;

void Com_QueueAsyncWork(asyncwork_t *work);
void Com_CompleteAsyncWork(void);
void Com_ShutdownAsyncWork(void);

#else

#define Com_QueueAsyncWork(work)    (void)0
#define Com_CompleteAsyncWork()     (void)0
#define Com_ShutdownAsyncWork()     (void)0

#endif
