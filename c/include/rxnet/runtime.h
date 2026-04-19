#pragma once

#include <stddef.h>
#include <stdint.h>

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

typedef void (*rx_deferred_action_fn)(rx_context *ctx, void *user);
typedef void (*rx_node_phase_fn)(rx_node *node, rx_context *ctx);

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
 * period_us   Activation period in microseconds.  0 = async: the node
 *             runs in every slot (event-driven; advances only when its
 *             own guards fire).
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
};

/* ------------------------------------------------------------------ */
/* Per-slot activation record (built by rx_runtime_build)              */
/* ------------------------------------------------------------------ */

/*
 * rx_runtime_slot — ordered list of node indices active in one base tick.
 *
 * Periodic nodes are listed first, sorted by effective deadline (EDF).
 * Async nodes (period_us == 0) are appended last.
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

    /* Internal hyperperiod dispatch table (filled by rx_runtime_build). */
    rx_runtime_slot slots_storage[RXNET_MAX_RUNTIME_SLOTS];
    int nslots;        /* 0 until rx_runtime_build() succeeds */
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
 * Invalidates any previously built slot table (nslots reset to 0).
 * Returns 0 on success, -1 on invalid arguments or capacity exceeded.
 */
int rx_runtime_add_node(rx_runtime *rt, rx_node *node,
                        long period_us, long deadline_us);

/*
 * Build the internal hyperperiod dispatch table.
 *
 * Computes:
 *   base_us  = GCD of all periodic node periods  → rt->period_us
 *   hyper_us = LCM of all periodic node periods
 *   nslots   = hyper_us / base_us
 *
 * Performs schedulability checks:
 *   - deadline_us <= period_us for all periodic nodes with explicit deadlines
 *   - nslots <= RXNET_MAX_RUNTIME_SLOTS
 *
 * Builds per-slot activation lists sorted by effective deadline (EDF).
 * Async nodes (period_us == 0) run in every slot, appended after periodic ones.
 *
 * Called automatically by rx_cyclic_exec_add() if not called first.
 * Returns 0 on success, -1 on error (prints reason to stderr).
 */
int rx_runtime_build(rx_runtime *rt);

void rx_node_set_latch_inputs_callback(rx_node *node, rx_node_phase_fn cb);
void rx_node_set_dump_outputs_callback(rx_node *node, rx_node_phase_fn cb);

/*
 * Execute one tick of the runtime.
 *
 * If the slot table has been built (nslots > 0):
 *   runs only the nodes active in the current slot, in EDF order,
 *   then advances to the next slot.
 *
 * If the slot table has not been built (nslots == 0):
 *   runs all registered nodes (unscheduled / manual-tick mode).
 */
int rx_tick(rx_runtime *rt);

#ifdef __cplusplus
}
#endif
