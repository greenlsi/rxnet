#include "rxnet/fsm.h"

static void rx_fsm_machine_evaluate(void *node, rx_context *ctx) {
    rx_fsm_machine *machine = (rx_fsm_machine *)node;
    size_t j;

    machine->next_state = machine->state;
    machine->proposed_action = NULL;
    if (machine->inputs_projector != NULL) {
        machine->inputs_projector(ctx, machine->user);
    }

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

static void rx_fsm_machine_commit(void *node, rx_context *ctx) {
    rx_fsm_machine *machine = (rx_fsm_machine *)node;

    machine->state = machine->next_state;

    if (machine->proposed_action != NULL) {
        rx_context_enqueue_deferred_action(ctx, machine->proposed_action, machine->user);
    }
}

static const rx_node_vtable RX_FSM_MACHINE_VTABLE = {
    .evaluate = rx_fsm_machine_evaluate,
    .commit = rx_fsm_machine_commit,
};

int rx_fsm_runtime_init(
    rx_fsm_runtime *runtime,
    void *inputs,
    size_t inputs_size,
    size_t machine_capacity
) {
    if (runtime == NULL) {
        return -1;
    }

    if (rx_context_init(&runtime->context, inputs, inputs_size) != 0) {
        return -1;
    }

    if (rx_runtime_init(&runtime->runtime, &runtime->context, machine_capacity) != 0) {
        rx_context_free(&runtime->context);
        return -1;
    }

    return 0;
}

void rx_fsm_runtime_free(rx_fsm_runtime *runtime) {
    if (runtime == NULL) {
        return;
    }

    rx_runtime_free(&runtime->runtime);
    rx_context_free(&runtime->context);
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
    machine->proposed_action = NULL;
    machine->inputs_projector = NULL;
}

void rx_fsm_machine_set_inputs_projector(rx_fsm_machine *machine, rx_fsm_inputs_projector_fn projector) {
    if (machine == NULL) {
        return;
    }

    machine->inputs_projector = projector;
}

int rx_fsm_runtime_add_machine(rx_fsm_runtime *runtime, rx_fsm_machine *machine) {
    rx_node node;

    if (runtime == NULL || machine == NULL) {
        return -1;
    }

    node.vtable = &RX_FSM_MACHINE_VTABLE;
    node.self = machine;
    return rx_runtime_add_node(&runtime->runtime, node);
}

int rx_fsm_tick(rx_fsm_runtime *runtime) {
    if (runtime == NULL) {
        return -1;
    }

    return rx_tick(&runtime->runtime);
}
