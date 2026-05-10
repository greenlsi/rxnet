// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rxnet/pn.h"
#include "rxnet/trace.h"

#include <stdlib.h>
#include <string.h>

static void rx_pn_noop(rx_pn_context *ctx, void *user) {
    (void)ctx;
    (void)user;
}

static int rx_pn_validate_arc_list(const rx_pn_net *net, const rx_pn_arc *arcs, size_t arc_count) {
    size_t i;

    if (arc_count > 0 && arcs == NULL) {
        return -1;
    }

    for (i = 0; i < arc_count; ++i) {
        if (arcs[i].place_id >= net->place_count || arcs[i].weight < 0) {
            return -1;
        }
    }

    return 0;
}

/*
 * Check whether a transition can fire given the current next_places.
 * Called during evaluate in declaration order; next_places is updated
 * immediately when each transition fires, so earlier transitions in
 * conflict consume tokens before later ones can see them (greedy
 * sequential / first-match semantics).
 */
static int rx_pn_transition_enabled(const rx_pn_net *net, const rx_pn_transition *transition) {
    size_t i;

    if (rx_pn_validate_arc_list(net, transition->consume, transition->consume_count) != 0 ||
        rx_pn_validate_arc_list(net, transition->produce, transition->produce_count) != 0) {
        return 0;
    }

    for (i = 0; i < transition->consume_count; ++i) {
        const rx_pn_arc *arc = &transition->consume[i];

        if (net->next_places[arc->place_id] < arc->weight) {
            return 0;
        }
    }

    return 1;
}

static void rx_pn_apply_transition_delta(rx_pn_net *net, const rx_pn_transition *transition) {
    size_t i;

    for (i = 0; i < transition->consume_count; ++i) {
        const rx_pn_arc *arc = &transition->consume[i];
        net->next_places[arc->place_id] -= arc->weight;
    }

    for (i = 0; i < transition->produce_count; ++i) {
        const rx_pn_arc *arc = &transition->produce[i];
        net->next_places[arc->place_id] += arc->weight;
    }
}

static void rx_pn_net_latch_inputs(rx_node *node, rx_context *ctx) {
    rx_pn_net *net = (rx_pn_net *)node;
    net->latch_inputs(ctx, net->user);
}

static void rx_pn_net_evaluate(rx_node *node, rx_context *ctx) {
    rx_pn_net *net = (rx_pn_net *)node;
    size_t t;

    memcpy(net->next_places, net->places, net->place_count * sizeof(int));
    memset(net->fire_flags, 0, net->transition_count * sizeof(unsigned char));

    for (t = 0; t < net->transition_count; ++t) {
        const rx_pn_transition *transition = &net->transitions[t];

        if (!rx_pn_transition_enabled(net, transition)) {
            continue;
        }

        if (transition->guard != NULL && !transition->guard(ctx, net->user)) {
            continue;
        }

        net->fire_flags[t] = 1;
        if (transition->action != NULL) {
            rx_context_enqueue_deferred_action(ctx, transition->action, net->user);
        }
        /* Apply delta immediately so subsequent transitions see the updated
         * token counts. This implements greedy sequential semantics: earlier
         * transitions in declaration order have priority over later ones. */
        rx_pn_apply_transition_delta(net, transition);
    }
}

static void rx_pn_net_commit(rx_node *node, rx_context *ctx) {
    rx_pn_net *net = (rx_pn_net *)node;
    size_t t;
    (void)ctx;

    /* Token deltas were already applied to next_places during evaluate.
     * Commit publishes the result after all active nodes have evaluated. */
    memcpy(net->places, net->next_places, net->place_count * sizeof(int));

    for (t = 0; t < net->transition_count; ++t) {
        if (!net->fire_flags[t]) {
            continue;
        }
        RX_TRACE_PN(node, (uint16_t)t);
    }
}

static void rx_pn_net_dump_outputs(rx_node *node, rx_context *ctx) {
    rx_pn_net *net = (rx_pn_net *)node;
    net->dump_outputs(ctx, net->user);
}

static const rx_node_vtable RX_PN_NET_VTABLE = {
    .latch_inputs = rx_pn_net_latch_inputs,
    .evaluate = rx_pn_net_evaluate,
    .commit = rx_pn_net_commit,
    .dump_outputs = rx_pn_net_dump_outputs,
};

static int
rx_pn_runtime_tick_fn(rx_runtime *base)
{
    return rx_pn_tick((rx_pn_runtime *)base);
}

int rx_pn_runtime_init(
    rx_pn_runtime *runtime,
    size_t net_capacity
) {
    if (runtime == NULL) {
        return -1;
    }

    if (rx_context_init(&runtime->context) != 0) {
        return -1;
    }

    if (rx_runtime_init(&runtime->runtime, &runtime->context, net_capacity) != 0) {
        rx_context_free(&runtime->context);
        return -1;
    }

    runtime->runtime.tick = rx_pn_runtime_tick_fn;
    return 0;
}

