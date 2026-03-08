#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rx_pn_context rx_pn_context;
typedef struct rx_pn_net rx_pn_net;
typedef struct rx_pn_runtime rx_pn_runtime;
typedef struct rx_pn_transition rx_pn_transition;
typedef struct rx_pn_arc rx_pn_arc;

typedef int (*rx_pn_guard_fn)(const rx_pn_context *ctx, void *user);
typedef void (*rx_pn_action_fn)(rx_pn_context *ctx, void *user);

struct rx_pn_context {
    void *inputs;
    void *latched_inputs;
    size_t inputs_size;
};

struct rx_pn_arc {
    size_t place_id;
    int weight;
};

struct rx_pn_transition {
    const rx_pn_arc *consume;
    size_t consume_count;
    const rx_pn_arc *produce;
    size_t produce_count;
    rx_pn_guard_fn guard;
    rx_pn_action_fn action;
};

struct rx_pn_net {
    const char *name;
    int *places;
    int *next_places;
    size_t place_count;
    const rx_pn_transition *transitions;
    size_t transition_count;
    unsigned char *fire_flags;
    void *user;
};

struct rx_pn_runtime {
    rx_pn_context context;
    rx_pn_net **nets;
    size_t net_count;
    size_t net_capacity;
    struct {
        rx_pn_action_fn fn;
        void *user;
    } *action_queue;
    size_t action_count;
    size_t action_capacity;
};

int rx_pn_runtime_init(rx_pn_runtime *runtime, size_t inputs_size, size_t net_capacity);
void rx_pn_runtime_free(rx_pn_runtime *runtime);

int rx_pn_net_init(
    rx_pn_net *net,
    const char *name,
    const int *initial_places,
    size_t place_count,
    const rx_pn_transition *transitions,
    size_t transition_count,
    void *user
);
void rx_pn_net_free(rx_pn_net *net);

int rx_pn_runtime_add_net(rx_pn_runtime *runtime, rx_pn_net *net);
int rx_pn_tick(rx_pn_runtime *runtime);

#ifdef __cplusplus
}
#endif
