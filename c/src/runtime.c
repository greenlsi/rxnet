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

static void
shared_resource_table_init(rx_shared_resource_table *table)
{
    size_t i;
    rx_mutex_init(&table->table_lock);
    for (i = 0; i < RXNET_MAX_SHARED_RESOURCES; ++i) {
        table->entries[i].in_use = 0;
        table->entries[i].resource_id = 0;
        rx_mutex_init(&table->entries[i].lock);
    }
}

static rx_mutex_t *
shared_resource_lock(rx_shared_resource_table *table, int resource_id)
{
    size_t i;
    rx_mutex_t *lock = NULL;

    if (table == NULL) return NULL;
    rx_mutex_lock(&table->table_lock);
    for (i = 0; i < RXNET_MAX_SHARED_RESOURCES; ++i) {
        if (table->entries[i].in_use &&
            table->entries[i].resource_id == resource_id) {
            lock = &table->entries[i].lock;
            break;
        }
    }
    if (lock == NULL) {
        for (i = 0; i < RXNET_MAX_SHARED_RESOURCES; ++i) {
            if (!table->entries[i].in_use) {
                table->entries[i].in_use = 1;
                table->entries[i].resource_id = resource_id;
                lock = &table->entries[i].lock;
                break;
            }
        }
    }
    rx_mutex_unlock(&table->table_lock);
    return lock;
}

static void
node_entry_record_resource_access(rx_node_entry *entry, int resource_id, long elapsed_us)
{
    int i;

    if (entry == NULL) return;
    for (i = 0; i < entry->resource_access_count; ++i) {
        if (entry->resource_accesses[i].resource_id == resource_id) {
            if (elapsed_us > entry->resource_accesses[i].max_us) {
                entry->resource_accesses[i].max_us = elapsed_us;
            }
            return;
        }
    }
    if (entry->resource_access_count < (int)RXNET_MAX_SHARED_RESOURCES) {
        entry->resource_accesses[entry->resource_access_count].resource_id = resource_id;
        entry->resource_accesses[entry->resource_access_count].max_us = elapsed_us;
        entry->resource_access_count++;
    }
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
    ctx->activation_us     = 0;
    ctx->active_entry      = NULL;
    ctx->shared_resources  = NULL;
    ctx->critical_locking_enabled = 0;
    ctx->critical_resource_id = 0;
    ctx->critical_started_at = 0;

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
    ctx->activation_us = 0;
    ctx->active_entry = NULL;
    ctx->shared_resources = NULL;
    ctx->critical_locking_enabled = 0;
}

void rx_context_destroy(rx_context *ctx) {
    if (ctx == NULL) {
        return;
    }

    rx_context_free(ctx);
    free(ctx);
}

long rx_context_activation_us(const rx_context *ctx) {
    return ctx == NULL ? 0 : ctx->activation_us;
}

void rx_context_set_activation_us(rx_context *ctx, long activation_us) {
    if (ctx == NULL) {
        return;
    }
    ctx->activation_us = activation_us;
}

int rx_context_critical_begin(rx_context *ctx, int resource_id) {
    rx_mutex_t *lock;

    if (ctx == NULL) return -1;
    if (ctx->critical_started_at != 0) return -1;
    if (ctx->critical_locking_enabled) {
        lock = shared_resource_lock(ctx->shared_resources, resource_id);
        if (lock == NULL) return -1;
        rx_mutex_lock(lock);
    }
    ctx->critical_resource_id = resource_id;
    ctx->critical_started_at = rx_tick_now();
    return 0;
}

int rx_context_critical_end(rx_context *ctx) {
    rx_tick_t finished_at;
    long elapsed_us;
    rx_mutex_t *lock;

    if (ctx == NULL || ctx->critical_started_at == 0) return -1;
    finished_at = rx_tick_now();
    elapsed_us = (long)((finished_at - ctx->critical_started_at + 999) / 1000);
    if (elapsed_us < 1) elapsed_us = 1;
    node_entry_record_resource_access(ctx->active_entry,
                                      ctx->critical_resource_id,
                                      elapsed_us);
    if (ctx->critical_locking_enabled) {
        lock = shared_resource_lock(ctx->shared_resources, ctx->critical_resource_id);
        if (lock != NULL) rx_mutex_unlock(lock);
    }
    ctx->critical_started_at = 0;
    return 0;
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
    shared_resource_table_init(&rt->shared_resources);
    ctx->shared_resources = &rt->shared_resources;

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
    rt->nodes[rt->node_count].resource_access_count = 0;
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
    return rx_runtime_tick_nodes_at(rt, node_idx, count, 0);
}

int rx_runtime_tick_nodes_at(rx_runtime *rt,
                             const unsigned char *node_idx,
                             int count,
                             long activation_us) {
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

    rx_context_set_activation_us(rt->ctx, activation_us);
    rt->ctx->critical_locking_enabled = 0;

    for (i = 0; i < count; ++i) {
        rx_node *node = rt->nodes[node_idx[i]].node;
        rt->ctx->active_entry = &rt->nodes[node_idx[i]];
        started_at[i] = rx_tick_now();
        RX_TRACE_NODE_START(node);
        RX_TRACE_PH_START(node, RX_TRACE_PH_LATCH);
        node->vtable->latch_inputs(node, rt->ctx);
        RX_TRACE_PH_END(node, RX_TRACE_PH_LATCH);
    }
    for (i = 0; i < count; ++i) {
        rx_node *node = rt->nodes[node_idx[i]].node;
        rt->ctx->active_entry = &rt->nodes[node_idx[i]];
        RX_TRACE_PH_START(node, RX_TRACE_PH_EVAL);
        node->vtable->evaluate(node, rt->ctx);
        RX_TRACE_PH_END(node, RX_TRACE_PH_EVAL);
    }
    for (i = 0; i < count; ++i) {
        rx_node *node = rt->nodes[node_idx[i]].node;
        rt->ctx->active_entry = &rt->nodes[node_idx[i]];
        RX_TRACE_PH_START(node, RX_TRACE_PH_COMMIT);
        node->vtable->commit(node, rt->ctx);
        RX_TRACE_PH_END(node, RX_TRACE_PH_COMMIT);
    }
    rx_context_dispatch_deferred(rt->ctx);
    for (i = 0; i < count; ++i) {
        rx_node *node = rt->nodes[node_idx[i]].node;
        rx_tick_t finished_at;
        long elapsed_us;
        rt->ctx->active_entry = &rt->nodes[node_idx[i]];
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
    rt->ctx->active_entry = NULL;

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
