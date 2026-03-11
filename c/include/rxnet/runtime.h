#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rx_context rx_context;
typedef struct rx_runtime rx_runtime;
typedef struct rx_node rx_node;
typedef struct rx_node_vtable rx_node_vtable;

typedef void (*rx_deferred_action_fn)(rx_context *ctx, void *user);

struct rx_context {
    void *inputs;
    void *latched_inputs;
    size_t inputs_size;
    struct {
        rx_deferred_action_fn fn;
        void *user;
    } *deferred_actions;
    size_t deferred_count;
    size_t deferred_capacity;
};

struct rx_node_vtable {
    void (*evaluate)(void *node, rx_context *ctx);
    void (*commit)(void *node, rx_context *ctx);
};

struct rx_node {
    const rx_node_vtable *vtable;
    void *self;
};

struct rx_runtime {
    rx_context *ctx;
    rx_node *nodes;
    size_t node_count;
    size_t node_capacity;
};

int rx_context_init(rx_context *ctx, void *inputs, size_t inputs_size);
void rx_context_free(rx_context *ctx);
void rx_context_latch_inputs(rx_context *ctx);
int rx_context_enqueue_deferred_action(rx_context *ctx, rx_deferred_action_fn fn, void *user);
void rx_context_run_deferred_actions(rx_context *ctx);

int rx_runtime_init(rx_runtime *rt, rx_context *ctx, size_t node_capacity);
void rx_runtime_free(rx_runtime *rt);
int rx_runtime_add_node(rx_runtime *rt, rx_node node);
int rx_tick(rx_runtime *rt);

#ifdef __cplusplus
}
#endif
