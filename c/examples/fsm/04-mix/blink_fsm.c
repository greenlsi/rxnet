// Copyright 2026 Jose M. Moya <jm.moya@upm.es>
// SPDX-License-Identifier: MIT

#include "blink_fsm.h"

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "rxnet/config.h"
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#endif

typedef struct {
    rx_fsm_machine *machine;   /* needed to read machine->state in callbacks */
    gpio_num_t button_gpio;
    gpio_num_t light_gpio;
    bool latched_event;
    bool event_consumed;
    int output_enabled;
    unsigned int base_hz;
    uint64_t now_ms;
    uint64_t next_toggle_ms;
} blink_machine_data;

static blink_machine_data s_data[RXNET_MAX_RUNTIME_NODES];
static size_t s_count = 0;

enum {
    BLINK_STATE_OFF = 0,
    BLINK_STATE_X1 = 1,
    BLINK_STATE_X2 = 2,
};

static uint64_t blink_now_ms(void) {
#ifdef ESP_PLATFORM
    return (uint64_t)(esp_timer_get_time() / 1000);
#else
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000u) + ((uint64_t)ts.tv_nsec / 1000000u);
#endif
}

static uint64_t half_period_ms_for_state(int state, unsigned int base_hz) {
    uint64_t hz = (uint64_t)base_hz;
    uint64_t half_period_ms;

    if (hz == 0u) {
        hz = 1u;
    }

    if (state == BLINK_STATE_X2) {
        hz *= 2u;
    }

    half_period_ms = 500u / hz;
    if (half_period_ms == 0u) {
        half_period_ms = 1u;
    }

    return half_period_ms;
}

static void blink_machine_latch_inputs(rx_fsm_context *ctx, void *user) {
    blink_machine_data *data = (blink_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->now_ms = blink_now_ms();
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
           data->next_toggle_ms > 0u &&
           data->now_ms >= data->next_toggle_ms;
}

static void enter_blink_x1(rx_fsm_context *ctx, void *user) {
    blink_machine_data *data = (blink_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->event_consumed = true;
    data->output_enabled = 1;
    data->next_toggle_ms = data->now_ms + half_period_ms_for_state(BLINK_STATE_X1, data->base_hz);
}

static void enter_blink_x2(rx_fsm_context *ctx, void *user) {
    blink_machine_data *data = (blink_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->event_consumed = true;
    data->output_enabled = 1;
    data->next_toggle_ms = data->now_ms + half_period_ms_for_state(BLINK_STATE_X2, data->base_hz);
}

static void toggle_light(rx_fsm_context *ctx, void *user) {
    blink_machine_data *data = (blink_machine_data *)user;
    uint64_t half_period_ms;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->output_enabled = !data->output_enabled;
    half_period_ms = half_period_ms_for_state(data->machine->state, data->base_hz);
    if (data->next_toggle_ms == 0u) {
        data->next_toggle_ms = data->now_ms + half_period_ms;
        return;
    }
    data->next_toggle_ms += half_period_ms;
}

static void enter_off(rx_fsm_context *ctx, void *user) {
    blink_machine_data *data = (blink_machine_data *)user;

    (void)ctx;
    if (data == NULL) {
        return;
    }

    data->event_consumed = true;
    data->output_enabled = 0;
    data->next_toggle_ms = 0u;
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
    data->now_ms = 0;
    data->next_toggle_ms = 0;

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
        data->next_toggle_ms = blink_now_ms() +
            half_period_ms_for_state(machine->state, base_hz);
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
