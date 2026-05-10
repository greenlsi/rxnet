// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * rxnet/port/zephyr.h — Zephyr RTOS port.
 *
 * Target: nRF52832 / nRF52840 / nRF5340 (nRF54) and any Zephyr project.
 *
 * Time:    k_uptime_ticks() → k_ticks_to_ns_floor64() → int64_t nanoseconds
 * Mutex:   struct k_mutex
 * Thread:  struct k_thread + k_thread_create() with a static stack pool
 * Barrier: struct k_mutex + struct k_condvar (generation-based, reusable)
 *
 * Tuning macros (override via -D or Kconfig-generated config):
 *
 *   RXNET_ZEPHYR_STACK_SIZE        thread stack in bytes (default 2048)
 *   RXNET_ZEPHYR_THREAD_PRIORITY   Zephyr priority       (default 5)
 *   RXNET_ZEPHYR_MAX_THREADS       pre-allocated stacks  (default = RXNET_MAX_RUNTIME_NODES
 *                                                          × RXNET_THREAD_MAX_RUNTIMES)
 *
 * Usage — Zephyr CMakeLists.txt:
 *
 *   # CONFIG_ZEPHYR is set automatically by Zephyr's build system.
 *   # No explicit -D needed.
 *   target_sources(app PRIVATE
 *       ${RXNET_DIR}/c/src/runtime.c
 *       ${RXNET_DIR}/c/src/fsm.c
 *       ${RXNET_DIR}/c/src/pn.c
 *       ${RXNET_DIR}/c/src/cyclic.c
 *       ${RXNET_DIR}/c/src/coop.c
 *       ${RXNET_DIR}/c/src/thread.c
 *       ${RXNET_DIR}/c/src/port/zephyr.c
 *   )
 *   target_include_directories(app PRIVATE ${RXNET_DIR}/c/include)
 *
 * Required prj.conf options:
 *
 *   CONFIG_EVENTS=y          # enables k_condvar
 *   CONFIG_TIMERS=y          # k_timer / k_uptime_ticks (usually on by default)
 *
 * Static stack pool:
 *   Zephyr requires thread stacks to be declared with
 *   K_THREAD_STACK_DEFINE at file scope and with a compile-time constant
 *   size.  This port pre-allocates a pool of RXNET_ZEPHYR_MAX_THREADS
 *   stacks using K_THREAD_STACK_ARRAY_DEFINE.  A simple counter
 *   (_rxnet_zephyr_thread_idx) assigns stacks at rx_thread_create() time.
 *   The pool is never freed (embedded devices do not dynamically unload
 *   rxnet runtimes).
 *
 * Priority note:
 *   Zephyr uses cooperative (negative) and preemptive (non-negative)
 *   priorities.  The default value 5 is a mid-level preemptive priority.
 *   Lower numbers have higher urgency.  Adjust RXNET_ZEPHYR_THREAD_PRIORITY
 *   to place rxnet worker threads above or below other application threads.
 */
#pragma once

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "rxnet/config.h"   /* RXNET_MAX_RUNTIME_NODES, RXNET_THREAD_MAX_RUNTIMES */

