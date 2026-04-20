// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

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
    gpio_num_t button_gpio;
    gpio_num_t light_gpio;
    int latched_event;
    rx_pn_net *net;
} light_pn_data;

static light_pn_data s_data[RXNET_MAX_RUNTIME_NODES];
static size_t s_count = 0;

static void light_pn_latch_inputs(rx_pn_context *ctx, void *user) {
    light_pn_data *data = (light_pn_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->latched_event = app_driver_latch_button_event(data->button_gpio);
    if (data->latched_event) {
        data->net->places[P_REQUEST]++;
    }
}

static void light_pn_dump_outputs(rx_pn_context *ctx, void *user) {
    light_pn_data *data = (light_pn_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    app_driver_set_light(data->light_gpio, data->net->places[P_ON] > 0);
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

    if (net == NULL || s_count >= RXNET_MAX_RUNTIME_NODES) {
        return -1;
    }

    data = &s_data[s_count++];

    if (app_driver_init_button(button_gpio) != ESP_OK ||
        app_driver_init_light(light_gpio) != ESP_OK) {
        --s_count;
        return -1;
    }

    data->button_gpio = button_gpio;
    data->light_gpio = light_gpio;
    data->latched_event = 0;
    data->net = net;

    if (rx_pn_net_init(net, "light", 
                       initial_places, LIGHT_PLACE_COUNT,
                       transitions, LIGHT_TRANSITION_COUNT, data,
                       light_pn_latch_inputs, light_pn_dump_outputs) != 0) {
        --s_count;
        return -1;
    }

    return 0;
}
