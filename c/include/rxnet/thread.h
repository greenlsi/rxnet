// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*
 * rxnet/thread.h — Parallel thread-per-node scheduler with BSP barriers.
 *
 * Each node registered in a runtime gets its own pthread.  Two barriers per
 * hyperperiod slot synchronise the reactive-synchronous execution phases:
 *
 *   latch_b[s]:  all nodes active in slot s arrive → latch inputs in
 *                parallel → evaluate in parallel.
 *   commit_b[s]: all evaluations done → commit outputs in parallel.
 *
 * Each node gets its own rx_context (per-node deferred action queue).
 * Input drivers must be non-destructive reads (same GPIO register read
 * twice within a tick window returns the same value).
 *
 * Multiple runtimes can be registered.  Each runtime forms an independent
 * group with its own barrier set; nodes in different runtimes run
 * independently.  The last node of the last runtime runs on the calling
 * (main) thread, so CLI/stdin nodes should be added to the last runtime.
 *
 * Usage — single runtime (all FSM nodes):
 *
 *   rx_fsm_runtime_add_machine(&rt, &light_a, 10000, 0);
 *   rx_fsm_runtime_add_machine(&rt, &blink_b, 10000, 0);
 *   rx_fsm_runtime_add_machine(&rt, &auto_c,  20000, 0);
 *   rx_fsm_runtime_add_machine(&rt, &cli,     10000, 0);  // last → main
 *
 *   rx_thread_exec te;
 *   rx_thread_exec_init(&te);
 *   rx_thread_exec_add(&te, &rt.runtime);
 *   rx_thread_exec_run(&te);  // returns after stop
 *
 * Usage — multiple runtimes (mixed PN + FSM):
 *
 *   rx_thread_exec_add(&te, &pn_rt.runtime);   // PN nodes → threads
 *   rx_thread_exec_add(&te, &cli_rt.runtime);  // cli node → main thread
 *   rx_thread_exec_run(&te);  // returns after stop
 */

#include "rxnet/config.h"
#include "rxnet/port.h"     /* rx_tick_t, rx_thread_t, rx_barrier_t */
#include "rxnet/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Per-runtime group                                                    */
/* ------------------------------------------------------------------ */

typedef struct rx_thread_exec rx_thread_exec;

typedef struct {
    rx_thread_exec  *te;
    int              group_idx;
    int              node_idx;
} rx_thread_arg;

typedef struct {
    rx_runtime    *rt;
    rx_context     node_ctx[RXNET_MAX_RUNTIME_NODES];
    rx_barrier_t   latch_b[RXNET_MAX_RUNTIME_SLOTS];
    rx_barrier_t   commit_b[RXNET_MAX_RUNTIME_SLOTS];
    rx_thread_arg  args[RXNET_MAX_RUNTIME_NODES];
    rx_thread_t    tids[RXNET_MAX_RUNTIME_NODES];
} rx_thread_group;

/* ------------------------------------------------------------------ */
/* Thread executor                                                      */
/* ------------------------------------------------------------------ */

struct rx_thread_exec {
    rx_thread_group groups[RXNET_THREAD_MAX_RUNTIMES];
    int             ngroups;
    rx_tick_t       t0;   /* common start time for all groups */
    volatile int    stop_requested;
    void          (*on_stop)(void *user);
    void           *on_stop_user;
};

/* Initialise to empty. */
void rx_thread_exec_init(rx_thread_exec *te);

/*
 * Register a runtime.
 *
 * Calls rx_runtime_build() if not already built.  Initialises per-node
 * contexts and per-slot barriers for the new group.
 *
 * Returns 0 on success, -1 on error.
 */
int rx_thread_exec_add(rx_thread_exec *te, rx_runtime *rt);

/*
 * Start the scheduler.
 *
 * Spawns one pthread per node across all groups, except the last node of
 * the last group which runs on the calling (main) thread.
 * Returns after rx_thread_exec_stop() is requested.
 */
void rx_thread_exec_run(rx_thread_exec *te);

/* Request rx_thread_exec_run() to return at a hyperperiod boundary. */
void rx_thread_exec_stop(rx_thread_exec *te);

/* Register an optional callback executed once before run() returns. */
void rx_thread_exec_on_stop(rx_thread_exec *te,
                            void (*callback)(void *user),
                            void *user);

#ifdef __cplusplus
}
#endif
