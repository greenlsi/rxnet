// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*
 * rxnet/cyclic.h — Cyclic executive with hyperperiod dispatch table.
 *
 * A cyclic executive runs runtime nodes at fixed periods using a static
 * activation table computed once at startup:
 *
 *   base  = GCD of all task periods  (= minimum common divisor)
 *   hyper = LCM of all task periods  (= minimum common multiple)
 *   n_slots = hyper / base
 *
 * Node n is scheduled at slot s when (s * base) % n.period == 0.
 * Periodic nodes active in a slot are sorted by effective deadline
 * (deadline_us if set, otherwise period_us); async nodes are appended after
 * periodic activations.
 * The loop advances one base tick per iteration, sleeping until the
 * next deadline with rx_tick_sleep_until() from rxnet/port.h.
 *
 * Usage:
 *
 *   rx_cyclic_exec ce;
 *   rx_cyclic_exec_init(&ce);
 *   rx_cyclic_exec_add(&ce, &fast_rt.runtime);
 *   rx_cyclic_exec_add(&ce, &slow_rt.runtime);
 *   rx_cyclic_exec_add(&ce, &cli_rt.runtime);
 *   rx_cyclic_exec_run(&ce);                            // returns after stop
 *
 * rx_fsm_runtime and rx_pn_runtime both embed rx_runtime as their first
 * member, so &my_rt.runtime gives the rx_runtime * to pass here.
 * The tick vtable entry is set by rx_fsm_runtime_init / rx_pn_runtime_init.
 *
 * Platform-specific time primitives (rx_tick_t, rx_tick_now,
 * rx_tick_add_us, rx_tick_sleep_until) are provided by rxnet/port.h,
 * which is included transitively here.
 */

#include "rxnet/config.h"
#include "rxnet/port.h"
#include "rxnet/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Cyclic executive                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    rx_runtime *rt;
} rx_ce_task;

typedef struct {
    rx_runtime    *rt;
    unsigned char  node_idx;
} rx_ce_activation;

typedef struct {
    rx_ce_activation activation[RXNET_CE_MAX_TASKS * RXNET_MAX_RUNTIME_NODES];
    int              count;
} rx_ce_slot;

typedef struct {
    rx_ce_task tasks[RXNET_CE_MAX_TASKS];
    int        ntasks;
    rx_ce_slot slots[RXNET_CE_MAX_SLOTS];
    int        nslots;   /* 0 until rx_cyclic_exec_build() is called */
    long       base_us;
    int        sched_check_enabled;
    volatile int stop_requested;
    void     (*on_stop)(void *user);
    void      *on_stop_user;
} rx_cyclic_exec;

/* Initialise to empty. */
void rx_cyclic_exec_init(rx_cyclic_exec *ce);

/*
 * Register a runtime with the cyclic executive.
 *
 * rx_runtime_build() is called here to validate scheduling metadata.  The
 * cyclic executive owns the hyperperiod activation table.
 *
 * Returns 0 on success, -1 if RXNET_CE_MAX_TASKS is exceeded, the runtime
 * has no periodic nodes (period_us == 0 after build), or build fails.
 */
int rx_cyclic_exec_add(rx_cyclic_exec *ce, rx_runtime *rt);

/* Compute base tick and activation table from registered runtime nodes.
 * Called automatically by rx_cyclic_exec_run() if not called first.
 * Returns 0 on success, -1 if tasks list is empty or table overflows
 * RXNET_CE_MAX_SLOTS. */
int rx_cyclic_exec_build(rx_cyclic_exec *ce);

int rx_cyclic_exec_enable_sched_check(rx_cyclic_exec *ce, int enabled);
int rx_cyclic_exec_check_schedulability(rx_cyclic_exec *ce,
                                        rx_sched_report *report,
                                        FILE *log);

/* Enter the scheduler loop.  Builds the table first if not yet built.
 * Returns after rx_cyclic_exec_stop() is requested. */
void rx_cyclic_exec_run(rx_cyclic_exec *ce);

/* Request rx_cyclic_exec_run() to return at the next safe point. */
void rx_cyclic_exec_stop(rx_cyclic_exec *ce);

/* Register an optional callback executed once before run() returns. */
void rx_cyclic_exec_on_stop(rx_cyclic_exec *ce,
                            void (*callback)(void *user),
                            void *user);

#ifdef __cplusplus
}
#endif
