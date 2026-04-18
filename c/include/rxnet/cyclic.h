#pragma once

/*
 * rxnet/cyclic.h — Cyclic executive with hyperperiod dispatch table,
 *                  plus POSIX time utilities shared by all schedulers.
 *
 * ---- Cyclic executive -------------------------------------------
 *
 * A cyclic executive runs tasks at fixed periods using a static
 * dispatch table computed once at startup:
 *
 *   base  = GCD of all task periods  (= minimum common divisor)
 *   hyper = LCM of all task periods  (= minimum common multiple)
 *   n_slots = hyper / base
 *
 * Task t is scheduled at slot s when (s * base) % t.period == 0.
 * The loop advances one base tick per iteration, sleeping until the
 * next deadline with rx_sleep_until().
 *
 * Usage:
 *
 *   rx_cyclic_exec ce;
 *   rx_cyclic_exec_init(&ce);
 *   rx_cyclic_exec_add(&ce, &fast_rt.runtime, 10000);   // 10 ms
 *   rx_cyclic_exec_add(&ce, &slow_rt.runtime, 20000);   // 20 ms
 *   rx_cyclic_exec_add(&ce, &cli_rt.runtime,  10000);   // 10 ms
 *   rx_cyclic_exec_run(&ce);                            // never returns
 *
 * rx_fsm_runtime and rx_pn_runtime both embed rx_runtime as their first
 * member, so &my_rt.runtime gives the rx_runtime * to pass here.
 * The tick vtable entry is set by rx_fsm_runtime_init / rx_pn_runtime_init.
 *
 * ---- Time utilities ---------------------------------------------
 *
 * rx_timespec_add_us, rx_timespec_compare, and rx_sleep_until are
 * general-purpose helpers useful in both cyclic executives and
 * multi-threaded schedulers (include this header for access).
 */

#include <time.h>

#include "rxnet/config.h"
#include "rxnet/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Time utilities                                                       */
/* ------------------------------------------------------------------ */

/* Add us microseconds to t, normalising tv_nsec. */
struct timespec rx_timespec_add_us(struct timespec t, long us);

/* Returns -1, 0, or +1 like strcmp. */
int rx_timespec_compare(struct timespec a, struct timespec b);

/* Sleep (with EINTR retry) until target as measured by CLOCK_MONOTONIC. */
void rx_sleep_until(struct timespec target);

/* ------------------------------------------------------------------ */
/* Cyclic executive                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    rx_runtime *rt;
    long        period_us;
} rx_ce_task;

typedef struct {
    rx_runtime *rt[RXNET_CE_MAX_TASKS];
    int         count;
} rx_ce_slot;

typedef struct {
    rx_ce_task tasks[RXNET_CE_MAX_TASKS];
    int        ntasks;
    rx_ce_slot slots[RXNET_CE_MAX_SLOTS];
    int        nslots;   /* 0 until rx_cyclic_exec_build() is called */
    long       base_us;
} rx_cyclic_exec;

/* Initialise to empty. */
void rx_cyclic_exec_init(rx_cyclic_exec *ce);

/*
 * Register a runtime with the cyclic executive.
 *
 * The runtime's scheduling period is read from rt->period_us, which is
 * set by rx_runtime_build().  If the slot table has not been built yet,
 * rx_runtime_build() is called automatically here.
 *
 * Returns 0 on success, -1 if RXNET_CE_MAX_TASKS is exceeded, the runtime
 * has no periodic nodes (period_us == 0 after build), or build fails.
 */
int rx_cyclic_exec_add(rx_cyclic_exec *ce, rx_runtime *rt);

/* Compute base tick and dispatch table from the registered tasks.
 * Called automatically by rx_cyclic_exec_run() if not called first.
 * Returns 0 on success, -1 if tasks list is empty or table overflows
 * RXNET_CE_MAX_SLOTS. */
int rx_cyclic_exec_build(rx_cyclic_exec *ce);

/* Enter the scheduler loop.  Builds the table first if not yet built.
 * Never returns. */
void rx_cyclic_exec_run(rx_cyclic_exec *ce);

#ifdef __cplusplus
}
#endif
