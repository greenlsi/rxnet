// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#include "rxnet/fsm.h"
#include "rxnet/trace.h"

#include <stdlib.h>

static void rx_fsm_machine_latch_inputs(rx_node *node, rx_context *ctx) {
    rx_fsm_machine *machine = (rx_fsm_machine *)node;
    machine->latch_inputs(ctx, machine->user);
}

static void rx_fsm_machine_evaluate(rx_node *node, rx_context *ctx) {
    rx_fsm_machine *machine = (rx_fsm_machine *)node;
    size_t j;

    machine->next_state = machine->state;
    machine->proposed_action = NULL;

    for (j = 0; j < machine->transition_count; ++j) {
        const rx_fsm_transition *t = &machine->transitions[j];

        if (t->from_state != machine->state) {
            continue;
        }

        if (t->guard == NULL || t->guard(ctx, machine->user)) {
            machine->next_state = t->to_state;
            machine->proposed_action = t->action;
            break;
        }
    }
}

static void rx_fsm_machine_commit(rx_node *node, rx_context *ctx) {
    rx_fsm_machine *machine = (rx_fsm_machine *)node;
    int prev = machine->state;

    machine->state = machine->next_state;
    RX_TRACE_FSM(node, prev, machine->state);
    (void)prev;   /* suppress -Wunused-variable when tracing is disabled */

    if (machine->proposed_action != NULL) {
        rx_context_enqueue_deferred_action(ctx, machine->proposed_action, machine->user);
    }
}

static void rx_fsm_machine_dump_outputs(rx_node *node, rx_context *ctx) {
    rx_fsm_machine *machine = (rx_fsm_machine *)node;
    machine->dump_outputs(ctx, machine->user);
}

static const rx_node_vtable RX_FSM_MACHINE_VTABLE = {
    .latch_inputs = rx_fsm_machine_latch_inputs,
    .evaluate = rx_fsm_machine_evaluate,
    .commit = rx_fsm_machine_commit,
    .dump_outputs = rx_fsm_machine_dump_outputs,
};

static int
rx_fsm_runtime_tick_fn(rx_runtime *base)
{
    return rx_fsm_tick((rx_fsm_runtime *)base);
}

int rx_fsm_runtime_init(
    rx_fsm_runtime *runtime,
    size_t machine_capacity
) {
    if (runtime == NULL) {
        return -1;
    }

    if (rx_context_init(&runtime->context) != 0) {
        return -1;
    }

    if (rx_runtime_init(&runtime->runtime, &runtime->context, machine_capacity) != 0) {
        rx_context_free(&runtime->context);
        return -1;
    }

    runtime->runtime.tick = rx_fsm_runtime_tick_fn;
    return 0;
}

rx_fsm_runtime *rx_fsm_runtime_create(
    size_t machine_capacity
) {
    rx_fsm_runtime *runtime = (rx_fsm_runtime *)malloc(sizeof(*runtime));

    if (runtime == NULL) {
        return NULL;
    }

    if (rx_fsm_runtime_init(runtime, machine_capacity) != 0) {
        free(runtime);
        return NULL;
    }

    return runtime;
}

void rx_fsm_runtime_free(rx_fsm_runtime *runtime) {
    if (runtime == NULL) {
        return;
    }

    rx_runtime_free(&runtime->runtime);
    rx_context_free(&runtime->context);
}

void rx_fsm_runtime_destroy(rx_fsm_runtime *runtime) {
    if (runtime == NULL) {
        return;
    }

    rx_fsm_runtime_free(runtime);
    free(runtime);
}

void rx_fsm_machine_init(
    rx_fsm_machine *machine,
    const char *name,
    int initial_state,
    const rx_fsm_transition *transitions,
    size_t transition_count,
    void *user,
    rx_fsm_node_phase_fn latch_inputs,
    rx_fsm_node_phase_fn dump_outputs
) {
    if (machine == NULL) {
        return;
    }

    machine->node.vtable = &RX_FSM_MACHINE_VTABLE;
    machine->node.latch_inputs_cb = NULL;
    machine->node.dump_outputs_cb = NULL;
    machine->name = name;
    machine->state = initial_state;
    machine->next_state = initial_state;
    machine->transitions = transitions;
    machine->transition_count = transition_count;
    machine->user = user;
    machine->proposed_action = NULL;
    machine->latch_inputs = latch_inputs;
    machine->dump_outputs = dump_outputs;
}

rx_fsm_machine *rx_fsm_machine_create(
    const char *name,
    int initial_state,
    const rx_fsm_transition *transitions,
    size_t transition_count,
    void *user,
    rx_fsm_node_phase_fn latch_inputs,
    rx_fsm_node_phase_fn dump_outputs
) {
    rx_fsm_machine *machine = (rx_fsm_machine *)malloc(sizeof(*machine));

    if (machine == NULL) {
        return NULL;
    }

    rx_fsm_machine_init(machine, name, initial_state, transitions, transition_count, user, latch_inputs, dump_outputs);
    return machine;
}

void rx_fsm_machine_destroy(rx_fsm_machine *machine) {
    if (machine == NULL) {
        return;
    }

    free(machine);
}

int rx_fsm_runtime_add_machine(rx_fsm_runtime *runtime, rx_fsm_machine *machine,
                               long period_us, long deadline_us) {
    if (runtime == NULL || machine == NULL) {
        return -1;
    }
    if (machine->latch_inputs == NULL || machine->dump_outputs == NULL) {
        return -1;
    }

    machine->node.vtable = &RX_FSM_MACHINE_VTABLE;
    return rx_runtime_add_node(&runtime->runtime, &machine->node, period_us, deadline_us);
}

int rx_fsm_tick(rx_fsm_runtime *runtime) {
    if (runtime == NULL) {
        return -1;
    }

    return rx_tick(&runtime->runtime);
}
