// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "rxnet/config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rx_context rx_context;
typedef struct rx_runtime rx_runtime;
typedef struct rx_node rx_node;
typedef struct rx_node_vtable rx_node_vtable;
typedef struct rx_node_entry rx_node_entry;
typedef struct rx_runtime_slot rx_runtime_slot;
typedef struct rx_deferred_action_entry rx_deferred_action_entry;
typedef struct rx_worker_pool rx_worker_pool;
typedef struct rx_sched_task_result rx_sched_task_result;
typedef struct rx_sched_report rx_sched_report;

typedef void (*rx_deferred_action_fn)(rx_context *ctx, void *user);
typedef void (*rx_node_phase_fn)(rx_node *node, rx_context *ctx);

#define RX_SCHED_UNSCHEDULABLE 0
#define RX_SCHED_SCHEDULABLE   1
#define RX_SCHED_ERROR        -1
#define RX_SCHED_UNSUPPORTED  -2

#define RXNET_SCHED_MAX_TASKS (RXNET_CE_MAX_TASKS * RXNET_MAX_RUNTIME_NODES)

struct rx_sched_task_result {
    rx_runtime   *rt;
    unsigned char node_idx;
    long          period_us;
    long          deadline_us;
    long          wcet_us;
    long          interference_us;
    long          blocking_us;
    long          response_us;
    int           schedulable;
};

struct rx_sched_report {
    int schedulable;
    int unsupported;
    int task_count;
    rx_sched_task_result tasks[RXNET_SCHED_MAX_TASKS];
};

/* ------------------------------------------------------------------ */
/* Priority                                                             */
/* ------------------------------------------------------------------ */

typedef enum {
    RX_PRIORITY_LOW      = 0,
    RX_PRIORITY_NORMAL   = 1,
    RX_PRIORITY_HIGH     = 2,
    RX_PRIORITY_CRITICAL = 3,
} rx_priority_t;

/* ------------------------------------------------------------------ */
/* Worker pool (user-provided, vtable interface)                        */
/* ------------------------------------------------------------------ */

/*
 * rx_worker_pool — pluggable async executor for deferred actions.
 *
 * Provide an implementation and register it with
 * rx_runtime_set_worker_pool().  rxnet does not supply a built-in
 * implementation: use FreeRTOS task queues, POSIX thread pools, or any
 * other mechanism that suits the target platform.
 *
 * Thread safety
 * -------------
 * post() MUST be thread-safe; it may be called from any context
 * (including ISRs on platforms that support deferred interrupt handling).
 */
struct rx_worker_pool {
    void (*post)(rx_worker_pool *self,
                 rx_deferred_action_fn fn,
                 rx_context *ctx,
                 void *user,
                 rx_priority_t priority);
};

/* ------------------------------------------------------------------ */
/* Deferred action queue entry                                          */
/* ------------------------------------------------------------------ */

struct rx_deferred_action_entry {
    rx_deferred_action_fn fn;
    void *user;
    rx_priority_t priority;
};

struct rx_context {
    rx_deferred_action_entry deferred_actions_storage[RXNET_MAX_DEFERRED_ACTIONS];
    rx_deferred_action_entry *deferred_actions;
    size_t deferred_count;
    size_t deferred_capacity;
    rx_worker_pool *worker_pool; /* NULL = synchronous deferred (default) */
};

struct rx_node_vtable {
    rx_node_phase_fn latch_inputs;
    void (*evaluate)(rx_node *node, rx_context *ctx);
    void (*commit)(rx_node *node, rx_context *ctx);
    rx_node_phase_fn dump_outputs;
};

struct rx_node {
    const rx_node_vtable *vtable;
    rx_node_phase_fn latch_inputs_cb;
    rx_node_phase_fn dump_outputs_cb;
#ifdef RX_TRACE_ENABLE
    struct rx_trace_buf *trace;  /* NULL = not traced; set by rx_trace_attach() */
    uint8_t              trace_nid;
#endif
};

/* ------------------------------------------------------------------ */
/* Per-node scheduling entry                                            */
/* ------------------------------------------------------------------ */

/*
 * rx_node_entry — associates a node with its scheduling parameters.
 *
 * period_us   Activation period in microseconds.  0 = async: executors
 *             include the node when they service the runtime.
 * deadline_us Relative deadline in microseconds.  0 = same as period_us.
 *             Must satisfy deadline_us <= period_us when both are > 0.
 *
 * Passed to rx_runtime_add_node() (and the model-specific wrappers
 * rx_fsm_runtime_add_machine() / rx_pn_runtime_add_net()).
 */
struct rx_node_entry {
    rx_node *node;
    long     period_us;
    long     deadline_us;
    long     wcet_us;
};

/* ------------------------------------------------------------------ */
/* Deprecated per-slot activation record                               */
/* ------------------------------------------------------------------ */

/*
 * rx_runtime_slot is retained for ABI/source compatibility with the thread
 * executor while its scheduler is being moved out of the runtime.  The base
 * runtime no longer owns or fills a hyperperiod activation table.
 */
