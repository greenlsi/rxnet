// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#pragma once

/*
 * rxnet/coop.h — Cooperative (single-thread) multi-rate scheduler.
 *
 * Runs registered runtimes on a single OS thread.  Each runtime is called
 * when its deadline has passed; after all due runtimes are serviced the
 * scheduler sleeps until the nearest upcoming deadline.
 *
 * Contrast with the cyclic executive (rxnet/cyclic.h):
 *   - Cyclic exec: pre-computed static dispatch table; jitter-free ticking.
 *   - Coop exec:   dynamic deadline check each iteration; more flexible
 *                  (tolerates tasks that run longer than one period without
 *                  corrupting the table), no OS scheduling overhead.
 *
 * The scheduling period for each runtime is read from rt->period_us, which
 * is set by rx_runtime_build() (called automatically by rx_coop_exec_add()).
 *
 * Usage:
 *
 *   rx_fsm_runtime_add_machine(&rt, &machine_a, 10000, 0);  // 10 ms
 *   rx_fsm_runtime_add_machine(&rt, &machine_b, 20000, 0);  // 20 ms
 *
 *   rx_coop_exec ce;
 *   rx_coop_exec_init(&ce);
 *   rx_coop_exec_add(&ce, &rt.runtime);
 *   rx_coop_exec_run(&ce);   // returns after stop
 */

#include "rxnet/config.h"
#include "rxnet/cyclic.h"   /* rx_tick_t, rx_tick_now, rx_tick_add_us, rx_tick_sleep_until */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    rx_runtime *rt;
    rx_tick_t   next_tick;
} rx_coop_task;

typedef struct {
    rx_coop_task tasks[RXNET_CE_MAX_TASKS];
    int          ntasks;
    volatile int stop_requested;
    void       (*on_stop)(void *user);
    void        *on_stop_user;
} rx_coop_exec;

/* Initialise to empty. */
void rx_coop_exec_init(rx_coop_exec *ce);

/*
 * Register a runtime.
 *
 * Calls rx_runtime_build() if not already built.  Reads rt->period_us as
 * the scheduling period.
 *
 * Returns 0 on success, -1 if RXNET_CE_MAX_TASKS is exceeded, the runtime
 * has no periodic nodes, or rx_runtime_build() fails.
 */
int rx_coop_exec_add(rx_coop_exec *ce, rx_runtime *rt);

/* Enter the scheduler loop.  Returns after rx_coop_exec_stop() is requested. */
void rx_coop_exec_run(rx_coop_exec *ce);

/* Request rx_coop_exec_run() to return at the next safe point. */
void rx_coop_exec_stop(rx_coop_exec *ce);

/* Register an optional callback executed once before run() returns. */
void rx_coop_exec_on_stop(rx_coop_exec *ce,
                          void (*callback)(void *user),
                          void *user);

#ifdef __cplusplus
}
#endif
