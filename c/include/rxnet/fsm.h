#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rx_fsm_context rx_fsm_context;
typedef struct rx_fsm_machine rx_fsm_machine;
typedef struct rx_fsm_runtime rx_fsm_runtime;
typedef struct rx_fsm_transition rx_fsm_transition;

typedef int (*rx_fsm_guard_fn)(const rx_fsm_context *ctx, void *user);
typedef void (*rx_fsm_action_fn)(rx_fsm_context *ctx, void *user);

struct rx_fsm_context {
    void *inputs;
    void *latched_inputs;
    size_t inputs_size;
};

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
    rx_fsm_action_fn deferred_action;
};

struct rx_fsm_runtime {
    rx_fsm_context context;
    rx_fsm_machine **machines;
    size_t machine_count;
    size_t machine_capacity;
    struct {
        rx_fsm_action_fn fn;
        void *user;
    } *action_queue;
    size_t action_count;
    size_t action_capacity;
};

int rx_fsm_runtime_init(rx_fsm_runtime *runtime, size_t inputs_size, size_t machine_capacity);
void rx_fsm_runtime_free(rx_fsm_runtime *runtime);

void rx_fsm_machine_init(
    rx_fsm_machine *machine,
    const char *name,
    int initial_state,
    const rx_fsm_transition *transitions,
    size_t transition_count,
    void *user
);

int rx_fsm_runtime_add_machine(rx_fsm_runtime *runtime, rx_fsm_machine *machine);
int rx_fsm_tick(rx_fsm_runtime *runtime);

#ifdef __cplusplus
}
#endif