rx_pn_runtime *rx_pn_runtime_create(
    size_t net_capacity
) {
    rx_pn_runtime *runtime = (rx_pn_runtime *)malloc(sizeof(*runtime));

    if (runtime == NULL) {
        return NULL;
    }

    if (rx_pn_runtime_init(runtime, net_capacity) != 0) {
        free(runtime);
        return NULL;
    }

    return runtime;
}

void rx_pn_runtime_free(rx_pn_runtime *runtime) {
    if (runtime == NULL) {
        return;
    }

    rx_runtime_free(&runtime->runtime);
    rx_context_free(&runtime->context);
}

void rx_pn_runtime_destroy(rx_pn_runtime *runtime) {
    if (runtime == NULL) {
        return;
    }

    rx_pn_runtime_free(runtime);
    free(runtime);
}

int rx_pn_net_init(
    rx_pn_net *net,
    const char *name,
    const int *initial_places,
    size_t place_count,
    const rx_pn_transition *transitions,
    size_t transition_count,
    void *user,
    rx_pn_node_phase_fn latch_inputs,
    rx_pn_node_phase_fn dump_outputs
) {
    size_t t;

    if (net == NULL || (place_count > 0 && initial_places == NULL) ||
        (transition_count > 0 && transitions == NULL)) {
        return -1;
    }

    net->name = name;
    net->node.vtable = &RX_PN_NET_VTABLE;
    net->node.latch_inputs_cb = NULL;
    net->node.dump_outputs_cb = NULL;
    RX_NODE_TRACE_INIT(&net->node);
    net->latch_inputs = latch_inputs != NULL ? latch_inputs : rx_pn_noop;
    net->dump_outputs = dump_outputs != NULL ? dump_outputs : rx_pn_noop;
    net->place_count = place_count;
    net->transitions = transitions;
    net->transition_count = transition_count;
    net->user = user;
    net->places = calloc(place_count, sizeof(int));
    net->next_places = calloc(place_count, sizeof(int));
    net->fire_flags = calloc(transition_count, sizeof(unsigned char));

    if ((place_count > 0 && (net->places == NULL || net->next_places == NULL)) ||
        (transition_count > 0 && net->fire_flags == NULL)) {
        rx_pn_net_free(net);
        return -1;
    }

    if (place_count > 0) {
        memcpy(net->places, initial_places, place_count * sizeof(int));
        memcpy(net->next_places, initial_places, place_count * sizeof(int));
    }

    for (t = 0; t < transition_count; ++t) {
        if (rx_pn_validate_arc_list(net, transitions[t].consume, transitions[t].consume_count) != 0 ||
            rx_pn_validate_arc_list(net, transitions[t].produce, transitions[t].produce_count) != 0) {
            rx_pn_net_free(net);
            return -1;
        }
    }

    return 0;
}

rx_pn_net *rx_pn_net_create(
    const char *name,
    const int *initial_places,
    size_t place_count,
    const rx_pn_transition *transitions,
    size_t transition_count,
    void *user,
    rx_pn_node_phase_fn latch_inputs,
    rx_pn_node_phase_fn dump_outputs
) {
    rx_pn_net *net = (rx_pn_net *)malloc(sizeof(*net));

    if (net == NULL) {
        return NULL;
    }

    if (rx_pn_net_init(net, name, initial_places, place_count, transitions, transition_count,
                       user, latch_inputs, dump_outputs) != 0) {
        free(net);
        return NULL;
    }

    return net;
}

void rx_pn_net_free(rx_pn_net *net) {
    if (net == NULL) {
        return;
    }

    free(net->places);
    free(net->next_places);
    free(net->fire_flags);

    net->name = NULL;
    net->places = NULL;
    net->next_places = NULL;
    net->place_count = 0;
    net->transitions = NULL;
    net->transition_count = 0;
    net->fire_flags = NULL;
    net->user = NULL;
}

void rx_pn_net_destroy(rx_pn_net *net) {
    if (net == NULL) {
        return;
    }

    rx_pn_net_free(net);
    free(net);
}

int rx_pn_runtime_add_net(rx_pn_runtime *runtime, rx_pn_net *net,
                          long period_us, long deadline_us) {
    if (runtime == NULL || net == NULL) {
        return -1;
    }

    net->node.vtable = &RX_PN_NET_VTABLE;
    return rx_runtime_add_node(&runtime->runtime, &net->node, period_us, deadline_us);
}

int rx_pn_tick(rx_pn_runtime *runtime) {
    if (runtime == NULL) {
        return -1;
    }

    return rx_tick(&runtime->runtime);
}
