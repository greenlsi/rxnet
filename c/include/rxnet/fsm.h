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
typedef void (*rx_fsm_inputs_projector_fn)(const rx_fsm_context *ctx, void *user);

struct rx_fsm_transition {
    int from_state;
    int to_state;
    rx_fsm_guard_fn guard;
    rx_fsm_action_fn action;
};

struct rx_fsm_machine {
    const char *name;
    int state;
    int next_state;
    const rx_fsm_transition *transitions;
    size_t transition_count;
    void *user;
    rx_fsm_action_fn proposed_action;
    rx_fsm_inputs_projector_fn inputs_projector;
};

struct rx_fsm_runtime {
    rx_context context;
    rx_runtime runtime;
};

int rx_fsm_runtime_init(
    rx_fsm_runtime *runtime,
    void *inputs,
    size_t inputs_size,
    size_t machine_capacity
);
void rx_fsm_runtime_free(rx_fsm_runtime *runtime);

void rx_fsm_machine_init(
    rx_fsm_machine *machine,
    const char *name,
    int initial_state,
    const rx_fsm_transition *transitions,
    size_t transition_count,
    void *user
);
void rx_fsm_machine_set_inputs_projector(rx_fsm_machine *machine, rx_fsm_inputs_projector_fn projector);

int rx_fsm_runtime_add_machine(rx_fsm_runtime *runtime, rx_fsm_machine *machine);
int rx_fsm_tick(rx_fsm_runtime *runtime);

#ifdef __cplusplus
}
#endif
