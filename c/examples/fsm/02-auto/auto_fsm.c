// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "auto_fsm.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "rxnet/config.h"
#include "rxnet/port.h"

typedef struct {
    rx_fsm_machine *machine;   /* needed to read machine->state in callbacks */
    gpio_num_t button_gpio;
    gpio_num_t light_gpio;
    bool latched_event;
    bool event_consumed;
    int output_enabled;
    long auto_off_timeout;
    rx_tick_t now;
    rx_tick_t wait_end;
} auto_machine_data;

static auto_machine_data s_data[RXNET_MAX_RUNTIME_NODES];
static size_t s_count = 0;

enum {
    AUTO_STATE_OFF = 0,
    AUTO_STATE_ON = 1,
};

static void auto_machine_latch_inputs(rx_fsm_context *ctx, void *user) {
    auto_machine_data *data = (auto_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->now = rx_tick_now();
    data->latched_event = app_driver_latch_button_event(data->button_gpio);
    data->event_consumed = false;

    if (data->latched_event) {
        data->wait_end = rx_tick_add_us(data->now, data->auto_off_timeout);
    }
}

static int button_pressed(const rx_fsm_context *ctx, void *user) {
    const auto_machine_data *data = (const auto_machine_data *)user;

    (void)ctx;
    return data != NULL && data->latched_event;
}

static int auto_off_elapsed(const rx_fsm_context *ctx, void *user) {
    const auto_machine_data *data = (const auto_machine_data *)user;

    (void)ctx;
    return data != NULL &&
           data->auto_off_timeout > 0 &&
           data->now >= data->wait_end;
}

static void auto_on(rx_fsm_context *ctx, void *user) {
    auto_machine_data *data = (auto_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->output_enabled = 1;
    data->event_consumed = true;
    data->wait_end = rx_tick_add_us(data->now, data->auto_off_timeout);
}

static void auto_off(rx_fsm_context *ctx, void *user) {
    auto_machine_data *data = (auto_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->output_enabled = 0;
}

static void auto_machine_dump_outputs(rx_fsm_context *ctx, void *user) {
    auto_machine_data *data = (auto_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    app_driver_set_light(data->light_gpio, data->output_enabled);
    if (data->event_consumed) {
        app_driver_clear_button_event(data->button_gpio);
    }
}

void auto_fsm_create(
    rx_fsm_machine *machine,
    gpio_num_t button_gpio,
    gpio_num_t light_gpio,
    unsigned int auto_off_timeout_ms
) {
    static const rx_fsm_transition transitions[] = {
        {AUTO_STATE_OFF, AUTO_STATE_ON, button_pressed, auto_on},
        {AUTO_STATE_ON, AUTO_STATE_ON, button_pressed, auto_on},
        {AUTO_STATE_ON, AUTO_STATE_OFF, auto_off_elapsed, auto_off},
    };
    auto_machine_data *data;

    if (machine == NULL || auto_off_timeout_ms == 0 ||
        s_count >= RXNET_MAX_RUNTIME_NODES) {
        return;
    }
    data = &s_data[s_count++];

    data->machine = machine;
    data->button_gpio = button_gpio;
    data->light_gpio = light_gpio;
    data->latched_event = false;
    data->event_consumed = false;
    data->output_enabled = 0;
    data->auto_off_timeout = auto_off_timeout_ms * 1000L;
    data->now = 0;
    data->wait_end = 0;

    if (app_driver_init_button(button_gpio) != ESP_OK ||
        app_driver_init_light(light_gpio) != ESP_OK) {
        --s_count;
        return;
    }

    rx_fsm_machine_init(
        machine,
        "auto",
        AUTO_STATE_OFF,
        transitions,
        sizeof(transitions) / sizeof(transitions[0]),
        data,
        auto_machine_latch_inputs,
        auto_machine_dump_outputs
    );
}

int auto_fsm_set_auto_off_timeout_ms(rx_fsm_machine *machine, unsigned int auto_off_timeout_ms) {
    auto_machine_data *data;

    if (machine == NULL || machine->user == NULL || auto_off_timeout_ms == 0) {
        return -1;
    }
    data = (auto_machine_data *)machine->user;
    data->auto_off_timeout = auto_off_timeout_ms * 1000L;
    if (machine->state == AUTO_STATE_ON) {
        data->wait_end = rx_tick_add_us(data->now, data->auto_off_timeout);
    }
    return 0;
}

unsigned int auto_fsm_get_auto_off_timeout_ms(const rx_fsm_machine *machine) {
    if (machine == NULL || machine->user == NULL) {
        return 0;
    }
    return ((const auto_machine_data *)machine->user)->auto_off_timeout / 1000L;
}
