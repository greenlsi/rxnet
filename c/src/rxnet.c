#include "rxnet.h"

#include <stdlib.h>
#include <string.h>

static void rx_context_latch(rx_context *ctx) {
    memcpy(ctx->inputs, ctx->staged_inputs, ctx->input_count * sizeof(int));
}

int rx_runtime_init(rx_runtime *runtime, size_t input_count, size_t machine_capacity) {
    if (runtime == NULL) {
        return -1;
    }

    runtime->context.input_count = input_count;
    runtime->context.staged_inputs = calloc(input_count, sizeof(int));
    runtime->context.inputs = calloc(input_count, sizeof(int));
    runtime->machines = calloc(machine_capacity, sizeof(*runtime->machines));
    runtime->action_queue = calloc(machine_capacity, sizeof(*runtime->action_queue));
    runtime->machine_count = 0;
    runtime->machine_capacity = machine_capacity;
    runtime->action_count = 0;
    runtime->action_capacity = machine_capacity;

    if ((input_count > 0 && (runtime->context.staged_inputs == NULL || runtime->context.inputs == NULL)) ||
        (machine_capacity > 0 && (runtime->machines == NULL || runtime->action_queue == NULL))) {
        rx_runtime_free(runtime);
        return -1;
    }

    return 0;
}

void rx_runtime_free(rx_runtime *runtime) {
    if (runtime == NULL) {
        return;
    }

    free(runtime->context.staged_inputs);
    free(runtime->context.inputs);
    free(runtime->machines);
    free(runtime->action_queue);

    runtime->context.input_count = 0;
    runtime->context.staged_inputs = NULL;
    runtime->context.inputs = NULL;
    runtime->machines = NULL;
    runtime->machine_count = 0;
    runtime->machine_capacity = 0;
    runtime->action_queue = NULL;
    runtime->action_count = 0;
    runtime->action_capacity = 0;
}

void rx_context_stage_input(rx_context *ctx, size_t input_id, int value) {
    ctx->staged_inputs[input_id] = value;
}

int rx_context_read_input(const rx_context *ctx, size_t input_id) {
    return ctx->inputs[input_id];
}

void rx_machine_init(
    rx_machine *machine,
    const char *name,
    int initial_state,
    const rx_transition *transitions,
    size_t transition_count,
    void *user
) {
    machine->name = name;
    machine->state = initial_state;
    machine->next_state = initial_state;
    machine->transitions = transitions;
    machine->transition_count = transition_count;
    machine->user = user;
    machine->deferred_action = NULL;
}

int rx_runtime_add_machine(rx_runtime *runtime, rx_machine *machine) {
    if (runtime->machine_count >= runtime->machine_capacity) {
        return -1;
    }

    runtime->machines[runtime->machine_count++] = machine;
    return 0;
}

void rx_tick(rx_runtime *runtime) {
    size_t i;

    rx_context_latch(&runtime->context);
    runtime->action_count = 0;

    for (i = 0; i < runtime->machine_count; ++i) {
        rx_machine *machine = runtime->machines[i];
        size_t j;

        machine->next_state = machine->state;
        machine->deferred_action = NULL;

        for (j = 0; j < machine->transition_count; ++j) {
            const rx_transition *t = &machine->transitions[j];

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
        rx_machine *machine = runtime->machines[i];
        machine->state = machine->next_state;

        if (machine->deferred_action != NULL) {
            runtime->action_queue[runtime->action_count].fn = machine->deferred_action;
            runtime->action_queue[runtime->action_count].user = machine->user;
            runtime->action_count += 1;
        }
    }

    for (i = 0; i < runtime->action_count; ++i) {
        runtime->action_queue[i].fn(&runtime->context, runtime->action_queue[i].user);
    }
}
