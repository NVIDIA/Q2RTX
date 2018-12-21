#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// storage will be in the corresponding threads.c file:
extern uint32_t threads_id;

typedef struct { void* x; } pthread_mutex_t;
#define threads_mutex_lock(m)    ((void*)m)
#define threads_mutex_unlock(m)  ((void*)m)
#define threads_mutex_destroy(m) ((void*)m)
#define threads_mutex_init(m, p) ((void*)m)

// hack :/
#define __sync_fetch_and_add(p, a) ((*(p) += (a)), *(p) - (a))

#ifndef aligned_alloc
#define aligned_alloc(a, s) _aligned_malloc(s, a)
#endif
#ifndef aligned_free
#define aligned_free(p) _aligned_free(p)
#endif

#define sysconf(x) 1
#ifndef usleep
#define usleep(x)
#endif
#ifndef sched_yield
#define sched_yield()
#endif

typedef struct threads_t
{
  uint32_t num_threads;
  uint32_t task[1];
  void *pool;
}
threads_t;

static inline void
pthread_pool_task_init(uint32_t *task, void *pool, void* (*f)(void *), void *param)
{ // single threaded execution in line
  f(param);
}

static inline void pthread_pool_wait(void *pool) { }

static inline threads_t *threads_init(uint32_t num_threads)
{
  threads_t *t = malloc(sizeof(*t));
  t->num_threads = 1;
  t->task[0] = 0;
  t->pool = 0;
  threads_id = 0;
  return t;
}

static inline void threads_cleanup(threads_t *t)
{
  free(t);
}

