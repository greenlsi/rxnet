// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "blink_fsm.h"

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
    unsigned int base_hz;
    rx_tick_t now;
    rx_tick_t next_toggle;
} blink_machine_data;

static blink_machine_data s_data[RXNET_MAX_RUNTIME_NODES];
static size_t s_count = 0;

enum {
    BLINK_STATE_OFF = 0,
    BLINK_STATE_X1 = 1,
    BLINK_STATE_X2 = 2,
};

static uint64_t half_period_us_for_state(int state, unsigned int base_hz) {
    uint64_t hz = (uint64_t)base_hz;
    uint64_t half_period_us;

    if (hz == 0u) {
        hz = 1u;
    }

    if (state == BLINK_STATE_X2) {
        hz *= 2u;
    }

    half_period_us = 500000u / hz;
    if (half_period_us == 0u) {
        half_period_us = 1u;
    }

    return half_period_us;
}

static void blink_machine_latch_inputs(rx_fsm_context *ctx, void *user) {
    blink_machine_data *data = (blink_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->now = rx_tick_now();
    data->latched_event = app_driver_latch_button_event(data->button_gpio);
    data->event_consumed = false;
}

static int button_pressed(const rx_fsm_context *ctx, void *user) {
    const blink_machine_data *data = (const blink_machine_data *)user;

    (void)ctx;
    return data != NULL && data->latched_event;
}

static int toggle_due(const rx_fsm_context *ctx, void *user) {
    const blink_machine_data *data = (const blink_machine_data *)user;

    (void)ctx;
    return data != NULL &&
           data->machine->state != BLINK_STATE_OFF &&
           data->next_toggle > 0u &&
           data->now >= data->next_toggle;
}

static void enter_blink_x1(rx_fsm_context *ctx, void *user) {
    blink_machine_data *data = (blink_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->event_consumed = true;
    data->output_enabled = 1;
    data->next_toggle = rx_tick_add_us(data->now, half_period_us_for_state(BLINK_STATE_X1, data->base_hz));
}

static void enter_blink_x2(rx_fsm_context *ctx, void *user) {
    blink_machine_data *data = (blink_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->event_consumed = true;
    data->output_enabled = 1;
    data->next_toggle = rx_tick_add_us(data->now, half_period_us_for_state(BLINK_STATE_X1, data->base_hz));
}

static void toggle_light(rx_fsm_context *ctx, void *user) {
    blink_machine_data *data = (blink_machine_data *)user;
    uint64_t half_period_us;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->output_enabled = !data->output_enabled;
    half_period_us = half_period_us_for_state(data->machine->state, data->base_hz);
    if (data->next_toggle == 0u) {
        data->next_toggle = rx_tick_add_us(data->now, half_period_us);
        return;
    }
    data->next_toggle = rx_tick_add_us(data->next_toggle, half_period_us);
}

static void enter_off(rx_fsm_context *ctx, void *user) {
    blink_machine_data *data = (blink_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->event_consumed = true;
    data->output_enabled = 0;
    data->next_toggle = 0u;
}

static void blink_machine_dump_outputs(rx_fsm_context *ctx, void *user) {
    blink_machine_data *data = (blink_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    app_driver_set_light(data->light_gpio, data->output_enabled);
    if (data->event_consumed) {
        app_driver_clear_button_event(data->button_gpio);
    }
}

void blink_fsm_create(
    rx_fsm_machine *machine,
    gpio_num_t button_gpio,
    gpio_num_t light_gpio,
    unsigned int base_hz
) {
    static const rx_fsm_transition transitions[] = {
        {BLINK_STATE_OFF, BLINK_STATE_X1, button_pressed, enter_blink_x1},
        {BLINK_STATE_X1, BLINK_STATE_X2, button_pressed, enter_blink_x2},
        {BLINK_STATE_X1, BLINK_STATE_X1, toggle_due, toggle_light},
        {BLINK_STATE_X2, BLINK_STATE_OFF, button_pressed, enter_off},
        {BLINK_STATE_X2, BLINK_STATE_X2, toggle_due, toggle_light},
    };
    blink_machine_data *data;

    if (machine == NULL || base_hz == 0u || s_count >= RXNET_MAX_RUNTIME_NODES) {
        return;
    }
    data = &s_data[s_count++];

    data->machine = machine;
    data->button_gpio = button_gpio;
    data->light_gpio = light_gpio;
    data->latched_event = false;
    data->event_consumed = false;
    data->output_enabled = 0;
    data->base_hz = base_hz;
    data->now = 0;
    data->next_toggle = 0;

    if (app_driver_init_button(button_gpio) != ESP_OK ||
        app_driver_init_light(light_gpio) != ESP_OK) {
        --s_count;
        return;
    }

    rx_fsm_machine_init(
        machine,
        "blink",
        BLINK_STATE_OFF,
        transitions,
        sizeof(transitions) / sizeof(transitions[0]),
        data,
        blink_machine_latch_inputs,
        blink_machine_dump_outputs
    );
}

int blink_fsm_set_base_hz(rx_fsm_machine *machine, unsigned int base_hz) {
    blink_machine_data *data;

    if (machine == NULL || machine->user == NULL || base_hz == 0u) {
        return -1;
    }
    data = (blink_machine_data *)machine->user;
    data->base_hz = base_hz;
    if (machine->state != BLINK_STATE_OFF) {
        data->next_toggle = rx_tick_add_us(data->now, half_period_us_for_state(data->machine->state, data->base_hz));
    }
    return 0;
}

unsigned int blink_fsm_get_base_hz(const rx_fsm_machine *machine) {
    if (machine == NULL || machine->user == NULL) {
        return 0u;
    }
    return ((const blink_machine_data *)machine->user)->base_hz;
}

int blink_fsm_get_output_enabled(const rx_fsm_machine *machine) {
    if (machine == NULL || machine->user == NULL) {
        return 0;
    }
    return ((const blink_machine_data *)machine->user)->output_enabled;
}