struct rx_runtime_slot {
    unsigned char node_idx[RXNET_MAX_RUNTIME_NODES]; /* indices into rx_runtime.nodes */
    int           count;
};

/* ------------------------------------------------------------------ */
/* Runtime                                                              */
/* ------------------------------------------------------------------ */

struct rx_runtime {
    int (*tick)(struct rx_runtime *self); /* set by fsm/pn init; NULL until then */

    /*
     * Computed by rx_runtime_build(): GCD of all periodic node periods.
     * 0 = not yet built, or all nodes are async.
     * The cyclic executive reads this after build to know the call rate.
     */
    long period_us;

    rx_context *ctx;

    /* Node registry (filled by rx_runtime_add_node). */
    rx_node_entry  nodes_storage[RXNET_MAX_RUNTIME_NODES];
    rx_node_entry *nodes;
    size_t node_count;
    size_t node_capacity;

    /* Deprecated scheduler state.  Runtime no longer fills this table. */
    rx_runtime_slot slots_storage[RXNET_MAX_RUNTIME_SLOTS];
    int nslots;
    int current_slot;
};

/*
 * Thread safety
 * -------------
 * rx_tick() is NOT thread-safe.  Only one thread may call it at a time for
 * a given rx_runtime.  If other threads produce data consumed by latch
 * callbacks, the caller is responsible for external synchronisation (e.g. a
 * mutex around the tick call, or lock-free ring buffers written by producers
 * and drained inside latch callbacks).
 */

int rx_context_init(rx_context *ctx);
rx_context *rx_context_create(void);
void rx_context_free(rx_context *ctx);
void rx_context_destroy(rx_context *ctx);

/* Enqueue a deferred action with NORMAL priority (backward-compatible). */
int rx_context_enqueue_deferred_action(rx_context *ctx, rx_deferred_action_fn fn, void *user);

/* Enqueue a deferred action with explicit priority. */
int rx_context_enqueue_deferred_action_p(rx_context *ctx, rx_deferred_action_fn fn,
                                          void *user, rx_priority_t priority);

/* Run all enqueued actions synchronously in priority order (FIFO within priority). */
void rx_context_run_deferred_actions(rx_context *ctx);

/* Dispatch deferred actions:
 *   - worker_pool == NULL (or ctx->worker_pool == NULL): runs synchronously
 *     in priority order before returning (identical to run_deferred_actions).
 *   - worker_pool != NULL: posts every action to the pool and returns
 *     immediately; actions may run after dump and their results arrive at
 *     the next latch via ctx inputs.
 * rx_tick() calls this instead of run_deferred_actions(). */
void rx_context_dispatch_deferred(rx_context *ctx);

/* Register a worker pool on the runtime's context.
 * Pass NULL to revert to synchronous deferred execution. */
void rx_runtime_set_worker_pool(rx_runtime *rt, rx_worker_pool *pool);

int rx_runtime_init(rx_runtime *rt, rx_context *ctx, size_t node_capacity);
rx_runtime *rx_runtime_create(rx_context *ctx, size_t node_capacity);
void rx_runtime_free(rx_runtime *rt);
void rx_runtime_destroy(rx_runtime *rt);

/*
 * Register a node with its scheduling parameters.
 *
 * period_us   0 = async (runs every base tick); > 0 = periodic.
 * deadline_us 0 = same as period_us; > 0 = explicit deadline (<= period_us).
 *
 * Invalidates previously computed scheduling metadata (period_us/nslots reset).
 * Returns 0 on success, -1 on invalid arguments or capacity exceeded.
 */
int rx_runtime_add_node(rx_runtime *rt, rx_node *node,
                        long period_us, long deadline_us);

/*
 * Validate scheduling parameters and compute runtime metadata.
 *
 * Computes:
 *   base_us = GCD of all periodic node periods  → rt->period_us
 *
 * Performs schedulability checks:
 *   - deadline_us <= period_us for all periodic nodes with explicit deadlines
 * It does not build per-slot activation lists; executors own scheduling.
 *
 * Called automatically by multi-rate executives when a runtime is registered.
 * Returns 0 on success, -1 on error (prints reason to stderr).
 */
int rx_runtime_build(rx_runtime *rt);

/*
 * Execute one synchronous tick for an explicit ordered list of node indices.
 * Executors own scheduling policy and pass the active nodes for this tick.
 */
int rx_runtime_tick_nodes(rx_runtime *rt,
                          const unsigned char *node_idx,
                          int count);

void rx_node_set_latch_inputs_callback(rx_node *node, rx_node_phase_fn cb);
void rx_node_set_dump_outputs_callback(rx_node *node, rx_node_phase_fn cb);

/*
 * Execute one tick of the runtime.
 *
 * Runs all registered nodes in registration order (manual-tick mode).
 * Multi-rate executives call rx_runtime_tick_nodes() with their own active
 * node list instead.
 */
int rx_tick(rx_runtime *rt);

#ifdef __cplusplus
}
#endif
