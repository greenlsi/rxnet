#include "rxnet/runtime.h"

#include <stdlib.h>
#include <string.h>

int rx_context_init(rx_context *ctx, void *inputs, size_t inputs_size) {
    if (ctx == NULL) {
        return -1;
    }

    ctx->inputs_size = inputs_size;
    ctx->inputs = inputs;
    ctx->latched_inputs = calloc(1, inputs_size);
    ctx->deferred_capacity = 8;
    ctx->deferred_actions = calloc(ctx->deferred_capacity, sizeof(*ctx->deferred_actions));
    ctx->deferred_count = 0;

    if ((inputs_size > 0 && (inputs == NULL || ctx->latched_inputs == NULL)) ||
        ctx->deferred_actions == NULL) {
        rx_context_free(ctx);
        return -1;
    }

    return 0;
}

void rx_context_free(rx_context *ctx) {
    if (ctx == NULL) {
        return;
    }

    /* inputs buffer ownership belongs to the application */
    free(ctx->latched_inputs);
    free(ctx->deferred_actions);

    ctx->inputs = NULL;
    ctx->latched_inputs = NULL;
    ctx->inputs_size = 0;
    ctx->deferred_actions = NULL;
    ctx->deferred_count = 0;
    ctx->deferred_capacity = 0;
}

void rx_context_latch_inputs(rx_context *ctx) {
    if (ctx == NULL || ctx->inputs_size == 0) {
        return;
    }

    memcpy(ctx->latched_inputs, ctx->inputs, ctx->inputs_size);
}

int rx_context_enqueue_deferred_action(rx_context *ctx, rx_deferred_action_fn fn, void *user) {
    size_t new_capacity;
    void *new_actions;

    if (ctx == NULL || fn == NULL) {
        return -1;
    }

    if (ctx->deferred_count >= ctx->deferred_capacity) {
        new_capacity = ctx->deferred_capacity == 0 ? 8 : ctx->deferred_capacity * 2;
        new_actions = realloc(ctx->deferred_actions, new_capacity * sizeof(*ctx->deferred_actions));
        if (new_actions == NULL) {
            return -1;
        }

        ctx->deferred_actions = new_actions;
        ctx->deferred_capacity = new_capacity;
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

    rt->ctx = ctx;
    rt->nodes = calloc(node_capacity, sizeof(*rt->nodes));
    rt->node_count = 0;
    rt->node_capacity = node_capacity;

    if (node_capacity > 0 && rt->nodes == NULL) {
        rx_runtime_free(rt);
        return -1;
    }

    return 0;
}

void rx_runtime_free(rx_runtime *rt) {
    if (rt == NULL) {
        return;
    }

    free(rt->nodes);
    rt->ctx = NULL;
    rt->nodes = NULL;
    rt->node_count = 0;
    rt->node_capacity = 0;
}

int rx_runtime_add_node(rx_runtime *rt, rx_node node) {
    if (rt == NULL || node.vtable == NULL || node.self == NULL) {
        return -1;
    }

    if (rt->node_count >= rt->node_capacity) {
        return -1;
    }

    rt->nodes[rt->node_count++] = node;
    return 0;
}

int rx_tick(rx_runtime *rt) {
    size_t i;

    if (rt == NULL || rt->ctx == NULL) {
        return -1;
    }

    rx_context_latch_inputs(rt->ctx);

    for (i = 0; i < rt->node_count; ++i) {
        rt->nodes[i].vtable->evaluate(rt->nodes[i].self, rt->ctx);
    }

    for (i = 0; i < rt->node_count; ++i) {
        rt->nodes[i].vtable->commit(rt->nodes[i].self, rt->ctx);
    }

    rx_context_run_deferred_actions(rt->ctx);
    return 0;
}
