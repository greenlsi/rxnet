// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * clock_gettime() and nanosleep() are POSIX extensions not exposed by
 * strict C99 mode (-std=c99).  _POSIX_C_SOURCE must be defined before the
 * first system header in each translation unit — the Makefile passes
 * -D_POSIX_C_SOURCE=200809L for this reason.  The guard below is a
 * best-effort fallback for build systems that omit it; it only works when
 * no system header has been included yet in the compilation unit.
 */
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#  undef  _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif

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
#include <sched.h>
#include <stdint.h>
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
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) == 0) {
#ifdef PTHREAD_PRIO_INHERIT
        (void)pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
#endif
        pthread_mutex_init(m, &attr);
        pthread_mutexattr_destroy(&attr);
    } else {
        pthread_mutex_init(m, NULL);
    }
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
typedef void *(*rx_thread_fn)(void *arg);

static inline int rx_thread_create(rx_thread_t *t,
                                   rx_thread_fn fn, void *arg)
{
    return (pthread_create(t, NULL, fn, arg) == 0) ? 0 : -1;
}

#define RXNET_THREAD_FIFO_AVAILABLE 1

static inline int rx_thread_configure_fifo(rx_thread_t *t,
                                           int priority_rank,
                                           int ranks)
{
    int min_prio = sched_get_priority_min(SCHED_FIFO);
    int max_prio = sched_get_priority_max(SCHED_FIFO);
    struct sched_param sp;

    if (min_prio < 0 || max_prio < 0) return -1;
    if (ranks < 1 || priority_rank < 0 || priority_rank >= ranks) return -1;
    if (ranks > (max_prio - min_prio + 1)) return -1;

    sp.sched_priority = max_prio - priority_rank;
    return (pthread_setschedparam(*t, SCHED_FIFO, &sp) == 0) ? 0 : -1;
}

static inline int rx_thread_configure_current_fifo(int priority_rank, int ranks)
{
    rx_thread_t self = pthread_self();
    return rx_thread_configure_fifo(&self, priority_rank, ranks);
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

static inline void rx_barrier_reset(rx_barrier_t *b, unsigned int count)
{
    pthread_mutex_lock(&b->mutex);
    b->waiting = 0;
    b->total = count;
    b->generation = 0;
    pthread_mutex_unlock(&b->mutex);
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
