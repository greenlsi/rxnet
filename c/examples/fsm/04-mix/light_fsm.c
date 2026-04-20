// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#include "light_fsm.h"

#include <stddef.h>

#include "rxnet/config.h"

typedef struct {
    gpio_num_t button_gpio;
    gpio_num_t light_gpio;
    bool latched_event;
    bool event_consumed;
    int output_enabled;
} light_machine_data;

static light_machine_data s_data[RXNET_MAX_RUNTIME_NODES];
static size_t s_count = 0;

enum {
    LIGHT_STATE_OFF = 0,
    LIGHT_STATE_ON = 1,
};

static void light_machine_latch_inputs(rx_fsm_context *ctx, void *user) {
    light_machine_data *data = (light_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->latched_event = app_driver_latch_button_event(data->button_gpio);
    data->event_consumed = false;
}

static int button_pressed(const rx_fsm_context *ctx, void *user) {
    const light_machine_data *data = (const light_machine_data *)user;

    (void)ctx;
    return data != NULL && data->latched_event;
}

static void light_on(rx_fsm_context *ctx, void *user) {
    light_machine_data *data = (light_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }
    data->output_enabled = 1;
    data->event_consumed = true;
}

static void light_off(rx_fsm_context *ctx, void *user) {
    light_machine_data *data = (light_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }
    data->output_enabled = 0;
    data->event_consumed = true;
}

static void light_machine_dump_outputs(rx_fsm_context *ctx, void *user) {
    light_machine_data *data = (light_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    app_driver_set_light(data->light_gpio, data->output_enabled);
    if (data->event_consumed) {
        app_driver_clear_button_event(data->button_gpio);
    }
}

void light_fsm_create(
    rx_fsm_machine *machine,
    gpio_num_t button_gpio,
    gpio_num_t light_gpio
) {
    static const rx_fsm_transition transitions[] = {
        {LIGHT_STATE_OFF, LIGHT_STATE_ON, button_pressed, light_on},
        {LIGHT_STATE_ON, LIGHT_STATE_OFF, button_pressed, light_off},
    };
    light_machine_data *data;

    if (machine == NULL || s_count >= RXNET_MAX_RUNTIME_NODES) {
        return;
    }
    data = &s_data[s_count++];

    data->button_gpio = button_gpio;
    data->light_gpio = light_gpio;
    data->latched_event = false;
    data->event_consumed = false;
    data->output_enabled = 0;

    if (app_driver_init_button(button_gpio) != ESP_OK ||
        app_driver_init_light(light_gpio) != ESP_OK) {
        --s_count;
        return;
    }

    rx_fsm_machine_init(
        machine,
        "light",
        LIGHT_STATE_OFF,
        transitions,
        sizeof(transitions) / sizeof(transitions[0]),
        data,
        light_machine_latch_inputs,
        light_machine_dump_outputs
    );
}
