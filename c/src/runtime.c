// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rxnet/port.h"
#include "rxnet/runtime.h"
#include "rxnet/trace.h"

static void rx_runtime_noop_node_phase(rx_node *node, rx_context *ctx) {
    (void)node;
    (void)ctx;
}

/* ------------------------------------------------------------------ */
/* Math helpers                                                         */
/* ------------------------------------------------------------------ */

static long
gcd(long a, long b)
{
    while (b) { long t = b; b = a % b; a = t; }
    return a;
}

/* ------------------------------------------------------------------ */
/* Deferred action queue                                                */
/* ------------------------------------------------------------------ */

/* Stable insertion sort descending by priority (highest runs first). */
static void sort_deferred_descending(rx_deferred_action_entry *arr, size_t n) {
    size_t i, j;
    rx_deferred_action_entry tmp;

    for (i = 1; i < n; ++i) {
        tmp = arr[i];
        j = i;
        while (j > 0 && arr[j - 1].priority < tmp.priority) {
            arr[j] = arr[j - 1];
            --j;
        }
        arr[j] = tmp;
    }
}

int rx_context_init(rx_context *ctx) {
    if (ctx == NULL) {
        return -1;
    }

    ctx->deferred_capacity = RXNET_MAX_DEFERRED_ACTIONS;
    ctx->deferred_actions  = ctx->deferred_actions_storage;
    ctx->deferred_count    = 0;
    ctx->worker_pool       = NULL;

    return 0;
}

