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

#include "client.h"
#include "shared/list.h"
#include "system/pthread.h"
#include <process.h>

typedef struct {
    list_t entry;
    void *(*func)(void *);
    void *arg, *ret;
    HANDLE handle;
    DWORD id;
} winthread_t;

static LIST_DECL(threads);
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static winthread_t *find_thread(pthread_t thread)
{
    winthread_t *t;

    LIST_FOR_EACH(winthread_t, t, &threads, entry)
        if (t->id == thread)
            return t;

    return NULL;
}

static unsigned __stdcall thread_func(void *arg)
{
    winthread_t *t = arg;
    t->ret = t->func(t->arg);
    return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg)
{
    int ret = 0;
    pthread_mutex_lock(&mutex);
    winthread_t *t = malloc(sizeof(*t));
    if (!t) {
        ret = EAGAIN;
        goto done;
    }
    t->func = start_routine;
    t->arg = arg;
    t->handle = (HANDLE)_beginthreadex(NULL, 0, thread_func, t, 0, thread);
    if (!t->handle) {
        free(t);
        ret = EAGAIN;
        goto done;
    }
    t->id = *thread;
    List_Append(&threads, &t->entry);
done:
    pthread_mutex_unlock(&mutex);
    return ret;
}

int pthread_join(pthread_t thread, void **retval)
{
    int ret = 0;
    pthread_mutex_lock(&mutex);
    winthread_t *t = find_thread(thread);
    if (!t) {
        ret = ESRCH;
        goto done;
    }
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
    List_Remove(&t->entry);
    if (retval)
        *retval = t->ret;
    free(t);
done:
    pthread_mutex_unlock(&mutex);
    return ret;
}
