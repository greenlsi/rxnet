#include "rxnet/runtime.h"

#include <stdlib.h>

static void rx_runtime_noop_node_phase(rx_node *node, rx_context *ctx) {
    (void)node;
    (void)ctx;
}

int rx_context_init(rx_context *ctx) {
    if (ctx == NULL) {
        return -1;
    }

    ctx->deferred_capacity = RXNET_MAX_DEFERRED_ACTIONS;
    ctx->deferred_actions = ctx->deferred_actions_storage;
    ctx->deferred_count = 0;

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

int rx_context_enqueue_deferred_action(rx_context *ctx, rx_deferred_action_fn fn, void *user) {
    if (ctx == NULL || fn == NULL) {
        return -1;
    }

    if (ctx->deferred_count >= ctx->deferred_capacity) {
        return -1;
    }

    ctx->deferred_actions[ctx->deferred_count].fn = fn;
    ctx->deferred_actions[ctx->deferred_count].user = user;
    ctx->deferred_count += 1;
    return 0;
}

void rx_context_run_deferred_actions(rx_context *ctx) {
    size_t i;

    if (ctx == NULL) {
        return;
    }

    for (i = 0; i < ctx->deferred_count; ++i) {
        ctx->deferred_actions[i].fn(ctx, ctx->deferred_actions[i].user);
    }

    ctx->deferred_count = 0;
}

int rx_runtime_init(rx_runtime *rt, rx_context *ctx, size_t node_capacity) {
    if (rt == NULL || ctx == NULL) {
        return -1;
    }
    if (node_capacity > RXNET_MAX_RUNTIME_NODES) {
        return -1;
    }

    rt->ctx = ctx;
    rt->nodes = rt->nodes_storage;
    rt->node_count = 0;
    rt->node_capacity = node_capacity;

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

    rt->ctx = NULL;
    rt->nodes = NULL;
    rt->node_count = 0;
    rt->node_capacity = 0;
}

void rx_runtime_destroy(rx_runtime *rt) {
    if (rt == NULL) {
        return;
    }

    rx_runtime_free(rt);
    free(rt);
}

int rx_runtime_add_node(rx_runtime *rt, rx_node *node) {
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

    rt->nodes[rt->node_count++] = node;
    return 0;
}

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

int rx_tick(rx_runtime *rt) {
    size_t i;

    if (rt == NULL || rt->ctx == NULL) {
        return -1;
    }

    for (i = 0; i < rt->node_count; ++i) {
        rt->nodes[i]->vtable->latch_inputs(rt->nodes[i], rt->ctx);
    }

    for (i = 0; i < rt->node_count; ++i) {
        rt->nodes[i]->vtable->evaluate(rt->nodes[i], rt->ctx);
    }

    for (i = 0; i < rt->node_count; ++i) {
        rt->nodes[i]->vtable->commit(rt->nodes[i], rt->ctx);
    }

    rx_context_run_deferred_actions(rt->ctx);

    for (i = 0; i < rt->node_count; ++i) {
        rt->nodes[i]->vtable->dump_outputs(rt->nodes[i], rt->ctx);
    }

    return 0;
}
