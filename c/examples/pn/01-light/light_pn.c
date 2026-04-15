#include "light_pn.h"

#include <stddef.h>
#include <string.h>

#include "rxnet/config.h"

enum {
    P_OFF     = 0,
    P_ON      = 1,
    P_REQUEST = 2,
};

#define LIGHT_PLACE_COUNT 3u
#define LIGHT_TRANSITION_COUNT 2u

typedef struct {
    int in_use;
    gpio_num_t button_gpio;
    gpio_num_t light_gpio;
    int latched_event;
    rx_pn_net *net;
} light_pn_data;

static light_pn_data s_data[RXNET_MAX_RUNTIME_NODES];

static light_pn_data *find_or_alloc(rx_pn_net *net) {
    size_t i;

    for (i = 0; i < RXNET_MAX_RUNTIME_NODES; ++i) {
        if (s_data[i].in_use && s_data[i].net == net) {
            return &s_data[i];
        }
    }

    for (i = 0; i < RXNET_MAX_RUNTIME_NODES; ++i) {
        if (!s_data[i].in_use) {
            memset(&s_data[i], 0, sizeof(s_data[i]));
            s_data[i].in_use = 1;
            s_data[i].net = net;
            return &s_data[i];
        }
    }

    return NULL;
}

static void light_pn_latch_inputs(rx_node *node, rx_context *ctx) {
    rx_pn_net *net = (rx_pn_net *)node;
    light_pn_data *data = (light_pn_data *)net->user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->latched_event = app_driver_latch_button_event(data->button_gpio);
    if (data->latched_event) {
        net->places[P_REQUEST]++;
    }
}

static void light_pn_dump_outputs(rx_node *node, rx_context *ctx) {
    rx_pn_net *net = (rx_pn_net *)node;
    light_pn_data *data = (light_pn_data *)net->user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    app_driver_set_light(data->light_gpio, net->places[P_ON] > 0);
    if (data->latched_event) {
        app_driver_clear_button_event(data->button_gpio);
    }
}

static const rx_pn_arc t_turn_on_consume[]  = {{P_OFF, 1}, {P_REQUEST, 1}};
static const rx_pn_arc t_turn_on_produce[]  = {{P_ON, 1}};
static const rx_pn_arc t_turn_off_consume[] = {{P_ON, 1}, {P_REQUEST, 1}};
static const rx_pn_arc t_turn_off_produce[] = {{P_OFF, 1}};

static const rx_pn_transition transitions[] = {
    {t_turn_on_consume,  2, t_turn_on_produce,  1, NULL, NULL},
    {t_turn_off_consume, 2, t_turn_off_produce, 1, NULL, NULL},
};

static const int initial_places[] = {1, 0, 0};

int light_pn_init(
    rx_pn_net *net,
    gpio_num_t button_gpio,
    gpio_num_t light_gpio
) {
    light_pn_data *data;

    if (net == NULL) {
        return -1;
    }

    data = find_or_alloc(net);
    if (data == NULL) {
        return -1;
    }

    if (app_driver_init_button(button_gpio) != ESP_OK ||
        app_driver_init_light(light_gpio) != ESP_OK) {
        data->in_use = 0;
        return -1;
    }

    data->button_gpio = button_gpio;
    data->light_gpio = light_gpio;
    data->latched_event = 0;

    if (rx_pn_net_init(net, "light", initial_places, LIGHT_PLACE_COUNT,
                       transitions, LIGHT_TRANSITION_COUNT, data) != 0) {
        data->in_use = 0;
        return -1;
    }

    rx_node_set_latch_inputs_callback(&net->node, light_pn_latch_inputs);
    rx_node_set_dump_outputs_callback(&net->node, light_pn_dump_outputs);
    return 0;
}