#ifdef __cplusplus
extern "C" {
#endif

/* ── tuning parameters ──────────────────────────────────────────────── */

#ifndef RXNET_ZEPHYR_STACK_SIZE
#  define RXNET_ZEPHYR_STACK_SIZE       2048u
#endif

#ifndef RXNET_ZEPHYR_THREAD_PRIORITY
#  define RXNET_ZEPHYR_THREAD_PRIORITY  5
#endif

#ifndef RXNET_ZEPHYR_MAX_THREADS
#  define RXNET_ZEPHYR_MAX_THREADS \
       (RXNET_MAX_RUNTIME_NODES * RXNET_THREAD_MAX_RUNTIMES)
#endif

/* ── static stack pool ──────────────────────────────────────────────── */
/*
 * Defined once in src/port/zephyr.c; declared here for all consumers.
 */
extern struct z_thread_stack_element
    _rxnet_zephyr_stacks[RXNET_ZEPHYR_MAX_THREADS][K_THREAD_STACK_LEN(RXNET_ZEPHYR_STACK_SIZE)];

extern unsigned int _rxnet_zephyr_thread_idx;

/* ── time ───────────────────────────────────────────────────────────── */

typedef int64_t rx_tick_t;   /* nanoseconds since boot */

static inline rx_tick_t rx_tick_now(void)
{
    return (int64_t)k_ticks_to_ns_floor64((uint64_t)k_uptime_ticks());
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
    int64_t rem_ns = target_ns - rx_tick_now();
    if (rem_ns > 0)
        k_sleep(K_NSEC(rem_ns));
}

/* ── mutex ──────────────────────────────────────────────────────────── */

typedef struct k_mutex rx_mutex_t;

static inline void rx_mutex_init(rx_mutex_t *m)
{
    k_mutex_init(m);
}

static inline void rx_mutex_lock(rx_mutex_t *m)
{
    k_mutex_lock(m, K_FOREVER);
}

static inline void rx_mutex_unlock(rx_mutex_t *m)
{
    k_mutex_unlock(m);
}

/* ── thread ─────────────────────────────────────────────────────────── */

typedef struct k_thread rx_thread_t;
typedef void *(*rx_thread_fn)(void *arg);

static void _rxnet_zephyr_thread_entry(void *arg, void *unused1, void *unused2)
{
    rx_thread_fn fn = ((rx_thread_fn *)arg)[0];
    void *user = ((void **)arg)[1];
    (void)unused1;
    (void)unused2;
    (void)fn(user);
}

static inline int rx_thread_create(rx_thread_t *t,
                                   rx_thread_fn fn, void *arg)
{
    unsigned int idx = _rxnet_zephyr_thread_idx++;
    static void *args[RXNET_ZEPHYR_MAX_THREADS][2];
    if (idx >= RXNET_ZEPHYR_MAX_THREADS) return -1;
    args[idx][0] = (void *)fn;
    args[idx][1] = arg;

    k_tid_t tid = k_thread_create(
        t,
        _rxnet_zephyr_stacks[idx],
        RXNET_ZEPHYR_STACK_SIZE,
        _rxnet_zephyr_thread_entry,
        args[idx], NULL, NULL,
        RXNET_ZEPHYR_THREAD_PRIORITY,
        0,
        K_NO_WAIT
    );

    return (tid != NULL) ? 0 : -1;
}

#define RXNET_THREAD_FIFO_AVAILABLE 1

static inline int rx_thread_configure_fifo(rx_thread_t *t,
                                           int priority_rank,
                                           int ranks)
{
    if (ranks < 1 || priority_rank < 0 || priority_rank >= ranks) return -1;
    k_thread_priority_set(t, RXNET_ZEPHYR_THREAD_PRIORITY + priority_rank);
    return 0;
}

static inline int rx_thread_configure_current_fifo(int priority_rank, int ranks)
{
    if (ranks < 1 || priority_rank < 0 || priority_rank >= ranks) return -1;
    k_thread_priority_set(k_current_get(), RXNET_ZEPHYR_THREAD_PRIORITY + priority_rank);
    return 0;
}

/* ── barrier (generation-based via k_mutex + k_condvar) ─────────────── */

typedef struct {
    struct k_mutex   mutex;
    struct k_condvar cond;
    unsigned int     waiting;
    unsigned int     total;
    unsigned int     generation;
} rx_barrier_t;

static inline int rx_barrier_init(rx_barrier_t *b, unsigned int count)
{
    b->waiting    = 0;
    b->total      = count;
    b->generation = 0;
    k_mutex_init(&b->mutex);
    k_condvar_init(&b->cond);
    return 0;
}

static inline void rx_barrier_reset(rx_barrier_t *b, unsigned int count)
{
    k_mutex_lock(&b->mutex, K_FOREVER);
    b->waiting = 0;
    b->total = count;
    b->generation = 0;
    k_mutex_unlock(&b->mutex);
}

static inline void rx_barrier_wait(rx_barrier_t *b)
{
    unsigned int gen;
    k_mutex_lock(&b->mutex, K_FOREVER);
    gen = b->generation;
    if (++b->waiting == b->total) {
        b->generation++;
        b->waiting = 0;
        k_condvar_broadcast(&b->cond);
    } else {
        while (b->generation == gen)
            k_condvar_wait(&b->cond, &b->mutex, K_FOREVER);
    }
    k_mutex_unlock(&b->mutex);
}

/* ── trace subsystem defaults ───────────────────────────────────────── */

#ifndef RX_TRACE_NOW_NS
#  define RX_TRACE_NOW_NS() rx_tick_now()
#endif

#ifndef RX_TRACE_LOCK_TYPE
#  define RX_TRACE_LOCK_TYPE          struct k_mutex
#  define RX_TRACE_LOCK_INIT(lk)      (k_mutex_init(&(lk)), 0)
#  define RX_TRACE_LOCK_ACQUIRE(lk)   k_mutex_lock(&(lk), K_FOREVER)
#  define RX_TRACE_LOCK_RELEASE(lk)   k_mutex_unlock(&(lk))
#endif

#ifdef __cplusplus
}
#endif
