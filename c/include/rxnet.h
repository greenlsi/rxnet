#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rx_context rx_context;
typedef struct rx_machine rx_machine;
typedef struct rx_runtime rx_runtime;
typedef struct rx_transition rx_transition;

typedef int (*rx_guard_fn)(const rx_context *ctx, void *user);
typedef void (*rx_action_fn)(rx_context *ctx, void *user);

struct rx_context {
    size_t input_count;
    int *staged_inputs;
    int *inputs;
};

struct rx_transition {
    int from_state;
    int to_state;
    rx_guard_fn guard;
    rx_action_fn action;
};

struct rx_machine {
    const char *name;
    int state;
    int next_state;
    const rx_transition *transitions;
    size_t transition_count;
    void *user;
    rx_action_fn deferred_action;
};

struct rx_runtime {
    rx_context context;
    rx_machine **machines;
    size_t machine_count;
    size_t machine_capacity;
    struct {
        rx_action_fn fn;
        void *user;
    } *action_queue;
    size_t action_count;
    size_t action_capacity;
};

int rx_runtime_init(rx_runtime *runtime, size_t input_count, size_t machine_capacity);
void rx_runtime_free(rx_runtime *runtime);

void rx_context_stage_input(rx_context *ctx, size_t input_id, int value);
int rx_context_read_input(const rx_context *ctx, size_t input_id);

void rx_machine_init(
    rx_machine *machine,
    const char *name,
    int initial_state,
    const rx_transition *transitions,
    size_t transition_count,
    void *user
);

int rx_runtime_add_machine(rx_runtime *runtime, rx_machine *machine);
void rx_tick(rx_runtime *runtime);

#ifdef __cplusplus
}
#endif
