// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static long
lcm(long a, long b)
{
    return a / gcd(a, b) * b;
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
    rt->node_count++;

    /* Invalidate any previously built slot table. */
    rt->nslots = 0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* rx_runtime_build — hyperperiod slot table                            */
/* ------------------------------------------------------------------ */

/*
 * Stable insertion sort ascending by effective deadline for the first
 * `count` entries in slot->node_idx.  All entries are periodic nodes.
 * Effective deadline = deadline_us if > 0, else period_us.
 */
static void sort_slot_by_deadline(rx_runtime_slot *slot, int count,
                                  const rx_node_entry *nodes)
{
    int i, j;
    unsigned char tmp;
    long dl_i, dl_j;

    for (i = 1; i < count; ++i) {
        tmp  = slot->node_idx[i];
        dl_i = nodes[tmp].deadline_us > 0
               ? nodes[tmp].deadline_us : nodes[tmp].period_us;
        j = i;
        while (j > 0) {
            unsigned char prev = slot->node_idx[j - 1];
            dl_j = nodes[prev].deadline_us > 0
                   ? nodes[prev].deadline_us : nodes[prev].period_us;
            if (dl_j <= dl_i) break;
            slot->node_idx[j] = slot->node_idx[j - 1];
            --j;
        }
        slot->node_idx[j] = tmp;
    }
}

int rx_runtime_build(rx_runtime *rt)
{
    size_t i;
    int    s, nslots;
    long   base_us = 0, hyper_us = 0;

    if (rt == NULL || rt->node_count == 0) {
        return -1;
    }

    /* Compute base tick (GCD) and hyperperiod (LCM) from periodic nodes. */
    for (i = 0; i < rt->node_count; ++i) {
        long p = rt->nodes[i].period_us;
        if (p <= 0) continue; /* async node — skip */
        if (base_us == 0) {
            base_us  = p;
            hyper_us = p;
        } else {
            base_us  = gcd(base_us, p);
            hyper_us = lcm(hyper_us, p);
        }
    }

    /* If all nodes are async, one slot covers everything. */
    nslots = (base_us == 0) ? 1 : (int)(hyper_us / base_us);

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
    if (nslots > (int)RXNET_MAX_RUNTIME_SLOTS) {
        fprintf(stderr,
                "rxnet: hyperperiod requires %d slots, limit is %u; "
                "increase RXNET_MAX_RUNTIME_SLOTS\n",
                nslots, RXNET_MAX_RUNTIME_SLOTS);
        return -1;
    }

    /* Build per-slot activation lists. */
    for (s = 0; s < nslots; ++s) {
        int periodic_count = 0;

        /* 1. Periodic nodes active in this slot. */
        for (i = 0; i < rt->node_count; ++i) {
            long p = rt->nodes[i].period_us;
            if (p > 0 && ((long)s * base_us) % p == 0) {
                rt->slots_storage[s].node_idx[periodic_count++] = (unsigned char)i;
            }
        }
        rt->slots_storage[s].count = periodic_count;

        /* 2. Sort periodic nodes by effective deadline (EDF, ascending). */
        sort_slot_by_deadline(&rt->slots_storage[s], periodic_count, rt->nodes);

        /* 3. Async nodes (period_us == 0) always run; append after periodic. */
        for (i = 0; i < rt->node_count; ++i) {
            if (rt->nodes[i].period_us == 0) {
                rt->slots_storage[s].node_idx[rt->slots_storage[s].count++] =
                    (unsigned char)i;
            }
        }
    }

    rt->period_us    = base_us;
    rt->nslots       = nslots;
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
/* rx_tick                                                              */
/* ------------------------------------------------------------------ */

int rx_tick(rx_runtime *rt) {
    size_t i;

    if (rt == NULL || rt->ctx == NULL) {
        return -1;
    }

    if (rt->nslots == 0) {
        /* Unscheduled / manual-tick mode: run all nodes. */
        for (i = 0; i < rt->node_count; ++i) {
            rx_node *node = rt->nodes[i].node;
            RX_TRACE_NODE_START(node);
            RX_TRACE_PH_START(node, RX_TRACE_PH_LATCH);
            node->vtable->latch_inputs(node, rt->ctx);
            RX_TRACE_PH_END(node, RX_TRACE_PH_LATCH);
        }
        for (i = 0; i < rt->node_count; ++i) {
            rx_node *node = rt->nodes[i].node;
            RX_TRACE_PH_START(node, RX_TRACE_PH_EVAL);
            node->vtable->evaluate(node, rt->ctx);
            RX_TRACE_PH_END(node, RX_TRACE_PH_EVAL);
        }
        for (i = 0; i < rt->node_count; ++i) {
            rx_node *node = rt->nodes[i].node;
            RX_TRACE_PH_START(node, RX_TRACE_PH_COMMIT);
            node->vtable->commit(node, rt->ctx);
            RX_TRACE_PH_END(node, RX_TRACE_PH_COMMIT);
        }
        rx_context_dispatch_deferred(rt->ctx);
        for (i = 0; i < rt->node_count; ++i) {
            rx_node *node = rt->nodes[i].node;
            RX_TRACE_PH_START(node, RX_TRACE_PH_DUMP);
            node->vtable->dump_outputs(node, rt->ctx);
            RX_TRACE_PH_END(node, RX_TRACE_PH_DUMP);
            RX_TRACE_NODE_END(node);
        }

    } else {
        /* Scheduled mode: run only active nodes for the current slot, EDF order. */
        const rx_runtime_slot *slot = &rt->slots_storage[rt->current_slot];
        size_t j;

        for (j = 0; j < (size_t)slot->count; ++j) {
            rx_node *node = rt->nodes[slot->node_idx[j]].node;
            RX_TRACE_NODE_START(node);
            RX_TRACE_PH_START(node, RX_TRACE_PH_LATCH);
            node->vtable->latch_inputs(node, rt->ctx);
            RX_TRACE_PH_END(node, RX_TRACE_PH_LATCH);
        }
        for (j = 0; j < (size_t)slot->count; ++j) {
            rx_node *node = rt->nodes[slot->node_idx[j]].node;
            RX_TRACE_PH_START(node, RX_TRACE_PH_EVAL);
            node->vtable->evaluate(node, rt->ctx);
            RX_TRACE_PH_END(node, RX_TRACE_PH_EVAL);
        }
        for (j = 0; j < (size_t)slot->count; ++j) {
            rx_node *node = rt->nodes[slot->node_idx[j]].node;
            RX_TRACE_PH_START(node, RX_TRACE_PH_COMMIT);
            node->vtable->commit(node, rt->ctx);
            RX_TRACE_PH_END(node, RX_TRACE_PH_COMMIT);
        }
        rx_context_dispatch_deferred(rt->ctx);
        for (j = 0; j < (size_t)slot->count; ++j) {
            rx_node *node = rt->nodes[slot->node_idx[j]].node;
            RX_TRACE_PH_START(node, RX_TRACE_PH_DUMP);
            node->vtable->dump_outputs(node, rt->ctx);
            RX_TRACE_PH_END(node, RX_TRACE_PH_DUMP);
            RX_TRACE_NODE_END(node);
        }
        rt->current_slot = (rt->current_slot + 1) % rt->nslots;
    }

    return 0;
}
