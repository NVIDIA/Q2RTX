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

#include "shared/shared.h"
#include "common/async.h"
#include "common/zone.h"
#include "system/pthread.h"

static bool work_initialized;
static bool work_terminate;
static pthread_mutex_t work_lock;
static pthread_cond_t work_cond;
static pthread_t work_thread;
static asyncwork_t *pend_head;
static asyncwork_t *done_head;

static void append_work(asyncwork_t **head, asyncwork_t *work)
{
    asyncwork_t *c, **p;
    for (p = head, c = *head; c; p = &c->next, c = c->next)
        ;
    work->next = NULL;
    *p = work;
}

static void *work_func(void *arg)
{
    pthread_mutex_lock(&work_lock);
    while (1) {
        while (!pend_head && !work_terminate)
            pthread_cond_wait(&work_cond, &work_lock);

        asyncwork_t *work = pend_head;
        if (!work)
            break;
        pend_head = work->next;

        pthread_mutex_unlock(&work_lock);
        work->work_cb(work->cb_arg);
        pthread_mutex_lock(&work_lock);

        append_work(&done_head, work);
    }
    pthread_mutex_unlock(&work_lock);

    return NULL;
}

void Com_QueueAsyncWork(asyncwork_t *work)
{
    if (!work_initialized) {
        pthread_mutex_init(&work_lock, NULL);
        pthread_cond_init(&work_cond, NULL);
        if (pthread_create(&work_thread, NULL, work_func, NULL))
            Com_Error(ERR_FATAL, "Couldn't create async work thread");
        work_initialized = true;
    }

    pthread_mutex_lock(&work_lock);
    append_work(&pend_head, Z_CopyStruct(work));
    pthread_mutex_unlock(&work_lock);

    pthread_cond_signal(&work_cond);
}

void Com_CompleteAsyncWork(void)
{
    asyncwork_t *work, *next;

    if (!work_initialized)
        return;
    if (pthread_mutex_trylock(&work_lock))
        return;
    if (q_unlikely(done_head)) {
        for (work = done_head; work; work = next) {
            next = work->next;
            if (work->done_cb)
                work->done_cb(work->cb_arg);
            Z_Free(work);
        }
        done_head = NULL;
    }
    pthread_mutex_unlock(&work_lock);
}

void Com_ShutdownAsyncWork(void)
{
    if (!work_initialized)
        return;

    pthread_mutex_lock(&work_lock);
    work_terminate = true;
    pthread_mutex_unlock(&work_lock);

    pthread_cond_signal(&work_cond);

    Q_assert(!pthread_join(work_thread, NULL));
    Com_CompleteAsyncWork();

    pthread_mutex_destroy(&work_lock);
    pthread_cond_destroy(&work_cond);
    work_initialized = false;
}
