// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

/*
 * rxnet/port/posix.h — POSIX port (Linux, macOS, POSIX-compliant systems).
 *
 * Time:    clock_gettime(CLOCK_MONOTONIC) → int64_t nanoseconds
 * Mutex:   pthread_mutex_t
 * Thread:  pthread_t + pthread_create
 * Barrier: pthread_mutex_t + pthread_cond_t (generation-based, reusable)
 *
 * No additional .c files required — everything is inline or uses the
 * POSIX standard library.
 */
#pragma once

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── time ───────────────────────────────────────────────────────────── */

typedef int64_t rx_tick_t;   /* nanoseconds since an arbitrary epoch */

static inline rx_tick_t rx_tick_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static inline rx_tick_t rx_tick_add_us(rx_tick_t t, long us)
{
    return t + (int64_t)us * 1000LL;
}

static inline int rx_tick_compare(rx_tick_t a, rx_tick_t b)
{
    return (a < b) ? -1 : (a > b) ? 1 : 0;
}

static inline void rx_tick_sleep_until(rx_tick_t target_ns)
{
    for (;;) {
        rx_tick_t now = rx_tick_now();
        if (now >= target_ns) return;

        int64_t rem_ns = target_ns - now;
        struct timespec ts;
        ts.tv_sec  = (time_t)(rem_ns / 1000000000LL);
        ts.tv_nsec = (long)  (rem_ns % 1000000000LL);

        if (nanosleep(&ts, NULL) == 0) return;
        if (errno != EINTR) return;   /* unexpected error — give up */
    }
}

/* ── mutex ──────────────────────────────────────────────────────────── */

typedef pthread_mutex_t rx_mutex_t;

static inline void rx_mutex_init(rx_mutex_t *m)
{
    pthread_mutex_init(m, NULL);
}

static inline void rx_mutex_lock(rx_mutex_t *m)
{
    pthread_mutex_lock(m);
}

static inline void rx_mutex_unlock(rx_mutex_t *m)
{
    pthread_mutex_unlock(m);
}

/* ── thread ─────────────────────────────────────────────────────────── */

typedef pthread_t rx_thread_t;

/* pthread_create requires void *(*)(void *), but rxnet uses void (*)(void *)
 * for cross-platform compatibility (FreeRTOS/Zephyr tasks return void).
 * The trampoline below adapts the signatures without a function-pointer cast.
 * The arg struct is heap-allocated and freed by the trampoline after the
 * thread function returns (which never happens in rxnet's infinite loops, but
 * it is correct to pair malloc/free for static analysis tools). */
typedef struct { void (*fn)(void *); void *arg; } _rx_posix_thread_args_t;

static inline void *_rx_posix_thread_trampoline(void *p)
{
    _rx_posix_thread_args_t a = *(_rx_posix_thread_args_t *)p;
    free(p);
    a.fn(a.arg);
    return NULL;
}

static inline int rx_thread_create(rx_thread_t *t,
                                   void (*fn)(void *), void *arg)
{
    _rx_posix_thread_args_t *a =
        (_rx_posix_thread_args_t *)malloc(sizeof(_rx_posix_thread_args_t));
    if (!a) return -1;
    a->fn  = fn;
    a->arg = arg;
    if (pthread_create(t, NULL, _rx_posix_thread_trampoline, a) != 0) {
        free(a);
        return -1;
    }
    return 0;
}

/* ── barrier (generation-based, reusable) ───────────────────────────── */

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    unsigned int    waiting;
    unsigned int    total;
    unsigned int    generation;
} rx_barrier_t;

static inline int rx_barrier_init(rx_barrier_t *b, unsigned int count)
{
    b->waiting    = 0;
    b->total      = count;
    b->generation = 0;
    if (pthread_mutex_init(&b->mutex, NULL) != 0) return -1;
    if (pthread_cond_init(&b->cond, NULL) != 0) {
        pthread_mutex_destroy(&b->mutex);
        return -1;
    }
    return 0;
}

static inline void rx_barrier_wait(rx_barrier_t *b)
{
    unsigned int gen;
    pthread_mutex_lock(&b->mutex);
    gen = b->generation;
    if (++b->waiting == b->total) {
        b->generation++;
        b->waiting = 0;
        pthread_cond_broadcast(&b->cond);
    } else {
        while (b->generation == gen)
            pthread_cond_wait(&b->cond, &b->mutex);
    }
    pthread_mutex_unlock(&b->mutex);
}

/* ── trace subsystem defaults ───────────────────────────────────────── */
/* trace.h checks these before setting its own POSIX defaults, so
 * defining them here keeps the two files in sync automatically.       */

#ifndef RX_TRACE_NOW_NS
#  define RX_TRACE_NOW_NS() rx_tick_now()
#endif

#ifndef RX_TRACE_LOCK_TYPE
#  define RX_TRACE_LOCK_TYPE            pthread_mutex_t
#  define RX_TRACE_LOCK_INIT(lk)        pthread_mutex_init(&(lk), NULL)
#  define RX_TRACE_LOCK_ACQUIRE(lk)     pthread_mutex_lock(&(lk))
#  define RX_TRACE_LOCK_RELEASE(lk)     pthread_mutex_unlock(&(lk))
#endif

#ifdef __cplusplus
}
#endif
