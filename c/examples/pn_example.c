#include "rxnet/pn.h"

#include <stdio.h>

typedef struct {
    int add;
    int remove;
} app_inputs;

enum {
    P_IDLE = 0,
    P_BUSY = 1,
};

typedef struct {
    int fired_count;
    app_inputs *inputs;
} app_data;

static int want_add(const rx_pn_context *ctx, void *user) {
    const app_inputs *in = ((const app_data *)user)->inputs;
    return in->add != 0;
}

static int want_remove(const rx_pn_context *ctx, void *user) {
    const app_inputs *in = ((const app_data *)user)->inputs;
    return in->remove != 0;
}

static void on_fire(rx_pn_context *ctx, void *user) {
    ((app_data *)user)->fired_count += 1;
}

int main(void) {
    app_inputs inputs = {0};
    app_data app = {0, &inputs};
    rx_pn_runtime *runtime;
    rx_pn_net *net;

    int initial_places[] = {1, 0};

    rx_pn_arc t0_consume[] = {{P_IDLE, 1}};
    rx_pn_arc t0_produce[] = {{P_BUSY, 1}};

    rx_pn_arc t1_consume[] = {{P_BUSY, 1}};
    rx_pn_arc t1_produce[] = {{P_IDLE, 1}};

    rx_pn_transition transitions[] = {
        {t0_consume, 1, t0_produce, 1, want_add, on_fire},
        {t1_consume, 1, t1_produce, 1, want_remove, on_fire},
    };

    runtime = rx_pn_runtime_create(1);
    if (runtime == NULL) {
        fprintf(stderr, "rx_pn_runtime_create failed\n");
        return 1;
    }

    net = rx_pn_net_create("queue", initial_places, 2, transitions, 2, &app);
    if (net == NULL) {
        fprintf(stderr, "rx_pn_net_create failed\n");
        rx_pn_runtime_destroy(runtime);
        return 1;
    }

    if (rx_pn_runtime_add_net(runtime, net) != 0) {
        fprintf(stderr, "rx_pn_runtime_add_net failed\n");
        rx_pn_net_destroy(net);
        rx_pn_runtime_destroy(runtime);
        return 1;
    }

    printf("init: places=[%d,%d] fired=%d\n", net->places[P_IDLE], net->places[P_BUSY], app.fired_count);

    inputs.add = 1;
    inputs.remove = 0;
    rx_pn_tick(runtime);
    printf("after add: places=[%d,%d] fired=%d\n", net->places[P_IDLE], net->places[P_BUSY], app.fired_count);

    inputs.add = 0;
    inputs.remove = 1;
    rx_pn_tick(runtime);
    printf("after remove: places=[%d,%d] fired=%d\n", net->places[P_IDLE], net->places[P_BUSY], app.fired_count);

    rx_pn_net_destroy(net);
    rx_pn_runtime_destroy(runtime);
    return 0;
}
