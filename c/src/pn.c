#include "rxnet/pn.h"

#include <stdlib.h>
#include <string.h>

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

static int rx_pn_transition_enabled(const rx_pn_net *net, const rx_pn_transition *transition) {
    size_t i;

    if (rx_pn_validate_arc_list(net, transition->consume, transition->consume_count) != 0 ||
        rx_pn_validate_arc_list(net, transition->produce, transition->produce_count) != 0) {
        return 0;
    }

    for (i = 0; i < transition->consume_count; ++i) {
        const rx_pn_arc *arc = &transition->consume[i];

        if (net->places[arc->place_id] < arc->weight) {
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

static void rx_pn_net_evaluate(void *node, rx_context *ctx) {
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
    }
}

static void rx_pn_net_commit(void *node, rx_context *ctx) {
    rx_pn_net *net = (rx_pn_net *)node;
    size_t t;

    for (t = 0; t < net->transition_count; ++t) {
        if (!net->fire_flags[t]) {
            continue;
        }

        rx_pn_apply_transition_delta(net, &net->transitions[t]);

        if (net->transitions[t].action != NULL) {
            rx_context_enqueue_deferred_action(ctx, net->transitions[t].action, net->user);
        }
    }

    memcpy(net->places, net->next_places, net->place_count * sizeof(int));
}

static const rx_node_vtable RX_PN_NET_VTABLE = {
    .evaluate = rx_pn_net_evaluate,
    .commit = rx_pn_net_commit,
};

int rx_pn_runtime_init(
    rx_pn_runtime *runtime,
    void *inputs,
    size_t inputs_size,
    size_t net_capacity
) {
    if (runtime == NULL) {
        return -1;
    }

    if (rx_context_init(&runtime->context, inputs, inputs_size) != 0) {
        return -1;
    }

    if (rx_runtime_init(&runtime->runtime, &runtime->context, net_capacity) != 0) {
        rx_context_free(&runtime->context);
        return -1;
    }

    return 0;
}

void rx_pn_runtime_free(rx_pn_runtime *runtime) {
    if (runtime == NULL) {
        return;
    }

    rx_runtime_free(&runtime->runtime);
    rx_context_free(&runtime->context);
}

int rx_pn_net_init(
    rx_pn_net *net,
    const char *name,
    const int *initial_places,
    size_t place_count,
    const rx_pn_transition *transitions,
    size_t transition_count,
    void *user
) {
    size_t t;

    if (net == NULL || (place_count > 0 && initial_places == NULL) ||
        (transition_count > 0 && transitions == NULL)) {
        return -1;
    }

    net->name = name;
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

int rx_pn_runtime_add_net(rx_pn_runtime *runtime, rx_pn_net *net) {
    rx_node node;

    if (runtime == NULL || net == NULL) {
        return -1;
    }

    node.vtable = &RX_PN_NET_VTABLE;
    node.self = net;
    return rx_runtime_add_node(&runtime->runtime, node);
}

int rx_pn_tick(rx_pn_runtime *runtime) {
    if (runtime == NULL) {
        return -1;
    }

    return rx_tick(&runtime->runtime);
}
