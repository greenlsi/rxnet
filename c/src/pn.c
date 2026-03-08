#include "rxnet/pn.h"

#include <stdlib.h>
#include <string.h>

static int rx_pn_context_latch(rx_pn_context *ctx) {
    if (ctx == NULL) {
        return -1;
    }

    if (ctx->inputs_size == 0) {
        return 0;
    }

    memcpy(ctx->latched_inputs, ctx->inputs, ctx->inputs_size);
    return 0;
}

static int rx_pn_runtime_queue_action(rx_pn_runtime *runtime, rx_pn_action_fn fn, void *user) {
    size_t new_capacity;
    void *new_queue;

    if (runtime->action_count < runtime->action_capacity) {
        runtime->action_queue[runtime->action_count].fn = fn;
        runtime->action_queue[runtime->action_count].user = user;
        runtime->action_count += 1;
        return 0;
    }

    new_capacity = runtime->action_capacity == 0 ? 8 : runtime->action_capacity * 2;
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
        return -1;
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

int rx_pn_runtime_init(rx_pn_runtime *runtime, size_t inputs_size, size_t net_capacity) {
    if (runtime == NULL) {
        return -1;
    }

    runtime->context.inputs_size = inputs_size;
    runtime->context.inputs = calloc(1, inputs_size);
    runtime->context.latched_inputs = calloc(1, inputs_size);
    runtime->nets = calloc(net_capacity, sizeof(*runtime->nets));
    runtime->net_count = 0;
    runtime->net_capacity = net_capacity;
    runtime->action_capacity = net_capacity > 0 ? net_capacity * 2 : 8;
    runtime->action_queue = calloc(runtime->action_capacity, sizeof(*runtime->action_queue));
    runtime->action_count = 0;

    if ((inputs_size > 0 && (runtime->context.inputs == NULL || runtime->context.latched_inputs == NULL)) ||
        (net_capacity > 0 && runtime->nets == NULL) || runtime->action_queue == NULL) {
        rx_pn_runtime_free(runtime);
        return -1;
    }

    return 0;
}

void rx_pn_runtime_free(rx_pn_runtime *runtime) {
    if (runtime == NULL) {
        return;
    }

    free(runtime->context.inputs);
    free(runtime->context.latched_inputs);
    free(runtime->nets);
    free(runtime->action_queue);

    runtime->context.inputs = NULL;
    runtime->context.latched_inputs = NULL;
    runtime->context.inputs_size = 0;
    runtime->nets = NULL;
    runtime->net_count = 0;
    runtime->net_capacity = 0;
    runtime->action_queue = NULL;
    runtime->action_count = 0;
    runtime->action_capacity = 0;
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
    if (runtime == NULL || net == NULL) {
        return -1;
    }

    if (runtime->net_count >= runtime->net_capacity) {
        return -1;
    }

    runtime->nets[runtime->net_count++] = net;
    return 0;
}

int rx_pn_tick(rx_pn_runtime *runtime) {
    size_t i;

    if (runtime == NULL) {
        return -1;
    }

    if (rx_pn_context_latch(&runtime->context) != 0) {
        return -1;
    }

    runtime->action_count = 0;

    for (i = 0; i < runtime->net_count; ++i) {
        rx_pn_net *net = runtime->nets[i];
        size_t t;

        if (net == NULL) {
            return -1;
        }

        memset(net->fire_flags, 0, net->transition_count * sizeof(unsigned char));
        memcpy(net->next_places, net->places, net->place_count * sizeof(int));

        for (t = 0; t < net->transition_count; ++t) {
            const rx_pn_transition *transition = &net->transitions[t];
            int enabled;

            enabled = rx_pn_transition_enabled(net, transition);
            if (enabled < 0) {
                return -1;
            }
            if (enabled == 0) {
                continue;
            }

            if (transition->guard != NULL && !transition->guard(&runtime->context, net->user)) {
                continue;
            }

            net->fire_flags[t] = 1;
        }
    }

    for (i = 0; i < runtime->net_count; ++i) {
        rx_pn_net *net = runtime->nets[i];
        size_t t;

        for (t = 0; t < net->transition_count; ++t) {
            if (!net->fire_flags[t]) {
                continue;
            }

            rx_pn_apply_transition_delta(net, &net->transitions[t]);

            if (net->transitions[t].action != NULL &&
                rx_pn_runtime_queue_action(runtime, net->transitions[t].action, net->user) != 0) {
                return -1;
            }
        }

        memcpy(net->places, net->next_places, net->place_count * sizeof(int));
    }

    for (i = 0; i < runtime->action_count; ++i) {
        runtime->action_queue[i].fn(&runtime->context, runtime->action_queue[i].user);
    }

    return 0;
}
