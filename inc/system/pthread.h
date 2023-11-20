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

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>

#define PTHREAD_MUTEX_INITIALIZER   {0}
#define PTHREAD_COND_INITIALIZER    {0}

typedef void pthread_attr_t;
typedef void pthread_mutexattr_t;
typedef void pthread_condattr_t;
typedef unsigned int pthread_t;

typedef struct {
    SRWLOCK srw;
} pthread_mutex_t;

typedef struct {
    CONDITION_VARIABLE cond;
} pthread_cond_t;

static inline int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr)
{
    InitializeSRWLock(&mutex->srw);
    return 0;
}

static inline int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    AcquireSRWLockExclusive(&mutex->srw);
    return 0;
}

static inline int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    return TryAcquireSRWLockExclusive(&mutex->srw) ? 0 : EBUSY;
}

static inline int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    ReleaseSRWLockExclusive(&mutex->srw);
    return 0;
}

static inline int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    return 0;
}

static inline int pthread_cond_init(pthread_cond_t *cond, pthread_condattr_t *cond_attr)
{
    InitializeConditionVariable(&cond->cond);
    return 0;
}

static inline int pthread_cond_signal(pthread_cond_t *cond)
{
    WakeConditionVariable(&cond->cond);
    return 0;
}

static inline int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    return SleepConditionVariableSRW(&cond->cond, &mutex->srw, INFINITE, 0) ? 0 : ETIMEDOUT;
}

static inline int pthread_cond_destroy(pthread_cond_t *cond)
{
    return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);

int pthread_join(pthread_t thread, void **retval);

#else
#include <pthread.h>
#endif
