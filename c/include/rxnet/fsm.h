#pragma once

#include <stddef.h>

#include "rxnet/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef rx_context rx_fsm_context;
typedef struct rx_fsm_machine rx_fsm_machine;
typedef struct rx_fsm_runtime rx_fsm_runtime;
typedef struct rx_fsm_transition rx_fsm_transition;

typedef int (*rx_fsm_guard_fn)(const rx_fsm_context *ctx, void *user);
typedef void (*rx_fsm_action_fn)(rx_fsm_context *ctx, void *user);
typedef void (*rx_fsm_node_phase_fn)(rx_fsm_context *ctx, void *user);

struct rx_fsm_transition {
    int from_state;
    int to_state;
    rx_fsm_guard_fn guard;
    rx_fsm_action_fn action;
};

struct rx_fsm_machine {
    rx_node node;
    const char *name;
    int state;
    const rx_fsm_transition *transitions;
    size_t transition_count;
    void *user;
    int next_state;
    rx_fsm_action_fn proposed_action;
    rx_fsm_node_phase_fn latch_inputs;
    rx_fsm_node_phase_fn dump_outputs;
};

struct rx_fsm_runtime {
    rx_runtime runtime;
    rx_context context;
};

int rx_fsm_runtime_init(
    rx_fsm_runtime *runtime,
    size_t machine_capacity
);
rx_fsm_runtime *rx_fsm_runtime_create(
    size_t machine_capacity
);
void rx_fsm_runtime_free(rx_fsm_runtime *runtime);
void rx_fsm_runtime_destroy(rx_fsm_runtime *runtime);

void rx_fsm_machine_init(
    rx_fsm_machine *machine,
    const char *name,
    int initial_state,
    const rx_fsm_transition *transitions,
    size_t transition_count,
    void *user,
    rx_fsm_node_phase_fn latch_inputs,
    rx_fsm_node_phase_fn dump_outputs
);
rx_fsm_machine *rx_fsm_machine_create(
    const char *name,
    int initial_state,
    const rx_fsm_transition *transitions,
    size_t transition_count,
    void *user,
    rx_fsm_node_phase_fn latch_inputs,
    rx_fsm_node_phase_fn dump_outputs
);
void rx_fsm_machine_destroy(rx_fsm_machine *machine);

/*
 * Register a machine in the runtime with its scheduling parameters.
 *
 * period_us   Activation period (µs).  0 = async (runs every base tick).
 * deadline_us Relative deadline (µs).  0 = same as period_us.
 */
int rx_fsm_runtime_add_machine(rx_fsm_runtime *runtime, rx_fsm_machine *machine,
                               long period_us, long deadline_us);
int rx_fsm_tick(rx_fsm_runtime *runtime);

#ifdef __cplusplus
}
#endif
