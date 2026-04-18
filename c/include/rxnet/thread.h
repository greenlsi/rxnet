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
 *   rx_thread_exec_run(&te);  // never returns
 *
 * Usage — multiple runtimes (mixed PN + FSM):
 *
 *   rx_thread_exec_add(&te, &pn_rt.runtime);   // PN nodes → threads
 *   rx_thread_exec_add(&te, &cli_rt.runtime);  // cli node → main thread
 *   rx_thread_exec_run(&te);  // never returns
 */

#include <pthread.h>
#include <time.h>

#include "rxnet/config.h"
#include "rxnet/cyclic.h"   /* rx_timespec_add_us, rx_sleep_until */
#include "rxnet/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Portable generation barrier (macOS lacks pthread_barrier_t)         */
/* ------------------------------------------------------------------ */

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    unsigned int    waiting;
    unsigned int    total;
    unsigned int    generation;
} rx_thread_barrier;

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
    rx_runtime       *rt;
    rx_context        node_ctx[RXNET_MAX_RUNTIME_NODES];
    rx_thread_barrier latch_b[RXNET_MAX_RUNTIME_SLOTS];
    rx_thread_barrier commit_b[RXNET_MAX_RUNTIME_SLOTS];
    rx_thread_arg     args[RXNET_MAX_RUNTIME_NODES];
    pthread_t         tids[RXNET_MAX_RUNTIME_NODES];
} rx_thread_group;

/* ------------------------------------------------------------------ */
/* Thread executor                                                      */
/* ------------------------------------------------------------------ */

struct rx_thread_exec {
    rx_thread_group groups[RXNET_THREAD_MAX_RUNTIMES];
    int             ngroups;
    struct timespec t0;   /* common start time for all groups */
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
 * Never returns.
 */
void rx_thread_exec_run(rx_thread_exec *te);

#ifdef __cplusplus
}
#endif
