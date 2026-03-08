#include "rxnet/fsm.h"

#include <stdlib.h>
#include <string.h>

static int rx_fsm_context_latch(rx_fsm_context *ctx) {
    if (ctx == NULL) {
        return -1;
    }

    if (ctx->inputs_size == 0) {
        return 0;
    }

    memcpy(ctx->latched_inputs, ctx->inputs, ctx->inputs_size);
    return 0;
}

static int rx_fsm_runtime_queue_action(rx_fsm_runtime *runtime, rx_fsm_action_fn fn, void *user) {
    size_t new_capacity;
    void *new_queue;

    if (runtime->action_count < runtime->action_capacity) {
        runtime->action_queue[runtime->action_count].fn = fn;
        runtime->action_queue[runtime->action_count].user = user;
        runtime->action_count += 1;
        return 0;
    }

    new_capacity = runtime->action_capacity == 0 ? 4 : runtime->action_capacity * 2;
    new_queue = realloc(runtime->action_queue, new_capacity * sizeof(*runtime->action_queue));
    if (new_queue == NULL) {
        return -1;
    }

    runtime->action_queue = new_queue;
    runtime->action_capacity = new_capacity;
    runtime->action_queue[runtime->action_count].fn = fn;
    runtime->action_queue[runtime->action_count].user = user;
    runtime->action_count += 1;
    return 0;
}

int rx_fsm_runtime_init(rx_fsm_runtime *runtime, size_t inputs_size, size_t machine_capacity) {
    if (runtime == NULL) {
        return -1;
    }

    runtime->context.inputs_size = inputs_size;
    runtime->context.inputs = calloc(1, inputs_size);
    runtime->context.latched_inputs = calloc(1, inputs_size);
    runtime->machines = calloc(machine_capacity, sizeof(*runtime->machines));
    runtime->machine_count = 0;
    runtime->machine_capacity = machine_capacity;
    runtime->action_capacity = machine_capacity > 0 ? machine_capacity : 4;
    runtime->action_queue = calloc(runtime->action_capacity, sizeof(*runtime->action_queue));
    runtime->action_count = 0;

    if ((inputs_size > 0 && (runtime->context.inputs == NULL || runtime->context.latched_inputs == NULL)) ||
        (machine_capacity > 0 && runtime->machines == NULL) || runtime->action_queue == NULL) {
        rx_fsm_runtime_free(runtime);
        return -1;
    }

    return 0;
}

void rx_fsm_runtime_free(rx_fsm_runtime *runtime) {
    if (runtime == NULL) {
        return;
    }

    free(runtime->context.inputs);
    free(runtime->context.latched_inputs);
    free(runtime->machines);
    free(runtime->action_queue);

    runtime->context.inputs = NULL;
    runtime->context.latched_inputs = NULL;
    runtime->context.inputs_size = 0;
    runtime->machines = NULL;
    runtime->machine_count = 0;
    runtime->machine_capacity = 0;
    runtime->action_queue = NULL;
    runtime->action_count = 0;
    runtime->action_capacity = 0;
}

void rx_fsm_machine_init(
    rx_fsm_machine *machine,
    const char *name,
    int initial_state,
    const rx_fsm_transition *transitions,
    size_t transition_count,
    void *user
) {
    if (machine == NULL) {
        return;
    }

    machine->name = name;
    machine->state = initial_state;
    machine->next_state = initial_state;
    machine->transitions = transitions;
    machine->transition_count = transition_count;
    machine->user = user;
    machine->deferred_action = NULL;
}

int rx_fsm_runtime_add_machine(rx_fsm_runtime *runtime, rx_fsm_machine *machine) {
    if (runtime == NULL || machine == NULL) {
        return -1;
    }

    if (runtime->machine_count >= runtime->machine_capacity) {
        return -1;
    }

    runtime->machines[runtime->machine_count++] = machine;
    return 0;
}

int rx_fsm_tick(rx_fsm_runtime *runtime) {
    size_t i;

    if (runtime == NULL) {
        return -1;
    }

    if (rx_fsm_context_latch(&runtime->context) != 0) {
        return -1;
    }

    runtime->action_count = 0;

    for (i = 0; i < runtime->machine_count; ++i) {
        rx_fsm_machine *machine = runtime->machines[i];
        size_t j;

        if (machine == NULL) {
            return -1;
        }

        machine->next_state = machine->state;
        machine->deferred_action = NULL;

        for (j = 0; j < machine->transition_count; ++j) {
            const rx_fsm_transition *t = &machine->transitions[j];

            if (t->from_state != machine->state) {
                continue;
            }

            if (t->guard == NULL || t->guard(&runtime->context, machine->user)) {
                machine->next_state = t->to_state;
                machine->deferred_action = t->action;
                break;
            }
        }
    }

    for (i = 0; i < runtime->machine_count; ++i) {
        rx_fsm_machine *machine = runtime->machines[i];
        machine->state = machine->next_state;

        if (machine->deferred_action != NULL &&
            rx_fsm_runtime_queue_action(runtime, machine->deferred_action, machine->user) != 0) {
            return -1;
        }
    }

    for (i = 0; i < runtime->action_count; ++i) {
        runtime->action_queue[i].fn(&runtime->context, runtime->action_queue[i].user);
    }

    return 0;
}
