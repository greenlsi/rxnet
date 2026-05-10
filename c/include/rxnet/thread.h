// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*
 * rxnet/thread.h — Parallel thread-per-node scheduler with BSP barriers.
 *
 * Each periodic node registered in a runtime gets its own thread.  Activation
 * groups are created by the executor for absolute activation instants, without
 * materialising a hyperperiod table.  Two barriers per active group enforce the
 * reactive-synchronous execution phases:
 *
 *   eval_b:   all active nodes have latched and evaluated.
 *   commit_b: all active nodes have committed and dispatched deferred actions.
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
    int              priority_rank;
} rx_thread_arg;

typedef struct {
    int          in_use;
    long         activation_us;
    rx_tick_t    activation_tick;
    int          count;
    int          done;
    rx_barrier_t eval_b;
    rx_barrier_t commit_b;
} rx_thread_activation_group;

typedef struct {
    rx_runtime    *rt;
    rx_context     node_ctx[RXNET_MAX_RUNTIME_NODES];
    rx_mutex_t     activation_lock;
    rx_thread_activation_group
                   active[RXNET_THREAD_MAX_ACTIVE_GROUPS];
    rx_thread_arg  args[RXNET_MAX_RUNTIME_NODES];
    rx_thread_t    tids[RXNET_MAX_RUNTIME_NODES];
    int            thread_count;
} rx_thread_group;

/* ------------------------------------------------------------------ */
/* Thread executor                                                      */
/* ------------------------------------------------------------------ */

struct rx_thread_exec {
    rx_thread_group groups[RXNET_THREAD_MAX_RUNTIMES];
    int             ngroups;
    rx_tick_t       t0;   /* common start time for all groups */
    int             sched_check_enabled;
    int             fifo_configured;
    int             fifo_config_attempted;
    volatile int    stop_requested;
    void          (*on_stop)(void *user);
    void           *on_stop_user;
};

/* Initialise to empty. */
void rx_thread_exec_init(rx_thread_exec *te);

/*
 * Register a runtime.
 *
 * Periodic nodes are supported.  Async nodes (period_us == 0) are rejected
 * because a single async thread cannot safely participate in multiple
 * overlapping activation groups.
 *
 * Returns 0 on success, -1 on error.
 */
int rx_thread_exec_add(rx_thread_exec *te, rx_runtime *rt);

int rx_thread_exec_enable_sched_check(rx_thread_exec *te, int enabled);
int rx_thread_exec_check_schedulability(rx_thread_exec *te,
                                        rx_sched_report *report,
                                        FILE *log);

/*
 * Start the scheduler.
 *
 * Spawns one thread per node across all groups, except the last node of
 * the last group which runs on the calling (main) thread.
 * Returns after rx_thread_exec_stop() is requested.
 */
void rx_thread_exec_run(rx_thread_exec *te);

/* Request rx_thread_exec_run() to return after each node finishes its current tick. */
void rx_thread_exec_stop(rx_thread_exec *te);

/* Register an optional callback executed once before run() returns. */
void rx_thread_exec_on_stop(rx_thread_exec *te,
                            void (*callback)(void *user),
                            void *user);

#ifdef __cplusplus
}
#endif
