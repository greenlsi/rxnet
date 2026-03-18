#pragma once

#include <stddef.h>

#include "rxnet/config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rx_context rx_context;
typedef struct rx_runtime rx_runtime;
typedef struct rx_node rx_node;
typedef struct rx_node_vtable rx_node_vtable;
typedef struct rx_deferred_action_entry rx_deferred_action_entry;

typedef void (*rx_deferred_action_fn)(rx_context *ctx, void *user);
typedef void (*rx_node_phase_fn)(rx_node *node, rx_context *ctx);

struct rx_deferred_action_entry {
    rx_deferred_action_fn fn;
    void *user;
};

struct rx_context {
    rx_deferred_action_entry deferred_actions_storage[RXNET_MAX_DEFERRED_ACTIONS];
    rx_deferred_action_entry *deferred_actions;
    size_t deferred_count;
    size_t deferred_capacity;
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
};

struct rx_runtime {
    rx_context *ctx;
    rx_node *nodes_storage[RXNET_MAX_RUNTIME_NODES];
    rx_node **nodes;
    size_t node_count;
    size_t node_capacity;
};

int rx_context_init(rx_context *ctx);
rx_context *rx_context_create(void);
void rx_context_free(rx_context *ctx);
void rx_context_destroy(rx_context *ctx);
int rx_context_enqueue_deferred_action(rx_context *ctx, rx_deferred_action_fn fn, void *user);
void rx_context_run_deferred_actions(rx_context *ctx);

int rx_runtime_init(rx_runtime *rt, rx_context *ctx, size_t node_capacity);
rx_runtime *rx_runtime_create(rx_context *ctx, size_t node_capacity);
void rx_runtime_free(rx_runtime *rt);
void rx_runtime_destroy(rx_runtime *rt);
int rx_runtime_add_node(rx_runtime *rt, rx_node *node);
void rx_node_set_latch_inputs_callback(rx_node *node, rx_node_phase_fn cb);
void rx_node_set_dump_outputs_callback(rx_node *node, rx_node_phase_fn cb);
int rx_tick(rx_runtime *rt);

#ifdef __cplusplus
}
#endif