rx_context *rx_context_create(void) {
    rx_context *ctx = (rx_context *)malloc(sizeof(*ctx));

    if (ctx == NULL) {
        return NULL;
    }

    if (rx_context_init(ctx) != 0) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

void rx_context_free(rx_context *ctx) {
    if (ctx == NULL) {
        return;
    }

    ctx->deferred_actions = NULL;
    ctx->deferred_count = 0;
    ctx->deferred_capacity = 0;
}

void rx_context_destroy(rx_context *ctx) {
    if (ctx == NULL) {
        return;
    }

    rx_context_free(ctx);
    free(ctx);
}

int rx_context_enqueue_deferred_action_p(rx_context *ctx, rx_deferred_action_fn fn,
                                          void *user, rx_priority_t priority) {
    if (ctx == NULL || fn == NULL) {
        return -1;
    }

    if (ctx->deferred_count >= ctx->deferred_capacity) {
        return -1;
    }

    ctx->deferred_actions[ctx->deferred_count].fn       = fn;
    ctx->deferred_actions[ctx->deferred_count].user     = user;
    ctx->deferred_actions[ctx->deferred_count].priority = priority;
    ctx->deferred_count += 1;
    return 0;
}

int rx_context_enqueue_deferred_action(rx_context *ctx, rx_deferred_action_fn fn, void *user) {
    return rx_context_enqueue_deferred_action_p(ctx, fn, user, RX_PRIORITY_NORMAL);
}

void rx_context_run_deferred_actions(rx_context *ctx) {
    size_t i;

    if (ctx == NULL) {
        return;
    }

    sort_deferred_descending(ctx->deferred_actions, ctx->deferred_count);

    for (i = 0; i < ctx->deferred_count; ++i) {
        ctx->deferred_actions[i].fn(ctx, ctx->deferred_actions[i].user);
    }

    ctx->deferred_count = 0;
}

void rx_context_dispatch_deferred(rx_context *ctx) {
    size_t i;

    if (ctx == NULL) {
        return;
    }

    if (ctx->worker_pool == NULL) {
        rx_context_run_deferred_actions(ctx);
    } else {
        for (i = 0; i < ctx->deferred_count; ++i) {
            ctx->worker_pool->post(ctx->worker_pool,
                                   ctx->deferred_actions[i].fn,
                                   ctx,
                                   ctx->deferred_actions[i].user,
                                   ctx->deferred_actions[i].priority);
        }
        ctx->deferred_count = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Runtime                                                              */
/* ------------------------------------------------------------------ */

void rx_runtime_set_worker_pool(rx_runtime *rt, rx_worker_pool *pool) {
    if (rt == NULL || rt->ctx == NULL) {
        return;
    }
    rt->ctx->worker_pool = pool;
}

int rx_runtime_init(rx_runtime *rt, rx_context *ctx, size_t node_capacity) {
    if (rt == NULL || ctx == NULL) {
        return -1;
    }
    if (node_capacity > RXNET_MAX_RUNTIME_NODES) {
        return -1;
    }

    rt->tick         = NULL;
    rt->period_us    = 0;
    rt->ctx          = ctx;
    rt->nodes        = rt->nodes_storage;
    rt->node_count   = 0;
    rt->node_capacity = node_capacity;
    rt->nslots       = 0;
    rt->current_slot = 0;

    memset(rt->nodes_storage, 0, sizeof(rt->nodes_storage));
    memset(rt->slots_storage, 0, sizeof(rt->slots_storage));

    return 0;
}

rx_runtime *rx_runtime_create(rx_context *ctx, size_t node_capacity) {
    rx_runtime *rt = (rx_runtime *)malloc(sizeof(*rt));

    if (rt == NULL) {
        return NULL;
    }

    if (rx_runtime_init(rt, ctx, node_capacity) != 0) {
        free(rt);
        return NULL;
    }

    return rt;
}

void rx_runtime_free(rx_runtime *rt) {
    if (rt == NULL) {
        return;
    }

    rt->ctx          = NULL;
    rt->nodes        = NULL;
    rt->node_count   = 0;
    rt->node_capacity = 0;
    rt->period_us    = 0;
    rt->nslots       = 0;
    rt->current_slot = 0;
}

void rx_runtime_destroy(rx_runtime *rt) {
    if (rt == NULL) {
        return;
    }

    rx_runtime_free(rt);
    free(rt);
}

int rx_runtime_add_node(rx_runtime *rt, rx_node *node,
                        long period_us, long deadline_us) {
    if (rt == NULL || node == NULL || node->vtable == NULL) {
        return -1;
    }
    if (node->vtable->latch_inputs == NULL ||
        node->vtable->evaluate == NULL ||
        node->vtable->commit == NULL ||
        node->vtable->dump_outputs == NULL) {
        return -1;
    }
    if (rt->node_count >= rt->node_capacity) {
        return -1;
    }
    if (period_us < 0 || deadline_us < 0) {
        return -1;
    }

    rt->nodes[rt->node_count].node        = node;
    rt->nodes[rt->node_count].period_us   = period_us;
    rt->nodes[rt->node_count].deadline_us = deadline_us;
    rt->nodes[rt->node_count].wcet_us     = 0;
    rt->node_count++;

    /* Invalidate any previously computed scheduling metadata. */
    rt->period_us = 0;
    rt->nslots = 0;
    rt->current_slot = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* rx_runtime_build — scheduling metadata                               */
/* ------------------------------------------------------------------ */

int rx_runtime_build(rx_runtime *rt)
{
    size_t i;
    long   base_us = 0;

    if (rt == NULL || rt->node_count == 0) {
        return -1;
    }

    /* Compute base tick (GCD) from periodic nodes. */
    for (i = 0; i < rt->node_count; ++i) {
        long p = rt->nodes[i].period_us;
        if (p <= 0) continue; /* async node — skip */
        if (base_us == 0) {
            base_us = p;
        } else {
            base_us = gcd(base_us, p);
        }
    }

    /* Schedulability checks. */
    for (i = 0; i < rt->node_count; ++i) {
        long p = rt->nodes[i].period_us;
        long d = rt->nodes[i].deadline_us;
        if (p > 0 && d > 0 && d > p) {
            fprintf(stderr,
                    "rxnet: node %u: deadline (%ld us) > period (%ld us) — "
                    "not schedulable\n",
                    (unsigned)i, d, p);
            return -1;
        }
    }

    rt->period_us    = base_us;
    rt->nslots       = 0;
    rt->current_slot = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Node callbacks                                                       */
/* ------------------------------------------------------------------ */

void rx_node_set_latch_inputs_callback(rx_node *node, rx_node_phase_fn cb) {
    if (node == NULL) {
        return;
    }

    node->latch_inputs_cb = cb == NULL ? rx_runtime_noop_node_phase : cb;
}

void rx_node_set_dump_outputs_callback(rx_node *node, rx_node_phase_fn cb) {
    if (node == NULL) {
        return;
    }

    node->dump_outputs_cb = cb == NULL ? rx_runtime_noop_node_phase : cb;
}

/* ------------------------------------------------------------------ */
/* Tick execution                                                       */
/* ------------------------------------------------------------------ */

int rx_runtime_tick_nodes(rx_runtime *rt,
                          const unsigned char *node_idx,
                          int count) {
    int i;
    rx_tick_t started_at[RXNET_MAX_RUNTIME_NODES];
    if (rt == NULL || rt->ctx == NULL) {
        return -1;
    }
    if (count < 0 || (count > 0 && node_idx == NULL)) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if ((size_t)node_idx[i] >= rt->node_count) {
            return -1;
        }
    }

    for (i = 0; i < count; ++i) {
        rx_node *node = rt->nodes[node_idx[i]].node;
        started_at[i] = rx_tick_now();
        RX_TRACE_NODE_START(node);
        RX_TRACE_PH_START(node, RX_TRACE_PH_LATCH);
        node->vtable->latch_inputs(node, rt->ctx);
        RX_TRACE_PH_END(node, RX_TRACE_PH_LATCH);
    }
    for (i = 0; i < count; ++i) {
        rx_node *node = rt->nodes[node_idx[i]].node;
        RX_TRACE_PH_START(node, RX_TRACE_PH_EVAL);
        node->vtable->evaluate(node, rt->ctx);
        RX_TRACE_PH_END(node, RX_TRACE_PH_EVAL);
    }
    for (i = 0; i < count; ++i) {
        rx_node *node = rt->nodes[node_idx[i]].node;
        RX_TRACE_PH_START(node, RX_TRACE_PH_COMMIT);
        node->vtable->commit(node, rt->ctx);
        RX_TRACE_PH_END(node, RX_TRACE_PH_COMMIT);
    }
    rx_context_dispatch_deferred(rt->ctx);
    for (i = 0; i < count; ++i) {
        rx_node *node = rt->nodes[node_idx[i]].node;
        rx_tick_t finished_at;
        long elapsed_us;
        RX_TRACE_PH_START(node, RX_TRACE_PH_DUMP);
        node->vtable->dump_outputs(node, rt->ctx);
        RX_TRACE_PH_END(node, RX_TRACE_PH_DUMP);
        RX_TRACE_NODE_END(node);
        finished_at = rx_tick_now();
        elapsed_us = (long)((finished_at - started_at[i] + 999) / 1000);
        if (elapsed_us <= 0) {
            elapsed_us = 1;
        }
        if (elapsed_us > rt->nodes[node_idx[i]].wcet_us) {
            rt->nodes[node_idx[i]].wcet_us = elapsed_us;
        }
    }

    return 0;
}

int rx_tick(rx_runtime *rt) {
    unsigned char node_idx[RXNET_MAX_RUNTIME_NODES];
    size_t i;

    if (rt == NULL || rt->ctx == NULL) {
        return -1;
    }

    for (i = 0; i < rt->node_count; ++i) {
        node_idx[i] = (unsigned char)i;
    }

    return rx_runtime_tick_nodes(rt, node_idx, (int)rt->node_count);
}
